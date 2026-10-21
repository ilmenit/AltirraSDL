//	AltirraSDL - Mobile UI: suspend/restore save state
//	Per-game checkpoints and process-restart recovery.
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
#include <vd2/system/registry.h>
#include <at/atcore/media.h>
#include <at/atio/image.h>

#include "ui_mobile.h"
#include "simulator.h"
#include "ui_main.h"

#include "mobile_internal.h"

extern void ATRegistryFlushToDisk();

namespace {

constexpr const char *kSuspendKey = "Mobile";
constexpr const char *kSuspendPath = "Suspend state path";
constexpr const char *kSuspendMedia = "Suspend media path";
constexpr const char *kSuspendPaused = "Suspend paused";

void ClearSuspendRecord(const VDStringW& path, bool clearAny = false) {
	VDRegistryAppKey key(kSuspendKey, true);
	VDStringW recordedPath;
	if (key.getString(kSuspendPath, recordedPath)
		&& (clearAny || recordedPath == path))
	{
		key.removeValue(kSuspendPath);
		key.removeValue(kSuspendMedia);
		key.removeValue(kSuspendPaused);
		ATRegistryFlushToDisk();
	}
}

} // namespace

VDStringW ATMobileUI_GetSuspendStatePath() {
	return QuickSaveStatePath();
}

void ATMobileUI_SaveSuspendState(ATSimulator &sim,
	const ATMobileUIState &mobileState)
{
	// No "autoSaveOnSuspend" self-gate here — the caller decides
	// whether to invoke this based on the relevant policy flag (the
	// background path checks autoSaveOnSuspend; the Exit Emulator
	// path checks saveStateOnExit).  We only short-circuit when there
	// is genuinely nothing to snapshot (no game loaded).  Do not
	// clear here: quicksaves are per-game, and the last boot path may
	// still identify an earlier game whose save must remain intact.
	if (!mobileState.gameLoaded) {
		return;
	}
	VDStringW path = QuickSaveStatePath();
	VDStringW temporaryPath = path;
	temporaryPath += L".tmp";
	try {
		// SaveState truncates its destination. Replace the previous complete
		// checkpoint only after serialization has closed the sibling file.
		sim.SaveState(temporaryPath.c_str());
		const VDStringA temporaryU8 = VDTextWToU8(temporaryPath);
		const VDStringA pathU8 = VDTextWToU8(path);
		if (!SDL_RenamePath(temporaryU8.c_str(), pathU8.c_str()))
			throw MyError("Cannot replace suspend state: %s", SDL_GetError());

		// This is a machine-global recovery pointer, not a profile setting.
		// XEX files have no mounted media after restart from which to derive
		// their per-game slot. Publish its identity only after saving succeeds.
		{
			VDRegistryAppKey key(kSuspendKey, true);
			key.setString(kSuspendPath, path.c_str());
			key.setString(kSuspendMedia,
				ATGetLastBootMediaPathSDL3().c_str());
			key.setBool(kSuspendPaused,
				MobileSessionWasPaused(sim, mobileState));
		}
		ATRegistryFlushToDisk();
	} catch (const MyError &e) {
		VDStringA u8 = VDTextWToU8(path);
		fprintf(stderr, "[mobile] SaveState(%s) failed: %s\n",
			u8.c_str(), e.c_str());
	} catch (...) {
		fprintf(stderr, "[mobile] SaveState threw unknown exception\n");
	}
	// Failed serialization/replacement can leave a partial sibling behind.
	// It is never used for recovery, and must not replace the old checkpoint.
	VDRemoveFile(temporaryPath.c_str());
}

bool ATMobileUI_RestoreSuspendState(ATSimulator &sim,
	ATMobileUIState &mobileState)
{
	if (!mobileState.autoRestoreOnStart)
		return false;
	VDStringW path = QuickSaveStatePath();
	VDStringW bootPath;
	bool paused = false;
	bool haveRecordedPath = false;
	{
		VDRegistryAppKey key(kSuspendKey, false);
		VDStringW recordedPath;
		if (key.getString(kSuspendPath, recordedPath)
			&& !recordedPath.empty())
		{
			path = recordedPath;
			haveRecordedPath = true;
			key.getString(kSuspendMedia, bootPath);
			paused = key.getBool(kSuspendPaused, false);
		}
	}
	if (!VDDoesPathExist(path.c_str()))
		return false;

	try {
		ATImageLoadContext ctx{};
		if (sim.Load(path.c_str(), kATMediaWriteMode_RO, &ctx)) {
			if (paused)
				sim.Pause();
			else
				sim.Resume();
			if (haveRecordedPath)
				ATSetLastBootMediaPathSDL3(bootPath);
			mobileState.gameLoaded = true;
			mobileState.currentScreen = ATMobileUIScreen::None;
			return true;
		}
	} catch (const MyError &e) {
		VDStringA u8 = VDTextWToU8(path);
		fprintf(stderr, "[mobile] LoadState(%s) failed: %s\n",
			u8.c_str(), e.c_str());
		VDRemoveFile(path.c_str());
		ClearSuspendRecord(path);
	} catch (...) {
		fprintf(stderr, "[mobile] LoadState threw unknown exception\n");
		VDRemoveFile(path.c_str());
		ClearSuspendRecord(path);
	}
	return false;
}

void ATMobileUI_ClearSuspendState() {
	VDStringW path = QuickSaveStatePath();
	if (VDDoesPathExist(path.c_str()))
		VDRemoveFile(path.c_str());
	// "Exit without saving" must not revive another game's older recovery
	// pointer. Its per-game quicksave remains available for manual loading.
	ClearSuspendRecord(path, true);
}
