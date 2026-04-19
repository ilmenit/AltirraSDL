//	AltirraSDL - Online Play activity hook
//
//	Subscribes the netplay UI to ATSimulatorEventManager so that
//	ColdReset / EXELoad / StateLoaded flip the user's activity to
//	`PlayingLocal`, which in turn causes ReconcileHostedGames() to
//	Suspend every hosted offer that isn't already in a session.
//
//	Call Activity_Hook() once after ATUIInit and Activity_Unhook()
//	before teardown.  Idempotent.

#pragma once

namespace ATNetplayUI {

void Activity_Hook();
void Activity_Unhook();

} // namespace ATNetplayUI
