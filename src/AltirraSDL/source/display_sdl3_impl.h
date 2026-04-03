//	AltirraSDL - VDVideoDisplaySDL3 full class definition
//	Include this header in files that need to call Present() or new/delete.

#pragma once
#include <SDL3/SDL.h>
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
	VDVideoDisplaySDL3(SDL_Renderer *renderer, int w, int h);
	~VDVideoDisplaySDL3();

	// IVDVideoDisplay
	void Destroy() override;
	void Reset() override;
	void SetSourceMessage(const wchar_t*) override {}
	bool SetSource(bool, const VDPixmap&, bool) override { return false; }
	bool SetSourcePersistent(bool, const VDPixmap&, bool,
		const VDVideoDisplayScreenFXInfo*, IVDVideoDisplayScreenFXEngine*) override { return false; }
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
	vdrect32 GetMonitorRect() override { return {0, 0, mWidth, mHeight}; }
	VDDMonitorInfo GetMonitorInformation() override { return {}; }
	bool IsScreenFXPreferred() const override { return false; }
	VDDHighColorAvailability GetHDRCapability() const override {
		return VDDHighColorAvailability::NoMinidriverSupport;
	}
	VDDHighColorAvailability GetWCGCapability() const override {
		return VDDHighColorAvailability::NoMinidriverSupport;
	}
	bool MapNormSourcePtToDest(vdfloat2&) const override { return false; }
	bool MapNormDestPtToSource(vdfloat2&) const override { return false; }
	void SetProfileHook(const vdfunction<void(ProfileEvent, uintptr)>&) override {}
	void RequestCapture(vdfunction<void(const VDPixmap*)>) override {}

	// Upload pending frame pixels to the SDL texture (does NOT present).
	// Returns true if the texture has valid content to render.
	bool PrepareFrame();

	// Legacy: upload + clear + render + present (used before ImGui integration)
	void Present();

	// Update SDL_SCALEMODE on the existing texture to match the current
	// ATDisplayFilterMode setting.  Called when the user changes filter mode.
	void UpdateScaleMode();

	SDL_Texture* GetTexture() const { return mpTexture; }
	int GetTextureWidth() const { return mTextureW; }
	int GetTextureHeight() const { return mTextureH; }

private:
	SDL_Renderer *mpRenderer;
	SDL_Texture  *mpTexture = nullptr;

	VDVideoDisplayFrame *mPendingFrame = nullptr;
	VDVideoDisplayFrame *mPrevFrame    = nullptr;

	int mWidth;
	int mHeight;
	int mTextureW = 0;
	int mTextureH = 0;

	std::vector<uint32> mConvertBuffer;
};
