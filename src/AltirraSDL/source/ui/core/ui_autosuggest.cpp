//	Altirra SDL3 — ImGui autosuggest popup implementation
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
//	Cross-platform replacement for ATUIVideoDisplayWindow::UpdateAutoSuggest
//	+ ATUIAutoSuggestPopup.  Implements the same triple-buffer debounce
//	used by the Win32 widget and renders the suggestion list with ImGui.

#include <stdafx.h>
#include <imgui.h>
#include <SDL3/SDL.h>

#include <vd2/system/refcount.h>
#include <vd2/system/VDString.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/text.h>

#include <at/atcore/atascii.h>
#include <at/atcore/ksyms.h>
#include "autosuggest.h"
#include "gtia.h"
#include "simulator.h"
#include "uiaccessors.h"

#include "ui_autosuggest.h"
#include "ui_main.h"
#include "ui_mode.h"

extern ATSimulator g_sim;
extern SDL_FRect g_displayRect;

namespace {
	struct EditorLine {
		uint8 mCursorCol = 0;
		uint8 mCursorRow = 0;
		vdfastvector<uint8> mText;

		bool operator==(const EditorLine& other) const {
			return mCursorCol == other.mCursorCol
				&& mCursorRow == other.mCursorRow
				&& mText.size() == other.mText.size()
				&& (mText.empty() || std::equal(mText.begin(), mText.end(), other.mText.begin()));
		}
		bool operator!=(const EditorLine& other) const { return !(*this == other); }
	};

	struct State {
		vdrefptr<IATAutoSuggestEngine> mpEngine;
		// mbEnabled state lives in uiaccessors_stubs.cpp (the canonical
		// holder so AltirraBridgeServer can persist it through settings.cpp
		// without dragging in ImGui).  Accessed via ATUIGet/SetAutoSuggestEnabled.
		uint32 mShowOnceCounter = 0;
		EditorLine mScan1, mScan2, mScan3;
		sint32 mSelectedIndex = -1;
		bool mbPopupOpen = false;
		bool mbAcceptRequested = false;
		size_t mNumResults = 0;

		// Anchor cursor cell captured at popup-open time.  Stored as
		// (col, row) — not screen pixels — so the popup tracks the
		// correct cursor cell even if the SDL window is resized, the
		// display zoom changes, or the overscan mode changes while the
		// popup is open.  The pixel anchors below are recomputed each
		// Render() from these cell coords + the current g_displayRect.
		uint8 mAnchorCol = 0;
		uint8 mAnchorRow = 0;
		ImVec2 mAnchorTL{0.0f, 0.0f};
		ImVec2 mAnchorBL{0.0f, 0.0f};
	};

	State g_state;

	// Mirrors ATUIVideoDisplayWindow::ReadEditorLine (uivideodisplaywindow.cpp
	// in test10).  Reads the OS shadow registers to find the BASIC editor's
	// current cursor cell, then reads the previous line + current line up to
	// the cursor — exactly what the Win32 path feeds to the engine.
	void ReadEditorLine(EditorLine& line) {
		const uint8 mode = g_sim.DebugReadByte(ATKernelSymbols::DINDEX);
		const bool textWindowActive = g_sim.DebugReadByte(ATKernelSymbols::SWPFLG);

		line.mText.clear();

		uint8 row = 0;
		uint8 col = 0;

		if (!mode || textWindowActive) {
			const bool textIsCurrent = !mode || textWindowActive;

			row = g_sim.DebugReadByte(textIsCurrent ? (uint32)ATKernelSymbols::ROWCRS : (uint32)ATKernelSymbols::TXTROW);
			col = g_sim.DebugReadByte(textIsCurrent ? (uint32)ATKernelSymbols::COLCRS : (uint32)ATKernelSymbols::TXTCOL);
			const uint8 lmar = g_sim.DebugReadByte(ATKernelSymbols::LMARGN);
			const uint8 rmar = g_sim.DebugReadByte(ATKernelSymbols::RMARGN);
			const uint16 addr = g_sim.DebugReadWord(textIsCurrent ? (uint32)ATKernelSymbols::SAVMSC : (uint32)ATKernelSymbols::TXTMSC);

			if (lmar <= rmar && rmar <= 39 && col >= lmar && col <= rmar && row < (mode ? 4 : 24)) {
				// Atari OS LOGMAP at $02B2 (4 bytes, one bit per screen
				// row, MSB-first per byte) marks each physical row that
				// STARTS a new logical line.  A clear bit means the row
				// is a wrap-continuation of the row above.
				//
				// We only prepend the previous row when the current row
				// is a continuation.  Otherwise the previous row's
				// leftover text (typically the "Ready" prompt or an
				// echoed command) would be concatenated to the line the
				// user is typing now, breaking the statement-keyword
				// regex anchor (the user types "P" at the start of a
				// fresh line but the engine sees "Ready...P").
				bool prevIsContinuation = false;
				if (row >= 1) {
					const uint8 byteIdx = row >> 3;
					const uint8 mask    = (uint8)(0x80 >> (row & 7));
					const uint8 logByte = g_sim.DebugReadByte((uint16)(ATKernelSymbols::LOGMAP + byteIdx));
					prevIsContinuation = (logByte & mask) == 0;
				}

				if (prevIsContinuation) {
					const size_t prevLineLen = (size_t)(rmar + 1 - lmar);
					// prevAddr already points at column lmar of the
					// previous row — the buggy +lmar in the inner read
					// dropped the first lmar chars of the wrap.
					const uint16 prevAddr = (uint16)(addr + (uint32)(row - 1) * 40 + lmar);

					line.mText.resize(prevLineLen);

					for(size_t i = 0; i < prevLineLen; ++i)
						line.mText[i] = g_sim.DebugReadByte((uint16)(prevAddr + i));
				}

				// read current line up to cursor
				const size_t currentLineLen = col - lmar;
				if (currentLineLen) {
					const uint16 curAddr = addr + row * 40;

					line.mText.resize(line.mText.size() + currentLineLen);

					uint8 *dst = &*(line.mText.end() - currentLineLen);

					for(size_t i = lmar; i < col; ++i)
						dst[i - lmar] = g_sim.DebugReadByte((uint16)(curAddr + i));
				}
			}
		}

		line.mCursorCol = col;
		line.mCursorRow = row;
	}

	// Convert an Atari beam-position cell to screen-pixel coordinates inside
	// the SDL3 emulator viewport (g_displayRect).  Mirrors Windows
	// ATUIVideoDisplayWindow::MapBeamPositionToPointF (uivideodisplaywindow.cpp
	// :2802): the visible image (g_displayRect) corresponds to the GTIA
	// scan area returned by GetFrameScanArea(), so beam X maps as
	//     pixel = displayRect.left + (beamX - scanArea.left)/scanArea.width()
	//             * displayRect.width()
	// and likewise for Y.  Using fixed 384x240 constants (the SDL3 code did
	// previously) drifts the popup tens of pixels away from the cursor for
	// every overscan mode except an exact scanArea of 0..384/0..240, which
	// no real Atari display configuration produces.
	ImVec2 BeamToScreen(int beamX, int beamY) {
		ATGTIAEmulator& gtia = g_sim.GetGTIA();
		const vdrect32 scanArea(gtia.GetFrameScanArea());

		const float scanW = (float)scanArea.width();
		const float scanH = (float)scanArea.height();
		if (scanW <= 0.0f || scanH <= 0.0f)
			return ImVec2(g_displayRect.x, g_displayRect.y);

		const float rx = ((float)beamX - (float)scanArea.left) / scanW;
		const float ry = ((float)beamY - (float)scanArea.top)  / scanH;

		return ImVec2(g_displayRect.x + rx * g_displayRect.w,
		              g_displayRect.y + ry * g_displayRect.h);
	}

	void RecomputeAnchor() {
		// ANTIC mode 2 (BASIC graphics 0) uses 4 beam cycles per character
		// column (each pixel is one color clock = half a beam cycle, and
		// the 8 mode-2 pixels span 4 cycles).  Y is 8 scanlines per row.
		// Constants match ATUIVideoDisplayWindow::UpdateAutoSuggest exactly.
		// Uses the cell captured in OpenPopupIfNeeded(), not the live
		// cursor — so the popup stays anchored to the trigger point even
		// while the user continues typing (matches Windows, which only
		// re-anchors on the next stabilization).
		const sint32 cursorBeamX1 = 48 + 4 * g_state.mAnchorCol;
		const sint32 cursorBeamY1 = 32 + 8 * g_state.mAnchorRow;
		const sint32 cursorBeamY2 = cursorBeamY1 + 8;

		g_state.mAnchorTL = BeamToScreen(cursorBeamX1, cursorBeamY1);
		g_state.mAnchorBL = BeamToScreen(cursorBeamX1, cursorBeamY2);
	}

	void OpenPopupIfNeeded() {
		g_state.mbPopupOpen = true;

		// Snapshot the cursor cell at the moment the popup opens (or
		// re-opens at a new stable point).  Render() recomputes the pixel
		// anchor from this cell each frame, so window resize, display
		// zoom, and overscan-mode changes are handled transparently.
		g_state.mAnchorCol = g_state.mScan3.mCursorCol;
		g_state.mAnchorRow = g_state.mScan3.mCursorRow;

		// Do NOT auto-select an item on open.  The Win32 widget only
		// binds Return → accept after the user navigates with arrow
		// keys (selection moves from -1 to >=0).  Auto-selecting here
		// would cause the user's first Enter (intended to commit the
		// BASIC line they're still typing) to paste the suggestion.
		if (g_state.mSelectedIndex >= (sint32)g_state.mNumResults)
			g_state.mSelectedIndex = (sint32)g_state.mNumResults - 1;
	}
}

namespace ATUIAutoSuggest {

void SetAutoSuggestEnabled(bool enabled) {
	const bool prev = ATUIGetAutoSuggestEnabled();
	if (prev != enabled) {
		ATUISetAutoSuggestEnabled(enabled);

		if (!enabled && !g_state.mShowOnceCounter)
			CloseAutoSuggest();
	}
}

bool IsAutoSuggestEnabled() {
	return ATUIGetAutoSuggestEnabled();
}

void ShowSuggestionsOnce() {
	// Respect the master gate so the Alt+, keybinding (routed through
	// commands_sdl3.cpp::CmdShowSuggestions) is a no-op when the user
	// has turned the whole subsystem off — without this, Update() would
	// still silently swallow the show-once flag, but the source of
	// truth for "master off" lives here.
	if (!ATUIGetAutoSuggestMasterEnabled())
		return;

	g_state.mShowOnceCounter = 1;

	g_state.mScan1.mText.clear();
	g_state.mScan2.mText.clear();
	g_state.mScan3.mText.clear();
}

void CloseAutoSuggest() {
	g_state.mbPopupOpen = false;
	g_state.mSelectedIndex = -1;
	g_state.mNumResults = 0;
	g_state.mbAcceptRequested = false;
}

bool IsPopupOpen() {
	return g_state.mbPopupOpen;
}

bool HandleKeyDown(const SDL_KeyboardEvent &ev) {
	if (!g_state.mbPopupOpen)
		return false;

	// Only navigation modifiers count.  Bare key + ImGui Ctrl/Cmd
	// modifiers should pass through (e.g. Ctrl+C in BASIC keeps working).
	const Uint16 baseMods = ev.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI);
	if (baseMods)
		return false;

	auto moveSelection = [](sint32 delta) {
		if (g_state.mNumResults == 0)
			return;

		if (g_state.mSelectedIndex < 0) {
			// First Up/Down anchors to top.  Matches Win32 widget
			// behaviour: arrows establish the selection, after which
			// Enter becomes the accept key.
			g_state.mSelectedIndex = 0;
		} else {
			sint32 next = g_state.mSelectedIndex + delta;
			if (next < 0)
				next = 0;
			else if ((size_t)next >= g_state.mNumResults)
				next = (sint32)g_state.mNumResults - 1;
			g_state.mSelectedIndex = next;
		}
	};

	switch (ev.scancode) {
		case SDL_SCANCODE_UP:
			moveSelection(-1);
			return true;
		case SDL_SCANCODE_DOWN:
			moveSelection(+1);
			return true;
		case SDL_SCANCODE_PAGEUP:
			moveSelection(-8);
			return true;
		case SDL_SCANCODE_PAGEDOWN:
			moveSelection(+8);
			return true;
		case SDL_SCANCODE_ESCAPE:
			CloseAutoSuggest();
			return true;
		case SDL_SCANCODE_RETURN:
		case SDL_SCANCODE_KP_ENTER:
			if (g_state.mSelectedIndex >= 0
				&& (size_t)g_state.mSelectedIndex < g_state.mNumResults)
			{
				g_state.mbAcceptRequested = true;
				return true;
			}
			// No selection — pass Enter through so BASIC sees the
			// line commit.  The popup remains open for the next char.
			return false;
		case SDL_SCANCODE_TAB:
			// Tab is the "accept top" shortcut: even with no arrow-key
			// navigation, Tab picks the first suggestion in the popup.
			// Mirrors the convention shells and code editors use and
			// is much faster than reaching for Enter (which the user
			// reserves for committing the BASIC line itself).
			//
			// Gated by the "Tab accepts top suggestion" Configure
			// Suggestions toggle so users that need literal Tab in
			// the BASIC editor can opt out.
			if (ATUIGetAutoSuggestTabAcceptEnabled() && g_state.mNumResults > 0) {
				if (g_state.mSelectedIndex < 0)
					g_state.mSelectedIndex = 0;
				g_state.mbAcceptRequested = true;
				return true;
			}
			return false;
		default:
			return false;
	}
}

void Update() {
	if (ATUIIsGamingMode()) {
		// Gaming mode has no physical Alt key.  Force-close in case the
		// user toggled modes while the popup was open.
		if (g_state.mbPopupOpen)
			CloseAutoSuggest();
		return;
	}

	// Master gate: when the user has turned Auto-Suggest for BASIC off,
	// stop polling entirely and force-close any open popup.  This must
	// fire BEFORE the show-once counter so Alt+, also becomes inert.
	if (!ATUIGetAutoSuggestMasterEnabled()) {
		if (g_state.mbPopupOpen)
			CloseAutoSuggest();
		g_state.mShowOnceCounter = 0;
		return;
	}

	if (g_state.mShowOnceCounter) {
		if (++g_state.mShowOnceCounter >= 100)
			g_state.mShowOnceCounter = 0;
	}

	if (!ATUIGetAutoSuggestEnabled() && !g_state.mShowOnceCounter) {
		// Win32 parity (uivideodisplaywindow.cpp:3617): plain early return.
		// A popup opened via the one-shot Alt+, trigger leaves
		// mShowOnceCounter at 0 on the next frame; closing here would
		// blink the popup out one frame after it appeared.  The popup
		// is dismissed by Escape, click-outside, accept, or by toggling
		// Auto-Show off via SetAutoSuggestEnabled() (which closes
		// explicitly).
		return;
	}

	if (!g_state.mpEngine)
		g_state.mpEngine = ATCreateAutoSuggestEngine();

	// Push the current category toggles into the engine each frame.  Cheap
	// (copy of three bools) and avoids having to plumb option-change
	// notifications from the Configure Suggestions dialog into here.
	{
		ATAutoSuggestOptions opts;
		opts.mbStatementKeywords = ATUIGetAutoSuggestStatementsEnabled();
		opts.mbFunctionKeywords  = ATUIGetAutoSuggestFunctionsEnabled();
		opts.mbVariableNames     = ATUIGetAutoSuggestVariablesEnabled();
		g_state.mpEngine->SetOptions(opts);
	}

	if (g_state.mShowOnceCounter)
		std::swap(g_state.mScan1, g_state.mScan2);

	std::swap(g_state.mScan2, g_state.mScan3);

	ReadEditorLine(g_state.mScan3);

	// Trigger an engine update when two consecutive frames show identical
	// editor state but differ from the prior triplet.  This matches the
	// Win32 debounce so we don't flicker the popup while characters are
	// still being typed.
	const bool stableNow = (g_state.mScan2 == g_state.mScan3);
	const bool changedSincePrev = g_state.mShowOnceCounter
		? g_state.mScan1 == g_state.mScan2
		: g_state.mScan1 != g_state.mScan2;

	if (stableNow && changedSincePrev) {
		g_state.mShowOnceCounter = 0;
		std::swap(g_state.mScan1, g_state.mScan2);

		VDStringA text;
		for(uint8 c : g_state.mScan1.mText) {
			c = ATConvertInternalToATASCII(c);
			if (c >= 0x20 && c < 0x7F)
				text.push_back((char)c);
			else
				text.push_back(' ');
		}

		g_state.mpEngine->Update(text);

		const size_t n = g_state.mpEngine->GetNumResults();
		if (n) {
			g_state.mNumResults = n;
			OpenPopupIfNeeded();
		} else {
			CloseAutoSuggest();
		}
	}
}

void Render() {
	if (!g_state.mbPopupOpen)
		return;

	if (ATUIIsGamingMode())
		return;

	// Master gate.  A flag toggled mid-frame (e.g. from the View menu)
	// can leave the popup flagged open until Update() runs again; bail
	// here so the popup hides immediately.
	if (!ATUIGetAutoSuggestMasterEnabled())
		return;

	if (!g_state.mpEngine || !g_state.mNumResults)
		return;

	// Re-derive the pixel anchor every frame from the saved cell coords.
	// This keeps the popup aligned to the cursor cell even if the user
	// resizes the SDL window, changes display zoom, toggles fullscreen,
	// or switches overscan mode while the popup is open.
	RecomputeAnchor();

	// Decide above-vs-below placement based on remaining viewport space.
	// Mirrors Windows ATUIVideoDisplayWindow::UpdateAutoSuggest placement
	// block (uivideodisplaywindow.cpp:3697-3730): prefer below the cursor
	// row; flip above only when below has less room AND above has more.
	// If neither side has the full ideal height, shrink to whatever the
	// chosen side offers — better to clip the list than to occlude the
	// cursor line or extend off-screen.
	const ImGuiViewport *vp = ImGui::GetMainViewport();
	const float vpTop    = vp->Pos.y;
	const float vpBottom = vp->Pos.y + vp->Size.y;
	const float vpLeft   = vp->Pos.x;
	const float vpRight  = vp->Pos.x + vp->Size.x;

	// Size to actual content rather than always 10 lines: most lookups
	// return 2-6 results and the wasted 200px stripe looks bad and
	// reduces the chance of fitting below the cursor on short windows.
	//
	// Add one extra line (or two, when wrapped) + separator at the
	// bottom for the "Syntax: ..." hint when the highlighted (or top)
	// result carries one.  We pre-compute this here so the window-
	// height clamp knows about it.  Width 400 fits the longest manual
	// syntax line ("Syntax: ON aexp {GOTO|GOSUB} lineno [, lineno...]")
	// on a 12-px font without wrap; smaller fonts/scales would wrap
	// gracefully via TextWrapped() at render time.
	const float lineH = ImGui::GetTextLineHeightWithSpacing();
	const float padY  = ImGui::GetStyle().WindowPadding.y * 2.0f;

	const sint32 syntaxIdx = (g_state.mSelectedIndex >= 0
		&& (size_t)g_state.mSelectedIndex < g_state.mNumResults)
		? g_state.mSelectedIndex : 0;
	bool hasSyntaxRow = false;
	if (ATUIGetAutoSuggestShowSyntaxEnabled()
		&& g_state.mpEngine && g_state.mNumResults > 0)
	{
		const auto& rTop = g_state.mpEngine->GetResult((size_t)syntaxIdx);
		hasSyntaxRow = !rTop.mSyntaxText.empty();
	}
	// Reserve ~2 lines of vertical space when a syntax row exists so a
	// long entry wrapping to a second line doesn't trigger an internal
	// scrollbar in the popup.  One-line entries simply leave a small
	// trailing gap.
	const float syntaxH = hasSyntaxRow ? (lineH * 2.0f + 4.0f /*separator*/) : 0.0f;

	const float contentH = (float)g_state.mNumResults * lineH + padY + syntaxH;
	const float idealH = std::min(contentH, 280.0f);
	// Shrink to fit narrow viewports (mobile portrait, embedded iframes)
	// — otherwise the popup overflows off-screen and the user only sees
	// a left strip of the suggestions.
	const float idealW = std::min(400.0f, vp->Size.x);

	// Clamp the cursor anchors into the viewport before measuring space.
	// (A cursor that is below the viewport — e.g. fullscreen → windowed
	// resize since the popup opened — would otherwise produce a negative
	// spaceBelow and put the popup off-screen.)
	const float anchorBLy = std::clamp(g_state.mAnchorBL.y, vpTop, vpBottom);
	const float anchorTLy = std::clamp(g_state.mAnchorTL.y, vpTop, vpBottom);

	const float spaceAbove = anchorTLy - vpTop;
	const float spaceBelow = vpBottom  - anchorBLy;

	ImVec2 pos;
	float sizeH;
	if (spaceBelow < idealH && spaceAbove > spaceBelow) {
		// Above: take min(ideal, room) and pin bottom to the cursor row.
		sizeH = std::min(idealH, spaceAbove);
		pos.y = anchorTLy - sizeH;
	} else {
		// Below: pin top to the cursor row, take min(ideal, room).
		sizeH = std::min(idealH, spaceBelow);
		pos.y = anchorBLy;
	}

	// Horizontal: align to cursor column, then slide left/right so the
	// popup fits inside the viewport.
	pos.x = g_state.mAnchorBL.x;
	if (pos.x + idealW > vpRight)
		pos.x = vpRight - idealW;
	if (pos.x < vpLeft)
		pos.x = vpLeft;

	// Degenerate case: less than one content line of room — viewport
	// tiny, or cursor pinned to an edge with no room either side.  Skip
	// the draw rather than emit a window that ImGui's WindowMinSize
	// (default 32px) would force taller than our sizeH, which would
	// overlap the cursor row we are trying to leave visible.
	if (sizeH < lineH + padY) {
		return;
	}

	ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(idealW, sizeH), ImGuiCond_Always);

	const ImGuiWindowFlags flags =
		  ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoFocusOnAppearing
		| ImGuiWindowFlags_NoNavInputs
		| ImGuiWindowFlags_NoBringToFrontOnFocus;

	if (!ImGui::Begin("##AutoSuggest", nullptr, flags)) {
		ImGui::End();
		return;
	}

	// Note: keyboard navigation is NOT polled here via ImGui::IsKeyPressed,
	// because the popup window uses NoFocusOnAppearing and never claims
	// keyboard focus — the emulator viewport always owns focus while the
	// popup is shown.  Polling here would double-fire (popup AND emulator
	// would see the same Up/Down/Esc/Enter).  Instead the SDL3 main loop
	// calls HandleKeyDown() before forwarding to the emulator, which is
	// the only way to atomically consume a key.  mbAcceptRequested is
	// set by HandleKeyDown when Enter triggers; selection moves are
	// already applied by then.
	const bool moved = (g_state.mSelectedIndex >= 0);

	// Item list.  Each row shows the item text (the Atari address or
	// filename being suggested) and the description in dimmed text.
	for(size_t i = 0; i < g_state.mNumResults; ++i) {
		const auto& r = g_state.mpEngine->GetResult(i);

		ImGui::PushID((int)i);

		const bool isSelected = ((sint32)i == g_state.mSelectedIndex);
		const VDStringA itemUtf8 = VDTextWToU8(VDStringSpanW(r.mItemText));
		const VDStringA descUtf8 = VDTextWToU8(VDStringSpanW(r.mDescriptionText));

		if (ImGui::Selectable(itemUtf8.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
			g_state.mSelectedIndex = (sint32)i;
			if (ImGui::IsMouseDoubleClicked(0))
				g_state.mbAcceptRequested = true;
		}

		if (!descUtf8.empty()) {
			ImGui::SameLine();
			ImGui::TextDisabled("%s", descUtf8.c_str());
		}

		if (isSelected && moved)
			ImGui::SetScrollHereY();

		ImGui::PopID();
	}

	// Syntax hint row.  Sits at the bottom of the popup, separated from
	// the item list, and reflects the highlighted item (or the top entry
	// when nothing is highlighted yet).  Read-only; no interaction.
	// Sourced from the Altirra BASIC Reference Manual via
	// LookupBasicSyntax — see kAtariBasicSyntax in autosuggest.cpp.
	if (hasSyntaxRow) {
		const auto& rSyn = g_state.mpEngine->GetResult((size_t)syntaxIdx);
		const VDStringA synUtf8 = VDTextWToU8(VDStringSpanW(rSyn.mSyntaxText));
		ImGui::Separator();
		// Dim the hint so the eye is drawn to the selectable list first;
		// the syntax line is reference material, not a click target.
		// TextWrapped (not TextDisabled) so the longest manual entries
		// wrap onto a second line at small popup widths / large font
		// scales instead of clipping.
		ImGui::PushStyleColor(ImGuiCol_Text,
			ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		ImGui::TextWrapped("Syntax: %s", synUtf8.c_str());
		ImGui::PopStyleColor();
	}

	if (g_state.mbAcceptRequested) {
		g_state.mbAcceptRequested = false;

		if (g_state.mSelectedIndex >= 0
			&& (size_t)g_state.mSelectedIndex < g_state.mNumResults)
		{
			const auto& r = g_state.mpEngine->GetResult(g_state.mSelectedIndex);

			if (!r.mInsertionText.empty()) {
				// ATUIPasteTextDirect takes UTF-16; the autosuggest engine
				// emits the insertion text in ASCII (PEEK/POKE digits,
				// CIO filename fragments).  Convert and inject.
				const VDStringW wInsert(VDTextU8ToW(VDStringSpanA(r.mInsertionText)));
				ATUIPasteTextDirect(wInsert.c_str(), wInsert.size());
			}

			CloseAutoSuggest();
		}
	}

	ImGui::End();
}

} // namespace ATUIAutoSuggest

// =========================================================================
// Configure Suggestions dialog
// =========================================================================
//
// ImGui modal listing every user-visible knob of the autosuggest system
// in one place.  Keeps the menu shallow ("Show Suggestions" + "Configure
// Suggestions…") while exposing the new BASIC-aware features the engine
// now supports.

namespace ATUIAutoSuggest {
	namespace {
		bool s_configDialogOpen = false;
	}

	void OpenConfigDialog() {
		s_configDialogOpen = true;
	}

	void RenderConfigDialog() {
		if (!s_configDialogOpen)
			return;
		if (ATUIIsGamingMode()) {
			s_configDialogOpen = false;
			return;
		}

		// Centre on the main viewport every time the dialog opens; never
		// persist position (CLAUDE.md "config dialogs vs. debugger panes").
		ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_Appearing);
		ImGui::SetNextWindowPos(
			ImGui::GetMainViewport()->GetCenter(),
			ImGuiCond_Appearing,
			ImVec2(0.5f, 0.5f)
		);

		bool keepOpen = true;
		if (ImGui::Begin(
				"Configure BASIC Suggestions",
				&keepOpen,
				ImGuiWindowFlags_AlwaysAutoResize
				| ImGuiWindowFlags_NoSavedSettings
				| ImGuiWindowFlags_NoCollapse))
		{
			// Wrapped-tooltip helper.  ImGui::SetItemTooltip renders via
			// Text() (no wrap), so paragraph-length tooltips spill off the
			// edge of the screen.  This is the canonical wrap pattern
			// from the ImGui demo: BeginItemTooltip + PushTextWrapPos +
			// TextUnformatted + Pop + EndTooltip.  Wrap width ≈ 35 em
			// columns matches the readable width used by other Atari /
			// vintage software popups.
			auto wrappedTooltip = [](const char *text) {
				if (ImGui::BeginItemTooltip()) {
					ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
					ImGui::TextUnformatted(text);
					ImGui::PopTextWrapPos();
					ImGui::EndTooltip();
				}
			};

			// --- Master toggle ---------------------------------------------
			// Mirrors the menu's "Suggestions Enabled" item — having it in
			// both places means users who reach for Configure first never
			// have to back out to the View menu just to turn the system
			// on/off.  Everything below is gated visually by the master:
			// the rest of the dialog dims when the master is off so it's
			// obvious nothing else can take effect.
			{
				bool master = ATUIGetAutoSuggestMasterEnabled();
				if (ImGui::Checkbox("Auto-Suggest for BASIC enabled", &master))
					ATUISetAutoSuggestMasterEnabled(master);
				wrappedTooltip(
					"Master toggle for the entire Auto-Suggest subsystem "
					"(popup, line numbering, replace-warning badge). "
					"All suggestions are BASIC-aware and only fire when a "
					"BASIC interpreter (Atari BASIC, Altirra BASIC, "
					"BASIC XL/XE, Turbo-BASIC XL) is loaded.");
			}

			ImGui::Dummy(ImVec2(0.0f, 6.0f));

			ImGui::BeginDisabled(!ATUIGetAutoSuggestMasterEnabled());

			// --- Trigger ----------------------------------------------------
			ImGui::TextDisabled("Trigger");
			ImGui::Separator();
			{
				bool autoShow = ATUIGetAutoSuggestEnabled();
				if (ImGui::Checkbox("Auto-Show Suggestions while typing", &autoShow))
					ATUIAutoSuggest::SetAutoSuggestEnabled(autoShow);
				wrappedTooltip(
					"When off, suggestions only appear when you press Alt+, "
					"(Show Suggestions). When on, the popup follows the BASIC "
					"editor as you type.");
			}

			ImGui::Dummy(ImVec2(0.0f, 6.0f));

			// --- Categories -------------------------------------------------
			ImGui::TextDisabled("Suggestion categories");
			ImGui::Separator();
			{
				bool stm = ATUIGetAutoSuggestStatementsEnabled();
				if (ImGui::Checkbox("BASIC statements (PRINT, GOTO, FOR, ...)", &stm))
					ATUISetAutoSuggestStatementsEnabled(stm);

				bool fns = ATUIGetAutoSuggestFunctionsEnabled();
				if (ImGui::Checkbox("BASIC functions and clauses (CHR$, RND, TO, THEN, ...)", &fns))
					ATUISetAutoSuggestFunctionsEnabled(fns);

				bool vars = ATUIGetAutoSuggestVariablesEnabled();
				if (ImGui::Checkbox("User variables (from running BASIC program)", &vars))
					ATUISetAutoSuggestVariablesEnabled(vars);
				wrappedTooltip(
					"Walks the BASIC variable name table (VNTP at $82) "
					"and suggests previously declared variables. Most "
					"recently declared variables appear first.");

				ImGui::TextDisabled(
					"PEEK/POKE address names and CIO path completion are "
					"always on (no failure mode worth gating).");
			}

			ImGui::Dummy(ImVec2(0.0f, 6.0f));

			// --- Acceptance shortcut ---------------------------------------
			ImGui::TextDisabled("Acceptance & display");
			ImGui::Separator();
			{
				bool tabAccept = ATUIGetAutoSuggestTabAcceptEnabled();
				if (ImGui::Checkbox("Tab accepts top suggestion", &tabAccept))
					ATUISetAutoSuggestTabAcceptEnabled(tabAccept);
				wrappedTooltip(
					"With this on, pressing Tab while the popup is showing "
					"inserts the top entry (or the highlighted one) instead "
					"of producing a literal tab in the BASIC editor. "
					"Up/Down arrows still navigate the list; Enter still "
					"accepts a selected entry; Esc dismisses the popup.");

				bool showSyn = ATUIGetAutoSuggestShowSyntaxEnabled();
				if (ImGui::Checkbox("Show \"Syntax: ...\" hint under selected item", &showSyn))
					ATUISetAutoSuggestShowSyntaxEnabled(showSyn);
				wrappedTooltip(
					"Shows the canonical syntax line from the Altirra "
					"BASIC Reference Manual under the highlighted "
					"keyword (e.g. \"Syntax: aexp = LEN(sexp)\"). "
					"Disable to keep the popup compact.");
			}

			ImGui::Dummy(ImVec2(0.0f, 6.0f));

			// --- Auto line numbering ---------------------------------------
			ImGui::TextDisabled("Editor helpers");
			ImGui::Separator();
			{
				bool aln = ATUIGetAutoLineNumberingEnabled();
				if (ImGui::Checkbox("Auto Line Numbering after Enter", &aln))
					ATUISetAutoLineNumberingEnabled(aln);
				wrappedTooltip(
					"When you press Enter at the end of a numbered BASIC line "
					"(e.g. \"10 PRINT\"), Altirra injects the next line number "
					"(\"20 \") so you can keep coding. If that next number is "
					"already used by an existing line, nothing is injected — "
					"you decide explicitly how to proceed.");

				int step = ATUIGetAutoLineNumberingStep();
				ImGui::BeginDisabled(!aln);
				ImGui::SetNextItemWidth(120.0f);
				if (ImGui::InputInt("Step", &step, 1, 10)) {
					if (step < 1)    step = 1;
					if (step > 1000) step = 1000;
					ATUISetAutoLineNumberingStep(step);
				}
				ImGui::EndDisabled();

				bool warn = ATUIGetAutoLineNumberingShowReplaceWarning();
				if (ImGui::Checkbox("Show \"Replaces line N\" warning while editing", &warn))
					ATUISetAutoLineNumberingShowReplaceWarning(warn);
				wrappedTooltip(
					"Floats a small amber badge near the cursor whenever "
					"the line you are typing starts with a number that "
					"is already stored in the BASIC program. Pressing "
					"Enter on a bare number deletes the stored line; "
					"typing more content replaces it. The badge warns "
					"you before either happens.");
			}

			ImGui::EndDisabled();   // matches BeginDisabled(!master) above

			ImGui::Dummy(ImVec2(0.0f, 10.0f));

			ImGui::Separator();
			if (ImGui::Button("Close", ImVec2(120.0f, 0.0f)))
				s_configDialogOpen = false;
		}
		ImGui::End();

		if (!keepOpen)
			s_configDialogOpen = false;
	}
}

// =========================================================================
// Auto-line-numbering helper
// =========================================================================

namespace ATUIBasicLineNumbering {
	namespace {
		// Track the SAVMSC+row+col anchor where we last injected so a
		// stale-cursor-state combo can't re-fire on every frame while
		// BASIC is still digesting the previous line.
		//
		// mPrevProgramSize tracks the BASIC program region byte count
		// (STARP - STMTAB) from the previous frame.  We only auto-
		// inject when that count changed since the previous frame,
		// which is the only reliable signal that "BASIC just accepted
		// a numbered line".  Immediate commands and syntax errors
		// both leave the program region unchanged, so neither triggers
		// a stray "20 " injection.
		struct State {
			bool   mFirstUpdate     = true;
			uint8  mPrevRow         = 0;
			uint8  mPrevCol         = 0;
			uint16 mLastInjectAddr  = 0;
			uint16 mPrevProgramSize = 0;
		};
		State g_lnState;

		// Read a single screen row at byte offset `rowAddr` for `lineLen`
		// bytes, returning the ATASCII representation with trailing spaces
		// stripped.
		void ReadScreenRow(uint16 rowAddr, size_t lineLen, VDStringA& out) {
			out.clear();
			out.resize(lineLen);
			for (size_t i = 0; i < lineLen; ++i) {
				uint8 c = g_sim.DebugReadByte((uint16)(rowAddr + i));
				out[i] = (char)ATConvertInternalToATASCII(c);
			}
			while (!out.empty() && out.back() == ' ')
				out.pop_back();
		}

		// Returns true iff `row` is "freshly cleared" — all bytes are
		// spaces (internal 0x00) or inverse-space (0x80).  BASIC uses
		// these as the post-scroll empty-cell value.
		bool RowIsEmpty(uint16 rowAddr, size_t lineLen) {
			for (size_t i = 0; i < lineLen; ++i) {
				uint8 c = g_sim.DebugReadByte((uint16)(rowAddr + i));
				if (c != 0x00 && c != 0x80)
					return false;
			}
			return true;
		}

		// Parse `^[0-9]+ <keyword-start>` from `line`.  Returns false
		// unless the line looks like a real BASIC source line — i.e.
		// the number is followed by a space and then either an
		// uppercase/lowercase letter or `?` (PRINT shorthand).  This
		// guards against "Error- 99 ..." style BASIC output (the
		// 'E' rejects), against pure numeric PRINT output (no letter
		// after the number), and against partial lines.
		bool ParseLeadingNumber(const VDStringA& line, unsigned& outNumber) {
			size_t p = 0;
			while (p < line.size() && line[p] == ' ')
				++p;
			if (p >= line.size() || line[p] < '0' || line[p] > '9')
				return false;

			unsigned n = 0;
			while (p < line.size() && line[p] >= '0' && line[p] <= '9') {
				n = n * 10 + (unsigned)(line[p] - '0');
				if (n > 32767)
					return false;
				++p;
			}
			if (p >= line.size() || line[p] != ' ')
				return false;
			++p;
			while (p < line.size() && line[p] == ' ')
				++p;
			if (p >= line.size())
				return false;
			const char c = line[p];
			const bool keywordStart =
				(c >= 'A' && c <= 'Z') ||
				(c >= 'a' && c <= 'z') ||
				c == '?';
			if (!keywordStart)
				return false;
			outNumber = n;
			return true;
		}

		uint16 ReadProgramSize() {
			const uint16 stmt  = g_sim.DebugReadWord(0x88);
			const uint16 starp = g_sim.DebugReadWord(0x8C);
			if (starp < stmt)
				return 0;
			return (uint16)(starp - stmt);
		}
	}

	void Update() {
		// Master Auto-Suggest gate: this helper is part of the same
		// subsystem, so the master switch hides it too.  Also reset
		// internal state so re-enabling won't false-fire later.
		if (!ATUIGetAutoSuggestMasterEnabled()) {
			g_lnState.mFirstUpdate = true;
			return;
		}

		if (!ATUIGetAutoLineNumberingEnabled()) {
			// Reset state so re-enabling later doesn't fire on the very
			// next frame against stale row positions.
			g_lnState.mFirstUpdate = true;
			return;
		}

		// Skip entirely when there is no BASIC interpreter loaded (the
		// VNTP/STMTAB/STARP zero-page chain is the canonical signal).
		// Without this check, ATUIPasteTextDirect would inject "20 "
		// into a DOS prompt or machine-code monitor on row advance,
		// which is never what the user wants.
		if (!ATIsBasicMemoryLayoutValid()) {
			g_lnState.mFirstUpdate = true;
			return;
		}

		// Track BASIC program region size.  Only auto-inject when the
		// program GREW or SHRANK between this frame and the previous —
		// BASIC only does that when it has successfully tokenised and
		// stored (or replaced) a numbered line.  Immediate commands
		// and syntax errors leave STARP-STMTAB unchanged.
		const uint16 programSize = ReadProgramSize();
		const uint16 prevProgramSize = g_lnState.mPrevProgramSize;
		g_lnState.mPrevProgramSize = programSize;
		const bool programChanged = (programSize != prevProgramSize);

		// Same screen-editor cursor resolution as ReadEditorLine() above:
		// when DINDEX==0 (graphics 0 / mode 0) the main screen window is
		// active; otherwise the text-window cursor variables apply.
		const uint8 mode = g_sim.DebugReadByte(ATKernelSymbols::DINDEX);
		const uint8 swap = g_sim.DebugReadByte(ATKernelSymbols::SWPFLG);
		const bool useText = (mode != 0 && swap == 0);

		const uint8 row = g_sim.DebugReadByte(useText ? (uint32)ATKernelSymbols::TXTROW : (uint32)ATKernelSymbols::ROWCRS);
		const uint8 col = g_sim.DebugReadByte(useText ? (uint32)ATKernelSymbols::TXTCOL : (uint32)ATKernelSymbols::COLCRS);
		const uint8 lmar = g_sim.DebugReadByte(ATKernelSymbols::LMARGN);
		const uint8 rmar = g_sim.DebugReadByte(ATKernelSymbols::RMARGN);
		const uint16 addr = g_sim.DebugReadWord(useText ? (uint32)ATKernelSymbols::TXTMSC : (uint32)ATKernelSymbols::SAVMSC);

		// First-update bootstrap: record cursor, skip injection so we
		// never inject on the very first frame (we have no prior cursor
		// state to compare against).
		if (g_lnState.mFirstUpdate) {
			g_lnState.mFirstUpdate    = false;
			g_lnState.mPrevRow        = row;
			g_lnState.mPrevCol        = col;
			g_lnState.mLastInjectAddr = 0;
			return;
		}

		// Program must have changed THIS frame — otherwise BASIC didn't
		// accept a new line and nothing should be auto-numbered.
		if (!programChanged)
			return;

		const uint8 prevRow = g_lnState.mPrevRow;
		const uint8 prevCol = g_lnState.mPrevCol;
		g_lnState.mPrevRow = row;
		g_lnState.mPrevCol = col;

		// Trigger preconditions:
		//   (a) cursor is at the left margin (Enter advances here).
		//   (b) cursor row OR address has changed since last frame
		//       (an idle cursor doesn't trigger).
		//   (c) row layout is valid.
		if (col != lmar)
			return;
		if (row == prevRow && col == prevCol)
			return;
		if (rmar <= lmar || rmar > 39)
			return;
		const size_t lineLen = (size_t)(rmar + 1u - lmar);
		const uint8 maxRow = (uint8)((mode == 0 || swap != 0) ? 23 : 3);
		if (row > maxRow)
			return;

		const uint16 curRowAddr = (uint16)(addr + (uint32)row * 40 + lmar);

		// Avoid re-injecting onto the same cursor cell — once we've
		// fired for this address, wait for the cursor to move
		// (mLastInjectAddr is reset when the user moves away).
		if (curRowAddr == g_lnState.mLastInjectAddr)
			return;
		g_lnState.mLastInjectAddr = 0;

		// The current row must be empty (BASIC just cleared it after Enter,
		// before printing anything).  If BASIC is running and PRINTed
		// output, the row will contain those characters and we bail.
		if (!RowIsEmpty(curRowAddr, lineLen))
			return;

		// We need a "previous BASIC line".  After Enter from the bottom
		// row, the screen scrolls and the user's line ends up on
		// (row - 1).  In the no-scroll case row > 0 also lands us on
		// (row - 1).  Either way: examine the row immediately above.
		if (row == 0)
			return;
		const uint16 prevRowAddr = (uint16)(addr + (uint32)(row - 1u) * 40 + lmar);

		VDStringA prevLine;
		ReadScreenRow(prevRowAddr, lineLen, prevLine);
		if (prevLine.empty())
			return;

		unsigned lastNumber = 0;
		if (!ParseLeadingNumber(prevLine, lastNumber))
			return;

		const int step = ATUIGetAutoLineNumberingStep();
		const unsigned next = lastNumber + (unsigned)step;
		if (next > 32767)
			return;

		// If the proposed number already exists as a stored line, abort
		// the injection entirely — DO NOT skip to the next free slot.
		// Why no skip:
		//
		//   Auto-injecting "30 " when line 20 exists and 30 also exists
		//   would either silently overwrite (if the user keeps typing)
		//   or silently delete line 30 (if the user just hits Enter on
		//   the bare number).  Skipping to "40" would silently rewrite
		//   the user's program structure (40 looks contiguous, hiding
		//   that 30 is already in use).  The only safe behaviour is to
		//   inject nothing and let the user decide explicitly.
		if (ATBasicProgramLineExists((uint16)next))
			return;

		// Inject "N ".  ATUIPasteTextDirect takes UTF-16; we convert from
		// ASCII (only ever digits + space here).
		char buf[16];
		const int n = snprintf(buf, sizeof(buf), "%u ", next);
		if (n <= 0 || n >= (int)sizeof(buf))
			return;
		const VDStringW wbuf(VDTextU8ToW(VDStringSpanA(buf, buf + n)));
		ATUIPasteTextDirect(wbuf.c_str(), (uint32)wbuf.size());

		g_lnState.mLastInjectAddr = curRowAddr;
	}

	// -----------------------------------------------------------------
	// "Replaces line N" warning badge
	// -----------------------------------------------------------------
	//
	// The renderer is intentionally separate from Update(): the warning
	// is purely cosmetic (no side effects on the BASIC editor or on
	// auto-line-numbering state), so it stays out of the injection
	// path.  Called every frame from ui_main.cpp.
	//
	// Trigger: master Auto-Suggest is on, ShowReplaceWarning is on,
	// BASIC is loaded, the row at the cursor begins with digits (after
	// any leading spaces) that parse to a number stored in STMTAB.  No
	// keyword-start or trailing-space requirement — pressing Enter on
	// a bare number deletes the stored line, so the warning must fire
	// the moment the digits match.
	//
	// The badge anchors above the cursor row, falling back to below if
	// there's no room above.  No interaction (no click target, no
	// keyboard nav) — it's a passive indicator.
	void RenderReplaceWarning() {
		if (ATUIIsGamingMode())
			return;
		if (!ATUIGetAutoSuggestMasterEnabled())
			return;
		if (!ATUIGetAutoLineNumberingShowReplaceWarning())
			return;
		if (!ATIsBasicMemoryLayoutValid())
			return;

		// Resolve cursor + line geometry exactly like Update() does so
		// the badge tracks the same row the user is editing.
		const uint8 mode = g_sim.DebugReadByte(ATKernelSymbols::DINDEX);
		const uint8 swap = g_sim.DebugReadByte(ATKernelSymbols::SWPFLG);
		const bool useText = (mode != 0 && swap == 0);

		const uint8 row  = g_sim.DebugReadByte(useText ? (uint32)ATKernelSymbols::TXTROW : (uint32)ATKernelSymbols::ROWCRS);
		const uint8 lmar = g_sim.DebugReadByte(ATKernelSymbols::LMARGN);
		const uint8 rmar = g_sim.DebugReadByte(ATKernelSymbols::RMARGN);
		const uint16 addr = g_sim.DebugReadWord(useText ? (uint32)ATKernelSymbols::TXTMSC : (uint32)ATKernelSymbols::SAVMSC);

		if (lmar > rmar || rmar > 39)
			return;
		const uint8 maxRow = (uint8)((mode == 0 || swap != 0) ? 23 : 3);
		if (row > maxRow)
			return;

		const size_t lineLen = (size_t)(rmar + 1u - lmar);
		const uint16 rowAddr = (uint16)(addr + (uint32)row * 40 + lmar);

		// Read the row, then parse a leading "<digits>" prefix.  The
		// warning fires the moment the digits match an existing stored
		// line, regardless of cursor position or whether a space/body
		// has been typed yet.  Why no cursor-inside-digits suppression:
		//
		//   In Atari BASIC, "20<RET>" with nothing else deletes line
		//   20, and "20 stuff<RET>" replaces it.  Both are destructive.
		//   The user needs to know IMMEDIATELY that pressing Enter from
		//   here would touch an existing line.  Frame-by-frame re-
		//   evaluation means typing "100" -> "1005" briefly shows the
		//   warning at "100" (matches line 100) and correctly hides it
		//   at "1005" (no match).  The flicker is informative, not
		//   noise: it says "if you stopped here you would lose data."
		VDStringA line;
		ReadScreenRow(rowAddr, lineLen, line);

		size_t p = 0;
		while (p < line.size() && line[p] == ' ')
			++p;
		if (p >= line.size() || line[p] < '0' || line[p] > '9')
			return;
		unsigned lineNo = 0;
		while (p < line.size() && line[p] >= '0' && line[p] <= '9') {
			lineNo = lineNo * 10 + (unsigned)(line[p] - '0');
			if (lineNo > 32767u)
				return;
			++p;
		}

		if (!ATBasicProgramLineExists((uint16)lineNo))
			return;

		// Anchor to the same beam position the popup uses.  We anchor
		// at column 0 of the current row so the badge sits over the
		// number itself; the popup uses the cursor column, which would
		// place the badge further to the right and overlap input text.
		const sint32 cursorBeamX1 = 48 + 4 * (sint32)lmar;
		const sint32 cursorBeamY1 = 32 + 8 * (sint32)row;
		const sint32 cursorBeamY2 = cursorBeamY1 + 8;

		const ImVec2 anchorTL = BeamToScreen(cursorBeamX1, cursorBeamY1);
		const ImVec2 anchorBL = BeamToScreen(cursorBeamX1, cursorBeamY2);

		char buf[64];
		const int n = snprintf(buf, sizeof(buf), "Replaces line %u", lineNo);
		if (n <= 0)
			return;

		// Measure the text, frame in a small padded box.  Place above
		// the cursor row by default; flip below when there's no room.
		const ImVec2 ts = ImGui::CalcTextSize(buf);
		const ImVec2 pad = ImVec2(6.0f, 3.0f);
		const ImVec2 size = ImVec2(ts.x + pad.x * 2.0f, ts.y + pad.y * 2.0f);

		const ImGuiViewport *vp = ImGui::GetMainViewport();
		const float vpTop = vp->Pos.y;
		const float vpBottom = vp->Pos.y + vp->Size.y;
		const float vpRight  = vp->Pos.x + vp->Size.x;
		const float vpLeft   = vp->Pos.x;

		ImVec2 pos;
		if (anchorTL.y - size.y - 2.0f >= vpTop) {
			pos.y = anchorTL.y - size.y - 2.0f;
		} else {
			pos.y = std::min(anchorBL.y + 2.0f, vpBottom - size.y);
		}
		pos.x = anchorTL.x;
		if (pos.x + size.x > vpRight)
			pos.x = vpRight - size.x;
		if (pos.x < vpLeft)
			pos.x = vpLeft;

		// Draw via the foreground draw-list so the badge sits above any
		// emulator overlay (status bar, paused indicator) but below any
		// modal dialog that ImGui paints on its own viewport.
		ImDrawList *dl = ImGui::GetForegroundDrawList();

		// Warning amber: high-contrast against both Atari blue and black
		// borders; matches the colour we use elsewhere for hint/warning
		// status.  The dark border + light text guarantees readability
		// regardless of the underlying screen colour.
		const ImU32 kFill   = IM_COL32(0xFF, 0xC1, 0x07, 0xE0);
		const ImU32 kStroke = IM_COL32(0x40, 0x2D, 0x00, 0xFF);
		const ImU32 kText   = IM_COL32(0x18, 0x12, 0x00, 0xFF);

		const ImVec2 rTL = pos;
		const ImVec2 rBR = ImVec2(pos.x + size.x, pos.y + size.y);
		dl->AddRectFilled(rTL, rBR, kFill, 3.0f);
		dl->AddRect      (rTL, rBR, kStroke, 3.0f, 0, 1.0f);
		dl->AddText      (ImVec2(rTL.x + pad.x, rTL.y + pad.y), kText, buf);
	}
}
