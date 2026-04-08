# 3D Rendering Pipeline

## Overview
The game renders a first-person 3D terrain view on the Atari 800XL at ~15-30fps.
The rendering pipeline runs in the main loop (not inside VBI), taking roughly
19,100 cycles/frame (64% of the NTSC budget of 29,868 cycles/frame). The VBI
and DLI handlers are kept lean (~100 and ~160 cyc/frame respectively) so the
main loop has maximum time for the 3D engine.

## Pipeline Stages

### 1. Camera Parameters (load_camera at $AD6C)
Loads the current camera state from the game variable area at $2800+:
- Heading: $289B/$289C (16-bit angle)
- Pitch: $289D/$289E (16-bit angle)
- Altitude: $289F/$28A0 (height above terrain)
- Speed: stored separately, affects position delta per frame

### 2. Offset Vector Computation (compute_screen_offset at $A0DB)
Computes screen-space basis vectors from camera heading and pitch.
Uses the 16-bit signed multiply at $9CCF (shift-and-add, unrolled 8 iterations)
to transform heading/pitch into X and Z increments for the vertex grid.
Outputs displacement vectors in $A0/$A1 (X step) and $A2/$A3 (Z step).

### 3. Vertex Transform (transform_vertex at $9E8C)
Transforms 45 terrain grid vertices from world space to camera space.
The terrain grid uses a **compressed delta encoding**: each byte at ($80)
encodes 4 direction bits controlling whether to add or subtract X/Z offsets
to successive vertex positions.

**Direction decode**: Each direction byte encodes up to 6 action flags via
cascading ROL/BPL tests. Bits select from 8 transform variants — combinations
of add/subtract on X and Z coordinates with swap/no-swap. The branch cascade
at $9EE7-$9FC3 dispatches into one of 8 code paths, each performing 16-bit
add or subtract on the coordinate arrays.

For each vertex (1,194 iterations per 300-frame profile window):
1. Read packed direction byte from ($80),Y
2. Decode flags and apply X/Z coordinate transforms
3. Look up terrain height from $0900,X (the fractal heightmap)
4. Subtract camera altitude ($8B/$8C), shift right 4 times to scale for screen
5. Store height data at $23AF,Y and $2382,Y/$2355,Y
6. Classify visibility: $80=behind camera, $40=right of FOV, $20=left of FOV, $00=visible

Storage arrays (45 entries each, indexed by vertex Y=0..$2C):
- $229D/$22CB: camera-space X (lo/hi) — accumulated from direction deltas
- $22F9/$2327: camera-space Z (lo/hi) — accumulated from direction deltas
- $23AF: terrain height per vertex (from $0900 heightmap)
- $2382/$2355: scaled height for Y projection (lo/hi)
- $2270: terrain grid index per vertex
- $2490: visibility/projection flags (bit 7=behind, 6=right clip, 5=left clip, 4=projected)

**Cost**: ~974 cyc/frame (5.1% of budget). The coordinate array stores dominate
(8 absolute-indexed stores per vertex at 5 cycles each).

### 4. Visibility Culling & Edge Processing (terrain_render at $A356)

#### Phase 1: Column mask initialization ($A35B-$A37D)
Builds the $BD00-$BD07 pixel column lookup table. Iterates 621 times (27 calls
× 23 inner iterations), writing 8 entries per iteration. Each entry stores
the column index for use by the pixel write path. This table maps screen
column → pixel byte offset within a scanline.

Also initializes the z-buffer ($2634-$26C7) with occlusion sentinel values
($67 and $6B) via two tight loops at $A389 and $A396.

#### Phase 2: Edge iteration ($A3E7-$A46C)
Iterates 1,934 edge pairs from the adjacency table at $B757, consuming
2 bytes per pair (vertex indices). For each edge:

1. Load first vertex index from $B757,Y; check visibility flags at $2490,X
   - Bit 7 set (behind camera): skip entirely (375 edges = 19%)
   - Bit 5 set (off-screen right): skip (354 edges = 18%)
2. Load second vertex index; check visibility:
   - Behind camera or bit 6 set (off-screen left): skip (181 edges = 9%)
3. Check projection flag (bit 4 of $2490,X): if not projected, call project_x
4. Copy projected coordinates to working area ($2572/$2590/$25AE/$25CC/$25EA)
5. Call rasterize_edge at $B24D

**53% of edges survive culling** (1,024 of 1,934 proceed to rasterize).
Edges are processed in fixed spatial order from the adjacency table.

**Cost**: ~1,068 cyc/frame (5.6% of budget).

### 5. Perspective Projection (project_x at $A157)

Performs two perspective divisions per vertex:

#### X projection ($A157-$A239)
```
screen_x = (world_x / world_z) * scale + 128
```

Steps:
1. Load Z-depth into divisor: $AF/$B0 = $22FA,X / $2328,X
2. Load X-coordinate into dividend: $B1/$B2 = $229E,X / $22CC,X (negated if negative)
3. **Pre-normalize**: loop at $A189-$A19E shifts divisor left until dividend < divisor,
   counting shifts in Y. Runs 522 extra iterations (avg 0.75 per call).
   If 8 shifts exhausted without normalizing: vertex is at infinity, clamp to screen edge.
4. Call div16 at $9DA7 — quotient in $B3
5. **Un-shift**: loop at $A1CB-$A1CF left-shifts quotient by Y positions
   (undoing the normalization). Then scale by 3/4: `result = (Q + Q/2) / 4`
6. Add $80 offset (screen center). Store at $23DC,X (lo) and $2409,X (hi).

#### Y projection ($A23A-$A315)
Same structure but divides height ($2355,X / $2382,X) by Z. Result stored
at $2436,X / $2463,X. Used for terrain edge vertical positions.

**Cost**: ~828 cyc/frame (4.3%). Called 713 times per profile window.
The two div16 calls per vertex dominate (1,369 total division calls).

### 6. Division Algorithm (div16 at $9DA7)

**Restoring division**, fully unrolled for 8 quotient bits. NOT non-restoring
as previously documented — the algorithm trial-subtracts and conditionally
restores based on the result.

#### Entry and normalization

The routine has a pre-normalization loop at $9D9F that shifts both dividend
($B1/$B2) and divisor ($AF/$B0) left until bit 6 of the divisor's high byte
is set. This ensures the division produces maximal quotient precision.

```asm
$9D9F: ASL $B1          ; shift dividend left
$9DA1: ROL $B2
$9DA3: ASL $AF          ; shift divisor left
$9DA5: ROL $B0
$9DA7: BIT $B0          ; test divisor bit 6
$9DA9: BVC $9D9F        ; loop until bit 6 set
```

Profile: enters $9DA7 4,452 times, normalization at $9D9F runs 3,280 times —
averaging 2.4 normalization shifts per call before the main body begins.

#### Main body (8 unrolled iterations at $9DAB-$9E8B)

Each iteration (example — first iteration at $9DAB):
```asm
ASL $B1           ; 5 cyc — shift remainder left
ROL $B2           ; 5 cyc
BMI try_sub       ; 2-3 cyc — if high bit set, subtraction will succeed
BIT $B2           ; 3 cyc — test bit 6 of remainder high byte
BVC no_sub        ; 2-3 cyc — if bit 6 clear, remainder < divisor
try_sub:
  SEC             ; 2 cyc
  LDA $B1         ; 3 cyc — trial subtract divisor from remainder
  SBC $AF         ; 3 cyc
  TAY             ; 2 cyc — save lo result in Y
  LDA $B2         ; 3 cyc
  SBC $B0         ; 3 cyc
  BCC no_sub      ; 2-3 cyc — if borrow: remainder was less, don't update
  STY $B1         ; 3 cyc — keep the subtraction result
  STA $B2         ; 3 cyc
no_sub:
ROL $B3           ; 5 cyc — shift quotient bit in (carry = did subtract)
```

Each iteration costs ~30-42 cycles depending on the subtraction branch.
8 iterations totals ~240-336 cycles, plus ~67 cycles average normalization.

- Inputs: $B1/$B2 = dividend (remainder), $AF/$B0 = divisor
- Output: $B3 = 8-bit quotient
- Total cost: ~88 cycles average per call (from profiling: 415,724 cyc / 4,715 attributed calls)
- **Actual unique entries**: 1,369 (from project_x X and Y projection phases)

### 7. Edge Rasterization (rasterize_edge at $B24D)

Called 1,024 times per profile window (once per visible edge pair).
Subdivides each terrain edge using midpoint interpolation and walks the
resulting pixel span.

#### Midpoint subdivision (edge_midpoint at $B3A7)
Computes midpoints between two projected vertices:
```
mid_x = (x_a + x_b + 1) / 2    (via ADC / ROR)
mid_color = (color_a + color_b) / 2
```
Generates up to 15 intermediate points per edge, building working arrays:
- $0095,Y: X positions of edge sample points
- $00E3,Y: depth/height values at each sample
- NSIGN,Y ($00EC): color/shade interpolation values

Called 506 times per profile window at ~107 cyc/call.

#### Inner pixel loop (fill_terrain_column at $B418 → hot loop at $B51C)

This is the **single hottest code path in the game**, consuming ~2,574 cyc/frame.
The loop walks X across screen columns, testing each pixel against the z-buffer
and writing to screen memory via table-driven indirection.

**Entry** at $B418: validates column bounds ($95 < $2D = left margin), then
enters the midpoint subdivision walk or jumps directly to the pixel loop.

**Pixel loop** at $B51C (runs 19,072 iterations per profile window):

```asm
$B51C: CPX #$D4             ; 2 cyc — right boundary check (column 212)
$B51E: BCS $B518            ; 2-3 cyc — exit if past right edge
$B520: TXA                  ; 2 cyc — current column → A
$B521: STX $82              ; 3 cyc — save column position
$B523: SEC                  ; 2 cyc
$B524: SBC $0095,Y          ; 4 cyc — distance = column - edge_sample[Y]
$B527: CMP #$FE             ; 2 cyc — are we exactly at this edge sample? (-2 threshold)
$B529: BNE $B5A3            ; 2-3 cyc — if not at edge: midpoint/advance path
```

**Branch statistics from profiling**:
- 18,964 iterations reach the SBC comparison
- 6,896 (36%) take the "at edge sample" path → z-buffer test + pixel write
- 12,068 (64%) take the "not at edge" path → midpoint subdivision at $B5A3

**When AT the edge sample** (36% of iterations), the z-buffer test:

```asm
$B52B: LDA $00E3,Y          ; 4 cyc — load depth value
$B52E: ADC $84              ; 3 cyc — interpolate with current
$B530: ROR                  ; 2 cyc — average
$B531: CMP $2608,X          ; 4 cyc — z-buffer test
$B534: BCC $B55F            ; 2-3 cyc — behind existing terrain: skip
$B536: BEQ $B55F            ; 2-3 cyc — equal: skip
```

Of 6,896 z-buffer tests: **3,458 pass** on the first sample, **3,438 rejected** (50% rejection rate).

**Pixel write** (when z-test passes, 6,337 total writes across both edge halves):

```asm
$B538: STA $2608,X          ; 5 cyc — update z-buffer
$B546: STY $B6              ; 3 cyc — save edge index
$B548: TAY                  ; 2 cyc — depth as row table index
$B549: LDA $28EC,Y          ; 4 cyc — scanline address lo from row table
$B54C: STA $80              ; 3 cyc
$B54E: LDA $291C,Y          ; 4 cyc — scanline address hi from row table
$B551: STA $81              ; 3 cyc
$B553: LDY $BD00,X          ; 4 cyc — pixel byte offset within scanline
$B556: LDA ($80),Y          ; 5 cyc — read current screen byte
$B558: ORA $BC00,X          ; 4 cyc — merge pixel color via mask table
$B55B: STA ($80),Y          ; 6 cyc — write back to screen
$B55D: LDY $B6              ; 3 cyc — restore edge index
```

**41 cycles per pixel write** × 6,337 pixels = ~260K cycles in pixel writes
alone (45% of the total rendering budget).

The loop processes pixels in **pairs** — two mirrored halves of the edge curve
are walked simultaneously. After the first sample's z-test and write, the loop
does a symmetric write for the second sample at $B560-$B591 with the same
structure (LDA/CMP/STA/STY/TAY/LDA/STA/LDY/LDA/ORA/STA/LDY).

**When NOT at the edge sample** (64%, the midpoint path at $B5A3):

```asm
$B5A3: BCC $B5E4            ; did column exceed edge position?
; If column > edge_pos: draw partial pixel (SBC path at $B5A5)
; If column < edge_pos: compute midpoint between current X and edge
$B5E4: TXA                  ; midpoint = (column + edge_pos[Y]) / 2
$B5E5: ADC $0095,Y
$B5E8: ROR
$B5E9: STA $0096,Y          ; store as next edge sample position
$B5EC: LDA $86              ; interpolate color
$B5EE: SEC
$B5EF: ADC EEXP,Y
$B5F2: STA NSIGN,Y          ; store interpolated color
$B5F5: BMI $B605            ; branch on sign for depth interp direction
```

This midpoint refinement is the **recursive subdivision at runtime**: the edge
is continuously bisected as the rasterizer walks across screen columns, producing
smooth terrain silhouettes from the coarse vertex data.

### 8. Ground Fill (fill_column_fast at $AF32)

Fills screen columns below the terrain edge with ground color. Called 28 times
per frame (once per terrain render call). Despite the low call count, the
unrolled scan + fill costs 2,574 cyc/frame — the most expensive single routine.

#### Phase 1: Top-down scan ($AF38-$B021)
Scans 20 screen rows from top to bottom comparing against zero (cleared screen):
```asm
LDA #$00
CMP $1010,Y          ; row 0
BEQ next0            ; skip if already clear
CMP $1070,Y          ; row 1
BEQ next1
...                  ; 20 unrolled CMP/BEQ pairs
CMP $1730,Y          ; row 19 — first non-zero found here usually
```

When a non-zero byte is found: load ground color (#$55), set row counter in X,
and jump into the fill chain. If all 20 rows are clear: continue to extended
scan at $B023+. Profile shows 1,092 calls reach row 20 before finding content.

#### Phase 2: Unrolled fill ($B1D5-$B20D)
Fall-through STA chain fills from the found row down to the bottom:
```asm
$B1D5: STA $16D0,Y          ; row 20 — entry point when first 20 rows clear
$B1D8: STA $1670,Y          ; row 19
...
$B208: STA $1070,Y          ; row 1
```

Execution count decreases toward the top (row 20: 1,092; row 35: ~500; row 42: ~50),
matching the terrain silhouette profile — closer terrain fills more rows.

#### Phase 3: Pixel-level blend ($B218-$B247)
Per-pixel fill with table-driven color blending:
```asm
$B218: LDX $97              ; row counter
$B21A: LDA $0751,X          ; row table → screen ptr
$B21D: STA $80
$B21F: LDA $07A7,X
$B222: STA $81
$B224: LDA ($80),Y          ; read screen byte
$B226: TAX
$B227: AND $95              ; mask bits
$B229: ORA $96              ; merge ground color
$B22B: ORA $BF00,X          ; OR with blend table
$B22E: STA ($80),Y          ; write back
$B230: LDA $BE00,X          ; check if more blending needed
$B233: AND $96
$B235: BEQ $B244            ; if no more blend: advance to next column
$B237: STA $96              ; update blend mask
$B239: TAX
$B23A: LDA $BE00,X
$B23D: STA $95
$B23F: INC $97              ; next row
$B241: JMP $B218            ; loop
```

This inner loop runs 3,404 iterations, blending the ground color into
the terrain pixels with dithered transitions using $BE00/$BF00 blend tables.

## Math Library

### 16-bit Signed Multiply ($9CCF)
- Inputs: $AB/$AC (16-bit multiplicand), $AD (8-bit multiplier)
- Output: $A9/$AA (16-bit product)
- Method: shift-and-add, 8 unrolled iterations
- Handles sign: saves sign of $AC in $AE, negates multiplicand if negative,
  performs unsigned multiply, then negates result if $AE had bit 7 set
- Cost: ~82 cyc/frame (112 calls × 220 cyc/call average)

### 16-bit Restoring Division ($9DA7)
- Dividend (remainder): $B1/$B2
- Divisor: $AF/$B0
- Quotient: $B3 (8-bit)
- Method: **restoring division**, 8 fully unrolled iterations with
  pre-normalization loop (NOT non-restoring — the algorithm trial-subtracts
  and conditionally keeps the result via BCC/STY/STA)
- Pre-normalization at $9D9F shifts both dividend and divisor left until
  divisor bit 6 is set (avg 2.4 shifts per call)
- Cost: ~1,386 cyc/frame (1,369 calls from project_x, ~88 cyc/call average)
- Represents **7.3% of the total rendering budget** — the single most expensive
  math operation

## Lookup Tables

| Address   | Size   | Purpose |
|-----------|--------|---------|
| $B757     | 144    | Edge adjacency table — 72 vertex-pair indices defining the terrain mesh connectivity |
| $BC00     | 256    | Pixel column color mask — which bits to OR for this column's pixel |
| $BD00     | 212    | Pixel byte offset within scanline — rebuilt each frame from viewport position |
| $BE00     | 256    | Pixel blend continuation table — maps current byte → next blend mask |
| $BF00     | 256    | Pixel blend merge table — additional bits to OR for dithered fill |
| $2608     | 212    | Per-column z-buffer — depth of nearest terrain at each screen column |
| $2634     | 148    | Z-buffer sentinel area — initialized with $67/$6B occlusion markers |
| $2708     | 32     | Horizon height profile curve — vertical offset per distance band |
| $28D0     | 32     | Height delta table — terrain height differences for perspective |
| $28EC     | 48     | Scanline address table (lo) — screen row → memory address lo byte |
| $291C     | 48     | Scanline address table (hi) — screen row → memory address hi byte |

## Performance Characteristics (profiled during in-flight gameplay)

### Cycle Budget
```
NTSC budget:        29,868 cyc/frame
Measured average:   19,102 cyc/frame (64.0%)
Headroom:           10,766 cyc/frame (36.0%)
```

### Cost Breakdown by Subsystem
| Subsystem         | Cyc/frame | % Budget | Key routines |
|-------------------|-----------|----------|--------------|
| Ground fill       | 2,574     | 13.5%    | fill_column_fast |
| Division          | 1,386     | 7.3%    | div16 |
| Terrain render    | 1,068     | 5.6%    | terrain_render |
| Vertex transform  | 974       | 5.1%    | transform_vertex |
| Unknown draw      | 916       | 4.8%    | unknown_draw_AE3E |
| Projection        | 828       | 4.3%    | project_x |
| Calc/render       | 674       | 3.5%    | unknown_render_99C2 |
| Edge rasterize    | 618       | 3.2%    | rasterize_edge |
| Input + physics   | 574       | 3.0%    | read_joystick |
| Sound             | 565       | 3.0%    | sound_dispatch |
| Interrupts        | 260       | 1.4%    | dli_dispatch + gameplay_vbi |
| Other             | 4,665     | 24.4%   | remaining 40+ subroutines |

### Rendering Techniques
- **Unrolled loops**: multiply, divide, and column fill are fully unrolled
  (no loop counters, no branch-back overhead)
- **Lookup tables**: pixel masks, byte offsets, screen addresses, and blend
  patterns are pre-computed in 256-byte pages for single-instruction lookup
- **Z-buffer occlusion**: per-column depth buffer at $2608 rejects 50% of
  candidate pixels — critical for overlapping terrain ridges
- **Near-plane culling**: vertices with Z < 6 are flagged $80 and skipped
  (28% of vertices culled during typical flight)
- **FOV clipping**: vertices outside the horizontal field of view are flagged
  $20 (left) or $40 (right) and their edges skipped without projection
- **Edge-based rendering**: instead of per-pixel raycasting or polygon fill,
  the engine rasterizes terrain edges using recursive midpoint subdivision —
  a hybrid between wireframe and filled rendering, well-suited to the 6502
- **Low resolution**: ANTIC mode D gives 160×96 effective pixels (2 scanlines
  per mode line, 2 bits per pixel = 4 colors)
