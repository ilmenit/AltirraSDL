# River Raid — Display List Architecture

## Display List Location

The display list lives at $3F00 and occupies 182 bytes ($3F00-$3FB5).
It is built during cold start at $A05D-$A06F by copying a template
from $B776.

## Structure

```
$3F00: $70        Blank 8 lines
$3F01: $F0        Blank 8 lines + DLI (triggers DLI at top of visible area)
$3F02: $30        Blank 4 lines

$3F03: $4E,$80,$3D  Mode 14 + LMS → $3D80 (status bar, first line)
$3F06-$3F0B: 6× $0E  Mode 14 (status bar lines 2-7, contiguous)

$3F0C: $4E,$00,$20  Mode 14 + LMS → $2000 (main playfield start)
$3F0F-$3FB4: 163× $0E  Mode 14 (main playfield scanlines, contiguous)

$3FB5: $41,$00,$3F  JVB → $3F00 (jump and wait for vertical blank)
```

Total visible lines: 7 (status bar) + 1 + 163 (playfield) = ~171 mode 14 lines.

## ANTIC Mode 14

- Resolution: 160 pixels wide × 1 scanline per mode line
- Colors: 4 from registers COLPF0-3 (2 bits per pixel)
- DMA: 40 bytes per line (48 bytes with wide playfield)
- DMACTL ($D400) = $3F: wide playfield + PM DMA + normal DMA

Each mode 14 line is one scanline tall, so 171 mode lines = 171 scanlines
of bitmap graphics. This is the highest color-resolution bitmap mode on
the Atari (equivalent to "Graphics 15" in BASIC).

## Screen RAM Layout

### Status Bar ($3D80-$3EFF)
- 9 scan lines × ~48 bytes = ~432 bytes
- Contains pre-rendered: player indicator, GAME number, fuel gauge,
  score digits, HI score, BRIDGE counter, lives icons, ACTIVISION logo
- Updated by the main loop when score/fuel/lives change

### Main Playfield ($2000-$3C7F)
- ~163 scan lines × ~48 bytes ≈ 7824 bytes
- Contains the river terrain bitmap — river water, banks, terrain
- Scrolled by shifting data upward and writing new rows at the bottom
- The terrain scroll routine at $AFD0 / $B00F copies new row data

## DLI Chain

A single DLI fires at $3F01 (the second blank line, before the first
visible mode 14 line). This DLI at $B500:

1. Saves registers (PHA/TXA/PHA/TYA/PHA)
2. Reads VCOUNT to determine which zone we're in
3. If VCOUNT < $3F: runs the **main playfield kernel** ($B510)
4. If VCOUNT >= $3F: runs the **status bar handler** ($B72A)

The DLI does NOT chain to a second DLI — instead, the playfield kernel
at $B510 uses a WSYNC-locked loop ($B567) that occupies every scanline
from top to bottom of the playfield. This is effectively a
"kernel-mode" renderer where the DLI runs for the entire visible frame.

The status bar handler at $B72A-$B773 sets up player positions for the
score area and then exits via JMP XITVBV ($E462).

## Display Timing

| Region | Scanlines | VCOUNT range | Handler |
|--------|-----------|-------------|---------|
| Top blank | 24 | $00-$0B | — |
| Status bar | 7-9 | $0C-$10 | DLI: status bar setup |
| Playfield | ~163 | $10-$78 | DLI: sprite kernel |
| Bottom blank | ~20 | $78-$83 | VBI fires here |

The main loop waits at $AE99 for VCOUNT < $50 (before the playfield
starts), ensuring all game logic runs during the vertical blank and
top-of-screen period. The DLI kernel then takes over for the visible
frame.

## Color Usage

The playfield colors (COLPF0-3) are set per the terrain type:
- COLPF0: River water (blue shades)
- COLPF1: River bank shadow
- COLPF2: Terrain / ground (brown/green)
- COLPF3: Terrain detail

Colors are changed by the DLI kernel per scanline zone, allowing
different terrain colors for different sections of the river. The
color table at $B48D provides per-entity-type color values.

Player/missile colors are set per-scanline by the DLI to multiplex
entity sprites with different colors (enemy jets are different colors
from helicopters, etc.).
