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

#include <stdafx.h>
#include <stdio.h>
#include <stdarg.h>

#include <vd2/system/vdtypes.h>
#include <vd2/system/atomic.h>
#include <vd2/system/error.h>
#include <vd2/system/text.h>
#include <vd2/system/VDString.h>

struct VDException::StringHeader {
	VDAtomicInt mRefCount;
	bool mbHidden = false;

	// Following this are:
	// - wide message
	// - narrow message
	//
	// The reason for this mess:
	// - We need Unicode in order to support parameters formatted into the
	//   message, such as filenames. This applies even if we avoid localization
	//   and keep all exception messages in English.
	//
	// - We need a narrow string in order to support std::exception::what(),
	//   which is required to be const noexcept. (Allocate + terminate() on
	//   failure is unsportsmanlike.) This also precludes delayed formatting
	//   of stored tokens.
	//
	// - UTF-8 locale support is not quite there yet in the UCRT, there are
	//   still too many bugs where UTF-8 code points >2 chars fail.
	//
	// - We have a lot of existing code using narrow strings.
	//
	// Therefore:
	// - We need _both_ the narrow and wide message on construction.
	// - We don't want to force both narrow and wide strings to be supplied,
	//   so the static literal optimization is ditched.
};

VDException::VDException(const VDException& err) noexcept {
	operator=(err);
}

VDException::VDException(VDException&& err) noexcept {
	operator=(std::move(err));
}

VDException::VDException(const char *s) {
	assign(s);
}

VDException::VDException(const wchar_t *s) {
	assign(s);
}

VDException::~VDException() {
	clear();
}

VDException& VDException::operator=(const VDException& err) noexcept {
	if (mpBuffer != err.mpBuffer) {
		clear();

		mpBuffer = err.mpBuffer;

		if (mpBuffer)
			++mpBuffer->mRefCount;
	}

	mpMessage = err.mpMessage;
	mpMessageW = err.mpMessageW;

	return *this;
}

VDException& VDException::operator=(VDException&& err) noexcept {
	if (&err != this) {
		clear();

		mpBuffer = err.mpBuffer;
		mpMessage = err.mpMessage;
		mpMessageW = err.mpMessageW;

		err.mpBuffer = nullptr;
		err.mpMessage = nullptr;
		err.mpMessageW = nullptr;
	}

	return *this;
}

void VDException::clear() noexcept {
	if (mpBuffer) {
		if (!--mpBuffer->mRefCount)
			free(mpBuffer);

		mpBuffer = nullptr;
	}

	mpMessage = nullptr;
	mpMessageW = nullptr;
}

void VDException::assign(const char *s) {
	clear();

	if (!s || !*s)
		return;

	const size_t len = strlen(s);
	char *buf = Alloc(len);

	if (buf) {
		memcpy(buf, s, len + 1);
		MakeWide();
	}
}

void VDException::assign(const wchar_t *s) {
	clear();

	if (!s || !*s)
		return;

	const size_t len = wcslen(s);
	wchar_t *buf = AllocWide(len);

	if (buf) {
		memcpy(buf, s, sizeof(wchar_t) * (len + 1));
		MakeNarrow();
	}
}

void VDException::setf(const char *f, ...) {
	va_list val;

	va_start(val, f);
	vsetf(f,val);
	va_end(val);
}

void VDException::wsetf(const wchar_t *f, ...) {
	va_list val;

	va_start(val, f);
	vwsetf(f,val);
	va_end(val);
}

void VDException::vsetf(const char *f, va_list val) {
	char *buf = Alloc(256);
	if (!buf)
		return;

	int len = vsnprintf(buf, 256, f, val);
	if (len == 0) {
		// don't keep empty strings
		clear();

		mpMessage = "";
		mpMessageW = L"";
		return;
	}

	if (len >= 256) {
		buf = Alloc(len + 1);
		if (!buf)
			len = -1;
		else
			len = vsnprintf(buf, len + 1, f, val);
	}

	if (len < 0) {
		clear();

		setf("<%s>", f);
	} else {
		MakeWide();
	}
}

void VDException::vwsetf(const wchar_t *f, va_list val) {
	wchar_t *buf = AllocWide(256);
	if (!buf)
		return;

	int len = vswprintf(buf, 256, f, val);
	if (len == 0) {
		// don't keep empty strings
		clear();

		mpMessage = "";
		mpMessageW = L"";
		return;
	}

	if (len >= 256) {
		buf = AllocWide(len + 1);
		if (!buf)
			len = -1;
		else
			len = vswprintf(buf, len + 1, f, val);
	}

	if (len < 0) {
		clear();

		wsetf(L"<%ls>", f);
	} else {
		MakeNarrow();
	}
}

void VDException::post(VDExceptionPostContext hWndParent, const char *title) const noexcept {
	if (!visible())
		return;

	VDPostException(hWndParent, c_str(), title);
}

const char *VDException::c_str() const noexcept {
	return mpMessage ? mpMessage : "";
}

const wchar_t *VDException::wc_str() const noexcept {
	return mpMessageW ? mpMessageW : L"";
}

void VDException::set_hidden() {
	mpBuffer->mbHidden = true;
}

bool VDException::visible() const noexcept {
	return mpBuffer && !mpBuffer->mbHidden;
}

const char *VDException::what() const noexcept {
	return c_str();
}

char *VDException::Alloc(size_t len) {
	// This is a legacy path where ASCII strings are passed, so we assume a 1:1
	// conversion to wchar.

	Alloc(len, len);

	return const_cast<char *>(mpMessage);
}

wchar_t *VDException::AllocWide(size_t len) {
	Alloc(len, len);

	return const_cast<wchar_t *>(mpMessageW);
}

void VDException::Alloc(size_t narrowLen, size_t wideLen) {
	clear();

	const size_t messageBytes = sizeof(wchar_t)*(wideLen + 1) + (narrowLen + 1);
	void *p = malloc(sizeof(StringHeader) + messageBytes);
	if (!p)
		return;

	mpBuffer = new(p) StringHeader;
	mpBuffer->mRefCount = 1;

	mpMessageW = (const wchar_t *)(mpBuffer + 1);
	mpMessage = (const char *)(mpMessageW + wideLen + 1);

	memset(mpBuffer + 1, 0, messageBytes);
}

void VDException::MakeNarrow() {
	char *narrow = const_cast<char *>(mpMessage);
	const wchar_t *wide = mpMessageW;

	for(;;) {
		const wchar_t c = *wide++;
		*narrow++ = c < 0x80 ? (char)c : '?';

		if (!c)
			break;
	}
}

void VDException::MakeWide() {
	const char *narrow = mpMessage;
	wchar_t *wide = const_cast<wchar_t *>(mpMessageW);

	for(;;) {
		const char c = *narrow++;
		*wide++ = (wchar_t)c;

		if (!c)
			break;
	}
}

////////////////////////////////////////////////////////////////////////////////

VDAllocationFailedException::VDAllocationFailedException() {
	assign("Out of memory");
}

VDAllocationFailedException::VDAllocationFailedException(size_t requestedSize) {
	setf("Out of memory (unable to allocate %llu bytes)", (unsigned long long)requestedSize);
}

////////////////////////////////////////////////////////////////////////////////

VDUserCancelException::VDUserCancelException() {
	assign("Operation cancelled by user");
	set_hidden();
}

////////////////////////////////////////////////////////////////////////////////

void VDPostCurrentException(VDExceptionPostContext context, const char *title) {
	try {
		throw;
	} catch(const VDException& ex) {
		VDPostException(context, ex.wc_str(), VDTextAToW(title).c_str());
	} catch(const std::exception& stdex) {
		VDPostException(context, stdex.what(), title);
	} catch(...) {
		VDPostException(context, "An unknown exception occurred.", title);
	}
}

////////////////////////////////////////////////////////////////////////////////

VDNOINLINE void VDRaiseInternalFailure(const char *context) {
	[[maybe_unused]] const char *volatile contextptr = context;

	std::terminate();
}
