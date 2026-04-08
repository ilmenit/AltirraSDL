# Game Economy: Power, Scoring, Shields, Items

## Resource Overview

The status bar labels DON'T match intuitive variable naming:

| Line label | What it shows | Address      | Digits | Purpose                           |
|------------|---------------|-------------|--------|-----------------------------------|
| FOUL       | foul_hi/lo    | $6C29-$6C2A | 3 (BCD)| Net monster count (spawns - moves)|
| FOUL       | foul_damage   | $6CC7-$6CC8 | 4 (BCD)| Total monster movement steps      |
| POWER      | power         | $511B-$511C | 4 (BCD)| Wizard's resource currency        |
| CASTLE     | castle_power  | $5127-$5129 | 3x2(BCD)| Individual castle health         |
| SCORE      | score_total   | $6CC0-$6CC2 | 6 (BCD)| Player activity score (inverse)   |

Plus 3 individual castle power counters:
| Name     | Address | Init | Purpose                          |
|----------|---------|------|----------------------------------|
| Castle 0 | $5127   | $90  | Individual castle health (BCD)   |
| Castle 1 | $5128   | $90  | Individual castle health         |
| Castle 2 | $5129   | $90  | Individual castle health         |

## POWER System

POWER is the central resource. Starting value appears to be $0016 (from $511B/C
initial state). It is spent on all wizard actions and gained from collecting items.

### Gaining POWER
1. **Collecting castle supply tiles ($85-$86)** via wizard walking over them:
   - $6954 handler: tile $85 -> gain $6BF9[0] POWER, tile $86 -> gain $6BF9[1]
   - Table values: $6BF9 = $50, $01 -> so $85 gives 50 POWER, $86 gives 01 POWER
   - Also adds to SCORE via $6BFB table

2. **Collecting items ($14-$18)** via cursor:
   - $5C5E handler: when cursor touches tile $14-$18:
     . Replace tile with $1E
     . Sound $BF0B
     . JMP $1F00 (item_collect)
   - item_collect uses $A2 (tile value) - $14 as index Y
   - Type dispatch by first parameter:
     . Type 0: add $5480[Y] to POWER (BCD), add to SCORE
     . Type 1: sound $BF08, add $6BD5[Y] to SCORE
     . Type 2+: subtract $6BEC[Y] from castle power

3. **Treasure tiles ($8E-$91)** via wizard collision:
   - Only collectible if foul_damage_hi ($6CC7) > tile_value XOR 3
   - Gain $6BF1[X] to POWER, add $6BF5[X] to SCORE

### Spending POWER
1. **Shield building (S key)** at $6300:
   - Costs $6BDE[strength] per tile, where strength = min(POWER_digit, 3)
   - Shield cost table $6BDE: values are BCD amounts
   - Deducted from $511B-$511C immediately

2. **Entity arrival at castle** via $1F00 type 2+:
   - Subtracts from castle power ($5127-$5129)
   - Amount from $6BEC table indexed by entity group

3. **Cursor entity kills** (indirect cost through $512B counter):
   - $5C95: each kill DECs $512B, when 0 -> triggers additional effects
   - Also resets score deltas and calls power_subtract

4. **POWER underflow = GAME OVER**:
   - power_subtract ($4335): if result < 0, set POWER = 0, game_state = 9
   - Also triggers sound $BF04 (death sound)

## SCORE Counter
- **Incremented** by 1 each entity spawn ($4100, BCD add at $419F)
- **Decremented** by 1 each entity move ($2300, BCD subtract at $2368)
- Net SCORE = total_spawns - total_moves (reflects how many entities were quickly
  eliminated vs how many are still roaming)

## SCORE Counter (player activity score)
Player's cumulative score. Incremented through $4300 (score_add):
- Wall destroyed by wand: +$50
- Castle supply collected by wizard: +$6BFB[type] (variable)
- Item collected: +$6BD5[Y] (per item type)
- Various other event-specific amounts stored in $6CC3-$6CC5 before calling $4300

## Castle Power System

Each castle starts with $90 power (BCD = 90 decimal).

**Damage sources:**
- Entity reaching castle coordinates: JSR item_collect with type 2+
  . Amount from $6BEC table
  . Table: $01, $08, $02, $04 per entity group
  . When castle power reaches 0:
    - Castle destroyed flag set ($6C24+X)
    - $511A (difficulty) halved via LSR
    - If $511A becomes 0: **GAME OVER** (game_state = 9)
    - Castle tiles on map replaced with $F2/$F3 (destroyed castle, shows on map)

**Protection:**
- Build shield barriers (S key) to block entity paths
- Kill entities with wand cursor (P0PF in wand mode) before they reach castles
- Use V-shield tracking tiles ($CD, stationary) to auto-kill approaching entities

## Shield Building ($6300)

When activated (via S/V activation point in wand mode, see player_state_handler):
1. Read POWER display digit from status bar ($6D9F - $50)
   - If 0: can't build (no POWER)
2. Strength = min(digit_value, 3) -> shield strength 1-3
3. Check wizard's current tile: must be empty or < 5
4. Place shield tile: $DF + strength ($E0, $E1, or $E2)
5. Cost: $6BDE[strength] subtracted from POWER
6. Sound $BF0D

Shield properties:
- $E0 (strength 1): weakest, cheapest
- $E1 (strength 2): medium
- $E2 (strength 3): strongest, most expensive
- Entities have a random chance to break through shields
  (probability from $6BDA per entity group)
- Broken shields become $E3 (debris, passable)

## V Shield (Tracking Marker)

The V mode ($5600/$5672):
1. In wand mode, cursor (Player 0) must collide with Player 2 (P0PL bit 2)
2. If player_y >= $64 (lower area): INC player_state -> tracking mode
3. While holding fire: charges V shield ($512D += 6 per frame)
4. On release: places single stationary $CD tracking tile at $BB + $81
5. Sets $B9 = 4

Tracking tiles ($CD) work like stationary landmines:
- When an entity walks onto $CD in entity_move ($2318):
  . Entity saves its direction ($B8)
  . Jumps to death handler (Phase 2)
  . Entity is removed from the map
- The tile does NOT move after placement

## Treasure Tiles ($8E-$91)

These are high-value items behind barriers (visible on the map view):
- Only accessible when foul_damage_hi ($6CC7) > (tile_value XOR 3)
- This means they unlock as the game progresses and CASTLE counter grows
- Collecting them: clear tile, sound $BF0F, add to POWER via $6BF1 table,
  add to SCORE via $6BF5 table

## Items Behind Barriers

The barriers around items/treasures are tiles $C7-$CA (breakable) and
$F2+ (indestructible). Entities can break $C7-$CA barriers during movement
(entity_move $2449), which deposits $1D tile and triggers item_collect.

The wizard can reach items through:
1. Wand cursor can destroy wall ($0B) tiles adjacent to items
2. Building and positioning to access the area
3. Waiting for entities to break through barriers (they try to path toward castles)

## Enemy Base

The "enemy base" area in the map corner is surrounded by $F2+ tiles
(indestructible barriers). Entities spawn via random position generation
($4100), not from a specific base location. The base area likely contains
high-value treasure tiles ($8E-$91) that become accessible as CASTLE score
grows.
