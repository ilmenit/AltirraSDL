#include <stdafx.h>

#include "libretro_video.h"

#include "gtia.h"
#include "simulator.h"
#include <vd2/VDDisplay/display.h>
#include <vd2/Kasumi/pixmapops.h>

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

class ATLibretroNullVideoDisplay final : public IVDVideoDisplay {
public:
	bool ConsumeFramePosted() {
		const bool posted = mFramePosted;
		mFramePosted = false;
		return posted;
	}

	void Destroy() override { FlushBuffers(); delete this; }
	void Reset() override { FlushBuffers(); }
	void SetSourceMessage(const wchar_t*) override {}
	bool SetSource(bool, const VDPixmap&, bool) override { return false; }
	bool SetSourcePersistent(bool, const VDPixmap&, bool,
		const VDVideoDisplayScreenFXInfo*,
		IVDVideoDisplayScreenFXEngine*) override { return true; }
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

	void PostBuffer(VDVideoDisplayFrame *frame) override {
		if (!frame)
			return;

		mFramePosted = true;

		if (mPending) {
			if (mPrev)
				mPrev->Release();
			mPrev = mPending;
		}

		frame->AddRef();
		mPending = frame;
	}

	bool RevokeBuffer(bool allowFrameSkip, VDVideoDisplayFrame **ppFrame) override {
		if (mPrev) {
			*ppFrame = mPrev;
			mPrev = nullptr;
			return true;
		}

		if (allowFrameSkip && mPending) {
			*ppFrame = mPending;
			mPending = nullptr;
			return true;
		}

		return false;
	}

	void FlushBuffers() override {
		if (mPending) {
			mPending->Release();
			mPending = nullptr;
		}

		if (mPrev) {
			mPrev->Release();
			mPrev = nullptr;
		}
	}

	void Invalidate() override {}
	void Update(int) override {}
	void Cache() override {}
	void SetCallback(IVDVideoDisplayCallback*) override {}
	void SetOnFrameStatusUpdated(vdfunction<void(int)>) override {}
	void SetAccelerationMode(AccelerationMode) override {}
	FilterMode GetFilterMode() override { return kFilterBilinear; }
	void SetFilterMode(FilterMode) override {}
	float GetSyncDelta() const override { return 0.0f; }
	int GetQueuedFrames() const override { return mPending ? 1 : 0; }
	bool IsFramePending() const override { return mPending != nullptr; }
	VDDVSyncStatus GetVSyncStatus() const override { return {}; }
	vdrect32 GetMonitorRect() override { return vdrect32(0, 0, 640, 480); }
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

private:
	VDVideoDisplayFrame *mPending = nullptr;
	VDVideoDisplayFrame *mPrev = nullptr;
	bool mFramePosted = false;
};

IVDVideoDisplay *ATLibretroCreateNullVideoDisplay() {
	return new ATLibretroNullVideoDisplay;
}

bool ATLibretroNullVideoDisplayConsumeFramePosted(IVDVideoDisplay *display) {
	return static_cast<ATLibretroNullVideoDisplay *>(display)->ConsumeFramePosted();
}

bool ATLibretroCaptureXrgb(ATSimulator& sim, VDPixmapBuffer& dst) {
	VDPixmapBuffer srcBuf;
	VDPixmap src;

	if (!sim.GetGTIA().GetLastFrameBuffer(srcBuf, src))
		return false;

	if (src.w <= 0 || src.h <= 0)
		return false;

	dst.init(src.w, src.h, nsVDPixmap::kPixFormat_XRGB8888);
	VDPixmapBlt(dst, src);
	return true;
}
