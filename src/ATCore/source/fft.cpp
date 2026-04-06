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

#include <stdafx.h>
#include <math.h>
#include <bit>
#include <numbers>
#include <intrin.h>
#include <vd2/system/cpuaccel.h>
#include <vd2/system/error.h>
#include <at/atcore/fft.h>

/////////////////////////////////////////////////////////////////////////////

void ATFFT_DIT_Radix2_Scalar(float *y0, const float *w4, int N, int logStep);
void ATFFT_DIF_Radix2_Scalar(float *y0, const float *w4, int N, int logStep);

void ATFFT_DIT_Radix8_Scalar(float *dst0, const float *y0, const uint32 *order0, int N);
void ATFFT_DIF_Radix8_Scalar(float *dst0, const float *src0, const uint32 *order0, int N);

void ATFFT_DIT_R2C_Scalar(float *dst0, const float *src0, const float *w, size_t N);
void ATFFT_DIF_C2R_Scalar(float *dst0, const float *x, const float *w, size_t N);

void ATFFT_MultiplyAdd_Scalar(float *VDRESTRICT dst, const float *VDRESTRICT src1, const float *VDRESTRICT src2, int N);

void ATFFT_IMDCT_PreTransform_Scalar(float *dst, const float *src, const float *w, size_t N);
void ATFFT_IMDCT_PostTransform_Scalar(float *dst, const float *src, const float *w, size_t N);

/////////////////////////////////////////////////////////////////////////////

void ATFFT_DIT_Radix2_SSE2(float *y0, const float *w4, int N, int logStep);
void ATFFT_DIT_Radix4_SSE2(float *y0, const float *w4, int N, int logStep);
void ATFFT_DIF_Radix2_SSE2(float *y0, const float *w4, int N, int logStep);
void ATFFT_DIF_Radix4_SSE2(float *y0, const float *w4, int N, int logStep);

void ATFFT_DIT_Radix8_SSE2(float *dst0, const float *y0, const uint32 *order0, int N);
void ATFFT_DIF_Radix8_SSE2(float *dst0, const float *src0, const uint32 *order0, int N);

void ATFFT_DIT_R2C_SSE2(float *dst0, const float *src0, const float *w, size_t N);
void ATFFT_DIF_C2R_SSE2(float *dst0, const float *x, const float *w, size_t N);

void ATFFT_MultiplyAdd_SSE2(float *VDRESTRICT dst, const float *VDRESTRICT src1, const float *VDRESTRICT src2, int N);

void ATFFT_IMDCT_PreTransform_SSE2(float *dst, const float *src, const float *w, size_t N);
void ATFFT_IMDCT_PostTransform_SSE2(float *dst, const float *src, const float *w, size_t N);

/////////////////////////////////////////////////////////////////////////////

void ATFFT_DIT_Radix2_AVX2(float *y0, const float *w4, int N, int logStep);
void ATFFT_DIT_Radix4_AVX2(float *y0, const float *w4, int N, int logStep);
void ATFFT_DIF_Radix2_AVX2(float *y0, const float *w4, int N, int logStep);
void ATFFT_DIF_Radix4_AVX2(float *y0, const float *w4, int N, int logStep);

void ATFFT_DIT_Radix8_AVX2(float *dst0, const float *y0, const uint32 *order0, int N);
void ATFFT_DIF_Radix8_AVX2(float *dst0, const float *src0, const uint32 *order0, int N);

void ATFFT_DIT_R2C_AVX2(float *dst0, const float *src0, const float *w, size_t N);
void ATFFT_DIF_C2R_AVX2(float *dst0, const float *x, const float *w, size_t N);

void ATFFT_MultiplyAdd_AVX2(float *VDRESTRICT dst, const float *VDRESTRICT src1, const float *VDRESTRICT src2, int N);

void ATFFT_IMDCT_PreTransform_AVX2(float *dst, const float *src, const float *w, size_t N);
void ATFFT_IMDCT_PostTransform_AVX2(float *dst, const float *src, const float *w, size_t N);

/////////////////////////////////////////////////////////////////////////////

void ATFFT_DIT_Radix2_NEON(float *y0, const float *w4, int N, int logStep);
void ATFFT_DIT_Radix4_NEON(float *y0, const float *w4, int N, int logStep);
void ATFFT_DIF_Radix2_NEON(float *y0, const float *w4, int N, int logStep);
void ATFFT_DIF_Radix4_NEON(float *y0, const float *w4, int N, int logStep);

void ATFFT_DIT_Radix8_NEON(float *dst0, const float *y0, const uint32 *order0, int N);
void ATFFT_DIF_Radix8_NEON(float *dst0, const float *src0, const uint32 *order0, int N);

void ATFFT_DIT_R2C_NEON(float *dst0, const float *src0, const float *w, size_t N);
void ATFFT_DIF_C2R_NEON(float *dst0, const float *x, const float *w, size_t N);

void ATFFT_IMDCT_PreTransform_NEON(float *dst, const float *src, const float *w, size_t N);
void ATFFT_IMDCT_PostTransform_NEON(float *dst, const float *src, const float *w, size_t N);

///////////////////////////////////////////////////////////////////////////

ATFFTAllocator::ATFFTAllocator() {
}

ATFFTAllocator::~ATFFTAllocator() {
	if (mpMemory)
		VDAlignedFree(mpMemory);
}

void ATFFTAllocator::ReserveWorkspace(size_t bytes) {
	if (mWorkspaceBytesNeeded < bytes)
		mWorkspaceBytesNeeded = bytes;
}

uint16_t ATFFTAllocator::AllocateTable(ATFFTTableType type, size_t n) {
	TableDesc desc { type, n };

	auto it = std::find_if(mTables.begin(), mTables.end(), [&](const TableEntry& te) { return te.mDesc == desc; });
	if (it != mTables.end())
		return (uint16_t)(it - mTables.begin());

	auto& te = mTables.emplace_back();
	te.mDesc = desc;
	te.mOffset = mTableSpaceBytesNeeded;
	mTableSpaceBytesNeeded += (desc.GetTableSize() + 63) & ~size_t(63);

	return (uint16_t)(mTables.size() - 1);
}

void ATFFTAllocator::Finalize() {
	if (mpMemory)
		VDRaiseInternalFailure();

	mWorkspaceBytesNeeded = (mWorkspaceBytesNeeded + 63) & ~size_t(63);

	const size_t totalNeeded = mTableSpaceBytesNeeded + mWorkspaceBytesNeeded;
	mpMemory = (char *)VDAlignedMalloc(totalNeeded, 64);
	if (!mpMemory)
		throw MyMemoryError();

	memset(mpMemory, 0, totalNeeded);

	for(TableEntry& te : mTables) {
		te.mOffset += mWorkspaceBytesNeeded;

		void *dst = mpMemory + te.mOffset;
		switch(te.mDesc.mType) {
			case ATFFTTableType::TwiddleNeg180:
				MakeTwiddleTable((float *)dst, te.mDesc.mCount, 0.0f, -(float)te.mDesc.mCount, 1.0f);
				break;

			case ATFFTTableType::TwiddleNeg180Vec4:
				MakeTwiddleTableVec4((float *)dst, te.mDesc.mCount, 0.0f, -(float)te.mDesc.mCount, 1.0f);
				break;

			case ATFFTTableType::TwiddleNeg180Vec8:
				MakeTwiddleTableVec8((float *)dst, te.mDesc.mCount, 0.0f, -(float)te.mDesc.mCount, 1.0f);
				break;

			case ATFFTTableType::TwiddleNeg180HalfVec4:
				MakeTwiddleTableVec4((float *)dst, te.mDesc.mCount, 0.0f, -(float)te.mDesc.mCount, 0.5f);
				break;

			case ATFFTTableType::TwiddleNeg180HalfVec8:
				MakeTwiddleTableVec8((float *)dst, te.mDesc.mCount, 0.0f, -(float)te.mDesc.mCount, 0.5f);
				break;

			case ATFFTTableType::Radix4TwiddleNeg90:
				MakeTwiddleTableRadix4((float *)dst, te.mDesc.mCount, 0.0f, -2.0f * (float)te.mDesc.mCount);
				break;

			case ATFFTTableType::Radix4TwiddleNeg90Vec4:
				MakeTwiddleTableRadix4Vec4((float *)dst, te.mDesc.mCount, 0.0f, -2.0f * (float)te.mDesc.mCount);
				break;

			case ATFFTTableType::Radix4TwiddleNeg90Vec8:
				MakeTwiddleTableRadix4Vec8((float *)dst, te.mDesc.mCount, 0.0f, -2.0f * (float)te.mDesc.mCount);
				break;

			case ATFFTTableType::ImdctTwiddle:
				MakeTwiddleTable((float *)dst, te.mDesc.mCount, 0.125f - (float)te.mDesc.mCount * 2.0f, -(float)te.mDesc.mCount * 2.0f, 1.0f);
				break;

			case ATFFTTableType::ImdctTwiddleVec4:
				MakeTwiddleTableVec4((float *)dst, te.mDesc.mCount, 0.125f - (float)te.mDesc.mCount * 2.0f, -(float)te.mDesc.mCount * 2.0f, 1.0f);
				break;

			case ATFFTTableType::ImdctTwiddleVec8:
				MakeTwiddleTableVec8((float *)dst, te.mDesc.mCount, 0.125f - (float)te.mDesc.mCount * 2.0f, -(float)te.mDesc.mCount * 2.0f, 1.0f);
				break;

			case ATFFTTableType::BitReverseX16:
				MakeBitReverseTableX16((uint32 *)dst, te.mDesc.mCount);
				break;
		}
	}
}

void *ATFFTAllocator::GetWorkspace(size_t offset) const {
	return mpMemory + offset;
}

const void *ATFFTAllocator::GetTable(uint16_t tableIndex) const {
	return mpMemory + mTables[tableIndex].mOffset;
}

size_t ATFFTAllocator::TableDesc::GetTableSize() const {
	switch(mType) {
		case ATFFTTableType::Radix4TwiddleNeg90:
		case ATFFTTableType::Radix4TwiddleNeg90Vec4:
		case ATFFTTableType::Radix4TwiddleNeg90Vec8:
			return mCount * sizeof(float) * 6;

		case ATFFTTableType::TwiddleNeg180:
		case ATFFTTableType::TwiddleNeg180Vec4:
		case ATFFTTableType::TwiddleNeg180Vec8:
		case ATFFTTableType::TwiddleNeg180HalfVec4:
		case ATFFTTableType::TwiddleNeg180HalfVec8:
		case ATFFTTableType::ImdctTwiddle:
		case ATFFTTableType::ImdctTwiddleVec4:
		case ATFFTTableType::ImdctTwiddleVec8:
			return mCount * sizeof(float) * 2;

		case ATFFTTableType::BitReverseX16:
			return mCount * sizeof(uint32_t);
	}

	VDRaiseInternalFailure();
}

void ATFFTAllocator::MakeTwiddleTable(float *dst, size_t n, float offset, float divs, float scale) {
	float dw = std::numbers::pi_v<float> / divs;

	for(size_t i = 0; i < n; ++i) {
		const float w = ((float)i + offset) * dw;

		dst[0] = scale*cosf(w);
		dst[1] = scale*sinf(w);
		dst += 2;
	}
}

void ATFFTAllocator::MakeTwiddleTableVec4(float *dst, size_t n, float offset, float divs, float scale) {
	float dw = std::numbers::pi_v<float> / divs;

	for(size_t i = 0; i < n; i += 4) {
		const float w0 = ((float)i + offset) * dw;
		const float w1 = ((float)i + offset + 1.0f) * dw;
		const float w2 = ((float)i + offset + 2.0f) * dw;
		const float w3 = ((float)i + offset + 3.0f) * dw;

		dst[0] = scale*cosf(w0);
		dst[1] = scale*cosf(w1);
		dst[2] = scale*cosf(w2);
		dst[3] = scale*cosf(w3);
		dst[4] = scale*sinf(w0);
		dst[5] = scale*sinf(w1);
		dst[6] = scale*sinf(w2);
		dst[7] = scale*sinf(w3);
		dst += 8;
	}
}

void ATFFTAllocator::MakeTwiddleTableVec8(float *dst, size_t n, float offset, float divs, float scale) {
	float dw = std::numbers::pi_v<float> / divs;

	for(size_t i = 0; i < n; i += 8) {
		const float w0 = ((float)i + offset) * dw;
		const float w1 = ((float)i + offset + 1.0f) * dw;
		const float w2 = ((float)i + offset + 2.0f) * dw;
		const float w3 = ((float)i + offset + 3.0f) * dw;
		const float w4 = ((float)i + offset + 4.0f) * dw;
		const float w5 = ((float)i + offset + 5.0f) * dw;
		const float w6 = ((float)i + offset + 6.0f) * dw;
		const float w7 = ((float)i + offset + 7.0f) * dw;

		dst[ 0] = scale*cosf(w0);
		dst[ 1] = scale*cosf(w1);
		dst[ 2] = scale*cosf(w2);
		dst[ 3] = scale*cosf(w3);
		dst[ 4] = scale*cosf(w4);
		dst[ 5] = scale*cosf(w5);
		dst[ 6] = scale*cosf(w6);
		dst[ 7] = scale*cosf(w7);
		dst[ 8] = scale*sinf(w0);
		dst[ 9] = scale*sinf(w1);
		dst[10] = scale*sinf(w2);
		dst[11] = scale*sinf(w3);
		dst[12] = scale*sinf(w4);
		dst[13] = scale*sinf(w5);
		dst[14] = scale*sinf(w6);
		dst[15] = scale*sinf(w7);
		dst += 16;
	}
}

void ATFFTAllocator::MakeTwiddleTableRadix4(float *dst, size_t n, float offset, float divs) {
	float dw1 = std::numbers::pi_v<float> / divs;
	float dw2 = dw1 * 2.0f;
	float dw3 = dw1 * 3.0f;

	for(size_t i = 0; i < n; ++i) {
		const float w1 = ((float)i + offset) * dw1;
		const float w2 = ((float)i + offset) * dw2;
		const float w3 = ((float)i + offset) * dw3;

		dst[0] = cosf(w1);
		dst[1] = sinf(w1);
		dst[2] = cosf(w2);
		dst[3] = sinf(w2);
		dst[4] = cosf(w3);
		dst[5] = sinf(w3);
		dst += 6;
	}
}

void ATFFTAllocator::MakeTwiddleTableRadix4Vec4(float *dst, size_t n, float offset, float divs) {
	float dw1 = std::numbers::pi_v<float> / divs;
	float dw2 = dw1 * 2.0f;
	float dw3 = dw1 * 3.0f;

	for(size_t i = 0; i < n; i += 4) {
		const float w1 = ((float)i + offset) * dw1;
		const float w2 = ((float)i + offset) * dw2;
		const float w3 = ((float)i + offset) * dw3;

		dst[0] = cosf(w1);
		dst[1] = cosf(w1 + dw1);
		dst[2] = cosf(w1 + dw1*2);
		dst[3] = cosf(w1 + dw1*3);
		dst[4] = sinf(w1);
		dst[5] = sinf(w1 + dw1);
		dst[6] = sinf(w1 + dw1*2);
		dst[7] = sinf(w1 + dw1*3);
		dst += 8;

		dst[0] = cosf(w2);
		dst[1] = cosf(w2 + dw2);
		dst[2] = cosf(w2 + dw2*2);
		dst[3] = cosf(w2 + dw2*3);
		dst[4] = sinf(w2);
		dst[5] = sinf(w2 + dw2);
		dst[6] = sinf(w2 + dw2*2);
		dst[7] = sinf(w2 + dw2*3);
		dst += 8;

		dst[0] = cosf(w3);
		dst[1] = cosf(w3 + dw3);
		dst[2] = cosf(w3 + dw3*2);
		dst[3] = cosf(w3 + dw3*3);
		dst[4] = sinf(w3);
		dst[5] = sinf(w3 + dw3);
		dst[6] = sinf(w3 + dw3*2);
		dst[7] = sinf(w3 + dw3*3);
		dst += 8;
	}
}

void ATFFTAllocator::MakeTwiddleTableRadix4Vec8(float *dst, size_t n, float offset, float divs) {
	float dw1 = std::numbers::pi_v<float> / divs;
	float dw2 = dw1 * 2.0f;
	float dw3 = dw1 * 3.0f;

	for(size_t i = 0; i < n; i += 8) {
		const float w1 = ((float)i + offset) * dw1;
		const float w2 = ((float)i + offset) * dw2;
		const float w3 = ((float)i + offset) * dw3;

		dst[ 0] = cosf(w1);
		dst[ 1] = cosf(w1 + dw1);
		dst[ 2] = cosf(w1 + dw1*2);
		dst[ 3] = cosf(w1 + dw1*3);
		dst[ 4] = cosf(w1 + dw1*4);
		dst[ 5] = cosf(w1 + dw1*5);
		dst[ 6] = cosf(w1 + dw1*6);
		dst[ 7] = cosf(w1 + dw1*7);
		dst[ 8] = sinf(w1);
		dst[ 9] = sinf(w1 + dw1);
		dst[10] = sinf(w1 + dw1*2);
		dst[11] = sinf(w1 + dw1*3);
		dst[12] = sinf(w1 + dw1*4);
		dst[13] = sinf(w1 + dw1*5);
		dst[14] = sinf(w1 + dw1*6);
		dst[15] = sinf(w1 + dw1*7);
		dst += 16;

		dst[ 0] = cosf(w2);
		dst[ 1] = cosf(w2 + dw2);
		dst[ 2] = cosf(w2 + dw2*2);
		dst[ 3] = cosf(w2 + dw2*3);
		dst[ 4] = cosf(w2 + dw2*4);
		dst[ 5] = cosf(w2 + dw2*5);
		dst[ 6] = cosf(w2 + dw2*6);
		dst[ 7] = cosf(w2 + dw2*7);
		dst[ 8] = sinf(w2);
		dst[ 9] = sinf(w2 + dw2);
		dst[10] = sinf(w2 + dw2*2);
		dst[11] = sinf(w2 + dw2*3);
		dst[12] = sinf(w2 + dw2*4);
		dst[13] = sinf(w2 + dw2*5);
		dst[14] = sinf(w2 + dw2*6);
		dst[15] = sinf(w2 + dw2*7);
		dst += 16;

		dst[ 0] = cosf(w3);
		dst[ 1] = cosf(w3 + dw3);
		dst[ 2] = cosf(w3 + dw3*2);
		dst[ 3] = cosf(w3 + dw3*3);
		dst[ 4] = cosf(w3 + dw3*4);
		dst[ 5] = cosf(w3 + dw3*5);
		dst[ 6] = cosf(w3 + dw3*6);
		dst[ 7] = cosf(w3 + dw3*7);
		dst[ 8] = sinf(w3);
		dst[ 9] = sinf(w3 + dw3);
		dst[10] = sinf(w3 + dw3*2);
		dst[11] = sinf(w3 + dw3*3);
		dst[12] = sinf(w3 + dw3*4);
		dst[13] = sinf(w3 + dw3*5);
		dst[14] = sinf(w3 + dw3*6);
		dst[15] = sinf(w3 + dw3*7);
		dst += 16;
	}
}

void ATFFTAllocator::MakeBitReverseTableX16(uint32_t *dst, size_t n) {
	uint32_t highBit = (uint32_t)n >> 1;

	uint32_t idx = 0;
	for(size_t i=0; i<n; ++i) {
		*dst++ = idx*16;

		uint32_t bit = highBit;
		while(idx & bit)
			bit >>= 1;

		idx = idx + bit*2 - (uint32_t)n + bit;
	}
}

///////////////////////////////////////////////////////////////////////////

void ATFFTBase::InitImpl(ATFFTAllocator& allocator, uint32 N, bool imdct, bool optimizeForSpeed) {
	ReserveImpl(allocator, N, imdct, optimizeForSpeed);
	allocator.Finalize();
	BindImpl(allocator);
}

void ATFFTBase::ReserveImpl(ATFFTAllocator& allocator, uint32 N, bool imdct, bool optimizeForSpeed) {
	const int sizeBits = std::countr_zero((unsigned)N);

#ifdef ATFFT_USE_RADIX_4
	const int numStagesRadix4 = (sizeBits - 4) >> 1;
#else
	const int numStagesRadix4 = 0;
#endif

	const int numStagesRadix2 = sizeBits - 4 - numStagesRadix4 * 2;

	mFFTSizeBits = sizeBits;
	mNumRadix4Stages = numStagesRadix4;
	mNumRadix2Stages = numStagesRadix2;
	mbIMDCT = imdct;

	bool useVec8 = false;

#if defined(ATFFT_USE_SSE2)
	mbUseAVX2 = false;

	if (N >= 32 && optimizeForSpeed && VDCheckAllExtensionsEnabled(VDCPUF_SUPPORTS_AVX | VDCPUF_SUPPORTS_AVX2 | VDCPUF_SUPPORTS_FMA)) {
		mbUseAVX2 = true;
		useVec8 = true;
	}
#endif

	const int N2 = N >> 1;
	const int N4 = N >> 2;
	const int N16 = N >> 4;
	int step = N16;
	
	size_t tableIdx = 0;

	if (imdct) {
		if constexpr (kUseVec4) {
			if (useVec8)
				mStageTables[tableIdx++].mTableIndex = allocator.AllocateTable(ATFFTTableType::ImdctTwiddleVec8, N2);
			else
				mStageTables[tableIdx++].mTableIndex = allocator.AllocateTable(ATFFTTableType::ImdctTwiddleVec4, N2);
		} else
			mStageTables[tableIdx++].mTableIndex = allocator.AllocateTable(ATFFTTableType::ImdctTwiddle, N2);
	}

	// radix-4
	for(int i = 0; i < numStagesRadix4; ++i) {
		step >>= 1;

		if constexpr (kUseVec4) {
			if (useVec8)
				mStageTables[tableIdx++].mTableIndex = allocator.AllocateTable(ATFFTTableType::Radix4TwiddleNeg90Vec8, N4 / step);
			else
				mStageTables[tableIdx++].mTableIndex = allocator.AllocateTable(ATFFTTableType::Radix4TwiddleNeg90Vec4, N4 / step);
		} else
			mStageTables[tableIdx++].mTableIndex = allocator.AllocateTable(ATFFTTableType::Radix4TwiddleNeg90, N4 / step);

		step >>= 1;
	}

	// radix-2
	for(int i = 0; i < numStagesRadix2; ++i) {
		if constexpr (kUseVec4) {
			if (useVec8)
				mStageTables[tableIdx++].mTableIndex = allocator.AllocateTable(ATFFTTableType::TwiddleNeg180Vec8, N2 / step);
			else
				mStageTables[tableIdx++].mTableIndex = allocator.AllocateTable(ATFFTTableType::TwiddleNeg180Vec4, N2 / step);
		} else
			mStageTables[tableIdx++].mTableIndex = allocator.AllocateTable(ATFFTTableType::TwiddleNeg180, N2 / step);

		step >>= 1;
	}

	if (!imdct) {
		if constexpr (kUseVec4) {
			if (useVec8)
				mStageTables[tableIdx++].mTableIndex = allocator.AllocateTable(ATFFTTableType::TwiddleNeg180HalfVec8, N2);
			else
				mStageTables[tableIdx++].mTableIndex = allocator.AllocateTable(ATFFTTableType::TwiddleNeg180HalfVec4, N2);
		} else
			mStageTables[tableIdx++].mTableIndex = allocator.AllocateTable(ATFFTTableType::TwiddleNeg180, N2);
	}

	mBitRevTable.mTableIndex = allocator.AllocateTable(ATFFTTableType::BitReverseX16, N16);

	// We allocate an additional 8 complex elements (64 bytes) to make R2C/C2R easier by
	// allowing load/store over.
	allocator.ReserveWorkspace(sizeof(float) * (N + 16) * (imdct ? 2 : 1));
}

void ATFFTBase::BindImpl(const ATFFTAllocator& allocator) {
	mpWorkArea = (float *)allocator.GetWorkspace(0);

	size_t numStages = mNumRadix4Stages + mNumRadix2Stages + 1;
	for(size_t i = 0; i < numStages; ++i)
		mStageTables[i].mpFloatTable = (float *)allocator.GetTable(mStageTables[i].mTableIndex);

	mBitRevTable.mpIntTable = (uint32 *)allocator.GetTable(mBitRevTable.mTableIndex);
}

void ATFFTBase::ForwardImpl(float *dst, const float *src) {
	const int log2N = mFFTSizeBits;
	const int N = 1 << log2N;
	float* const work = mpWorkArea;

	// FFT to IMDCT conversion
	const ATFFTTableReference* stageTables = mStageTables;
	if (mbIMDCT) {
		float *tmp = work + N + 16;

		#if defined(ATFFT_USE_NEON)
			ATFFT_IMDCT_PreTransform_NEON(tmp, src, stageTables++->mpFloatTable, N);
		#elif defined(ATFFT_USE_SSE2)
			if (mbUseAVX2)
				ATFFT_IMDCT_PreTransform_AVX2(tmp, src, stageTables++->mpFloatTable, N);
			else
				ATFFT_IMDCT_PreTransform_SSE2(tmp, src, stageTables++->mpFloatTable, N);
		#else
			ATFFT_IMDCT_PreTransform_Scalar(tmp, src, stageTables++->mpFloatTable, N);
		#endif

		src = tmp;
	}

	// run initial radix-8 stage, including bit reversal reordering
	#if defined(ATFFT_USE_NEON)
		ATFFT_DIT_Radix8_NEON(work, src, mBitRevTable.mpIntTable, N);
	#elif defined(ATFFT_USE_SSE2)
		if (mbUseAVX2)
			ATFFT_DIT_Radix8_AVX2(work, src, mBitRevTable.mpIntTable, N);
		else
			ATFFT_DIT_Radix8_SSE2(work, src, mBitRevTable.mpIntTable, N);
	#else
		ATFFT_DIT_Radix8_Scalar(work, src, mBitRevTable.mpIntTable, N);
	#endif

	// run radix-4 stages
	int lstep = 4;

	for(size_t i=0; i<mNumRadix4Stages; ++i) {
		#if defined(ATFFT_USE_NEON)
			ATFFT_DIT_Radix4_NEON(work, (stageTables++)->mpFloatTable, N, lstep);
		#elif defined(ATFFT_USE_SSE2)
			if (mbUseAVX2)
				ATFFT_DIT_Radix4_AVX2(work, (stageTables++)->mpFloatTable, N, lstep);
			else
				ATFFT_DIT_Radix4_SSE2(work, (stageTables++)->mpFloatTable, N, lstep);
		#endif

		lstep += 2;
	}
		
	// run radix-2 stages
	for(size_t i=0; i<mNumRadix2Stages; ++i) {
		#if defined(ATFFT_USE_SSE2)
			if (mbUseAVX2)
				ATFFT_DIT_Radix2_AVX2(work, (stageTables++)->mpFloatTable, N, lstep);
			else
				ATFFT_DIT_Radix2_SSE2(work, (stageTables++)->mpFloatTable, N, lstep);
		#elif defined(ATFFT_USE_NEON)
			ATFFT_DIT_Radix2_NEON(work, (stageTables++)->mpFloatTable, N, lstep);
		#else
			ATFFT_DIT_Radix2_Scalar(work, (stageTables++)->mpFloatTable, N, lstep);
		#endif

		++lstep;
	}

	if (mbIMDCT) {
		// FFT to IMDCT conversion
		#if defined(ATFFT_USE_NEON)
			ATFFT_IMDCT_PostTransform_NEON(dst, work, mStageTables[0].mpFloatTable, N);
		#elif defined(ATFFT_USE_SSE2)
			if (mbUseAVX2)
				ATFFT_IMDCT_PostTransform_AVX2(dst, work, mStageTables[0].mpFloatTable, N);
			else
				ATFFT_IMDCT_PostTransform_SSE2(dst, work, mStageTables[0].mpFloatTable, N);
		#else
			ATFFT_IMDCT_PostTransform_Scalar(dst, work, mStageTables[0].mpFloatTable, N);
		#endif
	} else {
		// final reordering for real-to-complex transform
		#if defined(ATFFT_USE_NEON)
			ATFFT_DIT_R2C_NEON(dst, work, (stageTables++)->mpFloatTable, N);
		#elif defined(ATFFT_USE_SSE2)
			if (mbUseAVX2)
				ATFFT_DIT_R2C_AVX2(dst, work, (stageTables++)->mpFloatTable, N);
			else
				ATFFT_DIT_R2C_SSE2(dst, work, (stageTables++)->mpFloatTable, N);
		#else
			ATFFT_DIT_R2C_Scalar(dst, work, (stageTables++)->mpFloatTable, N);
		#endif
	}
}

void ATFFTBase::InverseImpl(float *dst, const float *src) {
	// initial complex-to-real conversion
	const int log2N = mFFTSizeBits;
	const int N = 1 << log2N;
	float *const work = mpWorkArea;
	const ATFFTTableReference* stageTablesEnd = mStageTables + mNumRadix4Stages + mNumRadix2Stages + 1;

	#ifdef ATFFT_USE_NEON
		ATFFT_DIF_C2R_NEON(work, src, (--stageTablesEnd)->mpFloatTable, N);
	#elif defined(ATFFT_USE_SSE2)
		if (mbUseAVX2)
			ATFFT_DIF_C2R_AVX2(work, src, (--stageTablesEnd)->mpFloatTable, N);
		else
			ATFFT_DIF_C2R_SSE2(work, src, (--stageTablesEnd)->mpFloatTable, N);
	#else
		ATFFT_DIF_C2R_Scalar(work, src, (--stageTablesEnd)->mpFloatTable, N);
	#endif

	// decimation in frequency (DIF) loops

	// run radix-2 stages
	int lstep = log2N - 1;

	for(size_t i=0; i<mNumRadix2Stages; ++i) {
		#if defined(ATFFT_USE_SSE2)
			if (mbUseAVX2)
				ATFFT_DIF_Radix2_AVX2(work, (--stageTablesEnd)->mpFloatTable, N, lstep);
			else
				ATFFT_DIF_Radix2_SSE2(work, (--stageTablesEnd)->mpFloatTable, N, lstep);
		#elif defined(ATFFT_USE_NEON)
			ATFFT_DIF_Radix2_NEON(work, (--stageTablesEnd)->mpFloatTable, N, lstep);
		#else
			ATFFT_DIF_Radix2_Scalar(work, (--stageTablesEnd)->mpFloatTable, N, lstep);
		#endif
		
		--lstep;
	}

	// run radix-4 stages
	for(size_t i=0; i<mNumRadix4Stages; ++i) {
		--lstep;

		#if defined(ATFFT_USE_NEON)
			ATFFT_DIF_Radix4_NEON(work, (--stageTablesEnd)->mpFloatTable, N, lstep);
		#elif defined(ATFFT_USE_SSE2)
			if (mbUseAVX2)
				ATFFT_DIF_Radix4_AVX2(work, (--stageTablesEnd)->mpFloatTable, N, lstep);
			else
				ATFFT_DIF_Radix4_SSE2(work, (--stageTablesEnd)->mpFloatTable, N, lstep);
		#endif

		--lstep;
	}

	#ifdef ATFFT_USE_NEON
		ATFFT_DIF_Radix8_NEON(dst, work, mBitRevTable.mpIntTable, N);
	#elif defined(ATFFT_USE_SSE2)
		if (mbUseAVX2)
			ATFFT_DIF_Radix8_AVX2(dst, work, mBitRevTable.mpIntTable, N);
		else
			ATFFT_DIF_Radix8_SSE2(dst, work, mBitRevTable.mpIntTable, N);
	#else
		ATFFT_DIF_Radix8_Scalar(dst, work, mBitRevTable.mpIntTable, N);
	#endif
}

void ATFFTBase::MultiplyAddImpl(float *dst, const float *src1, const float *src2, int N) {
	#if defined(ATFFT_USE_SSE2)
		if (mbUseAVX2)
			ATFFT_MultiplyAdd_AVX2(dst, src1, src2, N);
		else
			ATFFT_MultiplyAdd_SSE2(dst, src1, src2, N);
	#else
		ATFFT_MultiplyAdd_Scalar(dst, src1, src2, N);
	#endif
}
