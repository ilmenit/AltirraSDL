# River Raid — Overview

## Identity

- **Title**: River Raid
- **Publisher**: Activision
- **Designer**: Carol Shaw
- **Year**: 1982 (2600), 1983 (Atari 8-bit port)
- **Platform**: Atari 400/800/XL/XE (requires 48K RAM, no BASIC ROM)
- **Genre**: Vertically scrolling shooter

## Memory Map

| Range | Size | Content |
|-------|------|---------|
| $0000-$00FF | 256 | Zero-page: game variables (OS disabled) |
| $0100-$01FF | 256 | 6502 hardware stack |
| $0200-$03FF | 512 | OS page 2-3: repurposed as game RAM |
| $0400-$05FF | 512 | Entity tables ($0500+), misc game state |
| $0600-$0AFF | 1280 | Game scratch / score display / status data |
| $0B00-$0FFF | 1280 | **Player/Missile graphics**, single-line DMA (PMBASE=$08). M1=bullet, P2=jet, P3=multiplexed enemies. P0/P1 unused in playfield. See `notes/sprites.md`. |
| $1000-$1FFF | 4096 | Available RAM (partially used for terrain gen) |
| $2000-$3CFF | 7424 | Screen RAM: mode 14 bitmap (river terrain) |
| $3D80-$3EFF | 384 | Screen RAM: status bar (score, fuel, lives) |
| $3F00-$3FB5 | 182 | ANTIC display list |
| $A000-$BFFF | 8192 | **Game code + data ROM** (relocated from XEX) |
| $C000-$FFFF | 16384 | OS ROM |

## Segment List

The XEX file contains 3 segments totaling 8348 payload bytes:

1. **$4080-$60FF** (8320 bytes): Relocator stub ($4080-$40FF) + game code ($4100-$60FF)
2. **$0400-$0419** (26 bytes): Init code — calls relocator, enters game
3. **$02E2-$02E3** (2 bytes): INITAD vector → $0400

## Boot Sequence

1. OS XEX loader loads segments to RAM
2. INITAD fires → runs $0400
3. $0400 calls $4086 (relocator)
4. Relocator copies $4100-$60FF → $A000-$BFFF (8192 bytes of game code)
5. Relocator zero-fills $0400-$3EFF
6. JMP ($BFFE) → game cold start at $A000
7. $A000 disables all hardware, clears RAM, initializes display, enters main loop

## Main Loop Architecture

The game runs a single main loop at $AE99 synchronized to the display:

```
$AE99: Wait for VCOUNT < $50     ; top-of-frame sync
$AEA0: JSR $AFD0                 ; prepare next frame display
$AEB5: Update terrain scroll     ; advance river, copy new data
$A271: Update entities           ; move enemies, check spawns
$A4E0: Process collisions        ; score, deaths, fuel
$A547: Update score (BCD)        ; decimal mode arithmetic
$A312: Read input                ; joystick + console switches
$AE90: Update fuel               ; drain or refuel
$A701: Continue...               ; → back to $AE99
```

## Rendering Approach

The game uses a **DLI-driven scanline kernel** for rendering:

- The display list covers 172 scanlines in ANTIC mode 14 (4-color bitmap)
- A DLI fires at the top of the playfield and chains through every scanline
- The DLI kernel multiplexes 4 hardware P/M sprites to display 6+ entities
- Per-scanline, the DLI sets: HPOSP (position), COLPM (color), SIZEP (width)
- The terrain (river banks) is pre-rendered into the mode 14 bitmap at $2000
- The DLI also handles terrain color changes per row

This is a classic "racing the beam" technique adapted for the Atari's
ANTIC/GTIA hardware, allowing far more on-screen objects than the
4-player hardware limit would normally permit.

## Key Technical Achievements

1. **8KB total code + data**: The entire game fits in exactly 8192 bytes,
   including all graphics, sound, game logic, and lookup tables.

2. **6-entity sprite multiplexer**: The DLI kernel displays 6 independent
   entities using only 4 hardware players by repositioning them per-scanline.

3. **Procedural terrain**: The river is generated algorithmically, providing
   an effectively infinite playing field with no stored level data.

4. **No OS dependency**: The game takes over the hardware completely,
   running its own interrupt handlers and display kernel. This maximizes
   the available CPU time for the scanline kernel.
