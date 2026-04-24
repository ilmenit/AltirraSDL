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

#ifndef f_VD2_SYSTEM_MATH_H
#define f_VD2_SYSTEM_MATH_H

#include <math.h>
#include <vd2/system/vdtypes.h>

#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
	#ifdef _MSC_VER
		#include <intrin.h>
	#else
		#include <x86intrin.h>
	#endif
#elif defined(VD_CPU_ARM64)
	#include <arm_neon.h>
#endif

// Constants
namespace nsVDMath {
	static constexpr float	kfPi = 3.1415926535897932384626433832795f;
	static constexpr double	krPi = 3.1415926535897932384626433832795;
	static constexpr float	kfTwoPi = 6.283185307179586476925286766559f;
	static constexpr double	krTwoPi = 6.283185307179586476925286766559;
	static constexpr float	kfLn2 = 0.69314718055994530941723212145818f;
	static constexpr double	krLn2 = 0.69314718055994530941723212145818;
	static constexpr float	kfLn10 = 2.3025850929940456840179914546844f;
	static constexpr double	krLn10 = 2.3025850929940456840179914546844;
	static constexpr float	kfOneOverLn10 = 0.43429448190325182765112891891661f;
	static constexpr double	krOneOverLn10 = 0.43429448190325182765112891891661;
	static constexpr double	kfSqrt2 = 1.4142135623730950488016887242097f;
	static constexpr double	krSqrt2 = 1.4142135623730950488016887242097;
};

///////////////////////////////////////////////////////////////////////////
// Integer clamping functions
//
#ifdef _M_IX86
	inline uint32 VDClampToUint32(uint64 v) {
		return v >= 0x100000000UL ? 0xFFFFFFFFUL : (uint32)v;
	}

	inline uint32 VDClampToUint32(sint64 v) {
		union U {
			__int64 v64;
			struct {
				unsigned lo;
				int hi;
			} v32;
		};

		return ((U *)&v)->v32.hi ? ~(((U *)&v)->v32.hi >> 31) : ((U *)&v)->v32.lo;
	}

	inline uint32 VDClampToUint32(sint32 v) {
		return v < 0 ? 0 : (uint32)v;
	}
#else
	inline uint32 VDClampToUint32(sint64 v) {
		uint32 r = (uint32)v;
		return r == v ? r : (uint32)~(sint32)(v>>63);
	}
#endif

inline sint32 VDClampToSint32(uint32 v) {
	return (v | ((sint32)v >> 31)) & 0x7FFFFFFF;
}

inline sint32 VDClampToSint32(sint64 v) {
	sint32 r = (sint32)v;
	return r == v ? r : (sint32)(v >> 63) ^ 0x7FFFFFFF;
}

inline uint16 VDClampToUint16(uint32 v) {
	if (v > 0xffff)
		v = 0xffff;
	return (uint16)v;
}

///////////////////////////////////////////////////////////////////////////
// Absolute value functions
inline sint64 VDAbs64(sint64 v) {
	return v<0 ? -v : v;
}

inline ptrdiff_t VDAbsPtrdiff(ptrdiff_t v) {
	return v<0 ? -v : v;
}

// Rounding functions
//
// Round a double to an int or a long.  Behavior is not specified at
// int(y)+0.5, if x is NaN or Inf, or if x is out of range.

int VDRoundToInt(float x);
int VDRoundToInt(double x);
sint32 VDRoundToInt32(float x);
sint32 VDRoundToInt32(double x);
sint64 VDRoundToInt64(float x);
sint64 VDRoundToInt64(double x);

inline sint32 VDRoundToIntFast(float x) {
#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
	return (int)_mm_cvtss_si32(_mm_set_ss(x));
#elif defined(VD_CPU_ARM64)
	return (int)vcvtns_s32_f32(x);
#else
	return (int)::lrintf(x);
#endif
}

inline sint32 VDRoundToIntFastFullRange(double x) {
#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
	return (int)_mm_cvtsd_si32(_mm_set_sd(x));
#elif defined(VD_CPU_ARM64)
	return (int)vcvtnd_s64_f64(x);
#else
	return (int)::lrint(x);
#endif
}

inline float VDRoundFast(float x) {
#if defined(VD_CPU_ARM64)
	return vrndns_f32(x);
#else
	return x >= -0x1p24f && x <= 0x1p24f ? (float)VDRoundToIntFast(x) : x;
#endif
}

////////////////////////////////////////////////////////////////////////////////

inline constexpr int VDFloorToInt(double x) {
	int estimate = (int)x;

	return x < (double)estimate ? estimate - 1 : estimate;
}

inline constexpr sint32 VDFloorToInt32(float x) {
	sint32 estimate = (sint32)x;

	return x < (float)estimate ? estimate - 1 : estimate;
}

inline constexpr sint32 VDFloorToInt32(double x) {
	sint32 estimate = (sint32)x;

	return x < (double)estimate ? estimate - 1 : estimate;
}

inline constexpr sint64 VDFloorToInt64(double x) {
	sint64 estimate = (sint64)x;

	return x < (double)estimate ? estimate - 1 : estimate;
}

////////////////////////////////////////////////////////////////////////////////

inline constexpr int VDCeilToInt(double x) {
	int estimate = (int)x;

	return (double)estimate < x ? estimate+1 : estimate;
}

inline constexpr sint32 VDCeilToInt32(float x) {
	sint64 estimate = (sint32)x;

	return (float)estimate < x ? estimate+1 : estimate;
}

inline constexpr sint32 VDCeilToInt32(double x) {
	sint64 estimate = (sint32)x;

	return (double)estimate < x ? estimate+1 : estimate;
}

inline constexpr sint64 VDCeilToInt64(double x) {
	sint64 estimate = (sint64)x;

	return (double)estimate < x ? estimate+1 : estimate;
}


///////////////////////////////////////////////////////////////////////////
/// Convert a value from [-~1..1] to [-32768, 32767] with clamping.
inline sint16 VDClampedRoundFixedToInt16Fast(float x) {
	sint32 v = VDRoundToIntFast(x * 32767.0f);

	if (v != (sint16)v)
		v = (~v >> 31) ^ 0x8000;

	return (sint16)v;
}

/// Convert a value from [0..1] to [0..255] with clamping.
inline uint8 VDClampedRoundFixedToUint8Fast(float x) {
	sint32 v = (sint32)(x * 255.0f + 0.5f);

	if ((uint32)v >= 0x100)
		v = ~v >> 31;

	return (uint8)v;
}

///////////////////////////////////////////////////////////////////////////

#if VD_CPU_X86
	sint64 __stdcall VDFractionScale64(uint64 a, uint32 b, uint32 c, uint32& remainder);
	uint64 __stdcall VDUMulDiv64x32(uint64 a, uint32 b, uint32 c);
#else
	sint64 VDFractionScale64(uint64 a, uint32 b, uint32 c, uint32& remainder);
	uint64 VDUMulDiv64x32(uint64 a, uint32 b, uint32 c);
#endif

sint64 VDMulDiv64(sint64 a, sint64 b, sint64 c);

///////////////////////////////////////////////////////////////////////////

bool VDVerifyFiniteFloats(const float *p, uint32 n);

class VDFastMathScope {
public:
	VDFastMathScope();
	~VDFastMathScope();

private:
	unsigned mPrevValue;
};

#endif
