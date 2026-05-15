// AltirraBridge - SDL3 frontend glue
//
// Implements the four ATBridgeDispatch* functions for the AltirraSDL
// target.
//
// BOOT / MOUNT route through the SDL3 deferred-action queue
// (ATUIPushDeferred / ATUIPollDeferredActions) so they share code
// with the menu File > Open and File > Mount commands. Boot in
// particular triggers a cold reset and several hundred frames of
// settle time, so the response is sent immediately and clients use
// FRAME N to wait for the action to complete.
//
// STATE_SAVE / STATE_LOAD run **synchronously** -- the bridge command
// blocks until the save/load finishes and the response carries the
// outcome (size, cycle counter, machine type, etc.). Both run on the
// bridge poll thread, which is the SDL3 main thread between frame
// ticks; this is the same point where ATUIPollDeferredActions would
// have processed the deferred action, so timing is identical. The
// upside is no FRAME 1 dance for clients -- the simulator is in the
// loaded state by the time the response is read.
//
// Pause state preservation: STATE_LOAD does NOT call Resume(). The
// menu-driven File > Load State path (in ui_main.cpp's deferred
// handler) does Resume() because the human user expects to keep
// playing; the bridge analyst's expectation is the opposite -- load
// the state, then inspect. Clients that want to resume issue RESUME
// explicitly after STATE_LOAD.
//
// The headless server target (AltirraBridgeServer) provides its own
// implementation of these functions that calls into the simulator
// directly without going through the SDL3 deferred queue.

#include <stdafx.h>

#include "bridge_main_glue.h"
#include "ui_main.h"            // ATUIPushDeferred, kATDeferred_*

#include "simulator.h"
#include <at/atcore/media.h>
#include <at/atio/image.h>
#include <vd2/system/text.h>
#include <vd2/system/VDString.h>
#include <vd2/system/error.h>

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

bool ATBridgeDispatchStateSave(ATSimulator& sim, const std::string& path) {
	if (path.empty()) return false;
	try {
		const VDStringW wpath = VDTextU8ToW(VDStringSpanA(path.c_str()));
		sim.SaveState(wpath.c_str());
		return true;
	} catch (const MyError&) {
		return false;
	}
}

bool ATBridgeDispatchStateLoad(ATSimulator& sim, const std::string& path) {
	if (path.empty()) return false;
	try {
		const VDStringW wpath = VDTextU8ToW(VDStringSpanA(path.c_str()));
		ATImageLoadContext ctx{};
		return sim.Load(wpath.c_str(), kATMediaWriteMode_RO, &ctx);
	} catch (const MyError&) {
		return false;
	}
}
