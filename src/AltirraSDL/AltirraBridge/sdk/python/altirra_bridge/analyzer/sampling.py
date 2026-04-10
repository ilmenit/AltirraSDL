"""analyzer.sampling — sampling analysis functions."""

from collections import defaultdict

from altirra_bridge._analyzer_tables import (
    OPCODES as _OPCODES,
    BRANCHES as _BRANCHES,
    ADDR_TO_NAME,
)

from .disasm import recursive_descent
from .hw import decode_portb

def profile_guided_classify(emu, memory, existing_regions, existing_labels,
                            frames=300, min_exec_count=2):
    """Use runtime profiling to discover code regions missed by static analysis.

    Runs the emulator with profiling, finds executed addresses not in any
    classified region, and runs recursive_descent from them.  This catches
    code reached via indirect jumps and dispatch tables that static analysis
    cannot follow.

    Args:
        emu: attached Atari800 emulator instance
        memory: memory accessor supporting __getitem__
        existing_regions: current list of region dicts
        existing_labels: current labels dict
        frames: frames to profile (default 300)
        min_exec_count: minimum execution count to treat as real code

    Returns:
        dict with 'new_regions', 'new_xrefs', 'entry_points',
        'total_profiled', 'already_classified', 'newly_discovered', 'report'.
    """
    # Step 1: profile
    emu.profile_reset()
    emu.frame(frames)
    resp = emu.profile_dump()
    entries = resp.get('entries', resp.get('data', []))

    profiled_addrs = set()
    for e in entries:
        addr = e.get('a', e.get('addr', 0))
        count = e.get('c', e.get('count', 0))
        if count >= min_exec_count and addr < 0xC000 and not (0xD000 <= addr <= 0xD7FF):
            profiled_addrs.add(addr)

    # Step 2: build classified set
    classified = set()
    for r in existing_regions:
        if r.get('type') == 'code':
            rs = int(r['start'], 16) if isinstance(r.get('start'), str) else r.get('start', 0)
            re_ = int(r['end'], 16) if isinstance(r.get('end'), str) else r.get('end', 0)
            for a in range(rs, re_ + 1):
                classified.add(a)

    # Step 3: find unclassified executed addresses
    unclassified = profiled_addrs - classified
    already = len(profiled_addrs) - len(unclassified)

    # Step 4: group into contiguous ranges (gap tolerance of 3 bytes)
    if not unclassified:
        return {
            'new_regions': [], 'new_xrefs': [], 'entry_points': [],
            'total_profiled': len(profiled_addrs), 'already_classified': already,
            'newly_discovered': 0,
            'report': (f'Profiled {len(profiled_addrs)} game code addresses, '
                       f'all already classified. No new code found.'),
        }

    sorted_addrs = sorted(unclassified)
    ranges = []
    start = sorted_addrs[0]
    prev = start
    for a in sorted_addrs[1:]:
        if a > prev + 3:
            ranges.append((start, prev))
            start = a
        prev = a
    ranges.append((start, prev))

    # Pick entry points: start of each range with >= 3 bytes
    entry_points = []
    for rs, re_ in ranges:
        if re_ - rs >= 2:
            entry_points.append((rs, 'profiled'))

    # Step 5: recursive descent from new entry points
    new_regions, new_xrefs = recursive_descent(memory, entry_points, existing_regions)

    # Step 6: build report
    lines = [
        f'=== Profile-Guided Classification ===',
        f'Frames profiled: {frames}',
        f'Executed game code addresses: {len(profiled_addrs)}',
        f'Already classified: {already}',
        f'Newly discovered: {len(unclassified)} addresses in {len(ranges)} ranges',
        f'Entry points used: {len(entry_points)}',
        f'New code regions: {len(new_regions)}',
        f'New xrefs: {len(new_xrefs)}',
    ]
    code_bytes = sum(int(r['end'], 16) - int(r['start'], 16) + 1 for r in new_regions)
    lines.append(f'New code bytes: {code_bytes}')
    if ranges:
        lines.append('')
        lines.append('Discovered ranges:')
        for rs, re_ in sorted(ranges):
            if re_ - rs >= 2:
                lines.append(f'  ${rs:04X}-${re_:04X}  ({re_ - rs + 1} bytes)')

    return {
        'new_regions': new_regions,
        'new_xrefs': new_xrefs,
        'entry_points': entry_points,
        'total_profiled': len(profiled_addrs),
        'already_classified': already,
        'newly_discovered': len(unclassified),
        'report': '\n'.join(lines),
    }



def sample_pc(emu, frames=100):
    """Sample PC values across frames to find hot code.

    Args:
        emu: attached Atari800 emulator instance
        frames: number of frames to sample

    Returns:
        Text with PC frequency table.
    """
    from collections import Counter
    pc_counts = Counter()

    for _ in range(frames):
        emu.frame(1)
        regs = emu.regs()
        pc = regs.get('PC')
        if pc is not None:
            if isinstance(pc, str):
                pc = int(pc.lstrip('$'), 16)
            pc_counts[pc] += 1

    lines = [f'=== PC Sampling ({frames} frames) ===']
    for addr, count in pc_counts.most_common(20):
        pct = count * 100.0 / frames
        name = ADDR_TO_NAME.get(addr, '')
        lines.append(f'  ${addr:04X}  {count:4d} ({pct:5.1f}%)  {name}')

    return '\n'.join(lines)



def sample_dli_chain(emu):
    """Discover DLI handler chain by sampling VDSLST across scanlines.

    Returns text describing the DLI chain.
    """
    # Read initial VDSLST
    vdslst_lo = emu.peek(0x0200, 1)[0]
    vdslst_hi = emu.peek(0x0201, 1)[0]
    initial = vdslst_lo | (vdslst_hi << 8)

    lines = [f'=== DLI Chain ===']
    lines.append(f'  Initial VDSLST: ${initial:04X}')

    # Run a frame and sample VDSLST multiple times
    # (limited by bridge — we sample between frames)
    seen = [initial]
    for i in range(10):
        emu.frame(1)
        lo = emu.peek(0x0200, 1)[0]
        hi = emu.peek(0x0201, 1)[0]
        addr = lo | (hi << 8)
        if addr not in seen:
            seen.append(addr)

    for i, addr in enumerate(seen):
        name = ADDR_TO_NAME.get(addr, '')
        lines.append(f'  DLI {i}: ${addr:04X}  {name}')

    return '\n'.join(lines)



def sample_portb(emu, frames=100):
    """Monitor PORTB values across frames.

    Returns text listing observed PORTB values.
    """
    seen = set()
    for _ in range(frames):
        emu.frame(1)
        val = emu.peek(0xD301, 1)[0]
        seen.add(val)

    lines = [f'=== PORTB Sampling ({frames} frames) ===']
    for val in sorted(seen):
        desc = decode_portb(val)
        first_line = desc.split('\n')[0]
        lines.append(f'  ${val:02X}  {first_line}')

    return '\n'.join(lines)



def diff_memory(emu, action, regions=None, label='action'):
    """Diff memory before/after an action to find changed bytes.

    Args:
        emu: attached emulator
        action: callable that performs the action (e.g., move joystick + frames)
        regions: list of (start, end) tuples to monitor (default: zero page + page 2)
        label: description of the action

    Returns:
        Text listing changed memory locations.
    """
    if regions is None:
        regions = [(0x80, 0xFF), (0x200, 0x2FF)]

    # Capture before
    before = {}
    for start, end in regions:
        length = end - start + 1
        data = emu.peek(start, length)
        for i, b in enumerate(data):
            before[start + i] = b

    # Perform action
    action()

    # Capture after
    after = {}
    for start, end in regions:
        length = end - start + 1
        data = emu.peek(start, length)
        for i, b in enumerate(data):
            after[start + i] = b

    # Find changes
    changes = []
    for addr in sorted(before.keys()):
        if before[addr] != after.get(addr, before[addr]):
            name = ADDR_TO_NAME.get(addr, '')
            changes.append((addr, before[addr], after[addr], name))

    lines = [f'=== Memory Diff: {label} ===']
    if not changes:
        lines.append('  No changes detected.')
    else:
        lines.append(f'  {len(changes)} byte(s) changed:')
        for addr, old, new, name in changes:
            label_str = f'  ({name})' if name else ''
            lines.append(f'  ${addr:04X}: ${old:02X} → ${new:02X}{label_str}')

    return '\n'.join(lines)


# ── Deep analysis helpers ──



