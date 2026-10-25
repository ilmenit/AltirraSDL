//	AltirraSDL - parent-console attachment for the Windows GUI-subsystem build.
//	See console_win32.h.

#include <stdafx.h>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdio>

#include "console_win32.h"

namespace {
	// A standard handle a parent set up for us (pipe, file or console).
	// GUI-subsystem processes started without one get NULL here.
	bool IsUsableStdHandle(DWORD which) {
		const HANDLE h = GetStdHandle(which);
		return h && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN;
	}
}

void ATAttachParentConsoleIfNeeded() {
	const bool haveOut = IsUsableStdHandle(STD_OUTPUT_HANDLE);
	const bool haveErr = IsUsableStdHandle(STD_ERROR_HANDLE);
	if (haveOut && haveErr)
		return;

	// Only borrow an existing console; never AllocConsole (that would open
	// the window this build exists to avoid).
	if (!AttachConsole(ATTACH_PARENT_PROCESS))
		return;

	FILE *f = nullptr;
	if (!haveOut) {
		freopen_s(&f, "CONOUT$", "w", stdout);
		setvbuf(stdout, nullptr, _IONBF, 0);
	}
	if (!haveErr) {
		freopen_s(&f, "CONOUT$", "w", stderr);
		setvbuf(stderr, nullptr, _IONBF, 0);
	}
}

#endif
