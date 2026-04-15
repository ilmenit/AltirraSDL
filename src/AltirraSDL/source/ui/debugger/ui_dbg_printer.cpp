//	AltirraSDL - Dear ImGui debugger printer output pane
//	Replaces Win32 ATPrinterOutputWindow (uidbgprinteroutput.cpp).
//	Displays text output from emulated printers.  Supports multiple
//	printer outputs via a dropdown selector, with Clear button.
//	Graphical printer output is rendered as a rasterized image with
//	zoom/pan controls and export to PNG/PDF/SVG.

#include <stdafx.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <imgui.h>
#include <SDL3/SDL.h>
#include "ui_file_dialog_sdl3.h"
#include <vd2/system/binary.h>
#include <vd2/system/color.h>
#include <vd2/system/file.h>
#include <vd2/system/strutil.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/function.h>
#include <vd2/system/vectors.h>
#include "ui_debugger.h"
#include "console.h"
#include "debugger.h"
#include "simulator.h"
#include "printeroutput.h"
#include "display_backend.h"
#include "gl_helpers.h"
#include "printer_internal.h"

extern ATSimulator g_sim;
extern SDL_Window *g_pWindow;

// =========================================================================
// Graphical printer renderer — software rasterizer
// =========================================================================
//
// Scalar version of the Windows 8x8 anti-aliased rasterizer.  Renders dots
// and vectors from ATPrinterGraphicalOutput to an XRGB8888 framebuffer.
// Uses 4x sub-sampling vertically with analytical horizontal coverage for
// anti-aliasing.  Supports full per-dot and per-vector color using the same
// linear RGB colour model as the Windows version.

namespace {

static constexpr uint32 kPaperBorderColor = 0xFF808080;

struct PrinterViewTransform {
	float mOriginX = 0;		// document-space left edge of viewport (mm)
	float mOriginY = 0;		// document-space top edge of viewport (mm)
	float mPixelsPerMM = 1;
	float mMMPerPixel = 1;
};

// Convert a linear-space printer color to an sRGB pixel value suitable for
// the ARGB8888 framebuffer.
//
// The linear color is produced by ConvertColor() which passes the input
// through FromBGR8() + SRGBToLinear() + packus8(*64).  This stores the
// channels in native byte order: byte 0 is the component that FromBGR8()
// placed in element 0.  Using ToBGR8() (which packs elements without
// permuting) recovers the correct 0x00RRGGBB value (byte layout
// B,G,R,X) — the same XRGB8888 layout GLCreateXRGB8888Texture /
// GLUploadXRGB8888 expect, and the one CSS #RRGGBB encodes.  Using
// ToRGB8() would apply an extra permute and swap R/B.
static uint32 LinearColorToSRGB8(uint32 linearColor) {
	uint32 srgb = VDColorRGB(vdfloat32x4::unpacku8(linearColor) * (1.0f / 64.0f)).LinearToSRGB().ToBGR8();
	return 0xFF000000 | srgb;
}

// Composite a printer dot (with color) onto an existing pixel.
// Uses 'darken' compositing — the printer deposits ink, so a new dot can
// only make the paper darker, never lighter.
static uint32 CompositePixel(uint32 bg, uint32 inkSRGB, float coverage) {
	if (coverage <= 0.0f)
		return bg;
	if (coverage > 1.0f)
		coverage = 1.0f;

	uint32 ir = (inkSRGB >> 16) & 0xFF;
	uint32 ig = (inkSRGB >> 8) & 0xFF;
	uint32 ib = inkSRGB & 0xFF;

	uint32 br = (bg >> 16) & 0xFF;
	uint32 bg_ = (bg >> 8) & 0xFF;
	uint32 bb = bg & 0xFF;

	// Lerp toward ink color based on coverage
	uint32 or_ = (uint32)(br + (int)(((int)ir - (int)br) * coverage));
	uint32 og = (uint32)(bg_ + (int)(((int)ig - (int)bg_) * coverage));
	uint32 ob = (uint32)(bb + (int)(((int)ib - (int)bb) * coverage));

	return 0xFF000000 | (or_ << 16) | (og << 8) | ob;
}

// Render dots and vectors to a framebuffer.
// The framebuffer is stored top-down (row 0 = top of image).
// Areas outside the paper are filled with kPaperBorderColor.
static void RenderPrinterOutput(
	ATPrinterGraphicalOutput& output,
	const PrinterViewTransform& vt,
	uint32 *framebuffer,
	uint32 w, uint32 h,
	float dotRadiusMM,
	float pageWidthMM)
{
	if (w == 0 || h == 0)
		return;

	// Compute document-space bounds of the viewport
	const float docX1 = vt.mOriginX;
	const float docY1 = vt.mOriginY;
	const float docX2 = vt.mOriginX + (float)w * vt.mMMPerPixel;
	const float docY2 = vt.mOriginY + (float)h * vt.mMMPerPixel;

	// Fill framebuffer: white for paper area, gray for outside.
	// Precompute paper edge pixel positions for efficiency.
	int paperLeftPx = std::max(0, (int)ceilf(-docX1 * vt.mPixelsPerMM));
	int paperRightPx = std::min((int)w, (int)ceilf((pageWidthMM - docX1) * vt.mPixelsPerMM));
	int paperTopPx = std::max(0, (int)ceilf(-docY1 * vt.mPixelsPerMM));

	for (uint32 y = 0; y < h; ++y) {
		uint32 *row = &framebuffer[y * w];
		if ((int)y < paperTopPx || paperLeftPx >= paperRightPx) {
			std::fill(row, row + w, kPaperBorderColor);
		} else {
			if (paperLeftPx > 0)
				std::fill(row, row + paperLeftPx, kPaperBorderColor);
			int pl = std::max(0, paperLeftPx);
			int pr = std::max(0, std::min((int)w, paperRightPx));
			if (pr > pl)
				std::fill(row + pl, row + pr, 0xFFFFFFFF);
			if (paperRightPx < (int)w)
				std::fill(row + std::max(0, paperRightPx), row + w, kPaperBorderColor);
		}
	}

	const bool hasVectors = output.HasVectors();

	// Pre-cull
	ATPrinterGraphicalOutput::CullInfo cullInfo;
	vdrect32f cullRect(docX1, docY1, docX2, docY2);
	bool hasDots = output.PreCull(cullInfo, cullRect);

	if (!hasDots && !hasVectors)
		return;

	const float dotRadius = dotRadiusMM;
	const float dotRadiusSq = dotRadius * dotRadius;
	static constexpr int kSubRows = 4;

	using RenderDot = ATPrinterGraphicalOutput::RenderDot;
	using RenderVector = ATPrinterGraphicalOutput::RenderVector;
	vdfastvector<RenderDot> dotBuf;
	vdfastvector<RenderVector> vecBuf;

	// Per-pixel coverage buffer (one row of pixels × kSubRows sub-rows).
	// Each entry stores 0..256 for that sub-row (256 = fully covered).
	// Final coverage = sum of kSubRows entries / (kSubRows * 256).
	std::vector<uint16> abufCov(w * kSubRows, 0);
	// Per-pixel ink color for blending (accumulated sRGB)
	std::vector<uint32> inkColor(w, 0);

	for (uint32 yoff = 0; yoff < h; ++yoff) {
		const float docRowYC = docY1 + ((float)yoff + 0.5f) * vt.mMMPerPixel;
		const float docRowYD = 0.5f * vt.mMMPerPixel;
		const float docRowY1 = docRowYC - docRowYD;
		const float docRowY2 = docRowYC + docRowYD;

		// Cull dots to this scanline
		dotBuf.clear();
		vecBuf.clear();

		vdrect32f lineCullRect(docX1 - vt.mMMPerPixel * 0.5f, docRowY1,
			docX2 + vt.mMMPerPixel * 0.5f, docRowY2);
		output.ExtractNextLineDots(dotBuf, cullInfo, lineCullRect);

		if (hasVectors) {
			output.ExtractVectors(vecBuf, lineCullRect);

			vdrect32f dotCullRect(
				lineCullRect.left - dotRadiusMM,
				lineCullRect.top - dotRadiusMM,
				lineCullRect.right + dotRadiusMM,
				lineCullRect.bottom + dotRadiusMM);

			for (const RenderVector& v : vecBuf) {
				if (dotCullRect.contains(vdpoint32f{v.mX1, v.mY1}))
					dotBuf.push_back(RenderDot{v.mX1, v.mY1, v.mLinearColor});
				if (dotCullRect.contains(vdpoint32f{v.mX2, v.mY2}))
					dotBuf.push_back(RenderDot{v.mX2, v.mY2, v.mLinearColor});
			}
		}

		if (dotBuf.empty() && vecBuf.empty())
			continue;

		// Clear coverage and ink buffers
		std::fill(abufCov.begin(), abufCov.end(), (uint16)0);
		std::fill(inkColor.begin(), inkColor.end(), (uint32)0);

		// Render dots with analytical horizontal coverage
		for (const RenderDot& dot : dotBuf) {
			float dy = dot.mY - docRowYC;
			if (fabsf(dy) >= docRowYD + dotRadius)
				continue;

			uint32 srgbInk = LinearColorToSRGB8(dot.mLinearColor);

			for (int sr = 0; sr < kSubRows; ++sr) {
				float subRowY = docRowYC + ((float)sr - (kSubRows - 1) * 0.5f) / (float)kSubRows * vt.mMMPerPixel;
				float sdy = dot.mY - subRowY;
				float dxSq = dotRadiusSq - sdy * sdy;
				if (dxSq <= 0.0f)
					continue;

				float dxMM = sqrtf(dxSq);
				// Dot circle x-range in pixel coordinates
				float dotLeftPx = (dot.mX - dxMM - docX1) * vt.mPixelsPerMM;
				float dotRightPx = (dot.mX + dxMM - docX1) * vt.mPixelsPerMM;

				// Pixel range affected (with 1px margin for partial coverage)
				int ix1 = std::max(0, (int)floorf(dotLeftPx));
				int ix2 = std::min((int)w, (int)ceilf(dotRightPx));

				uint16 *row = &abufCov[sr * w];
				for (int x = ix1; x < ix2; ++x) {
					// Analytical horizontal coverage: fraction of pixel [x, x+1] covered
					float pxLeft = (float)x;
					float pxRight = (float)(x + 1);
					float covLeft = std::max(pxLeft, dotLeftPx);
					float covRight = std::min(pxRight, dotRightPx);
					float hCov = std::max(0.0f, covRight - covLeft);

					// Each sub-row tracks its own 0..256 coverage (256 = fully covered).
					// Final coverage is the average across kSubRows sub-rows.
					uint16 covVal = (uint16)std::min(256.0f, hCov * 256.0f);
					row[x] = std::min<uint16>(row[x] + covVal, 256);

					// Track ink color (first writer wins for simplicity, like the
					// Windows dither approach for single-color dots)
					if (covVal > 0 && inkColor[x] == 0)
						inkColor[x] = srgbInk;
				}
			}
		}

		// Render vectors
		for (const RenderVector& v : vecBuf) {
			float dx = v.mX2 - v.mX1;
			float dy = v.mY2 - v.mY1;
			float lenSq = dx * dx + dy * dy;
			if (lenSq < 1e-6f)
				continue;

			float invLen = 1.0f / sqrtf(lenSq);
			float px = -dy * invLen * dotRadius;
			float py = dx * invLen * dotRadius;

			// Four corners of the line rectangle (counterclockwise)
			float cx[4] = {
				v.mX1 + px, v.mX1 - px,
				v.mX2 - px, v.mX2 + px
			};
			float cy[4] = {
				v.mY1 + py, v.mY1 - py,
				v.mY2 - py, v.mY2 + py
			};

			uint32 srgbInk = LinearColorToSRGB8(v.mLinearColor);

			for (int sr = 0; sr < kSubRows; ++sr) {
				float subRowY = docRowYC + ((float)sr - (kSubRows - 1) * 0.5f) / (float)kSubRows * vt.mMMPerPixel;

				// Find x-range of the rectangle at this y using edge intersections
				float xmin = 1e10f, xmax = -1e10f;
				for (int e = 0; e < 4; ++e) {
					int e2 = (e + 1) & 3;
					float ey1 = cy[e], ey2 = cy[e2];
					if ((ey1 <= subRowY && ey2 >= subRowY) ||
						(ey2 <= subRowY && ey1 >= subRowY)) {
						float t = (ey1 == ey2) ? 0.5f : (subRowY - ey1) / (ey2 - ey1);
						float ex = cx[e] + t * (cx[e2] - cx[e]);
						xmin = std::min(xmin, ex);
						xmax = std::max(xmax, ex);
					}
				}

				if (xmin >= xmax)
					continue;

				float vecLeftPx = (xmin - docX1) * vt.mPixelsPerMM;
				float vecRightPx = (xmax - docX1) * vt.mPixelsPerMM;

				int ix1 = std::max(0, (int)floorf(vecLeftPx));
				int ix2 = std::min((int)w, (int)ceilf(vecRightPx));

				uint16 *row = &abufCov[sr * w];
				for (int x = ix1; x < ix2; ++x) {
					float pxLeft = (float)x;
					float pxRight = (float)(x + 1);
					float covLeft = std::max(pxLeft, vecLeftPx);
					float covRight = std::min(pxRight, vecRightPx);
					float hCov = std::max(0.0f, covRight - covLeft);

					uint16 covVal = (uint16)std::min(256.0f, hCov * 256.0f);
					row[x] = std::min<uint16>(row[x] + covVal, 256);

					if (covVal > 0 && inkColor[x] == 0)
						inkColor[x] = srgbInk;
				}
			}
		}

		// Composite coverage into framebuffer
		uint32 *dst = &framebuffer[yoff * w];
		for (uint32 x = 0; x < w; ++x) {
			uint32 totalCov = abufCov[0 * w + x] + abufCov[1 * w + x] +
				abufCov[2 * w + x] + abufCov[3 * w + x];

			if (totalCov == 0)
				continue;

			float coverage = std::min(1.0f, (float)totalCov / 1024.0f);
			uint32 ink = inkColor[x];
			if (ink == 0)
				ink = 0xFF000000;	// fallback to black

			dst[x] = CompositePixel(dst[x], ink, coverage);
		}
	}
}

struct PrinterSaveRequest {
	enum class Format { PNG96, PNG300, PDF, SVG };
	Format mFormat;
};

static PrinterSaveRequest s_pendingSaveRequest {};
static std::string s_pendingSavePath;
static bool s_hasPendingSave = false;

static void SDLCALL PrinterSaveFileCallback(void *userdata, const char *const *filelist, int filter) {
	(void)userdata;
	(void)filter;
	if (!filelist || !filelist[0])
		return;

	s_pendingSavePath = filelist[0];
	s_hasPendingSave = true;
}

}  // namespace

// =========================================================================
// Printer output pane
// =========================================================================

class ATImGuiPrinterOutputPaneImpl final : public ATImGuiDebuggerPane {
public:
	ATImGuiPrinterOutputPaneImpl();
	~ATImGuiPrinterOutputPaneImpl() override;

	bool Render() override;

private:
	void RefreshOutputList();
	void AttachToAnyOutput();
	void AttachToTextOutput(int index);
	void AttachToGraphicalOutput(int index);
	void DetachFromOutput();
	void UpdateTextBuffer();

	// Graphical rendering
	void RenderGraphicalOutput();
	void UpdateGraphicalTexture(uint32 w, uint32 h);
	void DestroyGraphicalTexture();
	void *GetGraphicalImTextureID() const;

	// Export
	void RenderToFramebuffer(float dpi, std::vector<uint32> &fb, int &outW, int &outH);
	void ProcessPendingSave();

	// Current output tracking
	int mCurrentOutputIdx = -1;
	ATPrinterOutput *mpCurrentOutput = nullptr;
	ATPrinterGraphicalOutput *mpCurrentGfxOutput = nullptr;
	size_t mLastTextOffset = 0;
	std::string mTextBuffer;		// UTF-8 converted text for ImGui display
	bool mbNeedsScroll = false;

	// Graphical view state
	float mViewCenterX = 0;		// document-space center (mm)
	float mViewCenterY = 0;
	float mZoomClicks = 0;
	float mViewPixelsPerMM = 96.0f / 25.4f;
	float mViewMMPerPixel = 25.4f / 96.0f;
	float mDotRadiusMM = 0;
	float mPageWidthMM = 0;
	float mPageVBorderMM = 0;
	float mViewCursorY = 0;		// print head Y position (mm)

	bool mbDragging = false;
	float mDragLastX = 0;
	float mDragLastY = 0;

	static constexpr float kZoomMin = -10.0f;
	static constexpr float kZoomMax = 25.0f;

	// Texture for graphical output
	GLuint mGLTexture = 0;
	SDL_Texture *mpSDLTexture = nullptr;
	int mTexW = 0;
	int mTexH = 0;
	std::vector<uint32> mFramebuffer;

	// Invalidation tracking
	bool mbGfxInvalidated = true;
	vdfunction<void()> mGfxOnInvalidation;

	// Invalidation callback for text output
	vdfunction<void()> mOnInvalidation;

	// Output list (rebuilt on open and on events)
	struct OutputInfo {
		VDStringA mName;
		bool mbIsGraphical;
		int mIndex;				// index into text or graphical output list
	};
	std::vector<OutputInfo> mOutputList;
	bool mbOutputListDirty = true;

	// Event subscriptions for text outputs
	vdfunction<void(ATPrinterOutput&)> mOnAddedOutput;
	vdfunction<void(ATPrinterOutput&)> mOnRemovingOutput;

	// Event subscriptions for graphical outputs
	vdfunction<void(ATPrinterGraphicalOutput&)> mOnAddedGfxOutput;
	vdfunction<void(ATPrinterGraphicalOutput&)> mOnRemovingGfxOutput;
};

ATImGuiPrinterOutputPaneImpl::ATImGuiPrinterOutputPaneImpl()
	: ATImGuiDebuggerPane(kATUIPaneId_PrinterOutput, "Printer Output")
{
	auto& mgr = static_cast<ATPrinterOutputManager&>(g_sim.GetPrinterOutputManager());

	// Text output events — match Windows auto-attach behavior
	mOnAddedOutput = [this](ATPrinterOutput&) {
		mbOutputListDirty = true;
		// Auto-attach if nothing is currently attached
		if (!mpCurrentOutput && !mpCurrentGfxOutput)
			AttachToAnyOutput();
	};
	mOnRemovingOutput = [this](ATPrinterOutput& output) {
		if (mpCurrentOutput == &output) {
			DetachFromOutput();
			AttachToAnyOutput();	// re-attach to next available
		}
		mbOutputListDirty = true;
	};

	// Graphical output events — Windows always tries to attach graphical
	mOnAddedGfxOutput = [this](ATPrinterGraphicalOutput&) {
		mbOutputListDirty = true;
		// Auto-attach graphical even if text is already attached (Windows behavior)
		if (!mpCurrentGfxOutput)
			AttachToAnyOutput();
	};
	mOnRemovingGfxOutput = [this](ATPrinterGraphicalOutput& output) {
		if (mpCurrentGfxOutput == &output) {
			DetachFromOutput();
			AttachToAnyOutput();	// re-attach to next available
		}
		mbOutputListDirty = true;
	};

	mgr.OnAddedOutput.Add(&mOnAddedOutput);
	mgr.OnRemovingOutput.Add(&mOnRemovingOutput);
	mgr.OnAddedGraphicalOutput.Add(&mOnAddedGfxOutput);
	mgr.OnRemovingGraphicalOutput.Add(&mOnRemovingGfxOutput);

	// Auto-attach to first available output
	AttachToAnyOutput();
}

ATImGuiPrinterOutputPaneImpl::~ATImGuiPrinterOutputPaneImpl() {
	DetachFromOutput();
	DestroyGraphicalTexture();

	auto& mgr = static_cast<ATPrinterOutputManager&>(g_sim.GetPrinterOutputManager());
	mgr.OnAddedOutput.Remove(&mOnAddedOutput);
	mgr.OnRemovingOutput.Remove(&mOnRemovingOutput);
	mgr.OnAddedGraphicalOutput.Remove(&mOnAddedGfxOutput);
	mgr.OnRemovingGraphicalOutput.Remove(&mOnRemovingGfxOutput);
}

void ATImGuiPrinterOutputPaneImpl::RefreshOutputList() {
	mbOutputListDirty = false;
	mOutputList.clear();

	auto& mgr = static_cast<ATPrinterOutputManager&>(g_sim.GetPrinterOutputManager());

	uint32 textCount = mgr.GetOutputCount();
	for (uint32 i = 0; i < textCount; ++i) {
		ATPrinterOutput& out = mgr.GetOutput(i);
		OutputInfo info;
		info.mName = VDTextWToU8(VDStringW(out.GetName()));
		info.mbIsGraphical = false;
		info.mIndex = (int)i;
		mOutputList.push_back(std::move(info));
	}

	uint32 gfxCount = mgr.GetGraphicalOutputCount();
	for (uint32 i = 0; i < gfxCount; ++i) {
		ATPrinterGraphicalOutput& out = mgr.GetGraphicalOutput(i);
		OutputInfo info;
		info.mName = VDTextWToU8(VDStringW(out.GetName()));
		info.mName += " (graphical)";
		info.mbIsGraphical = true;
		info.mIndex = (int)i;
		mOutputList.push_back(std::move(info));
	}
}

void ATImGuiPrinterOutputPaneImpl::AttachToAnyOutput() {
	// Match Windows AttachToAnyOutput(): prefer graphical, then text.
	RefreshOutputList();
	for (int i = 0; i < (int)mOutputList.size(); ++i) {
		if (mOutputList[i].mbIsGraphical) {
			AttachToGraphicalOutput(mOutputList[i].mIndex);
			mCurrentOutputIdx = i;
			return;
		}
	}
	for (int i = 0; i < (int)mOutputList.size(); ++i) {
		if (!mOutputList[i].mbIsGraphical) {
			AttachToTextOutput(mOutputList[i].mIndex);
			mCurrentOutputIdx = i;
			return;
		}
	}
}

void ATImGuiPrinterOutputPaneImpl::AttachToTextOutput(int index) {
	DetachFromOutput();

	auto& mgr = static_cast<ATPrinterOutputManager&>(g_sim.GetPrinterOutputManager());
	if (index < 0 || index >= (int)mgr.GetOutputCount())
		return;

	mpCurrentOutput = &mgr.GetOutput(index);
	mLastTextOffset = 0;
	mTextBuffer.clear();

	mOnInvalidation = [this]() {
		// Text was added — will be picked up next frame
	};
	mpCurrentOutput->SetOnInvalidation(mOnInvalidation);

	UpdateTextBuffer();
}

void ATImGuiPrinterOutputPaneImpl::AttachToGraphicalOutput(int index) {
	DetachFromOutput();

	auto& mgr = static_cast<ATPrinterOutputManager&>(g_sim.GetPrinterOutputManager());
	if (index < 0 || index >= (int)mgr.GetGraphicalOutputCount())
		return;

	mpCurrentGfxOutput = &mgr.GetGraphicalOutput(index);

	const ATPrinterGraphicsSpec& spec = mpCurrentGfxOutput->GetGraphicsSpec();
	mPageWidthMM = spec.mPageWidthMM;
	mPageVBorderMM = spec.mPageVBorderMM;
	mDotRadiusMM = spec.mDotRadiusMM;

	// Set up invalidation callback
	mGfxOnInvalidation = [this]() {
		mbGfxInvalidated = true;
	};
	mpCurrentGfxOutput->SetOnInvalidation(mGfxOnInvalidation);

	// Track print head position
	mpCurrentGfxOutput->SetOnVerticalMove([this](float y) {
		mViewCursorY = y;
		mbGfxInvalidated = true;
	});

	// Reset view — match Windows ResetView() which sets zoom to 0 (base DPI)
	mZoomClicks = 0;
	float basePixelsPerMM = 96.0f / 25.4f;
	mViewPixelsPerMM = basePixelsPerMM * powf(2.0f, mZoomClicks / 5.0f);
	mViewMMPerPixel = 1.0f / mViewPixelsPerMM;
	mViewCenterX = mPageWidthMM * 0.5f;
	mViewCenterY = mPageVBorderMM;
	mViewCursorY = (float)mpCurrentGfxOutput->GetVerticalPos();
	mbGfxInvalidated = true;
}

void ATImGuiPrinterOutputPaneImpl::DetachFromOutput() {
	if (mpCurrentOutput) {
		mpCurrentOutput->SetOnInvalidation(vdfunction<void()>());
		mpCurrentOutput = nullptr;
	}
	if (mpCurrentGfxOutput) {
		mpCurrentGfxOutput->SetOnInvalidation(vdfunction<void()>());
		mpCurrentGfxOutput->SetOnVerticalMove(vdfunction<void(float)>());
		mpCurrentGfxOutput = nullptr;
	}
	mCurrentOutputIdx = -1;
	mbDragging = false;
}

void ATImGuiPrinterOutputPaneImpl::UpdateTextBuffer() {
	if (!mpCurrentOutput)
		return;

	size_t len = mpCurrentOutput->GetLength();
	if (len > mLastTextOffset) {
		const wchar_t *text = mpCurrentOutput->GetTextPointer(mLastTextOffset);
		size_t newChars = len - mLastTextOffset;

		VDStringW wstr(text, newChars);
		VDStringA utf8 = VDTextWToU8(wstr);
		mTextBuffer.append(utf8.c_str(), utf8.size());

		mLastTextOffset = len;
		mbNeedsScroll = true;
	}

	mpCurrentOutput->Revalidate();
}

void ATImGuiPrinterOutputPaneImpl::DestroyGraphicalTexture() {
	if (mGLTexture) {
		glDeleteTextures(1, &mGLTexture);
		mGLTexture = 0;
	}
	if (mpSDLTexture) {
		SDL_DestroyTexture(mpSDLTexture);
		mpSDLTexture = nullptr;
	}
	mTexW = 0;
	mTexH = 0;
}

void *ATImGuiPrinterOutputPaneImpl::GetGraphicalImTextureID() const {
	IDisplayBackend *backend = ATUIGetDisplayBackend();
	if (backend && backend->GetType() == DisplayBackendType::OpenGL)
		return (void *)(intptr_t)mGLTexture;
	return (void *)(intptr_t)mpSDLTexture;
}

void ATImGuiPrinterOutputPaneImpl::UpdateGraphicalTexture(uint32 w, uint32 h) {
	if (w == 0 || h == 0)
		return;

	// Render to framebuffer
	mFramebuffer.resize(w * h);

	PrinterViewTransform vt;
	vt.mOriginX = mViewCenterX - (float)w * 0.5f * mViewMMPerPixel;
	vt.mOriginY = mViewCenterY - (float)h * 0.5f * mViewMMPerPixel;
	vt.mPixelsPerMM = mViewPixelsPerMM;
	vt.mMMPerPixel = mViewMMPerPixel;

	RenderPrinterOutput(*mpCurrentGfxOutput, vt, mFramebuffer.data(), w, h,
		mDotRadiusMM, mPageWidthMM);

	// Drain the invalidation state so future Invalidate() calls will
	// re-trigger the callback.  Without this, mbInvalidated stays true
	// permanently and the callback never fires again after the first render.
	{
		bool all = false;
		vdrect32f r;
		mpCurrentGfxOutput->ExtractInvalidationRect(all, r);
	}

	// Draw print head cursor — gray triangle on left edge (matching Windows)
	{
		float cursorPixelY = (mViewCursorY - vt.mOriginY) * vt.mPixelsPerMM;
		int cy = (int)cursorPixelY;
		if (cy >= -10 && cy < (int)h + 10) {
			for (int dy = -5; dy <= 5; ++dy) {
				int py = cy + dy;
				if (py < 0 || py >= (int)h) continue;
				int triWidth = 6 - abs(dy);
				for (int dx = 0; dx < triWidth && dx < (int)w; ++dx)
					mFramebuffer[py * w + dx] = 0xFF808080;
			}
		}
	}

	// Upload to texture
	IDisplayBackend *backend = ATUIGetDisplayBackend();
	bool useGL = backend && backend->GetType() == DisplayBackendType::OpenGL;

	if (useGL) {
		// Printer output framebuffer is XRGB8888 (see RenderPrinterOutput
		// + the cursor draw above writing 0xFF808080); route through the
		// per-profile XRGB8888 helper.
		if (!mGLTexture || mTexW != (int)w || mTexH != (int)h) {
			if (mGLTexture)
				glDeleteTextures(1, &mGLTexture);

			mTexW = (int)w;
			mTexH = (int)h;
			mGLTexture = GLCreateXRGB8888Texture(mTexW, mTexH, true,
				mFramebuffer.data());
		} else {
			glBindTexture(GL_TEXTURE_2D, mGLTexture);
			GLUploadXRGB8888(mTexW, mTexH, mFramebuffer.data(), 0);
		}
	} else {
		if (!mpSDLTexture || mTexW != (int)w || mTexH != (int)h) {
			if (mpSDLTexture)
				SDL_DestroyTexture(mpSDLTexture);

			SDL_Renderer *renderer = SDL_GetRenderer(g_pWindow);
			if (!renderer) return;

			mTexW = (int)w;
			mTexH = (int)h;
			mpSDLTexture = SDL_CreateTexture(renderer,
				SDL_PIXELFORMAT_ARGB8888,
				SDL_TEXTUREACCESS_STREAMING,
				mTexW, mTexH);
			if (!mpSDLTexture) return;
		}

		void *pixels = nullptr;
		int pitch = 0;
		if (SDL_LockTexture(mpSDLTexture, nullptr, &pixels, &pitch)) {
			for (uint32 y = 0; y < h; ++y)
				memcpy((uint8 *)pixels + y * pitch, &mFramebuffer[y * w], w * 4);
			SDL_UnlockTexture(mpSDLTexture);
		}
	}

	mbGfxInvalidated = false;
}

void ATImGuiPrinterOutputPaneImpl::RenderToFramebuffer(float dpi, std::vector<uint32> &fb, int &outW, int &outH) {
	if (!mpCurrentGfxOutput) {
		outW = outH = 0;
		return;
	}

	vdrect32f docBounds = mpCurrentGfxOutput->GetDocumentBounds();

	// Ensure minimum size
	if (docBounds.width() < 1.0f) docBounds.right = docBounds.left + 10.0f;
	if (docBounds.height() < 1.0f) docBounds.bottom = docBounds.top + 10.0f;

	const float mmToInches = 1.0f / 25.4f;
	outW = std::max(1, (int)ceilf(docBounds.width() * mmToInches * dpi));
	outH = std::max(1, (int)ceilf(docBounds.height() * mmToInches * dpi));

	// Limit to reasonable size to prevent OOM
	if ((int64_t)outW * outH > 64 * 1024 * 1024) {
		float scale = sqrtf(64.0f * 1024 * 1024 / ((float)outW * outH));
		outW = (int)(outW * scale);
		outH = (int)(outH * scale);
	}

	fb.resize(outW * outH);

	PrinterViewTransform vt;
	vt.mOriginX = docBounds.left;
	vt.mOriginY = docBounds.top;
	vt.mPixelsPerMM = mmToInches * dpi;
	vt.mMMPerPixel = 1.0f / vt.mPixelsPerMM;

	RenderPrinterOutput(*mpCurrentGfxOutput, vt, fb.data(), outW, outH,
		mDotRadiusMM, mPageWidthMM);
}

void ATImGuiPrinterOutputPaneImpl::ProcessPendingSave() {
	if (!s_hasPendingSave || !mpCurrentGfxOutput)
		return;

	s_hasPendingSave = false;
	const std::string path = s_pendingSavePath;

	switch (s_pendingSaveRequest.mFormat) {
		case PrinterSaveRequest::Format::PNG96: {
			std::vector<uint32> fb;
			int w, h;
			RenderToFramebuffer(96.0f, fb, w, h);
			if (w > 0 && h > 0)
				SaveFramebufferAsPNG(fb.data(), w, h, path.c_str());
			break;
		}
		case PrinterSaveRequest::Format::PNG300: {
			std::vector<uint32> fb;
			int w, h;
			RenderToFramebuffer(300.0f, fb, w, h);
			if (w > 0 && h > 0)
				SaveFramebufferAsPNG(fb.data(), w, h, path.c_str());
			break;
		}
		case PrinterSaveRequest::Format::PDF: {
			std::vector<uint32> fb;
			int w, h;
			RenderToFramebuffer(300.0f, fb, w, h);
			if (w > 0 && h > 0) {
				vdrect32f bounds = mpCurrentGfxOutput->GetDocumentBounds();
				float docW = std::max(10.0f, bounds.width());
				float docH = std::max(10.0f, bounds.height());
				SaveFramebufferAsPDF(fb.data(), w, h, docW, docH, path.c_str());
			}
			break;
		}
		case PrinterSaveRequest::Format::SVG:
			SavePrinterOutputAsSVG(*mpCurrentGfxOutput, path.c_str());
			break;
	}
}

void ATImGuiPrinterOutputPaneImpl::RenderGraphicalOutput() {
	if (!mpCurrentGfxOutput)
		return;

	// Process any pending save requests
	ProcessPendingSave();

	// Release drag if window lost focus
	if (mbDragging && !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
		mbDragging = false;

	// Get available region early — needed by both context menu and rendering
	ImVec2 avail = ImGui::GetContentRegionAvail();
	int viewW = std::max(1, (int)avail.x);
	int viewH = std::max(1, (int)avail.y);

	// Context menu (right-click)
	if (ImGui::BeginPopupContextWindow("PrinterGfxContext")) {
		if (ImGui::MenuItem("Clear")) {
			mpCurrentGfxOutput->Clear();
			mViewCenterY = 0;
			mbGfxInvalidated = true;
		}

		if (ImGui::BeginMenu("Save As")) {
			auto startSave = [&](PrinterSaveRequest::Format fmt, const char *filterName, const char *ext) {
				s_pendingSaveRequest.mFormat = fmt;

				SDL_DialogFileFilter filter;
				filter.name = filterName;
				filter.pattern = ext;
				ATUIShowSaveFileDialog('prnt', PrinterSaveFileCallback, nullptr,
					g_pWindow, &filter, 1);
			};

			if (ImGui::MenuItem("PNG Image (96 DPI)"))
				startSave(PrinterSaveRequest::Format::PNG96, "PNG Image (*.png)", "png");
			if (ImGui::MenuItem("PNG Image (300 DPI)"))
				startSave(PrinterSaveRequest::Format::PNG300, "PNG Image (*.png)", "png");
			if (ImGui::MenuItem("PDF Document"))
				startSave(PrinterSaveRequest::Format::PDF, "PDF Document (*.pdf)", "pdf");
			if (ImGui::MenuItem("SVG 1.1 Document"))
				startSave(PrinterSaveRequest::Format::SVG, "SVG Document (*.svg)", "svg");

			ImGui::EndMenu();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Reset View")) {
			mZoomClicks = 0;
			float basePixelsPerMM = 96.0f / 25.4f;
			mViewPixelsPerMM = basePixelsPerMM * powf(2.0f, mZoomClicks / 5.0f);
			mViewMMPerPixel = 1.0f / mViewPixelsPerMM;
			mViewCenterX = mPageWidthMM * 0.5f;
			mViewCenterY = mPageVBorderMM;
			mbGfxInvalidated = true;
		}

		if (ImGui::MenuItem("Set Print Position")) {
			// Windows behaviour: set the print head to the Y position where the
			// user right-clicked.  Convert the right-click screen position to
			// document space.
			ImVec2 clickPos = ImGui::GetIO().MouseClickedPos[1];

			// The image occupies the content region below the toolbar/separator.
			// Its screen-space top = window bottom - viewH (content region height).
			ImVec2 winPos = ImGui::GetWindowPos();
			ImVec2 winSize = ImGui::GetWindowSize();
			float contentTopY = winPos.y + winSize.y - (float)viewH;
			float relY = clickPos.y - contentTopY;

			float docY = mViewCenterY + (relY - viewH * 0.5f) * mViewMMPerPixel;
			docY = std::max(0.0f, docY);

			mpCurrentGfxOutput->SetVerticalPos(docY);
			mViewCursorY = docY;
			mbGfxInvalidated = true;
		}

		ImGui::EndPopup();
	}

	// Handle zoom (mouse wheel)
	if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
		float wheel = ImGui::GetIO().MouseWheel;
		if (wheel != 0.0f) {
			float newZoom = std::clamp(mZoomClicks + wheel, kZoomMin, kZoomMax);
			if (newZoom != mZoomClicks) {
				// Zoom centered on mouse cursor position
				ImVec2 mousePos = ImGui::GetMousePos();
				ImVec2 windowPos = ImGui::GetCursorScreenPos();
				float relX = mousePos.x - windowPos.x;
				float relY = mousePos.y - windowPos.y;

				// Convert mouse position to document space before zoom
				float docMouseX = mViewCenterX + (relX - viewW * 0.5f) * mViewMMPerPixel;
				float docMouseY = mViewCenterY + (relY - viewH * 0.5f) * mViewMMPerPixel;

				mZoomClicks = newZoom;
				float basePixelsPerMM = 96.0f / 25.4f;
				mViewPixelsPerMM = basePixelsPerMM * powf(2.0f, mZoomClicks / 5.0f);
				mViewMMPerPixel = 1.0f / mViewPixelsPerMM;

				// Adjust center so the point under the mouse stays put
				mViewCenterX = docMouseX - (relX - viewW * 0.5f) * mViewMMPerPixel;
				mViewCenterY = docMouseY - (relY - viewH * 0.5f) * mViewMMPerPixel;

				mbGfxInvalidated = true;
			}
		}
	}

	// Handle pan (left-click drag)
	if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
		ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
		!ImGui::GetIO().KeyCtrl) {
		mbDragging = true;
		mDragLastX = ImGui::GetMousePos().x;
		mDragLastY = ImGui::GetMousePos().y;
	}

	if (mbDragging) {
		if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			float dx = ImGui::GetMousePos().x - mDragLastX;
			float dy = ImGui::GetMousePos().y - mDragLastY;
			if (dx != 0.0f || dy != 0.0f) {
				mViewCenterX -= dx * mViewMMPerPixel;
				mViewCenterY -= dy * mViewMMPerPixel;
				mDragLastX = ImGui::GetMousePos().x;
				mDragLastY = ImGui::GetMousePos().y;
				mbGfxInvalidated = true;
			}
		} else {
			mbDragging = false;
		}
	}

	// Keyboard navigation (matching Windows: arrows ±100/±1, PgUp/PgDn, +/-)
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
		!ImGui::GetIO().WantTextInput) {
		float scrollStep = ImGui::GetIO().KeyCtrl ? 1.0f : 100.0f;

		if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
			mViewCenterX -= scrollStep * mViewMMPerPixel;
			mbGfxInvalidated = true;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
			mViewCenterX += scrollStep * mViewMMPerPixel;
			mbGfxInvalidated = true;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
			mViewCenterY -= scrollStep * mViewMMPerPixel;
			mbGfxInvalidated = true;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
			mViewCenterY += scrollStep * mViewMMPerPixel;
			mbGfxInvalidated = true;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) {
			mViewCenterY -= (float)viewH * mViewMMPerPixel;
			mbGfxInvalidated = true;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) {
			mViewCenterY += (float)viewH * mViewMMPerPixel;
			mbGfxInvalidated = true;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_KeypadAdd) ||
			(ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Equal))) {
			mZoomClicks = std::min(mZoomClicks + 1.0f, kZoomMax);
			mViewPixelsPerMM = (96.0f / 25.4f) * powf(2.0f, mZoomClicks / 5.0f);
			mViewMMPerPixel = 1.0f / mViewPixelsPerMM;
			mbGfxInvalidated = true;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract) ||
			(ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Minus))) {
			mZoomClicks = std::max(mZoomClicks - 1.0f, kZoomMin);
			mViewPixelsPerMM = (96.0f / 25.4f) * powf(2.0f, mZoomClicks / 5.0f);
			mViewMMPerPixel = 1.0f / mViewPixelsPerMM;
			mbGfxInvalidated = true;
		}
	}

	// Render and display — also re-render if viewport size changed
	if (mbGfxInvalidated || mTexW != viewW || mTexH != viewH)
		UpdateGraphicalTexture(viewW, viewH);

	void *texID = GetGraphicalImTextureID();
	if (texID) {
		ImVec2 uv1(0, 0);
		ImVec2 uv2((float)viewW / (float)mTexW, (float)viewH / (float)mTexH);
		ImGui::Image((ImTextureID)texID, ImVec2((float)viewW, (float)viewH), uv1, uv2);
	}
}

bool ATImGuiPrinterOutputPaneImpl::Render() {
	bool open = true;

	if (mbFocusRequested) {
		ImGui::SetNextWindowFocus();
		mbFocusRequested = false;
	}

	ImGui::SetNextWindowSize(ImVec2(500, 300), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(mTitle.c_str(), &open)) {
		mbHasFocus = false;
		ImGui::End();
		return open;
	}
	mbHasFocus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

	if (mbOutputListDirty)
		RefreshOutputList();

	// Toolbar: output selector + clear button
	{
		const char *currentName = (mCurrentOutputIdx >= 0 && mCurrentOutputIdx < (int)mOutputList.size())
			? mOutputList[mCurrentOutputIdx].mName.c_str()
			: "(none)";

		ImGui::SetNextItemWidth(200);
		if (ImGui::BeginCombo("##output", currentName)) {
			for (int i = 0; i < (int)mOutputList.size(); ++i) {
				bool selected = (i == mCurrentOutputIdx);
				if (ImGui::Selectable(mOutputList[i].mName.c_str(), selected)) {
					if (i != mCurrentOutputIdx) {
						if (!mOutputList[i].mbIsGraphical) {
							AttachToTextOutput(mOutputList[i].mIndex);
							mCurrentOutputIdx = i;
						} else {
							AttachToGraphicalOutput(mOutputList[i].mIndex);
							mCurrentOutputIdx = i;
						}
					}
				}
			}
			ImGui::EndCombo();
		}

		ImGui::SameLine();
		if (ImGui::Button("Clear")) {
			if (mpCurrentOutput) {
				mpCurrentOutput->Clear();
				mTextBuffer.clear();
				mLastTextOffset = 0;
			}
			if (mpCurrentGfxOutput) {
				mpCurrentGfxOutput->Clear();
				mViewCenterY = 0;
				mbGfxInvalidated = true;
			}
		}
	}

	ImGui::Separator();

	// Check if current selection is graphical
	bool isGraphical = (mCurrentOutputIdx >= 0 && mCurrentOutputIdx < (int)mOutputList.size()
		&& mOutputList[mCurrentOutputIdx].mbIsGraphical);

	if (isGraphical) {
		RenderGraphicalOutput();
	} else if (mpCurrentOutput) {
		UpdateTextBuffer();

		if (ImGui::BeginChild("PrinterText", ImVec2(0, 0), ImGuiChildFlags_None,
				ImGuiWindowFlags_HorizontalScrollbar)) {
			if (!mTextBuffer.empty())
				ImGui::TextUnformatted(mTextBuffer.c_str(), mTextBuffer.c_str() + mTextBuffer.size());

			if (mbNeedsScroll) {
				ImGui::SetScrollHereY(1.0f);
				mbNeedsScroll = false;
			}
		}
		ImGui::EndChild();
	} else {
		ImGui::TextDisabled("(no printer output available)");
	}

	// Escape → focus Console (matches Windows pattern)
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
			&& !ImGui::GetIO().WantTextInput
			&& ImGui::IsKeyPressed(ImGuiKey_Escape))
		ATUIDebuggerFocusConsole();

	ImGui::End();
	return open;
}

// =========================================================================
// Registration
// =========================================================================

void ATUIDebuggerEnsurePrinterOutputPane() {
	if (!ATUIDebuggerGetPane(kATUIPaneId_PrinterOutput)) {
		auto *pane = new ATImGuiPrinterOutputPaneImpl();
		ATUIDebuggerRegisterPane(pane);
	}
}
