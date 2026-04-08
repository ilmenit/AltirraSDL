# IRQ Handler & Input System

## IRQ Handler ($4940)

The custom IRQ handler replaces the OS default to provide direct keyboard
processing. It reads KBCODE ($D209) on every IRQ and manages IRQEN state.

- Entry: standard IRQ save (PHA/TXA/TYA/PHA sequence implied by 6502 IRQ)
- Reads KBCODE ($D209) for the current key scancode
- Manages IRQEN ($D20E) to control which interrupts are active
- Handles special key combinations (START/SELECT/OPTION via CONSOL)
- Exit: restores registers, RTI

### Vector Setup

| Address | Name                | Role |
|---------|---------------------|------|
| $4956   | setup_irq_vectors   | Install custom IRQ and VBI vectors |
| $6988   | setup_game_vectors  | Install gameplay-mode VBI/DLI vectors |

`setup_irq_vectors` writes to VIMIRQ ($0216) for the IRQ vector and
calls SETVBV ($E45C) for VBI installation.

`setup_game_vectors` installs the gameplay VBI ($8059) and DLI dispatcher
($4CF1) — called when transitioning from title screen to gameplay.

## Joystick Input ($8530)

`read_joystick` reads PIA PORTA ($D300) and decodes direction + fire button.
The joystick port value is a 4-bit direction mask:
- Bit 0: up
- Bit 1: down
- Bit 2: left
- Bit 3: right
- TRIG0 ($D010): fire button (active low)

The routine also reads POKEY RANDOM for some randomization (possibly
related to control response variability or AI behavior).

## DLI Chain

The DLI system uses a **dispatch table** mechanism:

| Address | Name           | Role |
|---------|----------------|------|
| $4CF1   | dli_dispatch   | Read stage counter $C8, index jump table at $4DD6 |
| $4DD6   | dli_jump_table | 8-entry address table (lo/hi pairs) |
| $61EF   | dli_table_alt  | Alternate DLI dispatch table (2×8 entries) |

### DLI Stages

| Stage | Address | Name          | Function |
|-------|---------|---------------|----------|
| 0     | $4D0D   | dli_sky       | Sky colors — COLPF registers for sky gradient |
| 1     | $4D20   | dli_horizon   | Sky-to-terrain transition — PRIOR=$11 |
| 2     | $4D41   | dli_ground    | Terrain ground colors during draw region |
| 3     | $4D79   | dli_hud       | HUD transition — switch to cockpit colors |
| —     | $610B   | dli_colors_a  | Load color regs from shadow, set PRIOR=$94 |
| —     | $6184   | dli_colors_b  | Set terrain/ground color registers |
| —     | $61CF   | dli_colors_c  | Set PRIOR, load colors for cockpit section |

### Additional DLI Color Handlers

These are reached via the alternate dispatch tables:

| Address | Name               | Registers Written |
|---------|--------------------|-------------------|
| $6143   | set_colors_6143    | COLPF0-2, COLBK |
| $6177   | update_pmg_6177    | HPOSM0, HPOSM3 |
| $61B1   | set_colors_61B1    | COLPM0-1, COLPF3, COLBK |

### Indirect Dispatch

Two dispatch routines use **JMP ($00E1)** indirect through a ZP vector:

| Address | Name           | Table Source |
|---------|----------------|--------------|
| $60E1   | dispatch_60E1  | $61F1/$61F2 (main DLI table) |
| $60F6   | dispatch_60F6  | $6205/$6206 (alternate DLI table) |

These load the DLI stage counter ($C8), double it as an index, read the
target address from the table into ZP $E1/$E2, then JMP ($00E1).

## Color Architecture

The game maintains a 16-byte color palette at $0741, copied from ZP
shadow registers at $D0-$DF. The DLI chain swaps these into GTIA color
registers at the appropriate scanlines:

- $0741-$0750: 16-byte game palette (copied per-frame from ZP $D0-$DF)
- VBI writes palette to GTIA hardware registers
- Each DLI stage loads different subsets for sky/terrain/cockpit/HUD
- PRIOR register ($D01B) switches between modes: $11 (multicolor), $94 (5th player)
