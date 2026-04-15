# River Raid — Technical Findings

> **2026-04-14 correction — missile roles were backwards:** Runtime
> inspection (Altirra cheat search + breakpoint tracing) proved that
> earlier claims about M1 and M2 were swapped. Specifically:
>
> - **M1 is the FUEL GAUGE BAR**, not the bullet. The 10-byte M1
>   stripe at `$0BCC-$0BD5` drawn by the routine at `$B14B` (case
>   study called this `draw_bullet_pmg` — **misnamed**, it writes the
>   fuel-bar stripe, not the bullet) appears in the status-panel Y
>   range. Its horizontal position is derived from fuel level every
>   frame by the playfield DLI at `$B523`: `HPOSM1 = (fuel_level >> 3)
>   + $5C` (the `$5C` in the formula is an **immediate literal**, not
>   the fuel_drain_ctr byte at `$005C`). As fuel drops from `$FF` to
>   `$00` the bar slides left across 31 pixels of horizontal travel.
> - **M2 is the bullet**, drawn at a moving Y via the routine at
>   `$B223` which writes `$20` (M2 bit) to `$0B01+$56 .. $0B08+$56`
>   each frame. HPOSM2 is set to `player_sprite_x + 4` in the DLI;
>   as `$56` is decremented by `handle_bullet_fire`, the bitmap rises
>   up the screen. `$1A` captures M2-vs-PF (terrain) and drives
>   bullet reset at `entities.asm:$A58D`.
> - **The 50-byte block at `$AEC8-$AEF9`** previously labelled "fuel
>   bar indicator" is a separate M0 animation (COLPM0 colour cycle +
>   M0 dots drawn at `$0B00 + frame_ctr_copy` gated by `$C1`) — the
>   actual effect is uncharacterised and may be a decorative dot or
>   low-fuel warning, not the fuel gauge.
>
> The correct mental model: **M1 = fuel bar, M2 = bullet, M0 = TBD
> decorative/warning effect, M3 = unused.** See the sprite table in
> the "Player/Missile Graphics" section below for the full map.
>
> **Earlier 2026-04-11 corrections** still apply: `process_collision_results`,
> `check_terrain_collision`, `play_score_sound`, `update_lives_display`,
> `clear_status_line`/`write_status_text` are all misnomers;
> `lives_count = $002A` is a countdown timer; `player_death` at `$A688`
> is a 5-instruction hook (jumps to state_start_game).
>
> **Earlier 2026-04-14 correction** still applies: `$5B` is the real
> fuel level (cheat-search verified), `$76` is some other counter of
> unknown purpose. See the "Fuel System" section.

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

**CORRECTED 2026-04-14 — M1 and M2 roles were previously swapped.**

| Range                    | Sprite                | Role in gameplay                          |
|--------------------------|-----------------------|-------------------------------------------|
| `$0B00-$0BFF` bits 0-1   | M0                    | Some decorative/warning effect driven by the `$AEC8-$AEF9` block: COLPM0 color-cycled through yellow/green (`player_color_cycle`, `$BAC5`), M0 dots drawn at `$0B00 + frame_ctr_copy`, gated by `$C1` as an X position. **Exact gameplay role unverified** — NOT the fuel bar (that's M1) and NOT a reliable death-effect interpretation. |
| `$0B00-$0BFF` bits 2-3   | M1                    | **Fuel gauge bar.** 10-byte stripe written to `$0BCC-$0BD5` (PAL) / `$0BD3-$0BDC` (NTSC) by the routine at `$B14B` (misnamed `draw_bullet_pmg` — it draws the fuel bar). Falls in the status-panel Y range. HPOSM1 is computed every playfield DLI as `(fuel_level >> 3) + $5C` (literal) at `$B523`; bar slides left as fuel drops. COLPM1 set to `$1E` at river-zone DLI `$B697`. |
| `$0B00-$0BFF` bits 4-5   | M2                    | **Player bullet.** Bitmap written at moving Y via `$B223` (loop stores `$20` = M2 bit 5 to `$0B01+$56 .. $0B08+$56`); as `$56` is decremented by `handle_bullet_fire` each frame, the bullet stripe migrates upward. HPOSM2 = `player_sprite_x + 4` at DLI `$B539`. `$1A` captures M2-vs-PF (terrain hit → bullet reset at `entities.asm:$A58D`). `$1C` captures M2-vs-P3 (bullet-hits-enemy). |
| `$0B00-$0BFF` bits 6-7   | M3                    | **Genuinely unused** (no writers found). |
| `$0C00-$0CFF`            | P0                    | **Genuinely unused** — DMA buffer zeroed each frame by `$B012`. HPOSP0 ← `$3C`=0 hides it. |
| `$0D00-$0DFF`            | P1                    | **Genuinely unused** — DMA buffer never written. COLPM1 is consumed by M1 (the fuel bar colour). |
| `$0E00-$0EFF`            | P2                    | **The jet** (HPOSP2 == `$57` in gameplay). |
| `$0F00-$0FFF`            | P3                    | **Multiplexed enemies** (WSYNC loop at `$B567`). |

GRACTL = `$03` (both players and missiles enabled).
PRIOR = `$01` (players have priority over playfield).

**Bullet motion (CORRECTED — uses M2, not M1):** the in-flight bullet
is drawn on M2 by the routine at `$B1C0` / `$B223`. Each frame the game
calls `handle_bullet_fire` with X = new bullet position; the helper
clears M2 bits at the previous `$56` position then writes `$20` (M2 bit
5) to 8 consecutive bytes at `$0B01+X .. $0B08+X`, then stores X to
`$56`. X migrates downward (toward lower addresses = higher on the
screen) by 6 each frame. HPOSM2 stays at `player_sprite_x + 4` so the
bullet column is always 4 px right of the jet centre. `$1A` captures
M2 vs playfield (terrain); when fired, `entities.asm:$A58D` resets the
bullet to the at-rest position. **The `M1 DMA band migrates from
$0BCC → $0BA9 → $0BA3` observation in earlier revisions was a
misinterpretation — that motion is the M2 bullet stripe on different
DMA bytes, with M1 being a separate (fuel-bar) stripe at the fixed
`$0BCC-$0BD5` status-panel Y range.**

**Fuel bar (M1) bitmap** is written by the routine at `$B14B-$B15C`
(misnamed `draw_bullet_pmg` in the case-study labels — actually draws
the fuel bar, not the bullet):
```
$B14B  ldx #$09
$B14D  ldy #$09
$B14F  lda pal_flag ($BF)
$B151  bne $B155           ; if PAL, skip the NTSC Y override
$B153  ldy #$10             ; NTSC: Y = $10
$B155  lda #$0C             ; $0C = %00001100 → bits 2-3 = M1
$B157  sta $0BCC,Y          ; M1 stripe in missile DMA
$B15A  dey
$B15B  dex
$B15C  bpl $B157            ; 10 iterations
```
Writes 10 bytes of `$0C` into missile DMA at `$0BCC-$0BD5` (PAL) /
`$0BD3-$0BDC` (NTSC) — all in the status-panel Y range.

**Fuel bar X-position formula** comes from the playfield DLI baseline
at `$B523-$B52C`:
```
   lda fuel_level     ; $5B
   lsr; lsr; lsr      ; fuel_level >> 3
   clc
   adc #$5C           ; literal constant, NOT address $005C
   sta HPOSM1
```
So `HPOSM1 = (fuel_level >> 3) + $5C` (constant offset). Fuel `$FF`
→ HPOSM1 `$7B`; fuel `$00` → HPOSM1 `$5C`. The bar shifts 31 pixels
leftward as the tank empties. COLPM1 is `$00` in the early DLI
(hiding M1 above the status bar) and set to `$1E` (yellow) at the
river-zone DLI `$B697` so M1 is only visible in the status-panel
band. The "F 1/2 E" custom glyphs (chars 7-14, PF3 black, set by
`$B680`) have interior blank scanlines — M1 shows through only
those rows, producing the bar-inside-gauge appearance. `PRIOR=$04`
at `$B6B7` makes playfield have priority over players/missiles,
so the black glyph borders cover M1 at the frame pixels.

**The `HPOSP1 ← $39`** store at `$B54F` is mechanically present every
frame but has no visual effect because P1 has no bitmap — the DLI
still positions an empty P1 sprite. The role of `$39` is not fully
characterised yet; `bullet_x` is retained as a legacy label but may
not actually represent a bullet coordinate.

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
   P3 = multiplexed enemies, **M1 = fuel bar, M2 = player bullet**,
   M0 = uncharacterised decorative effect, M3 = unused, P0/P1 empty):

   | RAM byte | DLI source                                   | Meaning                                          |
   |----------|----------------------------------------------|--------------------------------------------------|
   | `$1A`    | `lda HPOSP2` = M2PF bits 0-1                 | **M2 vs PF0-1 = BULLET vs TERRAIN-BANK.** Drives bullet reset at `entities.asm:$A58D` (`lda $1A; bpl loc_A589`). |
   | `$1B`    | `lda HPOSM2` = P2PF bit 0                    | **JET vs PF0 = TERRAIN DEATH (bank/bridge).** |
   | `$1C`    | `lda M2PL` bit 3                             | **M2 vs P3 = BULLET vs ENEMY.** M2 is the bullet (not a hit probe); when the moving bullet stripe overlaps an enemy, M2PL bit 3 fires and the entity slot is captured. Consumed at `entities.asm:$A4B6`. |
   | `$1D`    | `lda P2PL` bit 3 (after `lsr;and #$04`)      | **JET (wide) vs ENEMY.** |
   | `$1E`    | `lda P2PL` bit 0                             | P2 vs P0 (dormant — P0 unused). |
   | `$1F`    | `lda P3PL` bit 0                             | P3 vs P0 (dormant — P0 unused). |
   | `$20`    | `lda HPOSM1` = P1PF bit 1                    | M1 (fuel bar) vs PF1. Fires when the fuel bar stripe overlaps the playfield colour-1 region, but the consumer path at `entities.asm` doesn't use this for gameplay-meaningful logic. Effectively dormant. |
   | `$21`    | `lda SIZEP0` = M0PL bit 2                    | M0 vs P2 (jet). Fires when the M0 decorative dot overlaps the jet. |
   | `$22`    | `lda HPOSP0` = M0PF bits 0-1                 | M0 vs PF0-1. Fires when the M0 decorative dot overlaps terrain. Consumed at `bullets_terrain.asm:$A5FD` to stop the M0 motion loop. |

   The **actual collision-to-effect dispatcher** is `check_entity_collision`
   at $A3E9 in `entities.asm`, which reads the `$1A-$22` bytes and
   branches per category. Under the corrected model, **bullet-vs-enemy
   hits DO use a hardware collision register** (M2PL bit 3 → `$1C`),
   contrary to an earlier claim that bullet hits were software-only.
   The software AABB check at `entities.asm:400-450` (which compares
   `bullet_x` against `entity_xpos_tbl,X`) runs in parallel and
   likely handles a wider hit box or a secondary scoring path.

2. **Software river bank collision** ($B56B): The DLI compares the
   current scanline X against the river bank position table ($0C-$15).
   If the player sprite overlaps the bank, a collision flag is set.

Collision results are processed in the main loop at $A3E9-$A4DF:
- Entity collision → destroy entity, add score
- Bank collision → destroy player, lose life
- Fuel depot overlap → refuel (flag at $38 bit 7)

## Fuel System

> **2026-04-14 CORRECTION:** The case study previously labeled `$76`
> as `fuel_level`. This was wrong — it was inherited from a partial
> static analysis. **Verified via Altirra cheat search (2026-04-14):
> the actual fuel level is at `$5B`.** Freezing `$5B` stops fuel
> from decreasing; freezing `$76` has no effect on fuel. The
> `$76` byte is some other counter (purpose TBD). The previous
> `m1_x_shift_src` label on `$5B` was also wrong.

### The real fuel logic

**Fuel storage**: `$5B` (range `$FF`=full to `$00`=empty).
Companion drain counter at `$5C`.

**Fuel drain** — runs every frame in `check_entity_collision` when
no collision fires. Code path at `entities.asm:$A43F`:

```
$A43F: lda $5C        ; drain counter (companion to $5B)
$A441: sec; sbc #$20  ; subtract $20
$A444: bcs $A453      ; if no borrow, just store back (no drain this frame)
$A446: ldy $5B        ; load fuel
$A448: bne $A451      ; fuel != 0 → drain
$A44A: lda #$02
$A44C: ldy #$23
$A44E: jmp $A43A      ; fuel = 0 → death sound (jsr play_score_sound w/ death params)
$A451: dec $5B        ; ← drain fuel by 1
$A453: sta $5C        ; update drain counter
```

So the drain happens when `$5C - $20` borrows. `$5C` accumulates
over multiple frames; each "tick" of the borrow chain costs 1 fuel.
When `$5B` reaches 0, the next frame's drain triggers the death sound.

**Fuel refill** — runs only while jet body (`$1D` collision) overlaps
an entity of type **> `$0E`** (fuel depots are types `$0F+`, not `$07`!).
Code path at `entities.asm:$A40A`:

```
$A40A: lda $5B        ; current fuel
$A40C: adc #$01       ; +1 (carry was set from cmp #$0E)
$A40E: ldx #$04       ; sound type 4
$A410: bcc $A418      ; no overflow → store
$A412: lda #$FF       ; overflow → clamp fuel to $FF
$A414: sta $5C        ; reset drain counter
$A417: ldx #$03       ; sound type 3
$A418: sta $5B        ; ← STORE NEW FUEL
$A41A: cpx $7A
$A41C: beq $A424      ; sound already set → skip
$A41E: stx $7A        ; refuel SOUND TYPE = $03 or $04
$A420: lda #$08
$A422: sta $7B        ; sound duration
```

So flying over a fuel depot increments `$5B` by 1 per frame and
triggers the refuel tick sound via `$7A=3/4` (consumed by VBI engine
sound dispatcher).

### Corrected entity type ranges

| Type range | Behavior on jet-body collision (`$1D`) |
|------------|----------------------------------------|
| `$00`      | Empty slot |
| `$01-$06`  | Enemies (ships, helicopters, jets) — handled by other collision paths |
| `$07-$0D`  | Enemies (additional variants) — falls through `cmp #$07/bcc; cmp #$0E/bcc` to `$A3FC`: plays score sound and destroys the enemy. **This was previously misidentified as the fuel-depot path.** |
| `$0E`      | Bridge — `inc bridges_crossed`, falls through to `$A3FC` |
| `$0F+`     | **Fuel depots** — falls to `$A40A`: increments fuel `$5B` by 1 per frame |

The previous claim "type `$07` = fuel depot" was wrong.

### Verification
- **Cheat search**: Altirra's memory cheat finder identified `$5B`
  by matching "value decreases over time" against the on-screen
  fuel gauge. Freezing `$5B` halts fuel drain.
- **Breakpoint at `$A418`**: fires every frame while jet1 flies
  over a fuel depot — confirmed user-side 2026-04-14.
- **Breakpoint at `$A3FC`**: only fires on jet-body collision with
  enemies (types `$07-$0E`), confirming the type-range correction.

### Fuel gauge display

The fuel gauge visual has two layers:
1. **Frame/labels**: ANTIC mode 6 custom text characters (chars 7-14)
   drawn in COLPF3 (black) showing "E 1/2 F" with a rectangular border.
   Interior rows have empty ($00) bitmap, letting the bar show through.
2. **Bar**: M0 (Missile 0) bits drawn at `$AEC8-$AEF9` in the missile
   DMA buffer at `$0B00 + frame_ctr_copy`. COLPM0 cycles through
   `player_color_cycle` ($1E-$BE = yellow-to-green). HPOSM0 is set
   from `$C1` which tracks the fuel bar's horizontal position —
   moves leftward as fuel depletes. With PRIOR=$04, the black PF3
   frame has priority over M0, creating the bordered gauge effect.

The bar's X position (`$C1`) is driven indirectly from `$5B` —
exact relationship still being traced.

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

## Extended Analysis Discoveries (2026-04-12)

> The following findings were made during deeper reverse-engineering
> of the game mechanics. They resolve several open questions and
> reveal previously undocumented mechanics.

### M0 — decorative animation (exact role TBD)

**REVISION 2026-04-14:** an earlier revision of this document claimed
M0 was the fuel bar. That was wrong — **M1 is the fuel bar** (see the
"Player/Missile Graphics" and "Fuel System" sections). The code at
`$AEC8-$AEF9` runs every frame but drives a separate M0 animation
whose gameplay function is not yet characterised:

```
$AEC8: lda frame_counter    ; animate color
$AECA: lsr; lsr; lsr
$AECD: and #$07             ; index 0-7
$AECF: tax
$AED0: lda player_color_cycle,X  ; $1E,$2E,$3E,$4E,$6E,$7E,$9E,$BE
$AED3: sta COLPM0           ; cycle COLPM0 through yellow-green

$AED6: ldx frame_ctr_copy   ; $C2 = current Y scanline offset
$AED8: cpx #$1C
$AEDA: bcc skip             ; only draw at Y >= $1C

$AEDC-$AEEF: clear M0 bits (and #$FC) at 3 consecutive missile
             DMA bytes ($0AFE+X, $0AFF+X, $0B00+X)

$AEF1: ldy $C1              ; M0 X position (or visibility gate)
$AEF3: beq skip             ; if $C1=0, don't set M0
$AEF5: ora #$03             ; SET M0 bits 0-1
$AEF7: sta $0B00,X          ; draw M0 at current scanline
```

`$C1` controls HPOSM0 (via DLI baseline at `$B561`). It tracks
`frame_ctr_copy`, producing a moving M0 dot that traverses the screen
vertically. The M0 motion loop in `bullets_terrain.asm:$A5CA-$A607`
(gated by `lda fuel_level; bne ...`) appears to be a separate effect
that activates on fuel=0 or invulnerability states, using `$C0`
(direction) and `$C1` (position). The exact visual / gameplay role
(decorative? low-fuel warning? death fragments?) is still unverified
— this section has been flagged as "in need of bridge-verification"
across multiple revisions.

### SIZEM initialized to $01 at $A116

`init.asm:$A116` executes `inx; stx SIZEM` where X wraps from
$FF→$00 then increments to $01. This sets M0 to double width
(SIZEM bits 0-1 = 01). M1/M2/M3 stay normal width (bits 2-7 = 0).

### Player movement uses velocity + sub-pixel accumulation

The horizontal movement system at `entities.asm:$A453-$A495` is
NOT simple inc/dec — it implements acceleration with fractional
positioning:

| Variable | Address | Role |
|----------|---------|------|
| `$2B` | ZP | Velocity accumulator: `+= 8` each frame with input held, capped at `$F8`. Reset to 0 on neutral. |
| `$58` | ZP | Fractional position: `+= velocity` each frame. Pixel move occurs only on 8-bit overflow. |

This creates inertia: the jet starts slowly (first pixel after ~8 frames),
accelerates to near-pixel-per-frame at max velocity ($F8). Releasing
the stick immediately resets velocity and fraction to 0 (no coasting).
The same pattern is used for vertical (up/down) movement with `$59`.

### Main game loop rate: ~1/9 VBI frequency (PAL)

Empirically confirmed via bridge: `frame_counter` ($18, incremented
in the VBI at `vbi_game_timers`) advances once per ~9 bridge
`frame(1)` calls. The game's main loop (entity update, collision,
input, fuel) runs at approximately **5.5 fps** while the VBI and DLI
maintain 50 fps visuals (terrain scroll, sprite multiplexing, sound).
Bullet1's smooth upward motion is VBI-driven (timer `$7D` decremented
in the VBI at `$B387`), not main-loop-driven.

### Bullet1 at-rest template is stationary

The 10 M1 bytes at `$0BCC-$0BD5` do **not move** during bullet
flight — they remain fixed at the muzzle position. Bullet1's
apparent upward motion is achieved by changing `HPOSM1` (the
horizontal position) frame-by-frame via the `($5B>>3)+$5C` formula
in the DLI, combined with the terrain scrolling downward past the
fixed missile strip. The bullet "advances" relative to the terrain
because the terrain moves, not because the bullet bitmap moves.

### Jet shape draw location: $0EAA, 14 bytes

The jet shape is drawn at `frame_sync.asm:$AF32` via
`lda (src_ptr_lo),Y; sta $0EAA,Y` with Y from $0D down to $00.
This places the jet at P2 buffer offset $AA-$B7 (14 scanlines).
The fuel-gauge blit (`copy_row_data` with dst=$0C00+entity_timer)
overwrites the P0 buffer at the jet's Y range, making any P0
bitmap at those scanlines transient.

### Free zero-page range: $CC-$EF (36 bytes)

Bridge runtime sampling across 60 frames of active gameplay
confirmed ZP bytes $CC through $EF are consistently $00. This
36-byte range is safe for new variables.

### Collision captures $1F and $21 activate with P0 bitmap

When P0 carries a sprite, the DLI captures at `$1F` (P3PL bit 0 =
enemy vs P0) and `$21` (M0PL bit 2 = M0 vs P2) begin firing. The
`$1F` consumer in `entities.asm` at `$A4C1` destroys the enemy on
contact. The `$21` consumer at `$A42E` triggers entity hit processing.

### Status panel architecture (2026-04-13)

The bottom panel uses three ANTIC mode 6 (20-column text) lines at
screen RAM $1000-$103B, rendered with CHBASE=$08 (charset at $0800,
set by DLI at $B6AF). Custom characters loaded at init from $BF2E→$0818
(chars 3-16) and $BF9E→$08D0 (chars 26-28).

**ANTIC mode 6 color mapping** (verified from Altirra source
`antic.cpp:1044-1049`): screen byte bits 7-6 select from 4 foreground
color registers; background is always COLBK.

| Bits 7-6 | Foreground | Register |
|----------|------------|----------|
| 00 | COLPF0 | $D016 |
| 01 | COLPF1 | $D017 |
| 10 | COLPF2 | $D018 |
| 11 | COLPF3 | $D019 |

At the text lines (after playfield DLI exit, before status-bar DLI):
- COLPF0 = $1E (yellow) — set at DLI $B68B, flashed to $06 when fuel < $80
- COLPF1 = $8E (blue) — set at DLI $B68F
- COLPF2 = $1E (yellow) — set at DLI $B697
- COLPF3 = $00 (black) — set at DLI $B680 (X=0 via all paths to loc_B66B)
- COLBK = $06 (dark gray) — set at DLI $B6BA

Score digit rendering uses offset in $B1 to encode color into screen
bytes: P1 score offset $10 (PF0=yellow), P2/HI offset $50 (PF1=blue),
bridge number offset $90 (PF2=yellow), game number uses $D0+digit (PF3=black).

**Fuel gauge frame**: custom chars 7-14 (5 per text line × 2 lines)
in PF3 (black). Interior rows are empty ($00 bitmap), letting the M0
fuel bar show through. With PRIOR=$04 (set at $B6B7), PF3 has priority
over players/missiles — black frame covers M0 bar at border pixels.

**Lives display**: custom char 28 ($9C, PF2=yellow) = jet icon shape
at $1033+X. Count driven by `$6A` (visible lives, 0-9).

**Score rendering**: shared digit-render routine at $B086, entered via
4 entry points ($B05C, $B068, $B06E, $B07C) with different X/Y/$B1
parameters. Reads BCD bytes from ZP, writes character codes to $1000+Y.

**Scrolling text**: mode.h 6 (HSCROL-enabled) at $3FB2, using
CHBASE=$06 (set by status-bar DLI at $B72A). LMS address at $3FB3
advanced by VBI for scroll effect.

## Open Questions

1. **Two-player alternation**: The code at $A312 reads player number from
   $77 and shifts PORTA accordingly, but the full two-player state save/restore
   hasn't been traced.

2. **Exact score values**: The BCD score table at $BB0B is indexed NOT by
   entity type but by the **sprite shape index** from `$050B[entity_slot]`.
   Known: fuel station shape index $0F → $08 BCD (8 points per hit via
   proximity probe). Confirmed by Altirra debugger execution trace
   (2026-04-13).

3. **Terrain PRNG algorithm**: The terrain generator uses $A5/$A6 as state
   but the exact algorithm (and whether the river pattern repeats) hasn't
   been fully reverse-engineered.

4. **Extra life thresholds**: The threshold tracked at $6A triggers extra
   lives when score_mid ($64) hundreds digit rolls over during BCD
   addition. Extra life count capped at 9 (`cpy #$09; beq`). Trigger
   sets `$7E = $40` (extra life display effect).

5. ~~**M0 role**~~: **RESOLVED** — M0 has dual purpose: fuel bar indicator
   during normal play ($AEC8-$AEF9), and death-effect dot when fuel=0
   ($A5CA-$A607). See "Extended Analysis Discoveries" above.

6. **In-flight bullet mechanism**: Bullet1's visual upward motion is
   achieved through HPOSM1 changes + terrain scroll, NOT by moving
   M1 DMA bits. The exact HPOSM1 update path (via $5B/$5C) and its
   relationship to the fire timer ($7D) needs complete tracing.

7. ~~**Real visible lives counter**~~: **PARTIALLY RESOLVED** — `$2A` is
   a death countdown timer. `play_score_sound` resets it to 0
   (`stx $2A` with X=0). The game cycles: fuel depletes → fuel=0 →
   `check_entity_collision` → `play_score_sound` → refuel $FE + reset
   timer. Death triggers when the timer counts from 1→0 (every 8
   frames). The visible lives display is driven by `update_lives_display`
   ($B21A) which sets `fuel=$FF, lives_count=$FF`.

### Collision system architecture (corrected 2026-04-14)

Key architectural findings from extended analysis:

1. **`check_entity_collision` ($A3E9) runs every frame**, not only at
   fuel=0 as previously claimed. The previous claim was based on the
   wrong fuel-level address ($76 instead of $5B). The drain code at
   `$A43F` (which decrements `$5B`) is reached every frame as the
   fall-through path of `check_entity_collision`.

2. **Fuel depots are entity type `$0F+`**, NOT `$07`. The previous
   claim was wrong. Types `$07-$0D` are enemies that kill the jet
   on `$1D` (jet body) collision; type `$0E` is the bridge; types
   `$0F+` trigger the refuel path at `$A40A`.

3. **`check_terrain_collision` ($B05C) and `process_collision_results`
   ($B07C) are digit-rendering routines**, not collision processors.
   They read BCD score bytes ($61-$65) and write character codes to
   display memory ($1000+Y). The misleading names came from the
   original reverse-engineering pass.

4. **Frame execution order:** VBI (VCOUNT high) → main_frame_sync
   (VCOUNT < $50) → frame_update → jet_draw → VCOUNT < $07
   check → attract_wait (blocks until VCOUNT ≥ $50, i.e., AFTER
   DLI) → state_dispatch → entity_dispatch → check_entity_collision.
   The DLI fires during display (VCOUNT ~30), between the jet draw
   and check_entity_collision.

5. **`play_score_sound` ($B1C0) at `$B200` does NOT refuel.** It sets
   `$76 = $FE` and `$2A = $00`, but `$76` is not the fuel level
   ($5B is). The role of `$76` and the `$B200` write is still TBD.

6. **The real refuel mechanism**: jet body (`$1D`) overlaps an entity
   of type `$0F+` → `check_entity_collision` at `$A3E9` falls through
   `cmp #$0E/bne` to `$A40A` → `lda $5B; adc #$01; sta $5B` increments
   fuel by 1 per frame. Sound type `$03`/`$04` written to `$7A`
   triggers the refuel tick sound via the VBI sound engine.

7. **The real drain mechanism**: same path's fall-through at `$A43F`
   subtracts `$20` from `$5C` each frame; on borrow, `$5B` decrements
   by 1. When `$5B` reaches 0, the next drain triggers death sound.

## Status

- Phases 0-3: Complete (XEX parse, boot, profiling)
- Phase 4: In progress (recursive descent, ~73% byte coverage)
- Phase 5: In progress (variables partially mapped)
- Phase 6: In progress (this document + notes)
- Phase 7: Pending (MADS round-trip)
- Phase 8: Pending (final hand-off)
- **Extended analysis**: M0 dual-role resolved, status panel architecture documented, fuel bar mechanism traced
