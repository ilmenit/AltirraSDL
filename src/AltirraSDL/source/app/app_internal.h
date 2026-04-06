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

	void Init();
	void UpdateRate(double fps);
	void WaitForNextFrame();
};

extern FramePacer g_pacer;

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
