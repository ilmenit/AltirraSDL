//	AltirraSDL - Game Library Browser (Gaming Mode)
//	List and grid views for the game library with gamepad/touch/keyboard nav.

#include <stdafx.h>
#include <ctime>
#include <algorithm>
#include <SDL3/SDL.h>
#ifndef ALTIRRA_NO_SDL3_IMAGE
#include <SDL3_image/SDL_image.h>
#endif
#include <imgui.h>
#include <imgui_internal.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <at/atcore/md5.h>

#include "ui_mobile.h"
#include "ui_main.h"
#include "touch_widgets.h"
#include "simulator.h"
#include "mobile_internal.h"
#include "constants.h"
#include "cpu.h"
#include "gtia.h"
#include "versioninfo.h"
#include "settings.h"
#include "ui_mode.h"
#include "oshelper.h"
#include <vd2/system/error.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/pixmaputils.h>

#include "../gamelibrary/game_library.h"
#include "../gamelibrary/game_library_art.h"

#ifdef ALTIRRA_NETPLAY_ENABLED
#include "../netplay/ui_netplay.h"
#endif

extern ATSimulator g_sim;
extern VDStringA ATGetConfigDir();
extern void ATRegistryFlushToDisk();

// WASM only — drains the queue of GamePack source paths stashed by JS
// before the library was constructed.  Defined in wasm_bridge.cpp;
// declared here at file scope (extern "C" can't appear inside a
// function body) so GameBrowser_Init can invoke it after creating the
// library.  No-op otherwise — guard with __EMSCRIPTEN__ at the call
// site so non-WASM links don't pull an undefined symbol.
#if defined(__EMSCRIPTEN__)
extern "C" void ATWasmDrainPendingGamePackSources();
#endif

static ATGameLibrary *s_gameLibrary = nullptr;
static GameArtCache *s_artCache = nullptr;
static bool s_needsRefresh = true;
static VDStringW s_currentGameVariantPath;
static VDStringA s_searchFilter;
static bool s_searchActive = false;
static char s_searchBuf[128] = {};
static bool s_searchCursorToEnd = false;

static int SearchInputCallback(ImGuiInputTextCallbackData *data) {
	if (s_searchCursorToEnd) {
		data->CursorPos = data->BufTextLen;
		data->SelectionStart = data->SelectionEnd = data->BufTextLen;
		s_searchCursorToEnd = false;
	}
	return 0;
}

// Indices into the library's entries, filtered and sorted for display
static std::vector<size_t> s_lastPlayedIndices;
static std::vector<size_t> s_allGamesIndices;

// Letter filter state.  `s_availableLetters` tracks which letters have
// at least one library entry (A-Z=0..25, non-alpha=26); the picker grid
// only shows tiles for letters that are actually populated so the user
// never taps a dead button.  `s_letterFilterIdx` is -1 when no letter
// filter is active.
static bool s_availableLetters[27] = {};
static int  s_letterFilterIdx = -1;

// Full-screen letter picker modal (replaces the old combo/inline bar).
// Opens when the user taps the "Letter" pill on the toolbar; shows an
// "All" tile + one big tile per populated letter + Cancel, all big
// enough to hit with a finger and navigable by D-pad / arrow keys.
static bool s_letterPickerOpen = false;
// One-shot focus flag set when the modal closes so keyboard/gamepad
// nav returns to the pill that opened it.
static bool s_focusLetterPill = false;

static void ComputeAvailableLetters();
static void RenderLetterPickerModal(ImGuiIO &io,
	const ATMobileUIState &mobileState);

void GameBrowser_Init() {
	if (s_gameLibrary)
		return;

	// The whole init is wrapped in a try/catch because at least one
	// path (game library cache JSON open on a fresh WASM install,
	// before /home/web_user/.config/altirra is reliably populated by
	// the IDBFS sync) was raising an uncaught VDException that
	// terminated the wasm module and locked the user out of Gaming
	// Mode entirely.  Catching here keeps the user in a degraded-but-
	// usable state — the library starts empty, the next save/scan
	// recreates the cache file, and the partial state is logged so
	// the actual failing call site can be tracked down without
	// crashing the page.
	try {
		// Make absolutely sure the config dir exists before any
		// LoadCache / SaveCache attempts touch it.  On a fresh WASM
		// install IDBFS may not have created the directory yet by
		// the time the user reaches Gaming Mode, and a file-open
		// against a non-existent directory raises a VDException
		// from the bottom of the system layer with no opportunity
		// for a higher try/catch to recover.
		const VDStringA &configDir = ATGetConfigDir();
		if (!configDir.empty()) {
			SDL_CreateDirectory(configDir.c_str());
		}

		s_gameLibrary = new ATGameLibrary();
		s_gameLibrary->SetConfigDir(configDir);
		s_gameLibrary->LoadSettingsFromRegistry();
		s_gameLibrary->LoadCache();
		ComputeAvailableLetters();
		s_gameLibrary->StartScan();
	} catch (const MyError &e) {
		std::fprintf(stderr,
			"[gamebrowser] Init caught VDException: %s — continuing "
			"with empty library\n", e.c_str());
		// Ensure we still have a valid library object even if the
		// load path threw partway through.  The user can still add
		// games; the failed load means cached state is lost, not
		// that the feature is broken.
		if (!s_gameLibrary) {
			s_gameLibrary = new ATGameLibrary();
			s_gameLibrary->SetConfigDir(ATGetConfigDir());
		}
	} catch (const std::exception &e) {
		std::fprintf(stderr,
			"[gamebrowser] Init caught std::exception: %s — continuing "
			"with empty library\n", e.what());
		if (!s_gameLibrary) {
			s_gameLibrary = new ATGameLibrary();
			s_gameLibrary->SetConfigDir(ATGetConfigDir());
		}
	} catch (...) {
		std::fprintf(stderr,
			"[gamebrowser] Init caught unknown exception — continuing "
			"with empty library\n");
		if (!s_gameLibrary) {
			s_gameLibrary = new ATGameLibrary();
			s_gameLibrary->SetConfigDir(ATGetConfigDir());
		}
	}
	s_needsRefresh = true;

	// WASM only: drain any GamePack source paths queued by JS before
	// the library existed.  ATWasmRegisterGamePackSource(), called from
	// the Curated Library deep-link's onRuntimeReady hook, fires before
	// main() reaches Gaming Mode and the library is null at that point;
	// it stashes the path in g_pendingGamePackPaths and we apply it
	// here once the library is ready so deep-link plays show up
	// alongside wizard-installed packs without requiring a page reload.
#if defined(__EMSCRIPTEN__)
	if (s_gameLibrary) {
		ATWasmDrainPendingGamePackSources();
	}
#endif

	// Defensive: ensure the letter picker modal is not "stuck open" from
	// a prior session.  It can only be opened from the Row 3 pill and is
	// always closed by tile / Cancel / ESC, but an unexpected crash mid-
	// modal would otherwise leave it open on next launch.
	s_letterPickerOpen = false;
	s_focusLetterPill  = false;

	if (!s_artCache) {
		s_artCache = new GameArtCache();
		s_artCache->SetCacheDir(ATGetConfigDir());
	}
}

void GameBrowser_Shutdown() {
	if (s_artCache) {
		s_artCache->Shutdown();
		delete s_artCache;
		s_artCache = nullptr;
	}

	if (s_gameLibrary) {
		s_gameLibrary->CancelScan();
		delete s_gameLibrary;
		s_gameLibrary = nullptr;
	}
}

GameArtCache *GetGameArtCache() {
	return s_artCache;
}

ATGameLibrary *GetGameLibrary() {
	return s_gameLibrary;
}

void GameBrowser_Invalidate() {
	s_needsRefresh = true;
}

int GameBrowser_FindCurrentEntry() {
	if (s_currentGameVariantPath.empty() || !s_gameLibrary)
		return -1;
	auto &entries = s_gameLibrary->GetEntries();
	for (size_t i = 0; i < entries.size(); ++i) {
		for (auto &v : entries[i].mVariants) {
			if (v.mPath == s_currentGameVariantPath)
				return (int)i;
		}
	}
	return -1;
}

bool GameBrowser_HasCurrentGame() {
	return !s_currentGameVariantPath.empty();
}

void GameBrowser_ClearArtCache() {
	if (s_artCache)
		s_artCache->Clear();
}

bool GameBrowser_CurrentEntryNeedsArt() {
	if (!s_gameLibrary)
		return false;
	int eidx = GameBrowser_FindCurrentEntry();
	if (eidx < 0)
		return false;  // not in library — setting art isn't meaningful.
	const auto &entries = s_gameLibrary->GetEntries();
	if ((size_t)eidx >= entries.size())
		return false;
	return entries[eidx].mArtPath.empty();
}

VDStringA GameBrowser_SetCurrentFrameAsArt() {
#ifdef ALTIRRA_NO_SDL3_IMAGE
	return VDStringA("Image saving is not available in this build.");
#else
	if (!s_gameLibrary)
		return VDStringA("Game library is not initialised.");

	int eidx = GameBrowser_FindCurrentEntry();
	if (eidx < 0)
		return VDStringA("No currently-booted game is in the library.");

	auto &entries = s_gameLibrary->GetEntries();
	if ((size_t)eidx >= entries.size() || entries[eidx].mVariants.empty())
		return VDStringA("Library entry is invalid.");

	// Pull the clean emulator frame directly from GTIA rather than the
	// SDL framebuffer — the latter contains the hamburger menu / settings
	// panel that's on top of the game when this action is triggered.
	VDPixmapBuffer pxbuf;
	VDPixmap px;
	if (!g_sim.GetGTIA().GetLastFrameBuffer(pxbuf, px) || !px.data)
		return VDStringA("No emulator frame is available yet.");

	auto &entry = entries[eidx];

	// Content-addressed filename: MD5 of the primary variant path so the
	// PNG is stable across restarts and doesn't depend on the display
	// name (which may change with library scans).  Matches the existing
	// "Save Screenshot as Game Art" path in mobile_settings.cpp — both
	// UIs call this helper to keep the save format in sync.
	VDStringA keyU8 = VDTextWToU8(entry.mVariants[0].mPath);
	ATMD5Digest digest = ATComputeMD5(keyU8.c_str(), keyU8.size());
	char hex[33];
	for (int i = 0; i < 16; ++i)
		snprintf(hex + i * 2, 3, "%02x", digest.digest[i]);

	VDStringA configDir = ATGetConfigDir();
	VDStringA artDir = configDir;
	artDir += "/custom_art";
	SDL_CreateDirectory(artDir.c_str());

	VDStringA pngPath = artDir;
	pngPath += '/';
	pngPath += hex;
	pngPath += ".png";

	VDStringW pngPathW = VDTextU8ToW(pngPath.c_str(), -1);
	try {
		ATSaveFrame(px, pngPathW.c_str());
	} catch (const MyError &e) {
		return VDStringA(e.c_str());
	}

	// Invalidate the old-art thumbnail (if any) and the new-art thumbnail
	// so the thumbnail cache regenerates from the new source on next open.
	VDStringA thumbDir = configDir;
	thumbDir += "/thumbnails";

	auto purgeThumb = [&thumbDir](const VDStringA &artU8) {
		ATMD5Digest d = ATComputeMD5(artU8.c_str(), artU8.size());
		char h[33];
		for (int j = 0; j < 16; ++j)
			snprintf(h + j * 2, 3, "%02x", d.digest[j]);
		VDStringA tp = thumbDir;
		tp += '/';
		tp += h;
		tp += ".png";
		SDL_RemovePath(tp.c_str());
	};

	if (!entry.mArtPath.empty())
		purgeThumb(VDTextWToU8(entry.mArtPath));
	purgeThumb(pngPath);

	entry.mArtPath = std::move(pngPathW);
	s_gameLibrary->SaveCache();
	GameBrowser_ClearArtCache();
	s_needsRefresh = true;
	return VDStringA();  // success
#endif
}

void GameBrowser_OnBootedGame(const VDStringW &variantPath) {
	// Make sure the library exists so we can add to it and so the
	// "currently playing" marker persists across the next browser open.
	if (!s_gameLibrary)
		GameBrowser_Init();

	s_currentGameVariantPath = variantPath;
	s_needsRefresh = true;

	if (!s_gameLibrary || variantPath.empty())
		return;

	bool addToLibrary = s_gameLibrary->GetSettings().mbAddBootedToLibrary;
	s_gameLibrary->AddBootedGame(variantPath, addToLibrary);

	// The settings-registry write from AddBootedGame lives in the in-memory
	// provider until flushed.  Match the other Game Library callbacks which
	// flush the registry explicitly so the source survives a crash.
	if (addToLibrary)
		ATRegistryFlushToDisk();

	s_letterFilterIdx = -1;
}

static void ComputeAvailableLetters() {
	memset(s_availableLetters, 0, sizeof(s_availableLetters));
	if (!s_gameLibrary) return;
	for (const auto &entry : s_gameLibrary->GetEntries()) {
		if (entry.mDisplayName.empty()) continue;
		wchar_t c = entry.mDisplayName[0];
		if (c >= L'A' && c <= L'Z')
			s_availableLetters[c - L'A'] = true;
		else if (c >= L'a' && c <= L'z')
			s_availableLetters[c - L'a'] = true;
		else
			s_availableLetters[26] = true;
	}
}

static int LetterIdxOfEntry(const GameEntry &entry) {
	if (entry.mDisplayName.empty()) return -1;
	wchar_t c = entry.mDisplayName[0];
	if (c >= L'A' && c <= L'Z') return c - L'A';
	if (c >= L'a' && c <= L'z') return c - L'a';
	return 26;
}

static void RebuildFilteredIndices() {
	if (!s_gameLibrary)
		return;

	const auto &entries = s_gameLibrary->GetEntries();
	s_lastPlayedIndices.clear();
	s_allGamesIndices.clear();

	VDStringA filterLower;
	for (size_t i = 0; i < s_searchFilter.size(); ++i)
		filterLower += (char)std::tolower((unsigned char)s_searchFilter[i]);

	for (size_t i = 0; i < entries.size(); ++i) {
		const auto &entry = entries[i];

		// Apply letter prefix filter (exclusive with search)
		if (s_letterFilterIdx >= 0) {
			if (LetterIdxOfEntry(entry) != s_letterFilterIdx)
				continue;
		} else if (!filterLower.empty()) {
			VDStringA nameU8 = VDTextWToU8(entry.mDisplayName);
			VDStringA nameLower;
			for (size_t j = 0; j < nameU8.size(); ++j)
				nameLower += (char)std::tolower((unsigned char)nameU8[j]);

			if (nameLower.find(filterLower.c_str()) == VDStringA::npos)
				continue;
		}

		s_allGamesIndices.push_back(i);

		if (entry.mLastPlayed > 0)
			s_lastPlayedIndices.push_back(i);
	}

	// Sort last played by recency
	std::sort(s_lastPlayedIndices.begin(), s_lastPlayedIndices.end(),
		[&entries](size_t a, size_t b) {
			return entries[a].mLastPlayed > entries[b].mLastPlayed;
		});

	// Cap last played to 5
	if (s_lastPlayedIndices.size() > 5)
		s_lastPlayedIndices.resize(5);

	s_needsRefresh = false;
}

static const char *MediaTypeIcon(GameMediaType type) {
	switch (type) {
		case GameMediaType::Disk:       return "ATR";
		case GameMediaType::Executable: return "XEX";
		case GameMediaType::Cartridge:  return "CAR";
		case GameMediaType::Cassette:   return "CAS";
		default:                        return "???";
	}
}

static ImU32 MediaTypeColor(GameMediaType type) {
	// Semantic per-type badge colours.  Two calibrated palettes: the
	// Dark set is tuned to pop off the near-black thumbnail canvas, the
	// Light set is deeper/more saturated so it reads against off-white
	// cards instead of washing out.  Keep the hue assignments identical
	// (blue=disk, green=exe, amber=cart, purple=cas) so muscle memory
	// from Dark mode carries over.
	const bool dark = ATMobileGetPalette().dark;
	switch (type) {
		case GameMediaType::Disk:
			return dark ? IM_COL32( 74, 144, 217, 255)
			            : IM_COL32( 32,  92, 176, 255);
		case GameMediaType::Executable:
			return dark ? IM_COL32(123, 198, 126, 255)
			            : IM_COL32( 52, 140,  72, 255);
		case GameMediaType::Cartridge:
			return dark ? IM_COL32(232, 168,  56, 255)
			            : IM_COL32(176, 110,  14, 255);
		case GameMediaType::Cassette:
			return dark ? IM_COL32(192, 132, 216, 255)
			            : IM_COL32(128,  64, 160, 255);
		default:
			return dark ? IM_COL32(128, 128, 128, 255)
			            : IM_COL32( 90,  96, 108, 255);
	}
}

static const char *RelativeTimeStr(uint64_t timestamp) {
	static char buf[64];
	if (timestamp == 0) {
		buf[0] = 0;
		return buf;
	}

	uint64_t now = (uint64_t)std::time(nullptr);
	if (now <= timestamp) {
		snprintf(buf, sizeof(buf), "just now");
		return buf;
	}

	uint64_t delta = now - timestamp;
	if (delta < 60)
		snprintf(buf, sizeof(buf), "%ds ago", (int)delta);
	else if (delta < 3600)
		snprintf(buf, sizeof(buf), "%dm ago", (int)(delta / 60));
	else if (delta < 86400)
		snprintf(buf, sizeof(buf), "%dh ago", (int)(delta / 3600));
	else if (delta < 86400 * 2)
		snprintf(buf, sizeof(buf), "yesterday");
	else if (delta < 86400 * 7)
		snprintf(buf, sizeof(buf), "%dd ago", (int)(delta / 86400));
	else {
		time_t t = (time_t)timestamp;
		struct tm *tm = localtime(&t);
		strftime(buf, sizeof(buf), "%b %d", tm);
	}
	return buf;
}

// Full-screen letter picker.  The old implementations (a combo on
// Android, an inline button row on desktop) were either too cramped for
// finger taps or fell off narrow windows; this modal replaces both with
// a dim-backdropped sheet containing an "All" tile plus one big tile
// per letter that has games, laid out in a grid.  Nav:
//   - Tap / click / A button   → apply filter, close
//   - ESC / B / Cancel tile    → close without changing
//   - Arrow keys / D-pad       → move between tiles
// Closing the modal restores focus to the pill that opened it.
static void RenderLetterPickerModal(ImGuiIO &io,
	const ATMobileUIState &mobileState)
{
	if (!s_letterPickerOpen) return;

	const ATMobilePalette &pal = ATMobileGetPalette();

	// No separate dim backdrop: the modalBg palette colour is opaque
	// (alpha ≈ 250/255) and the sheet fills the whole window, so a dim
	// rect drawn on BackgroundDrawList would render below every window
	// — including the browser whose content we're trying to cover —
	// and be invisible.  The sheet's own WindowBg does the job.

	// Sheet covers the safe area so the tiles are as big as possible on
	// phones while staying clear of the Android status bar and gesture
	// nav.  Earlier this used the full io.DisplaySize, which pushed the
	// title under the status bar and the Cancel row under the home-bar
	// on devices with display cutouts or bottom gesture insets.  Match
	// the inset math used by the Game Library / File Browser / Settings
	// screens so the whole mobile UI feels consistent.
	float insetT = (float)mobileState.layout.insets.top;
	float insetB = (float)mobileState.layout.insets.bottom;
	float insetL = (float)mobileState.layout.insets.left;
	float insetR = (float)mobileState.layout.insets.right;
	ImGui::SetNextWindowPos(ImVec2(insetL, insetT));
	ImGui::SetNextWindowSize(ImVec2(
		io.DisplaySize.x - insetL - insetR,
		io.DisplaySize.y - insetT - insetB));

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoScrollbar;

	ImGui::PushStyleColor(ImGuiCol_WindowBg, ATMobileCol(pal.modalBg));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
		ImVec2(dp(16.0f), dp(16.0f)));

	// ESC / gamepad B closes without changing.  Check BEFORE Begin so
	// the key is not consumed by a child input.  s_focusLetterPill is
	// set so the caller re-focuses the trigger pill next frame.
	bool closed = false;
	auto close = [&]() {
		s_letterPickerOpen = false;
		s_focusLetterPill = true;
		closed = true;
	};

	if (ImGui::Begin("##LetterPickerSheet", nullptr, flags)) {
		// When the modal first appears, explicitly steal focus so
		// gamepad / keyboard nav routes into the tile grid rather than
		// staying on the (now-covered) pill that triggered us.  Touch
		// and mouse don't rely on focus so this is a no-op for them,
		// but it fixes a subtle issue where pressing A on the pill
		// opened the modal yet the next A press would still click the
		// pill underneath.
		if (ImGui::IsWindowAppearing())
			ImGui::SetWindowFocus();

		if (!s_confirmActive && !s_infoModalOpen) {
			if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)
				|| ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)
				|| ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
			{
				close();
			}
		}

		// Title.  Palette-aware so it reads on both themes.
		ImGui::PushStyleColor(ImGuiCol_Text,
			ATMobileCol(pal.textTitle));
		ImGui::TextUnformatted("Filter by letter");
		ImGui::PopStyleColor();
		ImGui::PushStyleColor(ImGuiCol_Text,
			ATMobileCol(pal.textMuted));
		ImGui::TextUnformatted("Pick a letter to narrow the game list.");
		ImGui::PopStyleColor();
		ImGui::Spacing();

		// Grid geometry: tiles are square so rotation / landscape work
		// without fiddling.  Column count auto-fits to window width so
		// phones get a comfortable 4-5 cols, tablets / landscape 7-9.
		float tileSz  = dp(64.0f);
		float tilePad = dp(8.0f);
		float availW  = ImGui::GetContentRegionAvail().x;
		int   cols    = (int)((availW + tilePad) / (tileSz + tilePad));
		if (cols < 3) cols = 3;

		// Scrollable child in case a future device adds enough letters
		// that the grid overflows (currently the max is 27 tiles plus
		// "All", which fits, but clipping with ScrollY here is free).
		// Reserve dp(72) for the Cancel row; clamp so a very short
		// landscape window can't produce a negative / zero-height
		// child (which would make tiles un-clickable).
		float childH = ImGui::GetContentRegionAvail().y - dp(72.0f);
		if (childH < dp(80.0f)) childH = dp(80.0f);
		ImGui::BeginChild("##LetterPickerGrid", ImVec2(0, childH),
			ImGuiChildFlags_None, ImGuiWindowFlags_NoSavedSettings);

		// Build the list of tiles: "All" first, then every available
		// letter.  Empty letters are simply skipped (not shown as
		// disabled) so there are no dead targets to mis-tap.
		struct Tile { int idx; const char *label; };
		char letterLabels[27][2];
		for (int i = 0; i < 26; ++i) {
			letterLabels[i][0] = (char)('A' + i);
			letterLabels[i][1] = '\0';
		}
		letterLabels[26][0] = '$';
		letterLabels[26][1] = '\0';

		std::vector<Tile> tiles;
		tiles.push_back({ -1, "All" });
		for (int i = 0; i < 27; ++i) {
			if (s_availableLetters[i])
				tiles.push_back({ i, letterLabels[i] });
		}

		int defaultFocusIdx = 0;
		for (int i = 0; i < (int)tiles.size(); ++i) {
			if (tiles[i].idx == s_letterFilterIdx) {
				defaultFocusIdx = i;
				break;
			}
		}

		for (int i = 0; i < (int)tiles.size(); ++i) {
			if (i > 0 && (i % cols) != 0)
				ImGui::SameLine(0, tilePad);

			const Tile &t = tiles[i];
			bool active = (t.idx == s_letterFilterIdx);
			ATTouchButtonStyle style = active
				? ATTouchButtonStyle::Accent
				: ATTouchButtonStyle::Neutral;

			char btnId[16];
			snprintf(btnId, sizeof(btnId), "%s##Ltp%d", t.label,
				t.idx);

			if (ATTouchButton(btnId, ImVec2(tileSz, tileSz), style)) {
				s_letterFilterIdx = t.idx;
				// Selecting a letter clears an in-progress search so
				// the two filters never fight each other (same
				// invariant the toolbar used to enforce).
				s_searchBuf[0] = '\0';
				s_searchFilter.clear();
				s_searchActive = false;
				s_needsRefresh = true;
				close();
			}
			if (i == defaultFocusIdx && !closed)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndChild();

		// Cancel row — always visible at the bottom so the user has an
		// unambiguous "dismiss" path that doesn't rely on a keyboard.
		// Neutral (not Subtle) so the button reads as a tappable surface
		// on touch; Subtle was almost invisible against the modalBg.
		ImGui::Spacing();
		if (ATTouchButton("Cancel", ImVec2(-1, dp(48.0f)),
			ATTouchButtonStyle::Neutral))
		{
			close();
		}
	}
	ImGui::End();

	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
}

// Variant picker popup
static bool s_variantPickerOpen = false;
static size_t s_variantPickerEntry = 0;
// When set, the picker is in "swap" mode: tapping a variant hands the
// path to the callback instead of booting it.  Used by the Disk
// Drives "Select" button to LoadDisk() without resetting the session.
static std::function<void(const VDStringW &)> s_variantPickerSwapCb;

// ── External-picker mode ──────────────────────────────────────────
// When the Game Browser is opened by an external screen (currently
// netplay's "Pick from Library" flow) it runs as a picker instead of
// a launcher: tapping a tile fires `s_pickerCb` with the chosen
// variant's path / indices / name / label rather than booting the
// simulator.  The Boot Game / Online Play / Boot Empty toolbar is
// replaced by a banner + Cancel button so the user sees they're in a
// picker context.  Variant picker, grid/list toggle, letter pill,
// search all stay live — the full library UX is reused.
static bool s_pickerMode = false;
static GameBrowserPickFn s_pickerCb;
static std::function<void()> s_pickerCancelCb;
static std::string s_pickerBanner;
// Screen value in effect BEFORE the picker was opened.  Restored when
// the picker closes so a user who opened Online Play → Add Game →
// Pick from Library mid-session returns to the running emulator
// (currentScreen == None) instead of being stranded in the library
// browser after the netplay overlay finally closes.
static ATMobileUIScreen s_pickerReturnScreen = ATMobileUIScreen::None;

extern ATMobileUIState g_mobileState;

void GameBrowser_OpenPicker(GameBrowserPickFn onPick,
	std::function<void()> onCancel, const char *bannerText)
{
	s_pickerReturnScreen = g_mobileState.currentScreen;
	s_pickerMode = true;
	s_pickerCb = std::move(onPick);
	s_pickerCancelCb = std::move(onCancel);
	s_pickerBanner.assign(bannerText ? bannerText : "");
	s_needsRefresh = true;
}

bool GameBrowser_IsPickerActive() { return s_pickerMode; }

void GameBrowser_ClosePicker() {
	const bool wasActive = s_pickerMode;
	s_pickerMode = false;
	s_pickerCb = nullptr;
	s_pickerCancelCb = nullptr;
	s_pickerBanner.clear();
	s_variantPickerOpen = false;
	if (wasActive) {
		g_mobileState.currentScreen = s_pickerReturnScreen;
	}
}

// Clean cancel — fires the cancel callback (if any), then tears
// picker state down.  Safe to call even when no callback was set.
static void CancelPicker() {
	auto cb = std::move(s_pickerCancelCb);
	GameBrowser_ClosePicker();
	if (cb) cb();
}

static void LaunchGame(ATSimulator &sim, ATMobileUIState &mobileState,
	size_t entryIndex, int variantIndex)
{
	if (!s_gameLibrary)
		return;

	auto &entries = s_gameLibrary->GetEntries();
	if (entryIndex >= entries.size())
		return;

	auto &entry = entries[entryIndex];
	if (variantIndex < 0 || variantIndex >= (int)entry.mVariants.size())
		return;

	const auto &var = entry.mVariants[variantIndex];

	// Picker mode: don't boot — hand off to the external callback
	// (netplay Add-Game flow).  Drains picker state before firing so
	// the callback can safely open other screens.
	if (s_pickerMode && s_pickerCb) {
		VDStringW pickPath  = var.mPath;
		VDStringW pickName  = entry.mDisplayName;
		VDStringW pickLabel = var.mLabel;
		int pickEntry   = (int)entryIndex;
		int pickVariant = variantIndex;
		auto cb = std::move(s_pickerCb);
		GameBrowser_ClosePicker();  // also clears s_variantPickerOpen
		cb(pickPath, pickEntry, pickVariant, pickName, pickLabel);
		return;
	}

	VDStringA pathU8 = VDTextWToU8(var.mPath);
	ATUIPushDeferred(kATDeferred_BootImage, pathU8.c_str());
	mobileState.gameLoaded = true;
	mobileState.currentScreen = ATMobileUIScreen::None;
	s_currentGameVariantPath = var.mPath;
	s_gameLibrary->RecordPlay(entryIndex);
	s_letterFilterIdx = -1;
	s_variantPickerOpen = false;
	s_needsRefresh = true;
	sim.Resume();
}

static void ShowVariantPicker(size_t entryIndex) {
	s_variantPickerEntry = entryIndex;
	s_variantPickerOpen = true;
	s_variantPickerSwapCb = nullptr;  // boot mode
}

int GameBrowser_FindEntryForPath(const wchar_t *path) {
	if (!s_gameLibrary || !path || !*path)
		return -1;
	const auto &entries = s_gameLibrary->GetEntries();
	for (size_t i = 0; i < entries.size(); ++i) {
		for (const auto &v : entries[i].mVariants) {
			if (v.mPath == path)
				return (int)i;
		}
	}
	return -1;
}

int GameBrowser_GetVariantCount(int entryIdx) {
	if (!s_gameLibrary || entryIdx < 0)
		return 0;
	const auto &entries = s_gameLibrary->GetEntries();
	if ((size_t)entryIdx >= entries.size())
		return 0;
	return (int)entries[entryIdx].mVariants.size();
}

void GameBrowser_ShowVariantPickerForSwap(int entryIdx,
	std::function<void(const VDStringW &)> onPick)
{
	if (!s_gameLibrary || entryIdx < 0)
		return;
	const auto &entries = s_gameLibrary->GetEntries();
	if ((size_t)entryIdx >= entries.size())
		return;
	s_variantPickerEntry = (size_t)entryIdx;
	s_variantPickerSwapCb = std::move(onPick);
	s_variantPickerOpen = true;
}

// Forward decl — the picker renderer lives below.
static void RenderVariantPicker(ATSimulator &sim,
	ATMobileUIState &mobileState);

void GameBrowser_RenderOverlays(ATSimulator &sim,
	ATMobileUIState &mobileState)
{
	// Variant picker can be opened from the Game Browser (boot mode)
	// or the Disk Drives "Select" button (swap mode).  Render from
	// the top-level dispatcher so both flows work regardless of the
	// currently-active screen.
	RenderVariantPicker(sim, mobileState);
}

static void RenderVariantPicker(ATSimulator &sim, ATMobileUIState &mobileState) {
	if (!s_variantPickerOpen || !s_gameLibrary) {
		// Clean up any stale swap callback if the picker was closed
		// by external state (library shutdown, etc.).
		if (!s_variantPickerOpen)
			s_variantPickerSwapCb = nullptr;
		return;
	}

	auto &entries = s_gameLibrary->GetEntries();
	if (s_variantPickerEntry >= entries.size()) {
		s_variantPickerOpen = false;
		s_variantPickerSwapCb = nullptr;
		return;
	}

	auto &entry = entries[s_variantPickerEntry];
	VDStringA titleU8 = VDTextWToU8(entry.mDisplayName);

	ImGui::SetNextWindowSize(ImVec2(dp(360.0f), 0),
		ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoSavedSettings;

	if (ImGui::Begin(titleU8.c_str(), &s_variantPickerOpen, flags)) {
		float btnH = dp(48.0f);
		for (int i = 0; i < (int)entry.mVariants.size(); ++i) {
			const auto &var = entry.mVariants[i];
			VDStringA labelU8 = VDTextWToU8(var.mLabel);

			char sizeStr[32];
			if (var.mFileSize >= 1024 * 1024)
				snprintf(sizeStr, sizeof(sizeStr), "%.1f MB",
					var.mFileSize / (1024.0 * 1024.0));
			else if (var.mFileSize >= 1024)
				snprintf(sizeStr, sizeof(sizeStr), "%d KB",
					(int)(var.mFileSize / 1024));
			else
				snprintf(sizeStr, sizeof(sizeStr), "%d B",
					(int)var.mFileSize);

			char btnLabel[256];
			snprintf(btnLabel, sizeof(btnLabel), "%s    %s##var%d",
				labelU8.c_str(), sizeStr, i);

			ImU32 color = MediaTypeColor(var.mType);
			ImVec2 cursor = ImGui::GetCursorScreenPos();
			ImGui::GetWindowDrawList()->AddRectFilled(
				cursor, ImVec2(cursor.x + dp(4.0f), cursor.y + btnH),
				color);
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + dp(12.0f));

			if (ATTouchButton(btnLabel, ImVec2(-1, btnH))) {
				if (s_variantPickerSwapCb) {
					// Swap mode — hand the variant path to the caller
					// (Disk Drives "Select" button) and close.  We copy
					// the callback locally because LoadDisk may trigger
					// UI state changes that invalidate the static.
					auto cb = std::move(s_variantPickerSwapCb);
					s_variantPickerSwapCb = nullptr;
					s_variantPickerOpen = false;
					VDStringW path = var.mPath;
					cb(path);
				} else {
					LaunchGame(sim, mobileState, s_variantPickerEntry, i);
					s_variantPickerOpen = false;
				}
			}
			if (i == 0)
				ImGui::SetItemDefaultFocus();
		}

		ImGui::Spacing();
		if (ATTouchButton("Cancel", ImVec2(-1, dp(40.0f)),
			ATTouchButtonStyle::Subtle))
		{
			s_variantPickerOpen = false;
			s_variantPickerSwapCb = nullptr;
		}
	}
	// Drop swap callback if the user closed the window via the "X"
	// control (s_variantPickerOpen flips via &open arg to Begin).
	if (!s_variantPickerOpen)
		s_variantPickerSwapCb = nullptr;
	ImGui::End();
}

static VDStringA BuildHardwareInfoStr(ATSimulator &sim) {
	VDStringA info;
	auto append = [&](const char *s) {
		if (!info.empty()) info += " / ";
		info += s;
	};

	switch (sim.GetHardwareMode()) {
	case kATHardwareMode_800:    append("800");    break;
	case kATHardwareMode_800XL:  append("XL");     break;
	case kATHardwareMode_130XE:  append("XE");     break;
	case kATHardwareMode_1200XL: append("1200XL"); break;
	case kATHardwareMode_XEGS:   append("XEGS");   break;
	case kATHardwareMode_5200:   append("5200");   break;
	default: break;
	}
	switch (sim.GetVideoStandard()) {
	case kATVideoStandard_NTSC:   append("NTSC");    break;
	case kATVideoStandard_PAL:    append("PAL");     break;
	case kATVideoStandard_SECAM:  append("SECAM");   break;
	case kATVideoStandard_NTSC50: append("NTSC-50"); break;
	case kATVideoStandard_PAL60:  append("PAL-60");  break;
	default: break;
	}
	switch (sim.GetMemoryMode()) {
	case kATMemoryMode_8K:     append("8K");     break;
	case kATMemoryMode_16K:    append("16K");    break;
	case kATMemoryMode_24K:    append("24K");    break;
	case kATMemoryMode_32K:    append("32K");    break;
	case kATMemoryMode_40K:    append("40K");    break;
	case kATMemoryMode_48K:    append("48K");    break;
	case kATMemoryMode_52K:    append("52K");    break;
	case kATMemoryMode_64K:    append("64K");    break;
	case kATMemoryMode_128K:   append("128K");   break;
	case kATMemoryMode_256K:   append("256K Rambo"); break;
	case kATMemoryMode_320K:   append("320K Rambo"); break;
	case kATMemoryMode_320K_Compy: append("320K Compy"); break;
	case kATMemoryMode_576K:   append("576K");   break;
	case kATMemoryMode_576K_Compy: append("576K Compy"); break;
	case kATMemoryMode_1088K:  append("1088K");  break;
	default: break;
	}
	if (sim.IsUltimate1MBEnabled()) append("U1MB");
	if (sim.GetVBXE())              append("VBXE");
	if (sim.IsRapidusEnabled())     append("Rapidus");
	if (sim.IsBASICEnabled())       append("BASIC");

	return info;
}

static void RenderGameTile(ATSimulator &sim, ATMobileUIState &mobileState,
	size_t entryIndex, float tileW, float tileH)
{
	auto &entries = s_gameLibrary->GetEntries();
	if (entryIndex >= entries.size())
		return;

	const auto &entry = entries[entryIndex];
	GameMediaType primaryType = entry.mVariants.empty()
		? GameMediaType::Unknown : entry.mVariants[0].mType;
	ImU32 color = MediaTypeColor(primaryType);

	VDStringA nameU8 = VDTextWToU8(entry.mDisplayName);
	const char *badge = entry.mVariants.empty() ? "???"
		: MediaTypeIcon(entry.mVariants[0].mType);

	float imageH = tileW * 0.75f;
	float totalH = tileH;

	char btnId[32];
	snprintf(btnId, sizeof(btnId), "##tile_%zu", entryIndex);

	ImVec2 cursor = ImGui::GetCursorScreenPos();
	ImDrawList *dl = ImGui::GetWindowDrawList();

	ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0, 0, 0, 0));
	if (ImGui::Selectable(btnId, false, ImGuiSelectableFlags_None,
		ImVec2(tileW, totalH)))
	{
		if (!ATTouchIsDraggingBeyondSlop()) {
			if (entry.mVariants.size() == 1)
				LaunchGame(sim, mobileState, entryIndex, 0);
			else if (entry.mVariants.size() > 1)
				ShowVariantPicker(entryIndex);
		}
	}
	ImGui::PopStyleColor(3);

	bool hovered = ImGui::IsItemHovered() || ImGui::IsItemFocused();

	const ATMobilePalette &pal = ATMobileGetPalette();
	ImVec2 imgTL = cursor;
	ImVec2 imgBR(cursor.x + tileW, cursor.y + imageH);

	// Try to show game art; fall back to color placeholder
	int artW = 0, artH = 0;
	ImTextureID artTex = (ImTextureID)0;
	if (s_artCache && !entry.mArtPath.empty())
		artTex = s_artCache->GetTexture(entry.mArtPath, &artW, &artH);

	if (artTex && artW > 0 && artH > 0) {
		// Fit image inside tile area, centered, with aspect ratio preserved
		float srcAspect = (float)artW / (float)artH;
		float tileAspect = tileW / imageH;
		float drawW, drawH;
		if (srcAspect > tileAspect) {
			drawW = tileW;
			drawH = tileW / srcAspect;
		} else {
			drawH = imageH;
			drawW = imageH * srcAspect;
		}
		float ox = (tileW - drawW) * 0.5f;
		float oy = (imageH - drawH) * 0.5f;

		// Dark background behind letterboxed image
		dl->AddRectFilled(imgTL, imgBR, IM_COL32(20, 20, 25, 255), dp(4.0f));

		ImVec2 uvMin(0, 0), uvMax(1, 1);
		dl->AddImage(artTex,
			ImVec2(imgTL.x + ox, imgTL.y + oy),
			ImVec2(imgTL.x + ox + drawW, imgTL.y + oy + drawH),
			uvMin, uvMax);
	} else {
		// Color block placeholder
		dl->AddRectFilled(imgTL, imgBR, color, dp(4.0f));

		// Type badge centered in tile
		ImVec2 badgeSize = ImGui::CalcTextSize(badge);
		dl->AddText(
			ImVec2(cursor.x + (tileW - badgeSize.x) * 0.5f,
				cursor.y + (imageH - badgeSize.y) * 0.5f),
			IM_COL32(255, 255, 255, 180), badge);
	}

	// Variant count badge (top-right corner)
	if (entry.mVariants.size() > 1) {
		char countStr[16];
		snprintf(countStr, sizeof(countStr), "x%d",
			(int)entry.mVariants.size());
		ImVec2 cs = ImGui::CalcTextSize(countStr);
		float badgePad = dp(4.0f);
		dl->AddRectFilled(
			ImVec2(imgBR.x - cs.x - badgePad * 2, imgTL.y),
			ImVec2(imgBR.x, imgTL.y + cs.y + badgePad * 2),
			IM_COL32(0, 0, 0, 180), dp(4.0f));
		dl->AddText(
			ImVec2(imgBR.x - cs.x - badgePad, imgTL.y + badgePad),
			IM_COL32(255, 255, 255, 220), countStr);
	}

	// Hover/focus border
	if (hovered) {
		dl->AddRect(imgTL, imgBR,
			pal.rowFocus, dp(4.0f), 0, dp(2.0f));
	}

	// Game name below tile (centered, clipped)
	float nameY = cursor.y + imageH + dp(4.0f);
	ImGui::PushClipRect(ImVec2(cursor.x, nameY),
		ImVec2(cursor.x + tileW, cursor.y + totalH), true);
	ImVec2 nameSize = ImGui::CalcTextSize(nameU8.c_str());
	float nameX = cursor.x + (tileW - nameSize.x) * 0.5f;
	if (nameX < cursor.x) nameX = cursor.x;
	dl->AddText(ImVec2(nameX, nameY), pal.text, nameU8.c_str());
	ImGui::PopClipRect();
}

static void RenderGameRow(ATSimulator &sim, ATMobileUIState &mobileState,
	size_t entryIndex, bool showTime)
{
	auto &entries = s_gameLibrary->GetEntries();
	if (entryIndex >= entries.size())
		return;

	const auto &entry = entries[entryIndex];
	float rowH = dp(52.0f);
	float indicatorW = dp(6.0f);
	float pad = dp(8.0f);

	GameMediaType primaryType = entry.mVariants.empty()
		? GameMediaType::Unknown : entry.mVariants[0].mType;
	ImU32 color = MediaTypeColor(primaryType);

	VDStringA nameU8 = VDTextWToU8(entry.mDisplayName);

	// The type badge
	const char *badge = entry.mVariants.empty() ? "???"
		: MediaTypeIcon(entry.mVariants[0].mType);

	char btnId[32];
	snprintf(btnId, sizeof(btnId), "##game_%zu", entryIndex);

	ImVec2 cursor = ImGui::GetCursorScreenPos();
	float availW = ImGui::GetContentRegionAvail().x;

	// Full-width selectable row
	if (ImGui::Selectable(btnId, false, ImGuiSelectableFlags_None,
		ImVec2(availW, rowH)))
	{
		if (!ATTouchIsDraggingBeyondSlop()) {
			if (entry.mVariants.size() == 1)
				LaunchGame(sim, mobileState, entryIndex, 0);
			else if (entry.mVariants.size() > 1)
				ShowVariantPicker(entryIndex);
		}
	}

	// Draw the row content on top of the selectable
	ImDrawList *dl = ImGui::GetWindowDrawList();
	const ATMobilePalette &pal = ATMobileGetPalette();

	float thumbSize = rowH - dp(8.0f);
	float thumbX = cursor.x + dp(4.0f);
	float thumbY = cursor.y + dp(4.0f);
	float nameStartX;

	int artW = 0, artH = 0;
	ImTextureID artTex = (ImTextureID)0;
	if (s_artCache && !entry.mArtPath.empty())
		artTex = s_artCache->GetTexture(entry.mArtPath, &artW, &artH);

	if (artTex && artW > 0 && artH > 0) {
		// Small square thumbnail
		float srcAspect = (float)artW / (float)artH;
		float drawW, drawH;
		if (srcAspect > 1.0f) {
			drawW = thumbSize;
			drawH = thumbSize / srcAspect;
		} else {
			drawH = thumbSize;
			drawW = thumbSize * srcAspect;
		}
		float ox = (thumbSize - drawW) * 0.5f;
		float oy = (thumbSize - drawH) * 0.5f;

		dl->AddRectFilled(
			ImVec2(thumbX, thumbY),
			ImVec2(thumbX + thumbSize, thumbY + thumbSize),
			IM_COL32(20, 20, 25, 255), dp(2.0f));
		dl->AddImage(artTex,
			ImVec2(thumbX + ox, thumbY + oy),
			ImVec2(thumbX + ox + drawW, thumbY + oy + drawH));

		nameStartX = thumbX + thumbSize + pad;
	} else {
		// Color indicator bar + type badge
		dl->AddRectFilled(
			ImVec2(cursor.x, cursor.y + dp(4.0f)),
			ImVec2(cursor.x + indicatorW, cursor.y + rowH - dp(4.0f)),
			color, dp(2.0f));

		float badgeX = cursor.x + indicatorW + pad;
		float textYBadge = cursor.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
		dl->AddText(ImVec2(badgeX, textYBadge), pal.textMuted, badge);

		nameStartX = badgeX + dp(40.0f);
	}

	// Game name
	float textY = cursor.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
	float nameX = nameStartX;
	float rightEdge = cursor.x + availW;
	float nameMaxX = rightEdge - dp(60.0f);

	ImGui::PushClipRect(ImVec2(nameX, cursor.y),
		ImVec2(nameMaxX, cursor.y + rowH), true);
	dl->AddText(ImVec2(nameX, textY), pal.text, nameU8.c_str());
	ImGui::PopClipRect();

	// Right side: variant count and/or time
	float rightX = rightEdge - dp(8.0f);

	if (entry.mVariants.size() > 1) {
		char countStr[16];
		snprintf(countStr, sizeof(countStr), "x%d",
			(int)entry.mVariants.size());
		ImVec2 countSize = ImGui::CalcTextSize(countStr);
		dl->AddText(ImVec2(rightX - countSize.x, textY),
			pal.textMuted, countStr);
		rightX -= countSize.x + dp(8.0f);
	}

	if (showTime && entry.mLastPlayed > 0) {
		const char *timeStr = RelativeTimeStr(entry.mLastPlayed);
		ImVec2 timeSize = ImGui::CalcTextSize(timeStr);
		dl->AddText(ImVec2(rightX - timeSize.x, textY),
			pal.textMuted, timeStr);
	}
}

// =========================================================================
// Main render
// =========================================================================

void RenderGameBrowser(ATSimulator &sim, ATUIState &uiState,
	ATMobileUIState &mobileState, SDL_Window *window)
{
	if (!s_gameLibrary) {
		GameBrowser_Init();
		return;
	}

	// Process art texture uploads from the background loader
	if (s_artCache)
		s_artCache->ProcessPending();

	// Check for completed background scan
	if (s_gameLibrary->IsScanComplete()) {
		s_gameLibrary->ConsumeScanResults();
		ComputeAvailableLetters();
		s_needsRefresh = true;
		s_variantPickerOpen = false;
		if (s_artCache)
			s_artCache->Clear();
	}

	if (s_needsRefresh) {
		ComputeAvailableLetters();
		RebuildFilteredIndices();
	}

	ImGuiIO &io = ImGui::GetIO();
	float insetT = (float)mobileState.layout.insets.top;
	float insetB = (float)mobileState.layout.insets.bottom;
	float insetL = (float)mobileState.layout.insets.left;
	float insetR = (float)mobileState.layout.insets.right;

	// Full-screen palette-aware background — matches the rest of
	// Gaming Mode (About, Disk, Settings, File Browser).
	{
		const ATMobilePalette &bgPal = ATMobileGetPalette();
		ImGui::GetBackgroundDrawList()->AddRectFilled(
			ImVec2(0, 0), io.DisplaySize, bgPal.windowBg);
	}

	ImGui::SetNextWindowPos(ImVec2(insetL, insetT));
	ImGui::SetNextWindowSize(ImVec2(
		io.DisplaySize.x - insetL - insetR,
		io.DisplaySize.y - insetT - insetB));

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoBackground;

	if (ImGui::Begin("##GameBrowser", nullptr, flags)) {

		// ── Gamepad / keyboard back navigation ───────────────────
		// ESC / B / Backspace: context-dependent.
		// If search or variant picker is active, close it.
		// If a game was running, return to it.
		// Otherwise this IS the home screen — do nothing.
		//
		// When the letter picker modal is open it owns the back key —
		// we must not also process it here, otherwise a single ESC
		// press would close the modal AND pop the browser.
		bool backPressed = false;
		if (!s_confirmActive && !s_infoModalOpen && !s_letterPickerOpen) {
			backPressed = ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false);
			if (!ImGui::IsAnyItemActive()) {
				backPressed = backPressed
					|| ImGui::IsKeyPressed(ImGuiKey_Escape, false)
					|| ImGui::IsKeyPressed(ImGuiKey_Backspace, false);
			}
		}

		if (backPressed) {
			if (s_variantPickerOpen) {
				s_variantPickerOpen = false;
			} else if (s_searchActive) {
				s_searchBuf[0] = '\0';
				s_searchFilter.clear();
				s_searchActive = false;
				s_letterFilterIdx = -1;
				s_needsRefresh = true;
			} else if (s_letterFilterIdx >= 0) {
				s_letterFilterIdx = -1;
				s_needsRefresh = true;
			} else if (s_pickerMode) {
				// Picker mode: back exits the picker without booting.
				CancelPicker();
			} else if (mobileState.gameLoaded) {
				mobileState.currentScreen = ATMobileUIScreen::None;
				sim.Resume();
			}
		}

		// ── Row 1: Hardware info + top-right app chrome ──────────
		// Settings (and Exit Gaming Mode on desktop) are app-level
		// actions, not part of the "pick and boot a game" flow, so they
		// sit right-aligned on the info row rather than competing with
		// Boot Game / Boot Empty on Row 2.  This also frees Row 2 to
		// fit comfortably in portrait orientation without clipping.
		//
		// Picker mode swaps this row out for a banner + Cancel button —
		// the user is explicitly picking a game for an external flow
		// (e.g. netplay "Pick from Library"), so Settings / Exit Gaming
		// Mode / Boot actions don't apply.
		const ATMobilePalette &palBrowser = ATMobileGetPalette();
		const auto &libSettings = s_gameLibrary->GetSettings();
		const float btnH = dp(48.0f);

		if (s_pickerMode) {
			// Banner + Cancel — analogous to the Settings / Exit chrome
			// but framed as a picker context.  Cancel also fires on
			// ESC / Gamepad-B through the back-handler above.
			const float chromeH = dp(36.0f);
			const float chromePad = dp(16.0f);
			float cancelW = ImGui::CalcTextSize("Cancel").x
				+ chromePad * 2;

			float rowY = ImGui::GetCursorPosY();
			ImGui::TextColored(ATMobileCol(palBrowser.textTitle),
				"%s", s_pickerBanner.empty()
					? "Pick a game"
					: s_pickerBanner.c_str());

			float rightX = ImGui::GetContentRegionMax().x - cancelW;
			ImGui::SameLine(0, 0);
			ImGui::SetCursorPos(ImVec2(rightX, rowY));
			if (ATTouchButton("Cancel##pickchrome",
				ImVec2(cancelW, chromeH),
				ATTouchButtonStyle::Subtle))
			{
				CancelPicker();
			}
		} else {
			VDStringA hwInfo = BuildHardwareInfoStr(sim);
			int gameCount = (int)s_allGamesIndices.size();

			// Measure chrome buttons up front so we can right-align.
			const float chromeH   = dp(36.0f);
			const float chromePad = dp(16.0f);
			float settingsW = ImGui::CalcTextSize("Settings").x
				+ chromePad * 2;
#ifndef __ANDROID__
			float exitW = ImGui::CalcTextSize("Exit Gaming Mode").x
				+ chromePad * 2;
			float chromeW = settingsW + dp(6.0f) + exitW;
#else
			float chromeW = settingsW;
#endif

			float rowY = ImGui::GetCursorPosY();
			ImGui::TextColored(ATMobileCol(palBrowser.textMuted),
				"ALTIRRA  %s  (%d game%s)",
				hwInfo.c_str(), gameCount, gameCount == 1 ? "" : "s");

			// Right-align chrome on the same line as the info text.
			// GetContentRegionMax() is window-relative (same coordinate
			// space as SetCursorPos), which is what we need here.
			float rightX = ImGui::GetContentRegionMax().x - chromeW;
			ImGui::SameLine(0, 0);
			ImGui::SetCursorPos(ImVec2(rightX, rowY));
			if (ATTouchButton("Settings##chrome",
				ImVec2(settingsW, chromeH)))
			{
				s_settingsPage = ATMobileSettingsPage::Home;
				s_settingsReturnScreen = ATMobileUIScreen::GameBrowser;
				mobileState.currentScreen = ATMobileUIScreen::Settings;
			}
#ifndef __ANDROID__
			ImGui::SameLine(0, dp(6.0f));
			if (ATTouchButton("Exit Gaming Mode##chrome",
				ImVec2(exitW, chromeH)))
			{
				ATUISetMode(ATUIMode::Desktop);
				ATUISaveMode();
				float cs = SDL_GetDisplayContentScale(
					SDL_GetDisplayForWindow(window));
				if (cs < 1.0f) cs = 1.0f;
				if (cs > 4.0f) cs = 4.0f;
				ATUIApplyModeStyle(cs);
			}
#endif

			if (s_gameLibrary->IsScanning()) {
				int found = s_gameLibrary->GetScanProgress();
				VDStringA status = s_gameLibrary->GetScanStatus();
				if (status.empty())
					ImGui::TextColored(ATMobileCol(palBrowser.textSection),
						"Scanning... %d found", found);
				else
					ImGui::TextColored(ATMobileCol(palBrowser.textSection),
						"Scanning %s... %d found",
						status.c_str(), found);
			}
		}

		// ── Row 2: Launch actions ────────────────────────────────
		// Only the two hero actions live here now.  Grid/List, the
		// letter filter, and search moved to Row 3 so this row stays
		// comfortable in portrait orientation; Settings / Exit Gaming
		// Mode moved to the Row 1 chrome.  Hidden in picker mode —
		// tapping a tile hands off to the picker callback instead of
		// booting.
		if (!s_pickerMode) {
			// Boot Game is the hero action — accent gradient.  Every
			// other button uses the neutral card-gradient style so the
			// bar reads as a cohesive surface with one stand-out.
			if (ATTouchButton("Boot Game", ImVec2(0, btnH),
				ATTouchButtonStyle::Accent))
			{
				s_romFolderMode = false;
				mobileState.currentScreen = ATMobileUIScreen::FileBrowser;
				s_fileBrowserNeedsRefresh = true;
			}

#ifdef ALTIRRA_NETPLAY_ENABLED
			ImGui::SameLine();
			if (ATTouchButton("Online Play", ImVec2(0, btnH))) {
				// Go to My Hosted Games first — that's the primary
				// entry point for the offer-list UX.  The browser is
				// reachable from there with one tap.
				ATNetplayUI_OpenMyHostedGames();
			}
#endif

			ImGui::SameLine();
			if (ATTouchButton("Boot Empty", ImVec2(0, btnH))) {
				mobileState.gameLoaded = true;
				mobileState.currentScreen = ATMobileUIScreen::None;
				sim.ColdReset();
				sim.Resume();
			}
		}

		// ── Row 3: View & Filter ─────────────────────────────────
		// `Grid | List | Letter pill | Search` — one compact toolbar
		// that stays on a single line in both portrait and landscape.
		// The letter pill opens a full-screen grid picker (defined
		// below) instead of the cramped inline row / combo that the
		// previous revisions used; tiles are finger-sized and the grid
		// only shows letters that actually have games.
		static bool s_focusLetterBar = false;
		bool letterBarHasFocus = false;
		{
			// Grid / List segmented toggle (kept identical to before so
			// muscle memory carries over).  Treated as a single logical
			// unit — both halves always share a row.
			{
				bool isGrid = libSettings.mViewMode == 1;
				float viewW = dp(56.0f);
				float viewH = dp(36.0f);
				auto setMode = [&](int mode) {
					GameLibrarySettings settings = libSettings;
					settings.mViewMode = mode;
					s_gameLibrary->SetSettings(settings);
					s_gameLibrary->SaveSettingsToRegistry();
				};
				if (ATTouchButton("Grid##view", ImVec2(viewW, viewH),
					isGrid ? ATTouchButtonStyle::Accent
					       : ATTouchButtonStyle::Neutral))
				{
					if (!isGrid) setMode(1);
				}
				ImGui::SameLine(0, dp(2.0f));
				if (ATTouchButton("List##view", ImVec2(viewW, viewH),
					!isGrid ? ATTouchButtonStyle::Accent
					        : ATTouchButtonStyle::Neutral))
				{
					if (isGrid) setMode(0);
				}
			}

			// Letter filter pill.  Label reflects the current state so
			// the user never has to open the picker just to see what's
			// filtered.  Explicit ##id so the hash stays stable when
			// the visible label changes between "All" / "A" / etc.
			//
			// Focus return: both s_focusLetterPill (set when the modal
			// closes) and s_focusLetterBar (set by the list when Up is
			// pressed at the top row) land on THIS widget rather than
			// the Grid button — the pill is what the user conceptually
			// "came from" in both cases, so SetKeyboardFocusHere(0)
			// must fire immediately before ATTouchButton for the pill,
			// not at the top of Row 3.
			ImGui::SameLine(0, dp(8.0f));
			{
				char pillLabel[32];
				if (s_letterFilterIdx < 0)
					snprintf(pillLabel, sizeof(pillLabel),
						"Letter: All##letterpill");
				else if (s_letterFilterIdx < 26)
					snprintf(pillLabel, sizeof(pillLabel),
						"Letter: %c##letterpill",
						'A' + s_letterFilterIdx);
				else
					snprintf(pillLabel, sizeof(pillLabel),
						"Letter: $##letterpill");

				if (s_focusLetterPill || s_focusLetterBar) {
					// Bring the browser window to the front (in case
					// the letter picker sheet was just closed — its
					// window may still own focus) and focus the pill
					// specifically.  SetWindowFocus() with no arg
					// targets the current window, i.e. the browser.
					ImGui::SetWindowFocus();
					ImGui::SetKeyboardFocusHere(0);
					s_focusLetterPill = false;
					s_focusLetterBar = false;
				}

				// Fix the pill width against the longest possible label
				// so the adjacent Search control doesn't hop by a few
				// pixels when the user switches between "All" (long)
				// and "A" (short).
				float pillH = dp(36.0f);
				float pillW = ImGui::CalcTextSize("Letter: All").x
					+ dp(32.0f);
				ATTouchButtonStyle pillStyle = (s_letterFilterIdx >= 0)
					? ATTouchButtonStyle::Accent
					: ATTouchButtonStyle::Neutral;
				if (ATTouchButton(pillLabel, ImVec2(pillW, pillH),
					pillStyle))
				{
					s_letterPickerOpen = true;
				}
				if (ImGui::IsItemFocused())
					letterBarHasFocus = true;
			}

			// Search button / bar.  Behaviour is unchanged from the
			// previous implementation; only the geometry is tightened
			// so it fits on the same row as the other Row 3 controls.
			static bool s_focusSearchInput = false;
			ImGui::SameLine(0, dp(8.0f));
			float searchBtnSz = dp(36.0f);
			if (s_searchActive) {
				if (s_focusSearchInput) {
					ImGui::SetKeyboardFocusHere(0);
					s_focusSearchInput = false;
				}
				float searchW = ImGui::GetContentRegionAvail().x;
				if (searchW < dp(80.0f)) searchW = dp(80.0f);
				ImGui::SetNextItemWidth(searchW);
				if (ImGui::InputText("##search", s_searchBuf,
					sizeof(s_searchBuf),
					ImGuiInputTextFlags_CallbackAlways,
					SearchInputCallback))
				{
					s_letterFilterIdx = -1;
					s_searchFilter = s_searchBuf;
					s_needsRefresh = true;
				}
				if (ImGui::IsItemFocused() || ImGui::IsItemActive())
					letterBarHasFocus = true;

				if (!s_confirmActive && !s_infoModalOpen
					&& ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
					s_searchBuf[0] = '\0';
					s_searchFilter.clear();
					s_searchActive = false;
					s_needsRefresh = true;
				} else if (ImGui::IsItemDeactivatedAfterEdit()
					&& s_searchBuf[0] == '\0')
				{
					s_searchActive = false;
				}
			} else {
				if (ATTouchButton("Search##search",
					ImVec2(0, searchBtnSz)))
				{
					s_searchActive = true;
					s_focusSearchInput = true;
					s_searchBuf[0] = '\0';
					s_searchFilter.clear();
					s_letterFilterIdx = -1;
					s_needsRefresh = true;
				}
				if (ImGui::IsItemFocused())
					letterBarHasFocus = true;

				if (!s_variantPickerOpen && !s_letterPickerOpen
					&& !ImGui::IsAnyItemActive())
				{
					if (ImGui::IsKeyPressed(ImGuiKey_F, false)
						&& io.KeyCtrl)
					{
						s_searchActive = true;
						s_focusSearchInput = true;
						s_searchBuf[0] = '\0';
						s_searchFilter.clear();
						s_letterFilterIdx = -1;
						s_needsRefresh = true;
					}

					// Type any letter (no modifier) to open search
					// pre-filled with that letter — unchanged from the
					// previous behaviour.  Suppressed while the letter
					// picker is open so the letter goes to the modal's
					// quick-jump instead of bouncing into search.
					for (int k = ImGuiKey_A; k <= ImGuiKey_Z; ++k) {
						if (ImGui::IsKeyPressed((ImGuiKey)k, false)
							&& !io.KeyCtrl && !io.KeyAlt)
						{
							s_searchActive = true;
							s_focusSearchInput = true;
							s_searchCursorToEnd = true;
							s_searchBuf[0] = 'a' + (k - ImGuiKey_A);
							s_searchBuf[1] = '\0';
							s_searchFilter = s_searchBuf;
							s_letterFilterIdx = -1;
							s_needsRefresh = true;
							break;
						}
					}
				}
			}
		}

		ImGui::Separator();

		// ── Empty state ──────────────────────────────────────────
		if (s_gameLibrary->GetSources().empty()
			&& s_gameLibrary->GetEntries().empty()
			&& !s_gameLibrary->IsScanning())
		{
			float windowW = io.DisplaySize.x - insetL - insetR;
			float windowH = io.DisplaySize.y - insetT - insetB;
			float centerX = windowW * 0.5f;
			float centerY = windowH * 0.4f;

			const char *msg1 = "No games in your library yet.";
			ImVec2 s1 = ImGui::CalcTextSize(msg1);
			ImGui::SetCursorPos(ImVec2(centerX - s1.x * 0.5f, centerY));
			ImGui::TextUnformatted(msg1);

			const char *msg2 = "Add a folder containing Atari games";
			ImVec2 s2 = ImGui::CalcTextSize(msg2);
			ImGui::SetCursorPos(ImVec2(centerX - s2.x * 0.5f,
				centerY + dp(24.0f)));
			ImGui::TextUnformatted(msg2);

			const char *msg3 = "in Settings > Game Library.";
			ImVec2 s3 = ImGui::CalcTextSize(msg3);
			ImGui::SetCursorPos(ImVec2(centerX - s3.x * 0.5f,
				centerY + dp(48.0f)));
			ImGui::TextUnformatted(msg3);

			ImVec2 btnSize(dp(240.0f), dp(48.0f));
			ImGui::SetCursorPos(ImVec2(centerX - btnSize.x * 0.5f,
				centerY + dp(96.0f)));
			if (ATTouchButton("Open Settings", btnSize,
				ATTouchButtonStyle::Accent))
			{
				s_settingsPage = ATMobileSettingsPage::GameLibrary;
				s_settingsReturnScreen = ATMobileUIScreen::GameBrowser;
				mobileState.currentScreen = ATMobileUIScreen::Settings;
			}

			ImGui::SetCursorPos(ImVec2(centerX - btnSize.x * 0.5f,
				centerY + dp(160.0f)));
			if (ATTouchButton("Boot Atari without game", btnSize)) {
				mobileState.gameLoaded = true;
				mobileState.currentScreen = ATMobileUIScreen::None;
				g_sim.ColdReset();
				g_sim.Resume();
			}

			ImGui::End();
			return;
		}

		// ── Scrollable game content ──────────────────────────────
		// No NavFlattened — keeps PgUp/PgDown confined to the
		// list.  Header→list: only from the letter bar (last row).
		// List→header: ImGui handles naturally via cross-window
		// nav scoring (child→parent).
		static bool s_focusGameList = false;
		if (letterBarHasFocus
			&& !s_variantPickerOpen
			&& (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)
				|| ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown, false)))
		{
			s_focusGameList = true;
		}

		ImGui::BeginChild("##GameList", ImVec2(0, 0),
			ImGuiChildFlags_None);
		ATTouchDragScroll();

		if (s_focusGameList) {
			ImGuiContext &g = *GImGui;
			ImGui::FocusWindow(ImGui::GetCurrentWindow());
			ImGui::SetKeyboardFocusHere(0);
			s_focusGameList = false;
		}

		bool gridMode = libSettings.mViewMode == 1;

		if (gridMode) {
			// ── Grid View ────────────────────────────────────────
			//
			// Drive the column count directly from `mGridSize` so each
			// step is guaranteed to render visibly distinct tiles.  The
			// previous implementation picked a per-size minimum tile
			// width and let integer truncation in
			// `cols = contentW / (minTileW + pad)` decide the column
			// count — on common portrait phone widths (≈360–480 dp) the
			// `Medium` and `Large` minimums collapsed to the same
			// column count and rendered identical tiles, while `Small`
			// landed one column larger and looked like a "medium" tile.
			//
			// The new approach: derive an "approximate max columns"
			// from a baseline minimum tile width, then subtract a
			// width-scaled step per gridSz so wider screens take
			// proportionally larger jumps.  This keeps Small > Medium >
			// Large in column count on every reasonable screen size.
			int gridSz = libSettings.mGridSize;
			if (gridSz < 0 || gridSz > 2) gridSz = 1;

			float contentW = ImGui::GetContentRegionAvail().x;
			float tilePad = dp(8.0f);
			const float baselineMin = dp(110.0f);
			int approxCols = (int)(contentW / (baselineMin + tilePad));
			if (approxCols < 3) approxCols = 3;
			int step = approxCols / 3;
			if (step < 1) step = 1;
			int cols = approxCols - gridSz * step;
			if (cols < 1) cols = 1;
			float tileW = (contentW - tilePad * (cols - 1)) / cols;
			float imageH = tileW * 0.75f;
			float labelH = dp(36.0f);
			float tileH = imageH + labelH;

			// Last Played section (small, no clipper needed)
			if (!s_lastPlayedIndices.empty()) {
				ImGui::PushID("lp");
				ImGui::PushStyleColor(ImGuiCol_Text,
					ATMobileCol(palBrowser.textMuted));
				ImGui::TextUnformatted("LAST PLAYED");
				ImGui::PopStyleColor();
				ImGui::Spacing();

				for (size_t i = 0; i < s_lastPlayedIndices.size(); ++i) {
					if (i > 0 && (i % cols) != 0)
						ImGui::SameLine(0, tilePad);
					RenderGameTile(sim, mobileState,
						s_lastPlayedIndices[i], tileW, tileH);
				}
				ImGui::PopID();
				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();
			}

			// All Games section with list clipper (row-based)
			{
				char label[64];
				snprintf(label, sizeof(label), "ALL GAMES (%d)",
					(int)s_allGamesIndices.size());
				ImGui::PushStyleColor(ImGuiCol_Text,
					ATMobileCol(palBrowser.textMuted));
				ImGui::TextUnformatted(label);
				ImGui::PopStyleColor();
				ImGui::Spacing();

				int rowCount = ((int)s_allGamesIndices.size() + cols - 1)
					/ cols;
				float spacing = ImGui::GetStyle().ItemSpacing.y;
				ImGuiListClipper clipper;
				clipper.Begin(rowCount, tileH + spacing);
				clipper.IncludeItemsByIndex(0, 1);
				while (clipper.Step()) {
					for (int row = clipper.DisplayStart;
						row < clipper.DisplayEnd; ++row)
					{
						for (int col = 0; col < cols; ++col) {
							size_t idx = (size_t)row * cols + col;
							if (idx >= s_allGamesIndices.size())
								break;
							if (col > 0)
								ImGui::SameLine(0, tilePad);
							RenderGameTile(sim, mobileState,
								s_allGamesIndices[idx], tileW, tileH);
						}
					}
				}
				ImGui::Spacing();
			}

		} else {
			// ── List View ────────────────────────────────────────
			static const float kListRowH[] = { 52.0f, 72.0f, 96.0f };
			int listSz = libSettings.mListSize;
			if (listSz < 0 || listSz > 2) listSz = 0;
			float rowH = dp(kListRowH[listSz]);

			// Last Played section (small, no clipper needed)
			if (!s_lastPlayedIndices.empty()) {
				ImGui::PushID("lp");
				ImGui::PushStyleColor(ImGuiCol_Text,
					ATMobileCol(palBrowser.textMuted));
				ImGui::TextUnformatted("LAST PLAYED");
				ImGui::PopStyleColor();
				ImGui::Spacing();

				for (size_t idx : s_lastPlayedIndices)
					RenderGameRow(sim, mobileState, idx, true);

				ImGui::PopID();
				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();
			}

			// All Games section with list clipper
			{
				char sectionLabel[64];
				snprintf(sectionLabel, sizeof(sectionLabel),
					"ALL GAMES (%d)", (int)s_allGamesIndices.size());
				ImGui::PushStyleColor(ImGuiCol_Text,
					ATMobileCol(palBrowser.textMuted));
				ImGui::TextUnformatted(sectionLabel);
				ImGui::PopStyleColor();
				ImGui::Spacing();

				float spacing = ImGui::GetStyle().ItemSpacing.y;
				ImGuiListClipper clipper;
				clipper.Begin((int)s_allGamesIndices.size(), rowH + spacing);
				while (clipper.Step()) {
					for (int i = clipper.DisplayStart;
						i < clipper.DisplayEnd; ++i)
					{
						RenderGameRow(sim, mobileState,
							s_allGamesIndices[i], false);
					}
				}
			}
		}

		ImGui::Spacing();

		// Detect Up-at-top: two-frame check using NavId + scroll.
		// Frame N: Up pressed → record NavId and scroll position.
		// Frame N+1: if both are unchanged, nav truly failed → exit
		// to header.  A scroll change means ImGui is revealing an
		// off-screen row (clipper boundary), so we must not exit.
		{
			static ImGuiID s_navIdOnUp = 0;
			static float s_scrollOnUp = 0.0f;
			static bool s_checkUpExit = false;
			ImGuiContext &g = *GImGui;

			if (s_checkUpExit) {
				s_checkUpExit = false;
				float scrollNow = ImGui::GetScrollY();
				if (g.NavId == s_navIdOnUp
					&& fabsf(scrollNow - s_scrollOnUp) < 1.0f)
				{
					s_focusLetterBar = true;
				}
			}

			bool upPressed = ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)
				|| ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp, false);
			if (upPressed
				&& !s_searchActive && !s_variantPickerOpen)
			{
				s_navIdOnUp = g.NavId;
				s_scrollOnUp = ImGui::GetScrollY();
				s_checkUpExit = true;
			}
		}

		ATTouchEndDragScroll();
		ImGui::EndChild();
	}
	ImGui::End();

	// Overlays — variant picker is rendered globally by
	// GameBrowser_RenderOverlays (called from ui_mobile.cpp so the
	// swap-mode picker works from other screens too).  Letter picker
	// is game-browser-specific and stays here.
	RenderLetterPickerModal(io, mobileState);
}
