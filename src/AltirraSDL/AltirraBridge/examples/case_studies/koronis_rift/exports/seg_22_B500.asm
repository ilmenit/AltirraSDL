; =============================================================
; Segment 22: $B500-$B503 — Decomp params for seg 23 data
; =============================================================
; XEX load range: $B500-$B503 (4 bytes)
; WARNING: This segment is overwritten by segment 24

    org $B500

    .byte $00,$50,$1B,$67                                  ; $B500