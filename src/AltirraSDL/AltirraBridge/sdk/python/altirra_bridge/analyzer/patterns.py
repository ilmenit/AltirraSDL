"""analyzer.patterns — patterns analysis functions."""

from collections import defaultdict

from altirra_bridge._analyzer_tables import (
    OPCODES as _OPCODES,
    BRANCHES as _BRANCHES,
    ADDR_TO_NAME,
)

def detect_patterns(memory, start, end):
    """Detect data patterns in a memory range.

    Returns list of hints like {'start', 'end', 'hint', 'confidence'}.
    """
    hints = []
    size = end - start + 1

    # Check for charset (aligned 1024 or 512 bytes, 8-byte character patterns)
    if size >= 512 and (start & 0x1FF) == 0:
        # Simple heuristic: if many 8-byte groups have bit patterns
        nonzero_chars = 0
        for i in range(0, min(size, 1024), 8):
            char_bytes = [memory[(start + i + j) & 0xFFFF] for j in range(8)]
            if any(b != 0 for b in char_bytes):
                nonzero_chars += 1
        if nonzero_chars > 20:
            hint_end = min(start + 1023, end)
            hints.append({'start': f'{start:04X}', 'end': f'{hint_end:04X}',
                          'hint': 'charset', 'confidence': 0.6})

    # Check for repeated fill bytes
    if size >= 16:
        first_byte = memory[start]
        all_same = all(memory[(start + i) & 0xFFFF] == first_byte for i in range(size))
        if all_same:
            hints.append({'start': f'{start:04X}', 'end': f'{end:04X}',
                          'hint': 'fill', 'confidence': 0.9})

    # Check for ATASCII text
    text_count = sum(1 for i in range(size)
                     if 0x20 <= memory[(start + i) & 0xFFFF] < 0x7F)
    if text_count > size * 0.7 and size >= 4:
        hints.append({'start': f'{start:04X}', 'end': f'{end:04X}',
                      'hint': 'string_atascii', 'confidence': 0.5})

    return hints



def classify_gap(mem, start, end, code_range=None):
    """Heuristic classification of an unclassified memory range.

    Tries disassembly from start. If valid instruction sequence covers >60%
    of the gap with >=4 instructions and contains RTS/branch/>=8 instructions,
    classifies as code. Otherwise applies data pattern detection.

    Args:
        mem: memory accessor (supports mem[addr])
        start, end: address range (inclusive, int)
        code_range: optional list of (start, end) tuples for address table detection

    Returns:
        dict with 'type' ('code'|'data'), 'hint' (str), 'confidence' (float).
        Code results also include 'entry_addr' (int).
    """
    gap_size = end - start + 1
    if gap_size == 0:
        return {'type': 'data', 'hint': 'bytes', 'confidence': 0.0}

    # Try disassembly from start
    addr = start
    valid_instrs = 0
    total_bytes = 0
    has_rts = False
    has_branch = False
    max_scan = min(gap_size, 256)

    while addr <= end and (addr - start) < max_scan:
        opcode = mem[addr]
        if opcode not in _OPCODES:
            break
        mnem, mode, size = _OPCODES[opcode]
        if addr + size - 1 > end:
            break
        # Reject accesses to stack page as absolute targets
        if size == 3 and mode == 'abs':
            target = mem[(addr + 1) & 0xFFFF] | (mem[(addr + 2) & 0xFFFF] << 8)
            if 0x0100 <= target <= 0x01FF:
                break
        valid_instrs += 1
        total_bytes += size
        if mnem in ('RTS', 'RTI'):
            has_rts = True
        if mnem in _BRANCHES:
            has_branch = True
        addr += size

    coverage = total_bytes / gap_size if gap_size > 0 else 0

    if valid_instrs >= 4 and coverage > 0.6 and (has_rts or has_branch or valid_instrs >= 8):
        return {'type': 'code', 'hint': 'gap_code', 'confidence': coverage,
                'entry_addr': start}

    # Data classification
    # Check all-zero fill
    if all(mem[(start + i) & 0xFFFF] == 0 for i in range(gap_size)):
        return {'type': 'data', 'hint': 'fill_zero', 'confidence': 1.0}

    # Check ATASCII text (standard + inverse)
    text_count = sum(1 for i in range(gap_size)
                     if 0x20 <= mem[(start + i) & 0xFFFF] < 0x7F)
    if text_count > gap_size * 0.7 and gap_size >= 4:
        return {'type': 'data', 'hint': 'string', 'confidence': text_count / gap_size}

    # Check address table (lo/hi pairs pointing to valid code)
    if code_range and gap_size >= 4 and gap_size % 2 == 0:
        valid_addrs = 0
        check_count = min(gap_size, 32) // 2
        for i in range(0, check_count * 2, 2):
            lo = mem[(start + i) & 0xFFFF]
            hi = mem[(start + i + 1) & 0xFFFF]
            target = lo | (hi << 8)
            if any(s <= target <= e for s, e in code_range):
                valid_addrs += 1
        if valid_addrs >= 2 and valid_addrs >= check_count * 0.5:
            return {'type': 'data', 'hint': 'addr_table', 'confidence': valid_addrs / check_count}

    return {'type': 'data', 'hint': 'bytes', 'confidence': 0.3}



def scan_address_tables(mem, regions, code_range):
    """Find lo/hi byte pairs in data or unclassified areas that form valid code addresses.

    Args:
        mem: memory accessor
        regions: list of classified region dicts (with 'start', 'end', 'type' as hex strings)
        code_range: list of (start, end) tuples defining valid code address space

    Returns:
        list of {'table_addr': int, 'targets': [int, ...]} for discovered tables
    """
    classified = set()
    for r in regions:
        rs = int(r['start'], 16)
        re_ = int(r['end'], 16)
        for a in range(rs, re_ + 1):
            classified.add(a)

    # Collect data regions and unclassified gaps within code_range
    scan_ranges = []
    for r in regions:
        if r['type'] == 'data' and r.get('hint') in ('bytes', 'addr_table', None):
            scan_ranges.append((int(r['start'], 16), int(r['end'], 16)))

    for seg_start, seg_end in code_range:
        gap_start = None
        for addr in range(seg_start, seg_end + 1):
            if addr not in classified:
                if gap_start is None:
                    gap_start = addr
            else:
                if gap_start is not None:
                    scan_ranges.append((gap_start, addr - 1))
                    gap_start = None
        if gap_start is not None:
            scan_ranges.append((gap_start, seg_end))

    results = []
    for rng_start, rng_end in scan_ranges:
        size = rng_end - rng_start + 1
        if size < 4 or size % 2 != 0:
            continue
        targets = []
        for i in range(0, size, 2):
            lo = mem[(rng_start + i) & 0xFFFF]
            hi = mem[(rng_start + i + 1) & 0xFFFF]
            target = lo | (hi << 8)
            if any(s <= target <= e for s, e in code_range) and mem[target] in _OPCODES:
                targets.append(target)
            else:
                targets.append(None)
        # Find runs of at least 2 consecutive valid targets
        run_start = None
        run_targets = []
        for idx, t in enumerate(targets):
            if t is not None:
                if run_start is None:
                    run_start = idx
                    run_targets = [t]
                else:
                    run_targets.append(t)
            else:
                if run_start is not None and len(run_targets) >= 2:
                    results.append({
                        'table_addr': rng_start + run_start * 2,
                        'targets': list(run_targets),
                    })
                run_start = None
                run_targets = []
        if run_start is not None and len(run_targets) >= 2:
            results.append({
                'table_addr': rng_start + run_start * 2,
                'targets': list(run_targets),
            })

    return results



def scan_indirect_jumps(mem, regions):
    """Find JMP ($xxxx) instructions in code regions and resolve pointer targets.

    Args:
        mem: memory accessor
        regions: list of classified region dicts

    Returns:
        list of {'jmp_addr': int, 'ptr_addr': int, 'target_addr': int}
    """
    results = []
    for r in regions:
        if r.get('type') != 'code':
            continue
        start = int(r['start'], 16)
        end = int(r['end'], 16)
        addr = start
        while addr <= end:
            opcode = mem[addr]
            if opcode not in _OPCODES:
                addr += 1
                continue
            mnem, mode, size = _OPCODES[opcode]
            if opcode == 0x6C:  # JMP indirect
                ptr_lo = mem[(addr + 1) & 0xFFFF]
                ptr_hi = mem[(addr + 2) & 0xFFFF]
                ptr = ptr_lo | (ptr_hi << 8)
                if ptr < 0xD000 or ptr > 0xD7FF:
                    target_lo = mem[ptr & 0xFFFF]
                    target_hi = mem[(ptr + 1) & 0xFFFF]
                    target = target_lo | (target_hi << 8)
                    results.append({
                        'jmp_addr': addr,
                        'ptr_addr': ptr,
                        'target_addr': target,
                    })
            addr += size
    return results



def scan_vector_pointers(mem, code_range):
    """Check ZP ($80-$FF) and page 2 OS vectors for pointers into game code.

    Args:
        mem: memory accessor
        code_range: list of (start, end) tuples

    Returns:
        list of {'ptr_addr': int, 'target_addr': int, 'source': str, 'vector_name': str|None}
    """
    results = []

    def _in_range(target):
        return any(s <= target <= e for s, e in code_range)

    # Scan zero page $80-$FE by 2s
    for zp in range(0x80, 0xFF, 2):
        lo = mem[zp]
        hi = mem[(zp + 1) & 0xFFFF]
        target = lo | (hi << 8)
        if _in_range(target) and mem[target] in _OPCODES:
            results.append({
                'ptr_addr': zp,
                'target_addr': target,
                'source': 'zp',
                'vector_name': ADDR_TO_NAME.get(zp),
            })

    # Scan page 2 OS vectors
    p2_vectors = [
        0x0200, 0x0202, 0x0204, 0x0206, 0x0208, 0x020A, 0x020C, 0x020E,
        0x0210, 0x0212, 0x0214, 0x0216, 0x0218, 0x021A, 0x021C, 0x021E,
    ]
    for p2 in p2_vectors:
        lo = mem[p2]
        hi = mem[(p2 + 1) & 0xFFFF]
        target = lo | (hi << 8)
        if _in_range(target) and mem[target] in _OPCODES:
            results.append({
                'ptr_addr': p2,
                'target_addr': target,
                'source': 'page2',
                'vector_name': ADDR_TO_NAME.get(p2),
            })

    return results



def detect_split_tables(mem, regions, code_range):
    """Detect split lo/hi address tables — a common 6502 pattern.

    Many 6502 programs store address tables as two separate byte arrays:
    one for low bytes (addr_lo) and one for high bytes (addr_hi), indexed
    the same way. This detects such pairs by scanning data regions for
    byte sequences where combining lo[i]/hi[i] produces valid code addresses.

    Also detects monotonic/periodic lookup tables and constant data tables.

    Args:
        mem: memory accessor
        regions: list of classified region dicts
        code_range: list of (start, end) tuples defining valid code ranges

    Returns:
        list of dicts:
        - For split addr tables: {'type': 'split_addr_table', 'lo_addr': int,
          'hi_addr': int, 'count': int, 'targets': [int]}
        - For lookup tables: {'type': 'lookup_table', 'addr': int,
          'count': int, 'hint': str} where hint is 'monotonic', 'powers',
          'symmetric', or 'periodic'
    """
    results = []

    # Collect data region ranges
    data_ranges = []
    for r in regions:
        if r.get('type') == 'data':
            data_ranges.append((int(r['start'], 16), int(r['end'], 16)))

    def _valid_code_addr(target):
        return any(s <= target <= e for s, e in code_range)

    # Scan pairs of data regions for split lo/hi tables
    # Look for N-byte regions where combining with another region yields valid addresses
    for i, (lo_start, lo_end) in enumerate(data_ranges):
        lo_size = lo_end - lo_start + 1
        if lo_size < 3 or lo_size > 64:
            continue
        for j, (hi_start, hi_end) in enumerate(data_ranges):
            if j == i:
                continue
            hi_size = hi_end - hi_start + 1
            if hi_size != lo_size:
                continue
            # Try combining as lo/hi pair
            valid_count = 0
            targets = []
            for k in range(lo_size):
                lo_byte = mem[(lo_start + k) & 0xFFFF]
                hi_byte = mem[(hi_start + k) & 0xFFFF]
                target = lo_byte | (hi_byte << 8)
                if _valid_code_addr(target):
                    valid_count += 1
                    targets.append(target)
                else:
                    targets.append(None)
            # Need at least 3 valid and >60% valid
            if valid_count >= 3 and valid_count >= lo_size * 0.6:
                results.append({
                    'type': 'split_addr_table',
                    'lo_addr': lo_start,
                    'hi_addr': hi_start,
                    'count': lo_size,
                    'targets': [t for t in targets if t is not None],
                })

    # Detect lookup table patterns in data regions
    for d_start, d_end in data_ranges:
        d_size = d_end - d_start + 1
        if d_size < 8 or d_size > 256:
            continue
        data = [mem[(d_start + k) & 0xFFFF] for k in range(d_size)]

        # Check monotonic (sorted ascending or descending)
        ascending = all(data[k] <= data[k + 1] for k in range(d_size - 1))
        descending = all(data[k] >= data[k + 1] for k in range(d_size - 1))
        if ascending and data[-1] > data[0]:
            results.append({'type': 'lookup_table', 'addr': d_start,
                            'count': d_size, 'hint': 'monotonic_asc'})
        elif descending and data[0] > data[-1]:
            results.append({'type': 'lookup_table', 'addr': d_start,
                            'count': d_size, 'hint': 'monotonic_desc'})

        # Check symmetric (first half mirrors second half)
        if d_size >= 8 and d_size % 2 == 0:
            half = d_size // 2
            if all(data[k] == data[d_size - 1 - k] for k in range(half)):
                results.append({'type': 'lookup_table', 'addr': d_start,
                                'count': d_size, 'hint': 'symmetric'})

        # Check periodic (repeating pattern of period 2-8)
        for period in range(2, min(9, d_size // 2 + 1)):
            if d_size % period == 0:
                is_periodic = all(data[k] == data[k % period]
                                  for k in range(d_size))
                if is_periodic:
                    results.append({'type': 'lookup_table', 'addr': d_start,
                                    'count': d_size, 'hint': f'periodic_{period}'})
                    break

    return results


# ── Variable analysis ──



