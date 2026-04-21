//	AltirraSDL - Online Play reusable widgets (impl)

#include <stdafx.h>

#include "ui_netplay_widgets.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <SDL3/SDL.h>

#include "input/touch_widgets.h"
#include "ui/core/ui_mode.h"
#include "ui_netplay_state.h"
#include "../mobile/mobile_internal.h"
#include "../gamelibrary/game_library.h"
#include "../gamelibrary/game_library_art.h"

#include <vd2/system/text.h>

#include <cctype>
#include <cwctype>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <unordered_map>

namespace ATNetplayUI {

namespace {

// -------------------------------------------------------------------
// Scale + safe-area state
// -------------------------------------------------------------------
// Set by the mobile render path each frame; zero on desktop (the main
// viewport rect doubles as the safe area there).
int g_insetTop    = 0;
int g_insetBottom = 0;
int g_insetLeft   = 0;
int g_insetRight  = 0;

float CurrentScale() {
	ImGuiIO &io = ImGui::GetIO();
	float fs = io.Fonts->Fonts.Size > 0
		? io.Fonts->Fonts[0]->FontSize
		: 13.0f;
	float s = (fs / 16.0f) * io.FontGlobalScale;
	if (s < 0.5f) s = 0.5f;
	if (s > 5.0f) s = 5.0f;
	return s;
}

// -------------------------------------------------------------------
// Focus-request bookkeeping — per-widget one-shot focus steer.
// -------------------------------------------------------------------
std::unordered_map<int, bool> g_pendingFocus;

// BeginSheet tracks whether the outer container is a modal popup
// (desktop) or a full-window overlay (mobile).  Stored so EndSheet
// pops the right thing.
enum class SheetKind { None, DesktopModal, DesktopWindow, MobileSheet };
SheetKind g_sheetKind = SheetKind::None;

// BeginScreenGrid likewise tracks cleanup state.
bool   g_inGrid         = false;
int    g_gridColumns    = 1;
float  g_gridGap        = 0.0f;
ImVec2 g_gridTileSize   = ImVec2(0, 0);
int    g_gridItemIdx    = 0;

} // anonymous

// ---------------------------------------------------------------------
// Scale / safe-area
// ---------------------------------------------------------------------

float Dp(float d) { return d * CurrentScale(); }

ImVec2 GetSafeOrigin() {
	const ImGuiViewport *vp = ImGui::GetMainViewport();
	return ImVec2(vp->WorkPos.x + (float)g_insetLeft,
	              vp->WorkPos.y + (float)g_insetTop);
}

ImVec2 GetSafeSize() {
	const ImGuiViewport *vp = ImGui::GetMainViewport();
	return ImVec2(vp->WorkSize.x - (float)(g_insetLeft + g_insetRight),
	              vp->WorkSize.y - (float)(g_insetTop  + g_insetBottom));
}

// Exported so the mobile render path can update each frame.
void ATNetplayUI_SetSafeAreaInsets(int top, int bottom, int left, int right) {
	g_insetTop    = top;
	g_insetBottom = bottom;
	g_insetLeft   = left;
	g_insetRight  = right;
}

// ---------------------------------------------------------------------
// Focus steering
// ---------------------------------------------------------------------

void FocusOnceNextFrame(int tag) { g_pendingFocus[tag] = true; }

bool ConsumeFocusRequest(int tag) {
	auto it = g_pendingFocus.find(tag);
	if (it == g_pendingFocus.end() || !it->second) return false;
	it->second = false;
	return true;
}

// ---------------------------------------------------------------------
// BeginSheet — mode-appropriate container.
// ---------------------------------------------------------------------

bool BeginSheet(const char *title, bool *open,
                const ImVec2 &minSize, const ImVec2 &maxSize) {
	const bool gaming = ATUIIsGamingMode();
	const ImVec2 safeO = GetSafeOrigin();
	const ImVec2 safeS = GetSafeSize();

	// Pick a size that respects both the caller's min/max and the
	// available safe area, with comfortable margin.
	ImVec2 size = minSize;
	if (gaming) {
		// Full-bleed card: inset by dp(16) from safe area.
		float margin = Dp(16.0f);
		size = ImVec2(
			std::max(minSize.x, safeS.x - 2 * margin),
			std::max(minSize.y, safeS.y - 2 * margin));
		size.x = std::min(size.x, maxSize.x);
		size.y = std::min(size.y, maxSize.y);

		ImVec2 pos = ImVec2(
			safeO.x + (safeS.x - size.x) * 0.5f,
			safeO.y + (safeS.y - size.y) * 0.5f);

		// Dim backdrop behind the sheet.
		const ImU32 dim = IM_COL32(0, 0, 0, 160);
		ImGui::GetBackgroundDrawList()->AddRectFilled(
			safeO, ImVec2(safeO.x + safeS.x, safeO.y + safeS.y), dim);

		ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
		ImGui::SetNextWindowSize(size, ImGuiCond_Always);
		const ImGuiWindowFlags flags =
			ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoBringToFrontOnFocus |
			ImGuiWindowFlags_NoDocking;
		bool opened = ImGui::Begin(title, open, flags);
		if (opened) g_sheetKind = SheetKind::MobileSheet;
		else { ImGui::End(); g_sheetKind = SheetKind::None; }
		return opened;
	}

	// Desktop: centered modal with caller's min/max.
	ImVec2 sz = minSize;
	if (maxSize.x > 0 && sz.x > maxSize.x) sz.x = maxSize.x;
	if (maxSize.y > 0 && sz.y > maxSize.y) sz.y = maxSize.y;
	ImGui::SetNextWindowSize(sz, ImGuiCond_Appearing);
	ImGui::SetNextWindowSizeConstraints(
		minSize,
		ImVec2(maxSize.x > 0 ? maxSize.x : FLT_MAX,
		       maxSize.y > 0 ? maxSize.y : FLT_MAX));
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings;
	bool opened = ImGui::Begin(title, open, flags);
	g_sheetKind = opened ? SheetKind::DesktopWindow : SheetKind::None;
	if (!opened) ImGui::End();
	return opened;
}

void EndSheet() {
	if (g_sheetKind == SheetKind::None) return;
	ImGui::End();
	g_sheetKind = SheetKind::None;
}

// ---------------------------------------------------------------------
// Screen grid — respects safe area (mobile) / available content (desktop).
// ---------------------------------------------------------------------

ImVec2 BeginScreenGrid(int columns, float minTileWidthPx, float tileAspect) {
	g_inGrid      = true;
	g_gridItemIdx = 0;
	g_gridGap     = Dp(10.0f);
	if (columns < 1) columns = 1;

	// Available width = content region width minus right scrollbar etc.
	ImVec2 avail = ImGui::GetContentRegionAvail();
	// Recompute columns to fit minTileWidth.
	int maxCols = (int)std::floor(
		(avail.x + g_gridGap) / (minTileWidthPx + g_gridGap));
	if (maxCols < 1) maxCols = 1;
	if (maxCols < columns) columns = maxCols;

	g_gridColumns = columns;
	float tileW = (avail.x - g_gridGap * (columns - 1)) / columns;
	float tileH = tileW * (tileAspect > 0 ? tileAspect : 0.75f);
	g_gridTileSize = ImVec2(tileW, tileH);
	return g_gridTileSize;
}

void EndScreenGrid() {
	g_inGrid = false;
	g_gridColumns  = 1;
	g_gridItemIdx  = 0;
	g_gridTileSize = ImVec2(0, 0);
}

// Advance the grid cursor by one cell.  Callers invoke this after each
// tile to line up the next one on the same row (or wrap).
static void GridStep() {
	int col = (g_gridItemIdx + 1) % g_gridColumns;
	if (col == 0) {
		// Wrapping to the next row — no SameLine.
	} else {
		ImGui::SameLine(0.0f, g_gridGap);
	}
	++g_gridItemIdx;
}

// ---------------------------------------------------------------------
// SessionTile — card with art thumbnail + title + subtitle + chips.
// ---------------------------------------------------------------------

bool SessionTile(const TileInfo &info, const ImVec2 &size) {
	const ATMobilePalette &p = ATMobileGetPalette();
	ImDrawList *dl = ImGui::GetWindowDrawList();
	ImVec2 pos = ImGui::GetCursorScreenPos();

	char idBuf[64];
	std::snprintf(idBuf, sizeof idBuf, "##tile_%d_%p",
		g_gridItemIdx, (void*)info.title);

	// InvisibleButton gives us hover/active state + keyboard focus.
	ImGui::PushID(idBuf);
	bool clicked = ImGui::InvisibleButton("tile_btn", size);
	const bool hovered = ImGui::IsItemHovered();
	const bool focused = ImGui::IsItemFocused();

	ImVec2 rmin = pos;
	ImVec2 rmax = ImVec2(pos.x + size.x, pos.y + size.y);

	// Background.
	uint32_t bg = hovered ? p.cardBgHover
	             : (info.isSelected ? p.cardBgHover : p.cardBg);
	uint32_t bgTop = hovered ? p.cardBgHoverTop
	                : (info.isSelected ? p.cardBgHoverTop : p.cardBgTop);
	ATMobileDrawGradientRect(rmin, rmax, bgTop, bg, Dp(10.0f));

	// Border (thicker for selected / focused).
	uint32_t border = (info.isSelected || focused) ? p.accent : p.cardBorder;
	dl->AddRect(rmin, rmax, border, Dp(10.0f), 0,
		(info.isSelected || focused) ? Dp(2.0f) : 1.0f);

	// Art area on top — 60% of tile height.
	float artH = size.y * 0.60f;
	ImVec2 artMin = ImVec2(rmin.x + Dp(8), rmin.y + Dp(8));
	ImVec2 artMax = ImVec2(rmax.x - Dp(8), rmin.y + artH);
	dl->AddRectFilled(artMin, artMax, p.segBgInactive, Dp(6.0f));
	if (info.artTexId != 0) {
		// Fit inside while preserving aspect.
		float aw = info.artSize.x > 0 ? info.artSize.x : 1.0f;
		float ah = info.artSize.y > 0 ? info.artSize.y : 1.0f;
		float boxW = artMax.x - artMin.x;
		float boxH = artMax.y - artMin.y;
		float scale = std::min(boxW / aw, boxH / ah);
		float drawW = aw * scale;
		float drawH = ah * scale;
		ImVec2 aMin = ImVec2(
			artMin.x + (boxW - drawW) * 0.5f,
			artMin.y + (boxH - drawH) * 0.5f);
		ImVec2 aMax = ImVec2(aMin.x + drawW, aMin.y + drawH);
		dl->AddImage((ImTextureID)info.artTexId, aMin, aMax);
	} else {
		// Placeholder: centered "?" glyph.
		const char *glyph = "?";
		ImVec2 sz = ImGui::CalcTextSize(glyph);
		dl->AddText(
			ImVec2((artMin.x + artMax.x - sz.x) * 0.5f,
			       (artMin.y + artMax.y - sz.y) * 0.5f),
			p.textMuted, glyph);
	}

	// Padlock badge in top-right corner of art.
	if (info.isPrivate) {
		float sz = Dp(22.0f);
		ImVec2 bMin = ImVec2(artMax.x - sz - Dp(4), artMin.y + Dp(4));
		ImVec2 bMax = ImVec2(artMax.x - Dp(4), artMin.y + Dp(4) + sz);
		dl->AddRectFilled(bMin, bMax, p.warning, Dp(4));
		const char *glyph = "P";  // short stand-in; icon font not guaranteed
		ImVec2 glyphSz = ImGui::CalcTextSize(glyph);
		dl->AddText(
			ImVec2((bMin.x + bMax.x - glyphSz.x) * 0.5f,
			       (bMin.y + bMax.y - glyphSz.y) * 0.5f),
			p.textOnAccent, glyph);
	}

	// Bottom text area.
	ImVec2 txt = ImVec2(rmin.x + Dp(10), rmin.y + artH + Dp(4));
	dl->AddText(txt, p.text, info.title);
	if (info.subtitle) {
		txt.y += ImGui::GetTextLineHeightWithSpacing();
		dl->AddText(txt, p.textMuted, info.subtitle);
	}
	// Region line — rendered below the handle when the host published
	// one.  "global" / "eu" / "us" etc.  Matches Desktop's Region
	// column.
	if (info.region && *info.region) {
		txt.y += ImGui::GetTextLineHeightWithSpacing();
		char buf[64];
		std::snprintf(buf, sizeof buf, "Region: %s", info.region);
		dl->AddText(txt, p.textMuted, buf);
	}
	// Players chip — drawn at the bottom-right of the card.
	if (info.maxPlayers > 0) {
		char chip[32];
		std::snprintf(chip, sizeof chip, "%u/%u",
			info.playerCount, info.maxPlayers);
		ImVec2 cSz = ImGui::CalcTextSize(chip);
		float pad = Dp(6);
		ImVec2 cMin = ImVec2(rmax.x - cSz.x - pad * 2 - Dp(8),
		                     rmax.y - cSz.y - pad * 2 - Dp(6));
		ImVec2 cMax = ImVec2(rmax.x - Dp(8), rmax.y - Dp(6));
		dl->AddRectFilled(cMin, cMax, p.segBgInactive, Dp(4));
		dl->AddText(ImVec2(cMin.x + pad, cMin.y + pad), p.textMuted, chip);
	}

	ImGui::PopID();
	GridStep();
	return clicked;
}

// ---------------------------------------------------------------------
// StatusBadge — small coloured pill with optional spinner dots.
// ---------------------------------------------------------------------

void StatusBadge(const char *label, int severity, bool showSpinner) {
	const ATMobilePalette &p = ATMobileGetPalette();
	uint32_t fg = p.textOnAccent;
	uint32_t bg = p.accent;
	switch (severity) {
		case 1: bg = p.warning; break;
		case 2: bg = p.danger;  break;
		case 3: bg = p.success; break;
		default: break;
	}
	ImDrawList *dl = ImGui::GetWindowDrawList();
	const float padX = Dp(10), padY = Dp(6);
	ImVec2 textSz = ImGui::CalcTextSize(label);
	float dotsW = showSpinner ? Dp(22.0f) : 0.0f;
	ImVec2 cur = ImGui::GetCursorScreenPos();
	ImVec2 bmin = cur;
	ImVec2 bmax = ImVec2(cur.x + textSz.x + padX * 2 + dotsW,
	                     cur.y + textSz.y + padY * 2);
	dl->AddRectFilled(bmin, bmax, bg, Dp(12.0f));
	dl->AddText(ImVec2(bmin.x + padX, bmin.y + padY), fg, label);

	if (showSpinner) {
		float t = (float)ImGui::GetTime();
		float cx = bmax.x - padX - Dp(2);
		float cy = (bmin.y + bmax.y) * 0.5f;
		for (int i = 0; i < 3; ++i) {
			float phase = t * 3.0f - i * 0.45f;
			float a = 0.35f + 0.65f * (0.5f + 0.5f * std::sin(phase));
			uint32_t col = (fg & 0x00FFFFFFu) | ((unsigned)(a * 255) << 24);
			dl->AddCircleFilled(ImVec2(cx - i * Dp(6), cy), Dp(2.5f), col);
		}
	}

	ImGui::Dummy(ImVec2(bmax.x - bmin.x, bmax.y - bmin.y));
}

// ---------------------------------------------------------------------
// PeerChip — inline peer handle with optional region + padlock marker.
// ---------------------------------------------------------------------

// ---------------------------------------------------------------------
// LobbyStatusBanner — honest reachability header for Gaming Mode.
// Green when the most recent lobby poll succeeded, red on failure,
// neutral grey before the first response.  Mirrors Desktop's
// LobbyStatusIndicator so the two modes tell the same story.
// ---------------------------------------------------------------------
void LobbyStatusBanner(bool allowRetry) {
	const ATMobilePalette &p = ATMobileGetPalette();
	const LobbyHealth& h = GetState().lobbyHealth;
	const uint64_t nowMs = SDL_GetTicks();

	const bool haveOk   = (h.lastOkMs   != 0);
	const bool haveFail = (h.lastFailMs != 0);
	const bool okIsNewer = haveOk && (!haveFail || h.lastOkMs >= h.lastFailMs);

	uint32_t bg   = p.segBgInactive;
	uint32_t text = p.text;
	char line[192];

	if (okIsNewer) {
		bg   = p.success;
		text = p.textOnAccent;
		uint64_t age = (nowMs >= h.lastOkMs) ? nowMs - h.lastOkMs : 0;
		uint64_t sec = age / 1000;
		std::snprintf(line, sizeof line,
			"[OK]  Lobby reachable — listed  (checked %llus ago)",
			(unsigned long long)sec);
	} else if (haveFail) {
		bg   = p.danger;
		text = p.textOnAccent;
		const char *reason = h.lastError.empty()
			? "unreachable" : h.lastError.c_str();
		std::snprintf(line, sizeof line,
			"[!!]  Lobby unreachable — %s", reason);
	} else {
		bg   = p.segBgInactive;
		text = p.textMuted;
		std::snprintf(line, sizeof line, "[..]  Lobby: checking...");
	}

	// Pill background spanning the full content width.
	ImDrawList *dl = ImGui::GetWindowDrawList();
	ImVec2 cur = ImGui::GetCursorScreenPos();
	float w = ImGui::GetContentRegionAvail().x;
	float padX = Dp(14), padY = Dp(8);
	float lineH = ImGui::GetTextLineHeight() + padY * 2;
	ImVec2 bmin = cur;
	ImVec2 bmax = ImVec2(cur.x + w, cur.y + lineH);
	dl->AddRectFilled(bmin, bmax, bg, Dp(8.0f));
	dl->AddText(ImVec2(bmin.x + padX, bmin.y + padY), text, line);
	ImGui::Dummy(ImVec2(w, lineH));

	if (allowRetry && !okIsNewer && haveFail) {
		// Offer a manual retry beneath the banner when the last poll
		// failed.  The worker's backoff logic will still throttle, but
		// users appreciate an explicit "try again" affordance.
		if (ATTouchButton("Retry", ImVec2(Dp(160), Dp(40)),
		                  ATTouchButtonStyle::Neutral)) {
			State& st = GetState();
			st.browser.refreshRequested = true;
			st.browser.nextRetryMs = 0;
		}
	}
	ImGui::Spacing();
}

// ---------------------------------------------------------------------
// Game-art lookup — match by basename against the Game Library index.
// ---------------------------------------------------------------------
namespace {
// Lowercase, strip trailing extension, and strip any trailing
// parenthesised tag (like Game Library's ExtractCanonicalName does)
// so "World Karate Championship (v1,128).xex" reduces to
// "world karate championship".
std::wstring CanonicalBasename(const wchar_t *name) {
	std::wstring s = name ? name : L"";
	// Strip last extension.
	size_t dot = s.find_last_of(L'.');
	if (dot != std::wstring::npos) s.erase(dot);
	// Strip trailing " (...)" groups.  Game Library applies the same
	// rule when building canonical names, so the two keys line up.
	while (!s.empty() && s.back() == L' ') s.pop_back();
	while (!s.empty() && s.back() == L')') {
		size_t op = s.find_last_of(L'(');
		if (op == std::wstring::npos) break;
		// Refuse to strip if '(' is at the very start — that isn't a
		// disambiguation tag.
		if (op == 0) break;
		s.erase(op);
		while (!s.empty() && s.back() == L' ') s.pop_back();
	}
	for (auto& c : s) c = (wchar_t)std::towlower(c);
	return s;
}
} // anonymous

uintptr_t LookupArtByGameName(const char *gameName, int *outW, int *outH) {
	if (outW) *outW = 0;
	if (outH) *outH = 0;
	if (!gameName || !*gameName) return 0;

	ATGameLibrary *lib = GetGameLibrary();
	GameArtCache *cache = GetGameArtCache();
	if (!lib || !cache) return 0;

	VDStringW wname = VDTextU8ToW(gameName, -1);
	std::wstring key = CanonicalBasename(wname.c_str());
	if (key.empty()) return 0;

	const auto& entries = lib->GetEntries();
	for (const auto& e : entries) {
		if (e.mArtPath.empty()) continue;
		// First try the precomputed canonical name.  When that's empty
		// (older cache entries), fall back to the display name + any
		// variant basename so we don't miss legitimate matches.
		std::wstring cand = CanonicalBasename(e.mCanonicalName.c_str());
		if (cand == key) {
			return (uintptr_t)cache->GetTexture(e.mArtPath, outW, outH);
		}
		cand = CanonicalBasename(e.mDisplayName.c_str());
		if (cand == key) {
			return (uintptr_t)cache->GetTexture(e.mArtPath, outW, outH);
		}
		for (const auto& v : e.mVariants) {
			const wchar_t *p = v.mPath.c_str();
			const wchar_t *slash = nullptr;
			for (const wchar_t *q = p; *q; ++q)
				if (*q == L'/' || *q == L'\\') slash = q;
			const wchar_t *base = slash ? slash + 1 : p;
			if (CanonicalBasename(base) == key) {
				return (uintptr_t)cache->GetTexture(e.mArtPath, outW, outH);
			}
		}
	}
	return 0;
}

void PumpArtCache() {
	if (GameArtCache *c = GetGameArtCache()) c->ProcessPending();
}

void PeerChip(const char *handle, const char *region, bool isPrivate) {
	const ATMobilePalette &p = ATMobileGetPalette();
	ImGui::PushStyleColor(ImGuiCol_Text, ATMobileCol(p.text));
	ImGui::TextUnformatted(handle && *handle ? handle : "Anonymous");
	ImGui::PopStyleColor();
	if (region && *region) {
		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_Text, ATMobileCol(p.textMuted));
		ImGui::Text("· %s", region);
		ImGui::PopStyleColor();
	}
	if (isPrivate) {
		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_Text, ATMobileCol(p.warning));
		ImGui::TextUnformatted("[private]");
		ImGui::PopStyleColor();
	}
}

} // namespace ATNetplayUI
