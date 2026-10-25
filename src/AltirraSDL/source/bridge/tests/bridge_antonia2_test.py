#!/usr/bin/env python3
"""Exercise the upstream Antonia 2 descriptor through real 65C816 bus cycles."""
from __future__ import annotations

import os
from pathlib import Path
import queue
import random
import subprocess
import sys
import tempfile

from bridge_custom_device_test import start_stderr_reader, wait_for_token_file


def main() -> None:
    if len(sys.argv) != 4:
        raise SystemExit("usage: bridge_antonia2_test.py EMULATOR SDK_DIR --builtin|DESCRIPTOR")
    server, sdk = (Path(arg).resolve() for arg in sys.argv[1:3])
    builtin = sys.argv[3] == "--builtin"
    if builtin and os.name == "nt":
        print("SKIP: private INI profile fixture is POSIX-only")
        raise SystemExit(77)
    descriptor = None if builtin else Path(sys.argv[3]).resolve()
    sys.path.insert(0, str(sdk))
    from altirra_bridge import AltirraBridge

    with tempfile.TemporaryDirectory(prefix="altirra antonia2 test ") as td:
        env = os.environ.copy()
        env.update(TMPDIR=td, TEMP=td, TMP=td, XDG_CONFIG_HOME=td,
                   SDL_AUDIODRIVER="dummy", ALTIRRA_MACOS_FORCE_IOKIT="1")
        config = Path(td) / "altirra"
        config.mkdir()
        if builtin:
            (config / "settings.ini").write_text(
                '[User\\AltirraSDL\\Profiles]\n'
                '"Defaults inited" = 1\n"Current profile" = 0\n'
                '[User\\AltirraSDL\\Profiles\\00000000]\n'
                '"CPU: Antonia2 MULDIV Block" = 1\n')
        proc = subprocess.Popen(
            [str(server), "--bridge=tcp:127.0.0.1:0",
             "--cpu", "65c816", "--highbanks", "63"],
            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE, text=True, env=env)
        lines: list[str] = []
        pending: queue.Queue[str] = queue.Queue()
        reader = start_stderr_reader(proc, lines, pending)
        checks = 0
        try:
            token = wait_for_token_file(proc, pending)
            with AltirraBridge.from_token_file(str(token)) as a:
                a.pause()
                a.boot_bare()
                assert a.regs()["mode"] == "65C816"
                # Bank 0 ROM and an unrelated high bank must not alias FF:F0xx.
                rom = a.memdump(0xF000, 256)
                sentinel = bytes(range(256))
                if not builtin:
                    loaded = a.device_set("custom", True, path=str(descriptor))
                    assert loaded["config_loaded"], loaded
                    # Device attachment resets the machine; regain the parked CPU.
                    a.boot_bare()
                a.memload(0x12F000, sentinel)
                assert a.memdump(0xFFF008, 8) == b"\xff" * 8
                assert a.memdump(0xF000, 256) == rom

                program = bytearray()
                expected = bytearray()
                checkpoints: list[tuple[int, str]] = []
                operands = bytearray(b"\xff" * 8)
                result = bytearray(b"\xff" * 8)
                # Preserve the park jump's low address byte. The CPU may have
                # fetched it already when paused halfway through JMP $060F.
                code_base, output_base = 0x200F, 0x6000

                def write_byte(offset: int, value: int) -> None:
                    program.extend((0xA9, value, 0x8F))
                    program.extend((0xFFF008 + offset).to_bytes(3, "little"))
                    operands[offset] = value
                    if offset < 4:
                        n = int.from_bytes(operands[:2], "little")
                        d = int.from_bytes(operands[2:4], "little")
                        q, r = divmod(n, d) if d else (0xFFFF, n)
                        result[:4] = q.to_bytes(2, "little") + r.to_bytes(2, "little")
                    else:
                        x = int.from_bytes(operands[4:6], "little")
                        y = int.from_bytes(operands[6:8], "little")
                        result[4:] = (x * y).to_bytes(4, "little")

                def capture(label: str, offsets=range(8)) -> None:
                    nonlocal checks
                    for offset in offsets:
                        destination = output_base + len(expected)
                        program.append(0xAF)  # LDA long: actual CPU register read
                        program.extend((0xFFF008 + offset).to_bytes(3, "little"))
                        program.append(0x8D)  # STA absolute: collect result in RAM
                        program.extend(destination.to_bytes(2, "little"))
                        checkpoints.append((len(expected), label))
                        expected.append(result[offset])
                    checks += 1

                capture("initial readback is FF, without implicit calculation")
                # Every byte is a trigger; partial operands and unchanged bytes matter.
                for offset, value in ((0, 0x34), (1, 0x12), (2, 0), (3, 0),
                                      (4, 3), (5, 1), (6, 2), (7, 0)):
                    write_byte(offset, value)
                    capture(f"single-byte update {offset}")
                capture("repeated read has no side effects")

                rng = random.Random(0xA2702)
                pairs = [(0, 0), (0x1234, 0), (1000, 30), (0xFFFF, 0xFFFF),
                         (0xFFFF, 1), (0x8000, 2), (0x1234, 0x10)]
                pairs += [(rng.randrange(65536), rng.randrange(65536))
                          for _ in range(24)]
                for x, y in pairs:
                    for offset, value in enumerate(x.to_bytes(2, "little") +
                                                   y.to_bytes(2, "little")):
                        write_byte(offset, value)
                    capture(f"unsigned division {x}/{y}")
                    for offset, value in enumerate(x.to_bytes(2, "little") +
                                                   y.to_bytes(2, "little"), 4):
                        write_byte(offset, value)
                    capture(f"unsigned multiplication {x}*{y}")
                # Native M=0 uses two byte bus writes for a 16-bit STA long.
                program.extend(bytes.fromhex("18 fb c2 20 a9 34 12 8f 0c f0 ff"))
                operands[4:6] = bytes.fromhex("34 12")
                result[4:] = (0x1234 * int.from_bytes(operands[6:8], "little")).to_bytes(4, "little")
                for address in (0xFFF00C, 0xFFF00E):
                    program.append(0xAF)
                    program.extend(address.to_bytes(3, "little"))
                    program.append(0x8D)
                    program.extend((output_base + len(expected)).to_bytes(2, "little"))
                    start = address - 0xFFF008
                    expected.extend(result[start:start + 2])
                program.extend(bytes.fromhex("e2 20 38 fb"))  # M=1, return E=1
                capture("native word write/read preserves both independent blocks")
                # A sentinel proves the whole program executed before we inspect RAM.
                program.extend(bytes.fromhex("a9 a5 8d ff 7f"))
                park = code_base + len(program)
                program.append(0x4C)
                program.extend(park.to_bytes(2, "little"))
                assert code_base + len(program) < output_base
                assert output_base + len(expected) < 0x7FFF
                a.memload(code_base, program)
                a.memload(output_base, bytes(len(expected)))
                a.poke(0x7FFF, 0)
                # Replace BOOT_BARE's park loop; do not depend on register mutation.
                a.memload(0x060F, bytes((0x4C, code_base & 255, code_base >> 8)))
                a.cycles(50000)
                assert a.peek(0x7FFF) == b"\xa5", a.regs()
                actual = a.memdump(output_base, len(expected))
                if actual != expected:
                    first = next(i for i, (x, y) in enumerate(zip(actual, expected)) if x != y)
                    label = next((label for index, label in reversed(checkpoints)
                                  if index <= first), "native word")
                    raise AssertionError(f"{label}: byte {first}: "
                                         f"got {actual[first]:02x}, expected {expected[first]:02x}")
                assert a.memdump(0xF000, 256) == rom
                assert a.memdump(0x12F000, 256) == sentinel
                assert a.memdump(0xFFF008, 8) == result
                if builtin:
                    snapshot = str(Path(td) / "antonia2.altstate2")
                    a.state_save(snapshot)
                    a.memload(0xFFF008, bytes([operands[0] ^ 0xFF]))
                    assert a.memdump(0xFFF008, 8) != result
                    a.state_load(snapshot)
                    assert a.memdump(0xFFF008, 8) == result, "snapshot lost readback"
                    a.memload(0xFFF009, bytes([operands[1] ^ 0xFF]))
                    n = operands[0] | ((operands[1] ^ 0xFF) << 8)
                    d = int.from_bytes(operands[2:4], "little")
                    q, r = divmod(n, d) if d else (0xFFFF, n)
                    assert a.memdump(0xFFF008, 4) == q.to_bytes(2, "little") + r.to_bytes(2, "little"), "snapshot lost operands"
                    a.state_load(snapshot)
                a.warm_reset()
                assert a.memdump(0xFFF008, 8) == result, "warm reset changed state"
                a.cold_reset()
                assert a.memdump(0xFFF008, 8) == b"\xff" * 8, "cold reset recalculated results"
                if not builtin:
                    a.device_remove("custom")
                a.quit()
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
            reader.join(timeout=2)
    print(f"Antonia 2: {checks} CPU checkpoints, native word transfers, "
          "bank isolation and reset tests passed")


if __name__ == "__main__":
    main()
