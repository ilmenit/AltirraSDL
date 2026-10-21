// The exchange shared by profile callbacks and the isolated persistence test.
#pragma once
#include <vd2/system/registry.h>
#include <cartridge.h>

inline void ATUIExchangeRamCartSwitchDefaults(bool write, VDRegistryKey& key) {
	for (int mapper = 94; mapper <= 103; ++mapper) {
		const auto mode = ATGetCartridgeModeForMapper(mapper);
		VDStringA name;
		name.sprintf("RAMCart: Physical switches %d", mapper);
		if (write)
			key.setInt(name.c_str(), ATGetRamCartDefaultSwitches(mode));
		else
			// Each category is read from its owning profile key. Missing new
			// values mean factory defaults, never the previously active profile.
			ATSetRamCartDefaultSwitches(mode, key.getInt(name.c_str(), 0));
	}
}
