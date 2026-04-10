; =============================================================
; Runtime segment: $0400-$25FF — Runtime code (copied from $1A90-$3C8F)
; =============================================================
; Copied from XEX $1A90-$3C8F to $0400-$25FF at runtime
; Offset: runtime = xex - $1690

    org $0400

    brk
    brk
    brk
    brk
    brk
    rol CIOCHR
    and ENTVEC,x
    brk
    brk
    bvc $045D
    bvc $040F
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    bmi $0449
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
    bne $0419
    bne $041B
    bne $041D
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
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
    beq $0622
    lda $9FFF
    eor #$FF
    sta $9FFF
    cmp $9FFF
    bne $0622
    sec
    rts
    lda #$00
    sta $A000
    clc
    rts
    jsr $0700
    cpy #$01
    beq $0636
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
    beq $070C
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
    bmi $0756
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
    bvs $07AD
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
    bne $078A
    dec $95
    ldx #$00
    lda #$00
    sta ($92,x)
    inc $92
    bne $0796
    inc $93
    dec $94
    lda $94
    cmp #$FF
    beq $07A1
    jmp $078C
    lda $95
    beq $07AA
    dec $95
    jmp $078C
    rts
    asl
    .byte $D2
    cmp $90
    bcs $07BB
    lda RANDOM
    and #$03
    clc
    adc #$70
    sta ($8C),y
    dey
    bpl $0792
    lda $8C
    clc
    adc #$80
    sta $8C
    bcc $07C9
    inc $8D
    lda $8E
    clc
    adc #$80
    sta $8E
    bcc $07D4
    inc $8F
    lda $8D
    cmp #$9F
    bne $0790
    rts
    lda ($8E),y
    cmp #$74
    bcs $07EC
    cmp #$70
    bcc $07EC
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
    bne $081D
    sta scroll_direction
    ldx #$7F
    lda $5180,x
    sta wizard_y_tile,x
    dex
    bpl $0828
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
    lda gameplay_colors,x
    sta COLOR0,x
    dex
    bpl $0870
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
    beq $08A3
    inx
    inx
    inx
    lda $84
    clc
    adc #$80
    sta $84
    bcc $0885
    inc $85
    jmp $0885
    lda #$00
    sta AUDC1
    lda STRIG0
    bne $08BF
    ldy power_gem_value
    sty $6BFD
    lda ai_speed_table,y
    sta spawn_interval
    sta $51A0
    jmp $0A00
    lda STICK0
    cmp #$0F
    beq $08A3
    inc power_gem_value
    inc $6860
    lda power_gem_value
    cmp #$05
    bne $08DD
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
    bne $08E7
    jmp $08A3
    bpl $08ED
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
game_init:
    ; Indirect dispatch via (C08) - first call goes to map decoder
    jmp (init_dispatch_lo)
map_decoder:
    ; Decompresses map data from E00+: packs 4 tiles/byte with 2-bit rotation into screen RAM at (8)
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
    beq $094E
    and #$C0
    cmp #$80
    bcc $094B
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
    bne $098D
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
    bne $098D
    lda #$00
    sta map_decode_col
    lda $88
    clc
    adc #$28
    sta $88
    bcc $098D
    inc $89
    iny
    bpl $0939
    lda $86
    clc
    adc #$80
    sta $86
    bcc $0937
    inc $87
    lda $87
    cmp #$A0
    bne $0937
    rts
level_init:
    ; Level init: decode map, setup entities via /bin/bashB00, INC game_state
    jsr map_decoder
    jsr cursor_update
    inc game_state
    rts
level_init_next:
    ; Alternate level init: decode map, call 500 (level renderer)
    jsr map_decoder
    jsr level_renderer
    rts
    jmp ($00A0)
    lda ($86),y
    beq $09CB
    and #$C0
    cmp #$80
    bcc $09C8
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
    bne $0A0A
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
    bne $0A0A
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
    bpl $0A13
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
    bpl $0A41
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
    bpl $0A60
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
    bcc $0A6B
    inc $97
    lda $97
    cmp #$9F
    bne $0A6B
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
    bne $0AA5
    rts
.proc map_tile_fill
    ; Fill map tiles at boundaries
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
    beq $0AC7
    rts
.endp

    sta $0CA9,y
    ldy #$7D
    sta ($96),y
    sta ($98),y
    dey
    bpl $0ADD
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
.proc cursor_update
    ; Update player cursor/missile position on screen
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
.endp

    lda #$6E
    sta $97
    lda #$9F
    sta $99
    lda #$0C
    ldy #$7D
    sta ($96),y
    sta ($98),y
    dey
    bpl $0B4D
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
    bcc $0B58
    inc $97
    lda $97
    cmp #$9F
    bne $0B58
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
    bcs $0C3D
    cmp #$15
    bcc $0C1C
    lda #$85
    sta ($8C),y
    jmp $0C3D
    jsr $0C78
    sta ($8C),y
    lda RANDOM
    and #$07
    beq $0C3D
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
    bpl $0C0A
    lda $8C
    clc
    adc #$80
    sta $8C
    bcc $0C08
    inc $8D
    lda $8D
    cmp #$9F
    bne $0C08
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
    clc
    adc #$41
    rts
    lda $0C55,x
    tax
    ldy $0C5C,x
    beq $0C93
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
    bmi $0C9E
    inc $8D
    lda $8D
    cmp #$75
    bne $0C9E
    ldy #$11
    lda #$0B
    sta ($8C),y
    dey
    bpl $0CB6
    rts
    dex
    lda $8C
    clc
    adc #$80
    sta $8C
    bcc $0C85
    inc $8D
    lda $8D
    cmp #$9F
    bne $0C85
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
    bne $0D3D
    ldy #$00
    inc $6CC6
    lda $6CC6
    cmp #$92
    bcc $0D16
    lda power_gem_value
    cmp #$03
    bcs $0D64
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
    bcs $0D76
    jmp $0D60
    tya
    pha
    ldy #$03
    lda #$E1
    sta ($96),y
    dey
    bpl $0D88
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
    bne $0DA1
    pla
    tay
    rts
    sbc #$00
    sta $97
    jsr $0D82
    iny
    iny
    cpy #$08
    bne $0DBA
    ldy #$00
    inc $6CC6
    lda $6CC6
    cmp #$92
    bcc $0D93
    lda power_gem_value
    cmp #$03
    bcs $0DE1
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
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
pmg_base:
    ; Player/missile graphics base (PMBASE=$10)
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
pmg_missiles:
    ; Missile graphics area
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
pmg_player0:
    ; Player 0 graphics: wizard character (walk mode) / wand cursor (wand mode)
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
pmg_player1:
    ; Player 1 graphics area
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
pmg_player2:
    ; Player 2 graphics area
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
pmg_player3:
    ; Player 3 graphics: directional indicator (walk mode) / stationary wizard marker (wand mode)
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
scroll_edge_up:
    ; Scroll at top edge: updates 10A (map row), shifts entity data, boundary clamp
    ldx wizard_y_tile
    cpx #$10
    beq $1827
    lda move_cooldown
    bne $1814
    dec map_row
    ldy #$01
    jsr adjust_bb_sub
    dec view_offset_y
    dec view_offset_y
    dex
    dex
    stx wizard_y_tile
    cpx $5180
    bne $1827
    dec scroll_lock_v
    rts
scroll_edge_down:
    ; Scroll at bottom edge: updates 10A, shifts entity data
    ldx wizard_y_tile
    cpx #$80
    beq $1827
    lda move_cooldown
    bne $183C
    inc map_row
    ldy #$01
    jsr adjust_bb_add
    inc view_offset_y
    inc view_offset_y
    inx
    inx
    jmp $181C
scroll_edge_left:
    ; Scroll at left edge: updates 109 (map col), boundary clamp
    ldx wizard_x_tile
    cpx #$38
    beq $1827
    lda move_cooldown
    bne $185B
    dec map_col
    ldy #$00
    jsr adjust_bb_sub
    dec view_offset_x
    dex
    stx wizard_x_tile
    cpx $5181
    bne $1827
    dec scroll_lock_h
    rts
scroll_edge_right:
    ; Scroll at right edge: updates 109
    ldx wizard_x_tile
    cpx #$C0
    beq $1827
    lda move_cooldown
    bne $187F
    inc map_col
    ldy #$00
    jsr adjust_bb_add
    inc view_offset_x
    inx
    jmp $185F
.proc adjust_bb_add
    ; Add B00[Y] to screen base pointer /
    lda $BB
    clc
    adc scroll_offsets,y
    sta $BB
    bcc $1892
    inc $BC
    rts
.endp

.proc adjust_bb_sub
    ; Subtract B00[Y] from screen base pointer /
    lda $BB
    sec
    sbc scroll_offsets,y
    sta $BB
    bcs $1892
    dec $BC
    rts
.endp

    .byte $03
    dec scroll_lock_v
    rts
    ldx wizard_y_tile
    cpx #$80
    beq $18A4
    lda move_cooldown
    bne $18B9
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
    beq $18A4
    lda move_cooldown
    bne $18D8
    dec map_col
    ldy #$00
    jsr adjust_bb_sub
    dec view_offset_x
    dex
    stx wizard_x_tile
    cpx $5181
    bne $18A4
    dec scroll_lock_h
    rts
    ldx wizard_x_tile
    cpx #$C0
    beq $18A4
    lda move_cooldown
    bne $18FC
    inc map_col
    ldy #$00
    jsr VBI_SCANLINE
    .byte $23
    brk
    brk
    brk
.proc scroll_engine
    ; Scroll engine: reads joystick bits from $6C00, scrolls map via HSCROL/VSCROL. Called ONLY in walking mode (sub-state 0), NOT in wand mode
    lsr scroll_direction
    bcc $1908
    jsr scroll_up
    lsr scroll_direction
    bcc $1910
    jsr scroll_down
    lsr scroll_direction
    bcc $1918
    jsr scroll_left
    lsr scroll_direction
    bcc $1920
    jsr scroll_right
    lda hscrol_counter
    sta HSCROL
    lda vscrol_counter
    sta VSCROL
    rts
.endp

scroll_up:
    ; Scroll up: DEC VSCROL by 2, at boundary updates tile row pointer and INC 111
    lda scroll_lock_v
    beq $1935
    jmp scroll_edge_up
    dec vscrol_counter
    dec vscrol_counter
    lda vscrol_counter
    cmp #$FE
    bne $1965
    lda map_y_fine
    cmp #$59
    bne $1955
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
scroll_down:
    ; Scroll down: INC VSCROL by 2, at boundary DEC 111 (map Y position)
    lda scroll_lock_v
    beq $196E
    jmp scroll_edge_down
    lda map_y_fine
    bne $1979
    inc scroll_lock_v
    jmp scroll_edge_down
    inc vscrol_counter
    inc vscrol_counter
    lda vscrol_counter
    cmp #$10
    bne $1996
    lda #$00
    sta vscrol_counter
    dec map_y_fine
    inc map_row
    ldy #$01
    jsr update_lms_sub
    rts
scroll_left:
    ; Scroll left: INC HSCROL, at boundary DEC 110/109 (map X position)
    lda scroll_lock_h
    beq $199F
    jmp scroll_edge_left
    lda map_x_fine
    bne $19AA
    inc scroll_lock_h
    jmp scroll_edge_left
    inc hscrol_counter
    lda hscrol_counter
    cmp #$08
    bne $19C4
    lda #$00
    sta hscrol_counter
    dec map_x_fine
    dec map_col
    ldy #$00
    jsr update_lms_add
    rts
scroll_right:
    ; Scroll right: DEC HSCROL, at boundary INC 110/109
    lda scroll_lock_h
    beq $19CD
    jmp scroll_edge_right
    dec hscrol_counter
    lda hscrol_counter
    cmp #$FF
    bne $19F7
    lda map_x_fine
    cmp #$6A
    bne $19E7
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
.proc update_lms_sub
    ; Subtract offset from all LMS addresses in display list when scrolling vertically
    ldx #$00
    lda $6D04,x
    clc
    adc scroll_offsets,y
    sta $6D04,x
    bcc $1A09
    inc $6D05,x
    inx
    inx
    inx
    cpx #$21
    bne $19FA
    lda $BB
    clc
    adc scroll_offsets,y
    sta $BB
    bcc $1A1C
    inc $BC
    rts
.endp

.proc update_lms_add
    ; Add offset to all LMS addresses in display list when scrolling vertically
    ldx #$00
    lda $6D04,x
    sec
    sbc scroll_offsets,y
    sta $6D04,x
    bcs $1A2E
    dec $6D05,x
    inx
    inx
    inx
    cpx #$21
    bne $1A1F
    lda $BB
    sec
    sbc scroll_offsets,y
    sta $BB
    bcs $1A41
    dec $BC
    rts
.endp

    lda scroll_lock_h
    beq $1A4A
    jmp scroll_edge_right
    dec hscrol_counter
    lda hscrol_counter
    cmp #$FF
    bne $1A74
    lda map_x_fine
    cmp #$6A
    bne $1A64
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
entity_update_all:
    ; Updates 4 entity groups in round-robin: G1(10), G2(3), G3(2), G4(2) = 17 total
    lda #$09
    sta $9E
    ; entity_update_all: $A2=$14 for group 1 (entity type marker = item tile $14)
    lda #$14
    sta $A2
    lda #$00
    sta $BD
    ldx entity_grp1_idx
    lda entity_grp1_ypos,x
    beq $1B3C
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
    bpl $1B0F
    cpx #$FF
    bne $1B48
    ldx $5184
    stx entity_grp1_idx
    lda #$02
    sta $9E
    ; INC $A2: group 2 marker = $15
    inc $A2
    inc $BD
    ldx entity_grp2_idx
    lda $B480,x
    beq $1B83
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
    bpl $1B56
    cpx #$FF
    bne $1B8F
    ldx $5185
    stx entity_grp2_idx
    lda #$01
    sta $9E
    inc $A2
    inc $BD
    ldx entity_grp3_idx
    lda $B640,x
    beq $1BCA
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
    bpl $1B9D
    cpx #$FF
    bne $1BD6
    ldx $5186
    stx entity_grp3_idx
    lda #$01
    sta $9E
    inc $A2
    inc $BD
    ldx entity_grp4_idx
    lda $B720,x
    beq $1C11
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
    bpl $1BE4
    cpx #$FF
    bne $1C1D
    ldx $5187
    stx entity_grp4_idx
    dec $5126
    bne $1C71
    lda #$03
    sta $5126
    lda #$00
    sta $9E
    inc $A2
    inc $BD
    ldx $5108
    lda $B783,x
    beq $1C62
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
    bpl $1C35
    cpx #$FF
    bne $1C6E
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
    bcs $1C8E
    lda #$1E
    sta $B8
    lda #$01
    jsr $1E80
    lda #$CD
    sta ($B4),y
    rts
    lda player_state
    cmp #$03
    beq $1C9B
    rts
    dec $B7
    bpl $1CB5
    lda #$02
    sta $B7
    dec $B9
    ldy $B9
    cpy #$FF
    bne $1CAD
    ldy #$07
    lda $6B4C,y
    sec
    sbc #$01
    sta $B6
    ldy #$00
    lda $B8
    sta ($B4),y
    lda RANDOM
    beq $1CEC
    ldy $B6
    lda entity_move_signs,y
    beq $1CD6
    lda $B4
    sec
    sbc entity_move_deltas,y
    sta $B4
    bcs $1CE2
    dec $B5
    jmp $1C65
    lda $B4
    clc
    adc entity_move_deltas,y
    sta $B4
    bcc $1CE2
    inc $B5
    ldy #$00
    lda ($B4),y
    sta $B8
    cmp #$0C
    bne $1CFA
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
    beq $1D68
    cmp #$0B
    beq $1D68
    cmp #$CD
    bne $1D23
    lda $A0
    sta $B8
    jmp $1D58
    lda $BD
    cmp #$04
    beq $1D47
    cmp #$02
    bne $1D54
    lda $A0
    and #$3F
    cmp #$05
    bcs $1D54
    lda RANDOM
    and #$03
    cmp difficulty
    bcc $1D42
    jmp $1D54
    lda #$85
    jmp $1D56
    lda $A0
    and #$3F
    cmp #$05
    bcs $1D54
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
    bcs $1D8F
    lda RANDOM
    and #$1F
    beq $1D87
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
    beq $1DB5
    sta $B0
    tay
    lda castle_destroyed_1,y
    beq $1DA4
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
    bcc $1DD9
    lda $A6
    cmp $AF
    beq $1DD9
    bcs $1DD5
    lda #$02
    jmp $1DD7
    lda #$01
    sta $9F
    lda $A7
    cmp $A8
    bcc $1DF2
    lda $A5
    cmp $AE
    beq $1DF2
    bcs $1DEC
    lda #$08
    jmp $1DEE
    lda #$04
    ora $9F
    sta $9F
    lda $9F
    bne $1DF7
    rts
    sec
    sbc #$01
    tay
    lda entity_move_signs,y
    beq $1E0F
    lda $9C
    sec
    sbc entity_move_deltas,y
    sta $9C
    bcs $1E1B
    dec $9D
    jmp $1E1B
    lda $9C
    clc
    adc entity_move_deltas,y
    sta $9C
    bcc $1E1B
    inc $9D
    ldy #$00
    lda ($9C),y
    beq $1E87
    cmp #$C7
    bcc $1E6C
    cmp #$CB
    bcs $1E33
    lda #$1D
    sta ($9A),y
    lda #$02
    jsr item_collect
    rts
    cmp #$CD
    bne $1E41
    lda #$1E
    sta ($9A),y
    lda #$01
    jsr item_collect
    rts
    cmp #$F2
    bcs $1E87
    cmp #$E3
    bcs $1E76
    pha
    ldy $BD
    lda barrier_break_prob,y
    sta $BA
    pla
    cmp #$E0
    beq $1E5E
    lsr $BA
    cmp #$E1
    beq $1E5E
    lsr $BA
    ldy #$00
    lda RANDOM
    cmp $BA
    bcs $1E7F
    lda #$E3
    sta ($9C),y
    rts
    and #$3F
    cmp #$0B
    beq $1E87
    cmp #$07
    bcc $1E87
    lda $A1
    clc
    adc #$40
    sta $A1
    bcc $1EC3
    lda RANDOM
    and #$1C
    sta $A1
    rts
    lda $A0
    cmp #$0B
    beq $1E8F
    sta ($9A),y
    lda ($9C),y
    sta $A0
    cmp #$0B
    beq $1E9B
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
    bne $1EC3
    lda $A6
    cmp map_row
    bne $1EC3
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
    bcs $1EE9
    eor #$FF
    adc #$01
    sta $A7
    lda $A6
    sec
    sbc $AF
    bcs $1EF6
    eor #$FF
    adc #$01
    sta $A8
    rts
    lda ($90,x)
    .byte $44
    lda RANDOM
    .byte $29                     ; $1EFF
item_collect:
    .byte $48                     ; $1F00
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
    bcc $1F3D
    beq $1F20
    jmp $1F4E
    lda $5480,y
    sed
    clc
    adc power_gem_value
    sta score_delta_mid
    cld
    jsr score_add
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
    bcc $1F63
    cmp #$46
    bcc $1F62
    inx
    inx
    lda castle_power_0,x
    sec
    sed
    sbc castle_damage_costs,y
    sta castle_power_0,x
    cld
    beq $1F73
    bcs $1F38
    lda #$00
    sta castle_power_0,x
    inx
    inc castle_destroyed_0,x
    lda difficulty
    bne $1F86
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
    bcc $1FE0
    cmp #$46
    bcc $1FDF
    inx
    inx
    lda castle_power_0,x
    sec
    sed
    sbc castle_damage_costs,y
    sta castle_power_0,x
    cld
    beq $1FF0
    bcs $1FB5
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
cursor_hit_handler:
    ; Walking mode tile interaction (P3PF bit 0): tile $0B=wall (single-hit destroy, $50 POWER cost, becomes $86). Tiles $14-$18=entity markers (item_collect A=0, kill cost)
    lda $A9
    cmp #$04
    bcc $2111
    lda $AA
    cmp #$08
    bcc $211C
    ldy #$81
    jmp $2123
    lda $AA
    cmp #$08
    bcc $2121
    ldy #$80
    jmp $2123
    ldy #$01
    jmp $2123
    ldy #$00
    lda ($BB),y
    cmp #$0B
    bne $213B
    lda #$50
    sta score_delta_hi
    jsr power_subtract
    lda #$01
    sta $BF0E
    lda #$86
    sta ($BB),y
    rts
    cmp #$14
    bcc $2153
    cmp #$19
    bcs $2153
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
entity_move:
    ; Entity AI: check tile, deposit item (5/6), navigate toward castles, break barriers, decrement score
    lda $A3
    sta $9A
    sta $9C
    lda $A4
    sta $9B
    sta $9D
    ldy #$00
    lda ($9A),y
    cmp $A2
    beq $238C
    cmp #$0B
    beq $238C
    cmp #$CD
    bne $2323
    lda $A0
    sta $B8
    jmp $2358
    lda $BD
    cmp #$04
    beq $2347
    cmp #$02
    bne $2354
    lda $A0
    and #$3F
    cmp #$05
    bcs $2354
    lda RANDOM
    and #$03
    cmp difficulty
    bcc $2342
    jmp $2354
    lda #$85
    jmp $2356
    lda $A0
    and #$3F
    cmp #$05
    bcs $2354
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
    lda foul_lo
    sec
    sbc #$01
    sta foul_lo
    lda foul_hi
    sbc #$00
    sta foul_hi
    lda foul_damage_lo
    clc
    adc #$01
    sta foul_damage_lo
    lda foul_damage_hi
    adc #$00
    sta foul_damage_hi
    cld
    rts
    jsr entity_calc_position
    lda $A1
    and #$03
    cmp #$02
    bcs $23B3
    lda RANDOM
    and #$1F
    beq $23AB
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
    beq $23D9
    sta $B0
    tay
    lda castle_destroyed_1,y
    beq $23C8
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
    bcc $23FD
    lda $A6
    cmp $AF
    beq $23FD
    bcs $23F9
    lda #$02
    jmp $23FB
    lda #$01
    sta $9F
    lda $A7
    cmp $A8
    bcc $2416
    lda $A5
    cmp $AE
    beq $2416
    bcs $2410
    lda #$08
    jmp $2412
    lda #$04
    ora $9F
    sta $9F
    lda $9F
    bne $241B
    rts
    sec
    sbc #$01
    tay
    lda entity_move_signs,y
    beq $2433
    lda $9C
    sec
    sbc entity_move_deltas,y
    sta $9C
    bcs $243F
    dec $9D
    jmp $243F
    lda $9C
    clc
    adc entity_move_deltas,y
    sta $9C
    bcc $243F
    inc $9D
    ldy #$00
    lda ($9C),y
    beq $24C2
    cmp #$C7
    bcc $2493
    cmp #$CB
    bcs $2457
    lda #$1D
    sta ($9A),y
    lda #$02
    jsr item_collect
    rts
    cmp #$CD
    bne $2468
    lda #$1E
    sta ($9A),y
    lda #$01
    sta $BF0A
    jsr item_collect
    rts
    cmp #$F2
    bcs $24C2
    cmp #$E3
    bcs $24B1
    pha
    ldy $BD
    lda barrier_break_prob,y
    sta $BA
    pla
    cmp #$E0
    beq $2485
    lsr $BA
    cmp #$E1
    beq $2485
    lsr $BA
    ldy #$00
    lda RANDOM
    cmp $BA
    bcs $24BA
    lda #$E3
    sta ($9C),y
    rts
    and #$3F
    cmp #$0B
    beq $24C2
    cmp #$07
    bcc $24C2
    cmp #$0E
    bcc $24B1
    cmp #$12
    bcs $24B1
    lda #$00
    sta ($9C),y
    lda #$01
    sta $BF01
    jmp $24C2
    lda $A1
    clc
    adc #$40
    sta $A1
    bcc $24FE
    lda RANDOM
    and #$1C
    sta $A1
    rts
    lda $A0
    cmp #$0B
    beq $24CA
    sta ($9A),y
    lda ($9C),y
    sta $A0
    cmp #$0B
    beq $24D6
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
    bne $24FE
    lda $A6
    cmp map_row
    bne $24FE
    lda #$1D
    sta ($9C),y
    lda #$00
    jsr item_collect
    rts
.proc entity_calc_position
    ; Calculate entity screen position from address for AI targeting
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
.endp

.proc entity_distance_calc
    ; Calculate distance from entity to target (castle or wizard)
    lda $A5
    sec
    sbc $AE
    bcs $2524
    eor #$FF
    adc #$01
    sta $A7
    lda $A6
    sec
    sbc $AF
    bcs $2531
    eor #$FF
    adc #$01
    sta $A8
    rts
.endp

    lda ($90,x)
    .byte $44
    lda RANDOM
    and #$1C
    sta $A1
    rts
    lda $A0
    cmp #$0B
    beq $2547
    sta ($9A),y
    lda ($9C),y
    sta $A0
    cmp #$0B
    beq $2553
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
    bne $257B
    lda $A6
    cmp map_row
    bne $2586
    brk
    rti
    brk
    brk
    brk
    brk
    brk
    brk
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
    bne $2592
    inc $B4
    inc $B6
    inx
    txa
    cmp $B7
    bne $2590
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
    bvs $2662
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