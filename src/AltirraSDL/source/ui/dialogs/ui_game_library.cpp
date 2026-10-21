//	AltirraSDL - Game Library dialog (Desktop UI)
//
//	A lightweight desktop-mode counterpart to the Gaming-Mode Game Library
//	screen.  Reuses the shared ATGameLibrary singleton (background scanner,
//	JSON cache, play history, art matching) and the exact mutation
//	sequence used by Gaming Mode's management page:
//
//	    SetSources -> PurgeRemovedSourceEntries -> SaveSettingsToRegistry
//	                -> StartScan -> ATRegistryFlushToDisk
//
//	Three tabs:
//	  Games    - sortable table, filter box, double-click / Launch button.
//	             Multi-variant entries pop a "pick variant" sub-dialog.
//	  Sources  - single merged table (Folder / Archive / File) with
//	             Add Folder, Add File/Archive, Remove, Rescan.
//	  Options  - Recursive / CrossFolderArt / AddBootedToLibrary toggles
//	             plus Clear Play History / Clear Entire Library.
//
//	File dialog callbacks can be invoked from a worker thread on some
//	SDL backends; we stash results under a mutex and drain them from the
//	main thread at the top of the render function.

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <atomic>
#include <cstdint>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

#include <vd2/system/VDString.h>
#include <vd2/system/text.h>

#include "ui_main.h"
#include "ui/gamelibrary/game_library.h"
#include "ui/gamelibrary/game_library_art.h"
#include "ui_game_metadata.h"
#include "ui_library_url.h"
#include "media/metadata_scraper.h"
#include "ui_file_dialog_sdl3.h"
#include "ui_profile_services.h"
#include <algorithm>

// External glue — defined elsewhere in the SDL3 front-end.
extern VDStringA ATGetConfigDir();
extern void ATRegistryFlushToDisk();
extern void GameBrowser_Init();
extern void GameBrowser_Invalidate();
extern ATGameLibrary *GetGameLibrary();
extern GameArtCache *GetGameArtCache();

namespace {

// -----------------------------------------------------------------------
// Async folder / file picker glue.  SDL may invoke dialog callbacks on a
// helper thread, so we only stash a UTF-8 path and consume it on the main
// thread during the next render tick.
// -----------------------------------------------------------------------
std::mutex      g_pendingMutex;
std::string     g_pendingAddFolder;
std::string     g_pendingAddFile;

void AddFolderCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	std::lock_guard<std::mutex> lock(g_pendingMutex);
	g_pendingAddFolder = filelist[0];
}

void AddFileCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	std::lock_guard<std::mutex> lock(g_pendingMutex);
	g_pendingAddFile = filelist[0];
}

// Extensions accepted by Add File/Archive — supported games + archives.
const SDL_DialogFileFilter kAddFileFilters[] = {
	{ "Games & Archives",
	  "atr;xfd;dcm;pro;atx;xex;obx;com;bin;rom;car;cas;wav;zip;atz;gz;arc" },
	{ "All Files", "*" },
};

// -----------------------------------------------------------------------
// UI-only persistent state.  Not serialized — just remembers selection /
// sort across frames while the dialog is open.
// -----------------------------------------------------------------------
int                 g_selectedEntry    = -1;
std::vector<int> g_selectedEntries;
char                g_filterBuf[128]   = {};
int                 g_sortColumn       = 0;     // 0 Name 1 Type 2 LastPlayed 3 Plays
bool                g_sortDescending   = false;
bool                g_openVariantPopup = false;
bool                g_openClearLibPopup = false;
bool                g_pendingClose     = false; // request dialog dismiss after a successful boot
size_t              g_variantEntryIdx  = 0;

// Filtered + sorted row order, rebuilt inside the table each frame and
// read afterwards by the keyboard handler.  Kept at namespace scope
// rather than local so the handler can run *after* EndTable() — the sort
// spec is only readable inside the table, and building the order there
// keeps a sort change visible on the same frame the user clicks it.
std::vector<int>    g_order;

// Set by the keyboard handler; consumed by the next frame's table render
// to bring the newly selected row into view.  One frame of latency,
// which is invisible, and it avoids having to predict row geometry
// outside the table.
bool                g_scrollToSelected = false;

// Details pane is a splitter inside this same window rather than a
// second ImGui::Begin — that keeps it out of imgui.ini and avoids the
// "dialog opens off-screen after a resolution change" problem entirely
// (see CLAUDE.md, "ImGui window positioning").  Its width is a live
// splitter drag; only the visibility is persisted, because the width is
// meaningless once the dialog is resized.
float               g_detailsWidth     = 320.0f;
const float         kDetailsMinWidth   = 240.0f;
const float         kDetailsMaxWidth   = 520.0f;
const float         kTableMinWidth     = 300.0f;

// Lowercased-substring match for the filter textbox.
bool FilterMatch(const VDStringA &haystack, const char *needle) {
	if (!needle || !*needle) return true;
	const char *h = haystack.c_str();
	for (; *h; ++h) {
		const char *hp = h;
		const char *np = needle;
		while (*hp && *np) {
			char hc = *hp; if (hc >= 'A' && hc <= 'Z') hc = (char)(hc + 32);
			char nc = *np; if (nc >= 'A' && nc <= 'Z') nc = (char)(nc + 32);
			if (hc != nc) break;
			++hp; ++np;
		}
		if (!*np) return true;
	}
	return false;
}

const char *MediaTypeLabel(GameMediaType t) {
	switch (t) {
	case GameMediaType::Disk:       return "Disk";
	case GameMediaType::Executable: return "Executable";
	case GameMediaType::Cartridge:  return "Cartridge";
	case GameMediaType::Cassette:   return "Cassette";
	default:                        return "Unknown";
	}
}

const char *SourceTypeLabel(const GameSource &s) {
	if (s.mbIsArchive) return "Archive";
	if (s.mbIsFile)    return "File";
	return "Folder";
}

VDStringA FormatAgo(uint64_t lastScan) {
	if (lastScan == 0) return VDStringA();
	uint64_t now = (uint64_t)std::time(nullptr);
	if (now < lastScan) return VDStringA("just now");
	uint64_t ago = now - lastScan;
	char buf[64];
	if (ago < 60)         snprintf(buf, sizeof(buf), "just now");
	else if (ago < 3600)  snprintf(buf, sizeof(buf), "%d min ago",   (int)(ago / 60));
	else if (ago < 86400) snprintf(buf, sizeof(buf), "%d hours ago", (int)(ago / 3600));
	else                  snprintf(buf, sizeof(buf), "%d days ago",  (int)(ago / 86400));
	return VDStringA(buf);
}

// Apply the mutation sequence used consistently by Gaming Mode — set
// sources, purge removed entries, persist settings, kick a rescan, flush
// registry.  Kept in one place so every call site stays in sync.
void CommitSources(ATGameLibrary &lib, std::vector<GameSource> sources,
	bool rescan)
{
	lib.SetSources(std::move(sources));
	lib.PurgeRemovedSourceEntries();
	lib.SaveSettingsToRegistry();
	if (rescan)
		lib.StartScan();
	GameBrowser_Invalidate();
	ATRegistryFlushToDisk();
}

// Boot a single variant path.  Pushes the shared deferred action so the
// main loop does the actual Load + ColdReset + Resume (same path that
// "Boot Image..." and the Gaming Mode browser take).  Sets the pending-
// close flag so the caller dismisses the dialog after dispatch.
void BootVariant(ATGameLibrary &lib, size_t entryIdx, size_t variantIdx) {
	const auto &entries = lib.GetEntries();
	if (entryIdx >= entries.size())                return;
	if (variantIdx >= entries[entryIdx].mVariants.size()) return;

	const auto &var = entries[entryIdx].mVariants[variantIdx];
	VDStringA pathU8 = VDTextWToU8(var.mPath);
	const uint32 profile = entries[entryIdx].mLaunchProfileId;
	ATUIBootWithProfile(pathU8.c_str(), profile);
	// The deferred action reports the error on the main thread. Keep the
	// library open and do not record a play for a missing assigned setup.
	if (profile != kATProfileId_Invalid && !ATSettingsIsValidProfile(profile))
		return;
	lib.RecordPlay(entryIdx);
	g_pendingClose = true;
}

// Classify a freshly picked "Add File/Archive" path into the matching
// GameSource flavour.  Archives (.zip/.atz/.arc/.gz) get mbIsArchive;
// everything else gets mbIsFile (same as AddBootedGame's persistence
// path).  Returns false if the extension isn't recognized at all.
bool ClassifyAddFile(const VDStringW &path, GameSource &out) {
	// Extract the extension.
	const wchar_t *name = path.c_str();
	const wchar_t *slash = nullptr;
	for (const wchar_t *p = name; *p; ++p)
		if (*p == L'/' || *p == L'\\') slash = p;
	const wchar_t *base = slash ? slash + 1 : name;

	out.mPath = path;
	out.mbIsArchive = false;
	out.mbIsFile = false;

	if (IsArchiveExtension(base)) {
		out.mbIsArchive = true;
		return true;
	}
	if (IsSupportedGameExtension(base)) {
		out.mbIsFile = true;
		return true;
	}
	return false;
}

bool BeginLibraryTab(const char* label) {
	const bool visible = ImGui::BeginTabItem(label);
#ifdef IMGUI_ENABLE_TEST_ENGINE
	ImGuiContext& g = *ImGui::GetCurrentContext();
	IMGUI_TEST_ENGINE_ITEM_INFO(ImGui::GetItemID(), label, 0);
#endif
	return visible;
}

// ----- Tab: Games ----------------------------------------------------------

// Launch callback handed to the details pane.  Same rules as a
// double-click in the table: one variant boots straight away, several
// pop the picker.
void LaunchSelected(void *userData, int entryIndex) {
	ATGameLibrary &lib = *(ATGameLibrary *)userData;
	if (entryIndex < 0 || (size_t)entryIndex >= lib.GetEntries().size())
		return;
	if (lib.GetEntries()[entryIndex].mVariants.size() > 1) {
		g_variantEntryIdx = (size_t)entryIndex;
		g_openVariantPopup = true;
	} else {
		BootVariant(lib, (size_t)entryIndex, 0);
	}
}

// Draw one library row's Name cell: cover thumbnail, title, and — when
// we have metadata — a muted second line carrying publisher / year /
// genre.  This is what makes a downloaded description visible *in the
// list*, instead of only in the pane.
//
// Everything is drawn straight to the draw list at absolute screen
// coordinates.  ImGui pushes a per-cell clip rect, so long titles and
// wide fact lines are clipped to the Name column for free.
void DrawNameCell(ATGameLibrary &lib, const GameEntry &e,
	const ImVec2 &rowMin, float cellW, float rowH)
{
	ImDrawList *dl = ImGui::GetWindowDrawList();
	const float pad = 4.0f;
	const float thumbSz = rowH - pad * 2.0f;
	const float thumbX = rowMin.x + pad;
	const float thumbY = rowMin.y + pad;

	int artW = 0, artH = 0;
	ImTextureID artTex = (ImTextureID)0;
	GameArtCache *cache = GetGameArtCache();
	// GetTileArtPath applies the art precedence: user-set custom art
	// beats downloaded metadata media, which beats scanner-matched art.
	const VDStringW artPath = lib.GetTileArtPath(e);
	if (cache && !artPath.empty())
		artTex = cache->GetTexture(artPath, &artW, &artH);

	dl->AddRectFilled(ImVec2(thumbX, thumbY),
		ImVec2(thumbX + thumbSz, thumbY + thumbSz),
		IM_COL32(24, 24, 28, 255), 3.0f);

	if (artTex && artW > 0 && artH > 0) {
		// Fit inside the square, centred, aspect preserved.
		float scale = thumbSz / (float)artW;
		const float scaleH = thumbSz / (float)artH;
		if (scaleH < scale) scale = scaleH;
		const float dw = (float)artW * scale;
		const float dh = (float)artH * scale;
		dl->AddImage(artTex,
			ImVec2(thumbX + (thumbSz - dw) * 0.5f,
				thumbY + (thumbSz - dh) * 0.5f),
			ImVec2(thumbX + (thumbSz + dw) * 0.5f,
				thumbY + (thumbSz + dh) * 0.5f));
	} else {
		// No art: a media-type initial keeps the column from looking
		// broken and still tells the user something useful.
		const char *badge = "?";
		if (!e.mVariants.empty()) {
			switch (e.mVariants[0].mType) {
			case GameMediaType::Disk:       badge = "D"; break;
			case GameMediaType::Executable: badge = "X"; break;
			case GameMediaType::Cartridge:  badge = "C"; break;
			case GameMediaType::Cassette:   badge = "T"; break;
			default: break;
			}
		}
		const ImVec2 bs = ImGui::CalcTextSize(badge);
		dl->AddText(ImVec2(thumbX + (thumbSz - bs.x) * 0.5f,
			thumbY + (thumbSz - bs.y) * 0.5f),
			ImGui::GetColorU32(ImGuiCol_TextDisabled), badge);
	}

	const float textX = thumbX + thumbSz + 8.0f;
	const float lineH = ImGui::GetTextLineHeight();
	const VDStringA nameU8 = VDTextWToU8(e.mDisplayName);
	const VDStringA sub = ATUIMetadataFactsLine(e);

	if (sub.empty()) {
		// One line: centre it against the thumbnail.
		dl->AddText(ImVec2(textX, rowMin.y + (rowH - lineH) * 0.5f),
			ImGui::GetColorU32(ImGuiCol_Text), nameU8.c_str());
	} else {
		const float blockH = lineH * 2.0f;
		const float top = rowMin.y + (rowH - blockH) * 0.5f;
		dl->AddText(ImVec2(textX, top),
			ImGui::GetColorU32(ImGuiCol_Text), nameU8.c_str());
		dl->AddText(ImVec2(textX, top + lineH),
			ImGui::GetColorU32(ImGuiCol_TextDisabled), sub.c_str());
	}

	(void)cellW;
}

// Vertically centre a single line of text in a taller table row.
void CenterInRow(float rowH) {
	const float lineH = ImGui::GetTextLineHeight();
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (rowH - lineH) * 0.5f);
}

// Keyboard navigation for the table.  The Desktop build deliberately
// leaves ImGuiConfigFlags_NavEnableKeyboard off (it would change focus
// behaviour across every dialog in the app), so arrow-key browsing is
// implemented here against the visible row order instead.  This is what
// makes the details pane follow the keyboard the way it follows the
// mouse.
void HandleTableKeys(ATGameLibrary &lib) {
	// Never steal keys from the filter box or any other active widget.
	if (ImGui::IsAnyItemActive()
		|| ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
		return;
	if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
		return;

	// While this tab has focus the keyboard belongs to it, not to the
	// Atari.  The Desktop build leaves ImGuiConfigFlags_NavEnableKeyboard
	// off, so io.WantCaptureKeyboard would otherwise stay false for a
	// plain non-modal window and the main loop would forward these very
	// same arrows to the emulated joystick — browsing the library would
	// jiggle the stick in the running game, and Enter would launch a game
	// *and* press Return on the Atari.
	//
	// Requested from the frame the window gains focus, so it is already
	// in effect by the time the user can press anything.  Global
	// accelerators are dispatched ahead of this check in the main loop
	// and keep working.
	ImGui::SetNextFrameWantCaptureKeyboard(true);

	if (g_order.empty())
		return;

	int cur = -1;
	for (size_t i = 0; i < g_order.size(); ++i) {
		if (g_order[i] == g_selectedEntry) { cur = (int)i; break; }
	}

	const int last = (int)g_order.size() - 1;
	int next = cur;

	if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true))
		next = (cur < 0) ? 0 : (cur < last ? cur + 1 : last);
	else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true))
		next = (cur < 0) ? last : (cur > 0 ? cur - 1 : 0);
	else if (ImGui::IsKeyPressed(ImGuiKey_PageDown, true))
		next = (cur < 0) ? 0 : (cur + 10 > last ? last : cur + 10);
	else if (ImGui::IsKeyPressed(ImGuiKey_PageUp, true))
		next = (cur < 0) ? 0 : (cur - 10 < 0 ? 0 : cur - 10);
	else if (ImGui::IsKeyPressed(ImGuiKey_Home, false))
		next = 0;
	else if (ImGui::IsKeyPressed(ImGuiKey_End, false))
		next = last;
	else if (ImGui::IsKeyPressed(ImGuiKey_Enter, false)
		|| ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
	{
		if (cur >= 0)
			LaunchSelected(&lib, g_order[cur]);
		return;
	}

	if (next != cur && next >= 0) {
		g_selectedEntry = g_order[next];
		g_selectedEntries.assign(1, g_selectedEntry);
		g_scrollToSelected = true;
	}
}

void RenderTabGames(ATGameLibrary &lib) {
	GameLibrarySettings settings = lib.GetSettings();

	// Top row: [filter] [status] .......... [Show/Hide details]
	//
	// Every width here is measured rather than assumed.  The previous
	// version gave the filter "all but 260px" and then right-aligned the
	// button at a fixed offset, which collided as soon as the status text
	// grew: "9219 games · 53 min ago" is far wider than "12 games", and
	// it ran straight under the button.
	//
	// The filter is also capped.  A text box stretched across a 1600px
	// dialog looks broken, and nobody needs 1600px to type a few letters.
	{
		const ImGuiStyle& style = ImGui::GetStyle();
		const float rowStartX = ImGui::GetCursorPosX();
		const float avail = ImGui::GetContentRegionAvail().x;
		const float spacing = style.ItemSpacing.x;

		// Status text, measured before anything is laid out.
		char status[96];
		if (lib.IsScanning()) {
			snprintf(status, sizeof status, "Scanning... (%d)",
				lib.GetScanProgress());
		} else {
			const VDStringA ago = FormatAgo(lib.GetLastScanTime());
			if (!ago.empty())
				snprintf(status, sizeof status, "%zu games  \xC2\xB7  %s",
					lib.GetEntryCount(), ago.c_str());
			else
				snprintf(status, sizeof status, "%zu games",
					lib.GetEntryCount());
		}
		const float statusW = ImGui::CalcTextSize(status).x;

		const char *toggleLabel = settings.mbShowDetailsPanel
			? "Hide details" : "Show details";
		const float buttonW = ImGui::CalcTextSize("Hide details").x
			+ style.FramePadding.x * 2.0f + 12.0f;

		const float kFilterMin = 160.0f;
		const float kFilterMax = 340.0f;

		// Give the filter what is left after the fixed parts, then clamp.
		float filterW = avail - buttonW - spacing * 2.0f - statusW;
		if (filterW > kFilterMax) filterW = kFilterMax;

		// Too narrow to seat all three: the status line is the one that
		// can go.  It is a nicety; the filter and the toggle are not.
		bool showStatus = true;
		if (filterW < kFilterMin) {
			filterW = avail - buttonW - spacing;
			showStatus = false;
			if (filterW > kFilterMax) filterW = kFilterMax;
			if (filterW < 60.0f)      filterW = 60.0f;
		}

		ImGui::SetNextItemWidth(filterW);
		ImGui::InputTextWithHint("##filter", "Filter by name...",
			g_filterBuf, sizeof(g_filterBuf));

		if (showStatus) {
			ImGui::SameLine();
			if (lib.IsScanning()) {
				ImGui::TextColored(ImVec4(0.45f, 0.65f, 0.90f, 1.0f),
					"%s", status);
			} else {
				ImGui::TextUnformatted(status);
			}
		}

		// Details toggle, right-aligned.  It is a view control, not an
		// action on the selected game, so it belongs with the filter
		// rather than down among the launch buttons — and up here it is
		// actually discoverable.
		ImGui::SameLine();
		ImGui::SetCursorPosX(rowStartX + avail - buttonW);
		if (ImGui::Button(toggleLabel, ImVec2(buttonW, 0))) {
			settings.mbShowDetailsPanel = !settings.mbShowDetailsPanel;
			lib.SetSettings(settings);
			lib.SaveSettingsToRegistry();
			ATRegistryFlushToDisk();
		}
	}

	// Metadata toolbar: download action + coverage counts.
	ATUIRenderMetadataToolbar(lib, g_selectedEntry);

	const bool showDetails = settings.mbShowDetailsPanel;

	// The details pane is a splitter inside this window.  Below a certain
	// dialog width there is no honest way to show both, so the table wins
	// — silently, because the user did not do anything wrong.
	const float totalW = ImGui::GetContentRegionAvail().x;
	const float splitterW = 6.0f;
	float detailsW = 0.0f;
	if (showDetails && totalW >= kTableMinWidth + kDetailsMinWidth + splitterW) {
		detailsW = g_detailsWidth;
		const float maxW = totalW - kTableMinWidth - splitterW;
		if (detailsW > maxW)             detailsW = maxW;
		if (detailsW > kDetailsMaxWidth) detailsW = kDetailsMaxWidth;
		if (detailsW < kDetailsMinWidth) detailsW = kDetailsMinWidth;
	}

	// Absolute, not a negative reservation: the same number has to size
	// the table, the splitter and the details pane, and a negative size
	// resolves against whatever the cursor happens to be at, which is
	// not the same for all three once SameLine has moved it.
	const float bodyH = ImGui::GetContentRegionAvail().y
		- ImGui::GetFrameHeightWithSpacing() * 2 - 4.0f;
	const float tableW = detailsW > 0.0f
		? totalW - detailsW - splitterW
		: 0.0f;

	// Two-line rows: title on top, publisher / year / genre underneath.
	const float rowH = ImGui::GetTextLineHeight() * 2.0f + 8.0f;
	// What a row actually measures once the table adds its cell padding.
	// The clipper needs the real figure or SetScrollHereY lands short on
	// long lists.
	const float clipRowH = rowH + ImGui::GetStyle().CellPadding.y * 2.0f;

	// Table: Name | Type | Variants | Last Played | Plays | Metadata
	if (ImGui::BeginTable("##GamesTbl", 6,
		ImGuiTableFlags_Borders      | ImGuiTableFlags_RowBg      |
		ImGuiTableFlags_Sortable     | ImGuiTableFlags_Resizable  |
		ImGuiTableFlags_ScrollY,
		ImVec2(tableW, bodyH)))
	{
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Name",        ImGuiTableColumnFlags_WidthStretch, 0.55f, 0);
		ImGui::TableSetupColumn("Type",        ImGuiTableColumnFlags_WidthFixed,   90.0f, 1);
		ImGui::TableSetupColumn("Variants",    ImGuiTableColumnFlags_WidthFixed,   70.0f, 4);
		ImGui::TableSetupColumn("Last played", ImGuiTableColumnFlags_WidthFixed,   140.0f, 2);
		ImGui::TableSetupColumn("Plays",       ImGuiTableColumnFlags_WidthFixed,   60.0f, 3);
		ImGui::TableSetupColumn("Metadata",    ImGuiTableColumnFlags_WidthFixed,   90.0f, 5);
		ImGui::TableHeadersRow();

		// Pick up sort spec changes.
		if (ImGuiTableSortSpecs *spec = ImGui::TableGetSortSpecs()) {
			if (spec->SpecsDirty && spec->SpecsCount > 0) {
				g_sortColumn     = spec->Specs[0].ColumnUserID;
				g_sortDescending = (spec->Specs[0].SortDirection
					== ImGuiSortDirection_Descending);
				spec->SpecsDirty = false;
			}
		}

		const auto &entries = lib.GetEntries();

		// Build a filtered, sorted index list.
		g_order.clear();
		g_order.reserve(entries.size());
		for (size_t i = 0; i < entries.size(); ++i) {
			VDStringA nameU8 = VDTextWToU8(entries[i].mDisplayName);
			if (FilterMatch(nameU8, g_filterBuf))
				g_order.push_back((int)i);
		}

		std::sort(g_order.begin(), g_order.end(),
			[&](int a, int b) {
				const GameEntry &ea = entries[a];
				const GameEntry &eb = entries[b];
				int cmp = 0;
				switch (g_sortColumn) {
				case 0: {
					VDStringA na = VDTextWToU8(ea.mDisplayName);
					VDStringA nb = VDTextWToU8(eb.mDisplayName);
					cmp = strcasecmp(na.c_str(), nb.c_str());
					break;
				}
				case 1: {
					GameMediaType ta = ea.mVariants.empty()
						? GameMediaType::Unknown : ea.mVariants[0].mType;
					GameMediaType tb = eb.mVariants.empty()
						? GameMediaType::Unknown : eb.mVariants[0].mType;
					cmp = (int)ta - (int)tb;
					break;
				}
				case 2:
					if (ea.mLastPlayed < eb.mLastPlayed) cmp = -1;
					else if (ea.mLastPlayed > eb.mLastPlayed) cmp = 1;
					break;
				case 3:
					cmp = (int)ea.mPlayCount - (int)eb.mPlayCount;
					break;
				case 4:
					cmp = (int)ea.mVariants.size() - (int)eb.mVariants.size();
					break;
				case 5:
					cmp = (int)ea.mMeta.mStatus - (int)eb.mMeta.mStatus;
					break;
				}
				return g_sortDescending ? (cmp > 0) : (cmp < 0);
			});

		// Keep the selection meaningful: a filter or a rescan can drop
		// the selected entry out of the visible set, and a details pane
		// describing a game that is not on screen is worse than none.
		g_selectedEntries.erase(std::remove_if(g_selectedEntries.begin(),
			g_selectedEntries.end(), [&](int index) {
				return std::find(g_order.begin(), g_order.end(), index) == g_order.end();
			}), g_selectedEntries.end());
		int selectedRow = -1;
		for (size_t i = 0; i < g_order.size(); ++i) {
			if (g_order[i] == g_selectedEntry) { selectedRow = (int)i; break; }
		}
		if (selectedRow < 0) {
			g_selectedEntry = g_order.empty() ? -1 : g_order[0];
			g_selectedEntries.clear();
			if (g_selectedEntry >= 0)
				g_selectedEntries.push_back(g_selectedEntry);
			selectedRow = g_order.empty() ? -1 : 0;
		}

		ImGuiListClipper clipper;
		clipper.Begin((int)g_order.size(), clipRowH);
		// The row we are about to scroll to must be submitted even when
		// it is currently clipped, otherwise SetScrollHereY has nothing
		// to anchor against.  Strictly after Begin() and before the
		// first Step(): Begin() is what resets DisplayStart to -1, and
		// IncludeItemsByIndex asserts on that.  (In a release build the
		// assert is compiled out and the request is silently dropped,
		// so this would have shown up only as scroll-to-selection
		// failing on rows that happened to be clipped.)
		if (g_scrollToSelected && selectedRow >= 0)
			clipper.IncludeItemByIndex(selectedRow);
		while (clipper.Step()) {
			for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
				int idx = g_order[row];
				const GameEntry &e = entries[idx];
				ImGui::TableNextRow(ImGuiTableRowFlags_None, rowH);
				ImGui::PushID(idx);

				// Name column — selectable spans full row.
				ImGui::TableSetColumnIndex(0);
				const ImVec2 rowMin = ImGui::GetCursorScreenPos();
				const float cellW = ImGui::GetContentRegionAvail().x;
				bool selected = std::find(g_selectedEntries.begin(), g_selectedEntries.end(), idx)
					!= g_selectedEntries.end();
				if (ImGui::Selectable("##row", selected,
					ImGuiSelectableFlags_SpanAllColumns |
					ImGuiSelectableFlags_AllowDoubleClick,
					ImVec2(0, rowH)))
				{
					if (!ImGui::GetIO().KeyCtrl)
						g_selectedEntries.clear();
					auto selectedIt = std::find(g_selectedEntries.begin(), g_selectedEntries.end(), idx);
					if (selectedIt != g_selectedEntries.end())
						g_selectedEntries.erase(selectedIt);
					else
						g_selectedEntries.push_back(idx);
					g_selectedEntry = g_selectedEntries.empty() ? -1 : g_selectedEntries.back();
					if (ImGui::IsMouseDoubleClicked(0))
						LaunchSelected(&lib, idx);
				}

				if (g_scrollToSelected && selected) {
					ImGui::SetScrollHereY(0.5f);
					g_scrollToSelected = false;
				}

				// Right-click acts on the row under the cursor, which
				// is not necessarily the selected one — so select it
				// first, otherwise the menu would silently operate on a
				// different game than the one the user pointed at.
				if (ImGui::BeginPopupContextItem("##rowmenu")) {
					g_selectedEntry = idx;
					if (std::find(g_selectedEntries.begin(), g_selectedEntries.end(), idx) == g_selectedEntries.end())
						g_selectedEntries.assign(1, idx);
					if (ImGui::MenuItem("Launch", nullptr, false,
						!e.mVariants.empty()))
					{
						LaunchSelected(&lib, idx);
					}
					ImGui::Separator();
					ATUIRenderMetadataRowMenu(lib, idx);
					ImGui::EndPopup();
				}

				DrawNameCell(lib, e, rowMin, cellW, rowH);

				ImGui::TableSetColumnIndex(1);
				CenterInRow(rowH);
				GameMediaType t = e.mVariants.empty()
					? GameMediaType::Unknown : e.mVariants[0].mType;
				ImGui::TextUnformatted(MediaTypeLabel(t));

				ImGui::TableSetColumnIndex(2);
				CenterInRow(rowH);
				ImGui::Text("%zu", e.mVariants.size());

				ImGui::TableSetColumnIndex(3);
				CenterInRow(rowH);
				if (e.mLastPlayed > 0) {
					char tbuf[32];
					time_t tt = (time_t)e.mLastPlayed;
					struct tm tmv;
#ifdef _WIN32
					localtime_s(&tmv, &tt);
#else
					localtime_r(&tt, &tmv);
#endif
					strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M", &tmv);
					ImGui::TextUnformatted(tbuf);
				} else {
					ImGui::TextDisabled("\xE2\x80\x94");
				}

				ImGui::TableSetColumnIndex(4);
				CenterInRow(rowH);
				ImGui::Text("%u", e.mPlayCount);

				ImGui::TableSetColumnIndex(5);
				CenterInRow(rowH);
				{
					const GameMetaStatus st = e.mMeta.mStatus;
					ImVec4 colour(0.55f, 0.55f, 0.55f, 1.0f);
					if (st == GameMetaStatus::Matched
						|| st == GameMetaStatus::UserEdited)
					{
						colour = ImVec4(0.45f, 0.80f, 0.45f, 1.0f);
					} else if (st == GameMetaStatus::Error) {
						colour = ImVec4(0.95f, 0.60f, 0.40f, 1.0f);
					}
					ImGui::TextColored(colour, "%s",
						ATUIMetadataStatusLabel(st));
				}

				ImGui::PopID();
			}
		}

		// The target row may have been filtered away between the key
		// press and this frame; drop the request rather than leaving it
		// armed to fire on an unrelated row later.
		g_scrollToSelected = false;

		ImGui::EndTable();
	}

	if (detailsW > 0.0f) {
		// Splitter.  An invisible button carrying the resize cursor —
		// the same idiom ImGui's own docking splitters use.
		ImGui::SameLine(0, 0);
		ImGui::InvisibleButton("##split", ImVec2(splitterW, bodyH));
		if (ImGui::IsItemHovered() || ImGui::IsItemActive())
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
		if (ImGui::IsItemActive())
			g_detailsWidth = detailsW - ImGui::GetIO().MouseDelta.x;
		{
			// Draw the grip so the splitter is visible, not just felt.
			const ImVec2 mn = ImGui::GetItemRectMin();
			const ImVec2 mx = ImGui::GetItemRectMax();
			const float cx = (mn.x + mx.x) * 0.5f;
			ImGui::GetWindowDrawList()->AddLine(ImVec2(cx, mn.y),
				ImVec2(cx, mx.y),
				ImGui::GetColorU32(ImGui::IsItemActive()
					? ImGuiCol_SeparatorActive
					: (ImGui::IsItemHovered() ? ImGuiCol_SeparatorHovered
					                          : ImGuiCol_Separator)));
		}

		ImGui::SameLine(0, 0);
		if (ImGui::BeginChild("##GameDetails", ImVec2(detailsW, bodyH),
			ImGuiChildFlags_Borders))
		{
			ATUIRenderMetadataDetails(lib, g_selectedEntry,
				LaunchSelected, &lib);
		}
		ImGui::EndChild();
	}

	// Keyboard browsing runs after the table so it can see this frame's
	// row order (which is only known once the sort spec has been read).
	HandleTableKeys(lib);

	// User-requested Desktop extension: several games share one profile.
	if (g_selectedEntry >= 0 && (size_t)g_selectedEntry < lib.GetEntries().size()) {
		auto& selected = lib.GetEntries()[g_selectedEntry];
		const uint32 current = selected.mLaunchProfileId;
		VDStringA profileName = current == kATProfileId_Invalid
			? VDStringA("Current configuration")
			: ATSettingsIsValidProfile(current)
				? VDTextWToU8(ATSettingsProfileGetName(current)) : VDStringA("Missing profile - choose another");
		for (int index : g_selectedEntries)
			if (index >= 0 && (size_t)index < lib.GetEntries().size()
				&& lib.GetEntries()[index].mLaunchProfileId != current)
				profileName = "Multiple profiles";
		ImGui::SetNextItemWidth(260);
		if (ImGui::BeginCombo("Launch profile", profileName.c_str())) {
			vdfastvector<uint32> profiles;
			ATSettingsProfileEnum(profiles);
			profiles.insert(profiles.begin(), 0);
			profiles.insert(profiles.begin(), kATProfileId_Invalid);
			for (uint32 id : profiles) {
				if (id != 0 && id != kATProfileId_Invalid && !ATSettingsProfileGetVisible(id))
					continue;
				VDStringA name = id == kATProfileId_Invalid ? VDStringA("Current configuration")
					: id == 0 ? VDStringA("Global profile") : VDTextWToU8(ATSettingsProfileGetName(id));
				ImGui::PushID((int)id);
				if (ImGui::Selectable(name.c_str(), id == current)) {
					if (g_selectedEntries.empty())
						g_selectedEntries.push_back(g_selectedEntry);
					for (int index : g_selectedEntries)
						if (index >= 0 && (size_t)index < lib.GetEntries().size())
							lib.GetEntries()[index].mLaunchProfileId = id;
					lib.SaveCache();
					GameBrowser_Invalidate();
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		ImGui::SetItemTooltip("Ctrl-click games to assign one shared profile to several titles. Launch changes are temporary; update the saved profile explicitly in Profiles.");
		ImGui::SameLine();
		ImGui::TextDisabled("Applies to %zu selected", std::max<size_t>(1, g_selectedEntries.size()));
	}

	// Bottom row: Launch button.
	bool canLaunch = (g_selectedEntry >= 0)
		&& ((size_t)g_selectedEntry < lib.GetEntries().size())
		&& !lib.GetEntries()[g_selectedEntry].mVariants.empty();
	if (!canLaunch) ImGui::BeginDisabled();
	if (ImGui::Button("Launch", ImVec2(120, 0)))
		LaunchSelected(&lib, g_selectedEntry);
	if (!canLaunch) ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::TextDisabled("Double-click or Enter to play  \xC2\xB7  "
		"arrow keys to browse  \xC2\xB7  right-click for more");
}

// ----- Tab: Sources --------------------------------------------------------

void RenderTabSources(ATGameLibrary &lib, SDL_Window *window) {
	const auto &srcRef = lib.GetSources();

	if (ImGui::BeginTable("##SourcesTbl", 3,
		ImGuiTableFlags_Borders    | ImGuiTableFlags_RowBg     |
		ImGuiTableFlags_Resizable  | ImGuiTableFlags_ScrollY,
		ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2 - 8.0f)))
	{
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 90.0f);
		ImGui::TableSetupColumn("",     ImGuiTableColumnFlags_WidthFixed, 80.0f);
		ImGui::TableHeadersRow();

		int removeIdx = -1;
		for (size_t i = 0; i < srcRef.size(); ++i) {
			ImGui::TableNextRow();
			ImGui::PushID((int)i);

			ImGui::TableSetColumnIndex(0);
			VDStringA pathU8 = VDTextWToU8(srcRef[i].mPath);
			ImGui::TextUnformatted(pathU8.c_str());

			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(SourceTypeLabel(srcRef[i]));

			ImGui::TableSetColumnIndex(2);
			if (ImGui::SmallButton("Remove"))
				removeIdx = (int)i;

			ImGui::PopID();
		}

		ImGui::EndTable();

		if (removeIdx >= 0) {
			auto sources = srcRef;       // copy before mutation
			sources.erase(sources.begin() + removeIdx);
			CommitSources(lib, std::move(sources), /*rescan=*/true);
			// Clear selection in case its entry just vanished.
			g_selectedEntry = -1;
		}
	}

	// Bottom action row.  On WASM, SDL_ShowOpenFolderDialog returns no
	// path (browsers expose no folder picker for emscripten's SDL3
	// shim), so the standard "Add Folder..." button silently does
	// nothing.  Replace it with a text-input + Add pair so users can
	// still register a VFS folder by typing the path (e.g. one created
	// by the wizard's pack install at /home/web_user/games/<name>),
	// then a Browse button that walks the VFS.
	bool addFolderClicked = false;
	std::string typedFolder;
#if defined(__EMSCRIPTEN__)
	{
		static char vfsPath[256] = "/home/web_user/games/";
		ImGui::SetNextItemWidth(280.0f);
		bool entered = ImGui::InputTextWithHint("##addfolderpath",
			"VFS folder, e.g. /home/web_user/games/MyPack",
			vfsPath, sizeof vfsPath,
			ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::SameLine();
		bool clicked = ImGui::Button("Add Folder");
		if ((entered || clicked) && vfsPath[0]) {
			typedFolder = vfsPath;
			addFolderClicked = true;
		}
	}
#else
	if (ImGui::Button("Add Folder...")) {
		ATUIShowOpenFolderDialog('glib', AddFolderCallback, nullptr,
			window);
	}
#endif
	ImGui::SameLine();
	if (ImGui::Button("Add File or Archive...")) {
		ATUIShowOpenFileDialog('glib', AddFileCallback, nullptr, window,
			kAddFileFilters, 2, false);
	}
	if (ImGui::Button(ATUIGetLibraryURLStatus().pending
		? "Import progress..." : "Add from URL..."))
		ATUIShowLibraryURLDialog();
	ImGui::SameLine();
	// Stash the typed path for the post-render apply step (matches the
	// async-callback path on native — keeps the apply logic in one place).
	if (addFolderClicked) {
		std::lock_guard<std::mutex> lock(g_pendingMutex);
		g_pendingAddFolder = std::move(typedFolder);
	}

	bool scanning = lib.IsScanning();
	if (scanning) ImGui::BeginDisabled();
	if (ImGui::Button(scanning ? "Scanning..." : "Rescan Now")) {
		lib.StartScan();
		GameBrowser_Invalidate();
	}
	if (scanning) ImGui::EndDisabled();
}

// ----- Tab: Options --------------------------------------------------------

void RenderTabOptions(ATGameLibrary &lib) {
	GameLibrarySettings s = lib.GetSettings();
	bool changed = false;
	if (ImGui::Checkbox("Scan subfolders recursively",    &s.mbRecursive))        changed = true;
	if (ImGui::Checkbox("Match game art from other folders", &s.mbCrossFolderArt)) changed = true;
	if (ImGui::Checkbox("Add booted games to library automatically",
		&s.mbAddBootedToLibrary)) changed = true;
	if (changed) {
		lib.SetSettings(s);
		lib.SaveSettingsToRegistry();
		ATRegistryFlushToDisk();
	}

	ImGui::Separator();

	ImGui::Text("Library: %zu games", lib.GetEntryCount());
	VDStringA ago = FormatAgo(lib.GetLastScanTime());
	if (!ago.empty()) {
		ImGui::SameLine();
		ImGui::TextDisabled(" ·  last scan: %s", ago.c_str());
	}

	ImGui::Spacing();

	if (ImGui::Button("Clear Play History", ImVec2(180, 0))) {
		lib.ClearHistory();
		lib.SaveCache();
	}

	ImGui::SameLine();

	ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.60f, 0.18f, 0.18f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.22f, 0.22f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.50f, 0.14f, 0.14f, 1.0f));
	if (ImGui::Button("Clear Entire Library...", ImVec2(200, 0)))
		g_openClearLibPopup = true;
	ImGui::PopStyleColor(3);
}

// ----- Modal: pick variant -------------------------------------------------

void RenderVariantPopup(ATGameLibrary &lib) {
	if (g_openVariantPopup) {
		ImGui::OpenPopup("Pick variant##gamelib");
		g_openVariantPopup = false;
	}
	ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_Appearing);
	if (!ImGui::BeginPopupModal("Pick variant##gamelib", nullptr,
		ImGuiWindowFlags_NoSavedSettings))
		return;

	const auto &entries = lib.GetEntries();
	if (g_variantEntryIdx >= entries.size()) {
		ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		return;
	}

	const GameEntry &e = entries[g_variantEntryIdx];
	VDStringA nameU8 = VDTextWToU8(e.mDisplayName);
	ImGui::Text("%s", nameU8.c_str());
	ImGui::Separator();

	for (size_t i = 0; i < e.mVariants.size(); ++i) {
		VDStringA label = VDTextWToU8(e.mVariants[i].mLabel);
		if (label.empty()) label = VDTextWToU8(e.mVariants[i].mPath);
		ImGui::PushID((int)i);
		if (ImGui::Selectable(label.c_str())) {
			BootVariant(lib, g_variantEntryIdx, i);
			ImGui::CloseCurrentPopup();
		}
		ImGui::PopID();
	}

	ImGui::Separator();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		ImGui::CloseCurrentPopup();

	ImGui::EndPopup();
}

// ----- Modal: confirm Clear Entire Library ---------------------------------

void RenderClearLibPopup(ATGameLibrary &lib) {
	if (g_openClearLibPopup) {
		ImGui::OpenPopup("Clear Library?##gamelib");
		g_openClearLibPopup = false;
	}
	ImGui::SetNextWindowSize(ImVec2(440, 0), ImGuiCond_Appearing);
	if (!ImGui::BeginPopupModal("Clear Library?##gamelib", nullptr,
		ImGuiWindowFlags_NoSavedSettings))
		return;

	ImGui::TextWrapped("Remove all game sources and cached library data? "
		"This does not delete any game files on disk.");
	ImGui::Spacing();

	if (ImGui::Button("Clear", ImVec2(120, 0))) {
		lib.SetSources({});
		lib.GetEntries().clear();
		lib.SaveSettingsToRegistry();
		lib.SaveCache();
		// Match Gaming Mode's scrub: without this, the previous cache is
		// preserved in gamelibrary.json.bak and can be resurrected by a
		// future LoadCache fallback.
		VDStringA cacheDir = ATGetConfigDir();
		if (!cacheDir.empty() && cacheDir.back() != '/')
			cacheDir += '/';
		VDStringA bakPath = cacheDir + "gamelibrary.json.bak";
		VDStringA tmpPath = cacheDir + "gamelibrary.json.tmp";
		SDL_RemovePath(bakPath.c_str());
		SDL_RemovePath(tmpPath.c_str());
		ATRegistryFlushToDisk();
		g_selectedEntry = -1;
		GameBrowser_Invalidate();
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		ImGui::CloseCurrentPopup();

	ImGui::EndPopup();
}

// Drain file/folder picker results (always on main thread).  Commits via
// the shared mutation path so Gaming Mode and Desktop stay consistent.
void DrainPendingPicks(ATGameLibrary &lib) {
	std::string addFolder, addFile;
	{
		std::lock_guard<std::mutex> lock(g_pendingMutex);
		addFolder.swap(g_pendingAddFolder);
		addFile.swap(g_pendingAddFile);
	}

	if (!addFolder.empty()) {
		auto sources = lib.GetSources();
		VDStringW wpath = VDTextU8ToW(addFolder.c_str(), -1);
		bool dup = false;
		for (const auto &s : sources)
			if (!s.mbIsArchive && !s.mbIsFile && s.mPath == wpath)
				{ dup = true; break; }
		if (!dup) {
			GameSource src;
			src.mPath = wpath;
			sources.push_back(std::move(src));
			CommitSources(lib, std::move(sources), /*rescan=*/true);
		}
	}

	if (!addFile.empty()) {
		VDStringW wpath = VDTextU8ToW(addFile.c_str(), -1);
		GameSource src;
		if (ClassifyAddFile(wpath, src)) {
			auto sources = lib.GetSources();
			bool dup = false;
			for (const auto &s : sources)
				if (s.mbIsArchive == src.mbIsArchive
					&& s.mbIsFile == src.mbIsFile
					&& s.mPath == src.mPath)
					{ dup = true; break; }
			if (!dup) {
				sources.push_back(std::move(src));
				CommitSources(lib, std::move(sources), /*rescan=*/true);
			}
		}
	}
}

} // namespace

// ---------------------------------------------------------------------------
// Public entry point — called each frame from ui_main.cpp when
// state.showGameLibrary is true.
// ---------------------------------------------------------------------------
void ATUIRenderGameLibrary(ATSimulator & /*sim*/, ATUIState &state, SDL_Window *window) {
	// Lazily bring the shared Game Library singleton online (netplay and
	// the setup wizard follow the same pattern).
	GameBrowser_Init();
	ATGameLibrary *libp = GetGameLibrary();

	ImGui::SetNextWindowSize(ImVec2(1000, 620), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	if (!ImGui::Begin("Game Library", &state.showGameLibrary,
		ImGuiWindowFlags_NoSavedSettings))
	{
		ImGui::End();
		return;
	}

	if (!libp) {
		ImGui::TextWrapped("Game Library is not available.");
		ImGui::End();
		return;
	}
	ATGameLibrary &lib = *libp;

	// Drain any async scan results produced since the last frame.  The
	// scanner posts results atomically; calling this on the main thread
	// is what actually swaps them into mEntries.
	if (lib.IsScanComplete())
		lib.ConsumeScanResults();

	static const GameEntry *selectionData = nullptr;
	static size_t selectionCount = 0;
	if (selectionData != lib.GetEntries().data() || selectionCount != lib.GetEntryCount()) {
		g_selectedEntry = -1;
		g_selectedEntries.clear();
		selectionData = lib.GetEntries().data();
		selectionCount = lib.GetEntryCount();
	}


	// Same contract for the metadata scraper: workers post results, the
	// main thread applies them.  Doing it here — not in the tab body —
	// means a run keeps making progress while the user is on any tab.
	GameArtCache *artCache = GetGameArtCache();
	if (artCache)
		artCache->ProcessPending();
	if (ATMetadataGetScraper().ConsumeResults(lib, artCache))
		GameBrowser_Invalidate();

	DrainPendingPicks(lib);

	// Tab bar.
	if (ImGui::BeginTabBar("##GameLibTabs")) {
		if (BeginLibraryTab("Games"))    { RenderTabGames(lib);            ImGui::EndTabItem(); }
		if (BeginLibraryTab("Sources"))  { RenderTabSources(lib, window);  ImGui::EndTabItem(); }
		if (BeginLibraryTab("Metadata")) { ATUIRenderMetadataTab(lib);     ImGui::EndTabItem(); }
		if (BeginLibraryTab("Options"))  { RenderTabOptions(lib);          ImGui::EndTabItem(); }
		ImGui::EndTabBar();
	}

	// Progress lives outside the tab bar so a run stays visible and
	// cancellable no matter which tab the user is looking at.
	ATUIRenderMetadataProgress(lib);

	RenderVariantPopup(lib);
	RenderClearLibPopup(lib);
	ATUIRenderMetadataRemoveConfirm(lib);

	ImGui::End();

	// A successful boot closes the dialog so the user sees the running
	// game.  Mirrors the Gaming-Mode browser, which navigates back to the
	// emulator screen after dispatch.
	if (g_pendingClose) {
		state.showGameLibrary = false;
		g_pendingClose = false;
	}
}
