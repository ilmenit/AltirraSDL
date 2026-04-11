# River Raid — Technical Findings

> **2026-04-11 update — DUO port correction pass:** A re-read of the
> split asm files in `/home/ilm/Documents/GitHub/river_raid_duo/src/`
> while preparing a simultaneous-2P fork uncovered several wrong claims
> in this document and the accompanying notes. Specific corrections:
>
> - **M0 and M2 are NOT cleared / unused.** M0 has a writer in
>   `frame_sync.asm` at $AEF1 driving a moving dot via `$C1`, with
>   COLPM0 cycled every frame. M2 is positioned at `player_sprite_x + 4`
>   by the DLI at $B539 as a narrow jet hit probe.
> - **`process_collision_results` ($B07C), `check_terrain_collision`
>   ($B05C), `play_score_sound` ($B1C0), `update_lives_display`
>   ($B21A), `clear_status_line`/`write_status_text` ($B221/$B223)
>   are all misnomers.** The first two are entry points into a
>   shared digit-rendering helper; the rest manipulate state flags
>   and the M-DMA buffer but don't touch sound or lives display.
> - **`lives_count = $002A`** is a countdown timer that triggers
>   `player_death`, not a visible life count.
> - **`player_death` at $A688** is a 5-instruction hook, not the
>   real death handler.
>
> The state machine, display list, entity model, and high-level
> architecture descriptions in this document are still substantially
> correct. The corrections above primarily affect the sprite section
> and the collision section. See the inline **CORRECTED** annotations
> below, the corresponding `notes/sprites.md`, `notes/variables.md`,
> `notes/subroutine_map.md` updates, and the top-level `project.json`
> `notes` field for the full details.

## Status

- **Segments**: 3 XEX segments (main $4080-$60FF, init $0400-$0419, INITAD→$0400)
- **Game code**: 8192 bytes at $A000-$BFFF (relocated from $4100-$60FF)
- **Labels**: 64+ entry points identified (29 JSR targets, 35+ JMP targets)
- **Routines**: 29 subroutines, 2 major interrupt handlers (VBI + DLI)
- **Variables**: 70+ zero-page variables mapped, 6-slot entity table at $0500
- **SHA-256**: `3924596cf04e1c7f97af9688a125c8a7e0b0d686dbaa40820c812cc1c406e704`
- **Platform**: Atari 800 (48K RAM, no BASIC ROM required)

## Game Overview

River Raid is a vertically scrolling shooter by Carol Shaw / Activision (1982).
The player flies a jet up a procedurally generated river, shooting enemies and
refueling at fuel depots. The Atari 8-bit version uses a sophisticated DLI kernel
to multiplex sprites and color the terrain per-scanline, fitting the entire game
into exactly 8KB of code + data.

The game has no OS dependency — it disables all interrupts, takes over the
hardware directly, and runs its own VBI/DLI handlers. It was designed for the
Atari 800 (no BASIC ROM at $A000), but runs on XL/XE machines with BASIC
disabled.

## Core Gameplay Loop

The main loop runs at $AE99 and follows this per-frame sequence:

1. **VCOUNT sync** ($AE99): Wait for scanline counter < $50 (top of visible area)
2. **Frame update** ($AEA0): JSR $AFD0 — update display list, prepare next frame
3. **Terrain scroll** ($AEB5-$AF5A): advance terrain, copy new row data into screen RAM
4. **Entity update** ($A271-$AE85): iterate 6 entity slots, move enemies, check spawn
5. **Collision check** ($A4E0): test player-entity and player-terrain collisions
6. **Score update** ($A547): BCD arithmetic on score at $63/$64/$65
7. **Input handling** ($A312): read PORTA for joystick, TRIG0 for fire button
8. **Fuel management** ($AE90): decrement fuel counter, check for empty

The loop is entered from the state machine at $A1D9 (JMP $AE99) after
the per-frame game logic completes.

## Loading Sequence

| Segment | Address | Size | Purpose |
|---------|---------|------|---------|
| 0 | $4080-$60FF | 8320 | Main: relocator at $4086 + game code at $4100 |
| 1 | $0400-$0419 | 26 | Init: calls relocator, enters game loop |
| 2 | $02E2-$02E3 | 2 | INITAD vector → $0400 |

The init code at $0400:
1. Sets CASINI ($02/$03) to point at $4086
2. JSR $4086 — the relocator
3. Enters infinite loop via JMP ($000C) / JMP ($000A)

The relocator at $4086:
1. Copies 32 pages ($2000 bytes) from $4100 to $A000 (game code)
2. Zero-fills $0400-$3EFF (clears the init code and lower RAM)
3. JSR to JMP ($BFFE) → IRQ setup
4. JMP ($BFFA) → cold start at $A000

## State Machine

The game state is tracked in zero-page variable $24:

| State | Handler | Purpose |
|-------|---------|---------|
| $00 | $A197→$AE99 | Attract mode / title screen |
| $01 | $A271 | Active gameplay |
| $02 | $A3E9 | Player dying (explosion animation) |
| $03 | $A15D | Game starting (init lives, clear entities) |

The state machine dispatch is at $A1CE-$A1DC:
```
  LDX $24        ; game_state
  BNE $A1DC      ; not attract → dispatch
  ...
  JMP $AE99      ; attract: run main loop
$A1DC:
  CPX #$02
  ...
```

## Display Architecture

### Display List ($3F00)

The display list uses ANTIC mode 14 (160-pixel wide, 4-color bitmap)
for the entire playfield — 172 scanlines of bitmap graphics:

```
$3F00: 3 × blank 8 lines (24 blank lines at top)
$3F01: DLI trigger on 2nd blank (fires DLI at top of playfield)
$3F03: LMS mode 14 → $3D80 (status/score area, 9 lines)
$3F0C: LMS mode 14 → $2000 (main river area, ~160 lines)
...
$3FB5: JVB → $3F00 (jump-and-wait-for-VBI)
```

Screen RAM layout:
- $3D80-$3EFF: Status bar (score, lives, fuel gauge) — 9 scan lines
- $2000-$3C7F: Main river terrain bitmap — ~160 scan lines
- Each mode 14 line is 48 bytes (160 pixels ÷ 4 colors × 2 bits = 40 bytes, but DMA reads 48)

### DLI Kernel ($B500)

The DLI handler is the most CPU-intensive part of the game, consuming
~37% of total CPU cycles. It's a **scanline kernel** that runs on every
display line to:

1. **Multiplex sprites**: Position and color up to 6 entities using only
   4 hardware players by changing HPOSP/COLPM registers per scanline
2. **Set terrain colors**: Write COLPF registers per row to color the
   river banks differently from the water
3. **Check collisions**: Read missile-to-player collision registers (M2PL)
   to detect hits

The DLI branches at $B509 based on VCOUNT:
- VCOUNT < $3F: Main playfield kernel (sprite multiplexer at $B510-$B60B)
- VCOUNT >= $3F: Status bar handler (at $B72A)

The main kernel loop ($B567-$B5E6) is WSYNC-locked:
```
$B567: STA WSYNC      ; sync to start of scanline
$B56A: TXA            ; X = scanline within entity
$B56B: CMP ($12),Y    ; compare to river bank position
$B56E: BCS $B575      ; if past bank → draw entity
$B570: INX            ; next scanline
$B571: BNE $B567      ; loop until entity zone ends
```

### Player/Missile Graphics

PMG base is at `$0800` (PMBASE = `$08` at `$D407`), **single-line DMA**
mode (DMACTL bit 4 set). Each P/M slot is 256 bytes:

**CORRECTED 2026-04-11 — see `notes/sprites.md` for the full corrected map.**

| Range                    | Sprite                | Role in gameplay                          |
|--------------------------|-----------------------|-------------------------------------------|
| `$0B00-$0BFF` bits 0-1   | M0                    | **ACTIVE** — bits set conditionally by `frame_sync.asm:52-59` from `$C1`, COLPM0 cycled. Visual role TBD (in-flight bullet? enemy missile?). |
| `$0B00-$0BFF` bits 2-3   | M1                    | **Player bullet at-rest template** at `$0BCC,Y` (`draw_bullet_pmg` at $B14B). |
| `$0B00-$0BFF` bits 4-5   | M2                    | **Jet narrow hit probe** at `player_sprite_x + 4` (DLI at $B539). M2 vs P3 captured into `$1C`. |
| `$0B00-$0BFF` bits 6-7   | M3                    | **Genuinely unused** (no writers found). |
| `$0C00-$0CFF`            | P0                    | **Genuinely unused** — DMA buffer zeroed each frame by `$B012`. HPOSP0 ← `$3C`=0 hides it. |
| `$0D00-$0DFF`            | P1                    | **Genuinely unused** — DMA buffer never written. COLPM1 is used as the *color* for the M1 bullet. |
| `$0E00-$0EFF`            | P2                    | **The jet** (HPOSP2 == `$57` in gameplay). |
| `$0F00-$0FFF`            | P3                    | **Multiplexed enemies** (WSYNC loop at `$B567`). |

GRACTL = `$03` (both players and missiles enabled).
PRIOR = `$01` (players have priority over playfield).

Active-player-mode bullet motion is empirically confirmed: with fire
held, the M1 DMA band top edge migrates from `$0BCC → $0BA9 → $0BA3`
at roughly 2 scanlines per frame, i.e. the bullet visibly travels
upward from the jet toward the top of the screen. The `$B14B` routine
only writes the 10-byte at-rest template; a second writer (not yet
traced) emits the in-flight bullet at a descending Y offset each frame.

The at-rest bullet is drawn by the routine at `$B14B-$B15C`:
```
$B14B  ldx #$09
$B14D  ldy #$09
$B14F  lda pal_flag ($BF)
$B151  bne $B155           ; if PAL, skip the NTSC Y override
$B153  ldy #$10             ; NTSC: Y = $10
$B155  lda #$0C             ; bullet byte = %00001100 → bits 2-3 = M1
$B157  sta $0BCC,Y          ; into missile DMA
$B15A  dey
$B15B  dex
$B15C  bpl $B157            ; 10 iterations
```
The `$0C` pattern writes into **bits 2-3** of each missile DMA byte,
which in the packed missile DMA layout correspond to **M1** — so only
M1's column lights up. Scanline range is `$0BD3-$0BDC` on NTSC,
`$0BCC-$0BD5` on PAL.

M1's X position comes from the DLI baseline
`HPOSM1 = ($5B>>3)+$5C` at `$B52C`, and its color comes from
`COLPM1 = $1E` set at the river-zone DLI `$B697`. The `HPOSP1 ← $39`
store at `$B54F` is mechanically present every frame but has no visual
effect because P1 has no bitmap — the DLI still positions an
empty P1 sprite. The role of `$39` is not fully characterised yet;
`bullet_x` is retained as a legacy label but may not actually represent
a bullet coordinate.

### Verification methodology

These sprite assignments were empirically confirmed via the bridge:

1. **Exhaustive memory scan** of `$A000-$BFFF` for 3-byte store opcodes
   (`8D/8E/8C/9D/99 xx yy`) targeting any P/M DMA or GTIA register.
   Caught the obvious direct stores but **missed indirect stores** like
   `sta ($a3),Y` (2-byte instructions with no absolute address).
2. **Runtime write-watchpoints** via `bridge.watch_set(addr, mode="w")`
   on probe addresses within each DMA buffer. Halt the sim on firing,
   read `regs()` for PC, disassemble backward to identify the writer.
   Caught the generic indirect copier at `$B012` and the bullet draw
   loop at `$B155`.
3. **ORed runtime DMA sweeps** — sample every P/M buffer each frame for
   N frames, OR the snapshots. Isolates each sprite's bitmap. Showed
   `....##..` bullet pattern appearing only while fire was pulsed.
4. **Color classifier on `rawscreen()`** — distinguishes title / scroll
   intro / gameplay by counting green (riverbank) pixels in the
   playfield area; avoids the false positives from relying on
   zero-page heuristics.

## Entity System

The game maintains 6 entity slots in parallel arrays at $0500:

| Offset | Array | Purpose |
|--------|-------|---------|
| +$00 | entity_type[6] | Entity type (0=empty, 1-6=enemy, 7=fuel, $0E=bridge) |
| +$06 | entity_anim[5] | Animation frame |
| +$0B | entity_shape[6] | Shape/sprite table index |
| +$16 | entity_xpos[6] | Horizontal position |
| +$21 | entity_size[6] | SIZEP width value |

Entity types identified:
- **0**: Empty slot
- **1-2**: Ship variants (left/right facing)
- **3-4**: Helicopter variants
- **5-6**: Jet variants
- **7**: Fuel depot
- **$0C**: Balloon
- **$0E**: Bridge

Entities are spawned by the terrain generator and updated each frame.
The DLI kernel reads the entity tables to position sprites on the
correct scanlines.

## Collision Model

**CORRECTED 2026-04-11.** Collisions are detected in two ways:

1. **Hardware P/M collision** (DLI at `$B60B` end-zone and `$B5EA-$B5F1`
   mid-zone): The DLI reads collision-latch registers via the dual-purpose
   `$D000-$D00F` addresses (write = HPOS/SIZE/GRAF, read = collision)
   and stores **entity-Y indices** (not flag bits) into RAM bytes
   `$1A-$22`. Each byte holds either `$FF` (no hit this frame) or
   the entity slot index that was active during the collision. Main
   code then reads these bytes — it does NOT read hardware collision
   registers directly. After the captures, the DLI calls `HITCLR`
   ($D01E) at $B65F to reset the hardware latches.

   The exact mapping (under the corrected sprite map: P2 = jet,
   P3 = multiplexed enemies, M1 = bullet template, M0 = M0 motion
   sprite, M2 = jet hit probe, P0/P1 empty):

   | RAM byte | DLI source                                   | Meaning                                          |
   |----------|----------------------------------------------|--------------------------------------------------|
   | `$1A`    | `lda HPOSP2` = M2PF bits 0-1                 | M2 vs PF0-1 (dormant — M2 has no PF interaction) |
   | `$1B`    | `lda HPOSM2` = P2PF bit 0                    | **JET vs PF0 = TERRAIN DEATH (bank/bridge)**     |
   | `$1C`    | `lda M2PL` bit 3                             | M2 (jet hit probe) vs P3 (enemy)                 |
   | `$1D`    | `lda P2PL` bit 3 (after `lsr;and #$04`)      | **JET (wide) vs ENEMY**                          |
   | `$1E`    | `lda P2PL` bit 0                             | P2 vs P0 (dormant — P0 unused)                   |
   | `$1F`    | `lda P3PL` bit 0                             | P3 vs P0 (dormant — P0 unused)                   |
   | `$20`    | `lda HPOSM1` = P1PF bit 1                    | P1 vs PF1 (dormant — P1 unused)                  |
   | `$21`    | `lda SIZEP0` = M0PL bit 2                    | M0 vs P2 (jet) — fires when M0 dot overlaps jet  |
   | `$22`    | `lda HPOSP0` = M0PF bits 0-1                 | **M0 vs PF0-1 (M0-vs-bank/bridge hit-stop)**     |

   The **actual collision-to-effect dispatcher** is `check_entity_collision`
   at $A3E9 in `entities.asm`, which reads the `$1A-$22` bytes and
   branches per category. The previously claimed "M2PL bit 3 = bullet
   hits enemy" was wrong — M2PL bit 3 is the jet hit probe (M2) vs
   enemy (P3), captured into `$1C`. **Bullet-vs-enemy** is detected
   by a software AABB check at `entities.asm:400-450` that compares
   `bullet_x` against `entity_xpos_tbl,X`, NOT by a hardware collision
   register read.

2. **Software river bank collision** ($B56B): The DLI compares the
   current scanline X against the river bank position table ($0C-$15).
   If the player sprite overlaps the bank, a collision flag is set.

Collision results are processed in the main loop at $A3E9-$A4DF:
- Entity collision → destroy entity, add score
- Bank collision → destroy player, lose life
- Fuel depot overlap → refuel (flag at $38 bit 7)

## Fuel System

- Fuel level is stored at $76 (range $FF=full to $00=empty)
- Fuel decrements at $AE94: `DEC $32` — the drain counter at $32
  wraps around and decrements fuel via the main loop
- Fuel refill happens when the player overlaps a fuel depot
  ($38 bit 7 set), incrementing $76 each frame
- When fuel reaches $00, the player dies

The fuel gauge in the status bar is drawn as colored bars in the
screen RAM at $3D80+. The color changes (green → yellow → red) are
managed by the status bar DLI at $B72A+.

## Sound Engine

The VBI handler at $B2A5 manages all sound:

- **Engine sound** (channel 1, AUDF1/AUDC1): Continuous when flying,
  frequency varies with speed
- **Explosion sound** ($C6 timer): When $C6 > 0, plays a noise burst
  on channel 1 (AUDF1=$40, AUDC1=$AF), timer decremented each VBI
- **Score/refuel sounds** (channels 2-3): Short tonal effects for
  scoring events and refueling

The sound engine is simple — no music, just sound effects tied to
game events. The VBI checks $C6 (explosion timer), $24 (game state),
and $7A/$7B (engine sound parameters) to determine what to output.

## Scoring

Score is stored in BCD format across three bytes:
- $63: high (hundred-thousands / millions)
- $64: mid (thousands / ten-thousands)
- $65: low (ones / tens / hundreds)

The scoring routine at $A547 uses `SED` (set decimal mode) for BCD
addition. Score values per enemy type are looked up from a table at
$BB0B. Bridge crossing awards bonus points and increments the bridge
counter at $2E/$62.

Extra lives are awarded when the score crosses certain thresholds,
tracked via $6A (extra_life_thresh).

## Input Handling

Input is read at $A312-$A33F:

```
  LDA PORTA ($D300)  ; read joystick port A
  LDX $77            ; player_number (0 or 1)
  BEQ $A325          ; player 1: use low nibble
  LSR × 4            ; player 2: shift high nibble down
  AND #$0F
  STA $19            ; store direction
```

The joystick direction nibble:
- $0F: centered (no input)
- $0E: up
- $0D: down
- $0B: left
- $07: right
- Other values: diagonals

Fire button is read from TRIG0/TRIG1 ($D010/$D011).
Console switches from CONSOL ($D01F): bit 0=START, bit 1=SELECT, bit 2=OPTION.

## Terrain Generation

The river terrain is procedurally generated using a simple PRNG seeded
from $A5/$A6. The terrain generator runs in the main loop and produces
new rows at the bottom of the screen as the terrain scrolls.

Key terrain parameters:
- $0C-$15: Left bank positions for 10 visible rows
- $52: Player-relative X position within the river
- $53/$54: Current river width parameters (left/right edges)
- $5A: Section type (controls river shape — straight, narrow, winding)

The river banks are drawn as colored pixels in the mode 14 bitmap.
The terrain data at $B800-$BBFF contains shape templates for the
river banks and obstacles.

## Level Progression

The game has no discrete levels but difficulty increases continuously:

- After each bridge crossing, enemy density and speed may increase
- The river becomes narrower in certain sections
- Enemy types rotate — later sections introduce faster enemies
- The terrain_density variable ($5D) and spawn_cooldown ($5F) control
  how frequently enemies appear

The bridge counter at $2E tracks overall progress. Every few bridges,
the game introduces harder combinations of enemies and terrain.

## Performance Profile

From a 600-frame profile during active gameplay:

| Routine | Cycles | % of frame | Purpose |
|---------|--------|-----------|---------|
| DLI kernel ($B500) | 1,194,150 | 37.3% | Per-scanline sprite multiplexer |
| Main loop wait ($AE99) | 680,000 | 21.2% | VCOUNT sync (idle wait) |
| Hot loop ($B567) | 710,919 | 22.2% | WSYNC-locked scanline loop (inside DLI) |
| VBI ($B2A5) | 17,713 | 0.6% | Sound + game state |
| Terrain render ($AFF4) | 79,297 | 2.5% | Write terrain rows to screen |
| Entity update ($B034+) | 20,893 | 0.7% | Per-entity movement |

The DLI kernel dominates — it must run every scanline to maintain the
sprite multiplexing illusion. The main loop spends most of its time
waiting for the right VCOUNT value to start the next frame's processing.

## Data Tables ($B400-$BFFF)

| Address | Size | Content |
|---------|------|---------|
| $B48D-$B4D0 | 68 | Color tables (per-entity-type colors) |
| $B4D1-$B4FF | 47 | Terrain row pointer table (pairs of addresses) |
| $B776-$B7FF | 138 | Display list template + initialization data |
| $B800-$BBFF | 1024 | Sprite/shape data tables (entity graphics) |
| $BAB7-$BAC4 | 14 | Fuel gauge display pointers |
| $BAC5-$BACC | 8 | Player color cycle table |
| $BB0B-$BB23 | 25 | Score value table per entity type (BCD) |
| $BB24-$BB2F | 12 | Entity type transition table |
| $BB30-$BB5F | 48 | Sprite offset tables |
| $BB60-$BB8F | 48 | Sprite height tables |
| $BC00-$BEFF | 768 | Terrain shape templates (bank contours) |
| $BFFA-$BFFF | 6 | 6502 vectors: cold=$A000, ?=$8000, IRQ=$A000 |

## Open Questions

1. **Two-player alternation**: The code at $A312 reads player number from
   $77 and shifts PORTA accordingly, but the full two-player state save/restore
   hasn't been traced.

2. **Exact score values**: The BCD score table at $BB0B is indexed by entity
   type, but the exact point values per enemy haven't been fully decoded.

3. **Terrain PRNG algorithm**: The terrain generator uses $A5/$A6 as state
   but the exact algorithm (and whether the river pattern repeats) hasn't
   been fully reverse-engineered.

4. **Extra life thresholds**: The threshold tracked at $6A triggers extra
   lives but the exact score intervals haven't been confirmed.

## Status

- Phases 0-3: Complete (XEX parse, boot, profiling)
- Phase 4: In progress (recursive descent, ~73% byte coverage)
- Phase 5: In progress (variables partially mapped)
- Phase 6: In progress (this document + notes)
- Phase 7: Pending (MADS round-trip)
- Phase 8: Pending (final hand-off)
