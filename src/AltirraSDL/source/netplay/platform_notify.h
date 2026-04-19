// Altirra SDL3 netplay - platform notifications shim
//
// One entry point that the netplay UI calls when a waiting host needs
// attention: a join request arrived, or a joiner has connected.  The
// signal triples up so the user catches it no matter where the
// emulator window is:
//
//   1. Flash the taskbar/dock entry — SDL_FlashWindow() is a single
//      cross-platform call.  Works everywhere SDL3 supports it
//      (Windows/macOS/Linux/X11/Wayland).
//
//   2. Post a system notification via the native mechanism:
//        - Linux  : libnotify (dlopen'd at runtime — no build-time dep)
//        - Windows: Shell_NotifyIconW (tray balloon)
//        - macOS  : UNUserNotificationCenter (objective-C bridge)
//      Platforms without a backend silently no-op.
//
//   3. Play a chime through the existing IATAudioOutput.  The UI owns
//      the decision of whether to chime (user pref); we simply take
//      an opaque PCM blob from the caller.
//
// This module is intentionally stateless — it holds no threads, no
// persistent handles, no caches beyond a lazy libnotify dlopen() on
// Linux.  Callers invoke it once per event, the OS gets the
// notification, we return.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct SDL_Window;

namespace ATNetplay {

// User preference mask.  Callers compose this from the Preferences →
// Netplay dialog (checkboxes for "flash window", "system notification",
// "play chime").
struct NotifyPrefs {
	bool flashWindow  = true;
	bool systemNotify = true;
	bool playChime    = true;
};

// The main loop sets this once with the main-window handle.  All
// subsequent calls to Notify() use it for SDL_FlashWindow().
void SetWindow(SDL_Window* window);

// Must be called before the first Notify() to initialise optional
// backends (libnotify dlopen, Windows tray icon registration).  Safe
// to call more than once.
void Initialize(const char* appName);

// Release any OS-level handles the backend allocated.  Safe to call
// without Initialize().
void Shutdown();

// Issue a notification.  Returns number of signals that were actually
// emitted (0..3).  Never blocks: notifications that require async I/O
// run to completion on a background thread managed by the OS.
int Notify(const char* title,
           const char* body,
           const NotifyPrefs& prefs = NotifyPrefs{});

// ---- Chime ----------------------------------------------------------------
//
// The chime API is split out so that the audio mix can be set up
// once and reused.  PlayChime() is allowed from the main thread; it
// hands the sample to the audio subsystem and returns.

// Attach a mixed S16 mono PCM chime (duration ≤ 200 ms, sampleRate
// typically 44100).  Pass nullptr to detach.  The module takes a
// reference; caller must not free the buffer until Shutdown().
void SetChimeSample(const int16_t* samples, size_t numSamples,
                    int sampleRate);

// Play the chime through the current audio output.  No-op if
// SetChimeSample() was never called or audio output isn't up.
void PlayChime();

} // namespace ATNetplay
