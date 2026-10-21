// Fork-owned RAM-Cart settings: physical switches belong to the Hardware
// profile category, as does Windows' Console: Cartridge switch preference.
#include <stdafx.h>
#include <vd2/system/registry.h>
#include <cartridge.h>
#include <settings.h>
#include <simulator.h>
#include "ramcart_settings.h"
#include "ramcart_settings_storage.h"

extern ATSimulator g_sim;
namespace {
ATSettingsLoadSaveCallback gLoad, gSave;
bool gRegistered = false;

void Load(uint32, ATSettingsCategory mask, VDRegistryKey& key) {
	if (!(mask & kATSettingsCategory_Hardware))
		return;
	ATUIExchangeRamCartSwitchDefaults(false, key);
	for (uint32 unit = 0; unit < 2; ++unit) {
		if (auto *cart = g_sim.GetCartridge(unit))
			cart->SetRamCartSwitches(ATGetRamCartDefaultSwitches(cart->GetMode()));
	}
}

void Save(uint32, ATSettingsCategory mask, VDRegistryKey& key) {
	if (!(mask & kATSettingsCategory_Hardware))
		return;
	ATUIExchangeRamCartSwitchDefaults(true, key);
}
}

void ATUIRamCartSettingsInit() {
	if (gRegistered)
		return;
	gLoad = Load;
	gSave = Save;
	ATSettingsRegisterLoadCallback(&gLoad);
	ATSettingsRegisterSaveCallback(&gSave);
	gRegistered = true;
}

void ATUIRamCartSettingsShutdown() {
	if (!gRegistered)
		return;
	ATSettingsUnregisterLoadCallback(&gLoad);
	ATSettingsUnregisterSaveCallback(&gSave);
	gRegistered = false;
}
