# River Raid — Terrain & Scrolling System

## Scrolling Mechanism

River Raid uses **coarse scrolling** — the terrain bitmap is physically
shifted in RAM rather than using ANTIC's hardware scroll registers
(HSCROL/VSCROL are not used). Each frame:

1. The scroll position advances based on speed ($19 joystick up/down)
2. Screen RAM at $2000+ is shifted upward by one row (48 bytes)
3. A new terrain row is written at the bottom

The scroll rate is variable:
- Joystick up: scroll faster (2+ pixels per frame)
- Neutral: medium scroll (1 pixel per frame)
- Joystick down: slow scroll (< 1 pixel per frame, using a sub-pixel counter)

## Terrain Data Flow

```
Terrain PRNG ($A5/$A6)
    ↓
River bank calculation ($A652)
    ↓
Bank positions stored in $0C-$15 (10 rows)
    ↓
Row renderer ($AFF4 / $B00F)
    ↓
Screen RAM at $2000+ (mode 14 bitmap)
```

## River Bank Positions ($0C-$15)

The array at $0C-$15 holds the left bank X position for each of the
10 most recently generated terrain rows. These values scroll upward
as the terrain advances:

- $0C = topmost visible row's left bank X
- $15 = bottommost (newest) row's left bank X
- The right bank is computed as left_bank + river_width

The DLI kernel reads these values to determine where the river ends
and terrain begins for each scanline zone, enabling per-scanline
terrain coloring and collision detection.

## Terrain Generation ($A5B7-$A605)

The terrain generator at $A5B7 produces new rows procedurally:

1. Read PRNG state from $A5/$A6
2. Advance PRNG (simple shift-and-add algorithm)
3. Compute new left bank position: old_position + random_delta
4. Clamp to valid range (prevent river from going off-screen)
5. Compute river width based on section type ($5A)
6. Store into $15 (bottom row of the bank position array)
7. Shift array up: $14→$13→...→$0C

### Section Types ($5A)

| Value | Type | Effect |
|-------|------|--------|
| 1 | Normal | Standard river width, moderate winding |
| 2 | Narrow | Reduced river width, tighter turns |
| 3+ | Complex | Multiple channels, islands, extreme narrowing |

Section type changes at bridge boundaries. Each bridge crossing
may increase the section complexity.

## Row Rendering

### Terrain Row ($AFF4 / fill_terrain_pattern)
Writes a row of terrain data from ROM template to screen RAM:

```
$AFF4: LDA $4F               ; check draw mode
$AFF6: BNE $AFEC             ; alternate pattern
$AFF8: LDA #$55              ; default: $55 = alternating pixels (terrain pattern)
$AFFA: STY $B6               ; save Y
$AFFC: DEY
$AFFD: BMI $B009             ; if Y < 0: done
$AFFF: STA ($A3),Y           ; write pattern byte to screen RAM
$B001: DEY
$B002: BPL $AFFF             ; loop for all bytes in row
```

The pattern byte $55 = 01010101 in binary = alternating color 1
pixels, which produces the terrain's solid brown/green appearance
in mode 14 (2 bits per pixel).

### River Water
River areas are filled with $00 (background color = water blue) or
with specific water patterns from the shape tables at $B800+.

### Copy Row Data ($B00F)
Copies Y+1 bytes from source ($A1/$A2) to destination ($A3/$A4):

```
$B00F: TYA
$B010: BMI $B019             ; if Y negative: skip (empty row)
$B012: LDA ($A1),Y           ; load from source
$B014: STA ($A3),Y           ; store to screen RAM
$B016: DEY
$B017: BPL $B012             ; loop until Y < 0
$B019: RTS
```

This is the core blit routine used for both terrain rows and
entity graphics into the bitmap.

## Screen RAM Organization

The mode 14 bitmap at $2000-$3C7F is organized as contiguous
rows of 48 bytes each (40 visible + 8 for wide playfield DMA):

| Address | Row | Content |
|---------|-----|---------|
| $2000 | 0 (top of playfield) | First visible terrain row |
| $2030 | 1 | Second row |
| $2060 | 2 | Third row |
| ... | ... | ... |
| $3C50 | ~163 (bottom) | Last visible terrain row |

When the terrain scrolls, data is shifted upward:
- Rows 1-163 are moved to rows 0-162
- Row 163 is filled with newly generated terrain

The shift is done by the frame update routine at $AFD0, which
adjusts the display list's LMS pointers rather than physically
moving all the data (a clever optimization — instead of copying
~8KB, just change two bytes in the display list).

## Terrain Colors

The playfield colors (COLPF0-3) are set by the DLI kernel per
scanline zone to create the illusion of varied terrain:

- River water: blue shades (COLPF0 = $94 typical)
- River bank edge: darker green/brown
- Terrain: brown/olive (COLPF2)
- Terrain detail/trees: darker shade

The color table at $B48D provides per-entity-zone color values
that the DLI loads and writes to the GTIA color registers each
scanline. This creates the characteristic banded color appearance
of the Atari 8-bit River Raid.
