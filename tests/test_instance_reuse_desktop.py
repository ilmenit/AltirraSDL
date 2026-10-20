"""Real-emulator instance reuse integration test.

Run: python3 tests/test_instance_reuse_desktop.py /absolute/path/to/AltirraSDL
Uses isolated settings/media, an ordinary desktop primary, and the real
command-line debugger to verify RAM loaded from a forwarded Unicode XEX path.
On Linux/X11 it also verifies that hand-off restores keyboard focus. No primary
--test-mode or bridge is used. A separate test-mode process verifies exclusion.
"""
import ctypes
import ctypes.util
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest

BINARY = str(Path(sys.argv.pop(1)).resolve())


def eventually(predicate, message, timeout=20):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.05)
    raise AssertionError(message)


class X11Message(ctypes.Structure):
    _fields_ = [("type", ctypes.c_int), ("serial", ctypes.c_ulong),
                ("send_event", ctypes.c_int), ("display", ctypes.c_void_p),
                ("window", ctypes.c_ulong), ("message_type", ctypes.c_ulong),
                ("format", ctypes.c_int), ("data", ctypes.c_long * 5)]


class X11Event(ctypes.Union):
    _fields_ = [("message", X11Message), ("padding", ctypes.c_long * 24)]


class X11Windows:
    """Small Xlib adapter: only reads/focuses windows owned by test processes."""
    def __init__(self):
        self.x = ctypes.CDLL(ctypes.util.find_library("X11"))
        # A window can disappear between XQueryTree and property lookup.
        # Ignore asynchronous BadWindow errors and let polling retry; Xlib's
        # default handler would exit Python before process cleanup can run.
        self._error_handler = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p,
                                              ctypes.c_void_p)(lambda *_: 0)
        self.x.XSetErrorHandler.argtypes = [ctypes.c_void_p]
        self.x.XSetErrorHandler.restype = ctypes.c_void_p
        self._previous_error_handler = self.x.XSetErrorHandler(
            ctypes.cast(self._error_handler, ctypes.c_void_p))
        self.x.XOpenDisplay.argtypes = [ctypes.c_char_p]
        self.x.XOpenDisplay.restype = ctypes.c_void_p
        self.display = self.x.XOpenDisplay(None)
        if not self.display:
            raise RuntimeError("Unable to connect to the X11 display")
        self.x.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
        self.x.XDefaultRootWindow.restype = ctypes.c_ulong
        self.root = self.x.XDefaultRootWindow(self.display)
        self.x.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
        self.x.XInternAtom.restype = ctypes.c_ulong
        self.pid_atom = self.x.XInternAtom(self.display, b"_NET_WM_PID", 0)
        self.active_atom = self.x.XInternAtom(self.display, b"_NET_ACTIVE_WINDOW", 0)
        self.x.XSendEvent.argtypes = [ctypes.c_void_p, ctypes.c_ulong,
                                     ctypes.c_int, ctypes.c_long,
                                     ctypes.POINTER(X11Event)]
        self.x.XQueryTree.argtypes = [ctypes.c_void_p, ctypes.c_ulong,
                                    ctypes.POINTER(ctypes.c_ulong),
                                    ctypes.POINTER(ctypes.c_ulong),
                                    ctypes.POINTER(ctypes.POINTER(ctypes.c_ulong)),
                                    ctypes.POINTER(ctypes.c_uint)]
        self.x.XGetWindowProperty.argtypes = [
            ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_long,
            ctypes.c_long, ctypes.c_int, ctypes.c_ulong,
            ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_int),
            ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_ulong),
            ctypes.POINTER(ctypes.POINTER(ctypes.c_ubyte))]
        self.x.XFree.argtypes = [ctypes.c_void_p]
        self.x.XGetInputFocus.argtypes = [ctypes.c_void_p,
                                        ctypes.POINTER(ctypes.c_ulong),
                                        ctypes.POINTER(ctypes.c_int)]
        self.x.XSetInputFocus.argtypes = [ctypes.c_void_p, ctypes.c_ulong,
                                        ctypes.c_int, ctypes.c_ulong]
        self.x.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
        self.x.XCloseDisplay.argtypes = [ctypes.c_void_p]

    def tree(self, window):
        root, parent = ctypes.c_ulong(), ctypes.c_ulong()
        children = ctypes.POINTER(ctypes.c_ulong)()
        count = ctypes.c_uint()
        if not self.x.XQueryTree(self.display, window, ctypes.byref(root),
                                ctypes.byref(parent), ctypes.byref(children),
                                ctypes.byref(count)):
            return 0, []
        result = list(children[:count.value])
        if children:
            self.x.XFree(children)
        return parent.value, result

    def pid(self, window):
        kind, count, remaining = ctypes.c_ulong(), ctypes.c_ulong(), ctypes.c_ulong()
        fmt = ctypes.c_int()
        data = ctypes.POINTER(ctypes.c_ubyte)()
        self.x.XGetWindowProperty(self.display, window, self.pid_atom, 0, 1,
                                 0, 0, ctypes.byref(kind), ctypes.byref(fmt),
                                 ctypes.byref(count), ctypes.byref(remaining),
                                 ctypes.byref(data))
        value = (ctypes.cast(data, ctypes.POINTER(ctypes.c_ulong))[0]
                 if data and count.value and fmt.value == 32 else 0)
        if data:
            self.x.XFree(data)
        return value

    def find(self, pid):
        pending = [(self.root, 0)]
        while pending:
            window, depth = pending.pop()
            if self.pid(window) == pid:
                return window
            if depth < 4:
                pending.extend((child, depth + 1) for child in self.tree(window)[1])
        return None

    def focus(self, window):
        event = X11Event()
        event.message.type = 33  # ClientMessage
        event.message.display = self.display
        event.message.window = window
        event.message.message_type = self.active_atom
        event.message.format = 32
        event.message.data[0] = 2  # Explicit test-controller activation request.
        self.x.XSendEvent(self.display, self.root, 0, (1 << 19) | (1 << 20),
                          ctypes.byref(event))
        self.x.XSync(self.display, 0)

    def focused(self, window):
        focused, revert = ctypes.c_ulong(), ctypes.c_int()
        self.x.XGetInputFocus(self.display, ctypes.byref(focused), ctypes.byref(revert))
        current = focused.value
        while current and current != self.root:
            if current == window:
                return True
            current = self.tree(current)[0]
        return False

    def close(self):
        self.x.XCloseDisplay(self.display)
        self.x.XSetErrorHandler(self._previous_error_handler)


class DesktopInstanceReuse(unittest.TestCase):
    @unittest.skipIf(os.name == "nt", "INI isolation is POSIX-only; Windows uses its registry")
    def test_real_emulator_handoff_and_independent_launches(self):
        with tempfile.TemporaryDirectory(prefix="altirra-instance-desktop-") as temp:
            root = Path(temp)
            config = root / "config" / "altirra"
            config.mkdir(parents=True)
            settings = config / "settings.ini"
            settings.write_text(
                '[User\\AltirraSDL]\n"ShownSetupWizard" = 1\n\n'
                '[User\\AltirraSDL\\Settings]\n'
                '"Startup: Reuse program instance" = 1\n', encoding="utf-8")
            env = dict(os.environ, HOME=temp, XDG_CONFIG_HOME=str(root / "config"),
                       SDL_AUDIO_DRIVER="dummy")
            windows = None
            if sys.platform.startswith("linux") and os.environ.get("DISPLAY"):
                env["SDL_VIDEO_DRIVER"] = "x11"
                windows = X11Windows()
            elif sys.platform.startswith("linux"):
                env["SDL_VIDEO_DRIVER"] = "offscreen"
            processes, logs = [], []

            def launch(*args, cwd=None):
                log = open(root / f"process-{len(logs)}.log", "wb")
                logs.append(log)
                proc = subprocess.Popen([BINARY, "--renderer", "sdlrenderer", *args],
                                        env=env, cwd=cwd, stdout=log, stderr=log)
                processes.append(proc)
                return proc

            def forwarded(*args, cwd=None):
                proc = launch(*args, cwd=cwd)
                self.assertEqual(proc.wait(timeout=25), 0,
                                 "The secondary launch failed to hand off its command line")
                self.assertIsNone(primary.poll(), "Primary exited during instance hand-off")

            try:
                primary = launch("--nobasic", "--nodebugbrkrun")
                endpoint = Path(temp) / ".altirra-instance" / "endpoint"
                eventually(lambda: endpoint.exists() or primary.poll() is not None,
                           "The desktop primary never published an instance endpoint")
                self.assertIsNone(primary.poll(), "Desktop primary failed during startup")
                first_window = (eventually(lambda: windows.find(primary.pid),
                                          "Primary desktop window did not appear")
                                if windows else None)
                # The loader installs this segment and an infinite loop at $0600.
                # A later forwarded debugger command observes actual emulated RAM.
                signature = bytes([0x4C, 0x00, 0x06, 0x41, 0x54, 0x53, 0x44, 0x4C])
                media = root / "relative media"
                media.mkdir()
                filename = "späce 日本語.xex"
                (media / filename).write_bytes(
                    b"\xff\xff" + struct.pack("<HH", 0x600, 0x607) + signature +
                    struct.pack("<HHH", 0x2E0, 0x2E1, 0x600))
                def verify_ram(expected, label):
                    observed_bytes = None
                    for attempt in range(16):
                        time.sleep(0.5)
                        observed = root / f"{label}-{attempt}.bin"
                        forwarded("--debugcmd", f'.writemem "{observed}" 0600 L8')
                        eventually(lambda: observed.exists() and observed.stat().st_size == 8,
                                   "Forwarded debugger command did not execute in the primary")
                        observed_bytes = observed.read_bytes()
                        if observed_bytes == expected:
                            return
                    self.assertEqual(observed_bytes, expected,
                                     "Forwarded device did not load its sender-relative executable")

                # EXE loader is a standalone device whose path setting opens
                # a real file. Boot through SIO after both add and reconfigure,
                # observing RAM to prove the correct executable was selected.
                for action, marker in (("adddevice", b"ADD!!"),
                                       ("setdevice", b"SET!!")):
                    device_signature = b"\x4c\x00\x06" + marker
                    device_name = f"loader {action}.xex"
                    (media / device_name).write_bytes(
                        b"\xff\xff" + struct.pack("<HH", 0x600, 0x607) +
                        device_signature + struct.pack("<HHH", 0x2E0, 0x2E1, 0x600))
                    forwarded(f"--{action}", f'exeloader,path="{device_name}"', cwd=media)
                    forwarded("--debugcmd", ".restart")
                    verify_ram(device_signature, f"device-{action}")
                forwarded("--removedevice", "exeloader")

                forwarded("--run", filename, cwd=media)
                observed_bytes = None
                for attempt in range(12):
                    time.sleep(0.5)
                    observed = root / f"observed-ram-{attempt}.bin"
                    forwarded("--debugcmd", f'.writemem "{observed}" 0600 L8')
                    eventually(lambda: observed.exists() and observed.stat().st_size == 8,
                               "Forwarded debugger command did not execute in the primary")
                    observed_bytes = observed.read_bytes()
                    if observed_bytes == signature:
                        break
                self.assertEqual(observed_bytes, signature,
                                 "The forwarded Unicode relative XEX was not loaded")

                # A bad external launch must report an error without killing
                # the primary or resetting the already loaded program.
                forwarded("--cheats", "missing-cheats-file.a8t", cwd=media)
                post_error = root / "after-invalid-command.bin"
                forwarded("--debugcmd", f'.writemem "{post_error}" 0600 L8')
                eventually(lambda: post_error.exists() and post_error.stat().st_size == 8,
                           "Primary stopped accepting requests after invalid forwarded options")
                self.assertEqual(post_error.read_bytes(), signature)

                independent = launch("--nosi")
                controlled_focus = False
                require_focus = os.environ.get("ALTIRRA_TEST_REQUIRE_FOCUS") == "1"
                if windows:
                    independent_window = eventually(lambda: windows.find(independent.pid),
                                                    "--nosi did not create a second desktop window")
                    windows.focus(independent_window)
                    try:
                        eventually(lambda: windows.focused(independent_window),
                                   "Window manager denied test-controller activation", timeout=3)
                        controlled_focus = True
                    except AssertionError:
                        if require_focus:
                            raise
                        print("Foreground verification unavailable: window manager denied "
                              "the test-controller activation request.")
                else:
                    time.sleep(2)
                self.assertIsNone(independent.poll(), "--nosi was incorrectly forwarded")
                forwarded("--type", "INSTANCE REUSE~")
                if windows and controlled_focus:
                    try:
                        eventually(lambda: windows.focused(first_window),
                                   "Window manager denied Altirra's foreground request", timeout=3)
                    except AssertionError:
                        if require_focus:
                            raise
                        print("Foreground request subject to compositor policy: focus "
                              "was not granted (set ALTIRRA_TEST_REQUIRE_FOCUS=1 to require it).")
                elif not windows:
                    print("Foreground verification skipped: no X11 display.")

                # Test mode has its own process and socket even with reuse enabled.
                automated = launch("--test-mode", "--si")
                automation = Path(f"/tmp/altirra-test-{automated.pid}.sock")
                eventually(lambda: automation.exists() or automated.poll() is not None,
                           "Independent test-mode socket did not appear")
                self.assertIsNone(automated.poll(), "--test-mode was incorrectly forwarded")
                with socket.socket(socket.AF_UNIX) as connection:
                    connection.settimeout(15)
                    connection.connect(str(automation))
                    connection.sendall(b"ping\n")
                    response = b""
                    while b"\n" not in response:
                        chunk = connection.recv(65536)
                        self.assertTrue(chunk, "Test-mode socket closed before its ping reply")
                        response += chunk
                    self.assertTrue(json.loads(response.split(b"\n")[0])["ok"])
            except Exception:
                for log in logs:
                    log.flush()
                    print(f"\n{log.name}:\n{Path(log.name).read_text(errors='replace')[-7000:]}",
                          file=sys.stderr)
                raise
            finally:
                for proc in reversed(processes):
                    if proc.poll() is None:
                        proc.terminate()
                        try:
                            proc.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            proc.kill()
                            proc.wait(timeout=5)
                for log in logs:
                    log.close()
                if windows:
                    windows.close()


if __name__ == "__main__":
    unittest.main()
