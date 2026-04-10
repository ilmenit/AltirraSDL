; =============================================================
; Segment 1: $3800-$3BFF — Custom character set (128 chars)
; =============================================================
; XEX load range: $3800-$3BFF (1024 bytes)

    org $3800

dat_3800:
    ; CUSTOM CHARSET — 128 characters (1024 bytes), used for cockpit instruments and HUD text
    brk
    :7 .byte $00                    ; $3801 (7 zero bytes)
    :8 .byte $55                    ; $3808 (8 bytes)
    .byte $5D,$5F,$5D,$75,$75,$7D,$7F,$DD                  ; $3810
    .byte $6A,$6B,$AB,$AB,$6B,$5A,$56,$55                  ; $3818
    .byte $E9,$A9,$A9                                      ; $3820
    :4 .byte $A5                    ; $3823 (4 bytes)
    .byte $95,$6B,$6E,$AE,$AE,$6B,$5A,$56                  ; $3827
    .byte $55,$E9,$A9,$A9,$A5,$A5,$E5,$A5                  ; $382F
    .byte $95,$6A,$6E,$AE,$AE,$6B,$5A,$56                  ; $3837
    .byte $55,$E9,$E9,$E9,$E5,$A5,$A5,$A5                  ; $383F
    .byte $95,$6F,$6E,$AE,$AE,$6B,$5A,$56                  ; $3847
    .byte $55,$F9,$B9,$E9,$E5,$A5,$A5,$A5                  ; $384F
    .byte $95,$B9,$B9,$E9,$E5,$A5,$A5,$A5                  ; $3857
    .byte $95,$F9,$B9,$E9,$E5,$A5,$A5,$A5                  ; $385F
    .byte $95,$6A,$6B,$AE,$AE,$6A,$5A,$56                  ; $3867
    .byte $55,$E9,$A9,$A9                                  ; $386F
    :4 .byte $A5                    ; $3873 (4 bytes)
    .byte $95,$6A,$6A,$AA,$AA,$6B,$5A,$56                  ; $3877
    .byte $55,$E9,$F9,$E9,$E5,$A5,$A5,$A5                  ; $387F
    .byte $95,$D5,$D5,$75,$5D,$57,$55,$55                  ; $3887
    .byte $55,$5D,$5F,$5D,$75,$75,$7D,$77                  ; $388F
    .byte $D5,$D5,$D5                                      ; $3897
    :6 .byte $55                    ; $389A (6 bytes)
    .byte $02,$02                                          ; $38A0
    :6 .byte $00                    ; $38A2 (6 zero bytes)
    .byte $80,$80                                          ; $38A8
    :6 .byte $00                    ; $38AA (6 zero bytes)
    :4 .byte $AA                    ; $38B0 (4 bytes)
    .byte $A8,$A0,$A0,$80,$03,$00,$00,$03                  ; $38B4
    .byte $00,$00,$C3,$FF,$C0,$00,$00,$C0                  ; $38BC
    .byte $00,$00,$C3,$FF,$CF,$03,$00,$00                  ; $38C4
    .byte $03,$00,$00,$03,$F3,$C0,$00,$00                  ; $38CC
    .byte $C0,$00,$00,$C0,$AA,$AA,$A0,$00                  ; $38D4
    .byte $0C,$00,$00,$00,$AA,$AA,$0A,$00                  ; $38DC
    .byte $30,$00,$00,$00,$33,$00,$0C,$00                  ; $38E4
    .byte $30,$0A,$AA,$AA,$CC,$00,$30,$00                  ; $38EC
    .byte $0C,$A0,$AA,$AA,$03,$00,$0C,$00                  ; $38F4
    .byte $30,$02,$C2,$02,$C0,$00,$30,$00                  ; $38FC
loc_3904:
    .byte $0C,$80,$83,$80,$AA,$AA                          ; $3904
    :4 .byte $A9                    ; $390A (4 bytes)
    :4 .byte $A5                    ; $390E (4 bytes)
    :4 .byte $95                    ; $3912 (4 bytes)
    .byte $55,$55,$AA,$AA                                  ; $3916
    :4 .byte $6A                    ; $391A (4 bytes)
    :4 .byte $5A                    ; $391E (4 bytes)
    :4 .byte $56                    ; $3922 (4 bytes)
    .byte $55,$55,$80                                      ; $3926
    :13 .byte $00                    ; $3929 (13 zero bytes)
    .byte $80,$A8                                          ; $3936
    :7 .byte $AA                    ; $3938 (7 bytes)
    .byte $A8                                              ; $393F
    :7 .byte $AA                    ; $3940 (7 bytes)
    .byte $00,$AA,$96                                      ; $3947
    :4 .byte $7D                    ; $394A (4 bytes)
    .byte $96,$AA                                          ; $394E
    :14 .byte $2A                    ; $3950 (14 bytes)
    .byte $AA,$AA,$AA                                      ; $395E
    :7 .byte $2A                    ; $3961 (7 bytes)
    :7 .byte $00                    ; $3968 (7 zero bytes)
    .byte $0A,$00,$00                                      ; $396F
    :4 .byte $C0                    ; $3972 (4 bytes)
    .byte $00,$00,$00                                      ; $3976
    :4 .byte $C0                    ; $3979 (4 bytes)
    .byte $00,$00,$0F                                      ; $397D
    :7 .byte $00                    ; $3980 (7 zero bytes)
    .byte $FC                                              ; $3987
    :7 .byte $00                    ; $3988 (7 zero bytes)
    .byte $C0,$55,$54,$54,$50,$50,$40,$40                  ; $398F
    .byte $00,$55,$15,$15,$05,$05,$01,$01                  ; $3997
    .byte $00,$75,$FD,$FD,$75                              ; $399F
    :8 .byte $55                    ; $39A4 (8 bytes)
    .byte $57,$5F,$5F,$57                                  ; $39AC
    :5 .byte $55                    ; $39B0 (5 bytes)
    .byte $D5,$D5,$55,$57,$5F,$5F,$57                      ; $39B5
    :5 .byte $55                    ; $39BC (5 bytes)
    .byte $D5,$D5                                          ; $39C1
    :5 .byte $55                    ; $39C3 (5 bytes)
    .byte $D7,$F7,$F7,$FF,$DF,$DF,$D7,$55                  ; $39C8
    :6 .byte $57                    ; $39D0 (6 bytes)
    .byte $D7,$D5,$5D,$DD,$DD,$FD,$7D,$7D                  ; $39D6
    .byte $5D,$55,$5D,$5F,$5F,$5F,$5D,$5D                  ; $39DE
    .byte $5D,$55,$75,$75,$75,$F5,$F5,$F5                  ; $39E6
    .byte $75,$55,$75,$7D,$7D,$7F,$77,$77                  ; $39EE
    .byte $75,$55                                          ; $39F6
    :6 .byte $D5                    ; $39F8 (6 bytes)
    .byte $D7,$57                                          ; $39FE
    :8 .byte $A0                    ; $3A00 (8 bytes)
    .byte $0F,$0A,$0A,$0A,$0E,$0A,$0A,$0A                  ; $3A08
    .byte $6E,$6E,$AE,$AE,$6B,$5A,$56,$55                  ; $3A10
    .byte $56,$56                                          ; $3A18
    :4 .byte $5A                    ; $3A1A (4 bytes)
    .byte $6A,$6A,$55,$95,$A5,$B9,$BA,$EA                  ; $3A1E
    .byte $EA,$E9,$56,$56,$5A,$5A,$5B,$5A                  ; $3A26
    .byte $6A,$6A,$55,$95,$A5,$E9,$BA,$BA                  ; $3A2E
    .byte $BA,$B9,$56,$56,$5A,$5B,$5B,$5B                  ; $3A36
    .byte $6E,$6B,$55,$95,$A5,$E9,$BA,$AE                  ; $3A3E
    .byte $AA,$A9,$56,$56,$5A,$5A,$5B,$5A                  ; $3A46
    .byte $6A,$6B,$55,$95,$A5,$A9,$FA,$AE                  ; $3A4E
    .byte $AA,$A9,$56,$56,$5A,$5A,$5A,$5B                  ; $3A56
    .byte $6B,$6E,$56,$56,$5A,$5A,$5B,$5B                  ; $3A5E
    .byte $6B,$6B,$56,$56                                  ; $3A66
    :4 .byte $5A                    ; $3A6A (4 bytes)
    .byte $6A,$6A,$55,$95,$A5,$E9,$BA,$AE                  ; $3A6E
    .byte $AE,$B9,$56,$56                                  ; $3A76
    :4 .byte $5B                    ; $3A7A (4 bytes)
    .byte $6E,$6B,$55,$95,$A5,$A9,$BA,$BA                  ; $3A7E
    .byte $BA,$B9,$56,$56,$5A,$5A,$5B,$5B                  ; $3A86
    .byte $6B,$6E,$DD,$DD,$5D,$5D                          ; $3A8E
    :6 .byte $55                    ; $3A94 (6 bytes)
    .byte $D5,$75                                          ; $3A9A
    :6 .byte $55                    ; $3A9C (6 bytes)
    :4 .byte $D5                    ; $3AA2 (4 bytes)
    .byte $55,$55,$AA,$AA,$A0,$00,$00,$03                  ; $3AA6
    .byte $00,$00,$AA,$AA,$0A,$00,$00,$C0                  ; $3AAE
    .byte $00,$00                                          ; $3AB6
    :4 .byte $AA                    ; $3AB8 (4 bytes)
    .byte $2A,$0A,$0A,$02,$AA,$AE,$BB,$BF                  ; $3ABC
    .byte $BB,$BB,$AA,$AA,$AA,$BF,$BA,$BE                  ; $3AC4
    .byte $BA,$BF,$AA,$AA,$0A,$0A,$2A                      ; $3ACC
    :5 .byte $AA                    ; $3AD3 (5 bytes)
    .byte $00,$00,$C0,$00,$00,$0A,$AA,$AA                  ; $3AD8
    .byte $00,$00,$03,$00,$00,$A0,$AA,$AA                  ; $3AE0
    .byte $A0,$A0,$A8                                      ; $3AE8
    :5 .byte $AA                    ; $3AEB (5 bytes)
    :5 .byte $00                    ; $3AF0 (5 zero bytes)
    .byte $02,$02,$02                                      ; $3AF5
    :5 .byte $00                    ; $3AF8 (5 zero bytes)
    .byte $80,$80,$80                                      ; $3AFD
    :4 .byte $D7                    ; $3B00 (4 bytes)
    .byte $FF,$FF,$D7,$55                                  ; $3B04
    :4 .byte $5D                    ; $3B08 (4 bytes)
    .byte $FD,$FD,$5D,$55                                  ; $3B0C
    :4 .byte $5D                    ; $3B10 (4 bytes)
    .byte $5F,$5F,$5D,$55                                  ; $3B14
    :4 .byte $75                    ; $3B18 (4 bytes)
    .byte $F5,$F5,$75,$55                                  ; $3B1C
    :4 .byte $75                    ; $3B20 (4 bytes)
    .byte $7F,$7F,$75,$55,$AA,$BB,$BB,$BF                  ; $3B24
    .byte $BB,$BB,$AA,$AA,$7D,$D7,$D5,$7D                  ; $3B2C
    .byte $57,$D7,$7D,$55,$55,$57,$57,$55                  ; $3B34
    .byte $55,$57,$D5,$D5,$F5,$5D,$55,$F5                  ; $3B3C
    .byte $5D,$5D,$F5,$55,$57,$5D,$5D,$57                  ; $3B44
    .byte $55,$5D,$57,$55,$D5,$75,$55,$D5                  ; $3B4C
    .byte $75,$75,$D5,$55,$5F,$75,$75,$5F                  ; $3B54
    .byte $55,$75,$5F,$55,$55,$D5,$55,$55                  ; $3B5C
    .byte $D5,$D5,$57,$57,$FF,$D5,$D5,$FD                  ; $3B64
    .byte $D5,$D5,$FF,$55,$FD,$55,$55,$F5                  ; $3B6C
    .byte $55,$55,$FD,$55,$5F,$5D,$5D,$5F                  ; $3B74
    .byte $5D,$5D,$5F,$55,$F5,$55,$55,$D5                  ; $3B7C
    .byte $55,$55,$F5,$55,$7F,$75,$75,$7F                  ; $3B84
    .byte $75,$75,$7F,$55,$D5                              ; $3B8C
    :5 .byte $55                    ; $3B91 (5 bytes)
    .byte $D7,$57                                          ; $3B96
    :6 .byte $55                    ; $3B98 (6 bytes)
    .byte $5D,$5D                                          ; $3B9E
    :5 .byte $55                    ; $3BA0 (5 bytes)
    .byte $7D,$7D,$7D                                      ; $3BA5
    :8 .byte $AA                    ; $3BA8 (8 bytes)
    :6 .byte $55                    ; $3BB0 (6 bytes)
    .byte $75,$75                                          ; $3BB6
    :5 .byte $55                    ; $3BB8 (5 bytes)
    .byte $F5,$F5,$F5                                      ; $3BBD
    :5 .byte $55                    ; $3BC0 (5 bytes)
    .byte $57,$D7,$D7                                      ; $3BC5
    :5 .byte $55                    ; $3BC8 (5 bytes)
    .byte $D5,$D7,$D7                                      ; $3BCD
    :7 .byte $AA                    ; $3BD0 (7 bytes)
    .byte $0A                                              ; $3BD7
    :5 .byte $55                    ; $3BD8 (5 bytes)
    .byte $5F,$5F,$5F,$AA                                  ; $3BDD
    :4 .byte $BA                    ; $3BE1 (4 bytes)
    .byte $BF,$AA,$AA,$AA,$BE,$BB,$BE,$BE                  ; $3BE5
    .byte $BB,$AA,$AA,$33,$00,$00,$00,$30                  ; $3BED
    .byte $00,$00,$00,$CC,$00,$00,$00,$0C                  ; $3BF5
    .byte $00,$00,$00                                      ; $3BFD