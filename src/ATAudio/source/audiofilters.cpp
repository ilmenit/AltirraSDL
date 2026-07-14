//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2021 Avery Lee
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
#include <vd2/system/cpuaccel.h>
#include <vd2/system/math.h>
#include <at/ataudio/audiofilters.h>

#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
#include <vd2/system/win32/intrin.h>
#elif defined(VD_CPU_ARM64)
#include <arm_neon.h>
#endif

alignas(16) constexpr sint16 g_ATAudioResamplingKernel[32][8] = {
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
};

struct alignas(16) ATAudioResamplingKernel33x8 {
	float kernel[33][8];
};

constexpr ATAudioResamplingKernel33x8 ATComputeAudioResamplingKernel(float scale) {
	ATAudioResamplingKernel33x8 k {};

	for(int i=0; i<32; ++i) {
		for(int j=0; j<8; ++j) {
			k.kernel[i][j] = (float)g_ATAudioResamplingKernel[i][j] * scale;
		}
	}

	// repeat first item with shift for the interpolating version
	for(int j=0; j<7; ++j)
		k.kernel[32][j+1] = k.kernel[0][j];

	return k;
}

constexpr ATAudioResamplingKernel33x8 g_ATAudioResamplingKernel1 = ATComputeAudioResamplingKernel(1.0f / 16384.0f);
constexpr ATAudioResamplingKernel33x8 g_ATAudioResamplingKernel2 = ATComputeAudioResamplingKernel(32767.0f / 16384.0f);

extern "C" {
	constexpr ATAudioFilterKernel63To44 gATAudioResamplingKernel63To44 = []() constexpr -> ATAudioFilterKernel63To44 {
		constexpr double pi = 3.1415926535897932384626433832795;
		constexpr double twopi = pi*2;

		const auto crsin = [](double x) constexpr -> double {
			// fold angle to +/-pi
			double rotations = x / twopi;
			x -= twopi*(double)(long long)(rotations + (rotations < 0 ? -0.5 : rotations > 0 ? 0.5 : 0));

			// fold angle to [0,pi]
			double sign = 1;
			if (x < 0) {
				x = -x;
				sign = -1;
			}

			// fold angle to [0, pi/2]
			if (x > pi*0.5)
				x = pi - x;

			// calculate Maclaurin series
			double r = x;
			double z = x;
			double x2 = x*x;
			double div = 2;

			for(int i=0; i<11; ++i) {
				z *= -x2;
				z /= div * (div + 1);
				div += 2;

				r += z;
			}

			return r * sign;
		};

		const auto crcos = [crsin](double x) constexpr -> double {
			return crsin(x + pi*0.5);
		};

		// compute Blackman window
		ATAudioFilterKernel63To44 k{};
		double w[64] {};

		for(int i=0; i<64; ++i) {
			double x = (double)i / 64.0 * twopi;
			w[i] = 0.42 - 0.5*crcos(x) + 0.08*crcos(x+x);
		}

		double fc = 39000.0/63920.0;

		const auto crsinc = [crsin](double x) constexpr -> double {
			return x > -1e-100 && x < 1e-100 ? 1.0 : crsin(x) / x;
		};

		for(int i=0; i<=64; ++i) {
			double f[64];
			double sum = 0;

			for(int j=0; j<64; ++j) {
				f[j] = crsinc(((double)(j - 32) - (double)i / 64.0) * pi * fc) * w[j];
				sum += f[j];
			}

			// normalize filter
			double norm = 1.0 / sum;
			for(int j=0; j<64; ++j)
				f[j] *= norm;

			// convert big taps to fixed point (1.15)
			double error = 0;

			for(int j=0; j<16; ++j) {
				int idx = j & 1 ? (64 - j) >> 1 : (64 + j) >> 1;
				double tapf = f[idx] + error;
				double tapx = tapf * 0x1p+15;
				int tap = (int)tapx;

				if (tap != tapx)
					tap += (tap >> 31) | 1;

				if (tap != (sint16)tap)
					throw;

				k.mFilter[i][idx] = (sint16)tap;

				error = tapf - (double)tap * 0x1p-15;
			}

			// convert little taps to fixed point (1.19)
			for(int j=16; j<64; ++j) {
				int idx = j & 1 ? (64 - j) >> 1 : (64 + j) >> 1;
				double tapf = f[idx] + error;
				double tapx = tapf * 0x1p+19;
				int tap = (int)tapx;

				if (tap != tapx)
					tap += (tap >> 31) | 1;

				if (tap != (sint16)tap)
					throw;

				k.mFilter[i][idx] = (sint16)tap;
				error = tapf - (double)tap * 0x1p-19;
			}
		}

		return k;
	}();
}

void ATFilterNormalizeKernel(float *v, size_t n, float scale) {
	float sum = 0;

	for(size_t i=0; i<n; ++i)
		sum += v[i];

	scale /= sum;

	for(size_t i=0; i<n; ++i)
		v[i] *= scale;
}

template<bool T_Interp, bool T_DoubleToStereo, bool T_AddAux>
uint64 ATFilterResampleMono16_Scalar(sint16 *d, const float *s, const float *auxLeft, const float *auxRight, uint32 count, uint64 accum, sint64 inc) {
	static_assert(T_DoubleToStereo || !T_AddAux);

	do {
		const float *s2 = s + (accum >> 32);
		const float *f = g_ATAudioResamplingKernel1.kernel[(uint32)accum >> 27];

		float v;

		if constexpr (T_Interp) {
			const float frac2 = (float)(sint32)(accum & 0x7FFFFFF) * (1.0f / (float)0x8000000);
			const float frac1 = 1.0f - frac2;

			v	= s2[0]*(f[0]*frac1 + f[ 8]*frac2)
				+ s2[1]*(f[1]*frac1 + f[ 9]*frac2)
				+ s2[2]*(f[2]*frac1 + f[10]*frac2)
				+ s2[3]*(f[3]*frac1 + f[11]*frac2)
				+ s2[4]*(f[4]*frac1 + f[12]*frac2)
				+ s2[5]*(f[5]*frac1 + f[13]*frac2)
				+ s2[6]*(f[6]*frac1 + f[14]*frac2)
				+ s2[7]*(f[7]*frac1 + f[15]*frac2);

		} else {
			v	= s2[0]*f[0]
				+ s2[1]*f[1]
				+ s2[2]*f[2]
				+ s2[3]*f[3]
				+ s2[4]*f[4]
				+ s2[5]*f[5]
				+ s2[6]*f[6]
				+ s2[7]*f[7];
		}

		accum += inc;

		if (T_DoubleToStereo) {
			if constexpr (T_AddAux) {
				d[0] = VDClampedRoundFixedToInt16Fast(v + *auxLeft++);
				d[1] = VDClampedRoundFixedToInt16Fast(v + *auxRight++);
			} else {
				d[0] = d[1] = VDClampedRoundFixedToInt16Fast(v);
			}

			d += 2;
		} else
			*d++ = VDClampedRoundFixedToInt16Fast(v);
	} while(--count);

	return accum;
}

#if VD_CPU_X86 || VD_CPU_X64
template<bool T_Interp, bool T_DoubleToStereo, bool T_AddAux>
uint64 ATFilterResampleMonoToStereo16_SSE2(sint16 *d0, const float *s, const float *auxLeft, const float *auxRight, uint32 count, uint64 accum, sint64 inc) {
	static_assert(T_DoubleToStereo || !T_AddAux);

	sint16 *VDRESTRICT d = d0;

	do {
		const float *s2 = s + (accum >> 32);
		const float *f = g_ATAudioResamplingKernel2.kernel[(uint32)accum >> 27];

		__m128 x0 = _mm_loadu_ps(s2);
		__m128 x1 = _mm_loadu_ps(s2 + 4);
		__m128 c0 = _mm_load_ps(f);
		__m128 c1 = _mm_load_ps(f + 4);

		if constexpr (T_Interp) {
			__m128 frac = _mm_set1_ps((float)(sint32)(accum & 0x7FFFFFF) * (1.0f / (float)0x8000000));
			__m128 c2 = _mm_load_ps(f + 8);
			__m128 c3 = _mm_load_ps(f + 12);
			c0 = _mm_add_ps(_mm_sub_ps(c0, _mm_mul_ps(c0, frac)), _mm_mul_ps(c2, frac));
			c1 = _mm_add_ps(_mm_sub_ps(c1, _mm_mul_ps(c1, frac)), _mm_mul_ps(c3, frac));
		}

		accum += inc;

		__m128 y0 = _mm_mul_ps(c0, x0);
		__m128 y1 = _mm_mul_ps(c1, x1);
		__m128 y2 = _mm_add_ps(y0, y1);
		__m128 y3 = _mm_add_ps(y2, _mm_movehl_ps(y2, y2));
		__m128 y4 = _mm_add_ps(y3, _mm_shuffle_ps(y3, y3, 0b00010001));

		if constexpr (T_AddAux) {
			__m128 auxL = _mm_load_ss(auxLeft++);
			__m128 auxR = _mm_load_ss(auxRight++);

			auxL = _mm_mul_ss(auxL, _mm_set_ss(32767.0f));
			auxR = _mm_mul_ss(auxR, _mm_set_ss(32767.0f));

			y4 = _mm_add_ps(y4, _mm_unpacklo_ps(auxL, auxR));
		}

		__m128i z0 = _mm_cvtps_epi32(y4);
		__m128i z1 = _mm_packs_epi32(z0, z0);

		if constexpr (T_DoubleToStereo) {
			*(int *)d = _mm_cvtsi128_si32(z1);
			d += 2;
		} else
			*d++ = (sint16)_mm_cvtsi128_si32(z1);
	} while(--count);

	return accum;
}
#endif

#if VD_CPU_ARM64
template<bool T_Interp, bool T_DoubleToStereo, bool T_AddAux>
uint64 ATFilterResampleMonoToStereo16_NEON(sint16 *d0, const float *s, const float *auxLeft, const float *auxRight, uint32 count, uint64 accum, sint64 inc) {
	static_assert(T_DoubleToStereo || !T_AddAux);

	sint16 *VDRESTRICT d = d0;

	[[maybe_unused]] float32x2_t auxScale = vdup_n_f32(32767.0f);

	do {
		const float *s2 = s + (accum >> 32);
		const float *VDRESTRICT f = g_ATAudioResamplingKernel2.kernel[(uint32)accum >> 27];

		float32x4x2_t c;
		
		if constexpr (T_Interp) {
			float32x4_t frac = vmovq_n_f32((float)(accum & 0x7FFFFFF) * (1.0f / (float)0x8000000));
			float32x4x2_t c0 = vld2q_f32(f);
			float32x4x2_t c1 = vld2q_f32(f + 8);
			c.val[0] = vmlaq_f32(vmlsq_f32(c0.val[0], c0.val[0], frac), c1.val[0], frac);
			c.val[1] = vmlaq_f32(vmlsq_f32(c0.val[1], c0.val[1], frac), c1.val[1], frac);
		} else {
			c = vld2q_f32(f);
		}

		accum += inc;

		float32x4x2_t x = vld2q_f32(s2);
		float32x4_t y0 = vmulq_f32(c.val[0], x.val[0]);
		float32x4_t y1 = vfmaq_f32(y0, c.val[1], x.val[1]);
		float32x2_t y2 = vmov_n_f32(vaddvq_f32(y1));

		if constexpr (T_AddAux) {
			y2 = vmla_f32(y2, vset_lane_f32(*auxRight++, vdup_n_f32(*auxLeft++), 1), auxScale);
		}

		int32x2_t z0 = vcvtn_s32_f32(y2);
		int16x4_t z1 = vqmovn_s32(vcombine_s32(z0, z0));

		if constexpr (T_DoubleToStereo) {
			vst1_lane_s32((int32_t *)d, vreinterpret_s32_s16(z1), 0);
			d += 2;
		} else {
			vst1_lane_s16(d++, z1, 0);
		}
	} while(--count);

	return accum;
}
#endif

uint64 ATFilterResampleMono16(sint16 *d, const float *s, uint32 count, uint64 accum, sint64 inc, bool interp) {
#if VD_CPU_X86 || VD_CPU_X64
	if (SSE2_enabled) {
		if (interp)
			return ATFilterResampleMonoToStereo16_SSE2<true, false, false>(d, s, nullptr, nullptr, count, accum, inc);
		else
			return ATFilterResampleMonoToStereo16_SSE2<false, false, false>(d, s, nullptr, nullptr, count, accum, inc);
	}
#endif

#if VD_CPU_ARM64
	if (interp)
		return ATFilterResampleMonoToStereo16_NEON<true, false, false>(d, s, nullptr, nullptr, count, accum, inc);
	else
		return ATFilterResampleMonoToStereo16_NEON<false, false, false>(d, s, nullptr, nullptr, count, accum, inc);
#else
	if (interp)
		return ATFilterResampleMono16_Scalar<true, false, false>(d, s, nullptr, nullptr, count, accum, inc);
	else
		return ATFilterResampleMono16_Scalar<false, false, false>(d, s, nullptr, nullptr, count, accum, inc);
#endif
}

uint64 ATFilterResampleMonoToStereo16(sint16 *d, const float *s, uint32 count, uint64 accum, sint64 inc, bool interp) {
#if VD_CPU_X86 || VD_CPU_X64
	if (SSE2_enabled) {
		if (interp)
			return ATFilterResampleMonoToStereo16_SSE2<true, true, false>(d, s, nullptr, nullptr, count, accum, inc);
		else
			return ATFilterResampleMonoToStereo16_SSE2<false, true, false>(d, s, nullptr, nullptr, count, accum, inc);
	}
#endif

#if VD_CPU_ARM64
	if (interp)
		return ATFilterResampleMonoToStereo16_NEON<true, true, false>(d, s, nullptr, nullptr, count, accum, inc);
	else
		return ATFilterResampleMonoToStereo16_NEON<false, true, false>(d, s, nullptr, nullptr, count, accum, inc);
#else
	if (interp)
		return ATFilterResampleMono16_Scalar<true, true, false>(d, s, nullptr, nullptr, count, accum, inc);
	else
		return ATFilterResampleMono16_Scalar<false, true, false>(d, s, nullptr, nullptr, count, accum, inc);
#endif
}

uint64 ATFilterResampleMonoToStereoAdd16(sint16 *d, const float *s, const float *auxLeft, const float *auxRight, uint32 count, uint64 accum, sint64 inc, bool interp) {
#if VD_CPU_X86 || VD_CPU_X64
	if (SSE2_enabled) {
		if (interp)
			return ATFilterResampleMonoToStereo16_SSE2<true, true, true>(d, s, auxLeft, auxRight, count, accum, inc);
		else
			return ATFilterResampleMonoToStereo16_SSE2<false, true, true>(d, s, auxLeft, auxRight, count, accum, inc);
	}
#endif

#if VD_CPU_ARM64
	if (interp)
		return ATFilterResampleMonoToStereo16_NEON<true, true, true>(d, s, auxLeft, auxRight, count, accum, inc);
	else
		return ATFilterResampleMonoToStereo16_NEON<false, true, true>(d, s, auxLeft, auxRight, count, accum, inc);
#else
	if (interp)
		return ATFilterResampleMono16_Scalar<true, true, true>(d, s, auxLeft, auxRight, count, accum, inc);
	else
		return ATFilterResampleMono16_Scalar<false, true, true>(d, s, auxLeft, auxRight, count, accum, inc);
#endif
}

template<bool T_Interp, bool T_AddAux>
static uint64 ATFilterResampleStereo16_Reference(sint16 *d, const float *s1, const float *s2, const float *auxLeft, const float *auxRight, uint32 count, uint64 accum, sint64 inc) {
	do {
		const float *VDRESTRICT r1 = s1 + (accum >> 32);
		const float *VDRESTRICT r2 = s2 + (accum >> 32);
		const float *VDRESTRICT f = g_ATAudioResamplingKernel1.kernel[(uint32)accum >> 27];

		float f0 = f[0];
		float f1 = f[1];
		float f2 = f[2];
		float f3 = f[3];
		float f4 = f[4];
		float f5 = f[5];
		float f6 = f[6];
		float f7 = f[7];

		if constexpr (T_Interp) {
			const float frac2 = (float)(sint32)(accum & 0x7FFFFFF) * (1.0f / (float)0x8000000);
			const float frac1 = 1.0f - frac2;

			f0 = f0*frac1 + f[ 8]*frac2;
			f1 = f1*frac1 + f[ 9]*frac2;
			f2 = f2*frac1 + f[10]*frac2;
			f3 = f3*frac1 + f[11]*frac2;
			f4 = f4*frac1 + f[12]*frac2;
			f5 = f5*frac1 + f[13]*frac2;
			f6 = f6*frac1 + f[14]*frac2;
			f7 = f7*frac1 + f[15]*frac2;
		}

		accum += inc;

		float a = r1[0]*f0
				+ r1[1]*f1
				+ r1[2]*f2
				+ r1[3]*f3
				+ r1[4]*f4
				+ r1[5]*f5
				+ r1[6]*f6
				+ r1[7]*f7;

		float b = r2[0]*f0
				+ r2[1]*f1
				+ r2[2]*f2
				+ r2[3]*f3
				+ r2[4]*f4
				+ r2[5]*f5
				+ r2[6]*f6
				+ r2[7]*f7;

		if constexpr (T_AddAux) {
			a += *auxLeft++;
			b += *auxRight++;
		}

		d[0] = VDClampedRoundFixedToInt16Fast(a);
		d[1] = VDClampedRoundFixedToInt16Fast(b);
		d += 2;
	} while(--count);

	return accum;
}

#if VD_CPU_X86 || VD_CPU_X64
template<bool T_Interp, bool T_AddAux>
uint64 ATFilterResampleStereo16_SSE2(sint16 *d0, const float *s1, const float *s2, const float *auxLeft, const float *auxRight, uint32 count, uint64 accum, sint64 inc) {
	sint16 *VDRESTRICT d = d0;
	do {
		const float *VDRESTRICT r1 = s1 + (accum >> 32);
		const float *VDRESTRICT r2 = s2 + (accum >> 32);
		const float *VDRESTRICT f = g_ATAudioResamplingKernel2.kernel[(uint32)accum >> 27];

		__m128 f0 = _mm_load_ps(f + 0);
		__m128 f1 = _mm_load_ps(f + 4);

		if constexpr (T_Interp) {
			__m128 f2 = _mm_load_ps(f + 8);
			__m128 f3 = _mm_load_ps(f + 12);

			__m128 frac = _mm_set1_ps((float)(sint32)(accum & 0x7FFFFFF) * (1.0f / (float)0x8000000));
			f0 = _mm_add_ps(_mm_sub_ps(f0, _mm_mul_ps(f0, frac)), _mm_mul_ps(f2, frac));
			f1 = _mm_add_ps(_mm_sub_ps(f1, _mm_mul_ps(f1, frac)), _mm_mul_ps(f3, frac));
		}

		accum += inc;

		__m128 l = _mm_add_ps(_mm_mul_ps(_mm_loadu_ps(&r1[0]), f0), _mm_mul_ps(_mm_loadu_ps(&r1[4]), f1));
		__m128 r = _mm_add_ps(_mm_mul_ps(_mm_loadu_ps(&r2[0]), f0), _mm_mul_ps(_mm_loadu_ps(&r2[4]), f1));

		if constexpr (T_AddAux) {
			l = _mm_add_ss(l, _mm_load_ss(auxLeft++));
			r = _mm_add_ss(r, _mm_load_ss(auxRight++));
		}

		// fold horizontally -> llrr
		const __m128 lr1 = _mm_add_ps(_mm_shuffle_ps(l, r, 0b01'00'01'00), _mm_shuffle_ps(l, r, 0b11'10'11'10));

		// fold again -> lr
		const __m128 lr2 = _mm_add_ps(_mm_shuffle_ps(lr1, lr1, 0b10'00'10'00), _mm_shuffle_ps(lr1, lr1, 0b11'01'11'01));

		// convert to integer
		const __m128i ilr = _mm_cvtps_epi32(lr2);

		// pack to sint16[2] and write
		_mm_storeu_si32(d, _mm_packs_epi32(ilr, ilr));
		d += 2;
	} while(--count);

	return accum;
}
#endif

uint64 ATFilterResampleStereo16(sint16 *d, const float *s1, const float *s2, uint32 count, uint64 accum, sint64 inc, bool interp) {
#if VD_CPU_X86 || VD_CPU_X64
	if (SSE2_enabled) {
		if (interp)
			return ATFilterResampleStereo16_SSE2<true, false>(d, s1, s2, nullptr, nullptr, count, accum, inc);
		else
			return ATFilterResampleStereo16_SSE2<false, false>(d, s1, s2, nullptr, nullptr, count, accum, inc);
	}
#endif

	if (interp)
		return ATFilterResampleStereo16_Reference<true, false>(d, s1, s2, nullptr, nullptr, count, accum, inc);
	else
		return ATFilterResampleStereo16_Reference<false, false>(d, s1, s2, nullptr, nullptr, count, accum, inc);
}

uint64 ATFilterResampleStereoAdd16(sint16 *d, const float *s1, const float *s2, const float *auxLeft, const float *auxRight, uint32 count, uint64 accum, sint64 inc, bool interp) {
#if VD_CPU_X86 || VD_CPU_X64
	if (SSE2_enabled) {
		if (interp)
			return ATFilterResampleStereo16_SSE2<true, true>(d, s1, s2, auxLeft, auxRight, count, accum, inc);
		else
			return ATFilterResampleStereo16_SSE2<false, true>(d, s1, s2, auxLeft, auxRight, count, accum, inc);
	}
#endif

	if (interp)
		return ATFilterResampleStereo16_Reference<true, true>(d, s1, s2, auxLeft, auxRight, count, accum, inc);
	else
		return ATFilterResampleStereo16_Reference<false, true>(d, s1, s2, auxLeft, auxRight, count, accum, inc);
}

///////////////////////////////////////////////////////////////////////////

void ATFilterComputeSymmetricFIR_8_32F_Scalar(float *dst, size_t n, const float *kernel) {
	const float k0 = kernel[0];
	const float k1 = kernel[1];
	const float k2 = kernel[2];
	const float k3 = kernel[3];
	const float k4 = kernel[4];
	const float k5 = kernel[5];
	const float k6 = kernel[6];
	const float k7 = kernel[7];

	do {
		float v = dst[7] * k0
				+ (dst[ 6] + dst[ 8]) * k1
				+ (dst[ 5] + dst[ 9]) * k2
				+ (dst[ 4] + dst[10]) * k3
				+ (dst[ 3] + dst[11]) * k4
				+ (dst[ 2] + dst[12]) * k5
				+ (dst[ 1] + dst[13]) * k6
				+ (dst[ 0] + dst[14]) * k7;

		*dst++ = v;
	} while(--n);
}

void ATFilterComputeSymmetricFIR_16_32F_Scalar(float *dst, size_t n, const float *kernel) {
	const float k0 = kernel[0];
	const float k1 = kernel[1];
	const float k2 = kernel[2];
	const float k3 = kernel[3];
	const float k4 = kernel[4];
	const float k5 = kernel[5];
	const float k6 = kernel[6];
	const float k7 = kernel[7];
	const float k8 = kernel[8];
	const float k9 = kernel[9];
	const float k10 = kernel[10];
	const float k11 = kernel[11];
	const float k12 = kernel[12];
	const float k13 = kernel[13];
	const float k14 = kernel[14];
	const float k15 = kernel[15];

	do {
		float v =  dst[15] * k0
				+ (dst[14] + dst[16]) * k1
				+ (dst[13] + dst[17]) * k2
				+ (dst[12] + dst[18]) * k3
				+ (dst[11] + dst[19]) * k4
				+ (dst[10] + dst[20]) * k5
				+ (dst[ 9] + dst[21]) * k6
				+ (dst[ 8] + dst[22]) * k7
				+ (dst[ 7] + dst[23]) * k8
				+ (dst[ 6] + dst[24]) * k9
				+ (dst[ 5] + dst[25]) * k10
				+ (dst[ 4] + dst[26]) * k11
				+ (dst[ 3] + dst[27]) * k12
				+ (dst[ 2] + dst[28]) * k13
				+ (dst[ 1] + dst[29]) * k14
				+ (dst[ 0] + dst[30]) * k15
			;

		*dst++ = v;
	} while(--n);
}

#if defined(VD_CPU_X86) || defined(VD_CPU_AMD64)
void ATFilterComputeSymmetricFIR_8_32F_SSE(float *dst, size_t n, const float *kernel) {
	__m128 zero = _mm_setzero_ps();
	__m128 x0 = zero;
	__m128 x1 = zero;
	__m128 x2 = zero;
	__m128 x3 = zero;
	__m128 f0;
	__m128 f1;
	__m128 f2;
	__m128 f3;

	// init filter
	__m128 k0 = _mm_loadu_ps(kernel + 0);
	__m128 k1 = _mm_loadu_ps(kernel + 4);

	f0 = _mm_shuffle_ps(k1, k1, _MM_SHUFFLE(0, 1, 2, 3));
	f1 = _mm_shuffle_ps(k0, k0, _MM_SHUFFLE(0, 1, 2, 3));
	f2 = _mm_move_ss(k0, k1);
	f2 = _mm_shuffle_ps(f2, f2, _MM_SHUFFLE(0, 3, 2, 1));
	f3 = _mm_move_ss(k1, zero);
	f3 = _mm_shuffle_ps(f3, f3, _MM_SHUFFLE(0, 3, 2, 1));

	// prime
	for(int i=0; i<14; ++i) {
		x0 = _mm_move_ss(x0, x1);
		x0 = _mm_shuffle_ps(x0, x0, _MM_SHUFFLE(0, 3, 2, 1));
		x1 = _mm_move_ss(x1, x2);
		x1 = _mm_shuffle_ps(x1, x1, _MM_SHUFFLE(0, 3, 2, 1));
		x2 = _mm_move_ss(x2, x3);
		x2 = _mm_shuffle_ps(x2, x2, _MM_SHUFFLE(0, 3, 2, 1));
		x3 = _mm_move_ss(x3, zero);
		x3 = _mm_shuffle_ps(x3, x3, _MM_SHUFFLE(0, 3, 2, 1));

		__m128 s = _mm_load1_ps(dst++);
		x0 = _mm_add_ps(x0, _mm_mul_ps(f0, s));
		x1 = _mm_add_ps(x1, _mm_mul_ps(f1, s));
		x2 = _mm_add_ps(x2, _mm_mul_ps(f2, s));
		x3 = _mm_add_ps(x3, _mm_mul_ps(f3, s));
	}

	// pipeline
	do {
		x0 = _mm_move_ss(x0, x1);
		x0 = _mm_shuffle_ps(x0, x0, _MM_SHUFFLE(0, 3, 2, 1));
		x1 = _mm_move_ss(x1, x2);
		x1 = _mm_shuffle_ps(x1, x1, _MM_SHUFFLE(0, 3, 2, 1));
		x2 = _mm_move_ss(x2, x3);
		x2 = _mm_shuffle_ps(x2, x2, _MM_SHUFFLE(0, 3, 2, 1));
		x3 = _mm_move_ss(x3, zero);
		x3 = _mm_shuffle_ps(x3, x3, _MM_SHUFFLE(0, 3, 2, 1));

		__m128 s = _mm_load1_ps(dst);
		x0 = _mm_add_ps(x0, _mm_mul_ps(f0, s));
		x1 = _mm_add_ps(x1, _mm_mul_ps(f1, s));
		x2 = _mm_add_ps(x2, _mm_mul_ps(f2, s));
		x3 = _mm_add_ps(x3, _mm_mul_ps(f3, s));

		_mm_store_ss(dst - 14, x0);
		++dst;
	} while(--n);
}

void ATFilterComputeSymmetricFIR_16_32F_SSE(float *dst, size_t n, const float *kernel) {
	__m128 zero = _mm_setzero_ps();
	__m128 x0 = zero;
	__m128 x1 = zero;
	__m128 x2 = zero;
	__m128 x3 = zero;
	__m128 x4 = zero;
	__m128 x5 = zero;
	__m128 x6 = zero;
	__m128 x7 = zero;

	// init filter
	__m128 k0 = _mm_mul_ss(_mm_loadu_ps(kernel + 0), _mm_set1_ps(0.5f));
	__m128 k1 = _mm_loadu_ps(kernel + 4);
	__m128 k2 = _mm_loadu_ps(kernel + 8);
	__m128 k3 = _mm_loadu_ps(kernel + 12);

	// pipeline
	do {
		__m128 x0 = _mm_loadu_ps(dst +  0);
		__m128 x1 = _mm_loadu_ps(dst +  4);
		__m128 x2 = _mm_loadu_ps(dst +  8);
		__m128 x3 = _mm_loadu_ps(dst + 12);
		__m128 x4 = _mm_loadu_ps(dst + 15);
		__m128 x5 = _mm_loadu_ps(dst + 19);
		__m128 x6 = _mm_loadu_ps(dst + 23);
		__m128 x7 = _mm_loadu_ps(dst + 27);

		__m128 y0 = _mm_mul_ps(_mm_add_ps(x4, _mm_shuffle_ps(x3, x3, _MM_SHUFFLE(0, 1, 2, 3))), k0);
		__m128 y1 = _mm_mul_ps(_mm_add_ps(x5, _mm_shuffle_ps(x2, x2, _MM_SHUFFLE(0, 1, 2, 3))), k1);
		__m128 y2 = _mm_mul_ps(_mm_add_ps(x6, _mm_shuffle_ps(x1, x1, _MM_SHUFFLE(0, 1, 2, 3))), k2);
		__m128 y3 = _mm_mul_ps(_mm_add_ps(x7, _mm_shuffle_ps(x0, x0, _MM_SHUFFLE(0, 1, 2, 3))), k3);

		__m128 z0 = _mm_add_ps(_mm_add_ps(y0, y1), _mm_add_ps(y2, y3));
		__m128 z1 = _mm_add_ps(z0, _mm_movehl_ps(z0, z0));
		__m128 z2 = _mm_add_ps(z1, _mm_shuffle_ps(z1, z1, 0x55));

		_mm_store_ss(dst++, z2);
	} while(--n);
}
#endif

#if defined(VD_CPU_X86) && defined(VD_COMPILER_MSVC) && !defined(VD_COMPILER_MSVC_CLANG)
void __declspec(naked) __cdecl ATFilterComputeSymmetricFIR_8_32F_SSE_asm(float *dst, size_t n, const float *kernel) {
	__asm {
	mov	edx, esp
	and	esp, -16
	sub	esp, 80
	mov	[esp+64], edx

	mov	eax, [edx+12]
	mov	ecx, [edx+4]

	xorps	xmm7, xmm7

	movups	xmm0, [eax+16]
	movups	xmm1, [eax]
	movaps	xmm2, xmm1
	movaps	xmm3, xmm0
	shufps	xmm0, xmm0, 1bh
	shufps	xmm1, xmm1, 1bh
	movss	xmm2, xmm3
	shufps	xmm2, xmm2, 39h
	movss	xmm3, xmm7
	shufps	xmm3, xmm3, 39h
	movaps	[esp], xmm0
	movaps	[esp+16], xmm1
	movaps	[esp+32], xmm2
	movaps	[esp+48], xmm3

	mov	eax, 14
	xorps	xmm0, xmm0
	xorps	xmm1, xmm1
	xorps	xmm2, xmm2
	xorps	xmm3, xmm3
ploop:
	movss	xmm0, xmm1
	shufps	xmm0, xmm0, 39h
	movss	xmm1, xmm2
	shufps	xmm1, xmm1, 39h
	movss	xmm2, xmm3
	shufps	xmm2, xmm2, 39h
	movss	xmm3, xmm7
	shufps	xmm3, xmm3, 39h

	movss	xmm6, [ecx]
	add		ecx, 4
	shufps	xmm6, xmm6, 0

	movaps	xmm4, xmm6
	mulps	xmm4, [esp+0]
	addps	xmm0, xmm4

	movaps	xmm5, xmm6
	mulps	xmm5, [esp+16]
	addps	xmm1, xmm5

	movaps	xmm4, xmm6
	mulps	xmm4, [esp+32]
	addps	xmm2, xmm4

	mulps	xmm6, [esp+48]
	addps	xmm3, xmm6

	dec		eax
	jne		ploop
	
	mov	eax, [edx+8]
xloop:
	movss	xmm0, xmm1
	shufps	xmm0, xmm0, 39h
	movss	xmm1, xmm2
	shufps	xmm1, xmm1, 39h
	movss	xmm2, xmm3
	shufps	xmm2, xmm2, 39h
	movss	xmm3, xmm7
	shufps	xmm3, xmm3, 39h

	movss	xmm6, [ecx]
	shufps	xmm6, xmm6, 0

	movaps	xmm4, xmm6
	mulps	xmm4, [esp+0]
	addps	xmm0, xmm4

	movaps	xmm5, xmm6
	mulps	xmm5, [esp+16]
	addps	xmm1, xmm5

	movaps	xmm4, xmm6
	mulps	xmm4, [esp+32]
	addps	xmm2, xmm4

	mulps	xmm6, [esp+48]
	addps	xmm3, xmm6

	movss	[ecx-56], xmm0
	add	ecx, 4

	dec	eax
	jne	xloop

	mov	esp, [esp+64]
	ret
	}
}
#endif

#ifdef VD_CPU_ARM64
void ATFilterComputeSymmetricFIR_8_32F_NEON(float *dst, size_t n, const float *kernel) {
	float32x4_t zero = vdupq_n_f32(0);
	float32x4_t x0 = zero;
	float32x4_t x1 = zero;
	float32x4_t x2 = zero;
	float32x4_t x3 = zero;

	// init filter
	float32x4_t k0 = vld1q_f32(kernel);
	float32x4_t k1 = vld1q_f32(kernel + 4);

	float32x4_t f0 = vrev64q_f32(vcombine_f32(vget_high_f32(k1), vget_low_f32(k1)));
	float32x4_t f1 = vrev64q_f32(vcombine_f32(vget_high_f32(k0), vget_low_f32(k0)));
	float32x4_t f2 = vextq_f32(k0, k1, 1);
	float32x4_t f3 = vextq_f32(k1, zero, 1);

	// prime (skip write)
	for(int i=0; i<14; ++i) {
		x0 = vextq_f32(x0, x1, 1);
		x1 = vextq_f32(x1, x2, 1);
		x2 = vextq_f32(x2, x3, 1);
		x3 = vextq_f32(x3, zero, 1);

		float32x4_t s = vld1q_dup_f32(dst++);
		x0 = vfmaq_f32(x0, f0, s);
		x1 = vfmaq_f32(x1, f1, s);
		x2 = vfmaq_f32(x2, f2, s);
		x3 = vfmaq_f32(x3, f3, s);
	}

	// pipeline
	do {
		x0 = vextq_f32(x0, x1, 1);
		x1 = vextq_f32(x1, x2, 1);
		x2 = vextq_f32(x2, x3, 1);
		x3 = vextq_f32(x3, zero, 1);

		float32x4_t s = vld1q_dup_f32(dst);
		x0 = vfmaq_f32(x0, f0, s);
		x1 = vfmaq_f32(x1, f1, s);
		x2 = vfmaq_f32(x2, f2, s);
		x3 = vfmaq_f32(x3, f3, s);

		vst1q_lane_f32(dst - 15, x0, 0);
		++dst;
	} while(--n);
}

void ATFilterComputeSymmetricFIR_16_32F_NEON(float *dst, size_t n, const float *kernel) {
	float32x4_t zero = vdupq_n_f32(0);

							//  .w   .z   .y   .x
	float32x4_t x0 = zero;	// [ 0] [ 1] [ 2] [ 3]
	float32x4_t x1 = zero;	// [ 4] [ 5] [ 6] [ 7]
	float32x4_t x2 = zero;	// [ 8] [ 9] [10] [11]
	float32x4_t x3 = zero;	// [12] [13] [14] [15]
	float32x4_t x4 = zero;	// [18] [17] [16] [15]
	float32x4_t x5 = zero;	// [22] [21] [20] [19]
	float32x4_t x6 = zero;	// [26] [25] [24] [23]
	float32x4_t x7 = zero;	// [30] [29] [28] [27]

	// init filter
	float32x4_t k0 = vld1q_f32(kernel);
	float32x4_t k1 = vld1q_f32(kernel + 4);

	float32x4_t f0 = vrev64q_f32(vcombine_f32(vget_high_f32(k1), vget_low_f32(k1)));
	float32x4_t f1 = vrev64q_f32(vcombine_f32(vget_high_f32(k0), vget_low_f32(k0)));
	float32x4_t f2 = vextq_f32(k0, k1, 1);
	float32x4_t f3 = vextq_f32(k1, zero, 1);

	// preload
	x0 = vld1q_f32(dst +  0); x0 = vrev64q_f32(vextq_f32(x0, x0, 2)); x0 = vextq_f32(x0, x0, 1);
	x1 = vld1q_f32(dst +  3); x1 = vrev64q_f32(vextq_f32(x1, x1, 2));
	x2 = vld1q_f32(dst +  7); x2 = vrev64q_f32(vextq_f32(x2, x2, 2));
	x3 = vld1q_f32(dst + 11); x3 = vrev64q_f32(vextq_f32(x3, x3, 2));
	x4 = vld1q_f32(dst + 14);
	x5 = vld1q_f32(dst + 18);
	x6 = vld1q_f32(dst + 22);
	x7 = vld1q_f32(dst + 26);

	// pipeline
	do {
		x0 = vextq_f32(x0, x1, 3);
		x1 = vextq_f32(x1, x2, 3);
		x2 = vextq_f32(x2, x3, 3);
		x3 = vcopyq_laneq_f32(vextq_f32(x3, x3, 3), 0, x4, 0);
		x4 = vextq_f32(x4, x5, 1);
		x5 = vextq_f32(x5, x6, 1);
		x6 = vextq_f32(x6, x7, 1);
		x7 = vld1q_lane_f32(dst + 30, vextq_f32(x7, x7, 1), 0);

		float32x4_t acc;
		acc = vmulq_f32(vaddq_f32(x3, x4), f0);
		acc = vfmaq_f32(acc, vaddq_f32(x2, x5), f1);
		acc = vfmaq_f32(acc, vaddq_f32(x1, x6), f2);
		acc = vfmaq_f32(acc, vaddq_f32(x0, x7), f3);

		*dst++ = vaddvq_f32(acc);
	} while(--n);
}
#endif

void ATFilterComputeSymmetricFIR_8_32F(float *dst, size_t n, const float *kernel) {
#if defined(VD_CPU_X86) && defined(VD_COMPILER_MSVC) && !defined(VD_COMPILER_MSVC_CLANG)
	ATFilterComputeSymmetricFIR_8_32F_SSE_asm(dst, n, kernel);
#elif defined(VD_CPU_X64)
	ATFilterComputeSymmetricFIR_8_32F_SSE(dst, n, kernel);
#elif defined(VD_CPU_ARM64)
	ATFilterComputeSymmetricFIR_8_32F_NEON(dst, n, kernel);
#else
	ATFilterComputeSymmetricFIR_8_32F_Scalar(dst, n, kernel);
#endif
}

void ATFilterComputeSymmetricFIR_16_32F(float *dst, size_t n, const float *kernel) {
#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
	ATFilterComputeSymmetricFIR_16_32F_SSE(dst, n, kernel);
#elif defined(VD_CPU_ARM64)
	ATFilterComputeSymmetricFIR_16_32F_NEON(dst, n, kernel);
#else
	ATFilterComputeSymmetricFIR_16_32F_Scalar(dst, n, kernel);
#endif
}

///////////////////////////////////////////////////////////////////////////

class ATAudioFilterIIR {
public:
	void Run(float *p, size_t n);

	float mOverlapBuffer[128] {};
	float mOverlapBuffer2[128] {};
	float mZeroBuffer[64] {};
};

void ATAudioFilterIIR::Run(float *p, size_t n) {
	// Filter type: Low Pass
	// Filter model: Butterworth
	// Filter order: 4
	// Sampling Frequency: 63 KHz
	// Cut Frequency: 5.000000 KHz
	// Coefficents Quantization: float
	// 
	// Z domain Zeros
	// z = -1.000000 + j 0.000000
	// z = -1.000000 + j 0.000000
	// z = -1.000000 + j 0.000000
	// z = -1.000000 + j 0.000000
	// 
	// Z domain Poles
	// z = 0.613943 + j -0.125767
	// z = 0.613943 + j 0.125767
	// z = 0.746764 + j -0.369315
	// z = 0.746764 + j 0.369315

	constexpr float a02[2] {0.0412143f, 0.0501305f};
	constexpr float a1[2] {0.0824286f, 0.100261f};
	constexpr float b1[2] {-1.227886f, -1.493528f};
	constexpr float b2[2] {0.3927433f, 0.69405f};

	float x1[2] {};
	float x2[2] {};
	float y1[2] {};
	float y2[2] {};

	for(int i=0; i<2; ++i) {
		ptrdiff_t step = i & 1 ? -1 : 1;

		float x0[2] {};
		float y0[2] {};

		struct Range {
			float *dst;
			const float *src;
			size_t m;
		} ranges[4];

		if (i == 0) {
			ranges[0] = { p, p, n };
			ranges[1] = { mOverlapBuffer2, mZeroBuffer, 64 };
			ranges[2] = { nullptr, nullptr, 0 };
			ranges[3] = { nullptr, nullptr, 0 };
		} else {
			ranges[0] = { mOverlapBuffer2 + 127, mOverlapBuffer2 + 63, 64 };
			ranges[1] = { mOverlapBuffer2 + 63, p + n - 1, 64 };
			ranges[2] = { p + n - 1, p + n - 65, n - 64 };
			ranges[3] = { p + 63, mZeroBuffer + 63, 64 };
		}

		for(const Range& range : ranges) {
			const float *src = range.src;
			float *dst = range.dst;
			size_t m = range.m;

			for(size_t j=0; j<m; ++j) {
				x0[0] = *src;
				src += step;

				y0[0] = (x0[0]+x2[0])*a02[0] + x1[0]*a1[0] - y1[0]*b1[0] - y2[0]*b2[0];
				y0[1] = (x0[1]+x2[1])*a02[1] + x1[1]*a1[1] - y1[1]*b1[1] - y2[1]*b2[1];

				y2[0] = y1[0]; y2[1] = y1[1];
				y1[0] = y0[0]; y1[1] = y0[1];
				x2[0] = x1[0]; x2[1] = x1[1];
				x1[0] = x0[0]; x1[1] = x0[1];

				*dst = y0[1];
				x0[1] = y0[0];
				dst += step;
			}
		}
	}

	for(size_t j = 0; j < 128; ++j) {
		p[j] += mOverlapBuffer[j];
		mOverlapBuffer[j] = mOverlapBuffer2[j];
		mOverlapBuffer2[j] = 0;
	}
};

double ATAudioFilter::sDenormalThreshold = 7.56645539953615e-18;

ATAudioFilter::ATAudioFilter() {
	SetScale(1.0f);
	SetActiveMode(true);

	// Set up a FIR low pass filter, Blackman window, 15KHz cutoff (63920Hz sampling rate)
	SetLPF(15000.0f / 63920.0f, false);
//	SetLPF(5000.0f / 63920.0f, true);
//	SetLPF(11000.0f / 63920.0f, true);
}

ATAudioFilter::~ATAudioFilter() {
}

void ATAudioFilter::SetLPF(float fc, bool useFIR16) {
	float sum = 0.5f;

	memset(mLoPassCoeffs, 0, sizeof mLoPassCoeffs);

	mLoPassCoeffs[0] = 1.0f;

	const int n = useFIR16 ? 16 : 8;
	for(int i=1; i<n; ++i) {
		float x = (float)i * nsVDMath::kfPi;
		float y = x / (float)n;
		float w = 0.42f + 0.5f * cosf(y) + 0.08f * cosf(y+y);

		float f = sinf(2.0f * x * fc) / (2.0f * x * fc) * w;

		mLoPassCoeffs[i] = f;

		sum += f;
	}

	float scale = 0.5f / sum;

	for(int i=0; i<kFilterOverlap; ++i)
		mLoPassCoeffs[i] *= scale;

	mbUseFIR16 = useFIR16;
}

float ATAudioFilter::GetScale() const {
	return mScale;
}

void ATAudioFilter::SetScale(float scale) {
	mScale = scale;
}

void ATAudioFilter::SetActiveMode(bool active) {
	// Actual measurement from an NTSC 800XL is that CONSOL output decays to 2/3 in
	// 10ms, or a time constant of 0.024663 (-0.01/ln(2/3)). 

	if (active)
		mHiCoeff = 0.00063413100049954767179657139255229f;
	else
		mHiCoeff = 0.0001f;
}

void ATAudioFilter::PreFilter(float * VDRESTRICT dst, uint32 count, float dcLevel) {
	if (!count)
		return;

	// The below implements a high-pass filter with Direct Form II (ignoring final scale):
	//
	// v[i] = v[i-1]*(1-k) + x[i]
	// y[i] = v[i] - v[i-1]
	//
	// We can scale the input and divide the output by k:
	//
	// v[i] = v[i-1]*(1-k) + x[i]*k
	// y[i] = v[i]/k - v[i-1]/k
	//
	// ...and then do a bit of substitution and rearranging to get the nicer form:
	//
	// v[i] = v[i-1] + (x[i] - v[i-1])*k
	// y[i] = x[i] - v[i-1]

	const float scale = mScale;
	const float hiCoeff = mHiCoeff;
	float hiAccum = mHiPassAccum - dcLevel;

	do {
		float v0 = *dst;
		float v1 = v0 - hiAccum;
		hiAccum += v1 * hiCoeff;

		*dst++ = v1 * scale;
	} while(--count);

	// prevent denormals
	if (fabsf(hiAccum) < sDenormalThreshold)
		hiAccum = 0;

	mHiPassAccum = hiAccum;
}

void ATAudioFilter::PreFilterDiff(float * VDRESTRICT dst, uint32 count) {
	float last = mDiffHistory;

	while(count--) {
		float x = *dst;
		*dst++ = x - last;

		last = x;
	}

	mDiffHistory = last;
}

void ATAudioFilter::PreFilterEdges(float * VDRESTRICT dst, uint32 count, float dcLevel) {
	if (!count)
		return;

	// Flipping the order of the stages to switch from Direct Form II to Direct Form I
	// gives:
	//
	// v[i] = x[i] - x[i-1]
	// y[i] = y[i-1]*(1-k) + v[i]
	//
	// At first glance, this doesn't look interesting as it requires another delay element.
	// The advantage of this form is that we can cancel the differentiating stage against a
	// previous integrating stage. Instead of integrating edges to form pulses and then
	// differentiating the pulses back to edges in the high-pass filter, we can simply
	// take the edges directly in the high-pass filter, reducing it to just an exponential
	// decay calculation. Besides being faster, this avoids instability from accumulated
	// floating-point integration error.
	//
	// Note that mHiPassAccum stores y[i-1]*(1-k), to simplify the inner loop and for
	// compatibility with the normal path.

	const float scale = mScale;
	const float decay = 1.0f - mHiCoeff;
	float y = mHiPassAccum - dcLevel;

	do {
		y += *dst;
		*dst++ = y * scale;
		y *= decay;
	} while(--count);

	// prevent denormals
	if (fabsf(y) < sDenormalThreshold)
		y = 0;

	mHiPassAccum = y;
}

void ATAudioFilter::Filter(float *dst, uint32 count) {
	if (mpIIR)
		mpIIR->Run(dst, count);
	else if (mbUseFIR16)
		ATFilterComputeSymmetricFIR_16_32F(dst, count, mLoPassCoeffs);
	else
		ATFilterComputeSymmetricFIR_8_32F(dst, count, mLoPassCoeffs);
}
