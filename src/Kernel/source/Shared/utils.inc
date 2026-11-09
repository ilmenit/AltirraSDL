;	Altirra - Atari 800/800XL/5200 emulator
;	Copyright (C) 2026 Avery Lee
;
;	Copying and distribution of this file, with or without modification,
;	are permitted in any medium without royalty provided the copyright
;	notice and this notice are preserved.  This file is offered as-is,
;	without any warranty.

.ifndef f_UTILS_INC
.def f_UTILS_INC

;Workaround for MADS 2.1.8 requiring PEA #imm16 and misassembling PEA abs as
;PER, but MADS 2.1.9 requiring PEA abs (which is the standard syntax).
.macro _PEA
		dta		$F4,a(:1)
.endm

.endif
