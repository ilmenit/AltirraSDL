//	VirtualDub - Video processing and capture application
//	A/V interface library
//	Copyright (C) 1998-2005 Avery Lee
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
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include <vector>
#include <algorithm>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <vd2/system/atomic.h>
#include <vd2/system/function.h>
#include <vd2/system/thread.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/w32assist.h>
#include <vd2/system/win32/touch.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmaputils.h>

#include "displaymgr.h"
#include <vd2/VDDisplay/compositor.h>
#include <vd2/VDDisplay/display.h>
#include <vd2/VDDisplay/displaydrv.h>
#include <vd2/VDDisplay/logging.h>
#include <vd2/VDDisplay/internal/screenfx.h>

#include <vd2/Tessa/config.h>

#ifndef WM_TOUCH
#define WM_TOUCH		0x0240
#endif

#define VDDEBUG_DISP(...) VDDispLogF(__VA_ARGS__)

extern const char g_szVideoDisplayControlName[] = "phaeronVideoDisplay";
extern const wchar_t g_wszVideoDisplayControlName[] = L"phaeronVideoDisplay";

extern void VDMemcpyRect(void *dst, ptrdiff_t dststride, const void *src, ptrdiff_t srcstride, size_t w, size_t h);

extern IVDVideoDisplayMinidriver *VDCreateDisplayDriver3D();

vdautoptr<VDVideoDisplayManager> g_pVDVideoDisplayManager;

///////////////////////////////////////////////////////////////////////////

namespace {
	bool VDIsTerminalServicesClient() {
		return GetSystemMetrics(SM_REMOTESESSION) != 0;		// Requires Windows NT SP4 or later.
	}
}

VDVideoDisplayManager *VDGetVideoDisplayManager() {
	if (!g_pVDVideoDisplayManager) {
		g_pVDVideoDisplayManager = new VDVideoDisplayManager;
		g_pVDVideoDisplayManager->Init();
	}

	return g_pVDVideoDisplayManager;
}

///////////////////////////////////////////////////////////////////////////

VDVideoDisplayFrame::VDVideoDisplayFrame()
	: mRefCount(0)
{
}

VDVideoDisplayFrame::~VDVideoDisplayFrame() {
}

int VDVideoDisplayFrame::AddRef() {
	return ++mRefCount;
}

int VDVideoDisplayFrame::Release() {
	int rc = --mRefCount;

	if (!rc)
		delete this;

	return rc;
}

///////////////////////////////////////////////////////////////////////////

class VDVideoDisplayWindow final : public IVDVideoDisplay, public IVDVideoDisplayMinidriverCallback, public VDVideoDisplayClient {
public:
	static ATOM Register();

protected:
	VDVideoDisplayWindow(HWND hwnd, const CREATESTRUCT& createInfo);
	~VDVideoDisplayWindow();

	void SetSourceMessage(const wchar_t *msg) override;
	void SetSourcePalette(const uint32 *palette, int count);
	bool SetSource(bool bAutoUpdate, const VDPixmap& src, bool bAllowConversion) override;
	bool SetSourcePersistent(bool bAutoUpdate, const VDPixmap& src, bool bAllowConversion, const VDVideoDisplayScreenFXInfo *screenFX, IVDVideoDisplayScreenFXEngine *screenFXEngine) override;
	void SetSourceSubrect(const vdrect32 *r) override;
	void SetSourceSolidColor(uint32 color) override;
	void SetReturnFocus(bool fs) override;
	void SetTouchEnabled(bool enable) override;
	void SetUse16Bit(bool enable) override;
	void SetHDREnabled(bool enable) override;
	void SetFullScreen(bool fs, uint32 width, uint32 height, uint32 refresh) override;
	void SetCustomDesiredRefreshRate(float hz, float hzmin, float hzmax) override;
	void SetDestRect(const vdrect32 *r, uint32 backgroundColor) override;
	void SetDestRectF(const vdrect32f *r, uint32 backgroundColor) override;
	void SetPixelSharpness(float xsharpness, float ysharpness) override;
	void SetCompositor(IVDDisplayCompositor *comp) override;
	void SetSDRBrightness(float nits) override;

	void PostBuffer(VDVideoDisplayFrame *) override;
	bool RevokeBuffer(bool allowFrameSkip, VDVideoDisplayFrame **ppFrame) override;
	void FlushBuffers() override;
	void Invalidate() override;
	void Update(int) override;
	void Destroy() override;
	void Reset() override;
	void Cache() override;
	void SetCallback(IVDVideoDisplayCallback *pcb) override;
	void SetOnFrameStatusUpdated(vdfunction<void(int /*frames*/)> fn) override;
	void SetAccelerationMode(AccelerationMode mode) override;
	FilterMode GetFilterMode() override;
	void SetFilterMode(FilterMode mode) override;
	float GetSyncDelta() const override { return mSyncDelta; }

	int GetQueuedFrames() const override;
	bool IsFramePending() const override;
	VDDVSyncStatus GetVSyncStatus() const override;

	vdrect32 GetMonitorRect() override;
	VDDMonitorInfo GetMonitorInformation() override;

	bool IsScreenFXPreferred() const override { return mbScreenFXSupported; }
	VDDHighColorAvailability GetHDRCapability() const override { return mHDRAvailability; }
	VDDHighColorAvailability GetWCGCapability() const override { return mWCGAvailability; }

	bool MapNormSourcePtToDest(vdfloat2& pt) const override;
	bool MapNormDestPtToSource(vdfloat2& pt) const override;

	void SetProfileHook(const vdfunction<void(ProfileEvent, uintptr)>& profileHook) override {
		mpProfileHook = profileHook;

		g_pVDVideoDisplayManager->SetProfileHook(profileHook);
	}
	
	void RequestCapture(vdfunction<void(const VDPixmap *)> fn) override;

public:
	void OnTick() override {
		if (mpMiniDriver)
			mpMiniDriver->Poll();
	}

private:
	void ReleaseActiveFrame() override;
	void RequestNextFrame() override;
	void QueuePresent() override;
	void OnFrameCaptured(const VDPixmap *px) override;
	void OnBeginPresent(uint32 frameNumber, bool rePresent) override;
	void OnEndPresent(uint32 frameNumber, bool rePresent) override;
	void OnVsyncInfo(const VDDVSyncProfileInfo& vsyncInfo) override;

	void DispatchNextFrame();
	bool DispatchActiveFrame();

private:
	struct SourceInfoEx final : public VDVideoDisplaySourceInfo {
		IVDVideoDisplayScreenFXEngine *mpScreenFXEngine = nullptr;
		const VDVideoDisplayScreenFXInfo *mpScreenFX = nullptr;
	};

	static LRESULT CALLBACK StaticChildWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	LRESULT ChildWndProc(UINT msg, WPARAM wParam, LPARAM lParam);

	void OnChildPaint();

	static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

	void OnPaint();
	void SyncSetSourceMessage(const wchar_t *);
	bool SyncSetSource(bool bAutoUpdate, const SourceInfoEx& params);
	void SyncReset();
	bool SyncInit(bool bAutoRefresh, bool bAllowNonpersistentSource);
	void SyncInvalidate();
	void SyncUpdate(int);
	void SyncSetFilterMode(FilterMode mode);
	void SyncSetSolidColor(uint32 color);
	void OnDisplayChange() override;
	void OnForegroundChange(bool foreground) override;
	void OnRealizePalette(bool foreground) override;
	void OnGlobalSettingsChanged() override;
	bool InitMiniDriver();
	void ShutdownMiniDriver();
	void RequestUpdate();
	void VerifyDriverResult(bool result);
	void UpdateCoordinateMapping();
	bool CheckForMonitorChange();
	void CheckAndRespondToMonitorChange();
	bool IsOnSecondaryMonitor() const;

	static void GetMonitorRect(RECT *r, HMONITOR hmon);

	void UpdateSDRBrightness();

private:
	enum {
		kReinitDisplayTimerId = 500
	};

	HWND		mhwnd;
	HWND		mhwndChild;
	HPALETTE	mhOldPalette;
	HMONITOR	mhLastMonitor;
	RECT		mMonitorRect;
	RECT		mLastMonitorCheckRect;

	VDCriticalSection			mMutex;
	vdlist<VDVideoDisplayFrame>	mPendingFrames;
	vdlist<VDVideoDisplayFrame>	mIdleFrames;
	VDVideoDisplayFrame			*mpActiveFrame = nullptr;
	VDVideoDisplayFrame			*mpLastFrame = nullptr;
	VDVideoDisplaySourceInfo	mSource {};
	VDVideoDisplaySourceInfo	mSourceEmulatedFX {};
	VDVideoDisplayScreenFXInfo	mSourceScreenFX {};
	bool						mbSourceUseScreenFX = false;
	bool						mbSourceEmulatedFXEnabled = false;
	IVDVideoDisplayScreenFXEngine *mpSourceScreenFXEngine = nullptr;
	vdrefptr<IVDDisplayCompositor> mpCompositor;

	IVDVideoDisplayMinidriver *mpMiniDriver;
	int		mMinidriverInitLock;
	bool	mbMiniDriverSecondarySensitive;
	bool	mbMiniDriverClearOtherMonitors;
	UINT	mReinitDisplayTimer;

	IVDVideoDisplayCallback		*mpCB;
	vdfunction<void(int /*frames*/)> mpOnFrameStatusUpdated;

	int		mInhibitRefresh;

	/// Locks out normal WM_PAINT processing on the child window if non-zero.
	int		mInhibitPaint;

	VDAtomicFloat	mSyncDelta;

	FilterMode	mFilterMode;
	AccelerationMode	mAccelMode;

	bool		mbIgnoreMouse;
	bool		mbUseSubrect;
	bool		mbReturnFocus;
	bool		mbTouchEnabled;
	bool		mbUse16Bit;
	bool		mbHDR = false;
	bool		mbFullScreen;
	uint32		mFullScreenWidth;
	uint32		mFullScreenHeight;
	uint32		mFullScreenRefreshRate;
	bool		mbDestRectEnabled;
	bool		mbScreenFXSupported = false;
	VDDHighColorAvailability mHDRAvailability = VDDHighColorAvailability::NoMinidriverSupport;
	VDDHighColorAvailability mWCGAvailability = VDDHighColorAvailability::NoMinidriverSupport;
	float		mPixelSharpnessX;
	float		mPixelSharpnessY;
	vdrect32	mSourceSubrect;
	vdrect32f	mDestRectF;
	uint32		mBackgroundColor;
	VDStringW	mMessage;

	bool		mbDistortionMappingValid = false;
	VDDisplayDistortionMapping mDistortionMapping {};

	uint32		mSolidColorBuffer;
	float		mSDRBrightness = 80.0f;

	float		mCustomRefreshRate = 0;
	float		mCustomRefreshRateMin = 0;
	float		mCustomRefreshRateMax = 0;

	vdfunction<void(ProfileEvent, uintptr)> mpProfileHook;
	vdfunction<void(const VDPixmap *)> mpCaptureFn;

	VDPixmapBuffer		mCachedImage;
	VDPixmapBuffer		mCaptureBuffer;

	VDDVSyncStatus	mVSyncStatus;

	uint32	mSourcePalette[256];

	static ATOM				sChildWindowClass;

	static constexpr UINT MYWM_SETSOURCE			= WM_USER + 0x100;
	static constexpr UINT MYWM_UPDATE				= WM_USER + 0x101;
	static constexpr UINT MYWM_CACHE				= WM_USER + 0x102;
	static constexpr UINT MYWM_RESET				= WM_USER + 0x103;
	static constexpr UINT MYWM_SETSOURCEMSG			= WM_USER + 0x104;
	static constexpr UINT MYWM_PROCESSNEXTFRAME		= WM_USER + 0x105;
	static constexpr UINT MYWM_DESTROY				= WM_USER + 0x106;
	static constexpr UINT MYWM_SETFILTERMODE		= WM_USER + 0x107;
	static constexpr UINT MYWM_SETSOLIDCOLOR		= WM_USER + 0x108;
	static constexpr UINT MYWM_INVALIDATE			= WM_USER + 0x109;
	static constexpr UINT MYWM_QUEUEPRESENT			= WM_USER + 0x10A;
	static constexpr UINT MYWM_SETTOUCHENABLED		= WM_USER + 0x10B;
	static constexpr UINT MYWM_GETMONITORRECT		= WM_USER + 0x10C;
	static constexpr UINT MYWM_SETUSE16BIT			= WM_USER + 0x10D;
	static constexpr UINT MYWM_FRAMECAPTURED		= WM_USER + 0x10F;
	static constexpr UINT MYWM_REQUESTCAPTURE		= WM_USER + 0x110;
	static constexpr UINT MYWM_SETCUSTOMREFRESH		= WM_USER + 0x111;
	static constexpr UINT MYWM_GETQUEUEDFRAMES		= WM_USER + 0x112;
	static constexpr UINT MYWM_ISFRAMEPENDING		= WM_USER + 0x113;

public:
	static bool		sbEnableDX;
	static bool		sbEnableDXOverlay;
	static bool		sbEnableD3D;
	static bool		sbEnable3D;
	static bool		sbEnableTS;
	static bool		sbEnableDebugInfo;
	static bool		sbEnableHighPrecision;
	static bool		sbEnableBackgroundFallback;
	static bool		sbEnableSecondaryMonitorDX;
	static bool		sbEnableMonitorSwitchingDX;
	static bool		sbEnableD3D9Ex;
	static bool		sbEnableTS3D;
};

ATOM									VDVideoDisplayWindow::sChildWindowClass;
bool VDVideoDisplayWindow::sbEnableDX = true;
bool VDVideoDisplayWindow::sbEnableDXOverlay = true;
bool VDVideoDisplayWindow::sbEnableD3D;
bool VDVideoDisplayWindow::sbEnable3D;
bool VDVideoDisplayWindow::sbEnableTS;
bool VDVideoDisplayWindow::sbEnableDebugInfo;
bool VDVideoDisplayWindow::sbEnableHighPrecision;
bool VDVideoDisplayWindow::sbEnableBackgroundFallback;
bool VDVideoDisplayWindow::sbEnableSecondaryMonitorDX;
bool VDVideoDisplayWindow::sbEnableMonitorSwitchingDX;
bool VDVideoDisplayWindow::sbEnableD3D9Ex;
bool VDVideoDisplayWindow::sbEnableTS3D = false;

///////////////////////////////////////////////////////////////////////////

void VDVideoDisplaySetDebugInfoEnabled(bool enable) {
	if (VDVideoDisplayWindow::sbEnableDebugInfo != enable) {
		VDVideoDisplayWindow::sbEnableDebugInfo = enable;

		if (g_pVDVideoDisplayManager)
			g_pVDVideoDisplayManager->NotifyGlobalSettingsChanged();
	}
}

void VDVideoDisplaySetBackgroundFallbackEnabled(bool enable) {
	VDVideoDisplayWindow::sbEnableBackgroundFallback = enable;
}

void VDVideoDisplaySetSecondaryDXEnabled(bool enable) {
	VDVideoDisplayWindow::sbEnableSecondaryMonitorDX = enable;
}

void VDVideoDisplaySetMonitorSwitchingDXEnabled(bool enable) {
	VDVideoDisplayWindow::sbEnableMonitorSwitchingDX = enable;
}

void VDVideoDisplaySetTermServ3DEnabled(bool enable) {
	VDVideoDisplayWindow::sbEnableTS3D = enable;
}

void VDVideoDisplaySetFeatures(bool enableDirectX, bool enableDirectXOverlay, bool enableTermServ, bool enableDirect3D, bool enableDirect3DFX, bool enableHighPrecision) {
	VDVideoDisplayWindow::sbEnableDX = enableDirectX;
	VDVideoDisplayWindow::sbEnableDXOverlay = enableDirectXOverlay;
	VDVideoDisplayWindow::sbEnableD3D = enableDirect3D;
	VDVideoDisplayWindow::sbEnableTS = enableTermServ;
	VDVideoDisplayWindow::sbEnableHighPrecision = enableHighPrecision;
}

void VDVideoDisplaySetD3D9ExEnabled(bool enable) {
	VDVideoDisplayWindow::sbEnableD3D9Ex = enable;
}

void VDVideoDisplaySet3DEnabled(bool enable) {
	VDVideoDisplayWindow::sbEnable3D = enable;
}

void VDDSetLibraryOverridesEnabled(bool enabled) {
	VDTSetLibraryOverridesEnabled(enabled);
}

bool VDDGetLibraryOverridesEnabled() {
	return VDTGetLibraryOverridesEnabled();
}

///////////////////////////////////////////////////////////////////////////

ATOM VDVideoDisplayWindow::Register() {
	WNDCLASS wc {};
	HMODULE hInst = VDGetLocalModuleHandleW32();

	if (!sChildWindowClass) {
		wc.style			= CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc		= StaticChildWndProc;
		wc.cbClsExtra		= 0;
		wc.cbWndExtra		= sizeof(VDVideoDisplayWindow *);
		wc.hInstance		= hInst;
		wc.hIcon			= 0;
		wc.hCursor			= LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground	= (HBRUSH)(BLACK_BRUSH + 1);
		wc.lpszMenuName		= 0;
		wc.lpszClassName	= L"phaeronVideoDisplayChild";

		sChildWindowClass = RegisterClass(&wc);
		if (!sChildWindowClass)
			return 0;
	}

	wc.style			= CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc		= StaticWndProc;
	wc.cbClsExtra		= 0;
	wc.cbWndExtra		= sizeof(VDVideoDisplayWindow *);
	wc.hInstance		= hInst;
	wc.hIcon			= 0;
	wc.hCursor			= LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground	= (HBRUSH)(COLOR_3DFACE + 1);
	wc.lpszMenuName		= 0;
	wc.lpszClassName	= g_wszVideoDisplayControlName;

	return RegisterClass(&wc);
}

IVDVideoDisplay *VDGetIVideoDisplay(VDGUIHandle hwnd) {
	return static_cast<IVDVideoDisplay *>(reinterpret_cast<VDVideoDisplayWindow*>(GetWindowLongPtr((HWND)hwnd, 0)));
}

bool VDRegisterVideoDisplayControl() {
	return 0 != VDVideoDisplayWindow::Register();
}

///////////////////////////////////////////////////////////////////////////

VDVideoDisplayWindow::VDVideoDisplayWindow(HWND hwnd, const CREATESTRUCT& createInfo)
	: mhwnd(hwnd)
	, mhwndChild(NULL)
	, mhLastMonitor(NULL)
	, mhOldPalette(0)
	, mpMiniDriver(0)
	, mMinidriverInitLock(0)
	, mbMiniDriverSecondarySensitive(false)
	, mbMiniDriverClearOtherMonitors(false)
	, mReinitDisplayTimer(0)
	, mpCB(0)
	, mInhibitRefresh(0)
	, mInhibitPaint(0)
	, mSyncDelta(0.0f)
	, mFilterMode(kFilterAnySuitable)
	, mAccelMode(VDVideoDisplayWindow::sbEnableBackgroundFallback ? kAccelOnlyInForeground : kAccelAlways)
	, mbIgnoreMouse(false)
	, mbUseSubrect(false)
	, mbReturnFocus(false)
	, mbTouchEnabled(false)
	, mbFullScreen(false)
	, mFullScreenWidth(0)
	, mFullScreenHeight(0)
	, mFullScreenRefreshRate(0)
	, mbDestRectEnabled(false)
	, mDestRectF(0, 0, 0, 0)
	, mPixelSharpnessX(1.0f)
	, mPixelSharpnessY(1.0f)
	, mBackgroundColor(0)
	, mpActiveFrame(NULL)
	, mpLastFrame(NULL)
{
	mSource.pixmap.data = 0;

	memset(&mLastMonitorCheckRect, 0, sizeof mLastMonitorCheckRect);
	memset(&mMonitorRect, 0, sizeof mMonitorRect);

	if (createInfo.hwndParent) {
		DWORD dwThreadId = GetWindowThreadProcessId(createInfo.hwndParent, NULL);
		if (dwThreadId == GetCurrentThreadId())
			mbIgnoreMouse = true;
	}

	VDVideoDisplayManager *vdm = VDGetVideoDisplayManager();
	vdm->AddClient(this);
}

VDVideoDisplayWindow::~VDVideoDisplayWindow() {
	mpManager->RemoveClient(this);
}

///////////////////////////////////////////////////////////////////////////

void VDVideoDisplayWindow::SetSourceMessage(const wchar_t *msg) {
	SendMessage(mhwnd, MYWM_SETSOURCEMSG, 0, (LPARAM)msg);
}

void VDVideoDisplayWindow::SetSourcePalette(const uint32 *palette, int count) {
	memcpy(mSourcePalette, palette, 4*std::min<int>(count, 256));
}

bool VDVideoDisplayWindow::SetSource(bool bAutoUpdate, const VDPixmap& src, bool bAllowConversion) {
	// We do allow data to be NULL for set-without-load.
	if (src.data)
		VDAssertValidPixmap(src);

	SourceInfoEx params {};

	params.pixmap			= src;
	params.bAllowConversion	= bAllowConversion;
	params.bPersistent		= false;
	params.use16bit			= mbUse16Bit;
	params.mbHDR			= mbHDR;

	const VDPixmapFormatInfo& info = VDPixmapGetInfo(src.format);
	params.bpp = info.qsize >> info.qhbits;
	params.bpr = (((src.w-1) >> info.qwbits)+1) * info.qsize;

	params.mpCB				= this;

	return 0 != SendMessage(mhwnd, MYWM_SETSOURCE, bAutoUpdate, (LPARAM)&params);
}

bool VDVideoDisplayWindow::SetSourcePersistent(bool bAutoUpdate, const VDPixmap& src, bool bAllowConversion, const VDVideoDisplayScreenFXInfo *screenFX, IVDVideoDisplayScreenFXEngine *screenFXEngine) {
	// We do allow data to be NULL for set-without-load.
	if (src.data)
		VDAssertValidPixmap(src);

	SourceInfoEx params {};

	params.pixmap			= src;
	params.bAllowConversion	= bAllowConversion;
	params.bPersistent		= true;
	params.use16bit			= mbUse16Bit;
	params.mbHDR			= mbHDR;

	const VDPixmapFormatInfo& info = VDPixmapGetInfo(src.format);
	params.bpp = info.qsize >> info.qhbits;
	params.bpr = (((src.w-1) >> info.qwbits)+1) * info.qsize;
	params.mpCB				= this;

	params.mpScreenFX = screenFX;
	params.mpScreenFXEngine = screenFXEngine;

	return 0 != SendMessage(mhwnd, MYWM_SETSOURCE, bAutoUpdate, (LPARAM)&params);
}

void VDVideoDisplayWindow::SetSourceSubrect(const vdrect32 *r) {
	if (r) {
		mbUseSubrect = true;
		mSourceSubrect = *r;
	} else
		mbUseSubrect = false;

	if (mpMiniDriver) {
		if (!mpMiniDriver->SetSubrect(r))
			SyncReset();
	}
}

void VDVideoDisplayWindow::SetSourceSolidColor(uint32 color) {
	SendMessage(mhwnd, MYWM_SETSOLIDCOLOR, 0, (LPARAM)color);
}

void VDVideoDisplayWindow::SetReturnFocus(bool enable) {
	mbReturnFocus = enable;
}

void VDVideoDisplayWindow::SetTouchEnabled(bool enable) {
	SendMessage(mhwnd, MYWM_SETTOUCHENABLED, enable, 0);
}

void VDVideoDisplayWindow::SetUse16Bit(bool enable) {
	SendMessage(mhwnd, MYWM_SETUSE16BIT, enable, 0);
}

void VDVideoDisplayWindow::SetHDREnabled(bool hdr) {
	mbHDR = hdr;
}

void VDVideoDisplayWindow::SetFullScreen(bool fs, uint32 w, uint32 h, uint32 refresh) {
	mbFullScreen = fs;
	mFullScreenWidth = w;
	mFullScreenHeight = h;
	mFullScreenRefreshRate = refresh;
	if (mpMiniDriver)
		mpMiniDriver->SetFullScreen(fs, w, h, refresh, mbUse16Bit);

	const bool leavingFS = mbRequiresFullScreen && !fs;
	SetRequiresFullScreen(fs);

	// OnDisplayChange() is suppressed when in full screen mode, so if we are leaving
	// full screen mode, we need to trigger it here. We don't however, want it to force
	// a reinit, so we wrap it in an init lock.
	if (leavingFS) {
		++mMinidriverInitLock;
		OnDisplayChange();
		--mMinidriverInitLock;
	}

	// If we're going to full screen mode, resize the top-level window to the monitor.
	if (fs) {
		SetWindowPos(GetAncestor(mhwnd, GA_ROOT), NULL, mMonitorRect.left, mMonitorRect.top,
			mMonitorRect.right - mMonitorRect.left,
			mMonitorRect.bottom - mMonitorRect.top,
			SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
	}

	if (!mpActiveFrame)
		DispatchNextFrame();
	else
		DispatchActiveFrame();
}

void VDVideoDisplayWindow::SetCustomDesiredRefreshRate(float hz, float hzmin, float hzmax) {
	const float args[3] = { hz, hzmin, hzmax };

	SendMessage(mhwnd, MYWM_SETCUSTOMREFRESH, 0, (LPARAM)args);
}

void VDVideoDisplayWindow::SetDestRect(const vdrect32 *r, uint32 backgroundColor) {
	if (!r)
		return SetDestRectF(nullptr, backgroundColor);

	vdrect32f rf(
		(float)r->left,
		(float)r->top,
		(float)r->right,
		(float)r->bottom
	);

	SetDestRectF(&rf, backgroundColor);
}

void VDVideoDisplayWindow::SetDestRectF(const vdrect32f *r, uint32 backgroundColor) {
	mbDestRectEnabled = false;

	if (r) {
		mbDestRectEnabled = true;
		mDestRectF = *r;
	}

	mBackgroundColor = backgroundColor;

	if (mpMiniDriver)
		mpMiniDriver->SetDestRectF(r, backgroundColor);

	UpdateCoordinateMapping();
}

void VDVideoDisplayWindow::SetPixelSharpness(float xsharpness, float ysharpness) {
	mPixelSharpnessX = xsharpness;
	mPixelSharpnessY = ysharpness;

	if (mpMiniDriver)
		mpMiniDriver->SetPixelSharpness(xsharpness, ysharpness);
}

void VDVideoDisplayWindow::SetCompositor(IVDDisplayCompositor *comp) {
	if (mpCompositor == comp)
		return;

	const bool resetRequired = (!mpCompositor || !comp);

	mpCompositor = comp;

	if (resetRequired)
		SyncReset();
	else if (mpMiniDriver)
		mpMiniDriver->SetCompositor(comp);
}

void VDVideoDisplayWindow::SetSDRBrightness(float nits) {
	if (mSDRBrightness != nits) {
		mSDRBrightness = nits;

		if (mpMiniDriver) {
			UpdateSDRBrightness();

			Invalidate();
		}
	}
}

void VDVideoDisplayWindow::PostBuffer(VDVideoDisplayFrame *p) {
	p->AddRef();

	bool wasIdle = false;
	vdsynchronized(mMutex) {
		if (!mpMiniDriver || (!mpActiveFrame && mPendingFrames.empty()))
			wasIdle = true;

		mPendingFrames.push_back(p);
	}

	if (wasIdle) {
		WndProc(MYWM_PROCESSNEXTFRAME, 0, 0);
		//RequestNextFrame();
	}
}

bool VDVideoDisplayWindow::RevokeBuffer(bool allowFrameSkip, VDVideoDisplayFrame **ppFrame) {
	VDVideoDisplayFrame *p = NULL;
	vdsynchronized(mMutex) {
		if (allowFrameSkip && (!mPendingFrames.empty() && mPendingFrames.front() != mPendingFrames.back())) {
			p = mPendingFrames.back();
			mPendingFrames.pop_back();
		} else if (!mIdleFrames.empty()) {
			p = mIdleFrames.front();
			mIdleFrames.pop_front();
		}
	}

	if (!p)
		return false;

	*ppFrame = p;
	return true;
}

void VDVideoDisplayWindow::FlushBuffers() {
	vdlist<VDVideoDisplayFrame> frames;
	if (mpLastFrame) {
		frames.push_back(mpLastFrame);
		mpLastFrame = NULL;
	}

	// wait for any current frame to clear
	for(;;) {
		bool idle;
		vdsynchronized(mMutex) {
			// clear existing pending frames so the display doesn't start another render
			if (!mPendingFrames.empty())
				frames.splice(frames.end(), mIdleFrames);

			idle = !mpActiveFrame;
		}

		if (idle)
			break;

		::Sleep(1);
		OnTick();
	}

	vdsynchronized(mMutex) {
		frames.splice(frames.end(), mIdleFrames);
		frames.splice(frames.end(), mPendingFrames);
	}

	while(!frames.empty()) {
		VDVideoDisplayFrame *p = frames.back();
		frames.pop_back();

		p->Release();
	}
}

void VDVideoDisplayWindow::Invalidate() {
	SendMessage(mhwnd, MYWM_INVALIDATE, 0, 0);
}

void VDVideoDisplayWindow::Update(int fieldmode) {
	SendMessage(mhwnd, MYWM_UPDATE, fieldmode, 0);
}

void VDVideoDisplayWindow::Cache() {
	SendMessage(mhwnd, MYWM_CACHE, 0, 0);
}

void VDVideoDisplayWindow::Destroy() {
	SendMessage(mhwnd, MYWM_DESTROY, 0, 0);
}

void VDVideoDisplayWindow::Reset() {
	SendMessage(mhwnd, MYWM_RESET, 0, 0);
}

void VDVideoDisplayWindow::SetCallback(IVDVideoDisplayCallback *pCB) {
	mpCB = pCB;
}

void VDVideoDisplayWindow::SetOnFrameStatusUpdated(vdfunction<void(int /*frames*/)> fn) {
	mpOnFrameStatusUpdated = std::move(fn);
}

void VDVideoDisplayWindow::SetAccelerationMode(AccelerationMode mode) {
	mAccelMode = mode;
}

IVDVideoDisplay::FilterMode VDVideoDisplayWindow::GetFilterMode() {
	return mFilterMode;
}

void VDVideoDisplayWindow::SetFilterMode(FilterMode mode) {
	SendMessage(mhwnd, MYWM_SETFILTERMODE, 0, (LPARAM)mode);
}

int VDVideoDisplayWindow::GetQueuedFrames() const {
	return (int)SendMessage(mhwnd, MYWM_GETQUEUEDFRAMES, 0, 0);
}

bool VDVideoDisplayWindow::IsFramePending() const {
	return 0 != SendMessage(mhwnd, MYWM_ISFRAMEPENDING, 0, 0);
}

VDDVSyncStatus VDVideoDisplayWindow::GetVSyncStatus() const {
	return mVSyncStatus;
}

vdrect32 VDVideoDisplayWindow::GetMonitorRect() {
	RECT r {};
	SendMessage(mhwnd, MYWM_GETMONITORRECT, 0, (LPARAM)&r);

	return vdrect32(r.left, r.top, r.right, r.bottom);
}

VDDMonitorInfo VDVideoDisplayWindow::GetMonitorInformation() {
	VDDMonitorInfo monitorInfo;

	if (mpManager)
		mpManager->GetMonitorInformation(mhLastMonitor, monitorInfo);

	return monitorInfo;
}

bool VDVideoDisplayWindow::MapNormSourcePtToDest(vdfloat2& pt) const {
	using namespace nsVDMath;

	if (mbDistortionMappingValid)
		return mDistortionMapping.MapImageToScreen(pt);

	vdfloat2 pt2 { std::clamp(pt.x, 0.0f, 1.0f), std::clamp(pt.y, 0.0f, 1.0f) };
	const bool valid = (pt == pt2);

	pt = pt2;
	return valid;
}

bool VDVideoDisplayWindow::MapNormDestPtToSource(vdfloat2& pt) const {
	using namespace nsVDMath;

	return !mbDistortionMappingValid || mDistortionMapping.MapScreenToImage(pt);
}

void VDVideoDisplayWindow::RequestCapture(vdfunction<void(const VDPixmap *)> fn) {
	vdfunction<void(const VDPixmap *)> oldfn;

	vdsynchronized(mMutex) {
		oldfn = std::move(mpCaptureFn);
		mpCaptureFn = std::move(fn);
	}

	if (oldfn)
		oldfn(nullptr);

	PostMessage(mhwnd, MYWM_REQUESTCAPTURE, 0, 0);
}

void VDVideoDisplayWindow::ReleaseActiveFrame() {
	VDVideoDisplayFrame *pFrameToDiscard = NULL;
	VDVideoDisplayFrame *pFrameToDiscard2 = NULL;

	vdsynchronized(mMutex) {
		if (mpActiveFrame) {
			if (mpLastFrame) {
				if (mpLastFrame->mFlags & kDoNotCache)
					pFrameToDiscard = mpLastFrame;
				else {
					mIdleFrames.push_front(mpLastFrame);
				}
				mpLastFrame = NULL;
			}

			if (mpActiveFrame->mFlags & kDoNotCache) {
				pFrameToDiscard2 = mpActiveFrame;
			} else {
				mpLastFrame = mpActiveFrame;
			}

			mpActiveFrame = NULL;
		}
	}

	if (pFrameToDiscard)
		pFrameToDiscard->Release();

	if (pFrameToDiscard2)
		pFrameToDiscard2->Release();
}

void VDVideoDisplayWindow::QueuePresent() {
	PostMessage(mhwnd, MYWM_QUEUEPRESENT, 0, 0);
}

void VDVideoDisplayWindow::OnFrameCaptured(const VDPixmap *px) {
	vdsynchronized(mMutex) {
		if (px)
			mCaptureBuffer.assign(*px);
		else
			mCaptureBuffer.clear();
	}

	PostMessage(mhwnd, MYWM_FRAMECAPTURED, 0, 0);
}

void VDVideoDisplayWindow::OnBeginPresent(uint32 frameNumber, bool rePresent) {
	if (mpProfileHook)
		mpProfileHook(rePresent ? kProfileEvent_BeginRePresent : kProfileEvent_BeginPresent, frameNumber);
}

void VDVideoDisplayWindow::OnEndPresent(uint32 frameNumber, bool rePresent) {
	if (mpProfileHook)
		mpProfileHook(rePresent ? kProfileEvent_EndRePresent : kProfileEvent_EndPresent, frameNumber);
}

void VDVideoDisplayWindow::OnVsyncInfo(const VDDVSyncProfileInfo& vsyncInfo) {
	if (mpProfileHook)
		mpProfileHook(kProfileEvent_VSyncInfo, (uintptr)&vsyncInfo);
}

void VDVideoDisplayWindow::RequestNextFrame() {
	PostMessage(mhwnd, MYWM_PROCESSNEXTFRAME, 0, 0);
}

void VDVideoDisplayWindow::DispatchNextFrame() {
	vdsynchronized(mMutex) {
		VDASSERT(!mpActiveFrame);
		if (!mPendingFrames.empty()) {
			mpActiveFrame = mPendingFrames.front();
			mPendingFrames.pop_front();
		}
	}

	DispatchActiveFrame();
}

bool VDVideoDisplayWindow::DispatchActiveFrame() {
	if (mpActiveFrame) {
		SourceInfoEx params {};

		params.pixmap			= mpActiveFrame->mPixmap;

		uint32 flags = mpActiveFrame->mFlags;

		params.bAllowConversion	= mpActiveFrame->mbAllowConversion;
		params.bPersistent		= false;
		params.use16bit			= mbUse16Bit;
		params.mbHDR			= mbHDR;

		const VDPixmapFormatInfo& info = VDPixmapGetInfo(mpActiveFrame->mPixmap.format);
		params.bpp = info.qsize >> info.qhbits;
		params.bpr = (((mpActiveFrame->mPixmap.w-1) >> info.qwbits)+1) * info.qsize;

		params.mpCB				= this;
		params.mpScreenFX = mpActiveFrame->mpScreenFX;
		params.mpScreenFXEngine = mpActiveFrame->mpScreenFXEngine;

		params.mFrameNumber		= mpActiveFrame->mFrameNumber;

		if (!SyncSetSource(false, params)) {
			ReleaseActiveFrame();

			// Uh oh. At this point we have no guarantee that any of the other frames
			// will dispatch either, so flush them all.
			vdsynchronized(mMutex) {
				mIdleFrames.splice(mIdleFrames.end(), mPendingFrames);
			}
			return false;
		}

		SyncUpdate(flags);
		return true;
	}

	return false;
}

///////////////////////////////////////////////////////////////////////////

LRESULT CALLBACK VDVideoDisplayWindow::StaticChildWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	VDVideoDisplayWindow *pThis = (VDVideoDisplayWindow *)GetWindowLongPtr(hwnd, 0);

	switch(msg) {
	case WM_NCCREATE:
		pThis = (VDVideoDisplayWindow *)(((LPCREATESTRUCT)lParam)->lpCreateParams);
		pThis->mhwndChild = hwnd;
		SetWindowLongPtr(hwnd, 0, (DWORD_PTR)pThis);
		break;
	case WM_NCDESTROY:
		SetWindowLongPtr(hwnd, 0, (DWORD_PTR)NULL);
		break;
	}

	return pThis ? pThis->ChildWndProc(msg, wParam, lParam) : DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT VDVideoDisplayWindow::ChildWndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
	case WM_PAINT:
		OnChildPaint();
		return 0;
	case WM_NCHITTEST:
		if (mbIgnoreMouse)
			return HTTRANSPARENT;
		break;
	case WM_ERASEBKGND:
		if (!mbMiniDriverClearOtherMonitors)
			return TRUE;
		return FALSE;

	case WM_SETFOCUS:
		if (mbReturnFocus) {
			HWND hwndParent = GetParent(mhwnd);
			if (hwndParent)
				SetFocus(GetParent(mhwnd));
		}
		break;

	case WM_SIZE:
		if (mpMiniDriver)
			VerifyDriverResult(mpMiniDriver->Resize(LOWORD(lParam), HIWORD(lParam)));

		UpdateCoordinateMapping();
		break;

	case WM_TIMER:
		if (mpMiniDriver) {
			if (mpProfileHook)
				mpProfileHook(kProfileEvent_BeginTick, 0);

			VerifyDriverResult(mpMiniDriver->Tick((int)wParam));

			if (mpProfileHook)
				mpProfileHook(kProfileEvent_EndTick, 0);
		}
		break;

	case WM_TOUCH:
		if (HWND hwndParent = ::GetAncestor(mhwnd, GA_PARENT))
			return SendMessage(hwndParent, WM_TOUCH, wParam, lParam);
	}

	return DefWindowProc(mhwndChild, msg, wParam, lParam);
}

void VDVideoDisplayWindow::OnChildPaint() {
	if (mInhibitPaint) {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(mhwndChild, &ps);
		if (hdc)
			EndPaint(mhwndChild, &ps);
		return;
	}

	++mInhibitRefresh;

	CheckAndRespondToMonitorChange();

	bool bDisplayOK = false;

	if (mpMiniDriver) {
		if (mpMiniDriver->IsValid())
			bDisplayOK = true;
		else if (mSource.pixmap.data && mSource.bPersistent && !mpMiniDriver->Update(IVDVideoDisplayMinidriver::kModeNone))
			bDisplayOK = true;
	}

	if (!bDisplayOK) {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(mhwndChild, &ps);

		if (hdc) {
			if (ps.fErase) {
				RECT r;
				if (GetClientRect(mhwndChild, &r))
					FillRect(hdc, &r, (HBRUSH)(COLOR_WINDOW + 1));
			}
			EndPaint(mhwndChild, &ps);
		}

		--mInhibitRefresh;
		RequestUpdate();
		return;
	}

	VDASSERT(IsWindow(mhwndChild));

	PAINTSTRUCT ps;
	HDC hdc = BeginPaint(mhwndChild, &ps);

	if (hdc) {
		RECT r;

		GetClientRect(mhwndChild, &r);

		if (mpMiniDriver && mpMiniDriver->IsValid()) {
			VerifyDriverResult(mpMiniDriver->Paint(hdc, r, IVDVideoDisplayMinidriver::kModeNone));

			if (mbMiniDriverClearOtherMonitors && ps.fErase) {
				// Fill portions of the window on other monitors.
				RECT rMon(mMonitorRect);
				MapWindowPoints(NULL, mhwndChild, (LPPOINT)&rMon, 2);
				ExcludeClipRect(hdc, rMon.left, rMon.top, rMon.right, rMon.bottom);
				FillRect(hdc, &r, (HBRUSH)(COLOR_WINDOW + 1));
			}
		} else if (ps.fErase) {
			FillRect(hdc, &r, (HBRUSH)(COLOR_WINDOW + 1));
		}

		EndPaint(mhwndChild, &ps);
	}


	--mInhibitRefresh;

	if (mpMiniDriver && !mpMiniDriver->IsFramePending())
		RequestNextFrame();
}

///////////////////////////////////////////////////////////////////////////

LRESULT CALLBACK VDVideoDisplayWindow::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	VDVideoDisplayWindow *pThis = (VDVideoDisplayWindow *)GetWindowLongPtr(hwnd, 0);

	switch(msg) {
	case WM_NCCREATE:
		pThis = new VDVideoDisplayWindow(hwnd, *(const CREATESTRUCT *)lParam);
		SetWindowLongPtr(hwnd, 0, (DWORD_PTR)pThis);
		break;
	case WM_NCDESTROY:
		if (pThis)
			pThis->SyncReset();
		delete pThis;
		pThis = NULL;
		SetWindowLongPtr(hwnd, 0, 0);
		break;
	}

	return pThis ? pThis->WndProc(msg, wParam, lParam) : DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT VDVideoDisplayWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
	case WM_DESTROY:
		SyncReset();
		ReleaseActiveFrame();
		FlushBuffers();

		if (mReinitDisplayTimer) {
			KillTimer(mhwnd, mReinitDisplayTimer);
			mReinitDisplayTimer = 0;
		}

		if (mhOldPalette) {
			DeleteObject(mhOldPalette);
			mhOldPalette = 0;
		}

		break;
	case WM_PAINT:
		OnPaint();
		return 0;
	case MYWM_SETSOURCE:
		ReleaseActiveFrame();
		FlushBuffers();
		return SyncSetSource(wParam != 0, *(const SourceInfoEx *)lParam);
	case MYWM_UPDATE:
		SyncUpdate((FieldMode)wParam);
		return 0;
	case MYWM_DESTROY:
		SyncReset();
		DestroyWindow(mhwnd);
		return 0;
	case MYWM_RESET:
		mMessage.clear();
		InvalidateRect(mhwnd, NULL, TRUE);
		SyncReset();
		mSource.pixmap.data = NULL;
		return 0;
	case MYWM_SETSOURCEMSG:
		SyncSetSourceMessage((const wchar_t *)lParam);
		return 0;
	case MYWM_PROCESSNEXTFRAME:
		if (!mpMiniDriver || !mpMiniDriver->IsFramePending()) {
			bool newframe;
			int framesQueued;

			vdsynchronized(mMutex) {
				newframe = !mpActiveFrame;

				framesQueued = mPendingFrames.size() + (newframe ? 1 : 0);
			}

			if (newframe)
				DispatchNextFrame();

			if (mpOnFrameStatusUpdated)
				mpOnFrameStatusUpdated(framesQueued);
		}

		return 0;
	case MYWM_SETFILTERMODE:
		SyncSetFilterMode((FilterMode)lParam);
		return 0;
	case MYWM_SETSOLIDCOLOR:
		SyncSetSolidColor((uint32)lParam);
		return 0;

	case MYWM_INVALIDATE:
		SyncInvalidate();
		return 0;

	case MYWM_QUEUEPRESENT:
		if (mpMiniDriver) {
			if (mpProfileHook)
				mpProfileHook(kProfileEvent_BeginTick, 0);

			mpMiniDriver->PresentQueued();

			if (mpProfileHook)
				mpProfileHook(kProfileEvent_EndTick, 0);
		}

		SendMessage(mhwnd, MYWM_PROCESSNEXTFRAME, 0, 0);
		return 0;

	case MYWM_SETTOUCHENABLED:
		if (mbTouchEnabled != (wParam != 0)) {
			mbTouchEnabled = (wParam != 0);
			if (wParam) {
				VDRegisterTouchWindowW32(mhwnd, 0);

				if (mhwndChild)
					VDRegisterTouchWindowW32(mhwndChild, 0);
			} else {
				VDUnregisterTouchWindowW32(mhwnd);

				if (mhwndChild)
					VDUnregisterTouchWindowW32(mhwndChild);
			}
		}
		return 0;

	case MYWM_GETMONITORRECT:
		{
			CheckAndRespondToMonitorChange();

			GetMonitorRect((RECT *)lParam, mhLastMonitor);
		}
		return 0;

	case MYWM_SETUSE16BIT:
		mbUse16Bit = (wParam != 0);
		return 0;

	case MYWM_FRAMECAPTURED:
		{
			vdfunction<void(const VDPixmap *)> fn;
			VDPixmapBuffer buf;

			vdsynchronized(mMutex) {
				fn = std::move(mpCaptureFn);
				buf = std::move(mCaptureBuffer);
			}

			if (buf.data)
				fn(&buf);
			else
				fn(nullptr);
		}
		return 0;

	case MYWM_REQUESTCAPTURE:
		if (mpMiniDriver)
			mpMiniDriver->RequestCapture();
		return 0;

	case MYWM_SETCUSTOMREFRESH:
		{
			const float *args = (const float *)lParam;

			if (mCustomRefreshRate != args[0] || mCustomRefreshRateMin != args[1] || mCustomRefreshRateMax != args[2]) {
				mCustomRefreshRate = args[0];
				mCustomRefreshRateMin = args[1];
				mCustomRefreshRateMax = args[2];

				if (mpMiniDriver)
					mpMiniDriver->SetDesiredCustomRefreshRate(mCustomRefreshRate, mCustomRefreshRateMin, mCustomRefreshRateMax);
			}
		}
		return 0;

	case MYWM_GETQUEUEDFRAMES:
		return mPendingFrames.size() + (mpMiniDriver && mpMiniDriver->IsFramePending() ? 1 : 0);

	case MYWM_ISFRAMEPENDING:
		return mpMiniDriver && mpMiniDriver->IsFramePending();

	case WM_SIZE:
		if (mhwndChild)
			SetWindowPos(mhwndChild, NULL, 0, 0, LOWORD(lParam), HIWORD(lParam), SWP_NOMOVE|SWP_NOCOPYBITS|SWP_NOZORDER|SWP_NOACTIVATE);
		break;
	case WM_TIMER:
		if (wParam == mReinitDisplayTimer) {
			SyncInit(true, false);
			if (mpMiniDriver)
				DispatchActiveFrame();
			return 0;
		}
		break;
	case WM_NCHITTEST:
		if (mbIgnoreMouse) {
			LRESULT lr = DefWindowProc(mhwnd, msg, wParam, lParam);

			if (lr != HTCLIENT)
				return lr;
			return HTTRANSPARENT;
		}
		break;
	case WM_SETFOCUS:
		if (mbReturnFocus) {
			HWND hwndParent = GetParent(mhwnd);
			if (hwndParent)
				SetFocus(GetParent(mhwnd));
		}
		break;
	case WM_TOUCH:
		if (HWND hwndParent = ::GetAncestor(mhwnd, GA_PARENT))
			return SendMessage(hwndParent, WM_TOUCH, wParam, lParam);
		break;
	}

	return DefWindowProc(mhwnd, msg, wParam, lParam);
}

void VDVideoDisplayWindow::OnPaint() {
	++mInhibitRefresh;
	bool bDisplayOK = false;

	if (mpMiniDriver) {
		if (mpMiniDriver->IsValid())
			bDisplayOK = true;
		else if (mSource.pixmap.data && mSource.bPersistent && !mpMiniDriver->Update(IVDVideoDisplayMinidriver::kModeNone))
			bDisplayOK = true;
	}

	if (!bDisplayOK) {
		--mInhibitRefresh;
		RequestUpdate();
		++mInhibitRefresh;
	}

	PAINTSTRUCT ps;
	HDC hdc = BeginPaint(mhwnd, &ps);

	if (hdc) {
		RECT r;

		GetClientRect(mhwnd, &r);

		FillRect(hdc, &r, (HBRUSH)(COLOR_3DFACE + 1));
		if (!mMessage.empty()) {
			HGDIOBJ hgo = SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));
			SetBkMode(hdc, TRANSPARENT);
			VDDrawTextW32(hdc, mMessage.data(), mMessage.size(), &r, DT_CENTER | DT_VCENTER | DT_NOPREFIX | DT_WORDBREAK);
			SelectObject(hdc, hgo);
		}

		EndPaint(mhwnd, &ps);
	}

	--mInhibitRefresh;
}

bool VDVideoDisplayWindow::SyncSetSource(bool bAutoUpdate, const SourceInfoEx& params) {
	mCachedImage.clear();

	mSource = params;

	mbSourceUseScreenFX = (params.mpScreenFX != nullptr);
	if (params.mpScreenFX) {
		mSourceScreenFX = *params.mpScreenFX;

		if (mSourceScreenFX.mHDRIntensity < 0)
			mSourceScreenFX.mHDRIntensity = mpManager->GetSystemSDRBrightness(mhLastMonitor) / 80.0f;
	}

	mpSourceScreenFXEngine = params.mpScreenFXEngine;
	mSourceEmulatedFX = mSource;
	mSourceEmulatedFX.pixmap.data = nullptr;

	mMessage.clear();

	if (mpMiniDriver) {
		// Check if a monitor change has occurred (and we care).
		if ((sbEnableSecondaryMonitorDX && !sbEnableMonitorSwitchingDX) || !CheckForMonitorChange()) {
			// Check if the driver sensitive to secondary monitors and if we're now on the secondary
			// monitor.
			if (!mbMiniDriverSecondarySensitive || (!sbEnableMonitorSwitchingDX && !IsOnSecondaryMonitor())) {
				// Check if the driver can adapt to the current format.
				bool useEmulatedFX = !mpMiniDriver->SetScreenFX(mbSourceUseScreenFX ? &mSourceScreenFX : nullptr);

				if (useEmulatedFX) {
					mSourceEmulatedFX = mSource;
					mSourceEmulatedFX.pixmap = mpSourceScreenFXEngine->ApplyScreenFX(mSource.pixmap);
					const VDPixmapFormatInfo& info = VDPixmapGetInfo(mSourceEmulatedFX.pixmap.format);
					mSourceEmulatedFX.bpp = info.qsize >> info.qhbits;
					mSourceEmulatedFX.bpr = (((mSourceEmulatedFX.pixmap.w-1) >> info.qwbits)+1) * info.qsize;

					VDASSERT(mSourceEmulatedFX.pixmap.data);
				} else {
					VDASSERT(mSource.pixmap.data);
				}

				mbSourceEmulatedFXEnabled = useEmulatedFX;
				UpdateCoordinateMapping();

				if (mpMiniDriver->ModifySource(useEmulatedFX ? mSourceEmulatedFX : mSource)) {
					mpMiniDriver->SetColorOverride(0);

					mSource.bAllowConversion = true;

					if (bAutoUpdate)
						SyncUpdate(0);
					return true;
				}
			}
		}

		VDDEBUG_DISP("VideoDisplay: Monitor switch detected -- reinitializing display.");
	}

	SyncReset();
	if (!SyncInit(bAutoUpdate, true))
		return false;

	mSource.bAllowConversion = true;
	return true;
}

void VDVideoDisplayWindow::SyncReset() {
	if (mpMiniDriver) {
		ShutdownMiniDriver();
		VDASSERT(!mpMiniDriver);

		SetPreciseMode(false);
		SetTicksEnabled(false);
	}
}

void VDVideoDisplayWindow::SyncSetSourceMessage(const wchar_t *msg) {
	if (!mpMiniDriver && mMessage == msg)
		return;

	SyncReset();
	ReleaseActiveFrame();
	FlushBuffers();
	mSource.pixmap.format = 0;
	mMessage = msg;
	InvalidateRect(mhwnd, NULL, TRUE);
}

bool VDVideoDisplayWindow::SyncInit(bool bAutoRefresh, bool bAllowNonpersistentSource) {
	if (!mSource.pixmap.data || !mSource.pixmap.format)
		return true;

	VDASSERT(!mpMiniDriver);
	mbMiniDriverSecondarySensitive = false;
	mbMiniDriverClearOtherMonitors = false;

	bool bIsForeground = VDIsForegroundTaskW32();

	CheckForMonitorChange();

	do {
		bool isTermServ = (sbEnableTS || sbEnableTS3D) && VDIsTerminalServicesClient();

		if (!sbEnableTS || !sbEnableTS3D || !isTermServ) {
			if (mAccelMode != kAccelOnlyInForeground || !mSource.bAllowConversion || bIsForeground) {
				// The 3D drivers don't currently support subrects.
				if (sbEnableDX) {
					if (!mbUseSubrect && sbEnable3D && (sbEnableTS3D || !isTermServ)) {
						mpMiniDriver = VDCreateDisplayDriver3D();
						if (InitMiniDriver())
							break;
						SyncReset();
					}

					if (sbEnableSecondaryMonitorDX || sbEnableMonitorSwitchingDX || !IsOnSecondaryMonitor()) {
						if (!mbUseSubrect && sbEnableD3D && (sbEnableTS3D || !isTermServ)) {
							mpMiniDriver = VDCreateVideoDisplayMinidriverDX9(!sbEnableSecondaryMonitorDX || sbEnableMonitorSwitchingDX, sbEnableD3D9Ex);

							if (InitMiniDriver()) {
								mbMiniDriverSecondarySensitive = !sbEnableSecondaryMonitorDX;
								mbMiniDriverClearOtherMonitors = sbEnableSecondaryMonitorDX || sbEnableMonitorSwitchingDX;
								break;
							}

							SyncReset();
						}
					}
				}

			} else {
				VDDEBUG_DISP("VideoDisplay: Application in background -- disabling accelerated preview.");
			}
		}

		mpMiniDriver = VDCreateVideoDisplayMinidriverGDI();
		if (InitMiniDriver())
			break;

		VDDEBUG_DISP("VideoDisplay: No driver was able to handle the requested format! (%d)", mSource.pixmap.format);
		SyncReset();
	} while(false);

	if (mpMiniDriver) {
		mpMiniDriver->SetLogicalPalette(GetLogicalPalette());

		if (mReinitDisplayTimer)
			KillTimer(mhwnd, mReinitDisplayTimer);

		if (bAutoRefresh) {
			if (bAllowNonpersistentSource)
				SyncUpdate(0);
			else
				RequestUpdate();
		}
	}

	return mpMiniDriver != 0;
}

void VDVideoDisplayWindow::SyncInvalidate() {
	if (!mpMiniDriver || !mpCompositor || !mhwndChild)
		return;

	if (!mpMiniDriver->Invalidate())
		InvalidateRect(mhwndChild, NULL, FALSE);
}

void VDVideoDisplayWindow::SyncUpdate(int mode) {
	if (mSource.pixmap.data && !mpMiniDriver) {
		mSyncDelta = 0.0f;
		SyncInit(true, true);
		return;
	}

	if (mpMiniDriver) {
		bool vsync = 0 != (mode & kVSync);
		SetPreciseMode(vsync);
		SetTicksEnabled(vsync && mpMiniDriver->AreVSyncTicksNeeded());

		mpMiniDriver->SetColorOverride(0);

		mSyncDelta = 0.0f;

		bool success = mpMiniDriver->Update((IVDVideoDisplayMinidriver::UpdateMode)mode);
		ReleaseActiveFrame();
		if (success) {
			if (!mInhibitRefresh) {
				mpMiniDriver->Refresh((IVDVideoDisplayMinidriver::UpdateMode)mode);
				mSyncDelta = mpMiniDriver->GetSyncDelta();

				if (mpMiniDriver->IsFramePending())
					SetTicksEnabled(vsync && mpMiniDriver->AreVSyncTicksNeeded());
				else
					RequestNextFrame();
			}
		} else {
			VDDEBUG_DISP("SyncUpdate() failed at Update() call");

			vdsynchronized(mMutex) {
				mIdleFrames.splice(mIdleFrames.back(), mPendingFrames);
			}
		}

		mVSyncStatus = mpMiniDriver->GetVSyncStatus();
	}
}

void VDVideoDisplayWindow::SyncSetFilterMode(FilterMode mode) {
	if (mFilterMode != mode) {
		mFilterMode = mode;

		if (mpMiniDriver) {
			mpMiniDriver->SetFilterMode((IVDVideoDisplayMinidriver::FilterMode)mode);
			InvalidateRect(mhwnd, NULL, FALSE);
			InvalidateRect(mhwndChild, NULL, FALSE);
		}
	}
}

void VDVideoDisplayWindow::SyncSetSolidColor(uint32 color) {
	ReleaseActiveFrame();
	FlushBuffers();

	mSolidColorBuffer = color;

	SourceInfoEx info {};

	info.bAllowConversion	= true;
	info.bPersistent		= true;
	info.bpp				= 4;
	info.bpr				= 4;
	info.use16bit			= mbUse16Bit;
	info.mbHDR				= mbHDR;
	info.mpCB				= this;
	info.pixmap.data		= &mSolidColorBuffer;
	info.pixmap.format		= nsVDPixmap::kPixFormat_XRGB8888;
	info.pixmap.w			= 1;
	info.pixmap.h			= 1;
	info.pixmap.pitch		= 0;

	SyncSetSource(false, info);

	if (mpMiniDriver) {
		mpMiniDriver->SetColorOverride(color);
		InvalidateRect(mhwnd, NULL, FALSE);
		InvalidateRect(mhwndChild, NULL, FALSE);
	}
}

void VDVideoDisplayWindow::OnDisplayChange() {
	GetMonitorRect(&mMonitorRect, mhLastMonitor);

	HPALETTE hPal = GetPalette();
	if (mhOldPalette && !hPal) {
		if (HDC hdc = GetDC(mhwnd)) {
			SelectPalette(hdc, mhOldPalette, FALSE);
			mhOldPalette = 0;
			ReleaseDC(mhwnd, hdc);
		}
	}
	if (!mhOldPalette && hPal) {
		if (HDC hdc = GetDC(mhwnd)) {
			mhOldPalette = SelectPalette(hdc, hPal, FALSE);
			ReleaseDC(mhwnd, hdc);
		}
	}
	
	// Need to check the init lock in case we get an unexpected WM_DISPLAYCHANGE during the
	// minidriver init -- this can happen with the VirtualBox 4.2.18 driver on XP.
	if (!mbFullScreen && !mMinidriverInitLock) {
		mLastMonitorCheckRect = {};
		CheckForMonitorChange();

		if (!mReinitDisplayTimer) {
			SyncReset();
			if (!SyncInit(true, false))
				mReinitDisplayTimer = SetTimer(mhwnd, kReinitDisplayTimerId, 500, NULL);
		}
	}
}

void VDVideoDisplayWindow::OnForegroundChange(bool foreground) {
	if (mAccelMode != kAccelAlways)
		SyncReset();

	OnRealizePalette(foreground);
	UpdateSDRBrightness();
}

void VDVideoDisplayWindow::OnRealizePalette(bool foreground) {
	if (HDC hdc = GetDC(mhwnd)) {
		HPALETTE newPal = GetPalette();
		HPALETTE pal = SelectPalette(hdc, newPal, !foreground);
		if (!mhOldPalette)
			mhOldPalette = pal;
		RealizePalette(hdc);
		RemapPalette();
		ReleaseDC(mhwnd, hdc);

		if (mpMiniDriver) {
			mpMiniDriver->SetLogicalPalette(GetLogicalPalette());

			if (!mpMiniDriver->IsFramePending())
				RequestUpdate();
		}
	}
}

void VDVideoDisplayWindow::OnGlobalSettingsChanged() {
	if (mpMiniDriver)
		mpMiniDriver->SetDisplayDebugInfo(sbEnableDebugInfo);
}

bool VDVideoDisplayWindow::InitMiniDriver() {
	if (mhwndChild) {
		DestroyWindow(mhwndChild);
		mhwndChild = NULL;
	}

	RECT r;
	GetClientRect(mhwnd, &r);
	mhwndChild = CreateWindowEx(WS_EX_NOPARENTNOTIFY, (LPCTSTR)(uintptr)sChildWindowClass, L"", WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS, 0, 0, r.right, r.bottom, mhwnd, NULL, VDGetLocalModuleHandleW32(), this);
	if (!mhwndChild)
		return false;

	if (mbTouchEnabled)
		VDRegisterTouchWindowW32(mhwndChild, 0);

	CheckForMonitorChange();

	GetMonitorRect(&mMonitorRect, mhLastMonitor);

	++mInhibitPaint;
	++mMinidriverInitLock;
	const bool preInitSuccess = mpMiniDriver->PreInit(mhwndChild, mhLastMonitor);
	--mMinidriverInitLock;
	--mInhibitPaint;

	if (!preInitSuccess) {
		DestroyWindow(mhwndChild);
		mhwndChild = NULL;
		return false;
	}

	mbScreenFXSupported = mpMiniDriver->IsScreenFXSupported();

	mpMiniDriver->SetFilterMode((IVDVideoDisplayMinidriver::FilterMode)mFilterMode);
	mpMiniDriver->SetSubrect(mbUseSubrect ? &mSourceSubrect : NULL);
	mpMiniDriver->SetDisplayDebugInfo(sbEnableDebugInfo);
	mpMiniDriver->SetDesiredCustomRefreshRate(mCustomRefreshRate, mCustomRefreshRateMin, mCustomRefreshRateMax);
	mpMiniDriver->SetFullScreen(mbFullScreen, mFullScreenWidth, mFullScreenHeight, mFullScreenRefreshRate, mbUse16Bit);
	mpMiniDriver->SetHighPrecision(sbEnableHighPrecision);
	mpMiniDriver->SetDestRectF(mbDestRectEnabled ? &mDestRectF : NULL, mBackgroundColor);
	mpMiniDriver->SetPixelSharpness(mPixelSharpnessX, mPixelSharpnessY);
	UpdateSDRBrightness();

	VDASSERT(mSource.pixmap.data);

	bool useEmulatedFX = !mpMiniDriver->SetScreenFX(mbSourceUseScreenFX ? &mSourceScreenFX : nullptr);
	if (useEmulatedFX && !mSourceEmulatedFX.pixmap.data) {
		mSourceEmulatedFX = mSource;
		mSourceEmulatedFX.pixmap = mpSourceScreenFXEngine->ApplyScreenFX(mSource.pixmap);

		const VDPixmapFormatInfo& info = VDPixmapGetInfo(mSourceEmulatedFX.pixmap.format);
		mSourceEmulatedFX.bpp = info.qsize >> info.qhbits;
		mSourceEmulatedFX.bpr = (((mSourceEmulatedFX.pixmap.w-1) >> info.qwbits)+1) * info.qsize;

		VDASSERT(mSourceEmulatedFX.pixmap.data);
	}

	mbSourceEmulatedFXEnabled = useEmulatedFX;

	UpdateCoordinateMapping();

	mpMiniDriver->Resize(r.right, r.bottom);

	// The create device hook for PIX for Windows can make COM calls that cause a WM_PAINT to
	// be dispatched on Vista and above. We block the refresh here to prevent reentrancy into
	// the display minidriver.

	++mInhibitPaint;
	++mMinidriverInitLock;
	bool success = mpMiniDriver->Init(mhwndChild, mhLastMonitor, useEmulatedFX ? mSourceEmulatedFX : mSource);
	--mMinidriverInitLock;
	--mInhibitPaint;

	if (!success) {
		DestroyWindow(mhwndChild);
		mhwndChild = NULL;
		return false;
	}
	
	mHDRAvailability = mpMiniDriver->GetHDRAvailability();

	if (mHDRAvailability == VDDHighColorAvailability::NotEnabledOnDisplay && !mpManager->IsMonitorHDRCapable(mhLastMonitor))
		mHDRAvailability = VDDHighColorAvailability::NoDisplaySupport;

	mWCGAvailability = mpMiniDriver->GetWCGAvailability();

	// WCG cannot be detected via DXGI, so we must cross-check enabled status with DisplayConfig
	if (mWCGAvailability == VDDHighColorAvailability::Available && !mpManager->IsMonitorWCGEnabled(mhLastMonitor))
		mWCGAvailability = VDDHighColorAvailability::NotEnabledOnDisplay;

	if (mWCGAvailability == VDDHighColorAvailability::NotEnabledOnDisplay && !mpManager->IsMonitorWCGCapable(mhLastMonitor))
		mWCGAvailability = VDDHighColorAvailability::NoDisplaySupport;

	// Must be done after Init().
	mpMiniDriver->SetCompositor(mpCompositor);

	bool needCapture = false;
	vdsynchronized(mMutex) {
		needCapture = (mpCaptureFn != nullptr);
	}

	if (needCapture)
		mpMiniDriver->RequestCapture();

	return true;
}

void VDVideoDisplayWindow::ShutdownMiniDriver() {
	if (mpMiniDriver) {
		// prevent recursion due to messages being triggered by Direct3D
		IVDVideoDisplayMinidriver *pMiniDriver = mpMiniDriver;
		mpMiniDriver = NULL;
		pMiniDriver->Shutdown();
		delete pMiniDriver;
	}

	if (mhwndChild) {
		DestroyWindow(mhwndChild);
		mhwndChild = NULL;
	}
}

void VDVideoDisplayWindow::RequestUpdate() {
	if (mpLastFrame) {
		if (!mpActiveFrame) {
			VDASSERT(!mpMiniDriver || !mpMiniDriver->IsFramePending());
			mpActiveFrame = mpLastFrame;
			mpLastFrame = NULL;

			DispatchActiveFrame();
		}
	} else if (mpCB)
		mpCB->DisplayRequestUpdate(this);
	else if (mSource.pixmap.data && mSource.bPersistent) {
		SyncUpdate(0);
	}
}

void VDVideoDisplayWindow::VerifyDriverResult(bool result) {
	if (!result) {
		VDDEBUG_DISP("VerifyDriverResult() failed.");

		if (mpMiniDriver) {
			ShutdownMiniDriver();
		}

		if (!mReinitDisplayTimer)
			mReinitDisplayTimer = SetTimer(mhwnd, kReinitDisplayTimerId, 500, NULL);

		vdsynchronized(mMutex) {
			mIdleFrames.splice(mIdleFrames.end(), mPendingFrames);
		}
	}
}

void VDVideoDisplayWindow::UpdateCoordinateMapping() {
	if (!mbSourceUseScreenFX || mbSourceEmulatedFXEnabled || mSourceScreenFX.mDistortionX == 0.0f) {
		mbDistortionMappingValid = false;
	} else {
		float destAspect = 1;

		if (mbDestRectEnabled) {
			if (!mDestRectF.empty())
				destAspect = mDestRectF.width() / mDestRectF.height();
		} else {
			RECT r;
			if (mhwndChild && GetClientRect(mhwndChild, &r) && r.right > 0 && r.bottom > 0)
				destAspect = (float)r.right / (float)r.bottom;
		}

		mbDistortionMappingValid = true;
		mDistortionMapping.Init(mSourceScreenFX.mDistortionX, mSourceScreenFX.mDistortionYRatio, destAspect);
	}
}

bool VDVideoDisplayWindow::CheckForMonitorChange() {
	HMONITOR hmon = NULL;

	RECT r;
	if (!GetWindowRect(mhwnd, &r))
		return false;

	// As a (mostly safe) optimization, don't check if we haven't moved. Note that
	// we are checking the window rect in screen space and not in positioning space.
	if (!memcmp(&r, &mLastMonitorCheckRect, sizeof r))
		return false;

	mLastMonitorCheckRect = r;

	HWND hwndTest = mhwnd; 

	while(GetWindowLong(hwndTest, GWL_STYLE) & WS_CHILD) {
		HWND hwndParent = GetParent(hwndTest);
		if (!hwndParent)
			break;

		RECT rParent;
		GetWindowRect(hwndParent, &rParent);

		if (r.left  < rParent.left)  r.left  = rParent.left;
		if (r.right < rParent.left)  r.right = rParent.left;
		if (r.left  > rParent.right) r.left  = rParent.right;
		if (r.right > rParent.right) r.right = rParent.right;
		if (r.top    < rParent.top)    r.top    = rParent.top;
		if (r.bottom < rParent.top)    r.bottom = rParent.top;
		if (r.top    > rParent.bottom) r.top    = rParent.bottom;
		if (r.bottom > rParent.bottom) r.bottom = rParent.bottom;

		hwndTest = hwndParent;
	}

	hmon = MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST);
	if (hmon == mhLastMonitor)
		return false;

	VDDEBUG_DISP("VideoDisplay: Current monitor update: %p -> %p.", mhLastMonitor, hmon);

	mhLastMonitor = hmon;
	return true;
}

void VDVideoDisplayWindow::CheckAndRespondToMonitorChange() {
	if ((!sbEnableSecondaryMonitorDX || sbEnableMonitorSwitchingDX) && CheckForMonitorChange())
		SyncReset();
}

bool VDVideoDisplayWindow::IsOnSecondaryMonitor() const {
	if (!mhLastMonitor)
		return false;

	MONITORINFO monInfo = {sizeof(MONITORINFO)};
	if (!GetMonitorInfoW(mhLastMonitor, &monInfo))
		return false;

	return !(monInfo.dwFlags & MONITORINFOF_PRIMARY);
}

void VDVideoDisplayWindow::GetMonitorRect(RECT *r, HMONITOR hmon) {
	if (hmon) {
		MONITORINFO monInfo = {sizeof(MONITORINFO)};
		if (GetMonitorInfoW(hmon, &monInfo)) {
			*r = monInfo.rcMonitor;
			return;
		}
	}

	r->left = 0;
	r->top = 0;
	r->right = GetSystemMetrics(SM_CXSCREEN);
	r->bottom = GetSystemMetrics(SM_CYSCREEN);
}

void VDVideoDisplayWindow::UpdateSDRBrightness() {
	if (mpMiniDriver)
		mpMiniDriver->SetSDRBrightness(mSDRBrightness < 0 ? mpManager->GetSystemSDRBrightness(mhLastMonitor) : mSDRBrightness);
}

///////////////////////////////////////////////////////////////////////////////

VDGUIHandle VDCreateDisplayWindowW32(uint32 dwExFlags, uint32 dwFlags, int x, int y, int width, int height, VDGUIHandle hwndParent) {
	return (VDGUIHandle)CreateWindowEx(dwExFlags, g_wszVideoDisplayControlName, L"", dwFlags, x, y, width, height, (HWND)hwndParent, nullptr, VDGetLocalModuleHandleW32(), nullptr);
}
