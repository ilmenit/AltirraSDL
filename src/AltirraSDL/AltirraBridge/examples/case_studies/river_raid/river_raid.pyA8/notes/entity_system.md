# River Raid — Entity System

## Entity Slots

The game manages up to 6 entities simultaneously using parallel arrays
at $0500. Each entity occupies one slot (index 0-5) across multiple
arrays:

| Base | Array | Purpose |
|------|-------|---------|
| $0500 | entity_type[6] | Entity type identifier |
| $0506 | entity_anim[5] | Animation frame counter |
| $050B | entity_shape[6] | Shape/sprite table index |
| $0516 | entity_xpos[6] | Horizontal position on screen |
| $0521 | entity_size[6] | Sprite width (SIZEP register value) |
| $052C | entity_misc[4] | Movement direction / miscellaneous state |

## Entity Types

| Type | Name | Behavior | Sprite | Points |
|------|------|----------|--------|--------|
| $00 | empty | Slot is free for spawning | — | — |
| $01-$02 | ship_L/ship_R | Stationary in river, left/right facing | Small dark boat | Low |
| $03-$04 | heli_L/heli_R | Moves horizontally, changes direction | Helicopter | Medium |
| $05-$06 | jet_L/jet_R | Moves fast, flies toward player | Enemy plane | High |
| $07 | fuel_depot | Stationary, refuels player on overlap | Rectangular platform | 0 (don't shoot!) |
| $0C | balloon | Slow, floats upward | Round shape | Low |
| $0E | bridge | Spans river, must be destroyed to pass | Wide bar | Bonus |

## Entity Lifecycle

### Spawning ($B0C7)
Each frame, the spawn routine checks for empty slots (type $00) and
the spawn cooldown timer ($5F). When conditions are met:

1. Find first empty slot
2. Select entity type based on current difficulty ($4D/$4E) and
   section type ($5A)
3. Set initial position: X from the river edges, Y at the bottom
   of the visible area
4. Load shape index and size from the entity-type lookup tables

### Per-frame Update ($B0D8)
Each active entity is updated per frame:

1. Advance animation frame
2. Update X position based on movement pattern:
   - Ships: stationary
   - Helicopters: oscillate left/right
   - Jets: move toward player X
   - Fuel depot: stationary
   - Bridge: stationary (spans full river)
3. Check if entity has scrolled off the top of the screen
4. If off-screen: clear slot (type = $00)

### Rendering (DLI at $B560-$B60B)
The DLI kernel renders entities by iterating slots and setting
hardware sprite registers per scanline:

```
For each entity slot Y (0-5):
  Read entity_xpos[Y] → HPOSP3 (horizontal position)
  Read entity_size[Y] → SIZEP3 (width)
  Read entity_shape[Y] → lookup sprite data pointer
  For each scanline in entity's vertical extent:
    STA WSYNC              ; sync to scanline start
    LDA (sprite_ptr),Y     ; load sprite line
    STA COLPM3 ($D015)     ; set color
    LDA (color_ptr),Y      ; load color for this line  
    STA COLPF0 ($D016)     ; set playfield color (terrain)
```

The kernel uses hardware player 2 and player 3 (plus missiles)
for entities, while player 0 is the player's jet and player 1
is the bullet. By repositioning P2/P3 per scanline zone, 6
entities are displayed with 2 hardware players.

### Destruction ($A4E0)
When a collision is detected (bullet hits entity or player hits entity):

1. Look up score value from $BB0B table
2. Add to BCD score at $63/$64/$65
3. Set explosion timer ($7C)
4. Replace entity type with explosion animation type
5. Play explosion sound ($C6 timer)
6. After explosion animation completes: clear slot

## Entity Tables in ROM

### Sprite Shape Data ($B800-$BBFF)
Each entity type has sprite data in this 1KB region. The shape index
at $050B selects which block of bytes to use. Each sprite is typically
8-16 bytes tall.

### Entity Type Properties
Looked up from tables at $BB24-$BB5F:

| Table | Purpose |
|-------|---------|
| $BB24 | Type transition table (what type becomes after animation) |
| $BB30 | Sprite data offset per type |
| $BB60 | Sprite height per type |

### Score Values ($BB0B)
BCD score values indexed by entity type. Used by the scoring routine
at $A547 to add the correct amount to the running score.

## Collision Detection

Collisions are detected in two ways within the DLI kernel:

1. **Missile-to-player** (hardware): M2PL register ($D00A) bit 3
   detects when missile 2 (mapped to the bullet) overlaps player 3
   (mapped to the current entity). Read at $B5EA.

2. **Player overlap** (software): The DLI compares entity X positions
   against the river bank positions ($0C-$15). If an entity is outside
   the river, it's treated as a bank collision when the player sprite
   overlaps that same scanline zone.

Results are stored in $1A-$1E (one byte per entity zone) and
processed in the main loop's collision handler ($B07C).
