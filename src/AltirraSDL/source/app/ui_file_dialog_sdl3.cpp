#include "ui_file_dialog_sdl3.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>
#include <imgui.h>
#include <vd2/system/filesys.h>
#include <vd2/system/text.h>
#include <vd2/system/VDString.h>
#include <vd2/Dita/services.h>
#include <at/atcore/configvar.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace {
	ATConfigVarBool g_ATCVForceNonNativeDialogs(
		"ui.force_non_native_dialogs", false);

	enum class DialogMode {
		OpenFile,
		SaveFile,
		OpenFolder
	};

	struct FallbackEntry {
		std::string name;
		std::string path;
		SDL_PathInfo info {};
		bool selected = false;
	};

	// For each semicolon-separated extension, add an uppercase variant so
	// that native file dialogs on Linux/macOS (which are case-sensitive)
	// also show files with uppercase extensions like .ATR or .XFD.
	// The "*" wildcard is passed through unchanged.
	std::string ExpandExtensionsCaseInsensitive(const char *pattern) {
		if (!pattern || !*pattern)
			return {};
		if (pattern[0] == '*' && pattern[1] == '\0')
			return "*";

		std::string result;
		const char *p = pattern;
		while (*p) {
			const char *semi = p;
			while (*semi && *semi != ';')
				++semi;

			std::string ext(p, semi);

			if (!result.empty())
				result += ';';
			result += ext;

			std::string upper = ext;
			bool different = false;
			for (char &c : upper) {
				if (c >= 'a' && c <= 'z') {
					c = (char)(c - 'a' + 'A');
					different = true;
				}
			}
			if (different) {
				result += ';';
				result += upper;
			}

			p = *semi ? semi + 1 : semi;
		}
		return result;
	}

	struct DialogContext {
		long					nKey;
		SDL_DialogFileCallback	userCb;
		void *					userUd;
		VDStringA				defaultLocationUtf8;	// keeps the c_str alive until SDL returns
		DialogMode				mode = DialogMode::OpenFile;
		bool					allowMany = false;
		int						selectedFilter = 0;

		// Expanded filter storage — keeps strings alive until callback fires
		std::vector<std::string>         expandedPatterns;
		std::vector<SDL_DialogFileFilter> expandedFilters;

		std::string				currentDir;
		std::string				editDir;
		std::string				saveName;
		std::string				search;
		bool					showHidden = false;
		bool					sortByModified = false;
		bool					sortAscending = true;
		bool					needsRefresh = true;
		std::vector<FallbackEntry> entries;
	};

	void SDLCALL DialogTrampoline(void *ud, const char * const *filelist, int filter);

	std::mutex g_fallbackMutex;
	DialogContext *g_fallbackCtx = nullptr;
	bool g_fallbackOpenNextFrame = false;

	std::string ParentPath(std::string path) {
		while (path.size() > 1 && path.back() == '/')
			path.pop_back();
		const size_t slash = path.find_last_of('/');
		if (slash == std::string::npos || slash == 0)
			return "/";
		return path.substr(0, slash);
	}

	std::string JoinPath(const std::string& dir, const std::string& name) {
		if (dir.empty() || dir == "/")
			return "/" + name;
		return dir + "/" + name;
	}

	bool StringContainsI(const std::string& text, const std::string& needle) {
		if (needle.empty())
			return true;
		auto it = std::search(text.begin(), text.end(), needle.begin(), needle.end(),
			[](char a, char b) {
				return std::tolower((unsigned char)a)
					== std::tolower((unsigned char)b);
			});
		return it != text.end();
	}

	bool HasExtensionI(const std::string& name, std::string ext) {
		if (ext.empty() || ext == "*")
			return true;
		while (!ext.empty() && (ext[0] == '*' || ext[0] == '.'))
			ext.erase(ext.begin());
		if (ext.empty())
			return true;
		const size_t dot = name.find_last_of('.');
		if (dot == std::string::npos)
			return false;
		const std::string fileExt = name.substr(dot + 1);
		if (fileExt.size() != ext.size())
			return false;
		for (size_t i = 0; i < ext.size(); ++i) {
			if (std::tolower((unsigned char)fileExt[i])
				!= std::tolower((unsigned char)ext[i]))
				return false;
		}
		return true;
	}

	bool MatchesFilter(const DialogContext& ctx, const std::string& name) {
		if (ctx.mode == DialogMode::OpenFolder || ctx.expandedFilters.empty())
			return true;

		int idx = ctx.selectedFilter;
		if (idx < 0 || idx >= (int)ctx.expandedFilters.size())
			idx = 0;

		const char *pattern = ctx.expandedFilters[idx].pattern;
		if (!pattern || !*pattern || !strcmp(pattern, "*"))
			return true;

		const char *p = pattern;
		while (*p) {
			const char *semi = p;
			while (*semi && *semi != ';')
				++semi;
			if (HasExtensionI(name, std::string(p, semi)))
				return true;
			p = *semi ? semi + 1 : semi;
		}
		return false;
	}

	std::string FirstFilterExtension(const DialogContext& ctx) {
		if (ctx.expandedFilters.empty())
			return {};

		int idx = ctx.selectedFilter;
		if (idx < 0 || idx >= (int)ctx.expandedFilters.size())
			idx = 0;

		const char *pattern = ctx.expandedFilters[idx].pattern;
		if (!pattern || !*pattern || !strcmp(pattern, "*"))
			return {};

		const char *semi = strchr(pattern, ';');
		std::string ext(pattern, semi ? semi : pattern + strlen(pattern));
		while (!ext.empty() && (ext[0] == '*' || ext[0] == '.'))
			ext.erase(ext.begin());
		return ext == "*" ? std::string() : ext;
	}

	void SetFallbackDirectory(DialogContext& ctx, const std::string& path) {
		ctx.currentDir = path.empty() ? "/" : path;
		ctx.editDir = ctx.currentDir;
		ctx.needsRefresh = true;
		for (FallbackEntry& entry : ctx.entries)
			entry.selected = false;
	}

	void QueueFallback(DialogContext *ctx) {
		if (ctx->currentDir.empty()) {
			if (!ctx->defaultLocationUtf8.empty())
				SetFallbackDirectory(*ctx, ctx->defaultLocationUtf8.c_str());
			else if (const char *home = SDL_GetUserFolder(SDL_FOLDER_HOME))
				SetFallbackDirectory(*ctx, home);
			else
				SetFallbackDirectory(*ctx, "/");
		}

		std::lock_guard<std::mutex> lock(g_fallbackMutex);
		if (g_fallbackCtx) {
			DialogContext *old = g_fallbackCtx;
			g_fallbackCtx = nullptr;
			const char *noList[1] = { nullptr };
			DialogTrampoline(old, noList, 0);
		}
		g_fallbackCtx = ctx;
		g_fallbackOpenNextFrame = true;
	}

	void SDLCALL DialogTrampoline(void *ud, const char * const *filelist, int filter) {
		DialogContext *ctx = static_cast<DialogContext *>(ud);

		if (!filelist) {
			const char *err = SDL_GetError();
			fprintf(stderr,
				"[AltirraSDL] Native SDL file dialog failed: %s; using ImGui fallback.\n",
				err && *err ? err : "unknown error");
			QueueFallback(ctx);
			return;
		}

		// filelist == nullptr means an SDL error.  An empty list or empty
		// filename means cancel; do not forward it as a user action.
		if (!filelist[0] || !*filelist[0]) {
			delete ctx;
			return;
		}

		// Remember the selected file's full path so the next time this
		// dialog opens we can land in the same directory (matching
		// Windows Altirra's behaviour).
		VDStringW selected = VDTextU8ToW(VDStringSpanA(filelist[0]));
		VDSetLastLoadSavePath(ctx->nKey, selected.c_str());

		if (ctx->userCb)
			ctx->userCb(ctx->userUd, filelist, filter);

		delete ctx;
	}

	DialogContext *MakeContext(long nKey, SDL_DialogFileCallback cb, void *ud, const char *fallback) {
		DialogContext *ctx = new DialogContext;
		ctx->nKey   = nKey;
		ctx->userCb = cb;
		ctx->userUd = ud;

		const VDStringW lastPath = VDGetLastLoadSavePath(nKey);
		if (!lastPath.empty()) {
			uint32 attrs = VDFileGetAttributes(lastPath.c_str());

			bool isDirectory = (attrs != kVDFileAttr_Invalid && (attrs & kVDFileAttr_Directory));

			VDStringW dir;
			if (isDirectory) {
				dir = lastPath;
			} else {
				dir = VDFileSplitPathLeft(lastPath);

				if (dir.empty()) {
					dir = lastPath;
				}
			}

			ctx->defaultLocationUtf8 = VDTextWToU8(VDStringSpanW(dir));
		} else if (fallback && *fallback) {
			ctx->defaultLocationUtf8 = fallback;
		}

		return ctx;
	}
}

static void ExpandFilters(DialogContext *ctx, const SDL_DialogFileFilter *filters, int nfilters) {
	ctx->expandedPatterns.reserve(nfilters);
	ctx->expandedFilters.reserve(nfilters);
	for (int i = 0; i < nfilters; i++) {
		ctx->expandedPatterns.push_back(
			ExpandExtensionsCaseInsensitive(filters[i].pattern));
		ctx->expandedFilters.push_back({
			filters[i].name,
			ctx->expandedPatterns.back().c_str()
		});
	}
}

static void RefreshFallbackEntries(DialogContext& ctx) {
	ctx.entries.clear();

	auto cb = [](void *ud, const char *dirname, const char *fname)
		-> SDL_EnumerationResult {
		DialogContext *ctx = (DialogContext *)ud;
		if (!fname || !*fname)
			return SDL_ENUM_CONTINUE;
		if (!ctx->showHidden && fname[0] == '.')
			return SDL_ENUM_CONTINUE;
		if (!StringContainsI(fname, ctx->search))
			return SDL_ENUM_CONTINUE;

		FallbackEntry entry;
		entry.name = fname;
		entry.path = JoinPath(ctx->currentDir, fname);
		if (!SDL_GetPathInfo(entry.path.c_str(), &entry.info))
			return SDL_ENUM_CONTINUE;

		const bool isDir = entry.info.type == SDL_PATHTYPE_DIRECTORY;
		if (ctx->mode == DialogMode::OpenFolder) {
			if (!isDir)
				return SDL_ENUM_CONTINUE;
		} else if (!isDir && !MatchesFilter(*ctx, entry.name)) {
			return SDL_ENUM_CONTINUE;
		}

		ctx->entries.push_back(std::move(entry));
		return SDL_ENUM_CONTINUE;
	};

	SDL_EnumerateDirectory(ctx.currentDir.c_str(), cb, &ctx);
	std::sort(ctx.entries.begin(), ctx.entries.end(),
		[&](const FallbackEntry& a, const FallbackEntry& b) {
			const bool ad = a.info.type == SDL_PATHTYPE_DIRECTORY;
			const bool bd = b.info.type == SDL_PATHTYPE_DIRECTORY;
			if (ad != bd)
				return ad > bd;
			int cmp = 0;
			if (ctx.sortByModified) {
				if (a.info.modify_time < b.info.modify_time)
					cmp = -1;
				else if (a.info.modify_time > b.info.modify_time)
					cmp = 1;
			}
			if (!cmp) {
				cmp = SDL_strcasecmp(a.name.c_str(), b.name.c_str());
				if (!cmp)
					cmp = strcmp(a.name.c_str(), b.name.c_str());
			}
			return ctx.sortAscending ? cmp < 0 : cmp > 0;
		});
	ctx.needsRefresh = false;
}

static std::string FormatSize(uint64_t size) {
	char buf[64];
	if (size >= 1024ULL * 1024ULL * 1024ULL)
		snprintf(buf, sizeof buf, "%.1f GB",
			(double)size / (1024.0 * 1024.0 * 1024.0));
	else if (size >= 1024ULL * 1024ULL)
		snprintf(buf, sizeof buf, "%.1f MB",
			(double)size / (1024.0 * 1024.0));
	else if (size >= 1024ULL)
		snprintf(buf, sizeof buf, "%.1f KB", (double)size / 1024.0);
	else
		snprintf(buf, sizeof buf, "%llu B", (unsigned long long)size);
	return buf;
}

static std::string FormatTime(SDL_Time t) {
	if (!t)
		return {};

	time_t sec = (time_t)(t / 1000000000LL);
	struct tm tmv;
#if defined(_WIN32)
	localtime_s(&tmv, &sec);
#else
	localtime_r(&sec, &tmv);
#endif
	char buf[32];
	strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tmv);
	return buf;
}

static void CompleteFallback(DialogContext *ctx, std::vector<std::string> paths) {
	std::vector<const char *> list;
	list.reserve(paths.size() + 1);
	for (const std::string& p : paths)
		list.push_back(p.c_str());
	list.push_back(nullptr);

	{
		std::lock_guard<std::mutex> lock(g_fallbackMutex);
		if (g_fallbackCtx == ctx)
			g_fallbackCtx = nullptr;
	}

	DialogTrampoline(ctx, list.data(), ctx->selectedFilter);
}

static void CancelFallback(DialogContext *ctx) {
	const char *noList[1] = { nullptr };
	{
		std::lock_guard<std::mutex> lock(g_fallbackMutex);
		if (g_fallbackCtx == ctx)
			g_fallbackCtx = nullptr;
	}
	DialogTrampoline(ctx, noList, ctx->selectedFilter);
}

static void RenderShortcutButton(const char *label, const char *path,
	DialogContext& ctx)
{
	if (!path || !*path)
		return;

	if (ImGui::Button(label)) {
		SetFallbackDirectory(ctx, path);
	}
	ImGui::SameLine();
}

void ATUIRenderFileDialogFallback() {
	DialogContext *ctx = nullptr;
	bool openNext = false;
	{
		std::lock_guard<std::mutex> lock(g_fallbackMutex);
		ctx = g_fallbackCtx;
		openNext = g_fallbackOpenNextFrame;
		g_fallbackOpenNextFrame = false;
	}

	if (!ctx)
		return;

	const char *title = ctx->mode == DialogMode::SaveFile ? "Save File"
		: ctx->mode == DialogMode::OpenFolder ? "Select Folder"
		: "Open File";
	char popupName[64];
	snprintf(popupName, sizeof popupName, "%s##ATFileDialogFallback",
		title);

	if (openNext)
		ImGui::OpenPopup(popupName);

	const ImGuiViewport *vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowSize(ImVec2(
		std::min(880.0f, vp->WorkSize.x - 32.0f),
		std::min(620.0f, vp->WorkSize.y - 32.0f)),
		ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing,
		ImVec2(0.5f, 0.5f));

	bool popupOpen = true;
	if (!ImGui::BeginPopupModal(popupName,
		&popupOpen, ImGuiWindowFlags_NoSavedSettings))
		return;

	if (!popupOpen) {
		ImGui::EndPopup();
		CancelFallback(ctx);
		return;
	}

	if (ctx->needsRefresh)
		RefreshFallbackEntries(*ctx);

	if (ImGui::Button("Up")) {
		SetFallbackDirectory(*ctx, ParentPath(ctx->currentDir));
	}
	ImGui::SameLine();
	if (ImGui::Button("Refresh"))
		ctx->needsRefresh = true;
	ImGui::SameLine();

	ImGui::SetNextItemWidth(-1);
	char pathBuf[4096];
	snprintf(pathBuf, sizeof pathBuf, "%s", ctx->editDir.c_str());
	ImGuiInputTextFlags pathFlags = ImGuiInputTextFlags_EnterReturnsTrue;
	if (ImGui::InputText("##path", pathBuf, sizeof pathBuf,
		pathFlags))
	{
		ctx->editDir = pathBuf;
		SDL_PathInfo info;
		if (SDL_GetPathInfo(pathBuf, &info)
			&& info.type == SDL_PATHTYPE_DIRECTORY)
		{
			SetFallbackDirectory(*ctx, pathBuf);
		}
	} else if (ImGui::IsItemEdited()) {
		ctx->editDir = pathBuf;
	}

	const char *home = SDL_GetUserFolder(SDL_FOLDER_HOME);
	const char *downloads = SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS);
	const char *desktop = SDL_GetUserFolder(SDL_FOLDER_DESKTOP);
	RenderShortcutButton("Home", home, *ctx);
	RenderShortcutButton("Downloads", downloads, *ctx);
	RenderShortcutButton("Desktop", desktop, *ctx);
	if (ImGui::Button("/")) {
		SetFallbackDirectory(*ctx, "/");
	}
	ImGui::SameLine();
	if (ImGui::Checkbox("Hidden", &ctx->showHidden))
		ctx->needsRefresh = true;

	ImGui::SameLine();
	ImGui::SetNextItemWidth(220.0f);
	char searchBuf[256];
	snprintf(searchBuf, sizeof searchBuf, "%s", ctx->search.c_str());
	if (ImGui::InputTextWithHint("##search", "Search", searchBuf,
		sizeof searchBuf))
	{
		ctx->search = searchBuf;
		ctx->needsRefresh = true;
	}

	if (!ctx->expandedFilters.empty()) {
		ImGui::SameLine();
		ImGui::SetNextItemWidth(220.0f);
		const char *preview =
			ctx->expandedFilters[ctx->selectedFilter].name;
		if (ImGui::BeginCombo("##filter", preview ? preview : "Filter")) {
			for (int i = 0; i < (int)ctx->expandedFilters.size(); ++i) {
				bool selected = i == ctx->selectedFilter;
				if (ImGui::Selectable(ctx->expandedFilters[i].name,
					selected))
				{
					ctx->selectedFilter = i;
					ctx->needsRefresh = true;
				}
				if (selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
	}

	if (ctx->mode == DialogMode::OpenFolder) {
		if (ImGui::Button("Select This Folder")) {
			CompleteFallback(ctx, { ctx->currentDir });
			ImGui::EndPopup();
			return;
		}
	}

	const float footerH = ctx->mode == DialogMode::SaveFile
		? 88.0f : 48.0f;
	if (ImGui::BeginTable("##filetable", 3,
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
			| ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY
			| ImGuiTableFlags_Sortable,
		ImVec2(0, -footerH)))
	{
		ImGui::TableSetupColumn("Name",
			ImGuiTableColumnFlags_DefaultSort
				| ImGuiTableColumnFlags_PreferSortAscending);
		ImGui::TableSetupColumn("Modified",
			ImGuiTableColumnFlags_PreferSortDescending
				| ImGuiTableColumnFlags_WidthFixed, 150.0f);
		ImGui::TableSetupColumn("Size",
			ImGuiTableColumnFlags_NoSort
				| ImGuiTableColumnFlags_WidthFixed, 90.0f);
		ImGui::TableHeadersRow();

		if (ImGuiTableSortSpecs *sorts = ImGui::TableGetSortSpecs()) {
			if (sorts->SpecsDirty && sorts->SpecsCount > 0) {
				const ImGuiTableColumnSortSpecs& spec = sorts->Specs[0];
				ctx->sortByModified = spec.ColumnIndex == 1;
				ctx->sortAscending =
					spec.SortDirection == ImGuiSortDirection_Ascending;
				ctx->needsRefresh = true;
				sorts->SpecsDirty = false;
			}
		}

		for (FallbackEntry& e : ctx->entries) {
			const bool isDir = e.info.type == SDL_PATHTYPE_DIRECTORY;
			ImGui::PushID(e.path.c_str());
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			std::string label = isDir ? "[DIR] " + e.name : e.name;
			ImGuiSelectableFlags selFlags =
				ImGuiSelectableFlags_SpanAllColumns
				| ImGuiSelectableFlags_AllowDoubleClick;
				bool selected = e.selected;
				if (ImGui::Selectable(label.c_str(), selected, selFlags)) {
					if (isDir && (ctx->mode != DialogMode::OpenFolder
						|| ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)))
					{
						SetFallbackDirectory(*ctx, e.path);
					} else if (isDir && ctx->mode == DialogMode::OpenFolder) {
						if (!ctx->allowMany) {
							for (FallbackEntry& other : ctx->entries)
								other.selected = false;
						}
						e.selected = !selected;
					} else if (!isDir && ctx->mode == DialogMode::SaveFile) {
						ctx->saveName = e.name;
					} else if (!isDir || ctx->mode == DialogMode::OpenFolder) {
					if (!ctx->allowMany) {
						for (FallbackEntry& other : ctx->entries)
							other.selected = false;
					}
					e.selected = !selected;
				}
			}
			if (!isDir && ctx->mode == DialogMode::OpenFile
				&& ImGui::IsItemHovered()
				&& ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				CompleteFallback(ctx, { e.path });
				ImGui::PopID();
				ImGui::EndTable();
				ImGui::EndPopup();
				return;
			}
			ImGui::TableSetColumnIndex(1);
			const std::string timeStr = FormatTime(e.info.modify_time);
			ImGui::TextUnformatted(timeStr.c_str());
			ImGui::TableSetColumnIndex(2);
			if (!isDir) {
				const std::string sizeStr = FormatSize((uint64_t)e.info.size);
				ImGui::TextUnformatted(sizeStr.c_str());
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	if (ctx->mode == DialogMode::SaveFile) {
		char nameBuf[1024];
		snprintf(nameBuf, sizeof nameBuf, "%s", ctx->saveName.c_str());
		ImGui::SetNextItemWidth(-1);
		if (ImGui::InputText("File name", nameBuf, sizeof nameBuf))
			ctx->saveName = nameBuf;
	}

	const char *primary = ctx->mode == DialogMode::SaveFile ? "Save"
		: ctx->mode == DialogMode::OpenFolder ? "Select Folder"
		: "Open";
	if (ImGui::Button(primary, ImVec2(120.0f, 0))) {
		std::vector<std::string> paths;
		if (ctx->mode == DialogMode::SaveFile) {
			std::string name = ctx->saveName;
			if (!name.empty()) {
				if (!MatchesFilter(*ctx, name)) {
					std::string ext = FirstFilterExtension(*ctx);
					if (!ext.empty())
						name += "." + ext;
				}
				paths.push_back(JoinPath(ctx->currentDir, name));
			}
		} else {
			for (const FallbackEntry& e : ctx->entries) {
				if (e.selected)
					paths.push_back(e.path);
			}
		}
		if (!paths.empty()) {
			CompleteFallback(ctx, std::move(paths));
			ImGui::EndPopup();
			return;
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120.0f, 0))) {
		CancelFallback(ctx);
		ImGui::EndPopup();
		return;
	}

	ImGui::EndPopup();
}

bool ATUIGetForceBuiltinFileDialog() {
	return g_ATCVForceNonNativeDialogs;
}

void ATUISetForceBuiltinFileDialog(bool enabled) {
	g_ATCVForceNonNativeDialogs = enabled;
}

#if defined(__EMSCRIPTEN__)

// ---------------------------------------------------------------------
// WebAssembly override for SDL file dialogs.
//
// On native desktops, ATUIShowOpenFileDialog forwards to
// SDL_ShowOpenFileDialog which in turn uses the OS-native picker.  In a
// browser the picker would open the user's real filesystem — but the
// emulator can only see its own virtual filesystem (IDBFS under
// /home/web_user), so a returned OS path would be unreachable.
//
// Under WASM we instead route every file dialog through the JS-side
// file manager overlay (wasm_index.html.in) in "pick mode".  The user
// browses the already-uploaded library, picks a file (or cancels), and
// JS calls back into C via ATWasmOnFilePicked with the VFS path.  The
// original DialogContext is kept alive the whole time so the caller's
// SDL-style callback fires with the correct userdata / filter index.
//
// Save dialogs reuse the same overlay but in "save" mode; JS prompts
// the user for a filename, produces a path under the appropriate
// category directory, and feeds it back through the same callback.
// ---------------------------------------------------------------------

namespace {
	// Map Altirra's fourcc dialog keys (e.g. 'load', 'disk', 'cass',
	// 'scrn') to the category id understood by the JS file manager.
	// Falls back to 'games' for unknown keys so the overlay still opens
	// in a reasonable place.
	const char *WasmCategoryForDialogKey(long nKey) {
		switch (nKey) {
			case 'load': return "games";       // File → Boot Image
			case 'imag': return "games";       // Open Image
			case 'disk': return "disks";       // Disk mount
			case 'cass': return "cassettes";   // Cassette mount
			case 'cart': return "cartridges";  // Cartridge
			case 'firm': return "firmware";    // Firmware
			case 'ROMI': return "firmware";    // Firmware Manager → Add...
			case 'scrn': return "screenshots"; // Save screenshot
			case 'vid ': return "screenshots"; // Save video
			case 'aud ': return "screenshots"; // Save audio
			case 'sap ': return "screenshots"; // Save SAP/VGM
			case 'stat': return "states";      // Save state
			default:     return "games";
		}
	}

	// Single in-flight WASM dialog (SDL3 guarantees at most one open
	// dialog at a time, and the JS picker is modal, so a global is safe).
	DialogContext *g_pPendingWasmDialog = nullptr;

	// Invoke the JS-side opener.  Declared with EM_JS so we can call it
	// from C without a stringified Runtime_run_script hop.  kind=0 means
	// open, kind=1 means save.
	EM_JS(void, _altirra_wasm_open_picker,
	      (long nKey, const char *category, const char *filters, int kind), {
		if (typeof window.altirraOpenFilePicker === 'function') {
			window.altirraOpenFilePicker(
				nKey,
				UTF8ToString(category),
				UTF8ToString(filters),
				kind);
		}
	});

	// Join the dialog's filter list into a simple comma-separated string
	// the JS side can use to show which extensions are valid.
	VDStringA JoinFilterExtensions(const SDL_DialogFileFilter *filters,
	                               int nfilters) {
		VDStringA out;
		for (int i = 0; i < nfilters; ++i) {
			if (!filters[i].pattern) continue;
			if (!out.empty()) out += ';';
			out += filters[i].pattern;
		}
		return out;
	}
}

// Called from JS when the user picks a file in the overlay, or cancels.
// vfsPath == nullptr OR empty string → cancellation.
extern "C" EMSCRIPTEN_KEEPALIVE
void ATWasmOnFilePicked(const char *vfsPath) {
	const bool cancelled = !(vfsPath && *vfsPath);
	fprintf(stderr, "[wasm] ATWasmOnFilePicked: %s\n",
	        cancelled ? "<cancel>" : vfsPath);

	DialogContext *ctx = g_pPendingWasmDialog;
	g_pPendingWasmDialog = nullptr;
	if (!ctx) {
		fprintf(stderr, "[wasm] ATWasmOnFilePicked: no pending ctx, ignored\n");
		return;
	}

	// Fabricate an SDL-style (NULL-terminated) list so the existing
	// trampoline / callback logic works unchanged.
	const char *chosenList[2] = { nullptr, nullptr };
	if (!cancelled)
		chosenList[0] = vfsPath;

	try {
		DialogTrampoline(ctx, chosenList, 0);
		fprintf(stderr, "[wasm] ATWasmOnFilePicked: trampoline returned\n");
	} catch (const std::exception& ex) {
		fprintf(stderr, "[wasm] ATWasmOnFilePicked: std::exception: %s\n", ex.what());
	} catch (...) {
		fprintf(stderr, "[wasm] ATWasmOnFilePicked: unknown exception\n");
	}
	// DialogTrampoline deletes ctx on its own.
}

void ATUIShowOpenFileDialog(
	long nKey,
	SDL_DialogFileCallback callback,
	void *userdata,
	SDL_Window *window,
	const SDL_DialogFileFilter *filters,
	int nfilters,
	bool allow_many,
	const char *fallbackLocation)
{
	DialogContext *ctx = MakeContext(nKey, callback, userdata, fallbackLocation);
	ctx->mode = DialogMode::OpenFile;
	ctx->allowMany = allow_many;
	ExpandFilters(ctx, filters, nfilters);

	// Dismiss any stale pending picker — this should not normally
	// happen (SDL serialises dialogs), but if it does, fabricate a
	// cancellation for the previous caller before handing the picker
	// to the new one.
	if (g_pPendingWasmDialog) {
		DialogContext *stale = g_pPendingWasmDialog;
		g_pPendingWasmDialog = nullptr;
		const char *noList[1] = { nullptr };
		DialogTrampoline(stale, noList, 0);
	}

	g_pPendingWasmDialog = ctx;

	const VDStringA exts = JoinFilterExtensions(
		ctx->expandedFilters.data(),
		(int)ctx->expandedFilters.size());

	_altirra_wasm_open_picker(nKey,
		WasmCategoryForDialogKey(nKey),
		exts.c_str(),
		0);
}

void ATUIShowSaveFileDialog(
	long nKey,
	SDL_DialogFileCallback callback,
	void *userdata,
	SDL_Window *window,
	const SDL_DialogFileFilter *filters,
	int nfilters,
	const char *fallbackLocation)
{
	DialogContext *ctx = MakeContext(nKey, callback, userdata, fallbackLocation);
	ctx->mode = DialogMode::SaveFile;
	ExpandFilters(ctx, filters, nfilters);

	if (g_pPendingWasmDialog) {
		DialogContext *stale = g_pPendingWasmDialog;
		g_pPendingWasmDialog = nullptr;
		const char *noList[1] = { nullptr };
		DialogTrampoline(stale, noList, 0);
	}

	g_pPendingWasmDialog = ctx;

	const VDStringA exts = JoinFilterExtensions(
		ctx->expandedFilters.data(),
		(int)ctx->expandedFilters.size());

	_altirra_wasm_open_picker(nKey,
		WasmCategoryForDialogKey(nKey),
		exts.c_str(),
		1);
}

void ATUIShowOpenFolderDialog(
	long nKey,
	SDL_DialogFileCallback callback,
	void *userdata,
	SDL_Window *window,
	const char *fallbackLocation,
	bool allow_many)
{
	SDL_ShowOpenFolderDialog(callback, userdata, window, fallbackLocation,
		allow_many);
}

#else // !__EMSCRIPTEN__

void ATUIShowOpenFileDialog(
	long nKey,
	SDL_DialogFileCallback callback,
	void *userdata,
	SDL_Window *window,
	const SDL_DialogFileFilter *filters,
	int nfilters,
	bool allow_many,
	const char *fallbackLocation)
{
	DialogContext *ctx = MakeContext(nKey, callback, userdata, fallbackLocation);
	ctx->mode = DialogMode::OpenFile;
	ctx->allowMany = allow_many;
	ExpandFilters(ctx, filters, nfilters);
	const char *defLoc = ctx->defaultLocationUtf8.empty() ? nullptr : ctx->defaultLocationUtf8.c_str();

	if (ATUIGetForceBuiltinFileDialog()) {
		QueueFallback(ctx);
		return;
	}

	SDL_ShowOpenFileDialog(DialogTrampoline, ctx, window,
		ctx->expandedFilters.data(), nfilters, defLoc, allow_many);
}

void ATUIShowSaveFileDialog(
	long nKey,
	SDL_DialogFileCallback callback,
	void *userdata,
	SDL_Window *window,
	const SDL_DialogFileFilter *filters,
	int nfilters,
	const char *fallbackLocation)
{
	DialogContext *ctx = MakeContext(nKey, callback, userdata, fallbackLocation);
	ctx->mode = DialogMode::SaveFile;
	ExpandFilters(ctx, filters, nfilters);
	const char *defLoc = ctx->defaultLocationUtf8.empty() ? nullptr : ctx->defaultLocationUtf8.c_str();

	if (ATUIGetForceBuiltinFileDialog()) {
		QueueFallback(ctx);
		return;
	}

	SDL_ShowSaveFileDialog(DialogTrampoline, ctx, window,
		ctx->expandedFilters.data(), nfilters, defLoc);
}

void ATUIShowOpenFolderDialog(
	long nKey,
	SDL_DialogFileCallback callback,
	void *userdata,
	SDL_Window *window,
	const char *fallbackLocation,
	bool allow_many)
{
	DialogContext *ctx = MakeContext(nKey, callback, userdata, fallbackLocation);
	ctx->mode = DialogMode::OpenFolder;
	ctx->allowMany = allow_many;
	const char *defLoc = ctx->defaultLocationUtf8.empty()
		? nullptr : ctx->defaultLocationUtf8.c_str();

	if (ATUIGetForceBuiltinFileDialog()) {
		QueueFallback(ctx);
		return;
	}

	SDL_ShowOpenFolderDialog(DialogTrampoline, ctx, window, defLoc,
		allow_many);
}

#endif // __EMSCRIPTEN__
