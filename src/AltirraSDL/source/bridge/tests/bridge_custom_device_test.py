#!/usr/bin/env python3
"""End-to-end custom-device coverage for the lean bridge server."""

from __future__ import annotations

import os
from pathlib import Path
import queue
import secrets
import socket
import subprocess
import sys
import tempfile
import threading
import time


def fail(message: str) -> None:
    raise AssertionError(message)


def check(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def start_stderr_reader(proc: subprocess.Popen[str], lines: list[str],
        pending: queue.Queue[str]) -> threading.Thread:
    def read_stderr() -> None:
        assert proc.stderr is not None
        for line in proc.stderr:
            lines.append(line.rstrip())
            pending.put(line.rstrip())

    thread = threading.Thread(target=read_stderr, daemon=True)
    thread.start()
    return thread


def wait_for_token_file(proc: subprocess.Popen[str],
        pending: queue.Queue[str]) -> Path:
    deadline = time.monotonic() + 10
    prefix = "[Bridge] token-file: "
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            fail(f"bridge server exited during startup with {proc.returncode}")
        try:
            line = pending.get(timeout=0.1)
        except queue.Empty:
            continue
        if line.startswith(prefix):
            return Path(line[len(prefix):])
    fail("timed out waiting for bridge token file")


def expect_custom_load_error(bridge, path: Path, text: str) -> dict:
    from altirra_bridge import RemoteError

    try:
        bridge.device_set("custom", True, path=str(path), allowunsafe=True)
    except RemoteError as exc:
        response = exc.response
        check(response.get("ok") is False, "load failure must set ok=false")
        check(response.get("present") is True,
              "failed custom device must remain installed for hot reload")
        check(response.get("config_loaded") is False,
              "failed custom device must report config_loaded=false")
        check(response.get("healthy") is False,
              "failed custom device must report healthy=false")
        diagnostics = response.get("diagnostics", [])
        check(any(text in item for item in diagnostics),
              f"expected {text!r} in diagnostics: {diagnostics!r}")
        check(response.get("settings", {}).get("path") == str(path),
              "error response must preserve the exact descriptor path")
        return response
    fail("DEVICE_SET unexpectedly accepted an invalid custom descriptor")


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: bridge_custom_device_test.py SERVER SDK_DIR",
              file=sys.stderr)
        return 2

    server = Path(sys.argv[1]).resolve()
    sys.path.insert(0, str(Path(sys.argv[2]).resolve()))
    from altirra_bridge import AltirraBridge, RemoteError

    with tempfile.TemporaryDirectory(prefix="altirra bridge devices ") as td:
        temp_dir = Path(td)
        env = os.environ.copy()
        env["TMPDIR"] = td
        env["TEMP"] = td
        env["TMP"] = td
        proc = subprocess.Popen(
            [str(server), "--bridge=tcp:127.0.0.1:0"],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            env=env,
        )
        stderr_lines: list[str] = []
        pending: queue.Queue[str] = queue.Queue()
        stderr_thread = start_stderr_reader(proc, stderr_lines, pending)

        try:
            token_file = wait_for_token_file(proc, pending)
            with AltirraBridge.from_token_file(str(token_file)) as bridge:
                missing = temp_dir / "missing descriptor.atdevice"
                expect_custom_load_error(bridge, missing, "Cannot open file")

                malformed = temp_dir / "malformed descriptor.atdevice"
                malformed.write_text(
                    'option "name": "broken";\nthis is invalid\n',
                    encoding="utf-8")
                expect_custom_load_error(bridge, malformed, "Unknown type")

                try:
                    bridge.device_set("custom", True, path=str(malformed),
                                      typo=True)
                except RemoteError as exc:
                    check("unknown custom device option 'typo'" in exc.error,
                          f"unexpected unknown-option error: {exc.error}")
                else:
                    fail("unknown custom-device option was silently accepted")

                try:
                    bridge.device_set("custom", True, path=str(malformed),
                                      hotreload="sometimes")
                except RemoteError as exc:
                    check("requires true/false or on/off" in exc.error,
                          f"unexpected boolean-option error: {exc.error}")
                else:
                    fail("invalid custom-device boolean was silently accepted")

                listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                for _ in range(100):
                    port = 20000 + secrets.randbelow(20000)
                    try:
                        listener.bind(("127.0.0.1", port))
                        break
                    except OSError:
                        continue
                else:
                    fail("unable to reserve a custom-device test port")
                listener.listen(1)
                listener.settimeout(10)
                accepted: queue.Queue[object] = queue.Queue()

                def accept_connection() -> None:
                    try:
                        connection, address = listener.accept()
                        connection.close()
                        accepted.put(address)
                    except Exception as exc:
                        accepted.put(exc)

                accept_thread = threading.Thread(
                    target=accept_connection, daemon=True)
                accept_thread.start()

                valid = temp_dir / "valid network descriptor.atdevice"
                valid.write_text(
                    'option "name": "bridge regression";\n'
                    f'option "network": {{ port: {port} }};\n'
                    'event "init": function { $sio.enable_raw(true); };\n',
                    encoding="utf-8")

                response = bridge.device_set(
                    "custom", True, path=str(valid), allowunsafe=True)
                check(response.get("config_loaded") is True,
                      f"valid descriptor did not load: {response!r}")
                check(response.get("settings", {}).get("path") == str(valid),
                      "quoted path with spaces did not round-trip")
                accept_result = accepted.get(timeout=10)
                if isinstance(accept_result, Exception):
                    raise accept_result
                accept_thread.join(timeout=1)
                listener.close()

                syntax = bridge._send_command('DEVICE_GET "custom')
                check(syntax.get("ok") is False and
                      "unterminated quoted argument" in syntax.get("error", ""),
                      f"malformed quoting was not rejected: {syntax!r}")

                bridge.device_remove("custom")

                printer = bridge.device_add(
                    "printer", parent="/", translation_mode="default")
                check(printer.get("path") == "/printer",
                      f"unexpected root printer path: {printer!r}")

                try:
                    bridge.device_add("fx80", parent="/", auto_lf=True)
                except RemoteError as exc:
                    check("requires a compatible parent bus" in exc.error,
                          f"unexpected child-at-root error: {exc.error}")
                else:
                    fail("child-only FX-80 was accepted at the root")

                second_printer = bridge.device_add("printer", parent="/")
                check(second_printer.get("path") == "/printer.1",
                      f"duplicate root device index was lost: {second_printer!r}")
                check(bridge.device_get("/printer.1").get("present") is True,
                      "indexed root path did not resolve")

                try:
                    bridge.device_add("fx80", parent="/printer.1")
                except RemoteError as exc:
                    check("invalid parent bus path" in exc.error,
                          f"unexpected incompatible-bus error: {exc.error}")
                else:
                    fail("FX-80 was accepted on a device path instead of a bus")

                try:
                    bridge.device_add("printer", parent="/printer/parport")
                except RemoteError as exc:
                    check("is not compatible with parent bus" in exc.error,
                          f"unexpected incompatible-device error: {exc.error}")
                else:
                    fail("root-only printer was accepted as a child")

                printer_825 = bridge.device_add(
                    "825", parent="/printer.1/parport")
                printer_825_path = "/printer.1/parport/825"
                check(printer_825.get("path") == printer_825_path,
                      f"numeric child tag did not round-trip: {printer_825!r}")
                bridge.device_remove(printer_825_path)

                myide = bridge.device_add("myide-d1xx", parent="/")
                check(myide.get("path") == "/myide-d1xx",
                      f"hyphenated root tag did not round-trip: {myide!r}")
                check(bridge.device_get("/myide-d1xx").get("present") is True,
                      "hyphenated root path did not resolve")
                bridge.device_remove("/myide-d1xx")

                fx80 = bridge.device_add(
                    "fx80", parent="/printer/parport",
                    auto_lf=True, intl_mode=1)
                fx80_path = "/printer/parport/fx80"
                check(fx80.get("path") == fx80_path,
                      f"unexpected FX-80 child path: {fx80!r}")
                fx80_get = bridge.device_get(fx80_path)
                check(fx80_get.get("settings", {}).get("auto_lf") is True,
                      f"FX-80 boolean setting was not applied: {fx80_get!r}")
                check(fx80_get.get("settings", {}).get("intl_mode") == 1,
                      f"FX-80 integer setting was not applied: {fx80_get!r}")
                installed_paths = {
                    item.get("path") for item in
                    bridge.device_list().get("installed", [])
                }
                check({"/printer", fx80_path} <= installed_paths,
                      f"device tree paths missing from DEVICE_LIST: {installed_paths!r}")

                extra_list_arg = bridge._send_command("DEVICE_LIST extra")
                check(extra_list_arg.get("ok") is False,
                      "DEVICE_LIST silently accepted an extra argument")

                bridge.device_remove(fx80_path)
                bridge.device_remove("/printer.1")
                bridge.device_remove("printer")
                bridge.quit()
        finally:
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                if proc.poll() is None:
                    proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=5)
            stderr_thread.join(timeout=1)

        if proc.returncode not in (0, -15):
            print("\n".join(stderr_lines), file=sys.stderr)
            fail(f"bridge server exited with {proc.returncode}")

    print("Bridge custom-device integration tests passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise
