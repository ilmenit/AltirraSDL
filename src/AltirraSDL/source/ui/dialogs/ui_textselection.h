//	AltirraSDL - Text selection system
//	Handles mouse-drag text selection on the emulated Atari display,
//	text extraction from screen memory via ANTIC DL history, and
//	copy operations (ASCII, Escaped, Hex, Unicode).

#pragma once

#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/vectors.h>
#include <vd2/system/vdstl.h>
#include <imgui.h>
#include "uidisplay.h"

class ATSimulator;

// One selected span of text on a single mode line (matches Windows TextSpan).
struct ATTextSpan {
	int mX;			// beam X (left edge, color clocks)
	int mWidth;		// beam width (color clocks)
	int mY;			// beam Y (scanline)
	int mHeight;	// height in scanlines (for highlight rect)
	int mCharX;		// character column (offset from left border)
	int mCharWidth;	// character count
};

// Global text selection state.
struct ATTextSelectionState {
	bool mbDragActive = false;		// mouse-drag in progress
	bool mbDragInitial = false;		// drag started but mouse hasn't moved yet
	bool mbHighlightsValid = false;	// highlight rects computed this frame
	int mAnchorBeamX = 0;			// drag start beam X
	int mAnchorBeamY = 0;			// drag start beam Y
	uint32 mDragStartTime = 0;		// VDGetCurrentTick() at drag start

	vdfastvector<ATTextSpan> mSpans;	// selected spans

	// Highlight rectangles (screen-space) for ImGui overlay drawing.
	// Each rect is (x1, y1, x2, y2) in absolute screen coordinates.
	struct HighlightRect {
		float x1, y1, x2, y2;
	};
	vdfastvector<HighlightRect> mHighlightRects;
};

// Get global selection state (singleton).
ATTextSelectionState& ATUIGetTextSelection();

// Returns true if any text is selected.
bool ATUIIsTextSelected();

// Selection operations.
void ATUITextSelectAll();
void ATUITextDeselect();
void ATUITextCopy(ATTextCopyMode mode);

// Mouse interaction — call from the Display pane render function.
// imagePos/imageSize are the screen-space rectangle of the emulator texture.
// Returns true if the selection system consumed the mouse event.
bool ATUITextSelectionHandleMouse(const ImVec2& imagePos, const ImVec2& imageSize);

// Draw selection highlight overlay — call after ImGui::Image() in the Display pane.
// When drawList is null the current window's draw list is used (debugger pane
// path).  Callers that render outside any ImGui window (main display path)
// should pass ImGui::GetBackgroundDrawList() so the highlight is drawn above
// the Atari frame but below ImGui windows.
void ATUITextSelectionDrawOverlay(const ImVec2& imagePos, const ImVec2& imageSize,
	struct ImDrawList *drawList = nullptr);
