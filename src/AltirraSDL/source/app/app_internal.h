//	AltirraSDL - app/ internal header (Phase 3c)
//	Shared between main_sdl3.cpp and the split-out main_*.cpp files.
//	Promotes file-statics that need to cross translation units; the
//	definitions still live in exactly one .cpp.

#pragma once

#include <SDL3/SDL.h>
#include <vd2/system/vdtypes.h>

// -------------------------------------------------------------------------
// Frame pacer (definition in main_pacer.cpp)
// -------------------------------------------------------------------------

struct FramePacer {
	uint64_t perfFreq;
	uint64_t lastFrameTime;
	double   targetSecsPerFrame;
	int64_t  errorAccum;

	uint32_t frameCount;
	uint64_t telemetryStart;
	float    measuredFPS;

	// Telemetry: number of WaitForNextFrame calls in the current
	// 1-second window whose elapsed time exceeded the frame budget
	// (i.e. the main loop couldn't keep up with realtime).  Reset
	// when the window rolls over.  Used by the [Pace] log line.
	uint32_t lateFrameCount;
	int64_t  maxElapsedTicks;

	// Active clock-recovery multiplier applied to targetSecsPerFrame
	// each frame.  1.0 = pure wallclock pacing.  ComputeClockRecovery()
	// nudges this within ±0.5 % based on audio-pipeline-depth error.
	// See the long comment at ComputeClockRecovery in main_pacer.cpp
	// for why we do this (and why it deliberately diverges from
	// Windows Altirra).
	//
	// In-class init so the factor is a safe 1.0 even if
	// WaitForNextFrame() is ever called before Init() (it isn't today,
	// but the other pacer members are uninitialised on purpose to match
	// Windows; adding a defensive init only here preserves that shape
	// while removing the one footgun that would degenerate into
	// targetTicks == 0 and spin the main loop).
	double   clockRecoveryFactor = 1.0;

	void Init();
	void UpdateRate(double fps);
	void WaitForNextFrame();
	void ComputeClockRecovery();
};

extern FramePacer g_pacer;

// Desired GL swap interval / SDL renderer VSync state.  Set by
// UpdatePacerRate() based on whether the display refresh rate matches
// the emulation frame rate.  The main loop applies this to the actual
// GL context or SDL renderer.
//
// Mirrors Windows Altirra behaviour: in windowed mode, Windows uses
// DXGI Present(0) (immediate) and lets the DWM compositor prevent
// tearing, while the waitable timer controls frame pacing.  When the
// display refresh doesn't match the emulation rate (e.g. PAL 50 Hz on
// a 60 Hz display), blocking VSync (swap interval 1) quantises frame
// presentation to vblank boundaries, creating 5:6 judder.  Setting
// swap interval 0 in that case lets the frame pacer deliver even frame
// spacing, and the desktop compositor prevents tearing — just like DWM
// does on Windows.
//
// Values: 0 = immediate (rate mismatch or turbo), 1 = VSync (rate match).
extern int g_desiredSwapInterval;

void UpdatePacerRate();

// -------------------------------------------------------------------------
// Window placement / fullscreen helpers (definitions in main_window.cpp).
// ATSetFullscreen + ATUpdateWindowedGeometry are exported via uiaccessors.h
// already; the static helpers are forward-declared here so the trimmed
// main_sdl3.cpp can still call them from the main loop and shutdown path.
// -------------------------------------------------------------------------

void ATApplyFullscreenMode(SDL_Window *window);
void ATSaveWindowPlacement(SDL_Window *window);
void ATRestoreWindowPlacement(SDL_Window *window);
