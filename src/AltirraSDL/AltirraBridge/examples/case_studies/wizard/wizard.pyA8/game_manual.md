# WIZARD - Game Manual

## Overview

Wizard is a real-time strategy and action game for the Atari 800XL/XE. You play
as a wizard defending three castles from endless waves of monsters on a large,
mostly open scrolling map. You walk the map to collect mana gems, use a magic
wand cursor to destroy monsters and activate special abilities, and build
defensive barriers to protect your castles.

---

## Controls

- **Joystick (walking mode)**: Scrolls the map. The wizard moves with the map.
- **Joystick (wand mode)**: Moves the wand cursor freely across the screen.
- **Fire button**: Press and release to toggle between walking and wand mode.
- **M key**: Show the full map overview.
- **Any other key**: Return to title screen.

### Title Screen
Choose difficulty (1-9) with the keyboard, then press fire to begin.
Higher difficulty makes monsters more aggressive but also makes them drop mana
gems more frequently.

---

## Walking Mode

The default mode. The joystick scrolls the map and the wizard stays roughly
centered on screen. As the map scrolls under the wizard, anything at the
wizard's position is automatically interacted with:

- **Walking over a monster**: destroys it (costs 10 Power, adds 50 Foul)
- **Walking over a mana gem**: collects it (gains Power)
- **Walking over a treasure**: collects it (if the unlock condition is met)
- **Walking over a wall**: destroys the wall in a single hit (costs 50 Power),
  leaving a mana gem in its place

The map is **almost entirely open terrain**. The only things that block
monsters are:
- Castle structures (a few tiles around each castle)
- Castle barriers (breakable tiles surrounding castles)
- Player-built shield barriers (the S ability)
- Boundary walls (vertical dividers on the map)

The wizard walks freely through everything. Monsters are blocked by castle
structures, boundary walls, and shield barriers, but they path around or
break through most obstacles.

---

## Wand Mode (Magic Cursor)

Press and release fire to enter wand mode:
- The map **stops scrolling**
- The wizard **stays stationary** where it was standing
- A **wand cursor** separates from the wizard and can be moved freely with
  the joystick
- The wizard's color flashes to indicate wand mode
- When the joystick is released, the cursor drifts back toward the wizard

**The wand cursor can interact with things on the map:**
- **Moving over a monster**: destroys it (costs 10 Power, adds 50 Foul)
- **Moving over a wall**: damages it; after several hits, the wall breaks
  and leaves a mana gem
- **Moving over an item**: collects it

The cursor can also reach the **S** and **V** activation sprites in the
upper-left screen corner to activate special abilities (see below).

Press fire again (or any key) to return to walking mode. The cursor merges
back with the wizard.

---

## Mana Gems

Mana gems are glowing collectible tokens scattered across the map. They are
your **primary source of Power**. The wizard collects them by **walking over
them** in walking mode. Two types exist, each with a different sound and value:

- **Type 1 gem**: +50 Power, +01 Foul
- **Type 2 gem**: +10 Power, +20 Foul

Mana gems appear on the map in two ways:
- **Deposited by monsters**: Mana carriers and elite monsters leave gems
  behind on open ground as they walk. Higher difficulty means more frequent
  deposits. This is the primary gem source.
- **From destroyed walls**: Breaking a wall turns it into a gem.

---

## Monsters

### Types
- **Common monsters** (Group 1): Up to 10 at a time. Wander with some
  pathfinding toward castles.
- **Mana carriers** (Group 2): Up to 3. Deposit mana gems as they walk.
  Drop rate increases with difficulty.
- **Marauders** (Group 3): Up to 2. More aggressive castle pathfinding.
- **Elite monsters** (Group 4): Up to 2. Most dangerous. Also deposit mana
  gems. Deal heavy castle damage.

A rare fifth type (up to 3) can also appear, similar to elites.

### Behavior
Monsters move tile by tile across the open map. Since the terrain is mostly
empty, they roam freely in all directions. Their AI mixes random wandering
with targeted pathfinding toward castles (or occasionally toward the wizard).

When a monster hits a blocking tile, it recalculates direction and tries to
go around.

Monsters can **break through**:
- Castle barriers: destroyed on contact
- Gates: destroyed on contact
- Player-built shield barriers: random chance per attempt
  (common monsters: ~25%, elite monsters: ~50%)

### Castle Damage
When a monster reaches a castle, it damages that castle's power. Stronger
monster types deal more damage per hit (ranging from 1 to 8 points).

---

## Special Abilities

### The S/V Activation Point
In the upper-left screen corner, two sprite labels are displayed: **S**
(upper) and **V** (lower). To activate an ability:

1. Press and release fire to enter **wand mode**
2. Move the wand cursor to the **S** or **V** sprite
3. Press fire — the ability activates and you return to walking mode

### Shield Barrier (S)
Places a defensive barrier tile at the wizard's current map position.

- Barrier strength depends on available Power (level 1 to 3)
- Costs Power (more for stronger barriers)
- Monsters have a random chance to break through:
  - Common monsters: ~25% per attempt
  - Elite monsters: ~50% per attempt
- Broken barriers become passable rubble

**Since the map is almost entirely open**, shield barriers are the ONLY way to
create obstacles that slow monsters down. You must strategically place them
to form defensive lines protecting your castles.

### Tracking Marker (V)
Places a single **tracking marker** on the map near the wizard's position.

- The marker is **stationary** — it does not move after placement
- Any monster that walks onto it is **automatically destroyed**
- Acts as a magical landmine
- While the marker is active, the fire button is locked — you cannot enter
  wand mode until the tracking effect completes

---

## Status Bar

The status bar has four lines:

```
FOUL   [3 digits] [4 digits]
POWER  [4 digits]
CASTLE [2d] [2d] [2d]
SCORE  [6 digits, inverse video]
```

### FOUL (two numbers: 3 digits + 4 digits)
The FOUL line shows two separate threat indicators:
- **First number (3 digits)**: net monster count — increases when monsters
  spawn, decreases as they move. Shows how many unresolved monsters are active.
- **Second number (4 digits)**: total monster movement — increases by 1
  with every monster step on the map. This is a measure of cumulative
  monster activity and serves as the **unlock key for treasures**.

### Power (4-digit display)
Your primary resource currency and lifeline.

**Gaining Power:**
- Collecting mana gems: +50 or +10 depending on type
- Collecting treasures: variable amounts

**Spending Power:**
- Killing a monster (walking or wand mode): -10 Power
- Destroying a wall by walking over it: -50 Power
- Building shield barriers: variable cost by strength level

**If Power reaches zero, the game is over.**

### Castle Power (three 2-digit displays)
Each castle starts with 90 power. Decreases when monsters reach it. When any
castle reaches zero, it is destroyed. If all three castles fall, the game ends.

### Score (6-digit display, inverse video)
Your cumulative **activity score**, shown in inverse video on the bottom
status line. Grows with every action you take:
- Killing a monster: +50
- Collecting a type 1 mana gem: +01
- Collecting a type 2 mana gem: +20
- Collecting treasures: variable by type
- Also auto-increments by the difficulty level each scoring tick

A higher SCORE means more active play.

---

## Treasures

Certain areas of the map contain treasure items protected by barriers.
Treasures become collectible only after enough total monster activity has
occurred, measured by the second number on the **FOUL line** (total monster
movement). Higher-tier treasures require more monster activity to unlock,
but reward more Power (and add more to your Score).

| Treasure Tier | Unlock Condition              | Power Gain | Score Cost |
|---------------|-------------------------------|------------|-----------|
| Easiest       | After ~100 total monster moves | +03        | +30       |
| Medium-easy   | After ~200 total monster moves | +06        | +50       |
| Medium-hard   | After ~300 total monster moves | +09        | +70       |
| Hardest       | After ~400 total monster moves | +12        | +90       |

This creates a natural game progression: as the game runs longer and monsters
accumulate more total movement, higher-tier treasures gradually become
available.

---

## Map View (M Key)

Press M to see the entire map at reduced resolution. Useful for locating
monsters, finding mana gem concentrations, checking shield integrity, and
strategic planning.

Press any key to return to gameplay. The game does NOT pause during map view.

---

## Wand Mode Limits

The wand cursor has two hidden counters that limit its power:

**Wall breaking**: The cursor can damage walls by repeatedly passing over
them, but it takes many hits to break a single wall (which becomes a mana
gem). After one wall is broken this way, the ability is exhausted for the
rest of the game. Walking-mode wall breaking is instant but costs 50 Power.

**Kill penalty**: After every 256 monster kills with the wand cursor, the
game modifies tiles in the castle area of the map. This repeats every 256
cursor kills.

---

## Strategy Tips

1. **Walk through mana gem areas constantly** — gems are your main Power
   income and they accumulate over time as monsters roam.

2. **Don't kill every monster** — each kill costs 10 Power. Let distant
   monsters roam; mana carriers deposit gems as they walk, which is beneficial.
   The hidden kill counter also penalizes excessive killing.

3. **Build shield walls around castles** — the map is almost entirely open,
   so there are no natural defenses. Place shield barriers in rows or rings
   around vulnerable castles.

4. **Use tracking markers on approach paths** — deploy them where monsters
   are most likely to approach castles.

5. **Use wand mode actively** — the cursor can destroy monsters and walls.
   Use it to clear threats near castles and to reach the S/V activation area.

6. **Check the map (M) regularly** — see monster distribution, find gem
   concentrations, and check which shields are still intact.

7. **Higher difficulty = more gems** — monster drop rate increases, giving
   more Power income despite more aggressive behavior.

8. **Treasures unlock naturally** — the Castle Damage counter increases with
   ALL monster movement. Treasures gradually become available as the game
   progresses.

---

## Game Over

The game ends when:
- **Power reaches zero**
- **All three castles are destroyed**

Your score is compared to the high score. Press any key to try again.
