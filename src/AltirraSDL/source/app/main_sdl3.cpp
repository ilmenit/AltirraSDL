//	Altirra SDL3 frontend - main entry point
//	Integrates SDL3 window, emulator core, and Dear ImGui UI.
//
//	Frame pacing:
//	  The Windows version uses a waitable timer + error accumulator to
//	  sleep between frames and hit the exact Atari frame rate (~59.92 Hz
//	  NTSC, ~49.86 Hz PAL).  We replicate this with SDL_DelayPrecise()
//	  and SDL_GetPerformanceCounter().  Vsync is still enabled but only
//	  as a secondary backstop; the primary rate limiter is our own timer.

#include <stdafx.h>
#include <SDL3/SDL.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// Forward declarations of the per-tick drains added for the browser
// build — the WASM single-threaded scheduler (src/system/source/time_sdl3.cpp)
// and the JS upload/boot bridge (wasm_bridge.cpp).  Both are main-thread
// only; they are invoked from the tick lambda inside main().
extern "C" void VDWASMTimerTick();
extern void ATWasmBridgeTick();
extern "C" void ATWasmSyncFSOut();
#endif
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <string>

#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/registry.h>
#include <vd2/system/error.h>
#include <exception>
#include <at/atcore/media.h>
#include <at/atio/image.h>
#include "uiqueue.h"

#include "crash_report.h"
#include <imgui.h>
#include "display_sdl3_impl.h"
#include "display_backend.h"

#if ALTIRRA_HAS_BAKED_ICON
#include "altirra_icon_data.h"
#endif
#ifndef ALTIRRA_WASM
// The GL display backend is excluded from the WASM build — direct glXxx
// calls conflict with Emscripten's WebGL stubs at link time.  See
// src/AltirraSDL/CMakeLists.txt for the source-level exclusion; every
// call site in this file that references DisplayBackendGL, GL_* or
// SDL_GL_* is wrapped in `#ifndef ALTIRRA_WASM` below.
#include "display_backend_gl33.h"
#endif
#include "display_backend_sdl.h"
#include "gl_funcs.h"
#include "input_sdl3.h"
#include "touch_controls.h"
#include "ui_mobile.h"
#include "mobile_gamepad.h"
#include "mobile_internal.h"
#include "../ui/gamelibrary/game_library.h"
#include "ui_mode.h"
#include "options.h"
#include "ui_main.h"
#include "ui_debugger.h"
#include "debugger.h"   // IATDebugger + ATDebuggerSymbolLoadMode (used in the __EMSCRIPTEN__ startup block below)
#include "ui_testmode.h"
#ifdef ALTIRRA_BRIDGE_ENABLED
#include "bridge_server.h"
#endif

// Netplay and emote headers are included unconditionally.  When their
// modules are compiled in, the real namespace / functions are visible;
// when they are disabled (e.g. WASM build), each header exposes inline
// no-op stubs under `#ifndef ALTIRRA_NETPLAY_ENABLED` that satisfy the
// call sites in this file without `#ifdef` clutter.  See netplay_glue.h
// for the rationale.
#include "netplay/netplay_glue.h"
#include "ui/emotes/emote_picker.h"
#include "ui/emotes/emote_assets.h"
#include "ui/emotes/emote_netplay.h"
#include "ui/emotes/emote_overlay.h"
#ifdef ALTIRRA_NETPLAY_ENABLED
// ui_netplay.h has no stubs; its only call site
// (ATNetplayUI_Poll, below) is already inside an
// `#ifdef ALTIRRA_NETPLAY_ENABLED` block.
#include "ui/netplay/ui_netplay.h"
#endif
#include "ui_textselection.h"
#include "ui_progress.h"
#include "ui_emuerror.h"
#include "ui_virtual_keyboard.h"

#include "simulator.h"
#include "cassette.h"
#include "diskinterface.h"
#include "uikeyboard.h"
#include <at/ataudio/audiooutput.h>
#include "uiaccessors.h"
#include "inputmanager.h"
#include "inputmap.h"
#include "inputdefs.h"
#include "accel_sdl3.h"
#include "antic.h"
#include "gtia.h"
#include <at/ataudio/pokey.h>
#include "joystick.h"
#include "joystick_sdl3.h"
#include "firmwaremanager.h"
#include "settings.h"
#include "uitypes.h"
#include "uirender.h"

#include <at/atcore/constants.h>
#include <at/atcore/configvar.h>
#include <algorithm>
#include <cmath>
#include "logging.h"
#include <at/atcore/logging.h>
#include "app_internal.h"
#include "macos_menubar.h"

#ifdef __ANDROID__
#include <android/log.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include "android_platform.h"
#endif

ATSimulator g_sim;
extern ATUIKeyboardOptions g_kbdOpts;
VDVideoDisplaySDL3 *g_pDisplay = nullptr;
SDL_Window   *g_pWindow   = nullptr;  // non-static: accessed by console_stubs.cpp for fullscreen exit
static SDL_Renderer *g_pRenderer = nullptr;
static IDisplayBackend *g_pBackend = nullptr;
static IATJoystickManager *g_pJoystickMgr = nullptr;
static bool g_running = true;
static bool g_winActive = true;
// Android/mobile lifecycle: true while the app is backgrounded or the
// window is minimized/hidden.  When set, RenderAndPresent() and the
// simulator tick are both skipped so we do not touch a dead EGL surface
// and do not burn battery while the user cannot see us.
static bool g_appSuspended = false;
ATUIState g_uiState;

// Turbo-mode frame drop divisor. In turbo mode we render only 1 of every N
// frames at the GTIA framebuffer-allocation level, which lets the simulator
// run much faster than realtime without paying the per-frame artifacting /
// palette / upload cost. Matches the CVar name and default used by the
// Windows frontend (src/Altirra/source/main.cpp), so an "engine.turbo_fps_
// divisor" entry in settings behaves identically across platforms.
static ATConfigVarInt32 g_ATCVEngineTurboFPSDivisor("engine.turbo_fps_divisor", 16);

// =========================================================================
// Fatal error reporting
// =========================================================================
// Shows a modal message box and logs the failure phase.  SDL_LogError
// routes to logcat on Android (tag "SDL") and stderr everywhere else, so
// the next test cycle can diagnose startup failures without any native
// debugger.  Called from the top-level try/catch in main().
static void ATReportFatal(const char *phase, const char *message) {
	// Layer 1: logcat / stderr.  Works even if everything else is broken.
	SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
		"Altirra: FATAL at phase=%s: %s", phase ? phase : "?", message ? message : "?");

	// Layer 2: persistent crash file.  Read & shown by the next launch via
	// the ImGui crash viewer.  Best-effort; never throws.
	ATCrashReportWrite(phase, message);

	// Layer 3: SDL message box.  Works on desktop and (usually) on Android
	// as long as SDL is up enough to talk to the system.  If SDL video is
	// the thing that failed, this is a silent no-op, which is why layers
	// 1 and 2 exist.
	char buf[1024];
	SDL_snprintf(buf, sizeof buf,
		"Altirra failed to start.\n\nPhase: %s\n\n%s",
		phase ? phase : "?", message ? message : "?");
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
		"Altirra failed to start", buf, g_pWindow);
}

// Android stderr → logcat bridge lives in main_android_bridge.cpp.
#ifdef __ANDROID__
void ATAndroidInstallStderrBridge();
#endif

ATMobileUIState g_mobileState;

// Forward declaration — defined in window placement section below
void ATUpdateWindowedGeometry(SDL_Window *window);

// =========================================================================
// Pan/Zoom tool state
// =========================================================================

static bool g_panZoomActive = false;
static bool g_panZoomDragging = false;
static bool g_panZoomZooming = false;  // Ctrl+LMB zoom mode
static float g_panZoomLastX = 0, g_panZoomLastY = 0;

bool ATUIIsPanZoomToolActive() { return g_panZoomActive; }

void ATUISetPanZoomToolActive(bool active) {
	g_panZoomActive = active;
	g_panZoomDragging = false;
	g_panZoomZooming = false;
	IATUIRenderer *r = g_sim.GetUIRenderer();
	if (r) {
		if (active)
			r->SetStatusMessage(L"Pan/Zoom: LMB to pan, Ctrl+LMB or wheel to zoom, Esc to exit");
		else
			r->SetStatusMessage(nullptr);
	}
}

// =========================================================================
// Event handling
// =========================================================================

static bool g_prevImGuiCapture = false;
static bool g_prevImGuiMouseCapture = false;

// Display destination rectangle — computed each frame by ComputeDisplayRect().
// Declared here (before UpdateMousePosition) so mouse mapping can use it.
// Non-static so ui_main.cpp can read it via ATGetMainDisplayRect().
SDL_FRect g_displayRect = {0, 0, 0, 0};

// Accessor used by the ImGui UI layer to draw the text selection overlay
// and process mouse events over the main display (when the debugger is
// closed and the Atari frame is drawn directly to the SDL framebuffer).
bool ATGetMainDisplayRect(float& x, float& y, float& w, float& h) {
	if (g_displayRect.w <= 0.0f || g_displayRect.h <= 0.0f)
		return false;
	x = g_displayRect.x;
	y = g_displayRect.y;
	w = g_displayRect.w;
	h = g_displayRect.h;
	return true;
}

// Menu bar height from the previous frame.  ComputeDisplayRect() uses this to
// offset the display area below the ImGui menu bar.  Updated each frame after
// ATUIRenderMainMenu() runs.  In fullscreen the menu is hidden and this is 0.
float g_menuBarHeight = 0.0f;

// Update all mouse position inputs (pad, beam, virtual stick) from pixel coords.
// Matches Windows ATUIVideoDisplayWindow::UpdateMousePosition().
// Mouse coordinates are remapped relative to the display destination rectangle
// so that scaling/aspect ratio correction doesn't break light pen or paddle input.
static void UpdateMousePosition(ATInputManager *im, float mx, float my) {
	// Map mouse position relative to the display rect.
	const float dw = g_displayRect.w;
	const float dh = g_displayRect.h;
	if (dw < 1.0f || dh < 1.0f)
		return;

	const float relX = std::clamp((mx - g_displayRect.x) / dw, 0.0f, 1.0f);
	const float relY = std::clamp((my - g_displayRect.y) / dh, 0.0f, 1.0f);

	// Pad position: map display area to [-0x10000, +0x10000]
	int padX = (int)(relX * 131072.0f - 0x10000);
	int padY = (int)(relY * 131072.0f - 0x10000);
	im->SetMousePadPos(padX, padY);

	// Beam position: map relative position to ANTIC beam coordinates.
	{
		ATGTIAEmulator& gtia = g_sim.GetGTIA();
		const vdrect32 scanArea(gtia.GetFrameScanArea());

		float hcyc = (float)scanArea.left
			+ (relX * (float)scanArea.width()) - 0.5f;
		float vcyc = (float)scanArea.top
			+ (relY * (float)scanArea.height()) - 0.5f;

		im->SetMouseBeamPos(
			(int)((hcyc - 128.0f) * (65536.0f / 94.0f)),
			(int)((vcyc - 128.0f) * (65536.0f / 188.0f)));
	}

	// Virtual stick: normalized [-1, +1] relative to display rect center.
	{
		float normX = relX * 2.0f - 1.0f;
		float normY = relY * 2.0f - 1.0f;

		// Apply aspect ratio correction based on display rect proportions.
		if (dw > dh)
			normX *= dw / dh;
		else if (dh > dw)
			normY *= dh / dw;

		im->SetMouseVirtualStickPos(
			(int)(normX * 131072.0f),
			(int)(normY * 131072.0f));
	}
}

// Persist every piece of mutable state that lives only in memory:
//   * ATSaveSettings — converts simulator + UI state (HardwareMode,
//     VideoStandard, MemoryMode, BASIC enabled, SIO patch, display
//     filter mode, mounted media, etc.) into registry values.  Without
//     this step the final ATRegistryFlushToDisk only persists what was
//     already in the registry, dropping every runtime-only change.
//   * Game library cache — scan results and play history that are
//     normally written by RecordPlay/scan completion but not on a
//     mid-session background.
//   * VDSaveFilespecSystemData — per-dialog "last used directory" map
//     written separately from the main registry, matched to clean exit.
//   * ATRegistryFlushToDisk — finally serialise the in-memory registry
//     to settings.ini so a process kill preserves everything above.
//
// Each step has its own try/catch so a failure in any one does not skip
// the others — the final flush always attempts to run.  Mirrors the
// clean-exit save sequence at the bottom of main().  Called from both
// Android suspend (WILL_ENTER_BACKGROUND / TERMINATING) and desktop
// TERMINATING so a forced OS shutdown doesn't lose the user's mounted
// cart/disk or other runtime state.
static void ATPersistAllForSuspend() {
	try {
		ATSaveSettings((ATSettingsCategory)(
			kATSettingsCategory_All & ~kATSettingsCategory_FullScreen
		));
	} catch (...) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"Altirra: ATSaveSettings failed during suspend");
	}
	try {
		if (ATGameLibrary *lib = GetGameLibrary())
			lib->SaveCache();
	} catch (...) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"Altirra: game library SaveCache failed during suspend");
	}
	try {
		extern void VDSaveFilespecSystemData();
		VDSaveFilespecSystemData();
	} catch (...) {
		// Best-effort; failure here only loses the file-dialog history.
	}
	try {
		extern void ATRegistryFlushToDisk();
		ATRegistryFlushToDisk();
	} catch (...) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"Altirra: registry flush failed during suspend");
	}
}

static void HandleEvents() {
	// Detect when ImGui starts capturing keyboard (e.g. menu opened)
	// and release all held emulator keys to prevent stuck input.
	bool imguiCapture = ATUIWantCaptureKeyboard();
	if (imguiCapture && !g_prevImGuiCapture)
		ATInputSDL3_ReleaseAllKeys();
	g_prevImGuiCapture = imguiCapture;

	// Release mouse capture when ImGui wants the mouse (menu/dialog open)
	bool imguiMouseCapture = ATUIWantCaptureMouse();
	if (imguiMouseCapture && !g_prevImGuiMouseCapture)
		ATUIReleaseMouse();
	g_prevImGuiMouseCapture = imguiMouseCapture;

	SDL_Event ev;
	while (SDL_PollEvent(&ev)) {
		// Emote picker open-shortcut: R3 (right stick click) while a
		// netplay lockstep session is live.  Runs before any other
		// gamepad dispatch so the UI opens even in Gaming Mode.
		if (ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN
			&& ev.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_STICK
			&& ATNetplayGlue::IsLockstepping())
		{
			ATEmotePicker::Open();
			continue;
		}
		// Route touch/gamepad events to Gaming Mode UI before ImGui
		if (ATUIIsGamingMode()) {
			if (ATMobileGamepad_HandleEvent(ev, g_sim, g_mobileState))
				continue;
			if (ATMobileUI_HandleEvent(ev, g_mobileState))
				continue;

			// ESC on the emulation screen opens the hamburger (pause menu).
			// If somehow on None with no game, return to the library.
			if (ev.type == SDL_EVENT_KEY_DOWN
				&& ev.key.key == SDLK_ESCAPE
				&& g_mobileState.currentScreen == ATMobileUIScreen::None)
			{
				if (g_mobileState.gameLoaded) {
					ATMobileUI_OpenMenu(g_sim, g_mobileState);
				} else {
					g_mobileState.currentScreen = ATMobileUIScreen::GameBrowser;
				}
				continue;
			}
		}

		// Virtual keyboard intercepts gamepad events when visible.
		// Gamepad X toggles visibility; D-pad/A/LB/RB navigate and
		// press keys when the keyboard is showing.
		// Skip when a mobile UI screen (hamburger, file browser, etc.)
		// is open — ImGui needs gamepad events for dialog navigation.
		{
			bool mobileUIActive = ATUIIsGamingMode()
				&& (g_mobileState.currentScreen != ATMobileUIScreen::None);
			if (!mobileUIActive) {
				if (ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN
					&& ev.gbutton.button == SDL_GAMEPAD_BUTTON_WEST) {
					g_uiState.showVirtualKeyboard = !g_uiState.showVirtualKeyboard;
					if (!g_uiState.showVirtualKeyboard)
						ATUIVirtualKeyboard_ReleaseAll(g_sim);
					else
						ATTouchControls_ReleaseAll();  // hide touch controls cleanly
					continue;
				}
				if (g_uiState.showVirtualKeyboard) {
					if (ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN
						&& ev.gbutton.button == SDL_GAMEPAD_BUTTON_EAST) {
						// B button closes the keyboard
						g_uiState.showVirtualKeyboard = false;
						ATUIVirtualKeyboard_ReleaseAll(g_sim);
						continue;
					}
					if (ATUIVirtualKeyboard_HandleEvent(ev, g_sim, true))
						continue;
				}
			}
		}

		ATUIProcessEvent(&ev);

		switch (ev.type) {
		case SDL_EVENT_QUIT:
		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			if (g_uiState.exitConfirmed)
				g_running = false;
			else if (!g_uiState.showExitConfirm)
				g_uiState.showExitConfirm = true;  // Render loop will open the popup
			break;

		case SDL_EVENT_KEY_DOWN:
			// Cmd+Return toggles fullscreen (macOS green-button equivalent)
			if (ev.key.scancode == SDL_SCANCODE_RETURN && (ev.key.mod & SDL_KMOD_GUI)) {
				bool isFS = (SDL_GetWindowFlags(g_pWindow) & SDL_WINDOW_FULLSCREEN) != 0;
				ATSetFullscreen(!isFS);
				break;
			}

			// Right-Alt releases mouse capture (matches Windows behavior)
			if (ATUIIsMouseCaptured() && ev.key.scancode == SDL_SCANCODE_RALT) {
				ATUIReleaseMouse();
				break;
			}

			// ESC exits Pan/Zoom tool
			if (g_panZoomActive && ev.key.key == SDLK_ESCAPE && !ATUIWantCaptureKeyboard()) {
				ATUISetPanZoomToolActive(false);
				break;
			}

			// Shortcut capture mode (rebinding UI)
			if (g_shortcutCaptureActive) {
				ATUIHandleShortcutCapture(ev.key);
				break;
			}

			{
				// When a gaming mode screen is open, ImGui owns all
				// keyboard input for navigation (arrows, enter, etc.).
				// Skip accelerator dispatch and game input so keys
				// don't double-fire.
				bool gamingScreenOpen = ATUIIsGamingMode()
					&& g_mobileState.currentScreen != ATMobileUIScreen::None;

				// Accelerator table dispatch (matches Windows ATUIActivateVirtKeyMapping)
				// Priority: Global → Debugger → Display
				bool handled = false;
				if (!gamingScreenOpen) {
					handled = ATUISDLActivateAccelKey(ev.key, false, kATUIAccelContext_Global);

					if (!handled && ATUIDebuggerIsOpen())
						handled = ATUISDLActivateAccelKey(ev.key, false, kATUIAccelContext_Debugger);

					if (!handled && !ATUIWantCaptureKeyboard())
						handled = ATUISDLActivateAccelKey(ev.key, false, kATUIAccelContext_Display);
				}

				if (!handled && !ATUIWantCaptureKeyboard())
					ATInputSDL3_HandleKeyDown(ev.key);
			}
			break;

		case SDL_EVENT_KEY_UP: {
			// Dispatch key-up through accel tables (handles PulseWarpOff on F1 release).
			// Check all contexts symmetrically with key-down dispatch.
			// Skip when a gaming mode screen owns the keyboard (matches KEY_DOWN guard).
			bool gamingScreenOpenUp = ATUIIsGamingMode()
				&& g_mobileState.currentScreen != ATMobileUIScreen::None;
			if (!gamingScreenOpenUp) {
				ATUISDLActivateAccelKey(ev.key, true, kATUIAccelContext_Global);
				if (ATUIDebuggerIsOpen())
					ATUISDLActivateAccelKey(ev.key, true, kATUIAccelContext_Debugger);
				if (!ATUIWantCaptureKeyboard())
					ATUISDLActivateAccelKey(ev.key, true, kATUIAccelContext_Display);
			}

			if (!ATUIWantCaptureKeyboard()) {
				// Suppress emulator key-up for keys bound in accel tables
				// (replaces hardcoded F1/F5/F7/.../F12 list)
				uint32 upVk = SDLScancodeToVK(ev.key.scancode);
				if (upVk == kATInputCode_None || !ATUIFindBoundKey(upVk, ev.key.mod, ev.key.scancode))
					ATInputSDL3_HandleKeyUp(ev.key);
			}
			break;
		}

		case SDL_EVENT_TEXT_INPUT:
			if (!ATUIWantCaptureKeyboard())
				ATInputSDL3_HandleTextInput(ev.text.text);
			break;

		case SDL_EVENT_MOUSE_MOTION:
			// Pan/Zoom tool: handle drag
			if (g_panZoomActive && !ATUIWantCaptureMouse() && (g_panZoomDragging || g_panZoomZooming)) {
				float dx = ev.motion.x - g_panZoomLastX;
				float dy = ev.motion.y - g_panZoomLastY;
				g_panZoomLastX = ev.motion.x;
				g_panZoomLastY = ev.motion.y;

				if (g_panZoomZooming) {
					// Ctrl+LMB drag: vertical motion zooms
					float oldZoom = ATUIGetDisplayZoom();
					float newZoom = oldZoom * powf(2.0f, -dy / 100.0f);
					newZoom = std::clamp(newZoom, 0.01f, 100.0f);
					ATUISetDisplayZoom(newZoom);
				} else {
					// LMB drag: pan (use display rect size for consistent feel,
					// matching Windows ATUIDisplayToolPanAndZoom which uses outputRect)
					float dw = g_displayRect.w;
					float dh = g_displayRect.h;
					if (dw > 0 && dh > 0) {
						vdfloat2 pan = ATUIGetDisplayPanOffset();
						pan.x -= dx / dw;
						pan.y -= dy / dh;
						pan.x = std::clamp(pan.x, -10.0f, 10.0f);
						pan.y = std::clamp(pan.y, -10.0f, 10.0f);
						ATUISetDisplayPanOffset(pan);
					}
				}
				break;
			}
			if (!ATUIWantCaptureMouse()) {
				ATInputManager *im = g_sim.GetInputManager();
				// Forward motion when captured, or in absolute mode without
				// auto-capture (matches Windows OnMouseMove line 1400).
				if (im && (ATUIIsMouseCaptured() ||
					(!ATUIGetMouseAutoCapture() && im->IsMouseAbsoluteMode())))
				{
					if (im->IsMouseAbsoluteMode()) {
						UpdateMousePosition(im, ev.motion.x, ev.motion.y);
					} else {
						// Relative mode: forward deltas for paddle/trackball
						im->OnMouseMove(0, (int)ev.motion.xrel, (int)ev.motion.yrel);
					}
				}
			}
			break;

		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			// Pan/Zoom tool: start drag
			if (g_panZoomActive && !ATUIWantCaptureMouse() &&
				ev.button.button == SDL_BUTTON_LEFT) {
				g_panZoomLastX = ev.button.x;
				g_panZoomLastY = ev.button.y;
				SDL_Keymod mod = SDL_GetModState();
				if (mod & SDL_KMOD_CTRL) {
					g_panZoomZooming = true;
					g_panZoomDragging = false;
				} else {
					g_panZoomDragging = true;
					g_panZoomZooming = false;
				}
				break;
			}
			if (!ATUIWantCaptureMouse()) {
				// Middle-click releases mouse capture when MMB isn't mapped
				// as an input (matches Windows behavior).
				if (ev.button.button == SDL_BUTTON_MIDDLE &&
					ATUIIsMouseCaptured()) {
					ATInputManager *im = g_sim.GetInputManager();
					if (!im || !im->IsInputMapped(0, kATInputCode_MouseMMB))
						ATUIReleaseMouse();
					break;
				}

				// Auto-capture: on left click, capture the mouse if auto-capture
				// is enabled and mouse is mapped.  The click is consumed (not
				// forwarded to input manager) — matches Windows behavior.
				// When the mouse is NOT mapped as an Atari input, fall through
				// so the click reaches the text-selection handler in the UI
				// render path.
				{
					ATInputManager *imCap = g_sim.GetInputManager();
					if (ev.button.button == SDL_BUTTON_LEFT &&
						ATUIGetMouseAutoCapture() &&
						!ATUIIsMouseCaptured() &&
						imCap && imCap->IsMouseMapped()) {
						ATUICaptureMouse();
						break;
					}
				}

				// Forward button to input manager when captured or absolute mode
				ATInputManager *im = g_sim.GetInputManager();
				if (im && (ATUIIsMouseCaptured() || im->IsMouseAbsoluteMode())) {
					// In absolute mode, update position before the button press
					// (matches Windows OnMouseDown which calls UpdateMousePosition)
					if (im->IsMouseAbsoluteMode())
						UpdateMousePosition(im, ev.button.x, ev.button.y);

					uint32 code = 0;
					switch (ev.button.button) {
						case SDL_BUTTON_LEFT:   code = kATInputCode_MouseLMB; break;
						case SDL_BUTTON_MIDDLE: code = kATInputCode_MouseMMB; break;
						case SDL_BUTTON_RIGHT:  code = kATInputCode_MouseRMB; break;
						case SDL_BUTTON_X1:     code = kATInputCode_MouseX1B; break;
						case SDL_BUTTON_X2:     code = kATInputCode_MouseX2B; break;
					}
					if (code)
						im->OnButtonDown(0, code);
				}
			}
			break;

		case SDL_EVENT_MOUSE_BUTTON_UP:
			// Pan/Zoom tool: end drag
			if (g_panZoomActive && ev.button.button == SDL_BUTTON_LEFT) {
				g_panZoomDragging = false;
				g_panZoomZooming = false;
				break;
			}
			if (!ATUIWantCaptureMouse()) {
				ATInputManager *im = g_sim.GetInputManager();
				if (im && (ATUIIsMouseCaptured() || im->IsMouseAbsoluteMode())) {
					uint32 code = 0;
					switch (ev.button.button) {
						case SDL_BUTTON_LEFT:   code = kATInputCode_MouseLMB; break;
						case SDL_BUTTON_MIDDLE: code = kATInputCode_MouseMMB; break;
						case SDL_BUTTON_RIGHT:  code = kATInputCode_MouseRMB; break;
						case SDL_BUTTON_X1:     code = kATInputCode_MouseX1B; break;
						case SDL_BUTTON_X2:     code = kATInputCode_MouseX2B; break;
					}
					if (code)
						im->OnButtonUp(0, code);
				}
			}
			break;

		case SDL_EVENT_MOUSE_WHEEL:
			// Pan/Zoom tool: wheel to zoom (pinned to cursor)
			if (g_panZoomActive && !ATUIWantCaptureMouse() && ev.wheel.y != 0) {
				float oldZoom = ATUIGetDisplayZoom();
				float newZoom = oldZoom * powf(2.0f, ev.wheel.y / 4.0f);
				newZoom = std::clamp(newZoom, 0.01f, 100.0f);

				// Pinned zoom: keep the point under cursor stationary.
				// The display rect uses: left = w*(relOrigin.x - 1) + vW/2
				// where relOrigin = {0.5,0.5} - pan, w = baseW*zoom.
				// For cursor at (sx,sy), its fractional position in the display:
				//   frac = (sx - left) / w = (sx - vW/2)/w - relOrigin.x + 1
				//        = (sx - vW/2)/w + 0.5 + pan.x
				// After zoom change we need: same frac at same sx, so:
				//   newPan.x = pan.x + (sx - vW/2)*(1/oldW - 1/newW)
				// Since oldW = g_displayRect.w and newW = oldW * (newZoom/oldZoom):
				float mx, my;
				SDL_GetMouseState(&mx, &my);
				int winW, winH;
				SDL_GetWindowSize(g_pWindow, &winW, &winH);
				float dw = g_displayRect.w;
				float dh = g_displayRect.h;
				if (dw > 0 && dh > 0 && winW > 0 && winH > 0) {
					float zoomRatio = newZoom / oldZoom;
					float cxOff = mx - winW * 0.5f;  // cursor offset from viewport center
					float cyOff = my - winH * 0.5f;

					vdfloat2 pan = ATUIGetDisplayPanOffset();
					pan.x += (cxOff / dw) * (1.0f - 1.0f / zoomRatio);
					pan.y += (cyOff / dh) * (1.0f - 1.0f / zoomRatio);
					pan.x = std::clamp(pan.x, -10.0f, 10.0f);
					pan.y = std::clamp(pan.y, -10.0f, 10.0f);
					ATUISetDisplayPanOffset(pan);
				}

				ATUISetDisplayZoom(newZoom);
				break;
			}
			// Mouse wheel is always forwarded to input manager (matches
			// Windows which doesn't require capture for wheel events).
			if (!ATUIWantCaptureMouse()) {
				ATInputManager *im = g_sim.GetInputManager();
				if (im) {
					if (ev.wheel.y != 0)
						im->OnMouseWheel(0, ev.wheel.y);
					if (ev.wheel.x != 0)
						im->OnMouseHWheel(0, ev.wheel.x);
				}
			}
			break;

		case SDL_EVENT_WINDOW_MOUSE_LEAVE:
			// Release pan/zoom drag on mouse leave
			g_panZoomDragging = false;
			g_panZoomZooming = false;
			// Reset virtual stick to center when mouse leaves window
			// (matches Windows OnMouseLeave)
			{
				ATInputManager *im = g_sim.GetInputManager();
				if (im)
					im->SetMouseVirtualStickPos(0, 0);
			}
			break;

		case SDL_EVENT_GAMEPAD_ADDED:
		case SDL_EVENT_JOYSTICK_ADDED:
			// Devices without an SDL gamepad mapping (rare arcade
			// sticks, generic HID pads) only fire JOYSTICK_ADDED.
			// RescanForDevices() filters via SDL_IsGamepad internally.
			if (g_pJoystickMgr)
				g_pJoystickMgr->RescanForDevices();
			break;

		case SDL_EVENT_GAMEPAD_REMOVED:
		case SDL_EVENT_JOYSTICK_REMOVED:
			// RescanForDevices only adds new devices.  For removal,
			// the SDL3-specific manager exposes CloseGamepad(), which
			// despite its historical name handles both gamepads and
			// raw-HID joysticks. Devices without an SDL gamepad mapping
			// only fire JOYSTICK_REMOVED; mapped gamepads fire both.
			// Use the correct union member for each event type to keep
			// strict-aliasing rules happy.
			if (g_pJoystickMgr) {
				const SDL_JoystickID which =
					(ev.type == SDL_EVENT_GAMEPAD_REMOVED)
						? ev.gdevice.which
						: ev.jdevice.which;
				static_cast<ATJoystickManagerSDL3 *>(g_pJoystickMgr)->CloseGamepad(which);
			}
			break;

		case SDL_EVENT_DROP_BEGIN:
			g_dragDropState.active = true;
			g_dragDropState.x = ev.drop.x;
			g_dragDropState.y = ev.drop.y;
			break;
		case SDL_EVENT_DROP_POSITION:
			g_dragDropState.x = ev.drop.x;
			g_dragDropState.y = ev.drop.y;
			break;
		case SDL_EVENT_DROP_COMPLETE:
			g_dragDropState.active = false;
			break;
		case SDL_EVENT_DROP_FILE: {
			g_dragDropState.active = false;
			const char *file = ev.drop.data;
			if (file) {
				float dx = ev.drop.x, dy = ev.drop.y;
				// Priority chain for file drops (matches Windows behavior):
				// 1. Disk Explorer open + writable + cursor over it → import into disk image
				// 2. Firmware Manager open + cursor over it → add firmware ROM
				// 3. Otherwise → boot image (like dragging .xex/.atr/.car onto main window)
				if (!ATUIDiskExplorerHandleDrop(file, dx, dy)
					&& !ATUIFirmwareManagerHandleDrop(file, dx, dy)) {
					ATUIPushDeferred(kATDeferred_BootImage, file);
				}
			}
			break;
		}

		case SDL_EVENT_WINDOW_RESIZED:
			ATUpdateWindowedGeometry(g_pWindow);
			if (g_pBackend) {
				int w, h;
				SDL_GetWindowSizeInPixels(g_pWindow, &w, &h);
				g_pBackend->OnResize(w, h);
			}
#ifdef __ANDROID__
			// Orientation change / rotation → safe insets change too.
			ATAndroid_InvalidateSafeInsets();
#endif
			break;
		case SDL_EVENT_WINDOW_MOVED:
			ATUpdateWindowedGeometry(g_pWindow);
			break;

		case SDL_EVENT_WINDOW_FOCUS_GAINED:
			g_winActive = true;
			// Match Windows uivideodisplaywindow.cpp:1693 (OnSetFocus) —
			// re-enable input mapping when the window regains focus.
			if (ATInputManager *im = g_sim.GetInputManager())
				im->SetRestrictedMode(false);
			break;

		// -------- Android / mobile lifecycle --------
		// On Android SDL3 sends these when the activity is paused/resumed
		// or the OS is about to kill the process.  While suspended, the
		// EGL surface may be destroyed — we MUST NOT call into the display
		// backend (glViewport / SDL_GL_SwapWindow) until we are resumed,
		// or the app will SIGSEGV.  These also fire on desktop for
		// minimize/restore, and the same guard is correct there.
		case SDL_EVENT_WILL_ENTER_BACKGROUND:
			SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
				"Altirra: WILL_ENTER_BACKGROUND — releasing input");
			ATInputSDL3_ReleaseAllKeys();
			ATUIReleaseMouse();
			ATTouchControls_ReleaseAll();
			ATUIVirtualKeyboard_ReleaseAll(g_sim);
#ifdef __ANDROID__
			ATMobileUI_SaveSuspendState(g_sim, g_mobileState);
			// Persist Machine/Display/Acceleration/Boot/View state plus
			// the game library cache — they are not held in the registry
			// until ATSaveSettings runs, and the OS will likely kill the
			// process without ever returning to the clean-exit path.
			ATPersistAllForSuspend();
#endif
			break;

		case SDL_EVENT_DID_ENTER_BACKGROUND:
			SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
				"Altirra: DID_ENTER_BACKGROUND — suspending render/sim");
			g_appSuspended = true;
#ifdef __ANDROID__
			if (IATAudioOutput *ao = g_sim.GetAudioOutput())
				ao->SetMute(true);
#endif
			break;

		case SDL_EVENT_WILL_ENTER_FOREGROUND:
			SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
				"Altirra: WILL_ENTER_FOREGROUND");
			break;

		case SDL_EVENT_DID_ENTER_FOREGROUND:
			SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
				"Altirra: DID_ENTER_FOREGROUND — resuming");
			g_appSuspended = false;
#ifdef __ANDROID__
			if (IATAudioOutput *ao = g_sim.GetAudioOutput())
				ao->SetMute(g_mobileState.audioMuted);
			ATAndroid_InvalidateSafeInsets();
			ATAndroid_InvalidateStorageVolumes();
			ATMobileUI_InvalidateFileBrowser();
#endif
			break;

		case SDL_EVENT_TERMINATING:
			// Last chance before the OS kills the process.  Snapshot
			// the emulator state (mobile only) and persist every piece
			// of mutable settings + cache state to disk.  Without the
			// full ATSaveSettings pass, runtime-only state (mounted
			// cart/disk, machine settings changed this session) would
			// be dropped because the registry snapshot was never
			// refreshed from the live simulator.
			SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
				"Altirra: TERMINATING — flushing settings");
#ifdef __ANDROID__
			ATMobileUI_SaveSuspendState(g_sim, g_mobileState);
#endif
			ATPersistAllForSuspend();
			g_running = false;
			break;

		case SDL_EVENT_LOW_MEMORY:
			// No caches to drop yet, but keep the handler so the hook exists
			// when we add the library/scraper/thumbnail cache later.
			SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
				"Altirra: LOW_MEMORY");
			break;

		// NOTE: intentionally not hooking SDL_EVENT_WINDOW_MINIMIZED /
		// HIDDEN / RESTORED / SHOWN into g_appSuspended.  On desktop,
		// minimize is already governed by the user's "Pause when
		// inactive" setting via FOCUS_LOST, and some users rely on
		// background-run behavior.  On Android, the authoritative EGL-
		// surface-loss signal is DID_ENTER_BACKGROUND (handled above),
		// not WINDOW_HIDDEN, so those handlers would only muddle things.

		case SDL_EVENT_WINDOW_FOCUS_LOST:
			g_winActive = false;
			// Match Windows uivideodisplaywindow.cpp:1699 (OnKillFocus) —
			// restrict input mapping BEFORE releasing keys, so that any
			// release-time bookkeeping sees the same restricted state
			// the Windows path uses.
			if (ATInputManager *im = g_sim.GetInputManager())
				im->SetRestrictedMode(true);
			// Release pan/zoom drag state to prevent stuck drag
			g_panZoomDragging = false;
			g_panZoomZooming = false;
			// Release all held keys/buttons to prevent stuck input
			ATInputSDL3_ReleaseAllKeys();
			// Release held mouse buttons in input manager
			{
				ATInputManager *im = g_sim.GetInputManager();
				if (im) {
					im->OnButtonUp(0, kATInputCode_MouseLMB);
					im->OnButtonUp(0, kATInputCode_MouseMMB);
					im->OnButtonUp(0, kATInputCode_MouseRMB);
					im->OnButtonUp(0, kATInputCode_MouseX1B);
					im->OnButtonUp(0, kATInputCode_MouseX2B);
				}
			}
			// Release mouse capture on focus loss
			ATUIReleaseMouse();
			// Release all touch controls on focus loss
			ATTouchControls_ReleaseAll();
			ATUIVirtualKeyboard_ReleaseAll(g_sim);
			break;

		default:
			break;
		}
	}
}

// =========================================================================
// Display destination rectangle
// =========================================================================
// Ported from ATDisplayPane::ResizeDisplay() in uidisplay.cpp (lines 1992-2103).
// Computes where the emulator frame should be rendered within the SDL window,
// accounting for stretch mode, pixel aspect ratio, integer scaling, zoom, and pan.

static SDL_FRect ComputeDisplayRect() {
	int winW, winH;
	SDL_GetWindowSize(g_pWindow, &winW, &winH);

	// Reserve space for the menu bar at the top.  g_menuBarHeight is updated
	// each frame after the menu bar is rendered (we use the previous frame's
	// value which is stable).  In fullscreen the menu is hidden and this is 0.
	float menuH = g_menuBarHeight;

	float viewportW = (float)winW;
	float viewportH = (float)winH - menuH;

	// Reserve space for the virtual keyboard panel if visible
	{
		float kbdBottom = 0, kbdRight = 0;
		ATUIVirtualKeyboard_GetDisplayInset(
			g_uiState.showVirtualKeyboard, g_uiState.oskPlacement,
			&kbdBottom, &kbdRight);
		viewportW -= kbdRight;
		viewportH -= kbdBottom;
	}

	float w = viewportW;
	float h = viewportH;

	if (w < 1.0f) w = 1.0f;
	if (h < 1.0f) h = 1.0f;

	const auto& gtia = g_sim.GetGTIA();
	const ATDisplayStretchMode stretchMode = ATUIGetDisplayStretchMode();

	if (stretchMode == kATDisplayStretchMode_PreserveAspectRatio
		|| stretchMode == kATDisplayStretchMode_IntegralPreserveAspectRatio)
	{
		int sw = 1, sh = 1;
		bool rgb32 = false;
		gtia.GetRawFrameFormat(sw, sh, rgb32);

		const float fsw = (float)((double)sw * gtia.GetPixelAspectRatio());
		const float fsh = (float)sh;
		float zoom = std::min(w / fsw, h / fsh);

		if (stretchMode == kATDisplayStretchMode_IntegralPreserveAspectRatio && zoom > 1.0f) {
			// Small leeway for rounding errors (matches Windows).
			zoom = std::floor(zoom * 1.0001f);
		}

		w = fsw * zoom;
		h = fsh * zoom;
	} else if (stretchMode == kATDisplayStretchMode_SquarePixels
		|| stretchMode == kATDisplayStretchMode_Integral)
	{
		int sw = 1, sh = 1;
		gtia.GetFrameSize(sw, sh);

		const float fsw = (float)sw;
		const float fsh = (float)sh;

		float ratio = std::floor(std::min(w / fsw, h / fsh));

		if (ratio < 1.0f || stretchMode == kATDisplayStretchMode_SquarePixels) {
			// Continuous scaling maintaining source aspect ratio.
			if (w * fsh < h * fsw) {
				// Width is the constraining axis.
				h = (fsh * w) / fsw;
			} else {
				// Height is the constraining axis.
				w = (fsw * h) / fsh;
			}
		} else {
			w = fsw * ratio;
			h = fsh * ratio;
		}
	}
	// kATDisplayStretchMode_Unconstrained: w, h stay as viewport size.

	// Apply zoom / pan (matches uidisplay.cpp lines 2063-2075).
	const float displayZoom = ATUIGetDisplayZoom();
	w *= displayZoom;
	h *= displayZoom;

	const vdfloat2 pan = ATUIGetDisplayPanOffset();
	const vdfloat2 relOrigin = vdfloat2{0.5f, 0.5f} - pan;

	float left   = w * (relOrigin.x - 1.0f) + viewportW * 0.5f;
	float top    = h * (relOrigin.y - 1.0f) + viewportH * 0.5f + menuH;
	float right  = w * relOrigin.x           + viewportW * 0.5f;
	float bottom = h * relOrigin.y           + viewportH * 0.5f + menuH;

	// Pixel-snap: distribute rounding error evenly across opposite edges
	// to minimize sub-pixel shimmer (matches uidisplay.cpp lines 2077-2085).
	float errL = left   - std::round(left);
	float errR = right  - std::round(right);
	float errT = top    - std::round(top);
	float errB = bottom - std::round(bottom);

	left   -= 0.5f * (errL + errR);
	right  -= 0.5f * (errL + errR);
	top    -= 0.5f * (errT + errB);
	bottom -= 0.5f * (errT + errB);

	SDL_FRect rect;
	rect.x = left;
	rect.y = top;
	rect.w = right - left;
	rect.h = bottom - top;

	return rect;
}

// =========================================================================
// Rendering
// =========================================================================

static ATDisplayFilterMode s_lastAppliedFilter = (ATDisplayFilterMode)0xFF;
static int s_lastAppliedSharpness = 0x7FFFFFFF;

// Sync screen FX from GTIA → GL backend.  Called every frame.
static void SyncScreenFXToBackend() {
	if (!g_pBackend || !g_pBackend->SupportsScreenFX())
		return;

	// Push filter mode and sharpness changes
	ATDisplayFilterMode curFilter = ATUIGetDisplayFilterMode();
	if (curFilter != s_lastAppliedFilter) {
		g_pBackend->SetFilterMode(curFilter);
		s_lastAppliedFilter = curFilter;
	}

	int curSharpness = ATUIGetViewFilterSharpness();
	if (curSharpness != s_lastAppliedSharpness) {
		g_pBackend->SetFilterSharpness((float)curSharpness);
		s_lastAppliedSharpness = curSharpness;
	}

	// Read screen FX info produced by GTIA (set via SetSourcePersistent
	// or PostBuffer).  When GTIA has accel post-processing active, the
	// frame carries a non-null mpScreenFX with the current effect state.
	// When all accel effects are disabled, mpScreenFX is null and
	// HasScreenFX() returns false — we must still push a default (all-off)
	// state to the backend so it stops rendering stale effects.
	//
	// When the user picks View > Screen Effects > (None), bypass the GTIA
	// FX entirely and push an identity state regardless of what the core
	// produced this frame — the ATArtifactingParams values are retained
	// in GTIA so the user's bloom/distortion/etc. settings survive the
	// None → Basic round trip.
	const bool effectsDisabled =
		(g_uiState.screenEffectsMode == ATUIState::kSFXMode_None);

	if (!effectsDisabled && g_pDisplay->HasScreenFX()) {
		g_pBackend->UpdateScreenFX(g_pDisplay->GetLastScreenFX());
	} else {
		// Push an all-off state so the backend stops rendering stale effects.
		// Note: mGamma must be 1.0 (identity), not 0 (the struct default) —
		// a gamma of 0 triggers the screen FX shader path and produces black.
		VDVideoDisplayScreenFXInfo offFX {};
		offFX.mGamma = 1.0f;
		g_pBackend->UpdateScreenFX(offFX);
	}
}

static int s_diagFrameCount = 0;

static void RenderAndPresent() {
	// Lifecycle guard: on Android the EGL surface is destroyed while the
	// app is backgrounded, and any GL/SDL_Renderer call against it will
	// crash.  On desktop, a minimized window hits the same path harmlessly.
	// Must also be defensive if the window was never created (very early
	// error paths).
	if (g_appSuspended || !g_pWindow || !g_pBackend)
		return;

	g_pBackend->BeginFrame();

	// Upload frame pixels to the backend
	const void *pixels = g_pDisplay->GetFramePixels();
	if (pixels) {
		int pw = g_pDisplay->GetFramePixelWidth();
		int ph = g_pDisplay->GetFramePixelHeight();
		int pp = g_pDisplay->GetFramePixelPitch();
		g_pBackend->UploadFrame(pixels, pw, ph, pp);
	}

	// Update filter mode on texture if setting changed.
	ATDisplayFilterMode curFilter = ATUIGetDisplayFilterMode();
	if (curFilter != s_lastAppliedFilter) {
		if (g_pBackend->GetType() == DisplayBackendType::SDLRenderer)
			g_pDisplay->UpdateScaleMode();
		g_pBackend->SetFilterMode(curFilter);
		s_lastAppliedFilter = curFilter;
	}

	// Sync screen effects from GTIA to GL backend
	SyncScreenFXToBackend();

	// Draw emulator frame with proper scaling and aspect ratio.
	// When the debugger is open, the Display pane renders it inside an
	// ImGui dockable window instead.
	bool dbgOpen = ATUIDebuggerIsOpen();
	bool hasTex = g_pBackend->HasTexture();
	if (s_diagFrameCount < 5)
		LOG_INFO("Main", "RenderAndPresent: debuggerOpen=%d hasTex=%d", dbgOpen, hasTex);
	if (!dbgOpen) {
		if (hasTex) {
			g_displayRect = ComputeDisplayRect();
			if (s_diagFrameCount < 5)
				LOG_INFO("Main", "RenderFrame: rect=(%.1f,%.1f,%.1f,%.1f) tex=(%d,%d)", g_displayRect.x, g_displayRect.y, g_displayRect.w, g_displayRect.h,
					g_pBackend->GetTextureWidth(), g_pBackend->GetTextureHeight());
			g_pBackend->RenderFrame(
				g_displayRect.x, g_displayRect.y,
				g_displayRect.w, g_displayRect.h,
				g_pBackend->GetTextureWidth(),
				g_pBackend->GetTextureHeight());
		}
	}
	++s_diagFrameCount;

	// Draw ImGui UI on top
	ATUIRenderFrame(g_sim, *g_pDisplay, g_pBackend, g_uiState);

	g_pBackend->Present();
}

#ifdef ALTIRRA_BRIDGE_ENABLED
// Bridge → main loop coupling. The bridge's QUIT command needs to ask
// the SDL3 main loop to exit cleanly. We define the function here, in
// the file that owns g_running, so the bridge module stays decoupled
// from main_sdl3 internals.
void ATBridgeRequestAppQuit() {
	g_running = false;
}
#endif

// =========================================================================
// Main
// =========================================================================

int main(int argc, char *argv[]) {
#ifdef __ANDROID__
	// Must be installed before ANY fprintf(stderr, ...) / LOG_* macro call
	// or SDL logging call, so every startup diagnostic reaches logcat.
	ATAndroidInstallStderrBridge();
#endif

	// Phase tracker for the top-level try/catch below: any uncaught
	// exception during init is reported with the phase name that was
	// current when it threw, so logcat / message box show exactly
	// where we died.  Without this, Android shows a bare "app has
	// stopped" dialog with no diagnostic information.
	const char *phase = "startup";

	try {

	SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Altirra: starting...");

	// Check for --test-mode flag (must be before SDL_Init so it's stripped from argv)
	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--test-mode") == 0) {
			g_testModeEnabled = true;
			// Remove from argv so it's not treated as a boot image path
			for (int j = i; j < argc - 1; ++j)
				argv[j] = argv[j + 1];
			--argc;
			--i;
		}
	}

	// Check for --headless flag (must be before SDL_Init).
	//
	// --headless tells SDL3 to use the offscreen video driver and
	// the dummy audio driver, so AltirraSDL runs without opening a
	// window or playing audio. Use this for automated testing, CI,
	// RL training pipelines, or any context where you want the
	// emulator running but no UI on screen. Same binary, same code
	// paths, same dependencies — only the SDL3 video/audio backends
	// differ.
	//
	// We use SDL_SetHint() with SDL3's canonical hint names
	// (SDL_HINT_VIDEO_DRIVER / SDL_HINT_AUDIO_DRIVER) — calling
	// libc setenv() after main() has started is too late, because
	// SDL3 imports its hint store from the environment at startup
	// and won't see late env-var modifications. SDL_SetHint pokes
	// directly into SDL3's internal hint store, which is read by
	// SDL_Init.
	//
	// We respect any pre-existing SDL3 hint set by the user (so
	// --headless plus SDL_VIDEO_DRIVER=foo in the shell still uses
	// foo, not offscreen).
	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--headless") == 0) {
			if (SDL_GetHint(SDL_HINT_VIDEO_DRIVER) == nullptr)
				SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
			if (SDL_GetHint(SDL_HINT_AUDIO_DRIVER) == nullptr)
				SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
			SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
				"Altirra: --headless: SDL_HINT_VIDEO_DRIVER=offscreen SDL_HINT_AUDIO_DRIVER=dummy");
			for (int j = i; j < argc - 1; ++j)
				argv[j] = argv[j + 1];
			--argc;
			--i;
		}
	}

#ifdef ALTIRRA_BRIDGE_ENABLED
	// Check for --bridge / --bridge=<spec> (must be stripped before SDL_Init
	// for the same reason as --test-mode: otherwise the argv parser later
	// in startup would mistake it for a boot image path).
	//
	// Spec forms:
	//   --bridge                       tcp:127.0.0.1:0 (OS picks port)
	//   --bridge=tcp:127.0.0.1:6502    explicit TCP port
	//   --bridge=unix:/path/to/sock    POSIX UDS (POSIX only)
	//   --bridge=unix-abstract:NAME    Linux abstract UDS (Linux/Android only)
	bool bridgeRequested = false;
	std::string bridgeAddrSpec;
	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--bridge") == 0) {
			bridgeRequested = true;
		} else if (strncmp(argv[i], "--bridge=", 9) == 0) {
			bridgeRequested = true;
			bridgeAddrSpec = argv[i] + 9;
		} else {
			continue;
		}
		for (int j = i; j < argc - 1; ++j)
			argv[j] = argv[j + 1];
		--argc;
		--i;
	}
#endif

#if defined(__APPLE__) && !defined(__IPHONEOS__) && !defined(__ANDROID__)
	// macOS dev convenience: when the .app/Contents/MacOS/AltirraSDL
	// binary is launched directly from a terminal, GameController.framework
	// (gamecontrollerd) attributes permissions to the *responsible* process
	// — i.e. the parent shell / Terminal.app — not to AltirraSDL.  The
	// terminal's bundle has no NSGameControllerUsageDescription, so
	// gamecontrollerd silently refuses to enumerate USB / Bluetooth / MFi /
	// Xbox / DualShock / DualSense controllers.  SDL3's MFi driver
	// (SDL_mfijoystick.m) sees zero gamepads, no SDL_EVENT_GAMEPAD_ADDED
	// is delivered, and joystick port mapping has nothing to bind.
	//
	// Launching the .app via `open -a` / Finder / drag-and-drop fixes this
	// because LaunchServices makes AltirraSDL itself the responsible
	// process — but that is awkward for development iteration.
	//
	// Setting ALTIRRA_MACOS_FORCE_IOKIT=1 disables the MFi driver entirely
	// and falls back to SDL3's IOKit HID driver, which does not go through
	// gamecontrollerd and works regardless of the responsible-process
	// chain.  The trade-off is that controllers which advertise themselves
	// only via GameController.framework (rare on macOS, typically MFi-only
	// peripherals) become invisible — for the joystick-port use case
	// (digital direction + button) IOKit is sufficient on every common
	// pad.  See issue #62.
	if (const char *v = SDL_getenv("ALTIRRA_MACOS_FORCE_IOKIT");
		v && *v == '1') {
		SDL_SetHint(SDL_HINT_JOYSTICK_MFI, "0");
		SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
			"Altirra: ALTIRRA_MACOS_FORCE_IOKIT=1: SDL_JOYSTICK_MFI=0 "
			"(using IOKit HID instead of GameController.framework)");
	}
#endif

	phase = "SDL_Init";
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) {
		ATReportFatal(phase, SDL_GetError());
		return 1;
	}

	// Route ATLogChannel output (atcore/logging.h) to stderr so netplay
	// and other subsystem traces are visible in the terminal.  The core
	// library defaults to no-op sinks — without wiring these, channels
	// that users explicitly enable (via menus or toggled on by default,
	// like NETPLAY) emit nothing.
	ATLogSetWriteCallbacks(
		[](ATLogChannel *ch, const char *s) {
			std::fprintf(stderr, "[%s] %s\n", ch->GetName(), s);
		},
		[](ATLogChannel *ch, const char *fmt, va_list ap) {
			std::fprintf(stderr, "[%s] ", ch->GetName());
			std::vfprintf(stderr, fmt, ap);
			std::fputc('\n', stderr);
		});

	// On Android, always keep the screen on.  On desktop, this is handled
	// by ATUIApplyModeStyle() when entering Gaming Mode.
#ifdef __ANDROID__
	SDL_DisableScreenSaver();
#endif

	VDRegistryAppKey::setDefaultKey("AltirraSDL");

	phase = "registry load";
	{
		extern VDStringA ATGetConfigDir();
		SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
			"Altirra: config dir = %s", ATGetConfigDir().c_str());
	}
	// If the previous session died, load the persisted crash report so
	// the viewer can show it on the first frame.  Safe to call before
	// any UI is up — the actual viewer render happens later.
	ATCrashReportLoadPrevious();

	// Load persisted settings from ~/.config/altirra/settings.ini
	extern void ATRegistryLoadFromDisk();
	ATRegistryLoadFromDisk();

	// Load the per-dialog "last used directory" map from the registry so
	// the file dialogs can remember where the user last navigated.  This
	// uses the same "Saved filespecs" key Windows Altirra writes.
	extern void VDLoadFilespecSystemData();
	VDLoadFilespecSystemData();

	// Capture whether the registry had any data before anything writes to it.
	// Used later for first-run detection (matches Windows main.cpp:3371).
	const bool registryHadAnything = VDRegistryAppKey("", false).isReady();

	phase = "window/GL";

	const int kDefaultWidth = 1280;
	const int kDefaultHeight = 720;

	// Backend selection policy:
	//   1. Try the platform's "best fit" GL profile — Desktop 3.3 Core on
	//      Windows/Linux/macOS, OpenGL ES 3.0 on Android/iOS.  Both paths
	//      light up the full DisplayBackendGL feature set (screen FX,
	//      bicubic, bloom, librashader where the runtime is present).
	//   2. If GL/GLES context creation fails for any reason (driver bug,
	//      headless display, sandbox), fall back to SDL_Renderer — no
	//      custom shaders, but the emulator still renders correctly.
	// The choice is silent and automatic; the user does not pick a
	// backend.  IDisplayBackend::SupportsScreenFX / SupportsExternalShaders
	// gate UI surfaces (Visual Effects menu, Load Shader Preset...) so
	// a fallback session simply hides the unavailable items.

	bool useGL = false;
	SDL_GLContext glContext = nullptr;
	GLProfile glProfile = GLProfile::Desktop33;

#ifndef ALTIRRA_WASM
	// Pick the preferred GL profile per platform.  Only ONE profile is
	// attempted: requesting Desktop Core on Android, or ES on a desktop
	// driver, would either fail outright or silently mislead.
#if defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IOS) && TARGET_OS_IOS)
	glProfile = GLProfile::ES30;
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
	glProfile = GLProfile::Desktop33;
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	g_pWindow = SDL_CreateWindow("AltirraSDL", kDefaultWidth, kDefaultHeight,
		SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
	if (g_pWindow) {
		glContext = SDL_GL_CreateContext(g_pWindow);
		if (glContext) {
			SDL_GL_MakeCurrent(g_pWindow, glContext);
			// GLLoadFunctions stores the active profile internally; all
			// downstream code (shader compile, texture upload, librashader)
			// reads it via GLGetActiveProfile().
			if (GLLoadFunctions(glProfile)) {
				useGL = true;
				LOG_INFO("Main", "%s context created successfully",
					glProfile == GLProfile::ES30
						? "OpenGL ES 3.0" : "OpenGL 3.3 Core");
			} else {
				LOG_ERROR("Main", "GL function loading failed, falling back to SDL_Renderer");
				SDL_GL_DestroyContext(glContext);
				glContext = nullptr;
			}
		} else {
			LOG_ERROR("Main", "GL context creation failed: %s", SDL_GetError());
		}
	}

	// If GL failed, recreate window without OPENGL flag for SDL_Renderer.
	// SDL_Renderer is a true safety net: it picks the platform's best
	// 2D backend (D3D11/12 on Windows, Metal on macOS, GLES/Vulkan on
	// Android) but cannot run our screen FX / librashader pipeline.
	if (!useGL) {
		if (g_pWindow) SDL_DestroyWindow(g_pWindow);
		g_pWindow = SDL_CreateWindow("AltirraSDL", kDefaultWidth, kDefaultHeight, SDL_WINDOW_RESIZABLE);
		if (!g_pWindow) { LOG_INFO("Main", "CreateWindow: %s", SDL_GetError()); SDL_Quit(); return 1; }
		LOG_INFO("Main", "Falling back to SDL_Renderer (screen FX and librashader unavailable)");
	}
#else // ALTIRRA_WASM
	// WASM: no GL context creation.  SDL_Renderer on WebGL2 is the only
	// rendering path in-browser; librashader and screen FX are already
	// compiled out (see src/AltirraSDL/CMakeLists.txt).  Suppress the
	// unused-variable warning on glProfile/glContext — the values are
	// carried through to later logging code unchanged.
	(void)glProfile;
	(void)glContext;
	g_pWindow = SDL_CreateWindow("AltirraSDL", kDefaultWidth, kDefaultHeight,
		SDL_WINDOW_RESIZABLE);
	if (!g_pWindow) { LOG_INFO("Main", "CreateWindow: %s", SDL_GetError()); SDL_Quit(); return 1; }
#endif // ALTIRRA_WASM

	// Set the window/taskbar/dock icon from the baked RGBA data.
	// The largest image is the primary surface; smaller sizes are
	// attached as alternates so SDL can pick the right one for HiDPI
	// displays (matches how the Windows .ico multi-resolution picker
	// works).  On Windows the caption/ALT-TAB icon comes from here,
	// while the taskbar/Explorer icon still comes from the .ico
	// resource embedded via AltirraSDL_icon.rc — both paths are
	// required for full coverage.
#if ALTIRRA_HAS_BAKED_ICON
	if (g_pWindow && kAltirraIconCount > 0) {
		const ATBakedIcon& primary = kAltirraIcons[0];
		SDL_Surface* iconSurf = SDL_CreateSurfaceFrom(
			primary.size, primary.size,
			SDL_PIXELFORMAT_RGBA32,
			(void*)primary.rgba,
			primary.size * 4);
		if (iconSurf) {
			for (size_t i = 1; i < kAltirraIconCount; ++i) {
				const ATBakedIcon& alt = kAltirraIcons[i];
				SDL_Surface* altSurf = SDL_CreateSurfaceFrom(
					alt.size, alt.size,
					SDL_PIXELFORMAT_RGBA32,
					(void*)alt.rgba,
					alt.size * 4);
				if (altSurf) {
					SDL_AddSurfaceAlternateImage(iconSurf, altSurf);
					// SDL retains its own reference — release ours.
					SDL_DestroySurface(altSurf);
				}
			}
			if (!SDL_SetWindowIcon(g_pWindow, iconSurf)) {
				LOG_INFO("Main", "SDL_SetWindowIcon failed: %s",
					SDL_GetError());
			}
			SDL_DestroySurface(iconSurf);
		} else {
			LOG_INFO("Main", "SDL_CreateSurfaceFrom (icon) failed: %s",
				SDL_GetError());
		}
	}
#endif

	// Restore saved window size, position, and fullscreen state
	ATRestoreWindowPlacement(g_pWindow);

	// Enable text input on the main window so SDL_EVENT_TEXT_INPUT events
	// are delivered for printable characters (matches the Win32 WM_CHAR
	// path).  This is the source of cooked-character input that the
	// emulator routes through ATUIGetScanCodeForCharacter32 in
	// ATInputSDL3_HandleTextInput, which is required for non-US keyboard
	// layouts (AZERTY/QWERTZ/Dvorak), dead-key composition, IME, AltGr,
	// and the European-character cooked map (é, à, £, ñ, ö, etc.).
	//
	// On Android we deliberately do NOT enable text input at startup.
	// The default value of SDL_HINT_ENABLE_SCREEN_KEYBOARD is "auto",
	// which means SDL shows the on-screen IME whenever text input is
	// active and no physical keyboard is attached.  Activating it here
	// would auto-show the soft keyboard at launch, and any subsequent
	// configuration change (most visibly: a screen rotation) makes
	// Android re-bind the editing view and re-show the IME — exactly
	// the "keyboard appears on rotate" symptom users have reported.
	// On Android the user explicitly opts in to the system IME via the
	// ABC button on the Atari virtual keyboard (see
	// ui_virtual_keyboard.cpp), and the Atari virtual keyboard itself
	// sends raw scan codes via the POKEY API — neither path needs
	// SDL_EVENT_TEXT_INPUT.
#ifndef __ANDROID__
	SDL_StartTextInput(g_pWindow);
#endif

	// Create the display backend.
#ifndef ALTIRRA_WASM
	if (useGL) {
		g_pBackend = new DisplayBackendGL(g_pWindow, glContext);
		// VSync swap interval is managed dynamically by the main loop based
		// on g_desiredSwapInterval (set by UpdatePacerRate).  Start with
		// interval 0; the first UpdatePacerRate call will set the correct
		// value before any frame is presented.
		SDL_GL_SetSwapInterval(0);
	} else
#endif
	{
		g_pRenderer = SDL_CreateRenderer(g_pWindow, nullptr);
		if (!g_pRenderer) { LOG_INFO("Main", "CreateRenderer: %s", SDL_GetError()); SDL_DestroyWindow(g_pWindow); SDL_Quit(); return 1; }
		// Same as GL path: start with VSync off, main loop manages it.
		SDL_SetRenderVSync(g_pRenderer, 0);
		g_pBackend = new DisplayBackendSDLRenderer(g_pWindow, g_pRenderer);
	}

	// Load UI mode preference before ImGui init so ATUIApplyModeStyle()
	// inside ATUIInit() sees the correct mode.
	ATUILoadMode();
#ifdef __ANDROID__
	ATUISetMode(ATUIMode::Gaming);
#endif

	phase = "ImGui init";
	// Register the emote-preferences callbacks BEFORE settings load so
	// the "Netplay: Send emotes" / "Receive emotes" bools persist.
	ATEmoteNetplay::Initialize();
	if (!ATUIInit(g_pWindow, g_pBackend)) {
		delete g_pBackend; g_pBackend = nullptr;
		if (g_pRenderer) SDL_DestroyRenderer(g_pRenderer);
		SDL_DestroyWindow(g_pWindow);
		SDL_Quit();
		return 1;
	}

	// Register ImGui progress handler (replaces Windows ATUIInitProgressDialog)
	ATUIInitProgressSDL3();

	// Decode + upload the 16 baked emote PNGs now that the display
	// backend (GL or SDL renderer) is ready.  Textures live for the
	// entire process lifetime.
	ATEmotes::Initialize();

	// Initialize test mode automation (no-op if --test-mode not passed).
	// Skipped on Android: test mode binds a Unix socket under /tmp which
	// is not a useful path on Android and just produces confusing log noise.
#ifndef __ANDROID__
	if (!ATTestModeInit()) {
		LOG_ERROR("Main", "Failed to initialize test mode");
		// Non-fatal — continue without test mode
		g_testModeEnabled = false;
	}
#else
	g_testModeEnabled = false;
#endif

#ifdef ALTIRRA_BRIDGE_ENABLED
	// Initialise the AltirraBridge scripting/automation server. Runs on
	// every platform including Android (loopback TCP works inside the
	// app and is reachable via `adb forward tcp:N tcp:N`). Non-fatal:
	// if init fails (port in use, malformed addr spec) we log and
	// continue without the bridge.
	if (bridgeRequested) {
		if (!ATBridge::Init(bridgeAddrSpec)) {
			LOG_ERROR("Main", "Failed to initialise AltirraBridge");
		}
	}
#endif

	// Give the mouse capture system access to the window
	ATUISetMouseCaptureWindow(g_pWindow);

	// Use the real backing-store pixel size, not the 1280x720 we asked
	// the OS for.  On Android SDL3 gives us the actual device resolution
	// regardless of the requested size, and on hi-DPI desktops the pixel
	// size differs from the logical size.
	{
		int pxW = kDefaultWidth, pxH = kDefaultHeight;
		SDL_GetWindowSizeInPixels(g_pWindow, &pxW, &pxH);
		if (pxW <= 0) pxW = kDefaultWidth;
		if (pxH <= 0) pxH = kDefaultHeight;
		const char *backendName = "SDL_Renderer";
		if (useGL)
			backendName = (glProfile == GLProfile::ES30)
				? "OpenGL ES 3.0" : "OpenGL 3.3 Core";
		SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
			"Altirra: display backend = %s, window pixel size = %dx%d",
			backendName, pxW, pxH);
		g_pDisplay = new VDVideoDisplaySDL3(g_pRenderer, pxW, pxH);
	}

	// Tell the display whether the GL backend supports screen effects.
	// This makes GTIA's IsScreenFXPreferred() return true, which enables
	// accelerated screen effects (scanlines, bloom, color correction, etc.)
	if (useGL)
		g_pDisplay->SetScreenFXPreferred(true);

	// Auto-load last librashader preset if one was saved
	ATUIShaderPresetsAutoLoad(g_pBackend);

	// Pre-simulator initialization matching Windows main.cpp:3559-3589.
	// Register save state format 2 reader (needed for loading save states).
	{
		extern void ATInitSaveStateDeserializer();
		ATInitSaveStateDeserializer();
	}

	// Install ATFS virtual file system handler (needed for atfs:// paths).
	{
		extern void ATVFSInstallAtfsHandler();
		ATVFSInstallAtfsHandler();
	}

	phase = "simulator init";
	SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Altirra: initializing simulator...");
	g_sim.Init();

	// Seed the CRT rand() generator before sampling it for the simulator's
	// random seed. Without this, glibc/musl/MSVCRT all default to srand(1),
	// so every launch started the simulator in the same RNG state — POKEY
	// noise, uninitialized-memory fill patterns, and any other "randomized"
	// hardware state were bit-identical across runs. Parity with Windows
	// ATInitRand() in src/Altirra/source/main.cpp.
	//
	// Composed from SDL3's high-resolution counter plus a nanosecond
	// wall-clock sample — both are cross-platform (Windows/macOS/Linux/
	// Android), and the two sources jitter independently so the mixed
	// result is well-distributed even if either source alone has low
	// resolution on a given platform.
	{
		const Uint64 a = SDL_GetPerformanceCounter();
		const Uint64 b = SDL_GetTicksNS();
		const uint32 seed = (uint32)a ^ (uint32)(a >> 32)
		                  ^ (uint32)(b * 2654435761u)
		                  ^ (uint32)(b >> 32);
		srand(seed);
	}
	g_sim.SetRandomSeed(rand() ^ (rand() << 15));
	phase = "firmware load";
	g_sim.LoadROMs();

	g_sim.GetGTIA().SetVideoOutput(g_pDisplay);

	// Register all device definitions (Windows main.cpp:3904).
	// Must be called before ATLoadSettings so that device creation during
	// settings load can find device definitions.
	{
		extern void ATRegisterDevices(ATDeviceManager& dm);
		ATRegisterDevices(*g_sim.GetDeviceManager());
	}

	// Initialize joystick manager before loading settings — the Input
	// settings category needs GetJoystickManager() to read transforms.
	g_pJoystickMgr = ATCreateJoystickManager();
	if (g_pJoystickMgr->Init(nullptr, g_sim.GetInputManager()))
		g_sim.SetJoystickManager(g_pJoystickMgr);
	else {
		delete g_pJoystickMgr;
		g_pJoystickMgr = nullptr;
	}

	ATInputSDL3_Init(&g_sim.GetPokey(), g_sim.GetInputManager(), &g_sim.GetGTIA());

	// Initialize touch controls (active on Android, available for testing on desktop)
	ATTouchControls_Init(g_sim.GetInputManager(), &g_sim.GetGTIA());

	// Always initialize the Gaming Mode subsystem so switching at
	// runtime doesn't require deferred init.
	ATMobileUI_Init();
	ATMobileUI_LoadConfig(g_mobileState);
	ATTouchControls_SetHapticEnabled(g_mobileState.layoutConfig.hapticEnabled);

	{
		SDL_DisplayID displayID = SDL_GetDisplayForWindow(g_pWindow);
		float cs = SDL_GetDisplayContentScale(displayID);
		if (cs < 1.0f) cs = 1.0f;
		if (cs > 4.0f) cs = 4.0f;
		g_mobileState.layoutConfig.contentScale = cs;
		LOG_INFO("Main", "Touch controls content scale: %.2f", cs);
	}

	// Register device extended commands (copy/paste, explore disk, mount VHD, etc.)
	extern void ATRegisterDeviceXCmds(ATDeviceManager& dm);
	ATRegisterDeviceXCmds(*g_sim.GetDeviceManager());

	// Initialize network sockets (POSIX sockets, lookup worker, socket worker).
	// Must be called before ATLoadSettings as some device init may need sockets.
	extern bool ATSocketInit();
	ATSocketInit();

	// Load config variable overrides and options before loading settings
	// (matches Windows main.cpp:3654-3657).
	{
		extern void ATLoadConfigVars();
		ATLoadConfigVars();
	}
	ATOptionsLoad();

	// Re-apply the ImGui theme now that g_ATOptions.mThemeMode reflects
	// the user's saved choice.  ATUIInit() above already called
	// ATUIApplyTheme() once, but at that point the options were still at
	// their compile-time defaults (Light), so a persisted Dark selection
	// never took effect on startup — the options page would show "Dark"
	// while the UI rendered Light until the user toggled it manually.
	ATUIApplyTheme();

	// Load default profiles and then restore the last active profile.
	// Windows does this at main.cpp:3941-3979.  ATSettingsLoadLastProfile()
	// calls ATLoadSettings() internally with the profile's settings,
	// replacing the direct ATLoadSettings() call we had before.
	ATLoadDefaultProfiles();
	// Match Windows main.cpp:3947 — load every registered category except
	// FullScreen (window placement is handled separately).  Using the
	// _All mask ensures Devices, MountedImages, and NVRAM round-trip across
	// restarts, and any future category added to settings.cpp is picked up
	// automatically.
	ATSettingsLoadLastProfile((ATSettingsCategory)(
		kATSettingsCategory_All & ~kATSettingsCategory_FullScreen
	));

	// SDL3-specific audio-latency default (deliberate divergence from
	// Windows Altirra).
	//
	// settings.cpp defaults "Audio: Latency" to 80 ms when the INI key
	// is absent — that's the Windows-appropriate value because the Win32
	// audio backends (WaveOut / DirectSound / WASAPI) provide exact
	// hardware-side buffer accounting plus blocking Write(), so 80 ms is
	// a reliable floor.  The SDL3 build has neither and instead uses
	// active clock recovery (see FramePacer::ComputeClockRecovery in
	// main_pacer.cpp) to pin the audio queue tight to whatever target
	// the user picks.  That feedback loop makes much smaller targets
	// reliable, so for SDL3 we override the absent-key default to 30 ms
	// (≈ 1.5 × a PipeWire quantum, ≈ 50 ms tighter A/V offset than
	// Windows out of the box).
	//
	// We ONLY apply this when the user has no explicit setting in their
	// INI.  If the key is present — even if the user wrote "80" on
	// purpose — we respect it.  This is why we check the registry type
	// directly instead of comparing the loaded value: getInt returns
	// the fallback default indistinguishably from a stored value, so we
	// would otherwise unable to tell "user chose 80" from "no setting
	// exists, fell back to 80".
	{
		const uint32 profileId = ATSettingsGetCurrentProfileId();
		VDStringA path;
		path.sprintf("Profiles\\%08X", profileId);
		VDRegistryAppKey profileKey(path.c_str(), false);
		if (profileKey.getValueType("Audio: Latency")
		    == VDRegistryKey::kTypeUnknown)
		{
			if (IATAudioOutput *ao = g_sim.GetAudioOutput())
				ao->SetLatency(30);
		}
	}

#ifdef __ANDROID__
	// On first run (no saved settings), default to PAL for mobile.
	// ATSettingsLoadLastProfile sets NTSC in ATSettingsExchangeStartupConfig;
	// override to PAL when "Defaults inited" was just created this session
	// (ATLoadDefaultProfiles above will have set it on first boot).
	// We detect first run by checking a mobile-specific flag we set ourselves.
	{
		VDRegistryAppKey key("", true);
		if (!key.getBool("Mobile defaults applied")) {
			g_sim.SetVideoStandard(kATVideoStandard_PAL);
			key.setBool("Mobile defaults applied", true);
		}
	}

	// Make sure a *port-1* joystick input map is active, otherwise
	// the on-screen fire/joystick buttons emit input codes that are
	// never bound to the Atari port 0 controller (port 0 = joystick 1,
	// the port games default to).
	//
	// Background: on Windows / desktop, the user picks an input map
	// via the "Input" menu, and that activation is written to the
	// registry so next time LoadSelections() in inputmanager.cpp
	// re-activates it.  On a fresh mobile install there's no saved
	// selection *and* `defaultControllerType == kATInputControllerType_None`
	// on non-5200 hardware (settings.cpp:726) — so LoadSelections
	// skips its "pick first matching map" fallback and leaves the
	// input manager with every preset map loaded but none active.
	//
	// Even after we force-activate ANY joystick map, there are three
	// default joystick maps:
	//   "Gamepad -> Joystick (port 1)"   mUnit=-1 (any)  port=0
	//   "Gamepad 1 -> Joystick (port 1)" mUnit= 0         port=0
	//   "Gamepad 2 -> Joystick (port 2)" mUnit= 1         port=1
	// mInputMaps is a `std::map<ATInputMap*, bool>`, so iteration order
	// is by pointer address (nondeterministic).  If we happened to
	// pick the "port 2" map, touch inputs would route to the wrong
	// Atari port and nothing would happen in-game.
	//
	// Fix: look for a map that (a) matches the current hardware's
	// controller type, (b) uses Atari physical port 0, and
	// (c) prefers mUnit=-1 (works for any input source) over a
	// specific unit index.  Activate that one explicitly, and log
	// the name so future debugging is trivial.
	{
		ATInputManager *im = g_sim.GetInputManager();

		// First deactivate every currently-active map so we don't
		// end up with the wrong one lingering (e.g. if a previous
		// session saved "Gamepad 2" as active).
		const uint32 count = im->GetInputMapCount();
		for (uint32 i = 0; i < count; ++i) {
			vdrefptr<ATInputMap> imap;
			if (im->GetInputMapByIndex(i, ~imap) && imap)
				im->ActivateInputMap(imap, false);
		}

		ATInputControllerType wanted =
			(g_sim.GetHardwareMode() == kATHardwareMode_5200)
				? kATInputControllerType_5200Controller
				: kATInputControllerType_Joystick;

		ATInputMap *chosen = nullptr;
		int chosenScore = -1;

		// Codes that ATTouchControls_HandleEvent actually emits.  The
		// chosen input map MUST contain mappings for these, otherwise
		// touch input gets swallowed by the input manager with no
		// effect (e.g. the "Numpad -> Joystick (port 1)" preset also
		// targets port 0 but binds kATInputCode_KeyNumpad* — useless
		// for touch).
		// ---------------------------------------------------------------
		// Virtual joystick / fire wiring — how it works, in one place
		// ---------------------------------------------------------------
		// touch_controls.cpp converts finger events into five "source"
		// input codes it feeds directly into the input manager via
		// OnButtonDown/OnButtonUp with unit=0:
		//
		//   kATInputCode_JoyStick1Left  (0x2100)
		//   kATInputCode_JoyStick1Right (0x2101)
		//   kATInputCode_JoyStick1Up    (0x2102)
		//   kATInputCode_JoyStick1Down  (0x2103)
		//   kATInputCode_JoyButton0     (0x2800)
		//
		// For these to actually move the player / fire, ONE input map
		// must be active that:
		//   1. Has a Joystick controller attached to Atari physical
		//      port 0 (HasControllerType(Joystick) && UsesPhysicalPort(0)).
		//   2. SpecificInputUnit == -1 (accept any source unit — our
		//      touch controller is not a registered input unit).
		//   3. Contains direct 1:1 mappings from the five source codes
		//      above to the joystick triggers Left/Right/Up/Down/Button0.
		//
		// Several built-in presets look plausible but silently fail:
		//   "Numpad -> Joystick (port 1)"           — sources are
		//       kATInputCode_KeyNumpad*, not JoyStick1*.
		//   "Gamepad -> Joystick (port 1)"          — has all 5 direct
		//       sources but also has fall-through analog-axis sources,
		//       and std::map<ATInputMap*, bool> iteration order is by
		//       pointer address, so this one sometimes won the tiebreak
		//       over the properly-authored "Xbox 360 Controller" map.
		//   "Gamepad N -> Joystick (port 1)"        — unit != -1.
		//
		// countTouchBindings() below counts distinct direct 1:1 source
		// codes.  The selector requires all 5 to be present and picks
		// the best candidate by (unit-agnostic bonus + match count),
		// which reliably resolves to "Xbox 360 Controller -> Joystick
		// (port 1)" on a default install.
		//
		// The activation MUST happen after default maps are loaded
		// (settings.cpp runs LoadSelections) but BEFORE the main loop
		// starts polling input.  ColdReset does NOT detach controllers
		// from the port manager, so the activation is stable across
		// subsequent sim resets.
		auto countTouchBindings = [](ATInputMap *imap) -> int {
			bool hasL=false,hasR=false,hasU=false,hasD=false,hasFire=false;
			const uint32 m = imap->GetMappingCount();
			for (uint32 i = 0; i < m; ++i) {
				const auto &mapping = imap->GetMapping(i);
				uint32 code = mapping.mInputCode & kATInputCode_IdMask;
				if (code == kATInputCode_JoyStick1Left)  hasL = true;
				if (code == kATInputCode_JoyStick1Right) hasR = true;
				if (code == kATInputCode_JoyStick1Up)    hasU = true;
				if (code == kATInputCode_JoyStick1Down)  hasD = true;
				if (code == (uint32)kATInputCode_JoyButton0) hasFire = true;
			}
			return (hasL?1:0) + (hasR?1:0) + (hasU?1:0) + (hasD?1:0) + (hasFire?1:0);
		};

		for (uint32 i = 0; i < count; ++i) {
			vdrefptr<ATInputMap> imap;
			if (!im->GetInputMapByIndex(i, ~imap) || !imap)
				continue;
			if (!imap->HasControllerType(wanted))
				continue;
			// Must target Atari physical port 0 (= joystick port 1).
			if (!imap->UsesPhysicalPort(0))
				continue;
			int touchMatches = countTouchBindings(imap);
			// Must have ALL 5 direct touch source codes (4 dirs + fire),
			// otherwise some directions/fire will silently be dead.
			if (touchMatches < 5)
				continue;
			// Prefer generic (unit=-1) over a specific unit index,
			// because our touch controller isn't registered as a
			// specific input unit.  Break further ties by number of
			// direct matches (already clamped to 5).
			int score = (imap->GetSpecificInputUnit() == -1) ? 100 : 50;
			score += touchMatches;
			if (score > chosenScore) {
				chosenScore = score;
				chosen = imap;
			}
		}

		if (chosen) {
			im->ActivateInputMap(chosen, true);
			VDStringA nameU8 = VDTextWToU8(VDStringW(chosen->GetName()));
			SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
				"Altirra: activated mobile input map '%s' (unit=%d)",
				nameU8.c_str(), chosen->GetSpecificInputUnit());
		} else {
			SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
				"Altirra: no port-0 joystick input map matches current "
				"hardware — touch controls will not route to the emulator");
		}
	}
#endif

	// Register the options update callback for accelerated screen FX.
	// This mirrors Windows main.cpp — mbDisplayAccelScreenFX defaults to
	// true in ATOptions, and ATLoadSettings loads the persisted value.
	// The callback is invoked immediately (true = call now) to apply the
	// initial value, and again whenever g_ATOptions changes at runtime.
	ATOptionsAddUpdateCallback(true,
		[](ATOptions& opts, const ATOptions *prevOpts, void *) {
			g_sim.GetGTIA().SetAccelScreenFXEnabled(opts.mbDisplayAccelScreenFX);
		}
	);

	// Re-apply fullscreen mode if settings change while already in fullscreen.
	// In the ImGui build the settings dialog is non-modal, so the user can
	// change fullscreen options while the emulator is fullscreen.
	ATOptionsAddUpdateCallback(false,
		[](ATOptions& opts, const ATOptions *prevOpts, void *) {
			if (!g_pWindow || !prevOpts)
				return;
			// Only act if a fullscreen-related option actually changed
			if (opts.mbFullScreenBorderless == prevOpts->mbFullScreenBorderless &&
				opts.mFullScreenWidth == prevOpts->mFullScreenWidth &&
				opts.mFullScreenHeight == prevOpts->mFullScreenHeight &&
				opts.mFullScreenRefreshRate == prevOpts->mFullScreenRefreshRate)
				return;
			// If currently in fullscreen, re-apply the mode immediately
			if (SDL_GetWindowFlags(g_pWindow) & SDL_WINDOW_FULLSCREEN) {
				ATApplyFullscreenMode(g_pWindow);
				// Force SDL3 to re-enter fullscreen with the new mode
				SDL_SetWindowFullscreen(g_pWindow, false);
				SDL_SetWindowFullscreen(g_pWindow, true);
			}
		}
	);

	// Initialize the virtual key map from loaded keyboard options
	// (matches Windows main.cpp:3857).
	ATUIInitVirtualKeyMap(g_kbdOpts);

	// Initialize command handlers and accelerator tables for keyboard shortcuts.
	// Must be called after settings are loaded so custom shortcuts are restored.
	{
		extern void ATUIInitSDL3Commands();
		ATUIInitSDL3Commands();
		ATUIInitDefaultAccelTables();
		ATUILoadAccelTables();
	}

	// macOS: install native NSMenu bar (replaces ImGui menu bar).
	// Must be called after SDL window creation and command/accel table init.
	// On non-Apple platforms ATMacMenuBarInit() is an inline no-op.
	ATMacMenuBarInit();

	// Create the native audio device now that settings have been loaded
	// (SetApi, SetLatency, etc. may have been called during ATLoadSettings).
	phase = "audio init";
	g_sim.GetAudioOutput()->InitNativeAudio();

	// Initialize debugger engine (breakpoint manager, event callbacks, debug targets).
	// Must be called after g_sim.Init() and ATLoadSettings() — the settings load
	// may create devices whose debug targets the debugger needs to enumerate.
	{
		extern void ATInitDebugger();
		ATInitDebugger();
	}

#ifdef __EMSCRIPTEN__
	// WASM: silently skip the debugger's symbol + script auto-load
	// when the emulator cold-boots a game.
	//
	// By default Altirra looks for companion files next to the booted
	// image — for example, booting `foo.xex` triggers open() of
	// `foo.lst`, `foo.lab`, `foo.atdbg`, `foo.atdbg2` so that any
	// attached debug symbols become available automatically.  Those
	// files almost never exist in the browser's virtual filesystem
	// (users upload the game image alone), and the debugger's file
	// open call throws VDException("No such file or directory") which
	// propagates out of the deferred-boot handler and aborts the
	// emulator.
	//
	// Desktop users can still enable symbol auto-load from Configure
	// System → Debugger if they manually upload a .lst/.lab companion
	// alongside the game.
	{
		IATDebugger *dbg = ATGetDebugger();
		if (dbg) {
			dbg->SetSymbolLoadMode(false, ATDebuggerSymbolLoadMode::Disabled);
			dbg->SetSymbolLoadMode(true,  ATDebuggerSymbolLoadMode::Disabled);
			dbg->SetScriptAutoLoadMode(ATDebuggerScriptAutoLoadMode::Disabled);
		}
	}
#endif

	// Register emulation error handler (matches Windows main.cpp:4007).
	// Must be after debugger init so OnDebuggerOpen event is available.
	ATInitEmuErrorHandlerSDL3(&g_sim);

	// Initialize compatibility database (matches Windows main.cpp:3933).
	// The internal DB is embedded as a Windows resource (IDR_COMPATDB) which
	// is not available in SDL3 builds — ATLockResource returns nullptr and
	// the internal DB is skipped.  The external DB path (from options) works.
	{
		extern void ATCompatInit();
		ATCompatInit();
	}

	// Save options after initial load (matches Windows main.cpp:4002).
	ATOptionsSave();

	// Full command-line processing matching Windows uicommandline.cpp.
	// Handles all switches (--debug, --debugcmd, --hardware, --ntsc, etc.),
	// media loading (--cart, --disk, --run, positional args), startup.atdbg,
	// and debug suspend mode.  Returns true if any boot image was loaded.
	bool cmdLineHadBootImage = ATProcessCommandLineSDL3(argc, argv);
	if (!cmdLineHadBootImage) {
		// Detect whether ATSettingsLoadLastProfile restored any mounted
		// media (cart, disk, cassette).  If so the user's last session
		// had something running — honour that by jumping straight to the
		// emulator view instead of parking the user at the game browser
		// with an invisible-but-loaded cart.  Matches the user's mental
		// model of "what I closed is what I open."
		bool restoredMedia = false;
		if (g_sim.GetCartridge(0) || g_sim.GetCartridge(1))
			restoredMedia = true;
		if (!restoredMedia && g_sim.GetCassette().IsLoaded())
			restoredMedia = true;
		if (!restoredMedia) {
			for (int i = 0; i < 15; ++i) {
				if (g_sim.GetDiskInterface(i).IsDiskLoaded()) {
					restoredMedia = true;
					break;
				}
			}
		}

		// In gaming mode with nothing restored, Game Library is the home
		// screen.  The emulator stays paused until the user picks a game.
		if (ATUIIsGamingMode() && !restoredMedia) {
			GameBrowser_Init();
			g_mobileState.currentScreen = ATMobileUIScreen::GameBrowser;
			g_sim.ColdReset();
		} else {
			// Either desktop mode, or gaming mode with restored media —
			// in both cases show the emulator view and start running.
			if (ATUIIsGamingMode()) {
				GameBrowser_Init();
				g_mobileState.currentScreen = ATMobileUIScreen::None;
				// ui_mobile.cpp redirects None → GameBrowser unless
				// gameLoaded is true; flag it since LoadMountedImages
				// already restored the cart/disk/tape.
				g_mobileState.gameLoaded = true;
			}
			g_sim.ColdReset();
			g_sim.Resume();
		}
	}

	// Auto-show setup wizard on first run (matches Windows uicommandline.cpp:824-840).
	// Skip if: already shown before, command line had arguments, or registry had data
	// (meaning the program has been run before).
	{
		VDRegistryAppKey key;
		if (!key.getBool("ShownSetupWizard")) {
			key.setBool("ShownSetupWizard", true);

			bool cmdLineHadAnything = (argc > 1);

			if (!cmdLineHadAnything && !registryHadAnything) {
#ifndef __ANDROID__
				// On desktop, show the setup wizard for first-time
				// configuration.  On Android, the mobile first-run
				// flow handles this instead.
				g_uiState.showSetupWizard = true;
#endif
			}
		}
	}

	phase = "main loop";
	g_pacer.Init();
	UpdatePacerRate();

	if (ATUIIsGamingMode()) {
		ATMobileUI_ApplyVisualEffects(g_mobileState);
		ATMobileUI_ApplyPerformancePreset(g_mobileState);
	}

#ifdef __ANDROID__
	{
		bool restored = ATMobileUI_RestoreSuspendState(g_sim, g_mobileState);
		if (restored) {
			SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
				"Altirra: restored previous session from quicksave");
		}
	}
#endif

	// Present once immediately so compositors show the window.
	RenderAndPresent();

	SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Altirra: entering main loop");

	// Main loop.  Mirrors the Windows idle handler structure:
	//
	// 1. Advance() runs emulation for up to g_ATSimScanlinesPerAdvance
	//    scanlines (default 32).  A full NTSC frame is 262 scanlines,
	//    so ~9 Advance() calls per frame.
	//
	// 2. When Advance() returns kAdvanceResult_WaitingForFrame, GTIA
	//    has completed a frame.  We upload it, present, and then SLEEP
	//    until it's time for the next frame (frame pacing).
	//
	// 3. The Windows version sleeps via MsgWaitForMultipleObjects or a
	//    waitable timer.  We use SDL_DelayPrecise() for equivalent
	//    nanosecond-precision sleep.
	//
	// Without this sleep, the emulator runs Advance() as fast as the
	// CPU allows, producing frames far faster than 60 Hz.

	// --- Tick body ----------------------------------------------------
	// Extracted to a capture-free lambda so the browser target
	// (Emscripten) can drive it via `emscripten_set_main_loop` while
	// native targets keep the same code path inside an explicit while
	// loop.  Every `continue` inside the body became `return;`
	// (equivalent semantics — skip the rest of this iteration and come
	// back at the next tick) and the sole `break` (the
	// `if (!g_running) break;` right after HandleEvents) became
	// `if (!g_running) return;`.  `g_pacer.WaitForNextFrame()` calls
	// are skipped on WASM because the browser's requestAnimationFrame
	// already paces us at the display refresh rate — calling the pacer's
	// SDL_DelayPrecise on top would produce a long stall instead.
	//
	// Capture list is intentionally empty (`[]`): under WASM the
	// emscripten_set_main_loop path throws an unwind exception out of
	// main() that destroys its stack frame, so any captured reference
	// would dangle.  Every symbol the body references is either a
	// global, a file-static, a function, or a static-local inside
	// the lambda itself.
	auto tickBody = []() {
		HandleEvents();
		if (!g_running) return;

		// Update SDL window title from current simulator config
		// (no-op unless config or active profile actually changed).
		extern void ATUpdateWindowCaption();
		ATUpdateWindowCaption();

		// Process test mode commands from external agent (no-op if not in test mode)
		ATTestModePollCommands(g_sim, g_uiState);

#ifdef ALTIRRA_BRIDGE_ENABLED
		// Process AltirraBridge commands from external scripting clients
		// (no-op if --bridge wasn't passed). Single-threaded: socket I/O
		// happens here on the main thread, capped at 64 commands/frame.
		ATBridge::Poll(g_sim, g_uiState);
#endif

#ifdef ALTIRRA_NETPLAY_ENABLED
		// Drive the netplay coordinator (no-op when no session exists).
		// Drains the UDP socket, runs the handshake / snapshot transfer
		// state machine, and emits outgoing input packets.  Must run
		// BEFORE the pause-inactive check and the Advance() call so
		// that CanAdvanceThisTick() reflects the freshly-drained peer
		// input state for this tick.
		ATNetplayGlue::Poll(SDL_GetTicks());
		ATNetplayUI_Poll(SDL_GetTicks());
#endif

#ifdef __EMSCRIPTEN__
		// Drain the WASM-only timer scheduler (replaces the detached
		// std::thread-based VDLazyTimer path on native — see
		// src/system/source/time_sdl3.cpp).  Must be called every tick
		// before anything that might register or cancel a timer.
		// Drain any pending upload / boot / library-refresh requests
		// posted by the JS shell via ATWasmOnFileUploaded (see
		// wasm_bridge.cpp).  Safe to call every tick; no-op when the
		// queue is empty.  Forward-declared at file scope above.
		VDWASMTimerTick();
		ATWasmBridgeTick();

		// Periodic IDBFS flush so in-memory writes (emulator settings,
		// save states, registry updates, uploaded files that somehow
		// skipped the upload-time syncfs) persist to IndexedDB within
		// a few seconds of happening.  pagehide is a best-effort
		// fire-and-forget safety net; this is the primary durability
		// mechanism and covers the common "user closes the tab"
		// case without relying on onbeforeunload / pagehide firing.
		//
		// Interval: every 600 ticks ≈ 10 s at 60 Hz.  Shorter wastes
		// IDB transactions on unchanged state, longer loses more
		// recent writes on ungraceful close.  Tuned by feel.
		static int s_wasmFlushTick = 0;
		if (++s_wasmFlushTick >= 600) {
			s_wasmFlushTick = 0;
			ATWasmSyncFSOut();
		}
#endif

		// Process deferred file dialog results on main thread
		ATUIPollDeferredActions();

		// Surface any background I/O exceptions from active recording
		// writers (video/audio/SAP/VGM). Parity with Windows main.cpp:3098
		// ATUIFrontEnd::CheckRecordingExceptions(). No-op when nothing is
		// recording; when a writer reports an error it is torn down and a
		// modal error is shown to the user.
		ATUICheckRecordingExceptions();

		// Tick the debugger engine (process queued commands)
		ATUIDebuggerTick();

		// Android/mobile: while backgrounded, do not advance emulation
		// and do not render.  Just keep pumping events so we receive the
		// resume notification.  Longer sleep to stay off the CPU.  Must
		// be checked BEFORE pauseInactive so we take the cheap path and
		// avoid even the no-op RenderAndPresent call.
		if (g_appSuspended) {
#ifndef __EMSCRIPTEN__
			// On WASM the browser already throttles hidden-tab rAF to
			// ~1 Hz, so the equivalent of this sleep happens outside
			// our loop.  SDL_Delay would block the single JS thread
			// unnecessarily.
			SDL_Delay(100);
#endif
			return;
		}

		// Pause emulation when window loses focus (if enabled).
		// Suppressed during an active netplay session: if the joiner
		// stops advancing because their window is in the background
		// the host stalls on peer-silence, desyncs, and the joiner
		// sees a frozen black screen (no GTIA frames completed since
		// the snapshot apply).  Netplay MUST keep ticking while both
		// peers are connected.
		bool pauseInactive = ATUIGetPauseWhenInactive() && !g_winActive;
#ifdef ALTIRRA_NETPLAY_ENABLED
		if (ATNetplayGlue::IsActive()) pauseInactive = false;
#endif

		if (pauseInactive) {
			// Window inactive — render for UI responsiveness, sleep.
			RenderAndPresent();
#ifndef __EMSCRIPTEN__
			SDL_Delay(16);
#endif
			return;
		}

		// Turbo frame-skip: in turbo mode, drop most frames at the GTIA
		// framebuffer-allocation level so GTIA skips artifacting / palette
		// correction / framebuffer writes for (divisor-1) of every divisor
		// frames. Significant CPU saving in turbo mode on lower-end
		// Linux/macOS/Android, with no effect at normal speed (turbo=false
		// → dropFrame=false always). Matches Windows main.cpp:3180 — reads
		// the same g_ATCVEngineTurboFPSDivisor CVar and clamps to [1,100].
		const bool turbo = g_sim.IsTurboModeEnabled();
		static uint32 s_turboFrameCounter = 0;
		const uint32 turboDivisor =
			std::clamp<uint32>((uint32)(sint32)g_ATCVEngineTurboFPSDivisor, 1u, 100u);
		const bool dropFrame = turbo && ((++s_turboFrameCounter) % turboDivisor) != 0;

#ifdef ALTIRRA_NETPLAY_ENABLED
		if (ATNetplayGlue::IsLockstepping()) {
			// Capture local SDL input state and push to the
			// coordinator (keyed D frames ahead per the lockstep
			// invariant).
			ATNetplayGlue::SubmitLocalInput();

			// If the peer hasn't delivered their input for the
			// current emu frame yet, do a bounded fast-poll
			// instead of going through the full RenderAndPresent
			// (which blocks on vsync for up to ~16.7 ms).  On
			// localhost the peer's packet arrives sub-ms; a
			// vsync block here would turn a 0.5 ms wait into a
			// 16.7 ms wait and halve aggregate throughput to
			// ~30 fps (both peers block symmetrically each
			// frame).  See NETPLAY_DESIGN_PLAN.md §6.
			if (!ATNetplayGlue::CanAdvanceThisTick()) {
				const uint64_t stallStartMs = SDL_GetTicks();
				const uint64_t stallBudgetMs = 12;  // < 1 frame
				while (!ATNetplayGlue::CanAdvanceThisTick()
				       && SDL_GetTicks() - stallStartMs < stallBudgetMs) {
					SDL_Delay(0);  // yield to OS, no block
					ATNetplayGlue::Poll(SDL_GetTicks());
				}
				// Still closed after the budget — fall back to
				// render + vsync so the UI stays responsive and
				// the user can see the "Peer: N ms ago" HUD go
				// red.  Retry next iteration.
				if (!ATNetplayGlue::CanAdvanceThisTick()) {
					RenderAndPresent();
#ifndef __EMSCRIPTEN__
					if (!turbo) g_pacer.WaitForNextFrame();
#endif
					return;
				}
				// Gate opened inside the fast-poll budget —
				// resubmit local input (currentFrame may have
				// shifted) and fall through to Advance.
				ATNetplayGlue::SubmitLocalInput();
			}

			// Both peers' inputs for the upcoming frame are
			// available — drive the netplay-owned controller
			// ports with them so the sim's joystick reads
			// reflect (host, joiner) in lockstep.
			ATNetplayGlue::ApplyFrameInputsToSim();
		}
#endif

		// ── Emulation advance (paced on WASM) ─────────────────────
		//
		// Each Advance() call runs at most g_ATSimScanlinesPerAdvance
		// (default 32) scanlines.  A full NTSC frame is 262 lines, so
		// producing one displayed frame takes ~9 Advance() calls.
		//
		// On NATIVE the outer while(g_running) loop + g_pacer handle
		// timing: the loop spins Advance() until hadFrame, then
		// g_pacer.WaitForNextFrame() sleeps until the next target
		// frame.  That gives cycle-accurate Hardware / Broadcast /
		// Integral pacing driven by g_pacer.targetSecsPerFrame.
		//
		// On WASM there is exactly one tick invocation per browser
		// requestAnimationFrame (~60 Hz on most displays, 120+ Hz on
		// gaming monitors, sub-1 Hz when the tab is hidden).  We
		// cannot sleep inside the tick (that would burn the rAF
		// slot), so we instead use a real-time accumulator:
		//
		//   - Measure real time elapsed since the previous tick.
		//   - When the accumulated real time >= targetSecsPerFrame,
		//     emulate one frame and subtract the target period.
		//   - Otherwise, skip the Advance() call this tick — the
		//     render section further down still presents so UI
		//     stays responsive.
		//
		// This reuses the exact targetSecsPerFrame the native pacer
		// would apply, so Config → Speed (Hardware / Integral /
		// Broadcast), video standard (NTSC / PAL / SECAM), the speed
		// modifier, and slow motion all work identically on WASM.
		// Turbo bypasses the accumulator and emulates every rAF so
		// fast-forward still speeds up as much as the browser allows.
		ATSimulator::AdvanceResult result = ATSimulator::kAdvanceResult_Running;
		bool hadFrame = false;
#ifdef __EMSCRIPTEN__
		bool doEmulate = turbo;
		{
			// Real-time accumulator carried across ticks.  Static
			// locals are zeroed on first entry, which gives us the
			// right behaviour: initial delta == 0, no emulation until
			// the second tick measures real elapsed time.
			static uint64_t s_wasmLastRealMs = 0;
			static double   s_wasmAccumMs   = 0.0;

			const uint64_t nowMs = SDL_GetTicks();
			if (s_wasmLastRealMs == 0) s_wasmLastRealMs = nowMs;
			double deltaMs = (double)(nowMs - s_wasmLastRealMs);
			s_wasmLastRealMs = nowMs;

			// Cap runaway deltas (tab backgrounded for a while,
			// throttled to 1 Hz, then regains focus).  Without this
			// the accumulator would suddenly hold many seconds of
			// real time and the emulator would try to catch up all
			// at once, freezing the tab for a visible moment.
			if (deltaMs > 250.0) deltaMs = 250.0;
			s_wasmAccumMs += deltaMs;

			// targetSecsPerFrame is set by UpdatePacerRate from the
			// user's Speed settings and video standard.  Defaults:
			// NTSC Hardware 1/59.9227 s, NTSC Integral 1/60 s, PAL
			// Hardware 1/49.8607 s, PAL Integral 1/50 s.
			const double targetMs = g_pacer.targetSecsPerFrame * 1000.0;

			if (!turbo && targetMs > 0.0 && s_wasmAccumMs >= targetMs) {
				doEmulate = true;
				s_wasmAccumMs -= targetMs;
				// Drop stale accumulation so an unusually long stall
				// doesn't produce a burst of frames afterwards.
				if (s_wasmAccumMs > targetMs * 3.0) s_wasmAccumMs = 0.0;
				if (s_wasmAccumMs < 0.0)            s_wasmAccumMs = 0.0;
			}
		}

		if (doEmulate) {
			// Spin Advance() until a frame completes or the per-tick
			// budget is exhausted.  Budget < one 60 Hz slot so the
			// tick can't overshoot into the next rAF.
			const uint64_t advStartMs = SDL_GetTicks();
			constexpr uint64_t tickBudgetMs = 14;
			for (;;) {
				result = g_sim.Advance(dropFrame);
				hadFrame = g_pDisplay->IsFramePending();
				if (hadFrame) break;
				if (result == ATSimulator::kAdvanceResult_WaitingForFrame) break;
				if (result == ATSimulator::kAdvanceResult_Stopped)          break;
				if (SDL_GetTicks() - advStartMs > tickBudgetMs) break;
			}
		}
#else
		result = g_sim.Advance(dropFrame);
		hadFrame = g_pDisplay->IsFramePending();
#endif
		// Drain deferred UI queue steps (custom-device VM commands,
		// alert dialogs, etc.). Bounded to a handful per tick so a
		// runaway script can't stall the frame loop.
		for (int i = 0; i < 16; ++i) {
			if (!ATUIGetQueue().Run())
				break;
		}

		g_pDisplay->PrepareFrame();

#ifdef ALTIRRA_NETPLAY_ENABLED
		// CRITICAL for lockstep determinism: advance the lockstep
		// frame counter ONLY when a full emulated frame completed
		// (GTIA produced output).
		//
		// ATSimulator::Advance runs up to g_ATSimScanlinesPerAdvance
		// (32) scanlines per call — so a full 262-line NTSC frame
		// takes ~9 Advance calls.  And Advance can return
		// kAdvanceResult_WaitingForFrame with ZERO work done when
		// GTIA::BeginFrame can't start (display-consumer busy).  If
		// we call OnFrameAdvanced per Advance call, peer A's extra
		// no-op due to a transient display block bumps its lockstep
		// counter but not its sim state, while peer B's counter bumps
		// WITH sim progress — both counters match, but sim states
		// diverge.  The input-hash check can't catch it (inputs are
		// still identical), so the desync detector stays silent while
		// the games drift apart on screen.
		//
		// Tying OnFrameAdvanced to hadFrame means one lockstep tick =
		// one emu frame on both peers, regardless of how Advance is
		// internally chunked or how many no-op WaitingForFrame calls
		// occurred.
		if (hadFrame) {
			ATNetplayGlue::OnFrameAdvanced();
		}
#endif

		// (turbo is declared above for the dropFrame computation.)

		// Update GL swap interval based on turbo mode and frame-rate match.
		//
		// Turbo: always interval 0 so the GPU can swap as fast as possible.
		//
		// Normal: use g_desiredSwapInterval set by UpdatePacerRate().  When
		// the display refresh matches the emulation rate (e.g. NTSC ~60 Hz
		// on a 60 Hz display), interval 1 gives smooth VSync lock.  When
		// they don't match (e.g. PAL 50 Hz on 60 Hz), interval 0 lets the
		// frame pacer control timing while the desktop compositor prevents
		// tearing — mirroring Windows Altirra's DXGI Present(0) + DWM
		// behaviour in windowed mode.
#ifndef __EMSCRIPTEN__
		{
			int wantInterval = turbo ? 0 : g_desiredSwapInterval;
			static int s_lastInterval = 0;  // matches startup swap interval 0
			if (wantInterval != s_lastInterval) {
				s_lastInterval = wantInterval;
				if (g_pRenderer)
					SDL_SetRenderVSync(g_pRenderer, wantInterval);
				else
					SDL_GL_SetSwapInterval(wantInterval);
			}
		}
#endif

		bool didRender = false;
		if (hadFrame) {
#ifdef ALTIRRA_BRIDGE_ENABLED
			// Frame-gate hook for the bridge: decrements its counter
			// and re-pauses the simulator when the gate hits zero.
			// No-op if no FRAME command is currently active.
			ATBridge::OnFrameCompleted(g_sim);
#endif
			// A frame was uploaded — present it and pace.
			RenderAndPresent();
			didRender = true;

			// Sync pacer rate and audio rate with current speed settings.
			// Cheap — just reads a few values and updates if changed.
			UpdatePacerRate();

			// In turbo mode, skip frame pacing to run as fast as possible.
#ifndef __EMSCRIPTEN__
			if (!turbo)
				g_pacer.WaitForNextFrame();
#endif
		} else if (result == ATSimulator::kAdvanceResult_WaitingForFrame) {
			// GTIA is blocked but we had no frame to show.
			// Present anyway to keep UI responsive.
			RenderAndPresent();
			didRender = true;
#ifndef __EMSCRIPTEN__
			if (!turbo)
				g_pacer.WaitForNextFrame();
#endif
		} else if (result == ATSimulator::kAdvanceResult_Stopped) {
			// Paused/stopped — render for UI, sleep to avoid busy-wait.
			// Parity with Windows main.cpp:3268 which drains pending
			// deferred simulator events on every idle pass while not
			// running. Normally a no-op because nothing produces deferred
			// events while Advance() isn't running, but any path that
			// enqueues one from a UI command handler during pause would
			// otherwise stall until the next Advance().
			g_sim.FlushDeferredEvents();
			RenderAndPresent();
			didRender = true;
#ifndef __EMSCRIPTEN__
			SDL_Delay(16);
#endif
		}

#ifdef __EMSCRIPTEN__
		// Paced-skip slots (the accumulator wasn't ready yet) leave
		// didRender == false because none of the three cases above
		// hit.  On native the outer while loop would just spin back
		// into Advance() — on WASM we only get one tick per rAF, so
		// we must still present something, otherwise dialogs, file
		// manager animation, touch controls, and the emulator HUD
		// all freeze during the "gap" rAFs.
		if (!didRender) {
			RenderAndPresent();
		}
#endif
		(void)didRender;
		// kAdvanceResult_Running with no frame: loop immediately.
	};

#ifdef __EMSCRIPTEN__
	// WASM: hand the tick over to the browser's requestAnimationFrame
	// scheduler.  emscripten_set_main_loop_arg stashes the lambda
	// state pointer in a stable runtime slot, so `tickBody` is safe to
	// capture even though main() will formally return when the first
	// rAF dispatch schedules the next tick.  simulate_infinite_loop=1
	// throws an unwind exception that prevents the native shutdown
	// code below from executing — the browser owns the process until
	// the tab closes.
	static auto s_wasmTickFn = tickBody;
	emscripten_set_main_loop_arg(
		[](void* arg) {
			auto& fn = *static_cast<decltype(&s_wasmTickFn)>(arg);
			if (!g_running) {
				emscripten_cancel_main_loop();
				return;
			}
			fn();
		},
		&s_wasmTickFn,
		0,    // fps=0 → sync to browser rAF (typically 60 Hz)
		1     // simulate_infinite_loop=1 → throw out of main
	);
#else
	while (g_running) {
		tickBody();
	}
#endif

#ifdef ALTIRRA_BRIDGE_ENABLED
	// Drop any bridge client, release injected input, free the
	// joystick PIA input slot, and remove the token file before the
	// simulator goes away. Idempotent — safe even if --bridge wasn't
	// passed or Init() failed.
	ATBridge::Shutdown(g_sim);
#endif

	g_sim.GetGTIA().SetVideoOutput(nullptr);

	// Release mouse capture before shutdown
	ATUIReleaseMouse();

	// Save window placement before shutdown
	ATSaveWindowPlacement(g_pWindow);

	// Save settings before shutdown (must happen before joystick manager
	// is destroyed — ATSettingsExchangeInput reads joystick transforms
	// via g_sim.GetJoystickManager()).  ATPersistAllForSuspend wraps the
	// four-step save (ATSaveSettings → game library cache →
	// VDSaveFilespecSystemData → ATRegistryFlushToDisk) in per-step
	// try/catch, so a failure in one step does not skip the others.
	// Using the same helper as the TERMINATING path keeps clean exit
	// and forced-shutdown behaviour in lockstep.
	ATPersistAllForSuspend();

	// Detach and destroy joystick manager before simulator shutdown
	// (must be after ATSaveSettings which reads joystick transforms)
	if (g_pJoystickMgr) {
		g_sim.SetJoystickManager(nullptr);
		g_pJoystickMgr->Shutdown();
		delete g_pJoystickMgr;
		g_pJoystickMgr = nullptr;
	}

	// Shut down compatibility database (matches Windows main.cpp:4059)
	{
		extern void ATCompatShutdown();
		ATCompatShutdown();
	}

	// Shut down emulation error handler before debugger (must unsubscribe
	// from OnDebuggerOpen before debugger is destroyed).
	ATShutdownEmuErrorHandlerSDL3();

	// Shut down debugger before simulator (matches Windows cleanup order)
	extern void ATShutdownDebugger();
	ATShutdownDebugger();
	ATUIDebuggerShutdown();

	// Shut down network sockets before simulator shutdown
	extern void ATSocketPreShutdown();
	ATSocketPreShutdown();

	g_sim.Shutdown();

	// Final socket cleanup after simulator is gone
	extern void ATSocketShutdown();
	ATSocketShutdown();

	ATUIShutdownProgressSDL3();
	ATTouchControls_Shutdown();

#ifdef ALTIRRA_NETPLAY_ENABLED
	// Best-effort: send a clean Bye to the peer and free the
	// coordinator.  Safe to call with no active session.
	ATNetplayGlue::Shutdown();
#endif

	GameBrowser_Shutdown();
	ATTestModeShutdown();
	ATMacMenuBarShutdown();
	ATUIShutdown();

	delete g_pDisplay;
	delete g_pBackend;  // destroys GL context or SDL_Renderer internally
	g_pBackend = nullptr;
	if (g_pRenderer) {
		SDL_DestroyRenderer(g_pRenderer);
		g_pRenderer = nullptr;
	}
	SDL_DestroyWindow(g_pWindow);
	g_pWindow = nullptr;  // avoid dangling pointer if anything below throws
	SDL_Quit();
	return 0;

	} catch (const MyError &e) {
		ATReportFatal(phase, e.c_str());
		return 1;
	} catch (const std::exception &e) {
		ATReportFatal(phase, e.what());
		return 1;
	} catch (...) {
		ATReportFatal(phase, "unknown exception");
		return 1;
	}
}
