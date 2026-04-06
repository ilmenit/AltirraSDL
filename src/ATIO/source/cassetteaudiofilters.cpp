//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
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

#include <stdafx.h>
#include <vd2/system/constexpr.h>
#include <vd2/system/cpuaccel.h>
#include <vd2/system/error.h>
#include <vd2/system/math.h>
#include <at/atcore/configvar.h>
#include <at/atio/audioreader.h>
#include <at/atio/cassetteaudiofilters.h>

ATConfigVarFloat g_ATCVTapeDecodeCompensationRate("tape.decode.compensation_rate", 0.05f);
ATConfigVarFloat g_ATCVTapeDecodeCompensationThreshold("tape.decode.compensation_threshold", 2.0f);

////////////////////////////////////////////////////////////////////////////////

ATCassetteAudioSource::ATCassetteAudioSource(IATAudioReader& reader)
	: mAudioReader(reader)
{
}

uint32 ATCassetteAudioSource::GetLastDataPosKB() const {
	return mDataPos;
}

uint32 ATCassetteAudioSource::ReadAudio(sint16 (*dst)[2], uint32 n) {
	auto actual = mAudioReader.ReadStereo16(&dst[0][0], n);

	mDataPos = (uint32)(mAudioReader.GetDataPos() >> 10);

	return actual;
}

////////////////////////////////////////////////////////////////////////////////

namespace {
#if VD_CPU_X86 || VD_CPU_X64
	void minMax16x2_SSE2(const sint16 * VDRESTRICT src, uint32 n, sint32& minvL, sint32& maxvL, sint32& minvR, sint32& maxvR) {
		// We do unaligned loads from this array, so it's important that we
		// avoid data cache unit (DCU) split penalties on older CPUs.
		static const VDALIGN(64) uint64 window_table[6] = {
			0, 0, (uint64)0 - 1, (uint64)0 - 1, 0, 0
		};

		const __m128i * VDRESTRICT src128 = (const __m128i *)((uintptr)src & ~(uintptr)15);
		const __m128i * VDRESTRICT srcend128 = (const __m128i *)((uintptr)(src + n*2) & ~(uintptr)15);
		const ptrdiff_t leftOffset = (ptrdiff_t)((uintptr)src & 15);
		const __m128i leftMask = _mm_loadu_si128((const __m128i *)((const char *)window_table + 16 - leftOffset));
		const ptrdiff_t rightOffset = (ptrdiff_t)((uintptr)(src + n * 2) & 15);
		const __m128i rightMask = _mm_loadu_si128((const __m128i *)((const char *)window_table + 32 - rightOffset));

		__m128i minAcc = _mm_insert_epi16(_mm_cvtsi32_si128(minvL), minvR, 1);
		__m128i maxAcc = _mm_insert_epi16(_mm_cvtsi32_si128(maxvL), maxvR, 1);

		if (src128 != srcend128) {
			__m128i vleft = _mm_and_si128(*src128++, leftMask);
			minAcc = _mm_min_epi16(minAcc, vleft);
			maxAcc = _mm_max_epi16(maxAcc, vleft);

			while(src128 != srcend128) {
				__m128i vmid = *src128++;

				minAcc = _mm_min_epi16(minAcc, vmid);
				maxAcc = _mm_max_epi16(maxAcc, vmid);
			}

			if (rightOffset) {
				__m128i vright = _mm_and_si128(*src128, rightMask);
				minAcc = _mm_min_epi16(minAcc, vright);
				maxAcc = _mm_max_epi16(maxAcc, vright);
			}
		} else {
			__m128i v = _mm_and_si128(src128[0], _mm_and_si128(leftMask, rightMask));

			minAcc = _mm_min_epi16(minAcc, v);
			maxAcc = _mm_max_epi16(maxAcc, v);
		}

		// four four accumulators
		minAcc = _mm_min_epi16(minAcc, _mm_shuffle_epi32(minAcc, 0xEE));
		maxAcc = _mm_max_epi16(maxAcc, _mm_shuffle_epi32(maxAcc, 0xEE));
		minAcc = _mm_min_epi16(minAcc, _mm_shuffle_epi32(minAcc, 0x55));
		maxAcc = _mm_max_epi16(maxAcc, _mm_shuffle_epi32(maxAcc, 0x55));

		minvL = (sint16)_mm_extract_epi16(minAcc, 0);
		minvR = (sint16)_mm_extract_epi16(minAcc, 1);
		maxvL = (sint16)_mm_extract_epi16(maxAcc, 0);
		maxvR = (sint16)_mm_extract_epi16(maxAcc, 1);
	}
#endif

	void minMax16x2_scalar(const sint16 * VDRESTRICT src, uint32 n, sint32& minvL, sint32& maxvL, sint32& minvR, sint32& maxvR) {
		for(uint32 i=0; i<n; ++i) {
			sint32 vL = src[0];
			sint32 vR = src[1];
			src += 2;

			minvL = std::min(minvL, vL);
			maxvL = std::max(maxvL, vL);
			minvR = std::min(minvR, vR);
			maxvR = std::max(maxvR, vR);
		}
	}

	void minMax16x2(const sint16 * VDRESTRICT src, uint32 n, sint32& minvL, sint32& maxvL, sint32& minvR, sint32& maxvR) {
#if VD_CPU_X86 || VD_CPU_X64
		if (SSE2_enabled)
			minMax16x2_SSE2(src, n, minvL, maxvL, minvR, maxvR);
		else
#endif
			minMax16x2_scalar(src, n, minvL, maxvL, minvR, maxvR);
	}
}

ATCassetteAudioPeakMapFilter::ATCassetteAudioPeakMapFilter(IATCassetteAudioSource& source, bool stereo, double inputSamplesPerPeakSample, vdfastvector<uint8>& peakMapL, vdfastvector<uint8>& peakMapR)
	: mSource(source)
	, mbStereo(stereo)
	, mRateAccumInc((uint64)VDRoundToInt64(inputSamplesPerPeakSample * 4294967296.0))
	, mPeakMapL(peakMapL)
	, mPeakMapR(peakMapR)
{
	mRateAccum += mRateAccumInc;
	mInputSamplesLeft = (uint32)(mRateAccum >> 32);
	mRateAccum = (uint32)mRateAccum;
}

uint32 ATCassetteAudioPeakMapFilter::ReadAudio(sint16 (*dst)[2], uint32 n) {
	const uint32 actual = mSource.ReadAudio(dst, n);

	ProcessPeaks(dst, actual);

	return actual;
}

void ATCassetteAudioPeakMapFilter::ProcessPeaks(const sint16 (*samples)[2], uint32 n) {
	while(n) {
		while(mInputSamplesLeft == 0) {
			mRateAccum += mRateAccumInc;
			mInputSamplesLeft = (uint32)(mRateAccum >> 32);
			mRateAccum = (uint32)mRateAccum;

			static constexpr float scale = 1.0f / 32767.0f * 127.0f / 255.0f;

			const uint8 vR0 = VDClampedRoundFixedToUint8Fast(mValAccumR0 * scale + 128.0f / 255.0f);
			const uint8 vR1 = VDClampedRoundFixedToUint8Fast(mValAccumR1 * scale + 128.0f / 255.0f);
			mPeakMapR.push_back(vR0);
			mPeakMapR.push_back(vR1);
			mValAccumR0 = 0;
			mValAccumR1 = 0;

			if (mbStereo) {
				const uint8 vL0 = VDClampedRoundFixedToUint8Fast(mValAccumL0 * scale + 128.0f / 255.0f);
				const uint8 vL1 = VDClampedRoundFixedToUint8Fast(mValAccumL1 * scale + 128.0f / 255.0f);
				mPeakMapL.push_back(vL0);
				mPeakMapL.push_back(vL1);
				mValAccumL0 = 0;
				mValAccumL1 = 0;
			}
		}

		// accumulate peak map samples
		uint32 toScan = n;
		if (toScan > mInputSamplesLeft)
			toScan = mInputSamplesLeft;

		minMax16x2(&samples[0][0], toScan, mValAccumL0, mValAccumL1, mValAccumR0, mValAccumR1);

		mInputSamplesLeft -= toScan;

		n -= toScan;
		samples += toScan;
	}
}

////////////////////////////////////////////////////////////////////////////////

namespace {
	VDALIGN(16) const sint16 kernel[33][8] = {
		{+0x0000,+0x0000,+0x0000,+0x4000,+0x0000,+0x0000,+0x0000,+0x0000 },
		{-0x000a,+0x0052,-0x0179,+0x3fe2,+0x019f,-0x005b,+0x000c,+0x0000 },
		{-0x0013,+0x009c,-0x02cc,+0x3f86,+0x0362,-0x00c0,+0x001a,+0x0000 },
		{-0x001a,+0x00dc,-0x03f9,+0x3eef,+0x054a,-0x012c,+0x002b,+0x0000 },
		{-0x001f,+0x0113,-0x0500,+0x3e1d,+0x0753,-0x01a0,+0x003d,+0x0000 },
		{-0x0023,+0x0141,-0x05e1,+0x3d12,+0x097c,-0x021a,+0x0050,-0x0001 },
		{-0x0026,+0x0166,-0x069e,+0x3bd0,+0x0bc4,-0x029a,+0x0066,-0x0001 },
		{-0x0027,+0x0182,-0x0738,+0x3a5a,+0x0e27,-0x031f,+0x007d,-0x0002 },
		{-0x0028,+0x0197,-0x07b0,+0x38b2,+0x10a2,-0x03a7,+0x0096,-0x0003 },
		{-0x0027,+0x01a5,-0x0807,+0x36dc,+0x1333,-0x0430,+0x00af,-0x0005 },
		{-0x0026,+0x01ab,-0x083f,+0x34db,+0x15d5,-0x04ba,+0x00ca,-0x0007 },
		{-0x0024,+0x01ac,-0x085b,+0x32b3,+0x1886,-0x0541,+0x00e5,-0x0008 },
		{-0x0022,+0x01a6,-0x085d,+0x3068,+0x1b40,-0x05c6,+0x0101,-0x000b },
		{-0x001f,+0x019c,-0x0846,+0x2dfe,+0x1e00,-0x0644,+0x011c,-0x000d },
		{-0x001c,+0x018e,-0x0819,+0x2b7a,+0x20c1,-0x06bb,+0x0136,-0x0010 },
		{-0x0019,+0x017c,-0x07d9,+0x28e1,+0x2380,-0x0727,+0x014f,-0x0013 },
		{-0x0016,+0x0167,-0x0788,+0x2637,+0x2637,-0x0788,+0x0167,-0x0016 },
		{-0x0013,+0x014f,-0x0727,+0x2380,+0x28e1,-0x07d9,+0x017c,-0x0019 },
		{-0x0010,+0x0136,-0x06bb,+0x20c1,+0x2b7a,-0x0819,+0x018e,-0x001c },
		{-0x000d,+0x011c,-0x0644,+0x1e00,+0x2dfe,-0x0846,+0x019c,-0x001f },
		{-0x000b,+0x0101,-0x05c6,+0x1b40,+0x3068,-0x085d,+0x01a6,-0x0022 },
		{-0x0008,+0x00e5,-0x0541,+0x1886,+0x32b3,-0x085b,+0x01ac,-0x0024 },
		{-0x0007,+0x00ca,-0x04ba,+0x15d5,+0x34db,-0x083f,+0x01ab,-0x0026 },
		{-0x0005,+0x00af,-0x0430,+0x1333,+0x36dc,-0x0807,+0x01a5,-0x0027 },
		{-0x0003,+0x0096,-0x03a7,+0x10a2,+0x38b2,-0x07b0,+0x0197,-0x0028 },
		{-0x0002,+0x007d,-0x031f,+0x0e27,+0x3a5a,-0x0738,+0x0182,-0x0027 },
		{-0x0001,+0x0066,-0x029a,+0x0bc4,+0x3bd0,-0x069e,+0x0166,-0x0026 },
		{-0x0001,+0x0050,-0x021a,+0x097c,+0x3d12,-0x05e1,+0x0141,-0x0023 },
		{+0x0000,+0x003d,-0x01a0,+0x0753,+0x3e1d,-0x0500,+0x0113,-0x001f },
		{+0x0000,+0x002b,-0x012c,+0x054a,+0x3eef,-0x03f9,+0x00dc,-0x001a },
		{+0x0000,+0x001a,-0x00c0,+0x0362,+0x3f86,-0x02cc,+0x009c,-0x0013 },
		{+0x0000,+0x000c,-0x005b,+0x019f,+0x3fe2,-0x0179,+0x0052,-0x000a },

		{+0x0000,+0x0000,+0x0000,+0x0000,+0x4000,+0x0000,+0x0000,+0x0000 },
	};

	uint64 resample16x2_scalar(sint16 *d, const sint16 *s, uint32 count, uint64 accum, sint64 inc) {
		do {
			const sint16 *s2 = s + (uint32)(accum >> 32)*2;
			const sint16 (*f)[8] = &kernel[(uint32)accum >> 27];

			sint32 frac = ((uint32)accum >> 12) & 0x7FFF;
			accum += inc;

			sint32 f0 = (sint32)f[0][0] + ((((sint32)f[1][0] - (sint32)f[0][0])*frac) >> 15);
			sint32 f1 = (sint32)f[0][1] + ((((sint32)f[1][1] - (sint32)f[0][1])*frac) >> 15);
			sint32 f2 = (sint32)f[0][2] + ((((sint32)f[1][2] - (sint32)f[0][2])*frac) >> 15);
			sint32 f3 = (sint32)f[0][3] + ((((sint32)f[1][3] - (sint32)f[0][3])*frac) >> 15);
			sint32 f4 = (sint32)f[0][4] + ((((sint32)f[1][4] - (sint32)f[0][4])*frac) >> 15);
			sint32 f5 = (sint32)f[0][5] + ((((sint32)f[1][5] - (sint32)f[0][5])*frac) >> 15);
			sint32 f6 = (sint32)f[0][6] + ((((sint32)f[1][6] - (sint32)f[0][6])*frac) >> 15);
			sint32 f7 = (sint32)f[0][7] + ((((sint32)f[1][7] - (sint32)f[0][7])*frac) >> 15);

			sint32 l= (sint32)s2[ 0]*f0
					+ (sint32)s2[ 2]*f1
					+ (sint32)s2[ 4]*f2
					+ (sint32)s2[ 6]*f3
					+ (sint32)s2[ 8]*f4
					+ (sint32)s2[10]*f5
					+ (sint32)s2[12]*f6
					+ (sint32)s2[14]*f7
					+ 0x20002000;

			sint32 r= (sint32)s2[ 1]*f0
					+ (sint32)s2[ 3]*f1
					+ (sint32)s2[ 5]*f2
					+ (sint32)s2[ 7]*f3
					+ (sint32)s2[ 9]*f4
					+ (sint32)s2[11]*f5
					+ (sint32)s2[13]*f6
					+ (sint32)s2[15]*f7
					+ 0x20002000;

			l >>= 14;
			r >>= 14;

			if ((uint32)l >= 0x10000)
				l = ~l >> 31;
			if ((uint32)r >= 0x10000)
				r = ~r >> 31;

			d[0] = (sint16)(l - 0x8000);
			d[1] = (sint16)(r - 0x8000);
			d += 2;
		} while(--count);

		return accum;
	}

#if VD_CPU_X86 || VD_CPU_X64
	uint64 resample16x2_SSE2(sint16 *d, const sint16 *s, uint32 count, uint64 accum, sint64 inc) {
		__m128i round = _mm_set1_epi32(0x2000);

		do {
			const __m128i *VDRESTRICT s2 = (const __m128i *)(s + (size_t)(accum >> 32)*2);
			const __m128i *VDRESTRICT f = (const __m128i *)kernel[(uint32)accum >> 27];

			__m128i frac = _mm_shufflelo_epi16(_mm_cvtsi32_si128((accum >> 12) & 0x7FFF), 0);
			__m128i cdiff = _mm_mulhi_epi16(_mm_sub_epi16(f[1], f[0]), _mm_shuffle_epi32(frac, 0));
			__m128i coeff16 = _mm_add_epi16(f[0], _mm_add_epi16(cdiff, cdiff));

			accum += inc;

			__m128i x0 = _mm_loadu_si128(s2);
			__m128i x1 = _mm_loadu_si128(s2 + 1);

			__m128i y0 = _mm_shufflehi_epi16(_mm_shufflelo_epi16(x0, 0xd8), 0xd8);
			__m128i y1 = _mm_shufflehi_epi16(_mm_shufflelo_epi16(x1, 0xd8), 0xd8);

			__m128i z0 = _mm_madd_epi16(y0, _mm_shuffle_epi32(coeff16, 0x50));
			__m128i z1 = _mm_madd_epi16(y1, _mm_shuffle_epi32(coeff16, 0xfa));

			__m128i a = _mm_add_epi32(z0, z1);
			__m128i b = _mm_add_epi32(a, _mm_shuffle_epi32(a, 0xee));
			__m128i r = _mm_srai_epi32(_mm_add_epi32(b, round), 14);

			__m128i result = _mm_packs_epi32(r, r);

			*(int *)d = _mm_cvtsi128_si32(result);
			d += 2;
		} while(--count);

		return accum;
	}
#endif

#if VD_CPU_ARM64
	uint64 resample16x2_NEON(sint16 *d, const sint16 *s, uint32 count, uint64 accum, sint64 inc) {
		do {
			const sint16 *VDRESTRICT s2 = s + (uint32)(accum >> 32)*2;
			const sint16 (*VDRESTRICT f)[8] = &kernel[(uint32)accum >> 27];
			const int16x8_t c0 = vld1q_s16(f[0]);
			const int16x8_t c1 = vld1q_s16(f[1]);

			uint32 frac = ((uint32)accum >> 12) & 0x7FFF;
			accum += inc;

			// vqrdmlahq_s16() would be perfect here, but unfortunately it requires ARMv8.1.
			int16x8_t coeff16 = vaddq_s16(c0, vqrdmulhq_s16(vsubq_s16(c1, c0), vmovq_n_s16(frac)));

			const int16x8x2_t x = vld2q_s16(s2 + 0);

			int32x4_t z0 = vmlal_high_s16(vmull_s16(vget_low_s16(x.val[0]), vget_low_s16(coeff16)), x.val[0], coeff16);
			int32x4_t z1 = vmlal_high_s16(vmull_s16(vget_low_s16(x.val[1]), vget_low_s16(coeff16)), x.val[1], coeff16);

			int16x4x2_t a;
			a.val[0] = vrshrn_n_s32(vmovq_n_s32(vaddvq_s32(z0)), 14);
			a.val[1] = vrshrn_n_s32(vmovq_n_s32(vaddvq_s32(z1)), 14);

			vst2_lane_s16(d, a, 0);
			d += 2;
		} while(--count);

		return accum;
	}
#endif

	uint64 resample16x2(sint16 *d, const sint16 *s, uint32 count, uint64 accum, sint64 inc) {
#if VD_CPU_ARM64
		return resample16x2_NEON(d, s, count, accum, inc);
#elif VD_CPU_X86 || VD_CPU_X64
		if (SSE2_enabled)
			return resample16x2_SSE2(d, s, count, accum, inc);
		else
			return resample16x2_scalar(d, s, count, accum, inc);
#else
			return resample16x2_scalar(d, s, count, accum, inc);
#endif
	}
}

ATCassetteAudioResampler::ATCassetteAudioResampler(IATCassetteAudioSource& source, uint64 sampleStepF32)
	: mSource(source)
	, mSampleAccum(0)
	, mSampleStep(sampleStepF32)
{
	// Pre-fill the input buffer with 3 samples to center the window.
	mInputBufferLevel = 3;
}

uint32 ATCassetteAudioResampler::ReadAudio(sint16 (*dst)[2], uint32 n) {
	if (mbEndOfOutputStream)
		return 0;

	uint32 actual = 0;
	while(n) {
		// Compute how many samples can be processed given the input buffer
		// available.
		const auto computeMax = [](uint32 acc, uint64 step, uint32 inputLen, uint32 inputOffset) -> uint32 {
			// early out if we don't have enough to read even a single sample
			if (inputLen < inputOffset || inputLen - inputOffset < 8)
				return 0;

			const uint64 maxAccum = ((uint64)(inputLen - inputOffset - 7) << 32) - 1;

			return (uint32)((maxAccum - acc) / step) + 1;
		};

		static_assert(computeMax(0x0'00000000, 0x1'00000000, 8, 0) == 1);
		static_assert(computeMax(0x0'00000000, 0x0'10000000, 8, 0) == 16);
		static_assert(computeMax(0x0'0FFFFFFF, 0x0'10000000, 8, 0) == 16);
		static_assert(computeMax(0x0'10000000, 0x0'10000000, 8, 0) == 15);
		static_assert(computeMax(0x0'00000000, 0x1'00000000, 9, 0) == 2);
		static_assert(computeMax(0x0'00000000, 0x0'10000000, 9, 0) == 32);
		static_assert(computeMax(0x0'0FFFFFFF, 0x0'10000000, 9, 0) == 32);
		static_assert(computeMax(0x0'10000000, 0x0'10000000, 9, 0) == 31);

		const uint32 outputAvail = computeMax(mSampleAccum, mSampleStep, mInputBufferLevel, mInputBufferOffset);

		// Check if we can't run any samples, in which case we must refill the
		// buffer.
		if (!outputAvail) {
			if (mbEndOfSourceStream) {
				mbEndOfOutputStream = true;
				break;
			}

			// Determine how many samples that we're going to keep. This may be
			// zero if the filter is stepping very fast (which would be suboptimal
			// due to aliasing but is possible).
			const uint32 samplesToKeep = std::max<uint32>(mInputBufferOffset, mInputBufferLevel) - mInputBufferOffset;

			// Shift the remaining samples down and adjust tracking parameters.
			const uint32 samplesToRemove = mInputBufferLevel - samplesToKeep;

			if (samplesToKeep)
				memmove(&mInputBuffer[0], &mInputBuffer[samplesToRemove], sizeof(mInputBuffer[0]) * samplesToKeep);

			mInputBufferOffset -= samplesToRemove;
			mInputBufferLevel = samplesToKeep;

			// We should be able to read a full input chunk now.
			if (mInputBufferLevel > 16)
				VDRaiseInternalFailure();

			const uint32 samplesRead = mSource.ReadAudio(&mInputBuffer[mInputBufferLevel], kInputBufferSize);
			mInputBufferLevel += samplesRead;

			// check if we hit the end of the source stream
			if (!samplesRead) {
				mbEndOfSourceStream = true;

				// Add an additional four samples at the end.
				mInputBufferLevel += 4;
			}

			continue;
		}

		// compute how many samples we're going to run
		const uint32 tc = std::min<uint32>(outputAvail, n);

		// run the resampler
		const uint64 sampleAccum2 = resample16x2(dst[0], mInputBuffer[mInputBufferOffset], tc, mSampleAccum, mSampleStep);
		dst += tc;
		n -= tc;
		actual += tc;

		mInputBufferOffset += (uint32)(sampleAccum2 >> 32);
		mSampleAccum = (uint32)sampleAccum2;
	}

	return actual;
}

////////////////////////////////////////////////////////////////////////////////

struct ATCassetteAudioFSKSpeedCompensator::RotationTable {
	sint32 kRotations[72][2][10+2];
};

const constinit ATCassetteAudioFSKSpeedCompensator::RotationTable ATCassetteAudioFSKSpeedCompensator::kRotationTable =
[]() -> RotationTable {
	RotationTable table {};

	sint32 baseRotTable[72];
	for(int i=0; i<72; ++i)
		baseRotTable[i] = VDCxFloor(0.5 + 256 * VDCxSinPiD((float)i / 36.0f));

	for(int i=0; i<72; ++i) {
		auto& rotLine = table.kRotations[i];

		for(int j=0; j<10; ++j) {
			rotLine[0][j] = baseRotTable[(i * (6 + j) + 18) % 72];
			rotLine[1][j] = baseRotTable[(i * (6 + j) +  0) % 72];
		}
	}

	return table;
}();

ATCassetteAudioFSKSpeedCompensator::ATCassetteAudioFSKSpeedCompensator(IATCassetteAudioSource& source)
	: mSource(source)
{
	// Pre-pad window with 72 zero samples
	mInputLevel = 72;

	for(int i=0; i<72; ++i)
		mRateWindow[i] = 0x10000;
}

uint32 ATCassetteAudioFSKSpeedCompensator::ReadAudio(sint16 (*dst)[2], uint32 n) {
	if (mbOutputEnded)
		return 0;

	uint32 actual = 0;
	while(n) {
		// fill up the input window if possible
		if (!mbSourceEnded && mInputLevel < kFullWindowSize) {
			if (mInputLevel < kFFTSize)
				VDRaiseInternalFailure();

			uint32 readCount = mSource.ReadAudio(&mInputWindow[mInputLevel], kFullWindowSize - mInputLevel);

			if (readCount) {
				AnalyzeSamples(mInputLevel - kFFTSize, readCount);
				mInputLevel += readCount;
			} else
				mbSourceEnded = true;
		}

		// produce as many output samples as we can
		uint32 n0 = n;

		if (mInputLevel > kFFTSize) {
			const uint32 limitPos = mInputLevel - kFFTSize;

			while(n && (uint32)(mOutputAccum >> 32) < limitPos) {
				(void)resample16x2(&dst[0][0], &mInputWindow[0][0], 1, (uint64)mOutputAccum, 0);
				mOutputAccum += (uint64)mRateWindow[(uint32)(mOutputAccum >> 32)] << 16;

				++dst;
				--n;
			}
		}

		// check if anything got output
		if (n0 == n) {
			if (mbSourceEnded) {
				mbOutputEnded = true;
				break;
			}

			if (mInputLevel < kFFTSize)
				VDRaiseInternalFailure();

			// slide down the input window
			const uint32 basePos = std::min<uint32>(mInputLevel - kFFTSize, (uint32)(mOutputAccum >> 32));
			
			// panic if we're trying to consume more than we have
			if (basePos > mInputLevel)
				VDRaiseInternalFailure();

			// panic if we can't slide
			if (!basePos)
				VDRaiseInternalFailure();

			memmove(&mInputWindow[0], &mInputWindow[basePos], sizeof(mInputWindow[0]) * (mInputLevel - basePos));
			memmove(&mRateWindow[0], &mRateWindow[basePos], sizeof(mRateWindow[0]) * (mInputLevel - basePos));

			mInputLevel -= basePos;
			mOutputAccum -= (uint64)basePos << 32;

			if (mInputLevel < kFFTSize)
				VDRaiseInternalFailure();
		}

		actual += n0 - n;
	}

	return actual;
}

void ATCassetteAudioFSKSpeedCompensator::AnalyzeSamples(uint32 pos, uint32 n) {
	uint32 winIdx = mWindowOffset;

	// FSK speed compensation procedure:
	//
	// * Compute 72-point sliding DFT where ideally the FSK tones fall on bins
	//   9 and 12.
	//
	//   - This is done in fixed point so the accumulation is exact.
	//   - A total of 10 bins are extracted, bins 7-14.
	//
	// * Window the bins in frequency space.
	//
	//   - Blackman window is used.
	//   - This requires two additional bins.
	//
	// * Compute the bins to magnitude and linearly interpolate them according
	//   to the current speed adjustment.
	//
	//   - This requires another two additional bins, for a total of 10 raw
	//     bins.
	//
	// * When enough energy is concentrated in one side over the other, examine
	//   the skew in the three interpolated bins for the active tone, and nudge
	//   the rate higher or lower to correct it.
	//
	// * The running rate values are then used to reinterpolate the audio.
	//
	// One subtlety is that the speed detection algorithm is itself subject to
	// its own rate control. This allows its mark/space tracking bins to shift
	// to track speed drift beyond the halfway point between the tones.

	float bins[2][10 + 2] {};
	float binmags[8 + 4] {};
	float rateAccum = mRateAccum;

	const float compRate = std::clamp<float>(g_ATCVTapeDecodeCompensationRate, 0.0f, 1.0f);
	const float compThreshold = g_ATCVTapeDecodeCompensationThreshold;

	auto& VDRESTRICT fftBins = mFFTBins;

	for(uint32 i = 0; i < n; ++i) {
		const sint32 x = mInputWindow[pos + i + kFFTSize][1];
		sint32 dx = x - mInputWindow[pos + i][1];

		const auto& VDRESTRICT rotLine = kRotationTable.kRotations[winIdx];

		for(int j=0; j<10; ++j) {
			const sint32 wr = rotLine[0][j];
			const sint32 wi = rotLine[1][j];

			// update raw FFT bin
			fftBins[0][j] += dx * wr;
			fftBins[1][j] += dx * wi;
		}

		// +2 dummy values to allow for vectorization; we only do this here
		// because doing it above requires SSE4.1
		for(int j=0; j<10+2; ++j) {
			const float rawrf = (float)fftBins[0][j];
			const float rawif = (float)fftBins[1][j];

			// counter rotate bin back to aligned rotation
			const sint32 wr = rotLine[0][j];
			const sint32 wi = rotLine[1][j];
			const float wrf = (float)wr;
			const float wif = (float)wi;

			bins[0][j] = rawrf*wrf + rawif*wif;
			bins[1][j] = rawif*wrf - rawrf*wif;
		}

		// window in frequency space with a Blackman window and compute magnitudes of bins 7-14
		static constexpr float undoFixedScale = 1.0f / (32767.0f * 256.0f * 256.0f * 72.0f);
		static constexpr float a0 = 0.54f * undoFixedScale;
		static constexpr float a1 = 0.46f * 0.5f * undoFixedScale;

		for(int j=0; j<8; ++j) {
			const float binr = bins[0][j+1]*a0 - (bins[0][j+0] + bins[0][j+2])*a1;
			const float bini = bins[1][j+1]*a0 - (bins[1][j+0] + bins[1][j+2])*a1;
			const float binmag = binr*binr + bini*bini;

			binmags[j + 2] = binmag;
		}

		// interpolate magnitudes to sample bits 8-13 with rate correction
		float ibinmags[6];
		float invRateAccum = 1.0f / rateAccum;

		for(int j=0; j<6; ++j) {
			float pos = (float)(j + 8.0f) * invRateAccum - 5.0f;
			int ipos = (int)pos;
			float frac = pos - (float)ipos;

			ibinmags[j] = binmags[ipos] * (1.0f - frac) + binmags[ipos+1] * frac;
		}

		// compute mark/space energy
		float spaceEnergy = ibinmags[0] + ibinmags[1] + ibinmags[2];
		float markEnergy = ibinmags[3] + ibinmags[4] + ibinmags[5];
		float error = 0.0f;

		if (markEnergy + spaceEnergy > 0.001f) {
			if (spaceEnergy > markEnergy * compThreshold) {
				// compute balance over bins 8-10
				error = 0.10f * (ibinmags[0] - ibinmags[2]) / spaceEnergy;
			} else if (markEnergy > spaceEnergy * compThreshold) {
				// compute balance over bins 11-13
				error = 0.09f * (ibinmags[3] - ibinmags[5]) / markEnergy;
			}
		}

		rateAccum += error * compRate;

		// Clamp the rate accumulator. We do this not only for safety reasons, but
		// also to prevent the bin interpolator from going outside of the range of
		// bins computed. This actually allows going slightly outside, which works
		// better as long as we don't go beyond the zero padding elements in the
		// array.
		rateAccum = std::min(std::max(rateAccum, 0.90f), 1.10f);

		const sint32 irate = (sint32)(0.5f + 65536.0f * rateAccum);

		mRateWindow[pos + i + kFFTSize/2] = irate;

		if (++winIdx >= kFFTSize)
			winIdx = 0;
	}

	mWindowOffset = winIdx;
	mRateAccum = rateAccum;
}

////////////////////////////////////////////////////////////////////////////////

ATCassetteAudioCrosstalkCanceller::ATCassetteAudioCrosstalkCanceller(IATCassetteAudioSource& source)
	: mSource(source)
{
	// The gain of a cos^6 window (Hann^3) is 5/16, but we are applying it 8 times.
	// We also have a 1/1024 factor from the FFT to cancel, then we need to take
	// the cube root since we'll be cubing the window.
	float scale = 0.07310044345532165163766968400345f;
	for(int i = 0; i < 2048; ++i) {
		float y = 0.5f - 0.5f * cosf((float)i * (nsVDMath::kfTwoPi / 2048));

		mFFTWindow[i] = y * scale;
	}
}

uint32 ATCassetteAudioCrosstalkCanceller::ReadAudio(sint16 (*dst)[2], uint32 n) {
	uint32 read = 0;

	while(n) {
		// read the remainder of the block
		if (mOutputLeft) {
			uint32 tc = std::min<uint32>(n, mOutputLeft);

			const float *src1 = &mOutputWindow[mOutputOffset];
			const sint16 (*src2)[2] = &mInputWindow[mOutputOffset];
			for(uint32 i=0; i<tc; ++i) {
				dst[0][0] = (sint16)std::max(std::min(*src1++, 32767.0f), -32768.0f);
				dst[0][1] = (*src2++)[1];
				++dst;
			}

			mOutputOffset = (mOutputOffset + tc) & 2047;
			mOutputLeft -= tc;
			n -= tc;
			read += tc;
			continue;
		}

		sint16 (*inblock)[2] = &mInputWindow[(mWindowOffset + 7*256) & 2047];
		uint32 actual = mSource.ReadAudio(inblock, 256);

		if (actual < 256) {
			if (!mTailBlocksLeft)
				break;

			--mTailBlocksLeft;

			memset(inblock + actual, 0, sizeof(inblock[0]) * (256 - actual));
		}

		mSamplesPending += actual;

		ProcessBlock();

		mWindowOffset = (mWindowOffset + 256) & 2047;

		if (mPrerollBlocks) {
			--mPrerollBlocks;
			continue;
		}

		mOutputLeft = std::min<uint32>(mSamplesPending, 256);
		mSamplesPending -= mOutputLeft;
	}

	return read;
}

void ATCassetteAudioCrosstalkCanceller::ProcessBlock() {
	for(int i=0; i<8; ++i) {
		float *VDRESTRICT dstL = &mWorkLeft[i * 256];
		float *VDRESTRICT dstR = &mWorkRight[i * 256];
		const float *VDRESTRICT window = &mFFTWindow[i * 256];
		const sint16 (*VDRESTRICT src)[2] = &mInputWindow[(mWindowOffset + i * 256) & 2047];

		for(size_t i=0; i<256; ++i) {
			const float w = *window++;
			*dstL++ = (float)(*src)[0] * w;
			*dstR++ = (float)(*src)[1] * w;
			++src;
		}
	}

	mFFT.Forward(mWorkLeft);
	mFFT.Forward(mWorkRight);

	// cancel right channel image in left channel
	for(int i = 2; i < 2048; i += 2) {
		float lr = mWorkLeft[i+0];
		float li = mWorkLeft[i+1];
		float rr = mWorkRight[i+0];
		float ri = mWorkRight[i+1];

		// L' = L - R * saturate((L*R)/(R*R))
		float lmag = lr*lr + li*li;
		float rmag = rr*rr + ri*ri;

		float ratio = std::max<float>(0.0f, 1.0f - sqrtf(rmag / std::max<float>(1e-10f, lmag)));

		mWorkLeft[i+0] = lr * ratio;
		mWorkLeft[i+1] = li * ratio;
	}

	mFFT.Inverse(mWorkLeft);

	// accumulate blocks
	for(int i=0; i<7; ++i) {
		float *VDRESTRICT dst = &mOutputWindow[(mWindowOffset + i*256) & 2047];
		const float *VDRESTRICT src = &mWorkLeft[i * 256];
		const float *VDRESTRICT window = &mFFTWindow[i * 256];

		for(int j=0; j<256; ++j) {
			dst[j] += src[j] * (window[j] * window[j]);
		}
	}

	{
		float *VDRESTRICT dst = &mOutputWindow[(mWindowOffset + 7*256) & 2047];
		const float *VDRESTRICT src = &mWorkLeft[7 * 256];
		const float *VDRESTRICT window = &mFFTWindow[7 * 256];

		for(int j=0; j<256; ++j) {
			dst[j] = src[j] * (window[j] * window[j]);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////

ATCassetteAudioThreadedQueue::ATCassetteAudioThreadedQueue(IATCassetteAudioSource& source)
	: VDThread("Cassette Audio Worker")
	, mSource(source)
{
	// take one count off so we can simplify bock advance by always freeing one
	mBlocksFree.Wait();

	ThreadStart();
}

ATCassetteAudioThreadedQueue::~ATCassetteAudioThreadedQueue() {
	if (!mbStreamEnded) {
		// mark ready to exit early
		mbExit = true;

		// consume blocks until we get EOF
		for(;;) {
			// free last block
			mBlocksFree.Post();

			// advance to the next block
			if (++mCurrentBlock >= kBlockCount)
				mCurrentBlock = 0;

			// wait for block to be completed
			mBlocksQueued.Wait();

			if (!mBlockLengths[mCurrentBlock])
				break;
		}
	}

	ThreadWait();
}

uint32 ATCassetteAudioThreadedQueue::ReadAudio(sint16 (*dst)[2], uint32 n) {
	if (mbStreamEnded)
		return 0;

	uint32 read = 0;

	while(n) {
		if (mCurrentBlockOffset < mCurrentBlockLength) {
			uint32 tc = std::min<uint32>(mCurrentBlockLength - mCurrentBlockOffset, n);

			memcpy(dst, &mBlocks[mCurrentBlock][mCurrentBlockOffset], sizeof(dst[0]) * tc);
			mCurrentBlockOffset += tc;
			dst += tc;
			n -= tc;
			read += tc;
		} else {
			// free last block
			mBlocksFree.Post();

			// advance to the next block
			if (++mCurrentBlock >= kBlockCount)
				mCurrentBlock = 0;

			// wait for block to be completed
			mBlocksQueued.Wait();

			// read block length
			mCurrentBlockLength = mBlockLengths[mCurrentBlock];

			// if the next block was empty, we're done
			if (mCurrentBlockLength == 0) {
				mbStreamEnded = true;

				// if there was an exception throw it; it's critical that we do
				// this after setting stream ended so we don't hang in the dtor
				if (!mException.empty())
					throw mException;

				break;
			}

			mCurrentBlockOffset = 0;
		}
	}

	return read;
}

void ATCassetteAudioThreadedQueue::ThreadRun() {
	int blockIndex = 1;

	for(;;) {
		mBlocksFree.Wait();

		uint32 n = 0;

		if (!mbExit) {
			try {
				n = mSource.ReadAudio(mBlocks[blockIndex], vdcountof(mBlocks[blockIndex]));
			} catch(VDException& e) {
				mException = std::move(e);

				// n = 0 will signal end, upon which exception will be checked
			}
		}

		mBlockLengths[blockIndex] = n;

		mBlocksQueued.Post();

		if (!n)
			break;

		if (++blockIndex >= kBlockCount)
			blockIndex = 0;
	}
}
