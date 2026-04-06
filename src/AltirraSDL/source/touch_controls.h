//	AltirraSDL - Virtual touch controls for Android
//	Renders joystick/d-pad, fire buttons, and console keys as ImGui overlay.
//	Routes touch input to ATInputManager and GTIA console switches.

#pragma once

#include <SDL3/SDL.h>
#include "touch_layout.h"

class ATInputManager;
class ATGTIAEmulator;
class ATSimulator;

// Initialize touch controls — must be called after ATInputSDL3_Init()
void ATTouchControls_Init(ATInputManager *inputMgr, ATGTIAEmulator *gtia);

// Shutdown and release all held inputs
void ATTouchControls_Shutdown();

// Process an SDL touch event. Returns true if the event was consumed
// (i.e. it landed in a control zone and should not be forwarded to ImGui).
bool ATTouchControls_HandleEvent(const SDL_Event &ev, const ATTouchLayout &layout,
	const ATTouchLayoutConfig &config);

// Release all held touch inputs (call on focus loss, pause, menu open)
void ATTouchControls_ReleaseAll();

// Render touch controls as ImGui overlay
void ATTouchControls_Render(const ATTouchLayout &layout, const ATTouchLayoutConfig &config);

// Returns true if any touch control is currently active (finger down)
bool ATTouchControls_IsActive();

// Enable/disable haptic feedback pulses on button taps and direction
// changes.  When on, every actionable touch triggers an Android
// Vibrator pulse (via ATAndroid_Vibrate).  Default: on.
void ATTouchControls_SetHapticEnabled(bool enabled);
