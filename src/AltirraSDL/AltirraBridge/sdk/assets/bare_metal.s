;  bare_metal.s — tiny 6502 stub that parks the CPU for
;  "emulator-as-display-device" clients of AltirraBridge.
;
;  This source is kept for reference; the canonical copy the
;  bridge server actually ships is an embedded byte array in
;  src/AltirraSDL/source/bridge/bridge_bare_stub.cpp. When you
;  edit this file, re-assemble it (see below), compare against
;  kBareStubXex[] in bridge_bare_stub.cpp, and update both.
;
;  Assemble with MADS 2.1.0+:
;    mads bare_metal.s -o:bare_metal.xex
;
;  Expected output: 30 bytes — XEX magic (2) + segment header (4)
;  + 18 code bytes + RUNAD segment header (4) + RUNAD value (2).
;
;  What it does:
;
;  - SEI               disable IRQs (no more keyboard/pokey/serial
;                       interrupts interfering with our setup)
;  - CLD               clear decimal mode (6502 hygiene; some OS
;                       versions leave it set in interrupt context)
;  - LDA #$FF : STA $D301
;                      PORTB = $FF. Unmaps the BASIC cartridge so
;                      the $A000-$BFFF RAM underneath is visible
;                      and nothing writes there from the cart.
;  - LDA #$00 : STA $D40E
;                      NMIEN = 0. Disables *all* NMI sources — no
;                      VBI (the OS's shadow→ANTIC copy stops), no
;                      DLI (so whatever the client installs as a
;                      display list can't accidentally trigger a
;                      DLI handler we haven't written), no reset-
;                      button NMI.
;  - STA $D400         DMACTL = 0. Turns off ANTIC's playfield,
;                      missile, and player DMA. The screen goes
;                      black immediately. The client re-enables
;                      DMACTL (usually $22 = normal playfield,
;                      single-line resolution) after it has
;                      written a display list and pixel data and
;                      pointed ANTIC at the DL via $D402/$D403.
;  - JMP *             park the CPU forever in a tight 3-cycle
;                      loop. With NMIs off and IRQs masked, the
;                      CPU touches nothing. Memory, ANTIC, GTIA,
;                      PIA, POKEY are all under the client's
;                      control until the next cold reset.

        org $0600

start   sei
        cld
        lda #$ff
        sta $d301       ; disable BASIC cart (PORTB)
        lda #$00
        sta $d40e       ; NMIEN = 0
        sta $d400       ; DMACTL = 0 (ANTIC idle)
park    jmp park        ; park the CPU

        run start
