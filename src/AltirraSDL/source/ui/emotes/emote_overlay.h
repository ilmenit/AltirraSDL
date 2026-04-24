//	AltirraSDL - Online Play emote overlays.
//
//	Two independent single-slot overlays share the same 4.0 s entry /
//	hold / exit animation (slide + scale + fade):
//
//	   * Inbound  — received from peer — anchored top-left,
//	                slides in from offscreen LEFT.
//	   * Outbound — confirmation of what we just sent —
//	                anchored bottom-right, slides in from offscreen RIGHT.
//
//	A new Show() on the same side replaces the current emote and
//	restarts the animation for that side; the two sides never block
//	each other, so seeing the peer's reply doesn't hide our own send
//	confirmation and vice versa.

#pragma once

#include <stdint.h>

#ifdef ALTIRRA_NETPLAY_ENABLED

namespace ATEmoteOverlay {

// Inbound (received from peer) — top-left, slide from left.
void Show(int iconId, uint64_t nowMs);

// Outbound (local send confirmation) — bottom-right, slide from right.
void ShowOutbound(int iconId, uint64_t nowMs);

// Immediately hide both sides without animating out.  Called on
// netplay teardown.
void Clear();

// Render both overlays if active.  Safe to call every frame.
void Render(uint64_t nowMs);

} // namespace ATEmoteOverlay

#else // !ALTIRRA_NETPLAY_ENABLED

namespace ATEmoteOverlay {
    inline void Show(int, uint64_t)         {}
    inline void ShowOutbound(int, uint64_t) {}
    inline void Clear()                     {}
    inline void Render(uint64_t)            {}
} // namespace ATEmoteOverlay

#endif // ALTIRRA_NETPLAY_ENABLED
