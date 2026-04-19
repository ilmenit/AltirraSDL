// Altirra SDL3 netplay - platform notifications (common impl)
//
// Dispatches to the per-platform implementations.  SDL_FlashWindow and
// the chime path are the same on every platform, so they live here.
// libnotify / Shell_NotifyIconW / UNUserNotificationCenter live in
// sibling files compiled only on their native platform.

#include <stdafx.h>

#include "platform_notify.h"

#include <SDL3/SDL_video.h>

#include <cstdio>
#include <cstring>

namespace ATNetplay {

// Forward decls to the platform backends.  Each returns 1 on success
// (one signal emitted) and 0 on no-op (backend unavailable).  The
// common code multiplies by prefs.systemNotify.
int PlatformNotifyInit(const char* appName);
void PlatformNotifyShutdown();
int PlatformNotifyPost(const char* title, const char* body);

namespace {
SDL_Window* g_window = nullptr;

const int16_t* g_chimeSamples = nullptr;
size_t         g_chimeCount   = 0;
int            g_chimeRate    = 44100;
}

void SetWindow(SDL_Window* window) {
	g_window = window;
}

void Initialize(const char* appName) {
	PlatformNotifyInit(appName ? appName : "AltirraSDL");
}

void Shutdown() {
	PlatformNotifyShutdown();
	g_chimeSamples = nullptr;
	g_chimeCount = 0;
}

int Notify(const char* title, const char* body, const NotifyPrefs& prefs) {
	int n = 0;

	if (prefs.flashWindow && g_window) {
		if (SDL_FlashWindow(g_window, SDL_FLASH_UNTIL_FOCUSED))
			++n;
	}
	if (prefs.systemNotify) {
		if (PlatformNotifyPost(
				title ? title : "AltirraSDL",
				body  ? body  : "")) {
			++n;
		}
	}
	if (prefs.playChime && g_chimeSamples && g_chimeCount) {
		PlayChime();
		++n;
	}
	return n;
}

// ---------------------------------------------------------------------
// Chime — submit PCM through a dedicated SDL audio stream rather than
// going through IATAudioOutput.  That avoids contention with the
// emulated POKEY output (which owns the main audio stream) and means a
// missing emulator audio device doesn't mute the UI signal.
// ---------------------------------------------------------------------

#include <SDL3/SDL_audio.h>

namespace {
SDL_AudioStream* g_chimeStream = nullptr;

void EnsureChimeStream(int sampleRate) {
	if (g_chimeStream) {
		SDL_AudioSpec cur = {};
		if (SDL_GetAudioStreamFormat(g_chimeStream, &cur, nullptr) &&
		    cur.freq == sampleRate) {
			return;
		}
		SDL_DestroyAudioStream(g_chimeStream);
		g_chimeStream = nullptr;
	}
	SDL_AudioSpec spec = {};
	spec.format   = SDL_AUDIO_S16LE;
	spec.channels = 1;
	spec.freq     = sampleRate;
	g_chimeStream = SDL_OpenAudioDeviceStream(
		SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
	if (g_chimeStream) {
		SDL_ResumeAudioStreamDevice(g_chimeStream);
	}
}
}

void SetChimeSample(const int16_t* samples, size_t numSamples, int sampleRate) {
	g_chimeSamples = samples;
	g_chimeCount   = numSamples;
	g_chimeRate    = sampleRate > 0 ? sampleRate : 44100;
}

void PlayChime() {
	if (!g_chimeSamples || !g_chimeCount) return;
	EnsureChimeStream(g_chimeRate);
	if (!g_chimeStream) return;
	SDL_PutAudioStreamData(g_chimeStream, g_chimeSamples,
		(int)(g_chimeCount * sizeof(int16_t)));
}

// ---------------------------------------------------------------------
// Fallback: linker sees these when no platform backend is built in.
// Each platform-specific .cpp overrides them by being present in the
// sources list for that OS.  CMake selects one at configure time.
// ---------------------------------------------------------------------

#if !defined(ALTIRRA_NETPLAY_HAS_NOTIFY_BACKEND)

int  PlatformNotifyInit(const char*)                 { return 0; }
void PlatformNotifyShutdown()                        {}
int  PlatformNotifyPost(const char*, const char*)    { return 0; }

#endif

} // namespace ATNetplay
