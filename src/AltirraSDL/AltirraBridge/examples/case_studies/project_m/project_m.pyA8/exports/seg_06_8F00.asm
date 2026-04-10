; =============================================================
; Segment 6: $8F00-$90FB — 
; =============================================================
; XEX load range: $8F00-$90FB (508 bytes)

    org $8F00

col_distance:
    ; COL DISTANCE ARRAY — per-column wall distance (32 entries), indexed by column
    ; COL DISTANCE ARRAY — per-column wall distance (32 entries), indexed by column
    :32 .byte $00                    ; $8F00 (32 zero bytes)
col_texture:
    ; COL TEXTURE ARRAY — per-column texture ID (32 entries)
    :32 .byte $00                    ; $8F20 (32 zero bytes)
col_texcol:
    ; COL TEXCOL ARRAY — per-column texture column offset (32 entries)
    :32 .byte $00                    ; $8F40 (32 zero bytes)
    ; COL HEIGHT BUF0 — per-column pixel height for buffer 0
    :32 .byte $00                    ; $8F60 (32 zero bytes)
    ; COL SKY BUF0 — per-column sky height for buffer 0
    :32 .byte $00                    ; $8F80 (32 zero bytes)
    ; COL HEIGHT BUF1 — per-column pixel height for buffer 1
    :32 .byte $00                    ; $8FA0 (32 zero bytes)
    ; COL SKY BUF1 — per-column sky height for buffer 1
    :99 .byte $00                    ; $8FC0 (99 zero bytes)
    :4 .byte $01                    ; $9023 (4 bytes)
    .byte $02,$02,$02,$03,$03,$03                          ; $9027
    :5 .byte $04                    ; $902D (5 bytes)
    .byte $80,$8F,$90,$C0,$C0,$22,$88,$99,$11,$33,$88,$99,$00,$22,$88,$00 ; $9032
    .byte $22,$88,$22,$33,$44,$66,$77,$00,$00,$11,$00,$99,$00,$01,$02,$03 ; $9042
    .byte $04,$05,$06,$07,$08,$09,$0A,$0B,$0C,$0D,$0E,$0F,$10,$11,$12,$13 ; $9052
    .byte $07,$08,$09,$0F,$0F,$0F,$03,$05,$00,$01,$02,$01,$02,$07,$08,$09 ; $9062
    .byte $0F,$0F,$0D,$14,$15,$16,$00,$01,$02,$00,$01,$02,$01,$02,$07,$08 ; $9072
    .byte $09,$0F,$0F,$0D,$00,$01,$02,$03,$04,$05,$06,$07,$08,$09,$0A,$0B ; $9082
    .byte $0C,$0D,$0E,$0F,$10,$11,$12,$13,$0A,$0B,$0C,$0E,$10,$11,$04,$06 ; $9092
    .byte $00,$01,$02,$01,$02,$0A,$0B,$0C,$0E,$10,$0D,$14,$15,$16,$00,$01 ; $90A2
    .byte $02,$00,$01,$02,$01,$02,$0A,$0B,$0C,$0E,$10,$0D,$7E,$12,$7E,$07 ; $90B2
    .byte $2E,$8E,$2E,$2F,$99,$88,$00,$11,$00,$00,$00,$BB,$EE,$00,$00,$EE ; $90C2
    .byte $55,$11,$00,$55,$01,$02,$80,$01,$00,$FF,$FF,$00,$15,$13,$03,$08 ; $90D2
    .byte $0F,$0D,$1B,$03,$03,$04,$10,$1C,$2B,$55,$84,$A1,$B6,$D6,$F2 ; $90E2
    :11 .byte $3A                    ; $90F1 (11 bytes)