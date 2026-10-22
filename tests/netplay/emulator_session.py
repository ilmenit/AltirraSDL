#!/usr/bin/env python3
"""Exercise two actual emulators through an impaired private UDP relay.

Build AltirraSDL and the standalone lobby first. All traffic stays on loopback.
No downloaded game or public lobby registration is needed. Artifacts include
per-process logs, session telemetry and screenshots on failure.
"""
from __future__ import annotations

import argparse
import heapq
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import random
import select
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tests.ui.harness import AltirraTestHarness


def free_port(kind=socket.SOCK_STREAM):
    with socket.socket(type=kind) as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def click(app, label, window=None, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        found = next((item for item in app.list_items()
                      if item["label"] == label
                      and (window is None or item["window"] == window)), None)
        if found:
            app.send(f"click_at {found['x'] + found['w']/2} "
                     f"{found['y'] + found['h']/2}")
            app.wait_frames(5)
            return
        time.sleep(0.05)
    raise AssertionError(f"Missing widget: {window} / {label}")


class ImpairedRelay:
    """Two socket endpoints preserve source validation in both directions."""

    def __init__(self, target, one_way_ms, loss):
        self.target = target
        self.one_way_ms = one_way_ms
        self.loss = loss
        self.front = socket.socket(type=socket.SOCK_DGRAM)
        self.back = socket.socket(type=socket.SOCK_DGRAM)
        for sock in (self.front, self.back):
            sock.bind(("127.0.0.1", 0))
            sock.setblocking(False)
        self.port = self.front.getsockname()[1]
        self.stop = threading.Event()
        self.forwarded = self.dropped = 0
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        rng = random.Random(20261021)
        queue = []
        sequence = 0
        joiner = None
        while not self.stop.is_set():
            ready, _, _ = select.select([self.front, self.back], [], [], 0.001)
            for source in ready:
                data, address = source.recvfrom(65535)
                if source is self.front:
                    joiner = address
                    output, destination = self.back, self.target
                else:
                    output, destination = self.front, joiner
                if destination is None:
                    continue
                if rng.random() < self.loss:
                    self.dropped += 1
                    continue
                delay = max(0, self.one_way_ms + rng.uniform(-5, 5)) / 1000
                sequence += 1
                heapq.heappush(queue, (time.monotonic() + delay, sequence,
                                     output, destination, data))
            while queue and queue[0][0] <= time.monotonic():
                _, _, output, destination, data = heapq.heappop(queue)
                output.sendto(data, destination)
                self.forwarded += 1

    def close(self):
        self.stop.set()
        self.thread.join(timeout=2)
        self.front.close()
        self.back.close()


def discovery_proxy(base, relay):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def forward(self):
            body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
            # This fixture deliberately prevents direct rescue bypassing
            # the impaired path. The real server still authorizes all relay
            # tickets, registrations and gameplay packets.
            if self.path.endswith("/peer-hint"):
                self.send_response(204)
                self.end_headers()
                return
            request = urllib.request.Request(
                base + self.path, method=self.command,
                data=body if self.command == "POST" else None,
                headers={"Content-Type": "application/json"})
            try:
                with urllib.request.urlopen(request, timeout=5) as response:
                    status, data = response.status, response.read()
            except urllib.error.HTTPError as error:
                status, data = error.code, error.read()
            if self.path.startswith("/v1/sessions") and status == 200:
                rows = json.loads(data)
                for row in rows:
                    row["hostEndpoint"] = "127.0.0.1:1"
                    row["candidates"] = row["hostEndpoint"]
                data = json.dumps(rows).encode()
            if self.path.endswith("/relay-ticket") and status == 201:
                ticket = json.loads(data)
                ticket["relayPort"] = relay.port
                data = json.dumps(ticket).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        do_GET = do_POST = do_DELETE = forward

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def drain(pipe, path):
    with path.open("wb") as destination:
        while block := pipe.read1(8192):
            destination.write(block)
            destination.flush()


def make_disk_fixture(path):
    """Original 6502 boot program: verified sector-4 writes then reads.

    DCB $0300..$030B: Kernel/source/Shared/kerneldb.inc:342-352;
    SIOV $E459: Kernel/source/Shared/kernel.inc:51. Commands $57/$52
    follow Altirra/source/disk.cpp. Buffer4100[X]=X XOR input seed;
    buffer4200 is compared bytewise. RAM400B counts successes modulo256;
    RAM400C/D/E count write/read/compare errors. Marker: ATRRWTST.
    """
    program = bytes.fromhex(
        "a9418d1040a9548d1140a9528d1240a9528d1340a9578d1440a9548d1540a9538d1640a9"
        "548d1740a9008d0040a9008d0140a9008d0240a9008d0340a9008d0440a9008d0540a900"
        "8d0640a9008d0740a9008d0840a9008d0940a9008d0a40a9008d0b40a9008d0c40a9008d"
        "0d40a9008d0e40a9008d0f40ad00d34d1fd04d02408d0640a27f8a4d06409d0041ca10f6"
        "a20bbde8079d0003ca10f72059e48c0840302aa9528d0203a9408d0303a9428d05032059"
        "e48c09403019a27fbd0041dd0042d015ca10f5ee0b404ce207ee0c404ce207ee0d404ce2"
        "07ee0e40ee02404c7e07310157800041070080000400"
)
    data = bytearray(720 * 128)
    # Three boot sectors loaded at $0700, execution begins at $0706.
    data[:6] = struct.pack("<BBHH", 0, 3, 0x0700, 0x0706)
    data[6:6 + len(program)] = program
    header = struct.pack("<HHHH8s", 0x0296, len(data) // 16, 128, 0, b"\0" * 8)
    path.write_bytes(header + data)


def process_resources(app):
    """Optional Linux accounting; no additional dependency on other hosts."""
    try:
        stat = Path(f"/proc/{app._proc.pid}/stat").read_text().split()
        cpu = (int(stat[13]) + int(stat[14])) / os.sysconf("SC_CLK_TCK")
        status = Path(f"/proc/{app._proc.pid}/status").read_text().splitlines()
        rss = next(int(line.split()[1]) for line in status if line.startswith("VmRSS:"))
        return {"cpuSeconds": cpu, "rssKiB": rss}
    except (OSError, ValueError, StopIteration):
        return {}


def check_joystick_routing(apps, start_key=None):
    """Real local joystick-one keys must drive distinct ports on both peers."""
    deadline = time.monotonic() + 20
    while min(app.send("netplay_status")["frame"] for app in apps) < 300:
        assert time.monotonic() < deadline, "Boot did not reach joystick initialization"
        time.sleep(0.05)
    for app in apps:
        # Give the emulated display keyboard focus after the join dialogs.
        app.send("click_at 1200 850")
        app.wait_frames(5)
    if start_key:
        apps[0].send(f"physical_key down {start_key}")
        initial = [app.send("netplay_status")["frame"] for app in apps]
        deadline = time.monotonic() + 10
        while not all(app.send("netplay_status")["frame"] >= initial[i] + 20
                      for i, app in enumerate(apps)):
            assert time.monotonic() < deadline, "Start key did not advance on both peers"
            time.sleep(0.025)
        apps[0].send(f"physical_key up {start_key}")
    observations = []
    for peer, direction, key, porta, triggers in (
        (0, "down", "Left", 0xFB, [1, 1]),
        (1, "down", "Right", 0x7B, [1, 1]),
        (0, "down", "Left Ctrl", 0x7B, [0, 1]),
        (1, "down", "Left Ctrl", 0x7B, [0, 0]),
        (0, "up", "Left", 0x7F, [0, 0]),
        (0, "up", "Left Ctrl", 0x7F, [1, 0]),
        (1, "up", "Right", 0xFF, [1, 0]),
        (1, "up", "Left Ctrl", 0xFF, [1, 1]),
    ):
        apps[peer].send(f"physical_key {direction} {key}")
        initial = [app.send("netplay_status")["frame"] for app in apps]
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            status = [app.send("netplay_status") for app in apps]
            assert all(s["active"] and not s["desynced"] and not s["resyncing"]
                       for s in status), status
            registers = [{"porta": app.send("mem_read D300 1")["bytes"][0],
                          "triggers": app.send("mem_read D010 2")["bytes"]}
                         for app in apps]
            if (all(s["frame"] >= initial[i] + 20 for i, s in enumerate(status))
                    and all(r == {"porta": porta, "triggers": triggers} for r in registers)):
                break
            time.sleep(0.025)
        else:
            raise AssertionError((peer, direction, key, porta, triggers, registers, status))
        observations.append({"peer": peer, "key": key, "direction": direction,
                             "registers": registers, "frames": [s["frame"] for s in status]})
    return observations


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emu", type=Path, default=ROOT / "build/linux-release/src/AltirraSDL/AltirraSDL")
    parser.add_argument("--lobby", type=Path, default=ROOT / "build/lobby-audit/altirra-lobby")
    parser.add_argument("--image", type=Path, help="Local boot image; default uses the built-in XEX fixture")
    parser.add_argument("--disk-rw", action="store_true",
                        help="Boot the built-in sector write/read/verify ATR fixture")
    parser.add_argument("--joystick-test", action="store_true",
                        help="Verify real arrow/fire keys map host to port one and guest to port two")
    parser.add_argument("--start-key", help="Optional SDL key (e.g. F3) to enter gameplay before joystick checks")
    parser.add_argument("--video", choices=("ntsc", "pal"), default="pal")
    parser.add_argument("--mode", choices=("automatic", "delay"), default="automatic")
    parser.add_argument("--rtt-ms", type=float, default=60)
    parser.add_argument("--loss", type=float, default=0.01)
    parser.add_argument("--seconds", type=float, default=15)
    parser.add_argument("--pacing", choices=("realtime", "unlimited"), default="realtime")
    parser.add_argument("--resync-test", action="store_true",
                        help="Inject one RAM mismatch, require one recovery and 1000 further frames")
    args = parser.parse_args()
    if args.disk_rw and args.image:
        parser.error("Choose either --disk-rw or --image")
    if args.image:
        args.image = args.image.resolve()
        if not args.image.is_file():
            parser.error(f"Boot image does not exist: {args.image}")
    assert 0 <= args.loss < 1 and args.rtt_ms >= 0 and args.seconds >= 5
    scratch = ROOT / "build/_scratch/netplay-emulator"
    scratch.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix=args.mode + "-", dir=scratch))
    print(f"Artifacts: {work}", flush=True)
    http_port, ws_port, udp_port = free_port(), free_port(), free_port(socket.SOCK_DGRAM)
    base = f"http://127.0.0.1:{http_port}"
    log = (work / "lobby.log").open("wb")
    service = subprocess.Popen([str(args.lobby.resolve())], stdout=log, stderr=subprocess.STDOUT,
                              env=os.environ | {"BIND": f"127.0.0.1:{http_port}",
                              "PORT": str(http_port), "UDP_REFLECTOR_PORT": str(udp_port),
                              "WS_BRIDGE_PORT": "0"})
    relay = ImpairedRelay(("127.0.0.1", udp_port), args.rtt_ms / 2, args.loss)
    discovery = discovery_proxy(base, relay)
    join_base = f"http://127.0.0.1:{discovery.server_port}"
    apps = []
    try:
        for _ in range(100):
            try:
                with urllib.request.urlopen(base + "/healthz", timeout=1):
                    break
            except OSError:
                time.sleep(0.05)
        # Read joystick, RANDOM, keyboard and console registers continuously.
        program = bytes.fromhex("AD00D38D0040AD0AD28D0140AD09D28D0340"
                                "AD0FD28D0440AD1FD08D0540EE02404C0020")
        if args.image:
            image = args.image
        elif args.disk_rw:
            image = work / "sector-write-read.atr"
            make_disk_fixture(image)
        else:
            image = work / "input-and-random.xex"
            image.write_bytes(b"\xff\xff" + struct.pack("<HH", 0x2000, 0x2000 + len(program) - 1)
                              + program + struct.pack("<HHH", 0x2E0, 0x2E1, 0x2000))
        for name in ("Host", "Joiner"):
            config = work / name / "altirra"
            config.mkdir(parents=True)
            ini = ('[User\\AltirraSDL]\n"ShownSetupWizard" = 1\n'
                   '[User\\AltirraSDL\\Netplay]\n'
                   f'"Handle" = "Test{name}"\n"NotifySystem" = 0\n"NotifyChime" = 0\n')
            if name == "Host":
                ini += ('"GameCount" = 1\n"Game0_id" = "test-offer"\n'
                        f'"Game0_path" = "{image}"\n"Game0_name" = "Input test"\n'
                        f'"Game0_videoStd" = {0 if args.video == "ntsc" else 1}\n')
            # View settings inherit from the base profile. Tabs are not
            # included in ImGui item hooks, so exercise the real settings
            # load path rather than selecting a tab by screen coordinates.
            ini += ('[User\\AltirraSDL\\Profiles\\00000000]\n'
                    f'"View: Online play rollback window" = {8 if args.mode == "automatic" else 0}\n')
            (config / "settings.ini").write_text(ini)
            (config / "lobby.ini").write_text(
                f"[private-test]\nname=Private test\nurl={base if name == 'Host' else join_base}\nenabled=true\n")
            os.environ.update(XDG_CONFIG_HOME=str(work / name), SDL_VIDEODRIVER="dummy",
                              SDL_AUDIODRIVER="dummy", ALTIRRA_DISPLAY_BACKEND="sdlrenderer")
            app = AltirraTestHarness(executable=str(args.emu.resolve()),
                                     extra_args=["--ui-mode", "desktop", "--pacing=" + args.pacing])
            app.start()
            apps.append(app)
            for kind, pipe in (("stdout", app._proc.stdout), ("stderr", app._proc.stderr)):
                threading.Thread(target=drain, args=(pipe, work / f"{name}-{kind}.log"), daemon=True).start()
            app.close_dialog("SetupWizard")
            app.send("set_window_size 1280 900")
            app.wait_frames(4)
        host, joiner = apps
        original_profiles = [app.send("netplay_profile status") for app in apps]
        click(host, "Online Play", "##MainMenuBar")
        click(host, "Host Games...")
        click(host, "##en")
        for _ in range(150):
            with urllib.request.urlopen(base + "/v1/sessions", timeout=2) as response:
                if "Input test" in response.read().decode():
                    break
            time.sleep(0.1)
        click(joiner, "Online Play", "##MainMenuBar")
        click(joiner, "Browse Hosted Games...")
        click(joiner, "Input test")
        click(joiner, "Join", "Online Play — Browse Hosted Games##netplay")
        click(joiner, "Join", "Join Session##netplay")
        click(host, "Allow")
        deadline = time.monotonic() + 25
        while time.monotonic() < deadline:
            status = [app.send("netplay_status") for app in apps]
            if all(item["active"] and item["frame"] > 30 for item in status):
                break
            time.sleep(0.1)
        assert all(item["active"] and item["frame"] > 30 for item in status), status
        joystick = check_joystick_routing(apps, args.start_key) if args.joystick_test else None
        if joystick is not None:
            for name, app in zip(("Host", "Joiner"), apps):
                app.send(f"screenshot {work / (name + '-joystick.png')}")
        fixture_booted = [False, False]

        def check_disk_fixture():
            if not args.disk_rw:
                return
            for i, app in enumerate(apps):
                memory = app.send("mem_read 4000 32")["bytes"]
                marker = bytes(memory[16:24]) == b"ATRRWTST"
                if fixture_booted[i]:
                    assert marker, memory
                if marker:
                    fixture_booted[i] = True
                    assert not any(memory[12:15]), memory

        resources_before = [process_resources(app) for app in apps]
        start = time.monotonic()
        start_frames = [item["frame"] for item in status]
        samples = []
        previous = start_frames
        sequence = 0
        while time.monotonic() - start < args.seconds:
            scan = (sequence // 2) % 64
            host.send(f"netplay_input press {scan} 0 1" if sequence % 2 == 0
                      else f"netplay_input up {scan}")
            joiner.send(f"netplay_input console 1 {sequence % 2}")
            status = [app.send("netplay_status") for app in apps]
            samples.append(status)
            check_disk_fixture()
            assert all(item["frame"] >= previous[i] for i, item in enumerate(status)), status
            previous = [item["frame"] for item in status]
            assert all((item["window"] > 0) == (args.mode == "automatic") for item in status), status
            assert all(item["active"] and not item["desynced"]
                       and not item["resyncing"] for item in status), status
            assert all(item["frame"] - item["confirmedFrame"] <= item["window"] for item in status), status
            sequence += 1
            time.sleep(0.05)
        assert all(item["frame"] > start_frames[i] + 50 for i, item in enumerate(status)), status
        if args.mode == "automatic" and args.rtt_ms >= 50:
            assert sum(item["corrections"] for item in status) > 0, status
        recovery = None
        if args.resync_test:
            def resync_counts():
                return [(work / "Host-stderr.log").read_text(errors="replace").count(
                            "host: initiating resync epoch="),
                        (work / "Joiner-stderr.log").read_text(errors="replace").count(
                            "joiner: resync incoming (epoch=")]

            assert resync_counts() == [0, 0], "Unexpected resync before deliberate mismatch"
            with urllib.request.urlopen(base + "/v1/sessions", timeout=2) as response:
                before_sessions = json.load(response)
            identity = [(row["sessionId"], row["hostEndpoint"]) for row in before_sessions]
            original_byte = host.send("mem_read 5000 1")["bytes"][0]
            assert host.send(f"debugger_console e 5000 {original_byte ^ 0xFF:02X}")["ok"]
            deadline = time.monotonic() + 75
            resumed = None
            recovery_samples = []
            while time.monotonic() < deadline:
                scan = (sequence // 2) % 64
                host.send(f"netplay_input press {scan} 0 1" if sequence % 2 == 0
                          else f"netplay_input up {scan}")
                joiner.send(f"netplay_input console 1 {sequence % 2}")
                sequence += 1
                status = [app.send("netplay_status") for app in apps]
                recovery_samples.append(status)
                check_disk_fixture()
                counts = resync_counts()
                assert all(count <= 1 for count in counts), (counts, status)
                if counts == [1, 1] and all(item["active"] and not item["resyncing"]
                                          and not item["desynced"] for item in status):
                    if resumed is None:
                        resumed = [item["frame"] for item in status]
                    assert all(item["frame"] >= resumed[i] for i, item in enumerate(status)), status
                    assert all(item["frame"] - item["confirmedFrame"] <= item["window"]
                               for item in status), status
                    if all(item["frame"] >= resumed[i] + 1000 for i, item in enumerate(status)):
                        break
                time.sleep(0.05)
            assert resumed and all(item["active"] and item["frame"] >= resumed[i] + 1000
                                   for i, item in enumerate(status)), status
            assert resync_counts() == [1, 1]
            with urllib.request.urlopen(base + "/v1/sessions", timeout=2) as response:
                after_sessions = json.load(response)
            assert [(row["sessionId"], row["hostEndpoint"]) for row in after_sessions] == identity
            recovery = {"resumedFrames": resumed, "final": status,
                        "samples": recovery_samples, "sessionIdentity": identity}
        resources_after = [process_resources(app) for app in apps]
        cpu_seconds = [after.get("cpuSeconds", 0) - before.get("cpuSeconds", 0)
                       for before, after in zip(resources_before, resources_after)]
        memory = [app.send("mem_read 4000 32") for app in apps]
        if args.disk_rw:
            assert all(fixture_booted), "Disk fixture did not boot"
            assert all(item["bytes"][8:10] == [1, 1] and not any(item["bytes"][12:15])
                       for item in memory), memory
        result = {"parameters": vars(args) | {"emu": str(args.emu), "lobby": str(args.lobby),
                  "image": str(args.image) if args.image else None},
                  "joystickRouting": joystick,
                  "memory4000": memory, "processResources": resources_after,
                  "cpuSeconds": cpu_seconds,
                  "elapsedSeconds": time.monotonic() - start, "startFrames": start_frames,
                  "final": status, "samples": samples, "forwarded": relay.forwarded,
                  "dropped": relay.dropped, "recovery": recovery}
        (work / "result.json").write_text(json.dumps(result, indent=2))
        if args.resync_test:
            for app in reversed(apps):
                assert app.send("netplay_disconnect")["ok"]
            restored = [app.send("netplay_profile status") for app in apps]
            restore_keys = ("diskObject", "diskByte", "diskDirty", "diskPath", "writeMode",
                            "cartObject", "cartByte", "cartDirty", "tapeObject", "tapeDirty")
            for original, current in zip(original_profiles, restored):
                assert not current["active"]
                assert {key: current[key] for key in restore_keys} == {
                    key: original[key] for key in restore_keys}, (original, current)
            result["restoredProfiles"] = restored
        (work / "result.json").write_text(json.dumps(result, indent=2))
        print(json.dumps(status, indent=2))
        print("PASS", flush=True)
    except Exception:
        for name, app in zip(("Host", "Joiner"), apps):
            try:
                app.send(f"ui_screenshot {work / (name + '-failure.png')}")
                (work / f"{name}-failure-items.json").write_text(json.dumps(app.list_items()))
            except Exception:
                pass
        raise
    finally:
        for app in apps:
            app.stop()
        discovery.shutdown()
        discovery.server_close()
        relay.close()
        service.terminate()
        try:
            service.wait(timeout=4)
        except subprocess.TimeoutExpired:
            service.kill()
            service.wait()
        log.close()


if __name__ == "__main__":
    main()
