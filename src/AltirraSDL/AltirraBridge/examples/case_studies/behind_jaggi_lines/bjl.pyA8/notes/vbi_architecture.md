# VBI Architecture

## Title Screen VBI ($840A)
Simple handler that:
1. Writes SDMCTL → DMACTL (enable display)
2. Increments frame counter ($14) and slow timer ($0622)
3. Handles attract mode (color scramble after timeout)
4. Writes COLOR0-4 shadows to COLPF0-4 hardware registers
5. Calls update_pmg ($83F8) and vbi_deferred ($82AA)
6. Does NOT chain to OS VBI (JMP $82AA instead of JMP SYSVBV)

## Gameplay VBI ($8059)
More complex handler:
1. Resets DLI counter ($C8 = 0) for per-frame DLI dispatch
2. Clears player sizes (SIZEP0, SIZEP1)
3. Restores CHBASE to $E0 (ROM charset for HUD text)
4. Writes color palette from ZP $D0-$DA to GTIA hardware:
   - $D5 → COLBK, COLPM0, COLPM1 (sky color)
   - $2850 → COLPM2
   - $D9 → COLPF0, $D8 → COLPF1
   - $DA → COLPM3
5. Positions player 2+3 and missiles for cockpit elements
6. Sets GPRIOR shadow → PRIOR hardware
7. Increments frame counter; on overflow: attract mode cycle
8. Selects display list based on game state ($A7):
   - $A7=1: gameplay DL at $316B
   - $A7≠1: alternate DL at $3210
