//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2019 Avery Lee
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
#include <vd2/system/cpuaccel.h>
#include <vd2/system/math.h>
#include <vd2/system/vdstl.h>
#include <vd2/VDDisplay/displaytypes.h>
#include <vd2/VDDisplay/internal/screenfx.h>

void VDDisplayCreateGammaRamp(uint32 *gammaTex, uint32 len, bool enableInputConversion, float outputGamma, float gammaAdjust) {
	float correctionFactor = 1.0f / gammaAdjust;

	bool useSRGB = false;
	if (enableInputConversion) {
		if (outputGamma > 0)
			correctionFactor /= outputGamma;
		else
			useSRGB = true;
	}

	float inputScale = 1.0f / (float)(len - 1);
	for(uint32 i=0; i<len; ++i) {
		float x = (float)i * inputScale;

		if (useSRGB) {
			// sRGB gamma
			x = (x < 0.0031308f) ? x * 12.92f : 1.055f * powf(x, 1.0f / 2.4f) - 0.055f;
		}

		float y = powf(x, correctionFactor);
		uint32 px = (uint32)(y * 255.0f + 0.5f) * 0x01010101;

		gammaTex[i] = px;
	}
}

void VDDisplayCreateScanlineMaskTexture(uint32 *scanlineTex, ptrdiff_t pitch, uint32 srcH, uint32 dstH, float outY, float outH, uint32 texSize, float intensity, bool halfLineOffset, bool renderLinear) {
	vdfastvector<float> rawMask(texSize, 1.0f);

	// Compute the stepping rate over the scanline mask pattern and check if we are
	// undersampling the mask (vertical resolution below 2 pixel rows per scanline).
	// Since the mask is a raised cosine, we can trivially apply a brickwall low pass
	// filter to it: just reduce it to DC-only below the Nyquist rate.

	float dvdy = (float)srcH / (float)outH;
	if (dvdy <= 0.5f) {
		// Straight mapping would place the peak of the raised cosine in the center of
		// each scanline. We shift the pattern up by 1/4 scanline so that the two halves
		// of the scanline are full bright and full dark.

		float v = 0.25f + dvdy * (0.5f - outY);

		for(uint32 i=0; i<dstH; ++i) {
			float y = 0.5f - 0.5f * cosf((v - floorf(v)) * nsVDMath::kfTwoPi);
			v += dvdy;

			rawMask[i] = y;
		}
	} else {
		std::fill(std::begin(rawMask), std::end(rawMask), 0.5f);
	}

	// Apply scanline intensity setting and convert the mask from linear to gamma
	// space. The intensity setting is adjusted so that the specified level is
	// achieved in gamma space. Squaring works pretty well here, we can do a teeny
	// bit better with a 2.2 approximation (true sRGB is problematic as it's
	// piecewise).

	intensity = powf(intensity, 2.2f);
	for(float& y : rawMask) {
		y = y * (1.0f - intensity) + intensity;

		if (!renderLinear)
			y = powf(y, 1.0f / 2.2f);
	}

	// Repeat the last entry to the end of the texture so it clamps cleanly.
	if (dstH < texSize && dstH > 0)
		std::fill(rawMask.begin() + dstH, rawMask.end(), rawMask[dstH - 1]);

	// Convert the mask to texels.
	for(uint32 i=0; i<texSize; ++i) {
		float y = rawMask[i];

		uint32 px = (uint32)(y * 255.0f + 0.5f) * 0x01010101;

		*scanlineTex = px;

		scanlineTex = (uint32 *)((char *)scanlineTex + pitch);
	}
}

////////////////////////////////////////////////////////////////////////////////

VDDisplayApertureGrilleParams::VDDisplayApertureGrilleParams(const VDDScreenMaskParams& maskParams, float dstW, float srcW) {
	mPixelsPerTriad = maskParams.mSourcePixelsPerDot * dstW / srcW;
	mRedCenter = 1.0f / 6.0f;
	mGrnCenter = 3.0f / 6.0f;
	mBluCenter = 5.0f / 6.0f;
	mRedWidth = 10.0f / 60.0f * maskParams.mOpenness;
	mGrnWidth = 10.0f / 60.0f * maskParams.mOpenness;
	mBluWidth = 10.0f / 60.0f * maskParams.mOpenness;
}

////////////////////////////////////////////////////////////////////////////////

void VDDisplayRenderApertureGrilleChannel(
	uint32 *tex,
	uint32 w,
	uint32 channelMask,
	float pixelsPerDot,
	float dotCenter,
	float dotWidth,
	float dstX)
{
	float x1 = (dotCenter - dotWidth) * pixelsPerDot;
	float x2 = (dotCenter + dotWidth) * pixelsPerDot;
	uint32 channelScale = 0x010101 & channelMask;

	x1 += dstX;
	x2 += dstX;

	float offsetX = floorf(x2 / pixelsPerDot) * pixelsPerDot;
	x1 -= offsetX;
	x2 -= offsetX;

	float intensity4 = 0;
	float intensity3 = 0;
	float intensity2 = 0;

	while(w--) {
		float intensity = intensity2;
		intensity2 = intensity3;
		intensity3 = intensity4;
		intensity4 = 0;

		while(x1 < 1.0f) {
			float xn1 = std::min(1.0f, x1);
			float xn2 = std::min(1.0f, x2);

			xn1 = std::max(0.0f, xn1);
			xn2 = std::max(0.0f, xn2);

			// accumulate left half triangle filter
			const float tri = xn2*xn2 - xn1*xn1;
			const float linear = xn2 - xn1;

			intensity4 += tri;
			intensity3 += tri + linear*2;

			// accumulate right half triangle filter
			intensity2 += linear*4 - tri;
			intensity += linear*2 - tri;

			if (x2 >= 1.0f)
				break;

			x1 += pixelsPerDot;
			x2 += pixelsPerDot;
		}

		x1 -= 1.0f;
		x2 -= 1.0f;

		*tex++ |= channelScale * (int)(0.5f + intensity * (255.0f * 0.125f));
	}
}

void VDDisplayCreateApertureGrilleTexture(uint32 *tex, uint32 w, float dstX, const VDDisplayApertureGrilleParams& params) {
	memset(tex, 0, sizeof(tex[0]) * w);

	const float ppt = params.mPixelsPerTriad;

	VDDisplayRenderApertureGrilleChannel(
		tex,
		w,
		0xFF0000,
		ppt,
		params.mRedCenter,
		params.mRedWidth,
		dstX
	);

	VDDisplayRenderApertureGrilleChannel(
		tex,
		w,
		0x00FF00,
		ppt,
		params.mGrnCenter,
		params.mGrnWidth,
		dstX
	);

	VDDisplayRenderApertureGrilleChannel(
		tex,
		w,
		0x0000FF,
		ppt,
		params.mBluCenter,
		params.mBluWidth,
		dstX
	);
}


////////////////////////////////////////////////////////////////////////////////

VDDisplaySlotMaskParams::VDDisplaySlotMaskParams(const VDDScreenMaskParams& maskParams, float dstW, float srcW) {
	const float scale = maskParams.mSourcePixelsPerDot * dstW / (float)srcW;

	mPixelsPerBlockH = scale;
	mPixelsPerBlockV = scale;

	mRedCenter = scale * (0.5f / 3.0f);
	mGrnCenter = scale * (1.5f / 3.0f);
	mBluCenter = scale * (2.5f / 3.0f);

	const float slotHalfWidth = scale * (0.5f / 3.0f) * maskParams.mOpenness;
	mRedWidth = slotHalfWidth;
	mGrnWidth = slotHalfWidth;
	mBluWidth = slotHalfWidth;

	const float slotHalfHeight = mPixelsPerBlockV * 0.5f * ((2.0f + maskParams.mOpenness)/3.0f);
	mRedHeight = slotHalfHeight;
	mGrnHeight = slotHalfHeight;
	mBluHeight = slotHalfHeight;
}

////////////////////////////////////////////////////////////////////////////////

void VDDisplayBlendMasks_Scalar(uint32 *dst0, const uint32 *src00, const uint32 *src10, uint32 w, float weight0, float weight1) {
	const uint32 iweight0 = VDRoundToInt32(weight0 * 256.0f);
	const uint32 iweight1 = VDRoundToInt32(weight1 * 256.0f);

	const uint32 *VDRESTRICT src0 = src00;
	const uint32 *VDRESTRICT src1 = src10;
	uint32 *VDRESTRICT dst = dst0;

	for(uint32 x = 0; x < w; ++ x) {
		const uint32 px0 = src0[x];
		const uint32 px1 = src1[x];
		const uint32 rb0 = px0 & 0xFF00FF;
		const uint32 rb1 = px1 & 0xFF00FF;
		const uint32 g0 = px0 & 0xFF00;
		const uint32 g1 = px1 & 0xFF00;

		const uint32 rb = (rb0 * iweight0 + rb1 * iweight1 + 0x800080) & 0xFF00FF00;
		const uint32 g = (g0 * iweight0 + g1 * iweight1 + 0x8000) & 0xFF0000;

		dst[x] = (rb + g) >> 8;
	}
}

#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
VD_CPU_TARGET("ssse3")
void VDDisplayBlendMasks_SSSE3(uint32 *dst0, const uint32 *src00, const uint32 *src10, uint32 w, float weight0, float weight1) {
	const uint32 iweight0 = VDRoundToInt32(weight0 * 128.0f);
	const uint32 iweight1 = VDRoundToInt32(weight1 * 128.0f);

	const uint32 *VDRESTRICT src0 = src00;
	const uint32 *VDRESTRICT src1 = src10;
	uint32 *VDRESTRICT dst = dst0;

	uint32 w4 = w >> 2;
	w &= 3;

	const __m128i factors = _mm_set1_epi16(((0U-iweight0) & 0xFF) + ((0U-iweight1 & 0xFF) << 8));
	const __m128i round = _mm_set1_epi16(64);

	for(uint32 x4 = 0; x4 < w4; ++x4) {
		const __m128i px0 = _mm_loadu_si128((const __m128i *)src0);
		const __m128i px1 = _mm_loadu_si128((const __m128i *)src1);

		src0 += 4;
		src1 += 4;

		const __m128i ra = _mm_srai_epi16(_mm_sub_epi16(round, _mm_maddubs_epi16(_mm_unpacklo_epi8(px0, px1), factors)), 7);
		const __m128i rb = _mm_srai_epi16(_mm_sub_epi16(round, _mm_maddubs_epi16(_mm_unpackhi_epi8(px0, px1), factors)), 7);
		const __m128i r = _mm_packus_epi16(ra, rb);

		_mm_storeu_si128((__m128i *)dst, r);
		dst += 4;
	}

	for(uint32 x = 0; x < w; ++x) {
		const __m128i px0 = _mm_loadu_si32(src0++);
		const __m128i px1 = _mm_loadu_si32(src1++);

		const __m128i ra = _mm_srai_epi16(_mm_sub_epi16(round, _mm_maddubs_epi16(_mm_unpacklo_epi8(px0, px1), factors)), 7);
		const __m128i r = _mm_packus_epi16(ra, ra);

		*dst++ = _mm_cvtsi128_si32(r);
	}
}
#elif defined(VD_CPU_ARM64)
void VDDisplayBlendMasks_NEON(uint32 *dst0, const uint32 *src00, const uint32 *src10, uint32 w, float weight0, float weight1) {
	const uint32 iweight0 = VDRoundToInt32(weight0 * 128.0f);
	const uint32 iweight1 = VDRoundToInt32(weight1 * 128.0f);

	const uint32 *VDRESTRICT src0 = src00;
	const uint32 *VDRESTRICT src1 = src10;
	uint32 *VDRESTRICT dst = dst0;

	uint32 w4 = w >> 2;
	w &= 3;

	const uint8x16_t factor0 = vdupq_n_u8(iweight0);
	const uint8x16_t factor1 = vdupq_n_u8(iweight1);

	for(uint32 x4 = 0; x4 < w4; ++x4) {
		const uint8x16_t px0 = vreinterpretq_u8_u32(vld1q_u32(src0));
		const uint8x16_t px1 = vreinterpretq_u8_u32(vld1q_u32(src1));

		src0 += 4;
		src1 += 4;

		const uint8x8_t ra = vqrshrn_n_u16(vmlal_u8(vmull_u8(vget_low_u8(px0), vget_low_u8(factor0)), vget_low_u8(px1), vget_low_u8(factor1)), 7);
		const uint8x8_t rb = vqrshrn_n_u16(vmlal_high_u8(vmull_high_u8(px0, factor0), px1, factor1), 7);

		vst1_u32(dst + 0, vreinterpret_u32_u8(ra));
		vst1_u32(dst + 2, vreinterpret_u32_u8(rb));
		dst += 4;
	}

	for(uint32 x = 0; x < w; ++x) {
		const uint8x8_t px0 = vreinterpret_u8_u32(vdup_n_u32(*src0++));
		const uint8x8_t px1 = vreinterpret_u8_u32(vdup_n_u32(*src1++));

		const uint8x8_t ra = vqrshrn_n_u16(vmlal_u8(vmull_u8(px0, vget_low_u8(factor0)), px1, vget_low_u8(factor1)), 7);

		*dst++ = vget_lane_u32(vreinterpret_u32_u8(ra), 0);
	}
}
#endif

void VDDisplayBlendMasks(uint32 *dst, const uint32 *src0, const uint32 *src1, uint32 w, float weight0, float weight1) {
#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
	if (VDCheckAllExtensionsEnabled(CPUF_SUPPORTS_SSE2 | CPUF_SUPPORTS_SSSE3)) {
		return VDDisplayBlendMasks_SSSE3(dst, src0, src1, w, weight0, weight1);
	}

	return VDDisplayBlendMasks_Scalar(dst, src0, src1, w, weight0, weight1);
#elif defined(VD_CPU_ARM64)
	return VDDisplayBlendMasks_NEON(dst, src0, src1, w, weight0, weight1);
#endif
}

void VDDisplayCreateSlotMaskTexture(uint32 *tex, ptrdiff_t pitch, uint32 w, uint32 h, float dstX, float dstY, float dstW, float dstH, const VDDisplaySlotMaskParams& params) {
	// pre-render masks for the even and odd stripes using the aperture grille path
	VDDisplayApertureGrilleParams evenParams;
	evenParams.mPixelsPerTriad = params.mPixelsPerBlockH * 2;
	evenParams.mRedCenter = params.mRedCenter * 0.5f / params.mPixelsPerBlockH;
	evenParams.mRedWidth = params.mRedWidth * 0.5f / params.mPixelsPerBlockH;
	evenParams.mGrnCenter = params.mGrnCenter * 0.5f / params.mPixelsPerBlockH;
	evenParams.mGrnWidth = params.mGrnWidth * 0.5f / params.mPixelsPerBlockH;
	evenParams.mBluCenter = params.mBluCenter * 0.5f / params.mPixelsPerBlockH;
	evenParams.mBluWidth = params.mBluWidth * 0.5f / params.mPixelsPerBlockH;

	VDDisplayApertureGrilleParams oddParams = evenParams;
	oddParams.mRedCenter += 0.5f;
	oddParams.mGrnCenter += 0.5f;
	oddParams.mBluCenter += 0.5f;

	vdfastvector<uint32> evenMask(w, 0);
	vdfastvector<uint32> oddMask(w, 0);

	VDDisplayCreateApertureGrilleTexture(evenMask.data(), w, dstX, evenParams);
	VDDisplayCreateApertureGrilleTexture(oddMask.data(), w, dstX, oddParams);

	const float pixelsPerBlockV = params.mPixelsPerBlockV;
	const float slotHalfHeight = params.mRedHeight;

	for(uint32 y = 0; y < h; ++y) {
		// compute even/odd weights for this scanline
		const float dstY2 = dstY + (float)y;
		float slotWeights[2] {};

		for(int i=0; i<16; ++i) {
			float fy = ((float)i + 0.5f) / 16.0f + dstY2;

			float dy1 = fy;
			float dy2 = fy - pixelsPerBlockV * 0.5f;

			dy1 -= VDRoundFast(dy1 / pixelsPerBlockV) * pixelsPerBlockV;
			dy2 -= VDRoundFast(dy2 / pixelsPerBlockV) * pixelsPerBlockV;

			if (fabsf(dy1) < slotHalfHeight)
				slotWeights[0] += 1.0f / 16.0f;

			if (fabsf(dy2) < slotHalfHeight)
				slotWeights[1] += 1.0f / 16.0f;
		}

		// blend the even and odd masks together to form final scanline mask
		VDDisplayBlendMasks(tex, evenMask.data(), oddMask.data(), w, slotWeights[0], slotWeights[1]);

		tex = (uint32 *)((char *)tex + pitch);
	}
}

////////////////////////////////////////////////////////////////////////////////

VDDisplayTriadDotMaskParams::VDDisplayTriadDotMaskParams(const VDDScreenMaskParams& maskParams, float dstW, float srcW) {
	const float scale = maskParams.mSourcePixelsPerDot / 1.5f * dstW / (float)srcW;
	const float r3d2 = sqrtf(3.0f) * 0.5f;

	mPixelsPerTriadH = scale * 3.0f;
	mPixelsPerTriadV = scale * r3d2 * 2.0f;

	mRedCenter[0][0] = scale * 0.5f;
	mRedCenter[0][1] = scale * 0.5f;
	mGrnCenter[0][0] = scale * 1.5f;
	mGrnCenter[0][1] = scale * 0.5f;
	mBluCenter[0][0] = scale * 1.0f;
	mBluCenter[0][1] = scale * (0.5f + r3d2);

	mRedCenter[1][0] = scale * 2.0f;
	mRedCenter[1][1] = scale * (0.5f + r3d2);
	mGrnCenter[1][0] = scale * 3.0f;
	mGrnCenter[1][1] = scale * (0.5f + r3d2);
	mBluCenter[1][0] = scale * 2.5f;
	mBluCenter[1][1] = scale * 0.5f;

	const float dotRadius = scale * 0.5f * maskParams.mOpenness;
	mRedWidth = dotRadius;
	mGrnWidth = dotRadius;
	mBluWidth = dotRadius;
}

////////////////////////////////////////////////////////////////////////////////

void VDDisplayRenderDotMaskChannel_Scalar(uint32 *tex, uint32 w, uint32 channelMask,
	float pixelsPerTriadH,
	float pixelsPerTriadV,
	float posX1, float posY1, float posX2, float posY2, float dotRadius, float dstX, float dstY,
	float *prevScan0)
{
	// establish Y centerlines
	const float yc1 = posY1;
	const float yc2 = posY2;

	// set up X positions and spans
	posX1 += dotRadius + dstX;
	posX2 += dotRadius + dstX;

	float dotRightPos[2] {
		posX1 - ceilf(posX1 / pixelsPerTriadH) * pixelsPerTriadH,
		posX2 - ceilf(posX2 / pixelsPerTriadH) * pixelsPerTriadH
	};

	float dotSpans[2][2][8] {};

	const float dotRadiusSq = dotRadius * dotRadius;
	for(int i=0; i<8; ++i) {
		float y0 = ((float)i + 0.5f) / 8.0f + dstY;

		float dx1a = 0;
		float dx2a = 0;

		for(int j=0; j<8; ++j) {
			float y = y0 + ((float)j - 3.5f) / 64.0f;
			float dy1 = yc1 - y;
			float dy2 = yc2 - y;

			dy1 -= roundf(dy1 / pixelsPerTriadV) * pixelsPerTriadV;
			dy2 -= roundf(dy2 / pixelsPerTriadV) * pixelsPerTriadV;

			const float rsq1 = std::max<float>(0.0f, dotRadiusSq - dy1*dy1);
			const float rsq2 = std::max<float>(0.0f, dotRadiusSq - dy2*dy2);
			const float dx1 = sqrtf(rsq1);
			const float dx2 = sqrtf(rsq2);

			dx1a += dx1;
			dx2a += dx2;
		}

		dx1a *= 1.0f / 8.0f;
		dx2a *= 1.0f / 8.0f;

		dotSpans[0][0][i] = -dotRadius - dx1a;
		dotSpans[0][1][i] = -dotRadius + dx1a;
		dotSpans[1][0][i] = -dotRadius - dx2a;
		dotSpans[1][1][i] = -dotRadius + dx2a;
	}

	// render scanlines
	const uint32 channelScale = 0x010101 & channelMask;
	const float encodingScale = 255.0f * (float)channelScale / 256.0f;
	const float encodingAdd = (float)channelScale * 0.5f;

	const float triMask[8] {
		 1.0f * encodingScale,
		 3.0f * encodingScale,
		 5.0f * encodingScale,
		 7.0f * encodingScale,
		 9.0f * encodingScale,
		11.0f * encodingScale,
		13.0f * encodingScale,
		15.0f * encodingScale,
	};

	const float invTriMask = 16.0f * encodingScale;

	// scalar implementation
	float *VDRESTRICT prevScan = prevScan0;

	float prevLeftSum1 = 0;
	float prevLeftSum2 = 0;
	while(w--) {
		float rightSum1 = 0;
		float leftSum1 = 0;
		float rightSum2 = 0;
		float leftSum2 = 0;

		for(int i = 0; i < 2; ++i) {
			float xbase = dotRightPos[i];

			while(xbase <= 0.0f)
				xbase += pixelsPerTriadH;

			if (xbase < 1.0f + 2*dotRadius) {
				float xbase2 = xbase;

				const auto& VDRESTRICT dotSpansX1 = dotSpans[i][0];
				const auto& VDRESTRICT dotSpansX2 = dotSpans[i][1];

				do {
					for(int j=0; j<8; ++j) {
						// compute scanline intersection with disc
						float x1 = xbase2 + dotSpansX1[j];
						float x2 = xbase2 + dotSpansX2[j];

						// clip span to pixel width
						// manually min/max as MSVC can't vectorize through max(min)
						x2 = x2 < 1.0f ? x2 : 1.0f;
						x1 = x1 > 0.0f ? x1 : 0.0f;
						x1 = x1 < x2 ? x1 : x2;

						// integrate range over both halves of horizontal triangle filter
						// (1-x1)*(1-x1) - (1-x2)*(1-x2) = 2(x2-x1) - (x2^2 - x1^2)
						const float xs = x2+x1;
						const float dx = x2-x1;
						const float ls = dx*xs;
						const float rs = dx;			// *2 and -leftSum done at end

						const float s2 = triMask[j];
						const float s1 = invTriMask - s2;

						// accumulate sums for vertical triangle filter
						leftSum1 += ls * s1;
						rightSum1 += rs * s1;

						leftSum2 += ls * s2;
						rightSum2 += rs * s2;
					}

					xbase2 += pixelsPerTriadH;
				} while(xbase2 < 1.0f + 2*dotRadius);
			}

			dotRightPos[i] = xbase - 1.0f;
		}

		const float intensity1 = (rightSum1 * 2 - leftSum1 + prevLeftSum1);
		const float intensity2 = (rightSum2 * 2 - leftSum2 + prevLeftSum2);

		prevLeftSum1 = leftSum1;
		prevLeftSum2 = leftSum2;

		*tex++ |= channelMask & (int)((intensity1 + *prevScan) + encodingAdd);

		*prevScan++ = intensity2;
	}
}

#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
VD_CPU_TARGET("avx2,fma")
void VDDisplayRenderDotMaskChannel_AVX2(uint32 *tex, uint32 w, uint32 channelMask,
	float pixelsPerTriadH,
	float pixelsPerTriadV,
	float posX1, float posY1, float posX2, float posY2, float dotRadius, float dstX, float dstY,
	float *prevScan0)
{
	// establish Y centerlines
	const float yc1 = posY1;
	const float yc2 = posY2;

	// set up X positions and spans
	posX1 += dotRadius + dstX;
	posX2 += dotRadius + dstX;

	__m256 dotRightPos[2] {
		_mm256_set1_ps(posX1 - ceilf(posX1 / pixelsPerTriadH) * pixelsPerTriadH),
		_mm256_set1_ps(posX2 - ceilf(posX2 / pixelsPerTriadH) * pixelsPerTriadH)
	};

	alignas(32) float dotSpans[2][2][8] {};

	const __m256 vppth = _mm256_set1_ps(pixelsPerTriadH);
	const __m256 vone = _mm256_set1_ps(1.0f);

	alignas(32) static constexpr float kSublineOffsets[8] {
		0.5f / 8.0f - 3.5f / 64.0f,
		1.5f / 8.0f - 3.5f / 64.0f,
		2.5f / 8.0f - 3.5f / 64.0f,
		3.5f / 8.0f - 3.5f / 64.0f,
		4.5f / 8.0f - 3.5f / 64.0f,
		5.5f / 8.0f - 3.5f / 64.0f,
		6.5f / 8.0f - 3.5f / 64.0f,
		7.5f / 8.0f - 3.5f / 64.0f,
	};

	{
		__m256 vdotRadius = _mm256_set1_ps(dotRadius);
		__m256 vdotRadiusSq = _mm256_mul_ps(vdotRadius, vdotRadius);

		__m256 vpptv = _mm256_set1_ps(pixelsPerTriadV);
		__m256 vinvpptv = _mm256_div_ps(vone, vpptv);

		__m256 vy = _mm256_add_ps(_mm256_load_ps(kSublineOffsets), _mm256_broadcast_ss(&dstY));
		__m256 vdy1 = _mm256_sub_ps(_mm256_broadcast_ss(&yc1), vy);
		__m256 vdy2 = _mm256_sub_ps(_mm256_broadcast_ss(&yc2), vy);

		__m256 vdx1a = _mm256_setzero_ps();
		__m256 vdx2a = _mm256_setzero_ps();

		for(int i=0; i<8; ++i) {
			__m256 vdy1a = _mm256_fnmadd_ps(_mm256_round_ps(_mm256_mul_ps(vdy1, vinvpptv), _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC), vpptv, vdy1);
			__m256 vdy2a = _mm256_fnmadd_ps(_mm256_round_ps(_mm256_mul_ps(vdy2, vinvpptv), _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC), vpptv, vdy2);

			__m256 vdx1 = _mm256_sqrt_ps(_mm256_max_ps(_mm256_setzero_ps(), _mm256_fnmadd_ps(vdy1a, vdy1a, vdotRadiusSq)));
			__m256 vdx2 = _mm256_sqrt_ps(_mm256_max_ps(_mm256_setzero_ps(), _mm256_fnmadd_ps(vdy2a, vdy2a, vdotRadiusSq)));

			vdx1a = _mm256_add_ps(vdx1a, vdx1);
			vdx2a = _mm256_add_ps(vdx2a, vdx2);

			// MERGE NOTE: advance all eight subline samples, as the scalar
			// renderer does; repeating the first sample aliases the dot rows.
			const __m256 sublineStep = _mm256_set1_ps(1.0f / 64.0f);
			vdy1 = _mm256_sub_ps(vdy1, sublineStep);
			vdy2 = _mm256_sub_ps(vdy2, sublineStep);
		}

		__m256 vone8 = _mm256_set1_ps(1.0f / 8.0f);
		_mm256_store_ps(&dotSpans[0][0][0], _mm256_fnmsub_ps(vdx1a, vone8, vdotRadius));
		_mm256_store_ps(&dotSpans[0][1][0], _mm256_fmsub_ps(vdx1a, vone8, vdotRadius));
		_mm256_store_ps(&dotSpans[1][0][0], _mm256_fnmsub_ps(vdx2a, vone8, vdotRadius));
		_mm256_store_ps(&dotSpans[1][1][0], _mm256_fmsub_ps(vdx2a, vone8, vdotRadius));
	}

	// render scanlines
	const uint32 channelScale = 0x010101 & channelMask;
	const float encodingScale = 255.0f * (float)channelScale / 256.0f;

	alignas(32) const float triMaskBase[8] {
		 1.0f,
		 3.0f,
		 5.0f,
		 7.0f,
		 9.0f,
		11.0f,
		13.0f,
		15.0f,
	};

	const __m256 vtriMask2 = _mm256_mul_ps(_mm256_load_ps(triMaskBase), _mm256_set1_ps(encodingScale));
	__m256 vtriMask1 = _mm256_permute_ps(vtriMask2, 0b00'01'10'11);
	vtriMask1 = _mm256_permute2f128_ps(vtriMask1, vtriMask1, 0x01);

	// scalar implementation
	float *VDRESTRICT prevScan = prevScan0;

	const __m256 vhalf = _mm256_set1_ps(0.5f);

	__m256 vprevLeftSum = _mm256_setzero_ps();
	const float rightMargin = 1.0f + 2*dotRadius;
	while(w--) {
		__m256 vrightSum = _mm256_mul_ps(vprevLeftSum, vhalf);
		__m256 vleftSum = _mm256_setzero_ps();

		for(int i = 0; i < 2; ++i) {
			__m256 vxbase = dotRightPos[i];

			while(_mm256_cvtss_f32(vxbase) <= 0.0f)
				vxbase = _mm256_add_ps(vxbase, vppth);

			if (_mm256_cvtss_f32(vxbase) < rightMargin) {
				__m256 vxbase2 = vxbase;
				const __m256 vdotSpansX1 = _mm256_load_ps(dotSpans[i][0]);
				const __m256 vdotSpansX2 = _mm256_load_ps(dotSpans[i][1]);

				do {
					// compute scanline intersection with disc
					// clip span to pixel width
					__m256 vx1 = _mm256_max_ps(_mm256_add_ps(vxbase2, vdotSpansX1), _mm256_setzero_ps());
					__m256 vx2 = _mm256_min_ps(_mm256_add_ps(vxbase2, vdotSpansX2), vone);
					vx1 = _mm256_min_ps(vx1, vx2);

					// integrate range over both halves of triangle filter
					// (1-x1)*(1-x1) - (1-x2)*(1-x2) = 2(x2-x1) - (x2^2 - x1^2)
					const __m256 vxs = _mm256_add_ps(vx2, vx1);
					const __m256 vdx = _mm256_sub_ps(vx2, vx1);

					// *2 and -leftSum done at end
					vleftSum = _mm256_fmadd_ps(vdx, vxs, vleftSum);
					vrightSum = _mm256_add_ps(vrightSum, vdx);

					vxbase2 = _mm256_add_ps(vxbase2, vppth);
				} while(_mm256_cvtss_f32(vxbase2) < rightMargin);
			}

			dotRightPos[i] = _mm256_sub_ps(vxbase, vone);
		}

		__m256 vintensity = _mm256_fmsub_ps(vrightSum, _mm256_set1_ps(2.0f), vleftSum);
		vprevLeftSum = vleftSum;

		// apply vertical triangle filter
		__m256 vintensity1 = _mm256_mul_ps(vintensity, vtriMask1);
		__m256 vintensity2 = _mm256_mul_ps(vintensity, vtriMask2);

		// horizontal reduction of both vertical triangle filter halves in parallel
		__m256 vintensity3 = _mm256_add_ps(
			_mm256_permute2f128_ps(vintensity1, vintensity2, 0x20),
			_mm256_permute2f128_ps(vintensity1, vintensity2, 0x31)
		);

		vintensity3 = _mm256_add_ps(vintensity3, _mm256_permute_ps(vintensity3, 0b01'00'11'10));
		vintensity3 = _mm256_add_ps(vintensity3, _mm256_permute_ps(vintensity3, 0b10'11'00'01));

		*tex++ |= channelMask & _mm_cvt_ss2si(
			_mm_add_ss(_mm256_castps256_ps128(vintensity3), _mm_load_ss(prevScan))
		);

		_mm_store_ss(prevScan++, _mm256_extractf128_ps(vintensity3, 1));
	}

	_mm256_zeroupper();
}
#endif

void VDDisplayRenderDotMaskChannel(uint32 *tex, uint32 w, uint32 channelMask,
	float pixelsPerTriadH,
	float pixelsPerTriadV,
	float posX1, float posY1, float posX2, float posY2, float dotRadius, float dstX, float dstY,
	float *prevScan)
{
	// The AVX code is horrifically slow in MSVC -Od due to the compiler mixing
	// VEX and non-VEX instructions, more than 100x compared to optimized builds.
#if !defined(_DEBUG) && (defined(VD_CPU_X86) || defined(VD_CPU_X64))
	// MERGE NOTE: this renderer uses FMA intrinsics as well as AVX2.
	if (VDCheckAllExtensionsEnabled(VDCPUF_SUPPORTS_AVX2 | VDCPUF_SUPPORTS_FMA))
		return VDDisplayRenderDotMaskChannel_AVX2(tex, w, channelMask, pixelsPerTriadH, pixelsPerTriadV, posX1, posY1, posX2, posY2, dotRadius, dstX, dstY, prevScan);
#endif

	VDDisplayRenderDotMaskChannel_Scalar(tex, w, channelMask, pixelsPerTriadH, pixelsPerTriadV, posX1, posY1, posX2, posY2, dotRadius, dstX, dstY, prevScan);
}

void VDDisplayCreateTriadDotMaskTexture(uint32 *tex, ptrdiff_t pitch, uint32 w, uint32 h, float dstX, float dstY, float dstW, float dstH, const VDDisplayTriadDotMaskParams& params) {
	VDFastMathScope fastMathScope;

	const float pptH = params.mPixelsPerTriadH;
	const float pptV = params.mPixelsPerTriadV;

	vdfastvector<float> prevScan(w * 3, 0.0f);

	for(uint32 y = 0; y < h; ++y) {
		memset(tex, 0, sizeof(tex[0]) * w);

		VDDisplayRenderDotMaskChannel(
			tex,
			w,
			0xFF0000,
			pptH,
			pptV,
			params.mRedCenter[0][0],
			params.mRedCenter[0][1],
			params.mRedCenter[1][0],
			params.mRedCenter[1][1],
			params.mRedWidth,
			dstX,
			dstY + (float)y,
			prevScan.data()
		);

		VDDisplayRenderDotMaskChannel(
			tex,
			w,
			0x00FF00,
			pptH,
			pptV,
			params.mGrnCenter[0][0],
			params.mGrnCenter[0][1],
			params.mGrnCenter[1][0],
			params.mGrnCenter[1][1],
			params.mGrnWidth,
			dstX,
			dstY + (float)y,
			prevScan.data() + w
		);

		VDDisplayRenderDotMaskChannel(
			tex,
			w,
			0x0000FF,
			pptH,
			pptV,
			params.mBluCenter[0][0],
			params.mBluCenter[0][1],
			params.mBluCenter[1][0],
			params.mBluCenter[1][1],
			params.mBluWidth,
			dstX,
			dstY + (float)y,
			prevScan.data() + (size_t)w * 2
		);

		tex = (uint32 *)((char *)tex + pitch);
	}
}

////////////////////////////////////////////////////////////////////////////////

void VDDisplayDistortionMapping::Init(float viewAngleX, float viewRatioY, float viewAspect) {
	// The distortion algorithm works as follows:
	//
	//	- The screen is modeled as the front surface of an ellipsoid. At reduced distortion,
	//	  the size of the ellipsoid is reduced in one or both axes so that a smaller angle of
	//	  the ellipsoid is seen. When vertical distortion is disabled, it is infinitely tall
	//	  (a cylinder).
	//
	//	- For rendering, we need a reverse mapping from screen to image. A view ray is
	//	  constructed from the eye through the view plane and intersected against the
	//	  ellipsoid. A second ray representing the electron beam is then constructed and
	//	  reprojected onto the source image. The horizontal/vertical deflection slopes for
	//	  the beam are assumed to be linear with the image position, which makes this a
	//	  second projection. (Evaluation of physical accuracy is left to the reader.)
	//
	//	- For other rendering, such as for overlays, we need a forward mapping from image to
	//	  screen. As it happens, this is basically the same as the reverse mapping: construct
	//	  a projection ray through the image point, intersect it against the ellipsoid, and
	//	  reproject to the screen.
	//
	// The ellipsoid is sized by first starting with a sphere sized appropriately to be
	// inscribed within the dest area, then scaled according to the distortion view
	// angles. The view angle determines the angular amount of the ellipsoid subtended
	// horizontally by the image, and the view ratio Y parameter then sets the aspect
	// ratio of this projection (as a ratio of area, NOT angle).
	//
	// The destination area is assumed to have the same aspect ratio as the source, so the
	// destination aspect ratio is used to adjust the ellipsoid size to compensate for the
	// common aspect ratio.

	// Compute inverse half sizes for an ellipsoid with the right aspect ratio in view space.
	// Inverse radii are more convenient for the math and handle infinite radii, which are
	// necessary for an undistorted axis.

	const float invRadiusX = sinf(viewAngleX * (nsVDMath::kfPi / 180.0f) * 0.5f);
	const float invRadiusY = invRadiusX * viewRatioY / viewAspect;

	// The critical path we need to support is the reverse mapping since it is used in the
	// fragment program to map back from the screen to the image. This is the basic
	// algorithm:
	//
	//	unitSpherePos.xy = (screenPos - 0.5)*2 / radii
	//	unitSpherePos.z = sqrt(1 - dot(unitSpherePos.xy, unitSpherePos.xy))
	//	imagePos = unitSpherePos.xy * radii / unitSpherePos.z * imageScale
	//
	// The reverse mapping can be massaged for the pixel shader:
	//
	//	r = radii
	//	s = imageScale
	//	v = screenPos - 0.5
	//	v2 = v / (r * s)
	//	k = 1 / (2*s)^2 = (1*s^2)/4
	//	imagePos = v * rsqrt(k - dot(v2, v2))
	//
	// With imageScale set to the smaller of the two values that map (0.5,1) and (1,0.5)
	// to (x,1) and (1,y), inscribing the mapped image within the dest rect:
	//
	//	imageScale = 0.5 * sqrt(1 - 1/min(radii.x, radii.y)^2)

	const float maxInvRadius = std::min(invRadiusX, invRadiusY);
	const float invImageScale = 2.0f / sqrtf(1.0 - maxInvRadius*maxInvRadius);

	mScaleX = invRadiusX * invImageScale;
	mScaleY = invRadiusY * invImageScale;
	mSqRadius = invImageScale * invImageScale / 4.0f;
}

bool VDDisplayDistortionMapping::MapImageToScreen(vdfloat2& pt) const {
	using namespace nsVDMath;

	// clamp source point
	vdfloat2 pt2 { std::clamp(pt.x, 0.0f, 1.0f), std::clamp(pt.y, 0.0f, 1.0f) };

	bool valid = (pt2 == pt);

	// Forward projection:
	//
	//	-- warp coordinate system to change ellipsoid to unit sphere
	//	v = (imagePos - 0.5) / (r*s)
	//
	//	-- intersect image ray against the unit sphere by vec normalize
	//	z = sqrt(1 + dot(v.xy, v.xy))
	//	v /= z
	//
	//	-- unwarp the coordinate system and rebias from [-1,1] to [0,1]
	//	v = v * r / 2 - 0.5
	//
	// The slightly tricky part is that either axis of r can be infinite, so we must
	// avoid explicitly multiplying by it with some slight rearrangement. The rest
	// is just using the derived constants instead of r and s directly.

	vdfloat2 v = (pt2 - vdfloat2{0.5f, 0.5f});
	vdfloat2 v2 = v * vdfloat2{mScaleX, mScaleY};
	pt = v * sqrtf(mSqRadius / (1.0f + dot(v2, v2))) + vdfloat2{0.5f, 0.5f};

	return valid;
}

bool VDDisplayDistortionMapping::MapScreenToImage(vdfloat2& pt) const {
	using namespace nsVDMath;

	// convert point to ray cast from center of sphere to point on screen plane
	vdfloat2 v = pt - vdfloat2{0.5f, 0.5f};

	// intersect ray against sphere and reproject to source -- see Init() for
	// full derivation
	vdfloat2 v2 = v * vdfloat2{mScaleX, mScaleY};
	float d = std::max(1e-5f, mSqRadius - dot(v2, v2));

	v /= sqrtf(d);

	// clip the ray at the nearer of the X or Y border intersections
	float dx = fabsf(v.x);
	float dy = fabsf(v.y);
	float dmax = std::max(dx, dy);

	bool valid = true;
	if (dmax > 0.5f) {
		v /= 2.0f * dmax;
		valid = false;
	}

	// convert to source point
	pt = v + vdfloat2{0.5f, 0.5f};

	return valid;
}

