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
#include "ui_file_dialog_sdl3.h"

// External glue — defined elsewhere in the SDL3 front-end.
extern VDStringA ATGetConfigDir();
extern void ATRegistryFlushToDisk();
extern void GameBrowser_Init();
extern ATGameLibrary *GetGameLibrary();

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
char                g_filterBuf[128]   = {};
int                 g_sortColumn       = 0;     // 0 Name 1 Type 2 LastPlayed 3 Plays
bool                g_sortDescending   = false;
bool                g_openVariantPopup = false;
bool                g_openClearLibPopup = false;
bool                g_pendingClose     = false; // request dialog dismiss after a successful boot
size_t              g_variantEntryIdx  = 0;

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
	ATUIPushDeferred(kATDeferred_BootImage, pathU8.c_str(), 0);
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

// ----- Tab: Games ----------------------------------------------------------

void RenderTabGames(ATGameLibrary &lib) {
	// Top row: filter + status.
	ImGui::SetNextItemWidth(-180.0f);
	ImGui::InputTextWithHint("##filter", "Filter by name...", g_filterBuf,
		sizeof(g_filterBuf));

	ImGui::SameLine();
	if (lib.IsScanning()) {
		ImGui::TextColored(ImVec4(0.45f, 0.65f, 0.90f, 1.0f),
			"Scanning... (%d)", lib.GetScanProgress());
	} else {
		size_t n = lib.GetEntryCount();
		VDStringA ago = FormatAgo(lib.GetLastScanTime());
		if (!ago.empty())
			ImGui::Text("%zu games  ·  %s", n, ago.c_str());
		else
			ImGui::Text("%zu games", n);
	}

	// Table: Name | Type | Last Played | Plays
	if (ImGui::BeginTable("##GamesTbl", 5,
		ImGuiTableFlags_Borders      | ImGuiTableFlags_RowBg      |
		ImGuiTableFlags_Sortable     | ImGuiTableFlags_Resizable  |
		ImGuiTableFlags_ScrollY,
		ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 4.0f)))
	{
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Name",        ImGuiTableColumnFlags_WidthStretch, 0.55f, 0);
		ImGui::TableSetupColumn("Type",        ImGuiTableColumnFlags_WidthFixed,   90.0f, 1);
		ImGui::TableSetupColumn("Variants",    ImGuiTableColumnFlags_WidthFixed,   70.0f, 4);
		ImGui::TableSetupColumn("Last played", ImGuiTableColumnFlags_WidthFixed,   140.0f, 2);
		ImGui::TableSetupColumn("Plays",       ImGuiTableColumnFlags_WidthFixed,   60.0f, 3);
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
		std::vector<int> order;
		order.reserve(entries.size());
		for (size_t i = 0; i < entries.size(); ++i) {
			VDStringA nameU8 = VDTextWToU8(entries[i].mDisplayName);
			if (FilterMatch(nameU8, g_filterBuf))
				order.push_back((int)i);
		}

		std::sort(order.begin(), order.end(),
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
				}
				return g_sortDescending ? (cmp > 0) : (cmp < 0);
			});

		ImGuiListClipper clipper;
		clipper.Begin((int)order.size());
		while (clipper.Step()) {
			for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
				int idx = order[row];
				const GameEntry &e = entries[idx];
				ImGui::TableNextRow();
				ImGui::PushID(idx);

				// Name column — selectable spans full row.
				ImGui::TableSetColumnIndex(0);
				VDStringA nameU8 = VDTextWToU8(e.mDisplayName);
				bool selected = (g_selectedEntry == idx);
				if (ImGui::Selectable(nameU8.c_str(), selected,
					ImGuiSelectableFlags_SpanAllColumns |
					ImGuiSelectableFlags_AllowDoubleClick))
				{
					g_selectedEntry = idx;
					if (ImGui::IsMouseDoubleClicked(0)) {
						if (e.mVariants.size() > 1) {
							g_variantEntryIdx = (size_t)idx;
							g_openVariantPopup = true;
						} else if (!e.mVariants.empty()) {
							BootVariant(lib, (size_t)idx, 0);
						}
					}
				}

				ImGui::TableSetColumnIndex(1);
				GameMediaType t = e.mVariants.empty()
					? GameMediaType::Unknown : e.mVariants[0].mType;
				ImGui::TextUnformatted(MediaTypeLabel(t));

				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%zu", e.mVariants.size());

				ImGui::TableSetColumnIndex(3);
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
					ImGui::TextDisabled("—");
				}

				ImGui::TableSetColumnIndex(4);
				ImGui::Text("%u", e.mPlayCount);

				ImGui::PopID();
			}
		}

		ImGui::EndTable();
	}

	// Bottom row: Launch button.
	bool canLaunch = (g_selectedEntry >= 0)
		&& ((size_t)g_selectedEntry < lib.GetEntries().size())
		&& !lib.GetEntries()[g_selectedEntry].mVariants.empty();
	if (!canLaunch) ImGui::BeginDisabled();
	if (ImGui::Button("Launch", ImVec2(120, 0))) {
		const auto &e = lib.GetEntries()[g_selectedEntry];
		if (e.mVariants.size() > 1) {
			g_variantEntryIdx = (size_t)g_selectedEntry;
			g_openVariantPopup = true;
		} else {
			BootVariant(lib, (size_t)g_selectedEntry, 0);
		}
	}
	if (!canLaunch) ImGui::EndDisabled();
}

// ----- Tab: Sources --------------------------------------------------------

void RenderTabSources(ATGameLibrary &lib, SDL_Window *window) {
	const auto &srcRef = lib.GetSources();

	if (ImGui::BeginTable("##SourcesTbl", 3,
		ImGuiTableFlags_Borders    | ImGuiTableFlags_RowBg     |
		ImGuiTableFlags_Resizable  | ImGuiTableFlags_ScrollY,
		ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 4.0f)))
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
		SDL_ShowOpenFolderDialog(AddFolderCallback, nullptr, window,
			nullptr, false);
	}
#endif
	ImGui::SameLine();
	if (ImGui::Button("Add File or Archive...")) {
		ATUIShowOpenFileDialog('glib', AddFileCallback, nullptr, window,
			kAddFileFilters, 2, false);
	}
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

	ImGui::SetNextWindowSize(ImVec2(780, 520), ImGuiCond_Appearing);
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

	DrainPendingPicks(lib);

	// Tab bar.
	if (ImGui::BeginTabBar("##GameLibTabs")) {
		if (ImGui::BeginTabItem("Games"))   { RenderTabGames(lib);            ImGui::EndTabItem(); }
		if (ImGui::BeginTabItem("Sources")) { RenderTabSources(lib, window);  ImGui::EndTabItem(); }
		if (ImGui::BeginTabItem("Options")) { RenderTabOptions(lib);          ImGui::EndTabItem(); }
		ImGui::EndTabBar();
	}

	RenderVariantPopup(lib);
	RenderClearLibPopup(lib);

	ImGui::End();

	// A successful boot closes the dialog so the user sees the running
	// game.  Mirrors the Gaming-Mode browser, which navigates back to the
	// emulator screen after dispatch.
	if (g_pendingClose) {
		state.showGameLibrary = false;
		g_pendingClose = false;
	}
}
