// AltirraSDL - Adaptive Input mode
//
// Adaptive Input is a one-toggle setting that lets users plug a
// keyboard, gamepad, and on-screen touch joypad into Atari port 1
// without manually picking a single ATInputMap from a list.  It works
// by activating ALL the canonical port-1 maps for the current hardware
// (Joystick: Arrow Keys + Numpad + generic Gamepad; 5200: Keyboard +
// generic Gamepad) so they compose additively in
// ATInputManager::RebuildMappings().
//
// Why this exists
// ---------------
// The native Altirra (Windows) UX is "open Input menu, pick one map".
// That works on a desktop, but breaks down on every modern surface:
//
//   - Mobile (Android): the user has only a touch screen until they
//     pair a gamepad.  We need touch source codes (kATInputCode_JoyStick1*)
//     bound, AND the gamepad codes ready for when one connects.
//   - WASM (browser): there's no setup wizard for a deep-link join —
//     the joiner arrives from a [Join] click expecting to play, not
//     to configure.  No active map = "I joined but my arrow keys don't
//     work."
//   - Desktop Gaming Mode: the same touch / gamepad flexibility users
//     get on mobile should be a one-click path on desktop too.
//   - Desktop power users: untouched.  Adaptive is purely additive —
//     it doesn't deactivate user-configured maps.
//
// Behaviour contract
// ------------------
// Apply() is idempotent.  It walks ATInputManager::GetInputMapByIndex()
// and activates every map matching the canonical pattern set for the
// current hardware (joystick vs 5200) targeting Atari physical port 0
// (= "joystick port 1" in the user-facing menus, = "the joiner's local
// port whose input is captured into the netplay packet" in netplay).
// Maps not in the canonical set are left exactly as the user had them.
// Calling Apply() while Adaptive is disabled is a no-op.
//
// SetEnabled(true) flips the registry flag to true and immediately
// applies; SetEnabled(false) flips the flag and deactivates the maps
// that THIS module activated (tracked at runtime so we don't tear down
// a map the user activated by hand).
//
// Trigger points (where Apply() gets called):
//   - main_sdl3.cpp startup, after ATLoadSettings.
//   - ATUISetMode(Gaming) on entry.
//   - DriveDeepLinkJoin Phase 0 alongside the Gaming Mode switch.
//   - Mobile setup wizard finish (replaces Wiz_SeedDefaultPort1Map's
//     single-map seed).
//   - ATSimulator hardware-mode change → ATInputManager::Set5200Mode
//     (canonical map set differs between Joystick and 5200 controllers).
//
// Default value
// -------------
// First-run default is true on every platform.  Existing Windows
// Altirra users are unaffected because we don't touch their saved
// active-map flags.  Users who specifically want single-map exclusivity
// turn the checkbox off in Configure System → Input.

#ifndef ALTIRRASDL_INPUT_ADAPTIVE_INPUT_H
#define ALTIRRASDL_INPUT_ADAPTIVE_INPUT_H

#include <vd2/system/refcount.h>
#include <vector>

class ATInputManager;
class ATInputMap;

namespace ATAdaptiveInput {

// True if the user has Adaptive enabled (default true on first run).
bool IsEnabled();

// Toggle the Adaptive flag, persist to registry, and either activate
// (true) or deactivate (false) the managed canonical maps.  Use this
// from the Configure System → Input checkbox; do not toggle the
// registry directly.
void SetEnabled(bool enabled);

// Load the Adaptive flag from the registry.  Call once at startup,
// after ATLoadSettings (so the input maps are populated) but before
// the first Apply().
void Load();

// Idempotent: if Adaptive is enabled, activate every canonical port-1
// map for the current ATSimulator hardware mode (Joystick vs 5200).
// No-op if Adaptive is disabled or the input manager isn't ready.
//
// Returns the number of maps newly activated by THIS call (zero on a
// repeat call where they were already active).  The total count of
// maps Adaptive currently manages is available via GetManagedCount().
int Apply();

// Diagnostic: number of maps Adaptive has activated and is tracking.
size_t GetManagedCount();

} // namespace ATAdaptiveInput

#endif
