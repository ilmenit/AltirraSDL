//	Altirra SDL3 frontend - IVDVideoDisplay implementation

#include <stdafx.h>
#include <vd2/system/atomic.h>
#include <vd2/VDDisplay/display.h>
#include "display_sdl3_impl.h"
#include "uiaccessors.h"
#include "uitypes.h"

// ============================================================
// VDVideoDisplayFrame implementation (from VDDisplay library)
// ============================================================

VDVideoDisplayFrame::VDVideoDisplayFrame() : mRefCount(0) {}
VDVideoDisplayFrame::~VDVideoDisplayFrame() {}

int VDVideoDisplayFrame::AddRef() {
	return mRefCount.inc();
}

int VDVideoDisplayFrame::Release() {
	int n = mRefCount.dec();
	if (!n)
		delete this;
	return n;
}

// VDDSetBloomV2Settings: real implementation now comes from VDDisplay/source/bloom.cpp

VDVideoDisplaySDL3::VDVideoDisplaySDL3(SDL_Window *window, int w, int h)
	: mpWindow(window)
	, mWidth(w)
	, mHeight(h)
{
}

VDVideoDisplaySDL3::~VDVideoDisplaySDL3() {
	FlushBuffers();
}

bool VDVideoDisplaySDL3::SetSourcePersistent(bool bPersistent, const VDPixmap& px, bool
	, const VDVideoDisplayScreenFXInfo* pScreenFX
	, IVDVideoDisplayScreenFXEngine*)
{
	if (pScreenFX) {
		mLastScreenFX = *pScreenFX;
		mHasScreenFX = true;
	} else {
		mHasScreenFX = false;
	}
	// Delegate actual frame handling to PostBuffer path — return false
	// so GTIA falls through to PostBuffer().
	return false;
}

void VDVideoDisplaySDL3::Destroy() {
	delete this;
}

void VDVideoDisplaySDL3::Reset() {
	FlushBuffers();
}

vdrect32 VDVideoDisplaySDL3::GetMonitorRect() {
	int w = mWidth;
	int h = mHeight;

	if (mpWindow)
		SDL_GetWindowSizeInPixels(mpWindow, &w, &h);

	if (w <= 0)
		w = mWidth;
	if (h <= 0)
		h = mHeight;

	return {0, 0, w, h};
}

void VDVideoDisplaySDL3::PostBuffer(VDVideoDisplayFrame *frame) {
	if (!frame) return;

	// Capture screen FX from the frame buffer.  GTIA sets mpScreenFX on
	// the frame during SetFrameProperties() when hardware-accelerated
	// post-processing is enabled (bloom, scanlines, distortion, etc.).
	// SetSourcePersistent only handles the immediate/mid-frame path;
	// normal end-of-frame goes through PostBuffer, so we must capture here.
	if (frame->mpScreenFX) {
		mLastScreenFX = *frame->mpScreenFX;
		mHasScreenFX = true;
	} else {
		mHasScreenFX = false;
	}

	if (mPendingFrame) {
		if (mPrevFrame)
			mPrevFrame->Release();
		mPrevFrame = mPendingFrame;
	}

	frame->AddRef();
	mPendingFrame = frame;
}

bool VDVideoDisplaySDL3::RevokeBuffer(bool allowFrameSkip, VDVideoDisplayFrame **ppFrame) {
	if (mPrevFrame) {
		*ppFrame = mPrevFrame;
		mPrevFrame = nullptr;
		return true;
	}
	if (allowFrameSkip && mPendingFrame) {
		*ppFrame = mPendingFrame;
		mPendingFrame = nullptr;
		return true;
	}
	return false;
}

void VDVideoDisplaySDL3::FlushBuffers() {
	if (mPendingFrame) { mPendingFrame->Release(); mPendingFrame = nullptr; }
	if (mPrevFrame)    { mPrevFrame->Release();    mPrevFrame    = nullptr; }
}

bool VDVideoDisplaySDL3::PrepareFrame() {
	ATSDLVideoOutputUpdate();
	if (!mPendingFrame)
		return mHasFramePixels;

	const VDPixmap& px = mPendingFrame->mPixmap;
	if (!px.data || !px.w || !px.h) {
		// Bad frame — move to mPrevFrame so RevokeBuffer can return it.
		if (mPrevFrame)
			mPrevFrame->Release();
		mPrevFrame = mPendingFrame;
		mPendingFrame = nullptr;
		return mHasFramePixels;
	}

	mFrameW = px.w;
	mFrameH = px.h;

	// Always ensure conversion buffer is large enough
	mConvertBuffer.resize((size_t)px.w * px.h);

	if (px.format == nsVDPixmap::kPixFormat_Pal8 && px.palette) {
		// GTIA outputs Pal8 (palettized 8-bit) — convert to XRGB8888
		const uint32 *pal = px.palette;
		uint32 *dst = mConvertBuffer.data();

		for (int y = 0; y < px.h; y++) {
			const uint8 *src = (const uint8 *)px.data + y * px.pitch;
			uint32 *dstRow = dst + y * px.w;
			for (int x = 0; x < px.w; x++)
				dstRow[x] = pal[src[x]] | 0xFF000000u;
		}

	} else {
		// Copy XRGB8888 data into the backend handoff buffer.
		const int rowBytes = px.w * 4;
		uint32 *dst = mConvertBuffer.data();
		for (int y = 0; y < px.h; y++) {
			memcpy(dst + y * px.w, (const uint8 *)px.data + y * px.pitch, rowBytes);
		}
	}

	mHasFramePixels = true;

	// Move the consumed frame to mPrevFrame so GTIA can reclaim it via
	// RevokeBuffer().  GTIA's BeginFrame() calls RevokeBuffer() to get
	// a reusable frame buffer — if both mPendingFrame and mPrevFrame
	// are null, RevokeBuffer returns false and the pipeline deadlocks
	// once mActiveFrames reaches 3.
	if (mPrevFrame)
		mPrevFrame->Release();
	mPrevFrame = mPendingFrame;
	mPendingFrame = nullptr;
	return true;
}
