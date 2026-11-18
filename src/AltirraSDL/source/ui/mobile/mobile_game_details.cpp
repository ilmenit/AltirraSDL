//	AltirraSDL - Gaming Mode: game details sheet
//
//	Opened by long-press, Gamepad-Y, or the info badge on a tile.  A
//	plain tap still boots the game — that muscle memory is not worth
//	trading away for a details screen.
//
//	Navigation shape, deliberately the same as every other Gaming Mode
//	screen (netplay's BeginScreenBody / mobile_settings.cpp):
//
//	    ATTouchScreenHeader(...)                 <- owns Back / Escape / B
//	    BeginChild(..., ImGuiChildFlags_NavFlattened)
//	    ATTouchDragScroll()
//	        ...content...
//	    ATTouchEndDragScroll(); EndChild()
//	    ...footer buttons...
//
//	NavFlattened is the important part: it merges the scrolling child's
//	items into the parent's nav scope, so the D-pad walks header ->
//	content -> footer in one continuous run with no "enter the child"
//	press and no dead stop at the child boundary.  The Game Library
//	browser cannot do this (it needs PgUp/PgDown confined to the list and
//	uses a clipper), which is exactly why it has to hand-roll focus
//	transfer — see the two-frame Up-at-top detection in
//	mobile_game_browser.cpp.  A simple screen like this one should not
//	inherit that complexity.

#include <stdafx.h>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <cstdio>

#include <vd2/system/text.h>

#include "ui_mobile.h"
#include "mobile_internal.h"
#include "simulator.h"
#include "ui_main.h"
#include "touch_widgets.h"
#include "altirra_icons.h"
#include "ui/gamelibrary/game_library.h"
#include "ui/gamelibrary/game_library_art.h"
#include "ui/dialogs/ui_game_metadata.h"
#include "media/metadata_scraper.h"
#include "media/metadata_settings.h"

extern ATGameLibrary *GetGameLibrary();
extern GameArtCache *GetGameArtCache();
extern void GameBrowser_Invalidate();
extern ATMobileUIState g_mobileState;

namespace {

// Index of the entry the sheet is showing.  -1 closes it.
int  s_detailsEntry = -1;
// One-shot: put initial gamepad focus on the primary action so a user
// arriving with a controller can press A immediately rather than
// D-padding down past the artwork first.
bool s_focusPlay = false;

}  // namespace

void GameDetails_Open(int entryIndex) {
	s_detailsEntry = entryIndex;
	s_focusPlay = true;
}

int GameDetails_GetEntry() {
	return s_detailsEntry;
}

void GameDetails_Close() {
	s_detailsEntry = -1;
}

void RenderGameDetails(ATSimulator &sim, ATUIState &uiState,
	ATMobileUIState &mobileState, SDL_Window *window)
{
	(void)uiState;
	(void)window;

	ATGameLibrary *libp = GetGameLibrary();
	if (!libp) {
		mobileState.currentScreen = ATMobileUIScreen::GameBrowser;
		return;
	}
	ATGameLibrary &lib = *libp;

	auto &entries = lib.GetEntries();
	if (s_detailsEntry < 0 || (size_t)s_detailsEntry >= entries.size()) {
		GameDetails_Close();
		mobileState.currentScreen = ATMobileUIScreen::GameBrowser;
		return;
	}

	if (GameArtCache *cache = GetGameArtCache())
		cache->ProcessPending();

	GameEntry &entry = entries[s_detailsEntry];
	const GameMetadata &meta = entry.mMeta;

	ImGuiIO &io = ImGui::GetIO();
	const ATMobilePalette &pal = ATMobileGetPalette();

	const float insetT = (float)mobileState.layout.insets.top;
	const float insetB = (float)mobileState.layout.insets.bottom;
	const float insetL = (float)mobileState.layout.insets.left;
	const float insetR = (float)mobileState.layout.insets.right;

	ImGui::GetBackgroundDrawList()->AddRectFilled(
		ImVec2(0, 0), io.DisplaySize, pal.windowBg);

	ImGui::SetNextWindowPos(ImVec2(insetL, insetT));
	ImGui::SetNextWindowSize(ImVec2(
		io.DisplaySize.x - insetL - insetR,
		io.DisplaySize.y - insetT - insetB));

	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoBackground;

	if (ImGui::Begin("##GameDetails", nullptr, flags)) {
		// ATTouchScreenHeader owns Escape and Gamepad-B for this screen,
		// so we must NOT also handle them here — a single Back press
		// would otherwise pop two levels at once.
		const VDStringA displayName = VDTextWToU8(entry.mDisplayName);
		if (ATTouchScreenHeader(displayName.c_str())) {
			GameDetails_Close();
			mobileState.currentScreen = ATMobileUIScreen::GameBrowser;
			ImGui::End();
			return;
		}

		const float footerH = dp(ATTouch::kFooterReserveDouble);
		if (s_focusPlay)
			ImGui::SetNextWindowScroll(ImVec2(0, 0));
		ImGui::BeginChild("##detailsBody", ImVec2(0, -footerH),
			ImGuiChildFlags_NavFlattened);
		ATTouchDragScroll();

		// Keep right-stick scrolling available while Play or another
		// footer action has focus. ImGui scrolls the focused child itself.
		if (GImGui->NavWindow == ImGui::GetCurrentWindow()->RootWindow
			&& ImGui::GetScrollMaxY() > 0.0f)
		{
			// The backend maps the physical right stick to ImGui's
			// proportional scroll inputs in Gaming Mode.
			const float dy = ImGui::GetKeyData(ImGuiKey_GamepadLStickDown)->AnalogValue
				- ImGui::GetKeyData(ImGuiKey_GamepadLStickUp)->AnalogValue;
			if (dy != 0.0f) {
				const float speed = std::round(ImGui::GetFontSize() * 100.0f * io.DeltaTime);
				ImGui::SetScrollY(std::trunc(ImGui::GetScrollY() + dy * speed));
			}
		}

		const float availW = ImGui::GetContentRegionAvail().x;

		// --- Artwork -------------------------------------------------
		// GetTileArtPath already applies the precedence rules and
		// returns an absolute path, so draw it directly rather than
		// re-resolving a relative slot.
		{
			GameArtCache *cache = GetGameArtCache();
			const VDStringW art = lib.GetTileArtPath(entry);
			if (cache && !art.empty()) {
				int w = 0, h = 0;
				ImTextureID tex = cache->GetTexture(art, &w, &h);
				if (tex && w > 0 && h > 0) {
					float scale = availW / (float)w;
					const float scaleH = dp(220.0f) / (float)h;
					if (scaleH < scale)
						scale = scaleH;
					if (scale > 1.0f)
						scale = 1.0f;
					const float drawW = (float)w * scale;
					// Centre the artwork; a left-aligned cover on a wide
					// landscape screen looks like a layout bug.
					ImGui::SetCursorPosX(ImGui::GetCursorPosX()
						+ (availW - drawW) * 0.5f);
					ImGui::Image(tex, ImVec2(drawW, (float)h * scale));
					ImGui::Spacing();
				}
			}

			// Artwork switch, under the picture it controls.  Same
			// global preference the browser panel and the settings page
			// drive — one answer to "which artwork do I want", not three.
			ATMobileDrawArtSwitch(entry, availW);
			ImGui::Spacing();
		}

		// --- Title + facts -------------------------------------------
		// Built by the shared helpers in ui_game_metadata.cpp, which the
		// Desktop pane and the docked preview panel also use — three
		// presentations of the same handful of facts, one place that
		// decides how they read.
		{
			const VDStringA title = ATUIMetadataDisplayTitle(entry);
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextColored(ATMobileCol(pal.textTitle), "%s",
				title.c_str());
			ImGui::PopTextWrapPos();
		}

		{
			const VDStringA facts = ATUIMetadataFactsLine(entry);
			if (!facts.empty())
				ATTouchMutedText(facts.c_str());

			const VDStringA genre = ATUIMetadataGenreLine(entry);
			if (!genre.empty())
				ATTouchMutedText(genre.c_str());
		}

		// --- Description ---------------------------------------------
		if (!meta.mDescription.empty()) {
			ImGui::Spacing();
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextUnformatted(VDTextWToU8(meta.mDescription).c_str());
			ImGui::PopTextWrapPos();
		}

		// --- Library facts -------------------------------------------
		ImGui::Spacing();
		ATTouchSection("This copy");
		{
			const VDStringA copyLine = ATUIMetadataCopyLine(entry);
			if (!copyLine.empty())
				ATTouchMutedText(copyLine.c_str());

			// The path is what tells the user *which* file this entry
			// stands for — the one fact the preview panel has no room
			// for, so the full sheet is where it belongs.
			if (!entry.mVariants.empty()) {
				ImGui::PushTextWrapPos(0.0f);
				ATTouchMutedText(
					VDTextWToU8(entry.mVariants[0].mPath).c_str());
				ImGui::PopTextWrapPos();
			}
		}

		// --- Metadata state ------------------------------------------
		{
			const char *reason = ATUIMetadataEmptyReason(entry);
			if (*reason) {
				ImGui::Spacing();
				ATTouchMutedText(reason);
			}
		}

		ImGui::Dummy(ImVec2(0, dp(16.0f)));
		ATTouchEndDragScroll();
		ImGui::EndChild();

		// --- Footer actions ------------------------------------------
		// Outside the child, but the child is NavFlattened, so D-pad
		// still walks in and out of it without an extra press.
		const float btnH = dp(ATTouch::kButtonHeightNormal);
		const float gap = dp(8.0f);
		const float halfW = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;

		// Initial focus goes on Play: with a controller in hand the
		// user has already chosen the game, so one A press should start
		// it.  SetKeyboardFocusHere must be issued immediately before
		// the widget it targets.
		if (s_focusPlay) {
			// SetWindowFocus first: on the frame this screen appears the
			// previous screen's window may still own nav focus, and
			// SetKeyboardFocusHere only steers focus *within* the
			// focused window.  Same pairing the browser uses when it
			// hands focus back to the letter pill.
			ImGui::SetWindowFocus();
			ImGui::SetKeyboardFocusHere(0);
			s_focusPlay = false;
		}
		if (ATTouchButton("Play", ImVec2(halfW, btnH),
			ATTouchButtonStyle::Accent, ICON_MD_PLAY_ARROW))
		{
			const int entryIdx = s_detailsEntry;
			GameDetails_Close();
			if (entry.mVariants.size() > 1) {
				mobileState.currentScreen = ATMobileUIScreen::GameBrowser;
				GameBrowser_ShowVariantPickerForBoot(entryIdx);
			} else {
				GameBrowser_LaunchEntry(sim, mobileState, (size_t)entryIdx, 0);
			}
			ImGui::End();
			return;
		}

		ImGui::SameLine(0, gap);

		VDStringA whyNot;
		const bool canDownload = ATUIMetadataCanDownloadNow(whyNot);
		ImGui::BeginDisabled(!canDownload);
		if (ATTouchButton(meta.mStatus == GameMetaStatus::None
			? "Get metadata" : "Update metadata",
			ImVec2(halfW, btnH), ATTouchButtonStyle::Neutral,
			ICON_MD_CLOUD_DOWNLOAD))
		{
			ATUIMetadataStartDownloadForEntry(lib, s_detailsEntry);
		}
		ImGui::EndDisabled();

		// A disabled button with no explanation is a dead end on touch,
		// where there is no hover tooltip — say why inline instead.
		if (!canDownload && !whyNot.empty())
			ATTouchMutedText(whyNot.c_str());

		// Live progress, then the outcome, both inline.
		//
		// A toast alone is not enough here: this is the screen the user
		// is staring at when they press the button, and a message that
		// fades after a few seconds is a message they can miss.  Without
		// either, pressing "Get metadata" looks like it did nothing,
		// which is exactly what makes people press it again.
		{
			ATMetadataScraper& scraper = ATMetadataGetScraper();
			if (scraper.IsRunning()) {
				const VDStringA current = scraper.GetCurrentName();
				char busy[160];
				snprintf(busy, sizeof busy, "Looking up %s\xE2\x80\xA6",
					current.empty() ? "this game" : current.c_str());
				ImGui::PushStyleColor(ImGuiCol_Text,
					ATMobileCol(pal.textSection));
				ImGui::PushTextWrapPos(0.0f);
				ImGui::TextUnformatted(busy);
				ImGui::PopTextWrapPos();
				ImGui::PopStyleColor();
			} else {
				const VDStringA& report = ATUIMetadataGetLastRunText();
				if (!report.empty()) {
					const int kind = ATUIMetadataGetLastRunKind();
					uint32 col = pal.success;
					if (kind == 2) col = pal.warning;
					if (kind == 3) col = pal.danger;
					ImGui::PushStyleColor(ImGuiCol_Text, ATMobileCol(col));
					ImGui::PushTextWrapPos(0.0f);
					ImGui::TextUnformatted(report.c_str());
					ImGui::PopTextWrapPos();
					ImGui::PopStyleColor();
					if (ATTouchButton("Dismiss##runreport",
						ImVec2(0, dp(ATTouch::kButtonHeightSmall)),
						ATTouchButtonStyle::Subtle))
					{
						ATUIMetadataClearLastRun();
					}
				}
			}
		}

		// Row 2 is optional on both halves, so the separator between them
		// is emitted by whichever button comes second — a SameLine with
		// nothing after it leaves the next section hanging on the wrong
		// line.
		bool pendingSameLine = false;

		if (entry.mVariants.size() > 1) {
			if (ATTouchButton("Choose variant", ImVec2(halfW, btnH),
				ATTouchButtonStyle::Neutral, ICON_MD_LAYERS))
			{
				const int entryIdx = s_detailsEntry;
				GameDetails_Close();
				mobileState.currentScreen = ATMobileUIScreen::GameBrowser;
				GameBrowser_ShowVariantPickerForBoot(entryIdx);
				ImGui::End();
				return;
			}
			pendingSameLine = true;
		}

		// Remove from library.  Behind a confirmation, because "Remove"
		// beside a game name reads as "delete the file" to plenty of
		// people and being wrong about that is unrecoverable — the
		// confirmation says plainly that the file is left alone.
		if (pendingSameLine) {
			ImGui::SameLine(0, gap);
			pendingSameLine = false;
		}
		if (ATTouchButton("Remove from library", ImVec2(halfW, btnH),
			ATTouchButtonStyle::Neutral, ICON_MD_DELETE))
		{
			const int entryIdx = s_detailsEntry;
			VDStringA body;
			body.sprintf("Remove \"%s\" from your library?\n\n"
				"The file stays on your device. Only the library entry "
				"goes.", displayName.c_str());
			// Be straight about how long "removed" lasts for this game:
			// one that lives in a scanned folder is still in that folder.
			if (lib.WillRescanRestore((size_t)entryIdx)) {
				body += "\n\nIt sits inside a folder you scan, so a later "
					"rescan will find it again. Remove that folder under "
					"Settings > Game Library to keep it out for good.";
			}
			ShowConfirmDialog("Remove from library", body.c_str(),
				[entryIdx]() {
					ATGameLibrary *l = GetGameLibrary();
					if (!l)
						return;
					l->RemoveEntry((size_t)entryIdx);
					GameBrowser_Invalidate();
					// The sheet was showing that entry; there is nothing
					// left to show, so fall back to the browser.
					GameDetails_Close();
					g_mobileState.currentScreen =
						ATMobileUIScreen::GameBrowser;
					ATTouchPushToast("Removed from library",
						ATTouchToastSeverity::Success);
				});
		}
		pendingSameLine = true;

		// "Use current screen as art" acts on the RUNNING game, not on
		// whichever entry the sheet happens to be showing.  Offering it
		// while looking at a different game would silently retitle the
		// wrong entry, so it only appears when the two coincide.
		if (GameBrowser_FindCurrentEntry() == s_detailsEntry) {
			if (pendingSameLine) {
				ImGui::SameLine(0, gap);
				pendingSameLine = false;
			}
			if (ATTouchButton("Use current screen as art",
				ImVec2(halfW, btnH), ATTouchButtonStyle::Neutral,
				ICON_MD_IMAGE))
			{
				const VDStringA error = GameBrowser_SetCurrentFrameAsArt();
				if (!error.empty()) {
					ATMobileUI_ShowInfoModal("Could not set art",
						error.c_str());
				} else {
					ATTouchPushToast("Art updated",
						ATTouchToastSeverity::Success);
				}
			}
		}
	}
	ImGui::End();
}
