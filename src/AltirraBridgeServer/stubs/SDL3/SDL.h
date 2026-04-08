// SDL3/SDL.h — headless bridge server shim.
//
// The bridge server does not link SDL3. Several SDL3-frontend stub
// files (console_stubs.cpp, uiaccessors_stubs.cpp, win32_stubs.cpp)
// are shared with AltirraSDL and reference SDL3 for clipboard,
// message boxes, timers, and window flags. Those code paths are never
// called in the headless server — no UI, no window, no user
// interaction — so we provide no-op replacements here instead of
// forking the files.
//
// Every function returns a neutral value (0 / false / nullptr) and
// every type is a minimal stand-in. If any of these ever fires at
// runtime in the bridge server, a stub file's code path was reached
// that shouldn't have been — fix the caller, not this header.

#pragma once

#include <cstdint>
#include <cstddef>

using Uint32 = std::uint32_t;
using Uint64 = std::uint64_t;
using SDL_TimerID = Uint32;
using SDL_WindowFlags = Uint64;

struct SDL_Window;

using SDL_TimerCallback = Uint32 (*)(void*, SDL_TimerID, Uint32);
#define SDLCALL

// Clipboard
inline bool SDL_SetClipboardText(const char*) { return false; }
inline char* SDL_GetClipboardText() { return nullptr; }
inline bool SDL_HasClipboardText() { return false; }
inline void SDL_free(void*) {}

// URL
inline bool SDL_OpenURL(const char*) { return false; }

// Timers
inline SDL_TimerID SDL_AddTimer(Uint32, SDL_TimerCallback, void*) { return 0; }
inline bool SDL_RemoveTimer(SDL_TimerID) { return false; }

// Message box
enum { SDL_MESSAGEBOX_INFORMATION = 0, SDL_MESSAGEBOX_WARNING = 1, SDL_MESSAGEBOX_ERROR = 2 };
enum { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT = 1, SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT = 2 };

struct SDL_MessageBoxButtonData { Uint32 flags; int buttonID; const char* text; };
struct SDL_MessageBoxData {
	Uint32 flags;
	SDL_Window* window;
	const char* title;
	const char* message;
	int numbuttons;
	const SDL_MessageBoxButtonData* buttons;
	const void* colorScheme;
};

inline bool SDL_ShowSimpleMessageBox(Uint32, const char*, const char*, SDL_Window*) { return false; }
inline bool SDL_ShowMessageBox(const SDL_MessageBoxData*, int* buttonid) {
	if (buttonid) *buttonid = -1;
	return false;
}

// Window flags / state (never called — no window in headless mode)
#define SDL_WINDOW_FULLSCREEN  0x0000000000000001ULL

inline SDL_WindowFlags SDL_GetWindowFlags(SDL_Window*) { return 0; }
inline bool SDL_SetWindowFullscreen(SDL_Window*, bool) { return false; }
inline bool SDL_SetWindowMouseGrab(SDL_Window*, bool) { return false; }
inline bool SDL_SetWindowRelativeMouseMode(SDL_Window*, bool) { return false; }
