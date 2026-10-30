; Altirra BASIC
; Copyright (C) 2026 Avery Lee, All Rights Reserved.
;
; Copying and distribution of this file, with or without modification,
; are permitted in any medium without royalty provided the copyright
; notice and this notice are preserved.  This file is offered as-is,
; without any warranty.

;===============================================================================
; Inline loader code
;
; This code is used both by the non-relocating high loader (ATBASICX) and the
; low relocating loader (ATBASICR).

		;check if there is a command line to process
		ldy		#0
		sty		iocbidx				;!! - needed since we will be skipping it
		lda		(dosvec),y
		cmp		#$4c
		bne		no_cmdline
		ldy		#3
		lda		(dosvec),y
		cmp		#$4c
		beq		have_cmdline
no_cmdline:
		lda		#0
		jmp		no_filename

have_cmdline:
		;skip spaces
		ldy		#10
		lda		(dosvec),y
		clc
		adc		#63
		tay
space_loop:
		lda		(dosvec),y
		cmp		#$9b
		beq		no_filename
		iny
		cmp		#' '
		beq		space_loop

		;stash filename base offset
		dey
		sty		fr0+3

		;check if the first character is other than D and there is a colon
		;afterward -- if so, we should skip DOS's parser and use it straight
		;as it may be a CIO filename that DOS would munge
		cmp		#'D'
		beq		possibly_dos_file
cio_file:
		;copy filename to LBUFF
		ldx		#0
cio_copy_loop:
		lda		(dosvec),y
		sta		lbuff,x
		inx
		iny
		cmp		#$9b
		bne		cio_copy_loop

		;stash length
		stx		fr0+2

		tya
		jmp		have_filename_nz

possibly_dos_file:
		;scan for colon
colon_loop:
		lda		(dosvec),y
		iny
		cmp		#':'
		beq		cio_file
		cmp		#$9b
		bne		colon_loop

		;okay, assume it's a DOS file - clear the CIO filename flag
		lda		#0
		sta		fr0+2
		
		;try to parse out a filename
		ldy		fr0+3
		sec
		sbc		#63
		ldy		#10
		sta		(dosvec),y

		ldy		#4
		mva		(dosvec),y fr0
		iny
		mva		(dosvec),y fr0+1
		jsr		jump_fr0
		
no_filename:
have_filename_nz:
		;save off filename flag
		php

		;cold boot ATBasic
		ldx		#0
		stx		iocbidx
		stx		iocbexec
		inx
		stx		errno

		;print startup banner
		mwa		#msg_banner_begin icbal
		mwa		#msg_banner_end-msg_banner_begin icbll
		mva		#CIOCmdPutChars iccmd
		ldx		#0
		jsr		ciov

		jsr		_stNew.reset_entry
		jsr		ExecReset

		;read filename flag
		plp
		bne		explicit_fn

		;no filename... try loading implicit file
		ldx		#$70
		stx		iocbidx
		mwa		#default_fn_start icbal+$70
		mwa		#default_fn_end-default_fn_start icbll+$70
		mva		#CIOCmdOpen iccmd+$70
		mva		#$04 icax1+$70
		jsr		ciov
		bmi		load_failed

		;load and run
		lsr		stLoadRun._loadflg
		jmp		stLoadRun.with_open_iocb

load_failed:
		;failed... undo the EOL with an up arrow so the prompt is in the right place
		mva		#0 iocbidx
		lda		#$1c
		jsr		putchar

		;close IOCB and jump to prompt
		ldx		#$70
		mva		#CIOCmdClose iccmd+$70
		jsr		ciov
		jmp		immediateModeReset

explicit_fn:
		;move filename to line buffer
		ldy		#33
		ldx		#0
		stx		fr0+3

		;check if filename is already there
		lda		fr0+2
		bne		fncopy_skip
fncopy_loop:
		lda		(dosvec),y
		sta		lbuff,x
		cmp		#$9b
		beq		fncopy_exit
		iny
		inx
		bne		fncopy_loop
fncopy_exit:
		;finish length
		stx		fr0+2

fncopy_skip:
		;set string pointer
		mwa		#lbuff fr0

		;set up for RUN statement
		jsr		IoPutNewline
		lsr		stLoadRun._loadflg
		ldx		#$70
		stx		iocbidx
		jmp		stLoadRun.loader_entry

wait_vbl:
		sei
		mwa		#1 cdtmv3
		cli
		lda:rne	cdtmv4
		rts

jump_fr0:
		jmp		(fr0)

editor:
		dta		c'E',$9B

default_fn_start:
		dta		'D:AUTORUN.BAS',$9B
default_fn_end:
