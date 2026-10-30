; Altirra BASIC
; Copyright (C) 2026 Avery Lee, All Rights Reserved.
;
; Copying and distribution of this file, with or without modification,
; are permitted in any medium without royalty provided the copyright
; notice and this notice are preserved.  This file is offered as-is,
; without any warranty.

.macro _ATBASIC_VERSION
		dta		'1.59'
.endm

.macro _MSG_BANNER
		dta		c'Altirra 8K BASIC '
		_ATBASIC_VERSION
.endm
