//	AltirraSDL - Mobile UI: suspend/restore save state
//	Split out of ui_mobile.cpp.  Verbatim move; behaviour identical.
//
//	Android can terminate a backgrounded app at any time.  To make the
//	emulator feel like a native console handheld (flip open, keep
//	playing), we snapshot the simulator to disk whenever the app goes
//	to background or is about to terminate, and restore it on next
//	launch.  Both halves of the feature are user-toggleable under the
//	mobile Settings panel.

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/error.h>
#include <vd2/system/text.h>
#include <at/atcore/media.h>
#include <at/atio/image.h>

#include "ui_mobile.h"
#include "simulator.h"

#include "mobile_internal.h"

void ATMobileUI_SaveSuspendState(ATSimulator &sim,
	const ATMobileUIState &mobileState)
{
	if (!mobileState.autoSaveOnSuspend)
		return;
	if (!mobileState.gameLoaded) {
		ATMobileUI_ClearSuspendState();
		return;
	}
	VDStringW path = QuickSaveStatePath();
	try {
		sim.SaveState(path.c_str());
	} catch (const MyError &e) {
		VDStringA u8 = VDTextWToU8(path);
		fprintf(stderr, "[mobile] SaveState(%s) failed: %s\n",
			u8.c_str(), e.c_str());
	} catch (...) {
		fprintf(stderr, "[mobile] SaveState threw unknown exception\n");
	}
}

bool ATMobileUI_RestoreSuspendState(ATSimulator &sim,
	ATMobileUIState &mobileState)
{
	if (!mobileState.autoRestoreOnStart)
		return false;
	VDStringW path = QuickSaveStatePath();
	if (!VDDoesPathExist(path.c_str()))
		return false;

	try {
		ATImageLoadContext ctx{};
		if (sim.Load(path.c_str(), kATMediaWriteMode_RO, &ctx)) {
			sim.Resume();
			mobileState.gameLoaded = true;
			return true;
		}
	} catch (const MyError &e) {
		VDStringA u8 = VDTextWToU8(path);
		fprintf(stderr, "[mobile] LoadState(%s) failed: %s\n",
			u8.c_str(), e.c_str());
		ATMobileUI_ClearSuspendState();
	} catch (...) {
		fprintf(stderr, "[mobile] LoadState threw unknown exception\n");
		ATMobileUI_ClearSuspendState();
	}
	return false;
}

void ATMobileUI_ClearSuspendState() {
	VDStringW path = QuickSaveStatePath();
	if (VDDoesPathExist(path.c_str()))
		VDRemoveFile(path.c_str());
}
