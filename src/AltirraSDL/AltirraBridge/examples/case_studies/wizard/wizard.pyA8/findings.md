# Reverse Engineering: wizard.xex
# Last updated: 2026-03-25

## Status
- Segments: 1 data segment ($1A90-$6DD7, 21320 bytes), RUN at $3C0A
- Labels: 190 (all named and commented)
- Routines analyzed: ~50 routines fully traced
- Banks: none (no extended memory)
- Game logic fully understood

## Game Overview

**Wizard** is a real-time strategy/action game where the player controls a wizard
defending castles from waves of monsters on a scrolling tile map.

### Core Gameplay Loop
1. The wizard walks across a large scrolling tile map (walking mode, sub_state 0)
2. Monsters spawn randomly and navigate toward the 3 castles
3. The wizard presses fire to enter WAND MODE (sub_state 2) -- the wizard stays
   stationary (Player 3 at tile position) while a wand cursor (Player 0)
   moves freely with the joystick. Map does not scroll.
4. The wand cursor interacts with tiles via P0PF: destroys entities (multi-hit
   walls, kills entities, collects items) and can reach the S/V activation point
5. Pressing fire again returns to walking mode (cursor merges back with wizard)
6. S/V activation: in wand mode, move cursor to Player 2 corner sprites, press
   fire. player_y < $64 = S (shield build), >= $64 = V (tracking)
7. If any castle's power reaches 0, the game ends (state 9)
8. If POWER reaches 0 (from costs of actions), the game also ends

### Win Condition
Survive as long as possible. Score comes from monsters spawned (SCORE counter)
and FOUL tracks castle damage. POWER is the wizard's resource currency.

## Loading Sequence
| Step | Address       | Action                                         |
|------|---------------|------------------------------------------------|
| 1    | $1A90-$6DD7   | Load single segment (21320 bytes)              |
| 2    | RUN -> $3C0A  | Copies $1A90-$3C8F to $0400-$25FF (8704 bytes) |
| 3    | JMP $0917     | game_init: map decoder -> level setup          |

The entry point self-relocates code: the XEX loads at $1A90 but the game code
runs at $0400-$25FF. This contains the scroll engine, entity system, map decoder,
and other core routines. Everything above $3C4A stays at its loaded address.

## State Machine

### Main States (game_state at $6C0D, dispatch via $6B30)
| State | Handler | Purpose                                             |
|-------|---------|-----------------------------------------------------|
| 0     | $6BFF   | Idle (RTS)                                          |
| 1     | $6895   | Setup: switch to gameplay DList, load colors, CHBASE |
| 2     | $5500   | **MAIN GAMEPLAY** (runs every frame)                |
| 3     | $5A48   | Title attract: only entity_update_all (demo mode)   |
| 4     | $5A4C   | Title input: any key -> state 1, ESC excluded       |
| 5     | $4380   | Death/restart: any key -> game_state=2, ESC=wait    |
| 9     | $6480   | Game over: castle destroyed, high score check       |

### Sub-States within Gameplay (sub_state at $6C0B, dispatch via $6B80)
| Sub | Handler | Purpose                                              |
|-----|---------|------------------------------------------------------|
| 0   | $5A80   | **WALKING**: wizard moves via joystick scroll, collision check, fire->sub 1 |
| 1   | $5AAC   | Wait for fire release, restore colors, advance to sub 2 |
| 2   | $6A00   | **WAND MODE**: joystick moves wizard body, color flash. Fire/key->sub 3 |
| 3   | $6A30   | Return from wand: wait fire release, sub_state=0    |

### Gameplay Frame ($5500 - runs every VBI in state 2)
```
1. Animate character set (cycle CHBASE through font variants)
2. Call entity_update_all (move all 17 entities)
3. Dispatch by sub_state via $6B80 table
4. Update wizard PMG position (HPOSP3)
5. Render player sprite ($5B00)
6. Handle player_state / spell_flag ($5600)
7. Update score display ($5700)
8. Check keyboard:
   - M ($25): switch to map view (state 3 attract mode with map DList at $5000)
   - ESC ($1C): game_state = 5 (death/restart)
```

## Scrolling Engine ($1900)

The game uses hardware HSCROL/VSCROL for smooth scrolling of the ANTIC mode 7
tile map. The scroll engine at $1900 reads joystick direction bits from $6C00
and processes each direction:

- **Up** ($192D): DEC VSCROL by 2. At -2 boundary: reset to $0E, INC $5111 (map Y), update all 11 LMS addresses in display list
- **Down** ($1966): INC VSCROL by 2. At $10 boundary: reset to 0, DEC $5111, update LMS
- **Left** ($1997): INC HSCROL. At 8 boundary: reset to 0, DEC $5110 (map X), update column pointers
- **Right** ($19C5): DEC HSCROL. At -1 boundary: reset to 7, INC $5110

Edge handlers ($1800-$189F) manage the map boundaries and entity data
adjustment when scrolling reaches the edge of the map.

The scroll variables:
- $5109/$510A: coarse map position (column/row)
- $5110/$5111: fine map position within tile
- $6C03/$6C04: HSCROL/VSCROL hardware values
- $6C1A/$6C1B: boundary lock flags (prevent scrolling past map edges)
- $BB/$BC: screen memory base pointer (adjusted on every scroll)

## Wand Mode Mechanic

Player 0 is dual-purpose: in walking mode it overlaps Player 3 as part of the
wizard character; in wand mode it separates and becomes the freely-moving wand
cursor while Player 3 stays stationary at the wizard's tile position.

When the player presses fire in walking mode:
1. Sub-state advances to 1 (wait for fire release)
2. On release: sub-state 2 = **WAND MODE**
3. In wand mode ($6A00):
   - Joystick direction is stored as $6C05 (wand_active, from STICK0 XOR $0F)
   - Direction remapped via wand_render -> wand_direction ($6C17)
   - Direction indexes into shape table at $6B02 for wand indicator graphics
   - Wand indicator rendered as **Player 3** in PMG area $1720 (stays at wizard_x_tile)
   - Color flash effect ($6A4D) cycles PCOLR0 hue
4. Fire or any keyboard key exits wand mode -> sub-state 3 -> back to walking

### Wand Cursor Movement (Player 0 in wand mode)
In `player_sprite` ($5B00), when sub_state == 2:
- **Joystick held** (wand_active != 0): cursor (Player 0) moves at 4px/frame
  via wand_dx/wand_dy tables ($6B94/$6B9E) indexed by wizard_move_dir.
  Clamped to $30-$C8 (X), $04-$94 (Y) by player_clamp_x
- **Joystick released** (wand_active == 0): `player_auto_move` returns cursor
  toward the wizard position (target = joy_dx_table[wand_direction] + view_offset)
- On exiting wand mode: cursor snaps back to overlap with Player 3 (wizard)

### Walking Mode Collisions (P3PF, collision_handler $6900)
In sub_state 0, Player 3 (HPOSP3 = wizard_x_tile, fixed at tile position) detects
collisions as the map scrolls under it:
- **P3PF bit 0** -> cursor_hit_handler ($2100): checks tile at wizard position
  - Tile $0B (wall): single-hit destroy, place $86, cost $50 POWER, sound $BF0E
  - Tile $14-$18 (entity markers): replace with $1D, collect via $1F00 (item_collect A=0)
- **P3PF bit 2** -> wizard_special_collision ($6954): special tile interaction
  - Tile $85-$86 (mana gems): collect, add to POWER, add to SCORE
  - Tile $8E-$91 (treasure behind barrier): collect if foul_damage_hi allows

### Wand Cursor Collisions (P0PF, player_tile_check $5C00)
In sub_state 2, the wand cursor (Player 0 at player_x/player_y) detects collisions
as the cursor moves across the screen:
- **P0PF bit 0** -> check_tile_at ($5C60): checks tiles at cursor position
  - Tile $01-$0A: entity kill via $5CCD (10 POWER cost, 50 SCORE, DEC entity_kill_count)
  - Tile $0B (wall): multi-hit destroy via wall_hits_remaining; when 0 -> $86 gem
  - Tile $14-$18 (items): collect via item_collect, sound $BF0B

### Wand Mode Counters (check_tile_at internals)
Two hidden counters limit wand cursor power:

**wall_hits_remaining ($512A)**: initialized to $FA (250). Each cursor hit on a
wall ($0B) DECs it. When it reaches 0: wall breaks into $86 gem, sound $BF11.
After that, counter stays at 0 and BEQ skips all future wall hits — wand wall
breaking is exhausted for the rest of the game.

**entity_kill_count ($512B)**: initialized to $00. Each cursor kill on entity
tiles ($01-$0A) DECs it. First DEC wraps $00→$FF. After 256 kills, counter
reaches 0 and triggers:
1. BUG: adds undefined score_delta to SCORE (A = power_hi from power_subtract)
2. Writes tiles $32-$35 in 2x2 pattern to castle map address ($7178 from $6BAB)
3. Plays wall-break sound $BF11
Counter wraps and repeats every 256 cursor kills.

## Entity System

### 4 Entity Groups
| Group | Slots | Index  | Data Area    | Purpose              |
|-------|-------|--------|--------------|----------------------|
| 1     | 10    | $5104  | $B000-$B3FF  | Main monsters        |
| 2     | 3     | $5105  | $B400-$B5FF  | Stronger monsters    |
| 3     | 2     | $5106  | $B600-$B6FF  | Special enemies      |
| 4     | 2     | $5107  | $B700-$B7FF  | Boss/rare enemies    |

Each entity has 4 properties stored as parallel arrays:
- X position (screen address low byte)
- Y position (screen address high byte)
- Direction/behavior byte
- Type/appearance byte

### Entity AI ($2300 - entity_move)

Entities navigate the tile map with these behaviors:

1. **Check current tile**: if it matches the entity's own type marker ($A2) or
   is a wall ($0B), the entity has reached a dead end -> go to step 5

2. **Deposit item on tile**: based on entity group:
   - Group 2 (BD=$02): if tile type < 5, randomly (RANDOM AND $03 < difficulty)
     deposit castle supply tile $85
   - Group 4 (BD=$04): if tile type < 5, deposit $86
   - Otherwise: write entity's appearance byte to current tile

3. **Pathfinding** (when entity hits wall $0B or type marker):
   - Direction byte bits [1:0] < 2: random wandering with direction from $6B44 table
   - Direction byte bits [3:2]: determines which castle to target (0-2) or wizard (3)
   - $6C25 flags: if target castle is destroyed, redirect to random
   - Calculate distance to target castle/wizard using $2519
   - Choose direction that moves closer (up/down/left/right or combination)

4. **Movement execution** ($241B):
   - Look up position delta from $6BBC table
   - Check destination tile:
     - $C7-$CA (barrier): replace with $1D, add type 2 item via $1F00
     - $CD (tracking tile): replace with $1E, trigger sound $BF0A, call $1F00
     - $F2+ (strong barrier): can't pass
     - $E0-$E2 (shield barriers): random chance to break through ($6BDA probability)
       - If break succeeds: replace with $E3 (broken barrier)
       - Probability varies by entity group: G1=$40, G2=$00, G3=$80, G4=$80
     - $07-$0D (wall tiles, but not $0B): blocked, change direction
     - $0E-$11 (gate tiles): destroy gate, sound $BF01
     - Empty or passable: move entity there

5. **Score tracking**: every entity move decrements SCORE by 1 (BCD) and
   increments CASTLE damage counter ($6CC7-8)

6. **Castle arrival check** ($24E7): if entity reaches the position matching
   $5109/$510A (castle map coordinates), place $1D tile and call item_collect
   with type 0 (damages castle)

### Entity Spawning ($4100 - entity_spawner)

Called when $5120 timer reaches 0 (every N frames, N from $51A0):
1. RANDOM AND $1F -> index into $54E0 table -> entity type (0-4)
2. Check alive count ($5121+type) vs max ($6BE2+type): skip if at max
3. RANDOM AND $7E -> candidate screen position (even column)
4. Read tile at position: must be < 5 (empty ground) to spawn
5. RANDOM AND $03 + $6F -> entity appearance character
6. RANDOM bit 7 -> appearance variant (+$2C)
7. Store into appropriate group's data arrays
8. Increment alive count and BCD score

### Spawn Limits per Type
| Type | Max Alive | Respawn Max | Group |
|------|-----------|-------------|-------|
| 0    | $FA (250) | $F9 (249)   | 1     |
| 1    | $7E (126) | $7D (125)   | 2     |
| 2    | $40 (64)  | $3F (63)    | 3     |
| 3    | $20 (32)  | $1F (31)    | 4     |
| 4    | $03 (3)   | $02 (2)     | 4 alt |

## Tile System

The map is made of ANTIC mode 7 characters (20 chars wide, 16 scanlines tall,
4 colors). Tile values have specific meanings:

| Tile Range | Meaning                                            |
|------------|----------------------------------------------------|
| $00        | Empty ground (passable)                            |
| $01-$04    | Ground variants (passable, entities can spawn here) |
| $05-$06    | Decorative (impassable for spawn, passable for move)|
| $07-$0A    | Gate/fence tiles (destroyable by entities)         |
| $0B        | Wall (indestructible barrier, but wand can destroy) |
| $0C        | Player tracking marker (sets player_state=0)       |
| $0E-$11    | Gate tiles (entities destroy these, trigger sound)  |
| $14-$18    | **Collectible items** (power gems, crystals)       |
| $19+       | Special markers                                    |
| $1D        | Entity death marker (placed when entity killed)    |
| $1E        | Item collection marker                             |
| $32+       | Status bar display characters ($50=digit 0)        |
| $85-$86    | **Castle supply tiles** (deposited by entities, collectible by wizard for POWER) |
| $8E-$91    | **Treasure tiles** (behind barriers, unlocked by foul_damage_hi) |
| $C7-$CA    | **Barriers** (entities break these to reach castles) |
| $CD        | **Monster tracking tile** (placed by V shield mode) |
| $DF-$E2    | **Shield barriers** (built via S activation, strength 1-3) |
| $E3        | Broken shield barrier (entity broke through)       |
| $F2+       | **Passable special terrain** (entities walk through these) |

## Scoring and Economy

### Status Bar Layout
Note: the display labels do NOT match intuitive variable naming:
- **"FOUL" line** shows: FOUL net monster count (3d) + FOUL damage counter (4d)
- **"POWER" line** shows: POWER (4d)
- **"CASTLE" line** shows: 3 castle power counters (2d each)
- **"SCORE" line** shows: player SCORE (6d, inverse video) + static "45"

### POWER ($511B-$511C, BCD, 4 digits)
POWER is the wizard's main resource.

**Gaining POWER:**
- Collecting mana gems ($85-$86) by walking: +$50/+$01 POWER (via $6BF9 table)
- Collecting items ($14-$18) via cursor: gains from $5480 lookup table (BCD)

**Spending POWER:**
- Building shields (S activation): costs $6BDE[strength] POWER per shield tile
- Entity kills (walking or cursor): -$10 POWER per kill
- Destroying a wall by walking: -$50 POWER
- Entity arriving at castle: subtracts from POWER via $1F00
- If POWER reaches 0 or goes negative: **GAME OVER** (game_state = 9)

### FOUL — net monster count ($6C29-$6C2A, BCD, 3 digits)
Displayed as first number on the "FOUL" line.
Incremented by 1 each time an entity is spawned.
Decremented by 1 each time an entity moves (in entity_move at $2368).
Represents net active monster population.

### FOUL — damage counter ($6CC7-$6CC8, BCD, 4 digits)
Displayed as second number on the "FOUL" line.
Incremented by 1 every time an entity moves.
Tracks total cumulative monster movement. Used as unlock key for treasures
(treasure tiles $8E-$91 require foul_damage_hi > tile_value XOR 3).

### SCORE ($6CC0-$6CC2, BCD, 6 digits)
Displayed in inverse video on the "SCORE" line.
The player's cumulative activity score — grows with all actions:
- Entity kill: +$50
- Mana gem type 1 collected: +$01
- Mana gem type 2 collected: +$20
- Various other events via score_add ($4300)
- Auto-incremented by difficulty value each scoring tick (score_display $5723)

### Castle Power ($5127-$5129, BCD)
Each of the 3 castles has its own power counter.
- Starts at $90 each (shown in status bar)
- Decremented when entities reach the castle ($1F63)
- Amount subtracted depends on entity group ($6BEC table)
- When a castle reaches 0: $6C24+X flag set, $511A halved (LSR)
- When $511A becomes 0: **GAME OVER** (game_state = 9)

## S and V Activation (via Player 2 collision in wand mode)

S and V are NOT keyboard shortcuts. Activation requires **wand mode** (sub_state 2):

1. Enter wand mode (press and release fire)
2. Push joystick toward the S/V corner — wand cursor (Player 0) moves at 4px/frame
3. When cursor touches Player 2: `P0PL AND #$04` detected by
   `check_activation_point` ($5648) in `player_state_handler` ($5600)
4. Pressing fire triggers the ability AND exits wand mode
5. player_y determines which ability: < $64 = S (shield), >= $64 = V (tracking)

Note: the dead code block at $4391 contains S/V keyboard checks that branch
into sprite data — this was abandoned during development and is unreachable.

### Shield Barrier (S) - wizard Y < $64 (upper area of Player 2)
1. spell_flag = 1 (charging), then spell_flag = 2
2. spell_flag = 2 calls $6300 (shield_build)
3. Check POWER > 0, strength = min(power_digit, 3)
4. Place shield tile $DF + strength ($E0-$E2) at wizard position
5. Subtract cost from POWER via $6BDE table
6. Sound $BF0D

Shield tiles are semi-permeable:
- Group 1: $40 probability -> ~25% chance to break through
- Groups 3-4: $80 probability -> ~50% chance to break through
- Broken shields become $E3 (debris, passable)

### V-Shield Tracking (V) - wizard Y >= $64 (lower area of Player 2)
1. player_state = 1 (tracking mode)
2. Places a single stationary $CD tracking tile at `$BB + $81` (fixed map offset)
3. Any entity walking onto $CD is automatically destroyed (entity_move $2318)
4. Acts as a stationary landmine — the tile does NOT move after placement

## Entity Remnants ("Rings")

When entities move or die, they leave visual remnants on tiles:
- Entity appearance bytes ($6F-$72 or $9B-$9E): deposited as entities walk
- Castle supply tiles ($85/$86): deposited by Group 2/4 on open ground
- $1D marker: placed when entity reaches castle or dies to tracking tile
- These are the "rings" visible on the map. Supply tiles are collectible for POWER.

## Map View (M Key)

Pressing M ($25) during gameplay:
1. Sets colors from $6BA8 (map view palette)
2. Hides all PMG players (HPOSP0=HPOSP2=HPOSP3=0)
3. Switches display list to $5000 (map overview)
   - Mode $0D (160x2 hi-res graphics) with LMS=$A000
   - Shows the entire map at reduced resolution
4. Increments game_state to 3 (attract mode)
5. Pressing any key returns to gameplay

## Display Architecture

### Gameplay Display ($6D00)
- 3x BLANK 8 lines
- 11x MODE 7 lines with LMS (scrollable tile map, 128 bytes per row)
  - LMS addresses: $8A2D, $8AAD, $8B2D, $8BAD, $8C2D, $8CAD, $8D2D, $8DAD, $8E2D, $8EAD, $8F2D
  - DLI on line 4 ($6D0C) for color split
- BLANK 5 lines
- 4x MODE 6 lines (status bar: FOUL, POWER, CASTLE, SCORE)
- JVB (jump and wait for VBI)

### Map Display ($5000)
- 3x BLANK 8 lines
- ~60x MODE $0D lines (160x2 graphics mode, entire map visible)

### PMG Layout (PMBASE=$10)
- Missiles ($1300): cursor, wand projectile indicators
- Player 0 ($1400): wizard character (animated 4 frames)
- Player 1 ($1500): unused?
- Player 2 ($1600): special object (V shield target)
- Player 3 ($1700): wand cursor (15-byte directional sprites)

### Character Animation
The game cycles through multiple character sets by incrementing CHBASE ($511E)
by 2 every N frames ($511F countdown). This creates animated tiles (flickering
trees, moving water, etc). Range: $511E to $50, wraps to $519E.

## Sound System ($5D00)

Sound effect engine with 20 trigger slots ($BF00-$BF13) mapped to 4 POKEY
hardware channels (AUDF1-4/AUDC1-4). NOT 20 simultaneous channels -- the
Atari only has 4 POKEY voices. Each slot triggers a specific sound effect
routine that writes to one hardware channel. Multiple slots share channels,
with later triggers overriding earlier ones.

Slot status bytes:
- $00 = slot inactive
- $01 = trigger new sound (consumed on next frame, starts effect routine)
- $02+ = effect in progress (envelope/frequency updated each frame)

The engine at $5D00 iterates X from $13 down to 0 (20 slots), grouped in
sets of 5 (one group per POKEY channel). $C2 tracks the group base.
For each slot: if value=1, call init routine (from table at $5D65);
if value>1, call update routine (from table at $5D8D).

Init routines set AUDF/AUDC values, envelope parameters in $BF14-$BF5F area.
Update routines modify frequency/volume each frame for decay, sweep, tremolo.

Sound trigger assignments:
| Channel | Trigger | Event                          |
|---------|---------|--------------------------------|
| $BF00   | $01     | Monster spawn                  |
| $BF01   | $01     | Gate destroyed by entity       |
| $BF04   | $01     | Power depleted (game over)     |
| $BF05-07| $01     | Castle supply collected (x3)   |
| $BF08   | $01     | Item collected / shield crystal |
| $BF0A   | $01     | Tracking tile triggered         |
| $BF0B   | $01     | Item pickup (power gem)        |
| $BF0C   | $01     | Entity killed by cursor        |
| $BF0D   | $01     | Shield barrier built           |
| $BF0E   | $01     | Wall destroyed by wand         |
| $BF0F   | $01     | Treasure collected             |
| $BF11   | $01     | Wall fully destroyed           |

## Key Data Tables

### $6B30 - State Dispatch (10 entries)
### $6B80 - Sub-state Dispatch (4 entries: walk/wait/wand/return)
### $6BDE - Shield Cost per Strength (BCD)
### $6BE2 - Entity Max Alive per Type ($FA, $7E, $40, $20, $03)
### $6BE7 - Entity Respawn Max per Type ($F9, $7D, $3F, $1F, $02)
### $6BEC - Castle Damage Cost per Group
### $6BF9 - Power Gain from Castle Supply Collection
### $6BDA - Barrier Break Probability per Entity Group

## Profiling Summary (300 frames gameplay)
- 72% CPU utilization (21,493 cyc/frame of 29,868 NTSC budget)
- 53% in OS routines (mostly sound driver via POKEY timers)
- 11% in game code
- Hottest: player_auto_move (741), score_display (606), sound_engine (480),
  entity_update_all (400)
