//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
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

#ifndef f_VD2_VDDISPLAY_DISPLAY_H
#define f_VD2_VDDISPLAY_DISPLAY_H

#include <vd2/system/function.h>
#include <vd2/system/vectors.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/refcount.h>
#include <vd2/system/atomic.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/VDDisplay/displaytypes.h>

VDGUIHandle VDCreateDisplayWindowW32(uint32 dwExFlags, uint32 dwFlags, int x, int y, int width, int height, VDGUIHandle hwndParent);

class IVDVideoDisplay;
class IVDDisplayCompositor;
class VDPixmapBuffer;
class VDBufferedRandomAccessStream;

struct VDVideoDisplayScreenFXInfo {
	float mScanlineIntensity = 0;
	float mGamma = 0;

	float mPALBlendingOffset = 0;

	float mDistortionX = 0;
	float mDistortionYRatio = 0;

	// Radial vignette intensity (0 = off, 1 = full darkening at corners).
	float mVignetteIntensity = 0;

	bool mbSignedRGBEncoding = false;
	float mHDRIntensity = 0;

	// 0 = sRGB, otherwise explicit gamma (2.2 for Adobe RGB)
	float mOutputGamma = 0;

	float mColorCorrectionMatrix[3][3] {};

	bool mbBloomEnabled = false;

	// If true, the soft clip applied at the end of the bloom process works in
	// display RGB space instead of sRGB/scRGB. This currently can only be used
	// in SDR/WCG scenarios and not with HDR.
	bool mbBloomClipToCustomPrimaries = false;

	float mBloomThreshold = 0;
	float mBloomRadius = 0;
	float mBloomDirectIntensity = 0;
	float mBloomIndirectIntensity = 0;

	VDDScreenMaskParams mScreenMaskParams {};

	bool operator==(const VDVideoDisplayScreenFXInfo&) const = default;
};

struct VDDVSyncStatus {
	// Measured vsync wait offset in terms of seconds to the next vsync. Negative value
	// means not available. When using adaptive vsync, the caller is expected to adapt
	// timing to get this within a stable offset ahead of the vsync, without blowing
	// over to the next vsync.
	float	mOffset = -1.0f;

	// Estimate of delay due to backing up the present queue. May either be a measure of
	// blocking time in Present() or the wait time to Present(), depending on the exact
	// presentation mode used. The general idea is that a frame should be dropped when
	// this exceeds a threshold for a prolonged period of time, in order to reduce
	// latency.
	float	mPresentQueueTime = 0.0f;

	// Measured refresh rate, in Hz; -1 if not available. Generally accurate to three
	// significant figures with some noise in the fourth digit. If real-time refresh rate
	// is not available, this may be the refresh rate from the current display mode.
	float	mRefreshRate = -1.0f;
};

struct VDDVSyncProfileInfo {
	uint64 mQpcTimes[2];
	uint32 mRefreshCounts[2];
};

class IVDVideoDisplayScreenFXEngine {
public:
	// Apply screen FX to an image in software; returns resulting image. The source image buffer must
	// not be modified, and the engine must keep the result image alive as long as the original submitted
	// frame.
	virtual VDPixmap ApplyScreenFX(const VDPixmap& px) = 0;
};

enum class VDDHighColorAvailability : uint8 {
	// These are ordered in ascending capability.
	NoMinidriverSupport,
	NoSystemSupport,
	NoHardwareSupport,
	NoDisplaySupport,
	NotEnabledOnDisplay,
	Available
};

inline auto operator<=>(VDDHighColorAvailability a, VDDHighColorAvailability b) {
	return static_cast<std::underlying_type_t<VDDHighColorAvailability>>(a) <=>
	       static_cast<std::underlying_type_t<VDDHighColorAvailability>>(b);
}

class VDVideoDisplayFrame : public vdlist_node, public IVDRefCount {
public:
	VDVideoDisplayFrame();
	virtual ~VDVideoDisplayFrame();

	virtual int AddRef();
	virtual int Release();

	VDPixmap	mPixmap {};
	uint32		mFlags = 0;
	uint32		mFrameNumber = 0;
	bool		mbAllowConversion = false;

	IVDVideoDisplayScreenFXEngine *mpScreenFXEngine = nullptr;
	const VDVideoDisplayScreenFXInfo *mpScreenFX = nullptr;

protected:
	VDAtomicInt	mRefCount;
};

struct VDDMonitorInfo {
	bool mbValid = false;
	bool mbHDRCapable = false; 
	bool mbHDREnabled = false; 
	bool mbWCGCapable = false;
	bool mbWCGEnabled = false;

	VDStringW mDisplayName;

	vdfloat2 mRedPrimary { 0.0f, 0.0f };
	vdfloat2 mGreenPrimary { 0.0f, 0.0f };
	vdfloat2 mBluePrimary { 0.0f, 0.0f };
	vdfloat2 mWhitePoint { 0.0f, 0.0f };
	float mMaxLuminance = 0;
	float mMaxFullFrameLuminance = 0;
};

class VDINTERFACE IVDVideoDisplayCallback {
public:
	virtual void DisplayRequestUpdate(IVDVideoDisplay *pDisp) = 0;
};

class VDINTERFACE IVDVideoDisplay {
public:
	enum FieldMode : uint32 {
		// Present synchronized to vertical sync to avoid tearing.
		kVSync				= 0x0004,

		// Present synchronized to vertical sync, but do not enforce timing offset relative to vsync. Used when
		// the client is instead adaptively locking to the desired offset. Only effective if kVSync is set.
		kVSyncAdaptive		= 0x0008,

		// Do not store the current frame for refresh purposes.
		kDoNotCache			= 0x0020,

		// If the present operation would block, drop the frame instead of waiting for the present.
		kDoNotWait			= 0x0800,
	};

	enum FilterMode {
		kFilterAnySuitable,
		kFilterPoint,
		kFilterBilinear,
		kFilterBicubic
	};

	virtual void Destroy() = 0;
	virtual void Reset() = 0;
	virtual void SetSourceMessage(const wchar_t *msg) = 0;
	virtual bool SetSource(bool bAutoUpdate, const VDPixmap& src, bool bAllowConversion = true) = 0;
	virtual bool SetSourcePersistent(bool bAutoUpdate, const VDPixmap& src, bool bAllowConversion = true, const VDVideoDisplayScreenFXInfo *screenFX = nullptr, IVDVideoDisplayScreenFXEngine *screenFXEngine = nullptr) = 0;
	virtual void SetSourceSubrect(const vdrect32 *r) = 0;
	virtual void SetSourceSolidColor(uint32 color) = 0;

	virtual void SetReturnFocus(bool enable) = 0;
	virtual void SetTouchEnabled(bool enable) = 0;
	virtual void SetUse16Bit(bool enable) = 0;
	virtual void SetHDREnabled(bool hdr) = 0;

	virtual void SetFullScreen(bool fs, uint32 width = 0, uint32 height = 0, uint32 refresh = 0) = 0;
	virtual void SetCustomDesiredRefreshRate(float hz, float hzmin, float hzmax) = 0;
	virtual void SetDestRect(const vdrect32 *r, uint32 backgroundColor) = 0;
	virtual void SetDestRectF(const vdrect32f *r, uint32 backgroundColor) = 0;
	virtual void SetPixelSharpness(float xfactor, float yfactor) = 0;
	virtual void SetCompositor(IVDDisplayCompositor *compositor) = 0;
	virtual void SetSDRBrightness(float nits) = 0;

	virtual void PostBuffer(VDVideoDisplayFrame *) = 0;
	virtual bool RevokeBuffer(bool allowFrameSkip, VDVideoDisplayFrame **ppFrame) = 0;
	virtual void FlushBuffers() = 0;

	virtual void Invalidate() = 0;
	virtual void Update(int mode = 0) = 0;
	virtual void Cache() = 0;
	virtual void SetCallback(IVDVideoDisplayCallback *p) = 0;
	virtual void SetOnFrameStatusUpdated(vdfunction<void(int /*frames*/)> fn) = 0;

	enum AccelerationMode {
		kAccelOnlyInForeground,
		kAccelResetInForeground,
		kAccelAlways
	};

	virtual void SetAccelerationMode(AccelerationMode mode) = 0;

	virtual FilterMode GetFilterMode() = 0;
	virtual void SetFilterMode(FilterMode) = 0;
	virtual float GetSyncDelta() const = 0;

	virtual int GetQueuedFrames() const = 0;
	virtual bool IsFramePending() const = 0;
	virtual VDDVSyncStatus GetVSyncStatus() const = 0;

	virtual vdrect32 GetMonitorRect() = 0;
	virtual VDDMonitorInfo GetMonitorInformation() = 0;

	// Returns true if the current/last minidriver supported screen FX. This is a hint as to
	// whether hardware or software acceleration should be preferred. Calling code must still
	// be prepared to fall back to software emulation should the minidriver change to one that
	// doesn't support screen FX.
	virtual bool IsScreenFXPreferred() const = 0;

	// Returns if the current minidriver, adapter, and display are HDR/WCG capable.
	virtual VDDHighColorAvailability GetHDRCapability() const = 0;
	virtual VDDHighColorAvailability GetWCGCapability() const = 0;

	// Map a normalized source point in [0,1] to the destination size. Returns true if the
	// point was within the source, false if it was clamped. This is a no-op if distortion is
	// off.
	virtual bool MapNormSourcePtToDest(vdfloat2& pt) const = 0;

	// Map a normalized destination point in [0,1] to the destination size. Returns true if the
	// point was within the source, false if it was clamped. This is a no-op if distortion is
	// off.
	virtual bool MapNormDestPtToSource(vdfloat2& pt) const = 0;

	enum ProfileEvent {
		kProfileEvent_BeginTick,
		kProfileEvent_EndTick,
		kProfileEvent_BeginPresent,
		kProfileEvent_EndPresent,
		kProfileEvent_BeginRePresent,
		kProfileEvent_EndRePresent,
		kProfileEvent_VSyncInfo,
	};

	virtual void SetProfileHook(const vdfunction<void(ProfileEvent, uintptr)>& profileHook) = 0;

	virtual void RequestCapture(vdfunction<void(const VDPixmap *)> fn) = 0;
};

void VDVideoDisplaySetFeatures(bool enableDirectX, bool enableOverlays, bool enableTermServ, bool enableDirect3D, bool enableD3DFX, bool enableHighPrecision);
void VDVideoDisplaySetD3D9ExEnabled(bool enable);
void VDVideoDisplaySet3DEnabled(bool enable);
void VDVideoDisplaySetDebugInfoEnabled(bool enable);
void VDVideoDisplaySetBackgroundFallbackEnabled(bool enable);
void VDVideoDisplaySetSecondaryDXEnabled(bool enable);
void VDVideoDisplaySetMonitorSwitchingDXEnabled(bool enable);
void VDVideoDisplaySetTermServ3DEnabled(bool enable);
void VDVideoDisplaySetDXFlipModeEnabled(bool enable);
void VDVideoDisplaySetDXFlipDiscardEnabled(bool enable);
void VDVideoDisplaySetDXWaitableObjectEnabled(bool enable);
void VDVideoDisplaySetDXDoNotWaitEnabled(bool enable);
void VDVideoDisplaySetD3D9LimitPS1_1(bool enable);
void VDVideoDisplaySetD3D9LimitPS2_0(bool enable);
void VDVideoDisplaySetD3D11Force9_1(bool enable);
void VDVideoDisplaySetD3D11Force9_3(bool enable);
void VDVideoDisplaySetD3D11Force10_0(bool enable);
void VDVideoDisplaySetShowCustomShaderStats(bool enable);

struct VDDBloomV2Settings {
	float mCoeffWidthBase = -1.5f;
	float mCoeffWidthBaseSlope = 0.0f;
	float mCoeffWidthAdjustSlope = 0.050f;
	float mShoulderX = 0.305f;
	float mShoulderY = 0.305f;
	float mLimitX = 1.781f;
	float mLimitSlope = 0.086f;
};

void VDDSetBloomV2Settings(const VDDBloomV2Settings& settings);

IVDVideoDisplay *VDGetIVideoDisplay(VDGUIHandle hwnd);
bool VDRegisterVideoDisplayControl();

class IVDDisplayImageDecoder {
public:
	virtual bool DecodeImage(VDPixmapBuffer& buf, VDBufferedRandomAccessStream& stream) = 0;
};

void VDDisplaySetImageDecoder(IVDDisplayImageDecoder *pfn);

void VDDSetLibraryOverridesEnabled(bool enabled);
bool VDDGetLibraryOverridesEnabled();

#endif
