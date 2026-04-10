"""analyzer.subroutines — subroutines analysis functions."""

from collections import defaultdict

from altirra_bridge._analyzer_tables import (
    OPCODES as _OPCODES,
    BRANCHES as _BRANCHES,
    HW_READ,
    HW_WRITE,
    ADDR_TO_NAME,
)

from .hw import classify_hw_access

def analyze_subroutine(mem, addr):
    """Trace a subroutine to determine its characteristics.

    Follows control flow (branches, JSR skip-over, JMP) up to 200 instructions.
    Catalogs HW register accesses, shadow register writes, math operations,
    loop detection, call targets, and behavioral patterns (BCD arithmetic,
    block copy/fill, table lookups, wait loops, indirect dispatch, etc.).

    Args:
        mem: memory accessor
        addr: subroutine entry address (int)

    Returns:
        dict with keys:
        - instr_count, hw_reads, hw_writes, calls, has_loop,
          purposes (list), modifies_colors, modifies_pmg, modifies_sound,
          modifies_screen, does_math, shift_count, reads_joystick,
          reads_keyboard, stores_vector, modifies_dlist, zp_accesses
        - uses_bcd: SED/CLD pattern detected (score/counter arithmetic)
        - uses_indirect_indexed: STA ($zp),Y pattern (block write via pointer)
        - uses_indexed_abs: LDA table,X or ,Y (table lookup)
        - reads_random: reads POKEY RANDOM ($D20A)
        - polls_vcount: reads ANTIC VCOUNT ($D40B) in a loop
        - polls_rtclok: reads RTCLOK ($12-$14) in a loop
        - has_wait_loop: wait/poll loop detected
        - has_fill_loop: loop that stores constant via indexed or indirect
        - has_copy_loop: loop that loads+stores via indexed or indirect
        - has_search_loop: loop with CMP inside backward branch
        - has_cmp_chain: 3+ sequential CMP/branch (dispatch/range check)
        - uses_indirect_jmp: JMP ($zp) or JMP ($abs)
        - table_bases: set of absolute addresses used as table bases (addr,X or addr,Y)
        - abs_stores: set of absolute addresses written (non-HW, non-ZP)
        - abs_loads: set of absolute addresses read (non-HW, non-ZP)
        - loop_count: number of backward branches
        - backward_branches: list of (branch_addr, target_addr)
    """
    hw_reads = []
    hw_writes = []
    zp_accesses = []
    calls = []
    instr_count = 0
    has_loop = False
    modifies_colors = False
    modifies_pmg = False
    modifies_sound = False
    modifies_dlist = False
    does_math = False
    shift_count = 0
    stores_vector = False
    reads_joystick = False
    reads_keyboard = False
    modifies_screen = False

    # Enhanced pattern tracking
    uses_bcd = False
    uses_indirect_indexed = False
    uses_indexed_abs = False
    reads_random = False
    polls_vcount = False
    polls_rtclok = False
    has_wait_loop = False
    has_fill_loop = False
    has_copy_loop = False
    has_search_loop = False
    has_cmp_chain = False
    uses_indirect_jmp = False
    table_bases = set()
    abs_stores = set()
    abs_loads = set()
    loop_count = 0
    backward_branches = []

    # Track instruction sequence for pattern detection
    instr_seq = []  # list of (addr, mnem, mode, size, eff_addr)

    _STORE_MNEMS = {'STA', 'STX', 'STY'}
    _LOAD_MNEMS = {'LDA', 'LDX', 'LDY', 'CMP', 'CPX', 'CPY', 'BIT',
                   'ORA', 'AND', 'EOR', 'ADC', 'SBC'}

    visited = set()
    to_visit = [addr]
    while to_visit:
        cur = to_visit.pop(0)
        if cur in visited or cur < 0 or cur > 0xFFFF:
            continue
        if 0xD000 <= cur <= 0xD7FF:
            continue
        visited.add(cur)

        opcode = mem[cur]
        if opcode not in _OPCODES:
            continue
        mnem, mode, size = _OPCODES[opcode]
        instr_count += 1
        if instr_count > 200:
            break

        # Get effective address
        eff_addr = None
        if size == 2 and mode in ('zp', 'zpx', 'zpy'):
            eff_addr = mem[(cur + 1) & 0xFFFF]
            zp_accesses.append(eff_addr)
        elif size == 3 and mode in ('abs', 'abx', 'aby'):
            eff_addr = mem[(cur + 1) & 0xFFFF] | (mem[(cur + 2) & 0xFFFF] << 8)

        # Record instruction for sequence analysis
        instr_seq.append((cur, mnem, mode, size, eff_addr))

        # Detect BCD mode (SED instruction)
        if mnem == 'SED':
            uses_bcd = True

        # Detect indirect indexed writes: STA ($zp),Y
        if mnem == 'STA' and mode == 'iny':
            uses_indirect_indexed = True

        # Detect indirect indexed reads: LDA ($zp),Y
        if mnem in _LOAD_MNEMS and mode == 'iny':
            uses_indirect_indexed = True

        # Detect indexed absolute access (table lookups)
        if mode in ('abx', 'aby') and eff_addr is not None:
            uses_indexed_abs = True
            table_bases.add(eff_addr)

        # Detect indirect JMP
        if mnem == 'JMP' and mode == 'ind':
            uses_indirect_jmp = True

        if eff_addr is not None:
            is_store = mnem in _STORE_MNEMS
            is_load = mnem in _LOAD_MNEMS

            # GTIA ($D000-$D01F)
            if 0xD000 <= eff_addr <= 0xD01F:
                if is_store:
                    hw_writes.append(eff_addr)
                    if 0xD012 <= eff_addr <= 0xD01A:
                        modifies_colors = True
                    if eff_addr in (0xD000, 0xD001, 0xD002, 0xD003,
                                    0xD004, 0xD005, 0xD006, 0xD007,
                                    0xD008, 0xD009, 0xD00A, 0xD00B,
                                    0xD00D, 0xD00E, 0xD00F, 0xD010,
                                    0xD011, 0xD01D):
                        modifies_pmg = True
                else:
                    hw_reads.append(eff_addr)

            # POKEY ($D200-$D20F)
            elif 0xD200 <= eff_addr <= 0xD20F:
                if is_store:
                    hw_writes.append(eff_addr)
                    if eff_addr in (0xD200, 0xD201, 0xD202, 0xD203,
                                    0xD204, 0xD205, 0xD206, 0xD207,
                                    0xD208):
                        modifies_sound = True
                else:
                    hw_reads.append(eff_addr)
                    if eff_addr == 0xD209:
                        reads_keyboard = True
                    if eff_addr == 0xD20A:
                        reads_random = True

            # PIA ($D300-$D303)
            elif 0xD300 <= eff_addr <= 0xD303:
                if is_load:
                    hw_reads.append(eff_addr)
                    if eff_addr == 0xD300:
                        reads_joystick = True
                else:
                    hw_writes.append(eff_addr)

            # ANTIC ($D400-$D40F)
            elif 0xD400 <= eff_addr <= 0xD40F:
                if is_store:
                    hw_writes.append(eff_addr)
                    if eff_addr in (0xD402, 0xD403):
                        modifies_dlist = True
                if is_load:
                    hw_reads.append(eff_addr)
                    if eff_addr == 0xD40B:
                        polls_vcount = True

            # Shadow registers
            if 0x02C0 <= eff_addr <= 0x02C8 and is_store:
                modifies_colors = True
            if eff_addr in (0x0230, 0x0231) and is_store:
                modifies_dlist = True
            if eff_addr in (0x0200, 0x0201, 0x0222, 0x0223,
                            0x0224, 0x0225) and is_store:
                stores_vector = True

            # RTCLOK ($12-$14)
            if eff_addr in (0x12, 0x13, 0x14) and is_load:
                polls_rtclok = True

            # Track non-HW, non-ZP absolute accesses
            if eff_addr >= 0x100 and not (0xD000 <= eff_addr <= 0xD7FF):
                if is_store:
                    abs_stores.add(eff_addr)
                if is_load:
                    abs_loads.add(eff_addr)

            # Screen memory (heuristic: $1000-$4000 common screen area)
            if 0x1000 <= eff_addr <= 0x3FFF and is_store:
                modifies_screen = True

        # Track math ops
        if mnem in ('ASL', 'LSR', 'ROL', 'ROR'):
            shift_count += 1
        if mnem in ('ADC', 'SBC'):
            does_math = True
        if mnem == 'JSR' and size == 3:
            target = mem[(cur + 1) & 0xFFFF] | (mem[(cur + 2) & 0xFFFF] << 8)
            calls.append(target)

        # Follow flow
        if mnem in ('RTS', 'RTI', 'BRK'):
            continue
        if mnem == 'JMP':
            if mode == 'abs':
                target = mem[(cur + 1) & 0xFFFF] | (mem[(cur + 2) & 0xFFFF] << 8)
                to_visit.append(target)
            continue
        if mnem == 'JSR':
            to_visit.append(cur + size)
            continue
        if mnem in _BRANCHES:
            offset = mem[(cur + 1) & 0xFFFF]
            if offset >= 128:
                offset -= 256
            target = (cur + 2 + offset) & 0xFFFF
            if target < cur:
                has_loop = True
                loop_count += 1
                backward_branches.append((cur, target))
            to_visit.append(target)
            to_visit.append(cur + size)
            continue
        to_visit.append(cur + size)

    # ── Post-trace sequence analysis ──
    # Sort instructions by address for linear scan
    instr_seq.sort(key=lambda x: x[0])

    # Detect CMP chains: 3+ sequential CMP/CPX/CPY followed by branch
    cmp_run = 0
    for _, m, _, _, _ in instr_seq:
        if m in ('CMP', 'CPX', 'CPY'):
            cmp_run += 1
        elif m in _BRANCHES:
            if cmp_run >= 1:
                cmp_run = 0  # reset after branch, keep counting
        else:
            if cmp_run >= 3:
                has_cmp_chain = True
            cmp_run = 0

    if cmp_run >= 3:
        has_cmp_chain = True

    # Detect fill/copy/search loops by analyzing backward branch contexts
    for br_addr, br_target in backward_branches:
        # Collect instructions in the loop body
        loop_body = [(a, m, mo, s, e) for a, m, mo, s, e in instr_seq
                     if br_target <= a <= br_addr]
        loop_mnems = [m for _, m, _, _, _ in loop_body]
        loop_modes = [mo for _, _, mo, _, _ in loop_body]

        has_store_in_loop = any(m in _STORE_MNEMS for m in loop_mnems)
        has_load_in_loop = any(m in _LOAD_MNEMS for m in loop_mnems)
        has_indirect_store = any(m in _STORE_MNEMS and mo == 'iny'
                                 for _, m, mo, _, _ in loop_body)
        has_indexed_store = any(m in _STORE_MNEMS and mo in ('abx', 'aby')
                                for _, m, mo, _, _ in loop_body)
        has_cmp = any(m in ('CMP', 'CPX', 'CPY') for m in loop_mnems)

        if has_store_in_loop and (has_indirect_store or has_indexed_store):
            if has_load_in_loop and any(mo in ('iny', 'abx', 'aby')
                                        for _, m, mo, _, _ in loop_body
                                        if m in _LOAD_MNEMS):
                has_copy_loop = True
            else:
                has_fill_loop = True

        if has_cmp and has_load_in_loop and not has_store_in_loop:
            has_search_loop = True

    # Detect wait/poll loops
    if has_loop and (polls_vcount or polls_rtclok):
        has_wait_loop = True

    # Build purpose tags
    purposes = []
    if modifies_sound:
        purposes.append('sound')
    if modifies_colors:
        purposes.append('color')
    if modifies_pmg:
        purposes.append('pmg')
    if modifies_dlist:
        purposes.append('display_list')
    if stores_vector:
        purposes.append('vector_setup')
    if reads_joystick:
        purposes.append('joystick')
    if reads_keyboard:
        purposes.append('keyboard')
    if modifies_screen:
        purposes.append('screen')
    if shift_count >= 3:
        purposes.append('math')
    if uses_bcd:
        purposes.append('bcd')
    if reads_random:
        purposes.append('random')
    if has_wait_loop:
        purposes.append('wait')
    if has_fill_loop:
        purposes.append('fill')
    if has_copy_loop:
        purposes.append('copy')
    if has_search_loop:
        purposes.append('search')
    if uses_indirect_jmp:
        purposes.append('dispatch')
    if has_cmp_chain:
        purposes.append('dispatch')

    return {
        'instr_count': instr_count,
        'hw_reads': hw_reads,
        'hw_writes': hw_writes,
        'zp_accesses': zp_accesses,
        'calls': calls,
        'has_loop': has_loop,
        'purposes': purposes,
        'modifies_colors': modifies_colors,
        'modifies_pmg': modifies_pmg,
        'modifies_sound': modifies_sound,
        'modifies_screen': modifies_screen,
        'modifies_dlist': modifies_dlist,
        'does_math': does_math,
        'shift_count': shift_count,
        'stores_vector': stores_vector,
        'reads_joystick': reads_joystick,
        'reads_keyboard': reads_keyboard,
        # Enhanced fields
        'uses_bcd': uses_bcd,
        'uses_indirect_indexed': uses_indirect_indexed,
        'uses_indexed_abs': uses_indexed_abs,
        'reads_random': reads_random,
        'polls_vcount': polls_vcount,
        'polls_rtclok': polls_rtclok,
        'has_wait_loop': has_wait_loop,
        'has_fill_loop': has_fill_loop,
        'has_copy_loop': has_copy_loop,
        'has_search_loop': has_search_loop,
        'has_cmp_chain': has_cmp_chain,
        'uses_indirect_jmp': uses_indirect_jmp,
        'table_bases': table_bases,
        'abs_stores': abs_stores,
        'abs_loads': abs_loads,
        'loop_count': loop_count,
        'backward_branches': backward_branches,
    }



def suggest_name(addr, analysis, existing_names=None):
    """Generate a meaningful subroutine name from analysis results.

    Uses behavioral patterns (BCD, copy/fill, table lookup, wait, dispatch)
    in addition to HW access to produce descriptive names.

    Args:
        addr: subroutine address (int)
        analysis: dict from analyze_subroutine()
        existing_names: set of already-used names for uniqueness

    Returns:
        Suggested name string, or None if no meaningful name can be derived.
    """
    if existing_names is None:
        existing_names = set()

    ic = analysis['instr_count']
    base = None

    # Priority 1: distinctive behavioral patterns
    if analysis.get('uses_bcd') and analysis['does_math']:
        base = 'bcd_add' if ic < 15 else 'update_score'
    elif analysis.get('has_wait_loop'):
        if analysis.get('polls_vcount'):
            base = 'wait_vcount'
        elif analysis.get('polls_rtclok'):
            base = 'wait_frames'
        else:
            base = 'wait_sync'
    elif analysis.get('uses_indirect_jmp'):
        base = 'dispatch_indirect'
    elif analysis.get('has_cmp_chain') and not analysis.get('has_loop'):
        base = 'classify' if ic < 20 else 'dispatch'
    elif analysis.get('has_copy_loop') and analysis.get('uses_indirect_indexed'):
        base = 'copy_block'
    elif analysis.get('has_fill_loop') and analysis.get('uses_indirect_indexed'):
        base = 'fill_block'
    elif analysis.get('has_fill_loop'):
        base = 'fill_indexed'
    elif analysis.get('has_copy_loop'):
        base = 'copy_indexed'
    elif analysis.get('has_search_loop'):
        base = 'search'
    elif analysis.get('reads_random') and not analysis['modifies_sound']:
        base = 'random_perturb' if analysis['does_math'] else 'get_random'

    # Priority 2: HW access patterns
    if base is None:
        if analysis['modifies_sound'] and not analysis['modifies_colors']:
            base = 'set_sound' if ic < 20 else 'update_sound'
        elif analysis['modifies_colors'] and not analysis['modifies_sound']:
            base = 'update_pmg_colors' if analysis['modifies_pmg'] else 'set_colors'
        elif analysis['modifies_pmg'] and not analysis['modifies_colors']:
            base = 'update_pmg'
        elif analysis['modifies_screen']:
            base = 'render' if analysis['does_math'] and analysis['shift_count'] >= 2 else 'draw'
        elif analysis['reads_joystick']:
            base = 'read_joystick'
        elif analysis['reads_keyboard']:
            base = 'read_keyboard'
        elif analysis['stores_vector']:
            base = 'setup_vectors'
        elif analysis['modifies_dlist']:
            base = 'setup_display'

    # Priority 3: structural patterns
    if base is None:
        if analysis['does_math'] and analysis['shift_count'] >= 5:
            base = 'bit_extract'
        elif analysis['does_math'] and analysis['shift_count'] >= 3:
            base = 'calc'
        elif analysis.get('uses_indexed_abs') and ic <= 12:
            base = 'table_lookup'
        elif analysis['has_loop'] and analysis.get('uses_indexed_abs'):
            base = 'array_op'
        elif analysis['has_loop'] and ic <= 20 and not analysis['calls']:
            base = 'loop_util'
        elif ic <= 5:
            base = 'stub'
        elif ic <= 15 and analysis['does_math']:
            base = 'arith'
        elif analysis['has_loop'] and ic > 30:
            base = 'process'
        elif ic <= 10 and not analysis['calls']:
            base = 'helper'
        else:
            return None

    name = f'{base}_{addr:04X}'
    counter = 2
    while name in existing_names:
        name = f'{base}_{addr:04X}_{counter}'
        counter += 1
    return name



def suggest_comment(addr, analysis, labels=None):
    """Generate a behavioral description from subroutine analysis.

    Describes what the subroutine does (copy, fill, poll, dispatch, score update)
    rather than just cataloging HW access.

    Args:
        addr: subroutine address (int)
        analysis: dict from analyze_subroutine()
        labels: optional {addr_hex: label_info} dict for naming call targets

    Returns:
        Comment string, or None if nothing meaningful to say.
    """
    parts = []

    # Lead with the most descriptive behavioral pattern
    if analysis.get('uses_bcd') and analysis['does_math']:
        parts.append("BCD arithmetic — score or counter update")
    elif analysis.get('has_wait_loop'):
        if analysis.get('polls_vcount'):
            parts.append("Wait loop polling VCOUNT (scanline sync)")
        elif analysis.get('polls_rtclok'):
            parts.append("Wait loop polling RTCLOK (frame sync)")
        else:
            parts.append("Wait/poll loop")
    elif analysis.get('uses_indirect_jmp'):
        parts.append("Indirect dispatch via pointer")
    elif analysis.get('has_cmp_chain'):
        parts.append("Comparison chain — range check or dispatch")
    elif analysis.get('has_copy_loop') and analysis.get('uses_indirect_indexed'):
        parts.append("Block copy via indirect indexed addressing")
    elif analysis.get('has_fill_loop') and analysis.get('uses_indirect_indexed'):
        parts.append("Block fill via indirect indexed addressing")
    elif analysis.get('has_fill_loop'):
        parts.append("Fill loop — indexed store of constant value")
    elif analysis.get('has_copy_loop'):
        parts.append("Copy loop — indexed load/store transfer")
    elif analysis.get('has_search_loop'):
        parts.append("Search loop — scans array for match")
    elif analysis.get('reads_random') and not analysis['modifies_sound']:
        parts.append("Reads POKEY RANDOM for randomization")

    # HW access description
    if analysis['modifies_sound']:
        parts.append("Updates sound registers")
    if analysis['modifies_colors']:
        parts.append("Sets color registers")
    if analysis['modifies_pmg']:
        parts.append("Configures player-missile graphics")
    if analysis['modifies_screen']:
        parts.append("Writes to screen memory")
    if analysis['reads_joystick']:
        parts.append("Reads joystick input")
    if analysis['reads_keyboard']:
        parts.append("Processes keyboard input")
    if analysis['stores_vector']:
        parts.append("Installs interrupt/event vectors")
    if analysis['modifies_dlist']:
        parts.append("Modifies display list")

    # Math description
    if analysis['does_math'] and analysis['shift_count'] >= 5:
        parts.append("Bit extraction/packing via shifts and rotates")
    elif analysis['does_math'] and analysis['shift_count'] >= 3:
        parts.append("Arithmetic with shifts")

    # HW registers accessed
    hw_r = sorted(set(analysis['hw_reads']))
    hw_w = sorted(set(analysis['hw_writes']))
    if hw_r:
        names = [ADDR_TO_NAME.get(a, f'${a:04X}') for a in hw_r[:4]]
        parts.append(f"Reads: {', '.join(names)}")
    if hw_w:
        names = [ADDR_TO_NAME.get(a, f'${a:04X}') for a in hw_w[:4]]
        parts.append(f"Writes: {', '.join(names)}")

    # Call targets
    if analysis['calls'] and labels:
        call_names = []
        for c in analysis['calls'][:4]:
            ckey = f'{c:04X}'
            if ckey in labels:
                info = labels[ckey]
                call_names.append(info['name'] if isinstance(info, dict) else info)
            else:
                call_names.append(f'${c:04X}')
        parts.append(f"Calls: {', '.join(call_names)}")

    # Table bases accessed
    tbases = analysis.get('table_bases', set())
    if tbases and len(tbases) <= 4:
        parts.append(f"Table access: {', '.join(f'${a:04X}' for a in sorted(tbases))}")

    ic = analysis['instr_count']
    loop_n = analysis.get('loop_count', 0)
    size_desc = f"{ic} instructions"
    if loop_n > 1:
        size_desc += f", {loop_n} loops"
    elif analysis['has_loop']:
        size_desc += ", 1 loop"
    parts.append(size_desc)

    return "; ".join(parts) if parts else None


# ── Call graph analysis ──



