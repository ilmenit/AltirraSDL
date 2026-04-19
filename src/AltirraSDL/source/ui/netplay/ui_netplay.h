//	AltirraSDL - Online Play public UI surface
//
//	Bridges the menu bar / hamburger menu into the Netplay screens
//	(owned by ui_netplay_state).  Two render entry points share the
//	same screen state machine: ATNetplayUI_RenderDesktop() for the
//	menu-bar build, ATNetplayUI_RenderMobile() for Gaming Mode.  Both
//	routes are touch-, gamepad-, and keyboard-navigable, and the
//	reusable primitives live in ui_netplay_widgets.
//
//	All six screens (Nickname, Browser, HostSetup, Join*, Waiting,
//	Prefs) are implemented twice — once in ui_netplay_desktop.cpp as
//	centered modals, once in ui_netplay_mobile.cpp as full-screen
//	sheets that respect the SDL3 safe-area insets.  The common state
//	and widgets keep duplication low.
//
//	Lifecycle:
//	  Startup   — ATUIInit() calls ATNetplayUI_Initialize()
//	  Each tick — main loop calls ATNetplayUI_Poll() *after*
//	              ATNetplayGlue::Poll(), then ATNetplayUI_RenderXxx()
//	              from the appropriate renderer path.
//	  Shutdown  — ATUIShutdown() calls ATNetplayUI_Shutdown().

#pragma once

#include <cstdint>

struct SDL_Window;
class ATSimulator;
struct ATUIState;
struct ATMobileUIState;

// --- Lifecycle -----------------------------------------------------------

// Load preferences from the registry, initialise platform notify, seed
// the browser auto-refresh timer.  Idempotent.
void ATNetplayUI_Initialize(SDL_Window *window);

// Save preferences and drop transient state.  Called during app
// teardown *before* ATNetplayGlue::Shutdown().
void ATNetplayUI_Shutdown();

// Drive any per-tick UI work that is independent of render.  Today
// this covers:
//   - Consuming the coordinator's Phase changes and firing
//     notifications/chimes
//   - Auto-refresh of the lobby browser (10 s tick)
//   - Decaying the "just connected" HUD cue
// Call once per main-loop iteration.  `nowMs` is the same SDL_GetTicks
// value the rest of the main loop uses.
void ATNetplayUI_Poll(uint64_t nowMs);

// --- Menu entry points ---------------------------------------------------

// Open the nickname prompt if needed, otherwise jump straight to the
// browser.  Called from the "Online Play" menu.
void ATNetplayUI_OpenBrowser();

// Open the Preferences → Netplay page as a standalone sheet.  Called
// from the menu when no session is active.
void ATNetplayUI_OpenPrefs();

// Open the "My Hosted Games" list.  Called from the menu.  Forces the
// nickname prompt first if the user hasn't set one yet.
void ATNetplayUI_OpenMyHostedGames();

// Cleanly tear down the active session (if any) and return to the
// Browser.  Called from Online Play → End Session and from the
// Waiting panel's Cancel button.
void ATNetplayUI_EndSession();

// --- Render ----- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ----

// Draw the currently active netplay screen in the menu-bar build.
// Draws nothing when screen == Screen::Closed.
void ATNetplayUI_RenderDesktop(ATSimulator &sim, ATUIState &state,
                               SDL_Window *window);

// Draw the currently active netplay screen in Gaming Mode.  The
// hamburger / touch-chrome layer calls this between frames when the
// netplay overlay owns input.  Returns true if the screen consumed
// input this frame (so the caller can skip emitting touch-controls).
bool ATNetplayUI_RenderMobile(ATSimulator &sim, ATUIState &uiState,
                              ATMobileUIState &mobileState,
                              SDL_Window *window);

// True when any netplay screen is open.  Used by the main loop to
// gate pause-on-menu, input-capture dismissal, etc.
bool ATNetplayUI_IsActive();

// Fire a notification (flash + system notify + chime) with the user's
// pref mask applied.  Safe from the UI thread.
void ATNetplayUI_Notify(const char *title, const char *body);

// Called from the deferred host-boot handler when g_sim.Load() fails
// or the retry loop exhausts its options.  Marks the offer as failed
// (sets lastError, flips enabled off so ReconcileHostedGames doesn't spin-
// loop retrying), tears down the coordinator, and notifies the user.
// gameId = UTF-8 id from HostedGame::id; reason = short human text.
void ATNetplayUI_HostBootFailed(const char *gameId, const char *reason);

// Called from the deferred joiner-apply handler when the received
// snapshot fails to load (missing firmware, mode mismatch, corrupt
// bytes).  Tears down the joiner slot, surfaces the error to the
// user via the Error screen, and clears the Waiting modal so the
// joiner doesn't hang forever.
void ATNetplayUI_JoinerSnapshotFailed(const char *reason);
