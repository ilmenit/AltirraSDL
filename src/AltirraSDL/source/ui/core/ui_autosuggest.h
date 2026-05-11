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
}

#endif
