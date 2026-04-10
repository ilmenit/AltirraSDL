"""Code emission — disassemble via bridge and format as MADS source.

This is the only module that calls ``bridge.disasm()``.
"""

from __future__ import annotations
from typing import Dict, List, Optional, TYPE_CHECKING

from . import data as _data

if TYPE_CHECKING:
    from ..client import AltirraBridge
    from ..project import Project


def strip_inline_address(text: str) -> str:
    """Clean Altirra DISASM output for MADS emission.

    Altirra returns lines like::

        LBL  jmp $EED8    [$EED8] = $78

    We want just ``jmp $EED8``. Strip the leading label column
    (we emit our own) and the trailing reference annotation.
    """
    import re
    s = text.strip()

    # Drop trailing reference annotation: " [$EED8] = $78" or " [$03]"
    s = re.sub(r'\s+\[\$[0-9A-Fa-f]+\](\s*=\s*\$[0-9A-Fa-f]+)?$', '', s)

    # Drop leading symbolic label column.  Altirra emits an uppercase
    # label >=2 chars followed by spaces, then the lowercase mnemonic.
    parts = s.split(None, 1)
    if len(parts) == 2 and parts[0].isupper() and len(parts[0]) >= 2:
        rest = parts[1].lstrip()
        if rest[:3].islower() and rest[:3].isalpha():
            s = rest

    # Replace ALL Altirra-resolved symbols in operands with raw $hex.
    # Altirra uses its own comprehensive symbol database which is
    # larger than what we define in equates.asm. Rather than trying to
    # match every possible symbol, we strip them all and re-resolve
    # via MADS equates (which we control).  This avoids "undeclared
    # label" errors for symbols like INTABS, LNORG, ICAX4Z that
    # Altirra knows but our equates don't define.
    #
    # Pattern: replace SYMBOL or SYMBOL+N or SYMBOL-N or (SYMBOL,X)
    # etc. with the raw hex address.  We get the address from the
    # bridge's sym_resolve, but since we're in a text-processing
    # context, we use a simpler approach: just replace known patterns.
    s = _deresolver.sub(s)

    return s


class _SymbolDeresolverHelper:
    """Replace Altirra-resolved symbol references with raw $hex addresses.

    Altirra's DISASM output contains resolved OS symbols like
    ``DOSINI``, ``FR1+3``, ``CASINI+1``. MADS needs either a
    matching equate or a raw hex address. We replace all resolved
    symbols with raw hex for reliability — our equates.asm then
    provides the names MADS actually uses.

    Cached per-session; the resolver is called once per unique symbol
    by the strip function.
    """

    def __init__(self):
        self._cache: dict = {}   # symbol_name → addr (int) or None
        self._bridge = None

    def set_bridge(self, bridge):
        """Set the bridge for symbol resolution."""
        self._bridge = bridge
        self._cache.clear()

    def _resolve(self, name: str) -> int | None:
        """Resolve a symbol name to an address via bridge."""
        if name in self._cache:
            return self._cache[name]
        if self._bridge is None:
            self._cache[name] = None
            return None
        try:
            result = self._bridge.sym_resolve(name)
            addr = result if isinstance(result, int) else int(result)
            self._cache[name] = addr
            return addr
        except Exception:
            self._cache[name] = None
            return None

    def sub(self, s: str) -> str:
        """Replace symbol operands with raw hex in disassembly text."""
        import re
        # Split into mnemonic and operand
        parts = s.split(None, 1)
        if len(parts) < 2:
            return s
        mnem, operand = parts[0], parts[1]

        # Replace SYMBOL+N or SYMBOL-N or standalone SYMBOL in operand
        # Keep: $hex, X, Y, A, S (register names), #$xx (immediates)
        def _repl(m):
            full = m.group(0)
            base_name = m.group(1)
            offset_str = m.group(2) or ""

            # Skip register names and known tokens
            if base_name in ("X", "Y", "A", "S"):
                return full

            addr = self._resolve(base_name)
            if addr is None:
                return full  # can't resolve — keep as-is

            # Apply offset
            if offset_str:
                op = offset_str[0]
                try:
                    off = int(offset_str[1:])
                except ValueError:
                    return full
                addr = (addr + off) if op == "+" else (addr - off)

            if addr < 0x100:
                return f"${addr:02X}"
            return f"${addr:04X}"

        # Match uppercase symbols NOT preceded by $ (to avoid
        # replacing hex digits inside $XXXX addresses).
        new_operand = re.sub(
            r'(?<!\$)(?<![0-9A-Fa-f])([A-Z_][A-Z_][A-Z0-9_]*)([+-]\d+)?',
            _repl,
            operand
        )
        # Fix double-dollar from "#$" prefix + "$XX" replacement
        new_operand = new_operand.replace("#$$", "#$")
        # Fix "($$" from indirect addressing
        new_operand = new_operand.replace("($$", "($")
        return f"{mnem} {new_operand}"


_deresolver = _SymbolDeresolverHelper()


def emit_code_range(
    bridge: "AltirraBridge",
    start: int,
    end: int,
    labels: Dict[int, str],
    comments: Dict[int, str],
) -> List[str]:
    """Disassemble [start, end] via bridge and return MADS source lines.

    Each instruction comes from ``bridge.disasm(pc, count=1)``.
    Labels and comments are inserted at their addresses.
    """
    _deresolver.set_bridge(bridge)
    lines: List[str] = []
    pc = start
    while pc <= end:
        # Label
        if pc in labels:
            lines.append(f"{labels[pc]}:")
        # Comment
        if pc in comments:
            lines.append(f"    ; {comments[pc]}")

        result = bridge.disasm(pc, count=1)
        if not result:
            lines.append(f"    .byte ${_peek(bridge, pc):02X}  ; disasm failed at ${pc:04X}")
            pc += 1
            continue

        ins = result[0]
        text = strip_inline_address(ins["text"])
        ins_len = ins.get("length", 1)

        # Check if MADS can handle this instruction. Altirra knows
        # illegal opcodes (nop zp, slo, dcp, isb, ...) that MADS
        # doesn't support. Also catch illegal NOP variants ($1A,
        # $3A, $5A, $7A, $DA, $FA — all disassemble as "nop" but
        # MADS would encode as $EA).
        opcode_byte = _peek(bridge, pc)
        if _is_illegal_opcode(text, opcode_byte):
            raw = [f"${_peek(bridge, pc + i):02X}" for i in range(ins_len)]
            lines.append(f"    .byte {','.join(raw):20s} ; ${pc:04X}: {text}")
        # Safety: check for mid-instruction labels
        elif any(labels.get(pc + i) for i in range(1, ins_len)):
            for i in range(ins_len):
                a = pc + i
                if i > 0 and a in labels:
                    lines.append(f"{labels[a]}:")
                if a in comments and i > 0:
                    lines.append(f"    ; {comments[a]}")
                lines.append(f"    .byte ${_peek(bridge, a):02X}  ; ${a:04X}")
        else:
            # Fix MADS addressing mode: if the original opcode uses
            # zero-page mode but the operand text has a symbol/address
            # that MADS might promote to absolute, force zero-page
            # by emitting raw bytes.
            opcode = _peek(bridge, pc)
            if ins_len == 2 and _is_zp_opcode(opcode) and not _operand_is_zp_safe(text):
                raw = [f"${_peek(bridge, pc + i):02X}" for i in range(ins_len)]
                lines.append(f"    .byte {','.join(raw):20s} ; {text}")
            else:
                lines.append(f"    {text}")

        next_pc = int(ins["next"].lstrip("$"), 16)
        if next_pc <= pc:
            break
        pc = next_pc

    return lines


def emit_mixed(
    bridge: "AltirraBridge",
    proj: "Project",
    mem,
    start: int,
    end: int,
    region_map: Dict[int, dict],
    emit_labels: Dict[int, str],
    all_labels: Dict[int, str],
) -> str:
    """Emit mixed code/data content respecting region boundaries.

    Code regions → ``bridge.disasm()`` (real mnemonics).
    Data regions → ``.byte``/``.word``/``.ds`` via ``data.py``.
    Unclassified → attempt disassembly (fallback to ``.byte``).
    """
    lines: List[str] = []
    addr = start

    while addr <= end:
        region = region_map.get(addr)

        # --- Code region ---
        if region and region["type"] == "code":
            block_end = min(_region_end(region), end)
            code_lines = emit_code_range(
                bridge, addr, block_end, emit_labels, proj.comments)
            lines.extend(code_lines)
            addr = block_end + 1

        # --- Data region ---
        elif region and region["type"] == "data":
            block_end = min(_region_end(region), end)
            # Emit label at start
            if addr in emit_labels:
                lines.append(f"{emit_labels[addr]}:")
            if addr in proj.comments:
                lines.append(f"    ; {proj.comments[addr]}")
            inner_labels = {a: n for a, n in emit_labels.items()
                           if a > addr and a <= block_end}
            text = _data.emit_data_range(
                mem, addr, block_end,
                region.get("hint", "bytes"),
                inner_labels, proj.comments,
                resolve_labels=all_labels)
            lines.append(text)
            addr = block_end + 1

        # --- Unclassified: try disasm ---
        else:
            # Find extent of unclassified gap
            gap_end = addr
            while (gap_end + 1 <= end
                   and region_map.get(gap_end + 1) is None):
                gap_end += 1

            code_lines = emit_code_range(
                bridge, addr, gap_end, emit_labels, proj.comments)
            lines.extend(code_lines)
            addr = gap_end + 1

    return "\n".join(lines)


def build_region_map(
    proj: "Project",
    seg_start: int,
    seg_end: int,
) -> Dict[int, dict]:
    """Build addr → region dict for a segment range."""
    rmap: Dict[int, dict] = {}
    for r in proj.regions:
        rs = _parse_addr(r["start"])
        re = _parse_addr(r["end"])
        if re < seg_start or rs > seg_end:
            continue
        for a in range(max(rs, seg_start), min(re, seg_end) + 1):
            rmap[a] = r
    return rmap


# --- helpers ---

def _region_end(region: dict) -> int:
    return _parse_addr(region["end"])


def _parse_addr(v) -> int:
    if isinstance(v, int):
        return v
    return int(str(v).lstrip("$"), 16)


def _peek(bridge, addr: int) -> int:
    return bridge.peek(addr, 1)[0]


# Opcodes that use zero-page addressing (2 bytes: opcode + ZP addr).
# MADS may promote these to absolute (3 bytes) if the operand is a
# label or expression it can't prove is < $100 at assembly time.
_ZP_OPCODES = frozenset({
    0x05, 0x06, 0x24, 0x25, 0x26, 0x45, 0x46, 0x65, 0x66,
    0x84, 0x85, 0x86, 0xA4, 0xA5, 0xA6, 0xC4, 0xC5, 0xC6,
    0xE4, 0xE5, 0xE6,  # CPX/SBC/INC zp
    # Zero-page indexed
    0x15, 0x16, 0x35, 0x36, 0x55, 0x56, 0x75, 0x76,
    0x94, 0x95, 0x96, 0xB4, 0xB5, 0xB6, 0xD5, 0xD6, 0xF5, 0xF6,
    # Zero-page indirect
    0x01, 0x11, 0x21, 0x31, 0x41, 0x51, 0x61, 0x71,
    0x81, 0x91, 0xA1, 0xB1, 0xC1, 0xD1, 0xE1, 0xF1,
    # BIT, STY, LDY zero-page
    0x04,  # actually illegal NOP zp — already handled
})


def _is_zp_opcode(opcode: int) -> bool:
    """True if this opcode byte is a zero-page addressing mode."""
    return opcode in _ZP_OPCODES


def _operand_is_zp_safe(text: str) -> bool:
    """True if the operand in the disassembly text is a literal
    $XX (zero-page hex) that MADS will assemble as zero-page.

    Returns False if the operand contains a label or expression
    that MADS might promote to absolute mode.
    """
    parts = text.strip().split(None, 1)
    if len(parts) < 2:
        return True  # implied — no operand
    operand = parts[1].strip()
    # Indirect forms: ($XX),Y or ($XX,X) — check inside parens
    if operand.startswith("("):
        inner = operand.strip("()")
        inner = inner.split(",")[0].strip()
        operand = inner
    # Strip indexing: $XX,X → $XX
    operand = operand.split(",")[0].strip()
    # If it's a literal $XX (1-2 hex digits), it's safe
    if operand.startswith("$") and len(operand) <= 3:
        return True
    # If it's a literal number without $, might still be safe
    # But any label name or expression → unsafe
    return False


# MADS 6502 legal mnemonics (all standard NMOS 6502 instructions).
# Anything not in this set is an illegal opcode that MADS won't assemble.
_LEGAL_MNEMONICS = frozenset({
    "adc", "and", "asl", "bcc", "bcs", "beq", "bit", "bmi", "bne",
    "bpl", "brk", "bvc", "bvs", "clc", "cld", "cli", "clv", "cmp",
    "cpx", "cpy", "dec", "dex", "dey", "eor", "inc", "inx", "iny",
    "jmp", "jsr", "lda", "ldx", "ldy", "lsr", "nop", "ora", "pha",
    "php", "pla", "plp", "rol", "ror", "rti", "rts", "sbc", "sec",
    "sed", "sei", "sta", "stx", "sty", "tax", "tay", "tsx", "txa",
    "txs", "tya",
})


def _is_illegal_opcode(text: str, opcode_byte: int = 0xEA) -> bool:
    """Return True if the instruction can't be assembled by MADS.

    Checks both mnemonic text and opcode byte to catch:
    - Unknown mnemonics (slo, dcp, isb, sha, etc.)
    - NOP with operands (nop $XX — illegal nop variants)
    - Implied-NOP with wrong byte ($1A,$3A,$5A,$7A,$DA,$FA)
    """
    parts = text.strip().split(None, 1)
    if not parts:
        return True
    mnem = parts[0].lower()
    if mnem not in _LEGAL_MNEMONICS:
        return True
    # NOP is implied-only in MADS — any operand makes it illegal
    if mnem == "nop" and len(parts) > 1:
        return True
    # Illegal NOP variants that disassemble as bare "nop" but are
    # not $EA — MADS would encode as $EA, changing the binary
    if mnem == "nop" and opcode_byte != 0xEA:
        return True
    # Branch instructions in data regions can have out-of-range
    # targets. Detect branches that target addresses far from PC
    # (relative branch can only reach ±127 bytes). We don't know
    # PC here, but if the target is > 256 bytes away from any
    # reasonable position, it's suspect. This is caught more
    # reliably by marking data regions properly.
    return False
