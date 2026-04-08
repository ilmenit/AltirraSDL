# IRQ Architecture — POKEY Timer Scanline Chain

This is the most critical and emulator-sensitive part of Project-M.
The game uses **POKEY Timer 4 IRQs** to switch the GTIA PRIOR register
at precise scanline intervals, creating a raster effect that alternates
between GTIA display modes on consecutive scanline pairs.

## Why this matters for emulator compatibility

If the POKEY timer period or IRQ latency is not cycle-exact, the PRIOR
register switches happen at the wrong scanline positions, causing the
display to appear corrupted (wrong colors, shifted graphics, or no
visible image at all). This is the #1 reason the game fails on some
emulators.

## OS ROM is disabled

PORTB = $FE (bit 0 = 0) → OS ROM at $C000-$FFFF is replaced by RAM.
The game provides its own interrupt handlers in RAM:

```
$FFFA-$FFFB (NMI vector)  → $91C1  (immediate VBI handler)
$FFFC-$FFFD (RESET vector)→ $0000
$FFFE-$FFFF (IRQ vector)  → $9100  (first IRQ handler in chain)
```

The OS vectors at $0200-$0224 are NOT used — the hardware vectors point
directly to the game's handlers.

## POKEY Timer Setup ($3789-$37C7)

The timer synchronization at startup is critical for correct operation:

```asm
; Timer 4 setup
LDA #$01
STA AUDF4    ; $D206 — period = 1 (fastest)
LDA #$01
STA AUDCTL   ; $D208 — 15KHz clock for channels 1-2

; Synchronize POKEY timer to scanline boundary
STA WSYNC    ; wait for horizontal blank
STA WSYNC
LDA VCOUNT   ; read current scanline
wait:
STA WSYNC
CMP VCOUNT   ; wait for VCOUNT to increment
BEQ wait     ; (VCOUNT changes every 2 scanlines)
STA WSYNC    ; align to new scanline pair start
STA WSYNC

; Reset and restart POKEY
LDA #$00
STA SKCTL    ; $D20F — stop all POKEY timers
; 10x NOP      — precise delay for timer reset
LDA #$03
STA SKCTL    ; $D20F — restart POKEY
LDA #$01
STA STIMER   ; $D209 — trigger all timers to start NOW
CLI          ; enable interrupts
```

**Why this is fragile**: The 10 NOPs provide a precise delay between
stopping and restarting POKEY. If the emulator doesn't handle SKCTL
reset timing correctly (or STIMER doesn't synchronize timer underflow
to the current cycle), the timer will drift relative to scanlines and
the PRIOR switches will appear at wrong positions.

## IRQ Handler Chain ($9100-$91C0)

Only the LOW BYTE of the IRQ vector ($FFFE) is modified — the high
byte stays $91 throughout. The chain has 5 stages:

### Stage 0: $9100 (entry point)
```asm
STA $03          ; save A
LDA #$E1
STA PRIOR        ; GTIA mode 3 (16 luminances) + priority
LDA #$00
STA IRQEN        ; acknowledge IRQ
LDA #$04
STA IRQEN        ; re-enable Timer 4 IRQ
LDA #$26
STA $FFFE        ; → next IRQ goes to $9126
LDA VCOUNT       ; read scanline counter
STA COLPM3       ; write to player 3 color (raster effect)
LDA $FF          ; timing pad
LDA #$61
STA PRIOR        ; GTIA mode 2 (9 colors) + priority
LDA $03          ; restore A
RTI
```

### Stage 1: $9126
Same as stage 0 but:
- Chains to $914A (STA $FFFE with #$4A)
- Uses 4 NOPs instead of VCOUNT/COLPM3 for timing

### Stage 2: $914A
Same as stage 0 but:
- Chains to $9170 (STA $FFFE with #$70)
- Reads VCOUNT → COLPM3 (raster bar on player 3)

### Stage 3: $9170 (loop controller)
- Decrements counter at $04
- If counter > 0: chains back to $9100 (loop for next scanline group)
- If counter = 0: chains to $91A3 (bottom of viewport)
- Uses NOPs for timing when looping

### Stage 4: $91A3 (viewport bottom — switch to text mode)
```asm
STA $03
LDA #$21
STA PRIOR        ; normal hi-res mode (no GTIA)
LDA #$00
STA IRQEN        ; disable timer IRQ (no more switches needed)
STA $FFFE        ; reset IRQ vector to $9100 for next frame
LDA #$0E
STA COLPF1       ; white text
LDA #$34
STA COLPF2       ; dark blue background
LDA $03
RTI
```

## NMI Handler ($91C1) — VBI

The NMI handler has two stages:

### Immediate VBI: $91C1
```asm
STA $00          ; save A (only register saved)
LDA #$04
STA IRQEN        ; enable Timer 4 IRQ for new frame
LDA #$10
STA $04          ; reset scanline counter to 16
LDA #$D4
STA $FFFA        ; point NMI to deferred handler ($91D4)
LDA $00          ; restore A
RTI
```

### Deferred VBI: $91D4
```asm
STA $00 / STX $01 / STY $02   ; save all registers
LDA #$02  → STA COLPF1        ; set viewport colors
LDA #$EC  → STA COLPF2
INC $13                        ; increment frame sub-counter
JSR $3B66                      ; FPS calculation + fade effects
INC $14                        ; increment frame counter
; Music/sound dispatch based on $89 (trigger) and $8B (state)
LDA #$C1
STA $FFFA                      ; restore NMI to immediate handler
LDA $00 / LDX $01 / LDY $02   ; restore registers
RTI
```

## The PRIOR Toggle Effect

The IRQ chain creates this pattern on every frame:

```
Scanlines 0-30:   Blank lines (display list blanks)
Scanline ~31:     IRQ enables, PRIOR = $E1 (GTIA mode 3)
                  Then PRIOR = $61 (GTIA mode 2)
  Stage 0 ($9100): PRIOR $E1 → $61, chain to Stage 1
  Stage 1 ($9126): PRIOR $E1 → $61, chain to Stage 2
  Stage 2 ($914A): PRIOR $E1 → $61, chain to Stage 3
  Stage 3 ($9170): PRIOR $E1 → $61, dec counter, loop to Stage 0
  ... repeats 16 times (4 stages × 16 = 64 PRIOR toggles) ...
  Stage 3 final:   counter=0, chain to Stage 4
  Stage 4 ($91A3): PRIOR = $21 (normal mode for text)
                   Disable timer IRQ
Scanlines ~159+:  Status bar (ANTIC mode 2 text, normal colors)
```

Each IRQ handler toggles PRIOR between $E1 and $61:
- **$E1** = bits 7+6 ($C0) + bit 5 ($20) + bit 0 = **GTIA mode 3**
  (16 hues, luminance from COLBK) + multi-color players + priority 1
- **$61** = bit 6 ($40) + bit 5 ($20) + bit 0 = **GTIA mode 1**
  (16 luminance levels, hue from COLBK) + multi-color players + priority 1

In both modes, ANTIC mode F data is interpreted as **nibbles** (4 bits
per pixel), giving **80 pixels** horizontal resolution. Each nibble
value 0-15 selects a hue (mode 3) or luminance (mode 1).

By alternating these modes on consecutive scanline pairs, the engine
displays both hue and luminance information from the same nibble data.
The human eye blends adjacent scanlines, creating a richer perceived
palette than either single GTIA mode can produce ("interlaced GTIA").

The 4 stages per loop iteration correspond to 4 scanline pairs (8 total
scanlines). With 16 iterations, this covers 128 scanlines = the 3D viewport.

## Timing Requirements

For correct display, each IRQ must:
1. Fire within ~2 cycles of the correct scanline position
2. Complete the PRIOR=$E1 write before ANTIC starts fetching that scanline
3. Complete the PRIOR=$61 write at precisely the right cycle within the line
4. The IRQEN=$00/$04 acknowledge must not cause timer period drift

**AUDF4 = $01 with AUDCTL = $01** (15KHz clock):
- Timer 4 period = (AUDF4 + 1) * clock_period
- 15KHz mode ≈ 114 cycles/tick (one scanline)
- With AUDF4=$01: timer fires every 2 × 114 = 228 cycles = every 2 scanlines
- This matches VCOUNT granularity (VCOUNT increments every 2 scanlines)

The 4 chained handlers (stages 0-3) fire on 4 consecutive timer
underflows, covering 8 scanlines (4 VCOUNT values). Then the counter
loop repeats. 16 iterations × 8 scanlines = 128 scanlines = full viewport.
