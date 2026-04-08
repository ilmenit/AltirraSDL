# Display Architecture

## Screen Layout

The display is composed of three regions:

```
Scanlines 0-30:    Blank lines (overscan padding)
Scanlines 31-158:  3D viewport (128 lines of ANTIC mode F)
Scanlines 159-164: Blank (separator)
Scanlines 165-172: Status bar line 1 (ANTIC mode 2 text)
Scanlines 173-174: Blank
Scanlines 175-182: Status bar line 2 (ANTIC mode 2 text)
Scanlines 183-247: Vertical blank
```

## ANTIC Mode F + GTIA Nibble Graphics

The 3D viewport uses ANTIC mode F (160 color clocks, 40 bytes/line),
but GTIA interprets it in **nibble mode** — each 4-bit nibble is one
pixel, giving **80 pixels horizontal resolution** per scanline.

The viewport is therefore **80×128 pixels** (80 wide, 128 tall).
Each byte contains 2 pixels: high nibble = left, low nibble = right.
The nibble value (0-15) selects a color or luminance depending on
which GTIA mode is active on that scanline.

The PRIOR register toggles between two GTIA modes via the IRQ chain:

- **$E1** (bits 7+6 = $C0) → **GTIA mode 3**: each nibble selects one
  of 16 **hues**; luminance comes from COLBK
- **$61** (bit 6 = $40) → **GTIA mode 1**: each nibble selects one
  of 16 **luminance levels**; hue comes from COLBK

By alternating these modes on consecutive scanline pairs, the engine
displays both hue and luminance information from the same nibble data.
The human eye blends adjacent scanlines, perceiving a richer palette
than either mode alone could produce. This is sometimes called
"interlaced GTIA" or "GTIA blending".

Both PRIOR values also set bit 5 ($20 = multi-color players) and
bit 0 ($01 = PMG priority).

## Framebuffer Layout

Screen RAM lives at $D800-$F7FF (RAM under OS ROM, 8192 bytes).
Each scanline occupies 64 bytes ($40) despite only 40 being displayed:

```
Buffer 0 (LMS base $D800):
  Line  0: $D800-$D827 (40 bytes displayed, 24 padding)
  Line  1: $D840-$D867
  Line  2: $D880-$D8A7
  ...
  Line 127: $F7C0-$F7E7

Buffer 1 (LMS base $D820):
  Line  0: $D820-$D847
  Line  1: $D860-$D887
  Line  2: $D8A0-$D8C7
  ...
  Line 127: $F7E0-$F807
```

The 64-byte stride allows fast address calculation:
column_addr = base + (scanline << 6) + column_byte

The two buffers are interleaved with a $20 (32 byte) offset.
Since each line only uses 40 bytes and the stride is 64, there
are 24 bytes of padding — the second buffer starts at byte 32,
sharing the same 8KB memory region without overlap.

## Display List Structure

Each display list is ~400 bytes and looks like:

```
$9800: 70 70 70 30 30 60 80     ; blank lines (8+8+8+4+4+7+1 = 40 scanlines)
$9807: 01 0A 98                 ; JMP $980A (or $9A0A for buffer 1)
$980A: 4F 00 D8                 ; MODE F + LMS $D800
$980D: 4F 40 D8                 ; MODE F + LMS $D840
$9810: 4F 80 D8                 ; MODE F + LMS $D880
...                              ; 128 lines total
$998A: 60                       ; BLANK 7 lines
$998B: 47 95 9B                 ; MODE 2 + LMS $9B95 (status line 1)
$998E: 10                       ; BLANK 2 lines
$998F: 47 B5 9B                 ; MODE 2 + LMS $9BB5 (status line 2)
$9992: 41 00 98                 ; JVB (wait for VBI, jump to $9800)
```

Every mode F line has its own LMS address (no automatic address
increment). This is necessary because the framebuffer stride (64)
doesn't match what ANTIC expects (40). The LMS overhead costs
~2 DMA cycles per line but enables the interleaved double-buffer.

## Status Bar

Two ANTIC mode 2 (40-column text) lines at $9B95 and $9BB5:

```
Line 1: "FPS:24  ?:0  $:000  [21,16]  R3"
         │       │    │      │         └─ Room/region
         │       │    │      └─ [X,Y] coordinates (hex)
         │       │    └─ Score
         │       └─ ? counter
         └─ Frames per second

Line 2: " P-M 2.0 (PAL)          NRV 2010"
```

Character set at $F800 (CHBASE=$F8) — custom font stored in RAM
under the OS ROM, copied there by the INIT routine.

## Colors

### Viewport (set by IRQ handlers):
- COLPF1 = $02 (dark, used in GTIA mode 2/3 rendering)
- COLPF2 = $EC (light green/yellow — wall base color)
- COLBK  = $00 (black background)
- PRIOR alternates between $E1/$61

### Status bar (set by IRQ stage 4):
- COLPF1 = $0E (white text)
- COLPF2 = $34 (dark blue background)

### Player-Missile (set during init):
- COLPM0 = $F6 (white, player 0 — crosshair/gun left)
- COLPM1 = $FA (white, player 1 — crosshair/gun right)
- COLPM2 = $80 (grey, player 2 — minimap overlay, quad width)
- COLPM3 = set dynamically by VCOUNT reads (raster effect)
- HPOSP0 = HPOSP1 = $78 (horizontal center — crosshair)
- SIZEP0 = SIZEP1 = $01 (double width)
- SIZEP2 = $03 (quad width for minimap)

### Minimap overlay
Player 2 at quad width ($0A=3) is used for the minimap.
Toggle with the FIRE button (bit $83). Cycles through:
- Mode 0: hidden (HPOSP2=0, HPOSM3=0)
- Mode 1: minimap visible (HPOSP2=$9D)
- Mode 2: different position (HPOSM3=$C1)
