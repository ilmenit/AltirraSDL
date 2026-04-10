; =============================================================
; Segment 5: $3200-$8E94 — 
; =============================================================
; XEX load range: $3200-$8E94 (23701 bytes)

    org $3200

    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    ora (NGFLAG,x)
    ora (NGFLAG,x)
    ora (NGFLAG,x)
    ora (NGFLAG,x)
    brk
    brk
    brk
    brk
    ora (NGFLAG,x)
    ora (NGFLAG,x)
    ora (NGFLAG,x)
    ora (NGFLAG,x)
    .byte $02
    .byte $02
    .byte $02
    .byte $02
    brk
    brk
    brk
    ora (NGFLAG,x)
    ora (NGFLAG,x)
    ora (CASINI,x)
    .byte $02
    .byte $02
    .byte $02
    .byte $02
    .byte $03
    .byte $03
    .byte $03
    brk
    brk
    ora (NGFLAG,x)
    ora (NGFLAG,x)
    .byte $02
    .byte $02
    .byte $02
    .byte $02
    .byte $03
    .byte $03
    .byte $03
    .byte $03
    .byte $04
    .byte $04
    brk
    brk
    ora (NGFLAG,x)
    ora (CASINI,x)
    .byte $02
    .byte $02
    .byte $03
    .byte $03
    .byte $03
    .byte $04
    .byte $04
    .byte $04
    ora PM_DMA_CYCLES
    brk
    brk
    ora (NGFLAG,x)
    .byte $02
    .byte $02
    .byte $02
    .byte $03
    .byte $03
    .byte $04
    .byte $04
    .byte $04
    ora PM_DMA_CYCLES
    asl TRAMSZ
    brk
    brk
    ora (NGFLAG,x)
    .byte $02
    .byte $02
    .byte $03
    .byte $03
    .byte $04
    .byte $04
    ora PM_DMA_CYCLES
    asl TRAMSZ
    .byte $07
    .byte $07
    brk
    ora (NGFLAG,x)
    .byte $02
    .byte $02
    .byte $03
    .byte $03
    .byte $04
    .byte $04
    ora PM_DMA_CYCLES
    asl TRAMSZ
    .byte $07
    .byte $07
    php
    brk
    ora (NGFLAG,x)
    .byte $02
    .byte $02
    .byte $03
    .byte $04
    .byte $04
    ora PM_DMA_CYCLES
    asl CMCMD
    .byte $07
    php
    php
    ora #$00
    ora (NGFLAG,x)
    .byte $02
    .byte $03
    .byte $03
    .byte $04
    ora PM_DMA_CYCLES
    asl CMCMD
    .byte $07
    php
    ora #$09
    asl
    brk
    ora (NGFLAG,x)
    .byte $02
    .byte $03
    .byte $04
    .byte $04
    ora TRAMSZ
    .byte $07
    .byte $07
    php
    ora #$0A
    asl
    .byte $0B
    brk
    ora (CASINI,x)
    .byte $02
    .byte $03
    .byte $04
    ora TRAMSZ
    asl CMCMD
    php
    ora #$0A
    asl
    .byte $0B
    .byte $0C
    brk
    ora (CASINI,x)
    .byte $03
    .byte $03
    .byte $04
    ora TRAMSZ
    .byte $07
    php
    ora #$0A
    asl
    .byte $0B
    .byte $0C
    ora $0100
    .byte $02
    .byte $03
    .byte $04
    ora TRAMSZ
    .byte $07
    .byte $07
    php
    ora #$0A
    .byte $0B
    .byte $0C
    ora.w APPMHI
    ora (CASINI,x)
    .byte $03
    .byte $04
    ora TRAMSZ
    .byte $07
    php
    ora #$0A
    .byte $0B
    .byte $0C
    ora $0F0E
sub_3300:
    jmp loc_330F
rmt_player:
    ; RMT PLAYER — Raster Music Tracker main entry, dispatches play/init/stop
    jmp loc_34DB
    jmp loc_34F5
rmt_silence:
    ; RMT SILENCE — mute all channels, set AUDCTL=$01
    jmp rmt_stop
    jmp update_sound
loc_330F:
    stx ESIGN
    sty FCHFLG
    pha
    ldy #$75
    lda #$00
loc_3318:
    sta $2FDF,y
    dey
    bne loc_3318
    ldy #$04
    lda (ESIGN),y
    sta $34E9
    iny
    lda (ESIGN),y
    sta $33BC
    ldy #$08
loc_332D:
    lda (ESIGN),y
    sta $00DF,y
    iny
    cpy #$10
    bne loc_332D
    pla
    pha
    asl
    asl
    clc
    adc EEXP
    sta EEXP
    pla
    php
    and #$C0
    asl
    rol
    rol
    plp
    adc NSIGN
    sta NSIGN
    jsr sub_336B
rmt_stop:
    ; RMT STOP — zero all AUDF/AUDC registers, return
    lda #$01
    sta AUDCTL
    lda #$00
    sta AUDF1
    sta AUDC1
    sta AUDF2
    sta AUDC2
    sta AUDF3
    sta AUDC3
    lda #$01
    rts
sub_336B:
    ; RMT INIT CHANNELS — parse instrument headers, fill channel state at $2FE0-$3000
    ldx #$00
    stx $34E7
loc_3370:
    txa
    tay
    lda (EEXP),y
    cmp #$FE
    bcs loc_33A5
    tay
    lda ($E9),y
    sta $2FE0,x
    lda ($EB),y
    sta $2FE4,x
    lda #$00
    sta $2FE8,x
    lda #$01
loc_338A:
    sta $2FEC,x
    lda #$80
    sta $3000,x
    inx
    cpx #$04
    bne loc_3370
    lda EEXP
    clc
    adc #$04
    sta EEXP
    bcc loc_33BB
    inc NSIGN
    jmp loc_33BB
loc_33A5:
    beq loc_33AB
    lda #$00
    beq loc_338A
loc_33AB:
    ldy #$02
    lda (EEXP),y
    tax
    iny
    lda (EEXP),y
    sta NSIGN
    stx EEXP
    ldx #$00
    beq loc_3370
loc_33BB:
    ; RMT FRAME TICK — advance all 4 channels one step, update notes/envelopes
    lda #$FF
    sta $340F
    ldx #$FF
loc_33C2:
    inx
    dec $2FEC,x
    bne loc_340A
    lda $2FE0,x
    sta ESIGN
    lda $2FE4,x
    sta FCHFLG
loc_33D2:
    ldy $2FE8,x
    inc $2FE8,x
    lda (ESIGN),y
    sta ZTEMP1
    and #$3F
    cmp #$3D
    beq loc_33F0
    bcs loc_3419
    sta $2FF0,x
    iny
    lda (ESIGN),y
    lsr
    and #$7E
    sta $3000,x
loc_33F0:
    lda #$01
    sta $2FEC,x
    ldy $2FE8,x
    inc $2FE8,x
    lda (ESIGN),y
    lsr
    ror ZTEMP1
    lsr
    ror ZTEMP1
    lda ZTEMP1
    and #$F0
    sta $2FF4,x
loc_340A:
    cpx #$03
    bne loc_33C2
    lda #$FF
    sta $33BC
    sta $3054
    jmp loc_345E
loc_3419:
    cmp #$3F
    beq loc_3438
    lda ZTEMP1
    and #$C0
    beq loc_342C
    asl
    rol
    rol
    sta $2FEC,x
    jmp loc_340A
loc_342C:
    iny
    lda (ESIGN),y
    sta $2FEC,x
    inc $2FE8,x
    jmp loc_340A
loc_3438:
    lda ZTEMP1
    bmi loc_3448
    iny
    lda (ESIGN),y
    sta $340F
    inc $2FE8,x
    jmp loc_33D2
loc_3448:
    cmp #$FF
    beq loc_3455
    iny
    lda (ESIGN),y
    sta $2FE8,x
    jmp loc_33D2
loc_3455:
    jmp sub_336B
loc_3458:
    jmp loc_34F5
loc_345B:
    dex
    bmi loc_3458
loc_345E:
    ldy $3000,x
    bmi loc_345B
    lda ($E7),y
    sta $3004,x
    sta INBUFF
    iny
    lda ($E7),y
    sta $3008,x
    sta $F4
    ldy #$02
    lda (INBUFF),y
    sta $3010,x
    iny
    lda (INBUFF),y
    sta $3014,x
    iny
    lda (INBUFF),y
    sta $3034,x
    and #$3F
    sta $3044,x
    iny
    lda (INBUFF),y
    sta $3050,x
    iny
    lda (INBUFF),y
    sta $301C,x
    iny
    lda (INBUFF),y
    sta $3024,x
    iny
    lda (INBUFF),y
    sta $3028,x
    iny
    lda (INBUFF),y
    tay
    lda $3092,y
    sta $302C,x
    ldy #$0A
    lda (INBUFF),y
    sta $3030,x
    lda #$80
    sta $3020,x
    sta $3000,x
    asl
    sta $3018,x
    sta $2FFC,x
    tay
    lda (INBUFF),y
    sta $3040,x
    adc #$00
    sta $300C,x
    lda #$0C
    sta $303C,x
    tay
    lda (INBUFF),y
    sta $3038,x
    jmp loc_345B
loc_34DB:
    jsr update_sound
    dec $3054
    bne loc_34F5
    inc $34E7
    lda #$FF
    cmp #$FF
    beq loc_34EF
    jmp loc_33BB
loc_34EF:
    jmp sub_336B
loc_34F2:
    jmp loc_3616
loc_34F5:
    lda #$31
    sta CIX
    ldx #$03
loc_34FB:
    lda $3008,x
    beq loc_34F2
    sta FCHFLG
    lda $3004,x
    sta ESIGN
    ldy $300C,x
    lda (ESIGN),y
    sta ZTEMP1
    iny
    lda (ESIGN),y
    sta $F6
    iny
    lda (ESIGN),y
    sta ZTEMP4
    iny
    tya
    cmp $3010,x
    bcc loc_3529
    beq loc_3529
    lda #$80
    sta $3018,x
    lda $3014,x
loc_3529:
    sta $300C,x
    lda ZTEMP1
    and #$0F
    ora $2FF4,x
    tay
    lda $3200,y
    sta VBI_SCANLINE
    lda $F6
    and #$0E
    tay
    lda $3082,y
    sta DIGRT
    lda VBI_SCANLINE
    ora $3083,y
    sta $304C,x
    lda $3028,x
    beq loc_3571
    cmp #$01
    bne loc_356E
    lda $2FFC,x
    clc
    adc $3030,x
    clc
    ldy $302C,x
    adc $3096,y
    sta $2FFC,x
    lda $30AB,y
    sta $302C,x
    jmp loc_3571
loc_356E:
    dec $3028,x
loc_3571:
    ldy $3040,x
    cpy #$0D
    bcc loc_35AA
    lda $3044,x
    bpl loc_35A4
    tya
    cmp $303C,x
    bne loc_358A
    lda #$0C
    sta $303C,x
    bne loc_358D
loc_358A:
    inc $303C,x
loc_358D:
    lda $3004,x
    sta INBUFF
    lda $3008,x
    sta $F4
    ldy $303C,x
    lda (INBUFF),y
    sta $3038,x
    lda $3034,x
    and #$3F
loc_35A4:
    sec
    sbc #$01
    sta $3044,x
loc_35AA:
    lda $3018,x
    bpl loc_35CE
    lda $2FF4,x
    beq loc_35CE
    cmp $3024,x
    beq loc_35CE
    bcc loc_35CE
    tay
    lda $3020,x
    clc
    adc $301C,x
    sta $3020,x
    bcc loc_35CE
    tya
    sbc #$10
    sta $2FF4,x
loc_35CE:
    lda $F6
    and #$70
    beq loc_35D9
    lda ZTEMP4
    jmp loc_3613
loc_35D9:
    lda $2FF0,x
    clc
    adc ZTEMP4
    ldy $3034,x
    bmi loc_35FD
    clc
    adc $3038,x
    cmp #$3D
    bcc loc_35F3
    lda #$00
    sta $304C,x
    lda #$3F
loc_35F3:
    tay
    lda (DIGRT),y
    clc
    adc $2FFC,x
    jmp loc_3613
loc_35FD:
    cmp #$3D
    bcc loc_3608
    lda #$00
    sta $304C,x
    lda #$3F
loc_3608:
    tay
    lda $2FFC,x
    clc
    adc $3038,x
    clc
    adc (DIGRT),y
loc_3613:
    sta $3048,x
loc_3616:
    dex
    bmi loc_361C
    jmp loc_34FB
loc_361C:
    lda $3050
    ora $3051
    ora $3052
    ora $3053
    tax
    stx $3633
    stx $3633
    lda #$01
    rts
.proc update_sound
    ; UPDATE SOUND — write AUDF1-3 and AUDC1-3 from music state buffers $3048-$304E
    ldy #$FF
    lda $3048
    ldx $304C
    sta AUDF1
    stx AUDC1
    lda $3049
    ldx $304D
    sta AUDF2
    stx AUDC2
    lda $304A
    ldx $304E
    sta AUDF3
    stx AUDC3
    tya
    ora #$01
    tay
    sty AUDCTL
    rts
.endp

entry_point:
    ; ENTRY POINT — SEI, disable all interrupts/DMA, zero hardware regs, silence audio
    clc
    cld
    sei
    lda #$00
    sta IRQEN
    sta NMIEN
    sta DMACTL
    sta COLBK
    sta GRAFP0
    sta GRAFP1
    sta GRAFP2
    sta GRAFP3
    sta GRAFM
    sta HPOSP0
    sta HPOSP1
    sta HPOSP2
    sta HPOSP3
    sta HPOSM0
    sta HPOSM1
    sta HPOSM2
    sta HPOSM3
    sta PRIOR
    sta AUDCTL
    sta AUDC1
    sta AUDC2
    sta AUDC3
    sta AUDC4
    lda #$03
    sta SKCTL
    ; Disable OS ROM: PORTB AND #$FE — game owns $C000-$FFFF as RAM
    lda PORTB
    ora #$02
    sta PORTB
    lda PORTB
    and #$FE
    sta PORTB
    ; Set NMI vector $FFFA/B -> $91C1 (immediate VBI handler in RAM)
    jsr setup_nmi_vector
    ; Set IRQ vector $FFFE/F -> $9100 (first IRQ handler in chain)
    jsr setup_irq_vector
    ; Clear zero page $00-$FF
    lda #$00
    tay
loc_36C8:
    sta LNFLG,y
    iny
    bne loc_36C8
    ; Clear framebuffer: $F800-$FFFF (8KB under OS ROM, self-modifying high byte)
    ldx #$20
loc_36D0:
    sta AFP,y
    iny
    bne loc_36D0
    inc $36D2
    dex
    bne loc_36D0
    lda #$5A
    sta DSKUTL
    lda #$03
    sta $69
    ; PAL/NTSC detection via SKSTAT bit 3 — sets viewport width
    lda SKSTAT
    and #$08
    beq loc_36FA
    ; NTSC/wide path: 32 columns (first=0, last=$20, adj=$0F)
    lda #$00
    sta viewport_first_col
    lda #$20
    sta viewport_num_cols
    lda #$0F
    sta $73
    jmp loc_3706
loc_36FA:
    ; PAL/narrow path: 27 columns (first=5, last=$1B, adj=$0A)
    lda #$05
    sta viewport_first_col
    lda #$1B
    sta viewport_num_cols
    lda #$0A
    sta $73
loc_3706:
    sta ICBLHZ
    lda #$01
    ; Init game state: music trigger, speed, FPS counter
    sta $89
    sta $8A
    lda #$06
    sta KEYDEF
    lda #$64
    sta fps_timer
    lda #$00
    sta POKMSK
    ; JSR $8ADA — setup buffer pointers for double-buffer rendering
    jsr setup_buffer_ptrs_0
    ldx #$00
    ; JSR $3C5B — init column state arrays
    jsr sub_3C5B
    lda #$78
loc_3724:
    ; Wait for VCOUNT=$78 — sync to specific scanline before enabling display
    cmp VCOUNT
    bne loc_3724
    ; Set initial viewport colors: COLPF1=$0C, COLPF2=$04
    lda #$0C
    sta COLPF1
    lda #$04
    sta COLPF2
    lda #$00
    sta COLBK
    ; PRIOR=$21 — normal playfield mode + PMG priority 1
    lda #$21
    sta PRIOR
    ; CHBASE=$F8 — custom charset at $F800 (RAM under ROM)
    lda #$F8
    sta CHBASE
    ; Display list at $9800
    lda #$00
    sta DLISTL
    lda #$98
    sta DLISTH
    ; Player-missile setup: P0/P1 at $78 (crosshair center), double width
    lda #$00
    sta HPOSM3
    lda #$C0
    sta SIZEM
    lda #$FF
    sta COLPM3
    lda #$78
    sta HPOSP0
    sta HPOSP1
    lda #$00
    sta HPOSP2
    lda #$F6
    sta COLPM0
    lda #$FA
    sta COLPM1
    ; P2 = $80 color, quad width — minimap overlay player
    lda #$80
    sta COLPM2
    lda #$01
    sta SIZEP0
    sta SIZEP1
    lda #$03
    sta SIZEP2
    ; PMBASE=$90 — PMG data at $9000-$97FF
    lda #$90
    sta PMBASE
    lda #$00
    sta AUDC4
    ; CRITICAL: POKEY Timer 4 setup — AUDF4=$01, AUDCTL=$01 (15KHz base)
    lda #$01
    sta AUDF4
    lda #$01
    sta AUDCTL
    ; Sync timer to scanline: WSYNC pair to align with HBLANK
    sta WSYNC
    sta WSYNC
    ; Read VCOUNT, wait for it to increment (2-scanline boundary)
    lda VCOUNT
loc_37A1:
    sta WSYNC
    cmp VCOUNT
    beq loc_37A1
    ; Align to clean scanline pair start with double WSYNC
    sta WSYNC
    sta WSYNC
    ; SKCTL=$00 — STOP all POKEY timers
    lda #$00
    sta SKCTL
    ; 10x NOP — precise delay for timer counter reset
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    lda LNFLG
    ; SKCTL=$03 — restart POKEY
    lda #$03
    sta SKCTL
    ; STIMER=$01 — trigger all timer counters to reload NOW
    lda #$01
    sta STIMER
    ; CLI — interrupts enabled, timer chain begins firing
    cli
    ; GRACTL=$03 — enable player + missile DMA
    lda #$03
    sta GRACTL
    ; NMIEN=$80 — enable VBI (NMI on VBLANK)
    lda #$80
    sta NMIEN
    ; DMACTL=$3D — standard playfield DMA + PMG + normal width
    lda #$3D
    sta DMACTL
main_loop:
    ; MAIN LOOP — no frame sync; renders as fast as CPU allows (~24 FPS PAL)
    lda #$80
    ; Render frame to buffer 1 ($12=$80)
    jsr render_frame
    ; Render frame to buffer 0 ($12=$00)
    lda #$00
    jsr render_frame
    jmp main_loop
.proc render_frame
    ; RENDER FRAME — full pipeline: input → movement → raycast → draw → status
    sta buffer_flag
    ; Process joystick/fire/console input
    jsr process_input
    ; Apply turning velocity and movement
    jsr update_movement
    ; RAYCAST ALL COLUMNS — the hot path (~40% of frame budget)
    jsr render_columns
    ; Draw textured wall columns + sky/floor fill to framebuffer
    jsr draw_all_columns
    ; Check CONSOL keys — flash COLBK if any pressed
    lda CONSOL
    and #$07
    cmp #$07
    beq loc_3814
    lda #$20
    sta COLBK
    lda (LNFLG),y
    lda (LNFLG),y
    lda (LNFLG),y
    lda (LNFLG),y
    lda (LNFLG),y
    lda (LNFLG),y
    lda #$00
    sta COLBK
loc_3814:
    ; Flip display list pointer (double buffer swap)
    jsr sub_3B4E
    ; Update FPS/coordinates/score on status bar
    jsr flip_display_list
    jsr inc_frame_counter
    lda ENDPT
    ora #$10
    sta $9BA0
    ldx $75
    ldy #$10
    jsr draw_hex_byte
    ldx $49
    ldy #$16
    jsr draw_hex_byte
    ldx DSTAT
    ldy #$19
    jsr draw_hex_byte
    rts
.endp

update_movement:
    ; UPDATE MOVEMENT — apply turn accumulator and move player through map
    lda $9F
    beq loc_384B
    clc
    adc $A0
    bcc loc_3849
    jsr sub_38A3
    jmp loc_384B
loc_3849:
    sta $A0
loc_384B:
    ; Process turning: $76=turn state, $77=turn step (0-14)
    lda DELTAR
    beq loc_3892
    ldx DELTAC
    lda $9BD5,x
    tax
    jsr sub_397E
    bit DELTAR
    bmi loc_3865
    lda #$FF
    sta DELTAR
    ldx #$00
    jmp loc_3873
loc_3865:
    ldx DELTAC
    inx
    cpx #$0F
    bne loc_3873
    lda #$00
    sta DELTAR
    jmp loc_3892
loc_3873:
    stx DELTAC
    lda $9BD5,x
    tax
    lda $78
    cmp #$01
    bne loc_3885
    jsr sub_38F0
    jmp loc_3892
loc_3885:
    cmp #$02
    bne loc_388F
    jsr sub_391E
    jmp loc_3892
loc_388F:
    jsr sub_394A
loc_3892:
    ldx SWPFLG
    inx
    cpx #$1C
    bne loc_389B
    ldx #$00
loc_389B:
    stx SWPFLG
    lda $9BE4,x
    sta $7A
    rts
.proc sub_38A3
    ldy $A2
    lda $FFA0,y
    sta ICPTLZ
    lda $FFCD,y
    sta ICPTHZ
    ldy $A1
    lda #$00
    sta (ICPTLZ),y
    sta $9F
    sta $A0
    rts
.endp

sub_38BA:
    lda (ICPTLZ),y
    cmp #$24
    bcc loc_38EF
    cmp #$2F
    bcs loc_38EE
    cmp #$27
    bcs loc_38D3
    ldx ENDPT
    beq loc_38EE
    dec ENDPT
    ldx #$20
    jmp loc_38DE
loc_38D3:
    cmp #$29
    bcs loc_38DC
    ldx #$20
    jmp loc_38DE
loc_38DC:
    ldx #$10
loc_38DE:
    clc
    adc #$11
    sta (ICPTLZ),y
    lda $9F
    beq loc_38EA
    jsr sub_38A3
loc_38EA:
    stx $9F
    sec
    rts
loc_38EE:
    clc
loc_38EF:
    rts
.proc sub_38F0
    ldy #$00
loc_38F2:
    lda $390C,y
    sta $9400,x
    sta $9401,x
    lda $3915,y
    sta $9500,x
    sta $9501,x
    inx
    inx
    iny
    cpy #$09
    bne loc_38F2
    rts
.endp

    cpx #$00
    brk
    .byte $1F
    brk
    .byte $1A
    brk
    brk
    sbc FR1
    cpx #$E0
    .byte $BF
    .byte $BF
    lda $E5
    sbc LNFLG
.proc sub_391E
    ldy #$00
loc_3920:
    lda $393A,y
    sta $9400,x
    sta $9401,x
    lda $3942,y
    sta $9500,x
    sta $9501,x
    inx
    inx
    iny
    cpy #$08
    bne loc_3920
    rts
.endp

    .byte $FF
    .byte $FF
    inc $8181,x
    sta ($7F,x)
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    inc $FEFE,x
    .byte $80
    .byte $80
.proc sub_394A
    ldy #$00
loc_394C:
    lda $3966,y
    sta $9400,x
    sta $9401,x
    lda $3972,y
    sta $9500,x
    sta $9501,x
    inx
    inx
    iny
    cpy #$0C
    bne loc_394C
    rts
.endp

    sbc FTYPE,x
    .byte $7C
    .byte $7C
    .byte $3A
    .byte $14
    php
    bpl $397F
    php
    .byte $14
    .byte $3A
    inc $7E40,x
    ror $387C,x
    bpl $3992
    clc
    bpl $39B5
    .byte $7C
.proc sub_397E
    lda #$00
    ldy #$06
loc_3982:
    sta $9400,x
    sta $9500,x
    sta $9406,x
    sta $9506,x
    sta $940C,x
    sta $950C,x
    sta $9412,x
    sta $9512,x
    inx
    dey
    bpl loc_3982
    rts
.endp

sub_399F:
    ldx ICBLLZ
    inx
    cpx #$04
    bne loc_39A8
    ldx #$00
loc_39A8:
    stx ICBLLZ
    lda $90DA,x
    sta $8C
    lda $90D6,x
    sta $89
    and #$7F
    sta $8A
    jsr sub_3C5B
    ldx ICBLLZ
    lda $90BE,x
    sta WARMST
    lda $90C2,x
    sta MEMORY_REFRESH_CYCLES
    jsr sub_39EB
    lda #$02
    sta APPMHI
    ldx ICBLLZ
    lda $90C6,x
    sta $5098
    lda $90CE,x
    sta $509E
    lda $90CA,x
    sta $50A4
    lda $90D2,x
    sta $50AA
    jmp loc_508B
sub_39EB:
    ldy #$00
    sty $8D
    sty $9D
    sty $9E
    lda (WARMST),y
    tax
    lda $90E6,x
    sta TRAMSZ
    lda $90F1,x
    sta CMCMD
    jmp (TRAMSZ)
    rts
    jsr $3A3A
    jsr $3A4A
    jsr $3B0F
    jmp $3B09
    jsr $3A3A
    lda #$01
    sta $91
    sta $92
    jmp $3A0A
    jsr $3A3A
    iny
    lda (WARMST),y
    sta $91
    lda #$01
    sta $92
    jmp $3A0A
    jsr $3A3A
    lda #$01
    sta $91
    iny
    lda (WARMST),y
    sta $92
    jmp $3A0A
    iny
    lda (WARMST),y
    sta $8E
    iny
    lda (WARMST),y
    sta $8F
    iny
    lda (WARMST),y
    sta $90
    rts
    iny
    lda (WARMST),y
    sta $91
    iny
    lda (WARMST),y
    sta $92
    rts
    lda $9D
    clc
    adc $99
    sta $9D
    inc $9B
    lda $9B
    cmp $97
    bne $3A79
    lda #$00
    sta $9D
    sta $9B
    lda $9E
    clc
    adc $9A
    sta $9E
    inc $9C
    lda $9C
    cmp $98
    beq $3A7E
    ldy $96
    jmp $3B0A
    jsr $3ACB
    jmp $3B09
    iny
    lda (WARMST),y
    sta $97
    iny
    lda (WARMST),y
    sta $98
    iny
    lda (WARMST),y
    sta $99
    iny
    lda (WARMST),y
    sta $9A
    jsr $3ACB
    iny
    sty $96
    jmp $3B0A
    iny
    lda (WARMST),y
    sta $97
    lda #$01
    sta $98
    iny
    lda (WARMST),y
    sta $99
    lda #$00
    sta $9A
    jmp $3A98
    lda #$01
    sta $97
    iny
    lda (WARMST),y
    sta $98
    lda #$00
    sta $99
    iny
    lda (WARMST),y
    sta $9A
    jmp $3A98
    lda #$00
    sta $9B
    sta $9C
    sta $9D
    sta $9E
    rts
    iny
    lda (WARMST),y
    sta $95
    iny
    lda (WARMST),y
    sta $8E
    lda #$01
    sta $91
    sta $92
    jsr $3A3F
    jsr $3B0F
    dec $95
    bne $3AE6
    beq $3B09
    iny
    lda (WARMST),y
    sta $95
    iny
    lda (WARMST),y
    sta $8E
    jsr $3A3F
    jsr $3A4A
    jsr $3B0F
    dec $95
    bne $3AFC
    iny
    sty $8D
    jmp $39F3
    lda #$00
    sta $93
    sta $94
    jsr $3B2D
    inc $93
    lda $93
    cmp $91
    bne $3B15
    lda #$00
    sta $93
    inc $94
    lda $94
    cmp $92
    bne $3B15
    rts
    clc
    lda $9E
    adc $90
    adc $94
    tax
    lda $FFA0,x
    sta $3B4B
    lda $FFCD,x
    sta $3B4C
    lda $9D
    adc $8F
    adc $93
    tax
    lda $8E
    sta sub_FFFF,x
    rts
.proc sub_3B4E
    rts
.endp

.proc flip_display_list
    ; FLIP DISPLAY LIST — toggles JMP target at $9809 between $98 (buf 0) and $9A (buf 1)
    lda buffer_flag
    bne loc_3B59
    lda #$98
    sta $9809
    rts
loc_3B59:
    lda #$9A
    sta $9809
    rts
.endp

.proc inc_frame_counter
    ; INC FRAME COUNTER — 16-bit counter at $10/$11, used for FPS calc
    inc POKMSK
    bne loc_3B65
    inc BRKKEY
loc_3B65:
    rts
.endp

vbi_helper:
    ; VBI HELPER — decrement FPS timer, every 100 frames: compute FPS and update display
    dec fps_timer
    bne loc_3B82
    lda #$64
    sta fps_timer
    lda POKMSK
    lsr BRKKEY
    ror
    tax
    ldy #$05
    jsr draw_hex_byte
    lda #$00
    sta POKMSK
    sta BRKKEY
    jmp loc_3BA5
loc_3B82:
    rts
.proc draw_hex_byte
    ; DRAW HEX BYTE — convert X to 2 hex digits, write to status bar at position Y in $9B95
    lda $9995,x
    tax
    lsr
    lsr
    lsr
    lsr
    ora #$10
    sta status_line_1,y
    txa
    and #$0F
    ora #$10
    sta $9B96,y
    rts
.endp

loc_3B99:
    ; Reset fade timer to $5A (90 frames)
    lda #$5A
    sta DSKUTL
    lda #$00
    sta $1B
    sta COLBK
    rts
loc_3BA5:
    ; Fade effect: decrement timer, then ramp COLBK up by $10 per step
    lda DSKUTL
    beq loc_3BAC
    dec DSKUTL
    rts
loc_3BAC:
    lda $1B
    clc
    adc #$10
    bcc loc_3BB5
    lda #$10
loc_3BB5:
    sta $1B
    sta COLBK
    rts
.proc setup_nmi_vector
    ; SETUP NMI VECTOR — write $91C1 to $FFFA/B (RAM under ROM)
    lda #$C1
    sta $FFFA
    lda #$91
    sta $FFFB
    rts
.endp

setup_irq_vector:
    ; SETUP IRQ VECTOR — write $9100 to $FFFE/F (RAM under ROM)
    lda #$00
    sta $FFFE
    lda #$91
    sta sub_FFFF
    rts
render_columns:
    ; RENDER COLUMNS — iterates viewport columns ($71 to $72), raycasts + draws each
    lda viewport_first_col
    sta current_column
    ; Load column angle from lookup table
    tax
    lda $46C2,x
    sta STATUS
    lda ICBLHZ
    sta ICIDNO
    tax
    lda $486A,x
    sta CIOCHR
    lda ICAX1Z
    sta ICAX2Z
    ; Setup ray direction for first column
    jsr setup_ray
    ldx ICBLHZ
    lda $494F,x
    sta SHFAMT
    lda $497C,x
    sta ROWAC
    lda ENTVEC
    sta $2D
    bne loc_3C0C
    lda $44
    sta $6D
    lda $47
    sta BITMSK
    ; Compute perspective projection for this column
    jsr perspective_project
    jmp column_loop
loc_3C0C:
    lda $44
    sta BITMSK
    lda $47
    sta $6D
    jsr perspective_project
column_loop:
    ; COLUMN LOOP — cast ray, store results, advance to next column
    jsr cast_ray
    ldy current_column
    lda FRMADR
    sta col_distance,y
    ; Store wall distance to $8F00,Y; texture to $8F20,Y
    cmp #$21
    bcc loc_3C2C
    lda #$00
    sta (ABUFPT),y
    jmp loc_3C33
loc_3C2C:
    lda #$20
    sec
    sbc FRMADR
    sta (ABUFPT),y
loc_3C33:
    lda TOADR
    sta col_texture,y
    lda $67
    sta col_texcol,y
    lda col_texture,y
    tax
    lda $9037,x
    sta (ICHIDZ),y
    ; Advance column index, loop until viewport filled
    inc current_column
    lda current_column
    cmp viewport_num_cols
    beq loc_3C5A
    tax
    lda $46C2,x
    sta STATUS
    ; Advance ray to next column angle
    jsr advance_ray
    jmp column_loop
loc_3C5A:
    rts
.proc sub_3C5B
    lda $90DE,x
    sta $49
    lda $90E2,x
    sta DSTAT
    lda #$00
    sta ZCHAIN
    sta $4B
    lda #$9F
    sta ATRACT
    lda #$7C
    sta DRKMSK
    lda #$00
    sta OLDCOL
    sta $5C
    sta ICAX1Z
    sta ENTVEC
    lda $73
    sta ICBLHZ
    jsr sub_4086
    jsr sub_410B
    rts
.endp

sub_3C88:
    tay
    ldx OLDCOL
    lsr
    bcs loc_3CA5
    lda $472F,x
    sta $61
    lda $4702,x
    sta FKDEF
    lda $4789,x
    sta LOGCOL
    lda $475C,x
    sta PALNTS
    jmp loc_3CB9
loc_3CA5:
    lda $472F,x
    sta LOGCOL
    lda $4702,x
    sta PALNTS
    lda $4789,x
    sta $61
    lda $475C,x
    sta FKDEF
loc_3CB9:
    lda $4A03,y
    sta $5F
    lda $49
    sta OLDCHR
    lda DSTAT
    sta OLDADR
    lda #$00
    sta ADRESS
    sta $65
    bit $5F
    bvs loc_3CD3
    jmp loc_3D0F
loc_3CD3:
    lda $4B
    clc
    adc $61
    sta $4B
    lda ZCHAIN
    adc FKDEF
    sta ZCHAIN
    bcc loc_3CE4
    inc $49
loc_3CE4:
    lda OLDROW
    clc
    adc $61
    sta OLDROW
    lda $59
    adc FKDEF
    sta $59
    bcc loc_3CF5
    inc SAVMSC
loc_3CF5:
    lda DINDEX
    clc
    adc $61
    sta DINDEX
    lda $56
    adc FKDEF
    sta $56
    bcc loc_3D4B
    inc COLCRS
    inc $65
    lda #$08
    sta ADRESS
    jmp loc_3D4B
loc_3D0F:
    lda $4B
    sec
    sbc $61
    sta $4B
    lda ZCHAIN
    sbc FKDEF
    sta ZCHAIN
    bcs loc_3D20
    dec $49
loc_3D20:
    lda DINDEX
    sec
    sbc $61
    sta DINDEX
    lda $56
    sbc FKDEF
    sta $56
    bcs loc_3D31
    dec COLCRS
loc_3D31:
    lda OLDROW
    sec
    sbc $61
    sta OLDROW
    lda $59
    sbc FKDEF
    sta $59
    bcs loc_3D4B
    dec SAVMSC
    inc $65
    lda #$04
    sta ADRESS
    jmp loc_3D4B
loc_3D4B:
    bit $5F
    bmi loc_3D52
    jmp loc_3D90
loc_3D52:
    lda DRKMSK
    clc
    adc LOGCOL
    sta DRKMSK
    lda ATRACT
    adc PALNTS
    sta ATRACT
    bcc loc_3D63
    inc DSTAT
loc_3D63:
    lda HOLD1
    clc
    adc LOGCOL
    sta HOLD1
    lda TMPCHR
    adc PALNTS
    sta TMPCHR
    bcc loc_3D74
    inc COLRSH
loc_3D74:
    lda ROWCRS
    clc
    adc LOGCOL
    sta ROWCRS
    lda RMARGN
    adc PALNTS
    sta RMARGN
    bcc loc_3DCB
    inc LMARGN
    inc $65
    lda ADRESS
    ora #$02
    sta ADRESS
    jmp loc_3DCB
loc_3D90:
    lda DRKMSK
    sec
    sbc LOGCOL
    sta DRKMSK
    lda ATRACT
    sbc PALNTS
    sta ATRACT
    bcs loc_3DA1
    dec DSTAT
loc_3DA1:
    lda ROWCRS
    sec
    sbc LOGCOL
    sta ROWCRS
    lda RMARGN
    sbc PALNTS
    sta RMARGN
    bcs loc_3DB2
    dec LMARGN
loc_3DB2:
    lda HOLD1
    sec
    sbc LOGCOL
    sta HOLD1
    lda TMPCHR
    sbc PALNTS
    sta TMPCHR
    bcs loc_3DCB
    dec COLRSH
    inc $65
    lda ADRESS
    ora #$01
    sta ADRESS
loc_3DCB:
    lda $65
    beq loc_3E14
    cmp #$01
    bne loc_3DEA
    lda ADRESS
    lsr
    bcc loc_3DDB
    jmp loc_3ED4
loc_3DDB:
    lsr
    bcc loc_3DE1
    jmp loc_3E9B
loc_3DE1:
    lsr
    bcc loc_3DE7
    jmp loc_3E58
loc_3DE7:
    jmp loc_3E15
loc_3DEA:
    lda #$00
    sta PM_DMA_CYCLES
    ldx COLRSH
    lda $FFA0,x
    sta ICPTLZ
    lda $FFCD,x
    sta ICPTHZ
    lda ADRESS
    cmp #$0A
    bne loc_3E03
    jmp loc_3F0D
loc_3E03:
    cmp #$09
    bne loc_3E0A
    jmp loc_3F4B
loc_3E0A:
    cmp #$06
    bne loc_3E11
    jmp loc_3F84
loc_3E11:
    jmp loc_3FC4
loc_3E14:
    rts
loc_3E15:
    ldy COLCRS
    ldx COLRSH
    lda $FFA0,x
    sta ICPTLZ
    lda $FFCD,x
    sta ICPTHZ
    lda (ICPTLZ),y
    cmp #$08
    bcs loc_3E3C
    ldx LMARGN
    lda $FFA0,x
    sta ICPTLZ
    lda $FFCD,x
    sta ICPTHZ
    lda (ICPTLZ),y
    cmp #$08
    bcs loc_3E3C
    rts
loc_3E3C:
    dey
    sty COLCRS
    sty $49
    sty SAVMSC
    lda #$FF
    sta $56
    lda #$80
    sta ZCHAIN
    lda #$00
    sta $59
    lda #$00
    sta DINDEX
    sta $4B
    sta OLDROW
    rts
loc_3E58:
    ldy SAVMSC
    ldx COLRSH
    lda $FFA0,x
    sta ICPTLZ
    lda $FFCD,x
    sta ICPTHZ
    lda (ICPTLZ),y
    cmp #$08
    bcs loc_3E7F
    ldx LMARGN
    lda $FFA0,x
    sta ICPTLZ
    lda $FFCD,x
    sta ICPTHZ
    lda (ICPTLZ),y
    cmp #$08
    bcs loc_3E7F
    rts
loc_3E7F:
    iny
    sty SAVMSC
    sty $49
    sty COLCRS
    lda #$00
    sta $59
    lda #$80
    sta ZCHAIN
    lda #$FF
    sta $56
    lda #$00
    sta OLDROW
    sta $4B
    sta DINDEX
    rts
loc_3E9B:
    ldx LMARGN
    lda $FFA0,x
    sta ICPTLZ
    lda $FFCD,x
    sta ICPTHZ
    ldy SAVMSC
    lda (ICPTLZ),y
    cmp #$08
    bcs sub_3EB8
    ldy COLCRS
    lda (ICPTLZ),y
    cmp #$08
    bcs sub_3EB8
    rts
.proc sub_3EB8
    dex
    stx LMARGN
    stx DSTAT
    stx COLRSH
    lda #$FF
    sta RMARGN
    lda #$80
    sta ATRACT
    lda #$00
    sta TMPCHR
    lda #$00
    sta ROWCRS
    sta DRKMSK
    sta HOLD1
    rts
.endp

loc_3ED4:
    ldx COLRSH
    lda $FFA0,x
    sta ICPTLZ
    lda $FFCD,x
    sta ICPTHZ
    ldy SAVMSC
    lda (ICPTLZ),y
    cmp #$08
    bcs sub_3EF1
    ldy COLCRS
    lda (ICPTLZ),y
    cmp #$08
    bcs sub_3EF1
    rts
.proc sub_3EF1
    inx
    stx COLRSH
    stx DSTAT
    stx LMARGN
    lda #$00
    sta TMPCHR
    lda #$80
    sta ATRACT
    lda #$FF
    sta RMARGN
    lda #$00
    sta HOLD1
    sta DRKMSK
    sta ROWCRS
    rts
.endp

loc_3F0D:
    lda SAVMSC
    clc
    adc #$20
    tay
    lda (ICPTLZ),y
    cmp #$08
    bcc loc_3F20
    inc PM_DMA_CYCLES
    ldx LMARGN
    jsr sub_3EB8
loc_3F20:
    ldy COLCRS
    lda (ICPTLZ),y
    cmp #$08
    bcc loc_3F2B
    jmp loc_3E3C
loc_3F2B:
    lda PM_DMA_CYCLES
    bne loc_3F4A
    lda COLCRS
    adc #$20
    tay
    lda (ICPTLZ),y
    cmp #$08
    bcc loc_3F4A
    lda RMARGN
    cmp $56
    bcc loc_3F45
    ldy COLCRS
    jmp loc_3E3C
loc_3F45:
    ldx LMARGN
    jmp sub_3EB8
loc_3F4A:
    rts
loc_3F4B:
    ldy SAVMSC
    lda (ICPTLZ),y
    cmp #$08
    bcc loc_3F58
    inc PM_DMA_CYCLES
    jsr sub_3EF1
loc_3F58:
    lda COLCRS
    clc
    adc #$20
    tay
    lda (ICPTLZ),y
    cmp #$08
    bcc loc_3F69
    ldy COLCRS
    jmp loc_3E3C
loc_3F69:
    lda PM_DMA_CYCLES
    bne loc_3F83
    ldy COLCRS
    lda (ICPTLZ),y
    cmp #$08
    bcc loc_3F83
    lda #$00
    sbc TMPCHR
    cmp $56
    bcc loc_3F80
    jmp loc_3E3C
loc_3F80:
    jmp sub_3EF1
loc_3F83:
    rts
loc_3F84:
    lda COLCRS
    clc
    adc #$20
    tay
    lda (ICPTLZ),y
    cmp #$08
    bcc loc_3F97
    inc PM_DMA_CYCLES
    ldx LMARGN
    jsr sub_3EB8
loc_3F97:
    ldy SAVMSC
    lda (ICPTLZ),y
    cmp #$08
    bcc loc_3FA2
    jmp loc_3E7F
loc_3FA2:
    lda PM_DMA_CYCLES
    bne loc_3FC3
    lda SAVMSC
    adc #$20
    tay
    lda (ICPTLZ),y
    cmp #$08
    bcc loc_3FC3
    lda #$00
    sbc $59
    cmp RMARGN
    bcc loc_3FBE
    ldx LMARGN
    jmp sub_3EB8
loc_3FBE:
    ldy SAVMSC
    jmp loc_3E7F
loc_3FC3:
    rts
loc_3FC4:
    ldy COLCRS
    lda (ICPTLZ),y
    cmp #$08
    bcc loc_3FD1
    inc PM_DMA_CYCLES
    jsr sub_3EF1
loc_3FD1:
    lda SAVMSC
    clc
    adc #$20
    tay
    lda (ICPTLZ),y
    cmp #$08
    bcc loc_3FE2
    ldy SAVMSC
    jmp loc_3E7F
loc_3FE2:
    lda PM_DMA_CYCLES
    bne loc_4003
    ldy SAVMSC
    lda (ICPTLZ),y
    cmp #$08
    bcc loc_4003
    lda #$00
    sbc TMPCHR
    sta PM_DMA_CYCLES
    lda #$00
    sec
    sbc $59
    cmp PM_DMA_CYCLES
    bcc init_routine
    jmp sub_3EF1
init_routine:
    ; INIT ROUTINE — runs during XEX loading, copies tables to RAM under OS ROM, then returns
    jmp loc_3E7F
loc_4003:
    ; Disable interrupts and DMA
    rts
sub_4004:
    lda $5C
    jsr sub_3C88
    ; PORTB AND #$FE — disable OS ROM, expose RAM at $C000-$FFFF
    jmp loc_4030
sub_400C:
    lda $5C
    eor #$02
    jsr sub_3C88
    jmp loc_4030
sub_4016:
    lda $5C
    clc
    adc #$03
    and #$03
    jsr sub_3C88
    jmp loc_4030
sub_4023:
    lda $5C
    clc
    adc #$01
    and #$03
    jsr sub_3C88
    jmp loc_4030
loc_4030:
    ; Dest pointer: $FC/$FD = $C000
    lda $49
    cmp OLDCHR
    ; Counter: $FA/$FB = $1000 (4096 bytes)
    bne loc_403C
    lda DSTAT
    cmp OLDADR
    beq loc_4085
loc_403C:
    ldy DSTAT
    lda $FFA0,y
    sta ICPTLZ
    lda $FFCD,y
    sta ICPTHZ
    ldy $49
    lda (ICPTLZ),y
    beq loc_4085
    cmp #$01
    bne loc_4059
    inc ENDPT
    lda #$01
    jmp loc_407B
loc_4059:
    cmp #$05
    ; COPY 2: $509B+ → $F800+ (2042 bytes) — charset + HW vectors
    bne loc_4063
    jsr sub_399F
    jmp loc_4085
loc_4063:
    cmp #$03
    bne loc_406E
    inc $75
    lda #$02
    jmp loc_407B
loc_406E:
    cmp #$04
    bne loc_4085
    lda $75
    clc
    adc #$05
    sta $75
    lda #$03
loc_407B:
    sta $78
    lda #$00
    sta (ICPTLZ),y
    lda #$01
    sta DELTAR
loc_4085:
    rts
sub_4086:
    clc
    lda $4B
    adc #$00
    ; PORTB ORA #$01 — re-enable OS ROM
    sta DINDEX
    lda ZCHAIN
    adc #$7F
    ; CLI — re-enable interrupts
    sta $56
    lda $49
    adc #$00
    sta COLCRS
    sec
    .byte $A5                     ; $409A
init_tables_for_c000:
    .byte $4B                     ; $409B
    .byte $E9,$00,$85,$5A,$A5,$4A,$E9,$7F,$85,$59,$A5,$49,$E9,$00,$85,$58 ; $409C
    .byte $18,$A5,$4E,$69,$00,$85,$54,$A5,$4D,$69,$7F,$85,$53,$A5,$4C,$69 ; $40AC
    .byte $00,$85,$52,$38,$A5,$4E,$E9,$00,$85,$51,$A5,$4D,$E9,$7F,$85,$50 ; $40BC
    .byte $A5,$4C,$E9,$00,$85,$4F,$60                      ; $40CC
sub_40D3:
    .byte $A5,$5B,$38,$E5,$69,$90,$03,$85,$5B,$60          ; $40D3
loc_40DD:
    .byte $69,$2D,$85,$5B,$A5,$5C,$38,$E9,$01,$B0,$02,$A9,$03 ; $40DD
loc_40EA:
    .byte $85,$5C,$60                                      ; $40EA
sub_40ED:
    .byte $A5,$5B,$18,$65,$69,$C9,$2D,$B0,$03,$85,$5B,$60  ; $40ED
loc_40F9:
    .byte $E9,$2D,$85,$5B,$A5,$5C,$18,$69,$01,$C9,$04,$D0,$02,$A9,$00 ; $40F9
loc_4108:
    .byte $85,$5C,$60                                      ; $4108
sub_410B:
    .byte $A6,$5B,$A4,$5C,$98,$4A,$B0,$17,$BD,$E3,$47,$85,$61,$BD,$B6,$47 ; $410B
    .byte $85,$60,$BD,$3D,$48,$85,$63,$BD,$10,$48,$85,$62,$4C,$3E,$41 ; $411B
loc_412A:
    .byte $BD,$E3,$47,$85,$63,$BD,$B6,$47,$85,$62,$BD,$3D,$48,$85,$61,$BD ; $412A
    .byte $10,$48,$85,$60                                  ; $413A
loc_413E:
    .byte $B9,$03,$4A,$29,$40,$F0,$16,$A5,$4B,$38,$E5,$61,$85,$45,$A5,$4A ; $413E
    .byte $E5,$60,$85,$44,$A5,$49,$E9,$00,$85,$43,$4C,$6E,$41 ; $414E
loc_415B:
    .byte $A5,$4B,$18,$65,$61,$85,$45,$A5,$4A,$65,$60,$85,$44,$A5,$49,$69 ; $415B
    .byte $00,$85,$43                                      ; $416B
loc_416E:
    .byte $B9,$03,$4A,$10,$16,$A5,$4E,$38,$E5,$63,$85,$48,$A5,$4D,$E5,$62 ; $416E
    .byte $85,$47,$A5,$4C,$E9,$00,$85,$46,$4C,$9C,$41      ; $417E
loc_4189:
    .byte $A5,$4E,$18,$65,$63,$85,$48,$A5,$4D,$65,$62,$85,$47,$A5,$4C,$69 ; $4189
    .byte $00,$85,$46                                      ; $4199
loc_419C:
    .byte $60                                              ; $419C
setup_ray:
    ; SETUP RAY — compute ray direction from player angle + column offset, select quadrant
    .byte $A6,$2B,$BD,$07,$4A,$8D,$4F,$44,$8D,$DF,$45,$BD,$0B,$4A,$8D,$D9 ; $419D
    .byte $44,$8D,$69,$46,$BD,$4B,$49,$18,$65,$2E,$AA,$BD,$97,$48,$AA ; $41AD
    ; Determine ray quadrant (self-modifying: patches JSR target in cast_ray)
    .byte $30,$11,$A9,$A2,$8D,$A0,$43,$A9,$43,$8D,$A1,$43,$A9,$00,$85,$2C ; $41BC
    .byte $4C,$DD,$41                                      ; $41CC
loc_41CF:
    .byte $A9,$32,$8D,$A0,$43,$A9,$45,$8D,$A1,$43,$A9,$FF,$85,$2C ; $41CF
loc_41DD:
    .byte $8A,$29,$01,$F0,$06                              ; $41DD
    ; Setup X step direction based on quadrant
    .byte $20,$C7,$42,$4C,$EB,$41                          ; $41E2
loc_41E8:
    .byte $20,$EC,$42                                      ; $41E8
loc_41EB:
    .byte $8A,$29,$02,$F0,$06                              ; $41EB
    ; Setup Y step direction based on quadrant
    .byte $20,$33,$43,$4C,$F9,$41                          ; $41F0
loc_41F6:
    .byte $20,$58,$43                                      ; $41F6
loc_41F9:
    .byte $60                                              ; $41F9
advance_ray:
    ; ADVANCE RAY — step ray angle to next viewport column
    .byte $A5,$2E,$38,$E9,$01,$B0,$42,$AD,$4F,$44,$49,$FF,$8D,$4F,$44,$8D ; $41FA
    .byte $DF,$45,$AD,$D9,$44,$49,$FF,$8D,$D9,$44,$8D,$69,$46,$A5,$2B,$38 ; $420A
    .byte $E9,$01,$B0,$02,$A9,$03                          ; $421A
loc_4220:
    .byte $85,$2B,$D0,$06,$20,$33,$43,$4C,$41,$42          ; $4220
loc_422A:
    .byte $C9,$01,$D0,$06,$20,$C7,$42,$4C,$41,$42          ; $422A
loc_4234:
    .byte $C9,$02,$D0,$06,$20,$58,$43,$4C,$41,$42          ; $4234
loc_423E:
    .byte $20,$EC,$42                                      ; $423E
loc_4241:
    .byte $A9,$2C                                          ; $4241
loc_4243:
    ; Boundary handling: wrap ray angle, reconfigure step directions
    .byte $85,$2E,$AA,$BD,$6A,$48,$85,$2F,$E0,$16,$D0,$3F,$A6,$29,$BD,$A9 ; $4243
    .byte $49,$85,$6F,$BD,$D6,$49,$85,$70,$A5,$2D,$49,$FF,$85,$2D,$D0,$16 ; $4253
    .byte $A9,$A2,$8D,$A0,$43,$A9,$43,$8D,$A1,$43,$A5,$44,$85,$6D,$A5,$47 ; $4263
    .byte $85,$6E,$20,$37,$4C,$60                          ; $4273
loc_4279:
    .byte $A9,$32,$8D,$A0,$43,$A9,$45,$8D,$A1,$43,$A5,$44,$85,$6E,$A5,$47 ; $4279
    .byte $85,$6D,$20,$37,$4C                              ; $4289
loc_428E:
    .byte $60                                              ; $428E
sub_428F:
    .byte $A5,$29,$38,$E5,$69,$90,$03,$85,$29,$60          ; $428F
loc_4299:
    .byte $69,$2D,$85,$29,$A5,$2A,$38,$E9,$01,$B0,$02,$A9,$03 ; $4299
loc_42A6:
    .byte $85,$2A,$60                                      ; $42A6
sub_42A9:
    .byte $A5,$29,$18,$65,$69,$C9,$2D,$B0,$03,$85,$29,$60  ; $42A9
loc_42B5:
    .byte $E9,$2D,$85,$29,$A5,$2A,$18,$69,$01,$C9,$04,$D0,$02,$A9,$00 ; $42B5
loc_42C4:
    .byte $85,$2A,$60                                      ; $42C4
sub_42C7:
    .byte $A9,$E6,$8D,$A8,$44,$8D,$38,$46,$A5,$43,$85,$3D,$A5,$44,$85,$41 ; $42C7
    .byte $A9,$D6,$8D,$D4,$43,$A9,$43,$8D,$D5,$43,$A9,$66,$8D,$64,$45,$A9 ; $42D7
    .byte $45,$8D,$65,$45,$60                              ; $42E7
setup_x_step:
    .byte $A9,$C6,$8D,$A8,$44,$8D,$38,$46,$A5,$43,$85,$3D,$A5,$44,$D0,$02 ; $42EC
    .byte $C6,$3D                                          ; $42FC
loc_42FE:
    .byte $49,$FF,$18,$69,$01,$85,$41,$A5,$44,$F0,$15,$A9,$E8,$8D,$D4,$43 ; $42FE
    .byte $A9,$43,$8D,$D5,$43,$A9,$78,$8D,$64,$45,$A9,$45,$8D,$65,$45,$60 ; $430E
loc_431E:
    .byte $A9,$D6,$8D,$D4,$43,$A9,$43,$8D,$D5,$43,$A9,$66,$8D,$64,$45,$A9 ; $431E
    .byte $45,$8D,$65,$45,$60                              ; $432E
setup_y_step:
    .byte $A9,$E6,$8D,$1E,$44,$8D,$AE,$45,$A5,$46,$85,$3E,$A5,$47,$85,$42 ; $4333
    .byte $A9,$A7,$8D,$A5,$43,$A9,$43,$8D,$A6,$43,$A9,$37,$8D,$35,$45,$A9 ; $4343
    .byte $45,$8D,$36,$45,$60                              ; $4353
sub_4358:
    .byte $A9,$C6,$8D,$1E,$44,$8D,$AE,$45,$A5,$46,$85,$3E,$A5,$47,$D0,$02 ; $4358
    .byte $C6,$3E                                          ; $4368
loc_436A:
    .byte $49,$FF,$18,$69,$01,$85,$42,$A5,$47,$F0,$15,$A9,$B9,$8D,$A5,$43 ; $436A
    .byte $A9,$43,$8D,$A6,$43,$A9,$49,$8D,$35,$45,$A9,$45,$8D,$36,$45,$60 ; $437A
loc_438A:
    .byte $A9,$A7,$8D,$A5,$43,$A9,$43,$8D,$A6,$43,$A9,$37,$8D,$35,$45,$A9 ; $438A
    .byte $45,$8D,$36,$45,$60                              ; $439A
cast_ray:
    ; CAST RAY — DDA (Digital Differential Analyzer) wall detection through map grid
    .byte $4C,$FF,$FF                                      ; $439F
loc_43A2:
    .byte $A6,$2F,$4C,$FF,$FF                              ; $43A2
loc_43A7:
    ; Load precomputed ray step increments from tables at $4A00-$4C09
    .byte $BD,$7F,$4B,$85,$32,$BD,$F2,$4B,$85,$37,$BD,$09,$4C,$85,$38,$4C ; $43A7
    .byte $D3,$43,$38,$BD,$6B,$4A,$FD,$7F,$4B,$85,$32,$38,$BD,$F5,$4A,$FD ; $43B7
    .byte $09,$4C,$85,$38,$BD,$DE,$4A,$FD,$F2,$4B,$85,$37  ; $43C7
loc_43D3:
    .byte $4C,$FF,$FF                                      ; $43D3
loc_43D6:
    .byte $BD,$3A,$4B,$85,$35,$BD,$AD,$4B,$85,$3A,$BD,$C4,$4B,$85,$3B,$4C ; $43D6
    .byte $02,$44,$38,$BD,$26,$4A,$FD,$3A,$4B,$85,$35,$38,$BD,$B0,$4A,$FD ; $43E6
    .byte $C4,$4B,$85,$3B,$BD,$99,$4A,$FD,$AD,$4B,$85,$3A  ; $43F6
loc_4402:
    ; DDA main loop: step through grid cells, test for wall hit
    .byte $A5,$3D,$85,$3F,$A5,$3E,$85,$40                  ; $4402
loc_440A:
    ; Compare ray integer position against step boundary
    .byte $A5,$37,$C5,$3A,$F0,$05,$90,$0C,$4C,$A8,$44      ; $440A
loc_4415:
    .byte $A5,$38,$C5,$3B,$90,$03,$4C,$A8,$44              ; $4415
loc_441E:
    ; Step to next grid cell (fractional accumulator)
    .byte $E6,$40,$A5,$37,$D0,$09,$A6,$30,$BD,$E2,$46,$C5,$38,$B0,$5F ; $441E
loc_442D:
    .byte $A4,$40,$B9,$A0,$FF,$85,$26,$B9,$CD,$FF,$85,$27,$A4,$3F,$B1,$26 ; $442D
    .byte $F0,$4D                                          ; $443D
    ; Wall hit: check wall type, compute texture column from hit position
    .byte $C9,$08,$90,$49,$AA,$BD,$46,$90,$85,$66,$A5,$32,$18,$65,$41,$49 ; $443F
    .byte $00,$E0,$30,$90,$21,$E0,$32,$90,$0D,$E0,$35,$90,$13,$C5,$A0,$90 ; $444F
    .byte $2C,$E5,$A0,$4C,$75,$44                          ; $445F
loc_4465:
    .byte $C9,$58,$90,$23,$C9,$A8,$90,$08,$B0,$1D          ; $4465
loc_446F:
    .byte $C9,$60,$90,$19,$E9,$60                          ; $446F
loc_4475:
    ; Compute texture column offset (hit position >> 3 = nibble within wall)
    .byte $4A,$4A,$4A,$85,$67,$A5,$37                      ; $4475
    ; SELF-MODIFYING: patch BSP comparison addresses from ray position
    .byte $8D,$E3,$8D,$A5,$38,$8D,$F5,$8D,$A6,$30          ; $447C
    ; Call BSP traversal to determine visible wall face
    .byte $20,$C6,$8D,$86,$68,$60                          ; $4486
loc_448C:
    ; No wall hit: advance ray by full grid step, continue DDA
    .byte $A6,$2F,$18,$A5,$32,$7D,$6B,$4A,$85,$32,$18,$A5,$38,$7D,$F5,$4A ; $448C
    .byte $85,$38,$A5,$37,$7D,$DE,$4A,$85,$37,$4C,$0A,$44  ; $449C
loc_44A8:
    .byte $E6,$3F,$A5,$3A,$D0,$09,$A6,$30,$BD,$E2,$46,$C5,$3B,$B0,$5F ; $44A8
loc_44B7:
    .byte $A4,$40,$B9,$A0,$FF,$85,$26,$B9,$CD,$FF,$85,$27,$A4,$3F,$B1,$26 ; $44B7
    .byte $F0,$4D,$C9,$08,$90,$49,$AA,$BD,$7E,$90,$85,$66,$A5,$35,$18,$65 ; $44C7
    .byte $42,$49,$00,$E0,$30,$90,$21,$E0,$32,$90,$0D,$E0,$35,$90,$13,$C5 ; $44D7
    .byte $A0,$90,$2C,$E5,$A0,$4C,$FF,$44                  ; $44E7
loc_44EF:
    .byte $C9,$58,$90,$23,$C9,$A8,$90,$08,$B0,$1D          ; $44EF
loc_44F9:
    .byte $C9,$60,$90,$19,$E9,$60                          ; $44F9
loc_44FF:
    .byte $4A,$4A,$4A,$85,$67,$A5,$3A,$8D,$E3,$8D,$A5,$3B,$8D,$F5,$8D,$A6 ; $44FF
    .byte $30,$20,$C6,$8D,$86,$68,$60                      ; $450F
loc_4516:
    .byte $A6,$2F,$18,$A5,$35,$7D,$26,$4A,$85,$35,$18,$A5,$3B,$7D,$B0,$4A ; $4516
    .byte $85,$3B,$A5,$3A,$7D,$99,$4A,$85,$3A,$4C,$0A,$44,$A6,$2F,$4C,$FF ; $4526
    .byte $FF,$BD,$3A,$4B,$85,$32,$BD,$AD,$4B,$85,$37,$BD,$C4,$4B,$85,$38 ; $4536
    .byte $4C,$63,$45,$38,$BD,$26,$4A,$FD,$3A,$4B,$85,$32,$38,$BD,$B0,$4A ; $4546
    .byte $FD,$C4,$4B,$85,$38,$BD,$99,$4A,$FD,$AD,$4B,$85,$37,$4C,$FF,$FF ; $4556
    .byte $BD,$7F,$4B,$85,$35,$BD,$F2,$4B,$85,$3A,$BD,$09,$4C,$85,$3B,$4C ; $4566
    .byte $92,$45,$38,$BD,$6B,$4A,$FD,$7F,$4B,$85,$35,$38,$BD,$F5,$4A,$FD ; $4576
    .byte $09,$4C,$85,$3B,$BD,$DE,$4A,$FD,$F2,$4B,$85,$3A,$A5,$3D,$85,$3F ; $4586
    .byte $A5,$3E,$85,$40,$A5,$37,$C5,$3A,$F0,$05,$90,$0C,$4C,$38,$46,$A5 ; $4596
    .byte $38,$C5,$3B,$90,$03,$4C,$38,$46,$E6,$40,$A5,$37,$D0,$09,$A6,$30 ; $45A6
    .byte $BD,$E2,$46,$C5,$38,$B0,$5F,$A4,$40,$B9,$A0,$FF,$85,$26,$B9,$CD ; $45B6
    .byte $FF,$85,$27,$A4,$3F,$B1,$26,$F0,$4D,$C9,$08,$90,$49,$AA,$BD,$46 ; $45C6
    .byte $90,$85,$66,$A5,$32,$18,$65,$41,$49,$00,$E0,$30,$90,$21,$E0,$32 ; $45D6
    .byte $90,$0D,$E0,$35,$90,$13,$C5,$A0,$90,$2C,$E5,$A0,$4C,$05,$46,$C9 ; $45E6
    .byte $58,$90,$23,$C9,$A8,$90,$08,$B0,$1D,$C9,$60,$90,$19,$E9,$60,$4A ; $45F6
    .byte $4A,$4A,$85,$67,$A5,$37,$8D,$E3,$8D,$A5,$38,$8D,$F5,$8D,$A6,$30 ; $4606
    .byte $20,$C6,$8D,$86,$68,$60,$A6,$2F,$18,$A5,$32,$7D,$26,$4A,$85,$32 ; $4616
    .byte $18,$A5,$38,$7D,$B0,$4A,$85,$38,$A5,$37,$7D,$99,$4A,$85,$37,$4C ; $4626
    .byte $9A,$45,$E6,$3F,$A5,$3A,$D0,$09,$A6,$30,$BD,$E2,$46,$C5,$3B,$B0 ; $4636
    .byte $5F,$A4,$40,$B9,$A0,$FF,$85,$26,$B9,$CD,$FF,$85,$27,$A4,$3F,$B1 ; $4646
    .byte $26,$F0,$4D,$C9,$08,$90,$49,$AA,$BD,$7E,$90,$85,$66,$A5,$35,$18 ; $4656
    .byte $65,$42,$49,$00,$E0,$30,$90,$21,$E0,$32,$90,$0D,$E0,$35,$90,$13 ; $4666
    .byte $C5,$A0,$90,$2C,$E5,$A0,$4C,$8F,$46,$C9,$58,$90,$23,$C9,$A8,$90 ; $4676
    .byte $08,$B0,$1D,$C9,$60,$90,$19,$E9,$60,$4A,$4A,$4A,$85,$67,$A5,$3A ; $4686
    .byte $8D,$E3,$8D,$A5,$3B,$8D,$F5,$8D,$A6,$30,$20,$C6,$8D,$86,$68,$60 ; $4696
    .byte $A6,$2F,$18,$A5,$35,$7D,$6B,$4A,$85,$35,$18,$A5,$3B,$7D,$F5,$4A ; $46A6
    .byte $85,$3B,$A5,$3A,$7D,$DE,$4A,$85,$3A,$4C,$9A,$45,$0F,$0E,$0D,$0C ; $46B6
    .byte $0B,$0A,$09,$08,$07,$06,$05,$04,$03,$02,$01,$00,$00,$01,$02,$03 ; $46C6
    .byte $04,$05,$06,$07,$08,$09,$0A,$0B,$0C,$0D,$0E,$0F,$D5,$D5,$D5,$D6 ; $46D6
    .byte $D7,$D9,$DA,$DC,$DE,$E1,$E4,$E7,$EB,$EF,$F3,$F5,$0F,$51,$D7,$A0 ; $46E6
    .byte $AE,$03,$A1,$8A,$C2,$4D,$2F,$6C,$0C,$16,$90,$86,$00,$02,$04,$06 ; $46F6
    .byte $08,$0B,$0D,$0F,$11,$13,$15,$17,$1A,$1C,$1E,$20,$21,$23,$25,$27 ; $4706
    .byte $29,$2A,$2C,$2E,$2F,$31,$32,$33,$35,$36,$37,$38,$39,$3A,$3B,$3C ; $4716
    .byte $3C,$3D,$3E,$3E                                  ; $4726
    :5 .byte $3F                    ; $472A (5 bytes)
    .byte $00,$3C,$77,$B1,$E8,$1D,$4E,$7C,$A4,$C7,$E4,$FA,$08,$0E,$0C,$00 ; $472F
    .byte $EA,$CA,$9E,$67,$23,$D3,$75,$0A,$90,$07,$6F,$C7,$0F,$46,$6D,$82 ; $473F
    .byte $86,$78,$57,$24,$DE,$85,$19,$9A,$07,$61,$A6,$D8,$F6,$40 ; $474F
    :5 .byte $3F                    ; $475D (5 bytes)
    .byte $3E,$3E,$3D,$3C,$3C,$3B,$3A,$39,$38,$37,$36,$35,$33,$32,$31,$2F ; $4762
    .byte $2E,$2C,$2A,$29,$27,$25,$23,$21,$20,$1E,$1C,$1A,$17,$15,$13,$11 ; $4772
    .byte $0F,$0D,$0B,$08,$06,$04,$02,$00,$F6,$D8,$A6,$61,$07,$9A,$19,$85 ; $4782
    .byte $DE,$24,$57,$78,$86,$82,$6D,$46,$0F,$C7,$6F,$07,$90,$0A,$75,$D3 ; $4792
    .byte $23,$67,$9E,$CA,$EA,$00,$0C,$0E,$08,$FA,$E4,$C7,$A4,$7C,$4E,$1D ; $47A2
    .byte $E8,$B1,$77,$3C,$00,$07,$0E,$16,$1D,$24,$2C,$33,$3A,$41,$48,$4F ; $47B2
    .byte $56,$5D,$64,$6A,$70,$77,$7D,$83,$88,$8E,$93,$99,$9E,$A3,$A7,$AC ; $47C2
    .byte $B0,$B4,$B8,$BC,$BF,$C2,$C5,$C8,$CA,$CC,$CE,$D0,$D1,$D2,$D3,$D4 ; $47D2
    .byte $D4,$00,$6F,$DC,$44,$A6,$FE,$4A,$89,$B8,$D4,$DC,$CD,$A5,$63,$03 ; $47E2
    .byte $84,$E3,$20,$37,$27,$EE,$8B,$FB,$3D,$4F,$30,$DE,$58,$9C,$A8,$7D ; $47F2
    .byte $18,$78,$9C,$84,$2E,$9A,$C7,$B3,$5F,$CB,$F4,$DC,$82,$E6,$D5,$D4 ; $4802
    .byte $D4,$D3,$D2,$D1,$D0,$CE,$CC,$CA,$C8,$C5,$C2,$BF,$BC,$B8,$B4,$B0 ; $4812
    .byte $AC,$A7,$A3,$9E,$99,$93,$8E,$88,$83,$7D,$77,$70,$6A,$64,$5D,$56 ; $4822
    .byte $4F,$48,$41,$3A,$33,$2C,$24,$1D,$16,$0E,$07,$07,$E6,$82,$DC,$F4 ; $4832
    .byte $CB,$5F,$B3,$C7,$9A,$2E,$84,$9C,$78,$18,$7D,$A8,$9C,$58,$DE,$30 ; $4842
    .byte $4F,$3D,$FB,$8B,$EE,$27,$37,$20,$E3,$84,$03,$63,$A5,$CD,$DC,$D4 ; $4852
    .byte $B8,$89,$4A,$FE,$A6,$44,$DC,$6F,$00,$01,$02,$03,$04,$05,$06,$07 ; $4862
    .byte $08,$09,$0A,$0B,$0C,$0D,$0E,$0F,$10,$11,$12,$13,$14,$15,$16,$15 ; $4872
    .byte $14,$13,$12,$11,$10,$0F,$0E,$0D,$0C,$0B,$0A,$09,$08,$07,$06,$05 ; $4882
    .byte $04,$03,$02,$01,$00                              ; $4892
    :23 .byte $03                    ; $4897 (23 bytes)
    :22 .byte $83                    ; $48AE (22 bytes)
    :23 .byte $81                    ; $48C4 (23 bytes)
    :22 .byte $01                    ; $48DB (22 bytes)
    :23 .byte $00                    ; $48F1 (23 zero bytes)
    :22 .byte $80                    ; $4908 (22 bytes)
    :23 .byte $82                    ; $491E (23 bytes)
    :22 .byte $02                    ; $4935 (22 bytes)
    .byte $00,$2D,$5A,$87                                  ; $494B
    :23 .byte $00                    ; $494F (23 zero bytes)
    .byte $15,$14,$13,$12,$11,$10,$0F,$0E,$0D,$0C,$0B,$0A,$09,$08,$07,$06 ; $4966
    .byte $05,$04,$03,$02,$01,$00                          ; $4976
    :10 .byte $16                    ; $497C (10 bytes)
    .byte $15,$14,$13,$12,$11,$10,$11,$12,$13,$14,$15,$16,$17 ; $4986
    :22 .byte $16                    ; $4993 (22 bytes)
    .byte $0E,$0F,$10,$11,$12,$13,$14,$15,$16              ; $49A9
    :14 .byte $FF                    ; $49B2 (14 bytes)
    :9 .byte $00                    ; $49C0 (9 zero bytes)
    .byte $01,$02,$03,$04,$05,$06,$07,$08,$09,$0A,$0B,$0C,$0D ; $49C9
    :9 .byte $17                    ; $49D6 (9 bytes)
    :14 .byte $00                    ; $49DF (14 zero bytes)
    :22 .byte $17                    ; $49ED (22 bytes)
    .byte $C0,$40,$00,$80,$FF,$00,$FF,$00,$00,$FF,$00,$FF,$39,$13,$0B,$08 ; $4A03
    .byte $06,$05,$04,$03,$03                              ; $4A13
    :4 .byte $02                    ; $4A18 (4 bytes)
    :10 .byte $01                    ; $4A1C (10 bytes)
    .byte $4A,$14,$6E,$24,$50,$25,$54,$BB,$45,$E7,$9A,$5B,$24,$F6,$CD,$AA ; $4A26
    .byte $8A,$6D,$53,$3C,$26,$12,$00,$3B,$C5,$18,$F4,$52,$01,$DC,$68,$57 ; $4A36
    .byte $7A,$E7,$19,$FE,$6E,$D6,$0E,$35,$9B,$B9,$22,$7F,$87 ; $4A46
    :23 .byte $00                    ; $4A53 (23 zero bytes)
    .byte $01,$04,$0D,$16,$1F,$28,$31,$3B,$44,$4E,$58,$62,$6C,$77,$82,$8D ; $4A6A
    .byte $99,$A6,$B3,$C0,$CF,$DE,$EE,$00,$78,$6B,$66,$6F,$8C,$C3,$1A,$98 ; $4A7A
    .byte $44,$26,$45,$AA,$60,$70,$E7,$D2,$40,$41,$E9,$4E,$8A,$B9,$00,$39 ; $4A8A
    .byte $13,$0B,$08,$06,$05,$04,$03,$03,$03              ; $4A9A
    :5 .byte $02                    ; $4AA3 (5 bytes)
    :8 .byte $01                    ; $4AA8 (8 bytes)
    .byte $4C,$1B,$79,$35,$64,$3E,$72,$DD,$6C,$12,$CA,$8F,$5E,$34,$10,$F1 ; $4AB0
    .byte $D6,$BE,$A9,$97,$86,$77,$6A,$77,$7A,$45,$9C,$78,$A8,$06,$1C,$99 ; $4AC0
    .byte $51,$59,$2F,$BF,$E3,$0B,$0D,$09,$53,$61,$CA,$35,$5E,$0A ; $4AD0
    :23 .byte $01                    ; $4ADE (23 bytes)
    .byte $00,$00,$01,$02,$03,$05,$07,$09,$0C,$0F,$12,$16,$1A,$1F,$25,$2B ; $4AF5
    .byte $31,$39,$41,$49,$53,$5E,$6A,$0A,$5A,$FA,$EC,$31,$CB,$BC,$08,$B2 ; $4B05
    .byte $C0,$37,$1C,$77,$51,$B3,$A8,$3F,$85,$8C,$69,$34,$09,$0A ; $4B15
    :276 .byte $00                    ; $4B23 (276 zero bytes)
perspective_project:
    ; PERSPECTIVE PROJECT — compute screen column heights from ray distances
    .byte $A5,$6D,$F0,$5F                                  ; $4C37
    ; SELF-MODIFYING: negate delta for subtraction, patch inline operands
    .byte $49,$FF,$18,$69,$01,$8D,$61,$4C,$8D,$67,$4C,$8D,$74,$4C,$8D,$7D ; $4C3B
    .byte $4C,$49,$FF,$8D,$64,$4C,$8D,$6A,$4C,$8D,$77,$4C,$8D,$80,$4C ; $4C4B
    ; Loop over ray steps: compute wall/floor heights using $C000-$C6FF tables (RAM under ROM)
    .byte $A6,$6F                                          ; $4C5A
loc_4C5C:
    .byte $BC,$B0,$4A,$38,$B9,$00,$C0,$F9,$00,$C4,$B9,$00,$C2,$F9,$00,$C6 ; $4C5C
    .byte $8D,$87,$4C,$BC,$99,$4A,$38,$B9,$00,$C0,$F9,$00,$C4,$8D,$89,$4C ; $4C6C
    .byte $B9,$00,$C2,$F9,$00,$C6,$9D,$AD,$4B,$18,$A9,$FF,$69,$FF,$9D,$C4 ; $4C7C
    .byte $4B,$90,$03,$FE,$AD,$4B                          ; $4C8C
loc_4C92:
    .byte $E8,$E4,$70,$90,$C5,$4C,$AD,$4C                  ; $4C92
loc_4C9A:
    .byte $A4,$6F                                          ; $4C9A
loc_4C9C:
    .byte $B9,$99,$4A,$99,$AD,$4B,$B9,$B0,$4A,$99,$C4,$4B,$C8,$C4,$70,$90 ; $4C9C
    .byte $EF                                              ; $4CAC
loc_4CAD:
    ; Process vertical extent (floor-to-ceiling)
    .byte $A5,$6E,$F0,$3C,$49,$FF,$18,$69,$01,$85,$05,$8D,$CD,$4C,$8D,$D3 ; $4CAD
    .byte $4C,$49,$FF,$8D,$D0,$4C,$8D,$D6,$4C,$A6,$6F      ; $4CBD
loc_4CC8:
    .byte $BC,$F5,$4A,$38,$B9,$00,$C0,$F9,$00,$C4,$B9,$00,$C2,$F9,$00,$C6 ; $4CC8
    .byte $18,$65,$05,$9D,$09,$4C,$A9,$00,$69,$00,$9D,$F2,$4B,$E8,$E4,$70 ; $4CD8
    .byte $90,$DE,$4C,$00,$4D                              ; $4CE8
loc_4CED:
    .byte $A4,$6F                                          ; $4CED
loc_4CEF:
    .byte $B9,$DE,$4A,$99,$F2,$4B,$B9,$F5,$4A,$99,$09,$4C,$C8,$C4,$70,$90 ; $4CEF
    .byte $EF                                              ; $4CFF
loc_4D00:
    .byte $A5,$6D,$F0,$5F,$49,$FF,$18,$69,$01,$8D,$2A,$4D,$8D,$30,$4D,$8D ; $4D00
    .byte $3D,$4D,$8D,$46,$4D,$49,$FF,$8D,$2D,$4D,$8D,$33,$4D,$8D,$40,$4D ; $4D10
    .byte $8D,$49,$4D,$A6,$6F                              ; $4D20
loc_4D25:
    .byte $BC,$26,$4A,$38,$B9,$00,$C0,$F9,$00,$C4,$B9,$00,$C2,$F9,$00,$C6 ; $4D25
    .byte $8D,$50,$4D,$BC,$0F,$4A,$38,$B9,$00,$C0,$F9,$00,$C4,$8D,$52,$4D ; $4D35
    .byte $B9,$00,$C2,$F9,$00,$C6,$9D,$23,$4B,$18,$A9,$FF,$69,$FF,$9D,$3A ; $4D45
    .byte $4B,$90,$03,$FE,$23,$4B                          ; $4D55
loc_4D5B:
    .byte $E8,$E4,$70,$90,$C5,$4C,$76,$4D                  ; $4D5B
loc_4D63:
    .byte $A4,$6F                                          ; $4D63
loc_4D65:
    .byte $B9,$0F,$4A,$99,$23,$4B,$B9,$26,$4A,$99,$3A,$4B,$C8,$C4,$70,$90 ; $4D65
    .byte $EF                                              ; $4D75
loc_4D76:
    .byte $A5,$6E,$F0,$40,$49,$FF,$18,$69,$01,$8D,$95,$4B,$A2,$16,$E4,$6F ; $4D76
    .byte $F0,$3F,$E8,$E4,$70,$D0,$02,$C6,$70              ; $4D86
loc_4D8F:
    .byte $8D,$A4,$4D,$8D,$AA,$4D,$49,$FF,$8D,$A7,$4D,$8D,$AD,$4D,$A6,$6F ; $4D8F
loc_4D9F:
    .byte $BC,$6B,$4A,$38,$B9,$00,$C0,$F9,$00,$C4,$B9,$00,$C2,$F9,$00,$C6 ; $4D9F
    .byte $9D,$7F,$4B,$E8,$E4,$70,$90,$E8,$4C,$C7,$4D      ; $4DAF
loc_4DBA:
    .byte $A4,$6F                                          ; $4DBA
loc_4DBC:
    .byte $B9,$6B,$4A,$99,$7F,$4B,$C8,$C4,$70,$90,$F5      ; $4DBC
loc_4DC7:
    .byte $60                                              ; $4DC7
sub_4DC8:
    ; Column clear/draw routine: writes to framebuffer at $D800+ (RAM under ROM)
    .byte $A9,$99,$99,$00,$D8,$A9,$EE,$99,$40,$D8,$A9,$00,$99,$80,$F7,$A9 ; $4DC8
    .byte $55,$99,$C0,$F7,$A9,$99,$99,$80,$D8,$A9,$EE,$99,$C0,$D8,$A9,$00 ; $4DD8
    .byte $99,$00,$F7,$A9,$55,$99,$40,$F7,$A9,$99,$99,$00,$D9,$A9,$EE,$99 ; $4DE8
    .byte $40,$D9,$A9,$00,$99,$80,$F6,$A9,$55,$99,$C0,$F6,$A9,$99,$99,$80 ; $4DF8
    .byte $D9,$A9,$EE,$99,$C0,$D9,$A9,$00,$99,$00,$F6,$A9,$55,$99,$40,$F6 ; $4E08
    .byte $A9,$99,$99,$00,$DA,$A9,$EE,$99,$40,$DA,$A9,$00,$99,$80,$F5,$A9 ; $4E18
    .byte $55,$99,$C0,$F5,$A9,$99,$99,$80,$DA,$A9,$EE,$99,$C0,$DA,$A9,$00 ; $4E28
    .byte $99,$00,$F5,$A9,$55,$99,$40,$F5,$A9,$99,$99,$00,$DB,$A9,$EE,$99 ; $4E38
    .byte $40,$DB,$A9,$00,$99,$80,$F4,$A9,$55,$99,$C0,$F4,$A9,$99,$99,$80 ; $4E48
    .byte $DB,$A9,$EE,$99,$C0,$DB,$A9,$00,$99,$00,$F4,$A9,$55,$99,$40,$F4 ; $4E58
    .byte $A9,$99,$99,$00,$DC,$A9,$EE,$99,$40,$DC,$A9,$00,$99,$80,$F3,$A9 ; $4E68
    .byte $55,$99,$C0,$F3,$A9,$99,$99,$80,$DC,$A9,$EE,$99,$C0,$DC,$A9,$00 ; $4E78
    .byte $99,$00,$F3,$A9,$55,$99,$40,$F3,$A9,$99,$99,$00,$DD,$A9,$EE,$99 ; $4E88
    .byte $40,$DD,$A9,$00,$99,$80,$F2,$A9,$55,$99,$C0,$F2,$A9,$99,$99,$80 ; $4E98
    .byte $DD,$A9,$EE,$99,$C0,$DD,$A9,$00,$99,$00,$F2,$A9,$55,$99,$40,$F2 ; $4EA8
    .byte $A9,$99,$99,$00,$DE,$A9,$EE,$99,$40,$DE,$A9,$00,$99,$80,$F1,$A9 ; $4EB8
    .byte $55,$99,$C0,$F1,$A9,$99,$99,$80,$DE,$A9,$EE,$99,$C0,$DE,$A9,$00 ; $4EC8
    .byte $99,$00,$F1,$A9,$55,$99,$40,$F1,$A9,$99,$99,$00,$DF,$A9,$EE,$99 ; $4ED8
    .byte $40,$DF,$A9,$00,$99,$80,$F0,$A9,$55,$99,$C0,$F0,$A9,$99,$99,$80 ; $4EE8
    .byte $DF,$A9,$EE,$99,$C0,$DF,$A9,$00,$99,$00,$F0,$A9,$55,$99,$40,$F0 ; $4EF8
    .byte $A9,$99,$99,$00,$E0,$A9,$EE,$99,$40,$E0,$A9,$00,$99,$80,$EF,$A9 ; $4F08
    .byte $55,$99,$C0,$EF,$A9,$99,$99,$80,$E0,$A9,$EE,$99,$C0,$E0,$A9,$00 ; $4F18
    .byte $99,$00,$EF,$A9,$55,$99,$40,$EF,$A9,$99,$99,$00,$E1,$A9,$DD,$99 ; $4F28
    .byte $40,$E1,$A9,$00,$99,$80,$EE,$A9,$55,$99,$C0,$EE,$A9,$99,$99,$80 ; $4F38
    .byte $E1,$A9,$DD,$99,$C0,$E1,$A9,$00,$99,$00,$EE,$A9,$55,$99,$40,$EE ; $4F48
    .byte $A9,$99,$99,$00,$E2,$A9,$DD,$99,$40,$E2,$A9,$00,$99,$80,$ED,$A9 ; $4F58
    .byte $55,$99,$C0,$ED,$A9,$99,$99,$80,$E2,$A9,$DD,$99,$C0,$E2,$A9,$00 ; $4F68
    .byte $99,$00,$ED,$A9,$55,$99,$40,$ED,$A9,$99,$99,$00,$E3,$A9,$DD,$99 ; $4F78
    .byte $40,$E3,$A9,$00,$99,$80,$EC,$A9,$55,$99,$C0,$EC,$A9,$99,$99,$80 ; $4F88
    .byte $E3,$A9,$DD,$99,$C0,$E3,$A9,$00,$99,$00,$EC,$A9,$55,$99,$40,$EC ; $4F98
    .byte $A9,$99,$99,$00,$E4,$A9,$DD,$99,$40,$E4,$A9,$00,$99,$80,$EB,$A9 ; $4FA8
    .byte $55,$99,$C0,$EB,$A9,$99,$99,$80,$E4,$A9,$DD,$99,$C0,$E4,$A9,$00 ; $4FB8
    .byte $99,$00,$EB,$A9,$55,$99,$40,$EB,$A9,$99,$99,$00,$E5,$A9,$DD,$99 ; $4FC8
    .byte $40,$E5,$A9,$00,$99,$80,$EA,$A9,$55,$99,$C0,$EA,$A9,$99,$99,$80 ; $4FD8
    .byte $E5,$A9,$DD,$99,$C0,$E5,$A9,$00,$99,$00,$EA,$A9,$55,$99,$40,$EA ; $4FE8
    .byte $A9,$99,$99,$00,$E6,$A9,$CC,$99,$40,$E6,$A9,$00,$99,$80,$E9,$A9 ; $4FF8
    .byte $44,$99,$C0,$E9,$A9,$99,$99,$80,$E6,$A9,$CC,$99,$C0,$E6,$A9,$00 ; $5008
    .byte $99,$00,$E9,$A9,$44,$99,$40,$E9,$A9,$99,$99,$00,$E7,$A9,$CC,$99 ; $5018
    .byte $40,$E7,$A9,$00,$99,$80,$E8,$A9,$44,$99,$C0,$E8,$A9,$99,$99,$80 ; $5028
    .byte $E7,$A9,$CC,$99,$C0,$E7,$A9,$00,$99,$00,$E8,$A9,$44,$99,$40,$E8 ; $5038
    .byte $60,$C8,$DC,$F0,$04,$18,$2C,$40,$54,$68,$7C,$90,$A4,$B8,$CC,$E0 ; $5048
    .byte $F4,$08,$1C,$30,$44,$58,$6C,$80,$94,$A8,$BC,$D0,$E4,$F8,$0C,$20 ; $5058
    .byte $34,$48,$4D,$4D,$4D                              ; $5068
    :13 .byte $4E                    ; $506D (13 bytes)
    :13 .byte $4F                    ; $507A (13 bytes)
    :4 .byte $50                    ; $5087 (4 bytes)
loc_508B:
    .byte $A9,$C8,$85,$08,$A9,$4D,$85,$09,$A2,$20          ; $508B
loc_5095:
    .byte $A0,$01,$A9,$00,$91,$08                          ; $5095
init_data_for_f800:
    .byte $A0,$06,$A9,$00,$91,$08,$A0,$0B,$A9,$00,$91,$08,$A0,$10,$A9,$00 ; $509B
    .byte $91,$08,$A5,$08,$18,$69,$14,$90,$02,$E6,$09      ; $50AB
loc_50B6:
    .byte $85,$08,$CA,$D0,$DA,$60                          ; $50B6
draw_sky_floor:
    ; DRAW SKY/FLOOR — fill gradient above/below wall for one column
    .byte $A4,$0A,$A5,$0E,$F0,$05,$A9,$00,$4C,$CD,$50      ; $50BC
loc_50C7:
    .byte $B1,$1E,$C5,$0C,$B0,$31                          ; $50C7
loc_50CD:
    ; Compute gradient address from column position
    .byte $A8,$B9,$49,$50,$8D,$F6,$50,$B9,$6A,$50,$8D,$F7,$50,$A4,$0C,$B9 ; $50CD
    .byte $49,$50,$85,$06,$B9,$6A,$50,$85,$07,$A0,$00,$A9,$60,$91,$06,$A5 ; $50DD
    .byte $0A                                              ; $50ED
    ; BIT $12 — test buffer flag to select $D800 (buf 0) or $D820 (buf 1) offset
    .byte $24,$12,$10,$02,$09,$20                          ; $50EE
loc_50F4:
    .byte $A8                                              ; $50F4
    ; Call column clear routine at $4DC8
    .byte $20,$FF,$FF,$A0,$00,$A9,$A9,$91,$06              ; $50F5
loc_50FE:
    .byte $60,$99,$00,$D8,$99,$80,$F7,$99,$80,$D8,$99,$00,$F7,$99,$00,$D9 ; $50FE
    .byte $99,$80,$F6,$99,$80,$D9,$99,$00,$F6,$99,$00,$DA,$99,$80,$F5,$99 ; $510E
    .byte $80,$DA,$99,$00,$F5,$99,$00,$DB,$99,$80,$F4,$99,$80,$DB,$99,$00 ; $511E
    .byte $F4,$99,$00,$DC,$99,$80,$F3,$99,$80,$DC,$99,$00,$F3,$99,$00,$DD ; $512E
    .byte $99,$80,$F2,$99,$80,$DD,$99,$00,$F2,$99,$00,$DE,$99,$80,$F1,$99 ; $513E
    .byte $80,$DE,$99,$00,$F1,$99,$00,$DF,$99,$80,$F0,$99,$80,$DF,$99,$00 ; $514E
    .byte $F0,$99,$00,$E0,$99,$80,$EF,$99,$80,$E0,$99,$00,$EF,$99,$00,$E1 ; $515E
    .byte $99,$80,$EE,$99,$80,$E1,$99,$00,$EE,$99,$00,$E2,$99,$80,$ED,$99 ; $516E
    .byte $80,$E2,$99,$00,$ED,$99,$00,$E3,$99,$80,$EC,$99,$80,$E3,$99,$00 ; $517E
    .byte $EC,$99,$00,$E4,$99,$80,$EB,$99,$80,$E4,$99,$00,$EB,$99,$00,$E5 ; $518E
    .byte $99,$80,$EA,$99,$80,$E5,$99,$00,$EA,$99,$00,$E6,$99,$80,$E9,$99 ; $519E
    .byte $80,$E6,$99,$00,$E9,$99,$00,$E7,$99,$80,$E8,$99,$80,$E7,$99,$00 ; $51AE
    .byte $E8,$60,$FF,$05,$0B,$11,$17,$1D,$23,$29,$2F,$35,$3B,$41,$47,$4D ; $51BE
    .byte $53,$59,$5F,$65,$6B,$71,$77,$7D,$83,$89,$8F,$95,$9B,$A1,$A7,$AD ; $51CE
    .byte $B3,$B9,$50                                      ; $51DE
    :31 .byte $51                    ; $51E1 (31 bytes)
draw_texture_col:
    ; DRAW TEXTURE COLUMN — renders one vertical strip of textured wall
    .byte $A4,$0A,$A5,$0B                                  ; $5200
    ; Compare wall heights to determine visible extent
    .byte $D1,$22,$D0,$44,$A5,$0C,$D1,$1E,$90,$01,$60      ; $5204
loc_520F:
    .byte $B1,$1E,$C9,$20,$F0,$37                          ; $520F
    ; SELF-MODIFYING: patch JSR target at $5242 with texture routine address
    .byte $85,$0D,$A4,$0C,$B9,$C0,$51,$8D,$43,$52,$B9,$E0,$51,$8D,$44,$52 ; $5215
    .byte $A4,$0D,$B9,$C0,$51,$85,$06,$B9,$E0,$51,$85,$07  ; $5225
    ; Write RTS ($60) to texture entry, call it, then write STA ($99) — trick for indirect texture dispatch
    .byte $A9,$60,$A0,$00,$91,$06,$A5,$0A,$24,$12,$10,$02,$09,$20 ; $5231
loc_523F:
    .byte $A8,$A5,$0B                                      ; $523F
    ; SELF-MODIFIED JSR — jumps to per-texture column drawing routine
    .byte $20,$FF,$FF,$A9,$99,$A0,$00,$91,$06,$60          ; $5242
loc_524C:
    .byte $A4,$0C,$B9,$C0,$51,$85,$06,$B9,$E0,$51,$85,$07,$A5,$0A,$24,$12 ; $524C
    .byte $10,$02,$09,$20                                  ; $525C
loc_5260:
    .byte $A8,$A5,$0B,$6C,$06,$00,$BD,$00,$A4,$99,$40,$D8,$99,$C0,$D8,$99 ; $5260
    .byte $40,$D9,$99,$C0,$D9,$BD,$00,$A5,$99,$40,$DA,$99,$C0,$DA,$99,$40 ; $5270
    .byte $DB,$99,$C0,$DB,$BD,$00,$A6,$99,$40,$DC,$99,$C0,$DC,$99,$40,$DD ; $5280
    .byte $99,$C0,$DD,$BD,$00,$A7,$99,$40,$DE,$99,$C0,$DE,$99,$40,$DF,$99 ; $5290
    .byte $C0,$DF,$BD,$00,$A8,$99,$40,$E0,$99,$C0,$E0,$99,$40,$E1,$99,$C0 ; $52A0
    .byte $E1,$BD,$00,$A9,$99,$40,$E2,$99,$C0,$E2,$99,$40,$E3,$99,$C0,$E3 ; $52B0
    .byte $BD,$00,$AA,$99,$40,$E4,$99,$C0,$E4,$99,$40,$E5,$99,$C0,$E5,$BD ; $52C0
    .byte $00,$AB,$99,$40,$E6,$99,$C0,$E6,$99,$40,$E7,$99,$C0,$E7,$BD,$00 ; $52D0
    .byte $AC,$99,$40,$E8,$99,$C0,$E8,$99,$40,$E9,$99,$C0,$E9,$BD,$00,$AD ; $52E0
    .byte $99,$40,$EA,$99,$C0,$EA,$99,$40,$EB,$99,$C0,$EB,$BD,$00,$AE,$99 ; $52F0
    .byte $40,$EC,$99,$C0,$EC,$99,$40,$ED,$99,$C0,$ED,$BD,$00,$AF,$99,$40 ; $5300
    .byte $EE,$99,$C0,$EE,$99,$40,$EF,$99,$C0,$EF,$BD,$00,$B0,$99,$40,$F0 ; $5310
    .byte $99,$C0,$F0,$99,$40,$F1,$99,$C0,$F1,$BD,$00,$B1,$99,$40,$F2,$99 ; $5320
    .byte $C0,$F2,$99,$40,$F3,$99,$C0,$F3,$BD,$00,$B2,$99,$40,$F4,$99,$C0 ; $5330
    .byte $F4,$99,$40,$F5,$99,$C0,$F5,$BD,$00,$B3,$99,$40,$F6,$99,$C0,$F6 ; $5340
    .byte $99,$40,$F7,$99,$C0,$F7,$60,$BD,$00,$A3,$99,$40,$D8,$BD,$00,$A4 ; $5350
    .byte $99,$C0,$D8,$99,$40,$D9,$99,$C0,$D9,$99,$40,$DA,$BD,$00,$A5,$99 ; $5360
    .byte $C0,$DA,$99,$40,$DB,$99,$C0,$DB,$99,$40,$DC,$BD,$00,$A6,$99,$C0 ; $5370
    .byte $DC,$99,$40,$DD,$99,$C0,$DD,$99,$40,$DE,$BD,$00,$A7,$99,$C0,$DE ; $5380
    .byte $99,$40,$DF,$99,$C0,$DF,$BD,$00,$A8,$99,$40,$E0,$99,$C0,$E0,$99 ; $5390
    .byte $40,$E1,$99,$C0,$E1,$BD,$00,$A9,$99,$40,$E2,$99,$C0,$E2,$99,$40 ; $53A0
    .byte $E3,$99,$C0,$E3,$BD,$00,$AA,$99,$40,$E4,$99,$C0,$E4,$99,$40,$E5 ; $53B0
    .byte $99,$C0,$E5,$BD,$00,$AB,$99,$40,$E6,$99,$C0,$E6,$99,$40,$E7,$99 ; $53C0
    .byte $C0,$E7,$BD,$00,$AC,$99,$40,$E8,$99,$C0,$E8,$99,$40,$E9,$99,$C0 ; $53D0
    .byte $E9,$BD,$00,$AD,$99,$40,$EA,$99,$C0,$EA,$99,$40,$EB,$99,$C0,$EB ; $53E0
    .byte $BD,$00,$AE,$99,$40,$EC,$99,$C0,$EC,$99,$40,$ED,$99,$C0,$ED,$BD ; $53F0
    .byte $00,$AF,$99,$40,$EE,$99,$C0,$EE,$99,$40,$EF,$99,$C0,$EF,$BD,$00 ; $5400
    .byte $B0,$99,$40,$F0,$99,$C0,$F0,$99,$40,$F1,$BD,$00,$B1,$99,$C0,$F1 ; $5410
    .byte $99,$40,$F2,$99,$C0,$F2,$99,$40,$F3,$BD,$00,$B2,$99,$C0,$F3,$99 ; $5420
    .byte $40,$F4,$99,$C0,$F4,$99,$40,$F5,$BD,$00,$B3,$99,$C0,$F5,$99,$40 ; $5430
    .byte $F6,$99,$C0,$F6,$99,$40,$F7,$BD,$00,$B4,$99,$C0,$F7,$60,$BD,$00 ; $5440
    .byte $A3,$99,$40,$D8,$99,$C0,$D8,$BD,$00,$A4,$99,$40,$D9,$99,$C0,$D9 ; $5450
    .byte $99,$40,$DA,$99,$C0,$DA,$BD,$00,$A5,$99,$40,$DB,$99,$C0,$DB,$99 ; $5460
    .byte $40,$DC,$BD,$00,$A6,$99,$C0,$DC,$99,$40,$DD,$99,$C0,$DD,$99,$40 ; $5470
    .byte $DE,$BD,$00,$A7,$99,$C0,$DE,$99,$40,$DF,$99,$C0,$DF,$99,$40,$E0 ; $5480
    .byte $BD,$00,$A8,$99,$C0,$E0,$99,$40,$E1,$99,$C0,$E1,$99,$40,$E2,$BD ; $5490
    .byte $00,$A9,$99,$C0,$E2,$99,$40,$E3,$99,$C0,$E3,$BD,$00,$AA,$99,$40 ; $54A0
    .byte $E4,$99,$C0,$E4,$99,$40,$E5,$99,$C0,$E5,$BD,$00,$AB,$99,$40,$E6 ; $54B0
    .byte $99,$C0,$E6,$99,$40,$E7,$99,$C0,$E7,$BD,$00,$AC,$99,$40,$E8,$99 ; $54C0
    .byte $C0,$E8,$99,$40,$E9,$99,$C0,$E9,$BD,$00,$AD,$99,$40,$EA,$99,$C0 ; $54D0
    .byte $EA,$99,$40,$EB,$99,$C0,$EB,$BD,$00,$AE,$99,$40,$EC,$99,$C0,$EC ; $54E0
    .byte $99,$40,$ED,$BD,$00,$AF,$99,$C0,$ED,$99,$40,$EE,$99,$C0,$EE,$99 ; $54F0
    .byte $40,$EF,$BD,$00,$B0,$99,$C0,$EF,$99,$40,$F0,$99,$C0,$F0,$99,$40 ; $5500
    .byte $F1,$BD,$00,$B1,$99,$C0,$F1,$99,$40,$F2,$99,$C0,$F2,$99,$40,$F3 ; $5510
    .byte $BD,$00,$B2,$99,$C0,$F3,$99,$40,$F4,$99,$C0,$F4,$BD,$00,$B3,$99 ; $5520
    .byte $40,$F5,$99,$C0,$F5,$99,$40,$F6,$99,$C0,$F6,$BD,$00,$B4,$99,$40 ; $5530
    .byte $F7,$99,$C0,$F7,$60,$BD,$00,$A3,$99,$40,$D8,$99,$C0,$D8,$99,$40 ; $5540
    .byte $D9,$BD,$00,$A4,$99,$C0,$D9,$99,$40,$DA,$99,$C0,$DA,$BD,$00,$A5 ; $5550
    .byte $99,$40,$DB,$99,$C0,$DB,$99,$40,$DC,$99,$C0,$DC,$BD,$00,$A6,$99 ; $5560
    .byte $40,$DD,$99,$C0,$DD,$99,$40,$DE,$99,$C0,$DE,$BD,$00,$A7,$99,$40 ; $5570
    .byte $DF,$99,$C0,$DF,$99,$40,$E0,$BD,$00,$A8,$99,$C0,$E0,$99,$40,$E1 ; $5580
    .byte $99,$C0,$E1,$99,$40,$E2,$BD,$00,$A9,$99,$C0,$E2,$99,$40,$E3,$99 ; $5590
    .byte $C0,$E3,$99,$40,$E4,$BD,$00,$AA,$99,$C0,$E4,$99,$40,$E5,$99,$C0 ; $55A0
    .byte $E5,$BD,$00,$AB,$99,$40,$E6,$99,$C0,$E6,$99,$40,$E7,$99,$C0,$E7 ; $55B0
    .byte $BD,$00,$AC,$99,$40,$E8,$99,$C0,$E8,$99,$40,$E9,$99,$C0,$E9,$BD ; $55C0
    .byte $00,$AD,$99,$40,$EA,$99,$C0,$EA,$99,$40,$EB,$BD,$00,$AE,$99,$C0 ; $55D0
    .byte $EB,$99,$40,$EC,$99,$C0,$EC,$99,$40,$ED,$BD,$00,$AF,$99,$C0,$ED ; $55E0
    .byte $99,$40,$EE,$99,$C0,$EE,$99,$40,$EF,$BD,$00,$B0,$99,$C0,$EF,$99 ; $55F0
    .byte $40,$F0,$99,$C0,$F0,$BD,$00,$B1,$99,$40,$F1,$99,$C0,$F1,$99,$40 ; $5600
    .byte $F2,$99,$C0,$F2,$BD,$00,$B2,$99,$40,$F3,$99,$C0,$F3,$99,$40,$F4 ; $5610
    .byte $99,$C0,$F4,$BD,$00,$B3,$99,$40,$F5,$99,$C0,$F5,$99,$40,$F6,$BD ; $5620
    .byte $00,$B4,$99,$C0,$F6,$99,$40,$F7,$99,$C0,$F7,$60,$BD,$00,$A3,$99 ; $5630
    .byte $40,$D8,$99,$C0,$D8,$99,$40,$D9,$99,$C0,$D9,$BD,$00,$A4,$99,$40 ; $5640
    .byte $DA,$99,$C0,$DA,$99,$40,$DB,$BD,$00,$A5,$99,$C0,$DB,$99,$40,$DC ; $5650
    .byte $99,$C0,$DC,$99,$40,$DD,$BD,$00,$A6,$99,$C0,$DD,$99,$40,$DE,$99 ; $5660
    .byte $C0,$DE,$BD,$00,$A7,$99,$40,$DF,$99,$C0,$DF,$99,$40,$E0,$99,$C0 ; $5670
    .byte $E0,$BD,$00,$A8,$99,$40,$E1,$99,$C0,$E1,$99,$40,$E2,$BD,$00,$A9 ; $5680
    .byte $99,$C0,$E2,$99,$40,$E3,$99,$C0,$E3,$99,$40,$E4,$BD,$00,$AA,$99 ; $5690
    .byte $C0,$E4,$99,$40,$E5,$99,$C0,$E5,$BD,$00,$AB,$99,$40,$E6,$99,$C0 ; $56A0
    .byte $E6,$99,$40,$E7,$99,$C0,$E7,$BD,$00,$AC,$99,$40,$E8,$99,$C0,$E8 ; $56B0
    .byte $99,$40,$E9,$99,$C0,$E9,$BD,$00,$AD,$99,$40,$EA,$99,$C0,$EA,$99 ; $56C0
    .byte $40,$EB,$BD,$00,$AE,$99,$C0,$EB,$99,$40,$EC,$99,$C0,$EC,$99,$40 ; $56D0
    .byte $ED,$BD,$00,$AF,$99,$C0,$ED,$99,$40,$EE,$99,$C0,$EE,$BD,$00,$B0 ; $56E0
    .byte $99,$40,$EF,$99,$C0,$EF,$99,$40,$F0,$99,$C0,$F0,$BD,$00,$B1,$99 ; $56F0
    .byte $40,$F1,$99,$C0,$F1,$99,$40,$F2,$BD,$00,$B2,$99,$C0,$F2,$99,$40 ; $5700
    .byte $F3,$99,$C0,$F3,$99,$40,$F4,$BD,$00,$B3,$99,$C0,$F4,$99,$40,$F5 ; $5710
    .byte $99,$C0,$F5,$BD,$00,$B4,$99,$40,$F6,$99,$C0,$F6,$99,$40,$F7,$99 ; $5720
    .byte $C0,$F7,$60,$BD,$00,$A2,$99,$40,$D8,$BD,$00,$A3,$99,$C0,$D8,$99 ; $5730
    .byte $40,$D9,$99,$C0,$D9,$BD,$00,$A4,$99,$40,$DA,$99,$C0,$DA,$99,$40 ; $5740
    .byte $DB,$99,$C0,$DB,$BD,$00,$A5,$99,$40,$DC,$99,$C0,$DC,$99,$40,$DD ; $5750
    .byte $BD,$00,$A6,$99,$C0,$DD,$99,$40,$DE,$99,$C0,$DE,$99,$40,$DF,$BD ; $5760
    .byte $00,$A7,$99,$C0,$DF,$99,$40,$E0,$99,$C0,$E0,$BD,$00,$A8,$99,$40 ; $5770
    .byte $E1,$99,$C0,$E1,$99,$40,$E2,$99,$C0,$E2,$BD,$00,$A9,$99,$40,$E3 ; $5780
    .byte $99,$C0,$E3,$99,$40,$E4,$BD,$00,$AA,$99,$C0,$E4,$99,$40,$E5,$99 ; $5790
    .byte $C0,$E5,$99,$40,$E6,$BD,$00,$AB,$99,$C0,$E6,$99,$40,$E7,$99,$C0 ; $57A0
    .byte $E7,$BD,$00,$AC,$99,$40,$E8,$99,$C0,$E8,$99,$40,$E9,$BD,$00,$AD ; $57B0
    .byte $99,$C0,$E9,$99,$40,$EA,$99,$C0,$EA,$99,$40,$EB,$BD,$00,$AE,$99 ; $57C0
    .byte $C0,$EB,$99,$40,$EC,$99,$C0,$EC,$BD,$00,$AF,$99,$40,$ED,$99,$C0 ; $57D0
    .byte $ED,$99,$40,$EE,$99,$C0,$EE,$BD,$00,$B0,$99,$40,$EF,$99,$C0,$EF ; $57E0
    .byte $99,$40,$F0,$BD,$00,$B1,$99,$C0,$F0,$99,$40,$F1,$99,$C0,$F1,$99 ; $57F0
    .byte $40,$F2,$BD,$00,$B2,$99,$C0,$F2,$99,$40,$F3,$99,$C0,$F3,$BD,$00 ; $5800
    .byte $B3,$99,$40,$F4,$99,$C0,$F4,$99,$40,$F5,$99,$C0,$F5,$BD,$00,$B4 ; $5810
    .byte $99,$40,$F6,$99,$C0,$F6,$99,$40,$F7,$BD,$00,$B5,$99,$C0,$F7,$60 ; $5820
    .byte $BD,$00,$A2,$99,$40,$D8,$99,$C0,$D8,$BD,$00,$A3,$99,$40,$D9,$99 ; $5830
    .byte $C0,$D9,$99,$40,$DA,$BD,$00,$A4,$99,$C0,$DA,$99,$40,$DB,$99,$C0 ; $5840
    .byte $DB,$BD,$00,$A5,$99,$40,$DC,$99,$C0,$DC,$99,$40,$DD,$99,$C0,$DD ; $5850
    .byte $BD,$00,$A6,$99,$40,$DE,$99,$C0,$DE,$99,$40,$DF,$BD,$00,$A7,$99 ; $5860
    .byte $C0,$DF,$99,$40,$E0,$99,$C0,$E0,$99,$40,$E1,$BD,$00,$A8,$99,$C0 ; $5870
    .byte $E1,$99,$40,$E2,$99,$C0,$E2,$BD,$00,$A9,$99,$40,$E3,$99,$C0,$E3 ; $5880
    .byte $99,$40,$E4,$BD,$00                              ; $5890
    tax
    sta $E4C0,y
    sta $E540,y
    sta $E5C0,y
    sta $E640,y
    lda $AB00,x
    sta $E6C0,y
    sta $E740,y
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    sta $E8C0,y
    sta $E940,y
    lda $AD00,x
    sta $E9C0,y
    sta $EA40,y
    sta $EAC0,y
    sta $EB40,y
    lda $AE00,x
    sta $EBC0,y
    sta $EC40,y
    sta $ECC0,y
    lda $AF00,x
    sta $ED40,y
    sta $EDC0,y
    sta $EE40,y
    lda $B000,x
    sta $EEC0,y
    sta $EF40,y
    sta $EFC0,y
    sta $F040,y
    lda $B100,x
    sta $F0C0,y
    sta $F140,y
    sta $F1C0,y
    lda $B200,x
    sta $F240,y
    sta $F2C0,y
    sta $F340,y
    sta $F3C0,y
    lda $B300,x
    sta $F440,y
    sta $F4C0,y
    sta $F540,y
    lda $B400,x
    sta $F5C0,y
    sta $F640,y
    sta $F6C0,y
    lda $B500,x
    sta $F740,y
    sta $F7C0,y
    rts
    lda $A200,x
    sta $D840,y
    sta $D8C0,y
    lda $A300,x
    sta $D940,y
    sta $D9C0,y
    sta $DA40,y
    sta $DAC0,y
    lda $A400,x
    sta $DB40,y
    sta $DBC0,y
    sta $DC40,y
    lda $A500,x
    sta $DCC0,y
    sta PLYEVL,y
    sta EXP,y
    lda $A600,x
    sta $DE40,y
    sta $DEC0,y
    sta $DF40,y
    sta $DFC0,y
    lda $A700,x
    sta $E040,y
    sta $E0C0,y
    sta $E140,y
    lda $A800,x
    sta $E1C0,y
    sta $E240,y
    sta $E2C0,y
    lda $A900,x
    sta $E340,y
    sta $E3C0,y
    sta CASETV,y
    lda $AA00,x
    sta $E4C0,y
    sta $E540,y
    sta $E5C0,y
    sta $E640,y
    lda $AB00,x
    sta $E6C0,y
    sta $E740,y
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    sta $E8C0,y
    sta $E940,y
    lda $AD00,x
    sta $E9C0,y
    sta $EA40,y
    sta $EAC0,y
    sta $EB40,y
    lda $AE00,x
    sta $EBC0,y
    sta $EC40,y
    sta $ECC0,y
    lda $AF00,x
    sta $ED40,y
    sta $EDC0,y
    sta $EE40,y
    lda $B000,x
    sta $EEC0,y
    sta $EF40,y
    sta $EFC0,y
    lda $B100,x
    sta $F040,y
    sta $F0C0,y
    sta $F140,y
    sta $F1C0,y
    lda $B200,x
    sta $F240,y
    sta $F2C0,y
    sta $F340,y
    lda $B300,x
    sta $F3C0,y
    sta $F440,y
    sta $F4C0,y
    lda $B400,x
    sta $F540,y
    sta $F5C0,y
    sta $F640,y
    sta $F6C0,y
    lda $B500,x
    sta $F740,y
    sta $F7C0,y
    rts
    lda $A200,x
    sta $D840,y
    sta $D8C0,y
    sta $D940,y
    lda $A300,x
    sta $D9C0,y
    sta $DA40,y
    sta $DAC0,y
    lda $A400,x
    sta $DB40,y
    sta $DBC0,y
    sta $DC40,y
    sta $DCC0,y
    lda $A500,x
    sta PLYEVL,y
    sta EXP,y
    sta $DE40,y
    lda $A600,x
    sta $DEC0,y
    sta $DF40,y
    sta $DFC0,y
    lda $A700,x
    sta $E040,y
    sta $E0C0,y
    sta $E140,y
    lda $A800,x
    sta $E1C0,y
    sta $E240,y
    sta $E2C0,y
    lda $A900,x
    sta $E340,y
    sta $E3C0,y
    sta CASETV,y
    sta $E4C0,y
    lda $AA00,x
    sta $E540,y
    sta $E5C0,y
    sta $E640,y
    lda $AB00,x
    sta $E6C0,y
    sta $E740,y
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    sta $E8C0,y
    sta $E940,y
    lda $AD00,x
    sta $E9C0,y
    sta $EA40,y
    sta $EAC0,y
    lda $AE00,x
    sta $EB40,y
    sta $EBC0,y
    sta $EC40,y
    sta $ECC0,y
    lda $AF00,x
    sta $ED40,y
    sta $EDC0,y
    sta $EE40,y
    lda $B000,x
    sta $EEC0,y
    sta $EF40,y
    sta $EFC0,y
    lda $B100,x
    sta $F040,y
    sta $F0C0,y
    sta $F140,y
    lda $B200,x
    sta $F1C0,y
    sta $F240,y
    sta $F2C0,y
    lda $B300,x
    sta $F340,y
    sta $F3C0,y
    sta $F440,y
    sta $F4C0,y
    lda $B400,x
    sta $F540,y
    sta $F5C0,y
    sta $F640,y
    lda $B500,x
    sta $F6C0,y
    sta $F740,y
    sta $F7C0,y
    rts
    lda $A100,x
    sta $D840,y
    lda $A200,x
    sta $D8C0,y
    sta $D940,y
    sta $D9C0,y
    lda $A300,x
    sta $DA40,y
    sta $DAC0,y
    sta $DB40,y
    lda $A400,x
    sta $DBC0,y
    sta $DC40,y
    sta $DCC0,y
    lda $A500,x
    sta PLYEVL,y
    sta EXP,y
    sta $DE40,y
    lda $A600,x
    sta $DEC0,y
    sta $DF40,y
    sta $DFC0,y
    lda $A700,x
    sta $E040,y
    sta $E0C0,y
    sta $E140,y
    sta $E1C0,y
    lda $A800,x
    sta $E240,y
    sta $E2C0,y
    sta $E340,y
    lda $A900,x
    sta $E3C0,y
    sta CASETV,y
    sta $E4C0,y
    lda $AA00,x
    sta $E540,y
    sta $E5C0,y
    sta $E640,y
    lda $AB00,x
    sta $E6C0,y
    sta $E740,y
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    sta $E8C0,y
    sta $E940,y
    lda $AD00,x
    sta $E9C0,y
    sta $EA40,y
    sta $EAC0,y
    lda $AE00,x
    sta $EB40,y
    sta $EBC0,y
    sta $EC40,y
    lda $AF00,x
    sta $ECC0,y
    sta $ED40,y
    sta $EDC0,y
    lda $B000,x
    sta $EE40,y
    sta $EEC0,y
    sta $EF40,y
    sta $EFC0,y
    lda $B100,x
    sta $F040,y
    sta $F0C0,y
    sta $F140,y
    lda $B200,x
    sta $F1C0,y
    sta $F240,y
    sta $F2C0,y
    lda $B300,x
    sta $F340,y
    sta $F3C0,y
    sta $F440,y
    lda $B400,x
    sta $F4C0,y
    sta $F540,y
    sta $F5C0,y
    lda $B500,x
    sta $F640,y
    sta $F6C0,y
    sta $F740,y
    lda $B600,x
    sta $F7C0,y
    rts
    lda $A100,x
    sta $D840,y
    sta $D8C0,y
    lda $A200,x
    sta $D940,y
    sta $D9C0,y
    sta $DA40,y
    lda $A300,x
    sta $DAC0,y
    sta $DB40,y
    sta $DBC0,y
    lda $A400,x
    sta $DC40,y
    sta $DCC0,y
    sta PLYEVL,y
    lda $A500,x
    sta EXP,y
    sta $DE40,y
    sta $DEC0,y
    lda $A600,x
    sta $DF40,y
    sta $DFC0,y
    sta $E040,y
    lda $A700,x
    sta $E0C0,y
    sta $E140,y
    sta $E1C0,y
    lda $A800,x
    sta $E240,y
    sta $E2C0,y
    sta $E340,y
    lda $A900,x
    sta $E3C0,y
    sta CASETV,y
    sta $E4C0,y
    lda $AA00,x
    sta $E540,y
    sta $E5C0,y
    sta $E640,y
    lda $AB00,x
    sta $E6C0,y
    sta $E740,y
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    sta $E8C0,y
    sta $E940,y
    lda $AD00,x
    sta $E9C0,y
    sta $EA40,y
    sta $EAC0,y
    lda $AE00,x
    sta $EB40,y
    sta $EBC0,y
    sta $EC40,y
    lda $AF00,x
    sta $ECC0,y
    sta $ED40,y
    sta $EDC0,y
    lda $B000,x
    sta $EE40,y
    sta $EEC0,y
    sta $EF40,y
    lda $B100,x
    sta $EFC0,y
    sta $F040,y
    sta $F0C0,y
    lda $B200,x
    sta $F140,y
    sta $F1C0,y
    sta $F240,y
    lda $B300,x
    sta $F2C0,y
    sta $F340,y
    sta $F3C0,y
    lda $B400,x
    sta $F440,y
    sta $F4C0,y
    sta $F540,y
    lda $B500,x
    sta $F5C0,y
    sta $F640,y
    sta $F6C0,y
    lda $B600,x
    sta $F740,y
    sta $F7C0,y
    rts
    lda $A100,x
    sta $D840,y
    sta $D8C0,y
    lda $A200,x
    sta $D940,y
    sta $D9C0,y
    sta $DA40,y
    lda $A300,x
    sta $DAC0,y
    sta $DB40,y
    sta $DBC0,y
    lda $A400,x
    sta $DC40,y
    sta $DCC0,y
    sta PLYEVL,y
    lda $A500,x
    sta EXP,y
    sta $DE40,y
    sta $DEC0,y
    lda $A600,x
    sta $DF40,y
    sta $DFC0,y
    sta $E040,y
    lda $A700,x
    sta $E0C0,y
    sta $E140,y
    sta $E1C0,y
    lda $A800,x
    sta $E240,y
    sta $E2C0,y
    sta $E340,y
    lda $A900,x
    sta $E3C0,y
    sta CASETV,y
    sta $E4C0,y
    lda $AA00,x
    sta $E540,y
    sta $E5C0,y
    sta $E640,y
    lda $AB00,x
    sta $E6C0,y
    sta $E740,y
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    sta $E8C0,y
    sta $E940,y
    lda $AD00,x
    sta $E9C0,y
    sta $EA40,y
    sta $EAC0,y
    lda $AE00,x
    sta $EB40,y
    sta $EBC0,y
    sta $EC40,y
    lda $AF00,x
    sta $ECC0,y
    sta $ED40,y
    sta $EDC0,y
    lda $B000,x
    sta $EE40,y
    sta $EEC0,y
    sta $EF40,y
    lda $B100,x
    sta $EFC0,y
    sta $F040,y
    sta $F0C0,y
    lda $B200,x
    sta $F140,y
    sta $F1C0,y
    sta $F240,y
    lda $B300,x
    sta $F2C0,y
    sta $F340,y
    sta $F3C0,y
    lda $B400,x
    sta $F440,y
    sta $F4C0,y
    sta $F540,y
    lda $B500,x
    sta $F5C0,y
    sta $F640,y
    sta $F6C0,y
    lda $B600,x
    sta $F740,y
    sta $F7C0,y
    rts
    lda $A100,x
    sta $D840,y
    sta $D8C0,y
    sta $D940,y
    lda $A200,x
    sta $D9C0,y
    sta $DA40,y
    sta $DAC0,y
    lda $A300,x
    sta $DB40,y
    sta $DBC0,y
    sta $DC40,y
    lda $A400,x
    sta $DCC0,y
    sta PLYEVL,y
    sta EXP,y
    lda $A500,x
    sta $DE40,y
    sta $DEC0,y
    sta $DF40,y
    lda $A600,x
    sta $DFC0,y
    sta $E040,y
    lda $A700,x
    sta $E0C0,y
    sta $E140,y
    sta $E1C0,y
    lda $A800,x
    sta $E240,y
    sta $E2C0,y
    sta $E340,y
    lda $A900,x
    sta $E3C0,y
    sta CASETV,y
    sta $E4C0,y
    lda $AA00,x
    sta $E540,y
    sta $E5C0,y
    sta $E640,y
    lda $AB00,x
    sta $E6C0,y
    sta $E740,y
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    sta $E8C0,y
    sta $E940,y
    lda $AD00,x
    sta $E9C0,y
    sta $EA40,y
    sta $EAC0,y
    lda $AE00,x
    sta $EB40,y
    sta $EBC0,y
    sta $EC40,y
    lda $AF00,x
    sta $ECC0,y
    sta $ED40,y
    sta $EDC0,y
    lda $B000,x
    sta $EE40,y
    sta $EEC0,y
    sta $EF40,y
    lda $B100,x
    sta $EFC0,y
    sta $F040,y
    lda $B200,x
    sta $F0C0,y
    sta $F140,y
    sta $F1C0,y
    lda $B300,x
    sta $F240,y
    sta $F2C0,y
    sta $F340,y
    lda $B400,x
    sta $F3C0,y
    sta $F440,y
    sta $F4C0,y
    lda $B500,x
    sta $F540,y
    sta $F5C0,y
    sta $F640,y
    lda $B600,x
    sta $F6C0,y
    sta $F740,y
    sta $F7C0,y
    rts
    lda $A000,x
    sta $D840,y
    lda $A100,x
    sta $D8C0,y
    sta $D940,y
    sta $D9C0,y
    lda $A200,x
    sta $DA40,y
    sta $DAC0,y
    lda $A300,x
    sta $DB40,y
    sta $DBC0,y
    sta $DC40,y
    lda $A400,x
    sta $DCC0,y
    sta PLYEVL,y
    sta EXP,y
    lda $A500,x
    sta $DE40,y
    sta $DEC0,y
    sta $DF40,y
    lda $A600,x
    sta $DFC0,y
    sta $E040,y
    sta $E0C0,y
    lda $A700,x
    sta $E140,y
    sta $E1C0,y
    sta $E240,y
    lda $A800,x
    sta $E2C0,y
    sta $E340,y
    lda $A900,x
    sta $E3C0,y
    sta CASETV,y
    sta $E4C0,y
    lda $AA00,x
    sta $E540,y
    sta $E5C0,y
    sta $E640,y
    lda $AB00,x
    sta $E6C0,y
    sta $E740,y
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    sta $E8C0,y
    sta $E940,y
    lda $AD00,x
    sta $E9C0,y
    sta $EA40,y
    sta $EAC0,y
    lda $AE00,x
    sta $EB40,y
    sta $EBC0,y
    sta $EC40,y
    lda $AF00,x
    sta $ECC0,y
    sta $ED40,y
    lda $B000,x
    sta $EDC0,y
    sta $EE40,y
    sta $EEC0,y
    lda $B100,x
    sta $EF40,y
    sta $EFC0,y
    sta $F040,y
    lda $B200,x
    sta $F0C0,y
    sta $F140,y
    sta $F1C0,y
    lda $B300,x
    sta $F240,y
    sta $F2C0,y
    sta $F340,y
    lda $B400,x
    sta $F3C0,y
    sta $F440,y
    sta $F4C0,y
    lda $B500,x
    sta $F540,y
    sta $F5C0,y
    lda $B600,x
    sta $F640,y
    sta $F6C0,y
    sta $F740,y
    lda $B700,x
    sta $F7C0,y
    rts
    lda $A000,x
    sta $D840,y
    lda $A100,x
    sta $D8C0,y
    sta $D940,y
    sta $D9C0,y
    lda $A200,x
    sta $DA40,y
    sta $DAC0,y
    sta $DB40,y
    lda $A300,x
    sta $DBC0,y
    sta $DC40,y
    sta $DCC0,y
    lda $A400,x
    sta PLYEVL,y
    sta EXP,y
    sta $DE40,y
    lda $A500,x
    sta $DEC0,y
    sta $DF40,y
    lda $A600,x
    sta $DFC0,y
    sta $E040,y
    sta $E0C0,y
    lda $A700,x
    sta $E140,y
    sta $E1C0,y
    sta $E240,y
    lda $A800,x
    sta $E2C0,y
    sta $E340,y
    sta $E3C0,y
    lda $A900,x
    sta CASETV,y
    sta $E4C0,y
    lda $AA00,x
    sta $E540,y
    sta $E5C0,y
    sta $E640,y
    lda $AB00,x
    sta $E6C0,y
    sta $E740,y
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    sta $E8C0,y
    sta $E940,y
    lda $AD00,x
    sta $E9C0,y
    sta $EA40,y
    sta $EAC0,y
    lda $AE00,x
    sta $EB40,y
    sta $EBC0,y
    lda $AF00,x
    sta $EC40,y
    sta $ECC0,y
    sta $ED40,y
    lda $B000,x
    sta $EDC0,y
    sta $EE40,y
    sta $EEC0,y
    lda $B100,x
    sta $EF40,y
    sta $EFC0,y
    sta $F040,y
    lda $B200,x
    sta $F0C0,y
    sta $F140,y
    lda $B300,x
    sta $F1C0,y
    sta $F240,y
    sta $F2C0,y
    lda $B400,x
    sta $F340,y
    sta $F3C0,y
    sta $F440,y
    lda $B500,x
    sta $F4C0,y
    sta $F540,y
    sta $F5C0,y
    lda $B600,x
    sta $F640,y
    sta $F6C0,y
    sta $F740,y
    lda $B700,x
    sta $F7C0,y
    rts
    lda $A000,x
    sta $D840,y
    sta $D8C0,y
    lda $A100,x
    sta $D940,y
    sta $D9C0,y
    sta $DA40,y
    lda $A200,x
    sta $DAC0,y
    sta $DB40,y
    lda $A300,x
    sta $DBC0,y
    sta $DC40,y
    sta $DCC0,y
    lda $A400,x
    sta PLYEVL,y
    sta EXP,y
    sta $DE40,y
    lda $A500,x
    sta $DEC0,y
    sta $DF40,y
    sta $DFC0,y
    lda $A600,x
    sta $E040,y
    sta $E0C0,y
    lda $A700,x
    sta $E140,y
    sta $E1C0,y
    sta $E240,y
    lda $A800,x
    sta $E2C0,y
    sta $E340,y
    sta $E3C0,y
    lda $A900,x
    sta CASETV,y
    sta $E4C0,y
    sta $E540,y
    lda $AA00,x
    sta $E5C0,y
    sta $E640,y
    lda $AB00,x
    sta $E6C0,y
    sta $E740,y
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    sta $E8C0,y
    sta $E940,y
    lda $AD00,x
    sta $E9C0,y
    sta $EA40,y
    lda $AE00,x
    sta $EAC0,y
    sta $EB40,y
    sta $EBC0,y
    lda $AF00,x
    sta $EC40,y
    sta $ECC0,y
    sta $ED40,y
    lda $B000,x
    sta $EDC0,y
    sta $EE40,y
    sta $EEC0,y
    lda $B100,x
    sta $EF40,y
    sta $EFC0,y
    lda $B200,x
    sta $F040,y
    sta $F0C0,y
    sta $F140,y
    lda $B300,x
    sta $F1C0,y
    sta $F240,y
    sta $F2C0,y
    lda $B400,x
    sta $F340,y
    sta $F3C0,y
    sta $F440,y
    lda $B500,x
    sta $F4C0,y
    sta $F540,y
    lda $B600,x
    sta $F5C0,y
    sta $F640,y
    sta $F6C0,y
    lda $B700,x
    sta $F740,y
    sta $F7C0,y
    rts
    lda $A000,x
    sta $D840,y
    sta $D8C0,y
    sta $D940,y
    lda $A100,x
    sta $D9C0,y
    sta $DA40,y
    lda $A200,x
    sta $DAC0,y
    sta $DB40,y
    sta $DBC0,y
    lda $A300,x
    sta $DC40,y
    sta $DCC0,y
    sta PLYEVL,y
    lda $A400,x
    sta EXP,y
    sta $DE40,y
    lda $A500,x
    sta $DEC0,y
    sta $DF40,y
    sta $DFC0,y
    lda $A600,x
    sta $E040,y
    sta $E0C0,y
    sta $E140,y
    lda $A700,x
    sta $E1C0,y
    sta $E240,y
    lda $A800,x
    sta $E2C0,y
    sta $E340,y
    sta $E3C0,y
    lda $A900,x
    sta CASETV,y
    sta $E4C0,y
    sta $E540,y
    lda $AA00,x
    sta $E5C0,y
    sta $E640,y
    lda $AB00,x
    sta $E6C0,y
    sta $E740,y
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    sta $E8C0,y
    sta $E940,y
    lda $AD00,x
    sta $E9C0,y
    sta $EA40,y
    lda $AE00,x
    sta $EAC0,y
    sta $EB40,y
    sta $EBC0,y
    lda $AF00,x
    sta $EC40,y
    sta $ECC0,y
    sta $ED40,y
    lda $B000,x
    sta $EDC0,y
    sta $EE40,y
    lda $B100,x
    sta $EEC0,y
    sta $EF40,y
    sta $EFC0,y
    lda $B200,x
    sta $F040,y
    sta $F0C0,y
    sta $F140,y
    lda $B300,x
    sta $F1C0,y
    sta $F240,y
    lda $B400,x
    sta $F2C0,y
    sta $F340,y
    sta $F3C0,y
    lda $B500,x
    sta $F440,y
    sta $F4C0,y
    sta $F540,y
    lda $B600,x
    sta $F5C0,y
    sta $F640,y
    lda $B700,x
    sta $F6C0,y
    sta $F740,y
    sta $F7C0,y
    rts
    lda $9F00,x
    sta $D840,y
    lda $A000,x
    sta $D8C0,y
    sta $D940,y
    lda $A100,x
    sta $D9C0,y
    sta $DA40,y
    sta $DAC0,y
    lda $A200,x
    sta $DB40,y
    sta $DBC0,y
    lda $A300,x
    sta $DC40,y
    sta $DCC0,y
    sta PLYEVL,y
    lda $A400,x
    sta EXP,y
    sta $DE40,y
    sta $DEC0,y
    lda $A500,x
    sta $DF40,y
    sta $DFC0,y
    lda $A600,x
    sta $E040,y
    sta $E0C0,y
    sta $E140,y
    lda $A700,x
    sta $E1C0,y
    sta $E240,y
    sta $E2C0,y
    lda $A800,x
    sta $E340,y
    sta $E3C0,y
    lda $A900,x
    sta CASETV,y
    sta $E4C0,y
    sta $E540,y
    lda $AA00,x
    sta $E5C0,y
    sta $E640,y
    lda $AB00,x
    sta $E6C0,y
    sta $E740,y
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    sta $E8C0,y
    sta $E940,y
    lda $AD00,x
    sta $E9C0,y
    sta $EA40,y
    lda $AE00,x
    sta $EAC0,y
    sta $EB40,y
    sta $EBC0,y
    lda $AF00,x
    sta $EC40,y
    sta $ECC0,y
    lda $B000,x
    sta $ED40,y
    sta $EDC0,y
    sta $EE40,y
    lda $B100,x
    sta $EEC0,y
    sta $EF40,y
    sta $EFC0,y
    lda $B200,x
    sta $F040,y
    sta $F0C0,y
    lda $B300,x
    sta $F140,y
    sta $F1C0,y
    sta $F240,y
    lda $B400,x
    sta $F2C0,y
    sta $F340,y
    sta $F3C0,y
    lda $B500,x
    sta $F440,y
    sta $F4C0,y
    lda $B600,x
    sta $F540,y
    sta $F5C0,y
    sta $F640,y
    lda $B700,x
    sta $F6C0,y
    sta $F740,y
    lda $B800,x
    sta $F7C0,y
    rts
    lda $9F00,x
    sta $D840,y
    lda $A000,x
    sta $D8C0,y
    sta $D940,y
    sta $D9C0,y
    lda $A100,x
    sta $DA40,y
    sta $DAC0,y
    lda $A200,x
    sta $DB40,y
    sta $DBC0,y
    sta $DC40,y
    lda $A300,x
    sta $DCC0,y
    sta PLYEVL,y
    sta EXP,y
    lda $A400,x
    sta $DE40,y
    sta $DEC0,y
    lda $A500,x
    sta $DF40,y
    sta $DFC0,y
    sta $E040,y
    lda $A600,x
    sta $E0C0,y
    sta $E140,y
    lda $A700,x
    sta $E1C0,y
    sta $E240,y
    sta $E2C0,y
    lda $A800,x
    sta $E340,y
    sta $E3C0,y
    lda $A900,x
    sta CASETV,y
    sta $E4C0,y
    sta $E540,y
    lda $AA00,x
    sta $E5C0,y
    sta $E640,y
    lda $AB00,x
    sta $E6C0,y
    sta $E740,y
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    sta $E8C0,y
    sta $E940,y
    lda $AD00,x
    sta $E9C0,y
    sta $EA40,y
    lda $AE00,x
    sta $EAC0,y
    sta $EB40,y
    sta $EBC0,y
    lda $AF00,x
    sta $EC40,y
    sta $ECC0,y
    lda $B000,x
    sta $ED40,y
    sta $EDC0,y
    sta $EE40,y
    lda $B100,x
    sta $EEC0,y
    sta $EF40,y
    lda $B200,x
    sta $EFC0,y
    sta $F040,y
    sta $F0C0,y
    lda $B300,x
    sta $F140,y
    sta $F1C0,y
    lda $B400,x
    sta $F240,y
    sta $F2C0,y
    sta $F340,y
    lda $B500,x
    sta $F3C0,y
    sta $F440,y
    sta $F4C0,y
    lda $B600,x
    sta $F540,y
    sta $F5C0,y
    lda $B700,x
    sta $F640,y
    sta $F6C0,y
    sta $F740,y
    lda $B800,x
    sta $F7C0,y
    rts
    lda $9F00,x
    sta $D840,y
    sta $D8C0,y
    lda $A000,x
    sta $D940,y
    sta $D9C0,y
    lda $A100,x
    sta $DA40,y
    sta $DAC0,y
    sta $DB40,y
    lda $A200,x
    sta $DBC0,y
    sta $DC40,y
    lda $A300,x
    sta $DCC0,y
    sta PLYEVL,y
    sta EXP,y
    lda $A400,x
    sta $DE40,y
    sta $DEC0,y
    lda $A500,x
    sta $DF40,y
    sta $DFC0,y
    sta $E040,y
    lda $A600,x
    sta $E0C0,y
    sta $E140,y
    lda $A700,x
    sta $E1C0,y
    sta $E240,y
    sta $E2C0,y
    lda $A800,x
    sta $E340,y
    sta $E3C0,y
    lda $A900,x
    sta CASETV,y
    sta $E4C0,y
    sta $E540,y
    lda $AA00,x
    sta $E5C0,y
    sta $E640,y
    lda $AB00,x
    sta $E6C0,y
    sta $E740,y
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    sta $E8C0,y
    sta $E940,y
    lda $AD00,x
    sta $E9C0,y
    sta $EA40,y
    lda $AE00,x
    sta $EAC0,y
    sta $EB40,y
    sta $EBC0,y
    lda $AF00,x
    sta $EC40,y
    sta $ECC0,y
    lda $B000,x
    sta $ED40,y
    sta $EDC0,y
    sta $EE40,y
    lda $B100,x
    sta $EEC0,y
    sta $EF40,y
    lda $B200,x
    sta $EFC0,y
    sta $F040,y
    sta $F0C0,y
    lda $B300,x
    sta $F140,y
    sta $F1C0,y
    lda $B400,x
    sta $F240,y
    sta $F2C0,y
    sta $F340,y
    lda $B500,x
    sta $F3C0,y
    sta $F440,y
    lda $B600,x
    sta $F4C0,y
    sta $F540,y
    sta $F5C0,y
    lda $B700,x
    sta $F640,y
    sta $F6C0,y
    lda $B800,x
    sta $F740,y
    sta $F7C0,y
    rts
    lda $9F00,x
    sta $D840,y
    sta $D8C0,y
    lda $A000,x
    sta $D940,y
    sta $D9C0,y
    sta $DA40,y
    lda $A100,x
    sta $DAC0,y
    sta $DB40,y
    lda $A200,x
    sta $DBC0,y
    sta $DC40,y
    sta $DCC0,y
    lda $A300,x
    sta PLYEVL,y
    sta EXP,y
    lda $A400,x
    sta $DE40,y
    sta $DEC0,y
    sta $DF40,y
    lda $A500,x
    sta $DFC0,y
    sta $E040,y
    lda $A600,x
    sta $E0C0,y
    sta $E140,y
    sta $E1C0,y
    lda $A700,x
    sta $E240,y
    sta $E2C0,y
    lda $A800,x
    sta $E340,y
    sta $E3C0,y
    sta CASETV,y
    lda $A900,x
    sta $E4C0,y
    sta $E540,y
    lda $AA00,x
    sta $E5C0,y
    sta $E640,y
    sta $E6C0,y
    lda $AB00,x
    sta $E740,y
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    sta $E8C0,y
    lda $AD00,x
    sta $E940,y
    sta $E9C0,y
    sta $EA40,y
    lda $AE00,x
    sta $EAC0,y
    sta $EB40,y
    lda $AF00,x
    sta $EBC0,y
    sta $EC40,y
    sta $ECC0,y
    lda $B000,x
    sta $ED40,y
    sta $EDC0,y
    lda $B100,x
    sta $EE40,y
    sta $EEC0,y
    sta $EF40,y
    lda $B200,x
    sta $EFC0,y
    sta $F040,y
    lda $B300,x
    sta $F0C0,y
    sta $F140,y
    sta $F1C0,y
    lda $B400,x
    sta $F240,y
    sta $F2C0,y
    lda $B500,x
    sta $F340,y
    sta $F3C0,y
    sta $F440,y
    lda $B600,x
    sta $F4C0,y
    sta $F540,y
    lda $B700,x
    sta $F5C0,y
    sta $F640,y
    sta $F6C0,y
    lda $B800,x
    sta $F740,y
    sta $F7C0,y
    rts
    lda $9E00,x
    sta $D840,y
    lda $9F00,x
    sta $D8C0,y
    sta $D940,y
    lda $A000,x
    sta $D9C0,y
    sta $DA40,y
    lda $A100,x
    sta $DAC0,y
    sta $DB40,y
    sta $DBC0,y
    lda $A200,x
    sta $DC40,y
    sta $DCC0,y
    lda $A300,x
    sta PLYEVL,y
    sta EXP,y
    sta $DE40,y
    lda $A400,x
    sta $DEC0,y
    sta $DF40,y
    lda $A500,x
    sta $DFC0,y
    sta $E040,y
    sta $E0C0,y
    lda $A600,x
    sta $E140,y
    sta $E1C0,y
    lda $A700,x
    sta $E240,y
    sta $E2C0,y
    lda $A800,x
    sta $E340,y
    sta $E3C0,y
    sta CASETV,y
    lda $A900,x
    sta $E4C0,y
    sta $E540,y
    lda $AA00,x
    sta $E5C0,y
    sta $E640,y
    sta $E6C0,y
    lda $AB00,x
    sta $E740,y
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    sta $E8C0,y
    lda $AD00,x
    sta $E940,y
    sta $E9C0,y
    sta $EA40,y
    lda $AE00,x
    sta $EAC0,y
    sta $EB40,y
    lda $AF00,x
    sta $EBC0,y
    sta $EC40,y
    sta $ECC0,y
    lda $B000,x
    sta $ED40,y
    sta $EDC0,y
    lda $B100,x
    sta $EE40,y
    sta $EEC0,y
    lda $B200,x
    sta $EF40,y
    sta $EFC0,y
    sta $F040,y
    lda $B300,x
    sta $F0C0,y
    sta $F140,y
    lda $B400,x
    sta $F1C0,y
    sta $F240,y
    sta $F2C0,y
    lda $B500,x
    sta $F340,y
    sta $F3C0,y
    lda $B600,x
    sta $F440,y
    sta $F4C0,y
    sta $F540,y
    lda $B700,x
    sta $F5C0,y
    sta $F640,y
    lda $B800,x
    sta $F6C0,y
    sta $F740,y
    lda $B900,x
    sta $F7C0,y
    rts
    lda $9E00,x
    sta $D840,y
    lda $9F00,x
    sta $D8C0,y
    sta $D940,y
    sta $D9C0,y
    lda $A000,x
    sta $DA40,y
    sta $DAC0,y
    lda $A100,x
    sta $DB40,y
    sta $DBC0,y
    lda $A200,x
    sta $DC40,y
    sta $DCC0,y
    sta PLYEVL,y
    lda $A300,x
    sta EXP,y
    sta $DE40,y
    lda $A400,x
    sta $DEC0,y
    sta $DF40,y
    lda $A500,x
    sta $DFC0,y
    sta $E040,y
    sta $E0C0,y
    lda $A600,x
    sta $E140,y
    sta $E1C0,y
    lda $A700,x
    sta $E240,y
    sta $E2C0,y
    sta $E340,y
    lda $A800,x
    sta $E3C0,y
    sta CASETV,y
    lda $A900,x
    sta $E4C0,y
    sta $E540,y
    lda $AA00,x
    sta $E5C0,y
    sta $E640,y
    sta $E6C0,y
    lda $AB00,x
    sta $E740,y
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    sta $E8C0,y
    lda $AD00,x
    sta $E940,y
    sta $E9C0,y
    sta $EA40,y
    lda $AE00,x
    sta $EAC0,y
    sta $EB40,y
    lda $AF00,x
    sta $EBC0,y
    sta $EC40,y
    lda $B000,x
    sta $ECC0,y
    sta $ED40,y
    sta $EDC0,y
    lda $B100,x
    sta $EE40,y
    sta $EEC0,y
    lda $B200,x
    sta $EF40,y
    sta $EFC0,y
    sta $F040,y
    lda $B300,x
    sta $F0C0,y
    sta $F140,y
    lda $B400,x
    sta $F1C0,y
    sta $F240,y
    lda $B500,x
    sta $F2C0,y
    sta $F340,y
    sta $F3C0,y
    lda $B600,x
    sta $F440,y
    sta $F4C0,y
    lda $B700,x
    sta $F540,y
    sta $F5C0,y
    lda $B800,x
    sta $F640,y
    sta $F6C0,y
    sta $F740,y
    lda $B900,x
    sta $F7C0,y
    rts
    lda $9E00,x
    sta $D840,y
    sta $D8C0,y
    lda $9F00,x
    sta $D940,y
    sta $D9C0,y
    lda $A000,x
    sta $DA40,y
    sta $DAC0,y
    lda $A100,x
    sta $DB40,y
    sta $DBC0,y
    sta $DC40,y
    lda $A200,x
    sta $DCC0,y
    sta PLYEVL,y
    lda $A300,x
    sta EXP,y
    sta $DE40,y
    lda $A400,x
    sta $DEC0,y
    sta $DF40,y
    sta $DFC0,y
    lda $A500,x
    sta $E040,y
    sta $E0C0,y
    lda $A600,x
    sta $E140,y
    sta $E1C0,y
    lda $A700,x
    sta $E240,y
    sta $E2C0,y
    sta $E340,y
    lda $A800,x
    sta $E3C0,y
    sta CASETV,y
    lda $A900,x
    sta $E4C0,y
    sta $E540,y
    lda $AA00,x
    sta $E5C0,y
    sta $E640,y
    sta $E6C0,y
    lda $AB00,x
    sta $E740,y
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    sta $E8C0,y
    lda $AD00,x
    sta $E940,y
    sta $E9C0,y
    sta $EA40,y
    lda $AE00,x
    sta $EAC0,y
    sta $EB40,y
    lda $AF00,x
    sta $EBC0,y
    sta $EC40,y
    lda $B000,x
    sta $ECC0,y
    sta $ED40,y
    sta $EDC0,y
    lda $B100,x
    sta $EE40,y
    sta $EEC0,y
    lda $B200,x
    sta $EF40,y
    sta $EFC0,y
    lda $B300,x
    sta $F040,y
    sta $F0C0,y
    sta $F140,y
    lda $B400,x
    sta $F1C0,y
    sta $F240,y
    lda $B500,x
    sta $F2C0,y
    sta $F340,y
    lda $B600,x
    sta $F3C0,y
    sta $F440,y
    sta $F4C0,y
    lda $B700,x
    sta $F540,y
    sta $F5C0,y
    lda $B800,x
    sta $F640,y
    sta $F6C0,y
    lda $B900,x
    sta $F740,y
    sta $F7C0,y
    rts
    lda $9E00,x
    sta $D840,y
    sta $D8C0,y
    lda $9F00,x
    sta $D940,y
    sta $D9C0,y
    sta $DA40,y
    lda $A000,x
    sta $DAC0,y
    sta $DB40,y
    lda $A100,x
    sta $DBC0,y
    sta $DC40,y
    lda $A200,x
    sta $DCC0,y
    sta PLYEVL,y
    lda $A300,x
    sta EXP,y
    sta $DE40,y
    sta $DEC0,y
    lda $A400,x
    sta $DF40,y
    sta $DFC0,y
    lda $A500,x
    sta $E040,y
    sta $E0C0,y
    lda $A600,x
    sta $E140,y
    sta $E1C0,y
    sta $E240,y
    lda $A700,x
    sta $E2C0,y
    sta $E340,y
    lda $A800,x
    sta $E3C0,y
    sta CASETV,y
    lda $A900,x
    sta $E4C0,y
    sta $E540,y
    lda $AA00,x
    sta $E5C0,y
    sta $E640,y
    sta $E6C0,y
    lda $AB00,x
    sta $E740,y
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    sta $E8C0,y
    lda $AD00,x
    sta $E940,y
    sta $E9C0,y
    sta $EA40,y
    lda $AE00,x
    sta $EAC0,y
    sta $EB40,y
    lda $AF00,x
    sta $EBC0,y
    sta $EC40,y
    lda $B000,x
    sta $ECC0,y
    sta $ED40,y
    lda $B100,x
    sta $EDC0,y
    sta $EE40,y
    sta $EEC0,y
    lda $B200,x
    sta $EF40,y
    sta $EFC0,y
    lda $B300,x
    sta $F040,y
    sta $F0C0,y
    lda $B400,x
    sta $F140,y
    sta $F1C0,y
    sta $F240,y
    lda $B500,x
    sta $F2C0,y
    sta $F340,y
    lda $B600,x
    sta $F3C0,y
    sta $F440,y
    lda $B700,x
    sta $F4C0,y
    sta $F540,y
    lda $B800,x
    sta $F5C0,y
    sta $F640,y
    sta $F6C0,y
    lda $B900,x
    sta $F740,y
    sta $F7C0,y
    rts
    lda $9D00,x
    sta $D840,y
    lda $9E00,x
    sta $D8C0,y
    sta $D940,y
    lda $9F00,x
    sta $D9C0,y
    sta $DA40,y
    lda $A000,x
    sta $DAC0,y
    sta $DB40,y
    lda $A100,x
    sta $DBC0,y
    sta $DC40,y
    sta $DCC0,y
    lda $A200,x
    sta PLYEVL,y
    sta EXP,y
    lda $A300,x
    sta $DE40,y
    sta $DEC0,y
    lda $A400,x
    sta $DF40,y
    sta $DFC0,y
    lda $A500,x
    sta $E040,y
    sta $E0C0,y
    sta $E140,y
    lda $A600,x
    sta $E1C0,y
    sta $E240,y
    lda $A700,x
    sta $E2C0,y
    sta $E340,y
    lda $A800,x
    sta $E3C0,y
    sta CASETV,y
    lda $A900,x
    sta $E4C0,y
    sta $E540,y
    sta $E5C0,y
    lda $AA00,x
    sta $E640,y
    sta $E6C0,y
    lda $AB00,x
    sta $E740,y
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    sta $E8C0,y
    lda $AD00,x
    sta $E940,y
    sta $E9C0,y
    lda $AE00,x
    sta $EA40,y
    sta $EAC0,y
    sta $EB40,y
    lda $AF00,x
    sta $EBC0,y
    sta $EC40,y
    lda $B000,x
    sta $ECC0,y
    sta $ED40,y
    lda $B100,x
    sta $EDC0,y
    sta $EE40,y
    lda $B200,x
    sta $EEC0,y
    sta $EF40,y
    sta $EFC0,y
    lda $B300,x
    sta $F040,y
    sta $F0C0,y
    lda $B400,x
    sta $F140,y
    sta $F1C0,y
    lda $B500,x
    sta $F240,y
    sta $F2C0,y
    lda $B600,x
    sta $F340,y
    sta $F3C0,y
    sta $F440,y
    lda $B700,x
    sta $F4C0,y
    sta $F540,y
    lda $B800,x
    sta $F5C0,y
    sta $F640,y
    lda $B900,x
    sta $F6C0,y
    sta $F740,y
    lda $BA00,x
    sta $F7C0,y
    rts
    lda $9D00,x
    sta $D840,y
    lda $9E00,x
    sta $D8C0,y
    sta $D940,y
    lda $9F00,x
    sta $D9C0,y
    sta $DA40,y
    sta $DAC0,y
    lda $A000,x
    sta $DB40,y
    sta $DBC0,y
    lda $A100,x
    sta $DC40,y
    sta $DCC0,y
    lda $A200,x
    sta PLYEVL,y
    sta EXP,y
    lda $A300,x
    sta $DE40,y
    sta $DEC0,y
    lda $A400,x
    sta $DF40,y
    sta $DFC0,y
    sta $E040,y
    lda $A500,x
    sta $E0C0,y
    sta $E140,y
    lda $A600,x
    sta $E1C0,y
    sta $E240,y
    lda $A700,x
    sta $E2C0,y
    sta $E340,y
    lda $A800,x
    sta $E3C0,y
    sta CASETV,y
    lda $A900,x
    sta $E4C0,y
    sta $E540,y
    sta $E5C0,y
    lda $AA00,x
    sta $E640,y
    sta $E6C0,y
    lda $AB00,x
    sta $E740,y
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    sta $E8C0,y
    lda $AD00,x
    sta $E940,y
    sta $E9C0,y
    lda $AE00,x
    sta $EA40,y
    sta $EAC0,y
    sta $EB40,y
    lda $AF00,x
    sta $EBC0,y
    sta $EC40,y
    lda $B000,x
    sta $ECC0,y
    sta $ED40,y
    lda $B100,x
    sta $EDC0,y
    sta $EE40,y
    lda $B200,x
    sta $EEC0,y
    sta $EF40,y
    lda $B300,x
    sta $EFC0,y
    sta $F040,y
    sta $F0C0,y
    lda $B400,x
    sta $F140,y
    sta $F1C0,y
    lda $B500,x
    sta $F240,y
    sta $F2C0,y
    lda $B600,x
    sta $F340,y
    sta $F3C0,y
    lda $B700,x
    sta $F440,y
    sta $F4C0,y
    lda $B800,x
    sta $F540,y
    sta $F5C0,y
    sta $F640,y
    lda $B900,x
    sta $F6C0,y
    sta $F740,y
    lda $BA00,x
    sta $F7C0,y
    rts
    lda $9D00,x
    sta $D840,y
    sta $D8C0,y
    lda $9E00,x
    sta $D940,y
    sta $D9C0,y
    lda $9F00,x
    sta $DA40,y
    sta $DAC0,y
    lda $A000,x
    sta $DB40,y
    sta $DBC0,y
    lda $A100,x
    sta $DC40,y
    sta $DCC0,y
    lda $A200,x
    sta PLYEVL,y
    sta EXP,y
    lda $A300,x
    sta $DE40,y
    sta $DEC0,y
    sta $DF40,y
    lda $A400,x
    sta $DFC0,y
    sta $E040,y
    lda $A500,x
    sta $E0C0,y
    sta $E140,y
    lda $A600,x
    sta $E1C0,y
    sta $E240,y
    lda $A700,x
    sta $E2C0,y
    sta $E340,y
    lda $A800,x
    sta $E3C0,y
    sta CASETV,y
    lda $A900,x
    sta $E4C0,y
    sta $E540,y
    sta $E5C0,y
    lda $AA00,x
    sta $E640,y
    sta $E6C0,y
    lda $AB00,x
    sta $E740,y
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    sta $E8C0,y
    lda $AD00,x
    sta $E940,y
    sta $E9C0,y
    lda $AE00,x
    sta $EA40,y
    sta $EAC0,y
    sta $EB40,y
    lda $AF00,x
    sta $EBC0,y
    sta $EC40,y
    lda $B000,x
    sta $ECC0,y
    sta $ED40,y
    lda $B100,x
    sta $EDC0,y
    sta $EE40,y
    lda $B200,x
    sta $EEC0,y
    sta $EF40,y
    lda $B300,x
    sta $EFC0,y
    sta $F040,y
    lda $B400,x
    sta $F0C0,y
    sta $F140,y
    sta $F1C0,y
    lda $B500,x
    sta $F240,y
    sta $F2C0,y
    lda $B600,x
    sta $F340,y
    sta $F3C0,y
    lda $B700,x
    sta $F440,y
    sta $F4C0,y
    lda $B800,x
    sta $F540,y
    sta $F5C0,y
    lda $B900,x
    sta $F640,y
    sta $F6C0,y
    lda $BA00,x
    sta $F740,y
    sta $F7C0,y
    rts
    lda $9D00,x
    sta $D840,y
    sta $D8C0,y
    lda $9E00,x
    sta $D940,y
    sta $D9C0,y
    lda $9F00,x
    sta $DA40,y
    sta $DAC0,y
    lda $A000,x
    sta $DB40,y
    sta $DBC0,y
    sta $DC40,y
    lda $A100,x
    sta $DCC0,y
    sta PLYEVL,y
    lda $A200,x
    sta EXP,y
    sta $DE40,y
    lda $A300,x
    sta $DEC0,y
    sta $DF40,y
    lda $A400,x
    sta $DFC0,y
    sta $E040,y
    lda $A500,x
    sta $E0C0,y
    sta $E140,y
    lda $A600,x
    sta $E1C0,y
    sta $E240,y
    lda $A700,x
    sta $E2C0,y
    sta $E340,y
    lda $A800,x
    sta $E3C0,y
    sta CASETV,y
    sta $E4C0,y
    lda $A900,x
    sta $E540,y
    sta $E5C0,y
    lda $AA00,x
    sta $E640,y
    sta $E6C0,y
    lda $AB00,x
    sta $E740,y
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    sta $E8C0,y
    lda $AD00,x
    sta $E940,y
    sta $E9C0,y
    lda $AE00,x
    sta $EA40,y
    sta $EAC0,y
    lda $AF00,x
    sta $EB40,y
    sta $EBC0,y
    sta $EC40,y
    lda $B000,x
    sta $ECC0,y
    sta $ED40,y
    lda $B100,x
    sta $EDC0,y
    sta $EE40,y
    lda $B200,x
    sta $EEC0,y
    sta $EF40,y
    lda $B300,x
    sta $EFC0,y
    sta $F040,y
    lda $B400,x
    sta $F0C0,y
    sta $F140,y
    lda $B500,x
    sta $F1C0,y
    sta $F240,y
    lda $B600,x
    sta $F2C0,y
    sta $F340,y
    lda $B700,x
    sta $F3C0,y
    sta $F440,y
    sta $F4C0,y
    lda $B800,x
    sta $F540,y
    sta $F5C0,y
    lda $B900,x
    sta $F640,y
    sta $F6C0,y
    lda $BA00,x
    sta $F740,y
    sta $F7C0,y
    rts
    lda $9C00,x
    sta $D840,y
    lda $9D00,x
    sta $D8C0,y
    sta $D940,y
    lda $9E00,x
    sta $D9C0,y
    sta $DA40,y
    lda $9F00,x
    sta $DAC0,y
    sta $DB40,y
    lda $A000,x
    sta $DBC0,y
    sta $DC40,y
    lda $A100,x
    sta $DCC0,y
    sta PLYEVL,y
    lda $A200,x
    sta EXP,y
    sta $DE40,y
    lda $A300,x
    sta $DEC0,y
    sta $DF40,y
    lda $A400,x
    sta $DFC0,y
    sta $E040,y
    lda $A500,x
    sta $E0C0,y
    sta $E140,y
    lda $A600,x
    sta $E1C0,y
    sta $E240,y
    sta $E2C0,y
    lda $A700,x
    sta $E340,y
    sta $E3C0,y
    lda $A800,x
    sta CASETV,y
    sta $E4C0,y
    lda $A900,x
    sta $E540,y
    sta $E5C0,y
    lda $AA00,x
    sta $E640,y
    sta $E6C0,y
    lda $AB00,x
    sta $E740,y
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    sta $E8C0,y
    lda $AD00,x
    sta $E940,y
    sta $E9C0,y
    lda $AE00,x
    sta $EA40,y
    sta $EAC0,y
    lda $AF00,x
    sta $EB40,y
    sta $EBC0,y
    lda $B000,x
    sta $EC40,y
    sta $ECC0,y
    lda $B100,x
    sta $ED40,y
    sta $EDC0,y
    sta $EE40,y
    lda $B200,x
    sta $EEC0,y
    sta $EF40,y
    lda $B300,x
    sta $EFC0,y
    sta $F040,y
    lda $B400,x
    sta $F0C0,y
    sta $F140,y
    lda $B500,x
    sta $F1C0,y
    sta $F240,y
    lda $B600,x
    sta $F2C0,y
    sta $F340,y
    lda $B700,x
    sta $F3C0,y
    sta $F440,y
    lda $B800,x
    sta $F4C0,y
    sta $F540,y
    lda $B900,x
    sta $F5C0,y
    sta $F640,y
    lda $BA00,x
    sta $F6C0,y
    sta $F740,y
    lda $BB00,x
    sta $F7C0,y
    rts
    lda $9C00,x
    sta $D840,y
    lda $9D00,x
    sta $D8C0,y
    sta $D940,y
    lda $9E00,x
    sta $D9C0,y
    sta $DA40,y
    lda $9F00,x
    sta $DAC0,y
    sta $DB40,y
    lda $A000,x
    sta $DBC0,y
    sta $DC40,y
    lda $A100,x
    sta $DCC0,y
    sta PLYEVL,y
    lda $A200,x
    sta EXP,y
    sta $DE40,y
    lda $A300,x
    sta $DEC0,y
    sta $DF40,y
    lda $A400,x
    sta $DFC0,y
    sta $E040,y
    sta $E0C0,y
    lda $A500,x
    sta $E140,y
    sta $E1C0,y
    lda $A600,x
    sta $E240,y
    sta $E2C0,y
    lda $A700,x
    sta $E340,y
    sta $E3C0,y
    lda $A800,x
    sta CASETV,y
    sta $E4C0,y
    lda $A900,x
    sta $E540,y
    sta $E5C0,y
    lda $AA00,x
    sta $E640,y
    sta $E6C0,y
    lda $AB00,x
    sta $E740,y
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    sta $E8C0,y
    lda $AD00,x
    sta $E940,y
    sta $E9C0,y
    lda $AE00,x
    sta $EA40,y
    sta $EAC0,y
    lda $AF00,x
    sta $EB40,y
    sta $EBC0,y
    lda $B000,x
    sta $EC40,y
    sta $ECC0,y
    lda $B100,x
    sta $ED40,y
    sta $EDC0,y
    lda $B200,x
    sta $EE40,y
    sta $EEC0,y
    lda $B300,x
    sta $EF40,y
    sta $EFC0,y
    sta $F040,y
    lda $B400,x
    sta $F0C0,y
    sta $F140,y
    lda $B500,x
    sta $F1C0,y
    sta $F240,y
    lda $B600,x
    sta $F2C0,y
    sta $F340,y
    lda $B700,x
    sta $F3C0,y
    sta $F440,y
    lda $B800,x
    sta $F4C0,y
    sta $F540,y
    lda $B900,x
    sta $F5C0,y
    sta $F640,y
    lda $BA00,x
    sta $F6C0,y
    sta $F740,y
    lda $BB00,x
    sta $F7C0,y
    rts
    lda $9C00,x
    sta $D840,y
    sta $D8C0,y
    lda $9D00,x
    sta $D940,y
    sta $D9C0,y
    lda $9E00,x
    sta $DA40,y
    sta $DAC0,y
    lda $9F00,x
    sta $DB40,y
    sta $DBC0,y
    lda $A000,x
    sta $DC40,y
    sta $DCC0,y
    lda $A100,x
    sta PLYEVL,y
    sta EXP,y
    lda $A200,x
    sta $DE40,y
    sta $DEC0,y
    lda $A300,x
    sta $DF40,y
    sta $DFC0,y
    lda $A400,x
    sta $E040,y
    sta $E0C0,y
    lda $A500,x
    sta $E140,y
    sta $E1C0,y
    lda $A600,x
    sta $E240,y
    sta $E2C0,y
    lda $A700,x
    sta $E340,y
    sta $E3C0,y
    lda $A800,x
    sta CASETV,y
    sta $E4C0,y
    lda $A900,x
    sta $E540,y
    sta $E5C0,y
    lda $AA00,x
    sta $E640,y
    sta $E6C0,y
    lda $AB00,x
    sta $E740,y
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    sta $E8C0,y
    lda $AD00,x
    sta $E940,y
    sta $E9C0,y
    lda $AE00,x
    sta $EA40,y
    sta $EAC0,y
    lda $AF00,x
    sta $EB40,y
    sta $EBC0,y
    lda $B000,x
    sta $EC40,y
    sta $ECC0,y
    lda $B100,x
    sta $ED40,y
    sta $EDC0,y
    lda $B200,x
    sta $EE40,y
    sta $EEC0,y
    lda $B300,x
    sta $EF40,y
    sta $EFC0,y
    lda $B400,x
    sta $F040,y
    sta $F0C0,y
    lda $B500,x
    sta $F140,y
    sta $F1C0,y
    lda $B600,x
    sta $F240,y
    sta $F2C0,y
    lda $B700,x
    sta $F340,y
    sta $F3C0,y
    lda $B800,x
    sta $F440,y
    sta $F4C0,y
    lda $B900,x
    sta $F540,y
    sta $F5C0,y
    lda $BA00,x
    sta $F640,y
    sta $F6C0,y
    lda $BB00,x
    sta $F740,y
    sta $F7C0,y
    rts
    lda $9C00,x
    sta $D840,y
    sta $D8C0,y
    lda $9D00,x
    sta $D940,y
    sta $D9C0,y
    lda $9E00,x
    sta $DA40,y
    sta $DAC0,y
    lda $9F00,x
    sta $DB40,y
    sta $DBC0,y
    lda $A000,x
    sta $DC40,y
    sta $DCC0,y
    lda $A100,x
    sta PLYEVL,y
    sta EXP,y
    lda $A200,x
    sta $DE40,y
    sta $DEC0,y
    lda $A300,x
    sta $DF40,y
    sta $DFC0,y
    lda $A400,x
    sta $E040,y
    sta $E0C0,y
    lda $A500,x
    sta $E140,y
    sta $E1C0,y
    lda $A600,x
    sta $E240,y
    sta $E2C0,y
    lda $A700,x
    sta $E340,y
    sta $E3C0,y
    lda $A800,x
    sta CASETV,y
    sta $E4C0,y
    lda $A900,x
    sta $E540,y
    sta $E5C0,y
    lda $AA00,x
    sta $E640,y
    sta $E6C0,y
    lda $AB00,x
    sta $E740,y
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    sta $E8C0,y
    lda $AD00,x
    sta $E940,y
    sta $E9C0,y
    lda $AE00,x
    sta $EA40,y
    sta $EAC0,y
    lda $AF00,x
    sta $EB40,y
    sta $EBC0,y
    lda $B000,x
    sta $EC40,y
    sta $ECC0,y
    lda $B100,x
    sta $ED40,y
    sta $EDC0,y
    lda $B200,x
    sta $EE40,y
    sta $EEC0,y
    lda $B300,x
    sta $EF40,y
    sta $EFC0,y
    lda $B400,x
    sta $F040,y
    sta $F0C0,y
    lda $B500,x
    sta $F140,y
    sta $F1C0,y
    lda $B600,x
    sta $F240,y
    sta $F2C0,y
    lda $B700,x
    sta $F340,y
    sta $F3C0,y
    lda $B800,x
    sta $F440,y
    sta $F4C0,y
    lda $B900,x
    sta $F540,y
    sta $F5C0,y
    lda $BA00,x
    sta $F640,y
    sta $F6C0,y
    lda $BB00,x
    sta $F740,y
    sta $F7C0,y
    rts
    lda $9C00,x
    sta $D8C0,y
    sta $D940,y
    lda $9D00,x
    sta $D9C0,y
    sta $DA40,y
    lda $9E00,x
    sta $DAC0,y
    sta $DB40,y
    lda $9F00,x
    sta $DBC0,y
    sta $DC40,y
    lda $A000,x
    sta $DCC0,y
    sta PLYEVL,y
    lda $A100,x
    sta EXP,y
    sta $DE40,y
    lda $A200,x
    sta $DEC0,y
    sta $DF40,y
    lda $A300,x
    sta $DFC0,y
    lda $A400,x
    sta $E040,y
    sta $E0C0,y
    lda $A500,x
    sta $E140,y
    sta $E1C0,y
    lda $A600,x
    sta $E240,y
    sta $E2C0,y
    lda $A700,x
    sta $E340,y
    sta $E3C0,y
    lda $A800,x
    sta CASETV,y
    sta $E4C0,y
    lda $A900,x
    sta $E540,y
    sta $E5C0,y
    lda $AA00,x
    sta $E640,y
    sta $E6C0,y
    lda $AB00,x
    sta $E740,y
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    sta $E8C0,y
    lda $AD00,x
    sta $E940,y
    sta $E9C0,y
    lda $AE00,x
    sta $EA40,y
    sta $EAC0,y
    lda $AF00,x
    sta $EB40,y
    sta $EBC0,y
    lda $B000,x
    sta $EC40,y
    sta $ECC0,y
    lda $B100,x
    sta $ED40,y
    sta $EDC0,y
    lda $B200,x
    sta $EE40,y
    sta $EEC0,y
    lda $B300,x
    sta $EF40,y
    lda $B400,x
    sta $EFC0,y
    sta $F040,y
    lda $B500,x
    sta $F0C0,y
    sta $F140,y
    lda $B600,x
    sta $F1C0,y
    sta $F240,y
    lda $B700,x
    sta $F2C0,y
    sta $F340,y
    lda $B800,x
    sta $F3C0,y
    sta $F440,y
    lda $B900,x
    sta $F4C0,y
    sta $F540,y
    lda $BA00,x
    sta $F5C0,y
    sta $F640,y
    lda $BB00,x
    sta $F6C0,y
    sta $F740,y
    rts
    lda $9C00,x
    sta $D940,y
    sta $D9C0,y
    lda $9D00,x
    sta $DA40,y
    sta $DAC0,y
    lda $9E00,x
    sta $DB40,y
    sta $DBC0,y
    lda $9F00,x
    sta $DC40,y
    lda $A000,x
    sta $DCC0,y
    sta PLYEVL,y
    lda $A100,x
    sta EXP,y
    sta $DE40,y
    lda $A200,x
    sta $DEC0,y
    sta $DF40,y
    lda $A300,x
    sta $DFC0,y
    sta $E040,y
    lda $A400,x
    sta $E0C0,y
    sta $E140,y
    lda $A500,x
    sta $E1C0,y
    sta $E240,y
    lda $A600,x
    sta $E2C0,y
    sta $E340,y
    lda $A700,x
    sta $E3C0,y
    lda $A800,x
    sta CASETV,y
    sta $E4C0,y
    lda $A900,x
    sta $E540,y
    sta $E5C0,y
    lda $AA00,x
    sta $E640,y
    sta $E6C0,y
    lda $AB00,x
    sta $E740,y
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    sta $E8C0,y
    lda $AD00,x
    sta $E940,y
    sta $E9C0,y
    lda $AE00,x
    sta $EA40,y
    sta $EAC0,y
    lda $AF00,x
    sta $EB40,y
    lda $B000,x
    sta $EBC0,y
    sta $EC40,y
    lda $B100,x
    sta $ECC0,y
    sta $ED40,y
    lda $B200,x
    sta $EDC0,y
    sta $EE40,y
    lda $B300,x
    sta $EEC0,y
    sta $EF40,y
    lda $B400,x
    sta $EFC0,y
    sta $F040,y
    lda $B500,x
    sta $F0C0,y
    sta $F140,y
    lda $B600,x
    sta $F1C0,y
    sta $F240,y
    lda $B700,x
    sta $F2C0,y
    lda $B800,x
    sta $F340,y
    sta $F3C0,y
    lda $B900,x
    sta $F440,y
    sta $F4C0,y
    lda $BA00,x
    sta $F540,y
    sta $F5C0,y
    lda $BB00,x
    sta $F640,y
    sta $F6C0,y
    rts
    lda $9C00,x
    sta $D9C0,y
    sta $DA40,y
    lda $9D00,x
    sta $DAC0,y
    sta $DB40,y
    lda $9E00,x
    sta $DBC0,y
    lda $9F00,x
    sta $DC40,y
    sta $DCC0,y
    lda $A000,x
    sta PLYEVL,y
    sta EXP,y
    lda $A100,x
    sta $DE40,y
    sta $DEC0,y
    lda $A200,x
    sta $DF40,y
    sta $DFC0,y
    lda $A300,x
    sta $E040,y
    lda $A400,x
    sta $E0C0,y
    sta $E140,y
    lda $A500,x
    sta $E1C0,y
    sta $E240,y
    lda $A600,x
    sta $E2C0,y
    sta $E340,y
    lda $A700,x
    sta $E3C0,y
    sta CASETV,y
    lda $A800,x
    sta $E4C0,y
    sta $E540,y
    lda $A900,x
    sta $E5C0,y
    lda $AA00,x
    sta $E640,y
    sta $E6C0,y
    lda $AB00,x
    sta $E740,y
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    sta $E8C0,y
    lda $AD00,x
    sta $E940,y
    sta $E9C0,y
    lda $AE00,x
    sta $EA40,y
    lda $AF00,x
    sta $EAC0,y
    sta $EB40,y
    lda $B000,x
    sta $EBC0,y
    sta $EC40,y
    lda $B100,x
    sta $ECC0,y
    sta $ED40,y
    lda $B200,x
    sta $EDC0,y
    sta $EE40,y
    lda $B300,x
    sta $EEC0,y
    lda $B400,x
    sta $EF40,y
    sta $EFC0,y
    lda $B500,x
    sta $F040,y
    sta $F0C0,y
    lda $B600,x
    sta $F140,y
    sta $F1C0,y
    lda $B700,x
    sta $F240,y
    sta $F2C0,y
    lda $B800,x
    sta $F340,y
    sta $F3C0,y
    lda $B900,x
    sta $F440,y
    lda $BA00,x
    sta $F4C0,y
    sta $F540,y
    lda $BB00,x
    sta $F5C0,y
    sta $F640,y
    rts
    lda $9C00,x
    sta $DA40,y
    sta $DAC0,y
    lda $9D00,x
    sta $DB40,y
    lda $9E00,x
    sta $DBC0,y
    sta $DC40,y
    lda $9F00,x
    sta $DCC0,y
    sta PLYEVL,y
    lda $A000,x
    sta EXP,y
    sta $DE40,y
    lda $A100,x
    sta $DEC0,y
    lda $A200,x
    sta $DF40,y
    sta $DFC0,y
    lda $A300,x
    sta $E040,y
    sta $E0C0,y
    lda $A400,x
    sta $E140,y
    sta $E1C0,y
    lda $A500,x
    sta $E240,y
    lda $A600,x
    sta $E2C0,y
    sta $E340,y
    lda $A700,x
    sta $E3C0,y
    sta CASETV,y
    lda $A800,x
    sta $E4C0,y
    sta $E540,y
    lda $A900,x
    sta $E5C0,y
    lda $AA00,x
    sta $E640,y
    sta $E6C0,y
    lda $AB00,x
    sta $E740,y
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    sta $E8C0,y
    lda $AD00,x
    sta $E940,y
    lda $AE00,x
    sta $E9C0,y
    sta $EA40,y
    lda $AF00,x
    sta $EAC0,y
    sta $EB40,y
    lda $B000,x
    sta $EBC0,y
    sta $EC40,y
    lda $B100,x
    sta $ECC0,y
    lda $B200,x
    sta $ED40,y
    sta $EDC0,y
    lda $B300,x
    sta $EE40,y
    sta $EEC0,y
    lda $B400,x
    sta $EF40,y
    sta $EFC0,y
    lda $B500,x
    sta $F040,y
    lda $B600,x
    sta $F0C0,y
    sta $F140,y
    lda $B700,x
    sta $F1C0,y
    sta $F240,y
    lda $B800,x
    sta $F2C0,y
    sta $F340,y
    lda $B900,x
    sta $F3C0,y
    lda $BA00,x
    sta $F440,y
    sta $F4C0,y
    lda $BB00,x
    sta $F540,y
    sta $F5C0,y
    rts
    lda $9C00,x
    sta $DAC0,y
    sta $DB40,y
    lda $9D00,x
    sta $DBC0,y
    lda $9E00,x
    sta $DC40,y
    sta $DCC0,y
    lda $9F00,x
    sta PLYEVL,y
    sta EXP,y
    lda $A000,x
    sta $DE40,y
    lda $A100,x
    sta $DEC0,y
    sta $DF40,y
    lda $A200,x
    sta $DFC0,y
    sta $E040,y
    lda $A300,x
    sta $E0C0,y
    lda $A400,x
    sta $E140,y
    sta $E1C0,y
    lda $A500,x
    sta $E240,y
    sta $E2C0,y
    lda $A600,x
    sta $E340,y
    sta $E3C0,y
    lda $A700,x
    sta CASETV,y
    lda $A800,x
    sta $E4C0,y
    sta $E540,y
    lda $A900,x
    sta $E5C0,y
    sta $E640,y
    lda $AA00,x
    sta $E6C0,y
    lda $AB00,x
    sta $E740,y
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    sta $E8C0,y
    lda $AD00,x
    sta $E940,y
    lda $AE00,x
    sta $E9C0,y
    sta $EA40,y
    lda $AF00,x
    sta $EAC0,y
    sta $EB40,y
    lda $B000,x
    sta $EBC0,y
    lda $B100,x
    sta $EC40,y
    sta $ECC0,y
    lda $B200,x
    sta $ED40,y
    sta $EDC0,y
    lda $B300,x
    sta $EE40,y
    lda $B400,x
    sta $EEC0,y
    sta $EF40,y
    lda $B500,x
    sta $EFC0,y
    sta $F040,y
    lda $B600,x
    sta $F0C0,y
    sta $F140,y
    lda $B700,x
    sta $F1C0,y
    lda $B800,x
    sta $F240,y
    sta $F2C0,y
    lda $B900,x
    sta $F340,y
    sta $F3C0,y
    lda $BA00,x
    sta $F440,y
    lda $BB00,x
    sta $F4C0,y
    sta $F540,y
    rts
    lda $9C00,x
    sta $DB40,y
    sta $DBC0,y
    lda $9D00,x
    sta $DC40,y
    lda $9E00,x
    sta $DCC0,y
    sta PLYEVL,y
    lda $9F00,x
    sta EXP,y
    lda $A000,x
    sta $DE40,y
    sta $DEC0,y
    lda $A100,x
    sta $DF40,y
    sta $DFC0,y
    lda $A200,x
    sta $E040,y
    lda $A300,x
    sta $E0C0,y
    sta $E140,y
    lda $A400,x
    sta $E1C0,y
    sta $E240,y
    lda $A500,x
    sta $E2C0,y
    lda $A600,x
    sta $E340,y
    sta $E3C0,y
    lda $A700,x
    sta CASETV,y
    lda $A800,x
    sta $E4C0,y
    sta $E540,y
    lda $A900,x
    sta $E5C0,y
    sta $E640,y
    lda $AA00,x
    sta $E6C0,y
    lda $AB00,x
    sta $E740,y
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    sta $E8C0,y
    lda $AD00,x
    sta $E940,y
    lda $AE00,x
    sta $E9C0,y
    sta $EA40,y
    lda $AF00,x
    sta $EAC0,y
    lda $B000,x
    sta $EB40,y
    sta $EBC0,y
    lda $B100,x
    sta $EC40,y
    sta $ECC0,y
    lda $B200,x
    sta $ED40,y
    lda $B300,x
    sta $EDC0,y
    sta $EE40,y
    lda $B400,x
    sta $EEC0,y
    sta $EF40,y
    lda $B500,x
    sta $EFC0,y
    lda $B600,x
    sta $F040,y
    sta $F0C0,y
    lda $B700,x
    sta $F140,y
    lda $B800,x
    sta $F1C0,y
    sta $F240,y
    lda $B900,x
    sta $F2C0,y
    sta $F340,y
    lda $BA00,x
    sta $F3C0,y
    lda $BB00,x
    sta $F440,y
    sta $F4C0,y
    rts
    lda $9C00,x
    sta $DBC0,y
    sta $DC40,y
    lda $9D00,x
    sta $DCC0,y
    lda $9E00,x
    sta PLYEVL,y
    sta EXP,y
    lda $9F00,x
    sta $DE40,y
    lda $A000,x
    sta $DEC0,y
    sta $DF40,y
    lda $A100,x
    sta $DFC0,y
    lda $A200,x
    sta $E040,y
    sta $E0C0,y
    lda $A300,x
    sta $E140,y
    lda $A400,x
    sta $E1C0,y
    sta $E240,y
    lda $A500,x
    sta $E2C0,y
    sta $E340,y
    lda $A600,x
    sta $E3C0,y
    lda $A700,x
    sta CASETV,y
    sta $E4C0,y
    lda $A800,x
    sta $E540,y
    lda $A900,x
    sta $E5C0,y
    sta $E640,y
    lda $AA00,x
    sta $E6C0,y
    lda $AB00,x
    sta $E740,y
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    sta $E8C0,y
    lda $AD00,x
    sta $E940,y
    lda $AE00,x
    sta $E9C0,y
    sta $EA40,y
    lda $AF00,x
    sta $EAC0,y
    lda $B000,x
    sta $EB40,y
    sta $EBC0,y
    lda $B100,x
    sta $EC40,y
    lda $B200,x
    sta $ECC0,y
    sta $ED40,y
    lda $B300,x
    sta $EDC0,y
    lda $B400,x
    sta $EE40,y
    sta $EEC0,y
    lda $B500,x
    sta $EF40,y
    sta $EFC0,y
    lda $B600,x
    sta $F040,y
    lda $B700,x
    sta $F0C0,y
    sta $F140,y
    lda $B800,x
    sta $F1C0,y
    lda $B900,x
    sta $F240,y
    sta $F2C0,y
    lda $BA00,x
    sta $F340,y
    lda $BB00,x
    sta $F3C0,y
    sta $F440,y
    rts
    lda $9C00,x
    sta $DC40,y
    lda $9D00,x
    sta $DCC0,y
    sta PLYEVL,y
    lda $9E00,x
    sta EXP,y
    lda $9F00,x
    sta $DE40,y
    sta $DEC0,y
    lda $A000,x
    sta $DF40,y
    lda $A100,x
    sta $DFC0,y
    sta $E040,y
    lda $A200,x
    sta $E0C0,y
    lda $A300,x
    sta $E140,y
    sta $E1C0,y
    lda $A400,x
    sta $E240,y
    lda $A500,x
    sta $E2C0,y
    sta $E340,y
    lda $A600,x
    sta $E3C0,y
    lda $A700,x
    sta CASETV,y
    sta $E4C0,y
    lda $A800,x
    sta $E540,y
    lda $A900,x
    sta $E5C0,y
    sta $E640,y
    lda $AA00,x
    sta $E6C0,y
    lda $AB00,x
    sta $E740,y
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    lda $AD00,x
    sta $E8C0,y
    sta $E940,y
    lda $AE00,x
    sta $E9C0,y
    lda $AF00,x
    sta $EA40,y
    sta $EAC0,y
    lda $B000,x
    sta $EB40,y
    lda $B100,x
    sta $EBC0,y
    sta $EC40,y
    lda $B200,x
    sta $ECC0,y
    lda $B300,x
    sta $ED40,y
    sta $EDC0,y
    lda $B400,x
    sta $EE40,y
    lda $B500,x
    sta $EEC0,y
    sta $EF40,y
    lda $B600,x
    sta $EFC0,y
    lda $B700,x
    sta $F040,y
    sta $F0C0,y
    lda $B800,x
    sta $F140,y
    lda $B900,x
    sta $F1C0,y
    sta $F240,y
    lda $BA00,x
    sta $F2C0,y
    lda $BB00,x
    sta $F340,y
    sta $F3C0,y
    rts
    lda $9C00,x
    sta $DCC0,y
    lda $9D00,x
    sta PLYEVL,y
    sta EXP,y
    lda $9E00,x
    sta $DE40,y
    lda $9F00,x
    sta $DEC0,y
    sta $DF40,y
    lda $A000,x
    sta $DFC0,y
    lda $A100,x
    sta $E040,y
    sta $E0C0,y
    lda $A200,x
    sta $E140,y
    lda $A300,x
    sta $E1C0,y
    lda $A400,x
    sta $E240,y
    sta $E2C0,y
    lda $A500,x
    sta $E340,y
    lda $A600,x
    sta $E3C0,y
    sta CASETV,y
    lda $A700,x
    sta $E4C0,y
    lda $A800,x
    sta $E540,y
    sta $E5C0,y
    lda $A900,x
    sta $E640,y
    lda $AA00,x
    sta $E6C0,y
    sta $E740,y
    lda $AB00,x
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    lda $AD00,x
    sta $E8C0,y
    sta $E940,y
    lda $AE00,x
    sta $E9C0,y
    lda $AF00,x
    sta $EA40,y
    sta $EAC0,y
    lda $B000,x
    sta $EB40,y
    lda $B100,x
    sta $EBC0,y
    sta $EC40,y
    lda $B200,x
    sta $ECC0,y
    lda $B300,x
    sta $ED40,y
    lda $B400,x
    sta $EDC0,y
    sta $EE40,y
    lda $B500,x
    sta $EEC0,y
    lda $B600,x
    sta $EF40,y
    sta $EFC0,y
    lda $B700,x
    sta $F040,y
    lda $B800,x
    sta $F0C0,y
    sta $F140,y
    lda $B900,x
    sta $F1C0,y
    lda $BA00,x
    sta $F240,y
    sta $F2C0,y
    lda $BB00,x
    sta $F340,y
    rts
    lda $9C00,x
    sta PLYEVL,y
    lda $9D00,x
    sta EXP,y
    sta $DE40,y
    lda $9E00,x
    sta $DEC0,y
    lda $9F00,x
    sta $DF40,y
    lda $A000,x
    sta $DFC0,y
    sta $E040,y
    lda $A100,x
    sta $E0C0,y
    lda $A200,x
    sta $E140,y
    sta $E1C0,y
    lda $A300,x
    sta $E240,y
    lda $A400,x
    sta $E2C0,y
    lda $A500,x
    sta $E340,y
    sta $E3C0,y
    lda $A600,x
    sta CASETV,y
    lda $A700,x
    sta $E4C0,y
    lda $A800,x
    sta $E540,y
    sta $E5C0,y
    lda $A900,x
    sta $E640,y
    lda $AA00,x
    sta $E6C0,y
    sta $E740,y
    lda $AB00,x
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    lda $AD00,x
    sta $E8C0,y
    sta $E940,y
    lda $AE00,x
    sta $E9C0,y
    lda $AF00,x
    sta $EA40,y
    lda $B000,x
    sta $EAC0,y
    sta $EB40,y
    lda $B100,x
    sta $EBC0,y
    lda $B200,x
    sta $EC40,y
    sta $ECC0,y
    lda $B300,x
    sta $ED40,y
    lda $B400,x
    sta $EDC0,y
    lda $B500,x
    sta $EE40,y
    sta $EEC0,y
    lda $B600,x
    sta $EF40,y
    lda $B700,x
    sta $EFC0,y
    lda $B800,x
    sta $F040,y
    sta $F0C0,y
    lda $B900,x
    sta $F140,y
    lda $BA00,x
    sta $F1C0,y
    sta $F240,y
    lda $BB00,x
    sta $F2C0,y
    rts
    lda $9C00,x
    sta EXP,y
    lda $9D00,x
    sta $DE40,y
    sta $DEC0,y
    lda $9E00,x
    sta $DF40,y
    lda $9F00,x
    sta $DFC0,y
    lda $A000,x
    sta $E040,y
    sta $E0C0,y
    lda $A100,x
    sta $E140,y
    lda $A200,x
    sta $E1C0,y
    lda $A300,x
    sta $E240,y
    lda $A400,x
    sta $E2C0,y
    sta $E340,y
    lda $A500,x
    sta $E3C0,y
    lda $A600,x
    sta CASETV,y
    lda $A700,x
    sta $E4C0,y
    sta $E540,y
    lda $A800,x
    sta $E5C0,y
    lda $A900,x
    sta $E640,y
    lda $AA00,x
    sta $E6C0,y
    sta $E740,y
    lda $AB00,x
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    lda $AD00,x
    sta $E8C0,y
    sta $E940,y
    lda $AE00,x
    sta $E9C0,y
    lda $AF00,x
    sta $EA40,y
    lda $B000,x
    sta $EAC0,y
    sta $EB40,y
    lda $B100,x
    sta $EBC0,y
    lda $B200,x
    sta $EC40,y
    lda $B300,x
    sta $ECC0,y
    lda $B400,x
    sta $ED40,y
    sta $EDC0,y
    lda $B500,x
    sta $EE40,y
    lda $B600,x
    sta $EEC0,y
    lda $B700,x
    sta $EF40,y
    sta $EFC0,y
    lda $B800,x
    sta $F040,y
    lda $B900,x
    sta $F0C0,y
    lda $BA00,x
    sta $F140,y
    sta $F1C0,y
    lda $BB00,x
    sta $F240,y
    rts
    lda $9C00,x
    sta $DE40,y
    lda $9D00,x
    sta $DEC0,y
    lda $9E00,x
    sta $DF40,y
    sta $DFC0,y
    lda $9F00,x
    sta $E040,y
    lda $A000,x
    sta $E0C0,y
    lda $A100,x
    sta $E140,y
    lda $A200,x
    sta $E1C0,y
    sta $E240,y
    lda $A300,x
    sta $E2C0,y
    lda $A400,x
    sta $E340,y
    lda $A500,x
    sta $E3C0,y
    lda $A600,x
    sta CASETV,y
    sta $E4C0,y
    lda $A700,x
    sta $E540,y
    lda $A800,x
    sta $E5C0,y
    lda $A900,x
    sta $E640,y
    lda $AA00,x
    sta $E6C0,y
    sta $E740,y
    lda $AB00,x
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    lda $AD00,x
    sta $E8C0,y
    lda $AE00,x
    sta $E940,y
    sta $E9C0,y
    lda $AF00,x
    sta $EA40,y
    lda $B000,x
    sta $EAC0,y
    lda $B100,x
    sta $EB40,y
    lda $B200,x
    sta $EBC0,y
    sta $EC40,y
    lda $B300,x
    sta $ECC0,y
    lda $B400,x
    sta $ED40,y
    lda $B500,x
    sta $EDC0,y
    lda $B600,x
    sta $EE40,y
    sta $EEC0,y
    lda $B700,x
    sta $EF40,y
    lda $B800,x
    sta $EFC0,y
    lda $B900,x
    sta $F040,y
    lda $BA00,x
    sta $F0C0,y
    sta $F140,y
    lda $BB00,x
    sta $F1C0,y
    rts
    lda $9C00,x
    sta $DEC0,y
    lda $9D00,x
    sta $DF40,y
    lda $9E00,x
    sta $DFC0,y
    sta $E040,y
    lda $9F00,x
    sta $E0C0,y
    lda $A000,x
    sta $E140,y
    lda $A100,x
    sta $E1C0,y
    lda $A200,x
    sta $E240,y
    lda $A300,x
    sta $E2C0,y
    lda $A400,x
    sta $E340,y
    sta $E3C0,y
    lda $A500,x
    sta CASETV,y
    lda $A600,x
    sta $E4C0,y
    lda $A700,x
    sta $E540,y
    lda $A800,x
    sta $E5C0,y
    lda $A900,x
    sta $E640,y
    sta $E6C0,y
    lda $AA00,x
    sta $E740,y
    lda $AB00,x
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    lda $AD00,x
    sta $E8C0,y
    lda $AE00,x
    sta $E940,y
    sta $E9C0,y
    lda $AF00,x
    sta $EA40,y
    lda $B000,x
    sta $EAC0,y
    lda $B100,x
    sta $EB40,y
    lda $B200,x
    sta $EBC0,y
    lda $B300,x
    sta $EC40,y
    lda $B400,x
    sta $ECC0,y
    sta $ED40,y
    lda $B500,x
    sta $EDC0,y
    lda $B600,x
    sta $EE40,y
    lda $B700,x
    sta $EEC0,y
    lda $B800,x
    sta $EF40,y
    lda $B900,x
    sta $EFC0,y
    sta $F040,y
    lda $BA00,x
    sta $F0C0,y
    lda $BB00,x
    sta $F140,y
    rts
    lda $9C00,x
    sta $DF40,y
    lda $9D00,x
    sta $DFC0,y
    lda $9E00,x
    sta $E040,y
    lda $9F00,x
    sta $E0C0,y
    lda $A000,x
    sta $E140,y
    sta $E1C0,y
    lda $A100,x
    sta $E240,y
    lda $A200,x
    sta $E2C0,y
    lda $A300,x
    sta $E340,y
    lda $A400,x
    sta $E3C0,y
    lda $A500,x
    sta CASETV,y
    lda $A600,x
    sta $E4C0,y
    lda $A700,x
    sta $E540,y
    lda $A800,x
    sta $E5C0,y
    sta $E640,y
    lda $A900,x
    sta $E6C0,y
    lda $AA00,x
    sta $E740,y
    lda $AB00,x
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    lda $AD00,x
    sta $E8C0,y
    lda $AE00,x
    sta $E940,y
    lda $AF00,x
    sta $E9C0,y
    lda $B000,x
    sta $EA40,y
    sta $EAC0,y
    lda $B100,x
    sta $EB40,y
    lda $B200,x
    sta $EBC0,y
    lda $B300,x
    sta $EC40,y
    lda $B400,x
    sta $ECC0,y
    lda $B500,x
    sta $ED40,y
    lda $B600,x
    sta $EDC0,y
    lda $B700,x
    sta $EE40,y
    lda $B800,x
    sta $EEC0,y
    sta $EF40,y
    lda $B900,x
    sta $EFC0,y
    lda $BA00,x
    sta $F040,y
    lda $BB00,x
    sta $F0C0,y
    rts
    lda $9C00,x
    sta $DFC0,y
    lda $9D00,x
    sta $E040,y
    lda $9E00,x
    sta $E0C0,y
    lda $9F00,x
    sta $E140,y
    lda $A000,x
    sta $E1C0,y
    lda $A100,x
    sta $E240,y
    lda $A200,x
    sta $E2C0,y
    lda $A300,x
    sta $E340,y
    lda $A400,x
    sta $E3C0,y
    sta CASETV,y
    lda $A500,x
    sta $E4C0,y
    lda $A600,x
    sta $E540,y
    lda $A700,x
    sta $E5C0,y
    lda $A800,x
    sta $E640,y
    lda $A900,x
    sta $E6C0,y
    lda $AA00,x
    sta $E740,y
    lda $AB00,x
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    lda $AD00,x
    sta $E8C0,y
    lda $AE00,x
    sta $E940,y
    lda $AF00,x
    sta $E9C0,y
    lda $B000,x
    sta $EA40,y
    lda $B100,x
    sta $EAC0,y
    lda $B200,x
    sta $EB40,y
    lda $B300,x
    sta $EBC0,y
    lda $B400,x
    sta $EC40,y
    sta $ECC0,y
    lda $B500,x
    sta $ED40,y
    lda $B600,x
    sta $EDC0,y
    lda $B700,x
    sta $EE40,y
    lda $B800,x
    sta $EEC0,y
    lda $B900,x
    sta $EF40,y
    lda $BA00,x
    sta $EFC0,y
    lda $BB00,x
    sta $F040,y
    rts
    lda $9C00,x
    sta $E040,y
    lda $9D00,x
    sta $E0C0,y
    lda $9E00,x
    sta $E140,y
    lda $9F00,x
    sta $E1C0,y
    lda $A000,x
    sta $E240,y
    lda $A100,x
    sta $E2C0,y
    lda $A200,x
    sta $E340,y
    lda $A300,x
    sta $E3C0,y
    lda $A400,x
    sta CASETV,y
    lda $A500,x
    sta $E4C0,y
    lda $A600,x
    sta $E540,y
    lda $A700,x
    sta $E5C0,y
    lda $A800,x
    sta $E640,y
    lda $A900,x
    sta $E6C0,y
    lda $AA00,x
    sta $E740,y
    lda $AB00,x
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    lda $AD00,x
    sta $E8C0,y
    lda $AE00,x
    sta $E940,y
    lda $AF00,x
    sta $E9C0,y
    lda $B000,x
    sta $EA40,y
    lda $B100,x
    sta $EAC0,y
    lda $B200,x
    sta $EB40,y
    lda $B300,x
    sta $EBC0,y
    lda $B400,x
    sta $EC40,y
    lda $B500,x
    sta $ECC0,y
    lda $B600,x
    sta $ED40,y
    lda $B700,x
    sta $EDC0,y
    lda $B800,x
    sta $EE40,y
    lda $B900,x
    sta $EEC0,y
    lda $BA00,x
    sta $EF40,y
    lda $BB00,x
    sta $EFC0,y
    rts
    lda $9C00,x
    sta $E0C0,y
    lda $9D00,x
    sta $E140,y
    lda $9E00,x
    sta $E1C0,y
    lda $9F00,x
    sta $E240,y
    lda $A000,x
    sta $E2C0,y
    lda $A100,x
    sta $E340,y
    lda $A200,x
    sta $E3C0,y
    lda $A400,x
    sta CASETV,y
    lda $A500,x
    sta $E4C0,y
    lda $A600,x
    sta $E540,y
    lda $A700,x
    sta $E5C0,y
    lda $A800,x
    sta $E640,y
    lda $A900,x
    sta $E6C0,y
    lda $AA00,x
    sta $E740,y
    lda $AB00,x
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    lda $AD00,x
    sta $E8C0,y
    lda $AE00,x
    sta $E940,y
    lda $AF00,x
    sta $E9C0,y
    lda $B000,x
    sta $EA40,y
    lda $B100,x
    sta $EAC0,y
    lda $B200,x
    sta $EB40,y
    lda $B400,x
    sta $EBC0,y
    lda $B500,x
    sta $EC40,y
    lda $B600,x
    sta $ECC0,y
    lda $B700,x
    sta $ED40,y
    lda $B800,x
    sta $EDC0,y
    lda $B900,x
    sta $EE40,y
    lda $BA00,x
    sta $EEC0,y
    lda $BB00,x
    sta $EF40,y
    rts
    lda $9C00,x
    sta $E140,y
    lda $9D00,x
    sta $E1C0,y
    lda $9E00,x
    sta $E240,y
    lda $A000,x
    sta $E2C0,y
    lda $A100,x
    sta $E340,y
    lda $A200,x
    sta $E3C0,y
    lda $A300,x
    sta CASETV,y
    lda $A400,x
    sta $E4C0,y
    lda $A500,x
    sta $E540,y
    lda $A600,x
    sta $E5C0,y
    lda $A800,x
    sta $E640,y
    lda $A900,x
    sta $E6C0,y
    lda $AA00,x
    sta $E740,y
    lda $AB00,x
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    lda $AD00,x
    sta $E8C0,y
    lda $AE00,x
    sta $E940,y
    lda $B000,x
    sta $E9C0,y
    lda $B100,x
    sta $EA40,y
    lda $B200,x
    sta $EAC0,y
    lda $B300,x
    sta $EB40,y
    lda $B400,x
    sta $EBC0,y
    lda $B500,x
    sta $EC40,y
    lda $B600,x
    sta $ECC0,y
    lda $B800,x
    sta $ED40,y
    lda $B900,x
    sta $EDC0,y
    lda $BA00,x
    sta $EE40,y
    lda $BB00,x
    sta $EEC0,y
    rts
    lda $9C00,x
    sta $E1C0,y
    lda $9D00,x
    sta $E240,y
    lda $9F00,x
    sta $E2C0,y
    lda $A000,x
    sta $E340,y
    lda $A100,x
    sta $E3C0,y
    lda $A200,x
    sta CASETV,y
    lda $A400,x
    sta $E4C0,y
    lda $A500,x
    sta $E540,y
    lda $A600,x
    sta $E5C0,y
    lda $A700,x
    sta $E640,y
    lda $A800,x
    sta $E6C0,y
    lda $AA00,x
    sta $E740,y
    lda $AB00,x
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    lda $AD00,x
    sta $E8C0,y
    lda $AF00,x
    sta $E940,y
    lda $B000,x
    sta $E9C0,y
    lda $B100,x
    sta $EA40,y
    lda $B200,x
    sta $EAC0,y
    lda $B400,x
    sta $EB40,y
    lda $B500,x
    sta $EBC0,y
    lda $B600,x
    sta $EC40,y
    lda $B700,x
    sta $ECC0,y
    lda $B800,x
    sta $ED40,y
    lda $BA00,x
    sta $EDC0,y
    lda $BB00,x
    sta $EE40,y
    rts
    lda $9C00,x
    sta $E240,y
    lda $9E00,x
    sta $E2C0,y
    lda $9F00,x
    sta $E340,y
    lda $A000,x
    sta $E3C0,y
    lda $A200,x
    sta CASETV,y
    lda $A300,x
    sta $E4C0,y
    lda $A400,x
    sta $E540,y
    lda $A600,x
    sta $E5C0,y
    lda $A700,x
    sta $E640,y
    lda $A800,x
    sta $E6C0,y
    lda $AA00,x
    sta $E740,y
    lda $AB00,x
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    lda $AE00,x
    sta $E8C0,y
    lda $AF00,x
    sta $E940,y
    lda $B000,x
    sta $E9C0,y
    lda $B200,x
    sta $EA40,y
    lda $B300,x
    sta $EAC0,y
    lda $B400,x
    sta $EB40,y
    lda $B600,x
    sta $EBC0,y
    lda $B700,x
    sta $EC40,y
    lda $B800,x
    sta $ECC0,y
    lda $BA00,x
    sta $ED40,y
    lda $BB00,x
    sta $EDC0,y
    rts
    lda $9C00,x
    sta $E2C0,y
    lda $9E00,x
    sta $E340,y
    lda $9F00,x
    sta $E3C0,y
    lda $A100,x
    sta CASETV,y
    lda $A200,x
    sta $E4C0,y
    lda $A400,x
    sta $E540,y
    lda $A500,x
    sta $E5C0,y
    lda $A600,x
    sta $E640,y
    lda $A800,x
    sta $E6C0,y
    lda $A900,x
    sta $E740,y
    lda $AB00,x
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    lda $AE00,x
    sta $E8C0,y
    lda $AF00,x
    sta $E940,y
    lda $B100,x
    sta $E9C0,y
    lda $B200,x
    sta $EA40,y
    lda $B400,x
    sta $EAC0,y
    lda $B500,x
    sta $EB40,y
    lda $B600,x
    sta $EBC0,y
    lda $B800,x
    sta $EC40,y
    lda $B900,x
    sta $ECC0,y
    lda $BB00,x
    sta $ED40,y
    rts
    lda $9C00,x
    sta $E340,y
    lda $9E00,x
    sta $E3C0,y
    lda $A000,x
    sta CASETV,y
    lda $A100,x
    sta $E4C0,y
    lda $A300,x
    sta $E540,y
    lda $A400,x
    sta $E5C0,y
    lda $A600,x
    sta $E640,y
    lda $A800,x
    sta $E6C0,y
    lda $A900,x
    sta $E740,y
    lda $AB00,x
    sta $E7C0,y
    lda $AC00,x
    sta $E840,y
    lda $AE00,x
    sta $E8C0,y
    lda $B000,x
    sta $E940,y
    lda $B100,x
    sta $E9C0,y
    lda $B300,x
    sta $EA40,y
    lda $B400,x
    sta $EAC0,y
    lda $B600,x
    sta $EB40,y
    lda $B800,x
    sta $EBC0,y
    lda $B900,x
    sta $EC40,y
    lda $BB00,x
    sta $ECC0,y
    rts
    lda $BC00,x
    sta $E3C0,y
    lda $BC40,x
    sta CASETV,y
    lda $BC80,x
    sta $E4C0,y
    lda $BCC0,x
    sta $E540,y
    lda $BD00,x
    sta $E5C0,y
    lda $BD00,x
    sta $E640,y
    lda $BD40,x
    sta $E6C0,y
    lda $BD80,x
    sta $E740,y
    lda $BDC0,x
    sta $E7C0,y
    lda $BE00,x
    sta $E840,y
    lda $BE40,x
    sta $E8C0,y
    lda $BE80,x
    sta $E940,y
    lda $BEC0,x
    sta $E9C0,y
    lda $BF00,x
    sta $EA40,y
    lda $BF00,x
    sta $EAC0,y
    lda $BF40,x
    sta $EB40,y
    lda $BF80,x
    sta $EBC0,y
    lda $BFC0,x
    sta $EC40,y
    rts
    lda $BC00,x
    sta CASETV,y
    lda $BC40,x
    sta $E4C0,y
    lda $BC80,x
    sta $E540,y
    lda $BCC0,x
    sta $E5C0,y
    lda $BD00,x
    sta $E640,y
    lda $BD40,x
    sta $E6C0,y
    lda $BD80,x
    sta $E740,y
    lda $BDC0,x
    sta $E7C0,y
    lda $BE00,x
    sta $E840,y
    lda $BE40,x
    sta $E8C0,y
    lda $BE80,x
    sta $E940,y
    lda $BEC0,x
    sta $E9C0,y
    lda $BF00,x
    sta $EA40,y
    lda $BF40,x
    sta $EAC0,y
    lda $BF80,x
    sta $EB40,y
    lda $BFC0,x
    sta $EBC0,y
    rts
    lda $BC00,x
    sta $E4C0,y
    lda $BC40,x
    sta $E540,y
    lda $BC80,x
    sta $E5C0,y
    lda $BD00,x
    sta $E640,y
    lda $BD40,x
    sta $E6C0,y
    lda $BD80,x
    sta $E740,y
    lda $BDC0,x
    sta $E7C0,y
    lda $BE00,x
    sta $E840,y
    lda $BE40,x
    sta $E8C0,y
    lda $BE80,x
    sta $E940,y
    lda $BF00,x
    sta $E9C0,y
    lda $BF40,x
    sta $EA40,y
    lda $BF80,x
    sta $EAC0,y
    lda $BFC0,x
    sta $EB40,y
    rts
    lda $BC00,x
    sta $E540,y
    lda $BC80,x
    sta $E5C0,y
    lda $BCC0,x
    sta $E640,y
    lda $BD00,x
    sta $E6C0,y
    lda $BD80,x
    sta $E740,y
    lda $BDC0,x
    sta $E7C0,y
    lda $BE00,x
    sta $E840,y
    lda $BE80,x
    sta $E8C0,y
    lda $BEC0,x
    sta $E940,y
    lda $BF00,x
    sta $E9C0,y
    lda $BF80,x
    sta $EA40,y
    lda $BFC0,x
    sta $EAC0,y
    rts
    lda $BC00,x
    sta $E5C0,y
    lda $BC80,x
    sta $E640,y
    lda $BD00,x
    sta $E6C0,y
    lda $BD40,x
    sta $E740,y
    lda $BDC0,x
    sta $E7C0,y
    lda $BE00,x
    sta $E840,y
    lda $BE80,x
    sta $E8C0,y
    lda $BF00,x
    sta $E940,y
    lda $BF40,x
    sta $E9C0,y
    lda $BFC0,x
    sta $EA40,y
    rts
    lda $BC40,x
    sta $E640,y
    lda $BCC0,x
    sta $E6C0,y
    lda $BD40,x
    sta $E740,y
    lda $BDC0,x
    sta $E7C0,y
    lda $BE40,x
    sta $E840,y
    lda $BEC0,x
    sta $E8C0,y
    lda $BF40,x
    sta $E940,y
    lda $BFC0,x
    sta $E9C0,y
    rts
    lda $BC40,x
    sta $E6C0,y
    lda $BD00,x
    sta $E740,y
    lda $BD80,x
    sta $E7C0,y
    lda $BE40,x
    sta $E840,y
    lda $BF00,x
    sta $E8C0,y
    lda $BF80,x
    sta $E940,y
    rts
    lda $BC80,x
    sta $E740,y
    lda $BD80,x
    sta $E7C0,y
    lda $BE80,x
    sta $E840,y
    lda $BF80,x
    sta $E8C0,y
    rts
    lda #$33
    sta $E7C0,y
    sta $E840,y
    rts
    eor loc_4059,y
    .byte $1B
    nop
    lda $0F64
    ldx $C841
    .byte $43
    .byte $B2
    ora BUFSTR,x
    .byte $B7
    inc CIOCHR,x
    .byte $62
    .byte $8F
    ldx $D7,y
    .byte $F2
    .byte $07
    asl $1F,x
    .byte $22
    .byte $1F
    asl CMCMD,x
    .byte $F2
    .byte $D7
    ldx $95,y
    .byte $74
    .byte $53
    sec
    ora $E702,x
    .byte $D2
    lda $93A8,x
    sty $75
    ror DINDEX
    lsr $3C45
    .byte $33
    bmi $89C5
    rol
    .byte $27
    rol
    and $3330
    .byte $3C
    eor DRKMSK
    .byte $57
    ror $89
    .byte $89
    .byte $89
    .byte $89
    dey
    dey
    dey
    dey
    .byte $87
    .byte $87
    stx $86
    sta $85
    sty $83
    .byte $82
    .byte $82
    sta ($80,x)
    .byte $7F
    ror $7D7D,x
    .byte $7C
    .byte $7B
    .byte $7A
    adc $7778,y
    adc ENDPT,x
    .byte $73
    .byte $72
    adc (ROWAC),y
    .byte $6F
    ror $6B6D
    ror
    adc #$68
    .byte $67
    ror $65
    .byte $64
    .byte $63
    .byte $62
    adc (FKDEF,x)
    .byte $5F
    lsr $5C5D,x
    .byte $5B
    .byte $5A
    eor $5758,y
    lsr COLCRS,x
    .byte $54
    .byte $53
    .byte $52
plot_column:
    ; PLOT COLUMN — write one column of nibble pixels to framebuffer
    ldy current_column
    lda col_texture,y
    ; Load texture base from lookup table at $9020,X
    tax
    lda $9020,x
    sta PM_DMA_CYCLES
    ; Load wall distance to compute column height
    ldx col_distance,y
    lda $8962,x
    ; Get framebuffer address from height-to-address table at $8962/$89A3
    sta TRAMSZ
    lda $89A3,x
    sta CMCMD
    bit $8C
    bmi loc_8A4C
    cpx #$21
    bcs loc_8A1F
    cpx #$0A
    bcc loc_8A2F
    lda PM_DMA_CYCLES
    bmi loc_8A70
    ; Near wall: shift texture color into high nibble, add gradient
    asl
    asl
    asl
    asl
    clc
    adc #$A0
    sta PM_DMA_CYCLES
    lda col_texcol,y
    lsr
    clc
    adc PM_DMA_CYCLES
    jmp loc_8A40
loc_8A1F:
    ; Far wall: different color mapping (5-bit shift)
    lda PM_DMA_CYCLES
    bmi loc_8A70
    asl
    asl
    asl
    asl
    asl
    clc
    adc col_texcol,y
    jmp loc_8A40
loc_8A2F:
    lda PM_DMA_CYCLES
    bmi loc_8A8D
    asl
    asl
    asl
    sta PM_DMA_CYCLES
    lda col_texcol,y
    lsr
    lsr
    clc
    adc PM_DMA_CYCLES
loc_8A40:
    tax
    bit buffer_flag
    bpl loc_8A49
    tya
    ora #$20
    tay
loc_8A49:
    jmp (TRAMSZ)
loc_8A4C:
    lda PM_DMA_CYCLES
    cmp #$90
    beq loc_8A6A
    cpx #$14
    bcs loc_8A5F
    cpx #$0A
    bcc loc_8A65
    lda #$F0
    jmp loc_8A40
loc_8A5F:
    lda $2E3D,x
    jmp loc_8A40
loc_8A65:
    lda #$28
    jmp loc_8A40
loc_8A6A:
    cpx #$0A
    bcs loc_8A85
    bcc loc_8AA2
loc_8A70:
    and #$7F
    cmp #$10
    bcc loc_8A87
    cmp #$10
    beq loc_8A85
    lda col_texcol,y
    eor #$1F
    clc
    adc #$E9
    jmp loc_8A40
loc_8A85:
    lda $7A
loc_8A87:
    clc
    adc #$F0
    jmp loc_8A40
loc_8A8D:
    and #$7F
    cmp #$10
    bcc loc_8AA4
    cmp #$10
    beq loc_8AA2
    lda col_texcol,y
    eor #$1F
    clc
    adc #$21
    jmp loc_8A40
loc_8AA2:
    lda $7A
loc_8AA4:
    clc
    adc #$28
    jmp loc_8A40
draw_all_columns:
    ; DRAW ALL COLUMNS — 2nd pass: iterate columns, draw texture + sky/floor
    ldy viewport_first_col
    sty current_column
loc_8AAE:
    ; Column loop: load per-column state from $8F00-$8FFF arrays
    lda (ABUFPT),y
    sta DOSINI
    beq loc_8ABF
    ; Clear old column (sky/floor gradient fill)
    jsr draw_sky_floor
    lda DOSINI
    cmp #$20
    bcs loc_8AC9
    ldy current_column
loc_8ABF:
    lda (ICHIDZ),y
    sta $0B
    ; Draw textured wall column
    jsr draw_texture_col
    ; Plot column pixels to framebuffer
    jsr plot_column
loc_8AC9:
    ; Next column; loop until viewport filled
    inc current_column
    ldy current_column
    cpy viewport_num_cols
    bne loc_8AAE
    ; Decrement animation frame skip counter
    lda APPMHI
    beq loc_8AD7
    dec APPMHI
loc_8AD7:
    ; JMP ($24) — dispatch via render pointer table (buffer-dependent)
    jmp (ICBALZ)
.proc setup_buffer_ptrs_0
    ; SETUP BUFFER PTRS 0 — configure ZP pointers for buffer 0 rendering
    lda #$60
    sta ABUFPT
    lda #$8F
    sta $1D
    lda #$A0
    sta $1E
    lda #$8F
    sta $1F
    lda #$80
    sta ICHIDZ
    lda #$8F
    sta ICDNOZ
    lda #$C0
    sta ICCOMZ
    lda #$8F
    sta ICSTAZ
    lda #$03
    sta ICBALZ
    lda #$8B
    sta ICBAHZ
    rts
.endp

setup_buffer_ptrs_1:
    ; SETUP BUFFER PTRS 1 — configure ZP pointers for buffer 1 rendering
    lda #$A0
    sta ABUFPT
    lda #$8F
    sta $1D
    lda #$E0
    sta $1E
    lda #$8F
    sta $1F
    lda #$C0
    sta ICHIDZ
    lda #$8F
    sta ICDNOZ
    lda #$00
    sta ICCOMZ
    lda #$90
    sta ICSTAZ
    lda #$2C
    sta ICBALZ
    lda #$8B
    sta ICBAHZ
    rts
    ; SETUP BUFFER PTRS 2 — configure ZP pointers for alternate buffer mode
    lda #$E0
    sta ABUFPT
    lda #$8F
    sta $1D
    lda #$60
    sta $1E
    lda #$8F
    sta $1F
    lda #$00
    sta ICHIDZ
    lda #$90
    sta ICDNOZ
    lda #$80
    sta ICCOMZ
    lda #$8F
    sta ICSTAZ
    lda #$DA
    sta ICBALZ
    lda #$8A
    sta ICBAHZ
    rts
process_input:
    ; PROCESS INPUT — read joystick bits, fire button, console keys
    jsr swap_buffers
    ; BIT $7C — test forward flag
    bit HOLDCH
    bpl loc_8B68
    ; Forward: move player along facing direction
    jsr sub_40ED
    jsr sub_42A9
    jsr sub_410B
    jmp loc_8C7E
loc_8B68:
    ; BIT $7D — test backward flag
    bit INSDAT
    bpl loc_8B78
    jsr sub_40D3
    jsr sub_428F
    jsr sub_410B
    jmp loc_8C7E
loc_8B78:
    ; BIT $7E — test turn left
    bit COUNTR
    bpl loc_8B85
    jsr sub_4004
    jsr sub_410B
    jmp loc_8C7E
loc_8B85:
    ; BIT $7F — test turn right
    bit $7F
    bpl loc_8B92
    jsr sub_400C
    jsr sub_410B
    jmp loc_8C7E
loc_8B92:
    ; BIT $80 — test strafe left
    bit $80
    bpl loc_8B9F
    jsr sub_4016
    jsr sub_410B
    jmp loc_8C7E
loc_8B9F:
    ; BIT $81 — test strafe right
    bit $81
    bpl loc_8BAC
    jsr sub_4023
    jsr sub_410B
    jmp loc_8C7E
loc_8BAC:
    ; BIT $83 — FIRE button: toggle minimap display mode (0/1/2)
    bit $83
    bpl loc_8BDE
    ldx $88
    inx
    cpx #$03
    bne loc_8BB9
    ldx #$00
loc_8BB9:
    stx $88
    cpx #$00
    bne loc_8BCA
    lda #$00
    sta HPOSM3
    sta HPOSP2
    jmp loc_8C7E
loc_8BCA:
    cpx #$01
    bne loc_8BD6
    lda #$9D
    sta HPOSP2
    jmp loc_8C7E
loc_8BD6:
    lda #$C1
    sta HPOSM3
    jmp loc_8C7E
loc_8BDE:
    ; BIT $84 — OPTION: trigger music restart
    bit $84
    bpl loc_8BF1
    lda $8B
    bne loc_8BED
    lda $8A
    sta $89
    jmp loc_8BF1
loc_8BED:
    lda #$80
    sta $89
loc_8BF1:
    ; BIT $85 — SELECT: cycle through map levels (1-5)
    bit $85
    bpl loc_8C07
    ldx $69
    inx
    cpx #$06
    bcc loc_8BFE
    ldx #$01
loc_8BFE:
    stx $69
    txa
    clc
    adc #$1A
    sta $9BB3
loc_8C07:
    bit $82
    bpl loc_8C7E
    ldx DSTAT
    dex
    lda $FFA0,x
    clc
    adc $49
    sta ICPTLZ
    lda $FFCD,x
    adc #$00
    sta ICPTHZ
    ldx $5C
    lda $494B,x
    clc
    adc OLDCOL
    cmp #$17
    bcc loc_8C35
    cmp #$44
    bcc loc_8C6E
    cmp #$71
    bcc loc_8C48
    cmp #$9E
    bcc loc_8C5B
loc_8C35:
    ldy #$40
    jsr sub_38BA
    bcc loc_8C7E
    ldx $49
    stx $A1
    ldx DSTAT
    inx
    stx $A2
    jmp loc_8C7E
loc_8C48:
    ldy #$00
    jsr sub_38BA
    bcc loc_8C7E
    ldx $49
    stx $A1
    ldx DSTAT
    dex
    stx $A2
    jmp loc_8C7E
loc_8C5B:
    ldy #$1F
    jsr sub_38BA
    bcc loc_8C7E
    ldx $49
    dex
    stx $A1
    ldx DSTAT
    stx $A2
    jmp loc_8C7E
loc_8C6E:
    ldy #$21
    jsr sub_38BA
    bcc loc_8C7E
    ldx $49
    inx
    stx $A1
    ldx DSTAT
    stx $A2
loc_8C7E:
    rts
swap_buffers:
    ; SWAP BUFFERS — switch active buffer pointers for next frame rendering
    lda #$00
    sta HOLDCH
    sta INSDAT
    sta COUNTR
    sta $7F
    sta $81
    sta $80
    sta $82
    sta $83
    sta $84
    sta $85
    lda SKSTAT
    and #$04
    beq loc_8CA3
    lda #$00
    sta $86
    jmp loc_8D69
loc_8CA3:
    bit $86
    bmi loc_8CAC
    lda #$FF
    jmp loc_8CAE
loc_8CAC:
    lda #$00
loc_8CAE:
    sta $87
    lda #$FF
    sta $86
    lda KBCODE
    cmp #$06
    beq loc_8CC3
    cmp #$86
    beq loc_8CC3
    cmp #$3F
    bne loc_8CCA
loc_8CC3:
    lda #$FF
    sta HOLDCH
    jmp loc_8D6C
loc_8CCA:
    cmp #$07
    beq loc_8CD6
    cmp #$87
    beq loc_8CD6
    cmp #$3A
    bne loc_8CDD
loc_8CD6:
    lda #$FF
    sta INSDAT
    jmp loc_8D6C
loc_8CDD:
    cmp #$0E
    beq loc_8CE9
    cmp #$8E
    beq loc_8CE9
    cmp #$2E
    bne loc_8CF0
loc_8CE9:
    lda #$FF
    sta COUNTR
    jmp loc_8D6C
loc_8CF0:
    cmp #$0F
    beq loc_8CFC
    cmp #$8F
    beq loc_8CFC
    cmp #$3E
    bne loc_8D03
loc_8CFC:
    lda #$FF
    sta $7F
    jmp loc_8D6C
loc_8D03:
    cmp #$8A
    bne loc_8D12
    bit $87
    bpl loc_8D12
    lda #$FF
    sta $83
    jmp loc_8D6C
loc_8D12:
    cmp #$A5
    bne loc_8D21
    bit $87
    bpl loc_8D21
    lda #$FF
    sta $84
    jmp loc_8D6C
loc_8D21:
    cmp #$A8
    bne loc_8D30
    bit $87
    bpl loc_8D30
    lda #$FF
    sta $85
    jmp loc_8D6C
loc_8D30:
    cmp #$2F
    beq loc_8D3C
    cmp #$17
    beq loc_8D3C
    cmp #$C6
    bne loc_8D43
loc_8D3C:
    lda #$FF
    sta $81
    jmp loc_8D6C
loc_8D43:
    cmp #$2A
    beq loc_8D4F
    cmp #$16
    beq loc_8D4F
    cmp #$C7
    bne loc_8D56
loc_8D4F:
    lda #$FF
    sta $80
    jmp loc_8D6C
loc_8D56:
    cmp #$21
    beq loc_8D5E
    cmp #$0C
    bne loc_8D69
loc_8D5E:
    bit $87
    bpl loc_8D69
    lda #$FF
    sta $82
    jmp loc_8D6C
loc_8D69:
    jmp loc_8D6F
loc_8D6C:
    jmp loc_3B99
loc_8D6F:
    lda PORTA
    and #$0F
    tax
    lda TRIG0
    beq loc_8DA8
    txa
    and #$08
    bne loc_8D86
    lda #$FF
    sta INSDAT
    jmp loc_8DC2
loc_8D86:
    txa
    and #$04
    bne loc_8D92
    lda #$FF
    sta HOLDCH
    jmp loc_8DC2
loc_8D92:
    cpx #$0D
    bne loc_8D9D
    lda #$FF
    sta $7F
    jmp loc_8DC2
loc_8D9D:
    cpx #$0E
    bne loc_8DC5
    lda #$FF
    sta COUNTR
    jmp loc_8DC2
loc_8DA8:
    cpx #$07
    bne loc_8DB3
    lda #$FF
    sta $80
    jmp loc_8DC2
loc_8DB3:
    cpx #$0B
    bne loc_8DBE
    lda #$FF
    sta $81
    jmp loc_8DC2
loc_8DBE:
    lda #$FF
    sta $82
loc_8DC2:
    jmp loc_8D6C
loc_8DC5:
    rts
bsp_traverse:
    ; BSP TRAVERSE — walk binary space partition tree (max depth 6) to find visible wall
    lda $9240,x
    ; SELF-MODIFYING: patch CMP operands with current wall boundary addresses
    sta $8DE5
    lda $9250,x
    sta $8DE6
    lda $9260,x
    sta $8DF7
    lda $9270,x
    sta $8DF8
    ; Start at root node (X=$1F), max 6 levels deep (Y=6)
    ldx #$1F
    ldy #$06
loc_8DE2:
    ; BSP inner loop: compare ray position against node split plane
    lda #$FF
    ; SELF-MODIFIED: CMP $CF00,X — compare against wall distance threshold
    cmp sub_FFFF,x
    beq loc_8DF4
    bcs loc_8E06
    lda $92C0,x
    tax
    dey
    bne loc_8DE2
    inx
    rts
loc_8DF4:
    ; Equal: test second axis
    lda #$FF
    ; SELF-MODIFIED: CMP $CF40,X — compare against wall height threshold
    cmp sub_FFFF,x
    beq loc_8E0F
    bcs loc_8E06
    lda $92C0,x
    tax
    dey
    bne loc_8DE2
    inx
    rts
loc_8E06:
    ; Greater: take right child ($9280,X)
    lda $9280,x
    tax
    dey
    bne loc_8DE2
    inx
    rts
loc_8E0F:
    ; Exact match: wall found, return node index
    inx
    inx
    rts
    ; BSP NODE DATA — wall type, texture, and geometry for BSP tree nodes
    ora (ABUFPT,x)
    brk
    brk
    jsr $062D
    ora CASINI
    .byte $04
    .byte $04
    ora (LNFLG,x)
    asl DOSINI
    .byte $03
    .byte $03
    ora current_column
    ora #$00
    .byte $02
    ora (MEMORY_REFRESH_CYCLES,x)
    ora #$0C
    .byte $03
    .byte $07
    .byte $07
    clc
    .byte $03
    .byte $03
    .byte $03
    .byte $0B
    .byte $04
    ora (CASINI,x)
    .byte $13
    .byte $07
    .byte $02
    ora (BUFADR,x)
    .byte $04
    ora (irq_scanline_ctr,x)
    asl irq_scanline_ctr,x
    .byte $02
    ora (fps_timer,x)
    asl
    ora (TRAMSZ,x)
    ora (BRKKEY),y
    ora NGFLAG
    ora (ABUFPT,x)
    .byte $04
    .byte $03
    ora PM_DMA_CYCLES
    ora ($16,x)
    asl DBUFLO
    .byte $02
    ora (BUFADR,x)
    asl DUNUSE
    .byte $02
    ora #$03
    .byte $0B
    asl irq_scanline_ctr
    .byte $07
    ora PM_DMA_CYCLES
    ora (MEMORY_REFRESH_CYCLES),y
    .byte $04
    asl.w TRAMSZ
    .byte $0B
    .byte $07
    ora $1511
    ora $0809
    brk
    ora #$11
    .byte $0B
    .byte $0F
    ora $130D
    .byte $0F
    .byte $04
    ora PM_DMA_CYCLES
    ora TRAMSZ
    asl TRAMSZ
    .byte $07
    .byte $02
    ora (TRAMSZ,x)
    ora CASINI
    .byte $04
    asl BRKKEY
    .byte $02
    ora DSKUTL
    .byte $04
    .byte $02
    .byte $2F
    .byte $1B
    .byte $04
    brk