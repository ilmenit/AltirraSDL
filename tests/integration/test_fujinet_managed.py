#!/usr/bin/env python3
"""Opt-in real FujiNet-PC managed-file boot integration (native Unix test mode).

  python3 tests/integration/test_fujinet_managed.py \
    --emulator /path/to/AltirraSDL --runtime /path/to/fujinet-pc-ATARI --keep

The supplied official Atari release must contain fujinet, data/, SD/ and
fnconfig.ini. A private runtime template and XDG directories isolate all writes.
Two tiny XEX programs write distinct RAM markers after booting through FujiNet's
real disk protocol. The URL case uses the visible dialog and a local HTTP fixture.
No boot_image, load_state or emulator program-loader command is used. Firmware
must include the official REST mount and /download APIs. Logs also verify actual
SIO READ commands and the private SD ownership probes before mounting.

Success removes temporary artifacts unless --keep or --work-dir is specified.
Failures always retain artifacts. No public Internet service is required.
"""
from __future__ import annotations

import argparse
import configparser
import http.server
import json
import os
from pathlib import Path
import re
import shutil
import socket
import struct
import sys
import tempfile
import threading
import time
import traceback

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "ui"))
from harness import AltirraTestHarness  # noqa: E402


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def free_port(kind):
    with socket.socket(socket.AF_INET, kind) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def marker_xex(marker, address):
    # Standard XEX segments and RUNAD ($02E0); 6502 LDA #imm / STA abs,
    # CLI / JMP self. FujiNet's PicoBoot loads and runs this through SIO.
    code = bytearray()
    for offset, value in enumerate(marker):
        target = address + offset
        code.extend((0xA9, value, 0x8D, target & 255, target >> 8))
    loop = 0x2000 + len(code) + 1
    code.extend((0x58, 0x4C, loop & 255, loop >> 8))

    def segment(start, body):
        return struct.pack("<HH", start, start + len(body) - 1) + body

    return b"\xff\xff" + segment(0x2000, code) + segment(0x02E0, b"\x00\x20")


def wait_status(emu, condition, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        status = emu.send("fujinet status")
        if condition(status):
            return status
        time.sleep(.05)
    raise AssertionError("FujiNet status timeout: " + json.dumps(status))


def click(emu, window, label):
    emu.click(window, label)
    emu.wait_frames(6)


def run_url_dialog(emu, url):
    emu.send("fujinet show")
    emu.wait_frames(6)
    if emu.find_item("FujiNet", "Full controls"):
        click(emu, "FujiNet", "Full controls")
    if not emu.find_item("FujiNet", "Run a file through FujiNet..."):
        click(emu, "FujiNet", "Setup and full controls")
    click(emu, "FujiNet", "Run a file through FujiNet...")
    window = "Run a file through FujiNet"
    click(emu, window, "##FujiNet game URL")
    emu.send("key ctrl+a")
    emu.wait_frames(3)
    emu.send("send_text " + url)
    emu.wait_frames(3)
    click(emu, window, "Download, mount and boot")


def run(args, work):
    release = args.runtime.resolve()
    executable = release / "fujinet"
    for path in (executable, release / "data", release / "SD", release / "fnconfig.ini"):
        check(path.exists(), "Missing official Atari runtime component: " + str(path))
    template = work / "official runtime with spaces"
    template.mkdir()
    shutil.copy2(executable, template / "fujinet")
    shutil.copytree(release / "data", template / "data")
    shutil.copytree(release / "SD", template / "SD")
    ini = configparser.ConfigParser(interpolation=None)
    ini.read(release / "fnconfig.ini")
    # A private template gives the managed copy one known SD host. Neither the
    # supplied release nor an existing user's host configuration is changed.
    ini["Host1"] = {"type": "SD", "name": "SD"}
    with (template / "fnconfig.ini").open("w") as stream:
        ini.write(stream)
    local = work / "local game.xex"
    local.write_bytes(marker_xex(b"FNBOOT", 0x4000))
    url_body = marker_xex(b"FNURL1", 0x4100)
    sentinel = work / "user D1 preserved.atr"
    sentinel_body = b"USER DISK PRESERVED".ljust(720 * 128, b"\0")
    sentinel_bytes = struct.pack("<HHHH8s", 0x0296, len(sentinel_body) // 16,
                                 128, 0, bytes(8)) + sentinel_body
    sentinel.write_bytes(sentinel_bytes)
    requests = []

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            requests.append(self.path)
            if self.path != "/url-game.xex":
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(url_body)))
            self.end_headers()
            self.wfile.write(url_body)

        def log_message(self, *_):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    environment = {
        "XDG_CONFIG_HOME": str(work / "config"),
        "XDG_DATA_HOME": str(work / "data"),
        "XDG_CACHE_HOME": str(work / "cache"),
        "SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy",
        "ALTIRRA_DISPLAY_BACKEND": "sdlrenderer",
    }
    previous = {key: os.environ.get(key) for key in environment}
    os.environ.update(environment)
    emu = AltirraTestHarness(executable=str(args.emulator.resolve()),
                            extra_args=["--ui-mode", "desktop"])
    drains = []
    status = None
    results = []
    try:
        emu.start()
        # Drain pipes while the GUI runs: a verbose runtime must not deadlock
        # the emulator before the harness gets an opportunity to shut it down.
        for name, pipe in (("stdout", emu._proc.stdout), ("stderr", emu._proc.stderr)):
            def drain(source=pipe, output=work / ("emulator-" + name + ".log")):
                with output.open("wb") as target:
                    shutil.copyfileobj(source, target)
            reader = threading.Thread(target=drain, daemon=True)
            reader.start()
            drains.append(reader)
        emu.close_dialog("SetupWizard")
        emu.send("set_window_size 1280 1000")
        emu.pause()
        udp, web = free_port(socket.SOCK_DGRAM), free_port(socket.SOCK_STREAM)
        emu.send(f"fujinet runtime {web} {template / 'fujinet'}")
        emu.send(f"fujinet listen {udp} 0")
        emu.send("fujinet start")
        status = wait_status(emu, lambda value: value["running"] and value["connected"])
        state_dir = Path(status["stateDirectory"])
        cases = (("local", str(local), b"FNBOOT", 0x4000),
                 ("url-dialog", f"http://127.0.0.1:{server.server_port}/url-game.xex",
                  b"FNURL1", 0x4100))
        for name, source, marker, address in cases:
            if name == "local":
                check(not emu.query_state()["state"]["disks"][0]["path"],
                      "Fresh emulator D1 unexpectedly contains an image")
            else:
                emu.attach_disk(0, str(sentinel))
                emu.wait_frames(5)
            before = emu.query_state()["state"]["disks"][0]
            check(bytes(emu.mem_read(address, len(marker))) != marker,
                  "Marker already present before boot")
            if name == "local":
                emu.send("fujinet run " + source)
            else:
                run_url_dialog(emu, source)
            deadline = time.monotonic() + args.timeout
            while time.monotonic() < deadline:
                status = emu.send("fujinet status")
                if bytes(emu.mem_read(address, len(marker))) == marker:
                    break
                if not status["filePending"] and status["error"]:
                    raise AssertionError(status["error"])
                time.sleep(.05)
            else:
                raise AssertionError(name + " did not execute its 6502 marker: " + json.dumps(status))
            emu.pause()
            after = emu.query_state()["state"]["disks"][0]
            check(after == before, "FujiNet boot replaced or ejected emulator D1")
            check(not status["localD1Enabled"], "Local D1 still competes with FujiNet")
            check(sentinel.read_bytes() == sentinel_bytes, "Local user disk was modified")
            results.append({"case": name, "marker": marker.decode(), "address": hex(address),
                            "emulatorD1": after, "localD1Enabled": status["localD1Enabled"]})
            print("PASS", name, marker.decode(), flush=True)
        check(requests == ["/url-game.xex"], "URL dialog did not fetch the fixture exactly once")
        emu.screenshot(str(work / "managed-file-dialog.png"))
        emu.send("fujinet stop")
        status = wait_status(emu, lambda value: not value["running"] and not value["stopping"])
        log_path = state_dir / "fujinet-pc.log"
        log = log_path.read_text(errors="replace")
        reads = len(re.findall(r"CF: 31 52\b", log))
        probes = set(re.findall(r"/AltirraSDL/(\.owner-[0-9a-f]+)/probe\.txt", log))
        check(reads >= 6, "Runtime log lacks real SIO disk reads for both games")
        check(len(probes) >= 2, "Runtime log lacks distinct SD ownership checks for both mounts")
        check(not list((state_dir / "SD" / "AltirraSDL").glob(".owner-*")),
              "Ownership probe was not cleaned up")
        report = {"transport": "native NetSIO, managed official FujiNet-PC",
                  "cases": results, "sioReads": reads, "ownershipProbes": len(probes),
                  "httpRequests": requests, "runtimeLog": str(log_path)}
        (work / "results.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report, indent=2), flush=True)
    finally:
        if emu._sock:
            try:
                (work / "last-status.json").write_text(json.dumps(emu.send("fujinet status"), indent=2))
                emu.send("fujinet stop")
                wait_status(emu, lambda value: not value["running"] and not value["stopping"], 10)
            except Exception as error:
                print("Cleanup stop:", error, file=sys.stderr)
            try:
                emu.send_raw("quit")
            except (OSError, ConnectionError, RuntimeError):
                pass
            if emu._proc:
                try:
                    emu._proc.wait(timeout=10)
                except Exception:
                    pass
        emu.stop()
        for reader in drains:
            reader.join(timeout=3)
        server.shutdown()
        server.server_close()
        thread.join(timeout=3)
        for key, value in previous.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, help="Empty/new directory; retained on success too")
    parser.add_argument("--keep", action="store_true")
    parser.add_argument("--timeout", type=float, default=30, help="Seconds allowed for each real boot")
    args = parser.parse_args()
    if args.work_dir:
        work = args.work_dir.resolve()
        work.mkdir(parents=True, exist_ok=True)
        if any(work.iterdir()):
            parser.error("--work-dir must be empty")
    else:
        work = Path(tempfile.mkdtemp(prefix="altirra-fujinet-managed-"))
    print("ARTIFACTS", work, flush=True)
    try:
        run(args, work)
    except Exception:
        (work / "failure.txt").write_text(traceback.format_exc())
        traceback.print_exc()
        print("Failure artifacts retained:", work, file=sys.stderr)
        return 1
    if not (args.keep or args.work_dir):
        shutil.rmtree(work)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
