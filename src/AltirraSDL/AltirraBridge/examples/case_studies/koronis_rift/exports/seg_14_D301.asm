; =============================================================
; Segment 14: $D301-$D301 — PORTB = $EB - switch to bank 2 (130XE)
; =============================================================
; XEX load range: $D301-$D301 (1 bytes)
; WARNING: This segment is overwritten by segment 16

    org $D301

    .byte $EB                                              ; $D301