# Initialization and Startup

## INIT Routine ($4000, Segment 0)

The first segment loads to $4000-$5894 and executes as an INIT routine.
This segment is later overwritten by segment 5 (the main code), so the
INIT code is not present in memory at runtime.

The INIT routine:
1. Disables interrupts (SEI) and DMA
2. Disables OS ROM: `LDA PORTB; AND #$FE; STA PORTB`
3. Copies data from $409B+ to $C000+ (sine/cosine/perspective tables)
4. Copies data from $5098+ to $F800+ (custom character set + vectors)
5. Re-enables OS ROM: `LDA PORTB; ORA #$01; STA PORTB`
6. Re-enables VBI (NMIEN=$40), returns

After INIT, segments 1-9 load normally. Segment 5 ($3200-$8E94)
overwrites the INIT segment area.

### Data copied to RAM under ROM:

```
$C000-$C0FF: Sine table (256 entries, unsigned 8-bit)
$C100-$C1FF: Sine table (second quadrant / negated)
$C200-$C2FF: High bytes for 16-bit sine/cosine
$C300-$C3FF: Additional lookup table
$C400-$C6FF: Cosine / reciprocal tables
$CF00-$CF3F: Wall distance thresholds (BSP comparison values)
$CF40-$CF7F: Wall height lookup table
$F800-$FBFF: Custom character set (128 characters)
$FFFA-$FFFF: Hardware interrupt vectors
```

## Entry Point ($3660)

The RUN address $3660 performs full hardware initialization:

### Phase 1: Hardware reset ($3660-$36AE)
```asm
CLC / CLD / SEI          ; clear flags, disable interrupts
LDA #$00
STA IRQEN                 ; disable all POKEY IRQs
STA NMIEN                 ; disable NMI
STA DMACTL                ; disable DMA (blank screen)
STA COLBK                 ; black background
STA GRAFP0..GRAFP3        ; clear player graphics
STA GRAFM                 ; clear missiles
STA HPOSP0..HPOSM3        ; zero all positions
STA PRIOR                 ; normal mode
STA AUDCTL / AUDC1..AUDC4 ; silence audio
LDA #$03 / STA SKCTL      ; initialize POKEY serial
```

### Phase 2: Memory setup ($36AF-$36DA)
```asm
; Disable OS ROM again (PORTB AND #$FE)
LDA PORTB / ORA #$02 / STA PORTB   ; enable BASIC ROM (???)
LDA PORTB / AND #$FE / STA PORTB   ; disable OS ROM

; Set up NMI and IRQ vectors in RAM
JSR $3BBB     ; $FFFA/$FFFB = $91C1 (NMI → immediate VBI)
JSR $3BC6     ; $FFFE/$FFFF = $9100 (IRQ → first handler)

; Clear zero page
LDA #$00 / LDY #$00
clear_zp: STA $00,Y / INY / BNE clear_zp

; Clear framebuffer ($F800-$FFFF then $D800-$F7FF = total 8KB+)
; Uses self-modifying code: patches high byte at $36D2
LDX #$20      ; 32 pages = 8KB
LDA #$00
clear_fb: STA $F800,Y / INY / BNE clear_fb
          INC $36D2    ; increment high byte
          DEX / BNE clear_fb
```

### Phase 3: PAL/NTSC detection ($36E4-$3705)
```asm
LDA SKSTAT       ; $D20F
AND #$08         ; bit 3
BEQ pal_mode     ; if 0 → PAL settings (narrower viewport)

; NTSC or detection says wider:
LDA #$00 / STA $71    ; first column = 0
LDA #$20 / STA $72    ; num columns = 32
LDA #$0F / STA $73    ; adjustment = 15
JMP continue

pal_mode:
LDA #$05 / STA $71    ; first column = 5
LDA #$1B / STA $72    ; num columns = 27
LDA #$0A / STA $73    ; adjustment = 10
```

Note: The PAL/NTSC detection using SKSTAT bit 3 is unusual. Standard
detection reads GTIA $D014 (PAL register). In practice with Atari800
emulator in PAL mode, SKSTAT bit 3 is set, selecting the wider viewport.

### Phase 4: Display and timer setup ($3706-$37CB)
- Set up display list pointer ($9800)
- Configure player-missile positions, sizes, colors
- Set PMBASE=$90, CHBASE=$F8
- Configure POKEY Timer 4 (AUDF4=$01, AUDCTL=$01)
- **Critical**: Synchronize timer to scanline (WSYNC/VCOUNT/SKCTL/STIMER)
- Enable interrupts (CLI), enable DMA (DMACTL=$3D), enable VBI (NMIEN=$80)
- Enable PMG (GRACTL=$03)

### Phase 5: Enter main loop ($37DA)
```asm
  LDA #$80
  JSR render_frame
  LDA #$00
  JSR render_frame
  JMP $37DA
```

## Sound System

RMT (Raster Music Tracker) player at $3303-$36xx. Music data in
segments 1-4 ($2800-$31BF). The RMT header "RMT4" indicates 4-channel
format.

The music player runs from the deferred VBI handler ($91D4) via
`JSR $3303`. Sound register updates happen in `sub_3632` which writes
AUDF1-3 and AUDC1-3 from the music state buffers at $3048-$304E.

Note: AUDCTL bit 0 is set ($01 = 15KHz clock) for Timer 4 timing,
but this also affects channels 1-2 audio frequency range. The music
must be composed with this constraint in mind.
