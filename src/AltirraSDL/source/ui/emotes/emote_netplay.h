//	AltirraSDL - Online Play emote send/receive + rate limiting.
//
//	Thin facade between the emote UI and the netplay coordinator.
//	Reads the "Netplay: Send emotes" / "Netplay: Receive emotes" toggles
//	from the registry, enforces a rate limit on both ends (same icon:
//	4.0s cooldown = the overlay display duration; different icon: 1.5s),
//	and drops packets silently when disabled or throttled.
//
//	Callbacks from the netplay thread are marshalled onto the main
//	thread via an atomic slot — Process() drains it once per frame from
//	the main loop, keeping the rest of the emote UI single-threaded.

#pragma once

#include <stdint.h>

#ifdef ALTIRRA_NETPLAY_ENABLED

namespace ATEmoteNetplay {

// Called from the main loop after ATEmotes::Initialize() has run.
// Safe to call multiple times.
void Initialize();
void Shutdown();

// User pressed an icon in the picker.  Checks rate limit + send toggle
// + netplay-active; if all pass, encodes and sends.  Returns true on
// success (useful for UI feedback).
bool Send(int iconId);

// Drain any pending received-emote from the netplay thread onto the
// overlay.  Call once per main-loop frame before drawing the overlay.
void Process(uint64_t nowMs);

// Called on the netplay thread when a NetEmote arrives.  Pushes the
// id onto a thread-safe slot; the main thread consumes it in Process.
void OnReceivedFromPeer(uint8_t iconId);

// Registry-backed toggles.  Defaults true.
bool GetSendEnabled();
void SetSendEnabled(bool v);
bool GetReceiveEnabled();
void SetReceiveEnabled(bool v);

} // namespace ATEmoteNetplay

#else // !ALTIRRA_NETPLAY_ENABLED

// Inline no-op stubs for builds without the netplay module (e.g. WASM).
// See netplay_glue.h for the rationale.
namespace ATEmoteNetplay {
    inline void Initialize()                {}
    inline void Shutdown()                  {}
    inline bool Send(int)                   { return false; }
    inline void Process(uint64_t)           {}
    inline void OnReceivedFromPeer(uint8_t) {}
    inline bool GetSendEnabled()            { return false; }
    inline void SetSendEnabled(bool)        {}
    inline bool GetReceiveEnabled()         { return false; }
    inline void SetReceiveEnabled(bool)     {}
} // namespace ATEmoteNetplay

#endif // ALTIRRA_NETPLAY_ENABLED
