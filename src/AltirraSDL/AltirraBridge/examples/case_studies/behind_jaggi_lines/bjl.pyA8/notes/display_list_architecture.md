# Display List Architecture

## Gameplay Display List ($316B)

### Structure
```
$316B: 3x BLANK 8            24 blank scanlines (top overscan)
$316E: Mode 6+LMS+DLI        Title/header text → $32B5
$3171: BLANK 4               gap
$3172: Mode 4                Cockpit top (40-col multicolor text)
$3173: BLANK 2               gap
$3174-$31F1: 42x Mode D+LMS  Terrain viewport (42 rows)
$31F2: Mode D+LMS+DLI        Row 43 — DLI triggers ground color
$31F5-$31FB: 3x Mode D+LMS   Lower terrain (3 more rows)
$31FE: Mode D+LMS+DLI        Bottom terrain — DLI triggers cockpit
$3201: Mode 4+LMS → $332D    HUD text start
$3204-$320C: 9x Mode 4       HUD text (instrument panel)
$320A: Mode 4+DLI            DLI at HUD for final color setup
$320D: JVB → $316B           jump back, wait for VBI
```

### ANTIC Mode D Details
- 160 pixels wide, 4 colors per pixel (2 bits/pixel)
- 40 bytes per mode line
- 2 scanlines tall per mode line
- Each pixel maps to COLPF0-COLPF3 via 2-bit value
- With PRIOR=1: 5th player mode enabled + player/missile priority

### Screen Memory Layout
Each terrain row has LMS pointing to addresses with $60 (96) byte spacing:
$1070, $10D0, $1130, ..., $2150

Only 40 bytes are displayed per row, but the 96-byte stride provides space
for horizontal scrolling or double-buffering. The extra 56 bytes per row
may be used as off-screen workspace.

Total screen memory: 45 rows × 96 bytes = 4,320 bytes ($1070-$21B0)

### DLI Chain
The DLI handler at $4CF1 uses a dispatch table:
1. Saves A, Y registers
2. Uses $C8 as a counter (reset to 0 each VBI)
3. Looks up handler address from table at $4DD6[$C8*2]
4. Jumps through ($E1) indirect vector
5. Increments $C8, restores registers, RTI

#### DLI stages:
- **Stage 0** ($4D0D): Set sky/cockpit colors, switch CHBASE to $38 (custom charset)
- **Stage 1** ($4D20): Position players 0+1, set terrain colors (COLPF0-COLBK)
- **Stage 2** ($4D41): Configure missiles, set PMG colors for cockpit overlay
- **Stage 3+** ($4D79+): Final cockpit/HUD color setup
