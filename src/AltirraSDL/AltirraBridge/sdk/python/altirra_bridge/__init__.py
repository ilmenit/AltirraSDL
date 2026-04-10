"""altirra_bridge — Python client for the AltirraBridge protocol.

Connect to a running AltirraSDL launched with --bridge and drive it
programmatically: frame-step, peek/poke memory, capture screenshots,
inject input, run the debugger.

Quick start — scripting the emulator::

    from altirra_bridge import AltirraBridge

    with AltirraBridge.from_token_file("/tmp/altirra-bridge-12345.token") as a:
        a.boot("game.xex")
        a.frame(60)
        png = a.screenshot()
        open("title.png", "wb").write(png)

Quick start — reverse-engineering analysis::

    from altirra_bridge import AltirraBridge
    from altirra_bridge.analyzer import recursive_descent, classify_gap, BridgeEmu

    with AltirraBridge.from_token_file(tok) as bridge:
        bridge.boot("game.xex")
        bridge.frame(300)
        bridge.pause()
        ram = bytearray(bridge.memdump(0x0000, 0xFFFF))

    regions, xrefs = recursive_descent(ram, [(0xA000, 'entry')])

See ``altirra_bridge.analyzer`` subpackage for the full RE toolkit.
"""

from .client import AltirraBridge, BridgeError, AuthError, RemoteError, RawFrame
from .loader import XexImage, XexSegment, parse_xex, load_xex
from .project import Project, Note
from . import install_skills

__all__ = [
    # Client
    "AltirraBridge", "BridgeError", "AuthError", "RemoteError", "RawFrame",
    # XEX loader
    "XexImage", "XexSegment", "parse_xex", "load_xex",
    # RE project
    "Project", "Note",
    # Skills installer
    "install_skills",
]
__version__ = "0.1.0"
