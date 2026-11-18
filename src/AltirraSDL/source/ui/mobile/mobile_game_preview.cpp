//	AltirraSDL - Gaming Mode: docked game preview panel
//
//	The panel that makes downloaded metadata pay off while browsing: it
//	shows whatever game is currently highlighted, without the user having
//	to open anything.  Modelled on ES-DE's metadata pane, adapted to the
//	fact that this frontend has to serve four input devices at once.
//
//	How a game becomes "highlighted" is the browser's job (see
//	mobile_game_browser.cpp), but the contract matters here:
//
//	  gamepad / keyboard  D-pad focus moves      -> panel follows, free
//	  mouse               hover (after a move)   -> panel follows, free
//	  touch               long-press             -> panel follows
//
//	so on every input device the panel updates as a side effect of what
//	the user was already doing.  A tap / A press still boots the game
//	immediately; the panel never adds a step to launching.
//
//	Two deliberate constraints:
//
//	1. NoNav.  The panel never takes D-pad focus.  If it did, pressing
//	   Right on the grid's last column would drop into the Play button
//	   and the grid's spatial navigation would stop being predictable.
//	   Gamepad users get the panel's actions — and everything else — by
//	   pressing Y, which opens the full-screen details sheet.
//
//	2. No launch-blocking chrome.  The panel offers exactly two buttons
//	   (Play, Details).  Everything rarer lives in the details sheet, so
//	   the panel stays readable at a glance rather than becoming a
//	   second toolbar.

#include <stdafx.h>

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cstdio>

#include <vd2/system/text.h>

#include "ui_mobile.h"
#include "mobile_internal.h"
#include "simulator.h"
#include "touch_widgets.h"
#include "altirra_icons.h"
#include "ui/gamelibrary/game_library.h"
#include "ui/gamelibrary/game_library_art.h"
#include "ui/dialogs/ui_game_metadata.h"
#include "media/metadata_settings.h"
#include "media/metadata_scraper.h"

extern ATGameLibrary *GetGameLibrary();
extern GameArtCache *GetGameArtCache();

namespace {

// Below this the bottom dock switches to its compact form: a thumbnail,
// the title, one fact line and the two actions.  A bottom dock lays art
// beside text, so on a ~430dp phone the full form would leave a ~70dp
// synopsis column — unreadable.  Dropping what does not fit beats
// refusing to show anything, which is what the old 600dp *floor* did.
const float kCompactBodyW = 560.0f;
const float kCompactBodyH = 200.0f;

// Ceiling on the bottom dock's artwork as a fraction of the panel width,
// so the text column always keeps the majority of the row.
const float kBottomArtWidthFrac        = 0.38f;
const float kBottomArtWidthFracCompact = 0.30f;

// Fit inside the artwork box while keeping the original aspect ratio.
ImVec2 FitInside(int w, int h, float maxW, float maxH) {
	if (w <= 0 || h <= 0)
		return ImVec2(0, 0);
	float scale = maxW / (float)w;
	const float scaleH = maxH / (float)h;
	if (scaleH < scale)
		scale = scaleH;
	return ImVec2((float)w * scale, (float)h * scale);
}

// Artwork letterboxed into a fixed maxW x maxH box.
//
// Fixed rather than shrink-to-fit for the same reason as the Desktop
// pane: with a controller the user walks through games one press at a
// time, and box art / screenshots / logos all have different shapes.
// Sizing the box to each image makes the title and synopsis underneath
// hop up and down on every single press, which reads as the panel
// glitching rather than updating.
void DrawArt(ATGameLibrary &lib, const GameEntry &entry,
	float maxW, float maxH)
{
	GameArtCache *cache = GetGameArtCache();
	const VDStringW art = cache ? lib.GetTileArtPath(entry) : VDStringW();

	int w = 0, h = 0;
	ImTextureID tex = (art.empty() || !cache)
		? (ImTextureID)0 : cache->GetTexture(art, &w, &h);

	const ImVec2 tl = ImGui::GetCursorScreenPos();
	const ImVec2 br(tl.x + maxW, tl.y + maxH);
	ImDrawList *dl = ImGui::GetWindowDrawList();

	if (tex && w > 0 && h > 0) {
		dl->AddRectFilled(tl, br, IM_COL32(18, 18, 22, 255), dp(6.0f));
		const ImVec2 size = FitInside(w, h, maxW, maxH);
		const ImVec2 imgTL(tl.x + (maxW - size.x) * 0.5f,
			tl.y + (maxH - size.y) * 0.5f);
		dl->AddImage(tex, imgTL,
			ImVec2(imgTL.x + size.x, imgTL.y + size.y));
	} else {
		const ATMobilePalette &pal = ATMobileGetPalette();
		dl->AddRectFilled(tl, br, pal.cardBgHover, dp(6.0f));

		// "Nothing to show" and "not here yet" should not look alike.
		const char *badge = art.empty() ? "NO ART" : "\xE2\x80\xA6";
		const ImVec2 ts = ImGui::CalcTextSize(badge);
		dl->AddText(ImVec2(tl.x + (maxW - ts.x) * 0.5f,
			tl.y + (maxH - ts.y) * 0.5f), pal.textMuted, badge);
	}

	ImGui::Dummy(ImVec2(maxW, maxH));
}

}  // namespace

void ATMobileDrawArtSwitch(const GameEntry &entry, float width) {
	const ATMobilePalette &pal = ATMobileGetPalette();
	const float h = dp(40.0f);
	const float rowX = ImGui::GetCursorPosX();
	const ImVec2 rowTL = ImGui::GetCursorScreenPos();

	// Auto-width, not an explicit one: ATTouchButton silently drops the
	// icon when an explicit width cannot fit icon + padding, which for a
	// label-less button would leave a blank rectangle.
	if (ATTouchButton("##artprev", ImVec2(0, h),
		ATTouchButtonStyle::Subtle, ICON_MD_CHEVRON_LEFT))
	{
		ATUIMetadataCycleArtSlot(-1);
	}
	const float arrowW = ImGui::GetItemRectSize().x;

	ImGui::SameLine(0, 0);
	ImGui::SetCursorPosX(rowX + width - arrowW);
	if (ATTouchButton("##artnext", ImVec2(0, h),
		ATTouchButtonStyle::Subtle, ICON_MD_CHEVRON_RIGHT))
	{
		ATUIMetadataCycleArtSlot(1);
	}

	// The caption is drawn rather than laid out: it has to sit centred
	// between two buttons whose width is only known after the first one
	// is submitted, and it must clip rather than wrap or push them apart.
	const VDStringA caption = ATUIMetadataArtCaption(entry);
	const ImVec2 cs = ImGui::CalcTextSize(caption.c_str());
	ImDrawList *dl = ImGui::GetWindowDrawList();
	dl->PushClipRect(ImVec2(rowTL.x + arrowW, rowTL.y),
		ImVec2(rowTL.x + width - arrowW, rowTL.y + h), true);
	dl->AddText(ImVec2(rowTL.x + (width - cs.x) * 0.5f,
		rowTL.y + (h - cs.y) * 0.5f), pal.textMuted, caption.c_str());
	dl->PopClipRect();
}

namespace {

// Title + fact lines + synopsis.  Shared by both docks; only the width
// it is handed differs.
void DrawText(const GameEntry &entry, const ATMobilePalette &pal,
	bool includeSynopsis, bool compact = false)
{
	const VDStringA title = ATUIMetadataDisplayTitle(entry);
	ImGui::PushTextWrapPos(0.0f);
	ImGui::TextColored(ATMobileCol(pal.textTitle), "%s", title.c_str());
	ImGui::PopTextWrapPos();

	const VDStringA facts = ATUIMetadataFactsLine(entry);
	if (!facts.empty())
		ATTouchMutedText(facts.c_str());

	// Compact keeps only the line that identifies the game.  Genre,
	// variant counts and play counts are the first things to go when
	// there are three lines of room: they are the least of what someone
	// glancing at a phone in portrait is after.
	if (!compact) {
		const VDStringA genre = ATUIMetadataGenreLine(entry);
		if (!genre.empty())
			ATTouchMutedText(genre.c_str());

		const VDStringA copyLine = ATUIMetadataCopyLine(entry);
		if (!copyLine.empty())
			ATTouchMutedText(copyLine.c_str());
	}

	if (!includeSynopsis)
		return;

	ImGui::Spacing();

	if (!entry.mMeta.mDescription.empty()) {
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextUnformatted(
			VDTextWToU8(entry.mMeta.mDescription).c_str());
		ImGui::PopTextWrapPos();
	} else {
		const char *reason = ATUIMetadataEmptyReason(entry);
		if (*reason)
			ATTouchMutedText(reason);
	}
}

// The two-button footer, identical in both docks.  Returns nothing —
// both actions are terminal for this frame's panel.
void DrawActions(ATSimulator &sim, ATMobileUIState &mobileState,
	int entryIndex, const GameEntry &entry, float availW)
{
	const float btnH = dp(ATTouch::kButtonHeightNormal);
	const float gap = dp(8.0f);
	const float halfW = (availW - gap) * 0.5f;

	if (ATTouchButton("Play##preview", ImVec2(halfW, btnH),
		ATTouchButtonStyle::Accent, ICON_MD_PLAY_ARROW))
	{
		if (entry.mVariants.size() > 1)
			GameBrowser_ShowVariantPickerForBoot(entryIndex);
		else if (!entry.mVariants.empty())
			GameBrowser_LaunchEntry(sim, mobileState, (size_t)entryIndex, 0);
	}

	ImGui::SameLine(0, gap);

	// The second button offers whatever is actually useful for this game.
	//
	// "Details" on an entry with no metadata is a promise the sheet
	// cannot keep — there is nothing in there but the file name and a
	// line saying nothing has been downloaded.  So when there is nothing
	// to show, offer the thing that would create something to show.
	//
	// Once a lookup has been attempted the sheet becomes worth opening
	// even if it failed: it explains *why* (not in the database, or the
	// server could not be reached) and offers a retry.
	VDStringA whyNot;
	const bool nothingToShow =
		(entry.mMeta.mStatus == GameMetaStatus::None);
	const bool canFetch = ATUIMetadataCanDownloadNow(whyNot);
	ATMetadataScraper& scraper = ATMetadataGetScraper();

	if (scraper.IsRunning()) {
		// A run is in flight.  Say so instead of offering a second one.
		ImGui::BeginDisabled(true);
		ATTouchButton("Working\xE2\x80\xA6##preview", ImVec2(halfW, btnH),
			ATTouchButtonStyle::Neutral, ICON_MD_CLOUD_DOWNLOAD);
		ImGui::EndDisabled();
		return;
	}

	// If fetching is impossible (no HTTPS backend, no credential), fall
	// back to Details rather than a dead disabled button: the sheet is
	// where the reason is spelled out, and on touch there is no tooltip
	// to carry it here.
	if (nothingToShow && canFetch) {
		if (ATTouchButton("Get Metadata##preview", ImVec2(halfW, btnH),
			ATTouchButtonStyle::Neutral, ICON_MD_CLOUD_DOWNLOAD))
		{
			ATGameLibrary *libp = GetGameLibrary();
			if (libp)
				ATUIMetadataStartDownloadForEntry(*libp, entryIndex);
		}
		return;
	}

	if (ATTouchButton("Details##preview", ImVec2(halfW, btnH),
		ATTouchButtonStyle::Neutral, ICON_MD_INFO))
	{
		GameDetails_Open(entryIndex);
		mobileState.currentScreen = ATMobileUIScreen::GameDetails;
	}
}

}  // namespace

ATGamePreviewDock GamePreview_ComputeDock(float width, float height) {
	if (width >= height && width >= dp(440.0f) && height >= dp(180.0f))
		return ATGamePreviewDock::Side;

	if (height > width && height >= dp(460.0f))
		return ATGamePreviewDock::Bottom;

	return ATGamePreviewDock::None;
}

void GamePreview_Render(ATSimulator &sim, ATMobileUIState &mobileState,
	int entryIndex, ATGamePreviewDock dock, const ImVec2 &size)
{
	if (dock == ATGamePreviewDock::None)
		return;

	ATGameLibrary *libp = GetGameLibrary();
	if (!libp)
		return;
	ATGameLibrary &lib = *libp;

	const ATMobilePalette &pal = ATMobileGetPalette();

	// NoNav: see the file header.  This is the single most important
	// flag in this file — without it the grid's D-pad navigation leaks
	// into the panel.
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ATMobileCol(pal.cardBg));
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, dp(10.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
		ImVec2(dp(12.0f), dp(12.0f)));
	ImGui::BeginChild("##gamePreview", size, ImGuiChildFlags_AlwaysUseWindowPadding,
		ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse);

	auto &entries = lib.GetEntries();
	if (entryIndex < 0 || (size_t)entryIndex >= entries.size()) {
		// Nothing highlighted yet — say so once, centred, rather than
		// leaving a blank card that reads as a bug.
		const char *msg = "Highlight a game to see its details";
		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const ImVec2 ts = ImGui::CalcTextSize(msg);
		ImGui::SetCursorPos(ImVec2(
			ImGui::GetCursorPosX() + (avail.x - ts.x) * 0.5f,
			ImGui::GetCursorPosY() + (avail.y - ts.y) * 0.5f));
		ATTouchMutedText(msg);
		ImGui::EndChild();
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor();
		return;
	}

	const GameEntry &entry = entries[entryIndex];
	static int previousEntry = -1;
	const bool entryChanged = previousEntry != entryIndex;
	previousEntry = entryIndex;
	const float footerH = dp(ATTouch::kButtonHeightNormal)
		+ ImGui::GetStyle().ItemSpacing.y;

	// Short panes keep the image beside the synopsis so neither area
	// collapses when the artwork switch and actions take a row each.
	if (ImGui::GetContentRegionAvail().y >= dp(225.0f))
	{
		// ── Vertical layout ──────────────────────────────────────
		// Art on top, text under it, actions pinned to the bottom.
		const float availW = ImGui::GetContentRegionAvail().x;
		const float availH = ImGui::GetContentRegionAvail().y;
		const float spacing = ImGui::GetStyle().ItemSpacing.y;
		const float lineH = ImGui::GetTextLineHeightWithSpacing();
		const float switchH = dp(40.0f) + spacing;
		const bool compact = availH < dp(400.0f);
		const float minTextH = lineH * (compact ? 4.0f : 6.0f);
		const float artH = std::max(1.0f, std::min(availH * 0.52f,
			availH - footerH - switchH - minTextH - spacing * 2.0f));

		DrawArt(lib, entry, availW, artH);
		ATMobileDrawArtSwitch(entry, availW);

		if (entryChanged)
			ImGui::SetNextWindowScroll(ImVec2(0, 0));
		ImGui::BeginChild("##previewText", ImVec2(0, -footerH),
			ImGuiChildFlags_None, ImGuiWindowFlags_NoNav);
		ATTouchDragScroll();
		DrawText(entry, pal, /*includeSynopsis=*/true, compact);
		ATTouchEndDragScroll();
		ImGui::EndChild();

	} else {
		// ── Horizontal layout ────────────────────────────────────
		// Use the pane's width for art and text when height is limited.
		const float availH = ImGui::GetContentRegionAvail().y;
		const float availW = ImGui::GetContentRegionAvail().x;

		// Compact: a phone held upright.  Secondary fact lines and the art
		// switch are dropped so that the artwork, title and actions stay
		// full size and finger-sized.  Keep the synopsis: the text column
		// is already a scroll view, and omitting it leaves otherwise useful
		// space blank between the title/facts and the actions.
		const bool compact = (availW < dp(kCompactBodyW))
			|| (availH < dp(kCompactBodyH));

		const float artH = availH - footerH;
		// Slightly wider than tall suits the mix of portrait covers and
		// landscape screenshots — but never at the text column's expense,
		// so the aspect-derived width is capped against the panel width.
		float artW = artH * 1.1f;
		const float artCap = availW * (compact
			? kBottomArtWidthFracCompact : kBottomArtWidthFrac);
		if (artW > artCap)
			artW = artCap;

		ImGui::BeginGroup();
		if (compact) {
			// No room for the switch here; L1/R1 and the full-screen
			// sheet still reach it.
			DrawArt(lib, entry, artW, artH);
		} else {
			// The switch shares the art column, so the art box gives up
			// its height rather than overflowing the panel.
			const float switchH = dp(40.0f) + ImGui::GetStyle().ItemSpacing.y;
			DrawArt(lib, entry, artW, artH - switchH);
			ATMobileDrawArtSwitch(entry, artW);
		}
		ImGui::EndGroup();

		ImGui::SameLine(0, dp(compact ? 10.0f : 14.0f));

		const float textW = ImGui::GetContentRegionAvail().x;
		if (entryChanged)
			ImGui::SetNextWindowScroll(ImVec2(0, 0));
		ImGui::BeginChild("##previewText", ImVec2(textW, artH),
			ImGuiChildFlags_None, ImGuiWindowFlags_NoNav);
		ATTouchDragScroll();
		DrawText(entry, pal, /*includeSynopsis=*/true, compact);
		ATTouchEndDragScroll();
		ImGui::EndChild();

	}

	DrawActions(sim, mobileState, entryIndex, entry,
		ImGui::GetContentRegionAvail().x);

	ImGui::EndChild();
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor();
}
