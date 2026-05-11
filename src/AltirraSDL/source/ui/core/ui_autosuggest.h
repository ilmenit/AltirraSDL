//	Altirra SDL3 — ImGui autosuggest popup
//	Copyright (C) 2008-2026 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	Cross-platform ImGui replacement for the Win32 ATUIAutoSuggestPopup
//	widget.  Hosts the portable IATAutoSuggestEngine and renders the
//	suggestion list as an overlay above the emulator viewport.
//
//	Desktop only — guarded by ATUIIsGamingMode() so the popup never
//	appears on mobile/touch builds where there is no physical Alt key.
//
//	Read-only with respect to simulator state (uses g_sim.DebugReadByte),
//	so it is netplay-safe and runs client-locally with no broadcast.

#ifndef AT_SDL3_UI_AUTOSUGGEST_H
#define AT_SDL3_UI_AUTOSUGGEST_H

#include <SDL3/SDL_keycode.h>

struct SDL_KeyboardEvent;

namespace ATUIAutoSuggest {
	// Toggle for auto-show-on-edit behavior.  Persisted to settings via
	// the "View: Auto-suggest enabled" registry key.
	void SetAutoSuggestEnabled(bool enabled);
	bool IsAutoSuggestEnabled();

	// One-shot trigger (matches Alt+, / Edit.ShowSuggestions).  Causes
	// the engine to evaluate the current editor line for up to ~100
	// frames even when auto-show is disabled.
	void ShowSuggestionsOnce();

	// Force-dismiss the popup (called when the user types into the
	// emulator or moves focus elsewhere).
	void CloseAutoSuggest();

	// True while the popup is visible.  Used by the SDL3 main loop to
	// route navigation keys to the popup instead of the emulator.
	bool IsPopupOpen();

	// Consume a key event for the popup.  Returns true if the key was
	// absorbed and must NOT be forwarded to the emulator.  Returns
	// false otherwise (popup closed, or key is not a navigation key,
	// or key is Enter but no item has been selected yet).
	//
	// Mirrors the Win32 action-binding policy in
	// ATUIVideoDisplayWindow::UpdateAutoSuggest: Up/Down/PgUp/PgDn/Esc
	// are always consumed while popup is open; Enter is consumed only
	// after the user explicitly navigated to a suggestion.
	bool HandleKeyDown(const SDL_KeyboardEvent &ev);

	// Per-frame entry points.  Update() runs the engine debounce
	// algorithm; Render() draws the ImGui overlay if the popup is open.
	// Both are no-ops in gaming mode.
	void Update();
	void Render();

	// Open / render the modal "Configure Suggestions" dialog.  Open()
	// records the request; RenderConfigDialog() must be called every
	// frame from ui_main so the dialog can draw.  Both are no-ops in
	// gaming mode.
	void OpenConfigDialog();
	void RenderConfigDialog();
}

// =====================================================================
// Auto-line-numbering (RetroArch-style "keep typing" helper for BASIC)
//
// Watches the BASIC screen editor: after the user presses RETURN at the
// end of a numbered line and the cursor advances to a freshly-cleared
// row at the left margin, this helper injects "next + step" + space so
// the user can immediately keep coding without re-typing the line
// number.  Gated by ATUIGetAutoLineNumberingEnabled() (off by default).
//
// Read-only with respect to simulator state apart from the typed text
// injection — netplay-safe.
// =====================================================================
namespace ATUIBasicLineNumbering {
	void Update();

	// Draw a small "Replaces line N" badge near the BASIC cursor when
	// the line currently being typed has a leading number that is
	// already in STMTAB.  Gated by the master Auto-Suggest toggle and
	// by the per-feature ShowReplaceWarning toggle.  No-op in gaming
	// mode and when no BASIC is loaded.  Must be called every frame
	// from the SDL3 main loop, after Update().
	void RenderReplaceWarning();
}

#endif
