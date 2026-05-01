// Stubs that resolve UI-side symbols pulled in by coordinator.cpp
// when it's compiled into the standalone coordinator_selftest.
//
// In the production build:
//   - ATEmoteNetplay::OnReceivedFromPeer lives in
//     src/AltirraSDL/source/ui/emotes/emote_netplay.cpp
// The standalone test doesn't link any UI code, so we provide a
// no-op definition here.  Forward-declared in coordinator.cpp.

#include <cstdint>

namespace ATEmoteNetplay {
void OnReceivedFromPeer(uint8_t /*iconId*/) {
	// Test stub: discard the emote.  Live emote dispatch is the
	// frontend's job; the lockstep machinery doesn't depend on it.
}
} // namespace ATEmoteNetplay
