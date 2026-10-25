#!/usr/bin/env python3
"""Bridge/MCP input ownership, lossless TYPE, and Unicode screen decoding."""
import os
from pathlib import Path
import queue
import socket
import subprocess
import sys
import tempfile

from bridge_custom_device_test import start_stderr_reader, wait_for_token_file
from bridge_mcp_test import MCP


def main():
    server = Path(sys.argv[1]).resolve()
    sys.path.insert(0, str(Path(sys.argv[2]).resolve()))
    from altirra_bridge import AltirraBridge

    with tempfile.TemporaryDirectory(prefix="altirra-input-") as td:
        env = os.environ.copy()
        env.update(TMPDIR=td, TEMP=td, TMP=td, XDG_CONFIG_HOME=td)
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        proc = subprocess.Popen(
            [str(server), "--bridge=tcp:127.0.0.1:0", "--settings=none",
             f"--mcp={port}", "--mcp-token=test-token"],
            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE, text=True, env=env)
        lines, pending = [], queue.Queue()
        reader = start_stderr_reader(proc, lines, pending)
        try:
            token = wait_for_token_file(proc, pending)
            mcp = MCP(port)
            with AltirraBridge.from_token_file(str(token)) as a:
                a.pause()
                a.boot_bare()
                # POKEY SKCTL bit 1 enables keyboard scan (pokey.cpp).
                a.hwpoke(0xD20F, 3)

                def status():
                    a.cycles(40000)
                    # POKEY SKSTAT bits 2/3: active-low key/SHIFT state.
                    return a.peek(0xD20F)[0] & 0x0C

                a.key_raw("A", shift=True)
                mcp.tool("hold_key", {"key": "A", "down": True, "shift": True})
                assert status() == 0
                a.key_raw("A", down=False, shift=True)
                assert status() == 0, "release must preserve the MCP key and SHIFT"
                a.key_raw("all", down=False)
                assert status() == 0, "release-all must preserve the MCP context"
                mcp.tool("hold_key", {"key": "all", "down": False})
                assert status() == 0x0C

                # One client's two shifted keys each retain modifier ownership.
                a.key_raw("A", shift=True)
                a.key_raw("B", shift=True)
                a.key_raw("A", down=False, shift=True)
                assert status() & 0x08 == 0
                a.key_raw("B", down=False)  # release needn't repeat modifiers
                assert status() == 0x0C

                text = '  PRINT "A  B"  '
                typed = a.type(text)
                assert typed["typed"] == len(text) and typed["skipped"] == [], typed
                # U+10041 shares low 16 bits with ASCII A; it must be skipped,
                # not truncated to A on platforms with 32-bit wchar_t.
                skipped = a.type("\U00010041")
                assert skipped["typed"] == 0 and skipped["skipped"] == ["\U00010041"], skipped

                a.boot_bare()  # reset cooked-key queue and keep OS/VBI inactive
                # ANTIC mode 2, normal 40-column width, LMS $4000, JVB $3000.
                # Opcodes match antic.cpp display-list decode; internal-to-
                # ATASCII encodings match ATCore/source/atascii.cpp assertions.
                a.memload(0x3000, bytes.fromhex("707070420040410030"))
                a.memload(0x4000, bytes([0x21, 0, 0x40, 0x60]) + bytes(36))
                for address, value in ((0xD402, 0), (0xD403, 0x30),
                                       (0xD400, 0x22), (0xD409, 0xE0)):
                    a.hwpoke(address, value)
                a.frame(2)
                screen = a.screen_text()
                assert "A \u2665\u2666" in screen, screen
                a.memload(0x4000, bytes([0x40, 0x41, 0x5A]) + bytes(37))
                a.hwpoke(0xD409, 0xCC)  # international character set
                a.frame(2)
                screen = a.screen_text()
                assert "\u00e1\u00f9\u00c5" in screen, screen
                a.quit()
            assert proc.wait(timeout=10) == 0
        except Exception:
            print("\n".join(lines), file=sys.stderr)
            raise
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
            reader.join(timeout=5)
            proc.stderr.close()
    print("Bridge input ownership, TYPE, and Unicode screen text OK")


if __name__ == "__main__":
    main()
