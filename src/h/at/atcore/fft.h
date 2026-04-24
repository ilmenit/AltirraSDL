//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2022 Avery Lee
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
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

#ifndef f_AT_ATCORE_FFT_H
#define f_AT_ATCORE_FFT_H

#include <bit>
#include <vd2/system/vdtypes.h>

#if defined(VD_COMPILER_MSVC) || (defined(VD_COMPILER_CLANG) && __has_cpp_attribute(msvc::no_unique_address))
	#define VD_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#elif __has_cpp_attribute(no_unique_address)
	#define VD_NO_UNIQUE_ADDRESS [[no_unique_address]]
#else
	#define VD_NO_UNIQUE_ADDRESS
#endif

#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
#define ATFFT_USE_SSE2
#define ATFFT_USE_RADIX_4
#elif defined(VD_CPU_ARM64) && !defined(__EMSCRIPTEN__)
// ARM64 uses NEON intrinsics; WebAssembly has no NEON so on Emscripten
// we fall through to the scalar radix path below.
#define ATFFT_USE_NEON
#define ATFFT_USE_RADIX_4
#else
// Scalar fallback — used on Emscripten/WebAssembly and any target without
// a supported SIMD ISA.  The scalar path is always built and is correct;
// this just avoids pulling in NEON/SSE intrinsic headers that don't exist.
#define ATFFT_USE_RADIX_4
#endif

enum class ATFFTTableType : uint8 {
	// Cos/sin(-pi/N*i) for i in [0,N). Used for radix-2 and r2c/c2r
	// stages.
	TwiddleNeg180,

	// 0.5*Cos/sin(-pi/N*i) for i in [0,N), arranged as quads of 4 real + 4 imag.
	// Used for vectorized radix-2 stages.
	TwiddleNeg180Vec4,

	// Version of above for 256-bit (AVX).
	TwiddleNeg180Vec8,

	// 0.5*Cos/sin(-pi/N*i) for i in [0,N), arranged as quads of 4 real + 4 imag.
	// Used for vectorized r2c/c2r stages.
	TwiddleNeg180HalfVec4,

	// Version of above for 256-bit (AVX).
	TwiddleNeg180HalfVec8,

	// Cos/sin(-pi/2N*i), Cos/sin(-2pi/2N*i), Cos/sin(-3pi/2N*i) for in [0,N).
	// Used for radix-4 stages.
	Radix4TwiddleNeg90,

	// Same as Radix4TwiddleNeg90, but grouped into 4 real + 4 imag. Used for
	// vectorized radix-4 stages.
	Radix4TwiddleNeg90Vec4,

	// Version of above for 256-bit (AVX).
	Radix4TwiddleNeg90Vec8,

	// Cos/sin(-pi/2N*(i+1/8)) for i in [0,N). Used for IMDCT-to-FFT conversion
	// stages.
	ImdctTwiddle,

	// Same as ImdctTwiddleVec4, but grouped into 4 real + 4 imag. Used for
	// vectorized IMDCT stages.
	ImdctTwiddleVec4,

	// Version of above for 256-bit (AVX).
	ImdctTwiddleVec8,

	// Bit reverse table, with each element multiplied by 16.
	BitReverseX16,
};

class ATFFTAllocator {
	ATFFTAllocator(const ATFFTAllocator&) = delete;
	ATFFTAllocator& operator=(const ATFFTAllocator&) = delete;
public:
	ATFFTAllocator();
	~ATFFTAllocator();

	void ReserveWorkspace(size_t bytes);
	uint16_t AllocateTable(ATFFTTableType type, size_t n);

	void Finalize();

	void *GetWorkspace(size_t offset) const;
	const void *GetTable(uint16_t tableIndex) const;

private:
	void MakeTwiddleTable(float *dst, size_t n, float offset, float divs, float scale);
	void MakeTwiddleTableVec4(float *dst, size_t n, float offset, float divs, float scale);
	void MakeTwiddleTableVec8(float *dst, size_t n, float offset, float divs, float scale);
	void MakeTwiddleTableRadix4(float *dst, size_t n, float offset, float divs);
	void MakeTwiddleTableRadix4Vec4(float *dst, size_t n, float offset, float divs);
	void MakeTwiddleTableRadix4Vec8(float *dst, size_t n, float offset, float divs);
	void MakeBitReverseTableX16(uint32_t *dst, size_t n);

	size_t mWorkspaceBytesNeeded = 0;
	size_t mTableSpaceBytesNeeded = 0;
	char *mpMemory = nullptr;

	struct TableDesc {
		ATFFTTableType mType;
		size_t mCount;

		bool operator==(const TableDesc&) const = default;

		size_t GetTableSize() const;
	};

	struct TableEntry {
		TableDesc mDesc;
		size_t mOffset;
	};

	vdfastvector<TableEntry> mTables;
};

union ATFFTTableReference {
	uint16 mTableIndex;
	float *mpFloatTable;
	uint32 *mpIntTable;
};

class ATFFTBase {
protected:
#if defined(ATFFT_USE_SSE2) || defined(ATFFT_USE_NEON)
	static constexpr bool kUseVec4 = true;
#else
	static constexpr bool kUseVec4 = false;
#endif

	void InitImpl(ATFFTAllocator& allocator, uint32 N, bool imdct, bool optimizeForSpeed);

	void ReserveImpl(ATFFTAllocator& allocator, uint32 N, bool imdct, bool optimizeForSpeed);
	void BindImpl(const ATFFTAllocator& allocator);

	void ForwardImpl(float *dst, const float *src);
	void InverseImpl(float *dst, const float *src);
	void MultiplyAddImpl(float *dst, const float *src1, const float *src2, int N);

	ATFFTTableReference mBitRevTable {};
	float *mpWorkArea {};

	uint8_t mFFTSizeBits = 0;
	uint8_t mNumRadix4Stages = 0;
	uint8_t mNumRadix2Stages = 0;
	bool mbIMDCT = false;

#if defined(ATFFT_USE_SSE2)
	bool mbUseAVX2 = false;
#endif

	ATFFTTableReference mStageTables[16] {};
};

// Fast Fourier Transform implementation.
//
// Computes the forward DFT from real data to complex frequencies (sign -1), and the
// inverse DFT from complex frequencies to real data (sign +1). The inverse transform
// is unnormalized with a scaling factor of N/2. This means that:
//
//   - ifft(fft(x)) = (N/2)*x
//   - conv(x,y) = (N/2)*ifft(fft(x)*fft(y))
//               = ifft(fft(x*(N/2))*fft(y))
//
// The frequency domain order is: X[0], X[N/2], real(X[1]), imag(X[1]).... All terms
// except the DC and Nyquist terms represent a pair of symmetric +/- frequency bins,
// which must be taken into account if measuring energy -- this is responsible for
// ifft([1...]) returning half the energy of ifft([0 0 1...]), because in the second
// case there are actually a pair X[1] and X[-1] bins set to 1.
//
// Input and output arrays do not have to be aligned, though it is slightly better if
// they are aligned to 16. It is much more important for the FFT object itself to be
// aligned (natural alignment is 64 for cache line alignment).
//
// FFTs can be tuned either for speed or for efficiency. Speed allows use of higher
// vector ISAs that may be detrimental if used too occasionally (AVX); efficiency
// stays with 128-bit vectors.
//
template<int N, bool T_SharedAllocator = false>
class ATFFT : public ATFFTBase {
	static_assert(N >= 16 && N <= 8192 && (N & (N - 1)) == 0);

public:
	ATFFT(bool optimizeForSpeed = true) {
		if constexpr (!T_SharedAllocator) {
			InitImpl(mFFTAllocator, N, false, optimizeForSpeed);
		}
	}

	void Reserve(ATFFTAllocator& allocator, bool optimizeForSpeed) requires T_SharedAllocator {
		ReserveImpl(allocator, N, false, optimizeForSpeed);
	}

	void Bind(const ATFFTAllocator& allocator) requires T_SharedAllocator {
		BindImpl(allocator);
	}

	// Forward transform from time to frequency domain. This is a real-to-complex (r2c)
	// in-place transform. The first two output values are real X[0] and X[N/2], followed by
	// pairs of real/imag for X[1], X[2]... X[N/2-1]. This just passes the same pointer
	// to the out of place version.
	void Forward(float *x) {
		Forward(x, x);
	}

	// Forward transform, out of place.
	void Forward(float *dst, const float *src) {
		ForwardImpl(dst, src);
	}

	// Inverse transform from frequency to time domain. This is a complex-to-real (c2r)
	// in-place transform. The first two input values are real X[0] and X[N/2], followed by
	// pairs of real/imag for X[1], X[2]... X[N/2-1]. This just passes the same pointer
	// to the out of place version.
	void Inverse(float *x) {
		Inverse(x, x);
	}

	// Inverse transform, out of place.
	void Inverse(float *dst, const float *src) {
		InverseImpl(dst, src);
	}

	void MultiplyAdd(float *dst, const float *src1, const float *src2) {
		MultiplyAddImpl(dst, src1, src2, N);
	}

private:
	struct Empty {};

	VD_NO_UNIQUE_ADDRESS std::conditional_t<!T_SharedAllocator, ATFFTAllocator, Empty> mFFTAllocator;
};

// Inverse modified discrete cosine transform (IMDCT) implementation.
//
// The transform implemented:
//
//	x'[i] = sum(j = 0..N-1) { x[i]*cos(pi/N * (i + 0.5) * (j + 0.5))  }
//
// This form is compatible with Vorbis I decoding.
//
// The IMDCT transforms N points of input into 2N points of output, which is
// redundant: the first quarter is negated reverse of the second quarter, and
// the third quarter is the reverse of the fourth quarter. This version returns
// the third and second quarters, in that order.
//
// The underlying implementation uses a N/2 point complex FFT to implement the
// N-point IMDCT. Note that the inverse MDCT requires the _forward_ FFT.
//
class ATIMDCT : public ATFFTBase {
public:
	void Reserve(ATFFTAllocator& alloc, size_t N, bool optimizeForSpeed) {
		ReserveImpl(alloc, N, true, optimizeForSpeed);
	}

	void Bind(ATFFTAllocator& alloc) {
		BindImpl(alloc);
	}

	void Transform(float *x) {
		ForwardImpl(x, x);
	}
};

#endif
