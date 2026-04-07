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
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include <stdio.h>

#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/registry.h>
#include <vd2/system/error.h>
#include <exception>
#include <at/atcore/media.h>
#include <at/atio/image.h>

#include "crash_report.h"
#include <imgui.h>
#include "display_sdl3_impl.h"
#include "display_backend.h"
#include "display_backend_gl33.h"
#include "display_backend_sdl.h"
#include "gl_funcs.h"
#include "input_sdl3.h"
#include "touch_controls.h"
#include "ui_mobile.h"
#ifdef ALTIRRA_MOBILE
#include "mobile_gamepad.h"
#endif
#include "options.h"
#include "ui_main.h"
#include "ui_debugger.h"
#include "ui_testmode.h"
#include "ui_textselection.h"
#include "ui_progress.h"
#include "ui_emuerror.h"

#include "simulator.h"
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
#include <algorithm>
#include <cmath>
#include "logging.h"
#include "app_internal.h"

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

#ifdef ALTIRRA_MOBILE
ATMobileUIState g_mobileState;
#endif

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
static SDL_FRect g_displayRect = {0, 0, 0, 0};

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
		// Route touch events to mobile controls before ImGui processing
#ifdef ALTIRRA_MOBILE
		// Reserved gamepad buttons (Start, Back) drive the UI on
		// mobile and never reach the emulator.  Must run before
		// the touch handler so a gamepad press isn't shadowed.
		if (ATMobileGamepad_HandleEvent(ev, g_sim, g_mobileState))
			continue;
		if (ATMobileUI_HandleEvent(ev, g_mobileState))
			continue;
#endif

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
				// Accelerator table dispatch (matches Windows ATUIActivateVirtKeyMapping)
				// Priority: Global → Debugger → Display
				bool handled = ATUISDLActivateAccelKey(ev.key, false, kATUIAccelContext_Global);

				if (!handled && ATUIDebuggerIsOpen())
					handled = ATUISDLActivateAccelKey(ev.key, false, kATUIAccelContext_Debugger);

				if (!handled && !ATUIWantCaptureKeyboard())
					handled = ATUISDLActivateAccelKey(ev.key, false, kATUIAccelContext_Display);

				if (!handled && !ATUIWantCaptureKeyboard())
					ATInputSDL3_HandleKeyDown(ev.key);
			}
			break;

		case SDL_EVENT_KEY_UP:
			// Dispatch key-up through accel tables (handles PulseWarpOff on F1 release).
			// Check all contexts symmetrically with key-down dispatch.
			ATUISDLActivateAccelKey(ev.key, true, kATUIAccelContext_Global);
			if (ATUIDebuggerIsOpen())
				ATUISDLActivateAccelKey(ev.key, true, kATUIAccelContext_Debugger);
			if (!ATUIWantCaptureKeyboard())
				ATUISDLActivateAccelKey(ev.key, true, kATUIAccelContext_Display);

			if (!ATUIWantCaptureKeyboard()) {
				// Suppress emulator key-up for keys bound in accel tables
				// (replaces hardcoded F1/F5/F7/.../F12 list)
				uint32 upVk = SDLScancodeToVK(ev.key.scancode);
				if (upVk == kATInputCode_None || !ATUIFindBoundKey(upVk, ev.key.mod, ev.key.scancode))
					ATInputSDL3_HandleKeyUp(ev.key);
			}
			break;

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
				if (ev.button.button == SDL_BUTTON_LEFT &&
					ATUIGetMouseAutoCapture() &&
					!ATUIIsMouseCaptured()) {
					ATUICaptureMouse();
					break;
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
#ifdef ALTIRRA_MOBILE
			// Snapshot the current emulator state so if Android kills
			// us while backgrounded (low memory, incoming call swipe,
			// user swipe from recents) we can resume on next launch.
			// Also flush settings so any pending config changes land
			// on disk before we risk being killed.
			ATMobileUI_SaveSuspendState(g_sim, g_mobileState);
			try {
				extern void ATRegistryFlushToDisk();
				ATRegistryFlushToDisk();
			} catch (...) {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Altirra: registry flush failed on background");
			}
#endif
			break;

		case SDL_EVENT_DID_ENTER_BACKGROUND:
			SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
				"Altirra: DID_ENTER_BACKGROUND — suspending render/sim");
			g_appSuspended = true;
#ifdef ALTIRRA_MOBILE
			// Stop pushing audio to the device while invisible — the
			// AudioOutput SDL3 backend queues chunks and will keep
			// filling them if we don't mute.
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
#ifdef ALTIRRA_MOBILE
			// Re-enable audio output (unless the user explicitly
			// muted via the hamburger menu).
			if (IATAudioOutput *ao = g_sim.GetAudioOutput())
				ao->SetMute(g_mobileState.audioMuted);
			// Re-query safe insets in case rotation happened while
			// suspended.
			ATAndroid_InvalidateSafeInsets();
			// Refresh the file browser when returning from Settings
			// (after a possible "All files access" grant).
			ATMobileUI_InvalidateFileBrowser();
#endif
			break;

		case SDL_EVENT_TERMINATING:
			// Last chance before the OS kills the process.  Snapshot
			// the emulator state (mobile only) and flush settings,
			// both inside try/catch, then request a clean exit.
			SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
				"Altirra: TERMINATING — flushing settings");
#ifdef ALTIRRA_MOBILE
			ATMobileUI_SaveSuspendState(g_sim, g_mobileState);
#endif
			try {
				extern void ATRegistryFlushToDisk();
				ATRegistryFlushToDisk();
			} catch (...) {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Altirra: settings flush failed on terminate");
			}
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
	if (g_pDisplay->HasScreenFX()) {
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

	phase = "SDL_Init";
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) {
		ATReportFatal(phase, SDL_GetError());
		return 1;
	}

#ifdef ALTIRRA_MOBILE
	// Keep the screen on while the emulator is running.  On Android this
	// maps to FLAG_KEEP_SCREEN_ON; without it the device auto-locks
	// during gameplay because touches on the SDL window do not count as
	// "user interaction" to the power manager.  Desktop is unaffected
	// (this gate is mobile-only anyway).
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

	// Capture whether the registry had any data before anything writes to it.
	// Used later for first-run detection (matches Windows main.cpp:3371).
	const bool registryHadAnything = VDRegistryAppKey("", false).isReady();

	phase = "window/GL";

	const int kDefaultWidth = 1280;
	const int kDefaultHeight = 720;

	// Try OpenGL 3.3 first, fall back to SDL_Renderer
	bool useGL = false;
	SDL_GLContext glContext = nullptr;

	// Set GL attributes before window creation
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	g_pWindow = SDL_CreateWindow("AltirraSDL", kDefaultWidth, kDefaultHeight,
		SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
	if (g_pWindow) {
		glContext = SDL_GL_CreateContext(g_pWindow);
		if (glContext) {
			SDL_GL_MakeCurrent(g_pWindow, glContext);
			if (GLLoadFunctions()) {
				useGL = true;
				LOG_INFO("Main", "OpenGL 3.3 context created successfully");
			} else {
				LOG_ERROR("Main", "GL function loading failed, falling back to SDL_Renderer");
				SDL_GL_DestroyContext(glContext);
				glContext = nullptr;
			}
		} else {
			LOG_ERROR("Main", "GL context creation failed: %s", SDL_GetError());
		}
	}

	// If GL failed, recreate window without OPENGL flag for SDL_Renderer
	if (!useGL) {
		if (g_pWindow) SDL_DestroyWindow(g_pWindow);
		g_pWindow = SDL_CreateWindow("AltirraSDL", kDefaultWidth, kDefaultHeight, SDL_WINDOW_RESIZABLE);
		if (!g_pWindow) { LOG_INFO("Main", "CreateWindow: %s", SDL_GetError()); SDL_Quit(); return 1; }
	}

	// Restore saved window size, position, and fullscreen state
	ATRestoreWindowPlacement(g_pWindow);

	// Create the display backend
	if (useGL) {
		g_pBackend = new DisplayBackendGL33(g_pWindow, glContext);
		SDL_GL_SetSwapInterval(1);  // VSync
	} else {
		g_pRenderer = SDL_CreateRenderer(g_pWindow, nullptr);
		if (!g_pRenderer) { LOG_INFO("Main", "CreateRenderer: %s", SDL_GetError()); SDL_DestroyWindow(g_pWindow); SDL_Quit(); return 1; }
		SDL_SetRenderVSync(g_pRenderer, 1);
		g_pBackend = new DisplayBackendSDLRenderer(g_pWindow, g_pRenderer);
	}

	phase = "ImGui init";
	if (!ATUIInit(g_pWindow, g_pBackend)) {
		delete g_pBackend; g_pBackend = nullptr;
		if (g_pRenderer) SDL_DestroyRenderer(g_pRenderer);
		SDL_DestroyWindow(g_pWindow);
		SDL_Quit();
		return 1;
	}

	// Register ImGui progress handler (replaces Windows ATUIInitProgressDialog)
	ATUIInitProgressSDL3();

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
		SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
			"Altirra: display backend = %s, window pixel size = %dx%d",
			useGL ? "OpenGL 3.3" : "SDL_Renderer", pxW, pxH);
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

#ifdef ALTIRRA_MOBILE
	ATMobileUI_Init();
	ATMobileUI_LoadConfig(g_mobileState);
	ATTouchControls_SetHapticEnabled(g_mobileState.layoutConfig.hapticEnabled);

	// Query display content scale for DPI-aware touch control sizing.
	{
		SDL_DisplayID displayID = SDL_GetDisplayForWindow(g_pWindow);
		float cs = SDL_GetDisplayContentScale(displayID);
		if (cs < 1.0f) cs = 1.0f;
		if (cs > 4.0f) cs = 4.0f;
		g_mobileState.layoutConfig.contentScale = cs;
		LOG_INFO("Main", "Touch controls content scale: %.2f", cs);

		// Raise ImGui's mouse drag threshold on touch devices so small
		// finger jitter during a tap doesn't start a drag — scales with
		// display DPI so it's consistent across phones and tablets.
		ImGuiIO &io = ImGui::GetIO();
		io.MouseDragThreshold = 8.0f * cs;
	}

	// Storage permission is requested lazily by the file browser when
	// the user actually opens it — requesting on startup spawns the
	// dialog before the GL surface is stable, which some Android
	// builds handle by putting the activity through onPause/onStop
	// right at the moment we're trying to render the first frame.
#endif

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

#ifdef ALTIRRA_MOBILE
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
		// No boot image on command line — cold reset and start
		g_sim.ColdReset();
		g_sim.Resume();
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
#ifndef ALTIRRA_MOBILE
				// Desktop-only: the setup wizard is a mouse/keyboard dialog
				// and traps a mobile user on first launch.  The mobile
				// frontend handles first-run through its own flow.
				g_uiState.showSetupWizard = true;
#endif
			}
		}
	}

	phase = "main loop";
	g_pacer.Init();
	UpdatePacerRate();

#ifdef ALTIRRA_MOBILE
	// Apply visual effects toggles once, after the simulator is fully
	// initialised and the GTIA is ready to accept param writes.
	ATMobileUI_ApplyVisualEffects(g_mobileState);
	// Apply performance preset so its bundled simulator knobs
	// (FastBoot, Interlace, POKEY nonlinear mix, drive sounds)
	// are set before the first frame.
	ATMobileUI_ApplyPerformancePreset(g_mobileState);

	// If we were killed while backgrounded last session, restore the
	// emulator state the user was in.  Must happen AFTER firmware has
	// loaded and the simulator is fully initialised, and BEFORE the
	// main loop starts so the first frame shows the restored state.
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

	// --- Hiccup detector state.  Catches individual main-loop iterations
	// that exceed the frame budget and emits a per-phase breakdown so we
	// can tell whether the stall came from event processing, emulation,
	// frame preparation, rendering, or the pacer itself.  Rate-limited to
	// one log line per second to avoid flooding under sustained overload. ---
	const uint64_t hiccupPerfFreq = SDL_GetPerformanceFrequency();
	uint64_t hiccupLastLogMs = 0;

	while (g_running) {
		const uint64_t phaseT0 = SDL_GetPerformanceCounter();

		HandleEvents();
		if (!g_running) break;

		// Process test mode commands from external agent (no-op if not in test mode)
		ATTestModePollCommands(g_sim, g_uiState);

		// Process deferred file dialog results on main thread
		ATUIPollDeferredActions();

		// Tick the debugger engine (process queued commands)
		ATUIDebuggerTick();

		const uint64_t phaseT1 = SDL_GetPerformanceCounter();

		// Android/mobile: while backgrounded, do not advance emulation
		// and do not render.  Just keep pumping events so we receive the
		// resume notification.  Longer sleep to stay off the CPU.  Must
		// be checked BEFORE pauseInactive so we take the cheap path and
		// avoid even the no-op RenderAndPresent call.
		if (g_appSuspended) {
			SDL_Delay(100);
			continue;
		}

		// Pause emulation when window loses focus (if enabled).
		const bool pauseInactive = ATUIGetPauseWhenInactive() && !g_winActive;

		if (pauseInactive) {
			// Window inactive — render for UI responsiveness, sleep.
			RenderAndPresent();
			SDL_Delay(16);
			continue;
		}

		// Turbo frame-skip: in turbo mode, drop most frames at the GTIA
		// framebuffer-allocation level so GTIA skips artifacting / palette
		// correction / framebuffer writes for ~15 of every 16 frames.
		// Matches Windows main.cpp:3180 behaviour with the default
		// g_ATCVEngineTurboFPSDivisor=16.  Significant CPU saving in
		// turbo mode on lower-end Linux/macOS/Android, with no effect
		// at normal speed (turbo=false → dropFrame=false always).
		const bool turbo = g_sim.IsTurboModeEnabled();
		static uint32 s_turboFrameCounter = 0;
		constexpr uint32 kTurboFPSDivisor = 16;
		const bool dropFrame = turbo && ((++s_turboFrameCounter) % kTurboFPSDivisor) != 0;

		ATSimulator::AdvanceResult result = g_sim.Advance(dropFrame);

		const uint64_t phaseT2 = SDL_GetPerformanceCounter();

		// Check if a new frame arrived (GTIA called PostBuffer).
		bool hadFrame = g_pDisplay->IsFramePending();
		g_pDisplay->PrepareFrame();

		const uint64_t phaseT3 = SDL_GetPerformanceCounter();

		// (turbo is declared above for the dropFrame computation.)

		// Toggle GL swap interval when turbo state changes.
		// With vsync on (interval=1), SDL_GL_SwapWindow blocks at the display
		// refresh rate even in turbo mode — capping warp speed to ~60fps.
		// Disabling it while turbo is active lets the GPU swap as fast as it can.
		static bool s_lastTurbo = false;
		if (turbo != s_lastTurbo) {
			s_lastTurbo = turbo;
			SDL_GL_SetSwapInterval(turbo ? 0 : 1);
		}

		bool didRender = false;
		if (hadFrame) {
			// A frame was uploaded — present it and pace.
			RenderAndPresent();
			didRender = true;

			// Sync pacer rate and audio rate with current speed settings.
			// Cheap — just reads a few values and updates if changed.
			UpdatePacerRate();

			// In turbo mode, skip frame pacing to run as fast as possible.
			if (!turbo)
				g_pacer.WaitForNextFrame();
		} else if (result == ATSimulator::kAdvanceResult_WaitingForFrame) {
			// GTIA is blocked but we had no frame to show.
			// Present anyway to keep UI responsive.
			RenderAndPresent();
			didRender = true;
			if (!turbo)
				g_pacer.WaitForNextFrame();
		} else if (result == ATSimulator::kAdvanceResult_Stopped) {
			// Paused/stopped — render for UI, sleep to avoid busy-wait.
			RenderAndPresent();
			didRender = true;
			SDL_Delay(16);
		}
		// kAdvanceResult_Running with no frame: loop immediately.

		// --- Hiccup detection.  Measure total work (events + advance +
		// prepare + render) excluding the pacer sleep, and compare against
		// the frame budget.  If work exceeds budget + 8ms slack, emit a
		// per-phase breakdown so we can identify what stalled.  Rate-limited
		// to 1 line / sec so a sustained overload won't flood stderr.
		//
		// NOTE: "render" includes SDL_GL_SwapWindow which blocks on vsync
		// and can legitimately take up to one display refresh (~16.6ms on
		// 60Hz) — this is normal and accounted for in the threshold.
		const uint64_t phaseT4 = SDL_GetPerformanceCounter();
		const double totalWorkMs  = (double)(phaseT4 - phaseT0) * 1000.0 / (double)hiccupPerfFreq;
		const double targetMs     = g_pacer.targetSecsPerFrame * 1000.0;
		const double hiccupThresh = targetMs + 8.0;
		if (didRender && totalWorkMs > hiccupThresh) {
			const uint64_t nowMs = SDL_GetTicks();
			if (nowMs - hiccupLastLogMs >= 1000) {
				hiccupLastLogMs = nowMs;
				const double evMs  = (double)(phaseT1 - phaseT0) * 1000.0 / (double)hiccupPerfFreq;
				const double adMs  = (double)(phaseT2 - phaseT1) * 1000.0 / (double)hiccupPerfFreq;
				const double prMs  = (double)(phaseT3 - phaseT2) * 1000.0 / (double)hiccupPerfFreq;
				const double rdMs  = (double)(phaseT4 - phaseT3) * 1000.0 / (double)hiccupPerfFreq;
				LOG_INFO("Hiccup",
					"total=%.1fms (target=%.1f) events=%.1f advance=%.1f prepare=%.1f render+present=%.1f",
					totalWorkMs, targetMs, evMs, adMs, prMs, rdMs);
			}
		}
	}

	g_sim.GetGTIA().SetVideoOutput(nullptr);

	// Release mouse capture before shutdown
	ATUIReleaseMouse();

	// Save window placement before shutdown
	ATSaveWindowPlacement(g_pWindow);

	// Save settings before shutdown (must happen before joystick manager
	// is destroyed — ATSettingsExchangeInput reads joystick transforms
	// via g_sim.GetJoystickManager())
	extern void ATRegistryFlushToDisk();
	try {
		// Match Windows main.cpp:4044 — save every registered category
		// except FullScreen.  Covers Devices, MountedImages, and NVRAM.
		ATSaveSettings((ATSettingsCategory)(
			kATSettingsCategory_All & ~kATSettingsCategory_FullScreen
		));
		ATRegistryFlushToDisk();
	} catch (...) {
		LOG_ERROR("Main", "failed to save settings on exit");
	}

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
	ATTestModeShutdown();
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
