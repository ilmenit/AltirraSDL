# Key Variables and Zero Page Usage

## Zero Page Game Variables

```
$00    A register save (VBI/NMI)
$01    X register save (VBI/NMI)
$02    Y register save (VBI/NMI)
$03    A register save (IRQ)
$04    IRQ scanline counter (16 → 0, reset each VBI)
$05    Temp (PMG DMA cycles / working)
$06-$07  Pointer (texture address / working)
$0A    Current column index (viewport iteration)
$0B    Texture lookup value
$0C    Wall distance (current column)
$0D    Temp (texture index)
$0E    Frame skip flag (if non-zero, skips column rendering)
$0F    FPS timer countdown (counts 100→0, resets with FPS value)
$10-$11  Total frame counter (16-bit, incremented every frame)
$12    Buffer flag ($00 = buffer 0, $80 = buffer 1)
$13    Frame sub-counter (incremented by VBI)
$14    Frame counter (incremented by VBI, used for FPS calc)
$1A    Fade timer (counts down from $5A after FPS display)
$1B    Fade level (incremented $10/step, written to COLBK)
$1C-$1D  Pointer: column state array 0 ($8F60/$8FA0)
$1E-$1F  Pointer: column state array 1 ($8FA0/$8FE0)
$20-$21  Pointer: column state array 2 ($8F80/$8FC0)
$22-$23  Pointer: column state array 3 ($8FC0/$9000)
$24-$25  Pointer: render dispatch table ($8B03/$8B2C/$8ADA)
$29    Viewport width parameter (same as $73)
$2A    Temp / ray direction quadrant
$2B    Ray quadrant info
$2C    Direction flag (0 or $FF)
$2D    Direction sub-flag
$2E    Ray angle index
$2F    Map sector index
$30    Wall type at current cell
$32    Ray X fractional position
$35    Step X fractional increment
$37    Ray X integer position
$38    Ray Y position
$3A    Step X integer increment
$3B    Step Y increment
$3D    Map cell X
$3E    Map cell Y
$3F    Hit face index
$40    Working counter
$41    Texture offset base
$44    Perspective value A
$47    Perspective value B
$49    Score / display value
$4C    Room indicator
$66    Texture base ID
$67    Texture column (0-7)
$68    Wall height result (from BSP)
$69    Game speed / map index (1-5)
$6D    Perspective delta A
$6E    Perspective delta B
$6F    Column range start
$70    Column range end
$71    First visible column (0 for PAL, 5 for NTSC)
$72    Last visible column ($20=32 for PAL, $1B=27 for NTSC)
$73    Viewport adjustment
$74    Status display mode
$75    FPS value (hex)
$76    Turning state ($00=none, $FF=turning, <0=completing)
$77    Turn step index (0-14)
$79    Map level number (1-5)
$7C-$81  Input flags (joystick + fire decoded):
  $7C  = Forward
  $7D  = Backward (or strafe?)
  $7E  = Turn left
  $7F  = Turn right
  $80  = Strafe left
  $81  = Strafe right
$83    Fire button (minimap toggle)
$84    Option key (music restart)
$85    Select key (level change)
$88    Minimap display mode (0/1/2, cycles on fire)
$89    Music trigger ($01=start, $02=change, $80=stop)
$8A    Music track select
$8B    Music state (0=stopped, 1=playing, 2=transitioning)
$8C    Render mode flag (bit 7 = alternate rendering)
$9F    Turn velocity
$A0    Turn accumulator
```

## Column State Arrays ($8F00-$90FF)

Per-column data, indexed by column number (0-31):

```
$8F00,Y  Wall distance (0 = far, $20 = closest)
$8F20,Y  Texture ID
$8F40,Y  Texture column offset
$8F60,Y  Column pixel height (buffer 0)
$8FA0,Y  Column pixel height (buffer 1)
$8F80,Y  Sky height (buffer 0)
$8FC0,Y  Sky height (buffer 1)
```

The buffer 0/1 arrays alternate via the pointer setup at $8ADA/$8B03/$8B2C.

## Map/Level Data

Wall and floor data at $9240-$99FF. The BSP tree nodes are at $9240-$92BF
with child pointer tables. Level geometry appears to be in the $9A00-$BFFF
region (segment 9).

## Hardware Write-only State

These are maintained by the game but not readable via PEEK:

```
PRIOR:   Toggled by IRQ chain ($E1/$61 in viewport, $21 in text)
HPOSP0:  $78 (crosshair center)
HPOSP1:  $78 (crosshair center)
HPOSP2:  $00/$9D/$00 (minimap, mode-dependent)
HPOSM3:  $00/$00/$C1 (minimap indicator)
GRACTL:  $03 (PMG enabled)
DMACTL:  $3D (normal DMA + PMG)
```
