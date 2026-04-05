// Compatibility shim: provides POSIX/GNU string functions missing from MSVC.
// Force-included on MSVC Windows builds for Altirra targets (not third-party deps).
#ifndef COMPAT_POSIX_STRING_H
#define COMPAT_POSIX_STRING_H

#ifdef _MSC_VER

#include <string.h>
#include <ctype.h>

// strcasestr — case-insensitive substring search (GNU extension).
// MSVC has no equivalent; provide a portable implementation.
#ifdef __cplusplus
inline const char *strcasestr(const char *haystack, const char *needle) {
#else
static __inline const char *strcasestr(const char *haystack, const char *needle) {
#endif
	if (!needle[0])
		return haystack;
	for (; *haystack; ++haystack) {
		const char *h = haystack;
		const char *n = needle;
		while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
			++h;
			++n;
		}
		if (!*n)
			return haystack;
	}
	return (const char *)0;
}

#ifdef __cplusplus
// Non-const overload to match GNU signature (C++ only).
inline char *strcasestr(char *haystack, const char *needle) {
	return const_cast<char *>(strcasestr(static_cast<const char *>(haystack), needle));
}
#endif

#endif // _MSC_VER
#endif // COMPAT_POSIX_STRING_H
