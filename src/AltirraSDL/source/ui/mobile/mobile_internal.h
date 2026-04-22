//	AltirraSDL - Mobile UI internal header
//	Shared state and helpers between the split mobile screen
//	implementations (ui_mobile.cpp, mobile_*.cpp).  These were file-static
//	in ui_mobile.cpp before the Phase 3b split; the definitions still
//	live in ui_mobile.cpp but the symbols now have external linkage so
//	the per-screen translation units can reference them.
//
//	No public ATUI* signature in ui_mobile.h is changed by this header.

#pragma once

#include <cwctype>
#include <vector>
#include <functional>
#include <vd2/system/VDString.h>

#include "ui_mobile.h"
#include "firmwaremanager.h"

class ATSimulator;
struct ATUIState;
struct SDL_Window;

// -------------------------------------------------------------------------
// Density-independent pixel helper
// -------------------------------------------------------------------------

extern float s_contentScale;

inline float dp(float v) { return v * s_contentScale; }

// -------------------------------------------------------------------------
// File browser entry + state (definitions in ui_mobile.cpp)
// -------------------------------------------------------------------------

struct FileBrowserEntry {
	VDStringW name;
	VDStringW fullPath;
	bool isDirectory;
	bool operator<(const FileBrowserEntry &o) const {
		if (isDirectory != o.isDirectory) return isDirectory > o.isDirectory;
		return name < o.name;
	}
};

extern std::vector<FileBrowserEntry> s_fileBrowserEntries;
extern VDStringW s_fileBrowserDir;
extern bool s_fileBrowserNeedsRefresh;

extern bool s_romFolderMode;
extern VDStringW s_romDir;
extern int s_romScanResult;

// Folder-picker mode: when set, the file browser shows only directories
// and the "Select this folder" button. On selection, the callback is
// invoked with the chosen path and the browser returns to the previous
// screen stored in s_folderPickerReturnScreen.
extern bool s_folderPickerMode;
extern std::function<void(const VDStringW &)> s_folderPickerCallback;
extern ATMobileUIScreen s_folderPickerReturnScreen;

// Archive-file-picker mode: when set, the file browser lets the user
// navigate directories and tap a single archive file (.zip/.atz/.gz/.arc)
// to select it.  Tapping a ZIP does NOT enter the archive; it invokes
// the callback with the ZIP's full path and returns to
// s_archiveFilePickerReturnScreen.  Directories still navigate normally
// so the user can browse to the archive's location.
extern bool s_archiveFilePickerMode;
extern std::function<void(const VDStringW &)> s_archiveFilePickerCallback;
extern ATMobileUIScreen s_archiveFilePickerReturnScreen;

extern VDStringW s_zipArchivePath;
extern VDStringW s_zipInternalDir;

extern int s_diskMountTargetDrive;
extern bool s_mobileShowAllDrives;

// When true, the file browser shows all files regardless of extension
extern bool s_showAllFiles;

// -------------------------------------------------------------------------
// Modal info / confirmation sheet state
// -------------------------------------------------------------------------

extern VDStringA s_infoModalTitle;
extern VDStringA s_infoModalBody;
extern bool      s_infoModalOpen;

extern VDStringA s_confirmTitle;
extern VDStringA s_confirmBody;
extern std::function<void()> s_confirmAction;
extern bool      s_confirmActive;

void ShowInfoModal(const char *title, const char *body);
void ShowConfirmDialog(const char *title, const char *body,
	std::function<void()> onConfirm);

// -------------------------------------------------------------------------
// Settings page state
// -------------------------------------------------------------------------

enum class ATMobileSettingsPage {
	Home,
	Machine,
	Display,
	Audio,
	Performance,
	Controls,
	SaveState,
	Firmware,
	GameLibrary,
	OnlinePlay,
};
extern ATMobileSettingsPage s_settingsPage;
extern ATMobileUIScreen s_settingsReturnScreen;
// Set by the Online Play hub when the user taps the Preferences
// shortcut, so that pressing Back on the Settings home page not only
// restores `s_settingsReturnScreen` but also reopens the Online Play
// hub overlay the user came from.  Cleared by the Settings back
// handlers once consumed.
extern bool s_settingsReturnToNetplayHub;
extern ATFirmwareType s_fwPicker;

// -------------------------------------------------------------------------
// Helpers shared across the mobile_*.cpp split files
// -------------------------------------------------------------------------

void RefreshFileBrowser(const VDStringW &dir);
void NavigateUp();
void JumpToDirectory(const char *u8path);

inline bool IsZipFile(const wchar_t *name) {
	const wchar_t *ext = wcsrchr(name, L'.');
	if (!ext) return false;
	++ext;
	return (std::towlower(ext[0]) == L'z' && std::towlower(ext[1]) == L'i'
		&& std::towlower(ext[2]) == L'p' && ext[3] == 0);
}

// Persistence helpers (definitions in ui_mobile.cpp; promoted out of the
// anonymous namespace because they are now called from per-screen TUs).
void SaveFileBrowserDir(const VDStringW &dir);
bool IsFirstRunComplete();
void SetFirstRunComplete();
bool IsPermissionAsked();
void SetPermissionAsked();
void SaveMobileConfig(const ATMobileUIState &mobileState);
VDStringW QuickSaveStatePath();

// Firmware scan function (defined in ui_firmware.cpp)
extern void ExecuteFirmwareScan(class ATFirmwareManager *fwm,
	const VDStringW &scanDir);

// -------------------------------------------------------------------------
// Per-screen Render functions (definitions in mobile_*.cpp)
// -------------------------------------------------------------------------

void RenderHamburgerMenu(ATSimulator &sim, ATUIState &uiState,
	ATMobileUIState &mobileState, SDL_Window *window);
void RenderFileBrowser(ATSimulator &sim, ATUIState &uiState,
	ATMobileUIState &mobileState, SDL_Window *window);
void RenderSettings(ATSimulator &sim, ATUIState &uiState,
	ATMobileUIState &mobileState, SDL_Window *window);
void RenderMobileDiskRow(ATSimulator &sim, int driveIdx,
	ATMobileUIState &mobileState);
void RenderMobileDiskManager(ATSimulator &sim, ATUIState &uiState,
	ATMobileUIState &mobileState, SDL_Window *window);
void RenderMobileAbout(ATSimulator &sim, ATUIState &uiState,
	ATMobileUIState &mobileState, SDL_Window *window);
void RenderFirstRunWizard(ATSimulator &sim, ATUIState &uiState,
	ATMobileUIState &mobileState, SDL_Window *window);
void RenderLoadGamePrompt(ATSimulator &sim, ATUIState &uiState,
	ATMobileUIState &mobileState);

// Game Library browser (mobile_game_browser.cpp)
void RenderGameBrowser(ATSimulator &sim, ATUIState &uiState,
	ATMobileUIState &mobileState, SDL_Window *window);
void GameBrowser_Init();
void GameBrowser_Shutdown();
void GameBrowser_Invalidate();
int  GameBrowser_FindCurrentEntry();
bool GameBrowser_HasCurrentGame();
// True when the currently-booted game maps to a library entry that
// has no cover art yet — i.e. the hamburger-menu quick action would
// do something useful.  False when no game is booted, the booted file
// isn't in the library, or the entry already has art (in which case
// the user can still replace it from the Game Library settings page).
bool GameBrowser_CurrentEntryNeedsArt();
// Capture the current emulator frame and install it as the library
// entry's cover art.  Returns an empty string on success, or a user-
// facing error message on failure (no current game, no framebuffer,
// PNG save failed, etc.).  Shared by the Game Library settings page
// and the hamburger menu so both paths use the same save location
// (config_dir/custom_art/<md5>.png) and the same thumbnail-invalidate
// logic.
VDStringA GameBrowser_SetCurrentFrameAsArt();
void GameBrowser_ClearArtCache();
// Called by the file browser after a Boot Game launch.  Records the
// current variant path (so the "currently playing" badge works) and,
// if the user has enabled "Add booted games to library", also appends
// the file (or its archive) as a library source and creates an entry
// so it shows up in Last Played on the next open.
void GameBrowser_OnBootedGame(const VDStringW &variantPath);
class ATGameLibrary;
ATGameLibrary *GetGameLibrary();

// Art cache accessor — shared between the Gaming-Mode Game Browser
// and the Online Play screens (so netplay rows can reuse textures
// already loaded for the library grid instead of double-caching).
// Returns nullptr before GameBrowser_Init().
class GameArtCache;
GameArtCache *GetGameArtCache();

// Lookup helpers used by the Disk Drives screen to surface a
// "Select" button that re-opens the variant picker for multi-disk
// entries.  Return -1 / 0 when the path / index isn't in the library.
int GameBrowser_FindEntryForPath(const wchar_t *path);
int GameBrowser_GetVariantCount(int entryIdx);
// Open the variant picker in "swap" mode — instead of booting the
// chosen variant (which would start a fresh session), invoke
// `onPick(variantPath)` so the caller can LoadDisk() into an
// already-running drive.
void GameBrowser_ShowVariantPickerForSwap(int entryIdx,
	std::function<void(const VDStringW &)> onPick);
// Render the variant picker if currently open.  Safe no-op when
// closed.  Called from the top-level screen dispatcher so the swap-
// mode picker (opened from the Disk Drives screen) still renders
// even when the user isn't on the Game Browser screen.
void GameBrowser_RenderOverlays(ATSimulator &sim,
	ATMobileUIState &mobileState);

// Put the Game Browser into "picker mode" — the full Game Library UI
// (grid/list toggle, A-Z letter pill, search, cover-art grid) is
// reused, but tapping a game does not boot it.  Instead `onPick` is
// invoked with the chosen variant path + library indices + display
// name + variant label, and the caller decides what to do (netplay
// Add-Game, attach-to-multicart, etc.).
//
//   onPick     - called once on commit; picker then clears itself.
//   onCancel   - called if the user backs out (Cancel button / ESC /
//                Gamepad-B); nullable.
//   bannerText - small label rendered in place of the Boot Game /
//                Boot Empty toolbar so the user knows they're in
//                a picker, not the normal browse flow.  Nullable.
//
// Use `GameBrowser_IsPickerActive()` to query and
// `GameBrowser_ClosePicker()` to dismiss without firing callbacks
// (e.g. when the owner screen closes while the picker is still up).
using GameBrowserPickFn = std::function<void(
	const VDStringW& path, int entryIdx, int variantIdx,
	const VDStringW& displayName, const VDStringW& variantLabel)>;

void GameBrowser_OpenPicker(GameBrowserPickFn onPick,
	std::function<void()> onCancel, const char *bannerText);
bool GameBrowser_IsPickerActive();
void GameBrowser_ClosePicker();

// Settings sub-page functions split into their own TUs
void RenderSettingsPage_Firmware(ATMobileUIState &mobileState);

// Modal sheet renderer (mobile_dialogs.cpp)
void RenderMobileModalSheet(const ATMobileUIState &mobileState);
