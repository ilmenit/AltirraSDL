//	AltirraSDL - Touch-friendly ImGui widgets (implementation)
//	See touch_widgets.h for the API contract.

#include <stdafx.h>
#include <imgui.h>
#include <imgui_internal.h>
#include "touch_widgets.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

// The mobile UI's dp() helper is a static file-local in ui_mobile.cpp.
// We replicate a tiny version here that reads the current DPI scale
// from ImGui style so these widgets can be used from any file.
namespace {

static float s_contentScale = 1.0f;

float dp(float v) { return v * s_contentScale; }

// Approximate contentScale from the currently active ImGui style —
// we scale everything by FrameRounding×2 as a rough proxy (the mobile
// init does `style.ScaleAllSizes(cs)` which multiplies FrameRounding).
// More reliable: use the font size as the scale (base font is 16dp).
void UpdateContentScale() {
	ImGuiIO &io = ImGui::GetIO();
	// Base font size on mobile = 16dp × contentScale, so dividing by 16
	// recovers the scale factor.  On desktop the default font is
	// ~13px and contentScale is effectively 13/16 ≈ 0.81 — that's
	// fine for the widget sizes here since they still look clickable.
	float fs = io.Fonts->Fonts.Size > 0
		? io.Fonts->Fonts[0]->FontSize
		: 13.0f;
	s_contentScale = fs / 16.0f;
	if (s_contentScale < 1.0f) s_contentScale = 1.0f;
	if (s_contentScale > 4.0f) s_contentScale = 4.0f;
}

// Colour palette, matching the rest of the mobile UI (dark theme
// with a blue accent).
constexpr ImU32 kColAccent        = IM_COL32(64, 140, 230, 255);
constexpr ImU32 kColAccentDim     = IM_COL32(64, 140, 230, 140);
constexpr ImU32 kColRowHover      = IM_COL32(255, 255, 255, 20);
constexpr ImU32 kColTrackOff      = IM_COL32(60, 64, 80, 255);
constexpr ImU32 kColTrackOn       = IM_COL32(64, 140, 230, 255);
constexpr ImU32 kColThumb         = IM_COL32(240, 240, 245, 255);
constexpr ImU32 kColSegBgInactive = IM_COL32(45, 50, 65, 255);
constexpr ImU32 kColSegBgActive   = IM_COL32(64, 140, 230, 255);
constexpr ImU32 kColSegBorder     = IM_COL32(90, 100, 120, 255);
constexpr ImU32 kColText          = IM_COL32(230, 232, 240, 255);
constexpr ImU32 kColTextMuted     = IM_COL32(170, 180, 200, 255);
constexpr ImU32 kColTextOnAccent  = IM_COL32(255, 255, 255, 255);
constexpr ImU32 kColSection       = IM_COL32(110, 170, 235, 255);

} // namespace

// -------------------------------------------------------------------------
// Toggle switch
// -------------------------------------------------------------------------

bool ATTouchToggle(const char *label, bool *value) {
	UpdateContentScale();
	ImGuiWindow *window = ImGui::GetCurrentWindow();
	if (window->SkipItems) return false;

	ImGuiIO &io = ImGui::GetIO();

	const float rowH    = dp(56.0f);
	const float trackW  = dp(52.0f);
	const float trackH  = dp(28.0f);
	const float thumbR  = dp(12.0f);
	const float padLR   = dp(8.0f);

	ImVec2 cursor = window->DC.CursorPos;
	float availW = ImGui::GetContentRegionAvail().x;
	ImVec2 rowMin(cursor.x, cursor.y);
	ImVec2 rowMax(cursor.x + availW, cursor.y + rowH);

	// Unique ID + invisible full-row hit rect
	ImGuiID id = window->GetID(label);
	ImRect bb(rowMin, rowMax);
	ImGui::ItemSize(bb, 0.0f);
	if (!ImGui::ItemAdd(bb, id))
		return false;

	bool hovered, held;
	bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
	if (pressed) *value = !*value;

	ImDrawList *dl = window->DrawList;

	// Row hover tint
	if (hovered || held)
		dl->AddRectFilled(rowMin, rowMax, kColRowHover, dp(6.0f));

	// Label on the left, vertically centered
	ImVec2 ts = ImGui::CalcTextSize(label);
	dl->AddText(
		ImVec2(rowMin.x + padLR, rowMin.y + (rowH - ts.y) * 0.5f),
		kColText, label);

	// Track on the right
	float trackX1 = rowMax.x - padLR;
	float trackX0 = trackX1 - trackW;
	float trackY0 = rowMin.y + (rowH - trackH) * 0.5f;
	float trackY1 = trackY0 + trackH;

	ImU32 trackCol = *value ? kColTrackOn : kColTrackOff;
	dl->AddRectFilled(
		ImVec2(trackX0, trackY0), ImVec2(trackX1, trackY1),
		trackCol, trackH * 0.5f);

	// Thumb — fully slid when on, fully left when off.
	float thumbCy = (trackY0 + trackY1) * 0.5f;
	float thumbCx = *value
		? trackX1 - trackH * 0.5f
		: trackX0 + trackH * 0.5f;
	dl->AddCircleFilled(ImVec2(thumbCx, thumbCy), thumbR, kColThumb, 20);

	return pressed;
}

// -------------------------------------------------------------------------
// Segmented control
// -------------------------------------------------------------------------

bool ATTouchSegmented(const char *label, int *current,
	const char * const *items, int count)
{
	UpdateContentScale();
	ImGuiWindow *window = ImGui::GetCurrentWindow();
	if (window->SkipItems || count <= 0) return false;

	const float labelH = dp(22.0f);
	const float barH   = dp(48.0f);
	const float gapY   = dp(4.0f);
	const float rowH   = labelH + gapY + barH;
	const float padLR  = dp(8.0f);

	ImVec2 cursor = window->DC.CursorPos;
	float availW = ImGui::GetContentRegionAvail().x;
	ImVec2 rowMin(cursor.x, cursor.y);
	ImVec2 rowMax(cursor.x + availW, cursor.y + rowH);

	// Scope all per-segment IDs under the label so two segmented
	// controls on the same screen (e.g. "Video Standard" and
	// "Control Size") don't collide on their per-segment ids.
	ImGui::PushID(label);

	ImRect bb(rowMin, rowMax);
	ImGui::ItemSize(bb, 0.0f);
	// Note: no outer ItemAdd here — each segment registers its own
	// id via ItemAdd below so ImGui's hover/active tracking routes
	// touch taps to the correct segment.  A single outer ItemAdd
	// would swallow the hover for all segments and break tapping.

	ImDrawList *dl = window->DrawList;

	// Label on top
	dl->AddText(ImVec2(rowMin.x + padLR, rowMin.y), kColTextMuted, label);

	// Bar below
	float barY0 = rowMin.y + labelH + gapY;
	float barY1 = barY0 + barH;
	float barX0 = rowMin.x + padLR;
	float barX1 = rowMax.x - padLR;
	float segW = (barX1 - barX0) / (float)count;
	float rounding = dp(8.0f);

	bool changed = false;
	for (int i = 0; i < count; ++i) {
		float x0 = barX0 + segW * (float)i;
		float x1 = x0 + segW;
		bool isFirst = (i == 0);
		bool isLast  = (i == count - 1);

		ImRect segBB(ImVec2(x0, barY0), ImVec2(x1, barY1));
		ImGui::PushID(i);
		ImGuiID segId = window->GetID("##seg");
		// Register the segment as a hoverable item so ButtonBehavior
		// actually routes taps to it.  ItemAdd after the bar's
		// ItemSize does not advance the cursor.
		ImGui::ItemAdd(segBB, segId);
		bool segHovered, segHeld;
		bool segPressed = ImGui::ButtonBehavior(segBB, segId,
			&segHovered, &segHeld);
		ImGui::PopID();

		bool active = (*current == i);

		// Rounded corners only on the ends of the bar
		ImDrawFlags cornerFlags =
			(isFirst ? (ImDrawFlags_RoundCornersTopLeft | ImDrawFlags_RoundCornersBottomLeft) : 0)
			| (isLast ? (ImDrawFlags_RoundCornersTopRight | ImDrawFlags_RoundCornersBottomRight) : 0);
		if (!isFirst && !isLast) cornerFlags |= ImDrawFlags_RoundCornersNone;

		dl->AddRectFilled(segBB.Min, segBB.Max,
			active ? kColSegBgActive : kColSegBgInactive,
			rounding, cornerFlags);

		// Label
		ImVec2 ts = ImGui::CalcTextSize(items[i]);
		float tx = (x0 + x1) * 0.5f - ts.x * 0.5f;
		float ty = (barY0 + barY1) * 0.5f - ts.y * 0.5f;
		dl->AddText(ImVec2(tx, ty),
			active ? kColTextOnAccent : kColText,
			items[i]);

		// Inter-segment dividers
		if (!isLast) {
			dl->AddLine(
				ImVec2(x1, barY0 + dp(6.0f)),
				ImVec2(x1, barY1 - dp(6.0f)),
				kColSegBorder, 1.0f);
		}

		if (segPressed && *current != i) {
			*current = i;
			changed = true;
		}
	}

	// Outer border
	dl->AddRect(ImVec2(barX0, barY0), ImVec2(barX1, barY1),
		kColSegBorder, rounding, 0, 1.0f);

	ImGui::PopID();  // matches the PushID(label) at entry
	return changed;
}

// -------------------------------------------------------------------------
// Slider
// -------------------------------------------------------------------------

bool ATTouchSlider(const char *label, int *value, int minv, int maxv,
	const char *fmt)
{
	UpdateContentScale();
	ImGuiWindow *window = ImGui::GetCurrentWindow();
	if (window->SkipItems || maxv <= minv) return false;

	const float labelH = dp(22.0f);
	const float barH   = dp(48.0f);
	const float gapY   = dp(4.0f);
	const float rowH   = labelH + gapY + barH;
	const float padLR  = dp(8.0f);
	const float trackH = dp(4.0f);
	const float thumbR = dp(12.0f);
	const float labelW = dp(70.0f);

	ImVec2 cursor = window->DC.CursorPos;
	float availW = ImGui::GetContentRegionAvail().x;
	ImVec2 rowMin(cursor.x, cursor.y);
	ImVec2 rowMax(cursor.x + availW, cursor.y + rowH);

	ImGuiID id = window->GetID(label);
	ImRect bb(rowMin, rowMax);
	ImGui::ItemSize(bb, 0.0f);
	if (!ImGui::ItemAdd(bb, id))
		return false;

	ImDrawList *dl = window->DrawList;

	// Label on top
	dl->AddText(ImVec2(rowMin.x + padLR, rowMin.y), kColTextMuted, label);

	// Track
	float trackX0 = rowMin.x + padLR + thumbR;
	float trackX1 = rowMax.x - padLR - labelW;
	float trackY  = rowMin.y + labelH + gapY + barH * 0.5f;

	// Hit area is the full bar height plus the width of the track.
	ImRect hitBB(
		ImVec2(trackX0 - thumbR, rowMin.y + labelH + gapY),
		ImVec2(trackX1 + thumbR, rowMin.y + labelH + gapY + barH));

	bool hovered, held;
	bool pressed = ImGui::ButtonBehavior(hitBB, id, &hovered, &held);

	float t = (float)(*value - minv) / (float)(maxv - minv);
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;

	bool changed = false;
	if (held) {
		ImGuiIO &io = ImGui::GetIO();
		float mx = io.MousePos.x;
		float nt = (mx - trackX0) / (trackX1 - trackX0);
		if (nt < 0.0f) nt = 0.0f;
		if (nt > 1.0f) nt = 1.0f;
		int newV = minv + (int)std::lround(nt * (float)(maxv - minv));
		if (newV != *value) {
			*value = newV;
			changed = true;
			t = nt;
		}
	}

	float thumbX = trackX0 + (trackX1 - trackX0) * t;

	// Inactive track
	dl->AddRectFilled(
		ImVec2(trackX0, trackY - trackH * 0.5f),
		ImVec2(trackX1, trackY + trackH * 0.5f),
		kColTrackOff, trackH * 0.5f);

	// Active track (left of thumb)
	dl->AddRectFilled(
		ImVec2(trackX0, trackY - trackH * 0.5f),
		ImVec2(thumbX,  trackY + trackH * 0.5f),
		kColTrackOn, trackH * 0.5f);

	// Thumb
	dl->AddCircleFilled(ImVec2(thumbX, trackY), thumbR, kColThumb, 24);
	if (held)
		dl->AddCircle(ImVec2(thumbX, trackY), thumbR + dp(4.0f),
			kColAccentDim, 24, 2.0f);

	// Value label on the right
	char valueStr[32];
	snprintf(valueStr, sizeof(valueStr), fmt ? fmt : "%d", *value);
	ImVec2 vts = ImGui::CalcTextSize(valueStr);
	dl->AddText(
		ImVec2(rowMax.x - padLR - vts.x, trackY - vts.y * 0.5f),
		kColText, valueStr);

	return changed;
}

// -------------------------------------------------------------------------
// Touch-drag scroll
//
// Dear ImGui's BeginChild / Begin windows don't support touch-drag
// scrolling out of the box — you can only move the scrollbar or use
// a mouse wheel.  On a touchscreen neither is available, so any
// child with overflow is unreachable by the user.
//
// This helper installs per-window latched drag tracking.  Call it
// right after Begin/BeginChild.  On the first frame where the user
// puts a finger down anywhere inside the window, it captures the
// current scroll position in a fractional accumulator; on each
// subsequent frame while the finger is still down, it subtracts the
// pointer delta and pushes the result through SetScrollY.  Releasing
// the finger ends the drag.
//
// To co-exist with regular Buttons/Selectables inside the window,
// ATTouchIsDraggingBeyondSlop() lets callers suppress activation
// once the drag has exceeded a small tap-slop threshold.
// -------------------------------------------------------------------------

namespace {

struct DragState {
	ImGuiID window = 0;
	bool    active = false;
	float   scrollY = 0.0f;     // fractional accumulator
	ImVec2  pressAnchor{};
	float   dragDistSq = 0.0f;  // total distance moved during this press
	bool    exceededSlop = false; // sticky: kept set through release frame
	bool    clearedActive = false; // ClearActiveID already called this press
	int     pushedColors = 0;     // style colors pushed this frame, popped in End
};

// Keep a handful of per-window states so nested scroll regions
// (e.g. the file browser list inside the file browser window)
// don't clobber each other's drag state.
static DragState s_dragStates[8];

DragState *GetDragState(ImGuiID window) {
	for (auto &s : s_dragStates)
		if (s.window == window)
			return &s;
	// First free slot
	for (auto &s : s_dragStates)
		if (s.window == 0) { s.window = window; return &s; }
	// Recycle slot 0 if we somehow overflowed
	s_dragStates[0].window = window;
	s_dragStates[0].active = false;
	s_dragStates[0].dragDistSq = 0.0f;
	return &s_dragStates[0];
}

} // namespace

void ATTouchEndDragScroll() {
	ImGuiWindow *window = ImGui::GetCurrentWindow();
	if (!window) return;
	DragState *ds = GetDragState(window->ID);
	if (ds->pushedColors > 0) {
		ImGui::PopStyleColor(ds->pushedColors);
		ds->pushedColors = 0;
	}
}

void ATTouchDragScroll() {
	UpdateContentScale();
	ImGuiWindow *window = ImGui::GetCurrentWindow();
	if (!window) return;

	ImGuiIO &io = ImGui::GetIO();
	DragState *ds = GetDragState(window->ID);

	// Defensive: if a previous frame somehow left colors pushed
	// (e.g. caller forgot ATTouchEndDragScroll), drop the count.
	// They were already popped at end of frame by ImGui's stack
	// rebalance, so just zero the bookkeeping.
	ds->pushedColors = 0;

	bool hovered = ImGui::IsWindowHovered(
		ImGuiHoveredFlags_ChildWindows
		| ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

	bool mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);

	if (mouseDown && hovered) {
		if (!ds->active) {
			ds->active = true;
			ds->scrollY = ImGui::GetScrollY();
			ds->pressAnchor = io.MousePos;
			ds->dragDistSq = 0.0f;
			ds->exceededSlop = false;
			ds->clearedActive = false;
		}
		// Integrate finger delta into the scroll accumulator.
		ds->scrollY -= io.MouseDelta.y;
		float maxY = ImGui::GetScrollMaxY();
		if (ds->scrollY < 0.0f)  ds->scrollY = 0.0f;
		if (ds->scrollY > maxY)  ds->scrollY = maxY;
		ImGui::SetScrollY(ds->scrollY);

		// Accumulate total drag distance squared so the tap-slop
		// check can veto a click-through activation further down
		// the widget tree.
		ImVec2 d(io.MousePos.x - ds->pressAnchor.x,
		         io.MousePos.y - ds->pressAnchor.y);
		ds->dragDistSq = d.x * d.x + d.y * d.y;

		float slop = dp(10.0f);
		if (!ds->exceededSlop && ds->dragDistSq > slop * slop)
			ds->exceededSlop = true;

		// Once we've decided this press is a scroll gesture, cancel
		// whatever ImGui widget the press latched onto so its release
		// won't fire.  Without this, every Button/Selectable inside
		// the scrolling list activates when the finger lifts after a
		// scroll drag.
		if (ds->exceededSlop && !ds->clearedActive) {
			ImGui::ClearActiveID();
			ds->clearedActive = true;
		}

		// Mobile UX: a finger touching a scrollable list should NOT
		// light up every Button / Selectable it passes over.  Touch
		// screens have no real hover state — Material Design lists
		// only show the press ripple on the actually-tapped item, and
		// cancel even that as soon as a scroll begins.  We approximate
		// this in ImGui by forcing the Hovered/Active visuals back to
		// the resting colour for the duration of the touch.  The
		// click itself still works because Button uses ActiveID, not
		// the visual colour, to decide whether it fired.
		const ImGuiStyle &style = ImGui::GetStyle();
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, style.Colors[ImGuiCol_Button]);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive,  style.Colors[ImGuiCol_Button]);
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, style.Colors[ImGuiCol_Header]);
		ImGui::PushStyleColor(ImGuiCol_HeaderActive,  style.Colors[ImGuiCol_Header]);
		ds->pushedColors = 4;
	} else if (!mouseDown && ds->active) {
		// Keep exceededSlop set for THIS frame so any widget that
		// queries ATTouchIsDraggingBeyondSlop() during the release
		// frame still sees the drag.  Reset on the next press in the
		// `if (!ds->active)` branch above.
		ds->active = false;
		ds->dragDistSq = 0.0f;
		ds->clearedActive = false;
		// Note: exceededSlop intentionally NOT reset here.  It is
		// reset when the next press begins.
	}
}

bool ATTouchIsDraggingBeyondSlop() {
	UpdateContentScale();
	ImGuiWindow *window = ImGui::GetCurrentWindow();
	if (!window) return false;
	DragState *ds = GetDragState(window->ID);
	// Sticky flag: stays true through the release frame so tap-slop
	// suppression works on the frame the Selectable/Button reports
	// its click-release.
	return ds->exceededSlop;
}

// -------------------------------------------------------------------------
// Section header
// -------------------------------------------------------------------------

void ATTouchSection(const char *label) {
	UpdateContentScale();
	ImGui::Dummy(ImVec2(0, dp(16.0f)));
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(
		((kColSection >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f,
		((kColSection >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f,
		((kColSection >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f,
		1.0f));
	ImGui::TextUnformatted(label);
	ImGui::PopStyleColor();
	ImGui::Separator();
	ImGui::Dummy(ImVec2(0, dp(4.0f)));
}
