// AltirraSDL transactional media-load session preservation.
#include <stdafx.h>
#include <SDL3/SDL.h>
#include "ui_media_session.h"
#include "ui_file_dialog_sdl3.h"
#include "ui_main.h"
#include "input_sdl3.h"
#include "ui_profile_services.h"
#include "ui/mobile/ui_mobile.h"
#include "simulator.h"
#include "settings.h"
#include "cassette.h"
#include "disk.h"
#include "diskinterface.h"
#include "cartridge.h"
#include "gtia.h"
#include <at/atcore/serializable.h>
#include <at/atio/image.h>
#include <at/atio/diskimage.h>
#include <at/atio/cartridgeimage.h>
#include <at/atio/cassetteimage.h>
#include <vd2/system/error.h>
#include <vd2/system/refcount.h>
#include <vd2/system/registry.h>
#include <array>

extern ATMobileUIState g_mobileState;

struct ATUIMediaSessionBackup::Impl {
	struct Disk {
		vdrefptr<IATDiskImage> image;
		VDStringW path;
		ATMediaWriteMode writeMode {};
		bool persistent = false;
		bool enabled = false;
	};
	ATSimulator *sim = nullptr;
	ATUIState *ui = nullptr;
	ATUIState uiState;
	std::array<Disk, 15> disks;
	std::array<vdrefptr<IATCartridgeImage>, 2> carts;
	vdrefptr<IATCassetteImage> tape;
	VDStringW tapePath;
	uint32 tapePosition = 0;
	bool tapePersistent = false;
	bool tapeDirty = false;
	bool tapePlay = false;
	bool tapeRecord = false;
	bool tapePaused = false;
	vdrefptr<IATSerializable> snapshot;
	uint32 profile = kATProfileId_Invalid;
	uint32 scratchProfile = kATProfileId_Invalid;
	uint32 persistedProfile = 0;
	uint32 gameProfile = kATProfileId_Invalid;
	bool temporary = false;
	bool bootstrap = false;
	bool running = false;
	bool paused = true;
	bool holdKeysMode = false;
	ATMobileUIScreen screen {};
	bool gameLoaded = false;
	VDStringW lastBootPath;
	~Impl() {
		// Only remove our hidden scratch key; never restore live state from
		// a destructor, which can also run during application shutdown.
		if (scratchProfile != kATProfileId_Invalid) {
			try { ATSettingsProfileDelete(scratchProfile); } catch (...) {}
		}
	}
};

ATUIMediaSessionBackup::ATUIMediaSessionBackup() = default;
ATUIMediaSessionBackup::~ATUIMediaSessionBackup() = default;
void ATUIMediaSessionBackup::Discard() { mpImpl.reset(); }

void ATUIMediaSessionBackup::Commit(bool newGameProfile) {
	if (!mpImpl) return;
	ATSettingsSetTemporaryProfileMode(newGameProfile || mpImpl->temporary);
	ATSettingsSetBootstrapProfileMode(newGameProfile ? false : mpImpl->bootstrap);
	Discard();
}

bool ATUIMediaSessionBackup::Capture(ATSimulator& sim, ATUIState& ui,
	std::string& error)
{
	error.clear();
	try {
		auto saved = std::make_unique<Impl>();
		saved->sim = &sim;
		saved->ui = &ui;
		saved->uiState = ui;
		saved->profile = ATSettingsGetCurrentProfileId();
		VDRegistryAppKey profileKey("Profiles", false);
		saved->persistedProfile = profileKey.getInt("Current profile", saved->profile);
		saved->temporary = ATSettingsGetTemporaryProfileMode();
		saved->bootstrap = ATSettingsGetBootstrapProfileMode();
		saved->gameProfile = ATUIGetGameProfileMarker();
		// A picker lease temporarily suspends execution without pausing it.
		// Its lease can finish while a later media warning is still open;
		// restore the user's logical run state, not that temporary suspend.
		saved->running = sim.IsRunning() || ATUIIsFileDialogResumePending();
		saved->paused = sim.IsPaused();
		saved->holdKeysMode = ATInputSDL3_IsHoldingKeys();
		saved->screen = g_mobileState.currentScreen;
		saved->gameLoaded = g_mobileState.gameLoaded;
		saved->lastBootPath = ATGetLastBootMediaPathSDL3();
		if (!ATSettingsIsValidProfile(saved->profile)) {
			error = "The current settings profile is unavailable.";
			return false;
		}
		// Ordinary profiles can persist their live state normally. A game
		// session instead needs an independent hidden settings copy so its
		// live changes never overwrite the user's preset (or its parents).
		if (!saved->temporary && !saved->bootstrap)
			ATSaveSettings(kATSettingsCategory_All);
		else {
			saved->scratchProfile = ATSettingsGenerateProfileId();
			if (!saved->scratchProfile) {
				error = "Could not allocate a temporary settings profile.";
				saved->scratchProfile = kATProfileId_Invalid;
				return false;
			}
			ATSettingsProfileSetName(saved->scratchProfile,
				L"Temporary media-load rollback");
			ATSettingsProfileSetVisible(saved->scratchProfile, false);
			ATSettingsProfileSetParent(saved->scratchProfile, 0);
			ATSettingsProfileSetCategoryMask(saved->scratchProfile,
				kATSettingsCategory_All);
			try {
				ATSettingsLoadProfile(saved->scratchProfile,
					kATSettingsCategory_None);
				ATSaveSettings(kATSettingsCategory_All);
			} catch (...) {
				ATSettingsLoadProfile(saved->profile, kATSettingsCategory_None);
				ATSettingsSetTemporaryProfileMode(saved->temporary);
				ATSettingsSetBootstrapProfileMode(saved->bootstrap);
				ATUIRestoreGameProfileMarker(saved->gameProfile);
				throw;
			}
			ATSettingsLoadProfile(saved->profile, kATSettingsCategory_None);
			ATSettingsSetTemporaryProfileMode(saved->temporary);
			ATSettingsSetBootstrapProfileMode(saved->bootstrap);
			ATUIRestoreGameProfileMarker(saved->gameProfile);
		}
		for (unsigned i = 0; i < saved->disks.size(); ++i) {
			auto& disk = sim.GetDiskInterface(i);
			auto& entry = saved->disks[i];
			entry.image = disk.GetDiskImage();
			entry.path = disk.GetPath();
			entry.writeMode = disk.GetWriteMode();
			entry.persistent = disk.IsDiskBacked();
			entry.enabled = sim.GetDiskDrive(i).IsEnabled();
		}
		for (unsigned i = 0; i < saved->carts.size(); ++i)
			if (auto *cart = sim.GetCartridge(i)) saved->carts[i] = cart->GetImage();
		auto& tape = sim.GetCassette();
		saved->tape = tape.GetImage();
		saved->tapePath = tape.GetPath();
		saved->tapePosition = tape.GetSamplePos();
		saved->tapePersistent = tape.IsImagePersistent();
		saved->tapeDirty = tape.IsImageDirty();
		saved->tapePlay = tape.IsPlayEnabled();
		saved->tapeRecord = tape.IsRecordEnabled();
		saved->tapePaused = tape.IsPaused();
		sim.CreateSnapshot(~saved->snapshot, nullptr);
		if (!saved->snapshot) {
			error = "The current emulation session could not be preserved.";
			return false;
		}
		mpImpl = std::move(saved);
		return true;
	} catch (const MyError& e) {
		error = e.c_str();
	} catch (...) {
		error = "Unexpected error while preserving the current session.";
	}
	return false;
}

bool ATUIMediaSessionBackup::Restore(std::string& error) {
	error.clear();
	if (!mpImpl) {
		error = "No preserved emulation session is available.";
		return false;
	}
	auto& saved = *mpImpl;
	auto& sim = *saved.sim;
	sim.Pause();
	bool restored = false;
	try {
		// Load the old configuration without saving the abandoned target
		// profile and without reopening filenames over unsaved image data.
		ATSettingsLoadProfile(saved.scratchProfile != kATProfileId_Invalid
			? saved.scratchProfile : saved.profile, (ATSettingsCategory)(
			kATSettingsCategory_All & ~kATSettingsCategory_MountedImages));
		for (unsigned i = 0; i < saved.disks.size(); ++i) {
			const auto& entry = saved.disks[i];
			auto& disk = sim.GetDiskInterface(i);
			if (entry.image)
				disk.LoadDisk(entry.persistent ? entry.path.c_str() : nullptr,
					entry.path.c_str(), entry.image);
			else disk.UnloadDisk();
			disk.SetWriteMode(entry.writeMode);
			sim.GetDiskDrive(i).SetEnabled(entry.enabled);
		}
		for (unsigned i = 0; i < saved.carts.size(); ++i) {
			if (saved.carts[i])
				sim.LoadCartridge(i, saved.carts[i]->GetPath(), saved.carts[i]);
			else sim.UnloadCartridge(i);
		}
		auto& tape = sim.GetCassette();
		tape.Stop();
		if (saved.tape) {
			tape.Load(saved.tape, saved.tapePath.c_str(), saved.tapePersistent);
			tape.SeekToBitPos(saved.tapePosition);
			if (saved.tapeDirty) tape.SetImageDirty();
			if (saved.tapeRecord) tape.Record();
			else if (saved.tapePlay) tape.Play();
			tape.SetPaused(saved.tapePaused);
		} else tape.Unload();
		sim.GetGTIA().DiscardInFlightFrame();
		restored = sim.ApplySnapshot(*saved.snapshot, nullptr);
		if (!restored)
			error = "The preserved emulation snapshot could not be restored.";
	} catch (const MyError& e) {
		error = e.c_str();
	} catch (...) {
		error = "Unexpected error while restoring the previous session.";
	}
	// Loading a scratch profile changes the current ID. Restore only the ID
	// here so the live configuration just restored is not loaded a second time.
	try { ATSettingsLoadProfile(saved.profile, kATSettingsCategory_None); }
	catch (...) {
		restored = false;
		error = "The previous settings profile identity could not be restored.";
	}
	ATSettingsSetTemporaryProfileMode(saved.temporary);
	ATSettingsSetBootstrapProfileMode(saved.bootstrap);
	ATUIRestoreGameProfileMarker(saved.gameProfile);
	try {
		// A hardware-mode switch can update the durable startup profile.
		// A temporary game session may have a different live profile ID.
		VDRegistryAppKey profileKey("Profiles", true);
		profileKey.setInt("Current profile", saved.persistedProfile);
	} catch (...) {
		restored = false;
		error = "The previous startup profile selection could not be restored.";
	}
	if (restored) {
		*saved.ui = saved.uiState;
		ATInputSDL3_SetHoldKeysMode(saved.holdKeysMode);
		g_mobileState.currentScreen = saved.screen;
		g_mobileState.gameLoaded = saved.gameLoaded;
		ATSetLastBootMediaPathSDL3(saved.lastBootPath);
		if (saved.running) sim.Resume();
		else if (saved.paused) sim.Pause();
		else { sim.Resume(); sim.Suspend(); }
	}
	// A failed restore stays paused so partially restored hardware cannot run.
	return restored;
}
