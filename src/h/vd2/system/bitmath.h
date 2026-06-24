//	VirtualDub - Video processing and capture application
//	System library component
//	Copyright (C) 1998-2007 Avery Lee, All Rights Reserved.
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

#ifndef f_VD2_SYSTEM_BITMATH_H
#define f_VD2_SYSTEM_BITMATH_H

#ifdef _MSC_VER
	#pragma once
#endif

#ifndef f_VD2_SYSTEM_VDTYPES_H
	#include <vd2/system/vdtypes.h>
#endif

#ifdef VD_COMPILER_MSVC
	#include <intrin.h>
#elif defined(VD_COMPILER_CLANG_OR_GCC)
	#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
		#include <x86intrin.h>
	#elif defined(VD_CPU_ARM64)
		#include <arm_acle.h>
	#endif
#elif defined(VD_CPU_ARM64)
	#include <arm_acle.h>
#endif

inline int VDCountBits8(uint8 v) {
	v -= (v >> 1) & 0x55;
	v = ((v & 0xcc) >> 2) + (v & 0x33);
	return (int)((v + (v >> 4)) & 15);
}

int VDCountBits(uint32 v);
int VDFindLowestSetBit(uint32 v);
int VDFindHighestSetBit(uint32 v);
uint32 VDCeilToPow2(uint32 v);

union VDFloatAsInt {
	float f;
	sint32 i;
};

union VDIntAsFloat {
	sint32 i;
	float f;
};

inline sint32 VDGetFloatAsInt(float f) {
	const VDFloatAsInt conv = { f };
	return conv.i;
}

inline float VDGetIntAsFloat(sint32 i) {
	const VDIntAsFloat conv = { i };
	return conv.f;
}

///////////////////////////////////////////////////////////////////////////////

inline int VDFindLowestSetBit(uint32 v) {
	#ifdef VD_COMPILER_MSVC
		unsigned long index;
		return _BitScanForward(&index, v) ? index : 32;
	#else
		return v ? __builtin_ctz(v) : 32;
	#endif
}

inline int VDFindHighestSetBit(uint32 v) {
	#ifdef VD_COMPILER_MSVC
		unsigned long index;
		return _BitScanReverse(&index, v) ? index : -1;
	#else
		return v ? (31 - __builtin_clz(v)) : -1;
	#endif
}

inline int VDFindLowestSetBitFast(uint32 v) {
	#if defined(VD_CPU_X86) || defined(VD_CPU_AMD64)
		#ifdef VD_COMPILER_MSVC
			return (int)_tzcnt_u32(v);
		#else
			return __builtin_ctz(v);
		#endif
	#elif defined(VD_COMPILER_MSVC)
		// AltirraSDL: fork addition for the Windows-on-ARM64 build. Upstream
		// only handles MSVC under the x86/AMD64 branch above and otherwise
		// falls through to __builtin_ctz, which MSVC does not provide on
		// non-x86 targets. Use _BitScanForward instead, matching
		// VDFindLowestSetBit() above. Preserve when re-syncing from upstream.
		unsigned long index;
		_BitScanForward(&index, v);
		return (int)index;
	#else
		return __builtin_ctz(v);
	#endif
}

inline int VDFindHighestSetBitFast(uint32 v) {
	#ifdef VD_COMPILER_MSVC
		unsigned long index;
		_BitScanReverse(&index, v);
		return index;
	#else
		return 31 - __builtin_clz(v);
	#endif
}

inline int VDFindLowestSetBitFast64(uint64 v) {
	#if defined(VD_CPU_AMD64) && defined(VD_COMPILER_MSVC)
		return (int)_tzcnt_u64(v);
	#elif defined(VD_CPU_AMD64)
		return __builtin_ctzll(v);
	#elif defined(VD_CPU_ARM64)
		#ifdef VD_COMPILER_MSVC
			return _CountTrailingZeros64(v);
		#else
			return __builtin_ctzll(v);
		#endif
	#else
		if ((uint32)v)
			return VDFindLowestSetBitFast((uint32)v);
		else
			return 32 + VDFindLowestSetBitFast((uint32)(v >> 32));
	#endif
}

#endif
