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
// showControls gates the joystick / fire / console-key hit tests;
// showMenu gates the hamburger button hit test.  Either or both may be
// false (when both are false, nothing is hit-tested and the function
// never claims the event).
bool ATTouchControls_HandleEvent(const SDL_Event &ev, const ATTouchLayout &layout,
	const ATTouchLayoutConfig &config,
	bool showControls, bool showMenu);

// Release all held touch inputs (call on focus loss, pause, menu open)
void ATTouchControls_ReleaseAll();

// Render touch controls as ImGui overlay.  showControls toggles the
// joystick / fire / console-key layer; showMenu toggles the hamburger
// icon.  The two are independent so a user watching a demo can leave
// the menu visible while hiding the gameplay controls.
void ATTouchControls_Render(const ATTouchLayout &layout, const ATTouchLayoutConfig &config,
	bool showControls, bool showMenu);

// Returns true if any touch control is currently active (finger down)
bool ATTouchControls_IsActive();

// Enable/disable haptic feedback pulses on button taps and direction
// changes.  When on, every actionable touch triggers an Android
// Vibrator pulse (via ATAndroid_Vibrate).  Default: on.
void ATTouchControls_SetHapticEnabled(bool enabled);

// Show/hide the console-key row (START/SELECT/OPTION/>>) independently
// of the rest of the touch controls.  Embedder hook (e.g. a JS shell
// that auto-hides the row after a few seconds of inactivity, or shows
// START only while a game is at its titlescreen).  The hamburger menu
// button is unaffected.  Hiding releases any currently-held console
// switch.  Default: visible.
void ATTouchControls_SetConsoleKeysVisible(bool visible);
bool ATTouchControls_GetConsoleKeysVisible();
