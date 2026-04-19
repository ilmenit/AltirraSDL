//	AltirraSDL - Online Play activity hook (impl)

#include <stdafx.h>

#include "ui_netplay_activity.h"
#include "ui_netplay_actions.h"

#include "simulator.h"
#include "simeventmanager.h"

extern ATSimulator g_sim;

namespace ATNetplayUI {

namespace {

// Translate the sim-event enum into a boolean "local play just
// started" signal.  We fire on EXELoad (loading an Atari executable),
// StateLoaded (restoring a saved state), and ColdReset — the last
// one because a fresh cold-reset with an image attached implies the
// user started a fresh play session.
bool IsLocalPlayStartEvent(ATSimulatorEvent ev) {
	return ev == kATSimEvent_EXELoad ||
	       ev == kATSimEvent_StateLoaded ||
	       ev == kATSimEvent_ColdReset;
}

class ActivityCallback final : public IATSimulatorCallback {
public:
	void OnSimulatorEvent(ATSimulatorEvent ev) override {
		if (IsLocalPlayStartEvent(ev)) {
			ActivityTrack_OnLocalPlayStart();
		}
	}
};

ActivityCallback g_cb;
bool             g_hooked = false;

} // anonymous

void Activity_Hook() {
	if (g_hooked) return;
	auto* em = g_sim.GetEventManager();
	if (!em) return;
	em->AddCallback(&g_cb);
	g_hooked = true;
}

void Activity_Unhook() {
	if (!g_hooked) return;
	auto* em = g_sim.GetEventManager();
	if (em) em->RemoveCallback(&g_cb);
	g_hooked = false;
}

} // namespace ATNetplayUI
