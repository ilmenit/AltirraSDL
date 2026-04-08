// AltirraBridge - SDL3 frontend glue
//
// Implements the four ATBridgeDispatch* functions for the AltirraSDL
// target. Routes the bridge's BOOT / MOUNT / STATE_SAVE / STATE_LOAD
// commands through the existing SDL3 deferred-action queue
// (ATUIPushDeferred / ATUIPollDeferredActions), so the work runs on
// the same code path as the menu File > Open and File > Save State
// commands. The bridge response is sent immediately; clients use
// FRAME N afterwards to wait for the action to complete.
//
// The headless server target (AltirraBridgeServer) provides its own
// implementation of these functions that calls into the simulator
// directly without going through the SDL3 deferred queue.

#include <stdafx.h>

#include "bridge_main_glue.h"
#include "ui_main.h"            // ATUIPushDeferred, kATDeferred_*

class ATSimulator;

bool ATBridgeDispatchBoot(ATSimulator& /*sim*/, const std::string& path) {
	if (path.empty()) return false;
	ATUIPushDeferred(kATDeferred_BootImage, path.c_str());
	return true;
}

bool ATBridgeDispatchMount(ATSimulator& /*sim*/, int drive, const std::string& path) {
	if (path.empty()) return false;
	ATUIPushDeferred(kATDeferred_AttachDisk, path.c_str(), drive);
	return true;
}

bool ATBridgeDispatchStateSave(ATSimulator& /*sim*/, const std::string& path) {
	if (path.empty()) return false;
	ATUIPushDeferred(kATDeferred_SaveState, path.c_str());
	return true;
}

bool ATBridgeDispatchStateLoad(ATSimulator& /*sim*/, const std::string& path) {
	if (path.empty()) return false;
	ATUIPushDeferred(kATDeferred_LoadState, path.c_str());
	return true;
}
