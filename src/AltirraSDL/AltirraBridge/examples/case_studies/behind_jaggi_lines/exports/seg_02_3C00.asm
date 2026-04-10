; =============================================================
; Segment 2: $3C00-$52E6 — Game code + data: init, title screen, input, display lists
; =============================================================
; XEX load range: $3C00-$52E6 (5863 bytes)

    org $3C00

game_start:
    ; GAME ENTRY POINT — Initializes hardware, disables IRQ, sets up VBI/DLI vectors, configures PMBASE, CHBASE, SKCTL. Branches to title screen loop.
    lda #$00
    ldy #$07
loc_3C04:
    sta $0600,y
    dey
    bpl loc_3C04
    jsr memzero
    lda #$00
    sta slot_available
    sta game_timer
    sta $0740
    sta game_mode
    sta $061E
    sei
    lda #$40
    sta VIMIRQ
    lda #$49
    sta $0217
    jsr sound_init
    lda #$0D
    sta DOSINI
    lda #$3C
    sta $0D
loc_3C34:
    lda #$00
    sta SDMCTL
    sta GRACTL
    sta COLOR4
    jsr setup_gameplay_display
    lda #$0A
    sta VVBLKI
    lda #$84
    sta $0223
    jsr enable_display
    lda #$D5
    sta DLISTL
    lda #$41
    sta DLISTH
    lda #$00
    sta ATRACT
    sta $DD
    sta COLDST
    sta $A7
    sta $C8
    ldy #$24
loc_3C68:
    dey
    sta $061F,y
    bne loc_3C68
    lda #$08
    sta PMBASE
    lda #$E0
    sta CHBASE
    lda #$03
    sta SKCTL
    jsr game_init
    lda #$00
    sta zp_memcpy_dst_lo
    lda #$10
    sta zp_memcpy_dst_hi
    lda #$FF
    sta zp_memcpy_len_lo
    lda #$1F
    sta zp_memcpy_len_hi
    jsr memcpy_block
    jsr util_fill_8042
    jsr array_op_800E
    jsr render_fill_7D27
    lda #$E7
    sta zp_screen_ptr_lo
    lda #$50
    sta zp_screen_ptr_hi
    lda #$0D
    sta zp_screen_ptr2_lo
    lda #$35
    sta zp_screen_ptr2_hi
    jsr render_fill_7D18
    jsr render_fill_7EF9
    lda #$C0
    sta IRQEN
    jsr pmg_fill_4066
loc_3CBA:
    lda CONSOL
    and #$01
    beq loc_3CBA
    jsr render_fill_457F
    ldy #$09
    jsr render_loop_46ED
    jsr render_fill_4923
    lda #$08
    jsr set_viewport_offset
    lda #$2C
    sta COLOR2
    lda #$26
    sta COLOR3
    ldy #$08
loc_3CDD:
    lda $5205,y
    sta $00D0,y
    dey
    bpl loc_3CDD
    lda #$80
    ldy #$04
loc_3CEA:
    dey
    sta terrain_vis_flag,y
    bne loc_3CEA
    sta $066A
    sta $066B
    jsr render_loop_432A
    lda #$A0
    sta $0665
    sta $066C
    sta ent_flags
    ldx #$1F
    jsr entity_load_state
    inx
    jsr entity_load_state
    lda #$02
    sta $0642
    lda #$C0
    sta NMIEN
    sta $0612
    lda #$0D
    sta $3157
    lda #$35
    sta $3158
loc_3D24:
    jsr level_init
    lda #$2A
    jsr display_fill_4058
    jsr wait_1
    lda #$0D
    sta $3157
    lda #$35
    sta $3158
    lda #$00
    sta zp_random_work
    sta $A7
    sta viewport_state
    jsr clear_screen
    jsr loop_util_48BE
    jsr util_fill_5F91
    jsr render_fill_7CF5
    lda #$37
    sta zp_memcpy_dst_lo
    lda #$28
    sta zp_memcpy_dst_hi
    lda #$B8
    sta zp_memcpy_len_lo
    lda #$00
    sta zp_memcpy_len_hi
    jsr memcpy_block
    lda #$31
    sta zp_memcpy_dst_lo
    lda #$0B
    sta zp_memcpy_dst_hi
    lda #$56
    sta zp_memcpy_len_lo
    lda #$00
    sta zp_memcpy_len_hi
    jsr setup_gameplay_display
    lda #$59
    sta VVBLKI
    lda #$80
    sta $0223
    jsr memcpy_block
    lda #$11
    sta GPRIOR
    jsr util_copy_490B
    jsr enable_display
    lda #$F1
    sta VDSLST
    lda #$4C
    sta $0201
    lda #$6B
    sta DLISTL
    lda #$31
    sta DLISTH
    lda #$40
    sta HPOSM0
    jsr render_fill_4278
    lda $0621
    bne loc_3DB3
    jsr terrain_init
    jsr util_fill_7B52
loc_3DB3:
    lda #$60
    sta zp_memcpy_dst_lo
    lda #$10
    sta zp_memcpy_len_lo
    lda #$10
    sta zp_memcpy_len_hi
    jsr build_line_table
    jsr render_copy_7B2D
    lda game_mode
    bne loc_3DCF
    lda #$02
    jmp loc_3DD1
loc_3DCF:
    lda #$01
loc_3DD1:
    sta $0626
loc_3DD4:
    jsr transform_vertex
    ldx #$33
    jsr draw_AE3E
    ldx #$30
    jsr terrain_render
    ldx #$33
    jsr fill_column_fast
    lda viewport_state
    sta $28A8
    jsr render_random_perturb_AA2F
    lda #$02
    sta $A7
    lda $0634
    beq loc_3DFB
    jmp loc_3F1A
loc_3DFB:
    lda $0625
    cmp #$0E
    bcs loc_3E17
    lda game_mode
    beq loc_3E0D
    jsr game_logic_get_random_6539
    jmp loc_3E17
loc_3E0D:
    lda camera_view_param
    cmp #$01
    bne loc_3E17
    jsr draw_65A9
loc_3E17:
    lda game_event_flag
    beq loc_3E1F
    jsr render_random_perturb_64A9
loc_3E1F:
    jsr transform_vertex
    ldx #$03
    jsr draw_AE3E
    ldx #$00
    jsr terrain_render
    ldx #$03
    jsr fill_column_fast
    lda viewport_state
    beq loc_3E39
    sta $28A8
loc_3E39:
    jsr render_random_perturb_AA2F
    lda $0634
    beq loc_3E44
    jmp loc_3F1A
loc_3E44:
    lda $28A7
    beq loc_3E51
    lda $28A6
    beq loc_3E51
    jsr wait_frames_62AB
loc_3E51:
    lda $28A6
    sta $28A7
    lda $28A8
    bne loc_3E62
    lda difficulty_level
    jmp loc_3E64
loc_3E62:
    lda #$00
loc_3E64:
    sta $28A6
    lda #$01
    sta $A7
    cmp difficulty_level
    beq loc_3EAB
    ldx ent_type
    cpx #$02
    beq loc_3EAB
    lda flight_param
    beq loc_3EA3
    ldx #$3A
    lda #$02
    sta flight_param
    lda terrain_near_dist
    bpl loc_3E8D
    ldy #$A8
    jmp loc_3E9D
loc_3E8D:
    cmp #$08
    bcc loc_3E98
    ldx #$1A
    ldy #$A7
    jmp loc_3E9D
loc_3E98:
    ldy #$86
    inc flight_param
loc_3E9D:
    stx $0637
    sty $0635
loc_3EA3:
    lda game_event_flag
    beq loc_3EAB
    jsr render_random_perturb_64A9
loc_3EAB:
    ldx ent_type
    cpx #$02
    beq loc_3EB5
    jmp loc_3DD4
loc_3EB5:
    lda #$03
    sta $0626
    ldy #$80
    sty $28F4
    sty $28F5
    lda cam_altitude
    cmp #$40
    bcs loc_3ECC
    jmp loc_3DD4
loc_3ECC:
    lda $283F
    bpl loc_3ECC
    sty terrain_vis_flag
    lda #$00
    sta $063C
    sta terrain_detail
    jsr wait_10
    sta $0626
    jsr wait_n_frames
    ldy #$A2
loc_3EE7:
    sta $0F1E,y
    dey
    bpl loc_3EE7
    ldy #$1E
loc_3EEF:
    sta $0E8F,y
    dey
    bpl loc_3EEF
    lda $DF
    cmp #$4E
    bne loc_3EFF
    lda #$46
    sta $DF
loc_3EFF:
    jsr render_fill_4923
    lda $DE
    sta viewport_state
    sta $0612
    jsr display_fill_4058
    lda #$00
    ldy #$03
    sta ent_flags,y
    jsr queue_push
    jmp loc_3D24
loc_3F1A:
    lda #$80
    sta terrain_vis_flag
    jsr sound_init
    lda #$00
    sta $0626
    sta $2877
    sta $CD
    sta zp_memcpy_dst_lo
    lda #$3C
    sta zp_memcpy_len_lo
    lda #$20
    sta zp_memcpy_dst_hi
loc_3F36:
    jsr timing_get_random_3F97
    bne loc_3F3E
    jsr render_proc_4031
loc_3F3E:
    bpl loc_3F43
    jsr display_copy_4043
loc_3F43:
    ldx zp_memcpy_dst_hi
    cpx #$2B
    bne loc_3F36
loc_3F49:
    ldy #$00
    sty DMACTL
    jsr wait_60
loc_3F51:
    dec $067A
    ldy #$0D
    txa
    pha
    jsr queue_push
    pla
    tax
    stx FR0M
    jsr wait_2
    dex
    cpx #$1F
    bne loc_3F51
    lda #$00
    sta FR0M
    sta $061E
    jsr sound_landing
    jsr wait_60
    jmp loc_3C34
loc_3F77:
    lda #$00
    sta zp_memcpy_dst_lo
    lda #$34
    sta $3355
    lda #$1E
    sta zp_memcpy_len_lo
loc_3F84:
    jsr timing_get_random_3F97
    bne loc_3F84
    lda #$00
    sta viewport_state
    lda $0634
    beq loc_3F96
    jmp loc_3F1A
loc_3F96:
    rts
.proc timing_get_random_3F97
    ; Reads POKEY RANDOM for randomization; Writes to screen memory; Reads: SKRES; Calls: queue_push_flagged, wait_2, queue_push_flagged; 20 instructions
    dec zp_memcpy_dst_lo
    bpl loc_3FAD
    lda RANDOM
    and #$07
    ora #$01
    sta zp_memcpy_dst_lo
    ldx #$0E
    jsr queue_push_flagged
    inx
    jsr queue_push_flagged
loc_3FAD:
    lda RANDOM
    ora #$04
    sta viewport_state
    sta $DB
    sta $DD
    sta $DC
    sta $DE
    jsr wait_2
    dec zp_memcpy_len_lo
    rts
.endp

loc_3FC3:
    jsr update_sound_3FE2
    jsr render_proc_4031
    jsr wait_20
    lda #$20
    sta zp_memcpy_dst_hi
    sta viewport_state
loc_3FD3:
    jsr display_copy_4043
    jsr wait_2
    ldx zp_memcpy_dst_hi
    cpx #$2B
    bne loc_3FD3
    jmp loc_3F49
.proc update_sound_3FE2
    ; Updates sound registers; Reads: SKRES; Writes: AUDF1, AUDC1, AUDF2, AUDC2; Calls: wait_1; Table access: $401B; 27 instructions, 1 loop
    lda #$15
    sta zp_memcpy_dst_lo
    lda #$00
    sta AUDCTL
loc_3FEB:
    lda RANDOM
    bpl loc_3FF5
    and #$0F
    sta AUDF1
loc_3FF5:
    ldx zp_memcpy_dst_lo
    lda $401B,x
    tay
    lsr
    ora #$A0
    sta AUDC1
    tya
    ora #$80
    sta AUDC2
    tya
    asl
    eor #$1F
    sta AUDF2
    jsr wait_1
    dec zp_memcpy_dst_lo
    bpl loc_3FEB
    lda #$60
    sta AUDCTL
    rts
.endp

    .byte $00,$01,$01,$02,$02,$03,$03,$04,$04,$05,$05,$06,$06,$07,$08,$09 ; $401B
    .byte $0A,$0B,$0C,$0D,$0E,$0F                          ; $402B
render_proc_4031:
    ; Table access: $071B; 22 instructions
    lda #$00
    sta $066C
    sta $0688
    lda #$0C
    sta $067A
    ldy #$0D
    jmp queue_push
.proc display_copy_4043
    ; Copy loop — indexed load/store transfer; Sets color registers; Writes: COLPF3; Table access: $00D0, $5205; 10 instructions, 1 loop
    ldy #$07
loc_4045:
    lda $5205,y
    and #$0F
    ora zp_memcpy_dst_hi
    sta $00D0,y
    dey
    bpl loc_4045
    sta COLPF3
    inc zp_memcpy_dst_hi
    rts
.endp

.proc display_fill_4058
    ; Fill loop — indexed store of constant value; Sets color registers; Table access: $00DB, $02C0; 7 instructions, 1 loop
    ldy #$03
loc_405A:
    sta $00DB,y
    sta PCOLR0,y
    dey
    bpl loc_405A
    sta FRE
    rts
.endp

pmg_fill_4066:
    ; Fill loop — indexed store of constant value; Sets color registers; Configures player-missile graphics; Bit extraction/packing via shifts and rotates; Reads: GRAFP3, CONSOL, SKCTL; Writes: HPOSP0; Calls: sound_thrust, calc_4CB6, calc_7DCB, calc_4CC8; Table access: $088B, $0910, $41F3, $D000; 179 instructions, 5 loops
    ldy #$84
loc_4068:
    lda $41F3,y
    sta $088B,y
    dey
    bne loc_4068
    lda #$00
    ldy #$07
loc_4075:
    sta HPOSP0,y
    sta $0910,y
    dey
    bpl loc_4075
    lda slot_available
    ora $0740
    bne loc_4089
    jsr sound_thrust
loc_4089:
    lda CONSOL
    and #$01
    beq loc_4089
    lda game_mode
    bne loc_40E1
    lda game_ready_flag
    ldy game_active_flag
    ldx game_phase_timer
    cmp $0600
    bne loc_40AB
    cpy $0601
    bne loc_40AB
    cpx $0602
loc_40AB:
    bcc loc_40B6
    sta $0600
    sty $0601
    stx $0602
loc_40B6:
    lda #$ED
    sta $C6
    lda #$08
    sta zp_render_scratch
    lda $0606
    jsr calc_7DCB
    jsr calc_4CC8
    lda #$11
    sta $C6
    lda #$09
    sta zp_render_scratch
    lda $0600
    jsr calc_4CC8
    lda $0601
    jsr calc_4CD2
    lda $0602
    jsr calc_4CD2
loc_40E1:
    lda #$FD
    sta $C6
    lda #$08
    sta zp_render_scratch
    jsr calc_4CB6
    lda #$00
    sta game_ready_flag
    sta game_active_flag
    sta game_phase_timer
    lda #$2A
    sta COLOR1
    lda #$38
    sta COLOR0
    lda #$46
    sta COLOR2
    lda #$56
    sta COLOR3
    lda #$04
    sta zp_memcpy_len_lo
    jsr enable_display
    lda #$22
    sta SDMCTL
    lda game_mode
    beq loc_411F
    jmp loc_41B7
loc_411F:
    lda #$D9
    sta $C6
    lda #$08
    sta zp_render_scratch
    lda game_timer
    beq loc_4170
    bne loc_417F
loc_412E:
    lda CONSOL
    asl
    and SKSTAT
    and #$04
    bne loc_4143
    sta ATRACT
    ldy $061E
    beq loc_4143
    jmp loc_41A3
loc_4143:
    ldy $061E
    beq loc_414B
    jmp loc_418A
loc_414B:
    dec zp_memcpy_len_lo
    bne loc_418A
    inc zp_memcpy_len_lo
    lda CONSOL
    and #$02
    bne loc_418A
    ldy #$04
    sty zp_memcpy_len_lo
    lda SKSTAT
    and #$08
    bne loc_4170
    dec game_timer
    lda game_timer
    bne loc_416D
    lda #$63
loc_416D:
    jmp loc_417C
loc_4170:
    inc game_timer
    lda game_timer
    cmp #$64
    bne loc_417C
    lda #$01
loc_417C:
    sta game_timer
loc_417F:
    jsr calc_7DCB
    ldy #$00
    sty $08D9
    jsr calc_4CC8
loc_418A:
    lda slot_available
    beq loc_4195
    jsr wait_2
    jmp loc_41AD
loc_4195:
    jsr sound_explosion
    lda CONSOL
    and #$01
    sec
    sbc TRIG0
    beq loc_41AD
loc_41A3:
    jsr sound_laser
    sta SDMCTL
    sta $061E
    rts
loc_41AD:
    lda CONSOL
    and #$04
    beq loc_41B7
    jmp loc_412E
loc_41B7:
    inc $0607
    lda $0607
    cmp #$64
    bne loc_41C6
    lda #$01
    sta $0607
loc_41C6:
    lda slot_available
    bne loc_41C6
    lda #$10
    sta game_timer
    sta game_mode
    bne loc_41A3
    bvs loc_4247
    bvs loc_4249
    bvs loc_424B
    bvs loc_4224
    sty $7008
    bvs loc_4252
    asl POKMSK
    asl ROWAC
    bvs loc_4258
    bvs loc_41F0
    bpl loc_41F2
    bvs loc_425E
    asl POKMSK
loc_41F0:
    asl SOUNDR
loc_41F2:
    cmp SOUNDR,x
    brk
    .byte $62,$65,$68,$69,$6E,$64,$00,$6A,$61,$67,$67,$69,$00,$6C,$69,$6E ; $41F5
    .byte $65,$73,$41,$00,$23,$2F,$30,$39,$32,$29,$27,$28,$34,$00,$08,$23 ; $4205
    .byte $09,$00,$11,$19,$18,$14                          ; $4215
    :4 .byte $00                    ; $421B (4 zero bytes)
    .byte $2C,$35,$23,$21,$33                              ; $421F
loc_4224:
    rol ICBLHZ
    bit.w $002D
    bit $2434
    asl.w LNFLG
    brk
    .byte $00,$B3,$B4,$A1,$B2,$B4,$A9,$AE,$A7,$00,$AC,$A5,$B6,$A5,$AC,$9A ; $4230
    :6 .byte $00                    ; $4240 (6 zero bytes)
    .byte $B2                                              ; $4246
loc_4247:
    lda (zp_mul_sign,x)
loc_4249:
    .byte $AB,$A9                                          ; $4249
loc_424B:
    ldx.w $00A7
    ldy $B6A5
    .byte $A5                     ; $4251
loc_4252:
    .byte $AC                     ; $4252
    txs
    brk
    brk
    .byte $00,$00                                          ; $4256
loc_4258:
    brk
    cpx $F3E1
    .byte $F4
    brk
loc_425E:
    .byte $F3
    .byte $E3
    .byte $EF
    .byte $F2
    sbc FRE
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    brk
    inx
    sbc #$E7
    inx
    brk
    .byte $F3
    .byte $E3
    .byte $EF
    .byte $F2
    sbc FRE
render_fill_4278:
    ; Fill loop — indexed store of constant value; Writes to screen memory; Reads: SKRES; Calls: wait_n_frames, wait_n_frames, render_copy_430E, mirror_charset; 118 instructions, 5 loops
    ldy #$0E
    lda #$20
loc_427C:
    sta $0B4C,y
    sta $0B63,y
    dey
    bne loc_427C
    lda #$CC
    sta $0B5F
    sta $2823
    lda #$4D
    sta $2898
    lda #$4F
    sta cam_altitude
    lda #$1E
    sta $065E
    sta frame_counter
    jsr wait_n_frames
    tay
    lda #$FF
loc_42A5:
    sta $0E94,y
    dey
    bpl loc_42A5
    sta terrain_scroll_accum
    ldy #$5F
loc_42B0:
    sta $2210,y
    dey
    bpl loc_42B0
    sta $282A
    sta $2829
    sta ent_type
    ldy #$37
    lda #$C0
loc_42C3:
    sta $0B98,y
    dey
    bpl loc_42C3
    jsr wait_n_frames
    lda #$0E
    sta $281E
    lda #$02
    sta $289A
    jsr render_copy_430E
    jsr mirror_charset
    jsr wait_n_frames
    jsr draw_48E2
    jsr wait_n_frames
    lda #$38
    jsr render_copy_9943
    lda #$38
    jsr render_copy_99A9
    jsr wait_n_frames
    lda #$F4
    sta $288E
    lda #$91
    sta $BA
    lda #$0B
    sta $BB
    lda RANDOM
    cmp #$1F
    bcs loc_4309
    inc camera_view_param
loc_4309:
    lda #$07
    jmp render_4753
.proc render_copy_430E
    ; Copy loop — indexed load/store transfer; Writes to screen memory; Table access: $32E3, $4326, $4E08; 12 instructions, 1 loop
    clc
    lda $281E
    ldy $283A
    adc dat_4326,y
    tay
    ldx #$03
loc_431B:
    lda $4E08,y
    sta $32E3,x
    dey
    dex
    bpl loc_431B
    rts
.endp

dat_4326:
    .byte $03,$16,$29,$3C                                  ; $4326
render_loop_432A:
    ; Calls: render_proc_43A5, render_loop_4394, render_proc_43A9; Table access: $4DE0; 73 instructions, 1 loop
    lda #$00
    sta zp_color_work
    lda terrain_detail
    cmp $0646
    beq loc_4344
    ldy #$B4
    sty zp_screen_ptr_lo
    ldy #$33
    sty zp_screen_ptr_hi
    sta $0646
    jsr render_proc_43A5
loc_4344:
    lda score_digit
    cmp $0644
    beq loc_4362
    sta $0644
    ldy #$45
    sty zp_screen_ptr2_lo
    ldy #$34
    sty zp_screen_ptr2_hi
    ldy #$13
    sty zp_screen_ptr_lo
    ldy #$34
    sty zp_screen_ptr_hi
    jsr render_loop_4394
loc_4362:
    lda $063C
    beq loc_4375
    dec $063D
    lda #$0C
    bit $063D
    bne loc_4375
    lda #$80
    sta zp_color_work
loc_4375:
    lda display_enabled
    tay
    ora zp_color_work
    cmp $0645
    beq loc_43BF
    sta $0645
    tya
    ldy #$A4
    sty zp_screen_ptr2_lo
    ldy #$34
    sty zp_screen_ptr2_hi
    ldy #$72
    sty zp_screen_ptr_lo
    ldy #$34
    sty zp_screen_ptr_hi
render_loop_4394:
    ; Calls: render_proc_43A9; Table access: $4DE0; 26 instructions, 1 loop
    pha
    lsr
    lsr
    and #$3C
    jsr render_proc_43A9
    lda zp_screen_ptr2_lo
    sta zp_screen_ptr_lo
    lda zp_screen_ptr2_hi
    sta zp_screen_ptr_hi
    pla
render_proc_43A5:
    ; Table access: $4DE0; 16 instructions, 1 loop
    and #$0F
    asl
    asl
render_proc_43A9:
    ; Table access: $4DE0; 13 instructions, 1 loop
    tax
    ldy #$00
loc_43AC:
    lda $4DE0,x
    ora zp_color_work
    sta (zp_screen_ptr_lo),y
    inx
    iny
    cpy #$02
    bne loc_43BB
    ldy #$30
loc_43BB:
    cpy #$32
    bne loc_43AC
loc_43BF:
    rts
.proc render_copy_43C0
    ; Copy loop — indexed load/store transfer; Writes to screen memory; Table access: $0E87, $0E88, $4878, $4E54; 23 instructions, 1 loop
    ldx $2941
    ldy $2942
    cpx $2879
    bne loc_43D0
    cpy $287B
    beq loc_43F4
loc_43D0:
    stx $2879
    lda $4878,x
    tax
    sty $287B
    lda #$00
    sta $0E87,y
    lda #$15
    sta zp_screen_ptr2_lo
loc_43E3:
    iny
    lda $4E54,x
    sta $0E87,y
    inx
    dec zp_screen_ptr2_lo
    bne loc_43E3
    lda #$FF
    sta $0E88,y
loc_43F4:
    rts
.endp

render_fill_43F5:
    ; Fill loop — indexed store of constant value; Writes to screen memory; 31 instructions, 2 loops
    ldy $281C
    cpy $287C
    beq loc_4415
    sty $287C
    lda #$00
loc_4402:
    sta $0C97,y
    dey
    bpl loc_4402
    ldy $287C
    lda #$FF
loc_440D:
    sta $0C98,y
    iny
    cpy #$38
    bcc loc_440D
loc_4415:
    .byte $AC                     ; $4415
    .byte $1D                     ; $4416
loc_4417:
    .byte $28                     ; $4417
    cpy $287D
    beq loc_4440
    sty $287D
    lda $0B96,y
    and #$3F
    sta $0B96,y
    lda $0B97,y
    and #$3F
    sta $0B97,y
    lda $0B98,y
    ora #$C0
    sta $0B98,y
    lda $0B99,y
    ora #$C0
    sta $0B99,y
loc_4440:
    rts
render_4441:
    ; Writes to screen memory; Arithmetic with shifts; Calls: math_helper_44FA, queue_push_flagged, render_loop_476F, draw_8340; Table access: $0D98, $51FE; 79 instructions
    lda $0610
    cmp #$01
    bcc loc_4478
    dec $0610
    bne loc_446D
    lda #$0F
    .byte $8D                     ; $444F
    .byte $10                     ; $4450
loc_4451:
    .byte $06                     ; $4451
    lda difficulty_level
    bne loc_4466
    lda $0625
    beq loc_4466
    lda $282A
    bpl loc_4466
    ldx #$1C
    jsr queue_push_flagged
loc_4466:
    lda #$4E
    sta $DF
    jmp loc_4478
loc_446D:
    lda $0610
    cmp #$0A
    bcs loc_4478
    lda #$46
    sta $DF
loc_4478:
    lda $287F
    beq loc_4483
draw_447D:
    ; Writes to screen memory; Calls: math_helper_44FA, render_loop_476F, draw_8340; Table access: $0D98, $51FE; 41 instructions
    dec $287F
    jmp loc_449E
loc_4483:
    lda #$08
    sec
    sbc $0611
    lsr
    clc
    ldy difficulty_level
    bne loc_4492
    adc #$01
loc_4492:
    adc $287E
    adc $284C
    sta $284C
    bcs loc_449E
    rts
loc_449E:
    lda $0625
    beq loc_44A6
    dec $0625
loc_44A6:
    bne loc_44D9
    lda render_toggle
    beq loc_44C1
    lda ent_type
    cmp #$02
    beq loc_44C1
    lda #$10
    jsr render_loop_476F
    lda #$1E
    sta render_toggle
    jmp loc_44C4
loc_44C1:
    inc $0625
loc_44C4:
    lda $287F
    ora $287E
    cmp #$01
    bcc loc_44D9
    sta $0634
    lda #$00
    sta $0625
    jsr draw_8340
loc_44D9:
    jsr math_helper_44FA
    lsr
    lsr
    lsr
    tax
    lda #$00
    sta $0D98,y
    cpx #$06
    bne loc_44F4
    lda $0610
    bne loc_44F1
    stx $0610
loc_44F1:
    jmp loc_44F9
loc_44F4:
    lda $51FE,x
    sta $DF
loc_44F9:
    rts
.proc math_helper_44FA
    ; 7 instructions
    lda #$DC
    sec
    sbc $0625
    lsr
    lsr
    tay
    rts
.endp

math_copy_4504:
    ; Copy loop — indexed load/store transfer; Arithmetic with shifts; Calls: math_helper_44FA, wait_n_frames; Table access: $0D99, $51FE; 33 instructions, 1 loop
    ldx #$38
    bne loc_4529
math_copy_4508:
    ; Copy loop — indexed load/store transfer; Arithmetic with shifts; Calls: wait_n_frames, math_helper_44FA; Table access: $0D99, $51FE; 31 instructions, 1 loop
    lda #$01
    sta frame_counter
    clc
    lda #$04
    adc $0625
    cmp #$DC
    bcc loc_4523
    lda #$00
    sta frame_counter
    lda #$F0
    sta $0D98
    lda #$DC
loc_4523:
    sta $0625
    jsr wait_n_frames
loc_4529:
    jsr math_helper_44FA
    lda #$F0
    sta $0D99,y
    tya
    lsr
    lsr
    lsr
    tay
    cpy #$06
    bcs loc_453F
    lda #$00
    sta $0610
loc_453F:
    lda $51FE,y
    sta $DF
    dex
    bne math_copy_4508
    rts
pmg_fill_4548:
    ; Fill loop — indexed store of constant value; Configures player-missile graphics; Writes to screen memory; Bit extraction/packing via shifts and rotates; Reads: SKRES; Writes: HPOSP3, SIZEP3; 201 instructions, 7 loops
    ldx $0632
    bne loc_45A9
    lda terrain_vis_flag
    bpl loc_4574
    cmp #$81
    bcs loc_45AA
    dec $060A
    bpl loc_4571
    lda RANDOM
    and #$07
    sta $060A
    cmp #$06
    bcc loc_4568
    lsr
loc_4568:
    tay
    lda $3492,y
    eor #$80
    sta $3492,y
loc_4571:
    jmp loc_45A9
loc_4574:
    bne loc_458C
    inc terrain_vis_flag
    lda $060B
    sta $060A
render_fill_457F:
    ; Fill loop — indexed store of constant value; Writes to screen memory; Table access: $3492; 7 instructions, 1 loop
    lda #$A9
    ldy #$05
loc_4583:
    sta $3492,y
    dey
    bpl loc_4583
    jmp loc_45A9
loc_458C:
    dec $060A
    bpl loc_45A9
    ldy $060B
    sty $060A
    cmp #$07
    beq loc_45A9
    inc terrain_vis_flag
    tay
    lda #$29
loc_45A1:
    sta $3491,y
    ldx #$12
    jmp queue_push_flagged
loc_45A9:
    rts
loc_45AA:
    lsr $0628
    bcs loc_45A9
    inc $0628
    and #$0F
    cmp #$07
    bne loc_45BD
    lda #$06
    dec terrain_vis_flag
loc_45BD:
    dec terrain_vis_flag
    tay
    lda #$A9
    bne loc_45A1
pmg_fill_45C5:
    ; Fill loop — indexed store of constant value; Configures player-missile graphics; Writes to screen memory; Arithmetic with shifts; Reads: SKRES; Writes: HPOSP3, SIZEP3; 140 instructions, 4 loops
    pha
    cmp #$03
    bcc loc_45DA
    lda #$00
    sta SIZEP3
    lda #$0C
    sta $2826
    ldx #$FF
    lda #$6C
    bne loc_460D
loc_45DA:
    tay
    lda $4883,y
    sta SIZEP3
    lda $4886,y
    sta $2826
    sec
    lda $2827
    cmp #$8B
    bcc loc_45F1
    lda #$8C
loc_45F1:
    sbc #$55
    bpl loc_45F7
    lda #$00
loc_45F7:
    lsr
    lsr
    cpy #$01
    bmi loc_4607
    lsr
    tay
    ldx $4897,y
    lda #$5E
    jmp loc_460D
loc_4607:
    tay
    ldx $4889,y
    lda #$54
loc_460D:
    stx $2828
    sta zp_color_work
    ldx $2839
    beq loc_4630
    lda #$00
    ldy $2876
    cpy #$14
    bcs loc_4622
    ldy #$14
loc_4622:
    sta $0F1E,y
    cpy #$4F
    bcs loc_462C
    sta $0F71,y
loc_462C:
    iny
    dex
    bne loc_4622
loc_4630:
    pla
    bpl loc_4637
    dec $2823
loc_4636:
    rts
loc_4637:
    cmp #$20
    bcs loc_4636
    cmp #$15
    bcc loc_4643
    ldy #$49
    bcs loc_4671
loc_4643:
    cmp #$0C
    bcc loc_464B
    ldy #$48
    bcs loc_4671
loc_464B:
    cmp #$09
    bcc loc_4654
    lda #$07
    jmp loc_4663
loc_4654:
    cmp #$07
    bcc loc_465D
    lda #$06
    jmp loc_4663
loc_465D:
    cmp #$05
    bcc loc_4663
    lda #$05
loc_4663:
    tay
    clc
    lda $2825
    dey
    bmi loc_4670
loc_466B:
    adc #$09
    dey
    bpl loc_466B
loc_4670:
    tay
loc_4671:
    lda $2824
    cmp #$07
    bcs loc_467A
    lda #$07
loc_467A:
    cmp #$71
    bcc loc_4680
    lda #$71
loc_4680:
    cpy #$2D
    bcs loc_4688
    sec
    sbc $500E,y
loc_4688:
    sta $2876
    tax
    tya
    asl
    tay
    lda $503B,y
    sta zp_screen_ptr_lo
    lda $503C,y
    sta zp_screen_ptr_hi
    ldy #$00
loc_469B:
    lda $2829
    bne loc_46A3
    lda RANDOM
loc_46A3:
    and (zp_screen_ptr_lo),y
    beq loc_46C4
    cpx #$14
    bcc loc_46C1
    cpx #$73
    bcs loc_46B2
    sta $0F1E,x
loc_46B2:
    iny
    cpx #$2E
    bcc loc_46C1
    cpx #$4F
    bcs loc_46C1
    and $2828
    sta $0F71,x
loc_46C1:
    inx
    bpl loc_469B
loc_46C4:
    sty $2839
    clc
    lda $2826
    adc $2827
    sta $2877
    sta HPOSP3
    cmp #$8E
    bcc loc_46DD
    lda #$8D
    jmp loc_46E3
loc_46DD:
    cmp zp_color_work
    bcs loc_46E3
    lda zp_color_work
loc_46E3:
    sta $CD
    rts
render_loop_46E6:
    ; Table access: $489E, $489F; 54 instructions, 1 loop
    lda $061D
    lsr
    lsr
    lsr
    tay
render_loop_46ED:
    ; Table access: $489E, $489F; 49 instructions, 1 loop
    cpy #$09
    bcc loc_46F8
    lda #$08
    cmp $0624
    beq loc_474F
loc_46F8:
    cpy $0624
    beq loc_474F
    sty $0624
    sty zp_color_work
    lda #$FF
    sta zp_screen_ptr2_hi
    lda #$07
    sta zp_screen_ptr2_lo
loc_470A:
    tax
    asl
    tay
    lda $489E,y
    sta zp_screen_ptr_lo
    lda $489F,y
    sta zp_screen_ptr_hi
    ldy #$00
    txa
    lsr
    bcs loc_4724
    sty $C1
    lda #$B4
    jmp loc_4728
loc_4724:
    lda #$B7
    sta $C1
loc_4728:
    cpx zp_color_work
    bcc loc_4731
    and #$7F
    jmp loc_4733
loc_4731:
    ora #$80
loc_4733:
    sta (zp_screen_ptr_lo),y
    lda $C1
    beq loc_4747
    iny
    cpx zp_color_work
    bcc loc_4743
    lda #$38
    jmp loc_4745
loc_4743:
    lda #$B8
loc_4745:
    sta (zp_screen_ptr_lo),y
loc_4747:
    dec zp_screen_ptr2_lo
    lda zp_screen_ptr2_lo
    cmp zp_screen_ptr2_hi
    bne loc_470A
loc_474F:
    rts
render_4750:
    ; Writes to screen memory; Table access: $477C, $489E, $489F; 57 instructions, 1 loop
    lda $0611
render_4753:
    ; Writes to screen memory; Table access: $477C, $489E, $489F; 56 instructions, 1 loop
    sta $0611
    pha
    cmp #$08
    bne loc_4760
    lda #$00
    jmp loc_4768
loc_4760:
    clc
    adc $0619
    tay
    lda $477C,y
loc_4768:
    sta render_toggle
    pla
set_viewport_offset:
    ; Table access: $489E, $489F; 44 instructions, 1 loop
    clc
    adc #$08
render_loop_476F:
    ; Table access: $489E, $489F; 42 instructions, 1 loop
    sta zp_color_work
    lda #$07
    sta zp_screen_ptr2_hi
    lda #$0F
    sta zp_screen_ptr2_lo
    jmp loc_470A
    .byte $D6,$D1,$CB,$C4,$BC,$B3,$A9,$9E                  ; $477C
    .byte $92,$85,$77,$68,$57,$45,$32,$1E                  ; $4784
pmg_fill_478C:
    ; Fill loop — indexed store of constant value; Configures player-missile graphics; Writes to screen memory; Writes: SIZEP3; Table access: $0F98, $47EC; 45 instructions, 2 loops
    ldy $284A
    cpy $2849
    beq loc_47D8
    lda #$00
    ldx #$05
loc_4798:
    sta $0F98,y
    iny
    dex
    bne loc_4798
    ldy $2849
    bmi loc_47A8
    cpy #$2B
    bcc loc_47A9
loc_47A8:
    tay
loc_47A9:
    sty $284A
    sty $2849
    tya
    beq loc_47EB
    lda $283D
    bpl loc_47BC
    ldx #$00
    jmp loc_47C8
loc_47BC:
    lda $283E
    bne loc_47C6
    ldx #$05
    jmp loc_47C8
loc_47C6:
    ldx #$0A
loc_47C8:
    lda #$05
    sta zp_screen_ptr_lo
loc_47CC:
    lda $47EC,x
    sta $0F98,y
    inx
    iny
    dec zp_screen_ptr_lo
    bne loc_47CC
loc_47D8:
    ldy #$00
    lda $284B
    cmp #$6C
    bcc loc_47E5
    cmp #$8D
    bcc loc_47E6
loc_47E5:
    tya
loc_47E6:
    sta $CD
    sty SIZEP3
loc_47EB:
    rts
    .byte $38,$7C,$FE,$FE,$FE,$0E,$3E,$FE,$06,$1E,$E0,$F8,$FE,$C0,$F0 ; $47EC
.proc util_copy_47FB
    ; Copy loop — indexed load/store transfer; Table access: $0B91; 36 instructions, 2 loops
    ldy $BA
    lda #$1C
    sec
    sbc $28F5
    bmi loc_4809
    cmp #$1D
    bcc loc_480B
loc_4809:
    lda #$1E
loc_480B:
    sta $BA
    cpy $BA
    beq loc_482F
    ldx #$03
loc_4813:
    lda $0B91,y
    and #$CF
    sta $0B91,y
    iny
    dex
    bne loc_4813
    ldy $BA
    ldx #$03
loc_4823:
    lda $0B91,y
    ora #$30
    sta $0B91,y
    iny
    dex
    bne loc_4823
loc_482F:
    lda $28F4
    clc
    adc #$AB
    cmp #$A3
    bcc loc_483D
    cmp #$B4
    bcc loc_483F
loc_483D:
    lda #$B5
loc_483F:
    sta $CF
    rts
.endp

loc_4842:
    sta zp_screen_ptr_lo
    stx zp_screen_ptr2_lo
    and #$03
    ora zp_screen_ptr2_lo
    sta zp_screen_ptr2_lo
    and #$04
    eor #$04
    tax
    lsr zp_screen_ptr_lo
    lsr zp_screen_ptr_lo
    tya
    ora zp_screen_ptr_lo
    sta zp_screen_ptr_lo
    lda #$0E
    sta zp_color_work
loc_485E:
    cpy zp_screen_ptr_lo
    bcc loc_4869
    ldx zp_screen_ptr2_lo
    txa
    and #$04
    sta zp_screen_ptr2_lo
loc_4869:
    lda $52DF,x
    sta $2159,y
    sta $2189,y
    iny
    dec zp_color_work
    bne loc_485E
    rts
    .byte $D2,$BD,$A8,$93,$7E,$69,$54,$3F,$2A,$15,$00,$03,$01,$01,$00,$08 ; $4878
    .byte $08,$03,$07,$0F,$1F,$3F,$7F,$FF,$FE,$FC,$F8,$F0,$E0,$C0,$80,$03 ; $4888
    .byte $1F,$FF,$FF,$FF,$F0,$C0,$64,$33,$94,$33,$C5,$33,$F5,$33,$26,$34 ; $4898
    .byte $56,$34,$87,$34,$B7,$34,$92,$33,$C2,$33,$F3,$33,$23,$34,$54,$34 ; $48A8
    .byte $84,$34,$B5,$34,$E5,$34                          ; $48B8
.proc loop_util_48BE
    ; 20 instructions, 2 loops
    lda #$98
    sta zp_memcpy_dst_lo
    lda #$20
    sta zp_memcpy_dst_hi
    ldx #$08
loc_48C8:
    ldy #$1F
    lda #$FF
loc_48CC:
    sta (zp_memcpy_dst_lo),y
    dey
    bpl loc_48CC
    clc
    lda zp_memcpy_dst_lo
    adc #$30
    sta zp_memcpy_dst_lo
    lda zp_memcpy_dst_hi
    adc #$00
    sta zp_memcpy_dst_hi
    dex
    bne loc_48C8
    rts
.endp

.proc draw_48E2
    ; Writes to screen memory; 15 instructions
    lda #$BE
    sta $2107
    sta $2108
    sta $2137
    sta $2138
    sta $21C7
    sta $21C8
    sta $21F7
    sta $21F8
    lda #$AA
    sta $2167
    sta $2168
    sta $2197
    sta $2198
    rts
.endp

.proc util_copy_490B
    ; Copy loop — indexed load/store transfer; 10 instructions, 1 loop
    ldy #$07
loc_490D:
    lda $50CF,y
    sta $0C88,y
    lda $50D7,y
    sta $0D88,y
    lda $50DF,y
    sta $0B88,y
    dey
    bpl loc_490D
    rts
.endp

.proc render_fill_4923
    ; Fill loop — indexed store of constant value; Writes to screen memory; Table access: $32E3; 13 instructions, 1 loop
    ldy #$03
    lda #$01
loc_4927:
    sta $32E3,y
    dey
    bpl loc_4927
    lda #$B4
    sta $3355
    sta $3388
    lda #$B5
    sta $3356
    lda #$B6
    sta $3357
    rts
.endp

.proc irq_handler
    ; CUSTOM IRQ HANDLER — Processes keyboard input via KBCODE, handles special key combinations, manages IRQEN
    bit IRQST
    bvs loc_494B
    lda KBCODE
    and #$3F
    tax
loc_494B:
    lda #$00
    sta IRQEN
    lda #$C0
    sta IRQEN
    rti
.endp

setup_irq_vectors:
    ; Fill loop — indexed store of constant value; Updates sound registers; Writes to screen memory; Installs interrupt/event vectors; Writes: AUDC1; Calls: queue_push, util_copy_4C85, util_copy_4C85, queue_push; 201 instructions, 9 loops
    lda #$00
    ldy $0631
    sta $0631
    sta FR1
    bpl loc_496D
    ldy #$0F
loc_4964:
    lda $0741,y
    sta $00D0,y
    dey
    bpl loc_4964
loc_496D:
    txa
    ldy #$07
loc_4970:
    cmp $4B44,y
    bne loc_4978
    jmp loc_497C
loc_4978:
    dey
    bpl loc_4970
loc_497B:
    rts
loc_497C:
    cpy #$06
    bne loc_49A0
    lda $0632
    ora game_mode
    beq loc_499F
loc_4988:
    lda #$B2
    sta VVBLKI
    lda #$82
    sta $0223
    lda #$3C
    pha
    lda #$0D
    pha
    lda #$04
    pha
    rti
dat_499C:
    .byte $4C,$A0,$49                                      ; $499C
loc_499F:
    rts
loc_49A0:
    lda game_mode
    bne loc_497B
    cpy #$07
    bne loc_49ED
    ldy $0632
    bne loc_49D9
    lda $062A
    bne loc_499F
    lda $0626
    sta $0627
    sty $0626
    tya
    ldx #$06
loc_49BF:
    sta AUDC1,x
    dex
    dex
    bpl loc_49BF
    jsr util_copy_4C85
    inc $0632
    lda #$83
    pha
    lda #$48
    pha
    lda #$04
    pha
    rti
dat_49D6:
    .byte $4C,$ED,$49                                      ; $49D6
loc_49D9:
    lda #$00
    sta $0632
    lda $0627
    sta $0626
    ldy #$08
loc_49E6:
    pla
    dey
    bne loc_49E6
    jmp render_proc_8E9D
loc_49ED:
    lda $0632
    bne loc_499F
    lda #$21
    sta FR1
    cpy ent_type
    beq loc_4A4B
    cpy #$03
    bne loc_4A21
    lda flight_param
    cmp #$02
    bcc loc_4A4B
    lda difficulty_level
    bne loc_4A16
    jsr util_copy_4C85
    inc difficulty_level
    lda #$0B
    jmp loc_4A1D
loc_4A16:
    lda #$00
    sta difficulty_level
    lda #$0E
loc_4A1D:
    sta $0635
    rts
loc_4A21:
    cpy #$01
    bne loc_4A5E
    lda game_event_flag
    beq loc_4A35
    ldy #$71
    lda #$28
    sta $0637
    ldx #$17
    bne loc_4A59
loc_4A35:
    lda difficulty_level
    beq loc_4A4B
    lda $28A3
    beq loc_4A4F
    bmi loc_4A4B
    dec $28A3
    lda #$B4
    ldy #$0A
    jmp loc_4A4C
loc_4A4B:
    rts
loc_4A4C:
    jmp loc_4A54
loc_4A4F:
    inc $28A3
    lda #$34
loc_4A54:
    sta $3388
    ldx #$16
loc_4A59:
    stx FR1
    jmp render_copy_4AE7
loc_4A5E:
    cpy #$02
    bne loc_4A65
    jmp loc_4C54
loc_4A65:
    lda $0625
    beq loc_4A4B
    cpy #$04
    bne loc_4AA4
    lda flight_param
    beq loc_4A91
    lda #$00
    sta $288B
    sta $288C
    cmp difficulty_level
    bne loc_4A86
    sta flight_param
    jmp loc_4A89
loc_4A86:
    sta difficulty_level
loc_4A89:
    lda #$07
    jsr render_4753
    jmp loc_4A9C
loc_4A91:
    lda $0611
    beq loc_4A9C
    dec $0611
    jsr render_4750
loc_4A9C:
    lda #$FF
    sta ent_type
    jmp clear_hud_line
loc_4AA4:
    lda flight_param
    bne loc_4A4B
    cpy #$05
    bne loc_4AC2
    lda #$06
    cmp $0611
    bcc loc_4ABA
    inc $0611
    jsr render_4750
loc_4ABA:
    lda #$FF
    sta ent_type
    jmp clear_hud_line
loc_4AC2:
    cpy #$00
    bne loc_4AE4
    lda $061D
    cmp #$64
    bcc loc_4AD8
    lda #$1C
    sta FR1
    ldy #$C5
    lda #$FF
    jmp loc_4AE1
loc_4AD8:
    lda #$08
    jsr render_4753
    lda #$00
    ldy #$04
loc_4AE1:
    jmp loc_4C7B
loc_4AE4:
    sty ent_type
render_copy_4AE7:
    ; Copy loop — indexed load/store transfer; Writes to screen memory; Calls: clear_hud_line; Table access: $32B7, $4B4C, $4C41; 41 instructions, 1 loop
    tya
    and #$20
    bne loc_4AF1
    lda #$C8
    jmp loc_4AF4
loc_4AF1:
    lda $0637
loc_4AF4:
    sta QTEMP
    tya
    and #$DF
    tay
render_copy_4AFA:
    ; Copy loop — indexed load/store transfer; Writes to screen memory; Calls: clear_hud_line; Table access: $32B7, $4B4C, $4C41; 31 instructions, 1 loop
    tya
    bpl loc_4B03
    and #$7F
    tay
    jmp loc_4B06
loc_4B03:
    jsr clear_hud_line
loc_4B06:
    lda #$00
    sta $0635
    lda #$FF
    sta dirty_flag
    cpy #$40
    bcc loc_4B1D
    tya
    and #$3F
    tay
    lda #$5A
    sta dirty_flag
loc_4B1D:
    cpy #$09
    bne loc_4B25
    lda #$38
    sta QTEMP
loc_4B25:
    ldx $4C41,y
    ldy #$00
loc_4B2A:
    lda $4B4C,x
    bmi loc_4B38
    sta $32B7,y
    iny
    inx
    cpy #$0E
    bne loc_4B2A
loc_4B38:
    rts
.proc clear_hud_line
    ; Fill loop — indexed store of constant value; Writes to screen memory; Table access: $32B6; 6 instructions, 1 loop
    ldx #$0E
    lda #$00
loc_4B3D:
    sta $32B6,x
    dex
    bne loc_4B3D
    rts
.endp

    .byte $00,$3F,$15,$2A,$07,$06,$34,$1C                  ; $4B44
    .byte $21,$2C,$29,$25,$2E,$00,$2B,$29                  ; $4B4C
    .byte $2C,$2C,$25,$24,$FF,$21,$29,$32                  ; $4B54
    .byte $2C,$2F,$23,$2B,$00,$2F,$30,$25                  ; $4B5C
    .byte $2E,$25,$24,$26,$29,$32,$25,$00                  ; $4B64
    .byte $22,$2F,$2F,$33,$34,$25,$32,$33                  ; $4B6C
    .byte $FF,$2C,$25,$36,$25,$2C,$00,$23                  ; $4B74
    .byte $2F,$2D,$30,$2C,$25,$34,$25,$2E                  ; $4B7C
    .byte $2F,$37,$00,$2C,$21,$2E,$24,$29                  ; $4B84
    .byte $2E,$27,$0E,$0E,$0E,$0A,$0A,$00                  ; $4B8C
    .byte $34,$2F,$2F,$00,$28,$29,$27,$28                  ; $4B94
    .byte $00,$0A,$0A,$30,$29,$2C,$2F,$34                  ; $4B9C
    .byte $00,$29,$2E,$00,$32,$21,$2E,$27                  ; $4BA4
    .byte $25,$30,$29,$2C,$2F,$34,$00,$34                  ; $4BAC
    .byte $2F,$2F,$00,$26,$21,$32,$00,$33                  ; $4BB4
    .byte $28,$29,$30,$00,$2F,$26,$26,$00                  ; $4BBC
    .byte $33,$23,$2F,$30,$25,$30,$29,$2C                  ; $4BC4
    .byte $2F,$34,$00,$2B,$29,$2C,$2C,$25                  ; $4BCC
    .byte $24,$01,$FF,$21,$29,$32,$2C,$2F                  ; $4BD4
    .byte $23,$2B,$00,$23,$2C,$2F,$33,$25                  ; $4BDC
    .byte $24,$25,$2E,$27,$29,$2E,$25,$33                  ; $4BE4
    .byte $00,$2F,$26,$26,$FF,$22,$2F,$2E                  ; $4BEC
    .byte $35,$33,$00,$30,$2F,$29,$2E,$34                  ; $4BF4
    .byte $33,$FF,$2E,$2F,$00,$2D,$2F,$34                  ; $4BFC
    .byte $28,$25,$32,$00,$33,$28,$29,$30                  ; $4C04
    .byte $01,$FF,$25,$2E,$27,$29,$2E,$25                  ; $4C0C
    .byte $33,$00,$2F,$2E,$FF,$33,$39,$33                  ; $4C14
    .byte $34,$25,$2D,$33,$00,$2F,$26,$26                  ; $4C1C
    .byte $FF,$21,$2C,$29,$25,$2E,$00,$29                  ; $4C24
    .byte $2E,$00,$33,$28,$29,$30,$01,$21                  ; $4C2C
    .byte $29,$32,$2C,$2F,$23,$2B,$00,$2A                  ; $4C34
    .byte $21,$2D,$2D,$25,$24,$00,$0D,$1B                  ; $4C3C
    .byte $29,$37,$45,$53,$61,$6F,$7D,$8B                  ; $4C44
    .byte $99,$A5,$B2,$C2,$CD,$D9,$E7,$B5                  ; $4C4C
loc_4C54:
    lda camera_view_param
    bmi loc_4C61
    ldy #$1C
    sty FR1
    ldy #$CD
    bne loc_4C7E
loc_4C61:
    sty zp_screen_ptr_lo
    lda #$01
    jsr render_util_8E94
    lda #$00
    cmp difficulty_level
    bne loc_4C75
    sta flight_param
    jmp loc_4C78
loc_4C75:
    sta difficulty_level
loc_4C78:
    ldy zp_screen_ptr_lo
    tya
loc_4C7B:
    sta ent_type
loc_4C7E:
    lda #$48
    sta QTEMP
    jmp render_copy_4AFA
.proc util_copy_4C85
    ; Copy loop — indexed load/store transfer; Table access: $00D0, $0741; 6 instructions, 1 loop
    ldy #$0F
loc_4C87:
    lda $00D0,y
    sta $0741,y
    dey
    bpl loc_4C87
    rts
.endp

math_update_score_4C91:
    ; BCD arithmetic — score or counter update; Arithmetic with shifts; Calls: calc_4CC8, calc_4CD2, math_proc_4CDD; 42 instructions
    sed
    lda game_phase_timer
    clc
    adc score_lo
    sta game_phase_timer
    lda game_active_flag
    adc score_hi
    sta game_active_flag
    lda game_ready_flag
    adc #$00
    sta game_ready_flag
    cld
    lda #$C5
    sta $C6
    lda #$32
    sta zp_render_scratch
calc_4CB6:
    ; Arithmetic with shifts; Calls: calc_4CC8, calc_4CD2, math_proc_4CDD; 26 instructions
    lda game_ready_flag
    jsr calc_4CC8
    lda game_active_flag
    jsr calc_4CD2
    lda game_phase_timer
    jmp calc_4CD2
calc_4CC8:
    ; Arithmetic with shifts; Calls: math_proc_4CDD; 24 instructions
    ldy #$00
    sty score_lo
    sty score_hi
    ldx #$00
calc_4CD2:
    ; Arithmetic with shifts; Calls: math_proc_4CDD; 20 instructions
    pha
    lsr
    lsr
    lsr
    lsr
    jsr math_proc_4CDD
    pla
    and #$0F
.proc math_proc_4CDD
    ; 12 instructions
    cpx #$00
    bne loc_4CEA
    cmp #$00
    bne loc_4CE9
    cpy #$05
    bcc loc_4CEF
loc_4CE9:
    inx
loc_4CEA:
    clc
    adc #$50
    sta ($C6),y
loc_4CEF:
    iny
    rts
.endp

dli_dispatch:
    ; DLI DISPATCHER — Reads DLI stage counter ($C8), indexes into jump table at $4DD6 to dispatch per-scanline color changes
    sta $C9
    sty $CB
    lda $C8
    asl
    tay
    lda dli_jump_table,y
    sta zp_dli_vec_lo
    lda $4DD7,y
    sta zp_dli_vec_hi
    jmp (zp_dli_vec_lo)
loc_4D06:
    inc $C8
    ldy $CB
    lda $C9
    rti
dli_sky:
    ; DLI STAGE 0 — Sky colors: sets horizon-area COLPF registers for sky gradient
    lda FR0M
    sta COLPF1
    nop
    lda #$38
    sta CHBASE
    lda $D0
    sta COLPF0
    jmp loc_4D06
dli_horizon:
    ; DLI STAGE 1 — Terrain transition: switches colors from sky to ground palette, enables PRIOR=$11 for 5th player multicolor
    lda #$39
    sta HPOSP0
    lda #$BF
    sta HPOSP1
    lda $DB
    sta COLPF1
    lda $DE
    sta COLPF0
    lda $DC
    sta COLPF2
    lda $DD
    sta COLBK
    jmp loc_4D06
dli_ground:
    ; DLI STAGE 2 — Ground colors: maintains terrain color registers during ground rendering
    lda #$C0
    sta SIZEM
    lda #$BE
    sta HPOSM1
    stx $CA
    ldx FR0M
    lda $D0
    ldy #$01
    sta COLPM0
    sty SIZEP0
    sta COLPM1
    lda #$30
    sta HPOSP0
    stx COLPF2
    lda #$02
    sta PRIOR
    sty SIZEP1
    lda #$C0
    sta HPOSP1
    sty SIZEP2
    ldx $CA
    jmp loc_4D06
dli_hud:
    ; DLI STAGE 3 — HUD transition: switches to HUD colors, disables 5th player mode
    stx $CA
    ldy $D0
    ldx $D2
    lda $D1
    sta COLPM2
    lda FR0M
    sta WSYNC
    sty COLPF0
    sta COLPF1
    stx COLPF2
    lda $D3
    sta COLBK
    lda #$4C
    sta HPOSP2
    lda #$5C
    sta HPOSP0
    lda $D6
    sta COLPM0
    lda $D7
    sta COLPM3
    lda #$94
    sta HPOSP1
    lda $DF
    sta COLPM1
    lda $CD
    sta HPOSP3
    lda #$04
    sta PRIOR
    ldx $CA
    lda #$64
    sta HPOSM3
    lda $CF
    sta HPOSM2
    jmp loc_4D06
    lda FR0
    sta COLBK
    jmp loc_4D06
dli_jump_table:
    ; DLI DISPATCH TABLE — 8 pairs of lo/hi addresses for DLI stage handlers
    ; DLI DISPATCH TABLE — 8 pairs of lo/hi addresses for DLI stage handlers
    .byte $12,$4D,$20,$4D,$41,$4D,$79,$4D                  ; $4DD6
    .byte $CE,$4D,$51,$46,$42,$0B,$43,$44                  ; $4DDE
    .byte $03,$04,$45,$46,$05,$06,$49,$46                  ; $4DE6
    .byte $07,$08,$4F,$50,$0F,$10,$47,$48                  ; $4DEE
    .byte $07,$08,$4B,$4A,$09,$08,$4D,$4E                  ; $4DF6
    .byte $0D,$0E,$4C,$46,$09,$08,$4C,$46                  ; $4DFE
    .byte $07,$0C,$39,$73,$74,$76,$60,$73                  ; $4E06
    .byte $74,$76,$66,$73,$74,$76,$6D,$73                  ; $4E0E
    .byte $74,$76,$39,$73,$74,$3B,$76,$77                  ; $4E16
    .byte $3A,$61,$76,$77,$67,$68,$76,$77                  ; $4E1E
    .byte $3A,$6E,$76,$77,$3A,$3B,$76,$77                  ; $4E26
    .byte $3D,$78,$79,$62,$63,$78,$79,$69                  ; $4E2E
    .byte $6A,$78,$79,$6F,$70,$78,$79,$3C                  ; $4E36
    .byte $3D,$78,$79,$3F,$7B,$73,$64,$3F                  ; $4E3E
    .byte $7B,$73,$6B,$6C,$7B,$73,$71,$72                  ; $4E46
    .byte $7B,$73,$3E,$3F,$7B,$73,$01,$01                  ; $4E4E
    .byte $01,$03,$03,$03,$07,$07,$07,$0F                  ; $4E56
    .byte $0F,$0F,$1F,$1F,$1F,$3F,$3F,$3F                  ; $4E5E
    .byte $7F,$7F,$7F                                      ; $4E66
    :4 .byte $00                    ; $4E69 (4 zero bytes)
    .byte $01,$01,$03,$03,$07,$07,$0F,$0F                  ; $4E6D
    .byte $1F,$1F,$3F,$3F,$7F,$7F,$FF,$FF                  ; $4E75
    .byte $FF                                              ; $4E7D
    :6 .byte $00                    ; $4E7E (6 zero bytes)
    .byte $01,$01,$03,$07,$07,$0F,$1F,$1F                  ; $4E84
    .byte $3F,$7F,$7F                                      ; $4E8C
    :4 .byte $FF                    ; $4E8F (4 bytes)
    :8 .byte $00                    ; $4E93 (8 zero bytes)
    .byte $01,$03,$07,$0F,$1F,$3F,$7F                      ; $4E9B
    :6 .byte $FF                    ; $4EA2 (6 bytes)
    :10 .byte $00                    ; $4EA8 (10 zero bytes)
    .byte $03,$0F,$3F                                      ; $4EB2
    :8 .byte $FF                    ; $4EB5 (8 bytes)
    :12 .byte $00                    ; $4EBD (12 zero bytes)
    :9 .byte $FF                    ; $4EC9 (9 bytes)
    :10 .byte $00                    ; $4ED2 (10 zero bytes)
    .byte $C0,$F0,$FC                                      ; $4EDC
    :8 .byte $FF                    ; $4EDF (8 bytes)
    :8 .byte $00                    ; $4EE7 (8 zero bytes)
    .byte $80,$C0,$E0,$F0,$F8,$FC,$FE                      ; $4EEF
    :6 .byte $FF                    ; $4EF6 (6 bytes)
    :6 .byte $00                    ; $4EFC (6 zero bytes)
    .byte $80,$80,$C0,$E0,$E0,$F0,$F8,$F8                  ; $4F02
    .byte $FC,$FE,$FE                                      ; $4F0A
    :4 .byte $FF                    ; $4F0D (4 bytes)
    :4 .byte $00                    ; $4F11 (4 zero bytes)
    .byte $80,$80,$C0,$C0,$E0,$E0,$F0,$F0                  ; $4F15
    .byte $F8,$F8,$FC,$FC,$FE,$FE,$FF,$FF                  ; $4F1D
    .byte $FF,$80,$80,$80,$C0,$C0,$C0,$E0                  ; $4F25
    .byte $E0,$E0,$F0,$F0,$F0,$F8,$F8,$F8                  ; $4F2D
    .byte $FC,$FC,$FC,$FE,$FE,$FE,$80,$D0                  ; $4F35
    .byte $F0,$F8,$F8,$78,$78,$78,$3C,$3C                  ; $4F3D
    .byte $3C,$1E,$1E,$1E                                  ; $4F45
loc_4F49:
    .byte $0F,$0F,$07,$03,$01,$00,$F0,$F8                  ; $4F49
    .byte $F8,$F8,$78,$7C,$7C,$3E,$3E,$3E                  ; $4F51
    .byte $1F,$1F,$0F,$07,$00,$10,$38,$F8                  ; $4F59
    .byte $F8,$FC,$FC,$FE,$7E,$7F,$3F,$3F                  ; $4F61
    .byte $1F,$0F,$00,$10,$18,$38,$78,$FC                  ; $4F69
    .byte $FE,$FE,$FF,$7F,$7F,$3F,$1E,$0C                  ; $4F71
    .byte $00,$18,$18,$18,$3C,$7E                          ; $4F79
    :4 .byte $FF                    ; $4F7F (4 bytes)
    .byte $7E,$3C,$18,$00,$F0,$F8,$78,$7C                  ; $4F83
    .byte $3C,$3E,$1E,$0F,$07,$00,$70,$F8                  ; $4F8B
    .byte $FC,$7C,$3E,$3E,$1F,$0F,$00,$10                  ; $4F93
    .byte $78,$FC,$FE,$7F,$3F,$0E,$00,$18                  ; $4F9B
    .byte $3C,$7C,$FE,$7F,$3F,$1C,$00,$18                  ; $4FA3
    .byte $3C,$7E,$FF,$7E,$3C,$00,$70,$38                  ; $4FAB
    .byte $38,$1C,$0C,$06,$00,$78,$38,$3C                  ; $4FB3
    .byte $1C,$0E,$00,$10,$78,$7C,$3E,$0E                  ; $4FBB
    .byte $00,$18,$3C,$7E,$3C,$00,$F0,$78                  ; $4FC3
    .byte $3C,$0F,$00,$F0,$7C,$3E,$0F,$00                  ; $4FCB
    .byte $F8,$7E,$1F,$00,$3C,$FF,$3C,$00                  ; $4FD3
    .byte $78,$3C,$0E,$02,$00,$78,$3C,$0E                  ; $4FDB
    .byte $00,$38,$7C,$06,$00,$3C,$7E,$00                  ; $4FE3
    .byte $38,$1C,$0E,$00,$3C,$1E,$06,$00                  ; $4FEB
    .byte $18,$3C,$06,$00,$1C,$3E,$00,$38                  ; $4FF3
    .byte $1C,$00,$30,$1C,$00,$3C,$00,$18                  ; $4FFB
    .byte $0C,$00,$08,$1C,$00,$1C,$00,$18                  ; $5003
    .byte $00,$08,$00,$07                                  ; $500B
    :7 .byte $05                    ; $500F (7 bytes)
    .byte $07,$03,$03                                      ; $5016
    :5 .byte $02                    ; $5019 (5 bytes)
    .byte $03,$03,$02                                      ; $501E
    :7 .byte $01                    ; $5021 (7 bytes)
    .byte $02,$01,$01                                      ; $5028
    :5 .byte $00                    ; $502B (5 zero bytes)
    .byte $01,$01,$01                                      ; $5030
    :7 .byte $00                    ; $5033 (7 zero bytes)
    .byte $01,$3B,$4F,$4F,$4F,$5E,$4F,$6C                  ; $503A
    .byte $4F,$7A,$4F,$29,$27,$37,$27,$45                  ; $5042
    .byte $27,$54,$27,$87,$4F,$91,$4F,$9A                  ; $504A
    .byte $4F,$A2,$4F,$AA,$4F,$68,$27,$70                  ; $5052
    .byte $27,$78,$27,$81,$27,$B1,$4F,$B8                  ; $505A
    .byte $4F,$BE,$4F,$C4,$4F,$C4,$4F,$8B                  ; $5062
    .byte $27,$90,$27,$96,$27,$9C,$27,$C9                  ; $506A
    .byte $4F,$CE,$4F,$D3,$4F,$D7,$4F,$D7                  ; $5072
    .byte $4F,$A3,$27,$A7,$27,$AB,$27,$B0                  ; $507A
loc_5082:
    .byte $27,$DB,$4F,$E0,$4F,$E4,$4F,$E8                  ; $5082
    .byte $4F,$E8,$4F,$B5,$27,$B8,$27,$BC                  ; $508A
    .byte $27,$C0,$27,$EB,$4F,$EF,$4F,$F3                  ; $5092
    .byte $4F,$F7,$4F,$F7,$4F,$C5,$27,$C8                  ; $509A
    .byte $27,$CC,$27,$D0,$27,$FA,$4F,$FA                  ; $50A2
    .byte $4F,$FD,$4F,$00,$50,$00,$50,$D4                  ; $50AA
    .byte $27,$D6,$27,$D9,$27,$DC,$27,$02                  ; $50B2
    .byte $50,$05,$50,$08,$50,$08,$50,$08                  ; $50BA
    .byte $50,$DF,$27,$E1,$27,$E3,$27,$E6                  ; $50C2
    .byte $27,$0A,$50,$0C,$50,$01,$01,$07                  ; $50CA
    .byte $07,$1F,$1F,$7F,$7F,$80,$80,$E0                  ; $50D2
    .byte $E0,$F8,$F8,$FE,$FE,$00,$00                      ; $50DA
    :4 .byte $06                    ; $50E1 (4 bytes)
    .byte $0F,$0F,$07,$00,$01,$05,$20,$AA                  ; $50E5
    .byte $01,$50,$0D,$00,$01,$05,$01,$55                  ; $50ED
    .byte $01,$6A,$1E,$AA,$01,$A9,$01,$55                  ; $50F5
    .byte $01,$50,$0B,$00,$01,$05,$02,$55                  ; $50FD
    .byte $01,$6A,$1E,$AA,$01,$A9,$02,$55                  ; $5105
    .byte $01,$50,$09,$00,$01,$05,$03,$55                  ; $510D
    .byte $01,$5A,$1E,$AA,$01,$A5,$03,$55                  ; $5115
    .byte $01,$50,$04,$00,$00,$C8,$01,$24                  ; $511D
    .byte $75,$75,$16,$55,$56,$57,$D2,$75                  ; $5125
    .byte $16,$1B,$1C,$57,$75,$75,$22,$D1                  ; $512D
    .byte $01,$23,$75,$15,$17,$18,$14,$75                  ; $5135
    .byte $75,$40,$41,$75,$75,$27,$C4,$28                  ; $513D
    .byte $7A,$75,$75,$40,$41,$75,$75,$15                  ; $5145
    .byte $7F,$7E,$14,$75,$21,$D2,$01,$24                  ; $514D
    .byte $75,$5F,$19,$1A,$5E,$75,$75,$40                  ; $5155
    .byte $41,$75,$75,$25,$C5,$00,$2C,$75                  ; $515D
    .byte $40,$41,$75,$75,$20,$7F,$7E,$1F                  ; $5165
    .byte $75,$22,$D3,$01,$23,$5D,$5C,$5B                  ; $516D
    .byte $5A,$75,$75,$40,$41,$75,$75,$00                  ; $5175
    .byte $00,$30,$2F,$31,$00,$2A,$75,$40                  ; $517D
    .byte $41,$75,$75,$5D,$1E,$1D,$5A,$21                  ; $5185
    .byte $C3,$01,$02,$54,$CF,$01,$24,$75                  ; $518D
    .byte $58,$65,$C3,$75,$40,$41,$75,$75                  ; $5195
    .byte $C3,$00,$2E,$00,$00,$2A,$75,$40                  ; $519D
    .byte $41,$C3,$75,$7C,$7D,$75,$22,$C3                  ; $51A5
    .byte $01,$52,$D1,$01,$23,$C5,$75,$40                  ; $51AD
    .byte $41,$75,$75,$26,$C4,$00,$2D,$2B                  ; $51B5
    .byte $75,$40,$41,$C5,$75,$21,$D6,$01                  ; $51BD
    .byte $24,$C5,$75,$40,$41,$CA,$75,$40                  ; $51C5
    .byte $41,$C5,$75,$22,$C5,$01,$12,$53                  ; $51CD
    .byte $D0,$01,$23,$C3,$75,$58,$40,$41                  ; $51D5
    .byte $CA,$75,$40,$41,$59,$C3,$75,$21                  ; $51DD
    .byte $C6,$01,$11,$D1,$01,$24,$D6,$75                  ; $51E5
    .byte $22,$C5,$01,$12,$54,$D1,$01,$32                  ; $51ED
    .byte $D6,$00,$33,$C5,$01,$13,$C6,$01                  ; $51F5
    .byte $C0,$D6,$E6,$16,$16,$26,$36,$46                  ; $51FD
    .byte $04,$26,$2C,$90,$00,$06,$44,$9A                  ; $5205
    .byte $78,$08,$C0,$0E,$60,$0E,$30,$0E                  ; $520D
    .byte $18,$0E,$0C,$0E,$06,$08,$03,$00                  ; $5215
    .byte $08,$03,$0E,$06,$0E,$0C,$0E,$18                  ; $521D
    .byte $0E,$30,$0E,$60,$08,$C0,$00,$43                  ; $5225
    .byte $6F,$70,$79,$72,$69,$67,$68,$74                  ; $522D
    .byte $20,$28,$63,$29,$20,$31,$39,$38                  ; $5235
    .byte $33,$20,$4C,$75,$63,$61,$73,$66                  ; $523D
    .byte $69,$6C,$6D,$20,$4C,$74,$64,$2E                  ; $5245
    :8 .byte $1C                    ; $524D (8 bytes)
    .byte $15,$0E,$07,$00,$00,$07,$0E,$15                  ; $5255
    .byte $86,$78,$10,$86,$08,$11,$83,$08                  ; $525D
    .byte $09,$82,$0A,$13,$82,$08,$09,$0A                  ; $5265
    .byte $82,$0B,$14,$08,$09,$0A,$0B,$0C                  ; $526D
    .byte $0D,$15,$86,$78,$10,$86,$78,$11                  ; $5275
    .byte $84,$09,$82,$0A,$13,$82,$09,$0A                  ; $527D
    .byte $83,$0B                                          ; $5285
loc_5287:
    .byte $14,$82,$0A,$0B,$0C,$82,$0D,$15                  ; $5287
    .byte $59,$61,$69,$79,$82,$11,$10,$59                  ; $528F
    .byte $61,$69,$79,$83,$11,$5A,$62,$6A                  ; $5297
    .byte $7A,$82,$12,$13,$5B,$63,$6B,$7B                  ; $529F
    .byte $82,$13,$14,$5C,$64,$6C,$7C,$83                  ; $52A7
    .byte $15,$87,$10,$87,$11,$85,$19,$12                  ; $52AF
    .byte $87,$13,$14,$87,$15,$0E,$01,$00                  ; $52B7
    .byte $80                                              ; $52BF
.proc memzero
    ; Fill loop — indexed store of constant value; Table access: $07FD, $525D; 19 instructions, 3 loops
    ldx #$00
    ldy #$00
    sty $C6
loc_52C6:
    inc $C6
loc_52C8:
    lda $525D,x
    inx
    asl
    bcc loc_52D5
    lsr
    sta $C6
    bne loc_52C8
    rts
loc_52D5:
    sta $07FD,y
    iny
    dec $C6
    bne loc_52D5
    beq loc_52C6
    eor FR0M,x
    sbc $FD,x
.endp

dat_52E3:
    .byte $FF,$7F,$5F,$57                                  ; $52E3