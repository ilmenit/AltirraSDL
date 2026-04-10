"""Data emission — format raw bytes as MADS .byte/.word/.ds directives.

No bridge dependency. Works entirely on in-memory byte arrays.
"""

from __future__ import annotations
from typing import Dict, List, Optional


def emit_data_range(
    mem,
    start: int,
    end: int,
    hint: str = "bytes",
    labels: Optional[Dict[int, str]] = None,
    comments: Optional[Dict[int, str]] = None,
    *,
    resolve_labels: Optional[Dict[int, str]] = None,
) -> str:
    """Emit MADS data directives for [start, end].

    ``mem`` is indexable by address (``mem[addr]`` → int 0-255).
    ``hint`` controls formatting: ``'bytes'``, ``'words'``,
    ``'addr_table'``, ``'fill_zero'``, ``'string'``,
    ``'display_list'``, ``'charset'``, ``'sprite'``.
    """
    labels = labels or {}
    comments = comments or {}
    resolve = resolve_labels or labels

    if hint == "fill_zero":
        n = end - start + 1
        return f"    :${n:X} .byte $00"
    if hint == "words" or hint == "addr_table":
        return _emit_words(mem, start, end, labels, comments, resolve)
    if hint == "string":
        return _emit_string(mem, start, end, labels, comments)
    # Default: optimized byte emission
    lines = emit_optimized_bytes(mem, start, end, labels, comments,
                                 skip_first_label=True)
    return "\n".join(lines)


def emit_optimized_bytes(
    mem,
    start: int,
    end: int,
    labels: Optional[Dict[int, str]] = None,
    comments: Optional[Dict[int, str]] = None,
    *,
    bytes_per_line: int = 16,
    skip_first_label: bool = False,
) -> List[str]:
    """Emit .byte directives with run-length detection.

    Runs of >=4 identical bytes are compressed to ``:N .byte $XX``.
    Labels and comments interrupt runs.
    """
    labels = labels or {}
    comments = comments or {}
    lines: List[str] = []
    addr = start

    while addr <= end:
        # Emit label (except first if caller already emitted it)
        if addr in labels and not (addr == start and skip_first_label):
            lines.append(f"{labels[addr]}:")
        # Emit comment
        if addr in comments:
            lines.append(f"    ; {comments[addr]}")

        # Detect run of identical bytes
        run_byte = mem[addr & 0xFFFF]
        run_end = addr
        while (run_end + 1 <= end
               and mem[(run_end + 1) & 0xFFFF] == run_byte
               and (run_end + 1) not in labels
               and (run_end + 1) not in comments):
            run_end += 1
        run_len = run_end - addr + 1

        if run_len >= 4:
            lines.append(f"    :${run_len:X} .byte ${run_byte:02X}")
            addr = run_end + 1
            continue

        # Emit a line of mixed bytes (up to bytes_per_line)
        chunk_end = min(addr + bytes_per_line - 1, end)
        # Stop early at label/comment boundaries
        for a in range(addr + 1, chunk_end + 1):
            if a in labels or a in comments:
                chunk_end = a - 1
                break
        vals = []
        for a in range(addr, chunk_end + 1):
            vals.append(f"${mem[a & 0xFFFF]:02X}")
        lines.append(f"    .byte {','.join(vals)}")
        addr = chunk_end + 1

    return lines


def _emit_words(mem, start, end, labels, comments, resolve):
    """Emit .word directives for address tables."""
    lines = []
    addr = start
    while addr + 1 <= end:
        if addr in labels:
            lines.append(f"{labels[addr]}:")
        if addr in comments:
            lines.append(f"    ; {comments[addr]}")
        lo = mem[addr & 0xFFFF]
        hi = mem[(addr + 1) & 0xFFFF]
        val = lo | (hi << 8)
        name = resolve.get(val)
        if name:
            lines.append(f"    .word {name}")
        else:
            lines.append(f"    .word ${val:04X}")
        addr += 2
    # Odd trailing byte
    if addr <= end:
        lines.append(f"    .byte ${mem[addr & 0xFFFF]:02X}")
    return "\n".join(lines)


def _emit_string(mem, start, end, labels, comments):
    """Emit bytes with ASCII annotation in comments."""
    lines = []
    addr = start
    while addr <= end:
        if addr in labels:
            lines.append(f"{labels[addr]}:")
        chunk_end = min(addr + 15, end)
        vals = []
        chars = []
        for a in range(addr, chunk_end + 1):
            b = mem[a & 0xFFFF]
            vals.append(f"${b:02X}")
            c = b & 0x7F
            chars.append(chr(c) if 0x20 <= c < 0x7F else ".")
        ascii_str = "".join(chars)
        lines.append(f"    .byte {','.join(vals):48s} ; \"{ascii_str}\"")
        addr = chunk_end + 1
    return "\n".join(lines)
