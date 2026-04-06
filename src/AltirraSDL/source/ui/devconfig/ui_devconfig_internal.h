//	AltirraSDL - Device configuration internal header
//	Browse helpers shared between the split ui_devconfig_devices*.cpp files.

#ifndef f_AT_UI_DEVCONFIG_INTERNAL_H
#define f_AT_UI_DEVCONFIG_INTERNAL_H

struct SDL_DialogFileFilter;

// Called each frame on the main thread to apply pending async browse
// results into the target buffer(s). Defined in
// ui_devconfig_devices_browse.cpp; called from ui_devconfig.cpp.
void DevBrowseApplyPending();

// Async file / folder / save-file dialogs that write the chosen path
// into `buf` once the user closes the dialog.
void DevBrowseForFile(char *buf, int maxLen,
	const SDL_DialogFileFilter *filters, int filterCount);
void DevBrowseForFolder(char *buf, int maxLen);
void DevBrowseForSaveFile(char *buf, int maxLen,
	const SDL_DialogFileFilter *filters, int filterCount);

// InputText + "..." Browse button pair. Returns true if Browse clicked.
bool InputTextWithBrowse(const char *label, char *buf, int bufSize, const char *browseId);

#endif
