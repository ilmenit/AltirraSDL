"""Process-level desktop IPC regressions; pass a compiled fixture as argv[1]."""
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest

FIXTURE = str(Path(sys.argv.pop(1)).resolve())


def windows_elevated():
    if os.name != "nt":
        return False
    import ctypes
    from ctypes import wintypes
    kernel = ctypes.windll.kernel32
    security = ctypes.windll.advapi32
    kernel.GetCurrentProcess.restype = wintypes.HANDLE
    security.OpenProcessToken.argtypes = [wintypes.HANDLE, wintypes.DWORD,
                                         ctypes.POINTER(wintypes.HANDLE)]
    security.GetTokenInformation.argtypes = [wintypes.HANDLE, ctypes.c_int,
                                           wintypes.LPVOID, wintypes.DWORD,
                                           ctypes.POINTER(wintypes.DWORD)]
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    token = wintypes.HANDLE()
    if not security.OpenProcessToken(kernel.GetCurrentProcess(), 8, ctypes.byref(token)):
        return True  # Production also declines IPC when elevation is unknown.
    try:
        elevation, size = wintypes.DWORD(), wintypes.DWORD()
        known = security.GetTokenInformation(token, 20, ctypes.byref(elevation),
                                             ctypes.sizeof(elevation), ctypes.byref(size))
        return not known or bool(elevation.value)
    finally:
        kernel.CloseHandle(token)


WINDOWS_ELEVATED = windows_elevated()


@unittest.skipIf(WINDOWS_ELEVATED, "Elevated Windows launches deliberately bypass instance IPC")
class InstanceReuseTests(unittest.TestCase):
    def setUp(self):
        self.home = tempfile.TemporaryDirectory(prefix="altirra-instance-test-")
        self.env = dict(os.environ, HOME=self.home.name,
                        LOCALAPPDATA=self.home.name)
        self.processes = []

    def tearDown(self):
        for process in self.processes:
            process.terminate()
            process.communicate(timeout=5)
        self.home.cleanup()

    def server(self, mode="server"):
        process = subprocess.Popen([FIXTURE, mode], env=self.env,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   text=True)
        self.processes.append(process)
        self.assertEqual(process.stdout.readline().strip(), "CONTINUE")
        return process

    def client(self, *args, cwd=None):
        return subprocess.run([FIXTURE, *args], env=self.env, cwd=cwd,
                              capture_output=True, text=True, timeout=20)

    def endpoint(self):
        directory = (Path(self.home.name) / "AltirraSDL" / "instance"
                     if os.name == "nt" else
                     Path(self.home.name) / ".altirra-instance")
        return directory / "endpoint"

    def read_request(self, server):
        line = server.stdout.readline().strip()
        self.assertTrue(line.startswith("REQUEST "), line)
        count = int(line.split()[1])
        origin = bytes.fromhex(server.stdout.readline().strip()).decode()
        args = [bytes.fromhex(server.stdout.readline().strip()).decode()
                for _ in range(count)]
        return origin, args

    def test_arguments_and_origin_preserved_and_overrides_removed(self):
        server = self.server()
        with tempfile.TemporaryDirectory(prefix="at media origin ") as origin:
            filename = "game ä.xex" if os.name != "nt" else "game spaces.xex"
            result = self.client("client", "--Si", "--run", filename,
                                 "--type", 'hello "world"\n~', cwd=origin)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip(), "FORWARDED")
            cwd, args = self.read_request(server)
            self.assertEqual(Path(cwd).resolve(), Path(origin).resolve())
            self.assertEqual(args[1:], ["client", "--run", filename,
                                       "--type", 'hello "world"\n~'])

    def test_disabled_override_keeps_multiple_instances(self):
        server = self.server()
        self.assertEqual(self.client("independent").stdout.strip(), "CONTINUE")
        self.assertEqual(self.client("client", "--nosi").stdout.strip(), "CONTINUE")
        self.assertEqual(self.client("client").stdout.strip(), "FORWARDED")
        self.read_request(server)

    def test_crashed_owner_is_replaced(self):
        server = self.server()
        server.kill()
        server.communicate(timeout=5)
        self.processes.remove(server)
        replacement = self.server()
        self.assertEqual(self.client("client").stdout.strip(), "FORWARDED")
        self.read_request(replacement)

    def test_owner_exit_during_handoff_elects_client_as_replacement(self):
        owner = self.server("idle-server")
        client = subprocess.Popen([FIXTURE, "client"], env=self.env,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        time.sleep(0.1)
        self.assertIsNone(client.poll(), "Client must await the still-running owner")
        owner.kill()
        owner.communicate(timeout=5)
        self.processes.remove(owner)
        stdout, stderr = client.communicate(timeout=5)
        self.assertEqual(client.returncode, 0, stderr)
        self.assertEqual(stdout.strip(), "CONTINUE")

    def test_untrusted_and_partial_packets_do_not_block_valid_launch(self):
        server = self.server()
        port, _ = self.endpoint().read_text().split()
        with socket.create_connection(("127.0.0.1", int(port))) as partial:
            partial.sendall(b"\x00")
            with socket.create_connection(("127.0.0.1", int(port))) as invalid:
                invalid.sendall(struct.pack("!I", 4) + b"nope")
                invalid.settimeout(5)
                self.assertEqual(invalid.recv(1), b"\0")
            self.assertEqual(self.client("client").stdout.strip(), "FORWARDED")
            self.read_request(server)

    def test_simultaneous_startup_claims_one_owner(self):
        processes = [subprocess.Popen([FIXTURE, "server"], env=self.env,
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                      text=True) for _ in range(2)]
        self.processes.extend(processes)
        statuses = [process.stdout.readline().strip() for process in processes]
        self.assertEqual(sorted(statuses), ["CONTINUE", "FORWARDED"])
        owner = processes[statuses.index("CONTINUE")]
        self.assertEqual(self.read_request(owner)[1][1], "server")

    def test_simultaneous_clients_each_delivered_once(self):
        server = self.server()
        clients = [subprocess.Popen([FIXTURE, "client", str(i)], env=self.env,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                    text=True) for i in range(4)]
        for client in clients:
            stdout, stderr = client.communicate(timeout=20)
            self.assertEqual(client.returncode, 0, stderr)
            self.assertEqual(stdout.strip(), "FORWARDED")
        values = [self.read_request(server)[1][-1] for _ in clients]
        self.assertEqual(sorted(values), ["0", "1", "2", "3"])


@unittest.skipUnless(WINDOWS_ELEVATED, "Requires an elevated Windows runner")
class ElevatedWindowsTests(unittest.TestCase):
    def test_elevated_launches_remain_independent_without_publishing(self):
        with tempfile.TemporaryDirectory(prefix="altirra-elevated-instance-") as temp:
            env = dict(os.environ, HOME=temp, LOCALAPPDATA=temp)
            owner = subprocess.Popen([FIXTURE, "server", "--si"], env=env,
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                     text=True)
            try:
                self.assertEqual(owner.stdout.readline().strip(), "CONTINUE")
                launch = subprocess.run([FIXTURE, "client", "--si"], env=env,
                                        capture_output=True, text=True, timeout=5)
                self.assertEqual(launch.returncode, 0, launch.stderr)
                self.assertEqual(launch.stdout.strip(), "CONTINUE")
                self.assertFalse((Path(temp) / "AltirraSDL" / "instance" / "endpoint").exists())
                self.assertIsNone(owner.poll())
            finally:
                owner.terminate()
                owner.communicate(timeout=5)


if __name__ == "__main__":
    unittest.main()
