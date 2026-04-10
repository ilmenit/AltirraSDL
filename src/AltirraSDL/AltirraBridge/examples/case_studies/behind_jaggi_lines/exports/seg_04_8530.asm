; =============================================================
; Segment 4: $8530-$B7E6 — Game code + data: rendering, tables, level data
; =============================================================
; XEX load range: $8530-$B7E6 (12983 bytes)

    org $8530

read_joystick:
    ; Reads POKEY RANDOM for randomization; Writes to screen memory; Reads joystick input; Bit extraction/packing via shifts and rotates; Reads: SKRES, PORTA; Calls: render_8EC9, draw_8BDC, queue_push, queue_push; Table access: $071B; 201 instructions, 1 loop
    lda $0626
    cmp #$02
    bne loc_853C
    lda difficulty_level
    beq loc_853F
loc_853C:
    jmp loc_85A7
loc_853F:
    lda render_toggle
    lsr
    lsr
    adc #$20
    sta $2886
    lda PORTA
    tax
    and #$04
    bne loc_855C
    lda $2886
    eor #$FF
    sta $2886
    jmp loc_8566
loc_855C:
    txa
    and #$08
    beq loc_8566
    lda #$00
    sta $2886
loc_8566:
    lda $293D
    bne loc_8570
    lda #$D0
    jmp loc_85A4
loc_8570:
    txa
    and #$01
    bne loc_8588
    lda #$D0
    ldy $288E
    cpy #$F4
    bne loc_8585
    ldy $288D
    bne loc_8585
    lda #$FF
loc_8585:
    jmp loc_85A4
loc_8588:
    txa
    and #$02
    bne loc_85A2
    lda #$30
    ldy $288E
    cpy #$0B
    bne loc_859F
    ldy $288D
    cpy #$FF
    bne loc_859F
    lda #$01
loc_859F:
    jmp loc_85A4
loc_85A2:
    lda #$00
loc_85A4:
    sta $2888
loc_85A7:
    lda game_mode
    beq loc_85AF
    jsr render_8EC9
loc_85AF:
    lda ent_type
    cmp #$02
    bne loc_85E2
    lda #$30
    sta $2888
    lda $288C
    sta $2886
    lda $288B
    asl
    rol $2886
    asl
    rol $2886
    sec
    lda #$00
    sbc $2886
    sta $2886
    lda #$F0
    sta render_toggle
    lda #$00
    jsr set_viewport_offset
    jmp loc_8636
loc_85E2:
    ldy flight_param
    bne loc_861D
    cmp #$00
    bne loc_85F1
    jsr draw_8BDC
    jmp loc_861A
loc_85F1:
    lda #$01
    cmp $0678
    beq loc_860E
    lda #$00
    sta $066E
    sta $066F
    ldy #$01
    jsr queue_push
    iny
    jsr queue_push
    lda #$01
    jsr render_util_8E94
loc_860E:
    lda $288B
    sta $2889
    lda $288C
    sta $288A
loc_861A:
    jmp loc_8636
loc_861D:
    lda $2840
    bne loc_8631
    ldy #$01
    sty $066E
    sty $066F
    jsr queue_push
    iny
    jsr queue_push
loc_8631:
    lda #$34
    sta $3355
loc_8636:
    lda difficulty_level
    beq loc_8651
    lda #$B4
    sta $3355
    lda #$00
    sta $066E
    sta $066F
    ldy #$01
    jsr queue_push
    iny
    jmp queue_push
loc_8651:
    lda $2886
    bne loc_868C
    lda $288C
    sta zp_screen_ptr_lo
    lda $288B
    ldy #$05
loc_8660:
    asl
    rol zp_screen_ptr_lo
    dey
    bne loc_8660
    bcc loc_8678
    lda $288B
    sbc zp_screen_ptr_lo
    sta $288B
    bcc loc_8675
    inc $288C
loc_8675:
    jmp loc_8689
loc_8678:
    lda $288B
    sbc zp_screen_ptr_lo
    bcs loc_8689
    dec $288C
    bpl loc_8689
    lda #$00
    sta $288C
loc_8689:
    sta $288B
loc_868C:
    lda flight_param
    bne loc_86CA
    lda $2888
    bne loc_86CA
    lda $288E
    sta zp_screen_ptr_lo
    lda $288D
    asl
    rol zp_screen_ptr_lo
    asl
    rol zp_screen_ptr_lo
    bcc loc_86B6
    lda $288D
    sbc zp_screen_ptr_lo
    sta $288D
    bcc loc_86B3
    inc $288E
loc_86B3:
    jmp loc_86CA
loc_86B6:
    lda $288D
    sbc zp_screen_ptr_lo
    bcs loc_86C7
    dec $288E
    bpl loc_86C7
    lda #$00
    sta $288E
loc_86C7:
    sta $288D
loc_86CA:
    ldy #$01
    lda render_toggle
    bne loc_86D6
    ldy #$03
    jmp loc_86FB
loc_86D6:
    sec
    sbc $288E
    sbc $288E
    sta zp_screen_ptr_lo
    clc
    lda render_toggle
    lsr
    ora #$07
    and RANDOM
    adc zp_screen_ptr_lo
    bcc loc_86EF
    lda #$FF
loc_86EF:
    clc
    adc $2897
    sta $2897
    bcc loc_86FB
    inc $2898
loc_86FB:
    lda $293D
    beq loc_873A
    clc
    lda $2898
    adc #$02
    sta zp_screen_ptr_lo
    lda #$00
    rol
    sta zp_screen_ptr_hi
    lda $2897
loc_8710:
    asl
    rol zp_screen_ptr_lo
    rol zp_screen_ptr_hi
    dey
    bne loc_8710
    lda $2897
    sbc zp_screen_ptr_lo
    sta $2897
    lda $2898
    sbc zp_screen_ptr_hi
    sta $2898
    bcs loc_873A
    lda #$00
    sta $2897
    sta $2898
    cmp $0678
    beq loc_873A
    jsr render_util_8E94
loc_873A:
    clc
    lda $288B
    adc $2886
    sta $288B
    bit $2886
    bpl loc_8762
    bcs loc_875F
    dec $288C
    bpl loc_875F
    lda $288C
    cmp #$FB
    bcs loc_875F
    inc $288C
    lda #$00
    sta $288B
loc_875F:
    jmp loc_8778
loc_8762:
    bcc loc_8778
    inc $288C
    bmi loc_8778
    lda $288C
    cmp #$05
    bcc loc_8778
    dec $288C
    lda #$FF
    sta $288B
loc_8778:
    clc
    lda $288D
    adc $2888
    sta $288D
    bit $2888
    bpl loc_87A0
    bcs loc_879D
    dec $288E
    bpl loc_879D
    lda $288E
    cmp #$F4
    bcs loc_879D
    inc $288E
    lda #$00
    sta $288D
loc_879D:
    jmp loc_87B6
loc_87A0:
    bcc loc_87B6
    inc $288E
    bmi loc_87B6
    lda $288E
    cmp #$0C
    bcc loc_87B6
    dec $288E
    lda #$FF
    sta $288D
loc_87B6:
    lda $288E
    sta $2884
    lda $288D
    asl
    rol $2884
    asl
    rol $2884
    asl
    rol $2884
    lda $2884
    bcc loc_87D4
    eor #$FF
    adc #$00
loc_87D4:
    sta $28F0
    lda $2898
    sta $28F1
    jsr calc_8FF9
    ldy #$00
    bit $2884
    bpl loc_87F0
    dey
    eor #$FF
    clc
    adc #$01
    bcc loc_87F0
    iny
loc_87F0:
    sty $2896
    asl
    rol $2896
    asl
    rol $2896
    asl
    rol $2896
    sta $2895
    lda $288B
    sta zp_screen_ptr_lo
    lda $288C
    ldy #$04
loc_880C:
    cmp #$80
    ror
    ror zp_screen_ptr_lo
    dey
    bne loc_880C
    sta zp_screen_ptr_hi
    clc
    lda $2899
    adc zp_screen_ptr_lo
    sta $2899
    lda $289A
    adc zp_screen_ptr_hi
    and #$3F
    sta $289A
    jsr render_8F67
    clc
    lda cam_heading_lo
    adc $2891
    sta cam_heading_lo
    lda cam_heading_hi
    adc $2892
    sta cam_heading_hi
    lda $2892
    clc
    lda cam_pitch_lo
    adc $2893
    sta cam_pitch_lo
    lda cam_pitch_hi
    adc $2894
    sta cam_pitch_hi
    clc
    lda $289F
    adc $2895
    sta $289F
    lda cam_altitude
    adc $2896
    cmp #$FF
    bne loc_886B
    lda #$00
loc_886B:
    cmp #$50
    bcc loc_8887
    ldy #$FF
    ldx ent_type
    cpx #$02
    bne loc_8882
    cmp #$60
    bcc loc_887F
    sty $283F
loc_887F:
    jmp loc_8887
loc_8882:
    sty $289F
    lda #$4F
loc_8887:
    sta cam_altitude
    lda $288D
    rol
    lda $288E
    adc #$0C
    sta $287A
    lda $2889
    rol
    lda $288A
    adc #$05
    sta $2878
    sec
    lda #$3A
    ldy $2841
    bne loc_88B0
    sbc $289A
    jmp loc_88B2
loc_88B0:
    sbc $14
loc_88B2:
    and #$3F
    tay
    and #$03
    sta $283A
    tya
    lsr
    lsr
    sta $281E
    lda $2897
    asl
    lda $2898
    rol
    eor #$FF
    sta $0688
    sec
    sbc #$04
    sta ent_param_a
    jsr render_proc_8EA2
    ldy #$0C
    lda render_toggle
    cmp #$F0
    beq loc_88FE
    lda $0625
    bne loc_88F9
    sta $293D
    ldx $293C
    beq loc_8921
    dec $293C
    cpx RANDOM
    bcc loc_8921
    lda #$FF
    jmp loc_88FE
loc_88F9:
    lda #$FF
    sta $293C
loc_88FE:
    sta $293D
    ldx render_toggle
    bne loc_890A
    txa
    jmp loc_8921
loc_890A:
    cpx #$F0
    bcs loc_8913
    lda #$04
    jmp loc_8921
loc_8913:
    lda cam_altitude
    lsr
    lsr
    lsr
    eor #$0F
    cmp #$04
    bcs loc_8921
    lda #$04
loc_8921:
    sta ent_flags,y
    lda $2897
    asl
    lda $2898
    rol
    eor #$FF
    cmp #$0C
    bcs loc_8934
    lda #$0C
loc_8934:
    sta ent_state,y
    jsr queue_push
    lda $293E
    sta $2855
    lda $293F
    cmp #$80
    ror
    ror $2855
    sta $2856
    ldy #$00
    lda $2940
    sta $2857
    bpl loc_8957
    dey
loc_8957:
    tya
    ldy #$03
loc_895A:
    asl $2857
    rol
    dey
    bne loc_895A
    sta $2858
    ldy #$00
    lda $293F
    cmp #$80
    ror
    bpl loc_896F
    dey
loc_896F:
    clc
    adc $2831
    sta $2831
    tya
    adc $2832
    sta $2832
    ldy #$00
    lda $2940
    cmp #$80
    ror
    cmp #$80
    ror
    cmp #$80
    ror
    bpl loc_898E
    dey
loc_898E:
    clc
    adc $2833
    sta $2833
    tya
    adc $2834
    sta $2834
    lda $284E
    bne loc_89A4
    jmp loc_89D9
loc_89A4:
    bmi loc_89B3
    cmp #$01
    bne loc_89B0
    jsr render_8C38
    jmp loc_89B3
loc_89B0:
    jsr render_8CCB
loc_89B3:
    sec
    lda $2853
    sbc $2855
    sta $2853
    lda $2851
    sbc $2856
    sta $2851
    clc
    lda $2854
    adc $2857
    sta $2854
    lda $2852
    adc $2858
    sta $2852
loc_89D9:
    ldx $282A
    bmi loc_8A1B
    dex
    stx $282A
    bpl loc_89F8
    lda $2829
    bne loc_89EF
    jsr draw_8E6B
    jmp loc_89F5
loc_89EF:
    jsr draw_8E6B
    jsr pmg_fill_8B2D
loc_89F5:
    jmp loc_8A1B
loc_89F8:
    txa
    lsr
    lsr
    sta $2823
    cpx #$5A
    beq loc_8A15
    lda game_timer
    cmp #$1F
    bcc loc_8A18
    cpx #$3C
    beq loc_8A15
    cmp #$3D
    bcc loc_8A18
    cpx #$28
    bne loc_8A18
loc_8A15:
    jsr render_random_perturb_A665
loc_8A18:
    jsr draw_8A65
loc_8A1B:
    ldy $2943
    iny
    cpy #$07
    bcc loc_8A25
    ldy #$00
loc_8A25:
    sty $2943
    lda $28AD,y
    sta $293E
    lda $288B
    sta $28AD,y
    lda $28B4,y
    sta $293F
    lda $288C
    sta $28B4,y
    lda $28BB,y
    sta $2940
    lda $2888
    sta $28BB,y
    lda $28C2,y
    sta $2941
    lda $2878
    sta $28C2,y
    lda $28C9,y
    sta $2942
    lda $287A
    sta $28C9,y
    rts
draw_8A65:
    ; Writes to screen memory; Table access: $8B5C; 79 instructions
    sec
    lda $282B
    sbc $2855
    sta $282B
    lda $282C
    sbc $2856
    sta $282C
    lda $2856
    bpl loc_8A85
    bcc loc_8A82
    inc $282D
loc_8A82:
    jmp loc_8A8A
loc_8A85:
    bcs loc_8A8A
    dec $282D
loc_8A8A:
    clc
    lda $282E
    adc $2857
    sta $282E
    lda $282F
    adc $2858
    sta $282F
    lda $2858
    bmi loc_8AAA
    bcc loc_8AA7
    inc $2830
loc_8AA7:
    jmp loc_8AAF
loc_8AAA:
    bcs loc_8AAF
    dec $2830
loc_8AAF:
    sec
    lda $282B
    sbc $2831
    sta $282B
    lda $282C
    sbc $2832
    sta $282C
    lda $2832
    bpl loc_8ACF
    bcc loc_8ACC
    inc $282D
loc_8ACC:
    jmp loc_8AD4
loc_8ACF:
    bcs loc_8AD4
    dec $282D
loc_8AD4:
    clc
    lda $282E
    adc $2833
    sta $282E
    lda $282F
    adc $2834
    sta $282F
    lda $2834
    bmi loc_8AF4
    bcc loc_8AF1
    inc $2830
loc_8AF1:
    jmp loc_8AF9
loc_8AF4:
    bcs loc_8AF9
    dec $2830
loc_8AF9:
    lda $282D
    bne loc_8B27
    lda $2830
    bne loc_8B27
    ldx $282A
    lda $8B5C,x
    ldy cam_altitude
    cpy #$30
    bcc loc_8B12
    lda #$00
loc_8B12:
    cpy #$20
    bcc loc_8B19
    cmp #$80
    ror
loc_8B19:
    clc
    adc $282F
    sta $2824
    lda $282C
    sta $2827
    rts
loc_8B27:
    lda #$00
    sta $2827
    rts
pmg_fill_8B2D:
    ; Fill loop — indexed store of constant value; Sets color registers; Configures player-missile graphics; Writes to screen memory; Installs interrupt/event vectors; Modifies display list; Reads: CONSOL; Writes: GRACTL, IRQEN, SKCTL, DMACTL; Calls: draw_8340, render_random_perturb_AB39, timing_get_random_3F97, sound_init; Table access: $00D0, $061F, $0643, $5205; 201 instructions, 7 loops
    lda $282D
    bne loc_8B5B
    lda $2830
    bne loc_8B5B
    lda $282C
    cmp #$34
    bcc loc_8B5B
    cmp #$AC
    bcs loc_8B5B
    lda $282F
    cmp #$1A
    bcc loc_8B5B
    cmp #$6A
    bcs loc_8B5B
    lda #$20
    sta $287F
    jsr draw_8340
    jsr render_random_perturb_AB39
    jmp loc_3F77
loc_8B5B:
    rts
    :15 .byte $00                    ; $8B5C (15 zero bytes)
    :13 .byte $FF                    ; $8B6B (13 bytes)
    :11 .byte $FE                    ; $8B78 (11 bytes)
    :9 .byte $FD                    ; $8B83 (9 bytes)
    :7 .byte $FC                    ; $8B8C (7 bytes)
    :6 .byte $FB                    ; $8B93 (6 bytes)
    :5 .byte $FA                    ; $8B99 (5 bytes)
    :4 .byte $F9                    ; $8B9E (4 bytes)
    .byte $F8,$F8,$F8,$F7,$F7,$F7,$F6,$F6,$F6,$F5,$F5,$F4,$F4,$F4,$F3,$F3 ; $8BA2
    .byte $F2,$F2,$F1,$F1,$F0,$F0,$EF,$EF,$EE,$EE,$ED,$EC,$EC,$EB,$EB,$EA ; $8BB2
    .byte $E9,$E9,$E8,$E7,$E7,$E6,$E5,$E5,$E4,$E3,$E2,$E2,$E1,$E0,$DF,$DE ; $8BC2
    .byte $DE,$DD,$DC,$DB,$DA,$D9,$D9,$D8,$D7,$D6          ; $8BD2
draw_8BDC:
    ; Writes to screen memory; 44 instructions
    lda $288A
    bpl loc_8BF2
    clc
    lda $2889
    adc #$14
    sta $2889
    bcc loc_8BEF
    inc $288A
loc_8BEF:
    jmp loc_8C09
loc_8BF2:
    sec
    lda $2889
    sbc #$14
    tay
    lda $288A
    sbc #$00
    bcs loc_8C03
    lda #$00
    tay
loc_8C03:
    sta $288A
    sty $2889
loc_8C09:
    lda $061D
    beq loc_8C22
    sec
    lda $289F
    sbc #$30
    sta $289F
    lda cam_altitude
    sbc #$00
    sta cam_altitude
    jmp loc_8C37
loc_8C22:
    lda flight_param
    bne loc_8C37
    lda $067A
    bne loc_8C37
    inc flight_param
    lda $282A
    bmi loc_8C37
    jmp draw_8E6B
loc_8C37:
    rts
render_8C38:
    ; Writes to screen memory; Bit extraction/packing via shifts and rotates; Table access: $071B; 76 instructions
    lda #$00
    sta $2859
    sta $285C
    lda $2922
    sta $285A
    lda $2923
    sta $285B
    lda $2924
    sta $285D
    lda $2925
    sta $285E
    lda $2926
    sta $285F
    clc
    lda $2927
    adc #$08
    bcc loc_8C68
    lda #$FF
loc_8C68:
    sta $2860
    ldy #$00
    lda $2928
    sta $2861
    lda $2929
    sta $2862
    bpl loc_8C7C
    dey
loc_8C7C:
    tya
    asl $2861
    rol $2862
    rol
    asl $2861
    rol $2862
    rol
    sta $2863
    ldy #$00
    lda $292A
    sta $2864
    lda $292B
    sta $2865
    bpl loc_8C9F
    dey
loc_8C9F:
    tya
    asl $2864
    rol $2865
    rol
    asl $2864
    rol $2865
    rol
    sta $2866
    lda $292E
    sta $2867
    lda $292F
    cmp #$80
    ror
    ror $2867
    sec
    sbc #$01
    sta $2868
    ldx #$11
    jmp queue_push_flagged
render_8CCB:
    ; Writes to screen memory; Bit extraction/packing via shifts and rotates; Calls: render_random_perturb_8E0B, render_99C2, draw_8E00, draw_8E00; Table access: $071B, $0900, $0A00; 143 instructions
    clc
    lda $2859
    adc $2861
    sta $2859
    lda $285A
    adc $2862
    sta $285A
    sta $27FD
    lda $285B
    adc $2863
    and #$0F
    sta $285B
    sta $27FE
    clc
    lda $285C
    adc $2864
    sta $285C
    lda $285D
    adc $2865
    sta $285D
    sta $27FF
    lda $285E
    adc $2866
    and #$0F
    sta $285E
    sta $2800
    clc
    lda $285F
    adc $2867
    sta $285F
    lda $2860
    adc $2868
    bcc loc_8D2F
    bit $2868
    bmi loc_8D2C
    lda #$FF
loc_8D2C:
    jmp loc_8D36
loc_8D2F:
    bit $2868
    bpl loc_8D36
    lda #$00
loc_8D36:
    sta $2860
    lda $282A
    bmi loc_8D41
    jsr render_random_perturb_8E0B
loc_8D41:
    lda $285D
    asl
    lda $285E
    adc #$00
    asl
    asl
    asl
    asl
    sta zp_screen_ptr_lo
    lda $285A
    asl
    lda $285B
    adc #$00
    and #$0F
    ora zp_screen_ptr_lo
    sta $2869
    tay
    .byte $B9                     ; $8D61
loc_8D62:
    .byte $00                     ; $8D62
    .byte $0A                     ; $8D63
    beq loc_8D8E
    clc
    lda dat_0900,y
    adc #$10
    bcc loc_8D70
    lda #$FF
loc_8D70:
    cmp $2860
    bcc loc_8D8E
    lda $285A
    cmp #$30
    bcc loc_8D80
    cmp #$D0
    bcc loc_8D8E
loc_8D80:
    lda $285D
    cmp #$30
    bcc loc_8D8B
    cmp #$D0
    bcc loc_8D8E
loc_8D8B:
    jmp loc_8DB0
loc_8D8E:
    jsr render_99C2
    lda $2801
    cmp $2860
    bcs loc_8D9A
    rts
loc_8D9A:
    lda $285A
    cmp #$30
    bcc loc_8DA5
    cmp #$D0
    bcc draw_8DF6
loc_8DA5:
    lda $285D
    cmp #$30
    bcc loc_8DB0
    cmp #$D0
    bcc draw_8DF6
loc_8DB0:
    ldy $2869
    lda pilot_map,y
    beq draw_8DF6
    cmp #$F8
    bcs draw_8DF6
    cmp #$64
    bcs loc_8DC6
    jsr draw_8E00
    jmp draw_8DF6
loc_8DC6:
    bne loc_8DD9
    jsr draw_8E00
    jsr game_logic_bcd_add_6561
    lda terrain_vis_flag
    ora #$80
    sta terrain_vis_flag
    jmp draw_8DF6
loc_8DD9:
    cmp #$80
    bne loc_8DEB
    jsr draw_8E00
    jsr game_logic_bcd_add_6561
    lda #$40
    sta $0635
    jmp draw_8DF6
loc_8DEB:
    jsr draw_8E00
    jsr render_helper_652D
    lda #$49
    sta $0635
draw_8DF6:
    ; Writes to screen memory; Table access: $071B; 20 instructions
    lda #$80
    sta $284E
    ldx #$0E
    jmp queue_push_flagged
draw_8E00:
    ; Writes to screen memory; Table access: $071B, $0A00; 33 instructions
    lda #$FC
    sta pilot_map,y
    sta $2847
    jmp draw_8E77
render_random_perturb_8E0B:
    ; Reads POKEY RANDOM for randomization; Writes to screen memory; Arithmetic with shifts; Reads: SKRES; Calls: draw_8340, draw_8DF6, draw_8E77, game_logic_bcd_add_656B; Table access: $8EAB; 64 instructions
    lda $2823
    adc #$04
    sec
    sbc $284E
    bcs loc_8E1A
    eor #$FF
    adc #$01
loc_8E1A:
    cmp #$04
    bcs loc_8E6A
    sec
    lda $2851
    sbc #$0C
    sbc $2827
    bcs loc_8E2D
    eor #$FF
    adc #$01
loc_8E2D:
    sta zp_screen_ptr_lo
    clc
    lda $2852
    adc #$42
    sec
    sbc $2824
    bcs loc_8E3F
    eor #$FF
    adc #$01
loc_8E3F:
    clc
    adc zp_screen_ptr_lo
    ror
    lsr
    ldx $284E
    cmp $8EAB,x
    bcs loc_8E6A
    lda #$00
    sta $2829
    jsr draw_8340
    jsr draw_8DF6
    jsr draw_8E77
    lda #$50
    sta score_lo
    lda #$02
    sta score_hi
    jsr game_logic_bcd_add_656B
    jmp render_random_perturb_A665
loc_8E6A:
    rts
.proc draw_8E6B
    ; Writes to screen memory; 5 instructions
    lda #$FF
    sta $2823
    sta $282A
    sta $2829
    rts
.endp

draw_8E77:
    ; Writes to screen memory; Table access: $071B; 29 instructions
    inc viewport_state
    lda #$7E
    sta $DC
    lda #$7C
    sta $DE
    lda #$76
    sta $DD
    lda #$78
    sta $DB
    lda #$02
    sta $2913
    ldx #$0F
    jmp queue_push_flagged
render_util_8E94:
    ; Calls: queue_push, queue_push; Table access: $071B; 24 instructions
    sta $0678
render_proc_8E97:
    ; Calls: queue_push, queue_push; Table access: $071B; 23 instructions
    sta $067A
    sta ent_state
render_proc_8E9D:
    ; Calls: queue_push, queue_push; Table access: $071B; 21 instructions
    ldy #$0B
    jsr queue_push
render_proc_8EA2:
    ; Calls: queue_push; Table access: $071B; 19 instructions
    ldy #$0D
    jsr queue_push
    iny
    jmp queue_push
    .byte $28,$28,$18,$10,$0C,$0B,$0A,$09,$08,$08,$07,$07,$07 ; $8EAB
    :4 .byte $06                    ; $8EB8 (4 bytes)
    :13 .byte $05                    ; $8EBC (13 bytes)
.proc render_8EC9
    ; Writes to screen memory; Arithmetic with shifts; 73 instructions
    sec
    lda $27F1
    sbc $27F2
    sta $2886
    clc
    lda $27F1
    adc $27F2
    lsr
    cmp cam_altitude
    bcs loc_8EF2
    lda $2886
    bpl loc_8EE9
    eor #$FF
    adc #$01
loc_8EE9:
    cmp #$10
    bcs loc_8EF2
    lda #$00
    sta $2886
loc_8EF2:
    lda $293D
    bne loc_8EFD
    lda #$C8
    sta $2888
    rts
loc_8EFD:
    ldx #$38
    lda $1027
    and #$AA
    bne loc_8F16
    lda $1057
    and #$AA
    bne loc_8F16
    lda $061D
    cmp #$2A
    bcc loc_8F16
    ldx #$00
loc_8F16:
    stx $2888
    lda cam_altitude
    cmp #$2A
    bcc loc_8F29
    lda $288E
    bmi loc_8F29
    lda #$C8
    bne loc_8F35
loc_8F29:
    lda #$20
    sbc cam_altitude
    cmp #$80
    ror
    bmi loc_8F35
    lda #$00
loc_8F35:
    clc
    adc $2888
    sta $2888
    lda $2937
    cmp #$30
    bcc loc_8F48
    cmp #$D1
    bcs loc_8F48
    rts
loc_8F48:
    asl
    clc
    adc $2886
    sta $2886
    lda $2938
    cmp #$20
    bcc loc_8F5C
    cmp #$E1
    bcs loc_8F5C
    rts
loc_8F5C:
    asl
    eor #$FF
    clc
    adc $2888
    sta $2888
    rts
.endp

.proc render_8F67
    ; Writes to screen memory; Bit extraction/packing via shifts and rotates; Calls: calc_8FF9, calc_8FF9; Table access: $28F2, $900E; 60 instructions
    lda $289A
    sta $28F1
    lda $2899
    rol
    rol $28F1
    rol
    rol $28F1
    ldx #$00
    stx $2892
    lda $28F1
    asl
    bcc loc_8F86
    dec $2892
loc_8F86:
    asl
    bcc loc_8F8A
    dex
loc_8F8A:
    txa
    eor $2892
    sta $2894
    lda #$00
    rol
    tax
    lda $28F1
    and #$3F
    sta $28F2
    sta $28F3
    eor #$3F
    sta $28F2,x
    inc $28F2,x
    ldx $28F3
    lda $900E,x
    sta $28F1
    lda $2898
    sta $28F0
    jsr calc_8FF9
    sta $2891
    cmp #$00
    bne loc_8FC4
    sta $2892
loc_8FC4:
    lda $2892
    beq loc_8FD0
    clc
    sbc $2891
    sta $2891
loc_8FD0:
    ldx $28F2
    lda $900E,x
    sta $28F1
    lda $2898
    sta $28F0
    jsr calc_8FF9
    sta $2893
    cmp #$00
    bne loc_8FEC
    sta $2894
loc_8FEC:
    lda $2894
    beq loc_8FF8
    clc
    sbc $2893
    sta $2893
loc_8FF8:
    rts
.endp

.proc calc_8FF9
    ; Arithmetic with shifts; 9 instructions, 2 loops
    lda #$00
    beq loc_9003
loc_8FFD:
    lsr $28F1
    adc $28F1
loc_9003:
    asl $28F0
    bcs loc_8FFD
    lsr $28F1
    bne loc_9003
    rts
.endp

    .byte $00,$06,$0C,$12,$18,$1F,$25,$2B,$31,$37,$3D,$44,$4A,$4F,$55,$5B ; $900E
    .byte $61,$67,$6D,$72,$78,$7D,$83,$88,$8D,$92,$97,$9C,$A1,$A6,$AB,$AF ; $901E
    .byte $B4,$B8,$BC,$C1,$C5,$C9,$CC,$D0,$D4,$D7,$DA,$DD,$E0,$E3,$E6,$E9 ; $902E
    .byte $EB,$ED,$F0,$F2,$F4,$F5,$F7,$F8,$FA,$FB,$FC,$FD,$FD,$FE,$FE,$FE ; $903E
    .byte $FF,$00,$42,$83,$C2,$FE,$36,$6A,$98,$BF,$DE,$F5,$03,$05,$FD,$E8 ; $904E
    .byte $C5,$95,$56,$06,$A6,$34,$B0,$18,$6C,$AB,$D4,$E7,$E2,$C5,$8F,$3F ; $905E
    .byte $D5,$4F,$AE,$F1,$16,$1E,$07,$D1,$7B,$06,$70,$B8,$DF,$E3,$C5,$84 ; $906E
    .byte $1F,$96,$E9,$17,$21,$05,$C3,$5B,$CD,$19,$3E,$3D,$15,$C5,$4F,$B1 ; $907E
    .byte $EC,$00,$01                                      ; $908E
    :9 .byte $00                    ; $9091 (9 zero bytes)
    .byte $01                                              ; $909A
    :4 .byte $00                    ; $909B (4 zero bytes)
    .byte $01                                              ; $909F
    :4 .byte $00                    ; $90A0 (4 zero bytes)
    .byte $01,$00,$00,$00,$01,$00,$00,$01,$00,$00,$01,$00,$01,$00,$01,$00 ; $90A4
    .byte $01,$00,$01,$00                                  ; $90B4
    :5 .byte $01                    ; $90B8 (5 bytes)
.proc sound_init
    ; Fill loop — indexed store of constant value; Updates sound registers; Writes: AUDCTL; 38 instructions, 2 loops
    ldy #$0E
    lda #$00
    sta cmd_queue_ptr
    sta $073C
loc_90C7:
    sta ent_flags,y
    sta ent_active_flags,y
    sta ent_param_a,y
    sta ent_param_b,y
    sta ent_param_c,y
    sta ent_param_d,y
    sta ent_param_e,y
    sta ent_param_f,y
    sta ent_param_g,y
    sta ent_param_h,y
    sta ent_param_i,y
    dey
    bne loc_90C7
    sta $0716
    lda #$02
    sta $0717
    ldy #$04
loc_90F5:
    tya
    asl
    sta ent_active_flags,y
    tax
    lda #$00
    sta PDVS,x
    dey
    bne loc_90F5
    sta $0708
    sta $070A
    lda #$02
    sta $0714
    lda #$06
    sta $0715
    lda #$60
    sta AUDCTL
    rts
.endp

sound_dispatch_9119:
    ; Calls: sound_set_channel, queue_push_flagged, render_table_lookup_92FC, entity_swap; 86 instructions
    lda $062A
    beq loc_9121
    jsr sound_set_channel
loc_9121:
    ldy #$0E
loc_9123:
    lda #$00
    sta $071A
    lda ent_param_g,y
    beq loc_9161
    clc
    adc ent_param_h,y
    bpl loc_9138
    adc #$0A
    jmp loc_913E
loc_9138:
    cmp #$2D
    bcc loc_913E
    lda #$2C
loc_913E:
    sta ent_param_h,y
    tax
    lda $9090,x
    beq loc_9161
    clc
    lda ent_state,y
    adc ent_param_e,y
    sta ent_state,y
    cmp ent_param_f,y
    bne loc_915E
    lda #$00
    sta ent_param_g,y
    inc $071A
loc_915E:
    jsr render_table_lookup_92FC
loc_9161:
    lda ent_param_c,y
    beq loc_919C
    clc
    adc ent_param_d,y
    bpl loc_9171
    adc #$0A
    jmp loc_9177
loc_9171:
    cmp #$2D
    bcc loc_9177
    lda #$2C
loc_9177:
    sta ent_param_d,y
    tax
    lda $9090,x
    beq loc_919C
    clc
    lda ent_flags,y
    adc ent_param_a,y
    and #$0F
    sta ent_flags,y
    cmp ent_param_b,y
    bne loc_9199
    lda #$00
    sta ent_param_c,y
    inc $071A
loc_9199:
    jsr queue_push
loc_919C:
    lda $071A
    beq loc_91A7
    ldx ent_param_i,y
    jsr queue_push_flagged
loc_91A7:
    dey
    beq loc_91AD
    jmp loc_9123
loc_91AD:
    lda #$1F
    cmp cmd_queue_ptr
    bcs loc_91B7
    sta cmd_queue_ptr
loc_91B7:
    cmp $073C
    bcs loc_91BF
    sta $073C
loc_91BF:
    ldy $073C
    cpy cmd_queue_ptr
    beq loc_91E6
    lda cmd_queue_data,y
    bpl loc_91D5
    and #$7F
    tax
    jsr entity_load_state
    jmp loc_91D9
loc_91D5:
    tay
    jsr entity_swap
loc_91D9:
    dec $073C
    bpl loc_91E3
    lda #$1F
    sta $073C
loc_91E3:
    jmp loc_91BF
loc_91E6:
    rts
sound_set_channel:
    ; Updates sound registers; Reads: SKRES; Writes: AUDF1, AUDC1, AUDF3, AUDC3; Table access: $9270; 60 instructions
    cmp #$01
    bne loc_91EE
    jmp loc_9219
loc_91EE:
    dec $062C
    bpl loc_920F
    lda RANDOM
    and #$03
    clc
    adc #$01
    sta $062C
    lda RANDOM
    tay
    ora #$70
    sta AUDF3
    tya
    and #$7F
    ora #$17
    sta AUDF1
loc_920F:
    lda #$04
    sta AUDCTL
    lda #$03
    jmp loc_9267
loc_9219:
    ldy $062B
    dey
    bpl loc_9221
    ldy #$07
loc_9221:
    sty $062B
    sec
    lda $062E
    sbc $9270,y
    sta $062E
    sta AUDF1
    sec
    lda $062F
    sbc $9270,y
    sta $062F
    sta AUDF3
    ldy $062D
    dec $0630
    bpl loc_9256
    iny
    iny
    cpy #$0F
    bcc loc_9253
    ldy #$0F
    lda #$32
    sta $0630
loc_9253:
    jmp loc_9263
loc_9256:
    bne loc_9263
    dey
    cpy #$06
    bne loc_9260
    inc $062A
loc_9260:
    inc $0630
loc_9263:
    sty $062D
    tya
loc_9267:
    ora #$A0
    sta AUDC1
    sta AUDC3
    rts
    .byte $01,$00,$FF,$00,$FF,$00,$01,$00                  ; $9270
    :8 .byte $04                    ; $9278 (8 bytes)
    :8 .byte $06                    ; $9280 (8 bytes)
    :8 .byte $08                    ; $9288 (8 bytes)
queue_push:
    ; Table access: $071B; 15 instructions
    txa
    pha
    tya
loc_9293:
    ldx cmd_queue_ptr
    cpx #$20
    bcc loc_929C
    ldx #$1F
loc_929C:
    sta cmd_queue_data,x
    dex
    bpl loc_92A4
    ldx #$1F
loc_92A4:
    stx cmd_queue_ptr
    pla
    tax
    rts
entity_swap:
    ; Calls: render_table_lookup_9308, render_proc_9344, render_proc_931F, render_table_lookup_9308; Table access: $066D, $0707; 34 instructions
    tya
    pha
    lda ent_active_flags,y
    bne loc_92D6
    lda ent_flags,y
    and #$0F
    cmp $0716
    bcc loc_92F9
    bne loc_92C2
    cpy $0717
    bcc loc_92F9
loc_92C2:
    ldx $0717
    lda ent_active_flags,x
    sta ent_active_flags,y
    lda #$00
    sta ent_active_flags,x
    jsr render_table_lookup_9308
    jmp loc_92F6
loc_92D6:
    cpy #$0D
    bcs loc_92F3
    jsr render_proc_9344
    ldx $0717
    cpx $0719
    beq loc_92F3
    ldy $0719
    lda ent_active_flags,x
    sta ent_active_flags,y
    lda #$00
    sta ent_active_flags,x
loc_92F3:
    jsr render_table_lookup_9308
loc_92F6:
    jsr render_proc_931F
loc_92F9:
    pla
    tay
    rts
.proc render_table_lookup_92FC
    ; Table access: $067B, $0707, $D1FE; 5 instructions
    ldx ent_active_flags,y
    beq loc_9307
    lda ent_state,y
    sta $D1FE,x
loc_9307:
    rts
.endp

.proc render_table_lookup_9308
    ; 9 instructions
    ldx ent_active_flags,y
    beq loc_931E
    lda ent_state,y
    sta $D1FE,x
    lda ent_flags,y
    and #$0F
    ora ent_type,y
    sta PDVS,x
loc_931E:
    rts
.endp

.proc render_proc_931F
    ; Table access: $066D, $0707; 16 instructions, 1 loop
    lda #$10
    sta $0718
    ldx #$00
loc_9326:
    inx
    lda ent_active_flags,x
    beq loc_933F
    lda ent_flags,x
    and #$0F
    cmp $0718
    bcs loc_933F
    sta $0718
    sta $0716
    stx $0717
loc_933F:
    cpx #$0C
    bcc loc_9326
    rts
.endp

.proc render_proc_9344
    ; Table access: $066D, $0707; 16 instructions, 1 loop
    ldx #$00
    stx $0718
loc_9349:
    inx
    lda ent_active_flags,x
    beq loc_9354
    cpx $0717
    bne loc_9364
loc_9354:
    lda ent_flags,x
    and #$0F
    cmp $0718
    bcc loc_9364
    sta $0718
    stx $0719
loc_9364:
    cpx #$0C
    bcc loc_9349
    rts
.endp

    .byte $05,$05,$05,$06,$06,$04,$06,$06,$07,$03,$04,$03,$04,$04,$07,$03 ; $9369
    .byte $04,$03,$03,$03,$05,$08,$08,$08,$09,$08,$09,$0A,$03,$03,$01,$02 ; $9379
    .byte $03,$44,$00,$00,$21,$2F,$8F,$20,$27,$0F,$A1,$C1,$AF,$CF,$0F,$00 ; $9389
    .byte $A8,$8A,$A8,$08,$A4,$00,$07,$01,$07,$01,$46,$2E,$C3,$A8,$A8,$A0 ; $9399
    .byte $A0,$A4,$3A,$00,$00,$00,$0F,$00,$00,$07,$FF,$FF,$43,$FF,$43,$BF ; $93A9
    .byte $FF,$1E,$00,$0F,$FF,$2F,$00,$00,$00,$40,$18,$08,$B1,$28,$3F,$1F ; $93B9
    .byte $FE,$FF,$0B,$00                                  ; $93C9
    :4 .byte $01                    ; $93CD (4 bytes)
    .byte $FF,$01,$FF,$FF,$01,$01,$FF,$FF,$FF,$01,$FF,$01,$FF,$01,$FF,$00 ; $93D1
    .byte $01,$01,$01,$FF,$01,$01,$FF,$FF,$FF,$00,$00,$FF,$00,$01,$01,$02 ; $93E1
    .byte $FF,$FF,$01,$FF,$FF,$01,$01,$FF,$FF,$FF,$01,$FF,$01,$F8,$F8,$FC ; $93F1
    .byte $00,$FF,$01,$F9,$FF,$FE,$FE,$FD,$F8,$F8,$00,$00,$FC,$00,$08,$00 ; $9401
    .byte $0F,$00,$00,$07,$00,$00,$0F,$0F,$00,$00,$00,$0F,$00,$0F ; $9411
    :4 .byte $00                    ; $941F (4 zero bytes)
    .byte $01,$07                                          ; $9423
    :11 .byte $00                    ; $9425 (11 zero bytes)
    .byte $0F                                              ; $9430
    :5 .byte $2D                    ; $9431 (5 bytes)
    .byte $28,$28,$0F,$0F,$2D,$2D,$2A,$2D,$2A,$2D,$02,$2D,$04,$00,$2D,$0F ; $9436
    .byte $2D,$02,$2D,$2A,$0A,$02,$02,$00,$00,$03          ; $9446
    :8 .byte $01                    ; $9450 (8 bytes)
    :25 .byte $00                    ; $9458 (25 zero bytes)
    .byte $FF,$04,$04,$01,$01,$06,$01,$01,$1F              ; $9471
    :4 .byte $FC                    ; $947A (4 bytes)
    .byte $FF,$FC,$FC                                      ; $947E
    :8 .byte $2D                    ; $9481 (8 bytes)
    .byte $00,$15,$00,$05,$00,$00,$08,$00,$00,$0C,$0D,$00,$00,$00,$09,$00 ; $9489
    .byte $06                                              ; $9499
    :4 .byte $00                    ; $949A (4 zero bytes)
    .byte $17,$18                                          ; $949E
    :5 .byte $00                    ; $94A0 (5 zero bytes)
    .byte $1E,$12,$00,$00,$00                              ; $94A5
queue_push_flagged:
    ; Table access: $071B; 16 instructions
    txa
    pha
    ora #$80
    jmp loc_9293
.proc entity_load_state
    ; Calls: queue_push; 41 instructions
    txa
    pha
    tya
    pha
    dex
    bmi loc_950B
    ldy $9369,x
    lda $938A,x
    pha
    and #$F0
    sta ent_type,y
    pla
    and #$0F
    sta ent_flags,y
    lda $93AB,x
    sta ent_state,y
    lda $93CC,x
    sta ent_param_c,y
    lda $93ED,x
    sta ent_param_a,y
    lda $940E,x
    sta ent_param_b,y
    lda $942F,x
    sta ent_param_d,y
    lda $9450,x
    sta ent_param_g,y
    beq loc_9502
    lda $9471,x
    sta ent_param_e,y
    lda $9479,x
    sta ent_param_f,y
    lda $9481,x
    sta ent_param_h,y
loc_9502:
    lda $9489,x
    sta ent_param_i,y
    jsr queue_push
loc_950B:
    pla
    tay
    pla
    tax
    rts
.endp

.proc sound_thrust
    ; Updates sound registers; Writes: AUDCTL; 8 instructions
    ldx #$01
    stx $0740
    dex
    stx AUDCTL
    stx $073D
    dex
    stx $073F
    rts
.endp

.proc sound_explosion
    ; Updates sound registers; Writes: AUDC1, AUDC2, AUDC3; Calls: wait_2, sound_warning; Table access: $D1FF; 20 instructions
    jsr wait_2
    lda $0740
    beq loc_9550
    dec $073D
    bpl loc_9531
    jsr sound_warning
loc_9531:
    lda $073D
    lsr
    cmp #$03
    bcc loc_953B
    lda #$02
loc_953B:
    adc #$A0
    sta AUDC1
    sta AUDC2
    sta AUDC3
    ldy $073E
    beq loc_9550
    adc #$01
    sta PDVS,y
loc_9550:
    rts
.endp

.proc sound_laser
    ; Updates sound registers; Writes: AUDC1, AUDC2, AUDC3, AUDC4; 9 instructions
    lda #$00
    sta $0740
    sta AUDC1
    sta AUDC2
    sta AUDC3
    sta AUDC4
    ldy #$60
    sty AUDCTL
    rts
.endp

.proc sound_warning
    ; Updates sound registers; Writes: AUDF1, AUDF2, AUDF3, AUDF4; 32 instructions, 2 loops
    ldx $073F
loc_956B:
    inx
loc_956C:
    lda $95EE,x
    bne loc_9574
    tax
    beq loc_956C
loc_9574:
    bpl loc_9599
    and #$1F
    tay
    lda $95C7,y
    sta AUDF1
    lda $95BA,y
    sta AUDF2
    lda $95AD,y
    sta AUDF3
    lda $95D4,y
    sta AUDF4
    lda $95E1,y
    sta AUDC4
    bne loc_956B
loc_9599:
    stx $073F
    tay
    and #$1F
    sta $073D
    tya
    ror
    ror
    ror
    ror
    and #$06
    sta $073E
    rts
.endp

    .byte $43,$38,$2C,$3B,$38,$25,$32,$2C                  ; $95AD
    .byte $32,$2A,$2A,$25,$21,$54,$43,$38                  ; $95B5
    .byte $47,$43,$2C,$3B,$38,$3F,$38,$38                  ; $95BD
    .byte $2C,$2A,$71,$54,$43,$5A,$5A,$38                  ; $95C5
    .byte $4B,$4B,$54,$43,$43,$38,$38,$0A                  ; $95CD
    .byte $0A,$D7,$3F,$D7,$F2,$F2,$78,$CB                  ; $95D5
    .byte $0A,$15,$78,$0A,$22,$22                          ; $95DD
    :7 .byte $C2                    ; $95E3 (7 bytes)
    .byte $22,$22,$C2,$22,$80,$50,$22,$43                  ; $95EA
    .byte $6B,$4B,$81,$50,$22,$43,$77,$82                  ; $95F2
    .byte $50,$22,$43,$6B,$83,$2B,$84,$50                  ; $95FA
    .byte $22,$43                                          ; $9602
loc_9604:
    .byte $77,$84,$50,$22,$43,$6B,$4B,$82                  ; $9604
    .byte $50,$22,$43,$77,$85,$50,$22,$43                  ; $960C
    .byte $6B,$86,$2B,$87,$50,$22,$43,$71                  ; $9614
    .byte $45,$88,$50,$22,$43,$89,$71,$45                  ; $961C
    .byte $88,$50,$22,$43,$8A,$77,$88,$50                  ; $9624
    .byte $22,$43,$8B,$6B,$2B,$8C,$50,$22                  ; $962C
    .byte $43,$77,$88,$50,$22,$43,$89,$71                  ; $9634
    .byte $45,$88,$50,$22,$43,$8A,$77,$88                  ; $963C
    .byte $50,$22,$43,$8B,$6B,$2B,$8C,$50                  ; $9644
    .byte $22,$43,$77,$00                                  ; $964C
.proc sound_pickup
    ; Updates sound registers; Writes: AUDCTL; 7 instructions
    lda #$00
    sta AUDCTL
    sta $0650
    lda #$01
    sta $0652
    sta slot_available
    rts
.endp

sound_landing:
    ; Updates sound registers; Writes: AUDCTL; 12 instructions
    lda #$49
    sta $0656
    lda #$97
    sta $0657
    jmp sound_pickup
sound_ambient:
    ; Fill loop — indexed store of constant value; Updates sound registers; Arithmetic with shifts; Writes: AUDF1, AUDC1, AUDC2, AUDC3; Calls: sound_init; 78 instructions, 2 loops
    lda $0650
    beq loc_9689
    dec $0650
    bne loc_9686
    lda #$FF
    sta $064E
    sta $064C
    sta $064A
    sta $0648
loc_9686:
    jmp loc_96F1
loc_9689:
    dec $0652
    bne loc_96F1
    clc
    lda $0656
    sta zp_grid_x
    adc #$05
    sta $0656
    lda $0657
    sta $89
    adc #$00
    sta $0657
    ldy #$00
    lda (zp_grid_x),y
    bne loc_96C4
    sta slot_available
    sta AUDC1
    sta AUDC2
    sta AUDC3
    sta AUDC4
    lda $061E
    bne loc_96C3
    jsr sound_init
    jmp sound_thrust
loc_96C3:
    rts
loc_96C4:
    sta $0652
    lda #$04
    sta $0650
    ldx #$06
loc_96CE:
    iny
    lda (zp_grid_x),y
    bne loc_96DC
    sta $0647,x
    sta $0648,x
    jmp loc_96ED
loc_96DC:
    cmp #$01
    beq loc_96ED
    sta AUDF1,x
    lda #$50
    sta $0647,x
    lda #$FC
    sta $0648,x
loc_96ED:
    dex
    dex
    bpl loc_96CE
loc_96F1:
    ldx #$06
loc_96F3:
    clc
    lda $0647,x
    adc $0648,x
    bpl loc_96FE
    lda #$00
loc_96FE:
    sta $0647,x
    lsr
    lsr
    lsr
    eor $970F,x
    sta AUDC1,x
    dex
    dex
    bpl loc_96F3
    rts
    .byte $A0,$00,$A0,$00,$A0,$00,$C0,$0F                  ; $970F
    .byte $A1,$87,$65,$50,$03,$A1,$65,$50                  ; $9717
    .byte $43,$03,$01,$6B,$54,$47,$03,$01                  ; $971F
    .byte $65,$50,$43,$0F,$A1,$50,$43,$32                  ; $9727
    .byte $0F,$A1,$43,$38,$27,$0F,$F2,$4B                  ; $972F
    .byte $3B,$32,$06,$3F,$4B,$3B,$32,$06                  ; $9737
    .byte $D7,$78,$65,$4B,$33,$F2,$CB,$98                  ; $973F
    .byte $78,$00,$2D,$A1,$A0,$87,$71,$15                  ; $9747
    .byte $69,$00,$00,$00,$15,$CB,$A0,$CB                  ; $974F
    .byte $00,$2D,$F2,$98,$00,$00,$2D,$A1                  ; $9757
    .byte $98,$7F,$65,$15,$98,$98,$87,$6B                  ; $975F
    .byte $15,$01,$98,$7F,$65,$2D,$CB,$01                  ; $9767
    .byte $01,$01,$2D,$D7,$A0,$7F,$65,$2D                  ; $976F
    .byte $AA,$AA,$7F,$6B,$35,$A1,$A0,$87                  ; $9777
    .byte $71,$3D,$B6,$A0,$7F,$65,$45,$CB                  ; $977F
    .byte $CB,$98,$7F,$4D,$D7,$CB,$98,$7F                  ; $9787
    .byte $4D,$F2,$CB,$98,$7F,$25                          ; $978F
    :4 .byte $00                    ; $9795 (4 zero bytes)
    .byte $04,$00,$00,$00,$4B,$4D,$F2,$7F                  ; $9799
    .byte $5A,$50,$4D                                      ; $97A1
    :4 .byte $01                    ; $97A4 (4 bytes)
    .byte $00                                              ; $97A8
render_97A9:
    ; Writes to screen memory; Bit extraction/packing via shifts and rotates; Calls: render_99C2, draw_9AA5, draw_9AE7, render_copy_98B1; 109 instructions
    lda cam_heading_hi
    sta $27F8
    lda cam_heading_lo
    lsr $27F8
    ror
    lsr $27F8
    ror
    lsr $27F8
    ror
    lsr $27F8
    ror
    sta $27F7
    sta $27FD
    sta $2802
    lda $27F8
    sta $27FE
    sta $2803
    lda cam_pitch_hi
    sta $27FA
    lda cam_pitch_lo
    lsr $27FA
    ror
    lsr $27FA
    ror
    lsr $27FA
    ror
    lsr $27FA
    ror
    sta $27F9
    sta $27FF
    sta $2804
    lda $27FA
    sta $2800
    sta $2805
    lda cam_altitude
    cmp #$40
    bcc loc_9808
    lda #$3F
loc_9808:
    sta $27FC
    lda $289F
    asl
    rol $27FC
    asl
    rol $27FC
    sta $27FB
    jsr render_99C2
    lda $2801
    lsr
    adc #$00
    lsr
    adc #$00
    sta $27F3
    sec
    lda #$37
    sbc $27F3
    bcs loc_9832
    lda #$00
loc_9832:
    sta $281C
    sec
    lda $27FC
    sbc $2801
    bcc loc_984C
    sta $061D
    lda $2840
    bne loc_9849
    jsr draw_9AE7
loc_9849:
    jmp loc_986E
loc_984C:
    lda ent_type
    cmp #$00
    beq loc_9856
    jsr draw_9AA5
loc_9856:
    inc cam_altitude
    lda #$00
    sta $289F
    sta $061D
    lda $288E
    bpl loc_986E
    inc $288E
    lda #$00
    sta $288D
loc_986E:
    sec
    lda #$37
    sbc cam_altitude
    bcs loc_9887
    eor #$FF
    sec
    adc $281C
    cmp #$38
    bcc loc_9882
    lda #$38
loc_9882:
    sta $281C
    lda #$00
loc_9887:
    sta $281D
    jsr render_copy_98B1
    lda $066E
    cmp #$08
    bcc loc_989F
    lda #$01
    sta $2880
    jsr timing_draw_9B22
    jmp loc_98B0
loc_989F:
    ldy $2880
    beq loc_98B0
    cmp #$04
    bcs loc_98B0
    lda #$00
    sta $2880
    sta viewport_state
loc_98B0:
    rts
render_copy_98B1:
    ; Copy loop — indexed load/store transfer; Writes to screen memory; Bit extraction/packing via shifts and rotates; Calls: render_99C2, render_99C2; Table access: $2159, $2189, $52DF; 144 instructions, 3 loops
    lsr $2838
    bcs loc_98BA
    inc $2838
    rts
loc_98BA:
    lda $280A
    sta $2806
    lda $280B
    ldy #$04
loc_98C5:
    cmp #$80
    ror
    ror $2806
    dey
    bne loc_98C5
    sta $2807
    lda $280C
    sta $2808
    lda $280D
    ldy #$04
loc_98DC:
    cmp #$80
    ror
    ror $2808
    dey
    bne loc_98DC
    sta $2809
    lsr $2837
    bcs loc_9956
    inc $2837
    sec
    lda $2802
    sbc $2808
    sta $27FD
    lda $2803
    sbc $2809
    sta $27FE
    clc
    lda $2804
    adc $2806
    sta $27FF
    lda $2805
    adc $2807
    sta $2800
    jsr render_99C2
    lsr $2801
    lda $2801
    lsr
    sta $27F1
    lda $27FC
    lsr
    clc
    adc $288A
    clc
    adc #$0F
    sec
    sbc $2801
    bcs loc_9936
    lda #$00
loc_9936:
    sta $27F0
    sec
    lda #$38
    sbc $27F0
    bcs render_copy_9943
    lda #$00
render_copy_9943:
    ; Copy loop — indexed load/store transfer; Writes to screen memory; Table access: $2159, $2189, $52DF; 36 instructions, 1 loop
    sta $2835
    cmp $2845
    beq loc_9955
    sta $2845
    ldx #$00
    ldy #$00
    jmp loc_4842
loc_9955:
    rts
loc_9956:
    clc
    lda $2802
    adc $2808
    sta $27FD
    lda $2803
    adc $2809
    sta $27FE
    sec
    lda $2804
    sbc $2806
    sta $27FF
    lda $2805
    sbc $2807
    sta $2800
    jsr render_99C2
    lsr $2801
    lda $2801
    lsr
    sta $27F2
    lda $27FC
    lsr
    sec
    sbc $288A
    clc
    adc #$0F
    sec
    sbc $2801
    bcs loc_999C
    lda #$00
loc_999C:
    sta $27F0
    sec
    lda #$38
    sbc $27F0
    bcs render_copy_99A9
    lda #$00
render_copy_99A9:
    ; Copy loop — indexed load/store transfer; Writes to screen memory; Table access: $2159, $2189, $52DF; 39 instructions, 1 loop
    sta $2836
    cmp $2846
    beq loc_99C1
    sta $2846
    ldx #$04
    ldy #$10
    sec
    lda #$38
    sbc $2836
    jmp loc_4842
loc_99C1:
    rts
.proc render_99C2
    ; Writes to screen memory; Bit extraction/packing via shifts and rotates; Table access: $0900; 102 instructions, 6 loops
    lda $2800
    asl
    asl
    asl
    asl
    sta $27F6
    lda $27FE
    and #$0F
    ora $27F6
    tay
    lda dat_0900,y
    sta $27EA
    iny
    tya
    and #$0F
    ora $27F6
    tay
    lda dat_0900,y
    sta $27EB
    clc
    lda $27F6
    adc #$10
    sta $27F6
    lda $27FE
    and #$0F
    ora $27F6
    tay
    lda dat_0900,y
    sta $27EC
    iny
    tya
    and #$0F
    ora $27F6
    tay
    lda dat_0900,y
    sta $27ED
    ldx $27FD
    stx $27F4
    ldy #$08
    lda #$00
loc_9A19:
    asl $27F4
    bcs loc_9A2C
    lsr $27EB
    lsr $27EA
    adc $27EA
    dey
    bne loc_9A19
    beq loc_9A38
loc_9A2C:
    lsr $27EA
    lsr $27EB
    adc $27EB
    dey
    bne loc_9A19
loc_9A38:
    sta $27EE
    stx $27F4
    ldy #$08
    lda #$00
loc_9A42:
    asl $27F4
    bcs loc_9A55
    lsr $27ED
    lsr $27EC
    adc $27EC
    dey
    bne loc_9A42
    beq loc_9A61
loc_9A55:
    lsr $27EC
    lsr $27ED
    adc $27ED
    dey
    bne loc_9A42
loc_9A61:
    sta $27EF
    lda $27FF
    sta $27F5
    ldy #$08
    lda #$00
loc_9A6E:
    asl $27F5
    bcs loc_9A81
    lsr $27EF
    lsr $27EE
    adc $27EE
    dey
    bne loc_9A6E
    beq loc_9A8D
loc_9A81:
    lsr $27EE
    lsr $27EF
    adc $27EF
    dey
    bne loc_9A6E
loc_9A8D:
    sta $2801
    lsr
    lsr
    lsr
    lsr
    tay
    clc
    adc $2801
    sta $2801
    tya
    lsr
    adc $2801
    sta $2801
    rts
.endp

draw_9AA5:
    ; Writes to screen memory; Calls: queue_push, queue_push; Table access: $071B; 42 instructions
    lda $2897
    ora #$40
    sta $287E
    lda $0625
    bne loc_9AB3
    rts
loc_9AB3:
    lda #$34
    sta $3355
    lda #$0A
    cmp $066E
    beq loc_9ADA
    sta $0698
    sta $0699
    ldy #$01
    sty $068A
    sty $068B
    sty $06A6
    sty $06A7
    jsr queue_push
    iny
    jsr queue_push
loc_9ADA:
    lda $2898
    beq loc_9AE2
    dec $2898
loc_9AE2:
    ldx #$13
    jmp queue_push_flagged
draw_9AE7:
    ; Writes to screen memory; Calls: queue_push; Table access: $071B; 39 instructions
    lda $066E
    beq loc_9B21
    cmp #$02
    bcs loc_9AFA
    lda $06A6
    beq loc_9B21
    lda $0698
    beq loc_9B21
loc_9AFA:
    lda #$B4
    sta $3355
    lda #$00
    sta $287E
    sta $0698
    sta $0699
    lda #$FF
    sta $068A
    sta $068B
    ldy #$01
    sty $06A6
    sty $06A7
    jsr queue_push
    iny
    jmp queue_push
loc_9B21:
    rts
.proc timing_draw_9B22
    ; Writes to screen memory; 9 instructions
    lda #$01
    sta viewport_state
    lda #$3C
    sta $DE
    lda #$38
    sta $DD
    lda #$34
    sta $DB
    rts
.endp

    .byte $00,$00,$FF,$FF,$00,$3F,$00,$3F                  ; $9B34
.proc render_9B3C
    ; Writes to screen memory; Bit extraction/packing via shifts and rotates; Calls: render_9B80, render_9B80; 28 instructions
    lda $289A
    sta $280E
    lda $2899
    asl
    rol $280E
    asl
    rol $280E
    lda $2899
    lsr
    lsr
    lsr
    and #$07
    sta $280F
    jsr render_9B80
    lda $2813
    sta $280A
    lda $2814
    sta $280B
    clc
    lda $280E
    adc #$40
    sta $280E
    jsr render_9B80
    lda $2813
    sta $280C
    lda $2814
    sta $280D
    rts
.endp

render_9B80:
    ; Writes to screen memory; Bit extraction/packing via shifts and rotates; Calls: render_9C0E, render_9C0E; 51 instructions, 1 loop
    inc $280E
    jsr render_9C0E
    lda $2812
    sta $2818
    lda $2813
    sta $2819
    lda $2814
    sta $281A
    dec $280E
    jsr render_9C0E
    lda $2812
    sta $2815
    lda $2813
    sta $2816
    lda $2814
    sta $2817
    lda $280F
    sta $2811
    ldy #$03
loc_9BB8:
    lsr $2811
    bcc loc_9BDC
    clc
    lda $2812
    adc $2818
    sta $2812
    lda $2813
    adc $2819
    sta $2813
    lda $2814
    adc $281A
    sta $2814
    jmp loc_9BF8
loc_9BDC:
    clc
    lda $2812
    adc $2815
    sta $2812
    lda $2813
    adc $2816
    sta $2813
    lda $2814
    adc $2817
    sta $2814
loc_9BF8:
    asl $2815
    rol $2816
    rol $2817
    asl $2818
    rol $2819
    rol $281A
    dey
    bne loc_9BB8
    rts
render_9C0E:
    ; Writes to screen memory; Bit extraction/packing via shifts and rotates; Table access: $900E, $904F, $9B34, $9B38; 31 instructions
    lda #$00
    sta $2810
    lda $280E
    asl
    rol $2810
    asl
    rol $2810
    lsr
    lsr
    ldx $2810
    eor $9B38,x
    tay
    lda $9B34,x
    bne loc_9C3E
    sta $2814
    lda $900E,y
    sta $2813
    lda $904F,y
    sta $2812
    jmp loc_9C56
loc_9C3E:
    sec
    lda #$00
    sbc $904F,y
    sta $2812
    lda #$00
    sbc $900E,y
    sta $2813
    lda #$00
    sbc #$00
    sta $2814
loc_9C56:
    rts
bit_extract_9C57:
    ; Bit extraction/packing via shifts and rotates; 46 instructions
    sta zp_mul_cand_lo
.proc bit_extract_9C59
    ; Bit extraction/packing via shifts and rotates; 45 instructions
    lda #$00
    clc
    ror
    ror zp_mul_cand_hi
    bcc loc_9C64
    clc
    adc zp_mul_cand_lo
loc_9C64:
    ror
    ror zp_mul_cand_hi
    bcc loc_9C6C
    clc
    adc zp_mul_cand_lo
loc_9C6C:
    ror
    ror zp_mul_cand_hi
    bcc loc_9C74
    clc
    adc zp_mul_cand_lo
loc_9C74:
    ror
    ror zp_mul_cand_hi
    bcc loc_9C7C
    clc
    adc zp_mul_cand_lo
loc_9C7C:
    ror
    ror zp_mul_cand_hi
    bcc loc_9C84
    clc
    adc zp_mul_cand_lo
loc_9C84:
    ror
    ror zp_mul_cand_hi
    bcc loc_9C8C
    clc
    adc zp_mul_cand_lo
loc_9C8C:
    ror
    ror zp_mul_cand_hi
    bcc loc_9C94
    clc
    adc zp_mul_cand_lo
loc_9C94:
    ror
    ror zp_mul_cand_hi
    bcc loc_9C9C
    clc
    adc zp_mul_cand_lo
loc_9C9C:
    ror
    ror zp_mul_cand_hi
    rts
.endp

sub_9CA0:
    ; Calls: bit_extract_9C59; 28 instructions
    ldy #$00
    sta zp_mul_cand_lo
    cmp #$00
    bpl loc_9CAF
    tya
    sec
    sbc zp_mul_cand_lo
    sta zp_mul_cand_lo
    iny
loc_9CAF:
    lda zp_mul_cand_hi
    bpl loc_9CBB
    lda #$00
    sec
    sbc zp_mul_cand_hi
    sta zp_mul_cand_hi
    iny
loc_9CBB:
    jsr bit_extract_9C59
    cpy #$01
    bne loc_9CCE
    eor #$FF
    pha
    tya
    clc
    sbc zp_mul_cand_hi
    sta zp_mul_cand_hi
    pla
    adc #$00
loc_9CCE:
    rts
.proc mul16_signed
    ; 16-BIT SIGNED MULTIPLY — Multiplies two 16-bit signed values, returns 16-bit result. Used by 3D transform pipeline
    sta zp_mul_plier
    lda #$00
    sta zp_mul_result_hi
    sta zp_mul_result_lo
    lda zp_mul_cand_hi
    sta zp_mul_sign
    bpl loc_9CEA
    sec
    lda #$00
    sbc zp_mul_cand_lo
    sta zp_mul_cand_lo
    lda #$00
    sbc zp_mul_cand_hi
    sta zp_mul_cand_hi
loc_9CEA:
    ror zp_mul_plier
    bcc loc_9CF6
    lda zp_mul_cand_lo
    sta zp_mul_result_lo
    lda zp_mul_cand_hi
    sta zp_mul_result_hi
loc_9CF6:
    lsr zp_mul_result_hi
    ror zp_mul_result_lo
    ror zp_mul_plier
    bcc loc_9D0B
    clc
    lda zp_mul_cand_lo
    adc zp_mul_result_lo
    sta zp_mul_result_lo
    lda zp_mul_cand_hi
    adc zp_mul_result_hi
    sta zp_mul_result_hi
loc_9D0B:
    lsr zp_mul_result_hi
    ror zp_mul_result_lo
    ror zp_mul_plier
    bcc loc_9D20
    clc
    lda zp_mul_cand_lo
    adc zp_mul_result_lo
    sta zp_mul_result_lo
    lda zp_mul_cand_hi
    adc zp_mul_result_hi
    sta zp_mul_result_hi
loc_9D20:
    lsr zp_mul_result_hi
    ror zp_mul_result_lo
    ror zp_mul_plier
    bcc loc_9D35
    clc
    lda zp_mul_cand_lo
    adc zp_mul_result_lo
    sta zp_mul_result_lo
    lda zp_mul_cand_hi
    adc zp_mul_result_hi
    sta zp_mul_result_hi
loc_9D35:
    lsr zp_mul_result_hi
    ror zp_mul_result_lo
    ror zp_mul_plier
    bcc loc_9D4A
    clc
    lda zp_mul_cand_lo
    adc zp_mul_result_lo
    sta zp_mul_result_lo
    lda zp_mul_cand_hi
    adc zp_mul_result_hi
    sta zp_mul_result_hi
loc_9D4A:
    lsr zp_mul_result_hi
    ror zp_mul_result_lo
    ror zp_mul_plier
    bcc loc_9D5F
    clc
    lda zp_mul_cand_lo
    adc zp_mul_result_lo
    sta zp_mul_result_lo
    lda zp_mul_cand_hi
    adc zp_mul_result_hi
    sta zp_mul_result_hi
loc_9D5F:
    lsr zp_mul_result_hi
    ror zp_mul_result_lo
    ror zp_mul_plier
    bcc loc_9D74
    clc
    lda zp_mul_cand_lo
    adc zp_mul_result_lo
    sta zp_mul_result_lo
    lda zp_mul_cand_hi
    adc zp_mul_result_hi
    sta zp_mul_result_hi
loc_9D74:
    lsr zp_mul_result_hi
    ror zp_mul_result_lo
    ror zp_mul_plier
    bcc loc_9D89
    clc
    lda zp_mul_cand_lo
    adc zp_mul_result_lo
    sta zp_mul_result_lo
    lda zp_mul_cand_hi
    adc zp_mul_result_hi
    sta zp_mul_result_hi
loc_9D89:
    lsr zp_mul_result_hi
    ror zp_mul_result_lo
    bit zp_mul_sign
    bpl loc_9D9E
    sec
    lda #$00
    sbc zp_mul_result_lo
    sta zp_mul_result_lo
    lda #$00
    sbc zp_mul_result_hi
    sta zp_mul_result_hi
loc_9D9E:
    rts
.endp

loc_9D9F:
    asl zp_div_remain_lo
    rol zp_div_remain_hi
    asl zp_div_divisor_lo
    rol zp_div_divisor_hi
.proc div16
    ; 16-BIT DIVIDE — Divides 16-bit dividend by 16-bit divisor, returns quotient and remainder
    bit zp_div_divisor_hi
    bvc loc_9D9F
    asl zp_div_remain_lo
    rol zp_div_remain_hi
    bmi loc_9DB5
    bit zp_div_remain_hi
    bvc loc_9DC5
loc_9DB5:
    sec
    lda zp_div_remain_lo
    sbc zp_div_divisor_lo
    tay
    lda zp_div_remain_hi
    sbc zp_div_divisor_hi
    bcc loc_9DC5
    sty zp_div_remain_lo
    sta zp_div_remain_hi
loc_9DC5:
    rol zp_div_quotient
    asl zp_div_remain_lo
    rol zp_div_remain_hi
    bmi loc_9DD1
    bit zp_div_remain_hi
    bvc loc_9DE1
loc_9DD1:
    sec
    lda zp_div_remain_lo
    sbc zp_div_divisor_lo
    tay
    lda zp_div_remain_hi
    sbc zp_div_divisor_hi
    bcc loc_9DE1
    sty zp_div_remain_lo
    sta zp_div_remain_hi
loc_9DE1:
    rol zp_div_quotient
    asl zp_div_remain_lo
    rol zp_div_remain_hi
    bmi loc_9DED
    bit zp_div_remain_hi
    bvc loc_9DFD
loc_9DED:
    sec
    lda zp_div_remain_lo
    sbc zp_div_divisor_lo
    tay
    lda zp_div_remain_hi
    sbc zp_div_divisor_hi
    bcc loc_9DFD
    sty zp_div_remain_lo
    sta zp_div_remain_hi
loc_9DFD:
    rol zp_div_quotient
    asl zp_div_remain_lo
    rol zp_div_remain_hi
    bmi loc_9E09
    bit zp_div_remain_hi
    bvc loc_9E19
loc_9E09:
    sec
    lda zp_div_remain_lo
    sbc zp_div_divisor_lo
    tay
    lda zp_div_remain_hi
    sbc zp_div_divisor_hi
    bcc loc_9E19
    sty zp_div_remain_lo
    sta zp_div_remain_hi
loc_9E19:
    rol zp_div_quotient
    asl zp_div_remain_lo
    rol zp_div_remain_hi
    bmi loc_9E25
    bit zp_div_remain_hi
    bvc loc_9E35
loc_9E25:
    sec
    lda zp_div_remain_lo
    sbc zp_div_divisor_lo
    tay
    lda zp_div_remain_hi
    sbc zp_div_divisor_hi
    bcc loc_9E35
    sty zp_div_remain_lo
    sta zp_div_remain_hi
loc_9E35:
    rol zp_div_quotient
    asl zp_div_remain_lo
    rol zp_div_remain_hi
    bmi loc_9E41
    bit zp_div_remain_hi
    bvc loc_9E51
loc_9E41:
    sec
    lda zp_div_remain_lo
    sbc zp_div_divisor_lo
    tay
    lda zp_div_remain_hi
    sbc zp_div_divisor_hi
    bcc loc_9E51
    sty zp_div_remain_lo
    sta zp_div_remain_hi
loc_9E51:
    rol zp_div_quotient
    asl zp_div_remain_lo
    rol zp_div_remain_hi
    bmi loc_9E5D
    bit zp_div_remain_hi
    bvc loc_9E6D
loc_9E5D:
    sec
    lda zp_div_remain_lo
    sbc zp_div_divisor_lo
    tay
    lda zp_div_remain_hi
    sbc zp_div_divisor_hi
    bcc loc_9E6D
    sty zp_div_remain_lo
    sta zp_div_remain_hi
loc_9E6D:
    rol zp_div_quotient
    asl zp_div_remain_lo
    rol zp_div_remain_hi
    bmi loc_9E79
    bit zp_div_remain_hi
    bvc loc_9E89
loc_9E79:
    sec
    lda zp_div_remain_lo
    sbc zp_div_divisor_lo
    tay
    lda zp_div_remain_hi
    sbc zp_div_divisor_hi
    bcc loc_9E89
    sty zp_div_remain_lo
    sta zp_div_remain_hi
loc_9E89:
    rol zp_div_quotient
    rts
.endp

transform_vertex:
    ; VERTEX TRANSFORM — Transforms 3D vertex coordinates through camera matrix: rotation + translation, outputs screen-space XY
    jsr load_camera
    jsr compute_screen_offset
    lda zp_dir_quadrant
    bmi loc_9EBD
    rol
    bmi loc_9EB7
    lda vtx_cam_x_lo
    sta $229E
    lda vtx_cam_x_hi
    sta $22CC
    lda vtx_cam_z_lo
    sta $22FA
    lda vtx_cam_z_hi
    sta $2328
    ldx #$FD
    ldy #$B6
    bne loc_9ECA
loc_9EB7:
    ldx #$2A
    ldy #$B7
    bne loc_9ECA
loc_9EBD:
    rol
    bmi loc_9EC6
    ldx #$D0
    ldy #$B6
    bne loc_9ECA
loc_9EC6:
    ldx #$A3
    ldy #$B6
loc_9ECA:
    stx zp_ptr_lo
    sty zp_ptr_hi
    lda zp_grid_row
    rol
    rol
    rol
    rol
    and #$F0
    sta zp_grid_row_accum
    lda zp_grid_x
    and #$0F
    tax
    ora zp_temp
    sta zp_terrain_idx
    ldy #$00
loc_9EE3:
    lda (zp_ptr_lo),y
    sta zp_temp
    bpl loc_9F15
    lda zp_grid_row_accum
    clc
    adc #$F0
    sta zp_grid_row_accum
    clc
    lda vtx_cam_x_lo,y
    adc zp_disp_x_lo
    sta $229E,y
    lda vtx_cam_x_hi,y
    adc zp_disp_x_hi
    sta $22CC,y
    sec
    lda vtx_cam_z_lo,y
    sbc zp_disp_z_lo
    sta $22FA,y
    lda vtx_cam_z_hi,y
    sbc zp_disp_z_hi
    sta $2328,y
    jmp loc_9F99
loc_9F15:
    rol zp_temp
    bpl loc_9F45
    lda zp_grid_row_accum
    clc
    adc #$10
    sta zp_grid_row_accum
    sec
    lda vtx_cam_x_lo,y
    sbc zp_disp_x_lo
    sta $229E,y
    lda vtx_cam_x_hi,y
    sbc zp_disp_x_hi
    sta $22CC,y
    clc
    lda vtx_cam_z_lo,y
    adc zp_disp_z_lo
    sta $22FA,y
    lda vtx_cam_z_hi,y
    adc zp_disp_z_hi
    sta $2328,y
    jmp loc_9F99
loc_9F45:
    rol zp_temp
    bpl loc_9F6F
    dex
    sec
    lda vtx_cam_x_lo,y
    sbc zp_disp_z_lo
    sta $229E,y
    lda vtx_cam_x_hi,y
    sbc zp_disp_z_hi
    sta $22CC,y
    sec
    lda vtx_cam_z_lo,y
    sbc zp_disp_x_lo
    sta $22FA,y
    lda vtx_cam_z_hi,y
    sbc zp_disp_x_hi
    sta $2328,y
    jmp loc_9FEA
loc_9F6F:
    rol zp_temp
    bpl loc_9FEA
    inx
    clc
    lda vtx_cam_x_lo,y
    adc zp_disp_z_lo
    sta $229E,y
    lda vtx_cam_x_hi,y
    adc zp_disp_z_hi
    sta $22CC,y
    clc
    lda vtx_cam_z_lo,y
    adc zp_disp_x_lo
    sta $22FA,y
    lda vtx_cam_z_hi,y
    adc zp_disp_x_hi
    sta $2328,y
    jmp loc_9FEA
loc_9F99:
    rol zp_temp
    bpl loc_9FC3
    dex
    sec
    lda $229E,y
    sbc zp_disp_z_lo
    sta $229E,y
    lda $22CC,y
    sbc zp_disp_z_hi
    sta $22CC,y
    sec
    lda $22FA,y
    sbc zp_disp_x_lo
    sta $22FA,y
    lda $2328,y
    sbc zp_disp_x_hi
    sta $2328,y
    jmp loc_9FEA
loc_9FC3:
    rol zp_temp
    bpl loc_9FEA
    inx
    clc
    lda $229E,y
    adc zp_disp_z_lo
    sta $229E,y
    lda $22CC,y
    adc zp_disp_z_hi
    sta $22CC,y
    clc
    lda $22FA,y
    adc zp_disp_x_lo
    sta $22FA,y
    lda $2328,y
    adc zp_disp_x_hi
    sta $2328,y
loc_9FEA:
    sec
    lda #$00
    sbc zp_cam_alt_lo
    sta zp_temp
    txa
    and #$0F
    ora zp_grid_row_accum
    tax
    lda dat_0900,x
    sta vtx_raw_height,y
    sbc zp_cam_alt_hi
    bcc loc_A010
    lsr
    ror zp_temp
    lsr
    ror zp_temp
    lsr
    ror zp_temp
    lsr
    ror zp_temp
    jmp loc_A01E
loc_A010:
    lsr
    ror zp_temp
    lsr
    ror zp_temp
    lsr
    ror zp_temp
    lsr
    ror zp_temp
    eor #$F0
loc_A01E:
    sta vtx_height_hi,y
    lda zp_temp
    sta vtx_height_lo,y
    txa
    sta vtx_grid_idx,y
    lda $2328,y
    bmi loc_A073
    bne loc_A038
    lda $22FA,y
    cmp #$06
    bcc loc_A073
loc_A038:
    lda $22CC,y
    bpl loc_A05C
    sec
    lda #$00
    sbc $229E,y
    sta zp_temp
    lda #$00
    sbc $22CC,y
    cmp $2328,y
    bcc loc_A06F
    bne loc_A058
    lda zp_temp
    cmp $22FA,y
    bcc loc_A06F
loc_A058:
    lda #$40
    bne loc_A075
loc_A05C:
    cmp $2328,y
    bcc loc_A06F
    bne loc_A06B
    lda $229E,y
    cmp $22FA,y
    bcc loc_A06F
loc_A06B:
    lda #$20
    bne loc_A075
loc_A06F:
    lda #$00
    beq loc_A075
loc_A073:
    lda #$80
loc_A075:
    sta vtx_vis_flags,y
    iny
    cpy #$2D
    beq loc_A080
    jmp loc_9EE3
loc_A080:
    ldy #$00
loc_A082:
    ldx edge_adjacency,y
    stx current_vtx_idx
    iny
    lda vtx_vis_flags,x
    bmi loc_A0B1
    bne loc_A0D2
    ldx edge_adjacency,y
    iny
    lda vtx_vis_flags,x
    bpl loc_A0D3
    sty zp_temp
    ldy current_vtx_idx
    lda #$00
    sta $2328,x
    lda #$06
    sta $22FA,x
    lda #$00
    sta vtx_vis_flags,x
    ldy zp_temp
    bne loc_A0D3
loc_A0B1:
    ldx edge_adjacency,y
    iny
    lda vtx_vis_flags,x
    bne loc_A0D3
    sty zp_temp
    ldy current_vtx_idx
    lda #$00
    sta $2328,y
    lda #$06
    sta $22FA,y
    lda #$00
    sta vtx_vis_flags,y
    ldy zp_temp
    bne loc_A0D3
loc_A0D2:
    iny
loc_A0D3:
    cpy #$0C
    bcs loc_A0DA
    jmp loc_A082
loc_A0DA:
    rts
compute_screen_offset:
    ; COMPUTE OFFSETS — Calculates screen buffer offsets for terrain column rendering based on viewer position and heading
    .byte $A5                     ; $A0DB
loc_A0DC:
    .byte $A0                     ; $A0DC
    sta zp_mul_cand_lo
    lda zp_disp_x_hi
    sta zp_mul_cand_hi
    lda $89
    jsr mul16_signed
    lda zp_mul_result_lo
    sta vtx_cam_x_lo
    lda zp_mul_result_hi
    sta vtx_cam_x_hi
    lda zp_disp_z_lo
    sta zp_mul_cand_lo
    lda zp_disp_z_hi
    sta zp_mul_cand_hi
    lda $87
    jsr mul16_signed
    sec
    lda vtx_cam_x_lo
    sbc zp_mul_result_lo
    sta vtx_cam_x_lo
    lda vtx_cam_x_hi
    sbc zp_mul_result_hi
    sta vtx_cam_x_hi
    lda zp_disp_z_lo
    sta zp_mul_cand_lo
    lda zp_disp_z_hi
    sta zp_mul_cand_hi
    lda $89
    jsr mul16_signed
    lda zp_mul_result_lo
    sta vtx_cam_z_lo
    lda zp_mul_result_hi
    sta vtx_cam_z_hi
    lda zp_disp_x_lo
    sta zp_mul_cand_lo
    lda zp_disp_x_hi
    sta zp_mul_cand_hi
    lda $87
    jsr mul16_signed
    clc
    lda zp_mul_result_lo
    adc vtx_cam_z_lo
    sta vtx_cam_z_lo
    lda zp_mul_result_hi
    adc vtx_cam_z_hi
    sta vtx_cam_z_hi
    sec
    lda #$00
    sbc vtx_cam_z_lo
    sta vtx_cam_z_lo
    lda #$00
    sbc vtx_cam_z_hi
    sta vtx_cam_z_hi
    rts
project_x:
    ; PROJECT X — Projects 3D X coordinate to screen column, applies perspective division
    lda vtx_vis_flags,x
    ora #$10
    sta vtx_vis_flags,x
    ldy #$00
    lda $22FA,x
    sta zp_div_divisor_lo
    lda $2328,x
    sta zp_div_divisor_hi
    lda $22CC,x
    bmi loc_A17A
    sta zp_div_remain_hi
    lda $229E,x
    sta zp_div_remain_lo
    jmp loc_A189
loc_A17A:
    sec
    lda #$00
    sbc $229E,x
    sta zp_div_remain_lo
    lda #$00
    sbc $22CC,x
    sta zp_div_remain_hi
loc_A189:
    lda zp_div_remain_hi
    cmp zp_div_divisor_hi
    bcc loc_A1BF
    bne loc_A197
    lda zp_div_remain_lo
    cmp zp_div_divisor_lo
    bcc loc_A1BF
loc_A197:
    asl zp_div_divisor_lo
    rol zp_div_divisor_hi
    iny
    cpy #$08
    bne loc_A189
    lda $22CC,x
    bmi loc_A1B2
    lda #$40
    sta vtx_screen_x_hi,x
    lda #$00
    sta vtx_screen_x_lo,x
    jmp loc_A23A
loc_A1B2:
    lda #$C0
    sta vtx_screen_x_hi,x
    lda #$00
    sta vtx_screen_x_lo,x
    jmp loc_A23A
loc_A1BF:
    sty zp_norm_shift
    jsr div16
    ldy zp_norm_shift
    dey
    bmi loc_A211
    lda #$00
loc_A1CB:
    asl zp_div_quotient
    rol
    dey
    bpl loc_A1CB
    sta zp_temp
    lsr
    tay
    lda zp_div_quotient
    ror
    clc
    adc zp_div_quotient
    sta zp_div_quotient
    tya
    adc zp_temp
    lsr
    ror zp_div_quotient
    lsr
    ror zp_div_quotient
    sta zp_temp
    lda $22CC,x
    bmi loc_A1FF
    lda #$80
    clc
    adc zp_div_quotient
    sta vtx_screen_x_lo,x
    lda zp_temp
    adc #$00
    sta vtx_screen_x_hi,x
    jmp loc_A23A
loc_A1FF:
    lda #$80
    sec
    sbc zp_div_quotient
    sta vtx_screen_x_lo,x
    lda #$00
    sbc zp_temp
    sta vtx_screen_x_hi,x
    jmp loc_A23A
loc_A211:
    lda #$00
    sta vtx_screen_x_hi,x
    lda $22CC,x
    bmi loc_A22A
    lda zp_div_quotient
    lsr
    sec
    adc zp_div_quotient
    ror
    lsr
    ora #$80
    sta vtx_screen_x_lo,x
    bne loc_A23A
loc_A22A:
    lda zp_div_quotient
    lsr
    sec
    adc zp_div_quotient
    ror
    lsr
    eor #$FF
    sec
    adc #$80
    sta vtx_screen_x_lo,x
loc_A23A:
    ldy #$00
    lda $22FA,x
    sta zp_div_divisor_lo
    lda $2328,x
    sta zp_div_divisor_hi
    lda vtx_height_hi,x
    bmi loc_A255
    sta zp_div_remain_hi
    lda vtx_height_lo,x
    sta zp_div_remain_lo
    jmp loc_A264
loc_A255:
    sec
    lda #$00
    sbc vtx_height_lo,x
    sta zp_div_remain_lo
    lda #$00
    sbc vtx_height_hi,x
    sta zp_div_remain_hi
loc_A264:
    lda zp_div_remain_hi
    cmp zp_div_divisor_hi
    bcc loc_A29A
    bne loc_A272
    lda zp_div_remain_lo
    cmp zp_div_divisor_lo
    bcc loc_A29A
loc_A272:
    asl zp_div_divisor_lo
    rol zp_div_divisor_hi
    iny
    cpy #$08
    bne loc_A264
    lda vtx_height_hi,x
    bmi loc_A28D
    lda #$40
    sta vtx_screen_y_hi,x
    lda #$00
    sta vtx_screen_y_lo,x
    jmp loc_A315
loc_A28D:
    lda #$C0
    sta vtx_screen_y_hi,x
    lda #$00
    sta vtx_screen_y_lo,x
    jmp loc_A315
loc_A29A:
    sty zp_norm_shift
    jsr div16
    ldy zp_norm_shift
    dey
    bmi loc_A2EC
    lda #$00
loc_A2A6:
    asl zp_div_quotient
    rol
    dey
    bpl loc_A2A6
    sta zp_temp
    lsr
    tay
    lda zp_div_quotient
    ror
    clc
    adc zp_div_quotient
    sta zp_div_quotient
    tya
    adc zp_temp
    lsr
    ror zp_div_quotient
    lsr
    ror zp_div_quotient
    sta zp_temp
    lda vtx_height_hi,x
    bmi loc_A2DA
    lda #$80
    clc
    adc zp_div_quotient
    sta vtx_screen_y_lo,x
    lda zp_temp
    adc #$00
    sta vtx_screen_y_hi,x
    jmp loc_A315
loc_A2DA:
    lda #$80
    sec
    sbc zp_div_quotient
    sta vtx_screen_y_lo,x
    lda #$00
    sbc zp_temp
    sta vtx_screen_y_hi,x
    jmp loc_A315
loc_A2EC:
    lda #$00
    sta vtx_screen_y_hi,x
    lda vtx_height_hi,x
    bmi loc_A305
    lda zp_div_quotient
    lsr
    sec
    adc zp_div_quotient
    ror
    lsr
    ora #$80
    sta vtx_screen_y_lo,x
    bne loc_A315
loc_A305:
    lda zp_div_quotient
    lsr
    sec
    adc zp_div_quotient
    ror
    lsr
    eor #$FF
    sec
    adc #$80
    sta vtx_screen_y_lo,x
loc_A315:
    lda vtx_screen_x_hi,x
    bmi loc_A348
    bne loc_A34F
    lda vtx_screen_x_lo,x
    lsr
    lsr
    lsr
    tay
    lda horizon_profile,y
    bmi loc_A338
loc_A328:
    clc
    adc vtx_screen_y_lo,x
    sta vtx_screen_y_lo,x
    lda vtx_screen_y_hi,x
    adc #$00
    sta vtx_screen_y_hi,x
    rts
loc_A338:
    clc
    adc vtx_screen_y_lo,x
    sta vtx_screen_y_lo,x
    lda vtx_screen_y_hi,x
    adc #$FF
    sta vtx_screen_y_hi,x
    rts
loc_A348:
    lda horizon_profile
    bmi loc_A338
    bpl loc_A328
loc_A34F:
    lda $2727
    bmi loc_A338
    bpl loc_A328
terrain_render:
    ; RENDER TERRAIN — Main terrain rendering entry: iterates visible terrain columns, transforms heights to screen coordinates, calls fill routines
    stx zp_saved_x
    ldy #$20
    clc
loc_A35B:
    txa
    sta pix_byte_offset,y
    sta $BD01,y
    sta $BD02,y
    sta $BD03,y
    inx
    txa
    sta $BD04,y
    sta $BD05,y
    sta $BD06,y
    sta $BD07,y
    inx
    tya
    adc #$08
    tay
    cpy #$D4
    bcc loc_A35B
    txa
    clc
    adc #$2E
    sta $B4
    ldy #$14
    lda #$67
loc_A389:
    dey
    sta zbuf_sentinel_a,y
    sta $26C8,y
    bne loc_A389
    ldy #$21
    lda #$6B
loc_A396:
    dey
    sta zbuf_sentinel_b,y
    sta $2669,y
    sta $268A,y
    sta $26AB,y
    bne loc_A396
    lda $288D
    sta $292C
    lda $288E
    sta $292D
    jsr render_copy_AE0A
    lda #$80
    sta $290C
    sta $290D
    sta $2937
    sta $2938
    sta $2939
    sta $293A
    ldy zp_saved_x
    bne loc_A3D2
    sta $290F
    sta terrain_near_dist
loc_A3D2:
    lda #$00
    sta $2912
    sta $2920
    sta $2921
    lda $2823
    bmi loc_A3E5
    inc $2921
loc_A3E5:
    ldy #$00
loc_A3E7:
    lda edge_adjacency,y
    iny
    tax
    stx current_vtx_idx
    lda vtx_vis_flags,x
    bmi loc_A467
    and #$20
    bne loc_A467
    lda edge_adjacency,y
    iny
    sty edge_iter_y_save
    tax
    lda vtx_vis_flags,x
    bmi loc_A468
    and #$40
    bne loc_A468
    lda vtx_vis_flags,x
    and #$10
    bne loc_A416
    jsr project_x
    jsr detect_pilot
loc_A416:
    lda vtx_screen_x_lo,x
    sta work_proj_x_lo
    lda vtx_screen_x_hi,x
    sta work_proj_x_hi
    lda vtx_screen_y_lo,x
    sta work_proj_y_lo
    lda vtx_screen_y_hi,x
    sta work_proj_y_hi
    lda vtx_raw_height,x
    sta work_height
    ldx current_vtx_idx
    lda vtx_vis_flags,x
    and #$10
    bne loc_A444
    jsr project_x
    jsr detect_pilot
loc_A444:
    lda vtx_screen_x_lo,x
    sta zp_column_x
    lda vtx_screen_x_hi,x
    sta zp_proj_x_hi
    lda vtx_screen_y_lo,x
    sta zp_depth_accum
    lda vtx_screen_y_hi,x
    sta zp_proj_y_hi
    lda vtx_raw_height,x
    sta zp_color_shade
    ldx #$00
    jsr rasterize_edge
    ldy edge_iter_y_save
    bne loc_A468
loc_A467:
    iny
loc_A468:
    cpy #$90
    beq loc_A46F
    jmp loc_A3E7
loc_A46F:
    lda $290C
    sta $28F4
    lda $290D
    sta $28F5
    lda terrain_near_dist
    bpl loc_A488
    lda #$00
    sta terrain_detail
    jmp loc_A4AA
loc_A488:
    lsr
    lsr
    clc
    adc #$01
    cmp #$0A
    bcc loc_A493
    lda #$09
loc_A493:
    sta terrain_detail
    cmp prev_detail_level
    beq loc_A4AA
    sta prev_detail_level
    bcs loc_A4AA
    lda flight_param
    bne loc_A4AA
    ldx #$14
    jsr queue_push_flagged
loc_A4AA:
    lda $2937
    cmp #$80
    ror
    clc
    adc #$7D
    sta $284B
    lda $2938
    clc
    adc #$15
    sta $2849
    lda $2939
    sta $293B
    lda $2935
    sta $283D
    lda $2936
    sta $283E
    lda game_mode
    beq loc_A4D9
    jsr draw_AD16
loc_A4D9:
    lda $2921
    beq loc_A4E0
    lda #$74
loc_A4E0:
    sta render_state_flag
    lda $27F7
    sta $2922
    lda $27F8
    sta $2923
    lda $27F9
    sta $2924
    lda $27FA
    sta $2925
    lda $27FB
    sta $2926
    lda $27FC
    sta $2927
    lda $280A
    sta $2928
    lda $280B
    sta $2929
    lda $280C
    sta $292A
    lda $280D
    sta $292B
    lda $292C
    sta $292E
    lda $292D
    sta $292F
    ldy #$1F
loc_A52D:
    sec
    lda $2718
    sbc horizon_profile,y
    cmp #$80
    ror
    adc #$00
    sta height_delta_tab,y
    dey
    bpl loc_A52D
    lda $2920
    bne loc_A54B
    sec
    lda terrain_scroll_rate
    jmp loc_A5A5
loc_A54B:
    lda flight_param
    beq loc_A555
    lda #$00
    jmp loc_A5A4
loc_A555:
    lda $288B
    asl
    sta zp_memcpy_len_lo
    lda $288C
    rol
    asl zp_memcpy_len_lo
    rol
    asl zp_memcpy_len_lo
    rol
    sta zp_memcpy_len_lo
    lda $293E
    asl
    sta zp_memcpy_len_hi
    lda $293F
    rol
    asl zp_memcpy_len_hi
    rol
    asl zp_memcpy_len_hi
    rol
    sec
    sbc zp_memcpy_len_lo
    bpl loc_A581
    clc
    eor #$FF
    adc #$01
loc_A581:
    sta zp_memcpy_len_lo
    lda $2888
    cmp #$80
    ror
    cmp #$80
    ror
    sta zp_memcpy_len_hi
    lda $2940
    cmp #$80
    ror
    cmp #$80
    ror
    sec
    sbc zp_memcpy_len_hi
    bpl loc_A5A1
    clc
    eor #$FF
    adc #$01
loc_A5A1:
    clc
    adc zp_memcpy_len_lo
loc_A5A4:
    clc
loc_A5A5:
    adc terrain_scroll_accum
    bpl loc_A5AC
    lda #$7F
loc_A5AC:
    sec
    sbc terrain_scroll_rate
    bcs loc_A5B4
    lda #$00
loc_A5B4:
    sta terrain_scroll_accum
    cmp #$20
    bcc loc_A5C3
    lda #$80
    sta terrain_vis_flag
    jmp loc_A5CA
loc_A5C3:
    lda terrain_vis_flag
    bpl loc_A5CA
    lda #$00
loc_A5CA:
    sta terrain_vis_flag
    lda viewport_state
    beq loc_A5E2
    sec
    lda game_timer
    sbc #$06
    bcc loc_A5E2
    cmp RANDOM
    bcc loc_A5E2
    jsr render_copy_7BF0
loc_A5E2:
    lda $2847
    beq loc_A5FC
    ldx #$00
    stx $2847
    lda #$F9
loc_A5EE:
    cmp pilot_map,x
    bcs loc_A5F9
    inc pilot_map,x
    inc $2847
loc_A5F9:
    inx
    bne loc_A5EE
loc_A5FC:
    ldx $282A
    bpl loc_A664
    lda flight_param
    bne loc_A664
    lda $0615
    beq loc_A664
    dec enabled_flag_0616
    bne loc_A664
    sta enabled_flag_0616
    clc
    lda RANDOM
    bmi loc_A664
    adc #$40
    tax
    lda RANDOM
    and #$1F
    adc #$6E
    cmp zbuf_columns,x
    bcc loc_A664
    beq loc_A664
    sta $282F
    lda #$80
    sbc $282F
    asl
    clc
    adc #$42
    sta $282F
    txa
    sec
    sbc #$10
    sta $282C
    lda #$00
    sta $282D
    sta $2830
    sta $2832
    sta $2834
    jsr render_random_perturb_A665
    lda #$7F
    sta $2823
    sta $282A
    sta $2849
    jsr pmg_fill_478C
    lda #$01
    sta $2842
loc_A664:
    rts
render_random_perturb_A665:
    ; Reads POKEY RANDOM for randomization; Writes to screen memory; Reads: SKRES; 20 instructions
    clc
    lda RANDOM
    adc RANDOM
    sta $2831
    lda #$00
    rol
    ldy $282C
    cpy #$6C
    bcs loc_A67B
    eor #$FF
loc_A67B:
    sta $2832
    sec
    ldy #$00
    lda RANDOM
    sbc #$80
    bcs loc_A689
    dey
loc_A689:
    sty $2834
    sta $2833
loc_A68F:
    rts
detect_pilot:
    ; DETECT PILOT — Scans terrain surface for downed pilot: checks pilot_map buffer at $0A00, compares with viewer position
    ldy vtx_grid_idx,x
    lda pilot_map,y
    beq loc_A68F
    lda vtx_screen_y_hi,x
    bne loc_A68F
    lda vtx_screen_x_hi,x
    bne loc_A68F
    lda $2328,x
    beq loc_A68F
    stx $2904
    lda dat_0900,y
    bpl loc_A6B2
    jmp loc_A8AB
loc_A6B2:
    lda zp_saved_x
    bne loc_A6C1
    lda pilot_map,y
    cmp #$02
    bcc loc_A6C1
    cmp #$F8
    bcc loc_A6C4
loc_A6C1:
    jmp loc_A751
loc_A6C4:
    lda $22CC,x
    sta $2906
    sta $2908
    cmp #$80
    bcc loc_A6DA
    lda #$00
    sbc $2908
    sta $2908
    sec
loc_A6DA:
    ror $2906
    bcc loc_A6E2
    inc $2906
loc_A6E2:
    lda $2328,x
    sta $2907
    cmp $2908
    bcc loc_A751
    adc $2908
    cmp $290F
    bcs loc_A751
    sta $290F
    lsr $2908
    sec
    sbc $2908
    sta terrain_near_dist
    lda $2906
    sta $290C
    lda $2907
    sta $290D
    lda vtx_grid_idx,x
    sta $290B
    lda #$64
    sta $28AB
    lda vtx_screen_x_hi,x
    bne loc_A751
    lda vtx_screen_y_hi,x
    bne loc_A751
    lda vtx_screen_x_hi,x
    bne loc_A72E
    lda vtx_screen_x_lo,x
    jmp loc_A737
loc_A72E:
    bmi loc_A735
    lda #$FF
    jmp loc_A737
loc_A735:
    lda #$00
loc_A737:
    sta $28AA
    lda vtx_screen_y_hi,x
    bne loc_A745
    lda vtx_screen_y_lo,x
    jmp loc_A74E
loc_A745:
    bmi loc_A74C
    lda #$FF
    jmp loc_A74E
loc_A74C:
    lda #$00
loc_A74E:
    sta $28AB
loc_A751:
    jmp loc_A99D
plot_sprite_pixel:
    ; Writes to screen memory; 21 instructions
    tya
    cmp zbuf_columns,x
    bcc loc_A782
    beq loc_A782
draw_A75C:
    ; Writes to screen memory; Table access: $28EC, $291C, $BC00, $BD00; 17 instructions
    cpy #$97
    bcs loc_A782
    sty $2905
    lda row_addr_lo_render,y
    sta zp_ptr_lo
    lda row_addr_hi_render,y
    sta zp_ptr_hi
    ldy pix_byte_offset,x
    lda pix_color_mask,x
    sta zp_temp
    lsr
    ora zp_temp
    and $2903
    ora (zp_ptr_lo),y
    sta (zp_ptr_lo),y
    ldy $2905
loc_A782:
    rts
    .byte $00,$3F,$FC,$00,$03,$FF,$FF,$C0,$0F,$FF,$FF,$F0,$1F,$FF,$FF,$F8 ; $A783
    .byte $3F,$FF,$FF,$FC,$7F,$FF,$FF,$FE,$7F,$FF,$FF,$FE  ; $A793
    :20 .byte $FF                    ; $A79F (20 bytes)
    :10 .byte $00                    ; $A7B3 (10 zero bytes)
    .byte $30,$00,$00,$9B,$78,$00,$00,$5F,$F6,$00,$00,$F7,$BB,$00,$00,$FD ; $A7BD
    .byte $FF,$80,$01,$BF,$ED,$00,$00,$7E,$FE,$00,$00,$1B,$D8 ; $A7CD
    :11 .byte $00                    ; $A7DA (11 zero bytes)
    .byte $20,$00,$13,$8C,$70,$00,$05,$F7,$F9,$08,$01,$ED,$BE,$00,$01,$D3 ; $A7E5
    .byte $6E,$00,$43,$F0,$BF,$80,$06,$E8,$9F,$F0,$07,$AD,$7B,$00,$09,$F6 ; $A7F5
    .byte $BF,$10,$04,$FB,$ED,$00,$00,$58,$F8              ; $A805
    :6 .byte $00                    ; $A80E (6 zero bytes)
    .byte $A1,$30,$00,$21,$04,$2A,$00,$05,$29,$12,$90,$43,$00,$0C,$40,$14 ; $A814
    .byte $40,$56,$80,$0D,$00,$02,$20,$06,$00,$23,$40,$2D,$42,$08,$80,$08 ; $A824
    .byte $90,$44,$40,$05,$0D,$62,$88,$09,$A8,$AE,$10,$30,$00,$70,$40,$00 ; $A834
    .byte $02,$10,$00,$02,$20,$08,$80,$08,$00,$00,$40,$45,$00,$02,$18,$90 ; $A844
    .byte $00,$00,$40,$02,$00,$00,$04,$C5,$00,$01,$07,$00,$00,$00,$68,$80 ; $A854
    .byte $00,$00,$08,$08,$00,$04,$00,$33,$00,$08,$30,$C0,$80,$02,$13,$B3 ; $A864
    .byte $E3,$13,$43,$A7,$A7,$A8,$A8,$00,$00,$FF,$F0,$00,$0F,$FF,$F0,$00 ; $A874
    .byte $FF,$FF,$F0,$0F,$FF,$FF,$F0,$FF,$FF,$FF,$F0,$FF,$FF,$FF,$F0,$00 ; $A884
    .byte $00,$0F,$C0,$00,$03,$FF,$F0,$08,$03,$FF,$F0,$00,$80,$10,$00,$40 ; $A894
    .byte $00,$00,$02,$00,$04,$00,$00                      ; $A8A4
loc_A8AB:
    lda #$83
    sta $28FB
    lda #$A7
    sta $28FC
    lda #$FF
    sta $2903
    lda $2328,x
    sta $28FA
    lda $22FA,x
    sta $28F9
    lda vtx_screen_y_lo,x
    sta $28F8
    tay
    lda vtx_screen_x_lo,x
    sta $28F7
    lda vtx_grid_idx,x
    tax
    lda pilot_map,x
    cmp #$FA
    bcc loc_A8E7
    jsr draw_AC2B
    jmp loc_A930
dat_A8E4:
    .byte $4C,$2C,$A9                                      ; $A8E4
loc_A8E7:
    tya
    ldx $28F7
    cmp zbuf_columns,x
    bcc loc_A92C
    lda #$80
    jsr draw_A93F
    dex
    jsr plot_sprite_pixel
    inx
    lda cam_altitude
    cmp #$37
    bcs loc_A930
    lda #$01
    sta $2921
    sta $2920
    lda $2823
    bpl loc_A930
    lda difficulty_level
    bne loc_A930
    lda RANDOM
    bpl loc_A930
    lda $2912
    bne loc_A930
    stx $2910
    sty $2911
    lda $28FA
    sta $2912
    jmp loc_A930
loc_A92C:
    ldx $2904
    rts
loc_A930:
    lda $28FA
    cmp #$0D
    bcs loc_A92C
    lda #$00
    sta $2902
    jmp render_AC4F
.proc draw_A93F
    ; Writes to screen memory; 39 instructions
    sta $2933
    lda $28F7
    sec
    sbc #$80
    sta $2930
    bpl loc_A953
    sec
    lda #$00
    sbc $2930
loc_A953:
    sta $2932
    sec
    lda #$80
    sbc $28F8
    sta $2931
    bpl loc_A967
    sec
    lda #$00
    sbc $2931
loc_A967:
    clc
    adc $2932
    bcs loc_A99C
    sta $2932
    clc
    adc $28FA
    bcs loc_A99C
    cmp $293A
    bcs loc_A99C
    sta $293A
    lda $2932
    sta $2939
    lda $2930
    sta $2937
    lda $2931
    sta $2938
    lda $2933
    sta $2935
    lda $2934
    sta $2936
loc_A99C:
    rts
.endp

loc_A99D:
    lda #$7B
    sta $28FB
    lda #$A8
    sta $28FC
    lda vtx_raw_height,x
    and #$01
    sta $2934
    sta $2902
    lda vtx_screen_x_lo,x
    sta $28F7
    lda vtx_screen_y_lo,x
    sta $28F8
    lda $2328,x
    sta $28FA
    lda $22FA,x
    sta $28F9
    ldy vtx_grid_idx,x
    lda pilot_map,y
    cmp #$FA
    bcc loc_A9D7
    jsr draw_AC2B
loc_A9D7:
    ldy $28F7
    lda $28F8
    cmp zbuf_columns,y
    bcc loc_A9F6
    lda #$00
    jsr draw_A93F
    lda #$AA
    sta $2903
    lda $28FA
    cmp #$0D
    bcs loc_A9F6
    jsr render_AC4F
loc_A9F6:
    lda zp_saved_x
    bne loc_AA2B
    lda difficulty_level
    bne loc_AA2B
    ldy vtx_grid_idx,x
    lda pilot_map,y
    cmp #$02
    bcc loc_AA2B
    cmp #$F8
    bcs loc_AA2B
    lda #$FF
    sta $2903
    ldy vtx_screen_y_lo,x
    lda vtx_screen_x_lo,x
    tax
    jsr plot_sprite_pixel
    dex
    dey
    jsr plot_sprite_pixel
    dex
    iny
    jsr plot_sprite_pixel
    inx
    iny
    jsr plot_sprite_pixel
loc_AA2B:
    ldx $2904
    rts
render_random_perturb_AA2F:
    ; Reads POKEY RANDOM for randomization; Writes to screen memory; Bit extraction/packing via shifts and rotates; Reads: SKRES; Calls: draw_8E6B, render_search_AB79, render_search_AB7E, queue_push_flagged; Table access: $071B; 160 instructions, 2 loops
    dec $2913
    bpl loc_AA58
    lda $0618
    and RANDOM
    sta $2913
    lda $2829
    bne loc_AA45
    jsr draw_8E6B
loc_AA45:
    lda flight_param
    bne loc_AA4F
    lda #$B4
    sta $3355
loc_AA4F:
    lda #$00
    sta viewport_state
    sta $2912
loc_AA57:
    rts
loc_AA58:
    bne loc_AA57
    lda $2912
    beq loc_AA57
    lda $2910
    sta $2915
    lda $2911
    sta $2917
    lda #$01
    sta $2919
    lda #$80
    sta $2914
    sta $2916
    sta $2918
    lda #$FF
    sta $2903
    lda #$00
    sta $291B
    sta $291D
    lda RANDOM
    asl
    sta $291A
    bcc loc_AA94
    dec $291B
loc_AA94:
    lda $293F
    bpl loc_AAA5
    cmp #$FF
    bcs loc_AAA2
    lda #$00
    sta $291B
loc_AAA2:
    jmp loc_AAAE
loc_AAA5:
    cmp #$02
    bcc loc_AAAE
    lda #$FF
    sta $291B
loc_AAAE:
    lda RANDOM
    asl
    sta row_addr_hi_render
    dec $291D
    lda terrain_vis_flag
    cmp #$07
    beq loc_AAEA
    lda terrain_scroll_accum
    eor #$FF
    lsr
    lsr
    lsr
    clc
    adc #$0C
    sta $291E
    jsr render_search_AB79
    lda #$BE
    sta $DC
    lda viewport_state
    bne loc_AADD
    lda #$28
    sta $DE
loc_AADD:
    inc viewport_state
    ldx #$07
    jsr queue_push_flagged
    ldx #$02
    jmp queue_push_flagged
loc_AAEA:
    lda #$00
    sta $291B
    sec
    lda #$67
    sbc $2910
    asl
    sta $291A
    bcc loc_AAFE
    dec $291B
loc_AAFE:
    sec
    lda #$6B
    sbc $2911
    sta $291E
    jsr render_search_AB7E
    lda #$10
    sta $287F
    inc viewport_state
    lda #$BE
    sta $DC
    lda #$BC
    sta $DE
    lda #$B6
    sta $DD
    lda #$B8
    sta $DB
    lda #$34
    sta $3355
    ldx #$03
    jsr queue_push_flagged
    inx
    jsr queue_push_flagged
    ldx #$0A
    jsr queue_push_flagged
    inx
    jsr queue_push_flagged
render_random_perturb_AB39:
    ; Reads POKEY RANDOM for randomization; Writes to screen memory; Reads: SKRES; 32 instructions
    ldy $288E
    cpy #$F4
    beq loc_AB46
    dey
    cpy #$F4
    beq loc_AB46
    dey
loc_AB46:
    sty $288E
    ldy $288C
    lda RANDOM
    bpl loc_AB5E
    cpy #$FB
    beq loc_AB5B
    dey
    cpy #$FB
    beq loc_AB5B
    dey
loc_AB5B:
    jmp loc_AB68
loc_AB5E:
    cpy #$05
    beq loc_AB68
    iny
    cpy #$05
    beq loc_AB68
    iny
loc_AB68:
    sty $288C
    sec
    lda $2898
    sbc #$08
    bcs loc_AB75
    lda #$00
loc_AB75:
    sta $2898
    rts
render_search_AB79:
    ; Search loop — scans array for match; Writes to screen memory; Reads: SKRES; Calls: draw_A75C, draw_A75C; 76 instructions, 6 loops
    lda RANDOM
    bmi loc_ABD6
render_search_AB7E:
    ; Search loop — scans array for match; Writes to screen memory; Calls: draw_A75C; 38 instructions, 2 loops
    sec
    lda $291A
    sbc #$40
    sta $291A
    bcs loc_AB8C
    dec $291B
loc_AB8C:
    ldx $2915
    ldy $2917
    cpy #$6C
    bcc loc_ABD5
loc_AB96:
    lda $2919
    sta $291F
loc_AB9C:
    cpx #$2C
    bcc loc_ABA7
    cpx #$D4
    bcs loc_ABA7
    jsr draw_A75C
loc_ABA7:
    inx
    dec $291F
    bne loc_AB9C
    clc
    lda $2918
    adc $291E
    sta $2918
    bcc loc_ABBD
    inc $2919
    clc
loc_ABBD:
    lda $2914
    adc $291A
    sta $2914
    lda $2915
    adc $291B
    sta $2915
    tax
    dey
    cpy #$6C
    bcs loc_AB96
loc_ABD5:
    rts
loc_ABD6:
    ldx $2915
    ldy $2917
loc_ABDC:
    cpx #$2C
    bcc loc_ABD5
    cpx #$D4
    bcs loc_ABD5
    cpy #$6C
    bcc loc_ABD5
    lda $2919
    sta $291F
loc_ABEE:
    jsr draw_A75C
    dey
    cpy #$6C
    bcc loc_ABFB
    dec $291F
    bne loc_ABEE
loc_ABFB:
    clc
    lda $2918
    adc $291E
    sta $2918
    bcc loc_AC0B
    inc $2919
    clc
loc_AC0B:
    lda $2916
    adc row_addr_hi_render
    sta $2916
    lda $2917
    adc $291D
    sta $2917
    tay
    lda $291B
    bmi loc_AC27
    inx
    jmp loc_ABDC
loc_AC27:
    dex
    jmp loc_ABDC
.proc draw_AC2B
    ; Writes to screen memory; Table access: $A873, $A877; 15 instructions
    and #$03
    tax
    lda $A873,x
    sta $28FB
    lda $A877,x
    sta $28FC
    txa
    bne loc_AC42
    lda #$FF
    sta $2903
loc_AC42:
    lsr $28FA
    ror $28F9
    lsr $28FA
    ror $28F9
    rts
.endp

render_AC4F:
    ; Writes to screen memory; Bit extraction/packing via shifts and rotates; Calls: plot_sprite_pixel; Table access: $AD0E; 83 instructions, 2 loops
    lda $28FA
    bne loc_AC5C
    lda #$00
    sta $28F9
    inc $28FA
loc_AC5C:
    lda #$10
    sta zp_memcpy_len_hi
    lda #$00
    sta zp_memcpy_len_lo
    sec
loc_AC65:
    lda zp_memcpy_len_lo
    sbc $28F9
    sta zp_memcpy_len_lo
    lda zp_memcpy_len_hi
    sbc $28FA
    sta zp_memcpy_len_hi
    dec $28F7
    bcs loc_AC65
    lda $28F7
    sta $2901
    lda #$00
    sta $28FF
    sta $2900
loc_AC86:
    lda #$00
    sta $28FD
    sta $28FE
    sta zp_memcpy_len_hi
    lda $2900
    asl
    rol zp_memcpy_len_hi
    asl
    rol zp_memcpy_len_hi
    clc
    adc $28FB
    sta zp_memcpy_len_lo
    lda zp_memcpy_len_hi
    adc $28FC
    sta zp_memcpy_len_hi
    lda $2901
    sta $28F7
loc_ACAC:
    lda $28FE
    ldx $2902
    beq loc_ACBA
    sec
    lda #$1F
    sbc $28FE
loc_ACBA:
    tax
    lsr
    lsr
    lsr
    tay
    txa
    and #$07
    tax
    lda (zp_memcpy_len_lo),y
    and $AD0E,x
    beq loc_ACD3
    ldx $28F7
    ldy $28F8
    jsr plot_sprite_pixel
loc_ACD3:
    inc $28F7
    clc
    lda $28FD
    adc $28F9
    sta $28FD
    lda $28FE
    adc $28FA
    sta $28FE
    cmp #$20
    bcc loc_ACAC
    dec $28F8
    clc
    lda $28FF
    adc $28F9
    sta $28FF
    lda $2900
    adc $28FA
    sta $2900
    cmp #$0C
    bcs loc_AD0A
    jmp loc_AC86
loc_AD0A:
    ldx $2904
    rts
    .byte $80,$40,$20,$10,$08,$04,$02,$01                  ; $AD0E
draw_AD16:
    ; Writes to screen memory; 37 instructions
    lda $284E
    bne loc_AD6B
    lda $0626
    beq loc_AD6B
    lda $282A
    bpl loc_AD41
    lda $2937
    cmp #$0C
    bcc loc_AD30
    cmp #$F5
    bcc loc_AD66
loc_AD30:
    lda $2938
    cmp #$0C
    bcc loc_AD3B
    cmp #$F5
    bcc loc_AD66
loc_AD3B:
    ldx $2935
    jmp loc_AD59
loc_AD41:
    lda $282C
    cmp #$48
    bcc loc_AD66
    cmp #$98
    bcs loc_AD66
    lda $282F
    cmp #$24
    bcc loc_AD66
    cmp #$60
    bcs loc_AD66
    ldx #$01
loc_AD59:
    inc $283B
    lda $283B
    cmp #$02
    bcc loc_AD6B
    stx $283C
loc_AD66:
    lda #$00
    sta $283B
loc_AD6B:
    rts
load_camera:
    ; Writes to screen memory; Bit extraction/packing via shifts and rotates; Calls: render_9B3C; 80 instructions
    lda cam_heading_lo
    sta $87
    lda cam_heading_hi
    lsr
    ror $87
    lsr
    ror $87
    lsr
    ror $87
    lsr
    ror $87
    sta zp_grid_x
    lda cam_pitch_lo
    sta $89
    lda cam_pitch_hi
    lsr
    ror $89
    lsr
    ror $89
    lsr
    ror $89
    lsr
    ror $89
    sta zp_grid_row
    lda $289F
    sta zp_cam_alt_lo
    lda cam_altitude
    cmp #$40
    bcc loc_ADAB
    lda #$FF
    sta zp_cam_alt_hi
    jmp loc_ADB3
loc_ADAB:
    asl zp_cam_alt_lo
    rol
    rol zp_cam_alt_lo
    rol
    sta zp_cam_alt_hi
loc_ADB3:
    lda $289A
    rol
    rol
    sta zp_dir_quadrant
    jsr render_9B3C
    lda $280A
    sta zp_disp_x_lo
    lda $280B
    sta zp_disp_x_hi
    lda $280C
    sta zp_disp_z_lo
    lda $280D
    sta zp_disp_z_hi
    lda $288E
    sta $A6
    lda $288D
    rol
    rol $A6
    rol
    rol $A6
    lda #$06
    sec
    sbc $A6
    sta $A6
    lda $288A
    pha
    cmp #$80
    ror
    sta $A4
    lda $2889
    ror
    sta $A5
    clc
    pla
    adc #$04
    bpl loc_AE00
    lda #$00
    jmp loc_AE06
loc_AE00:
    cmp #$09
    bcc loc_AE06
    lda #$08
loc_AE06:
    sta $2825
    rts
.proc render_copy_AE0A
    ; Copy loop — indexed load/store transfer; Writes to screen memory; Table access: $2708, $2709, $2718, $2719; 26 instructions, 2 loops
    lda $A6
    sta $2718
    ldx #$00
    stx zp_temp
loc_AE13:
    lda zp_temp
    clc
    adc $A5
    sta zp_temp
    lda $2718,x
    adc $A4
    sta $2719,x
    inx
    cpx #$0F
    bne loc_AE13
    lda #$00
    sta zp_temp
loc_AE2B:
    lda zp_temp
    sec
    sbc $A5
    sta zp_temp
    lda $2709,x
    sbc $A4
    sta horizon_profile,x
    dex
    bpl loc_AE2B
    rts
.endp

draw_AE3E:
    ; Writes to screen memory; 85 instructions
    stx $94
    ldy #$2A
    lda #$00
loc_AE44:
    sta $1010,x
    sta $1070,x
    sta $10D0,x
    sta $1130,x
    sta $1190,x
    sta $11F0,x
    sta $1250,x
    sta $12B0,x
    sta $1310,x
    sta $1370,x
    sta $13D0,x
    sta $1430,x
    sta $1490,x
    sta $14F0,x
    sta $1550,x
    sta $15B0,x
    sta $1610,x
    sta $1670,x
    sta $16D0,x
    sta $1730,x
    sta $1790,x
    sta $17F0,x
    sta $1850,x
    sta $18B0,x
    sta $1910,x
    sta $1970,x
    sta $19D0,x
    sta $1A30,x
    sta $1A90,x
    sta $1AF0,x
    sta $1B50,x
    sta $1BB0,x
    sta $1C10,x
    sta $1C70,x
    sta $1CD0,x
    sta $1D30,x
    sta $1D90,x
    sta $1DF0,x
    sta $1E50,x
    sta $1EB0,x
    sta $1F10,x
    sta $1F70,x
    sta $1FD0,x
    sta $2030,x
    inx
    dey
    beq loc_AECF
    jmp loc_AE44
loc_AECF:
    ldx $94
    sta $2090,x
    sta $2091,x
    sta $2092,x
    sta $2093,x
    sta $2094,x
    sta $20BA,x
    sta $20B9,x
    sta $20B8,x
    sta $20B7,x
    sta $20B6,x
    sta $20B5,x
    sta $20F0,x
    sta $20F1,x
    sta $20F2,x
    sta $20F3,x
    sta $211A,x
    sta $2119,x
    sta $2118,x
    sta $2117,x
    sta $2116,x
    sta $2150,x
    sta $2151,x
    sta $2152,x
    sta $217A,x
    sta $2179,x
    sta $2178,x
    sta $2177,x
    sta $21B0,x
    sta $21DA,x
    sta $21B1,x
    sta $21D9,x
    sta $21D8,x
    rts
fill_column_fast:
    ; FILL COLUMN FAST — Fast vertical column fill for terrain: writes color bytes to screen memory using unrolled store sequence
    txa
    tay
    lda #$2A
    sta zp_norm_shift
loc_AF38:
    lda #$00
    cmp $1010,y
    beq loc_AF46
    lda #$55
    ldx #$00
    jmp loc_B20E
loc_AF46:
    cmp $1070,y
    beq loc_AF52
    lda #$55
    ldx #$01
    jmp loc_B20B
loc_AF52:
    cmp $10D0,y
    beq loc_AF5E
    lda #$55
    ldx #$02
    jmp loc_B208
loc_AF5E:
    cmp $1130,y
    beq loc_AF6A
    lda #$55
    ldx #$03
    jmp loc_B205
loc_AF6A:
    cmp $1190,y
    beq loc_AF76
    lda #$55
    ldx #$04
    jmp loc_B202
loc_AF76:
    cmp $11F0,y
    beq loc_AF82
    lda #$55
    ldx #$05
    jmp loc_B1FF
loc_AF82:
    cmp $1250,y
    beq loc_AF8E
    lda #$55
    ldx #$06
    jmp loc_B1FC
loc_AF8E:
    cmp $12B0,y
    beq loc_AF9A
    lda #$55
    ldx #$07
    jmp loc_B1F9
loc_AF9A:
    cmp $1310,y
    beq loc_AFA6
    lda #$55
    ldx #$08
    jmp loc_B1F6
loc_AFA6:
    cmp $1370,y
    beq loc_AFB2
    lda #$55
    ldx #$09
    jmp loc_B1F3
loc_AFB2:
    cmp $13D0,y
    beq loc_AFBE
    lda #$55
    ldx #$0A
    jmp loc_B1F0
loc_AFBE:
    cmp $1430,y
    beq loc_AFCA
    lda #$55
    ldx #$0B
    jmp loc_B1ED
loc_AFCA:
    cmp $1490,y
    beq loc_AFD6
    lda #$55
    ldx #$0C
    jmp loc_B1EA
loc_AFD6:
    cmp $14F0,y
    beq loc_AFE2
    lda #$55
    ldx #$0D
    jmp loc_B1E7
loc_AFE2:
    cmp $1550,y
    beq loc_AFEE
    lda #$55
    ldx #$0E
    jmp loc_B1E4
loc_AFEE:
    cmp $15B0,y
    beq loc_AFFA
    lda #$55
    ldx #$0F
    jmp loc_B1E1
loc_AFFA:
    cmp $1610,y
    beq loc_B006
    lda #$55
    ldx #$10
    jmp loc_B1DE
loc_B006:
    cmp $1670,y
    beq loc_B012
    lda #$55
    ldx #$11
    jmp loc_B1DB
loc_B012:
    cmp $16D0,y
    beq loc_B01E
    lda #$55
    ldx #$12
    jmp loc_B1D8
loc_B01E:
    cmp $1730,y
    beq loc_B02A
    lda #$55
    ldx #$13
    jmp loc_B1D5
loc_B02A:
    cmp $1790,y
    beq loc_B036
    lda #$55
    ldx #$14
    jmp loc_B1D2
loc_B036:
    cmp $17F0,y
    beq loc_B042
    lda #$55
    ldx #$15
    jmp loc_B1CF
loc_B042:
    cmp $1850,y
    beq loc_B04E
    lda #$55
    ldx #$16
    jmp loc_B1CC
loc_B04E:
    cmp $18B0,y
    beq loc_B05A
    lda #$55
    ldx #$17
    jmp loc_B1C9
loc_B05A:
    cmp $1910,y
    beq loc_B066
    lda #$55
    ldx #$18
    jmp loc_B1C6
loc_B066:
    cmp $1970,y
    beq loc_B072
    lda #$55
    ldx #$19
    jmp loc_B1C3
loc_B072:
    cmp $19D0,y
    beq loc_B07E
    lda #$55
    ldx #$1A
    jmp loc_B1C0
loc_B07E:
    cmp $1A30,y
    beq loc_B08A
    lda #$55
    ldx #$1B
    jmp loc_B1BD
loc_B08A:
    cmp $1A90,y
    beq loc_B096
    lda #$55
    ldx #$1C
    jmp loc_B1BA
loc_B096:
    cmp $1AF0,y
    beq loc_B0A2
    lda #$55
    ldx #$1D
    jmp loc_B1B7
loc_B0A2:
    cmp $1B50,y
    beq loc_B0AE
    lda #$55
    ldx #$1E
    jmp loc_B1B4
loc_B0AE:
    cmp $1BB0,y
    beq loc_B0BA
    lda #$55
    ldx #$1F
    jmp loc_B1B1
loc_B0BA:
    cmp $1C10,y
    beq loc_B0C6
    lda #$55
    ldx #$20
    jmp loc_B1AE
loc_B0C6:
    cmp $1C70,y
    beq loc_B0D2
    lda #$55
    ldx #$21
    jmp loc_B1AB
loc_B0D2:
    cmp $1CD0,y
    beq loc_B0DE
    lda #$55
    ldx #$22
    jmp loc_B1A8
loc_B0DE:
    cmp $1D30,y
    beq loc_B0EA
    lda #$55
    ldx #$23
    jmp loc_B1A5
loc_B0EA:
    cmp $1D90,y
    beq loc_B0F6
    lda #$55
    ldx #$24
    jmp loc_B1A2
loc_B0F6:
    cmp $1DF0,y
    beq loc_B102
    lda #$55
    ldx #$25
    jmp loc_B19F
loc_B102:
    cmp $1E50,y
    beq loc_B10E
    lda #$55
    ldx #$26
    jmp loc_B19C
loc_B10E:
    cmp $1EB0,y
    beq loc_B11A
    lda #$55
    ldx #$27
    jmp loc_B199
loc_B11A:
    cmp $1F10,y
    beq loc_B126
    lda #$55
    ldx #$28
    jmp loc_B196
loc_B126:
    cmp $1F70,y
    beq loc_B132
    lda #$55
    ldx #$29
    jmp loc_B193
loc_B132:
    cmp $1FD0,y
    beq loc_B13E
    lda #$55
    ldx #$2A
    jmp loc_B190
loc_B13E:
    cmp $2030,y
    beq loc_B14A
    lda #$55
    ldx #$2B
    .byte $4C                     ; $B147
loc_B148:
    .byte $8D                     ; $B148
    .byte $B1                     ; $B149
loc_B14A:
    cmp $2090,y
    beq loc_B156
    lda #$55
    ldx #$2C
    jmp loc_B18A
loc_B156:
    cmp $20F0,y
    beq loc_B162
    lda #$55
    ldx #$2D
    jmp loc_B187
loc_B162:
    cmp $2150,y
    beq loc_B16E
    lda #$55
    ldx #$2E
    .byte $4C                     ; $B16B
    .byte $84                     ; $B16C
loc_B16D:
    .byte $B1                     ; $B16D
loc_B16E:
    cmp $21B0,y
    beq loc_B17A
    lda #$55
    ldx #$2F
    jmp loc_B181
loc_B17A:
    lda #$55
    ldx #$30
    sta $21B0,y
loc_B181:
    sta $2150,y
loc_B184:
    sta $20F0,y
loc_B187:
    sta $2090,y
loc_B18A:
    sta $2030,y
loc_B18D:
    sta $1FD0,y
loc_B190:
    .byte $99                     ; $B190
    .byte $70                     ; $B191
loc_B192:
    .byte $1F                     ; $B192
loc_B193:
    sta $1F10,y
loc_B196:
    sta $1EB0,y
loc_B199:
    sta $1E50,y
loc_B19C:
    sta $1DF0,y
loc_B19F:
    sta $1D90,y
loc_B1A2:
    sta $1D30,y
loc_B1A5:
    sta $1CD0,y
loc_B1A8:
    sta $1C70,y
loc_B1AB:
    sta $1C10,y
loc_B1AE:
    sta $1BB0,y
loc_B1B1:
    sta $1B50,y
loc_B1B4:
    sta $1AF0,y
loc_B1B7:
    sta $1A90,y
loc_B1BA:
    sta $1A30,y
loc_B1BD:
    .byte $99                     ; $B1BD
    .byte $D0                     ; $B1BE
loc_B1BF:
    .byte $19                     ; $B1BF
loc_B1C0:
    sta $1970,y
loc_B1C3:
    sta $1910,y
loc_B1C6:
    sta $18B0,y
loc_B1C9:
    sta $1850,y
loc_B1CC:
    sta $17F0,y
loc_B1CF:
    sta $1790,y
loc_B1D2:
    sta $1730,y
loc_B1D5:
    sta $16D0,y
loc_B1D8:
    sta $1670,y
loc_B1DB:
    sta $1610,y
loc_B1DE:
    sta $15B0,y
loc_B1E1:
    sta $1550,y
loc_B1E4:
    sta $14F0,y
loc_B1E7:
    sta $1490,y
loc_B1EA:
    sta $1430,y
loc_B1ED:
    sta $13D0,y
loc_B1F0:
    sta $1370,y
loc_B1F3:
    sta $1310,y
loc_B1F6:
    sta $12B0,y
loc_B1F9:
    sta $1250,y
loc_B1FC:
    sta $11F0,y
loc_B1FF:
    sta $1190,y
loc_B202:
    sta $1130,y
loc_B205:
    sta $10D0,y
loc_B208:
    sta $1070,y
loc_B20B:
    nop
    nop
    nop
loc_B20E:
    stx zp_blend_row
    lda #$00
    sta zp_edge_x_arr
    lda #$55
    sta zp_edge_x_next
loc_B218:
    ldx zp_blend_row
    lda row_addr_lo_blend,x
    sta zp_ptr_lo
    lda row_addr_hi_blend,x
    sta zp_ptr_hi
    lda (zp_ptr_lo),y
    tax
    and zp_edge_x_arr
    ora zp_edge_x_next
    ora pix_blend_merge,x
    sta (zp_ptr_lo),y
    lda pix_blend_cont,x
    and zp_edge_x_next
    beq loc_B244
    sta zp_edge_x_next
    tax
    lda pix_blend_cont,x
    sta zp_edge_x_arr
    inc zp_blend_row
    jmp loc_B218
loc_B244:
    iny
    dec zp_norm_shift
    beq loc_B24C
    jmp loc_AF38
loc_B24C:
    rts
rasterize_edge:
    ; RASTERIZE EDGE — Draws terrain edge (mountain silhouette): Bresenham-style line between two projected vertices
    lda work_proj_x_hi
    eor #$80
    sta zp_temp
    lda zp_proj_x_hi
    eor #$80
    cmp zp_temp
    bne loc_B261
    lda zp_column_x
    cmp work_proj_x_lo
loc_B261:
    bcs loc_B29C
    lda #$14
    sta zp_norm_shift
loc_B267:
    lda zp_proj_x_hi
    bpl loc_B2B4
    dec zp_norm_shift
    bmi loc_B29C
    jsr edge_midpoint
    lda $8E
    bmi loc_B29D
    bne loc_B27E
    lda $8D
    cmp #$28
    bcc loc_B29D
loc_B27E:
    lda $8D
    sta $2573,x
    lda $8E
    sta $2591,x
    lda $8F
    sta $25AF,x
    lda $90
    sta $25CD,x
    lda $91
    sta $25EB,x
    inx
    cpx #$0F
    bcc loc_B267
loc_B29C:
    rts
loc_B29D:
    lda $8D
    sta zp_column_x
    lda $8E
    sta zp_proj_x_hi
    lda $8F
    sta zp_depth_accum
    lda $90
    sta zp_proj_y_hi
    lda $91
    sta zp_color_shade
    jmp loc_B267
loc_B2B4:
    lda zp_proj_x_hi
    bne loc_B29C
    lda zp_column_x
    cmp #$D8
    bcs loc_B29C
loc_B2BE:
    lda work_proj_x_hi,x
    beq loc_B2EC
loc_B2C3:
    dec zp_norm_shift
    bmi loc_B2EB
    jsr edge_midpoint
    lda $8D
    sta $2573,x
    lda $8E
    sta $2591,x
    lda $8F
    sta $25AF,x
    lda $90
    sta $25CD,x
    lda $91
    sta $25EB,x
    inx
    cpx #$0F
    bcs loc_B2EB
    jmp loc_B2BE
loc_B2EB:
    rts
loc_B2EC:
    lda zp_proj_y_hi
    bmi loc_B2F8
    bne loc_B308
    lda zp_depth_accum
    cmp #$6C
    bcs loc_B308
loc_B2F8:
    lda work_proj_y_hi,x
    bmi loc_B319
    bne loc_B31C
    lda work_proj_y_lo,x
    cmp #$6C
    bcc loc_B319
    bcs loc_B31C
loc_B308:
    lda work_proj_y_hi,x
    bmi loc_B338
    bne loc_B356
    lda work_proj_y_lo,x
    cmp #$6C
    bcc loc_B338
    jmp loc_B356
loc_B319:
    jmp loc_B385
loc_B31C:
    sec
    lda work_proj_x_lo,x
    sbc zp_column_x
    cmp #$14
    bcc loc_B356
    lsr
    lsr
    sta zp_temp
    lda zp_depth_accum
    sec
    sbc zp_temp
    lda zp_proj_y_hi
    sbc #$00
    bpl loc_B356
    jmp loc_B2C3
loc_B338:
    sec
    lda work_proj_x_lo,x
    sbc zp_column_x
    cmp #$14
    bcc loc_B356
    lsr
    lsr
    sta zp_temp
    lda work_proj_y_lo,x
    sec
    sbc zp_temp
    lda work_proj_y_hi,x
    sbc #$00
    bpl loc_B356
    jmp loc_B2C3
loc_B356:
    lda zp_proj_y_hi
    beq loc_B364
    bmi loc_B360
    lda #$FF
    bne loc_B362
loc_B360:
    lda #$00
loc_B362:
    sta zp_depth_accum
loc_B364:
    lda work_proj_y_hi,x
    beq loc_B373
    bmi loc_B36F
    lda #$FF
    bne loc_B376
loc_B36F:
    lda #$00
    beq loc_B376
loc_B373:
    lda work_proj_y_lo,x
loc_B376:
    sta zp_depth_arr
    lda work_proj_x_lo,x
    sta zp_edge_x_arr
    lda work_height,x
    sta EEXP
    jsr fill_terrain_column
loc_B385:
    cpx #$00
    beq loc_B3A6
    lda work_proj_x_lo,x
    sta zp_column_x
    lda work_proj_x_hi,x
    sta zp_proj_x_hi
    lda work_proj_y_lo,x
    sta zp_depth_accum
    lda work_proj_y_hi,x
    sta zp_proj_y_hi
    lda work_height,x
    sta zp_color_shade
    dex
    jmp loc_B2B4
loc_B3A6:
    rts
.proc edge_midpoint
    ; EDGE MIDPOINT — Computes midpoint of terrain edge for subdivision, adds fractal perturbation
    sec
    lda zp_column_x
    adc work_proj_x_lo,x
    sta $8D
    lda zp_proj_x_hi
    adc work_proj_x_hi,x
    cmp #$80
    ror
    sta $8E
    ror $8D
    sec
    lda zp_depth_accum
    adc work_proj_y_lo,x
    sta $8F
    lda zp_proj_y_hi
    adc work_proj_y_hi,x
    cmp #$80
    ror
    sta $90
    ror $8F
    lda zp_color_shade
    sec
    adc work_height,x
    sta $91
    bmi loc_B3DA
    rts
loc_B3DA:
    bcs loc_B3FA
    lda $8D
    sec
    sbc zp_column_x
    sta zp_temp
    lda $8E
    sbc zp_proj_x_hi
    lsr
    sta zp_grid_row_accum
    ror zp_temp
    lda $8F
    sec
    sbc zp_temp
    sta $8F
    lda $90
    sbc zp_grid_row_accum
    sta $90
    rts
loc_B3FA:
    lda $8D
    sec
    sbc zp_column_x
    sta zp_temp
    lda $8E
    sbc zp_proj_x_hi
    lsr
    sta zp_grid_row_accum
    ror zp_temp
    lda $8F
    clc
    adc zp_temp
    sta $8F
    lda $90
    adc zp_grid_row_accum
    sta $90
    rts
.endp

fill_terrain_column:
    ; Copy loop — indexed load/store transfer; Writes to screen memory; Bit extraction/packing via shifts and rotates; 201 instructions, 2 loops
    stx saved_column_x
    lda zp_edge_x_arr
    cmp #$2D
    bcc loc_B45C
    cmp zp_column_x
    bcc loc_B45C
    bne loc_B45D
    ldx zp_edge_x_arr
    lda zp_depth_arr
    cmp zbuf_columns,x
    bcc loc_B459
    beq loc_B459
    sta zbuf_columns,x
    cmp #$97
    bcc loc_B440
    lda #$FF
    sta zbuf_columns,x
    lda #$97
loc_B440:
    sty zp_temp
    tay
    lda row_addr_lo_render,y
    sta zp_ptr_lo
    lda row_addr_hi_render,y
    sta zp_ptr_hi
    ldy pix_byte_offset,x
    lda (zp_ptr_lo),y
    ora pix_color_mask,x
    sta (zp_ptr_lo),y
    ldy zp_temp
loc_B459:
    ldx saved_column_x
loc_B45C:
    rts
loc_B45D:
    ldy #$00
loc_B45F:
    lda zp_column_x
    cmp #$2C
    bcc loc_B469
    tax
    jmp loc_B51C
loc_B469:
    clc
    adc zp_edge_x_arr,y
    ror
    cmp #$2C
    bcc loc_B474
    bne loc_B4C2
loc_B474:
    sta zp_column_x
    lda zp_color_shade
    sec
    adc EEXP,y
    sta zp_color_shade
    bmi loc_B48C
    clc
    lda zp_depth_accum
    adc zp_depth_arr,y
    ror
    sta zp_depth_accum
    jmp loc_B45F
loc_B48C:
    bcs loc_B4A6
    lda zp_edge_x_arr,y
    sec
    sbc zp_column_x
    lsr
    sta zp_temp
    clc
    lda zp_depth_accum
    adc zp_depth_arr,y
    ror
    sec
    sbc zp_temp
    sta zp_depth_accum
    jmp loc_B45F
loc_B4A6:
    lda zp_edge_x_arr,y
    sec
    sbc zp_column_x
    lsr
    sta zp_temp
    clc
    lda zp_depth_accum
    adc zp_depth_arr,y
    ror
    clc
    adc zp_temp
    bcc loc_B4BD
    lda #$FF
loc_B4BD:
    sta zp_depth_accum
    jmp loc_B45F
loc_B4C2:
    sta zp_edge_x_next,y
    lda zp_color_shade
    sec
    adc EEXP,y
    sta NSIGN,y
    bmi loc_B4DE
    clc
    lda zp_depth_accum
    adc zp_depth_arr,y
    ror
    sta zp_depth_interp,y
    iny
    jmp loc_B45F
loc_B4DE:
    bcs loc_B4FA
    lda zp_edge_x_next,y
    sec
    sbc zp_column_x
    lsr
    sta zp_temp
    clc
    lda zp_depth_accum
    adc zp_depth_arr,y
    ror
    sec
    sbc zp_temp
    sta zp_depth_interp,y
    iny
    jmp loc_B45F
loc_B4FA:
    lda zp_edge_x_next,y
    sec
    sbc zp_column_x
    lsr
    sta zp_temp
    clc
    lda zp_depth_accum
    adc zp_depth_arr,y
    ror
    clc
    adc zp_temp
    bcc loc_B511
    lda #$FF
loc_B511:
    sta zp_depth_interp,y
    iny
    jmp loc_B45F
loc_B518:
    ldx saved_column_x
    rts
loc_B51C:
    cpx #$D4
    bcs loc_B518
    txa
    stx zp_column_x
    sec
    sbc zp_edge_x_arr,y
    cmp #$FE
    bne loc_B5A3
    lda zp_depth_arr,y
    adc zp_depth_accum
    ror
    cmp zbuf_columns,x
    bcc loc_B55F
    beq loc_B55F
    sta zbuf_columns,x
    cmp #$97
    bcc loc_B546
    lda #$FF
    sta zbuf_columns,x
    lda #$97
loc_B546:
    sty zp_temp
    tay
    lda row_addr_lo_render,y
    sta zp_ptr_lo
    lda row_addr_hi_render,y
    sta zp_ptr_hi
    ldy pix_byte_offset,x
    lda (zp_ptr_lo),y
    ora pix_color_mask,x
    sta (zp_ptr_lo),y
    ldy zp_temp
loc_B55F:
    inx
    lda zp_depth_arr,y
    sta zp_depth_accum
    cmp zbuf_columns,x
    bcc loc_B593
    beq loc_B593
    sta zbuf_columns,x
    cmp #$97
    bcc loc_B57A
    lda #$FF
    sta zbuf_columns,x
    lda #$97
loc_B57A:
    sty zp_temp
    tay
    lda row_addr_lo_render,y
    sta zp_ptr_lo
    lda row_addr_hi_render,y
    sta zp_ptr_hi
    ldy pix_byte_offset,x
    lda (zp_ptr_lo),y
    ora pix_color_mask,x
    sta (zp_ptr_lo),y
    ldy zp_temp
loc_B593:
    dey
    bmi loc_B59F
    inx
    lda NSIGN,y
    sta zp_color_shade
    jmp loc_B51C
loc_B59F:
    ldx saved_column_x
    rts
loc_B5A3:
    bcc loc_B5E4
    lda zp_depth_arr,y
    sta zp_depth_accum
    cmp zbuf_columns,x
    bcc loc_B5D8
    beq loc_B5D8
    sta zbuf_columns,x
    cmp #$97
    bcc loc_B5BF
    lda #$FF
    sta zbuf_columns,x
    lda #$97
loc_B5BF:
    sty zp_temp
    tay
    lda row_addr_lo_render,y
    sta zp_ptr_lo
    lda row_addr_hi_render,y
    sta zp_ptr_hi
    ldy pix_byte_offset,x
    lda (zp_ptr_lo),y
    ora pix_color_mask,x
    sta (zp_ptr_lo),y
    ldy zp_temp
loc_B5D8:
    dey
    bmi loc_B59F
    inx
    lda NSIGN,y
    sta zp_color_shade
    jmp loc_B51C
loc_B5E4:
    txa
    adc zp_edge_x_arr,y
    ror
    sta zp_edge_x_next,y
    lda zp_color_shade
    sec
    adc EEXP,y
    sta NSIGN,y
    bmi loc_B605
    clc
    lda zp_depth_accum
    adc zp_depth_arr,y
    ror
    sta zp_depth_interp,y
    iny
    jmp loc_B51C
loc_B605:
    bcs loc_B61F
    lda zp_edge_x_next,y
    sbc zp_column_x
    lsr
    sta zp_temp
    clc
    lda zp_depth_accum
    adc zp_depth_arr,y
    ror
    sbc zp_temp
    sta zp_depth_interp,y
    iny
    jmp loc_B51C
loc_B61F:
    lda zp_edge_x_next,y
    sbc zp_column_x
    lsr
    sta zp_temp
    clc
    lda zp_depth_accum
    adc zp_depth_arr,y
    ror
    adc zp_temp
    bcc loc_B634
    lda #$FF
loc_B634:
    sta zp_depth_interp,y
    iny
    jmp loc_B51C
    .byte $80,$20,$08,$02,$C0,$30,$0C,$03                  ; $B63B
    :4 .byte $2F                    ; $B643 (4 bytes)
    .byte $2E,$2D,$2C                                      ; $B647
    :35 .byte $2B                    ; $B64A (35 bytes)
    .byte $2C,$2D,$2E                                      ; $B66D
    :7 .byte $2F                    ; $B670 (7 bytes)
    .byte $2E,$2D,$2C                                      ; $B677
    :35 .byte $2B                    ; $B67A (35 bytes)
    .byte $2C,$2D,$2E,$2F,$2F,$2F,$10,$50,$20,$20,$80,$80,$C0 ; $B69D
    :4 .byte $40                    ; $B6AA (4 bytes)
    :4 .byte $10                    ; $B6AE (4 bytes)
    .byte $50                                              ; $B6B2
    :6 .byte $20                    ; $B6B3 (6 bytes)
    :6 .byte $80                    ; $B6B9 (6 bytes)
    .byte $C0                                              ; $B6BF
    :8 .byte $40                    ; $B6C0 (8 bytes)
    :8 .byte $10                    ; $B6C8 (8 bytes)
    .byte $50,$60,$80,$80,$10,$10,$A0                      ; $B6D0
    :4 .byte $20                    ; $B6D7 (4 bytes)
    :4 .byte $40                    ; $B6DB (4 bytes)
    .byte $60                                              ; $B6DF
    :6 .byte $80                    ; $B6E0 (6 bytes)
    :6 .byte $10                    ; $B6E6 (6 bytes)
    .byte $A0                                              ; $B6EC
    :8 .byte $20                    ; $B6ED (8 bytes)
    :8 .byte $40                    ; $B6F5 (8 bytes)
    .byte $00,$A0,$40,$40,$20,$20,$60                      ; $B6FD
    :4 .byte $10                    ; $B704 (4 bytes)
    :4 .byte $80                    ; $B708 (4 bytes)
    .byte $A0                                              ; $B70C
    :6 .byte $40                    ; $B70D (6 bytes)
    :6 .byte $20                    ; $B713 (6 bytes)
    .byte $60                                              ; $B719
    :8 .byte $10                    ; $B71A (8 bytes)
    :8 .byte $80                    ; $B722 (8 bytes)
    .byte $40,$C0,$10,$10,$40,$40,$50                      ; $B72A
    :4 .byte $80                    ; $B731 (4 bytes)
    :4 .byte $20                    ; $B735 (4 bytes)
    .byte $C0                                              ; $B739
    :6 .byte $10                    ; $B73A (6 bytes)
    :6 .byte $40                    ; $B740 (6 bytes)
    .byte $50                                              ; $B746
    :8 .byte $80                    ; $B747 (8 bytes)
    :8 .byte $20                    ; $B74F (8 bytes)
edge_adjacency:
    ; EDGE ADJACENCY TABLE — Lookup table for terrain edge neighbor relationships
    .byte $02,$00,$00,$04,$02,$01,$03,$02,$04,$03,$05,$04,$05,$07,$04,$08 ; $B757
    .byte $09,$03,$03,$0B,$0C,$02,$0D,$01,$06,$07,$07,$08,$08,$09,$09,$0A ; $B767
    .byte $0A,$0B,$0B,$0C,$0C,$0D,$0D,$0E,$10,$0E,$11,$0D,$12,$0C,$0B,$13 ; $B777
    .byte $0A,$14,$16,$0A,$17,$09,$08,$18,$07,$19,$06,$1A,$1B,$1A,$1A,$19 ; $B787
    .byte $19,$18,$18,$17,$17,$16,$16,$15,$15,$14,$14,$13,$13,$12,$12,$11 ; $B797
    .byte $11,$10,$10,$0F,$2B,$0F,$2A,$10,$29,$11,$28,$12,$13,$27,$14,$26 ; $B7A7
    .byte $15,$25,$23,$15,$22,$16,$21,$17,$18,$20,$19,$1F,$1A,$1E,$1B,$1D ; $B7B7
    .byte $1C,$1D,$1D,$1E,$1E,$1F,$1F,$20,$20,$21,$21,$22,$22,$23,$23,$24 ; $B7C7
    .byte $24,$25,$25,$26,$26,$27,$27,$28,$28,$29,$29,$2A,$2A,$2B,$2B,$2C ; $B7D7