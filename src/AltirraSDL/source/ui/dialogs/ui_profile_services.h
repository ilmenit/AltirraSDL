#ifndef f_AT_UI_PROFILE_SERVICES_H
#define f_AT_UI_PROFILE_SERVICES_H

#include "settings.h"
#include <cstddef>

struct ATUIProfileCategory {
	ATSettingsCategory bit;
	const char *name;
	const char *description;
};
const ATUIProfileCategory *ATUIProfileCategories(size_t& count);
bool ATUIProfileCanParent(uint32 profile, uint32 parent);
uint32 ATUICreateProfileCopy(bool independent);
void ATUIActivateGameProfile(uint32 profile);
bool ATUIIsGameProfileSession();
// Transactional media loads restore this marker with the original profile.
uint32 ATUIGetGameProfileMarker();
void ATUIRestoreGameProfileMarker(uint32 profile);
void ATUIUpdateGameProfile();
void ATUISwitchProfile(uint32 profile);

#endif
