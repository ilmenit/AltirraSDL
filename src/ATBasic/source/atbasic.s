; Altirra BASIC
; Copyright (C) 2014-2016 Avery Lee, All Rights Reserved.
;
; Copying and distribution of this file, with or without modification,
; are permitted in any medium without royalty provided the copyright
; notice and this notice are preserved.  This file is offered as-is,
; without any warranty.

		opt		m-

		icl		'system.inc'
		icl		'tokens.inc'
		icl		'config.inc'

;===========================================================================
		icl		'interpvars.inc'


.macro _STATIC_ASSERT
		.if :1
		.else
		.error ":2"
		.endif
.endm

.macro _PAGE_CHECK
		.if [:1^*]&$ff00
		.error "Page boundary crossed between ",:1," and ",*
		.endif
.endm

;==========================================================================
; EXE loader start
;
		.if CART==0

		org		$3000
		opt		o+
		
;==========================================================================
; Preloader
;
; The preloader executes before the main load.

.proc __preloader
		;check if BASIC is on
		jsr		__testROM
		beq		basic_ok
		
		;try to turn basic off
		lda		#0
		sta		basicf
		lda		portb
		ora		#2
		sta		portb

		;check again if BASIC is on
		jsr		__testROM
		beq		basic_ok

		;print failure
		mwa		#msg_romconflict_begin icbal
		mwa		#msg_romconflict_end-msg_romconflict_begin icbll
		mva		#CIOCmdPutChars iccmd
		ldx		#0
		jsr		ciov

		lda		#$ff
		cmp:req	ch
		sta		ch

		;exit
		jmp		(dosvec)
		
basic_ok:
		;print loading banner and continue disk load
		mwa		#msg_loading_begin icbal
		mwa		#msg_loading_end-msg_loading_begin icbll
		mva		#CIOCmdPutChars iccmd
		ldx		#0
		jmp		ciov

msg_loading_begin:
		dta		'Loading Altirra BASIC...'
msg_loading_end:

msg_romconflict_begin:
		dta		$9B
		dta		'Cannot load Altirra BASIC: another',$9B
		dta		'ROM is already present at $A000.',$9B
		dta		$9B
		dta		'If you are running under SpartaDOS X,',$9B
		dta		'use the X command to run ATBasic.',$9B
		dta		$9B
		dta		'Press a key',$9B
msg_romconflict_end:
.endp

;--------------------------------------------------------------------------
; Exit:
;	Z=0: Not writable
;	Z=1: Writable
;
.proc __testROM
		lda		$a000
		tax
		eor		#$ff
		sta		$a000
		cmp		$a000
		stx		$a000
		rts		
.endp

;--------------------------------------------------------------------------

		ini		__preloader

;--------------------------------------------------------------------------
.proc __loader
		;reset RAMTOP if it is above $A000
		lda		#$a0
		cmp		ramtop
		bcs		ramtop_ok

adjust_ramtop:
		sta		ramtop
		
		;reinitialize GR.0 screen if needed (XEP80 doesn't)
		lda		sdmctl
		and		#$20
		beq		dma_off
		
		jsr		wait_vbl

		ldx		#0
		stx		dmactl
		stx		sdmctl
		lda		#4
		cmp:rcc	vcount
		mva		#CIOCmdClose iccmd
		jsr		ciov

		mva		#CIOCmdOpen iccmd
		mwa		#editor icbal
		mva		#$0c icax1
		mva		#$00 icax2
		ldx		#0
		jsr		ciov

		;Wait for a VBLANK to ensure that the screen has taken place;
		;we don't just use RTCLOK because we need to ensure that stage
		;2 VBLANK has been run, not just stage 1.
		jsr		wait_vbl
dma_off:
ramtop_ok:

		;Check if we might have the OS-A or OS-B screen editor. The OS-A/B
		;screen editor has a nasty bug of clearing up to one page more than
		;it should on a screen clear, which will trash the beginning of our
		;soft-loaded BASIC interpreter. In that case, we need to drop RAMTOP
		;by another 4K to compensate. 4K is a lot, but is necessary since
		;the screen editor does not align playfields properly otherwise.
		
		;The first check we do is to see if the memory limit is already $9F00
		;or lower; if so, we don't have a problem.
		lda		ramtop
		cmp		#$A0
		bcc		editor_ok

		;The first check we do is to see if the put char routine for E: is
		;that of OS-A/B. If it isn't, then either we aren't running on OS-A/B,
		;or E: has been replaced. Either way, we're fine.
		lda		icptl
		cmp		#$A3
		bne		editor_ok
		lda		#$F6
		cmp		icptl+1
		bne		editor_ok

		;Okay, we might have OS-A/B. However, there's a chance that someone
		;preserved that entry point but fixed the editor (hah). So, let's do
		;a test: put a test byte at $A000 and clear the screen. If it gets
		;wiped, we have a problem.
		sta		$A000

		ldx		#0
		stx		icbll
		stx		icblh
		mva		#CIOCmdPutChars iccmd
		lda		#$7d
		jsr		ciov

		lda		$A000
		cmp		#$FC
		beq		editor_ok

		;Whoops... the test byte was overwritten. Lower RAMTOP from $A0 to
		;$90 and reinitialize E:.
		lda		#$90
		bne		adjust_ramtop

editor_ok:
		;reset RUNAD to $A000 so we can be re-invoked
		mwa		#$a000 runad

		;move $3800-57FF to $A000-BFFF
		mva		#$38 fr0+1
		mva		#$a0 fr1+1
		ldy		#0
		sty		fr0
		sty		fr1
		ldx		#$20
copy_loop:
		mva:rne	(fr0),y (fr1),y+
		inc		fr0+1
		inc		fr1+1
		dex
		bne		copy_loop

		;inline common loader
		icl		'loader.inc'

msg_banner_begin:
		dta		$9c				;delete loading line
		_MSG_BANNER
		dta		$9b
msg_banner_end:
.endp

		opt		h-
		dta		a($ffff),a($3800),a($57ff)

		.endif
		
;==========================================================================
; Cartridge start
;
		opt		h-

.ifdef ATBASIC_LO
		org		$2000
.else
		org		$a000
.endif

		opt		o+f+

;==========================================================================
; Entry point
;
; This is totally skipped in the EXE version, where we reuse the space for
; a reload-E: stub when returning to DOS.
;
		.if CART==0
msg_base = *+13
ReturnToDOS:
		ldx		#0
		jsr		IoCloseX
		mva		#$c0 ramtop
		sta		icax2
		lda		#$0c
		ldy		#<devname_e
		jsr		IoOpenStockDeviceX			;!! this overwrites $BC20+
		jmp		(dosvec)

		:13 dta 0

		.else
main:
		;check if this is a warm start
		ldx		warmst
		bmi		immediateModeReset
				
		;print banner
		sec
		rol		ioTermFlag
		jsr		IoPrintMessageIOCB0		;!! - X=0
		jmp		stNew

;==========================================================================
; Message base
;
msg_base:
msg_banner:
		_MSG_BANNER
		dta		0
		.endif

		.if		msg_base != (*&$E000)+$000D
		.error	"msg_base misaligned: ",*
		.endif

		.if		* != (*&$E000)+$0023
		.error	"msg_ready misaligned: ",*
		.endif

		.ifdef ATBASIC_LO
		org		$2023
		.else
		org		$A023
		.endif

msg_ready:
		dta		$9B,c'Ready',$9B,0

msg_stopped:
		dta		$9B,c"Stopped",0

msg_error:
		dta		$9B
msg_error2:
		dta		c"Error-   ",0

msg_atline:
		dta		c" at line ",0
msg_end:

		_STATIC_ASSERT (msg_end - msg_base) <= $100

;==========================================================================
immediateModeReset:
		jsr		ExecReset
immediateMode:
		;use IOCB #0 (E:) for commands
		ldx		#0
		stx		iocbexec
.proc execLoop
		;display prompt
		ldx		#msg_ready-msg_base
		jsr		IoPrintMessageIOCB0

loop2:	
		;reset stack
		ldx		#$ff
		txs

		;read read pointer
		inx
		stx		cix

		;reset errno
		inx
		stx		errno
	
		;read line
		ldx		iocbexec
		jsr		IoReadLineX			;also sets iocbidx for error handling
		beq		eof
		
		;float the stack if it isn't already
		jsr		ExecFloatStack

		;##TRACE "Parsing immediate mode line: [%.*s]" dw(icbll) lbuff
		jsr		parseLine
		bcc		loop2		

		;execute immediate mode line
		jmp		execDirect
		
eof:
		;close IOCB #7
		jsr		IoSetupIOCB7		;closes #7 as side effect
		
		;restart in immediate mode with IOCB 0
		jmp		immediateMode
.endp
 
;==========================================================================

		icl		'parserbytecode.s'
		icl		'parser.s'
		icl		'exec.s'
		icl		'data.s'
		icl		'statements.s'
		icl		'evaluator.s'
		icl		'functions.s'
		icl		'variables.s'
		icl		'math.s'
		icl		'io.s'
		icl		'memory.s'
		icl		'list.s'
		icl		'error.s'
		icl		'util.s'


;==========================================================================

pmg_dmactl_tab:					;3 bytes ($1c 0c 00)
		dta		$1c,$0c
empty_program:					;4 bytes ($00 00 80 03)
		dta		$00
pmgmode_tab:					;2 bytes ($00 80)
		dta		$00,$80
		dta		$03

;==========================================================================

.ifdef ATBASIC_LO
const_table = $3ffa - 4 - 6*9 - 6 - 7
		.echo	"Main program ends at ",*," (",[((((*-$2000)*100/8192)/10)*16+(((*-$2000)*100)/8192)%10)],"% full) (", const_table-*," bytes free)"
.else
const_table = $bffa - 4 - 6*9 - 6 - 7
		.echo	"Main program ends at ",*," (",[((((*-$a000)*100/8192)/10)*16+(((*-$a000)*100)/8192)%10)],"% full) (", const_table-*," bytes free)"
.endif

		org		const_table
		.echo	"Constant table begins at ",*
		.pages 1

devname_c:
		dta		'C'
devname_s:
		dta		'S'
devname_e:
		dta		'E'
devname_p:
		dta		'P'
devpath_d1all:
		dta		'D:*.*',$9B

		;The Maclaurin expansion for sin(x) is as follows:
		;
		; sin(x) = x - x^3/3! + x^5/5! - x^7/7! + x^9/9! - x^11/11!...
		;
		;We modify it this way:
		;
		; let y = x / pi2 (for x in [0, pi], pi2 = pi/2
		; sin(x) = y*[pi2 - y^2*pi2^3/3! + y^4*pi2^5/5! - y^6*pi2^7/7! + y^8*pi2*9/9! - y^10*pi2^11/11!...]
		;
		; let z = y^2
		; sin(x) = y*[pi2 - z*pi2^3/3! + z^2*pi2^5/5! - z^3*pi2^7/7! + z^4*pi2*9/9! - z^5*pi2^11/11!...]
		;
fpconst_sin:
		dta		$BD,$03,$43,$18,$69,$61		;-0.00 00 03 43 18 69 61 07114469471
		dta		$3E,$01,$60,$25,$47,$91		; 0.00 01 60 25 47 91 80067132008
		dta		$BE,$46,$81,$65,$78,$84		;-0.00 46 81 65 78 83 6641486819
		dta		$3F,$07,$96,$92,$60,$37		; 0.07 96 92 60 37 48579552158
		dta		$BF,$64,$59,$64,$09,$56		;-0.64 59 64 09 55 8200198258
angle_conv_tab:
fpconst_pi2:
		dta		$40,$01,$57,$07,$96,$33		; 1.57 07 96 32 67682236008 (also last sin coefficient)
		dta		$40,$90,$00,$00,$00			; 90 (!! - last byte shared with next table!)
hvstick_table:
		dta		$00,$FF,$01,$00

fp_180_div_pi:
		.fl		57.295779513082

const_one:
		dta		$40,$01
pmg_move_mask_tab:
		dta		$00,$00,$00,$00,$fc,$f3,$cf,$3f

		.endpg

.ifdef ATBASIC_LO
		_STATIC_ASSERT *=$3ffa
.else
		_STATIC_ASSERT *=$bffa
.endif
		
;==========================================================================
		
.ifdef ATBASIC_LO
		.echo	"Program ends at ",*," (",[((((*-$2000)*100/8192)/10)*16+(((*-$2000)*100)/8192)%10)],"% full)"

		org		$3ffa
		dta		a($2000)		;boot vector (not real, but for consistency)
.else
		.echo	"Program ends at ",*," (",[((((*-$a000)*100/8192)/10)*16+(((*-$a000)*100)/8192)%10)],"% full)"

		org		$bffa
		dta		a($a000)		;boot vector
.endif

		dta		$00				;do not init
		dta		$05				;boot disk/tape, boot cart
		dta		a(ExNop)		;init vector (no-op)

		.if CART==0
		opt		f-h+
		run		__loader
		.endif

;==========================================================================
; Internal pointers for relocatable version - only used during assembly
;

.ifdef ATBASIC_LO
		dta		a(_stNew.reset_entry)
		dta		a(ExecReset)
		dta		a(stLoadRun._loadflg)
		dta		a(stLoadRun.with_open_iocb)
		dta		a(putchar)
		dta		a(immediateModeReset)
		dta		a(IoPutNewline)
		dta		a(stLoadRun.loader_entry)
		dta		a(IoConvNumToHex.print_digit_pair)
.endif
		
		end
