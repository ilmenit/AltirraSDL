//	AltirraSDL - UI mode (Desktop / Gaming)
//	See ui_mode.h for the public API.

#include <stdafx.h>
#include <imgui.h>
#include <SDL3/SDL.h>
#include <vd2/system/registry.h>

#include "ui_mode.h"
#include "mobile_gamepad.h"
#include "logging.h"

static ATUIMode s_uiMode = ATUIMode::Desktop;

// Track the scale factor currently applied to ImGui sizes so we can
// undo it before reapplying.  ScaleAllSizes() is multiplicative,
// so applying 2.0 twice gives 4.0 — we must invert first.
static float s_currentScale = 1.0f;

ATUIMode ATUIGetMode() {
	return s_uiMode;
}

void ATUISetMode(ATUIMode mode) {
	s_uiMode = mode;
}

bool ATUIIsGamingMode() {
	return s_uiMode == ATUIMode::Gaming;
}

void ATUILoadMode() {
	VDRegistryAppKey key("UI", false);
	int mode = key.getInt("Mode", (int)ATUIMode::Desktop);
	if (mode < 0 || mode > (int)ATUIMode::Gaming)
		mode = (int)ATUIMode::Desktop;
	s_uiMode = (ATUIMode)mode;
}

void ATUISaveMode() {
	VDRegistryAppKey key("UI", true);
	key.setInt("Mode", (int)s_uiMode);
	extern void ATRegistryFlushToDisk();
	ATRegistryFlushToDisk();
}

void ATUIApplyModeStyle(float contentScale) {
	ImGuiStyle &style = ImGui::GetStyle();

	// Undo any previous ScaleAllSizes() so we start from the unscaled
	// baseline.  This preserves colors and other non-size style changes
	// (e.g. theme switches) that happened since the last call.
	if (s_currentScale != 1.0f)
		style.ScaleAllSizes(1.0f / s_currentScale);

	if (ATUIIsGamingMode()) {
		ATMobileGamepad_Init();

		style.ScaleAllSizes(contentScale);
		s_currentScale = contentScale;

		ImGuiIO &io = ImGui::GetIO();
		io.MouseDragThreshold = 8.0f * contentScale;

		SDL_DisableScreenSaver();

		LOG_INFO("UI", "Gaming Mode style applied (scale=%.2f)", contentScale);
	} else {
		s_currentScale = 1.0f;

		SDL_EnableScreenSaver();

		LOG_INFO("UI", "Desktop Mode style applied");
	}
}
