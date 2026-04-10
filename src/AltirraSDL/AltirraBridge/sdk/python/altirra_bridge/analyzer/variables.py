"""analyzer.variables — variables analysis functions."""

from collections import defaultdict

from altirra_bridge._analyzer_tables import (
    OPCODES as _OPCODES,
    BRANCHES as _BRANCHES,
    HW_READ,
    HW_WRITE,
    ADDR_TO_NAME,
)

def analyze_variables(mem, procedures, xrefs, regions):
    """Analyze how memory addresses are used across all procedures.

    Traces instruction context for every memory access to classify variables
    by their role: counter, flag, pointer, accumulator, array base, BCD value,
    signed value, etc. Also detects parallel array groups and producer/consumer
    relationships.

    This is fully generic — works for any 6502 program.

    Args:
        mem: memory accessor
        procedures: dict from build_procedures() (int addr -> proc info)
        xrefs: list of xref dicts
        regions: list of region dicts

    Returns:
        dict of addr (int) -> variable info:
        {
            'readers': set of procedure entry addrs that read this var,
            'writers': set of procedure entry addrs that write this var,
            'modes': set of addressing modes used,
            'ops': dict of mnemonic -> count,
            'imm_values': set of immediate values stored here,
            'in_bcd': bool — accessed in SED/CLD context,
            'type': str — classified type (see below),
            'type_confidence': float,
            'scope': str — 'local' (1 proc), 'shared' (2-5), 'global' (6+),
        }

    Variable types:
        'counter' — INC/DEC dominant, no shifts
        'flag' — only written with #$00/#$01/#$FF
        'bcd_value' — accessed in SED/CLD context with ADC/SBC
        'accumulator' — subject to ADC/SBC/shifts (arithmetic workspace)
        'array_base' — accessed with indexed addressing from multiple sites
        'pointer' — ZP pair used in indirect indexed mode
        'shift_reg' — heavy ASL/LSR/ROL/ROR (bit manipulation target)
        'state_var' — read by many, written by few (game state)
        'work_var' — written by many, read by few (scratch workspace)
        'unknown' — insufficient evidence
    """
    # Build set of classified code addresses for body tracing
    code_addrs = set()
    for r in regions:
        if r.get('type') == 'code':
            for a in range(int(r['start'], 16), int(r['end'], 16) + 1):
                code_addrs.add(a)

    _STORE = {'STA', 'STX', 'STY'}
    _LOAD = {'LDA', 'LDX', 'LDY', 'CMP', 'CPX', 'CPY', 'BIT',
             'ORA', 'AND', 'EOR', 'ADC', 'SBC'}
    _RMW = {'INC', 'DEC', 'ASL', 'LSR', 'ROL', 'ROR'}

    variables = defaultdict(lambda: {
        'readers': set(), 'writers': set(), 'modes': set(),
        'ops': defaultdict(int), 'imm_values': set(),
        'in_bcd': False, 'type': 'unknown', 'type_confidence': 0.0,
        'scope': 'unknown',
    })

    # Trace each procedure's body
    for entry in procedures:
        visited = set()
        queue = [entry]
        in_bcd = False
        prev_imm = None

        while queue:
            cur = queue.pop(0)
            if cur in visited or cur < 0 or cur > 0xFFFF:
                continue
            if 0xD000 <= cur <= 0xD7FF:
                continue
            if code_addrs and cur not in code_addrs:
                continue
            visited.add(cur)

            opcode = mem[cur]
            if opcode not in _OPCODES:
                continue
            mnem, mode, size = _OPCODES[opcode]

            if mnem == 'SED':
                in_bcd = True
            if mnem == 'CLD':
                in_bcd = False

            # Track immediate value for STA detection
            if mode == 'imm' and size == 2:
                prev_imm = mem[(cur + 1) & 0xFFFF]

            eff_addr = None
            if size == 2 and mode in ('zp', 'zpx', 'zpy'):
                eff_addr = mem[(cur + 1) & 0xFFFF]
            elif size == 3 and mode in ('abs', 'abx', 'aby'):
                eff_addr = mem[(cur + 1) & 0xFFFF] | (mem[(cur + 2) & 0xFFFF] << 8)
            elif size == 2 and mode in ('inx', 'iny'):
                eff_addr = mem[(cur + 1) & 0xFFFF]

            if eff_addr is not None and not (0xD000 <= eff_addr <= 0xD7FF):
                v = variables[eff_addr]
                v['ops'][mnem.lower()] += 1
                v['modes'].add(mode)

                if mnem in _STORE:
                    v['writers'].add(entry)
                    if prev_imm is not None:
                        v['imm_values'].add(prev_imm)
                elif mnem in _LOAD:
                    v['readers'].add(entry)
                elif mnem in _RMW:
                    v['readers'].add(entry)
                    v['writers'].add(entry)

                if mode in ('inx', 'iny'):
                    v['modes'].add('indirect')
                if mode in ('abx', 'aby', 'zpx', 'zpy'):
                    v['modes'].add('indexed')

                if in_bcd:
                    v['in_bcd'] = True

            if mode != 'imm':
                prev_imm = None

            # Follow control flow (same as analyze_subroutine)
            if mnem in ('RTS', 'RTI', 'BRK'):
                continue
            if mnem == 'JMP':
                if mode == 'abs':
                    t = mem[(cur + 1) & 0xFFFF] | (mem[(cur + 2) & 0xFFFF] << 8)
                    queue.append(t)
                continue
            if mnem == 'JSR':
                queue.append(cur + size)
                continue
            if mnem in _BRANCHES:
                off = mem[(cur + 1) & 0xFFFF]
                if off >= 128:
                    off -= 256
                queue.append((cur + 2 + off) & 0xFFFF)
                queue.append(cur + size)
                continue
            queue.append(cur + size)

    # Classify each variable
    for addr, v in variables.items():
        ops = v['ops']
        n_readers = len(v['readers'])
        n_writers = len(v['writers'])
        total_procs = len(v['readers'] | v['writers'])

        # Scope
        if total_procs <= 1:
            v['scope'] = 'local'
        elif total_procs <= 5:
            v['scope'] = 'shared'
        else:
            v['scope'] = 'global'

        # BCD value
        if v['in_bcd'] and (ops.get('adc', 0) + ops.get('sbc', 0)) > 0:
            v['type'] = 'bcd_value'
            v['type_confidence'] = 0.9
            continue

        # Pointer (indirect indexed access)
        if 'indirect' in v['modes'] and addr < 0x100:
            v['type'] = 'pointer'
            v['type_confidence'] = 0.9
            continue

        # Flag/boolean (only written with small set of flag values)
        if v['imm_values'] and v['imm_values'].issubset({0x00, 0x01, 0xFF}):
            if ops.get('sta', 0) >= 2 and ops.get('asl', 0) == 0:
                v['type'] = 'flag'
                v['type_confidence'] = 0.8
                continue

        # Counter (INC/DEC dominant, no bit manipulation)
        inc_dec = ops.get('inc', 0) + ops.get('dec', 0)
        shifts = ops.get('asl', 0) + ops.get('lsr', 0) + ops.get('rol', 0) + ops.get('ror', 0)
        if inc_dec >= 2 and shifts == 0 and ops.get('adc', 0) <= 1:
            v['type'] = 'counter'
            v['type_confidence'] = 0.7
            continue

        # Shift register (heavy bit manipulation)
        if shifts >= 3:
            v['type'] = 'shift_reg'
            v['type_confidence'] = 0.7
            continue

        # Array base (indexed access from multiple procedures)
        if 'indexed' in v['modes'] and total_procs >= 3 and addr >= 0x100:
            v['type'] = 'array_base'
            v['type_confidence'] = 0.8
            continue

        # Accumulator (arithmetic operations)
        arith = ops.get('adc', 0) + ops.get('sbc', 0) + shifts
        if arith >= 2:
            v['type'] = 'accumulator'
            v['type_confidence'] = 0.6
            continue

        # State vs work variable (by reader/writer ratio)
        if n_readers >= 4 and n_writers <= 2:
            v['type'] = 'state_var'
            v['type_confidence'] = 0.5
            continue
        if n_writers >= 4 and n_readers <= 2:
            v['type'] = 'work_var'
            v['type_confidence'] = 0.5
            continue

    return dict(variables)



def detect_parallel_arrays(variables, min_entries=3):
    """Detect groups of parallel arrays — multiple array bases at regular offsets.

    Common 6502 pattern: entity attributes stored as separate byte arrays at
    consecutive base addresses, all indexed by the same register.

    Args:
        variables: dict from analyze_variables()
        min_entries: minimum number of arrays in a group (default 3)

    Returns:
        list of {'base': int, 'stride': int, 'count': int, 'addrs': [int]}
        where addrs are the array base addresses in the group.
    """
    # Collect all array bases, sorted
    array_bases = sorted(addr for addr, v in variables.items()
                         if v['type'] == 'array_base' and addr >= 0x100)

    if len(array_bases) < min_entries:
        return []

    # Look for runs of evenly-spaced array bases
    groups = []
    i = 0
    while i < len(array_bases):
        # Try different strides
        for stride in (1, 2, 4, 8, 14, 16, 32, 46, 48, 64, 128, 256):
            run = [array_bases[i]]
            expected = array_bases[i] + stride
            j = i + 1
            while j < len(array_bases):
                if array_bases[j] == expected:
                    run.append(array_bases[j])
                    expected += stride
                    j += 1
                elif array_bases[j] > expected:
                    break
                else:
                    j += 1
            if len(run) >= min_entries:
                groups.append({
                    'base': run[0],
                    'stride': stride,
                    'count': len(run),
                    'addrs': run,
                })
                break  # use first matching stride
        i += 1

    # Deduplicate (remove groups that are subsets of larger groups)
    groups.sort(key=lambda g: g['count'], reverse=True)
    result = []
    used = set()
    for g in groups:
        addrs_set = set(g['addrs'])
        if not addrs_set.issubset(used):
            result.append(g)
            used.update(addrs_set)

    return result



def variable_report(variables, labels=None, procedures=None):
    """Generate text report of variable analysis.

    Args:
        variables: dict from analyze_variables()
        labels: optional {addr_hex: label_info}
        procedures: optional dict from build_procedures()

    Returns:
        Multi-line text report.
    """
    if labels is None:
        labels = {}

    lines = ['=== Variable Analysis ===']

    # Count by type
    type_counts = defaultdict(int)
    for v in variables.values():
        type_counts[v['type']] += 1
    lines.append(f'Total addresses analyzed: {len(variables)}')
    lines.append(f'Types: {dict(sorted(type_counts.items(), key=lambda x: -x[1]))}')

    # Global state variables (most interesting for understanding)
    lines.append('\n--- Global State Variables (read by many, written by few) ---')
    state_vars = [(a, v) for a, v in variables.items()
                  if v['scope'] == 'global' and v['type'] in ('state_var', 'counter', 'flag', 'bcd_value')]
    state_vars.sort(key=lambda x: len(x[1]['readers']), reverse=True)
    for a, v in state_vars[:25]:
        name = labels.get(f'{a:04X}', {}).get('name', '') if isinstance(labels.get(f'{a:04X}'), dict) else ''
        lines.append(f'  ${a:04X}  {v["type"]:12s}  R={len(v["readers"]):2d} W={len(v["writers"]):2d}  {name}')

    # Array bases (data structures)
    lines.append('\n--- Array Bases (indexed access, likely data structures) ---')
    arrays = [(a, v) for a, v in variables.items() if v['type'] == 'array_base']
    arrays.sort(key=lambda x: x[0])
    for a, v in arrays[:30]:
        total_ops = sum(v['ops'].values())
        name = labels.get(f'{a:04X}', {}).get('name', '') if isinstance(labels.get(f'{a:04X}'), dict) else ''
        lines.append(f'  ${a:04X}  ops={total_ops:3d}  procs={len(v["readers"] | v["writers"]):2d}  {name}')

    # BCD variables
    bcd_vars = [(a, v) for a, v in variables.items() if v['type'] == 'bcd_value']
    if bcd_vars:
        lines.append(f'\n--- BCD Values (score/counter, {len(bcd_vars)} found) ---')
        for a, v in sorted(bcd_vars):
            lines.append(f'  ${a:04X}  imm={sorted(v["imm_values"])}')

    # Flags
    flags = [(a, v) for a, v in variables.items() if v['type'] == 'flag' and v['scope'] in ('shared', 'global')]
    if flags:
        lines.append(f'\n--- Flags/Booleans ({len(flags)} shared/global) ---')
        for a, v in sorted(flags):
            vals = sorted(v['imm_values'])
            lines.append(f'  ${a:04X}  R={len(v["readers"]):2d} W={len(v["writers"]):2d}  vals={vals}')

    # Pointers (ZP indirect)
    ptrs = [(a, v) for a, v in variables.items() if v['type'] == 'pointer']
    if ptrs:
        lines.append(f'\n--- ZP Pointers ({len(ptrs)} found) ---')
        for a, v in sorted(ptrs):
            lines.append(f'  ${a:02X}  R={len(v["readers"]):2d} W={len(v["writers"]):2d}')

    return '\n'.join(lines)



