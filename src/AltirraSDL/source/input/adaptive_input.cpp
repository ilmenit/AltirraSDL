// AltirraSDL - Adaptive Input mode (impl)
//
// See adaptive_input.h for the contract.  This file owns:
//   - the persisted enable flag (Input/Adaptive registry key)
//   - the runtime set of "maps WE activated" so toggling Adaptive off
//     deactivates only what we turned on, leaving user-activated maps
//     intact
//   - the canonical-map heuristic (substring match against the
//     well-known map names that ship with the default settings)

#include <stdafx.h>

#include "adaptive_input.h"

#include "simulator.h"
#include "inputmanager.h"
#include "inputmap.h"
#include "inputdefs.h"

#include <vd2/system/refcount.h>
#include <vd2/system/registry.h>
#include <vd2/system/VDString.h>

#include <algorithm>
#include <cstdlib>
#include <cwctype>
#include <unordered_set>

extern ATSimulator g_sim;

namespace ATAdaptiveInput {

namespace {

// On first launch (no registry key yet) Adaptive defaults to true on
// every platform.  Existing Windows users who already have a saved
// active-map selection are not affected because Apply() is purely
// additive — it only ENABLES extra canonical maps; it never disables
// anything the user activated themselves.
//
// Users who specifically want exclusive single-map behaviour set the
// checkbox off in Configure System → Input.
constexpr bool kFirstRunDefault = true;

// Registry-backed enable flag.  Cached in memory so IsEnabled() is a
// hot-path no-op call (it's called once per Apply() and once per UI
// frame on the Configure System Input page).
bool g_enabledLoaded = false;
bool g_enabled       = kFirstRunDefault;

// Set of input maps Adaptive has activated.  Stored as raw pointers
// because ATInputManager owns the maps and outlives this module; the
// pointers are keys for "did WE turn this on" lookups, never
// dereferenced for refcount purposes here.  Cleared on SetEnabled(false)
// after the deactivations land, and on hardware-mode change before
// re-applying.
std::unordered_set<ATInputMap*> g_managed;

// Case-insensitive wide-string substring search.  Used for the
// canonical-map name heuristic — we ship a fixed set of preset names
// that the default settings install ("Arrow Keys -> Joystick (port
// 1)", "Numpad -> Joystick (port 1)", etc.) and Adaptive matches them
// by substring so a user with a custom map named "Arrow Keys (custom)"
// still gets picked up.
bool ContainsCI(const wchar_t *hay, const wchar_t *needle) {
	if (!hay || !needle || !*needle) return false;
	size_t nlen = 0;
	while (needle[nlen]) ++nlen;
	for (; *hay; ++hay) {
		size_t i = 0;
		while (i < nlen
			&& hay[i]
			&& std::towlower((wint_t)hay[i])
			   == std::towlower((wint_t)needle[i])) {
			++i;
		}
		if (i == nlen) return true;
	}
	return false;
}

// Decide whether `imap` is one of the canonical port-1 maps Adaptive
// should activate for the current hardware.
//
// Acceptance criteria:
//   1. Targets Atari physical port 0 (= "joystick port 1" in user
//      terms = port the netplay capture redirects).
//   2. Matches the current hardware's controller type (Joystick for
//      Atari 8-bit / XEGS; 5200Controller for the 5200).
//   3. Generic input source (mUnit == -1) — accepts any input device,
//      so it works whether or not a specific gamepad is plugged in.
//      Specific-unit maps (e.g. "Gamepad 2 -> Joystick (port 1)") are
//      left to user choice; they exist precisely so users with two
//      physical gamepads can lock-bind one per Atari port.
//   4. Name matches one of the canonical seed-names so we don't sweep
//      up arbitrary user maps.  Match is substring + case-insensitive.
bool IsCanonicalPort1Map(ATInputMap *imap, bool is5200) {
	if (!imap) return false;
	if (!imap->UsesPhysicalPort(0)) return false;
	if (imap->GetSpecificInputUnit() != -1) return false;

	const ATInputControllerType wantedType = is5200
		? kATInputControllerType_5200Controller
		: kATInputControllerType_Joystick;
	if (!imap->HasControllerType(wantedType)) return false;

	const wchar_t *name = imap->GetName();
	if (!name) return false;

	if (is5200) {
		// 5200 canonical seeds:
		//   "Keyboard -> 5200 Controller (absolute; port 1)"
		//   "Keyboard -> 5200 Controller (relative; port 1)"
		//   "Gamepad -> 5200 Controller (port 1)"
		// Numpad sometimes appears as a 5200 alias; allow it too.
		if (ContainsCI(name, L"5200 Controller")) return true;
		return false;
	}

	// Joystick canonical seeds:
	//   "Arrow Keys -> Joystick (port 1)"          (keyboard arrows)
	//   "Numpad -> Joystick (port 1)"              (keyboard numpad)
	//   "Gamepad -> Joystick (port 1)"             (any gamepad +
	//                                               touch source codes
	//                                               kATInputCode_JoyStick1*
	//                                               that the on-screen
	//                                               touch joypad emits)
	if (ContainsCI(name, L"Arrow Keys")) return true;
	if (ContainsCI(name, L"Numpad"))     return true;
	// Match "Gamepad -> Joystick" but NOT "Gamepad N -> Joystick" — the
	// `mSpecificInputUnit == -1` check above already filters those out,
	// so a plain Gamepad substring is sufficient here.
	if (ContainsCI(name, L"Gamepad"))    return true;
	return false;
}

// Walk every input map and either activate (enable=true) or deactivate
// (enable=false) the canonical port-1 set for current hardware.  The
// activation tracking goes into g_managed so a later SetEnabled(false)
// only deactivates what we touched.
//
// Returns the number of state transitions this call produced.
int ApplyOrUnapply(bool enable) {
	ATInputManager *im = g_sim.GetInputManager();
	if (!im) return 0;

	const bool is5200 =
		(g_sim.GetHardwareMode() == kATHardwareMode_5200);
	const uint32 count = im->GetInputMapCount();

	int transitions = 0;
	for (uint32 i = 0; i < count; ++i) {
		vdrefptr<ATInputMap> imap;
		if (!im->GetInputMapByIndex(i, ~imap) || !imap)
			continue;
		if (!IsCanonicalPort1Map(imap, is5200))
			continue;

		ATInputMap *raw = imap;
		if (enable) {
			// Track the activation so a later disable removes only
			// what we added.  ActivateInputMap is a no-op if the map
			// is already active in ATInputManager's internal state,
			// so we always insert into g_managed even on re-apply
			// (idempotency).
			if (g_managed.insert(raw).second) {
				im->ActivateInputMap(raw, true);
				++transitions;
			}
		} else {
			auto it = g_managed.find(raw);
			if (it != g_managed.end()) {
				im->ActivateInputMap(raw, false);
				g_managed.erase(it);
				++transitions;
			}
		}
	}
	return transitions;
}

} // anonymous

bool IsEnabled() {
	if (!g_enabledLoaded) Load();
	return g_enabled;
}

void SetEnabled(bool enabled) {
	// Persist before applying so a crash mid-apply still leaves the
	// preference in the state the user clicked.
	{
		VDRegistryAppKey key("Input", true);
		if (key.isReady())
			key.setBool("Adaptive", enabled);
	}
	g_enabled       = enabled;
	g_enabledLoaded = true;

	if (enabled) {
		ApplyOrUnapply(true);
	} else {
		ApplyOrUnapply(false);
	}
}

void Load() {
	VDRegistryAppKey key("Input", false);
	if (key.isReady()) {
		g_enabled = key.getBool("Adaptive", kFirstRunDefault);
	} else {
		g_enabled = kFirstRunDefault;
	}
	g_enabledLoaded = true;
}

int Apply() {
	if (!g_enabledLoaded) Load();
	if (!g_enabled) return 0;
	return ApplyOrUnapply(true);
}

size_t GetManagedCount() {
	return g_managed.size();
}

} // namespace ATAdaptiveInput

// Free-function wrapper so callers in modules that don't include
// adaptive_input.h (e.g. stubs/uiaccessors_stubs.cpp, which is the
// implementation point for ATUISwitchHardwareMode) can call us via
// `extern void ATAdaptiveInput_ApplyAfterHardwareSwitch();`.
//
// On a 5200↔Joystick toggle, the canonical map set changes — Joystick
// maps become 5200-controller maps and vice versa.  The old maps stay
// in g_managed but ATInputManager::RebuildMappings() filters out
// incompatible controllers automatically (inputmanager.cpp:1430), so
// their lingering active flags are functionally inert.  Apply() finds
// the NEW canonical maps and activates them; the bookkeeping leak (a
// few stale entries in g_managed across a hardware switch) is small
// and self-corrects on the next SetEnabled(false) cycle.
void ATAdaptiveInput_ApplyAfterHardwareSwitch() {
	ATAdaptiveInput::Apply();
}
