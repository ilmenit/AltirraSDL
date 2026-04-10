# River Raid — Player's Guide

## Overview

River Raid is a vertically scrolling shoot-'em-up originally designed by
Carol Shaw and published by Activision in 1982. In this Atari 8-bit
version, you pilot a fighter jet flying up a river, destroying enemy
vehicles and refueling at fuel depots while avoiding the river banks.
The game is an endless challenge — there is no final level or win
condition. Your goal is to score as many points as possible before
running out of fuel or crashing.

## Controls

River Raid uses one joystick (plugged into port 1) and the console
switches on the Atari.

### Joystick

| Input | Action |
|-------|--------|
| Up | Accelerate — the plane moves faster up the screen and the terrain scrolls faster |
| Down | Decelerate — the plane slows down, terrain scrolls slower |
| Left | Steer left — the plane banks left across the river |
| Right | Steer right — the plane banks right across the river |
| Fire button | Fire a bullet straight ahead (one bullet at a time on screen) |

Your plane always moves forward (the river always scrolls), but pushing
up makes it scroll faster and pushing down slows it to a crawl.

### Console switches

| Switch | Action |
|--------|--------|
| START | Begin a new game (from the title screen or after game over) |
| SELECT | Toggle between Game 1 (one player) and Game 2 (two players, taking turns) |
| OPTION | Reset to the title screen |

## The River

The playing field is a winding river viewed from above. The river is
bordered by brown terrain on both sides. The river width varies — sometimes
it's wide and easy to navigate, sometimes it narrows dangerously. The
terrain scrolls continuously from top to bottom as your plane flies
forward.

If your plane touches either river bank, it is destroyed and you lose
a life.

## Enemies

Several types of enemy vehicles patrol the river. Each awards a different
number of points when destroyed:

### Ships (boats)
Small dark vessels that sit in the river. They don't move aggressively
but they block your path. Worth moderate points when destroyed.

### Helicopters
Airborne enemies that move left and right across the river. They're
harder to hit because they change direction. Worth more points than
ships.

### Jets (enemy planes)
Fast-moving enemy aircraft that fly down the river toward you. They're
the most dangerous and highest-scoring regular enemies.

### Balloons
Slow-moving, easy targets that float above the river. Worth fewer
points but safe to destroy.

### Bridges
Periodically you encounter a bridge spanning the river. Bridges must
be destroyed to pass through — your bullet can break through them.
Each bridge crossed is tracked in the "BRIDGE" counter on the status
bar. Bridges mark the boundary between river sections; after crossing
one, the difficulty may increase.

## Fuel

Your plane has a limited fuel supply, shown by the fuel gauge in the
center of the status bar at the bottom of the screen. The gauge
consists of colored bars:

- **Green**: Fuel is plentiful — you're in good shape
- **Yellow/Orange**: Fuel is getting low — start looking for a depot
- **Red**: Fuel is critical — you must refuel immediately

Fuel drains continuously during flight. Flying faster (pushing up)
consumes fuel faster. Flying slower (pushing down or neutral) conserves
fuel.

### Fuel Depots

Fuel depots appear periodically along the river. They look like
rectangular platforms sitting in the middle of the river, marked with
a distinct color. To refuel, fly your plane directly over the depot
without firing at it. As long as your plane overlaps the depot, your
fuel gauge refills.

**Warning:** If you shoot a fuel depot, it is destroyed and you cannot
refuel from it. Be careful with your trigger finger when approaching
a depot!

## Scoring

Points are awarded for destroying enemies. The score is displayed at
the center of the status bar. The high score for the current session
is shown to the right ("HI").

The exact point values depend on the enemy type, with jets and
helicopters worth more than ships and balloons. Bridges also award
points when destroyed.

An extra life is awarded at certain score thresholds.

## Lives

You start with a set number of lives (shown as small plane icons in
the status bar). You lose a life when:

1. **Crashing into a river bank** — steering too far left or right
2. **Colliding with an enemy** — running into a ship, helicopter, or
   other obstacle
3. **Running out of fuel** — the fuel gauge reaches empty

After losing a life, the game pauses briefly, then you respawn at the
current position to continue flying.

When all lives are lost, the game is over. Press START to play again.

## Two-player mode

In Game 2 (selected with the SELECT switch), two players take turns.
Player 1 plays until losing a life, then Player 2 takes over. The
"P1" or "P2" indicator on the left of the status bar shows whose
turn it is. Each player has their own score and lives.

## Strategy Tips

1. **Manage your speed.** Flying fast makes the game harder to control
   but lets you cover more ground. Slow down in narrow sections.

2. **Watch your fuel gauge.** The most common way to lose is running
   out of fuel. Always keep an eye on the gauge and plan your route
   to pass over fuel depots.

3. **Don't shoot fuel depots.** It's tempting to fire at everything,
   but destroying a fuel depot when you need it is a fatal mistake.
   Release the fire button when approaching one.

4. **Memorize the river pattern.** The river follows a procedurally
   generated but repeating pattern. As you play more, you'll learn
   which sections are safe and which are dangerous.

5. **Use the full width.** In wide sections, dodge enemies by moving
   to the opposite bank rather than trying to shoot everything.

6. **Bridges are checkpoints.** Each bridge you cross means you're
   making progress. The bridge counter on the status bar tracks
   your advancement.

## Status Bar

The bottom of the screen shows a status bar with the following
information (left to right):

| Area | Display |
|------|---------|
| P1/P2 | Current player indicator |
| GAME | Game number (1 or 2) |
| Fuel gauge | Colored bar showing remaining fuel |
| Score | Current player's score (center) |
| HI | Session high score |
| BRIDGE | Number of bridges crossed |
| Lives | Remaining lives (shown as plane icons) |

## Title Screen

When the game first loads, you see the title screen with the Activision
logo and the player's plane on the river. The game runs a brief demo
(attract mode) before returning to the title. Press START to begin.

## About This Version

This is the Atari 8-bit (400/800/XL/XE) version of River Raid. The
game requires a joystick in port 1. It was originally designed for the
Atari 2600 by Carol Shaw and later ported to the Atari 8-bit computers
with enhanced graphics and gameplay. The Atari 8-bit version uses
ANTIC mode 14 graphics (160-pixel wide, 4-color bitmap) with
player/missile sprites for the plane and enemies, creating a colorful
and detailed playing field.
