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

#ifndef f_AT_PRINTERRASTERIZER_H
#define f_AT_PRINTERRASTERIZER_H

#include <optional>
#include <vd2/system/vdtypes.h>
#include "printeroutput.h"

// Printer rasterizer
//
// The printer rasterizer converts the vector dot/line representation in a
// graphical printer output to a rendered image, at the requested size and
// position for display or export. It currently uses an 8x gamma-corrected,
// dithered antialiasing algorithm.
//
class ATPrinterRasterizer {
	ATPrinterRasterizer(const ATPrinterRasterizer&) = delete;
	ATPrinterRasterizer& operator=(const ATPrinterRasterizer&) = delete;

public:
	static constexpr float kBlackLevel = 0.00f;
	static constexpr float kWhiteLevel = 1.00f;

	struct ViewTransform {
		float mOriginX = 0;
		float mOriginY = 0;
		float mPixelsPerMM = 1;
		float mMMPerPixel = 1;
	};

	ATPrinterRasterizer();
	~ATPrinterRasterizer();

	// Return the background color used where no dots have been printed.
	uint32 GetPaperBackgroundColor() const {
		return mGammaTable[0];
	}

	// Trim large buffers that may have been allocated after recent requests.
	void TrimBuffers();

	// Render the requested rectangle.
	//
	//	output:
	//		Printer output to render. If null, blank paper is rendered.
	//
	//	viewTransform:
	//		Defines transform between viewport and document coordinates.
	//
	//	x, y, w, h:
	//		Rectangle to render in viewport coordinates (pixels).
	//
	//	force:
	//		If true, force rendering even if blank.
	//
	//	Returns:
	//		Pixmap of rendered rectangle, if non-empty or force was set.
	//		Valid until next Render() or TrimBuffers(). Nullopt is returned
	//		if force=false and the output is blank.
	//
	std::optional<VDPixmap> Render(ATPrinterGraphicalOutput *output, const ViewTransform& viewTransform, sint32 x, sint32 y, uint32 w, uint32 h, bool force);

private:
	void SortCullBuffers(float minY, float scanLineHeightY, sint32 numScanLines, float dotRadius, const vdrect32f& fullDotCullRect);

	std::optional<VDPixmap> RenderBlank(uint32 w, uint32 h);
	void RenderTrapezoid(const sint32 subSpans[2][8], uint32 linearColor, bool rgb);
	void RenderTrapezoidRGB_Scalar(const sint32 subSpans[2][8], uint32 linearColor);

#if defined(VD_CPU_X64) || defined(VD_CPU_X86)
	template<bool T_RGB>
	void RenderTrapezoid_SSE2(const sint32 subSpans[2][8], uint32 linearColor);
#if defined(VD_CPU_X64)
	template<bool T_RGB>
	VD_CPU_TARGET("avx2,fma")
	void RenderTrapezoid_AVX2(const sint32 subSpans[2][8], uint32 linearColor);
#endif
#elif defined(VD_CPU_ARM64)
	template<bool T_RGB>
	void RenderTrapezoid_NEON(const sint32 subSpans[2][8], uint32 linearColor);
#endif

	void Downsample8x8(uint32 *dst, uint8 *src, size_t w, bool rgb);
	void Downsample8x8_Scalar(uint32 *dst, uint8 *src, size_t w);
#if defined(VD_CPU_X64)
	void Downsample8x8_POPCNT64(uint32 *dst, uint8 *src, size_t w);
#elif defined(VD_CPU_X86)
	void Downsample8x8_POPCNT32(uint32 *dst, uint8 *src, size_t w);
#elif defined(VD_CPU_ARM64)
	void Downsample8x8_NEON(uint32 *dst, uint8 *src, size_t w);
#endif

	void DownsampleRGB8x8_Scalar(uint32 *dst, uint8 *src, size_t w);
#if defined(VD_CPU_X64)
	void DownsampleRGB8x8_POPCNT64(uint32 *dst, uint8 *src, size_t w);
	void DownsampleRGB8x8_AVX2(uint32 *dst, uint8 *src, size_t w);
#elif defined(VD_CPU_X86)
	void DownsampleRGB8x8_POPCNT32(uint32 *dst, uint8 *src, size_t w);
#elif defined(VD_CPU_ARM64)
	void DownsampleRGB8x8_NEON(uint32 *dst, uint8 *src, size_t w);
#endif

	struct IndexBlock {
		static constexpr size_t kBlockSize = 32;

		uint32 mIndices[kBlockSize];
		IndexBlock *mpPrev;
	};

	vdfastvector<IndexBlock *> mIndexBlocks;

	using RenderDot = ATPrinterGraphicalOutput::RenderDot;
	vdfastvector<RenderDot> mDotCullBuffer;
	vdfastvector<RenderDot> mDotCullBuffer2;

	using RenderVector = ATPrinterGraphicalOutput::RenderVector;
	vdfastvector<RenderVector> mVectorCullBuffer;
	vdfastvector<RenderVector> mVectorCullBuffer2;

	struct VectorEdge {
		float x;
		float slope;
	};

	struct SortBin {
		IndexBlock *mpCurrentIndexBlock = nullptr;
		size_t mIndicesLeftInBlock = 0;
		size_t mTotalBlocks = 0;
	};

	vdvector<SortBin> mSortBins;

	struct RenderBin {
		uint32 mNumVectors;
		uint32 mDotStart;
		uint32 mDotEnd;
	};

	vdfastvector<RenderBin> mRenderBins;

	struct DotHashNode {
		uint32 mX;
		uint32 mColorAndMicroY;

		bool operator==(const DotHashNode&) const = default;
	};

	vdfastvector<DotHashNode> mDotHash;

	struct ExpandedVector {
		float mCullY1;
		float mCullY2;
		uint32 mLinearColor;
		uint32 mPad;
		vdfloat32x4 mEdgeX;
		vdfloat32x4 mEdgeSlope;
	};

	vdfastvector<ExpandedVector, vdaligned_alloc<ExpandedVector>> mVectorExpansionBuffer;

	vdfastvector<uint8, vdaligned_alloc<uint8, 32>> mABuffer;
	vdfastvector<uint32, vdaligned_alloc<uint32, 32>> mFrameBuffer;

	uint8 mPopCnt8[256];
	uint32 mGammaTable[65];
	alignas(8) uint8 mLevelDithers[65][3][8] {};
};

#endif
