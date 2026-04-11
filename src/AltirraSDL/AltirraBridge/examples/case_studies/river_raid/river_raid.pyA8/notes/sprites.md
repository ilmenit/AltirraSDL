# River Raid — Sprite map (verified via bridge)

Previous passes of this project misidentified several of the game's
sprite assignments (they had the jet on P0, the bullet on P1's bitmap,
and listed a stale `$0600`-based PMG layout). This note captures the
**verified** sprite map after an empirical bridge investigation and
should be treated as the source of truth for any future analysis.

## Final sprite assignments

In **single-line PM DMA mode** (DMACTL bit 4 set), with
**PMBASE = `$08`** (ANTIC register `$D407`), the P/M RAM regions are:

| Region          | Sprite               | Role                                                                   |
|-----------------|----------------------|------------------------------------------------------------------------|
| `$0B00-$0BFF`   | Missiles (packed)    | **M1 = the player's bullet** (bits 2-3 of each byte). M0/M2/M3 cleared. |
| `$0C00-$0CFF`   | P0                   | **Unused in playfield.** DMA buffer actively zeroed each frame.        |
| `$0D00-$0DFF`   | P1                   | **Unused in playfield.** DMA buffer never written; HPOSP1 positions a blank sprite. COLPM1 is used only because missiles share it with their parent player — i.e. *it's actually the M1 bullet color.* |
| `$0E00-$0EFF`   | P2                   | **The player's jet.** HPOSP2 ← `$57` (`player_sprite_x`).              |
| `$0F00-$0FFF`   | P3                   | **Multiplexed enemies.** The WSYNC loop at `$B567` rewrites HPOSP3 per scanline. |

`GRACTL = $03` (both players and missiles DMA-enabled).
`PRIOR = $01` (players front of playfield).

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
   is *M2-vs-P3*, which under the corrected sprite map is a jet-helper
   missile M2 touching an enemy P3. The real bullet-hit-enemy
   collision would be `M1PL` read, which the game may or may not
   actually check.
6. **"P0 = terrain marker" or "P0 = riverbank decoration"** — no.
   P0 has no visible role during gameplay. I had invented that
   explanation to account for the `HPOSP0 ← $3C` store without
   checking that `$3C = 0`.

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

## Open questions

- **What actually happens when the player dies or the plane reaches
  a new section?** The game was repeatedly observed in a "waiting to
  launch" state (status bar drawn, playfield black) rather than active
  gameplay with river scrolling and enemies visible. Either the input
  sequence to fully enter gameplay is more complex than
  `START → wait → FIRE`, or some state preconditions are not met
  after `a.boot()`. Need a clean reboot with clearly-confirmed active
  gameplay (jet visible mid-river, enemies moving) before re-probing.
- **What `$39` actually represents** given that it's not the bullet X.
- **What `$3C` does outside the gameplay state** where it might be
  non-zero.
- **Whether any code path ever writes non-zero data into P1 DMA**
  during a gameplay state we haven't reached. The watchpoint on
  `$0D00/$0D40/$0D80/$0DC0` for 4 s each caught no writes, but in
  the "waiting to launch" state.
- **M1PL / M1PF collision reads** — if the game checks bullet-hits
  via M1's collision registers (the only ones that track missile
  collisions independently of the parent player), where exactly is
  that read? No watch has been set on those registers yet.
