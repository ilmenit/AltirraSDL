# Display Architecture and Scrolling

## Display Modes

### Title Screen ($6800)
- 3x BLANK 8 lines
- MODE 7 (ANTIC $07) with LMS for "WIZARD" title
- BLANK lines separating text
- MODE 6 lines for "HIGH SCORE", "LAST SCORE", "SELECT LEVEL", "AND PRESS FIRE"
- JVB back to $0000 (intentional? or should be $6800)

### Gameplay Screen ($6D00)
- 3x BLANK 8 lines (top border)
- 11x MODE 7 lines with explicit LMS addresses (scrollable playfield):
  $8A2D, $8AAD, $8B2D, $8BAD, $8C2D, $8CAD, $8D2D, $8DAD, $8E2D, $8EAD, $8F2D
  - Each line 128 ($80) bytes apart for smooth horizontal scrolling
  - DLI flag on line 4 ($6D0C) for color split at status bar
- BLANK 5 lines (gap before status)
- 4x MODE 6 lines (status bar at $6D80):
  - Line 1: "FOUL   xxx  xxxx" (net monster count 3d, monster movement total 4d)
  - Line 2: "POWER  xxxx"
  - Line 3: "CASTLE xx xx xx"
  - Line 4: "SCORE  xxxxxxxx"
- JVB -> $6D00

### Map View ($5000)
- 3x BLANK 8 lines
- ~60x MODE $0D lines (ANTIC mode D = 160x2 4-color graphics)
  - LMS = $A000 (map overview bitmap)
  - Shows entire map at reduced resolution

## Scrolling Engine ($1900)

### Hardware Scroll Mechanism
ANTIC mode 7 supports fine scrolling via HSCROL and VSCROL registers:
- HSCROL: 0-7 (3 bits), each step = 1 color clock = 2 hi-res pixels
- VSCROL: 0-15 (4 bits), each step = 1 scanline

### Scroll Variables
| Variable  | Address | Purpose                                    |
|-----------|---------|--------------------------------------------|
| hscrol    | $6C03   | Current HSCROL value (0-7)                 |
| vscrol    | $6C04   | Current VSCROL value (0-15)                |
| map_x     | $5110   | Coarse map X position                      |
| map_y     | $5111   | Coarse map Y position                      |
| map_col   | $5109   | Map column (for entity targeting)          |
| map_row   | $510A   | Map row (for entity targeting)             |
| lock_h    | $6C1A   | Horizontal scroll lock (at edge)           |
| lock_v    | $6C1B   | Vertical scroll lock (at edge)             |
| view_y    | $5102   | View Y offset                              |
| view_x    | $5103   | View X offset                              |
| base_ptr  | $BB/$BC | Screen memory base for tile lookups        |

### Scroll Direction Processing ($1900)
```
$6C00 holds joystick direction bits (from STICK0 XOR $0F):
  Bit 0 = UP:    JSR $192D (scroll_up) or $1800 (edge_up)
  Bit 1 = DOWN:  JSR $1966 (scroll_down) or $1828 (edge_down)
  Bit 2 = LEFT:  JSR $1997 (scroll_left) or $1847 (edge_left)
  Bit 3 = RIGHT: JSR $19C5 (scroll_right) or $186B (edge_right)

After all 4 bits processed:
  STA HSCROL from $6C03
  STA VSCROL from $6C04
```

### Coarse Scroll (LMS Update)
When HSCROL/VSCROL overflow their range, the engine does a "coarse scroll":

**Vertical coarse** ($19F8 / $1A1D):
- Iterates through all 11 LMS addresses at $6D04, $6D07, $6D0A, ... (+3 each)
- Subtracts or adds $6B00[Y] from each LMS address
  . $6B00[0] = $80 (128 bytes = one MODE 7 line)
  . $6B00[1] = $01 (1 byte = one column)
- Also adjusts screen base pointer $BB/$BC

**Boundary limits:**
- $5100 (Y tile): clamps to $10-$80 range
- $5101 (X tile): clamps to $38-$C0 range
- $5110 (map X): clamps to $6A maximum
- $5111 (map Y): clamps to $59 maximum
- Lock flags $6C1A/$6C1B prevent scrolling past edges

### Map Size
From boundary limits:
- X range: $38 to $C0 = 136 tile positions
- Y range: $10 to $80 = 112 tile positions
- Each tile = 8 pixels wide, 16 pixels tall (ANTIC mode 7)
- Map size: ~136 x 112 tiles

## DLI Handler ($6880)

Simple color bar handler for the status area:
```
PHA / TXA / PHA           ; save A, X
LDX #$03
loop:
  STA WSYNC               ; wait for horizontal sync
  LDA $6B7C,X             ; load color from table
  STA COLPF0,X            ; write to playfield color register
  DEX
  BPL loop
PLA / TAX / PLA / RTI     ; restore and return
```

This sets 4 playfield colors ($6B7C = COLPF0-3) at the DLI point (line 4
of the playfield), creating a color split between the game area and status bar.

## PMG Layout

PMBASE = $10, so PMG area is $1000-$17FF:
- $1000-$12FF: unused (PMBASE offset for player data)
- $1300-$13FF: Missiles (4x 2-bit wide objects)
  - Used for cursor/wand targeting indicators
  - $1322+offset: missile data at player Y position ($6C20)
- $1400-$14FF: Player 0 = wizard character
  - 8 bytes written at offset $1420+Y_position
  - Animated 4 frames from pointers at $6B28 (frame data at $59xx)
- $1500-$15FF: Player 1 (not clearly used in analyzed code)
- $1600-$16FF: Player 2 = special V-shield trigger object
- $1700-$17FF: Player 3 = wand/cursor
  - 15 bytes written at offset $1720+$5100
  - 8 directional shapes from $6B02 pointer table

### Character Set Animation
The game uses multiple character set pages for tile animation:
- $511E starts at one CHBASE value
- Every $511F frames: CHBASE += 2 (next variant)
- When CHBASE reaches $50: wrap to $519E
- Also cycles COLOR2 += $10 for color shimmer effect
- Tiles that have different graphics across font pages will appear animated
  (exact animated tiles not yet catalogued)

## Status Bar Layout ($6D80)

The score_display routine ($5700) converts BCD values to screen characters
by extracting nibbles and adding $50 (digit 0 character code):

```
Line 0 "FOUL":
  $6D8B-$6D8D: foul_hi/lo (3 BCD digits, net monster count, $6C29-$6C2A)
  $6D8F-$6D92: foul_damage_hi/lo (4 BCD digits, monster movement total, $6CC7-$6CC8)
Line 1 "POWER":
  $6D9F-$6DA2: power (4 BCD digits from $511B-$511C)
Line 2 "CASTLE":
  $6DB3-$6DBA: castle powers (3x2 digits from $5127-$5129)
Line 3 "SCORE":
  $6DC7-$6DCC: score_total (6 BCD digits, inverse $D0+, player score, $6CC0-$6CC2)
  $6DCE-$6DCF: static "45" (never changes, purpose unknown)
```
