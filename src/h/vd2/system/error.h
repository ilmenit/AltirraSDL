//	VirtualDub - Video processing and capture application
//	System library component
//	Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
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

#ifndef f_VD2_ERROR_H
#define f_VD2_ERROR_H

#ifdef _MSC_VER
	#pragma once
#endif

#include <source_location>
#include <exception>
#include <type_traits>
#include <vd2/system/vdtypes.h>

class VDException;

#if defined(VD_OS_WINDOWS) || defined(_WIN32)
using VDExceptionPostContext = struct HWND__ *;
#else
using VDExceptionPostContext = void *;
#endif

template<typename T>
concept VDPrintfCompatible = (std::is_arithmetic_v<std::decay_t<T>> || std::is_pointer_v<std::decay_t<T>> || std::is_enum_v<std::decay_t<T>>);

///////////////////////////////////////////////////////////////////////////
//	VDException
//
class VDException : public std::exception {
protected:
	struct StringHeader;

	StringHeader *mpBuffer = nullptr;
	const char *mpMessage = nullptr;
	const wchar_t *mpMessageW = nullptr;

public:
	VDException() = default;
	VDException(const VDException& err) noexcept;
	VDException(VDException&& err) noexcept;
	VDException(const char *s);
	VDException(const wchar_t *s);

	template<typename... Args>
	VDException(const char *format, Args&& ...args) {
		constexpr bool validParams = (VDPrintfCompatible<Args> && ...);

		if constexpr(validParams)
			setf(format, std::forward<Args>(args)...);
		else
			static_assert(!validParams, "Unsupported parameter types passed");
	}

	template<typename... Args>
	VDException(const wchar_t *format, Args&& ...args) {
		constexpr bool validParams = (VDPrintfCompatible<Args> && ...);

		if constexpr(validParams)
			wsetf(format, std::forward<Args>(args)...);
		else
			static_assert(!validParams, "Unsupported parameter types passed");
	}

	~VDException();

	VDException& operator=(const VDException&) noexcept;
	VDException& operator=(VDException&&) noexcept;

	void clear() noexcept;
	void assign(const char *s);
	void assign(const wchar_t *s);

	void setf(const char *f, ...);
	void wsetf(const wchar_t *f, ...);

	void vsetf(const char *f, va_list val);
	void vwsetf(const wchar_t *f, va_list val);
	void post(VDExceptionPostContext context, const char *title) const noexcept;
	const char *c_str() const noexcept;
	const wchar_t *wc_str() const noexcept;

	// Return true if the object contains an exception. Note that an empty
	// string isn't the same (it corresponds to a user cancel).
	bool empty() const { return !mpMessage; }

	// An exception is visible if it is meant to be displayed. Control flow
	// forcing exceptions like VDUserCancelException aren't. An empty
	// exception object is not visible.
	void set_hidden();
	bool visible() const noexcept;

	const char *what() const noexcept override;

protected:
	char *Alloc(size_t len);
	wchar_t *AllocWide(size_t len);
	void Alloc(size_t narrowLen, size_t wideLen);
	void MakeNarrow();
	void MakeWide();
};

class VDAllocationFailedException : public VDException {
public:
	VDAllocationFailedException();
	VDAllocationFailedException(size_t attemptedSize);
};

#if defined(VD_OS_WINDOWS) || defined(_WIN32)
class VDWin32Exception : public VDException {
public:
	VDWin32Exception(const char *format, uint32 err, ...);
	VDWin32Exception(const wchar_t *format, uint32 err, ...);

	uint32 GetWin32Error() const { return mWin32Error; }

protected:
	const uint32 mWin32Error;
};
#endif

class VDUserCancelException : public VDException {
public:
	VDUserCancelException();
};

////////////////////////////////////////////////////////////////////////////////

using MyError = VDException;
using MyMemoryError = VDAllocationFailedException;
#if defined(VD_OS_WINDOWS) || defined(_WIN32)
using MyWin32Error = VDWin32Exception;
#endif
using MyUserAbortError = VDUserCancelException;

////////////////////////////////////////////////////////////////////////////////

void VDPostException(VDExceptionPostContext context, const char *message, const char *title);
void VDPostException(VDExceptionPostContext context, const wchar_t *message, const wchar_t *title);
void VDPostCurrentException(VDExceptionPostContext context, const char *title);

[[noreturn]] void VDRaiseInternalFailure(const char *context = std::source_location::current().function_name());

#endif
