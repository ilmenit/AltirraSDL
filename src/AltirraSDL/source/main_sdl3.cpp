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
#include "input_sdl3.h"
#include "options.h"
#include "ui_main.h"
#include "ui_debugger.h"

#include "simulator.h"
#include "uikeyboard.h"
#include <at/ataudio/audiooutput.h>
#include "uiaccessors.h"
#include "inputmanager.h"
#include "inputdefs.h"
#include "gtia.h"
#include "joystick.h"
#include "joystick_sdl3.h"
#include "firmwaremanager.h"
#include "settings.h"

ATSimulator g_sim;
static VDVideoDisplaySDL3 *g_pDisplay = nullptr;
SDL_Window   *g_pWindow   = nullptr;  // non-static: accessed by console_stubs.cpp for fullscreen exit
static SDL_Renderer *g_pRenderer = nullptr;
static IATJoystickManager *g_pJoystickMgr = nullptr;
static bool g_running = true;
static bool g_winActive = true;
static ATUIState g_uiState;

// Forward declaration — defined in window placement section below
void ATUpdateWindowedGeometry(SDL_Window *window);

// =========================================================================
// Frame pacing — matches Windows main.cpp timing architecture
// =========================================================================

// Atari frame rates (from main.cpp ATUIUpdateSpeedTiming):
//   NTSC:  262 scanlines * 114 clocks @ 1.7897725 MHz = ~59.9227 Hz
//   PAL:   312 scanlines * 114 clocks @ 1.773447  MHz = ~49.8607 Hz
//   SECAM: 312 scanlines * 114 clocks @ 1.7815    MHz = ~50.0818 Hz
static constexpr double kFrameRate_NTSC  = 59.9227;
static constexpr double kFrameRate_PAL   = 49.8607;
static constexpr double kFrameRate_SECAM = 50.0818;

struct FramePacer {
	uint64_t perfFreq;          // SDL_GetPerformanceFrequency()
	uint64_t lastFrameTime;     // perf counter at last frame presentation
	double   targetSecsPerFrame;// seconds per emulated frame
	int64_t  errorAccum;        // timing error in perf counter ticks (positive = ahead)

	void Init() {
		perfFreq = SDL_GetPerformanceFrequency();
		lastFrameTime = SDL_GetPerformanceCounter();
		errorAccum = 0;
		UpdateRate(kFrameRate_NTSC);
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
	}
};

static FramePacer g_pacer;

// =========================================================================
// Event handling
// =========================================================================

static bool g_prevImGuiCapture = false;
static bool g_prevImGuiMouseCapture = false;

// Update all mouse position inputs (pad, beam, virtual stick) from pixel coords.
// Matches Windows ATUIVideoDisplayWindow::UpdateMousePosition().
static void UpdateMousePosition(ATInputManager *im, float mx, float my) {
	int winW, winH;
	SDL_GetWindowSize(g_pWindow, &winW, &winH);
	if (winW <= 1 || winH <= 1)
		return;

	// Pad position: map window area to [-0x10000, +0x10000]
	int padX = (int)((mx / (float)(winW - 1)) * 131072.0f - 0x10000);
	int padY = (int)((my / (float)(winH - 1)) * 131072.0f - 0x10000);
	im->SetMousePadPos(padX, padY);

	// Beam position: map pixel to ANTIC beam coordinates, then normalize.
	{
		ATGTIAEmulator& gtia = g_sim.GetGTIA();
		const vdrect32 scanArea(gtia.GetFrameScanArea());

		float hcyc = (float)scanArea.left
			+ (mx + 0.5f) * (float)scanArea.width() / (float)winW - 0.5f;
		float vcyc = (float)scanArea.top
			+ (my + 0.5f) * (float)scanArea.height() / (float)winH - 0.5f;

		im->SetMouseBeamPos(
			(int)((hcyc - 128.0f) * (65536.0f / 94.0f)),
			(int)((vcyc - 128.0f) * (65536.0f / 188.0f)));
	}

	// Virtual stick: normalized [-1, +1] with aspect ratio correction
	{
		float sizeX = (float)(winW - 1);
		float sizeY = (float)(winH - 1);
		float normX = mx / sizeX * 2.0f - 1.0f;
		float normY = my / sizeY * 2.0f - 1.0f;

		if (sizeX > sizeY)
			normX *= sizeX / sizeY;
		else if (sizeY > sizeX)
			normY *= sizeY / sizeX;

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
			if (!ATUIWantCaptureKeyboard()) {
				// Debugger shortcuts take precedence when debugger is active
				if (ATUIDebuggerIsOpen()) {
					bool handled = true;
					if (ev.key.key == SDLK_F5)
						ATUIDebuggerRunStop();
					else if (ev.key.key == SDLK_F10)
						ATUIDebuggerStepOver();
					else if (ev.key.key == SDLK_F11 && (ev.key.mod & SDL_KMOD_SHIFT))
						ATUIDebuggerStepOut();
					else if (ev.key.key == SDLK_F11)
						ATUIDebuggerStepInto();
					else
						handled = false;

					if (handled)
						break;
				}

				if (ev.key.key == SDLK_F5 && (ev.key.mod & SDL_KMOD_SHIFT)) {
					g_sim.ColdReset();
					g_sim.Resume();
					extern ATUIKeyboardOptions g_kbdOpts;
					if (!g_kbdOpts.mbAllowShiftOnColdReset)
						g_sim.GetPokey().SetShiftKeyState(false, true);
				} else if (ev.key.key == SDLK_F5 && !(ev.key.mod & SDL_KMOD_SHIFT)) {
					g_sim.WarmReset();
					g_sim.Resume();
				} else if (ev.key.key == SDLK_F9) {
					if (g_sim.IsPaused())
						g_sim.Resume();
					else
						g_sim.Pause();
				} else if (ev.key.key == SDLK_F7)
					ATUIQuickLoadState();
				else if (ev.key.key == SDLK_F8)
					ATUIQuickSaveState();
				else if (ev.key.key == SDLK_RETURN &&
					(ev.key.mod & SDL_KMOD_ALT)) {
					bool fs = (SDL_GetWindowFlags(g_pWindow) & SDL_WINDOW_FULLSCREEN) != 0;
					SDL_SetWindowFullscreen(g_pWindow, !fs);
				} else
					ATInputSDL3_HandleKeyDown(ev.key);
			}
			break;

		case SDL_EVENT_KEY_UP:
			if (!ATUIWantCaptureKeyboard()) {
				// Don't forward key-up for keys consumed as hotkeys
				// (F5, Shift+F5, F7, F8, Alt+Enter) to avoid spurious
				// ATInputManager release events.
				if (ev.key.key != SDLK_F5 && ev.key.key != SDLK_F7 &&
					ev.key.key != SDLK_F8 && ev.key.key != SDLK_F9)
					ATInputSDL3_HandleKeyUp(ev.key);
			}
			break;

		case SDL_EVENT_MOUSE_MOTION:
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

		case SDL_EVENT_DROP_FILE: {
			const char *file = ev.drop.data;
			if (file) {
				// If firmware manager is open, route drop there (matches Windows OnDropFiles)
				if (!ATUIFirmwareManagerHandleDrop(file)) {
					// Otherwise boot image (matches Windows drag-and-drop behavior)
					ATUIPushDeferred(kATDeferred_BootImage, file);
				}
			}
			break;
		}

		case SDL_EVENT_WINDOW_RESIZED:
		case SDL_EVENT_WINDOW_MOVED:
			ATUpdateWindowedGeometry(g_pWindow);
			break;

		case SDL_EVENT_WINDOW_FOCUS_GAINED:
			g_winActive = true;
			break;

		case SDL_EVENT_WINDOW_FOCUS_LOST:
			g_winActive = false;
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
			break;

		default:
			break;
		}
	}
}

// =========================================================================
// Rendering
// =========================================================================

static void RenderAndPresent() {
	SDL_SetRenderDrawColor(g_pRenderer, 0, 0, 0, 255);
	SDL_RenderClear(g_pRenderer);

	// Draw emulator frame texture with blending disabled.
	// ImGui's SDLRenderer3 backend leaves SDL_BLENDMODE_BLEND active.
	SDL_Texture *emuTex = g_pDisplay->GetTexture();
	if (emuTex) {
		SDL_SetTextureBlendMode(emuTex, SDL_BLENDMODE_NONE);
		SDL_RenderTexture(g_pRenderer, emuTex, nullptr, nullptr);
	}

	// Draw ImGui UI on top
	ATUIRenderFrame(g_sim, *g_pDisplay, g_pRenderer, g_uiState);

	SDL_RenderPresent(g_pRenderer);
}

// =========================================================================
// Update frame pacer rate from current video standard
// =========================================================================

static double GetBaseFrameRate() {
	switch (g_sim.GetVideoStandard()) {
	case kATVideoStandard_PAL:
	case kATVideoStandard_NTSC50:	// NTSC color at 50Hz → PAL timing
		return kFrameRate_PAL;
	case kATVideoStandard_SECAM:
		return kFrameRate_SECAM;
	case kATVideoStandard_PAL60:	// PAL color at 60Hz → NTSC timing
	default:
		return kFrameRate_NTSC;
	}
}

static void UpdatePacerRate() {
	double rate = GetBaseFrameRate();

	// Apply speed modifier: 0 = 1x, 1 = 2x, 3 = 4x, 7 = 8x
	float spd = ATUIGetSpeedModifier();
	double speedFactor = (double)spd + 1.0;
	if (ATUIGetSlowMotion())
		speedFactor *= 0.25;

	if (speedFactor < 0.01) speedFactor = 0.01;
	if (speedFactor > 100.0) speedFactor = 100.0;

	g_pacer.UpdateRate(rate * speedFactor);

	// Update audio output resampling rate to match speed.
	// This mirrors Windows main.cpp line 537.
	IATAudioOutput *audio = g_sim.GetAudioOutput();
	if (audio) {
		double cyclesPerSecond = g_sim.GetScheduler()->GetRate().asDouble();
		if (cyclesPerSecond <= 0)
			cyclesPerSecond = 1789772.5;

		audio->SetCyclesPerSecond(cyclesPerSecond, 1.0 / speedFactor);
	}
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

	if (key.getBool("Fullscreen", false))
		SDL_SetWindowFullscreen(window, true);
}

// =========================================================================
// Main
// =========================================================================

int main(int argc, char *argv[]) {
	fprintf(stderr, "[AltirraSDL] Starting...\n");

	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return 1;
	}

	VDRegistryAppKey::setDefaultKey("AltirraSDL");

	// Load persisted settings from ~/.config/altirra/settings.ini
	extern void ATRegistryLoadFromDisk();
	ATRegistryLoadFromDisk();

	const int kDefaultWidth = 1280;
	const int kDefaultHeight = 720;
	g_pWindow = SDL_CreateWindow("AltirraSDL", kDefaultWidth, kDefaultHeight, SDL_WINDOW_RESIZABLE);
	if (!g_pWindow) { fprintf(stderr, "CreateWindow: %s\n", SDL_GetError()); SDL_Quit(); return 1; }

	// Restore saved window size, position, and fullscreen state
	ATRestoreWindowPlacement(g_pWindow);

	g_pRenderer = SDL_CreateRenderer(g_pWindow, nullptr);
	if (!g_pRenderer) { fprintf(stderr, "CreateRenderer: %s\n", SDL_GetError()); SDL_DestroyWindow(g_pWindow); SDL_Quit(); return 1; }

	SDL_SetRenderVSync(g_pRenderer, 1);

	if (!ATUIInit(g_pWindow, g_pRenderer)) {
		SDL_DestroyRenderer(g_pRenderer);
		SDL_DestroyWindow(g_pWindow);
		SDL_Quit();
		return 1;
	}

	// Give the mouse capture system access to the window
	ATUISetMouseCaptureWindow(g_pWindow);

	g_pDisplay = new VDVideoDisplaySDL3(g_pRenderer, kDefaultWidth, kDefaultHeight);

	fprintf(stderr, "[AltirraSDL] Initializing simulator...\n");
	g_sim.Init();
	g_sim.LoadROMs();

	g_sim.GetGTIA().SetVideoOutput(g_pDisplay);

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

	// Register device extended commands (copy/paste, explore disk, mount VHD, etc.)
	extern void ATRegisterDeviceXCmds(ATDeviceManager& dm);
	ATRegisterDeviceXCmds(*g_sim.GetDeviceManager());

	// Load emulator settings using the same code path as Windows.
	// On first run (no config file), VDRegistryKey returns defaults for all
	// keys, which matches a fresh Windows install.
	// Initialize network sockets (POSIX sockets, lookup worker, socket worker).
	// Must be called before ATLoadSettings as some device init may need sockets.
	extern bool ATSocketInit();
	ATSocketInit();

	ATLoadSettings((ATSettingsCategory)(
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

	if (argc > 1) {
		// Push as deferred boot action so it goes through the full retry loop
		// with hardware mode auto-switching, BASIC conflict detection, etc.
		ATUIPushDeferred(kATDeferred_BootImage, argv[1]);
	} else {
		g_sim.ColdReset();
		g_sim.Resume();
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
		// We must present whenever a frame is ready, regardless of
		// Advance() return value.  GTIA's PostBuffer and BeginFrame
		// can both happen inside a single Advance() call — the next
		// frame may already be in progress when Advance() returns
		// kAdvanceResult_Running.  The original main loop called
		// Present() on every Advance() result for this reason.
		bool hadFrame = g_pDisplay->IsFramePending();
		g_pDisplay->PrepareFrame();

		const bool turbo = g_sim.IsTurboModeEnabled();

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

	// Detach and destroy joystick manager before simulator shutdown
	if (g_pJoystickMgr) {
		g_sim.SetJoystickManager(nullptr);
		g_pJoystickMgr->Shutdown();
		delete g_pJoystickMgr;
		g_pJoystickMgr = nullptr;
	}

	// Release mouse capture before shutdown
	ATUIReleaseMouse();

	// Save window placement before shutdown
	ATSaveWindowPlacement(g_pWindow);

	// Save settings before shutdown
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

	ATUIShutdown();

	delete g_pDisplay;
	SDL_DestroyRenderer(g_pRenderer);
	SDL_DestroyWindow(g_pWindow);
	SDL_Quit();
	return 0;
}
