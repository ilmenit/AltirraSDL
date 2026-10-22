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
// Opening a picker suspends emulation until its selected-file action has
// been processed. Existing manual pauses are preserved on completion.
// SDL completion threads only copy selected paths, filter, and error data.
// User callbacks, path remembering, and error fallback installation run on
// the main thread when ATUIPollFileDialogCompletions() is drained. The pause
// lease remains held until that callback has returned.

#ifndef AT_UI_FILE_DIALOG_SDL3_H
#define AT_UI_FILE_DIALOG_SDL3_H

#include <SDL3/SDL.h>

// notifyCancellation opts into the SDL cancellation callback (an empty list).
// Existing callers keep their historical success-only callback behavior.
void ATUIShowOpenFileDialog(
	long nKey,
	SDL_DialogFileCallback callback,
	void *userdata,
	SDL_Window *window,
	const SDL_DialogFileFilter *filters,
	int nfilters,
	bool allow_many,
	const char *fallbackLocation = nullptr,
	bool notifyCancellation = false);

void ATUIShowSaveFileDialog(
	long nKey,
	SDL_DialogFileCallback callback,
	void *userdata,
	SDL_Window *window,
	const SDL_DialogFileFilter *filters,
	int nfilters,
	const char *fallbackLocation = nullptr,
	bool notifyCancellation = false);

void ATUIShowOpenFolderDialog(
	long nKey,
	SDL_DialogFileCallback callback,
	void *userdata,
	SDL_Window *window,
	const char *fallbackLocation = nullptr,
	bool allow_many = false,
	bool notifyCancellation = false);

void ATUIRenderFileDialogFallback();

// Main-thread lifecycle. Shutdown is idempotent and does not access the core;
// late native callbacks discard their data without invoking client userdata.
void ATUIInitFileDialogs();
void ATUIPollFileDialogCompletions();
void ATUIShutdownFileDialogs();

#if defined(IMGUI_ENABLE_TEST_ENGINE) && !defined(__EMSCRIPTEN__)
// Exercises the actual SDL completion trampoline from a real worker, without
// depending on an interactive OS picker. Test-mode callers only.
void ATUITestFileDialogWorkerCompletion(SDL_DialogFileCallback, void *userdata,
	const char *mode, bool notifyCancellation);
#endif

// Main-thread pause lease, shared by native, built-in and browser pickers.
// Returns true while selected-file actions must run without emulation.
bool ATUIPollFileDialogPause(bool fileBrowserOpen = false);
bool ATUIIsFileDialogPaused();
// Main-thread logical run intent hidden by the temporary picker suspension.
// An explicit manual pause takes priority over the pre-picker running state.
bool ATUIIsFileDialogResumePending();

bool ATUIGetForceBuiltinFileDialog();
void ATUISetForceBuiltinFileDialog(bool enabled);

#endif
