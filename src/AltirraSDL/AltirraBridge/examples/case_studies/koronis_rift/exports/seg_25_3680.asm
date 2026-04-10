; =============================================================
; Segment 25: $3680-$87CB — Main game code $3680-$87CB (compressed, final)
; =============================================================
; XEX load range: $3680-$87CB (20812 bytes)
; INI at $B504 (decompress)
; INI at $B100 ()

    org $3680

    .byte $A2,$DE,$3F,$8E,$06,$02,$8D,$04,$FA,$8C,$05,$59,$4C,$CE,$20,$B7 ; $3680
    .byte $E7,$A9,$16,$A0,$25,$59,$BE,$70,$FB,$81,$CF,$92,$0A,$DB,$54,$8D ; $3690
    .byte $B0,$59,$86,$85,$91,$17,$00,$AC,$E2,$8B,$84,$CD,$E9,$82,$4F,$DC ; $36A0
    .byte $36                                              ; $36B0
title_decomp_source:
    .byte $60,$38,$4F,$24,$18,$E7,$A2,$52,$8E,$02,$7A,$4A,$0A,$D2,$80,$08 ; $36B1
title_decompress:
    eor DINDEX,x
    .byte $92
    .byte $5F
    .byte $03
    .byte $0B
    rol $8C,x
    .byte $07
    .byte $02
    .byte $EF
    jsr SIOV
    bmi $36D5
    plp
    bcs $36D3
    rts
    eor ($AD,x)
    .byte $FC
    ldy #$F6
    .byte $02
    .byte $17
    pla
    clc
    .byte $E7
    adc $F780,y
    eor $5C45,x
    inx
    iny
    cpy #$43
    bne $36DF
    sbc $8D82,x
    sta (LTEMP,x)
    .byte $5F
    tya
    sta $A7
    .byte $17
    rti
    .byte $95                     ; $36F6
wait_vblank:
    .byte $F5                     ; $36F7
    cpx $251F
    .byte $2B
    .byte $07
    and ($37),y
    and $4A44,x
    .byte $4F
    eor OLDCOL,x
    .byte $FF
    adc ($67,x)
    adc $7872
    adc $8883,x
    .byte $FF
    sta $9792
    .byte $9C
    lda ($A6,x)
    .byte $AB
    .byte $AF
    .byte $FF
    ldy $B8,x
    ldy $C5C1,x
    cmp #$CC
    bne $3720
    .byte $D4
    .byte $D7
    .byte $DA
    cmp $E3E0,x
    inc $E9
    .byte $FF
    .byte $EB
    sbc $F2F0
    .byte $F4
    sbc ZTEMP4,x
    sed
    .byte $FF
    .byte $FA
    .byte $FB
    .byte $FC
    sbc (COUNTR,x)
    .byte $04
    cmp $03,x
    .byte $42
    .byte $83
    .byte $C2
    inc $36CF,x
    ror
    tya
    .byte $BF
    dec $03F5,x
    ora $FF
    sbc $D7E8,x
    sta $56,x
    asl $A6
    .byte $34
    bcs $376B
    .byte $7F
    jmp ($D4AB)
    .byte $E7
    .byte $E2
    .byte $FB
    .byte $8F
    .byte $3F
    cmp COLRSH,x
    .byte $EF
    ldx $16F1
    asl $D107,x
    .byte $7B
    asl $FF
    bvs $3722
    .byte $DF
    .byte $E3
    .byte $F3
    sty $1F
    stx $E9,y
    .byte $17
    .byte $DF
    and (PM_DMA_CYCLES,x)
    .byte $C3
    .byte $5B
    cmp $3EFC
    and $C5EC,x
    .byte $4F
    lda (DELTAC),y
    cpx $30C5
    .byte $77
    .byte $1F
    ora BUFRLO,x
    .byte $63
    and #$02
    dec CHKSUM
    asl
    bcc $3790
    dex
    txa
    eor $BF
    ora $67D9,y
    lsr ICAX1Z,x
    tax
    lda DELTAR
    .byte $B7
    rol $48
    sta $CA29
    eor #$3F
    sta $FE78,x
    .byte $9C
    ldx $2C19
    .byte $B2
    bcc $37AE
    cpy $4490
    ora $CAFA,y
    ora (DSKFMS),y
    eor $2BAE
    .byte $B2
    lda $D1FD,x
    plp
    eor #$17
    inc $9E3D
    .byte $83
    cmp #$A9
    lda $EFEB,x
    ora ($A5),y
    .byte $1B
    .byte $7B
    ora DSKFMS
    rts
    brk
    .byte $7C
    .byte $1A
    .byte $5F
    jsr $047D
    cpy $65
    and #$06
    and #$B0
    .byte $D7
    lsr ICBLLZ
    .byte $CB
    lsr ICAX2Z,x
    sbc ($F6,x)
    clv
    asl $2230
    bpl $3791
    bpl $3791
    .byte $89
    sty FMSZPG
    bne $3823
    lda $13
    and (CRITIC),y
    .byte $34
    and $0F,x
    bvs $3792
    lsr ATRACT
    ora $4119
    bcs $37A1
    adc #$69
    pla
    .byte $83
    lda #$0D
    sta VCOUNT_START
    .byte $97
    .byte $87
    .byte $5B
    rol OLDROW,x
    adc $67EE,y
    ora (DOSVEC),y
    jsr $6AA0
    stx $E610
    ldy MEMORY_REFRESH_CYCLES,x
    lda #$6E
    bvs $3826
    stx $88E8
    .byte $EF
    adc $0E6C
    .byte $6F
    dex
    bmi $37B9
    .byte $F2
    .byte $80
    inc $1E
    sta $CD
    .byte $80
    lsr $44
    inc $87
    eor ($85,x)
    sty APPMHI
    inc VCOUNT_START
    ror $71,x
    .byte $57
    txa
    .byte $47
    jsr $4030
    .byte $67
    adc $1F,x
    .byte $83
    .byte $93
    .byte $A3
    .byte $F3
    .byte $03
    asl $78
    cli
    .byte $7C
    and $5B2F,x
    iny
    dey
    dec $790B,x
    .byte $3F
    asl $C2,x
    .byte $CB
    .byte $C2
    .byte $14
    .byte $F3
    ldx $16,y
    .byte $BF
    bpl $380A
    pla
    jmp ($F2EE)
    cmp (CIX,x)
    bmi $38CB
    bcc $3807
    .byte $6F
    stx.w STATUS
    ora (DOSVEC),y
    .byte $A7
    .byte $F4
    ldx $90
    .byte $77
    ldy $97,x
    cmp (WARMST,x)
    ora $AA66
    .byte $3A
    .byte $0F
    sta (KEYDEF),y
    sei
    bmi $38E9
    lda COUNTR
    stx $6D
    dey
    .byte $33
    .byte $64
    ldx $96
    beq $3811
    bit $D2
    eor (BUFRFL),y
    .byte $1C
    eor ($44,x)
    ldy #$71
    asl
    bvc $3835
    .byte $13
    lsr ICSTAZ,x
    .byte $03
    .byte $CB
    .byte $0B
    .byte $80
    dec BFENLO
    eor DOSINI
    dec $56
    .byte $67
    .byte $0C
    cpy $1B
    sei
    .byte $89
    jmp $6CC4
    ldy $CA9A,x
    .byte $9C
    adc (ATRACT,x)
    txa
    iny
    .byte $B7
    stx RMARGN,y
    sty $84
    and #$A2
    eor PM_DMA_CYCLES
    ldx MEMORY_REFRESH_CYCLES
    txs
    jsr $4205
    .byte $57
    lsr $A4
    sta $67C4,x
    .byte $2F
    .byte $64
    .byte $03
    asl $6066,x
    .byte $D3
    .byte $73
    .byte $43
    .byte $8B
    cmp $44
    .byte $3F
    .byte $6F
    sta $0F
    .byte $5F
    .byte $DA
    .byte $6B
    .byte $80
    .byte $23
    .byte $F3
    .byte $F2
    asl ICAX2Z
    ora SOUNDR,x
    jmp $2C41
    and ($B3,x)
    .byte $54
    cpy $14
    .byte $C3
    .byte $12
    .byte $23
    brk
    .byte $3B
    .byte $80
    .byte $F7
    .byte $9C
    .byte $FA
    sed
    ldy $82D7,x
    .byte $07
    .byte $33
    .byte $33
    asl
    rti
    .byte $54
    adc $4C30
    cmp (STATUS),y
    .byte $02
    .byte $14
    adc #$82
    rts
    .byte $8F
    .byte $64
    lsr
    ror $2255
    .byte $17
    sta $8862
    bvc $3970
    ror SHFAMT,x
    .byte $7B
    adc #$E7
    adc $5943
    .byte $4F
    .byte $34
    tay
    adc WARMST
    tsx
    .byte $82
    .byte $53
    .byte $02
    rts
    cmp $6002
    .byte $1B
    bvs $3955
    .byte $43
    .byte $54
    adc $E7
    ror $87,x
    .byte $D7
    rti
    cpx #$22
    sta $3710
    .byte $5C
    cmp ROWAC,x
    txs
    plp
    .byte $E3
    dec $7AB9
    txa
    .byte $03
    sta ($C0),y
    .byte $BB
    pha
    dec MEMORY_REFRESH_CYCLES,x
    eor #$B3
    .byte $83
    asl $C872
    bvs $3948
    .byte $5C
    asl
    cpx #$0B
    .byte $DA
    .byte $EB
    .byte $1C
    dey
    asl $8E05
    bvc $399D
    txs
    .byte $62
    sbc $6FFF,x
    .byte $7F
    .byte $8F
    .byte $93
    lda $FF
    bit $61
    ldy $1E
    eor ($8D,x)
    brk
    sta $2035
    sta $7253,y
    .byte $F3
    inc BFENLO
    .byte $17
    adc $2B2C,y
    ror
    asl $1149
    and $B3C3,y
    ldx VBI_SCANLINE
    sta $A1,x
    sty $93,x
    .byte $3F
    .byte $2F
    ror $7F43
    bmi $39E8
    and $80
    eor RMARGN,x
    and $C9,x
    .byte $7A
    inc $3379
    lda (DSKFMS,x)
    .byte $34
    asl RECVDN,x
    and $C005
    .byte $89
    inc $EDCD
    ora FREQ
    .byte $DB
    lda $1D4A,y
    .byte $FB
    ldx $330F,y
    .byte $FC
    ldy #$FE
    lda $84BC,y
    .byte $0B
    bcc $3956
    asl $88,x
    rts
    .byte $89
    pla
    and $83
    tya
    .byte $9B
    cpy $BA30
    lda #$2B
    .byte $04
    cli
    .byte $AB
    and (NOCKSM,x)
    .byte $E2
    adc $16,x
    dey
    ora ($B9,x)
    .byte $4B
    sty PM_DMA_CYCLES
    .byte $17
    inc $AA,x
    .byte $04
    eor APPMHI,x
    ora #$0F
    .byte $4F
    asl DRKMSK,x
    asl $EF16
    sec
    ora (FREQ,x)
    rti
    pla
    cli
    .byte $34
    .byte $9C
    ora (ROWCRS,x)
    lsr APPMHI,x
    cmp $CBE4
    bcs $3A0E
    .byte $1A
    and #$09
    .byte $F3
    .byte $33
    bit $920A
    bcc $398A
    .byte $73
    .byte $63
    .byte $9F
    sty $98,x
    ora ROWCRS
    .byte $E2
    bvs $3A13
    eor $65
    eor APPMHI,x
    .byte $E3
    sec
    brk
    sta (CASINI),y
    .byte $F4
    beq $3A12
    inc $AB10
    .byte $42
    sta (BUFRFL,x)
    rti
    txa
    sec
    .byte $BB
    bvc $39B5
    tsx
    .byte $9B
    sta $6F88,y
    tax
    .byte $0B
    .byte $82
    cpx #$60
    brk
    brk
    cli
    .byte $C2
    eor $375E,y
    .byte $A3
    lda #$EE
    .byte $83
    ora #$E8
    ldy #$F0
    ldx $BDEA,y
    .byte $14
    tax
    .byte $0B
    cmp ($FA),y
    cmp ($AF,x)
    sbc #$F0
    lda #$C5
    cmp $DC
    iny
    ror $8E7E
    sta ESIGN,x
    lda $C5,x
    .byte $43
    pha
    inx
    bmi $3AD7
    tya
    and $05B0,x
    rol $C8,x
    clc
    and LNFLG
    eor ($C2),y
    bvs $3A8E
    rti
    .byte $80
    .byte $33
    .byte $82
    .byte $32
    ora $2D
    ora ($C0,x)
    .byte $43
    eor (ICBAHZ,x)
    .byte $02
    php
    .byte $53
    bpl $3ABD
    .byte $4B
    .byte $74
    bit $E4
    .byte $14
    clc
    pla
    php
    ror $48
    .byte $52
    asl TMPCHR,x
    .byte $D2
    .byte $02
    .byte $9C
    .byte $E2
    .byte $77
    and ($45,x)
    pha
    .byte $42
    eor (DINDEX),y
    .byte $1C
    rts
    ora #$C0
    cmp $C655,x
    .byte $EB
    beq $3AEC
    sed
    iny
    sbc $5950,y
    .byte $B3
    eor (STATUS),y
    sta $DD46,y
    plp
    .byte $9F
    cpx #$53
    beq $3A80
    .byte $BF
    cpy $A6F0
    .byte $89
    .byte $44
    .byte $BB
    eor $F988,y
    clv
    .byte $92
    .byte $FB
    ldy $C7FF,x
    sta $84
    sta $61,x
    rti
    jsr $5E5F
    sta $0111
    .byte $22
    cpx COLCRS
    ror $CC
    bpl $3AC7
    jsr $8134
    ora ($82),y
    .byte $2B
    cpx #$8C
    .byte $43
    .byte $53
    clc
    jsr $780E
    sec
    .byte $1A
    bvs $3A65
    cpy $FBFF
    bvs $3A69
    sbc RTCLOK,x
    .byte $13
    ora ($83,x)
    iny
    .byte $37
    bit $A1
    txa
    .byte $64
    .byte $5C
    ora ($44,x)
    .byte $34
    .byte $54
    .byte $13
    adc $A600,y
    adc $321C,x
    .byte $43
    ror XMTDON,x
    dec FREQ
    .byte $43
    .byte $F2
    .byte $0C
    ora ($CA),y
    jmp $CC45
    .byte $F2
    .byte $77
    ror DSKFMS
    .byte $DC
    brk
    lda (HOLDCH,x)
    .byte $53
    and $2D,x
    .byte $47
    and ($56,x)
    eor $C6
    .byte $42
    bmi $3B40
    bpl $3B96
    .byte $64
    .byte $54
    inc $0324,x
    .byte $02
    .byte $B7
    ora ($86,x)
    adc $90,x
    cmp ($65),y
    bne $3B74
    .byte $47
    lsr ICIDNO
    .byte $E2
    bpl $3B58
    .byte $32
    ror $65,x
    .byte $74
    .byte $22
    lda FKDEF
    .byte $5F
    .byte $54
    jmp $4455
    .byte $33
    rol $76BC
    brk
    .byte $B2
    ldy #$91
    sta SWPFLG
    .byte $72
    .byte $3F
    .byte $6B
    .byte $64
    lsr $5459,x
    bvc $3BA2
    eor #$FF
    lsr FMSZPG
    rti
    rol $393B,x
    .byte $37
    and $FF,x
    .byte $34
    .byte $32
    bmi $3B94
    rol $FF2C
    sbc ($9E,x)
    .byte $1C
    cpy $B4F0
    .byte $23
    .byte $F3
    cli
    jmp ($6420)
    .byte $9E
    .byte $04
    sty $17CF
    .byte $8F
    .byte $27
    asl $3C56,x
    .byte $04
    ora $1882,y
    eor (ICCOMT,x)
    jsr $9016
    ora $88,x
    .byte $14
    .byte $44
    .byte $13
    .byte $22
    bpl $3BA1
    sty BRKKEY
    and (POKMSK,x)
    .byte $83
    .byte $4F
    plp
    .byte $1C
    .byte $73
    .byte $80
    .byte $04
    ora $2804
    .byte $0C
    php
    bvs $3BAC
    bpl $3BF3
    asl
    php
    sec
    ora #$04
    bit WARMST
    .byte $02
    asl $80,x
    brk
    ora $B0
    txa
    brk
    .byte $07
    adc (TMPCHR,x)
    and LOGCOL,x
    cpx #$AA
    .byte $C7
    sta ($56,x)
    rol $AC0C,x
    .byte $7C
    .byte $04
    ldy.w KEYDEF
    .byte $02
    .byte $1A
    sta LOGCOL,x
    cpx #$6A
    .byte $C7
    .byte $80
    rti
    adc $84
    cmp (ICBALZ,x)
    adc (ICBAHZ,x)
    asl
    rol $14
    .byte $27
    plp
    .byte $72
    brk
    .byte $80
    .byte $DA
    rti
    .byte $07
    .byte $03
    .byte $73
    cmp $48
    sta (DSKFMS),y
    lsr $3000
    sbc $2410,x
    bvs $3BEF
    .byte $80
    .byte $80
    asl $EB8E
    bne $3BA7
    .byte $D7
    .byte $4F
    lda (ICBLLZ,x)
    ora ($C3),y
    bne $3BFB
    .byte $4F
    brk
    .byte $1B
    .byte $57
    ora LNFLG
    .byte $0F
    .byte $8F
    sec
    rol DOSINI,x
    cmp #$91
    and $A5
    cld
    ldx $A834,y
    brk
    .byte $D4
    .byte $C2
    .byte $83
    sty $8260
    .byte $02
    stx TRAMSZ
    ldy $D8CC
    bne $3BE1
    sbc #$74
    rti
    sbc $5B61,x
    .byte $43
    bcc $3BB8
    cli
    asl $C447,x
    bvs $3C42
    inc $8418
    .byte $3A
    .byte $9B
    .byte $D4
    eor ($C9),y
    .byte $54
    .byte $7B
    ror $4C00,x
    ora (SOUNDR),y
    ora ($98,x)
    rti
    .byte $7F
    adc #$EB
    .byte $0C
    .byte $5A
    lda $DC32,x
    .byte $A7
    .byte $C7
    .byte $97
    .byte $F4
    eor (CASINI,x)
    beq $3C09
    .byte $F3
    inx
    ror
    inc $E9
    .byte $F2
    sbc DIGRT
    .byte $E2
    sbc $B0,x
    .byte $F4
    .byte $EF
    inc $C0B8
    bvc $3C50
    ora $2208
    .byte $0F
    beq $3C2B
    .byte $FF
    .byte $82
    asl $45
    clc
    .byte $5B
    eor ($D0,x)
    bvs $3CB7
    .byte $6F
    .byte $27
    asl
    .byte $2F
    eor ($2D,x)
    .byte $C2
    .byte $0F
    beq $3C7A
    sta $D6
    and #$6C
    ror $8004,x
    .byte $33
    .byte $23
    .byte $6B
    .byte $2F
    .byte $32
    and FR1M
    rti
    ror $D071,x
    ldy #$7E
    asl CYCLES_PER_SCANLINE
    inx
    bvs $3CF9
    .byte $FB
    ldy #$0E
    .byte $73
    .byte $0F
    .byte $22
    ora (MEMORY_REFRESH_CYCLES,x)
    cmp $801E,x
    ror
    bmi $3CD2
    beq $3CD5
    .byte $83
    .byte $54
    .byte $63
    cpy $3736
    .byte $83
    sec
    txa
    .byte $3B
    lda $3D3C,y
    rol $403F,x
    eor (CRITIC,x)
    .byte $43
    .byte $FF
    .byte $44
    .byte $E7
    .byte $7C
    .byte $47
    pha
    eor #$4A
    .byte $4B
    jmp $4E4D
    .byte $FF
    .byte $4F
    sta HOLD1,x
    .byte $52
    and RMARGN,x
    .byte $54
    eor $56,x
    .byte $7A
    .byte $57
    cli
    cli
    bvc $3D2B
    .byte $5A
    .byte $5B
    jmp $5C5F
    eor $485E,x
    .byte $44
    .byte $5F
    rts
    .byte $47
    .byte $FF
    adc (PALNTS,x)
    .byte $E2
    .byte $64
    .byte $63
    sta $65
    ror $F6
    jsr $6967
    ror
    .byte $AB
    .byte $6B
    .byte $92
    jmp ($646D)
    ror $986F
    pla
    bvs $3D54
    and ($48),y
    .byte $53
    adc (CYCLES_PER_SCANLINE),y
    adc (ENDPT,x)
    eor $73
    adc $DF,x
    .byte $1B
    .byte $FC
    ora $7CC4
    asl $A8
    bpl $3D1D
    cmp ($13,x)
    eor $B4
    bcs $3D09
    sta (CASINI,x)
    .byte $3A
    .byte $1B
    .byte $1F
    .byte $07
    cmp #$00
    .byte $A3
    brk
    and ICPTLZ
    stx RTCLOK,y
    eor #$24
    .byte $82
    .byte $12
    .byte $0F
    ora DOSINI
    ora $F120
    .byte $22
    .byte $2F
    .byte $DF
    .byte $23
    plp
    rol $0258,x
    bit $CA2D
    bmi $3D7A
    .byte $32
    tsx
    .byte $34
    eor #$35
    .byte $33
    sta LTEMP
    rol $2FC1
    .byte $37
    and (BUFRFL),y
    bit $3F39
    .byte $3A
    cpy ICAX1Z
    and #$85
    .byte $2B
    .byte $F7
    .byte $7B
    eor (ABUFPT),y
    asl $497B
    .byte $7B
    ror $C561,x
    eor $0DD0,x
    .byte $A7
    .byte $42
    .byte $C7
    cpx #$03
    cld
    lsr FCHFLG
    .byte $AF
    sta (BUFRHI,x)
    inc $1D13,x
    ora (DIGRT,x)
    .byte $1A
    .byte $1C
    .byte $04
    .byte $87
    .byte $12
    ora #$C6
    eor $6D
    jmp $42A0
    ldx $AB3A,y
    lda $37
    adc $73E6,x
    adc (SAVMSC),y
    .byte $FF
    rol $9A
    cmp ICAX1Z,x
    eor $7302,x
    inc $0A7D
    .byte $D7
    inc HOLD1,x
    .byte $92
    asl $15B8
    jsr $6286
    lda $16
    ror $97E1,x
    ora $A6F6
    .byte $73
    lda (FR1),y
    sbc (SOUNDR),y
    .byte $FB
    jmp $430E
    .byte $9F
    .byte $4B
    .byte $AB
    lda (ICPTLZ),y
    .byte $4F
    cld
    .byte $4F
    .byte $89
    nop
    .byte $7B
    ldx $5A90
    cmp OLDCOL
    bit $A052
    dex
    eor #$80
    .byte $4F
    cmp $9176,x
    .byte $4F
    bpl $3D4B
    .byte $02
    adc BFENLO,x
    sbc CIX,x
    brk
    ldy $AE,x
    jsr $C3A0
    ora ($85,x)
    ror $8C,x
    cpx #$FA
    jmp $ED4E
    and TRAMSZ
    rol $A20D,x
    ora $A9ED,y
    .byte $22
    txa
    sed
    .byte $DC
    .byte $B2
    dec $B4
    lda $A74C
    .byte $42
    brk
    .byte $FF
    .byte $3F
    sta $9D5E,y
    sta ($BD),y
    iny
    .byte $92
    .byte $7A
    .byte $F2
    .byte $7B
    .byte $CF
    sbc $93,x
    .byte $5B
    lda BITMSK
    jsr $4368
    beq $3E64
    ldx #$10
    brk
    .byte $0F
    beq $3E6C
    bpl $3E0C
    .byte $80
    .byte $67
    inc $01FF,x
    .byte $02
    .byte $64
    stx $7F
    beq $3E76
    cmp (ICDNOZ,x)
    .byte $17
    sta (BUFSTR,x)
    ldy $1416,x
    .byte $CB
    ora $0B0C
    asl
    .byte $4F
    rol $2B2D
    rol
    plp
    and ICBALZ
    and ($FF,x)
    .byte $1F
    .byte $1C
    .byte $1B
    ora $F816,y
    .byte $12
    bpl $3EA6
    stx $08EF
    cpx ICAX1Z
    iny
    .byte $B3
    .byte $C7
    sed
    .byte $17
    .byte $04
    ora ICBLHZ,x
    and $724D,x
    sed
    jsr $5E7F
    sta $87
    bcs $3E10
    brk
    cpy VCOUNT_START
    .byte $44
    .byte $43
    .byte $80
    eor $84
    lsr $AE
    adc ($AD),y
    .byte $17
    .byte $73
    asl $A7,x
    ora INSDAT,x
    .byte $14
    ldy $AB13
    .byte $12
    .byte $BF
    .byte $A7
    ldy #$11
    lda #$D7
    tay
    eor $62A6,x
    .byte $A3
    lda ($C5,x)
    ora ($8B),y
    .byte $EB
    dey
    ldx $BA86
    sta $B5
    cpy $2004
    .byte $83
    dey
    rti
    .byte $82
    dey
    jsr $2181
    .byte $44
    ora ($CA),y
    and #$69
    .byte $DA
    .byte $04
    .byte $77
    ora ICIDNO
    .byte $07
    and $355F
    .byte $2B
    asl
    rol
    .byte $0B
    plp
    .byte $1F
    ora $0E27
    rol $0F
    bit POKMSK
    .byte $23
    .byte $FF
    ora (ICCOMZ),y
    .byte $13
    .byte $FA
    .byte $14
    inx
    inc $16,x
    tsx
    .byte $17
    cmp $1D20
    sbc $1527,x
    lsr $9A
    and #$76
    php
    sta $2E09,x
    .byte $0B
    .byte $D7
    and $2C0C
    ora $0E2B
    rol
    .byte $0F
    .byte $FF
    plp
    ora (ICPTHZ),y
    .byte $12
    rol $13
    bit $14
    .byte $FF
    .byte $23
    ora ICCOMZ,x
    .byte $17
    sbc $1918,x
    adc DSKUTL,x
    .byte $D7
    .byte $1B
    eor $1D,x
    .byte $62
    asl $2182,x
    and PALNTS
    plp
    brk
    .byte $0C
    .byte $0F
    ora (ICPTLZ,x)
    .byte $23
    asl $2102
    ora $2102
    .byte $0C
    .byte $04
    .byte $44
    .byte $02
    .byte $32
    .byte $9C
    .byte $FB
    bvs $3EEC
    cpx $88A0
    .byte $57
    ldy #$3E
    ora #$83
    .byte $12
    .byte $9B
    .byte $0B
    tya
    .byte $0C
    ora $0D
    ldy $040E,x
    cli
    dey
    sty $1083
    .byte $E2
    .byte $22
    ora (CASINI),y
    bmi $3EE4
    .byte $14
    .byte $62
    asl DSKFMS,x
    cmp NGFLAG
    .byte $1A
    .byte $D7
    php
    .byte $CF
    stx $CA
    adc $05C6,y
    .byte $2F
    .byte $C2
    inc $8200,x
    .byte $72
    and FMSZPG
    tay
    ora ($C2),y
    bpl $3F4C
    .byte $0B
    ora $4E71,y
    jsr $A4B8
    .byte $C3
    .byte $13
    php
    .byte $44
    rti
    lsr $A6
    .byte $07
    bvs $3F0E
    .byte $89
    tya
    dey
    asl $14,x
    lda $121F
    ora #$11
    .byte $47
    ora (PM_DMA_CYCLES),y
    sty CMCMD
    lsr RTCLOK
    .byte $F4
    php
    asl BUFADR
    .byte $5F
    .byte $14
    lsr APPMHI,x
    .byte $44
    sta (DOSVEC,x)
    iny
    .byte $C7
    .byte $04
    jsr LBPR1
    .byte $8B
    .byte $03
    ora #$06
    dey
    rti
    .byte $07
    eor #$08
    .byte $D4
    .byte $44
    .byte $12
    .byte $A3
    and $0D11,y
    .byte $82
    and (BRKKEY,x)
    .byte $03
    .byte $12
    ora ($13,x)
    .byte $17
    .byte $8B
    beq $3F8C
    .byte $07
    asl EEXP
    .byte $64
    sty $1D
    .byte $17
    ora CHKSNT
    .byte $03
    cmp $4DD1,y
    sed
    .byte $44
    .byte $0B
    .byte $13
    .byte $67
    ldy BUFADR
    asl
    asl MEMORY_REFRESH_CYCLES,x
    .byte $17
    .byte $5F
    clc
    dec $19,x
    .byte $02
    .byte $77
    .byte $1A
    ora #$07
    .byte $0F
    asl ZTEMP4
    ora $B3
    .byte $04
    tsx
    .byte $03
    sbc #$F4
    .byte $43
    lda ($14,x)
    .byte $2B
    .byte $13
    php
    .byte $12
    sei
    ora (CMCMD),y
    .byte $B3
    asl $DD
    ora $61
    .byte $32
    .byte $FC
    .byte $BB
    adc (NSIGN,x)
    .byte $3F
    lsr $0D84,x
    .byte $0F
    cmp DOSVEC
    inx
    rts
    pha
    .byte $AF
    .byte $72
    ora #$B9
    .byte $0C
    dec LNFLG,x
    .byte $13
    rol CASINI,x
    sty $045A
    .byte $32
    pla
    asl $E4
    .byte $CB
    tay
    ora #$D5
    .byte $0B
    lsr $94
    ora $466E
    adc #$D4
    dex
    ora $1300
    .byte $13
    ldx $BA12
    ora (POKMSK),y
    .byte $EB
    .byte $0F
    ldx $BA0E
    ora $EB0C
    .byte $0B
    ldx $BA0A
    ora #$08
    .byte $EB
    .byte $07
    ldx $BA06
    ora VCOUNT_START
    .byte $EB
    lda #$A2
    .byte $02
    ora ($C5,x)
    sty $C4
    .byte $03
    sbc $1E
    bne $405A
    tya
    brk
    adc $CD
    lsr $81BF
    .byte $DA
    inc $42C8,x
    ldy RTCLOK
    asl $A27C,x
    .byte $14
    ldx #$46
    .byte $42
    ror $E2BC
    .byte $80
    .byte $12
    .byte $47
    .byte $97
    ora #$0A
    tay
    jsr $2A08
    rol.w $008B,x
    plp
    .byte $14
    .byte $D3
    .byte $B2
    ldx #$22
    .byte $80
    nop
    .byte $02
    .byte $5A
    .byte $E2
    brk
    dey
    txa
    tya
    sec
    tax
    .byte $04
    jsr $04E2
    sta (DELTAC,x)
    .byte $F7
    adc DINDEX,x
    asl
    .byte $E2
    cpx #$01
    .byte $44
    jmp $8893
    .byte $12
    .byte $EB
    .byte $77
    cpy #$72
    asl $4157
    .byte $FF
    .byte $02
    inc $83,x
    .byte $5A
    asl $3168
    ldy #$F5
    asl $A8
    .byte $32
    .byte $83
    iny
    .byte $34
    .byte $87
    clc
    sta $56,x
    .byte $32
    lda OLDROW
    .byte $4F
    .byte $80
    lda #$6A
    .byte $02
    .byte $2F
    and #$68
    .byte $CB
    .byte $64
    .byte $DB
    eor (ICHIDZ),y
    asl
    ldy #$08
    adc $4BE2,x
    tya
    .byte $D2
    rol OLDROW
    .byte $4F
    lda ICBLHZ
    txs
    ldx $A602,y
    tay
    sbc ($AA,x)
    rol
    rts
    .byte $80
    ora $FD,x
    .byte $80
    beq $409B
    sbc ($9F),y
    cpx #$86
    txa
    pha
    ror ICAX1Z
    .byte $EB
    .byte $89
    ldy #$DB
    .byte $5F
    ldy $4224
    .byte $DF
    cmp ($94),y
    rol
    .byte $3F
    .byte $DF
    .byte $5F
    ror.w $0045
    .byte $3C
    sta $A37A,y
    .byte $07
    ora ($59,x)
    eor ($CC),y
    asl
    and #$20
    lda $B6BB,y
    .byte $43
    .byte $1A
    dex
    dey
    ldx ZTEMP4,y
    .byte $E3
    .byte $2F
    dec HOLD1,x
    plp
    .byte $2B
    .byte $12
    .byte $83
    lsr $CD25,x
    rol.w TOADR,x
    and $22FA,x
    lsr EEXP,x
    .byte $AF
    .byte $2B
    rti
    .byte $42
    tya
    .byte $5A
    ror $BAC3,x
    cpy $0A08
    .byte $C7
    ora #$C0
    bvs $4117
    asl HOLD1
    sty $C9,x
    .byte $EB
    .byte $77
    .byte $34
    and #$6D
    .byte $83
    sty $9EC4
    txa
    .byte $D7
    ora $8131
    .byte $F4
    rol $71,x
    dey
    cpy #$91
    .byte $C3
    sta ($99,x)
    lsr $08A8,x
    sta $B1A3
    eor $19
    lda $97F3,x
    sbc #$CF
    asl $88DE,x
    ora ($BE,x)
    sta CMCMD
    .byte $64
    .byte $3C
    .byte $5A
    .byte $5F
    .byte $54
    ora $B138
    plp
    sta $5883
    eor ($BE),y
    cmp $EB,x
    cmp ($80,x)
    .byte $5F
    asl
    sta $F3B9,x
    .byte $73
    cmp $B4D7
    sbc $C5
    .byte $2B
    sta (EEXP,x)
    sed
    cmp (ICCOMZ),y
    .byte $3F
    .byte $C3
    cli
    cmp (ICCOMZ),y
    rol PALNTS
    tsx
    lda (SOUNDR,x)
    eor $10A7,y
    .byte $89
    lda $85
    sta ($B8),y
    sta $61
    .byte $E7
    .byte $DB
    lda $0A67,x
    iny
    dey
    pla
    .byte $42
    ror BUFSTR
    ror $F714,x
    lda ($85,x)
    .byte $57
    .byte $EF
    inx
    .byte $13
    eor $6B81,y
    .byte $93
    .byte $54
    and #$20
    lda $99A5,x
    clv
    eor (LMARGN),y
    bmi $4151
    plp
    cmp ($DF,x)
    .byte $AF
    lda ($AB,x)
    .byte $4F
    sbc $BD,x
    ora $9035
    eor $6D,x
    .byte $93
    .byte $27
    adc ($C3,x)
    adc $81A5,y
    .byte $DB
    .byte $E7
    beq $4176
    sbc (ZTEMP3),y
    .byte $A7
    lda $8F9F,x
    sbc ($45,x)
    .byte $D7
    .byte $42
    eor $0239
    .byte $BB
    .byte $F7
    tya
    bvc $4196
    .byte $FF
    .byte $9E
    .byte $B2
    ora ($6D,x)
    .byte $8B
    .byte $AB
    .byte $C7
    ldy FR0M,x
    .byte $F4
    .byte $2F
    iny
    .byte $80
    asl
    iny
    ora $BF66,y
    ldy #$7F
    asl $C571
    bcc $4199
    .byte $2F
    beq $41EB
    jmp ($F817)
    bit $5322
    brk
    php
    .byte $2B
    cmp (BUFADR),y
    tay
    .byte $1C
    ldx #$B0
    .byte $1B
    sta COLRSH,x
    dex
    cpx $1433
    .byte $D3
    .byte $B2
    sta $5099,x
    .byte $57
    clc
    ror ICHIDZ,x
    .byte $E7
    .byte $5C
    sta $67B6,x
    sta $3508,y
    .byte $4F
    sei
    lsr $A9
    bcc $4183
    lsr $E78E,x
    .byte $7F
    .byte $67
    .byte $54
    .byte $02
    .byte $87
    tay
    sta (DIGRT),y
    eor $9D8F,x
    ror $902F,x
    .byte $82
    inx
    cpx #$21
    bne $41FB
    .byte $7C
    php
    .byte $89
    .byte $C7
    ldy $8A2E
    cli
    .byte $63
    rol $2718
    and $8606,x
    ldy $A547,x
    ldy $B1,x
    .byte $E7
    ldx #$F1
    asl $4E9D
    .byte $FF
    ror $317C,x
    dec $2245,x
    and $0511
    ror $44AF
    cmp ($99,x)
    lda $D1
    asl $F6
    .byte $DC
    .byte $EB
    dex
    .byte $A7
    sbc ($B7),y
    and ($E2),y
    .byte $FA
    .byte $42
    clc
    .byte $E7
    .byte $E7
    .byte $9E
    adc $1C99,x
    bvc $4273
    bvs $4204
    .byte $33
    sty $FAD6
    .byte $7A
    ora $2D
    .byte $AB
    ora $FB,x
    cmp (DOSINI,x)
    eor BUFSTR,x
    adc #$8E
    rol
    .byte $32
    cmp $A3,x
    ldy #$C3
    lda ($13,x)
    ldx #$12
    bcc $4209
    .byte $04
    .byte $EF
    .byte $3A
    lsr $2D85,x
    clc
    stx OLDROW,y
    .byte $A3
    cpy #$C6
    .byte $9F
    .byte $80
    plp
    tax
    and $B4,x
    and ($9E,x)
    sbc #$83
    ldy DELTAR,x
    adc #$2B
    and $0AD3
    .byte $2F
    jmp $3CA1
    rol $3EB2
    .byte $D7
    sta ($B1),y
    inc $73,x
    .byte $7A
    rol $5C32
    .byte $7B
    lda (FKDEF,x)
    .byte $7C
    and $D0
    sta ($8F,x)
    ora ($A3,x)
    .byte $12
    lda #$0D
    .byte $5B
    .byte $6F
    .byte $80
    .byte $C2
    .byte $FC
    inx
    rol $109E,x
    sbc DSTAT,x
    .byte $D3
    lsr
    jsr $7F81
    .byte $AF
    sta ($61),y
    .byte $A3
    .byte $DC
    sta $0D
    dey
    sta $B17F
    ldy #$47
    .byte $7B
    sta DSTAT,x
    .byte $43
    .byte $A7
    beq $4267
    sta $F972,x
    .byte $64
    .byte $FA
    eor $F8A4
    tay
    .byte $53
    cld
    sta $0EE7,x
    adc ($D7,x)
    inc $6836
    eor #$33
    ora $E34C,x
    .byte $4F
    .byte $EF
    lsr $FB
    .byte $92
    beq $428D
    .byte $97
    .byte $D4
    .byte $F4
    .byte $1C
    rol
    .byte $7A
    ora ($BE,x)
    sei
    sty $A0
    ora $99
    .byte $7B
    .byte $CB
    dec $81E9
    eor $B0
    ora #$FC
    and $B7B3
    cmp ICCOMZ
    .byte $5A
    .byte $74
    lsr
    .byte $23
    .byte $B3
    .byte $80
    ldy $80
    bcs $4291
    tax
    jsr $E859
    ora CIOCHR
    bmi $4365
    ldy SOUNDR
    and (CASINI),y
    .byte $AB
    ora #$16
    ora BRKKEY,x
    .byte $DB
    ldy #$B9
    .byte $07
    ora CHKSNT,x
    .byte $5B
    cpx #$10
    lda ICAX2Z,x
    cmp (MEMORY_REFRESH_CYCLES,x)
    ror LOGCOL,x
    .byte $32
    .byte $A3
    inx
    .byte $57
    ldy $B2D4,x
    .byte $3C
    .byte $3B
    sei
    eor $CA36
    tay
    rts
    .byte $3C
    and $440C,y
    sta $E807,y
    .byte $C3
    lda ($C8,x)
    .byte $3A
    cli
    ldy #$55
    .byte $8F
    .byte $CB
    .byte $E2
    .byte $87
    .byte $3C
    eor #$6B
    .byte $23
    .byte $13
    ldy TRAMSZ
    .byte $67
    jsr $666F
    .byte $BF
    dex
    .byte $8F
    ora ($83,x)
    ldy $0A8F,x
    .byte $82
    inx
    rol $F894
    bit RECVDN
    beq $4320
    lda $E3
    .byte $1F
    .byte $7C
    cmp CIOCHR
    .byte $4F
    cpy #$EC
    bvc $433D
    and #$F3
    sbc ($A0),y
    asl FRE
    sty $520F
    sta $B209,y
    sbc DSTAT
    beq $430A
    .byte $2B
    cpy ICAX2Z
    sec
    lda $E94E
    .byte $93
    and $01E4,y
    .byte $2F
    and ($E2,x)
    ora ($16),y
    rol DSKFMS,x
    .byte $EB
    bpl $4402
    .byte $37
    ora (DELTAC),y
    rti
    lsr $F1B4,x
    .byte $C7
    and $9F
    sta $E4
    .byte $5F
    sei
    adc #$02
    .byte $82
    .byte $D2
    .byte $07
    .byte $14
    ldy #$6B
    brk
    .byte $83
    eor $3EC5,x
    jmp $655B
    .byte $3F
    sed
    bpl $4405
    rol $8108,x
    sta $B004,y
    rol $9CCF,x
    tay
    inx
    .byte $33
    sta ($BE),y
    beq $4427
    .byte $3A
    .byte $E7
    .byte $9B
    .byte $83
    .byte $93
    inc $A431,x
    .byte $03
    asl $D84F
    asl BFENLO
    ora $56,x
    .byte $F3
    beq $43C3
    beq $43F2
    lda $47
    sed
    asl
    ror $E0CE,x
    bmi $43D3
    .byte $E7
    pha
    iny
    sty $E333
    .byte $6B
    .byte $1B
    .byte $C2
    rol $03
    sta $1BBE,y
    .byte $3F
    .byte $7F
    .byte $52
    .byte $62
    sed
    .byte $7A
    .byte $67
    bvc $439C
    rol APPMHI
    sta $244F,y
    .byte $4F
    sta $1FD7,y
    ldx #$81
    clv
    cli
    .byte $37
    bpl $4413
    .byte $47
    ror $1D60,x
    .byte $D2
    inc $13
    asl TMPCHR,x
    inc $0D80
    bne $43D7
    rti
    ora $16D0
    .byte $93
    rti
    and BUFSTR
    bmi $440E
    lda TMPCHR
    sbc ICPTHZ,x
    adc ($95),y
    .byte $0F
    .byte $A3
    ora $6893
    .byte $83
    cmp $B331
    .byte $04
    .byte $0C
    .byte $02
    and $93
    ora $C80A,x
    .byte $5C
    sta ($E9,x)
    .byte $DC
    ldx #$CC
    asl $590A,x
    nop
    .byte $04
    ldy $1E
    .byte $42
    lda (TMPCHR,x)
    .byte $02
    .byte $83
    txs
    sbc $82,x
    .byte $E2
    rol $7E27,x
    lsr $634D
    rti
    .byte $B2
    stx $A5
    clc
    eor #$86
    .byte $E7
    .byte $BB
    dec DSKUTL
    inx
    inx
    jmp $35F9
    bcs $4434
    clc
    .byte $E3
    eor $3A19,y
    dec $82B7
    ldx $0B,y
    .byte $0F
    .byte $9B
    ora ($1F,x)
    nop
    .byte $D4
    .byte $53
    bcs $4402
    sta ($84,x)
    .byte $82
    .byte $7B
    cmp $BC,x
    ldx $B2E6
    ldy $E87D
    .byte $02
    ldy #$91
    .byte $B7
    .byte $47
    rts
    sty $8EE4
    dec $658C
    .byte $B7
    asl $8E
    eor #$A3
    pla
    ror $FB,x
    beq $44A8
    dec $CEAB
    brk
    sty EEXP
    ldx $81
    ldy $82
    asl CHACT,x
    rol
    ora ($E8,x)
    ora ($85,x)
    lda #$0E
    .byte $A3
    sbc $5809,y
    bit $F071
    bmi $4532
    .byte $07
    php
    lda (ZTEMP3,x)
    asl DOSINI,x
    .byte $32
    sbc #$23
    rti
    .byte $DF
    ora $C407,x
    .byte $0B
    .byte $6B
    lsr $C5,x
    .byte $17
    ora $E96C
    .byte $B2
    .byte $FB
    .byte $EB
    .byte $04
    .byte $C2
    .byte $32
    rol
    cld
    sec
    .byte $3C
    lsr SOUNDR
    ora $1B74,x
    and $A7DA,y
    tax
    ora #$6A
    beq $4552
    sbc $438E,x
    sbc LTEMP,x
    .byte $0F
    .byte $D2
    .byte $37
    ora $64D9
    asl EEXP
    lda #$94
    asl $F13A,x
    ora ($B4),y
    lda $3BF8
    lda ROWAC,x
    ora (ICBAHZ,x)
    dey
    ora #$28
    .byte $F7
    eor #$20
    .byte $F2
    cmp $52A8,x
    asl
    .byte $3A
    .byte $0B
    pla
    ldx $D1
    and $A1,x
    .byte $47
    jsr $A338
    bcc $4567
    eor ($D0),y
    beq $44C2
    bcc $44CD
    eor ($D0),y
    sbc $A8A3,y
    cli
    asl
    .byte $3A
    inc $14,x
    .byte $72
    ldy #$0E
    plp
    .byte $F3
    eor ($C4),y
    .byte $02
    .byte $3A
    .byte $B3
    and ($73),y
    lsr $140A,x
    .byte $82
    and ($BE),y
    sbc ($A3,x)
    .byte $42
    .byte $72
    .byte $D4
    .byte $07
    and $A07E,y
    .byte $34
    .byte $3B
    brk
    ldx $53EF
    sty RTCLOK,x
    inc $9DC4
    .byte $03
    .byte $43
    .byte $FA
    pha
    ora $69,x
    sta $80
    rti
    .byte $77
    .byte $43
    lsr $1DF5,x
    sbc ENDPT,x
    clv
    ora $AD02,x
    adc $F4,x
    .byte $B2
    .byte $EB
    cmp $E3,x
    .byte $44
    sta $80
    adc (COUNTR),y
    .byte $F3
    .byte $D7
    asl OLDCHR,x
    sty FKDEF
    adc #$2A
    adc $D687,y
    rol $DE
    lsr $5D02
    dec ATRACT
    .byte $1B
    adc $7D62,x
    adc $8A,x
    .byte $74
    cpy $054E
    lsr $4F20,x
    .byte $2F
    brk
    cpy $CB
    ldx $ADC3
    cpx $93
    bvc $45A4
    rol $E983
    lda (FCHFLG),y
    and $EC92,y
    ora $21B9,x
    rti
    and $8FEE,y
    .byte $1F
    cmp COUNTR
    .byte $9F
    .byte $EF
    .byte $5C
    sta $5AD8
    sty ZCHAIN,x
    cmp $95B4,y
    sta FRE,x
    adc #$96
    rol
    .byte $DB
    ldx $97
    lda $079B
    .byte $5C
    adc ICBALZ,x
    asl $4186,x
    clc
    lsr FKDEF
    pha
    inc $6D3A
    .byte $82
    .byte $EF
    .byte $AB
    .byte $8F
    clv
    bne $4587
    nop
    sbc ($A3,x)
    adc $38E6,x
    .byte $B3
    bcs $4633
    and $87DF,x
    sbc $B150
    sta $ABDE,x
    ldx $5F17,y
    rts
    txa
    ora $AA,x
    cmp $D2,x
    .byte $1C
    .byte $D7
    eor LMARGN,x
    .byte $DF
    .byte $5C
    dec $BE,x
    ldy #$4F
    sei
    .byte $5F
    nop
    .byte $57
    .byte $54
    sta $AFBF,y
    clc
    sta FLPTR,x
    adc (ABUFPT),y
    cli
    tay
    inc $A83E
    beq $464B
    cmp $15E1,y
    .byte $E3
    lsr $E2,x
    .byte $8B
    cpx $BB
    bvc $45D1
    dey
    sta $2EB3,y
    bne $4699
    .byte $FA
    sbc #$79
    cld
    sbc (VCOUNT_START),y
    and $86EA,x
    .byte $5B
    .byte $32
    cmp $2045,x
    adc $13,x
    lda $6077,y
    tax
    .byte $FB
    .byte $87
    sta $47
    rti
    lda #$BB
    tya
    cmp $C625,x
    sta $A36C,y
    dec $5A08,x
    .byte $80
    .byte $D3
    sty $46F3
    .byte $BB
    .byte $47
    .byte $34
    tax
    .byte $6B
    .byte $AB
    .byte $34
    iny
    .byte $6B
    .byte $A3
    .byte $34
    sta $B354
    inc LNFLG
    cpy $9023
    .byte $14
    stx $5063
    lda $5987
    bit $A501
    lda $0E03,y
    and #$10
    .byte $3A
    dec $78,x
    ora $7E59,y
    cmp $A4,x
    and $0B4E
    and $DE
    txa
    bne $46CE
    asl $C9AA
    lsr $5D61
    .byte $3C
    and INBUFF
    sta ICCOMT
    rol $14
    lsr $DE,x
    ldx $AD,y
    sta $92CF,x
    .byte $AF
    .byte $32
    cmp $DAE5,x
    .byte $D2
    sbc FKDEF
    ldy $CF
    beq $46A8
    .byte $1F
    ldy $059D,x
    .byte $27
    .byte $FB
    .byte $CF
    ldx TMPCHR,y
    cld
    .byte $EB
    .byte $EF
    .byte $9F
    sta (BRKKEY,x)
    cmp ($E2,x)
    cpx FKDEF
    .byte $DC
    bmi $46B5
    ror $7038
    rol $1F,x
    adc #$43
    .byte $FB
    asl $9A,x
    .byte $34
    and BUFRLO
    ora #$E3
    asl $7A51,x
    .byte $64
    adc $93,x
    .byte $1C
    sta $DF
    .byte $FA
    .byte $A3
    cmp $231A
    lda $E0EB,x
    .byte $47
    .byte $1A
    .byte $57
    asl $4124
    .byte $92
    pha
    cpx $E8
    .byte $CF
    lda $B8
    asl EEXP
    .byte $02
    lsr FR1M
    sta FR1
    jmp $706F
    eor (DSKFMS),y
    sbc $4A65,y
    ror
    bmi $471A
    .byte $EF
    stx $D0,y
    bne $4746
    .byte $FA
    ora DOSVEC
    .byte $9C
    .byte $64
    .byte $0C
    clv
    .byte $E3
    cmp ($FA),y
    sta $4162
    stx ICBLLZ,y
    sbc (DSTAT,x)
    adc $F051
    ldx $19
    cpx #$FF
    .byte $DB
    ldy $E8E3,x
    .byte $14
    rol
    .byte $32
    .byte $14
    sta $B0A6
    dex
    .byte $D4
    cpx $EA
    adc $57DA
    .byte $E3
    .byte $DB
    dey
    cpx $3EA1
    rol $AEE2
    adc $DFAE,x
    cmp $65,x
    bcc $4737
    cpy $4C64
    rol
    eor ($A4),y
    cld
    iny
    .byte $FC
    pha
    bcs $4778
    cpy #$08
    .byte $BF
    .byte $33
    .byte $80
    .byte $07
    cpy $4114
    sta $57C0,y
    ror $C0
    rol
    .byte $43
    .byte $74
    .byte $2B
    adc ($85),y
    .byte $E3
    .byte $72
    ldx #$E7
    lda $3B46,x
    .byte $A7
    adc $90B3,y
    sta $F14C,y
    .byte $FF
    .byte $4F
    rts
    lda $0EFF
    .byte $32
    sta $75
    txs
    eor #$80
    cmp $A3
    bne $4773
    ror $CD75,x
    .byte $9B
    .byte $97
    bcs $47AE
    .byte $33
    adc $D578,y
    stx POKMSK,y
    eor #$6C
    inx
    and $A0B3
    bmi $47AA
    .byte $FB
    ldx $34F6
    .byte $1B
    .byte $1F
    and (OLDCOL),y
    sec
    bcc $478F
    .byte $CB
    .byte $EB
    jmp $CC43
    .byte $D4
    .byte $72
    adc $32E3,x
    .byte $DC
    .byte $13
    .byte $1C
    .byte $DC
    .byte $89
    .byte $8B
    sbc $DB
    eor ($8E),y
    adc $E4D0
    .byte $CF
    rol PALNTS,x
    .byte $8B
    dec $E8E5,x
    beq $47D7
    dec $C7
    .byte $93
    bmi $47D7
    jsr $531B
    .byte $FC
    and $6FC5,x
    adc #$D4
    rol
    sta $D5D2
    .byte $AB
    eor $A4,x
    dec $C9,x
    tax
    lda $9DD7
    .byte $E7
    ldx $DFE8
    cpx #$0F
    cld
    and $8C32
    .byte $CB
    tya
    cli
    .byte $BB
    lda #$5E
    .byte $1B
    and CIOCHR,x
    bpl $4838
    .byte $1C
    .byte $43
    ora $D37A,x
    .byte $13
    bcs $47FA
    .byte $07
    .byte $4B
    bmi $4815
    bne $4833
    .byte $47
    cmp ($A2),y
    bvc $47E6
    sbc $CAC4,x
    lda #$4C
    sbc $A868,y
    bmi $4805
    plp
    asl $73
    .byte $14
    bvs $47C3
    and (HOLD1,x)
    and $0F
    ldy LOADAD
    inc $84
    ldy $4214,x
    tsx
    tya
    .byte $DA
    sty $0FC3
    sbc #$22
    sbc $5E24,y
    .byte $37
    .byte $52
    and $540E,x
    rti
    .byte $F2
    tya
    and ($A1,x)
    asl
    bmi $4829
    .byte $B7
    .byte $0C
    sei
    .byte $0F
    .byte $2F
    .byte $0C
    .byte $3F
    sty $A1BA
    sbc #$B0
    .byte $0B
    plp
    cpy ICHIDZ
    .byte $73
    .byte $53
    cpx #$00
    tsx
    ldy $72DC,x
    dec $B08A
    .byte $62
    .byte $CF
    tax
    txs
    bne $47D0
    ldy $D162
    lda $34BC,x
    .byte $D2
    dex
    jmp $5228
    .byte $7F
    .byte $33
    sec
    dec $8D6E
    and $2E27
    .byte $CF
    ldx $AC8C
    .byte $D4
    eor (TOADR,x)
    cpx #$C8
    .byte $57
    asl $AA
    sty $D1F5
    ora $C8,x
    cmp $CC,x
    .byte $C2
    beq $4846
    ror FR0M
    sbc $7D86,y
    inc $AE
    .byte $3C
    .byte $D7
    cpx $BC24
    .byte $13
    clv
    lsr
    cpy #$1A
    sty $90
    jmp ICAX1
    .byte $D3
    adc (DOSINI,x)
    ora $D414
    sbc $CF
    .byte $FA
    .byte $9C
    ror OLDCHR
    bvc $485E
    .byte $5C
    and (FR0M),y
    tax
    cmp $A465,y
    sta $D6
    rts
    stx QTEMP
    .byte $7F
    lda $2DBA,x
    .byte $3A
    beq $48BF
    cmp $CE
    cpx $D034
    .byte $33
    .byte $77
    ldx $AB
    .byte $7C
    tsx
    sty $C04B
    ora (HOLD1,x)
    ldy #$88
    .byte $87
    dec $0439
    tax
    .byte $23
    .byte $E2
    tya
    clc
    iny
    cpy $DFC5
    .byte $3B
    bit $BD52
    dec $0E9B
    cmp (FPTR2,x)
    bmi $48DD
    .byte $E7
    adc (LNFLG),y
    .byte $92
    asl $8F19
    cpy $E8
    ora (RTCLOK,x)
    lda $0DB0,y
    sta $5000,x
    clv
    txa
    .byte $23
    bit $C0
    brk
    bcs $4903
    asl $D092,x
    .byte $07
    dec $C092,x
    rol $1399,x
    sec
    .byte $14
    .byte $89
    cpx #$18
    rol $92,x
    cpy #$D1
    .byte $53
    adc (TRAMSZ,x)
    cpx FR1
    and ($8A),y
    sta $86,x
    dec $F9F7
    lsr $5C
    cmp #$FE
    .byte $73
    sbc $83,x
    adc $D0
    ror
    .byte $47
    .byte $03
    .byte $3C
    brk
    inx
    .byte $42
    .byte $D4
    .byte $80
    ora #$04
    bit $A640
    cmp $7D60,y
    and $BAB9,x
    lda $D04C,x
    sbc #$6D
    inx
    cmp $4235,x
    .byte $22
    ldx $5032,y
    .byte $5F
    and ($84),y
    .byte $A3
    ldy $9DF1,x
    .byte $9F
    .byte $B3
    beq $497D
    tya
    adc #$E5
    .byte $89
    jsr $2F32
    .byte $2F
    bpl $492E
    adc $4FD0,y
    ldx $A471,y
    .byte $A3
    dey
    bmi $4919
    inx
    .byte $7F
    lda $85B6,y
    sta $DC,x
    txa
    .byte $A3
    bcs $4914
    .byte $64
    rol $D293,x
    sec
    lda ($C4),y
    cmp $C5,x
    ora $0E30,y
    .byte $9B
    sei
    .byte $80
    eor #$B0
    .byte $1C
    .byte $03
    .byte $44
    ldy #$17
    .byte $A3
    lda #$11
    cmp (RTCLOK,x)
    lda $07B1,y
    inc $F3CE,x
    and $62F1,y
    lsr $79D0
    tsx
    adc $656A,x
    .byte $A3
    ldx $7004,y
    sta $00BB,y
    iny
    jmp $5495
    .byte $FF
    .byte $12
    .byte $17
    bpl $4986
    rti
    cmp ($A0,x)
    .byte $63
    .byte $F4
    .byte $23
    cmp $FAC7
    .byte $13
    eor $57C4
    .byte $9B
    lda $E8CE
    dex
    sed
    cpy $6A7A
    .byte $17
    .byte $92
    bcs $49A3
    rti
    sta FCHFLG
    asl $9C,x
    cpx $D36A
    sbc $7384
    sbc $DC,x
    bne $49CD
    .byte $6F
    .byte $72
    inc $CC05,x
    jmp $55F5
    lda $9F05
    ror $97,x
    .byte $D7
    .byte $E2
    .byte $92
    .byte $EB
    .byte $B7
    ora (ROWAC,x)
    eor ($71),y
    sty FKDEF,x
    jsr $2F58
    .byte $57
    ldx $E7E9
    ora ($95),y
    ldy $BD
    ora $5915,x
    inc $90
    ldx #$88
    sed
    .byte $12
    .byte $8F
    sbc ($8C),y
    .byte $E2
    .byte $22
    .byte $B3
    .byte $D2
    beq $49B9
    .byte $2B
    bne $4A23
    rti
    sta ($CC),y
    ldx CASINI,y
    ldy $03
    .byte $33
    and $1C63
    ldy $1F,x
    .byte $27
    inc $0AFA
    bvc $49A0
    .byte $D2
    sta SOUNDR,x
    .byte $F3
    rts
    sbc $41E6,x
    .byte $FB
    .byte $F4
    asl
    asl $8CCA,x
    .byte $12
    rts
    .byte $14
    .byte $80
    rts
    tay
    sec
    ldy #$CD
    ora ICHIDZ
    sbc $85
    jmp $56A4
    .byte $47
    rol $A3
    bpl $4A4A
    inx
    sbc $2D53,x
    stx $5F,y
    sty $3D06
    .byte $73
    ldx #$2E
    .byte $32
    sty FR0,x
    .byte $42
    .byte $63
    adc $95
    and $C2AD
    .byte $F4
    pha
    .byte $72
    ldy $EA,x
    lda ($E4),y
    .byte $2B
    .byte $13
    cmp (FRMADR),y
    ora $8C
    .byte $87
    ldx $D33C,y
    sta $AD4E,y
    sec
    sbc $EB9F
    lda ($90),y
    .byte $0F
    .byte $FA
    .byte $9C
    jmp $56E1
    .byte $07
    bcc $4AA8
    .byte $12
    .byte $FC
    eor #$F0
    .byte $1B
    asl $1C56,x
    lda (NSIGN),y
    sta $8A,x
    ror
    inc $B3AB,x
    lsr $C62A
    .byte $5A
    adc (FREQ),y
    and ($B2),y
    eor $91A5,y
    .byte $B3
    sed
    bmi $4A11
    .byte $22
    ldy $2A01,x
    .byte $AF
    bne $4AC2
    lsr DOSINI,x
    ror $26F9,x
    lda $108F,y
    .byte $1F
    .byte $53
    .byte $42
    ror DOSINI,x
    sta $8D
    .byte $DB
    stx $C55E
    and ($90,x)
    .byte $3F
    .byte $97
    cpx #$5B
    cpx $C8
    .byte $1B
    .byte $62
    rol $0D11,x
    ldx $94DA,y
    .byte $12
    bne $4A9A
    lda $D22E,y
    cmp $BCFE,y
    asl $20AB
    ora $99E3,y
    .byte $89
    cmp $78,x
    asl $C697
    .byte $FB
    .byte $9F
    clc
    lsr FKDEF,x
    ora $812D,x
    adc $80,x
    .byte $3A
    .byte $02
    cpx #$2E
    lda (QTEMP,x)
    stx $BB,y
    bcs $4B28
    .byte $B2
    ora (LTEMP),y
    rol $A3,x
    sta $9197
    cmp $1D22,y
    bcc $4AC1
    .byte $3C
    .byte $82
    sbc #$94
    .byte $97
    txs
    rol $5995
    .byte $9B
    cmp $0520,x
    .byte $32
    .byte $5B
    inc $6798
    eor $99A5,y
    .byte $23
    adc $E4
    sta ($9A,x)
    .byte $C2
    and $56B2
    .byte $0B
    ora ($B3),y
    rts
    .byte $2B
    pla
    .byte $E7
    jsr $35B9
    plp
    .byte $E7
    cpx #$11
    and $E120,y
    .byte $44
    .byte $E7
    clc
    cmp ($A5,x)
    and ($FB,x)
    adc #$04
    tax
    and #$4F
    .byte $07
    ldy $EA,x
    .byte $32
    .byte $0F
    ora $C6
    ora $A5BA,y
    lda $1EE7,y
    sbc (BRKKEY),y
    .byte $BB
    ldx #$44
    jsr $580C
    .byte $9F
    cpx #$E4
    bne $4B35
    rts
    ldy $BA
    .byte $FF
    ldx #$B5
    .byte $3A
    jsr $43FA
    .byte $9E
    and $98,x
    ldx SHFAMT,y
    lsr $BA85,x
    and ($B4),y
    cld
    .byte $B7
    bcs $4AF5
    .byte $5F
    .byte $0F
    ldy $B8
    dec $73
    .byte $3B
    .byte $02
    .byte $72
    brk
    eor ($9C),y
    .byte $80
    .byte $67
    .byte $27
    jsr $097D
    iny
    .byte $93
    .byte $02
    .byte $72
    brk
    lda #$9C
    .byte $80
    .byte $BF
    .byte $27
    jsr $09D5
    iny
    .byte $EB
    cli
    .byte $03
    and $C8E8,y
    .byte $83
    ldx ICPTLZ,y
    .byte $8B
    lda $B9,x
    .byte $D7
    rol $90,x
    ora MEMORY_REFRESH_CYCLES
    cpy #$4C
    ora ($7F,x)
    eor $7F29,y
    ror
    sbc (OLDCOL),y
    rol $158A,x
    ldy DSKFMS
    tya
    .byte $9B
    adc #$07
    sta $B8
    rts
    tax
    .byte $D7
    adc $50FF,x
    sty $5F
    ror $A4,x
    ora #$10
    sta $AE3E,x
    .byte $FC
    lda (POKMSK),y
    ora #$D2
    cli
    ldy #$89
    jmp $594B
    .byte $0F
    bit $612E
    stx BUFRFL,y
    .byte $0F
    eor #$62
    rts
    .byte $03
    .byte $5A
    and $8C
    .byte $64
    rts
    and $8C
    ldx $D15E
    .byte $33
    .byte $1C
    bne $4B63
    .byte $27
    brk
    asl $5832
    cpx #$7E
    stx BUFRFL,y
    asl FKDEF
    stx ICBLLZ,y
    ror $C732,x
    brk
    jmp $53FC
    eor $18A0,y
    ldx $5EDD
    lda $CBAC,y
    ora (LNFLG),y
    sbc $4C2C,x
    .byte $03
    ora $56C5
    clv
    stx $9ED2
    ldx $2A55,y
    .byte $9F
    bvs $4BCD
    asl
    inc $BD,x
    tya
    bcc $4C4F
    rol OLDROW
    sec
    .byte $FB
    inc $AC11
    ldy $D6
    sbc $8513,x
    .byte $D3
    .byte $14
    adc $3061
    .byte $34
    beq $4BF5
    lda ICBLLZ,x
    eor (CMCMD,x)
    tya
    iny
    .byte $5B
    txs
    sbc #$E2
    dec $0D,x
    sta FREQ
    .byte $5C
    sty $C002
    lda $4D68,y
    sec
    bne $4C15
    sty $8A
    .byte $9B
    .byte $6B
    ldy $93
    .byte $1C
    bmi $4C80
    .byte $DB
    cpy $2A06
    sbc $235C
    sbc (ROWCRS,x)
    eor ($78),y
    lda $97
    jsr $9876
    .byte $BF
    ldx $88
    .byte $B3
    ror BUFRHI
    sta $A7
    stx $B1A1
    .byte $34
    clc
    adc $C6
    lda $A3
    .byte $43
    jmp ($8018)
    sta (BUFRFL,x)
    ldx #$8D
    sbc DSKUTL
    stx $A3
    .byte $27
    pla
    .byte $D7
    .byte $5A
    lda #$72
    ldy $BD,x
    asl $0F32,x
    ora $09A9,y
    sta ($19,x)
    bne $4C87
    lda $0F
    .byte $92
    stx $65
    ldx #$6A
    lsr
    sbc (BPTR),y
    rol $1080,x
    bvc $4C28
    cmp $D032
    .byte $37
    .byte $DC
    and $2915
    .byte $7A
    asl $12A8
    sty.w FKDEF
    .byte $23
    bmi $4CB0
    adc $2671,x
    cmp $3BBD,x
    adc #$49
    .byte $FF
    sta $AE0E,x
    jmp $5B1C
    inc $37DE,x
    sbc #$92
    cmp BFENHI,x
    sbc $30AB,x
    .byte $E2
    .byte $6F
    bpl $4C83
    .byte $F2
    cpx ICAX1Z
    lda $83E2,x
    adc RAMTOP
    .byte $3F
    .byte $AF
    sta $357E,x
    bcc $4CC7
    inc $ADAE,x
    rts
    .byte $7F
    .byte $9C
    lda $0652
    .byte $42
    rti
    sty TMPCHR
    .byte $1A
    .byte $93
    .byte $8F
    .byte $0C
    ora LTEMP,x
    adc #$08
    clc
    .byte $D2
    .byte $64
    rol ADRESS,x
    .byte $80
    brk
    ror $9C
    bcc $4CF1
    clc
    .byte $7F
    txs
    eor $A56C,x
    .byte $9B
    adc $8F
    .byte $B2
    lsr CIOCHR
    ror $BD
    .byte $C2
    .byte $7C
    rol $CE,x
    php
    tya
    lda $0D,x
    tya
    sta $99
    .byte $8B
    rts
    ldy $84
    .byte $9E
    cmp $9FA2,y
    .byte $DB
    .byte $F4
    lda $16,x
    cli
    brk
    asl VCOUNT_START
    rol DELTAC
    bmi $4D3E
    bit NSIGN
    bvc $4D25
    sec
    cmp $9F43,x
    .byte $5C
    tay
    cmp ($B9),y
    .byte $FA
    bcc $4D24
    sty $E7
    ldy #$98
    rol $B9
    ldx #$60
    dec $E4,x
    ldy CIX
    .byte $80
    cld
    ldy $41D9,x
    clv
    rol ICBLHZ,x
    adc #$40
    and $87,x
    jsr $5D1D
    .byte $4F
    tay
    sta $A9DA,y
    .byte $52
    stx $DB
    .byte $6B
    ldx $C2
    clv
    .byte $72
    lda $6ADC
    bpl $4D57
    rol $8274
    .byte $0F
    ldx $39DD
    .byte $DA
    sta $A8
    sbc $BD
    sta (OLDCHR,x)
    bne $4D6F
    sbc $6854,x
    .byte $12
    lda $440A,y
    jmp $5D65
    asl $13ED,x
    eor (CRITIC,x)
    .byte $9F
    .byte $9C
    sbc $3747,y
    eor ($AA),y
    .byte $9F
    and ($BC),y
    .byte $EF
    .byte $8F
    asl $010E,x
    dec $DFC4,x
    rol $E0A5
    .byte $C7
    eor VBI_SENTINEL
    .byte $0B
    brk
    ora #$3F
    rol $AD,x
    ora (ICHIDZ,x)
    tya
    ldx $B1
    .byte $A3
    jmp ($A7C2)
    .byte $2F
    eor ($96),y
    php
    stx FKDEF
    plp
    .byte $89
    ror $45
    .byte $33
    sbc #$22
    lda ($A8),y
    .byte $63
    eor #$69
    sty OLDROW
    nop
    bpl $4DB8
    bvs $4DB3
    .byte $03
    lda EEXP
    .byte $F4
    rol $3E99
    txs
    ora #$04
    .byte $93
    cmp $D3
    bcc $4DCC
    ldx $A66C
    sbc $9E3F
    eor $E5A6
    and ($69),y
    stx $AD
    rol $E9
    lda (ZCHAIN,x)
    .byte $27
    .byte $53
    ldy #$20
    .byte $1C
    eor $8AAB,y
    adc OLDADR
    adc #$AC
    .byte $DC
    .byte $9E
    lda $4F0B,y
    .byte $9F
    bne $4DE8
    .byte $73
    .byte $43
    .byte $0B
    jmp $7709
    ldy #$DC
    .byte $5C
    clc
    lda $EB
    adc #$02
    ror
    adc $D3AC,x
    .byte $C7
    ldx #$5E
    bmi $4D88
    asl $A6
    rti
    rol ICCOMZ,x
    .byte $0C
    ldx #$80
    .byte $43
    .byte $12
    .byte $13
    .byte $A7
    .byte $B3
    .byte $07
    lda $C296,y
    and $97
    and $2813
    .byte $B3
    eor $0C60
    stx $AA,y
    sbc $B1E4
    .byte $7C
    asl ICIDNO,x
    .byte $93
    .byte $0B
    adc (BUFRFL,x)
    ror
    .byte $3F
    ora #$11
    jmp $5E5F
    inc $3265
    lda (DSTAT),y
    .byte $7F
    adc ($A3),y
    rol $AD,x
    .byte $1C
    .byte $AB
    .byte $AB
    ldy $2223
    .byte $14
    lsr
    bne $4DD3
    ror $E8
    jmp $5E7C
    .byte $DC
    .byte $74
    and $0E37,x
    bpl $4E4A
    .byte $DC
    .byte $04
    .byte $F4
    ora (ZCHAIN,x)
    .byte $47
    sec
    lda DRKMSK
    sbc $AE
    ror $A7,x
    .byte $37
    tax
    lda $3E0F,x
    adc $AF,x
    .byte $6F
    stx $83,y
    lsr $0D
    ldx $C841
    .byte $5B
    eor ($FF,x)
    .byte $A7
    ldy #$09
    .byte $3A
    php
    .byte $62
    bpl $4EE0
    .byte $0B
    cpy DOSINI
    plp
    .byte $44
    .byte $2F
    bit FR2
    bpl $4E97
    ora (CRITIC),y
    .byte $12
    .byte $13
    sta $14
    asl
    ora $14,x
    asl ICBLLZ,x
    .byte $17
    bvc $4E9C
    ldy #$8C
    .byte $B3
    brk
    .byte $1B
    bcc $4EA7
    ldy #$1D
    bcc $4EAD
    pha
    .byte $1F
    bit ICDNOZ
    tya
    cpy BUFADR
    ora $8D
    .byte $80
    .byte $72
    .byte $23
    bmi $4F1B
    dec DSKUTL
    ora $674A,y
    .byte $62
    ldx DSKFMS
    bvc $4EB8
    cmp (FMSZPG),y
    lsr $1B
    .byte $80
    ldy #$FF
    .byte $C7
    ora $73,x
    ora $4112,x
    ora $0C21,y
    .byte $D7
    stx ICSTAZ
    .byte $42
    txs
    asl $0A09,x
    bcc $4EC8
    .byte $1C
    .byte $37
    jmp ($4C21)
    lsr $5F
    lda $DF
    and $71BE,y
    bmi $4EC0
    ora $C0
    eor NGFLAG,x
    sbc ($A8),y
    sty BFENHI
    sbc $2085,x
    .byte $E2
    ora (ICBALZ,x)
    .byte $02
    jmp ($5815)
    cpy #$38
    jmp ($7600)
    bvc $4EEC
    ldy ABUFPT
    .byte $80
    cli
    rts
    bpl $4EA5
    .byte $04
    rol $0F,x
    cmp (FRX,x)
    brk
    rti
    brk
    lsr
    .byte $52
    asl $2C40
    ldy #$0C
    .byte $1B
    .byte $02
    .byte $9B
    rts
    .byte $04
    bpl $4F0C
    adc FKDEF
    plp
    ldy ABUFPT
    .byte $80
    cli
    cpy #$10
    bpl $4F51
    .byte $04
    rti
    bvc $4F2C
    jsr $4180
    jsr $0003
    .byte $17
    lda $651C
    bvs $4F23
    .byte $34
    plp
    .byte $FA
    ldy $6E81
    .byte $0B
    asl
    .byte $8F
    php
    .byte $07
    .byte $B3
    asl $1B
    nop
    cpy $2A2B
    and #$07
    plp
    sty $C645
    sbc #$DB
    eor $1C1B,x
    .byte $47
    lsr TMPCHR,x
    brk
    asl $6E4A
    .byte $7C
    ora $2303,y
    .byte $02
    ora (CYCLES_PER_SCANLINE,x)
    .byte $C3
    adc ($0B),y
    lda $49,x
    .byte $04
    .byte $03
    .byte $97
    dec $E9
    asl
    ora #$DA
    .byte $92
    .byte $1A
    ora $180B,y
    inc $14BD,x
    .byte $13
    .byte $BB
    ora $0F49,y
    ora $0B
    .byte $0B
    .byte $27
    .byte $8F
    .byte $EB
    dec $7061,x
    .byte $32
    .byte $92
    ora $0B1E,x
    .byte $1F
    inc $FD22
    .byte $23
    bit ICBAHZ
    rol ICPTHZ
    inc $B01F,x
    bit $56C3
    ldy $D530,x
    lda $1A86,x
    bcs $4F48
    .byte $C3
    lsr $BF,x
    .byte $1A
    sty ATRACT,x
    sbc $95B8
    adc BUFCNT
    .byte $02
    .byte $D7
    bpl $4FDE
    .byte $34
    rol $AD8C
    cmp ($A6,x)
    ldy $80,x
    stx $C4,y
    cmp (DSKUTL),y
    stx ATRACT,y
    cpx $A5
    .byte $97
    ldy #$A7
    .byte $04
    .byte $DC
    .byte $82
    .byte $D7
    bpl $4FF7
    and DOSVEC,x
    .byte $32
    .byte $9F
    .byte $9F
    ldy #$02
    lsr $D84E
    ror $C99C
    bvs $4F79
    .byte $D3
    .byte $DB
    adc $83,x
    ora $81ED,x
    .byte $C7
    .byte $42
    .byte $EB
    asl CHKSNT
    adc $8E03
    cpx #$D4
    .byte $32
    .byte $07
    asl XMTDON
    cpy ICBAHZ
    bvc $4FAC
    bmi $4FA1
    cpy $1886
    ldx XMTDON,y
    dec ICBAHZ
    bvc $4FC0
    ora $1BC7,x
    ldy $1CC2
    .byte $72
    adc $07C3
    stx nmi_handler
    sec
    cpx $0EC1
    and USAREA,y
    clc
    cpx $AA
    asl SHFAMT,x
    iny
    .byte $44
    tax
    adc $F416
    cmp #$4A
    bcs $500D
    dex
    ldx #$55
    brk
    ldy $CB,x
    .byte $37
    cli
    cpy #$31
    .byte $B3
    eor $C1D2
    asl
    ldy RMARGN,x
    adc $C2,x
    .byte $83
    .byte $14
    lda $DD,x
    jsr $C5C3
    ldx XMTDON,y
    jsr $CBFB
    inc $D6F8
    adc $BC99
    clc
    bne $4FD6
    cpy $CD29
    lda $D129,x
    lsr
    cmp $3253
    ldx $D231,y
    jmp $52CE
    .byte $63
    .byte $BF
    and #$D3
    eor #$27
    .byte $CF
    bit $D4E8
    sta $7160
    and #$8F
    .byte $54
    brk
    and.w OLDROW,x
    .byte $04
    .byte $5C
    .byte $03
    cmp ($89),y
    jmp $6257
    ora $4B41,x
    .byte $2B
    rts
    cpy $80
    asl $2E76
    lsr ICCOMZ,x
    inx
    cmp #$A7
    bit $FDA8
    sta $C8D5
    ror $748B
    adc $66D6
    .byte $72
    ora $BD4A
    jsr $34D4
    .byte $1F
    lsr
    jmp $627C
    .byte $47
    ror
    ora $B0C9,x
    sbc $61
    sbc ($9F,x)
    bpl $505A
    sbc $1263
    .byte $74
    sty $DBC9
    .byte $0C
    .byte $87
    pha
    cpy #$38
    .byte $C7
    .byte $E7
    php
    ldx $AD
    .byte $37
    ldx $B4
    bvc $50BF
    jmp ($807A)
    .byte $DA
    .byte $62
    adc FR1
    .byte $DF
    ldx $1DEB
    .byte $04
    sbc $D779
    sta $9048
    .byte $54
    .byte $B7
    cmp $AE87,y
    .byte $1F
    ora (LMARGN),y
    clv
    sbc $34CC,x
    cmp $A5A1
    dec $280D
    .byte $CF
    adc #$20
    .byte $D4
    .byte $52
    cmp $E6D5
    .byte $53
    iny
    stx $E4
    cmp #$37
    dex
    and ($B9,x)
    .byte $CB
    ora $4CC8
    ora LOGCOL,x
    nop
    cpy $D2
    .byte $1C
    cmp COLCRS
    ldy $1D
    dec $AB
    asl $C749,x
    lda $1F0D,x
    jsr $80E0
    sbc (MEMORY_REFRESH_CYCLES),y
    .byte $F2
    sta ($BD,x)
    .byte $02
    eor ($BA,x)
    cld
    adc ($B3),y
    cmp FR1
    .byte $07
    ldy #$92
    bcc $514B
    nop
    .byte $17
    sta INBUFF
    lda ($5C),y
    lda $53C0
    beq $512C
    .byte $07
    .byte $C2
    .byte $83
    .byte $54
    and #$8D
    ldy #$0A
    sec
    nop
    sbc $98
    .byte $B2
    jmp $9866
    cmp ($E9,x)
    bcs $50A6
    cmp $FDF0,x
    sbc $8F
    eor $4916,y
    rol $560E,x
    cmp (DSTAT,x)
    .byte $7F
    .byte $53
    .byte $63
    cmp #$16
    .byte $FF
    lsr $C3
    .byte $F4
    eor #$A8
    .byte $DC
    sta ICCOMT
    inc LTEMP
    sta ICHIDZ,x
    ror $2E67
    .byte $43
    sbc BUFADR
    eor DOSINI,x
    inc COLCRS
    .byte $5C
    and ($E7),y
    eor ($A5),y
    ldx $15E8,y
    cmp $8F4C,x
    asl $5C,x
    .byte $1C
    .byte $F4
    ora #$75
    bmi $50ED
    pha
    .byte $CB
    cli
    bcc $5171
    txa
    clc
    .byte $37
    ldx $B1
    jsr $E265
    jmp $63E6
    .byte $8F
    .byte $54
    .byte $CB
    clc
    eor $461C,x
    iny
    ora $1D68,y
    sec
    dec $1AB2,x
    .byte $D7
    asl $B251,x
    sbc $1B
    sta $1F
    rts
    .byte $FA
    stx $67,y
    .byte $62
    and ($AA,x)
    adc $1522,x
    lda COLCRS,x
    lda COLCRS
    .byte $AF
    .byte $64
    .byte $D2
    ldx ICDNOZ,y
    .byte $23
    lda $A4
    .byte $04
    asl
    sta $386D
    lda $4FB4
    sbc ICDNOZ
    ldx $A0,y
    .byte $CF
    .byte $0F
    ora $B10D
    tay
    rol $7500
    rol ICSTAZ
    .byte $04
    ora ($A9,x)
    eor #$24
    sta (ICAX1Z,x)
    bpl $520E
    rts
    and #$B9
    ora (BFENHI),y
    .byte $67
    rol
    dec $2502
    and $6C7C,x
    ldx $B1B3
    .byte $CF
    cmp $C0,x
    .byte $74
    dey
    sta ($BA),y
    ror $23A4
    asl $6DE9
    bit $13
    nop
    rti
    .byte $F3
    stx $2D23
    rts
    and #$AD
    .byte $42
    .byte $34
    .byte $89
    .byte $1B
    ldx OLDROW
    rti
    and $BE
    sta ($6D),y
    rol BFENHI
    ldy MEMORY_REFRESH_CYCLES
    lda $A6B5
    .byte $93
    and $2FA0,y
    lda #$71
    rol
    sta $B0,x
    asl $485F
    .byte $89
    .byte $1B
    ldx ICBLHZ
    .byte $EF
    beq $5215
    ora $2E27,x
    lsr $0FB1
    plp
    .byte $67
    .byte $82
    sbc ($E2,x)
    .byte $92
    .byte $02
    rol $A8,x
    clc
    .byte $DC
    rts
    lsr
    adc $8D5B
    .byte $89
    .byte $BB
    rts
    lda $4898
    ldy CHKSUM
    sbc (STATUS),y
    .byte $07
    lda ($B8),y
    .byte $EB
    ora $F9F2,y
    inc $A9
    stx BFENLO,y
    bcs $5289
    ora $48,x
    ldy $181E,x
    .byte $43
    eor $4F31
    rol LNFLG
    adc BUFRFL
    sty $1E,x
    .byte $33
    .byte $6B
    .byte $04
    lda $85
    cmp $AF
    .byte $32
    sty FRMADR
    tay
    rol CHKSNT,x
    sta ($87),y
    bne $52B5
    dec $88
    dex
    bpl $52CA
    inc FKDEF,x
    ldx #$08
    .byte $FB
    .byte $FA
    .byte $C3
    eor $75
    asl $2A84
    lda #$CD
    .byte $D2
    lsr $FDED
    txs
    lsr
    inc $13E8,x
    inc $FF8D
    .byte $9C
    sty $18A8
    rol
    .byte $E2
    .byte $0F
    .byte $27
    asl $A9
    lda #$1D
    bmi $52C7
    asl
    bcc $528E
    .byte $8F
    .byte $D7
    and $C4,x
    jmp $7B69
    lda $BED6
    clv
    lda #$62
    .byte $13
    .byte $2F
    .byte $80
    ora $C4
    cpx ICDNOZ
    rti
    adc DIGRT
    ror $60AB
    eor (WARMST,x)
    .byte $04
    .byte $43
    asl $82
    php
    .byte $0C
    sta $3B76
    .byte $FC
    .byte $1A
    .byte $02
    sec
    .byte $87
    cpy $E9
    .byte $63
    .byte $03
    and $E380,x
    lda $0C40,x
    lsr $598C,x
    asl $4C0D,x
    .byte $6F
    ora $F8F0,x
    cmp $E9
    .byte $02
    .byte $43
    txa
    lsr SOUNDR,x
    txa
    stx OLDROW
    ldy $5990,x
    adc $3443,y
    jmp ($E33B)
    sbc (BUFSTR,x)
    .byte $3B
    bne $52DD
    sta $8E
    .byte $7A
    and $8A65,y
    dey
    plp
    tya
    bvc $5275
    lda $AC
    ldy #$78
    lda #$95
    sta ($9F),y
    sta $8560
    dex
    bne $52E0
    dec $AE,x
    dec $E4B1,x
    .byte $62
    eor $4C79,x
    pla
    .byte $47
    brk
    .byte $04
    php
    .byte $0C
    .byte $CF
    bpl $52B7
    nop
    cmp #$52
    .byte $B3
    .byte $04
    tax
    lsr ICBAHZ
    .byte $07
    eor (ICAX1Z),y
    .byte $80
    dec $4825,x
    .byte $5B
    .byte $12
    ldy $AF
    ora $7051
    .byte $0F
    dec $7DCA
    asl
    .byte $AF
    .byte $07
    bne $52C6
    .byte $03
    adc $480C,x
    adc $800A,x
    .byte $27
    tay
    .byte $A3
    .byte $F3
    cli
    cli
    .byte $D2
    .byte $83
    lda $6859,x
    sta $84
    .byte $7F
    .byte $32
    .byte $80
    cli
    plp
    ora $A0,x
    txa
    .byte $3C
    .byte $04
    .byte $4B
    .byte $C7
    ora (DRKMSK,x)
    sta $674C
    eor (ICCOMT,x)
    sta $6D
    asl $CB
    pha
    cpx $D048
    ora #$BD
    adc $59,x
    and TMPCHR
    .byte $82
    .byte $E3
    ror $73
    lda $D254,y
    ldx $A2ED,y
    .byte $44
    ora $91,x
    .byte $83
    ldy $6840,x
    ldx $7F16
    cmp $47,x
    .byte $D4
    inc $AD38
    tya
    .byte $A3
    .byte $F7
    lda ($97),y
    bmi $5385
    sbc $25B9,y
    sta $9DF5
    .byte $7C
    .byte $9F
    jsr $1C90
    sbc $B1D6,y
    ror ZTEMP1,x
    .byte $07
    .byte $02
    .byte $12
    lsr $2386
    .byte $5B
    sta $DB,x
    cld
    .byte $47
    .byte $BB
    .byte $7B
    jsr $1914
    bvs $53AD
    ora $1400,x
    brk
    and (ROWCRS),y
    lda $F61B
    and #$AA
    lda #$28
    cld
    ldy $1CCC,x
    .byte $63
    .byte $5B
    bvs $53F8
    sed
    sta $683C
    .byte $F3
    jmp $D664
    lda (COUNTR,x)
    rol
    sta $33A6,y
    .byte $5C
    .byte $AF
    and $06E1
    .byte $22
    sec
    sbc $98
    and $C4
    rol $C4
    .byte $27
    cpy ICBLLZ
    cpy LTEMP
    dec $0737
    .byte $9F
    .byte $5A
    clc
    .byte $23
    bcc $537F
    ldx $16A6
    bpl $5448
    ora FR1
    sta $9D4C
    .byte $67
    beq $53EB
    ora #$FE
    and #$26
    rol
    rol ICAX2Z
    rol ENTVEC
    and ICSTAZ
    and $DA07
    rol NOCKSM
    ror $383C
    .byte $DA
    ldy $8D09,x
    asl $2A50
    sec
    .byte $B3
    .byte $0B
    lda $CC41
    .byte $89
    .byte $1A
    .byte $92
    .byte $87
    brk
    .byte $0F
    pha
    php
    cmp $B283,y
    inc $CA
    .byte $B7
    .byte $D3
    inc $634C
    sbc $67
    .byte $FC
    .byte $72
    cpx #$99
    .byte $E3
    and #$05
    iny
    cpy #$0F
    inc $B1D9,x
    ldx ZCHAIN
    bne $5432
    .byte $EF
    cpx FKDEF
    jmp ($D0DD)
    lda HOLDCH
    .byte $6B
    and $7149,x
    .byte $FF
    cmp ($B1,x)
    beq $543D
    .byte $9E
    .byte $67
    .byte $D4
    eor ICHIDZ
    cmp #$CB
    sta (FRMADR,x)
    cmp $F06C
    bit $AA
    ldy $039E
    rol $E1C2,x
    .byte $13
    sta $4DA9,y
    .byte $64
    and ADRESS,x
    brk
    .byte $B2
    adc $59
    .byte $1F
    lda $6B66
    pla
    sta $06D6,x
    stx $FF21
    sty FCHFLG
    asl
    lda ICIDNO,x
    ldx FTYPE,y
    plp
    and #$2A
    .byte $2B
    sta $1F,x
    eor $56,x
    .byte $0B
    rti
    bvc $54D0
    .byte $57
    cpy #$A8
    .byte $80
    dey
    .byte $5F
    .byte $83
    .byte $7C
    .byte $B2
    eor CHKSUM
    dec $32A2,x
    .byte $8B
    .byte $1A
    .byte $92
    cmp BUFRHI
    beq $5456
    rts
    dec $CE27,x
    plp
    rol
    cmp $442B
    .byte $CB
    .byte $43
    lda $9D42,x
    eor ($9B,x)
    rol $9736,x
    .byte $34
    .byte $3B
    .byte $32
    .byte $37
    bmi $54E2
    rol $2F39
    .byte $2B
    .byte $3A
    rol
    .byte $3B
    .byte $FB
    .byte $C3
    brk
    .byte $A3
    txa
    .byte $43
    .byte $87
    pha
    lda $6501
    .byte $93
    sta (BPTR,x)
    iny
    sta (HOLD1,x)
    sty $19
    .byte $63
    sty $78
    ora $CC45
    pla
    ror $E4,x
    cpx $6E06
    and (CIOCHR),y
    rti
    and $F2CB,x
    asl $DEA8
    .byte $5B
    .byte $22
    cmp (ICAX2Z,x)
    .byte $5B
    ora $8E85,x
    lda (NGFLAG),y
    .byte $97
    asl ENTVEC,x
    .byte $D4
    .byte $DC
    .byte $12
    clc
    .byte $6B
    adc $3A51,x
    .byte $3B
    .byte $8F
    dey
    adc #$38
    eor $B6C9,y
    .byte $D4
    inc $C2
    clc
    ora ICPTLZ
    ror $4848
    eor ($85),y
    dex
    .byte $7A
    php
    .byte $8B
    .byte $FB
    .byte $62
    sbc $8C7A,x
    .byte $87
    cmp $D351
    sty $A80F
    bvs $5585
    sta (ICBLLZ),y
    sty $E2,x
    iny
    jmp $D647
    .byte $FC
    .byte $64
    ldy $FB
    .byte $3A
    jsr $0B65
    inc $2D9D,x
    adc (ICBAHZ),y
    cmp #$C3
    eor HOLD1,x
    .byte $12
    adc $9050
    lda $9D
    brk
    tay
    jmp ($1D33)
    .byte $F7
    jsr $98F0
    php
    .byte $EF
    lda ($92,x)
    sei
    inc $F6,x
    .byte $DC
    clc
    sty EEXP,x
    eor ($B9),y
    .byte $9B
    .byte $9F
    and $6915,y
    dec SHFAMT
    .byte $6F
    sty $51E5
    and $E9
    cmp $A6A2
    .byte $0F
    rts
    and #$B5
    cpx $A844
    cpy $D16E
    cmp ($B8,x)
    and $D1
    ror $48
    jmp $69AB
    cmp $4CFE
    ldy CIOCHR,x
    .byte $72
    ror
    ldx $E298
    .byte $63
    .byte $6F
    .byte $2F
    ldy #$88
    sed
    .byte $62
    .byte $7B
    .byte $02
    .byte $1C
    .byte $4F
    bvc $55A4
    .byte $2B
    tay
    .byte $F7
    .byte $F3
    rol $B0,x
    .byte $02
    .byte $B3
    inc $8D5D,x
    stx ICBLHZ
    .byte $47
    cpx $184E
    adc $9C73
    .byte $97
    cld
    cmp #$4C
    ora ($69),y
    cmp $4EBE,x
    .byte $C7
    lda $D261,x
    sta $5E0A,x
    brk
    .byte $3C
    sbc (FKDEF,x)
    .byte $7A
    .byte $9C
    rol $79AC,x
    bit VCOUNT_START
    eor $4090,y
    .byte $D2
    bne $55EF
    ldy $B684,x
    ora ($81,x)
    bcs $55AC
    .byte $89
    .byte $13
    .byte $42
    lsr ICHIDZ,x
    .byte $67
    bne $560B
    lda ICAX1Z
    sta $8B9C
    .byte $14
    ldx $2BC1,y
    pha
    lda $B6C2,x
    sei
    cmp $A0,x
    .byte $27
    lda ($E7),y
    sta ($99),y
    cld
    sbc VBI_SCANLINE
    sed
    sbc (DSTAT),y
    ror RAMTOP,x
    .byte $F3
    .byte $2F
    .byte $22
    lsr $0383,x
    .byte $DC
    adc FREQ
    sta $9063,x
    asl $D8,x
    asl $C8
    dec $8D6B
    ror ICSTAZ
    adc ($F4,x)
    cld
    ora $AC5A,x
    .byte $A3
    eor COLPF0
    ora ($4B,x)
    asl
    .byte $82
    txa
    lda HOLD1
    bpl $55C8
    .byte $22
    sta ICAX1Z
    .byte $43
    ora $0D97,y
    bne $5620
    lsr $5A34
    rti
    .byte $14
    .byte $82
    cpy ICDNOZ
    .byte $3C
    sbc $B6
    cpy #$31
    ror
    ora $DDC5
    bit $1AF0
    cpx #$E9
    ora ($C2,x)
    .byte $13
    dec $A9AC
    .byte $57
    .byte $EF
    .byte $04
    ora FRMADR,x
    .byte $54
    .byte $BB
    .byte $97
    adc #$B2
    lda $1D
    pla
    .byte $AB
    clv
    .byte $E3
    ror
    .byte $0B
    .byte $F3
    .byte $0C
    .byte $62
    .byte $34
    .byte $5C
    cmp $9D
    .byte $33
    ora #$5B
    .byte $D4
    .byte $44
    .byte $83
    ror $84
    ldy $D4C5
    .byte $8B
    .byte $AB
    ldy $6BD3
    .byte $FC
    .byte $5C
    bit $2D
    inc FR0M,x
    brk
    .byte $03
    .byte $80
    ora $61,x
    ror EEXP
    .byte $33
    .byte $12
    bit $B670
    bit $A890
    .byte $0C
    plp
    .byte $72
    .byte $6B
    .byte $97
    clc
    ldy $2D86
    .byte $6F
    .byte $64
    adc DIGRT,x
    tya
    .byte $04
    .byte $07
    .byte $3F
    dex
    ldy TRAMSZ,x
    ora ($BE,x)
    .byte $27
    sta $64B0,x
    adc #$67
    .byte $33
    bmi $56E9
    adc $9D,x
    ora $7383,x
    and FREQ,x
    bvs $5651
    bmi $568D
    jmp $419F
    cmp $92C0
    ldy FR1
    cpy $E561
    ora $2E29
    .byte $62
    cmp DOSINI,x
    pla
    lda PALNTS
    .byte $DA
    .byte $12
    adc $55E5
    .byte $12
    eor $99A8,y
    eor $99
    adc $C3A3,x
    .byte $93
    ora $667A
    pla
    .byte $DC
    .byte $D2
    .byte $62
    plp
    cmp $A6,x
    .byte $6B
    sbc #$6E
    .byte $6F
    .byte $77
    jmp ($2CF9)
    .byte $67
    .byte $F2
    .byte $23
    adc ($75),y
    .byte $A3
    .byte $E3
    rti
    .byte $42
    .byte $34
    ror
    sty $B2,x
    .byte $0B
    .byte $0F
    .byte $62
    .byte $2B
    adc ($6D,x)
    .byte $37
    sta (FRX),y
    sty PALNTS,x
    lsr
    adc $1D
    adc #$1D
    ror $C767
    .byte $73
    .byte $72
    .byte $6F
    adc LOGCOL,x
    cmp CYCLES_PER_SCANLINE,x
    adc (TOADR,x)
    .byte $E3
    .byte $82
    .byte $12
    .byte $5A
    sed
    and BITMSK,x
    adc #$AE
    lda ($D3,x)
    .byte $73
    .byte $6B
    .byte $72
    .byte $FA
    adc $64FB,y
    .byte $0C
    .byte $FB
    .byte $23
    and ($56,x)
    bit $3315
    plp
    and #$30
    lsr $6F74,x
    .byte $97
    .byte $72
    lda ROWAC,x
    jmp ($6361)
    adc $5F
    pha
    sbc PM_DMA_CYCLES
    cld
    asl TMPCHR
    tay
    rol $68FB
    .byte $1C
    tya
    .byte $5C
    .byte $44
    .byte $72
    nop
    rol $9D01
    bmi $57A5
    asl $E9
    ror DSTAT,x
    and $0EBA
    .byte $47
    .byte $52
    ldy $6C4D,x
    inc $1410,x
    ldx #$C8
    sta $4183,y
    rts
    .byte $1F
    bvc $56CC
    .byte $27
    lsr $A0
    brk
    and ($C9,x)
    .byte $7F
    .byte $8F
    and FTYPE,x
    sty $EA,x
    ora #$FC
    .byte $D4
    sta (NSIGN),y
    .byte $82
    adc (POKMSK,x)
    sec
    rti
    pla
    bcc $571D
    .byte $7F
    inc $1D37
    ldx $9D,y
    .byte $EF
    .byte $B2
    .byte $C7
    .byte $53
    .byte $F2
    .byte $42
    ldx INBUFF
    sta ATRACT
    jsr $4505
    bcs $570E
    sta (DSTAT),y
    bcs $57E4
    ldx FREQ,y
    .byte $03
    lda $8AA8
    asl
    .byte $37
    .byte $5C
    and #$26
    lda $6CFF,y
    sbc $4818
    dex
    adc $6D0A,x
    .byte $FA
    .byte $8B
    lda CIX
    .byte $57
    .byte $4B
    sty $CB8D
    lsr $0A0D,x
    .byte $4F
    .byte $07
    lda ($BD),y
    rol $6071,x
    jmp $D8E3
    jmp ($52DF)
    bcs $582C
    .byte $9E
    sta $C3F5,x
    eor $3C70,x
    cld
    asl $730D,x
    sbc ($48),y
    sta ($8B),y
    iny
    inx
    cpy #$08
    bne $57C2
    sbc FKDEF,x
    cpy ICHIDZ
    .byte $04
    dey
    asl SOUNDR
    bpl $57D2
    .byte $82
    .byte $22
    brk
    .byte $07
    .byte $1A
    .byte $92
    .byte $B7
    .byte $C7
    .byte $0B
    .byte $D7
    .byte $FC
    adc #$4C
    sbc ($56),y
    .byte $64
    sei
    sty $9A
    ldx #$B8
    dex
    .byte $FF
    cld
    nop
    .byte $FA
    php
    .byte $1C
    rol
    sec
    lsr $FF
    .byte $5A
    jmp ($887A)
    stx $A6,y
    ldy $C6,x
    .byte $FF
    .byte $DA
    cpx $F4
    asl $F4
    ror $86
    ldx DSKUTL,y
    plp
    lsr $BF
    asl $109D
    ora (RTCLOK),y
    .byte $13
    .byte $14
    sbc $9F46,y
    ora $950A,y
    .byte $33
    brk
    stx NGFLAG
    rol CASINI
    rol $03
    rol $1B
    bit $F4
    .byte $5A
    .byte $12
    .byte $C2
    dec FMSZPG
    .byte $1B
    cli
    rol $D8
    .byte $E3
    .byte $DB
    ldx $6D
    ora $B060
    rol
    bcc $58A0
    sta $ADE0
    .byte $0F
    asl vbi_immediate
    sty $6A18
    ora OLDCHR
    jmp $8F1F
    adc $83D9
    cpy #$3E
    dec $87,x
    ora $F2C1,x
    cpy $C006
    .byte $C7
    .byte $C3
    lsr NGFLAG
    stx $4887
    ora $94
    .byte $97
    .byte $07
    .byte $D2
    ldx #$13
    ror $9FD1,x
    .byte $82
    .byte $4B
    .byte $DA
    brk
    lsr
    ora $1761
    jsr $8B32
    bcc $5874
    and $0A40,x
    .byte $1A
    .byte $23
    txa
    .byte $43
    beq $57F6
    plp
    .byte $32
    ora ICCOMZ
    adc $64DD
    sta $46
    .byte $33
    lda $1483,x
    .byte $A7
    rol $FB
    cmp $8D50,x
    .byte $80
    tsx
    dec $CF51
    sbc ($A9),y
    .byte $63
    .byte $0F
    and $25F9
    .byte $44
    sed
    asl $C8
    inc $418C
    .byte $BF
    asl $61,x
    dec $8ACB
    sbc ($D7),y
    ldy #$14
    .byte $77
    cmp $E1F3,x
    and $468E
    eor (SWPFLG,x)
    ldx $607C,y
    adc (BUFRFL,x)
    and $7235
    .byte $22
    and ($B5,x)
    and $B4
    .byte $DA
    tax
    ror $392F
    and ICBALZ
    .byte $F2
    tya
    .byte $13
    iny
    and #$2E
    .byte $27
    sec
    jsr $6E1B
    beq $58D6
    eor $CFAF
    .byte $82
    lda $B0D7
    jmp $5F6C
    ror $61F6
    beq $5902
    pha
    .byte $F2
    bne $5877
    eor $E8,x
    .byte $74
    bpl $5875
    adc ($86),y
    .byte $1C
    iny
    lda $6A32,y
    ldx BUFRHI
    and $4C04,y
    tax
    .byte $FA
    jmp $03A3
    cmp #$7D
    brk
    .byte $F2
    .byte $27
    ora ($1D,x)
    eor ICHIDZ,x
    cpy #$65
    .byte $7A
    ldy #$EB
    .byte $1F
    lda (BITMSK),y
    sta $CF4C,y
    eor ($CA,x)
    sbc ($BE,x)
    inc FKDEF,x
    .byte $33
    adc NMIEN,x
    eor #$34
    .byte $DC
    .byte $32
    tax
    bit $3621
    .byte $AB
    .byte $0B
    and ICIDNO
    bit $AD
    .byte $6F
    .byte $7C
    .byte $5A
    .byte $0F
    sei
    ora $9201
    sta (BFENHI),y
    eor $40B2
    .byte $7A
    .byte $32
    bcc $5977
    lda #$B6
    pha
    .byte $87
    pla
    adc $2392,y
    .byte $54
    .byte $DA
    cld
    cld
    .byte $C7
    .byte $47
    iny
    .byte $93
    sbc BUFRLO,x
    txa
    asl
    .byte $CB
    and $BA,x
    ora ($AE,x)
    ldy $AD,x
    ora ($AA),y
    eor $0389
    cmp $1AB2,y
    ora #$9F
    bcc $5960
    lda $9EA2,x
    lda #$1C
    .byte $5F
    sta (CHKSUM),y
    .byte $93
    bit $3438
    .byte $83
    ora $F518
    stx TMPCHR
    .byte $BF
    adc ($1B),y
    lda $7C8E
    rol
    iny
    cpx #$4C
    pla
    txa
    ora #$FD
    bvs $599A
    sty $718F
    .byte $DA
    sta $A2A0
    bvc $59F2
    .byte $CF
    dec BUFRFL
    .byte $3A
    .byte $1A
    sbc $A5CE
    bne $599F
    rol $D1,x
    clv
    asl NSIGN
    adc (FREQ,x)
    bcc $593D
    cli
    ror $B2,x
    dec $E7
    jmp $6F94
    ldx $8804,y
    .byte $C3
    and $D930,y
    .byte $73
    lda BUFRHI
    ora HOLD1
    ora #$F1
    eor ($8E),y
    brk
    sbc ($A0,x)
    lda (BUFADR),y
    adc FRMADR
    .byte $BF
    and $49F1
    pha
    rts
    .byte $5A
    .byte $74
    .byte $92
    bvs $5951
    .byte $8F
    .byte $B7
    stx $CC
    adc ($BC,x)
    ror $B9
    stx $C3,y
    .byte $62
    .byte $E2
    .byte $14
    bne $5A2D
    .byte $E7
    .byte $7A
    .byte $7C
    .byte $E3
    .byte $DA
    .byte $5B
    ldy #$D8
    ror $E815
    .byte $9C
    .byte $22
    .byte $80
    .byte $DB
    .byte $6F
    .byte $33
    ldx $861B,y
    bvs $59C1
    .byte $12
    sec
    inc $C0,x
    .byte $D2
    cmp #$31
    .byte $8F
    ora #$30
    .byte $F3
    .byte $13
    stx DSTAT
    ora (FTYPE,x)
    .byte $82
    ora $CB6A,y
    ldy #$17
    cpy #$4A
    cmp #$43
    .byte $D7
    .byte $77
    tay
    clc
    and $A722,y
    .byte $CF
    .byte $87
    bcc $5A0F
    beq $5A33
    .byte $03
    ldx $78E1
    asl $2587
    dey
    .byte $5F
    sta $0F75
    .byte $3A
    lda (BFENHI),y
    .byte $1A
    asl QTEMP
    cmp (VBI_SCANLINE,x)
    .byte $04
    pla
    brk
    .byte $1A
    .byte $92
    bpl $5A29
    ldx #$01
    .byte $3F
    adc $0828,x
    .byte $1A
    .byte $80
    .byte $1A
    .byte $87
    bcc $5A98
    lsr $C8
    cmp $D079,x
    ora TOADR
    .byte $6B
    rol FRMADR
    cmp $E2EA
    .byte $3A
    asl $DC
    bvs $59C9
    .byte $82
    .byte $34
    beq $5A4B
    adc STATUS
    ror
    ldx $3B7A
    cmp $A7DB,x
    bcs $5A5C
    txs
    .byte $82
    tsx
    .byte $33
    .byte $5B
    .byte $0F
    sta FR2
    cpy $48
    .byte $0F
    cmp $4132,y
    .byte $34
    ora ($1D,x)
    .byte $AF
    jmp ($FB1D)
    sec
    clc
    rol
    .byte $63
    cmp $D6BA
    .byte $13
    and SOUNDR
    and $90BB,y
    .byte $0C
    clv
    .byte $DF
    .byte $9B
    stx $A2
    .byte $3B
    and #$20
    tya
    sta $C7,x
    rts
    cmp #$93
    cmp ($B9,x)
    inc RMARGN
    cpy #$D0
    .byte $12
    .byte $80
    .byte $83
    eor (FKDEF,x)
    .byte $7B
    ldy $84,x
    plp
    .byte $03
    .byte $B3
    inx
    stx $410E
    sty $DC
    ldy $6481,x
    ora HOLDCH,x
    dex
    cpy $0BE5
    .byte $9C
    lda $DF19
    rol
    .byte $14
    asl
    inc $8D90
    .byte $14
    .byte $D2
    stx $3A6D
    .byte $0C
    rol BUFRLO
    nop
    .byte $E2
    ora $D8
    sty $C0E0
    .byte $52
    sei
    sbc $6DDE,x
    and ($88),y
    .byte $6B
    sbc $7B9D,y
    adc $248A,y
    beq $5AF4
    .byte $92
    bcc $5AFA
    adc $1AFD
    adc (ESIGN),y
    .byte $F7
    jsr $300B
    dec $14
    sta FR1
    and #$5D
    .byte $7B
    sei
    .byte $3F
    dec $DE14,x
    .byte $52
    cpy #$59
    ora $27EF,x
    .byte $E7
    ror RECVDN
    .byte $14
    sec
    sbc $EA86,x
    bcc $5AFC
    sta $1CFF,x
    jmp $718B
    inc LTEMP,x
    cmp (ZTEMP1,x)
    sta $82B1
    jmp ($5D6D)
    rol $C4
    sty $AE,x
    .byte $07
    lsr $6543
    jmp $71A6
    .byte $07
    .byte $D4
    .byte $64
    inc $91
    sbc $DE,x
    adc (ZTEMP4),y
    .byte $9E
    .byte $22
    .byte $0C
    cpx $A928
    sta ICBLHZ
    .byte $4F
    .byte $2F
    sec
    .byte $32
    rol $DB40,x
    .byte $7C
    sty $DE,x
    dey
    eor $CB60,x
    .byte $52
    ora (STATUS,x)
    rol FR1
    .byte $73
    lda $0687
    .byte $87
    .byte $CF
    bcc $5B4C
    .byte $1B
    and (TMPCHR),y
    php
    lsr $E152
    ldx $E5,y
    adc (FKDEF),y
    .byte $8F
    lda BUFRHI,x
    .byte $0B
    .byte $83
    and BUFRFL
    .byte $92
    .byte $89
    cmp #$64
    and $B2,x
    .byte $7F
    eor $2C36,y
    .byte $77
    stx $37,y
    .byte $4B
    adc $3227,y
    lda #$EA
    adc $206A,x
    sty ICPTHZ
    .byte $89
    cmp (BFENLO),y
    .byte $72
    sta $8CF7,y
    .byte $B2
    .byte $DB
    inc $A8
    .byte $1F
    ldx $0CB4,y
    .byte $AF
    plp
    ldy $85
    eor $8BAE,x
    .byte $3F
    bmi $5B1F
    adc $60A3,y
    .byte $FC
    lda ($65),y
    ora ICAX1Z
    jsr $6532
    ldy $2364
    bne $5B62
    ror
    .byte $0C
    rti
    bit $3A4E
    rol $10D0,x
    bmi $5BC3
    .byte $DC
    .byte $74
    .byte $03
    ora $8B
    .byte $12
    .byte $B2
    sta (CMCMD,x)
    .byte $04
    asl BUFADR
    ldx $D361,y
    pla
    ora #$95
    ora $C440,y
    adc $E8
    .byte $02
    asl
    clv
    .byte $80
    .byte $1A
    lda ($D2),y
    .byte $04
    .byte $0C
    ora $2F50,x
    .byte $37
    ldx TRAMSZ,y
    eor $AB,x
    cli
    bne $5B6D
    sbc #$02
    asl $B8,x
    rts
    ldy #$32
    .byte $0F
    ora $1D
    rol $3C03
    and (DSKFMS,x)
    .byte $03
    .byte $4F
    rol
    .byte $80
    asl $0177,x
    .byte $14
    ora ($9E,x)
    bmi $5BCC
    .byte $4B
    inx
    adc #$B2
    .byte $B7
    bpl $5B88
    rts
    .byte $32
    .byte $23
    .byte $02
    ora $2F20,y
    ldy $2D14,x
    .byte $DB
    .byte $32
    and FKDEF
    bvc $5BA7
    ora $3A3C
    .byte $37
    bit $E970
    .byte $0C
    lsr $CE
    eor (ICBLHZ,x)
    inc $6828
    .byte $4B
    sbc #$04
    bvc $5C3F
    .byte $80
    .byte $F4
    ldx $03
    eor OLDROW,x
    sbc XMTDON,x
    ldy #$64
    sbc #$7E
    .byte $5F
    lda BFENLO,x
    bcc $5C8B
    .byte $3C
    .byte $04
    .byte $72
    ora $DD
    eor $E5D0,x
    jsr $7886
    ora ($D6,x)
    rts
    ldy $8D,x
    stx $03,y
    .byte $B7
    .byte $82
    ora (PM_DMA_CYCLES),y
    txs
    .byte $02
    iny
    ldy #$97
    cpy #$8C
    brk
    lda $FAD1
    beq $5C51
    .byte $DF
    .byte $47
    ldy $D236,x
    stx $184C
    inc $B0
    ldy $2EB0
    tax
    dec $E2,x
    sta $C7
    lda $F079
    ora $DC35,y
    sta (ZCHAIN),y
    ora $58D7
    asl $6476,x
    dec $E4A5
    bpl $5C43
    asl
    dey
    lda $2C97
    and DRKMSK,x
    eor #$37
    sbc #$05
    nop
    .byte $A7
    eor ($A6),y
    .byte $5C
    rol LOGCOL,x
    .byte $92
    clc
    bmi $5CBB
    lda $8EFC
    .byte $1C
    .byte $FB
    adc ($44,x)
    .byte $F4
    .byte $E7
    stx ICBAH
    .byte $1F
    ora ($BB),y
    cpy TOADR
    bcs $5C25
    dex
    sta $75
    sbc $A273,y
    .byte $FF
    stx $18F8
    cmp LNFLG,x
    .byte $33
    and ($DE,x)
    .byte $0C
    lsr
    .byte $E2
    dey
    ror DELTAR,x
    bit APPMHI
    .byte $42
    and #$51
    .byte $7F
    ldy $E173,x
    cld
    .byte $12
    lda $78
    ror $0F,x
    jsr $6327
    adc $9507,y
    .byte $63
    .byte $54
    ldx $4C56
    inc $83
    .byte $73
    inc $C8E4,x
    adc $624A,y
    .byte $B2
    bmi $5CDB
    clv
    sta $D780,y
    .byte $44
    .byte $47
    ldx ICCOMT,y
    php
    eor DOSVEC,x
    beq $5C83
    .byte $3C
    beq $5CE6
    .byte $9E
    pha
    ror $AD
    .byte $C7
    ora $BCFC
    .byte $1C
    .byte $AF
    .byte $67
    and $6879,y
    .byte $0C
    tax
    ora $C988,y
    .byte $52
    .byte $43
    dex
    .byte $54
    bpl $5C97
    bne $5D1D
    inx
    lda $49EF,x
    and ($6D),y
    .byte $D3
    .byte $CF
    eor $CB6A,x
    .byte $03
    eor (DOSINI,x)
    .byte $CB
    sty $B8D2
    sei
    sta ICHIDZ,x
    .byte $0F
    .byte $DC
    .byte $0C
    eor ($B9,x)
    .byte $04
    ora $80
    nop
    bpl $5D7E
    ldy #$17
    .byte $43
    lda $D90F,y
    sei
    ora $E002,y
    .byte $04
    .byte $2F
    ldy #$81
    .byte $22
    jsr $7459
    .byte $F3
    .byte $03
    ror $14
    .byte $D7
    pha
    cmp #$FD
    bcc $5D43
    .byte $9F
    .byte $3A
    cmp ADRESS,x
    .byte $37
    ora ICHIDZ
    .byte $DA
    pla
    jmp $8E9A
    .byte $5A
    lda $6543
    sty ENTVEC
    cpx #$A7
    .byte $0B
    ora $8AF9
    sty ZCHAIN
    lsr
    bpl $5CD2
    ora $D368,x
    .byte $D7
    sty $A7
    .byte $74
    tax
    lda $9F39,x
    adc $8699
    bit $A0F1
    ldy LNFLG,x
    asl
    sty $D3
    asl $3B01,x
    .byte $2B
    cpx #$06
    .byte $07
    sta WARMST
    ora #$85
    ora $4DAC,y
    sta TMPCHR
    adc PRIOR,x
    .byte $7A
    cld
    .byte $EB
    plp
    ror $3DD9,x
    inc NGFLAG
    .byte $02
    lda $553E
    rol $E6DB
    .byte $DC
    .byte $63
    inc $8DE0
    .byte $F3
    ora #$D4
    inc $D3,x
    adc $2785,x
    inc $14
    jsr $822B
    lda $9CFF
    ldy $F3F0,x
    ldy #$07
    ldx $B85A,y
    pla
    asl
    ror $2620
    .byte $2F
    .byte $1C
    bcc $5DED
    cpx DSTAT
    cmp ($8C,x)
    ldy $9C,x
    lsr $95E2
    .byte $42
    sed
    .byte $D4
    .byte $73
    nop
    sta ($A8,x)
    sbc $19,x
    eor $711D,y
    .byte $FC
    .byte $04
    .byte $97
    .byte $33
    .byte $8B
    adc $47,x
    cld
    asl
    .byte $80
    .byte $D2
    php
    sta (COLCRS,x)
    sbc ($D7,x)
    .byte $82
    .byte $87
    .byte $93
    eor (FMSZPG),y
    tya
    bvc $5DBC
    .byte $AB
    sty $B7,x
    .byte $9B
    pla
    sta $7275
    .byte $54
    and ($96,x)
    .byte $EF
    ror $EB,x
    inc $B2DF
    .byte $EB
    cmp $7C7D,y
    ror
    bcc $5D94
    .byte $0F
    .byte $44
    adc $95EB
    ror $D6
    inc $2D95
    rol $4C67,x
    sei
    adc $FD,x
    bpl $5E65
    and $6862
    .byte $53
    eor (SHFAMT),y
    lsr $E045
    .byte $3B
    sta ($C9),y
    .byte $1F
    .byte $62
    inx
    .byte $7B
    sty $AE,x
    cpy $76CC
    .byte $4F
    lsr KEYDEF
    ror FR0
    sbc #$18
    .byte $5B
    .byte $43
    .byte $0F
    bcs $5E3C
    stx $5D0A
    .byte $43
    dec $AD25
    and #$4A
    .byte $2F
    tsx
    .byte $2F
    .byte $82
    stx $E7,y
    lda (BFENLO),y
    .byte $77
    .byte $AF
    .byte $F7
    .byte $52
    lsr $825D
    jsr $86DE
    inc $7FF4
    lda ($B8),y
    ora $528A
    ora $265E,x
    cmp #$09
    ora $CE
    clc
    .byte $3A
    php
    lsr $E819
    cpx $83
    sbc ($75,x)
    jmp XITVBV
    .byte $1F
    jmp (CDTMA1)
    sbc #$06
    .byte $1A
    ora $A4,x
    asl $0BAA
    php
    sbc $8E18
    sty RMARGN
    ldy #$AC
    .byte $E7
    inc $13,x
    .byte $1A
    .byte $3C
    lsr
    .byte $14
    tax
    lda #$89
    ora (ZTEMP1,x)
    bne $5E9D
    .byte $D4
    .byte $7C
    .byte $E3
    adc ($AC,x)
    ldy DOSINI
    sbc $96CE,x
    .byte $82
    txs
    .byte $7C
    sta $2D
    jmp $762C
    .byte $57
    rol $9D
    cmp #$C0
    beq $5E7F
    eor #$8D
    .byte $DB
    lda $6E98,x
    rti
    jmp $7643
    .byte $AF
    and STATUS,x
    ora $0FE2,x
    .byte $0F
    .byte $82
    lsr
    eor #$0F
    .byte $EF
    .byte $CB
    ldy $E8
    bpl $5F0B
    .byte $42
    .byte $3C
    .byte $9F
    adc ($BD),y
    ora DSKUTL,x
    cmp $7667,x
    and $41C9,x
    bvs $5E78
    adc ($86,x)
    inc $D2AB,x
    .byte $12
    ldy CYCLES_PER_SCANLINE,x
    ror $AC25,x
    dec $4C42
    .byte $7A
    ror $9E,x
    adc $1DDA
    .byte $DA
    sbc $80
    txs
    dec DRKMSK
    .byte $33
    php
    ora $90,x
    ora $0F,x
    .byte $17
    lda $2230,x
    stx $FB,y
    .byte $D2
    ora ($1F),y
    ora $5E45,x
    ror PM_DMA_CYCLES,x
    .byte $97
    .byte $57
    jmp $76A7
    lda #$C0
    ora $8D3F
    .byte $89
    asl $A0F9,x
    ldy DRKMSK,x
    .byte $83
    asl $E3,x
    adc ($CF,x)
    sbc $1A4B
    cpy #$76
    lda ($D7),y
    jmp $68A8
    cpx #$EE
    lda $E2
    stx $4120
    adc FKDEF
    sbc $3DD6,x
    .byte $03
    adc ($82,x)
    rts
    cpx $A5
    .byte $FB
    sta (BUFCNT),y
    sbc $88
    sbc $8C
    asl $0ABA
    .byte $C7
    inc $C7
    rol
    .byte $D3
    bpl $5F3F
    .byte $93
    bvs $5F14
    lda #$BC
    .byte $03
    .byte $2B
    ldy #$F8
    .byte $4B
    rti
    .byte $13
    .byte $5A
    bit $AD8C
    .byte $5B
    .byte $33
    sta $7F4A
    lda $14
    .byte $F3
    .byte $1B
    .byte $47
    .byte $0C
    cpx #$87
    rts
    sta $2712
    .byte $82
    asl $1F0D,x
    .byte $80
    clv
    php
    adc $99
    sec
    sbc ($9D,x)
    lsr $ECB1,x
    asl $C7,x
    ror HOLDCH
    ror $0C06,x
    rol $19A0
    .byte $C2
    .byte $C3
    bpl $5F5B
    .byte $5C
    and $56
    .byte $D2
    .byte $23
    .byte $52
    adc $68E6,x
    cld
    ror $D179
    rol $19
    pla
    sbc $C196,x
    txa
    and $96,x
    bpl $5FC7
    sta $80
    .byte $22
    ora $61,x
    lda $D832
    .byte $2F
    and $056C,y
    jmp $6A9F
    and ($59),y
    pla
    inc BUFCNT
    stx OLDROW,y
    .byte $3A
    .byte $37
    .byte $32
    sta (POKMSK,x)
    .byte $FB
    txa
    and $BE
    .byte $62
    sta $2B4D
    .byte $C2
    cmp $E4,x
    bit $C610
    ora DSKUTL,x
    bmi $5F86
    .byte $B2
    .byte $03
    bcs $5FE5
    .byte $B2
    .byte $D7
    adc $B5
    cpx $4C14
    cpx DELTAC
    .byte $A7
    iny
    sta $83
    inc $AE
    .byte $8F
    .byte $34
    iny
    lda CHKSNT
    ldx $F031,y
    ora ($19,x)
    cmp ($90),y
    .byte $33
    jmp $30A8
    cmp ($D1),y
    eor $D0A3,x
    sta $46
    jmp $781A
    ldx $40E2
    bit FR2
    .byte $33
    bpl $5FD5
    asl $A0C1
    cmp ZTEMP4
    .byte $9C
    lda #$3A
    .byte $C3
    .byte $23
    brk
    .byte $80
    ror
    bpl $5FFA
    .byte $EF
    ora $E998
    sbc $9CC2,y
    .byte $0C
    cmp $C907
    .byte $5C
    adc $B4,x
    .byte $5B
    .byte $E2
    and ESIGN,x
    .byte $9B
    rts
    and $9903
    .byte $33
    .byte $92
    sec
    lda #$9B
    .byte $8F
    sbc SWPFLG
    sta $E3
    ldy $B962
    .byte $53
    jmp ($9170)
    dey
    bne $601A
    lda $E0F0,x
    eor ($93,x)
    inx
    bmi $5FEF
    .byte $9B
    .byte $CB
    .byte $42
    sec
    and $9097
    ora RAMTOP
    .byte $87
    bmi $60AB
    bpl $5FF1
    .byte $7A
    lsr $D140
    sei
    .byte $3C
    .byte $82
    sta ($96,x)
    eor ($2D,x)
    dey
    bit VCOUNT_START
    pha
    ora $1605,y
    lda TRAMSZ
    cmp STATUS
    .byte $BB
    plp
    plp
    ora (FR2),y
    lsr
    inc $C9F2
    sei
    asl $025D
    cli
    rts
    .byte $1B
    clc
    inc NGFLAG
    bne $6095
    stx $1CAF
    and #$47
    asl
    .byte $0C
    .byte $42
    jsr $181E
    sta $9C44,y
    ora (FREQ,x)
    pha
    dey
    .byte $42
    .byte $B3
    lda $DF07,x
    .byte $12
    bne $60F6
    .byte $AB
    .byte $2F
    bmi $60F8
    .byte $7A
    .byte $9C
    .byte $17
    jmp ($8577)
    .byte $14
    dec CMCMD
    bpl $6095
    stx $95,y
    bpl $6040
    .byte $62
    rti
    .byte $3F
    jsr $A517
    .byte $F7
    cmp $B233
    .byte $77
    ror $073D,x
    dey
    inc DINDEX
    lsr $4C9C,x
    .byte $4F
    .byte $9B
    jmp $A668
    rol $9731
    bmi $60BF
    beq $60EC
    nop
    dec FTYPE,x
    .byte $37
    php
    rts
    .byte $5C
    bne $6105
    pla
    .byte $83
    adc $CD0A,y
    ora #$9D
    .byte $34
    .byte $EB
    cpx FR1
    pha
    cmp $46C9
    asl $82,x
    ora DSKFMS,x
    .byte $80
    .byte $14
    .byte $42
    .byte $A3
    sta FR0
    adc ($CE),y
    ora $3301,x
    and $08D8,x
    and #$32
    beq $6072
    lsr $D6,x
    ora ICBALZ
    sta (FR1M,x)
    .byte $DA
    bpl $60E9
    .byte $A7
    sta $2650,y
    .byte $04
    bne $611B
    stx $3438
    .byte $8F
    .byte $8B
    cmp $EE8E
    .byte $17
    .byte $14
    .byte $2B
    jsr $B1F0
    bvs $609A
    .byte $82
    .byte $7A
    jsr $F64C
    adc $F474,y
    ora #$E1
    sta DRKMSK
    sei
    rol $A031,x
    lda $B2D7
    plp
    beq $6184
    sei
    cpx $C9
    .byte $0F
    cli
    jmp $79A5
    .byte $0C
    php
    tay
    ora $663F,y
    cpy #$15
    .byte $04
    lda $1BD5,y
    sta ICCOMZ
    cmp ($0D,x)
    eor ATRACT,x
    sta TMPCHR
    cmp WARMST
    inc $C2
    bcc $6147
    .byte $3A
    .byte $43
    adc (FR0,x)
    adc ($9D,x)
    .byte $14
    jmp ($072A)
    lda $5317
    rts
    asl ZTEMP3,x
    jmp ($042E)
    ldx $4FC4
    dex
    .byte $5B
    .byte $02
    clv
    .byte $12
    and FEOF,x
    sta $07C1
    cmp $0EDD,x
    lda (NSIGN),y
    bcc $6154
    ror $2BA2
    ldy $0AFB
    .byte $0C
    lda $A3A3,x
    cpy #$02
    lda #$7D
    .byte $12
    .byte $5B
    lda ($CD),y
    sty $84,x
    rol COLRSH,x
    sta $1DC2
    cmp $2CC3
    bcs $6184
    .byte $77
    .byte $92
    inc $4C35,x
    .byte $BF
    .byte $7A
    bcc $61D3
    php
    .byte $B3
    .byte $3C
    rti
    asl $2FFD
    .byte $27
    lda $06F3,y
    sta (DOSINI,x)
    .byte $12
    clc
    asl $0EF1,x
    .byte $74
    .byte $23
    dey
    asl
    rol ICPTLZ
    .byte $63
    cmp #$52
    plp
    ldy CHKSNT
    ror $80C1
    lsr
    sta ($DF),y
    sta ($65),y
    ora $E0A2
    sec
    .byte $77
    .byte $F3
    sbc #$51
    .byte $9F
    .byte $E7
    .byte $37
    .byte $B2
    ldy $4CE2,x
    .byte $AF
    cmp ($A9,x)
    ora $8C,x
    adc $CE
    lda ICDNOZ
    and #$07
    sei
    cmp $A4A3
    .byte $5C
    .byte $32
    asl $2186,x
    txs
    iny
    lda ($BB),y
    sta $AD7A
    .byte $53
    .byte $1A
    .byte $2B
    .byte $FC
    ldx #$00
    asl $F8D2
    sty ICIDNO
    eor $8301,y
    .byte $3F
    .byte $D4
    .byte $2B
    bcc $61D8
    beq $61D8
    adc $AE67,y
    and $59,x
    .byte $74
    ldy $4C75
    ror ZCHAIN,x
    sta PADDL7,x
    .byte $5F
    sty DOSINI,x
    cli
    ora ($D0,x)
    .byte $EF
    .byte $22
    .byte $02
    .byte $BB
    cld
    lsr $B098,x
    asl $0CD1
    jsr $7BB0
    .byte $F7
    inx
    dec OLDADR
    bpl $61F4
    txa
    .byte $FC
    sei
    ldx $CF8D
    .byte $3B
    clc
    cmp $0865,x
    ldx $A5BB
    ldy $3EAD,x
    ldy $E3CE,x
    .byte $D3
    .byte $E7
    jmp $7BA3
    tsx
    asl $AA
    cmp ($90),y
    ora ($0B,x)
    tay
    sec
    stx $C4
    jmp $7BC5
    adc POKMSK,x
    clc
    rol PADDL4,x
    sbc (WARMST),y
    cpx #$C8
    dex
    dec $1CBE
    plp
    .byte $D7
    tay
    ror $D1
    lda ($CB),y
    adc ($D8),y
    .byte $54
    .byte $89
    .byte $BB
    rts
    .byte $53
    stx $33A7
    php
    asl $C7,x
    jsr $ABCD
    ora SOUNDR,x
    .byte $0F
    sta $4324,x
    .byte $0C
    lda $3E33
    .byte $03
    .byte $A7
    .byte $14
    .byte $2B
    .byte $0C
    ora $07A0,x
    .byte $7A
    eor $B8
    .byte $12
    ror $4B
    ldy $CD
    lda #$F9
    .byte $93
    lda $BE36,y
    lsr $8831
    bpl $6285
    inc $94
    .byte $93
    asl
    lda $A8C3
    inx
    sbc $9504,x
    inc $D24A
    pla
    .byte $82
    ror CHKSUM,x
    bne $625F
    ldx POKMSK,y
    ldx #$01
    .byte $33
    .byte $54
    eor $1D,x
    lda $A75F,x
    jsr $BCE0
    .byte $8B
    .byte $B2
    dec $7AC6,x
    .byte $6F
    .byte $A7
    and #$A8
    ora $C9B9,x
    beq $62BA
    txa
    .byte $67
    clc
    adc $74E2
    inx
    cpx #$8F
    cmp $3A93,y
    and #$9C
    .byte $4F
    .byte $14
    .byte $1B
    ror
    .byte $C7
    .byte $53
    cmp ($E2),y
    cmp $B11E
    eor $EDA0,x
    .byte $02
    .byte $04
    .byte $74
    ldx $4EAA
    dex
    bpl $62DF
    ror $A6D5
    .byte $14
    jmp $BDA4
    .byte $4F
    sta $A5C6,x
    .byte $7C
    inc $EFA5
    brk
    lda $221D,x
    ror
    bit $F562
    .byte $07
    bne $635E
    brk
    .byte $64
    and $7201,x
    sta $2115,y
    ldx $094C
    .byte $03
    adc $23C1,x
    .byte $A3
    asl $85
    ora $ED91,y
    ora $8AFD,x
    .byte $34
    .byte $3B
    cmp (CRITIC),y
    cli
    lda $19A4,x
    .byte $A3
    lda $83D2,y
    sta $8E27
    lda $AB
    .byte $12
    bne $630D
    .byte $1B
    sbc (ATRACT),y
    sta $785C
    .byte $0F
    ldy $B07C,x
    sbc $9134,y
    .byte $6B
    lda $CCA8
    .byte $89
    lsr $1E,x
    .byte $9E
    bpl $6394
    .byte $33
    .byte $80
    ror $6E05
    asl VCOUNT_START
    .byte $07
    .byte $FB
    php
    ldy #$80
    .byte $22
    .byte $03
    and (PM_DMA_CYCLES),y
    .byte $62
    asl LNFLG
    cpy $3120
    adc $9E,x
    jmp ($292F)
    ora $6A30
    .byte $A3
    adc $0712,y
    inc TRAMSZ
    ora ICBLHZ
    sbc ($AE,x)
    rol $5DD7
    dec $49,x
    .byte $FF
    .byte $37
    rol BUFRLO,x
    ora $1F00
    asl DSKUTL
    .byte $92
    .byte $7C
    .byte $0B
    ora LNFLG
    .byte $67
    .byte $27
    .byte $0C
    .byte $3F
    .byte $0C
    sec
    lda #$5D
    sbc SWPFLG
    adc $4A46,x
    ror $7E07
    sty $7D8E
    .byte $03
    .byte $C3
    sty $C3
    sbc #$30
    .byte $EF
    and #$2E
    .byte $67
    plp
    lda (ICHIDZ),y
    .byte $EF
    .byte $37
    .byte $F3
    lda FR0M,x
    sec
    sbc $181C
    adc RMARGN
    .byte $7A
    inc $C840,x
    rti
    asl $71FC
    .byte $3C
    cld
    lda $D21F
    txa
    rts
    txa
    .byte $0C
    dec ZTEMP3
    .byte $D4
    ldx $6344
    .byte $17
    tya
    .byte $43
    .byte $04
    inx
    cpx $5678
    bcc $63B2
    lda $A774
    .byte $44
    .byte $5C
    jmp $C5D9
    sta $B349
    tay
    ora ($D0,x)
    ora $78
    .byte $C3
    lda #$1A
    ora #$19
    bcs $63B7
    lsr $B2
    bcc $6419
    lsr $95A1
    ora ($19,x)
    ldy $06B6
    .byte $B2
    lda COUNTR
    cmp #$02
    .byte $0F
    dec $67
    sta $B5
    .byte $74
    eor $83
    inc $D485
    lsr ICBAHZ
    .byte $44
    ldy TRAMSZ
    rol $ACF8,x
    sec
    ldx $2664
    and ($B0,x)
    ora #$90
    bit $7D43
    ora $4116,y
    cmp ZCHAIN
    and ATRACT
    lsr CRITIC,x
    sta $628D,x
    inc $C793
    cmp $4B21
    bcs $63F7
    .byte $EB
    .byte $7C
    pha
    tsx
    adc $964C
    .byte $32
    .byte $53
    ldx $8D7E,y
    dec $1A94
    lda #$FA
    .byte $43
    tax
    .byte $6B
    .byte $47
    pla
    .byte $C3
    .byte $04
    inc $AD8D
    lsr $93
    cmp $4B4D
    beq $6439
    .byte $CB
    bcs $63EC
    dec $E9
    jmp $7EBD
    adc $7A,x
    lsr
    bcs $6485
    ldx COLRSH
    jmp ($7BA5)
    .byte $DA
    .byte $43
    .byte $47
    sta $BE4E
    .byte $CF
    jmp $978C
    .byte $E7
    .byte $9C
    beq $6437
    .byte $43
    cpy #$80
    .byte $DC
    sed
    .byte $44
    rts
    and #$40
    sta FLPTR
    .byte $33
    sei
    bvs $6496
    jsr $FE7C
    cmp $B37C,y
    sec
    ror POKMSK
    sta COUNTR
    .byte $33
    .byte $3C
    .byte $1C
    clc
    php
    .byte $7B
    dey
    rol $0C85,x
    .byte $04
    .byte $37
    bpl $649D
    asl VBREAK
    .byte $FF
    .byte $7F
    .byte $3F
    .byte $1F
    .byte $FF
    .byte $0F
    lda HOLD1
    beq $6480
    rol $9B,x
    .byte $EB
    sei
    ldy $548E
    .byte $DC
    eor ($5F),y
    .byte $9B
    sbc $FC16
    rol $7BC6
    ldy $097C
    .byte $DF
    asl $8A,x
    txa
    .byte $54
    .byte $3A
    lda $AC85,x
    sta OLDCHR,x
    .byte $E7
    cpx $6C75
    .byte $9B
    .byte $F3
    asl $03,x
    .byte $77
    cpy $0D7B
    adc $5F10,x
    .byte $1C
    .byte $82
    .byte $8B
    sty CMCMD
    sta (FR0M,x)
    sta $C10E
    lda $0E06,y
    bvc $647A
    .byte $4F
    and #$4C
    pha
    rts
    .byte $5B
    .byte $80
    bne $6506
    jsr $F724
    lda ($8E),y
    ora ($CA,x)
    ldx $307A,y
    tya
    .byte $23
    ldx $0E5E
    cmp $0B8A
    txs
    ora (ICCOMT,x)
    .byte $34
    bpl $6518
    .byte $64
    asl
    .byte $17
    rol CMCMD,x
    cmp $0170
    .byte $52
    asl
    .byte $32
    asl
    ror
    lda CMCMD,x
    lda $CA71,x
    inx
    .byte $C7
    inc $1DD6
    asl $6C2A
    .byte $63
    .byte $93
    .byte $92
    ora $4C67,x
    sta $26B5,x
    lsr $F639
    cpy $C3
    lsr $86,x
    ldx #$BD
    .byte $9F
    bit $FDE2
    jmp ($4BE1)
    and $98AE,x
    .byte $8B
    sta ($C1,x)
    ldy.w ROWAC
    .byte $BF
    .byte $7A
    .byte $C7
    cpy #$07
    inc $D24D,x
    inc $1016,x
    jmp $69FF
    ror $518E,x
    .byte $07
    .byte $02
    .byte $9B
    pla
    .byte $1A
    .byte $B2
    .byte $1C
    eor $1B
    and $163C,y
    .byte $FA
    and HOLD1,x
    lda $87C4,x
    lda $AF,x
    lda ($F6),y
    beq $65CB
    cpy #$61
    rol
    lda #$C3
    ldx BUFSTR
    .byte $1F
    .byte $A7
    dex
    .byte $17
    .byte $03
    ldy #$4D
    dey
    nop
    tsx
    sta $CC2D,y
    sta $F45A,y
    cpy $CB
    ldx #$07
    txa
    sta $A63D
    asl ABUFPT,x
    ora ($E8,x)
    .byte $6B
    bit $7C60
    .byte $80
    .byte $54
    cmp $DE
    asl
    ldy #$ED
    .byte $04
    .byte $32
    dec $7A
    asl.w FRE
    .byte $72
    lda $3CF0
    bvc $6514
    dex
    bit $843A
    .byte $9F
    stx $8A
    eor $6D18,y
    rts
    pha
    tay
    asl $9D
    cli
    nop
    lda $9D57,y
    .byte $04
    eor $8A26
    adc ICIDNO,x
    tax
    inc $ADE3
    .byte $89
    .byte $DC
    dec $9801,x
    nop
    dec $C05A
    .byte $80
    inc $6862
    .byte $B7
    eor ($DE,x)
    rts
    .byte $1B
    bpl $6606
    pha
    ldy $7DEF
    lda $69E8,y
    rol $C80E,x
    sbc $C4C0,y
    ldy $68F5,x
    .byte $C2
    asl
    tay
    .byte $1A
    sbc $E3B0
    beq $6635
    plp
    dey
    jmp $01B1
    jsr $0503
    bpl $65E2
    .byte $E2
    bpl $6608
    .byte $0C
    .byte $1A
    .byte $02
    jsr $14D2
    .byte $0B
    .byte $32
    .byte $07
    .byte $BF
    eor $03
    adc (NGFLAG),y
    brk
    .byte $FA
    ldy #$83
    .byte $14
    bne $6610
    ldy $EB58,x
    bcs $6588
    stx $76F0
    adc $24C6
    .byte $3B
    sta (RECVDN),y
    .byte $22
    ldy $151F
    .byte $F4
    rol DSKFMS,x
    ora ($94),y
    bne $6659
    rti
    .byte $64
    .byte $AB
    .byte $72
    .byte $B2
    cmp $C7
    iny
    .byte $02
    ror $C3D3
    dey
    .byte $3A
    tya
    ldy #$C8
    .byte $B7
    eor (LNFLG,x)
    lda ($9A),y
    ora ($B3,x)
    cpy CASBUF
    bpl $660F
    bit $4FF2
    .byte $64
    cmp ($D0),y
    .byte $5A
    and $D0
    sbc $E881,y
    lsr
    sta $757C
    dex
    .byte $82
    and $D1,x
    plp
    dey
    .byte $D7
    bvc $65E9
    stx $3C5D
    lda $B094,x
    .byte $8F
    .byte $B2
    rol
    bpl $65F2
    ora ($89),y
    .byte $12
    .byte $89
    .byte $13
    .byte $89
    .byte $14
    .byte $89
    ora $89,x
    ora $2902,x
    .byte $03
    .byte $B3
    .byte $6B
    and ($93),y
    asl $A1BA
    ldx $0AA4
    cmp $7AC0,y
    sta $525E,x
    clv
    cpy ICPTLZ
    ora $E3F3
    sbc ($BD,x)
    ror $B0,x
    dec $0461
    .byte $E7
    .byte $97
    sei
    asl
    tya
    .byte $32
    .byte $27
    lsr FR0M,x
    .byte $1F
    .byte $CB
    ora NOCKSM
    clc
    bit $D679
    .byte $0F
    .byte $0F
    lda (VCOUNT_START),y
    cmp $8DA7
    and $2313
    tax
    .byte $47
    pla
    .byte $34
    ora #$D2
    and #$3F
    adc $EE21,y
    .byte $6F
    bcs $66F2
    .byte $7B
    .byte $12
    .byte $D4
    adc ICBALZ,x
    cmp #$1C
    .byte $3F
    php
    bne $6719
    jmp $5C41
    clv
    .byte $CF
    .byte $D7
    cmp $8659,x
    .byte $57
    .byte $17
    ora $A5
    sei
    ora $B267
    .byte $3F
    .byte $53
    bmi $66B9
    .byte $EF
    asl DOSVEC
    adc ($82),y
    .byte $47
    ldy $30DA
    .byte $0F
    .byte $D3
    .byte $77
    .byte $07
    lsr $A4
    .byte $03
    .byte $72
    .byte $DF
    rts
    sbc $92
    ora $1DC8,x
    sty $B1E3
    lda KEYDEF
    bne $66E0
    and #$11
    cpy POKMSK
    asl $7A
    brk
    sbc #$A7
    .byte $82
    jmp $2F21
    jmp ($E67B)
    .byte $33
    inc $2B52
    .byte $27
    and #$6D
    .byte $5F
    bit $FB20
    .byte $6B
    lda HOLDCH
    .byte $3F
    ora $9D,x
    lda $A457,x
    and $87B9
    eor $E36C,x
    eor ICBLLZ,x
    sbc ($C9,x)
    sei
    asl $91,x
    .byte $83
    adc $9E31,y
    tya
    .byte $2F
    lda #$8C
    sed
    asl ICDNOZ
    .byte $42
    ror $BCDA,x
    clv
    cpx FCHFLG
    .byte $17
    lda $FB
    cmp $9069
    .byte $0B
    sbc $B218
    sta FEOF
    and SWPFLG
    cmp $F07D
    .byte $82
    cpy $B3E2
    bvc $66E3
    bit $7FC9
    adc $B8,x
    ora $7F
    .byte $B2
    sbc (DSKFMS,x)
    adc $13
    and $DFFE
    .byte $72
    .byte $1F
    cmp #$D0
    cpx #$84
    sta $FBEF
    .byte $34
    php
    adc #$77
    .byte $0F
    adc FCHFLG,x
    ora DELTAR,x
    jsr $EC52
    .byte $83
    and ($8B),y
    .byte $17
    rol DIGRT,x
    sei
    .byte $12
    lda ICPTHZ
    sty $B3
    .byte $62
    .byte $54
    jmp $CFAB
    .byte $3A
    ora $93,x
    .byte $64
    .byte $73
    inc $376C,x
    bcc $677E
    .byte $62
    eor ENDPT
    .byte $04
    .byte $9C
    clc
    asl $2193,x
    ora #$C5
    .byte $2F
    sbc (FR0,x)
    cpx #$14
    adc ($CB,x)
    .byte $F3
    sta ($CA,x)
    .byte $47
    jsr $803D
    inc $EB3E
    rts
    .byte $3F
    .byte $07
    .byte $BF
    .byte $E7
    .byte $A3
    bpl $672F
    lsr $B0
    ora $A203
    ora ICSTAZ
    .byte $92
    .byte $D7
    eor (TRAMSZ,x)
    lda $B770,x
    bcs $674D
    txa
    .byte $77
    beq $6754
    .byte $83
    dec $16,x
    cmp $749B
    lda COLRSH,x
    .byte $12
    .byte $92
    .byte $EF
    .byte $04
    .byte $AF
    .byte $9C
    sec
    .byte $DC
    .byte $32
    cpy $9084
    ror $98,x
    ldy $2083
    .byte $97
    sbc $80
    .byte $F2
    bpl $67D3
    .byte $1C
    asl
    rti
    eor ($AA),y
    lda #$2D
    sta $AF52
    brk
    .byte $EF
    sta $EAC2
    beq $6830
    .byte $7B
    cmp #$B1
    cmp $21DD
    .byte $6B
    .byte $3C
    .byte $EF
    bcs $686D
    .byte $1A
    beq $680D
    .byte $D7
    .byte $82
    plp
    .byte $2F
    .byte $5F
    lda (APPMHI,x)
    ldy $2B0E
    pha
    .byte $D4
    ora #$B5
    sta $33D9
    stx $9D4C
    .byte $C3
    dec ICIDNO,x
    ora $312E
    lsr $0D
    ldy #$23
    .byte $3C
    sta $06D1
    .byte $B3
    rts
    inc $F064,x
    .byte $1C
    ldx ICIDNO
    .byte $1F
    .byte $92
    ldx $A1AC
    jsr $7FD2
    lsr $62EC
    dec WARMST
    .byte $CB
    tax
    rts
    .byte $02
    .byte $12
    .byte $80
    lda $9A8D
    .byte $13
    cli
    and FR1M,x
    adc $CC,x
    sta $AFE9,y
    bpl $6850
    inx
    stx $B1
    cmp #$FF
    .byte $3F
    .byte $1B
    inc $1019
    .byte $72
    rol $844C
    sta COUNTR
    .byte $BB
    rti
    lsr
    lsr $5741,x
    cpy #$73
    rol
    pha
    asl $AA
    pha
    ora (DSKFMS,x)
    ldy $8B46,x
    bne $6873
    ora $4B
    inx
    .byte $72
    bpl $686D
    .byte $83
    bpl $686D
    dex
    ora $AD2F,x
    adc DOSINI
    tax
    lda ($69),y
    ror $0B,x
    sta $83
    rol
    sec
    jmp $FCDE
    clc
    rts
    .byte $B7
    bpl $68CC
    .byte $0C
    .byte $87
    beq $68B4
    and TMPCHR
    tay
    eor ($BA),y
    .byte $D7
    asl
    .byte $74
    sta $C06F,y
    .byte $17
    dec OLDCHR
    .byte $82
    .byte $67
    .byte $42
    txa
    eor $5375,x
    .byte $8B
    eor #$84
    asl ICSTAZ,x
    eor ($83),y
    asl $D3
    ora (ICAX1Z,x)
    rol DSTAT,x
    eor #$66
    bmi $68CF
    cmp PALNTS
    ldx $C685
    bcs $68AD
    .byte $DC
    asl $0E6F,x
    .byte $52
    ldy #$05
    .byte $A3
    .byte $C3
    jsr $B183
    beq $689D
    dey
    asl ZTEMP3
    ldx $7DBC
    .byte $3B
    .byte $17
    sbc #$9C
    stx $8D0F
    .byte $DA
    adc $060E,x
    dey
    .byte $5F
    .byte $1C
    rti
    lsr
    jmp $8564
    lda $7B5F,x
    sbc #$E4
    lda $4B43,y
    .byte $57
    sta ($B0),y
    .byte $07
    .byte $A7
    .byte $63
    rol $D8,x
    inx
    asl DOSVEC,x
    asl $A8,x
    cmp $D6BE
    ldx $0A84,y
    .byte $C7
    txs
    asl $8B,x
    asl $F034
    asl
    sta $F0B7
    .byte $53
    stx $190F
    inc $BA23
    ora $14,x
    dex
    .byte $0F
    sta $9D03,y
    and (VCOUNT_START),y
    .byte $D2
    .byte $DF
    rol MEMORY_REFRESH_CYCLES
    ora $CB,x
    .byte $57
    dec RECVDN
    .byte $7F
    cpy $CB46
    ora ICCOMZ
    .byte $83
    and $CA,x
    bcc $6925
    .byte $D4
    .byte $82
    cmp $7559,y
    sei
    and ($96,x)
    .byte $80
    .byte $1F
    bpl $6934
    bvc $69B1
    rti
    sty $A265
    .byte $1F
    .byte $33
    .byte $F4
    and #$65
    cmp OLDADR
    bvs $6970
    rti
    bpl $6965
    .byte $0C
    php
    .byte $02
    .byte $33
    .byte $03
    rol $620B
    ldx XMTDON,y
    .byte $F3
    .byte $04
    and ($65),y
    asl $1C30
    .byte $5C
    jsr $5C18
    jsr $5C1E
    .byte $44
    stx $205A
    pha
    ora $E375
    .byte $0B
    cpy $D0
    .byte $A7
    .byte $8B
    ldx $E4,y
    .byte $B2
    sta $2462,x
    sbc ($7A),y
    rol $0B32
    ldx $BC
    .byte $0C
    bpl $6942
    .byte $02
    dec $4811
    inc $C7
    .byte $43
    and (TRAMSZ),y
    cpx FRMADR
    .byte $C2
    .byte $B2
    .byte $17
    .byte $53
    .byte $0F
    tya
    .byte $D2
    eor $3184,x
    .byte $14
    tya
    txa
    .byte $87
    lsr $CA72
    inc $010F,x
    .byte $63
    dec RMARGN
    php
    iny
    rol $AC
    and ($C7,x)
    .byte $92
    .byte $B3
    .byte $72
    .byte $92
    eor ($8C),y
    .byte $D2
    bit FMSZPG
    clc
    adc $2537,x
    .byte $14
    tya
    .byte $14
    ldy $E8A9
    ora $9C05,x
    brk
    sty $89EA
    cli
    ora $CD25
    eor ($A4),y
    bcc $69C6
    sta $B2C9
    txa
    tay
    inc $3782,x
    ora $8824
    .byte $7A
    stx $C4
    ldy #$20
    lda $D8
    .byte $87
    ldx #$03
    sbc ($9F),y
    .byte $62
    cmp (BUFRFL),y
    .byte $54
    .byte $64
    bmi $6A3A
    ldy $AC70,x
    and ($56),y
    bcc $6A31
    bvc $6A2C
    sta VBI_SCANLINE
    lda $3FBE,x
    clc
    bmi $69C5
    ora (FREQ,x)
    .byte $34
    .byte $0C
    bit $F70F
    plp
    pla
    and ($03),y
    bvc $69A0
    clv
    .byte $B2
    .byte $E2
    and $19AE,y
    jmp $8753
    ldy $863E,x
    bmi $6A2E
    stx ZTEMP4
    txa
    adc $54AA,x
    bit $380A
    .byte $E3
    sbc $C6B7
    and $8899,y
    .byte $B7
    ldx $D6,y
    beq $69FA
    ldy KEYDEF
    lda $9DB3,y
    ldy $CB
    ldx ZTEMP4
    inc $0C39,x
    cmp $F996,x
    rts
    ldy #$03
    sta $BEFC
    .byte $D3
    .byte $7A
    ora QTEMP
    .byte $2B
    ror
    beq $6A49
    .byte $7B
    .byte $3F
    .byte $F3
    jsr $86C1
    lda $3FCA
    eor #$C2
    .byte $5A
    .byte $89
    sta $BE
    eor $E3,x
    .byte $F3
    cpy BUFRLO
    bne $69EA
    sta $F3BA,y
    iny
    bne $6AC3
    bcc $6A1E
    .byte $13
    ldx $E9CB
    rol $83C6,x
    lda $A321,x
    .byte $80
    plp
    .byte $42
    .byte $2B
    pla
    ora $939C,y
    adc $BACC
    .byte $63
    bcc $6A8C
    asl $5270
    ora $2938,x
    .byte $1F
    nop
    sty $57A8
    bpl $6A51
    php
    lda $FB96,y
    .byte $03
    .byte $A7
    adc $8C1F
    rti
    sbc $86A5
    sty $461C
    lda #$38
    sta $4372
    bit $F059
    lsr $9B
    .byte $07
    nop
    and (OLDCHR),y
    bne $6A47
    .byte $14
    ora $7202,y
    .byte $1B
    bmi $6A73
    .byte $77
    .byte $8F
    ldx #$B3
    ora ($A9,x)
    adc ($C5),y
    .byte $C3
    php
    cld
    ora $9944,x
    brk
    .byte $D2
    lda #$1E
    inc $37F1,x
    dec $1DCF
    .byte $0C
    .byte $0C
    bne $6AAD
    .byte $62
    ldy $1FD1
    .byte $B3
    stx FKDEF
    .byte $67
    .byte $5F
    rti
    beq $6B12
    .byte $22
    tay
    .byte $1B
    .byte $17
    bpl $6ADB
    .byte $B7
    ldy DSTAT,x
    .byte $2B
    dey
    cmp $1679
    bne $6AF0
    iny
    lda ($7A),y
    jsr $877D
    lda $BD
    lsr ESIGN,x
    .byte $32
    cmp $0B2E,x
    .byte $FA
    rts
    .byte $1C
    cmp (POKMSK),y
    bpl $6AFD
    ldy $8C7B
    .byte $D2
    .byte $1A
    ldx $69D7,y
    cmp #$2B
    sbc $D049,y
    sta $9D85,x
    .byte $FA
    inc $60EA
    tax
    beq $6B28
    lda $E29F,x
    .byte $89
    cmp (CRITIC,x)
    clv
    tax
    .byte $DC
    stx $29CE
    asl $20CD
    lsr $88EB
    ldy MEMORY_REFRESH_CYCLES
    rti
    sta $2624
    lda $A5AD,y
    ldy ICBLHZ
    .byte $BF
    sta $B295
    rts
    inc $A824,x
    .byte $A7
    ldx $A5
    ldy $A3
    .byte $BF
    ldx #$A1
    .byte $C2
    .byte $8B
    brk
    eor $BB02,x
    eor VCOUNT_START
    .byte $97
    pla
    asl $B5
    php
    sta $5AD4,x
    jsr $E70C
    pla
    .byte $B2
    lda CYCLES_PER_SCANLINE
    and $7610
    .byte $12
    sbc $4C16
    .byte $1C
    .byte $8B
    asl RMARGN,x
    .byte $37
    rol $8B
    rol APPMHI
    .byte $B3
    eor ABUFPT
    ldx $30B2
    dec $028D,x
    .byte $BB
    eor VCOUNT_START
    .byte $97
    pla
    ldy WARMST,x
    cmp $5AD4,y
    asl
    dec $ED0C,x
    asl APPMHI,x
    .byte $5C
    sta $D1
    .byte $12
    .byte $B7
    pla
    .byte $B2
    .byte $14
    sbc $1616
    .byte $B3
    tax
    .byte $5C
    .byte $8B
    .byte $1A
    ror $9F2E,x
    .byte $1C
    bit $741E
    pla
    .byte $F4
    .byte $02
    and $9A,x
    lda $B013,x
    php
    and $5DD4,y
    .byte $77
    .byte $0C
    ror
    .byte $9E
    and $750E
    bpl $6B7D
    dex
    and $1474
    and $59,x
    asl OLDADR,x
    sbc (DSKFMS),y
    adc BPTR
    .byte $17
    lsr DELTAC,x
    .byte $1C
    stx $7E1E
    .byte $7F
    ldy LNFLG,x
    sta $6A06,x
    .byte $CB
    .byte $07
    php
    sbc DOSINI
    .byte $D7
    eor ($9F),y
    ror $474F,x
    inc $0D
    brk
    .byte $67
    asl $67
    plp
    adc $C629,x
    and #$0C
    .byte $E2
    clv
    stx POKMSK,y
    asl $E5,x
    .byte $54
    .byte $17
    .byte $3F
    clc
    .byte $34
    sbc $4554,x
    brk
    .byte $3B
    .byte $FB
    txa
    bmi $6BF1
    .byte $12
    .byte $B3
    eor $B5
    .byte $0C
    asl DSKUTL,x
    lda $6A1C
    .byte $5B
    asl $A5AD,x
    .byte $93
    .byte $02
    ror COLRSH
    .byte $04
    .byte $8B
    asl $8E
    .byte $9B
    asl
    lsr $590C,x
    sed
    .byte $BF
    asl $D549
    and ($46),y
    bpl $6C7F
    .byte $EF
    eor COLRSH
    .byte $14
    .byte $8B
    lda $A8C5,x
    clc
    lda $B3,x
    cmp $A8
    .byte $77
    ora $BCC9,y
    lda #$58
    lsr ABUFPT
    .byte $EF
    .byte $FF
    ldy QTEMP
    .byte $BF
    asl $5841
    adc ($98),y
    sed
    dey
    bvc $6BB8
    sta ZTEMP1
    inc $3A02
    .byte $03
    .byte $04
    .byte $D2
    asl $56
    .byte $9E
    rol $3B05
    asl
    ora $03,x
    php
    and #$CA
    jmp $89FD
    bvs $6C03
    clv
    asl
    asl FTYPE,x
    and $B2,x
    eor $0D9A,x
    .byte $C3
    adc $9D,x
    .byte $9C
    sta $74C4
    .byte $1C
    ldy BPTR
    beq $6C7A
    .byte $5A
    cpy #$3F
    bcs $6C63
    tya
    and #$FE
    .byte $FF
    sta $CE
    ldy #$3E
    .byte $C3
    and $4054,y
    bmi $6CD2
    eor $01A9,x
    ldy $F584,x
    sta $DA8E,y
    asl ZTEMP4
    adc ($A6),y
    .byte $63
    sbc #$20
    and $8B,x
    .byte $3C
    asl ROWAC
    .byte $4F
    txs
    dec FREQ,x
    and ($B5,x)
    and #$8F
    inc $505C
    sec
    sbc ADRESS
    ror $A824,x
    .byte $DF
    cmp (FKDEF),y
    bit $8B
    brk
    .byte $62
    .byte $E2
    .byte $2B
    sta $3C71,y
    eor $4CE0,x
    .byte $8F
    txa
    rol $B1BA,x
    ldx ADRESS
    ldy #$29
    .byte $AF
    inc $C8
    stx $8CCC
    ora $E8
    cpx #$AF
    bcs $6CBD
    .byte $FF
    sbc ($A9),y
    inc $DE4B
    lda LOGCOL
    ldy #$8F
    .byte $FC
    sbc $AD79,y
    ldx $9EF5,y
    .byte $FA
    lsr $E4,x
    and #$31
    ora (ZCHAIN,x)
    .byte $C2
    ror
    .byte $9E
    ldy $1D
    .byte $53
    lda $F997,y
    ror $A6,x
    rts
    .byte $A7
    bit $5F
    sbc ($A9),y
    .byte $17
    .byte $43
    cmp ($98,x)
    lda $BEA2,y
    beq $6D3C
    inx
    lda ($7F),y
    eor $BC,x
    dec $70DC,x
    .byte $14
    bit OLDADR
    sbc (CMCMD),y
    sta (ATRACT),y
    bit $6120
    sta $626E,x
    cpy LMARGN
    .byte $8F
    ldx #$38
    clv
    .byte $6B
    sbc $8528
    asl FRMADR
    inc FRMADR
    cpx CMCMD
    .byte $62
    jsr $8D54
    .byte $8F
    eor $4C7A,y
    sta $8B,x
    .byte $A7
    ldx FCHFLG
    php
    lda ($FA),y
    and #$0F
    ora OLDCHR
    sta (FPTR2),y
    .byte $C3
    eor (ROWAC),y
    bvs $6CF6
    lda $1E26
    .byte $14
    .byte $FA
    adc $B0,x
    ldx #$39
    adc ($56),y
    rol
    adc #$2F
    cpx $DC
    .byte $77
    adc $C6
    .byte $53
    .byte $C3
    cpx $A5AC
    txa
    rts
    .byte $74
    .byte $6F
    sbc #$51
    .byte $63
    .byte $D3
    bvs $6D42
    sty $0F
    bpl $6DCB
    ldy $75
    sty RMARGN
    .byte $D2
    asl
    rol ICSTAZ
    .byte $1A
    .byte $02
    ldx $A113
    adc FR1M
    .byte $54
    sbc $BC7C,x
    bit $0B30
    adc (POKMSK),y
    ora (SHFAMT),y
    .byte $0B
    ror $8B,x
    asl $5428,x
    .byte $C2
    and ($C5,x)
    sbc $180A,x
    eor ($EA),y
    rol $5236,x
    .byte $83
    ora ($CC,x)
    .byte $3F
    .byte $0F
    .byte $2F
    .byte $07
    .byte $37
    .byte $17
    .byte $9F
    .byte $27
    .byte $13
    .byte $2B
    .byte $03
    .byte $3B
    .byte $1B
    .byte $23
    .byte $0B
    .byte $FF
    .byte $33
    ora #$35
    ora ICBLHZ,x
    ora RECVDN
    ora $25FF,y
    ora (BPTR,x)
    ora $1121,x
    and $FF0D
    and (APPMHI),y
    bmi $6DBC
    plp
    asl BUFRFL
    .byte $1A
    .byte $FF
    bit CASINI
    .byte $3C
    asl
    .byte $34
    .byte $12
    bit $FF1C
    .byte $22
    php
    rol POKMSK,x
    rol $2618
    .byte $04
    .byte $FF
    .byte $3A
    .byte $14
    rol
    asl $0C20,x
    .byte $32
    brk
    .byte $FF
    rol $3B58,x
    .byte $AF
    sed
    .byte $FF
    .byte $B2
    .byte $4F
    clv
    dey
    .byte $9F
    .byte $B3
    iny
    cmp $6439,x
    .byte $5B
    ldx FEOF,y
    .byte $62
    .byte $E2
    lsr $637C,x
    .byte $57
    eor #$B9
    adc $58C4,x
    and $C98A,y
    .byte $02
    bcs $6DEE
    lda #$18
    ldx $C95A,y
    bcc $6E46
    .byte $0C
    sec
    sbc #$08
    .byte $FB
    .byte $97
    .byte $5B
    bcc $6D9E
    adc $99,x
    sbc $B0,x
    .byte $BF
    sec
    .byte $1C
    .byte $0B
    .byte $62
    jsr $E508
    jmp $3117
    .byte $C2
    .byte $33
    lda #$0C
    sta OLDROW
    .byte $0F
    .byte $7A
    .byte $D7
    tax
    eor $BD03,y
    .byte $72
    sty $DF99
    and $23CA,x
    .byte $F4
    sbc (LNFLG),y
    .byte $3A
    cmp #$EF
    .byte $9C
    lda (DINDEX),y
    bpl $6E43
    ldy #$02
    .byte $3F
    sta $5009,x
    bpl $6E76
    sta ($CB),y
    .byte $EF
    lda #$A6
    clc
    jmp $657B
    sty $E385
    .byte $72
    eor $84
    cpy $F6
    .byte $C2
    .byte $DC
    .byte $03
    tax
    lda $BE7C,x
    sta ($B9),y
    txa
    .byte $42
    lda OLDADR
    .byte $E3
    sec
    adc $E2
    rti
    adc $9CD2,y
    .byte $5A
    dec HOLD1
    bne $6E17
    rts
    .byte $7F
    eor ICCOMZ,x
    tax
    .byte $97
    brk
    cmp $BB33,x
    .byte $03
    .byte $07
    .byte $9C
    sty $0DFF
    sta $18C4
    pha
    ora ($B4,x)
    bvs $6E72
    cld
    .byte $04
    rti
    .byte $1A
    .byte $43
    .byte $3C
    cmp $C0
    .byte $42
    brk
    .byte $1C
    bvs $6E8D
    jsr $34A3
    tya
    .byte $03
    asl $96AE
    tax
    .byte $9F
    jmp $AA19
    stx $44
    cli
    txs
    sbc FREQ
    tya
    .byte $7B
    ora (ICBLLZ,x)
    inc $124C,x
    .byte $44
    .byte $1A
    .byte $F2
    stx $A9,y
    eor $3E44
    .byte $B3
    lda NOCKSM
    .byte $2F
    .byte $F4
    sec
    .byte $22
    .byte $34
    .byte $D7
    bmi $6E6D
    rol
    eor STATUS
    stx $2812
    bcc $6F0C
    .byte $1A
    txs
    .byte $34
    and (FLPTR,x)
    ldx #$AA
    sta $C9,x
    ldx $102A
    .byte $BF
    rol
    .byte $0C
    sec
    .byte $03
    .byte $B7
    ldx BFENLO
    .byte $17
    ldx $9BBD,y
    rti
    clv
    .byte $AF
    .byte $F4
    sty $97,x
    lda $A4,x
    sta $1148,y
    .byte $7A
    .byte $47
    .byte $97
    .byte $44
    dey
    .byte $7A
    .byte $9C
    ldx #$A0
    .byte $89
    ldx BUFRFL
    asl $9149,x
    .byte $3C
    cpy $217C
    .byte $27
    eor $DDAD,y
    .byte $47
    .byte $B2
    bne $6EFE
    sbc $625E,y
    stx $4C47
    and #$DC
    .byte $7B
    .byte $7F
    rol $8F5B
    adc $C8
    lda #$27
    .byte $77
    .byte $1B
    .byte $D7
    .byte $1B
    sty POKMSK,x
    rts
    beq $6F4E
    lda ROWCRS
    sta $D8F7
    .byte $57
    .byte $B2
    .byte $54
    cmp $5958
    .byte $12
    ror $A7
    ldy $96A1
    tay
    .byte $4B
    lda ICBAHZ
    .byte $F7
    .byte $92
    ldx #$C9
    lda $B3F8,y
    .byte $D7
    sta $90A6
    sbc $5BFF,y
    ora ($EB),y
    .byte $1B
    .byte $6B
    .byte $63
    clc
    .byte $EF
    .byte $4F
    eor $19,x
    .byte $9F
    ldx RMARGN
    lda $7AA0,y
    cmp $B05E,x
    asl
    ldx $21A7,y
    stx $9A
    eor (DSTAT),y
    .byte $0B
    .byte $A7
    stx $A4BD
    sed
    .byte $BF
    .byte $AB
    .byte $37
    sty OLDCHR
    inc RMARGN
    cmp $BF
    tay
    .byte $1F
    ldx SAVMSC
    sta $9D
    asl
    rol $A35E
    .byte $02
    lsr DSKFMS
    adc DINDEX
    tay
    bcc $6F6D
    inx
    stx $FF
    lda #$BD
    rts
    cpy $0C87
    nop
    sty $688B
    .byte $5A
    sta $DAB7
    lda #$9D
    sta ($59),y
    lda $7F
    bit $44
    clv
    .byte $52
    dec $D7
    bvs $6F87
    .byte $2B
    cmp $91,x
    adc $2CAD
    adc #$5E
    eor $A6,x
    sta $881B
    jsr $8E5E
    .byte $6F
    dec ROWCRS
    cpx $A9AD
    sta $738C
    ror $9B
    .byte $34
    cmp $CDA6,y
    rol ICBALZ,x
    .byte $1F
    cmp $D36C,y
    ror $9B
    .byte $34
    cmp $C4A6,y
    .byte $9B
    asl $9A2D,x
    jmp ($66D3)
    .byte $9B
    .byte $34
    cmp $C4A6,y
    .byte $9B
    ora $9A2D,x
    jmp ($66D3)
    .byte $9B
    .byte $34
    cld
    .byte $93
    .byte $1C
    adc $B3
    eor $6C9A
    .byte $D3
    ror $9B
    .byte $34
    cld
    .byte $93
    .byte $1B
    adc $B3
    eor $6C9A
    .byte $D3
    ror $9B
    .byte $12
    jmp ($B61A)
    adc #$B3
    eor $6C9A
    .byte $D3
    .byte $62
    eor $9619
    cmp $6936
    .byte $B3
    eor $6C9A
    .byte $D3
    .byte $62
    eor $9B8F
    .byte $34
    cmp $CDA6,y
    rol $69,x
    lda (ICPTLZ),y
    .byte $17
    .byte $CB
    ror $9B
    .byte $34
    cmp $CDA6,y
    rol $69,x
    lda (ICPTLZ),y
    asl $CF,x
    ror $BB
    and QTEMP,x
    ldx $76CD
    and BUFADR
    cmp $D7EC,y
    ror $BB
    and QTEMP,x
    ldx $BBC4
    .byte $14
    and $EC9A,x
    .byte $D7
    ror $BB
    and QTEMP,x
    ldx $BBC4
    .byte $13
    and $EC9A,x
    .byte $D7
    ror $BB
    and $D8,x
    .byte $97
    .byte $12
    .byte $67
    .byte $B3
    eor $EC9A,x
    .byte $D7
    ror $BB
    and $D8,x
    .byte $97
    ora (ADRESS),y
    .byte $73
    ror $9B
    .byte $34
    cmp $CDA6,y
    rol ICBALZ,x
    .byte $2F
    cmp $D36C,y
    ror $9B
    .byte $34
    cmp $C4A6,y
    .byte $9B
    rol $9A2D
    jmp ($66D3)
    .byte $9B
    .byte $34
    cmp $C4A6,y
    .byte $9B
    and $9A2D
    jmp ($66D3)
    .byte $9B
    .byte $34
    cld
    .byte $93
    bit $B365
    eor $6C9A
    .byte $D3
    ror $9B
    .byte $34
    cld
    .byte $93
    .byte $2B
    adc $B3
    eor $6C9A
    .byte $D3
    ror $9B
    .byte $12
    jmp ($B62A)
    adc #$B3
    eor $6C9A
    .byte $D3
    .byte $62
    eor $9629
    cmp $6936
    .byte $B3
    eor $6C9A
    .byte $D3
    .byte $62
    eor $9628
    cmp $6936
    .byte $B3
    eor $6C9A
    eor #$27
    .byte $B2
    cmp $CDA6,y
    rol $69,x
    .byte $B3
    eor $6C9A
    eor #$26
    .byte $B3
    cld
    ldy $4CB0
    dey
    jmp $4C60
    sec
    jmp $2510
    .byte $4F
    inx
    cli
    cpy #$98
    tya
    tya
    bvs $7065
    pha
    tya
    jsr $9E24
    sed
    lda ($D0),y
    and ($A8),y
    and ($80),y
    and (SAVMSC),y
    and (STATUS),y
    and (WARMST),y
    .byte $23
    and $62E0,x
    clv
    .byte $62
    bcc $7149
    pla
    .byte $62
    rti
    .byte $62
    clc
    .byte $22
    .byte $7A
    beq $70B4
    iny
    cpy $A0
    cpy $78
    cpy TMPCHR
    cpy ICBLLZ
    sta $21A5,x
    rts
    lsr $A2,x
    brk
    brk
    .byte $9B
    .byte $EF
    .byte $CF
    .byte $8F
    inc $DFFC,x
    sed
    lda CYCLES_PER_SCANLINE
    beq $7137
    inc $2FE6,x
    eor $9291,x
    .byte $1F
    ldx $0FFF,y
    sed
    .byte $CF
    bvc $7189
    adc $90CF,x
    ora ZTEMP4
    sec
    .byte $62
    .byte $7C
    sta $38F7,y
    ora $4361,x
    sta ($8F),y
    dec OLDCHR
    .byte $FF
    .byte $7A
    bit $2699
    brk
    cld
    bcs $70BE
    rts
    sec
    .byte $1F
    bpl $7123
    cpy #$98
    bvs $7187
    jsr $FFF8
    bne $70EC
    .byte $80
    cli
    bmi $7150
    cpx #$B8
    .byte $FF
    bcc $71B5
    rti
    clc
    beq $7119
    ldy #$78
    .byte $FF
    bvc $717E
    brk
    cpx #$0F
    .byte $82
    php
    asl $0D21
    .byte $82
    php
    .byte $0C
    and ($0B,x)
    .byte $82
    php
    asl
    jsr $8209
    clc
    php
    jsr $8207
    clc
    asl ICHIDZ
    ora $82
    php
    .byte $04
    and ($03,x)
    .byte $82
    php
    .byte $02
    and ($81,x)
    .byte $5F
    stx NGFLAG
    jsr $9FA0
    .byte $A7
    tax
    .byte $13
    ldy #$0D
    cmp #$FF
    asl $C214,x
    .byte $F3
    clv
    rol BUFRHI,x
    cpy #$09
    .byte $9C
    .byte $12
    .byte $E3
    lsr $A6E1,x
    ora $8C,x
    .byte $0F
    and ICIDNO
    bit $C7
    .byte $1A
    adc (ABUFPT,x)
    .byte $5C
    adc #$8B
    .byte $97
    lda $5A62
    .byte $34
    rol
    bcs $719D
    plp
    ldx STATUS
    cmp $31B5
    pla
    .byte $F2
    clc
    inc CIX,x
    .byte $87
    ldx $3EB1,y
    .byte $BB
    .byte $A7
    and $8E1B,x
    ror $8DBA,x
    bpl $7183
    sbc $DE,x
    lda $EBAE
    .byte $07
    lsr
    adc $CB06
    .byte $22
    .byte $03
    sta $9E14
    ora #$E0
    and ($DB,x)
    bpl $7238
    lda ICSTAZ
    .byte $EB
    .byte $0F
    ora $85
    lsr $32AC,x
    .byte $3F
    bmi $71EA
    bcs $7200
    cpx $57C2
    sta ($0D,x)
    and #$B9
    bcs $7187
    cpx #$0A
    ldx $4B94,y
    adc $D292,y
    ldy $9CE1,x
    .byte $7B
    rts
    sty $BE35
    asl GRAFP1,x
    asl
    stx $38D6
    .byte $D2
    iny
    tax
    .byte $62
    cmp BUFRHI
    .byte $92
    iny
    .byte $13
    rol
    sbc $9281
    tya
    sta ($D0),y
    .byte $74
    .byte $80
    .byte $5C
    lda $6C0C
    .byte $23
    bcc $71C5
    php
    cpx $86
    jmp $928C
    lda $5AF2,y
    brk
    asl
    .byte $8B
    .byte $2F
    adc #$B1
    dey
    bcs $71DB
    .byte $04
    ora ($E3),y
    .byte $07
    clv
    .byte $03
    tax
    .byte $8B
    .byte $CB
    sta $3500,x
    sta $2DAC,x
    and (PM_DMA_CYCLES,x)
    .byte $FA
    cld
    .byte $0C
    jsr $88AF
    .byte $5C
    .byte $1C
    .byte $23
    .byte $B7
    ldy #$7C
    .byte $42
    cld
    asl $FF
    .byte $80
    sta (BUFRLO),y
    cpx #$5E
    cmp $E7,x
    .byte $93
    lda $B15F
    .byte $AF
    rol $B7
    asl
    brk
    inx
    sty ICHIDZ
    cpx #$92
    .byte $EF
    inc $E7
    .byte $5B
    .byte $C7
    cmp #$86
    lda #$87
    ora #$40
    ora $8B,x
    cmp #$7A
    .byte $93
    .byte $C7
    .byte $E2
    tax
    jmp $DCFD
    eor #$92
    .byte $9C
    jsr $9F68
    .byte $7C
    .byte $F4
    bvc $7302
    .byte $5C
    rol FKDEF,x
    .byte $0C
    .byte $1A
    .byte $5B
    asl DSKUTL
    sec
    jmp $8EC1
    .byte $4B
    .byte $1B
    .byte $5F
    .byte $63
    bit $B6
    .byte $17
    and $A5,x
    rol $AA5A
    lsr
    .byte $22
    ldy $AB,x
    sta DSKUTL,x
    .byte $54
    ora #$6C
    .byte $37
    clv
    and STATUS,x
    .byte $43
    .byte $37
    jsr $E637
    adc $AF8D,x
    .byte $BF
    ora (ICCOMZ,x)
    ldy BFENLO,x
    .byte $C2
    lda $C3D8
    brk
    stx $FF
    lda $AE
    cmp #$83
    .byte $3C
    cpy $FF
    eor (ICPTLZ,x)
    ora $9738
    .byte $53
    lsr
    and #$DE
    .byte $DC
    adc $ABDE,x
    ldx $E87D,y
    bvs $7335
    plp
    adc $5B24,y
    clc
    adc $2E68,y
    bcs $7346
    adc ($B9,x)
    inc $1D0A
    dey
    .byte $53
    .byte $80
    lda $1A0A,y
    .byte $0B
    sbc $1F
    sta $F005,y
    .byte $0B
    .byte $EF
    .byte $54
    .byte $3C
    tsx
    and $CB
    .byte $64
    cmp #$FB
    bvs $728A
    .byte $04
    bpl $730D
    sbc (CASINI),y
    bmi $7389
    jmp $93BF
    ldx $8320,y
    sta $68B8,y
    rts
    cli
    cmp $0C00,y
    .byte $63
    .byte $72
    inc $0B,x
    and $6341,y
    ora ($DC,x)
    .byte $74
    ora $1B07
    lda $0B,x
    bvc $739F
    jmp $43EA
    .byte $93
    dec $AF5C
    sbc (ICCOMZ),y
    ora MEMORY_REFRESH_CYCLES
    .byte $8F
    lsr $7CB0
    rts
    cmp #$AF
    bcc $7322
    .byte $9C
    .byte $7C
    sta ($88),y
    .byte $12
    inx
    .byte $64
    sta $1E6D,y
    lsr $93C3,x
    .byte $1F
    .byte $80
    .byte $1C
    bvc $7333
    ora ($C7),y
    pla
    cpy #$0C
    lda $1CDF,y
    .byte $5C
    .byte $1C
    tya
    and $869D,y
    inx
    dey
    bpl $7348
    .byte $9E
    brk
    lda $E005
    lda (DELTAC),y
    stx $1F19
    eor FMSZPG
    .byte $7C
    ora $7DAE,x
    .byte $7C
    sty BUFRFL
    sta $701A,x
    .byte $B2
    sbc $261E,x
    bcc $7386
    lda $6894
    rol
    lsr $8E94,x
    and DSKINV,x
    .byte $14
    stx $6BF0
    jmp $0AAD
    lsr APPMHI,x
    dec $14
    plp
    inc $A0
    .byte $03
    sty $7F57
    plp
    and $48
    .byte $57
    ora ICBLHZ
    cpy #$36
    rts
    sta ($1D,x)
    txa
    jmp $1913
    ora (FR0M),y
    stx DSTAT
    ora ($D0,x)
    sty $FA,x
    .byte $42
    .byte $D4
    .byte $42
    .byte $07
    sty $1EAC
    adc ($99),y
    cpy #$CD
    lsr FREQ,x
    cmp $B394,y
    ldy $8457
    cmp $75,x
    .byte $E2
    .byte $04
    stx $94
    cld
    eor (APPMHI),y
    .byte $AB
    .byte $63
    eor #$12
    lsr ADRESS,x
    .byte $92
    asl $AD,x
    .byte $62
    and $56
    .byte $1C
    sta $8D
    ldy BUFSTR,x
    rol
    dec $4735,x
    .byte $FA
    .byte $62
    .byte $43
    .byte $CF
    .byte $8B
    .byte $77
    bit BUFSTR
    sbc $B0,x
    .byte $64
    rol $9D
    .byte $5A
    dec $83
    inc $89
    ldy $75
    .byte $3C
    dey
    cld
    ora ($C6),y
    .byte $8F
    .byte $5F
    and #$7D
    .byte $97
    ldx $153C,y
    .byte $FA
    cpx $0D
    and #$4C
    .byte $32
    sta $A5,x
    .byte $64
    sed
    .byte $74
    .byte $5C
    sta $205F,y
    sed
    .byte $9B
    .byte $4F
    stx $B77A
    sbc $C543,y
    .byte $3F
    .byte $8F
    .byte $1B
    bcs $7459
    cmp $BB
    beq $7458
    ora $1064,x
    bmi $73EF
    ror $3C87
    and #$E2
    asl $538C
    bcc $7454
    cmp #$AD
    bcs $73F5
    asl $BAF9,x
    rti
    adc $ECE6
    pla
    ldy $9831,x
    .byte $67
    .byte $37
    cmp TMPCHR
    cmp $D3E6,x
    asl $1E60
    .byte $1A
    .byte $7B
    .byte $1C
    ora $3BBD
    .byte $D4
    rti
    .byte $E3
    pla
    cmp #$C0
    .byte $F3
    sbc $028F,x
    .byte $AF
    .byte $44
    .byte $87
    stx $C6
    stx $B285
    rol $AB7C,x
    .byte $77
    sbc $C6,x
    rol $AC
    stx $59BD
    rol
    ldx $58FC,y
    sbc $14,x
    .byte $17
    bpl $740F
    .byte $14
    bcs $7499
    .byte $57
    adc (RAMTOP),y
    jmp $A3F5
    sta $8C,x
    bne $74FD
    .byte $0F
    inc $8B
    bvc $74F2
    rti
    adc $044D,y
    .byte $97
    .byte $57
    bcc $7482
    sta $68E7
    lda $8F
    sbc (ADRESS),y
    asl $59
    .byte $80
    adc $98
    .byte $3A
    lda (ICPTHZ),y
    eor ($89),y
    clc
    cmp (ICBLLZ),y
    .byte $54
    adc $6A6F,x
    and ICBLHZ
    stx $9D,y
    .byte $5C
    .byte $4B
    ora ($C8,x)
    cpy #$2D
    bne $7486
    .byte $3F
    sta (POKMSK,x)
    cpy $9D5A
    .byte $12
    .byte $54
    .byte $8F
    lda #$71
    ldx $A9C4
    .byte $04
    and #$05
    adc HOLD1,x
    cmp $894E
    and $4508
    ora #$0A
    .byte $8B
    .byte $0B
    eor $2D,x
    .byte $0C
    eor $99
    stx $0F10
    ldx $3038
    .byte $32
    .byte $53
    txa
    lsr
    tax
    inc $D1F6
    adc #$5A
    ror $584A
    .byte $BB
    stx SHFAMT,y
    lda $968C,x
    beq $74FC
    sta $6D
    .byte $8F
    .byte $3A
    cmp #$29
    .byte $22
    inx
    .byte $3C
    ldy $7EAC,x
    .byte $52
    lda $BE7B
    sbc $1F51,x
    sta $C6
    sec
    jmp $969E
    .byte $AF
    .byte $74
    plp
    ora (NGFLAG,x)
    jsr $9002
    .byte $44
    .byte $13
    bvs $7543
    adc DOSVEC
    lda #$97
    sta FRMADR
    .byte $CF
    ldy #$CE
    jsr $96B6
    stx ROWAC
    sbc $8381,y
    .byte $0C
    .byte $5A
    ora $9447
    lda $A26C,y
    brk
    .byte $C7
    lda SHFAMT
    .byte $F2
    and $E990,x
    cmp #$A3
    bcs $7521
    sty $FF
    ror
    ldx $96AD
    .byte $EF
    clv
    .byte $14
    .byte $04
    tya
    ora #$97
    lsr $C5
    sty $C6CB
    asl $C0
    ror $F231,x
    cpy $ECC8
    .byte $F2
    lda $6D
    and #$CF
    beq $7575
    adc DIGRT
    asl $BA67,x
    dec FRMADR,x
    lda #$DC
    .byte $3A
    sbc SHFAMT
    .byte $5A
    and ($85,x)
    .byte $5B
    ldy RAMTOP
    sta $BE50,y
    .byte $9F
    bmi $756D
    stx FR2,y
    .byte $43
    bcc $758B
    cmp #$9D
    .byte $5F
    bcs $758C
    txa
    tay
    sbc $053A,x
    jmp ($C731)
    .byte $67
    ldy CIX
    sta ($65),y
    inc BUFCNT
    sed
    dec $A5E4
    ror $7D0A
    .byte $8B
    adc $60AA,x
    .byte $CF
    .byte $E3
    bpl $759F
    .byte $1C
    asl $82
    .byte $13
    ldy #$CC
    asl FREQ
    .byte $9B
    sec
    .byte $62
    bcs $75AC
    .byte $80
    inc $7C82,x
    .byte $17
    ldx #$04
    ldy #$20
    .byte $FF
    .byte $42
    ror $8081,x
    brk
    lda $794E
    .byte $DF
    inc $63B1
    ldx $EE4F
    lsr $72C8,x
    .byte $62
    clv
    .byte $97
    bmi $75E3
    .byte $CF
    ldx #$03
    cmp $35E2,x
    asl $A38A
    lda $8809,x
    sbc ($B0),y
    .byte $53
    jmp ($7137)
    adc $19,x
    nop
    rts
    dec FR1M
    .byte $02
    .byte $82
    brk
    ora $84,x
    ora ($85,x)
    .byte $73
    .byte $0F
    stx $7F
    asl $AC76
    cpx #$B2
    .byte $FB
    .byte $07
    .byte $93
    bmi $766F
    .byte $77
    lda $69
    adc $4917,x
    ldx RAMTOP
    dex
    .byte $3F
    .byte $64
    .byte $D4
    .byte $0F
    ldx ATRACT,y
    .byte $77
    stx TOADR
    .byte $77
    .byte $64
    cld
    stx BFENHI,y
    and ($65),y
    asl SWPFLG
    .byte $5F
    .byte $3F
    .byte $0B
    asl $6BA5,x
    adc $0BD3,x
    .byte $17
    .byte $FF
    .byte $3A
    cli
    txa
    cpx BUFRLO
    sbc #$E9
    ora (DOSINI),y
    cmp ($F6),y
    ora $16
    ldy FEOF,x
    .byte $23
    txa
    .byte $53
    .byte $3A
    dey
    sbc (LTEMP,x)
    txa
    sec
    tax
    .byte $33
    .byte $D2
    .byte $37
    lda $7D3B,y
    sta RECVDN
    .byte $77
    asl $51A4
    dec $03
    rol BUFADR,x
    lsr $AB,x
    tsx
    rol $A5AA,x
    .byte $EB
    ldy $4699
    .byte $EF
    .byte $0F
    .byte $27
    bcc $765C
    bne $7667
    cpx COUNTR
    bcs $7667
    sty COLCRS
    .byte $6F
    eor ($86),y
    adc $EE53,y
    tax
    inc $C6DD
    eor (POKMSK),y
    .byte $3F
    cmp $A8
    ora ICAX1Z
    eor $A9,x
    lda ($E2,x)
    .byte $54
    tax
    jsr $93A9
    txs
    .byte $1C
    cpy BFENLO
    ldx $248D,y
    .byte $CF
    eor $ED9B,y
    .byte $83
    lda ($B0),y
    .byte $02
    .byte $BF
    .byte $A3
    cmp #$23
    bcc $7697
    beq $7697
    ror $2068,x
    sei
    ldx $6326
    ror $6D,x
    cpy ZCHAIN
    .byte $34
    clc
    sec
    sbc #$87
    .byte $1C
    lda (SWPFLG,x)
    .byte $D4
    dey
    ldx $C9,y
    .byte $64
    sta $AD93
    asl $16
    .byte $62
    .byte $9B
    ora $A666,x
    .byte $22
    cmp #$04
    ror $7A,x
    eor (FRX),y
    clv
    eor $20CE,x
    cmp $E798,x
    cli
    sta (TOADR,x)
    rol
    sta $4167
    lda $5F84
    lda ($8A),y
    .byte $07
    .byte $03
    rts
    .byte $23
    sbc #$30
    and WARMST
    sta $1088,y
    .byte $D3
    .byte $FB
    .byte $A3
    nop
    .byte $62
    lda $19A2
    iny
    lda RMARGN
    beq $775B
    .byte $BF
    .byte $0C
    ror $ADA8
    lda ($B1,x)
    .byte $0F
    ora ($B4),y
    and POKMSK
    dec $2E28
    .byte $34
    eor $48,x
    txa
    .byte $5A
    dey
    pla
    jsr $991E
    sbc ($DF),y
    .byte $8B
    ora $65
    cmp #$10
    sbc $1ACB,y
    .byte $53
    ldy #$00
    sbc $78D9
    .byte $03
    ldx #$0F
    sec
    inx
    sbc #$0A
    .byte $BF
    bcs $7711
    adc #$1A
    sbc (WARMST),y
    sbc (BUFADR,x)
    plp
    adc $C934,y
    plp
    .byte $1F
    ora $DA28
    asl $CE
    .byte $33
    ldx PUPBT1,y
    tya
    jsr $9A2F
    ldx $AC
    dec $196B,x
    and (LOGCOL,x)
    adc BUFRFL,x
    lsr
    ora ($AC),y
    ora #$9E
    .byte $C2
    sta $D999,x
    jmp $DB0A
    asl $0390,x
    jsr $1F19
    .byte $9B
    dec $75
    bpl $7710
    rts
    .byte $FF
    adc $E81E
    sty.w $00CE
    sta MEMORY_REFRESH_CYCLES,x
    .byte $D2
    .byte $9B
    .byte $04
    adc (ICHIDZ,x)
    .byte $52
    .byte $9B
    rts
    sbc POT2,y
    ldx $A735
    php
    .byte $9C
    .byte $7F
    ldx $0832,y
    sta ($C8),y
    adc CMCMD,x
    eor $9C63,x
    sec
    .byte $03
    bne $777E
    inx
    asl $C14C
    dec $90
    .byte $F4
    cmp (BRKKEY,x)
    adc (BUFRFL,x)
    lda ($B2),y
    .byte $93
    .byte $8B
    ror $9A,x
    .byte $1A
    adc (ROWCRS),y
    tax
    jsr $F603
    sta $D0ED,y
    cmp (TRAMSZ),y
    .byte $F2
    cmp #$58
    .byte $9C
    .byte $A3
    ldy $E4CA,x
    and $8F3B,x
    .byte $8F
    .byte $12
    .byte $3F
    .byte $FF
    .byte $1F
    sbc $540A,y
    cpy $CA04
    jmp ($4E11)
    .byte $0B
    jsr $6559
    sta ($59,x)
    jmp $0C32
    bit $2482
    .byte $B3
    .byte $D7
    clc
    ldy #$B4
    sec
    .byte $5C
    lsr $CA
    .byte $DA
    .byte $F3
    rol
    lsr $5454
    .byte $DC
    adc $1AB6,x
    txs
    rol $516C
    brk
    adc $110A,y
    rol $BE
    .byte $89
    ora STATUS
    .byte $12
    clc
    dec $80B0
    adc $AC
    jmp $9A74
    .byte $87
    .byte $13
    sec
    adc $C47D,y
    .byte $33
    txs
    .byte $14
    .byte $54
    sbc $56
    sta $C39F,x
    rts
    tya
    .byte $87
    lsr $BA84
    txa
    sta ($C5,x)
    sta $8B9A,y
    ora $82
    ldx NGFLAG
    .byte $9C
    .byte $03
    .byte $0C
    sta (ICAX1Z),y
    asl DSKFMS
    bne $780A
    .byte $C2
    .byte $3F
    lsr $B1,x
    jmp (RMARGN)
    lda #$28
    .byte $FF
    .byte $14
    rts
    adc (SHFAMT),y
    asl $45,x
    .byte $53
    ldx $0F29
    .byte $1B
    .byte $23
    sbc $CD9A,y
    sta (ZCHAIN),y
    inc QTEMP
    sbc ($F4,x)
    bne $77E9
    .byte $F2
    .byte $1C
    cmp (STATUS,x)
    .byte $2F
    .byte $04
    .byte $87
    .byte $93
    sbc OLDADR
    .byte $CF
    lsr $0C4F,x
    .byte $44
    .byte $F3
    cmp $238E,y
    .byte $7F
    sta $87B3,y
    ldy $C488,x
    sbc $CF,x
    cmp $3060,y
    .byte $AF
    ora $78
    .byte $07
    plp
    .byte $0F
    bvc $7894
    .byte $1F
    sta $46
    .byte $04
    .byte $82
    .byte $04
    ora CMCMD
    lda $CEB9,x
    .byte $1B
    sta COLRSH,x
    ora (TMPCHR,x)
    php
    clc
    ora #$92
    cmp (ICBAHZ),y
    .byte $D3
    rts
    jmp $5151
    .byte $9B
    ldx #$1E
    sbc $E030,x
    iny
    ldx $0C22
    .byte $62
    ora.w LMARGN
    sta $ECA5,y
    asl $10CA
    .byte $EB
    rts
    .byte $FF
    .byte $FB
    lsr $FA,x
    .byte $4B
    .byte $FF
    and $2D
    inc $0356,x
    .byte $B2
    .byte $02
    cmp #$64
    .byte $07
    .byte $B2
    asl $59
    .byte $0B
    bit $960A
    .byte $27
    .byte $4B
    rol ICBAHZ
    .byte $2B
    .byte $92
    rol
    cmp #$64
    .byte $33
    .byte $B2
    .byte $32
    eor $2C37,y
    rol $96,x
    .byte $3B
    .byte $4B
    .byte $3A
    and FEOF
    .byte $92
    rol $64C9,x
    .byte $43
    .byte $B2
    .byte $42
    eor $2C47,y
    lsr $96
    .byte $4B
    .byte $4B
    lsr
    and COLRSH
    .byte $92
    lsr $64C9
    .byte $53
    sta LMARGN,x
    and $F6
    lsr
    .byte $5A
    sbc $B0,x
    adc ZTEMP1
    .byte $64
    .byte $64
    .byte $B2
    adc #$59
    pla
    bit $966D
    jmp ($9E4E)
    .byte $AB
    .byte $1A
    lda ($9A),y
    .byte $8B
    eor $80A3,x
    and $7F
    .byte $92
    .byte $87
    cmp #$64
    stx $AD
    inx
    .byte $EB
    eor $808B
    .byte $3C
    .byte $5A
    ora (CRITIC,x)
    .byte $F2
    cmp OLDCHR
    lsr BUFRHI
    inc $3F64,x
    cli
    cmp $7E02,x
    rts
    lsr ICBLHZ,x
    sty $90
    .byte $9C
    .byte $33
    .byte $BB
    adc (APPMHI),y
    .byte $5A
    .byte $53
    lda $12D3,x
    cpx #$8D
    .byte $FC
    .byte $0C
    ldy $14E7,x
    .byte $4B
    clc
    sbc $2869,y
    sec
    sbc $FA60,x
    lda #$DE
    lda $B12E,x
    adc $A0,x
    clv
    .byte $C3
    .byte $D2
    eor ($B9),y
    cpy $8B
    .byte $7B
    .byte $9B
    .byte $C3
    adc $BE,x
    cmp $93
    dec $ADF4,x
    eor $F225,y
    sta $F46F,y
    ldy FKDEF
    eor $EBF3,x
    pha
    .byte $53
    tax
    .byte $C7
    .byte $34
    eor #$FF
    and OLDCHR
    .byte $CF
    sta OLDADR
    lda $BD73,y
    .byte $FC
    .byte $E3
    .byte $54
    .byte $CB
    bit $E938
    bvc $794C
    .byte $27
    stx $9B
    ldy $8B
    cmp (HOLD1),y
    lda $C91F
    .byte $5F
    bcs $79D9
    .byte $1B
    tay
    cmp $90D5,x
    sta $BAB9
    lda $76F6,y
    sta $56
    ldy.w ESIGN,x
    .byte $3F
    bmi $7978
    .byte $2F
    asl BPTR
    ora (FMSZPG),y
    ora OLDADR
    sta (COLCRS),y
    dec $FF
    lda $8A
    cmp $59
    ldx $60B2
    .byte $22
    .byte $33
    .byte $6F
    .byte $63
    .byte $57
    ldy DSKUTL,x
    sty $F096
    bmi $7A0A
    .byte $13
    .byte $4F
    sbc #$0F
    sta $1CE9
    .byte $64
    .byte $02
    .byte $03
    adc $8630,y
    .byte $0B
    sty $A9E1
    eor (TRAMSZ,x)
    sta ($16),y
    .byte $12
    .byte $04
    ror $37
    .byte $92
    brk
    lda $2536
    .byte $0C
    .byte $5A
    and $704F,y
    .byte $DC
    adc ($B9),y
    .byte $7C
    .byte $03
    adc ($D0,x)
    .byte $42
    .byte $DF
    .byte $FB
    .byte $73
    asl
    .byte $04
    eor #$09
    adc ($9A,x)
    .byte $80
    inc $B9
    stx $C5
    .byte $8F
    .byte $5C
    bcc $79DA
    .byte $CB
    .byte $F4
    lsr
    sbc $1424,y
    inc INBUFF
    txa
    ora $1B,x
    cmp OLDCHR
    .byte $FB
    .byte $13
    lsr
    ora $0630
    .byte $83
    .byte $A3
    sbc $DFE2,y
    pla
    lda #$1A
    .byte $F3
    sty $A50C
    .byte $5B
    dec $3DC3
    sbc (COLCRS),y
    adc $86
    .byte $62
    lsr DIGRT
    adc (VCOUNT_START,x)
    txs
    lsr
    cmp #$F2
    bne $7A24
    asl $110A,x
    inc $B1A3
    asl $58B8,x
    ldy $A23D,x
    .byte $02
    inc DSTAT,x
    cli
    .byte $43
    sta $1681,x
    .byte $17
    cmp MEMORY_REFRESH_CYCLES
    .byte $87
    bcs $79C6
    lda (RTCLOK),y
    .byte $AB
    sbc $609A,y
    cmp (RECVDN),y
    .byte $8B
    bmi $7A84
    .byte $1A
    .byte $73
    asl $0FAD
    cmp ($E4,x)
    stx $D1
    lda DOSINI
    .byte $12
    adc $2C20
    ror $99
    asl $840F
    bvs $7AB1
    .byte $80
    ldx FREQ,y
    asl $D0
    .byte $33
    sbc $93
    rti
    and $4B
    .byte $27
    rti
    bpl $7ABF
    ora $1A1C,y
    cmp ($B2,x)
    rts
    .byte $03
    cli
    .byte $1B
    rts
    .byte $6B
    jmp $9E0D
    asl $BA07
    ora ($A9),y
    .byte $BB
    .byte $FA
    sbc #$FC
    .byte $89
    adc $3731,y
    pla
    bvs $7AA6
    rts
    cmp #$08
    .byte $E7
    bne $7A9F
    .byte $DA
    beq $7A98
    .byte $A3
    .byte $E7
    adc (CMCMD),y
    sec
    .byte $67
    .byte $67
    .byte $0C
    .byte $92
    bmi $7A23
    ora $C5
    lda $0490,x
    beq $7A8F
    sta HOLDCH
    ror $201D,x
    .byte $57
    .byte $87
    .byte $AF
    .byte $3C
    bmi $7A3E
    .byte $E2
    .byte $DB
    .byte $83
    ror $0645
    inc $53C3
    .byte $73
    eor #$9F
    bit $BA32
    brk
    ror $D185
    sbc $A611
    .byte $7A
    .byte $07
    lsr
    .byte $74
    adc $0D,x
    rol
    cpx #$A9
    bmi $7AD6
    lda TMPCHR
    ora $6D,x
    rol
    sta (FKDEF,x)
    asl $62AD
    lda (PALNTS),y
    .byte $0F
    .byte $62
    sbc STATUS,x
    .byte $D4
    sbc (ICBAHZ,x)
    pha
    .byte $02
    ror CASINI,x
    .byte $DA
    sbc #$07
    lda ($D1),y
    and ICPTHZ,x
    .byte $AB
    cpy ENDPT
    bit $45C8
    .byte $1A
    tya
    .byte $74
    lda $3569
    lsr $6003,x
    .byte $A3
    .byte $B3
    .byte $1C
    adc ($8C),y
    lda $CBAE,y
    .byte $53
    lda $C9A4,y
    beq $7AC4
    ora ($E8,x)
    stx $FFB0
    lsr $C4,x
    ldy #$3A
    lsr $253D
    bmi $7AF9
    sbc #$63
    cpy $9AB0
    .byte $23
    sta ($47,x)
    .byte $B2
    cpx #$4C
    .byte $17
    .byte $1C
    .byte $52
    ora (LTEMP,x)
    tax
    bcs $7B7E
    .byte $F2
    ora BITMSK
    eor DSKFMS,x
    .byte $62
    asl $0934,x
    pla
    eor FKDEF,x
    .byte $53
    .byte $E2
    ora $F5BE
    sty $8C
    .byte $62
    eor ($AB,x)
    tax
    sty $67FD
    .byte $B2
    ldy $A92B,x
    ldx $8D
    .byte $6B
    sbc $86,x
    sbc (COLCRS),y
    bit FKDEF
    and #$32
    and (CIX,x)
    lsr BFENLO,x
    .byte $07
    adc ($8D,x)
    adc $391D
    rol $F4
    eor $56,x
    and ($D0,x)
    ldx $D0,y
    .byte $17
    asl ICHIDZ
    and $9D,x
    .byte $03
    dec $EB10
    .byte $A7
    ldx $ED23,y
    bit $B1
    lda $145B,x
    cmp #$28
    sta $B006,x
    clc
    rts
    ldy #$04
    sbc COLCRS,x
    ora #$55
    .byte $89
    .byte $22
    sta $6221,x
    and $5162,y
    .byte $62
    adc #$62
    sta (LMARGN,x)
    sta $0B08,y
    dey
    bpl $7B66
    .byte $E3
    .byte $F3
    bcs $7BB9
    .byte $1C
    lsr $0D,x
    .byte $13
    inc $AD54
    eor ($A4,x)
    bmi $7B9E
    dec $9DFC
    bpl $7B47
    lsr ICAX2Z
    .byte $43
    .byte $5F
    sec
    cpy $CF35
    bne $7BA8
    bvs $7C16
    .byte $B2
    .byte $A3
    .byte $3F
    .byte $9F
    .byte $72
    .byte $23
    asl $60F3
    .byte $8B
    .byte $9E
    adc ($56),y
    lda $F4E7
    beq $7BD9
    jsr $9E07
    lda #$FF
    .byte $FF
    .byte $2B
    .byte $1C
    ldy $B962
    sta $B39F,x
    .byte $FC
    lda (NSIGN),y
    bcs $7C32
    cpy #$11
    bne $7C46
    dec $AF8E,x
    ldy $C7AD,x
    cmp $B2,x
    .byte $F3
    eor $9F8A,x
    rts
    stx $01A3
    bvc $7BEA
    .byte $92
    rti
    ldx #$0E
    .byte $0C
    .byte $C3
    inc FTYPE
    dec $7178,x
    .byte $02
    .byte $5A
    lsr ZCHAIN,x
    asl $B4
    .byte $57
    sta DOSVEC,x
    adc #$58
    rol
    .byte $32
    .byte $D2
    .byte $73
    rol COLCRS,x
    cmp (ENDPT,x)
    sta $593A,x
    cpx ENTVEC
    rol $E596,x
    .byte $4B
    .byte $42
    and $E8
    .byte $92
    lsr
    cmp #$64
    sbc #$B2
    .byte $52
    .byte $74
    .byte $3A
    ora $C5
    sei
    .byte $7F
    .byte $4B
    .byte $CB
    .byte $89
    .byte $B2
    sta $7A32
    jsr $7D4B
    eor $F660,x
    .byte $2B
    jmp $8F25
    rol $5C
    .byte $83
    sbc $FB22,x
    .byte $CF
    asl $B0
    rol $703A
    ldy $97,x
    .byte $72
    sta DINDEX,x
    sty $5574
    lsr $BD
    .byte $32
    asl CIX
    lsr $03
    pha
    rti
    asl
    .byte $04
    asl FRMADR
    .byte $F4
    ora $B658,y
    asl $7E1B,x
    lda $ADD1,x
    asl $13,x
    .byte $E2
    ldy $1D89,x
    cli
    ora $CB
    plp
    beq $7C5D
    .byte $F3
    rts
    .byte $AB
    eor $CE,x
    ora TRAMSZ
    bne $7C8F
    adc ($C6,x)
    .byte $0C
    inx
    .byte $44
    cpy FEOF
    sec
    cmp $73B1
    bpl $7C7B
    and #$DF
    .byte $7F
    tay
    sty $45EB
    iny
    .byte $07
    cpx RAMTOP
    .byte $53
    clc
    adc $E693
    sta $32EB
    cpx #$48
    .byte $4F
    ldx $03,y
    and ($13),y
    bpl $7C31
    sbc #$06
    inx
    .byte $22
    inc ICHIDZ
    bcc $7CDC
    beq $7CDC
    cmp #$BF
    rts
    bcs $7CAF
    cpx $14A2
    rol $C085,x
    ldy #$B0
    .byte $17
    adc $96D1,x
    ora ($C2,x)
    .byte $02
    jmp $A126
    .byte $77
    cmp #$E0
    bcs $7CC3
    .byte $F2
    ora #$8D
    .byte $5A
    ldy $13,x
    eor #$FF
    clc
    adc #$01
    .byte $EF
    and $651C
    rol DSKUTL,x
    eor $1D37,y
    and #$37
    .byte $CB
    asl $3894,x
    pla
    .byte $5A
    clc
    .byte $DF
    .byte $6F
    cpy $B939
    ror $47FB,x
    .byte $EF
    ldy COUNTR,x
    .byte $FB
    .byte $47
    inx
    ldy $46F3
    lsr $69,x
    sta $1957,y
    cli
    sta ($CD),y
    .byte $89
    and DINDEX,x
    eor ($73),y
    jsr $5297
    rol $5C21
    .byte $53
    lda $5622,y
    .byte $54
    .byte $23
    lda $32AB
    adc $F385,x
    .byte $64
    bne $7D10
    .byte $B7
    .byte $AB
    cmp CHKSNT
    .byte $17
    bcs $7D11
    .byte $EB
    .byte $72
    jmp $739F
    ldy #$CF
    .byte $07
    .byte $0B
    ldy #$8B
    .byte $83
    ldy $E9
    eor ($97),y
    .byte $74
    .byte $52
    dex
    .byte $FA
    .byte $53
    adc NGFLAG
    .byte $93
    .byte $54
    jsr $3782
    lda $BCAC
    .byte $7F
    ora $1B,x
    .byte $54
    .byte $CB
    .byte $4B
    eor RAMTOP,x
    .byte $73
    asl $19
    stx $187C
    ldy $1ACD
    .byte $57
    adc $99,x
    adc $1B
    .byte $9B
    .byte $E7
    jsr $A239
    bcc $7D16
    .byte $0F
    .byte $D4
    eor ($D2,x)
    .byte $03
    and $E14C,x
    .byte $23
    lda ($B5,x)
    lda $3568,x
    cmp #$3C
    bne $7D5E
    lda #$FD
    .byte $BF
    .byte $04
    bit $BC5E
    .byte $1F
    .byte $9F
    brk
    .byte $73
    .byte $54
    clc
    lda ENDPT
    .byte $C7
    .byte $0C
    .byte $63
    cpx $56
    .byte $3A
    .byte $4B
    sbc ICBAHZ
    rol $E892,x
    cmp #$64
    .byte $42
    .byte $B2
    sbc #$59
    lsr
    .byte $3A
    lda $A2E6
    sbc #$46
    clc
    ora ROWAC,x
    lsr $19,x
    .byte $92
    .byte $72
    lda $251A
    .byte $74
    .byte $5A
    .byte $1B
    lsr
    ror $AA,x
    lda $AA
    sta $F978,x
    .byte $17
    and ($CD),y
    sta $6BB2
    .byte $5B
    sbc $A9
    ldy ZTEMP4
    .byte $5C
    ldx $5F
    adc $4A57,x
    .byte $EF
    .byte $92
    cli
    lda ICBALZ
    lsr $7555,x
    clc
    adc $A2EB,x
    inc $98,x
    .byte $87
    .byte $1C
    .byte $52
    .byte $07
    tax
    .byte $6B
    and #$0D
    .byte $3F
    .byte $D7
    lsr $80
    ora (FR1),y
    asl
    rol $03
    pla
    clc
    adc RMARGN
    eor $90A8,x
    .byte $EB
    inc $5C
    lda (OLDCOL),y
    and $A27E,x
    beq $7E4D
    lsr $C6
    .byte $5F
    bpl $7D90
    .byte $8F
    ldy $75
    .byte $C2
    ldx $2FD0,y
    bvs $7D74
    bit $425D
    pha
    .byte $F2
    sbc #$07
    sta ZTEMP4
    lsr $32AE,x
    .byte $F3
    .byte $7F
    ldx $F02B,y
    asl $F6,x
    .byte $33
    sty OLDCHR
    dey
    pha
    .byte $0C
    lda $A43B,x
    sbc ($E5),y
    lsr $0EC9,x
    bcs $7DC7
    sec
    tsx
    dex
    tay
    .byte $E2
    clc
    rts
    dec $E61B,x
    .byte $07
    .byte $CF
    .byte $CF
    txs
    .byte $1C
    .byte $F3
    cmp BUFRLO,x
    .byte $93
    lda $A2FB,y
    ldx $AA36
    .byte $8F
    sty $5A3A
    rol FTYPE
    clc
    .byte $77
    .byte $03
    sta $3E52,x
    .byte $7B
    sta (LNFLG,x)
    .byte $83
    ldx #$40
    jsr $E048
    .byte $03
    .byte $1B
    and $5F3F
    .byte $54
    jmp ($937E)
    beq $7E81
    .byte $A3
    sbc $E1A1,x
    ora ($14,x)
    cmp $100F,y
    .byte $33
    brk
    lda $A200,x
    .byte $5F
    ldx #$CC
    .byte $80
    iny
    stx $BE,y
    .byte $14
    cmp (QTEMP),y
    sbc $9B,x
    inc $0320,x
    sbc (BUFADR),y
    bpl $7E65
    .byte $2F
    bmi $7DF7
    .byte $57
    .byte $44
    bpl $7EE2
    ora ($EA),y
    .byte $22
    eor ($91),y
    rol $DC,x
    ora $828C,y
    .byte $34
    dey
    ora $443F,x
    php
    ror $CC,x
    .byte $8F
    asl $BFA6,x
    .byte $44
    .byte $04
    .byte $1B
    .byte $FF
    .byte $03
    lda $2A01
    .byte $44
    ora (ATRACT),y
    inc $02B1,x
    .byte $02
    .byte $82
    asl $83,x
    stx $75
    sta $863D
    tya
    sbc COLCRS,x
    ldy OLDCHR
    .byte $FA
    .byte $AB
    .byte $43
    lsr $7C2A,x
    ldx $1AE9,y
    lsr $B1
    eor $85,x
    .byte $63
    .byte $57
    ldx $A3F9
    ldx $75
    sbc $F118,y
    .byte $E2
    ldy $3BA9
    .byte $27
    iny
    sbc $55A2,y
    tax
    inc $ADB0,x
    lsr COLCRS,x
    .byte $02
    adc #$57
    rol
    asl $A5
    cli
    .byte $AB
    asl
    .byte $57
    bit APPMHI
    and #$29
    sbc NOCKSM,x
    and FRMADR
    .byte $5A
    brk
    lsr
    jmp ($A9AA)
    sta $EB64,x
    lda (FKDEF),y
    inc FKDEF
    ora $40CC,x
    sty $80C8
    cpy $8C56
    .byte $82
    bvs $7EEF
    adc $1F
    sbc #$02
    inc $AE4A,x
    .byte $F3
    cmp #$2E
    bcs $7EE7
    sta SAVMSC
    .byte $7F
    rts
    .byte $B2
    rol BFENLO
    stx ADRESS
    ldx DSKFMS,y
    .byte $7F
    lda $83,x
    .byte $F4
    lsr
    adc $46,x
    bvc $7F68
    adc #$EF
    adc $47,x
    .byte $5F
    sei
    beq $7F3A
    .byte $23
    jsr $82BE
    asl $3107
    ora $16,x
    .byte $17
    clc
    .byte $32
    .byte $FF
    .byte $3F
    .byte $1B
    .byte $DC
    jsr $2157
    jmp DCOMND
    .byte $04
    ora TRAMSZ
    .byte $07
    .byte $34
    php
    .byte $FF
    .byte $3C
    tya
    rol $5614,x
    .byte $1C
    .byte $FC
    .byte $14
    ora ($C1,x)
    sbc ($5C),y
    cpx #$FF
    cld
    .byte $3F
    .byte $52
    .byte $14
    sty $99D6
    iny
    tya
    adc #$97
    pha
    ora ($85,x)
    sta (ICHIDZ),y
    sta ($A0,x)
    .byte $C3
    nop
    .byte $0B
    .byte $E3
    lda $8050
    bpl $7FBC
    .byte $E2
    rts
    ora ($82),y
    .byte $3C
    cpy $AF
    ora $E2D0,y
    .byte $1C
    .byte $5C
    tya
    lsr
    .byte $E2
    .byte $17
    sbc ($2D,x)
    dex
    lda DIGRT,x
    dec $5D38
    .byte $F4
    sec
    rti
    dec $E187
    ora $E325,y
    .byte $97
    sta (ICBALZ),y
    ora CRITIC,x
    .byte $63
    eor $54ED,x
    .byte $80
    bcc $7FDA
    .byte $22
    .byte $34
    and ($AF,x)
    .byte $02
    pla
    lda ($E4),y
    bmi $7FBF
    .byte $22
    .byte $64
    .byte $04
    bpl $7FA8
    ror ICCOMT,x
    ora #$30
    sta $131C
    .byte $FF
    cpx #$F1
    jsr $812E
    and ($E4,x)
    bit $9030
    cpx #$81
    sty $8E
    lda (CRITIC,x)
    eor (CMCMD,x)
    lda $7168
    .byte $04
    .byte $0B
    .byte $C7
    .byte $03
    lda ($98),y
    php
    bit $18C8
    .byte $47
    beq $7FC9
    .byte $F7
    .byte $74
    rol $FD
    ldx $EE41,y
    .byte $0C
    bmi $7FBC
    cmp ($78),y
    .byte $CB
    .byte $02
    rts
    .byte $9B
    sta ($99,x)
    cpy #$E9
    .byte $63
    clc
    .byte $13
    .byte $27
    .byte $07
    .byte $B7
    .byte $34
    .byte $0C
    sta BUFRHI
    .byte $33
    .byte $3C
    cpy TRAMSZ
    ldy ICSTAZ,x
    dey
    .byte $33
    ora (VCOUNT_START),y
    .byte $80
    sta $F83F,y
    .byte $27
    dex
    and #$20
    .byte $03
    sbc (BPTR,x)
    .byte $D4
    ora $E2,x
    and (ROWAC,x)
    .byte $1F
    eor $5D76
    .byte $07
    cpy #$A6
    stx LMARGN,y
    pla
    .byte $87
    and $F8BC,x
    .byte $13
    .byte $0F
    lda #$83
    dey
    cmp (CASINI,x)
    .byte $42
    bcc $7FF8
    ora ($BB,x)
    rti
    tax
    sec
    lda $924C,y
    .byte $C3
    .byte $3A
    php
    .byte $63
    cpy #$EA
    bpl $7FF8
    .byte $03
    stx BUFCNT
    txa
    cpx #$80
    sei
    .byte $9B
    dey
    adc.w $00C9,x
    ora (FR1,x)
    clv
    .byte $53
    bmi $8098
    .byte $0C
    .byte $07
    .byte $DC
    bvs $8075
    .byte $0F
    .byte $47
    .byte $04
    .byte $47
    .byte $83
    cpx FCHFLG
    ror $1F3C,x
    .byte $FF
    .byte $E7
    .byte $87
    .byte $8B
    .byte $04
    .byte $FF
    asl $A388
    .byte $B7
    cpy $382E
    bcs $8039
    .byte $AB
    adc ($16,x)
    .byte $B2
    asl $3B9C
    .byte $07
    .byte $63
    lsr
    jmp (CRSINH)
    ldx #$09
    .byte $7A
    ldy BITMSK
    jsr $98B2
    .byte $5F
    clv
    tya
    .byte $0F
    sta $AE72,y
    sbc $C4
    cpy #$2D
    eor (LOGCOL,x)
    inc $F1E1
    sta ($D1,x)
    cmp $14
    beq $8077
    rts
    .byte $7F
    .byte $A7
    lsr $4B,x
    sta $ABA8,y
    .byte $27
    cpy $424B
    ldy TMPCHR
    dec $9138
    cmp $2113,y
    lda $647A,y
    sbc #$36
    .byte $1C
    ror
    bcc $80AA
    adc ($E8,x)
    sta $2AD4,x
    .byte $0B
    .byte $9E
    .byte $52
    bvs $803A
    jmp $F33C
    .byte $A3
    pha
    .byte $53
    eor $1108,x
    cmp ($9C),y
    cmp $61EA
    .byte $CB
    .byte $63
    .byte $0B
    php
    bne $80CC
    rts
    stx $60C1
    .byte $FF
    ora $7F,x
    eor RECVDN,x
    lda (TMPCHR,x)
    lda (BUFRHI),y
    .byte $1A
    .byte $12
    txa
    dec $AD62
    .byte $7B
    .byte $5C
    ora $D4A0
    .byte $DF
    rts
    sty $9967
    .byte $9E
    .byte $9C
    sta ABUFPT,x
    ora #$48
    inc $86E7
    .byte $3A
    cmp #$80
    bne $80EC
    .byte $9E
    cmp ($95,x)
    .byte $53
    dex
    sbc ZTEMP1
    .byte $54
    ldy #$AC
    sta $D568,y
    eor $50A5,y
    .byte $E3
    ldx $61
    and $9A8E,y
    sty $A999
    .byte $87
    stx FREQ
    .byte $C2
    sei
    jmp ($2700)
    stx $E8
    ora ($A5,x)
    .byte $AB
    .byte $A3
    .byte $27
    .byte $EB
    ldx BPTR
    ldy DRKMSK
    ldx $B49A
    ldy $BD9B
    rol $1C97,x
    .byte $9C
    ldy $08C6,x
    sta $5814
    eor ($0F,x)
    lda $D2
    ldx SSKCTL,y
    ldy #$2B
    lda $0AFE,y
    sta BUFADR,y
    dey
    .byte $5F
    bpl $8128
    rts
    nop
    .byte $80
    .byte $A3
    clv
    eor $480F
    lda #$FE
    dec $AE,x
    eor OLDCOL
    clc
    inc ADRESS
    .byte $B3
    eor $42EF
    adc #$92
    .byte $C3
    asl
    dex
    bpl $8139
    .byte $9C
    .byte $DC
    ldy #$0A
    bmi $80E1
    clc
    ldy OLDCHR
    bne $810B
    .byte $B2
    eor $80,x
    bne $81B3
    txs
    .byte $64
    brk
    ror $BF
    ora $4317
    .byte $E7
    brk
    and $BD
    .byte $53
    ldy CMCMD
    eor ($A0),y
    asl
    rol $481A
    .byte $89
    sta (STATUS,x)
    lda (FR1,x)
    jmp $D816
    ora ($44,x)
    .byte $DA
    ora (FREQ),y
    .byte $DA
    .byte $0B
    ora $1898
    .byte $0C
    .byte $D4
    .byte $93
    jsr $C866
    lda $A4A5
    adc #$AE
    cmp #$9D
    ldy $32EC
    ldy $90
    ldx #$F0
    ldy #$FE
    jmp (PADDL0)
    eor $808F
    .byte $07
    eor $B38C
    rol $C6
    dec $1D
    .byte $22
    beq $81C0
    bvc $81F5
    .byte $CB
    sbc #$A9
    ldy $CD,x
    .byte $33
    sty $B0,x
    and ZCHAIN,x
    .byte $07
    .byte $BF
    .byte $43
    ldy DRKMSK
    .byte $92
    php
    .byte $0F
    inx
    rol $8A
    ldx #$4D
    .byte $7F
    .byte $C3
    bne $8213
    .byte $80
    ora TMPCHR,x
    stx $99,y
    sta ($8C,x)
    lda $93C9,x
    .byte $7F
    jmp ($7636)
    cpx #$5C
    bne $81C2
    ldx FMSZPG,y
    rts
    ora $11EE,y
    cpy DOSVEC
    cmp $A8
    .byte $07
    .byte $EB
    tax
    .byte $F4
    lda $B308,y
    .byte $72
    .byte $F2
    .byte $04
    dec STATUS,x
    jsr $A69D
    .byte $CF
    .byte $FA
    .byte $92
    .byte $DA
    lda (OLDROW),y
    sta (PALNTS),y
    .byte $CB
    bne $8200
    inc $5C
    adc $D0
    sbc $A107
    .byte $DA
    .byte $34
    txs
    adc $A3AD
    txs
    and $18B0
    lsr TMPCHR
    dec $DE
    eor ($A5),y
    eor $C76D
    tay
    sta LMARGN
    .byte $5F
    rts
    .byte $82
    brk
    asl
    sbc #$50
    bpl $824F
    adc.w ICDNOZ
    asl
    .byte $1B
    adc #$CC
    stx FRE
    .byte $22
    adc (KEYDEF,x)
    .byte $92
    .byte $03
    and $10DA
    bcc $8212
    sta $42DD
    .byte $12
    dec $3B15,x
    clc
    cpx #$D2
    php
    .byte $67
    sbc (FREQ,x)
    .byte $7F
    jmp ($7402)
    .byte $52
    and ($47,x)
    bne $8219
    .byte $EF
    .byte $1A
    rti
    and #$6D
    eor $AC9F,y
    ldy $6D
    .byte $53
    adc ($D8,x)
    .byte $5F
    cpy $F4
    and $3132,x
    ror $FF
    ora ICAX2Z,x
    lda $F045,y
    .byte $0F
    sta $C8C5
    bit $2AAB
    .byte $62
    .byte $97
    .byte $B2
    .byte $32
    adc #$CC
    adc $9CBD,y
    .byte $C7
    rti
    .byte $D3
    cmp VCOUNT
    bcc $827B
    lda #$02
    .byte $7F
    ora ICPTLZ
    .byte $D4
    .byte $AB
    ora $A6
    .byte $9B
    pha
    inc $98
    .byte $E7
    tya
    inx
    .byte $B2
    txs
    sbc $B5B0,y
    rol
    cpx COLCRS
    ldy ATRACT
    sbc BPTR
    .byte $AB
    dey
    rol $80,x
    rti
    adc LNFLG,x
    iny
    asl $2FE0
    ldy #$3B
    .byte $1F
    .byte $14
    .byte $07
    .byte $62
    nop
    sty $8363
    asl $BE
    .byte $F3
    lsr INSDAT,x
    .byte $3B
    sta $6B84
    lsr ICHIDZ
    .byte $4F
    jmp $F31F
    sbc HOLD1
    rti
    rol
    sta $15E5,y
    dex
    .byte $A3
    ora (DINDEX,x)
    ldy $A415
    .byte $1F
    lda $1DAB
    eor #$AE
    eor $A5,x
    ora $5C,x
    .byte $AF
    bne $82F2
    ora #$3F
    pla
    .byte $5C
    lda ($8D),y
    iny
    cli
    stx $CE,y
    .byte $D4
    .byte $97
    .byte $23
    and (FTYPE,x)
    sty $8A,x
    .byte $A7
    sta $D1,x
    cmp $3703,x
    sta $FE26,y
    tax
    dec $FB93
    bpl $82F3
    .byte $DC
    .byte $FA
    .byte $02
    txa
    rts
    .byte $0F
    .byte $D4
    .byte $80
    .byte $2B
    .byte $EB
    .byte $80
    .byte $1B
    nop
    lda ($1F),y
    tay
    bmi $838B
    brk
    lda ($FA,x)
    cpy #$70
    tax
    .byte $A7
    lda #$50
    tay
    cmp $E3,x
    lda NOCKSM
    .byte $43
    lda (BUFADR,x)
    bvs $8338
    ldx #$FE
    ora ($94,x)
    .byte $04
    sta $7F,x
    adc #$AF
    rol
    .byte $80
    ldx $55ED
    .byte $9F
    eor CHKSNT,x
    asl $37,x
    ldy $7B52,x
    ldx TRAMSZ
    sbc $D3,x
    sty ICHIDZ
    bit $A7
    .byte $C7
    ldy #$08
    beq $834C
    lda $FDA3,y
    sta $6D7E,y
    dey
    bne $8343
    .byte $6F
    ldy $33A8
    adc #$BE
    .byte $AF
    .byte $AB
    pha
    and $CBAE,y
    ora $CD53,x
    ldy $4EC9
    cmp LMARGN
    sty $EB,x
    cmp $85
    .byte $1F
    .byte $7A
    .byte $CF
    cpy $CB
    bcc $8332
    lda $D1B1,y
    .byte $FC
    .byte $3A
    ror VCOUNT_START,x
    bcs $833D
    .byte $FF
    rol $3A68,x
    .byte $C7
    jmp $5AE1
    jsr $802F
    .byte $BF
    eor $E9
    adc (ICBLHZ,x)
    bmi $8398
    cmp ($91),y
    eor ($73,x)
    beq $83B5
    .byte $04
    sbc $9E
    .byte $E3
    .byte $E2
    .byte $83
    adc $B2
    rts
    asl $6A46,x
    ldx $71
    bit $95
    .byte $E7
    .byte $9B
    sei
    .byte $9E
    .byte $FA
    ora ($D3,x)
    lda #$C0
    sta $3F0E
    .byte $D4
    cli
    .byte $C2
    rti
    nop
    rol
    stx $D0,y
    tax
    .byte $D4
    .byte $97
    ldy KEYDEF
    clc
    adc #$93
    .byte $80
    sta $EA94
    bcc $835C
    sta $9C,x
    .byte $A3
    inc $0149
    lda (SHFAMT),y
    .byte $1F
    eor $A102,y
    cmp (FR2),y
    ora $73,x
    lda $0EF2
    php
    bcs $83D7
    inc $FE06
    .byte $44
    .byte $CF
    and #$C9
    .byte $2B
    bit $CE
    .byte $23
    lda $F0CE
    lda #$0E
    dec $AEAC
    ldy $7F99,x
    .byte $9F
    ldx $A562,y
    lda (INBUFF),y
    cmp $E1B0
    .byte $7C
    .byte $82
    .byte $FF
    cmp $CA93
    tay
    .byte $43
    .byte $8B
    jsr $0391
    bne $8426
    inc $B39F
    asl $E8
    sbc $72A0
    .byte $77
    .byte $83
    and #$61
    sty $97
    .byte $F7
    .byte $03
    sta $A9C7,y
    lda $B3B9
    inc $EE06
    stx DSTAT,y
    ldy $D7AB
    .byte $7F
    tya
    sty VCOUNT_START
    sbc $2CD0,y
    cmp $20E5,x
    .byte $E7
    .byte $63
    .byte $07
    .byte $BB
    lda $CDB1,x
    nop
    bcc $843B
    .byte $9B
    .byte $DC
    tya
    ldy $C74C,x
    cmp $E2AB,x
    rti
    lsr
    sta $47
    inc ICCOMT
    lda $3856
    sbc OLDCHR
    lsr $B35E
    rts
    .byte $E7
    .byte $23
    .byte $9B
    and MEMORY_REFRESH_CYCLES,x
    dec $46,x
    cmp #$90
    eor $03,x
    bcs $84A4
    bmi $841B
    .byte $82
    .byte $A3
    sec
    .byte $89
    lsr $5C81
    jsr $AD33
    cmp #$5C
    iny
    cmp ($D0),y
    cmp ICAX2Z,x
    .byte $C2
    .byte $03
    stx $16
    ldy #$06
    ror $88,x
    iny
    lda ($1B),y
    .byte $9B
    .byte $92
    jmp ($6E91)
    clc
    .byte $3B
    ldy $B502
    bit $6930
    stx DSKFMS,y
    jmp $ABDE
    rts
    .byte $FA
    .byte $0F
    .byte $9B
    .byte $4F
    .byte $44
    bpl $844F
    .byte $7A
    jsr $ACE8
    .byte $F7
    .byte $DC
    eor #$D1
    ora $74F8,y
    .byte $14
    .byte $43
    ldx #$30
    ora $4C10,x
    inx
    pla
    .byte $07
    .byte $87
    ldx #$A0
    .byte $3A
    ldx $D121
    ldy $880E,x
    dex
    .byte $74
    cld
    .byte $43
    ldx #$01
    inc ICBAHZ
    rol $1888,x
    .byte $3B
    .byte $F4
    rol $4751
    ldx #$3A
    .byte $67
    and $9110,x
    inx
    sta $0085,y
    .byte $02
    .byte $62
    .byte $9F
    lsr $B9AD,x
    .byte $B2
    .byte $17
    bne $84EA
    sbc $CAC8
    and $90,x
    jsr $EC20
    .byte $1A
    .byte $3F
    .byte $5A
    cmp #$54
    bit $AD
    bpl $84B4
    .byte $67
    beq $8508
    dec $B180
    sec
    .byte $FF
    eor $6B03,y
    .byte $89
    lda $AF
    and $E9
    txa
    sty COLPM2
    ora #$C6
    bpl $8577
    .byte $63
    .byte $F2
    jsr $E8E3
    cpx CHKSUM
    lda #$52
    stx $03D3
    cli
    .byte $04
    tya
    ora $98
    asl $98
    .byte $07
    sty $8D,x
    php
    .byte $BF
    dex
    .byte $77
    stx $C6
    ora $886D
    sta $81DD
    txs
    cpx #$08
    .byte $DF
    sty CMCMD,x
    .byte $07
    cpx LTEMP
    sta $6786,y
    bmi $853A
    and $0440,y
    lsr $0150
    .byte $13
    bcc $8531
    .byte $04
    sbc $80
    ldx KEYDEF
    .byte $17
    sta $B174,x
    .byte $62
    sbc (PM_DMA_CYCLES),y
    .byte $62
    lda PM_DMA_CYCLES
    jsr $660D
    inc $5F
    stx TRAMSZ,y
    cmp $BD
    bne $8542
    .byte $6F
    ora ICBALZ
    .byte $89
    pha
    ror $B2D7
    sta $8C88,y
    lda $EC4B,x
    txa
    .byte $03
    .byte $2F
    .byte $62
    .byte $AF
    .byte $03
    bmi $85A5
    .byte $63
    jsr $845D
    inc TRAMSZ
    eor (FMSZPG),y
    jmp $21C7
    ldy $05A2
    lda $FFF7,x
    beq $859D
    clc
    adc $9A5F,x
    .byte $64
    bcc $858B
    cmp #$B7
    clv
    bcs $858A
    sec
    sbc #$FA
    jmp $ADCF
    adc #$BC
    sta $B1FD,x
    ror $094A
    tay
    .byte $D4
    .byte $22
    ldx $4C8F
    lda #$EC
    sbc $58BD
    .byte $07
    txa
    pha
    .byte $57
    jsr $ADF0
    pla
    sbc ($CA),y
    bpl $8570
    .byte $DC
    ora $1F60
    beq $85CB
    .byte $33
    ldy #$03
    lda RANDOM
    and #$AA
    inc $8885,x
    bpl $85B2
    .byte $9E
    bit $CA20
    bne $85A6
    ora $06C9,x
    bcc $85C8
    rts
    .byte $5F
    ora ($8A,x)
    asl $BDD2
    eor $660F
    nop
    sec
    tya
    ora $AFFC,y
    plp
    sbc #$79
    ldx #$15
    stx $A147
    tya
    .byte $97
    .byte $87
    tax
    ldy #$FC
    lda $9F6D,x
    ldx $C691
    adc $F7F8
    clc
    .byte $FB
    dec OLDROW,x
    plp
    sta $EBA5
    adc #$CB
    bcs $8588
    cpx #$F3
    .byte $54
    sty $7D79
    dec $B1F6
    bne $85D4
    cmp $DE60,x
    bne $8675
    eor ($CD,x)
    ora $DC
    .byte $23
    sty $AC,x
    nop
    plp
    ldx $3042,y
    bcc $85C1
    .byte $A3
    .byte $F3
    dex
    .byte $97
    eor $C293,x
    inc $C88F
    .byte $22
    eor $7480,x
    .byte $4B
    .byte $02
    and $3215,y
    txa
    .byte $83
    .byte $C3
    .byte $F2
    .byte $FC
    .byte $A3
    .byte $CF
    .byte $B2
    dey
    lsr
    cpx $A5A2
    dex
    lsr
    ldy #$0A
    plp
    iny
    .byte $AF
    .byte $5F
    .byte $FA
    .byte $23
    .byte $F2
    tay
    adc $80F2,x
    .byte $CB
    .byte $02
    .byte $8F
    cpy #$80
    sta $E0B0,y
    eor #$80
    rts
    dey
    .byte $02
    .byte $57
    lsr NGFLAG,x
    bmi $86B3
    .byte $82
    adc ($80,x)
    eor $2498,y
    brk
    .byte $14
    adc ($80,x)
    adc $98
    lda SOUNDR
    bmi $85FE
    .byte $82
    rts
    rti
    .byte $27
    .byte $04
    eor $67,x
    .byte $22
    jmp $4288
    .byte $63
    jsr $2406
    php
    rol
    bmi $8626
    tay
    sty $0818
    lda $AD9E
    .byte $42
    sta (DOSVEC,x)
    .byte $1C
    adc $98AC
    .byte $2F
    and $8D
    .byte $D3
    .byte $B2
    sta $363F,x
    inx
    bne $867E
    .byte $AF
    inc $AD98
    asl QTEMP
    asl
    sta $A534
    ldy #$FF
    ldx #$04
    jsr $6515
    .byte $FF
    .byte $43
    ora (RECVDN,x)
    eor $0700
    .byte $1C
    ldx $80
    ora $540E
    brk
    cpx #$60
    brk
    .byte $3A
    bvs $871A
    .byte $1C
    .byte $AB
    brk
    ora $E5,x
    .byte $34
    brk
    .byte $1B
    .byte $72
    sty LNFLG
    asl $CB
    and ($95),y
    plp
    ror
    .byte $34
    jsr $64E0
    pla
    .byte $DF
    pha
    and ($E5,x)
    .byte $87
    .byte $CB
    bmi $866D
    dey
    .byte $5C
    cmp ($A1,x)
    and $AE
    .byte $97
    stx ICIDNO
    sta ($5C,x)
    .byte $32
    .byte $A3
    .byte $AF
    lsr BUFRHI
    stx SIOINV
    .byte $F4
    sta $BA
    cmp ($69),y
    cmp #$0B
    tya
    inx
    .byte $6F
    ldx #$71
    adc (LNFLG,x)
    ldx #$F0
    ror CHKSNT
    cmp #$42
    .byte $C7
    lsr $8E,x
    ora $B2
    .byte $33
    cmp $CDEC,y
    bne $8710
    inx
    .byte $DF
    .byte $CB
    .byte $0B
    bcc $8701
    pla
    .byte $3C
    bcs $8729
    txa
    clc
    .byte $6F
    adc #$10
    tax
    cpx #$80
    inc $EED7,x
    .byte $DC
    lda $B108
    cmp #$4F
    .byte $6B
    bne $86EA
    beq $8762
    ldy #$00
    .byte $FF
    .byte $04
    .byte $97
    and ($BD,x)
    cmp $B2,x
    tax
    .byte $63
    and #$03
    lda CYCLES_PER_SCANLINE,x
    pha
    eor $8A,x
    sta $0175,y
    pla
    sty $20FC
    and #$90
    bmi $8761
    .byte $AB
    ldy $B1C2
    beq $8755
    .byte $FC
    pha
    and #$FC
    sta $0178
    ldx $FEFE
    lda $B2FD
    .byte $97
    iny
    cpy #$03
    bcc $8735
    inc DOSINI
    ldx #$FF
    .byte $FF
    txs
    inx
    sbc (ICIDNO),y
    stx NSIGN
    .byte $2F
    .byte $02
    .byte $4B
    dec FR0,x
    jmp $429B
    .byte $EF
    stx SKRES
    .byte $0B
    lsr CHKSUM,x
    .byte $92
    .byte $AF
    .byte $D4
    .byte $52
    sta CASINI,x
    adc #$0E
    rol
    asl $D2
    rti
    eor $A0,x
    bcc $8792
    .byte $D2
    ora (MEMORY_REFRESH_CYCLES,x)
    eor $A4,x
    brk
    .byte $04
    tax
    lda #$B2
    sta $AF05
    .byte $03
    stx SIOV
    rts
    .byte $4B
    sed
    .byte $52
    .byte $4F
    lsr $BE49
    jsr $4153
    lsr CIOCHR,x
    cpy $84
    ora (FR2),y
    txa
    .byte $22
    cpy $8844
    php
    .byte $73
    and $59,x
    asl $2CAA,x
    cpy $E0C0
    tay
    ldx #$C0
    cpy $88
    brk
    brk
    ora ($CD,x)
    lda ($80),y
    brk
    eor ($D0,x)
    .byte $43
    bvs $87D2