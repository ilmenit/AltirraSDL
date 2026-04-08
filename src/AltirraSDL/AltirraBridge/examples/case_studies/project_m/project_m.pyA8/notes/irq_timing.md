
CRITICAL: The POKEY timer sync at 798-7C7 is the most timing-sensitive
code in the entire program. It uses SKCTL reset + 10 NOPs + STIMER to align
Timer 4 underflow to scanline boundaries. Any emulator that doesn't handle
this sequence with cycle accuracy will produce incorrect display output.

Timer 4 parameters: AUDF4=/bin/bash1, AUDCTL=/bin/bash1 (15KHz base for ch1-2)
Timer 4 uses 64KHz clock → period ≈ 224 cycles ≈ 2 scanlines
4 IRQ stages × 16 iterations = 128 scanlines = full 3D viewport
