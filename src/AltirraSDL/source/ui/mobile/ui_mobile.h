//	AltirraSDL - Mobile UI (hamburger menu, settings, file browser)
//	Streamlined touch-first UI for Android phones and tablets.

#pragma once

#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <functional>
#include "touch_layout.h"

struct ATUIState;
class ATSimulator;
struct SDL_Window;

enum class ATMobileUIScreen {
	None,            // Normal emulation (controls visible)
	HamburgerMenu,   // Slide-in menu panel
	FileBrowser,     // Full-screen file browser
	Settings,        // Full-screen settings panel
	FirstRunWizard,  // First-boot firmware setup
	About,           // Full-screen About panel
	DiskManager,     // Full-screen Disk Drive manager
	GameBrowser      // Full-screen Game Library browser
};

struct ATMobileUIState {
	ATMobileUIScreen currentScreen = ATMobileUIScreen::None;

	// Touch layout
	ATTouchLayout layout;
	ATTouchLayoutConfig layoutConfig;

	// File browser state
	VDStringW currentDir;
	int selectedFileIdx = -1;

	// Settings modified flag
	bool settingsDirty = false;

	// Audio muted state (independent of emulator volume)
	bool audioMuted = false;

	// Whether a game is loaded (affects idle screen)
	bool gameLoaded = false;

	// Top bar auto-hide timer (seconds of inactivity)
	float topBarTimer = 0.0f;
	bool topBarVisible = true;

	// --- Auto save-state ---
	// When enabled, the emulator snapshots its state whenever the
	// app is backgrounded (SDL_EVENT_WILL_ENTER_BACKGROUND /
	// TERMINATING).  When enabled, the snapshot is automatically
	// reloaded at startup so the user resumes exactly where they
	// left off — important because Android will kill backgrounded
	// apps at any time (low memory, incoming call, swipe away).
	bool autoSaveOnSuspend = true;
	bool autoRestoreOnStart = true;

	// --- Visual effects (CRT look) ---
	// Scanlines work in both SDL_Renderer and GL backends (CPU path).
	// Bloom and distortion require the GL display backend; they are
	// silently no-op on the SDL_Renderer fallback.  The toggles stay
	// exposed regardless so the user can enable them ahead of a
	// future GL build without losing their preference.
	bool fxScanlines = false;
	bool fxBloom     = false;
	bool fxDistortion = false;
	bool fxApertureGrille = false;
	bool fxVignette = true;

	// --- Performance preset ---
	// 0 = Efficient, 1 = Balanced (default), 2 = Quality, 3 = Custom.
	// Changing the preset drives the three fx* toggles + filter mode;
	// manually flipping one of those fields switches the preset to
	// Custom so the user can see they've left the bundle.
	int performancePreset = 1;

	// --- Interface scale ---
	// 0 = Small (0.75×), 1 = Standard (1.0×), 2 = Large (1.25×).
	// Multiplies the DPI-based content scale so the user can shrink
	// UI chrome on devices where headers and shortcut bars eat too
	// much screen in landscape, or enlarge it for accessibility.
	int interfaceScale = 1;

	// --- On-screen touch controls ---
	// When false the joystick, fire buttons, and console keys drawn by
	// ATTouchControls_Render are hidden.  The hamburger icon has its
	// own toggle (showHamburgerMenu) so a user watching a demo can
	// leave only the menu button visible.
	// Default true on Android (primary input), false on desktop
	// (keyboard/mouse/gamepad are the primary input).
#ifdef __ANDROID__
	bool showTouchControls = true;
#else
	bool showTouchControls = false;
#endif

	// --- On-screen hamburger menu button ---
	// Independent of showTouchControls so the user can hide the joystick
	// / fire / console controls while keeping the menu icon visible
	// (e.g. demoscene viewing).  Default: visible.
	bool showHamburgerMenu = true;
};

// Initialize mobile UI (call once at startup, after ImGui init)
void ATMobileUI_Init();

// Load/save mobile-only settings (control size, opacity, haptic)
// from the persistent registry under the "Mobile" key.
void ATMobileUI_LoadConfig(ATMobileUIState &mobileState);
void ATMobileUI_SaveConfig(const ATMobileUIState &mobileState);

// Push the user's visual-effects toggles (scanlines / bloom /
// distortion) into the GTIA.  Safe on every backend — effects
// that the backend doesn't support are silently no-op.  Call on
// startup after settings load, and whenever a toggle changes.
void ATMobileUI_ApplyVisualEffects(const ATMobileUIState &mobileState);

// Apply a performance preset — updates the fx* toggles and filter
// mode according to the selected preset (Efficient / Balanced /
// Quality).  Called on preset change and at startup after
// LoadConfig.  Preset = 3 (Custom) is a no-op.
void ATMobileUI_ApplyPerformancePreset(ATMobileUIState &mobileState);

// Force the mobile file browser to re-enumerate its current
// directory on the next render pass.  Called from the SDL3 event
// loop when the app returns from background (user may have
// granted storage access in the Settings app while we were gone).
void ATMobileUI_InvalidateFileBrowser();

// Auto save-state hooks — called from the SDL3 event loop in
// response to Android lifecycle events.  Safe no-ops if the feature
// is disabled in the user's settings, or if no game is loaded.
//
// SaveSuspendState: writes the current emulator state to a
//   well-known file under the config dir.  Called from
//   SDL_EVENT_WILL_ENTER_BACKGROUND and SDL_EVENT_TERMINATING.
// RestoreSuspendStateIfAny: at startup, loads the file if present
//   and restores the emulator to the previous state.  Returns true
//   if a state was restored.  The file is kept so a subsequent
//   crash can still recover.
void ATMobileUI_SaveSuspendState(class ATSimulator &sim,
	const ATMobileUIState &mobileState);
bool ATMobileUI_RestoreSuspendState(class ATSimulator &sim,
	ATMobileUIState &mobileState);
void ATMobileUI_ClearSuspendState();

// Check if this is a first run (no firmware configured or wizard
// not yet completed).  True until ATMobileUI_FinishFirstRun is called.
bool ATMobileUI_IsFirstRun();

// Main render entry point — call from ATUIRenderFrame() when in Gaming Mode
void ATMobileUI_Render(ATSimulator &sim, ATUIState &uiState,
	ATMobileUIState &mobileState, SDL_Window *window);

// Process SDL events for mobile UI. Returns true if consumed.
bool ATMobileUI_HandleEvent(const SDL_Event &ev, ATMobileUIState &mobileState);

// Open the hamburger menu (pauses emulation)
void ATMobileUI_OpenMenu(ATSimulator &sim, ATMobileUIState &mobileState);

// Navigate the global Gaming-Mode screen stack to the Game Library
// browser.  Exposed so external flows — netplay "Pick from Library",
// share-sheet handlers, etc. — can hand control to the browser
// without touching `g_mobileState` directly.  Does nothing if not in
// Gaming Mode.
void ATMobileUI_SwitchToGameBrowser();

// Close the hamburger menu (resumes emulation if not explicitly paused)
void ATMobileUI_CloseMenu(ATSimulator &sim, ATMobileUIState &mobileState);

// Open file browser
void ATMobileUI_OpenFileBrowser(ATMobileUIState &mobileState);

// Open settings
void ATMobileUI_OpenSettings(ATMobileUIState &mobileState);

// Open Settings directly on the Online Play preferences page with
// Back wired to return to the Online Play hub overlay.  Called by
// the Online Play hub's "Preferences" shortcut so the gaming-mode
// config tree stays the single place for every settings category.
void ATMobileUI_OpenOnlinePlaySettings();

// Public entry points to the mobile-style info and confirm sheet.
// Any non-mobile code path that would normally open a desktop modal
// can call these to get the full-screen mobile card renderer
// instead.  Safe to call from ui_main.cpp when ATUIIsGamingMode().
void ATMobileUI_ShowInfoModal(const char *title, const char *body);
void ATMobileUI_ShowConfirmDialog(const char *title, const char *body,
	std::function<void()> onConfirm);
