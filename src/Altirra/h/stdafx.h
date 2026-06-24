//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008 Avery Lee
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

#pragma once

#ifdef _WIN32
#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#endif

#define _SCL_SECURE_NO_WARNINGS

// Windows SDK 10.0.26100 UCRT <wchar.h> contains inline functions that
// unconditionally reference __m128i/__m256i SIMD types and the _mm_* /
// _mm256_* intrinsic family. On some MSVC + preset combinations the
// include chain reaches <wchar.h> (via <tchar.h>, <math.h>, <string.h>)
// before the intrinsic declarations are visible, producing hundreds of
// "undeclared identifier" errors. Pulling <immintrin.h> first (the
// umbrella header that declares both __m128i and __m256i) forces the
// declarations to be available. <intrin.h> alone is not sufficient —
// on MSVC 14.44.35207 it does not transitively declare __m256i before
// the SDK's <wchar.h> is reached. This is a no-op on GCC/Clang (which
// handle the same types via their own compiler builtins) — guarded on
// _MSC_VER.
#if defined(_MSC_VER)
#include <intrin.h>      // _ReturnAddress etc. (needed by <ppltasks.h>)
// AltirraSDL: fork change for the Windows-on-ARM64 build. The <immintrin.h>
// <wchar.h> workaround is x86/x64-only — the SDK's SIMD inlines use
// __m128i/__m256i (x86 vector types). On ARM64 those don't apply and
// <immintrin.h> rejects the target (error C1189), so the include is guarded to
// x86/x64. Upstream includes it unconditionally for MSVC. Preserve this guard
// when re-syncing from upstream.
#if defined(_M_IX86) || defined(_M_X64)
#include <immintrin.h>   // __m128i / __m256i (needed by <wchar.h> on x86/x64)
#endif
#endif

#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <tchar.h>
#include <vd2/system/error.h>
#include <vd2/system/fraction.h>
#include <vd2/system/function.h>
#include <vd2/system/linearalloc.h>
#include <vd2/system/refcount.h>
#include <vd2/system/time.h>
#include <vd2/system/unknown.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>
#include <vd2/system/vectors.h>
#include <vd2/Kasumi/pixmap.h>
#include <at/atcore/device.h>
#include <at/atcore/enumparse.h>
#include <atomic>
#include <deque>
#include <iterator>
#include <utility>
#include <optional>

struct IUnknown;