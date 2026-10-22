#pragma once

struct SDL_Window;

// Call on the main thread after building the UI, before presenting it.
void ATInputSDL3_UpdatePointer(SDL_Window *window);
// Reuse the current visible image geometry for both overlays and mouse input.
bool ATInputSDL3_GetDisplayRect(float& x, float& y, float& w, float& h);
// ImGui owns debugger panes, but the emulator image itself accepts mouse input.
bool ATInputSDL3_CanRouteMouseToDisplay();
