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

		// Anchor for the popup, in screen-pixel coordinates.  Updated
		// at the moment the popup opens; not re-anchored each frame so
		// the popup doesn't follow cursor motion while open.
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
				// read previous line, if there is one
				if (row >= 1) {
					const size_t prevLineLen = rmar + 1 - lmar;
					const uint16 prevAddr = addr + (row - 1) * 40 + lmar;

					line.mText.resize(prevLineLen);

					for(size_t i = 0; i < prevLineLen; ++i)
						line.mText[i] = g_sim.DebugReadByte((uint16)(prevAddr + lmar + i));
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
	// the SDL3 emulator viewport (g_displayRect).  The text mode (graphics 0)
	// drawing originates at beam position (48, 32) in standard NTSC/PAL frame
	// timing — same constants as the Win32 widget uses.  The visible image
	// width is 336 beams (with overscan) or 320 beams (normal); for the popup
	// we just use the normalized [0,1] mapping over the visible image area so
	// the popup tracks well enough for the common case.  The 384-wide /
	// 240-tall scan region is a defensible estimate; we clamp into
	// g_displayRect anyway.
	ImVec2 BeamToScreen(int beamX, int beamY) {
		const float kBeamW = 384.0f;
		const float kBeamH = 240.0f;

		const float nx = std::clamp((float)beamX / kBeamW, 0.0f, 1.0f);
		const float ny = std::clamp((float)beamY / kBeamH, 0.0f, 1.0f);

		return ImVec2(g_displayRect.x + nx * g_displayRect.w,
		              g_displayRect.y + ny * g_displayRect.h);
	}

	void RecomputeAnchor() {
		// ANTIC mode 2 (BASIC graphics 0) uses 4 beam cycles per character
		// column (each pixel is one color clock = half a beam cycle, and
		// the 8 mode-2 pixels span 4 cycles).  Y is 8 scanlines per row.
		// Constants match ATUIVideoDisplayWindow::UpdateAutoSuggest exactly.
		const sint32 cursorBeamX1 = 48 + 4 * g_state.mScan3.mCursorCol;
		const sint32 cursorBeamY1 = 32 + 8 * g_state.mScan3.mCursorRow;
		const sint32 cursorBeamY2 = cursorBeamY1 + 8;

		g_state.mAnchorTL = BeamToScreen(cursorBeamX1, cursorBeamY1);
		g_state.mAnchorBL = BeamToScreen(cursorBeamX1, cursorBeamY2);
	}

	void OpenPopupIfNeeded() {
		g_state.mbPopupOpen = true;
		RecomputeAnchor();

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

	if (!g_state.mpEngine || !g_state.mNumResults)
		return;

	// Decide above-vs-below placement based on remaining viewport space.
	// We size the window with AlwaysAutoResize and let ImGui clip it; the
	// 280px width is the typical Win32 popup width.
	const ImGuiViewport *vp = ImGui::GetMainViewport();
	const float vpBottom = vp->Pos.y + vp->Size.y;

	const float idealH = std::min(ImGui::GetTextLineHeightWithSpacing() * 10.0f, 240.0f);
	const float idealW = 320.0f;

	ImVec2 pos = g_state.mAnchorBL;
	if (pos.y + idealH > vpBottom && g_state.mAnchorTL.y > idealH)
		pos = ImVec2(g_state.mAnchorTL.x, g_state.mAnchorTL.y - idealH);

	// Clamp horizontally to the viewport.
	if (pos.x + idealW > vp->Pos.x + vp->Size.x)
		pos.x = vp->Pos.x + vp->Size.x - idealW;
	if (pos.x < vp->Pos.x)
		pos.x = vp->Pos.x;

	ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(idealW, idealH), ImGuiCond_Always);

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
