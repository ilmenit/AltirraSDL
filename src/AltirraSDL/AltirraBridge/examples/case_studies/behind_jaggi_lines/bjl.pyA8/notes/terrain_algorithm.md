# Fractal Terrain Generation

## Overview
The terrain engine generates a 256-entry height buffer at $0900-$09FF using
a recursive fractal subdivision algorithm. This is a variant of **midpoint
displacement** (the 1D equivalent of the diamond-square algorithm).

## Data Structures
- **Height buffer** $0900[256]: ring buffer of terrain heights
  - Values $00-$3F = valid height (6-bit, 0-63)
  - Values $80-$BF = initialized but not yet subdivided
  - Bit 7 set = marker for "needs subdivision"
- **Recursion stack** $24BE[N]: stores seed positions for pending subdivisions
- **ZP variables**:
  - $98 = recursion depth / stack pointer
  - $99 = previous depth (for comparison)
  - $9A = delta_x (step direction: +1, -1, +16, -16)
  - $9B = delta_y (perpendicular step)
  - $9C = current seed position in height buffer

## Algorithm: init_terrain ($843C)

1. Fill entire 256-byte buffer with random values:
   Each value = (RANDOM AND $3F) OR $80, giving range $80-$BF.
   Bit 7 is set to mark these as "unresolved" — not yet subdivided.

2. Set recursion depth ($98) = 1

3. Pick random seed position from RANDOM hardware register

4. Write a random 6-bit value (AND $3F, bit 7 clear = "resolved") at seed

5. Call terrain_step to check all 4 cardinal neighbors

6. Compare depth before vs after: if terrain_step pushed new seeds,
   the depth increased — keep subdividing

7. Random walk: read 2 bits from RANDOM to choose a direction:
   - Bit 6=0, Bit 7=0: delta_x=+1,  delta_y=+16  (right, down)
   - Bit 6=0, Bit 7=1: delta_x=-1,  delta_y=-16   (left, up)
   - Bit 6=1, Bit 7=0: delta_x=+16, delta_y=+1    (down, right)
   - Bit 6=1, Bit 7=1: delta_x=-16, delta_y=-1     (up, left)

8. Walk forward using sub_84C4: check if a chain of neighbors has bit 7 set

9. If chain is all unresolved: write new random height, advance seed position

10. When depth decreases back to match, pop from recursion stack at $24BE
    and continue from the stored seed. When depth reaches 0, terrain is done.

## Algorithm: terrain_step ($84E6)
Checks 4 orthogonal neighbors of current position by calling sub_84C4
with each of the 4 direction pairs:
  - (+1, +16): right neighbor, check perpendicular down
  - (-1, -16): left neighbor, check perpendicular up
  - (+16, -1): down neighbor, check perpendicular left
  - (-16, +1): up neighbor, check perpendicular right

For each direction: if sub_84C4 returns negative (unresolved chain found),
calls sub_8526 to push the current seed position onto the recursion stack
and increment the depth counter.

## Algorithm: sub_84C4 (neighbor chain check)
Starting from seed position $9C, checks a 3-cell neighborhood:
  1. addr1 = seed + delta_x
  2. If height[addr1] has bit 7 clear (already resolved): return positive (ok)
  3. addr2 = addr1 + delta_x (two steps in same direction)
  4. If height[addr2] has bit 7 clear: return positive
  5. addr3 = addr1 + delta_y (one step perpendicular)
  6. If height[addr3] has bit 7 clear: return positive
  7. All three neighbors unresolved: return negative (needs work)

This L-shaped neighborhood check determines whether the fractal subdivision
has "reached" this area yet. If all nearby cells are still unresolved,
the algorithm needs to expand the frontier in this direction.

## Historical Context
This is one of the earliest real-time fractal terrain implementations on
an 8-bit home computer. The algorithm is related to the work of Loren
Carpenter (who created the fractal terrain sequences for Star Trek II:
The Wrath of Khan, 1982) and Benoit Mandelbrot's fractal geometry.

The Lucasfilm Games team (including David Fox and Charlie Kellner) adapted
these techniques for the constrained Atari hardware — 1.79 MHz 6502 CPU,
generating believable mountain landscapes in real-time.

## Ring Buffer Nature
The height buffer at $0900 is 256 bytes, indexed by a single byte.
Address arithmetic wraps naturally (6502 8-bit addition wraps at 256),
creating a seamless wraparound terrain — the player can fly in any
direction indefinitely without hitting edges.

## 2D Grid Interpretation
Although the buffer is linearly addressed ($0900-$09FF), the step sizes
reveal a 2D grid interpretation:
- Step +1 / -1: moves horizontally (left/right in the terrain grid)
- Step +16 / -16: moves vertically (forward/backward in the terrain grid)
This gives a 16x16 grid of terrain cells, each with a 6-bit height.
The 16x16 grid wraps in both dimensions due to 8-bit index arithmetic.

## Rendering Pipeline
The terrain heights in the $0900 buffer are transformed into the
on-screen 3D view through a perspective projection pipeline:
1. The ship's current position indexes into the terrain ring
2. Heights at visible positions are read and scaled by distance
3. Closer terrain rows are taller, further rows are shorter (perspective)
4. Each screen row (ANTIC mode D, 2 scanlines) shows one "slice" of terrain
5. The rendering fills pixels from the horizon down to the ground level
6. Colors change based on distance (DLI-driven per-scanline)

## From Heightmap to Screen: The Edge-Based Renderer

The terrain is NOT rendered as filled polygons or raycasted columns.
Instead, it uses a unique **edge-based subdivision rasterizer** that
is closer to a wireframe renderer with fill.

### Vertex Grid
The 16x16 heightmap at $0900 is sampled at 45 positions (a subset of the
grid visible from the current camera). Each sample becomes a vertex with:
- World X/Z coordinates (computed via incremental direction deltas)
- Height (read from $0900 ring buffer, scaled by camera altitude)
- Screen X/Y (computed via perspective division: coord / Z)
- Visibility flags (behind camera, left/right of FOV)

### Edge Mesh
72 edges connect the 45 vertices in a fixed adjacency pattern stored
at $B757. Each edge is a pair of vertex indices defining a terrain
silhouette segment. The edges form a mesh that roughly traces the
mountain ridgeline as seen from the cockpit.

### Subdivision Rasterization
Each visible edge is rasterized by the inner loop at $B51C, which walks
X across screen columns. When a column exactly matches an edge sample
point (tested via `SBC $0095,Y; CMP #$FE`), the routine:

1. Computes the interpolated depth at this column
2. Tests against the per-column z-buffer at $2608
3. If nearer: writes the pixel using 4-page table-driven indirection
   ($28EC/$291C for row address, $BD00 for byte offset, $BC00 for color mask)
4. If farther: skips (another ridge is already closer)

When a column does NOT match an edge sample, the routine recursively
subdivides: `new_sample = (column + edge_sample[Y]) / 2`, creating
a finer approximation of the terrain silhouette. This is **midpoint
subdivision applied at rasterization time**, not just at heightmap
generation time — the fractal refinement extends all the way to the
pixel level.

### Ground Fill
After edges are rasterized, fill_column_fast at $AF32 fills the screen
area below each terrain edge with ground color ($55 = brown/green).
It scans each column top-down to find where terrain content begins
(the edge the rasterizer just drew), then fills from that point down
using an unrolled STA chain through the 45 scanline addresses.

The pixel blend phase at $B218 uses 4 lookup tables ($BC00-$BFFF) to
produce dithered color transitions at terrain boundaries, avoiding the
hard color banding that would result from simple fills.
