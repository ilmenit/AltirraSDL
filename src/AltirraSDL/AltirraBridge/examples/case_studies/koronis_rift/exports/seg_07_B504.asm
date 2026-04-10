; =============================================================
; Segment 7: $B504-$B658 — Decompressor routine at B504-B658
; =============================================================
; XEX load range: $B504-$B658 (341 bytes)

    org $B504

decompress:
    ldx #$0B
    lda ICHIDZ,x
    pha
    cpx #$04
    bcs $B512
    lda decomp_params,x
    sta ICHIDZ,x
    dex
    bpl $B506
    ldy #$00
    lda (ICCOMZ),y
    tax
    sta ICBALZ
    lda ICCOMZ
    sec
    sbc ICBALZ
    sta ICCOMZ
    lda ICSTAZ
    sbc #$00
    sta ICSTAZ
    lda (ICCOMZ),y
    sta $B6F6,y
    iny
    dex
    bne $B529
    dey
    tya
    clc
    adc #$F6
    sta $B5BA
    lda #$B6
    adc #$00
    sta $B5BE
    ldx #$03
    ldy #$00
    lda ICCOMZ
    bne $B54B
    dec ICSTAZ
    dec ICCOMZ
    cpx #$00
    beq $B556
    lda (ICCOMZ),y
    sta.w ICBALZ,x
    dex
    bpl $B545
    inx
    inx
    tya
    and #$0F
    beq $B574
    txa
    lsr
    ldx $B658,y
    rol
    rol ICBLHZ
    dex
    bpl $B565
    adc $B68C,y
    tax
    lda ICBLHZ
    adc $B6C0,y
    sta $B6C1,y
    txa
    sta $B68D,y
    ldx #$04
    jsr $B596
    sta $B659,y
    iny
    cpy #$34
    bne $B55A
    ldy #$00
    jsr $B5E7
    ldx #$F4
    pla
    sta ENTVEC,x
    inx
    bne $B58F
    rts
    lda #$00
    sta ICBLHZ
    cpx #$01
    bcc $B5D9
    lsr ICBAHZ
    bne $B5D3
    pha
    sty ICBALZ
    ldy #$00
    lda (ICCOMZ),y
    php
    pha
    ldy ICBALZ
    lda ICSTAZ
    cmp ICDNOZ
    bne $B5C3
    lda ICCOMZ
    cmp ICHIDZ
    bne $B5C3
    lda #$FF
    sta ICCOMZ
    lda #$FF
    sta ICSTAZ
    bne $B5CB
    lda ICCOMZ
    bne $B5C9
    dec ICSTAZ
    dec ICCOMZ
    pla
    plp
    bcc $B5E2
    ror
    sta ICBAHZ
    pla
    rol
    rol ICBLHZ
    dex
    bne $B59E
    rts
    dex
    dec ICPTHZ
    dec ICAX2Z
    dey
    lda (ICAX1Z),y
    sta (ICPTLZ),y
    tya
    bne $B5DF
    txa
    bne $B5DA
    dey
    inx
    jsr $B59E
    lsr
    iny
    bcc $B5EB
    beq $B620
    cpy #$11
    bcs $B652
    ldx $B658,y
    jsr $B596
    adc $B68C,y
    sta ICBLLZ
    lda ICBLHZ
    adc $B6C0,y
    pha
    bne $B612
    ldy ICBLLZ
    cpy #$04
    bcc $B614
    ldy #$03
    ldx $B652,y
    jsr $B596
    adc $B655,y
    tay
    lda ICBLLZ
    sbc ICPTLZ
    bcc $B626
    dec ICPTHZ
    eor #$FF
    sta ICPTLZ
    cpy #$01
    bcs $B631
    jmp $B5A3
    ldx $B659,y
    jsr $B596
    adc $B68D,y
    bcc $B63F
    inc ICBLHZ
    clc
    adc ICPTLZ
    sta ICAX1Z
    lda ICBLHZ
    adc $B6C1,y
    adc ICPTHZ
    sta ICAX2Z
    pla
    tax
    ldy ICBLLZ
    bcc $B5E4
    rts
    .byte $02
    .byte $04
    .byte $04
    bmi $B678
    .byte $10                     ; $B658