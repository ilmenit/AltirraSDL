// AltirraBridgeServer — UI stubs for symbols normally provided by the
// SDL3 frontend (main_sdl3.cpp, source/ui/, source/app/).
//
// The bridge server reuses the AltirraSDL stubs/ files for things
// like uiaccessors_stubs.cpp and console_stubs.cpp. Those files
// reference SDL3-frontend globals and UI functions that exist in
// main_sdl3.cpp / source/ui/debugger/ — neither of which we link.
// This file provides no-op stubs for everything the linker would
// otherwise complain about, plus the small ATBridgeRequestAppQuit
// hook that the bridge module calls on QUIT.

#include <stdafx.h>

#include <atomic>
#include <mutex>
#include <string>
#include <vd2/system/VDString.h>
#include <vd2/VDDisplay/display.h>
#include <SDL3/SDL.h>

// Pull in the real ATDeferredActionType enum so our ATUIPushDeferred
// stub has the same C++ mangled name as the SDL3 frontend's. This
// header transitively includes <imgui.h>, which our local stubs/
// directory shims.
#include "ui_main.h"

// =========================================================================
// Bridge → main loop quit hook. main_bridge.cpp owns g_running.
// =========================================================================

extern std::atomic<bool> g_running;

void ATBridgeRequestAppQuit() {
	g_running.store(false);
}

// =========================================================================
// Globals expected by AltirraSDL stubs/* (which we link in)
// =========================================================================

// console_stubs.cpp / win32_stubs.cpp pass these around as the
// "console output" / "screenshot path" handoff between the
// debugger console and the rendering thread. With no UI, no console,
// and no rendering thread, they're vestigial — but the linker still
// needs them to exist, **with exactly matching types** so MSVC's
// name mangling (which encodes variable types into mangled symbols)
// resolves. Linux/macOS Itanium ABI doesn't mangle variable types so
// a mismatch here passes there but fails on Windows with
// LNK2019/LNK1120. The types below must match the extern declarations
// in src/AltirraSDL/stubs/console_stubs.cpp and win32_stubs.cpp.
class IATUIDebuggerConsoleWindow;  // opaque — we never dereference it
std::mutex               g_consoleMutex;
std::string              g_consoleText;
std::atomic<bool>        g_consoleTextDirty{false};
std::atomic<bool>        g_consoleScrollToBottom{false};
IATUIDebuggerConsoleWindow* g_pConsoleWindow = nullptr;
std::mutex               g_saveFrameMutex;
VDStringA                g_saveFramePath;  // win32_stubs.cpp uses UTF-8 path

// SDL3 window pointer. uiaccessors_stubs.cpp's ATUIGetFullscreen()
// reads this to decide whether the window is fullscreen. The bridge
// server has no window, so we hard-stub it to nullptr.
SDL_Window *g_pWindow = nullptr;

// =========================================================================
// SDL3-frontend UI symbols. All no-ops — calling any of these from
// the bridge server would be a logic bug (the bridge has no UI).
// =========================================================================

// Forward declarations for opaque types referenced in some
// signatures. We don't include the real headers because they pull
// in ImGui / SDL3 frontend chain. The type names here must match the
// extern declarations in src/AltirraSDL/stubs/console_stubs.cpp so
// MSVC's return-type-aware C++ mangling produces matching symbols.
struct ATDebuggerSourceFileInfo;
class IATSourceWindow;

void ATUIDebuggerOpen() {}
void ATUIDebuggerClose() {}
bool ATUIDebuggerIsOpen() { return false; }
void ATUIDebuggerShowSourceListDialog() {}
void ATUIDebuggerClosePaneById(unsigned int /*id*/) {}

IATSourceWindow* ATImGuiFindSourceWindow(const wchar_t* /*path*/) { return nullptr; }
IATSourceWindow* ATImGuiOpenSourceWindow(const wchar_t* /*path*/) { return nullptr; }

void ATUISetPanZoomToolActive(bool /*active*/) {}

// uiaccessors_stubs.cpp's ATUIBootImage(...) calls ATUIPushDeferred
// to queue a boot through the SDL3 deferred-action queue. The bridge
// server has no such queue — and the bridge module routes its own
// BOOT command through bridge_main_glue.h instead, calling
// sim.Load() directly. The settings.cpp / cmd handler path that
// would otherwise call ATUIBootImage is never invoked from headless
// mode (no menu, no command system). We provide a no-op stub so the
// link succeeds; if anything ever DOES call it at runtime, the boot
// silently does nothing.
//
// The signature MUST match the real declaration in ui_main.h
// (ATDeferredActionType, const char*, int) so the C++ mangled name
// matches what the linker is looking for.
void ATUIPushDeferred(ATDeferredActionType /*type*/, const char* /*path*/, int /*extra*/) {}

// settings.cpp's full-screen exchange path calls ATSetFullscreen.
// The bridge server has no window so this is a no-op.
void ATSetFullscreen(bool /*fs*/) {}

// =========================================================================
// IATUIRenderer no-op implementation
//
// ATDiskInterface::Init dereferences the IATUIRenderer pointer (no
// nullptr check), so we need a real vtable. We can't link
// AltirraSDL's source/ui/core/ui_indicators.cpp because it actively
// uses ImGui (ImDrawList, GetForegroundDrawList, IM_COL32) and we
// don't link Dear ImGui. Instead we provide a complete no-op
// implementation here that satisfies both the IATUIRenderer and
// IATDeviceIndicatorManager interfaces.
//
// All methods are stubs. Disk LEDs, status messages, audio displays,
// FPS indicators, and the like simply discard their inputs — there's
// no UI to render to.
// =========================================================================

#include <vd2/system/vdtypes.h>
#include <vd2/system/refcount.h>
#include <vd2/system/vectors.h>
#include "uirender.h"

namespace {

class ATBridgeServerNullUIRenderer final : public IATUIRenderer {
public:
	ATBridgeServerNullUIRenderer() = default;
	~ATBridgeServerNullUIRenderer() = default;

	// IVDRefCount
	int AddRef() override { return mRefCount.inc(); }
	int Release() override {
		int n = mRefCount.dec();
		if (!n) delete this;
		return n;
	}

	// IATUIRenderer
	bool IsVisible() const override { return false; }
	void SetVisible(bool) override {}
	void SetCyclesPerSecond(double) override {}
	void SetLedStatus(uint8) override {}
	void SetHeldButtonStatus(uint8) override {}
	void SetPendingHoldMode(bool) override {}
	void SetPendingHeldKey(int) override {}
	void SetPendingHeldButtons(uint8) override {}
	void ClearWatchedValue(int) override {}
	void SetWatchedValue(int, uint32, WatchFormat) override {}
	void SetTracingSize(sint64) override {}
	void SetAudioStatus(const ATUIAudioStatus*) override {}
	void SetAudioMonitor(bool, ATAudioMonitor*) override {}
	void SetAudioDisplayEnabled(bool, bool) override {}
	void SetAudioScopeEnabled(bool) override {}
	void SetSlightSID(ATSlightSIDEmulator*) override {}
	vdrect32 GetPadArea() const override { return vdrect32(0, 0, 0, 0); }
	void SetPadInputEnabled(bool) override {}
	void SetFpsIndicator(float) override {}
	void SetMessage(StatusPriority, const wchar_t*) override {}
	void ClearMessage(StatusPriority) override {}
	void SetHoverTip(int, int, const wchar_t*) override {}
	void SetPaused(bool) override {}
	void SetUIManager(ATUIManager*) override {}
	void Relayout(int, int) override {}
	void Update() override {}
	sint32 GetIndicatorSafeHeight() const override { return 0; }
	void AddIndicatorSafeHeightChangedHandler(const vdfunction<void()>*) override {}
	void RemoveIndicatorSafeHeightChangedHandler(const vdfunction<void()>*) override {}
	void BeginCustomization() override {}

	// IATDeviceIndicatorManager
	void SetStatusFlags(uint32) override {}
	void ResetStatusFlags(uint32, uint32) override {}
	void PulseStatusFlags(uint32) override {}
	void SetStatusCounter(uint32, uint32) override {}
	void SetDiskLEDState(uint32, sint32) override {}
	void SetDiskMotorActivity(uint32, bool) override {}
	void SetDiskErrorState(uint32, bool) override {}
	void SetHActivity(bool) override {}
	void SetIDEActivity(bool, uint32) override {}
	void SetPCLinkActivity(bool) override {}
	void SetFlashWriteActivity() override {}
	void SetCartridgeActivity(sint32, sint32) override {}
	void SetCassetteIndicatorVisible(bool) override {}
	void SetCassettePosition(float, float, bool, bool) override {}
	void SetRecordingPosition() override {}
	void SetRecordingPositionPaused() override {}
	void SetRecordingPosition(float, sint64, bool) override {}
	void SetModemConnection(const char*) override {}
	void SetStatusMessage(const wchar_t*) override {}
	uint32 AllocateErrorSourceId() override { return 0; }
	void ClearErrors(uint32) override {}
	void ReportError(uint32, const wchar_t*) override {}

private:
	VDAtomicInt mRefCount { 0 };
};

}  // namespace

void ATCreateUIRenderer(IATUIRenderer **r) {
	if (!r) return;
	*r = new ATBridgeServerNullUIRenderer();
	(*r)->AddRef();
}

// =========================================================================
// VDVideoDisplayFrame implementation
//
// The full VDDisplay/source/display.cpp pulls in <windows.h> via
// w32assist.h and isn't portable. ATFrameBuffer (in gtia.cpp) derives
// from VDVideoDisplayFrame and only needs the constructor / virtual
// destructor / AddRef / Release symbols. We inline them here, same
// as the SDL3 frontend's display_sdl3.cpp does.
// =========================================================================

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

// =========================================================================
// Null IVDVideoDisplay — required for GTIA to actually produce frames.
//
// ATGTIAEmulator::BeginFrame() bails out immediately if mpDisplay is
// null, so the simulator never populates mpLastFrame and SCREENSHOT /
// RAWSCREEN / RENDER_FRAME return "no frame available". The null
// display has no output — it just pockets the frame ref the simulator
// posts each VBI and hands it back on RevokeBuffer so the frame
// allocator can reuse it. GetLastFrameBuffer() reads directly from
// mpLastFrame inside GTIA and is unaffected by what we do here.
// =========================================================================

class ATBridgeNullVideoDisplay final : public IVDVideoDisplay {
public:
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
		if (!frame) return;
		// Cycle pending → prev so the frame allocator can recycle via
		// RevokeBuffer. Matches the SDL3 display's approach.
		if (mPending) {
			if (mPrev) mPrev->Release();
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
		if (mPending) { mPending->Release(); mPending = nullptr; }
		if (mPrev)    { mPrev->Release();    mPrev    = nullptr; }
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
	VDVideoDisplayFrame *mPrev    = nullptr;
};

IVDVideoDisplay *ATBridgeCreateNullVideoDisplay() {
	return new ATBridgeNullVideoDisplay();
}

// debugger.cpp's ActivateSourceWindow() asks the UI to open or focus
// a debug pane. Bridge server has no panes — no-op.
void ATActivateUIPane(unsigned int /*id*/, bool /*giveFocus*/, bool /*activate*/,
                      unsigned int /*subId*/, int /*subVal*/) {}

// debuggerautotest.cpp uses the global command manager to dispatch
// "autotest" commands by name. Custom-device VM (customdevicevmtypes.cpp)
// also pushes commands through it. Bridge server doesn't populate a
// command registry; provide an empty global so calls return
// ExecuteCommandNT==false (no commands registered).
#include <at/atui/uicommandmanager.h>

ATUICommandManager g_ATUICommandMgr;

ATUICommandManager& ATUIGetCommandManager() {
	return g_ATUICommandMgr;
}
