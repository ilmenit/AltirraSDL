//	AltirraSDL - UI mode (Desktop / Gaming)
//	Runtime switch between the traditional menu-bar interface and
//	the simplified, gamepad/touch-friendly Gaming Mode UI.

#pragma once

#include <vd2/system/vdtypes.h>

enum class ATUIMode : uint8 {
	Desktop,	// Traditional menu bar + ImGui dialogs
	Gaming		// Touch/gamepad-friendly fullscreen UI (hamburger menu, large buttons)
};

ATUIMode ATUIGetMode();
void     ATUISetMode(ATUIMode mode);
bool     ATUIIsGamingMode();

// Load/save the UI mode from/to the persistent registry.
// Called from main_sdl3.cpp during startup and on mode switch.
void ATUILoadMode();
void ATUISaveMode();

// Apply the visual style appropriate for the current UI mode
// (font scaling, touch target sizes, ImGui nav flags).
// Called after mode changes and during initial UI setup.
void ATUIApplyModeStyle(float contentScale);
