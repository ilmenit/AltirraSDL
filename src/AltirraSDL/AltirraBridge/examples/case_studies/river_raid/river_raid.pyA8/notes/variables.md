# River Raid — Variable Map

> **2026-04-11 update:** Several variable labels in this table were
> found to be wrong during the DUO 2-player port re-read of the
> split asm files. Corrections noted inline with **CORRECTED** or
> **SUSPECT** tags. The original column values are kept where the
> name is plausibly partially right; flagged otherwise. See
> `notes/sprites.md` "What was wrong" section and the top-level
> `project.json` `notes` array for the full corrections list.

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
| $18 | frame_counter | 1 | **SUSPECT** — sound.asm:58 (`update_lives_display` at $B21A) explicitly stores `$FF` here from a non-VBI path. Not a simple monotonic counter. Likely a state-transition timer; verify with bridge watchpoint. |
| $19 | joystick_dir | 1 | Current joystick direction ($0F=none, $0E=up, $0B=left, $07=right, etc.) |
| $1A | (DLI capture) | 1 | **CORRECTED** — DLI-populated entity-Y capture, set when M2PF bits 0-1 fire (M2 vs PF, dormant). Read by entities.asm at $A581. $FF = no hit. |
| $1B | (DLI capture) | 1 | **CORRECTED** — DLI-populated entity-Y capture for **JET-VS-TERRAIN** (P2PF bit 0 = jet hit bank/bridge). Read by entities.asm at $A42E. $FF = no hit. |
| $1C | (DLI capture) | 1 | **CORRECTED** — DLI-populated entity-Y capture for narrow jet hitbox vs enemy (M2PL bit 3 = M2 vs P3, where M2 is the jet+4 hit probe). Read by entities.asm at $A4B6. |
| $1D | (DLI capture) | 1 | **CORRECTED** — DLI-populated entity-Y capture for wide JET-VS-ENEMY (P2PL bit 3). Read by entities.asm at $A3E9. |
| $1E | (DLI capture) | 1 | **CORRECTED** — DLI-populated entity-Y capture for P2 vs P0 (jet vs P0, currently dormant since P0 has no bitmap). Read by entities.asm at $A424. |
| $1F | (DLI capture) | 1 | **CORRECTED** — DLI-populated entity-Y capture for P3 vs P0 (enemy vs P0, dormant). Read by entities.asm at $A4B0. |
| $20 | (DLI capture) | 1 | **CORRECTED** — DLI-populated entity-Y capture for P1PF bit 1 (P1 vs PF1, dormant since P1 has no bitmap). Read by entities.asm and state_machine.asm. |
| $21 | (DLI capture) | 1 | **CORRECTED** — DLI-populated entity-Y capture for M0PL bit 2 (M0 vs P2 = M0 hit jet). NOT dormant — M0 is actively used (see $C1). Read by entities.asm at $A42E. |
| $22 | (DLI capture) | 1 | **CORRECTED** — DLI-populated entity-Y capture for M0PF bits 0-1 (M0 vs bank/bridge). Read by bullets_terrain.asm at $A5FD where it stops the M0 motion loop on playfield impact. |
| $24 | game_state? | 1 | **SUSPECT** — runtime values $00/$48/$80 don't match the claimed 0/1/2/3 state semantics. Real play-state variable not yet identified. |
| $27 | dli_status_x | 1 | Status bar player 3 horizontal position |
| $28 | start_pressed | 1 | START button pressed flag |
| $29 | invuln_flags | 1 | Invulnerability / death animation flags (bit 7 = dying) |
| $2A | death_timer | 1 | **RENAMED** (was `lives_count`). Countdown timer that triggers `player_death_hook` when exhausted. Decremented every 8 frames at entities.asm:89-103, reset to $25 (37) by the death hook at $A6D5. The real visible lives counter is somewhere else and has not been located. |
| $2E | bridges_crossed | 1 | Count of bridges successfully crossed |
| $2F | terrain_gen_idx | 1 | Terrain generation index (advances with scrolling) |
| $31 | temp_score | 1 | Temporary score accumulator |
| $32 | fuel_drain_ctr | 1 | Fuel drain counter (decrements, triggers fuel decrement on wrap) |
| $38 | fuel_state | 1 | Fuel tank state (bit 7 = refueling at depot) |
| $39 | bullet_x (legacy name) | 1 | Written to HPOSP1 at $B54F every frame. **P1's DMA buffer is never populated with a bullet bitmap** — verified via bridge write-watches over 180 frames. The actual bullet sprite is **M1** (packed missile DMA), drawn at `$B14B-$B15C`, positioned via `HPOSM1 = ($5B>>3)+$5C` at `$B52C`. The `HPOSP1 ← $39` store therefore has no visible effect; $39 drifts by ±1 per frame via an oscillator at `$A25B/$A25F` that is not correlated with fire input. Role not yet characterised; `bullet_x` is retained as legacy label. See `notes/sprites.md`. |
| $3A | scroll_position | 1 | Coarse scroll position (advances with terrain) |
| $3B | fuel_gauge_idx | 1 | Fuel-gauge row index: ASL'd at $AF0F to index fuel_gauge_ptrs ($BAB7) for per-row blits. (Was mislabelled `bullet_state`; it's never written as a bullet flag.) |
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
| $76 | fuel_level | 1 | **PARTIALLY SUSPECT** — primary role is fuel level ($FF=full, decrements to $00=empty), but the routine at $B200 (which `play_score_sound` $B1C0 falls into) explicitly stores `$FE` here from a non-fuel code path. Either dual-purpose or one of the labels is wrong. Verify fuel logic with bridge watchpoint before relying on this. |
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
| $C0 | m0_direction | 1 | **NEW LABEL** — M0 motion-direction byte. Set to $02 or $FE in `bullets_terrain.asm` at $A5F8 based on bit 7 of `$42,X`. Each frame the motion loop adds m0_direction to m0_x_alive to step M0's X position. |
| $C1 | m0_x_alive | 1 | **RENAMED** (was `terrain_draw_flag`). The M0 sprite's X position AND visibility gate, packed in one byte. (1) Read in the playfield DLI at $B561 (`lda m0_x_alive; sta HPOSM0`) — sets M0's screen X. (2) Read in frame_sync.asm at $AEF4 (`ldy m0_x_alive; beq skip; ora #$03; sta $0B00,X`) — gates M0 bit visibility in the missile DMA buffer. Updated by the motion loop in bullets_terrain.asm at $A5CA-$A607 together with $C0 and $22. |
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
| $0B00-$0BFF | pm_missiles | 256 | Packed missile DMA (single-line). **CORRECTED 2026-04-11:** M1 (bits 2-3) is the bullet at-rest template at `$0BCC,Y`, drawn by `$B14B-$B15C`. **M0 (bits 0-1) is NOT cleared** — `frame_sync.asm` at $AEF1 conditionally writes M0 bits at `$0B00+frame_ctr_copy` driven by `$C1`, producing a moving M0 dot with COLPM0 cycled through `player_color_cycle`. **M2 (bits 4-5) is the jet hit probe** — positioned at `player_sprite_x + 4` by the DLI at $B539, with collision capture into `$1C` consumed by entities.asm at $A4B6. Only M3 (bits 6-7) appears unused. See `notes/sprites.md` for the full corrected sprite map. |
| $0C00-$0CFF | pm_player0 | 256 | **P0 DMA — unused in gameplay.** Zeroed each frame by the generic indirect copier at `$B012`. `HPOSP0 ← $3C = 0` hides it. |
| $0D00-$0DFF | pm_player1 | 256 | **P1 DMA — unused in gameplay.** Never written. `HPOSP1 ← $39` positions an empty sprite. COLPM1 is used because missiles inherit their parent player's colour register — i.e. it's actually the M1 bullet colour. |
| $0E00-$0EFF | pm_player2 | 256 | **P2 DMA = the player's jet.** HPOSP2 ← `$57` (`player_sprite_x`). Verified. |
| $0F00-$0FFF | pm_player3 | 256 | **P3 DMA = multiplexed enemies.** HPOSP3 rewritten per scanline by the WSYNC loop at `$B567`. |
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
