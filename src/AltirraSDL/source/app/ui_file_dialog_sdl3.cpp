#include "ui_file_dialog_sdl3.h"

#include <vd2/system/filesys.h>
#include <vd2/system/text.h>
#include <vd2/system/VDString.h>
#include <vd2/Dita/services.h>

namespace {
	struct DialogContext {
		long					nKey;
		SDL_DialogFileCallback	userCb;
		void *					userUd;
		VDStringA				defaultLocationUtf8;	// keeps the c_str alive until SDL returns
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
	const char *defLoc = ctx->defaultLocationUtf8.empty() ? nullptr : ctx->defaultLocationUtf8.c_str();

	SDL_ShowOpenFileDialog(DialogTrampoline, ctx, window, filters, nfilters, defLoc, allow_many);
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
	const char *defLoc = ctx->defaultLocationUtf8.empty() ? nullptr : ctx->defaultLocationUtf8.c_str();

	SDL_ShowSaveFileDialog(DialogTrampoline, ctx, window, filters, nfilters, defLoc);
}
