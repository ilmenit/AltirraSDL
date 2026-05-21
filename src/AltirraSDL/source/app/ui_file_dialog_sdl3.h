// SDL3 file dialog wrapper that remembers the last-used directory per
// dialog, using the same VDGet/SetLastLoadSavePath map Windows Altirra
// persists to settings.ini.
//
// Call sites pass a FourCC key identifying the logical dialog (e.g.
// 'load', 'cart', 'disk', 'cass', 'save').  Use the same keys as the
// Windows build so settings.ini is interoperable.
//
// The wrapper:
//   1. looks up the saved last path for the key and passes its parent
//      directory to SDL as default_location;
//   2. when the user picks a file, stores the full selected path back
//      under the same key before invoking the user's callback.
//
// Callbacks may be invoked from SDL's dialog thread — this wrapper only
// touches the thread-safe VD registry/filespec API, so it is safe to
// call the underlying VDSetLastLoadSavePath from the callback trampoline
// before forwarding to user code.

#ifndef AT_UI_FILE_DIALOG_SDL3_H
#define AT_UI_FILE_DIALOG_SDL3_H

#include <SDL3/SDL.h>

void ATUIShowOpenFileDialog(
	long nKey,
	SDL_DialogFileCallback callback,
	void *userdata,
	SDL_Window *window,
	const SDL_DialogFileFilter *filters,
	int nfilters,
	bool allow_many,
	const char *fallbackLocation = nullptr);

void ATUIShowSaveFileDialog(
	long nKey,
	SDL_DialogFileCallback callback,
	void *userdata,
	SDL_Window *window,
	const SDL_DialogFileFilter *filters,
	int nfilters,
	const char *fallbackLocation = nullptr);

void ATUIShowOpenFolderDialog(
	long nKey,
	SDL_DialogFileCallback callback,
	void *userdata,
	SDL_Window *window,
	const char *fallbackLocation = nullptr,
	bool allow_many = false);

void ATUIRenderFileDialogFallback();

bool ATUIGetForceBuiltinFileDialog();
void ATUISetForceBuiltinFileDialog(bool enabled);

#endif
