//	VirtualDub - Video processing and capture application
//	System library component
//	Copyright (C) 1998-2012 Avery Lee, All Rights Reserved.
//
//	Beginning with 1.6.0, the VirtualDub system library is licensed
//	differently than the remainder of VirtualDub.  This particular file is
//	thus licensed as follows (the "zlib" license):
//
//	This software is provided 'as-is', without any express or implied
//	warranty.  In no event will the authors be held liable for any
//	damages arising from the use of this software.
//
//	Permission is granted to anyone to use this software for any purpose,
//	including commercial applications, and to alter it and redistribute it
//	freely, subject to the following restrictions:
//
//	1.	The origin of this software must not be misrepresented; you must
//		not claim that you wrote the original software. If you use this
//		software in a product, an acknowledgment in the product
//		documentation would be appreciated but is not required.
//	2.	Altered source versions must be plainly marked as such, and must
//		not be misrepresented as being the original software.
//	3.	This notice may not be removed or altered from any source
//		distribution.

#include <stdafx.h>
#include <windows.h>

#include <vd2/system/error.h>
#include <vd2/system/VDString.h>

/////////////////////////////////////////////////////////////////////////////

VDWin32Exception::VDWin32Exception(const char *format, uint32 err, ...)
	: mWin32Error(err)
{
	// format the base error message
	VDStringA errorMessage;

	va_list val;
	va_start(val, err);
	errorMessage.append_vsprintf(format, val);
	va_end(val);

	// force the system error message to come through if formatting fails
	if (errorMessage.empty())
		errorMessage = "%s";

	// Determine the position of the last %s, and escape everything else. This doesn't
	// track escaped % signs properly, but it works for the strings that we receive (and at
	// worst just produces a funny message).
	auto lastStrPos = errorMessage.find("%s");
	if (lastStrPos == VDStringA::npos) {
		// hmm, there is no %s substitution
		assign(errorMessage.c_str());
		return;
	}

	for(;;) {
		auto nextStrPos = errorMessage.find("%s", lastStrPos + 2);
		if (nextStrPos == VDStringA::npos)
			break;

		lastStrPos = nextStrPos;
	}

	// have OS convert error code to system error message
	VDStringA formatMessageError;
	LPSTR buffer = nullptr;
	if (!FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_ALLOCATE_BUFFER,
			0,
			err,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPSTR)&buffer,
			1,
			nullptr) || !buffer)
	{
		formatMessageError.sprintf("Unknown error %08X", err);
	} else if (buffer[0]) {
		// strip off any trailing newline on the error message
		const size_t l = strlen(buffer);

		if (l>1 && buffer[l-2] == '\r')
			buffer[l-2] = 0;
		else if (buffer[l-1] == '\n')
			buffer[l-1] = 0;
	}

	// splice the error message
	const char *systemErrorMessage = buffer ? buffer : formatMessageError.c_str();

	errorMessage.replace(lastStrPos, 2, systemErrorMessage, strlen(systemErrorMessage));
	assign(errorMessage.c_str());

	if (buffer)
		LocalFree(buffer);
}

VDWin32Exception::VDWin32Exception(const wchar_t *format, uint32 err, ...)
	: mWin32Error(err)
{
	// format the base error message
	VDStringW errorMessage;

	va_list val;
	va_start(val, err);
	errorMessage.append_vsprintf(format, val);
	va_end(val);

	// force the system error message to come through if formatting fails
	if (errorMessage.empty())
		errorMessage = L"%s";

	// Determine the position of the last %s, and escape everything else. This doesn't
	// track escaped % signs properly, but it works for the strings that we receive (and at
	// worst just produces a funny message).
	auto lastStrPos = errorMessage.find(L"%s");
	if (lastStrPos == VDStringA::npos) {
		// hmm, there is no %s substitution
		assign(errorMessage.c_str());
		return;
	}

	for(;;) {
		auto nextStrPos = errorMessage.find(L"%s", lastStrPos + 2);
		if (nextStrPos == VDStringA::npos)
			break;

		lastStrPos = nextStrPos;
	}

	// have OS convert error code to system error message
	VDStringW formatMessageError;
	LPWSTR buffer = nullptr;
	if (!FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_ALLOCATE_BUFFER,
			0,
			err,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPWSTR)&buffer,
			1,
			nullptr) || !buffer)
	{
		formatMessageError.sprintf(L"Unknown error %08X", err);
	} else if (buffer[0]) {
		// strip off any trailing newline on the error message
		const size_t l = wcslen(buffer);

		if (l>1 && buffer[l-2] == L'\r')
			buffer[l-2] = 0;
		else if (buffer[l-1] == L'\n')
			buffer[l-1] = 0;
	}

	// splice the error message
	const wchar_t *systemErrorMessage = buffer ? buffer : formatMessageError.c_str();

	errorMessage.replace(lastStrPos, 2, systemErrorMessage, wcslen(systemErrorMessage));
	assign(errorMessage.c_str());

	if (buffer)
		LocalFree(buffer);
}

/////////////////////////////////////////////////////////////////////////////

void VDPostException(VDExceptionPostContext context, const char *message, const char *title) {
	MessageBoxA(context, message, title, MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

void VDPostException(VDExceptionPostContext context, const wchar_t *message, const wchar_t *title) {
	MessageBoxW(context, message, title, MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}
