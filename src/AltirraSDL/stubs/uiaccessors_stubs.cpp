//	Altirra SDL3 frontend - UI accessor stubs
//
//	Provides implementations of all ATUIGetXxx/ATUISetXxx functions,
//	g_kbdOpts, g_ATUIManager, and related symbols that settings.cpp
//	and other emulation code reference.
//
//	State-storage functions use static variables so that settings.cpp
//	can read/write them through the normal VDRegistryKey path.  UI
//	action functions are no-ops until the ImGui UI implements them.

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/vectors.h>
#include <vd2/system/VDString.h>
#include <vd2/system/vdstl.h>
#include "uiaccessors.h"
#include "uiconfirm.h"
#include "uikeyboard.h"
#include "uimenu.h"
#include "uitypes.h"
#include "simulator.h"
#include "devicemanager.h"
#include <at/atcore/device.h>
#include <vd2/system/text.h>
#include "constants.h"
#include "settings.h"
#include "ui_main.h"

extern ATSimulator g_sim;
#include <at/atui/uimanager.h>

// Forward declarations for types used in stub signatures
class ATInputManager;
class IATAsyncDispatcher;
class IATDisplayPane;
enum ATHardwareMode : uint32;
enum ATMemoryMode : uint32;
enum ATVideoStandard : uint32;

// =========================================================================
// Globals expected by settings.cpp
// =========================================================================

ATUIKeyboardOptions g_kbdOpts = {};
ATUIManager g_ATUIManager;

// ATUIManager stub methods
static VDStringW s_customEffectPath;

const wchar_t *ATUIManager::GetCustomEffectPath() const {
	return s_customEffectPath.c_str();
}

void ATUIManager::SetCustomEffectPath(const wchar_t *s, bool) {
	s_customEffectPath = s ? s : L"";
}

// =========================================================================
// Display filter / stretch
// =========================================================================

static ATDisplayFilterMode s_displayFilterMode = kATDisplayFilterMode_AnySuitable;
ATDisplayFilterMode ATUIGetDisplayFilterMode() { return s_displayFilterMode; }
void ATUISetDisplayFilterMode(ATDisplayFilterMode mode) { s_displayFilterMode = mode; }

static int s_viewFilterSharpness = 0;
int ATUIGetViewFilterSharpness() { return s_viewFilterSharpness; }
void ATUISetViewFilterSharpness(int v) { s_viewFilterSharpness = v; }

static ATDisplayStretchMode s_displayStretchMode = kATDisplayStretchMode_PreserveAspectRatio;
ATDisplayStretchMode ATUIGetDisplayStretchMode() { return s_displayStretchMode; }
void ATUISetDisplayStretchMode(ATDisplayStretchMode mode) { s_displayStretchMode = mode; }

// =========================================================================
// Display indicators
// =========================================================================

static bool s_displayIndicators = true;
bool ATUIGetDisplayIndicators() { return s_displayIndicators; }
void ATUISetDisplayIndicators(bool v) { s_displayIndicators = v; }

static bool s_displayPadIndicators = false;
bool ATUIGetDisplayPadIndicators() { return s_displayPadIndicators; }
void ATUISetDisplayPadIndicators(bool v) { s_displayPadIndicators = v; }

static bool s_drawPadBounds = false;
bool ATUIGetDrawPadBoundsEnabled() { return s_drawPadBounds; }
void ATUISetDrawPadBoundsEnabled(bool v) { s_drawPadBounds = v; }

static bool s_drawPadPointers = false;
bool ATUIGetDrawPadPointersEnabled() { return s_drawPadPointers; }
void ATUISetDrawPadPointersEnabled(bool v) { s_drawPadPointers = v; }

// =========================================================================
// Pointer / mouse
// =========================================================================

static bool s_pointerAutoHide = true;
bool ATUIGetPointerAutoHide() { return s_pointerAutoHide; }
void ATUISetPointerAutoHide(bool v) { s_pointerAutoHide = v; }

static bool s_constrainMouseFS = false;
bool ATUIGetConstrainMouseFullScreen() { return s_constrainMouseFS; }
void ATUISetConstrainMouseFullScreen(bool v) { s_constrainMouseFS = v; }

static bool s_targetPointerVisible = false;
bool ATUIGetTargetPointerVisible() { return s_targetPointerVisible; }
void ATUISetTargetPointerVisible(bool v) { s_targetPointerVisible = v; }

static bool s_mouseAutoCapture = true;
bool ATUIGetMouseAutoCapture() { return s_mouseAutoCapture; }
void ATUISetMouseAutoCapture(bool v) { s_mouseAutoCapture = v; }

static bool s_rawInput = false;
bool ATUIGetRawInputEnabled() { return s_rawInput; }
void ATUISetRawInputEnabled(bool v) { s_rawInput = v; }

// =========================================================================
// Display zoom / pan
// =========================================================================

static float s_displayZoom = 1.0f;
float ATUIGetDisplayZoom() { return s_displayZoom; }
void ATUISetDisplayZoom(float v) { s_displayZoom = v; }

static vdfloat2 s_displayPan = {0, 0};
vdfloat2 ATUIGetDisplayPanOffset() { return s_displayPan; }
void ATUISetDisplayPanOffset(const vdfloat2& v) { s_displayPan = v; }

// =========================================================================
// Menu
// =========================================================================

static bool s_menuAutoHide = false;
bool ATUIIsMenuAutoHideEnabled() { return s_menuAutoHide; }
bool ATUIIsMenuAutoHideActive() { return false; }
void ATUISetMenuAutoHideEnabled(bool v) { s_menuAutoHide = v; }
bool ATUIIsMenuAutoHidden() { return false; }
void ATUISetMenuAutoHidden(bool) {}
void ATUISetMenuHidden(bool) {}
void ATUISetMenuFullScreenHidden(bool) {}

// =========================================================================
// View / output
// =========================================================================

static VDStringA s_altOutputName;
const char *ATUIGetCurrentAltOutputName() { return s_altOutputName.c_str(); }
void ATUISetCurrentAltOutputName(const char *s) { s_altOutputName = s ? s : ""; }
void ATUIToggleAltOutput(const char *) {}
bool ATUIIsAltOutputAvailable() { return false; }

bool ATUIIsXEPViewEnabled() { return false; }
void ATUISetXEPViewEnabled(bool) {}

static bool s_altViewEnabled = false;
bool ATUIGetAltViewEnabled() { return s_altViewEnabled; }
void ATUISetAltViewEnabled(bool v) { s_altViewEnabled = v; }

sint32 ATUIGetCurrentAltViewIndex() { return -1; }
void ATUISetAltViewByIndex(sint32) {}
void ATUISelectPrevAltOutput() {}
void ATUISelectNextAltOutput() {}

static bool s_altViewAutoSwitch = true;
bool ATUIGetAltViewAutoswitchingEnabled() { return s_altViewAutoSwitch; }
void ATUISetAltViewAutoswitchingEnabled(bool v) { s_altViewAutoSwitch = v; }

static bool s_showFPS = false;
bool ATUIGetShowFPS() { return s_showFPS; }
void ATUISetShowFPS(bool v) { s_showFPS = v; }

// =========================================================================
// Speed control
// =========================================================================

static float s_speedModifier = 0.0f;
float ATUIGetSpeedModifier() { return s_speedModifier; }
void ATUISetSpeedModifier(float v) { s_speedModifier = v; }

static ATFrameRateMode s_frameRateMode = kATFrameRateMode_Hardware;
ATFrameRateMode ATUIGetFrameRateMode() { return s_frameRateMode; }
void ATUISetFrameRateMode(ATFrameRateMode mode) { s_frameRateMode = mode; }

static bool s_vsyncAdaptive = false;
bool ATUIGetFrameRateVSyncAdaptive() { return s_vsyncAdaptive; }
void ATUISetFrameRateVSyncAdaptive(bool v) { s_vsyncAdaptive = v; }

static bool s_turbo = false;
static bool s_turboPulse = false;

bool ATUIGetTurbo() { return s_turbo; }
void ATUISetTurbo(bool v) {
	s_turbo = v;
	g_sim.SetTurboModeEnabled(v || s_turboPulse);
}

bool ATUIGetTurboPulse() { return s_turboPulse; }
void ATUISetTurboPulse(bool v) {
	s_turboPulse = v;
	g_sim.SetTurboModeEnabled(s_turbo || v);
}

static bool s_slowMotion = false;
bool ATUIGetSlowMotion() { return s_slowMotion; }
void ATUISetSlowMotion(bool v) { s_slowMotion = v; }

// =========================================================================
// Fullscreen
// =========================================================================

// ATSetFullscreen(bool) is implemented in main_sdl3.cpp (queries SDL window state).
// ATUIGetFullscreen/ATUIGetDisplayFullscreen query SDL directly.
extern SDL_Window *g_pWindow;
bool ATUIGetFullscreen() {
	return g_pWindow && (SDL_GetWindowFlags(g_pWindow) & SDL_WINDOW_FULLSCREEN) != 0;
}
bool ATUIGetDisplayFullscreen() { return ATUIGetFullscreen(); }
// ATSetFullscreen(bool) defined in main_sdl3.cpp

// =========================================================================
// System state
// =========================================================================

static bool s_pauseWhenInactive = true;
bool ATUIGetPauseWhenInactive() { return s_pauseWhenInactive; }
void ATUISetPauseWhenInactive(bool v) { s_pauseWhenInactive = v; }

static uint32 s_bootUnloadMask = 0;
uint32 ATUIGetBootUnloadStorageMask() { return s_bootUnloadMask; }
void ATUISetBootUnloadStorageMask(uint32 v) { s_bootUnloadMask = v; }

static VDStringA s_windowCaption;
const char *ATUIGetWindowCaptionTemplate() { return s_windowCaption.c_str(); }
void ATUISetWindowCaptionTemplate(const char *s) { s_windowCaption = s ? s : ""; }

// =========================================================================
// Enhanced text mode
// =========================================================================

static ATUIEnhancedTextMode s_enhTextMode = kATUIEnhancedTextMode_None;
ATUIEnhancedTextMode ATUIGetEnhancedTextMode() { return s_enhTextMode; }
void ATUISetEnhancedTextMode(ATUIEnhancedTextMode mode) { s_enhTextMode = mode; }

// =========================================================================
// Reset flags (uiconfirm.h)
// =========================================================================

static uint32 s_resetFlags = kATUIResetFlag_Default;
uint32 ATUIGetResetFlags() { return s_resetFlags; }
void ATUISetResetFlags(uint32 v) { s_resetFlags = v; }
bool ATUIIsResetNeeded(uint32 flag) { return (s_resetFlags & flag) != 0; }
void ATUIModifyResetFlag(uint32 flag, bool state) {
	if (state) s_resetFlags |= flag; else s_resetFlags &= ~flag;
}

// =========================================================================
// Recording status
// =========================================================================

ATUIRecordingStatus ATUIGetRecordingStatus() { return kATUIRecordingStatus_None; }

// =========================================================================
// Keyboard (uikeyboard.h)
// =========================================================================

static vdfastvector<uint32> s_customKeyMap;

void ATUIGetCustomKeyMap(vdfastvector<uint32>& mappings) {
	mappings = s_customKeyMap;
}

void ATUISetCustomKeyMap(const uint32 *mappings, size_t n) {
	s_customKeyMap.assign(mappings, mappings + n);
}

void ATUIInitVirtualKeyMap(const ATUIKeyboardOptions&) {
	// No-op on SDL3 — keyboard mapping is handled by input_sdl3.cpp
}

// =========================================================================
// Port menus — SDL3 ImGui port menus are rendered inline in ui_main.cpp.
// These stubs satisfy the linker for code that calls the Win32 port menu
// functions (e.g. settings.cpp after profile switch).  The SDL3 UI
// re-queries ATInputManager directly each frame, so no-ops are fine.
// =========================================================================

void ATInitPortMenus(ATInputManager *) {}
void ATUpdatePortMenus() {}
void ATShutdownPortMenus() {}
void ATReloadPortMenus() {}
bool ATUIHandlePortMenuCommand(uint32) { return false; }

// =========================================================================
// Functions forward-declared in settings.cpp — no-ops for SDL3
// =========================================================================

void ATSyncCPUHistoryState() {}
void ATUIUpdateSpeedTiming() {}
void ATUIResizeDisplay() {}

// =========================================================================
// Mouse capture — real SDL3 implementation
// =========================================================================

#include <SDL3/SDL.h>
#include "inputmanager.h"

// The SDL3 window pointer, set by ATUISetMouseCaptureWindow() from main.
static SDL_Window *s_pMouseCaptureWindow = nullptr;
static bool s_mouseCaptured = false;

void ATUISetMouseCaptureWindow(SDL_Window *window) {
	s_pMouseCaptureWindow = window;
}

bool ATUIIsMouseCaptured() {
	return s_mouseCaptured;
}

void ATUICaptureMouse() {
	if (s_mouseCaptured || !s_pMouseCaptureWindow)
		return;

	ATInputManager *im = g_sim.GetInputManager();
	if (!im || !im->IsMouseMapped() || !g_sim.IsRunning())
		return;

	s_mouseCaptured = true;

	if (im->IsMouseAbsoluteMode()) {
		// Absolute mode (light pen): confine cursor to window
		SDL_SetWindowMouseGrab(s_pMouseCaptureWindow, true);
	} else {
		// Relative mode (paddle/mouse): hide cursor and use relative motion
		SDL_SetWindowRelativeMouseMode(s_pMouseCaptureWindow, true);
	}
}

void ATUIReleaseMouse() {
	if (!s_mouseCaptured || !s_pMouseCaptureWindow)
		return;

	s_mouseCaptured = false;
	SDL_SetWindowRelativeMouseMode(s_pMouseCaptureWindow, false);
	SDL_SetWindowMouseGrab(s_pMouseCaptureWindow, false);
}

// =========================================================================
// Misc UI functions — no-ops / simple stubs
// =========================================================================

IATDisplayPane *ATUIGetDisplayPane() { return nullptr; }

// ---------------------------------------------------------------------------
// ATUISwitchHardwareMode — real implementation matching Windows main.cpp
//
// Handles: 5200 mode switching (unload all, default cart, 16K memory),
// profile switching, incompatible kernel reset, NTSC enforcement for 5200,
// and cold reset.
// ---------------------------------------------------------------------------
bool ATUISwitchHardwareMode(VDGUIHandle h, ATHardwareMode mode, bool switchProfiles) {
	ATHardwareMode prevMode = g_sim.GetHardwareMode();
	if (prevMode == mode)
		return true;

	ATDefaultProfile defaultProfile;
	switch (mode) {
		case kATHardwareMode_800:
			defaultProfile = kATDefaultProfile_800;
			break;
		case kATHardwareMode_800XL:
		case kATHardwareMode_130XE:
		default:
			defaultProfile = kATDefaultProfile_XL;
			break;
		case kATHardwareMode_5200:
			defaultProfile = kATDefaultProfile_5200;
			break;
		case kATHardwareMode_XEGS:
			defaultProfile = kATDefaultProfile_XEGS;
			break;
		case kATHardwareMode_1200XL:
			defaultProfile = kATDefaultProfile_1200XL;
			break;
	}

	const uint32 oldProfileId = ATSettingsGetCurrentProfileId();
	const uint32 newProfileId = ATGetDefaultProfileId(defaultProfile);
	const bool switchingProfile = switchProfiles && (newProfileId != kATProfileId_Invalid && newProfileId != oldProfileId);

	// Switch profile if necessary
	if (switchingProfile)
		ATSettingsSwitchProfile(newProfileId);

	// Check if we are switching to or from 5200 mode
	const bool switching5200 = (mode == kATHardwareMode_5200 || prevMode == kATHardwareMode_5200);
	if (switching5200) {
		g_sim.UnloadAll();

		// 5200 mode needs the default cart and 16K memory
		if (mode == kATHardwareMode_5200) {
			g_sim.LoadCartridge5200Default();
			g_sim.SetMemoryMode(kATMemoryMode_16K);
		}
	}

	g_sim.SetHardwareMode(mode);

	// Check for incompatible kernel
	switch (g_sim.GetKernelMode()) {
		case kATKernelMode_Default:
			break;
		case kATKernelMode_XL:
			if (!kATHardwareModeTraits[mode].mbRunsXLOS)
				g_sim.SetKernel(0);
			break;
		case kATKernelMode_5200:
			if (mode != kATHardwareMode_5200)
				g_sim.SetKernel(0);
			break;
		default:
			if (mode == kATHardwareMode_5200)
				g_sim.SetKernel(0);
			break;
	}

	// If we are in 5200 mode, we must be in NTSC
	if (mode == kATHardwareMode_5200 && g_sim.GetVideoStandard() != kATVideoStandard_NTSC) {
		g_sim.SetVideoStandard(kATVideoStandard_NTSC);
		ATUIUpdateSpeedTiming();
	}

	g_sim.ColdReset();
	return true;
}

// ---------------------------------------------------------------------------
// ATUISwitchMemoryMode — real implementation matching Windows main.cpp
//
// Validates memory mode compatibility with hardware mode:
// - 5200: only 16K allowed
// - 800XL: no 48K/52K/8K/24K/32K/40K
// - 1200XL/XEGS/130XE/1400XL: no 48K/52K/8K-40K
// Cold resets after change.
// ---------------------------------------------------------------------------
void ATUISwitchMemoryMode(VDGUIHandle h, ATMemoryMode mode) {
	if (g_sim.GetMemoryMode() == mode)
		return;

	switch (g_sim.GetHardwareMode()) {
		case kATHardwareMode_5200:
			if (mode != kATMemoryMode_16K)
				return;
			break;
		case kATHardwareMode_800XL:
			if (mode == kATMemoryMode_48K ||
				mode == kATMemoryMode_52K ||
				mode == kATMemoryMode_8K ||
				mode == kATMemoryMode_24K ||
				mode == kATMemoryMode_32K ||
				mode == kATMemoryMode_40K)
				return;
			break;
		case kATHardwareMode_1200XL:
		case kATHardwareMode_XEGS:
		case kATHardwareMode_130XE:
		case kATHardwareMode_1400XL:
			if (mode == kATMemoryMode_48K ||
				mode == kATMemoryMode_52K ||
				mode == kATMemoryMode_8K ||
				mode == kATMemoryMode_16K ||
				mode == kATMemoryMode_24K ||
				mode == kATMemoryMode_32K ||
				mode == kATMemoryMode_40K)
				return;
			break;
	}

	g_sim.SetMemoryMode(mode);
	g_sim.ColdReset();
}
static bool s_driveSounds = true;
bool ATUIGetDriveSoundsEnabled() { return s_driveSounds; }
void ATUISetDriveSoundsEnabled(bool v) { s_driveSounds = v; }
void ATUIRecalibrateLightPen() {}
void ATUIActivatePanZoomTool() {}
void ATUIOpenOnScreenKeyboard() {}
static bool s_holdKeysActive = false;
void ATUIToggleHoldKeys() {
	s_holdKeysActive = !s_holdKeysActive;

	if (!s_holdKeysActive) {
		g_sim.ClearPendingHeldKey();
		g_sim.SetPendingHeldSwitches(0);
	}
}
bool ATUICanManipulateWindows() { return false; }
bool ATUIIsModalActive() { return false; }

VDGUIHandle ATUIGetMainWindow() { return nullptr; }
VDGUIHandle ATUIGetNewPopupOwner() { return nullptr; }
static bool s_appActive = true;
bool ATUIGetAppActive() { return s_appActive; }
void ATUISetAppActive(bool v) { s_appActive = v; }
void ATUIExit(bool) {}

// Device button support — real implementation matching Windows main.cpp.
static uint32 g_devBtnMask = 0;
static uint32 g_devBtnCC = 0;
static void UpdateDevBtnMask() {
	ATDeviceManager& dm = *g_sim.GetDeviceManager();
	uint32 cc = dm.GetChangeCounter();
	if (g_devBtnCC != cc) { g_devBtnCC = cc; uint32 m = 0;
		for (IATDeviceButtons *db : dm.GetInterfaces<IATDeviceButtons>(false, false, false)) m |= db->GetSupportedButtons();
		g_devBtnMask = m; }
}
bool ATUIGetDeviceButtonSupported(uint32 idx) { UpdateDevBtnMask(); return (g_devBtnMask & (1 << idx)) != 0; }
bool ATUIGetDeviceButtonDepressed(uint32 idx) { ATDeviceManager& dm = *g_sim.GetDeviceManager();
	for (IATDeviceButtons *db : dm.GetInterfaces<IATDeviceButtons>(false, false, false))
		if (db->GetSupportedButtons() & (1 << idx)) return db->IsButtonDepressed((ATDeviceButton)idx);
	return false; }
void ATUIActivateDeviceButton(uint32 idx, bool state) { UpdateDevBtnMask();
	if (!(g_devBtnMask & (1 << idx))) return; ATDeviceManager& dm = *g_sim.GetDeviceManager();
	for (IATDeviceButtons *db : dm.GetInterfaces<IATDeviceButtons>(false, false, false)) db->ActivateButton((ATDeviceButton)idx, state); }

// Scan code lookup for text paste (extracted from uikeyboard.cpp)
#include "uikeyboard.h"
static const uint8 kScanMap[128] = {
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0x21,0x5F,0x5E,0x5A,0x58,0x5D,0x5B,0x73, 0x70,0x72,0x07,0x06,0x20,0x0E,0x22,0x26,
	0x32,0x1F,0x1E,0x1A,0x18,0x1D,0x1B,0x33, 0x35,0x30,0x42,0x02,0x36,0x0F,0x37,0x66,
	0x75,0x7F,0x55,0x52,0x7A,0x6A,0x78,0x7D, 0x79,0x4D,0x41,0x45,0x40,0x65,0x63,0x48,
	0x4A,0x6F,0x68,0x7E,0x6D,0x4B,0x50,0x6E, 0x56,0x6B,0x57,0x60,0x46,0x62,0x47,0x4E,
	0x27,0x3F,0x15,0x12,0x3A,0x2A,0x38,0x3D, 0x39,0x0D,0x01,0x05,0x00,0x25,0x23,0x08,
	0x0A,0x2F,0x28,0x3E,0x2D,0x0B,0x10,0x2E, 0x16,0x2B,0x17,0xFF,0x4F,0xFF,0x67,0xFF,
};
bool ATUIGetDefaultScanCodeForCharacter(uint32 c32, uint8& ch) {
	if (c32 < 128 && kScanMap[c32] != 0xFF) { ch = kScanMap[c32]; return true; }
	switch(c32) {
		case 0x2665: ch=0xA0; return true; case 0x251C: ch=0xBF; return true;
		case 0x2518: ch=0x92; return true; case 0x2524: ch=0xBA; return true;
		case 0x2510: ch=0xAA; return true; case 0x250C: ch=0xAF; return true;
		case 0x2500: ch=0xA8; return true; case 0x253C: ch=0xBE; return true;
		case 0x2502: ch=0x9A; return true; case 0x2514: ch=0xA6; return true;
		default: return false;
	}
}

void ATUIBootImage(const wchar_t *path) {
	if (path && *path) { VDStringA u8 = VDTextWToU8(VDStringW(path)); ATUIPushDeferred(kATDeferred_BootImage, u8.c_str()); }
}

static IATAsyncDispatcher *s_dispatcher = nullptr;
IATAsyncDispatcher *ATUIGetDispatcher() { return s_dispatcher; }
void ATUISetDispatcher(IATAsyncDispatcher *d) { s_dispatcher = d; }

void ATSetVideoStandard(ATVideoStandard vs) {
	// Matches Windows main.cpp:2644 — set standard + update timing, NO cold reset.
	// Cold reset is handled separately by the caller via ATUIConfirmResetComplete()
	// only if kATUIResetFlag_VideoStandardChange is set.
	if (g_sim.GetHardwareMode() == kATHardwareMode_5200)
		return;
	g_sim.SetVideoStandard(vs);
	ATUIUpdateSpeedTiming();
}

// ATUIGetManager — return our stub global
ATUIManager& ATUIGetManager() { return g_ATUIManager; }

// Enum tables for ATDebuggerSymbolLoadMode and ATDebuggerScriptAutoLoadMode
// are now provided by debugger.cpp (no longer excluded from SDL3 build).

// ATUIGetCommandManager — needed by debuggerautotest.cpp.
// ATUICommandManager implementation is now compiled from ATUI/source/uicommandmanager.cpp.
#include <at/atui/uicommandmanager.h>
static ATUICommandManager s_cmdMgr;
ATUICommandManager& ATUIGetCommandManager() { return s_cmdMgr; }
