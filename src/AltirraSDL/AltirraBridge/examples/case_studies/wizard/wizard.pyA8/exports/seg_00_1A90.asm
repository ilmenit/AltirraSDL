; =============================================================
; Segment 0: $1A90-$6DD7 — 
; =============================================================
; XEX load range: $1A90-$6DD7 (21320 bytes)

    org $1A90

    brk
    brk
    brk
    brk
    brk
    rol CIOCHR
    and ENTVEC,x
    brk
    brk
    bvc $1AED
    bvc $1A9F
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    bmi $1AD9
    .byte $37
    and BUFRLO
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
    .byte $23
    and (BUFRHI,x)
    .byte $34
    bit.w ICBAHZ
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
    .byte $B3
    .byte $A3
    .byte $AF
    .byte $B2
    lda LNFLG
    brk
    bne $1AA9
    bne $1AAB
    bne $1AAD
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
entity_update_all:
    ; Updates 4 entity groups in round-robin: G1(10), G2(3), G3(2), G4(2) = 17 total
    brk
    brk
    brk
    brk
    ; entity_update_all: $A2=$14 for group 1 (entity type marker = item tile $14)
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
    brk
    brk
    brk
    ; INC $A2: group 2 marker = $15
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
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    .byte $FF
    .byte $14
    brk
    asl ICBLHZ
    asl $AD
    brk
    ldy #$49
    .byte $FF
    sta $A000
    cmp $A000
    beq $1CB2
    lda $9FFF
    eor #$FF
    sta $9FFF
    cmp $9FFF
    bne $1CB2
    sec
    rts
    lda #$00
    sta $A000
    clc
    rts
    jsr $0700
    cpy #$01
    beq $1CC6
    sty COLOR4
    jmp $0633
    lda #$3C
    sta PACTL
    lda #$36
    sta DOSVEC
    sta DOSINI
    lda #$06
    sta $0B
    sta $0D
    lda #$00
    sta AUDCTL
    lda #$03
    sta SSKCTL
    sta SKCTL
    lda #$01
    sta MEMORY_REFRESH_CYCLES
    lda #$00
    sta COLDST
    lda #$00
    sta game_state
    jmp $0900
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
    lda #$62
    sta SDLSTL
    lda #$07
    sta SDLSTH
    lda $14
    cmp $14
    beq $1D9C
    jsr ZFR0
    ldx #$03
    stx $0352
    lda #$07
    ldy #$5F
    sty $0354
    sta $0355
    inx
    sta $035A
    lda #$80
    sta $035B
    jsr $0757
    ldx #$18
    ldy #$00
    stx $0355
    sty $0354
    ldx #$56
    ldy #$00
    stx $0359
    sty $0358
    lda #$07
    sta $0352
    jsr $0757
    bmi $1DE6
    lda #$0C
    sta $0352
    jsr $0757
    ldy #$01
    rts
    ldx #$10
    stx CH
    jmp CIOV
    .byte $43
    .byte $3A
    jsr $7070
    bvs $1E3D
    .byte $6B
    .byte $07
    eor (PALNTS,x)
    .byte $07
    bit $212F
    bit ICBLHZ
    rol $0E27
    asl.w APPMHI
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    sta $94C6
    lda $94
    cmp #$FF
    bne $1E1A
    dec $95
    ldx #$00
    lda #$00
    sta ($92,x)
    inc $92
    bne $1E26
    inc $93
    dec $94
    lda $94
    cmp #$FF
    beq $1E31
    jmp $078C
    lda $95
    beq $1E3A
    dec $95
    jmp $078C
    rts
    asl
    .byte $D2
    cmp $90
    bcs $1E4B
    lda RANDOM
    and #$03
    clc
    adc #$70
    sta ($8C),y
    dey
    bpl $1E22
    lda $8C
    clc
    adc #$80
    sta $8C
    bcc $1E59
    inc $8D
    lda $8E
    clc
    adc #$80
    sta $8E
    bcc $1E64
    inc $8F
    lda $8D
    cmp #$9F
    bne $1E20
    rts
    lda ($8E),y
    cmp #$74
    bcs $1E7C
    cmp #$70
    bcc $1E7C
    lda $90
    clc
    adc #$23
    sta $90
    rts
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
    lda $6B1B
    sta $BB
    lda $6B1C
    sta $BC
    lda #$00
    sta SDLSTL
    lda #$68
    sta SDLSTH
    lda #$66
    sta CHBAS
    ldx #$FF
    lda #$00
    sta scroll_direction,x
    dex
    bne $1EAD
    sta scroll_direction
    ldx #$7F
    lda $5180,x
    sta wizard_y_tile,x
    dex
    bpl $1EB8
    lda $6BFD
    sta power_gem_value
    clc
    adc #$51
    sta $6860
    ldy #$00
    ldx #$5A
    lda #$07
    jsr SETVBV
    lda #$01
    sta game_running
    lda #$80
    sta VDSLST
    lda #$68
    sta $0201
    lda #$C0
    sta NMIEN
    lda #$03
    sta GRACTL
    lda #$3E
    sta SDMCTL
    lda #$10
    sta PMBASE
    lda #$01
    sta GPRIOR
    ldx #$03
item_collect:
    ; Item collect dispatch. A=param (0=entity kill penalty, 1=item pickup, 2+=castle damage). Y=tile-$14. ALL interactions cost POWER
    lda gameplay_colors,x
    sta COLOR0,x
    dex
    bpl item_collect
    lda #$2D
    sta $84
    lda #$8A
    sta $85
    ldx #$04
    ldy #$0B
    lda $84
    sta $6D00,x
    lda $85
    sta $6D01,x
    dey
    beq $1F33
    inx
    inx
    inx
    lda $84
    clc
    adc #$80
    sta $84
    bcc $1F15
    inc $85
    jmp $0885
    lda #$00
    sta AUDC1
    lda STRIG0
    bne $1F4F
    ldy power_gem_value
    sty $6BFD
    lda ai_speed_table,y
    sta spawn_interval
    sta $51A0
    jmp $0A00
    lda STICK0
    cmp #$0F
    beq $1F33
    inc power_gem_value
    inc $6860
    lda power_gem_value
    cmp #$05
    bne $1F6D
    lda #$00
    sta power_gem_value
    lda #$51
    sta $6860
    lda #$EF
    sta AUDC1
    lda $14
    clc
    adc #$19
    ldx RANDOM
    stx AUDF1
    cmp $14
    bne $1F77
    jmp $08A3
    bpl $1F7D
    lda #$2D
    sta $84
    .byte $3C
    brk
    .byte $77
    brk
    brk
    brk
    lda game_state
    asl
    tax
    lda $6B58,x
    sta init_dispatch_lo
    lda $6B59,x
    sta init_dispatch_hi
    jsr game_init
    jmp $0900
    jmp (init_dispatch_lo)
    lda #$00
    sta $86
    lda #$6E
    sta $87
    lda #$04
    sta $88
    lda #$A0
    sta $89
    lda #$04
    sta map_decode_counter
    lda #$00
    sta map_decode_accum
    sta map_decode_col
    ldy #$00
    lda ($86),y
    beq $1FDE
    and #$C0
    cmp #$80
    bcc $1FDB
    clc
    adc #$40
    eor #$C0
    jmp $094E
    clc
    adc #$40
    asl
    rol map_decode_accum
    asl
    rol map_decode_accum
    dec map_decode_counter
    bne $201D
    sty map_decode_save_y
    ldy map_decode_col
    lda map_decode_accum
    sta ($88),y
    ldy map_decode_save_y
    lda #$00
    sta map_decode_accum
    lda #$04
    sta map_decode_counter
    inc map_decode_col
    lda map_decode_col
    cmp #$20
    bne $201D
    lda #$00
    sta map_decode_col
    lda $88
    clc
    adc #$28
    sta $88
    bcc $201D
    inc $89
    iny
    bpl $1FC9
    lda $86
    clc
    adc #$80
    sta $86
    bcc $1FC7
    inc $87
    lda $87
    cmp #$A0
    bne $1FC7
    rts
    jsr map_decoder
    jsr cursor_update
    inc game_state
    rts
    jsr map_decoder
    jsr level_renderer
    rts
    jmp ($00A0)
    lda ($86),y
    beq $205B
    and #$C0
    cmp #$80
    bcc $2058
    clc
    adc #$40
    eor #$C0
    jmp $094E
    clc
    adc #$40
    asl
    rol map_decode_accum
    asl
    rol map_decode_accum
    dec map_decode_counter
    bne $209A
    sty map_decode_save_y
    ldy map_decode_col
    lda map_decode_accum
    sta ($88),y
    ldy map_decode_save_y
    lda #$00
    sta map_decode_accum
    lda #$04
    sta map_decode_counter
    inc map_decode_col
    lda map_decode_col
    cmp #$20
    bne $209A
    plp
    brk
    rol LNFLG,x
    clc
    brk
    lda #$00
    sta $92
    sta $94
    lda #$10
    sta $93
    lda #$08
    sta $95
    jsr $0780
    ldx #$07
    lda $6798,x
    sta $1624,x
    lda $67B0,x
    sta $16B4,x
    dex
    bpl $20A3
    lda #$00
    sta $92
    sta $94
    lda #$6E
    sta $93
    lda #$52
    sta $95
    jsr $0780
    jsr $0C00
    jsr $0A4E
    ldx #$0E
    txa
    clc
    adc wizard_y_tile
    tay
    lda $5900,x
    sta $1720,y
    dey
    dex
    bpl $20D1
    jmp $0D00
    lda #$02
    sta $96
    sta $98
    lda #$6E
    sta $97
    lda #$9F
    sta $99
    lda #$0C
    ldy #$7D
    sta ($96),y
    sta ($98),y
    dey
    bpl $20F0
    lda #$00
    sta $96
    lda #$00
    tay
    sta ($96),y
cursor_hit_handler:
    ; Wizard walks onto entity marker or wall. tile /bin/bashB=wall (DEC hits until break->6), tile 4-8=entity kill (item_collect param=0, costs POWER)
    iny
    sta ($96),y
    iny
    lda #$0C
    sta ($96),y
    ldy #$7F
    sta ($96),y
    lda $96
    clc
    adc #$80
    sta $96
    bcc $20FB
    inc $97
    lda $97
    cmp #$9F
    bne $20FB
    lda #$80
    sta $92
    sta $94
    lda #$9F
    sta $93
    lda #$00
    sta $95
    jsr $0780
    ldx #$00
    lda #$07
    sta first_tile
    lda castle_screen_addrs,x
    sta $96
    lda $6BAC,x
    sta $97
    jsr map_tile_fill
    lda #$C7
    sta first_tile
    inx
    inx
    cpx #$08
    bne $2135
    rts
    lda #$01
    sta $98
    lda first_tile
    ldy #$00
    sta ($96),y
    clc
    adc #$01
    iny
    sta ($96),y
    adc #$01
    ldy #$80
    dec $98
    beq $2157
    rts
    sta $0CA9,y
    ldy #$7D
    sta ($96),y
    sta ($98),y
    dey
    bpl $216D
    lda #$00
    sta $96
    lda #$00
    tay
    sta ($96),y
    iny
    sta ($96),y
    iny
    lda #$0C
    sta ($96),y
    ldy #$7F
    sta ($96),y
    lda BFENLO
    brk
    .byte $5B
    brk
    brk
    brk
    lda wizard_x_tile
    sec
    sbc #$38
    lsr
    lsr
    lsr
    clc
    adc #$42
    adc map_x_fine
    sta HPOSM3
    lda #$00
    ldy missile_offset
    sta $1322,y
    sta $1323,y
    lda wizard_y_tile
    sec
    sbc #$10
    lsr
    lsr
    lsr
    sta map_decode_accum
    lda #$59
    sec
    sbc map_y_fine
    asl
    clc
    adc map_decode_accum
    sta missile_offset
    tay
    lda #$40
    sta $1322,y
    sta $1323,y
    rts
    lda #$6E
    sta $97
    lda #$9F
    sta $99
    lda #$0C
    ldy #$7D
    sta ($96),y
    sta ($98),y
    dey
    bpl $21DD
    lda #$00
    sta $96
    lda #$00
    tay
    sta ($96),y
    iny
    sta ($96),y
    iny
    lda #$0C
    sta ($96),y
    ldy #$7F
    sta ($96),y
    lda $96
    clc
    adc #$80
    sta $96
    bcc $21E8
    inc $97
    lda $97
    cmp #$9F
    bne $21E8
    lda #$80
    sta $92
    sta $94
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
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    lda #$80
    sta $8C
    lda #$6E
    sta $8D
    ldy #$7F
    lda RANDOM
    cmp #$1C
    bcs $22CD
    cmp #$15
    bcc $22AC
    lda #$85
    sta ($8C),y
    jmp $0C3D
    jsr $0C78
    sta ($8C),y
    lda RANDOM
    and #$07
    beq $22CD
    tax
    dex
    tya
    clc
    adc $8C
    sta $8E
    lda $8D
    adc #$00
    sta $8F
    sty $90
    jsr $0C81
    ldy $90
    dey
    bpl $229A
    lda $8C
    clc
    adc #$80
    sta $8C
    bcc $2298
    inc $8D
    lda $8D
    cmp #$9F
    bne $2298
    jsr $0C94
    rts
    brk
    .byte $04
    .byte $07
    .byte $0B
    .byte $0F
    .byte $13
    clc
    ora ($7F,x)
    sta (LNFLG,x)
    .byte $7F
    .byte $80
    brk
    .byte $7F
    .byte $80
    sta (LNFLG,x)
    sta ($82,x)
    .byte $FF
    brk
    ror $FF7F,x
    brk
    .byte $01                     ; $22FF
entity_move:
    .byte $02                     ; $2300
    .byte $80
    sta (LNFLG,x)
    .byte $7F
    inc.w $00FF,x
    lda RANDOM
    and #$03
    clc
    adc #$41
    rts
    lda $0C55,x
    tax
    ldy $0C5C,x
    beq $2323
    jsr $0C78
    sta ($8E),y
    inx
    jmp $0C85
    rts
    lda #$ED
    sta $8C
    lda #$6E
    sta $8D
    ldy #$00
    lda #$0B
    sta ($8C),y
    lda $8C
    eor #$80
    sta $8C
    bmi $232E
    inc $8D
    lda $8D
    cmp #$75
    bne $232E
    ldy #$11
    lda #$0B
    sta ($8C),y
    dey
    bpl $2346
    rts
    dex
    lda $8C
    clc
    adc #$80
    sta $8C
    bcc $2315
    inc $8D
    lda $8D
    cmp #$9F
    bne $2315
    jsr $0C94
    rts
    brk
    .byte $04
    .byte $07
    .byte $0B
    .byte $0F
    .byte $13
    clc
    ora ($7F,x)
    sta (LNFLG,x)
    .byte $7F
    .byte $80
    brk
    .byte $7F
    .byte $80
    sta (LNFLG,x)
    sta ($82,x)
    .byte $FF
    brk
    ror $FF7F,x
    brk
    ora (CASINI,x)
    .byte $80
    sta (LNFLG,x)
    .byte $7F
    inc.w $00FF,x
    lda RANDOM
    and #$03
    .byte $74
    brk
    .byte $3F
    .byte $BF
    lda #$05
    lda #$00
    sta HSCROL
    sta VSCROL
    lda #$8E
    sta $6CC6
    ldx #$00
    lda RANDOM
    and #$03
    asl
    tay
    lda $6B6C,y
    sta $96
    lda $6B6D,y
    sta $97
    lda $6CC6
    sta ($96,x)
    lda $96
    sec
    sbc #$81
    sta $96
    lda $97
    sbc #$00
    sta $97
    jsr $0D82
    iny
    iny
    cpy #$08
    bne $23CD
    ldy #$00
    inc $6CC6
    lda $6CC6
    cmp #$92
    bcc $23A6
    lda power_gem_value
    cmp #$03
    bcs $23F4
    lda $6BB1
    sec
    sbc #$81
    sta $96
    lda $6BB2
    sbc #$00
    sta $97
    jsr $0D82
    inc game_state
    rts
    ldy #$78
    lda #$00
    sta $96
    sta $98
    lda #$85
    sta $97
    lda #$92
    sta $99
    lda #$E0
    sta ($96),y
    sta ($98),y
    dey
    cpy #$09
    bcs $2406
    jmp $0D60
    tya
    pha
    ldy #$03
    lda #$E1
    sta ($96),y
    dey
    bpl $2418
    ldy #$80
    sta ($96),y
    ldy #$83
    sta ($96),y
    inc $97
    ldy #$00
    sta ($96),y
    ldy #$03
    sta ($96),y
    ldy #$83
    sta ($96),y
    dey
    cpy #$7F
    bne $2431
    pla
    tay
    rts
    sbc #$00
    sta $97
    jsr $0D82
    iny
    iny
    cpy #$08
    bne $244A
    ldy #$00
    inc $6CC6
    lda $6CC6
    cmp #$92
    bcc $2423
    lda power_gem_value
    cmp #$03
    bcs $2471
    lda $6BB1
    sec
    sbc #$81
    sta $96
    lda $6BB2
    sbc #$00
    sta $97
    jsr $0D82
    inc game_state
    rts
    ldy #$78
    lda #$00
    sta $96
    sta $98
    lda #$85
    sta $97
    lda #$92
    sta $99
    lda #$E0
    sta ($96),y
    sta ($98),y
    dey
    cpy #$09
    pha
    brk
    rol.w LNFLG
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
.proc entity_calc_position
    ; Calculate entity screen position from address for AI targeting
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
    brk
    brk
.endp

.proc entity_distance_calc
    ; Calculate distance from entity to target (castle or wizard)
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
    brk
    brk
    brk
.endp

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
    ldx wizard_y_tile
    cpx #$10
    beq $2EB7
    lda move_cooldown
    bne $2EA4
    dec map_row
    ldy #$01
    jsr adjust_bb_sub
    dec view_offset_y
    dec view_offset_y
    dex
    dex
    stx wizard_y_tile
    cpx $5180
    bne $2EB7
    dec scroll_lock_v
    rts
    ldx wizard_y_tile
    cpx #$80
    beq $2EB7
    lda move_cooldown
    bne $2ECC
    inc map_row
    ldy #$01
    jsr adjust_bb_add
    inc view_offset_y
    inc view_offset_y
    inx
    inx
    jmp $181C
    ldx wizard_x_tile
    cpx #$38
    beq $2EB7
    lda move_cooldown
    bne $2EEB
    dec map_col
    ldy #$00
    jsr adjust_bb_sub
    dec view_offset_x
    dex
    stx wizard_x_tile
    cpx $5181
    bne $2EB7
    dec scroll_lock_h
    rts
    ldx wizard_x_tile
    cpx #$C0
    beq $2EB7
    lda move_cooldown
    bne $2F0F
    inc map_col
    ldy #$00
    jsr adjust_bb_add
    inc view_offset_x
    inx
    jmp $185F
    lda $BB
    clc
    adc scroll_offsets,y
    sta $BB
    bcc $2F22
    inc $BC
    rts
    lda $BB
    sec
    sbc scroll_offsets,y
    sta $BB
    bcs $2F22
    dec $BC
    rts
    .byte $03
    dec scroll_lock_v
    rts
    ldx wizard_y_tile
    cpx #$80
    beq $2F34
    lda move_cooldown
    bne $2F49
    inc map_row
    ldy #$01
    jsr adjust_bb_add
    inc view_offset_y
    inc view_offset_y
    inx
    inx
    jmp $181C
    ldx wizard_x_tile
    cpx #$38
    beq $2F34
    lda move_cooldown
    bne $2F68
    dec map_col
    ldy #$00
    jsr adjust_bb_sub
    dec view_offset_x
    dex
    stx wizard_x_tile
    cpx $5181
    bne $2F34
    dec scroll_lock_h
    rts
    ldx wizard_x_tile
    cpx #$C0
    beq $2F34
    lda move_cooldown
    bne $2F8C
    inc map_col
    ldy #$00
    jsr VBI_SCANLINE
    .byte $23
    brk
    brk
    brk
    lsr scroll_direction
    bcc $2F98
    jsr scroll_up
    lsr scroll_direction
    bcc $2FA0
    jsr scroll_down
    lsr scroll_direction
    bcc $2FA8
    jsr scroll_left
    lsr scroll_direction
    bcc $2FB0
    jsr scroll_right
    lda hscrol_counter
    sta HSCROL
    lda vscrol_counter
    sta VSCROL
    rts
    lda scroll_lock_v
    beq $2FC5
    jmp scroll_edge_up
    dec vscrol_counter
    dec vscrol_counter
    lda vscrol_counter
    cmp #$FE
    bne $2FF5
    lda map_y_fine
    cmp #$59
    bne $2FE5
    inc vscrol_counter
    inc vscrol_counter
    inc scroll_lock_v
    jmp scroll_edge_up
    lda #$0E
    sta vscrol_counter
    inc map_y_fine
    dec map_row
    ldy #$01
    jsr update_lms_add
    rts
    lda scroll_lock_v
    beq $2FFE
    jmp scroll_edge_down
    lda map_y_fine
    bne $3009
    inc scroll_lock_v
    jmp scroll_edge_down
    inc vscrol_counter
    inc vscrol_counter
    lda vscrol_counter
    cmp #$10
    bne $3026
    lda #$00
    sta vscrol_counter
    dec map_y_fine
    inc map_row
    ldy #$01
    jsr update_lms_sub
    rts
    lda scroll_lock_h
    beq $302F
    jmp scroll_edge_left
    lda map_x_fine
    bne $303A
    inc scroll_lock_h
    jmp scroll_edge_left
    inc hscrol_counter
    lda hscrol_counter
    cmp #$08
    bne $3054
    lda #$00
    sta hscrol_counter
    dec map_x_fine
    dec map_col
    ldy #$00
    jsr update_lms_add
    rts
    lda scroll_lock_h
    beq $305D
    jmp scroll_edge_right
    dec hscrol_counter
    lda hscrol_counter
    cmp #$FF
    bne $3087
    lda map_x_fine
    cmp #$6A
    bne $3077
    inc hscrol_counter
    inc scroll_lock_h
    jmp scroll_edge_right
    lda #$07
    sta hscrol_counter
    inc map_x_fine
    inc map_col
    ldy #$00
    jsr update_lms_sub
    rts
    ldx #$00
    lda $6D04,x
    clc
    adc scroll_offsets,y
    sta $6D04,x
    bcc $3099
    inc $6D05,x
    inx
    inx
    inx
    cpx #$21
    bne $308A
    lda $BB
    clc
    adc scroll_offsets,y
    sta $BB
    bcc $30AC
    inc $BC
    rts
    ldx #$00
    lda $6D04,x
    sec
    sbc scroll_offsets,y
    sta $6D04,x
    bcs $30BE
    dec $6D05,x
    inx
    inx
    inx
    cpx #$21
    bne $30AF
    lda $BB
    sec
    sbc scroll_offsets,y
    sta $BB
    bcs $30D1
    dec $BC
    rts
    lda scroll_lock_h
    beq $30DA
    jmp scroll_edge_right
    dec hscrol_counter
    lda hscrol_counter
    cmp #$FF
    bne $3104
    lda map_x_fine
    cmp #$6A
    bne $30F4
    inc hscrol_counter
    inc scroll_lock_h
    jmp scroll_edge_right
    lda #$07
    sta hscrol_counter
    inc map_x_fine
    inc map_col
    ldy #$00
    jsr update_lms_sub
    rts
    ldx #$00
    .byte $F4
    brk
    pha
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
    lda #$09
    sta $9E
    lda #$14
    sta $A2
    lda #$00
    sta $BD
    ldx entity_grp1_idx
    lda entity_grp1_ypos,x
    beq $31CC
    sta $A4
    lda entity_grp1_xpos,x
    sta $A3
    lda entity_grp1_dir,x
    sta $A0
    lda entity_grp1_type,x
    sta $A1
    jsr entity_move
    lda $A4
    sta entity_grp1_ypos,x
    lda $A3
    sta entity_grp1_xpos,x
    lda $A0
    sta entity_grp1_dir,x
    lda $A1
    sta entity_grp1_type,x
    dex
    dec $9E
    bpl $319F
    cpx #$FF
    bne $31D8
    ldx $5184
    stx entity_grp1_idx
    lda #$02
    sta $9E
    inc $A2
    inc $BD
    ldx entity_grp2_idx
    lda $B480,x
    beq $3213
    sta $A4
    lda $B400,x
    sta $A3
    lda $B500,x
    sta $A0
    lda $B580,x
    sta $A1
    jsr entity_move
    lda $A4
    sta $B480,x
    lda $A3
    sta $B400,x
    lda $A0
    sta $B500,x
    lda $A1
    sta $B580,x
    dex
    dec $9E
    bpl $31E6
    cpx #$FF
    bne $321F
    ldx $5185
    stx entity_grp2_idx
    lda #$01
    sta $9E
    inc $A2
    inc $BD
    ldx entity_grp3_idx
    lda $B640,x
    beq $325A
    sta $A4
    lda $B600,x
    sta $A3
    lda $B680,x
    sta $A0
    lda $B6C0,x
    sta $A1
    jsr entity_move
    lda $A4
    sta $B640,x
    lda $A3
    sta $B600,x
    lda $A0
    sta $B680,x
    lda $A1
    sta $B6C0,x
    dex
    dec $9E
    bpl $322D
    cpx #$FF
    bne $3266
    ldx $5186
    stx entity_grp3_idx
    lda #$01
    sta $9E
    inc $A2
    inc $BD
    ldx entity_grp4_idx
    lda $B720,x
    beq $32A1
    sta $A4
    lda $B700,x
    sta $A3
    lda $B740,x
    sta $A0
    lda $B760,x
    sta $A1
    jsr entity_move
    lda $A4
    sta $B720,x
    lda $A3
    sta $B700,x
    lda $A0
    sta $B740,x
    lda $A1
    sta $B760,x
    dex
    dec $9E
    bpl $3274
    cpx #$FF
    bne $32AD
    ldx $5187
    stx entity_grp4_idx
    dec $5126
    bne $3301
    lda #$03
    sta $5126
    lda #$00
    sta $9E
    inc $A2
    inc $BD
    ldx $5108
    lda $B783,x
    beq $32F2
    sta $A4
    lda $B780,x
    sta $A3
    lda $B786,x
    sta $A0
    lda $B789,x
    sta $A1
    jsr entity_move
    lda $A4
    sta $B783,x
    lda $A3
    sta $B780,x
    lda $A0
    sta $B786,x
    lda $A1
    sta $B789,x
    dex
    dec $9E
    bpl $32C5
    cpx #$FF
    bne $32FE
    ldx $5188
    stx $5108
    jmp $5200
    .byte $B7
    sta $A1
    sty $7A00
    brk
    brk
    brk
    brk
    brk
    brk
    ora $19C9
    bcs $331E
    lda #$1E
    sta $B8
    lda #$01
    jsr $1E80
    lda #$CD
    sta ($B4),y
    rts
    lda player_state
    cmp #$03
    beq $332B
    rts
    dec $B7
    bpl $3345
    lda #$02
    sta $B7
    dec $B9
    ldy $B9
    cpy #$FF
    bne $333D
    ldy #$07
    lda $6B4C,y
    sec
    sbc #$01
    sta $B6
    ldy #$00
    lda $B8
    sta ($B4),y
    lda RANDOM
    beq $337C
    ldy $B6
    lda entity_move_signs,y
    beq $3366
    lda $B4
    sec
    sbc entity_move_deltas,y
    sta $B4
    bcs $3372
    dec $B5
    jmp $1C65
    lda $B4
    clc
    adc entity_move_deltas,y
    sta $B4
    bcc $3372
    inc $B5
    ldy #$00
    lda ($B4),y
    sta $B8
    cmp #$0C
    bne $338A
    sty player_state
    ldy #$0C
    lda #$00
    sta $00A4,y
    .byte $1C
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    lda $A3
    sta $9A
    sta $9C
    lda $A4
    sta $9B
    sta $9D
    ldy #$00
    lda ($9A),y
    cmp $A2
    beq $33F8
    cmp #$0B
    beq $33F8
    cmp #$CD
    bne $33B3
    lda $A0
    sta $B8
    jmp $1D58
    lda $BD
    cmp #$04
    beq $33D7
    cmp #$02
    bne $33E4
    lda $A0
    and #$3F
    cmp #$05
    bcs $33E4
    lda RANDOM
    and #$03
    cmp difficulty
    bcc $33D2
    jmp $1D54
    lda #$85
    jmp $1D56
    lda $A0
    and #$3F
    cmp #$05
    bcs $33E4
    lda #$86
    jmp $1D56
    lda $A0
    sta ($9A),y
    lda #$00
    sta $A4
    ldy $BD
    lda entity_count_0,y
    sec
    sbc #$01
    sta entity_count_0,y
    rts
    jsr $1EC4
    lda $A1
    and #$03
    cmp #$02
    bcs $341F
    lda RANDOM
    and #$1F
    beq $3417
    lda $A1
    and #$1C
    lsr
    lsr
    tay
    lda ai_direction_map,y
    jmp $1DF7
    lda RANDOM
    and #$1F
    sta $A1
    rts
    lda $A1
    lsr
    lsr
    and #$03
    cmp #$03
    beq $3445
    sta $B0
    tay
    lda castle_destroyed_1,y
    beq $3434
    jmp $1E7F
    lda $B0
    asl
    tay
    lda castle_map_positions,y
    sta $AE
    lda $6BB4,y
    sta $AF
    jmp $1DBF
    lda map_col
    sta $AE
    lda map_row
    sta $AF
    jsr $1EDE
    lda $A8
    cmp $A7
    bcc $3469
    lda $A6
    cmp $AF
    beq $3469
    bcs $3465
    lda #$02
    jmp $1DD7
    lda #$01
    sta $9F
    lda $A7
    cmp $A8
    bcc $3482
    lda $A5
    cmp $AE
    beq $3482
    bcs $347C
    lda #$08
    jmp $1DEE
    lda #$04
    ora $9F
    sta $9F
    lda $9F
    bne $3487
    rts
    sec
    sbc #$01
    tay
    lda entity_move_signs,y
    beq $349F
    lda $9C
    sec
    sbc entity_move_deltas,y
    sta $9C
    bcs $34AB
    dec $9D
    jmp $1E1B
    lda $9C
    clc
    adc entity_move_deltas,y
    sta $9C
    bcc $34AB
    inc $9D
    ldy #$00
    lda ($9C),y
    beq $3517
    cmp #$C7
    bcc $34FC
    cmp #$CB
    bcs $34C3
    lda #$1D
    sta ($9A),y
    lda #$02
    jsr item_collect
    rts
    cmp #$CD
    bne $34D1
    lda #$1E
    sta ($9A),y
    lda #$01
    jsr item_collect
    rts
    cmp #$F2
    bcs $3517
    cmp #$E3
    bcs $3506
    pha
    ldy $BD
    lda barrier_break_prob,y
    sta $BA
    pla
    cmp #$E0
    beq $34EE
    lsr $BA
    cmp #$E1
    beq $34EE
    lsr $BA
    ldy #$00
    lda RANDOM
    cmp $BA
    bcs $350F
    lda #$E3
    sta ($9C),y
    rts
    and #$3F
    cmp #$0B
    beq $3517
    cmp #$07
    bcc $3517
    lda $A1
    clc
    adc #$40
    sta $A1
    bcc $3553
    lda RANDOM
    and #$1C
    sta $A1
    rts
    lda $A0
    cmp #$0B
    beq $351F
    sta ($9A),y
    lda ($9C),y
    sta $A0
    cmp #$0B
    beq $352B
    lda $A2
    sta ($9C),y
    lda $A1
    and #$1F
    sta $A1
    lda $9C
    sta $A3
    lda $9D
    sta $A4
    jsr $1EC4
    lda $A5
    cmp map_col
    bne $3553
    lda $A6
    cmp map_row
    bne $3553
    lda #$1D
    sta ($9C),y
    lda #$00
    jsr item_collect
    rts
    lda #$00
    sta $9F
    lda $A3
    and #$7F
    sta $A5
    lda $A4
    sec
    sbc #$6E
    asl
    pha
    lda $A3
    asl
    pla
    adc #$00
    sta $A6
    rts
    lda $A5
    sec
    sbc $AE
    bcs $3579
    eor #$FF
    adc #$01
    sta $A7
    lda $A6
    sec
    sbc $AF
    bcs $3586
    eor #$FF
    adc #$01
    sta $A8
    rts
    lda ($90,x)
    .byte $44
    lda RANDOM
    and #$48
    stx $BF
    sty $B3
    lda #$00
    sta score_delta_hi
    sta score_delta_mid
    sta score_delta_lo
    lda $A2
    sec
    sbc #$14
    tay
    pla
    cmp #$01
    bcc $35CD
    beq $35B0
    jmp $1F4E
    lda $5480,y
    sed
    clc
    adc power_gem_value
    sta score_delta_mid
    cld
    jsr score_add_foul
    lda $6BD0,y
    sta score_delta_mid
    jsr power_subtract
    ldx $BF
    ldy $B3
    rts
    lda #$01
    sta $BF08
    lda $6BD5,y
    sta score_delta_hi
    jsr power_subtract
    jmp $1F38
    lda #$01
    sta $BF00
    ldy $BD
    ldx #$00
    lda $A5
    cmp #$32
    bcc $35F3
    cmp #$46
    bcc $35F2
    inx
    inx
    lda castle_power_0,x
    sec
    sed
    sbc castle_damage_costs,y
    sta castle_power_0,x
    cld
    beq $3603
    bcs $35C8
    lda #$00
    sta castle_power_0,x
    inx
    inc castle_destroyed_0,x
    lda difficulty
    bne $3616
    lda #$09
    sta game_state
    lsr difficulty
    txa
    asl
    tax
    lda castle_screen_addrs,x
    sta $C0
    lda $6BAC,x
    sta $C1
    ldy #$00
    lda #$F2
    jsr $1FA5
    ldy #$80
    jsr $1FA5
    jmp $1F38
    sta ($C0),y
    clc
    adc #$01
    iny
    sta ($C0),y
    clc
    adc #$01
    rts
    jmp ($3520)
    .byte $43
    ldx $BF
    ldy $B3
    rts
    lda #$01
    sta $BF08
    lda $6BD5,y
    sta score_delta_hi
    jsr power_subtract
    jmp $1F38
    lda #$01
    sta $BF00
    ldy $BD
    ldx #$00
    lda $A5
    cmp #$32
    bcc $3670
    cmp #$46
    bcc $366F
    inx
    inx
    lda castle_power_0,x
    sec
    sed
    sbc castle_damage_costs,y
    sta castle_power_0,x
    cld
    beq $3680
    bcs $3645
    lda #$00
    sta castle_power_0,x
    inx
    inc castle_destroyed_0,x
    lda.w ICBALZ
    .byte $34
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
    lda $A9
    cmp #$04
    bcc $37A1
    lda $AA
    cmp #$08
    bcc $37AC
    ldy #$81
    jmp $2123
    lda $AA
    cmp #$08
    bcc $37B1
    ldy #$80
    jmp $2123
    ldy #$01
    jmp $2123
    ldy #$00
    lda ($BB),y
    cmp #$0B
    bne $37CB
    lda #$50
    sta score_delta_hi
    jsr power_subtract
    lda #$01
    sta $BF0E
    lda #$86
    sta ($BB),y
    rts
    cmp #$14
    bcc $37E3
    cmp #$19
    bcs $37E3
    sta $A2
    lda #$1D
    sta ($BB),y
    lda #$01
    sta $BF08
    lda #$00
    jsr item_collect
    rts
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
    lda $A3
    sta $9A
    sta $9C
    lda $A4
    sta $9B
    sta $9D
    ldy #$00
    lda ($9A),y
    cmp $A2
    beq $3A1C
    cmp #$0B
    beq $3A1C
    cmp #$CD
    bne $39B3
    lda $A0
    sta $B8
    jmp $2358
    lda $BD
    cmp #$04
    beq $39D7
    cmp #$02
    bne $39E4
    lda $A0
    and #$3F
    cmp #$05
    bcs $39E4
    lda RANDOM
    and #$03
    cmp difficulty
    bcc $39D2
    jmp $2354
    lda #$85
    jmp $2356
    lda $A0
    and #$3F
    cmp #$05
    bcs $39E4
    lda #$86
    jmp $2356
    lda $A0
    sta ($9A),y
    lda #$00
    sta $A4
    ldy $BD
    lda entity_count_0,y
    sec
    sbc #$01
    sta entity_count_0,y
    sed
    lda score_lo
    sec
    sbc #$01
    sta score_lo
    lda score_hi
    sbc #$00
    sta score_hi
    lda castle_score_lo
    clc
    adc #$01
    sta castle_score_lo
    lda castle_score_hi
    adc #$00
    sta castle_score_hi
    cld
    rts
    jsr entity_calc_position
    lda $A1
    and #$03
    cmp #$02
    bcs $3A43
    lda RANDOM
    and #$1F
    beq $3A3B
    lda $A1
    and #$1C
    lsr
    lsr
    tay
    lda ai_direction_map,y
    jmp $241B
    lda RANDOM
    and #$1F
    sta $A1
    rts
    lda $A1
    lsr
    lsr
    and #$03
    cmp #$03
    beq $3A69
    sta $B0
    tay
    lda castle_destroyed_1,y
    beq $3A58
    jmp $24BA
    lda $B0
    asl
    tay
    lda castle_map_positions,y
    sta $AE
    lda $6BB4,y
    sta $AF
    jmp $23E3
    lda map_col
    sta $AE
    lda map_row
    sta $AF
    jsr entity_distance_calc
    lda $A8
    cmp $A7
    bcc $3A8D
    lda $A6
    cmp $AF
    beq $3A8D
    bcs $3A89
    lda #$02
    jmp $23FB
    lda #$01
    sta $9F
    lda $A7
    cmp $A8
    bcc $3AA6
    lda $A5
    cmp $AE
    beq $3AA6
    bcs $3AA0
    lda #$08
    jmp $2412
    lda #$04
    ora $9F
    sta $9F
    lda $9F
    bne $3AAB
    rts
    sec
    sbc #$01
    tay
    lda entity_move_signs,y
    beq $3AC3
    lda $9C
    sec
    sbc entity_move_deltas,y
    sta $9C
    bcs $3ACF
    dec $9D
    jmp $243F
    lda $9C
    clc
    adc entity_move_deltas,y
    sta $9C
    bcc $3ACF
    inc $9D
    ldy #$00
    lda ($9C),y
    beq $3B52
    cmp #$C7
    bcc $3B23
    cmp #$CB
    bcs $3AE7
    lda #$1D
    sta ($9A),y
    lda #$02
    jsr item_collect
    rts
    cmp #$CD
    bne $3AF8
    lda #$1E
    sta ($9A),y
    lda #$01
    sta $BF0A
    jsr item_collect
    rts
    cmp #$F2
    bcs $3B52
    cmp #$E3
    bcs $3B41
    pha
    ldy $BD
    lda barrier_break_prob,y
    sta $BA
    pla
    cmp #$E0
    beq $3B15
    lsr $BA
    cmp #$E1
    beq $3B15
    lsr $BA
    ldy #$00
    lda RANDOM
    cmp $BA
    bcs $3B4A
    lda #$E3
    sta ($9C),y
    rts
    and #$3F
    cmp #$0B
    beq $3B52
    cmp #$07
    bcc $3B52
    cmp #$0E
    bcc $3B41
    cmp #$12
    bcs $3B41
    lda #$00
    sta ($9C),y
    lda #$01
    sta $BF01
    jmp $24C2
    lda $A1
    clc
    adc #$40
    sta $A1
    bcc $3B8E
    lda RANDOM
    and #$1C
    sta $A1
    rts
    lda $A0
    cmp #$0B
    beq $3B5A
    sta ($9A),y
    lda ($9C),y
    sta $A0
    cmp #$0B
    beq $3B66
    lda $A2
    sta ($9C),y
    lda $A1
    and #$1F
    sta $A1
    lda $9C
    sta $A3
    lda $9D
    sta $A4
    jsr entity_calc_position
    lda $A5
    cmp map_col
    bne $3B8E
    lda $A6
    cmp map_row
    bne $3B8E
    lda #$1D
    sta ($9C),y
    lda #$00
    jsr item_collect
    rts
    lda #$00
    sta $9F
    lda $A3
    and #$7F
    sta $A5
    lda $A4
    sec
    sbc #$6E
    asl
    pha
    lda $A3
    asl
    pla
    adc #$00
    sta $A6
    rts
    lda $A5
    sec
    sbc $AE
    bcs $3BB4
    eor #$FF
    adc #$01
    sta $A7
    lda $A6
    sec
    sbc $AF
    bcs $3BC1
    eor #$FF
    adc #$01
    sta $A8
    rts
    lda ($90,x)
    .byte $44
    lda RANDOM
    and #$1C
    sta $A1
    rts
    lda $A0
    cmp #$0B
    beq $3BD7
    sta ($9A),y
    lda ($9C),y
    sta $A0
    cmp #$0B
    beq $3BE3
    lda $A2
    sta ($9C),y
    lda $A1
    and #$1F
    sta $A1
    lda $9C
    sta $A3
    lda $9D
    sta $A4
    jsr entity_calc_position
    lda $A5
    cmp map_col
    bne $3C0B
    lda $A6
    cmp map_row
    bne $3C16
    brk
    rti
    brk
    brk
    brk
    brk
    brk
    brk
entry_point:
    ; RUN: copies A90-C8F to /bin/bash400-5FF, sets DOS vectors, JMP /bin/bash917
    lda #$90
    sta $B3
    lda #$1A
    sta $B4
    lda #$00
    sta $B5
    lda #$04
    sta $B6
    lda #$22
    sta $B7
    ldx #$00
    ldy #$00
    lda ($B3),y
    sta ($B5),y
    iny
    bne $3C22
    inc $B4
    inc $B6
    inx
    txa
    cmp $B7
    bne $3C20
    lda #$36
    sta DOSVEC
    sta DOSINI
    lda #$06
    sta $0B
    sta $0D
    lda #$01
    sta MEMORY_REFRESH_CYCLES
    lda #$FF
    sta WARMST
    jmp game_init
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
    brk
    brk
    brk
    brk
    brk
    brk
    bvs $3CF2
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    plp
    brk
    brk
    plp
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
    brk
    brk
    brk
    brk
    plp
    brk
    brk
    plp
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
    brk
    brk
    brk
    brk
    plp
    brk
    brk
    plp
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
    brk
    brk
    brk
    brk
    plp
    brk
    brk
    plp
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
    brk
    brk
    brk
    brk
    plp
    brk
    brk
    plp
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
    brk
    brk
    brk
    brk
    plp
    brk
    brk
    plp
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
    brk
    brk
    brk
    brk
    plp
    brk
    brk
    plp
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
    brk
    brk
    brk
    brk
    plp
    brk
    brk
    plp
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
    brk
    brk
    brk
    brk
    plp
entity_spawner:
    ; Spawn entities: RANDOM selects type/position, 5 types into 4 groups, checks spawn limits
    lda RANDOM
    and #$1F
    tax
    lda $54E0,x
    sta spawn_type_idx
    tax
    clc
    adc #$14
    sta $99
    dec spawn_timer_0,x
    lda spawn_timer_0,x
    cmp #$FF
    bne $4122
    lda entity_respawn_max,x
    sta spawn_timer_0,x
    sta $BE
    lda entity_count_0,x
    cmp entity_max_alive,x
    beq $4167
    txa
    asl
    tay
    lda $4140,y
    sta dispatch_lo
    lda $4141,y
    sta dispatch_hi
    ldy $BE
    jmp (dispatch_lo)
    lsr
    eor (TMPCHR,x)
    eor ($56,x)
    eor ($5C,x)
    eor (PALNTS,x)
    eor ($B9,x)
    brk
    lda (DSTAT),y
    adc SOUNDR
    lda $B480,y
    jmp $4165
    lda $B640,y
    jmp $4165
    lda $B720,y
    jmp $4165
    lda $B783,y
    beq $416D
    lda #$01
    sta spawn_interval
    rts
    lda RANDOM
    and #$03
    clc
    adc #$6F
    ldy RANDOM
    bpl $417C
    adc #$2C
    sta $97
    lda RANDOM
    and #$7E
    cmp #$03
    bcc $4167
    sta $96
    ldy #$00
    lda ($96),y
    sta $98
    and #$3F
    cmp #$05
    bcs $4167
    lda $99
    sta ($96),y
    ldx spawn_type_idx
    inc entity_count_0,x
    sed
    lda score_lo
    clc
    adc #$01
    sta score_lo
    lda score_hi
    adc #$00
    sta score_hi
    cld
    txa
    asl
    tay
    lda $41CB,y
    sta dispatch_lo
    lda $41CC,y
    sta dispatch_hi
    ldy $BE
    lda RANDOM
    and #$1F
    jmp (dispatch_lo)
    cmp SOUNDR,x
    nop
    eor ($FF,x)
    eor ($14,x)
    .byte $42
    and #$42
    sta entity_grp1_type,y
    lda $96
    sta entity_grp1_xpos,y
    lda $97
    sta entity_grp1_ypos,y
    lda $98
    sta entity_grp1_dir,y
    jmp $423B
    sta $B580,y
    lda $96
    sta $B400,y
    lda $97
    sta $B480,y
    lda $98
    sta $B500,y
    jmp $423B
    sta $B6C0,y
    lda $96
    sta $B600,y
    lda $97
    sta $B640,y
    lda $98
    sta $B680,y
    jmp $423B
    sta $B760,y
    lda $96
    sta $B700,y
    lda $97
    sta $B720,y
    lda $98
    sta $B740,y
    jmp $423B
    sta $B789,y
    lda $96
    sta $B780,y
    lda $97
    sta $B783,y
    lda $98
    sta $B786,y
    lda $51A0
    sta spawn_interval
    rts
    .byte $D2
    and #$1F
    jmp (dispatch_lo)
    cmp SOUNDR,x
    nop
    eor ($FF,x)
    eor ($14,x)
    .byte $42
    and #$42
    sta entity_grp1_type,y
    lda $96
    sta entity_grp1_xpos,y
    lda $97
    sta entity_grp1_ypos,y
    lda $98
    sta entity_grp1_dir,y
    jmp $423B
    sta $B580,y
    lda $96
    sta $B400,y
    lda $97
    sta $B480,y
    lda $98
    sta FREQ,y
    pha
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    plp
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
    brk
    brk
    brk
    brk
    plp
.proc score_add_foul
    ; Add CC3-5 to FOUL score (CC0-2) in BCD
    sed
    lda foul_lo
    clc
    adc score_delta_lo
    sta foul_lo
    lda foul_mid
    adc score_delta_mid
    sta foul_mid
    lda foul_hi
    adc score_delta_hi
    sta foul_hi
    cld
    rts
.endp

.proc power_add
    ; Add CC3-4 to POWER (11B-C) in BCD
    sed
    lda power_lo
    clc
    adc score_delta_mid
    sta power_lo
    lda power_hi
    adc score_delta_hi
    sta power_hi
    cld
    rts
.endp

.proc power_subtract
    ; Subtract CC3-4 from POWER (11B-C) in BCD. If underflow: POWER=0, game_state=9 (game over)
    sed
    lda power_lo
    sec
    sbc score_delta_mid
    sta power_lo
    lda power_hi
    sbc score_delta_hi
    sta power_hi
    bcs $435D
    lda #$00
    sta power_hi
    sta power_lo
    lda #$09
    sta game_state
    lda #$01
    sta $BF04
    cld
    rts
.endp

    bpl $4301
    sta (DSTAT,x)
    .byte $77
    adc #$A5
    tax
    cmp #$08
    bcc $4375
    ldy #$80
    jmp $6977
    ldy #$01
    jmp $6977
    ldy #$00
    lda ($BB),y
    cmp #$14
    bcc $438C
    cmp #$19
    .byte $B0                     ; $437F
death_state:
    .byte $AD                     ; $4380
    .byte $FC
    .byte $02
    cmp #$1C
    beq $4390
    cmp #$FF
    beq $4390
    lda #$02
    sta game_state
    rts
    ; DEAD CODE: unreachable gameplay handler variant. Branches to sprite data at $4414/$4424. Contains S($3E)/V($10) key checks that were likely abandoned during development
    sta COLOR2
    ldx chbase_current
    inx
    inx
    cpx #$50
    bne $43A0
    ldx $519E
    stx chbase_current
    stx CHBAS
    jsr entity_update_all
    lda CH
    cmp #$25
    beq $43F5
    lda sub_state
    asl
    tax
    lda substate_dispatch_table,x
    sta dispatch_lo
    lda $6B81,x
    sta dispatch_hi
    jsr $5572
    lda wizard_x_tile
    sta HPOSP3
    jsr player_sprite
    jsr player_state_handler
    jsr score_display
    lda key_lock
    bne $4413
    lda sub_state
    bne $4413
    lda CH
    cmp #$10
    beq $4414
    cmp #$3E
    beq $4424
    cmp #$1C
    bne $43F1
    lda #$05
    sta game_state
    rts
    jmp (dispatch_lo)
    ldx #$02
    lda map_view_colors,x
    sta COLOR0,x
    dex
    bpl $43F7
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
sprite_data:
    ; Sprite shape data for wizard, wand, monsters etc (bitmap patterns)
    .byte $1C
    ror $FFFF,x
    ror $4848,x
    php
    asl $7F3F
    ror $74FC,x
    bit ICHIDZ
    .byte $1C
    rol $1C3E,x
    php
    php
    php
    brk
    brk
    bpl $445B
    .byte $7C
    inc $1010,x
    bpl $4469
    ldx #$45
    .byte $02
    brk
    bpl $4457
    bpl $4449
    bit CRITIC
    sta ($81,x)
    .byte $42
    bit DSKFMS
    brk
    brk
    brk
    brk
    ora CMCMD
    .byte $03
    .byte $03
    .byte $80
    .byte $80
    .byte $80
    .byte $80
    ldy #$E0
    rti
    cpy #$AB
    .byte $FB
    .byte $FF
    inc $7F7F
    ror $D57E,x
    .byte $DF
    sbc $FEFF,x
    inc $7E7E,x
    eor $80,x
    ora $2DB4,x
    clv
    ora ($AA,x)
    brk
    brk
    brk
    .byte $14
    .byte $3A
    adc $FFFE,x
    jsr $1004
    sty ICDNOZ
    php
    rti
    .byte $12
    clc
    .byte $3C
    clc
    bit CRITIC
    .byte $42
    bit DSKFMS
    php
    jsr $1008
    bpl $448E
    bpl $4490
    brk
    brk
    brk
    eor #$2A
    rol.w LNFLG,x
    brk
    .byte $1C
    php
    .byte $1C
    rol ICAX1Z,x
    rol ABUFPT,x
    php
    jsr $1008
    bpl $44A6
    bpl $44A8
    brk
    brk
    brk
    eor #$2A
    rol.w LNFLG,x
    .byte $22
    rol $FF5B,x
    .byte $AB
    .byte $83
    dec HOLDCH,x
    brk
    sec
    .byte $7C
    tsx
    plp
    jmp (LNFLG)
    and $7C1A,y
    clv
    clv
    plp
    plp
    jmp ($9C7E)
    .byte $EB
    cmp $EBFF,x
    cmp COUNTR,x
    .byte $22
    rol $FF5B,x
    .byte $AB
    dec HOLDCH,x
    brk
    brk
    .byte $3C
    ror FTYPE
    asl DOSINI
    sec
    brk
    brk
    brk
    clc
    clc
    brk
    clc
    clc
    brk
    brk
    brk
    clc
    clc
    brk
    clc
    clc
    bmi $44E7
    .byte $0C
    clc
    bmi $44FD
    .byte $0C
    asl LNFLG
    sta (LNFLG,x)
    brk
    clc
    clc
    brk
    brk
    sta (ICHIDZ,x)
    .byte $04
    bpl $4478
    and (WARMST,x)
    rti
    .byte $12
    brk
    .byte $3C
    ror DOSINI
    clc
    brk
    clc
    brk
    brk
    .byte $3C
    .byte $42
    .byte $42
    .byte $42
    .byte $42
    .byte $3C
    brk
    brk
    .byte $3C
    .byte $42
    .byte $5A
    .byte $5A
    .byte $42
    .byte $3C
    brk
    brk
    .byte $3C
    lsr $724E
    .byte $72
    .byte $3C
    brk
    clc
    .byte $42
    brk
    sta ($81,x)
    brk
    .byte $42
    clc
    brk
    clc
    brk
    .byte $42
    .byte $42
    brk
    clc
    brk
    .byte $7C
    inc $AAAA,x
    tax
    tax
    .byte $54
    inc DDEVIC,x
    ora (BUFADR,x)
    .byte $1F
    .byte $1F
    .byte $1B
    .byte $1F
    brk
    brk
    brk
    .byte $54
    .byte $FC
    .byte $FC
    jmp (HOLDCH)
    brk
    brk
    .byte $14
    .byte $3A
    adc $FFFE,x
    ora (CASINI,x)
    .byte $04
    php
    bcc $45AE
    rts
    bcc $4551
    sec
    bpl $458C
    .byte $74
    .byte $7C
    .byte $5C
    sec
    brk
    ror BUFSTR
    sei
    sei
    jmp (TOADR)
    brk
    rts
    rts
    rts
    rts
    rts
    ror.w LNFLG,x
    .byte $63
    .byte $77
    .byte $7F
    .byte $6B
    .byte $63
    .byte $63
    brk
    brk
    ror DELTAR
    ror $6E7E,x
    ror LNFLG
    brk
    .byte $3C
    ror TOADR
    ror TOADR
    .byte $3C
    brk
    .byte $FF
    .byte $FF
    .byte $EF
    .byte $E7
    .byte $EF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $BF
    .byte $DF
    .byte $BF
    .byte $BF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    brk
    brk
    brk
    brk
    brk
    ora (CMCMD,x)
    .byte $03
    brk
    brk
    brk
    .byte $80
    .byte $80
    ldy #$E0
    rti
    ora ($0B,x)
    .byte $1F
    .byte $0F
    .byte $5F
    ror $F9FF
    cpy #$95
    .byte $DF
    sbc $7CDE,x
    ror.w $00B7,x
    sec
    bpl $45EC
    .byte $74
    .byte $7C
    .byte $5C
    sec
    brk
    brk
    brk
    .byte $7F
    rol $081C,x
    rol $6600,x
    ror NOCKSM
    .byte $3C
    ror TOADR
    brk
    brk
    ror TOADR
    .byte $3C
    clc
    clc
    clc
    brk
    brk
    ror $180C,x
    bmi $4636
    ror.w LNFLG,x
    asl $1818,x
    clc
    clc
    asl.w LNFLG,x
    .byte $1C
    ror $FEFF,x
    pha
    pha
    php
    asl $7F3F
    ror $74FC,x
    bit ICHIDZ
    .byte $1C
    rol $7F7F,x
    rol $1414,x
    .byte $04
    bvs $45F2
    inc $3F7F,x
    rol $0404
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    .byte $1C
    ror $FFFF,x
    ror $4848,x
    php
    asl $7F3F
    ror $74FC,x
    bit ICHIDZ
    .byte $1C
    rol $1C3E,x
    php
    php
    php
    brk
    brk
    bpl $465B
    .byte $7C
    inc $1010,x
    bpl $4679
    jsr $0255
    ora TMPCHR
    jsr $1850
    bit CRITIC
    .byte $42
    .byte $42
    .byte $42
    bit DSKFMS
    brk
    brk
    brk
    brk
    ora CMCMD
    .byte $03
    .byte $03
    .byte $80
    .byte $80
    .byte $80
    .byte $80
    ldy #$E0
    rti
    cpy #$AB
    .byte $FB
    .byte $FF
    inc $7F7F
    ror $D57E,x
    .byte $DF
    sbc $FEFF,x
    inc $7E7E,x
    tax
    ora ($B8,x)
    and $1DB4
    .byte $80
    eor LNFLG,x
    brk
    brk
    .byte $14
    .byte $3A
    adc $FFFE,x
    jsr $1004
    sty ICDNOZ
    php
    rti
    .byte $12
    clc
    .byte $3C
    clc
    bit CRITIC
    .byte $42
    bit DSKFMS
    jsr $4004
    bpl $468D
    bpl $468F
    bpl $4681
    brk
    brk
    eor #$2A
    rol.w LNFLG,x
    brk
    .byte $1C
    php
    .byte $1C
    rol ICAX1Z,x
    rol ABUFPT,x
    brk
    .byte $3C
    ror DOSINI
    clc
    bmi $4715
    brk
    brk
    ror $180C,x
    .byte $0C
    ror NOCKSM
    brk
    .byte $22
    rol $FF5B,x
    .byte $AB
    .byte $83
    dec HOLDCH,x
    brk
    sec
    .byte $7C
    tsx
    plp
    jmp (LNFLG)
    and $7C1A,y
    clv
    clv
    plp
    plp
    jmp ($9C7E)
    .byte $EB
    cmp $EBFF,x
    cmp COUNTR,x
    .byte $22
    rol $FF5B,x
    .byte $AB
    dec HOLDCH,x
    brk
    brk
    .byte $3C
    ror FTYPE
    asl DOSINI
    sec
    brk
    brk
    brk
    clc
    clc
    brk
    clc
    clc
    brk
    brk
    brk
    clc
    clc
    brk
    clc
    clc
    bmi $46E7
    .byte $0C
    clc
    bmi $46FD
    .byte $0C
    asl LNFLG
    brk
    brk
    .byte $3C
    bit ICBALZ
    .byte $3C
    brk
    brk
    php
    rti
    .byte $02
    jsr $1184
    .byte $02
    pha
    jsr $1004
    sty ICDNOZ
    php
    rti
    .byte $12
    brk
    .byte $3C
    .byte $42
    .byte $42
    .byte $42
    .byte $42
    .byte $3C
    brk
    brk
    .byte $3C
    .byte $42
    .byte $5A
    .byte $5A
    .byte $42
    .byte $3C
    brk
    brk
    .byte $3C
    lsr $724E
    .byte $72
    .byte $3C
    brk
    clc
    .byte $42
    brk
    sta ($81,x)
    brk
    .byte $42
    clc
    brk
    clc
    brk
    .byte $42
    .byte $42
    brk
    clc
    brk
    .byte $7C
    inc $AAAA,x
    tax
    tax
    .byte $54
    inc DDEVIC,x
    ora (BUFADR,x)
    .byte $1F
    .byte $1F
    .byte $1B
    .byte $1F
    brk
    brk
    brk
    .byte $54
    .byte $FC
    .byte $FC
    jmp (HOLDCH)
    brk
    brk
    .byte $14
    .byte $3A
    adc $FFFE,x
    ora (CASINI,x)
    .byte $04
    php
    bcc $47AE
    rts
    bcc $4751
    sec
    bpl $478C
    .byte $74
    .byte $7C
    .byte $5C
    sec
    brk
    ror BUFSTR
    sei
    sei
    jmp (TOADR)
    brk
    rts
    rts
    rts
    rts
    rts
    ror.w LNFLG,x
    .byte $63
    .byte $77
    .byte $7F
    .byte $6B
    .byte $63
    .byte $63
    brk
    brk
    ror DELTAR
    ror $6E7E,x
    ror LNFLG
    brk
    .byte $3C
    ror TOADR
    ror TOADR
    .byte $3C
    brk
    .byte $FF
    .byte $FF
    .byte $EF
    .byte $E7
    .byte $EF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $BF
    .byte $DF
    .byte $BF
    .byte $BF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    brk
    brk
    brk
    brk
    brk
    ora (CMCMD,x)
    .byte $03
    brk
    brk
    brk
    .byte $80
    .byte $80
    ldy #$E0
    rti
    ora ($0B,x)
    .byte $1F
    .byte $0F
    .byte $5F
    ror $F9FF
    cpy #$95
    .byte $DF
    sbc $7CDE,x
    ror.w $00B7,x
    sec
    bpl $47EC
    .byte $74
    .byte $7C
    .byte $5C
    sec
    brk
    brk
    brk
    .byte $7F
    rol $081C,x
    rol $6600,x
    ror NOCKSM
    .byte $3C
    ror TOADR
    brk
    brk
    ror TOADR
    .byte $3C
    clc
    clc
    clc
    brk
    brk
    ror $180C,x
    bmi $4836
    ror.w LNFLG,x
    asl $1818,x
    clc
    clc
    asl.w LNFLG,x
    .byte $1C
    ror $FEFF,x
    pha
    pha
    php
    asl $7F3F
    ror $74FC,x
    bit ICHIDZ
    .byte $1C
    rol $7F7F,x
    rol $1414,x
    .byte $04
    bvs $47F2
    inc $3F7F,x
    rol $0404
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    .byte $1C
    ror $FFFF,x
    ror $4848,x
    php
    asl $7F3F
    ror $74FC,x
    bit ICHIDZ
    .byte $1C
    rol $1C3E,x
    php
    php
    php
    brk
    brk
    bpl $485B
    .byte $7C
    inc $1010,x
    bpl $4839
    plp
    bpl $482E
    eor $A2
    rti
    brk
    clc
    bit ICBALZ
    bit ICBALZ
    bit ICBALZ
    clc
    brk
    brk
    brk
    brk
    ora CMCMD
    .byte $03
    .byte $03
    cpy #$80
    .byte $80
    .byte $80
    ldy #$E0
    rti
    cpy #$AB
    .byte $FB
    .byte $FF
    inc $7F7F
    ror $D57E,x
    .byte $DF
    sbc $FEFF,x
    inc $7E7E,x
    eor $80,x
    ora $2DB4,x
    clv
    ora ($AA,x)
    brk
    brk
    brk
    .byte $14
    .byte $3A
    adc $FFFE,x
    jsr $1004
    sty ICDNOZ
    php
    rti
    .byte $12
    clc
    .byte $3C
    clc
    bit CRITIC
    .byte $42
    bit DSKFMS
    bpl $4882
    brk
    .byte $54
    bpl $488E
    bpl $4890
    brk
    brk
    brk
    eor #$2A
    rol.w LNFLG,x
    brk
    .byte $1C
    php
    .byte $1C
    rol ICAX1Z,x
    rol ABUFPT,x
    brk
    .byte $3C
    ror DOSINI
    clc
    bmi $4915
    brk
    brk
    ror $180C,x
    .byte $0C
    ror NOCKSM
    brk
    .byte $22
    rol $FF5B,x
    .byte $AB
    .byte $83
    dec HOLDCH,x
    brk
    sec
    .byte $7C
    tsx
    plp
    jmp (LNFLG)
    and $7C2A,y
    clv
    clv
    plp
    plp
    jmp ($9C7E)
    .byte $EB
    cmp $EBFF,x
    cmp COUNTR,x
    .byte $22
    rol $FF5B,x
    .byte $AB
    dec HOLDCH,x
    brk
    brk
    .byte $3C
    ror FTYPE
    asl DOSINI
    sec
    brk
    brk
    brk
    clc
    clc
    brk
    clc
    clc
    brk
    brk
    brk
    clc
    clc
    brk
    clc
    clc
    bmi $48E7
    .byte $0C
    clc
    bmi $48FD
    .byte $0C
    asl LNFLG
    brk
    ror $4242,x
    .byte $42
    .byte $42
    ror $0400,x
    bpl $4934
    bpl $48F9
    .byte $80
    and (VCOUNT_START,x)
    jsr $1004
    sty ICDNOZ
    php
    rti
    .byte $12
    brk
    .byte $3C
    .byte $42
    .byte $42
    .byte $42
    .byte $42
    .byte $3C
    brk
    brk
    .byte $3C
    .byte $42
    .byte $5A
    .byte $5A
    .byte $42
    .byte $3C
    brk
    brk
    .byte $3C
    lsr $724E
    .byte $72
    .byte $3C
    brk
    clc
    .byte $42
    brk
    sta ($81,x)
    brk
    .byte $42
    clc
    brk
    clc
    brk
    .byte $42
    .byte $42
    brk
    clc
    brk
    .byte $7C
    inc $AAAA,x
    tax
    tax
    .byte $54
    inc DDEVIC,x
    ora (BUFADR,x)
    .byte $1F
    .byte $1F
    .byte $1B
    .byte $1F
    brk
    brk
    brk
    .byte $54
    .byte $FC
    .byte $FC
    jmp (HOLDCH)
    brk
    brk
    .byte $14
    .byte $3A
    adc $FFFE,x
    ora (CASINI,x)
    .byte $04
    php
    bcc $49AE
    rts
    bcc $4951
    sec
    bpl $498C
    .byte $74
    .byte $7C
    .byte $5C
    sec
    brk
    ror BUFSTR
    sei
    sei
    jmp (TOADR)
    brk
    rts
    rts
    rts
    rts
    rts
    ror.w LNFLG,x
    .byte $63
    .byte $77
    .byte $7F
    .byte $6B
    .byte $63
    .byte $63
    brk
    brk
    ror DELTAR
    ror $6E7E,x
    ror LNFLG
    brk
    .byte $3C
    ror TOADR
    ror TOADR
    .byte $3C
    brk
    .byte $FF
    .byte $FF
    .byte $EF
    .byte $E7
    .byte $EF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $BF
    .byte $DF
    .byte $BF
    .byte $BF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    brk
    brk
    brk
    brk
    brk
    ora (CMCMD,x)
    .byte $03
    brk
    brk
    brk
    .byte $80
    .byte $80
    ldy #$E0
    rti
    ora ($0B,x)
    .byte $1F
    .byte $0F
    .byte $5F
    ror $F9FF
    cpy #$95
    .byte $DF
    sbc $7CDE,x
    ror.w $00B7,x
    sec
    bpl $49EC
    .byte $74
    .byte $7C
    .byte $5C
    sec
    brk
    brk
    brk
    .byte $7F
    rol $081C,x
    rol $6600,x
    ror NOCKSM
    .byte $3C
    ror TOADR
    brk
    brk
    ror TOADR
    .byte $3C
    clc
    clc
    clc
    brk
    brk
    ror $180C,x
    bmi $4A36
    ror.w LNFLG,x
    asl $1818,x
    clc
    clc
    asl.w LNFLG,x
    .byte $1C
    ror $FEFF,x
    pha
    pha
    php
    asl $7F3F
    ror $74FC,x
    bit ICHIDZ
    .byte $1C
    rol $7F7F,x
    rol $1414,x
    .byte $04
    bvs $49F2
    inc $3F7F,x
    rol $0404
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    .byte $1C
    ror $FFFF,x
    ror $4848,x
    php
    asl $7F3F
    ror $74FC,x
    bit ICHIDZ
    .byte $1C
    rol $1C3E,x
    php
    php
    php
    brk
    brk
    bpl $4A5B
    .byte $7C
    inc $1010,x
    bpl $4A3D
    php
    .byte $14
    ldy #$45
    ldx #$05
    brk
    clc
    clc
    clc
    clc
    clc
    clc
    clc
    clc
    brk
    brk
    brk
    brk
    ora CMCMD
    .byte $03
    .byte $03
    cpy #$80
    .byte $80
    .byte $80
    ldy #$E0
    rti
    cpy #$AB
    .byte $FB
    .byte $FF
    inc $7F7F
    ror $D57E,x
    .byte $DF
    sbc $FEFF,x
    inc $7E7E,x
    tax
    ora ($B8,x)
    and $1DB4
    .byte $80
    eor LNFLG,x
    brk
    brk
    .byte $14
    .byte $3A
    adc $FFFE,x
    jsr $1004
    sty ICDNOZ
    php
    rti
    .byte $12
    clc
    .byte $3C
    clc
    bit CRITIC
    .byte $42
    bit DSKFMS
    php
    rti
    .byte $12
    brk
    bpl $4A8E
    bpl $4A90
    brk
    brk
    brk
    eor #$2A
    rol.w LNFLG,x
    brk
    .byte $1C
    php
    .byte $1C
    rol ICAX1Z,x
    rol ABUFPT,x
    brk
    .byte $3C
    ror DOSINI
    clc
    bmi $4B15
    brk
    brk
    ror $180C,x
    .byte $0C
    ror NOCKSM
    brk
    .byte $22
    rol $FF5B,x
    .byte $AB
    dec HOLDCH,x
    brk
    brk
    sec
    .byte $7C
    tsx
    plp
    jmp (LNFLG)
    and $7C2A,y
    clv
    clv
    plp
    plp
    jmp ($9C7E)
    .byte $EB
    cmp $EBFF,x
    cmp COUNTR,x
    .byte $22
    rol $FF5B,x
    .byte $AB
    dec HOLDCH,x
    brk
    brk
    .byte $3C
    ror FTYPE
    asl DOSINI
    sec
    brk
    brk
    brk
    clc
    clc
    brk
    clc
    clc
    brk
    brk
    brk
    clc
    clc
    brk
    clc
    clc
    bmi $4AE7
    .byte $0C
    clc
    bmi $4AFD
    .byte $0C
    asl LNFLG
    .byte $3C
    .byte $42
    sta ($99,x)
    sta $4281,y
    .byte $3C
    bpl $4A76
    bpl $4B36
    php
    sta (VCOUNT_START),y
    pha
    jsr $1004
    sty ICDNOZ
    php
    rti
    .byte $12
    brk
    .byte $3C
    .byte $42
    .byte $42
    .byte $42
    .byte $42
    .byte $3C
    brk
    brk
    .byte $3C
    .byte $42
    .byte $5A
    .byte $5A
    .byte $42
    .byte $3C
    brk
    brk
    .byte $3C
    lsr $724E
    .byte $72
    .byte $3C
    brk
    clc
    .byte $42
    brk
    sta ($81,x)
    brk
    .byte $42
    clc
    brk
    clc
    brk
    .byte $42
    .byte $42
    brk
    clc
    brk
    .byte $7C
    inc $AAAA,x
    tax
    tax
    .byte $54
    inc DDEVIC,x
    ora (BUFADR,x)
    .byte $1F
    .byte $1F
    .byte $1B
    .byte $1F
    brk
    brk
    brk
    .byte $54
    .byte $FC
    .byte $FC
    jmp (HOLDCH)
    brk
    brk
    .byte $14
    .byte $3A
    adc $FFFE,x
    ora (CASINI,x)
    .byte $04
    php
    bcc $4BAE
    rts
    bcc $4B51
    sec
    bpl $4B8C
    .byte $74
    .byte $7C
    .byte $5C
    sec
    brk
    ror BUFSTR
    sei
    sei
    jmp (TOADR)
    brk
    rts
    rts
    rts
    rts
    rts
    ror.w LNFLG,x
    .byte $63
    .byte $77
    .byte $7F
    .byte $6B
    .byte $63
    .byte $63
    brk
    brk
    ror DELTAR
    ror $6E7E,x
    ror LNFLG
    brk
    .byte $3C
    ror TOADR
    ror TOADR
    .byte $3C
    brk
    .byte $FF
    .byte $FF
    .byte $EF
    .byte $E7
    .byte $EF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $BF
    .byte $DF
    .byte $BF
    .byte $BF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    brk
    brk
    brk
    brk
    brk
    ora (CMCMD,x)
    .byte $03
    brk
    brk
    brk
    .byte $80
    .byte $80
    ldy #$E0
    rti
    ora ($0B,x)
    .byte $1F
    .byte $0F
    .byte $5F
    ror $F9FF
    cpy #$95
    .byte $DF
    sbc $7CDE,x
    ror.w $00B7,x
    sec
    bpl $4BEC
    .byte $74
    .byte $7C
    .byte $5C
    sec
    brk
    brk
    brk
    .byte $7F
    rol $081C,x
    rol $6600,x
    ror NOCKSM
    .byte $3C
    ror TOADR
    brk
    brk
    ror TOADR
    .byte $3C
    clc
    clc
    clc
    brk
    brk
    ror $180C,x
    bmi $4C36
    ror.w LNFLG,x
    asl $1818,x
    clc
    clc
    asl.w LNFLG,x
    .byte $1C
    ror $FEFF,x
    pha
    pha
    php
    asl $7F3F
    ror $74FC,x
    bit ICHIDZ
    .byte $1C
    rol $7F7F,x
    rol $1414,x
    .byte $04
    bvs $4BF2
    inc $3F7F,x
    rol $0404
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    .byte $1C
    ror $FFFF,x
    ror $4848,x
    php
    asl $7F3F
    ror $74FC,x
    bit ICHIDZ
    .byte $1C
    rol $1C3E,x
    php
    php
    php
    brk
    brk
    bpl $4C5B
    .byte $7C
    inc $1010,x
    bpl $4C2D
    asl
    .byte $44
    ldy #$40
    .byte $04
    asl
    .byte $04
    clc
    bit ICBALZ
    bit ICBALZ
    bit ICBALZ
    clc
    brk
    brk
    brk
    brk
    ora CMCMD
    .byte $03
    .byte $03
    cpy #$80
    .byte $80
    .byte $80
    ldy #$E0
    rti
    cpy #$AB
    .byte $FB
    .byte $FF
    inc $7F7F
    ror $D57E,x
    .byte $DF
    sbc $FEFF,x
    inc $7E7E,x
    eor $80,x
    ora $2DB4,x
    clv
    ora ($AA,x)
    brk
    brk
    brk
    .byte $14
    .byte $3A
    adc $FFFE,x
    jsr $1004
    sty ICDNOZ
    php
    rti
    .byte $12
    clc
    .byte $3C
    clc
    bit CRITIC
    .byte $42
    bit DSKFMS
    rti
    .byte $04
    jsr $1012
    bpl $4C8F
    bpl $4C81
    brk
    brk
    eor #$2A
    rol.w LNFLG,x
    brk
    .byte $1C
    php
    .byte $1C
    rol ICAX1Z,x
    rol ABUFPT,x
    brk
    .byte $3C
    ror DOSINI
    clc
    bmi $4D15
    brk
    brk
    ror $180C,x
    .byte $0C
    ror NOCKSM
    brk
    .byte $22
    rol $FF5B,x
    .byte $AB
    dec HOLDCH,x
    brk
    brk
    sec
    .byte $7C
    tsx
    plp
    jmp (LNFLG)
    and $7C32,y
    clv
    clv
    plp
    plp
    jmp ($9C7E)
    .byte $EB
    cmp $EBFF,x
    cmp COUNTR,x
    .byte $22
    rol $FF5B,x
    .byte $AB
    dec HOLDCH,x
    brk
    brk
    .byte $3C
    ror FTYPE
    asl DOSINI
    sec
    brk
    brk
    brk
    clc
    clc
    brk
    clc
    clc
    brk
    brk
    brk
    clc
    clc
    brk
    clc
    clc
    bmi $4CE7
    .byte $0C
    clc
    bmi $4CFD
    .byte $0C
    asl LNFLG
    sta $4224,y
    sta $4299,y
    bit $99
    .byte $04
    bpl $4C77
    jsr $400A
    ora ($84),y
    jsr $1004
    sty ICDNOZ
    php
    rti
    .byte $12
    brk
    .byte $3C
    .byte $42
    .byte $42
    .byte $42
    .byte $42
    .byte $3C
    brk
    brk
    .byte $3C
    .byte $42
    .byte $5A
    .byte $5A
    .byte $42
    .byte $3C
    brk
    brk
    .byte $3C
    lsr $724E
    .byte $72
    .byte $3C
    brk
    clc
    .byte $42
    brk
    sta ($81,x)
    brk
    .byte $42
    clc
    brk
    clc
    brk
    .byte $42
    .byte $42
    brk
    clc
    brk
    .byte $7C
    inc $AAAA,x
    tax
    tax
    .byte $54
    inc DDEVIC,x
    ora (BUFADR,x)
    .byte $1F
    .byte $1F
    .byte $1B
    .byte $1F
    brk
    brk
    brk
    .byte $54
    .byte $FC
    .byte $FC
    jmp (HOLDCH)
    brk
    brk
    .byte $14
    .byte $3A
    adc $FFFE,x
    ora (CASINI,x)
    .byte $04
    php
    bcc $4DAE
    rts
    bcc $4D51
    sec
    bpl $4D8C
    .byte $74
    .byte $7C
    .byte $5C
    sec
    brk
    ror BUFSTR
    sei
    sei
    jmp (TOADR)
    brk
    rts
    rts
    rts
    rts
    rts
    ror.w LNFLG,x
    .byte $63
    .byte $77
    .byte $7F
    .byte $6B
    .byte $63
    .byte $63
    brk
    brk
    ror DELTAR
    ror $6E7E,x
    ror LNFLG
    brk
    .byte $3C
    ror TOADR
    ror TOADR
    .byte $3C
    brk
    .byte $FF
    .byte $FF
    .byte $EF
    .byte $E7
    .byte $EF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $BF
    .byte $DF
    .byte $BF
    .byte $BF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    brk
    brk
    brk
    brk
    brk
    ora (CMCMD,x)
    .byte $03
    brk
    brk
    brk
    .byte $80
    .byte $80
    ldy #$E0
    rti
    ora ($0B,x)
    .byte $1F
    .byte $0F
    .byte $5F
    ror $F9FF
    cpy #$95
    .byte $DF
    sbc $7CDE,x
    ror.w $00B7,x
    sec
    bpl $4DEC
    .byte $74
    .byte $7C
    .byte $5C
    sec
    brk
    brk
    brk
    .byte $7F
    rol $081C,x
    rol $6600,x
    ror NOCKSM
    .byte $3C
    ror TOADR
    brk
    brk
    ror TOADR
    .byte $3C
    clc
    clc
    clc
    brk
    brk
    ror $180C,x
    bmi $4E36
    ror.w LNFLG,x
    asl $1818,x
    clc
    clc
    asl.w LNFLG,x
    .byte $1C
    ror $FEFF,x
    pha
    pha
    php
    asl $7F3F
    ror $74FC,x
    bit ICHIDZ
    .byte $1C
    rol $7F7F,x
    rol $1414,x
    .byte $04
    bvs $4DF2
    inc $3F7F,x
    rol $0404
charset:
    ; Custom character set (CHBASE=E)
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    .byte $1C
    ror $FFFF,x
    ror $4848,x
    php
    asl $7F3F
    ror $74FC,x
    bit ICHIDZ
    .byte $1C
    rol $1C3E,x
    php
    php
    php
    brk
    brk
    bpl $4E5B
    .byte $7C
    inc $1010,x
    bpl $4E2E
    ldx #$45
    ldy #$00
    .byte $14
    php
    .byte $14
    clc
    bit CRITIC
    .byte $42
    .byte $42
    .byte $42
    bit DSKFMS
    brk
    brk
    brk
    brk
    ora CMCMD
    .byte $03
    .byte $03
    cpy #$80
    .byte $80
    .byte $80
    ldy #$E0
    rti
    cpy #$AB
    .byte $FB
    .byte $FF
    inc $7F7F
    ror $D57E,x
    .byte $DF
    sbc $FEFF,x
    inc $7E7E,x
    tax
    ora ($B8,x)
    and $1DB4
    .byte $80
    eor LNFLG,x
    brk
    brk
    .byte $14
    .byte $3A
    adc $FFFE,x
    jsr $1004
    sty ICDNOZ
    php
    rti
    .byte $12
    clc
    .byte $3C
    clc
    bit CRITIC
    .byte $42
    bit DSKFMS
    php
    bpl $4EBF
    bpl $4E8D
    bpl $4E8F
    bpl $4E81
    brk
    brk
    eor #$2A
    rol.w LNFLG,x
    brk
    .byte $1C
    php
    .byte $1C
    rol ICAX1Z,x
    rol ABUFPT,x
    brk
    .byte $3C
    ror DOSINI
    clc
    bmi $4F15
    brk
    brk
    ror $180C,x
    .byte $0C
    ror NOCKSM
    brk
    .byte $22
    rol $FF5B,x
    .byte $AB
    dec HOLDCH,x
    brk
    brk
    sec
    .byte $7C
    tsx
    plp
    jmp (LNFLG)
    and $7C32,y
    clv
    clv
    plp
    plp
    jmp ($9C7E)
    .byte $EB
    cmp $EBFF,x
    cmp COUNTR,x
    .byte $22
    rol $FF5B,x
    .byte $AB
    dec HOLDCH,x
    brk
    brk
    .byte $3C
    ror FTYPE
    asl DOSINI
    sec
    brk
    brk
    brk
    clc
    clc
    brk
    clc
    clc
    brk
    brk
    brk
    clc
    clc
    brk
    clc
    clc
    bmi $4EE7
    .byte $0C
    clc
    bmi $4EFD
    .byte $0C
    asl LNFLG
    clc
    .byte $42
    brk
    sta $0099,y
    .byte $42
    clc
    brk
    .byte $02
    bpl $4E78
    and (LNFLG,x)
    pha
    ora (ICHIDZ,x)
    .byte $04
    bpl $4E80
    and (WARMST,x)
    rti
    .byte $12
    brk
    .byte $3C
    .byte $42
    .byte $42
    .byte $42
    .byte $42
    .byte $3C
    brk
    brk
    .byte $3C
    .byte $42
    .byte $5A
    .byte $5A
    .byte $42
    .byte $3C
    brk
    brk
    .byte $3C
    lsr $724E
    .byte $72
    .byte $3C
    brk
    clc
    .byte $42
    brk
    sta ($81,x)
    brk
    .byte $42
    clc
    brk
    clc
    brk
    .byte $42
    .byte $42
    brk
    clc
    brk
    .byte $7C
    inc $AAAA,x
    tax
    tax
    .byte $54
    inc DDEVIC,x
    ora (BUFADR,x)
    .byte $1F
    .byte $1F
    .byte $1B
    .byte $1F
    brk
    brk
    brk
    .byte $54
    .byte $FC
    .byte $FC
    jmp (HOLDCH)
    brk
    brk
    .byte $14
    .byte $3A
    adc $FFFE,x
    ora (CASINI,x)
    .byte $04
    php
    bcc $4FAE
    rts
    bcc $4F51
    sec
    bpl $4F8C
    .byte $74
    .byte $7C
    .byte $5C
    sec
    brk
    ror BUFSTR
    sei
    sei
    jmp (TOADR)
    brk
    rts
    rts
    rts
    rts
    rts
    ror.w LNFLG,x
    .byte $63
    .byte $77
    .byte $7F
    .byte $6B
    .byte $63
    .byte $63
    brk
    brk
    ror DELTAR
    ror $6E7E,x
    ror LNFLG
    brk
    .byte $3C
    ror TOADR
    ror TOADR
    .byte $3C
    brk
    .byte $FF
    .byte $FF
    .byte $EF
    .byte $E7
    .byte $EF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $BF
    .byte $DF
    .byte $BF
    .byte $BF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    brk
    brk
    brk
    brk
    brk
    ora (CMCMD,x)
    .byte $03
    brk
    brk
    brk
    .byte $80
    .byte $80
    ldy #$E0
    rti
    ora ($0B,x)
    .byte $1F
    .byte $0F
    .byte $5F
    ror $F9FF
    cpy #$95
    .byte $DF
    sbc $7CDE,x
    ror.w $00B7,x
    sec
    bpl $4FEC
    .byte $74
    .byte $7C
    .byte $5C
    sec
    brk
    brk
    brk
    .byte $7F
    rol $081C,x
    rol $6600,x
    ror NOCKSM
    .byte $3C
    ror TOADR
    brk
    brk
    ror TOADR
    .byte $3C
    clc
    clc
    clc
    brk
    brk
    ror $180C,x
    bmi $5036
    ror.w LNFLG,x
    asl $1818,x
    clc
    clc
    asl.w LNFLG,x
    .byte $1C
    ror $FEFF,x
    pha
    pha
    php
    asl $7F3F
    ror $74FC,x
    bit ICHIDZ
    .byte $1C
    rol $7F7F,x
    rol $1414,x
    .byte $04
    bvs $4FF2
    inc $3F7F,x
    rol $0404
map_display_list:
    ; Display list for map overview (M key): mode /bin/bashD = 160x2 graphics, LMS=
    bvs $5072
    bvs $5051
    brk
    ldy #$0D
    ora $0D0D
    ora $0D0D
    ora $0D0D
    ora $0D0D
    ora $0D0D
    ora $0D0D
    ora $0D0D
    ora $0D0D
    ora $0D0D
    ora $0D0D
    ora $0D0D
    ora $0D0D
    ora $0D0D
    ora $0D0D
    ora $0D0D
    ora $0D0D
    ora $0D0D
    ora $0D0D
    ora $0D0D
    ora $0D0D
    ora $0D0D
    ora $0D0D
    ora $0D0D
    ora $0D0D
    ora $0D0D
    ora $0D0D
    ora $0D0D
    ora $0D0D
    ora $0D0D
    ora $0D0D
    ora $0D0D
    ora $0D0D
    ora.w SOUNDR
    bvc $50BC
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
    .byte $3C
    ror BITMSK
    ror TOADR,x
    .byte $3C
    brk
    .byte $03
    .byte $27
    ror $3FFC
    rol $8162,x
    brk
    .byte $3C
    ror DOSINI
    clc
    bmi color_pcolr3
    brk
    brk
    ror $180C,x
    .byte $0C
    ror NOCKSM
    brk
    .byte $22
    rol $FF5B,x
    .byte $AB
    dec HOLDCH,x
    brk
    brk
    sec
    .byte $7C
    tsx
    plp
    jmp (LNFLG)
    and $7C2A,y
    clv
    clv
    plp
    plp
    jmp ($9C7E)
    .byte $EB
    cmp $EBFF,x
    cmp COUNTR,x
    brk
    .byte $3C
    ror NOCKSM
    ror TOADR
    .byte $3C
    brk
    brk
    .byte $3C
    ror FTYPE
    asl DOSINI
    sec
    brk
    brk
    brk
    clc
    clc
    brk
    clc
    clc
    brk
    brk
    brk
    clc
    clc
    brk
    clc
    clc
    bmi $50E7
    .byte $0C
    clc
    bmi $50FD
    .byte $0C
    asl LNFLG
    sta $4224,y
    sta $4299,y
    bit $99
    jsr $1004
    sty ICDNOZ
    php
    rti
    .byte $12
    brk
    .byte $3C
    ror DOSINI
    clc
    brk
    clc
    brk
wizard_y_tile:
    ; wizard_y_tile: wizard Y position in tile coordinates (for PMG)
    brk
wizard_x_tile:
    ; wizard_x_tile: wizard X position in tile coordinates (HPOSP3)
    brk
view_offset_y:
    ; view_offset_y: current view Y offset (scroll position)
    brk
view_offset_x:
    ; view_offset_x: current view X offset (scroll position)
    brk
entity_grp1_idx:
    ; entity_grp1_idx: entity group 1 circular buffer index
    .byte $05                     ; $5104
entity_grp2_idx:
    .byte $07                     ; $5105
entity_grp3_idx:
    ; entity_grp3_idx: entity group 3 circular buffer index
    .byte $03
entity_grp4_idx:
    ; entity_grp4_idx: entity group 4 circular buffer index
    .byte $03
    .byte $C0                     ; $5108
map_col:
    .byte $80                     ; $5109
map_row:
    ; map_row: current map row (coarse scroll Y)
    .byte $80
spawn_timer_0:
    ; spawn_timer_0: spawn countdown timer for entity type 0
    .byte $80
    ldy #$E0
    rti
    .byte $C0                     ; $510F
map_x_fine:
    .byte $AB                     ; $5110
map_y_fine:
    ; map_y_fine: fine map Y position for scroll boundary
    .byte $FB
color_pcolr0:
    ; color_pcolr0: saved PCOLR0 color value
    .byte $FF
    .byte $EE                     ; $5113
    .byte $7F                     ; $5114
color_pcolr3:
    .byte $7F                     ; $5115
    ror $D57E,x
    .byte $DF
difficulty:
    ; difficulty: difficulty level (1-9, from title select. Controls entity speed and spawn rates)
    .byte $FD                     ; $511A
power_hi:
    .byte $FF                     ; $511B
power_lo:
    .byte $FE                     ; $511C
score_update_timer:
    ; score_update_timer: score display update countdown
    .byte $FE                     ; $511D
chbase_current:
    .byte $7E                     ; $511E
    .byte $7E                     ; $511F
spawn_interval:
    ; spawn_interval: frames between entity spawn attempts (reset from 1A0)
    brk
entity_count_0:
    ; entity_count_0: alive count for entity type 0
    brk
entity_count_1:
    ; entity_count_1: alive count for entity type 1
    brk
entity_count_2:
    ; entity_count_2: alive count for entity type 2
    .byte $04
entity_count_3:
    ; entity_count_3: alive count for entity type 3
    .byte $3C
entity_count_4:
    ; entity_count_4: alive count for entity type 4
    .byte $7E                     ; $5125
    .byte $6A                     ; $5126
castle_power_0:
    .byte $6E                     ; $5127
castle_power_1:
    ; castle_power_1: castle 1 remaining power (BCD)
    .byte $7C
castle_power_2:
    ; castle_power_2: castle 2 remaining power (BCD)
    .byte $FE                     ; $5129
wall_hits_remaining:
    .byte $AA                     ; $512A
entity_kill_count:
    .byte $AA                     ; $512B
pcolr2_dynamic:
    ; pcolr2_dynamic: dynamic PCOLR2 value (changes with state)
    tax
v_shield_charge:
    ; v_shield_charge: V shield charge-up counter
    tax
    .byte $54
    inc DDEVIC,x
    ora (BUFADR,x)
    .byte $1F
    .byte $1F
    .byte $1B
    .byte $1F
    brk
    brk
    brk
    .byte $54
    .byte $FC
    .byte $FC
    jmp (HOLDCH)
    brk
    brk
    .byte $14
    .byte $3A
    adc $FFFE,x
    ora (CASINI,x)
    .byte $04
    php
    bcc $51AE
    rts
    bcc $5151
    sec
    bpl $518C
    .byte $74
    .byte $7C
    .byte $5C
    sec
    brk
    ror BUFSTR
    sei
    sei
    jmp (TOADR)
    brk
    rts
    rts
    rts
    rts
    rts
    ror.w LNFLG,x
    .byte $63
    .byte $77
    .byte $7F
    .byte $6B
    .byte $63
    .byte $63
    brk
    brk
    ror DELTAR
    ror $6E7E,x
    ror LNFLG
    brk
    .byte $3C
    ror TOADR
    ror TOADR
    .byte $3C
    brk
    bvc $51FA
    .byte $4B
    .byte $73
    sbc $3F7D,y
    .byte $1F
    .byte $02
    sec
    and.w LNFLG,x
    brk
    brk
    brk
    and $3621
    php
    php
    cpx $A6E8
    .byte $0C
    ror
    .byte $02
    asl LNFLG,x
    .byte $0F
    .byte $44
    .byte $03
    brk
    brk
    brk
    brk
    brk
    brk
    .byte $03
    bcc $5139
    bcc $51A5
    brk
    iny
    clc
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
    lda player_state
    cmp #$02
    beq $5208
    rts
    dec $B7
    bpl $5224
    lda #$03
    sta $B7
    dec $B9
    ldy $B9
    cpy #$FF
    bne $521C
    ldy #$03
    sty $B9
    lda $6B4C,y
    sec
    sbc #$01
    sta $B6
    ldy #$00
    lda $B8
    sta ($B4),y
    lda P3PF
    and #$08
    bne $5265
    ldy $B6
    lda entity_move_signs,y
    beq $5247
    lda $B4
    sec
    sbc entity_move_deltas,y
    sta $B4
    bcs $5253
    dec $B5
    jmp $5253
    lda $B4
    clc
    adc entity_move_deltas,y
    sta $B4
    bcc $5253
    inc $B5
    ldy #$00
    lda ($B4),y
    sta $B8
    cmp #$0C
    beq $5265
    cmp #$E0
    bcc $526F
    cmp #$E5
    bcs $526F
    sty player_state
    lda $51AD
    sta v_shield_charge
    rts
    cmp #$14
    bcc $5283
    cmp #$19
    bcs $5283
    lda #$1E
    sta $B8
    lda #$01
    sta $BF0A
    jsr item_collect
    lda #$CD
    sta ($B4),y
    rts
    clc
    lda #$03
    sta $B7
    dec $B9
    ldy $B9
    cpy #$FF
    bne $5299
    ldy #$03
    sty $B9
    lda $6B4C,y
    sec
    sbc #$01
    sta $B6
    ldy #$00
    lda $B8
    sta ($B4),y
    lda P3PF
    and #$08
    bne $52E2
    ldy $B6
    lda entity_move_signs,y
    beq $52C4
    lda $B4
    sec
    sbc entity_move_deltas,y
    sta $B4
    bcs $52D0
    dec $B5
    jmp $5253
    lda $B4
    clc
    adc entity_move_deltas,y
    sta $B4
    bcc $52D0
    inc $B5
    ldy #$00
    lda ($B4),y
    sta $B8
    cmp #$0C
    beq $52E2
    cmp #$E0
    bcc $52EC
    cmp #$E5
    bcs $52EC
    sty player_state
    lda $51AD
    sta v_shield_charge
    rts
    cmp #$14
    bcc $5300
    cmp #$19
    bcs $5300
    lda #$1E
    sta $B8
    lda #$01
    sei
    brk
    .byte $0B
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    ora CMCMD
    .byte $03
    .byte $03
    cpy #$80
    .byte $80
    .byte $80
    ldy #$E0
    rti
    cpy #$AB
    .byte $FB
    .byte $FF
    inc $7F7F
    ror $D57E,x
    .byte $DF
    sbc $FEFF,x
    inc $7E7E,x
    brk
    brk
    brk
    .byte $04
    .byte $3C
    ror $6E6A,x
    .byte $7C
    inc $AAAA,x
    tax
    tax
    .byte $54
    inc DDEVIC,x
    ora (BUFADR,x)
    .byte $1F
    .byte $1F
    .byte $1B
    .byte $1F
    brk
    brk
    brk
    .byte $54
    .byte $FC
    .byte $FC
    jmp (HOLDCH)
    brk
    brk
    .byte $14
    .byte $3A
    adc $FFFE,x
    ora (CASINI,x)
    .byte $04
    php
    bcc $53AE
    rts
    bcc $5351
    sec
    bpl $538C
    .byte $74
    .byte $7C
    .byte $5C
    sec
    brk
    ror BUFSTR
    sei
    sei
    jmp (TOADR)
    brk
    rts
    rts
    rts
    rts
    rts
    ror.w LNFLG,x
    .byte $63
    .byte $77
    .byte $7F
    .byte $6B
    .byte $63
    .byte $63
    brk
    brk
    ror DELTAR
    ror $6E7E,x
    ror LNFLG
    brk
    .byte $3C
    ror TOADR
    ror TOADR
    .byte $3C
    brk
    .byte $FF
    .byte $FF
    .byte $EF
    .byte $E7
    .byte $EF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $BF
    .byte $DF
    .byte $BF
    .byte $BF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $7F
    .byte $BF
    .byte $DF
    .byte $EF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FB
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FB
    .byte $FB
    sec
    .byte $7C
    .byte $7C
    sec
    bpl $53B6
    bpl $53A8
    ora (CASINI,x)
    .byte $04
    php
    bcc $540E
    rts
    bcc $53B1
    sec
    bpl $53EC
    .byte $74
    .byte $7C
    .byte $5C
    sec
    brk
    brk
    brk
    .byte $7F
    rol $081C,x
    rol $6600,x
    ror NOCKSM
    .byte $3C
    ror TOADR
    brk
    brk
    ror TOADR
    .byte $3C
    clc
    clc
    clc
    brk
    brk
    ror $180C,x
    bmi $5436
    ror.w LNFLG,x
    asl $1818,x
    clc
    clc
    asl.w LNFLG,x
    .byte $1C
    ror $FEFF,x
    pha
    pha
    php
    asl $7F3F
    ror $74FC,x
    bit ICHIDZ
    .byte $1C
    rol $7F7F,x
    rol $1414,x
    .byte $04
    bvs $53F2
    inc $3F7F,x
    rol $0404
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
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    ora (CASINI,x)
    .byte $03
    ora CMCMD
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
    ora (CASINI,x)
    .byte $02
    .byte $02
    .byte $02
    .byte $02
    .byte $02
    .byte $02
    .byte $03
    .byte $03
    .byte $03
    .byte $03
    .byte $04
gameplay_frame:
    ; State 2: MAIN GAMEPLAY frame - charset animation, sub-state dispatch, player sprite, scoring, key check
    dec $511F
    bpl $5526
    lda $519F
    sta $511F
    lda COLOR2
    clc
    adc #$10
    sta COLOR2
    ldx chbase_current
    inx
    inx
    cpx #$50
    bne $5520
    ldx $519E
    stx chbase_current
    stx CHBAS
    jsr entity_update_all
    lda $5196
    sta COLOR0
    lda sub_state
    asl
    tax
    lda substate_dispatch_table,x
    sta dispatch_lo
    lda $6B81,x
    sta dispatch_hi
    jsr gameplay_subdispatch
    lda wizard_x_tile
    sta HPOSP3
    jsr player_sprite
    jsr player_state_handler
    jsr score_display
    lda CH
    cmp #$25
    beq $5566
    cmp #$1C
    bne $5562
    lda #$05
    sta game_state
    rts
gameplay_subdispatch:
    ; JMP (C0E) - sub-state indirect dispatch
    jmp (dispatch_lo)
    ldx #$02
    lda map_view_colors,x
    sta COLOR0,x
    dex
    bpl $5568
    lda #$00
    sta HPOSP0
    sta HPOSP2
    sta HPOSP3
    sta SDLSTL
    lda #$50
    sta SDLSTH
    inc game_state
    rts
    lda COLOR2
    clc
    adc #$10
    sta COLOR2
    ldx chbase_current
    inx
    inx
    cpx #$50
    bne $559D
    ldx $519E
    stx chbase_current
    stx CHBAS
    jsr entity_update_all
    lda $5196
    sta COLOR0
    lda sub_state
    asl
    tax
    lda substate_dispatch_table,x
    sta dispatch_lo
    lda $6B81,x
    sta dispatch_hi
    jsr gameplay_subdispatch
    lda wizard_x_tile
    sta HPOSP3
    jsr player_sprite
    jsr player_state_handler
    jsr score_display
    lda CH
    cmp #$25
    beq $55E3
    cmp #$1C
    bne $55DF
    lda #$05
    sta game_state
    rts
    jmp (dispatch_lo)
    ldx #$02
    lda map_view_colors,x
    sta COLOR0,x
    dex
    bpl $55E5
    lda #$00
    sta HPOSP0
    sta HPOSP2
    sta HPOSP3
    sta.w ICBALZ
    .byte $0B
    brk
    brk
    brk
player_state_handler:
    ; Manages spell_flag: 0=check P0PL for Player 2 touch, 1=charging(fire held), 2=JMP shield_build, 3=releasing. NOT keyboard triggered
    lda spell_flag
    beq $5635
    cmp #$02
    bcc $561E
    beq $5632
    lda STRIG0
    beq $5652
    lda #$00
    sta spell_flag
    lda $51AC
    sta pcolr2_dynamic
    jmp $5652
    lda STRIG0
    beq $5652
    inc spell_flag
    lda pcolr2_dynamic
    clc
    adc #$06
    sta pcolr2_dynamic
    jmp $5652
    jmp shield_build
    lda player_state
    cmp #$01
    bcc check_activation_point
    beq $5641
    jmp $5652
    lda STRIG0
    bne $5652
    lda v_shield_charge
    clc
    adc #$06
    sta v_shield_charge
    jmp enter_tracking_mode
    rts
check_activation_point:
    ; Wizard body (P0) collides with Player 2 (S/V activation sprites). Y<4->S (shield build), Y>=4->V (tracking). Triggered by wizard auto-following cursor toward S/V corner
    lda P0PL
    and #$04
    beq $5652
    lda STRIG0
    bne $5652
    lda player_y
    cmp #$64
    bcc $566C
    inc player_state
    jmp $5652
    inc spell_flag
    jmp $5652
enter_tracking_mode:
    ; V-shield: places $CD tracking tile at BB+$81 (map offset). Monsters walking onto $CD are auto-destroyed
    inc player_state
    lda $BB
    clc
    adc #$81
    sta $B4
    lda $BC
    adc #$00
    sta $B5
    lda #$01
    sta $B7
    ldy #$09
    sty $B6
    ldy #$00
    lda ($B4),y
    sta $B8
    lda #$CD
    sta ($B4),y
    lda #$04
    sta $B9
    jmp $5652
    lda STRIG0
    beq $56CF
    inc spell_flag
    lda pcolr2_dynamic
    clc
    adc #$06
    sta pcolr2_dynamic
    jmp $5652
    jmp shield_build
    lda player_state
    cmp #$01
    bcc $56D0
    beq $56BE
    jmp $5652
    lda STRIG0
    bne $56CF
    lda v_shield_charge
    clc
    adc #$06
    sta v_shield_charge
    jmp enter_tracking_mode
    rts
    lda P0PL
    and #$04
    beq $56CF
    lda STRIG0
    bne $56CF
    lda player_y
    cmp #$64
    bcc $56E9
    inc player_state
    jmp $5652
    inc spell_flag
    jmp $5652
    inc player_state
    lda $BB
    clc
    adc #$81
    sta $B4
    lda ENDPT
    brk
    asl.w LNFLG,x
    brk
score_display:
    ; BCD->screen: converts POWER, castle counters, FOUL, SCORE to screen digits in status bar
    dec score_update_timer
    bne $574E
    lda $519D
    sta score_update_timer
    lda difficulty
    bne $5723
    lda #$00
    sta score_delta_hi
    sta score_delta_lo
    lda #$02
    sta score_delta_mid
    jsr power_subtract
    jmp $574E
    sed
    clc
    adc power_lo
    sta power_lo
    lda power_hi
    adc #$00
    sta power_hi
    lda foul_lo
    clc
    adc difficulty
    sta foul_lo
    lda foul_mid
    adc #$00
    sta foul_mid
    lda foul_hi
    adc #$00
    sta foul_hi
    cld
    lda power_hi
    pha
    and #$F0
    lsr
    lsr
    lsr
    lsr
    adc #$50
    sta $6D9F
    pla
    and #$0F
    adc #$50
    sta $6DA0
    lda power_lo
    pha
    and #$F0
    lsr
    lsr
    lsr
    lsr
    adc #$50
    sta $6DA1
    pla
    and #$0F
    adc #$50
    sta $6DA2
    lda castle_power_0
    pha
    and #$F0
    lsr
    lsr
    lsr
    lsr
    adc #$50
    sta $6DB3
    pla
    and #$0F
    adc #$50
    sta $6DB4
    lda castle_power_1
    pha
    and #$F0
    lsr
    lsr
    lsr
    lsr
    adc #$50
    sta $6DB6
    pla
    and #$0F
    adc #$50
    sta $6DB7
    lda castle_power_2
    pha
    and #$F0
    lsr
    lsr
    lsr
    lsr
    adc #$50
    sta $6DB9
    pla
    and #$0F
    adc #$50
    sta $6DBA
    lda castle_score_hi
    pha
    and #$F0
    lsr
    lsr
    lsr
    lsr
    adc #$50
    sta $6D8F
    pla
    and #$0F
    adc #$50
    sta $6D90
    lda castle_score_lo
    pha
    and #$F0
    lsr
    lsr
    lsr
    lsr
    adc #$50
    sta $6D91
    pla
    and #$0F
    adc #$50
    sta $6D92
    jmp status_bar_update
    and #$0F
    adc #$50
    sta $6DA2
    lda.w DSKFMS
    adc LNFLG,x
    brk
    brk
    pha
    txa
    pha
    tya
    pha
    lda $6C0C
    beq $5865
    cmp #$02
    beq $582D
    bcs $5848
    inc $6C0C
    lda #$66
    ldx $6B1E
    ldy $6B1F
    sta WSYNC
    sta CHBASE
    stx COLPF0
    sty COLPF1
    pla
    tay
    pla
    tax
    pla
    rti
    inc $6C0C
    lda $6B20
    ldx $6B21
    ldy $6B24
    sta WSYNC
    sta COLPF0
    stx COLPF1
    sty COLPF2
    jmp $5827
    lda #$00
    sta $6C0C
    lda $6B22
    ldx $6B23
    ldy $6B25
    sta WSYNC
    sta COLPF0
    stx COLPF1
    sty COLPF3
    jmp $5827
    inc $6C0C
    lda v_shield_charge
    sta COLPM2
    jmp $5827
    lsr NSIGN,x
    and (BUFSTR,x)
    lda $BB
    clc
    adc #$81
    sta $B4
    lda $BC
    adc #$00
.proc wand_render
    ; Render wand/cursor using Player 3 PMG area 720 from shape data at (0),Y
    lda wand_active
    sec
    sbc #$01
    sta wand_direction
    asl
    tay
    lda wand_shape_ptrs,y
    sta $80
    lda $6B03,y
    sta $81
    ldy #$0E
    tya
    clc
    adc wizard_y_tile
    tax
    lda ($80),y
    sta $1720,x
    dex
    dey
    bpl $589D
    lda #$00
    ldx wizard_y_tile
    sta $171F,x
    sta $171E,x
    sta $172F,x
    sta $1730,x
    rts
.endp

    lda #$68
    lsr
    lsr
    lsr
    clc
    adc $6D04
    sta $B1
    lda wizard_y_tile
    pha
    and #$0F
    ora vscrol_counter
    sta $AA
    pla
    lsr
    lsr
    lsr
    lsr
    tay
    lda $B1
    ldx $6D05
    cpy #$00
    beq $58E6
    eor #$80
    bmi $58E2
    inx
    dey
    jmp $695B
    sta $B1
    stx $B2
    lda $A9
    cmp #$04
    bcc $58FB
    lda $AA
    cmp #$08
    bcc $5906
    ldy #$81
    jmp $698D
    lda $AA
    cmp #$08
    bcc $5903
    .byte $02
    .byte $1A
    .byte $1A
    .byte $1A
    rol $583C,x
    cli
    clc
    clc
    clc
    .byte $1C
    .byte $1C
    rol.w LNFLG,x
    clc
    clc
    clc
    .byte $3C
    .byte $7C
    .byte $5A
    .byte $5A
    cli
    cli
    clc
    clc
    sec
    .byte $7C
    .byte $77
    adc #$73
    bvs $5923
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
    clc
    clc
    php
    sed
    sei
    sec
    clc
    clc
    clc
    clc
    .byte $1C
    .byte $1C
    asl.w LNFLG,x
    tya
    cld
    sei
    sec
    sec
    sec
    clc
    clc
    clc
    clc
    .byte $1C
    .byte $1C
    asl.w LNFLG,x
    clc
    clc
    clc
    clc
    sec
    sec
    sei
    cli
    tya
    clc
    .byte $1C
    .byte $1C
    asl.w LNFLG,x
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
    clc
    clc
    bpl $598E
    asl $181C,x
    clc
    clc
    clc
    sec
    sec
    sei
    brk
    brk
    ora $1E1B,y
    .byte $1C
    .byte $1C
    .byte $1C
    clc
    clc
    clc
    clc
    sec
    sec
    sei
    brk
    brk
    clc
    clc
    clc
    clc
    .byte $1C
    .byte $1C
    asl $191A,x
    clc
    sec
    sec
    sei
    php
    brk
    brk
    tya
    ora LNFLG,y
    bpl $59A3
    php
    .byte $80
    rti
    .byte $02
    ora (POKMSK,x)
    jsr $0400
    pha
    jsr $1204
    jsr LNFLG
    brk
    bit DSKFMS
    clc
    bit LNFLG
    brk
joy_dx_table:
    ; Joystick direction X delta table
    php
    .byte $02
    brk
    brk
    brk
    brk
    brk
    asl
    asl
    asl
joy_dy_table:
    ; Joystick direction Y delta table
    brk
    ora $0700
    .byte $02
    ora $0700
    .byte $02
    ora.w LNFLG
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
    brk
    brk
    brk
    brk
vbi_deferred:
    ; Deferred VBI: dispatches by game_state via table B30, runs sound engine, entity spawner
    lda game_running
    beq $5A33
    cld
    lda game_state
    asl
    tax
    lda state_dispatch_table,x
    sta dispatch_lo
    lda $6B31,x
    sta dispatch_hi
    jsr vbi_dispatch
    sta HITCLR
    jsr sound_engine
    lda game_state
    cmp #$02
    bcc $5A33
    cmp #$05
    beq $5A33
    dec spawn_interval
    bne $5A33
    jsr entity_spawner
    lda #$00
    sta ATRACT
    lda #$FF
    sta CH
    lda pcolr2_dynamic
    sta PCOLR2
    jmp XITVBV
vbi_dispatch:
    ; JMP (C0E) - indirect dispatch to state handler
    jmp (dispatch_lo)
vbi_state_attract:
    ; State 3: title attract mode - only calls entity_update_all
    jsr entity_update_all
    rts
vbi_state_title_input:
    ; State 4: title input - updates entities, checks keyboard (any key->state 1, ESC excluded)
    jsr entity_update_all
    inc PCOLR3
    lda CH
    cmp #$25
    beq $5A6A
    cmp #$FF
    beq $5A6A
    lda #$01
    sta game_state
    sta HPOSM3
    lda #$30
    sta HPOSP2
    rts
    sta COLPM2
    jmp $5827
vbi_state_scroll:
    ; VBI handler for scroll/transition state
    lsr NSIGN,x
    and (BUFSTR,x)
    lda $BB
    clc
    adc #$81
    sta $B4
    lda $BC
    adc #$00
gameplay_walk_mode:
    ; Sub-state 0 WALKING: joystick scrolls map via scroll_engine + renders wizard indicator. Map SCROLLS. Wizard walks with map. All combat/collection happens here
    jsr collision_handler
    lda wand_active
    beq $5A8E
    jsr scroll_engine
    jsr wand_render
    lda STRIG0
    bne $5AA9
    lda move_cooldown
    bne $5AA9
    lda spell_flag
    bne $5AA9
    lda player_state
    beq $5AA6
    cmp #$03
    bne $5AA9
    inc sub_state
    rts
    brk
    brk
gameplay_wait_release:
    ; Sub-state 1: wait fire release after pickup, restores colors, INC state_advance
    lda STRIG0
    beq $5AA9
    lda $6B16
    sta PCOLR3
    sta color_pcolr3
    lda #$0E
    sta PCOLR0
    inc sub_state
    rts
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
player_sprite:
    ; Player sprite render: clear old, check sub-state, move or auto-follow, animate 4 frames, write to PMG 420
    jsr player_clear
    lda sub_state
    cmp #$02
    bne $5B35
    jsr player_tile_check
    lda wand_active
    bne $5B18
    jsr player_auto_move
    jmp $5B38
    ; Wand mode: wizard body slides in cursor direction (adds wand_dx/dy to player_x/y) but map stays fixed. NOT for navigation.
    ldx wizard_move_dir
    lda player_x
    clc
    adc wand_dx,x
    sta player_x
    lda player_y
    clc
    adc wand_dy,x
    sta player_y
    jsr player_clamp_x
    jmp $5B38
    jsr player_auto_move
    lda player_x
    sta HPOSP0
    lda $14
    and #$01
    bne $5B4E
    dec anim_frame
    bpl $5B4E
    lda #$03
    sta anim_frame
    lda anim_frame
    asl
    tay
    lda anim_ptrs,y
    sta $8A
    lda $6B29,y
    sta $8B
    jsr player_calc_offset
    ldy #$07
    lda ($8A),y
    sta $1420,x
    dex
    dey
    bpl $5B62
    rts
.proc player_clear
    ; Clear 8 bytes of player sprite at PMG offset
    jsr player_calc_offset
    ldy #$07
    lda #$00
    sta $1420,x
    dex
    dey
    bpl $5B73
    rts
.endp

.proc player_calc_offset
    ; Calculate PMG write offset from player Y + 7
    lda player_y
    clc
    adc #$07
    tax
    rts
.endp

.proc player_clamp_x
    ; Clamp player X to range 0-
    lda #$30
    cmp player_x
    bcc $5B8D
    sta player_x
    lda #$C8
    cmp player_x
    bcs $5B97
    sta player_x
    lda #$04
    cmp player_y
    bcc $5BA1
    sta player_y
    lda #$94
    cmp player_y
    bcs $5BAB
    sta player_y
    rts
.endp

    jsr player_clamp_x
    jmp $5B38
    jsr player_auto_move
    lda player_x
    sta HPOSP0
    lda $14
    and #$01
    bne $5BCB
    dec anim_frame
    bpl $5BCB
    lda #$03
    sta anim_frame
    lda anim_frame
    asl
    tay
    lda anim_ptrs,y
    sta $8A
    lda $6B29,y
    sta $8B
    jsr player_calc_offset
    ldy #$07
    lda ($8A),y
    sta $1420,x
    dex
    dey
    bpl $5BDF
    rts
    jsr player_calc_offset
    ldy #$07
    lda #$00
    sta $1420,x
    dex
    dey
    bpl $5BF0
    rts
    lda $AC15
    brk
    .byte $2F
    lda $A1
    clc
.proc player_tile_check
    ; Player 0 (wizard body) P0PF check. Runs ONLY in wand mode (sub_state=2). Converts wizard PMG coords to tile address, checks tile interactions
    lda P0PF
    and #$01
    bne $5C08
    rts
    lda player_x
    sec
    sbc #$20
    pha
    and #$07
    sta $A9
    pla
    lsr
    lsr
    lsr
    clc
    adc $6D04
    sta $AC
    lda player_y
    pha
    and #$0F
    sta $AA
    pla
    lsr
    lsr
    lsr
    lsr
    tay
    lsr
    pha
    tya
    clc
    and #$01
    ror
    ror
    adc $AC
    sta $AC
    pla
    adc $6D05
    sta $AD
    ldy #$00
    jsr check_tile_at
    lda $A9
    beq $5C4A
    iny
    jsr check_tile_at
    lda $AA
    cmp #$0C
    bne $5C5D
    ldy #$80
    jsr check_tile_at
    lda $A9
    beq $5C5D
    iny
    jsr check_tile_at
    rts
.endp

check_tile_at:
    ; Read tile at (),Y: handle empty, wall (/bin/bashB), item (4-8), entity death (E)
    lda ($AC),y
    beq $5C94
    cmp #$0B
    bcc $5C95
    beq $5C7E
    cmp #$14
    bcc $5C94
    cmp #$19
    bcs $5C94
    sta $A2
    lda #$1E
    sta ($AC),y
    lda #$01
    sta $BF0B
    jmp item_collect
    lda wall_hits_remaining
    beq $5C94
    jsr $5CCD
    dec wall_hits_remaining
    bne $5C94
    lda #$86
    sta ($AC),y
    lda #$01
    sta $BF11
    rts
    jsr $5CCD
    dec entity_kill_count
    bne $5C94
    ; Kill counter exhausted: $512B wrapped to 0. Damages own castle tiles at $6BAB address. Penalty for excessive killing
    ldy #$01
    sta score_delta_hi
    dey
    sta score_delta_mid
    jsr score_add_foul
    lda castle_screen_addrs
    sta $AC
    lda $6BAC
    sta $AD
    lda #$32
    ldy #$00
    jsr $5CC2
    ldy #$80
    jsr $5CC2
    jmp $5C8F
    sta ($AC),y
    iny
    clc
    adc #$01
    sta ($AC),y
    adc #$01
    rts
    ; Entity killed by walking: COLOR0=$0E, sound $BF0C, FOUL += 00:00:50, POWER -= 00:10 (BCD). DEC $512B kill counter
    lda #$0E
    sta COLOR0
    lda #$01
    sta $BF0C
    lda #$00
    sta score_delta_hi
    sta score_delta_mid
    lda #$50
    sta score_delta_lo
    jsr score_add_foul
    lda #$00
    sta score_delta_lo
    lda #$10
    sta score_delta_mid
    jmp power_subtract
    ora ($8D,x)
    .byte $0B
    .byte $BF
    jmp $5400
    brk
    .byte $77
    brk
    brk
    brk
sound_engine:
    ; Sound effect engine: 20 trigger slots ($BF00-$13) -> 4 POKEY channels. Slots grouped 5/channel. $01=trigger init, $02+=update envelope
    ldx #$13
    lda #$0F
    sta $C2
    lda $BF00,x
    cmp #$01
    bcc $5D3B
    beq $5D12
    jmp $5D4D
    txa
    pha
    ldy #$04
    ldx $C2
    lda #$00
    sta $BF00,x
    inx
    dey
    bpl $5D1A
    pla
    tax
    asl
    tay
    lda #$02
    sta $BF00,x
    lda sound_init_table,y
    sta $6C98
    lda $5D66,y
    sta $6C99
    jsr sound_dispatch
    ldx $C2
    dex
    bmi $5D4C
    cpx $C2
    bcs $5D06
    lda $C2
    sec
    sbc #$05
    sta $C2
    jmp $5D06
    rts
    txa
    asl
    tay
    lda sound_update_table,y
    sta $6C98
    lda $5D8E,y
    sta $6C99
    jsr sound_dispatch
    jmp $5D3B
sound_dispatch:
    ; JMP ($6C98) - indirect call to sound init or update routine
    jmp ($6C98)
sound_init_table:
    ; Sound init routine pointers: 20 entries x 2 bytes (slot 0-19)
    lda OLDCHR,x
    dec OLDCHR
    dex
    eor $5DCE,x
    .byte $D2
    eor sfx_castle_supply_1,x
    inc OLDCHR
    inc OLDCHR,x
    .byte $FA
    eor $5E14,x
    clc
    lsr sfx_gem_pickup,x
    .byte $3B
    lsr sfx_shield_build,x
    .byte $64
    lsr sfx_treasure,x
    adc $965E,x
    lsr $5E9A,x
    .byte $9E
    .byte $5E                     ; $5D8C
sound_update_table:
    .byte $A2                     ; $5D8D
    .byte $5E                     ; $5D8E
    .byte $C2
    lsr $5EC2,x
    .byte $C2
    lsr $5EC2,x
    .byte $C3
    lsr $5EE0,x
    .byte $07
    .byte $5F
    php
    .byte $5F
    .byte $3C
    .byte $5F
    and $5F5F,x
    .byte $5F
    .byte $93
    .byte $5F
    lda ($5F,x)
    .byte $BB
    .byte $5F
    ldy $EF5F,x
    .byte $5F
    ora $1960,y
    rts
    .byte $19                     ; $5DB3
    .byte $60                     ; $5DB4
sfx_monster_approach:
    .byte $EE                     ; $5DB5
    brk
    .byte $BF
    lda #$8F
    sta $BF28
    sta AUDC1
    lda #$64
    sta AUDF1
    rts
    inc $BF01
    rts
    inc $BF02
    rts
    inc $BF03
    rts
    inc $BF04
    rts
sfx_castle_supply_1:
    ; SFX slot 5: castle supply $85 collected (ring pickup). Ch2: AUDC2=$AF, AUDF2=$F0 sweep down
    inc $BF05
    lda #$F0
    sta $BF41
    lda #$AF
    sta $BF2D
    jmp $5EC3
sfx_castle_supply_2:
    ; SFX slot 6: castle supply $86 collected (ring pickup). Ch2: AUDC2=$CF, AUDF2=$28 random modulation
    inc $BF06
    lda #$28
    sta $BF42
    lda #$CF
    sta $BF2E
    jmp $5EE0
    inc $BF07
    rts
sfx_item_collect:
    ; SFX slot 8: entity killed by cursor or item collected. Ch2: AUDC2=$EF, sweep up from $32 to $46
    inc $BF08
    lda #$EF
    sta AUDC2
    lda #$07
    sta $BF1C
    lda #$32
    sta $BF44
    lda #$46
    sta $BF58
    jmp $5F08
    inc $BF09
    rts
sfx_tracking_hit:
    ; SFX slot 10: tracking tile ($CD) triggered by monster. Ch3: AUDC3=$EF, random freq + decay
    inc $BF0A
    lda #$EF
    sta $BF32
    lda #$0F
    sta $BF1E
    jmp $5F3D
sfx_gem_pickup:
    ; SFX slot 11: power gem pickup from map. Ch3: AUDC3=$EF, warble effect with period=3
    inc $BF0B
    lda #$EF
    sta $BF33
    sta $BF1F
    lda #$03
    sta $BF5B
    jmp $5F5F
sfx_cursor_kill:
    ; SFX slot 12: entity killed by cursor (flash effect). Ch3: AUDC3=$4F, AUDF3=$50, timed decay
    inc $BF0C
    lda #$4F
    sta AUDC3
    lda #$50
    sta AUDF3
    lda #$05
    sta $BF5C
    rts
sfx_shield_build:
    ; SFX slot 13: shield barrier built (S action). Ch3: AUDC3 sweep up +2/frame, AUDF3=$0A
    inc $BF0D
    lda #$05
    sta $BF21
    lda #$0A
    sta AUDF3
    lda #$02
    sta $BF35
    sta AUDC3
    rts
    inc $BF0E
    rts
sfx_treasure:
    ; SFX slot 15: treasure tile collected ($8E-$91). Ch4: AUDC4=$AF, random freq, warble period=3
    inc $BF0F
    lda #$AF
    sta $BF37
    lda #$0F
    sta $BF23
    lda #$03
    sta $BF5F
    jmp $5FBC
sfx_alarm:
    ; SFX slot 16: alarm/castle danger. Ch4: AUDC4=$4F, AUDF4=$00, freq sweep up 10 steps
    inc $BF10
    lda #$4F
    sta $BF38
    sta AUDC4
    lda #$00
    sta $BF4C
    sta AUDF4
    lda #$0A
    sta $BF24
    rts
    inc $BF11
    rts
    inc $BF12
    rts
    inc $BF13
    rts
    lda $BF28
    sta AUDC1
    lda RANDOM
    and #$03
    bne $5EC1
    dec $BF28
    lda $BF28
    cmp #$7F
    bne $5EC1
    lda #$00
    sta AUDC1
    sta $BF00
    rts
    rts
    lda $BF2D
    sta AUDC2
    lda $BF41
    sta AUDF2
    sec
    sbc #$14
    sta $BF41
    bne $5EDF
    lda #$00
    sta AUDC2
    sta $BF05
    rts
    lda RANDOM
    and #$03
    sta $C3
    lda $BF2E
    sta AUDC2
    lda $BF42
    cmp #$06
    bcc $5EFE
    sta AUDF2
    sec
    sbc $C3
    sta $BF42
    rts
    lda #$00
    sta AUDC2
    sta $BF06
    rts
    rts
    lda $BF44
    clc
    adc #$04
    sta $BF44
    sta AUDF2
    cmp $BF58
    bne $5F3B
    dec $BF1C
    beq $5F33
    lda $BF58
    clc
    adc #$0A
    sta $BF58
    lda $BF44
    sec
    sbc #$0A
    sta $BF44
    jmp $5F3B
    lda #$00
    sta AUDC2
    sta $BF08
    rts
    rts
    lda $BF32
    sta AUDC3
    lda RANDOM
    and #$0F
    adc #$07
    sta AUDF3
    dec $BF32
    dec $BF1E
    lda $BF1E
    bne $5F5E
    sta AUDC3
    sta $BF0A
    rts
    lda RANDOM
    and #$3F
    adc #$3C
    sta AUDF3
    lda $BF33
    sta AUDC3
    dec $BF5B
    beq $5F7A
    lda #$00
    sta $BF33
    rts
    lda #$03
    sta $BF5B
    dec $BF1F
    lda $BF1F
    sta $BF33
    and #$0F
    bne $5F92
    sta AUDC3
    sta $BF0B
    rts
    dec $BF5C
    bne $5FA0
    lda #$00
    sta AUDC3
    sta $BF0C
    rts
    lda $BF35
    clc
    adc #$02
    sta $BF35
    sta AUDC3
    dec $BF21
    bne $5FBA
    lda #$00
    sta AUDC3
    sta $BF0D
    rts
    rts
    lda RANDOM
    and #$1F
    adc #$14
    sta AUDF4
    lda $BF37
    sta AUDC4
    dec $BF5F
    beq $5FD7
    lda #$00
    sta $BF37
    rts
    lda #$03
    sta $BF5F
    lda #$AF
    sta $BF37
    dec $BF23
    bne $5FEE
    lda #$00
    sta AUDC4
    sta $BF0F
    rts
    inc $BF4C
    lda $BF4C
    sta AUDF4
    dec $BF24
    lda $BF24
    bne $6018
    dec $BF38
    lda $BF38
    cmp #$3F
    bne $6010
    lda #$00
    sta $BF10
    rts
    sta AUDC4
    lda #$0A
    sta $BF24
    rts
    rts
    sta $BF0C
    rts
    lda $BF35
    clc
    adc #$02
    sta $BF35
    sta AUDC3
    dec $BF21
    bne $6037
    lda #$00
    sta AUDC3
    sta $BF0D
    rts
    rts
    lda RANDOM
    and #$1F
    adc #$14
    sta AUDF4
    lda $BF37
    sta AUDC4
    dec $BF5F
    beq $6054
    lda #$00
    sta $BF37
    rts
    lda #$03
    sta $BF5F
    lda #$AF
    sta $BF37
    dec $BF23
    bne $606B
    lda #$00
    sta AUDC4
    sta $BF0F
    .byte $5C
    brk
    bit.w LNFLG
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
    .byte $D2
    and #$7E
    cmp #$03
    bcc $6067
    sta $96
    ldy #$00
    lda ($96),y
    sta $98
    and #$3F
    cmp #$05
    bcs $6067
    lda $99
    sta ($96),y
    ldx spawn_type_idx
    inc entity_count_0,x
    txa
    asl
    tay
    lda $60B8,y
    sta dispatch_lo
    lda $60B9,y
    sta dispatch_hi
    ldy $BE
    lda RANDOM
    and #$1F
    jmp (dispatch_lo)
    .byte $C2
    rts
    .byte $D7
    rts
    cpx $0160
    adc ($16,x)
    adc ($99,x)
    brk
    .byte $B3
    lda $96
    sta entity_grp1_xpos,y
    lda $97
    sta entity_grp1_ypos,y
    lda $98
    sta entity_grp1_dir,y
    jmp $6128
    sta $B580,y
    lda $96
    sta $B400,y
    lda $97
    sta $B480,y
    lda $98
    sta $B500,y
    jmp $6128
    sta $B6C0,y
    lda $96
    sta $B600,y
    lda $97
    sta $B640,y
    lda $98
    sta $B680,y
    jmp $6128
    sta $B760,y
    lda $96
    sta $B700,y
    lda $97
    sta $B720,y
    lda $98
    sta $B740,y
    jmp $6128
    sta $B789,y
    lda $96
    sta $B780,y
    lda $97
    sta $B783,y
    lda $98
    sta $B786,y
    lda $51A0
    sta spawn_interval
    rts
    .byte $D2
    and #$1F
    jmp (dispatch_lo)
    .byte $C2
    rts
    .byte $D7
    rts
    cpx $0160
    adc ($16,x)
    adc ($99,x)
    brk
    .byte $B3
    lda $96
    sta entity_grp1_xpos,y
    lda $97
    sta entity_grp1_ypos,y
    lda $98
    sta entity_grp1_dir,y
    jmp $6128
    sta $B580,y
    lda $96
    sta $B400,y
    lda $97
    sta $B480,y
    lda $98
    sta $B500,y
    jmp $6128
    sta $B6C0,y
    lda $96
    sta $B600,y
    lda $97
    sta $B640,y
    lda SAVMSC
    brk
    and LNFLG,x
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
    brk
    brk
    brk
    brk
    brk
shield_build:
    ; S-shield: builds barrier at wizard position. Strength=min(power_digit,3). Tile $E0-$E2. Cost from $6BDE table
    lda STRIG0
    bne $6309
    inc spell_flag
    rts
    lda move_cooldown
    bne $634B
    lda $6D9F
    sec
    sbc #$50
    beq $634B
    cmp #$04
    bcc $631C
    lda #$03
    sta $BE
    ldy #$00
    lda ($BB),y
    beq $632A
    and #$3F
    cmp #$05
    bcs $634B
    lda #$DF
    clc
    adc $BE
    sta ($BB),y
    lda #$01
    sta $BF0D
    ldy $BE
    sed
    lda power_lo
    sec
    sbc shield_cost,y
    sta power_lo
    lda power_hi
    sbc #$00
    sta power_hi
    cld
    rts
    sta $B4
    bcc $6353
    inc $B5
    ldy #$00
    lda ($B4),y
    sta $B8
    cmp #$0C
    beq $6365
    cmp #$E0
    bcc $6369
    cmp #$E5
    bcs $6369
    sty player_state
    rts
    cmp #$14
    bcc $637D
    cmp #$19
    bcs $637D
    lda #$1E
    sta $B8
    lda #$01
    sta $BF0A
    jsr item_collect
    lda #$CD
    sta (LNFLG),y
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
    brk
    brk
    brk
    brk
    brk
    brk
    brk
status_bar_update:
    ; Updates status bar text lines at D80+
    lda foul_hi
    pha
    and #$F0
    lsr
    lsr
    lsr
    lsr
    adc #$D0
    sta $6DC7
    pla
    and #$0F
    adc #$D0
    sta $6DC8
    lda foul_mid
    pha
    and #$F0
    lsr
    lsr
    lsr
    lsr
    adc #$D0
    sta $6DC9
    pla
    and #$0F
    adc #$D0
    sta $6DCA
    lda foul_lo
    pha
    and #$F0
    lsr
    lsr
    lsr
    lsr
    adc #$D0
    sta $6DCB
    pla
    and #$0F
    adc #$D0
    sta $6DCC
    lda score_hi
    and #$0F
    adc #$50
    sta $6D8B
    lda score_lo
    pha
    and #$F0
    lsr
    lsr
    lsr
    lsr
    adc #$50
    sta $6D8C
    pla
    and #$0F
    adc #$50
    sta $6D8D
    rts
    lda $B580,x
    sta $A1
    jsr entity_move
    lda $A4
    sta $B480,x
    lda $A3
    sta $B400,x
    lda $A0
    sta $B500,x
    lda $A1
game_over_state:
    ; State 9: game over animation - DEC PCOLR3, compare/update high score at 835/849
    dec PCOLR3
    dec game_over_timer
    beq $6489
    rts
    ldx #$05
    lda $6DC7,x
    sta last_score_data,x
    dex
    bpl $648B
    lda #$00
    sta game_state
    sta HPOSP0
    sta HPOSP2
    sta HPOSP3
    ldx #$00
    lda last_score_data,x
    cmp high_score_data,x
    bcc $6488
    beq $64BA
    ldx #$05
    lda last_score_data,x
    sta high_score_data,x
    dex
    bpl $64B0
    rts
    inx
    cpx #$06
    bne $64A4
    rts
hardware_init:
    ; Initialize hardware: set VVBLKD, VDSLST, PMBASE=0, GRACTL=/bin/bash3, PRIOR=/bin/bash1, colors
    .byte $5A
    lda #$07
    jsr SETVBV
    lda #$01
    sta game_running
    lda #$80
    sta VDSLST
    lda #$68
    sta $0201
    lda #$C0
    sta NMIEN
    lda #$03
    sta GRACTL
    lda #$3E
    sta SDMCTL
    lda #$10
    sta PMBASE
    lda #$01
    sta GPRIOR
    ldx #$03
    lda gameplay_colors,x
    sta COLOR0,x
    dex
    bpl $64F0
    lda #$2D
    sta $84
    lda #$8A
    .byte $85                     ; $64FF
level_renderer:
    .byte $A9                     ; $6500
    brk
    sta $8C
    lda #$6E
    sta $8D
    ldy #$7E
    lda ($8C),y
    cmp #$E3
    beq $6536
    cmp #$E4
    bne $6522
    ldx #$00
    lda RANDOM
    and #$07
    bne $651F
    ldx #$42
    txa
    sta ($8C),y
    dey
    bpl $650A
    lda $8C
    eor #$80
    sta $8C
    bmi $6508
    inc $8D
    lda $8D
    cmp #$9F
    bne $6508
    rts
    lda #$E4
    sta ($8C),y
    tya
    clc
    adc $8C
    sta $8C
    bcc $6544
    inc $8D
    sec
    sbc #$81
    sta $8C
    bcs $654D
    dec $8D
    ldx #$02
    ldy #$02
    lda ($8C),y
    cmp #$E0
    bcc $6565
    cmp #$E3
    bcs $6565
    lda RANDOM
    and #$01
    clc
    adc #$E3
    sta ($8C),y
    dey
    bpl $6551
    lda $8C
    clc
    adc #$80
    sta $8C
    bcc $6573
    inc $8D
    dex
    bpl $654F
    lda #$01
    sta $BF10
    rts
    sbc castle_damage_costs,y
    sta $FFFF,x
    .byte $EF
    .byte $E7
    .byte $EF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $BF
    .byte $DF
    .byte $BF
    .byte $BF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $7F
    .byte $BF
    .byte $DF
    .byte $EF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FB
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FB
    .byte $FB
    sec
    .byte $7C
    .byte $7C
    sec
    bpl $65B6
    bpl $65A8
    ora (CASINI,x)
    .byte $04
    php
    bcc $660E
    rts
    bcc $65B1
    sec
    bpl $65EC
    .byte $74
    .byte $7C
    .byte $5C
    sec
    brk
    brk
    brk
    .byte $7F
    rol $081C,x
    rol $6600,x
    ror NOCKSM
    .byte $3C
    ror TOADR
    brk
    brk
    ror TOADR
    .byte $3C
    clc
    clc
    clc
    brk
    brk
    ror $180C,x
    bmi $6636
    ror.w LNFLG,x
    asl $1818,x
    clc
    clc
    asl.w LNFLG,x
    .byte $1C
    ror $FEFF,x
    pha
    pha
    php
    asl $7F3F
    ror $74FC,x
    bit ICHIDZ
    .byte $1C
    rol $7F7F,x
    rol $1414,x
    .byte $04
    bvs $65F2
    inc $3F7F,x
    rol $0404
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    sec
    sec
    sec
    sec
    sec
    brk
    sec
    brk
    ror TOADR
    ror LNFLG
    brk
    brk
    brk
    brk
    brk
    ror $FF
    ror TOADR
    .byte $FF
    ror LNFLG
    clc
    rol $3C60,x
    asl HOLDCH
    clc
    brk
    brk
    ror BUFSTR
    clc
    bmi $6694
    lsr LNFLG
    .byte $1C
    rol ABUFPT,x
    sec
    .byte $6F
    ror CHKSNT
    brk
    brk
    clc
    clc
    clc
    brk
    brk
    brk
    brk
    asl APPMHI
    .byte $1C
    clc
    clc
    .byte $1C
    asl $6006
    bvs $6683
    clc
    clc
    sec
    bvs $66B0
    brk
    ror NOCKSM
    .byte $FF
    .byte $3C
    ror LNFLG
    brk
    brk
    clc
    clc
    ror $1818,x
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    clc
    clc
    bmi $6669
    brk
    brk
    ror.w LNFLG,x
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    clc
    clc
    brk
    brk
    asl DOSINI
    clc
    bmi $66DE
    rti
    brk
    .byte $7C
    dec $C6C6
    dec FR2
    .byte $7C
    brk
    sec
    sec
    clc
    clc
    clc
    clc
    clc
    brk
    .byte $7C
    inc DOSINI
    clc
    bmi $66F6
    inc $7E00,x
    .byte $0C
    clc
    .byte $0C
    asl TOADR
    .byte $3C
    brk
    .byte $0C
    .byte $1C
    .byte $3C
    jmp ($FECC)
    .byte $0C
    brk
    ror $7C60,x
    asl TRAMSZ
    ror NOCKSM
    brk
    .byte $7C
    dec $C0
    .byte $FC
    dec $7CE6
    brk
    ror $0C06,x
    clc
    bmi $66EE
    bmi $66C0
    .byte $7C
    dec $7CE6
    dec $7CE6
    brk
    .byte $7C
    dec $E6C6
    ror $180C,x
    bmi $66D1
    sec
    sec
    brk
    brk
    sec
    sec
    brk
    brk
    brk
    clc
    clc
    brk
    clc
    clc
    bmi $66E7
    .byte $0C
    clc
    bmi $66FD
    .byte $0C
    asl LNFLG
    brk
    brk
    ror.w LNFLG,x
    ror.w LNFLG,x
    rts
    bmi $670B
    .byte $0C
    clc
    bmi $6757
    brk
    .byte $3C
    ror TOADR
    .byte $0C
    clc
    brk
    clc
    brk
    brk
    .byte $3C
    ror BITMSK
    ror $3E60
    brk
    sei
    .byte $9C
    .byte $3C
    rol FTYPE,x
    ror TOADR
    .byte $C3
    inc $6373
    .byte $63
    ror $6363
    dec $663C,x
    cpy $C0C0
    cpy #$E6
    .byte $7C
    inc $6373
    .byte $63
    .byte $63
    .byte $63
    .byte $63
    dec $66FE,x
    rts
    sei
    rts
    .byte $63
    ror HOLDCH
    inc $6066,x
    sei
    rts
    rts
    rts
    rts
    .byte $3C
    ror $C6
    cpy #$DE
    dec TOADR
    .byte $3C
    dec $C6
    dec $CE
    inc $C6E6,x
    dec STATUS
    clc
    clc
    clc
    clc
    clc
    clc
    .byte $0C
    asl $0C0C,x
    .byte $0C
    .byte $0C
    .byte $1C
    sec
    rts
    dec BUFSTR
    jmp ($7878)
    jmp ($C66C)
    cpx #$60
    rts
    rts
    rts
    ror COUNTR
    sei
    dec NSIGN
    inc $C6D6,x
    dec $C6
    dec $C6
    dec FR2
    inc $DE,x
    dec $C6C6
    .byte $7C
    dec $C6C6
    dec $C6
    inc HOLDCH
    .byte $FC
    ror TOADR
    ror BUFSTR
    rts
    rts
    cpy #$7C
    inc $C6
    dec $C6
    dec $CE
    .byte $F3
    inc $6673
    jmp ($6666)
    .byte $63
    .byte $C3
    .byte $3A
    ror FKDEF
    .byte $3C
    asl TRAMSZ
    ror $5C
    inc $6030,x
    cpy #$C0
    .byte $C2
    dec HOLDCH
    inc TOADR
    ror TOADR
    ror TOADR
    ror $C33F
    ror TOADR
    ror TOADR
    ror NOCKSM
    clc
    .byte $C3
    .byte $C3
    .byte $C3
    .byte $D3
    .byte $CB
    .byte $DF
    .byte $77
    .byte $62
    .byte $C3
    .byte $C3
    ror NOCKSM
    .byte $3C
    ror $C3
    .byte $C3
    .byte $C3
    ror TOADR
    .byte $3C
    clc
    clc
    clc
    clc
    ror $0CC6,x
    clc
    inc $C360,x
    inc $1E00,x
    clc
    clc
    clc
    clc
    asl.w LNFLG,x
    rti
    rts
    bmi $67FD
    .byte $0C
    asl LNFLG
    brk
    sei
    clc
    clc
    clc
    clc
    sei
    brk
    brk
    php
    .byte $1C
    rol LOGCOL,x
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    .byte $FF
    brk
    bvs $6872
    bvs $684B
    .byte $14
    pla
    bvs $6878
    bvs $6810
    bvs $6792
    bvs $687E
    bvs $6816
    bvs $6818
    eor (LNFLG,x)
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    .byte $37
    and #$3A
    and (BUFRLO,x)
    bit LNFLG
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    pla
    adc #$67
    pla
    brk
    .byte $73
    .byte $63
    .byte $6F
    .byte $72
    adc LNFLG
high_score_data:
    ; High score: 6 BCD bytes
    bne $6807
    bne $6809
    bne $680B
    brk
    brk
    brk
    ldy $B3A1
    ldy LNFLG,x
    .byte $B3
    .byte $A3
    .byte $AF
    .byte $B2
    lda LNFLG
last_score_data:
    ; Last score: 6 BCD bytes (compared against high score on game over)
    bne $681B
    bne $681D
    bne $681F
    brk
    brk
    brk
    brk
    .byte $33
    and ENTVEC
    and ICSTAZ
    .byte $34
    brk
    bit $3625
    and ENTVEC
    brk
    eor (LNFLG),y
    brk
    brk
    brk
    brk
    brk
    lda ($AE,x)
    ldy LNFLG
    bcs $681F
    lda $B3
    .byte $B3
    brk
    inc $E9
    .byte $F2
    sbc LNFLG
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
dli_handler:
    ; DLI: WSYNC, copies 4 colors from B7C to COLPF0-3 (status bar color split)
    pha
    txa
    pha
    ldx #$03
    sta WSYNC
    lda $6B7C,x
    sta COLPF0,x
    dex
    bpl $6888
    pla
    tax
    pla
    rti
setup_gameplay_screen:
    ; State 1: switches display list to D00, loads colors from 112, sets CHBASE
    lda screen_initialized
    bne $68BE
    inc screen_initialized
    lda #$00
    sta SDLSTL
    lda #$6D
    sta SDLSTH
    lda chbase_current
    sta CHBAS
    lda #$30
    sta HPOSP2
    ldx #$07
    lda color_pcolr0,x
    sta PCOLR0,x
    dex
    bpl $68B4
    rts
    lda #$00
    sta VDSLST
    sta screen_initialized
    lda #$58
    sta $0201
    inc game_state
    rts
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
    brk
.proc collision_handler
    ; Walking mode collision: P3PF (wizard indicator on playfield). bit 0=entity marker (4-17), bit 2=castle supply (5-86)/treasure. Also reads joystick for scroll direction
    lda #$00
    sta score_delta_hi
    sta score_delta_mid
    sta score_delta_lo
    lda wizard_x_tile
    and #$07
    ora hscrol_counter
    sta $A9
    lda wizard_y_tile
    and #$0F
    ora vscrol_counter
    sta $AA
    lda P3PF
    and #$01
    beq $6929
    jsr cursor_hit_handler
    lda P3PF
    and #$04
    beq $6933
    jsr wizard_special_collision
    lda move_cooldown
    bne $694A
    lda STICK0
    eor #$0F
    sta scroll_direction
    sta wand_active
    beq $6953
    lda #$08
    sta move_cooldown
    dec move_cooldown
    lda wand_active
    sta scroll_direction
    rts
.endp

wizard_special_collision:
    ; Wizard walks onto special tile. $85/$86=mana gems (animated, inverse chars $05/$06). Sound $BF05/06. $85: +50 POWER, +01 FOUL. $86: +10 POWER, +20 FOUL
    ldy #$00
    lda ($BB),y
    cmp #$85
    bcc $6953
    cmp #$87
    bcs $697F
    sec
    sbc #$85
    tax
    lda #$01
    sta $BF05,x
    lda #$00
    sta ($BB),y
    lda castle_power_gain,x
    sta score_delta_mid
    jsr power_add
    lda $6BFB,x
    sta score_delta_mid
    jmp score_add_foul
    cmp #$8E
    bcc $6953
    cmp #$92
    bcs $6953
    sec
    sbc #$8E
    tax
    eor #$03
    cmp castle_score_hi
    bcs $6953
    lda #$00
    sta ($BB),y
    lda #$01
    sta $BF0F
    lda castle_score_rewards,x
    sta score_delta_hi
    jsr power_add
    lda #$00
    sta score_delta_hi
    lda treasure_foul_costs,x
    sta score_delta_mid
    jmp score_add_foul
    jmp ($12D0)
    lda STICK0
    eor #$0F
    sta scroll_direction
    sta wand_active
    beq $69D0
    lda #$08
    sta move_cooldown
    dec move_cooldown
    lda wand_active
    sta scroll_direction
    rts
    ldy #$00
    lda ($BB),y
    cmp #$85
    bcc $69D0
    cmp #$87
    bcs $69FC
    sec
    sbc #$85
    tax
    lda #$01
    sta $BF05,x
    lda #$00
    sta ($BB),y
    lda castle_power_gain,x
    sta score_delta_mid
    jsr power_add
    lda $6BFB,x
    sta score_delta_mid
    jmp SAVMSC
    and LNFLG,x
    brk
    brk
wand_mode:
    ; Sub-state 2 WAND: cursor moves, map does NOT scroll, wizard does NOT walk. Only for selecting S/V. Fire/key exits
    jsr color_flash
    lda STRIG0
    beq $6A26
    lda CH
    cmp #$FF
    bne $6A26
    lda STICK0
    eor #$0F
    sta scroll_direction
    sta wand_active
    beq $6A25
    jsr wand_render
    lda wand_direction
    sta wizard_move_dir
    rts
    inc sub_state
    rts
    brk
    brk
    brk
    brk
    brk
    brk
wand_return:
    ; Sub-state 3: return from wand mode - wait fire release, restore colors, state_advance=0
    jsr color_flash
    lda STRIG0
    beq $6A4C
    lda #$00
    sta sub_state
    lda color_pcolr0
    sta PCOLR0
    lda $5195
    sta color_pcolr3
    sta PCOLR3
    rts
.proc color_flash
    ; Color flash effect: cycles PCOLR0 hue every 2 frames
    dec flash_timer
    bpl $6A60
    lda #$01
    sta flash_timer
    lda PCOLR0
    clc
    adc #$10
    sta PCOLR0
    rts
.endp

    lda #$04
    sta $B9
    jmp $5635
    sty TXTROW
    inc $8D
    dex
    bpl $6A4F
    lda #$0B
    sta $6C9A
    rts
    .byte $32
    .byte $3F
    .byte $3F
    .byte $3F
    bmi $6AAC
    and (FEOF),y
    .byte $3F
    .byte $3F
player_auto_move:
    ; Automated wizard movement: approaches target at +-4px/frame with clamping
    ldx wand_direction
    lda joy_dx_table,x
    clc
    adc view_offset_x
    sta target_x
    cmp player_x
    beq $6AC1
    bcs $6AAD
    lda player_x
    sec
    sbc #$04
    sta player_x
    cmp target_x
    beq $6AC1
    bcs $6AC1
    lda target_x
    sta player_x
    jmp $6AC1
    lda player_x
    clc
    adc #$04
    sta player_x
    cmp target_x
    bcc $6AC1
    lda target_x
    sta player_x
    lda joy_dy_table,x
    clc
    adc view_offset_y
    sta target_y
    cmp player_y
    beq $6AFD
    bcs $6AE9
    lda player_y
    sec
    sbc #$04
    sta player_y
    cmp target_y
    beq $6AFD
    bcs $6AFD
    lda target_y
    sta player_y
    rts
    lda player_y
    clc
    adc #$04
    sta player_y
    cmp target_y
    bcc $6AFD
    lda target_y
    sta player_y
    rts
    .byte $17
    .byte $6C                     ; $6AFF
scroll_offsets:
    .byte $01                     ; $6B00
    .byte $80                     ; $6B01
wand_shape_ptrs:
    ; Wand shape pointers: 8 directions x 2 bytes each -> 9xx shape data
    brk
    eor $590F,y
    asl $2D59,x
    eor $593C,y
    .byte $4B
    eor $595A,y
    adc #$59
    sei
    eor $5987,y
    .byte $36                     ; $6B16
gameplay_colors:
    .byte $3A                     ; $6B17
    .byte $7A
    pla
    jmp $8CB8
    brk
    dec FRE,x
    ror $7A,x
    sec
    .byte $3A
    lsr DOSINI,x
    brk
    brk
anim_ptrs:
    ; Player animation frame pointers: 4 frames x 2 bytes
    stx $59,y
    .byte $9E
    eor $59A6,y
    .byte $AE                     ; $6B2E
    .byte $59                     ; $6B2F
state_dispatch_table:
    .byte $FF                     ; $6B30
    .byte $6B
    sta FRMADR,x
    brk
    eor $46,x
    .byte $5A
    jmp $805A
    .byte $43
    .byte $FF
    .byte $6B
    .byte $FF
    .byte $6B
    .byte $FF
    .byte $6B
    .byte $80
    .byte $64
ai_direction_map:
    ; Entity AI direction mapping: converts compass bits to movement direction index
    ora (CASINI,x)
    .byte $04
    ora TRAMSZ
    php
    ora #$0A
    php
    .byte $02
    .byte $04
    .byte $01                     ; $6B4F
ai_speed_table:
    .byte $50                     ; $6B50
    eor (BUFRLO,x)
    .byte $23
    .byte $14
    brk
    brk
    brk
    brk
    php
    .byte $FF
    .byte $6B
level_dispatch_table:
    ; Level initialization dispatch table
    brk
    adc $A2
    ora #$AC
    ora #$FF
    .byte $6B
    .byte $FF
    .byte $6B
    .byte $FF
    .byte $6B
    .byte $FF
    .byte $6B
    .byte $FF
    .byte $6B
    .byte $07
    bvs $6BC9
    .byte $72
    bvc $6B0E
    asl.w $009A
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    ldx $AA
    rol $EA
substate_dispatch_table:
    ; Gameplay sub-state dispatch: walk/wait/wand/return + death sub-states
    .byte $80
    .byte $5A
    ldy.w OLDROW
    ror
    bmi $6BF2
    .byte $FF
    .byte $6B
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
wand_dx:
    ; Wand cursor X deltas per direction (signed)
    brk
    brk
    brk
    .byte $FC
    .byte $FC
    .byte $FC
    brk
    .byte $04
    .byte $04
    .byte $04
wand_dy:
    ; Wand cursor Y deltas per direction (signed)
    .byte $FC
    .byte $04
    brk
    brk
    .byte $FC
    .byte $04
    brk
    brk
    .byte $FC
    .byte $04
map_view_colors:
    ; Colors for map view mode (M key)
    nop
    ldy $8A
castle_screen_addrs:
    ; Castle screen memory addresses: 3 castles x 2 bytes
    sei
    adc (DSKUTL),y
    .byte $87
    .byte $3A
    .byte $8B
    adc $8E,x
castle_map_positions:
    ; Castle positions in map coordinates: 3 castles x 2 bytes (for entity AI targeting)
    .byte $1A
    .byte $32
    .byte $3A
    .byte $3A
    adc FREQ,x
    brk
    brk
    brk
entity_move_deltas:
    ; Entity position change deltas per direction (unsigned, 0=0 for half-tile)
    .byte $80
    .byte $80
    brk
    ora ($81,x)
    .byte $7F
    brk
    ora ($7F,x)
    .byte $81                     ; $6BC5
entity_move_signs:
    .byte $01                     ; $6BC6
    brk
    brk
    ora (NGFLAG,x)
    brk
    brk
    brk
    ora (LNFLG,x)
    brk
    ora (CASINI,x)
    .byte $04
    php
    ora (CASINI,x)
    .byte $04
    asl MEMORY_REFRESH_CYCLES
barrier_break_prob:
    ; Probability of entity breaking barrier per entity group (higher=harder to break)
    .byte $02
    .byte $04
    bpl $6BE6
shield_cost:
    ; S shield building cost in POWER per strength level (BCD)
    jsr $1510
    .byte $20                     ; $6BE1
entity_max_alive:
    .byte $FA                     ; $6BE2
    .byte $7E                     ; $6BE3
    rti
    .byte $20                     ; $6BE5
    .byte $03                     ; $6BE6
entity_respawn_max:
    .byte $F9                     ; $6BE7
    adc $1F3F,x
    .byte $02
castle_damage_costs:
    ; Castle damage cost table: BCD amounts per entity group index
    ora (WARMST,x)
    .byte $02
    .byte $04
    .byte $16                     ; $6BF0
castle_score_rewards:
    .byte $12                     ; $6BF1
    ora #$06
    .byte $03
treasure_foul_costs:
    ; Foul cost for collecting treasures
    bcc $6C67
    bvc score_hi
castle_power_gain:
    ; Power gained when wizard picks up castle items (5-6)
    bpl $6C4B
    ora (ICHIDZ,x)
    brk
    brk
state0_idle:
    ; Game state 0: idle/init
    rts
scroll_direction:
    ; scroll_direction: joystick direction bits for scroll engine (U/D/L/R = bits 0-3)
    brk
spawn_type_idx:
    ; spawn_type_idx: current entity type index being spawned
    brk
game_over_timer:
    ; game_over_timer: countdown for game over animation
    brk
hscrol_counter:
    ; hscrol_counter: fine horizontal scroll position (0-7)
    brk
vscrol_counter:
    ; vscrol_counter: fine vertical scroll position (0-15)
    brk
wand_active:
    ; wand_active: wand/cursor active flag (joystick bits when wand out)
    brk
move_cooldown:
    ; move_cooldown: movement cooldown timer (8 frames between moves)
    brk
game_running:
    ; game_running: master game running flag (0=stopped)
    brk
init_dispatch_lo:
    ; init_dispatch_lo: initial dispatch vector lo (for game_init indirect JMP)
    brk
init_dispatch_hi:
    ; init_dispatch_hi: initial dispatch vector hi
    php
screen_initialized:
    ; screen_initialized: gameplay screen setup done flag
    brk
sub_state:
    ; sub_state: sub-state counter (0=walk, 1=wait, 2=wand, 3=return)
    brk
    brk
game_state:
    ; game_state: main state machine (0=idle,1=setup,2=play,3=attract,4=title input,5=death,9=gameover)
    brk
dispatch_lo:
    ; dispatch_lo: current dispatch vector low byte
    brk
dispatch_hi:
    ; dispatch_hi: current dispatch vector high byte
    brk
map_decode_counter:
    ; map_decode_counter: map decoder bit counter (counts down 4->0)
    brk
map_decode_accum:
    ; map_decode_accum: map decoder accumulated byte
    brk
map_decode_save_y:
    ; map_decode_save_y: saved Y during map decode
    brk
map_decode_col:
    ; map_decode_col: current column in map decode (0-F)
    brk
player_x:
    ; player_x: player PMG horizontal position
    brk
player_y:
    ; player_y: player PMG vertical position
    brk
anim_frame:
    ; anim_frame: player animation frame (0-3, decrements)
    brk
wand_direction:
    ; wand_direction: wand movement direction index (from joystick)
    brk
    brk
key_lock:
    ; key_lock: keyboard input lock flag
    brk
scroll_lock_h:
    ; scroll_lock_h: horizontal scroll boundary lock
    brk
scroll_lock_v:
    ; scroll_lock_v: vertical scroll boundary lock
    brk
flash_timer:
    ; flash_timer: color flash animation timer
    brk
target_x:
    ; target_x: wizard auto-move target X position
    brk
target_y:
    ; target_y: wizard auto-move target Y position
    brk
wizard_move_dir:
    ; wizard_move_dir: wizard movement direction for auto-move
    brk
missile_offset:
    ; missile_offset: missile PMG Y offset for cursor display
    brk
player_state:
    ; player_state: 0=normal, 1=tracking(V shield), 2=attacking, 3=dying
    brk
first_tile:
    ; first_tile: first tile value for map boundary fill
    brk
power_gem_value:
    ; power_gem_value: BCD value of current power gem pickup
    brk
castle_destroyed_0:
    ; castle_destroyed_0: castle 0 destroyed flag
    brk
castle_destroyed_1:
    ; castle_destroyed_1: castle 1 destroyed flag
    brk
castle_destroyed_2:
    ; castle_destroyed_2: castle 2 destroyed flag
    brk
    brk
spell_flag:
    ; spell_flag: 0=normal, 1=fire pressed(V activation), 2=shield build(S), 3=releasing
    brk
score_hi:
    ; score_hi: score high BCD byte
    brk
score_lo:
    ; score_lo: score low BCD byte
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
    brk
    brk
    brk
    brk
    brk
    brk
foul_hi:
    ; foul_hi: FOUL counter high BCD byte (6 digits total)
    brk
foul_mid:
    ; foul_mid: FOUL counter mid BCD byte
    brk
foul_lo:
    ; foul_lo: FOUL counter low BCD byte
    brk
score_delta_hi:
    ; score_delta_hi: score delta high (temporary for add/subtract)
    brk
score_delta_mid:
    ; score_delta_mid: score delta mid
    brk
score_delta_lo:
    ; score_delta_lo: score delta low
    brk
    brk
castle_score_hi:
    ; castle_score_hi: CASTLE damage score high BCD byte
    brk
castle_score_lo:
    ; castle_score_lo: CASTLE damage score low BCD byte
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
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    bvs $6D72
    bvs $6D7B
    and $778A
    lda $778A
    and $F78B
    lda $778B
    and $778C
    lda $778C
    and $778D
    lda $778D
    and $778E
    lda $578E
    and $C08F
    lsr $80
    adc $0680
    .byte $80
    asl LNFLG
    asl SOUNDR
    brk
    adc.w LNFLG
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
    brk
    brk
    brk
    brk
    brk
status_bar_text:
    ; Status bar screen memory: FOUL xxx, POWER xxxx, CASTLE xx xx xx, SCORE xxxxxxxx
    brk
    brk
    brk
    brk
    brk
    rol CIOCHR
    and ENTVEC,x
    brk
    brk
    bvc $6DDD
    bvc $6D8F
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    bmi $6DC9
    .byte $37
    and BUFRLO
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
    .byte $23
    and (BUFRHI,x)
    .byte $34
    bit.w ICBAHZ
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
    .byte $B3
    .byte $A3
    .byte $AF
    .byte $B2
    lda LNFLG
    brk
    bne $6D99
    bne $6D9B
    bne $6D9D
    brk
    .byte $54
    eor LMARGN,x
    .byte $42
    .byte $4F
    jsr $544A
    brk
    brk