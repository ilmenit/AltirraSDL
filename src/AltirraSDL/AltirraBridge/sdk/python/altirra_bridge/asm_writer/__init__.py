"""MADS assembly source generator for altirra_bridge projects.

Generates MADS-compatible .asm files from a Project's analysis state
plus live disassembly from the bridge server. Produces:

- ``main.asm``        — master file with ``opt h-``, ORG brackets, ICL chain
- ``equates.asm``     — referenced OS/HW symbol definitions
- ``seg_NN_XXXX.asm`` — per-segment source with code/data emission

Usage::

    from altirra_bridge import AltirraBridge, Project, load_xex
    from altirra_bridge.asm_writer import write_all, verify

    image = load_xex("game.xex")
    proj  = Project.load("game.pyA8/project.json")

    with AltirraBridge.from_token_file(token) as bridge:
        bridge.boot("game.xex"); bridge.frame(300); bridge.pause()
        summary = write_all(bridge, image, proj, "game.pyA8/exports")

    print(verify(proj, "game.pyA8/exports"))

Backward compatibility: the old single-function API
(``emit_xex``, ``emit_segment``, ``export_xex_to_file``) is
preserved as thin wrappers for callers that haven't migrated.
"""

from .emit import write_all, write_main, write_equates, write_segment, verify

# Backward-compatible single-file API (used by older callers)
from .compat import emit_xex, emit_segment, emit_header, emit_labels, export_xex_to_file

__all__ = [
    "write_all", "write_main", "write_equates", "write_segment", "verify",
    "emit_xex", "emit_segment", "emit_header", "emit_labels", "export_xex_to_file",
]
