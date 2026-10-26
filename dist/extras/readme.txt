sampledevices/

    Contains sample device descriptions to use with the Custom Device feature
    of the emulator, in System > Configure System > Devices > Add > Custom
    Device. The .atdevice file specification is in the help file.

deviceserver/

    Base implementation of the TCP device server for handling custom requests
    from a custom device, and sample device servers for printer and R-Verter
    emulation (paired with the corresponding .atdevices).

    Python 3 is required to run the scripts.

Unlike Altirra itself, these files are licensed under the zlib/libpng license
instead of the GNU General Public License (GPL).

Antonia 2 multiply/divide registers

    sampledevices/antonia2-muldiv.atdevice is the upstream Altirra example,
    providing unsigned 16-bit division (16-bit quotient and remainder) and
    16x16-bit multiplication (32-bit product) at $FF:F008-$FF:F00F.

    AltirraSDL provides a built-in device:
    System > Configure System > Devices > Add Device... > Internal devices >
    Antonia 2 MULDIV block. Select a 65C816 CPU on the Computer > CPU page.
    The device is saved per profile; no .atdevice file is required.
    Profiles using the former CPU checkbox are migrated automatically.
    The sample descriptor is retained as an upstream reference only.
    Remove an older Antonia 2 Custom Device entry if one was added before.

    Every operand-byte write recalculates its result. Division by zero
    returns quotient $FFFF and the dividend as remainder. Results are read
    through the same addresses; operands and readback are independent.
    This models the arithmetic registers, not the complete accelerator board.
