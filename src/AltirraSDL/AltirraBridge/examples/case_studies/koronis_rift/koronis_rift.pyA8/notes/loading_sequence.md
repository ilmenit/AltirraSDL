# Loading Sequence Analysis

## Overview
Koronis Rift 128K uses a complex XEX loading scheme with:
- Direct bank loading via PORTB writes in XEX segments
- LZW-style decompression during INI routines
- A title screen shown mid-load with cold restart to continue

## Banking Scheme (130XE)
- PORTB $FF = main memory
- PORTB $E7 = bank 1 (bits 3-2 = 01, bit 4 = 0 for CPU access)
- PORTB $EB = bank 2 (bits 3-2 = 10)
- PORTB $EF = bank 3 (bits 3-2 = 11)

## Decompressor
At $B504-$B658. Parameters at $B500-$B503 = {start_lo, start_hi, end_lo, end_hi}.
Updated before each compressed segment load.

## Title Screen (Seg 6 INI at $33AA)
- Saves zero page $50-$FF
- Sets up display, decompresses title graphics
- Sets VBI to $35BD for animation/sound
- Waits for keypress (CMP CH loop at $2FEA)
- JMP COLDSV to cold restart and continue loading

## OS in RAM
The game runs with OS ROM disabled (PORTB bit 0 = 0).
Custom OS code at $C000+ handles NMI/IRQ dispatch, VBI, clock, etc.
At runtime, PORTB = $5C (bank 3 visible to ANTIC, OS ROM off).

## Key Segments
- Seg 0-5: Setup (PORTB, DMACTL, COLDST)
- Seg 6: Title screen (destroyed by seg 25)
- Seg 7: Decompressor at B504-B658
- Seg 8,19,22,24: Decomp params at B500-B503
- Seg 9: Display list + screen data (compressed)
- Seg 10: Stack area
- Seg 11: Main bank data at $4000-$7723
- Seg 12-17: Bank 1-3 data with PORTB switching
- Seg 18-20: Additional data + decompression
- Seg 21-23: More data with PORTB switching
- Seg 25: Main game code $3680-$87CB (final, compressed)
