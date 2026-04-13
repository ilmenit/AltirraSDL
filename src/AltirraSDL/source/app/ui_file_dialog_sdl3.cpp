#include "ui_file_dialog_sdl3.h"

#include <cctype>
#include <string>
#include <vector>
#include <vd2/system/filesys.h>
#include <vd2/system/text.h>
#include <vd2/system/VDString.h>
#include <vd2/Dita/services.h>

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
