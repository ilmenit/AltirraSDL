; =============================================================
; Segment 24: $B500-$B503 — Decomp params for final segment
; =============================================================
; XEX load range: $B500-$B503 (4 bytes)

    org $B500

decomp_params:
    .byte $80,$36,$CB,$87                                  ; $B500