; =============================================================
; Segment 12: $D301-$D301 — PORTB = $E7 - switch to bank 1 (130XE)
; =============================================================
; XEX load range: $D301-$D301 (1 bytes)
; WARNING: This segment is overwritten by segment 14

    org $D301

    .byte $E7                                              ; $D301