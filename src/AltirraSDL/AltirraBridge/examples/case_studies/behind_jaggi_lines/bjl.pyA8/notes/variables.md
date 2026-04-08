# Variable Analysis

137 variables named from cross-referencing profiling disassembly, runtime state
observation, and automated variable classification (1,436 addresses analyzed).
Total named labels: 403. All names applied via `proj.label()`.

## Zero Page — Math/Rendering Workspace ($80-$B8)

Named from instruction-level disassembly of the hot path routines.

### General purpose
| Address | Name | Description |
|---------|------|-------------|
| $80/$81 | `zp_ptr_lo/hi` | General pointer — screen row addr (pixel write), direction table (transform) |
| $82 | `zp_column_x` | Current column X position — `STX $82` in rasterize inner loop |
| $84 | `zp_depth_accum` | Depth interpolation accumulator — averaged with edge samples |
| $86 | `zp_color_shade` | Color/shade interpolation — terrain color gradient during edge walk |
| $B6 | `zp_temp` | Temp — Y save during pixel write, partial result during transform |

### 3D Transform
| Address | Name | Description |
|---------|------|-------------|
| $83 | `zp_proj_x_hi` | Projected X high byte (from project_x) |
| $85 | `zp_proj_y_hi` | Projected Y high byte |
| $88 | `zp_grid_x` | Terrain grid X component |
| $8A | `zp_grid_row` | Terrain grid row — upper nibble for heightmap index |
| $8B/$8C | `zp_cam_alt_lo/hi` | Camera altitude — subtracted from height in transform_vertex |
| $92 | `zp_dir_quadrant` | Direction quadrant — bits 7-6 select transform variant |
| $A0/$A1 | `zp_disp_x_lo/hi` | X displacement vector — heading projected to grid X step |
| $A2/$A3 | `zp_disp_z_lo/hi` | Z displacement vector — heading projected to grid Z step |
| $A8 | `zp_saved_x` | Saved X register (terrain_render) |
| $B5 | `zp_terrain_idx` | Terrain index composite — combined grid X/row |
| $B7 | `zp_grid_row_accum` | Grid row accumulator during vertex walk |

### Multiply ($9CCF)
| Address | Name | Description |
|---------|------|-------------|
| $A9/$AA | `zp_mul_result_lo/hi` | 16-bit multiply result |
| $AB/$AC | `zp_mul_cand_lo/hi` | Multiplicand input |
| $AD | `zp_mul_plier` | Multiplier (8-bit, doubles as shift reg) |
| $AE | `zp_mul_sign` | Sign save — bit 7 = negate result |

### Division ($9DA7)
| Address | Name | Description |
|---------|------|-------------|
| $AF/$B0 | `zp_div_divisor_lo/hi` | Divisor — `BIT $B0` tests bit 6 for normalization |
| $B1/$B2 | `zp_div_remain_lo/hi` | Dividend/remainder |
| $B3 | `zp_div_quotient` | 8-bit quotient output |
| $9F | `zp_norm_shift` | Normalization shift count — passed between project_x and div16 |

### Terrain Fractal
| Address | Name | Description |
|---------|------|-------------|
| $98 | `zp_frac_depth` | Recursion depth / stack pointer |
| $99 | `zp_frac_prev_depth` | Previous depth (compared against $98) |
| $9A | `zp_frac_delta_x` | Step direction (+1/-1/+16/-16) |
| $9B | `zp_frac_delta_y` | Perpendicular step |
| $9C | `zp_frac_seed_pos` | Current seed position in heightmap |
| $B8 | `zp_random_work` | Random perturbation workspace |

### Edge Sample Arrays (indexed by Y)
| Address | Name | Description |
|---------|------|-------------|
| $95 | `zp_edge_x_arr` | Edge sample X positions — terrain edge column positions |
| $96 | `zp_edge_x_next` | Next-level subdivided X — output of midpoint bisection |
| $E3 | `zp_depth_arr` | Depth values at each edge sample point |
| $E4 | `zp_depth_interp` | Interpolated depth output |
| EEXP ($EC),Y | — | Color interpolation offsets (OS name) |
| NSIGN ($ED),Y | — | Interpolated color/shade (OS name) |

### Screen/DMA
| Address | Name | Description |
|---------|------|-------------|
| $BC/$BD | `zp_screen_ptr_lo/hi` | Screen pointer — heavy write, rendering copy loops |
| $BE/$BF | `zp_screen_ptr2_lo/hi` | Secondary screen pointer — double-buffer ops |
| $C0 | `zp_color_work` | DLI/color workspace (R=20, W=18) |
| $C2/$C3 | `zp_memcpy_dst_lo/hi` | Memcpy dest ptr — most-written ZP pair |
| $C4/$C5 | `zp_memcpy_len_lo/hi` | Memcpy length |
| $C7 | `zp_render_scratch` | Rendering scratch |
| $C8 | — | DLI stage counter (already labeled) |
| $E1/$E2 | `zp_dli_vec_lo/hi` | DLI indirect jump vector |
| $97 | `zp_blend_row` | Row counter for pixel blend loop |

## Game State ($0600-$065E)

Named from runtime change detection (100-frame observation during flight)
and access pattern analysis.

| Address | Name | Evidence |
|---------|------|----------|
| $0603 | `game_ready_flag` | Only written #$00, R=3, W=3 |
| $0604 | `game_active_flag` | Only written #$01, R=3, W=3 |
| $0605 | `game_phase_timer` | Increments ~2 per 100 frames at runtime |
| $0609 | `frame_counter` | Referenced by wait_n_frames; incremented by VBI |
| $060C | `terrain_scroll_accum` | ADC/SBC in terrain_render ($A5A5/$A5B4) |
| $060F | `terrain_scroll_rate` | Speed-dependent, SBC in terrain_render ($A545/$A5AD) |
| $0616 | `enabled_flag_0616` | Only written #$01 |
| $061B | `game_timer` | R=9, W=4 — frequently read counter |
| $061C | `game_mode` | R=15, W=2 — most-read state variable in the game |
| $0622 | `slow_timer` | Incremented by VBI title screen handler |
| $0629 | `game_event_flag` | R=6, W=2 — triggers game events |
| $0636 | `dirty_flag` | Only written #$FF, read once — triggers redraw |
| $0638 | `score_lo` | BCD, ADC #$50 (500 points per pilot rescue) |
| $0639 | `score_hi` | BCD, ADC #$01/#$02 |
| $063B | `lives_or_fuel` | Value 02 at runtime |
| $063F | `score_digit` | BCD increment at $656B |
| $0640 | `display_enabled` | Only written #$01 |
| $0641 | `terrain_detail` | Written by terrain_render based on camera distance |
| $0643 | `terrain_vis_flag` | Set #$80 when terrain close, #$00 when far |
| $0654 | `slot_available` | Only written #$00, R=4 |

## Entity System ($065F-$0719)

**12 parallel arrays, stride 14, 14 entries each** — the game entity system.
Runtime observation shows entities at indices 0-3 are active (non-zero type),
indices 4-10 are inactive (zero), indices 11-13 carry persistent state.

| Base | Name | Access | Runtime sample |
|------|------|--------|----------------|
| $065F | `ent_type` | 24 ops, 15 procs | FF A0 A0 A0 00 40 A0 00 00 00 00 80 80 A0 |
| $066D | `ent_flags` | 21 ops, 13 procs | A0 00 00 00 00 00 00 00 00 00 00 01 04 01 |
| $067B | `ent_state` | 14 ops, 9 procs | 01 FE FF 2F 00 1F 00 00 00 00 00 0E DF DF |
| $0689 | `ent_param_a` | 5 ops | DB FF FF FC 00 00 00 00 00 00 00 00 00 00 |
| $0697 | `ent_param_b` | 3 ops | (all zero) |
| $06A5 | `ent_param_c` | 4 ops | (all zero) |
| $06B3 | `ent_param_d` | 4 ops | 00 22 22 00 ... |
| $06C1 | `ent_param_e` | 3 ops | 00 00 00 00 00 FF 00 00 00 00 00 FF 00 FF |
| $06CF | `ent_param_f` | 4 ops | FF 00 00 00 00 1F ... |
| $06DD | `ent_param_g` | 4 ops | (all zero) |
| $06EB | `ent_param_h` | 6 ops | 00 00 00 00 00 2C ... 00 2C |
| $06F9 | `ent_param_i` | 4 ops | 2C 00 00 00 ... |

| $0707 | `ent_active_flags` | R=6, W=3 | 00..00 08 04 02 06 01 0B ... |
| $071B | `cmd_queue_data` | 32 slots | Written by queue_push (25 writers) |
| $073B | `cmd_queue_ptr` | R=26, W=27 | Most-accessed address in the game |

## Camera State ($2800+)

Named from runtime change detection (heading/pitch/altitude change during flight).

| Address | Name | Evidence |
|---------|------|----------|
| $289B/$289C | `cam_heading_lo/hi` | Changed $1AEE→$1B84 (small turn in 100 frames) |
| $289D/$289E | `cam_pitch_lo/hi` | Changed $8F6B→$928B |
| $28A0 | `cam_altitude` | Value $2D=45 during flight |
| $28A4 | `flight_param` | R=11, W=7 — affects terrain render |
| $28A5 | `difficulty_level` | R=14, W=1 — written once at level init |
| $28A9 | `terrain_near_dist` | Controls detail level $0641 |
| $28AC | `viewport_state` | R=11, W=11 |
| $2843 | `prev_detail_level` | Compared with $0641 |
| $2882 | `camera_view_param` | R=9, W=2 |
| $2887 | `render_toggle` | Values $00/$01, alternates per frame |

## Vertex Arrays ($2270-$24BD)

45 entries each, one per terrain grid vertex. Named from transform_vertex
and project_x disassembly.

| Base | Name | Description |
|------|------|-------------|
| $2270 | `vtx_grid_idx` | Terrain grid index per vertex |
| $229D | `vtx_cam_x_lo` | Camera-space X lo |
| $22CB | `vtx_cam_x_hi` | Camera-space X hi |
| $22F9 | `vtx_cam_z_lo` | Camera-space Z lo (divisor for projection) |
| $2327 | `vtx_cam_z_hi` | Camera-space Z hi |
| $2355 | `vtx_height_lo` | Scaled height lo (after altitude subtract + shift) |
| $2382 | `vtx_height_hi` | Scaled height hi |
| $23AF | `vtx_raw_height` | Raw terrain height (from $0900 heightmap) |
| $23DC | `vtx_screen_x_lo` | Projected screen X lo (project_x output) |
| $2409 | `vtx_screen_x_hi` | Projected screen X hi |
| $2436 | `vtx_screen_y_lo` | Projected screen Y lo |
| $2463 | `vtx_screen_y_hi` | Projected screen Y hi |
| $2490 | `vtx_vis_flags` | Visibility flags: $80=behind, $40=right, $20=left, $10=projected |

## Rendering Tables

| Address | Name | Description |
|---------|------|-------------|
| $0751 | `row_addr_lo_blend` | Screen row addr lo — for pixel blend (phase 3 fill) |
| $07A7 | `row_addr_hi_blend` | Screen row addr hi — paired with $0751 |
| $2608 | `zbuf_columns` | Per-column Z-buffer — 212 entries, nearest terrain depth |
| $2634 | `zbuf_sentinel_a` | Z-buffer sentinel — init $67 per frame |
| $2648 | `zbuf_sentinel_b` | Z-buffer sentinel — init $6B, 4 sub-arrays |
| $2708 | `horizon_profile` | Horizon height — 32 entries, offset per distance |
| $28D0 | `height_delta_tab` | Height deltas — perspective correction |
| $28EC | `row_addr_lo_render` | Scanline addr lo — for pixel write in inner loop |
| $291C | `row_addr_hi_render` | Scanline addr hi — paired with $28EC |
| $2572 | `work_proj_x_lo` | Working projected X lo (per edge pair) |
| $2590 | `work_proj_x_hi` | Working projected X hi |
| $25AE | `work_proj_y_lo` | Working projected Y lo |
| $25CC | `work_proj_y_hi` | Working projected Y hi |
| $25EA | `work_height` | Working terrain height |
| $27E9 | `saved_column_x` | Saved column X (fill_terrain_column) |
| $2728 | `edge_iter_y_save` | Edge iteration Y save (terrain_render) |
| $28F6 | `current_vtx_idx` | Current vertex index (terrain_render) |

## Pixel Rendering Tables ($BC00-$BFFF)

| Address | Name | Description |
|---------|------|-------------|
| $BC00 | `pix_color_mask` | ORA mask for plotting pixel at column X |
| $BD00 | `pix_byte_offset` | Byte offset within scanline — rebuilt each frame |
| $BE00 | `pix_blend_cont` | Blend continuation — maps byte → next blend mask |
| $BF00 | `pix_blend_merge` | Blend merge — additional ORA bits for dithered fill |
