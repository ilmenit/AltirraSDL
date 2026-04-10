; =============================================================
; Segment 8: $B500-$B503 — Decomp params for seg 9 data ($0F00-$1AF5)
; =============================================================
; XEX load range: $B500-$B503 (4 bytes)
; WARNING: This segment is overwritten by segment 19

    org $B500

    .byte $00,$0F,$F5,$1A                                  ; $B500