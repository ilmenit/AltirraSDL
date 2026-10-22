#include <stdafx.h>
#include "ui_profile_services.h"
#include "simulator.h"
#include <vd2/system/registry.h>
#include <vd2/system/vdstl.h>
#include <algorithm>

extern ATSimulator g_sim;
extern void ATRegistryFlushToDisk();

namespace {
	uint32 g_gameProfile = kATProfileId_Invalid;
	constexpr auto kGameCategories = (ATSettingsCategory)(kATSettingsCategory_All
		& ~(kATSettingsCategory_MountedImages | kATSettingsCategory_FullScreen));
	// Same categories and order as Windows uiprofiles.cpp.
	const ATUIProfileCategory kCategories[] = {
		{kATSettingsCategory_Hardware, "Hardware", "Computer, CPU, memory and PAL/NTSC"},
		{kATSettingsCategory_Firmware, "Firmware", "Operating system and BASIC ROM selections"},
		{kATSettingsCategory_Acceleration, "Acceleration", "Fast boot and SIO patches"},
		{kATSettingsCategory_Debugging, "Debugging", "Debugger and memory randomization"},
		{kATSettingsCategory_Devices, "Devices", "Attached devices and their configuration"},
		{kATSettingsCategory_StartupConfig, "Startup config.", "BASIC and console switch startup state"},
		{kATSettingsCategory_Environment, "Environment", "Emulator behavior and ease of use"},
		{kATSettingsCategory_Color, "Color", "Color palettes and artifacting colors"},
		{kATSettingsCategory_View, "View", "Display scaling, filters and effects"},
		{kATSettingsCategory_InputMaps, "Input maps", "Controller mapping definitions"},
		{kATSettingsCategory_Input, "Input", "Active maps, keyboard and light pen"},
		{kATSettingsCategory_Speed, "Speed", "Warp and emulation speed"},
		{kATSettingsCategory_MountedImages, "Mounted images", "References to disks, cartridges and tapes"},
		{kATSettingsCategory_FullScreen, "Full screen", "Windowed or fullscreen state"},
		{kATSettingsCategory_Sound, "Audio", "Audio configuration"},
		{kATSettingsCategory_Boot, "Boot", "Boot Image behavior"},
		{kATSettingsCategory_NVRAM, "Device NVRAM", "Device non-volatile memory settings"},
	};
	static_assert((1U << vdcountof(kCategories)) - 1 == kATSettingsCategory_AllCategories);
}

const ATUIProfileCategory *ATUIProfileCategories(size_t& count) {
	count = vdcountof(kCategories);
	return kCategories;
}

bool ATUIProfileCanParent(uint32 profile, uint32 parent) {
	vdfastvector<uint32> seen;
	while (parent) {
		if (parent == profile || !ATSettingsIsValidProfile(parent)
			|| std::find(seen.begin(), seen.end(), parent) != seen.end())
			return false;
		seen.push_back(parent);
		parent = ATSettingsProfileGetParent(parent);
	}
	return profile != 0;
}

uint32 ATUICreateProfileCopy(bool independent) {
	const uint32 previous = ATSettingsGetCurrentProfileId();
	const bool temporary = ATSettingsGetTemporaryProfileMode();
	const bool bootstrap = ATSettingsGetBootstrapProfileMode();
	ATSaveSettings(kATSettingsCategory_All);
	const uint32 id = ATSettingsGenerateProfileId();
	ATSettingsProfileSetName(id, L"New Profile");
	ATSettingsProfileSetVisible(id, true);
	ATSettingsProfileSetParent(id, independent ? 0 : previous);
	ATSettingsProfileSetCategoryMask(id, independent
		? kATSettingsCategory_All : kATSettingsCategory_None);
	if (independent) {
		// Capture the live configuration without loading or resetting hardware.
		ATSettingsLoadProfile(id, kATSettingsCategory_None);
		try {
			ATSaveSettings(kATSettingsCategory_All);
		} catch (...) {
			ATSettingsLoadProfile(previous, kATSettingsCategory_None);
			ATSettingsSetTemporaryProfileMode(temporary);
			ATSettingsSetBootstrapProfileMode(bootstrap);
			ATSettingsProfileDelete(id);
			throw;
		}
		ATSettingsLoadProfile(previous, kATSettingsCategory_None);
		ATSettingsSetTemporaryProfileMode(temporary);
		ATSettingsSetBootstrapProfileMode(bootstrap);
	}
	ATRegistryFlushToDisk();
	return id;
}

void ATUIActivateGameProfile(uint32 profile) {
	if (!ATSettingsIsValidProfile(profile))
		throw MyError("The assigned profile is missing. Assign an available profile in Game Library.");
	ATSaveSettings(kATSettingsCategory_All);
	// A game's selected image is loaded by the boot action. Do not restore
	// unrelated mounted images or change fullscreen while selecting its setup.
	ATSettingsLoadProfile(profile, kGameCategories);
	ATSettingsSetTemporaryProfileMode(true);
	g_gameProfile = profile;
}

bool ATUIIsGameProfileSession() {
	return g_gameProfile == ATSettingsGetCurrentProfileId()
		&& ATSettingsGetTemporaryProfileMode();
}

uint32 ATUIGetGameProfileMarker() { return g_gameProfile; }
void ATUIRestoreGameProfileMarker(uint32 profile) { g_gameProfile = profile; }

void ATUIUpdateGameProfile() {
	if (!ATUIIsGameProfileSession())
		return;
	ATSettingsSetTemporaryProfileMode(false);
	try {
		// Updating a shared preset must not alter its inherited parent.
		ATSaveSettings((ATSettingsCategory)(
			ATSettingsProfileGetCategoryMask(g_gameProfile) & kGameCategories));
	} catch (...) {
		ATSettingsSetTemporaryProfileMode(true);
		throw;
	}
	ATSettingsSetTemporaryProfileMode(true);
	ATRegistryFlushToDisk();
}

void ATUISwitchProfile(uint32 profile) {
	if (ATUIIsGameProfileSession()) {
		// Reload even when choosing the same profile: discard session changes.
		ATSettingsLoadProfile(profile, kATSettingsCategory_All);
		g_sim.ColdReset();
		VDRegistryAppKey key("Profiles", true);
		key.setInt("Current profile", profile);
	} else {
		ATSettingsSwitchProfile(profile);
	}
	g_gameProfile = kATProfileId_Invalid;
}
