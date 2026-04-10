; =============================================================
; Segment 0: $4000-$5894 — 
; =============================================================
; XEX load range: $4000-$5894 (6293 bytes)
; INI at $4000 (init_routine)
; WARNING: This segment is overwritten by segment 5

    org $4000

    ; INIT ROUTINE — runs during XEX loading, copies tables to RAM under OS ROM, then returns
    sei
    lda #$00
    ; Disable interrupts and DMA
    sta IRQEN
    sta NMIEN
    ; PORTB AND #$FE — disable OS ROM, expose RAM at $C000-$FFFF
    lda PORTB
    and #$FE
    sta PORTB
    lda #$00
    sta SDMCTL
    sta DMACTL
    sta COLOR4
    sta COLBK
    lda PAL
    and #$0E
    ; COPY 1: $409B+ → $C000+ (4096 bytes) — sine/cosine/perspective tables
    bne $4024
    ldy #$00
    lda #$9B
    sta FPTR2
    ; Source pointer: $FE/$FF = $409B
    lda #$40
    sta $FF
    ; Dest pointer: $FC/$FD = $C000
    lda #$00
    sta FLPTR
    ; Counter: $FA/$FB = $1000 (4096 bytes)
    lda #$C0
    sta $FD
    lda #$00
    sta $FA
    lda #$10
    sta $FB
    ; Copy loop: LDA (src),Y → STA (dst),Y, 256-byte pages
    lda (FPTR2),y
    sta (FLPTR),y
    iny
    bne $404B
    inc $FF
    inc $FD
    lda $FA
    bne $4051
    dec $FB
    dec $FA
    lda $FA
    ora $FB
    bne $4040
    ldy #$00
    ; COPY 2: $509B+ → $F800+ (2042 bytes) — charset + HW vectors
    lda #$9B
    sta FPTR2
    lda #$50
    sta $FF
    lda #$00
    sta FLPTR
    lda #$F8
    sta $FD
    lda #$FA
    sta $FA
    lda #$07
    sta $FB
    ; Second copy loop
    lda (FPTR2),y
    sta (FLPTR),y
    iny
    bne $407E
    inc $FF
    inc $FD
    lda $FA
    bne $4084
    dec $FB
    dec $FA
    lda $FA
    ora $FB
    bne $4073
    lda PORTB
    ora #$01
    ; CLI — re-enable interrupts
    sta PORTB
    cli
    lda #$40
    sta NMIEN
    rts
    .byte $00,$00,$01,$02,$04,$06,$09,$0C,$10,$14,$19,$1E,$24,$2A,$31,$38 ; $409B
    .byte $40,$48,$51,$5A,$64,$6E,$79,$84,$90,$9C,$A9,$B6,$C4,$D2,$E1,$F0 ; $40AB
    .byte $00,$10,$21,$32,$44,$56,$69,$7C,$90,$A4,$B9,$CE,$E4,$FA,$11,$28 ; $40BB
    .byte $40,$58,$71,$8A,$A4,$BE,$D9,$F4,$10,$2C,$49,$66,$84,$A2,$C1,$E0 ; $40CB
    .byte $00,$20,$41,$62,$84,$A6,$C9,$EC,$10,$34,$59,$7E,$A4,$CA,$F1,$18 ; $40DB
    .byte $40,$68,$91,$BA,$E4,$0E,$39,$64,$90,$BC,$E9,$16,$44,$72,$A1,$D0 ; $40EB
    .byte $00,$30,$61,$92,$C4,$F6,$29,$5C,$90,$C4,$F9,$2E,$64,$9A,$D1,$08 ; $40FB
    .byte $40,$78,$B1,$EA,$24,$5E,$99,$D4,$10,$4C,$89,$C6,$04,$42,$81,$C0 ; $410B
    .byte $00,$40,$81,$C2,$04,$46,$89,$CC,$10,$54,$99,$DE,$24,$6A,$B1,$F8 ; $411B
    .byte $40,$88,$D1,$1A,$64,$AE,$F9,$44,$90,$DC,$29,$76,$C4,$12,$61,$B0 ; $412B
    .byte $00,$50,$A1,$F2,$44,$96,$E9,$3C,$90,$E4,$39,$8E,$E4,$3A,$91,$E8 ; $413B
    .byte $40,$98,$F1,$4A,$A4,$FE,$59,$B4,$10,$6C,$C9,$26,$84,$E2,$41,$A0 ; $414B
    .byte $00,$60,$C1,$22,$84,$E6,$49,$AC,$10,$74,$D9,$3E,$A4,$0A,$71,$D8 ; $415B
    .byte $40,$A8,$11,$7A,$E4,$4E,$B9,$24,$90,$FC,$69,$D6,$44,$B2,$21,$90 ; $416B
    .byte $00,$70,$E1,$52,$C4,$36,$A9,$1C,$90,$04,$79,$EE,$64,$DA,$51,$C8 ; $417B
    .byte $40,$B8,$31,$AA,$24,$9E,$19,$94,$10,$8C,$09,$86,$04,$82,$01,$80 ; $418B
    .byte $00,$80                                          ; $419B
    ; SETUP RAY — compute ray direction from player angle + column offset, select quadrant
    .byte $01,$82,$04,$86,$09,$8C,$10,$94,$19,$9E,$24,$AA,$31,$B8,$40,$C8 ; $419D
    .byte $51,$DA,$64,$EE,$79,$04,$90,$1C,$A9,$36,$C4,$52,$E1,$70,$00 ; $41AD
    ; Determine ray quadrant (self-modifying: patches JSR target in cast_ray)
    .byte $90,$21,$B2,$44,$D6,$69,$FC,$90,$24,$B9,$4E,$E4,$7A,$11,$A8,$40 ; $41BC
    .byte $D8,$71,$0A,$A4,$3E,$D9,$74,$10,$AC,$49,$E6,$84,$22,$C1,$60,$00 ; $41CC
    .byte $A0,$41,$E2,$84,$26,$C9                          ; $41DC
    ; Setup X step direction based on quadrant
    .byte $6C,$10,$B4,$59,$FE,$A4,$4A,$F1,$98,$40,$E8,$91,$3A,$E4 ; $41E2
    ; Setup Y step direction based on quadrant
    .byte $8E,$39,$E4,$90,$3C,$E9,$96,$44,$F2,$A1          ; $41F0
    ; ADVANCE RAY — step ray angle to next viewport column
    .byte $50,$00,$B0,$61,$12,$C4,$76,$29,$DC,$90,$44,$F9,$AE,$64,$1A,$D1 ; $41FA
    .byte $88,$40,$F8,$B1,$6A,$24,$DE,$99,$54,$10,$CC,$89,$46,$04,$C2,$81 ; $420A
    .byte $40,$00,$C0,$81,$42,$04,$C6,$89,$4C,$10,$D4,$99,$5E,$24,$EA,$B1 ; $421A
    .byte $78,$40,$08,$D1,$9A,$64,$2E,$F9,$C4,$90,$5C,$29,$F6,$C4,$92,$61 ; $422A
    .byte $30,$00,$D0,$A1,$72,$44,$16,$E9,$BC              ; $423A
    ; Boundary handling: wrap ray angle, reconfigure step directions
    .byte $90,$64,$39,$0E,$E4,$BA,$91,$68,$40,$18,$F1,$CA,$A4,$7E,$59,$34 ; $4243
    .byte $10,$EC,$C9,$A6,$84,$62,$41,$20,$00,$E0,$C1,$A2,$84,$66,$49,$2C ; $4253
    .byte $10,$F4,$D9,$BE,$A4,$8A,$71,$58,$40,$28,$11,$FA,$E4,$CE,$B9,$A4 ; $4263
    .byte $90,$7C,$69,$56,$44,$32,$21,$10,$00,$F0,$E1,$D2,$C4,$B6,$A9,$9C ; $4273
    .byte $90,$84,$79,$6E,$64,$5A,$51,$48,$40,$38,$31,$2A,$24,$1E,$19,$14 ; $4283
    .byte $10,$0C,$09,$06,$04,$02,$01                      ; $4293
    :33 .byte $00                    ; $429A (33 zero bytes)
    :14 .byte $01                    ; $42BB (14 bytes)
    :10 .byte $02                    ; $42C9 (10 bytes)
    :8 .byte $03                    ; $42D3 (8 bytes)
    :8 .byte $04                    ; $42DB (8 bytes)
    :7 .byte $05                    ; $42E3 (7 bytes)
    :6 .byte $06                    ; $42EA (6 bytes)
    :6 .byte $07                    ; $42F0 (6 bytes)
    :5 .byte $08                    ; $42F6 (5 bytes)
    :6 .byte $09                    ; $42FB (6 bytes)
    :5 .byte $0A                    ; $4301 (5 bytes)
    :4 .byte $0B                    ; $4306 (4 bytes)
    :5 .byte $0C                    ; $430A (5 bytes)
    :4 .byte $0D                    ; $430F (4 bytes)
    :4 .byte $0E                    ; $4313 (4 bytes)
    :4 .byte $0F                    ; $4317 (4 bytes)
    :4 .byte $10                    ; $431B (4 bytes)
    :4 .byte $11                    ; $431F (4 bytes)
    :4 .byte $12                    ; $4323 (4 bytes)
    :4 .byte $13                    ; $4327 (4 bytes)
    .byte $14,$14,$14                                      ; $432B
    :4 .byte $15                    ; $432E (4 bytes)
    .byte $16,$16,$16,$17,$17,$17,$18,$18,$18              ; $4332
    :4 .byte $19                    ; $433B (4 bytes)
    .byte $1A,$1A,$1A,$1B,$1B,$1B,$1C,$1C,$1C,$1D,$1D,$1D,$1E,$1E,$1E,$1F ; $433F
    .byte $1F,$1F,$20,$20,$21,$21,$21,$22,$22,$22,$23,$23,$24,$24,$24,$25 ; $434F
    .byte $25,$25,$26,$26,$27,$27,$27,$28,$28,$29,$29,$29,$2A,$2A,$2B,$2B ; $435F
    .byte $2B,$2C,$2C,$2D,$2D,$2D,$2E,$2E,$2F,$2F,$30,$30,$31,$31,$31,$32 ; $436F
    .byte $32,$33,$33,$34,$34,$35,$35,$35,$36,$36,$37,$37,$38,$38,$39,$39 ; $437F
    .byte $3A,$3A,$3B,$3B,$3C,$3C,$3D,$3D,$3E,$3E,$3F,$3F,$40,$40,$41,$41 ; $438F
    ; CAST RAY — DDA (Digital Differential Analyzer) wall detection through map grid
    .byte $42,$42,$43,$43,$44,$44,$45,$45                  ; $439F
    ; Load precomputed ray step increments from tables at $4A00-$4C09
    .byte $46,$46,$47,$47,$48,$48,$49,$49,$4A,$4A,$4B,$4C,$4C,$4D,$4D,$4E ; $43A7
    .byte $4E,$4F,$4F,$50,$51,$51,$52,$52,$53,$53,$54,$54,$55,$56,$56,$57 ; $43B7
    .byte $57,$58,$59,$59,$5A,$5A,$5B,$5C,$5C,$5D,$5D,$5E,$5F,$5F,$60,$60 ; $43C7
    .byte $61,$62,$62,$63,$64,$64,$65,$65,$66,$67,$67,$68,$69,$69,$6A,$6A ; $43D7
    .byte $6B,$6C,$6C,$6D,$6E,$6E,$6F,$70,$70,$71,$72,$72,$73,$74,$74,$75 ; $43E7
    .byte $76,$76,$77,$78,$79,$79,$7A,$7B,$7B,$7C,$7D      ; $43F7
    ; DDA main loop: step through grid cells, test for wall hit
    .byte $7D,$7E,$7F,$7F,$80,$81,$82,$82                  ; $4402
    ; Compare ray integer position against step boundary
    .byte $83,$84,$84,$85,$86,$87,$87,$88,$89,$8A,$8A,$8B,$8C,$8D,$8D,$8E ; $440A
    .byte $8F,$90,$90,$91                                  ; $441A
    ; Step to next grid cell (fractional accumulator)
    .byte $92,$93,$93,$94,$95,$96,$96,$97,$98,$99,$99,$9A,$9B,$9C,$9D,$9D ; $441E
    .byte $9E,$9F,$A0,$A0,$A1,$A2,$A3,$A4,$A4,$A5,$A6,$A7,$A8,$A9,$A9,$AA ; $442E
    .byte $AB                                              ; $443E
    ; Wall hit: check wall type, compute texture column from hit position
    .byte $AC,$AD,$AD,$AE,$AF,$B0,$B1,$B2,$B2,$B3,$B4,$B5,$B6,$B7,$B7,$B8 ; $443F
    .byte $B9,$BA,$BB,$BC,$BD,$BD,$BE,$BF,$C0,$C1,$C2,$C3,$C4,$C4,$C5,$C6 ; $444F
    .byte $C7,$C8,$C9,$CA,$CB,$CB,$CC,$CD,$CE,$CF,$D0,$D1,$D2,$D3,$D4,$D4 ; $445F
    .byte $D5,$D6,$D7,$D8,$D9,$DA                          ; $446F
    ; Compute texture column offset (hit position >> 3 = nibble within wall)
    .byte $DB,$DC,$DD,$DE,$DF,$E0,$E1                      ; $4475
    ; SELF-MODIFYING: patch BSP comparison addresses from ray position
    .byte $E1,$E2,$E3,$E4,$E5,$E6,$E7,$E8,$E9,$EA          ; $447C
    ; Call BSP traversal to determine visible wall face
    .byte $EB,$EC,$ED,$EE,$EF,$F0                          ; $4486
    ; No wall hit: advance ray by full grid step, continue DDA
    .byte $F1,$F2,$F3,$F4,$F5,$F6,$F7,$F8,$F9,$FA,$FB,$FC,$FD,$FE,$FF,$80 ; $448C
    .byte $01,$82,$04,$86,$09,$8C,$10,$94,$19,$9E,$24,$AA,$31,$B8,$40,$C8 ; $449C
    .byte $51,$DA,$64,$EE,$79,$04,$90,$1C,$A9,$36,$C4,$52,$E1,$70,$00,$90 ; $44AC
    .byte $21,$B2,$44,$D6,$69,$FC,$90,$24,$B9,$4E,$E4,$7A,$11,$A8,$40,$D8 ; $44BC
    .byte $71,$0A,$A4,$3E,$D9,$74,$10,$AC,$49,$E6,$84,$22,$C1,$60,$00,$A0 ; $44CC
    .byte $41,$E2,$84,$26,$C9,$6C,$10,$B4,$59,$FE,$A4,$4A,$F1,$98,$40,$E8 ; $44DC
    .byte $91,$3A,$E4,$8E,$39,$E4,$90,$3C,$E9,$96,$44,$F2,$A1,$50,$00,$B0 ; $44EC
    .byte $61,$12,$C4,$76,$29,$DC,$90,$44,$F9,$AE,$64,$1A,$D1,$88,$40,$F8 ; $44FC
    .byte $B1,$6A,$24,$DE,$99,$54,$10,$CC,$89,$46,$04,$C2,$81,$40,$00,$C0 ; $450C
    .byte $81,$42,$04,$C6,$89,$4C,$10,$D4,$99,$5E,$24,$EA,$B1,$78,$40,$08 ; $451C
    .byte $D1,$9A,$64,$2E,$F9,$C4,$90,$5C,$29,$F6,$C4,$92,$61,$30,$00,$D0 ; $452C
    .byte $A1,$72,$44,$16,$E9,$BC,$90,$64,$39,$0E,$E4,$BA,$91,$68,$40,$18 ; $453C
    .byte $F1,$CA,$A4,$7E,$59,$34,$10,$EC,$C9,$A6,$84,$62,$41,$20,$00,$E0 ; $454C
    .byte $C1,$A2,$84,$66,$49,$2C,$10,$F4,$D9,$BE,$A4,$8A,$71,$58,$40,$28 ; $455C
    .byte $11,$FA,$E4,$CE,$B9,$A4,$90,$7C,$69,$56,$44,$32,$21,$10,$00,$F0 ; $456C
    .byte $E1,$D2,$C4,$B6,$A9,$9C,$90,$84,$79,$6E,$64,$5A,$51,$48,$40,$38 ; $457C
    .byte $31,$2A,$24,$1E,$19,$14,$10,$0C,$09,$06,$04,$02,$01,$00,$00,$00 ; $458C
    .byte $01,$02,$04,$06,$09,$0C,$10,$14,$19,$1E,$24,$2A,$31,$38,$40,$48 ; $459C
    .byte $51,$5A,$64,$6E,$79,$84,$90,$9C,$A9,$B6,$C4,$D2,$E1,$F0,$00,$10 ; $45AC
    .byte $21,$32,$44,$56,$69,$7C,$90,$A4,$B9,$CE,$E4,$FA,$11,$28,$40,$58 ; $45BC
    .byte $71,$8A,$A4,$BE,$D9,$F4,$10,$2C,$49,$66,$84,$A2,$C1,$E0,$00,$20 ; $45CC
    .byte $41,$62,$84,$A6,$C9,$EC,$10,$34,$59,$7E,$A4,$CA,$F1,$18,$40,$68 ; $45DC
    .byte $91,$BA,$E4,$0E,$39,$64,$90,$BC,$E9,$16,$44,$72,$A1,$D0,$00,$30 ; $45EC
    .byte $61,$92,$C4,$F6,$29,$5C,$90,$C4,$F9,$2E,$64,$9A,$D1,$08,$40,$78 ; $45FC
    .byte $B1,$EA,$24,$5E,$99,$D4,$10,$4C,$89,$C6,$04,$42,$81,$C0,$00,$40 ; $460C
    .byte $81,$C2,$04,$46,$89,$CC,$10,$54,$99,$DE,$24,$6A,$B1,$F8,$40,$88 ; $461C
    .byte $D1,$1A,$64,$AE,$F9,$44,$90,$DC,$29,$76,$C4,$12,$61,$B0,$00,$50 ; $462C
    .byte $A1,$F2,$44,$96,$E9,$3C,$90,$E4,$39,$8E,$E4,$3A,$91,$E8,$40,$98 ; $463C
    .byte $F1,$4A,$A4,$FE,$59,$B4,$10,$6C,$C9,$26,$84,$E2,$41,$A0,$00,$60 ; $464C
    .byte $C1,$22,$84,$E6,$49,$AC,$10,$74,$D9,$3E,$A4,$0A,$71,$D8,$40,$A8 ; $465C
    .byte $11,$7A,$E4,$4E,$B9,$24,$90,$FC,$69,$D6,$44,$B2,$21,$90,$00,$70 ; $466C
    .byte $E1,$52,$C4,$36,$A9,$1C,$90,$04,$79,$EE,$64,$DA,$51,$C8,$40,$B8 ; $467C
    .byte $31,$AA,$24,$9E,$19,$94,$10,$8C,$09,$86,$04,$82,$01,$80,$00,$3F ; $468C
    .byte $3F,$3E,$3E,$3D,$3D,$3C,$3C,$3B,$3B,$3A,$3A,$39,$39,$38,$38,$37 ; $469C
    .byte $37,$36,$36,$35,$35,$35,$34,$34,$33,$33,$32,$32,$31,$31,$31,$30 ; $46AC
    .byte $30,$2F,$2F,$2E,$2E,$2D,$2D,$2D,$2C,$2C,$2B,$2B,$2B,$2A,$2A,$29 ; $46BC
    .byte $29,$29,$28,$28,$27,$27,$27,$26,$26,$25,$25,$25,$24,$24,$24,$23 ; $46CC
    .byte $23,$22,$22,$22,$21,$21,$21,$20,$20,$1F,$1F,$1F,$1E,$1E,$1E,$1D ; $46DC
    .byte $1D,$1D,$1C,$1C,$1C,$1B,$1B,$1B,$1A,$1A,$1A      ; $46EC
    :4 .byte $19                    ; $46F7 (4 bytes)
    .byte $18,$18,$18,$17,$17,$17,$16,$16,$16              ; $46FB
    :4 .byte $15                    ; $4704 (4 bytes)
    .byte $14,$14,$14                                      ; $4708
    :4 .byte $13                    ; $470B (4 bytes)
    :4 .byte $12                    ; $470F (4 bytes)
    :4 .byte $11                    ; $4713 (4 bytes)
    :4 .byte $10                    ; $4717 (4 bytes)
    :4 .byte $0F                    ; $471B (4 bytes)
    :4 .byte $0E                    ; $471F (4 bytes)
    :4 .byte $0D                    ; $4723 (4 bytes)
    :5 .byte $0C                    ; $4727 (5 bytes)
    :4 .byte $0B                    ; $472C (4 bytes)
    :5 .byte $0A                    ; $4730 (5 bytes)
    :6 .byte $09                    ; $4735 (6 bytes)
    :5 .byte $08                    ; $473B (5 bytes)
    :6 .byte $07                    ; $4740 (6 bytes)
    :6 .byte $06                    ; $4746 (6 bytes)
    :7 .byte $05                    ; $474C (7 bytes)
    :8 .byte $04                    ; $4753 (8 bytes)
    :8 .byte $03                    ; $475B (8 bytes)
    :10 .byte $02                    ; $4763 (10 bytes)
    :14 .byte $01                    ; $476D (14 bytes)
    :63 .byte $00                    ; $477B (63 zero bytes)
    :14 .byte $01                    ; $47BA (14 bytes)
    :10 .byte $02                    ; $47C8 (10 bytes)
    :8 .byte $03                    ; $47D2 (8 bytes)
    :8 .byte $04                    ; $47DA (8 bytes)
    :7 .byte $05                    ; $47E2 (7 bytes)
    :6 .byte $06                    ; $47E9 (6 bytes)
    :6 .byte $07                    ; $47EF (6 bytes)
    :5 .byte $08                    ; $47F5 (5 bytes)
    :6 .byte $09                    ; $47FA (6 bytes)
    :5 .byte $0A                    ; $4800 (5 bytes)
    :4 .byte $0B                    ; $4805 (4 bytes)
    :5 .byte $0C                    ; $4809 (5 bytes)
    :4 .byte $0D                    ; $480E (4 bytes)
    :4 .byte $0E                    ; $4812 (4 bytes)
    :4 .byte $0F                    ; $4816 (4 bytes)
    :4 .byte $10                    ; $481A (4 bytes)
    :4 .byte $11                    ; $481E (4 bytes)
    :4 .byte $12                    ; $4822 (4 bytes)
    :4 .byte $13                    ; $4826 (4 bytes)
    .byte $14,$14,$14                                      ; $482A
    :4 .byte $15                    ; $482D (4 bytes)
    .byte $16,$16,$16,$17,$17,$17,$18,$18,$18              ; $4831
    :4 .byte $19                    ; $483A (4 bytes)
    .byte $1A,$1A,$1A,$1B,$1B,$1B,$1C,$1C,$1C,$1D,$1D,$1D,$1E,$1E,$1E,$1F ; $483E
    .byte $1F,$1F,$20,$20,$21,$21,$21,$22,$22,$22,$23,$23,$24,$24,$24,$25 ; $484E
    .byte $25,$25,$26,$26,$27,$27,$27,$28,$28,$29,$29,$29,$2A,$2A,$2B,$2B ; $485E
    .byte $2B,$2C,$2C,$2D,$2D,$2D,$2E,$2E,$2F,$2F,$30,$30,$31,$31,$31,$32 ; $486E
    .byte $32,$33,$33,$34,$34,$35,$35,$35,$36,$36,$37,$37,$38,$38,$39,$39 ; $487E
    .byte $3A,$3A,$3B,$3B,$3C,$3C,$3D,$3D,$3E,$3E,$3F,$3F,$40,$23,$15,$0F ; $488E
    .byte $0B,$09,$08,$07,$06,$05,$05,$04,$04              ; $489E
    :5 .byte $03                    ; $48A7 (5 bytes)
    :9 .byte $02                    ; $48AC (9 bytes)
    :26 .byte $01                    ; $48B5 (26 bytes)
    :12 .byte $00                    ; $48CF (12 zero bytes)
    .byte $83,$4E,$38,$D6,$AF,$32,$1A,$44,$9B,$13,$A2,$43,$F2,$AC,$70,$3A ; $48DB
    .byte $0B,$E1,$BB,$99,$7A,$5E,$44,$2D,$17,$03,$F0,$DE,$CE,$BF,$B1,$A4 ; $48EB
    .byte $97,$8B,$80,$76,$6C,$62,$59,$51,$49,$41,$39,$32,$2C,$25,$1F,$19 ; $48FB
    .byte $13,$0E,$09,$04,$FF,$FA,$F6,$F1,$ED,$E9,$E5,$E1,$DE,$DA,$D7,$D3 ; $490B
    .byte $23,$15,$0F,$0B,$09,$08,$07,$06,$05,$05,$04,$04  ; $491B
    :5 .byte $03                    ; $4927 (5 bytes)
    :9 .byte $02                    ; $492C (9 bytes)
    :26 .byte $01                    ; $4935 (26 bytes)
    :12 .byte $00                    ; $494F (12 zero bytes)
    .byte $8E,$55,$3D,$DA,$B2,$34,$1C,$46,$9D,$14,$A3,$44,$F3,$AE,$71,$3B ; $495B
    .byte $0C,$E2,$BC,$9A,$7B,$5F,$45,$2D,$17,$03,$F0,$DF,$CF,$C0,$B1,$A4 ; $496B
    .byte $98,$8C,$81,$76,$6C,$63,$5A,$51,$49,$41,$3A,$33,$2C,$26,$1F,$19 ; $497B
    .byte $14,$0E,$09,$04,$FF,$FB,$F6,$F2,$ED,$E9,$E5,$E2,$DE,$DA,$D7,$D4 ; $498B
    .byte $23,$15,$0F,$0B,$09,$08,$07,$06,$05,$05,$04,$04  ; $499B
    :5 .byte $03                    ; $49A7 (5 bytes)
    :9 .byte $02                    ; $49AC (9 bytes)
    :27 .byte $01                    ; $49B5 (27 bytes)
    :11 .byte $00                    ; $49D0 (11 zero bytes)
    .byte $A4,$62,$46,$E1,$B8,$3A,$21,$4A,$A1,$17,$A6,$47,$F6,$B0,$73,$3D ; $49DB
    .byte $0E,$E4,$BE,$9C,$7D,$60,$46,$2F,$19,$04,$F2,$E0,$D0,$C1,$B2,$A5 ; $49EB
    .byte $99,$8D,$82,$77,$6D,$63,$5A,$52,$4A,$42,$3B,$34,$2D,$26,$20,$1A ; $49FB
    .byte $14,$0F,$0A,$05,$00,$FB,$F7,$F2,$EE,$EA,$E6,$E2,$DF,$DB,$D8,$D4 ; $4A0B
    .byte $23,$15,$0F,$0B,$09,$08,$07,$06,$05,$05,$04,$04  ; $4A1B
    :5 .byte $03                    ; $4A27 (5 bytes)
    :9 .byte $02                    ; $4A2C (9 bytes)
    :27 .byte $01                    ; $4A35 (27 bytes)
    :11 .byte $00                    ; $4A50 (11 zero bytes)
    .byte $C5,$76,$55,$EC,$C1,$41,$27,$50,$A6,$1C,$AA,$4B,$F9,$B3,$76,$40 ; $4A5B
    .byte $11,$E6,$C0,$9E,$7F,$62,$49,$31,$1B,$06,$F3,$E2,$D2,$C2,$B4,$A7 ; $4A6B
    .byte $9A,$8E,$83,$78,$6E,$65,$5C,$53,$4B,$43,$3C,$35,$2E,$27,$21,$1B ; $4A7B
    .byte $15,$10,$0B,$06,$01,$FC,$F7,$F3,$EF,$EB,$E7,$E3,$DF,$DC,$D8,$D5 ; $4A8B
    .byte $23,$15,$0F,$0B,$09,$08,$07,$06,$05,$05,$04,$04  ; $4A9B
    :5 .byte $03                    ; $4AA7 (5 bytes)
    :9 .byte $02                    ; $4AAC (9 bytes)
    :27 .byte $01                    ; $4AB5 (27 bytes)
    :11 .byte $00                    ; $4AD0 (11 zero bytes)
    .byte $F2,$91,$68,$FB,$CE,$4C,$30,$58,$AD,$23,$B0,$50,$FE,$B8,$7B,$45 ; $4ADB
    .byte $15,$EA,$C4,$A1,$82,$65,$4B,$33,$1D,$09,$F6,$E4,$D4,$C5,$B6,$A9 ; $4AEB
    .byte $9C,$90,$85,$7A,$70,$67,$5D,$55,$4D,$45,$3D,$36,$2F,$29,$23,$1D ; $4AFB
    .byte $17,$11,$0C,$07,$02,$FD,$F9,$F4,$F0,$EC,$E8,$E4,$E0,$DD,$D9,$D6 ; $4B0B
    .byte $24,$15,$0F,$0C,$09,$08,$07,$06,$05,$05,$04,$04,$04 ; $4B1B
    :4 .byte $03                    ; $4B28 (4 bytes)
    :9 .byte $02                    ; $4B2C (9 bytes)
    :27 .byte $01                    ; $4B35 (27 bytes)
    :11 .byte $00                    ; $4B50 (11 zero bytes)
    .byte $2B,$B4,$80,$0E,$DD,$59,$3C,$62,$B6,$2B,$B8,$57,$05,$BE,$80,$4A ; $4B5B
    .byte $1A,$EF,$C8,$A6,$86,$69,$4F,$37,$21,$0C,$F9,$E7,$D7,$C7,$B9,$AB ; $4B6B
    .byte $9F,$93,$87,$7D,$72,$69,$60,$57,$4F,$47,$3F,$38,$31,$2B,$24,$1E ; $4B7B
    .byte $19,$13,$0E,$09,$04,$FF,$FA,$F6,$F2,$ED,$E9,$E6,$E2,$DE,$DB,$D7 ; $4B8B
    .byte $24,$15,$0F,$0C,$09,$08,$07,$06,$05,$05,$04,$04,$04 ; $4B9B
    :4 .byte $03                    ; $4BA8 (4 bytes)
    :9 .byte $02                    ; $4BAC (9 bytes)
    :28 .byte $01                    ; $4BB5 (28 bytes)
    :10 .byte $00                    ; $4BD1 (10 zero bytes)
    .byte $70,$DD,$9E,$25,$F0,$69,$4A,$6E,$C1,$35,$C1,$5F,$0C,$C5,$87,$50 ; $4BDB
    .byte $20,$F4,$CE,$AB,$8B,$6E,$53,$3B,$25,$10,$FD,$EB,$DA,$CB,$BC,$AF ; $4BEB
    .byte $A2,$96,$8A,$7F,$75,$6B,$62,$59,$51,$49,$42,$3A,$34,$2D,$27,$21 ; $4BFB
    .byte $1B,$15,$10,$0B,$06,$01,$FC,$F8,$F3,$EF,$EB,$E7,$E4,$E0,$DC,$D9 ; $4C0B
    .byte $24,$16,$0F,$0C,$0A,$08,$07,$06,$05,$05,$04,$04,$04 ; $4C1B
    :4 .byte $03                    ; $4C28 (4 bytes)
    :10 .byte $02                    ; $4C2C (10 bytes)
    .byte $01                                              ; $4C36
    ; PERSPECTIVE PROJECT — compute screen column heights from ray distances
    :4 .byte $01                    ; $4C37 (4 bytes)
    ; SELF-MODIFYING: negate delta for subtraction, patch inline operands
    :22 .byte $01                    ; $4C3B (22 bytes)
    :9 .byte $00                    ; $4C51 (9 zero bytes)
    ; Loop over ray steps: compute wall/floor heights using $C000-$C6FF tables (RAM under ROM)
    .byte $00,$C2,$0E,$C1,$41,$06,$7B,$5A,$7D,$CE,$40,$CB,$69,$16,$CD,$8F ; $4C5A
    .byte $57,$27,$FB,$D4,$B1,$90,$73,$59,$40,$2A,$15,$01,$EF,$DE,$CF,$C0 ; $4C6A
    .byte $B2,$A5,$99,$8E,$83,$78,$6F,$65,$5D,$54,$4C,$44,$3D,$36,$30,$29 ; $4C7A
    .byte $23,$1D,$17,$12,$0D,$08,$03,$FE,$FA,$F5,$F1,$ED,$E9,$E6,$E2,$DE ; $4C8A
    .byte $DB,$25,$16,$0F,$0C,$0A,$08,$07,$06,$05,$05,$04,$04,$04 ; $4C9A
    :5 .byte $03                    ; $4CA8 (5 bytes)
    ; Process vertical extent (floor-to-ceiling)
    :9 .byte $02                    ; $4CAD (9 bytes)
    :28 .byte $01                    ; $4CB6 (28 bytes)
    :9 .byte $00                    ; $4CD2 (9 zero bytes)
    .byte $20,$47,$E9,$60,$20,$91,$6D,$8D,$DD,$4E,$D8,$75,$20,$D7,$98,$60 ; $4CDB
    .byte $2F,$03,$DB,$B7,$97,$7A,$5F,$46,$2F,$1A,$06,$F4,$E3,$D3,$C5,$B7 ; $4CEB
    .byte $AA,$9D,$92,$87,$7C,$72,$69,$60,$58,$4F,$48,$40,$39,$33,$2C,$26 ; $4CFB
    .byte $20,$1A,$15,$10,$0A,$06,$01,$FC,$F8,$F4,$F0,$EC,$E8,$E4,$E1,$DD ; $4D0B
    .byte $25,$16,$10,$0C,$0A,$08,$07,$06,$05,$05,$04,$04,$04 ; $4D1B
    :5 .byte $03                    ; $4D28 (5 bytes)
    :9 .byte $02                    ; $4D2D (9 bytes)
    :28 .byte $01                    ; $4D36 (28 bytes)
    :9 .byte $00                    ; $4D52 (9 zero bytes)
    .byte $8D,$88,$18,$84,$3E,$AA,$83,$A0,$EE,$5D,$E6,$82,$2C,$E2,$A2,$6A ; $4D5B
    .byte $38,$0B,$E3,$BF,$9F,$81,$66,$4D,$35,$20,$0C,$FA,$E9,$D9,$CA,$BC ; $4D6B
    .byte $AE,$A2,$96,$8B,$81,$77,$6D,$64,$5B,$53,$4B,$44,$3D,$36,$30,$29 ; $4D7B
    .byte $23,$1E,$18,$13,$0E,$09,$04,$FF,$FB,$F6,$F2,$EE,$EA,$E7,$E3,$E0 ; $4D8B
    .byte $26,$16,$10,$0C,$0A,$08,$07,$06,$06,$05,$04,$04,$04 ; $4D9B
    :5 .byte $03                    ; $4DA8 (5 bytes)
    :10 .byte $02                    ; $4DAD (10 bytes)
    :17 .byte $01                    ; $4DB7 (17 bytes)
    ; Column clear/draw routine: writes to framebuffer at $D800+ (RAM under ROM)
    :11 .byte $01                    ; $4DC8 (11 bytes)
    :8 .byte $00                    ; $4DD3 (8 zero bytes)
    .byte $08,$D2,$4D,$AD,$5F,$C7,$9B,$B6,$01,$6F,$F6,$90,$3A,$EF,$AE,$75 ; $4DDB
    .byte $43,$15,$ED,$C8,$A7,$89,$6D,$54,$3D,$27,$13,$00,$EF,$DF,$D0,$C1 ; $4DEB
    .byte $B4,$A7,$9B,$90,$85,$7B,$72,$69,$60,$58,$50,$48,$41,$3A,$33,$2D ; $4DFB
    .byte $27,$21,$1C,$16,$11,$0C,$07,$02,$FE,$FA,$F5,$F1,$ED,$EA,$E6,$E2 ; $4E0B
    .byte $26,$17,$10,$0C,$0A,$08,$07,$06,$06,$05,$05,$04,$04 ; $4E1B
    :5 .byte $03                    ; $4E28 (5 bytes)
    :10 .byte $02                    ; $4E2D (10 bytes)
    :29 .byte $01                    ; $4E37 (29 bytes)
    :7 .byte $00                    ; $4E54 (7 zero bytes)
    .byte $92,$24,$88,$DB,$85,$E7,$B7,$CE,$17,$83,$08,$A1,$49,$FD,$BC,$82 ; $4E5B
    .byte $4E,$21,$F8,$D2,$B1,$92,$76,$5D,$45,$2F,$1B,$08,$F6,$E6,$D6,$C8 ; $4E6B
    .byte $BA,$AD,$A1,$96,$8B,$81,$77,$6E,$65,$5C,$54,$4D,$46,$3F,$38,$31 ; $4E7B
    .byte $2B,$25,$20,$1A,$15,$10,$0B,$06,$02,$FD,$F9,$F5,$F1,$ED,$E9,$E6 ; $4E8B
    .byte $27,$17,$10,$0D,$0A,$09,$07,$06,$06,$05,$05,$04,$04,$04 ; $4E9B
    :5 .byte $03                    ; $4EA9 (5 bytes)
    :9 .byte $02                    ; $4EAE (9 bytes)
    :30 .byte $01                    ; $4EB7 (30 bytes)
    :6 .byte $00                    ; $4ED5 (6 zero bytes)
    .byte $2D,$81,$CA,$0F,$AF,$0A,$D6,$EA,$2F,$99,$1C,$B3,$5A,$0D,$CB,$90 ; $4EDB
    .byte $5C,$2D,$03,$DE,$BC,$9D,$80,$66,$4E,$38,$23,$10,$FE,$ED,$DE,$CF ; $4EEB
    .byte $C1,$B4,$A8,$9C,$91,$87,$7D,$73,$6A,$62,$5A,$52,$4B,$44,$3D,$36 ; $4EFB
    .byte $30,$2A,$24,$1F,$19,$14,$0F,$0A,$06,$01,$FD,$F9,$F5,$F1,$ED,$E9 ; $4F0B
    .byte $27,$17,$11,$0D,$0A,$09,$07,$07,$06,$05,$05,$04,$04,$04 ; $4F1B
    :5 .byte $03                    ; $4F29 (5 bytes)
    :10 .byte $02                    ; $4F2E (10 bytes)
    :30 .byte $01                    ; $4F38 (30 bytes)
    :5 .byte $00                    ; $4F56 (5 zero bytes)
    .byte $D9,$E9,$14,$48,$DE,$32,$F8,$08,$4B,$B1,$33,$C8,$6D,$1F,$DB,$9F ; $4F5B
    .byte $6A,$3B,$11,$EA,$C8,$A8,$8B,$71,$58,$41,$2C,$19,$07,$F6,$E6,$D7 ; $4F6B
    .byte $C9,$BC,$AF,$A3,$98,$8D,$83,$7A,$71,$68,$60,$58,$50,$49,$42,$3B ; $4F7B
    .byte $35,$2F,$29,$23,$1E,$19,$14,$0F,$0A,$06,$01,$FD,$F9,$F5,$F1,$ED ; $4F8B
    .byte $28,$18,$11,$0D,$0B,$09,$08,$07,$06,$05,$05,$04,$04,$04 ; $4F9B
    :5 .byte $03                    ; $4FA9 (5 bytes)
    :10 .byte $02                    ; $4FAE (10 bytes)
    :31 .byte $01                    ; $4FB8 (31 bytes)
    :4 .byte $00                    ; $4FD7 (4 zero bytes)
    .byte $98,$5B,$66,$88,$12,$5E,$1E,$2A,$69,$CD,$4B,$DF,$83,$33,$EE,$B1 ; $4FDB
    .byte $7B,$4B,$1F,$F8,$D5,$B5,$97,$7C,$63,$4C,$37,$23,$10,$FF,$EF,$E0 ; $4FEB
    .byte $D1,$C4,$B7,$AB,$A0,$95,$8B,$81,$78,$6F,$66,$5E,$57,$4F,$48,$41 ; $4FFB
    .byte $3B,$35,$2F,$29,$23,$1E,$19,$14,$0F,$0A,$06,$02,$FD,$F9,$F5,$F2 ; $500B
    .byte $29,$18,$11,$0D,$0B,$09,$08,$07,$06,$05,$05      ; $501B
    :4 .byte $04                    ; $5026 (4 bytes)
    :5 .byte $03                    ; $502A (5 bytes)
    :10 .byte $02                    ; $502F (10 bytes)
    :31 .byte $01                    ; $5039 (31 bytes)
    .byte $00,$00,$00,$6C,$DA,$C0,$CF,$4C,$8F,$49,$4F,$8A,$EB,$67,$F8,$9A ; $5058
    .byte $49,$02,$C4,$8D,$5C,$30,$08,$E4,$C3,$A5,$89,$70,$58,$42,$2E,$1B ; $5068
    .byte $09,$F9,$E9,$DB,$CD,$C0,$B4,$A8,$9D,$93,$89,$7F,$76,$6E,$65,$5E ; $5078
    .byte $56,$4F,$48,$41,$3B,$35,$2F,$29,$24,$1F,$1A,$15,$10,$0B,$07,$03 ; $5088
    .byte $FE,$FA,$F7                                      ; $5098
    :9 .byte $00                    ; $509B (9 zero bytes)
    :4 .byte $18                    ; $50A4 (4 bytes)
    .byte $00,$18,$00,$00,$66,$66,$66                      ; $50A8
    :5 .byte $00                    ; $50AF (5 zero bytes)
    .byte $10,$10,$FE,$7C,$28,$44,$00,$18                  ; $50B4
    ; DRAW SKY/FLOOR — fill gradient above/below wall for one column
    .byte $3E,$60,$3C,$06,$7C,$18,$00,$00,$66,$6C,$18,$30,$66,$46,$00,$1C ; $50BC
    .byte $36                                              ; $50CC
    ; Compute gradient address from column position
    .byte $1C,$38,$6F,$66,$3B,$00,$00,$18,$18,$18          ; $50CD
    :5 .byte $00                    ; $50D7 (5 zero bytes)
    .byte $0E,$1C,$18,$18,$1C,$0E,$00,$00,$70,$38,$18,$18,$38,$70,$00,$00 ; $50DC
    .byte $66,$3C                                          ; $50EC
    ; BIT $12 — test buffer flag to select $D800 (buf 0) or $D820 (buf 1) offset
    .byte $FF,$3C,$66,$00,$00,$00,$18                      ; $50EE
    ; Call column clear routine at $4DC8
    .byte $18,$7E,$18,$18                                  ; $50F5
    :7 .byte $00                    ; $50F9 (7 zero bytes)
    .byte $18,$18,$30,$00,$00,$00,$7E                      ; $5100
    :9 .byte $00                    ; $5107 (9 zero bytes)
    .byte $18,$18,$00,$00,$06,$0C,$18,$30,$60,$40,$00,$00,$3C,$66,$6E,$76 ; $5110
    .byte $66,$3C,$00,$00,$18,$38,$18,$18,$18,$7E,$00,$00,$3C,$66,$0C,$18 ; $5120
    .byte $30,$7E,$00,$00,$7E,$0C,$18,$0C,$66,$3C,$00,$00,$0C,$1C,$3C,$6C ; $5130
    .byte $7E,$0C,$00,$00,$7E,$60,$7C,$06,$66,$3C,$00,$00,$3C,$60,$7C,$66 ; $5140
    .byte $66,$3C,$00,$00,$7E,$06,$0C,$18,$30,$30,$00,$00,$3C,$66,$3C,$66 ; $5150
    .byte $66,$3C,$00,$00,$3C,$66,$3E,$06,$0C,$38,$00,$00,$00,$18,$18,$00 ; $5160
    .byte $18,$18,$00,$00,$E2,$96,$92,$E2,$92,$97,$00,$00,$E6,$91,$91,$E2 ; $5170
    .byte $94,$97,$00,$00,$E6,$91,$96,$E1,$91,$96,$00,$00,$E4,$95,$95,$E7 ; $5180
    .byte $91,$91,$00,$00,$E7,$94,$96,$E1,$91,$96,$00,$38,$44,$44,$38,$10 ; $5190
    .byte $1C,$10,$1C,$00,$18,$3C,$66,$66,$7E,$66,$00,$00,$7C,$66,$7C,$66 ; $51A0
    .byte $66,$7C,$00,$00,$3C,$66,$60,$60,$66,$3C,$00,$00,$78,$6C,$66,$66 ; $51B0
    .byte $6C,$78,$00,$00,$7E,$60,$7C,$60,$60,$7E,$00,$00,$7E,$60,$7C,$60 ; $51C0
    .byte $60,$60,$00,$00,$3E,$60,$60,$6E,$66,$3E,$00,$00,$66,$66,$7E,$66 ; $51D0
    .byte $66,$66,$00,$00,$7E                              ; $51E0
    :4 .byte $18                    ; $51E5 (4 bytes)
    .byte $7E,$00,$00                                      ; $51E9
    :4 .byte $06                    ; $51EC (4 bytes)
    .byte $66,$3C,$00,$00,$66,$6C,$78,$78,$6C,$66,$00,$00  ; $51F0
    :4 .byte $60                    ; $51FC (4 bytes)
    ; DRAW TEXTURE COLUMN — renders one vertical strip of textured wall
    .byte $60,$7E,$00,$00                                  ; $5200
    ; Compare wall heights to determine visible extent
    .byte $63,$77,$7F,$6B,$63,$63,$00,$00,$66,$76,$7E,$7E,$6E,$66,$00,$00 ; $5204
    .byte $3C                                              ; $5214
    ; SELF-MODIFYING: patch JSR target at $5242 with texture routine address
    :4 .byte $66                    ; $5215 (4 bytes)
    .byte $3C,$00,$00,$7C,$66,$66,$7C,$60,$60,$00,$00,$3C,$66,$66,$66,$6C ; $5219
    .byte $36,$00,$00,$7C,$66,$66,$7C,$6C                  ; $5229
    ; Write RTS ($60) to texture entry, call it, then write STA ($99) — trick for indirect texture dispatch
    .byte $66,$00,$00,$3C,$60,$3C,$06,$06,$3C,$00,$00,$7E  ; $5231
    :5 .byte $18                    ; $523D (5 bytes)
    ; SELF-MODIFIED JSR — jumps to per-texture column drawing routine
    .byte $00,$00                                          ; $5242
    :5 .byte $66                    ; $5244 (5 bytes)
    .byte $7E,$00,$00                                      ; $5249
    :4 .byte $66                    ; $524C (4 bytes)
    .byte $3C,$18,$00,$00,$63,$63,$6B,$7F,$77,$63,$00,$00,$66,$66,$3C,$3C ; $5250
    .byte $66,$66,$00,$00,$66,$66,$3C,$18,$18,$18,$00,$00,$7E,$0C,$18,$30 ; $5260
    .byte $60,$7E,$00,$00,$1E                              ; $5270
    :4 .byte $18                    ; $5275 (4 bytes)
    .byte $1E,$00,$00,$00,$10,$70,$70,$70,$10,$00,$00,$78  ; $5279
    :4 .byte $18                    ; $5285 (4 bytes)
    .byte $78,$00,$00,$02,$14,$70,$73,$70,$14,$02          ; $5289
    :6 .byte $00                    ; $5293 (6 zero bytes)
    .byte $FF,$00                                          ; $5299
    :23 .byte $1C                    ; $529B (23 bytes)
    .byte $1D,$1C,$1C,$1C                                  ; $52B2
    :5 .byte $1D                    ; $52B6 (5 bytes)
    .byte $1C,$00,$01,$0B,$1C                              ; $52BB
    :6 .byte $00                    ; $52C0 (6 zero bytes)
    .byte $1C,$1C                                          ; $52C6
    :6 .byte $00                    ; $52C8 (6 zero bytes)
    .byte $1C,$1C,$00,$00,$1D,$00,$00,$00,$1C,$03,$00,$03,$1D,$1C,$00,$1C ; $52CE
    .byte $1C,$1C,$00,$1C,$29,$1C,$00,$1C,$1C,$1C,$00,$19,$19,$19,$00,$1C ; $52DE
    .byte $1C,$1C,$00,$00,$00,$27,$00,$00,$00,$08,$03,$1C,$1D,$1C,$00,$1C ; $52EE
    .byte $1C,$1C,$00,$1C,$03,$1C,$00,$1C,$1C,$1C,$00,$19,$00,$19,$00,$1C ; $52FE
    .byte $1C,$1C,$00,$00,$1D,$00,$00,$00,$1C,$03,$00,$03,$1D,$1C ; $530E
    :5 .byte $00                    ; $531C (5 zero bytes)
    .byte $1C,$00,$1C                                      ; $5321
    :5 .byte $00                    ; $5324 (5 zero bytes)
    .byte $19,$19,$19                                      ; $5329
    :6 .byte $00                    ; $532C (6 zero bytes)
    .byte $1D,$00,$00,$00,$1C                              ; $5332
    :4 .byte $1D                    ; $5337 (4 bytes)
    .byte $1C                                              ; $533B
    :4 .byte $15                    ; $533C (4 bytes)
    .byte $00,$1C,$03,$1C,$00,$15,$15,$15                  ; $5340
    :5 .byte $00                    ; $5348 (5 zero bytes)
    .byte $15,$15,$15,$00,$00,$1D,$00,$00,$00,$1C          ; $534D
    :4 .byte $1D                    ; $5357 (4 bytes)
    .byte $1C,$00,$00,$00,$15,$00,$1C,$1C,$1C,$00,$15,$00,$15,$00,$1C,$1C ; $535B
    .byte $1C,$00,$15,$00,$15,$12,$12,$1D,$00,$00,$00,$1C,$00,$00,$1C,$1C ; $536B
    .byte $1C                                              ; $537B
    :4 .byte $15                    ; $537C (4 bytes)
    :5 .byte $00                    ; $5380 (5 zero bytes)
    .byte $15,$15,$15,$00,$1C,$03,$1C,$00,$15,$15,$15,$00,$00,$1D ; $5385
    :4 .byte $00                    ; $5393 (4 zero bytes)
    .byte $26,$00,$00,$1C,$1C                              ; $5397
    :5 .byte $00                    ; $539C (5 zero bytes)
    .byte $19,$19,$19                                      ; $53A1
    :5 .byte $00                    ; $53A4 (5 zero bytes)
    .byte $1C,$00,$1C                                      ; $53A9
    :6 .byte $00                    ; $53AC (6 zero bytes)
    .byte $1D,$00,$00,$00,$1C,$00,$00,$1C,$1C,$1C,$00,$1C,$1C,$1C,$00,$19 ; $53B2
    .byte $00,$19,$00,$1C,$1C,$1C,$00,$1C,$03,$1C,$00,$1C,$1C,$1C,$00,$00 ; $53C2
    :9 .byte $1D                    ; $53D2 (9 bytes)
    .byte $1C,$00,$1C,$1C,$1C,$00,$19,$19,$19,$00,$1C,$1C,$1C,$00,$1C,$29 ; $53DB
    .byte $1C,$00,$1C,$1C,$1C                              ; $53EB
    :10 .byte $00                    ; $53F0 (10 zero bytes)
    .byte $1C,$1C,$00,$00,$1C,$1C                          ; $53FA
    :6 .byte $00                    ; $5400 (6 zero bytes)
    .byte $1C,$1C                                          ; $5406
    :6 .byte $00                    ; $5408 (6 zero bytes)
    .byte $1C,$1C                                          ; $540E
    :10 .byte $00                    ; $5410 (10 zero bytes)
    .byte $1C,$1C,$1C,$00                                  ; $541A
    :26 .byte $1C                    ; $541E (26 bytes)
    .byte $26                                              ; $5438
    :4 .byte $1C                    ; $5439 (4 bytes)
    .byte $27,$1C,$1C,$15,$0B,$15,$0B,$15,$0B,$15,$0B,$15  ; $543D
    :7 .byte $1C                    ; $5449 (7 bytes)
    .byte $0E,$1C,$1C,$1C,$0E,$1C,$1C,$1C,$00,$1C,$1C,$1E,$03,$00,$03,$1E ; $5450
    .byte $15,$04,$00,$03,$15,$03,$15,$03,$15,$15,$00,$00,$00,$1C,$1C,$00 ; $5460
    .byte $00,$00,$1C,$00,$00,$00,$1C,$00,$00,$00,$1C,$1E,$00,$03,$00,$1E ; $5470
    .byte $0B                                              ; $5480
    :7 .byte $00                    ; $5481 (7 zero bytes)
    .byte $26                                              ; $5488
    :5 .byte $00                    ; $5489 (5 zero bytes)
    .byte $1C                                              ; $548E
    :7 .byte $00                    ; $548F (7 zero bytes)
    .byte $1C,$00,$00,$00,$1D,$1E,$03,$00,$03,$1E,$15,$01,$00,$03,$15,$03 ; $5496
    .byte $15,$03,$15,$15,$00,$00,$00,$1C,$1C,$00,$00,$00,$1C,$00,$00,$00 ; $54A6
    .byte $1C,$00,$00,$00,$1C,$1C,$1C,$01,$1C,$1C,$1C,$0B,$15,$0B,$15,$0B ; $54B6
    .byte $15,$0B,$15,$1C,$1C,$00,$00,$1C,$1C,$1C,$00,$1C,$1C,$1C,$01,$1C ; $54C6
    .byte $1C,$00,$1E,$00,$1D,$1C,$00,$2A,$1C,$00,$04,$0B  ; $54D6
    :6 .byte $0E                    ; $54E2 (6 bytes)
    .byte $0B,$0B,$0B,$1C,$00,$1C,$1C,$1C,$26,$1C,$1C,$1C,$0B,$1C,$1C,$00 ; $54E8
    .byte $00,$00,$1C,$1C,$29,$1C,$1C,$29,$1C,$1C,$0E,$0E,$00,$00,$01,$0B ; $54F8
    .byte $0B,$04,$0B,$1C                                  ; $5508
    :8 .byte $00                    ; $550C (8 zero bytes)
    .byte $1C,$1C                                          ; $5514
    :4 .byte $00                    ; $5516 (4 zero bytes)
    .byte $1D,$1C,$00,$29,$00,$00,$1C,$1C,$0E,$00,$00,$00,$0E,$0E,$0B,$00 ; $551A
    .byte $08,$00,$00,$00,$1D                              ; $552A
    :6 .byte $00                    ; $552F (6 zero bytes)
    .byte $27                                              ; $5535
    :4 .byte $00                    ; $5536 (4 zero bytes)
    .byte $1C,$1C,$00                                      ; $553A
    :5 .byte $1C                    ; $553D (5 bytes)
    .byte $00,$00,$00,$0E,$0E,$0E,$0B,$04,$0B,$1C          ; $5542
    :8 .byte $00                    ; $554C (8 zero bytes)
    .byte $1C,$1C                                          ; $5554
    :4 .byte $00                    ; $5556 (4 zero bytes)
    .byte $1D                                              ; $555A
    :7 .byte $1C                    ; $555B (7 bytes)
    .byte $0E,$00,$00,$00,$0E,$0E,$0B,$0B,$0B,$1C,$00,$1C,$1C,$1C,$0E ; $5562
    :6 .byte $1C                    ; $5571 (6 bytes)
    .byte $00,$00,$00,$1C,$1C,$1C,$0B,$1C,$0B,$1C,$1C,$0E,$0E,$00,$00,$00 ; $5577
    .byte $0E,$0E,$1C,$1C,$00,$00,$1C,$1C,$1C,$0B,$1F,$00,$00,$00,$1C,$1C ; $5587
    .byte $00,$1E,$00,$1D,$15,$00,$03,$00,$03,$00,$1C,$0E,$0E,$0E,$00,$00 ; $5597
    .byte $00,$0E,$0E,$00,$00,$00,$1C,$1C,$1F,$01,$1F,$1C,$00,$00,$1C,$1C ; $55A7
    .byte $00,$00,$00,$1C,$15                              ; $55B7
    :5 .byte $00                    ; $55BC (5 zero bytes)
    .byte $1C,$1C,$1C                                      ; $55C1
    :4 .byte $00                    ; $55C4 (4 zero bytes)
    .byte $26                                              ; $55C8
    :5 .byte $00                    ; $55C9 (5 zero bytes)
    .byte $1C,$1F,$00,$2C,$00,$00,$00,$27                  ; $55CE
    :4 .byte $00                    ; $55D6 (4 zero bytes)
    .byte $1D,$15,$00,$00,$1E,$00,$00                      ; $55DA
    :4 .byte $0E                    ; $55E1 (4 bytes)
    .byte $00,$00,$00,$0E,$0E,$00,$00,$00,$1C,$1C,$1F,$00,$1F,$1C,$00,$00 ; $55E5
    .byte $1C,$1C,$00,$00,$00,$1C,$0B,$03                  ; $55F5
    :4 .byte $00                    ; $55FD (4 zero bytes)
    .byte $1C,$0E,$0E,$00,$00,$00,$0E,$0E                  ; $5601
    :6 .byte $1C                    ; $5609 (6 bytes)
    .byte $1F,$00,$1F,$00,$00,$00,$1C,$1C,$1C,$00,$1C,$1C,$15 ; $560F
    :5 .byte $00                    ; $561C (5 zero bytes)
    .byte $1C,$0E,$00,$00,$00,$0E,$1C,$1C,$0D              ; $5621
    :5 .byte $1C                    ; $562A (5 bytes)
    .byte $1F,$00,$1F                                      ; $562F
    :4 .byte $1C                    ; $5632 (4 bytes)
    .byte $0B,$0B,$26,$0B,$0B,$0B,$03,$00,$1E,$00,$00,$27,$00,$00,$00,$0E ; $5636
    .byte $0E,$1C,$00,$00,$00,$15,$00,$00,$1C,$1F,$00      ; $5646
    :5 .byte $1F                    ; $5651 (5 bytes)
    .byte $0B,$03,$00,$03,$0B,$15                          ; $5656
    :5 .byte $00                    ; $565C (5 zero bytes)
    .byte $1C,$0E,$00,$00,$00,$0E,$1C,$00,$00,$00,$15,$00,$08,$1C,$1F,$03 ; $5661
    .byte $00,$03,$00,$03,$1F,$0B,$00,$0C,$00,$0B,$0B,$03  ; $5671
    :4 .byte $00                    ; $567D (4 zero bytes)
    .byte $1C,$0E,$0E,$00,$00,$00,$27,$00,$00,$00,$15,$15,$00,$1C,$1F,$00 ; $5681
    .byte $03,$00,$03,$00,$1F,$0B,$03,$00,$03,$0B,$15,$00,$00,$1E,$00,$00 ; $5691
    :6 .byte $0E                    ; $56A1 (6 bytes)
    .byte $1C                                              ; $56A7
    :6 .byte $00                    ; $56A8 (6 zero bytes)
    .byte $1C                                              ; $56AE
    :5 .byte $1F                    ; $56AF (5 bytes)
    .byte $2C,$1F                                          ; $56B4
    :5 .byte $0B                    ; $56B6 (5 bytes)
    .byte $15                                              ; $56BB
    :5 .byte $00                    ; $56BC (5 zero bytes)
    :7 .byte $1C                    ; $56C1 (7 bytes)
    :6 .byte $00                    ; $56C8 (6 zero bytes)
    .byte $1C,$0C,$0C,$1C,$1C,$1C,$00                      ; $56CE
    :6 .byte $1C                    ; $56D5 (6 bytes)
    .byte $15,$00,$03,$00,$03,$00,$00,$00,$08,$00,$00,$00,$1C ; $56DB
    :6 .byte $00                    ; $56E8 (6 zero bytes)
    .byte $1C,$2F,$05,$00,$00,$1C                          ; $56EE
    :6 .byte $00                    ; $56F4 (6 zero bytes)
    .byte $1C,$1C,$1C,$0B,$1C,$0B,$1C,$00,$1C,$1C,$1C,$00,$04,$1C,$1C,$0D ; $56FA
    .byte $1C,$1C,$0D,$1C,$1C,$0C,$0C,$1C,$26,$1C          ; $570A
    :6 .byte $00                    ; $5714 (6 zero bytes)
    :7 .byte $1C                    ; $571A (7 bytes)
    .byte $26                                              ; $5721
    :4 .byte $1C                    ; $5722 (4 bytes)
    .byte $0E,$1C,$1C,$0E,$1C,$1C,$0E,$1C,$1C,$0E,$1C,$1C,$00,$1C ; $5726
    :4 .byte $1E                    ; $5734 (4 bytes)
    .byte $00,$00,$1C,$1C                                  ; $5738
    :7 .byte $00                    ; $573C (7 zero bytes)
    .byte $1C,$1C                                          ; $5743
    :14 .byte $00                    ; $5745 (14 zero bytes)
    .byte $1C                                              ; $5753
    :4 .byte $1E                    ; $5754 (4 bytes)
    .byte $00,$00,$1C,$1C,$00                              ; $5758
    :6 .byte $17                    ; $575D (6 bytes)
    .byte $1C,$1C,$00,$1E,$1E,$00,$1C,$00,$1E,$00,$1C,$00,$1E,$00,$1C,$1C ; $5763
    .byte $1C                                              ; $5773
    :6 .byte $00                    ; $5774 (6 zero bytes)
    .byte $1C,$1C,$00,$19,$00,$00,$00,$01,$17,$00,$00,$00,$1E,$1E,$31,$1C ; $577A
    .byte $31,$1E,$31,$1C,$31,$1E,$31,$1C,$1C,$1C          ; $578A
    :6 .byte $00                    ; $5794 (6 zero bytes)
    .byte $1C,$1C,$00,$00,$26,$00,$00,$15,$17,$00,$1C,$1C  ; $579A
    :12 .byte $00                    ; $57A6 (12 zero bytes)
    .byte $1C,$1C,$00,$00                                  ; $57B2
    :4 .byte $1E                    ; $57B6 (4 bytes)
    .byte $1C,$1C,$00,$19,$00,$00,$00,$01,$17,$26,$1C,$1C  ; $57BA
    :4 .byte $0C                    ; $57C6 (4 bytes)
    .byte $00,$0C,$0C,$0C,$00,$0C,$0C,$0C,$1C,$1C,$00,$00  ; $57CA
    :4 .byte $1E                    ; $57D6 (4 bytes)
    .byte $1C,$1C,$00                                      ; $57DA
    :6 .byte $17                    ; $57DD (6 bytes)
    .byte $00,$1C,$1C,$0C,$0C                              ; $57E3
    :5 .byte $00                    ; $57E8 (5 zero bytes)
    .byte $0C,$00,$0C,$0C,$00,$1C,$1C,$00,$03,$00,$03,$00,$01,$0B,$1C ; $57ED
    :8 .byte $00                    ; $57FC (8 zero bytes)
    .byte $1C,$1C,$00,$00,$00,$0B,$0B,$0B                  ; $5804
    :6 .byte $00                    ; $580C (6 zero bytes)
    .byte $1C,$1C,$03,$00,$03,$00,$03,$00,$0B              ; $5812
    :11 .byte $1C                    ; $581B (11 bytes)
    :12 .byte $0B                    ; $5826 (12 bytes)
    :9 .byte $1C                    ; $5832 (9 bytes)
    .byte $00,$20,$40,$60,$80,$A0,$C0,$E0,$00,$20,$40,$60,$80,$A0,$C0,$E0 ; $583B
    .byte $00,$20,$40,$60,$80,$A0,$C0,$E0,$00,$20,$40,$60,$80,$A0,$C0,$E0 ; $584B
    .byte $00,$20,$40,$60,$80,$A0,$C0,$E0,$00,$20,$40,$60,$80 ; $585B
    :8 .byte $FA                    ; $5868 (8 bytes)
    :8 .byte $FB                    ; $5870 (8 bytes)
    :8 .byte $FC                    ; $5878 (8 bytes)
    :8 .byte $FD                    ; $5880 (8 bytes)
    :8 .byte $FE                    ; $5888 (8 bytes)
    :5 .byte $FF                    ; $5890 (5 bytes)