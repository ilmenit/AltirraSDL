#!/usr/bin/env python3
"""05_state_checkpoints.py — save-state capabilities walkthrough.

This example exercises every mode of the bridge's save-state API:
path, slot, inline, and the auto-rewinding ``checkpoint()`` context
manager.

The canonical analyst workflow is "boot once, save a checkpoint,
probe destructively, rewind, probe again." In-memory slots make
that fast (no disk I/O); the context manager wraps the
save/rewind/drop bookkeeping into a ``with`` block.

Run:

    1. start a bridge server (either GUI or headless)::

         ./AltirraSDL --bridge=tcp:127.0.0.1:0
       or
         ./AltirraBridgeServer --bridge=tcp:127.0.0.1:0

    2. python 05_state_checkpoints.py /tmp/altirra-bridge-<pid>.token
"""

import os
import sys
import tempfile

from altirra_bridge import AltirraBridge


def banner(title: str) -> None:
    print()
    print("=" * 60)
    print(f"  {title}")
    print("=" * 60)


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <token-file>", file=sys.stderr)
        return 2

    with AltirraBridge.from_token_file(sys.argv[1]) as a:
        # Boot the bare-metal stub so we have a known, deterministic
        # state ($600..$611 hold the stub bytes; the CPU is parked at
        # JMP * inside that range). We can scribble RAM and verify
        # rewinds by checking that $600 is back to 0x78 (the first
        # stub byte, SEI).
        a.boot_bare()
        original = a.peek(0x600).hex()
        print(f"Initial PEEK($600) = {original} (expect '78')")
        assert original == "78"

        # --- Anonymous checkpoint -----------------------------------
        # Save now, probe, auto-rewind on block exit.
        banner("1. checkpoint() — anonymous, auto-rewind")
        with a.checkpoint() as cp:
            print(f"slot = {cp.slot}")
            print(f"saved at cycle {cp.info['cycle']}, PC {cp.info['pc']}")
            a.poke(0x600, 0xee)
            print(f"  inside block: PEEK($600) = {a.peek(0x600).hex()}")
            a.frame(2)
        print(f"after block: PEEK($600) = {a.peek(0x600).hex()} (expect 78)")
        assert a.peek(0x600).hex() == "78"
        print(f"slots remaining: {a.state_list()}")  # auto-dropped

        # --- Named checkpoint, persists -----------------------------
        banner("2. checkpoint('name') — persists after block exit")
        with a.checkpoint("level_2") as cp:
            a.poke(0x600, 0x42)
            print(f"  inside block: PEEK($600) = {a.peek(0x600).hex()}")
        print(f"after block: PEEK($600) = {a.peek(0x600).hex()}")
        slots = a.state_list()
        print(f"slots: {[s['name'] for s in slots]}  (level_2 persists)")

        # Reload the named slot later.
        a.poke(0x600, 0x99)
        a.state_load(slot="level_2")
        print(f"after manual state_load(slot='level_2'): PEEK($600) "
              f"= {a.peek(0x600).hex()}")

        # --- cp.keep() suppresses rewind ----------------------------
        banner("3. cp.keep() — experiment succeeded, keep the state")
        a.poke(0x600, 0x11)
        with a.checkpoint() as cp:
            a.poke(0x600, 0x22)
            cp.keep()                          # don't rewind on exit
        print(f"after keep(): PEEK($600) = {a.peek(0x600).hex()} (expect 22)")

        # --- Trial loop using a single named checkpoint -------------
        banner("4. trial loop — save once, reload many times")
        a.state_save(slot="trial_base")
        for trial in range(3):
            a.state_load(slot="trial_base")
            # Each trial mutates state independently.
            a.poke(0x600, 0xa0 + trial)
            a.frame(2)
            print(f"  trial {trial}: PEEK($600) = "
                  f"{a.peek(0x600).hex()}")

        # --- File mode round-trip -----------------------------------
        banner("5. file mode — persistent across server restarts")
        with tempfile.NamedTemporaryFile(suffix=".altstate2",
                                         delete=False) as f:
            path = f.name
        try:
            info = a.state_save(path)
            print(f"saved to {path}: {info['size']} bytes")
            a.poke(0x600, 0xff)
            print(f"after poke: PEEK($600) = {a.peek(0x600).hex()}")
            a.state_load(path)
            print(f"after path load: PEEK($600) = {a.peek(0x600).hex()}")
        finally:
            os.unlink(path)

        # --- Inline mode round-trip ---------------------------------
        # Same blob, just travelling over the socket. Useful when
        # client and server don't share a filesystem.
        banner("6. inline mode — blob over the socket")
        info = a.state_save(inline=True)
        blob = info["data"]
        print(f"inline blob: {len(blob)} bytes (already base64-decoded)")
        a.poke(0x600, 0xab)
        print(f"after poke: PEEK($600) = {a.peek(0x600).hex()}")
        a.state_load(data=blob)
        print(f"after inline load: PEEK($600) = {a.peek(0x600).hex()}")

        # --- Cleanup ------------------------------------------------
        banner("7. cleanup")
        dropped = a.state_drop(all=True)
        print(f"dropped {dropped} slot(s); state_list() = {a.state_list()}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
