"""analyzer.hw — hw analysis functions."""

from collections import defaultdict

from altirra_bridge._analyzer_tables import (
    ADDR_TO_NAME,
    HW_READ,
    HW_WRITE,
)

# ── Direction classification ──

_READ_MNEMONICS = {'LDA', 'LDX', 'LDY', 'CMP', 'CPX', 'CPY', 'BIT',
                   'ORA', 'AND', 'EOR', 'ADC', 'SBC'}
_WRITE_MNEMONICS = {'STA', 'STX', 'STY'}
_RMW_MNEMONICS = {'INC', 'DEC', 'ASL', 'LSR', 'ROL', 'ROR'}



def classify_hw_access(mnemonic):
    """Classify a mnemonic's memory access direction.

    Returns:
        'read', 'write', 'rmw', or None for non-memory instructions.
    """
    m = mnemonic.upper()
    if m in _READ_MNEMONICS:
        return 'read'
    if m in _WRITE_MNEMONICS:
        return 'write'
    if m in _RMW_MNEMONICS:
        return 'rmw'
    return None



def decode_portb(value, scheme='rambo'):
    """Decode a PORTB value into human-readable text.

    Args:
        value: PORTB byte value (0-255)
        scheme: banking scheme ('rambo', '130xe')

    Returns:
        Multi-line text explaining the PORTB value.
    """
    bits = f'{value:08b}'
    lines = [f'PORTB ${value:02X} = {bits}']

    # Bit 0: OS ROM
    rom_enabled = bool(value & 0x01)
    lines.append(f'  Bit 0:    {value & 1}     → OS ROM {"enabled" if rom_enabled else "DISABLED (RAM at $C000-$CFFF, $D800-$FFFF)"}')

    # Bit 1: BASIC
    basic_disabled = bool(value & 0x02)
    lines.append(f'  Bit 1:    {(value >> 1) & 1}     → BASIC {"disabled" if basic_disabled else "ENABLED ($A000-$BFFF)"}')

    # Bits 2-5: bank selection
    bank_bits = (value >> 2) & 0x0F
    if scheme == 'rambo':
        if bank_bits == 0x0F:
            bank_desc = 'main memory (no bank)'
        else:
            bank_desc = f'bank {bank_bits}'
        lines.append(f'  Bits 5-2: {bank_bits:04b}  → {bank_desc} (RAMBO)')
    elif scheme == '130xe':
        xe_bank = (value >> 2) & 0x03
        cpu_antic = 'CPU+ANTIC' if not (value & 0x10) else 'CPU only'
        if xe_bank == 3 and (value & 0x10):
            bank_desc = 'main memory (no bank)'
        else:
            bank_desc = f'bank {xe_bank} ({cpu_antic})'
        lines.append(f'  Bits 3-2: {xe_bank:02b}    → {bank_desc} (130XE)')
        lines.append(f'  Bit 4:    {(value >> 4) & 1}     → {"CPU only" if value & 0x10 else "CPU+ANTIC"} bank access')
    else:
        lines.append(f'  Bits 5-2: {bank_bits:04b}  → bank bits = {bank_bits}')

    # Bits 6-7
    lines.append(f'  Bits 7-6: {(value >> 6) & 3:02b}    (unused)')

    return '\n'.join(lines)



def build_hw_tables():
    """Build HW_READ and HW_WRITE dicts from constants.py.

    Returns the pre-built tables from constants.py. This function exists
    for API completeness — the tables are already statically defined.

    Returns:
        (HW_READ, HW_WRITE) tuple of dicts mapping address -> register name.
    """
    return dict(HW_READ), dict(HW_WRITE)



def auto_label(addr, label_type, existing_labels=None, bank=None):
    """Generate an automatic label name for an address.

    Args:
        addr: address (int)
        label_type: one of 'subroutine', 'jump_target', 'data', 'vector',
                    'entry_point', 'branch_local', 'variable', etc.
        existing_labels: dict to check for uniqueness
        bank: bank qualifier (int or 'main' or None)

    Returns:
        Label name string (e.g. 'sub_3980', 'loc_3A40')
    """
    prefix_map = {
        'subroutine': 'sub',
        'jump_target': 'loc',
        'data': 'dat',
        'entry_point': 'entry',
        'vector': 'vec',
        'dli': 'dli',
        'irq': 'irq',
        'variable': 'var',
        'branch_local': 'loc',
        'string': 'str',
        'charset': 'chr',
        'screen': 'scr',
        'display_list': 'dl',
        'pm_data': 'pm',
        'table': 'tbl',
    }
    prefix = prefix_map.get(label_type, 'unk')
    suffix = f'_{addr:04X}'
    if bank is not None:
        suffix += f'_b{bank}'
    name = f'{prefix}{suffix}'

    # Ensure uniqueness — existing_labels can be a set of names or dict (addr->name)
    if existing_labels:
        check = existing_labels if isinstance(existing_labels, set) else set(existing_labels.values())
        counter = 1
        base = name
        while name in check:
            name = f'{base}_{counter}'
            counter += 1

    return name



