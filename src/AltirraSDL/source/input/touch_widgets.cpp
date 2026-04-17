//	AltirraSDL - Touch-friendly ImGui widgets (implementation)
//	See touch_widgets.h for the API contract.

#include <stdafx.h>
#include <imgui.h>
#include <imgui_internal.h>
#include "touch_widgets.h"
#include "options.h"
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
	s_contentScale = (fs / 16.0f) * io.FontGlobalScale;
	if (s_contentScale < 0.5f) s_contentScale = 0.5f;
	if (s_contentScale > 5.0f) s_contentScale = 5.0f;
}

// Find the visible end of a label string — ImGui's convention is
// that text after "##" is the widget's ID suffix and should not be
// rendered (CalcTextSize(..., hide_text_after_double_hash=true)
// knows this, but ImDrawList::AddText does not — it just draws the
// raw buffer).  Match ImGui::FindRenderedTextEnd semantics.
const char *RenderedLabelEnd(const char *label) {
	if (!label) return nullptr;
	const char *p = label;
	while (*p) {
		if (p[0] == '#' && p[1] == '#') break;
		++p;
	}
	return p;
}

} // namespace

// -------------------------------------------------------------------------
// Theme palette
// -------------------------------------------------------------------------

// Single source of truth for the resolved theme — lives in ui_main.cpp
// so it can reach s_systemThemeIsDark (refreshed on SDL_EVENT_THEME_CHANGED).
extern bool ATUIIsDarkTheme();

const ATMobilePalette &ATMobileGetPalette() {
	// Two static palettes; re-resolve which one to return every call so a
	// theme switch takes effect immediately.
	static ATMobilePalette dark = []{
		ATMobilePalette p{};
		p.dark = true;

		// Rich near-black with a very subtle cool tint so the window
		// doesn't look like pure void; the gradient adds depth.
		p.windowBg         = IM_COL32( 22,  25,  35, 255);
		p.windowBgTop      = IM_COL32( 30,  34,  46, 255);

		p.cardBg           = IM_COL32( 34,  40,  56, 235);
		p.cardBgTop        = IM_COL32( 46,  54,  74, 235);
		p.cardBgHover      = IM_COL32( 50,  65, 100, 235);
		p.cardBgHoverTop   = IM_COL32( 66,  86, 130, 235);
		p.cardBorder       = IM_COL32(255, 255, 255,  16);
		p.backdropDim      = IM_COL32(  0,   0,   0, 160);

		p.accent           = IM_COL32( 64, 140, 230, 255);
		p.accentHover      = IM_COL32( 82, 158, 244, 255);
		p.accentPressed    = IM_COL32( 44, 114, 200, 255);
		p.accentSoft       = IM_COL32( 64, 140, 230, 140);
		p.accentTop        = IM_COL32( 86, 160, 248, 255);

		p.text             = IM_COL32(232, 236, 244, 255);
		p.textMuted        = IM_COL32(170, 182, 206, 255);
		p.textOnAccent     = IM_COL32(255, 255, 255, 255);
		p.textSection      = IM_COL32(120, 180, 240, 255);
		p.textTitle        = IM_COL32(110, 180, 255, 255);

		p.trackOff         = IM_COL32( 60,  66,  84, 255);
		p.trackOn          = p.accent;
		p.thumb            = IM_COL32(240, 242, 248, 255);

		p.segBgInactive    = IM_COL32( 45,  52,  70, 255);
		p.segBgInactiveTop = IM_COL32( 58,  66,  88, 255);
		p.segBgHover       = IM_COL32( 66,  78, 108, 255);
		p.segBgHoverTop    = IM_COL32( 80,  94, 130, 255);
		p.segBgActive      = p.accent;
		p.segBgActiveTop   = p.accentTop;
		p.segBorder        = IM_COL32(255, 255, 255,  22);
		p.segFocus         = IM_COL32(110, 180, 255, 200);

		p.rowHover         = IM_COL32(255, 255, 255,  22);
		p.rowFocus         = IM_COL32(110, 180, 255, 200);

		p.modalBg          = IM_COL32( 26,  32,  46, 250);
		p.modalBgTop       = IM_COL32( 40,  48,  68, 250);
		p.modalBorder      = IM_COL32( 70, 130, 210, 255);

		p.buttonHover      = IM_COL32(255, 255, 255,  24);
		p.buttonActive     = IM_COL32(255, 255, 255,  44);

		p.warning          = IM_COL32(255, 186,  64, 255);
		p.danger           = IM_COL32(240,  92,  92, 255);
		p.success          = IM_COL32(110, 208, 130, 255);
		return p;
	}();

	static ATMobilePalette light = []{
		ATMobilePalette p{};
		p.dark = false;

		// Soft off-white with a faint cool tint — warmer than pure
		// ImGui StyleColorsLight, closer to modern iOS/Material light.
		p.windowBg         = IM_COL32(238, 242, 248, 255);
		p.windowBgTop      = IM_COL32(248, 250, 254, 255);

		p.cardBg           = IM_COL32(255, 255, 255, 255);
		p.cardBgTop        = IM_COL32(252, 253, 255, 255);
		p.cardBgHover      = IM_COL32(220, 232, 248, 255);
		p.cardBgHoverTop   = IM_COL32(236, 244, 255, 255);
		p.cardBorder       = IM_COL32( 20,  30,  60,  32);
		p.backdropDim      = IM_COL32( 18,  24,  36, 110);

		p.accent           = IM_COL32( 28,  98, 196, 255);
		p.accentHover      = IM_COL32( 44, 118, 216, 255);
		p.accentPressed    = IM_COL32( 18,  78, 168, 255);
		p.accentSoft       = IM_COL32( 28,  98, 196, 140);
		p.accentTop        = IM_COL32( 52, 128, 226, 255);

		p.text             = IM_COL32( 24,  32,  48, 255);
		p.textMuted        = IM_COL32( 88, 100, 124, 255);
		p.textOnAccent     = IM_COL32(255, 255, 255, 255);
		p.textSection      = IM_COL32( 22,  82, 172, 255);
		p.textTitle        = IM_COL32( 22,  82, 172, 255);

		p.trackOff         = IM_COL32(194, 202, 218, 255);
		p.trackOn          = p.accent;
		p.thumb            = IM_COL32(255, 255, 255, 255);

		p.segBgInactive    = IM_COL32(226, 232, 242, 255);
		p.segBgInactiveTop = IM_COL32(240, 244, 252, 255);
		p.segBgHover       = IM_COL32(208, 222, 244, 255);
		p.segBgHoverTop    = IM_COL32(224, 236, 250, 255);
		p.segBgActive      = p.accent;
		p.segBgActiveTop   = p.accentTop;
		p.segBorder        = IM_COL32( 20,  30,  60,  38);
		p.segFocus         = IM_COL32( 28,  98, 196, 200);

		p.rowHover         = IM_COL32( 20,  60, 140,  20);
		p.rowFocus         = IM_COL32( 28,  98, 196, 200);

		p.modalBg          = IM_COL32(252, 253, 255, 252);
		p.modalBgTop       = IM_COL32(255, 255, 255, 252);
		p.modalBorder      = IM_COL32( 28,  98, 196, 255);

		p.buttonHover      = IM_COL32( 20,  60, 140,  28);
		p.buttonActive     = IM_COL32( 20,  60, 140,  52);

		// Warnings in Light mode need a darker amber — a pale amber on
		// off-white (the light cardBg) has almost no contrast.
		p.warning          = IM_COL32(184, 108,  10, 255);
		p.danger           = IM_COL32(186,  42,  42, 255);
		p.success          = IM_COL32( 40, 132,  68, 255);
		return p;
	}();

	return ATUIIsDarkTheme() ? dark : light;
}

void ATMobileDrawGradientRect(const ImVec2 &p1, const ImVec2 &p2,
	uint32 topCol, uint32 bottomCol, float rounding)
{
	ImDrawList *dl = ImGui::GetWindowDrawList();
	if (!dl) return;

	// ImGui's AddRectFilledMultiColor does not honour rounding — it
	// paints a flat axis-aligned rectangle.  Naively stamping the
	// gradient over a rounded AddRectFilled base makes the gradient
	// overflow into the outside-corner region (visible as sharp
	// rectangular corners on what was supposed to be a rounded
	// pill/card), because AddRectFilled leaves those corner pixels
	// un-painted — the gradient is the first thing drawn there.
	//
	// Fix: paint the rounded base in the bottom colour, then inset
	// the gradient horizontally by `rounding` so its rectangular
	// bounding box fits entirely inside the horizontal extent of the
	// rounded rect's top and bottom edges.  The rounded corner arcs
	// stay solid bottom-colour; the interior picks up the gradient.
	if (rounding > 0.0f) {
		dl->AddRectFilled(p1, p2, bottomCol, rounding);
		ImVec2 gMin(p1.x + rounding, p1.y);
		ImVec2 gMax(p2.x - rounding, p2.y);
		if (gMax.x > gMin.x && gMax.y > gMin.y) {
			dl->AddRectFilledMultiColor(gMin, gMax,
				topCol, topCol, bottomCol, bottomCol);
		}
	} else {
		dl->AddRectFilledMultiColor(p1, p2, topCol, topCol,
			bottomCol, bottomCol);
	}
}

// -------------------------------------------------------------------------
// Toggle switch
// -------------------------------------------------------------------------

bool ATTouchToggle(const char *label, bool *value) {
	UpdateContentScale();
	ImGuiWindow *window = ImGui::GetCurrentWindow();
	if (window->SkipItems) return false;

	const ATMobilePalette &pal = ATMobileGetPalette();

	const float rowH    = dp(56.0f);
	const float trackW  = dp(52.0f);
	const float trackH  = dp(28.0f);
	const float thumbR  = dp(12.0f);
	const float padLR   = dp(8.0f);

	ImVec2 cursor = window->DC.CursorPos;
	float availW = ImGui::GetContentRegionAvail().x;
	ImVec2 rowMin(cursor.x, cursor.y);
	ImVec2 rowMax(cursor.x + availW, cursor.y + rowH);

	ImGuiID id = window->GetID(label);
	ImRect bb(rowMin, rowMax);
	ImGui::ItemSize(bb, 0.0f);
	if (!ImGui::ItemAdd(bb, id))
		return false;

	bool hovered, held;
	bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
	if (pressed) *value = !*value;

	bool focused = ImGui::IsItemFocused();

	ImDrawList *dl = window->DrawList;
	auto A = [](uint32 col) { return (uint32)ImGui::GetColorU32(col); };

	if (hovered || held || focused)
		dl->AddRectFilled(rowMin, rowMax, A(pal.rowHover), dp(6.0f));
	if (focused)
		dl->AddRect(rowMin, rowMax, A(pal.rowFocus), dp(6.0f), 0, dp(2.0f));

	ImVec2 ts = ImGui::CalcTextSize(label);
	dl->AddText(
		ImVec2(rowMin.x + padLR, rowMin.y + (rowH - ts.y) * 0.5f),
		A(pal.text), label);

	float trackX1 = rowMax.x - padLR;
	float trackX0 = trackX1 - trackW;
	float trackY0 = rowMin.y + (rowH - trackH) * 0.5f;
	float trackY1 = trackY0 + trackH;
	float pillR = trackH * 0.5f;

	// Solid pill — rounded silhouette, no corner bleed.  The on state
	// additionally gets a subtle top-half gloss: a second rounded rect
	// that reuses the pill's top corners (ImDrawFlags_RoundCornersTop)
	// with a sharp straight bottom edge, filled in accentTop at low
	// alpha.  This reads as "lit from above" without the rectangular
	// corner bleed that AddRectFilledMultiColor would produce on a
	// tight pill shape.
	ImU32 trackCol = A(*value ? pal.trackOn : pal.trackOff);
	dl->AddRectFilled(
		ImVec2(trackX0, trackY0), ImVec2(trackX1, trackY1),
		trackCol, pillR);
	if (*value) {
		uint32 glossTop = (pal.accentTop & 0x00FFFFFFu) | (90u << IM_COL32_A_SHIFT);
		dl->AddRectFilled(
			ImVec2(trackX0, trackY0),
			ImVec2(trackX1, trackY0 + trackH * 0.55f),
			A(glossTop), pillR, ImDrawFlags_RoundCornersTop);
	}

	float thumbCy = (trackY0 + trackY1) * 0.5f;
	float thumbCx = *value
		? trackX1 - trackH * 0.5f
		: trackX0 + trackH * 0.5f;
	// Shadow under the thumb for a tactile lift, then the thumb itself.
	dl->AddCircleFilled(ImVec2(thumbCx, thumbCy + dp(1.0f)),
		thumbR, A(IM_COL32(0, 0, 0, 40)), 20);
	dl->AddCircleFilled(ImVec2(thumbCx, thumbCy), thumbR, A(pal.thumb), 20);

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

	const ATMobilePalette &pal = ATMobileGetPalette();

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
	auto A = [](uint32 col) { return (uint32)ImGui::GetColorU32(col); };

	// Label on top
	dl->AddText(ImVec2(rowMin.x + padLR, rowMin.y),
		A(pal.textMuted), label);

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
		ImGui::ItemAdd(segBB, segId);
		bool segHovered, segHeld;
		bool segPressed = ImGui::ButtonBehavior(segBB, segId,
			&segHovered, &segHeld);
		ImGui::PopID();

		bool active = (*current == i);
		bool segFocused = ImGui::IsItemFocused();

		// Rounded corners only on the ends of the bar
		ImDrawFlags cornerFlags =
			(isFirst ? (ImDrawFlags_RoundCornersTopLeft | ImDrawFlags_RoundCornersBottomLeft) : 0)
			| (isLast ? (ImDrawFlags_RoundCornersTopRight | ImDrawFlags_RoundCornersBottomRight) : 0);
		if (!isFirst && !isLast) cornerFlags |= ImDrawFlags_RoundCornersNone;

		ImU32 segBg = A(active ? pal.segBgActive
			: (segHovered || segFocused) ? pal.segBgHover
			: pal.segBgInactive);
		dl->AddRectFilled(segBB.Min, segBB.Max, segBg,
			rounding, cornerFlags);

		// Top highlight — a gradient overlay covering the upper
		// ~40% of the segment, fading from a semi-transparent light
		// tint at the top to fully transparent at the mid.  Inset
		// horizontally by `rounding` on the first/last segment so the
		// overlay does not bleed into the rounded corner arcs.  Every
		// state (active / hover / inactive) gets the same treatment so
		// the control reads as a single glossy bar rather than one
		// lit segment next to flat neighbours.
		{
			float glossH = (segBB.Max.y - segBB.Min.y) * 0.45f;
			float gX0 = segBB.Min.x + (isFirst ? rounding : 0.0f);
			float gX1 = segBB.Max.x - (isLast  ? rounding : 0.0f);
			ImVec2 gMin(gX0, segBB.Min.y);
			ImVec2 gMax(gX1, segBB.Min.y + glossH);
			int alpha = active ? 120 : (segHovered ? 70 : 50);
			if (!pal.dark) alpha = (int)(alpha * 0.6f);
			uint32 glossTop    = A(IM_COL32(255, 255, 255, alpha));
			uint32 glossBottom = A(IM_COL32(255, 255, 255,  0));
			dl->AddRectFilledMultiColor(gMin, gMax,
				glossTop, glossTop, glossBottom, glossBottom);
		}

		// Top accent-coloured highlight line on active segment.
		if (active) {
			float hiY = segBB.Min.y;
			dl->AddLine(
				ImVec2(segBB.Min.x + (isFirst ? rounding : 0), hiY + 0.5f),
				ImVec2(segBB.Max.x - (isLast  ? rounding : 0), hiY + 0.5f),
				A(pal.accentTop), dp(1.5f));
		}

		if (segFocused)
			dl->AddRect(segBB.Min, segBB.Max, A(pal.segFocus),
				rounding, cornerFlags, dp(2.0f));

		// Label
		ImVec2 ts = ImGui::CalcTextSize(items[i]);
		float tx = (x0 + x1) * 0.5f - ts.x * 0.5f;
		float ty = (barY0 + barY1) * 0.5f - ts.y * 0.5f;
		dl->AddText(ImVec2(tx, ty),
			A(active ? pal.textOnAccent : pal.text),
			items[i]);

		// Inter-segment dividers
		if (!isLast) {
			dl->AddLine(
				ImVec2(x1, barY0 + dp(6.0f)),
				ImVec2(x1, barY1 - dp(6.0f)),
				A(pal.segBorder), 1.0f);
		}

		if (segPressed && *current != i) {
			*current = i;
			changed = true;
		}
	}

	// Outer border
	dl->AddRect(ImVec2(barX0, barY0), ImVec2(barX1, barY1),
		A(pal.segBorder), rounding, 0, 1.0f);

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

	const ATMobilePalette &pal = ATMobileGetPalette();

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
	auto A = [](uint32 col) { return (uint32)ImGui::GetColorU32(col); };

	// Label on top
	dl->AddText(ImVec2(rowMin.x + padLR, rowMin.y),
		A(pal.textMuted), label);

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
	bool focused = ImGui::IsItemFocused();

	if (hovered || focused)
		dl->AddRectFilled(rowMin, rowMax, A(pal.rowHover), dp(6.0f));
	if (focused)
		dl->AddRect(rowMin, rowMax, A(pal.rowFocus),
			dp(6.0f), 0, dp(2.0f));

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
		A(pal.trackOff), trackH * 0.5f);

	// Active track (left of thumb)
	dl->AddRectFilled(
		ImVec2(trackX0, trackY - trackH * 0.5f),
		ImVec2(thumbX,  trackY + trackH * 0.5f),
		A(pal.trackOn), trackH * 0.5f);

	// Thumb shadow + thumb
	dl->AddCircleFilled(ImVec2(thumbX, trackY + dp(1.0f)),
		thumbR, A(IM_COL32(0, 0, 0, 48)), 24);
	dl->AddCircleFilled(ImVec2(thumbX, trackY), thumbR,
		A(pal.thumb), 24);
	if (held)
		dl->AddCircle(ImVec2(thumbX, trackY), thumbR + dp(4.0f),
			A(pal.accentSoft), 24, 2.0f);

	// Value label on the right
	char valueStr[32];
	snprintf(valueStr, sizeof(valueStr), fmt ? fmt : "%d", *value);
	ImVec2 vts = ImGui::CalcTextSize(valueStr);
	dl->AddText(
		ImVec2(rowMax.x - padLR - vts.x, trackY - vts.y * 0.5f),
		A(pal.text), valueStr);

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

// Momentum scroll tunables.  Values are in dp (logical pixels) where
// noted — the dp() helper rescales for HiDPI.  Only the decay rate is
// dimensionless.
//
// kInertiaDecayPerSec controls the flick "glide distance".  With 6.0,
// velocity decays to ~2% of the release value in ~0.65s — close to the
// feel of Material scroll lists without being floaty.
constexpr float kInertiaDecayPerSec       = 6.0f;
constexpr float kMinFlingVelocityDpPerSec = 120.0f; // release below this = no inertia
constexpr float kStopVelocityDpPerSec     = 40.0f;  // inertia halts when |v| drops below
constexpr float kVelocitySampleWinSec     = 0.060f; // fit window
constexpr int   kVelocitySampleCount      = 8;
constexpr float kExternalScrollTolerance  = 2.0f;   // px: external change cancels inertia

struct VelSample {
	float t;       // seconds (ImGui::GetTime)
	float scrollY; // absolute scroll position at sample time
};

struct DragState {
	ImGuiID window = 0;
	bool    active = false;
	float   scrollY = 0.0f;     // fractional accumulator
	ImVec2  pressAnchor{};
	float   dragDistSq = 0.0f;  // total distance moved during this press
	bool    exceededSlop = false; // sticky: kept set through release frame
	bool    clearedActive = false; // ClearActiveID already called this press
	int     pushedColors = 0;     // style colors pushed this frame, popped in End

	// --- Momentum scrolling (touch input only) ---
	VelSample samples[kVelocitySampleCount]{};
	int       sampleCount = 0;
	int       sampleHead  = 0;    // next write index (ring buffer)
	float     velocity    = 0.0f; // px/s; sign matches scrollY direction
	bool      inInertia   = false;
	bool      touchInput  = false; // was the current/last drag from a touch?
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
	s_dragStates[0] = DragState{};
	s_dragStates[0].window = window;
	return &s_dragStates[0];
}

void PushVelocitySample(DragState *ds, float t, float scrollY) {
	ds->samples[ds->sampleHead] = { t, scrollY };
	ds->sampleHead = (ds->sampleHead + 1) % kVelocitySampleCount;
	if (ds->sampleCount < kVelocitySampleCount)
		++ds->sampleCount;
}

// Least-squares fit of scrollY vs. time over the newest samples that
// fall inside the velocity window.  Returns px/s.  The 60ms window
// naturally averages out a single near-stationary sample at the very
// end of a drag (the finger-lift deceleration artifact), so a slow
// release after a quick flick still produces a reasonable velocity.
float EstimateVelocity(const DragState &ds, float nowT) {
	if (ds.sampleCount < 2) return 0.0f;

	float sumT = 0, sumY = 0, sumTT = 0, sumTY = 0;
	int n = 0;

	// Walk from newest to oldest.
	int idx = ds.sampleHead;
	for (int i = 0; i < ds.sampleCount; ++i) {
		idx = (idx - 1 + kVelocitySampleCount) % kVelocitySampleCount;
		const VelSample &s = ds.samples[idx];
		float dt = s.t - nowT; // <= 0
		if (-dt > kVelocitySampleWinSec) break;
		sumT += dt; sumY += s.scrollY;
		sumTT += dt * dt; sumTY += dt * s.scrollY;
		++n;
	}
	if (n < 2) return 0.0f;
	float denom = (float)n * sumTT - sumT * sumT;
	if (std::fabs(denom) < 1e-6f) return 0.0f;
	return ((float)n * sumTY - sumT * sumY) / denom;
}

void ResetVelocityState(DragState *ds) {
	ds->velocity = 0.0f;
	ds->inInertia = false;
	ds->sampleCount = 0;
	ds->sampleHead = 0;
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
	const bool mouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
	const float nowT = (float)ImGui::GetTime();

	// Flywheel-catch: ANY fresh press (inside this window, on or off
	// the scrollbar) cancels an in-progress inertia glide, so the user
	// can grab the list mid-fling and the scrollbar keeps working.
	// Runs before the scrollbar short-circuit below so the glide
	// doesn't silently continue while the user is dragging the bar.
	bool caughtInertia = false;
	if (mouseClicked && hovered && ds->inInertia) {
		ds->inInertia = false;
		ds->velocity = 0.0f;
		caughtInertia = true;
	}

	if (mouseDown && hovered) {
		bool justActivated = false;
		if (!ds->active) {
			// Don't capture drag if the press is on the scrollbar —
			// let ImGui handle it natively.  The scrollbar occupies the
			// rightmost ScrollbarSize pixels of the window.
			if (window->ScrollbarY) {
				float scrollbarLeft = window->InnerRect.Max.x;
				if (io.MousePos.x >= scrollbarLeft) {
					ds->active = false;
					goto skip_drag;
				}
			}
			ds->active = true;
			ds->scrollY = ImGui::GetScrollY();
			ds->pressAnchor = io.MousePos;
			ds->dragDistSq = 0.0f;
			ds->exceededSlop = false;
			ds->clearedActive = false;
			ds->sampleCount = 0;
			ds->sampleHead = 0;
			// Only arm inertia if this press originated on a touch
			// surface.  Desktop mouse drag keeps its existing 1:1
			// behaviour with no fling on release.
			ds->touchInput =
				(io.MouseSource == ImGuiMouseSource_TouchScreen);
			justActivated = true;

			// If this press caught an in-flight glide, treat it as a
			// pure "stop the scroll" gesture and suppress the item
			// that would otherwise activate on release.  We can't
			// clear ActiveID on this frame (the item hasn't been
			// submitted yet) — instead we pre-set exceededSlop and
			// leave clearedActive=false, so the drag-frame block
			// runs ClearActiveID next frame after the item has
			// latched the press.
			if (caughtInertia)
				ds->exceededSlop = true;
		}
		// Skip delta integration on the activation frame.  ImGui's
		// MouseDelta is computed against the previous frame's cursor
		// position, which for a fresh touch can be anywhere (the
		// synthetic mouse warps to the finger on FINGER_DOWN).  The
		// next frame has a valid delta.
		if (!justActivated) {
			ds->scrollY -= io.MouseDelta.y;
			float maxY = ImGui::GetScrollMaxY();
			if (ds->scrollY < 0.0f)  ds->scrollY = 0.0f;
			if (ds->scrollY > maxY)  ds->scrollY = maxY;
			ImGui::SetScrollY(ds->scrollY);
		}

		// Record one velocity sample per frame (including the
		// activation frame, which seeds the ring with the starting
		// position).  The estimator picks the newest samples within
		// a ~60ms window, so the exact frame rate doesn't matter.
		if (ds->touchInput)
			PushVelocitySample(ds, nowT, ds->scrollY);

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
		// Release: compute fling velocity from the sample ring and
		// enter inertia if the user was flinging (exceeded slop AND
		// the fit velocity is above kMinFlingVelocity).  Otherwise
		// just stop.  Keep exceededSlop set for THIS frame so any
		// widget that queries ATTouchIsDraggingBeyondSlop() during
		// the release frame still sees the drag — it's reset on the
		// next press in the `if (!ds->active)` branch above.
		if (ds->touchInput && ds->exceededSlop) {
			float v = EstimateVelocity(*ds, nowT);
			float minFling = dp(kMinFlingVelocityDpPerSec);
			if (std::fabs(v) >= minFling) {
				ds->velocity = v;
				ds->inInertia = true;
			} else {
				ds->velocity = 0.0f;
				ds->inInertia = false;
			}
		} else {
			ds->velocity = 0.0f;
			ds->inInertia = false;
		}
		ds->active = false;
		ds->dragDistSq = 0.0f;
		ds->clearedActive = false;
		ds->sampleCount = 0;
		ds->sampleHead = 0;
	} else if (ds->inInertia) {
		// Advance the inertia glide.  If something external (keyboard
		// nav, programmatic SetScrollY) moved the scroll position
		// since our last write, honour that and cancel the glide.
		float actual = ImGui::GetScrollY();
		if (std::fabs(actual - ds->scrollY) > kExternalScrollTolerance) {
			ResetVelocityState(ds);
			goto skip_drag;
		}
		float dt = io.DeltaTime;
		if (dt > 0.05f) dt = 0.05f;  // clamp pathological spikes
		ds->scrollY += ds->velocity * dt;
		float maxY = ImGui::GetScrollMaxY();
		bool hitEdge = false;
		if (ds->scrollY < 0.0f)  { ds->scrollY = 0.0f;  hitEdge = true; }
		if (ds->scrollY > maxY)  { ds->scrollY = maxY;  hitEdge = true; }
		if (hitEdge) ds->velocity = 0.0f;
		ds->velocity *= std::exp(-kInertiaDecayPerSec * dt);
		ImGui::SetScrollY(ds->scrollY);

		float stop = dp(kStopVelocityDpPerSec);
		if (std::fabs(ds->velocity) < stop) {
			ds->velocity = 0.0f;
			ds->inInertia = false;
		}
	}
skip_drag: ;
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

// -------------------------------------------------------------------------
// ATTouchButton
//
// Draws a palette-aware, gradient-filled button at the current cursor.
// Matches the category-card aesthetic from the settings home screen
// so all primary interactive surfaces across Gaming Mode share one
// visual language.
//
// Corner-bleed workaround: AddRectFilledMultiColor has no rounding
// support.  We paint the rounded bottom-colour base first, then stamp
// the gradient over the same bounding box and re-paint the rounded
// silhouette in the bottom colour on top so the hard-cornered gradient
// edges are masked behind the final rounded frame.  The net effect is
// a smooth top-to-bottom fade everywhere except the inner ~1-2px of
// each corner arc, which reads as the base colour — looks intentional.
// -------------------------------------------------------------------------

bool ATTouchButton(const char *label, const ImVec2 &sizeArg,
	ATTouchButtonStyle style)
{
	UpdateContentScale();
	ImGuiWindow *window = ImGui::GetCurrentWindow();
	if (window->SkipItems) return false;

	const ATMobilePalette &pal = ATMobileGetPalette();

	// Pick top/bottom gradient colours + text colour per variant.
	struct Colors { uint32 top, mid, bot, textCol, border; };
	Colors c{};
	switch (style) {
	case ATTouchButtonStyle::Accent:
		c.top     = pal.accentTop;
		c.mid     = pal.accent;
		c.bot     = pal.accentPressed;
		c.textCol = pal.textOnAccent;
		c.border  = pal.accentSoft;
		break;
	case ATTouchButtonStyle::Danger:
		// Semantic red derived on-the-fly from palette-neutral ramps
		// so both themes produce a recognisable warning colour.
		c.top     = pal.dark
			? IM_COL32(232,  92,  92, 255)
			: IM_COL32(214,  64,  64, 255);
		c.mid     = pal.dark
			? IM_COL32(198,  70,  70, 255)
			: IM_COL32(186,  42,  42, 255);
		c.bot     = pal.dark
			? IM_COL32(160,  50,  50, 255)
			: IM_COL32(150,  30,  30, 255);
		c.textCol = IM_COL32(255, 248, 248, 255);
		c.border  = pal.dark
			? IM_COL32(255, 120, 120, 120)
			: IM_COL32(214,  64,  64, 140);
		break;
	case ATTouchButtonStyle::Subtle:
		c.top     = 0;
		c.mid     = 0;
		c.bot     = 0;
		c.textCol = pal.text;
		c.border  = 0;
		break;
	case ATTouchButtonStyle::Neutral:
	default:
		c.top     = pal.cardBgTop;
		c.mid     = pal.cardBg;
		c.bot     = pal.cardBg;
		c.textCol = pal.text;
		c.border  = pal.cardBorder;
		break;
	}

	// Resolve the requested size.  -FLT_MIN / -1 = fill axis; 0 = auto.
	ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);
	const float padLR = dp(16.0f);
	const float minH  = dp(56.0f);
	float w = sizeArg.x;
	float h = sizeArg.y;
	if (w <= 0.0f) {
		float avail = ImGui::GetContentRegionAvail().x;
		w = (w < 0.0f) ? avail : (labelSize.x + padLR * 2);
		if (w > avail) w = avail;
	}
	if (h <= 0.0f) {
		h = (h < 0.0f)
			? ImGui::GetContentRegionAvail().y
			: std::max(minH, labelSize.y + dp(20.0f));
	}

	ImVec2 cursor = window->DC.CursorPos;
	ImVec2 rowMin(cursor.x, cursor.y);
	ImVec2 rowMax(cursor.x + w, cursor.y + h);

	ImGuiID id = window->GetID(label);
	ImRect bb(rowMin, rowMax);
	ImGui::ItemSize(bb, 0.0f);
	if (!ImGui::ItemAdd(bb, id))
		return false;

	bool hovered, held;
	bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
	// Touch UX: a tap that turns into a scroll drag should not fire
	// the button.  ATTouchDragScroll sets a sticky "beyond slop" flag
	// for the current frame; honour it by swallowing the press.
	if (pressed && ATTouchIsDraggingBeyondSlop())
		pressed = false;
	bool focused = ImGui::IsItemFocused();

	ImDrawList *dl = window->DrawList;
	float rounding = dp(10.0f);

	// Apply the currently-active ImGui style.Alpha (halved under
	// BeginDisabled) to every palette colour up-front so the draw
	// sites below don't need to know about disabled state.  Without
	// this, a disabled ATTouchButton would render at full opacity and
	// the user couldn't tell it was disabled.
	auto A = [](uint32 col) { return (uint32)ImGui::GetColorU32(col); };
	c.top     = A(c.top);
	c.mid     = A(c.mid);
	c.bot     = A(c.bot);
	c.textCol = A(c.textCol);
	c.border  = A(c.border);

	// State-dependent tint.  Subtle variant skips the card entirely
	// and only reveals a semi-transparent overlay on hover/press.
	if (style == ATTouchButtonStyle::Subtle) {
		uint32 tint = held     ? A(pal.buttonActive)
		              : hovered ? A(pal.buttonHover)
		              : 0;
		if (tint)
			dl->AddRectFilled(rowMin, rowMax, tint, rounding);
		if (focused)
			dl->AddRect(rowMin, rowMax, A(pal.rowFocus),
				rounding, 0, dp(2.0f));
	} else {
		// State-dependent top/bottom colours.  Hover shifts toward the
		// lighter top colour, press shifts toward the darker bottom.
		uint32 topCol = c.top;
		uint32 botCol = c.mid;
		if (held) {
			topCol = c.mid;
			botCol = c.bot;
		} else if (hovered || focused) {
			topCol = c.top;
			botCol = c.mid;
		}

		// Gradient card.  ATMobileDrawGradientRect insets the gradient
		// horizontally by `rounding` so the rounded corners stay solid
		// bottom-colour instead of bleeding the rectangular gradient
		// silhouette into the outside-corner region.
		ATMobileDrawGradientRect(rowMin, rowMax, topCol, botCol, rounding);
		if (c.border) {
			dl->AddRect(rowMin, rowMax, c.border, rounding, 0, 1.0f);
		}
		if (focused) {
			dl->AddRect(rowMin, rowMax, A(pal.rowFocus),
				rounding, 0, dp(2.0f));
		}
	}

	// Centred label.  AddText draws the raw buffer including any
	// "##id" suffix — find the visible end explicitly so "Grid##view"
	// renders as "Grid" to match ImGui::Button's own behaviour.
	if (label && *label) {
		const char *labelEnd = RenderedLabelEnd(label);
		float tx = rowMin.x + (w - labelSize.x) * 0.5f;
		float ty = rowMin.y + (h - labelSize.y) * 0.5f;
		dl->PushClipRect(rowMin, rowMax, true);
		dl->AddText(nullptr, 0.0f, ImVec2(tx, ty),
			c.textCol, label, labelEnd);
		dl->PopClipRect();
	}

	return pressed;
}

// -------------------------------------------------------------------------
// ATTouchListItem
//
// Two-line card row (title + subtitle) with an optional right-side
// chevron.  Mirrors the Settings category cards so list-heavy screens
// (file browser, disk manager, save-state slots) look consistent.
// -------------------------------------------------------------------------

bool ATTouchListItem(const char *title, const char *subtitle,
	bool selected, bool chevron)
{
	UpdateContentScale();
	ImGuiWindow *window = ImGui::GetCurrentWindow();
	if (window->SkipItems) return false;

	const ATMobilePalette &pal = ATMobileGetPalette();

	const float rowH = subtitle && *subtitle ? dp(76.0f) : dp(56.0f);
	const float padLR = dp(16.0f);

	ImVec2 cursor = window->DC.CursorPos;
	float availW = ImGui::GetContentRegionAvail().x;
	ImVec2 rowMin(cursor.x, cursor.y);
	ImVec2 rowMax(cursor.x + availW, cursor.y + rowH);

	ImGuiID id = window->GetID(title ? title : "##item");
	ImRect bb(rowMin, rowMax);
	ImGui::ItemSize(bb, 0.0f);
	if (!ImGui::ItemAdd(bb, id))
		return false;

	bool hovered, held;
	bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
	if (pressed && ATTouchIsDraggingBeyondSlop())
		pressed = false;
	bool focused = ImGui::IsItemFocused();

	ImDrawList *dl = window->DrawList;
	float rounding = dp(10.0f);

	// Apply style.Alpha (halved under BeginDisabled) to every colour.
	auto A = [](uint32 col) { return (uint32)ImGui::GetColorU32(col); };

	uint32 bgTop, bgBot, textCol, subCol;
	if (selected) {
		bgTop   = pal.accentTop;
		bgBot   = pal.accent;
		textCol = pal.textOnAccent;
		subCol  = pal.textOnAccent;
	} else if (hovered || held || focused) {
		bgTop   = pal.cardBgHoverTop;
		bgBot   = pal.cardBgHover;
		textCol = pal.text;
		subCol  = pal.textMuted;
	} else {
		bgTop   = pal.cardBgTop;
		bgBot   = pal.cardBg;
		textCol = pal.text;
		subCol  = pal.textMuted;
	}
	bgTop = A(bgTop); bgBot = A(bgBot);
	textCol = A(textCol); subCol = A(subCol);

	// Gradient card — uses ATMobileDrawGradientRect which insets the
	// gradient horizontally by `rounding` so the rounded corners stay
	// solid bottom-colour instead of leaking the axis-aligned gradient
	// rectangle into the outside-corner region.
	ATMobileDrawGradientRect(rowMin, rowMax, bgTop, bgBot, rounding);
	dl->AddRect(rowMin, rowMax, A(pal.cardBorder), rounding, 0, 1.0f);
	if (focused) {
		dl->AddRect(rowMin, rowMax, A(pal.rowFocus),
			rounding, 0, dp(2.0f));
	}

	// Title — strip "##id" suffix so "[DIR] foo##42" renders as
	// "[DIR] foo".  Matches ImGui::Text behaviour.
	if (title && *title) {
		const char *titleEnd = RenderedLabelEnd(title);
		float tx = rowMin.x + padLR;
		float ty = (subtitle && *subtitle)
			? rowMin.y + dp(12.0f)
			: rowMin.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
		float rightEdge = rowMax.x - padLR - (chevron ? dp(20.0f) : 0.0f);
		dl->PushClipRect(ImVec2(tx, rowMin.y),
			ImVec2(rightEdge, rowMax.y), true);
		dl->AddText(nullptr, 0.0f, ImVec2(tx, ty),
			textCol, title, titleEnd);
		dl->PopClipRect();
	}
	// Subtitle
	if (subtitle && *subtitle) {
		float tx = rowMin.x + padLR;
		float ty = rowMin.y + dp(44.0f);
		float rightEdge = rowMax.x - padLR - (chevron ? dp(20.0f) : 0.0f);
		const char *subEnd = RenderedLabelEnd(subtitle);
		dl->PushClipRect(ImVec2(tx, rowMin.y),
			ImVec2(rightEdge, rowMax.y), true);
		dl->AddText(nullptr, 0.0f, ImVec2(tx, ty),
			subCol, subtitle, subEnd);
		dl->PopClipRect();
	}
	// Chevron
	if (chevron) {
		ImVec2 chev(rowMax.x - dp(24.0f),
			rowMin.y + rowH * 0.5f - dp(8.0f));
		dl->AddText(chev, subCol, ">");
	}

	return pressed;
}

void ATTouchMutedText(const char *text) {
	const ATMobilePalette &pal = ATMobileGetPalette();
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(
		((pal.textMuted >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f,
		((pal.textMuted >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f,
		((pal.textMuted >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f,
		((pal.textMuted >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f));
	ImGui::TextWrapped("%s", text);
	ImGui::PopStyleColor();
}

void ATTouchSection(const char *label) {
	UpdateContentScale();
	const ATMobilePalette &pal = ATMobileGetPalette();
	ImGui::Dummy(ImVec2(0, dp(16.0f)));
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(
		((pal.textSection >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f,
		((pal.textSection >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f,
		((pal.textSection >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f,
		1.0f));
	ImGui::TextUnformatted(label);
	ImGui::PopStyleColor();
	ImGui::Separator();
	ImGui::Dummy(ImVec2(0, dp(4.0f)));
}
