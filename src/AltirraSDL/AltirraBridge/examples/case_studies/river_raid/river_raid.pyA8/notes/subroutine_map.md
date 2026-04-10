# River Raid — Subroutine Map

## JSR Subroutines

All subroutines reachable via JSR within the game code range $A000-$BFFF.

| Address | Name | Purpose |
|---------|------|---------|
| $AF5B | update_entities | Update entity positions and states for current frame |
| $AF80 | prepare_display | Set up display parameters for the next frame |
| $AFD0 | frame_update | Main per-frame display update — advance scroll, prepare terrain |
| $AFEC | fill_terrain_row | Fill a terrain row with solid color (river or bank) |
| $AFF0 | fill_terrain_blank | Fill terrain row with $FF (blank/sky) |
| $AFF4 | fill_terrain_pattern | Fill terrain row from pattern data |
| $B00F | copy_row_data | Copy Y+1 bytes from ($A1) to ($A3) — terrain row blit |
| $B01A | copy_terrain_chunk | Copy a multi-row terrain chunk from pointer at $BA/$BB |
| $B034 | draw_entity_column | Draw entity graphics into a screen column |
| $B03B | clear_entity_column | Clear entity graphics from a screen column |
| $B04B | advance_dst_ptr | Advance destination pointer $A3/$A4 by row pitch |
| $B05C | check_terrain_collision | Check if player overlaps terrain (river bank) |
| $B064 | check_entity_collision_p1 | Check entity collision for player 1 |
| $B068 | check_entity_collision_p2 | Check entity collision for player 2 |
| $B06E | check_all_collisions | Iterate entity slots and check collisions |
| $B07C | process_collision_results | Handle collision outcomes (score, destroy, death) |
| $B0C7 | spawn_entity | Try to spawn a new entity in an empty slot |
| $B0D8 | update_entity_movement | Update entity X position based on type/direction |
| $B0F1 | init_display_region | Initialize a display region (X = DL instruction count) |
| $B183 | update_fuel_gauge | Update the fuel gauge display in the status bar |
| $B194 | update_score_display | Write current score digits to status bar screen RAM |
| $B1C0 | play_score_sound | Play a scoring sound effect (Y = duration, A = type) |
| $B21A | update_lives_display | Update lives remaining indicator in status bar |
| $B221 | clear_status_line | Clear a status bar line (for refresh) |
| $B223 | write_status_text | Write text/graphics to status bar area |
| $B247 | setup_game_start | Initialize game state for a new game |
| $B261 | reset_entities | Clear all entity slots |
| $B298 | read_console | Read CONSOL switches ($D01F) and return decoded result |
| $B480 | set_terrain_colors | Set COLPF registers for current terrain type |

## JMP Targets (Major Code Blocks)

| Address | Name | Purpose |
|---------|------|---------|
| $A000 | cold_start | Game cold start — disable hardware, clear RAM, init display |
| $A15D | state_start_game | State 3: begin new game — clear ZP, init lives/score |
| $A197 | state_attract_wait | Wait for VCOUNT, read input during attract mode |
| $A271 | entity_update_loop | State 1: iterate entities during active gameplay |
| $A2EE | entity_dispatch | Dispatch entity behavior by type |
| $A3B1 | display_update_misc | Update miscellaneous display elements |
| $A3E6 | continue_frame | Continue main frame processing |
| $A436 | handle_extra_life | Award extra life and related display update |
| $A43A | finalize_entity | Finish entity update, return to loop |
| $A4E0 | process_entity_hit | Process a hit on an entity (score, destroy) |
| $A543 | check_score_threshold | Check if score crossed extra-life threshold |
| $A5AA | handle_bullet_fire | Process fire button press — launch bullet |
| $A5B7 | frame_terrain_update | Update terrain generation for current frame |
| $A605 | terrain_gen_advance | Advance terrain generator to next row |
| $A652 | terrain_bank_update | Update river bank positions |
| $A688 | player_death | Handle player death — start explosion animation |
| $A701 | main_loop_continue | Continue main loop — fuel management, return to sync |
| $A7FB | state_dying_update | State 2: update dying animation each frame |
| $A8C4 | death_animation | Death explosion animation frame |
| $A95F | game_over_check | Check if game is over (no lives left) |
| $A96A | respawn_player | Respawn after death |
| $A96C | game_over_display | Display game over state |
| $AA77 | two_player_switch | Switch active player (2-player mode) |
| $AA7A | continue_two_player | Continue after player switch |
| $AB37 | deep_gameplay_logic | Advanced gameplay logic (later sections) |
| $AD37 | entity_ai_update | Entity AI — movement patterns for enemies |
| $AE86 | fuel_and_continue | Fuel management, then continue to main loop |
| $AE94 | decrement_fuel | Decrement fuel counter |
| $AE96 | jump_to_main | JMP $A701 → back to main loop |
| $AE99 | main_frame_sync | **Main loop entry** — wait for VCOUNT sync |

## Interrupt Handlers

| Vector | Address | Name | Purpose |
|--------|---------|------|---------|
| VVBLKI ($0222) | $B2A5 | vbi_handler | Immediate VBI: sound engine + timers |
| VDSLST ($0200) | $B500 | dli_handler | DLI: scanline kernel (sprite multiplex + terrain) |
| NMI ($FFFA) | $C018 | os_nmi | OS NMI dispatcher (routes to VBI/DLI) |
| IRQ ($FFFE) | $C02C | os_irq | OS IRQ dispatcher (keyboard, serial) |

## Call Frequency (from profiling)

| Subroutine | Calls/frame | Total cycles | Purpose |
|------------|-------------|-------------|---------|
| $B500 (DLI) | 1 (but runs ~170 scanlines) | 1,194,150 | Display kernel |
| $B2A5 (VBI) | 1 | 17,713 | Sound + timers |
| $AFD0 (frame_update) | 1 | ~5,000 | Frame prep |
| $B00F (copy_row) | 2-3 | ~3,000 | Terrain blit |
| $B04B (advance_ptr) | 2-4 | ~500 | Pointer math |
| $B0D8 (entity_move) | 6 | ~13,500 | Per-entity |
| $B0C7 (spawn) | 0-1 | ~12,500 | When slot free |
| $B07C (collision) | 1 | ~10,500 | Per-frame |
| $B183 (fuel_gauge) | 1 | ~4,600 | Display update |
