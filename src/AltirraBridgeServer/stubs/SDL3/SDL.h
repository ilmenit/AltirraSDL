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
#include <set>
#include <chrono>

namespace HeadlessSDLTimer {
	struct TimerEntry {
		SDL_TimerID id;
		std::chrono::steady_clock::time_point fireTime;
		Uint32 interval;
		SDL_TimerCallback cb;
		void *param;
	};

	struct State {
		std::mutex mutex;
		std::condition_variable cv;
		std::map<SDL_TimerID, TimerEntry> timers;
		std::set<SDL_TimerID> active;
		std::set<SDL_TimerID> cancelled;
		std::thread worker;
		bool running = false;
		SDL_TimerID nextID = 1;

		~State() {
			{
				std::lock_guard<std::mutex> lock(mutex);
				running = false;
				timers.clear();
				cancelled.clear();
			}
			cv.notify_all();
			if (worker.joinable())
				worker.join();
		}
	};

	inline State& GetState() {
		static State state;
		return state;
	}

	inline void WorkerThread() {
		State& state = GetState();
		std::unique_lock<std::mutex> lock(state.mutex);
		while (state.running) {
			if (state.timers.empty()) {
				state.cv.wait(lock, [&] { return !state.running || !state.timers.empty(); });
			} else {
				auto nextIt = state.timers.begin();
				for (auto it = state.timers.begin(); it != state.timers.end(); ++it) {
					if (it->second.fireTime < nextIt->second.fireTime)
						nextIt = it;
				}
				auto now = std::chrono::steady_clock::now();
				if (now >= nextIt->second.fireTime) {
					TimerEntry entry = nextIt->second;
					state.timers.erase(nextIt);
					state.active.insert(entry.id);
					lock.unlock();
					Uint32 nextInterval = entry.cb(entry.param, entry.id, entry.interval);
					lock.lock();
					state.active.erase(entry.id);
					if (nextInterval > 0 && state.running
						&& !state.cancelled.erase(entry.id)) {
						entry.interval = nextInterval;
						entry.fireTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(nextInterval);
						state.timers[entry.id] = entry;
					}
					state.cv.notify_all();
				} else {
					state.cv.wait_until(lock, nextIt->second.fireTime);
				}
			}
		}
	}
}

inline SDL_TimerID SDL_AddTimer(Uint32 interval, SDL_TimerCallback callback, void* userdata) {
	if (!callback || interval == 0) return 0;
	auto& state = HeadlessSDLTimer::GetState();
	std::lock_guard<std::mutex> lock(state.mutex);
	if (!state.running) {
		state.running = true;
		state.worker = std::thread(HeadlessSDLTimer::WorkerThread);
	}
	SDL_TimerID id = state.nextID++;
	HeadlessSDLTimer::TimerEntry entry;
	entry.id = id;
	entry.interval = interval;
	entry.fireTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(interval);
	entry.cb = callback;
	entry.param = userdata;
	state.cancelled.erase(id);
	state.timers[id] = entry;
	state.cv.notify_one();
	return id;
}

inline bool SDL_RemoveTimer(SDL_TimerID id) {
	if (id == 0) return false;
	auto& state = HeadlessSDLTimer::GetState();
	std::unique_lock<std::mutex> lock(state.mutex);
	auto it = state.timers.find(id);
	if (it != state.timers.end()) {
		state.timers.erase(it);
		state.cv.notify_one();
		return true;
	}
	if (state.active.contains(id)) {
		state.cancelled.insert(id);
		state.cv.wait(lock, [&] { return !state.active.contains(id); });
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
