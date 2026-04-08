# Performance Profile — In-Flight Gameplay

Profiled during in-flight gameplay, ~20,000 frames after boot (past title screen),
300-frame measurement window. NTSC mode.

## Summary
```
NTSC cycle budget:  29,868 cyc/frame
Measured average:   19,102 cyc/frame (64.0% utilization)
Remaining headroom: 10,766 cyc/frame (36.0%)
Total instructions: 1,731,617 over 300 frames (5,772/frame)
Total cycles:       5,730,600 over 300 frames
```

The game runs comfortably within budget during normal flight. The headroom
allows for more complex scenes (multiple enemies, explosions, landing sequences)
without frame drops.

## Top Routines by Cost

| Rank | Address | Name | Cyc/frame | % Used | Calls/frame | Cyc/call |
|------|---------|------|-----------|--------|-------------|----------|
| 1    | $AF32   | fill_column_fast | 2,574 | 13.5% | ~3.6 | 27,573 |
| 2    | $9DA7   | div16 | 1,386 | 7.3% | ~4.6 | 88 |
| 3    | $A356   | terrain_render | 1,068 | 5.6% | ~0.09 | 11,441 |
| 4    | $9E8C   | transform_vertex | 974 | 5.1% | ~0.09 | 10,434 |
| 5    | $AE3E   | unknown_draw_AE3E | 916 | 4.8% | ~0.09 | 9,810 |
| 6    | $A157   | project_x | 828 | 4.3% | ~2.5 | 332 |
| 7    | $99C2   | unknown_render_99C2 | 674 | 3.5% | ~0.75 | 899 |
| 8    | $B24D   | rasterize_edge | 618 | 3.2% | ~3.6 | 172 |
| 9    | $8530   | read_joystick | 574 | 3.0% | 0.5 | 1,148 |
| 10   | $9119   | sound_dispatch | 565 | 3.0% | 1.0 | 565 |

Notes:
- fill_column_fast, terrain_render, transform_vertex, and unknown_draw_AE3E
  are called ~28 times per 300 frames (once every ~10 frames), indicating the
  3D rendering runs at approximately 1/10th the frame rate — about 6 fps for
  the full terrain redraw, with the VBI maintaining stable display at 60 fps.
- div16 and project_x are called per-vertex within the terrain render pass.
- rasterize_edge is called per visible edge pair (1,024 per render pass).

## Execution Contexts

| Context    | Cycles/frame | % Budget | Routines |
|------------|-------------|----------|----------|
| Main loop  | 12,185      | 40.8%    | 52 subroutines — fill_column_fast, div16, terrain_render lead |
| Interrupt  | 260         | 0.9%     | 2 subroutines — dli_dispatch (160), gameplay_vbi (100) |

The interrupt handlers are remarkably lean. The DLI chain processes 5 DLI
triggers per frame at ~32 cycles each. The VBI does palette writes, PMG
positioning, and DLI counter reset in ~100 cycles.

## Hot Instruction Addresses

The top 10 addresses by cycle consumption are ALL inside the rasterize_edge
inner loop at $B51C-$B5F5:

| Addr  | Instruction | Count/300f | Cycles | Role |
|-------|-------------|-----------|--------|------|
| $B524 | SBC $0095,Y | 18,964 | 75,856 | Edge distance calculation |
| $B521 | STX $82 | 18,964 | 56,892 | Save column position |
| $B529 | BNE $B5A3 | 18,964 | 49,996 | "Not at edge" branch |
| $B5E9 | STA $0096,Y | 9,183 | 45,915 | Store subdivided X position |
| $B5F2 | STA NSIGN,Y | 9,183 | 45,915 | Store interpolated color |
| $B51E | BCS $B518 | 19,072 | 38,252 | Right boundary exit check |
| $B51C | CPX #$D4 | 19,072 | 38,144 | Right boundary compare |
| $B520 | TXA | 18,964 | 37,928 | Column → accumulator |
| $B523 | SEC | 18,964 | 37,928 | Set carry for subtraction |
| $B527 | CMP #$FE | 18,964 | 37,928 | Edge proximity test |

These 10 instructions alone account for 504,754 cycles (8.8% of total).

## Z-Buffer Effectiveness

The per-column z-buffer at $2608 is tested during every pixel write attempt:
- **6,896 z-buffer tests** per 300 frames (at $B531: CMP $2608,X)
- **3,458 pass** (50.1%) — pixel is nearer than existing, gets drawn
- **3,438 rejected** (49.9%) — pixel is behind existing terrain, skipped

The 50% rejection rate indicates moderate occlusion. Terrain ridges
frequently overlap from the cockpit perspective, and the z-buffer prevents
far ridges from overwriting near ones.

With front-to-back edge sorting, the rejection rate could increase to 60-70%,
reducing pixel writes by ~20-30%.

## Visibility Culling Statistics

During transform_vertex, each of the 45 vertices gets a visibility flag:
- **$00 = visible**: vertex is in front of camera and within horizontal FOV
- **$80 = behind camera**: Z < 6 (near-plane clip)
- **$40 = off-screen right**: |X| > |Z| and X positive
- **$20 = off-screen left**: |X| > |Z| and X negative

During terrain_render, edge pairs are culled based on endpoint flags:
- Of 1,934 edge iterations:
  - 375 (19%) — first vertex behind camera ($2490 bit 7)
  - 354 (18%) — first vertex off-screen ($2490 bit 5)
  - 181 (9%) — second vertex behind or off-screen
  - **1,024 (53%) survive** — proceed to project_x + rasterize_edge

## Division Algorithm Profile

div16 (restoring division at $9DA7) is entered from project_x at two call sites:
- $A1C1: JSR $9DA7 — X projection (696 calls per 300 frames)
- $A29C: JSR $9DA7 — Y projection (673 calls per 300 frames)
- Total: **1,369 division calls**

Pre-normalization loop at $9D9F:
- Executes 3,280 times (avg 2.4 shifts per division call)
- Cost: ~28 cyc per shift × 2.4 = ~67 cyc overhead per call

Main 8-iteration body: each of the 8 unrolled steps executes 1,369 times.
The conditional subtract path is taken ~50% of the time per iteration
(ranging from 829/1,369 = 61% in iteration 1 to 696/1,369 = 51% in iteration 8).

## Hot Loops

| Rank | Range | Routine | Iters/call | Total cycles | Description |
|------|-------|---------|------------|-------------|-------------|
| 1 | $9326-$9341 | sub_931F | 1.0 | 90,900 | Entity state scan loop |
| 2 | $9349-$9366 | sub_9344 | 1.0 | 59,700 | Entity state scan loop (variant) |
| 3 | $9A6E-$9A8B | unknown_render_99C2 | 0.4 | 53,215 | Arithmetic inner loop |
| 4 | $9003-$900B | unknown_calc_8FF9 | 0.9 | 45,900 | Calculation inner loop |
| 5 | $A35B-$A37D | terrain_render | 1.0 | 37,968 | BD00 column mask init (8 stores/iter) |
| 6 | $B29C-$B2BC | rasterize_edge | 10.6 | 23,732 | Edge subdivision walk |
| 7 | $A52D-$A53D | terrain_render | 1.0 | 23,268 | Height delta computation |
| 8 | $A396-$A3A3 | terrain_render | 1.0 | 23,072 | Z-buffer sentinel init (4 stores/iter) |
| 9 | $A189-$A19E | project_x | 0.4 | 22,720 | Division pre-normalization |
| 10 | $A264-$A279 | project_x | 0.4 | 18,145 | Division pre-normalization (Y phase) |

## Optimization Targets

### 1. Division: restoring → non-restoring + CLZ table (~500 cyc/frame)
Replace the BIT/BVC conditional test with always-subtract/always-add based on
remainder sign. Add a 256-byte leading-zero-count table to replace the
normalization loop with a direct jump offset. Eliminates ~67 cyc normalization
overhead and saves ~8 cyc per iteration.

### 2. Z-buffer: front-to-back edge sorting (~217 cyc/frame)
Sort the adjacency table edges by average Z-depth each frame before rendering.
Near edges populate the z-buffer first, causing far edges to be rejected faster.
Cost: ~1,500 cyc for insertion sort on 72 pairs. Benefit: 20-30% fewer pixel
writes at 41 cyc each.

### 3. Fill scan: cache terrain-top per column (~182 cyc/frame)
Store the row index where terrain begins in each of the 42 columns (42 bytes).
Jump directly into the unrolled STA fill chain instead of scanning 20 CMP/BEQ
pairs. Update the cache during rasterize_edge pixel writes.

### 4. Pixel write: register allocation (~127 cyc/frame)
The edge index Y is saved/restored (STY $B6 / LDY $B6 = 6 cycles) around
every pixel write. Keeping the edge index in a zero-page variable instead
of Y would eliminate this overhead for 6,337 pixel writes.

### 5. Column mask: cache between frames (~100 cyc/frame)
The $BD00 column-to-pixel-offset table is rebuilt every terrain render call
(621 iterations × 37K cycles total). It only changes when the viewport X
offset shifts by a full column width. Skip rebuild when unchanged.

### 6. Transform: early behind-camera cull (~91 cyc/frame)
28% of vertices are behind the camera but are fully transformed before being
flagged. Test the Z-coordinate before the direction decode to skip ~334
vertex transforms per profile window.

### Total potential savings: ~1,217 cyc/frame (6.4% of current budget)
