//	Altirra - Atari 800/800XL/5200 emulator
//	System library - text/encoding functions for non-Windows
//
//	On Linux, wchar_t is 4 bytes (UTF-32). All conversions go through
//	UTF-8 at the OS boundary.

#include <stdafx.h>
#include <stdarg.h>
#include <vector>
#include <algorithm>

#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstdc.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/text.h>
#include <vd2/system/VDString.h>

// -------------------------------------------------------------------------
// UTF-8 / UTF-32 helpers
// -------------------------------------------------------------------------

// Encode a single Unicode code point to UTF-8. Returns bytes written (1-4),
// or 0 if dst is too small or cp is invalid.
static int EncodeUTF8(uint8 *dst, size_t dstLen, uint32 cp) {
	if (cp < 0x80) {
		if (dstLen < 1) return 0;
		dst[0] = (uint8)cp;
		return 1;
	} else if (cp < 0x800) {
		if (dstLen < 2) return 0;
		dst[0] = (uint8)(0xC0 | (cp >> 6));
		dst[1] = (uint8)(0x80 | (cp & 0x3F));
		return 2;
	} else if (cp < 0x10000) {
		if (dstLen < 3) return 0;
		dst[0] = (uint8)(0xE0 | (cp >> 12));
		dst[1] = (uint8)(0x80 | ((cp >> 6) & 0x3F));
		dst[2] = (uint8)(0x80 | (cp & 0x3F));
		return 3;
	} else if (cp < 0x110000) {
		if (dstLen < 4) return 0;
		dst[0] = (uint8)(0xF0 | (cp >> 18));
		dst[1] = (uint8)(0x80 | ((cp >> 12) & 0x3F));
		dst[2] = (uint8)(0x80 | ((cp >> 6)  & 0x3F));
		dst[3] = (uint8)(0x80 | (cp & 0x3F));
		return 4;
	}
	return 0; // invalid
}

// Decode one UTF-8 sequence from src. Returns code point, advances *src.
// Returns U+FFFD on invalid sequence.
static uint32 DecodeUTF8(const uint8 **src, const uint8 *end) {
	uint8 b0 = **src;
	(*src)++;

	if (b0 < 0x80)
		return b0;

	uint32 cp;
	int extra;
	if ((b0 & 0xE0) == 0xC0) { cp = b0 & 0x1F; extra = 1; }
	else if ((b0 & 0xF0) == 0xE0) { cp = b0 & 0x0F; extra = 2; }
	else if ((b0 & 0xF8) == 0xF0) { cp = b0 & 0x07; extra = 3; }
	else return 0xFFFD;

	for (int i = 0; i < extra; ++i) {
		if (*src >= end || (**src & 0xC0) != 0x80)
			return 0xFFFD;
		cp = (cp << 6) | (**src & 0x3F);
		(*src)++;
	}
	return cp;
}

// -------------------------------------------------------------------------
// VDCodePointToU8
// -------------------------------------------------------------------------

size_t VDCodePointToU8(uint8 *dst, size_t dstLen, const wchar_t *src, size_t srcLen, size_t& srcElementsUsed) {
	size_t total = 0;
	size_t used = 0;

	while (used < srcLen) {
		uint32 cp = (uint32)src[used];
		if (cp == 0 && srcLen == (size_t)-1)
			break;

		uint8 tmp[4];
		int n = EncodeUTF8(tmp, dstLen - total, cp);
		if (n == 0) break;

		if (dst)
			memcpy(dst + total, tmp, n);
		total += n;
		++used;
	}

	srcElementsUsed = used;
	return total;
}

// -------------------------------------------------------------------------
// VDTextWToU8 / VDTextU8ToW
// -------------------------------------------------------------------------

VDStringA VDTextWToU8(const VDStringSpanW& s) {
	return VDTextWToU8(s.data(), (int)s.size());
}

VDStringA VDTextWToU8(const wchar_t *src, int length) {
	if (!src) return VDStringA();

	int srcLen = (length < 0) ? (int)wcslen(src) : length;

	// Measure first
	size_t needed = 0;
	for (int i = 0; i < srcLen; ++i) {
		uint8 tmp[4];
		int n = EncodeUTF8(tmp, sizeof(tmp), (uint32)src[i]);
		needed += (n > 0) ? n : 3; // U+FFFD fallback = 3 bytes
	}

	VDStringA result;
	result.resize(needed);
	uint8 *dst = (uint8 *)result.data();
	size_t pos = 0;

	for (int i = 0; i < srcLen; ++i) {
		uint32 cp = (uint32)src[i];
		int n = EncodeUTF8(dst + pos, needed - pos, cp);
		if (n > 0)
			pos += n;
		else {
			// U+FFFD
			dst[pos++] = 0xEF;
			dst[pos++] = 0xBF;
			dst[pos++] = 0xBD;
		}
	}
	result.resize(pos);
	return result;
}

VDStringW VDTextU8ToW(const VDStringSpanA& s) {
	return VDTextU8ToW(s.data(), (int)s.size());
}

VDStringW VDTextU8ToW(const char *src, int length) {
	if (!src) return VDStringW();

	const uint8 *p   = (const uint8 *)src;
	const uint8 *end = p + ((length < 0) ? strlen(src) : (size_t)length);

	VDStringW result;
	while (p < end) {
		uint32 cp = DecodeUTF8(&p, end);
		result += (wchar_t)cp;
	}
	return result;
}

// -------------------------------------------------------------------------
// VDTextWToA / VDTextAToW (ACP → ASCII fallback; lossy for non-ASCII)
// -------------------------------------------------------------------------

int VDTextWToA(char *dst, int max_dst, const wchar_t *src, int max_src) {
	if (!dst || !src || max_dst <= 0) return 0;

	int srcLen = (max_src < 0) ? (int)wcslen(src) : max_src;
	int written = 0;

	for (int i = 0; i < srcLen && written < max_dst - 1; ++i) {
		wchar_t wc = src[i];
		if (wc < 0x80)
			dst[written++] = (char)wc;
		else
			dst[written++] = '?'; // lossy
	}
	dst[written] = '\0';
	return max_src < 0 ? written : written;
}

int VDTextAToW(wchar_t *dst, int max_dst, const char *src, int max_src) {
	if (!dst || !src || max_dst <= 0) return 0;

	int srcLen = (max_src < 0) ? (int)strlen(src) : max_src;
	int written = 0;

	for (int i = 0; i < srcLen && written < max_dst - 1; ++i)
		dst[written++] = (wchar_t)(uint8)src[i];

	dst[written] = L'\0';
	return max_src < 0 ? written : written;
}

VDStringA VDTextWToA(const VDStringW& sw) {
	return VDTextWToA(sw.data(), (int)sw.length());
}

VDStringA VDTextWToA(const wchar_t *src, int srclen) {
	if (!src) return VDStringA();

	int len = (srclen < 0) ? (int)wcslen(src) : srclen;
	VDStringA result;
	result.resize(len);
	for (int i = 0; i < len; ++i) {
		wchar_t wc = src[i];
		result[i] = (wc < 0x80) ? (char)wc : '?';
	}
	return result;
}

VDStringW VDTextAToW(const char *src, int length) {
	if (!src) return VDStringW();

	int len = (length < 0) ? (int)strlen(src) : length;
	VDStringW result;
	result.resize(len);
	for (int i = 0; i < len; ++i)
		result[i] = (wchar_t)(uint8)src[i];
	return result;
}

VDStringW VDTextAToW(const VDStringA& sw) {
	return VDTextAToW(sw.data(), (int)sw.length());
}

int VDTextWToALength(const wchar_t *s, int length) {
	if (!s) return 0;
	return (length < 0) ? (int)wcslen(s) : length;
}

int VDTextAToWLength(const char *s, int length) {
	if (!s) return 0;
	return (length < 0) ? (int)strlen(s) : length;
}

// -------------------------------------------------------------------------
// VDTextContainsSubstringMatchByLocale — simple case-insensitive substring
// -------------------------------------------------------------------------

bool VDTextContainsSubstringMatchByLocale(VDStringSpanW sourceString, VDStringSpanW searchString) {
	if (searchString.empty()) return true;
	if (sourceString.size() < searchString.size()) return false;

	// Simple locale-agnostic case fold
	for (size_t i = 0; i <= sourceString.size() - searchString.size(); ++i) {
		bool match = true;
		for (size_t j = 0; j < searchString.size(); ++j) {
			wchar_t a = towlower(sourceString[i + j]);
			wchar_t b = towlower(searchString[j]);
			if (a != b) { match = false; break; }
		}
		if (match) return true;
	}
	return false;
}

// -------------------------------------------------------------------------
// Wide printf helpers
// -------------------------------------------------------------------------

VDStringW VDaswprintf(const wchar_t *format, int args, const void *const *argv) {
	// This function takes a pre-built argv array — provide a minimal fallback.
	// In practice it's only called via VDswprintf which goes through va_list.
	return VDStringW(format);
}

VDStringW VDvswprintf(const wchar_t *format, int /*args*/, va_list val) {
	wchar_t buf[4096];
	vswprintf(buf, sizeof(buf)/sizeof(buf[0]), format, val);
	buf[4095] = L'\0';
	return VDStringW(buf);
}

VDStringW VDswprintf(const wchar_t *format, int args, ...) {
	va_list val;
	va_start(val, args);
	VDStringW result = VDvswprintf(format, args, val);
	va_end(val);
	return result;
}
