//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2012 Avery Lee
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

#include <stdafx.h>
#include <windows.h>
#include <combaseapi.h>
#include <UIAutomation.h>
#include <vd2/system/file.h>
#include <vd2/system/math.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/w32assist.h>
#include <vd2/system/win32/touch.h>
#include <vd2/VDDisplay/display.h>
#include <at/atcore/configvar.h>
#include <at/atcore/devicevideo.h>
#include <at/atcore/logging.h>
#include <at/atcore/profile.h>
#include <at/atui/accessibility.h>
#include <at/atui/uimanager.h>
#include <at/atui/uimenulist.h>
#include <at/atui/uicontainer.h>
#include <at/atuicontrols/uitextedit.h>
#include <at/atuicontrols/uibutton.h>
#include <at/atuicontrols/uilistview.h>
#include <at/atnativeui/accessibility_win32.h>
#include <at/atnativeui/nativeevents.h>
#include <at/atnativeui/messageloop.h>
#include <at/atnativeui/uiframe.h>
#include <at/atnativeui/uipanewindow.h>
#include "console.h"
#include "devicemanager.h"
#include "inputdefs.h"
#include "inputmanager.h"
#include "oshelper.h"
#include "simulator.h"
#include "uidisplay.h"
#include "uidisplayaccessibility.h"
#include "uidisplayaccessibility_win32.h"
#include "uienhancedtext.h"
#include "uirender.h"
#include "uitypes.h"
#include "uicaptionupdater.h"
#include "uimenu.h"
#include "uiportmenus.h"
#include "uifilebrowser.h"
#include "uivideodisplaywindow.h"
#include "uimessagebox.h"
#include "uiqueue.h"
#include "uiaccessors.h"
#include "uicommondialogs.h"
#include "uiprofiler.h"
#include "resource.h"
#include "options.h"
#include "decode_png.h"
#include "virtualscreen.h"

///////////////////////////////////////////////////////////////////////////

void ATUIUpdateNativeProfiler();

extern ATSimulator g_sim;

extern IVDVideoDisplay *g_pDisplay;

extern bool g_ATAutoFrameFlipping;
extern LOGFONTW g_enhancedTextFont;

extern ATDisplayFilterMode g_dispFilterMode;
extern float g_dispCustomRefreshRate;
extern int g_dispFilterSharpness;

extern ATDisplayStretchMode g_displayStretchMode;
extern float g_displayZoom;
extern vdfloat2 g_displayPan;

ATLogChannel g_ATLCHostKeys(false, false, "HOSTKEYS", "Host keyboard activity");
ATLogChannel g_ATLCHostUI(false, false, "HOSTUI", "Host UI debug output");

ATConfigVarBool g_ATCVUIShowNativeProfiler("ui.show_native_profiler", false, ATUIUpdateNativeProfiler);

ATUIManager g_ATUIManager;

ATUIManager& ATUIGetManager() {
	return g_ATUIManager;
}

void OnCommandEditPasteText();

void ATUIFlushDisplay();
void ATUIResizeDisplay();

void ATUISetDragDropSubTarget(VDGUIHandle h, ATUIManager *mgr);

///////////////////////////////////////////////////////////////////////////

ATUIVideoDisplayWindow *g_pATVideoDisplayWindow;
bool g_dispPadIndicators = false;

void ATUIOpenOnScreenKeyboard() {
	if (g_pATVideoDisplayWindow)
		g_pATVideoDisplayWindow->OpenOSK();
}

void ATUIToggleHoldKeys() {
	if (g_pATVideoDisplayWindow)
		g_pATVideoDisplayWindow->ToggleHoldKeys();
}

void ATUIRecalibrateLightPen() {
	if (g_pATVideoDisplayWindow)
		g_pATVideoDisplayWindow->RecalibrateLightPen();
}

void ATUIActivatePanZoomTool() {
	if (g_pATVideoDisplayWindow)
		g_pATVideoDisplayWindow->ActivatePanZoomTool();
}

bool ATUIGetDisplayPadIndicators() {
	return g_dispPadIndicators;
}

void ATUISetDisplayPadIndicators(bool enabled) {
	if (g_dispPadIndicators != enabled) {
		g_dispPadIndicators = enabled;

		ATUIResizeDisplay();
	}
}

bool ATUIGetDisplayIndicators() {
	IATUIRenderer *r = g_sim.GetUIRenderer();
	return r->IsVisible();
}

void ATUISetDisplayIndicators(bool enabled) {
	IATUIRenderer *r = g_sim.GetUIRenderer();
	r->SetVisible(enabled);

	if (g_dispPadIndicators)
		ATUIResizeDisplay();
}

#ifndef MOUSEEVENTF_MASK
	#define MOUSEEVENTF_MASK 0xFFFFFF00
#endif

#ifndef MOUSEEVENTF_FROMTOUCH
	#define MOUSEEVENTF_FROMTOUCH 0xFF515700
#endif

namespace {
	bool IsInjectedTouchMouseEvent() {
		// No WM_TOUCH prior to Windows 7.
		if (!VDIsAtLeast7W32())
			return false;

		// Recommended by MSDN. Seriously. Bit 7 is to distinguish pen events from
		// touch events.
		return (GetMessageExtraInfo() & (MOUSEEVENTF_MASK | 0x80)) == (MOUSEEVENTF_FROMTOUCH | 0x80);

	}
}

///////////////////////////////////////////////////////////////////////////

class ATDisplayImageDecoder final : public IVDDisplayImageDecoder {
public:
	bool DecodeImage(VDPixmapBuffer& buf, VDBufferedRandomAccessStream& stream) override;
} gATDisplayImageDecoder;

bool ATDisplayImageDecoder::DecodeImage(VDPixmapBuffer& buf, VDBufferedRandomAccessStream& stream) {
	vdautoptr<IVDImageDecoderPNG> dec { VDCreateImageDecoderPNG() };

	sint64 len = stream.Length();
	if (len > 64*1024*1024)
		return false;

	vdblock<uint8> rawbuf(len);
	stream.Read(rawbuf.data(), len);
	auto err = dec->Decode(rawbuf.data(), len);

	if (err != kPNGDecodeOK)
		return false;

	buf.assign(dec->GetFrameBuffer());
	return true;
}

///////////////////////////////////////////////////////////////////////////

struct ATUIStepDoModalWindow final : public vdrefcount {
	ATUIStepDoModalWindow()
		: mbModalWindowActive(true)
	{
	}

	void Run() {
		if (mbModalWindowActive) {
			auto thisptr = vdmakerefptr(this);
			ATUIPushStep([thisptr]() { thisptr->Run(); });

			int rc;
			ATUIProcessMessages(true, rc);
		}
	}

	bool mbModalWindowActive;
};

class ATUINativeDisplay final : public IATUINativeDisplay, public IATUIClipboard {
public:
	ATUINativeDisplay()
		: mhcurTarget((HCURSOR)LoadImage(VDGetLocalModuleHandleW32(), MAKEINTRESOURCE(IDC_TARGET), IMAGE_CURSOR, 0, 0, LR_SHARED))
		, mhcurTargetOff((HCURSOR)LoadImage(VDGetLocalModuleHandleW32(), MAKEINTRESOURCE(IDC_TARGET_OFF), IMAGE_CURSOR, 0, 0, LR_SHARED))
	{
	}

	~ATUINativeDisplay() {
		if (mTimerId)
			::KillTimer(nullptr, mTimerId);
	}

	void Invalidate() override {
		if (!g_pDisplay)
			return;

		if (mbIgnoreAutoFlipping || (!g_ATAutoFrameFlipping || mModalCount))
			g_pDisplay->Invalidate();
	}

	void ConstrainCursor(bool constrain) override {
		UpdateCursorConstraint(mbMouseConstrainedByUI, constrain);
	}

	void CaptureCursor(bool motionMode) override {
		if (!mbMouseCaptured) {
			if (mhwnd)
				::SetCapture(mhwnd);

			mbMouseCaptured = true;
		}

		if (mbMouseMotionMode != motionMode) {
			UpdateCursorConstraint(mbMouseMotionMode, motionMode);

			if (motionMode ? mbRawInputEnabled : mbRawInputActive) {
				if (motionMode)
					EnableRawInput();
				else
					DisableRawInput();
			}

			if (motionMode)
				WarpCapturedMouse();
		}
	}

	void ReleaseCursor() override {
		mbMouseCaptured = false;
		mbMouseConstrainedByUI = false;

		UpdateCursorConstraint(mbMouseMotionMode, false);

		DisableRawInput();

		if (mhwnd)
			::ReleaseCapture();
	}

	vdpoint32 GetCursorPosition() override {
		if (!mhwnd)
			return vdpoint32(0, 0);

		DWORD pos = GetMessagePos();
		POINT pt = { (short)LOWORD(pos), (short)HIWORD(pos) };

		::ScreenToClient(mhwnd, &pt);

		return vdpoint32(pt.x, pt.y);
	}

	void SetCursorImage(uint32 id) override {
		if (mbCursorImageChangesEnabled)
			SetCursorImageDirect(id);
	}

	void *BeginModal() override {
		ATActivateUIPane(kATUIPaneId_Display, true);

		ATUIStepDoModalWindow *step = new ATUIStepDoModalWindow;
		step->AddRef();

		auto stepptr = vdmakerefptr(step);
		ATUIPushStep([stepptr]() { stepptr->Run(); });

		if (!mModalCount++) {
			ATUISetMenuEnabled(false);

			if (mpFrame)
				mpFrame->GetContainer()->SetModalFrame(mpFrame);
		}

		ATUIFlushDisplay();

		return step;
	}

	void EndModal(void *cookie) override {
		VDASSERT(mModalCount);

		if (!--mModalCount) {
			if (mpFrame)
				mpFrame->GetContainer()->SetModalFrame(NULL);

			ATUISetMenuEnabled(true);
		}

		ATUIStepDoModalWindow *step = (ATUIStepDoModalWindow *)cookie;
		step->mbModalWindowActive = false;
		step->Release();
	}

	bool IsKeyDown(uint32 vk) override {
		return GetKeyState(vk) < 0;
	}

	IATUIClipboard *GetClipboard() override {
		return this;
	}

	void SetTimer(float delay) override {
		if (mTimerId)
			::KillTimer(nullptr, mTimerId);

		mTimerId = ::SetTimer(nullptr, 0, VDRoundToInt(delay * 1000.0f), StaticOnTimer);
	}

public:
	void CopyText(const wchar_t *s) override {
		if (mhwnd)
			ATCopyTextToClipboard(mhwnd, s);
	}

public:
	bool IsMouseCaptured() const { return mbMouseCaptured; }
	bool IsMouseActuallyConstrained() const { return mbMouseActuallyConstrained; }
	bool IsMouseMotionModeEnabled() const { return mbMouseMotionMode; }

	void SetDisplay(HWND hwnd, ATFrameWindow *frame) {
		if (mpFrame && mModalCount)
			mpFrame->GetContainer()->SetModalFrame(NULL);

		mhwnd = hwnd;
		mpFrame = frame;

		if (mpFrame && mModalCount)
			mpFrame->GetContainer()->SetModalFrame(mpFrame);
	}

	vdpoint32 WarpCapturedMouse() {
		RECT r;
		if (!mhwnd || !::GetClientRect(mhwnd, &r))
			return vdpoint32(0, 0);

		const vdpoint32 pt(r.right >> 1, r.bottom >> 1);

		POINT pt2 = {pt.x, pt.y};
		::ClientToScreen(mhwnd, &pt2);
		::SetCursorPos(pt2.x, pt2.y);

		return pt;
	}

	void OnCaptureLost() {
		mbMouseCaptured = false;
		mbMouseMotionMode = false;
		mbMouseConstrainedByUI = false;

		EndConstrainingCursor();
	}

	void SetCursorImageChangesEnabled(bool enabled) {
		mbCursorImageChangesEnabled = enabled;
	}

	void SetCursorImageDirect(uint32 id) {
		switch(id) {
			case kATUICursorImage_Hidden:
				::SetCursor(NULL);
				break;

			case kATUICursorImage_Arrow:
				::SetCursor(::LoadCursor(NULL, IDC_ARROW));
				break;

			case kATUICursorImage_IBeam:
				::SetCursor(::LoadCursor(NULL, IDC_IBEAM));
				break;

			case kATUICursorImage_Cross:
				::SetCursor(::LoadCursor(NULL, IDC_CROSS));
				break;

			case kATUICursorImage_Query:
				::SetCursor(::LoadCursor(NULL, IDC_HELP));
				break;

			case kATUICursorImage_Move:
				::SetCursor(::LoadCursor(NULL, IDC_SIZEALL));
				break;

			case kATUICursorImage_SizeHoriz:
				::SetCursor(::LoadCursor(NULL, IDC_SIZEWE));
				break;

			case kATUICursorImage_SizeVert:
				::SetCursor(::LoadCursor(NULL, IDC_SIZENS));
				break;

			case kATUICursorImage_SizeDiagFwd:
				::SetCursor(::LoadCursor(NULL, IDC_SIZENESW));
				break;

			case kATUICursorImage_SizeDiagRev:
				::SetCursor(::LoadCursor(NULL, IDC_SIZENWSE));
				break;

			case kATUICursorImage_Target:
				::SetCursor(mhcurTarget);
				break;

			case kATUICursorImage_TargetOff:
				::SetCursor(mhcurTargetOff);
				break;
		}
	}

	void SetIgnoreAutoFlipping(bool ignore) {
		if (mbIgnoreAutoFlipping != ignore) {
			mbIgnoreAutoFlipping = ignore;

			// We need to issue an invalidation when turning on this setting in case the manager suppressed an invalidation
			// that it expected to be handled by the auto-flip. (This doesn't need to happen for pause, where we explicitly
			// check for this case.)
			if (ignore)
				Invalidate();
		}
	}

	bool GetConstrainCursorByFullScreen() const { return mbMouseConstrainedByFullScreen; }
	void SetConstrainCursorByFullScreen(bool enabled);

	void SetMouseConstraintEnabled(bool enabled);
	void SetRawInputEnabled(bool enabled) { mbRawInputEnabled = enabled; }
	bool IsRawInputEnabled() const { return mbRawInputEnabled; }

	void SetFullScreen(bool enabled);

private:
	static void CALLBACK StaticOnTimer(HWND, UINT, UINT_PTR, DWORD);

	void UpdateCursorConstraint(bool& state, bool newValue);
	bool ShouldConstrainCursor() const;
	void BeginConstrainingCursor();
	void EndConstrainingCursor();

	void EnableRawInput();
	void DisableRawInput();

	HWND mhwnd = nullptr;
	ATFrameWindow *mpFrame = nullptr;
	uint32 mModalCount = 0;
	bool mbMouseCaptured = false;
	bool mbMouseActuallyConstrained = false;
	bool mbMouseConstrainedByUI = false;
	bool mbMouseConstrainedByFullScreen = false;
	bool mbMouseConstraintEnabled = false;
	bool mbMouseMotionMode = false;
	bool mbRawInputEnabled = false;
	bool mbRawInputActive = false;
	bool mbCursorImageChangesEnabled = false;
	bool mbIgnoreAutoFlipping = false;
	bool mbFullScreen = false;
	HCURSOR	mhcurTarget = nullptr;
	HCURSOR	mhcurTargetOff = nullptr;
	UINT_PTR mTimerId = 0;
} g_ATUINativeDisplay;

void ATUINativeDisplay::SetConstrainCursorByFullScreen(bool enabled) {
	UpdateCursorConstraint(mbMouseConstrainedByFullScreen, enabled);
}

void ATUINativeDisplay::SetMouseConstraintEnabled(bool enabled) {
	UpdateCursorConstraint(mbMouseConstraintEnabled, enabled);
}

void ATUINativeDisplay::SetFullScreen(bool enabled) {
	UpdateCursorConstraint(mbFullScreen, enabled);
}

void CALLBACK ATUINativeDisplay::StaticOnTimer(HWND, UINT, UINT_PTR, DWORD) {
	if (g_ATUINativeDisplay.mTimerId) {
		::KillTimer(nullptr, g_ATUINativeDisplay.mTimerId);
		g_ATUINativeDisplay.mTimerId = 0;
	}

	g_ATUIManager.OnTimer();
}

void ATUINativeDisplay::UpdateCursorConstraint(bool& state, bool newValue) {
	if (state != newValue) {
		const bool oldShouldConstrain = ShouldConstrainCursor();
		
		state = newValue;

		const bool newShouldConstrain = ShouldConstrainCursor();

		if (oldShouldConstrain != newShouldConstrain) {
			if (newShouldConstrain)
				BeginConstrainingCursor();
			else
				EndConstrainingCursor();
		}
	}
}

bool ATUINativeDisplay::ShouldConstrainCursor() const {
	if (mbMouseMotionMode)
		return true;

	if (mbMouseConstraintEnabled) {
		if (mbMouseConstrainedByUI)
			return true;

		if (mbMouseConstrainedByFullScreen && mbFullScreen)
			return true;
	}

	return false;
}

void ATUINativeDisplay::BeginConstrainingCursor() {
	if (!mhwnd)
		return;

	RECT r;

	if (::GetClientRect(mhwnd, &r)) {
		::MapWindowPoints(mhwnd, NULL, (LPPOINT)&r, 2);
		::ClipCursor(&r);

		mbMouseActuallyConstrained = true;
	}
}

void ATUINativeDisplay::EndConstrainingCursor() {
	::ClipCursor(nullptr);
	mbMouseActuallyConstrained = false;
}

void ATUINativeDisplay::EnableRawInput() {
	if (mbRawInputActive)
		return;

	mbRawInputActive = true;

	RAWINPUTDEVICE rid {};
	rid.usUsagePage = 1;
	rid.usUsage = 2;
	rid.hwndTarget = mhwnd;
	rid.dwFlags = 0;
	RegisterRawInputDevices(&rid, 1, sizeof(RAWINPUTDEVICE));
}

void ATUINativeDisplay::DisableRawInput() {
	if (!mbRawInputActive)
		return;

	mbRawInputActive = false;

	RAWINPUTDEVICE rid {};
	rid.usUsagePage = 1;
	rid.usUsage = 2;
	rid.hwndTarget = mhwnd;
	rid.dwFlags = RIDEV_REMOVE;
	RegisterRawInputDevices(&rid, 1, sizeof(RAWINPUTDEVICE));
}

///////////////////////////////////////////////////////////////////////////

void ATUISetRawInputEnabled(bool enabled) {
	g_ATUINativeDisplay.SetRawInputEnabled(enabled);
}

bool ATUIGetRawInputEnabled() {
	return g_ATUINativeDisplay.IsRawInputEnabled();
}

bool ATUIGetConstrainMouseFullScreen() {
	return g_ATUINativeDisplay.GetConstrainCursorByFullScreen();
}

void ATUISetConstrainMouseFullScreen(bool enabled) {
	g_ATUINativeDisplay.SetConstrainCursorByFullScreen(enabled);
}


void ATUIUpdateThemeScaleCallback(ATOptions& opts, const ATOptions *prevOpts, void *) {
	sint32 scale = opts.mThemeScale;

	if (scale < 10)
		scale = 10;

	if (scale > 1000)
		scale = 1000;

	g_ATUIManager.SetThemeScaleFactor((float)opts.mThemeScale / 100.0f);
}

void ATUIUpdateNativeProfiler() {
	if (g_pATVideoDisplayWindow) {
		if (g_ATCVUIShowNativeProfiler)
			ATUIProfileCreateWindow(&g_ATUIManager);
		else
			ATUIProfileDestroyWindow();
	}
}

void ATUIInitManager() {
	ATOptionsAddUpdateCallback(true, ATUIUpdateThemeScaleCallback);
	g_ATUIManager.Init(&g_ATUINativeDisplay);

	VDDisplaySetImageDecoder(&gATDisplayImageDecoder);
	g_pATVideoDisplayWindow = new ATUIVideoDisplayWindow;
	g_pATVideoDisplayWindow->AddRef();

	ATDeviceManager& devMgr = *g_sim.GetDeviceManager();
	g_ATUIManager.GetMainWindow()->AddChild(g_pATVideoDisplayWindow);
	g_pATVideoDisplayWindow->Init(*g_sim.GetEventManager(), *devMgr.GetService<IATDeviceVideoManager>());
	g_pATVideoDisplayWindow->SetPlacementFill();

	g_sim.GetUIRenderer()->SetUIManager(&g_ATUIManager);

	g_pATVideoDisplayWindow->Focus();

	ATUIUpdateNativeProfiler();
}

void ATUIShutdownManager() {
	ATUIProfileDestroyWindow();

	vdsaferelease <<= g_pATVideoDisplayWindow;

	g_sim.GetUIRenderer()->SetUIManager(NULL);

	g_ATUIManager.Shutdown();
}

void ATUITriggerButtonDown(uint32 vk) {
	ATUIKeyEvent event;
	event.mVirtKey = vk;
	event.mExtendedVirtKey = vk;
	event.mbIsRepeat = false;
	event.mbIsExtendedKey = false;
	g_ATUIManager.OnKeyDown(event);
}

void ATUITriggerButtonUp(uint32 vk) {
	ATUIKeyEvent event;
	event.mVirtKey = vk;
	event.mExtendedVirtKey = vk;
	event.mbIsRepeat = false;
	event.mbIsExtendedKey = false;
	g_ATUIManager.OnKeyUp(event);
}

void ATUIFlushDisplay() {
	if (g_ATUIManager.IsInvalidated()) {
		if (g_pDisplay)
			g_pDisplay->Invalidate();
	}
}

bool ATUICloseActiveModals() {
	for(;;) {
		auto *w = g_ATUIManager.GetModalWindow();

		if (!w)
			return true;

		if (w->Close() != ATUICloseResult::Success)
			return false;
	}
}

bool ATUIIsActiveModal() {
	return g_ATUIManager.GetModalWindow() != NULL;
}

class ATDisplayPane final : public ATUIPaneWindow, public IATDisplayPane, public IATUIDisplayAccessibilityCallbacksW32 {
public:
	ATDisplayPane();
	~ATDisplayPane();

	void *AsInterface(uint32 iid) override;

	void ReleaseMouse() override;
	void ToggleCaptureMouse() override;
	void ResetDisplay() override;

	bool IsTextSelected() const override { return g_pATVideoDisplayWindow->IsTextSelected(); }
	void SelectAll() override;
	void Deselect() override;
	void Copy(ATTextCopyMode copyMode) override;
	void CopyFrame(bool trueAspect) override;
	bool CopyFrameImage(bool trueAspect, VDPixmapBuffer& buf) override;
	void SaveFrame(bool trueAspect, const wchar_t *path = nullptr) override;
	void Paste();
	void Paste(const wchar_t *s, size_t len) override;

	void OnSize() override;
	void UpdateFilterMode() override;
	void UpdateCustomRefreshRate() override;
	void RequestRenderedFrame(vdfunction<void(const VDPixmap *)> fn) override;
	void SetVideoWriter(IATVideoWriter *writer) override;

	void SetAutoSuggestEnabled(bool enabled) override;
	void ShowSuggestions() override;

public:		// IATUIDisplayAccessibilityCallbacksW32
	vdpoint32 GetNearestBeamPositionForScreenPoint(sint32 x, sint32 y) const override;
	vdrect32 GetTextSpanBoundingBox(int ypos, int xc1, int xc2) const override;

protected:
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam) override;
	virtual ATUITouchMode GetTouchModeAtPoint(const vdpoint32& pt) const override;

	bool OnCreate() override;
	void OnDestroy() override;
	void OnMouseMove(WPARAM wParam, LPARAM lParam);
	void OnMouseHover(WPARAM wParam);

	ATUIKeyEvent ConvertVirtKeyEvent(WPARAM wParam, LPARAM lParam);

	void SetHaveMouse();
	void WarpCapturedMouse();
	void UpdateTextDisplay(bool enabled) override;
	void UpdateTextModeFont() override;
	void UpdateAccessibilityProvider();

	void OnMenuActivating(ATUIMenuList *);
	void OnMenuItemSelected(ATUIMenuList *sender, uint32 id);
	void OnAllowContextMenu();
	void OnDisplayContextMenu(const vdpoint32& pt);
	void OnOSKChange();
	void ResizeDisplay();

	HWND	mhwndDisplay = nullptr;
	HMENU	mhmenuContext = nullptr;
	IVDVideoDisplay *mpDisplay = nullptr;
	bool	mbDestroying = false;
	bool	mbLastTrackMouseValid = false;
	int		mLastTrackMouseX = 0;
	int		mLastTrackMouseY = 0;
	int		mMouseCursorLock = 0;

	vdstructex<RAWINPUT> mRawInputBuffer;

	int		mWidth = 0;
	int		mHeight = 0;

	vdpoint32	mGestureOrigin = { 0, 0 };

	vdrect32f	mDisplayRectF = { 0, 0, 0, 0 };

	bool	mbTextModeEnabled = false;
	bool	mbTextModeVirtScreen = false;
	bool	mbHaveMouse = false;
	bool	mbEatNextInjectedCaps = false;
	bool	mbTurnOffCaps = false;
	bool	mbAllowContextMenu = false;

	enum {
		kDblTapState_Initial,
		kDblTapState_WaitDown1,
		kDblTapState_WaitDown2,
		kDblTapState_WaitUp2,
		kDblTapState_Invalid
	} mDblTapState = kDblTapState_Initial;

	DWORD	mDblTapTime = 0;
	LONG	mDblTapX = 0;
	LONG	mDblTapY = 0;
	LONG	mDblTapThreshold = 0;

	vdautoptr<IATUIEnhancedTextEngine> mpEnhTextEngine;
	vdrefptr<ATUIMenuList> mpMenuBar;

	vdfunction<void()> mIndicatorSafeAreaChangedFn;

	TOUCHINPUT mRawTouchInputs[32] {};
	ATUITouchInput mTouchInputs[32] {};

	vdrefptr<IATUIDisplayAccessibilityProviderW32> mpAccessibilityProvider;
	vdrefptr<ATUIDisplayAccessibilityScreen> mpAccessibilityCurrentScreen;
	vdrefptr<ATUIDisplayAccessibilityScreen> mpAccessibilityNextScreen;
};

ATDisplayPane::ATDisplayPane()
	: ATUIPaneWindow(kATUIPaneId_Display, L"Display")
	, mhmenuContext(LoadMenu(NULL, MAKEINTRESOURCE(IDR_DISPLAY_CONTEXT_MENU)))
{
	SetTouchMode(kATUITouchMode_Dynamic);

	mPreferredDockCode = kATContainerDockCenter;
}

ATDisplayPane::~ATDisplayPane() {
	if (mhmenuContext)
		DestroyMenu(mhmenuContext);
}

void *ATDisplayPane::AsInterface(uint32 iid) {
	if (iid == IATDisplayPane::kTypeID)
		return static_cast<IATDisplayPane *>(this);

	return ATUIPane::AsInterface(iid);
}

vdpoint32 ATDisplayPane::GetNearestBeamPositionForScreenPoint(sint32 x, sint32 y) const {
	vdpoint32 cpt = TransformScreenToClient(vdpoint32(x, y));

	return g_pATVideoDisplayWindow->GetNearestBeamPositionForPoint(vdpoint32(x, y));
}

vdrect32 ATDisplayPane::GetTextSpanBoundingBox(int ypos, int xc1, int xc2) const {
	vdrect32 r = g_pATVideoDisplayWindow->GetTextSpanBoundingBox(ypos, xc1, xc2);

	return TransformClientToScreen(r);
}

LRESULT ATDisplayPane::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case ATWM_PRESYSKEYDOWN:
		case ATWM_PREKEYDOWN:
			if (mbEatNextInjectedCaps && LOWORD(wParam) == VK_CAPITAL) {
				// drop injected CAPS LOCK keys
				if (!(lParam & 0x00ff0000)) {
					mbEatNextInjectedCaps = false;
					return true;
				}
			}

			{
				bool result = g_ATUIManager.OnKeyDown(ConvertVirtKeyEvent(wParam, lParam));

				if (g_ATLCHostKeys.IsEnabled()) {
					g_ATLCHostKeys("Received host vkey down: VK=$%02X LP=%08X MOD=%c%c%c-%c%c%c (current key mask: %016llX)%s\n"
						, LOWORD(wParam)
						, (unsigned)lParam
						, GetKeyState(VK_LCONTROL) < 0 ? 'C' : '.'
						, GetKeyState(VK_LSHIFT) < 0 ? 'S' : '.'
						, GetKeyState(VK_LMENU) < 0 ? 'A' : '.'
						, GetKeyState(VK_RCONTROL) < 0 ? 'C' : '.'
						, GetKeyState(VK_RSHIFT) < 0 ? 'S' : '.'
						, GetKeyState(VK_RMENU) < 0 ? 'A' : '.'
						, g_sim.GetPokey().GetRawKeyMask()
						, (HIWORD(lParam) & KF_REPEAT) != 0 ? " (repeat)" : ""
					);
				}

				if (result) {
					if (LOWORD(wParam) == VK_CAPITAL)
						mbTurnOffCaps = true;

					return true;
				}
			}
			return false;


		case ATWM_PRESYSKEYUP:
		case ATWM_PREKEYUP:
			if (LOWORD(wParam) == VK_CAPITAL && mbTurnOffCaps && (GetKeyState(VK_CAPITAL) & 1)) {
				mbTurnOffCaps = false;

				// force caps lock back off
				mbEatNextInjectedCaps = true;

				keybd_event(VK_CAPITAL, 0, 0, 0);
				keybd_event(VK_CAPITAL, 0, KEYEVENTF_KEYUP, 0);
			}


			// Repost the key up message back to ourselves in case we need it to match against
			// a WM_CHAR message. This is necessary since WM_CHAR is posted and in theory could
			// arrive after the corresponding WM_KEYUP, although this has not been seen in practice.
			PostMessage(mhwnd, ATWM_CHARUP, wParam, lParam);

			{
				bool result = g_ATUIManager.OnKeyUp(ConvertVirtKeyEvent(wParam, lParam));

				g_ATLCHostKeys("Received host vkey up:   VK=$%02X LP=%08X (current key mask: %016llX)\n", LOWORD(wParam), (unsigned)lParam, g_sim.GetPokey().GetRawKeyMask());
				return result;
			}

		case ATWM_SETFULLSCREEN:
			if (mpMenuBar)
				mpMenuBar->SetVisible(wParam != 0);

			g_ATUINativeDisplay.SetFullScreen(wParam != 0);
			return 0;

		case ATWM_ENDTRACKING:
			if (g_pATVideoDisplayWindow)
				g_pATVideoDisplayWindow->EndEnhTextSizeIndicator();
			return 0;

		case ATWM_FORCEKEYSUP:
			g_ATUIManager.OnForceKeysUp();
			return 0;

		case WM_SIZE:
			{
				int w = LOWORD(lParam);
				int h = HIWORD(lParam);

				if (w != mWidth || h != mHeight) {
					mWidth = w;
					mHeight = h;

					g_ATUIManager.Resize(w, h);

					IATUIRenderer *r = g_sim.GetUIRenderer();

					if (r)
						r->Relayout(w, h);

					if (g_pATVideoDisplayWindow) {
						ATFrameWindow *frame = ATFrameWindow::GetFrameWindow(GetParent(mhwnd));
						
						if (frame && frame->IsActivelyMovingSizing()) {
							frame->EnableEndTrackNotification();
							g_pATVideoDisplayWindow->BeginEnhTextSizeIndicator();
						}
					}
				}
			}
			break;

		case WM_CHAR:
			{
				ATUICharEvent event = ATUINativeDecodeCharEvent(wParam, lParam);

				g_ATUIManager.OnChar(event);

				if (g_ATLCHostKeys.IsEnabled()) {
					g_ATLCHostKeys("Received host char:      CH=$%02X LP=%08X MOD=%c%c%c-%c%c%c (current key mask: %016llX)%s\n"
						, LOWORD(wParam)
						, (unsigned)lParam
						, GetKeyState(VK_LCONTROL) < 0 ? 'C' : '.'
						, GetKeyState(VK_LSHIFT) < 0 ? 'S' : '.'
						, GetKeyState(VK_LMENU) < 0 ? 'A' : '.'
						, GetKeyState(VK_RCONTROL) < 0 ? 'C' : '.'
						, GetKeyState(VK_RSHIFT) < 0 ? 'S' : '.'
						, GetKeyState(VK_RMENU) < 0 ? 'A' : '.'
						, g_sim.GetPokey().GetRawKeyMask()
						, event.mbIsRepeat ? " (repeat)" : ""
					);
				}
			}

			return 0;

		case ATWM_CHARUP:
			{
				ATUICharEvent event;
				event.mCh = 0;
				event.mScanCode = (uint32)((lParam >> 16) & 0xFF);
				event.mbIsRepeat = false;

				g_ATUIManager.OnCharUp(event);
			}

			g_ATLCHostKeys("Received host char up:          LP=%08X (current key mask: %016llX)\n", (unsigned)lParam, g_sim.GetPokey().GetRawKeyMask());
			return 0;

		case WM_PARENTNOTIFY:
			switch(LOWORD(wParam)) {
			case WM_LBUTTONDOWN:
			case WM_MBUTTONDOWN:
			case WM_RBUTTONDOWN:
				SetFocus(mhwnd);
				break;
			}
			break;

		case WM_LBUTTONUP:
			if (IsInjectedTouchMouseEvent())
				break;

			g_ATUIManager.OnMouseUp((short)LOWORD(lParam), (short)HIWORD(lParam), kATUIVK_LButton);
			break;

		case WM_MBUTTONUP:
			g_ATUIManager.OnMouseUp((short)LOWORD(lParam), (short)HIWORD(lParam), kATUIVK_MButton);
			break;

		case WM_RBUTTONUP:
			mbAllowContextMenu = false;
			g_ATUIManager.OnMouseUp((short)LOWORD(lParam), (short)HIWORD(lParam), kATUIVK_RButton);

			if (mbAllowContextMenu)
				break;

			return 0;

		case WM_XBUTTONUP:
			switch(HIWORD(wParam)) {
				case XBUTTON1:
					g_ATUIManager.OnMouseUp((short)LOWORD(lParam), (short)HIWORD(lParam), kATUIVK_XButton1);
					break;
				case XBUTTON2:
					g_ATUIManager.OnMouseUp((short)LOWORD(lParam), (short)HIWORD(lParam), kATUIVK_XButton2);
					break;
			}
			break;

		case WM_LBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
			if (IsInjectedTouchMouseEvent())
				break;

			// fall through

		case WM_MBUTTONDOWN:
		case WM_MBUTTONDBLCLK:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONDBLCLK:
		case WM_XBUTTONDOWN:
		case WM_XBUTTONDBLCLK:
			SetHaveMouse();

			::SetFocus(mhwnd);

			{
				int x = (short)LOWORD(lParam);
				int y = (short)HIWORD(lParam);

				if (!ATUIGetFullscreen())
					ATUISetNativeDialogMode(true);

				switch(msg) {
					case WM_LBUTTONDOWN:
					case WM_LBUTTONDBLCLK:
						if (g_ATUIManager.OnMouseDown(x, y, kATUIVK_LButton, msg == WM_LBUTTONDBLCLK))
							return 0;
						break;

					case WM_RBUTTONDOWN:
					case WM_RBUTTONDBLCLK:
						if (g_ATUIManager.OnMouseDown(x, y, kATUIVK_RButton, false))
							return 0;
						break;

					case WM_MBUTTONDOWN:
					case WM_MBUTTONDBLCLK:
						if (g_ATUIManager.OnMouseDown(x, y, kATUIVK_MButton, false))
							return 0;
						break;

					case WM_XBUTTONDOWN:
					case WM_XBUTTONDBLCLK:
						switch(HIWORD(wParam)) {
							case XBUTTON1:
								if (g_ATUIManager.OnMouseDown(x, y, kATUIVK_XButton1, false))
									return 0;
								break;
							case XBUTTON2:
								if (g_ATUIManager.OnMouseDown(x, y, kATUIVK_XButton2, false))
									return 0;
								break;
						}
						break;
				}
			}
			break;

		case WM_MOUSEWHEEL:
			{
				int x = (short)LOWORD(lParam);
				int y = (short)HIWORD(lParam);
				int dz = (short)HIWORD(wParam);

				POINT pt = { x, y };
				ScreenToClient(mhwnd, &pt);

				UINT lines = 1;
				::SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &lines, FALSE);

				bool doPages = false;
				if (lines == WHEEL_PAGESCROLL) {
					lines = 1;
					doPages = true;
				}

				if (g_ATUIManager.OnMouseWheel(pt.x, pt.y, (float)dz / (float)WHEEL_DELTA * (float)lines, doPages)) {
					if (GetKeyState(VK_MENU) < 0)
						SendMessage(GetAncestor(mhwnd, GA_ROOT), ATWM_SUPPRESS_KEYMENU, 0, 0);
					return 0;
				}
			}
			break;

		case WM_MOUSEHWHEEL:
			{
				int x = (short)LOWORD(lParam);
				int y = (short)HIWORD(lParam);
				int dz = (short)HIWORD(wParam);

				POINT pt = { x, y };
				ScreenToClient(mhwnd, &pt);

				UINT lines = 1;
				::SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &lines, FALSE);

				bool doPages = false;
				if (lines == WHEEL_PAGESCROLL) {
					lines = 1;
					doPages = true;
				}

				if (g_ATUIManager.OnMouseHWheel(pt.x, pt.y, (float)dz / (float)WHEEL_DELTA * (float)lines, doPages)) {
					if (GetKeyState(VK_MENU) < 0)
						SendMessage(GetAncestor(mhwnd, GA_ROOT), ATWM_SUPPRESS_KEYMENU, 0, 0);
					return 0;
				}
			}
			break;

		case WM_CONTEXTMENU:
			{
				POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };

				if (pt.x == -1 && pt.y == -1) {
					g_ATUIManager.OnContextMenu(NULL);
					return 0;
				}

				ScreenToClient(mhwnd, &pt);

				if ((uint32)pt.x < (uint32)mWidth && (uint32)pt.y < (uint32)mHeight) {
					vdpoint32 pt2(pt.x, pt.y);
					g_ATUIManager.OnContextMenu(&pt2);
					return 0;
				}
			}
			break;

		case WM_SETCURSOR:
			if (mMouseCursorLock)
				break;

			if (LOWORD(lParam) == HTCLIENT) {
				const uint32 id = g_ATUIManager.GetCurrentCursorImageId();

				if (id) {
					g_ATUINativeDisplay.SetCursorImageDirect(id);
					return 0;
				}
			}
			break;

		case WM_MOUSEMOVE:
			OnMouseMove(wParam, lParam);
			break;

		case WM_MOUSEHOVER:
			OnMouseHover(wParam);
			return 0;

		case WM_MOUSELEAVE:
			// We can get mouse leave messages while the mouse is captured. Suppress these
			// until capture ends.
			if (::GetCapture() != mhwnd) {
				g_ATUINativeDisplay.SetCursorImageChangesEnabled(false);
				mbHaveMouse = false;

				g_ATUIManager.OnMouseLeave();
			}
			return 0;

		case WM_INPUT:
			if (g_ATUINativeDisplay.IsMouseMotionModeEnabled() && g_ATUINativeDisplay.IsRawInputEnabled()) {
				UINT cbSize = 0;
				if (!GetRawInputData((HRAWINPUT)lParam, RID_INPUT, nullptr, &cbSize, sizeof(RAWINPUTHEADER)) && cbSize >= sizeof(RAWINPUTHEADER)) {
					if (mRawInputBuffer.size() < cbSize)
						mRawInputBuffer.resize(cbSize);

					UINT cbSize2 = cbSize;
					if (cbSize == GetRawInputData((HRAWINPUT)lParam, RID_INPUT, mRawInputBuffer.data(), &cbSize2, sizeof(RAWINPUTHEADER))) {
						if (mRawInputBuffer->header.dwType == RIM_TYPEMOUSE) {
							SetHaveMouse();

							sint32 dx = 0;
							sint32 dy = 0;
							sint32 x = mRawInputBuffer->data.mouse.lLastX;
							sint32 y = mRawInputBuffer->data.mouse.lLastY;
							if (mRawInputBuffer->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) {

								const bool usingVirtualDesktopCoords = (mRawInputBuffer->data.mouse.usFlags & MOUSE_VIRTUAL_DESKTOP) != 0;

								// This looks wrong since it would map the absolute bottom right to (w,h), but it's
								// the conversion recommended by the raw mouse API documentation.
								x = (sint32)(((float)x / 65535.0f) * GetSystemMetrics(usingVirtualDesktopCoords ? SM_CXVIRTUALSCREEN : SM_CXSCREEN));
								y = (sint32)(((float)y / 65535.0f) * GetSystemMetrics(usingVirtualDesktopCoords ? SM_CYVIRTUALSCREEN : SM_CYSCREEN));

								if (mbLastTrackMouseValid) {
									dx = x - mLastTrackMouseX;
									dy = y - mLastTrackMouseY;
								}

								mLastTrackMouseX = x;
								mLastTrackMouseY = y;
								mbLastTrackMouseValid = true;
							} else {
								dx = x;
								dy = y;
								mbLastTrackMouseValid = false;
							}

							// WM_SETCURSOR isn't set when mouse capture is enabled, in which case we must poll for a cursor
							// update.
							const uint32 id = g_ATUIManager.GetCurrentCursorImageId();

							if (id)
								g_ATUINativeDisplay.SetCursorImageDirect(id);

							if (dx || dy)
								g_ATUIManager.OnMouseRelativeMove(dx, dy);
						}
					}
				}
			}
			break;

		case WM_SETFOCUS:
			// Remember that we are effectively nesting window managers here -- when the display
			// window gains focus, to our window manager it is like a Win32 window becoming
			// active.
			g_ATUIManager.SetForeground(true);
			g_ATUINativeDisplay.SetMouseConstraintEnabled(true);

			if (mpAccessibilityProvider)
				mpAccessibilityProvider->OnGainedFocus();
			break;

		case WM_KILLFOCUS:
			g_ATUIManager.SetForeground(false);
			g_ATUINativeDisplay.SetMouseConstraintEnabled(false);

			if (::GetCapture() == mhwnd)
				::ReleaseCapture();
			break;

		case WM_CAPTURECHANGED:
			if (g_ATUINativeDisplay.IsMouseCaptured()) {
				g_ATUINativeDisplay.OnCaptureLost();
				g_ATUIManager.OnCaptureLost();
			}

			mbLastTrackMouseValid = false;

			if (mbHaveMouse) {
				TRACKMOUSEEVENT tme = {sizeof(TRACKMOUSEEVENT)};
				tme.dwFlags = TME_LEAVE;
				tme.hwndTrack = mhwnd;
				::TrackMouseEvent(&tme);
			}
			break;

		case WM_ERASEBKGND:
			return 0;

		case WM_COPY:
			Copy(ATTextCopyMode::ASCII);
			return 0;

		case WM_PASTE:
			Paste();
			return 0;

		case WM_ENTERMENULOOP:
			// We get this directly for a pop-up menu.
			g_ATUIManager.OnForceKeysUp();
			break;

		case ATWM_GETAUTOSIZE:
			{
				vdsize32& sz = *(vdsize32 *)lParam;

				sz.w = VDCeilToInt(mDisplayRectF.width());
				sz.h = VDCeilToInt(mDisplayRectF.height());

				if (g_dispPadIndicators) {
					if (auto *p = g_sim.GetUIRenderer())
						sz.h += p->GetIndicatorSafeHeight();
				}

				return TRUE;
			}
			break;

		case WM_TOUCH:
			{
				uint32 numInputs = LOWORD(wParam);
				if (numInputs > vdcountof(mTouchInputs))
					numInputs = vdcountof(mTouchInputs);

				HTOUCHINPUT hti = (HTOUCHINPUT)lParam;
				if (numInputs && VDGetTouchInputInfoW32(hti, numInputs, mRawTouchInputs, sizeof(TOUCHINPUT))) {
					const TOUCHINPUT *src = mRawTouchInputs;
					ATUITouchInput *dst = mTouchInputs;
					uint32 numCookedInputs = 0;

					SetHaveMouse();

					::SetFocus(mhwnd);

					if (!ATUIGetFullscreen())
						ATUISetNativeDialogMode(true);

					for(uint32 i=0; i<numInputs; ++i, ++src) {
						if (src->dwFlags & TOUCHEVENTF_PALM)
							continue;

						POINT pt = { (src->x + 50) / 100, (src->y + 50) / 100 };

						if (!::ScreenToClient(mhwnd, &pt))
							continue;

						dst->mId = src->dwID;
						dst->mX = pt.x;
						dst->mY = pt.y;
						dst->mbPrimary = (src->dwFlags & TOUCHEVENTF_PRIMARY) != 0;
						dst->mbDown = (src->dwFlags & TOUCHEVENTF_DOWN) != 0;
						dst->mbUp = (src->dwFlags & TOUCHEVENTF_UP) != 0;
						dst->mbDoubleTap = false;

						if (dst->mbPrimary) {
							bool dblTapValid = false;

							if (mDblTapState != kDblTapState_Initial) {
								// The regular double-click metrics are too small for touch.
								if (abs(pt.x - mDblTapX) <= 32
									&& abs(pt.y - mDblTapY) <= 32)
								{
									dblTapValid = true;
								}
							}

							if (dst->mbDown) {
								if (mDblTapState == kDblTapState_Initial) {
									mDblTapState = kDblTapState_WaitDown1;
									mDblTapX = pt.x;
									mDblTapY = pt.y;
									mDblTapTime = VDGetCurrentTick();
								} else if (mDblTapState == kDblTapState_WaitDown2) {
									if (dblTapValid && (VDGetCurrentTick() - mDblTapTime) <= GetDoubleClickTime()) {
										dst->mbDoubleTap = true;
										mDblTapState = kDblTapState_WaitUp2;
									} else {
										mDblTapX = pt.x;
										mDblTapY = pt.y;
										mDblTapTime = VDGetCurrentTick();
									}
								}

								mGestureOrigin = vdpoint32(pt.x, pt.y);
							} else if (dst->mbUp) {
								if (mDblTapState == kDblTapState_Invalid)
									mDblTapState = kDblTapState_Initial;
								else if (mDblTapState == kDblTapState_WaitDown1) {
									if (dblTapValid)
										mDblTapState = kDblTapState_WaitDown2;
									else
										mDblTapState = kDblTapState_Initial;
								} else if (mDblTapState == kDblTapState_WaitUp2) {
									mDblTapState = kDblTapState_Initial;
								}

								// A swipe up of at least 1/6th of the screen from the bottom quarter opens
								// the OSK.
								sint32 dy = pt.y - mGestureOrigin.y;

								if (dy < -mHeight / 6 && mGestureOrigin.y >= mHeight * 3 / 4) {
									if (g_pATVideoDisplayWindow)
										g_pATVideoDisplayWindow->OpenOSK();
								}
							} else {
								if (mDblTapState != kDblTapState_Initial && !dblTapValid)
									mDblTapState = kDblTapState_Invalid;
							}
						}

						++dst;
						++numCookedInputs;
					}

					VDCloseTouchInputHandleW32(hti);

					g_ATUIManager.OnTouchInput(mTouchInputs, numCookedInputs);
					return 0;
				}
			}

			break;

		case WM_GETOBJECT:
			if (ATUIAccGetEnabled() && !mbDestroying && (long)lParam == (long)UiaRootObjectId) {
				if (!mpAccessibilityProvider) {
					ATUICreateDisplayAccessibilityProviderW32(mhwnd, *this, ~mpAccessibilityProvider);

					mpAccessibilityCurrentScreen = new ATUIDisplayAccessibilityScreen;
					mpAccessibilityNextScreen = new ATUIDisplayAccessibilityScreen;
				}

				return UiaReturnRawElementProvider(mhwnd, wParam, lParam, mpAccessibilityProvider->AsRawElementProviderSimple());
			}
			break;
	}

	return ATUIPaneWindow::WndProc(msg, wParam, lParam);
}

ATUITouchMode ATDisplayPane::GetTouchModeAtPoint(const vdpoint32& pt) const {
	return g_ATUIManager.GetTouchModeAtPoint(pt);
}

bool ATDisplayPane::OnCreate() {
	mbDestroying = false;

	if (!ATUIPaneWindow::OnCreate())
		return false;

	g_ATLCHostUI("Creating display window");

	mhwndDisplay = (HWND)VDCreateDisplayWindowW32(WS_EX_NOPARENTNOTIFY, WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN, 0, 0, 0, 0, (VDGUIHandle)mhwnd);
	if (!mhwndDisplay)
		return false;

	SetWindowTextW(mhwndDisplay, L"Video Display");

	mpDisplay = VDGetIVideoDisplay((VDGUIHandle)mhwndDisplay);
	g_pDisplay = mpDisplay;
	
	g_ATLCHostUI("Setting up display window");

	mpDisplay->SetReturnFocus(true);
	mpDisplay->SetTouchEnabled(true);
	mpDisplay->SetUse16Bit(g_ATOptions.mbDisplay16Bit);
	UpdateFilterMode();
	UpdateCustomRefreshRate();
	mpDisplay->SetAccelerationMode(IVDVideoDisplay::kAccelAlways);
	mpDisplay->SetCompositor(&g_ATUIManager);

	g_ATLCHostUI("Pushing initial frame");

	// We need to push in an initial frame for two reasons: (1) black out immediately, (2) force full
	// screen mode to size the window correctly.
	mpDisplay->SetSourceSolidColor(0);

	mpDisplay->SetProfileHook(
		[](IVDVideoDisplay::ProfileEvent event, uintptr arg) {
			switch(event) {
				case IVDVideoDisplay::kProfileEvent_BeginTick:
					ATProfileBeginRegion(kATProfileRegion_DisplayTick);
					break;

				case IVDVideoDisplay::kProfileEvent_EndTick:
					ATProfileEndRegion(kATProfileRegion_DisplayTick);
					break;

				case IVDVideoDisplay::kProfileEvent_BeginPresent:
				case IVDVideoDisplay::kProfileEvent_BeginRePresent:
					ATProfileBeginRegionWithArg(kATProfileRegion_DisplayPresent, arg);
					break;

				case IVDVideoDisplay::kProfileEvent_EndPresent:
				case IVDVideoDisplay::kProfileEvent_EndRePresent:
					ATProfileEndRegion(kATProfileRegion_DisplayPresent);
					break;

				case IVDVideoDisplay::kProfileEvent_VSyncInfo:
					ATProfileMarkEventWithArg(kATProfileEvent_DisplayVSync, arg);
					break;
			}
		}
	);

	g_ATUINativeDisplay.SetDisplay(mhwnd, ATFrameWindow::GetFrameWindow(GetParent(mhwnd)));

	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	if (!mpEnhTextEngine)
		gtia.SetVideoOutput(mpDisplay);

	g_ATLCHostUI("Pushing initial frame update");

	gtia.UpdateScreen(true, true);

	g_ATUINativeDisplay.SetIgnoreAutoFlipping(mpEnhTextEngine != nullptr);

	g_ATLCHostUI("Creating local UI");

	mpMenuBar = new ATUIMenuList;
	mpMenuBar->SetVisible(false);
	g_ATUIManager.GetMainWindow()->AddChild(mpMenuBar);
	mpMenuBar->SetFont(g_ATUIManager.GetThemeFont(kATUIThemeFont_Menu));
	mpMenuBar->SetMenu(ATUIGetMenu());
	mpMenuBar->SetAutoHide(true);
	mpMenuBar->OnActivatingEvent() = ATBINDCALLBACK(this, &ATDisplayPane::OnMenuActivating);
	mpMenuBar->OnItemSelected() = ATBINDCALLBACK(this, &ATDisplayPane::OnMenuItemSelected);
	mpMenuBar->SetPlacement(vdrect32f(0, 0, 1, 0), vdpoint32(0, 0), vdfloat2{0, 0});
	mpMenuBar->SetSizeOffset(vdsize32(0, mpMenuBar->GetIdealHeight()));

	g_pATVideoDisplayWindow->SetOnAllowContextMenu([this]() { OnAllowContextMenu(); });
	g_pATVideoDisplayWindow->SetOnDisplayContextMenu([this](const vdpoint32& pt) { OnDisplayContextMenu(pt); });
	g_pATVideoDisplayWindow->SetOnOSKChange([this]() { OnOSKChange(); });

	g_pATVideoDisplayWindow->SetDisplaySourceMapping(
		[this](vdfloat2& pt) -> bool {
			return !mpDisplay || mpDisplay->MapNormDestPtToSource(pt);
		},

		[this](vdfloat2& pt) -> bool {
			return !mpDisplay || mpDisplay->MapNormSourcePtToDest(pt);
		}
	);

	mIndicatorSafeAreaChangedFn = [this] { ResizeDisplay(); };
	g_sim.GetUIRenderer()->AddIndicatorSafeHeightChangedHandler(&mIndicatorSafeAreaChangedFn);

	g_ATLCHostUI("Initializing drag-drop");
	ATUISetDragDropSubTarget((VDGUIHandle)mhwnd, &g_ATUIManager);

	g_ATLCHostUI("Init complete");
	return true;
}

void ATDisplayPane::OnDestroy() {
	// Block any more accessibility objects from being created -- this is important
	// to avoid memory leaks from being created while trying to disconnect.
	mbDestroying = true;

	if (mpAccessibilityProvider) {
		auto p = std::move(mpAccessibilityProvider);
		mpAccessibilityProvider = nullptr;
		mpAccessibilityCurrentScreen = nullptr;
		mpAccessibilityNextScreen = nullptr;

		// Must be done while the provider is still valid, or it'll proc an error
		// from the provider, since UiaDisconnectProvider() calls get_ProviderOptions()
		// and get_HostRawElementProvider().
		ATUiaDisconnectProviderW32(p->AsRawElementProviderSimple());

		p->Detach();

		VDVERIFY(SUCCEEDED(UiaReturnRawElementProvider(mhwnd, 0, 0, nullptr)));
	}

	ATUISetDragDropSubTarget(nullptr, nullptr);

	g_sim.GetUIRenderer()->RemoveIndicatorSafeHeightChangedHandler(&mIndicatorSafeAreaChangedFn);

	if (g_pATVideoDisplayWindow) {
		g_pATVideoDisplayWindow->SetOnAllowContextMenu(nullptr);
		g_pATVideoDisplayWindow->SetOnDisplayContextMenu(nullptr);
		g_pATVideoDisplayWindow->SetOnOSKChange(nullptr);
		g_pATVideoDisplayWindow->SetDisplaySourceMapping({}, {});
		g_pATVideoDisplayWindow->UnbindAllActions();
	}

	if (mpMenuBar) {
		mpMenuBar->Destroy();
		mpMenuBar.clear();
	}

	if (mpEnhTextEngine) {
		if (g_pATVideoDisplayWindow)
			g_pATVideoDisplayWindow->SetEnhancedTextEngine(NULL);

		mpEnhTextEngine->Shutdown();
		mpEnhTextEngine = NULL;
	}

	if (mpDisplay) {
		g_ATUINativeDisplay.SetDisplay(NULL, NULL);
		g_sim.GetGTIA().SetVideoOutput(NULL);

		mpDisplay->Destroy();
		mpDisplay = NULL;
		g_pDisplay = NULL;
		mhwndDisplay = NULL;
	}

	ATUIPaneWindow::OnDestroy();
}

void ATDisplayPane::OnMouseMove(WPARAM wParam, LPARAM lParam) {
	const int x = (short)LOWORD(lParam);
	const int y = (short)HIWORD(lParam);

	SetHaveMouse();

	if (ATUIIsMenuAutoHideActive() && ATUIIsMenuAutoHidden() && mpFrameWindow->IsDocked()) {
		ATContainerWindow *container = mpFrameWindow->GetContainer();
		if (container)
			container->TestMenuAutoShow(TransformClientToScreen(vdpoint32(x, y)));
	}

	// WM_SETCURSOR isn't set when mouse capture is enabled, in which case we must poll for a cursor
	// update.
	if (g_ATUINativeDisplay.IsMouseCaptured()) {
		const uint32 id = g_ATUIManager.GetCurrentCursorImageId();

		if (id)
			g_ATUINativeDisplay.SetCursorImageDirect(id);
	}

	if (g_ATUINativeDisplay.IsMouseMotionModeEnabled()) {
		if (!g_ATUINativeDisplay.IsRawInputEnabled()) {
			int dx = x - mLastTrackMouseX;
			int dy = y - mLastTrackMouseY;

			if (dx | dy) {
				// If this is the first move message we've gotten since getting the mouse,
				// ignore the delta.
				if (mbLastTrackMouseValid)
					g_ATUIManager.OnMouseRelativeMove(dx, dy);

				WarpCapturedMouse();
			}
		}
	} else {
		TRACKMOUSEEVENT tme = {sizeof(TRACKMOUSEEVENT)};
		tme.dwFlags = TME_HOVER;
		tme.hwndTrack = mhwnd;
		tme.dwHoverTime = HOVER_DEFAULT;

		TrackMouseEvent(&tme);

		g_ATUIManager.OnMouseMove(x, y);
	}
}

void ATDisplayPane::OnMouseHover(WPARAM wParam) {
	if (!g_ATUINativeDisplay.IsMouseMotionModeEnabled()) {
		DWORD pos = ::GetMessagePos();

		int x = (short)LOWORD(pos);
		int y = (short)HIWORD(pos);

		POINT pt = { x, y };

		if (::ScreenToClient(mhwnd, &pt))
			g_ATUIManager.OnMouseHover(pt.x, pt.y);
	}
}

ATUIKeyEvent ATDisplayPane::ConvertVirtKeyEvent(WPARAM wParam, LPARAM lParam) {
	return ATUINativeDecodeKeyEvent(wParam, lParam);
}

void ATDisplayPane::ReleaseMouse() {
	if (g_pATVideoDisplayWindow)
		g_pATVideoDisplayWindow->ReleaseMouse();
}

void ATDisplayPane::ToggleCaptureMouse() {
	if (g_pATVideoDisplayWindow)
		g_pATVideoDisplayWindow->ToggleCaptureMouse();
}

void ATDisplayPane::ResetDisplay() {
	if (mpDisplay) {
		mpDisplay->Reset();
		mpDisplay->SetUse16Bit(g_ATOptions.mbDisplay16Bit);
	}
}

void ATDisplayPane::SelectAll() {
	g_pATVideoDisplayWindow->SelectAll();
}

void ATDisplayPane::Deselect() {
	g_pATVideoDisplayWindow->Deselect();
}

void ATDisplayPane::Copy(ATTextCopyMode copyMode) {
	g_pATVideoDisplayWindow->Copy(copyMode);
}

void ATDisplayPane::CopyFrame(bool trueAspect) {
	g_pATVideoDisplayWindow->CopySaveFrame(false, trueAspect);
}

bool ATDisplayPane::CopyFrameImage(bool trueAspect, VDPixmapBuffer& buf) {
	return g_pATVideoDisplayWindow->CopyFrameImage(trueAspect, buf);
}

void ATDisplayPane::SaveFrame(bool trueAspect, const wchar_t *path) {
	g_pATVideoDisplayWindow->CopySaveFrame(true, trueAspect, path);
}

void ATDisplayPane::Paste() {
	OnCommandEditPasteText();
}

void ATDisplayPane::Paste(const wchar_t *s, size_t len) {
	if (mpEnhTextEngine)
		mpEnhTextEngine->Paste(s, len);
}

void ATDisplayPane::OnSize() {
	RECT r;
	GetClientRect(mhwnd, &r);

	if (g_ATUINativeDisplay.IsMouseActuallyConstrained()) {
		RECT rs = r;

		MapWindowPoints(mhwnd, NULL, (LPPOINT)&rs, 2);
		ClipCursor(&rs);
	}

	if (mhwndDisplay) {
		if (mpDisplay)
			ResizeDisplay();

		SetWindowPos(mhwndDisplay, NULL, 0, 0, r.right, r.bottom, SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
	}
}

void ATDisplayPane::UpdateFilterMode() {
	if (!mpDisplay)
		return;

	switch(g_dispFilterMode) {
		case kATDisplayFilterMode_Point:
			mpDisplay->SetFilterMode(IVDVideoDisplay::kFilterPoint);
			mpDisplay->SetPixelSharpness(1.0f, 1.0f);
			break;

		case kATDisplayFilterMode_Bilinear:
			mpDisplay->SetFilterMode(IVDVideoDisplay::kFilterBilinear);
			mpDisplay->SetPixelSharpness(1.0f, 1.0f);
			break;

		case kATDisplayFilterMode_SharpBilinear:
			mpDisplay->SetFilterMode(IVDVideoDisplay::kFilterBilinear);
			{
				ATGTIAEmulator& gtia = g_sim.GetGTIA();
				int pw = 2;
				int ph = 2;

				gtia.GetPixelAspectMultiple(pw, ph);

				static const float kFactors[5] = { 1.259f, 1.587f, 2.0f, 2.520f, 3.175f };

				const float factor = kFactors[std::max(0, std::min(4, g_dispFilterSharpness + 2))];

				const auto afmode = gtia.GetArtifactingMode();
				const bool isHighArtifacting = afmode == ATArtifactMode::NTSCHi || afmode == ATArtifactMode::PALHi || afmode == ATArtifactMode::AutoHi;

				mpDisplay->SetPixelSharpness(isHighArtifacting ? 1.0f : std::max(1.0f, factor / (float)pw), std::max(1.0f, factor / (float)ph));
			}
			break;

		case kATDisplayFilterMode_Bicubic:
			mpDisplay->SetFilterMode(IVDVideoDisplay::kFilterBicubic);
			mpDisplay->SetPixelSharpness(1.0f, 1.0f);
			break;

		case kATDisplayFilterMode_AnySuitable:
			mpDisplay->SetFilterMode(IVDVideoDisplay::kFilterAnySuitable);
			mpDisplay->SetPixelSharpness(1.0f, 1.0f);
			break;
	}
}

void ATDisplayPane::UpdateCustomRefreshRate() {
	if (mpDisplay)
		mpDisplay->SetCustomDesiredRefreshRate(g_dispCustomRefreshRate, g_dispCustomRefreshRate * 0.99f, g_dispCustomRefreshRate * 1.03f);
}

void ATDisplayPane::RequestRenderedFrame(vdfunction<void(const VDPixmap *)> fn) {
	if (!mpDisplay)
		fn(nullptr);
	else {
		mpDisplay->RequestCapture(std::move(fn));
		mpDisplay->Invalidate();
	}
}

void ATDisplayPane::SetVideoWriter(IATVideoWriter *writer) {
	if (g_pATVideoDisplayWindow)
		g_pATVideoDisplayWindow->SetVideoWriter(writer);
}

void ATDisplayPane::SetAutoSuggestEnabled(bool enabled) {
	if (g_pATVideoDisplayWindow)
		g_pATVideoDisplayWindow->SetAutoSuggestEnabled(enabled);
}

void ATDisplayPane::ShowSuggestions() {
	if (g_pATVideoDisplayWindow)
		g_pATVideoDisplayWindow->ShowSuggestions();
}

void ATDisplayPane::SetHaveMouse() {
	if (!mbHaveMouse) {
		mbHaveMouse = true;
		mbLastTrackMouseValid = false;

		g_ATUINativeDisplay.SetCursorImageChangesEnabled(true);

		TRACKMOUSEEVENT tme = {sizeof(TRACKMOUSEEVENT)};
		tme.dwFlags = TME_LEAVE;
		tme.hwndTrack = mhwnd;
		::TrackMouseEvent(&tme);
	}
}

void ATDisplayPane::WarpCapturedMouse() {
	const vdpoint32& pt = g_ATUINativeDisplay.WarpCapturedMouse();

	mLastTrackMouseX = pt.x;
	mLastTrackMouseY = pt.y;
	mbLastTrackMouseValid = true;
}

void ATDisplayPane::UpdateTextDisplay(bool enabled) {
	IATVirtualScreenHandler *vs = g_sim.GetVirtualScreenHandler();

	if (vs && vs->IsPassThroughEnabled())
		enabled = false;

	g_ATUINativeDisplay.SetIgnoreAutoFlipping(enabled);

	if (mpAccessibilityProvider)
		UpdateAccessibilityProvider();

	if (!enabled) {
		if (mbTextModeEnabled) {
			mbTextModeEnabled = false;

			if (mpEnhTextEngine) {
				g_pATVideoDisplayWindow->SetEnhancedTextEngine(NULL);
				mpEnhTextEngine->Shutdown();
				mpEnhTextEngine = NULL;

				g_sim.GetGTIA().SetVideoOutput(mpDisplay);
				if (mpDisplay)
					mpDisplay->SetSourceSolidColor(0);
			}
		}

		return;
	}

	bool forceInvalidate = false;
	bool virtScreen = (vs != nullptr);

	if (!mbTextModeEnabled) {
		mbTextModeEnabled = true;
		mbTextModeVirtScreen = virtScreen;

		if (!mpEnhTextEngine) {
			mpEnhTextEngine = ATUICreateEnhancedTextEngine();
			mpEnhTextEngine->Init(g_pATVideoDisplayWindow, &g_sim);
		}

		g_sim.GetGTIA().SetVideoOutput(NULL);

		// This will also register the enhanced text engine with the video window.
		UpdateTextModeFont();

		forceInvalidate = true;
	} else if (mbTextModeVirtScreen != virtScreen) {
		mbTextModeVirtScreen = virtScreen;

		g_pATVideoDisplayWindow->UpdateEnhTextSize();

		forceInvalidate = true;
	}

	if (mpEnhTextEngine)
		mpEnhTextEngine->Update(forceInvalidate);

	if (forceInvalidate)
		g_pATVideoDisplayWindow->InvalidateTextOutput();
}

void ATDisplayPane::UpdateTextModeFont() {
	if (mpEnhTextEngine) {
		mpEnhTextEngine->SetFont(&g_enhancedTextFont);

		// Must be done after we have changed the font to reinitialize char dimensions based on char size.
		g_pATVideoDisplayWindow->SetEnhancedTextEngine(nullptr);
		g_pATVideoDisplayWindow->SetEnhancedTextEngine(mpEnhTextEngine);
	}
}

void ATDisplayPane::UpdateAccessibilityProvider() {
	if (!mpAccessibilityProvider)
		return;

	if (!UiaClientsAreListening()) {
		mpAccessibilityProvider->Detach();
		mpAccessibilityProvider = nullptr;
		UiaReturnRawElementProvider(mhwnd, 0, 0, nullptr);
		return;
	}

	g_pATVideoDisplayWindow->ReadScreen(*mpAccessibilityNextScreen);

	if (*mpAccessibilityNextScreen != *mpAccessibilityCurrentScreen) {
		std::swap(mpAccessibilityCurrentScreen, mpAccessibilityNextScreen);
		mpAccessibilityProvider->SetScreen(mpAccessibilityCurrentScreen);
	}
}

void ATDisplayPane::OnMenuActivating(ATUIMenuList *) {
	ATUIUpdateMenu();
	ATUpdatePortMenus();
}

void ATDisplayPane::OnMenuItemSelected(ATUIMenuList *sender, uint32 id) {
	// trampoline to top-level window
	::SendMessage(::GetAncestor(mhwnd, GA_ROOT), WM_COMMAND, id, 0);
}

void ATDisplayPane::OnAllowContextMenu() {
	mbAllowContextMenu = true;
}

void ATDisplayPane::OnDisplayContextMenu(const vdpoint32& pt) {
	HMENU hmenuPopup = GetSubMenu(mhmenuContext, 0);

	if (hmenuPopup) {
		POINT pt2 = { pt.x, pt.y };

		::ClientToScreen(mhwnd, &pt2);

		++mMouseCursorLock;

		const UINT canCopyTextMode = IsTextSelected() ? MF_ENABLED : MF_DISABLED|MF_GRAYED;

		EnableMenuItem(hmenuPopup, ID_DISPLAYCONTEXTMENU_COPY, canCopyTextMode);
		EnableMenuItem(hmenuPopup, ID_DISPLAYCONTEXTMENU_COPYESCAPEDTEXT, canCopyTextMode);
		EnableMenuItem(hmenuPopup, ID_DISPLAYCONTEXTMENU_COPYHEX, canCopyTextMode);
		EnableMenuItem(hmenuPopup, ID_DISPLAYCONTEXTMENU_COPYUNICODE, canCopyTextMode);
		EnableMenuItem(hmenuPopup, ID_DISPLAYCONTEXTMENU_PASTE, IsClipboardFormatAvailable(CF_TEXT) ? MF_ENABLED : MF_DISABLED|MF_GRAYED);

		UINT cmd = (UINT)TrackPopupMenu(hmenuPopup, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, pt2.x, pt2.y, 0, GetAncestor(mhwnd, GA_ROOTOWNER), NULL);
		switch(cmd) {
			case ID_DISPLAYCONTEXTMENU_COPY:
				Copy(ATTextCopyMode::ASCII);
				break;

			case ID_DISPLAYCONTEXTMENU_COPYESCAPEDTEXT:
				Copy(ATTextCopyMode::Escaped);
				break;

			case ID_DISPLAYCONTEXTMENU_COPYHEX:
				Copy(ATTextCopyMode::Hex);
				break;

			case ID_DISPLAYCONTEXTMENU_COPYUNICODE:
				Copy(ATTextCopyMode::Unicode);
				break;

			case ID_DISPLAYCONTEXTMENU_PASTE:
				Paste();
				break;
		}

		--mMouseCursorLock;
	}
}

void ATDisplayPane::OnOSKChange() {
	ResizeDisplay();
}

void ATDisplayPane::ResizeDisplay() {
	UpdateFilterMode();

	RECT r {};
	GetClientRect(mhwnd, &r);

	float viewportWidth = (float)r.right;
	float viewportHeight = (float)r.bottom;

	if (g_dispPadIndicators)
		viewportHeight -= g_sim.GetUIRenderer()->GetIndicatorSafeHeight();

	if (g_pATVideoDisplayWindow) {
		vdrect32 rsafe = g_pATVideoDisplayWindow->GetOSKSafeArea();

		viewportHeight = std::min<sint32>(viewportHeight, rsafe.height());
	}

	float w = viewportWidth;
	float h = viewportHeight;

	if (w < 1)
		w = 1;

	if (h < 1)
		h = 1;

	const auto& gtia = g_sim.GetGTIA();

	if (g_displayStretchMode == kATDisplayStretchMode_PreserveAspectRatio || g_displayStretchMode == kATDisplayStretchMode_IntegralPreserveAspectRatio) {
		int sw = 1;
		int sh = 1;
		bool rgb32 = false;

		gtia.GetRawFrameFormat(sw, sh, rgb32);

		const float fsw = (float)sw * gtia.GetPixelAspectRatio();
		const float fsh = (float)sh;
		float zoom = std::min<float>(w / fsw, h / fsh);

		if (g_displayStretchMode == kATDisplayStretchMode_IntegralPreserveAspectRatio && zoom > 1) {
			// We may have some small rounding errors, so give a teeny bit of leeway.
			zoom = floorf(zoom * 1.0001f);
		}

		w = fsw * zoom;
		h = fsh * zoom;
	} else if (g_displayStretchMode == kATDisplayStretchMode_SquarePixels || g_displayStretchMode == kATDisplayStretchMode_Integral) {
		int sw = 1;
		int sh = 1;
		gtia.GetFrameSize(sw, sh);

		float fsw = (float)sw;
		float fsh = (float)sh;

		float ratio = floorf(std::min<float>(w / fsw, h / fsh));

		if (ratio < 1 || g_displayStretchMode == kATDisplayStretchMode_SquarePixels) {
			if (w*fsh < h*fsw) {		// (w / sw) < (h / sh) -> w*sh < h*sw
				// width is smaller ratio -- compute restricted height
				h = (sh * w) / fsw;
			} else {
				// height is smaller ratio -- compute restricted width
				w = (sw * h) / sh;
			}
		} else {
			w = sw * ratio;
			h = sh * ratio;
		}
	}
	
	w *= g_displayZoom;
	h *= g_displayZoom;

	vdfloat2 relativeSourceOrigin = vdfloat2(0.5f, 0.5f) - g_displayPan;

	vdrect32f rd(
		w * (relativeSourceOrigin.x - 1.0f),
		h * (relativeSourceOrigin.y - 1.0f),
		w * relativeSourceOrigin.x,
		h * relativeSourceOrigin.y
	);

	rd.translate(viewportWidth * 0.5f, viewportHeight * 0.5f);

	// try to pixel snap the rectangle if possible
	vdrect32f error(
		rd.left   - roundf(rd.left  ),
		rd.top    - roundf(rd.top   ),
		rd.right  - roundf(rd.right ),
		rd.bottom - roundf(rd.bottom)
	);

	rd.translate(-0.5f*(error.left + error.right), -0.5f*(error.top + error.bottom));

	if (mDisplayRectF != rd) {
		mDisplayRectF = rd;

		g_ATUIManager.Invalidate(nullptr);
	}

	vdrect32 ri;

	ri.left = VDRoundToInt(mDisplayRectF.left);
	ri.top = VDRoundToInt(mDisplayRectF.top);
	ri.right = ri.left + VDRoundToInt(mDisplayRectF.width());
	ri.bottom = ri.top + VDRoundToInt(mDisplayRectF.height());

	if (mpDisplay)
		mpDisplay->SetDestRectF(&mDisplayRectF, 0);
	g_pATVideoDisplayWindow->SetDisplayRect(ri);
}

///////////////////////////////////////////////////////////////////////////

void ATUIRegisterDisplayPane() {
	ATRegisterUIPaneType(kATUIPaneId_Display, VDRefCountObjectFactory<ATDisplayPane, ATUIPane>);
}
