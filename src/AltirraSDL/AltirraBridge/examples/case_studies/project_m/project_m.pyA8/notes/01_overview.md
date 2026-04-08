# Project-M 2.0 — Overview

**Author**: NRV, 2010
**Platform**: Atari 800XL/XE (PAL/NTSC, 64KB minimum)
**Type**: Real-time 3D raycasting engine (Wolfenstein 3D-style)
**Entry point**: $3660 (RUN address)

## What it does

Project-M is a first-person 3D engine for the Atari 8-bit. It renders a
textured maze in real-time at ~24 FPS (PAL) using raycasting, with a
status bar showing FPS, time, score, coordinates, and a minimap overlay.

The engine uses several advanced techniques:
- **POKEY timer IRQ chain** for scanline-precise GTIA mode switching
- **Double-buffered framebuffer** in RAM under the OS ROM
- **Self-modifying code** throughout the rendering pipeline
- **PAL/NTSC adaptive viewport** — wider on PAL (more cycles available)

## XEX Loading Sequence

```
Seg  0: $4000-$5894 (6293 bytes)  INI=$4000  [overwritten by seg 5]
  → INIT copies data to RAM under OS ROM ($C000-$FFFF, $F800-$FFFF)
  → Contains sine/cosine tables, perspective lookup tables, charset, level data
Seg  1: $2800-$2E50 (1617 bytes)  — RMT music data ("RMT4" header)
Seg  2: $2E51-$2F5C (268 bytes)   — More music data
Seg  3: $3082-$30BF (62 bytes)    — Sound effect data / lookup tables
Seg  4: $3100-$31BF (192 bytes)   — Sound/music data
Seg  5: $3200-$8E94 (23701 bytes) — Main code + data (overwrites seg 0)
Seg  6: $8F00-$90FB (508 bytes)   — Column rendering state buffers
Seg  7: $9100-$922D (302 bytes)   — IRQ/NMI handlers + lookup tables
Seg  8: $9240-$99F8 (1977 bytes)  — BSP/wall data + display lists + text
Seg  9: $9A00-$BFFF (9728 bytes)  — Second display list + level map data
RUN: $3660
```

## Memory Map

```
$0000-$00FF  Zero page workspace (game variables, pointers)
$0100-$01FF  6502 stack
$2800-$31BF  RMT music data + sound effects
$3200-$3B65  Main loop, input handling, movement, status bar
$3B66-$3BD0  VBI helper routines (FPS counter, fade, vector setup)
$3BD1-$3C5A  Column rendering loop (iterates over viewport columns)
$3C5B-$41F9  Ray setup, coordinate transforms, direction tables
$41FA-$46E1  Raycasting engine (wall detection, distance calculation)
$46E2-$4E00  Column fill (perspective projection, self-modifying code)
$4E01-$50BB  Screen clear / column erase routines
$50BC-$5200  Sky/floor gradient fill
$5200-$52xx  Wall texture column renderer (self-modifying)
$8962-$8B54  Texture lookup tables + buffer pointer setup
$8B55-$8C7E  Input processing (joystick, fire, minimap toggle)
$8C7F-$8DC5  Buffer swap + page setup
$8DC6-$8E11  BSP tree traversal (wall visibility ordering)
$8E12-$8E94  BSP node data
$8F00-$90FF  Per-column state arrays (distance, texture, height)
$9100-$922D  IRQ/NMI interrupt handlers (scanline-timed PRIOR switching)
$9240-$92BF  BSP child pointers + wall lookup tables
$92C0-$98FF  Display list 0 + padding
$9800-$9992  Display list 0 (blanks + 128 mode F lines + text)
$9A00-$9B94  Display list 1 (alternate buffer)
$9B95-$9BFF  Status bar text (ANTIC mode 2)
$C000-$C6FF  Sine/cosine/perspective lookup tables (RAM under ROM)
$CF00-$CF7F  Wall distance/height tables
$D800-$F7FF  Framebuffer (128 scanlines * 64 bytes, double-buffered)
$F800-$FBFF  Custom character set (1024 bytes, at CHBASE $F8)
$FFFA-$FFFF  Hardware interrupt vectors (NMI/RESET/IRQ in RAM)
```
