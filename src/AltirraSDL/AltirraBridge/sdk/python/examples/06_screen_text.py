#!/usr/bin/env python3
"""06_screen_text.py — decode the current screen to text via SCREEN_TEXT.

Connects to a running AltirraSDL launched with --bridge, calls
SCREEN_TEXT (decodes every ANTIC text-mode row on screen, top to
bottom), and prints each line to stdout.

Run:
    1. In one terminal: ./AltirraSDL --bridge
       It logs lines like:
         [Bridge] listening on tcp:127.0.0.1:54321
         [Bridge] token-file: /tmp/altirra-bridge-12345.token
    2. In another terminal:
         python 06_screen_text.py /tmp/altirra-bridge-12345.token
"""

import sys

from altirra_bridge import AltirraBridge


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <token-file>", file=sys.stderr)
        print(
            "  The token file path is logged on stderr by AltirraSDL\n"
            "  when launched with --bridge, e.g. /tmp/altirra-bridge-<pid>.token",
            file=sys.stderr,
        )
        return 2

    with AltirraBridge.from_token_file(sys.argv[1]) as a:
        lines = a.screen_text()

        if not lines:
            print("(no text-mode content on screen)")
        else:
            for line in lines:
                print(line)

    return 0


if __name__ == "__main__":
    sys.exit(main())
