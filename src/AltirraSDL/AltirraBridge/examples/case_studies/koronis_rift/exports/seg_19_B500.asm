; =============================================================
; Segment 19: $B500-$B503 — Decomp params for seg 20 data
; =============================================================
; XEX load range: $B500-$B503 (4 bytes)
; WARNING: This segment is overwritten by segment 22

    org $B500

    .byte $00,$7B,$BD,$7B                                  ; $B500