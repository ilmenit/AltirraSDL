//	AltirraSDL - Mobile UI internal header
//	Shared state and helpers between the split mobile screen
//	implementations (ui_mobile.cpp, mobile_*.cpp).  These were file-static
//	in ui_mobile.cpp before the Phase 3b split; the definitions still
//	live in ui_mobile.cpp but the symbols now have external linkage so
//	the per-screen translation units can reference them.
//
//	No public ATUI* signature in ui_mobile.h is changed by this header.

#pragma once

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

extern int s_diskMountTargetDrive;
extern bool s_mobileShowAllDrives;

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
	Performance,
	Controls,
	SaveState,
	Firmware,
};
extern ATMobileSettingsPage s_settingsPage;
extern ATFirmwareType s_fwPicker;

// -------------------------------------------------------------------------
// Helpers shared across the mobile_*.cpp split files
// -------------------------------------------------------------------------

void RefreshFileBrowser(const VDStringW &dir);
void NavigateUp();
void JumpToDirectory(const char *u8path);

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

// Settings sub-page functions split into their own TUs
void RenderSettingsPage_Firmware(ATMobileUIState &mobileState);

// Modal sheet renderer (mobile_dialogs.cpp)
void RenderMobileModalSheet(const ATMobileUIState &mobileState);
