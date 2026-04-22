//	AltirraSDL - Online Play reusable widgets
//
//	DRY primitives that both the Desktop menu-bar build and the
//	Gaming Mode full-screen flow consume.  Each widget is a thin
//	ImGui wrapper that delegates styling to the existing
//	`touch_widgets.h` API (ATTouchButton, ATMobileGetPalette, etc.)
//	so there is a single source of truth for look-and-feel across
//	both modes.
//
//	Why not just call touch_widgets directly?  Because the shapes
//	here are netplay-specific:
//	  - SessionTile     — one row in the Online browser grid.
//	                      Pulls art from ATGameLibrary / GameArtCache,
//	                      overlays peer handle, region, visibility,
//	                      and a padlock badge for private sessions.
//	  - StatusBadge     — phase label for the waiting panel (colour +
//	                      icon + spinner).
//	  - LobbyLine       — one row in a small lobby-listing dropdown.
//	  - BeginSheet      — opens a mode-appropriate container (centred
//	                      ImGui modal in Desktop; full-screen card
//	                      with safe-area insets in Gaming Mode).
//	  - BeginScreenGrid — common grid layout respecting safe-area
//	                      insets and keyboard/gamepad nav focus.
//
//	All widgets are pure functions — no static state — so they compile
//	and link identically on Windows, macOS, Linux, and Android.

#pragma once

#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ui_netplay_state.h"   // SpecLineToken + MachineConfig

namespace ATNetplay { struct LobbySession; }

namespace ATNetplayUI {

// Machine Configuration form — hardware / video / BASIC / SIO +
// firmware combos + "Copy from current emulator".  Identical layout
// in Desktop and Gaming Mode so both Add-Game flows expose the same
// set of knobs through one DRY implementation.
void RenderMachineConfigSection(MachineConfig& cfg);

// Re-scan the installed firmware list.  Called after the user
// installs/removes firmware via Settings → Firmware so the Add-Game
// dropdowns reflect the fresh state without restart.
void ReloadMachineConfigFirmwareList();

// -----------------------------------------------------------------------
// Mode-aware container helpers.  Both must be followed by
// EndSheet()/EndScreenGrid() exactly once.
// -----------------------------------------------------------------------

// Opens a sheet for the current UI mode:
//   Desktop      — centred modal window with NoSavedSettings.
//   Gaming Mode  — full-screen card with safe-area inset padding and
//                  a translucent backdrop that dims the emulator.
//
// `title` is the window title; `open` is the "X" close-button flag
// (nullable ⇒ no close button).  `minSize`/`maxSize` are hints in
// logical pixels (content-scaled by the caller of the mode layer).
//
// Returns true if the sheet is open and the caller should draw its
// body; false otherwise.  Matches ImGui::Begin semantics.
bool BeginSheet(const char *title, bool *open,
                const ImVec2 &minSize,
                const ImVec2 &maxSize);
void EndSheet();

// Full-bleed grid container — sized to the available client area
// minus safe-area insets on Gaming Mode, sized to the parent's
// content region on Desktop.  Returns the tile size that ComputeTile
// below produces for `columns`.
ImVec2 BeginScreenGrid(int columns,
                       float minTileWidthPx,
                       float tileAspect);
void EndScreenGrid();

// -----------------------------------------------------------------------
// Reusable widgets
// -----------------------------------------------------------------------

// Renders one tile inside BeginScreenGrid().  `artTexId` is a
// SDL_Texture* pointer reinterpreted as a 64-bit handle (matches the
// convention used by Game Library mobile browser).  Pass 0 / nullptr
// when no art exists; a placeholder icon is drawn instead.
//
// Returns true on the frame the user activates the tile (click /
// Enter / gamepad A).
struct TileInfo {
	const char *title       = "";      // cart name — bold top line
	const char *subtitle    = nullptr; // peer handle — small line
	const char *region      = nullptr; // optional "global" / "eu"
	uint32_t    playerCount = 0;
	uint32_t    maxPlayers  = 0;
	bool        isPrivate   = false;   // padlock badge
	bool        isSelected  = false;   // accent border if true
	uintptr_t   artTexId    = 0;       // SDL_Texture* or nullptr
	ImVec2      artSize     = ImVec2(0, 0);

	// Stable identity for ImGui focus tracking.  If set, SessionTile
	// derives its widget ID from this string instead of the title
	// pointer + grid index (which both change across lobby refreshes,
	// causing the keyboard/gamepad cursor to jump home every 10 s).
	// Pass the sessionId from the lobby entry here.
	const char *idKey       = nullptr;

	// Machine spec line — "hardware | video | memory | OS | BASIC"
	// — rendered below the subtitle.  Tokens flagged `missing`
	// paint in a theme-aware red; the rest stay muted.  Empty
	// vector = don't draw a spec line at all (hub cards, etc.).
	std::vector<SpecLineToken> specTokens;

	// True when any spec token is missing.  The tile renders a
	// "Missing firmware" overlay and the caller must treat it as
	// non-joinable (the Join path is gated on this flag at the
	// call site, not inside SessionTile — SessionTile just signals
	// the click; filtering lives in the browser loop).
	bool                       specMissing = false;
};

bool SessionTile(const TileInfo &info, const ImVec2 &size);

// Single-line lobby reachability banner — mirrors Desktop's
// LobbyStatusIndicator but sized/coloured for Gaming Mode section
// headers.  Renders an "[OK]/[!!]/[..] Lobby: <status>" line; colour
// is green when the last List succeeded, red after a failure, dim
// while waiting for the first response.
//
// If `allowRetry` is true and health is red, a "Retry" touch button
// is appended that re-arms the Browser refresh queue.
void LobbyStatusBanner(bool allowRetry);

// -----------------------------------------------------------------------
// Transient toast — a non-intrusive single-line banner that fades in at
// the top of the screen, auto-dismisses after `durationMs`, and stacks
// beneath any prior toasts still on screen.  Used for events where the
// user deserves in-app feedback but an interrupting modal would be
// overkill: "peer joined — switching to <game>", "session ended, your
// game is restored", "host declined your request", etc.
//
// Severity maps to palette colours (info/success/warning/danger).
// Toasts render above every other Online Play UI — including the
// netplay overlay — so they are visible even when the user is
// browsing Settings / Game Library / the emulator canvas.
// -----------------------------------------------------------------------
enum class ToastSeverity {
	Info,     // neutral surface, textMuted on card
	Success,  // green pill
	Warning,  // amber pill
	Danger,   // red pill
};

void PushToast(const char *text,
               ToastSeverity severity = ToastSeverity::Info,
               uint64_t durationMs = 4000);
void RenderToasts();
void ClearToasts();

// Draws the Gaming-Mode screen header: a "<" back-arrow button
// (48x48 dp Subtle) plus the centred title text, matching the
// pattern used by Settings / About.  No-op on Desktop (the ImGui
// window chrome already carries the title and a close button).
//
// Returns `true` on the frame the back-arrow is activated (by
// mouse / touch / Enter / gamepad A).  The caller is expected to
// call `Back()` / `Navigate(...)` in response.  Also handles the
// Escape / Gamepad B hardware-back keys so every screen pops
// consistently without per-screen wiring.
bool ScreenHeader(const char *title);

// Resolve a hosted game's display name (typically a filename with
// extension, e.g. "Boulder Dash (1986).xex") to an ImTextureID
// pointing at the cover art found in the Game Library — matched by
// canonical basename.  Returns 0 when no match / art cache / library
// is available.  When non-zero, `outW` and `outH` receive the texture
// dimensions in pixels.  Cheap to call per row; the cache de-dupes
// loads and only uploads once per image.  Must be called from the UI
// thread.
uintptr_t LookupArtByGameName(const char *gameName, int *outW, int *outH);

// Drives the shared GameArtCache upload queue — call once per frame
// from any screen that paints art thumbnails.  Safe no-op when the
// cache is not initialised.
void PumpArtCache();

// Status indicator used by the Waiting panel.  `severity` controls the
// colour (0=accent/neutral, 1=warning, 2=danger, 3=success) and
// `showSpinner` adds a rotating dot train.
void StatusBadge(const char *label, int severity, bool showSpinner);

// Renders the peer handle + region + padlock tag inline — used by
// SessionTile internally, and by the Waiting panel to echo the peer.
void PeerChip(const char *handle, const char *region, bool isPrivate);

// -----------------------------------------------------------------------
// Keyboard / gamepad navigation helpers.
// -----------------------------------------------------------------------

// Wraps the pattern: on the first frame a screen is visible, push
// focus to the given widget so keyboard / gamepad nav picks it up.
// The caller passes a small integer key unique per-widget so the
// focus-on-first-frame fires at most once per screen appearance.
void FocusOnceNextFrame(int tag);

// True if a focus request for `tag` is pending this frame.  The
// widget immediately after this check should call
// ImGui::SetKeyboardFocusHere().
bool ConsumeFocusRequest(int tag);

// -----------------------------------------------------------------------
// Safe-area / scale helpers.
// -----------------------------------------------------------------------

// Returns the content rect inside the main window minus the platform
// safe-area insets.  On Android this avoids the status bar + rounded
// screen corners + gesture pill.  On desktop returns the full client
// area.
ImVec2 GetSafeOrigin();
ImVec2 GetSafeSize();

// Called by the mobile render path each frame with the current
// safe-area insets.  Desktop leaves all zero.
void ATNetplayUI_SetSafeAreaInsets(int top, int bottom,
                                   int left, int right);

// DPI-scaled pixel-per-dp helper (mirrors the one inside
// touch_widgets but exposed for netplay sizing).
float  Dp(float dp);

} // namespace ATNetplayUI
