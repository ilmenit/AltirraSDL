; =============================================================
; Segment 0: $3C00-$3C00 — Placeholder byte at start addr
; =============================================================
; XEX load range: $3C00-$3C00 (1 bytes)
; WARNING: This segment is overwritten by segment 2

    org $3C00

    ; GAME ENTRY POINT — Initializes hardware, disables IRQ, sets up VBI/DLI vectors, configures PMBASE, CHBASE, SKCTL. Branches to title screen loop.
    brk