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
#include <at/atcore/media.h>
#include <at/atio/image.h>

#include "display_sdl3_impl.h"
#include "display_backend.h"
#include "display_backend_gl33.h"
#include "display_backend_sdl.h"
#include "gl_funcs.h"
#include "input_sdl3.h"
#include "touch_controls.h"
#include "ui_mobile.h"
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

ATSimulator g_sim;
extern ATUIKeyboardOptions g_kbdOpts;
VDVideoDisplaySDL3 *g_pDisplay = nullptr;
SDL_Window   *g_pWindow   = nullptr;  // non-static: accessed by console_stubs.cpp for fullscreen exit
static SDL_Renderer *g_pRenderer = nullptr;
static IDisplayBackend *g_pBackend = nullptr;
static IATJoystickManager *g_pJoystickMgr = nullptr;
static bool g_running = true;
static bool g_winActive = true;
ATUIState g_uiState;
#ifdef ALTIRRA_MOBILE
ATMobileUIState g_mobileState;
#endif

// Forward declaration — defined in window placement section below
void ATUpdateWindowedGeometry(SDL_Window *window);

// =========================================================================
// Frame pacing — matches Windows main.cpp timing architecture
// =========================================================================

// Frame period tables — mirrors Windows ATUIUpdateSpeedTiming() kPeriods[3][3].
// Rows: Hardware / Broadcast / Integral.  Cols: NTSC / PAL / SECAM.
static constexpr double kMasterClocks[3] = {
	kATMasterClock_NTSC,
	kATMasterClock_PAL,
	kATMasterClock_SECAM,
};

static constexpr double kFramePeriods[3][3] = {
	{ 1.0 / kATFrameRate_NTSC, 1.0 / kATFrameRate_PAL, 1.0 / kATFrameRate_SECAM },
	{ 1.0 / 59.9400,           1.0 / 50.0000,          1.0 / 50.0 },
	{ 1.0 / 60.0000,           1.0 / 50.0000,          1.0 / 50.0 },
};

struct FramePacer {
	uint64_t perfFreq;          // SDL_GetPerformanceFrequency()
	uint64_t lastFrameTime;     // perf counter at last frame presentation
	double   targetSecsPerFrame;// seconds per emulated frame
	int64_t  errorAccum;        // timing error in perf counter ticks (positive = ahead)

	// Frame timing telemetry — independent of ImGui::GetIO().Framerate.
	uint32_t frameCount;        // frames since last telemetry reset
	uint64_t telemetryStart;    // perf counter at last telemetry reset
	float    measuredFPS;       // last measured FPS (updated once per second)

	void Init() {
		perfFreq = SDL_GetPerformanceFrequency();
		lastFrameTime = SDL_GetPerformanceCounter();
		errorAccum = 0;
		targetSecsPerFrame = kFramePeriods[0][0]; // NTSC hardware rate
		frameCount = 0;
		telemetryStart = lastFrameTime;
		measuredFPS = 0.0f;
	}

	void UpdateRate(double fps) {
		targetSecsPerFrame = 1.0 / fps;
	}

	// Called after a frame is complete.  Sleeps to maintain correct rate.
	void WaitForNextFrame() {
		uint64_t now = SDL_GetPerformanceCounter();
		int64_t elapsed = (int64_t)(now - lastFrameTime);
		int64_t targetTicks = (int64_t)(targetSecsPerFrame * (double)perfFreq);

		// Error accumulator: positive = we finished early, need to wait.
		// Mirrors the Windows "error += lastFrameDuration - g_frameTicks"
		// logic, but with inverted sign (they track lateness, we track
		// earliness).
		errorAccum += targetTicks - elapsed;

		// Clamp error to ±2 frames to prevent runaway drift (matches the
		// Windows g_frameErrorBound = 2 * g_frameTicks).
		int64_t errorBound = 2 * targetTicks;
		if (errorAccum > errorBound || errorAccum < -errorBound)
			errorAccum = 0;

		// If we're ahead of schedule, sleep.
		if (errorAccum > 0) {
			uint64_t waitNs = (uint64_t)((double)errorAccum / (double)perfFreq * 1e9);
			if (waitNs > 1000000) // only bother sleeping > 1ms
				SDL_DelayPrecise(waitNs);
		}

		lastFrameTime = SDL_GetPerformanceCounter();

		// Update telemetry once per second.
		++frameCount;
		uint64_t telemetryElapsed = lastFrameTime - telemetryStart;
		if (telemetryElapsed >= perfFreq) {
			measuredFPS = (float)((double)frameCount * (double)perfFreq
				/ (double)telemetryElapsed);
			frameCount = 0;
			telemetryStart = lastFrameTime;
		}
	}
};

static FramePacer g_pacer;

float ATUIGetMeasuredFPS() {
	return g_pacer.measuredFPS;
}

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
			if (g_pJoystickMgr)
				g_pJoystickMgr->RescanForDevices();
			break;

		case SDL_EVENT_GAMEPAD_REMOVED:
			// RescanForDevices only adds new gamepads.  For removal,
			// the SDL3-specific manager exposes CloseGamepad().
			if (g_pJoystickMgr)
				static_cast<ATJoystickManagerSDL3 *>(g_pJoystickMgr)->CloseGamepad(ev.gdevice.which);
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
			break;
		case SDL_EVENT_WINDOW_MOVED:
			ATUpdateWindowedGeometry(g_pWindow);
			break;

		case SDL_EVENT_WINDOW_FOCUS_GAINED:
			g_winActive = true;
			break;

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
		fprintf(stderr, "[DIAG] RenderAndPresent: debuggerOpen=%d hasTex=%d\n", dbgOpen, hasTex);
	if (!dbgOpen) {
		if (hasTex) {
			g_displayRect = ComputeDisplayRect();
			if (s_diagFrameCount < 5)
				fprintf(stderr, "[DIAG] RenderFrame: rect=(%.1f,%.1f,%.1f,%.1f) tex=(%d,%d)\n",
					g_displayRect.x, g_displayRect.y, g_displayRect.w, g_displayRect.h,
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
// Update frame pacer rate from current video standard
// =========================================================================

// Returns the video standard table index: 0=NTSC, 1=PAL, 2=SECAM.
// Mirrors Windows ATUIUpdateSpeedTiming() tableIndex logic.
static int GetVideoStandardIndex() {
	const auto vstd = g_sim.GetVideoStandard();
	const bool isSECAM = vstd == kATVideoStandard_SECAM;
	const bool hz50 = vstd != kATVideoStandard_NTSC && vstd != kATVideoStandard_PAL60;
	return isSECAM ? 2 : hz50 ? 1 : 0;
}

// Get the display refresh period (seconds) for the window's current display.
// Returns 0 if unavailable.
static double GetDisplayRefreshPeriod() {
	SDL_DisplayID displayID = SDL_GetDisplayForWindow(g_pWindow);
	if (!displayID)
		return 0;

	const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(displayID);
	if (!mode || mode->refresh_rate <= 0.0f)
		return 0;

	// Use precise numerator/denominator if available, otherwise float.
	if (mode->refresh_rate_numerator > 0 && mode->refresh_rate_denominator > 0)
		return (double)mode->refresh_rate_denominator / (double)mode->refresh_rate_numerator;

	return 1.0 / (double)mode->refresh_rate;
}

// Update frame pacing and audio resampling to match current video standard,
// frame rate mode, and speed settings.
// Ported from Windows ATUIUpdateSpeedTiming() (main.cpp lines 491-553).
static void UpdatePacerRate() {
	const int tableIndex = GetVideoStandardIndex();
	const ATFrameRateMode frameRateMode = ATUIGetFrameRateMode();
	const double rawSecsPerFrame = kFramePeriods[frameRateMode][tableIndex];

	// Compute adjusted cycles per second for audio resampling.
	// When frame rate mode changes the effective frame period (e.g. Broadcast
	// 59.94 vs Hardware 59.9227), the audio engine must resample at a matching
	// rate so pitch stays correct.
	const double cyclesPerSecond = kMasterClocks[tableIndex]
		* kFramePeriods[0][tableIndex] / rawSecsPerFrame;

	// Speed multiplier: modifier + 1.0, with slow motion factor.
	// Windows skips modifier in turbo (audio rate is irrelevant at warp speed).
	double rate = 1.0;
	if (!g_sim.IsTurboModeEnabled()) {
		rate = std::max(0.0, (double)ATUIGetSpeedModifier() + 1.0);
		if (ATUIGetSlowMotion())
			rate *= 0.5; // Windows default: g_ATCVEngineSlowmoScale = 0.5
	}
	rate = std::clamp(rate, 0.01, 100.0);

	// Adaptive VSync: if enabled and display refresh is within 2% of the
	// target frame rate, lock to the display refresh period instead of the
	// emulation period.  This prevents the ~1 frame drop every ~12 seconds
	// that occurs when a 60Hz display runs 59.92Hz NTSC content.
	// Matches Windows ATUIUpdateSpeedTiming() lines 544-547.
	double secsPerFrame = rawSecsPerFrame / rate;

	if (ATUIGetFrameRateVSyncAdaptive()) {
		double refreshPeriod = GetDisplayRefreshPeriod();
		if (refreshPeriod > 0
			&& refreshPeriod > secsPerFrame * 0.98
			&& refreshPeriod < secsPerFrame * 1.02)
		{
			secsPerFrame = refreshPeriod;
		}
	}

	g_pacer.UpdateRate(1.0 / secsPerFrame);

	// Update audio output resampling rate.
	IATAudioOutput *audio = g_sim.GetAudioOutput();
	if (audio)
		audio->SetCyclesPerSecond(cyclesPerSecond, 1.0 / rate);
}

// =========================================================================
// Fullscreen mode helpers
// =========================================================================

// Apply the fullscreen display mode from g_ATOptions before entering
// fullscreen.  Borderless mode (or exclusive with zero dimensions) uses
// SDL3's default desktop-resolution borderless fullscreen.  Exclusive
// mode with a specific resolution sets the closest matching display mode.
static void ATApplyFullscreenMode(SDL_Window *window) {
	if (g_ATOptions.mbFullScreenBorderless ||
		(g_ATOptions.mFullScreenWidth == 0 && g_ATOptions.mFullScreenHeight == 0)) {
		// Borderless fullscreen (desktop resolution, no mode switch)
		SDL_SetWindowFullscreenMode(window, NULL);
	} else {
		// Exclusive fullscreen with a specific resolution
		SDL_DisplayID displayID = SDL_GetDisplayForWindow(window);
		if (displayID) {
			SDL_DisplayMode closest = {};
			if (SDL_GetClosestFullscreenDisplayMode(
					displayID,
					(int)g_ATOptions.mFullScreenWidth,
					(int)g_ATOptions.mFullScreenHeight,
					g_ATOptions.mFullScreenRefreshRate > 0
						? (float)g_ATOptions.mFullScreenRefreshRate : 0.0f,
					false,
					&closest)) {
				SDL_SetWindowFullscreenMode(window, &closest);
			} else {
				fprintf(stderr, "[AltirraSDL] No matching fullscreen mode %ux%u@%u Hz, falling back to borderless\n",
					g_ATOptions.mFullScreenWidth, g_ATOptions.mFullScreenHeight,
					g_ATOptions.mFullScreenRefreshRate);
				SDL_SetWindowFullscreenMode(window, NULL);
			}
		}
	}
}

// Implement ATSetFullscreen(bool) from uiaccessors.h.
// All fullscreen transitions in the SDL3 build must go through this
// function so that borderless vs. exclusive mode is respected.
// Replaces the stub in uiaccessors_stubs.cpp.
void ATSetFullscreen(bool fs) {
	if (!g_pWindow)
		return;

	bool isFS = (SDL_GetWindowFlags(g_pWindow) & SDL_WINDOW_FULLSCREEN) != 0;
	if (isFS == fs)
		return;

	if (fs) {
		ATApplyFullscreenMode(g_pWindow);
		ATUIShowFullscreenNotification();
	}

	SDL_SetWindowFullscreen(g_pWindow, fs);
}

// =========================================================================
// Window placement persistence
// =========================================================================

// Cache the last windowed-mode geometry so we can save it even when
// the window is currently fullscreen (SDL3 doesn't expose the "normal"
// placement the way Win32 GetWindowPlacement does).
static int s_lastWindowedX = 0;
static int s_lastWindowedY = 0;
static int s_lastWindowedW = 0;
static int s_lastWindowedH = 0;

void ATUpdateWindowedGeometry(SDL_Window *window) {
	if ((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) == 0) {
		SDL_GetWindowPosition(window, &s_lastWindowedX, &s_lastWindowedY);
		SDL_GetWindowSize(window, &s_lastWindowedW, &s_lastWindowedH);
	}
}

static void ATSaveWindowPlacement(SDL_Window *window) {
	// Capture current windowed geometry one last time before saving
	ATUpdateWindowedGeometry(window);

	VDRegistryAppKey key("Window Placement", true);

	bool fullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
	key.setBool("Fullscreen", fullscreen);

	if (s_lastWindowedW > 0 && s_lastWindowedH > 0) {
		key.setInt("X", s_lastWindowedX);
		key.setInt("Y", s_lastWindowedY);
		key.setInt("Width", s_lastWindowedW);
		key.setInt("Height", s_lastWindowedH);
	}
}

static void ATRestoreWindowPlacement(SDL_Window *window) {
	VDRegistryAppKey key("Window Placement", false);

	int w = key.getInt("Width", 0);
	int h = key.getInt("Height", 0);

	if (w > 0 && h > 0) {
		SDL_SetWindowSize(window, w, h);

		int x = key.getInt("X", SDL_WINDOWPOS_CENTERED);
		int y = key.getInt("Y", SDL_WINDOWPOS_CENTERED);
		SDL_SetWindowPosition(window, x, y);
	}

	// Seed the cached windowed geometry before entering fullscreen
	ATUpdateWindowedGeometry(window);

	if (key.getBool("Fullscreen", false)) {
		ATApplyFullscreenMode(window);
		SDL_SetWindowFullscreen(window, true);
		ATUIShowFullscreenNotification();
	}
}

// =========================================================================
// Main
// =========================================================================

int main(int argc, char *argv[]) {
	fprintf(stderr, "[AltirraSDL] Starting...\n");

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

	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return 1;
	}

	VDRegistryAppKey::setDefaultKey("AltirraSDL");

	// Load persisted settings from ~/.config/altirra/settings.ini
	extern void ATRegistryLoadFromDisk();
	ATRegistryLoadFromDisk();

	// Capture whether the registry had any data before anything writes to it.
	// Used later for first-run detection (matches Windows main.cpp:3371).
	const bool registryHadAnything = VDRegistryAppKey("", false).isReady();

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
				fprintf(stderr, "[AltirraSDL] OpenGL 3.3 context created successfully\n");
			} else {
				fprintf(stderr, "[AltirraSDL] GL function loading failed, falling back to SDL_Renderer\n");
				SDL_GL_DestroyContext(glContext);
				glContext = nullptr;
			}
		} else {
			fprintf(stderr, "[AltirraSDL] GL context creation failed: %s\n", SDL_GetError());
		}
	}

	// If GL failed, recreate window without OPENGL flag for SDL_Renderer
	if (!useGL) {
		if (g_pWindow) SDL_DestroyWindow(g_pWindow);
		g_pWindow = SDL_CreateWindow("AltirraSDL", kDefaultWidth, kDefaultHeight, SDL_WINDOW_RESIZABLE);
		if (!g_pWindow) { fprintf(stderr, "CreateWindow: %s\n", SDL_GetError()); SDL_Quit(); return 1; }
	}

	// Restore saved window size, position, and fullscreen state
	ATRestoreWindowPlacement(g_pWindow);

	// Create the display backend
	if (useGL) {
		g_pBackend = new DisplayBackendGL33(g_pWindow, glContext);
		SDL_GL_SetSwapInterval(1);  // VSync
	} else {
		g_pRenderer = SDL_CreateRenderer(g_pWindow, nullptr);
		if (!g_pRenderer) { fprintf(stderr, "CreateRenderer: %s\n", SDL_GetError()); SDL_DestroyWindow(g_pWindow); SDL_Quit(); return 1; }
		SDL_SetRenderVSync(g_pRenderer, 1);
		g_pBackend = new DisplayBackendSDLRenderer(g_pWindow, g_pRenderer);
	}

	if (!ATUIInit(g_pWindow, g_pBackend)) {
		delete g_pBackend; g_pBackend = nullptr;
		if (g_pRenderer) SDL_DestroyRenderer(g_pRenderer);
		SDL_DestroyWindow(g_pWindow);
		SDL_Quit();
		return 1;
	}

	// Register ImGui progress handler (replaces Windows ATUIInitProgressDialog)
	ATUIInitProgressSDL3();

	// Initialize test mode automation (no-op if --test-mode not passed)
	if (!ATTestModeInit()) {
		fprintf(stderr, "[AltirraSDL] Failed to initialize test mode\n");
		// Non-fatal — continue without test mode
		g_testModeEnabled = false;
	}

	// Give the mouse capture system access to the window
	ATUISetMouseCaptureWindow(g_pWindow);

	g_pDisplay = new VDVideoDisplaySDL3(g_pRenderer, kDefaultWidth, kDefaultHeight);

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

	fprintf(stderr, "[AltirraSDL] Initializing simulator...\n");
	g_sim.Init();
	g_sim.SetRandomSeed(rand() ^ (rand() << 15));
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

	// Query display content scale for DPI-aware touch control sizing.
	{
		SDL_DisplayID displayID = SDL_GetDisplayForWindow(g_pWindow);
		float cs = SDL_GetDisplayContentScale(displayID);
		if (cs < 1.0f) cs = 1.0f;
		if (cs > 4.0f) cs = 4.0f;
		g_mobileState.layoutConfig.contentScale = cs;
		fprintf(stderr, "[AltirraSDL] Touch controls content scale: %.2f\n", cs);
	}
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
	ATSettingsLoadLastProfile((ATSettingsCategory)(
		kATSettingsCategory_Hardware
		| kATSettingsCategory_Firmware
		| kATSettingsCategory_Acceleration
		| kATSettingsCategory_Debugging
		| kATSettingsCategory_View
		| kATSettingsCategory_Color
		| kATSettingsCategory_Sound
		| kATSettingsCategory_Boot
		| kATSettingsCategory_Environment
		| kATSettingsCategory_Speed
		| kATSettingsCategory_StartupConfig
		| kATSettingsCategory_FullScreen
		| kATSettingsCategory_Input
		| kATSettingsCategory_InputMaps
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
				g_uiState.showSetupWizard = true;
			}
		}
	}

	g_pacer.Init();
	UpdatePacerRate();

	// Present once immediately so compositors show the window.
	RenderAndPresent();

	fprintf(stderr, "[AltirraSDL] Entering main loop\n");

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

	while (g_running) {
		HandleEvents();
		if (!g_running) break;

		// Process test mode commands from external agent (no-op if not in test mode)
		ATTestModePollCommands(g_sim, g_uiState);

		// Process deferred file dialog results on main thread
		ATUIPollDeferredActions();

		// Tick the debugger engine (process queued commands)
		ATUIDebuggerTick();

		// Pause emulation when window loses focus (if enabled).
		const bool pauseInactive = ATUIGetPauseWhenInactive() && !g_winActive;

		if (pauseInactive) {
			// Window inactive — render for UI responsiveness, sleep.
			RenderAndPresent();
			SDL_Delay(16);
			continue;
		}

		ATSimulator::AdvanceResult result = g_sim.Advance(false);

		// Check if a new frame arrived (GTIA called PostBuffer).
		bool hadFrame = g_pDisplay->IsFramePending();
		g_pDisplay->PrepareFrame();

		const bool turbo = g_sim.IsTurboModeEnabled();

		if (s_diagFrameCount < 5)
			fprintf(stderr, "[DIAG] MainLoop: advance=%d hadFrame=%d running=%d paused=%d\n",
				(int)result, hadFrame, g_sim.IsRunning(), g_sim.IsPaused());

		if (hadFrame) {
			// A frame was uploaded — present it and pace.
			RenderAndPresent();

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
			if (!turbo)
				g_pacer.WaitForNextFrame();
		} else if (result == ATSimulator::kAdvanceResult_Stopped) {
			// Paused/stopped — render for UI, sleep to avoid busy-wait.
			RenderAndPresent();
			SDL_Delay(16);
		}
		// kAdvanceResult_Running with no frame: loop immediately.
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
		ATSaveSettings((ATSettingsCategory)(
			kATSettingsCategory_Hardware
			| kATSettingsCategory_Firmware
			| kATSettingsCategory_Acceleration
			| kATSettingsCategory_Debugging
			| kATSettingsCategory_View
			| kATSettingsCategory_Color
			| kATSettingsCategory_Sound
			| kATSettingsCategory_Boot
			| kATSettingsCategory_Environment
			| kATSettingsCategory_Speed
			| kATSettingsCategory_StartupConfig
			| kATSettingsCategory_FullScreen
			| kATSettingsCategory_Input
			| kATSettingsCategory_InputMaps
		));
		ATRegistryFlushToDisk();
	} catch (...) {
		fprintf(stderr, "[AltirraSDL] Warning: failed to save settings on exit\n");
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
	SDL_Quit();
	return 0;
}
