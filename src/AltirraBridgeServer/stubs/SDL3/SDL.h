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
#include <thread>
#include <mutex>
#include <condition_variable>
#include <map>
#include <chrono>

namespace HeadlessSDLTimer {
	struct TimerEntry {
		SDL_TimerID id;
		std::chrono::steady_clock::time_point fireTime;
		Uint32 interval;
		SDL_TimerCallback cb;
		void *param;
	};

	inline std::mutex& GetMutex() {
		static std::mutex m;
		return m;
	}
	inline std::condition_variable& GetCV() {
		static std::condition_variable cv;
		return cv;
	}
	inline std::map<SDL_TimerID, TimerEntry>& GetTimers() {
		static std::map<SDL_TimerID, TimerEntry> timers;
		return timers;
	}
	inline bool& GetRunning() {
		static bool running = false;
		return running;
	}
	inline std::thread*& GetThread() {
		static std::thread *t = nullptr;
		return t;
	}
	inline SDL_TimerID& GetNextID() {
		static SDL_TimerID nextID = 1;
		return nextID;
	}

	inline void WorkerThread() {
		std::unique_lock<std::mutex> lock(GetMutex());
		while (GetRunning()) {
			if (GetTimers().empty()) {
				GetCV().wait(lock, [] { return !GetRunning() || !GetTimers().empty(); });
			} else {
				auto nextIt = GetTimers().begin();
				for (auto it = GetTimers().begin(); it != GetTimers().end(); ++it) {
					if (it->second.fireTime < nextIt->second.fireTime)
						nextIt = it;
				}
				auto now = std::chrono::steady_clock::now();
				if (now >= nextIt->second.fireTime) {
					TimerEntry entry = nextIt->second;
					GetTimers().erase(nextIt);
					lock.unlock();
					Uint32 nextInterval = entry.cb(entry.param, entry.id, entry.interval);
					lock.lock();
					if (nextInterval > 0) {
						entry.interval = nextInterval;
						entry.fireTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(nextInterval);
						GetTimers()[entry.id] = entry;
					}
				} else {
					GetCV().wait_until(lock, nextIt->second.fireTime);
				}
			}
		}
	}
}

inline SDL_TimerID SDL_AddTimer(Uint32 interval, SDL_TimerCallback callback, void* userdata) {
	if (!callback || interval == 0) return 0;
	std::lock_guard<std::mutex> lock(HeadlessSDLTimer::GetMutex());
	if (!HeadlessSDLTimer::GetRunning()) {
		HeadlessSDLTimer::GetRunning() = true;
		HeadlessSDLTimer::GetThread() = new std::thread(HeadlessSDLTimer::WorkerThread);
	}
	SDL_TimerID id = HeadlessSDLTimer::GetNextID()++;
	HeadlessSDLTimer::TimerEntry entry;
	entry.id = id;
	entry.interval = interval;
	entry.fireTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(interval);
	entry.cb = callback;
	entry.param = userdata;
	HeadlessSDLTimer::GetTimers()[id] = entry;
	HeadlessSDLTimer::GetCV().notify_one();
	return id;
}

inline bool SDL_RemoveTimer(SDL_TimerID id) {
	if (id == 0) return false;
	std::lock_guard<std::mutex> lock(HeadlessSDLTimer::GetMutex());
	auto it = HeadlessSDLTimer::GetTimers().find(id);
	if (it != HeadlessSDLTimer::GetTimers().end()) {
		HeadlessSDLTimer::GetTimers().erase(it);
		HeadlessSDLTimer::GetCV().notify_one();
		return true;
	}
	return false;
}

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
