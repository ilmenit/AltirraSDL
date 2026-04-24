//	Altirra - Atari 800/800XL/5200 emulator
//	I/O library - cassette analog decoder filters
//	Copyright (C) 2009-2017 Avery Lee
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

#include <stdafx.h>
#include <vd2/system/bitmath.h>
#include <vd2/system/math.h>
#include <at/atcore/configvar.h>
#include <at/atio/cassettedecoder.h>
#include <at/atio/cassetteimage.h>		// for constants

#ifdef VD_CPU_ARM64
#include <arm_neon.h>
#endif

ATConfigVarFloat g_ATCVTapeDecodeDirectHPFCutoff("tape.decode.slope.hpf_cutoff", 5327.0f);
ATConfigVarFloat g_ATCVTapeDecodeFSKMarkGain("tape.decode.fsk.mark_gain", 1.0f);

#ifdef VD_COMPILER_MSVC
#pragma runtime_checks("", off)
#pragma optimize("gt", on)
#endif

ATCassetteDecoderFSK::ATCassetteDecoderFSK() {
	Reset();
}

void ATCassetteDecoderFSK::Reset() {
	mAcc.m0R = 0;
	mAcc.m0I = 0;
	mAcc.m1R = 0;
	mAcc.m1I = 0;
	mIndex = 0;
	memset(mHistory, 0, sizeof mHistory);
}

template<bool T_DoAnalysis>
void ATCassetteDecoderFSK::Process(const sint16 *samples, uint32 n, uint32 *bitfield, uint32 bitoffset, float *adest) {
	static constexpr float sin_0_24 = 0;
	static constexpr float sin_1_24 = 0.25881904510252076234889883762405f;
	static constexpr float sin_2_24 = 0.5f;
	static constexpr float sin_3_24 = 0.70710678118654752440084436210485f;
	static constexpr float sin_4_24 = 0.86602540378443864676372317075294;
	static constexpr float sin_5_24 = 0.9659258262890682867497431997289f;
	static constexpr float sin_6_24 = 1.0f;

	static constexpr float sintab_24[24]={
		sin_0_24,	sin_1_24,	sin_2_24,	sin_3_24,
		sin_4_24,	sin_5_24,	sin_6_24,	sin_5_24,
		sin_4_24,	sin_3_24,	sin_2_24,	sin_1_24,
		-sin_0_24,	-sin_1_24,	-sin_2_24,	-sin_3_24,
		-sin_4_24,	-sin_5_24,	-sin_6_24,	-sin_5_24,
		-sin_4_24,	-sin_3_24,	-sin_2_24,	-sin_1_24,
	};

	static constexpr struct RotTab {
		alignas(16) sint16 vec[24][4] = {};

		static constexpr sint16 intround16(float v) {
			return v < 0 ? (sint16)(v - 0.5f) : (sint16)(v + 0.5f);
		}

		constexpr RotTab() {
			for(int i=0; i<24; ++i) {
				vec[i][0] = intround16(sintab_24[(6 + i*3) % 24] * 0x1000);
				vec[i][1] = intround16(sintab_24[(0 + i*3) % 24] * 0x1000);
				vec[i][2] = intround16(sintab_24[(6 + i*4) % 24] * 0x1000);
				vec[i][3] = intround16(sintab_24[(0 + i*4) % 24] * 0x1000);
			}
		}
	} kRotTab;

#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
	static constexpr struct RotTabSSE2 {
		alignas(16) sint16 vec[24][8] {};

		constexpr RotTabSSE2() {
			for(int i=0; i<24; ++i) {
				for(int j=0; j<4; ++j) {
					vec[i][j*2+0] =  kRotTab.vec[i][j];
					vec[i][j*2+1] = -kRotTab.vec[i][j];
				}
			}
		}
	} kRotTabSSE2;
#endif

#if defined(VD_CPU_ARM64)
	static constexpr struct RotTabNEON {
		sint16 vec[24][4] {};

		constexpr RotTabNEON() {
			for(int i=0; i<24; ++i) {
				// permute from (r0, i0, r1, i1) to (r0, r1, i0, i1) order
				vec[i][0] = kRotTab.vec[i][0];
				vec[i][1] = kRotTab.vec[i][2];
				vec[i][2] = kRotTab.vec[i][1];
				vec[i][3] = kRotTab.vec[i][3];
			}
		}
	} kRotTabNEON;
#endif

	uint32 bitaccum = 0;
	uint32 bitcounter = 32 - bitoffset;
	const float markGain = g_ATCVTapeDecodeFSKMarkGain;

#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
	__m128i acc01 = _mm_loadu_si128((const __m128i *)&mAcc + 0);
#elif defined(VD_CPU_ARM64)
	int32x4_t acc01 = vld1q_s32(&mAcc.m0R);
#endif

	do {
		// update history window
		const sint32 x1 = *samples;
		samples += 2;

		// We sample at 31960Hz and use a 24-point DFT.
		// 3995Hz (zero) filter extracts from bin 3.
		// 5327Hz (one) filter extracts from bin 4.
		//
		// We compute these via a sliding DFT. The per-sample phase shift angles
		// for the two filters are 2*pi/24*3 = pi/4 and 2*pi/24*4 = pi/3. A 24-point
		// DFT with rectangular windowing performed best in testing. A 32-point
		// DFT is not bad, but has non-ideal frequencies; a 48-point DFT was too
		// long in time domain. Different windows didn't work out either as the
		// ringing in the frequency domain increases the crosstalk between the
		// filters. With the 24-point DFT, the bins for the two FSK tones have
		// nulls in their responses at each other's frequencies, which is what we
		// want.
		//
		// The sliding DFT is computed in integer arithmetic to avoid error
		// accumulation that would occur with floating-point. A decay constant
		// is normally used to combat this, but if it is too short it makes the
		// response asymmetric, and if it is too long it causes false pulses on
		// long runs -- which is very important for the 30s leader.
		//
		// There is no silence detection on the filter. Cases have been seen where
		// the FSK detector has been able to recover data at extremely low volume
		// levels, as low as -60dB.
		//
		// This filter introduces a delay of 12 samples; currently we just ignore
		// that.

		uint32 hpos1 = mIndex++;

		if (mIndex == 24)
			mIndex = 0;

#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
		// advance sliding window
		const sint32 x0 = mHistory[hpos1];
		const __m128i x01 = _mm_shuffle_epi32(_mm_insert_epi16(_mm_insert_epi16(_mm_setzero_si128(), (uint16)x1, 0), (uint16)x0, 1), 0);

		mHistory[hpos1] = x1;

		acc01 = _mm_add_epi32(acc01, _mm_madd_epi16(x01, _mm_load_si128((const __m128i *)kRotTabSSE2.vec[hpos1])));

		// compute mark/space magnitudes
		__m128 resp = _mm_cvtepi32_ps(acc01);
		resp = _mm_mul_ps(resp, resp);
		resp = _mm_add_ps(resp, _mm_shuffle_ps(resp, resp, 0b0'10'11'00'01));

		const float zero = _mm_cvtss_f32(resp);
		const float one = _mm_cvtss_f32(_mm_movehl_ps(resp, resp)) * markGain;
#elif defined(VD_CPU_ARM64)
		// advance sliding window
		const sint32 x0 = mHistory[hpos1];
		mHistory[hpos1] = x1;

		int16x4_t rot = vld1_s16(kRotTabNEON.vec[hpos1]);
		acc01 = vmlal_n_s16(acc01, rot, x1);
		acc01 = vmlsl_n_s16(acc01, rot, x0);

		// compute mark/space magnitudes
		float32x4_t resp = vcvtq_f32_s32(acc01);
		float32x2_t respR = vget_low_f32(resp);
		float32x2_t respI = vget_high_f32(resp);

		float32x2_t zeroOne = vmla_f32(vmul_f32(respR, respR), respI, respI);

		const float zero = vget_lane_f32(zeroOne, 0);
		const float one = vget_lane_f32(zeroOne, 1) * markGain;
#else
		const sint32 x0 = mHistory[hpos1];
		mHistory[hpos1] = x1;

		const sint32 y = x1 - x0;

		// sliding update for frequency bins 2-5
		mAcc.m0R += kRotTab.vec[hpos1][0] * y;
		mAcc.m0I += kRotTab.vec[hpos1][1] * y;
		mAcc.m1R += kRotTab.vec[hpos1][2] * y;
		mAcc.m1I += kRotTab.vec[hpos1][3] * y;

		// convert frequency bin responses to float
		const float acc0r = (float)mAcc.m0R;
		const float acc0i = (float)mAcc.m0I;
		const float acc1r = (float)mAcc.m1R;
		const float acc1i = (float)mAcc.m1I;

		const float zero = acc0r*acc0r + acc0i*acc0i;
		const float one = (acc1r*acc1r + acc1i*acc1i) * markGain;
#endif

		if (T_DoAnalysis) {
			adest[0] = (float)mHistory[hpos1 >= 12 ? hpos1 - 12 : hpos1 + 12] * (1.0f / 32767.0f);
			adest[1] = sqrtf(zero) * (1.0f / 32767.0f / 4096.0f / 12.0f);
			adest[2] = sqrtf(one) * (1.0f / 32767.0f / 4096.0f / 12.0f);
			adest[3] = (one > zero ? 0.8f : -0.8f);
			// slots 4 and 5 reserved for direct decoder
			adest += 6;
		}

		bitaccum += bitaccum;
		if (one >= zero)
			++bitaccum;

		if (!--bitcounter) {
			bitcounter = 32;
			*bitfield++ |= bitaccum;
		}
	} while(--n);

	if (bitcounter < 32)
		*bitfield++ |= bitaccum << bitcounter;

#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
	_mm_storeu_si128((__m128i *)&mAcc + 0, acc01);
#elif defined(VD_CPU_ARM64)
	vst1q_s32(&mAcc.m0R, acc01);
#endif
}

template void ATCassetteDecoderFSK::Process<false>(const sint16 *samples, uint32 n, uint32 *bitfield, uint32 bitoffset, float *adest);
template void ATCassetteDecoderFSK::Process<true>(const sint16 *samples, uint32 n, uint32 *bitfield, uint32 bitoffset, float *adest);

///////////////////////////////////////////////////////////////////////////////

ATCassetteDecoderTurbo::ATCassetteDecoderTurbo() {
}

void ATCassetteDecoderTurbo::Init(ATCassetteTurboDecodeAlgorithm algorithm, bool enableAnalysis) {
	using AlgorithmPtr = void (ATCassetteDecoderTurbo::*)(const sint16 *samples, uint32 n, float *adest);

	static constexpr AlgorithmPtr kAlgorithms[2][5] {
		{
			&ATCassetteDecoderTurbo::Process<false, DetectorType::Slope, PreFilterType::None>,
			&ATCassetteDecoderTurbo::Process<false, DetectorType::Slope, PreFilterType::HP_IIR>,
			&ATCassetteDecoderTurbo::Process<false, DetectorType::Peak, PreFilterType::HP_IIR>,
			&ATCassetteDecoderTurbo::Process<false, DetectorType::Peak, PreFilterType::HP_IIR>,
			&ATCassetteDecoderTurbo::Process<false, DetectorType::Peak, PreFilterType::HP_IIR>
		},
		{
			&ATCassetteDecoderTurbo::Process<true, DetectorType::Slope, PreFilterType::None>,
			&ATCassetteDecoderTurbo::Process<true, DetectorType::Slope, PreFilterType::HP_IIR>,
			&ATCassetteDecoderTurbo::Process<true, DetectorType::Peak, PreFilterType::HP_IIR>,
			&ATCassetteDecoderTurbo::Process<true, DetectorType::Peak, PreFilterType::HP_IIR>,
			&ATCassetteDecoderTurbo::Process<true, DetectorType::Peak, PreFilterType::HP_IIR>
		}
	};

	mAlgorithm = algorithm;
	mpAlgorithm = kAlgorithms[(int)enableAnalysis][(int)algorithm];

	Reset();
}

void ATCassetteDecoderTurbo::Reset() {
	mAGC = 0;
	mPrefilterState = 0;

	mBitAccum = 0;
	mBitCounter = 32;
	mWrittenBits = 0;

	mPostFilterWindowIdx = 0;
	mbLastStable = true;
	mbLastPolarity = true;
	mPeakSignCounter = 3;
	mPeakOffset = 0;
	mPeakWindowCount = 0;
	mPeakValue = 0;
	mPeakSign = +1;
	mShiftReg = ~UINT64_C(0);

	std::fill(std::begin(mPostFilterWindow), std::end(mPostFilterWindow), 0);

	// First-order HPF formed by subtracting first-order LPF from input.
	if (g_ATCVTapeDecodeDirectHPFCutoff < 0) {
		mHPFFactor = 0.9999f;
	} else {
		float y = 2.0f - cosf(2.0f * nsVDMath::kfPi / (float)kATCassetteDataSampleRateD * g_ATCVTapeDecodeDirectHPFCutoff);
		mHPFFactor = y - sqrtf(y*y - 1.0f);
	}

	mPrevLevel = 0;

	memset(mHPFWindow, 0, sizeof mHPFWindow);

	mBitfield.clear();
}

template<bool T_DoAnalysis, ATCassetteDecoderTurbo::DetectorType T_Detector, ATCassetteDecoderTurbo::PreFilterType T_PreFilter>
void ATCassetteDecoderTurbo::Process(const sint16 *samples, uint32 n, float *adest0) {
	float *VDRESTRICT adest = adest0;

	uint32 bitaccum = mBitAccum;
	uint32 bitcounter = mBitCounter;
	uint32 *VDRESTRICT dst = &mBitfield[mWrittenBits >> 5];

	mWrittenBits += n;

#if VD_CPU_X86 || VD_CPU_X64
	[[maybe_unused]] __m128 hpf0;
	[[maybe_unused]] __m128 hpf1;
	[[maybe_unused]] __m128 hpf2;
	[[maybe_unused]] __m128 hpf3;

	if constexpr (T_PreFilter == PreFilterType::HP_FIR) {
		hpf0 = _mm_loadu_ps(mHPFWindow + 0);
		hpf1 = _mm_loadu_ps(mHPFWindow + 4);
		hpf2 = _mm_loadu_ps(mHPFWindow + 8);
		hpf3 = _mm_loadu_ps(mHPFWindow + 12);
	}
#elif VD_CPU_ARM64
	[[maybe_unused]] float32x4_t hpf0;
	[[maybe_unused]] float32x4_t hpf1;
	[[maybe_unused]] float32x4_t hpf2;
	[[maybe_unused]] float32x4_t hpf3;

	if constexpr (T_PreFilter == PreFilterType::HP_FIR) {
		hpf0 = vld1q_f32(mHPFWindow +  0);
		hpf1 = vld1q_f32(mHPFWindow +  4);
		hpf2 = vld1q_f32(mHPFWindow +  8);
		hpf3 = vld1q_f32(mHPFWindow + 12);
	}
#else
	[[maybe_unused]] float hpf[16];

	if constexpr (T_PreFilter == PreFilterType::HP_FIR) {
		memcpy(hpf, mHPFWindow, sizeof hpf);
	}
#endif

	do {
		float x = *samples;
		samples += 2;

		if constexpr (T_PreFilter == PreFilterType::HP_IIR) {
			// To combat high-frequency attenuation ostensibly from Dolby-B decoding being
			// improperly applied and to also cancel out low frequency components we don't
			// care about, apply a high-pass filter at ~3.8KHz. This is just a
			// simple single-pole filter with soft falloff so it doesn't distort too much.

			x -= mPrefilterState;
			mPrefilterState += mHPFFactor * x;
		} else if constexpr (T_PreFilter == PreFilterType::HP_FIR) {
			// To combat high-frequency attenuation ostensibly from Dolby-B decoding being
			// improperly applied and to also cancel out low frequency components we don't
			// care about, apply a high-pass filter at ~3.8KHz. This is just a
			// simple single-pole filter with soft falloff so it doesn't distort too much.

			alignas(16) static constexpr float kHPFKernel[16] {
				-0.0123454f, -0.0246906f, -0.0370356f, -0.0493802f,
				-0.0617247f, -0.0740689f, -0.0864128f, 0.901243f,
				-0.0864091f, -0.074062f, -0.061715f, -0.0493684f,
				-0.037022f, -0.0246758f, -0.0123299f, 0
			};

#if VD_CPU_X86 || VD_CPU_X64
			__m128 xv = _mm_set1_ps(x);
			hpf0 = _mm_add_ps(hpf0, _mm_mul_ps(_mm_load_ps(kHPFKernel +  0), xv));
			hpf1 = _mm_add_ps(hpf1, _mm_mul_ps(_mm_load_ps(kHPFKernel +  4), xv));
			hpf2 = _mm_add_ps(hpf2, _mm_mul_ps(_mm_load_ps(kHPFKernel +  8), xv));
			hpf3 = _mm_add_ps(hpf3, _mm_mul_ps(_mm_load_ps(kHPFKernel + 12), xv));

			x = _mm_cvtss_f32(hpf0);
			hpf0 = _mm_move_ss(hpf0, hpf1);
			hpf1 = _mm_move_ss(hpf1, hpf2);
			hpf2 = _mm_move_ss(hpf2, hpf3);

			hpf0 = _mm_shuffle_ps(hpf0, hpf0, 0b0'00'11'10'01);
			hpf1 = _mm_shuffle_ps(hpf1, hpf1, 0b0'00'11'10'01);
			hpf2 = _mm_shuffle_ps(hpf2, hpf2, 0b0'00'11'10'01);

			hpf3 = _mm_castsi128_ps(_mm_srli_si128(_mm_castps_si128(hpf3), 4));
#elif VD_CPU_ARM64
			hpf0 = vmlaq_n_f32(hpf0, vld1q_f32(kHPFKernel +  0), x);
			hpf1 = vmlaq_n_f32(hpf1, vld1q_f32(kHPFKernel +  4), x);
			hpf2 = vmlaq_n_f32(hpf2, vld1q_f32(kHPFKernel +  8), x);
			hpf3 = vmlaq_n_f32(hpf3, vld1q_f32(kHPFKernel + 12), x);

			x = vgetq_lane_f32(hpf0, 0);
			hpf0 = vextq_f32(hpf0, hpf1, 1);
			hpf1 = vextq_f32(hpf1, hpf2, 1);
			hpf2 = vextq_f32(hpf2, hpf3, 1);
			hpf3 = vextq_f32(hpf3, vmovq_n_f32(0), 1);
#else
			// VS2019 does well at autovectorizing this accumulation loop.
			// Unfortunately, it barfs on the shift loop below it for both x86
			// and ARM64, which is why we need the hand-vectorized versions above.
			for(int i=0; i<16; ++i) {
				hpf[i] += x * kHPFKernel[i];
			}

			x = hpf[0];

			for(int i=0; i<15; ++i)
				hpf[i] = hpf[i+1];

			hpf[15] = 0;
#endif
		}

		if constexpr (T_Detector == DetectorType::Slope) {
			float y = x - mPrevLevel;
			mPrevLevel = x;

			float z = fabsf(y);
			const bool edge = (z > mAGC * 0.25f);

			if (edge)
				mbCurrentState = (y > 0);

			if (z > mAGC)
				mAGC += (z - mAGC) * 0.40f;
			else
				mAGC += (z - mAGC) * 0.05f;

			bitaccum += bitaccum;
			if (mbCurrentState)
				++bitaccum;

			if (!--bitcounter) {
				bitcounter = 32;
				*dst++ = bitaccum;
			}
		} else if constexpr (T_Detector == DetectorType::Level) {
			const bool polarity = (x > 0);

			bitaccum += bitaccum;
			if (polarity)
				++bitaccum;

			if (!--bitcounter) {
				bitcounter = 32;
				*dst++ = bitaccum;
			}
		} else if constexpr (T_Detector == DetectorType::Peak) {
			int curVal = x >= 0 ? 1 : -1;

			mPeakSignCounter += curVal;
			mPeakSignCounter -= mPostFilterWindow[(mPostFilterWindowIdx - 3) & 63] >= 0 ? 1 : -1;
			mPostFilterWindow[mPostFilterWindowIdx & 63] = x;
			const float x2 = mPostFilterWindow[(mPostFilterWindowIdx - 1) & 63];
			++mPostFilterWindowIdx;

			bool polarity = ((mShiftReg >> 62) & 1) != 0;

			const bool stable = (mPeakSignCounter <= -2 || mPeakSignCounter >= 2);
			if (stable && !mbLastStable) {
				bool newPolarity = x2 >= 0;

				if (mPeakWindowCount) {
					if (mbLastPolarity == newPolarity) {
						if (!newPolarity) {
							mShiftReg -= UINT64_C(1) << mPeakWindowCount;
							mShiftReg += 1;
						}
					} else if (mPeakWindowCount < 63) {
						if (!newPolarity) {
							// +slope to -slope
							mShiftReg -= UINT64_C(1) << (mPeakWindowCount - mPeakOffset);

							mShiftReg += 1;
						} else {
							// -slope to +slope
							mShiftReg -= UINT64_C(1) << mPeakWindowCount;
							mShiftReg += UINT64_C(1) << (mPeakWindowCount - mPeakOffset);
						}
					}

					mPeakWindowCount = 0;
					mPeakOffset = 0;
					mPeakValue = 0;
				}

				mbLastPolarity = newPolarity;
				mPeakSign = newPolarity ? +1 : -1;
			}

			if (mPeakWindowCount < 64) {
				float val = x2 * mPeakSign;
				if (val > mPeakValue) {
					mPeakValue = val;
					mPeakOffset = mPeakWindowCount;
				}

				++mPeakWindowCount;
			}

			mShiftReg <<= 1;
			++mShiftReg;

			mbLastStable = stable;

			bitaccum += bitaccum;
			if (polarity)
				++bitaccum;

			if (!--bitcounter) {
				bitcounter = 32;
				*dst++ = bitaccum;
			}
		}

		if constexpr (T_DoAnalysis) {
			// slots 0-3 reserved for FSK decoder
			if constexpr (T_Detector == DetectorType::Peak)
				adest[4] = mPostFilterWindow[mPostFilterWindowIdx & 63] * (1.0f / 32767.0f);
			else
				adest[4] = x * (1.0f / 32767.0f);

			adest[5] = mbCurrentState ? 0.8f : -0.8f;
			adest += 6;
		}
	} while(--n);

	mBitAccum = bitaccum;
	mBitCounter = bitcounter;

	if constexpr (T_PreFilter == PreFilterType::HP_FIR) {
#if VD_CPU_X86 || VD_CPU_X64
		_mm_storeu_ps(mHPFWindow +  0, hpf0);
		_mm_storeu_ps(mHPFWindow +  4, hpf1);
		_mm_storeu_ps(mHPFWindow +  8, hpf2);
		_mm_storeu_ps(mHPFWindow + 12, hpf3);
#elif VD_CPU_ARM64
		vst1q_f32(mHPFWindow +  0, hpf0);
		vst1q_f32(mHPFWindow +  4, hpf1);
		vst1q_f32(mHPFWindow +  8, hpf2);
		vst1q_f32(mHPFWindow + 12, hpf3);
#else
		memcpy(mHPFWindow, hpf, sizeof mHPFWindow);
#endif
	}
}

void ATCassetteDecoderTurbo::Process(const sint16 *samples, uint32 n, float *adest) {
	mBitfield.resize((mWrittenBits + n + 31) >> 5, 0);

	(this->*mpAlgorithm)(samples, n, adest);
}

vdfastvector<uint32> ATCassetteDecoderTurbo::Finalize() {
	auto getBit = [this](uint32 i) {
		return (mBitfield[i >> 5] & (0x80000000U >> (i & 31))) != 0;
	};

	auto setBit = [this](uint32 i, bool v) {
		uint32& mask = mBitfield[i >> 5];
		uint32 bit = 0x80000000U >> (i & 31);

		if (v)
			mask |= bit;
		else
			mask &= ~bit;
	};

	// capture range: 0.9KHz - 4.4KHz
	const auto rebalance = [=, this](auto firstPolarity) {
		constexpr uint32 kCaptureMin = (uint32)(kATCassetteDataSampleRate / 4400.0f + 0.5f);
		constexpr uint32 kCaptureMax = (uint32)(kATCassetteDataSampleRate /  900.0f + 0.5f);

		uint32 pos = 0;
		uint32 n = mWrittenBits;

		while(pos < n) {
			uint32 pos0 = pos;
			uint32 lo = 0;
			while(pos < n && getBit(pos) == firstPolarity) {
				++lo;
				++pos;
			}

			uint32 hi = 0;
			while(pos < n && getBit(pos) != firstPolarity) {
				++hi;
				++pos;
			}

			if (lo >= kCaptureMin && lo <= kCaptureMax
				&& hi >= kCaptureMin && hi <= kCaptureMax)
			{
				uint32 tot = lo + hi;
				uint32 newLo = tot >> 1;
				uint32 newHi = (tot + 1) >> 1;

				for(uint32 i = 0; i < newLo; ++i)
					setBit(pos0++, false);

				for(uint32 i = 0; i < newHi; ++i)
					setBit(pos0++, true);
			}
		}
	};

	switch(mAlgorithm) {
		case ATCassetteTurboDecodeAlgorithm::PeakFilterBalanceLoHi:
			rebalance(std::false_type());
			break;

		case ATCassetteTurboDecodeAlgorithm::PeakFilterBalanceHiLo:
			rebalance(std::true_type());
			break;
	}

	return std::move(mBitfield);
}

