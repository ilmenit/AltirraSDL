# Execution Architecture

## VBI-Driven Game Loop
ALL game logic runs inside the deferred VBI handler at $5A00. There is no
main-line loop. The OS calls VVBLKD -> $5A00 every frame (60Hz NTSC).

## Main VBI Flow ($5A00)
```
1. Check game_running ($6C07) -- skip everything if 0
2. CLD (clear decimal mode, safety)
3. Load game_state ($6C0D), multiply by 2
4. Look up handler from $6B30 table
5. JSR to handler via JMP ($6C0E) at $5A45
6. STA HITCLR (clear collision registers)
7. JSR sound_engine ($5D00) -- runs every frame
8. If game_state >= 2 and != 5:
   a. DEC spawn_interval ($5120)
   b. If zero: JSR entity_spawner ($4100)
9. Clear ATRACT ($4D = 0)
10. Reset CH to $FF (no key pressed)
11. Copy $512C to PCOLR2 (dynamic color update)
12. JMP XITVBV ($E462) -- return from VBI
```

## State Machine Details

### State 2: Main Gameplay ($5500)
This is the most complex state. Runs every frame during active gameplay:
```
1. Charset animation:
   - DEC $511F (animation timer)
   - When 0: INC CHBASE by 2 (cycle through font variants)
   - Wraps from $50 back to $519E
   - Also cycles COLOR2 by +$10 (color shift)
2. JSR entity_update_all ($1B00) -- move all entities
3. Load COLOR0 from $5196
4. Sub-state dispatch via $6B80 table:
   - state_advance ($6C0B) * 2 -> handler address
   - JSR via JMP ($6C0E)
5. Set HPOSP3 from $5101 (wizard X tile position)
6. JSR player_sprite ($5B00) -- render wizard PMG
7. JSR player_state_handler ($5600) -- handle V shield, spell_flag
8. JSR score_display ($5700) -- update status bar
9. Check keyboard:
   - M ($25): enter map view (hide PMG, switch DList to $5000, INC game_state)
   - ESC ($1C): game_state = 5 (death/restart)
```

### State 4: Title Input ($5A4C)
```
1. JSR entity_update_all -- entities still move in title screen
2. INC PCOLR3 -- color cycling animation
3. Check CH:
   - $25 (M key): ignored (skip)
   - $FF: no key, skip
   - Any other: game_state = 1, hide PMG (start new game)
```

### State 5: Death/Restart ($4380)
Simple keyboard handler — no gameplay logic runs in this state:
```
1. Check CH for ESC ($1C) or no key ($FF):
   - $1C (ESC): RTS (stay in state 5, wait)
   - $FF (no key): RTS (wait for keypress)
   - Any other key: game_state = 2 (resume gameplay)

Note: dead code at $4391 contains an alternate gameplay handler with S/V key
checks, but it is unreachable and branches into sprite data at $4414/$4424.
This was likely an earlier version abandoned during development.
```

### State 9: Game Over ($6480)
```
1. DEC PCOLR3 (color drain animation)
2. DEC $6C02 (game over timer)
3. When timer = 0:
   a. Copy score from $6DC7 to $6849 (last score)
   b. game_state = 0 (idle)
   c. Hide all PMG players
   d. Compare last score vs high score at $6835
   e. If new high: copy to $6835
```

## Sub-State Cycle (Walking/Wand)

The gameplay alternates between walking and wand modes:

```
Sub-state 0 (WALKING, $5A80):
  - JSR collision_handler ($6900):
    . Zero score delta counters
    . Calculate wizard tile position from $5100/$5101
    . Check P3PF (Player 3 to Playfield):
      - Bit 0: cursor hit entity -> JSR cursor_hit_handler ($2100)
      - Bit 2: wizard hit special -> JSR wizard_special_collision ($6954)
    . If move_cooldown ($6C06) = 0:
      - Read STICK0, XOR $0F -> direction bits
      - Store to $6C00 (scroll direction) and $6C05 (wand_active)
      - Set cooldown = 8 frames
    . Else: DEC cooldown, keep last direction
  - If wand_active: JSR scroll_engine ($1900), JSR wand_render ($5880)
  - Check STRIG0 (fire button):
    . Pressed (=0), no cooldown, no spell_flag, player_state 0 or 3:
    . INC sub_state -> go to sub-state 1

Sub-state 1 (WAIT RELEASE, $5AAC):
  - Wait STRIG0 != 0 (fire released)
  - Restore PCOLR3 from $6B16, PCOLR0 = $0E
  - INC sub_state -> go to sub-state 2

Sub-state 2 (WAND MODE, $6A00):
  - Wizard (Player 3) stays stationary at tile position (HPOSP3 = wizard_x_tile)
  - Player 0 separates from Player 3 and becomes the wand cursor
  - JSR color_flash ($6A4D) -- cycle PCOLR0 hue
  - If STRIG0=0 (fire) or CH!=FF (key): INC sub_state -> exit wand
  - Read STICK0 -> direction bits
  - Store to $6C00 and $6C05
  - If direction != 0: JSR wand_render ($5880)
  - Store wand_direction -> wizard_move_dir
  - Meanwhile, player_sprite ($5B00) moves cursor (Player 0):
    . If wand_active != 0: player_x += wand_dx[wizard_move_dir],
      player_y += wand_dy[wizard_move_dir] (4px/frame, clamped)
    . If wand_active == 0: player_auto_move returns cursor toward wizard
      (target = joy_dx_table[wand_direction] + view_offset)
  - player_tile_check ($5C00) runs P0PF collision checks on cursor:
    tiles $01-$0A entity kill, $0B multi-hit wall, $14-$18 item collect

Sub-state 3 (RETURN, $6A30):
  - JSR color_flash
  - Wait STRIG0 != 0 (fire released)
  - sub_state = 0, restore PCOLR0 and PCOLR3
  - Back to walking mode
```
