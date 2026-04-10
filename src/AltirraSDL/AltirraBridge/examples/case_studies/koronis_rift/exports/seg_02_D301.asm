; =============================================================
; Segment 2: $D301-$D301 — PORTB = $FF - main memory, ROM enabled
; =============================================================
; XEX load range: $D301-$D301 (1 bytes)
; WARNING: This segment is overwritten by segment 12

    org $D301

    .byte $FF                                              ; $D301