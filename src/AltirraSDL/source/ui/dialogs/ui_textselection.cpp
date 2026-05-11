//	AltirraSDL - Text selection system
//	Implements mouse-drag text selection on the emulated Atari display,
//	text extraction from screen memory, and copy to clipboard.
//	Port of Windows ATUIVideoDisplayWindow selection logic.

#include <stdafx.h>
#include <algorithm>
#include <cmath>
#include <SDL3/SDL.h>
#include <imgui.h>

#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/time.h>
#include <vd2/system/vdstl.h>
#include <at/atcore/atascii.h>

#include "ui_textselection.h"
#include "simulator.h"
#include "antic.h"
#include "gtia.h"
#include "memorymanager.h"
#include "uiaccessors.h"

extern ATSimulator g_sim;

// ---------------------------------------------------------------------------
// Singleton state
// ---------------------------------------------------------------------------

static ATTextSelectionState s_sel;

ATTextSelectionState& ATUIGetTextSelection() { return s_sel; }

bool ATUIIsTextSelected() {
	return !s_sel.mSpans.empty();
}

// ---------------------------------------------------------------------------
// Coordinate mapping helpers
// ---------------------------------------------------------------------------

// Map a pixel position within the display image to ANTIC beam coordinates.
// imagePos/imageSize = screen-space origin and size of the emulator texture.
// Matches Windows ATUIVideoDisplayWindow::MapPixelToBeamPosition
// (uivideodisplaywindow.cpp:2737): pixel centers map to beam centers, with the
// -0.5 offset accounting for the half-color-clock granularity of beam coords.
static bool MapPixelToBeam(const ImVec2& imagePos, const ImVec2& imageSize,
						   float px, float py, int& beamX, int& beamY) {
	float dx = px - imagePos.x;
	float dy = py - imagePos.y;

	if (dx < 0.0f) dx = 0.0f; if (dx > imageSize.x) dx = imageSize.x;
	if (dy < 0.0f) dy = 0.0f; if (dy > imageSize.y) dy = imageSize.y;

	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	const vdrect32 scanArea(gtia.GetFrameScanArea());

	const float xf = (float)scanArea.left
		+ (dx + 0.5f) * (float)scanArea.width()  / imageSize.x - 0.5f;
	const float yf = (float)scanArea.top
		+ (dy + 0.5f) * (float)scanArea.height() / imageSize.y - 0.5f;

	beamX = (int)floorf(xf + 0.5f);
	beamY = (int)floorf(yf + 0.5f);
	return true;
}

// Walk back from scanline ys to the start scanline of the mode line that
// contains it.  Returns -1 if no mode line is found.  Matches Windows
// ATUIVideoDisplayWindow::GetModeLineYPos (uivideodisplaywindow.cpp:3105),
// simplified to the checkValidCopyText=false path that the selection drag
// uses.
//
// This snap is essential: clicks typically land in the middle of a mode line
// where dlhist[yc].mbValid is false; the partial-line clipping in
// SelectByBeamPosition only fires when yc1/yc2 coincide with the iteration
// yc, which requires them to be the mode line *start* scanlines.  Without
// the snap, every selection collapses to full horizontal lines.
static int GetModeLineYPos(int ys) {
	ATAnticEmulator& antic = g_sim.GetAntic();
	const ATAnticEmulator::DLHistoryEntry *dlhist = antic.GetDLHistory();

	if (ys >= 248)
		ys = 247;

	for (; ys >= 8; --ys) {
		if (dlhist[ys].mbValid)
			return ys;
	}
	return -1;
}

// Map beam coordinates back to screen-space pixel position.
static ImVec2 MapBeamToPixel(const ImVec2& imagePos, const ImVec2& imageSize,
							 int beamX, int beamY) {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	const vdrect32 scanArea(gtia.GetFrameScanArea());

	float rx = (float)(beamX - scanArea.left) / (float)scanArea.width();
	float ry = (float)(beamY - scanArea.top)  / (float)scanArea.height();

	return ImVec2(imagePos.x + rx * imageSize.x,
				  imagePos.y + ry * imageSize.y);
}

// ---------------------------------------------------------------------------
// Selection by beam position (matches Windows SelectByBeamPositionAntic)
// ---------------------------------------------------------------------------

static void SelectByBeamPosition(int xc1, int yc1, int xc2, int yc2) {
	xc1 = std::clamp(xc1, 0, 228);
	xc2 = std::clamp(xc2, 0, 228);
	yc1 = std::clamp(yc1, 8, 248);
	yc2 = std::clamp(yc2, 8, 248);

	if (yc1 > yc2 || (yc1 == yc2 && xc1 > xc2)) {
		std::swap(xc1, xc2);
		std::swap(yc1, yc2);
	}

	ATAnticEmulator& antic = g_sim.GetAntic();
	const ATAnticEmulator::DLHistoryEntry *dlhist = antic.GetDLHistory();

	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	const vdrect32 scanArea(gtia.GetFrameScanArea());

	s_sel.mSpans.clear();

	for (int yc = yc1; yc <= yc2; ++yc) {
		if (!dlhist[yc].mbValid)
			continue;

		bool textModeLine = false;
		switch (dlhist[yc].mControl & 15) {
			case 2: case 3: case 6: case 7:
				textModeLine = true;
				break;
		}
		if (!textModeLine)
			continue;

		int pfwidth = dlhist[yc].mDMACTL & 3;
		if (!pfwidth)
			continue;
		if (pfwidth < 3 && (dlhist[yc].mControl & 0x10))
			++pfwidth;

		int left  = (yc == yc1) ? xc1 : scanArea.left;
		int right = (yc == yc2) ? xc2 : scanArea.right;

		const int leftborder = 0x50 - 0x10 * pfwidth;
		left  = std::max(left,  leftborder);
		right = std::min(right, 0xB0 + 0x10 * pfwidth);

		bool dblwide = false;
		switch (dlhist[yc].mControl & 15) {
			case 2: case 3:
				left  = (left  + 2) & ~3;
				right = (right + 2) & ~3;
				break;
			case 6: case 7:
				left  = (left  + 4) & ~7;
				right = (right + 4) & ~7;
				dblwide = true;
				break;
		}

		if (left >= right)
			continue;

		ATTextSpan ts;
		ts.mX = left;
		ts.mWidth = right - left;
		ts.mY = yc;
		ts.mHeight = 0;

		if (dblwide) {
			ts.mCharX = (left - leftborder) >> 3;
			ts.mCharWidth = (right - left) >> 3;
		} else {
			ts.mCharX = (left - leftborder) >> 2;
			ts.mCharWidth = (right - left) >> 2;
		}

		// Count consecutive blank lines (character height)
		for (int i = 0; i < 16; ++i) {
			++ts.mHeight;
			if (yc + ts.mHeight >= 248 || dlhist[yc + ts.mHeight].mbValid)
				break;
		}

		s_sel.mSpans.push_back(ts);
	}
}

// ---------------------------------------------------------------------------
// Update highlight rectangles from spans
// ---------------------------------------------------------------------------

static void UpdateHighlightRects(const ImVec2& imagePos, const ImVec2& imageSize) {
	s_sel.mHighlightRects.clear();

	for (const ATTextSpan& ts : s_sel.mSpans) {
		if (!ts.mWidth || !ts.mHeight)
			continue;

		ImVec2 p1 = MapBeamToPixel(imagePos, imageSize, ts.mX, ts.mY);
		ImVec2 p2 = MapBeamToPixel(imagePos, imageSize, ts.mX + ts.mWidth, ts.mY + ts.mHeight);

		ATTextSelectionState::HighlightRect hr;
		hr.x1 = p1.x;
		hr.y1 = p1.y;
		hr.x2 = p2.x;
		hr.y2 = p2.y;
		s_sel.mHighlightRects.push_back(hr);
	}
}

// ---------------------------------------------------------------------------
// ReadText — extract ATASCII characters from a text mode line
// (matches Windows ATUIVideoDisplayWindow::ReadText exactly)
// ---------------------------------------------------------------------------

static int ReadText(uint8 *dst, int yc, int startChar, int numChars, bool& intl) {
	ATAnticEmulator& antic = g_sim.GetAntic();
	const ATAnticEmulator::DLHistoryEntry *dlhist = antic.GetDLHistory();
	const ATAnticEmulator::DLHistoryEntry& dle = dlhist[yc];

	intl = false;

	if (!dle.mbValid)
		return 0;

	switch (dle.mControl & 15) {
		case 2: case 3: case 6: case 7: break;
		default: return 0;
	}

	// Compute width
	static const int kWidthLookup[2][4] = {
		{ 0, 16, 20, 24 },	// no horizontal scrolling
		{ 0, 20, 24, 24 },	// horizontal scrolling
	};

	int len = (dle.mControl & 4 ? 1 : 2)
		* kWidthLookup[(dle.mControl & 0x10) != 0][dle.mDMACTL & 3];

	// Clip
	if (numChars <= 0 || startChar >= len)
		return 0;
	if (startChar < 0) {
		numChars += startChar;
		startChar = 0;
	}
	if (numChars > len - startChar)
		numChars = len - startChar;
	if (numChars <= 0)
		return 0;

	// Read raw bytes via ANTIC memory
	uint8 data[48];
	g_sim.GetMemoryManager()->DebugAnticReadMemory(data, dle.mPFAddress + startChar, numChars);

	// Internal-to-ATASCII conversion
	static const uint8 kInternalToATASCIIXorTab[4] = { 0x20, 0x60, 0x40, 0x00 };

	uint8 mask = (dle.mControl & 4) ? 0x3f : 0xff;
	uint8 xorval = (dle.mControl & 4) && (dle.mCHBASE & 1) ? 0x40 : 0x00;

	if (!(dle.mControl & 4) && dle.mCHBASE == (0xCC >> 1))
		intl = true;

	for (int i = 0; i < numChars; ++i) {
		uint8 c = data[i];
		c &= mask;
		c ^= xorval;
		c ^= kInternalToATASCIIXorTab[(c & 0x60) >> 5];
		*dst++ = c;
	}

	return numChars;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ATUITextSelectAll() {
	SelectByBeamPosition(0, 8, 228, 248);
}

void ATUITextDeselect() {
	s_sel.mSpans.clear();
	s_sel.mHighlightRects.clear();
	s_sel.mbDragActive = false;
	s_sel.mbDragInitial = false;
	s_sel.mbHighlightsValid = false;
}

void ATUITextCopy(ATTextCopyMode mode) {
	if (s_sel.mSpans.empty())
		return;

	uint8 data[80];
	VDStringW s;

	for (const ATTextSpan& ts : s_sel.mSpans) {
		bool intl = false;
		int actual = ReadText(data, ts.mY, ts.mCharX, ts.mCharWidth, intl);
		if (!actual)
			continue;

		const auto& decodeTab = kATATASCIITables.mATASCIIToUnicode[intl];

		if (mode == ATTextCopyMode::Escaped) {
			uint8 inv = 0;
			bool started = false;

			for (int i = 0; i < actual; ++i) {
				uint8 c = data[i];

				if (!started) {
					if (c == 0x20)
						continue;
					started = true;
				}

				if ((c ^ inv) & 0x80) {
					inv ^= 0x80;
					s.append(L"{inv}");
				}

				c &= 0x7F;
				const uint16 wc = decodeTab[c];

				if (wc < 0x100)
					s += (wchar_t)wc;
				else if (c == 0x00)
					s.append(L"{^},");
				else if (c >= 0x01 && c < 0x1B) {
					s.append(L"{^}");
					s += (char)('a' + (c - 0x01));
				} else if (c == 0x1B) {
					s.append(L"{esc}{esc}");
				} else if (c == 0x1C) {
					if (inv)
						s.append(L"{esc}{+delete}");
					else
						s.append(L"{esc}{up}");
				} else if (c == 0x1D) {
					if (inv)
						s.append(L"{esc}{+insert}");
					else
						s.append(L"{esc}{down}");
				} else if (c == 0x1E) {
					if (inv)
						s.append(L"{esc}{^tab}");
					else
						s.append(L"{esc}{left}");
				} else if (c == 0x1F) {
					if (inv)
						s.append(L"{esc}{+tab}");
					else
						s.append(L"{esc}{right}");
				} else if (c == 0x60) {
					s.append(L"{^}.");
				} else if (c >= 0x61 && c < 0x7B) {
					s += (char)c;
				} else if (c == 0x7B) {
					s.append(L"{^};");
				} else if (c == 0x7D) {
					if (inv)
						s.append(L"{esc}{^}2");
					else
						s.append(L"{esc}{clear}");
				} else if (c == 0x7E) {
					if (inv)
						s.append(L"{esc}{del}");
					else
						s.append(L"{esc}{back}");
				} else if (c == 0x7F) {
					if (inv)
						s.append(L"{esc}{ins}");
					else
						s.append(L"{esc}{tab}");
				}
			}

			while (!s.empty() && s.back() == L' ')
				s.pop_back();

			if (inv)
				s.append(L"{inv}");

		} else if (mode == ATTextCopyMode::Hex) {
			for (int i = 0; i < actual; ++i)
				s.append_sprintf(L"%02X ", data[i]);
			if (actual)
				s.pop_back();

		} else {
			// ASCII or Unicode
			if (mode != ATTextCopyMode::Unicode) {
				for (int i = 0; i < actual; ++i) {
					uint16 wc = decodeTab[data[i] & 0x7f];
					data[i] = wc < 0x100 ? (uint8)wc : 0x20;
				}
			}

			int base = 0;
			while (base < actual && data[base] == 0x20)
				++base;
			while (actual > base && data[actual - 1] == 0x20)
				--actual;

			if (mode == ATTextCopyMode::Unicode) {
				for (int i = base; i < actual; ++i)
					s += (wchar_t)decodeTab[data[i] & 0x7f];
			} else {
				for (int i = base; i < actual; ++i)
					s += (wchar_t)data[i];
			}
		}

		s += L"\r\n";
	}

	if (s.size() > 2) {
		s.pop_back();
		s.pop_back();

		// Convert to UTF-8 and set clipboard
		VDStringA u8 = VDTextWToU8(s);
		SDL_SetClipboardText(u8.c_str());
	}
}

// ---------------------------------------------------------------------------
// Mouse interaction
// ---------------------------------------------------------------------------

bool ATUITextSelectionHandleMouse(const ImVec2& imagePos, const ImVec2& imageSize) {
	if (imageSize.x <= 0 || imageSize.y <= 0)
		return false;

	ImVec2 mouse = ImGui::GetMousePos();

	bool mouseInImage = mouse.x >= imagePos.x && mouse.x < imagePos.x + imageSize.x
					 && mouse.y >= imagePos.y && mouse.y < imagePos.y + imageSize.y;

	// --- LMB pressed: start a drag -----------------------------------------
	// Matches Windows uivideodisplaywindow.cpp:1269 — plain LMB starts a
	// drag; modifiers are not required.  Mouse must not be captured for
	// emulator input (paddle/light pen).
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouseInImage
		&& !ATUIIsMouseCaptured()) {
		int bx, by;
		MapPixelToBeam(imagePos, imageSize, mouse.x, mouse.y, bx, by);
		s_sel.mbDragActive = true;
		s_sel.mbDragInitial = true;
		s_sel.mAnchorBeamX = bx;
		s_sel.mAnchorBeamY = by;
		s_sel.mDragStartTime = VDGetCurrentTick();
		// Don't clear existing selection here — Windows intentionally
		// preserves it so touch users can Copy from the context menu
		// (uivideodisplaywindow.cpp:1277).
		return true;
	}

	// --- Active drag: update or finalize ------------------------------------
	if (s_sel.mbDragActive) {
		// I-beam cursor during drag (Windows: kATUICursorImage_IBeam,
		// uivideodisplaywindow.cpp:1385).
		ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);

		// Check for mouse release first — must be checked even when mouse
		// is outside the window to avoid stuck drag state.
		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			s_sel.mbDragActive = false;
			const bool quickClick = (VDGetCurrentTick() - s_sel.mDragStartTime) < 250;

			if (s_sel.mbDragInitial) {
				// Mouse never moved to a different beam position.
				// Windows (uivideodisplaywindow.cpp:1308): if < 250ms
				// this is a quick click — clear mbDragInitial but don't
				// update the preview (no actual selection).  Otherwise
				// just cancel.
				s_sel.mbDragInitial = false;
				if (quickClick && !s_sel.mSpans.empty()) {
					// Quick click on existing selection → deselect
					// (matches Windows:1314-1316 ClearDragPreview).
					ATUITextDeselect();
				}
				return true;
			}

			// Normal drag release — finalize selection.  Snap the
			// raw anchor/end Y coords to the start scanline of the
			// mode line that contains them (Windows
			// UpdateDragPreviewAntic, uivideodisplaywindow.cpp:2912).
			// GetModeLineYPos returns -1 when nothing is found above
			// the click — SelectByBeamPosition's internal clamp to
			// [8, 248] turns that into "start from the first scanline",
			// which matches Windows' behaviour (its GetModeLineYPos
			// returns 0 in the same case, also clamped to 8).
			int bx, by;
			MapPixelToBeam(imagePos, imageSize, mouse.x, mouse.y, bx, by);
			const int yc1 = GetModeLineYPos(s_sel.mAnchorBeamY);
			const int yc2 = GetModeLineYPos(by);
			SelectByBeamPosition(s_sel.mAnchorBeamX, yc1, bx, yc2);
			if (s_sel.mSpans.empty())
				ATUITextDeselect();
			return true;
		}

		int bx, by;
		MapPixelToBeam(imagePos, imageSize, mouse.x, mouse.y, bx, by);

		// Drag threshold: suppress selection preview until the mouse
		// moves to a different beam position from the anchor.  Matches
		// Windows UpdateDragPreviewAntic() mbDragInitial check
		// (uivideodisplaywindow.cpp:2854-2858).  Compare raw beam coords
		// (pre-snap) so any sub-mode-line motion still trips the threshold.
		if (s_sel.mbDragInitial) {
			if (bx == s_sel.mAnchorBeamX && by == s_sel.mAnchorBeamY)
				return true;
			s_sel.mbDragInitial = false;
		}

		// Snap to mode line start (see GetModeLineYPos comment for why).
		// -1 (no mode line above) is fine — SelectByBeamPosition's
		// std::clamp(yc, 8, 248) turns it into yc=8, which matches the
		// Windows fallback behaviour.
		const int yc1 = GetModeLineYPos(s_sel.mAnchorBeamY);
		const int yc2 = GetModeLineYPos(by);
		SelectByBeamPosition(s_sel.mAnchorBeamX, yc1, bx, yc2);
		UpdateHighlightRects(imagePos, imageSize);
		s_sel.mbHighlightsValid = true;
		return true;
	}

	return false;
}

// ---------------------------------------------------------------------------
// Draw selection highlight overlay
// ---------------------------------------------------------------------------

void ATUITextSelectionDrawOverlay(const ImVec2& imagePos, const ImVec2& imageSize,
	ImDrawList *drawList) {
	if (s_sel.mSpans.empty())
		return;

	// Recompute highlight rects if not already done this frame by mouse handler
	if (!s_sel.mbHighlightsValid)
		UpdateHighlightRects(imagePos, imageSize);
	s_sel.mbHighlightsValid = false;	// reset for next frame

	if (s_sel.mHighlightRects.empty())
		return;

	ImDrawList *dl = drawList ? drawList : ImGui::GetWindowDrawList();
	const ImU32 selColor = IM_COL32(0, 160, 255, 128);	// semi-transparent blue

	for (const auto& hr : s_sel.mHighlightRects) {
		dl->AddRectFilled(ImVec2(hr.x1, hr.y1), ImVec2(hr.x2, hr.y2), selColor);
	}
}
