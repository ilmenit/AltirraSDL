//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2023 Avery Lee
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

#include "stdafx.h"
#include <vd2/system/binary.h>
#include <vd2/system/cpuaccel.h>
#include <vd2/system/math.h>
#include <vd2/system/zip.h>
#include <at/atio/vorbismisc.h>

#ifdef VD_CPU_ARM64
#include <arm_neon.h>
#if !VD_COMPILER_MSVC
#include <arm_acle.h>
#endif
#endif

void ATVorbisRenderFloorLine_Scalar(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, float *dst, uint32_t limit);
void ATVorbisDecoupleChannels_Scalar(float *__restrict magnitudes, float *__restrict angles, uint32_t halfBlockSize);
void ATVorbisOverlapAdd_Scalar(float *__restrict dst, float *__restrict prev, const float *__restrict window, size_t n2);

void ATVorbisDeinterleaveResidue2_Scalar(float *const *dst, const float *__restrict src, size_t n);
uint32 ATVorbisUpdateCRC_Scalar(uint32 crc, const void *src, size_t len);

void ATVorbisConvertF32ToS16_Scalar(sint16 *dst, const float *src, size_t n);
void ATVorbisConvertF32ToS16x2_Scalar(sint16 *dst, const float *src1, const float *src2, size_t n);
void ATVorbisConvertF32ToS16Rep2_Scalar(sint16 *dst, const float *src, size_t n);

#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
void ATVorbisRenderFloorLine_SSE2(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, float *dst, uint32_t limit);
void ATVorbisDecoupleChannels_SSE2(float *__restrict magnitudes, float *__restrict angles, uint32_t halfBlockSize);
void ATVorbisOverlapAdd_SSE2(float *__restrict dst, float *__restrict prev, const float *__restrict window, size_t n2);
void ATVorbisDeinterleaveResidue2_SSE2(float *const *dst, const float *__restrict src, size_t n);
uint32 ATVorbisUpdateCRC_SSSE3_CLMUL(uint32 crc, const void *src, size_t len);
void ATVorbisConvertF32ToS16_SSE2(sint16 *dst, const float *src, size_t n);
void ATVorbisConvertF32ToS16x2_SSE2(sint16 *dst, const float *src1, const float *src2, size_t n);
void ATVorbisConvertF32ToS16Rep2_SSE2(sint16 *dst, const float *src, size_t n);
#elif defined(VD_CPU_ARM64)
void ATVorbisRenderFloorLine_NEON(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, float *dst, uint32_t limit);
void ATVorbisDecoupleChannels_NEON(float *__restrict magnitudes, float *__restrict angles, uint32_t halfBlockSize);
void ATVorbisOverlapAdd_NEON(float *__restrict dst, float *__restrict prev, const float *__restrict window, size_t n2);
void ATVorbisDeinterleaveResidue2_NEON(float *const *dst, const float *__restrict src, size_t n);
uint32 ATVorbisUpdateCRC_ARM64_CRC32(uint32 crc, const void *src, size_t len);
void ATVorbisConvertF32ToS16_NEON(sint16 *dst, const float *src, size_t n);
void ATVorbisConvertF32ToS16x2_NEON(sint16 *dst, const float *src1, const float *src2, size_t n);
void ATVorbisConvertF32ToS16Rep2_NEON(sint16 *dst, const float *src, size_t n);
#endif

// [10.1. floor1_inverse_dB_table]
extern constexpr float g_ATVorbisInverseDbTable[] {
	1.0649863e-07f, 1.1341951e-07f, 1.2079015e-07f, 1.2863978e-07f,
	1.3699951e-07f, 1.4590251e-07f, 1.5538408e-07f, 1.6548181e-07f,
	1.7623575e-07f, 1.8768855e-07f, 1.9988561e-07f, 2.1287530e-07f,
	2.2670913e-07f, 2.4144197e-07f, 2.5713223e-07f, 2.7384213e-07f,
	2.9163793e-07f, 3.1059021e-07f, 3.3077411e-07f, 3.5226968e-07f,
	3.7516214e-07f, 3.9954229e-07f, 4.2550680e-07f, 4.5315863e-07f,
	4.8260743e-07f, 5.1396998e-07f, 5.4737065e-07f, 5.8294187e-07f,
	6.2082472e-07f, 6.6116941e-07f, 7.0413592e-07f, 7.4989464e-07f,
	7.9862701e-07f, 8.5052630e-07f, 9.0579828e-07f, 9.6466216e-07f,
	1.0273513e-06f, 1.0941144e-06f, 1.1652161e-06f, 1.2409384e-06f,
	1.3215816e-06f, 1.4074654e-06f, 1.4989305e-06f, 1.5963394e-06f,
	1.7000785e-06f, 1.8105592e-06f, 1.9282195e-06f, 2.0535261e-06f,
	2.1869758e-06f, 2.3290978e-06f, 2.4804557e-06f, 2.6416497e-06f,
	2.8133190e-06f, 2.9961443e-06f, 3.1908506e-06f, 3.3982101e-06f,
	3.6190449e-06f, 3.8542308e-06f, 4.1047004e-06f, 4.3714470e-06f,
	4.6555282e-06f, 4.9580707e-06f, 5.2802740e-06f, 5.6234160e-06f,
	5.9888572e-06f, 6.3780469e-06f, 6.7925283e-06f, 7.2339451e-06f,
	7.7040476e-06f, 8.2047000e-06f, 8.7378876e-06f, 9.3057248e-06f,
	9.9104632e-06f, 1.0554501e-05f, 1.1240392e-05f, 1.1970856e-05f,
	1.2748789e-05f, 1.3577278e-05f, 1.4459606e-05f, 1.5399272e-05f,
	1.6400004e-05f, 1.7465768e-05f, 1.8600792e-05f, 1.9809576e-05f,
	2.1096914e-05f, 2.2467911e-05f, 2.3928002e-05f, 2.5482978e-05f,
	2.7139006e-05f, 2.8902651e-05f, 3.0780908e-05f, 3.2781225e-05f,
	3.4911534e-05f, 3.7180282e-05f, 3.9596466e-05f, 4.2169667e-05f,
	4.4910090e-05f, 4.7828601e-05f, 5.0936773e-05f, 5.4246931e-05f,
	5.7772202e-05f, 6.1526565e-05f, 6.5524908e-05f, 6.9783085e-05f,
	7.4317983e-05f, 7.9147585e-05f, 8.4291040e-05f, 8.9768747e-05f,
	9.5602426e-05f, 0.00010181521f, 0.00010843174f, 0.00011547824f,
	0.00012298267f, 0.00013097477f, 0.00013948625f, 0.00014855085f,
	0.00015820453f, 0.00016848555f, 0.00017943469f, 0.00019109536f,
	0.00020351382f, 0.00021673929f, 0.00023082423f, 0.00024582449f,
	0.00026179955f, 0.00027881276f, 0.00029693158f, 0.00031622787f,
	0.00033677814f, 0.00035866388f, 0.00038197188f, 0.00040679456f,
	0.00043323036f, 0.00046138411f, 0.00049136745f, 0.00052329927f,
	0.00055730621f, 0.00059352311f, 0.00063209358f, 0.00067317058f,
	0.00071691700f, 0.00076350630f, 0.00081312324f, 0.00086596457f,
	0.00092223983f, 0.00098217216f, 0.0010459992f,  0.0011139742f,
	0.0011863665f,  0.0012634633f,  0.0013455702f,  0.0014330129f,
	0.0015261382f,  0.0016253153f,  0.0017309374f,  0.0018434235f,
	0.0019632195f,  0.0020908006f,  0.0022266726f,  0.0023713743f,
	0.0025254795f,  0.0026895994f,  0.0028643847f,  0.0030505286f,
	0.0032487691f,  0.0034598925f,  0.0036847358f,  0.0039241906f,
	0.0041792066f,  0.0044507950f,  0.0047400328f,  0.0050480668f,
	0.0053761186f,  0.0057254891f,  0.0060975636f,  0.0064938176f,
	0.0069158225f,  0.0073652516f,  0.0078438871f,  0.0083536271f,
	0.0088964928f,  0.009474637f,   0.010090352f,   0.010746080f,
	0.011444421f,   0.012188144f,   0.012980198f,   0.013823725f,
	0.014722068f,   0.015678791f,   0.016697687f,   0.017782797f,
	0.018938423f,   0.020169149f,   0.021479854f,   0.022875735f,
	0.024362330f,   0.025945531f,   0.027631618f,   0.029427276f,
	0.031339626f,   0.033376252f,   0.035545228f,   0.037855157f,
	0.040315199f,   0.042935108f,   0.045725273f,   0.048696758f,
	0.051861348f,   0.055231591f,   0.058820850f,   0.062643361f,
	0.066714279f,   0.071049749f,   0.075666962f,   0.080584227f,
	0.085821044f,   0.091398179f,   0.097337747f,   0.10366330f,
	0.11039993f,    0.11757434f,    0.12521498f,    0.13335215f,
	0.14201813f,    0.15124727f,    0.16107617f,    0.17154380f,
	0.18269168f,    0.19456402f,    0.20720788f,    0.22067342f,
	0.23501402f,    0.25028656f,    0.26655159f,    0.28387361f,
	0.30232132f,    0.32196786f,    0.34289114f,    0.36517414f,
	0.38890521f,    0.41417847f,    0.44109412f,    0.46975890f,
	0.50028648f,    0.53279791f,    0.56742212f,    0.60429640f,
	0.64356699f,    0.68538959f,    0.72993007f,    0.77736504f,
	0.82788260f,    0.88168307f,    0.9389798f,     1.f
};

static_assert(std::size(g_ATVorbisInverseDbTable) == 256);

void ATVorbisRenderFloorLine(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, float *dst, uint32_t limit) {
#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
	ATVorbisRenderFloorLine_SSE2(x0, y0, x1, y1, dst, limit);
#elif defined(VD_CPU_ARM64)
	ATVorbisRenderFloorLine_NEON(x0, y0, x1, y1, dst, limit);
#else
	ATVorbisRenderFloorLine_Scalar(x0, y0, x1, y1, dst, limit);
#endif
}

void ATVorbisRenderFloorLine_Scalar(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, float *dst, uint32_t limit) {
	if (x0 >= limit)
		return;

	int32_t dy = (int32_t)y1 - (int32_t)y0;
	int32_t adx = x1 - x0;
	int32_t ady = abs(dy);
	int32_t base = dy / adx;
	int32_t x = x0;
	int32_t y = y0;
	int32_t err = 0;
	int32_t sy = dy < 0 ? -1 : +1;

	ady -= abs(base) * adx;
	dst[x] *= g_ATVorbisInverseDbTable[y & 255];

	if (x1 > limit)
		x1 = limit;

	while(++x < (int32_t)x1) {
		err += ady;
		if (err >= adx) {
			err -= adx;
			y += sy;
		}

		y += base;

		dst[x] *= g_ATVorbisInverseDbTable[y & 255];
	}
}

#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
void ATVorbisRenderFloorLine_SSE2(uint32_t x, uint32_t y, uint32_t x1, uint32_t y1, float *dst, uint32_t limit) {
	// The floor curve may extend beyond the length of the residue vector, so we must
	// clip it on the right.
	if (x >= limit)
		return;

	const int32_t dy = y1 - y;
	const int32_t adx = x1 - x;
	const int32_t base = dy / adx;
	const int32_t ady = abs(dy) - abs(base) * adx;
	const int32_t sy = dy < 0 ? -1 : +1;

	// X values in the floor curve are guaranteed unique (and enforced in header
	// decode), so we are guaranteed to always plot at least one point.
	dst[x++] *= g_ATVorbisInverseDbTable[y & 255];
	dst += x;

	if (x1 > limit)
		x1 = limit;

	// A floor1 curve can extend as far as 64K values, but the majority are smaller
	// than that. We can gain significant efficiency by going to 16-bit accumulators,
	// so we vectorize for that case.
	int32_t n = x1 - x;
	int32_t err = 0;
	if (n < 16 || adx >= 0x4000) {
		while(n--) {
			err += ady;
			if (err >= adx) {
				err -= adx;
				y += sy;
			}

			y += base;

			*dst++ *= g_ATVorbisInverseDbTable[y & 255];
		}
	} else {
		// vectorized implementation
		__m128i vsigny = _mm_set1_epi16(sy);
		__m128i vadx = _mm_set1_epi16(adx);

		// precompute initial Y positions and errors
		int16_t vydat[8];
		int16_t vedat[8];

		for(int i=0; i<8; ++i) {
			err += ady;
			if (err >= adx) {
				err -= adx;
				y += sy;
			}

			y += base;

			vedat[i] = err;
			vydat[i] = y;
		}

		__m128i verr = _mm_loadu_si128((const __m128i *)vedat);
		__m128i vy = _mm_loadu_si128((const __m128i *)vydat);

		// compute 8x advance
		int32_t ady8 = ady * 8;
		int32_t base8 = base * 8 + sy*(ady8 / adx);
		ady8 %= adx;

		__m128i vady8 = _mm_set1_epi16(ady8);
		__m128i vbase8 = _mm_set1_epi16(base8);

		// bias up Y against the bottom of the signed 16-bit range so we get a free lower clamp during stepping
		vy = _mm_adds_epi16(vy, _mm_set1_epi16(-0x7FFF-1));
				
		__m128i vyclamp = _mm_set1_epi16(0x80FF - 0x10000);

		// do 8x
		while(n >= 8) {
			n -= 8;

			// extract 4 values
			__m128i vyc = _mm_min_epi16(vy, vyclamp);
			dst[0] *= g_ATVorbisInverseDbTable[_mm_extract_epi16(vyc, 0) - 0x8000];
			dst[1] *= g_ATVorbisInverseDbTable[_mm_extract_epi16(vyc, 1) - 0x8000];
			dst[2] *= g_ATVorbisInverseDbTable[_mm_extract_epi16(vyc, 2) - 0x8000];
			dst[3] *= g_ATVorbisInverseDbTable[_mm_extract_epi16(vyc, 3) - 0x8000];
			dst[4] *= g_ATVorbisInverseDbTable[_mm_extract_epi16(vyc, 4) - 0x8000];
			dst[5] *= g_ATVorbisInverseDbTable[_mm_extract_epi16(vyc, 5) - 0x8000];
			dst[6] *= g_ATVorbisInverseDbTable[_mm_extract_epi16(vyc, 6) - 0x8000];
			dst[7] *= g_ATVorbisInverseDbTable[_mm_extract_epi16(vyc, 7) - 0x8000];
			dst += 8;

			// advance all eight accumulators
			verr = _mm_add_epi16(verr, vady8);
			vy = _mm_adds_epi16(vy, vbase8);

			__m128i vnostep = _mm_cmplt_epi16(verr, vadx);
			verr = _mm_sub_epi16(verr, _mm_andnot_si128(vnostep, vadx));
			vy = _mm_adds_epi16(vy, _mm_andnot_si128(vnostep, vsigny));
		}

		// do leftovers
		if (n) {
			__m128i vyc = _mm_min_epi16(vy, vyclamp);

			switch(n) {
				case 7:
					dst[6] *= g_ATVorbisInverseDbTable[_mm_extract_epi16(vyc, 6) - 0x8000];
					[[fallthrough]];
				case 6:
					dst[5] *= g_ATVorbisInverseDbTable[_mm_extract_epi16(vyc, 5) - 0x8000];
					[[fallthrough]];
				case 5:
					dst[4] *= g_ATVorbisInverseDbTable[_mm_extract_epi16(vyc, 4) - 0x8000];
					[[fallthrough]];
				case 4:
					dst[3] *= g_ATVorbisInverseDbTable[_mm_extract_epi16(vyc, 3) - 0x8000];
					[[fallthrough]];
				case 3:
					dst[2] *= g_ATVorbisInverseDbTable[_mm_extract_epi16(vyc, 2) - 0x8000];
					[[fallthrough]];
				case 2:
					dst[1] *= g_ATVorbisInverseDbTable[_mm_extract_epi16(vyc, 1) - 0x8000];
					[[fallthrough]];
				case 1:
					dst[0] *= g_ATVorbisInverseDbTable[_mm_extract_epi16(vyc, 0) - 0x8000];
					break;
			}
		}
	}
}
#endif

#if defined(VD_CPU_ARM64)
void ATVorbisRenderFloorLine_NEON(uint32_t x, uint32_t y, uint32_t x1, uint32_t y1, float *dst0, uint32_t limit) {
	// The floor curve may extend beyond the length of the residue vector, so we must
	// clip it on the right.
	if (x >= limit)
		return;

	float* VDRESTRICT dst = dst0;

	const int32_t dy = y1 - y;
	const int32_t adx = x1 - x;
	const int32_t base = dy / adx;
	const int32_t ady = abs(dy) - abs(base) * adx;
	const int32_t sy = dy < 0 ? -1 : +1;

	// X values in the floor curve are guaranteed unique (and enforced in header
	// decode), so we are guaranteed to always plot at least one point.
	dst[x++] *= g_ATVorbisInverseDbTable[y & 255];
	dst += x;

	if (x1 > limit)
		x1 = limit;

	// A floor1 curve can extend as far as 64K values, but the majority are smaller
	// than that. We can gain significant efficiency by going to 16-bit accumulators,
	// so we vectorize for that case.
	int32_t n = x1 - x;
	int32_t err = 0;
	if (n < 16 || adx >= 0x4000) {
		while(n--) {
			err += ady;
			if (err >= adx) {
				err -= adx;
				y += sy;
			}

			y += base;

			*dst++ *= g_ATVorbisInverseDbTable[y & 255];
		}
	} else {
		// vectorized implementation
		int16x8_t vsigny = vmovq_n_s16(sy);
		int16x8_t vadx = vmovq_n_s16(adx);

		// precompute initial Y positions and errors
		int16_t vydat[8];
		int16_t vedat[8];

		for(int i=0; i<8; ++i) {
			err += ady;
			if (err >= adx) {
				err -= adx;
				y += sy;
			}

			y += base;

			vedat[i] = err;
			vydat[i] = y;
		}

		int16x8_t verr = vld1q_s16(vedat);
		int16x8_t vy = vld1q_s16(vydat);

		// compute 8x advance
		int32_t ady8 = ady * 8;
		int32_t base8 = base * 8 + sy*(ady8 / adx);
		ady8 %= adx;

		int16x8_t vady8 = vmovq_n_s16(ady8);
		int16x8_t vbase8 = vmovq_n_s16(base8);

		// bias up Y against the bottom of the signed 16-bit range so we get a free lower clamp during stepping
		vy = vqaddq_s16(vy, vmovq_n_s16(-0x7FFF-1));
				
		int16x8_t vyclamp = vmovq_n_s16(0x80FF - 0x10000);

		// do 8x
		uintptr invDbTab = (uintptr)g_ATVorbisInverseDbTable - 0x8000*sizeof(float);

		while(n >= 8) {
			n -= 8;

			// extract 4 values
			uint16x8_t vyc = vreinterpretq_u16_s16(vminq_s16(vy, vyclamp));

			const float* VDRESTRICT fl0 = (const float*)(invDbTab + (size_t)vgetq_lane_u16(vyc, 0)*4);
			const float* VDRESTRICT fl1 = (const float*)(invDbTab + (size_t)vgetq_lane_u16(vyc, 1)*4);
			const float* VDRESTRICT fl2 = (const float*)(invDbTab + (size_t)vgetq_lane_u16(vyc, 2)*4);
			const float* VDRESTRICT fl3 = (const float*)(invDbTab + (size_t)vgetq_lane_u16(vyc, 3)*4);
			const float* VDRESTRICT fl4 = (const float*)(invDbTab + (size_t)vgetq_lane_u16(vyc, 4)*4);
			const float* VDRESTRICT fl5 = (const float*)(invDbTab + (size_t)vgetq_lane_u16(vyc, 5)*4);
			const float* VDRESTRICT fl6 = (const float*)(invDbTab + (size_t)vgetq_lane_u16(vyc, 6)*4);
			const float* VDRESTRICT fl7 = (const float*)(invDbTab + (size_t)vgetq_lane_u16(vyc, 7)*4);

			float32x4_t fa;
			fa = vld1q_dup_f32(fl0);
			fa = vld1q_lane_f32(fl1, fa, 1);
			fa = vld1q_lane_f32(fl2, fa, 2);
			fa = vld1q_lane_f32(fl3, fa, 3);

			float32x4_t fb;
			fb = vld1q_dup_f32(fl4);
			fb = vld1q_lane_f32(fl5, fb, 1);
			fb = vld1q_lane_f32(fl6, fb, 2);
			fb = vld1q_lane_f32(fl7, fb, 3);

			vst1q_f32(dst, vmulq_f32(fa, vld1q_f32(dst)));
			vst1q_f32(dst + 4, vmulq_f32(fb, vld1q_f32(dst + 4)));

			dst += 8;

			// advance all four accumulators
			verr = vaddq_s16(verr, vady8);
			vy = vqaddq_s16(vy, vbase8);

			int16x8_t vnostep = vreinterpretq_s16_u16(vcltq_s16(verr, vadx));
			verr = vsubq_s16(verr, vbicq_s16(vadx, vnostep));
			vy = vqaddq_s16(vy, vbicq_s16(vsigny, vnostep));
		}

		// do leftovers
		if (n) {
			uint16x8_t vyc = vreinterpretq_u16_s16(vminq_s16(vy, vyclamp));

			switch(n) {
				case 7:
					dst[6] *= g_ATVorbisInverseDbTable[vgetq_lane_u16(vyc, 6) - 0x8000];
					[[fallthrough]];
				case 6:
					dst[5] *= g_ATVorbisInverseDbTable[vgetq_lane_u16(vyc, 5) - 0x8000];
					[[fallthrough]];
				case 5:
					dst[4] *= g_ATVorbisInverseDbTable[vgetq_lane_u16(vyc, 4) - 0x8000];
					[[fallthrough]];
				case 4:
					dst[3] *= g_ATVorbisInverseDbTable[vgetq_lane_u16(vyc, 3) - 0x8000];
					[[fallthrough]];
				case 3:
					dst[2] *= g_ATVorbisInverseDbTable[vgetq_lane_u16(vyc, 2) - 0x8000];
					[[fallthrough]];
				case 2:
					dst[1] *= g_ATVorbisInverseDbTable[vgetq_lane_u16(vyc, 1) - 0x8000];
					[[fallthrough]];
				case 1:
					dst[0] *= g_ATVorbisInverseDbTable[vgetq_lane_u16(vyc, 0) - 0x8000];
					break;
			}
		}
	}
}
#endif

///////////////////////////////////////////////////////////////////////////////

VDNOINLINE void ATVorbisDecoupleChannels(float *magnitudes, float *angles, uint32_t halfBlockSize) {
#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
	ATVorbisDecoupleChannels_SSE2(magnitudes, angles, halfBlockSize);
#elif defined(VD_CPU_ARM64)
	ATVorbisDecoupleChannels_NEON(magnitudes, angles, halfBlockSize);
#else
	ATVorbisDecoupleChannels_Scalar(magnitudes, angles, halfBlockSize);
#endif
}

void ATVorbisDecoupleChannels_Scalar(float *__restrict magnitudes, float *__restrict angles, uint32_t halfBlockSize) {
	for(uint32_t i = 0; i < halfBlockSize; ++i) {
		float m = magnitudes[i];
		float a = angles[i];
		float m2;
		float a2;

		if (m > 0) {
			if (a > 0) {
				m2 = m;
				a2 = m - a;
			} else {
				m2 = m + a;
				a2 = m;
			}
		} else {
			if (a > 0) {
				m2 = m;
				a2 = m + a;
			} else {
				m2 = m - a;
				a2 = m;
			}
		}

		magnitudes[i] = m2;
		angles[i] = a2;
	}
}

#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
void ATVorbisDecoupleChannels_SSE2(float *__restrict magnitudes, float *__restrict angles, uint32_t halfBlockSize) {
	const auto zero = _mm_setzero_ps();
	const auto signbit = _mm_castsi128_ps(_mm_set1_epi32(-0x7FFFFFFF - 1));

	for(uint32_t i = 0; i < halfBlockSize; i += 4) {
		// The original logic:
		//
		//   mag  ang  |  mag'  ang'
		//    +    +   |   m    m-a
		//    +    -   |  m+a    m
		//    -    +   |   m    m+a
		//    -    -   |  m-a    m
		//
		// This is a branch prediction nightmare.
		//
		// We can cut this table in half by taking the absolute value
		// of the magnitude and then reintroducing the sign at the end
		// (-0 does not matter to us):
		//
		//   mag  ang  |  mag'  ang'
		//    +    +   |   m    m-a
		//    +    -   |  m+a    m
		//
		// At this point the sign of the angle input determines which
		// output gets perturbed, which we can do with some min/max
		// logic.

		const auto m = _mm_loadu_ps(&magnitudes[i]);
		const auto a = _mm_loadu_ps(&angles[i]);

		const auto sign = _mm_and_ps(m, signbit);
		const auto mabs = _mm_andnot_ps(signbit, m);

		const auto m2 = _mm_add_ps(mabs, _mm_min_ps(a, zero));
		const auto a2 = _mm_sub_ps(mabs, _mm_max_ps(a, zero));

		_mm_storeu_ps(&magnitudes[i], _mm_xor_ps(m2, sign));
		_mm_storeu_ps(&angles[i], _mm_xor_ps(a2, sign));
	}
}
#endif

#if defined(VD_CPU_ARM64)
void ATVorbisDecoupleChannels_NEON(float *__restrict magnitudes, float *__restrict angles, uint32_t halfBlockSize) {
	const auto zero = vmovq_n_f32(0);
	const auto signbit = vmovq_n_u32(0x80000000U);

	for(uint32_t i = 0; i < halfBlockSize; i += 4) {
		// The original logic:
		//
		//   mag  ang  |  mag'  ang'
		//    +    +   |   m    m-a
		//    +    -   |  m+a    m
		//    -    +   |   m    m+a
		//    -    -   |  m-a    m
		//
		// This is a branch prediction nightmare.
		//
		// We can cut this table in half by taking the absolute value
		// of the magnitude and then reintroducing the sign at the end
		// (-0 does not matter to us):
		//
		//   mag  ang  |  mag'  ang'
		//    +    +   |   m    m-a
		//    +    -   |  m+a    m
		//
		// At this point the sign of the angle input determines which
		// output gets perturbed, which we can do with some min/max
		// logic.

		const auto m = vld1q_f32(&magnitudes[i]);
		const auto a = vld1q_f32(&angles[i]);

		const auto sign = vandq_u32(vreinterpretq_u32_f32(m), signbit);
		const auto mabs = vabsq_f32(m);

		const auto m2 = vaddq_f32(mabs, vminq_f32(a, zero));
		const auto a2 = vsubq_f32(mabs, vmaxq_f32(a, zero));

		vst1q_f32(&magnitudes[i], veorq_u32(vreinterpretq_u32_f32(m2), sign));
		vst1q_f32(&angles[i], veorq_u32(vreinterpretq_u32_f32(a2), sign));
	}
}
#endif

// Perform overlap-and-add between the right half of the previous block and
// the left half of the current block.
//
// dst: n2*2 elements to hold overlapped region, containing the third and
//      second quarters of the IMDCT output, in that order. Q2 is negated and
//      reversed to form Q1 and the two are blended against Q4/Q3 of prev to
//      form the final output.
// prev: Pointer to start of third quarter of IMDCT output for previous
//       block. The fourth quadrant is reconstructed from this by
//       symmetry (reverse). This area is then updated with the Q3 of the
//       current block as that area is replaced by output data.
// window: Pointer to start of left half of overlap windowing function.
//         This is reflected to produce the right half.
// n2: Half the size of the overlap window.

VDNOINLINE void ATVorbisOverlapAdd(
	float *dst,
	float *prev,
	const float *window,
	size_t n2)
{
#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
	ATVorbisOverlapAdd_SSE2(dst, prev, window, n2);
#elif defined(VD_CPU_ARM64)
	ATVorbisOverlapAdd_NEON(dst, prev, window, n2);
#else
	ATVorbisOverlapAdd_Scalar(dst, prev, window, n2);
#endif
}

void ATVorbisOverlapAdd_Scalar(
	float *__restrict dst,
	float *__restrict prev,
	const float *__restrict window,
	size_t n2)
{
	const size_t nm1 = n2*2 - 1;

	for(size_t i = 0; i < n2; ++i) {
		float curnq1 = dst[nm1 - i];	// q2 reversed = -q1
		float prevq3 = prev[i];
		float winq1 = window[i];
		float winq2 = window[nm1 - i];

		// save off q3/q4 of prev into overlap buffer
		prev[i] = dst[i];

		// Overlap q3 of prev with q1=-q2 of current
		dst[i] = prevq3 * winq2 - curnq1 * winq1;

		// Overlap q4=(q3) of prev against q2 of current
		dst[nm1 - i] = prevq3 * winq1 + curnq1 * winq2;
	}	
}

#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
void ATVorbisOverlapAdd_SSE2(
	float *__restrict dst,
	float *__restrict prev,
	const float *__restrict window,
	size_t n2)
{
	const size_t nm4 = n2*2 - 4;

	for(size_t i = 0; i < n2; i += 4) {
		auto curnq1 = _mm_loadu_ps(&dst[nm4 - i]);
		auto prevq3 = _mm_loadu_ps(&prev[i]);
		prevq3 = _mm_shuffle_ps(prevq3, prevq3, 0x1B);

		auto winq1 = _mm_loadu_ps(&window[i]);
		winq1 = _mm_shuffle_ps(winq1, winq1, 0x1B);
		auto winq2 = _mm_loadu_ps(&window[nm4 - i]);

		// save off q3/q4 of prev into overlap buffer
		_mm_storeu_ps(&prev[i], _mm_loadu_ps(&dst[i]));

		// Overlap q3(=q4) of prev with q1(=-q2) of current
		auto r1 = _mm_sub_ps(_mm_mul_ps(prevq3, winq2), _mm_mul_ps(curnq1, winq1));
		_mm_storeu_ps(&dst[i], _mm_shuffle_ps(r1, r1, 0x1B));

		// Overlap q4 of prev against q2 of current
		auto r2 = _mm_add_ps(_mm_mul_ps(prevq3, winq1), _mm_mul_ps(curnq1, winq2));
		_mm_storeu_ps(&dst[nm4 - i], r2);
	}	
}
#endif

#if defined(VD_CPU_ARM64)
void ATVorbisOverlapAdd_NEON(
	float *__restrict dst,
	float *__restrict prev,
	const float *__restrict window,
	size_t n2)
{
	const size_t nm4 = n2*2 - 4;

	for(size_t i = 0; i < n2; i += 4) {
		auto curnq1 = vld1q_f32(&dst[nm4 - i]);
		auto prevq3 = vld1q_f32(&prev[i]);
		prevq3 = vrev64q_f32(vcombine_f32(vget_high_f32(prevq3), vget_low_f32(prevq3)));

		auto winq1 = vld1q_f32(&window[i]);
		winq1 = vrev64q_f32(vcombine_f32(vget_high_f32(winq1), vget_low_f32(winq1)));
		auto winq2 = vld1q_f32(&window[nm4 - i]);

		// save off q3/q4 of prev into overlap buffer
		vst1q_f32(&prev[i], vld1q_f32(&dst[i]));

		// Overlap q3(=q4) of prev with q1(=-q2) of current
		auto r1 = vfmsq_f32(vmulq_f32(prevq3, winq2), curnq1, winq1);
		vst1q_f32(&dst[i], vrev64q_f32(vcombine_f32(vget_high_f32(r1), vget_low_f32(r1))));

		// Overlap q4 of prev against q2 of current
		auto r2 = vfmaq_f32(vmulq_f32(prevq3, winq1), curnq1, winq2);
		vst1q_f32(&dst[nm4 - i], r2);
	}	
}
#endif

///////////////////////////////////////////////////////////////////////////////

void ATVorbisDeinterleaveResidue(float *const *dst, const float *__restrict src, size_t n, size_t numChannels) {
	if (numChannels == 2) {
#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
		return ATVorbisDeinterleaveResidue2_SSE2(dst, src, n);
#elif defined(VD_CPU_ARM64)
		return ATVorbisDeinterleaveResidue2_NEON(dst, src, n);
#else
		return ATVorbisDeinterleaveResidue2_Scalar(dst, src, n);
#endif
	}

	for(size_t i = 0; i < n; ++i) {
		for(size_t j = 0; j < numChannels; ++j) {
			dst[j][i] = *src++;
		}
	}
}

void ATVorbisDeinterleaveResidue2_Scalar(float *const *dst, const float *__restrict src, size_t n) {
	float *__restrict v0 = dst[0];
	float *__restrict v1 = dst[1];

	for(size_t i = 0; i < n; ++i) {
		v0[i] = src[0];
		v1[i] = src[1];

		src += 2;
	}
}

#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
void ATVorbisDeinterleaveResidue2_SSE2(float *const *dst, const float *__restrict src, size_t n) {
	float *__restrict dst1 = dst[0];
	float *__restrict dst2 = dst[1];

	size_t n4 = n >> 2;
	for(size_t i=0; i<n4; ++i) {
		__m128 v0 = _mm_loadu_ps(&src[0]);
		__m128 v1 = _mm_loadu_ps(&src[4]);
		src += 8;

		_mm_storeu_ps(dst1, _mm_shuffle_ps(v0, v1, 0x88));
		dst1 += 4;
		_mm_storeu_ps(dst2, _mm_shuffle_ps(v0, v1, 0xDD));
		dst2 += 4;
	}
}
#endif

#if defined(VD_CPU_ARM64)
void ATVorbisDeinterleaveResidue2_NEON(float *const *dst, const float *__restrict src, size_t n) {
	float *__restrict dst1 = dst[0];
	float *__restrict dst2 = dst[1];

	size_t n4 = n >> 2;
	for(size_t i=0; i<n4; ++i) {
		float32x4x2_t v = vld2q_f32(src);
		src += 8;

		vst1q_f32(dst1, v.val[0]);
		dst1 += 4;
		vst1q_f32(dst2, v.val[1]);
		dst2 += 4;
	}
}
#endif

////////////////////////////////////////////////////////////////////////////////

struct ATVorbisCrcTable {
	constexpr ATVorbisCrcTable()
		: tab{}
	{
		for(uint32_t i=0; i<256; ++i) {
			uint32_t x = i << 24;

			for(int j=0; j<8; ++j)
				x = (x << 1) ^ (x >> 31 ? 0x04c11db7 : 0);

			tab[i] = x;
		}
	}

	uint32_t tab[256];

	static const ATVorbisCrcTable sTable;
};

constexpr ATVorbisCrcTable ATVorbisCrcTable::sTable;

uint32 ATVorbisComputeCRC(const void *header, size_t headerLen, const void *data, size_t dataLen) {
#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
	if (VDCheckAllExtensionsEnabled(CPUF_SUPPORTS_CLMUL | CPUF_SUPPORTS_SSSE3)) {
		uint32 crc = ATVorbisUpdateCRC_SSSE3_CLMUL(0, header, headerLen);
		crc = ATVorbisUpdateCRC_SSSE3_CLMUL(crc, data, dataLen);

		return crc;
	}
#elif defined(VD_CPU_ARM64)
	if (VDCheckAllExtensionsEnabled(VDCPUF_SUPPORTS_CRC32)) {
		uint32 crc = ATVorbisUpdateCRC_ARM64_CRC32(0, header, headerLen);
		crc = ATVorbisUpdateCRC_ARM64_CRC32(crc, data, dataLen);

		return crc;
	}
#endif

	return ATVorbisUpdateCRC_Scalar(ATVorbisUpdateCRC_Scalar(0, header, headerLen), data, dataLen);
}

uint32 ATVorbisUpdateCRC_Scalar(uint32 crc, const void *src, size_t len) {
	const uint8 *VDRESTRICT src8 = (const uint8 *)src;

	while(len--)
		crc = (crc << 8) ^ ATVorbisCrcTable::sTable.tab[(crc >> 24) ^ *src8++];

	return crc;
}

#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
VD_CPU_TARGET("ssse3,pclmul")
uint32 ATVorbisUpdateCRC_SSSE3_CLMUL(uint32 crc, const void *src, size_t len) {
	// This algorithm is based on Intel's white paper, Fast CRC Computation
	// Using PCLMULQDQ Instruction. It is similar to the Ethernet CRC also used
	// by zlib/PNG, but there is a critical difference: the Ogg CRC is
	// computed bit-reversed from the Ethernet CRC. More specifically, the
	// Vorbis CRC is equivalent to the Ethernet CRC with all data bytes
	// bit-reversed, an initial value of 0 instead of ~0, and no final
	// inversion. Since x86 lacks a bit reverse instruction, it is impractical
	// to use an Ethernet CRC based routine and so a different CRC routine is
	// required.
	//
	// See zip.cpp for the base implementation.

	static constexpr uint32 kByteShifts[] {
		UINT32_C(0x00000001),	// [ 0] = x^  0 mod P
		UINT32_C(0x00000100),	// [ 1] = x^  8 mod P
		UINT32_C(0x00010000),	// [ 2] = x^ 16 mod P
		UINT32_C(0x01000000),	// [ 3] = x^ 24 mod P
		UINT32_C(0x04C11DB7),	// [ 4] = x^ 32 mod P
		UINT32_C(0xD219C1DC),	// [ 5] = x^ 40 mod P
		UINT32_C(0x01D8AC87),	// [ 6] = x^ 48 mod P
		UINT32_C(0xDC6D9AB7),	// [ 7] = x^ 56 mod P
		UINT32_C(0x490D678D),	// [ 8] = x^ 64 mod P
		UINT32_C(0x1B280D78),	// [ 9] = x^ 72 mod P
		UINT32_C(0x4F576811),	// [10] = x^ 80 mod P
		UINT32_C(0x5BA1DCCA),	// [11] = x^ 88 mod P
		UINT32_C(0xF200AA66),	// [12] = x^ 96 mod P
		UINT32_C(0x8090A067),	// [13] = x^104 mod P
		UINT32_C(0xF9AC87EE),	// [14] = x^112 mod P
		UINT32_C(0x07F6E306),	// [15] = x^120 mod P
		UINT32_C(0xE8A45605),	// [16] = x^128 mod P
		UINT32_C(0x47F7CEC1),	// [17] = x^136 mod P
		UINT32_C(0xDD0FE172),	// [18] = x^144 mod P
		UINT32_C(0x2FB7BF3A),	// [19] = x^152 mod P
		UINT32_C(0x17D3315D),	// [20] = x^160 mod P
		UINT32_C(0x8167D675),	// [21] = x^168 mod P
		UINT32_C(0x0A1B8859),	// [22] = x^172 mod P
		UINT32_C(0x34028FD6),	// [23] = x^184 mod P
		UINT32_C(0xC5B9CD4C),	// [24] = x^192 mod P
	};

	alignas(16) static constexpr uint8 kByteRev[16] { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 };
	const __m128i byteRev = _mm_load_si128((const __m128i *)kByteRev);

	// (x^192, x^128) mod P
	const __m128i vfold128 = _mm_set_epi64x(kByteShifts[24], kByteShifts[16]);

	// Multiply running CRC by (x^-32 mod P') so the next 128 bits are weighted as
	// x^0..127.
	__m128i vcrc = _mm_clmulepi64_si128(_mm_cvtsi32_si128(crc), _mm_cvtsi32_si128(0xCBF1ACDA), 0x00);

	// do 512-bit chunks
	if (len >= 64) {
		__m128i vcrc0 = _mm_setzero_si128();
		__m128i vcrc1 = vcrc0;
		__m128i vcrc2 = vcrc0;
		__m128i vcrc3 = vcrc;

		static constexpr uint64 x512_modP = UINT64_C(0xE6228B11);
		static constexpr uint64 x576_modP = UINT64_C(0x8833794C);
		__m128i vfold512 = _mm_set_epi64x(x576_modP, x512_modP);
		while(len >= 64) {
			vcrc0 = _mm_xor_si128(
				_mm_clmulepi64_si128(vcrc0, vfold512, 0x11),
				_mm_xor_si128(
					_mm_clmulepi64_si128(vcrc0, vfold512, 0x00),
					_mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)src + 0), byteRev)
				)
			);

			vcrc1 = _mm_xor_si128(
				_mm_clmulepi64_si128(vcrc1, vfold512, 0x11),
				_mm_xor_si128(
					_mm_clmulepi64_si128(vcrc1, vfold512, 0x00),
					_mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)src + 1), byteRev)
				)
			);

			vcrc2 = _mm_xor_si128(
				_mm_clmulepi64_si128(vcrc2, vfold512, 0x11),
				_mm_xor_si128(
					_mm_clmulepi64_si128(vcrc2, vfold512, 0x00),
					_mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)src + 2), byteRev)
				)
			);

			vcrc3 = _mm_xor_si128(
				_mm_clmulepi64_si128(vcrc3, vfold512, 0x11),
				_mm_xor_si128(
					_mm_clmulepi64_si128(vcrc3, vfold512, 0x00),
					_mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)src + 3), byteRev)
				)
			);

			src = (const char *)src + 64;
			len -= 64;
		}

		// fold down from 512 to 128
		static constexpr uint64 x256_modP = UINT64_C(0x75BE46B7);
		static constexpr uint64 x320_modP = UINT64_C(0x569700E5);
		static constexpr uint64 x384_modP = UINT64_C(0x8C3828A8);
		static constexpr uint64 x448_modP = UINT64_C(0x64BF7A9B);

		const __m128i vfold256 = _mm_set_epi64x(x320_modP, x256_modP);
		const __m128i vfold384 = _mm_set_epi64x(x448_modP, x384_modP);

		vcrc = vcrc3;
		vcrc = _mm_xor_si128(vcrc,
			_mm_xor_si128(
				_mm_clmulepi64_si128(vcrc2, vfold128, 0x00),
				_mm_clmulepi64_si128(vcrc2, vfold128, 0x11)
			)
		);
		vcrc = _mm_xor_si128(vcrc,
			_mm_xor_si128(
				_mm_clmulepi64_si128(vcrc1, vfold256, 0x00),
				_mm_clmulepi64_si128(vcrc1, vfold256, 0x11)
			)
		);
		vcrc = _mm_xor_si128(vcrc,
			_mm_xor_si128(
				_mm_clmulepi64_si128(vcrc0, vfold384, 0x00),
				_mm_clmulepi64_si128(vcrc0, vfold384, 0x11)
			)
		);
	}

	// do 128-bit chunks
	while(len >= 16) {
		// multiply current CRC chunks by x^128 mod P and x^192 mod P
		__m128i foldedHi = _mm_clmulepi64_si128(vcrc, vfold128, 0x11);
		__m128i foldedLo = _mm_clmulepi64_si128(vcrc, vfold128, 0x00);

		// load next 128 bits
		__m128i va = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)src), byteRev);
		src = (const char *)src + 16;

		// fold existing CRC and add in another 128 bits of the message
		vcrc = _mm_xor_si128(_mm_xor_si128(va, foldedLo), foldedHi);

		len -= 16;
	}

	// handle leftover bits
	if (len) {
		// multiply running CRC by x^8..120 mod P
		__m128i vfoldnlo = _mm_loadu_si32(&kByteShifts[len]);
		__m128i vfoldnhi = _mm_loadu_si32(&kByteShifts[len + 8]);

		__m128i foldedLo = _mm_clmulepi64_si128(vcrc, vfoldnlo, 0x00);
		__m128i foldedHi = _mm_clmulepi64_si128(vcrc, vfoldnhi, 0x01);

		// fold and add in remaining message bytes
		alignas(16) uint8 buf[16] {};
		memcpy(buf + (16 - len), src, len);

		__m128i va = _mm_shuffle_epi8(_mm_load_si128((const __m128i *)buf), byteRev);
		vcrc = _mm_xor_si128(_mm_xor_si128(va, foldedLo), foldedHi);
	}

	// At this point, we have a full 128-bit CRC bit-reversed in vcrc.
	// First, fold down the high 64 bits to produce a 96-bit intermediate,
	// right-aligned. This also multiplies the low 64 bits by x^32 to make
	// room for the final CRC.
	//
	// 127                                                      0 (reg order)
	//  +-------------+-------------+-------------+-------------+
	//  |     CRC high 64 bits      |      CRC low 64 bits      | vcrc
	//  +-------------+-------------+-------------+-------------+
	//          |                                 +-----------------+ 
	//          |                                                   |
	//          |                   +-------------+-------------+   |
	//          +------------------>|      CRC high 64 bits     |   |
	//                              +-------------+-------------+   |
	//                                            x                 |
	//                                            +-------------+   |
	//                                            | x^96 mod P  |   |
	//                                            +-------------+   |
	//                                            =                 |
	//                +-------------+-------------+-------------+   |
	//                |             CRC high product            |   |
	//                +-------------+-------------+-------------+   |
	//                              +                               |
	//                +-------------+-------------+                 |
	//                |      CRC low 64 bits      |<----------------+
	//                +-------------+-------------+
	//                              =
	// + - - - - - - -+-------------+-------------+-------------+
	// .      0       |               96-bit CRC                |
	// + - - - - - - -+-------------+-------------+-------------+

	vcrc = _mm_xor_si128(
		_mm_slli_si128(_mm_castps_si128(_mm_movelh_ps(_mm_castsi128_ps(vcrc), _mm_setzero_ps())), 4),
		_mm_clmulepi64_si128(vcrc, _mm_loadu_si32(&kByteShifts[12]), 0x01)
	);

	// Fold the high 32-bits. In the bit-reversed register order, this is:
	// vcrc[95:32] ^ vcrc[31:0]*((x^64 mod P') << 31). This leaves us a 64-bit CRC
	// in the low 64 bits.
	//
	// 127                                                      0 (reg order)
	// + - - - - - - -+-------------+-------------+-------------+
	// .      0       | CRC high 32b|      CRC low 64 bits      | vcrc
	// + - - - - - - -+-------------+-------------+-------------+
	//                       x                    +------------------+
	//                +-------------+                                |
	//                | x^64 mod P '|                                |
	//                +-------------+                                |
	//                       |                                       |
	//                       |      +-------------+-------------+    |
	//                       +----->|      CRC high product     |    |
	//                              +-------------+-------------+    |
	//                                                               |
	//                              +-------------+-------------+    |
	//                              |      CRC low 64 bits      |<---+
	//                              +-------------+-------------+
	//                                            =
	// +----------------------------+-------------+-------------+
	// |            ???             |         64-bit CRC        |
	// +----------------------------+-------------+-------------+

	vcrc = _mm_xor_si128(vcrc, _mm_clmulepi64_si128(vcrc, _mm_loadu_si32(&kByteShifts[8]), 0x01));

	// At this point, we now have a 64-bit CRC value in the low
	// 64 bits and need to do a Barrett reduction.
	//
	static constexpr uint64 x64divP = UINT64_C(0x104D101DF);
	const uint64 P = UINT64_C(0x104C11DB7);
	const __m128i redConsts = _mm_set_epi64x(P, x64divP);

	//	T1(x) = floor(R(x)/x^32)*floor(x^64/P(x))
	__m128i t1 = _mm_clmulepi64_si128(_mm_srli_epi64(vcrc, 32), redConsts, 0x00);

	//	T2(x) = floor(T1(x)/x^32)*P(x)
	__m128i t2 = _mm_clmulepi64_si128(_mm_srli_epi64(t1, 32), redConsts, 0x10);

	//	C(x) = R(x) ^ loword(T2(x) mod x^32)
	vcrc = _mm_xor_si128(vcrc, t2);

	// extract running CRC32 in low 32-bits
	return _mm_cvtsi128_si32(vcrc);
}
#endif

#if defined(VD_CPU_ARM64)
VD_CPU_TARGET("+crc")
uint32 ATVorbisUpdateCRC_ARM64_CRC32(uint32 crc, const void *src, size_t len) {
	// The ARM64 version is stupidly simpler than the x64 version because ARM64
	// has a native instruction to calculate the Ethernet CRC. It has all
	// scalar sizes (8/16/32/64-bit) and works on a running CRC32
	// without the final inversion. It does require the optional CRC32 ISA
	// extension, which is common, but technically we still need to do a runtime
	// check.
	//
	// For the Ogg CRC, we also need to bit-reverse each byte.

	// replace when MSVC actually supports __rbit()
	const auto rbit32 = [](uint32 v) -> uint32 {
		return VDSwizzleU32(
			vget_lane_u32(
				vreinterpret_u32_u8(
					vrbit_u8(vreinterpret_u8_u32(vdup_n_u32(v)))
				), 0
			)
		);
	};

	const auto partialUpdate = [](uint32 crc, const void *src, size_t len) VD_CPU_TARGET_LAMBDA("+crc") -> uint32 {
		if (len & 8) {
			uint64x1_t v64 = vreinterpret_u64_u8(vrbit_u8(vld1_u8((const uint8_t *)src)));

			crc = __crc32d(crc, vget_lane_u64(v64, 0));
			src = (const char *)src + 8;
		}

		if (len & 4) {
			uint32_t v32 = VDReadUnalignedU32(src);
			v32 = vget_lane_u32(vreinterpret_u32_u8(vrbit_u8(vreinterpret_u8_u32(vdup_n_u32(v32)))), 0);

			crc = __crc32w(crc, v32);
			src = (const char *)src + 4;
		}

		if (len & 2) {
			uint16_t v16 = VDReadUnalignedU16(src);
			v16 = vget_lane_u16(vreinterpret_u16_u8(vrbit_u8(vreinterpret_u8_u16(vdup_n_u16(v16)))), 0);

			crc = __crc32h(crc, v16);
			src = (const char *)src + 2;
		}

		if (len & 1) {
			uint8_t v8 = *(const uint8 *)src;
			v8 = vget_lane_u8(vrbit_u8(vdup_n_u8(v8)), 0);

			crc = __crc32b(crc, v8);
		}

		return crc;
	};

	// bit reverse CRC back to compatible order
	crc = rbit32(crc);

	// check for pre-alignment
	size_t alignLen = ((size_t)0 - len) & 15;
	if (alignLen) {
		if (alignLen > len)
			alignLen = len;

		crc = partialUpdate(crc, src, alignLen);
		src = (const char *)src + alignLen;
		len -= alignLen;
	}

	// do large blocks
	if (len >= 64) {
		size_t numLargeBlocks = len >> 6;
		do {
			uint64x2_t v0 = vreinterpretq_u64_u8(vrbitq_u8(vld1q_u8((const uint8_t *)src + 0)));
			uint64x2_t v1 = vreinterpretq_u64_u8(vrbitq_u8(vld1q_u8((const uint8_t *)src + 16)));
			uint64x2_t v2 = vreinterpretq_u64_u8(vrbitq_u8(vld1q_u8((const uint8_t *)src + 32)));
			uint64x2_t v3 = vreinterpretq_u64_u8(vrbitq_u8(vld1q_u8((const uint8_t *)src + 48)));

			crc = __crc32d(crc, vgetq_lane_u64(v0, 0));
			crc = __crc32d(crc, vgetq_lane_u64(v0, 1));
			crc = __crc32d(crc, vgetq_lane_u64(v1, 0));
			crc = __crc32d(crc, vgetq_lane_u64(v1, 1));
			crc = __crc32d(crc, vgetq_lane_u64(v2, 0));
			crc = __crc32d(crc, vgetq_lane_u64(v2, 1));
			crc = __crc32d(crc, vgetq_lane_u64(v3, 0));
			crc = __crc32d(crc, vgetq_lane_u64(v3, 1));

			src = (const char *)src + 64;
		} while(--numLargeBlocks);

		len &= 63;
	}

	// do small blocks
	while(len >= 16) {
		uint64x2_t v = vreinterpretq_u64_u8(vrbitq_u8(vld1q_u8((const uint8_t *)src)));
		crc = __crc32d(crc, vgetq_lane_u64(v, 0));
		crc = __crc32d(crc, vgetq_lane_u64(v, 1));
		src = (const char *)src + 16;
		len -= 16;
	}

	// do tail
	return rbit32(partialUpdate(crc, src, len));
}
#endif

////////////////////////////////////////////////////////////////////////////////

void ATVorbisConvertF32ToS16(sint16 *dst, const float *src, size_t n) {
#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
	return ATVorbisConvertF32ToS16_SSE2(dst, src, n);
#elif defined(VD_CPU_ARM64)
	return ATVorbisConvertF32ToS16_NEON(dst, src, n);
#else
	return ATVorbisConvertF32ToS16_Scalar(dst, src, n);
#endif
}

void ATVorbisConvertF32ToS16x2(sint16 *dst, const float *src1, const float *src2, size_t n) {
#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
	return ATVorbisConvertF32ToS16x2_SSE2(dst, src1, src2, n);
#elif defined(VD_CPU_ARM64)
	return ATVorbisConvertF32ToS16x2_NEON(dst, src1, src2, n);
#else
	return ATVorbisConvertF32ToS16x2_Scalar(dst, src1, src2, n);
#endif
}

void ATVorbisConvertF32ToS16Rep2(sint16 *dst, const float *src, size_t n) {
#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
	return ATVorbisConvertF32ToS16Rep2_SSE2(dst, src, n);
#elif defined(VD_CPU_ARM64)
	return ATVorbisConvertF32ToS16Rep2_NEON(dst, src, n);
#else
	return ATVorbisConvertF32ToS16Rep2_Scalar(dst, src, n);
#endif
}

void ATVorbisConvertF32ToS16_Scalar(sint16 *dst, const float *src, size_t n) {
	sint16 *VDRESTRICT dstr = dst;

	for(size_t i=0; i<n; ++i) {
		*dstr++ = (sint16)VDRoundToInt32(std::max(std::min(*src++, 1.0f), -1.0f) * 32767.0f);
	}
}

void ATVorbisConvertF32ToS16x2_Scalar(sint16 *dst, const float *src1, const float *src2, size_t n) {
	sint16 *VDRESTRICT dstr = dst;

	for(size_t i=0; i<n; ++i) {
		*dstr++ = (sint16)VDRoundToInt32(std::max(std::min(*src1++, 1.0f), -1.0f) * 32767.0f);
		*dstr++ = (sint16)VDRoundToInt32(std::max(std::min(*src2++, 1.0f), -1.0f) * 32767.0f);
	}
}

void ATVorbisConvertF32ToS16Rep2_Scalar(sint16 *dst, const float *src, size_t n) {
	sint16 *VDRESTRICT dstr = dst;

	for(size_t i=0; i<n; ++i) {
		const sint16 v = (sint16)VDRoundToInt32(std::max(std::min(*src++, 1.0f), -1.0f) * 32767.0f);
		*dstr++ = v;
		*dstr++ = v;
	}
}

#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
void ATVorbisConvertF32ToS16_SSE2(sint16 *dst, const float *src, size_t n) {
	if (const size_t n4 = n/4) {
		sint16 *VDRESTRICT dstr = dst;

		const __m128 c1 = _mm_set1_ps(1.0f);
		const __m128 cn1 = _mm_set1_ps(-1.0f);
		const __m128 cscale = _mm_set1_ps(32767.0f);

		for(size_t i=0; i<n4; ++i) {
			__m128 v = _mm_loadu_ps(src);
			src += 4;

			__m128i vi = _mm_cvtps_epi32(_mm_mul_ps(_mm_max_ps(_mm_min_ps(v, c1), cn1), cscale));

			_mm_storel_epi64((__m128i *)dstr, _mm_packs_epi32(vi, vi));
			dstr += 4;
		}

		dst = dstr;
	}

	ATVorbisConvertF32ToS16_Scalar(dst, src, n & 3);
}

void ATVorbisConvertF32ToS16x2_SSE2(sint16 *dst, const float *src1, const float *src2, size_t n) {
	if (const size_t n4 = n/4) {
		sint16 *VDRESTRICT dstr = dst;

		const __m128 c1 = _mm_set1_ps(1.0f);
		const __m128 cn1 = _mm_set1_ps(-1.0f);
		const __m128 cscale = _mm_set1_ps(32767.0f);

		for(size_t i=0; i<n4; ++i) {
			__m128 vl = _mm_loadu_ps(src1);
			__m128 vr = _mm_loadu_ps(src2);
			src1 += 4;
			src2 += 4;

			__m128 v1 = _mm_unpacklo_ps(vl, vr);
			__m128 v2 = _mm_unpackhi_ps(vl, vr);

			__m128i vi1 = _mm_cvtps_epi32(_mm_mul_ps(_mm_max_ps(_mm_min_ps(v1, c1), cn1), cscale));
			__m128i vi2 = _mm_cvtps_epi32(_mm_mul_ps(_mm_max_ps(_mm_min_ps(v2, c1), cn1), cscale));

			_mm_storeu_si128((__m128i *)dstr, _mm_packs_epi32(vi1, vi2));
			dstr += 8;
		}

		dst = dstr;
	}

	ATVorbisConvertF32ToS16x2_Scalar(dst, src1, src2, n & 3);
}

void ATVorbisConvertF32ToS16Rep2_SSE2(sint16 *dst, const float *src, size_t n) {
	if (const size_t n4 = n/4) {
		sint16 *VDRESTRICT dstr = dst;

		const __m128 c1 = _mm_set1_ps(1.0f);
		const __m128 cn1 = _mm_set1_ps(-1.0f);
		const __m128 cscale = _mm_set1_ps(32767.0f);

		for(size_t i=0; i<n4; ++i) {
			__m128 v = _mm_loadu_ps(src);
			src += 4;

			__m128i vi = _mm_cvtps_epi32(_mm_mul_ps(_mm_max_ps(_mm_min_ps(v, c1), cn1), cscale));

			vi = _mm_packs_epi32(vi, vi);

			_mm_storeu_si128((__m128i *)dstr, _mm_unpacklo_epi16(vi, vi));
			dstr += 8;
		}
	
		dst = dstr;
	}

	ATVorbisConvertF32ToS16Rep2_Scalar(dst, src, n & 3);
}
#endif

#if defined(VD_CPU_ARM64)
void ATVorbisConvertF32ToS16_NEON(sint16 *dst, const float *src, size_t n) {
	if (const size_t n4 = n/4) {
		sint16 *VDRESTRICT dstr = dst;

		const int16x4_t cmin = vmov_n_s16(-32767);
		const float32x4_t cscale = vmovq_n_f32(32767.0f);

		for(size_t i=0; i<n4; ++i) {
			float32x4_t v = vld1q_f32(src);
			src += 4;

			int16x4_t vi = vmax_s16(vqmovn_s32(vcvtnq_s32_f32(vmulq_f32(v, cscale))), cmin);

			vst1_s16(dstr, vi);
			dstr += 4;
		}

		dst = dstr;
	}

	ATVorbisConvertF32ToS16_Scalar(dst, src, n & 3);
}

void ATVorbisConvertF32ToS16x2_NEON(sint16 *dst, const float *src1, const float *src2, size_t n) {
	if (const size_t n4 = n/4) {
		sint16 *VDRESTRICT dstr = dst;

		const int16x4_t cmin = vmov_n_s16(-32767);
		const float32x4_t cscale = vmovq_n_f32(32767.0f);

		for(size_t i=0; i<n4; ++i) {
			float32x4_t vl = vld1q_f32(src1);
			float32x4_t vr = vld1q_f32(src2);
			src1 += 4;
			src2 += 4;

			int16x4x2_t vi;
			vi.val[0] = vmax_s16(vqmovn_s32(vcvtnq_s32_f32(vmulq_f32(vl, cscale))), cmin);
			vi.val[1] = vmax_s16(vqmovn_s32(vcvtnq_s32_f32(vmulq_f32(vr, cscale))), cmin);

			vst2_s16(dstr, vi);
			dstr += 8;
		}

		dst = dstr;
	}

	ATVorbisConvertF32ToS16x2_Scalar(dst, src1, src2, n & 3);
}

void ATVorbisConvertF32ToS16Rep2_NEON(sint16 *dst, const float *src, size_t n) {
	if (const size_t n4 = n/4) {
		sint16 *VDRESTRICT dstr = dst;

		const int16x4_t cmin = vmov_n_s16(-32767);
		const float32x4_t cscale = vmovq_n_f32(32767.0f);

		for(size_t i=0; i<n4; ++i) {
			float32x4_t v = vld1q_f32(src);
			src += 4;

			int16x4x2_t vi;
			vi.val[0] = vmax_s16(vqmovn_s32(vcvtnq_s32_f32(vmulq_f32(v, cscale))), cmin);
			vi.val[1] = vi.val[0];

			vst2_s16(dstr, vi);
			dstr += 8;
		}

		dst = dstr;
	}

	ATVorbisConvertF32ToS16Rep2_Scalar(dst, src, n & 3);
}
#endif
