//	AltirraSDL - Persistence for fork-only UI state.
//
//	Fork-only per-profile settings hook into the same load/save lifecycle
//	as the native settings.cpp categories via the registered-callback
//	mechanism (ATSettingsRegisterLoadCallback / ATSettingsRegisterSaveCallback
//	in settings.h).  The values land in the same per-profile registry key
//	as the native View-category data, so they get profile inheritance,
//	temporary-profile suppression (netplay sessions), and the
//	exit/suspend/profile-switch save points without any edits to the
//	upstream-tracked settings.cpp.
//
//	Storage rule (see CLAUDE.md "Persisting fork-only settings"): a value
//	belongs here only if Windows would store it per-profile.  Machine-
//	global fork state (shader preset paths, mobile layout) uses its own
//	VDRegistryAppKey namespace instead.

#include <stdafx.h>

#include <vd2/system/registry.h>
#include <settings.h>

#include "ui_main.h"

extern ATUIState g_uiState;

namespace {

ATSettingsLoadSaveCallback gLoadCallback;
ATSettingsLoadSaveCallback gSaveCallback;
bool gCallbacksRegistered = false;

void LoadCallback(uint32 /*profileId*/, ATSettingsCategory mask, VDRegistryKey& key) {
	if (!(mask & kATSettingsCategory_View))
		return;

	// Screen effects base mode (None vs Basic).  Preset mode is derived
	// state — active exactly when a librashader preset is loaded, which
	// persists via the machine-global Shader Presets\LastPreset key and
	// is restored by ATUIShaderPresetsAutoLoad before the initial
	// settings load — so an active preset must not be clobbered here.
	if (g_uiState.screenEffectsMode != ATUIState::kSFXMode_Preset) {
		const bool enabled = key.getBool("View: Screen effects enabled",
			g_uiState.screenEffectsMode != ATUIState::kSFXMode_None);
		g_uiState.screenEffectsMode = enabled
			? ATUIState::kSFXMode_Basic
			: ATUIState::kSFXMode_None;
	}

	int placement = key.getInt("View: Virtual keyboard placement",
		g_uiState.oskPlacement);
	if (placement < 0 || placement > 2)
		placement = 0;
	g_uiState.oskPlacement = placement;
}

void SaveCallback(uint32 /*profileId*/, ATSettingsCategory mask, VDRegistryKey& key) {
	if (!(mask & kATSettingsCategory_View))
		return;

	// While a preset is active, leave the base None/Basic key untouched
	// so the user's last explicit choice survives — it is the fallback
	// applied when the saved preset fails to load on startup.
	if (g_uiState.screenEffectsMode != ATUIState::kSFXMode_Preset) {
		key.setBool("View: Screen effects enabled",
			g_uiState.screenEffectsMode != ATUIState::kSFXMode_None);
	}

	key.setInt("View: Virtual keyboard placement", g_uiState.oskPlacement);
}

} // namespace

void ATUIStateSettingsInit() {
	if (gCallbacksRegistered)
		return;

	gLoadCallback = LoadCallback;
	gSaveCallback = SaveCallback;
	ATSettingsRegisterLoadCallback(&gLoadCallback);
	ATSettingsRegisterSaveCallback(&gSaveCallback);
	gCallbacksRegistered = true;
}

void ATUIStateSettingsShutdown() {
	if (!gCallbacksRegistered)
		return;

	ATSettingsUnregisterLoadCallback(&gLoadCallback);
	ATSettingsUnregisterSaveCallback(&gSaveCallback);
	gCallbacksRegistered = false;
}
