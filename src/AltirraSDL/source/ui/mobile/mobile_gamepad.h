//	AltirraSDL - Mobile UI: gamepad navigation
//	Lets the on-screen mobile UI (hamburger drawer, settings,
//	dialogs) be driven entirely from a gamepad without putting it
//	down to tap the screen.  Touch coexists — the focus ring only
//	appears after gamepad input is received.
//
//	Two reserved global buttons:
//	  Start  → cold-boot the loaded image / toggle pause
//	  Back   → toggle the hamburger drawer
//	All other UI navigation (D-pad, A=activate, B=back, L1/R1 tabs)
//	is handled automatically by Dear ImGui's NavEnableGamepad once
//	the flag is set in ATMobileGamepad_Init().

#pragma once

union SDL_Event;
class ATSimulator;
struct ATMobileUIState;

// Enable ImGui gamepad navigation and stylise the focus ring.
// Idempotent; safe to call repeatedly.
void ATMobileGamepad_Init();

// Intercept SDL_EVENT_GAMEPAD_BUTTON_DOWN for the two reserved
// buttons (Start, Back).  Returns true if the event was consumed
// — caller should NOT forward it to ImGui or to the joystick
// manager when the return value is true.
bool ATMobileGamepad_HandleEvent(const SDL_Event &ev,
	ATSimulator &sim, ATMobileUIState &mobileState);

// True whenever the mobile UI is currently displaying a screen
// that should own the gamepad (drawer, settings, file browser,
// dialogs, etc.).  When true, joystick_sdl3 must NOT dispatch
// game input to ATInputManager — the gamepad belongs to ImGui.
bool ATMobileGamepad_IsUIOwning();

// Track UI ownership so joystick_sdl3 can query without pulling in
// ATMobileUIState.  Called from ATMobileUI_Render each frame.
void ATMobileGamepad_SetUIOwning(bool owning);

// True iff the given SDL gamepad button index is one of the two
// UI-reserved buttons (Start or Back).  joystick_sdl3 uses this to
// mask those buttons out of game input on mobile builds.
bool ATMobileGamepad_IsReservedButton(int sdlGamepadButton);
