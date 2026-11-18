//	AltirraSDL - Game Library Browser (Gaming Mode)
//	List and grid views for the game library with gamepad/touch/keyboard nav.

#include <stdafx.h>
#include <ctime>
#include <algorithm>
#include <cmath>
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
#include "altirra_icons.h"
#include "constants.h"
#include "cpu.h"
#include "gtia.h"
#include "versioninfo.h"
#include "settings.h"
#include "ui_mode.h"
#include "ui_frame_capture.h"
#include "oshelper.h"
#include <vd2/system/error.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/pixmaputils.h>

#include "app/android_platform.h"   // ATAndroid_Vibrate (no-op off Android)

#include "../gamelibrary/game_library.h"
#include "../gamelibrary/game_library_art.h"
#include "../dialogs/ui_game_metadata.h"
#include "media/metadata_scraper.h"
#include "media/metadata_settings.h"

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

// Wrap a toolbar item when the landscape details pane narrows the browser.
static void SameLineIfFits(float width, float gap) {
	const float right = ImGui::GetWindowPos().x + ImGui::GetContentRegionMax().x;
	if (ImGui::GetItemRectMax().x + gap + width <= right)
		ImGui::SameLine(0, gap);
}

static bool NavDownPressed() {
	return ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)
		|| ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown, false);
}

static bool NavUpPressed() {
	return ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)
		|| ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp, false);
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
	// Order matters: the scraper's workers hold no library pointers
	// (Start() snapshots everything they need), but ConsumeResults
	// writes into mEntries, so both background users must be fully
	// joined before the library object goes away.
	ATMetadataGetScraper().Shutdown();
	ATUIMetadataShutdownAccountTest();

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
	return s_gameLibrary->GetTileArtPath(entries[eidx]).empty();
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

	VDPixmapBuffer pxbuf;
	if (!ATUICaptureEmulatorFrame(g_sim, ATUIFrameCaptureMode::Display, pxbuf))
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
		ATSaveFrame(pxbuf, pngPathW.c_str());
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
			ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoSavedSettings);

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
			ATTouchButtonStyle::Neutral, ICON_MD_CLOSE))
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
	ATUIBootWithProfile(pathU8.c_str(), entry.mLaunchProfileId);
	// Leave the browser and pause state intact when the deferred action
	// reports a missing assigned profile; this was not a successful play.
	if (entry.mLaunchProfileId != kATProfileId_Invalid
		&& !ATSettingsIsValidProfile(entry.mLaunchProfileId))
		return;
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

// Public wrappers used by the Game Details sheet
// (mobile_game_details.cpp).  It deliberately does not duplicate the
// launch and picker logic — picker-mode hand-off, play-count
// recording and the resume-after-boot sequencing all live in
// LaunchGame and must stay in exactly one place.
void GameBrowser_LaunchEntry(ATSimulator &sim, ATMobileUIState &mobileState,
	size_t entryIndex, int variantIndex)
{
	LaunchGame(sim, mobileState, entryIndex, variantIndex);
}

void GameBrowser_ShowVariantPickerForBoot(int entryIdx) {
	if (entryIdx < 0)
		return;
	ShowVariantPicker((size_t)entryIdx);
}

// Choose the dock before drawing the tiles so gestures know whether to
// highlight the docked preview or open the dedicated details screen.
static ATGamePreviewDock s_previewDock = ATGamePreviewDock::None;

// ── Highlight (preview) selection ─────────────────────────────────
//
// The docked preview panel needs a persistent notion of "the game the
// user is looking at".  Three of the four input devices already supply
// one for free:
//
//     gamepad / keyboard   ImGui nav focus
//     mouse                hover
//     touch                nothing — so long-press supplies it
//
// so this is a latch fed by whichever of those fired most recently.  It
// is deliberately never cleared when focus or hover goes away: a panel
// that blanked the instant the pointer left the grid would be useless,
// and it would flicker on every mouse move between tiles.
static int s_previewEntry = -1;
static ImGuiID s_previewItem = 0;

static void SetPreviewFromItem(size_t entryIndex) {
	s_previewEntry = (int)entryIndex;
	s_previewItem = ImGui::GetItemID();
}

static bool IsPreviewHighlighted(size_t entryIndex, bool recentlyPlayed) {
	if (s_previewDock == ATGamePreviewDock::None)
		return false;

	const ImGuiContext &g = *GImGui;
	if (g.NavCursorVisible && g.NavWindow == ImGui::GetCurrentWindow())
		return ImGui::IsItemFocused();

	return s_previewEntry == (int)entryIndex
		&& (s_previewItem ? s_previewItem == ImGui::GetItemID() : !recentlyPlayed);
}

// Call immediately after a tile/row's Selectable.
static void UpdatePreviewFromItem(size_t entryIndex) {
	if (s_previewDock == ATGamePreviewDock::None)
		return;

	const ImGuiContext &g = *GImGui;
	if (g.NavCursorVisible && g.NavWindow == ImGui::GetCurrentWindow()) {
		if (ImGui::IsItemFocused())
			SetPreviewFromItem(entryIndex);
		return;
	}

	// Hover only counts when the pointer actually moved this frame.
	// Without that test, a mouse parked over one tile would fight the
	// D-pad for the panel on every single frame.  The drag test stops a
	// touch-scroll from riffling the panel through every game the finger
	// slides across.
	const ImGuiIO &io = ImGui::GetIO();
	if (ImGui::IsItemHovered()
		&& (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)
		&& !ATTouchIsDraggingBeyondSlop())
	{
		SetPreviewFromItem(entryIndex);
	}
}

// Long-press / Gamepad-Y / F2 / right-click ask for more about a game.
// A plain tap still boots, so the fast path nobody wants to lose is
// untouched.
//
// Press-and-hold is measured per tile rather than globally: two tiles
// can never be held at once, but a hold that slides off its tile must
// not fire on whatever ends up under the finger.
static ImGuiID s_holdItemId = 0;
static float   s_holdTime = 0.0f;
static const float kLongPressSeconds = 0.5f;

enum class DetailsGesture {
	None,
	// Long-press while a preview panel is docked.  The panel is already
	// the answer to "tell me more", so moving it is both cheaper and
	// less disorienting than a full screen change.
	Highlight,
	// No panel to move (phone-sized screen), or an explicit request —
	// open the full-screen details sheet.
	OpenSheet,
};

static void OpenGameDetails(ATMobileUIState &mobileState, size_t entryIndex) {
	GameDetails_Open((int)entryIndex);
	mobileState.currentScreen = ATMobileUIScreen::GameDetails;
}

// Call immediately after the tile/row Selectable.
static DetailsGesture ConsumeDetailsGesture() {
	const ImGuiID id = ImGui::GetItemID();

	// Gamepad Y / keyboard F2 on the focused tile.  This is also how a
	// gamepad user reaches the panel's actions, since the panel itself
	// is NoNav by design — so it always opens the sheet, never merely
	// re-highlights what is already highlighted.
	if (ImGui::IsItemFocused()
		&& (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp, false)
			|| ImGui::IsKeyPressed(ImGuiKey_F2, false)))
	{
		return DetailsGesture::OpenSheet;
	}

	// Right-click is the mouse equivalent — the same affordance desktop
	// users expect from the Game Library table.
	if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
		return DetailsGesture::OpenSheet;

	DetailsGesture result = DetailsGesture::None;

	if (ImGui::IsItemActive() && !ATTouchIsDraggingBeyondSlop()) {
		if (s_holdItemId != id) {
			s_holdItemId = id;
			s_holdTime = 0.0f;
		}
		s_holdTime += ImGui::GetIO().DeltaTime;
		if (s_holdTime >= kLongPressSeconds) {
			// Clear the active item so the release does not also
			// register as a tap and boot the game we just opened.
			ImGui::ClearActiveID();
			s_holdItemId = 0;
			s_holdTime = 0.0f;
			result = (s_previewDock == ATGamePreviewDock::None)
				? DetailsGesture::OpenSheet
				: DetailsGesture::Highlight;

			// A highlight makes no screen transition, so without a pulse
			// the user has no confirmation that the hold registered.
			if (result == DetailsGesture::Highlight
				&& g_mobileState.layoutConfig.hapticEnabled)
			{
				ATAndroid_Vibrate(15);
			}
		}
	} else if (s_holdItemId == id) {
		s_holdItemId = 0;
		s_holdTime = 0.0f;
	}

	return result;
}

// Apply a gesture result to one entry.  Shared by the tile and row
// renderers so the two can never diverge.
static void ApplyDetailsGesture(ATMobileUIState &mobileState,
	DetailsGesture gesture, size_t entryIndex)
{
	switch (gesture) {
		case DetailsGesture::Highlight:
			SetPreviewFromItem(entryIndex);
			break;
		case DetailsGesture::OpenSheet:
			SetPreviewFromItem(entryIndex);
			OpenGameDetails(mobileState, entryIndex);
			break;
		default:
			break;
	}
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
	// Latch: when the bool flips false → true, kick the popup open.
	// Same pattern as desktop ui_disk.cpp:RenderDiskVariantPopup —
	// keeps the public API (s_variantPickerOpen + Show*ForSwap)
	// untouched while routing the actual rendering through ImGui's
	// modal-popup machinery.  The modal blocks input behind it so
	// the picker cannot bleed onto the next screen if the user
	// cancels by tapping a UI element underneath.
	static bool prevOpen = false;
	const char *kPopupId = "##MobileVariantPicker";

	if (!s_variantPickerOpen || !s_gameLibrary) {
		// Clean up any stale swap callback if the picker was closed
		// by external state (library shutdown, etc.).
		if (!s_variantPickerOpen) {
			s_variantPickerSwapCb = nullptr;
			prevOpen = false;
		}
		return;
	}

	auto &entries = s_gameLibrary->GetEntries();
	if (s_variantPickerEntry >= entries.size()) {
		s_variantPickerOpen = false;
		s_variantPickerSwapCb = nullptr;
		prevOpen = false;
		return;
	}

	if (!prevOpen) {
		ImGui::OpenPopup(kPopupId);
		prevOpen = true;
	}

	auto &entry = entries[s_variantPickerEntry];
	VDStringA titleU8 = VDTextWToU8(entry.mDisplayName);

	ImGui::SetNextWindowSize(ImVec2(dp(360.0f), 0),
		ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	bool keepOpen = true;
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_AlwaysAutoResize;

	if (ImGui::BeginPopupModal(kPopupId, &keepOpen, flags)) {
		bool popupClosed = false;
		auto closePopup = [&]() {
			s_variantPickerOpen = false;
			s_variantPickerSwapCb = nullptr;
			prevOpen = false;
			popupClosed = true;
			ImGui::CloseCurrentPopup();
		};

		if (!s_confirmActive && !s_infoModalOpen) {
			bool back = ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false);
			if (!ImGui::IsAnyItemActive()) {
				back = back
					|| ImGui::IsKeyPressed(ImGuiKey_Escape, false)
					|| ImGui::IsKeyPressed(ImGuiKey_Backspace, false);
			}
			if (back)
				closePopup();
		}

		if (!popupClosed) {
			// Render the entry name as the body header — the popup ID is
			// hidden ("##..."), so we draw the human-readable title here.
			ImGui::SetWindowFontScale(1.1f);
			ImGui::TextUnformatted(titleU8.c_str());
			ImGui::SetWindowFontScale(1.0f);
			ImGui::Separator();
			ImGui::Spacing();

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
						VDStringW path = var.mPath;
						closePopup();
						cb(path);
					} else {
						LaunchGame(sim, mobileState,
							s_variantPickerEntry, i);
						closePopup();
					}
				}
				if (i == 0)
					ImGui::SetItemDefaultFocus();
				if (popupClosed)
					break;
			}

			if (!popupClosed) {
				ImGui::Spacing();
				if (ATTouchButton("Cancel", ImVec2(-1, dp(40.0f)),
					ATTouchButtonStyle::Subtle))
				{
					closePopup();
				}
			}
		}

		ImGui::EndPopup();
	}

	// keepOpen flips false when the user clicks the X in the popup
	// titlebar; ImGui has already closed the popup internally, so
	// just sync our tracking flags + drop any pending swap callback.
	// IsPopupOpen catches the ESC dismiss path which BeginPopupModal
	// also handles internally without touching keepOpen.
	if (!keepOpen || !ImGui::IsPopupOpen(kPopupId)) {
		s_variantPickerOpen = false;
		s_variantPickerSwapCb = nullptr;
		prevOpen = false;
	}
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
	size_t entryIndex, float tileW, float tileH, bool recentlyPlayed)
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
	const DetailsGesture gesture = ConsumeDetailsGesture();
	UpdatePreviewFromItem(entryIndex);
	ImGui::PopStyleColor(3);
	ApplyDetailsGesture(mobileState, gesture, entryIndex);

	bool hovered = ImGui::IsItemHovered() || ImGui::IsItemFocused();
	const bool highlighted = IsPreviewHighlighted(entryIndex, recentlyPlayed);

	const ATMobilePalette &pal = ATMobileGetPalette();
	ImVec2 imgTL = cursor;
	ImVec2 imgBR(cursor.x + tileW, cursor.y + imageH);

	// Try to show game art; fall back to color placeholder
	int artW = 0, artH = 0;
	ImTextureID artTex = (ImTextureID)0;
	// GetTileArtPath applies the art precedence: user-set custom art
	// beats downloaded metadata media, which beats scanner-matched art.
	const VDStringW artPath = s_gameLibrary->GetTileArtPath(entry);
	if (s_artCache && !artPath.empty())
		artTex = s_artCache->GetTexture(artPath, &artW, &artH);

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

	// Metadata badge (bottom-right).  Drawn only when the entry has
	// real metadata, so it doubles as the "there is more to see here"
	// affordance that makes the long-press discoverable.
	if (entry.mMeta.mStatus == GameMetaStatus::Matched
		|| entry.mMeta.mStatus == GameMetaStatus::UserEdited)
	{
		const char *glyph = "i";
		const ImVec2 gs = ImGui::CalcTextSize(glyph);
		const float badgePad = dp(4.0f);
		dl->AddRectFilled(
			ImVec2(imgBR.x - gs.x - badgePad * 2,
				imgBR.y - gs.y - badgePad * 2),
			ImVec2(imgBR.x, imgBR.y),
			IM_COL32(0, 0, 0, 180), dp(4.0f));
		dl->AddText(
			ImVec2(imgBR.x - gs.x - badgePad, imgBR.y - gs.y - badgePad),
			IM_COL32(255, 255, 255, 220), glyph);
	}

	// Selection / hover ring.
	//
	// One ring, not two.  With a gamepad, focus and highlight are always
	// the same tile, so drawing both would double-stroke every focused
	// item; the highlight ring simply takes precedence.  It wraps the
	// whole tile, label included, because "this card is selected" is
	// what it has to communicate, and a card ringed round only its top
	// half looks unfinished.
	if (highlighted) {
		const ImVec2 tileTL(cursor.x, cursor.y);
		const ImVec2 tileBR(cursor.x + tileW, cursor.y + totalH);

		// The accent wash goes on the label strip only.  This draw list
		// is immediate-mode and the artwork is already down, so a fill
		// spanning the whole tile would tint the cover instead of
		// sitting behind it — the label area is the one part that is
		// still empty background at this point.  The name is drawn
		// after, so it lands on top of the wash.
		dl->AddRectFilled(ImVec2(tileTL.x, cursor.y + imageH), tileBR,
			pal.accentSoft, dp(4.0f));

		// Ring thickness is centred on the edge, so a 2dp stroke bleeds
		// 1dp outside the tile — comfortably inside the 8dp gutter
		// between tiles, and it cannot collide with the next row's ring.
		dl->AddRect(tileTL, tileBR, pal.accent, dp(4.0f), 0, dp(2.0f));
	} else if (hovered) {
		dl->AddRect(imgTL, imgBR, pal.rowFocus, dp(4.0f), 0, dp(2.0f));
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
	size_t entryIndex, float rowH, bool showTime)
{
	auto &entries = s_gameLibrary->GetEntries();
	if (entryIndex >= entries.size())
		return;

	const auto &entry = entries[entryIndex];
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
	const DetailsGesture gesture = ConsumeDetailsGesture();
	UpdatePreviewFromItem(entryIndex);
	ApplyDetailsGesture(mobileState, gesture, entryIndex);

	// Draw the row content on top of the selectable
	ImDrawList *dl = ImGui::GetWindowDrawList();
	const ATMobilePalette &pal = ATMobileGetPalette();

	// Selection band.  Drawn first so everything below lands on top of
	// it; a list row has no spare margin for a ring, so it gets a filled
	// band plus an accent edge instead.
	if (IsPreviewHighlighted(entryIndex, showTime))
	{
		dl->AddRectFilled(cursor,
			ImVec2(cursor.x + availW, cursor.y + rowH),
			pal.accentSoft, dp(4.0f));
		dl->AddRectFilled(cursor,
			ImVec2(cursor.x + dp(3.0f), cursor.y + rowH),
			pal.accent, dp(2.0f));
	}

	float thumbSize = rowH - dp(8.0f);
	float thumbX = cursor.x + dp(4.0f);
	float thumbY = cursor.y + dp(4.0f);
	float nameStartX;

	int artW = 0, artH = 0;
	ImTextureID artTex = (ImTextureID)0;
	// GetTileArtPath applies the art precedence: user-set custom art
	// beats downloaded metadata media, which beats scanner-matched art.
	const VDStringW artPath = s_gameLibrary->GetTileArtPath(entry);
	if (s_artCache && !artPath.empty())
		artTex = s_artCache->GetTexture(artPath, &artW, &artH);

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

	// Apply any metadata the scraper's workers finished since the last
	// frame.  Same main-thread contract as ConsumeScanResults below.
	if (ATMetadataGetScraper().ConsumeResults(*s_gameLibrary, s_artCache))
		s_needsRefresh = true;

	// Check for completed background scan
	if (s_gameLibrary->IsScanComplete()) {
		s_gameLibrary->ConsumeScanResults();
		ComputeAvailableLetters();
		s_needsRefresh = true;
		s_variantPickerOpen = false;
		if (s_artCache)
			s_artCache->Clear();
	}

	const bool rebuiltIndices = s_needsRefresh;
	if (s_needsRefresh) {
		ComputeAvailableLetters();
		RebuildFilteredIndices();
	}

	// Keep the highlight pointing at something the user can actually
	// see.  A filter change, a letter jump or a rescan can drop the
	// highlighted entry out of the visible set, and a preview panel
	// describing a game that is not on screen is worse than an empty
	// one.  The full scan only runs on the frames the list changed; every
	// other frame just needs the bounds check, because entries can shrink
	// underneath us when a scan lands.
	if (rebuiltIndices) {
		s_previewItem = 0;
		bool visible = false;
		for (size_t idx : s_allGamesIndices) {
			if ((int)idx == s_previewEntry) { visible = true; break; }
		}
		if (!visible) {
			s_previewEntry = s_allGamesIndices.empty()
				? -1 : (int)s_allGamesIndices[0];
		}
	} else if (s_previewEntry >= (int)s_gameLibrary->GetEntries().size()) {
		s_previewEntry = -1;
		s_previewItem = 0;
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

		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const float panelGap = dp(10.0f);
		const ATGamePreviewDock dockCapable =
			GamePreview_ComputeDock(avail.x, avail.y);
		s_previewDock = s_gameLibrary->GetSettings().mbShowDetailsPanel
			&& !s_pickerMode && !s_gameLibrary->GetEntries().empty()
			? dockCapable : ATGamePreviewDock::None;
		const bool split = s_previewDock != ATGamePreviewDock::None;
		const bool side = s_previewDock == ATGamePreviewDock::Side;
		const bool compactLandscape = !s_pickerMode
			&& avail.x >= avail.y && avail.y < dp(420.0f);
		const ImGuiID actionsPopup = ImGui::GetID("##libraryActions");
		static int lastActionsFrame = -2;
		const int frame = ImGui::GetFrameCount();
		// ImGui can dismiss a popup on Back before rendering this frame.
		const bool actionsOwnInput = ImGui::IsPopupOpen(actionsPopup, 0)
			|| lastActionsFrame == frame - 1;
		const ImVec2 previewSize(
			side ? (avail.x - panelGap) * 0.5f : avail.x,
			split && !side ? (avail.y - panelGap) * 0.5f : avail.y);
		if (split) {
			ImGui::BeginChild("##Library", previewSize,
				ImGuiChildFlags_NavFlattened);
		}

		// ── Gamepad / keyboard back navigation ───────────────────
		// ESC / B / Backspace: context-dependent.
		// If search or variant picker is active, close it.
		// If a game was running, return to it.
		// Otherwise this IS the home screen — on Android we minimize
		// the activity to the launcher (the Android idiom for "Back
		// on root"), which keeps the process alive in the background
		// so the user can resume from the task switcher.  On desktop
		// there is no equivalent gesture, so Back on root is a no-op
		// and the user closes the window through normal OS chrome.
		//
		// When the letter picker modal is open it owns the back key —
		// we must not also process it here, otherwise a single ESC
		// press would close the modal AND pop the browser.
		bool backPressed = false;
		if (!actionsOwnInput && !s_confirmActive && !s_infoModalOpen
			&& !s_letterPickerOpen)
		{
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
			} else {
#ifdef __ANDROID__
				// Root Game Library screen with no game: ask for a clean
				// emulator exit instead of silently backgrounding the app.
				ATMobileUI_ShowExitEmulatorConfirm(sim, uiState, mobileState);
#endif
			}
		}

		const ATMobilePalette &palBrowser = ATMobileGetPalette();
		const auto &libSettings = s_gameLibrary->GetSettings();
		const bool compactHeader = split
			&& ImGui::GetContentRegionAvail().x < dp(420.0f);
		const float btnH = dp(compactHeader ? 38.0f : 48.0f);
		const bool isGrid = libSettings.mViewMode == 1;
		const bool panelOn = libSettings.mbShowDetailsPanel;

		auto openSettings = [&] {
			s_settingsPage = ATMobileSettingsPage::Home;
			s_settingsReturnScreen = ATMobileUIScreen::GameBrowser;
			mobileState.currentScreen = ATMobileUIScreen::Settings;
		};
#ifndef __ANDROID__
		auto exitGamingMode = [&] {
			ATUISetMode(ATUIMode::Desktop);
			ATUISaveMode();
			float cs = SDL_GetDisplayContentScale(
				SDL_GetDisplayForWindow(window));
			ATUIApplyModeStyle(std::clamp(cs, 1.0f, 4.0f));
		};
#endif
		auto bootGame = [&] {
			s_romFolderMode = false;
			mobileState.currentScreen = ATMobileUIScreen::FileBrowser;
			s_fileBrowserNeedsRefresh = true;
		};
		auto bootEmpty = [&] {
			mobileState.gameLoaded = true;
			mobileState.currentScreen = ATMobileUIScreen::None;
			sim.ColdReset();
			sim.Resume();
		};
		auto setViewMode = [&](int mode) {
			GameLibrarySettings settings = libSettings;
			settings.mViewMode = mode;
			s_gameLibrary->SetSettings(settings);
			s_gameLibrary->SaveSettingsToRegistry();
		};
		auto toggleDetails = [&] {
			GameLibrarySettings settings = libSettings;
			settings.mbShowDetailsPanel = !panelOn;
			s_gameLibrary->SetSettings(settings);
			s_gameLibrary->SaveSettingsToRegistry();
			ATRegistryFlushToDisk();
		};

		// The picker has its own header and never offers boot actions.

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
		} else if (!compactLandscape) {
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

			char info[160];
			snprintf(info, sizeof info, "ALTIRRA  %s  (%d game%s)",
				hwInfo.c_str(), gameCount, gameCount == 1 ? "" : "s");

			ImGui::TextColored(ATMobileCol(palBrowser.textMuted), "%s", info);
			SameLineIfFits(chromeW, dp(8.0f));
			ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - chromeW);
			if (ATTouchButton("Settings##chrome", ImVec2(settingsW, chromeH)))
				openSettings();
#ifndef __ANDROID__
			ImGui::SameLine(0, dp(6.0f));
			if (ATTouchButton("Exit Gaming Mode##chrome", ImVec2(exitW, chromeH)))
				exitGamingMode();
#endif
		}

		if (!s_pickerMode) {
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

			// Metadata download status.  Sits with the scan status
			// because both answer the same question: "is the library
			// still changing under me?"  The Cancel button is inline so
			// a long run is always one tap from stopping.
			{
				ATMetadataScraper &scraper = ATMetadataGetScraper();
				if (scraper.IsRunning()) {
					ImGui::TextColored(ATMobileCol(palBrowser.textSection),
						"Downloading metadata  %d / %d   (%d found, %d missing)",
						scraper.GetDone(), scraper.GetTotal(),
						scraper.GetMatched(), scraper.GetNotFound());
					ImGui::SameLine(0, dp(12.0f));
					if (ATTouchButton("Cancel##meta",
						ImVec2(0, dp(28.0f)), ATTouchButtonStyle::Subtle))
					{
						scraper.Cancel();
					}
				} else {
					const VDStringA banner = scraper.GetBanner();
					if (!banner.empty()) {
						ImGui::PushTextWrapPos(0.0f);
						ImGui::TextColored(ATMobileCol(palBrowser.warning),
							"%s", banner.c_str());
						ImGui::PopTextWrapPos();
						if (ATTouchButton("Dismiss##metabanner",
							ImVec2(0, dp(28.0f)), ATTouchButtonStyle::Subtle))
						{
							scraper.ClearBanner();
						}
					}
				}
			}
		}

		// Short landscape layouts put launch and app actions behind More.
		if (!s_pickerMode && !compactLandscape) {
			const float gap = ImGui::GetStyle().ItemSpacing.x;
#ifdef ALTIRRA_NETPLAY_ENABLED
			constexpr int kLaunchButtons = 3;
#else
			constexpr int kLaunchButtons = 2;
#endif
			const float btnW = compactHeader
				? std::floor((ImGui::GetContentRegionAvail().x
					- gap * (kLaunchButtons - 1)) / kLaunchButtons)
				: 0.0f;
			if (ATTouchButton("Boot Game", ImVec2(btnW, btnH),
				ATTouchButtonStyle::Accent, ICON_MD_PLAY_ARROW))
			{
				bootGame();
			}

#ifdef ALTIRRA_NETPLAY_ENABLED
			SameLineIfFits(btnW > 0 ? btnW
				: ImGui::CalcTextSize("Online Play").x + dp(56.0f), gap);
			if (ATTouchButton("Online Play", ImVec2(btnW, btnH),
				ATTouchButtonStyle::Neutral, ICON_MD_PUBLIC)) {
				// Go to My Hosted Games first — that's the primary
				// entry point for the offer-list UX.  The browser is
				// reachable from there with one tap.
				ATNetplayUI_OpenMyHostedGames();
			}
#endif

			SameLineIfFits(btnW > 0 ? btnW
				: ImGui::CalcTextSize("Boot Empty").x + dp(56.0f), gap);
			if (ATTouchButton("Boot Empty", ImVec2(btnW, btnH),
				ATTouchButtonStyle::Neutral, ICON_MD_POWER_SETTINGS_NEW)) {
				bootEmpty();
			}
		}

		// Keep the filters one tap away while browsing.
		static bool s_focusLetterBar = false;
		bool letterBarHasFocus = false;
		{
			if (!compactLandscape) {
				const float viewW = dp(56.0f);
				const float viewH = dp(36.0f);
				if (ATTouchButton("Grid##view", ImVec2(viewW, viewH),
					isGrid ? ATTouchButtonStyle::Accent
					       : ATTouchButtonStyle::Neutral))
				{
					if (!isGrid) setViewMode(1);
				}
				if (ImGui::IsItemFocused())
					letterBarHasFocus = true;
				ImGui::SameLine(0, dp(2.0f));
				if (ATTouchButton("List##view", ImVec2(viewW, viewH),
					!isGrid ? ATTouchButtonStyle::Accent
					        : ATTouchButtonStyle::Neutral))
				{
					if (isGrid) setViewMode(0);
				}
				if (ImGui::IsItemFocused())
					letterBarHasFocus = true;
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
			{
				const float pillW = ImGui::CalcTextSize("Letter: All").x + dp(32.0f);
				if (!compactLandscape)
					SameLineIfFits(pillW, dp(8.0f));
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

			// Details panel toggle.  Hidden entirely on screens too
			// small to dock a panel, where it could only ever be a
			// no-op — those screens reach the same information through
			// the full-screen sheet instead.
			if (!compactLandscape && !s_pickerMode
				&& dockCapable != ATGamePreviewDock::None)
			{
				const float infoW = ImGui::CalcTextSize("Details").x
					+ dp(28.0f);
				SameLineIfFits(infoW, dp(8.0f));
				if (ATTouchButton("Details##infopill",
					ImVec2(infoW, dp(36.0f)),
					panelOn ? ATTouchButtonStyle::Accent
					        : ATTouchButtonStyle::Neutral))
				{
					toggleDetails();
				}
				// Row 3 sits in the parent window, directly above the
				// non-flattened game list.  Every widget here that can
				// be the last focusable thing before the list MUST feed
				// letterBarHasFocus, or D-pad Down would simply stick.
				if (ImGui::IsItemFocused())
					letterBarHasFocus = true;
			}

			// Leave room for More alongside the active search field.
			static bool s_focusSearchInput = false;
			const float gap = dp(8.0f);
			const float moreW = ImGui::CalcTextSize("More").x + dp(22.0f);
			const float searchMin = dp(compactLandscape ? 64.0f : 80.0f);
			SameLineIfFits(s_searchActive ? searchMin
				: ImGui::CalcTextSize("Search").x + dp(32.0f), gap);
			const float searchBtnSz = dp(36.0f);
			if (s_searchActive) {
				if (s_focusSearchInput) {
					ImGui::SetKeyboardFocusHere(0);
					s_focusSearchInput = false;
				}
				float searchW = ImGui::GetContentRegionAvail().x;
				if (compactLandscape && searchW >= searchMin + gap + moreW)
					searchW -= gap + moreW;
				searchW = std::max(searchW, searchMin);
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

				if (!actionsOwnInput && !s_confirmActive && !s_infoModalOpen
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

				if (!actionsOwnInput && !s_variantPickerOpen && !s_letterPickerOpen
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

			if (compactLandscape) {
				SameLineIfFits(moreW, gap);
				if (ATTouchButton("More##libraryActions",
					ImVec2(moreW, searchBtnSz)))
					ImGui::OpenPopup(actionsPopup);
				if (ImGui::IsItemFocused())
					letterBarHasFocus = true;
			}
		}

		// Use the browser's popup ID even when the library is in a child.
		// A resize can move the toolbar into or out of that child.
		if (ImGui::IsPopupOpen(actionsPopup, 0)) {
			const float popupW = std::min(dp(240.0f), avail.x);
			ImGui::SetNextWindowSizeConstraints(ImVec2(popupW, 0),
				ImVec2(popupW, std::max(dp(60.0f), avail.y - dp(48.0f))));
			const ImGuiWindowFlags popupFlags = ImGuiWindowFlags_AlwaysAutoResize
				| ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings;
			if (ImGui::BeginPopupEx(actionsPopup, popupFlags))
			{
				lastActionsFrame = frame;
				if (compactLandscape
					&& !ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
				{
					ATTouchDragScroll();
					auto item = [](const char *label) {
						if (!ATTouchButton(label, ImVec2(-1, dp(36.0f)),
							ATTouchButtonStyle::Subtle))
							return false;
						ImGui::CloseCurrentPopup();
						return true;
					};

					if (item("Boot Game"))
						bootGame();
#ifdef ALTIRRA_NETPLAY_ENABLED
					if (item("Online Play"))
						ATNetplayUI_OpenMyHostedGames();
#endif
					if (item("Boot Empty"))
						bootEmpty();
					if (item("Settings"))
						openSettings();
					if (item(isGrid ? "Switch to List" : "Switch to Grid"))
						setViewMode(isGrid ? 0 : 1);
					if (dockCapable != ATGamePreviewDock::None
						&& item(panelOn ? "Hide Details" : "Show Details")) {
						toggleDetails();
						s_focusLetterBar = true;
					}
#ifndef __ANDROID__
					if (item("Exit Gaming Mode"))
						exitGamingMode();
#endif
					ImGui::Separator();
					ATTouchMutedText(BuildHardwareInfoStr(sim).c_str());
					ATTouchEndDragScroll();
				} else {
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
		}

		ImGui::Separator();

		// ── Empty state ──────────────────────────────────────────
		if (s_gameLibrary->GetSources().empty()
			&& s_gameLibrary->GetEntries().empty()
			&& !s_gameLibrary->IsScanning())
		{
			float windowW = ImGui::GetWindowWidth();
			float windowH = ImGui::GetWindowHeight();
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
			ImGui::SetItemDefaultFocus();

			ImGui::SetCursorPos(ImVec2(centerX - btnSize.x * 0.5f,
				centerY + dp(160.0f)));
			if (ATTouchButton("Boot Atari without game", btnSize)) {
				mobileState.gameLoaded = true;
				mobileState.currentScreen = ATMobileUIScreen::None;
				g_sim.ColdReset();
				g_sim.Resume();
			}

			if (split)
				ImGui::EndChild();
			ImGui::End();
			return;
		}

		// ── First-run metadata nudge ─────────────────────────────
		// Shown once, ever, and only when it would actually help: the
		// library has games, none of them have metadata, downloading is
		// possible, and nothing is already running.  Dismissal is
		// sticky so this never becomes a recurring interruption.
		{
			ATMetadataSettings &mds = ATMetadataGetSettings();
			VDStringA whyNotNudge;
			// Leave room for the list on short handhelds. Metadata is
			// still available from Settings and the details pane.
			const int missingMeta = (!compactLandscape
				&& ImGui::GetContentRegionAvail().y >= dp(230.0f)
				&& !mds.mbFirstRunNudgeShown
				&& !s_gameLibrary->IsScanning()
				&& !ATMetadataGetScraper().IsRunning()
				&& ATUIMetadataIsUsable(whyNotNudge))
				? ATMetadataCountEntries(*s_gameLibrary, true)
				: 0;

			// Any missing game is worth offering for, not just a
			// library where every single one is missing: the common case
			// after adding a folder is a mostly-scraped library with a
			// handful of blanks, and that is exactly when the offer is
			// most useful and least intrusive.  Dismissal is still
			// sticky, and Settings > Metadata remains the permanent home
			// of the same two actions.
			if (missingMeta > 0) {
				ImGui::Dummy(ImVec2(0, dp(4.0f)));
				char msg[192];
				snprintf(msg, sizeof msg,
					"%d game%s ha%s no cover art yet. "
					"Download covers and descriptions?",
					missingMeta, missingMeta == 1 ? "" : "s",
					missingMeta == 1 ? "s" : "ve");
				ImGui::PushTextWrapPos(0.0f);
				ImGui::TextColored(ATMobileCol(palBrowser.textSection),
					"%s", msg);
				ImGui::PopTextWrapPos();

				// These sit in the header (parent window), directly
				// above the game-list child.  The child is NOT
				// NavFlattened — deliberately, so PgUp/PgDown stay
				// confined to the list — which means ImGui will not
				// walk D-pad focus into it on its own.  Every header
				// widget that can be the last focusable thing before
				// the list must therefore feed `letterBarHasFocus`,
				// which is what arms the explicit header->list
				// transfer below.  Without this, focusing a nudge
				// button and pressing Down would simply stick.
				const float nudgeH = dp(40.0f);
				if (ATTouchButton("Not now##nudge", ImVec2(0, nudgeH),
					ATTouchButtonStyle::Subtle))
				{
					mds.mbFirstRunNudgeShown = true;
					ATMetadataSaveSettings();
				}
				if (ImGui::IsItemFocused())
					letterBarHasFocus = true;

				ImGui::SameLine(0, dp(8.0f));
				char dlLabel[64];
				snprintf(dlLabel, sizeof dlLabel,
					"Get metadata (%d)##nudge", missingMeta);
				if (ATTouchButton(dlLabel, ImVec2(0, nudgeH),
					ATTouchButtonStyle::Accent, ICON_MD_CLOUD_DOWNLOAD))
				{
					mds.mbFirstRunNudgeShown = true;
					ATMetadataSaveSettings();
					ATUIMetadataStartDownload(*s_gameLibrary, true);
				}
				if (ImGui::IsItemFocused())
					letterBarHasFocus = true;
				ImGui::Dummy(ImVec2(0, dp(4.0f)));
			}
		}

		// ── Scrollable game content ──────────────────────────────
		// No NavFlattened — keeps PgUp/PgDown confined to the
		// list.  Header→list: only from the letter bar (last row).
		// List→header: ImGui handles naturally via cross-window
		// nav scoring (child→parent).
		static bool s_focusGameList = false;
		if (letterBarHasFocus
			&& !actionsOwnInput && !s_variantPickerOpen
			&& NavDownPressed() && ImGui::NavMoveRequestButNoResultYet())
		{
			s_focusGameList = true;
		}

		const float listH = split
			? std::max(dp(96.0f), ImGui::GetContentRegionAvail().y) : 0.0f;
		ImGui::BeginChild("##GameList", ImVec2(0, listH));
		ImGuiWindow *listWindow = ImGui::GetCurrentWindow();
		ATTouchDragScroll();

		if (s_focusGameList) {
			ImGui::FocusWindow(ImGui::GetCurrentWindow());
			ImGui::SetKeyboardFocusHere(0);
			s_focusGameList = false;
		}

		// The right stick scrolls independently of focus. Confirm first
		// reveals a focused game that has scrolled completely out of view.
		ImGuiContext &nav = *GImGui;
		if (nav.NavWindow == listWindow && nav.NavId
			&& nav.NavActivateId == nav.NavId)
		{
			const ImRect focused = ImGui::WindowRectRelToAbs(
				listWindow, listWindow->NavRectRel[nav.NavLayer]);
			if (!listWindow->ClipRect.Overlaps(focused)) {
				ImGui::ScrollToRectEx(listWindow, focused, ImGuiScrollFlags_AlwaysCenterY);
				nav.NavActivateId = nav.NavActivateDownId = nav.NavActivatePressedId = 0;
			}
		}

		bool gridMode = libSettings.mViewMode == 1;
		static ImGuiID previousList = 0;
		static bool previousGridMode = false;
		if (previousList != listWindow->ID || previousGridMode != gridMode) {
			s_previewItem = 0;
			previousList = listWindow->ID;
			previousGridMode = gridMode;
		}

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
						s_lastPlayedIndices[i], tileW, tileH, true);
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
								s_allGamesIndices[idx], tileW, tileH, false);
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
					RenderGameRow(sim, mobileState, idx, rowH, true);

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
							s_allGamesIndices[i], rowH, false);
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
				if (g.NavWindow == ImGui::GetCurrentWindow() && g.NavId == s_navIdOnUp
					&& fabsf(scrollNow - s_scrollOnUp) < 1.0f)
				{
					s_focusLetterBar = true;
				}
			}

			if (g.NavWindow == ImGui::GetCurrentWindow() && NavUpPressed()
				&& !s_searchActive && !s_variantPickerOpen)
			{
				s_navIdOnUp = g.NavId;
				s_scrollOnUp = ImGui::GetScrollY();
				s_checkUpExit = true;
			}
		}

		ATTouchEndDragScroll();
		ImGui::EndChild();

		// Shoulder buttons cycle the artwork the library shows.
		//
		// The panel's own left/right arrows are mouse- and touch-only by
		// construction: the panel is NoNav so it cannot steal D-pad
		// focus from the grid.  That would leave a controller with no way
		// to reach the switch at all, so L1/R1 do it directly.  They are
		// unbound elsewhere in this screen, and "shoulder buttons flip
		// the view" is the console idiom.
		if (s_previewDock != ATGamePreviewDock::None
			&& !actionsOwnInput && !s_variantPickerOpen && !s_letterPickerOpen
			&& !s_confirmActive && !s_infoModalOpen)
		{
			if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false))
				ATUIMetadataCycleArtSlot(-1);
			else if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false))
				ATUIMetadataCycleArtSlot(1);
		}

		// X (or F5) fetches metadata for the highlighted game.
		//
		// A launches and Y opens the details sheet, which left no way to
		// ask for a retry on a game whose automatic attempt failed
		// without navigating into the sheet to press a button there.  It
		// is a retry affordance more than a primary action — with
		// automatic fetching on, most games never need it.
		if (!actionsOwnInput && !s_variantPickerOpen && !s_letterPickerOpen
			&& !s_confirmActive && !s_infoModalOpen && !s_searchActive
			&& s_previewEntry >= 0
			&& (size_t)s_previewEntry < s_gameLibrary->GetEntries().size())
		{
			if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft, false)
				|| ImGui::IsKeyPressed(ImGuiKey_F5, false))
			{
				VDStringA whyNotFetch;
				if (ATUIMetadataCanDownloadNow(whyNotFetch)) {
					ATUIMetadataStartDownloadForEntry(*s_gameLibrary,
						s_previewEntry);
				} else if (!whyNotFetch.empty()) {
					ATTouchPushToast(whyNotFetch.c_str(),
						ATTouchToastSeverity::Warning);
				}
			}
		}

		if (split) {
			ImGui::EndChild();
			if (side)
				ImGui::SameLine(0, panelGap);
			else
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + panelGap
					- ImGui::GetStyle().ItemSpacing.y);
			GamePreview_Render(sim, mobileState, s_previewEntry,
				s_previewDock, previewSize);

			// Clicking the empty area or scrollbar can still focus the
			// preview window. Restore the list's last navigation target.
			ImGuiWindow *navWindow = GImGui->NavWindow;
			if (navWindow && navWindow->RootWindow == listWindow->RootWindow
				&& (navWindow->Flags & ImGuiWindowFlags_NoNavInputs))
				ImGui::FocusWindow(listWindow);
		}
	}
	ImGui::End();

	// Overlays — variant picker is rendered globally by
	// GameBrowser_RenderOverlays (called from ui_mobile.cpp so the
	// swap-mode picker works from other screens too).  Letter picker
	// is game-browser-specific and stays here.
	RenderLetterPickerModal(io, mobileState);
}
