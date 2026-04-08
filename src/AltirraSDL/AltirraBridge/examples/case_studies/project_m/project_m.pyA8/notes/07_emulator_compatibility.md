# Emulator Compatibility Issues

## The Core Problem

Project-M's display relies on a POKEY Timer 4 IRQ firing at precise
2-scanline intervals to toggle the GTIA PRIOR register. If the timing
is off by even a few cycles, the PRIOR switch happens at the wrong
scanline, causing:

- **Garbled colors** — wrong GTIA mode for the bitmap data
- **Horizontal bands** — PRIOR switches visible as color shifts
- **Blank screen** — if timer never fires or fires too late
- **Flickering** — if timer period drifts over time

## Specific Requirements

### 1. POKEY Timer synchronization via STIMER ($D209)

The game writes STIMER after a careful SKCTL reset sequence:
```
STA SKCTL=0    ; stop all timers
10x NOP        ; precise delay
STA SKCTL=3    ; restart POKEY
STA STIMER=1   ; synchronize all timer counters NOW
```

The emulator must:
- Reset timer counters when SKCTL is written to 0
- Re-arm timers when SKCTL is set back to 3
- **STIMER write must reset all active timer underflow counters** to
  their AUDF period value on the exact cycle of the write

### 2. Timer 4 underflow with AUDF4=$01, AUDCTL=$01

- AUDCTL bit 0 = 1 → 15KHz clock for channels 1&2
- Timer 4 uses the standard 64KHz clock regardless of AUDCTL
- AUDF4=$01 → period = 2 × (64KHz tick) ≈ 28.4μs per tick
- But actual behavior: Timer 4 underflow period = (AUDF4+1) ticks
  of the 1.77MHz clock ÷ some factor

Actually, POKEY timers use a 1.79MHz base clock divided by either
1 (1.79MHz), 28 (64KHz), or 114 (15KHz). Timer 4 with AUDCTL
default uses the 64KHz clock → period = (AUDF4+7) × 28 cycles
= (1+7) × 28 = 224 cycles ≈ 2 scanlines (228 cycles/2 lines PAL).

This near-2-scanline period is what makes the IRQ chain work.

### 3. IRQ acknowledge sequence

Each IRQ handler does:
```
LDA #$00 / STA IRQEN    ; disable all IRQs (acknowledge)
LDA #$04 / STA IRQEN    ; re-enable Timer 4 IRQ only
```

The emulator must properly:
- Clear pending IRQ status when IRQEN is written to 0
- Not re-trigger immediately when IRQEN is set back to $04
- Handle the case where a timer underflow occurs between the two writes

### 4. NMI vector self-modification

The NMI handler modifies $FFFA (NMI vector low byte) at runtime:
- Immediate VBI ($91C1) sets $FFFA to $D4 → deferred ($91D4)
- Deferred VBI ($91D4) sets $FFFA to $C1 → immediate ($91C1)

Similarly, IRQ vector $FFFE is modified every IRQ. The emulator
must read the vector from RAM at the actual moment of the NMI/IRQ,
not cache it.

### 5. RAM under OS ROM

The entire $C000-$FFFF region is RAM (PORTB bit 0 = 0). The emulator
must properly handle:
- Reading vectors from RAM at $FFFA-$FFFF (not from ROM)
- The framebuffer at $D800-$F7FF coexisting with hardware I/O
  (hardware registers at $D000-$D7FF are still active)
- Reading/writing the sine tables at $C000-$C6FF

### 6. PRIOR register timing

When the IRQ writes `STA PRIOR` ($D01B), the new PRIOR value must
take effect on the **current scanline being rendered**. If the write
is delayed even slightly, it affects the wrong scanline.

The sequence in each handler is carefully timed:
```
STA $03          ; 3 cycles (save A)
LDA #$E1         ; 2 cycles
STA PRIOR        ; 4 cycles ← must happen before ANTIC fetch
; ... timing pad ...
LDA #$61         ; 2 cycles
STA PRIOR        ; 4 cycles ← must happen mid-line
```

## Known Working Emulators

- **Atari800** (this project's engine) — cycle-exact POKEY, works correctly
- **Altirra** — cycle-exact everything, works correctly

## Known Issues

- Emulators with simplified POKEY timer handling may show incorrect display
- Emulators that don't synchronize STIMER correctly will show drifting bands
- Emulators that cache NMI/IRQ vectors may miss the self-modification
