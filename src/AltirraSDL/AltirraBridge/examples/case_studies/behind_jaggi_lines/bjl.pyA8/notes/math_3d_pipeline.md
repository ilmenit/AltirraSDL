# 3D Math Pipeline

The 3D engine uses fixed-point arithmetic with 16-bit multiply and divide
routines. The pipeline transforms world-space terrain vertices through a
camera matrix into screen coordinates.

## Core Math Routines

| Address | Name              | Role |
|---------|-------------------|------|
| $9CCF   | mul16_signed      | 16-bit signed multiply → 16-bit result |
| $9DA7   | div16             | 16-bit unsigned divide → quotient + remainder |
| $9C59   | bit_extract_9C59  | 45-instruction bit extraction/packing |
| $8329   | bit_extract_8329  | 15-instruction shift/rotate extraction |
| $7DCB   | calc_7DCB         | 14-instruction arithmetic with shift loop |

### mul16_signed ($9CCF)

Multiplies two 16-bit signed values. Used by the vertex transform for
rotation matrix application. The ZP workspace uses $AB/$AC for operands.
Called from 8 sites — all in the 3D transform path.

### div16 ($9DA7)

16-bit **restoring division** for perspective projection. Converts world-space
distances to screen coordinates. Called from 2 sites in project_x (X and Y
projection phases), totaling ~1,369 calls per render pass.

Algorithm: 8 fully unrolled iterations of trial-subtract with conditional
restore. Each iteration shifts the remainder left (ASL/ROL), trial-subtracts
the divisor, and keeps the result only if no borrow (BCC skips the STY/STA
restore). A pre-normalization loop at $9D9F shifts both dividend and divisor
left until divisor bit 6 is set (avg 2.4 shifts per call).

- Dividend (remainder): $B1/$B2
- Divisor: $AF/$B0
- Quotient: $B3 (8-bit)
- Cost: ~88 cycles/call average, ~1,386 cyc/frame (7.3% of render budget)

## 3D Transform Pipeline

| Address | Name                  | Role |
|---------|-----------------------|------|
| $9E8C   | transform_vertex      | Full 3D vertex transform through camera matrix |
| $A0DB   | compute_screen_offset | Screen buffer offsets from viewer position |
| $A157   | project_x             | 3D X → screen column (perspective division) |
| $AD6C   | load_camera           | Copy camera pos/orientation to transform workspace |

### transform_vertex ($9E8C)

The vertex transform applies rotation + translation through the camera matrix.
It calls mul16_signed for each matrix element multiplication. Input is a 3D
world coordinate, output is camera-relative coordinates ready for projection.

### project_x ($A157)

Perspective division: divides camera-space X by Z depth using div16 to get
the screen column. Handles near-plane clipping when Z approaches zero.

## Camera State

Camera parameters are stored at $2800+ region:
- Heading, pitch (orientation angles)
- X, Y, Z position (world coordinates)
- Altitude above terrain
- Speed / velocity

`load_camera` ($AD6C) copies these into a fast-access workspace for the
per-frame transform pass.

## Lookup Tables

The math/rendering pipeline uses several precomputed tables:

| Address     | Name                | Content |
|-------------|---------------------|---------|
| $6B9E-$7909 | terrain_lut         | Perspective transform and height-to-screen LUTs |
| $BC00       | pixel_masks         | Pixel bit masks for ANTIC mode D (2bpp) |
| $BD00       | byte_offsets        | Byte offset table for screen column addressing |
| $BE00-$BF00 | screen_addresses    | Screen line address table (lo/hi) |
| $B757       | edge_adjacency      | Terrain edge neighbor lookup (72 edge pairs) |

## Fixed-Point Conventions

- Heights are 6-bit values (0-63) stored in the terrain buffer at $0900
- Screen coordinates are 8-bit (0-159 horizontal in mode D, 0-44 vertical)
- Rotation angles use full 8-bit range (256 = 360°)
- 16-bit intermediate results are carried in A (low) and ZP (high)
