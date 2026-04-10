; =============================================================
; Segment 3: $52E7-$852F — Game code + data: game engine, AI, terrain, sound
; =============================================================
; XEX load range: $52E7-$852F (12873 bytes)

    org $52E7

level_init:
    ; LEVEL SETUP — Initializes level: loads terrain seed, configures difficulty parameters, resets enemy/pilot state
    lda #$06
    sta COLOR3
    lda #$02
    sta $08CC
    jsr build_screen_table
    jsr patch_display_list
    lda #$00
    sta zp_random_work
    lda #$00
    sta zp_memcpy_dst_lo
    lda #$10
    sta zp_memcpy_dst_hi
    lda #$0F
    sta zp_memcpy_len_lo
    lda #$08
    sta zp_memcpy_len_hi
    jsr memcpy_block
    lda #$D8
    sta zp_memcpy_dst_lo
    lda #$08
    sta zp_memcpy_dst_hi
    lda #$06
    sta zp_memcpy_len_lo
    lda #$00
    sta zp_memcpy_len_hi
    jsr memcpy_block
    jsr setup_gameplay_display
    lda #$4B
    sta VVBLKI
    lda #$83
    sta $0223
    jsr enable_display
    lda #$F6
    sta VDSLST
    lda #$60
    sta $0201
    lda #$20
    sta DLISTL
    lda #$31
    sta DLISTH
    lda #$96
    sta zp_screen_ptr_lo
    lda #$62
    sta zp_screen_ptr_hi
    lda #$31
    sta zp_screen_ptr2_lo
    lda #$0B
    sta zp_screen_ptr2_hi
    jsr render_fill_7D18
    lda #$14
    sta GPRIOR
    sta PRIOR
    ldy #$56
    lda #$00
    sta ATRACT
    sta $088B
loc_5369:
    sta $0C31,y
    sta $0D31,y
    dey
    bpl loc_5369
    sta SIZEM
    ldy #$03
    tya
loc_5378:
    sta SIZEP0,y
    dey
    bpl loc_5378
    lda #$2D
    sta HPOSP0
    sta HPOSP2
    lda #$BE
    sta zp_temp
    sta HPOSP3
    jsr wait_1
    lda #$3F
    sta DMACTL
    lda #$03
    sta GRACTL
    lda #$00
    sta PCOLR0
    lda #$06
    sta COLOR4
    lda #$88
    sta zp_random_work
    jsr pmg_util_7CC7
    jsr enable_display
    lda #$E1
    sta VDSLST
    lda #$60
    sta $0201
    lda #$00
    sta DLISTL
    lda #$30
    sta DLISTH
    jsr terrain_init_strips
    lda $061E
    bne loc_53CD
    jmp loc_54F6
loc_53CD:
    jsr clear_hud_line
    ldy #$04
loc_53D2:
    lda $5FB0,y
    sta $08C2,y
    dey
    bpl loc_53D2
    lda #$1A
    jsr util_fill_5C7C
    jsr render_random_perturb_5BF2
    ldx $DE
loc_53E5:
    stx $0612
    jsr wait_2
    dex
    cpx #$1F
    bne loc_53E5
    jsr wait_n_frames
    lda #$00
    sta $0612
    ldx #$0B
loc_53FA:
    txa
    jsr util_fill_5C7C
    jsr wait_1
    inx
    cpx #$0F
    bne loc_53FA
    jsr util_copy_5988
    lda game_event_flag
    beq loc_5416
    ldy #$00
    sty game_event_flag
    jsr render_copy_4AE7
loc_5416:
    jsr render_loop_59B4
    jsr render_util_6215
    sta $BA
    lda #$08
    sta $08BA
loc_5423:
    ldy $BA
    jsr render_loop_5D1D
    dec $BA
    bpl loc_5423
    lda #$07
    jsr set_viewport_offset
    ldy #$00
loc_5433:
    ldx #$0E
loc_5435:
    txa
    sta $08CD,y
    jsr wait_n_frames
    dex
    bpl loc_5435
    iny
    cpy #$06
    bne loc_5433
    jsr pmg_util_7CC7
    lda #$03
    sta frame_counter
    sta $27A3
    ldy #$90
loc_5451:
    sty $08D2
    jsr wait_n_frames
    iny
    cpy #$9B
    bne loc_5451
    jsr render_fill_5C9C
    ldy #$05
loc_5461:
    lda $62A5,y
    sta $08CD,y
    dey
    bpl loc_5461
    lda #$01
    sta $08DD
    sta $08BA
    sta $06CE
    jsr render_util_6215
    sta $BA
    lda #$7E
    sta zp_memcpy_len_lo
    lda #$2A
    sta zp_memcpy_len_hi
    sta zp_memcpy_dst_hi
    jsr clear_hud_line
    sta zp_memcpy_dst_lo
loc_5489:
    ldy $BA
    bne loc_5498
    lda #$FF
    sta $08DD
    lda $08D1
    sta $0612
loc_5498:
    lda $08DE
    beq loc_5498
    lda #$00
    sta $08DE
    jsr render_copy_5CBE
    inc $08BA
    lda $08BA
    cmp #$07
    bne loc_54B4
    lda #$01
    sta $08BA
loc_54B4:
    dec $BA
    bpl loc_5489
    lda #$01
    sta $08DD
    jsr render_util_6215
    ldy #$08
    sty $08BA
    lda #$00
loc_54C7:
    sta $0C87,y
    sta $0D87,y
    dey
    bne loc_54C7
    lda #$C0
    sta $0612
loc_54D5:
    lda $08DD
    bne loc_54D5
    jsr render_proc_59E1
    ldy #$00
    lda #$FF
loc_54E1:
    jsr wait_1
    sta $0C88,y
    sta $0D88,y
    iny
    cpy #$08
    bne loc_54E1
    tya
    jsr set_viewport_offset
    jsr build_screen_table
loc_54F6:
    jsr sound_init
    jsr sound_thrust
    jsr wait_5
    lda #$07
    sta $08BB
    lda #$88
    sta zp_random_work
    jsr pmg_util_7CC7
    jsr calc_5B0C
    jsr bit_extract_5BB3
    jsr patch_display_list
    jsr timing_helper_59A6
    lda #$FF
    sta $061A
    lda $061E
    bne loc_552A
    inc $061E
    jsr wait_60
    jmp loc_574E
loc_552A:
    lda $063A
    bne loc_5532
    jmp loc_55EC
loc_5532:
    sec
    sbc lives_or_fuel
    sta $061A
    bpl loc_554C
    ldy lives_or_fuel
    sec
    tya
    sbc $063A
    sta lives_or_fuel
    lda $063A
    jmp loc_5550
loc_554C:
    lda lives_or_fuel
    tay
loc_5550:
    sta $08BC
    tya
    jsr wait_frames_7E0F
    ldy #$0C
    lda #$78
    sta QTEMP
    jsr render_copy_4AFA
loc_5560:
    lda #$00
    sta score_lo
    lda #$05
    sta score_hi
    jsr timing_update_score_7DF6
    dec $08BC
    bne loc_5560
    lda $061A
    bmi loc_55E1
    beq loc_5598
    jsr wait_60
    jsr wait_frames_7E0F
    lda #$7C
    sta QTEMP
loc_5583:
    lda #$10
    sta score_hi
    ldx #$10
    jsr entity_load_state
    jsr wait_5
    jsr timing_update_score_7DF6
    dec $061A
    bne loc_5583
loc_5598:
    jsr wait_60
    lda $0621
    bne loc_55E4
    lda #$2A
    sta QTEMP
    ldy #$83
    jsr render_copy_4AFA
    lda #$16
    sta $0656
    lda #$97
    sta $0657
    jsr sound_pickup
loc_55B6:
    lda slot_available
    bne loc_55B6
    lda game_timer
    sta zp_memcpy_dst_lo
    cmp $0606
    bcc loc_55C8
    sta $0606
loc_55C8:
    jsr timing_proc_7DDF
loc_55CB:
    lda #$02
    sta $08BC
loc_55D0:
    lda zp_memcpy_len_lo
    sta score_hi
    jsr wait_frames_7E04
    dec $08BC
    bne loc_55D0
    dec zp_memcpy_dst_lo
    bne loc_55CB
loc_55E1:
    jsr wait_60
loc_55E4:
    jsr clear_hud_line
    lda #$00
    sta $063A
loc_55EC:
    lda $061A
    bpl loc_5611
    lda game_timer
    cmp #$04
    bcs loc_5605
    lda #$01
    sta $08D6
    lda #$03
    sta $08D7
    jmp loc_560E
loc_5605:
    sta $08D7
    sec
    sbc #$02
    sta $08D6
loc_560E:
    jmp loc_56EC
loc_5611:
    lda game_timer
    sta $08D6
    inc $08D6
    clc
    adc #$03
    cmp #$64
    bcc loc_5623
    lda #$63
loc_5623:
    sta $08D7
loc_5626:
    ldy #$00
    sty zp_random_work
    lda #$4F
    sta $BA
    lda #$44
    sta AUDC2
    lda #$52
    sta $08D4
    lda #$01
    sta $08D5
loc_563D:
    lda $5975,y
    sta frame_counter
    jsr sound_play_effect
    lda $BA
    sta $08DB
    jsr render_copy_5DEB
    lda #$00
    sta $08DB
    dec $BA
    inc zp_random_work
    ldy zp_random_work
    cpy #$13
    bne loc_563D
    jsr wait_n_frames
    lda #$53
    sta $08DB
loc_5665:
    ldx #$05
loc_5667:
    lda $08DB
    cmp #$3E
    bne loc_568B
    lda #$56
    sta $08DB
    cpx #$03
    bne loc_568A
    lda #$08
    sta $08BB
    jsr bit_extract_5BB3
    dec $08BB
    inc game_timer
    jsr bit_extract_5BB3
    ldx #$03
loc_568A:
    dex
loc_568B:
    bne loc_5667
    lda #$56
    sta $08DB
loc_5692:
    lda $08DB
    cmp #$0F
    bne loc_5692
    sta $BA
    lda #$00
    sta $08DB
    lda CONSOL
    and #$02
    bne loc_56B4
    lda #$56
    sta $08DB
    ldy game_timer
    cpy $08D7
    bcc loc_5665
loc_56B4:
    lda #$FF
    sta $08D5
loc_56B9:
    ldy $BA
    lda $5978,y
    sta frame_counter
    sty $08DB
    jsr render_copy_5DEB
    lda #$00
    sta $08DB
    jsr sound_play_effect
    dec $BA
    bne loc_56B9
    inc frame_counter
    jsr wait_n_frames
    lda #$55
    jsr render_copy_5E1A
    asl frame_counter
    jsr wait_n_frames
    jsr patch_display_list
    lda #$00
    sta AUDC2
loc_56EC:
    lda game_mode
    bne loc_574E
loc_56F1:
    lda ATRACT
    bpl loc_56F8
    jmp loc_3C34
loc_56F8:
    jsr sound_explosion
    lda CONSOL
    and #$02
    bne loc_5743
    sta ATRACT
    jsr sound_laser
    lda game_timer
    cmp $08D7
    bcc loc_5740
    ldy $0612
loc_5712:
    jsr wait_2
    sty $0612
    dey
    cpy #$BF
    bne loc_5712
    lda #$08
    sta $08BB
    jsr calc_5B33
    jsr bit_extract_5BB3
    lda $08D6
    sta game_timer
    dec $08BB
    jsr wait_60
    jsr calc_5B33
    jsr bit_extract_5BB3
    jsr timing_helper_59A6
    jmp loc_5743
loc_5740:
    jmp loc_5626
loc_5743:
    lda CONSOL
    and #$01
    sec
    sbc TRIG0
    beq loc_56F1
loc_574E:
    jsr sound_laser
    ldy #$00
    lda game_mode
    bne loc_576B
    lda game_timer
    cmp $061F
    sta $061F
    bne loc_5769
    inc $0621
    jmp loc_577D
loc_5769:
    bcs loc_5777
loc_576B:
    sty game_ready_flag
    sty game_active_flag
    sty game_phase_timer
    sty $0620
loc_5777:
    sty $0621
    jsr bit_extract_7E1B
loc_577D:
    ldx #$1D
    jsr entity_load_state
    jsr math_copy_4504
    jsr display_fill_5F9F
    ldy #$05
    lda #$3A
loc_578C:
    sta $08CD,y
    sec
    sbc #$02
    dey
    bpl loc_578C
    lda #$00
    sta score_digit
    jsr render_loop_432A
    jsr util_copy_5988
    jsr render_loop_59B4
    ldx #$01
    jsr entity_load_state
    lda #$1F
loc_57AA:
    cmp $0680
    bne loc_57AA
    lda #$00
    sta $06E2
    lda #$8C
    sta zp_ptr_lo
    lda #$17
    sta zp_ptr_hi
    lda #$BA
    sta zp_column_x
    lda #$17
    sta zp_proj_x_hi
    lda #$02
    sta $08CB
    lda #$7F
    sta $08CA
    lda #$2B
    sta $08DA
    lda #$07
    jsr set_viewport_offset
    lda #$7F
    sta $0686
    lda #$09
    sta $0678
    lda #$FB
    sta $0688
    lda #$FF
    sta ent_param_a
    lda #$01
    jsr render_proc_8E97
loc_57F1:
    jsr wait_10
    inc $067A
    inc ent_state
    lda $067A
    cmp #$03
    bne loc_5822
    lda #$FF
    sta $06CC
    sta $06CE
    sta ent_param_f
    lda #$01
    sta $06E8
    sta $06EA
    sta ent_param_h
    lda #$00
    sta $06F6
    sta $06F8
    sta ent_param_i
loc_5822:
    dec $0678
    jsr render_proc_8E9D
    lda $0678
    cmp #$04
    bne loc_57F1
    lda #$64
loc_5831:
    cmp $0686
    bne loc_5831
    ldy #$05
    lda #$00
    sta ent_flags,y
    jsr entity_swap
    lda #$1D
loc_5842:
    cmp $0686
    bne loc_5842
    lda #$FF
    sta $06E8
    lda #$64
loc_584E:
    cmp $0688
    bne loc_584E
    jsr render_util_6215
    lda #$00
    sta $08BA
    jsr set_viewport_offset
    ldy #$0C
    lda #$0F
    sta ent_flags,y
    lda #$B4
    sta ent_state,y
    jsr entity_swap
    lda #$02
    sta $0678
    lda #$00
    sta $06EA
    sta ent_param_h
    lda #$01
    jsr render_proc_8E97
    lda #$01
    sta $08D8
    lda #$0F
    sta frame_counter
loc_5889:
    jsr wait_n_frames
    lda $0679
    cmp #$08
    beq loc_5899
    dec $0679
    jmp loc_589E
loc_5899:
    lda #$01
    sta frame_counter
loc_589E:
    lda $08D8
    bne loc_5889
    lda #$04
    sta $0679
    jsr set_viewport_offset
    ldy #$0C
    lda #$65
    sta ent_state,y
    jsr entity_swap
    ldy #$0B
    lda #$01
    sta ent_flags,y
    lda #$0E
    sta ent_state,y
    jsr entity_swap
    lda #$01
    ldy #$03
loc_58C8:
    sta SIZEP0,y
    dey
    bpl loc_58C8
    lda #$38
    sta HPOSP0
    lda #$62
    sta zp_temp
    lda #$8E
    sta HPOSP2
    lda #$B8
    sta HPOSP3
    jsr display_fill_5F9F
    ldx #$2C
loc_58E6:
    lda row_addr_lo_blend,x
    sta zp_memcpy_dst_lo
    lda row_addr_hi_blend,x
    sta zp_memcpy_dst_hi
    ldy #$2D
    lda #$00
loc_58F4:
    sta (zp_memcpy_dst_lo),y
    dey
    bpl loc_58F4
    dex
    bpl loc_58E6
    jsr render_table_lookup_7D89
    lda #$00
    sta $DD
    sta $0612
    lda #$10
    sta $3157
    lda #$18
    sta $3158
    jsr enable_display
    lda #$F6
    sta VDSLST
    lda #$60
    sta $0201
    lda #$20
    sta DLISTL
    lda #$31
    sta DLISTH
    jsr util_copy_5FB5
    lda #$7F
    sta $08D9
    jsr game_logic_helper_5F0D
    lda #$00
loc_5934:
    ldx #$03
loc_5936:
    sta PCOLR0,x
    dex
    bpl loc_5936
    jsr wait_2
    clc
    adc #$01
    cmp #$0D
    bne loc_5934
    lda #$30
    jsr util_copy_598A
loc_594B:
    lda $08D9
    cmp #$04
    bpl loc_594B
    lda #$00
    sta $14
loc_5956:
    lda $14
    cmp #$02
    bcc loc_596F
    lda #$00
    sta $14
    jsr calc_5FD8
    lda $1002
    cmp #$FF
    bne loc_596F
    lda #$00
    sta $08D9
loc_596F:
    lda $08D9
    bne loc_5956
    rts
    .byte $06,$04,$03,$03,$02,$02,$01,$02,$01,$02,$01,$01,$02 ; $5975
    :4 .byte $01                    ; $5982 (4 bytes)
    .byte $02,$01                                          ; $5986
util_copy_5988:
    ; Copy loop — indexed load/store transfer; Table access: $0751, $0752, $07A7, $07A8; 23 instructions, 1 loop
    lda #$2E
util_copy_598A:
    ; Copy loop — indexed load/store transfer; Table access: $0751, $0752, $07A7, $07A8; 22 instructions, 1 loop
    sta zp_memcpy_dst_lo
    lda #$00
    sta zp_memcpy_len_lo
    lda #$10
    sta zp_memcpy_len_hi
    jmp build_line_table
build_screen_table:
    ; BUILD SCREEN LINES — Constructs screen line address table: 45 entries for ANTIC mode D rows (96-byte stride)
    lda #$2E
    sta zp_memcpy_dst_lo
    lda #$00
    sta zp_memcpy_len_lo
    lda #$20
    sta zp_memcpy_len_hi
    jmp build_line_table
.proc timing_helper_59A6
    ; Calls: wait_2; 7 instructions, 1 loop
    ldy #$C2
loc_59A8:
    jsr wait_2
    sty $0612
    iny
    cpy #$C9
    bne loc_59A8
    rts
.endp

render_loop_59B4:
    ; Calls: render_util_6215, render_proc_5A0A, render_table_lookup_5A9D, pixel_shift_index; Table access: $624C; 33 instructions, 2 loops
    jsr render_util_6215
    lda #$01
    sta $08BA
    bmi loc_59DE
loc_59BE:
    ldy $08C1
    lda $624C,y
    sta $08BC
    jsr render_proc_5A0A
    inc $08BA
    lda $08BA
    cmp #$07
    bne loc_59D9
    lda #$01
    sta $08BA
loc_59D9:
    dec $08C1
    bpl loc_59BE
loc_59DE:
    dec $08BA
.proc render_proc_59E1
    ; Calls: render_table_lookup_5A9D, pixel_shift_index, pixel_shift_index, pixel_shift_index; 16 instructions, 1 loop
    lda #$55
    sta $08B9
    clc
    lda $08BE
    adc #$01
    sta $08C1
loc_59EF:
    jsr render_table_lookup_5A9D
    lda $08BD
    jsr pixel_shift_index
    lda $08BE
    jsr pixel_shift_index
    lda $08C1
    jsr pixel_shift_index
    dec $08B9
    bpl loc_59EF
    rts
.endp

.proc render_proc_5A0A
    ; Calls: pmg_loop_5A2B, render_loop_5A6E; Table access: $5AC3; 13 instructions, 1 loop
    lda $08BA
    tay
    ora $5AC3,y
    sta $BA
loc_5A13:
    jsr pmg_loop_5A2B
    jsr render_loop_5A6E
    dec $08BD
    inc $08BE
    inc $08BF
    dec $08C0
    dec $08BC
    bne loc_5A13
    rts
.endp

pmg_loop_5A2B:
    ; Table access: $0751, $07A7; 32 instructions, 1 loop
    ldy $08BF
    lda row_addr_lo_blend,y
    sta zp_ptr_lo
    lda row_addr_hi_blend,y
    sta zp_ptr_hi
    ldy $08C0
    lda row_addr_lo_blend,y
    sta zp_random_work
    lda row_addr_hi_blend,y
    sta $B9
    lda $08BD
    lsr
    sta zp_column_x
    lda $08BE
    lsr
    sta zp_proj_x_hi
    bcs loc_5A58
    dec zp_proj_x_hi
    jmp loc_5A5A
loc_5A58:
    inc zp_column_x
loc_5A5A:
    sec
    lda zp_proj_x_hi
    tay
    sbc zp_column_x
    sta FR1
    lda $BA
loc_5A64:
    sta (zp_ptr_lo),y
    sta (zp_random_work),y
    dey
    dec FR1
    bpl loc_5A64
    rts
.proc render_loop_5A6E
    ; Calls: pixel_shift_index, render_table_lookup_5AB8; Table access: $0751, $07A7; 21 instructions, 1 loop
    lda $08C0
    sta zp_depth_accum
    sec
    lda $08BF
    sbc $08C0
    sta FR1
loc_5A7C:
    ldy zp_depth_accum
    lda row_addr_lo_blend,y
    sta zp_ptr_lo
    lda row_addr_hi_blend,y
    sta zp_ptr_hi
    lda $08BD
    jsr pixel_shift_index
    lda $08BE
    lsr
    tay
    jsr render_table_lookup_5AB8
    inc zp_depth_accum
    dec FR1
    bpl loc_5A7C
    rts
.endp

render_table_lookup_5A9D:
    ; Table access: $0751, $07A7; 6 instructions
    ldy $08B9
.proc render_table_lookup_5AA0
    ; Table access: $0751, $07A7; 5 instructions
    lda row_addr_lo_blend,y
    sta zp_ptr_lo
    lda row_addr_hi_blend,y
    sta zp_ptr_hi
    rts
.endp

render_proc_5AAB:
    ; Table access: $5AC3, $5AD5; 12 instructions
    lda $08B8
pixel_shift_index:
    ; Table access: $5AC3, $5AD5; 11 instructions
    lsr
    tay
    lda $08BA
    bcc loc_5AB7
    adc #$08
loc_5AB7:
    tax
.proc render_table_lookup_5AB8
    ; Table access: $5AC3, $5AD5; 5 instructions
    lda (zp_ptr_lo),y
    ora $5AC3,x
    and $5AD5,x
    sta (zp_ptr_lo),y
    rts
.endp

    .byte $00,$10,$20,$30,$40,$50,$60,$70,$80,$00,$01,$02,$03,$04,$05,$06 ; $5AC3
    .byte $07,$08,$0F,$1F,$2F,$3F,$4F,$5F,$6F,$7F,$8F,$F0,$F1,$F2,$F3,$F4 ; $5AD3
    .byte $F5,$F6,$F7,$F8                                  ; $5AE3
render_proc_5AE7:
    ; Calls: render_proc_5A0A; Table access: $624C; 15 instructions
    .byte $AC,$C1,$08,$C0,$06,$30,$0C,$B9,$4C,$62,$8D,$BC,$08,$20,$0A,$5A ; $5AE7
    .byte $4C,$FF,$5A                                      ; $5AF7
loc_5AFA:
    .byte $A9,$00,$8D,$D1,$08                              ; $5AFA
loc_5AFF:
    .byte $CE,$C1,$08,$18,$AD,$C1,$08,$69,$01,$8D,$D8,$08,$60 ; $5AFF
calc_5B0C:
    ; Arithmetic with shifts; Calls: render_table_lookup_5A9D, pixel_shift_index, pixel_shift_index, pixel_shift_index; Table access: $6260; 47 instructions, 2 loops
    lda #$54
    sta $08B9
    lda #$00
    sta $08BA
loc_5B16:
    jsr render_table_lookup_5A9D
    lda #$15
    jsr pixel_shift_index
    lda #$2E
    jsr pixel_shift_index
    lda #$47
    jsr pixel_shift_index
    sec
    lda $08B9
    sbc #$08
    sta $08B9
    bpl loc_5B16
.proc calc_5B33
    ; Arithmetic with shifts; Calls: math_loop_5B71; Table access: $6260; 31 instructions, 1 loop
    lda #$2E
    sta $08B9
    ldx game_mode
    beq loc_5B3F
    ldx #$05
loc_5B3F:
    lda #$1B
    sta $08BD
    stx $C6
    ldy #$05
    sty zp_render_scratch
loc_5B4A:
    lda #$00
    sta zp_proj_y_hi
    ldx $C6
    lda $6260,x
    asl
    asl
    asl
    sta zp_depth_accum
    rol zp_proj_y_hi
    clc
    lda zp_depth_accum
    adc #$00
    sta zp_depth_accum
    lda zp_proj_y_hi
    adc #$E0
    sta zp_proj_y_hi
    jsr math_loop_5B71
    inc $C6
    dec zp_render_scratch
    bne loc_5B4A
    rts
.endp

.proc math_loop_5B71
    ; Calls: render_table_lookup_5A9D, render_proc_5AAB; 30 instructions, 2 loops
    lda $08BB
    sta $08BA
    jsr render_table_lookup_5A9D
    lda #$07
    sta $08CA
loc_5B7F:
    lda $08BD
    sta $08B8
    ldy $08CA
    lda (zp_depth_accum),y
loc_5B8A:
    asl
    pha
    bcc loc_5B91
    jsr render_proc_5AAB
loc_5B91:
    inc $08B8
    pla
    bne loc_5B8A
    sec
    lda zp_ptr_lo
    sbc #$2E
    sta zp_ptr_lo
    lda zp_ptr_hi
    sbc #$00
    sta zp_ptr_hi
    dec $08CA
    bpl loc_5B7F
    clc
    lda $08BD
    adc #$08
    sta $08BD
    rts
.endp

bit_extract_5BB3:
    ; Bit extraction/packing via shifts and rotates; Calls: calc_7DCB, math_loop_5B71, render_table_lookup_5A9D, render_proc_5AAB; 61 instructions, 2 loops
    lda #$38
    sta $08B9
    lda #$27
    sta $08BD
    lda #$E0
    sta zp_proj_y_hi
    lda game_timer
    cmp #$63
    bcc loc_5BCD
    lda #$63
    sta game_timer
loc_5BCD:
    ldy game_mode
    beq loc_5BD5
    lda $0607
loc_5BD5:
    jsr calc_7DCB
    pha
    lsr
    and #$78
    clc
    adc #$80
    sta zp_depth_accum
    jsr math_loop_5B71
    pla
    and #$0F
    asl
    asl
    asl
    clc
    adc #$80
    sta zp_depth_accum
    jmp math_loop_5B71
.proc render_random_perturb_5BF2
    ; Reads POKEY RANDOM for randomization; Reads: SKRES; Calls: util_random_perturb_5C69, render_table_lookup_5AA0, util_random_perturb_5C69, pixel_shift_index; 54 instructions, 3 loops
    lda #$04
    sta $08BA
    lda #$06
    sta FR1
    lda #$07
    sta zp_column_x
    lda #$00
    sta zp_depth_accum
loc_5C03:
    lda zp_column_x
    ora #$07
    lsr
    lsr
    sta $08BC
loc_5C0C:
    lda RANDOM
    and zp_column_x
    sta $08B8
    lda RANDOM
    and zp_column_x
    sta $08B9
    cmp zp_depth_accum
    bcs loc_5C27
    lda $08B8
    cmp zp_depth_accum
    bcc loc_5C0C
loc_5C27:
    lda #$2A
    sta zp_proj_y_hi
    lda $08B9
    jsr util_random_perturb_5C69
    tay
    jsr render_table_lookup_5AA0
    lda #$2F
    sta zp_proj_y_hi
    lda $08B8
    jsr util_random_perturb_5C69
    jsr pixel_shift_index
    dec $08BC
    bne loc_5C0C
    clc
    lda zp_column_x
    adc #$07
    sta zp_column_x
    clc
    lda zp_depth_accum
    adc #$06
    sta zp_depth_accum
    inc $08BA
    lda $08BA
    cmp #$07
    bne loc_5C64
    lda #$01
    sta $08BA
loc_5C64:
    dec FR1
    bne loc_5C03
    rts
.endp

util_random_perturb_5C69:
    ; Reads POKEY RANDOM for randomization; Reads: SKRES; 10 instructions
    sta zp_random_work
    lda zp_proj_y_hi
    ldy RANDOM
    bpl loc_5C78
    sec
    sbc zp_random_work
    jmp loc_5C7B
loc_5C78:
    clc
    adc zp_random_work
loc_5C7B:
    rts
.proc util_fill_5C7C
    ; Fill loop — indexed store of constant value; Table access: $08CD; 5 instructions, 1 loop
    ldy #$05
loc_5C7E:
    sta $08CD,y
    dey
    bpl loc_5C7E
    rts
.endp

dat_5C85:
    .byte $20,$E4,$5D                                      ; $5C85
.proc terrain_init_strips
    ; INIT TERRAIN STRIPS — Initializes terrain strip rendering buffers for the 3D viewport columns
    ldy #$08
    lda #$FF
loc_5C8C:
    sta $0C87,y
    sta $0D87,y
    sta $0E87,y
    sta $0F87,y
    dey
    bne loc_5C8C
    rts
.endp

.proc render_fill_5C9C
    ; Fill loop — indexed store of constant value; Writes to screen memory; 17 instructions, 2 loops
    ldy #$2F
    lda #$88
loc_5CA0:
    sta $2F74,y
    dey
    bne loc_5CA0
    ldy #$7E
loc_5CA8:
    lda #$74
    sta $300A,y
    sta $308B,y
    lda #$2F
    sta $300B,y
    sta $308C,y
    dey
    dey
    dey
    bpl loc_5CA8
    rts
.endp

render_copy_5CBE:
    ; Copy loop — indexed load/store transfer; Writes to screen memory; Calls: render_loop_5A6E; 65 instructions, 3 loops
    lda $624C,y
    sta $08BC
    pha
    ldx zp_memcpy_len_hi
    ldy zp_memcpy_len_lo
loc_5CC9:
    lda row_addr_lo_blend,x
    sta $300A,y
    lda row_addr_hi_blend,x
    sta $300B,y
    dey
    dey
    dey
    dex
    dec $08BC
    bne loc_5CC9
    stx zp_memcpy_len_hi
    pla
    sta $08BC
    ldx zp_memcpy_dst_hi
    ldy zp_memcpy_dst_lo
loc_5CE8:
    lda row_addr_lo_blend,x
    sta $308B,y
    lda row_addr_hi_blend,x
    sta $308C,y
    iny
    iny
    iny
    inx
    dec $08BC
    bne loc_5CE8
    stx zp_memcpy_dst_hi
    ldy $BA
    sec
    lda zp_memcpy_len_lo
    sbc $624C,y
    sbc $624C,y
    sbc $624C,y
    sta zp_memcpy_len_lo
    clc
    lda zp_memcpy_dst_lo
    adc $624C,y
    adc $624C,y
    adc $624C,y
    sta zp_memcpy_dst_lo
.proc render_loop_5D1D
    ; Calls: render_loop_5A6E; Table access: $624C; 21 instructions, 1 loop
    lda $624C,y
    sta $08BC
    cpy #$00
    bne loc_5D2D
    inc $08BC
    inc $08BC
loc_5D2D:
    sta zp_proj_y_hi
loc_5D2F:
    jsr render_loop_5A6E
    dec $08BD
    inc $08BE
    dec $08BC
    bne loc_5D2F
    clc
    lda $08BF
    adc zp_proj_y_hi
    sta $08BF
    sec
    lda $08C0
    sbc zp_proj_y_hi
    sta $08C0
    rts
.endp

sub_5D50:
    ; Calls: render_copy_5DC8, render_copy_5DAC, draw_5D91, draw_5D76; 14 instructions
    dec $08DA
    beq loc_5D5E
    jsr render_copy_5DC8
    jsr render_copy_5DAC
    jmp loc_5D63
loc_5D5E:
    lda #$08
    sta $08DC
loc_5D63:
    ldy $08CB
    jsr draw_5D91
    sty $08CB
    ldx $08CA
    jsr draw_5D76
    stx $08CA
    rts
.proc draw_5D76
    ; Writes to screen memory; Table access: $300A; 15 instructions
    lda zp_ptr_hi
    sta $300A,x
    dex
    lda zp_ptr_lo
    sta $300A,x
    sec
    lda zp_ptr_lo
    sbc #$2E
    sta zp_ptr_lo
    lda zp_ptr_hi
    sbc #$00
    sta zp_ptr_hi
    dex
    dex
    rts
.endp

.proc draw_5D91
    ; Writes to screen memory; Table access: $3089; 15 instructions
    lda zp_column_x
    sta $3089,y
    iny
    lda zp_proj_x_hi
    sta $3089,y
    clc
    lda zp_column_x
    adc #$2E
    sta zp_column_x
    lda zp_proj_x_hi
    adc #$00
    sta zp_proj_x_hi
    iny
    iny
    rts
.endp

.proc render_copy_5DAC
    ; Copy loop — indexed load/store transfer; Writes to screen memory; Table access: $3009, $300C; 13 instructions, 1 loop
    ldx #$01
    cpx $08CA
    beq loc_5DC7
loc_5DB3:
    lda $300C,x
    sta $3009,x
    inx
    lda $300C,x
    sta $3009,x
    inx
    inx
    cpx $08CA
    bne loc_5DB3
loc_5DC7:
    rts
.endp

.proc render_copy_5DC8
    ; Copy loop — indexed load/store transfer; Writes to screen memory; Table access: $3087, $308A; 13 instructions, 1 loop
    ldy #$80
    cpy $08CB
    beq loc_5DE3
loc_5DCF:
    lda $3087,y
    sta $308A,y
    dey
    lda $3087,y
    sta $308A,y
    dey
    dey
    cpy $08CB
    bne loc_5DCF
loc_5DE3:
    rts
.endp

patch_display_list:
    ; Copy loop — indexed load/store transfer; Writes to screen memory; 38 instructions, 2 loops
    lda #$00
    sta $08DB
    beq loc_5DEE
render_copy_5DEB:
    ; Copy loop — indexed load/store transfer; Writes to screen memory; 35 instructions, 2 loops
    dec $08DB
loc_5DEE:
    lda #$0A
    sta $C6
    lda #$30
    sta zp_render_scratch
    lda #$56
    sta zp_color_shade
render_copy_5DFA:
    ; Copy loop — indexed load/store transfer; Writes to screen memory; 28 instructions, 2 loops
    ldx $08DB
    ldy #$00
loc_5DFF:
    lda row_addr_lo_blend,x
    sta ($C6),y
    iny
    bne loc_5E09
    inc zp_render_scratch
loc_5E09:
    lda row_addr_hi_blend,x
    sta ($C6),y
    iny
    iny
    inx
    cpx zp_color_shade
    bne loc_5DFF
    lda $08DB
    beq loc_5E31
render_copy_5E1A:
    ; Copy loop — indexed load/store transfer; Writes to screen memory; Table access: $3007, $3008, $300A, $300B; 12 instructions, 1 loop
    sta zp_depth_accum
    ldy #$FF
loc_5E1E:
    lda $3007,y
    sta $300A,y
    lda $3008,y
    sta $300B,y
    dey
    dey
    dey
    dec zp_depth_accum
    bne loc_5E1E
loc_5E31:
    rts
.proc render_table_lookup_5E32
    ; Table access: $0C87, $0D87; 9 instructions
    dec $08DC
    sec
    lda #$08
    sbc $08DC
    tay
    lda #$00
    sta $0C87,y
    sta $0D87,y
    rts
.endp

loc_5E45:
    .byte $A9,$75,$20,$CE,$5E,$8D,$C5,$08,$CD,$C6          ; $5E45
    php
    bne loc_5E53
    rts
loc_5E53:
    sta $08C6
    cmp #$90
    bcc loc_5E5D
    jsr render_proc_5AE7
loc_5E5D:
    lda $08D2
    pha
    lda $08D1
    sta $08D2
    lda $08D0
    sta $08D1
    lda $08CF
    sta $08D0
    lda $08CE
    sta $08CF
    lda $08CD
    sta $08CE
    pla
    sta $08CD
    lda $08DD
    bpl loc_5E8E
    lda $08D1
    sta $0612
loc_5E8E:
    ldy #$0C
    clc
    lda ent_state,y
    adc $06CE
    bne loc_5E9B
    lda #$FF
loc_5E9B:
    sta ent_state,y
    jmp entity_swap
sub_5EA1:
    ; Calls: game_logic_proc_5EE9, render_proc_5A0A, render_table_lookup_9308, render_proc_9344; Table access: $066D, $067B, $0707, $624C; 78 instructions
    lda #$7E
    jsr game_logic_proc_5EE9
    sta $08C5
    cmp $08C6
    beq loc_5ECD
    sta $08C6
    cmp #$14
    bcs loc_5EC2
    tay
    .byte $8D                     ; $5EB6
    .byte $DD                     ; $5EB7
loc_5EB8:
    .byte $08                     ; $5EB8
    lda $624C,y
    sta $08BC
    jsr render_proc_5A0A
loc_5EC2:
    lda $08DD
    beq loc_5ECD
    inc $08DE
    jmp loc_5E5D
loc_5ECD:
    rts
.proc arith_5ECE
    ; 11 instructions
    clc
    adc $08C2
    sta $08C2
    lda #$00
    adc $08C3
    sta $08C3
    adc $08C4
    sta $08C4
    lda #$00
    adc $08C5
    rts
.endp

.proc game_logic_proc_5EE9
    ; 14 instructions
    sec
    sta $08D3
    lda $08C2
    sbc $08D3
    sta $08C2
    lda $08C3
    sbc #$00
    sta $08C3
    lda $08C4
    sbc $08C3
    sta $08C4
    lda $08C5
    sbc #$00
    rts
.endp

.proc game_logic_helper_5F0D
    ; Calls: game_logic_table_lookup_5F5C; 5 instructions, 1 loop
    ldy #$59
loc_5F0F:
    jsr game_logic_table_lookup_5F5C
    dey
    bne loc_5F0F
    rts
.endp

copy_5F16:
    ; Copy loop — indexed load/store transfer; Calls: arith_5ECE, random_select, random_select, random_select; 38 instructions, 1 loop
    cmp #$04
    bmi loc_5F3A
    lda #$FF
    jsr arith_5ECE
    cmp #$64
    bne loc_5F2A
    lda #$02
    sta $08D9
    bne loc_5F3A
loc_5F2A:
    sta $08C5
    sec
    sbc $08C6
    bne loc_5F34
    rts
loc_5F34:
    lda $08C5
    sta $08C6
loc_5F3A:
    ldy #$00
loc_5F3C:
    lda $0C33,y
    sta $0C32,y
    lda $0D33,y
    sta $0D32,y
    lda $0E33,y
    sta $0E32,y
    lda $0F33,y
    sta $0F32,y
    iny
    cpy #$59
    bne loc_5F3C
    dec $08CC
.proc game_logic_table_lookup_5F5C
    ; Calls: random_select, random_select, random_select, random_select; Table access: $0C32, $0D32, $0E32, $0F32; 9 instructions
    jsr random_select
    sta $0C32,y
    jsr random_select
    sta $0D32,y
    jsr random_select
    sta $0E32,y
    jsr random_select
    sta $0F32,y
    rts
.endp

random_select:
    ; Reads POKEY RANDOM for randomization; Reads: SKRES; Table access: $5F8D; 11 instructions
    lda RANDOM
    and #$1F
    cmp #$01
    bpl loc_5F8A
    lda RANDOM
    and #$03
    tax
    lda dat_5F8D,x
    jmp loc_5F8C
loc_5F8A:
    lda #$00
loc_5F8C:
    rts
dat_5F8D:
    .byte $80,$20,$04,$01                                  ; $5F8D
.proc util_fill_5F91
    ; Fill loop — indexed store of constant value; Table access: $0E32, $0F32; 7 instructions, 1 loop
    ldy #$5F
    lda #$00
loc_5F95:
    sta $0E32,y
    sta $0F32,y
    dey
    bpl loc_5F95
    rts
.endp

.proc display_fill_5F9F
    ; Fill loop — indexed store of constant value; Sets color registers; Table access: $02C0, $08C2; 8 instructions, 1 loop
    lda #$00
    ldx #$03
loc_5FA3:
    sta PCOLR0,x
    sta $08C2,x
    dex
    bpl loc_5FA3
    sta $08C6
    rts
.endp

    .byte $72,$C4,$6C,$95,$95                              ; $5FB0
.proc util_copy_5FB5
    ; Copy loop — indexed load/store transfer; Table access: $088C, $088D, $626A, $626B; 16 instructions, 1 loop
    lda #$00
    sta $08C8
    sta $08C9
    sta $08C7
    ldy #$2A
loc_5FC2:
    clc
    lda $626A,y
    adc #$E0
    sta $088C,y
    lda $626B,y
    adc #$2E
    sta $088D,y
    dey
    dey
    bpl loc_5FC2
    rts
.endp

.proc calc_5FD8
    ; Arithmetic with shifts; Calls: math_loop_601D; Table access: $088C, $088D; 34 instructions, 1 loop
    inc $08C7
    clc
    lda $08C8
    adc #$18
    sta $08C8
    lda $08C9
    adc #$00
    sta $08C9
    ldx #$2A
loc_5FEE:
    lda $088C,x
    sta $BA
    sta zp_depth_accum
    lda $088D,x
    sta $BB
    sta zp_proj_y_hi
    asl
    sta zp_depth_accum
    lda #$00
    rol
    asl zp_depth_accum
    rol
    sta zp_proj_y_hi
    clc
    lda zp_depth_accum
    adc $08C9
    sta zp_depth_accum
    lda zp_proj_y_hi
    adc #$00
    sta zp_proj_y_hi
    jsr math_loop_601D
    dex
    dex
    bpl loc_5FEE
    rts
.endp

.proc math_loop_601D
    ; Calls: math_loop_607D, math_loop_607D, render_table_lookup_5A9D, loop_util_60C6; Table access: $088C, $088D; 85 instructions, 1 loop
    sec
    lda $BA
    sbc zp_depth_accum
    sta zp_random_work
    lda $BB
    sbc zp_proj_y_hi
    bpl loc_602C
    lda #$00
loc_602C:
    sta $B9
    lda zp_random_work
    sta $088C,x
    lda $B9
    sta $088D,x
    lda #$FF
    sta zp_depth_accum
    lda $BB
    cmp #$2F
    bcc loc_6044
    lda #$2E
loc_6044:
    sta zp_random_work
    jsr math_loop_607D
    lda #$AA
    sta zp_depth_accum
    dec zp_random_work
    bmi loc_607C
    sec
    lda $B9
    sbc $08C9
    bpl loc_605B
    lda #$00
loc_605B:
    sta $B9
    lda zp_random_work
    jsr math_loop_607D
    lda $B9
    sta zp_random_work
    lda #$55
    sta zp_depth_accum
    dec zp_random_work
    bmi loc_607C
    dec $B9
    bpl loc_6076
    lda #$00
    sta $B9
loc_6076:
    lda zp_random_work
    cmp #$2B
    bmi math_loop_607D
loc_607C:
    rts
.endp

math_loop_607D:
    ; Calls: render_table_lookup_5A9D, loop_util_60C6, loop_util_60C6; 38 instructions, 1 loop
    sta $08B9
    cmp $B9
    bmi loc_60C5
loc_6084:
    cmp #$00
    bpl loc_608D
    lda #$00
    sta $08B9
loc_608D:
    pha
    jsr render_table_lookup_5A9D
    txa
    lsr
    clc
    adc #$02
    sta zp_proj_y_hi
    tay
    pla
    cmp #$2B
    bcc loc_60AD
    jsr loop_util_60C6
    sec
    lda #$2F
    sbc zp_proj_y_hi
    tay
    jsr loop_util_60C6
    jmp loc_60BB
loc_60AD:
    lda zp_depth_accum
    sta (zp_ptr_lo),y
    sec
    lda #$2F
    sbc zp_proj_y_hi
    tay
    lda zp_depth_accum
    sta (zp_ptr_lo),y
loc_60BB:
    dec $08B9
    lda $08B9
    cmp $B9
    bpl loc_6084
loc_60C5:
    rts
.proc loop_util_60C6
    ; 18 instructions, 1 loop
    txa
    pha
    ldx #$04
    lda #$C0
    sta zp_column_x
    lda (zp_ptr_lo),y
loc_60D0:
    bit zp_column_x
    bne loc_60D6
    ora #$C0
loc_60D6:
    rol
    rol
    dex
    bne loc_60D0
    rol
    sta (zp_ptr_lo),y
    pla
    tax
    rts
.endp

dispatch_60E1:
    ; Indirect dispatch via pointer; Table access: $61F1, $61F2; 10 instructions
    sta $C9
    sty $CB
    lda $C8
    asl
    tay
    lda $61F1,y
    sta zp_dli_vec_lo
    lda $61F2,y
    sta zp_dli_vec_hi
    jmp (zp_dli_vec_lo)
dispatch_60F6:
    ; Indirect dispatch via pointer; Table access: $6205, $6206; 10 instructions
    sta $C9
    sty $CB
    lda $C8
    asl
    tay
    lda $6205,y
    sta zp_dli_vec_lo
    lda $6206,y
    sta zp_dli_vec_hi
    jmp (zp_dli_vec_lo)
dli_colors_a:
    ; Sets color registers; Writes: COLPM1, COLPM2, COLPM3, PRIOR; 13 instructions
    lda $08D1
    .byte $8D                     ; $610E
loc_610F:
    .byte $14                     ; $610F
    .byte $D0                     ; $6110
    lda $08D0
    sta COLPM1
    lda $08D2
    sta COLPM3
    lda #$94
    sta PRIOR
    jmp loc_4D06
    sta WSYNC
    lda $0612
    sta COLBK
    lda $08CD
    sta COLPF0
    lda $08CE
    sta COLPF1
    lda $08CF
    sta COLPF2
    jmp loc_4D06
set_colors_6143:
    ; Sets color registers; Writes: COLPF0, COLPF1, COLPF2, COLBK; 14 instructions
    ldy $DD
    lda #$2A
    sta WSYNC
    sty COLBK
    sta COLPF2
    lda #$28
    sta COLPF1
    lda #$24
    sta COLPF0
    jmp loc_4D06
    sta WSYNC
    lda #$3E
    sta HPOSM2
    lda #$3C
    sta HPOSM3
    lda #$C2
    sta HPOSM0
    lda #$C0
    sta HPOSM1
    jmp loc_4D06
update_pmg_6177:
    ; Configures player-missile graphics; Writes: HPOSM0, HPOSM3; 9 instructions
    lda #$3F
    sta HPOSM3
    lda #$BF
    sta HPOSM0
    jmp loc_4D06
dli_colors_b:
    ; Sets color registers; Writes: COLPM3, COLPF0, COLPF1, COLPF2; 13 instructions
    lda #$06
    sta COLPF1
    lda #$04
    sta COLPF0
    lda #$2C
    sta COLPF2
    lda $08D1
    sta COLPM3
    jmp loc_4D06
    sta WSYNC
    lda #$06
    sta COLPF1
    lda #$04
    .byte $8D                     ; $61A6
loc_61A7:
    .byte $16                     ; $61A7
    .byte $D0                     ; $61A8
    lda #$26
    sta COLPF3
    jmp loc_4D06
set_colors_61B1:
    ; Sets color registers; Writes: COLPM0, COLPM1, COLPF3, COLBK; 15 instructions
    lda #$04
    sta WSYNC
    sta PRIOR
    lda #$00
    sta COLBK
    lda $0612
    sta COLPM0
    sta COLPM1
    lda #$26
    sta COLPF3
    jmp loc_4D06
dli_colors_c:
    ; Writes: PRIOR; 7 instructions
    lda #$04
    sta PRIOR
    jmp loc_4D06
    sta WSYNC
    lda #$94
    sta HPOSP1
    lda $DF
    sta COLPM1
    lda #$2C
    sta COLPF2
    lda #$90
    sta COLBK
    .byte $4C                     ; $61EE
dli_table_alt:
    .byte $06                     ; $61EF
    .byte $4D                     ; $61F0
    .byte $0D,$4D,$0B,$61,$25,$61,$5D,$61                  ; $61F1
    .byte $77,$61,$84,$61,$B1,$61,$CF,$61                  ; $61F9
    .byte $D7,$61,$CE,$4D,$0D,$4D,$43,$61                  ; $6201
    .byte $5D,$61,$77,$61,$9C,$61,$CF,$61                  ; $6209
    .byte $D7,$61,$CE,$4D                                  ; $6211
.proc render_util_6215
    ; 11 instructions
    lda #$2E
    sta $08BD
    lda #$30
    sta $08BE
    lda #$2B
    sta $08BF
    lda #$2A
    sta $08C0
    lda #$13
    sta $08C1
    rts
.endp

.proc sound_play_effect
    ; Updates sound registers; Writes: AUDF2; Calls: wait_1; 14 instructions, 1 loop
    ldy frame_counter
    tya
    pha
loc_6234:
    sec
    lda $08D4
    sbc $08D5
    sta $08D4
    sta AUDF2
    jsr wait_1
    dey
    bne loc_6234
    pla
    sta frame_counter
    rts
.endp

    .byte $0B,$06,$04,$03,$02,$02,$02                      ; $624C
    :13 .byte $01                    ; $6253 (13 bytes)
    .byte $2C,$25,$36,$25,$2C,$24,$32,$2F,$29,$24,$9E,$13,$23,$12,$A8,$10 ; $6260
    .byte $53,$0F,$FD,$0D,$CE,$0C,$9F,$0B,$96,$0A,$8D,$09,$A9,$08,$C6,$07 ; $6270
    .byte $08,$07,$4B,$06,$B3,$05,$1C,$05,$AA,$04,$38,$04,$EC,$03,$A1,$03 ; $6280
    .byte $7B,$03,$55,$03,$55,$03,$09,$C3,$0E,$66,$0E,$3C,$0E,$C3,$0E,$66 ; $6290
    .byte $0E,$3C,$08,$C3,$00,$96,$94,$92,$90,$9A,$98      ; $62A0
wait_frames_62AB:
    ; Wait loop polling RTCLOK (frame sync); Writes to screen memory; Bit extraction/packing via shifts and rotates; Reads: SKRES; Calls: draw_6495, render_random_perturb_64A9, wait_frames_644B, game_logic_bcd_add_655E; Table access: $0A00; 187 instructions, 6 loops
    lda #$80
    sta terrain_vis_flag
    sta terrain_scroll_accum
    lda flight_param
    cmp #$03
    bcc loc_62EB
    lda #$00
    sta $28F9
    lda terrain_near_dist
    sta $28FA
    lda #$01
    sta $2820
    ldx $290B
    lda pilot_map,x
    cmp #$80
    bne loc_62E1
    lda $060E
    beq loc_62E4
    cmp RANDOM
    bcc loc_62E4
    jmp loc_62EB
loc_62E1:
    dec $2820
loc_62E4:
    inc viewport_state
    lda #$0C
    sta $DC
loc_62EB:
    lda game_event_flag
    beq loc_6307
    lda $14
    and #$08
    beq loc_6304
    lda $2848
    bne loc_6301
    inc $2848
    jsr render_random_perturb_64A9
loc_6301:
    jmp loc_6307
loc_6304:
    sta $2848
loc_6307:
    lda difficulty_level
    bne loc_6343
    ldy $28A3
    beq loc_6314
    jsr draw_6495
loc_6314:
    ldy flight_param
    lda #$00
    sta flight_param
    sta viewport_state
    cpy #$04
    beq loc_6328
    cpy #$03
    beq loc_6328
    rts
loc_6328:
    lda $2820
    bne loc_6338
    lda #$49
    sta $0635
    jsr game_logic_helper_652A
    jmp loc_6340
loc_6338:
    lda #$40
    sta $0635
    jsr game_logic_bcd_add_655E
loc_6340:
    jmp loc_6577
loc_6343:
    lda flight_param
    cmp #$03
    bcc loc_62EB
    cmp #$04
    bne loc_6351
    jmp loc_63D3
loc_6351:
    bcs loc_62EB
    lda #$80
    sta $28F4
    sta $28F5
    lda $28FA
    sta $290E
    lda RANDOM
    sta $2908
    lda $28AB
    cmp #$6C
    bcc loc_6374
    lda RANDOM
    jmp loc_6376
loc_6374:
    lda #$00
loc_6376:
    sta $2909
loc_6379:
    lda $28AA
    asl
    ror $2908
    lsr $2909
    lsr $290E
    bne loc_6379
    lda $28FA
    cmp #$01
    bne loc_63CC
    lda $2908
    ora #$40
    sta $2908
    lda $28F9
    bne loc_63C4
    lda $28AB
    cmp #$6C
    bcs loc_63BF
    sta $14
    lda RANDOM
    adc RANDOM
    ror
    lsr
    lsr
    adc $14
    ldy $2820
    beq loc_63B7
    adc #$20
loc_63B7:
    sta $14
    sta $2881
    inc flight_param
loc_63BF:
    lda #$FF
    jmp loc_63C9
loc_63C4:
    lda $2909
    ora #$80
loc_63C9:
    sta $2909
loc_63CC:
    lda flight_param
    cmp #$04
    bne loc_6443
loc_63D3:
    lda $2881
    beq loc_63DC
    lda $14
    bne loc_6440
loc_63DC:
    lda $2820
    beq loc_63FB
    lda $28A3
    bne loc_63F1
    lda #$00
    sta viewport_state
    jsr sound_engine_update
    jmp loc_63F8
loc_63F1:
    lda #$80
    sta $28A3
    bne loc_640A
loc_63F8:
    jmp loc_6440
loc_63FB:
    ldy $28A3
    beq loc_643D
loc_6400:
    lda $0701
    bne loc_6400
    lda #$FF
    sta $28A3
loc_640A:
    jsr timing_proc_647D
    bne loc_6412
    jmp loc_62EB
loc_6412:
    jsr clear_hud_line
    jsr wait_20
    lda $28A3
    cmp #$80
    bne loc_642F
    jsr game_logic_table_lookup_6555
    sta game_event_flag
    lda #$D8
    sta $0637
    lda #$70
    jmp loc_6434
loc_642F:
    jsr game_logic_update_score_64ED
    lda #$0A
loc_6434:
    jsr draw_6497
    inc flight_param
    jmp loc_6440
loc_643D:
    jsr wait_frames_644B
loc_6440:
    jmp loc_6448
loc_6443:
    bcs loc_6448
    jsr wait_frames_65C2
loc_6448:
    jmp loc_62EB
.proc wait_frames_644B
    ; Wait loop polling RTCLOK (frame sync); Writes to screen memory; Reads: SKRES; Calls: queue_push_flagged, wait_1; 24 instructions, 2 loops
    lda $14
    bne loc_647C
    sta $2881
    lda RANDOM
    and #$07
    cmp #$03
    bcs loc_645D
    ora #$03
loc_645D:
    tay
loc_645E:
    lda $28A3
    bne loc_6475
    ldx #$1B
    jsr queue_push_flagged
    ldx #$0F
loc_646A:
    jsr wait_1
    lda difficulty_level
    beq loc_647C
    dex
    bne loc_646A
loc_6475:
    dey
    bne loc_645E
    lda #$A5
    sta $14
loc_647C:
    rts
.endp

.proc timing_proc_647D
    ; Calls: wait_1, queue_push_flagged; 12 instructions, 2 loops
    ldy #$05
loc_647F:
    ldx #$14
loc_6481:
    jsr wait_1
    lda difficulty_level
    beq loc_6494
    dex
    bne loc_6481
    ldx #$1A
    jsr queue_push_flagged
    dey
    bpl loc_647F
loc_6494:
    rts
.endp

draw_6495:
    ; Writes to screen memory; Table access: $071B; 24 instructions
    lda #$4A
draw_6497:
    ; Writes to screen memory; Table access: $071B; 23 instructions
    sta $0635
    lda #$B4
    sta $3388
    lda #$00
    sta $28A3
    ldx #$16
    jmp queue_push_flagged
render_random_perturb_64A9:
    ; Reads POKEY RANDOM for randomization; Writes to screen memory; Reads: SKRES; Calls: queue_push_flagged, render_random_perturb_AB39; Table access: $071B; 45 instructions
    lda RANDOM
    bpl loc_64EC
    and #$0F
    beq loc_64D3
    ldy camera_view_param
    bmi loc_64BF
    ldy #$6D
    sty $0635
    sty $0637
loc_64BF:
    and #$03
    beq loc_64D3
    and #$01
    sta $2841
    ldy #$70
    sty $0635
    sty $0637
    jsr render_random_perturb_AB39
loc_64D3:
    lda ent_type
    cmp #$02
    beq loc_64E3
    clc
    lda $287F
    adc #$04
    sta $287F
loc_64E3:
    ldx #$1A
    jsr queue_push_flagged
    inx
    jmp queue_push_flagged
loc_64EC:
    rts
game_logic_update_score_64ED:
    ; BCD arithmetic — score or counter update; Calls: game_logic_get_random_6539, math_copy_4508, game_logic_table_lookup_6555, render_search_7B9E; 32 instructions
    inc $063A
    lda $0621
    bne loc_64FD
    lda $063A
    cmp $0613
    bcc loc_6500
loc_64FD:
    jsr game_logic_get_random_6539
loc_6500:
    sed
    lda display_enabled
    ldy $063C
    bne loc_650F
    sec
    sbc #$01
    jmp loc_6512
loc_650F:
    clc
    adc #$01
loc_6512:
    sta display_enabled
    cld
    bne loc_651B
    inc $063C
loc_651B:
    lda #$00
    sta score_lo
    lda #$02
    sta score_hi
    ldx #$08
    jsr math_copy_4508
game_logic_helper_652A:
    ; Calls: game_logic_table_lookup_6555, render_search_7B9E; 6 instructions
    jsr game_logic_table_lookup_6555
.proc render_helper_652D
    ; Calls: render_search_7B9E; 5 instructions
    lda $0614
    beq loc_6538
    jsr render_search_7B9E
    dec $0614
loc_6538:
    rts
.endp

.proc game_logic_get_random_6539
    ; Reads POKEY RANDOM for randomization; Writes to screen memory; Reads: SKRES; 13 instructions
    lda camera_view_param
    bmi loc_6554
    lda $284D
    bne loc_6554
    lda RANDOM
    ora #$08
    and #$3F
    ldy game_mode
    beq loc_6551
    lsr
    lsr
loc_6551:
    sta $284D
loc_6554:
    rts
.endp

.proc game_logic_table_lookup_6555
    ; Table access: $0A00; 4 instructions
    ldx $290B
    lda #$01
    sta pilot_map,x
    rts
.endp

game_logic_bcd_add_655E:
    ; BCD arithmetic — score or counter update; Calls: game_logic_table_lookup_6555; 12 instructions
    jsr game_logic_table_lookup_6555
game_logic_bcd_add_6561:
    ; BCD arithmetic — score or counter update; 11 instructions
    lda #$00
    sta score_lo
    lda #$01
    sta score_hi
.proc game_logic_bcd_add_656B
    ; BCD arithmetic — score or counter update; 7 instructions
    lda score_digit
    clc
    sed
    adc #$01
    cld
    sta score_digit
    rts
.endp

loc_6577:
    inc $2840
    ldx #$01
loc_657C:
    stx $066E
    stx $066F
    ldy #$01
    jsr queue_push
    iny
    jsr queue_push
    jsr wait_1
    ldx $066E
    inx
    cpx #$10
    bne loc_657C
    ldx #$3C
loc_6598:
    jsr wait_1
    lda RANDOM
    ora #$04
    sta $DC
    dex
    bne loc_6598
    stx $2840
    rts
draw_65A9:
    ; Writes to screen memory; Table access: $071B; 26 instructions
    lda #$FF
    sta camera_view_param
    lda game_mode
    beq loc_65B8
    lda #$02
    sta ent_type
loc_65B8:
    lda #$52
    sta $0635
    ldx #$1D
    jmp queue_push_flagged
wait_frames_65C2:
    ; Wait loop polling RTCLOK (frame sync); Writes to screen memory; Calls: queue_push_flagged, render_6671; Table access: $6785, $678D, $6795; 71 instructions, 1 loop
    lda #$00
    sta $2944
loc_65C7:
    lda #$03
loc_65C9:
    cmp $14
    bcs loc_65C9
    lda #$00
    sta $14
    ldy $2944
    cpy #$01
    beq loc_65DC
    cpy #$05
    bne loc_65E1
loc_65DC:
    ldx #$19
    jsr queue_push_flagged
loc_65E1:
    lda $6785,y
    sta zp_memcpy_len_lo
    lda $678D,y
    sta zp_memcpy_len_hi
    lda $6795,y
    sta $2902
    clc
    lda $2908
    bmi loc_660A
    adc $2906
    sta $2906
    bcc loc_6607
    inc $28AA
    bne loc_6607
    dec $28AA
loc_6607:
    jmp loc_661A
loc_660A:
    adc $2906
    sta $2906
    bcs loc_661A
    lda $28AA
    beq loc_661A
    dec $28AA
loc_661A:
    lda $28AA
    sta $28F7
    clc
    lda $2907
    adc $2909
    sta $2907
    bcc loc_6634
    lda $28AB
    beq loc_6634
    dec $28AB
loc_6634:
    lda $28AB
    sta $28F8
    sec
    lda $28F9
    bne loc_6647
    ldy $28FA
    cpy #$01
    beq loc_665B
loc_6647:
    sbc #$10
    sta $28F9
    lda $28FA
    sbc #$00
    bne loc_6658
    sta $28F9
    lda #$01
loc_6658:
    sta $28FA
loc_665B:
    jsr render_6671
    lda difficulty_level
    beq loc_666D
    inc $2944
    lda $2944
    and #$07
    bne loc_666E
loc_666D:
    rts
loc_666E:
    jmp loc_65C7
.proc render_6671
    ; Writes to screen memory; Bit extraction/packing via shifts and rotates; Calls: render_672A, render_672A, render_672A, render_672A; Table access: $679D, $67A1, $67B3, $67CB; 82 instructions, 4 loops
    lda #$06
    sta zp_memcpy_dst_hi
    lda #$00
    sta zp_memcpy_dst_lo
    sec
loc_667A:
    lda zp_memcpy_dst_lo
    sbc $28F9
    sta zp_memcpy_dst_lo
    lda zp_memcpy_dst_hi
    sbc $28FA
    sta zp_memcpy_dst_hi
    dec $28F7
    bcs loc_667A
    lda $28F7
    sta $2901
    lda #$00
    sta $28FF
    sta $2900
    lda #$0E
    sta $28FE
loc_66A0:
    lda #$00
    jsr render_672A
    dec $28FE
    bne loc_66A0
    dec $28F8
loc_66AD:
    lda $2901
    sta $28F7
    lda #$00
    sta $28FD
    sta $28FE
    jsr render_672A
loc_66BE:
    ldy $2900
    clc
    lda $28FE
    adc $2902
    tax
    lda $67A1,y
    adc $67B3,x
    tay
    lda $67CB,x
    tax
    lda (zp_memcpy_len_lo),y
    dex
    bmi loc_66E5
    lsr
    lsr
    dex
    bmi loc_66E5
    lsr
    lsr
    dex
    bmi loc_66E5
    lsr
    lsr
loc_66E5:
    and #$03
    tax
    lda $679D,x
    jsr render_672A
    clc
    lda $28FD
    adc $28F9
    sta $28FD
    lda $28FE
    adc $28FA
    sta $28FE
    cmp #$0C
    bcc loc_66BE
    lda #$00
    jsr render_672A
    lda #$00
    jsr render_672A
    dec $28F8
    clc
    lda $28FF
    adc $28F9
    sta $28FF
    lda $2900
    adc $28FA
    sta $2900
    cmp #$12
    bcc loc_66AD
    rts
.endp

.proc render_672A
    ; Writes to screen memory; Table access: $0751, $07A7, $67E3, $B63F; 46 instructions
    sta $2903
    ldx $28F7
    ldy $28F8
    cpy #$6C
    bcc loc_6781
    cpy #$97
    bcs loc_6781
    cpx #$28
    bcc loc_6781
    cpx #$D8
    bcs loc_6781
    sec
    lda #$97
    sbc $28F8
    tay
    clc
    lda row_addr_lo_blend,y
    sta zp_ptr_lo
    adc #$30
    sta zp_memcpy_dst_lo
    lda row_addr_hi_blend,y
    sta zp_ptr_hi
    adc #$00
    sta zp_memcpy_dst_hi
    txa
    lsr
    lsr
    clc
    adc #$F8
    cmp $B4
    bcs loc_6781
    tay
    txa
    and #$03
    tax
    lda $2903
    bne loc_6773
    lda (zp_ptr_lo),y
loc_6773:
    and $B63F,x
    sta zp_edge_x_arr
    lda (zp_memcpy_dst_lo),y
    and $67E3,x
    ora zp_edge_x_arr
    sta (zp_memcpy_dst_lo),y
loc_6781:
    inc $28F7
    rts
.endp

    .byte $E7,$1D,$53,$89,$E7,$1D,$53,$89,$67,$68,$68,$68,$67,$68,$68,$68 ; $6785
    :4 .byte $0C                    ; $6795 (4 bytes)
    :5 .byte $00                    ; $6799 (5 zero bytes)
    .byte $FF,$AA,$55,$00,$03,$06,$09,$0C,$0F,$12,$15,$18,$1B,$1E,$21,$24 ; $679E
    .byte $27,$2A,$2D,$30,$33                              ; $67AE
    :4 .byte $00                    ; $67B3 (4 zero bytes)
    :4 .byte $01                    ; $67B7 (4 bytes)
    :8 .byte $02                    ; $67BB (8 bytes)
    :4 .byte $01                    ; $67C3 (4 bytes)
    :4 .byte $00                    ; $67C7 (4 zero bytes)
    .byte $03,$02,$01,$00,$03,$02,$01,$00,$03,$02,$01,$00,$00,$01,$02,$03 ; $67CB
    .byte $00,$01,$02,$03,$00,$01,$02,$03,$3F,$CF,$F3,$FC  ; $67DB
    :4 .byte $00                    ; $67E7 (4 zero bytes)
    .byte $14,$00,$00,$55,$00,$00,$55,$00,$03,$AA,$F0,$0F,$BE,$30,$3F,$5F ; $67EB
    .byte $10,$00,$FF,$10,$00,$FF,$00,$00,$AA,$00,$03,$FF,$00,$0F,$AF,$00 ; $67FB
    .byte $0F,$AF,$00,$00,$2F,$00,$00,$0A,$00,$00,$0A,$00,$00,$02,$00,$00 ; $680B
    .byte $02,$80,$00,$14,$00,$00,$55,$00,$00,$55,$00,$00,$14,$00,$03,$AA ; $681B
    .byte $C0,$3F,$BE,$F0,$3C,$FF,$30,$0C,$FF,$50,$05,$AA,$00,$0F,$FF,$00 ; $682B
    .byte $3F,$FF,$00,$0A,$0F,$00,$02,$8F,$00,$02,$8F,$00,$00,$0A,$00,$00 ; $683B
    .byte $0A,$00,$00,$08,$00,$00,$08,$00,$00,$14,$00,$00,$55,$00,$00,$55 ; $684B
    .byte $00,$00,$14,$00,$3F,$AA,$F0,$3C,$BE,$F0,$F0,$F5,$00,$40,$AA,$00 ; $685B
    .byte $4F,$FF,$00,$3F,$FF,$00,$28,$0F,$C0,$08,$03,$C0,$28,$0B,$C0,$28 ; $686B
    .byte $0A,$00,$00,$08,$00,$00,$08,$00,$00,$08          ; $687B
    :5 .byte $00                    ; $6885 (5 zero bytes)
    .byte $14,$00,$00,$55,$00,$00,$55,$00,$0F,$AA,$00,$3F,$BE,$F0,$3C,$F5 ; $688A
    .byte $FC,$10,$FF,$00,$10,$FF,$00,$00,$AA,$00,$03,$FF,$C0,$0F,$FF,$C0 ; $689A
    .byte $0F,$0B,$C0,$0A,$2A,$00,$0A,$20,$00,$28,$00,$00,$28 ; $68AA
    :8 .byte $00                    ; $68B7 (8 zero bytes)
.proc sound_engine_update
    ; Wait loop polling RTCLOK (frame sync); Updates sound registers; Writes to screen memory; Reads: SKRES; Writes: AUDC1, AUDC2, AUDC3, AUDC4; Calls: pmg_draw_697D, pmg_draw_697D, pmg_draw_697D, pmg_draw_697D; Table access: $6BC4, $6BE3; 75 instructions, 2 loops
    ldx #$01
    stx $2945
    lda $6BC4,x
    sta $2949
    ldx #$02
    stx $2946
    lda $6BC4,x
    sta $294A
    ldx #$00
    stx $2947
    stx $2948
    lda $6BE3,x
    sta $294B
    inc $062A
    stx $062D
    stx $0630
    stx AUDCTL
    lda #$20
    sta $062B
    lda #$BF
    sta $062E
    lda #$D3
    sta $062F
    clc
    lda RANDOM
    and #$07
    adc #$0C
    sta $2953
    clc
    lda RANDOM
    and #$07
    adc #$21
    jsr pmg_draw_697D
    sbc #$10
    jsr pmg_draw_697D
    sbc #$0A
    jsr pmg_draw_697D
    sbc #$04
    jsr pmg_draw_697D
    sbc #$02
    sta $2952
    lda difficulty_level
    beq loc_697C
    lda RANDOM
    and #$03
    clc
    adc $060D
    bpl loc_693A
    lda #$00
loc_693A:
    sta $2950
    inc $2950
    ldx #$04
    stx $2945
    lda $6BC4,x
    sta $2949
    ldx #$05
    stx $2946
    lda $6BC4,x
    sta $294A
loc_6956:
    lda #$04
loc_6958:
    cmp $14
    bcs loc_6958
    lda #$00
    sta $14
    jsr setup_game_vectors
    lda difficulty_level
    bne loc_6956
    sta $062A
    sta AUDC1
    sta AUDC2
    sta AUDC3
    sta AUDC4
    lda #$60
    sta AUDCTL
loc_697C:
    rts
.endp

.proc pmg_draw_697D
    ; Writes to screen memory; Calls: setup_game_vectors; 5 instructions
    sta $2952
    jsr setup_game_vectors
    lda $2952
    sec
    rts
.endp

setup_game_vectors:
    ; Search loop — scans array for match; Updates sound registers; Sets color registers; Configures player-missile graphics; Writes to screen memory; Installs interrupt/event vectors; Modifies display list; Reads: SKRES; Writes: GRACTL, AUDC1, AUDC2, AUDC3; Calls: pmg_loop_6AC4, queue_push_flagged, update_sound_3FE2, render_proc_4031; 201 instructions, 4 loops
    inc $2949
    ldx $2949
    lda $6BCA,x
    bne loc_69B5
    ldx $2945
    beq loc_699C
    cpx #$04
    bcc loc_69AB
loc_699C:
    lda $2950
    bne loc_69A6
    ldx #$03
    jmp loc_69A8
loc_69A6:
    ldx #$00
loc_69A8:
    stx $2945
loc_69AB:
    lda $6BC4,x
    sta $2949
    tax
    lda $6BCA,x
loc_69B5:
    sta $294C
    inc $294A
    ldx $294A
    lda $6BCA,x
    bne loc_69E5
    ldx $2946
    beq loc_69CC
    cpx #$04
    bcc loc_69DB
loc_69CC:
    lda $2950
    bne loc_69D6
    ldx #$03
    jmp loc_69D8
loc_69D6:
    ldx #$00
loc_69D8:
    stx $2946
loc_69DB:
    lda $6BC4,x
    sta $294A
    tax
    lda $6BCA,x
loc_69E5:
    sta $294D
    inc $294B
    ldx $294B
    lda $6BF4,x
    bne loc_6A17
loc_69F3:
    lda RANDOM
    and #$0F
    tax
    inx
    cpx $2947
    beq loc_6A02
    cpx $2948
loc_6A02:
    beq loc_69F3
    lda $2947
    sta $2948
    stx $2947
    lda $6BE3,x
    sta $294B
    tax
    lda $6BF4,x
loc_6A17:
    sta $294E
    ldx $294C
    lda $6B9D,x
    sta zp_proj_y_hi
    lda $6BA5,x
    sta zp_color_shade
    ldx $294D
    lda $6B9D,x
    sta $87
    lda $6BA5,x
    sta zp_grid_x
    lda #$00
    sta $294F
    lda $294E
    cmp #$0C
    bcc loc_6A45
    sbc #$0B
    inc $294F
loc_6A45:
    tax
    lda $6BAD,x
    sta $89
    lda $6BB8,x
    sta zp_grid_row
    lda #$00
    sta zp_ptr_lo
    sta zp_ptr_hi
    lda #$FF
    sta zp_column_x
    lda #$E7
    sta zp_proj_x_hi
    ldx $2952
    clc
    lda row_addr_lo_blend,x
    adc $2953
    sta zp_cam_alt_lo
    lda row_addr_hi_blend,x
    adc #$00
    sta zp_cam_alt_hi
    clc
    lda zp_cam_alt_lo
    adc #$30
    sta $8D
    lda zp_cam_alt_hi
    adc #$00
    sta $8E
loc_6A7E:
    stx $2951
    jsr pmg_loop_6AC4
    ldx $2951
    inx
    cpx #$2C
    bcc loc_6A7E
    lda $2949
    beq loc_6A9E
    cmp #$0D
    beq loc_6A9E
    lda $294A
    beq loc_6A9E
    cmp #$0D
    bne loc_6AC3
loc_6A9E:
    dec $2950
    bpl loc_6ABA
    lda #$00
    sta $0626
    sta $062A
    sta AUDC1
    sta AUDC2
    sta AUDC3
    sta AUDC4
    jmp loc_3FC3
loc_6ABA:
    ldx #$1A
    jsr queue_push_flagged
    inx
    jmp queue_push_flagged
loc_6AC3:
    rts
.proc pmg_loop_6AC4
    ; Calls: sub_6B04, loop_util_6B1D, sub_6B35, array_op_6B64; Table access: $008F, $BE00; 32 instructions, 2 loops
    ldx #$10
    lda #$00
loc_6AC8:
    sta $8F,x
    dex
    bpl loc_6AC8
    jsr sub_6B04
    jsr loop_util_6B1D
    jsr sub_6B35
    jsr array_op_6B64
    ldy #$10
loc_6ADB:
    lda $008F,y
    sta zp_depth_accum
    tax
    lda pix_blend_cont,x
    and (zp_cam_alt_lo),y
    ora zp_depth_accum
    sta ($8D),y
    dey
    bpl loc_6ADB
    clc
    lda zp_cam_alt_lo
    adc #$60
    sta zp_cam_alt_lo
    bcc loc_6AF9
    inc zp_cam_alt_hi
    clc
loc_6AF9:
    adc #$30
    sta $8D
    lda zp_cam_alt_hi
    adc #$00
    sta $8E
    rts
.endp

.proc sub_6B04
    ; Calls: sub_6B7D; 14 instructions, 1 loop
    ldy zp_ptr_lo
    cpy #$AA
    bcc loc_6B0D
    inc zp_ptr_lo
    rts
loc_6B0D:
    ldx #$04
loc_6B0F:
    lda (zp_proj_y_hi),y
    jsr sub_6B7D
    sta $8F,x
    iny
    dex
    bpl loc_6B0F
    sty zp_ptr_lo
    rts
.endp

.proc loop_util_6B1D
    ; 14 instructions, 1 loop
    ldy zp_ptr_hi
    cpy #$AA
    bcc loc_6B26
    inc zp_ptr_hi
    rts
loc_6B26:
    ldx #$0C
loc_6B28:
    lda ($87),y
    sta $8F,x
    iny
    inx
    cpx #$11
    bcc loc_6B28
    sty zp_ptr_hi
    rts
.endp

.proc sub_6B35
    ; Calls: sub_6B7D; 26 instructions, 2 loops
    ldy zp_column_x
    cpy #$A8
    bcc loc_6B3E
    inc zp_column_x
    rts
loc_6B3E:
    lda $294F
    bne loc_6B52
    ldx #$05
loc_6B45:
    lda ($89),y
    sta $8F,x
    iny
    inx
    cpx #$0C
    bcc loc_6B45
    sty zp_column_x
    rts
loc_6B52:
    ldx #$0B
loc_6B54:
    lda ($89),y
    jsr sub_6B7D
    sta $8F,x
    iny
    dex
    cpx #$05
    bcs loc_6B54
    sty zp_column_x
    rts
.endp

.proc array_op_6B64
    ; Table access: $35CD; 14 instructions, 1 loop
    ldy zp_proj_x_hi
    cpy #$A8
    bcc loc_6B6D
    inc zp_proj_x_hi
    rts
loc_6B6D:
    ldx #$05
loc_6B6F:
    lda $35CD,y
    sta $8F,x
    iny
    inx
    cpx #$0C
    bcc loc_6B6F
    sty zp_proj_x_hi
    rts
.endp

.proc sub_6B7D
    ; 25 instructions
    rol
    rol
    ror zp_depth_accum
    ror
    ror zp_depth_accum
    rol
    rol
    rol
    ror zp_depth_accum
    ror
    ror zp_depth_accum
    rol
    rol
    rol
    ror zp_depth_accum
    ror
    ror zp_depth_accum
    rol
    rol
    rol
    ror zp_depth_accum
    ror
    lda zp_depth_accum
    ror
    rts
.endp

dat_6B9E:
    .byte $42,$EC,$96,$40,$EA,$94,$3E,$E8,$6C,$6C,$6D,$6E,$6E,$6F,$70,$70 ; $6B9E
    .byte $92,$3A,$E2,$8A,$32,$DA,$82,$2A,$D2,$7A,$22,$71,$72,$72,$73,$74 ; $6BAE
    .byte $74,$75,$76,$76,$77,$78,$00,$09,$0B,$0D,$0F,$12,$01,$01,$02,$03 ; $6BBE
    .byte $04,$05,$06,$07,$00,$05,$00,$04,$00,$08,$00,$06,$07,$00,$04,$04 ; $6BCE
    .byte $04,$05,$06,$07,$00,$00,$06,$0E,$16,$1C,$20,$23,$26,$2B,$2F,$32 ; $6BDE
    .byte $38,$3C,$3F,$42,$47,$4B,$0B,$0B,$0B,$0A,$09,$00,$08,$02,$03,$04 ; $6BEE
    .byte $04,$03,$02,$00,$09,$0D,$0E,$0F,$0F,$0E,$0D,$00,$07,$01,$06,$05 ; $6BFE
    .byte $0C,$00,$08,$09,$0A,$00,$09,$0C,$00,$09,$01,$00,$09,$02,$07,$0D ; $6C0E
    .byte $00,$07,$06,$07,$00,$08,$09,$00,$07,$01,$06,$05,$0C,$00,$08,$09 ; $6C1E
    .byte $0A,$00,$09,$0C,$00,$09,$01,$00,$09,$02,$07,$0D,$00,$07,$06,$07 ; $6C2E
    .byte $00,$09,$0A,$00,$00,$AF,$F0,$00,$00,$02,$F2,$BF,$00,$00,$0B,$EF ; $6C3E
    .byte $FA,$80,$00,$2B,$BF,$AF,$FC,$00,$3E,$BE,$FF,$FC,$00,$3F,$BE,$FF ; $6C4E
    .byte $FC,$00,$2D,$7E,$FF,$FF,$00,$0A,$BE,$FF,$FF,$00,$02,$AA,$D6,$FF ; $6C5E
    .byte $00,$00,$FA,$AA,$FF,$80,$00,$FF,$AB,$FF,$80,$00,$3F,$EF,$FF,$80 ; $6C6E
    .byte $00,$2F,$FF,$FE,$40,$00,$1B,$FF,$F9,$50,$00,$16,$AA,$A5,$50,$00 ; $6C7E
    .byte $15,$55,$55,$50,$00,$05,$55,$55,$50,$00,$05,$55,$55,$50,$00,$05 ; $6C8E
    .byte $55,$55,$54,$00,$01,$55,$55,$54,$A0,$01,$55,$55,$54,$A5,$01,$55 ; $6C9E
    .byte $55,$54,$A5,$56,$55,$55,$54,$A5,$55,$95,$55,$54,$A5,$55,$95,$55 ; $6CAE
    .byte $54,$A5,$55,$95,$55,$54,$A5,$55,$55,$55,$54,$A5,$55,$55,$55,$54 ; $6CBE
    .byte $A5,$55,$55,$55,$54,$A5,$55,$55,$55,$50,$A5,$55,$55,$55,$50,$A5 ; $6CCE
    .byte $55,$55,$55,$50,$A0,$55,$55,$55,$40,$A0,$01,$55,$55 ; $6CDE
    :11 .byte $00                    ; $6CEB (11 zero bytes)
    .byte $02,$BF,$C0,$00,$00,$0B,$CA,$FC,$00,$00,$2F,$AF,$EA,$00,$00,$AE ; $6CF6
    .byte $FE,$AF,$F0,$00,$FA,$FB,$FF,$F0,$00,$FE,$FB,$FF,$F0,$00,$B5,$FB ; $6D06
    .byte $EE,$FC,$00                                      ; $6D16
loc_6D19:
    .byte $2A,$FB,$FB,$FC,$00,$0A,$AB,$5B,$FC,$00          ; $6D19
loc_6D23:
    .byte $03,$EA,$AB,$FE,$00,$03,$FE,$AF,$FE,$00          ; $6D23
loc_6D2D:
    .byte $00,$FF,$BF,$FE,$00,$00,$BF,$FF,$F9,$00,$00,$6F,$FF,$E5,$40,$00 ; $6D2D
    .byte $5A,$AA,$95,$40,$00,$55,$55,$55,$40,$00,$15,$55,$55,$50,$00,$15 ; $6D3D
    .byte $55,$55,$50,$A0,$05,$55,$55,$50,$A5,$09,$55,$55,$50,$A5,$59,$55 ; $6D4D
    .byte $55,$50,$A5,$56,$55,$55,$50,$A5,$55,$95,$55,$50,$A5,$55,$95,$55 ; $6D5D
    .byte $50,$A5,$55,$55,$55,$50,$A5,$55,$55,$55,$50,$A5,$55,$55,$55,$50 ; $6D6D
    .byte $A5,$55,$55,$55,$50,$A5,$55,$55,$55,$50,$A5,$55,$55,$55,$40,$A0 ; $6D7D
    .byte $55,$55,$55,$00,$A0,$00,$15,$54                  ; $6D8D
    :17 .byte $00                    ; $6D95 (17 zero bytes)
    .byte $AC,$00,$00,$00,$0A,$FA,$C0,$00,$00,$2F,$AF,$FE,$00,$00,$AE,$FF ; $6DA6
    .byte $EA,$00,$00,$FA,$FA,$AF,$C0,$00,$FE,$FB,$FF,$C0,$00,$25,$FB,$EE ; $6DB6
    .byte $F0,$00,$0A,$FB,$FB,$F0,$00,$02,$AB,$5B,$F0,$00,$03,$EA,$AB,$F0 ; $6DC6
    .byte $00,$03,$FE,$AF,$F0,$00,$00,$FF,$BF,$E0,$00,$00,$BF,$FF,$94,$00 ; $6DD6
    .byte $00,$6A,$AA,$54,$00,$00,$55,$55,$55,$00,$00,$15,$55,$55,$00,$00 ; $6DE6
    .byte $15,$55,$55,$00,$A0,$05,$55,$55,$00,$A5,$09,$55,$55,$00,$A5,$59 ; $6DF6
    .byte $55,$55,$40,$A5,$59,$55,$55,$40,$A5,$56,$55,$55,$40,$A5,$56,$55 ; $6E06
    .byte $55,$40,$A5,$55,$55,$55,$40,$A5,$55,$55,$55,$40,$A5,$55,$55,$55 ; $6E16
    .byte $40,$A5,$55,$55,$55,$40,$A5,$55,$55,$55,$40,$A5,$55,$55,$55,$00 ; $6E26
    .byte $A0,$55,$55,$54,$00,$A0,$00,$15,$50              ; $6E36
    :32 .byte $00                    ; $6E3F (32 zero bytes)
    .byte $03,$F8,$00,$00,$00,$0B,$AA,$80,$00,$00,$AE,$FF,$A0,$00,$00,$EB ; $6E5F
    .byte $AA,$FC,$00,$00,$FB,$BF,$FC,$00,$00,$FB,$BE,$EF,$00,$00,$96,$A5 ; $6E6F
    .byte $BF,$00,$00,$2A,$AA,$FF,$00,$00,$3E,$AB,$FF,$00,$00,$3F,$EB,$FF ; $6E7F
    .byte $00,$00,$0F,$FB,$FE,$00,$00,$0B,$FF,$F9,$40,$00,$06,$AA,$A5,$40 ; $6E8F
    .byte $00,$05,$55,$55,$40,$A0,$05,$55,$55,$40,$A5,$09,$55,$55,$40,$A5 ; $6E9F
    .byte $59,$55,$55,$40,$A5,$59,$55,$55,$40,$A5,$59,$55,$55,$00,$A5,$59 ; $6EAF
    .byte $55,$55,$00,$A5,$59,$55,$55,$00,$A5,$55,$55,$55,$00,$A5,$55,$55 ; $6EBF
    .byte $55,$00,$A5,$55,$55,$55,$00,$A5,$55,$55,$55,$00,$A5,$55,$55,$54 ; $6ECF
    .byte $00,$A0,$55,$55,$40,$00,$A0                      ; $6EDF
    :26 .byte $00                    ; $6EE6 (26 zero bytes)
    .byte $2E,$A0,$00,$00,$03,$BE,$F8,$00,$00,$02,$BB,$EF,$C0,$00,$0D,$7B ; $6F00
    .byte $BF,$C0,$00,$3F,$BB,$BE,$F0,$00,$3A,$AA,$B6,$F0,$00,$3F,$EA,$AB ; $6F10
    .byte $F0,$00,$0F,$FA,$BF,$F0,$00,$03,$FE,$BF,$F0,$00,$00,$FF,$BF,$E0 ; $6F20
    .byte $00,$00,$BF,$FF,$90,$00,$00,$6A,$AA,$50,$00,$00,$55,$55,$50,$00 ; $6F30
    .byte $02,$55,$55,$50,$00                              ; $6F40
loc_6F45:
    .byte $02,$55,$55,$50,$00,$02,$55,$55,$50,$A0,$02,$55,$55,$50,$A5,$56 ; $6F45
    .byte $55,$55,$50,$A5,$55,$55,$55,$50,$A5,$55,$55,$55,$50,$A5,$55,$55 ; $6F55
    .byte $55,$40,$A5,$55,$55,$55,$40,$A5,$55,$55,$55,$40,$A5,$55,$55,$55 ; $6F65
    .byte $00,$A5,$55,$55,$55,$00,$A5,$55,$55,$54,$00,$A5,$55,$54,$00,$00 ; $6F75
    .byte $A5,$40,$00,$00,$00,$A0                          ; $6F85
    :4 .byte $00                    ; $6F8B (4 zero bytes)
    .byte $A0                                              ; $6F8F
    :16 .byte $00                    ; $6F90 (16 zero bytes)
    .byte $2E,$A0,$00,$00,$03,$BE,$F8,$00,$00,$02,$BB,$EF,$C0 ; $6FA0
loc_6FAD:
    .byte $00,$0D,$7B,$BF,$C0,$00,$3F,$BB,$BE,$F0,$00,$3A,$AA,$B6,$F0,$00 ; $6FAD
    .byte $3F,$EA,$AB,$F0,$00,$0F,$FA,$BF,$F0,$00,$03,$FE,$BF,$F0,$00,$00 ; $6FBD
    .byte $FF,$BF,$E0,$00,$00,$BF,$FF,$80,$00,$00,$6A,$AA,$40,$00,$00,$55 ; $6FCD
    .byte $55,$40,$00,$00,$55,$55,$40,$00,$02,$55,$55,$40,$00,$02,$55,$55 ; $6FDD
    .byte $40,$00,$02,$55,$55,$50,$00,$02,$55,$55,$50,$A0,$16,$55,$55,$50 ; $6FED
    .byte $A5,$56,$55,$55,$50,$A5,$55,$55,$55,$50,$A5,$55,$55,$55,$50,$A5 ; $6FFD
    .byte $55,$55,$55,$40,$A5,$55,$55,$55,$40,$A5,$55,$55,$55,$40,$A5,$55 ; $700D
    .byte $55,$55,$00,$A5,$55,$55,$54,$00,$A5,$55,$55,$00,$00,$A5,$55,$40 ; $701D
    .byte $00,$00,$A5,$40,$00,$00,$00,$A0                  ; $702D
    :4 .byte $00                    ; $7035 (4 zero bytes)
    .byte $A0                                              ; $7039
    :9 .byte $00                    ; $703A (9 zero bytes)
    .byte $03,$BC,$00,$00,$00,$02,$BB,$C0,$00,$00,$0D,$7B,$BC,$00,$00,$3F ; $7043
    .byte $BB,$BE,$00,$00,$3A,$AA,$B6,$00,$00,$3F,$EA,$AB,$C0,$00,$0F,$FA ; $7053
    .byte $BF,$C0,$00,$03,$FE,$BF,$C0,$00,$00,$FF,$BF,$C0,$00,$00,$BF,$FF ; $7063
    .byte $80,$00,$00,$6A,$AA,$40,$00,$00,$55,$55,$40,$00,$00,$15,$55,$50 ; $7073
    .byte $00,$00,$15,$55,$50,$00,$00,$05,$55,$50,$00,$00,$05,$55,$50,$00 ; $7083
    .byte $00,$05,$55,$54,$00,$00,$05,$55,$54,$00,$00,$01,$55,$54,$00,$A0 ; $7093
    .byte $19,$55,$54,$00,$A5,$59,$55,$54,$00,$A5,$59,$55,$54,$00,$A5,$55 ; $70A3
    .byte $55,$54,$00,$A5,$55,$55,$54,$00,$A5,$55,$55,$50,$00,$A5,$55,$55 ; $70B3
    .byte $50,$00,$A5,$55,$55,$40,$00,$A5,$55,$54,$00,$00,$A5,$55,$40,$00 ; $70C3
    .byte $00,$A5,$54,$00,$00,$00,$A5,$40,$00,$00,$00,$A0  ; $70D3
    :4 .byte $00                    ; $70DF (4 zero bytes)
    .byte $A0                                              ; $70E3
    :6 .byte $00                    ; $70E4 (6 zero bytes)
    .byte $04,$00,$00,$01,$80,$18,$00,$00,$00,$50,$60,$00,$00,$01,$85,$80 ; $70EA
    .byte $06,$10,$16,$01,$00,$01,$18,$10,$0F,$C0,$00,$50,$00,$FF,$FC,$05 ; $70FA
    .byte $90,$03,$FF,$FF,$02,$40,$0F,$FB,$FF,$C1,$00,$3F,$EF,$FF,$FC,$00 ; $710A
    .byte $FF,$BF,$EF,$FF,$50,$FE,$FF,$BF,$FF,$C4,$3A,$FE,$FF,$BF,$C9,$FF ; $711A
    .byte $EF,$FE,$FF,$F2,$FD,$5B,$FB,$FB,$F0,$3F,$6F,$EF,$EB,$F9,$0E,$FE ; $712A
    .byte $EF,$AB,$F4,$60,$3F,$EE,$BF,$D0,$02,$2F,$FF,$FF,$40,$00,$1A,$FF ; $713A
    .byte $E5,$62,$A0,$19,$59,$55,$A0,$A5,$01,$56,$65,$64,$A5,$66,$55,$65 ; $714A
    .byte $50,$A5,$96,$55,$66,$90,$A6,$55,$95,$59,$68,$9A,$55,$55,$56,$54 ; $715A
    .byte $A5,$55,$55,$59,$54,$A5,$55,$55,$55,$54,$A5,$55,$55,$55,$54,$A5 ; $716A
    .byte $55,$55,$55,$50,$A5,$55,$55,$55,$40,$A5,$55,$55,$54,$00,$A0,$55 ; $717A
    .byte $55,$40,$00,$A0                                  ; $718A
    :23 .byte $00                    ; $718E (23 zero bytes)
    .byte $FF                                              ; $71A5
    :5 .byte $00                    ; $71A6 (5 zero bytes)
    .byte $0F,$FA,$A0,$00,$FF,$00,$00,$3F,$E9,$58,$0A,$AF,$F0,$00,$3F,$A5 ; $71AB
    .byte $54,$25,$6B,$FC,$00,$3E,$96,$54,$15,$5A,$FC,$00,$0E,$56,$54,$15 ; $71BB
    .byte $96,$BC,$00,$0B,$55,$58,$15,$95,$B0,$00,$02,$D5,$60,$25,$55,$EC ; $71CB
    .byte $82,$CB,$FA,$80,$09,$57,$BF,$EB,$FF,$E8,$00,$02,$AF,$AE,$EB,$BF ; $71DB
    .byte $BF,$00,$00,$BF,$F7,$BE,$DF,$FE,$00,$00,$EB,$FF,$FF,$FF,$E8,$00 ; $71EB
    .byte $00,$3B,$FF,$FF,$FF,$EC,$00,$00,$2E,$FB,$FF,$EF,$B8,$00,$00,$AF ; $71FB
    .byte $FE,$69,$BF,$FA,$00,$15,$AB,$E6,$AA,$9B,$EA,$54,$55,$AB,$E9,$AA ; $720B
    .byte $6B,$EA,$55,$55,$AB,$D6,$AA,$97,$EA,$55,$55,$6A,$E6,$69,$9B,$A9 ; $721B
    .byte $55,$55,$6A,$FE,$69,$BF,$A9,$55,$55,$6A,$BF,$FF,$FE,$A9,$55 ; $722B
    :15 .byte $00                    ; $723A (15 zero bytes)
    .byte $FF,$00,$00,$FF,$F0,$00,$0A,$AF,$F0,$03,$FA,$BC,$00,$25,$6F,$FC ; $7249
    .byte $0F,$A9,$7F,$00,$15,$6B,$FC,$0E,$95,$5F,$00,$15,$9B,$FC,$0E,$59 ; $7259
    .byte $5F,$00,$15,$9F,$F0,$09,$59,$5F,$00,$25,$5F,$E0,$09,$55,$78,$00 ; $7269
    .byte $09,$7E,$BB,$EE,$55,$E0,$00,$02,$AF,$AE,$EE,$BA,$8C,$00,$00,$2B ; $7279
    .byte $FE,$BF,$FE,$A3,$00,$00,$FE,$EE,$BB,$FE,$FF,$00,$00,$BF,$FB,$EF ; $7289
    .byte $FF,$FB,$00,$00,$2F,$FF,$FF,$FE,$AF,$00,$00,$3B,$EF,$FE,$FE,$FC ; $7299
    .byte $00,$00,$2B,$F9,$9B,$FB,$EC,$00,$00,$AF,$D9,$9B,$BF,$EA,$00,$15 ; $72A9
    .byte $AB,$EA,$A6,$BF,$EA,$54,$55,$AB,$D6,$A9,$7F,$AA,$55,$55,$AB,$EA ; $72B9
    .byte $AA,$BF,$AA,$55,$55,$6A,$E9,$99,$BE,$A9,$55,$55,$6A,$F9,$9B,$FE ; $72C9
    .byte $A9,$55,$55,$6A,$BF,$FF,$EA,$A9,$55              ; $72D9
    :15 .byte $00                    ; $72E2 (15 zero bytes)
    .byte $0F,$F0,$03,$FF,$C0,$00,$00,$AB,$FF,$0F,$EA,$F0,$00,$02,$5B,$FF ; $72F1
    .byte $3E,$AF,$FC,$00,$01,$5B,$FF,$3A,$57,$FC,$00,$01,$6E,$FF,$39,$67 ; $7301
    .byte $FC,$00,$01,$6F,$FF,$25,$67,$FC,$00,$02,$5F,$FE,$25,$57,$E0,$00 ; $7311
    .byte $00,$97,$AB,$E9,$5F,$B0,$00,$00,$2A,$FE,$FA,$EA,$FF,$00,$00,$02 ; $7321
    .byte $BE,$FB,$FA,$BF,$C0,$00,$0F,$FB,$BF,$FB,$FF,$C0,$00,$0B,$FF,$FF ; $7331
    .byte $FF,$EF,$C0,$00,$0E,$FF,$FF,$FA,$BF,$C0,$00,$0F,$EF,$EF,$FB,$FF ; $7341
    .byte $C0,$00,$2B,$F9,$BF,$EF,$BF,$C0,$00,$AB,$F9,$BB,$FF,$AA,$00,$15 ; $7351
    .byte $AB,$FA,$6B,$FF,$AA,$54,$55,$AB,$F6,$97,$FA,$AA,$55,$55,$AB,$FA ; $7361
    .byte $AB,$FA,$AA,$55,$55,$6A,$F9,$9B,$FA,$A9,$55,$55,$6A,$FD,$BF,$FA ; $7371
    .byte $A9,$55,$55,$6A,$BF,$FE,$AA,$A9,$55              ; $7381
    :16 .byte $00                    ; $738A (16 zero bytes)
    .byte $0F,$FF                                          ; $739A
    :5 .byte $00                    ; $739C (5 zero bytes)
    .byte $3F,$AB,$C0,$00,$00,$00,$0C,$FA,$BF,$F0,$00,$00,$00,$3E,$E9,$5F ; $73A1
    .byte $F0,$00,$00,$00,$3E,$E5,$6F,$F8,$00,$00,$00,$3E,$95,$6F,$F8,$00 ; $73B1
    .byte $00,$00,$0F,$95,$7F,$AF,$FC,$00,$00,$02,$A5,$DE,$FF,$FF,$C0,$00 ; $73C1
    .byte $02,$EA,$AB,$FB,$FF,$F0,$00,$02,$EF,$EA,$EF,$FF,$F0,$00,$0B,$BF ; $73D1
    .byte $EF,$EF,$FF,$F0,$00,$0F,$FF,$FF,$BF,$FF,$C0,$00,$3F,$FF,$EA,$FF ; $73E1
    .byte $FF,$C0,$00,$3F,$BF,$EF,$FF,$FF,$C0,$00,$06,$FF,$BE,$FF,$FF,$00 ; $73F1
    .byte $00,$A6,$EF,$FE,$FF,$FE,$00,$15,$A1,$AF,$FE,$FA,$AA,$54,$55,$AA ; $7401
    .byte $5F,$EA,$AA,$AA,$55,$55,$AA,$AF,$EA,$AA,$AA,$55,$55,$6A,$6F,$EA ; $7411
    .byte $AA,$A9,$55,$55,$6E,$FF,$EA,$AA,$A9,$55,$55,$6B,$FA,$AA,$AA,$A9 ; $7421
    .byte $55,$00,$3F,$C0,$00,$03,$FC,$00,$02,$AB,$FC,$00,$3F,$EA,$80,$09 ; $7431
    .byte $5A,$FF,$00,$FF,$A5,$60,$05,$56,$BF,$00,$FE,$95,$50,$05,$65,$AF ; $7441
    .byte $00,$FA,$59,$50,$05,$65,$6C,$00,$39,$59,$50,$09,$55,$78,$00,$2D ; $7451
    .byte $55,$60,$02,$55,$E0,$00,$0B,$55,$80,$00,$AB,$E0,$00,$0B,$EA,$00 ; $7461
    .byte $00,$2B,$E8,$00,$2B,$E8,$00,$00,$2F,$FB,$82,$EF,$F8,$00,$00,$3B ; $7471
    .byte $FF,$EB,$FF,$EC,$00,$00,$FE,$FE,$EB,$BF,$BF,$00,$00,$BF,$F7,$BE ; $7481
    .byte $DF,$FE,$00,$00,$2B,$FF,$FF,$FF,$E8,$00,$00,$3B,$FF,$FF,$FF,$EC ; $7491
    .byte $00,$00,$2E,$FB,$FF,$EF,$B8,$00,$00,$AF,$FE,$69,$BF,$FA,$00,$15 ; $74A1
    .byte $AB,$E6,$69,$9B,$EA,$54,$55,$AB,$EA,$AA,$AB,$EA,$55,$55,$AB,$E6 ; $74B1
    .byte $69,$9B,$EA,$55,$55,$6A,$FE,$69,$BF,$A9,$55,$55,$6A,$FF,$FF,$FF ; $74C1
    .byte $A9,$55,$55,$6A,$BF,$FF,$FE,$A9,$55,$00,$3F,$C0,$00,$03,$FC,$00 ; $74D1
    .byte $02,$AB,$FC,$00,$3F,$EA,$80,$09,$5A,$FF,$00,$FF,$A5,$60,$05,$56 ; $74E1
    .byte $BF,$00,$FE,$95,$50,$05,$65,$AF,$00,$FA,$59,$50,$05,$65,$6C,$00 ; $74F1
    .byte $39,$59,$50,$09,$55,$78,$00,$2D,$55,$60,$02,$55,$E0,$00,$0B,$55 ; $7501
    .byte $80,$00,$AB,$E0,$00,$0B,$EA,$00,$00,$2B,$E8,$00,$2B,$E8,$00,$00 ; $7511
    .byte $2F,$FB,$82,$EF,$F8,$00,$00,$3B,$FF,$EB,$FF,$EC,$00,$00,$FE,$FE ; $7521
    .byte $EB,$BF,$BF,$00,$00,$BF,$F7,$BE,$DF,$FE,$00,$00,$2B,$FB,$FF,$EF ; $7531
    .byte $E8,$00,$00,$3B,$FE,$69,$BF,$EC,$00,$00,$2E,$E6,$69,$9B,$B8,$00 ; $7541
    .byte $00,$AF,$E9,$AA,$6B,$FA,$00,$15,$AB,$FA,$AA,$AF,$EA,$54,$55,$AB ; $7551
    .byte $96,$AA,$96,$EA,$55,$55,$AB,$EA,$AA,$AB,$EA,$55,$55,$6A,$E6,$69 ; $7561
    .byte $9B,$A9,$55,$55,$6A,$FE,$69,$BF,$A9,$55,$55,$6A,$BF,$FF,$FE,$A9 ; $7571
    .byte $55                                              ; $7581
    :15 .byte $00                    ; $7582 (15 zero bytes)
    .byte $FF,$00,$00,$00,$FF,$00,$0A,$AF,$F0,$00,$0F,$FA,$A0,$25,$6B,$FC ; $7591
    .byte $00,$3F,$E9,$58,$15,$5A,$FC,$00,$3F,$A5,$54,$15,$96,$BC,$00,$3E ; $75A1
    .byte $96,$54,$15,$95,$B0,$00,$0E,$56,$54,$25,$55,$E0,$00,$0B,$55,$58 ; $75B1
    .byte $09,$57,$80,$00,$02,$D5,$60,$02,$AF,$E3,$82,$CB,$FA,$80,$00,$2B ; $75C1
    .byte $FF,$EB,$FF,$E8,$00,$00,$FE,$FE,$EB,$BF,$BF,$00,$00,$BF,$F7,$BE ; $75D1
    .byte $DF,$FE,$00,$00,$2B,$FF,$FF,$FF,$E8,$00,$00,$3B,$FB,$FF,$EF,$EC ; $75E1
    .byte $00,$00,$2E,$FE,$69,$BF,$B8,$00,$00,$AF,$E6,$69,$9B,$FA,$00,$15 ; $75F1
    .byte $AB,$E9,$AA,$6B,$EA,$54,$55,$AB,$D6,$AA,$97,$EA,$55,$55,$AB,$EA ; $7601
    .byte $AA,$AB,$EA,$55,$55,$6A,$E6,$69,$9B,$A9,$55,$55,$6A,$FE,$69,$BF ; $7611
    .byte $A9,$55,$55,$6A,$BF,$FF,$FE,$A9,$55              ; $7621
    :29 .byte $00                    ; $762A (29 zero bytes)
    .byte $FF,$00,$00,$00,$FF,$00,$0A,$AF,$F0,$00,$0F,$FA,$A0,$25,$6B,$FC ; $7647
    .byte $00,$3F,$E9,$58,$15,$5A,$FC,$00,$3F,$A5,$54,$15,$96,$BC,$00,$3E ; $7657
    .byte $96,$54,$15,$95,$B0,$00,$0E,$56,$54,$25,$55,$EC,$82,$3B,$55,$58 ; $7667
    .byte $09,$57,$BF,$EB,$FE,$D5,$60,$02,$AF,$AE,$EB,$BA,$FA,$80,$00,$BF ; $7677
    .byte $F7,$BE,$DF,$FE,$00,$00,$EB,$FF,$FF,$FF,$EB,$00,$00,$3B,$FF,$FF ; $7687
    .byte $FF,$EC,$00,$00,$2E,$FF,$FF,$FF,$B8,$00,$00,$AF,$FB,$FF,$EF,$FA ; $7697
    .byte $00,$15,$AB,$FE,$69,$BF,$EA,$54,$55,$AB,$E6,$69,$9B,$EA,$55,$55 ; $76A7
    .byte $AB,$EA,$AA,$AB,$EA,$55,$55,$6A,$E6,$69,$9B,$A9,$55,$55,$6A,$FE ; $76B7
    .byte $69,$BF,$A9,$55,$55,$6A,$BF,$FF,$FE,$A9,$55      ; $76C7
    :29 .byte $00                    ; $76D2 (29 zero bytes)
    .byte $FF,$00,$00,$00,$FF,$00,$0A,$AF,$F0,$00,$0F,$FA,$A0,$25,$6B,$FC ; $76EF
    .byte $00,$3F,$E9,$58,$15,$5A,$FC,$00,$3F,$A5,$54,$15,$96,$BC,$00,$3E ; $76FF
    .byte $96,$54,$15,$95,$B0,$00,$0E,$56,$54,$25,$55,$EC,$82,$3B,$55,$58 ; $770F
    .byte $09,$57,$BF,$EB,$FE,$D5,$60,$02,$AF,$AE,$EB,$BA,$FA,$80,$00,$BF ; $771F
    .byte $F7,$BE,$DF,$FE,$00,$00,$EB,$FF,$FF,$FF,$EB,$00,$00,$3B,$FF,$FF ; $772F
    .byte $FF,$EC,$00,$00,$2E,$FF,$FF,$FF,$B8,$00,$00,$AF,$FB,$FF,$EF,$FA ; $773F
    .byte $00,$15,$AB,$EE,$69,$BB,$EA,$54,$55,$AB,$EA,$AA,$AB,$EA,$55,$55 ; $774F
    .byte $AA,$E6,$69,$9B,$AA,$55,$55,$6A,$FE,$69,$BF,$A9,$55,$55,$6A,$BF ; $775F
    .byte $FF,$FE,$A9,$55,$55,$6A,$AA,$AA,$AA,$A9,$55      ; $776F
    :29 .byte $00                    ; $777A (29 zero bytes)
    .byte $FF,$00,$00,$00,$FF,$00,$0F,$FF,$F0,$00,$0F,$FF,$F0,$3F,$FF,$FC ; $7797
    .byte $00,$3F,$FF,$FC,$3F,$FF,$FC,$00,$3F,$FF,$FC,$2A,$AF,$FC,$00,$3F ; $77A7
    .byte $FA,$A8,$3E,$AB,$F0,$00,$0F,$EA,$BC,$2F,$FE,$AC,$82,$3A,$BF,$F8 ; $77B7
    .byte $0B,$FF,$BF,$EB,$FE,$FF,$E0,$02,$AF,$AE,$EB,$BA,$FA,$80,$00,$BF ; $77C7
    .byte $F7,$BE,$DF,$FE,$00,$00,$EB,$FF,$FF,$FF,$EB,$00,$00,$3B,$FF,$FF ; $77D7
    .byte $FF,$EC,$00,$00,$2E,$FF,$FF,$FF,$B8,$00,$00,$AF,$FB,$FF,$EF,$FA ; $77E7
    .byte $00,$15,$AB,$EE,$69,$BB,$EA,$54,$55,$AB,$EA,$AA,$AB,$EA,$55,$55 ; $77F7
    .byte $AA,$E6,$69,$9B,$AA,$55,$55,$6A,$FE,$69,$BF,$A9,$55,$55,$6A,$BF ; $7807
    .byte $FF,$FE,$A9,$55,$55,$6A,$AA,$AA,$AA,$A9,$55      ; $7817
    :43 .byte $00                    ; $7822 (43 zero bytes)
    .byte $FF,$AF,$00,$FA,$FF,$00,$0F,$FF,$FB,$C3,$EF,$FF,$F0,$3F,$FF,$FE ; $784D
    .byte $FF,$BF,$FF,$FC,$3F,$FF,$FE,$FF,$BF,$FF,$FC,$3F,$FF,$FE,$FF,$BF ; $785D
    .byte $FF,$FC,$3F,$FF,$FB,$FF,$EF,$FF,$FC,$2F,$FF,$FB,$FF,$EF,$FF,$F8 ; $786D
    .byte $0B,$FF,$EF,$FF,$FB,$FF,$E0,$02,$AA,$BF,$BE,$FE,$AA,$80,$00,$AA ; $787D
    .byte $F7,$EB,$DF,$AA,$00,$00,$3B,$FE,$EB,$BF,$EC,$00,$00,$AE,$FF,$BE ; $788D
    .byte $FF,$BA,$00,$15,$AB,$FF,$FF,$FF,$EA,$54,$55,$AB,$FB,$FF,$EF,$EA ; $789D
    .byte $55,$55,$AA,$E6,$69,$9B,$AA,$55,$55,$6A,$E6,$AA,$9B,$A9,$55,$55 ; $78AD
    .byte $6A,$BF,$FF,$FE,$A9,$55,$55,$6A,$AA,$AA,$AA,$A9,$55,$55,$5A,$C3 ; $78BD
    .byte $AA,$A5,$55,$55,$56,$C3,$AA,$95,$C3,$55,$6A,$AA,$A9,$F3,$55,$56 ; $78CD
    .byte $59,$65,$56,$59,$55,$95,$56,$59,$66,$55,$A5,$55,$95,$55,$A5,$6A ; $78DD
    .byte $96,$A9,$6A,$95,$C8,$55,$59,$5A,$56,$59,$59,$55,$55,$59,$65,$96 ; $78ED
    .byte $59,$66,$55,$56,$A9,$6A,$95,$A5,$6A,$95,$C7,$55,$C0 ; $78FD
set_pmg_positions:
    ; Fill loop — indexed store of constant value; Sets color registers; Configures player-missile graphics; Writes to screen memory; Arithmetic with shifts; Writes: HPOSP2, SIZEP2, COLPM2; 159 instructions, 3 loops
    lda #$00
    ldy $286B
    beq loc_791E
    sty zp_screen_ptr2_lo
    ldy $286A
loc_7916:
    sta $0E32,y
    iny
    dec zp_screen_ptr2_lo
    bne loc_7916
loc_791E:
    sta $2875
    sta $CE
    lda #$01
    sta zp_screen_ptr2_lo
    stx $286C
    lda $2851
    tax
    and #$03
    sta $286D
    txa
    lsr
    lsr
    sec
    sbc #$10
    bcs loc_7940
    lda height_delta_tab
    bcc loc_7961
loc_7940:
    cmp #$1F
    bcc loc_7949
    lda $28EF
    bcs loc_7961
loc_7949:
    tay
    ldx #$03
    lda #$00
loc_794E:
    clc
    adc height_delta_tab,y
    cpx $286D
    bne loc_7958
    iny
loc_7958:
    dex
    bpl loc_794E
    cmp #$80
    ror
    cmp #$80
    ror
loc_7961:
    sta $286E
    ldx $286C
    cpx #$1A
    bcc loc_797B
    bne loc_7978
loc_796D:
    lda #$00
    sta $284E
    lda #$01
    sta $284F
    rts
loc_7978:
    jmp loc_79DB
loc_797B:
    stx $284F
    lda $7A64,x
    clc
    adc $2852
    clc
    adc $286E
    bpl loc_798D
    lda #$58
loc_798D:
    tax
    lda $284E
    cmp #$08
    bcc loc_7997
    lda #$07
loc_7997:
    tay
    lda $7ADA,y
loc_799B:
    tay
    stx $286A
loc_799F:
    lda zp_screen_ptr2_lo
    sta zp_screen_ptr2_hi
    lda $7A7F,y
    bne loc_79CB
    sec
    txa
    sbc $286A
    sta $286B
    inc $284E
    sec
    lda $2851
    sbc $2875
    sta $CC
    sta HPOSP2
    lda $2850
    sta COLPM2
    lda $CE
    sta SIZEP2
    rts
loc_79CB:
    cpx #$56
    bcs loc_79D3
    sta $0E32,x
    inx
loc_79D3:
    dec zp_screen_ptr2_hi
    bne loc_79CB
    iny
    jmp loc_799F
loc_79DB:
    ldx $284F
    lda $2871
    bne loc_7A00
    lda #$0E
    sta $2850
    ldy $284F
    cpy #$09
    bcc loc_79F1
    ldy #$08
loc_79F1:
    lda $7B01,y
    sta $2873
    lda $7AF9,y
    sta $2874
    sta $2871
loc_7A00:
    cmp $2873
    bcc loc_7A0F
    ldy #$3E
    sty $2850
    ldy #$FF
    sty $2872
loc_7A0F:
    clc
    adc $2872
    sta $2871
    tay
    lda $7A64,x
    sec
    sbc $7AED,y
    clc
    adc $2852
    clc
    adc $286E
    bpl loc_7A2A
    lda #$58
loc_7A2A:
    tax
    lda $2872
    bpl loc_7A3B
    dec $2850
    cpy $2874
    bcs loc_7A3B
    jmp loc_796D
loc_7A3B:
    tya
    cmp #$07
    bcc loc_7A5D
    cmp #$0A
    bcc loc_7A51
    ldy #$0C
    sty $2875
    ldy #$03
    sty $CE
    iny
    jmp loc_7A5B
loc_7A51:
    ldy #$04
    sty $2875
    ldy #$01
    sty $CE
    iny
loc_7A5B:
    sty zp_screen_ptr2_lo
loc_7A5D:
    tay
    lda $7AE1,y
    jmp loc_799B
    .byte $52,$51,$44,$3E,$3A,$38,$36,$35,$34,$33,$32,$31,$31,$30,$30,$30 ; $7A64
    :5 .byte $2F                    ; $7A74 (5 bytes)
    :6 .byte $2E                    ; $7A79 (6 bytes)
    .byte $08,$1C,$1C,$3E,$7F,$00,$08,$1C                  ; $7A7F
    :11 .byte $3E                    ; $7A87 (11 bytes)
    .byte $1C,$00,$08                                      ; $7A92
    :5 .byte $1C                    ; $7A95 (5 bytes)
    .byte $00                                              ; $7A9A
    :4 .byte $0C                    ; $7A9B (4 bytes)
    .byte $00,$08,$08,$08,$00,$08,$08,$00,$08,$00,$18,$18,$00 ; $7A9F
    :4 .byte $1C                    ; $7AAC (4 bytes)
    .byte $00,$18                                          ; $7AB0
    :4 .byte $3C                    ; $7AB2 (4 bytes)
    .byte $18,$00,$1C                                      ; $7AB6
    :6 .byte $3E                    ; $7AB9 (6 bytes)
    .byte $1C,$00,$18,$3C,$3C                              ; $7ABF
    :4 .byte $7E                    ; $7AC4 (4 bytes)
    .byte $3C,$3C,$18,$00,$18,$3C,$7E,$7E                  ; $7AC8
    :6 .byte $FF                    ; $7AD0 (6 bytes)
    .byte $7E,$7E,$3C,$18,$00,$00,$06,$15,$1C,$21,$25,$28,$2A,$2D,$32,$39 ; $7AD6
    .byte $42,$4D,$39,$42,$4D,$39,$42,$4D,$01,$02,$03,$04,$05,$07,$08,$0A ; $7AE6
    .byte $0E,$10,$14,$1C,$07,$07,$06,$05,$04,$03,$02,$01,$0C,$0C,$0B,$0A ; $7AF6
    .byte $09,$08,$07,$06                                  ; $7B06
.proc build_line_table
    ; BUILD LINE TABLE — Constructs rendering line table for terrain perspective
    ldy #$00
    lda zp_memcpy_len_lo
    sta row_addr_lo_blend
    lda zp_memcpy_len_hi
    sta row_addr_hi_blend
loc_7B16:
    clc
    lda row_addr_lo_blend,y
    adc zp_memcpy_dst_lo
    sta $0752,y
    lda row_addr_hi_blend,y
    adc #$00
    sta $07A8,y
    iny
    cpy #$55
    bne loc_7B16
    rts
.endp

.proc render_copy_7B2D
    ; Copy loop — indexed load/store transfer; Writes to screen memory; Table access: $0751, $07A7, $2954, $2984; 10 instructions, 1 loop
    ldx #$00
    ldy #$2F
loc_7B31:
    lda row_addr_lo_blend,y
    sta $2954,x
    lda row_addr_hi_blend,y
    sta $2984,x
    inx
    dey
    bpl loc_7B31
    rts
.endp

.proc setup_gameplay_display
    ; Wait loop polling VCOUNT (scanline sync); Reads: VCOUNT; 4 instructions, 1 loop
    lda #$30
loc_7B44:
    cmp VCOUNT
    bne loc_7B44
    rts
.endp

.proc enable_display
    ; Wait loop polling VCOUNT (scanline sync); Reads: VCOUNT; 4 instructions, 1 loop
    lda VCOUNT
    cmp #$7A
    bcc enable_display
    rts
.endp

.proc util_fill_7B52
    ; Fill loop — indexed store of constant value; Reads: SKRES; Calls: render_search_7B9E, render_search_7B97; Table access: $0900, $0A00; 34 instructions, 4 loops
    ldy #$00
    tya
loc_7B55:
    sta pilot_map,y
    dey
    bne loc_7B55
    ldy #$08
    lda game_timer
    cmp #$01
    bne loc_7B66
    ldy #$10
loc_7B66:
    jsr render_search_7B9E
    dey
    bne loc_7B66
    lda game_timer
    cmp #$04
    bcc loc_7B7B
    ldy #$04
loc_7B75:
    jsr render_search_7B97
    dey
    bne loc_7B75
loc_7B7B:
    ldx #$00
loc_7B7D:
    lda RANDOM
    cmp $0617
    bcs loc_7B8F
    lda dat_0900,x
    bpl loc_7B8F
    lda #$64
    sta pilot_map,x
loc_7B8F:
    clc
    txa
    adc #$43
    tax
    bne loc_7B7D
    rts
.endp

render_search_7B97:
    ; Search loop — scans array for match; Writes to screen memory; Table access: $0900, $0A00, $2270; 37 instructions, 2 loops
    lda #$01
    sta $281F
    bne loc_7BA3
render_search_7B9E:
    ; Search loop — scans array for match; Writes to screen memory; Table access: $0900, $0A00, $2270; 34 instructions, 2 loops
    lda #$C8
    sta $281F
loc_7BA3:
    ldx #$00
    stx $28A2
loc_7BA8:
    stx $2821
    lda pilot_map,x
    bne loc_7BD8
    lda dat_0900,x
    sta $2822
    cmp #$30
    bcs loc_7BD8
    cmp $28A2
    bcc loc_7BD8
    ldx #$2C
loc_7BC1:
    lda vtx_grid_idx,x
    cmp $2821
    beq loc_7BD8
    dex
    bpl loc_7BC1
    lda $2822
    sta $28A2
    lda $2821
    sta $28A1
loc_7BD8:
    clc
    lda $2821
    adc #$43
    tax
    bne loc_7BA8
    lda $28A2
    beq loc_7BEF
    ldx $28A1
    lda $281F
    sta pilot_map,x
loc_7BEF:
    rts
render_copy_7BF0:
    ; Copy loop — indexed load/store transfer; Writes to screen memory; Table access: $0A00, $2270; 21 instructions, 2 loops
    ldx #$00
loc_7BF2:
    stx $2821
    lda pilot_map,x
    cmp #$01
    bne loc_7C12
    ldx #$2C
loc_7BFE:
    lda vtx_grid_idx,x
    cmp $2821
    beq loc_7C12
    dex
    bpl loc_7BFE
    ldx $2821
    lda #$80
    sta pilot_map,x
    rts
loc_7C12:
    clc
    lda $2821
    adc #$43
    tax
    bne loc_7BF2
    rts
.proc mirror_charset
    ; Table access: $503B, $503C; 52 instructions, 4 loops
    lda #$00
    sta zp_depth_accum
    lda #$10
    sta zp_ptr_lo
    lda #$08
    sta FR1
loc_7C28:
    lda #$00
    sta zp_ptr_hi
    lda #$04
    sta zp_column_x
loc_7C30:
    ldy zp_depth_accum
    lda $503B,y
    sta zp_memcpy_dst_lo
    lda $503C,y
    sta zp_memcpy_dst_hi
    sec
    lda zp_ptr_lo
    sbc zp_ptr_hi
    tay
    lda $503B,y
    sta zp_memcpy_len_lo
    lda $503C,y
    sta zp_memcpy_len_hi
    ldy #$00
loc_7C4E:
    lda (zp_memcpy_dst_lo),y
    ldx #$08
loc_7C52:
    asl
    ror zp_proj_y_hi
    dex
    bne loc_7C52
    lda zp_proj_y_hi
    sta (zp_memcpy_len_lo),y
    beq loc_7C61
    iny
    bpl loc_7C4E
loc_7C61:
    inc zp_depth_accum
    inc zp_depth_accum
    inc zp_ptr_hi
    inc zp_ptr_hi
    dec zp_column_x
    bne loc_7C30
    clc
    lda zp_ptr_lo
    adc #$12
    sta zp_ptr_lo
    clc
    lda zp_depth_accum
    adc #$0A
    sta zp_depth_accum
    dec FR1
    bne loc_7C28
    rts
.endp

game_init:
    ; INIT GAME — Full game state initialization: clears variables, sets starting position, fuel, lives, shield. Calls level_setup
    lda #$00
    sta zp_random_work
    lda #$B5
    sta zp_memcpy_dst_lo
    lda #$32
    sta zp_memcpy_dst_hi
    lda #$47
    sta zp_memcpy_len_lo
    lda #$00
    sta zp_memcpy_len_hi
    jsr memcpy_block
    lda #$00
    sta zp_memcpy_dst_lo
    lda #$0B
    sta zp_memcpy_dst_hi
    lda #$FF
    sta zp_memcpy_len_lo
    lda #$04
    sta zp_memcpy_len_hi
    jmp memcpy_block
clear_screen:
    ; Fill loop — indexed store of constant value; Writes to screen memory; Table access: $2608; 29 instructions, 1 loop
    ldy #$00
    lda #$FF
loc_7CAE:
    sta zbuf_columns,y
    dey
    bne loc_7CAE
    lda #$70
    sta zp_memcpy_dst_lo
    lda #$10
    sta zp_memcpy_dst_hi
    lda #$9F
    sta zp_memcpy_len_lo
    lda #$11
    sta zp_memcpy_len_hi
    jmp memcpy_block
pmg_util_7CC7:
    ; 23 instructions
    lda #$00
    sta zp_memcpy_dst_lo
    lda #$20
    sta zp_memcpy_dst_hi
    lda #$73
    sta zp_memcpy_len_lo
    lda #$0F
    sta zp_memcpy_len_hi
memcpy_block:
    ; 15 instructions
    ldy #$00
loc_7CD9:
    lda zp_random_work
    sta (zp_memcpy_dst_lo),y
    inc zp_memcpy_dst_lo
    bne loc_7CE3
    inc zp_memcpy_dst_hi
loc_7CE3:
    dec zp_memcpy_len_lo
    lda #$FF
    cmp zp_memcpy_len_lo
    bne loc_7CF2
    dec zp_memcpy_len_hi
    cmp zp_memcpy_len_hi
    bne loc_7CF2
    rts
loc_7CF2:
    jmp loc_7CD9
render_fill_7CF5:
    ; Fill loop — indexed store of constant value; Writes to screen memory; Calls: render_fill_7D18, render_helper_7D71, render_loop_7D54, render_helper_7D7E; Table access: $32FD; 69 instructions, 4 loops
    lda #$0E
    sta zp_screen_ptr_lo
    lda #$52
    sta zp_screen_ptr_hi
    lda #$32
    sta zp_screen_ptr2_lo
    lda #$0C
    sta zp_screen_ptr2_hi
    jsr render_fill_7D18
    lda #$1D
    sta zp_screen_ptr_lo
    lda #$52
    sta zp_screen_ptr_hi
    lda #$32
    sta zp_screen_ptr2_lo
    lda #$0D
    sta zp_screen_ptr2_hi
.proc render_fill_7D18
    ; Fill loop — indexed store of constant value; Writes to screen memory; Calls: render_helper_7D71, render_loop_7D54, render_helper_7D7E, render_helper_7D71; Table access: $32FD; 52 instructions, 4 loops
    ldy #$00
loc_7D1A:
    lda (zp_screen_ptr_lo),y
    bne loc_7D1F
    rts
loc_7D1F:
    jsr render_helper_7D71
    ldy zp_color_work
    iny
    bne loc_7D1A
.endp

render_fill_7D27:
    ; Fill loop — indexed store of constant value; Writes to screen memory; Calls: render_loop_7D54, render_helper_7D7E, render_helper_7D71, render_helper_7D7E; Table access: $32FD; 44 instructions, 3 loops
    ldy #$2F
    lda #$AA
loc_7D2B:
    sta $32FD,y
    dey
    bpl loc_7D2B
    lda #$2D
    sta zp_screen_ptr2_lo
    lda #$33
    sta zp_screen_ptr2_hi
    lda #$22
    sta zp_screen_ptr_lo
    lda #$51
    sta zp_screen_ptr_hi
    jsr render_loop_7D54
    lda #$CD
    sta zp_screen_ptr2_lo
    lda #$35
    sta zp_screen_ptr2_hi
    lda #$CA
    sta zp_screen_ptr_lo
    lda #$78
    sta zp_screen_ptr_hi
render_loop_7D54:
    ; Calls: render_helper_7D7E, render_helper_7D71, render_helper_7D7E; 22 instructions, 2 loops
    ldy #$00
loc_7D56:
    lda (zp_screen_ptr_lo),y
    cmp #$C0
    bcs loc_7D64
    sty zp_color_work
    jsr render_helper_7D7E
    jmp loc_7D6C
loc_7D64:
    and #$3F
    bne loc_7D69
    rts
loc_7D69:
    jsr render_helper_7D71
loc_7D6C:
    ldy zp_color_work
    iny
    bne loc_7D56
.proc render_helper_7D71
    ; Calls: render_helper_7D7E; 8 instructions, 1 loop
    tax
    iny
    lda (zp_screen_ptr_lo),y
    sty zp_color_work
loc_7D77:
    jsr render_helper_7D7E
    dex
    bne loc_7D77
    rts
.endp

.proc render_helper_7D7E
    ; 6 instructions
    ldy #$00
    sta (zp_screen_ptr2_lo),y
    inc zp_screen_ptr2_lo
    bne loc_7D88
    inc zp_screen_ptr2_hi
loc_7D88:
    rts
.endp

.proc render_table_lookup_7D89
    ; Table access: $350C; 10 instructions, 1 loop
    lda #$0F
    sta zp_screen_ptr_lo
    lda #$18
    sta zp_screen_ptr_hi
    ldy #$C0
loc_7D93:
    lda $350C,y
    sta (zp_screen_ptr_lo),y
    dey
    bne loc_7D93
    rts
.endp

wait_n_frames:
    ; Wait loop polling RTCLOK (frame sync); 8 instructions, 1 loop
    pha
loc_7D9D:
    lda #$00
    sta $14
    lda frame_counter
loc_7DA4:
    cmp $14
    bne loc_7DA4
    pla
    rts
wait_60:
    ; Wait loop polling RTCLOK (frame sync); Arithmetic with shifts; 40 instructions, 7 loops
    pha
    lda #$3C
    bne loc_7DB2
wait_1:
    ; WAIT 1 FRAME — Convenience: waits exactly 1 frame
    pha
    lda #$01
loc_7DB2:
    sta frame_counter
    bne loc_7D9D
.proc wait_2
    ; WAIT 2 FRAMES — Convenience: waits exactly 2 frames
    pha
    lda #$02
    bne loc_7DB2
.endp

.proc wait_5
    ; WAIT 5 FRAMES — Convenience: waits exactly 5 frames
    pha
    lda #$05
    bne loc_7DB2
.endp

.proc wait_10
    ; WAIT 10 FRAMES — Convenience: waits exactly 10 frames
    pha
    lda #$0A
    bne loc_7DB2
.endp

.proc wait_20
    ; WAIT 20 FRAMES — Convenience: waits exactly 20 frames
    pha
    lda #$14
    bne loc_7DB2
.endp

.proc calc_7DCB
    ; Arithmetic with shifts; 14 instructions, 1 loop
    ldy #$FF
    sec
loc_7DCE:
    iny
    sbc #$0A
    bcs loc_7DCE
    adc #$0A
    sta zp_memcpy_dst_lo
    tya
    asl
    asl
    asl
    asl
    ora zp_memcpy_dst_lo
    rts
.endp

.proc timing_proc_7DDF
    ; 11 instructions, 1 loop
    lda #$00
    sta zp_memcpy_len_lo
loc_7DE3:
    inc $0620
    clc
    lda zp_memcpy_len_lo
    adc #$01
    sta zp_memcpy_len_lo
    lda $0620
    cmp game_timer
    bne loc_7DE3
    rts
.endp

timing_update_score_7DF6:
    ; BCD arithmetic — score or counter update; Arithmetic with shifts; Calls: render_loop_432A, math_update_score_4C91, queue_push_flagged; 46 instructions, 7 loops
    sed
    sec
    lda display_enabled
    sbc #$01
    sta display_enabled
    cld
    jsr render_loop_432A
wait_frames_7E04:
    ; Wait loop polling RTCLOK (frame sync); Arithmetic with shifts; Calls: math_update_score_4C91, queue_push_flagged; 39 instructions, 7 loops
    jsr math_update_score_4C91
    ldx #$10
    jsr queue_push_flagged
    jmp wait_10
wait_frames_7E0F:
    ; Wait loop polling RTCLOK (frame sync); Arithmetic with shifts; Calls: calc_7DCB, render_loop_432A; 39 instructions, 7 loops
    jsr calc_7DCB
    sta display_enabled
    jsr render_loop_432A
    jmp wait_20
bit_extract_7E1B:
    ; Bit extraction/packing via shifts and rotates; Calls: calc_7DCB; 119 instructions
    ldx game_timer
    txa
    lsr
    clc
    adc #$02
    cmp #$14
    bcc loc_7E29
    lda #$14
loc_7E29:
    pha
    sta lives_or_fuel
    jsr calc_7DCB
    sta display_enabled
    pla
    tay
    lsr
    clc
    adc #$01
    sta $0613
    tya
    sec
    sbc #$05
    bpl loc_7E44
    lda #$00
loc_7E44:
    sta $0614
    txa
    cmp #$01
    bne loc_7E51
    lda #$00
    jmp loc_7E59
loc_7E51:
    cmp #$2B
    bcc loc_7E57
    lda #$2B
loc_7E57:
    asl
    asl
loc_7E59:
    sta $0617
    txa
    cmp #$04
    bcs loc_7E66
    lda #$00
    jmp loc_7E74
loc_7E66:
    cmp #$23
    bcc loc_7E6C
    lda #$22
loc_7E6C:
    asl
    sta zp_memcpy_dst_lo
    sec
    lda #$58
    sbc zp_memcpy_dst_lo
loc_7E74:
    sta $0615
    sta enabled_flag_0616
    txa
    cmp #$28
    bcc loc_7E81
    lda #$28
loc_7E81:
    sta zp_memcpy_dst_lo
    lda #$2C
    sec
    sbc zp_memcpy_dst_lo
    lsr
    sta $0618
    stx zp_memcpy_dst_lo
    lda #$2A
    sec
    sbc zp_memcpy_dst_lo
    bmi loc_7E9A
    lsr
    cmp #$04
    bcs loc_7E9C
loc_7E9A:
    lda #$04
loc_7E9C:
    sta $060B
    sec
    lda #$1A
    sbc zp_memcpy_dst_lo
    bmi loc_7EA7
    lsr
loc_7EA7:
    sta $060D
    bmi loc_7EB0
    cmp #$02
    bcs loc_7EB2
loc_7EB0:
    lda #$02
loc_7EB2:
    cmp #$08
    bcc loc_7EBB
    lda #$FF
    jmp loc_7EC0
loc_7EBB:
    asl
    asl
    asl
    asl
    asl
loc_7EC0:
    sta $060E
    txa
    lsr
    lsr
    clc
    adc #$04
    cmp #$0F
    bcc loc_7ECF
    lda #$0F
loc_7ECF:
    .byte $8D                     ; $7ECF
    .byte $0F                     ; $7ED0
loc_7ED1:
    .byte $06                     ; $7ED1
    lsr zp_memcpy_dst_lo
    lsr zp_memcpy_dst_lo
    sec
    lda #$08
    sbc zp_memcpy_dst_lo
    bpl loc_7EDF
    lda #$00
loc_7EDF:
    sta $0619
    txa
    cmp #$10
    bcc loc_7EF5
    sec
    sbc #$10
    cmp #$20
    bcc loc_7EF0
    lda #$1F
loc_7EF0:
    sta $088B
    lda #$00
loc_7EF5:
    sta $0608
    rts
render_fill_7EF9:
    ; Fill loop — indexed store of constant value; Writes to screen memory; Calls: build_line_table, render_copy_5DFA, build_line_table; 132 instructions, 7 loops
    ldy #$08
loc_7EFB:
    lda $7FF8,y
    sta $3000,y
    sta $3120,y
    sta $316B,y
    sta $3210,y
    lda wait_frames_7FF2,y
    sta $310B,y
    sta $3156,y
    dey
    bpl loc_7EFB
    ldy #$0E
loc_7F18:
    lda $8001,y
    sta $3111,y
    sta $315C,y
    sta $3201,y
    sta $32A6,y
    dey
    bpl loc_7F18
    ldy #$87
    lda #$4D
loc_7F2E:
    sta $3174,y
    sta $3219,y
    dey
    cpy #$FF
    bne loc_7F2E
    sta $3129
    lda #$00
    sta $312A
    lda #$10
    sta $312B
    ldy #$7E
    .byte $A9                     ; $7F48
loc_7F49:
    .byte $4F                     ; $7F49
loc_7F4A:
    sta $3009,y
    sta $308A,y
    dey
    dey
    dey
    bpl loc_7F4A
    ldy #$28
    lda #$0D
loc_7F59:
    sta $312C,y
    dey
    bpl loc_7F59
    lda #$CF
    sta $306F
    sta $30ED
    sta $30F9
    sta $3105
    lda #$8D
    sta $3008
    sta $3128
    sta $313C
    sta $3151
    sta $3155
    lda #$84
    sta $3007
    lda #$CD
    sta $31F2
    sta $31FE
    sta $3297
    sta $32A3
    lda #$00
    sta $311E
    lda #$30
    sta $311F
    lda #$20
    sta $3169
    lda #$31
    sta $316A
    lda #$6B
    sta $320E
    lda #$31
    sta $320F
    lda #$10
    sta $32B3
    lda #$32
    sta $32B4
    lda #$60
    sta zp_memcpy_dst_lo
    lda #$70
    sta zp_memcpy_len_lo
    .byte $A9                     ; $7FC1
loc_7FC2:
    .byte $10                     ; $7FC2
    sta zp_memcpy_len_hi
    jsr build_line_table
    lda #$75
    sta $C6
    lda #$31
    sta zp_render_scratch
    lda #$00
    sta $08DB
    lda #$2F
    sta zp_color_shade
    jsr render_copy_5DFA
    lda #$A0
    sta zp_memcpy_len_lo
    lda #$10
    sta zp_memcpy_len_hi
    jsr build_line_table
    lda #$1A
    sta $C6
    lda #$32
    sta zp_render_scratch
    jmp render_copy_5DFA
wait_frames_7FF2:
    ; Wait loop polling RTCLOK (frame sync); Sets color registers; Configures player-missile graphics; Writes to screen memory; Modifies display list; Bit extraction/packing via shifts and rotates; Reads: GRAFP3; Writes: HPOSP2, HPOSP3, HPOSM1, HPOSM2; Calls: pmg_fill_4548, render_4441, set_pmg_positions, read_joystick; Table access: $00D0, $0741, $BC00, $BD00; 201 instructions, 5 loops
    cmp $350D
    ora $8D0D
    bvs loc_806A
    bmi loc_7FC2
    lda BUFRLO,x
    bmi loc_8004
    bcc loc_8046
    .byte $2D                     ; $8002
    .byte $33                     ; $8003
loc_8004:
    .byte $04                     ; $8004
    :5 .byte $04                    ; $8005 (5 bytes)
    .byte $84,$04,$04,$41                                  ; $800A
.proc array_op_800E
    ; Table access: $803A, $803E, $BE00, $BF00; 25 instructions, 2 loops
    ldx #$00
loc_8010:
    txa
    ldy #$04
    sty zp_memcpy_dst_lo
loc_8015:
    pha
    and #$03
    tay
    lda $803A,y
    lsr
    ror pix_blend_cont,x
    lsr
    ror pix_blend_cont,x
    lda $803E,y
    lsr
    ror pix_blend_merge,x
    lsr
    ror pix_blend_merge,x
    pla
    lsr
    lsr
    dec zp_memcpy_dst_lo
    bne loc_8015
    inx
    bne loc_8010
    rts
.endp

    .byte $03                                              ; $803A
    :6 .byte $00                    ; $803B (6 zero bytes)
    .byte $03                                              ; $8041
util_fill_8042:
    ; Fill loop — indexed store of constant value; Table access: $BC00, $BD00; 14 instructions, 2 loops
    ldy #$00
    lda #$80
loc_8046:
    sta pix_color_mask,y
    lsr
    lsr
    bcc loc_804E
    ror
loc_804E:
    iny
    bne loc_8046
    tya
loc_8052:
    sta pix_byte_offset,y
    iny
    bne loc_8052
    rts
gameplay_vbi:
    ; GAMEPLAY VBI — Reset DLI counter, clear player sizes, set CHBASE, load sky/background colors from palette, handle attract mode color cycling, position missiles for crosshair, swap display lists based on game state, dispatch rendering pipeline
    lda #$00
    sta $C8
    sta SIZEP0
    sta SIZEP1
    lda #$E0
    sta CHBASE
    lda FR0M
loc_806A:
    sta COLBK
    sta COLPM0
    sta COLPM1
    lda $2850
    sta COLPM2
    lda QTEMP
    sta COLPF0
    lda $D8
    sta COLPF1
    lda FRE
    sta COLPM3
    lda $CC
    sta HPOSP2
    lda $CE
    sta SIZEP2
    lda $2877
    sta HPOSP3
    lda GPRIOR
    sta PRIOR
    inc $14
    bne loc_80C5
    lda $0632
    ora difficulty_level
    beq loc_80C5
    inc RTCLOK
    inc $0631
    bpl loc_80C5
    lda #$80
    sta $0631
    ldy #$0E
loc_80B8:
    lda $0741,y
    eor RTCLOK
    and #$F6
    sta $00D0,y
    dey
    bpl loc_80B8
loc_80C5:
    clc
    lda render_state_flag
    sta HPOSM3
    adc #$0C
    sta HPOSM2
    adc #$05
    sta HPOSM1
    lda #$CC
    sta SIZEM
    lda $0626
    beq loc_8105
    lda $A7
    beq loc_8105
    lda $A7
    cmp #$01
    bne loc_80F7
    lda #$6B
    sta DLISTL
    lda #$31
    sta DLISTH
    jmp loc_8101
loc_80F7:
    lda #$10
    sta DLISTL
    lda #$32
    sta DLISTH
loc_8101:
    lda #$00
    sta $A7
loc_8105:
    dec $0642
    bne loc_810D
    jmp loc_819B
loc_810D:
    lda FRE
    lsr
    bcc loc_8119
    sbc #$01
    cmp #$4B
    jmp loc_811D
loc_8119:
    adc #$01
    cmp #$4E
loc_811D:
    rol
    sta FRE
    ldx $284E
    beq loc_812D
    lda $0632
    bne loc_812D
    jsr set_pmg_positions
loc_812D:
    lda $0626
    beq loc_8178
    lda $2823
    bmi loc_813D
    jsr pmg_fill_45C5
    jmp loc_8147
loc_813D:
    cmp #$FF
    bne loc_8144
    jsr pmg_fill_45C5
loc_8144:
    jsr pmg_fill_478C
loc_8147:
    jsr render_loop_432A
    lda camera_view_param
    bpl loc_8170
    dec $281B
    lda #$08
    bit $281B
    bne loc_8166
    lda #$35
    sta $3356
    lda #$36
    sta $3357
    jmp loc_8170
loc_8166:
    lda #$B5
    sta $3356
    lda #$B6
    sta $3357
loc_8170:
    lda $287F
    beq loc_8178
    jsr draw_447D
loc_8178:
    lda dirty_flag
    bmi loc_8188
    dec dirty_flag
    bpl loc_8185
    jsr clear_hud_line
loc_8185:
    jmp loc_8190
loc_8188:
    ldy $0635
    beq loc_8190
    jsr render_copy_4AE7
loc_8190:
    lda score_hi
    beq loc_8198
    jsr math_update_score_4C91
loc_8198:
    jmp loc_829F
loc_819B:
    jsr pmg_fill_4548
    lda #$02
    sta $0642
    ldy game_mode
    beq loc_81B4
    ldy $283C
    bne loc_81D3
    ldy $0626
    bne loc_81FA
    beq loc_820E
loc_81B4:
    ldy $0632
    bne loc_81FD
    cmp $0626
    bne loc_820E
    lda TRIG0
    bne loc_81FA
    lda difficulty_level
    beq loc_81D3
    sta dirty_flag
    lda #$4F
    sta $0635
    jmp loc_81FA
loc_81D3:
    lda $284E
    bne loc_81FA
    lda #$7C
    sta $2851
    lda #$00
    sta $2871
    sta $2852
    sta $283C
    lda #$78
    sta $2850
    lda #$0C
    sta $2873
    lda #$01
    sta $2872
    sta $284E
loc_81FA:
    jsr render_4441
loc_81FD:
    ldx #$FF
    cli
    sei
    txa
    bmi loc_820E
    jsr setup_irq_vectors
    ldx FR1
    beq loc_820E
    jsr queue_push_flagged
loc_820E:
    lda $0626
    bne loc_8216
    jmp loc_829F
loc_8216:
    jsr read_joystick
    jsr render_97A9
    lda viewport_state
    bne loc_826E
    lda $0631
    bmi loc_826E
    sec
    lda cam_altitude
    sbc #$32
    bcc loc_8238
    lsr
    cmp #$07
    bcc loc_8235
    lda #$06
loc_8235:
    jmp loc_823A
loc_8238:
    lda #$00
loc_823A:
    clc
    adc $0889
    tay
    lda $07FD,y
    sta $DB
    lda $0820,y
    sta $DD
    lda $0843,y
    sta $DC
    lda $0866,y
    sta $DE
    lda $0608
    bne loc_825B
    dec $088A
loc_825B:
    bne loc_826E
    inc $088B
    lda $088B
    lsr
    lsr
    and #$0F
    tay
    lda $524D,y
    sta $0889
loc_826E:
    jsr render_copy_43C0
    jsr render_fill_43F5
    jsr render_copy_430E
    jsr render_loop_46E6
    jsr util_copy_47FB
    dec $065E
    bne loc_829F
    lda $284D
    beq loc_828F
    dec $284D
    bne loc_828F
    jsr draw_65A9
loc_828F:
    lda $061D
    beq loc_829A
    inc score_lo
    jsr math_update_score_4C91
loc_829A:
    lda #$1E
    sta $065E
loc_829F:
    lda $0632
    bne loc_82B2
    jsr render_82B5
loc_82A7:
    jsr sound_dispatch_9119
vbi_deferred:
    ; DEFERRED VBI — Secondary VBI processing: game state updates, collision checks, timing
    lda slot_available
    beq loc_82B2
    jsr sound_ambient
loc_82B2:
    jmp loc_E462
render_82B5:
    ; Writes to screen memory; Arithmetic with shifts; Calls: bit_extract_8329, bit_extract_8329; Table access: $071B; 69 instructions
    lda $2842
    bne loc_82BB
    rts
loc_82BB:
    lda ent_type
    cmp #$02
    beq loc_8306
    lda $282A
    bmi loc_8306
    eor #$7F
    lsr
    lsr
    sec
    sbc #$10
    sta $2842
    lda $282D
    sta zp_screen_ptr2_lo
    lda $282C
    jsr bit_extract_8329
    sta zp_screen_ptr_lo
    lda $2830
    sta zp_screen_ptr2_lo
    lda $282F
    jsr bit_extract_8329
    clc
    adc zp_screen_ptr_lo
    sta zp_screen_ptr_lo
    sec
    lda #$0F
    sbc zp_screen_ptr_lo
    clc
    adc $2842
    bpl loc_82FB
    lda #$00
loc_82FB:
    lsr
    lsr
    clc
    adc #$02
    sta $2842
    jmp loc_830F
loc_8306:
    lda $14
    and #$0F
    bne loc_830F
    dec $2842
loc_830F:
    lda $2842
    sta $0675
    lda #$A0
    sta $0667
    lda $14
    and #$05
    clc
    adc #$02
    sta $0683
    ldy #$08
    jmp queue_push
.proc bit_extract_8329
    ; Bit extraction/packing via shifts and rotates; 15 instructions
    lsr zp_screen_ptr2_lo
    ror
    lsr zp_screen_ptr2_lo
    ror
    lsr zp_screen_ptr2_lo
    ror
    lsr zp_screen_ptr2_lo
    ror
    sec
    sbc #$08
    bpl loc_833F
    eor #$FF
    clc
    adc #$01
loc_833F:
    rts
.endp

draw_8340:
    ; Writes to screen memory; Table access: $071B; 29 instructions
    lda #$00
    sta $2842
    jmp loc_830F
stub_8348:
    ; 1 instructions
    jmp stub_8348
update_pmg_colors_834B:
    ; Sets color registers; Configures player-missile graphics; Writes: HPOSP1, HPOSM0, HPOSM1, HPOSM2; Calls: update_pmg_data, render_copy_83BD, pmg_fill_4548, sound_dispatch_9119; 55 instructions
    lda #$00
    sta $C8
    lda #$E0
    sta CHBASE
    lda QTEMP
    sta COLPF0
    lda COLOR3
    sta COLPF3
    lda COLOR4
    sta COLBK
    lda #$78
    sta COLPF1
    lda PCOLR0
    sta COLPM0
    lda PCOLR1
    sta COLPM1
    lda PCOLR2
    sta COLPM2
    lda PCOLR3
    sta COLPM3
    lda zp_temp
    sta HPOSP1
    lda GPRIOR
    sta PRIOR
    lda #$3B
    sta HPOSM2
    lda #$39
    sta HPOSM3
    lda #$C5
    sta HPOSM0
    lda #$C3
    sta HPOSM1
    inc $14
    inc RTCLOK
    bne loc_83A9
    inc ATRACT
loc_83A9:
    jsr update_pmg_data
    jsr render_copy_83BD
    lsr $0642
    bne loc_83BA
    jsr pmg_fill_4548
    inc $0642
loc_83BA:
    jmp loc_82A7
render_copy_83BD:
    ; Copy loop — indexed load/store transfer; Writes to screen memory; Calls: game_logic_proc_5EE9, arith_5ECE, arith_5ECE, render_proc_5AE7; 198 instructions, 3 loops
    lda $08DD
    beq loc_83C5
    jmp sub_5EA1
loc_83C5:
    lda $08D8
    beq loc_83CD
    jmp loc_5E45
loc_83CD:
    lda $08D9
    beq loc_83D5
    jmp copy_5F16
loc_83D5:
    lda $08DB
    beq loc_83DD
    jmp render_copy_5DEB
loc_83DD:
    dec $08CC
    bne loc_83F7
    lda #$02
    sta $08CC
    lda $08DC
    beq loc_83EF
    jsr render_table_lookup_5E32
loc_83EF:
    lda $08DA
    beq loc_83F7
    jmp sub_5D50
loc_83F7:
    rts
update_pmg_data:
    ; Installs interrupt/event vectors; 21 instructions
    ldx #$FF
    cli
    sei
    txa
    bmi loc_8406
    cmp #$34
    bne loc_8406
    jmp loc_4988
loc_8406:
    stx $0623
    rts
vbi_title_screen:
    ; TITLE SCREEN VBI — Handles attract mode cycling and title screen animations
    lda SDMCTL
    sta DMACTL
    inc $14
    inc slow_timer
    bne loc_841B
    inc ATRACT
    inc $13
loc_841B:
    ldy ATRACT
    bpl loc_8423
    ldy #$80
    sty ATRACT
loc_8423:
    ldx #$04
loc_8425:
    lda COLOR0,x
    cpy #$80
    bcc loc_8430
    eor $13
    and #$F6
loc_8430:
    sta COLPF0,x
    dex
    bpl loc_8425
    jsr update_pmg_data
    jmp vbi_deferred
terrain_init:
    ; INIT TERRAIN — Fills 256-byte terrain buffer with random unresolved heights (bit 7 set), then starts fractal midpoint displacement subdivision
    ldy #$00
    ldx #$00
loc_8440:
    lda RANDOM
    and #$3F
    ora #$80
    sta dat_0900,x
    inx
    dey
    bne loc_8440
    ldy #$01
    sty zp_frac_depth
    lda RANDOM
    sta zp_frac_seed_pos
    tax
    lda RANDOM
    and #$3F
    sta dat_0900,x
loc_8460:
    ; FRACTAL MAIN LOOP — Midpoint displacement: checks neighbors, computes average + random perturbation, places resolved heights. Uses recursion stack at $24BE
    lda zp_frac_depth
    sta zp_frac_prev_depth
    jsr terrain_subdivide
    lda zp_frac_depth
    cmp zp_frac_prev_depth
    beq loc_84B5
loc_846D:
    bit RANDOM
    bvc loc_8480
    bmi loc_847A
    lda #$10
    ldx #$FF
    bne loc_848C
loc_847A:
    lda #$F0
    ldx #$01
    bne loc_848C
loc_8480:
    bmi loc_8488
    lda #$01
    ldx #$10
    bne loc_848C
loc_8488:
    lda #$FF
    ldx #$F0
loc_848C:
    sta zp_frac_delta_x
    stx zp_frac_delta_y
    jsr terrain_check_neighbors
    bpl loc_846D
    lda zp_frac_seed_pos
    clc
    adc zp_frac_delta_x
    tax
    lda RANDOM
    and #$3F
    sta dat_0900,x
    txa
    clc
    adc zp_frac_delta_x
    tax
    lda RANDOM
    and #$3F
    sta dat_0900,x
    stx zp_frac_seed_pos
    jmp loc_8460
loc_84B5:
    ; FRACTAL POP — Decreases recursion depth, pops from stack at $24BE or returns if depth exhausted
    dec zp_frac_depth
    bne loc_84BA
    rts
loc_84BA:
    ldx zp_frac_depth
    lda dat_24BE,x
    sta zp_frac_seed_pos
    jmp loc_8460
.proc terrain_check_neighbors
    ; Table access: $0900; 20 instructions
    lda zp_frac_seed_pos
    clc
    adc zp_frac_delta_x
    tax
    lda dat_0900,x
    bpl loc_84E3
    txa
    clc
    adc zp_frac_delta_x
    tay
    lda dat_0900,y
    bpl loc_84E3
    txa
    clc
    adc zp_frac_delta_y
    tax
    lda dat_0900,x
    bmi loc_84E5
loc_84E3:
    lda #$00
loc_84E5:
    rts
.endp

.proc terrain_subdivide
    ; Writes to screen memory; Calls: terrain_check_neighbors, terrain_push_seed, terrain_check_neighbors, terrain_push_seed; Table access: $24BE; 34 instructions
    lda #$01
    sta zp_frac_delta_x
    lda #$10
    sta zp_frac_delta_y
    jsr terrain_check_neighbors
    bpl loc_84F6
    jsr terrain_push_seed
loc_84F6:
    lda #$FF
    sta zp_frac_delta_x
    lda #$F0
    sta zp_frac_delta_y
    jsr terrain_check_neighbors
    bpl loc_8506
    jsr terrain_push_seed
loc_8506:
    lda #$10
    sta zp_frac_delta_x
    lda #$FF
    sta zp_frac_delta_y
    jsr terrain_check_neighbors
    bpl loc_8516
    jsr terrain_push_seed
loc_8516:
    lda #$F0
    sta zp_frac_delta_x
    lda #$01
    sta zp_frac_delta_y
    jsr terrain_check_neighbors
    bpl loc_8525
    bmi terrain_push_seed
loc_8525:
    rts
.endp

.proc terrain_push_seed
    ; PUSH SEED — Saves current terrain seed to recursion stack, increments depth counter
    ldx zp_frac_depth
    lda zp_frac_seed_pos
    sta dat_24BE,x
    inc zp_frac_depth
    rts
.endp