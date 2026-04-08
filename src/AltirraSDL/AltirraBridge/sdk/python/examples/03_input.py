#!/usr/bin/env python3
"""03_input.py — Phase 3 input injection example.

Connects, boots past power-on, then drives joystick + console
switches + keyboard. Verifies the input changes are visible in the
PIA / GTIA / POKEY state.

Run:
    1. ./AltirraSDL --bridge
    2. python 03_input.py /tmp/altirra-bridge-<pid>.token
"""

import sys

from altirra_bridge import AltirraBridge


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <token-file>", file=sys.stderr)
        return 2

    with AltirraBridge.from_token_file(sys.argv[1]) as a:
        a.pause()
        a.frame(60)  # let the OS finish booting and configure PIA

        print("Joystick directions on port 0 (PORTA bits 0-3):")
        for direction in ("up", "down", "left", "right",
                          "upleft", "upright", "downleft", "downright",
                          "centre"):
            a.joy(0, direction)
            print(f"  {direction:<10} -> PORTA={a.pia()['PORTA']}")

        print("\nFire button (GTIA TRIG0 via SetControllerTrigger):")
        a.joy(0, "centre", fire=True)
        print(f"  fire pressed  -> consol_in unchanged, trigger now active")
        a.joy(0, "centre", fire=False)

        print("\nConsole switches:")
        a.consol(start=True, select=True)
        print(f"  start+select -> consol_in={a.gtia()['consol_in']}")
        a.consol()  # release
        print(f"  released     -> consol_in={a.gtia()['consol_in']}")

        print("\nKeyboard:")
        for name in ("RETURN", "SPACE", "ESC"):
            resp = a.key(name)
            print(f"  KEY {name:<8} -> kbcode={resp['kbcode']}")

        print("\nMemory write round-trip:")
        a.poke(0x600, 0x42)
        print(f"  POKE $600 $42 -> PEEK $600 = {a.peek(0x600, 1).hex()}")

        a.poke16(0x602, 0x1234)
        print(f"  POKE16 $602 $1234 -> PEEK16 $602 = ${a.peek16(0x602):04x}")

        # Bigger payload via base64
        data = bytes(range(64))
        a.memload(0x700, data)
        print(f"  MEMLOAD/MEMDUMP 64 bytes round-trip: "
              f"{'OK' if a.memdump(0x700, 64) == data else 'FAIL'}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
