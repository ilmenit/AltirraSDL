//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2026 Avery Lee
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
#include <intrin.h>
#include <vd2/system/binary.h>
#include <vd2/system/color.h>
#include <vd2/system/constexpr.h>
#include <vd2/system/cpuaccel.h>
#include <vd2/system/int128.h>
#include <vd2/system/vecmath.h>
#include "printerrasterizer.h"

#define AT_PRINTERRASTERIZER_USE_AVX2 1

#define AT_PRINTERRASTERIZER_SORT_TRACE(...) ((void)0)
//#define AT_PRINTERRASTERIZER_SORT_TRACE(...) VDDEBUG2(__VA_ARGS__)

ATPrinterRasterizer::ATPrinterRasterizer() {
	for(int i=0; i<256; ++i) {
		uint8 v = i;

		v -= (v >> 1) & 0x55;
		v = (v & 0x33) + ((v & 0xCC) >> 2);

		mPopCnt8[i] = (v & 0x0F) + (v >> 4);
	}

	static constexpr float kRange = kWhiteLevel - kBlackLevel;
	for(int i=0; i<=64; ++i) {
		float luma = kWhiteLevel - kRange * (float)i / 64.0f;
		uint32 v = VDColorRGB(vdfloat32x3::set(luma, luma, luma)).LinearToSRGB().ToRGB8();

		mGammaTable[i] = v;
	}

	// Threshold table for 8x8 dither.
	static constexpr uint8 kDitherPattern[] {
		64,  1, 57, 37, 43, 44, 22, 11,
		60, 30, 15, 62, 31, 54, 27, 52,
		26, 13, 63, 38, 19, 48, 24, 12,
		 6,  3, 56, 28, 14,  7, 58, 29,
		55, 34, 17, 49, 33, 41, 45, 47,
		46, 23, 50, 25, 53, 35, 40, 20,
		10,  5, 59, 36, 18,  9, 61, 39,
		42, 21, 51, 32, 16,  8,  4,  2
	};

	static_assert(vdcountof(kDitherPattern) == 64);

	// Convert the threshold table to an order table, and also transpose the
	// dither matrix if we're using NEON.
	//
	// Scalar and SSE2 use book order, where each byte corresponds to a subrow.
	// NEON is more complex and instead uses the order:
	//
	//  63               48 47               32 31               16 15                0
	// |  7 : 3  |  7 : 3  |  6 : 2  |  6 : 2  |  5 : 1  |  5 : 1  |  4 : 0  |  4 : 0  |   x-position
	// |7531 7531|6420 6420|7531 7531|6420 6420|7531 7531|6420 6420|7531 7531|6420 6420|   y-subrow

	static constexpr auto kDitherOrder = [] {
		VDCxArray<uint8, 64> r;

		for(unsigned i=0; i<64; ++i) {
			unsigned j = kDitherPattern[i];

#ifdef VD_CPU_ARM64
			// ARM64 uses a transposed order compared to x64.
			unsigned x = i & 7;
			unsigned y = i >> 3;
			unsigned bitPos = (y >> 1) + (y & 1 ? 8 : 0) + (x & 3)*16 + (x & 4);

			if (bitPos >= 64)
				throw;

			r[j - 1] = bitPos;
#else
			r[j - 1] = i;
#endif
		}

		return r;
	}();

	memset(mLevelDithers, 0xFF, sizeof mLevelDithers);

	for(int ch=0; ch<3; ++ch) {
		uint32 offset = 7 * ch;
		uint64 dither = 0;

		for(int level = 0; level < 64; ++level) {
			dither |= UINT64_C(1) << kDitherOrder[(level + offset) & 63];

			VDWriteUnalignedLEU64(mLevelDithers[level + 1][ch], ~dither);
		}
	}
}

ATPrinterRasterizer::~ATPrinterRasterizer() {
	while(!mIndexBlocks.empty()) {
		delete mIndexBlocks.back();
		mIndexBlocks.pop_back();
	}
}

void ATPrinterRasterizer::TrimBuffers() {
	vdfastvector<uint32, vdaligned_alloc<uint32, 32>> fb;
	fb.swap(mFrameBuffer);

	vdfastvector<uint8, vdaligned_alloc<uint8, 32>> ab;
	ab.swap(mABuffer);
}

std::optional<VDPixmap> ATPrinterRasterizer::Render(ATPrinterGraphicalOutput *outputOpt, const ViewTransform& viewTransform, sint32 x, sint32 y, uint32 w, uint32 h, bool force) {
	if (!outputOpt || w <= 0 || h <= 0)
		return force ? std::optional(RenderBlank(w, h)) : std::nullopt;

	auto& output = *outputOpt;

	// compute pixel bounding box
	const sint32 viewix1 = x;
	const sint32 viewiy1 = y;
	const sint32 viewix2 = x + w;
	const sint32 viewiy2 = y + h;
	const float viewfx1 = (float)viewix1;
	const float viewfy1 = (float)viewiy1;
	const float viewfx2 = (float)viewix2;
	const float viewfy2 = (float)viewiy2;

	// compute document space bounding box
	const float viewDocX1 = viewTransform.mOriginX + viewfx1 * viewTransform.mMMPerPixel;
	const float viewDocY1 = viewTransform.mOriginY + viewfy1 * viewTransform.mMMPerPixel;
	const float viewDocX2 = viewTransform.mOriginX + viewfx2 * viewTransform.mMMPerPixel;
	const float viewDocY2 = viewTransform.mOriginY + viewfy2 * viewTransform.mMMPerPixel;

	// pre-cull line range to entire vertical range
	const bool hasVectors = output.HasVectors();

	ATPrinterGraphicalOutput::CullInfo cullInfo;

	if (!output.PreCull(cullInfo, vdrect32f(viewDocX1, viewDocY1, viewDocX2, viewDocY2)) && !hasVectors) {
		if (!force)
			return std::nullopt;

		return RenderBlank(w, h);
	}

	const float dotRadiusMM = output.GetGraphicsSpec().mDotRadiusMM;

	// clear framebuffer
	size_t fbWidth = (w + 3) & ~3;

	mFrameBuffer.resize(fbWidth * h);
	std::fill(mFrameBuffer.begin(), mFrameBuffer.end(), 0xFFFFFFFF);

	// initialize antialiasing buffer
	const size_t abufferSize = fbWidth * (hasVectors ? 24 : 8);

	if (mABuffer.size() < abufferSize)
		mABuffer.resize(abufferSize);

	// clear the antialiasing buffer -- this only needs to be done once as the
	// downsampler will re-clear it
	uint8 *abuf = mABuffer.data();
	memset(abuf, 0, abufferSize);

	// render one scan line at a time
	const float viewSubPixelsPerMM = viewTransform.mPixelsPerMM * 8.0f;
	const float rowCenterToFirstSubRowOffset = viewTransform.mMMPerPixel * (-3.5f / 8.0f);
	const float subRowStep = viewTransform.mMMPerPixel / 8.0f;
	const float dotRadius = dotRadiusMM;
	const float dotRadiusSq = dotRadius * dotRadius;
	const sint32 subw = w * 8;

	alignas(16) static constexpr float kDither[8] {
		(0.0f - 3.5f) / 8.0f,
		(4.0f - 3.5f) / 8.0f,
		(2.0f - 3.5f) / 8.0f,
		(6.0f - 3.5f) / 8.0f,
		(1.0f - 3.5f) / 8.0f,
		(5.0f - 3.5f) / 8.0f,
		(3.0f - 3.5f) / 8.0f,
		(7.0f - 3.5f) / 8.0f,
	};

	vdfloat32x4 subRowYOffsets_a = vdfloat32x4::set(kDither[0] + 0.0f, kDither[1] + 1.0f, kDither[2] + 2.0f, kDither[3] + 3.0f) * subRowStep + rowCenterToFirstSubRowOffset;
	vdfloat32x4 subRowYOffsets_b = vdfloat32x4::set(kDither[4] + 4.0f, kDither[5] + 5.0f, kDither[6] + 6.0f, kDither[7] + 7.0f) * subRowStep + rowCenterToFirstSubRowOffset;

	const vdrect32f fullCullRect {
		viewDocX1 - viewTransform.mMMPerPixel * 0.5f,
		viewDocY1,
		viewDocX2 + viewTransform.mMMPerPixel * 0.5f,
		viewDocY2
	};

	mDotCullBuffer.clear();
	mVectorCullBuffer.clear();

	output.ExtractNextLineDots(mDotCullBuffer, cullInfo, fullCullRect);

	vdrect32f fullDotCullRect {
		fullCullRect.left - dotRadiusMM,
		fullCullRect.top - dotRadiusMM,
		fullCullRect.right + dotRadiusMM,
		fullCullRect.bottom + dotRadiusMM
	};

	if (hasVectors) {
		output.ExtractVectors(mVectorCullBuffer, fullDotCullRect);
	}

	SortCullBuffers(viewDocY1, viewTransform.mMMPerPixel, h, dotRadius, fullDotCullRect);

	mVectorExpansionBuffer.resize(mVectorCullBuffer.size());

	auto itDotStart = mDotCullBuffer.begin();
	auto itVectorStart = mVectorCullBuffer.begin();
	auto itVxStart = mVectorExpansionBuffer.begin();
	auto itVxEnd = itVxStart;

	const vdfloat32x4 vsubwf = vdfloat32x4::set1((float)subw);
	vdfloat32x4 subSpansF0[2][2];
	for(int i=0; i<2; ++i) {
		// we need a slight offset here to account for dithering
		subSpansF0[0][i] = vdfloat32x4::set1(-1.0f);
		subSpansF0[1][i] = vsubwf + vdfloat32x4::set1(1.0f);
	}

	const vdfloat32x4 ditherx_a = vdfloat32x4::set(kDither[0], kDither[1], kDither[2], kDither[3]);
	const vdfloat32x4 ditherx_b = vdfloat32x4::set(kDither[4], kDither[5], kDither[6], kDither[7]);

	const float docRowYD = 0.5f * viewTransform.mMMPerPixel;
	const float dotCullDist = dotRadius + docRowYD;

	for(uint32 yoff = 0; yoff < h; ++yoff) {
		const float docRowYC = viewDocY1 + ((float)yoff + 0.5f) * viewTransform.mMMPerPixel;

		// pre-cull lines to scan line vertical range
		const float docRowY1 = docRowYC - docRowYD;
		const float docRowY2 = docRowYC + docRowYD;

		const vdrect32f cullRect { viewDocX1 - viewTransform.mMMPerPixel * 0.5f, docRowY1, viewDocX2 + viewTransform.mMMPerPixel * 0.5f, docRowY2 };

		// render all dots within the scan line
		const RenderBin& rbin = mRenderBins[yoff];
		const float subwf = (float)subw;

		vdspan<const RenderDot> scanLineDots(itDotStart + rbin.mDotStart, rbin.mDotEnd - rbin.mDotStart);

#if defined(VD_CPU_X64) && AT_PRINTERRASTERIZER_USE_AVX2
		if (VDCheckAllExtensionsEnabled(VDCPUF_SUPPORTS_AVX | VDCPUF_SUPPORTS_AVX2 | VDCPUF_SUPPORTS_FMA | VDCPUF_SUPPORTS_POPCNT)) {
			const auto rasterizeDots = [=, this](auto rgb) VD_CPU_TARGET_LAMBDA("avx2,fma") {
				__m256 z = _mm256_setzero_ps();
				__m256 vsubwf = _mm256_set1_ps(subwf);
				__m256 subRowYOffsets = _mm256_setr_m128(subRowYOffsets_a.v, subRowYOffsets_b.v);
				__m256 ditherx = _mm256_setr_m128(ditherx_a.v, ditherx_b.v);

				for(const RenderDot& VDRESTRICT dot : scanLineDots) {
					const float dy = dot.mY - docRowYC;

					if (fabsf(dy) >= dotCullDist)
						continue;

					// process one subrow at a time
					sint32 subSpans[2][8];

					__m256 dy2 = _mm256_sub_ps(_mm256_set1_ps(dy), subRowYOffsets);
					__m256 dx = _mm256_mul_ps(_mm256_sqrt_ps(_mm256_max_ps(_mm256_fnmadd_ps(dy2, dy2, _mm256_set1_ps(dotRadiusSq)), z)), _mm256_set1_ps(viewSubPixelsPerMM));

					// compute x range in document space
					const float xcs = (dot.mX - viewDocX1) * viewSubPixelsPerMM;
					const __m256 xc = _mm256_add_ps(_mm256_set1_ps(xcs), ditherx);
					const __m256 x1 = _mm256_min_ps(_mm256_max_ps(_mm256_sub_ps(xc, dx), z), vsubwf);
					const __m256 x2 = _mm256_min_ps(_mm256_max_ps(_mm256_add_ps(xc, dx), z), vsubwf);

					// convert x range to subpixels
					_mm256_store_si256((__m256i *)&subSpans[0][0], _mm256_cvtps_epi32(_mm256_ceil_ps(x1)));
					_mm256_store_si256((__m256i *)&subSpans[1][0], _mm256_cvtps_epi32(_mm256_ceil_ps(x2)));

#ifdef VD_COMPILER_MSVC
					[[msvc::forceinline_calls]]
#endif
#ifdef VD_COMPILER_CLANG
					[[clang::always_inline]]
#endif
					RenderTrapezoid_AVX2<rgb>(subSpans, dot.mLinearColor);
				}
			};

			if (hasVectors)
				rasterizeDots(std::true_type());
			else
				rasterizeDots(std::false_type());
		} else
#endif

		for(const RenderDot& VDRESTRICT dot : scanLineDots) {
			const float dy = dot.mY - docRowYC;

			if (fabsf(dy) >= dotCullDist)
				continue;

			// process one subrow at a time
			sint32 subSpans[2][8];

			vdfloat32x4 z = vdfloat32x4::zero();
			vdfloat32x4 vsubwf = vdfloat32x4::set1(subwf);
			vdfloat32x4 dy2_a = dy - subRowYOffsets_a;
			vdfloat32x4 dy2_b = dy - subRowYOffsets_b;
			vdfloat32x4 dx_a = sqrt(max(dotRadiusSq - dy2_a*dy2_a, z)) * viewSubPixelsPerMM;
			vdfloat32x4 dx_b = sqrt(max(dotRadiusSq - dy2_b*dy2_b, z)) * viewSubPixelsPerMM;

			// compute x range in document space
			const float xc = (dot.mX - viewDocX1) * viewSubPixelsPerMM;
			const vdfloat32x4 xc_a = xc + ditherx_a;
			const vdfloat32x4 xc_b = xc + ditherx_b;
			const vdfloat32x4 x1_a = min(max(xc_a - dx_a, z), vsubwf);
			const vdfloat32x4 x1_b = min(max(xc_b - dx_b, z), vsubwf);
			const vdfloat32x4 x2_a = min(max(xc_a + dx_a, z), vsubwf);
			const vdfloat32x4 x2_b = min(max(xc_b + dx_b, z), vsubwf);

			// convert x range to subpixels
			const vdint32x4 ix1_a = ceilint(x1_a);
			const vdint32x4 ix1_b = ceilint(x1_b);
			const vdint32x4 ix2_a = ceilint(x2_a);
			const vdint32x4 ix2_b = ceilint(x2_b);

			// horizontally clip
			storeu(&subSpans[0][0], ix1_a);
			storeu(&subSpans[0][4], ix1_b);
			storeu(&subSpans[1][0], ix2_a);
			storeu(&subSpans[1][4], ix2_b);

			RenderTrapezoid(subSpans, dot.mLinearColor, hasVectors);
		}

		// add any new vectors
		for(uint32 i = 0, numNewVecs = rbin.mNumVectors; i < numNewVecs; ++i) {
			const RenderVector& VDRESTRICT v = *itVectorStart;
			++itVectorStart;

			ExpandedVector& VDRESTRICT ev = *itVxEnd;

			// load vector -- note that this is always pre-sorted top-down so y1 <= y2
			const float x1 = v.mX1;
			const float y1 = v.mY1;
			const float x2 = v.mX2;
			const float y2 = v.mY2;

			// compute perpendicular vector (points left)
			const float dx = x2 - x1;
			const float dy = y2 - y1;
			const float l2 = dx*dx + dy*dy;
			if (l2 < 1e-4f)
				continue;

			const float ps = l2 > 1e-6f ? dotRadiusMM / sqrtf(l2) : 0.0f;
			const float xp = -dy * ps;
			const float yp = dx * ps;

			// compute edges in counterclockwise order
			struct Edge {
				float x, y, dx, dy, slope;
			} edges[4];

			edges[0] = Edge { x1 + xp, y1 + yp, dx, dy };		// always down
			edges[2] = Edge { x1 - xp, y1 - yp, -dx, -dy };		// always up

			if (dx < 0) {
				edges[1] = Edge { x2, y2, dy, -dx };		// always down
				edges[3] = Edge { x1, y1, -dy, dx };		// always up
			} else {
				edges[3] = Edge { x2, y2, dy, -dx };		// up or horizontal
				edges[1] = Edge { x1, y1, -dy, dx };		// down or horizontal
			}

			for(Edge& edge : edges) {
				if (fabsf(edge.dy) < 1e-4f) {
					edge.slope = 0;
					edge.x = &edge < &edges[2] ? 0.0f : (float)subw;
				} else {
					edge.slope = edge.dx / edge.dy;
					edge.x -= viewDocX1 + edge.y * edge.slope;
				}

				edge.slope *= viewSubPixelsPerMM;
				edge.x = edge.x * viewSubPixelsPerMM - 0.5f;
			}

			ev.mEdgeX = vdfloat32x4::set(edges[0].x, edges[1].x, edges[2].x, edges[3].x);
			ev.mEdgeSlope = vdfloat32x4::set(edges[0].slope, edges[1].slope, edges[2].slope, edges[3].slope);

			// set culling Y values
			ev.mCullY1 = y1 - fabsf(yp);
			ev.mCullY2 = y2 + fabsf(yp);
			ev.mLinearColor = v.mLinearColor;

			++itVxEnd;
		}

		// render all vectors, removing vectors as they expire
#if defined(VD_CPU_X64) && AT_PRINTERRASTERIZER_USE_AVX2
		if (VDCheckAllExtensionsEnabled(VDCPUF_SUPPORTS_AVX | VDCPUF_SUPPORTS_AVX2 | VDCPUF_SUPPORTS_FMA | VDCPUF_SUPPORTS_POPCNT)) {
			[=, &itVxEnd, this]() VD_CPU_TARGET_LAMBDA("avx2,fma") {
				__m256 vDocRowYC = _mm256_set1_ps(docRowYC);
				__m256 vSubRowYOffsets = _mm256_setr_m128(subRowYOffsets_a.v, subRowYOffsets_b.v);
				const __m256 suby = _mm256_add_ps(vDocRowYC, vSubRowYOffsets);

				__m256 vsubwf256 = _mm256_setr_m128(vsubwf.v, vsubwf.v);
				__m256 subScanSteps = _mm256_mul_ps(_mm256_set1_ps(subRowStep), _mm256_set_ps(7, 6, 5, 4, 3, 2, 1, 0));
				__m256 vDither = _mm256_setr_m128(ditherx_a.v, ditherx_b.v);

				auto itVxEnd2 = itVxEnd;
				for(auto it = itVxStart; it != itVxEnd2; ++it) {
					ExpandedVector& VDRESTRICT ev = *it;

					while(ev.mCullY2 <= cullRect.top) {
						--itVxEnd2;

						if (it == itVxEnd2) [[unlikely]]
							goto avx2_vectors_end;

						ev = *itVxEnd2;
					}

					// init subrow edges
					__m256 subSpansF[2];

					subSpansF[0] = _mm256_loadu_ps((const float *)&subSpansF0[0]);
					subSpansF[1] = _mm256_loadu_ps((const float *)&subSpansF0[1]);

					// apply vertical clipping by forcing max to 0 if either vertical plane fails
					const __m256 vtop = _mm256_set1_ps(ev.mCullY1);
					const __m256 vbot = _mm256_set1_ps(ev.mCullY2);

					subSpansF[1] = _mm256_min_ps(subSpansF[1], _mm256_and_ps(_mm256_and_ps(vsubwf256, _mm256_cmp_ps(suby, vtop, 13 /* ge */)), _mm256_cmp_ps(vbot, suby, 14 /* gt */)));

					// render descending edges on left (0/1) and ascending edges on right (2/3)
					__m128 edgesSubX0 = _mm_fmadd_ps(_mm_broadcastss_ps(_mm256_castps256_ps128(suby)), ev.mEdgeSlope.v, ev.mEdgeX.v);
					__m256 vEdgesSubX0 = _mm256_setr_m128(edgesSubX0, edgesSubX0);

					// process edges 0 and 1 (always down)
					subSpansF[0] = _mm256_max_ps(subSpansF[0], _mm256_fmadd_ps(_mm256_broadcast_ss((const float *)&ev.mEdgeSlope + 0), subScanSteps, _mm256_permute_ps(vEdgesSubX0, 0x00)));
					subSpansF[0] = _mm256_max_ps(subSpansF[0], _mm256_fmadd_ps(_mm256_broadcast_ss((const float *)&ev.mEdgeSlope + 1), subScanSteps, _mm256_permute_ps(vEdgesSubX0, 0x55)));

					// process edges 2 and 3 (always up)
					subSpansF[1] = _mm256_min_ps(subSpansF[1], _mm256_fmadd_ps(_mm256_broadcast_ss((const float *)&ev.mEdgeSlope + 2), subScanSteps, _mm256_permute_ps(vEdgesSubX0, 0xAA)));
					subSpansF[1] = _mm256_min_ps(subSpansF[1], _mm256_fmadd_ps(_mm256_broadcast_ss((const float *)&ev.mEdgeSlope + 3), subScanSteps, _mm256_permute_ps(vEdgesSubX0, 0xFF)));

					// convert subspan edges to integer subpixel coordinates
					alignas(32) sint32 subSpans[2][8];
					_mm256_storeu_si256((__m256i *)&subSpans[0][0], _mm256_cvtps_epi32(_mm256_ceil_ps(_mm256_min_ps(_mm256_max_ps(_mm256_add_ps(subSpansF[0], vDither), _mm256_setzero_ps()), vsubwf256))));
					_mm256_storeu_si256((__m256i *)&subSpans[1][0], _mm256_cvtps_epi32(_mm256_ceil_ps(_mm256_min_ps(_mm256_max_ps(_mm256_add_ps(subSpansF[1], vDither), _mm256_setzero_ps()), vsubwf256))));

#ifdef VD_COMPILER_MSVC
					[[msvc::forceinline_calls]]
#endif
#ifdef VD_COMPILER_CLANG
					[[clang::always_inline]]
#endif
					RenderTrapezoid_AVX2<true>(subSpans, ev.mLinearColor);
				}

avx2_vectors_end:
				itVxEnd = itVxEnd2;
			}();
		} else
#endif
		{
			const vdfloat32x4 subScanSteps_a = subRowStep * vdfloat32x4::set(0, 1, 2, 3);
			const vdfloat32x4 subScanSteps_b = subRowStep * vdfloat32x4::set(4, 5, 6, 7);
			const vdfloat32x4 suby_a = docRowYC + subRowYOffsets_a;
			const vdfloat32x4 suby_b = docRowYC + subRowYOffsets_b;

			for(auto it = itVxStart; it != itVxEnd; ++it) {
				ExpandedVector& VDRESTRICT ev = *it;

				while(ev.mCullY2 <= cullRect.top) {
					--itVxEnd;

					if (it == itVxEnd) [[unlikely]]
						goto vectors_done;

					ev = *itVxEnd;
				}

				// init subrow edges
				vdfloat32x4 subSpansF[2][2];

				subSpansF[0][0] = subSpansF0[0][0];
				subSpansF[0][1] = subSpansF0[0][1];
				subSpansF[1][0] = subSpansF0[1][0];
				subSpansF[1][1] = subSpansF0[1][1];

				// apply vertical clipping by forcing max to 0 if either vertical plane fails
				const vdfloat32x4 vtop = vdfloat32x4::set1(ev.mCullY1);
				const vdfloat32x4 vbot = vdfloat32x4::set1(ev.mCullY2);

				subSpansF[1][0] = min(subSpansF[1][0], nonzeromask(vsubwf, cmpge(suby_a, vtop) & cmpgt(vbot, suby_a)));
				subSpansF[1][1] = min(subSpansF[1][1], nonzeromask(vsubwf, cmpge(suby_b, vtop) & cmpgt(vbot, suby_b)));

				// render descending edges on left (0/1) and ascending edges on right (2/3)
				vdfloat32x4 edgesSubX0 = ev.mEdgeX + suby_a.x() * ev.mEdgeSlope;
				vdfloat32x4 edgesSubXInc = ev.mEdgeSlope;

				// process edges 0 and 1 (always down)
				subSpansF[0][0] = max(subSpansF[0][0], edgesSubX0.x() + edgesSubXInc.x()*subScanSteps_a);
				subSpansF[0][1] = max(subSpansF[0][1], edgesSubX0.x() + edgesSubXInc.x()*subScanSteps_b);
				subSpansF[0][0] = max(subSpansF[0][0], edgesSubX0.y() + edgesSubXInc.y()*subScanSteps_a);
				subSpansF[0][1] = max(subSpansF[0][1], edgesSubX0.y() + edgesSubXInc.y()*subScanSteps_b);

				// process edges 2 and 3 (always up)
				subSpansF[1][0] = min(subSpansF[1][0], edgesSubX0.z() + edgesSubXInc.z()*subScanSteps_a);
				subSpansF[1][1] = min(subSpansF[1][1], edgesSubX0.z() + edgesSubXInc.z()*subScanSteps_b);
				subSpansF[1][0] = min(subSpansF[1][0], edgesSubX0.w() + edgesSubXInc.w()*subScanSteps_a);
				subSpansF[1][1] = min(subSpansF[1][1], edgesSubX0.w() + edgesSubXInc.w()*subScanSteps_b);

				// convert subspan edges to integer subpixel coordinates
				sint32 subSpans[2][8];
				storeu(&subSpans[0][0], ceilint(min(max(subSpansF[0][0] + ditherx_a, vdfloat32x4::zero()), vsubwf)));
				storeu(&subSpans[0][4], ceilint(min(max(subSpansF[0][1] + ditherx_b, vdfloat32x4::zero()), vsubwf)));
				storeu(&subSpans[1][0], ceilint(min(max(subSpansF[1][0] + ditherx_a, vdfloat32x4::zero()), vsubwf)));
				storeu(&subSpans[1][4], ceilint(min(max(subSpansF[1][1] + ditherx_b, vdfloat32x4::zero()), vsubwf)));

				RenderTrapezoid(subSpans, ev.mLinearColor, true);
			}
vectors_done:
			;
		}

		// render antialiasing buffer row to framebuffer
		Downsample8x8(&mFrameBuffer[fbWidth * (h - 1 - yoff)], abuf, w, hasVectors);
	}

	VDPixmap px {};
	px.w = w;
	px.h = h;
	px.data = &mFrameBuffer[fbWidth * (h - 1)];
	px.pitch = -(ptrdiff_t)fbWidth*4;
	px.format = nsVDPixmap::kPixFormat_XRGB8888;
	return px;
}

void ATPrinterRasterizer::SortCullBuffers(float minY, float scanLineHeightY, sint32 numScanLines, float dotRadius, const vdrect32f& fullDotCullRect) {
	const float invBinHeight = scanLineHeightY > 0 ? 1.0f / scanLineHeightY : 0.0f;
	size_t nextIndexBlock = 0;

	mSortBins.clear();
	mSortBins.resize(numScanLines);
	mRenderBins.resize(numScanLines);

	vdspan<SortBin> bins(mSortBins);

	// bin sort all vectors by Y1 minus a dot radius
	const float binBaseY = minY + dotRadius;
	const size_t maxBin = numScanLines - 1;
	size_t i = 0;
	for(const RenderVector& v : mVectorCullBuffer) {
		const size_t binIndex = std::min<size_t>((sint32)std::max<float>((v.mY1 - binBaseY) * invBinHeight, 0.0f), maxBin);
		SortBin& bin = bins[binIndex];

		if (!bin.mIndicesLeftInBlock) [[unlikely]] {
			if (nextIndexBlock >= mIndexBlocks.size()) [[unlikely]] {
				vdautoptr newBlock { new IndexBlock };

				mIndexBlocks.emplace_back(newBlock);
				newBlock.release();
			}

			IndexBlock *newBlock = mIndexBlocks[nextIndexBlock++];

			newBlock->mpPrev = bin.mpCurrentIndexBlock;
			bin.mpCurrentIndexBlock = newBlock;

			++bin.mTotalBlocks;
			bin.mIndicesLeftInBlock = IndexBlock::kBlockSize;
		}

		--bin.mIndicesLeftInBlock;
		bin.mpCurrentIndexBlock->mIndices[bin.mIndicesLeftInBlock] = i;

		++i;
	}

	// reorder cull buffer and sort each bin by Y1
	const auto vectorSortPred =
		[](const RenderVector& a, const RenderVector& b) {
			return a.mY1 < b.mY1;
		};

	const size_t numVectors = mVectorCullBuffer.size();
	mVectorCullBuffer2.resize(numVectors);

	i = 0;

	RenderBin *rbin = mRenderBins.data();
	for(const SortBin& bin : bins) {
		size_t binSize = bin.mTotalBlocks * IndexBlock::kBlockSize - bin.mIndicesLeftInBlock;
		size_t binStart = i;

		if (numVectors - i < binSize)
			VDRaiseInternalFailure();

		rbin->mNumVectors = binSize;
		++rbin;

		i += binSize;
		size_t binDst = i;

		size_t blockOffset = bin.mIndicesLeftInBlock;
		for(const IndexBlock *ib = bin.mpCurrentIndexBlock; ib; ib = ib->mpPrev) {
			for(size_t j = blockOffset; j < IndexBlock::kBlockSize; ++j)
				mVectorCullBuffer2[--binDst] = mVectorCullBuffer[ib->mIndices[j]];

			blockOffset = 0;
		}

		if (binDst != binStart)
			VDRaiseInternalFailure();
	}

	if (i != numVectors)
		VDRaiseInternalFailure();

	mVectorCullBuffer2.swap(mVectorCullBuffer);

	// process dots
	i = mDotCullBuffer.size();
	mDotCullBuffer.resize(i + mVectorCullBuffer.size() * 2);

	for (const RenderVector& v : mVectorCullBuffer) {
		if (fullDotCullRect.contains(vdpoint32f { v.mX1, v.mY1 }))
			mDotCullBuffer[i++] = RenderDot(v.mX1, v.mY1, v.mLinearColor);

		if (fullDotCullRect.contains(vdpoint32f { v.mX2, v.mY2 }))
			mDotCullBuffer[i++] = RenderDot(v.mX2, v.mY2, v.mLinearColor);
	}

	mDotCullBuffer.resize(i);

	// bin sort dots by Y
	for(SortBin& bin : bins)
		bin = {};

	nextIndexBlock = 0;
	i = 0;

	for(const RenderDot& dot : mDotCullBuffer) {
		const size_t binIndex = std::min<size_t>((sint32)std::max<float>((dot.mY - binBaseY) * invBinHeight, 0.0f), maxBin);
		SortBin& bin = bins[binIndex];

		if (!bin.mIndicesLeftInBlock) [[unlikely]] {
			if (nextIndexBlock >= mIndexBlocks.size()) [[unlikely]] {
				vdautoptr newBlock { new IndexBlock };

				mIndexBlocks.emplace_back(newBlock);
				newBlock.release();
			}

			IndexBlock *newBlock = mIndexBlocks[nextIndexBlock++];

			newBlock->mpPrev = bin.mpCurrentIndexBlock;
			bin.mpCurrentIndexBlock = newBlock;

			++bin.mTotalBlocks;
			bin.mIndicesLeftInBlock = IndexBlock::kBlockSize;
		}

		--bin.mIndicesLeftInBlock;
		bin.mpCurrentIndexBlock->mIndices[bin.mIndicesLeftInBlock] = i;

		++i;
	}

	const size_t numDots = mDotCullBuffer.size();
	mDotCullBuffer2.resize(numDots);

	i = 0;
	rbin = mRenderBins.data();

	// Dots intersect a scanline for centers within a vertical distance of +/-R
	// from either the top or bottom of the scanline. This means that for each
	// scanline, we are interested in a vertical range of 2R+H. Bins are shifted down
	// vertically so that a bin ends at a height where dot centers positioned
	// at the end of the bin are tangent to the bottom of the scanline.
	ptrdiff_t dotRadiusBins = VDCeilToInt32(2.0f * dotRadius / scanLineHeightY) + 1;

	ptrdiff_t binIndex = 0;
	for(const SortBin& bin : bins) {
		size_t binSize = bin.mTotalBlocks * IndexBlock::kBlockSize - bin.mIndicesLeftInBlock;
		size_t binStart = i;

		if (numDots - i < binSize)
			VDRaiseInternalFailure();

		i += binSize;

		const size_t binEnd = i;
		size_t binDst = i;

		size_t blockOffset = bin.mIndicesLeftInBlock;
		for(const IndexBlock *ib = bin.mpCurrentIndexBlock; ib; ib = ib->mpPrev) {
			for(size_t j = blockOffset; j < IndexBlock::kBlockSize; ++j)
				mDotCullBuffer2[--binDst] = mDotCullBuffer[ib->mIndices[j]];

			blockOffset = 0;
		}

		if (binDst != binStart)
			VDRaiseInternalFailure();

		auto dotStart = mDotCullBuffer2.begin() + binStart;
		auto dotEnd = mDotCullBuffer2.begin() + binEnd;

		// If we have at least two dots, deduplicate them.
		//
		// Why do we want to do this? Because each vector is capped by two dots and any two
		// vectors that share a common endpoint will have redundant dots. This is even more
		// important if Bresenham stepping is enabled, which produces a very high number of
		// tiny, connected vectors. Therefore, we can save quite a bit of rasterization
		// effort by removing the duplicate dots.

		const size_t numScanDots = dotEnd - dotStart;
		if (numScanDots > 1) {
			const size_t htSize = VDComputePrimeBucketCount(numScanDots * 4);
			mDotHash.clear();
			mDotHash.resize(htSize);
			memset(mDotHash.data(), 0, htSize * sizeof(mDotHash[0]));

			// Compute constants for fast remainder using the algorithm from:
			//
			//	D. Lemire, O. Kaser, and N. Kurz, Faster Remainder by Direct Computation, 2018.
			//
			// This computes a u32 % u32 remainder using a 64x64>64 multiply followed
			// by the high half of a 64x64>128 multiply, as ((v*c mod 2^64)*d) / 2^64.
			// We need a u64 % u32 remainder, so instead of computing (v*c), we compute
			// (v mod 2^32)*c + (v/2^32)*(c*(2^32 mod d)). This probably slightly violates
			// the requirements for a fully accurate remainder, but we don't need full
			// accuracy -- the only hard requirement is for the hash table index to be
			// deterministic from the source values.

			const uint32 divisor = (uint32)htSize;
			const uint64 reciprocal = UINT64_C(0xFFFF'FFFF'FFFF'FFFF) / divisor + 1;
			const uint64 reciprocalHi = (uint32)(UINT64_C(0x1'0000'0000) % divisor) * reciprocal;

			const float scanBaseY = binBaseY + scanLineHeightY * (float)binIndex;
			const float scanMicroYScale = 256.0f * invBinHeight;

			auto dst = dotStart;
			auto src = dotStart;

			DotHashNode *const dotHash = mDotHash.data();
			DotHashNode *const dotHashEnd = mDotHash.data() + mDotHash.size();

			do {
				const RenderDot& VDRESTRICT dot = *src;

				// Pack the 12-byte dot into an 8-byte representation. This shrinks the hash table
				// and makes it more efficient. The X coordinate and color are simply used as is,
				// with a slight shift on the X to ensure that a value of 0 is invalid and can
				// be used for empty cells (a bit pattern of FFFFFFFF would be a NaN). This means
				// that X=0 and X=-0 are distinct, but that is generally not possible since it'd
				// be on the left edge of the paper.
				//
				// The Y coordinate, however, has a narrow range since we know that all dots have
				// been prefiltered to be within the vertical bounds of the scanline. Therefore, we
				// use a Y resolution of 1/256 scanline, which is way more than enough resolution to
				// guarantee that any matching dots are close enough since the rasterizer uses 8
				// sub-scans. This allows the micro relative Y to be packed into the unused alpha
				// byte of the color.

				DotHashNode dhn {
					std::bit_cast<uint32>(dot.mX) + 1,
					((uint32)(sint32)((dot.mY - scanBaseY) * scanMicroYScale) << 24) + dot.mLinearColor
				};

				const size_t htidx = VDUMul64x32To96Hi32(reciprocal * dhn.mX + reciprocalHi * dhn.mColorAndMicroY, divisor);
				DotHashNode *VDRESTRICT p = &dotHash[htidx];
				const uint64 ref = VDReadUnalignedU64(&dhn);

				for(;;) {
					// find/add to hash table with linear probing
					do {
						uint64 v = VDReadUnalignedU64(p);

						// check if we found an empty spot, and therefore don't have
						// this dot; this will always happen since the hash table
						// is larger than the source array
						if (!v)
							goto keep_dot;

						// check if we already have this dot
						if (v == ref)
							goto skip_dot;
					} while(++p != dotHashEnd);

					p = dotHash;
				}
keep_dot:
				VDWriteUnalignedU64(p, ref);
				*dst++ = dot;
skip_dot:
				;
			} while(++src != dotEnd);

			i = binStart + (dst - dotStart);
		}

		rbin->mDotStart = binIndex >= dotRadiusBins ? rbin[-dotRadiusBins].mDotEnd : 0;
		rbin->mDotEnd = i;
		++rbin;
		++binIndex;
	}

	if (i > numDots)
		VDRaiseInternalFailure();

	mDotCullBuffer2.swap(mDotCullBuffer);

	AT_PRINTERRASTERIZER_SORT_TRACE("%u/%d dots removed\n", (unsigned)(mDotCullBuffer2.size() - i), (unsigned)mDotCullBuffer2.size());
}

std::optional<VDPixmap> ATPrinterRasterizer::RenderBlank(uint32 w, uint32 h) {
	// clear framebuffer
	mFrameBuffer.resize(w * h);
	std::fill(mFrameBuffer.begin(), mFrameBuffer.end(), mGammaTable[0]);

	VDPixmap px {};
	px.w = w;
	px.h = h;
	px.data = &mFrameBuffer[w * (h - 1)];
	px.pitch = -(ptrdiff_t)w*4;
	px.format = nsVDPixmap::kPixFormat_XRGB8888;
	return px;
}

void ATPrinterRasterizer::RenderTrapezoid(const sint32 subSpans[2][8], uint32 linearColor, bool rgb) {
	uint8 *VDRESTRICT abuf = mABuffer.data();

	if (rgb) {
#if defined(VD_CPU_X64) && AT_PRINTERRASTERIZER_USE_AVX2
		if (VDCheckAllExtensionsEnabled(VDCPUF_SUPPORTS_AVX | VDCPUF_SUPPORTS_AVX2 | VDCPUF_SUPPORTS_FMA | VDCPUF_SUPPORTS_POPCNT))
			return RenderTrapezoid_AVX2<true>(subSpans, linearColor);
#endif

#if defined(VD_CPU_X64) || defined(VD_CPU_X86)
		if (SSE2_enabled)
			return RenderTrapezoid_SSE2<true>(subSpans, linearColor);

		return RenderTrapezoidRGB_Scalar(subSpans, linearColor);
#elif defined(VD_CPU_ARM64)
		return RenderTrapezoid_NEON<true>(subSpans, linearColor);
#endif

	} else {
#if defined(VD_CPU_X64) && AT_PRINTERRASTERIZER_USE_AVX2
		if (VDCheckAllExtensionsEnabled(VDCPUF_SUPPORTS_AVX | VDCPUF_SUPPORTS_AVX2 | VDCPUF_SUPPORTS_FMA | VDCPUF_SUPPORTS_POPCNT))
			return RenderTrapezoid_AVX2<false>(subSpans, linearColor);
#endif

#if defined(VD_CPU_X64) || defined(VD_CPU_X86)
		if (SSE2_enabled)
			return RenderTrapezoid_SSE2<false>(subSpans, linearColor);
#elif defined(VD_CPU_ARM64)
		return RenderTrapezoid_NEON<false>(subSpans, linearColor);
#endif

		for(uint32 subRow = 0; subRow < 8; ++subRow) {
			const sint32 csubx1 = subSpans[0][subRow];
			const sint32 csubx2 = subSpans[1][subRow];

			if (csubx1 >= csubx2)
				continue;

			// draw bits
			const uint32 ucsubx1 = (uint32)csubx1;
			const uint32 ucsubx2 = (uint32)csubx2;
			const uint32 maskx1 = ucsubx1 >> 3;
			const uint32 maskx2 = (ucsubx2 - 1) >> 3;
			const uint8 mask1 = 0xFF >> (ucsubx1 & 7);
			const uint8 mask2 = 0xFF << ((8 - ucsubx2) & 7);
			uint8 *__restrict dst = abuf + subRow + maskx1*8;

			if (maskx1 == maskx2) {
				*dst |= mask1 & mask2;
			} else {
				*dst |= mask1;
				dst += 8;

				for(uint32 x = maskx1 + 1; x < maskx2; ++x) {
					*dst = 0xFF;
					dst += 8;
				}

				*dst |= mask2;
			}
		}
	}
}

void ATPrinterRasterizer::RenderTrapezoidRGB_Scalar(const sint32 subSpans[2][8], uint32 linearColor) {
	uint8 *VDRESTRICT abuf = mABuffer.data();
	const uint8 (&VDRESTRICT redDither)[8] = mLevelDithers[(linearColor >> 16)&0xFF][0];
	const uint8 (&VDRESTRICT grnDither)[8] = mLevelDithers[(linearColor >>  8)&0xFF][1];
	const uint8 (&VDRESTRICT bluDither)[8] = mLevelDithers[(linearColor >>  0)&0xFF][2];

	for(uint32 subRow = 0; subRow < 8; ++subRow) {
		const sint32 csubx1 = subSpans[0][subRow];
		const sint32 csubx2 = subSpans[1][subRow];

		if (csubx1 >= csubx2)
			continue;

		const uint8 rdither = redDither[subRow];
		const uint8 gdither = grnDither[subRow];
		const uint8 bdither = bluDither[subRow];

		// draw bits
		const uint32 ucsubx1 = (uint32)csubx1;
		const uint32 ucsubx2 = (uint32)csubx2;
		const uint32 maskx1 = ucsubx1 >> 3;
		const uint32 maskx2 = (ucsubx2 - 1) >> 3;
		const uint8 mask1 = 0xFF >> (ucsubx1 & 7);
		const uint8 mask2 = 0xFF << ((8 - ucsubx2) & 7);
		uint8 *VDRESTRICT dst = abuf + subRow + maskx1*24;

		if (maskx1 == maskx2) {
			const uint8 mask = mask1 & mask2;

			dst[0] |= rdither & mask;
			dst[8] |= gdither & mask;
			dst[16] |= bdither & mask;
		} else {
			dst[0] |= rdither & mask1;
			dst[8] |= gdither & mask1;
			dst[16] |= bdither & mask1;
			dst += 24;

			for(uint32 x = maskx1 + 1; x < maskx2; ++x) {
				dst[0] |= rdither;
				dst[8] |= gdither;
				dst[16] |= bdither;
				dst += 24;
			}

			dst[0] |= rdither & mask2;
			dst[8] |= gdither & mask2;
			dst[16] |= bdither & mask2;
		}
	}
}

#if defined(VD_CPU_X64) || defined(VD_CPU_X86)
template<bool T_RGB>
void ATPrinterRasterizer::RenderTrapezoid_SSE2(const sint32 subSpans[2][8], uint32 linearColor) {
#if defined(VD_CPU_X64)
	[[maybe_unused]] uint64 rdither;
	[[maybe_unused]] uint64 gdither;
	[[maybe_unused]] uint64 bdither;

	if constexpr (T_RGB) {
		rdither = VDReadUnalignedU64(mLevelDithers[(linearColor >> 16)&0xFF][0]);
		gdither = VDReadUnalignedU64(mLevelDithers[(linearColor >>  8)&0xFF][1]);
		bdither = VDReadUnalignedU64(mLevelDithers[(linearColor >>  0)&0xFF][2]);
	}
#else
	[[maybe_unused]] uint32 rdither1;
	[[maybe_unused]] uint32 rdither2;
	[[maybe_unused]] uint32 gdither1;
	[[maybe_unused]] uint32 gdither2;
	[[maybe_unused]] uint32 bdither1;
	[[maybe_unused]] uint32 bdither2;

	if constexpr (T_RGB) {
		rdither1 = VDReadUnalignedU32(&mLevelDithers[(linearColor >> 16)&0xFF][0][0]);
		rdither2 = VDReadUnalignedU32(&mLevelDithers[(linearColor >> 16)&0xFF][0][4]);
		gdither1 = VDReadUnalignedU32(&mLevelDithers[(linearColor >>  8)&0xFF][1][0]);
		gdither2 = VDReadUnalignedU32(&mLevelDithers[(linearColor >>  8)&0xFF][1][4]);
		bdither1 = VDReadUnalignedU32(&mLevelDithers[(linearColor >>  0)&0xFF][2][0]);
		bdither2 = VDReadUnalignedU32(&mLevelDithers[(linearColor >>  0)&0xFF][2][4]);
	}
#endif

	// compute min/max of non-empty scans
	sint32 minSubX1 = 0x7FFFFFFF;
	sint32 maxSubX2 = 0;

	for(int i=0; i<8; ++i) {
		sint32 subx1 = subSpans[0][i];
		sint32 subx2 = subSpans[1][i];

		if (subx1 < subx2) {
			if (minSubX1 > subx1)
				minSubX1 = subx1;

			if (maxSubX2 < subx2)
				maxSubX2 = subx2;
		}
	}

	if (minSubX1 >= maxSubX2)
		return;

	// expand to byte boundaries
	minSubX1 &= ~(sint32)7;
	maxSubX2 = (maxSubX2 + 7) & ~(sint32)7;

	// load subscan ranges
	__m128i subX1A = _mm_loadu_si128((const __m128i *)&subSpans[0][0]);
	__m128i subX1B = _mm_loadu_si128((const __m128i *)&subSpans[0][4]);
	__m128i subX2A = _mm_loadu_si128((const __m128i *)&subSpans[1][0]);
	__m128i subX2B = _mm_loadu_si128((const __m128i *)&subSpans[1][4]);

	// rasterize blocks
	uint8 *__restrict dst = mABuffer.data();

	if constexpr (T_RGB) {
		dst += (ptrdiff_t)minSubX1*3;
	} else {
		dst += (ptrdiff_t)minSubX1;
	}

	for(sint32 blockStart = minSubX1; blockStart < maxSubX2; blockStart += 192) {
		// compute block-relative bounds
		__m128i blockPos = _mm_set1_epi32(blockStart);
		__m128i blockSubX1A = _mm_sub_epi32(subX1A, blockPos);
		__m128i blockSubX1B = _mm_sub_epi32(subX1B, blockPos);
		__m128i blockSubX2A = _mm_sub_epi32(subX2A, blockPos);
		__m128i blockSubX2B = _mm_sub_epi32(subX2B, blockPos);

		// compress to unsigned bytes with saturation
		__m128i blockSubX1 = _mm_packs_epi32(blockSubX1A, blockSubX1B);
		__m128i blockSubX2 = _mm_packs_epi32(blockSubX2A, blockSubX2B);
		__m128i blockSubX1X2b = _mm_packus_epi16(blockSubX1, blockSubX2);

		// convert to base and width
		__m128i blockSubX1DXb = _mm_subs_epu8(blockSubX1X2b, _mm_slli_si128(blockSubX1X2b, 8));

		// double up bytes and rebias for signed math
		__m128i blockSubX1bb = _mm_unpacklo_epi8(blockSubX1DXb, blockSubX1DXb);
		__m128i blockSubDXbb = _mm_xor_si128(_mm_unpackhi_epi8(blockSubX1DXb, blockSubX1DXb), _mm_set1_epi8(-0x80));

		// set up 64 ranges
		__m128i blockSubX1bbl = _mm_unpacklo_epi16(blockSubX1bb, blockSubX1bb);
		__m128i blockSubX1bbh = _mm_unpackhi_epi16(blockSubX1bb, blockSubX1bb);
		__m128i blockSubDXbbl = _mm_unpacklo_epi16(blockSubDXbb, blockSubDXbb);
		__m128i blockSubDXbbh = _mm_unpackhi_epi16(blockSubDXbb, blockSubDXbb);

		__m128i row01_x1 = _mm_shuffle_epi32(blockSubX1bbl, 0b0'01010000);
		__m128i row01_dx = _mm_shuffle_epi32(blockSubDXbbl, 0b0'01010000);
		__m128i row23_x1 = _mm_shuffle_epi32(blockSubX1bbl, 0b0'11111010);
		__m128i row23_dx = _mm_shuffle_epi32(blockSubDXbbl, 0b0'11111010);
		__m128i row45_x1 = _mm_shuffle_epi32(blockSubX1bbh, 0b0'01010000);
		__m128i row45_dx = _mm_shuffle_epi32(blockSubDXbbh, 0b0'01010000);
		__m128i row67_x1 = _mm_shuffle_epi32(blockSubX1bbh, 0b0'11111010);
		__m128i row67_dx = _mm_shuffle_epi32(blockSubDXbbh, 0b0'11111010);

		// set up bit position counter
		__m128i pos = _mm_set_epi8(-128, -127, -126, -125, -124, -123, -122, -121, -128, -127, -126, -125, -124, -123, -122, -121);
		__m128i posinc = _mm_set1_epi8(8);

		size_t byteCnt = std::min<size_t>(maxSubX2 - blockStart, 192) >> 3;
		while(byteCnt--) {
			// compute (pos >= x1 && pos < x1+dx) as (dx-128 > (pos - x1)-128) to bit mask
#if defined(VD_CPU_X64)
			const uint64 mask
				=  (uint64)_mm_movemask_epi8(_mm_cmpgt_epi8(row01_dx, _mm_sub_epi8(pos, row01_x1)))
				+ ((uint64)_mm_movemask_epi8(_mm_cmpgt_epi8(row23_dx, _mm_sub_epi8(pos, row23_x1))) << 16)
				+ ((uint64)_mm_movemask_epi8(_mm_cmpgt_epi8(row45_dx, _mm_sub_epi8(pos, row45_x1))) << 32)
				+ ((uint64)_mm_movemask_epi8(_mm_cmpgt_epi8(row67_dx, _mm_sub_epi8(pos, row67_x1))) << 48);

			if constexpr (T_RGB) {
				VDWriteUnalignedU64(dst +  0, VDReadUnalignedU64(dst +  0) | (mask & rdither));
				VDWriteUnalignedU64(dst +  8, VDReadUnalignedU64(dst +  8) | (mask & gdither));
				VDWriteUnalignedU64(dst + 16, VDReadUnalignedU64(dst + 16) | (mask & bdither));
			} else {
				VDWriteUnalignedU64(dst +  0, VDReadUnalignedU64(dst +  0) | mask);
			}
#else
			const uint32 mask1
				=  (uint32)_mm_movemask_epi8(_mm_cmpgt_epi8(row01_dx, _mm_sub_epi8(pos, row01_x1)))
				+ ((uint32)_mm_movemask_epi8(_mm_cmpgt_epi8(row23_dx, _mm_sub_epi8(pos, row23_x1))) << 16);
			const uint32 mask2
				=  (uint32)_mm_movemask_epi8(_mm_cmpgt_epi8(row45_dx, _mm_sub_epi8(pos, row45_x1)))
				+ ((uint32)_mm_movemask_epi8(_mm_cmpgt_epi8(row67_dx, _mm_sub_epi8(pos, row67_x1))) << 16);

			if constexpr (T_RGB) {
				VDWriteUnalignedU32(dst +  0, VDReadUnalignedU32(dst +  0) | (mask1 & rdither1));
				VDWriteUnalignedU32(dst +  4, VDReadUnalignedU32(dst +  4) | (mask2 & rdither2));
				VDWriteUnalignedU32(dst +  8, VDReadUnalignedU32(dst +  8) | (mask1 & gdither1));
				VDWriteUnalignedU32(dst + 12, VDReadUnalignedU32(dst + 12) | (mask2 & gdither2));
				VDWriteUnalignedU32(dst + 16, VDReadUnalignedU32(dst + 16) | (mask1 & bdither1));
				VDWriteUnalignedU32(dst + 20, VDReadUnalignedU32(dst + 20) | (mask2 & bdither2));
			} else {
				VDWriteUnalignedU32(dst +  0, VDReadUnalignedU32(dst +  0) | mask1);
				VDWriteUnalignedU32(dst +  4, VDReadUnalignedU32(dst +  4) | mask2);
			}
#endif

			pos = _mm_adds_epi8(pos, posinc);

			if constexpr (T_RGB) {
				dst += 24;
			} else {
				dst += 8;
			}
		}
	}
}
#endif

#if defined(VD_CPU_X64)
template<bool T_RGB>
VD_CPU_TARGET("avx2,fma")
void ATPrinterRasterizer::RenderTrapezoid_AVX2(const sint32 subSpans[2][8], uint32 linearColor) {
	[[maybe_unused]] __m256i rdither;
	[[maybe_unused]] __m256i gdither;
	[[maybe_unused]] __m256i bdither;

	if constexpr (T_RGB) {
		rdither = _mm256_broadcastq_epi64(_mm_loadl_epi64((const __m128i *)&mLevelDithers[(linearColor >> 16)&0xFF][0]));
		gdither = _mm256_broadcastq_epi64(_mm_loadl_epi64((const __m128i *)&mLevelDithers[(linearColor >>  8)&0xFF][1]));
		bdither = _mm256_broadcastq_epi64(_mm_loadl_epi64((const __m128i *)&mLevelDithers[(linearColor >>  0)&0xFF][2]));
	}

	// load subscan ranges
	__m256i subX1 = _mm256_loadu_si256((const __m256i *)&subSpans[0][0]);
	__m256i subX2 = _mm256_loadu_si256((const __m256i *)&subSpans[1][0]);

	// compute mask of non-empty ranges
	__m256i validMask = _mm256_cmpgt_epi32(subX2, subX1);

	// convert to dword ranges
	__m256i byteX1 = _mm256_srai_epi32(subX1, 5);
	__m256i rbyteX2 = _mm256_srai_epi32(_mm256_sub_epi32(_mm256_set1_epi32(65535*32), subX2), 5);

	// force invalid ranges to [inf, -inf]
	__m256i infMask = _mm256_set1_epi32(0x7FFFFFFF);
	byteX1 = _mm256_blendv_epi8(infMask, byteX1, validMask);
	rbyteX2 = _mm256_blendv_epi8(infMask, rbyteX2, validMask);

	// pack to u16 and compute min and max
	__m128i minByteX1 = _mm_minpos_epu16(_mm256_castsi256_si128(_mm256_permute4x64_epi64(_mm256_packus_epi32(byteX1, byteX1), 0b0'10001000)));
	__m128i minRbyteX2 = _mm_minpos_epu16(_mm256_castsi256_si128(_mm256_permute4x64_epi64(_mm256_packus_epi32(rbyteX2, rbyteX2), 0b0'10001000)));

	const sint32 minSubX1 = _mm_extract_epi16(minByteX1, 0) << 5;
	const sint32 maxSubX2 = (_mm_extract_epi16(minRbyteX2, 0) ^ 0xFFFF) << 5;

	if (minSubX1 >= maxSubX2)
		return;

	// rasterize blocks
	ptrdiff_t offset = minSubX1;

	if constexpr (T_RGB) {
		offset *= 3;
	}

	__m256i *__restrict dst = (__m256i *)(mABuffer.data() + offset);

	__m256i u8min = _mm256_set1_epi32(0);
	__m256i u8max = _mm256_set1_epi32(255);
	__m256i compactMask = _mm256_set_epi8(
		-128, -128, -128, -128, -128, -128, -128, -128,
		-128, -128, -128, -128, 12, 8, 4, 0,
		-128, -128, -128, -128, -128, -128, -128, -128,
		-128, -128, -128, -128, 12, 8, 4, 0
	);

	__m256i loMask = _mm256_set_epi8(
		3, 3, 3, 3, 3, 3, 3, 3,
		2, 2, 2, 2, 2, 2, 2, 2,
		1, 1, 1, 1, 1, 1, 1, 1,
		0, 0, 0, 0, 0, 0, 0, 0
	);

	__m256i hiMask = _mm256_set_epi8(
		11, 11, 11, 11, 11, 11, 11, 11,
		10, 10, 10, 10, 10, 10, 10, 10,
		9, 9, 9, 9, 9, 9, 9, 9,
		8, 8, 8, 8, 8, 8, 8, 8
	);

	for(sint32 blockStart = minSubX1; blockStart < maxSubX2; blockStart += 192) {
		// compute block-relative bounds and saturate to signed byte bounds
		__m256i blockPos = _mm256_set1_epi32(blockStart);
		__m256i blockSubX1 = _mm256_min_epi32(_mm256_max_epi32(_mm256_sub_epi32(subX1, blockPos), u8min), u8max);
		__m256i blockSubX2 = _mm256_min_epi32(_mm256_max_epi32(_mm256_sub_epi32(subX2, blockPos), u8min), u8max);

		// compact to bytes
		blockSubX1 = _mm256_permute4x64_epi64(_mm256_shuffle_epi8(blockSubX1, compactMask), 0b0'10001000);
		blockSubX2 = _mm256_permute4x64_epi64(_mm256_shuffle_epi8(blockSubX2, compactMask), 0b0'10001000);

		// convert to base and biased width
		__m256i blockSubDX = _mm256_xor_si256(_mm256_subs_epu8(blockSubX2, blockSubX1), _mm256_set1_epi8(-0x80));

		// set up 64 ranges
		__m256i row0123_x1 = _mm256_shuffle_epi8(blockSubX1, loMask);
		__m256i row0123_dx = _mm256_shuffle_epi8(blockSubDX, loMask);
		__m256i row4567_x1 = _mm256_shuffle_epi8(blockSubX1, hiMask);
		__m256i row4567_dx = _mm256_shuffle_epi8(blockSubDX, hiMask);

		// set up bit position counter
		__m256i pos = _mm256_set_epi8(
			-128, -127, -126, -125, -124, -123, -122, -121,
			-128, -127, -126, -125, -124, -123, -122, -121,
			-128, -127, -126, -125, -124, -123, -122, -121,
			-128, -127, -126, -125, -124, -123, -122, -121
		);
		__m256i posinc = _mm256_set1_epi8(8);

		row0123_x1 = _mm256_sub_epi8(pos, row0123_x1);
		row4567_x1 = _mm256_sub_epi8(pos, row4567_x1);

		size_t dwordCnt = std::min<size_t>(maxSubX2 - blockStart, 192) >> 5;
		while(dwordCnt--) {
			// compute (pos >= x1 && pos < x1+dx) as (dx-128 > (pos - x1)-128) to bit mask
			const uint64 mask1
				=  (uint64)(uint32)_mm256_movemask_epi8(_mm256_cmpgt_epi8(row0123_dx, row0123_x1))
				+ ((uint64)(uint32)_mm256_movemask_epi8(_mm256_cmpgt_epi8(row4567_dx, row4567_x1)) << 32);
			row0123_x1 = _mm256_add_epi8(row0123_x1, posinc);
			row4567_x1 = _mm256_add_epi8(row4567_x1, posinc);

			const uint64 mask2
				=  (uint64)(uint32)_mm256_movemask_epi8(_mm256_cmpgt_epi8(row0123_dx, row0123_x1))
				+ ((uint64)(uint32)_mm256_movemask_epi8(_mm256_cmpgt_epi8(row4567_dx, row4567_x1)) << 32);
			row0123_x1 = _mm256_add_epi8(row0123_x1, posinc);
			row4567_x1 = _mm256_add_epi8(row4567_x1, posinc);

			const uint64 mask3
				=  (uint64)(uint32)_mm256_movemask_epi8(_mm256_cmpgt_epi8(row0123_dx, row0123_x1))
				+ ((uint64)(uint32)_mm256_movemask_epi8(_mm256_cmpgt_epi8(row4567_dx, row4567_x1)) << 32);
			row0123_x1 = _mm256_add_epi8(row0123_x1, posinc);
			row4567_x1 = _mm256_add_epi8(row4567_x1, posinc);

			const uint64 mask4
				=  (uint64)(uint32)_mm256_movemask_epi8(_mm256_cmpgt_epi8(row0123_dx, row0123_x1))
				+ ((uint64)(uint32)_mm256_movemask_epi8(_mm256_cmpgt_epi8(row4567_dx, row4567_x1)) << 32);
			row0123_x1 = _mm256_add_epi8(row0123_x1, posinc);
			row4567_x1 = _mm256_add_epi8(row4567_x1, posinc);

			__m256i mask = _mm256_set_epi64x(mask4, mask3, mask2, mask1);

			if constexpr (T_RGB) {
				_mm256_storeu_si256(dst + 0, _mm256_or_si256(_mm256_loadu_si256(dst + 0), _mm256_and_si256(mask, rdither)));
				_mm256_storeu_si256(dst + 1, _mm256_or_si256(_mm256_loadu_si256(dst + 1), _mm256_and_si256(mask, gdither)));
				_mm256_storeu_si256(dst + 2, _mm256_or_si256(_mm256_loadu_si256(dst + 2), _mm256_and_si256(mask, bdither)));
			} else {
				_mm256_storeu_si256(dst + 0, _mm256_or_si256(_mm256_loadu_si256(dst + 0), mask));
			}

			if constexpr (T_RGB) {
				dst += 3;
			} else {
				++dst;
			}
		}
	}
}
#endif

#if defined(VD_CPU_ARM64)
template<bool T_RGB>
void ATPrinterRasterizer::RenderTrapezoid_NEON(const sint32 subSpans[2][8], uint32 linearColor) {
	[[maybe_unused]] uint8x8_t rdither;
	[[maybe_unused]] uint8x8_t gdither;
	[[maybe_unused]] uint8x8_t bdither;

	if constexpr (T_RGB) {
		rdither = vld1_u8(mLevelDithers[(linearColor >> 16)&0xFF][0]);
		gdither = vld1_u8(mLevelDithers[(linearColor >>  8)&0xFF][1]);
		bdither = vld1_u8(mLevelDithers[(linearColor >>  0)&0xFF][2]);
	}

	// load subscan ranges
	const uint32x4_t subX1A = vreinterpretq_u32_s32(vld1q_s32(&subSpans[0][0]));
	const uint32x4_t subX1B = vreinterpretq_u32_s32(vld1q_s32(&subSpans[0][4]));
	const uint32x4_t subX2A = vreinterpretq_u32_s32(vld1q_s32(&subSpans[1][0]));
	const uint32x4_t subX2B = vreinterpretq_u32_s32(vld1q_s32(&subSpans[1][4]));

	// compute mask of valid ranges
	const uint32x4_t validMaskA = vcltq_u32(subX1A, subX2A);
	const uint32x4_t validMaskB = vcltq_u32(subX1B, subX2B);

	// compute min/max of non-empty scans
	uint32 minSubX1 = vminvq_u32(vminq_u32(vornq_u32(subX1A, validMaskA), vornq_u32(subX1B, validMaskB)));
	uint32 maxSubX2 = vmaxvq_u32(vmaxq_u32(vandq_u32(subX2A, validMaskA), vandq_u32(subX2B, validMaskB)));

	if (minSubX1 >= maxSubX2)
		return;

	// expand to byte boundaries
	minSubX1 &= ~(uint32)7;
	maxSubX2 = (maxSubX2 + 7) & ~(uint32)7;

	// rasterize blocks
	uint8 *VDRESTRICT dst = mABuffer.data();

	if constexpr (T_RGB) {
		dst += (ptrdiff_t)minSubX1*3;
	} else {
		dst += (ptrdiff_t)minSubX1;
	}

	for(uint32 blockStart = minSubX1; blockStart < maxSubX2; blockStart += 192) {
		// compute block-relative bounds
		const uint32x4_t blockPos = vmovq_n_u32(blockStart);
		const uint32x4_t blockSubX1A = vqsubq_u32(subX1A, blockPos);
		const uint32x4_t blockSubX1B = vqsubq_u32(subX1B, blockPos);
		const uint32x4_t blockSubX2A = vqsubq_u32(subX2A, blockPos);
		const uint32x4_t blockSubX2B = vqsubq_u32(subX2B, blockPos);

		// compress to unsigned bytes with saturation
		const uint16x4_t zu16 = vmov_n_u16(0);
		const uint8x8_t blockSubX1 = vqmovn_u16(vcombine_u16(vqmovn_u32(blockSubX1A), vqmovn_u32(blockSubX1B)));
		const uint8x8_t blockSubX2 = vqmovn_u16(vcombine_u16(vqmovn_u32(blockSubX2A), vqmovn_u32(blockSubX2B)));
		const uint8x8_t blockSubDX = vqsub_u8(blockSubX2, blockSubX1);

		// broadcast 8x
		const uint8x16_t row01_x1 = vreinterpretq_u8_u16(vdupq_lane_u16(vreinterpret_u16_u8(blockSubX1), 0));
		const uint8x16_t row23_x1 = vreinterpretq_u8_u16(vdupq_lane_u16(vreinterpret_u16_u8(blockSubX1), 1));
		const uint8x16_t row45_x1 = vreinterpretq_u8_u16(vdupq_lane_u16(vreinterpret_u16_u8(blockSubX1), 2));
		const uint8x16_t row67_x1 = vreinterpretq_u8_u16(vdupq_lane_u16(vreinterpret_u16_u8(blockSubX1), 3));
		const uint8x16_t row01_dx = vreinterpretq_u8_u16(vdupq_lane_u16(vreinterpret_u16_u8(blockSubDX), 0));
		const uint8x16_t row23_dx = vreinterpretq_u8_u16(vdupq_lane_u16(vreinterpret_u16_u8(blockSubDX), 1));
		const uint8x16_t row45_dx = vreinterpretq_u8_u16(vdupq_lane_u16(vreinterpret_u16_u8(blockSubDX), 2));
		const uint8x16_t row67_dx = vreinterpretq_u8_u16(vdupq_lane_u16(vreinterpret_u16_u8(blockSubDX), 3));

		// set up bit position counter
		static constexpr uint8_t kInitialBitPos[] { 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7 };
		uint8x16_t pos = vld1q_u8(kInitialBitPos);
		uint8x16_t posinc = vmovq_n_u8(8);

		size_t byteCnt = std::min<size_t>(maxSubX2 - blockStart, 192) >> 3;
		while(byteCnt--) {
			// compute (pos - x1) < dx
			uint8x16_t mask16 =         vcltq_u8(vsubq_u8(pos, row01_x1), row01_dx);
			mask16 = vsliq_n_u8(mask16, vcltq_u8(vsubq_u8(pos, row23_x1), row23_dx), 1);
			mask16 = vsliq_n_u8(mask16, vcltq_u8(vsubq_u8(pos, row45_x1), row45_dx), 2);
			mask16 = vsliq_n_u8(mask16, vcltq_u8(vsubq_u8(pos, row67_x1), row67_dx), 3);

			uint8x8_t mask = vsli_n_u8(vget_low_u8(mask16), vget_high_u8(mask16), 4);

			if constexpr (T_RGB) {
				vst1_u8(dst +  0, vorr_u8(vld1_u8(dst +  0), vand_u8(mask, rdither)));
				vst1_u8(dst +  8, vorr_u8(vld1_u8(dst +  8), vand_u8(mask, gdither)));
				vst1_u8(dst + 16, vorr_u8(vld1_u8(dst + 16), vand_u8(mask, bdither)));
				dst += 24;
			} else {
				vst1_u8(dst, vorr_u8(vld1_u8(dst), mask));
				dst += 8;
			}

			pos = vqaddq_u8(pos, posinc);
		}
	}
}
#endif

void ATPrinterRasterizer::Downsample8x8(uint32 *dst, uint8 *src, size_t w, bool rgb) {
	if (rgb) {
#ifdef VD_CPU_X64
#ifdef AT_PRINTERRASTERIZER_USE_AVX2
		if (VDCheckAllExtensionsEnabled(VDCPUF_SUPPORTS_AVX | VDCPUF_SUPPORTS_AVX2 | VDCPUF_SUPPORTS_FMA | VDCPUF_SUPPORTS_POPCNT))
			return DownsampleRGB8x8_AVX2(dst, src, w);
#endif

		if (VDCheckAllExtensionsEnabled(VDCPUF_SUPPORTS_POPCNT))
			return DownsampleRGB8x8_POPCNT64(dst, src, w);
#endif

#ifdef VD_CPU_X86
		if (VDCheckAllExtensionsEnabled(VDCPUF_SUPPORTS_POPCNT))
			return DownsampleRGB8x8_POPCNT32(dst, src, w);
#endif

#ifdef VD_CPU_ARM64
		return DownsampleRGB8x8_NEON(dst, src, w);
#else
		DownsampleRGB8x8_Scalar(dst, src, w);
#endif
	} else {
#ifdef VD_CPU_X64
		if (VDCheckAllExtensionsEnabled(VDCPUF_SUPPORTS_POPCNT))
			return Downsample8x8_POPCNT64(dst, src, w);
#endif

#ifdef VD_CPU_X86
		if (VDCheckAllExtensionsEnabled(VDCPUF_SUPPORTS_POPCNT))
			return Downsample8x8_POPCNT32(dst, src, w);
#endif

#ifdef VD_CPU_ARM64
		return Downsample8x8_NEON(dst, src, w);
#else
		Downsample8x8_Scalar(dst, src, w);
#endif
	}
}

void ATPrinterRasterizer::Downsample8x8_Scalar(uint32 *dst, uint8 *src, size_t w) {
	uint32 *__restrict fbdst = dst;
	uint8 *__restrict asrc = src;

	for(size_t x = 0; x < w; ++x) {
		// compute coverage by counting bits within 8x8 window
		const uint32 level
			= mPopCnt8[asrc[0]]
			+ mPopCnt8[asrc[1]]
			+ mPopCnt8[asrc[2]]
			+ mPopCnt8[asrc[3]]
			+ mPopCnt8[asrc[4]]
			+ mPopCnt8[asrc[5]]
			+ mPopCnt8[asrc[6]]
			+ mPopCnt8[asrc[7]];

		VDWriteUnalignedLEU64(asrc, 0);
		asrc += 8;

		// convert coverage to sRGB color
		*fbdst++ = mGammaTable[level];
	}
}

#ifdef VD_CPU_X64
VD_CPU_TARGET("popcnt")
void ATPrinterRasterizer::Downsample8x8_POPCNT64(uint32 *dst, uint8 *src, size_t w) {
	uint32 *__restrict fbdst = dst;
	uint8 *__restrict asrc = src;

	for(size_t x = 0; x < w; ++x) {
		// compute coverage by counting bits within 8x8 window
		const size_t level = _mm_popcnt_u64(VDReadUnalignedU64(asrc));
		VDWriteUnalignedLEU64(asrc, 0);
		asrc += 8;

		// convert coverage to sRGB color
		*fbdst++ = mGammaTable[level];
	}
}
#endif

#ifdef VD_CPU_X86
void ATPrinterRasterizer::Downsample8x8_POPCNT32(uint32 *dst, uint8 *src, size_t w) {
	uint32 *__restrict fbdst = dst;
	uint8 *__restrict asrc = src;

	for(size_t x = 0; x < w; ++x) {
		// compute coverage by counting bits within 8x8 window
		const size_t level = _mm_popcnt_u32(VDReadUnalignedU32(asrc))
			+ _mm_popcnt_u32(VDReadUnalignedU32(asrc + 4));
		VDWriteUnalignedLEU32(asrc, 0);
		VDWriteUnalignedLEU32(asrc + 4, 0);
		asrc += 8;

		// convert coverage to sRGB color
		*fbdst++ = mGammaTable[level];
	}
}
#endif

#ifdef VD_CPU_ARM64
void ATPrinterRasterizer::Downsample8x8_NEON(uint32 *dst, uint8 *src, size_t w) {
	uint32 *__restrict fbdst = dst;
	uint8 *__restrict asrc = src;
	const auto *__restrict gtab = mGammaTable;

	uint8x8_t zero = vmov_n_u8(0);

	for(size_t x = 0; x < w; ++x) {
		// compute coverage by counting bits within 8x8 window
		const uint32 level = vaddv_u8(vcnt_u8(vld1_u8(asrc)));
		vst1_u8(asrc, zero);
		asrc += 8;

		// convert coverage to sRGB color
		*fbdst++ = gtab[level];
	}
}
#endif

void ATPrinterRasterizer::DownsampleRGB8x8_Scalar(uint32 *dst, uint8 *src, size_t w) {
	uint32 *__restrict fbdst = dst;
	uint8 *__restrict asrc = src;

	for(size_t x = 0; x < w; ++x) {
		// compute coverage by counting bits within 8x8 window
		const uint32 rlevel
			= mPopCnt8[asrc[0]]
			+ mPopCnt8[asrc[1]]
			+ mPopCnt8[asrc[2]]
			+ mPopCnt8[asrc[3]]
			+ mPopCnt8[asrc[4]]
			+ mPopCnt8[asrc[5]]
			+ mPopCnt8[asrc[6]]
			+ mPopCnt8[asrc[7]];

		const uint32 glevel
			= mPopCnt8[asrc[8]]
			+ mPopCnt8[asrc[9]]
			+ mPopCnt8[asrc[10]]
			+ mPopCnt8[asrc[11]]
			+ mPopCnt8[asrc[12]]
			+ mPopCnt8[asrc[13]]
			+ mPopCnt8[asrc[14]]
			+ mPopCnt8[asrc[15]];

		const uint32 blevel
			= mPopCnt8[asrc[16]]
			+ mPopCnt8[asrc[17]]
			+ mPopCnt8[asrc[18]]
			+ mPopCnt8[asrc[19]]
			+ mPopCnt8[asrc[20]]
			+ mPopCnt8[asrc[21]]
			+ mPopCnt8[asrc[22]]
			+ mPopCnt8[asrc[23]];

		VDWriteUnalignedLEU64(asrc, 0);
		VDWriteUnalignedLEU64(asrc + 8, 0);
		VDWriteUnalignedLEU64(asrc + 16, 0);

		asrc += 24;

		// convert coverage to sRGB color
		*fbdst++
			= (mGammaTable[rlevel] & 0xFF0000)
			+ (mGammaTable[glevel] & 0x00FF00)
			+ (mGammaTable[blevel] & 0x0000FF)
			;
	}
}

#ifdef VD_CPU_X64
VD_CPU_TARGET("popcnt")
void ATPrinterRasterizer::DownsampleRGB8x8_POPCNT64(uint32 *dst, uint8 *src, size_t w) {
	uint32 *__restrict fbdst = dst;
	uint8 *__restrict asrc = src;

	for(size_t x = 0; x < w; ++x) {
		// compute coverage by counting bits within 8x8 window
		const size_t rlevel = _mm_popcnt_u64(VDReadUnalignedU64(asrc));
		const size_t glevel = _mm_popcnt_u64(VDReadUnalignedU64(asrc+8));
		const size_t blevel = _mm_popcnt_u64(VDReadUnalignedU64(asrc+16));

		VDWriteUnalignedLEU64(asrc, 0);
		VDWriteUnalignedLEU64(asrc + 8, 0);
		VDWriteUnalignedLEU64(asrc + 16, 0);

		asrc += 24;

		// convert coverage to sRGB color
		*fbdst++
			= (mGammaTable[rlevel] & 0xFF0000)
			+ (mGammaTable[glevel] & 0x00FF00)
			+ (mGammaTable[blevel] & 0x0000FF)
			;
	}
}

VD_CPU_TARGET("avx2,popcnt")
void ATPrinterRasterizer::DownsampleRGB8x8_AVX2(uint32 *dst, uint8 *src, size_t w) {
	uint32 *__restrict fbdst = dst;
	uint8 *__restrict asrc = src;

	// We need to use a special routine when AVX2 is active since the A-buffer layout
	// is different. The algorithm is the same as the POPCNT64 version. This is only
	// needed for RGB mode as the layout is compatible in monochrome mode.
	//
	// Note that the framebuffer is padded to ensure that we can do quads without a
	// special tail.

	__m256i z = _mm256_setzero_si256();

	size_t w4 = (w + 3) >> 2;
	for(size_t x = 0; x < w4; ++x) {
		// compute coverage by counting bits within 8x8 window
		const size_t rlevel0 = _mm_popcnt_u64(VDReadUnalignedU64(asrc +  0));
		const size_t rlevel1 = _mm_popcnt_u64(VDReadUnalignedU64(asrc +  8));
		const size_t rlevel2 = _mm_popcnt_u64(VDReadUnalignedU64(asrc + 16));
		const size_t rlevel3 = _mm_popcnt_u64(VDReadUnalignedU64(asrc + 24));
		const size_t glevel0 = _mm_popcnt_u64(VDReadUnalignedU64(asrc + 32));
		const size_t glevel1 = _mm_popcnt_u64(VDReadUnalignedU64(asrc + 40));
		const size_t glevel2 = _mm_popcnt_u64(VDReadUnalignedU64(asrc + 48));
		const size_t glevel3 = _mm_popcnt_u64(VDReadUnalignedU64(asrc + 56));
		const size_t blevel0 = _mm_popcnt_u64(VDReadUnalignedU64(asrc + 64));
		const size_t blevel1 = _mm_popcnt_u64(VDReadUnalignedU64(asrc + 72));
		const size_t blevel2 = _mm_popcnt_u64(VDReadUnalignedU64(asrc + 80));
		const size_t blevel3 = _mm_popcnt_u64(VDReadUnalignedU64(asrc + 88));

		_mm256_store_si256((__m256i *)(asrc + 0), z);
		_mm256_store_si256((__m256i *)(asrc + 32), z);
		_mm256_store_si256((__m256i *)(asrc + 64), z);

		asrc += 96;

		// convert coverage to sRGB color
		*fbdst++
			= (mGammaTable[rlevel0] & 0xFF0000)
			+ (mGammaTable[glevel0] & 0x00FF00)
			+ (mGammaTable[blevel0] & 0x0000FF)
			;

		*fbdst++
			= (mGammaTable[rlevel1] & 0xFF0000)
			+ (mGammaTable[glevel1] & 0x00FF00)
			+ (mGammaTable[blevel1] & 0x0000FF)
			;

		*fbdst++
			= (mGammaTable[rlevel2] & 0xFF0000)
			+ (mGammaTable[glevel2] & 0x00FF00)
			+ (mGammaTable[blevel2] & 0x0000FF)
			;

		*fbdst++
			= (mGammaTable[rlevel3] & 0xFF0000)
			+ (mGammaTable[glevel3] & 0x00FF00)
			+ (mGammaTable[blevel3] & 0x0000FF)
			;
	}
}
#endif

#ifdef VD_CPU_X86
void ATPrinterRasterizer::DownsampleRGB8x8_POPCNT32(uint32 *dst, uint8 *src, size_t w) {
	uint32 *__restrict fbdst = dst;
	uint8 *__restrict asrc = src;

	for(size_t x = 0; x < w; ++x) {
		// compute coverage by counting bits within 8x8 window
		const size_t rlevel = _mm_popcnt_u32(VDReadUnalignedU32(asrc))
							+ _mm_popcnt_u32(VDReadUnalignedU32(asrc+4));
		const size_t glevel = _mm_popcnt_u32(VDReadUnalignedU32(asrc+8))
							+ _mm_popcnt_u32(VDReadUnalignedU32(asrc+12));
		const size_t blevel = _mm_popcnt_u32(VDReadUnalignedU32(asrc+16))
							+ _mm_popcnt_u32(VDReadUnalignedU32(asrc+20));

		VDWriteUnalignedLEU32(asrc, 0);
		VDWriteUnalignedLEU32(asrc + 4, 0);
		VDWriteUnalignedLEU32(asrc + 8, 0);
		VDWriteUnalignedLEU32(asrc + 12, 0);
		VDWriteUnalignedLEU32(asrc + 16, 0);
		VDWriteUnalignedLEU32(asrc + 20, 0);

		asrc += 24;

		// convert coverage to sRGB color
		*fbdst++
			= (mGammaTable[rlevel] & 0xFF0000)
			+ (mGammaTable[glevel] & 0x00FF00)
			+ (mGammaTable[blevel] & 0x0000FF)
			;
	}
}
#endif

#ifdef VD_CPU_ARM64
void ATPrinterRasterizer::DownsampleRGB8x8_NEON(uint32 *dst, uint8 *src, size_t w) {
	uint32 *__restrict fbdst = dst;
	uint8 *__restrict asrc = src;

	uint8x8_t zero = vmov_n_u8(0);

	for(size_t x = 0; x < w; ++x) {
		// compute coverage by counting bits within 8x8 window
		const uint32 rlevel = vaddv_u8(vcnt_u8(vld1_u8(asrc)));
		const uint32 glevel = vaddv_u8(vcnt_u8(vld1_u8(asrc+8)));
		const uint32 blevel = vaddv_u8(vcnt_u8(vld1_u8(asrc+16)));

		vst1_u8(asrc, zero);
		vst1_u8(asrc + 8, zero);
		vst1_u8(asrc + 16, zero);

		asrc += 24;

		// convert coverage to sRGB color
		*fbdst++
			= (mGammaTable[rlevel] & 0xFF0000)
			+ (mGammaTable[glevel] & 0x00FF00)
			+ (mGammaTable[blevel] & 0x0000FF)
			;
	}
}
#endif
