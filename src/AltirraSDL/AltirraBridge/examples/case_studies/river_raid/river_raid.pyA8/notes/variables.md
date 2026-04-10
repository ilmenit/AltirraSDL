# River Raid — Variable Map

## Zero-page variables ($00-$FF)

The game disables the OS and repurposes all of zero-page for its own use.
Atari OS names (like CASINI, DOSINI) appear in disassembly output but are
just the standard labels for those addresses — the game uses them for
completely different purposes.

| Address | Name | Size | Description |
|---------|------|------|-------------|
| $00-$01 | temp_ptr | 2 | General-purpose pointer (used by sprite renderer) |
| $02 | sprite_base | 1 | Sprite data base offset (set in DLI kernel) |
| $04-$05 | render_x / render_end | 2 | Scanline renderer X position / end marker |
| $06 | entity_slot | 1 | Current entity slot index for DLI kernel |
| $07 | entity_flags | 1 | Entity attribute flags for current scanline |
| $0C-$15 | river_left_bank[10] | 10 | Left bank X positions per terrain row (scroll with terrain) |
| $16 | scroll_phase | 1 | Fine scroll position within terrain row |
| $18 | frame_counter | 1 | Main frame counter (increments each game frame) |
| $19 | joystick_dir | 1 | Current joystick direction ($0F=none, $0E=up, $0B=left, $07=right, etc.) |
| $1A-$1E | collision_flags | 5 | Per-player collision status (set by DLI kernel from P/M collision regs) |
| $1F-$22 | dli_sprite_mask | 4 | Sprite visibility mask per scanline zone |
| $24 | game_state | 1 | Game state machine: $00=attract, $01=playing, $02=dying, $03=starting |
| $27 | bullet_x | 1 | Player bullet horizontal position |
| $28 | start_pressed | 1 | START button pressed flag |
| $29 | invuln_flags | 1 | Invulnerability / death animation flags (bit 7 = dying) |
| $2A | lives_count | 1 | Current lives remaining |
| $2E | bridges_crossed | 1 | Count of bridges successfully crossed |
| $2F | terrain_gen_idx | 1 | Terrain generation index (advances with scrolling) |
| $31 | temp_score | 1 | Temporary score accumulator |
| $32 | fuel_drain_ctr | 1 | Fuel drain counter (decrements, triggers fuel decrement on wrap) |
| $38 | fuel_state | 1 | Fuel tank state (bit 7 = refueling at depot) |
| $39 | player_screen_y | 1 | Player's screen Y position |
| $3A | scroll_position | 1 | Coarse scroll position (advances with terrain) |
| $3B | bullet_state | 1 | Bullet active/animation state |
| $40 | entity_timer | 1 | Entity spawn/update timer |
| $41 | sound_flags | 1 | Sound engine flags |
| $42-$43 | sound_freq | 2 | Sound frequency parameter |
| $47 | fire_pressed | 1 | Fire button state |
| $4D-$4E | difficulty | 2 | Difficulty / spawn rate parameters |
| $50 | collision_result | 1 | Result of collision check ($FF = hit detected) |
| $52 | player_x | 1 | Player horizontal position (river-relative) |
| $53 | river_width_L | 1 | Current river width (left edge param) |
| $54 | river_width_R | 1 | Current river width (right edge param) |
| $56 | bullet_column | 1 | Bullet's column in entity space |
| $57 | player_sprite_x | 1 | Player sprite HPOS value (absolute screen X) |
| $5A | section_type | 1 | Current river section type (1=normal, 2=narrow, etc.) |
| $5B | dli_scroll_offset | 1 | DLI uses this for missile positioning (OLDCOL) |
| $5D | terrain_density | 1 | Terrain obstacle density parameter |
| $5F | spawn_cooldown | 1 | Frames until next enemy can spawn |
| $60 | enemy_active | 1 | Bit flags for which enemy types are currently active |
| $61 | score_10k | 1 | Score: ten-thousands digit (BCD) |
| $62 | score_100 | 1 | Score: hundreds digit (BCD) / bridge counter |
| $63 | score_high | 1 | Score: high byte (BCD) |
| $64 | score_mid | 1 | Score: middle byte (BCD) |
| $65 | score_low | 1 | Score: low byte (BCD) |
| $66-$67 | score_display_ptr | 2 | Pointer to score digit display area in screen RAM |
| $68-$69 | hiscore_ptr | 2 | Pointer to high score display area |
| $6A | extra_life_thresh | 1 | Extra life threshold counter |
| $76 | fuel_level | 1 | **Fuel gauge level** ($FF=full, decrements to $00=empty) |
| $77 | player_number | 1 | Current player (0=P1, 1=P2) |
| $78 | option_pressed | 1 | OPTION key pressed flag |
| $7A-$7B | engine_sound | 2 | Engine sound parameters |
| $7C | explosion_timer | 1 | Explosion animation timer |
| $7E | level_advance | 1 | Level/section advance timer |
| $A0 | temp_counter | 1 | Temporary loop counter |
| $A1-$A2 | src_ptr | 2 | Source pointer for data copy operations |
| $A3-$A4 | dst_ptr | 2 | Destination pointer for screen writes |
| $A5-$A6 | terrain_seed | 2 | Terrain PRNG state |
| $AA-$AC | score_backup | 3 | Score backup during display update |
| $B1 | score_tens_digit | 1 | Score tens digit for display comparison |
| $B6-$B7 | render_param | 2 | Rendering parameters (entity height, etc.) |
| $B9 | joy_shifted | 1 | Joystick value shifted for 2-player lookup |
| $BA-$BB | data_ptr | 2 | Data pointer for terrain chunk copy |
| $BC-$BE | chunk_params | 3 | Terrain chunk size/type parameters |
| $BF | pal_flag | 1 | PAL/NTSC detection ($00=NTSC, $0E=PAL) |
| $C1 | terrain_draw_flag | 1 | Terrain drawing active flag |
| $C2 | frame_counter_copy | 1 | Copy of $18 (frame counter) |
| $C3-$C5 | entity_spawn | 3 | Entity spawn parameters |
| $C6 | sound_timer | 1 | Sound effect duration timer (decremented in VBI) |
| $C7 | cold_start_flag | 1 | Set to 1 after first init (prevents re-clearing RAM) |
| $C8-$C9 | entity_coords | 2 | Current entity screen coordinates |
| $CA | warm_start | 1 | Warm start flag (skip init if set) |
| $CB | scanlines_per_row | 1 | NTSC=$2F, PAL=$2E — scanlines per terrain row |

## RAM variables ($0500+)

| Address | Name | Size | Description |
|---------|------|------|-------------|
| $0500-$0505 | entity_type[6] | 6 | Entity type for each of 6 slots (0=empty, 1-6=enemy types, 7=fuel, $0E=bridge) |
| $0506-$050A | entity_anim[5] | 5 | Entity animation frame |
| $050B-$0510 | entity_shape[6] | 6 | Entity shape/sprite index |
| $0516-$051B | entity_xpos[6] | 6 | Entity horizontal position |
| $0521-$0526 | entity_size[6] | 6 | Entity sprite width (SIZEP value) |
| $052C-$052F | entity_misc[4] | 4 | Entity miscellaneous state |
| $0600-$06BF | pm_missiles | 192 | Player/Missile graphics: missiles area |
| $0700-$07CF | pm_player0 | 208 | Player 0 graphics (player's plane) |
| $0800-$08CF | pm_player1 | 208 | Player 1 graphics (bullet) |
| $0900+ | pm_player2_3 | var | Player 2/3 graphics (enemies, fuel depot) |
| $0B00-$0B07 | status_display | 8 | Status bar display data |
| $2000-$3CFF | screen_ram | ~7424 | ANTIC mode 14 bitmap (river + terrain) |
| $3D80-$3EFF | status_screen | 384 | Status bar screen RAM (score, lives, fuel gauge) |
| $3F00-$3FB5 | display_list | 182 | ANTIC display list (172 mode 14 lines + header) |

## Hardware registers used

| Register | Name | Usage |
|----------|------|-------|
| $D000-$D003 | HPOSP0-3 | Horizontal position of players 0-3 |
| $D004-$D007 | HPOSM0-3 | Horizontal position of missiles 0-3 |
| $D008-$D00B | SIZEP0-3 | Player width (normal/double/quad) |
| $D00A | M2PL | Missile-to-player collision (read in DLI) |
| $D010-$D013 | TRIG0-3 | Joystick trigger buttons |
| $D012-$D015 | COLPM0-3 | Player/missile colors (written per-scanline in DLI) |
| $D016-$D019 | COLPF0-3 | Playfield colors |
| $D01D | GRACTL | Graphics control (PM DMA enable) |
| $D01E | HITCLR | Collision register clear |
| $D01F | CONSOL | Console switches (START/SELECT/OPTION) |
| $D200-$D207 | AUDF1-4/AUDC1-4 | POKEY audio frequency and control |
| $D20E | IRQEN | IRQ enable |
| $D20F | SKSTAT | Serial port status (keyboard) |
| $D300 | PORTA | PIA port A (joystick directions) |
| $D302-$D303 | PACTL/PBCTL | PIA control registers |
| $D400 | DMACTL | ANTIC DMA control |
| $D407 | PMBASE | Player/Missile base address |
| $D40A | WSYNC | Wait for horizontal sync |
| $D40B | VCOUNT | Vertical line counter |
| $D40E | NMIEN | NMI enable |
