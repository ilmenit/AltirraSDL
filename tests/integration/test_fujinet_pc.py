#!/usr/bin/env python3
"""Opt-in live FujiNet-PC integration through Atari OS SIOV, never host HTTP shortcuts.

Example (runtime directory must contain the official `fujinet` and `data/`):
  python3 tests/integration/test_fujinet_pc.py \
    --bridge build/linux-release/src/AltirraBridgeServer/AltirraBridgeServer \
    --runtime /path/to/fujinet-pc-ATARI --tnfsd /path/to/tnfsd --public --keep

The default checks need only the supplied runtime and a local HTTP fixture.
--legacy-bridge /path/to/fujinet-emulator-bridge runs the same checks through
its custom .atdevice and Python NetSIO hub, using private descriptor/ports.
--tnfsd adds a real local TNFS daemon (UDP/TCP port 16384 must be free).
--public records actual results from the official runtime's public TNFS hosts
and an official FujiNet HTTPS JSON file; availability is reported separately unless
--require-public is given. Nothing downloads firmware or writes user settings.
"""
from __future__ import annotations

import argparse
import http.server
import json
import os
from pathlib import Path
import re
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

sys.dont_write_bytecode = True

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "src/AltirraSDL/AltirraBridge/sdk/python"))
from altirra_bridge import AltirraBridge  # noqa: E402

# Atari OS ABI: src/Kernel/source/Shared/kernel.inc and kerneldb.inc.
SIOV, DCB = 0xE459, 0x0300
# Official FujiNet include/fujiDeviceID.h, fujiCommandID.h and SIO command docs.
FUJI, NETWORK, DISK = 0x70, 0x71, 0x31
READ, WRITE = 0x40, 0x80
PAYLOAD = b"AltirraSDL through real FujiNet-PC\n"


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def free_port(kind=socket.SOCK_STREAM, maximum=65535):
    for _ in range(128):
        with socket.socket(socket.AF_INET, kind) as s:
            # macOS can allocate ephemeral ports entirely above the
            # legacy VM's allowed range. Probe within that range instead.
            candidate = (1024 + int.from_bytes(os.urandom(2), "little") % (maximum - 1023)
                         if maximum < 65535 else 0)
            try:
                s.bind(("127.0.0.1", candidate))
            except OSError:
                continue
            port = s.getsockname()[1]
            if port <= maximum:
                return port
    raise RuntimeError(f"No available port at or below {maximum}")


def atr(payload: bytes) -> bytes:
    """Synthetic single-density disk, with recognizable first-sector bytes."""
    data = payload.ljust(128, b"\0") + bytes(128 * 719)
    return struct.pack("<HHHH8s", 0x0296, len(data) // 16, 128, 0, bytes(8)) + data


def helper_xex() -> bytes:
    # CLI; poll $0700; JSR SIOV; STY $0701; clear request; repeat.
    # The OS remains initialized: real POKEY/SIO and timers execute normally.
    code = bytes.fromhex("58 ad0007 f0fb 2059e4 8c0107 a900 8d0007 4c0106")
    def segment(address, data):
        return struct.pack("<HH", address, address + len(data) - 1) + data
    return b"\xff\xff" + segment(0x600, code) + segment(0x700, b"\0\xff") + segment(0x2E0, b"\0\x06")


class FixtureHandler(http.server.BaseHTTPRequestHandler):
    requests = []
    def do_GET(self):
        self.requests.append(self.path)
        body = (json.dumps({"integration": {"message": PAYLOAD.decode().strip()}}).encode()
                if self.path == "/fixture.json" else PAYLOAD)
        self.send_response(200)
        self.send_header("Content-Type", "application/json" if self.path.endswith(".json") else "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)
    def log_message(self, *_):
        pass


class Processes:
    def __init__(self):
        self.children = []
        self.logs = []
    def start(self, args, cwd, log, env=None):
        stream = open(log, "w", encoding="utf-8")
        self.logs.append(stream)
        proc = subprocess.Popen([str(arg) for arg in args], cwd=cwd, env=env,
                                stdout=stream, stderr=subprocess.STDOUT, text=True,
                                start_new_session=True)
        self.children.append(proc)
        return proc
    @staticmethod
    def stop(proc):
        if proc.poll() is None:
            os.killpg(proc.pid, signal.SIGTERM)
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait(timeout=5)
    def close(self):
        for proc in reversed(self.children):
            self.stop(proc)
        for stream in self.logs:
            stream.close()


def wait_token(proc, log):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        check(proc.poll() is None, f"Bridge exited with {proc.returncode}; see {log}")
        for line in log.read_text(errors="replace").splitlines():
            if line.startswith("[Bridge] token-file: "):
                return line.removeprefix("[Bridge] token-file: ")
        time.sleep(0.05)
    raise TimeoutError(f"No bridge token file; see {log}")


def screen_text(bridge):
    # Find the first visible LMS in the actual display list. CONFIG uses its
    # own display memory, not the OS SAVMSC screen pointer ($58).
    antic = bridge.antic()
    dlist = int(antic["DLIST"].lstrip("$"), 16)
    instructions = bridge.peek(dlist, min(128, 0x10000 - dlist))
    for index, instruction in enumerate(instructions[:-2]):
        if (instruction & 0x4F) >= 0x42 and (instruction & 0x40):
            address = int.from_bytes(instructions[index + 1:index + 3], "little")
            data = bridge.peek(address, min(1000, 0x10000 - address))
            # Atari screen codes: $00..3f -> ASCII $20..5f, $60..7f
            # preserve lowercase; strip inverse video.
            return "".join(chr(v + 32) if v < 64 else chr(v) if v >= 96 else "."
                           for value in data for v in [value & 127])
    return ""


def wait_config(bridge, runtime, root, timeout=45):
    deadline = time.monotonic() + timeout
    text = ""
    while time.monotonic() < deadline:
        check(runtime.poll() is None, "FujiNet-PC exited; inspect runtime log")
        bridge.frame(60)
        text = screen_text(bridge)
        # Both are rendered by CONFIG's host screen (src/atari/screen.c in
        # FujiNetWIFI/fujinet-config); static strings in loaded code cannot
        # satisfy this check because we read the active ANTIC display memory.
        if all(marker in text for marker in ("HOST LIST", "fujinet.online", "DRIVE SLOTS", "OPTION")):
            (root / "config-boot.png").write_bytes(bridge.screenshot())
            return {"pc": bridge.regs()["PC"], "screen": text}
        time.sleep(0.02)
    (root / "config-boot.png").write_bytes(bridge.screenshot())
    raise TimeoutError(f"CONFIG host screen not ready; active screen: {text!r}")


class Atari:
    def __init__(self, bridge, helper):
        self.bridge, self.helper = bridge, helper
    def install(self):
        self.bridge.boot(str(self.helper))
        self.bridge.frame(180)
        pc = int(self.bridge.regs()["PC"].lstrip("$"), 16)
        check(0x600 <= pc < 0x614, f"SIO helper did not start: PC=${pc:04x}")
        # BOOT resets peripherals. The official wrapper restarts FujiNet and
        # its dummy WiFi takes 400 ms to reconnect in wall-clock time.
        time.sleep(1)
        self.bridge.frame(1)
    def sio(self, device, command, *, length=0, data=None, aux=0, unit=1,
            timeout=8, expect=True, wall_timeout=35):
        b = self.bridge
        direction = WRITE if data is not None else READ if length else 0
        if data is not None:
            length = len(data)
            b.memload(0x2000, data)
        elif length:
            b.memload(0x2000, bytes(length))
        b.memload(DCB, struct.pack("<BBBBHBBHH", device, unit, command,
                                  direction, 0x2000, timeout, 0, length, aux))
        b.poke(0x701, 0xFF)
        b.poke(0x700, 1)
        deadline = time.monotonic() + wall_timeout
        while time.monotonic() < deadline:
            # Bridge runs with --pacing=realtime: WAN requests execute in
            # wall-clock time, so the Atari timeout clock must follow it.
            b.frame(1)
            if b.peek(0x700)[0] == 0:
                status = b.peek(0x701)[0]
                if expect:
                    check(status == 1, f"SIO ${device:02x}/${command:02x}, unit={unit}, aux=${aux:04x}: status=${status:02x}")
                return status, b.peek(0x2000, length) if direction == READ else b""
        raise TimeoutError(f"SIO ${device:02x}/${command:02x} did not return")
    def command(self, *args, **kwargs):
        return self.sio(*args, **kwargs)[1]
    def network_open(self, url):
        self.command(NETWORK, ord("O"), data=("N:" + url).encode().ljust(256, b"\0"), aux=4)
    def network_read(self, *, max_bytes=4096):
        status = self.command(NETWORK, ord("S"), length=4)
        available = min(int.from_bytes(status[:2], "little"), max_bytes)
        check(available > 0, f"N: channel has no bytes: status={status.hex()}")
        return self.command(NETWORK, ord("R"), length=available, aux=available)
    def network_close(self):
        self.command(NETWORK, ord("C"))
    def http(self, url):
        self.network_open(url)
        try:
            return self.network_read()
        finally:
            self.network_close()
    def json_query(self, url, pointer):
        self.network_open(url)
        try:
            self.command(NETWORK, 0xFC, aux=0x100)  # JSON parser, DAUX2=1
            self.command(NETWORK, ord("P"))
            self.command(NETWORK, ord("Q"), data=pointer.encode().ljust(256, b"\0"))
            return self.network_read()
        finally:
            self.sio(NETWORK, ord("C"), expect=False)
    def directory(self, slot):
        self.command(FUJI, 0xF9, aux=slot)
        self.command(FUJI, 0xF7, data=b"/\0".ljust(256, b"\0"), aux=slot)
        try:
            entries = []
            for _ in range(16):
                entry = self.command(FUJI, 0xF6, length=128, aux=128)
                if entry.startswith(b"\x7f\x7f"):
                    break
                entries.append(entry.split(b"\0", 1)[0])
            check(entries, f"Host slot {slot} returned no directory entries")
            return b"\n".join(entries)
        finally:
            self.command(FUJI, 0xF5)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--bridge", required=True, type=Path)
    parser.add_argument("--runtime", required=True, type=Path)
    parser.add_argument("--legacy-bridge", type=Path,
                        help="Test the custom .atdevice and Python hub from this fujinet-emulator-bridge checkout")
    parser.add_argument("--tnfsd", type=Path)
    parser.add_argument("--public", action="store_true")
    parser.add_argument("--basic", choices=("on", "off"), default="off",
                        help="BASIC state for the CONFIG boot check")
    parser.add_argument("--require-public", action="store_true")
    parser.add_argument("--boot-only", action="store_true", help="Only check the actual CONFIG host screen")
    parser.add_argument("--keep", action="store_true")
    parser.add_argument("--work-dir", type=Path)
    args = parser.parse_args()
    check(os.name != "nt", "This harness requires POSIX XDG settings isolation")
    args.bridge, args.runtime = args.bridge.resolve(), args.runtime.resolve()
    if args.tnfsd:
        args.tnfsd = args.tnfsd.resolve()
    if args.legacy_bridge:
        args.legacy_bridge = args.legacy_bridge.resolve()
        check((args.legacy_bridge / "altirra-custom-device/netsio.atdevice").is_file() and
              (args.legacy_bridge / "fujinet-bridge/netsiohub/__main__.py").is_file(),
              "--legacy-bridge must point to a complete fujinet-emulator-bridge checkout")
    transport = "legacy-custom-device" if args.legacy_bridge else "native-netsio"
    check(args.bridge.is_file(), "--bridge executable missing")
    check((args.runtime / "fujinet").is_file() and (args.runtime / "data/autorun.atr").is_file(),
          "--runtime must contain official fujinet and data/autorun.atr")
    root = args.work_dir.resolve() if args.work_dir else Path(tempfile.mkdtemp(prefix="altirra-fujinet-live-"))
    root.mkdir(parents=True, exist_ok=True)
    children = Processes()
    results = []
    def record(name, value=None):
        item = {"test": name, "transport": transport, "passed": True}
        if value is not None:
            item["result"] = value
        results.append(item)
        print(json.dumps(item), flush=True)
    def require_content(data, expected):
        check(expected in data, f"Expected {expected!r} in response: {data[:160]!r}")
        return data
    def external(name, operation):
        try:
            record(name, operation().decode(errors="replace")[:160])
        except Exception as exc:
            item = {"test": name, "transport": transport, "passed": False, "error": str(exc)}
            results.append(item)
            print(json.dumps(item), flush=True)
            if args.require_public:
                raise
    fixture = http.server.ThreadingHTTPServer(("127.0.0.1", 0), FixtureHandler)
    http_thread = threading.Thread(target=fixture.serve_forever, daemon=True)
    http_thread.start()
    success = False
    try:
        fndir = root / "runtime"
        fndir.mkdir(exist_ok=True)
        shutil.copytree(args.runtime / "data", fndir / "data", dirs_exist_ok=True)
        sd = root / "SD"
        sd.mkdir(exist_ok=True)
        local_disk = root / "local-d2.atr"
        local_disk.write_bytes(atr(b"ALTIRRA LOCAL D2"))
        (sd / "integration.atr").write_bytes(atr(b"FUJINET REMOTE D1"))
        (sd / "hello.txt").write_bytes(PAYLOAD)
        if args.tnfsd:
            for kind in (socket.SOCK_STREAM, socket.SOCK_DGRAM):
                with socket.socket(socket.AF_INET, kind) as probe:
                    probe.bind(("127.0.0.1", 16384))
            children.start([args.tnfsd, "-r", "-a", "-p", "16384", sd], root, root / "tnfsd.log")
        port, web_port = free_port(socket.SOCK_DGRAM), free_port()
        legacy_descriptor = None
        if args.legacy_bridge:
            # The published custom-device VM accepts ports 1024..49150.
            # Change only this port in a private, deliberately renamed
            # descriptor; the checkout and user profiles stay untouched.
            tcp_port = free_port(maximum=49150)
            descriptor = (args.legacy_bridge / "altirra-custom-device/netsio.atdevice").read_text()
            descriptor, count = re.subn(
                r'(option\s+"network"\s*:\s*\{[^}]*?\bport\s*:\s*)\d+',
                lambda match: match[1] + str(tcp_port), descriptor,
                count=1, flags=re.IGNORECASE)
            check(count == 1, "Legacy descriptor has no recognizable network port setting")
            legacy_descriptor = root / "renamed legacy transport.atdevice"
            legacy_descriptor.write_text(descriptor)
            hub_log = root / "netsiohub.log"
            hub_env = os.environ.copy()
            hub_env["PYTHONDONTWRITEBYTECODE"] = "1"
            hub = children.start(
                [sys.executable, "-u", "-m", "netsiohub", "--port", tcp_port, "--netsio-port", port],
                args.legacy_bridge / "fujinet-bridge", hub_log, hub_env)
            deadline = time.monotonic() + 10
            while "Waiting for localhost connection" not in hub_log.read_text(errors="replace"):
                check(hub.poll() is None, f"Legacy NetSIO hub exited; see {hub_log}")
                check(time.monotonic() < deadline, f"Legacy NetSIO hub did not start; see {hub_log}")
                time.sleep(.05)
        fnconfig = root / "fnconfig.ini"
        config = (args.runtime / "fnconfig.ini").read_text()
        import configparser
        ini = configparser.ConfigParser(interpolation=None)
        ini.read_string(config)
        ini["BOIP"].update(enabled="1", host="127.0.0.1", port=str(port))
        ini["General"].update(configenabled="1", status_wait_enabled="0")
        ini["ENABLE"]["enable_device_slot_2"] = "0"  # Keep Altirra's local D2.
        hosts = ["SD", "127.0.0.1", "fujinet.online", "apps.irata.online", "fujinet.diller.org"]
        for slot, host in enumerate(hosts, 1):
            ini[f"Host{slot}"] = {"type": "SD" if host == "SD" else "TNFS", "name": host}
        with fnconfig.open("w") as stream:
            ini.write(stream)
        # Use the release's own temporary-failure/reboot policy. NetSIO
        # COLD_RESET exits FujiNet-PC with 75; its official wrapper restarts.
        (fndir / "fujinet").symlink_to(args.runtime / "fujinet")
        check((args.runtime / "run-fujinet").is_file(), "Official run-fujinet wrapper missing")
        def start_fn():
            return children.start(["/bin/sh", args.runtime / "run-fujinet", "-c", fnconfig,
                                   "-s", sd, "-u", f"http://127.0.0.1:{web_port}"],
                                  fndir, root / f"fujinet-{len(children.children)}.log")
        env = os.environ.copy()
        env.update(XDG_CONFIG_HOME=str(root), TMPDIR=str(root), TMP=str(root), TEMP=str(root))
        configdir = root / "altirra"
        configdir.mkdir(exist_ok=True)
        # Missing D1 disables the standard drive when user settings load;
        # D2 remains mounted. No real user profile or runtime config is touched.
        (configdir / "settings.ini").write_text(
            '[User\\AltirraSDL\\Profiles]\n"Defaults inited" = 1\n"Current profile" = 0\n'
            '[User\\AltirraSDL\\Profiles\\00000000\\Mounted Images]\n'
            f'"Disk 1" = "R{local_disk.as_posix()}"\n')
        bridge_log = root / "bridge.log"
        server = children.start([args.bridge, "--settings=user", "--pacing=realtime", "--bridge=tcp:127.0.0.1:0"], root, bridge_log, env)
        with AltirraBridge.from_token_file(wait_token(server, bridge_log)) as b:
            b.config("randmem", "off")
            b.config("randdelay", "off")
            b.config("basic", args.basic)
            b.config("fastboot", "off")
            b.config("siopatch", "off")
            if legacy_descriptor:
                b.device_set("custom", True, path=str(legacy_descriptor))
            else:
                b.device_set("netsio", True, port=port, bind_address="127.0.0.1", sync_timeout_ms=100)
            record("SIO transport", {"udpPort": port,
                   "descriptor": str(legacy_descriptor) if legacy_descriptor else None})
            if legacy_descriptor:
                # Reset the custom VM before a peer starts transmitting.
                # Resetting its script while a hub memory request is in
                # flight can invalidate that request's segment indices.
                b.cold_reset()
            runtime = start_fn()
            time.sleep(1)
            if not legacy_descriptor:
                b.cold_reset()
            record("FujiNet CONFIG boot", wait_config(b, runtime, root))
            if args.boot_only:
                b.quit()
                check(server.wait(timeout=10) == 0, "Bridge did not shut down cleanly")
                success = True
                return
            helper = root / "siov-helper.xex"
            helper.write_bytes(helper_xex())
            a = Atari(b, helper)
            a.install()
            hosts_before = a.command(FUJI, 0xF4, length=256)
            check(b"fujinet.online" in hosts_before, "Host slots not received through SIO")
            record("Fuji control host slots", hosts_before[:96].decode(errors="replace"))
            check(a.command(DISK, ord("R"), length=128, aux=1, unit=2).startswith(b"ALTIRRA LOCAL D2"),
                  "Local D2 conflicted with FujiNet")
            record("Local D2 coexistence")
            b.config("siopatch", "on")
            # Local accelerated disks must retain priority over the raw
            # external bus, while unhandled FujiNet devices bypass the patch.
            def d2_wire_reads():
                return sum(log.read_bytes().count(b"CF: 32 52 01 00")
                           for log in root.glob("fujinet-*.log"))
            before = d2_wire_reads()
            check(a.command(DISK, ord("R"), length=128, aux=1, unit=2).startswith(b"ALTIRRA LOCAL D2"),
                  "Local D2 failed with the SIO patch enabled")
            check(d2_wire_reads() == before, "Accelerated D2 read leaked onto the external SIO bus")
            check(a.command(FUJI, 0xF4, length=256) == hosts_before,
                  "SIO acceleration intercepted FujiNet control commands")
            check(a.http(f"http://127.0.0.1:{fixture.server_port}/payload") == PAYLOAD,
                  "SIO acceleration intercepted FujiNet N: HTTP")
            record("SIO acceleration with local D2 and external control/HTTP")
            # Keep acceleration enabled for the remaining real-device checks.
            check(a.http(f"http://127.0.0.1:{fixture.server_port}/payload") == PAYLOAD, "N: HTTP payload mismatch")
            record("N: HTTP local fixture")
            parsed = a.json_query(f"http://127.0.0.1:{fixture.server_port}/fixture.json", "/integration/message")
            check(PAYLOAD.strip() in parsed, "N: JSON pointer result mismatch")
            check("/payload" in FixtureHandler.requests and "/fixture.json" in FixtureHandler.requests,
                  "Official FujiNet never requested local fixtures")
            record("N: JSON local fixture", parsed.decode(errors="replace"))
            listing = a.directory(0)
            check(b"hello.txt" in listing and b"integration.atr" in listing,
                  "SD directory did not contain the private fixture files")
            record("SD host directory", listing.decode(errors="replace"))
            remote_slot = 1 if args.tnfsd else 0
            if args.tnfsd:
                listing = a.directory(remote_slot)
                check(b"hello.txt" in listing and b"integration.atr" in listing,
                      "TNFS directory did not contain the private fixture files")
                record("TNFS local directory", listing.decode(errors="replace"))
            a.command(FUJI, 0xE2, data=b"/integration.atr\0".ljust(256, b"\0"), aux=((remote_slot << 4) | 1) << 8)
            a.command(FUJI, 0xF8, aux=0x100)  # D1, read-only
            a.command(FUJI, 0xD9, aux=0)  # Leave CONFIG boot redirection.
            check(a.command(DISK, ord("R"), length=128, aux=1).startswith(b"FUJINET REMOTE D1"),
                  "Mounted FujiNet disk did not provide its first sector")
            record("TNFS image sector" if args.tnfsd else "SD image sector")
            if args.public or args.require_public:
                for slot, host in enumerate(hosts[2:], 2):
                    external(f"Public TNFS {host}", lambda slot=slot: a.directory(slot))
                external("Public HTTPS official FujiNet board JSON", lambda: require_content(a.json_query(
                    "https://raw.githubusercontent.com/FujiNetWIFI/fujinet-firmware/master/boards/fujinet-v1.json",
                    "/name"), b"#FujiNet v1"))
                external("Public HTTP fujinet.online", lambda: require_content(
                    a.http("http://fujinet.online/"), b"FujiNet"))
            children.stop(runtime)
            status, _ = a.sio(FUJI, 0xF4, length=256, timeout=1, expect=False)
            check(status != 1, "Disconnected FujiNet returned stale success/data")
            record("Runtime disconnect", {"siov_status": status})
            # A new runtime UDP source port must reconnect to the existing
            # listener. DEVICE_SET would cold-reset the entire Atari and
            # discard our running helper; it does not model runtime restart.
            runtime = start_fn()
            time.sleep(1)
            b.frame(1)
            check(a.command(FUJI, 0xF4, length=256) == hosts_before, "Reconnect did not recover control transactions")
            record("Runtime reconnect")
            b.quit()
        check(server.wait(timeout=10) == 0, "Bridge did not shut down cleanly")
        success = True
    except Exception as exc:
        results.append({"test": "integration", "transport": transport, "passed": False, "error": str(exc)})
        print(json.dumps(results[-1]), flush=True)
        print(f"Logs retained: {root}", file=sys.stderr)
        raise
    finally:
        fixture.shutdown()
        fixture.server_close()
        children.close()
        (root / "results.json").write_text(json.dumps(results, indent=2) + "\n")
        if success and not args.keep and not args.work_dir:
            shutil.rmtree(root)
        else:
            print(f"Artifacts: {root}", flush=True)


if __name__ == "__main__":
    main()
