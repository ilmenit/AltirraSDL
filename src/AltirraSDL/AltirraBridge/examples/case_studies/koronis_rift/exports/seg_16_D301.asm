; =============================================================
; Segment 16: $D301-$D301 — PORTB = $EF - switch to bank 3 (130XE)
; =============================================================
; XEX load range: $D301-$D301 (1 bytes)
; WARNING: This segment is overwritten by segment 21

    org $D301

    .byte $EF                                              ; $D301