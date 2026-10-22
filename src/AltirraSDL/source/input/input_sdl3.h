#pragma once
#include <SDL3/SDL.h>

class ATPokeyEmulator;
class ATInputManager;
class ATGTIAEmulator;

void ATInputSDL3_Shutdown();
void ATInputSDL3_EndHoldKeys();
// Change UI collection mode without altering pending simulator input.
void ATInputSDL3_SetHoldKeysMode(bool enabled);
void ATInputSDL3_Init(ATPokeyEmulator *pokey, ATInputManager *inputMgr, ATGTIAEmulator *gtia);
void ATInputSDL3_HandleKeyDown(const SDL_KeyboardEvent& ev);
void ATInputSDL3_HandleKeyUp(const SDL_KeyboardEvent& ev);
void ATInputSDL3_HandleTextInput(const char *text);
void ATInputSDL3_ReleaseAllKeys();
// Controller mappings use physical keys, including during binding capture.
uint32_t ATInputSDL3_GetInputCode(SDL_Scancode scancode);

bool ATInputSDL3_IsHoldingKeys();
