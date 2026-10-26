# Antonia 2 arithmetic registers

AltirraSDL has a built-in arithmetic block with the register behavior of
upstream Altirra's Antonia 2 model. It is optional and does not change
CPU speed or RAM configuration.

## Enable

1. Open **System > Configure System > Computer > CPU**.
2. Select a **65C816** CPU mode and the desired speed.
3. Open **Devices > Add Device... > Internal devices** and select
   **Antonia 2 MULDIV block**.

No descriptor file, Custom Device entry or unsafe-operation permission is
needed. Adding or removing the device maps/unmaps the block immediately.
It is saved per profile in the Devices category, like other hardware
extensions, and is absent by default. To disable it, select the block in
the Devices list and click **Remove**. If a legacy Antonia 2 Custom Device
was previously added, remove that entry before adding the built-in device
so only one device owns this address range.

Profiles saved with the former CPU checkbox are migrated automatically to
the device list. The old CPU setting is removed, so removing the device
will keep it disabled after a restart. Where Hardware and Devices had
different inheritance, migration preserves the effective device list by
giving the affected profile its own Devices category when necessary.

The 65C816 can address the registers in emulation or native mode using
long addresses. Linear high RAM is a separate setting. Switching to a
6502 does not make the high-bank registers accessible.

| Address | Write | Read |
|---|---|---|
| `$FF:F008-F009` | unsigned 16-bit dividend | unsigned 16-bit quotient |
| `$FF:F00A-F00B` | unsigned 16-bit divisor | unsigned 16-bit remainder |
| `$FF:F00C-F00D` | unsigned 16-bit factor A | product bits 0-15 |
| `$FF:F00E-F00F` | unsigned 16-bit factor B | product bits 16-31 |

All fields are little-endian. Every byte write recomputes that block using
the current operands; there is no START or BUSY register. Division by zero
returns quotient `$FFFF` and the dividend as remainder. Reads do not alter
operands. Multiplication retains the full unsigned 32-bit result.

Cold initialization sets both operand and readback banks to `$FF`, without
calculating a result until a write. Warm reset preserves them. Other
addresses in the page are not claimed. This is the upstream arithmetic
register model, not complete emulation of the Antonia 2 accelerator board.

## Regression coverage

The CTest `bridge-antonia2` test (with `ALTIRRA_BUILD_TESTS=ON`) enables the
built-in device using a private profile and runs without a display.
It executes real 65C816 long loads/stores, testing byte-by-byte recalculation,
unsigned edge cases and fixed-seed operand pairs, native 16-bit transfers,
readback/operand independence, bank isolation, resets, and save-state
restoration of both operands and results. The INI fixture is POSIX-only;
CTest skips it on Windows rather than touching the real Windows registry.

`tests/ui/test_antonia2.py` exercises device catalog placement, add/remove
behavior, independent profiles, restart persistence, and migration of the
old CPU setting, including inherited profiles.

To run the arithmetic regression directly:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 src/AltirraSDL/source/bridge/tests/bridge_antonia2_test.py \
  /path/to/AltirraSDL \
  src/AltirraSDL/AltirraBridge/sdk/python --builtin
```

The original upstream `dist/extras/sampledevices/antonia2-muldiv.atdevice`
is retained as a reference oracle, not as a dependency of the built-in device.
Passing its path instead of `--builtin` runs the same arithmetic bus vectors
against that original model. The sample retains its upstream zlib/libpng
licensing; the built-in fork code is GPL-2.0-or-later.

The built-in device supports simulator save states; its transient operands
and results are not stored in the profile. Enabling it initializes both
banks to `$FF`; disabling removes its memory layer completely.
As with other device save states, enable the block before loading a state
that uses it; loading a state does not add a disabled device.
