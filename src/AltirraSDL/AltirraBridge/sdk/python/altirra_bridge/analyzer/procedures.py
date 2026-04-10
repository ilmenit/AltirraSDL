"""analyzer.procedures — procedures analysis functions."""

from collections import defaultdict

from altirra_bridge._analyzer_tables import (
    OPCODES as _OPCODES,
    BRANCHES as _BRANCHES,
    HW_READ,
    HW_WRITE,
    ADDR_TO_NAME,
)

from .hw import classify_hw_access

def _trace_procedure_body(mem, entry, all_entries, code_addrs=None):
    """Trace one procedure from its entry, collecting body addresses and edges.

    Follows branches and fall-through but stops at RTS/RTI, JMP to external
    addresses, or when entering another procedure's entry.

    Args:
        mem: memory accessor
        entry: entry address (int)
        all_entries: set of all known procedure entry addresses
        code_addrs: optional set to restrict tracing to classified code addresses

    Returns:
        dict with 'body' (set of addrs), 'exits' (list of exit info),
        'jsr_targets' (list), 'jmp_targets' (list), 'branch_targets' (list),
        'flags' (list of anomaly strings)
    """
    body = set()
    exits = []        # [{addr, type: 'rts'|'rti'|'jmp'|'fall_through'|'brk'}]
    jsr_targets = []
    jmp_targets = []
    branch_targets = []
    flags = []

    visited = set()
    queue = [entry]

    while queue:
        addr = queue.pop(0)
        if addr in visited:
            continue
        if addr < 0 or addr > 0xFFFF:
            continue
        if 0xD000 <= addr <= 0xD7FF:
            continue
        # Stop at another procedure's entry (unless it's our own)
        if addr != entry and addr in all_entries:
            # Fall-through into another procedure
            if body and addr == max(body) + 1:
                exits.append({'addr': addr, 'type': 'fall_through', 'target': addr})
            continue
        # Stop if outside classified code (if we have that info)
        if code_addrs is not None and addr not in code_addrs:
            continue

        visited.add(addr)

        opcode = mem[addr]
        if opcode not in _OPCODES:
            continue

        mnem, mode, size = _OPCODES[opcode]

        # Add all bytes of this instruction to body
        for i in range(size):
            body.add(addr + i)

        # Detect anomalies
        # Stack manipulation: PLA/PLA pattern (discard return address)
        if mnem == 'PLA' and (addr + 1) in visited:
            prev_op = mem[(addr - 1) & 0xFFFF]
            if prev_op in _OPCODES and _OPCODES[prev_op][0] == 'PLA':
                flags.append('stack_discard')

        # Self-modifying code: STA targeting an address within a code region
        if mnem == 'STA' and mode == 'abs':
            sta_target = mem[(addr + 1) & 0xFFFF] | (mem[(addr + 2) & 0xFFFF] << 8)
            if code_addrs is not None and sta_target in code_addrs:
                flags.append('self_modify')

        # Handle control flow
        if mnem in ('RTS', 'RTI'):
            exits.append({'addr': addr, 'type': mnem.lower()})
            continue

        if mnem == 'BRK':
            exits.append({'addr': addr, 'type': 'brk'})
            continue

        if mnem == 'JMP':
            if mode == 'abs':
                target = mem[(addr + 1) & 0xFFFF] | (mem[(addr + 2) & 0xFFFF] << 8)
                jmp_targets.append(target)
                exits.append({'addr': addr, 'type': 'jmp', 'target': target})
            elif mode == 'ind':
                exits.append({'addr': addr, 'type': 'jmp_indirect'})
                flags.append('indirect_jmp')
            continue

        if mnem == 'JSR':
            target = mem[(addr + 1) & 0xFFFF] | (mem[(addr + 2) & 0xFFFF] << 8)
            jsr_targets.append(target)
            # Detect inline data pattern: JSR to routine that starts with PLA/STA
            if mem[target] in _OPCODES:
                t_mnem = _OPCODES[mem[target]][0]
                if t_mnem == 'PLA':
                    # Check next instruction at target+1
                    if mem[target + 1] in _OPCODES and _OPCODES[mem[target + 1]][0] in ('STA', 'TAX', 'TAY'):
                        flags.append('inline_data_jsr')
            # Continue after JSR
            queue.append(addr + size)
            continue

        if mnem in _BRANCHES:
            offset = mem[(addr + 1) & 0xFFFF]
            if offset >= 128:
                offset -= 256
            target = (addr + 2 + offset) & 0xFFFF
            branch_targets.append(target)
            queue.append(target)
            queue.append(addr + size)
            continue

        # Normal instruction — continue to next
        queue.append(addr + size)

    # Detect BIT skip trick: look for $2C (BIT abs) where a branch targets $2C+1
    branch_set = set(branch_targets)
    for a in body:
        if mem[a] == 0x2C and (a + 1) in branch_set:
            flags.append('bit_skip_trick')
            break

    # Detect RTS dispatch: PHA/LDA/PHA/RTS pattern
    sorted_body = sorted(body)
    for i in range(len(sorted_body) - 3):
        a = sorted_body[i]
        seq = [mem[(a + j) & 0xFFFF] for j in range(4)]
        # PHA=48, LDA variants, PHA=48, RTS=60
        if seq[0] == 0x48 and seq[2] == 0x48 and seq[3] == 0x60:
            flags.append('rts_dispatch')
            break

    return {
        'body': body,
        'exits': exits,
        'jsr_targets': jsr_targets,
        'jmp_targets': jmp_targets,
        'branch_targets': branch_targets,
        'flags': list(set(flags)),  # deduplicate
    }



def build_procedures(mem, xrefs, regions, labels=None):
    """Identify procedure boundaries from xrefs and code regions.

    Layer 1: Uses JSR targets as procedure entries, traces each to find body
    and exits. Layer 2: Classifies JMP edges as tail calls vs internal jumps.

    Args:
        mem: memory accessor
        xrefs: list of xref dicts with 'from', 'to', 'type'
        regions: list of region dicts with 'start', 'end', 'type'
        labels: optional {addr_hex: label_info} dict

    Returns:
        dict of addr -> procedure info:
        {
            'entry': int,
            'end': int,              # last instruction address
            'size': int,             # body byte count
            'exit_type': str,        # primary: 'rts', 'rti', 'jmp_tail', 'fall_through', 'noreturn', 'mixed'
            'calls': [int],          # JSR targets
            'tail_calls': [int],     # JMP to known procedure
            'internal_jumps': [int], # JMP within own body
            'callers': [int],        # who JSRs here
            'tail_callers': [int],   # who JMPs here as tail call
            'is_leaf': bool,
            'is_interrupt': bool,
            'flags': [str],          # anomalies
        }
    """
    if labels is None:
        labels = {}

    # Build set of all classified code addresses
    code_addrs = set()
    for r in regions:
        if r.get('type') == 'code':
            rs = int(r['start'], 16)
            re_ = int(r['end'], 16)
            for a in range(rs, re_ + 1):
                code_addrs.add(a)

    # Collect JSR targets as procedure entries
    jsr_targets = set()
    jsr_callers = defaultdict(list)   # target -> [caller_addrs]
    jmp_edges = []                     # (from_addr, to_addr)

    for x in xrefs:
        from_addr = int(x['from'].split(':')[0], 16)
        to_addr = int(x['to'].split(':')[0], 16)
        if x['type'] == 'jsr':
            jsr_targets.add(to_addr)
            jsr_callers[to_addr].append(from_addr)
        elif x['type'] == 'jmp':
            jmp_edges.append((from_addr, to_addr))

    # Also add labeled entry points, vectors, IRQs, DLIs
    for key, info in labels.items():
        if info.get('type') in ('entry_point', 'vector', 'dli', 'irq'):
            try:
                addr = int(key.split(':')[0], 16)
                jsr_targets.add(addr)
            except ValueError:
                pass

    all_entries = jsr_targets

    # Trace each procedure
    procedures = {}
    for entry in sorted(all_entries):
        if entry not in code_addrs and code_addrs:
            continue

        trace = _trace_procedure_body(mem, entry, all_entries, code_addrs if code_addrs else None)
        if not trace['body']:
            continue

        body = trace['body']
        sorted_body = sorted(body)
        end_addr = sorted_body[-1]

        # Classify exits and JMP edges
        calls = list(set(trace['jsr_targets']))
        tail_calls = []
        internal_jumps = []

        for target in trace['jmp_targets']:
            if target in all_entries:
                tail_calls.append(target)
            elif target in body:
                internal_jumps.append(target)
            else:
                # JMP to address not in body and not a known procedure
                # Could be shared epilogue, trampoline, or unknown code
                # Check if target is a small block ending in RTS (shared epilogue)
                if mem[target] in _OPCODES:
                    # Trace a few instructions from target
                    t_addr = target
                    t_count = 0
                    is_epilogue = False
                    while t_count < 8:
                        t_op = mem[t_addr]
                        if t_op not in _OPCODES:
                            break
                        t_mnem, t_mode, t_size = _OPCODES[t_op]
                        t_count += 1
                        if t_mnem in ('RTS', 'RTI'):
                            is_epilogue = True
                            break
                        if t_mnem in ('JMP', 'JSR') or t_mnem in _BRANCHES:
                            break
                        t_addr += t_size
                    if is_epilogue:
                        # Shared epilogue — treat as part of this procedure conceptually
                        internal_jumps.append(target)
                    else:
                        # Unknown — might be tail call to undiscovered procedure
                        tail_calls.append(target)

        # Determine primary exit type
        exit_types = set(e['type'] for e in trace['exits'])
        if len(exit_types) == 1:
            et = exit_types.pop()
            if et == 'jmp':
                # All exits are JMPs — are they all tail calls?
                if all(e.get('target') in all_entries for e in trace['exits'] if e['type'] == 'jmp'):
                    exit_type = 'jmp_tail'
                else:
                    exit_type = 'jmp'
            else:
                exit_type = et
        elif 'rts' in exit_types and 'jmp' in exit_types:
            exit_type = 'mixed'
        elif 'rts' in exit_types:
            exit_type = 'rts'
        elif 'rti' in exit_types:
            exit_type = 'rti'
        elif 'fall_through' in exit_types:
            exit_type = 'fall_through'
        elif not exit_types:
            exit_type = 'noreturn'
        else:
            exit_type = 'mixed'

        # Check if interrupt handler
        entry_key = f'{entry:04X}'
        is_interrupt = False
        if entry_key in labels:
            if labels[entry_key].get('type') in ('vector', 'dli', 'irq'):
                is_interrupt = True

        procedures[entry] = {
            'entry': entry,
            'end': end_addr,
            'size': len(body),
            'exit_type': exit_type,
            'calls': sorted(set(calls)),
            'tail_calls': sorted(set(tail_calls)),
            'internal_jumps': sorted(set(internal_jumps)),
            'callers': sorted(jsr_callers.get(entry, [])),
            'tail_callers': [],  # filled below
            'is_leaf': len(calls) == 0 and len(tail_calls) == 0,
            'is_interrupt': is_interrupt,
            'flags': trace['flags'],
        }

    # Fill tail_callers: for each procedure, find who tail-calls it
    for addr, proc in procedures.items():
        for tc in proc['tail_calls']:
            if tc in procedures:
                procedures[tc]['tail_callers'].append(addr)

    # Sort tail_callers
    for proc in procedures.values():
        proc['tail_callers'] = sorted(set(proc['tail_callers']))

    return procedures



def call_graph_context(procedures, addr):
    """Derive naming context for a procedure from its position in the call graph.

    Analyzes caller chains, callee patterns, and graph topology to suggest
    what subsystem a procedure belongs to.

    Args:
        procedures: dict from build_procedures()
        addr: procedure entry address (int)

    Returns:
        dict with 'subsystem' (str|None), 'role' (str), 'caller_context' (list of str),
        'depth' (int), 'fan_in' (int), 'fan_out' (int)
    """
    proc = procedures.get(addr)
    if not proc:
        return {'subsystem': None, 'role': 'unknown', 'caller_context': [],
                'depth': 0, 'fan_in': 0, 'fan_out': 0}

    fan_in = len(proc['callers']) + len(proc['tail_callers'])
    fan_out = len(proc['calls']) + len(proc['tail_calls'])

    # Determine role
    if proc['is_interrupt']:
        role = 'interrupt'
    elif proc['is_leaf'] and fan_in >= 5:
        role = 'utility'
    elif proc['is_leaf']:
        role = 'leaf'
    elif fan_in == 0 and fan_out > 0:
        role = 'entry'
    elif fan_in >= 10:
        role = 'hub'
    elif fan_out >= 5:
        role = 'dispatcher'
    else:
        role = 'internal'

    # Walk callers up to 3 levels to find named ancestors
    caller_context = []
    visited = set()

    def _walk_callers(a, depth):
        if depth > 3 or a in visited:
            return
        visited.add(a)
        p = procedures.get(a)
        if not p:
            return
        for caller in p['callers'] + p['tail_callers']:
            if caller in procedures:
                caller_context.append(caller)
            _walk_callers(caller, depth + 1)

    _walk_callers(addr, 0)

    # Compute call depth (max distance from any entry/interrupt)
    depth = _compute_depth(procedures, addr)

    return {
        'subsystem': None,  # filled by subsystem detection
        'role': role,
        'caller_context': sorted(set(caller_context)),
        'depth': depth,
        'fan_in': fan_in,
        'fan_out': fan_out,
    }



def _compute_depth(procedures, addr, _visited=None):
    """Compute call depth — distance from nearest root (entry/interrupt)."""
    if _visited is None:
        _visited = set()
    if addr in _visited:
        return 0
    _visited.add(addr)

    proc = procedures.get(addr)
    if not proc:
        return 0
    if proc['is_interrupt'] or len(proc['callers']) + len(proc['tail_callers']) == 0:
        return 0

    min_depth = 999
    for caller in proc['callers'] + proc['tail_callers']:
        d = _compute_depth(procedures, caller, _visited) + 1
        if d < min_depth:
            min_depth = d
    return min_depth if min_depth < 999 else 0



def _classify_by_hw(mem, addr):
    """Classify a procedure's subsystem by the hardware registers it accesses.

    This is fully generic — works for any Atari 8-bit program. The hardware
    register set uniquely identifies which chip a routine interacts with:
    - POKEY audio regs → sound
    - GTIA color/PMG regs → display or pmg
    - ANTIC scroll/DL/DMA regs → display
    - PIA joystick/keyboard → input
    - RTCLOK/VCOUNT polling in loops → timing
    - Heavy math with no HW → math
    - Vector stores (VVBLKI/VVBLKD/VDSLST) → init
    - Copy/fill patterns with no HW → util

    Args:
        mem: memory accessor
        addr: procedure entry address

    Returns:
        subsystem name (str) or None if not classifiable from HW alone.
    """
    analysis = analyze_subroutine(mem, addr)

    # POKEY audio writes are the strongest signal — only sound code does this
    if analysis['modifies_sound'] and not analysis['modifies_colors']:
        return 'sound'

    # Vector stores (VVBLKI, VVBLKD, VDSLST, VIMIRQ) → initialization code
    if analysis['stores_vector'] and not analysis['modifies_screen']:
        return 'init'

    # Display list modification → display setup
    if analysis['modifies_dlist'] and not analysis['modifies_screen']:
        return 'display'

    # PMG positioning/shape (HPOSP, SIZEP, GRAFP, GRACTL) without colors → pmg
    if analysis['modifies_pmg'] and not analysis['modifies_colors']:
        return 'pmg'

    # Color register writes (GTIA COLPF/COLPM/COLBK or shadow COLOR0-4)
    if analysis['modifies_colors'] and analysis['modifies_pmg']:
        return 'pmg'
    if analysis['modifies_colors'] and not analysis['modifies_screen']:
        return 'display'

    # Joystick/keyboard reads → input
    if analysis['reads_joystick'] or analysis['reads_keyboard']:
        return 'input'

    # Wait/poll loops (RTCLOK, VCOUNT) → timing
    if analysis.get('has_wait_loop'):
        return 'timing'
    if analysis.get('polls_rtclok') and analysis['has_loop']:
        return 'timing'

    # Screen memory writes with math/shifts → rendering
    if analysis['modifies_screen'] and analysis['does_math']:
        return 'render'
    if analysis['modifies_screen'] and analysis['has_loop']:
        return 'render'

    # Pure math (shifts, no HW access, no screen writes)
    if analysis['does_math'] and analysis['shift_count'] >= 3 and not analysis['hw_writes']:
        return 'math'

    # Copy/fill patterns with no HW access → utility
    if (analysis.get('has_copy_loop') or analysis.get('has_fill_loop')) and not analysis['hw_writes']:
        return 'util'

    # BCD arithmetic (score handling) → game_logic
    if analysis.get('uses_bcd'):
        return 'game_logic'

    return None



def detect_subsystems(procedures, labels=None, mem=None):
    """Detect subsystems using hardware register analysis, label keywords,
    call graph propagation, ZP workspace correlation, and address proximity.

    Classification strategy (all generic — works for any Atari 8-bit program):
    1. HW-register analysis: classify by which chip registers are accessed
       (POKEY audio → sound, GTIA color → display, PIA → input, etc.)
    2. Label keywords: generic Atari terms (sound, dli, vbi, pmg, etc.)
    3. Propagate: inherit from direct callers/callees with weighted voting
    4. ZP workspace: procedures sharing unusual ZP vars ($80+) with classified
       procedures get the same subsystem
    5. Address proximity: nearby unclassified procedures inherit from neighbors
    6. Fallback: isolated or unknown

    Args:
        procedures: dict from build_procedures()
        labels: optional {addr_hex: label_info} for seed names
        mem: optional memory accessor for HW analysis and ZP correlation

    Returns:
        dict of addr -> subsystem name (str)
    """
    if labels is None:
        labels = {}

    # Generic Atari keywords only — no game-specific terms.
    # These map common label words to subsystem categories based on
    # standard Atari hardware and programming conventions.
    _SUBSYSTEM_KEYWORDS = {
        # Sound: POKEY audio
        'sound': 'sound', 'audio': 'sound', 'pokey': 'sound',
        'music': 'sound', 'sfx': 'sound',
        # Rendering: screen buffer writes
        'render': 'render', 'rasterize': 'render', 'draw': 'render',
        'plot': 'render', 'pixel': 'render', 'sprite': 'render',
        'blit': 'render', 'fill': 'render', 'line': 'render',
        # Display: ANTIC/GTIA configuration, DLI
        'dli': 'display', 'display': 'display', 'screen': 'display',
        'color': 'display', 'charset': 'display',
        # VBI: vertical blank interrupt handlers
        'vbi': 'vbi',
        # Initialization
        'init': 'init', 'setup': 'init', 'boot': 'init',
        # Math: arithmetic routines
        'mul': 'math', 'div': 'math', 'sqrt': 'math',
        'sin': 'math', 'cos': 'math', 'atan': 'math',
        # Input: joystick, keyboard, paddle, console keys
        'joystick': 'input', 'keyboard': 'input',
        'paddle': 'input', 'consol': 'input',
        # Player-Missile Graphics
        'pmg': 'pmg', 'player': 'pmg', 'missile': 'pmg',
        # IRQ handler
        'irq': 'irq',
        # Timing: frame sync, delays
        'wait': 'timing', 'delay': 'timing', 'timer': 'timing',
        # Utilities: memory operations
        'clear': 'util', 'memzero': 'util', 'memcpy': 'util',
        'memset': 'util', 'copy_block': 'util', 'fill_block': 'util',
        # Game logic: score, entities, state
        'score': 'game_logic', 'random': 'game_logic',
    }

    subsystems = {}

    # Step 1: HW-register-based classification (fully generic)
    if mem is not None:
        for addr in procedures:
            subsys = _classify_by_hw(mem, addr)
            if subsys:
                subsystems[addr] = subsys

    # Step 2: Seed from named labels (generic keywords only)
    for addr in procedures:
        if addr in subsystems:
            continue  # HW classification takes priority
        key = f'{addr:04X}'
        if key in labels:
            info = labels[key]
            name = info.get('name', '').lower()
            if info.get('status') == 'auto' and name.startswith(('sub_', 'loc_')):
                continue  # skip auto labels for seeding
            best_match = None
            best_len = 0
            for kw, subsys in _SUBSYSTEM_KEYWORDS.items():
                if kw in name and len(kw) > best_len:
                    best_match = subsys
                    best_len = len(kw)
            if best_match:
                subsystems[addr] = best_match

        # Also seed from procedure flags
        proc = procedures[addr]
        if proc['is_interrupt'] and addr not in subsystems:
            subsystems[addr] = 'interrupt'

    # Step 3: Propagate from seeded neighbors (3 passes for convergence)
    for _ in range(3):
        for addr, proc in procedures.items():
            if addr in subsystems:
                continue
            neighbor_votes = defaultdict(float)
            for caller in proc['callers']:
                if caller in subsystems:
                    neighbor_votes[subsystems[caller]] += 1.0
            for caller in proc['tail_callers']:
                if caller in subsystems:
                    neighbor_votes[subsystems[caller]] += 1.0
            for callee in proc['calls']:
                if callee in subsystems:
                    neighbor_votes[subsystems[callee]] += 0.5
            for callee in proc['tail_calls']:
                if callee in subsystems:
                    neighbor_votes[subsystems[callee]] += 0.5

            if neighbor_votes:
                winner = max(neighbor_votes, key=neighbor_votes.get)
                total = sum(neighbor_votes.values())
                if neighbor_votes[winner] >= total * 0.5:
                    subsystems[addr] = winner

    # Step 4: ZP workspace correlation
    if mem is not None:
        zp_fingerprints = {}
        for addr in procedures:
            analysis = analyze_subroutine(mem, addr)
            zp_set = set(z for z in analysis['zp_accesses'] if z >= 0x80)
            if zp_set:
                zp_fingerprints[addr] = zp_set

        for addr in procedures:
            if addr in subsystems:
                continue
            if addr not in zp_fingerprints:
                continue
            my_zp = zp_fingerprints[addr]
            best_overlap = 0
            best_subsys = None
            for other_addr, other_zp in zp_fingerprints.items():
                if other_addr not in subsystems:
                    continue
                overlap = len(my_zp & other_zp)
                if overlap >= 3 and overlap > best_overlap:
                    best_overlap = overlap
                    best_subsys = subsystems[other_addr]
            if best_subsys:
                subsystems[addr] = best_subsys

    # Step 5: Address proximity
    sorted_addrs = sorted(procedures.keys())
    for i, addr in enumerate(sorted_addrs):
        if addr in subsystems:
            continue
        candidates = defaultdict(float)
        for j in range(max(0, i - 3), min(len(sorted_addrs), i + 4)):
            neighbor = sorted_addrs[j]
            if neighbor == addr or neighbor not in subsystems:
                continue
            distance = abs(neighbor - addr)
            if distance <= 128:
                weight = 1.0 / (1 + distance / 32.0)
                candidates[subsystems[neighbor]] += weight
        if candidates:
            winner = max(candidates, key=candidates.get)
            if candidates[winner] >= 0.5:
                subsystems[addr] = winner

    # Step 6: Fallback
    for addr in procedures:
        if addr not in subsystems:
            proc = procedures[addr]
            if proc['is_leaf'] and len(proc['callers']) + len(proc['tail_callers']) == 0:
                subsystems[addr] = 'isolated'
            else:
                subsystems[addr] = 'unknown'

    return subsystems



def suggest_name_from_graph(addr, analysis, procedures, subsystems, labels, existing_names=None):
    """Enhanced name suggestion using call graph context and behavioral patterns.

    Combines subroutine analysis (HW access, behavioral patterns) with call graph
    position (subsystem, role, caller context) for descriptive naming.

    Args:
        addr: subroutine address (int)
        analysis: dict from analyze_subroutine()
        procedures: dict from build_procedures()
        subsystems: dict from detect_subsystems()
        labels: {addr_hex: label_info}
        existing_names: set of used names

    Returns:
        Suggested name string, or None.
    """
    if existing_names is None:
        existing_names = set()

    proc = procedures.get(addr)
    ctx = call_graph_context(procedures, addr) if proc else None
    subsys = subsystems.get(addr)

    ic = analysis['instr_count']
    role = ctx['role'] if ctx else 'unknown'
    fan_in = ctx['fan_in'] if ctx else 0

    base = None

    # Priority 1: distinctive behavioral patterns (most specific)
    if analysis.get('uses_bcd') and analysis['does_math']:
        prefix = f'{subsys}_' if subsys and subsys not in ('unknown', 'isolated') else ''
        base = f'{prefix}bcd_add' if ic < 15 else f'{prefix}update_score'
    elif analysis.get('has_wait_loop'):
        if analysis.get('polls_vcount'):
            base = 'wait_vcount'
        elif analysis.get('polls_rtclok'):
            base = 'wait_frames'
        else:
            base = 'wait_sync'
    elif analysis.get('uses_indirect_jmp'):
        prefix = f'{subsys}_' if subsys and subsys not in ('unknown', 'isolated') else ''
        base = f'{prefix}dispatch'
    elif analysis.get('has_cmp_chain') and not analysis.get('has_loop'):
        prefix = f'{subsys}_' if subsys and subsys not in ('unknown', 'isolated') else ''
        base = f'{prefix}classify' if ic < 20 else f'{prefix}dispatch'
    elif analysis.get('has_copy_loop') and analysis.get('uses_indirect_indexed'):
        prefix = f'{subsys}_' if subsys and subsys not in ('unknown', 'isolated') else ''
        base = f'{prefix}copy_block'
    elif analysis.get('has_fill_loop') and analysis.get('uses_indirect_indexed'):
        prefix = f'{subsys}_' if subsys and subsys not in ('unknown', 'isolated') else ''
        base = f'{prefix}fill_block'
    elif analysis.get('has_fill_loop'):
        prefix = f'{subsys}_' if subsys and subsys not in ('unknown', 'isolated') else ''
        base = f'{prefix}fill'
    elif analysis.get('has_copy_loop'):
        prefix = f'{subsys}_' if subsys and subsys not in ('unknown', 'isolated') else ''
        base = f'{prefix}copy'
    elif analysis.get('has_search_loop'):
        prefix = f'{subsys}_' if subsys and subsys not in ('unknown', 'isolated') else ''
        base = f'{prefix}search'
    elif analysis.get('reads_random') and not analysis['modifies_sound']:
        prefix = f'{subsys}_' if subsys and subsys not in ('unknown', 'isolated') else ''
        base = f'{prefix}random_perturb' if analysis['does_math'] else f'{prefix}get_random'

    # Priority 2: specific HW access patterns
    if base is None:
        if analysis['modifies_sound'] and not analysis['modifies_colors']:
            base = 'set_sound' if ic < 20 else 'update_sound'
        elif analysis['modifies_colors'] and analysis['modifies_pmg']:
            base = 'update_pmg_colors'
        elif analysis['modifies_colors']:
            base = 'set_colors'
        elif analysis['modifies_pmg']:
            base = 'update_pmg'
        elif analysis['reads_joystick']:
            base = 'read_joystick'
        elif analysis['reads_keyboard']:
            base = 'read_keyboard'
        elif analysis['stores_vector']:
            base = 'setup_vectors'
        elif analysis['modifies_dlist']:
            base = 'setup_display'

    # Priority 3: caller context / subsystem + structural patterns
    if base is None and subsys and subsys not in ('unknown', 'isolated'):
        if analysis['modifies_screen']:
            if analysis['does_math'] and analysis['shift_count'] >= 2:
                base = f'{subsys}_render' if subsys != 'render' else 'render'
            else:
                base = f'{subsys}_draw' if subsys != 'render' else 'draw'
        elif analysis['does_math'] and analysis['shift_count'] >= 5:
            base = f'{subsys}_bit_extract' if subsys != 'math' else 'bit_extract'
        elif analysis['does_math'] and analysis['shift_count'] >= 3:
            base = f'{subsys}_calc' if subsys != 'math' else 'calc'
        elif analysis.get('uses_indexed_abs') and ic <= 10:
            base = f'{subsys}_table_lookup'
        elif role == 'utility' and fan_in >= 5:
            base = f'{subsys}_util'
        elif role == 'leaf' and ic <= 5:
            base = f'{subsys}_stub'
        elif analysis['has_loop'] and ic > 20:
            base = f'{subsys}_loop'
        elif role == 'hub':
            base = f'{subsys}_main'
        elif role == 'dispatcher':
            base = f'{subsys}_dispatch'
        elif ic <= 10:
            base = f'{subsys}_helper'
        else:
            base = f'{subsys}_proc'

    # Priority 4: structural role only (no subsystem)
    if base is None:
        if role == 'utility':
            base = 'util'
        elif analysis['modifies_screen']:
            base = 'draw'
        elif analysis['does_math'] and analysis['shift_count'] >= 5:
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

    # Clean up redundant subsystem prefixes
    base = base.replace('render_render', 'render').replace('math_calc', 'calc')
    base = base.replace('sound_set_sound', 'set_sound').replace('sound_update_sound', 'update_sound')
    base = base.replace('math_bit_extract', 'bit_extract')
    # Remove double underscores from empty prefixes
    while '__' in base:
        base = base.replace('__', '_')
    base = base.strip('_')
    # Ensure label doesn't start with a digit (invalid in MADS and most assemblers)
    if base and base[0].isdigit():
        base = 'x' + base

    name = f'{base}_{addr:04X}'
    counter = 2
    while name in existing_names:
        name = f'{base}_{addr:04X}_{counter}'
        counter += 1
    return name



def call_graph_report(procedures, subsystems, labels=None):
    """Generate text report of the call graph for LLM consumption.

    Args:
        procedures: dict from build_procedures()
        subsystems: dict from detect_subsystems()
        labels: optional {addr_hex: label_info}

    Returns:
        Multi-line text report.
    """
    if labels is None:
        labels = {}

    lines = ['=== Call Graph Report ===']
    lines.append(f'Procedures: {len(procedures)}')

    # Stats
    exits = defaultdict(int)
    for p in procedures.values():
        exits[p['exit_type']] += 1
    lines.append(f'Exit types: {dict(exits)}')

    leaves = sum(1 for p in procedures.values() if p['is_leaf'])
    interrupts = sum(1 for p in procedures.values() if p['is_interrupt'])
    flagged = sum(1 for p in procedures.values() if p['flags'])
    lines.append(f'Leaves: {leaves}, Interrupts: {interrupts}, Flagged: {flagged}')

    # Subsystem summary
    subsys_counts = defaultdict(int)
    for s in subsystems.values():
        subsys_counts[s] += 1
    lines.append(f'\nSubsystems: {dict(sorted(subsys_counts.items(), key=lambda x: -x[1]))}')

    # Most-called (hubs)
    by_fanin = sorted(procedures.values(), key=lambda p: len(p['callers']) + len(p['tail_callers']), reverse=True)
    lines.append('\nTop procedures by caller count:')
    for p in by_fanin[:15]:
        entry = p['entry']
        key = f'{entry:04X}'
        name = labels.get(key, {}).get('name', f'${entry:04X}') if isinstance(labels.get(key), dict) else f'${entry:04X}'
        fanin = len(p['callers']) + len(p['tail_callers'])
        fanout = len(p['calls']) + len(p['tail_calls'])
        subsys = subsystems.get(entry, '?')
        leaf = ' LEAF' if p['is_leaf'] else ''
        flags = f' [{",".join(p["flags"])}]' if p['flags'] else ''
        lines.append(f'  {name:30s} in={fanin:2d} out={fanout:2d} {p["exit_type"]:12s} {subsys:10s}{leaf}{flags}')

    # Flagged procedures (anomalies)
    if flagged:
        lines.append(f'\nAnomalous procedures ({flagged}):')
        for addr in sorted(procedures.keys()):
            p = procedures[addr]
            if p['flags']:
                key = f'{addr:04X}'
                name = labels.get(key, {}).get('name', f'${addr:04X}') if isinstance(labels.get(key), dict) else f'${addr:04X}'
                lines.append(f'  {name:30s} flags: {", ".join(p["flags"])}')

    return '\n'.join(lines)


