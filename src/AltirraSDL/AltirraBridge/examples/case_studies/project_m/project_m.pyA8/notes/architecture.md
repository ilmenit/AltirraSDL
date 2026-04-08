
Project-M 2.0 is a Wolfenstein 3D-style raycasting engine for Atari XL/XE.

Key architectural decisions:
1. OS ROM disabled — game owns - for framebuffer + lookup tables
2. POKEY Timer 4 IRQ chain toggles PRIOR between GTIA mode 2/3 each scanline
3. Double-buffered framebuffer with interleaved layout (32-byte offset)
4. Self-modifying code throughout rendering for speed
5. BSP tree for wall visibility ordering
6. RMT music player runs in deferred VBI

The IRQ architecture is the primary source of emulator compatibility issues.
See notes/02_irq_architecture.md for full details.
