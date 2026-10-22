#!/usr/bin/env python3
"""Opt-in live FujiNet Lobby/BBS launcher integration (native Unix build).

  python3 tests/integration/test_fujinet_activities.py \
    --emulator build/linux-release/src/AltirraSDL/AltirraSDL \
    --runtime /path/to/fujinet-pc-ATARI --keep

Clicks the production Network games / Lobby and BBS directory buttons with
BASIC both enabled and disabled. The application fetches its real official
client URLs; the BBS client may fetch the public directory. No source override,
guest keyboard input, login, game joining, BBS connection or message is used.
Success requires the active emulated screen's accessibility text, not strings
found in executable RAM. A supplied official runtime and Internet access are
required. No emulation core or runtime is built by this script.

Settings, runtime data, SD cards and ports are private. Success removes artifacts
unless --keep/--work-dir is supplied; failures always preserve their evidence.
"""
from __future__ import annotations

import argparse
import configparser
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import struct
import sys
import tempfile
import threading
import time
import traceback
import urllib.request

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/ui"))
sys.path.insert(0, str(ROOT / "src/AltirraSDL/AltirraBridge/sdk/python"))
from harness import AltirraTestHarness  # noqa: E402
from altirra_bridge import AltirraBridge  # noqa: E402


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def free_port(kind):
    with socket.socket(socket.AF_INET, kind) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def wait_status(emu, predicate, timeout=15):
    deadline = time.monotonic() + timeout
    status = {}
    while time.monotonic() < deadline:
        status = emu.send("fujinet status")
        if predicate(status):
            return status
        time.sleep(.05)
    raise AssertionError("FujiNet status timeout: " + json.dumps(status))


def make_runtime(release, template):
    for component in ("fujinet", "fnconfig.ini", "data", "SD"):
        check((release / component).exists(),
              "Missing official Atari runtime component: " + str(release / component))
    template.mkdir()
    shutil.copy2(release / "fujinet", template / "fujinet")
    shutil.copytree(release / "data", template / "data")
    shutil.copytree(release / "SD", template / "SD")
    ini = configparser.ConfigParser(interpolation=None)
    ini.read(release / "fnconfig.ini")
    ini["Host1"] = {"type": "SD", "name": "SD"}
    with (template / "fnconfig.ini").open("w") as output:
        ini.write(output)


def begin_draining(emu, work):
    readers = []
    errors = []
    for name, source in (("stdout", emu._proc.stdout), ("stderr", emu._proc.stderr)):
        def drain(pipe=source, target=work / ("emulator-" + name + ".log")):
            try:
                with target.open("wb") as output:
                    shutil.copyfileobj(pipe, output)
            except Exception as error:
                errors.append(str(error))
        reader = threading.Thread(target=drain, daemon=True)
        reader.start()
        readers.append(reader)
    return readers, errors


def screen_ready(activity, text):
    normalized = " ".join(text.lower().split())
    if activity == "bbs":
        return "atari bbs directory" in normalized and "bbs list loaded" in normalized
    # The empty username prompt is the boundary before any user identity or
    # game/room selection. The title alone can appear during a loading splash.
    return "lobby" in normalized and any(
        prompt in normalized for prompt in ("user name", "username", "player name"))


def run_case(args, template, work, activity, label, basic):
    work.mkdir()
    environment = {
        "XDG_CONFIG_HOME": str(work / "config"),
        "XDG_DATA_HOME": str(work / "data"),
        "XDG_CACHE_HOME": str(work / "cache"),
        "SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy",
        "ALTIRRA_DISPLAY_BACKEND": "sdlrenderer",
    }
    previous = {key: os.environ.get(key) for key in environment}
    os.environ.update(environment)
    emu = AltirraTestHarness(executable=str(args.emulator.resolve()), timeout=15,
                            extra_args=["--ui-mode", "desktop", "--bridge"])
    readers, drain_errors, cleanup_errors = [], [], []
    bridge = None
    state_dir = None
    try:
        emu.start()
        readers, drain_errors = begin_draining(emu, work)
        emu.close_dialog("SetupWizard")
        emu.send("set_window_size 1280 1000")
        emu.wait_frames(3)
        bridge = AltirraBridge.from_token_file(
            str(Path(tempfile.gettempdir()) / f"altirra-bridge-{emu._proc.pid}.token"))
        bridge.config("basic", str(basic).lower())
        emu.pause()
        emu.send("accessibility enabled")
        # A real local D1 image stays inserted even when FujiNet takes its bus
        # number. Hash the source and compare the mounted-media UI state too.
        local = work / "preserved local D1.atr"
        body = b"LOCAL DISK MUST BE PRESERVED".ljust(720 * 128, b"\0")
        local.write_bytes(struct.pack("<HHHH8s", 0x0296, len(body) // 16,
                                      128, 0, bytes(8)) + body)
        local_hash = digest(local)
        emu.attach_disk(0, str(local))
        emu.wait_frames(5)
        disk_before = emu.query_state()["state"]["disks"][0]
        udp, web = free_port(socket.SOCK_DGRAM), free_port(socket.SOCK_STREAM)
        emu.send(f"fujinet runtime {web} {template / 'fujinet'}")
        emu.send(f"fujinet listen {udp} 0 200")
        emu.send("fujinet show")
        emu.wait_frames(6)
        if emu.find_item("FujiNet", "Full controls"):
            emu.click("FujiNet", "Full controls")
            emu.wait_frames(6)
        button = emu.find_item("FujiNet", label)
        check(button and not button.get("disabled"), "Production launcher unavailable: " + label)
        # This is the sole launch operation. It exercises automatic runtime
        # startup, real application downloads, mounting and booting in the UI.
        emu.click("FujiNet", label)
        started = time.monotonic()
        deadline = started + args.timeout
        text = ""
        startup_errors = []
        while time.monotonic() < deadline:
            status = emu.send("fujinet status")
            state_dir = Path(status["stateDirectory"])
            if status["error"] and status["error"] not in startup_errors:
                startup_errors.append(status["error"])
            # Mount/reset completion precedes guest startup. An SIO timeout
            # while the client loads its online directory can recover; the
            # active guest screen is the success criterion, not one poll.
            text = emu.send("accessibility status")["text"]
            if screen_ready(activity, text):
                break
            time.sleep(.1)
        else:
            (work / "last-screen.txt").write_text(text)
            (work / "startup-errors.json").write_text(json.dumps(startup_errors, indent=2) + "\n")
            raise AssertionError(label + " did not reach its active guest prompt: "
                                 + repr(text) + "; startup errors: " + repr(startup_errors))
        emu.pause()
        elapsed = time.monotonic() - started
        screenshot = work / "guest-screen.png"
        emu.screenshot(str(screenshot))
        (work / "guest-screen.txt").write_text(text)
        status = emu.send("fujinet status")
        (work / "status.json").write_text(json.dumps(status, indent=2) + "\n")
        check(status["running"] and status["connected"], "Runtime disconnected at guest prompt")
        check(not status["activityPending"] and not status["filePending"],
              "Guest prompt appeared before launch completion")
        check(not status["localD1Enabled"], "Local D1 still competes with FujiNet")
        check(bridge.config("basic")["basic"] == basic, "Launcher changed the BASIC preference")
        check(digest(local) == local_hash, "Launcher modified the local disk image")
        disk_after = emu.query_state()["state"]["disks"][0]
        check(disk_after == disk_before, "Launcher replaced/ejected the local disk image")
        with urllib.request.urlopen(f"http://127.0.0.1:{web}/api/v1/drives", timeout=10) as response:
            drives = json.load(response)
        d1 = next(item for item in drives if item["slot"] == 1)
        check(d1["mounted"] and d1["mode"] == "r", "FujiNet D1 is not mounted read-only")
        mounted = state_dir / "SD" / d1["path"].lstrip("/")
        check(mounted.is_file(), "Downloaded client missing from private SD")
        result = {"activity": activity, "button": label, "basic": basic,
                  "seconds": round(elapsed, 2), "screen": str(screenshot),
                  "startupErrors": startup_errors,
                  "localDiskSHA256": local_hash, "clientSHA256": digest(mounted),
                  "fujiNetD1": d1, "runtimeLog": str(state_dir / "fujinet-pc.log")}
        print("PASS", activity, "basic=" + str(basic), str(screenshot), flush=True)
        return result
    finally:
        failure_active = sys.exc_info()[0] is not None
        if bridge:
            bridge.close()
        if emu._sock:
            try:
                emu.send("fujinet stop")
                wait_status(emu, lambda value: not value["running"] and not value["stopping"], 10)
                (work / "cleanup.json").write_text('{"runtimeStopped":true}\n')
            except Exception as error:
                cleanup_errors.append("Managed runtime stop failed: " + str(error))
            try:
                emu.send("quit")
                emu._proc.wait(timeout=10)
            except Exception as error:
                cleanup_errors.append("Graceful emulator exit failed: " + str(error))
        emu.stop()
        for reader in readers:
            reader.join(timeout=3)
            if reader.is_alive():
                cleanup_errors.append("Emulator output pipe did not close")
        cleanup_errors.extend(drain_errors)
        for key, value in previous.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value
        if cleanup_errors:
            message = "\n".join(cleanup_errors)
            (work / "cleanup-errors.txt").write_text(message + "\n")
            if failure_active:
                print(message, file=sys.stderr)
            else:
                raise AssertionError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, help="New/empty directory, retained after success")
    parser.add_argument("--keep", action="store_true")
    parser.add_argument("--timeout", type=float, default=75, help="Seconds allowed per live launch")
    args = parser.parse_args()
    if args.work_dir:
        work = args.work_dir.resolve()
        work.mkdir(parents=True, exist_ok=True)
        if any(work.iterdir()):
            parser.error("--work-dir must be empty")
    else:
        work = Path(tempfile.mkdtemp(prefix="altirra-fujinet-activities-"))
    print("ARTIFACTS", work, flush=True)
    try:
        template = work / "official runtime with spaces"
        make_runtime(args.runtime.resolve(), template)
        results = []
        for activity, label in (("lobby", "Network games / Lobby"), ("bbs", "BBS directory")):
            for basic in (False, True):
                case = work / (activity + ("-basic-on" if basic else "-basic-off"))
                results.append(run_case(args, template, case, activity, label, basic))
        (work / "results.json").write_text(json.dumps(results, indent=2) + "\n")
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
