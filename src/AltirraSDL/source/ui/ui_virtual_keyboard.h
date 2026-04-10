//	AltirraSDL - Virtual on-screen keyboard
//	Displays an Atari XL/XE keyboard image with clickable/touchable keys.
//	Supports mouse, touch (Android), and gamepad D-pad navigation.

#pragma once

class ATSimulator;
struct ATUIState;
union SDL_Event;

// Placement modes for the virtual keyboard panel
enum ATOSKPlacement {
	kOSKPlacement_Auto   = 0,	// Pick bottom or right based on aspect ratio
	kOSKPlacement_Bottom = 1,	// Below the emulator display
	kOSKPlacement_Right  = 2,	// To the right of the emulator display
};

void ATUIVirtualKeyboard_Shutdown();

// Main render function — call from ATUIRenderFrame().
// Returns true if the keyboard requested to be closed (close button pressed).
bool ATUIRenderVirtualKeyboard(ATSimulator &sim, bool visible, int placement);

// Handle SDL events (gamepad D-pad/A for navigation, touch for mobile).
// Returns true if the event was consumed by the virtual keyboard.
bool ATUIVirtualKeyboard_HandleEvent(const SDL_Event &ev, ATSimulator &sim, bool visible);

// Release all held keys and console switches (call on focus loss, pause, menu open).
void ATUIVirtualKeyboard_ReleaseAll(ATSimulator &sim);

// Returns the pixel insets the keyboard panel needs from the emulator viewport.
// outBottom and outRight are set based on current placement mode.
void ATUIVirtualKeyboard_GetDisplayInset(bool visible, int placement, float *outBottom, float *outRight);
