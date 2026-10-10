#!/usr/bin/env python3
"""POKEY [chip]: the stereo slave's registers through the real bridge."""

from __future__ import annotations

import os
from pathlib import Path
import queue
import subprocess
import sys
import tempfile

from bridge_custom_device_test import start_stderr_reader, wait_for_token_file


def main() -> None:
    server = Path(sys.argv[1]).resolve()
    sys.path.insert(0, str(Path(sys.argv[2]).resolve()))
    from altirra_bridge import AltirraBridge, RemoteError

    with tempfile.TemporaryDirectory(prefix="altirra pokey chip test ") as td:
        env = os.environ.copy()
        env.update(TMPDIR=td, TEMP=td, TMP=td)
        proc = subprocess.Popen(
            [str(server), "--bridge=tcp:127.0.0.1:0"],
            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE, text=True, env=env)
        lines: list[str] = []
        pending: queue.Queue[str] = queue.Queue()
        reader = start_stderr_reader(proc, lines, pending)
        try:
            token_file = wait_for_token_file(proc, pending)
            with AltirraBridge.from_token_file(str(token_file)) as bridge:
                # Mono machine: chip 1 answers, chip 2 is an error.
                bridge.boot_bare()
                # CONFIG stereo cold-resets the machine. Keep it paused so
                # OS startup cannot overwrite the registers under test.
                bridge.pause()
                assert bridge.pokey()["ok"]
                try:
                    bridge.pokey(2)
                    raise AssertionError("POKEY 2 must fail without dual POKEY")
                except RemoteError as e:
                    assert "stereo" in str(e), e
                for chip in (0, -1, 3, 4294967296, "bad"):
                    try:
                        bridge.pokey(chip)
                        raise AssertionError(f"POKEY {chip} must be rejected")
                    except RemoteError as e:
                        assert "chip" in str(e), e

                # Stereo machine: the chips are distinct - a write to the
                # slave's AUDF1 ($D210) shows up in chip 2 only, a write to
                # the primary's AUDF1 ($D200) in chip 1 only.
                bridge.config("stereo", "true")
                bridge.hwpoke(0xd210, 0x5a)
                bridge.hwpoke(0xd200, 0xa5)
                primary = bridge.pokey()
                slave = bridge.pokey(2)
                assert primary["AUDF1"] == "$a5", primary
                assert slave["AUDF1"] == "$5a", slave

                assert bridge.pokey(1) == primary
                assert bridge.hwstate()["pokey"] == {
                    k: v for k, v in primary.items() if k != "ok"}

                bridge.config("stereo", "false")
                assert bridge.pokey()["ok"]
                try:
                    bridge.pokey(2)
                    raise AssertionError("Disabled chip 2 must be inaccessible")
                except RemoteError as e:
                    assert "stereo" in str(e), e

                bridge.quit()
            assert proc.wait(timeout=10) == 0
        except Exception:
            print("\n".join(lines), file=sys.stderr)
            raise
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
            reader.join(timeout=5)
            if proc.stderr:
                proc.stderr.close()
    print("POKEY chip selection passed")


if __name__ == "__main__":
    main()
