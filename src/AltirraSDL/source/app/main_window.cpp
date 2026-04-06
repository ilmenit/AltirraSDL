//	AltirraSDL - Window placement + fullscreen helpers (split from main_sdl3.cpp Phase 3c)

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/registry.h>
#include "uiaccessors.h"
#include "ui_main.h"
#include "options.h"
#include "logging.h"
#include "app_internal.h"

extern SDL_Window *g_pWindow;


// =========================================================================
// Fullscreen mode helpers
// =========================================================================

// Apply the fullscreen display mode from g_ATOptions before entering
// fullscreen.  Borderless mode (or exclusive with zero dimensions) uses
// SDL3's default desktop-resolution borderless fullscreen.  Exclusive
// mode with a specific resolution sets the closest matching display mode.
void ATApplyFullscreenMode(SDL_Window *window) {
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
				LOG_INFO("Main", "No matching fullscreen mode %ux%u@%u Hz, falling back to borderless", g_ATOptions.mFullScreenWidth, g_ATOptions.mFullScreenHeight,
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

void ATSaveWindowPlacement(SDL_Window *window) {
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

void ATRestoreWindowPlacement(SDL_Window *window) {
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
