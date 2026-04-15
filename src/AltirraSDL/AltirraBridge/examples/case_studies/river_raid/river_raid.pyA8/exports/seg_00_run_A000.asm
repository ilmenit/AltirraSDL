; =============================================================
; Runtime segment: $A000-$BFFF (from XEX $4100-$60FF)
; =============================================================
; Copied by the game's relocator at boot. In reconstructed
; export mode these bytes are emitted at their runtime
; address so labels and comments line up with the code.

    org $A000

cold_start:
    ; cold_start: disable NMI, IRQ, GRACTL, DMACTL; zero all HW regs. Entry point jumped to via ($BFFA) after the relocator at $4086 finishes.
    lda #$00
    sta NMIEN
    sta IRQEN
    sta GRACTL
    sta DMACTL
    tay
    sta PBCTL
loc_A012:
    sta HPOSP0,Y
    sta DMACTL,Y
    sta AUDF1,Y
    sta PORTA,Y
    iny
    bne loc_A012
    ldx #$38
    stx PACTL
    sta PORTA
    ldx #$3C
    stx PACTL
    ldx #$03
    stx SKSTAT
    ldy #$0F
loc_A035:
    sty $A0
    ; JSR $B298 - read console switches, check for cartridge/START at startup.
    jsr read_console
    cmp $0090,Y
    beq loc_A043
    ldy #$00
    beq loc_A048
loc_A043:
    dey
    bpl loc_A035
    ldy #$A0
loc_A048:
    lda #$00
    sta temp_ptr_lo
    sta temp_ptr_hi

clear_ram:
    ; Zero-fill RAM $00 through $3FFF before setting up the display list.
    sta (temp_ptr_lo),Y
    iny
    bne clear_ram
    inc temp_ptr_hi
    ldx temp_ptr_hi
    cpx #$40
    bcc clear_ram
    ldx #$03

setup_display_list:
    ; Copy display list template from $B776 to $3F00.
    lda dl_template_data,X
    sta $3F00,X
    dex
    bpl setup_display_list
    ; Set up display list pointers (DLISTL/DLISTH -> $3F00) after copying the template.
    ldy #$0E
    ldx #$2E

detect_pal_ntsc:
    ; Detect PAL/NTSC from $D014 bit 3; store in pal_flag.
    lda COLPM2
    and #$0E
    sta pal_flag
    bne loc_A077
    ldy #$1D
    ldx #$2F
loc_A077:
    stx scanlines_per_row
    ldx #$0E
loc_A07B:
    lda $B77A,Y
    sta $3FA9,X
    dey
    dex
    bpl loc_A07B
    lda #$0E
    ldx #$A3
loc_A089:
    sta $3F05,X
    dex
    bne loc_A089
    stx $A8
    sta $A7
    lda #$4E
    sta $3F06
    sta $3F57
    stx $3F04
    stx $3F58
    ldx #$30
    stx $3F07
    ldx #$20
    stx $3F05
    stx $3F08
    ldx #$30
    stx $3F59
    ldy #$02
    sty terrain_seed_lo
    ldy #$53
    sty terrain_seed_hi
    lda #$00
    sta DLISTL
    lda #$3F
    sta DLISTH
    lda #$00
    sta $0200
    lda #$B5
    sta $0201
    ldx #$B2
    stx $0223
    ldy #$A5
    sty $0222
    lda #$85
    sta $0216
    lda #$B4
    sta $0217
    lda #$08
    sta PMBASE
    lda #$B9
    sta temp_ptr_hi
    lda #$B7
    sta $03
    ldx #$3B
loc_A0F2:
    lda $BB70,X
    sta $1000,X
    dex
    bpl loc_A0F2
    ldx #$08
loc_A0FD:
    lda $E000,X
    sta $0800,X
    lda $E100,X
    sta $0900,X
    sta $0600,X
    lda $E300,X
    sta $0700,X
    inx
    bne loc_A0FD
    inx
    stx SIZEM
    ldx #$3F
loc_A11B:
    lda $BEEE,X
    sta $06C0,X
    dex
    bpl loc_A11B
    ldx #$6F
loc_A126:
    lda $BF2E,X
    sta $0818,X
    dex
    bpl loc_A126
    ldx #$17
loc_A131:
    lda $BF9E,X
    sta $08D0,X
    dex
    bpl loc_A131
    ldx #$2F
loc_A13C:
    lda $BFB6,X
    sta $07D0,X
    dex
    bpl loc_A13C
    lda #$9B
    sta src_ptr_lo
    lda #$A1
    sta src_ptr_hi
    ldy #$00
    lda #$FF
    ldx $6000
    cpx #$47
    beq loc_A15A
    nop
    nop
loc_A15A:
    cli
    ldx #$09

state_start_game:
    ; State 3: begin new game — clear vars, init lives/score/fuel, enter gameplay.
    lda #$00
loc_A15F:
    sta temp_ptr_lo,X
    inx
    bpl loc_A15F
    ldx #$29
    jsr init_display_region
    jsr prepare_display
    ldy start_pressed
    bne loc_A17E
    inc start_pressed
    lda #$01
    sta lives_count
    lda #$FF
    sta fuel_level
    ldx #$40
    bne loc_A18D
loc_A17E:
    bit invuln_flags
    bmi loc_A18B
    jsr update_entities
    jsr check_terrain_collision
    jsr process_collision_results
loc_A18B:
    ldx #$C0
loc_A18D:
    lda VCOUNT
    cmp #$78
    bne loc_A18D
    stx NMIEN

attract_wait:
    ; Attract mode / title handler: wait for VCOUNT >= $78, poll CONSOL/joystick, fall through to main loop at $AE99.
    lda VCOUNT
    cmp #$50
    bcc attract_wait
    ; Read joystick port A (PORTA $D300) during attract mode.
    ldx PORTA
    inx
    bne loc_A1BF
    ; Reads hardware TRIG0 ($D010) directly for the player fire button. Branches to $A1BF when fire is pressed (TRIG0 = 0). The game bypasses the OS STRIG0 shadow at $0284 — OS VBI is disabled because the game installs its own interrupt handlers.
    lda TRIG0
    beq loc_A1BF
    lda TRIG1
    beq loc_A1BF

read_consol_switches:
    ; Read CONSOL ($D01F): bit0=START, bit1=SELECT, bit2=OPTION. START leaves attract into state 3.
    lda #$08
    sta CONSOL
    lda CONSOL
    lsr
    bcc loc_A1BF
    lsr
    bcc loc_A1BF
    lsr
    bcs loc_A1C5
loc_A1BF:
    ldy #$00
    sty $78
    sty var_0024
loc_A1C5:
    lda SKSTAT
    and #$04
    bne state_dispatch
    sta $78

state_dispatch:
    ; State machine dispatch: LDX game_state ($24); BNE to $A1DC for non-attract states, otherwise fall through into attract handling and JMP $AE99.
    ldx var_0024
    bne loc_A1DC
    tax
    bne loc_A1F5
    lda #$03
    sta var_0024
loc_A1D9:
    ; Dispatch exit: JMP $AE99 - hand control to the main frame loop after per-frame state work.
    jmp main_frame_sync
loc_A1DC:
    ; Non-attract dispatch tail: CPX #$02 etc., routing to $A271 (gameplay), $A3E9 (dying), $A15D (starting).
    cpx #$02
    bcc loc_A1F0
    beq loc_A1E9
    tax
    beq loc_A1D9
    dec var_0024
    bne loc_A1D9
loc_A1E9:
    tax
    bne loc_A1D9
    dec var_0024
    bne loc_A1F5
loc_A1F0:
    tax
    beq loc_A1F5
    dec var_0024
loc_A1F5:
    lda $78
    bmi loc_A1D9
    lda frame_counter
    eor $33
    sta $B1
    lda frame_counter
    and #$0F
    bne loc_A21B
    lda SKRES
    and #$0F
    cmp #$0B
    bcc loc_A210
    sbc #$05
loc_A210:
    tax
    cpx $4E
    beq loc_A21B
    lda $42,X
    ora #$40
    sta $42,X
loc_A21B:
    lda #$0F
    ldx fuel_state
    cpx #$08
    bcs loc_A229
    cpx #$04
    bcc loc_A229
    lda #$03
loc_A229:
    and frame_counter
    bne loc_A26C
    lda collision_result
    bpl loc_A261
    bit invuln_flags
    bmi loc_A239
    lda fuel_level
    bne loc_A261
loc_A239:
    bit sound_flags
    bpl loc_A24F
    lda scroll_position
    cmp #$1A
    bcc loc_A261
    bit collision_result
    bpl loc_A261
    bit $20
    bmi loc_A253
    inc collision_result
    bpl loc_A261
loc_A24F:
    lda bridges_crossed
    bne loc_A261
loc_A253:
    lda fuel_state
    and #$7F
    cmp #$06
    bcs loc_A25F
    inc bullet_x
    bcc loc_A261
loc_A25F:
    dec bullet_x
loc_A261:
    lda #$03
    ldx fuel_state
    bmi loc_A26A
    lda $BAAF,X
loc_A26A:
    sta fuel_state
loc_A26C:
    ldx $4D
    jmp entity_dispatch

entity_update_loop:
    ; State 1 - Entity update loop: iterate 6 entity slots at $0500, move each by type, spawn new ones via $B0C7, check ranges.
    ldy entity_type_tbl,X
    cpy #$0E
    bcs loc_A2ED
    cpy #$0C
    bcs loc_A28F
    lda #$01
    cpy #$07
    bcs loc_A284
    lda #$07
loc_A284:
    and frame_counter
    bne loc_A28F
    lda entity_type_trans,Y
    sta entity_type_tbl,X
    tay
loc_A28F:
    bit invuln_flags
    bmi loc_A297
    lda fuel_level
    bne loc_A2ED
loc_A297:
    lda $60
    lsr
    beq loc_A2ED
    cpy #$07
    beq loc_A2CA
    bcc loc_A2ED
    cpy #$0C
    bne loc_A2AC
    lda frame_counter
    and #$02
    bne loc_A2ED
loc_A2AC:
    txa
    eor $B1
    ror
    bcs loc_A2ED
    lda $42,X
    rol
    bpl loc_A2ED
    ror
    ror
    bcc loc_A2C5
    rol
    bit $B964
    bne loc_A2CA
    eor #$18
    bne loc_A2C8
loc_A2C5:
    rol
    and #$EF
loc_A2C8:
    sta $42,X
loc_A2CA:
    lda $42,X
    and #$08
    bne loc_A2DE
    lda entity_xpos_tbl,X
    clc
    adc #$01
    cmp #$D5
    bcc loc_A2EA
    lda #$2C
    bne loc_A2EA
loc_A2DE:
    lda entity_xpos_tbl,X
    sec
    sbc #$01
    cmp #$2C
    bcs loc_A2EA
    lda #$D4
loc_A2EA:
    sta entity_xpos_tbl,X
loc_A2ED:
    inx

entity_dispatch:
    cpx #$0B
    bcs loc_A2F5
    jmp entity_update_loop
loc_A2F5:
    ldx lives_count
    beq loc_A312
    lda frame_counter
    and #$07
    bne loc_A312
    dex
    bne loc_A310
    dex
    lda invuln_flags
    bmi loc_A310
    ora #$80
    sta invuln_flags
    stx lives_count
    jmp player_death
loc_A310:
    stx lives_count
loc_A312:
    ; Input handling: LDA PORTA, select nibble by player_number ($77) - low nibble for player 1, high nibble LSR'd x4 for player 2 - store direction in $19.
    lda joystick_dir
    asl
    asl
    asl
    asl
    sta $B9
    lda PORTA
    ldx player_number
    beq loc_A325
    lsr
    lsr
    lsr
    lsr
loc_A325:
    ; Player 1 joystick branch: use PORTA low nibble directly.
    and #$0F
    sta joystick_dir
    cmp #$0F
    bne loc_A332
    lda TRIG0,X
    bne loc_A33F
loc_A332:
    lda fuel_level
    sec
    sbc #$2F
    bne loc_A33F
    sta fuel_level
    ldx #$40
    stx $59
loc_A33F:
    ldx fuel_level
    bmi loc_A35C
    bne loc_A348
    jmp check_entity_collision
loc_A348:
    cpx #$30
    bcc loc_A3B9
    bne loc_A358
    ldy #$80
    sty $052C
    ldy $6A
    dey
    sty $6A
loc_A358:
    dec fuel_level
    bne loc_A3B9
loc_A35C:
    inx
    beq loc_A3B9
    txa
    and #$07
    bne loc_A36A
    ldy $5E
    beq loc_A36A
    dec $5E
loc_A36A:
    dec fuel_level
    bmi loc_A3B9
    lda $75
    beq loc_A375
    jsr setup_game_start
loc_A375:
    lda #$01
    sta $79
    lda $6A
    bne loc_A3BC
    lda #$63
    jsr reset_entities
    lda #$6E
    jsr reset_entities
    lda $09
    lsr
    bcs loc_A392
    jsr loc_B06E
    jmp display_update_misc
loc_A392:
    lda player_number
    beq loc_A399
    jsr setup_game_start
loc_A399:
    ldx #$03
loc_A39B:
    lda $BB8F,X
    eor #$C0
    sta $1033,X
    dex
    bpl loc_A39B
    lda #$50
    sta $B1
    ldx #$6C
    ldy #$37
    jsr loc_B064

display_update_misc:
    jsr update_lives_display
    lda #$20
    sta $3FB3
loc_A3B9:
    jmp continue_frame
loc_A3BC:
    ldx #$12
    jsr init_display_region
loc_A3C1:
    lda VCOUNT
    cmp #$50
    bcc loc_A3C1
    lda $66
    sta $AA
    lda $67
    sta $AB
    lda $68
    sta $AC
    lda $69
    sta player_x
    clc
    adc #$26
    adc $54
    sta player_sprite_x
    bit invuln_flags
    bpl continue_frame
    jsr update_lives_display

continue_frame:
    jmp terrain_gen_update

check_entity_collision:
    ; State 2 - player dying / collision processing: run explosion animation, decrement lives, clear entities, return to gameplay or game-over.
    ldx $1D
    bmi loc_A424
    lda entity_type_tbl,X
    cmp #$07
    bcc loc_A424
    cmp #$0E
    bcc loc_A3FC
    bne loc_A40A
    inc bridges_crossed
loc_A3FC:
    ldy #$1F
    lda #$01
    jsr play_score_sound
    ldx $1D
    lda #$03
    jmp process_entity_hit
loc_A40A:
    lda m1_x_shift_src
    adc #$01
    ldx #$04
    bcc loc_A418
    lda #$FF
    sta m1_x_base
    ldx #$03
loc_A418:
    sta m1_x_shift_src
    cpx $7A
    beq loc_A424
    stx $7A
    lda #$08
    sta $7B
loc_A424:
    ldx $1E
    bmi loc_A42E
    ldx collision_result
    cpx #$11
    bcs handle_extra_life
loc_A42E:
    ldx $21
    bpl handle_extra_life
    ldx $1B
    bmi loc_A43F

handle_extra_life:
    ldy #$1F
    lda #$01
loc_A43A:
    jsr play_score_sound
    bne continue_frame
loc_A43F:
    lda m1_x_base
    sec
    sbc #$20
    bcs loc_A453
    ldy m1_x_shift_src
    bne loc_A451
    lda #$02
    ldy #$23
    jmp loc_A43A
loc_A451:
    dec m1_x_shift_src
loc_A453:
    sta m1_x_base
    ldx #$09
    lda joystick_dir
    tay
    and #$0C
    eor #$0C
    bne loc_A466
    sta $2B
    sta $58
    beq loc_A495
loc_A466:
    lda $2B
    clc
    adc #$08
    bcs loc_A46F
    sta $2B
loc_A46F:
    dex
    tya
    and #$08
    beq loc_A483
    dex
    bcs loc_A47F
    lda $58
    sec
    sbc $2B
    bcs loc_A493
loc_A47F:
    dec player_sprite_x
    bne loc_A493
loc_A483:
    bcs loc_A491
    lda $58
    bit $B9
    bpl loc_A48D
    lda #$FF
loc_A48D:
    adc $2B
    bcc loc_A493
loc_A491:
    inc player_sprite_x
loc_A493:
    sta $58
loc_A495:
    stx $5E
    ldx $59
    tya
    lsr
    bcs loc_A4A4
loc_A49D:
    txa
    adc #$02
    bcc loc_A4B4
    bcs loc_A4B6
loc_A4A4:
    lsr
    bcc loc_A4AD
    txa
    asl
    bcc loc_A49D
    beq loc_A4B6
loc_A4AD:
    txa
    cmp #$41
    bcc loc_A4B6
    sbc #$02
loc_A4B4:
    sta $59
loc_A4B6:
    ldx $1C
    bmi loc_A4C1
    ldy entity_type_tbl,X
    cpy #$07
    bcs loc_A4DE
loc_A4C1:
    ldx $1F
    bmi loc_A4DB
    ldy collision_result
    cpy #$11
    bcc loc_A4DB
    ldy entity_type_tbl,X
    cpy #$08
    bcc loc_A4DB
    lda #$02
    sta entity_type_tbl,X
    lda #$17
    sta $7C
loc_A4DB:
    jmp loc_A58D
loc_A4DE:
    lda #$02

process_entity_hit:
    ; Entity hit processing: set destruction animation, compute BCD score from $BB0B[type], schedule sound.
    sta entity_type_tbl,X
    .byte $BC  ; $A4E3
    ; player_sprite_x = player_x ($52) + $26 + river_width_R ($54) - converts river-relative to absolute HPOS, stored at $57.
    .byte $0B  ; $A4E4
    .byte $05  ; $A4E5
    lda #$17
    sta $7C
    cpy #$0E
    bne update_score_bcd
    sta $31
    lda score_100
    sed
    adc #$00
    sta score_100
    lda score_10k
    adc #$00
    cld
    sta score_10k
    inc bridges_crossed
    ; Bullet-vs-entity range check (low slot): compares bullet_x ($39) against entity_xpos. Hit sets fuel_state bit 7 ($38 |= $80) and schedules score/explosion.
    lda entity_xpos_tbl,X
    sec
    sbc #$08
    cmp bullet_x
    bcs loc_A521
    lda entity_xpos_tbl,X
    adc #$1F
    cmp bullet_x
    bcc loc_A521
    lda fuel_state
    ora #$80
    sta fuel_state
    lda #$37
    sta $7C
    ldy #$10
    bne update_score_bcd
loc_A521:
    lda score_10k
    bne loc_A52B
    lda score_100
    cmp #$05
    bcc update_score_bcd
loc_A52B:
    lda fuel_state
    cmp #$06
    bcs loc_A53C
    lda entity_xpos_tbl,X
    sec
    ; Bullet-vs-entity range check (high slot): paired with $A500 for the upper entity slots.
    sbc bullet_x
    cmp #$29
    jmp loc_A543
loc_A53C:
    lda bullet_x
    sbc entity_xpos_tbl,X
    cmp #$41
loc_A543:
    bcs update_score_bcd
    inc collision_result

update_score_bcd:
    ; Score update: SED + ADC BCD addition of the entity's score-table entry at $BB0B into $63/$64/$65.
    sed
    lda score_mid
    and #$F0
    sta $B1
    lda score_value_table,Y
    clc
    adc score_low
    sta score_low
    lda score_mid
    adc #$00
    sta score_mid
    lda score_high
    adc #$00
    sta score_high
    cld
    bcc loc_A574
    lda #$99
    sta score_high
    sta score_mid
    sta score_low
    lda #$00
    sta $6A
    jmp handle_extra_life
loc_A574:
    lda score_mid
    and #$F0
    cmp $B1
    beq loc_A589
    ldy $6A
    cpy #$09
    beq loc_A589
    iny
    sty $6A
    lda #$40
    sta $7E
loc_A589:
    ldx #$01
    bne handle_bullet_fire
loc_A58D:
    lda $1A
    bpl loc_A589
    lda $56
    cmp #$1C
    bcc loc_A59D
    sbc #$06
    tax
    jmp handle_bullet_fire
loc_A59D:
    ldx player_number
    lda TRIG0,X
    bne loc_A589
    ldx #$0F
    stx $7D
    ldx #$A9

handle_bullet_fire:
    ; Bullet interaction path. Saves X, calls helpers at $B221/$B223, stores return into bullet_column ($56). Then iterates on slot index $51, checks entity_type $0A/$0B, and computes a position vs entity_xpos using bullet_x ($39). Not a simple 'fire button' handler — more like 'process active bullet vs current entity slot'.
    txa
    pha
    jsr loc_B221
    pla
    sta $56
    lda #$20
    jsr loc_B223

terrain_gen_update:
    ; Advance terrain PRNG ($A5/$A6), compute new river bank positions for the next row.
    ldy #$00
    ldx $51
    cpx #$0B
    bcs terrain_gen_advance
    lda entity_type_tbl,X
    cmp #$0A
    beq loc_A5CA
    cmp #$0B
    bne terrain_gen_advance
loc_A5CA:
    lda $C1
    bne loc_A5FD
    bit invuln_flags
    bmi loc_A5D6
    lda fuel_level
    bne terrain_gen_advance
loc_A5D6:
    lda $0C,X
    sbc #$3C
    cmp #$1C
    bcc loc_A607
    sta frame_ctr_copy
    lda #$09
    sta $7F
    ldy entity_xpos_tbl,X
    dey
    dey
    dey
    lda $42,X
    ldx #$FE
    and #$08
    bne loc_A5F8
    ldx #$02
    tya
    adc #$09
    tay
loc_A5F8:
    stx $C0
    jmp terrain_gen_advance
loc_A5FD:
    ldx $22
    bpl terrain_gen_advance
    clc
    adc $C0
    tay

terrain_gen_advance:
    sty $C1
loc_A607:
    ldy #$00
    lda collision_result
    bmi loc_A671
    bne loc_A62A
    lda scroll_position
    cmp #$19
    bcc loc_A671
    sbc #$06
    sta entity_timer
    sty $3D
    lda bullet_x
    ldx fuel_state
    cpx #$06
    bcs loc_A625
    adc #$09
loc_A625:
    sbc #$04
    jmp terrain_bank_update
loc_A62A:
    tax
    sec
    sbc #$11
    bcs loc_A657
    lda entity_timer
    adc $B4AF,X
    sta entity_timer
    lda $3D
    ldx fuel_state
    cpx #$06
    bcs loc_A64A
    adc $3F
    sta $3D
    lda $3C
    adc $3E
    jmp terrain_bank_update
loc_A64A:
    sbc $3F
    sta $3D
    lda $3C
    sbc $3E

terrain_bank_update:
    ; Terrain bank update: 16-bit accumulation into $3C/$3D via ADC/SBC with $3E/$3F. Also clamps collision_result ($50) and clears fuel_gauge_idx ($3B = 0).
    sta $3C
    iny
    bne loc_A65E
loc_A657:
    lsr
    lsr
    lsr
    clc
    adc #$02
    tay
loc_A65E:
    ldx collision_result
    inx
    cpx #$39
    bcc loc_A66F
    ldx #$00
    ldy #$00
    lda fuel_level
    beq loc_A66F
    ldx #$FF
loc_A66F:
    stx collision_result
loc_A671:
    sty fuel_gauge_idx
    ldy #$00
    lda #$08
    sta CONSOL
    lda CONSOL
    lsr
    bcs loc_A693
    lda #$02
    sta sound_timer
    sty invuln_flags
    sty lives_count

player_death:
    ; Player death: game_state ($24)=2, arm explosion timer ($C6), trigger explosion sound.
    lda $09
    and #$01
    sta player_number
    ldx #$79
    jmp state_start_game
loc_A693:
    lsr
    bcc loc_A699
    lsr
    bcs loc_A6DC
loc_A699:
    dec $25
    bpl loc_A6DE
    ldx $09
    inx
    cpx #$08
    bcc loc_A6A6
    ldx #$00
loc_A6A6:
    stx $09
    txa
    clc
    adc #$D1
    sty $102A
    sta $102B
    lda #$00
    sta $78
    sta invuln_flags
    sta $6A
    sta $75
    sta player_number
    ldx #$02
    stx sound_timer
loc_A6C2:
    sta score_high,X
    sta $6E,X
    dex
    bpl loc_A6C2
    sta $7F
    stx collision_result
    jsr prepare_display
    jsr update_entities
    jsr update_lives_display
    ldx #$25
    stx lives_count
    ldy #$1E
loc_A6DC:
    sty $25
loc_A6DE:
    lda fuel_level
    bmi loc_A6ED
    cmp #$2F
    bne loc_A6FD
    lda #$09
    sta $5E
loc_A6EA:
    jmp main_frame_sync
loc_A6ED:
    bit invuln_flags
    bpl loc_A6EA
    lda #$80
    ldx $09
    cpx #$04
    bcc loc_A6FB
    lda #$FE
loc_A6FB:
    sta $59
loc_A6FD:
    lda #$02
    sta $A9

main_loop_continue:
    ; Main loop tail: fuel management, jump back to frame sync.
    dec $A9
    bmi loc_A6EA
    lda $59
    cmp #$FE
    bcs loc_A711
    adc $30
    sta $30
    bcc main_loop_continue
loc_A711:
    ldy #$00
    lda $60
    cmp #$09
    bcs loc_A71B
    ldy #$06
loc_A71B:
    sty $B5
    inc $2F
    lda $2F
    cmp #$20
    bcs loc_A72B
    bit $23
    bpl loc_A756
    bmi loc_A759
loc_A72B:
    ldx #$00
    stx $2F
    stx $36
    stx $052D
    lda $23
    and #$04
    ora #$10
    sta $23
    ldx $5F
    stx $2C
    dec section_type
    beq loc_A75C
    ldx $5D
    dex
    bne loc_A756
    stx bridges_crossed
    lda $60
    lsr
    lda #$80
    bcs loc_A754
    lda #$84
loc_A754:
    sta $23
loc_A756:
    jsr update_fuel_gauge
loc_A759:
    jmp state_dying_update
loc_A75C:
    dec $5D
    bne loc_A767
    jsr update_score_display
    ldx #$10
    stx $5D
loc_A767:
    jsr update_fuel_gauge
    ldx $5D
    dex
    bne loc_A781
    stx $2D
    lda #$0D
    sta $5F
    lda $60
    lsr
    bcc loc_A77D
    dec $052D
loc_A77D:
    lda #$36
    bne loc_A7E0
loc_A781:
    lda $60
    lsr
    lda #$07
    bcs loc_A7DC
    lda $2D
    dex
    bne loc_A793
    cmp #$C0
    beq loc_A79E
    bne loc_A7B2
loc_A793:
    asl
    eor $2D
    bmi loc_A7AC
    lda $AA
    and #$30
    bne loc_A7B6
loc_A79E:
    lda $2D
    and #$80
    bne loc_A7A6
    ora #$40
loc_A7A6:
    sta $2D
    lda #$00
    beq loc_A7DC
loc_A7AC:
    lda #$C0
    bit $2D
    bvs loc_A7B4
loc_A7B2:
    lda #$00
loc_A7B4:
    sta $2D
loc_A7B6:
    ldy #$0F
    lda $AA
    and #$0F
    cmp #$02
    bcs loc_A7C2
    adc #$02
loc_A7C2:
    bit $2D
    bpl loc_A7C7
    dey
loc_A7C7:
    ldx $60
    cpx #$14
    bcs loc_A7CE
    dey
loc_A7CE:
    ldx $B5
    beq loc_A7D4
    ldy #$08
loc_A7D4:
    sty $B1
    cmp $B1
    bcc loc_A7DC
    lda $B1
loc_A7DC:
    sta $5F
    asl
    asl
loc_A7E0:
    ldy #$02
    sty section_type
    sta $34
    sec
    sbc $54
    beq state_dying_update
    bcc loc_A7F3
    cmp #$20
    lda #$01
    bne loc_A7F7
loc_A7F3:
    cmp #$E0
    lda #$FE
loc_A7F7:
    adc #$00
    sta $35

state_dying_update:
    lda $35
    beq loc_A826
    bpl loc_A809
    lda $34
    cmp $54
    bcs loc_A80F
    bcc loc_A815
loc_A809:
    lda $54
    cmp $34
    bcc loc_A815
loc_A80F:
    lda #$00
    sta $35
    beq loc_A826
loc_A815:
    lda $54
    clc
    adc $35
    sta $54
    lda $53
    sec
    sbc $35
    sec
    sbc $35
    sta $53
loc_A826:
    bit $23
    bpl loc_A83A
    lda #$00
    sta $C3
    ldx $2F
    lda $BA8F,X
    clc
    adc player_x
    tax
loc_A837:
    jmp respawn_player
loc_A83A:
    lda $052D
    beq loc_A84F
    ldx player_x
    cpx #$18
    bcs loc_A848
    inx
    bne loc_A837
loc_A848:
    cpx #$29
    bcc loc_A84F
    dex
    bne loc_A837
loc_A84F:
    ldx #$01
    ldy #$40
    lda $60
    cmp #$03
    bcs loc_A85F
    ldx #$1C
    ldy #$24
    bne loc_A885
loc_A85F:
    lsr
    bcc loc_A86D
    lda $052D
    bne loc_A86D
    lda $60
    cmp #$0A
    bcs loc_A871
loc_A86D:
    ldy #$28
    bne loc_A883
loc_A871:
    cmp #$14
    bcs loc_A879
    ldx #$0D
    ldy #$34
loc_A879:
    lda $36
    beq loc_A885
    bmi loc_A883
    ldy #$28
    bne loc_A885
loc_A883:
    ldx #$19
loc_A885:
    stx $C4
    sty $C5
    lda $AC
    and #$0F
    ldy $C3
    bne loc_A89E
    cmp #$08
    lda #$01
    bcs loc_A899
    lda #$FF
loc_A899:
    sta $C3
    jmp death_animation
loc_A89E:
    cmp #$08
    bcs loc_A8A5
loc_A8A2:
    jmp loc_A96C
loc_A8A5:
    cmp #$0D
    bne loc_A8B1
    lda $C3
    eor #$FE
    sta $C3
    bne death_animation
loc_A8B1:
    cmp #$0F
    bne death_animation
    ldx #$0E
    bit $2D
    bpl loc_A8BC
    dex
loc_A8BC:
    cpx $5F
    bcc loc_A8A2
    cpx $2C
    bcc loc_A8A2

death_animation:
    lda $54
    cmp $34
    bcs loc_A8CC
    lda $34
loc_A8CC:
    sta $C9
    lda #$40
    sec
    sbc $C9
    asl
    sta $C8
    ldx $4E
    lda $C3
    bpl loc_A929
    lda player_x
    cmp $C4
    bcs loc_A8E5
    jmp game_over_check
loc_A8E5:
    cpx #$0B
    bcs loc_A923
    lda $050B,X
    cmp #$08
    bcc loc_A923
    lda player_x
    bit $2D
    bpl loc_A906
    bit $37
    bpl loc_A8FE
    adc #$7F
    bne loc_A90C
loc_A8FE:
    lda $C8
    lsr
    clc
    adc player_x
    bne loc_A90C
loc_A906:
    clc
    adc $C8
    clc
    adc $C9
loc_A90C:
    sta $B7
    lda entity_xpos_tbl,X
    sec
    sbc #$17
    clc
    adc $B5
    ldy entity_size_tbl,X
    beq loc_A91F
    clc
    adc #$08
loc_A91F:
    cmp $B7
    bcs game_over_check
loc_A923:
    ldx player_x
    dex
    jmp respawn_player
loc_A929:
    lda player_x
    cmp $C5
    bcs game_over_check
    cpx #$0B
    bcs loc_A967
    lda $050B,X
    cmp #$08
    bcc loc_A967
    lda player_x
    bit $2D
    bpl loc_A94A
    bit $37
    bpl loc_A94D
    adc #$3F
    adc $C9
    bne loc_A94D
loc_A94A:
    clc
    adc $C9
loc_A94D:
    clc
    adc #$02
    sta $B7
    lda entity_xpos_tbl,X
    sec
    sbc #$20
    sec
    sbc $B5
    cmp $B7
    bcs loc_A967

game_over_check:
    lda $C3
    eor #$FE
    sta $C3
    bne loc_A96C
loc_A967:
    ldx player_x
    inx

respawn_player:
    stx player_x
loc_A96C:
    bit $23
    bmi loc_A9D7
    lda $2F
    bne loc_A9D7
    lda $AC
    and #$20
    beq loc_A9D7
    lda $60
    lsr
    bcc loc_A989
    lda $AA
    and #$07
    tax
    lda $BBCC,X
    bpl loc_A9A4
loc_A989:
    ldx $5F
    cpx $2C
    bcc loc_A991
    ldx $2C
loc_A991:
    cpx #$02
    bcc loc_A9D7
    bit $2D
    bpl loc_A9A1
    cpx #$05
    bcc loc_A9D7
    txa
    adc #$0A
    tax
loc_A9A1:
    lda $BBD4,X
loc_A9A4:
    sta $BD
    lda $BBB2,X
    sta $BC
    tax
    lda $BBEB,X
    sta $BE
    ldy player_x
    lda $AC
    and #$80
    bmi loc_A9BF
    cpy #$29
    bcc loc_A9C5
    bcs loc_A9C3
loc_A9BF:
    cpy #$18
    bcs loc_A9C5
loc_A9C3:
    eor #$80
loc_A9C5:
    clc
    adc #$01
    sta $36
    txa
    asl
    tax
    lda $BBF0,X
    sta $BA
    lda $BBF1,X
    sta $BB
loc_A9D7:
    lda $3F04
    sec
    sbc #$30
    sta $3F04
    bcs loc_A9F4
    ldx $3F05
    dex
    cpx #$20
    bcs loc_A9EC
    ldx #$3E
loc_A9EC:
    cpx #$2F
    bne loc_A9F1
    dex
loc_A9F1:
    stx $3F05
loc_A9F4:
    ldy $A7
    beq loc_AA05
    ldy #$00
    sty $3F07
    sty $A7
    jsr spawn_entity
    jmp loc_AA7A
loc_AA05:
    ldy $A8
    beq loc_AA16
    ldy #$00
    sty $3F07
    sty $A8
    jsr update_entity_movement
    jmp loc_AA7A
loc_AA16:
    lda terrain_seed_lo
    cmp #$A2
    bcc loc_AA45
    lda #$02
    sta terrain_seed_lo
    lda #$0E
    sta $A7
    sta $3FA6
    sta $3FA7
    sta $3FA8
    ldx #$4E
    stx $3F06
    ldx #$30
    stx $3F07
    ldx #$20
    stx $3F08
    jsr spawn_entity
    jsr spawn_entity
    jmp two_player_switch
loc_AA45:
    jsr update_entity_movement
    lda terrain_seed_hi
    cmp #$A2
    bcc two_player_switch
    lda #$02
    sta terrain_seed_hi
    lda #$0E
    sta $A8
    sta $3FA6
    sta $3FA7
    sta $3FA8
    ldx #$4E
    stx $3F06
    ldx #$30
    stx $3F07
    ldx #$30
    stx $3F08
    jsr update_entity_movement
    jsr update_entity_movement
    jmp loc_AA7A

two_player_switch:
    jsr spawn_entity
loc_AA7A:
    lda #$B8
    sta src_ptr_hi
    lda $3F04
    sta dst_ptr_lo
    lda $3F05
    sta dst_ptr_hi
    lda #$00
    sta $B4
    lda player_x
    bit $2D
    bmi loc_AA95
    clc
    adc $54
loc_AA95:
    tax
    lsr
    lsr
    tay
    dey
    bit $2D
    bmi loc_AAB9
    lda $36
    bpl loc_AAB9
    lda $2F
    cmp $BE
    bcs loc_AAB9
    tya
    pha
    ldy $BD
    jsr fill_terrain_pattern
    jsr copy_terrain_chunk
    pla
    sec
    sbc $BC
    sbc $BD
    tay
loc_AAB9:
    jsr fill_terrain_pattern
    jsr draw_entity_column
    lda $53
    bit $2D
    bpl loc_AB37
    ldy $54
    cpy #$06
    bcs loc_AAD2
    adc $54
    adc $54
    jmp loc_AB37
loc_AAD2:
    stx $B7
    lsr
    clc
    adc $B7
    tax
    lsr
    lsr
    sec
    sbc $B4
    tay
    jsr fill_terrain_blank
    adc #$02
    ldy $54
    cpy #$08
    bcs loc_AAEC
    adc #$10
loc_AAEC:
    jsr loc_B03B
    stx $B7
    lda $54
    asl
    adc $B7
    tax
    lsr
    lsr
    sec
    sbc $B4
    tay
    dey
    lda $54
    cmp #$08
    bcs loc_AB0C
    jsr fill_terrain_row
    clc
    adc #$10
    bne loc_AB31
loc_AB0C:
    lda $36
    beq loc_AB2E
    lda $2F
    cmp $BE
    bcs loc_AB2E
    lda $BD
    sec
    sbc $B4
    sta $B7
    tya
    sec
    sbc $B7
    sbc $BC
    pha
    ldy $B7
    jsr fill_terrain_pattern
    jsr copy_terrain_chunk
    pla
    tay
loc_AB2E:
    jsr fill_terrain_pattern
loc_AB31:
    jsr loc_B03B
    lda $53
    lsr
loc_AB37:
    stx $B7
    clc
    adc $B7
    tax
    lsr
    lsr
    sec
    sbc $B4
    tay
    jsr fill_terrain_blank
    adc #$02
    jsr draw_entity_column
    lda #$30
    sec
    sbc $B4
    tay
    bit $2D
    bmi loc_AB73
    lda $36
    beq loc_AB73
    bmi loc_AB73
    lda $2F
    cmp $BE
    bcs loc_AB73
    tya
    sec
    sbc $BD
    sbc $BC
    tay
    iny
    jsr fill_terrain_pattern
    jsr copy_terrain_chunk
    lda $BD
    tay
    dey
loc_AB73:
    jsr fill_terrain_pattern
    lda $4F
    beq loc_AB7C
    dec $4F
loc_AB7C:
    inc scroll_position
    inc entity_timer
    inc frame_ctr_copy
    inc $0C
    inc $0D
    inc $0E
    inc $0F
    inc $10
    inc $11
    inc $12
    inc $13
    inc $14
    inc $15
    inc $16
    bne loc_ABE1
    ldx $4D
    cpx #$0B
    bcs loc_ABE1
    ldx #$09
loc_ABA2:
    lda $0C,X
    sta $0D,X
    lda entity_xpos_tbl,X
    sta $0517,X
    lda $42,X
    sta $43,X
    lda entity_type_tbl,X
    sta $0501,X
    lda $050B,X
    sta $050C,X
    lda entity_size_tbl,X
    sta $0522,X
    dex
    bmi loc_ABC9
    cpx $4D
    bcs loc_ABA2
loc_ABC9:
    lda $33
    eor #$01
    sta $33
    inc $4D
    ldx $51
    cpx #$0B
    bcs loc_ABD9
    inc $51
loc_ABD9:
    ldx $4E
    cpx #$0B
    bcs loc_ABE1
    inc $4E
loc_ABE1:
    ldx $4E
    cpx #$0B
    bcs loc_ABF3
    ldy fuel_drain_ctr
    dey
    bmi loc_ABEF
    jmp decrement_fuel
loc_ABEF:
    ldy #$0B
    sty $4E
loc_ABF3:
    lda $2F
    and #$0F
    bne loc_ABFD
    ldx $4D
    bne loc_AC00
loc_ABFD:
    jmp loc_AE96
loc_AC00:
    dex
    stx $4D
    stx $4E
    lda #$00
    sta $B8
    lda $23
    sta $42,X
    bpl loc_AC23
    and #$04
    ora #$D2
    sta $0B
    lda #$07
    sta $B8
    ldy #$0E
    lda $54
    sec
    sbc #$06
    jmp entity_ai_update
loc_AC23:
    ldy #$01
    lda $2F
    beq loc_AC3E
    lda $60
    sec
    sbc #$08
    bmi loc_AC3B
    cmp #$20
    bcs loc_AC62
    asl
    asl
    asl
    cmp $AB
    bcs loc_AC62
loc_AC3B:
    jmp entity_ai_update
loc_AC3E:
    dey
    lda $5D
    clc
    adc section_type
    cmp #$12
    bcs loc_AC3B
    lda $60
    cmp scanlines_per_row
    bcc loc_AC50
    lda scanlines_per_row
loc_AC50:
    eor #$FF
    sec
    adc #$3F
    asl
    cmp $AB
    bcc loc_AC62
    bit $AA
    bvs loc_ACB9
    ldy #$0F
    bne loc_AC8C
loc_AC62:
    lda $AB
    and #$07
    tax
    ldy $BB1C,X
    ldx $60
    cpy #$07
    bne loc_AC76
    cpx #$05
    bcs loc_AC84
    ldy #$0D
loc_AC76:
    cpy #$0C
    bne loc_AC84
    lda $2F
    bne loc_AC82
    cpx #$03
    bcs loc_AC84
loc_AC82:
    ldy #$08
loc_AC84:
    cpy #$0D
    bne loc_AC8C
    lda #$05
    sta $B8
loc_AC8C:
    lda $5F
    cmp $2C
    bne loc_ACBC
    sta $B3
    lda $60
    lsr
    bcc loc_ACC2
    lda #$3F
    ldx $B8
    beq loc_ACA1
    lda #$37
loc_ACA1:
    sbc $B5
    sta $B1
    lda $AA
    and #$3F
    clc
    adc $B5
    cmp $B1
    bcc loc_ACB2
    lda $B1
loc_ACB2:
    clc
    adc $54
    bit $AA
    bmi loc_AD29
loc_ACB9:
    jmp entity_ai_update
loc_ACBC:
    bcs loc_ACC0
    lda $2C
loc_ACC0:
    sta $B3
loc_ACC2:
    ldx #$0E
    bit $2D
    bpl loc_ACC9
    dex
loc_ACC9:
    cpx $B3
    bcs loc_ACD5
    cpy #$07
    beq loc_ACD5
    ldy #$01
    bne entity_ai_update
loc_ACD5:
    ldx $5D
    dex
    bne loc_ACE0
    inc $B5
    inc $B5
    bne loc_ACEC
loc_ACE0:
    ldx #$0D
    bit $2D
    bpl loc_ACE8
    ldx #$0A
loc_ACE8:
    cpx $B3
    bcs loc_ACF6
loc_ACEC:
    cpy #$0D
    bne loc_ACF6
    lda #$00
    sta $B8
    ldy #$08
loc_ACF6:
    lda $B3
    asl
    asl
    beq loc_AD10
    bit $2D
    bpl loc_AD10
    eor #$FF
    adc #$41
    ldx $AA
    stx $37
    bpl loc_AD1D
    eor #$FF
    adc #$80
    bne loc_AD14
loc_AD10:
    bit $AA
    bmi loc_AD19
loc_AD14:
    sec
    adc $B5
    bne entity_ai_update
loc_AD19:
    eor #$FF
    adc #$81
loc_AD1D:
    cpy #$0F
    sbc #$08
    sbc $B5
    ldx $B8
    beq loc_AD29
    sbc #$08
loc_AD29:
    cpy #$0F
    beq entity_ai_update
    pha
    ldx $4E
    lda $42,X
    ora #$08
    sta $42,X
    pla

entity_ai_update:
    ldx $4E
    clc
    adc #$20
    sta warm_start
    lda player_x
    bit $052D
    bpl loc_AD51
    cmp #$18
    bcs loc_AD4B
    lda #$18
loc_AD4B:
    cmp #$28
    bcc loc_AD51
    lda #$28
loc_AD51:
    clc
    adc warm_start
    sta entity_xpos_tbl,X
    tya
    sta entity_type_tbl,X
    sta $050B,X
    cmp #$08
    bne loc_AD88
    lda $51
    cmp #$0B
    bcc loc_AD88
    lda $5D
    cmp #$0E
    bcs loc_AD88
    lda $AC
    and #$70
    bne loc_AD88
    lda $60
    cmp #$0D
    bcc loc_AD88
    lda #$0A
    sta entity_type_tbl,X
    sta $050B,X
    lda #$00
    sta $C1
    stx $51
loc_AD88:
    lda sprite_height_tbl,Y
    clc
    adc #$04
    sta fuel_drain_ctr
    lda $BBA2,Y
    sta entity_size_tbl,X
    lda #$61
    sec
    sbc fuel_drain_ctr
    sta $0C,X
    lda $2F
    ora fuel_state
    beq loc_ADA6
loc_ADA3:
    jmp decrement_fuel
loc_ADA6:
    bit $23
    bpl loc_ADCD
    lda #$0B
    sta scroll_position
    ldx #$05
    lda #$00
    sta sound_flags
    sta $3F
    lda #$02
    sta $3E
    lda $AA
    and #$7F
    cmp #$40
    bcc loc_ADC3
    inx
loc_ADC3:
    stx fuel_state
    clc
    adc #$40
    sta bullet_x
    jmp decrement_fuel
loc_ADCD:
    ldx $4E
    lda entity_type_tbl,X
    cmp #$08
    bcc loc_ADA3
    lda $60
    cmp #$07
    bcc loc_ADA3
    lda $5D
    cmp #$05
    bcc loc_ADA3
    cmp #$0E
    bcs loc_ADA3
    lda $AC
    and #$C0
    bne loc_ADA3
    ldx $5F
    cpx $2C
    bcc loc_ADF4
    ldx $2C
loc_ADF4:
    cpx #$02
    bcc loc_ADA3
    cpx #$0E
    bcs loc_ADA3
    lda $AC
    lsr
    lsr
    lsr
    ldy $60
    cpy #$0A
    bne loc_AE09
    eor #$07
loc_AE09:
    and #$06
    bne loc_AE17
    bit $2D
    bmi loc_AE17
    cpx #$08
    bcs loc_AE17
    lda #$08
loc_AE17:
    tay
    lda $B7F4,Y
    sta $3E
    lda $B7F5,Y
    sta $3F
    ldy #$05
    bit $2D
    bpl loc_AE3E
    lda $36
    bne decrement_fuel
    cpx #$0B
    bcs decrement_fuel
    cpx #$04
    bcc decrement_fuel
    lda $AC
    lsr
    bcc loc_AE3A
    iny
loc_AE3A:
    lda #$4C
    bne loc_AE83
loc_AE3E:
    lda $60
    lsr
    lda $BFE4,X
    bcc loc_AE6C
    lda $AC
    and #$1F
    cmp #$10
    bcc loc_AE59
    adc #$57
    ldx $36
    beq loc_AE56
    bpl loc_AE5E
loc_AE56:
    iny
    bpl loc_AE63
loc_AE59:
    ldx $36
    bpl loc_AE63
    iny
loc_AE5E:
    eor #$FF
    sec
    adc #$78
loc_AE63:
    clc
    adc player_x
    clc
    adc #$20
    jmp fuel_and_continue
loc_AE6C:
    cmp #$50
    bcc loc_AE79
    ldx $36
    beq loc_AE76
    bpl loc_AE7E
loc_AE76:
    iny
    bpl loc_AE83
loc_AE79:
    ldx $36
    bpl loc_AE83
    iny
loc_AE7E:
    eor #$FF
    sec
    adc #$98
loc_AE83:
    clc
    .byte $69  ; $AE84
    ; End of entity update loop: after all 6 slots have been processed, fall through to collision/fuel handling.
    .byte $30  ; $AE85

fuel_and_continue:
    ; fuel_and_continue tail: stores computed bullet_x ($39 = table + player_x + offsets), writes fuel_state ($38 = Y), resets scroll_position ($3A = $0C), collision_result ($50 = $FF), sound_flags ($41 = $FF), then falls into decrement_fuel.
    sta bullet_x
    sty fuel_state
    lda #$0C
    sta scroll_position
    lda #$FF
    ; Fuel management entry: check fuel_state ($38) and fuel_level ($76) before decrementing the drain counter.
    sta collision_result
    sta sound_flags

decrement_fuel:
    ; DEC fuel_drain_ctr ($32); on wrap, the main loop decrements fuel_level ($76). JMP back to main_loop_continue.
    dec fuel_drain_ctr
loc_AE96:
    jmp main_loop_continue

main_frame_sync:
    ; === MAIN FRAME SYNC === wait for VCOUNT < $50 (top of visible area). Entered from the state dispatch at $A1D9 once per frame.
    lda VCOUNT
    cmp #$50
    bcs main_frame_sync

call_frame_update:
    ; JSR frame_update ($AFD0) - advance terrain, prepare display for the next frame.
    jsr frame_update
    lda scroll_position
    cmp #$BC
    bcc loc_AEAF
    ldy #$00
    sty fuel_state
    sty scroll_position
loc_AEAF:
    sta dst_ptr_lo
    lda #$0D
    sta dst_ptr_hi
    ; Terrain scroll start: advance the terrain bank tables and prepare to blit a new row at the bottom of the playfield.
    lda fuel_state
    asl
    tax
    lda $B4D1,X
    sta src_ptr_lo
    lda $B4D2,X
    sta src_ptr_hi
    ldy #$0A
    jsr copy_row_data
    lda frame_counter
    lsr
    lsr
    lsr
    and #$07
    tax
    lda player_color_cycle,X
    sta COLPM0
    ldx frame_ctr_copy
    cpx #$1C
    bcc loc_AEFA
    lda $0AFE,X
    and #$FC
    sta $0AFE,X
    lda $0AFF,X
    and #$FC
    sta $0AFF,X
    lda $0B00,X
    and #$FC
    ldy $C1
    beq loc_AEF7
    ora #$03
loc_AEF7:
    ; Missile DMA clear loop: sta $0B00,X with X iterating full range. Called each frame to wipe missile sprites (including M1 bullet) before the per-frame draw routines repopulate them.
    sta $0B00,X
loc_AEFA:
    lda entity_timer
    cmp #$BC
    bcc loc_AF09
    ; Redraw fuel gauge rows: reset fuel_gauge_idx ($3B=0), then loop reading $BAB7,X pointer pairs and blitting each row via copy_row_data.
    ldy #$FF
    sty collision_result
    iny
    sty fuel_gauge_idx
    sty entity_timer
loc_AF09:
    sta dst_ptr_lo
    lda #$0C
    sta dst_ptr_hi
    lda fuel_gauge_idx
    asl
    tax
    lda fuel_gauge_ptrs,X
    sta src_ptr_lo
    lda $BAB8,X
    sta src_ptr_hi
    ldy #$0F
    jsr copy_row_data
    ldy #$0D
    lda $5E
    asl
    tax
    lda $BACD,X
    sta src_ptr_lo
    lda $BACE,X
    sta src_ptr_hi
loc_AF32:
    lda (src_ptr_lo),Y
    sta $0EAA,Y
    dey
    bpl loc_AF32
    lda VCOUNT
    cmp #$07
    bcs loc_AF52
    lda lives_count
    bne loc_AF48
    jsr update_entities
loc_AF48:
    jsr check_terrain_collision
    lda player_number
    bne loc_AF55
    jsr process_collision_results
loc_AF52:
    jmp attract_wait
loc_AF55:
    jsr loc_B068
    .byte $4C  ; $AF58
    .byte $97  ; $AF59
    ; Terrain scroll end: falls through into per-frame housekeeping before the entity loop.
    .byte $A1  ; $AF5A

.proc update_entities
    ldx #$00
    ldy $6A
    beq loc_AF75
    lda $7E
    beq loc_AF6C
    and #$08
    beq loc_AF6C
    dey
    beq loc_AF75
loc_AF6C:
    lda #$9C
loc_AF6E:
    sta $1033,X
    inx
    dey
    bne loc_AF6E
loc_AF75:
    lda #$00
loc_AF77:
    sta $1033,X
    inx
    cpx #$09
    bcc loc_AF77
    rts
.endp


.proc prepare_display
    lda $09
    ror
    bcs loc_AF8E
    jsr loc_B06E
    lda #$DB
    ldx #$D0
    bne loc_AF95
loc_AF8E:
    jsr loc_B068
    lda #$4F
    ldx #$50
loc_AF95:
    sta $100B
    stx $1012
    lda $09
    lsr
    tax
    lda $B494,X
    sta $66
    sta $71
    lda $B498,X
    sta $67
    sta $72
    lda $B49C,X
    sta $68
    sta $73
    lda $B4A0,X
    sta $69
    sta $74
    lda $B4A4,X
    sta $60
    sta $6B
    lda $B4A8,X
    sta score_100
    sta $6D
    lda #$00
    sta score_10k
    sta $6C
    rts
.endp


.proc frame_update
    ; Frame update: scroll terrain, copy new row data to screen, advance display-list LMS pointers.
    lda $23
    and #$04
    eor #$D6
    ldx $4E
    cpx #$0B
    bcs loc_AFE8
    ldy $42,X
    bpl loc_AFE8
    ldy $0C,X
    cpy #$5C
    bcs loc_AFE8
    eor #$04
loc_AFE8:
    sta COLPF0
    rts
.endp


fill_terrain_row:
    lda #$AA
    bne loc_AFFA

fill_terrain_blank:
    lda #$FF
    bne loc_AFFA

fill_terrain_pattern:
    ; Fill terrain row: write pattern $55 to a mode-14 bitmap line (used to prefill newly-exposed river rows).
    lda $4F
    bne fill_terrain_row
    lda #$55
loc_AFFA:
    sty $B6
    dey
    bmi loc_B009
loc_AFFF:
    sta (dst_ptr_lo),Y
    dey
    bpl loc_AFFF
    lda $B6
    jsr advance_dst_ptr
loc_B009:
    txa
    and #$03
    asl
    asl
    rts

.proc copy_row_data
    ; Blit Y+1 bytes from (src_ptr) to (dst_ptr) - general row copy helper.
    tya
    bmi loc_B019
loc_B012:
    lda (src_ptr_lo),Y
    sta (dst_ptr_lo),Y
    dey
    bpl loc_B012
loc_B019:
    rts
.endp


copy_terrain_chunk:
    ldy $BC
    dey
loc_B01D:
    lda ($BA),Y
    sta (dst_ptr_lo),Y
    dey
    bpl loc_B01D
    lda $BA
    clc
    adc $BC
    sta $BA
    bcc loc_B02F
    inc $BB
loc_B02F:
    lda $BC
    jmp advance_dst_ptr

draw_entity_column:
    ldy $4F
    beq loc_B03B
    clc
    adc #$10
loc_B03B:
    sta src_ptr_lo
    ldy #$01
    lda (src_ptr_lo),Y
    sta (dst_ptr_lo),Y
    dey
    lda (src_ptr_lo),Y
    sta (dst_ptr_lo),Y
    dey
    lda #$02

.proc advance_dst_ptr
    ; dst_ptr += 48 (one ANTIC mode 14 row pitch).
    pha
    clc
    adc dst_ptr_lo
    sta dst_ptr_lo
    bcc loc_B055
    inc dst_ptr_hi
loc_B055:
    pla
    clc
    adc $B4
    sta $B4
    rts
.endp


check_terrain_collision:
    lda #$90
    sta $B1
    ldx #$61
    ldy #$23
loc_B064:
    lda #$02
    bne loc_B086
loc_B068:
    lda #$50
    ldy #$0C
    bne loc_B080
loc_B06E:
    lda $09
    and #$0E
    asl
    adc #$80
    tax
    lda #$D0
    ldy #$0C
    bne loc_B082

process_collision_results:
    ; Process collision flags set by the DLI kernel ($1A-$22): translate into entity hits, bank hits, and fuel-depot overlap.
    lda #$10
    ldy #$02
loc_B080:
    ldx #$63
loc_B082:
    sta $B1
    lda #$03
loc_B086:
    bit invuln_flags
    bmi loc_B0C6
    sta $B2
loc_B08C:
    lda temp_ptr_lo,X
    lsr
    lsr
    lsr
    lsr
    bne loc_B0AF
    sta $1000,Y
    iny
    lda temp_ptr_lo,X
    and #$0F
    bne loc_B0BA
    sta $1000,Y
    iny
    inx
    dec $B2
    bne loc_B08C
    beq loc_B0C6
loc_B0A9:
    lda temp_ptr_lo,X
    lsr
    lsr
    lsr
    lsr
loc_B0AF:
    clc
    adc $B1
    sta $1000,Y
    iny
    lda temp_ptr_lo,X
    and #$0F
loc_B0BA:
    clc
    adc $B1
    sta $1000,Y
    iny
    inx
    dec $B2
    bne loc_B0A9
loc_B0C6:
    rts

.proc spawn_entity
    ; Entity spawner: find empty entity slot, select type from $BB24 transition table, init position/shape, reset spawn_cooldown ($5F).
    ldy #$03
    ldx terrain_seed_hi
loc_B0CB:
    lda $B0E9,Y
    sta $3F04,X
    inx
    dey
    bpl loc_B0CB
    inc terrain_seed_hi
    rts
.endp


.proc update_entity_movement
    ; Per-entity movement dispatch: stationary / oscillate / track player - depends on entity_type at $0500.
    ldy #$03
    ldx terrain_seed_lo
loc_B0DC:
    lda $B0ED,Y
    sta $3F04,X
    inx
    dey
    bpl loc_B0DC
    inc terrain_seed_lo
    rts
.endp

    bmi loc_B0EB
loc_B0EB:
    lsr $200E
    brk
    .byte $4E  ; $B0EF
    .byte $0E  ; $B0F0
init_display_region:
    .byte $BD  ; $B0F1
    sbc ($BA,X)
    sta $4D,X
    dex
    bpl init_display_region
    stx $17
    ldx #$0B
    lda #$00
    sta src_ptr_lo
    sta cold_start_flag
loc_B103:
    ldy #$BC
    stx src_ptr_hi
loc_B107:
    sta (src_ptr_lo),Y
    dey
    bne loc_B107
    inx
    cpx #$10
    bcc loc_B103
    lda #$00
    ldx #$20
loc_B115:
    stx src_ptr_hi
loc_B117:
    sta (src_ptr_lo),Y
    iny
    bne loc_B117
    inx
    cpx #$3F
    bcc loc_B115
    inc cold_start_flag
    lda #$00
    ldx #$21
loc_B127:
    sta $2B,X
    dex
    bpl loc_B127
    inx
loc_B12D:
    sta entity_type_tbl,X
    inx
    bne loc_B12D
    ldy #$06
    lda pal_flag
    bne loc_B13B
    ldy #$12
loc_B13B:
    ldx #$06
loc_B13D:
    lda #$FF
    sta $0EDB,Y
    lda #$FC
    sta $0FDB,Y
    dey
    dex
    bpl loc_B13D

draw_bullet_pmg:
    ; Draws the player's bullet into missile DMA. M1 is the bullet sprite — bits 2-3 of packed missile DMA bytes ($0C = %00001100) write into the M1 slot. The routine sets X=$09 (iteration count) and Y=$09 (PAL) or Y=$10 (NTSC, overridden at $B153 when pal_flag=0), then falls into the write loop at $B155. EMPIRICALLY VERIFIED via bridge in active player mode: with fire held, the M1 DMA band grew from 10 bytes at $0BCC-$0BD5 (at-rest / muzzle) to include a moving segment starting at $0BA9, then $0BA3 over ~20 frames, i.e. the bullet migrates toward lower Y addresses = upward on screen at ~2 scanlines per frame. Exact motion mechanism (separate draw at a running Y offset) not yet traced.
    ldx #$09
    ldy #$09
    lda pal_flag
    bne draw_bullet_loop
    ldy #$10

draw_bullet_loop:
    ; Bullet draw loop body: writes $0C (M1 bit pattern, bits 2-3) into missile DMA at $0BCC+Y. dey; dex; bpl back. X counts iterations (10 total), Y indexes into the M DMA buffer. Loop writes the at-rest bullet template; a separate mechanism (probably a second draw path keyed off bullet_alive state) emits the moving in-flight bullet at increasing scanline offsets.
    lda #$0C
loc_B157:
    sta $0BCC,Y
    dey
    dex
    bpl loc_B157
    lda #$17
    sta fuel_drain_ctr
    lda #$0E
    sta $0515
    lda #$4A
    sta $16
    lda $60
    lsr
    lda #$D6
    bcs loc_B174
    lda #$D2
loc_B174:
    sta $0B
    and #$84
    sta $4C
    sta $23
    lda #$0A
    sta $4D
    sta $4E
    rts

.proc update_fuel_gauge
    ; Redraw fuel gauge bars in status screen RAM at $3D80+.
    lda $AB
    asl
    asl
    asl
    asl
    asl
    eor $AB
    asl
    rol $AA
    rol $AC
    rol $AB
    rts
.endp


.proc update_score_display
    ldx $60
    cpx #$50
    bcc loc_B19C
    ldx #$4E
loc_B19C:
    lda $66
    sta $AD
    lda $67
    sta $AE
    lda $68
    sta $AF
    lda $69
    sta $B0
    lda $AA
    sta $66
    lda $AB
    sta $67
    lda $AC
    sta $68
    lda player_x
    sta $69
    inx
    stx $60
    rts
.endp


play_score_sound:
    sty $7B
    sta $7A
    lda $4C
    eor $23
    and #$04
    beq loc_B1F8
    lda bridges_crossed
    beq loc_B1D9
    bit $23
    bpl loc_B1F8
    jsr update_score_display
    bne loc_B1F8
loc_B1D9:
    bit $23
    bmi loc_B1F8
    ldx $60
    dex
    cpx #$4E
    bne loc_B1E6
    ldx #$50
loc_B1E6:
    stx $60
    lda $AD
    sta $66
    lda $AE
    sta $67
    lda $AF
    sta $68
    lda $B0
    sta $69
loc_B1F8:
    lda #$06
    sta $5E
    lda #$FE
    ldx #$00
loc_B200:
    sta fuel_level
    stx lives_count
    jsr loc_B221
    lda #$0B
    sta $51
    ldx frame_ctr_copy
    lda $0B00,X
    and #$FC
    sta $0B00,X
    lda #$01
    sta $56
    rts

.proc update_lives_display
    lda #$FF
    sta frame_counter
    tax
    bne loc_B200
.endp

loc_B221:
    ; Bullet helper - called from $A5AA with saved X; returns a bullet-column value merged into $56.
    lda #$00
loc_B223:
    ; Bullet helper companion - paired with $B221 in the bullet-vs-slot interaction path.
    ldx $56
    dex
    beq loc_B246
    cpx #$1B
    bcs loc_B22E
    ldx #$1B
loc_B22E:
    sta $0B01,X
    sta $0B02,X
    sta $0B03,X
    sta $0B04,X
    sta $0B05,X
    sta $0B06,X
    sta $0B07,X
    sta $0B08,X
loc_B246:
    rts

.proc setup_game_start
    lda $09
    and #$01
    beq loc_B260
    eor player_number
    sta player_number
    ldx #$0A
loc_B253:
    lda $60,X
    pha
    lda $6B,X
    sta $60,X
    pla
    sta $6B,X
    dex
    bpl loc_B253
loc_B260:
    rts
.endp


.proc reset_entities
    sta src_ptr_lo
    lda #$00
    sta src_ptr_hi
    sta dst_ptr_hi
    lda $09
    and #$0E
    asl
    adc #$80
    sta dst_ptr_lo
    ldy #$00
loc_B274:
    lda (src_ptr_lo),Y
    cmp (dst_ptr_lo),Y
    bcc loc_B297
    bne loc_B281
    iny
    cpy #$03
    bcc loc_B274
loc_B281:
    ldy #$02
loc_B283:
    lda (src_ptr_lo),Y
    sta (dst_ptr_lo),Y
    dey
    bpl loc_B283
    ldy #$0F
loc_B28C:
    sty $A0
    jsr read_console
    sta $0090,Y
    dey
    bpl loc_B28C
loc_B297:
    rts
.endp


.proc read_console
    ; Read CONSOL switches ($D01F), return decoded START/SELECT/OPTION in A.
    lda #$00
    ldx #$0E
    clc
loc_B29D:
    adc $80,X
    eor $A0
    dex
    bpl loc_B29D
    rts
.endp


vbi_handler:
    ; === VBI HANDLER === sound engine — engine hum (AUDF1/AUDC1), explosion burst driven by $C6, score/refuel effects on channels 2-3, silence when $24 is in attract/dying.
    cld
    lda sound_timer
    beq loc_B2BA
    dec sound_timer
    lda #$40
    sta AUDF1
    lda #$AF
    sta AUDC1
    ldx #$00
    beq loc_B2C5
loc_B2BA:
    lda var_0024
    cmp #$02
    bcc vbi_engine_sound
    ldx #$00
    stx AUDC1
loc_B2C5:
    stx AUDC2
    stx AUDC3
    jmp vbi_game_timers

vbi_engine_sound:
    ldy #$39
    lda $7B
    ldx $7A
    beq loc_B2F9
    dex
    beq loc_B2E1
    ldy #$1F
    cpx #$02
    bcs loc_B2E6
    ldy #$11
loc_B2E1:
    sec
    ror
    tax
    bne loc_B2EE
loc_B2E6:
    beq loc_B2EA
    ldy #$3F
loc_B2EA:
    clc
    adc #$A0
    tax
loc_B2EE:
    dec $7B
    bne loc_B34C
    lda #$00
    sta $7A
    jmp loc_B34C
loc_B2F9:
    lda fuel_level
    bne loc_B34F
    lda m1_x_shift_src
    cmp #$40
    bcs loc_B32C
    ldy $7B
    bne loc_B309
    ldy #$3F
loc_B309:
    dey
    sty $7B
    ldx m1_x_base
    stx render_x
    cmp #$04
    bcs loc_B321
    rol render_x
    rol
    rol render_x
    rol
    eor #$FF
    adc #$20
    asl
    bne loc_B326
loc_B321:
    cpy #$1C
    bcc loc_B32C
    tya
loc_B326:
    asl
    tay
    ldx #$AF
    bne loc_B34C
loc_B32C:
    lda $59
    lsr
    lsr
    lsr
    lsr
    eor #$FF
    sec
    adc #$1F
    tay
    lda $052C
    cmp #$1B
    bcc loc_B343
    dec $052C
    tay
loc_B343:
    lda joystick_dir
    and #$03
    tax
    lda $B4AC,X
    tax
loc_B34C:
    sty AUDF1
loc_B34F:
    stx AUDC1
    ldx $7C
    beq loc_B367
    dec $7C
    txa
    lsr
    clc
    adc #$04
    and #$0F
    adc #$80
    tax
    lda SKRES
    ora #$18
loc_B367:
    ldy $7F
    beq loc_B37E
    dec $7F
    bit invuln_flags
    bmi loc_B37E
    tya
    clc
    adc #$46
    tax
    eor #$0E
    adc #$04
    and #$0F
    asl
    asl
loc_B37E:
    sta AUDF2
    stx AUDC2
    ldx #$00
    lda $7D
    beq loc_B399
    dec $7D
    eor #$FF
    sec
    adc #$1C
    asl
    sta render_x
    asl
    adc render_x
    ldx #$A8
loc_B399:
    ldy $7E
    beq loc_B3B0
    dey
    sty $7E
    tya
    and #$0F
    clc
    adc #$A0
    tax
    tya
    lsr
    lsr
    lsr
    lsr
    tay
    lda $B4E1,Y
loc_B3B0:
    sta AUDF3
    stx AUDC3
    ldx #$00
    bit invuln_flags
    bmi vbi_game_timers
    lda collision_result
    bmi vbi_game_timers
    beq vbi_game_timers
    cmp #$11
    bcs loc_B3CE
    tax
    lda $B4C0,X
    ldx #$AC
    bne loc_B3D5
loc_B3CE:
    ldx #$8C
    lda SKRES
    ora #$20
loc_B3D5:
    sta POT6

vbi_game_timers:
    ; VBI continuation: game timers, color animation, end of per-frame housekeeping before exiting via XITVBV.
    stx POT7
    inc frame_counter
    bne loc_B3F2
    inc invuln_flags
    lda invuln_flags
    and #$C7
    sta invuln_flags
    and #$07
    bne loc_B3F2
    inc $78
    bne loc_B3F2
    sec
    ror $78
loc_B3F2:
    ldy #$3F
    ldx #$C0
    lda $78
    rol
    lda #$03
    bcc loc_B402
    lda #$00
    tay
    ldx #$40
loc_B402:
    sta GRACTL
    stx NMIEN
    sty DMACTL
    ldx #$01
    stx PRIOR
    dex
    stx HPOSP3
    ldx #$84
    lda var_0024
    cmp #$02
    bcs loc_B427
    lda $31
    beq loc_B427
    dec $31
    lsr
    bcc loc_B427
    ldx #$42
loc_B427:
    stx COLPF2
    lda #$20
    sta COLPF1
    jsr frame_update
    lda cold_start_flag
    bne loc_B441
    lda #$00
    sta COLPF0
    sta COLPF1
    sta COLPF2
loc_B441:
    lda #$00
    sta DLISTL
    lda #$3F
    sta DLISTH
    lda #$44
    ldx #$E7
    ldy lives_count
    beq loc_B478
    ldy dli_status_x
    dey
    tya
    ldx $3FB3
    ldy $26
    dey
    bpl loc_B478
    inx
    cpx #$D7
    bcc loc_B466
    ldx #$4B
loc_B466:
    cpx #$9B
    bcs loc_B474
    cpx #$7E
    bcc loc_B474
    bne loc_B476
    lda #$EC
    bne loc_B476
loc_B474:
    lda #$00
loc_B476:
    ldy #$07
loc_B478:
    sta dli_status_x
    stx $3FB3
    sty $26
    .byte $8C  ; $B47F
set_terrain_colors:
    .byte $04  ; $B480
    .byte $D4  ; $B481
    jmp XITVBV
    pha
    lda #$00
    sta IRQEN
    pla
    rti
    ; Color tables: per-entity-type color palettes (68 bytes through $B4D0). Indexed by entity type when setting COLPM during the DLI.
    sty $D6
    dec $1A,X
    rol $26
    .byte $44,$E3              ; $B493: nop FR1+3    [$E3] = $00
    jsr set_terrain_colors
    .byte $D4,$83              ; $B498: nop STJMP,X  [$47] = $1C
    cmp ($6D,X)
    .byte $F7,$B5              ; $B49C: isb $B5,X    [$79]
    .byte $7F,$C0,$20          ; $B49E: rra $20C0,X  [$2184] = $55
    and ($20,X)
    and (temp_ptr_hi,X)
    ora $14
    .byte $32                  ; $B4A7: kil
    ora (render_end,X)
    jsr collision_result
    .byte $83,$86              ; $B4AD: sax (STSEL,X) [$1414]
    sty temp_ptr_lo
    brk
    brk
    brk
    ora (temp_ptr_lo,X)
    ora (temp_ptr_lo,X)
    ora (temp_ptr_lo,X)
    ora (temp_ptr_lo,X)
    ora (temp_ptr_hi,X)
    ora (temp_ptr_hi,X)
    .byte $0C,$0E,$11          ; $B4C0: nop $110E    [$110E] = $00
    ora joystick_dir,X
    asl $2B24,X
    .byte $34,$3E              ; $B4C8: nop FTYPE,X  [$02] = $98
    lsr
    eor $806B,Y
    txs
    .byte $B9  ; $B4CF
    .byte $DE  ; $B4D0
    ; Terrain row pointer table: pairs of addresses (48 bytes through $B4FF) feeding the terrain row copier.
    .byte $00  ; $B4D1
    lda $B94C,Y
    .byte $1A                  ; $B4D5: nop
    lda $B94C,Y
    sbc #$B9
    .byte $F4,$B9              ; $B4DB: nop $B9,X    [$7D] = $0D
    .byte $FF,$B9,$0A          ; $B4DD: isb $0AB9,X  [$0B7D]
    tsx
    bmi loc_B523
    bvc loc_B545
    .byte $04,$04              ; $B4E5: nop RAMLO    [$04] = $A7
    .byte $04,$04              ; $B4E7: nop RAMLO    [$04] = $A7
    php
    php
    php
    php
    php
    php
    .byte $1C,$1C,$06          ; $B4EF: nop $061C,X  [$06E0] = $2F
    asl entity_slot
    asl entity_slot
    .byte $04,$04              ; $B4F6: nop RAMLO    [$04] = $A7
    .byte $04,$FF              ; $B4F8: nop $FF      [$FF] = $00
    .byte $FF,$FF,$FF          ; $B4FA: isb $FFFF,X  [$00C3]
    .byte $FF,$FF,$FF          ; $B4FD: isb $FFFF,X  [$00C3]

dli_handler:
    ; === DLI HANDLER === save regs, branch by VCOUNT at $B509: VCOUNT<$3F -> playfield kernel at $B510, else status-bar handler at $B72A.
    cld
    pha
    txa
    pha
    tya
    pha
    lda VCOUNT
    ; DLI dispatch: compares current VCOUNT to $3F and branches to the playfield or status-bar kernel accordingly.
    cmp #$3F
    bcc dli_playfield_kernel
    jmp dli_status_bar

dli_playfield_kernel:
    ; === DLI PLAYFIELD KERNEL === Baseline stores for the playfield zone: clears collision/sprite-mask flags $1A-$22; HPOSM1 = ($5B>>3)+$5C at $B52C (bullet X — M1 is the bullet); HPOSP2 = player_sprite_x ($57, the jet) at $B534; HPOSM2 = P2+4; COLPM1 = 0 at $B541 (bullet/P1 color hidden at top of screen, flipped to $1E at river zone $B697 — M1 shares COLPM1 with P1, which is why COLPM1 matters even though P1's bitmap is empty); HPOSP1 = bullet_x ($39) at $B54F (but P1's DMA buffer is never populated — verified); HPOSM0 = $C1; HPOSP0 = $3C (=0 in gameplay, P0 hidden). Falls into WSYNC loop at $B567 which multiplexes enemies on P3.
    lda #$FF
    sta $1A
    sta $1B
    sta $1C
    sta $1D
    sta $1E
    sta $20
    sta WSYNC
    sta $21
loc_B523:
    sta $22
    sta $1F
    lda m1_x_shift_src
    lsr
    lsr
    lsr
    clc
    adc #$5C
    sta HPOSM1
    lda player_sprite_x
    ; DLI baseline: HPOSP2 = player_sprite_x ($57). The jet is on P2. Verified: moving $57 moves the jet sprite.
    sta HPOSP2
    clc
    adc #$04
    sta HPOSM2
    lda #$00
    sta COLPM1
    lda #$8E
    .byte $A6  ; $B544
loc_B545:
    .byte $77  ; $B545
    bne loc_B54A
    lda #$1E
loc_B54A:
    sta COLPM2
    ; DLI baseline: HPOSP1 = bullet_x ($39) - positions the bullet (P1) for the playfield zone.
    lda bullet_x
    ; DLI baseline: HPOSP1 = $39. P1 is positioned each frame but its DMA buffer at $0D00-$0DFF is never written to — verified by write-watchpoint and 180-frame DMA sweep. The 'bullet' the game renders is M1 (drawn at $B14B), not P1.
    sta HPOSP1
    lda $C1
    sta HPOSM0
    ; DLI baseline: HPOSP0 = $3C (terrain scroll low byte) - P0 inherits its X from the terrain scroll accumulator.
    lda $3C
    ; DLI baseline: HPOSP0 = $3C. $3C = 0 during gameplay, so this effectively hides P0 at X=0. The P0 DMA buffer ($0C00-$0CFF) is actively zeroed each frame via the generic indirect copier at $B012, so P0 renders blank even if it were on-screen. P0 is effectively unused in the playfield zone.
    sta HPOSP0
    ldy $4D
    ldx #$5D
    bne loc_B565
loc_B562:
    ldy entity_slot
    iny
loc_B565:
    sty entity_slot

dli_wsync_loop:
    ; DLI WSYNC loop: per-scanline entity multiplexer (hottest code). Only HPOSP3/COLPM3/SIZEP3 change per scanline — HPOSP0/1/2 keep their $B510 baselines.
    sta WSYNC
    txa
    cmp $000C,Y
    bcs loc_B575
    inx
    bne dli_wsync_loop
    beq loc_B5E3
loc_B575:
    stx render_x
    inx
    beq loc_B5E3
    lda entity_xpos_tbl,Y
    sta HPOSP3
    lda #$00
    sta $0EBC,X
    sta $0EBD,X
    lda $0042,Y
    sta entity_flags
    sta WSYNC
    and #$08
    beq loc_B596
    lda #$10
loc_B596:
    clc
    adc entity_type_tbl,Y
    tay
    lda $BB40,Y
    sta temp_ptr_lo
    inx
    beq loc_B5E3
    ldy entity_slot
    lda entity_size_tbl,Y
    sta SIZEP3
    lda $000C,Y
    sta render_end
    sta WSYNC
    lda $050B,Y
    tay
    lda sprite_offset_tbl,Y
    sta sprite_base
    lda render_end
    clc
    adc sprite_height_tbl,Y
    sec
    sbc render_x
    inx
    beq loc_B5E3
    tay
    bmi loc_B562
loc_B5CB:
    lda (temp_ptr_lo),Y
    sta $0EBC,X
    sta WSYNC
    bit entity_flags
    bpl loc_B5DD
    lda $B4E5,Y
    sta COLPF0
loc_B5DD:
    lda (sprite_base),Y
    sta COLPM3
    inx
loc_B5E3:
    beq dli_end_zone
    dey
    bpl loc_B5CB
    ldy entity_slot
    ; DLI collision read start: after each entity zone, read P/M collision registers to record bullet-hit-enemy (M2PL bit 3) and related hits.
    lda M2PL
    and #$08
    beq loc_B5F3
    ; DLI collision read end: final stores into collision_flags $1A-$1E before moving to the next entity zone.
    sty $1C
loc_B5F3:
    sta WSYNC
    bit entity_flags
    bpl loc_B602
    lda $0B
    sta COLPF0
    jmp loc_B60A
loc_B602:
    lda P3PL
    lsr
    bcc loc_B60A
    sty $1F
loc_B60A:
    inx

dli_end_zone:
    ; DLI end-zone: read P/M collision registers (P2PL, P3PF, M0PL, M0PF, P1PF, P2PF, M2PF) into collision_flags $1A-$1E and dli_sprite_mask $1F-$22; clear HITCLR.
    beq loc_B66B
    lda P2PL
    lsr
    bcc loc_B615
    sty $1E
loc_B615:
    and #$04
    beq loc_B61B
    sty $1D
loc_B61B:
    lda HPOSM3
    lsr
    sta WSYNC
    eor $0042,Y
    and #$01
    eor $0042,Y
    sta $0042,Y
    inx
    beq loc_B66B
    lda SIZEP0
    and #$04
    beq loc_B639
    sty $21
loc_B639:
    lda HPOSP0
    and #$03
    beq loc_B642
    sty $22
loc_B642:
    lda HPOSM1
    sta WSYNC
    and #$02
    beq loc_B64E
    sty $20
loc_B64E:
    lda HPOSM2
    lsr
    bcc loc_B656
    sty $1B
loc_B656:
    lda HPOSP2
    and #$03
    beq loc_B65F
    sty $1A
loc_B65F:
    sta HITCLR
    iny
    inx
    beq loc_B669
    jmp loc_B565
loc_B669:
    stx $0A
loc_B66B:
    sta WSYNC
    stx HPOSP1
    stx HPOSP0
    stx HPOSM0
    stx COLPF3
    lda #$3E
    sta DMACTL
    lda #$1E
    ; DLI river-zone: COLPM1 = $1E (P1 becomes visible here), COLPF0/1/2 set to river-color scheme. This is where the bullet actually shows up.
    sta COLPM1
    ldx #$1E
    stx COLPF0
    lda #$8E
    sta COLPF1
    ldy $79
    beq loc_B697
    ldy player_number
    beq loc_B697
    tax
loc_B697:
    ; River-zone DLI: COLPM1 = $1E. This makes M1 (the bullet) visible at river scanlines — M1 inherits COLPM1 as its color even though P1 itself has no bitmap. The baseline COLPM1 = 0 store at $B541 keeps M1 hidden above the river.
    stx COLPF2
    lda fuel_level
    beq loc_B6AD
    ora invuln_flags
    bmi loc_B6AD
    lda frame_counter
    and #$30
    bne loc_B6AD
    lda #$06
    sta COLPF0,Y
loc_B6AD:
    lda #$08
    sta CHBASE
    lda #$04
    sta WSYNC
    sta PRIOR
    lda #$06
    sta COLBK
    ldy entity_slot
    cpy #$0B
    bcs loc_B727
    lda #$0A
    ldx $0A
    sta $0A
    beq loc_B727
    lda HPOSM3
    lsr
    eor $0042,Y
    and #$01
    eor $0042,Y
    sta $0042,Y
    lda SIZEP0
    and #$04
    beq loc_B6E5
    sty $21
loc_B6E5:
    lda HPOSP0
    and #$03
    beq loc_B6EE
    sty $22
loc_B6EE:
    lda HPOSP2
    and #$03
    beq loc_B6F7
    sty $1A
loc_B6F7:
    lda HPOSM2
    lsr
    bcc loc_B6FF
    sty $1B
loc_B6FF:
    lda P2PL
    lsr
    bcc loc_B707
    sty $1E
loc_B707:
    and #$04
    beq loc_B70D
    sty $1D
loc_B70D:
    lda P3PL
    lsr
    bcc loc_B715
    sty $1F
loc_B715:
    lda HPOSM1
    and #$02
    beq loc_B71E
    sty $20
loc_B71E:
    lda M2PL
    and #$08
    beq loc_B727
    sty $1C
loc_B727:
    jmp XITVBV

dli_status_bar:
    ; DLI status bar: colors for score/fuel display area. HPOSP3 = dli_status_x ($27), then WSYNC loop advances HPOSP2/P3 together for the lives-icon display using color table $B48D.
    ldx #$00
    lda #$3F
    sta DMACTL
    sta WSYNC
    lda #$0C
    sta COLPF0
    stx COLBK
    lda #$06
    sta CHBASE
    lda pal_flag
    bne loc_B74B
    sta WSYNC
    sta WSYNC
loc_B74B:
    ldx #$03
    stx SIZEP3

dli_player_positions:
    ; DLI status-bar tail: position player sprites, clear collision regs, fall through to XITVBV return.
    lda dli_status_x
    ; HPOSP3 write in the status-bar DLI, driven from dli_status_x ($27).
    sta HPOSP3
    adc #$15
    tay
    ldx #$06
loc_B75A:
    sta WSYNC
    sty HPOSP2
    lda $B48D,X
    sta COLPM3
    sta COLPM2
    dey
    dex
    bpl loc_B75A
    sta WSYNC
    sta HITCLR

dli_exit:
    ; JMP XITVBV — return from interrupt through the OS VBI exit.
    jmp XITVBV
dl_template_data:
    ; Display list template: copied to $3F00 at cold start ($A05D). 138 bytes including blank-lines, LMS instructions, and the JVB wrap.
    ; Display list template: copied to $3F00 at cold start ($A05D). 138 bytes including blank-lines, LMS instructions, and the JVB wrap.
    .byte $70,$F0,$30,$4E,$10,$46,$00,$10,$00,$06,$06,$A0,$00,$56,$4B,$B8
    .byte $41,$00,$3F,$50,$46,$00,$10,$30,$06,$06,$D0,$20,$56,$4B,$B8,$41
    .byte $00,$3F,$0C,$0C,$0C,$0C,$0C,$0C,$48,$48,$48,$48,$48,$48,$0C,$0C
    :$4 .byte $0C
    :$6 .byte $48
    .byte $B0,$B0,$B0,$80,$80,$B0,$B0,$2A,$2A,$2A,$B0,$B0,$B0,$B0,$16,$B0
    .byte $B0,$2A,$2A,$2A,$AC,$AC,$9C,$9C,$8C,$8C,$A8,$A8,$32,$32,$00,$00
    .byte $00,$00,$22,$14,$14,$12,$12,$14,$14,$18,$18,$12,$12,$14,$14,$12
    :$8 .byte $22
    .byte $18,$28,$3A,$4A,$6A,$7A,$9A,$AA,$BA,$CA,$EA,$FC,$01,$80,$00,$80
    .byte $01,$00,$02,$00,$04,$00,$FF,$FF
sprite_shape_data:
    ; Sprite shape data ($B800-$BBFF): entity graphics, 1024 bytes of player/missile bitmaps for ships, helicopters, jets, fuel depots, balloons and bridges.
    .byte $AA,$FF,$AA,$55,$6A,$BF,$EA,$95,$5A,$AF,$FA,$A5,$56,$AB,$FE,$A9
    .byte $AA,$FF,$AA,$AA,$AA,$BF,$EA,$AA,$AA,$AF,$FA,$AA,$AA,$AB,$FE,$AA
    :$18 .byte $00
    :$5 .byte $3F
    .byte $07,$21,$2D,$25,$00,$2F,$36,$25,$32,$3F,$3F,$3F,$3F,$3F,$00,$00
    :$18 .byte $00
    .byte $12,$09,$16,$05,$12,$00,$12,$01,$09,$04,$19,$1A,$00,$22,$39,$00
    .byte $03,$21,$32,$2F,$2C,$00,$13,$28,$21,$37,$00,$00,$00,$00,$03,$2F
    .byte $30,$39,$32,$29,$27,$28,$34,$00,$3B,$3C,$3D,$3A,$18,$3B,$3C,$3D
    .byte $3E,$00,$00,$00,$1B,$1C,$1D,$1E,$1F,$00,$00,$00,$00,$00,$00,$00
    :$11 .byte $00
    .byte $10,$32,$25,$33,$33,$00,$13,$14,$01,$12,$14,$00,$2B,$25,$39,$00
    .byte $00,$00,$00,$15,$33,$25,$00,$2A,$2F,$39,$33,$34,$29,$23,$2B,$00
    .byte $3B,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00
    :$8 .byte $00
    .byte $1B,$1C,$1D,$1E,$1F,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00
    .byte $00,$FF,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00
    :$E .byte $00
    .byte $14,$48,$54,$2A,$89,$2A,$A4,$1A,$61,$8C,$23,$C4,$19,$29,$92,$8A
    .byte $45,$94,$8A,$2A,$91,$4A,$24,$12,$02,$14,$22,$11,$44,$82,$48,$81
    .byte $14,$42,$88,$22,$48,$14,$41,$24,$88,$22,$14,$41,$22,$84,$10,$08
    .byte $00,$00,$00,$04,$12,$48,$12,$44,$08,$10,$44,$10,$24,$02,$24,$40
    .byte $12,$48,$20,$04,$40,$00,$00,$00,$10,$FE,$C6,$DE,$DE,$DE,$DE,$FE
    .byte $C6,$DE,$CE,$DE,$C6,$FE,$C6,$D6,$D6,$D6,$FE,$DE,$DE,$CE,$DE,$C6
    .byte $7C,$7C,$FC,$FE,$FF,$78,$30,$10,$10,$30,$38,$4F,$FF,$C6,$80,$0E
    .byte $04,$8E,$FF,$FF,$9F,$0E,$04,$1C,$07,$0E,$04,$8E,$FF,$FF,$9F,$0E
    .byte $04,$07,$1C,$3E,$3F,$7F,$FF,$1E,$0C,$08,$08,$0C,$1C,$F2,$FF,$63
    .byte $01,$70,$20,$71,$FF,$FF,$F9,$70,$20,$38,$E0,$70,$20,$71,$FF,$FF
    .byte $F9,$70,$20,$E0,$38,$00,$24,$24,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF
    :$7 .byte $FF
    .byte $00,$00,$18,$18,$18,$24,$24,$24,$24,$24,$3C,$7E,$7E,$FF,$FF,$FF
    .byte $FF,$FF,$FF,$7E,$7E,$3C,$00,$00,$18,$1F,$18,$7E,$FF,$FF,$2A,$80
    .byte $2A,$00,$00,$18,$1F,$18,$7E,$FF,$FF,$54,$01,$54,$00,$00,$18,$F8
    .byte $18,$7E,$FF,$FF,$2A,$80,$2A,$00,$00,$18,$F8,$18,$7E,$FF,$FF,$54
    .byte $01,$54,$00,$00,$00,$00,$00,$00,$00,$00,$00,$18,$00,$00,$00,$00
    :$4 .byte $00
    :$4 .byte $18
    :$7 .byte $00
    .byte $18,$3C,$3C,$3C,$3C,$18,$00,$00,$00,$00,$00,$00,$18,$3C,$3C,$3C
    .byte $3C,$3C,$3C,$18,$00,$00,$00,$00,$00,$18,$3C,$3C,$7E,$7E,$7E,$7E
    .byte $3C,$3C,$18,$00,$00,$00,$00,$3C,$7E,$7E,$FF,$FF,$FF,$FF,$FF,$FF
    .byte $7E,$7E,$3C,$08,$08,$08,$1C,$3E,$7F,$7F,$6B,$49,$08,$1C,$3E,$2A
    .byte $00,$08,$08,$08,$18,$3C,$3E,$3E,$2E,$0A,$08,$18,$3C,$2E,$02,$08
    .byte $08,$08,$0C,$1E,$3E,$3E,$3A,$28,$08,$0C,$1E,$3A,$20,$00,$01,$FF
    .byte $FF,$00,$01,$00,$00,$01,$00,$01,$FF,$00,$FF,$FF,$FF,$00,$01,$01
    .byte $00,$01,$FF,$FF,$00,$00,$FF,$00,$00,$00,$01,$01,$00,$00,$00,$01
    .byte $02,$05,$04,$07,$06
fuel_gauge_ptrs:
    ; Fuel-gauge row pointer table: 2-byte pointers indexed by fuel_gauge_idx ($3B) via ASL/TAX at $AF0F-$AF18 (14 bytes through $BAC4).
    .byte $00,$B9,$15,$BA,$1F,$BA,$2B,$BA,$38,$BA,$46,$BA,$55,$BA
player_color_cycle:
    ; Player color cycle table: 8 bytes through $BACC, cycled through to animate the player color.
    .byte $1E,$2E,$3E,$4E,$6E,$7E,$9E,$BE,$00,$B9,$00,$B9,$4C,$B9,$34,$B9
    .byte $1C,$B9,$34,$B9,$4C,$B9,$81,$BA,$73,$BA,$65,$BA,$0B,$0B,$02,$FF
    .byte $0B,$20,$14,$36,$00,$01,$7C,$00,$FE,$01,$FF,$FF,$11,$00,$0D,$01
    :$9 .byte $00
    .byte $04,$01,$00,$00,$00,$00,$00,$00,$00,$00,$00,$04,$80
score_value_table:
    ; BCD score-value table per entity type (25 bytes through $BB23). Indexed by entity_type when crediting points at $A547.
    :$7 .byte $00
    .byte $10,$06,$06,$15,$15,$06,$03,$50,$08,$75,$0D,$08,$0D,$08,$07,$0D
    .byte $08,$0C
entity_type_trans:
    ; Entity type transition table (12 bytes through $BB2F) - maps old entity type to next when recycling slots.
    .byte $00,$01,$03,$04,$05,$06,$00,$07,$09,$08,$0B,$0A
sprite_offset_tbl:
    ; Sprite offset tables (48 bytes through $BB5F): per-entity offset into the shape data at $B800.
    :$7 .byte $00
    .byte $C4,$B0,$B0,$BA,$BA,$E0,$CA,$CF,$98,$00,$00,$4C,$34,$1C,$34,$4C
    .byte $85,$8B,$95,$8B,$95,$D5,$7D,$C1,$65,$00,$00,$4C,$34,$1C,$34,$4C
    .byte $A7,$AD,$B7,$AD,$B7,$D5,$9F,$C1,$65
sprite_height_tbl:
    ; Sprite height tables (48 bytes through $BB8F): per-entity scanline span used by the DLI multiplexer.
    .byte $17,$05,$05,$05,$05,$05,$05,$05,$09,$09,$09,$09,$13,$07,$13,$17
    .byte $00,$1A,$00,$00,$00,$00,$00,$00,$10,$00,$00,$5B,$00,$00,$00,$00
    .byte $00,$00,$50,$00,$00,$E7,$E1,$ED,$E5,$C7,$C8,$C9,$C8,$CA,$00,$83
    .byte $84,$85,$86,$00,$00,$00,$91,$00,$00,$00,$00,$D1,$00,$CB,$CC,$CD
    .byte $CC,$CE,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$01
    .byte $03,$00,$05,$08,$03,$04,$05,$06,$06,$08,$08,$0A,$08,$08,$0A,$0A
    .byte $0A,$0A,$03,$05,$06,$08,$08,$08,$0A,$0A,$0A,$0A,$07,$04,$08,$06
    .byte $06,$04,$05,$04,$04,$04,$04,$04,$04,$04,$05,$04,$05,$04,$07,$08
    .byte $06,$08,$09,$0A,$17,$16,$15,$14,$13,$15,$13,$12,$14,$15,$0C,$0C
    .byte $14,$0E,$0E,$16,$16,$1E,$06,$BC,$2A,$BC,$5A,$BC,$BE,$BC,$BE,$BC
terrain_shape_data:
    ; Terrain bank shape templates (768 bytes through $BEFF): bank contours and obstacle masks for procedural river generation.
    .byte $12,$BD,$12,$BD,$C2,$BD,$0A,$AA,$AA,$42,$AA,$A9,$42,$A2,$A5,$42
    .byte $A2,$95,$50,$82,$95,$50,$0A,$55,$50,$2A,$55,$54,$29,$55,$54,$25
    .byte $55,$55,$25,$55,$55,$15,$55,$55,$15,$55,$AA,$AA,$AA,$BF,$6A,$AA
    .byte $AA,$FD,$5A,$AA,$AA,$F5,$56,$AA,$EA,$F5,$55,$AA,$FB,$D5,$55,$6A
    .byte $FF,$D5,$55,$5A,$BF,$D5,$55,$5A,$BF,$55,$55,$56,$AF,$55,$55,$55
    .byte $AD,$55,$55,$55,$6D,$55,$55,$55,$5D,$55,$AA,$BA,$AE,$AA,$AA,$6A
    .byte $BA,$AE,$AA,$A9,$5A,$BE,$AF,$AA,$A9,$5A,$BF,$AB,$AA,$A9,$5A,$AF
    .byte $AB,$EA,$A5,$5A,$AF,$AB,$EA,$A5,$56,$AB,$AA,$EA,$A5,$56,$AB,$AA
    .byte $FA,$A5,$56,$A9,$AA,$BA,$A5,$56,$A9,$AA,$BE,$A5,$56,$A5,$6A,$A6
    .byte $95,$56,$A5,$6A,$A6,$95,$55,$95,$5A,$96,$95,$55,$95,$5A,$95,$95
    .byte $55,$55,$5A,$95,$95,$55,$55,$5A,$95,$95,$55,$55,$56,$55,$55,$55
    .byte $55,$56,$55,$55,$55,$55,$56,$55,$55,$55,$55,$56,$55,$55,$AA,$AA
    .byte $AA,$AA,$AA,$00,$6A,$AA,$AA,$A2,$AA,$01,$5A,$AA,$AA,$82,$A8,$01
    .byte $5A,$A8,$AA,$0A,$A8,$05,$56,$A8,$28,$0A,$A8,$05,$56,$AA,$08,$2A
    .byte $A0,$15,$56,$AA,$80,$6A,$00,$55,$55,$AA,$81,$6A,$01,$55,$55,$AA
    .byte $01,$58,$05,$55,$55,$6A,$05,$58,$15,$55,$55,$58,$15,$54,$55,$55
    .byte $55,$58,$15,$54,$55,$55,$55,$54,$55,$55,$55,$55,$55,$54,$55,$55
    :$5 .byte $55
    .byte $95,$55,$55,$55,$55,$6A,$55,$6A,$A9,$55,$55,$55,$55,$AA,$AA,$AA
    .byte $AA,$AF,$EA,$AB,$55,$AA,$AA,$AA,$AA,$AF,$AA,$AB,$D5,$6A,$AA,$AA
    .byte $AA,$AF,$AA,$AB,$F5,$6A,$AA,$AA,$AA,$BF,$AA,$AB,$FF,$5A,$AA,$FA
    .byte $AA,$BE,$AA,$AF,$FF,$5A,$AA,$FA,$AA,$BE,$AA,$AF,$FF,$5A,$AB,$EA
    .byte $AA,$FA,$AA,$AF,$FF,$56,$AF,$EA,$AA,$FA,$AA,$AF,$FD,$55,$AF,$AA
    .byte $AA,$EA,$AA,$AF,$FD,$55,$BD,$AA,$AB,$EA,$AA,$AF,$F5,$55,$75,$AA
    .byte $AB,$AA,$AA,$BF,$F5,$55,$75,$6A,$AB,$AA,$AA,$BF,$F5,$55,$55,$6A
    .byte $A9,$AA,$AA,$BF,$D5,$55,$55,$5A,$A5,$6A,$AA,$BF,$55,$55,$55,$5A
    .byte $95,$5A,$AA,$BF,$55,$55,$55,$56,$55,$55,$AA,$BD,$55,$55,$55,$56
    .byte $55,$55,$5A,$BD,$55,$55,$55,$55,$55,$55,$56,$B5,$55,$55,$55,$55
    .byte $55,$55,$55,$B5,$55,$55,$55,$55,$55,$55,$55,$75,$55,$55,$55,$55
    .byte $55,$55,$69,$55,$55,$55,$55,$55,$5A,$AA,$55,$5A,$AA,$95,$55,$55
    .byte $55,$AA,$AA,$AA,$AA,$AA,$AA,$AA,$AA,$55,$55,$5A,$AA,$AA,$AA,$AA
    .byte $AA,$AA,$A9,$55,$55,$56,$AA,$AA,$AA,$AA,$AA,$AA,$A5,$55,$55,$56
    :$6 .byte $AA
    .byte $A5,$55,$55,$55,$AA,$AA,$AA,$AA,$AA,$AA,$15,$55,$55,$55,$6A,$AA
    :$4 .byte $AA
    .byte $15,$55,$55,$55,$6A,$AA,$AA,$AA,$AA,$A8,$15,$55,$55,$55,$5A,$AA
    .byte $AA,$AA,$AA,$A8,$55,$55,$55,$55,$5A,$AA,$AA,$AA,$AA,$A8,$55,$55
    .byte $55,$55,$55,$AA,$AA,$AA,$AA,$A0,$55,$55,$55,$55,$55,$6A,$AA,$AA
    .byte $AA,$A2,$AA,$A0,$01,$55,$55,$6A,$AA,$AA,$AA,$82,$AA,$A0,$05,$55
    .byte $55,$5A,$AA,$A8,$2A,$8A,$AA,$A0,$15,$55,$55,$56,$AA,$AA,$2A,$8A
    .byte $AA,$80,$15,$55,$55,$56,$AA,$AA,$0A,$0A,$AA,$80,$55,$55,$55,$56
    .byte $AA,$AA,$8A,$2A,$AA,$81,$55,$55,$55,$55,$AA,$AA,$80,$2A,$AA,$01
    :$4 .byte $55
    .byte $7E,$AA,$80,$AA,$AA,$05,$55,$55,$55,$55,$5F,$AA,$80,$AA,$AA,$05
    :$4 .byte $55
    .byte $57,$AA,$80,$6A,$A8,$15,$55,$55,$55,$55,$57,$EA,$81,$5A,$A8,$15
    :$5 .byte $55
    .byte $EA,$81,$56,$A8,$55,$55,$55,$55,$55,$55,$FA,$81,$55,$A0,$55,$55
    :$4 .byte $55
    .byte $7A,$85,$55,$61,$55,$55,$55,$55,$55,$55,$7E,$85,$55,$59,$55,$55
    :$4 .byte $55
    .byte $5E,$15,$55,$55,$55,$55,$55,$55,$55,$55,$5F,$15,$55,$55,$55,$55
    :$4 .byte $55
    .byte $57,$15,$55,$55,$55,$55,$00,$00,$00,$00,$00,$60,$60,$C0,$74,$26
    .byte $27,$25,$24,$00,$00,$00,$40,$C0,$C0,$40,$40,$00,$00,$00,$00,$00
    .byte $00,$01,$03,$06,$0C,$00,$2F,$61,$ED,$A9,$E9,$29,$2D,$00,$F0,$11
    .byte $53,$56,$5C,$58,$50,$00,$FE,$80,$3A,$A2,$BA,$8A,$BA,$00,$00,$00
    .byte $E9,$AD,$AF,$AB,$E9,$00,$00,$E7,$B5,$E5,$B7,$B5,$E4,$00,$00,$3B
    .byte $92,$92,$12,$92,$BB,$00,$00,$8E,$D8,$50,$56,$DA,$8E,$00,$00,$E0
    .byte $80,$C0,$80,$80,$E0,$00,$7F,$4C,$4C,$4C,$4C,$40,$40,$4F,$FF,$00
    :$6 .byte $00
    .byte $FF,$08,$08,$48,$C0,$43,$46,$EC,$FF,$19,$19,$19,$19,$01,$01,$F1
    .byte $48,$48,$4E,$48,$48,$4F,$40,$7F,$00,$00,$00,$00,$00,$00,$00,$FF
    .byte $18,$37,$61,$C7,$04,$07,$00,$FF,$81,$81,$E1,$81,$81,$81,$01,$FF
    .byte $00,$00,$EE,$A2,$EE,$88,$8E,$00,$00,$3C,$66,$66,$66,$66,$3C,$00
    .byte $00,$00,$E4,$AC,$E4,$84,$8E,$00,$00,$00,$AE,$A4,$E4,$A4,$AE,$00
    .byte $08,$08,$1C,$3E,$6B,$49,$1C,$2A,$00,$3C,$66,$0C,$18,$30,$7E,$00
    .byte $00,$18,$38,$18,$18,$18,$7E,$00,$00,$3C,$66,$3E,$06,$0C,$38,$00
    .byte $00,$3C,$66,$3C,$66,$66,$3C,$00,$00,$7E,$0C,$18,$0C,$66,$3C,$00
    :$5 .byte $00
    .byte $18,$18,$00,$96,$04,$8C,$10,$8E,$0C,$7C,$20,$86,$14,$6C,$30,$FF
    :$7 .byte $FF
vec_cold:
    ; Vector table: cold=$A000 ($BFFA/$BFFB), warm=$8000 ($BFFC/$BFFD), IRQ=$A000 ($BFFE/$BFFF). Referenced by the relocator at $4086 for cold-start and IRQ setup.
    .byte $00,$A0,$00,$80
vec_irq:
    .byte $00,$A0
