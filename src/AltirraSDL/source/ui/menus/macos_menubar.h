//	AltirraSDL - Native macOS menu bar
//	Provides a native NSMenu-based menu bar on macOS, replacing the ImGui
//	menu bar for a platform-native look and feel.  On non-Apple platforms
//	these functions are no-ops (the header is still includable).

#ifndef f_AT_MACOS_MENUBAR_H
#define f_AT_MACOS_MENUBAR_H

#ifdef VD_OS_MACOS

// Initialise the native macOS menu bar.  Call once after SDL window
// creation and ImGui init.  Replaces SDL3's default application menu
// with a full Altirra menu bar (Application, File, View, System, etc.).
void ATMacMenuBarInit();

// Tear down the native menu bar.  Call before SDL_DestroyWindow().
void ATMacMenuBarShutdown();

// Returns true when the native macOS menu bar is active and the ImGui
// menu bar should be skipped.
bool ATMacMenuBarIsActive();

#else // !VD_OS_MACOS

inline void ATMacMenuBarInit() {}
inline void ATMacMenuBarShutdown() {}
inline bool ATMacMenuBarIsActive() { return false; }

#endif

#endif // f_AT_MACOS_MENUBAR_H
