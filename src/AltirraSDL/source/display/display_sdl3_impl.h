//	AltirraSDL - VDVideoDisplaySDL3 full class definition
//	Include this header in files that need the SDL display adapter.

#pragma once
#include <SDL3/SDL.h>
#include "video_output.h"
#include "display_backend.h"
#include <vd2/system/vdtypes.h>
#include <vd2/system/refcount.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/atomic.h>
#include <vector>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/VDDisplay/display.h>
#include <vd2/VDDisplay/displaytypes.h>

class VDVideoDisplaySDL3 final : public IVDVideoDisplay {
public:
	VDVideoDisplaySDL3(SDL_Window *window, int w, int h);
	~VDVideoDisplaySDL3();

	// IVDVideoDisplay
	void Destroy() override;
	void Reset() override;
	void SetSourceMessage(const wchar_t*) override {}
	bool SetSource(bool, const VDPixmap&, bool) override { return false; }
	bool SetSourcePersistent(bool bPersistent, const VDPixmap& px, bool
		, const VDVideoDisplayScreenFXInfo* pScreenFX
		, IVDVideoDisplayScreenFXEngine*) override;

	void SetSourceSubrect(const vdrect32*) override {}
	void SetSourceSolidColor(uint32) override {}
	void SetReturnFocus(bool) override {}
	void SetTouchEnabled(bool) override {}
	void SetUse16Bit(bool) override {}
	void SetHDREnabled(bool) override {}
	void SetFullScreen(bool, uint32, uint32, uint32) override {}
	void SetCustomDesiredRefreshRate(float, float, float) override {}
	void SetDestRect(const vdrect32*, uint32) override {}
	void SetDestRectF(const vdrect32f*, uint32) override {}
	void SetPixelSharpness(float, float) override {}
	void SetCompositor(IVDDisplayCompositor*) override {}
	void SetSDRBrightness(float) override {}

	void PostBuffer(VDVideoDisplayFrame *frame) override;
	bool RevokeBuffer(bool allowFrameSkip, VDVideoDisplayFrame **ppFrame) override;
	void FlushBuffers() override;

	void Invalidate() override {}
	void Update(int) override {}
	void Cache() override {}
	void SetCallback(IVDVideoDisplayCallback*) override {}
	void SetOnFrameStatusUpdated(vdfunction<void(int)>) override {}
	void SetAccelerationMode(AccelerationMode) override {}
	FilterMode GetFilterMode() override { return kFilterBilinear; }
	void SetFilterMode(FilterMode) override {}
	float GetSyncDelta() const override { return 0; }
	int GetQueuedFrames() const override { return mPendingFrame ? 1 : 0; }
	bool IsFramePending() const override { return mPendingFrame != nullptr; }
	VDDVSyncStatus GetVSyncStatus() const override { return {}; }
	vdrect32 GetMonitorRect() override;
	VDDMonitorInfo GetMonitorInformation() override { return {}; }
	bool IsScreenFXPreferred() const override { return mbScreenFXPreferred; }
	VDDHighColorAvailability GetHDRCapability() const override {
		auto *backend = ATUIGetDisplayBackend();
		return backend && backend->IsHDREnabled()
			? VDDHighColorAvailability::Available
			: backend && backend->SupportsHDR()
				? VDDHighColorAvailability::NotEnabledOnDisplay
				: VDDHighColorAvailability::NoMinidriverSupport;
	}
	VDDHighColorAvailability GetWCGCapability() const override {
		return VDDHighColorAvailability::NoMinidriverSupport;
	}
	bool MapNormSourcePtToDest(vdfloat2&) const override { return false; }
	bool MapNormDestPtToSource(vdfloat2&) const override { return false; }
	void SetProfileHook(const vdfunction<void(ProfileEvent, uintptr)>&) override {}
	void RequestCapture(vdfunction<void(const VDPixmap*)>) override {}

	// Convert a pending core frame into backend-ready XRGB8888 pixels.
	// Texture upload and presentation are owned by IDisplayBackend.
	bool PrepareFrame();

	// Expose converted pixel data for the selected display backend.
	const void *GetFramePixels() const {
		const auto *alt = ATSDLGetVideoOutputFrame();
		return alt ? alt->data
			: (mHasFramePixels ? mConvertBuffer.data() : nullptr);
	}
	int GetFramePixelWidth() const {
		const auto *alt = ATSDLGetVideoOutputFrame();
		return alt ? alt->w : mFrameW;
	}
	int GetFramePixelHeight() const {
		const auto *alt = ATSDLGetVideoOutputFrame();
		return alt ? alt->h : mFrameH;
	}
	int GetFramePixelPitch() const {
		const auto *alt = ATSDLGetVideoOutputFrame();
		return alt ? (int)alt->pitch : mFrameW * 4;
	}

	// GL path: screen FX info from GTIA's SetSourcePersistent call
	bool HasScreenFX() const { return mHasScreenFX && !ATSDLGetActiveVideoOutput(); }
	const VDVideoDisplayScreenFXInfo &GetLastScreenFX() const { return mLastScreenFX; }

	// Set by the main loop based on whether the GL backend is active
	void SetScreenFXPreferred(bool v) { mbScreenFXPreferred = v; }

private:
	SDL_Window *mpWindow;

	VDVideoDisplayFrame *mPendingFrame = nullptr;
	VDVideoDisplayFrame *mPrevFrame    = nullptr;

	int mWidth;
	int mHeight;
	int mFrameW = 0;
	int mFrameH = 0;

	bool mbScreenFXPreferred = false;
	bool mHasScreenFX = false;
	bool mHasFramePixels = false;
	VDVideoDisplayScreenFXInfo mLastScreenFX {};

	std::vector<uint32> mConvertBuffer;
};
