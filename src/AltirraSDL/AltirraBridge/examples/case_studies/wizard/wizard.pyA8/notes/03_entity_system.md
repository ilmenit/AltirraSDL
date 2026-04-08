# Entity System

## Architecture Overview
The game manages up to 17 simultaneous entities across 4 groups using a
structure-of-arrays layout with circular buffer indices for round-robin updates.

## Entity Groups
| Group | Slots | Index Var | X Array   | Y Array   | Dir Array | Type Array |
|-------|-------|-----------|-----------|-----------|-----------|------------|
| 1     | 10    | $5104     | $B000     | $B100     | $B200     | $B300      |
| 2     | 3     | $5105     | $B400     | $B480     | $B500     | $B580      |
| 3     | 2     | $5106     | $B600     | $B640     | $B680     | $B6C0      |
| 4     | 2     | $5107     | $B700     | $B720     | $B740     | $B760      |
| 4-alt | -     | $5107     | $B780     | $B783     | $B786     | $B789      |

Note: Group 4 has a secondary set at $B780+ for entity type 4 (3-slot mini-group).

## Update Loop ($1B00 - entity_update_all)

Called every VBI frame during gameplay. Processes each group sequentially:

```
For each group G (1-4):
  Set loop_count ($9E) = group_size
  Set entity_type_indicator ($A2) = 0x14 + group_offset
  Set group_index ($BD) = group_number
  Load circular_index from $5104+G

  For loop_count iterations:
    X = circular_index
    Load Y_position from Y_array[X]
    If Y_position = 0: skip (empty slot)

    Copy all 4 properties to zero page:
      $A3/$A4 = X/Y position (screen address)
      $A0 = direction/behavior
      $A1 = type/speed

    JSR entity_move ($2300)

    Write back updated properties to arrays

    DEX (next entity in circular order)
    If X wraps below 0: reload from initial_index ($5180+G)

  Store updated circular_index back to $5104+G
```

The circular buffer means entities are processed starting from a different
position each frame, giving fair update distribution. The initial index values
at $5180-$5186 are reset copies of $5100-$5106 from level init.

## Entity AI ($2300 - entity_move)

The most complex routine in the game. Entity behavior has multiple phases:

### Phase 1: Tile Check ($2300-$2358)
```
Read tile at entity's current position: ($9A),Y where Y=0
Compare with:
  - $A2 (entity's own type marker): entity has been here -> jump to Phase 3
  - $0B (wall): entity at wall -> jump to Phase 3
  - $CD (tracking tile from V shield):
    . Save direction in $B8
    . Jump to Phase 2 (entity dies/captured)
```

### Phase 2: Item Deposit ($2323-$2357)
Based on which entity group ($BD):
```
If BD = 4 (group 4, bosses):
  If tile type AND $3F < 5 (open ground):
    Write tile $86 (castle supply item)

If BD = 2 (group 2):
  If tile type AND $3F < 5:
    RANDOM AND $03 compared to difficulty ($511A)
    If RANDOM < difficulty: write tile $85 (castle supply)

Otherwise:
  Write entity's appearance byte ($A0) to tile
```

This is the item generation mechanic -- monsters leave behind collectible
castle supply tiles ($85/$86) as they walk. Harder difficulty = more items
deposited by group 2.

### Phase 3: Entity Dies / Score Update ($2358-$238B)
```
Clear Y position (entity removed from map)
Decrement alive_count[$BD] at $5121
BCD: score_lo -= 1, score_hi -= carry
BCD: foul_damage_lo += 1, foul_damage_hi += carry
```

Every entity death decrements SCORE and increments CASTLE damage counter.

### Phase 4: Direction Decision ($238C-$241A)
When entity hits wall/marker, recalculate direction:

```
JSR entity_calc_position ($24FF) -- get entity screen coordinates

Direction byte ($A1) bits [1:0] control behavior mode:
  Mode 0-1 (bits < 2): RANDOM WANDERING
    - 1/32 chance: completely new random direction
    - 31/32: use direction from $6B44 lookup table based on bits [4:2]

  Mode 2-3 (bits >= 2): TARGETED MOVEMENT
    - bits [3:2] select target:
      . 0-2: target castle (from $6BB3 table)
      . 3: target wizard position ($5109/$510A)
    - Check $6C25+target: if castle destroyed, take random direction
    - Otherwise: JSR entity_distance_calc ($2519)
    - Compare X and Y distances to target
    - Choose direction that moves closer:
      . If Y distance > X distance: prioritize vertical
      . If X distance > Y distance: prioritize horizontal
      . Combine into direction bits (up/down/left/right)
    - If no valid direction found: take random direction
```

### Phase 5: Movement Execution ($241B-$24FE)
```
Subtract 1 from direction -> Y index into $6BC6 (sign) and $6BBC (delta)
If $6BC6[Y] != 0: subtract delta from position
Else: add delta to position

Check destination tile at ($9C),Y:
  $C7-$CA (barrier): destroy it -> tile = $1D, JSR item_collect with type 2
  $CD (tracking): destroy -> tile = $1E, sound $BF0A, JSR item_collect
  $F2+ (indestructible): can't pass -> go to passable check
  $E0-$E2 (shield barrier):
    Random check against $6BDA[group_index] probability
    If break: tile = $E3 (broken shield)
    If fail: change direction randomly
  $07-$0D (walls, not $0B):
    $0B specifically = wall, blocked
    $07-$0A: blocked, change direction
    $0E-$11: destroy gate -> tile = $00, sound $BF01
  Empty ($00) or passable:
    Write entity appearance to old tile
    Read destination tile content -> save as new $A0
    Write entity marker ($A2) to destination
    Clear direction high bits

Castle arrival check ($24E7):
  If entity position matches $5109/$510A (castle coords):
    Place $1D tile, JSR item_collect with type 0 (castle damage)
```

## Entity Spawning ($4100)

Runs when $5120 timer reaches 0:

```
1. RANDOM AND $1F -> index into $54E0 type mapping table -> type (0-4)
2. X = type index
3. DEC spawn_timer[$510B + X]
4. If timer wraps to $FF: reset from $6BE7[X] table
5. Check alive_count[$5121 + X] vs max_alive[$6BE2 + X]
6. If at max: skip, set spawn_interval = 1, return

7. Entity type dispatch via $4140 jump table:
   Type 0 -> $414A: check $B480[Y] for group 2 vacancy
   Type 1 -> $4150: check $B480[Y] for group 2
   Type 2 -> $4156: check $B640[Y] for group 3
   Type 3 -> $415C: check $B720[Y] for group 4
   Type 4 -> $4162: check $B783[Y] for group 4-alt

8. If vacancy check value != 0: set spawn_interval = 1, return

9. Generate position:
   RANDOM AND $7E -> screen column (even)
   If < 3: reject, set spawn_interval = 1, return
   Store to $96 (address low byte)

   Read tile at ($96),Y:
   If (tile AND $3F) >= 5: reject (not open ground)

10. Generate appearance:
    RANDOM AND $03 + $6F -> character code ($6F-$72)
    If RANDOM bit 7 set: add $2C ($9B-$9E range)
    Store to $97

11. Store entity data:
    $96 -> X position array
    $97 -> Y position array
    saved_tile ($98) -> direction array
    RANDOM AND $1F -> type array (from $41C3)

12. INC alive_count, BCD score += 1
13. Set spawn_interval from $51A0, return
```

## Difficulty Scaling

The difficulty level ($511A, set from title screen 1-9) affects:
1. **Entity item deposit rate**: group 2 deposits castle supply tiles more
   often at higher difficulty (RANDOM AND $03 < difficulty)
2. **Castle damage scaling**: $511A is halved (LSR) each time a castle is
   destroyed, reducing damage rate
3. **Entity max alive counts**: the $6BE2 table values are constant but
   the spawn timers at $510B reset from $6BE7 -- these may be initialized
   differently per level
