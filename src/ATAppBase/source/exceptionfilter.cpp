//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2010 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include <stdafx.h>
#include <windows.h>

// dbghelp.h(1540): warning C4091: 'typedef ': ignored on left of '' when no variable is declared (compiling source file source\exceptionfilter.cpp)
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4091)
#endif
#include <dbghelp.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <vd2/system/w32assist.h>

extern HWND g_hwnd;

bool g_ATDumpWithFullHeap;
void (*g_pATExceptionPreFilter)(DWORD code, const EXCEPTION_POINTERS *);

void ATSetExceptionPreFilter(void (*fn)(DWORD code, const EXCEPTION_POINTERS *)) {
	g_pATExceptionPreFilter = fn;
}

int ATExceptionFilter(DWORD code, EXCEPTION_POINTERS *exp) {
	if (g_pATExceptionPreFilter)
		g_pATExceptionPreFilter(code, exp);

	if (IsDebuggerPresent()) {
		__debugbreak();
		return EXCEPTION_CONTINUE_SEARCH;
	}

	WCHAR buf[1024];
	bool dumpSucceeded = false;

	HMODULE hmodDbgHelp = VDLoadSystemLibraryW32("dbghelp");
	if (hmodDbgHelp) {
		typedef BOOL (WINAPI *tpMiniDumpWriteDump)(
			  HANDLE hProcess,
			  DWORD ProcessId,
			  HANDLE hFile,
			  MINIDUMP_TYPE DumpType,
			  PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
			  PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
			  PMINIDUMP_CALLBACK_INFORMATION CallbackParam
			);

		tpMiniDumpWriteDump pMiniDumpWriteDump = (tpMiniDumpWriteDump)GetProcAddress(hmodDbgHelp, "MiniDumpWriteDump");

		if (pMiniDumpWriteDump) {
			MINIDUMP_EXCEPTION_INFORMATION exInfo;

			exInfo.ThreadId = GetCurrentThreadId();
			exInfo.ExceptionPointers = exp;
			exInfo.ClientPointers = TRUE;

			static const WCHAR kFilename[] = L"AltirraCrash.mdmp";
			if (GetModuleFileName(NULL, buf, sizeof buf / sizeof buf[0])) {
				size_t len = wcslen(buf);

				while(len > 0) {
					WCHAR c = buf[len - 1];

					if (c == L':' || c == L'\\' || c == L'/')
						break;

					--len;
				}

				if (len < MAX_PATH - sizeof(kFilename)) {
					wcscpy(buf + len, kFilename);

					HANDLE hFile = CreateFile(buf, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

					if (hFile != INVALID_HANDLE_VALUE) {
						if (pMiniDumpWriteDump(
								GetCurrentProcess(),
								GetCurrentProcessId(),
								hFile,
								g_ATDumpWithFullHeap ? MiniDumpWithFullMemory : MiniDumpNormal,
								&exInfo,
								NULL,
								NULL))
						{
							dumpSucceeded = true;
						}

						CloseHandle(hFile);
					}
				}
			}
		}

		FreeLibrary(hmodDbgHelp);
	}

	if (g_hwnd) {
		EnableWindow(g_hwnd, FALSE);
		SetWindowLongPtr(g_hwnd, GWLP_WNDPROC, (LONG_PTR)(IsWindowUnicode(g_hwnd) ? DefWindowProcW : DefWindowProcA));
	}

	const WCHAR *writeResult = dumpSucceeded
		? L"A minidump file called AltirraCrash.mdmp has been written for diagnostic purposes."
		: L"(Could not write AltirraCrash.mdmp!)";

#ifdef VD_CPU_X86
	const uintptr pc = exp->ContextRecord->Eip;
#elif defined(VD_CPU_AMD64)
	const uintptr pc = exp->ContextRecord->Rip;
#elif defined(VD_CPU_ARM64)
	const uintptr pc = exp->ContextRecord->Pc;
#else
	#error Platform not supported
#endif


	wsprintf(buf, L"A fatal error has occurred in the emulator. %ls\n"
		L"\n"
#ifdef VD_CPU_X86
		L"Exception code: %08X  PC: %08X", writeResult, code, pc);
#else
		L"Exception code: %08X  PC: %08X`%08X", writeResult, code, (uint32)(pc >> 32), (uint32)pc);
#endif

	HMODULE hmod = VDGetLocalModuleHandleW32();
	MEMORY_BASIC_INFORMATION memInfo {};
	if (VirtualQuery((LPCVOID)pc, &memInfo, sizeof memInfo)) {
		if ((uintptr)memInfo.AllocationBase == (uintptr)hmod) {
			wsprintf(buf + wcslen(buf), L" (ExeBase+%08X)", pc - (uintptr)hmod);
		}
	}

	MessageBoxW(g_hwnd, buf, L"Altirra Program Failure", MB_OK | MB_ICONERROR);

	TerminateProcess(GetCurrentProcess(), code);
	return 0;
}

LONG WINAPI ATUnhandledExceptionFilter(EXCEPTION_POINTERS *exp) {
	return (LONG)ATExceptionFilter(exp->ExceptionRecord->ExceptionCode, exp);
}

void ATExceptionFilterSetFullHeapDump(bool enabled) {
	g_ATDumpWithFullHeap = enabled;
}
