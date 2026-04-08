# Complete Subroutine Map

246 procedures identified across 15 subsystems. Statistics as of analysis
with pyA8 enhanced analyzer (register-based subsystem detection):

## Subsystem Distribution

| Subsystem   | Count | Description |
|-------------|-------|-------------|
| render      | ~70   | Screen buffer writes, terrain/sprite rendering |
| math        | ~24   | Arithmetic, bit extraction, 16-bit mul/div |
| timing      | ~20   | Frame sync, RTCLOK/VCOUNT polling, wait routines |
| util        | ~18   | Memory copy/fill, table operations |
| sound       | ~14   | POKEY audio channel management |
| pmg         | ~13   | Player-Missile Graphics positioning and shapes |
| game_logic  | ~11   | Score, entities, random selection, pilot detection |
| display     | ~6    | Color registers, DLI, display list configuration |
| init        | ~3    | Vector installation, hardware setup |
| input       | ~3    | Joystick, keyboard, IRQ |
| unknown     | ~14   | Insufficient HW access for classification |

## Key Procedures by Call Count (Fan-In)

| Callers | Address | Name                 | Subsystem |
|---------|---------|----------------------|-----------|
| 45      | $94AA   | queue_push_flagged   | game_logic |
| 41      | $9290   | queue_push           | game_logic |
| 28      | $7D9C   | wait_n_frames        | timing |
| 20      | $7DAF   | wait_1               | timing |
| 18      | $5AAE   | pixel_shift_index    | render |
| 18      | $7DB7   | wait_2               | timing |
| 16      | $5F75   | random_select        | game_logic |
| 14      | $476C   | set_viewport_offset  | render |
| 14      | $7CD7   | memcpy_block         | util |
| 14      | $7DAA   | wait_60              | timing |
| 13      | $94B1   | entity_load_state    | game_logic |
| 12      | $4B39   | clear_hud_line       | util |
| 12      | $7B4A   | enable_display       | display |
| 12      | $A754   | plot_sprite_pixel    | render |
| 11      | $92AA   | entity_swap          | game_logic |

## Anomalous Procedures (Flagged)

6 procedures use non-standard 6502 patterns:

| Address | Name             | Flag              | Meaning |
|---------|------------------|-------------------|---------|
| $4CF1   | dli_dispatch     | indirect_jmp      | JMP ($xxxx) dispatch |
| $52E7   | level_init       | bit_skip_trick    | $2C (BIT abs) to skip 2 bytes |
| $7B97   | sub_7B97         | bit_skip_trick    | " |
| $7B9E   | sub_7B9E         | bit_skip_trick    | " |
| $7BF0   | sub_7BF0         | bit_skip_trick    | " |
| $9119   | sound_dispatch   | bit_skip_trick    | " |

## Data Regions

| Address Range   | Label              | Content |
|-----------------|--------------------|---------|
| $3800-$3BFF     | charset            | Custom charset — 128 chars for cockpit/HUD |
| $0900-$09FF     | terrain_height_buf | 256-byte fractal terrain height ring buffer |
| $0A00-$0AFF     | pilot_map          | Pilot location tracking buffer |
| $24BE-$24FF     | terrain_stack      | Fractal midpoint displacement recursion stack |
| $4DD6            | dli_jump_table     | DLI stage handler address table (8 entries) |
| $61EF            | dli_table_alt      | Alternate DLI dispatch tables (2×8 entries) |
| $6B9E-$7909     | terrain_lut        | Perspective transform, height-to-screen LUTs |
| $B757            | edge_adjacency     | Terrain edge neighbor lookup table |

## Code Segment Layout

| Segment | Range           | Size    | Primary Content |
|---------|-----------------|---------|-----------------|
| 0       | $3C00 (header)  | 2 bytes | Load stub |
| 1       | $3800-$3BFF     | 1024 B  | Custom charset data |
| 2       | $3C00-$52E6     | 5863 B  | Init, title screen, IRQ, display list, color |
| 3       | $52E7-$852F     | 12873 B | Game engine: terrain, VBI/DLI, entity, sound |
| 4       | $8530-$B7E6     | 12983 B | 3D rendering: transform, projection, fill |
| 5       | $0000 (padding) | 2 bytes | Padding |

Total: 32,777 bytes, 100% classified (24,494 code + 9,269 data).
Assembly export verified byte-exact with MADS.
