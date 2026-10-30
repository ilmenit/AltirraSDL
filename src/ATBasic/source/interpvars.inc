; Altirra BASIC
; Copyright (C) 2026 Avery Lee, All Rights Reserved.
;
; Copying and distribution of this file, with or without modification,
; are permitted in any medium without royalty provided the copyright
; notice and this notice are preserved.  This file is offered as-is,
; without any warranty.

;===========================================================================

; Zero page variables
;
; We try to be sort of compatible with Atari BASIC here, supporting all
; public variables and trying to support some unofficial usage as well.
;
; Test cases:
;	QUADRATO.BAS
;	- Uses $B0-B3 from USR() routine

		org		$0080
		opt		o-
argstk	equ		*
lomem	dta		a(0)		;$0080 (compat) from lomem; argument/operator stack
vntp	dta		a(0)		;$0082 (compat - loaded) variable name table pointer
vntd	dta		a(0)		;$0084 (compat - loaded) variable name table end
vvtp	dta		a(0)		;$0086 (compat - loaded) variable value table pointer
stmtab	dta		a(0)		;$0088 (compat - loaded) statement table pointer
stmcur	dta		a(0)		;$008A (compat - loaded) current statement pointer
starp	dta		a(0)		;$008C (compat - loaded) string and array table
runstk	dta		a(0)		;$008E (compat) runtime stack pointer
memtop2	dta		a(0)		;$0090 (compat) top of BASIC memory

exLineOffset	dta		0		;offset within current line being executed
exLineOffsetNxt	dta		0		;offset of next statement
exLineEnd		dta		0		;offset of end of current line
exTrapLine		dta		a(0)	;TRAP line
exFloatStk		dta		0		;bit 7 set if stack is floating (line numbers)
parPtrSav:					;[6 bytes] VNTD/VVTP/STMTAB save area for parser rollback
opsp		dta		0		;operand stack pointer offset
argsp		dta		0		;argument stack pointer offset
expCommas	dta		0		;expression evaluator comma count
expFCommas	dta		0
expAsnCtx	dta		0		;flag - set if this is an assignment context for arrays
expType		dta		0		;bit 7 = 0 for numeric, 1 for string
varptr		dta		a(0)	;pointer to current variable
lvarptr		dta		a(0)	;lvar pointer for array assignment
parptr		dta		a(0)	;parsing state machine pointer
parout		dta		0		;parsing output idx
expCurPrec	dta		0		;expression evaluator current operator precedence
iocbexec	dta		0		;current immediate/deferred mode IOCB
iocbidx		dta		0		;current IOCB*16
iocbidx2	dta		0		;current IOCB (used to distinguish #0 and #16)
iterPtr		dta		a(0)	;pointer used for sequential name table indexing
ioPrintCol	dta		0		;IO: current PRINT column
ioTermSave	dta		0		;IO: String terminator byte save location
ioTermOff	dta		0		;IO: String terminator byte offset
argstk2		dta		a(0)	;Evaluator: Second argument stack pointer
dataLnEnd	dta		0		;current DATA statement line end
pmgbase		dta		0
pmgmode		dta		0
ioTermFlag	dta		0

		.if grColor!=$c8
		.error "Graphics color is at ",grColor," but must be at $C8 for PEEK(200) to work (see Space Station Multiplication.bas)"
		.endif

		.if *>$b7
		.error "Zero page overflow: ",*
		.endif

			org		$b7
dataln		dta		a(0)	;(compat - Mapping the Atari / ANALOG verifier) current DATA statement line


stopln	= $ba				;(compat - Atari BASIC manual): line number of error
		; $bb
		
;--------------------------------------------------------------------------
; $BC-BF are reserved as scratch space for use by the currently executing
; statement or by the parser. They must not be used by functions or library
; code.
;
			org		$bc
stScratch	dta		0
stScratch2	dta		0
stScratch3	dta		0
stScratch4	dta		0

printDngl	= stScratch		;set if the print statement is 'dangling' - no follow EOL
parStrType	= prefr0		;parsing string type: set if string exp, clear if numeric
parStBegin	= stScratch2	;parsing offset of statement begin (0 if none)

;--------------------------------------------------------------------------
; $C0-C1 are reserved as scratch space for use by the currently executing
; function.
;
funScratch1	= $c0
funScratch2	= $c1
;--------------------------------------------------------------------------
errno	= $c2
errsave	= $c3				;(compat - Atari BASIC manual): error number

			org		$c4
dataptr		dta		a(0)	;current DATA statement pointer
dataoff		dta		0		;current DATA statement offset
			dta		0		;(unused)
grColor		dta		0		;graphics color (must be at $C8 for Space Station Multiplication)
ptabw		dta		0		;(compat - Atari BASIC manual): tab width

.if ptabw != $C9
			.error	"PTABW is wrong"
.endif

.if * > $CB
.error "$CB-D1 are reserved"
.endif

;--------------------------------------------------------------------------
; $CB-D1 are reserved for use by annoying people that read Mapping The
; Atari.
;--------------------------------------------------------------------------
; Floating-point library vars
;
; $D2-D3 is used as an extension prefix to FR0; $D4-FF are used by the FP
; library, but can be reused outside of it.
;
prefr0	= fr0-2
a0		= fr0				;temporary pointer 0
a1		= fr0+2				;temporary pointer 1
a2		= fr0+4				;temporary pointer 2
a3		= fr0+6				;temporary pointer 3
a4		= fr0+8				;temporary pointer 4
a5		= fr0+10			;temporary pointer 5

degflg	= $fb				;(compat) degree/radian flag: 0 for radians, 6 for degrees

lbuff	equ		$0580
