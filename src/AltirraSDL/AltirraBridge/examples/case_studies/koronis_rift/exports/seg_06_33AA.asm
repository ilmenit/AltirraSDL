; =============================================================
; Segment 6: $33AA-$37FF — Title screen init: splash, music, keypress wait
; =============================================================
; XEX load range: $33AA-$37FF (1110 bytes)
; INI at $33AA (title_screen_init)

    org $33AA

title_screen_init:
    ldx #$50
    lda LNFLG,x
    sta $3200,x
    inx
    bne $33AC
    jsr $33C2
    ldx #$50
    lda $3200,x
    sta LNFLG,x
    inx
    bne $33B9
    rts
    ldx #$FF
    stx TMPCHR
    inx
    stx AUDCTL
    stx NMIEN
    stx $78
    stx $61
    lda #$03
    sta SKCTL
    lda #$B6
    sta $5F
    lda #$36
    sta FKDEF
    lda #$40
    sta PALNTS
    jsr title_decompress
    lda #$00
    sta DLISTL
    lda #$40
    sta DLISTH
    lda #$20
    sta DMACTL
    lda #$BD
    sta VVBLKI
    lda #$35
    sta $0223
    lda #$53
    ldy #$40
    ldx #$00
    sta $7F,x
    sty $BF,x
    clc
    adc #$28
    bcc $340E
    iny
    inx
    cpx #$40
    bcc $3404
    ldx #$02
    stx INSDAT
    ldx #$04
    stx SWPFLG
    inx
    jsr $34DF
    dec SWPFLG
    ldx #$04
    jsr $34DF
    inc $78
    ldx #$04
    jsr $34DF
    lda #$08
    sta PMBASE
    lda #$41
    sta PRIOR
    jsr wait_vblank
    lda #$10
    sta COLBK
    sta COLPF2
    lda #$3A
    sta DMACTL
    lda #$40
    sta NMIEN
    lda #$02
    sta GRACTL
    ldx #$09
    lda title_color_data,x
    sta TMPCHR,x
    dex
    bpl $3453
    ldy #$0E
    lda #$10
    sta $0C40,y
    dey
    bpl $345F
    ldy #$04
    lda #$38
    sta $0C45,y
    dey
    bpl $3469
    lda TMPCHR
    bne $346F
    lda #$02
    sta INSDAT
    lda #$36
    sta SWPFLG
    ldx #$14
    jsr $34E5
    dec SWPFLG
    ldx #$13
    jsr $34E5
    lda HOLD1
    bpl $3487
    lda #$FF
    sta OLDROW
    tax
    asl
    sta OLDCOL
    sta $0C47
    lda #$C1
    sta HPOSP0
    ldy #$08
    sty COLPM0
    jsr $371A
    ldy #$0F
    stx $5C
    sty COLPM0
    jsr $371A
    lda #$00
    sta $0C40,y
    ldx $5C
    sta $0C40,x
    inc $5C
    ldx #$03
    cpx $5C
    inx
    bcc $34D1
    lda $0C45,x
    and OLDROW
    and OLDCOL
    sta $0C45,x
    dex
    bpl $34C0
    lsr OLDROW
    asl OLDCOL
    dey
    bpl $34A7
    lda #$22
    sta DMACTL
    lda #$00
    sta HPOSP0
    rts
    lda #$09
    ldy #$00
    beq $34E9
    lda #$04
    ldy #$66
    stx CYCLES_PER_SCANLINE
    sta HOLDCH
    lda SWPFLG
    sta $73
    jsr $3502
    clc
    lda CYCLES_PER_SCANLINE
    adc #$08
    sta CYCLES_PER_SCANLINE
    dec HOLDCH
    bne $34ED
    dec INSDAT
    rts
    lda #$01
    sta COUNTR
    bne $351B
    lda title_music_data,y
    beq $3535
    pha
    and #$1F
    sta COUNTR
    pla
    lsr
    lsr
    lsr
    lsr
    lsr
    bne $3521
    iny
    lda title_music_data,y
    jmp $3525
    tax
    lda $36AE,x
    sta KEYDEF
    iny
    sty $7A
    jsr $3537
    dec COUNTR
    bne $352A
    ldy $7A
    bne $3508
    iny
    rts
    ldy $73
    lda INSDAT
    bne $3548
    tya
    sec
    sbc #$02
    lsr
    lsr
    eor #$0F
    jmp $3555
    cmp #$02
    beq $3555
    tya
    lsr
    lsr
    cmp #$08
    bne $3555
    lda #$07
    sta DELTAR
    lda KEYDEF
    sta $75
    lda #$08
    sta ENDPT
    clc
    adc CYCLES_PER_SCANLINE
    tax
    dex
    lsr $75
    bcc $356B
    jsr $3576
    dec ENDPT
    bne $3563
    stx CYCLES_PER_SCANLINE
    iny
    iny
    sty $73
    rts
    lda $78
    bne $3589
    dey
    dey
    jsr $3589
    inx
    jsr $3589
    iny
    iny
    jsr $3589
    dex
    dey
    jsr $358E
    iny
    stx CYCLES_PER_SCANLINE
    sty $73
    lda $007F,y
    sta ROWAC
    lda $00BF,y
    sta $71
    txa
    lsr
    tay
    lda DELTAR
    bcc $35A9
    tax
    ora #$F0
    jmp $35B0
    asl
    asl
    asl
    asl
    tax
    ora #$0F
    stx DELTAC
    and (ROWAC),y
    ora DELTAC
    sta (ROWAC),y
    ldx CYCLES_PER_SCANLINE
    ldy $73
    rts
title_vbi:
    inc.w $0014
    ldy TMPCHR
    bne $35EE
    lda HOLD1
    bmi $35E2
    dec HOLD1
    bne $35DF
    lda #$60
    sta AUDCTL
    lda #$FA
    sta AUDF1
    lda #$FF
    sta AUDF3
    lda #$24
    sta LMARGN
    jmp $3608
    jsr $360B
    dec LMARGN
    bne $35EB
    dec TMPCHR
    jmp $3608
    bmi $3608
    jsr $360B
    inc LMARGN
    ldx #$06
    lda RMARGN,x
    sta AUDF1,x
    clc
    adc $3627,x
    sta RMARGN,x
    dex
    dex
    bpl $35F7
    dec TMPCHR
    jmp XITVBV
    lda LMARGN
    lsr
    lsr
    lsr
    ora #$A0
    sta AUDC1
    sta AUDC2
    adc #$00
    sta AUDC3
    eor #$60
    sta AUDC4
    rts
title_color_data:
    .byte $56,$7F,$00,$2A,$01,$22,$02,$00,$03,$87,$00      ; $3623
title_music_data:
    .byte $F9,$16,$70,$04,$73,$01,$77,$02,$7F,$00,$EE,$D5,$02,$44,$03,$7C ; $362E
    .byte $61,$22,$00,$74,$E4,$C4,$A6,$C5,$E3,$61,$26,$00,$78,$E4,$85,$E3 ; $363E
    .byte $88,$29,$00,$74,$E4,$C2,$A2,$01,$F8,$E1,$01,$7C,$42,$83,$E2,$01 ; $364E
    .byte $B8,$2A,$00,$FD,$E4,$C2,$A3,$03,$F8,$A8,$22,$01,$01,$26,$00,$EF ; $365E
    .byte $16,$C6,$01,$E6,$02,$07,$24,$00,$1F,$05,$0E,$04,$0F,$04,$0D,$47 ; $366E
    .byte $02,$4C,$03,$CC,$41,$01,$1E,$22,$00,$7C,$05,$38,$64,$04,$D8,$04 ; $367E
    .byte $98,$0B,$18,$01,$3C,$00,$01,$04,$01,$00,$EF,$01,$09,$01,$6F,$01 ; $368E
    .byte $29,$01,$E9,$00,$7D,$04,$55,$00,$EF,$01,$08,$01,$EF,$01,$01,$01 ; $369E
    .byte $EF,$00,$0C,$78,$8C,$C0,$C4,$FC,$C8,$70,$4F,$A3,$40,$FD,$0F,$41 ; $36AE
    .byte $00,$40,$C0                                      ; $36BE
    ldy #$00
    lda ($5F),y
    cmp #$C0
    bcs $36D1
    ldx #$01
    jsr $36E5
    jmp $36D9
    and #$3F
    bne $36D6
    rts
    jsr $36DC
    jmp $36C3
    tax
    inc $5F
    bne $36E3
    inc FKDEF
    lda ($5F),y
    sta ($61),y
    inc $61
    bne $36ED
    inc PALNTS
    dex
    bne $36E5
    inc $5F
    bne $36F6
    inc FKDEF
    rts
    lda VCOUNT
    cmp #$7A
    bcc wait_vblank
    rts
    pha
    lda #$00
    sta.w $0014
    lda ICHIDZ
    cmp.w $0014
    bne $3707
    pla
    rts
    pha
    lda #$32
    bne $3716
    pha
    lda #$01
    sta ICHIDZ
    bne $3700
    pha
    lda #$02
    bne $3716
    ora ($E9,x)
    brk
    adc $5504,x
    brk
    .byte $EF
    ora (WARMST,x)
    ora (ESIGN,x)
    ora (NGFLAG,x)
    ora (ESIGN,x)
    brk
    .byte $0C
    sei
    sty $C4C0
    .byte $FC
    iny
    bvs $3788
    .byte $A3
    rti
    sbc $410F,x
    brk
    rti
    cpy #$A0
    brk
    lda ($5F),y
    cmp #$C0
    bcs $3751
    ldx #$01
    jsr $36E5
    jmp $36D9
    and #$3F
    bne $3756
    rts
    jsr $36DC
    jmp $36C3
    tax
    inc $5F
    bne $3763
    inc FKDEF
    lda ($5F),y
    sta ($61),y
    inc $61
    bne $376D
    inc PALNTS
    dex
    bne $3765
    inc $5F
    bne $3776
    inc FKDEF
    rts
    lda VCOUNT
    cmp #$7A
    bcc $3777
    rts
    pha
    sta $3007
    ldx #$1F
    lda $0418,x
    sta $0100,x
    dex
    cpx #$FF
    bne $3785
    rts
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    jsr DSKINV
    lda #$C0
    sta NMIEN
    rts
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF
    .byte $FF