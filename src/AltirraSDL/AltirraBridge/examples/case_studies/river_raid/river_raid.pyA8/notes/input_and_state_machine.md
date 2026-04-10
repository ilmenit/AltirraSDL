# River Raid — Input Handling & State Machine

## Input System

### Joystick Reading ($A312-$A33F)

The joystick is read from PIA Port A ($D300):

```
$A31A: LDA PORTA ($D300)      ; read both joystick ports
$A31D: LDX $77                ; player_number (0=P1, 1=P2)
$A31F: BEQ $A325              ; P1: use low nibble directly
$A321: LSR × 4                ; P2: shift high nibble to low
$A325: AND #$0F               ; mask to 4-bit direction
$A327: STA $19                ; store as joystick_dir
```

The 4-bit direction encoding (active low):

| Nibble | Direction | Meaning |
|--------|-----------|---------|
| $0F | None | Centered |
| $0E | Up | Accelerate |
| $0D | Down | Decelerate |
| $0B | Left | Steer left |
| $07 | Right | Steer right |
| $06 | Up+Right | Accelerate + steer right |
| $0A | Up+Left | Accelerate + steer left |

### Fire Button ($A329-$A33F)

```
$A329: CMP #$0F              ; joystick centered?
$A32B: BNE $A332             ; no → skip trigger check (direction = shoot)
$A32D: LDA TRIG0,X ($D010)   ; read trigger (0=pressed, 1=released)
$A330: BNE $A33F             ; not pressed → skip
$A332: LDA $76               ; fuel_level
$A334: SEC
$A335: SBC #$2F              ; check if fuel allows firing
```

The fire button is read from TRIG0 ($D010) for player 1 or TRIG1
($D011) for player 2. The button is active-low (0 = pressed).

### Console Switches ($A1AE-$A1BD)

```
$A1AE: LDA #$08              ; read CONSOL register
$A1B0: STA CONSOL ($D01F)    ; latch current switch state
$A1B3: LDA CONSOL ($D01F)    ; read latched state
$A1B6: LSR                   ; bit 0 → carry = START
$A1B7: BCC $A1BF             ; START pressed → handle
$A1B9: LSR                   ; bit 1 → carry = SELECT
$A1BA: BCC $A1BF             ; SELECT pressed → handle
$A1BC: LSR                   ; bit 2 → carry = OPTION
$A1BD: BCS $A1C5             ; OPTION pressed → handle
```

Also checked: SKSTAT ($D20F) bit 2 for keyboard (shift key).

## State Machine

### Game States ($24)

| Value | State | Description |
|-------|-------|-------------|
| $00 | ATTRACT | Title screen / demo mode |
| $01 | PLAYING | Active gameplay |
| $02 | DYING | Player death animation |
| $03 | STARTING | New game initialization |

### State Transitions

```
                    START
    ATTRACT ($00) ────────→ STARTING ($03)
        ↑                        │
        │                        ↓
    GAME_OVER ←── DYING ($02) ← PLAYING ($01)
        │              ↑              │
        │              └──────────────┘
        │                  collision/
        │                  fuel empty
        └─── OPTION pressed
```

### Attract Mode ($A197-$A1D9)

In attract mode ($24 = $00):
1. Wait for VCOUNT sync at $A197
2. Check all input: joystick, triggers, console switches
3. If START pressed: set $24 = $03, enter STARTING state
4. If SELECT pressed: toggle Game 1/Game 2
5. If OPTION pressed: reset to title
6. Otherwise: run the main loop ($AE99) for attract display

### Starting State ($A15D)

When $24 = $03:
1. Clear zero-page variables $00-$7F
2. Initialize lives ($2A), score ($63-$65), fuel ($76)
3. Set up display regions
4. Set $24 = $01, transition to PLAYING

### Playing State ($A271-$AE96)

The main gameplay state. Each frame:
1. Update entities ($A271)
2. Process collisions ($A4E0)
3. Update score ($A547)
4. Handle input ($A312)
5. Update terrain ($A5B7)
6. Manage fuel ($AE90)
7. Check for death conditions

### Dying State ($A7FB-$A96C)

When the player dies:
1. Set $24 = $02
2. Start explosion animation ($C6 timer, $7C timer)
3. Play explosion sound
4. Each frame: animate explosion at $A8C4
5. When animation completes: decrement lives ($2A)
6. If lives > 0: respawn at $A96A, set $24 = $01
7. If lives = 0: game over at $A96C, set $24 = $00

### Two-Player Mode

In Game 2 (2-player alternating), the game tracks:
- $77: current player number (0 or 1)
- Player-specific state is saved/restored at $AA77 during player switch
- The switch happens after each death: $AA77 swaps the active player's
  state (score, fuel, position) with the stored backup
