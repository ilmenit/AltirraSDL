//	VirtualDub - Video processing and capture application
//	Copyright (C) 1998-2023 Avery Lee, All Rights Reserved.
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

#ifndef f_VD2_SYSTEM_CONSTEXPR_H
#define f_VD2_SYSTEM_CONSTEXPR_H

#include <bit>
#include <iterator>
#include <type_traits>
#include <math.h>

template<typename T, T N> struct VDCxPrint;

template<typename T, size_t N>
struct VDCxArray {
	using value_type      = T;
	using size_type       = size_t;
	using difference_type = ptrdiff_t;
	using reference       = T&;
	using const_reference = const T&;
	using pointer         = T*;
	using const_pointer   = const T*;
	using iterator        = T*;
	using const_iterator  = const T*;
	using reverse_iterator       = std::reverse_iterator<iterator>;
	using const_reverse_iterator = std::reverse_iterator<const_iterator>;

	T v[N];

	[[nodiscard]] constexpr T&       operator[](size_t i)       { return v[i]; }
	[[nodiscard]] constexpr const T& operator[](size_t i) const { return v[i]; }

	[[nodiscard]] constexpr       T *begin()       { return v; }
	[[nodiscard]] constexpr const T *begin() const { return v; }
	[[nodiscard]] constexpr       T *end()       { return v+N; }
	[[nodiscard]] constexpr const T *end() const { return v+N; }

	[[nodiscard]] constexpr       T *data()       { return v; }
	[[nodiscard]] constexpr const T *data() const { return v; }

	[[nodiscard]] constexpr       T& front()       { return v[0]; }
	[[nodiscard]] constexpr const T& front() const { return v[0]; }

	[[nodiscard]] constexpr       T& back()       { return v[N-1]; }
	[[nodiscard]] constexpr const T& back() const { return v[N-1]; }

	[[nodiscard]] consteval bool   empty() const { return false; }
	[[nodiscard]] consteval size_t size() const { return N; }

	template<typename T_Src>
	static constexpr VDCxArray transform(T_Src&& input, auto&& fn) {
		VDCxArray output {};

		for(size_t i=0; i<N; ++i)
			output.v[i] = fn(input[i]);

		return output;
	}

	static constexpr VDCxArray from_index(auto&& fn) {
		VDCxArray output {};

		for(size_t i=0; i<N; ++i)
			output.v[i] = fn(T(i));

		return output;
	}
};

template<typename Key, typename Value>
struct VDCxPair {
	Key first;
	Value second;
};

template<typename Key, typename Value, size_t N, size_t NumBuckets>
class VDCxHashMap {
public:
	using Entry = VDCxPair<Key, Value>;
	using Index = std::conditional_t<(N < 256), uint8, std::conditional_t<(N < 65536), uint16, uint32>>;

	using value_type      = Entry;
	using size_type       = size_t;
	using difference_type = ptrdiff_t;
	using reference       = Entry&;
	using const_reference = const Entry&;
	using pointer         = Entry*;
	using const_pointer   = const Entry*;
	using iterator        = Entry*;
	using const_iterator  = const Entry*;
	using reverse_iterator       = std::reverse_iterator<iterator>;
	using const_reverse_iterator = std::reverse_iterator<const_iterator>;

	constexpr void init(const Entry (&entries)[N]) {
		uint32 hashfirst[NumBuckets] {};
		uint32 hashnext[N] {};

		for(size_t i = 0; i < N; ++i) {
			size_t h = (size_t)entries[i].first % NumBuckets;

			hashnext[i] = hashfirst[h];
			hashfirst[h] = i + 1;
		}

		size_t count = 0;
		for(size_t h = 0; h < NumBuckets; ++h) {
			mPartitions[h] = (Index)count;

			for(uint32 i = hashfirst[h]; i; i = hashnext[i - 1])
				mEntries[count++] = entries[i - 1];
		}

		mPartitions[NumBuckets] = (Index)count;
	}

	[[nodiscard]] constexpr Entry&       operator[](size_t i)       { return mEntries[i]; }
	[[nodiscard]] constexpr const Entry& operator[](size_t i) const { return mEntries[i]; }

	[[nodiscard]] constexpr       Entry *begin()       { return mEntries; }
	[[nodiscard]] constexpr const Entry *begin() const { return mEntries; }
	[[nodiscard]] constexpr       Entry *end()       { return mEntries+N; }
	[[nodiscard]] constexpr const Entry *end() const { return mEntries+N; }

	[[nodiscard]] constexpr       Entry *data()       { return mEntries; }
	[[nodiscard]] constexpr const Entry *data() const { return mEntries; }

	[[nodiscard]] constexpr       Entry& front()       { return mEntries[0]; }
	[[nodiscard]] constexpr const Entry& front() const { return mEntries[0]; }

	[[nodiscard]] constexpr       Entry& back()       { return mEntries[N-1]; }
	[[nodiscard]] constexpr const Entry& back() const { return mEntries[N-1]; }

	[[nodiscard]] constexpr bool   empty() const { return false; }
	[[nodiscard]] constexpr size_t size() const { return N; }

	[[nodiscard]] constexpr iterator find(const Key& k) const {
		const size_t h = (size_t)k % NumBuckets;
		const Index idx1 = mPartitions[h];
		const Index idx2 = mPartitions[h + 1];
		const Entry *p = &mEntries[idx1];

		for(Index idx = idx1; idx != idx2; ++idx) {
			if (p->mKey == k)
				return p;
		}

		return &mEntries[N];
	}

private:
	Index mPartitions[NumBuckets + 1] {};
	Entry mEntries[N] {};
};

constexpr float VDCxNarrowToFloat(double d) {
	// #pragma float_control isn't enough to force correct narrowing with
	// fast math, so use the bit_cast sledgehammer to prevent the compiler from
	// cheating.
	return std::bit_cast<float>(std::bit_cast<uint32>((float)d));
}

// Compute sin(pi*x).
constexpr double VDCxSinPiD(double x) {
	// mirror across v=0 antisymmetry
	float sign = 1;

	if (x < 0) {
		x = -x;
		sign = -1;
	}

	// guard against integers exceeding a long long -- these will be even integers
	// and thus guaranteed to result in 0
	if (x >= 18014398509481984.0)
		return 0;

	// wrap around 360
	x -= (long long)(x * 0.5) * 2;

	// mirror across v=1 antisymmetry
	if (x > 1.0) {
		sign = -sign;
		x -= 1.0;
	}

	// mirror across v=0.5 symmetry
	if (x > 0.5)
		x = 1.0 - x;

	// compute series expansion over first quadrant
	// need to split into octants to avoid cancellation related issues
	if (x > 0.25) {
		x = 0.5 - x;
		const double x2 = x*x;
		return sign*(
			((((((((-0.00000013879*x2 + 0.00000430307)*x2 - 0.000104638105)*x2 + 0.001929574309)*x2 - 0.02580689139)*x2 + 0.235330630359)*x2 - 1.335262768855)*x2 + 4.058712126417)*x2 - 4.934802200545)*x2 + 1.0
		);
	} else {
		const double x2 = x*x;

		return sign*(
			((((((((0.000000795205*x2 - 0.000021915353)*x2 + 0.000466302806)*x2 - 0.007370430946)*x2 + 0.082145886611)*x2 - 0.599264529321)*x2 + 2.550164039877)*x2 - 5.16771278005)*x2 + 3.14159265359)*x
		);
	}
}

constexpr float VDCxSinPi(float x) {
	// Use std::is_constant_evaluated() instead of C++23 `if consteval`
	// so this header compiles on gcc 11 (Ubuntu 22.04 default).  Both
	// expressions are evaluated at compile time when called from a
	// constexpr context, but the standard guarantees that only the
	// taken branch is instantiated, so the runtime sin() call is never
	// reached during constant evaluation.
	if (std::is_constant_evaluated()) {
		return VDCxNarrowToFloat(VDCxSinPiD(x));
	} else {
		return VDCxNarrowToFloat(sin((double)x * 3.1415926535897932384626433832795));
	}
}

// Compute cos(pi*x).
constexpr double VDCxCosPiD(double x) {
	// mirror across v=0 symmetry
	if (x < 0)
		x = -x;

	// guard against integers exceeding a long long -- these will be even integers
	// and thus guaranteed to result in 1
	if (x >= 18014398509481984.0)
		return 1;

	// wrap around 360
	x -= (long long)(x * 0.5) * 2;

	// mirror across v=1 symmetry
	if (x > 1.0)
		x = 2.0 - x;

	// mirror across v=0.5 antisymmetry
	float sign = 1;

	if (x > 0.5) {
		sign = -1;
		x = 1.0 - x;
	}

	// compute series expansion over first quadrant
	// need to split into octants to avoid cancellation related issues
	if (x > 0.25) {
		x = 0.5 - x;
		const double x2 = x*x;

		return sign*(
			((((((((0.000000795205*x2 - 0.000021915353)*x2 + 0.000466302806)*x2 - 0.007370430946)*x2 + 0.082145886611)*x2 - 0.599264529321)*x2 + 2.550164039877)*x2 - 5.16771278005)*x2 + 3.14159265359)*x
		);
	} else {
		const double x2 = x*x;
		return sign*(
			((((((((-0.00000013879*x2 + 0.00000430307)*x2 - 0.000104638105)*x2 + 0.001929574309)*x2 - 0.02580689139)*x2 + 0.235330630359)*x2 - 1.335262768855)*x2 + 4.058712126417)*x2 - 4.934802200545)*x2 + 1.0
		);
	}
}

constexpr float VDCxCosPi(float x) {
	if (std::is_constant_evaluated()) {
		return VDCxNarrowToFloat(VDCxCosPiD(x));
	} else {
		return VDCxNarrowToFloat(cos((double)x * 3.1415926535897932384626433832795));
	}
}

constexpr float VDCxSin(float v) {
	if (std::is_constant_evaluated()) {
		return VDCxNarrowToFloat(VDCxSinPiD(v * 0.31830988618379067153776752674503));
	} else {
		return sinf(v);
	}
}

constexpr float VDCxCos(float v) {
	if (std::is_constant_evaluated()) {
		return VDCxNarrowToFloat(VDCxSinPiD(v * 0.31830988618379067153776752674503));
	} else {
		return cosf(v);
	}
}

// Computes sinc(pi*x) = sin(pi*x)/(pi*x) with limit handling for x=0.
constexpr float VDCxSincPi(float v) {
	if (v >= -1e-8f && v <= 1e-8f)
		return 1.0f;

	if (std::is_constant_evaluated()) {
		return VDCxNarrowToFloat(VDCxSinPiD(v) / (3.1415926535897932384626433832795 * v));
	} else {
		const double d = (double)v * 3.1415926535897932384626433832795;
		return VDCxNarrowToFloat(sin(d) / d);
	}
}

constexpr float VDCxAbs(float v) {
	return v < 0 ? -v : v;
}

constexpr float VDCxSqrt(float v) {
	if (v < 0)
		throw;

	if (v == 0.0f)
		return 0.0f;

	// Compute initial approximation
	//
	// This differs slightly from the classic Q3 magic constant for a slightly
	// better error bound.
	//
	// Lomont, Chris. Fast Inverse Square Root. PDF file. February 2003.
	// https://www.lomont.org/papers/2003/InvSqrt.pdf
	// 
	double vhalf = 0.5 * (double)v;
	double x = (double)std::bit_cast<float>(UINT32_C(0x5F375A86) - (std::bit_cast<uint32>(v) >> 1));

	// three Newton-Raphson iterations in double precision
	x = x*(1.5 - vhalf * x * x);
	x = x*(1.5 - vhalf * x * x);
	x = x*(1.5 - vhalf * x * x);

	// compute sqrt(x) = x * rsqrt(x) and round to float
	return VDCxNarrowToFloat((double)v * x);
}

constexpr float VDCxFloor(float v) {
	// early out if the value is guaranteed integer (which is also possibly
	// outside of integer range)
	if (v <= -0x1.0p24 || v >= +0x1.0p24)
		return v;

	// truncate to integer
	sint32 i = (sint32)v;

	// return lower integer
	return (float)i > v ? (float)(i - 1) : (float)i;
}

constexpr double VDCxFloor(double v) {
	// early out if the value is guaranteed integer (which is also possibly
	// outside of integer range)
	if (v <= -0x1.0p53 || v >= +0x1.0p53)
		return v;

	// truncate to integer
	long long i = (long long)v;

	// return lower integer
	return (double)i > v ? (double)(i - 1) : (double)i;
}

constexpr float VDCxExp(float v) {
	// convert to double
	double d = std::bit_cast<double>(std::bit_cast<uint64>((double)v));

	// scale by 1/ln(2) to switch to computing exp2()
	d *= 1.4426950408889634073599246810019;

	// split integer/fraction
	double x = VDCxFloor(d);
	double f = d - x;

	// return zero if too small for a float
	if (x < -127 - 25)
		return 0;

	// fail if overflow
	if (x >= 128)
		throw;

	// Compute series expansion for e^(frac*ln2/2) over [0, 0.34658]. We need
	// order-8 in order to clear full float precision.
	double z = f * 0.34657359027997265470861606072909;

	double fr = 0;

	fr += 1.0 / 3628800.0;
	fr *= z;
	fr += 1.0 / 362880.0;
	fr *= z;
	fr += 1.0 / 40320.0;
	fr *= z;
	fr += 1.0 / 5040.0;
	fr *= z;
	fr += 1.0 / 720.0;
	fr *= z;
	fr += 1.0 / 120.0;
	fr *= z;
	fr += 1.0 / 24.0;
	fr *= z;
	fr += 1.0 / 6.0;
	fr *= z;
	fr += 1.0 / 2.0;
	fr *= z;
	fr += 1.0;
	fr *= z;
	fr += 1.0;

	// compute 2^x * e^(f*ln2/2)
	double r = std::bit_cast<double>((unsigned long long)(x + 1023) << 52) * (fr * fr);

	// round to float and return
	return VDCxNarrowToFloat(r);
}

#endif
