# Koronis Rift — Reverse Engineering Overview

## Game
Koronis Rift by Lucasfilm Games (1985). 128K (64KB base + 64KB extended).
Uses 130XE banking scheme with 4 banks.

## Status
- Byte-exact MADS reconstruction: VERIFIED
- 26 XEX segments fully mapped
- Title screen init code analyzed
- Decompressor at $B504-$B658 identified
- OS-in-RAM code at $C000+ identified
- Banking scheme fully decoded

## Architecture
- Runs with OS ROM disabled (PORTB bit 0 = 0)
- Custom OS code in RAM at $C000-$FFFF
- ANTIC banking: PORTB $5C at runtime (ANTIC reads bank 3)
- Main game code at $3680-$87CB (compressed in XEX)
- Display list at $0F00 (ANTIC mode 14 = hi-res bitmap)
- Screen memory at $1010 and $2000 (two screens)

## Loading Sequence
1. Setup: disable display, set PORTB=$FF
2. Title screen INI at $33AA — shows splash, waits for keypress
3. Cold start continues loading
4. Decompressor loaded to $B504-$B658
5. Display list + screen data decompressed at $0F00-$1AF5
6. Bank data loaded directly via PORTB switching:
   - $E7 = bank 1, $EB = bank 2, $EF = bank 3
7. Additional data decompressed via INI=$B504 calls
8. Final segment $3680-$87CB with INI=$B100

## Key Discovery: Multiple INITADs
This game uses multiple INITAD segments per data segment (e.g., INITAD=$B504
followed by INITAD=$2F90). The decompressor at $B504 runs first, then the
actual init routine. This required a loader fix to track all INITADs.
