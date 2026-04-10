# River Raid — VBI Architecture

## VBI Handler ($B2A5)

The game installs a custom immediate VBI handler at $B2A5, replacing the
OS VVBLKI vector at $0222. The deferred VBI (VVBLKD at $0224) is left
as $0000 — unused.

## Handler Flow

```
$B2A5: CLD                    ; clear decimal mode (safety)
$B2A6: LDA $C6                ; check sound_timer
$B2A8: BEQ $B2BA              ; if zero → skip explosion sound
$B2AA: DEC $C6                ; decrement timer
$B2AC: LDA #$40               ; explosion sound: AUDF1 = $40
$B2AE: STA AUDF1 ($D200)
$B2B1: LDA #$AF               ; AUDC1 = $AF (noise, volume 15)
$B2B3: STA AUDC1 ($D201)
$B2B6: LDX #$00
$B2B8: BEQ $B2C5              ; → continue sound engine
$B2BA: LDA $24                ; game_state
$B2BC: CMP #$02               ; state >= 2 (dying/dead)?
$B2BE: BCC $B2CE              ; if playing → engine sound
$B2C0: LDX #$00               ; dying: silence all channels
$B2C2: STX AUDC1 ($D201)
$B2C5: STX AUDC2 ($D203)
$B2C8: STX AUDC3 ($D205)
$B2CB: JMP $B3D8              ; → rest of VBI (game timers)
```

After the sound section, the VBI continues at $B2CE-$B3D7 with:
- Engine sound management (channels 2-3 based on speed)
- Score display sound effects
- Game timer updates
- Color animation for fuel gauge

The VBI exits via the standard OS deferred VBI path or directly
through register restore and RTI.

## Sound Engine Detail

### Channel Allocation

| Channel | AUDF/AUDC | Purpose |
|---------|-----------|---------|
| 1 | $D200/$D201 | Explosion noise (timer-driven) |
| 2 | $D202/$D203 | Engine hum / score effect |
| 3 | $D204/$D205 | Engine harmonic / refuel sound |
| 4 | $D206/$D207 | Unused (silent) |

AUDCTL ($D208) = $00: all channels at 64kHz clock, no joining.

### Explosion Sound
When an entity is destroyed, $C6 is loaded with a duration value.
Each VBI frame decrements $C6 and plays AUDF1=$40, AUDC1=$AF (white
noise at maximum volume). When $C6 reaches zero, the explosion
sound stops.

### Engine Sound
During active gameplay ($24 < $02), the engine sound is generated
on channels 2-3. The frequency varies with the joystick's up/down
input, creating a higher pitch when accelerating and lower when
decelerating. Variables $7A/$7B control the engine pitch.

### Silence on Death
When the game state is "dying" ($24 >= $02), all three sound
channels are silenced (AUDC = $00).

## Cycle Budget

The VBI handler is lightweight — profiling shows it consumes only
~17,713 cycles per frame (0.6% of the frame budget). This is
because the heavy lifting (display rendering) is done by the DLI
kernel, not the VBI. The VBI just handles sound and a few timer
updates.

The Atari's NTSC frame is ~29,868 scanlines × ~114 cycles/line ≈
~3.4M cycles. The VBI has plenty of time — it runs during the
~70 blank lines at the bottom of the screen before the next frame
starts.

## VBI vs DLI Interaction

The VBI and DLI do not directly communicate through shared flags.
They use separate sets of variables:

- **VBI writes**: $C6 (sound timer), AUDF/AUDC registers, color
  animation state
- **DLI writes**: HPOSP/COLPM/SIZEP registers, collision flags
  ($1A-$1E), WSYNC

The main loop (running outside both interrupts) reads the collision
flags written by the DLI and the game state updated by the VBI.
