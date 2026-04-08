# Behind Jaggi Lines! — Overview

## Identity
"Behind Jaggi Lines!" is the development title of **Rescue on Fractalus!** (1984, Lucasfilm Games).
One of the first games to use real-time fractal terrain generation on home computers.
Platform: Atari 800XL/XE, NTSC.

## Memory Map (Runtime)

### Zero Page ($0080-$00FF) — Math and Rendering Workspace
| Range       | Purpose |
|-------------|---------|
| $80/$81     | General-purpose pointer (screen addr, direction table ptr) |
| $82         | Current column X position (saved during pixel write) |
| $83         | Projected X high byte |
| $84         | Depth interpolation accumulator |
| $85         | Projected Y high byte |
| $86         | Color/shade interpolation value |
| $88/$8A     | Terrain grid position components |
| $8B/$8C     | Camera altitude lo/hi |
| $92         | Direction quadrant selector |
| $95-$96     | Edge sample arrays (column X positions, page 0) |
| $97         | Row counter for pixel blend loop |
| $9F         | Normalization shift count (for project_x ↔ div16) |
| $A0/$A1     | X displacement vector (from camera heading) |
| $A2/$A3     | Z displacement vector (from camera heading) |
| $A8         | Saved X register (terrain_render) |
| $A9/$AA     | Multiply result lo/hi |
| $AB/$AC     | Multiply multiplicand lo/hi |
| $AD/$AE     | Multiply multiplier / sign save |
| $AF/$B0     | Division: divisor lo/hi |
| $B1/$B2     | Division: dividend (remainder) lo/hi |
| $B3         | Division: quotient (8-bit) |
| $B5         | Terrain index composite |
| $B6         | Temp: Y save during pixel write, or partial result |
| $B7         | Terrain grid row accumulator |
| $C8         | DLI stage counter (reset to 0 each VBI) |
| $D0-$DF     | Color palette shadow (VBI copies to GTIA hardware) |
| $E1/$E2     | DLI indirect jump vector |
| $E3-$EF     | Depth values per edge sample point |

### Page 0 Extended ($0090-$009F) — Edge Working Arrays
| Range       | Purpose |
|-------------|---------|
| $0095,Y     | Edge sample X positions (indexed by edge point Y) |
| $0096,Y     | Next-level subdivided X positions |
| EEXP ($EC),Y| Color interpolation offsets per edge sample |
| NSIGN ($ED),Y| Interpolated color/shade per edge sample |
| $00E3,Y     | Depth values per edge sample point |
| $00E4,Y     | Interpolated depth output |

### Main Memory
| Range         | Size    | Content                                           |
|---------------|---------|---------------------------------------------------|
| $0600-$0700   | 256     | Game state variables, flags, counters              |
| $0641         | 1       | Terrain distance/detail level                      |
| $0707-$072A   | 36      | Entity state table (scanned in entity update loop) |
| $071B-$073A   | 32      | Command queue — circular buffer, 32 slots          |
| $0741-$0750   | 16      | Color palette source table (copied to $D0-$DF)    |
| $0751-$07FC   | 172     | Screen row address tables (lo at $0751, hi at $07A7) — for pixel blend |
| $0900-$09FF   | 256     | Terrain height buffer — 16×16 fractal heightmap ring (6-bit heights, bit 7 = unresolved) |
| $0A00-$0AFF   | 256     | Pilot location tracking buffer                     |
| $1010-$21B0   | ~4500   | Screen memory — terrain viewport (45 rows × 96-byte stride, ANTIC mode D) |
| $2270-$249F   | 560     | **Vertex arrays** — 45 entries each: grid index ($2270), X lo/hi ($229D/$22CB), Z lo/hi ($22F9/$2327), height ($23AF), projected X lo/hi ($23DC/$2409), projected Y lo/hi ($2436/$2463), height scaled ($2355/$2382), visibility flags ($2490) |
| $24BE-$24FF   | ~66     | Fractal recursion stack (terrain seeds)             |
| $2572-$25FF   | ~142    | Rendering work area (copied from vertex arrays per edge) |
| $2608-$26CB   | 196     | **Z-buffer** — per-column depth of nearest terrain (212 columns, reset each frame) |
| $2708-$2727   | 32      | Horizon height profile curve                       |
| $2800-$28AC   | ~172    | Camera state ($289B-$28A0), PMG positions, game parameters |
| $28D0-$28EF   | 32      | Height delta table (perspective correction per distance) |
| $28EC-$294B   | 96      | Scanline address tables (lo at $28EC, hi at $291C) — for pixel write |
| $316B-$320D   | 163     | Display list (gameplay)                            |
| $32B5-$332C   | 120     | Status text screen memory (ANTIC mode 6)           |
| $332D-$3400   | ~211    | HUD text screen memory (ANTIC mode 4)              |
| $3800-$3BFF   | 1024    | Custom character set (128 chars for cockpit/HUD)   |
| $3C00-$52E6   | 5863    | Game code: init, title, input, display lists       |
| $52E7-$852F   | 12873   | Game code: engine, terrain, VBI, rendering         |
| $8530-$B7E6   | 12983   | Game code: 3D engine, rendering tables, levels     |
| $B757-$B7E6   | 144     | Edge adjacency table — 72 vertex pairs defining terrain mesh |
| $BC00-$BFFF   | 1024    | **Pixel rendering tables** — 4 pages: color mask ($BC00), byte offset ($BD00, rebuilt per frame), blend continuation ($BE00), blend merge ($BF00) |

## Key Entry Points
| Address | Name            | Purpose                                    |
|---------|-----------------|--------------------------------------------|
| $3C00  | game_start      | Main entry — hardware init, VBI/IRQ setup  |
| $840A  | vbi_handler     | Title screen VBI                           |
| $8059  | gameplay_vbi    | Gameplay VBI — colors, PMG, DMA            |
| $4940  | irq_handler     | Custom IRQ — keyboard + IRQEN              |
| $4CF1  | dli_handler     | Gameplay DLI chain — per-scanline colors   |
| $843C  | init_terrain    | Fractal terrain buffer initialization      |
| $84E6  | terrain_step    | Terrain midpoint displacement step         |
| $7D9C  | wait_frames     | Frame-synced delay (waits on $0609)       |
| $52C0  | clear_mem       | Memory clear utility                       |
| $90BD  | init_sound      | Sound system initialization                |
| $7C80  | init_game       | Game state initialization                  |
| $82AA  | vbi_deferred    | Deferred VBI processing (from gameplay VBI)|

## Display Architecture
### Title Screen
- Simple ANTIC modes 6+7 (text), DL at $41D5
- VBI at $840A handles attract mode and color cycling
- No DLI chain active

### Gameplay
- **DL at $316B**: 45 rows of ANTIC mode D (160px, 4-color bitmap)
  with LMS on every row (96-byte stride for scroll/double-buffer)
- Screen memory at $1070-$21B0
- Three DLI trigger points:
  - Top of viewport: sky colors
  - Horizon line (~row 42): terrain/ground color switch
  - Bottom of viewport: HUD colors
- DLI handler at $4CF1 uses jump table at $4DD6 indexed by $C8 counter
- PRIOR=$11 during terrain (5th player + multicolor mode)

### Color Architecture
Colors are loaded from a palette table at $0741 (16 bytes) into ZP $D0-$DF.
The VBI writes $D0-$D5 to GTIA color registers. DLIs swap colors for
terrain vs sky vs cockpit sections. Attract mode EOR-scrambles the palette.

## Subsystem Architecture

The game code is organized into these major subsystems (identified via
hardware register analysis and call graph propagation):

| Subsystem   | Key Routines | Description |
|-------------|-------------|-------------|
| Rendering   | terrain_render, fill_column_fast, rasterize_edge, plot_sprite_pixel | Screen buffer writes, terrain fill, sprite blitting |
| 3D Math     | mul16_signed, div16, transform_vertex, project_x, load_camera | Fixed-point 16-bit math, camera transform pipeline |
| Terrain     | terrain_init, terrain_subdivide, terrain_check_neighbors | Fractal midpoint displacement, height buffer |
| Sound       | sound_init, sound_engine_update, sound_play_effect + 7 SFX routines | 4-channel POKEY management |
| Display     | dli_dispatch, dli_sky/horizon/ground/hud, patch_display_list | DLI chain, color register programming |
| PMG         | set_pmg_positions, update_pmg_data, update_pmg_colors | Player-Missile positioning, shape data |
| Game Logic  | queue_push (41 callers), entity_load_state, entity_swap, detect_pilot | Command queue, entity management, pilot rescue |
| Timing      | wait_n_frames (28 callers), wait_1..wait_60 | RTCLOK-based frame sync delays |
| Input       | irq_handler, read_joystick, setup_irq_vectors | Keyboard via IRQ, joystick via PIA |
| Init        | game_start, game_init, level_init | Hardware setup, state initialization |
| Utility     | memzero, memcpy_block, clear_screen, clear_hud_line | Memory operations |

The command queue at $071B (32 slots, circular buffer) is the central
inter-subsystem communication mechanism, referenced from 86 call sites.

## Analysis Coverage

- **32,777 bytes** total XEX size, 100% classified
- **24,494 bytes** code (103 regions), **9,269 bytes** data (42 regions)
- **246 procedures** identified via call graph analysis
- **1,563 labels** (277 named, 1,280 branch targets, 6 remaining auto)
- **308+ comments** (72 hand-written game-specific, rest auto-generated)
- **109 .proc blocks** in MADS assembly output
- Assembly export **byte-exact verified** against original XEX
