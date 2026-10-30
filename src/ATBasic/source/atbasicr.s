; Altirra BASIC
; Copyright (C) 2026 Avery Lee, All Rights Reserved.
;
; Copying and distribution of this file, with or without modification,
; are permitted in any medium without royalty provided the copyright
; notice and this notice are preserved.  This file is offered as-is,
; without any warranty.

		icl		'system.inc'
		icl		'interpvars.inc'
		icl		'config.inc'

relsrc = $80
relptr = $82
reldst = $84
reldst2 = $86
reldelta = $88

		opt		o+
		org		$3000

		.get	'atbasic.bin',0,8192
		.get [8192] 'atbasic-lo.bin'

;===============================================================================
; base image (assembled at $A000)
;
.proc base_image
		.sav	[0] 8192
.endp

;===============================================================================
; jump table
;
_stNew.reset_entry			jmp .wget[$4000]+$8000
ExecReset					jmp .wget[$4002]+$8000
stLoadRun.with_open_iocb	jmp .wget[$4006]+$8000
putchar						jmp .wget[$4008]+$8000
immediateModeReset			jmp .wget[$400a]+$8000
IoPutNewline				jmp .wget[$400c]+$8000
stLoadRun.loader_entry		jmp .wget[$400e]+$8000
IoConvNumToHex.print_digit_pair:
							jmp .wget[$4010]+$8000

;===============================================================================
; extern address table (non-relocated addresses)
;
stLoadRun._loadflg			= .wget[$4004]

;===============================================================================
; relocating loader
;
.proc main
		;round MEMLO up to next page and push start address
		lda		memlo
		clc
		adc		#$ff
		lda		#0
		sta		reldst
		sta		relsrc
		adc		memlo+1
		sta		reldst+1

		;push start address
		tax
		adc		#$ff
		pha
		lda		#$ff
		pha
		
		;set relocation delta
		txa
		sec
		sbc		#$a0
		sta		reldelta

		;apply relocation table
		mwa		#relocs relptr
		mwa		#base_image reldst2
		ldy		#0
		ldx		#0

reloc_loop:
		lda		(relptr),y
		bne		do_reloc

		;run loader
		jmp		loader

ext_delta:
		lda		(relptr),y
do_reloc:
		iny
		sne:inc	relptr+1

		cmp		#$ff
		php
		clc
		adc		reldst2
		sta		reldst2
		scc:inc	reldst2+1
		plp
		bcs		ext_delta

		;apply relocation
		lda		(reldst2,x)
		adc		reldelta
		sta		(reldst2,x)
		jmp		reloc_loop

loader:
		;copy down
		mva		#>base_image relsrc+1
		ldx		#$20
		ldy		#0
copy_loop:
		mva:rne	(relsrc),y (reldst),y+
		inc		relsrc+1
		inc		reldst+1
		dex
		bne		copy_loop

		;adjust MEMLO
		lda		reldst+1
		sta		memlo+1
		lda		#0
		sta		memlo

		;update banner with load address
		mwa		#msg_banner_addr inbuff
		lda		reldst+1
		sec
		sbc		#$20
		jsr		IoConvNumToHex.print_digit_pair

		;inline common loader
		icl		'loader.inc'

msg_banner_begin:
		dta		$9c				;delete loading line
		_MSG_BANNER
		dta		' @ $'
msg_banner_addr:
		dta		'0000',$9b
msg_banner_end:
.endp

;===============================================================================
; relocation table
;
		opt		l-
.proc relocs
		?last_offset = 0

	.if 0
		.rept 8192
		    ?byte1 = .get[#]
			?byte2 = .get[#+8192]
			.if ?byte1 != ?byte2
			    .if ?byte1 != ?byte2+$80
					.error "Invalid relocation at ",#
				.endif

				?delta = #-?last_offset
				:?delta/255 dta $FF
				dta ?delta%255

				?last_offset = #
			.endif
		.endr
	.else
		.rept 2048,#
			?dword1 = .dget[#*4]
			?dword2 = .dget[#*4+8192]

			.if ?dword1 != ?dword2
				?byte1 = .get[#*4+0]
				?byte2 = .get[#*4+8192]
				.if ?byte1 != ?byte2
					.if ?byte1 != ?byte2+$80
						.error "Invalid relocation at ",#*4+0
					.endif

					?delta = #*4+0-?last_offset
					:?delta/255 dta $FF
					dta ?delta%255

					?last_offset = #*4+0
				.endif

				?byte1 = .get[#*4+1]
				?byte2 = .get[#*4+8193]
				.if ?byte1 != ?byte2
					.if ?byte1 != ?byte2+$80
						.error "Invalid relocation at ",#*4+1
					.endif

					?delta = #*4+1-?last_offset
					:?delta/255 dta $FF
					dta ?delta%255

					?last_offset = #*4+1
				.endif

				?byte1 = .get[#*4+2]
				?byte2 = .get[#*4+8194]
				.if ?byte1 != ?byte2
					.if ?byte1 != ?byte2+$80
						.error "Invalid relocation at ",#*4+2
					.endif

					?delta = #*4+2-?last_offset
					:?delta/255 dta $FF
					dta ?delta%255

					?last_offset = #*4+2
				.endif

				?byte1 = .get[#*4+3]
				?byte2 = .get[#*4+8195]
				.if ?byte1 != ?byte2
					.if ?byte1 != ?byte2+$80
						.error "Invalid relocation at ",#*4+3
					.endif

					?delta = #*4+3-?last_offset
					:?delta/255 dta $FF
					dta ?delta%255

					?last_offset = #*4+3
				.endif
			.endif
		.endr
	.endif

		;jump table
		?delta = $2002-?last_offset
		:?delta/255 dta $FF
		dta		?delta%255
		:7 dta	3

		;end sentinel
		dta		$00
.endp
		opt		l+

		.echo "Relocation size: ",[.len relocs]

;===============================================================================
		run		main

		end
