"""analyzer.disasm — disasm analysis functions."""

from collections import defaultdict

from altirra_bridge._analyzer_tables import (
    OPCODES as _OPCODES,
    BRANCHES as _BRANCHES,
)

def recursive_descent(memory, entry_points, regions=None):
    """Perform recursive descent disassembly from entry points.

    Args:
        memory: object supporting __getitem__ for byte access
        entry_points: list of (addr, label_type) tuples
        regions: optional existing regions list to avoid re-classifying

    Returns:
        (new_regions, xrefs) where:
        - new_regions: list of {'start', 'end', 'type': 'code'} dicts
        - xrefs: list of {'from', 'to', 'type'} dicts
    """
    visited = set()
    queue = list(entry_points)
    xrefs = []
    code_addrs = set()

    # Build set of already-classified addresses
    classified = set()
    if regions:
        for r in regions:
            for a in range(int(r.get('start', '0'), 16) if isinstance(r.get('start'), str) else r.get('start', 0),
                           (int(r.get('end', '0'), 16) if isinstance(r.get('end'), str) else r.get('end', 0)) + 1):
                classified.add(a)

    while queue:
        addr, _ = queue.pop(0)
        if addr in visited:
            continue
        if addr < 0 or addr > 0xFFFF:
            continue
        # Don't traverse into hardware registers
        if 0xD000 <= addr <= 0xD7FF:
            continue

        visited.add(addr)

        opcode = memory[addr]
        if opcode not in _OPCODES:
            continue

        mnem, mode, size = _OPCODES[opcode]
        for i in range(size):
            code_addrs.add(addr + i)

        # Follow control flow
        if mnem == 'RTS' or mnem == 'RTI' or mnem == 'BRK':
            continue

        if mnem == 'JMP':
            if mode == 'abs':
                lo = memory[(addr + 1) & 0xFFFF]
                hi = memory[(addr + 2) & 0xFFFF]
                target = lo | (hi << 8)
                xrefs.append({'from': f'{addr:04X}', 'to': f'{target:04X}', 'type': 'jmp'})
                queue.append((target, 'jump_target'))
            # JMP indirect — can't follow statically
            continue

        if mnem == 'JSR':
            lo = memory[(addr + 1) & 0xFFFF]
            hi = memory[(addr + 2) & 0xFFFF]
            target = lo | (hi << 8)
            xrefs.append({'from': f'{addr:04X}', 'to': f'{target:04X}', 'type': 'jsr'})
            queue.append((target, 'subroutine'))
            # Continue after JSR
            queue.append((addr + size, 'code'))
            continue

        if mnem in _BRANCHES:
            offset = memory[(addr + 1) & 0xFFFF]
            if offset >= 128:
                offset -= 256
            target = (addr + 2 + offset) & 0xFFFF
            xrefs.append({'from': f'{addr:04X}', 'to': f'{target:04X}', 'type': 'branch'})
            queue.append((target, 'branch_local'))
            queue.append((addr + size, 'code'))
            continue

        # Normal instruction — continue to next
        queue.append((addr + size, 'code'))

    # Build contiguous code regions from discovered addresses
    new_regions = []
    if code_addrs:
        sorted_addrs = sorted(code_addrs)
        region_start = sorted_addrs[0]
        region_end = sorted_addrs[0]
        for a in sorted_addrs[1:]:
            if a == region_end + 1:
                region_end = a
            else:
                new_regions.append({'start': f'{region_start:04X}', 'end': f'{region_end:04X}', 'type': 'code'})
                region_start = a
                region_end = a
        new_regions.append({'start': f'{region_start:04X}', 'end': f'{region_end:04X}', 'type': 'code'})

    return new_regions, xrefs



