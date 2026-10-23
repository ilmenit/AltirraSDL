#!/usr/bin/env python3
"""VBXE inspection, local RAM and FX1.27 blitter through the real server."""
import os
from pathlib import Path
import queue
import subprocess
import sys
import tempfile

from bridge_custom_device_test import start_stderr_reader, wait_for_token_file


def main():
    server = Path(sys.argv[1]).resolve()
    sys.path.insert(0, str(Path(sys.argv[2]).resolve()))
    sdk_test = Path(sys.argv[3]).resolve() if len(sys.argv) > 3 else None
    from altirra_bridge import AltirraBridge, RemoteError
    with tempfile.TemporaryDirectory(prefix="altirra-vbxe-") as td:
        env = os.environ.copy()
        env.update(TMPDIR=td, TEMP=td, TMP=td, XDG_CONFIG_HOME=td)
        proc = subprocess.Popen(
            [str(server), "--bridge=tcp:127.0.0.1:0", *sys.argv[4:]],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
            stdin=subprocess.DEVNULL, text=True, env=env)
        lines, pending = [], queue.Queue()
        reader = start_stderr_reader(proc, lines, pending)
        try:
            token = wait_for_token_file(proc, pending)
            with AltirraBridge.from_token_file(str(token)) as a:
                a.pause()
                assert a.vbxe() == {"ok": True, "present": False}
                try:
                    a.vbxe_read(0)
                    raise AssertionError("Absent VBXE accepted a memory read")
                except RemoteError:
                    pass
                for version in (120, 124, 126, 127):
                    a.device_set("vbxe", True, version=version, base="d700", shared_mem=True)
                    a.pause()
                    state = a.vbxe()
                    assert state["version"] == version and state["present"]
                    assert state["register_base"] == "$d700"
                    assert state["shared_memory"] == (version < 127)
                    assert state["ntsc_vertical_offset"] == (version < 127)
                a.boot_bare()
                a.pause()
                base = int(a.vbxe()["register_base"][1:], 16)
                a.hwpoke(base + 0x4B, 0x99)
                assert a.vbxe()["feature_control"] == "$99"

                # A full protocol-sized transfer and the final physical byte.
                payload = bytes(range(256)) * 64
                before = a.vbxe()
                clock = a.clock()
                a.vbxe_write(0x10000, payload)
                assert a.vbxe_read(0x10000, len(payload)) == payload
                a.vbxe_write(0x7FFFF, b"\xa5")
                assert a.vbxe_read(0x7FFFF) == b"\xa5"
                assert a.vbxe() == before  # RAM access never changes MEMAC/registers
                assert a.clock() == clock  # inspection never advances time
                for command in ("VBXE extra", "VBXE_READ", "VBXE_READ $80000",
                                "VBXE_READ 0 0", "VBXE_READ 0 16385",
                                "VBXE_READ $7FFFF 2", "VBXE_READ 0 1 extra",
                                "VBXE_WRITE 0", "VBXE_WRITE 0 0",
                                "VBXE_WRITE $7FFFF 1122", "VBXE_WRITE $7FFFF aaZZ",
                                "VBXE_WRITE 0 11 extra"):
                    try:
                        a._cmd_ok(command)
                        raise AssertionError(command)
                    except RemoteError:
                        pass
                assert a.vbxe_read(0x7FFFF) == b"\xa5"  # no partial invalid write

                # The BCB offsets match production LoadBCB: one source byte,
                # a two-byte destination expanded by X zoom, Add-Carry mode.
                bcb = bytearray(21)
                bcb[1], bcb[5], bcb[7], bcb[11] = 0x10, 1, 0x20, 1
                bcb[15], bcb[18], bcb[20] = 0xFF, 1, 0x12
                a.vbxe_write(0x1000, b"\x30")
                a.vbxe_write(0x2000, b"\xe0\x10")
                a.vbxe_write(0x4000, bcb)
                for offset, value in ((0x50, 0), (0x51, 0x40), (0x52, 0), (0x54, 0), (0x53, 1)):
                    a.hwpoke(base + offset, value)
                a.cycles(114 * 3)
                assert a.vbxe_read(0x2000, 2) == b"\xff\x00"
                state = a.vbxe()
                assert not state["blitter"]["busy"] and not state["irq_pending"]
                assert not a.clock()["running"]
                # A long live blit exposes the decoded submode and BCB,
                # not just the write-only CPU register readback.
                bcb[12:14] = b"\xff\xff"
                bcb[14] = 0xFF
                bcb[20] = 0x22
                a.vbxe_write(0x4000, bcb)
                a.hwpoke(base + 0x53, 1)
                state = a.vbxe()["blitter"]
                assert state["busy"] and state["mode"] == 2 and state["submode"] == "$20"
                assert len(bytes.fromhex(state["bcb"])) == 21
                a.hwpoke(base + 0x53, 0)
                a.device_set("vbxe", False)
                assert not a.vbxe()["present"]
            if sdk_test:
                subprocess.run([str(sdk_test), str(token)], check=True, timeout=10)
            with AltirraBridge.from_token_file(str(token)) as a:
                a.quit()
            assert proc.wait(timeout=10) == 0
            print("VBXE bridge configuration, RAM and blitter checks passed")
        except Exception:
            print("\n".join(lines), file=sys.stderr)
            raise
        finally:
            if proc.poll() is None:
                proc.kill()
            proc.wait(timeout=5)
            reader.join(timeout=5)


if __name__ == "__main__":
    main()
