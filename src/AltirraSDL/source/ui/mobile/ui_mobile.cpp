//	AltirraSDL - Mobile UI (hamburger menu, settings, file browser)
//	Touch-first UI for Android phones and tablets.
//	Provides hamburger slide-in menu, streamlined settings, and file browser.
//	All sizing uses density-independent pixels (dp) scaled by contentScale.

#include <stdafx.h>
#include <cwctype>
#include <vector>
#include <algorithm>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/registry.h>
#include <vd2/system/error.h>
#include <vd2/system/zip.h>
#include <at/atcore/media.h>
#include <at/atcore/vfs.h>
#include <at/atio/image.h>

#include "ui_mobile.h"
#include "ui_main.h"
#include "touch_controls.h"
#include "touch_widgets.h"
#include "simulator.h"
#include "gtia.h"
#include <at/ataudio/pokey.h>
#include "diskinterface.h"
#include "disk.h"
#include <at/atio/diskimage.h>
#include "mediamanager.h"
#include "firmwaremanager.h"
#include "uiaccessors.h"
#include "uitypes.h"
#include "constants.h"
#include "display_backend.h"
#include "android_platform.h"
#include <at/ataudio/audiooutput.h>

#include "mobile_internal.h"
#include "mobile_gamepad.h"
#include "ui_virtual_keyboard.h"

extern ATSimulator g_sim;
extern ATUIState g_uiState;
extern VDStringA ATGetConfigDir();
extern void ATRegistryFlushToDisk();
extern IDisplayBackend *ATUIGetDisplayBackend();

// -------------------------------------------------------------------------
// Persistence (registry-backed) for mobile-only UI settings.
// Desktop settings live in the existing Settings branch; mobile settings
// get their own namespace so they can't collide.
// -------------------------------------------------------------------------

namespace {
constexpr const char *kMobileKey = "Mobile";
}

static void LoadMobileConfig(ATMobileUIState &mobileState) {
	VDRegistryAppKey key(kMobileKey, false);
	if (!key.isReady())
		return;
	int sz = key.getEnumInt("ControlSize", 3, (int)ATTouchControlSize::Medium);
	mobileState.layoutConfig.controlSize = (ATTouchControlSize)sz;
	int opacityPct = key.getInt("ControlOpacity", 50);
	if (opacityPct < 10) opacityPct = 10;
	if (opacityPct > 100) opacityPct = 100;
	mobileState.layoutConfig.controlOpacity = opacityPct / 100.0f;
	mobileState.layoutConfig.hapticEnabled = key.getBool("HapticEnabled", true);
	mobileState.autoSaveOnSuspend   = key.getBool("AutoSaveOnSuspend", true);
	mobileState.autoRestoreOnStart  = key.getBool("AutoRestoreOnStart", true);
	mobileState.fxScanlines         = key.getBool("FxScanlines", false);
	mobileState.fxBloom             = key.getBool("FxBloom", false);
	mobileState.fxDistortion        = key.getBool("FxDistortion", false);
	mobileState.performancePreset   = key.getInt("PerformancePreset", 1);
	if (mobileState.performancePreset < 0 || mobileState.performancePreset > 3)
		mobileState.performancePreset = 1;
	int js = key.getInt("JoystickStyle", (int)ATTouchJoystickStyle::Analog);
	if (js < 0 || js > 2) js = 0;
	mobileState.layoutConfig.joystickStyle = (ATTouchJoystickStyle)js;
	mobileState.interfaceScale = key.getInt("InterfaceScale", 1);
	if (mobileState.interfaceScale < 0 || mobileState.interfaceScale > 2)
		mobileState.interfaceScale = 1;
#ifdef __ANDROID__
	mobileState.showTouchControls = key.getBool("ShowTouchControls", true);
#else
	mobileState.showTouchControls = key.getBool("ShowTouchControls", false);
#endif
}

void SaveMobileConfig(const ATMobileUIState &mobileState) {  // shared via mobile_internal.h
	{
		VDRegistryAppKey key(kMobileKey, true);
		if (!key.isReady())
			return;
		key.setInt("ControlSize", (int)mobileState.layoutConfig.controlSize);
		int pct = (int)(mobileState.layoutConfig.controlOpacity * 100.0f + 0.5f);
		if (pct < 10) pct = 10;
		if (pct > 100) pct = 100;
		key.setInt("ControlOpacity", pct);
		key.setBool("HapticEnabled", mobileState.layoutConfig.hapticEnabled);
		key.setBool("AutoSaveOnSuspend",  mobileState.autoSaveOnSuspend);
		key.setBool("AutoRestoreOnStart", mobileState.autoRestoreOnStart);
		key.setBool("FxScanlines",  mobileState.fxScanlines);
		key.setBool("FxBloom",      mobileState.fxBloom);
		key.setBool("FxDistortion", mobileState.fxDistortion);
		key.setInt("PerformancePreset", mobileState.performancePreset);
		key.setInt("JoystickStyle", (int)mobileState.layoutConfig.joystickStyle);
		key.setInt("InterfaceScale", mobileState.interfaceScale);
		key.setBool("ShowTouchControls", mobileState.showTouchControls);
	}
	// Persist immediately — registry-only writes are lost if the user
	// swipes the app away from recents before it backgrounds properly.
	ATRegistryFlushToDisk();
}

// Path to the quick save-state file under the config dir.
// Kept as VDStringW because simulator.SaveState takes wchar_t*.
VDStringW QuickSaveStatePath() {
	VDStringA dirU8 = ATGetConfigDir();
	dirU8 += "/quicksave.atstate2";
	return VDTextU8ToW(dirU8);
}

void SaveFileBrowserDir(const VDStringW &dir) {
	{
		VDRegistryAppKey key(kMobileKey, true);
		if (!key.isReady())
			return;
		key.setString("FileBrowserDir", dir.c_str());
	}
	ATRegistryFlushToDisk();
}

static VDStringW LoadFileBrowserDir() {
	VDRegistryAppKey key(kMobileKey, false);
	VDStringW s;
	if (key.isReady())
		key.getString("FileBrowserDir", s);
	return s;
}

bool IsFirstRunComplete() {
	VDRegistryAppKey key(kMobileKey, false);
	if (!key.isReady()) return false;
	return key.getBool("FirstRunComplete", false);
}

void SetFirstRunComplete() {
	{
		VDRegistryAppKey key(kMobileKey, true);
		if (!key.isReady()) return;
		key.setBool("FirstRunComplete", true);
	}
	// Flush IMMEDIATELY.  The user may close the app before it ever
	// gets a proper WILL_ENTER_BACKGROUND event, and without flushing
	// here the flag would be lost and the wizard would re-appear on
	// every launch.
	ATRegistryFlushToDisk();
}

bool IsPermissionAsked() {
	VDRegistryAppKey key(kMobileKey, false);
	if (!key.isReady()) return false;
	return key.getBool("PermissionAsked", false);
}

void SetPermissionAsked() {
	{
		VDRegistryAppKey key(kMobileKey, true);
		if (!key.isReady()) return;
		key.setBool("PermissionAsked", true);
	}
	ATRegistryFlushToDisk();
}

// -------------------------------------------------------------------------
// dp helper / shared state — declarations live in mobile_internal.h
// -------------------------------------------------------------------------

float s_contentScale = 1.0f;

std::vector<FileBrowserEntry> s_fileBrowserEntries;
VDStringW s_fileBrowserDir;
bool s_fileBrowserNeedsRefresh = true;

// ROM folder browser mode — when true, selecting a folder triggers firmware scan
bool s_romFolderMode = false;
VDStringW s_romDir;
int s_romScanResult = -1;  // -1 = no scan yet, 0+ = number of ROMs found

// Folder-picker mode — used by Game Library settings to select source folders
bool s_folderPickerMode = false;
std::function<void(const VDStringW &)> s_folderPickerCallback;
ATMobileUIScreen s_folderPickerReturnScreen = ATMobileUIScreen::Settings;

// Zip-as-folder browsing — when s_zipArchivePath is non-empty, the file
// browser shows contents of that zip archive instead of the filesystem.
// s_zipInternalDir is the current subdirectory within the zip (empty = root).
VDStringW s_zipArchivePath;
VDStringW s_zipInternalDir;

// Disk-mount browser mode — when >= 0, picking a file in the browser
// mounts it into the specified drive index instead of booting.
// -1 means normal Load Game mode.
int s_diskMountTargetDrive = -1;
bool s_mobileShowAllDrives = false;
bool s_showAllFiles = false;

// Generic modal info popup — every destructive / long-running action
// in the mobile UI should give the user explicit feedback.  This is a
// tiny system: set s_infoModalTitle/Body, the main render pass shows
// the modal, OK dismisses it.
VDStringA s_infoModalTitle;
VDStringA s_infoModalBody;
bool      s_infoModalOpen = false;

void ShowInfoModal(const char *title, const char *body) {
	s_infoModalTitle = title ? title : "";
	s_infoModalBody  = body ? body : "";
	s_infoModalOpen  = true;
}

// Confirm dialog — reuses the same mobile sheet renderer as the
// info modal but shows Cancel + Confirm buttons and fires a
// std::function on confirm.  Used by destructive hamburger actions
// (Cold/Warm Reset, Quick Save/Load).
VDStringA s_confirmTitle;
VDStringA s_confirmBody;
std::function<void()> s_confirmAction;
bool      s_confirmActive = false;

void ShowConfirmDialog(const char *title, const char *body,
	std::function<void()> onConfirm)
{
	s_confirmTitle  = title ? title : "";
	s_confirmBody   = body  ? body  : "";
	s_confirmAction = std::move(onConfirm);
	s_confirmActive = true;
}

// Public forwarders — give non-mobile translation units (ui_main.cpp
// tool/confirm popups) a way to drive the mobile sheet without
// touching the file-local statics.
void ATMobileUI_ShowInfoModal(const char *title, const char *body) {
	ShowInfoModal(title, body);
}
void ATMobileUI_ShowConfirmDialog(const char *title, const char *body,
	std::function<void()> onConfirm)
{
	ShowConfirmDialog(title, body, std::move(onConfirm));
}

// Hierarchical settings — definition lives in mobile_internal.h.
ATMobileSettingsPage s_settingsPage = ATMobileSettingsPage::Home;

// Firmware slot currently being picked within the Firmware sub-page.
// File scope so the header back button can close the picker.
ATFirmwareType s_fwPicker = kATFirmwareType_Unknown;

// Supported file extensions for Atari images
static bool IsSupportedExtension(const wchar_t *name) {
	const wchar_t *ext = wcsrchr(name, L'.');
	if (!ext) return false;
	ext++;

	VDStringW extLower;
	for (const wchar_t *p = ext; *p; ++p)
		extLower += (wchar_t)towlower(*p);

	static const wchar_t *kExtensions[] = {
		L"xex", L"atr", L"car", L"bin", L"rom", L"cas",
		L"dcm", L"atz", L"zip", L"gz", L"xfd", L"atx",
		L"obx", L"com", L"exe", L"pro", L"wav",
		nullptr
	};

	for (const wchar_t **p = kExtensions; *p; ++p) {
		if (extLower == *p)
			return true;
	}
	return false;
}

static void RefreshFileBrowserFromZip() {
	s_fileBrowserEntries.clear();

	try {
		VDFileStream fs(s_zipArchivePath.c_str());
		VDZipArchive zip;
		zip.Init(&fs);

		VDStringW prefix = s_zipInternalDir;
		if (!prefix.empty() && prefix.back() != L'/')
			prefix += L'/';

		std::vector<VDStringW> seenDirs;
		sint32 n = zip.GetFileCount();
		for (sint32 i = 0; i < n; i++) {
			const VDZipArchive::FileInfo &info = zip.GetFileInfo(i);
			const VDStringW &rawName = info.mDecodedFileName;

			if (rawName.length() <= prefix.length())
				continue;
			if (!prefix.empty() &&
				wcsncmp(rawName.c_str(), prefix.c_str(), prefix.length()) != 0)
				continue;

			const wchar_t *remainder = rawName.c_str() + prefix.length();
			const wchar_t *slash = wcschr(remainder, L'/');

			if (slash) {
				VDStringW dirName(remainder, (size_t)(slash - remainder));
				bool already = false;
				for (const auto &d : seenDirs) {
					if (d == dirName) { already = true; break; }
				}
				if (!already) {
					seenDirs.push_back(dirName);
					FileBrowserEntry entry;
					entry.name = dirName;
					entry.fullPath = prefix + dirName;
					entry.isDirectory = true;
					s_fileBrowserEntries.push_back(std::move(entry));
				}
			} else {
				if (*remainder == 0)
					continue;
				VDStringW fileName(remainder);
				if (s_romFolderMode)
					continue;
				if (!s_showAllFiles && !IsSupportedExtension(fileName.c_str()))
					continue;
				FileBrowserEntry entry;
				entry.name = fileName;
				entry.fullPath = ATMakeVFSPathForZipFile(
					s_zipArchivePath.c_str(), rawName.c_str());
				entry.isDirectory = false;
				s_fileBrowserEntries.push_back(std::move(entry));
			}
		}
	} catch (...) {
		s_zipArchivePath.clear();
		s_zipInternalDir.clear();
	}

	std::sort(s_fileBrowserEntries.begin(), s_fileBrowserEntries.end());
	s_fileBrowserNeedsRefresh = false;
}

void RefreshFileBrowser(const VDStringW &dir) {
	if (!s_zipArchivePath.empty()) {
		RefreshFileBrowserFromZip();
		return;
	}

	s_fileBrowserEntries.clear();
	s_fileBrowserDir = dir;

	VDStringA dirU8 = VDTextWToU8(VDStringW(dir));

	struct EnumCtx {
		VDStringW baseDir;
		std::vector<FileBrowserEntry> *entries;
		bool romMode;
	};

	EnumCtx ctx;
	ctx.baseDir = dir;
	if (!ctx.baseDir.empty() && ctx.baseDir.back() != L'/')
		ctx.baseDir += L'/';
	ctx.entries = &s_fileBrowserEntries;
	ctx.romMode = s_romFolderMode;

	auto callback = [](void *userdata, const char *dirname, const char *fname) -> SDL_EnumerationResult {
		EnumCtx *ctx = (EnumCtx *)userdata;

		if (fname[0] == '.')
			return SDL_ENUM_CONTINUE;

		VDStringW wname = VDTextU8ToW(VDStringA(fname));
		VDStringW fullPath = ctx->baseDir + wname;
		VDStringA fullPathU8 = VDTextWToU8(fullPath);

		FileBrowserEntry entry;
		entry.name = std::move(wname);
		entry.fullPath = std::move(fullPath);

		SDL_PathInfo info;
		if (SDL_GetPathInfo(fullPathU8.c_str(), &info)) {
			entry.isDirectory = (info.type == SDL_PATHTYPE_DIRECTORY);
		} else {
			entry.isDirectory = false;
		}

		if (ctx->romMode) {
			if (entry.isDirectory)
				ctx->entries->push_back(std::move(entry));
		} else {
			if (entry.isDirectory || s_showAllFiles || IsSupportedExtension(entry.name.c_str()))
				ctx->entries->push_back(std::move(entry));
		}

		return SDL_ENUM_CONTINUE;
	};

	SDL_EnumerateDirectory(dirU8.c_str(), callback, &ctx);
	std::sort(s_fileBrowserEntries.begin(), s_fileBrowserEntries.end());
	s_fileBrowserNeedsRefresh = false;
}

// -------------------------------------------------------------------------
// Init
// -------------------------------------------------------------------------

void ATMobileUI_Init() {
	// Enable ImGui gamepad nav once at startup.  Idempotent.
	ATMobileGamepad_Init();

	// 1) Restore last-used browser dir from registry, if any.
	VDStringW saved = LoadFileBrowserDir();
	if (!saved.empty()) {
		s_fileBrowserDir = saved;
	} else {
#ifdef __ANDROID__
		// Prefer the public Downloads dir via Environment — this is the
		// same path users see when they drop files onto the phone via
		// ADB, Files app, or a file manager.  Chain through
		// SDL_GetUserFolder as an API-33+ fallback (SAF URIs), then
		// the app-private external dir, then config.
		const char *dl = ATAndroid_GetPublicDownloadsDir();
		if (dl && *dl) {
			s_fileBrowserDir = VDTextU8ToW(VDStringA(dl));
		} else {
			const char *dl2 = SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS);
			if (dl2 && *dl2) {
				s_fileBrowserDir = VDTextU8ToW(VDStringA(dl2));
			} else {
				const char *ext = SDL_GetAndroidExternalStoragePath();
				if (ext && *ext)
					s_fileBrowserDir = VDTextU8ToW(VDStringA(ext));
				else
					s_fileBrowserDir = VDTextU8ToW(ATGetConfigDir());
			}
		}
#else
		const char *home = SDL_GetUserFolder(SDL_FOLDER_HOME);
		if (home)
			s_fileBrowserDir = VDTextU8ToW(VDStringA(home));
		else
			s_fileBrowserDir = L"/";
#endif
	}
	s_fileBrowserNeedsRefresh = true;
}

bool ATMobileUI_IsFirstRun() {
	return !IsFirstRunComplete();
}

// Load persisted mobile config into a state object.  Exposed for
// main_sdl3.cpp to call right after creating g_mobileState so the
// settings are in place before the first layout computation.
void ATMobileUI_LoadConfig(ATMobileUIState &mobileState) {
	LoadMobileConfig(mobileState);
}

// Save persisted mobile config.  Called from the Settings panel on
// change and on explicit shutdown.
void ATMobileUI_SaveConfig(const ATMobileUIState &mobileState) {
	SaveMobileConfig(mobileState);
}

// ATMobileUI_ApplyVisualEffects + ATMobileUI_ApplyPerformancePreset
// have moved to mobile_visual_effects.cpp.

// Force the file browser to re-enumerate next frame.  Used after
// returning from the Android Settings app so any newly-granted
// "All files access" permission is reflected immediately.
void ATMobileUI_InvalidateFileBrowser() {
	s_fileBrowserNeedsRefresh = true;
}

// Suspend save-state: see mobile_suspend.cpp.

// -------------------------------------------------------------------------
// Menu button polling
// -------------------------------------------------------------------------

extern bool s_menuTapped;

static bool ConsumeMenuTap() {
	if (s_menuTapped) {
		s_menuTapped = false;
		return true;
	}
	return false;
}

// -------------------------------------------------------------------------
// Hamburger menu
// -------------------------------------------------------------------------

static bool s_wasPausedBeforeMenu = false;

void ATMobileUI_OpenMenu(ATSimulator &sim, ATMobileUIState &mobileState) {
	s_wasPausedBeforeMenu = sim.IsPaused();
	sim.Pause();
	ATTouchControls_ReleaseAll();
	mobileState.currentScreen = ATMobileUIScreen::HamburgerMenu;
}

void ATMobileUI_CloseMenu(ATSimulator &sim, ATMobileUIState &mobileState) {
	mobileState.currentScreen = ATMobileUIScreen::None;
	if (!s_wasPausedBeforeMenu)
		sim.Resume();
}


// -------------------------------------------------------------------------
// File browser
// -------------------------------------------------------------------------


// -------------------------------------------------------------------------
// Settings
// -------------------------------------------------------------------------


// -------------------------------------------------------------------------
// Mobile Disk Drive Manager — full-screen touch-first replacement for
// the desktop ATUIRenderDiskManager dialog.  Large 96dp rows per
// drive with clear Mount/Eject buttons, shows D1:-D4: by default and
// reveals D5:-D15: behind a disclosure.
// -------------------------------------------------------------------------


// -------------------------------------------------------------------------
// Mobile About panel — full-screen replacement for the desktop
// `About AltirraSDL` dialog (which is sized 420×220 px and looks
// tiny on a phone).  Centered title/subtitle, scrollable credits,
// and a big Close button that returns to the hamburger menu.
// -------------------------------------------------------------------------


// -------------------------------------------------------------------------
// Main render entry point
// -------------------------------------------------------------------------

void ATMobileUI_Render(ATSimulator &sim, ATUIState &uiState,
	ATMobileUIState &mobileState, SDL_Window *window)
{
	// Cache content scale for dp() helper, applying user's interface
	// scale preference on top of the physical DPI factor.
	static constexpr float kInterfaceScaleFactors[] = { 0.75f, 1.0f, 1.25f };
	int scaleIdx = mobileState.interfaceScale;
	if (scaleIdx < 0 || scaleIdx > 2) scaleIdx = 1;
	float userScale = kInterfaceScaleFactors[scaleIdx];
	s_contentScale = mobileState.layoutConfig.contentScale * userScale;

	ImGuiIO &io = ImGui::GetIO();
	io.FontGlobalScale = userScale;

	int w, h;
	SDL_GetWindowSize(window, &w, &h);

	// Query Android safe-area insets (status bar, nav bar, cutout).
	// Zero on desktop.  Cached — only re-queried when the window was
	// resized (insets cache is invalidated from main_sdl3.cpp).
#ifdef __ANDROID__
	ATSafeInsets androidInsets = ATAndroid_GetSafeInsets();
	ATTouchLayoutInsets insets;
	insets.top    = androidInsets.top;
	insets.bottom = androidInsets.bottom;
	insets.left   = androidInsets.left;
	insets.right  = androidInsets.right;
#else
	ATTouchLayoutInsets insets;
#endif

	// Update layout if screen size, config, or insets changed
	if (w != mobileState.layout.screenW || h != mobileState.layout.screenH
		|| mobileState.layoutConfig.controlSize != mobileState.layout.lastControlSize
		|| mobileState.layoutConfig.contentScale != mobileState.layout.lastContentScale
		|| insets.top    != mobileState.layout.lastInsets.top
		|| insets.bottom != mobileState.layout.lastInsets.bottom
		|| insets.left   != mobileState.layout.lastInsets.left
		|| insets.right  != mobileState.layout.lastInsets.right)
	{
		ATTouchLayout_Update(mobileState.layout, w, h, mobileState.layoutConfig, insets);
	}

	// First run: show welcome wizard on top of everything until the user
	// picks ROMs or skips.  The wizard sets the flag itself.
	// On desktop, the setup wizard may have already run (checking
	// "ShownSetupWizard") — skip the mobile first-run in that case.
	if (mobileState.currentScreen == ATMobileUIScreen::None
		&& !IsFirstRunComplete())
	{
		VDRegistryAppKey appKey;
		if (appKey.getBool("ShownSetupWizard", false))
			SetFirstRunComplete();
		else
			mobileState.currentScreen = ATMobileUIScreen::FirstRunWizard;
	}

	// Check for menu button tap
	if (ConsumeMenuTap() && mobileState.currentScreen == ATMobileUIScreen::None)
		ATMobileUI_OpenMenu(sim, mobileState);

	// Tell the joystick layer whether the gamepad belongs to the UI
	// this frame.  Also factors in the modal sheet so dialogs that
	// pop up over the emulator (e.g. confirm) capture the gamepad.
	const bool uiOwns = (mobileState.currentScreen != ATMobileUIScreen::None)
		|| s_infoModalOpen || s_confirmActive;
	ATMobileGamepad_SetUIOwning(uiOwns);

	switch (mobileState.currentScreen) {
	case ATMobileUIScreen::None:
		// Render touch controls overlay — but hide them when the virtual
		// keyboard is visible so they don't overlap the keyboard keys,
		// or when the user has disabled them (default on desktop).
		if (mobileState.showTouchControls && !uiState.showVirtualKeyboard)
			ATTouchControls_Render(mobileState.layout, mobileState.layoutConfig);

		// If no game loaded, show a styled centered "Load Game" button
		if (!mobileState.gameLoaded)
			RenderLoadGamePrompt(sim, uiState, mobileState);

		// When touch controls are hidden (desktop default), render a
		// small ImGui menu button so mouse users can open the hamburger.
		// Touch users have the on-screen hamburger icon instead.
		if (!mobileState.showTouchControls) {
			float btnH = dp(36.0f);
			float padX = dp(12.0f);
			float btnW = ImGui::CalcTextSize("Menu").x + padX * 2.0f;
			float border = 1.0f;
			float winW = btnW + border * 2.0f;
			float winH = btnH + border * 2.0f;
			float margin = dp(8.0f);
			float insetT = (float)mobileState.layout.insets.top;
			float insetR = (float)mobileState.layout.insets.right;
			ImGui::SetNextWindowPos(
				ImVec2(io.DisplaySize.x - insetR - winW - margin,
					insetT + margin));
			ImGui::SetNextWindowSize(ImVec2(winW, winH));
			ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.12f, 0.18f, 0.80f));
			ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.3f, 0.4f, 0.6f, 0.5f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, dp(6.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, border);
			ImGui::Begin("##DesktopMenuBtn", nullptr,
				ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
				| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
				| ImGuiWindowFlags_NoScrollbar);
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.15f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.25f));
			if (ImGui::Button("Menu", ImVec2(btnW, btnH)))
				ATMobileUI_OpenMenu(sim, mobileState);
			ImGui::PopStyleColor(3);
			ImGui::End();
			ImGui::PopStyleVar(3);
			ImGui::PopStyleColor(2);
		}
		break;

	case ATMobileUIScreen::HamburgerMenu:
		RenderHamburgerMenu(sim, uiState, mobileState, window);
		break;

	case ATMobileUIScreen::FileBrowser:
		RenderFileBrowser(sim, uiState, mobileState, window);
		break;

	case ATMobileUIScreen::Settings:
		RenderSettings(sim, uiState, mobileState, window);
		break;

	case ATMobileUIScreen::FirstRunWizard:
		RenderFirstRunWizard(sim, uiState, mobileState, window);
		break;

	case ATMobileUIScreen::About:
		RenderMobileAbout(sim, uiState, mobileState, window);
		break;

	case ATMobileUIScreen::DiskManager:
		RenderMobileDiskManager(sim, uiState, mobileState, window);
		break;

	case ATMobileUIScreen::GameBrowser:
		RenderGameBrowser(sim, uiState, mobileState, window);
		break;
	}

	// Global mobile dialog sheet — implementation in mobile_dialogs.cpp.
	RenderMobileModalSheet(mobileState);
}

// -------------------------------------------------------------------------
// Event handling
// -------------------------------------------------------------------------

// Fingers that began their touch sequence on the in-game overlay (touch
// controls, console buttons, hamburger button).  We must continue to
// swallow MOTION/UP events for these fingers even after a screen
// transition (e.g. menu opens), otherwise ImGui sees the synthetic
// FINGER_UP and treats it as a click on whatever item happens to be
// under the finger — typically the close button or the first menu row.
namespace {
	constexpr int kMaxOwnedFingers = 8;
	SDL_FingerID s_gameOwnedFingers[kMaxOwnedFingers] = {};
	int s_gameOwnedCount = 0;

	bool IsGameOwnedFinger(SDL_FingerID id) {
		for (int i = 0; i < s_gameOwnedCount; ++i)
			if (s_gameOwnedFingers[i] == id) return true;
		return false;
	}
	void AddGameOwnedFinger(SDL_FingerID id) {
		if (IsGameOwnedFinger(id)) return;
		if (s_gameOwnedCount < kMaxOwnedFingers)
			s_gameOwnedFingers[s_gameOwnedCount++] = id;
	}
	void RemoveGameOwnedFinger(SDL_FingerID id) {
		for (int i = 0; i < s_gameOwnedCount; ++i) {
			if (s_gameOwnedFingers[i] == id) {
				s_gameOwnedFingers[i] = s_gameOwnedFingers[--s_gameOwnedCount];
				return;
			}
		}
	}
}

bool ATMobileUI_HandleEvent(const SDL_Event &ev, ATMobileUIState &mobileState) {
	const bool isFinger =
		ev.type == SDL_EVENT_FINGER_DOWN ||
		ev.type == SDL_EVENT_FINGER_MOTION ||
		ev.type == SDL_EVENT_FINGER_UP;

	// Continue swallowing any in-flight finger that originated on the
	// game-side touch layer, regardless of which screen is now active.
	if (isFinger && IsGameOwnedFinger(ev.tfinger.fingerID)) {
		// Forward to touch_controls so it can run its own release
		// bookkeeping for the joystick / console / fire / menu
		// buttons.  Ignored if the screen is no longer the gameplay
		// overlay — the handler is robust to that.
		ATTouchControls_HandleEvent(ev, mobileState.layout, mobileState.layoutConfig);
		if (ev.type == SDL_EVENT_FINGER_UP)
			RemoveGameOwnedFinger(ev.tfinger.fingerID);
		return true;
	}

	// If a menu/dialog is open, let ImGui handle everything (it
	// receives the synthetic touch mouse stream from the SDL3 backend).
	if (mobileState.currentScreen != ATMobileUIScreen::None)
		return false;

	// Route touch events to virtual keyboard first (if visible), then touch controls.
	// When the virtual keyboard is showing, touch controls are hidden so we
	// don't forward events to them — the keyboard owns the entire touch surface.
	if (isFinger) {
		if (g_uiState.showVirtualKeyboard) {
			ATUIVirtualKeyboard_HandleEvent(ev, g_sim, true);
			// Consume all touch events when keyboard is visible to prevent
			// them from reaching touch controls (which are hidden).
			return true;
		}

		if (!mobileState.showTouchControls)
			return false;

		bool consumed = ATTouchControls_HandleEvent(ev, mobileState.layout, mobileState.layoutConfig);
		if (consumed && ev.type == SDL_EVENT_FINGER_DOWN)
			AddGameOwnedFinger(ev.tfinger.fingerID);
		return consumed;
	}

	return false;
}

void ATMobileUI_OpenFileBrowser(ATMobileUIState &mobileState) {
	s_romFolderMode = false;
	mobileState.currentScreen = ATMobileUIScreen::FileBrowser;
	s_fileBrowserNeedsRefresh = true;
#ifdef __ANDROID__
	// Lazy permission request — only the first time the user actually
	// needs storage access.
	if (!IsPermissionAsked()) {
		ATAndroid_RequestStoragePermission();
		SetPermissionAsked();
	}
#endif
}

void ATMobileUI_OpenSettings(ATMobileUIState &mobileState) {
	mobileState.currentScreen = ATMobileUIScreen::Settings;
}
