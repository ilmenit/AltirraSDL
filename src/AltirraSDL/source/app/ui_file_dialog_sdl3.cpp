#include "ui_file_dialog_sdl3.h"

#include <cctype>
#include <string>
#include <vector>
#include <vd2/system/filesys.h>
#include <vd2/system/text.h>
#include <vd2/system/VDString.h>
#include <vd2/Dita/services.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace {

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

		// Expanded filter storage — keeps strings alive until callback fires
		std::vector<std::string>         expandedPatterns;
		std::vector<SDL_DialogFileFilter> expandedFilters;
	};

	void SDLCALL DialogTrampoline(void *ud, const char * const *filelist, int filter) {
		DialogContext *ctx = static_cast<DialogContext *>(ud);

		// Remember the selected file's full path so the next time this
		// dialog opens we can land in the same directory (matching
		// Windows Altirra's behaviour).  filelist == nullptr means an
		// SDL error, and an empty list (filelist[0] == nullptr) means
		// the user cancelled — in both cases leave the saved path alone.
		if (filelist && filelist[0]) {
			VDStringW selected = VDTextU8ToW(VDStringSpanA(filelist[0]));
			VDSetLastLoadSavePath(ctx->nKey, selected.c_str());
		}

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
	DialogContext *ctx = g_pPendingWasmDialog;
	g_pPendingWasmDialog = nullptr;
	if (!ctx) return;

	// Fabricate an SDL-style (NULL-terminated) list so the existing
	// trampoline / callback logic works unchanged.
	const char *chosenList[2] = { nullptr, nullptr };
	if (vfsPath && *vfsPath) {
		chosenList[0] = vfsPath;
		DialogTrampoline(ctx, chosenList, 0);
	} else {
		DialogTrampoline(ctx, chosenList, 0);
	}
	// DialogTrampoline deletes ctx.
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
	ExpandFilters(ctx, filters, nfilters);
	const char *defLoc = ctx->defaultLocationUtf8.empty() ? nullptr : ctx->defaultLocationUtf8.c_str();

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
	ExpandFilters(ctx, filters, nfilters);
	const char *defLoc = ctx->defaultLocationUtf8.empty() ? nullptr : ctx->defaultLocationUtf8.c_str();

	SDL_ShowSaveFileDialog(DialogTrampoline, ctx, window,
		ctx->expandedFilters.data(), nfilters, defLoc);
}

#endif // __EMSCRIPTEN__
