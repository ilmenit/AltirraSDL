//	AltirraSDL - Menu bar internal header
//	Shared helpers and per-menu Render entry points used by the main
//	menu bar dispatcher (ui_menus.cpp) and the individual menu files.

#ifndef f_AT_UI_MENUS_INTERNAL_H
#define f_AT_UI_MENUS_INTERNAL_H

struct SDL_Window;
class ATSimulator;
class ATInputManager;
struct ATUIState;
class IDisplayBackend;

// Helper: right-click "Assign Keyboard Shortcut..." context menu.
// Call after ImGui::MenuItem() for items with an accel command name.
// (defined in ui_menus.cpp)
void ATUIMenuShortcutContextMenu(const char *command);

// Per-menu render entry points. Each extracted menu is defined in its
// own .cpp file; the rest remain in ui_menus.cpp.
void ATUIRenderViewMenu(ATSimulator &sim, ATUIState &state, SDL_Window *window, IDisplayBackend *backend);
void ATUIRenderSystemMenu(ATSimulator &sim, ATUIState &state);
void ATUIRenderDebugMenu(ATSimulator &sim);
void ATUIRenderOnlineMenu();

#endif
