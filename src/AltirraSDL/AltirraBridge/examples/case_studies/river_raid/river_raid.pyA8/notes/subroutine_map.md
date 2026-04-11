# River Raid — Subroutine Map

> **2026-04-11 update:** Several "subroutines" in this table are
> mislabelled. The most egregious are at $B05C-$B07C (claimed
> collision routines, actually digit-rendering helper entry points)
> and $B1C0-$B223 (claimed sound/lives display, actually state
> manipulation and M-DMA byte clearing). Corrections noted inline
> with **CORRECTED** tags. The original names are kept where they're
> exposed via project.json labels (renaming would break the export
> flow), but the descriptions are now accurate. See
> `notes/sprites.md` "What was wrong" §11 for the full list.

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
| $B05C | digit_render_a | **RENAMED** (was `check_terrain_collision`). Entry point into a shared digit-rendering helper at $B086. Sets `$B1=$90`, `X=$61`, `Y=$23`, then falls through. Renders some category of digits into screen RAM. Not a collision check. |
| $B064 | digit_render_b | **RENAMED** (was `check_entity_collision_p1`). Another entry point into the digit-rendering helper. Loads `lda #$02; bne $B086`. |
| $B068 | digit_render_c | **RENAMED** (was `check_entity_collision_p2`). Another entry point: `lda #$50; ldy #$0C; bne $B080`. |
| $B06E | digit_render_d | **RENAMED** (was `check_all_collisions`). Yet another digit-renderer entry point: reads `$09`, masks `#$0E`, ASLs and ADCs `#$80` to compute X (a screen RAM offset based on player number), then `ldy #$0C; bne $B082`. Probably the per-player score renderer. |
| $B07C | digit_render_e | **RENAMED** (was `process_collision_results`). Yet another entry point: `lda #$10; ldy #$02; ldx #$63; sta $B1; lda #$03; bit invuln_flags; bmi out; sta $B2`. The shared nibble loop at $B086-$B0C5 reads `temp_ptr_lo,X` as a BCD byte, splits into high and low nibbles, and writes them to screen RAM at `$1000+Y`. The actual collision dispatch is `check_entity_collision` at $A3E9 (entities.asm). |
| $B0C7 | spawn_entity | Try to spawn a new entity in an empty slot |
| $B0D8 | update_entity_movement | Update entity X position based on type/direction |
| $B0F1 | init_display_region | Initialize a display region (X = DL instruction count) |
| $B183 | update_fuel_gauge | Update the fuel gauge display in the status bar |
| $B194 | update_score_display | Write current score digits to status bar screen RAM |
| $B1C0 | state_helper_b1c0 | **RENAMED** (was `play_score_sound`). Touches no POKEY register. Saves Y/A into $7B/$7A, branches on `$4C`/`$23` state flags, can call `update_score_display` and the M-DMA byte-clear at $B221, then falls into $B200 which stores `$FE → fuel_level` and `$00 → death_timer`. Actual purpose unverified — likely a state-transition / reset helper. |
| $B21A | state_helper_b21a | **RENAMED** (was `update_lives_display`). Stores `$FF → frame_counter`, then `tax; bne $B200` to enter the same block as state_helper_b1c0. Does not update any visible lives counter. |
| $B221 | clear_status_line | **CORRECTED — LABEL IS WRONG.** M-DMA byte-clear helper. Loads `lda #$00`, falls into $B223. |
| $B223 | write_status_text | **CORRECTED — LABEL IS WRONG.** Continuation of the M-DMA clear: stores zero to 8 consecutive missile DMA bytes at `$0B01-$0B08+X`. Wipes M0/M1/M2/M3 bits in that window. Probably a bullet trail clearer. |
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
| $A688 | player_death_hook | **RENAMED** (was `player_death`). 5-instruction hook only: `lda $09; and #$01; sta player_number; ldx #$79; jmp state_start_game`. Toggles `player_number` from bit 0 of `$09` and jumps to `state_start_game` at $A15D (which clears RAM from `$79` upward). The real death processing is scattered across `entities.asm` (the `$1B`/`$1D` read branches in `check_entity_collision`), `state_dying_update` at $A7FB, and routines those call. To find the real entry point, watchpoint the byte that decrements when an on-screen life icon visibly disappears. |
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
