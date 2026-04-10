"""OS symbol management — load Atari OS symbols into bridge and project.

The SDK ships symbol tables for known OS ROMs. When the bridge is
running with a matching OS ROM, loading these symbols makes
``bridge.disasm()`` output use the standard names (``VDSLST``,
``SIOV``, ``XITVBV``, etc.) instead of raw hex addresses.

The same symbols are used by ``asm_writer`` to generate the
``equates.asm`` file with only the symbols the program references.

Usage::

    from altirra_bridge.symbols import load_os_symbols, get_os_symbols

    # Load into bridge for disassembly
    load_os_symbols(bridge)

    # Get the dict for equate generation
    syms = get_os_symbols()  # {addr: (name, comment)}
"""

from __future__ import annotations

import os
from pathlib import Path
from typing import Dict, Optional, Tuple, TYPE_CHECKING

if TYPE_CHECKING:
    from .client import AltirraBridge

_DATA_DIR = Path(__file__).parent / "data"


def get_os_symbols() -> Dict[int, Tuple[str, str]]:
    """Return the Atari XL OS Rev.2 symbol table.

    Returns ``{addr: (name, comment)}`` dict with 588 entries
    covering zero-page, page 2-3 vectors, hardware registers,
    and OS ROM entry points.
    """
    from .data.os_symbols import OS_SYMBOLS
    return OS_SYMBOLS


def get_os_lab_path() -> str:
    """Return path to the OS symbols .lab file (MADS format)."""
    return str(_DATA_DIR / "atari_xl_os_rev2.lab")


def load_os_symbols(bridge: "AltirraBridge") -> int:
    """Load Atari XL OS symbols into the bridge's symbol table.

    After this call, ``bridge.disasm()`` output will use standard
    OS names like ``VDSLST``, ``SDLSTL``, ``WARMST``, ``SIOV``,
    etc. Returns the symbol module ID from the bridge.

    Call this BEFORE loading project-specific symbols so the
    project labels take priority over OS names for overlapping
    addresses (like zero-page locations the game repurposes).
    """
    lab_path = get_os_lab_path()
    return bridge.sym_load(lab_path)


def os_name_at(addr: int) -> Optional[str]:
    """Look up the OS symbol name at ``addr``, or ``None``."""
    syms = get_os_symbols()
    entry = syms.get(addr)
    return entry[0] if entry else None
