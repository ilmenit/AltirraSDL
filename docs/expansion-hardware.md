# RAM cartridges and 80-column/RAM expansions

These implementations use the hardware references below in addition to
Atari800's source. Hardware-specific differences from Atari800 are intentional.
The normal regression suite uses synthetic firmware and does not redistribute
commercial ROM images.

## RAM-Cart and SiDiCar

CART mapper IDs 94–102 cover RAM-Cart 64KB, 128KB, Double RAM-Cart
256KB, and 1/2/4/8/16/32MB variants. ID 103 is SiDiCar 32KB.
Register encodings and physical switches follow
[Atari800's cartridge specification](https://github.com/atari800/atari800/blob/master/DOC/cart.txt)
and its `src/cartridge.c` implementation. RAM-Cart starts read-only with
its physical switches clear; users can change these under **System →
Console Switches → RAM-Cart switches**. Physical defaults persist per
profile in the Hardware category. Snapshot restoration also restores the
latched registers and complete mutable cartridge image. Use the normal
cartridge Save command to save modified battery-backed memory as a CART
or raw image.

SiDiCar's original designer describes four independent 8KB banks in
[Tajemnice Atari 8/92, “Dysk krzemowy”](https://tajemnice.atari8.info/8_92/8_92_dyskkrzemowy.html).
The selected bank occupies `$8000–$9FFF`; the low two control bits drive
RAM address bits A13/A14. Consequently the bank offset is `bank << 13`.
Atari800's `Calculate_SiDiCar_Address()` instead shifts by 11, which
aliases adjacent banks. AltirraSDL follows the hardware description and
checks both ends of all four banks in its regression test. Writes with
control bit 7 set do not change the latch; the simplified `/CCTL` decoder
mirrors control writes throughout `$D500–$D5FF`.

The banked debugger namespace contains 256 bank numbers. RAM-Cart exposes
consecutive physical 16KB banks across `$8000–$BFFF`, up to that limit;
SiDiCar exposes its four 8KB banks only at `$8000–$9FFF`. Linear cartridge
memory access covers the complete image, including RAM-Carts larger than
4MB.

## Mosaic 64K Select

The primary reference is the
[Mosaic 64K Select owner's manual, including updates and insert](https://archive.org/details/mosaic-64-k-select-ram-owners-manual-with-updates-and-insert),
especially pages 12–16 and 31–35. The implementation models the
**800-strapped bank-select wiring**. Configure the computer for 48KB of
base RAM. The device adds 4KB banks at `$C000–$CFFF`; its settings offer
64–192KB total memory in 16KB increments, corresponding to 4–36 banks.

Writing `$FFC0 + bank` selects a bank; the written value is irrelevant.
An uninstalled bank deselects expansion RAM. The decoder also observes
the rest of the `$FFxx` page, where writes deselect RAM; reads continue
to read the underlying OS. This decode behavior is also described in
Michael Current's [Atari 8-bit FAQ](https://mcurrent.name/atari-8-bit/faq.txt).
The manual describes 52KB of contiguous memory at power-on, so bank 0
starts mapped. Warm reset preserves the selection; cold reset clears
expansion RAM and selects bank 0. Snapshots preserve every bank and the
current selection.

The manual's separate **1200-strapped modification**, including the PIA
wire and OS-under-RAM switching used to simulate a 1200XL, is not
implemented by this device. It must not be inferred from the presence of
the Mosaic bank-select expansion.

## Austin Franklin 80

The primary hardware investigation and original ROM/manual dumps are
[warerat's “Austin Franklin 80 information” posts](https://forums.atariage.com/topic/154535-austin-franklin-80-information/).
The board provides 80×25 text with 8×10 character cells, RGBI attributes,
2KB character RAM, 2KB attribute RAM, and a banked 4KB right-cartridge
ROM. A separate 4KB character ROM is required. Select the two AF80
firmware types in Firmware Manager and add the device through System
Configuration's Devices page. Its output uses the same alternate-video
routing as XEP80 and Bit 3.

Two hardware details differ from Atari800's implementation:

- Cartridge ROM reads exclude `$D5F0–$D5F7`; writes to the control
  registers at `$D5F6/$D5F7` still work. The dumped decoder PROM confirms
  this read hole, including the clock-only decode at `$D5F7`.
- `$D5F6` bit 7 selects 80-column output when **set** and standard
  40-column pass-through when **clear**. Atari800's inverted output flag
  must not be copied literally.

On October 21, 2026, a Linux release build was also tested with the
original 2009 AF80 ROM dumps from that thread, Atari OS-B NTSC, 48KB RAM,
and a left-slot Atari BASIC rev. C cartridge. After 600 frames the real
firmware initialized the CRTC and wrote `READY` into AF80 display RAM.
The serialized device state contained control `$B0` and ROM control
`$28`, independently confirming active-high bit 7. Without a left-slot
BASIC cartridge, the same setup initialized the AF80 memo-pad display.
These external ROMs were used only in temporary local test directories.

The synthetic regression target additionally checks mapping gates, ROM
and VRAM banks, mirrors, firmware status, glyph/attribute rendering,
output switching, and serialized snapshots. Real-ROM boot testing
complements those checks; it does not establish compatibility with every
AF80 application.
