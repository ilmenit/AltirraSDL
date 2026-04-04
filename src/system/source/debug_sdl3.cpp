//	Altirra - Atari 800/800XL/5200 emulator
//	System library - debug output for non-Windows
//	Copyright (C) 2009-2024 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.

#include <stdafx.h>
#include <stdio.h>
#include <stdarg.h>

#include <vd2/system/vdtypes.h>
#include <vd2/system/debug.h>

VDAssertResult VDAssert(const char *exp, const char *file, int line) {
	fprintf(stderr, "%s(%d): Assert failed: %s\n", file, line, exp);
	fflush(stderr);
	// On Windows, __debugbreak() is a soft breakpoint that a debugger can
	// step past.  On Linux/GCC, __builtin_trap() sends SIGILL which
	// terminates unconditionally — even for benign asserts that the Windows
	// build routinely continues through.  Return kVDAssertContinue so the
	// assert is logged but execution continues, matching MSVC behaviour
	// when no debugger is attached.
	return kVDAssertContinue;
}

VDAssertResult VDAssertPtr(const char *exp, const char *file, int line) {
	fprintf(stderr, "%s(%d): Assert failed: %s is not a valid pointer\n", file, line, exp);
	fflush(stderr);
	return kVDAssertContinue;
}

void VDDebugPrint(const char *format, ...) {
	va_list val;
	va_start(val, format);
	vfprintf(stderr, format, val);
	va_end(val);
}

void VDProtectedAutoScopeICLWorkaround() {}

bool IsMMXState() { return false; }
void ClearMMXState() {}
void VDClearEvilCPUStates() {}
void VDPreCheckExternalCodeCall(const char *file, int line) {}
void VDPostCheckExternalCodeCall(const wchar_t *mpContext, const char *mpFile, int mLine) {}

namespace {
	IVDExternalCallTrap *g_pExCallTrap;
}

void VDSetExternalCallTrap(IVDExternalCallTrap *trap) {
	g_pExCallTrap = trap;
}
