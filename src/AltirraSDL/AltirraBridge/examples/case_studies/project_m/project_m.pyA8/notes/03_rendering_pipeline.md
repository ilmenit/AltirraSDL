# Rendering Pipeline

## Main Loop ($37DA-$37E4)

The main loop is extremely simple — just two calls to the frame
renderer, alternating a buffer flag:

```asm
main_loop:
  LDA #$80
  JSR render_frame    ; render to buffer 1 ($12=$80)
  LDA #$00
  JSR render_frame    ; render to buffer 0 ($12=$00)
  JMP main_loop
```

There is **no frame sync wait**. The engine renders as fast as possible
(CPU-bound). The FPS counter on screen shows actual throughput (~24 FPS
on PAL). The double buffer prevents tearing.

## Frame Renderer: render_frame ($37E7)

```
$37E7  render_frame:
  STA $12              ; set buffer flag (0 or $80)
  JSR $8B55            ; process_input — joystick, fire, minimap
  JSR $383A            ; update_movement — apply velocity, turning
  JSR $3BD1            ; render_columns — raycasting + drawing (THE HOT PATH)
  JSR $8AAA            ; draw_column_pixels — texture + sky/floor fill
  ; Check console keys (START/SELECT/OPTION → flash COLBK)
  JSR $3B4E            ; (placeholder — just RTS)
  JSR $3B4F            ; flip_display_list — swap DL pointer for double buffer
  JSR $3B5F            ; inc_frame_counter — 16-bit counter at $10/$11
  ; Update status bar: FPS, coordinates, score
  JSR $3B83            ; draw_hex_byte (FPS at column $10)
  JSR $3B83            ; draw_hex_byte (X coord at column $16)
  JSR $3B83            ; draw_hex_byte (Y coord at column $19)
  RTS
```

## Double Buffering ($3B4F)

Two display lists at $9800 and $9A00 share the same structure but
with different LMS (Load Memory Scan) addresses:

```
Display List 0 ($9800):  LMS = $D800, $D840, $D880, ... (stride $40)
Display List 1 ($9A00):  LMS = $D820, $D860, $D8A0, ... (stride $40)
```

The two buffers are **interleaved in memory** with a $20 (32 byte) offset.
Each ANTIC mode F line reads 40 bytes, but the stride is $40 (64 bytes),
so there's a 24-byte gap between scanlines. The second buffer lives in
this gap (starting 32 bytes later in each row).

Buffer flip is done by changing the JMP target in the display list:
- $9809 = high byte of the JMP address at DL offset $0807
- When $12 = 0: $9809 = $98 → JMP $980A (display list 0 active)
- When $12 ≠ 0: $9809 = $9A → JMP $9A0A (display list 1 active)

The renderer writes to the INACTIVE buffer while ANTIC displays the
active one. The `BIT $12` instruction is used throughout the rendering
code to select which buffer offset ($00 or $20) to use.

## Column Rendering Loop: render_columns ($3BD1)

The viewport is divided into columns. PAL mode renders 32 columns
(NTSC would render 27). For each column:

```
$3BD1  render_columns:
  LDA $71          ; first column index
  STA $0A          ; current column
  ; Load column angle from table
  ; Setup ray direction
  JSR $419D        ; setup_ray — compute ray direction, select quadrant
column_loop:
  JSR $439F        ; cast_ray — walk through BSP, find wall hit
  ; Store per-column results:
  ;   $8F00,Y = wall distance (0-32)
  ;   $8F20,Y = texture ID
  ;   $8F40,Y = texture column offset
  ;   ($1C),Y = pixel height for column
  ;   ($20),Y = sky/ceiling height
  INC $0A          ; next column
  CMP $72          ; reached last column?
  BEQ done
  JSR $41FA        ; advance_ray — step to next column angle
  JMP column_loop
done:
  RTS
```

## Raycasting: cast_ray ($439F)

The raycaster uses DDA (Digital Differential Analyzer) to step through
the map grid along the ray direction. It uses extensive lookup tables
at $4A00-$4C09 for precomputed step increments.

Key data structures:
- $32/$37/$38: Ray position (X fractional, X integer, Y)
- $35/$3A/$3B: Step increments (per-column delta)
- $3D/$3E: Map cell coordinates
- $2F: Current map sector index
- $30: Wall type at current position

The raycasting terminates when it hits a wall (non-zero map cell).
It then calls `$8DC6` (BSP traversal) to determine the visible wall
face and compute the exact hit position for texture mapping.

Self-modifying code is used extensively — the addresses at $8DE3/$8DE5/
$8DE6/$8DF5/$8DF7/$8DF8 are patched per-ray to select the correct
comparison tables.

## BSP Traversal: $8DC6

The BSP (Binary Space Partition) tree determines wall visibility order.
Node data is at $9240-$92BF:

```
$9240,X = left child pointer (low)
$9250,X = left child pointer (high) — patched into CMP instruction
$9260,X = right child pointer (low)
$9270,X = right child pointer (high)
$9280,X = "greater" child
$92C0,X = "lesser" child
```

The tree is walked with a maximum depth of 6 levels. The comparison
at each node uses the ray's integer coordinates (self-modified) against
wall boundary values at $CF00,X and $CF40,X.

## Column Drawing: draw_column_pixels ($8AAA)

After raycasting, the second pass draws all columns:

```
$8AAA  draw_column_pixels:
  FOR each column (Y = $71 to $72):
    Load wall distance → compute pixel height
    IF height > 0:
      JSR $50BC      ; draw sky/floor gradient above/below wall
      JSR $5200      ; draw textured wall column
      JSR $89E4      ; plot column to framebuffer
```

The texture renderer at $5200 uses self-modifying code to patch a
JSR target ($5242-$5244) that points to the correct texture strip
routine. Textures are stored as column-major data and indexed by
the texture ID and hit position.

## Profiling Summary (300 frames, PAL)

| Rank | Routine      | Cyc/frame | % Budget | Description |
|------|-------------|-----------|----------|-------------|
| 1    | $8DC6       | 5,897     | 16.6%    | BSP tree traversal |
| 2    | $439F       | 5,495     | 15.5%    | Ray casting (DDA) |
| 3    | $5200       | 4,606     | 13.0%    | Texture column render |
| 4    | $3BD1       | 1,431     | 4.0%     | Column loop + setup |
| 5    | $4C37       | 1,112     | 3.1%     | Perspective projection |
| 6    | $8AAA       | 946       | 2.7%     | Column pixel draw loop |
| 7    | $89E4       | 768       | 2.2%     | Framebuffer plotting |
| 8    | $3303       | 681       | 1.9%     | RMT music player |
| 9    | $41FA       | 521       | 1.5%     | Ray advance per column |
| 10   | $50BC       | 253       | 0.7%     | Sky/floor gradient |
|      | **Total**   | **26,028**| **73.2%**| of 35,568 PAL budget |

The top 3 routines (BSP + raycast + texture) consume 45% of the
frame budget — this is the core rendering hot path. The remaining
~27% is available for overhead, input, sound, and the IRQ chain.
