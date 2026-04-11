# River Raid — Sprite map (verified via bridge)

Previous passes of this project misidentified several of the game's
sprite assignments (they had the jet on P0, the bullet on P1's bitmap,
and listed a stale `$0600`-based PMG layout). This note captures the
**verified** sprite map after an empirical bridge investigation and
should be treated as the source of truth for any future analysis.

> **2026-04-11 update:** A re-read of the split asm files during the
> DUO 2-player port uncovered that **M0 and M2 are NOT cleared / idle**
> as the original "Final sprite assignments" table claimed. Both have
> active writers, positioning, and collision capture. The corrected
> table below reflects this. The original "Final" table has been kept
> for historical reference at the end of this file under the heading
> "Original (incomplete) sprite table — what was missed". The "What was
> wrong in the earlier analysis" section below has also been extended
> with the newly-found mistakes. See also `DUO_DESIGN_DOC.md` §2 in
> `/home/ilm/Documents/GitHub/river_raid_duo/`.

## Final sprite assignments (corrected 2026-04-11)

In **single-line PM DMA mode** (DMACTL bit 4 set), with
**PMBASE = `$08`** (ANTIC register `$D407`), the P/M RAM regions are:

| Region                 | Sprite               | Role                                                                   |
|------------------------|----------------------|------------------------------------------------------------------------|
| `$0B00-$0BFF` bits 0-1 | M0                   | **ACTIVE.** Bits set conditionally by `frame_sync.asm` at $AEF1 (`lda $0B00,X; and #$FC; ldy $C1; beq skip; ora #$03; sta $0B00,X`). Drives a moving dot whose X position comes from `$C1` (HPOSM0 ← $C1 in the DLI at $B561). Color from COLPM0 which is **cycled** every frame from `player_color_cycle[(frame_counter>>3)&7]` at frame_sync.asm. Likely the player's in-flight bullet (with M1 being the muzzle template) or an enemy projectile — exact role TBD. |
| `$0B00-$0BFF` bits 2-3 | M1                   | **The player's bullet at-rest template** (bits 2-3 of each byte). 10-byte block at $0BCC,Y written by `draw_bullet_pmg` at $B14B. M1 inherits color from COLPM1 which is set to $1E in the river-zone DLI at $B697. |
| `$0B00-$0BFF` bits 4-5 | M2                   | **The jet's narrow hit probe.** HPOSM2 = `player_sprite_x + 4` written every frame by the playfield DLI at $B539. M2 vs P3 collisions captured into $1C at $B5EA / $B727 and consumed by entities.asm at $A4B6 — provides a narrower (more forgiving) collision hitbox than P2's full 16-pixel sprite. The actual M2 bit writer in the DMA buffer has not yet been located in static analysis but the design implies it must exist. |
| `$0B00-$0BFF` bits 6-7 | M3                   | **Genuinely unused.** No writers found for bits 6-7 in static analysis. HPOSM3 not positioned by the DLI. Not yet bridge-verified. |
| `$0C00-$0CFF`          | P0                   | **Genuinely unused in playfield.** DMA buffer actively zeroed each frame by the indirect copier at $B012. HPOSP0 ← $3C = 0 hides it. P0 has no bitmap; the playfield DLI does write HPOSP0 but the source is always 0. |
| `$0D00-$0DFF`          | P1                   | **Genuinely unused in playfield.** DMA buffer never written; HPOSP1 ← $39 positions a blank sprite. COLPM1 is used only because M1 missiles inherit color from their parent player register. |
| `$0E00-$0EFF`          | P2                   | **The player's jet.** HPOSP2 ← `$57` (`player_sprite_x`). |
| `$0F00-$0FFF`          | P3                   | **Multiplexed enemies.** The WSYNC loop at `$B567` rewrites HPOSP3 per scanline. |

`GRACTL = $03` (both players and missiles DMA-enabled).
`PRIOR = $01` (players front of playfield).

## The M0 motion loop (newly traced 2026-04-11)

`$00C1` is **not** a "terrain draw flag" (its old project.json label).
It is the M0 sprite's X coordinate AND its visibility gate, packed
into one byte. Three pieces of evidence:

1. **HPOSM0 source.** `dli.asm:61` does `lda $C1; sta HPOSM0` every
   frame in the playfield DLI baseline. So whatever value `$C1`
   holds is M0's screen X.

2. **DMA buffer gate.** `frame_sync.asm` at $AEF1 contains:
   ```
   ldx frame_ctr_copy        ; X = $C2
   cpx #$1C
   bcc skip                  ; only if frame_ctr_copy >= $1C
   lda $0AFE,X               ; clear M0 bits two bytes back
   and #$FC
   sta $0AFE,X
   lda $0AFF,X               ; clear M0 bits one byte back
   and #$FC
   sta $0AFF,X
   lda $0B00,X               ; this byte: clear, then maybe set
   and #$FC
   ldy $C1
   beq leave_clear           ; if $C1 == 0, leave M0 bits zero
   ora #$03                  ; else set M0 bit 0-1 = $03 = M0 ON
   leave_clear:
   sta $0B00,X
   ```
   So when `$C1 != 0`, M0 has bits set at $0B00+frame_ctr_copy, with
   the previous two bytes cleared (a fading 2-byte trail).

3. **Motion loop.** `bullets_terrain.asm:6-65` reads `$C1`, branches
   on whether M0 is currently active, advances `$C1` by `$C0`
   (direction byte: $02 or $FE), and stops when `$22` (M0 vs PF0-1
   collision capture) indicates the M0 dot has hit a bank or bridge.

The COLPM0 register is **not** stuck at boot value as the original
analysis assumed. `frame_sync.asm` at $AEF6 explicitly does:
```
lda frame_counter
lsr / lsr / lsr
and #$07
tax
lda player_color_cycle,X
sta COLPM0
```
i.e. cycles through 8 hues from `player_color_cycle` ($BAC5-$BACC)
every frame. Combined with the moving M0 dot, this produces a
multi-color pulsing element somewhere on the screen. The exact
on-screen meaning (bullet? enemy missile? scoring effect?) is the
top open question for the next bridge investigation.

## The M2 jet hitbox probe (newly characterized 2026-04-11)

The DLI at $B534 stores `player_sprite_x` to HPOSP2 (the jet) and
then immediately at $B539 does:
```
clc
adc #$04
sta HPOSM2
```
positioning M2 four pixels to the right of the jet, every frame.
This is not a dead store. The M2 vs P3 collision (captured at $B5EA
into `$1C` and consumed at $A4B6 by `entities.asm`) provides a
secondary collision channel for the jet — narrower than P2's full
16-pixel sprite, so enemies have to overlap the jet's cockpit, not
just its wingtips, to trigger this capture.

The M2 bit slice of the missile DMA buffer must therefore have bits
set somewhere (at the jet's Y row), but no static writer has been
found. Two hypotheses:

- A writer exists in code we haven't fully traced (maybe inside the
  jet draw routine that fills `$0E00,Y`).
- M2 bits get there indirectly via the same `sta $0B00,X` paths
  whose `and #$FC` happens to leave M2 bits intact when they were
  pre-set elsewhere.

A bridge runtime DMA sweep over `$0B00-$0BFF` bits 4-5 during active
gameplay will reveal where the M2 bits live and how they get written.

## Where the bullet is drawn

```
$B14B  ldx #$09            ; 10 iterations
$B14D  ldy #$09            ; PAL starting Y
$B14F  lda pal_flag ($BF)
$B151  bne $B155           ; if PAL (≠ 0), skip the NTSC override
$B153  ldy #$10            ; NTSC starting Y
$B155  lda #$0C            ; the bullet byte: %00001100 → bits 2-3 = M1
$B157  sta $0BCC,Y         ; write into missile DMA
$B15A  dey
$B15B  dex
$B15C  bpl $B157
```

`$0C = %00001100`: only bits 2 and 3 are set. In the single-line
packed missile DMA encoding, bit pairs map to missiles as:

```
   bit: 7 6 5 4 3 2 1 0
        M3  M2  M1  M0
```

so a byte of `$0C` lights **M1** and nothing else. The loop writes 10
of them, which on NTSC targets `$0BD3-$0BDC` (Y from `$10` down to
`$07`) and on PAL targets `$0BCC-$0BD5` (Y from `$09` down to `$00`).
The difference is the vertical offset of M1 within the missile DMA
buffer — the PAL frame has more scanlines so the bullet sits lower.

Verified via runtime DMA sweep: 10 bytes of `$0C` accumulate in
`$0BD3-$0BDC` while fire is pulsed; zero bytes with fire released.
Verified via byte-search in RAM: `8x$0C` run found at `$0BD3`.

## Where the bullet is *positioned* and *coloured*

| What      | Address       | Source                                                                  |
|-----------|---------------|-------------------------------------------------------------------------|
| M1 X pos  | `$B52C` (DLI) | `HPOSM1 = ($5B>>3) + $5C` — computed from ZP `$5B` (shift source) and `$5C` (base) |
| M1 colour | `$B697` (DLI) | `COLPM1 = $1E` — set at river-zone DLI. M1 inherits P1's colour register. The baseline `COLPM1 = 0` at `$B541` keeps M1 hidden above the river. |

## What `$39` (`bullet_x`) actually does

`$39` is written to `HPOSP1` at `$B54F` every frame. Its value is
oscillated by `$A25B inc $39` / `$A25F dec $39` under a carry-flag
condition so it drifts ±1 per frame. **It does not position the
bullet.** The bullet's position is `HPOSM1`, computed from `$5B/$5C`.
Because P1's DMA buffer is never populated, the `HPOSP1 ← $39` store
has no visible effect. `$39`'s actual role is not yet characterised;
the `bullet_x` label is retained as legacy.

## What P0 does

Nothing visible in the playfield zone. The playfield DLI writes
`HPOSP0 ← $3C` at `$B559`, but `$3C = 0` during gameplay, so P0 is
always at X=0 and off-screen. The P0 DMA buffer `$0C00-$0CFF` is
actively zeroed each frame by a **generic indirect byte-copy loop** at
`$B012`:

```
$B012  lda ($a1),Y
$B014  sta ($a3),Y
$B016  dey
$B017  bpl $B012
$B019  rts
```

This routine is not P0-specific — the caller stashes any source and
destination pointer into ZP `$a1/$a2` and `$a3/$a4` and jumps here. One
of its callers uses it to zero the P0 DMA region (confirmed by a
write-watchpoint halting with A=`$00`, `$a3/$a4 = $0C7E`). My earlier
static scan missed this because `sta ($a3),Y` is a **2-byte indirect
instruction with no absolute address encoded in the opcode bytes** —
static byte-pattern search can only find 3-byte absolute stores.

## What was wrong in the earlier analysis

Older versions of this project state several things that turned out to
be incorrect:

1. **PMBASE claimed as `$06`.** It is `$08`. ANTIC reports
   `PMBASE = $08` at runtime.
2. **PMG layout listed as `$0600-$09CF`.** That would be
   double-line mode at PMBASE `$06`. The game uses **single-line
   mode at PMBASE `$08`**, so the layout is `$0B00-$0FFF`.
3. **"Jet is on P0"** or **"P0 is the player plane"** — no. The jet
   is on **P2**, verified by the DLI stores and by bridge runtime
   snapshot (`HPOSP2 == $57` during gameplay).
4. **"Bullet is drawn into P1's bitmap"** — no. P1's DMA buffer is
   never written. The bullet is a **packed missile (M1)**.
5. **"Bit 3 of M2PL detects bullet-hits-enemy"** — no. Bit 3 of M2PL
   is *M2-vs-P3*, which under the corrected sprite map is the
   jet hit-probe missile M2 touching an enemy P3. The real
   bullet-hit-enemy detection appears to use a software bullet_x
   range check at `entities.asm:400-450`, not a hardware collision
   read.
6. **"P0 = terrain marker" or "P0 = riverbank decoration"** — no.
   P0 has no visible role during gameplay. I had invented that
   explanation to account for the `HPOSP0 ← $3C` store without
   checking that `$3C = 0`.

### Additional mistakes found 2026-04-11 during DUO port

The original "Final sprite assignments" table claimed M0/M2/M3 were
"cleared" / unused and described the layout as if only M1 mattered
in the missile slot. This was wrong:

7. **"M0/M2/M3 cleared"** — no. M0 has an active writer in
   `frame_sync.asm` at $AEF1 that conditionally sets M0 bits at
   `$0B00+frame_ctr_copy` based on `$C1`, AND COLPM0 is cycled
   every frame from `player_color_cycle`. M2 is positioned every
   frame at `player_sprite_x + 4` by the DLI at $B539 and its
   collision capture into `$1C` is consumed by `entities.asm` at
   $A4B6. Only M3 appears genuinely free in static analysis.
8. **`$00C1` labelled `terrain_draw_flag`** — no. It is the M0
   sprite's X position AND its visibility gate (HPOSM0 ← $C1 in
   the DLI at $B561; the M0 bit-set in frame_sync.asm is gated by
   `ldy $C1; beq skip`). See "The M0 motion loop" section above.
9. **`$002A` labelled `lives_count`** — no. It is a countdown
   timer that triggers `player_death` when exhausted. Decremented
   every 8 frames at `entities.asm:89-103`, reset to `$25` (37) by
   the death hook at $A6D5. The real visible lives counter is
   somewhere else.
10. **`$A688 player_death`** — no, this is just a 5-instruction
    hook. It does `lda $09; and #$01; sta player_number; ldx #$79;
    jmp state_start_game`. The real death handler is scattered
    across `entities.asm` (the `$1B`/`$1D` read branches at
    `check_entity_collision`), `state_dying_update` at $A7FB, and
    several routines those call.
11. **`$B05C check_terrain_collision`, `$B07C process_collision_results`,
    `$B1C0 play_score_sound`, `$B21A update_lives_display`,
    `$B221 clear_status_line`, `$B223 write_status_text`** — all
    misnomers. The first two are entry points into a shared
    digit-rendering helper at $B086 that reads BCD bytes from
    `temp_ptr_lo,X` and writes screen RAM at `$1000+`. The next
    two manipulate `fuel_level`, `lives_count`, `frame_counter`,
    and the M-DMA buffer but touch no POKEY register and no lives
    display. The last two are an M-DMA byte clear helper that
    zeros 8 consecutive missile bytes at `$0B01-$0B08+X`. Real
    purposes need bridge tracing.
12. **`$0018 frame_counter`** — suspect. Explicitly set to `$FF`
    by sound.asm at $B21A from a non-VBI path. Not a simple
    monotonic counter.
13. **`$0076 fuel_level`** — partially suspect. The routine at
    $B200 (which $B1C0 falls into) stores `$FE` here from a
    non-fuel code path. Either dual-purpose or mislabelled.

## Methodology

This is how the true map was established, so future investigations
don't waste the same time:

1. **Don't trust a single ZP variable as a gameplay detector.** `$57`
   being non-zero does not mean the game has launched the plane — the
   value is initialised to `$7C` (screen centre) during the title
   screen too. Use a **visual** detector instead: classify the pixels
   in `rawscreen()` by colour, and look for **green riverbank pixels**
   in the middle of the playfield area. Title and "press fire to
   launch" states have no green.

2. **Watchpoints beat static scanning.** The bridge's
   `watch_set(addr, mode="w")` pattern — set watchpoint, `resume()`,
   poll `regs()` for a cycle stall, read PC, disasm backward — caught
   every indirect store that static memsearch for `8D/8E/8C/9D/99 xx yy`
   could not. Specifically, the `sta ($a3),Y` writer to P0 DMA and
   the `sta $0BCC,Y` M1 bullet writer were both invisible to static
   scans but unambiguous to runtime watches.

3. **ORed runtime DMA sweeps.** Sample each P/M DMA buffer every
   frame for N frames, OR all the bytes together. Anything that was
   ever drawn into that sprite, even briefly, accumulates into the
   OR result. A delta between a "before fire" sweep and a "during
   fire" sweep isolates what fire specifically contributes.

4. **Bit-slice analysis of missile DMA.** The packed missile DMA
   layout means a byte pattern like `$0C = %00001100` tells you
   *which missile* is being drawn just from the bit positions —
   bits 0-1 = M0, 2-3 = M1, 4-5 = M2, 6-7 = M3. The 10-byte `$0C`
   band was immediately identifiable as M1 without having to chase
   the writer code.

5. **Literal constant scan for the bit pattern.** Once the bullet
   byte `$0C` was known, searching `$A000-$BFFF` for `a9 0c` (LDA
   #$0C) near a `xx 0b` reference instantly pointed at `$B155` and
   the full 12-instruction draw routine. This is the fastest way to
   close the loop between "I see this pattern at runtime" and
   "here's the code that drew it."

6. **Current bridge API gaps to be aware of.**
   - `history()` returns "no instructions recorded yet" because the
     bridge never calls `ATCPUEmulator::SetHistoryEnabled(true)`.
     All investigations here used `disasm(pc-4, 6)` with an alignment
     walker to reconstruct the offending instruction instead of relying
     on history ring-buffer.
   - `frame(N)` and breakpoints do not cooperate: a watchpoint firing
     mid-frame pauses the sim but the frame gate stays armed, and
     every subsequent command in the recv buffer deferred behind the
     gate — effectively a deadlock. Workaround: use `resume()` +
     polling `regs()` for cycle-stall instead of `frame(N)` while
     watches are armed.
   - No pause-reason channel: had to infer "halted by watchpoint"
     from heuristic cycle stall instead of a proper event stream.

   All three are worth fixing as a separate bridge ergonomics task,
   but none were blockers once the workarounds were understood.

## Live gameplay verification

After fixing two bridge bugs (`SetFrameSkip(true)` so the simulator
actually runs at real time, and using `--machine=800xl --no-basic
--memory=64k` so the XEX actually loads — `--machine=800` is broken
for XEX files in headless mode), I reached fully active player mode
with the Activision logo + three jet icons at the bottom of the
status bar (replacing the attract-mode `RIVER RAID by Carol Shaw`
scroller). In that state, with fire held, the M1 DMA band behaved
like this frame-by-frame:

```
frames 1-4:   $0BCC..$0BD5  (10 bytes) — at-rest bullet template
frames 5-13:  $0BA9..$0BD5  (18 bytes) — bullet launched, top at $A9
frames 14-20: $0BA3..$0BD5  (18 bytes) — bullet advanced, top at $A3
```

Top edge migrated $CC → $A9 → $A3 over 20 frames = **~2 scanlines
per frame upward** = a visible bullet traveling from the jet toward
the top of the screen. This is the final, empirical confirmation
that the bullet is drawn as **M1**.

Two observations from the motion data:

1. The 10-byte at-rest template at `$0BCC-$0BD5` is always present
   (even with fire released) and corresponds to `draw_bullet_pmg`
   at `$B14B` writing the fixed `$0C` pattern. That's the muzzle /
   chambered bullet sitting just above the jet.
2. The in-flight bullet `$0BA9..$0BCB` area is emitted by a
   **different draw path** (not yet traced) that writes at an
   animated Y offset. The `$B14B` routine alone only writes the
   fixed 10-byte at-rest block, so there must be a second writer
   (probably keyed off a `bullet_alive` flag that is set on fire
   press and decrements the in-flight bullet's Y each frame).

## Open questions

- **Where is the in-flight bullet draw code?** `$B14B` only produces
  the at-rest 10-byte M1 block; the moving bullet bytes at lower
  Y addresses come from somewhere else. PARTIAL ANSWER (2026-04-11):
  `frame_sync.asm` at $AEF1 writes M0 bits at `$0B00+frame_ctr_copy`
  gated by `$C1`, and the motion loop in `bullets_terrain.asm:6-65`
  drives `$C1`. The "moving bullet" the original analysis observed
  may actually be M0, with M1 being only the muzzle template. To
  confirm, run a frame-by-frame DMA sweep separately on M0 bits
  (bit pair 0-1) and M1 bits (bit pair 2-3) with fire held — see
  if it's M0 that walks upward, M1 that walks upward, or both.
- **`$0024`** is not actually the game-state machine — runtime
  values (`$00/$48/$80`) don't match the 0/1/2/3 semantics in the
  old project.json comment, and the attract → player transition
  happens without `$24` changing. The real play-state variable is
  somewhere else and needs to be located.
- **What `$39` actually represents** given that it's not the bullet
  X (bullet X comes from `HPOSM1 = ($5B>>3)+$5C` at `$B52C`). The
  `HPOSP1 ← $39` store at `$B54F` appears to be vestigial / dead
  since P1's bitmap is never populated.
- **M1PL / M1PF collision reads** — the hardware collision bit for
  bullet-hit-terrain should come through `M1PF` ($D001 read). The
  DLI doesn't currently read `$D001`. PARTIAL ANSWER (2026-04-11):
  bullet-vs-enemy detection appears to be a software AABB check at
  `entities.asm:400-450` (compare `bullet_x` against
  `entity_xpos_tbl,X`) rather than a hardware collision read. This
  is open question O14 in the DUO design doc.
- **What `$3C` does outside the attract state** where it's
  consistently `0`. It's probably the terrain scroll accumulator
  (per the `terrain_bank_update` comment at `$A652`) but I haven't
  captured non-zero values during active scrolling.
- **Where do M2 bits actually get written?** The M2 hit probe
  pattern from §"M2 jet hitbox probe" needs a bitmap somewhere in
  `$0B00-$0BFF` bits 4-5 to ever fire. Static analysis hasn't
  found the writer. Bridge sweep needed.
- **What is the visible meaning of the M0 motion + COLPM0 cycle?**
  Is it the player's bullet, an enemy bullet, an explosion, a
  fuel-marker animation? Watch the M0 bit progression and the
  on-screen pixel at HPOSM0 frame-by-frame to identify the visual.

## Original (incomplete) sprite table — what was missed

For historical reference, the original "Final sprite assignments"
table (now superseded by the corrected version above) listed:

| Region          | Sprite               | Role (as originally claimed)                                           |
|-----------------|----------------------|------------------------------------------------------------------------|
| `$0B00-$0BFF`   | Missiles (packed)    | M1 = the player's bullet (bits 2-3). **M0/M2/M3 cleared.** ❌          |
| `$0C00-$0CFF`   | P0                   | Unused in playfield. Buffer zeroed each frame. ✓                       |
| `$0D00-$0DFF`   | P1                   | Unused in playfield. ✓                                                 |
| `$0E00-$0EFF`   | P2                   | The player's jet. ✓                                                    |
| `$0F00-$0FFF`   | P3                   | Multiplexed enemies. ✓                                                 |

The "M0/M2/M3 cleared" claim was based on inspecting only the
explicitly-located bullet writer at $B14B (which writes M1) and not
finding any other writers via static byte-pattern search. The
actual writers — the M0 bit conditional in `frame_sync.asm` at
$AEF1, and the implicit M2 writes — were missed because the
former uses an indirect read-modify-write pattern (`lda; and; ora;
sta` rather than a single `sta #imm`) and the latter is implicit
(M2 bits live in the missile DMA buffer alongside M1, set somehow
by code that hasn't been traced). The lesson for future analyses
is to **always combine static analysis with runtime DMA sweeps**:
the static pass tells you "where could a writer be", the runtime
pass tells you "where bits actually live". Both are needed.

## Bridge configuration that actually works

For anyone re-running this investigation from scratch, these are
the prerequisites that took way more time than they should have to
nail down:

1. **Bridge binary**: use
   `src/AltirraBridgeServer/AltirraBridgeServer` from the repo build,
   not an older copy. The older builds don't support the CLI flags
   below.
2. **CLI flags**:
   `--bridge=tcp:127.0.0.1:0 --no-basic --machine=800xl --memory=64k`.
   `--machine=800` has a boot bug in headless mode (XEX files do not
   load — all RAM stays at the uninitialised `ff 00` pattern) that
   `800xl` does not. I did not trace the root cause; `800xl` is the
   documented-working combination.
3. **Settings.ini**: because `InitSimulator()` calls `g_sim.LoadROMs()`
   *before* `ATSettingsLoadLastProfile()`, any BASIC / kernel /
   memory setting in `~/.config/altirra/settings.ini` gets applied
   too late and silently has no effect at runtime. CLI flags run
   after settings and trigger a proper `ColdReset()`, so they are
   the reliable configuration path. This ordering is a latent
   bridge-server bug worth fixing separately.
4. **`SetFrameSkip(true)`** in `main_bridge.cpp:325` — without this
   the simulator runs at ~5% of real time because nothing drains
   the null display's `PostBuffer` queue, so GTIA stalls every
   frame. This was fixed as part of this investigation.
5. **Timing**: at real sim speed, River Raid needs **roughly 20-35
   real seconds** of wall clock between cold boot and active player
   mode (attract intro plays the `RIVER RAID by Carol Shaw` banner
   for a long time before the state machine accepts START). Any
   script that allocates "3 seconds per stage" is too impatient.
