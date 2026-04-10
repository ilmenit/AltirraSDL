"""Generic 6502 / Atari 8-bit reverse-engineering toolkit.

Submodules
----------
hw           Hardware classification, PORTB decode, auto-labelling.
disasm       Recursive-descent disassembler.
patterns     Gap classification, address-table scanning, pattern detection.
sampling     Runtime helpers (PC sampling, DLI chain, PORTB monitor,
             memory diff, profile-guided classification). These need an
             ``emu`` object — use ``altirra_bridge.analyzer.adapter.BridgeEmu``
             to wrap an ``AltirraBridge`` client.
variables    Variable cross-referencing and reporting.
subroutines  Single-subroutine deep analysis, name/comment suggestions.
procedures   Procedure body tracing, call-graph building, subsystem
             detection, call-graph reporting.
adapter      ``BridgeEmu`` — adapts ``AltirraBridge`` to the ``emu``
             interface the sampling helpers expect.

Quick start
-----------
::

    from altirra_bridge.analyzer import recursive_descent, classify_gap
    ram = bytearray(open('snapshot.bin', 'rb').read())
    regions, xrefs = recursive_descent(ram, [(0xA000, 'entry')])

All public names are re-exported here so existing code that does
``from altirra_bridge.analyzer import X`` keeps working.
"""

# -- hw --
from .hw import (
    classify_hw_access,
    decode_portb,
    build_hw_tables,
    auto_label,
)

# -- disasm --
from .disasm import recursive_descent

# -- patterns --
from .patterns import (
    detect_patterns,
    classify_gap,
    scan_address_tables,
    scan_indirect_jumps,
    scan_vector_pointers,
    detect_split_tables,
)

# -- sampling (emu-dependent) --
from .sampling import (
    profile_guided_classify,
    sample_pc,
    sample_dli_chain,
    sample_portb,
    diff_memory,
)

# -- variables --
from .variables import (
    analyze_variables,
    detect_parallel_arrays,
    variable_report,
)

# -- subroutines --
from .subroutines import (
    analyze_subroutine,
    suggest_name,
    suggest_comment,
)

# -- procedures / call graph --
from .procedures import (
    build_procedures,
    call_graph_context,
    detect_subsystems,
    suggest_name_from_graph,
    call_graph_report,
)

# -- adapter --
from .adapter import BridgeEmu

__all__ = [
    # hw
    "classify_hw_access", "decode_portb", "build_hw_tables", "auto_label",
    # disasm
    "recursive_descent",
    # patterns
    "detect_patterns", "classify_gap", "scan_address_tables",
    "scan_indirect_jumps", "scan_vector_pointers", "detect_split_tables",
    # sampling
    "profile_guided_classify", "sample_pc", "sample_dli_chain",
    "sample_portb", "diff_memory",
    # variables
    "analyze_variables", "detect_parallel_arrays", "variable_report",
    # subroutines
    "analyze_subroutine", "suggest_name", "suggest_comment",
    # procedures
    "build_procedures", "call_graph_context", "detect_subsystems",
    "suggest_name_from_graph", "call_graph_report",
    # adapter
    "BridgeEmu",
]
