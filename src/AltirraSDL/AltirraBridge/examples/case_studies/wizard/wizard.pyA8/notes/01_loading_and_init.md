# Loading and Initialization

## XEX Structure
Single segment: $1A90-$6DD7 (21320 bytes). No INI routines, no banking, no
under-ROM code. This is a straightforward single-load game.

## Entry Point ($3C0A)
The RUN address is a one-shot memory copy routine:
- Source: ($B3/$B4) = $1A90 (start of XEX segment)
- Destination: ($B5/$B6) = $0400
- Count: $22 pages = 8704 bytes ($B7)
- Loop: copies 256 bytes at a time, incrementing page pointers

After the copy:
- DOSVEC ($0A/$0B) = $0636 (warm start handler)
- DOSINI ($0C/$0D) = $0636
- MEMORY_REFRESH_CYCLES ($09) = $01
- WARMST ($08) = $FF
- JMP $0917

The critical insight: bytes at XEX offsets $1A90-$3C8F are the runtime code at
$0400-$25FF. The disassembler needs segment source for the XEX but runtime
addresses for labels. This includes the scroll engine ($1900), entity system
($1B00, $2300), map decoder ($091A), and item handlers ($1F00).

## Game Init ($0917)
This is an indirect JMP through ($6C08). On first boot $6C08/$6C09 = $0000,
which falls through to the map decoder. On subsequent calls (warm restart) it
dispatches to the appropriate level init handler via $6B5C table:
- $6B5C: $6500 (level_renderer)
- $6B5E: $09A2 (level_init)
- $6B60: $09AC (level_init_next)

## Hardware Init ($64C0)
Called during level setup:
- Sets VVBLKD to $5A00 via SETVBV (OS call $E45C)
- game_running = 1
- VDSLST = $6880 (DLI handler)
- NMIEN = $C0 (enable VBI + DLI)
- GRACTL = $03 (enable player + missile DMA)
- SDMCTL = $3E (enable standard DMA)
- PMBASE = $10 (PMG at $1000)
- GPRIOR = $01 (players overlay playfield)
- Colors loaded from $6B17 table
- Screen base pointer set to $8A2D

## Map Decoder ($091A)
Decompresses map data stored at $6E00+ into screen memory:
- Source pointer: ($86/$87) starts at $6E00
- Destination pointer: ($88/$89) starts at $04A0
- Processes 128-byte chunks (signed Y loop: 0 to $7F)
- Each source byte's top 2 bits are transformed:
  - If >= $80: add $40, XOR $C0 (rotation)
  - If < $80: add $40
- Then ASL twice into $6C11 accumulator
- Every 4 source bytes produce 1 output byte (4 tiles packed)
- 32 tiles per row ($20), wraps to next row every 32 columns
- Continues through multiple 128-byte pages until $87 reaches $A0
