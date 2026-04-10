; =============================================================
; Segment 7: $9100-$922D — 
; =============================================================
; XEX load range: $9100-$922D (302 bytes)

    org $9100

irq_stage0:
    ; IRQ STAGE 0 — toggle PRIOR $E1 (16 hues) → $61 (16 luminances), chain to $9126
    sta $03
    lda #$E1
    ; Set PRIOR=$E1 — GTIA mode 3: nibble selects hue, COLBK sets luminance
    sta PRIOR
    ; Acknowledge IRQ: IRQEN=0 then IRQEN=$04 (re-enable Timer 4)
    lda #$00
    sta IRQEN
    lda #$04
    sta IRQEN
    lda #$26
    ; Self-modify: set $FFFE=$26 → next IRQ dispatches to $9126
    sta $FFFE
    ; LDA VCOUNT → STA COLPM3: raster bar effect on player 3
    lda VCOUNT
    sta COLPM3
    lda $FF
    lda #$61
    ; Set PRIOR=$61 — GTIA mode 1: nibble selects luminance, COLBK sets hue
    sta PRIOR
    lda $03
    rti
irq_stage1:
    ; IRQ STAGE 1 — same PRIOR toggle, 4 NOPs for timing, chain to $914A
    sta $03
    lda #$E1
    sta PRIOR
    lda #$00
    sta IRQEN
    lda #$04
    sta IRQEN
    lda #$4A
    sta $FFFE
    nop
    nop
    nop
    nop
    lda $FF
    lda #$61
    sta PRIOR
    lda $03
    rti
irq_stage2:
    ; IRQ STAGE 2 — same PRIOR toggle + VCOUNT raster bar, chain to $9170
    sta $03
    lda #$E1
    sta PRIOR
    lda #$00
    sta IRQEN
    lda #$04
    sta IRQEN
    lda #$70
    sta $FFFE
    lda VCOUNT
    sta COLPM3
    lda $FF
    lda #$61
    sta PRIOR
    lda $03
    rti
irq_stage3:
    ; IRQ STAGE 3 — PRIOR toggle + loop controller: DEC $04, if >0 loop to $9100
    sta $03
    lda #$E1
    sta PRIOR
    lda #$00
    sta IRQEN
    lda #$04
    sta IRQEN
    dec irq_scanline_ctr
    ; Counter >0: loop back (set $FFFE=$00 → $9100), 2 NOPs for timing
    beq loc_9194
    lda #$00
    sta $FFFE
    nop
    nop
    lda #$61
    sta PRIOR
    lda $03
    rti
loc_9194:
    ; Counter =0: viewport done (set $FFFE=$A3 → $91A3 bottom handler)
    lda #$A3
    sta $FFFE
    lda $FF
    lda #$61
    sta PRIOR
    lda $03
    rti
irq_bottom:
    ; IRQ BOTTOM — viewport complete: PRIOR=$21 (normal mode), set text colors
    sta $03
    ; PRIOR=$21 — exit GTIA mode, standard 4-color playfield for status bar
    lda #$21
    sta PRIOR
    lda #$00
    ; Disable Timer 4 IRQ (no more PRIOR switching needed this frame)
    sta IRQEN
    lda #$00
    sta $FFFE
    ; Set text colors: COLPF1=$0E (white), COLPF2=$34 (dark blue)
    lda #$0E
    sta COLPF1
    lda #$34
    sta COLPF2
    lda $03
    rti
nmi_immediate:
    ; NMI IMMEDIATE VBI — enable Timer 4 IRQ, reset scanline counter, chain to deferred
    sta LNFLG
    ; IRQEN=$04 — enable Timer 4 underflow IRQ for new frame
    lda #$04
    sta IRQEN
    ; $04=$10 — reset scanline counter to 16 (16 iterations × 8 scanlines = 128)
    lda #$10
    sta irq_scanline_ctr
    ; Self-modify NMI: $FFFA=$D4 → next NMI goes to deferred handler $91D4
    lda #$D4
    sta $FFFA
    lda LNFLG
    rti
nmi_deferred:
    ; NMI DEFERRED VBI — set viewport colors, music player, FPS timing
    sta LNFLG
    stx NGFLAG
    sty CASINI
    ; Set 3D viewport colors: COLPF1=$02 (dark), COLPF2=$EC (green)
    lda #$02
    sta COLPF1
    lda #$EC
    sta COLPF2
    ; INC $13 — VBI sub-counter; INC $14 — VBI frame counter
    inc $13
    ; JSR vbi_helper — FPS calculation and background fade
    jsr vbi_helper
    inc $14
    ; Music dispatch: $89=trigger, $8B=state; routes to RMT player or silence
    lda $89
    bne loc_91F9
    lda $8B
    beq loc_9222
    jsr rmt_player
    jmp loc_9222
loc_91F9:
    bmi loc_9217
    cmp #$01
    bne loc_9207
    lda #$01
    sta $8B
    lda #$00
    beq loc_920D
loc_9207:
    lda #$02
    sta $8B
    lda #$0B
loc_920D:
    ldx #$00
    ldy #$28
    jsr sub_3300
    jmp loc_921E
loc_9217:
    lda #$00
    sta $8B
    jsr rmt_silence
loc_921E:
    lda #$00
    sta $89
loc_9222:
    ; Restore NMI: $FFFA=$C1 → next NMI goes back to immediate handler $91C1
    lda #$C1
    sta $FFFA
    lda LNFLG
    ldx NGFLAG
    ldy CASINI
    rti