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
#include <at/atcore/media.h>
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

extern ATSimulator g_sim;
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

void LoadMobileConfig(ATMobileUIState &mobileState) {
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
}

void SaveMobileConfig(const ATMobileUIState &mobileState) {
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
	}
	// Persist immediately — registry-only writes are lost if the user
	// swipes the app away from recents before it backgrounds properly.
	ATRegistryFlushToDisk();
}

// Path to the quick save-state file under the config dir.
// Kept as VDStringW because simulator.SaveState takes wchar_t*.
static VDStringW QuickSaveStatePath() {
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

VDStringW LoadFileBrowserDir() {
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

} // namespace

// Firmware scan function from ui_firmware.cpp
extern void ExecuteFirmwareScan(ATFirmwareManager *fwm, const VDStringW &scanDir);

// -------------------------------------------------------------------------
// dp helper — converts density-independent pixels to physical pixels
// -------------------------------------------------------------------------

static float s_contentScale = 1.0f;

static float dp(float v) { return v * s_contentScale; }

// -------------------------------------------------------------------------
// File browser state
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

static std::vector<FileBrowserEntry> s_fileBrowserEntries;
static VDStringW s_fileBrowserDir;
static bool s_fileBrowserNeedsRefresh = true;

// ROM folder browser mode — when true, selecting a folder triggers firmware scan
static bool s_romFolderMode = false;
static VDStringW s_romDir;
static int s_romScanResult = -1;  // -1 = no scan yet, 0+ = number of ROMs found

// Disk-mount browser mode — when >= 0, picking a file in the browser
// mounts it into the specified drive index instead of booting.
// -1 means normal Load Game mode.
static int s_diskMountTargetDrive = -1;
static bool s_mobileShowAllDrives = false;

// Generic modal info popup — every destructive / long-running action
// in the mobile UI should give the user explicit feedback.  This is a
// tiny system: set s_infoModalTitle/Body, the main render pass shows
// the modal, OK dismisses it.
static VDStringA s_infoModalTitle;
static VDStringA s_infoModalBody;
static bool      s_infoModalOpen = false;

static void ShowInfoModal(const char *title, const char *body) {
	s_infoModalTitle = title ? title : "";
	s_infoModalBody  = body ? body : "";
	s_infoModalOpen  = true;
}

// Confirm dialog — reuses the same mobile sheet renderer as the
// info modal but shows Cancel + Confirm buttons and fires a
// std::function on confirm.  Used by destructive hamburger actions
// (Cold/Warm Reset, Quick Save/Load).
#include <functional>
static VDStringA s_confirmTitle;
static VDStringA s_confirmBody;
static std::function<void()> s_confirmAction;
static bool      s_confirmActive = false;

static void ShowConfirmDialog(const char *title, const char *body,
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

// Hierarchical settings — which sub-page is currently being shown.
// Local to ui_mobile.cpp so we don't have to pollute the public
// ATMobileUIScreen enum with per-page entries.
enum class ATMobileSettingsPage {
	Home,
	Machine,
	Display,
	Performance,
	Controls,
	SaveState,
	Firmware,
};
static ATMobileSettingsPage s_settingsPage = ATMobileSettingsPage::Home;

// Firmware slot currently being picked within the Firmware sub-page.
// File scope so the header back button can close the picker.
static ATFirmwareType s_fwPicker = kATFirmwareType_Unknown;

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
		L"obx", L"com", L"exe",
		nullptr
	};

	for (const wchar_t **p = kExtensions; *p; ++p) {
		if (extLower == *p)
			return true;
	}
	return false;
}

static void RefreshFileBrowser(const VDStringW &dir) {
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

		// In ROM folder mode, only show directories
		if (ctx->romMode) {
			if (entry.isDirectory)
				ctx->entries->push_back(std::move(entry));
		} else {
			if (entry.isDirectory || IsSupportedExtension(entry.name.c_str()))
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

// Push the three visual-effect toggles into the GTIA's
// ATArtifactingParams + scanlines flag.  Safe to call on any
// display backend — if the backend doesn't support GPU screen FX,
// the params are still stored but SyncScreenFXToBackend in
// main_sdl3.cpp skips the push (see main_sdl3.cpp:975).  Scanlines
// work in both the CPU and GL paths.
void ATMobileUI_ApplyVisualEffects(const ATMobileUIState &mobileState) {
	ATGTIAEmulator &gtia = g_sim.GetGTIA();

	// Scanlines toggle: GPU-accelerated in GL, CPU fallback otherwise.
	gtia.SetScanlinesEnabled(mobileState.fxScanlines);

	// Read current params, tweak the three fields we care about,
	// leave everything else at the user's/default values.
	ATArtifactingParams params = gtia.GetArtifactingParams();

	// Bloom: pushed hard enough that the effect is obvious on a
	// phone LCD.  The default-constructed params leave radius/
	// intensity at zero so mbEnableBloom alone does nothing visible.
	params.mbEnableBloom = mobileState.fxBloom;
	if (mobileState.fxBloom) {
		params.mBloomRadius            = 8.0f;
		params.mBloomDirectIntensity   = 1.00f;
		params.mBloomIndirectIntensity = 0.80f;
	} else {
		params.mBloomRadius            = 0.0f;
		params.mBloomDirectIntensity   = 0.0f;
		params.mBloomIndirectIntensity = 0.0f;
	}

	if (mobileState.fxDistortion) {
		// Noticeable barrel distortion — larger than the previous
		// "subtle" value so the curvature is actually visible on a
		// 6" phone screen without looking cartoonish.
		params.mDistortionViewAngleX = 85.0f;
		params.mDistortionYRatio     = 0.50f;
	} else {
		params.mDistortionViewAngleX = 0.0f;
		params.mDistortionYRatio     = 0.0f;
	}

	gtia.SetArtifactingParams(params);
}

// Apply a bundled performance preset.  Efficient turns everything
// off and picks the cheapest filter.  Balanced keeps effects off
// but uses a nicer filter.  Quality enables all three CRT effects.
// Custom (3) is a no-op so the user's manual tweaks stay put.
void ATMobileUI_ApplyPerformancePreset(ATMobileUIState &mobileState) {
	int p = mobileState.performancePreset;
	if (p < 0 || p >= 3) return;  // Custom or out of range

	ATDisplayFilterMode filter = kATDisplayFilterMode_Bilinear;
	bool fastBoot       = true;   // always on — near-free speed win
	bool interlace      = false;  // extra frame work; off except Quality
	bool nonlinearMix   = true;   // POKEY quality
	bool audioMonitor   = false;  // profiler overhead, off everywhere
	bool driveSounds    = false;  // extra audio mixing; off except Quality

	switch (p) {
	case 0: // Efficient
		mobileState.fxScanlines  = false;
		mobileState.fxBloom      = false;
		mobileState.fxDistortion = false;
		filter        = kATDisplayFilterMode_Point;
		fastBoot      = true;
		interlace     = false;
		nonlinearMix  = false;   // cheaper linear mix
		driveSounds   = false;
		break;
	case 1: // Balanced
		mobileState.fxScanlines  = false;
		mobileState.fxBloom      = false;
		mobileState.fxDistortion = false;
		filter        = kATDisplayFilterMode_Bilinear;
		fastBoot      = true;
		interlace     = false;
		nonlinearMix  = true;
		driveSounds   = false;
		break;
	case 2: // Quality
		mobileState.fxScanlines  = true;
		mobileState.fxBloom      = true;
		mobileState.fxDistortion = true;
		filter        = kATDisplayFilterMode_SharpBilinear;
		fastBoot      = false;   // authentic boot timing
		interlace     = true;    // high-res interlace for games that use it
		nonlinearMix  = true;
		driveSounds   = true;
		break;
	}

	ATUISetDisplayFilterMode(filter);
	ATMobileUI_ApplyVisualEffects(mobileState);

	// Simulator-side knobs — match the Windows defaults where they
	// exist and fall back to the cheapest option elsewhere.
	g_sim.SetFastBootEnabled(fastBoot);
	g_sim.GetGTIA().SetInterlaceEnabled(interlace);
	g_sim.GetPokey().SetNonlinearMixingEnabled(nonlinearMix);
	g_sim.SetAudioMonitorEnabled(audioMonitor);
	ATUISetDriveSoundsEnabled(driveSounds);
}

// Force the file browser to re-enumerate next frame.  Used after
// returning from the Android Settings app so any newly-granted
// "All files access" permission is reflected immediately.
void ATMobileUI_InvalidateFileBrowser() {
	s_fileBrowserNeedsRefresh = true;
}

// -------------------------------------------------------------------------
// Suspend save-state
//
// Android can terminate a backgrounded app at any time.  To make the
// emulator feel like a native console handheld (flip open, keep
// playing), we snapshot the simulator to disk whenever the app goes
// to background or is about to terminate, and restore it on next
// launch.  Both halves of the feature are user-toggleable under the
// mobile Settings panel.
// -------------------------------------------------------------------------

void ATMobileUI_SaveSuspendState(ATSimulator &sim,
	const ATMobileUIState &mobileState)
{
	if (!mobileState.autoSaveOnSuspend)
		return;
	if (!mobileState.gameLoaded) {
		// Nothing worth saving — remove any stale file so a later
		// restore doesn't load a session from a different game.
		ATMobileUI_ClearSuspendState();
		return;
	}
	VDStringW path = QuickSaveStatePath();
	try {
		sim.SaveState(path.c_str());
	} catch (const MyError &e) {
		// Non-fatal — just log.  We don't want suspend to fail because
		// a save-state write had a disk error.
		VDStringA u8 = VDTextWToU8(path);
		fprintf(stderr, "[mobile] SaveState(%s) failed: %s\n",
			u8.c_str(), e.c_str());
	} catch (...) {
		fprintf(stderr, "[mobile] SaveState threw unknown exception\n");
	}
}

bool ATMobileUI_RestoreSuspendState(ATSimulator &sim,
	ATMobileUIState &mobileState)
{
	if (!mobileState.autoRestoreOnStart)
		return false;
	VDStringW path = QuickSaveStatePath();
	if (!VDDoesPathExist(path.c_str()))
		return false;

	try {
		ATImageLoadContext ctx{};
		if (sim.Load(path.c_str(), kATMediaWriteMode_RO, &ctx)) {
			// Match Windows behaviour: a save-state load suppresses
			// the cold reset that Load() would otherwise perform.
			sim.Resume();
			mobileState.gameLoaded = true;
			return true;
		}
	} catch (const MyError &e) {
		VDStringA u8 = VDTextWToU8(path);
		fprintf(stderr, "[mobile] LoadState(%s) failed: %s\n",
			u8.c_str(), e.c_str());
		// Corrupt snapshot — remove it so we don't keep crashing.
		ATMobileUI_ClearSuspendState();
	} catch (...) {
		fprintf(stderr, "[mobile] LoadState threw unknown exception\n");
		ATMobileUI_ClearSuspendState();
	}
	return false;
}

void ATMobileUI_ClearSuspendState() {
	VDStringW path = QuickSaveStatePath();
	if (VDDoesPathExist(path.c_str()))
		VDRemoveFile(path.c_str());
}

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

static void RenderHamburgerMenu(ATSimulator &sim, ATUIState &uiState,
	ATMobileUIState &mobileState, SDL_Window *window)
{
	ImGuiIO &io = ImGui::GetIO();
	float menuW = io.DisplaySize.x * 0.65f;
	float minW = dp(280.0f);
	float maxW = dp(400.0f);
	if (menuW < minW) menuW = minW;
	if (menuW > maxW) menuW = maxW;

	// Dim background
	ImGui::GetBackgroundDrawList()->AddRectFilled(
		ImVec2(0, 0), io.DisplaySize,
		IM_COL32(0, 0, 0, 128));

	// Menu panel (slides from right), inset inside safe area so the
	// title bar isn't eaten by the status bar and the last item isn't
	// hidden by the nav bar.
	float insetT = (float)mobileState.layout.insets.top;
	float insetB = (float)mobileState.layout.insets.bottom;
	float insetR = (float)mobileState.layout.insets.right;
	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - insetR - menuW, insetT));
	ImGui::SetNextWindowSize(ImVec2(menuW, io.DisplaySize.y - insetT - insetB));

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

	if (ImGui::Begin("##MobileMenu", nullptr, flags)) {
		// Install touch-drag scrolling for the hamburger panel so a
		// short phone or landscape orientation can still reach all
		// the menu items.
		ATTouchDragScroll();

		// Title bar with close button
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + dp(8.0f));
		ImGui::Text("Altirra");
		ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - dp(32.0f));
		if (ImGui::Button("X", ImVec2(dp(32.0f), dp(32.0f))))
			ATMobileUI_CloseMenu(sim, mobileState);

		ImGui::Separator();
		ImGui::Spacing();

		// Menu button height scaled for touch
		float btnH = dp(56.0f);
		ImVec2 btnSize(-1, btnH);

		// Resume
		if (ImGui::Button("Resume", btnSize))
			ATMobileUI_CloseMenu(sim, mobileState);
		ImGui::Spacing();

		// Load Game
		if (ImGui::Button("Load Game", btnSize)) {
			s_romFolderMode = false;
			mobileState.currentScreen = ATMobileUIScreen::FileBrowser;
			s_fileBrowserNeedsRefresh = true;
		}
		ImGui::Spacing();

		// Disk Drives — mobile-friendly full-screen manager
		if (ImGui::Button("Disk Drives", btnSize)) {
			mobileState.currentScreen = ATMobileUIScreen::DiskManager;
		}
		ImGui::Spacing();

		// Audio toggle
		{
			const char *audioLabel = mobileState.audioMuted ? "Audio: OFF" : "Audio: ON";
			if (ImGui::Button(audioLabel, btnSize)) {
				mobileState.audioMuted = !mobileState.audioMuted;
				IATAudioOutput *audioOut = g_sim.GetAudioOutput();
				if (audioOut)
					audioOut->SetMute(mobileState.audioMuted);
			}
		}
		ImGui::Spacing();

		ImGui::Separator();
		ImGui::Spacing();

		// Quick Save State — with confirmation to prevent accidental
		// overwrite of an earlier checkpoint.
		if (ImGui::Button("Quick Save State", btnSize)) {
			ShowConfirmDialog("Quick Save State",
				"Overwrite the current quick save with the "
				"emulator's state right now?",
				[&mobileState]() {
					try {
						VDStringW path = QuickSaveStatePath();
						g_sim.SaveState(path.c_str());
						ShowInfoModal("Saved",
							"Emulator state saved.");
					} catch (const MyError &e) {
						ShowInfoModal("Save Failed", e.c_str());
					}
				});
		}
		ImGui::Spacing();

		// Quick Load State — confirmation, with a distinct info
		// dialog if no save is available.
		if (ImGui::Button("Quick Load State", btnSize)) {
			VDStringW path = QuickSaveStatePath();
			if (!VDDoesPathExist(path.c_str())) {
				ShowInfoModal("No Quick Save",
					"There is no quick save available to load.");
			} else {
				ShowConfirmDialog("Quick Load State",
					"Replace the current emulator state with the "
					"quick save?  Any unsaved progress will be lost.",
					[&sim, &mobileState]() {
						VDStringW p = QuickSaveStatePath();
						try {
							ATImageLoadContext ctx{};
							if (sim.Load(p.c_str(),
								kATMediaWriteMode_RO, &ctx))
							{
								sim.Resume();
								mobileState.gameLoaded = true;
								ShowInfoModal("Loaded",
									"Emulator state restored.");
							}
						} catch (const MyError &e) {
							ShowInfoModal("Load Failed", e.c_str());
						}
					});
			}
		}
		ImGui::Spacing();

		ImGui::Separator();
		ImGui::Spacing();

		// Warm Reset — with confirmation.
		if (ImGui::Button("Warm Reset", btnSize)) {
			ShowConfirmDialog("Warm Reset",
				"Reset the emulator without clearing memory?",
				[&sim, &mobileState]() {
					sim.WarmReset();
					ATMobileUI_CloseMenu(sim, mobileState);
					sim.Resume();
				});
		}
		ImGui::Spacing();

		// Cold Reset — with confirmation.
		if (ImGui::Button("Cold Reset", btnSize)) {
			ShowConfirmDialog("Cold Reset",
				"Power-cycle the emulator?  This clears RAM and "
				"reboots, just like unplugging the machine.",
				[&sim, &mobileState]() {
					sim.ColdReset();
					ATMobileUI_CloseMenu(sim, mobileState);
					sim.Resume();
				});
		}
		ImGui::Spacing();

		ImGui::Separator();
		ImGui::Spacing();

		// Settings
		if (ImGui::Button("Settings", btnSize)) {
			s_settingsPage = ATMobileSettingsPage::Home;
			mobileState.currentScreen = ATMobileUIScreen::Settings;
		}
		ImGui::Spacing();

		ImGui::Separator();
		ImGui::Spacing();

		// About — mobile-friendly full-screen panel
		if (ImGui::Button("About", btnSize)) {
			mobileState.currentScreen = ATMobileUIScreen::About;
		}
	}
	ImGui::End();

	// Tap outside menu panel to close
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		ImVec2 mousePos = ImGui::GetMousePos();
		if (mousePos.x < io.DisplaySize.x - insetR - menuW)
			ATMobileUI_CloseMenu(sim, mobileState);
	}
}

// -------------------------------------------------------------------------
// File browser
// -------------------------------------------------------------------------

static void NavigateUp() {
	// Never leave the filesystem tree.  If we're at "/" there's nothing
	// to do and calling this would corrupt s_fileBrowserDir.
	if (s_fileBrowserDir.empty() || s_fileBrowserDir == L"/")
		return;

	VDStringW parent = s_fileBrowserDir;
	while (!parent.empty() && parent.back() == L'/')
		parent.pop_back();
	for (size_t i = parent.size(); i > 0; --i) {
		if (parent[i - 1] == L'/') {
			if (i > 1)
				parent.resize(i - 1);
			else
				parent.resize(1);  // keep root "/"
			s_fileBrowserDir = parent;
			s_fileBrowserNeedsRefresh = true;
			SaveFileBrowserDir(s_fileBrowserDir);
			return;
		}
	}
	// No '/' found — we were at something weird like "relative".
	// Fall back to the public Downloads dir so the user can recover.
	s_fileBrowserDir = L"/";
	s_fileBrowserNeedsRefresh = true;
	SaveFileBrowserDir(s_fileBrowserDir);
}

// Jump to a well-known directory.  Used by the shortcut buttons so a
// user who has navigated into an empty/dead folder can always get back
// to somewhere useful.
static void JumpToDirectory(const char *u8path) {
	s_fileBrowserDir = VDTextU8ToW(VDStringA(u8path));
	s_fileBrowserNeedsRefresh = true;
	SaveFileBrowserDir(s_fileBrowserDir);
}

static void RenderFileBrowser(ATSimulator &sim, ATUIState &uiState,
	ATMobileUIState &mobileState, SDL_Window *window)
{
#ifdef __ANDROID__
	// Lazy permission request — only fires the pre-API-30 runtime
	// dialog.  On API 30+ this is a no-op because we rely on
	// MANAGE_EXTERNAL_STORAGE, which requires a Settings page visit,
	// handled via a dedicated banner below.
	if (!IsPermissionAsked()) {
		ATAndroid_RequestStoragePermission();
		SetPermissionAsked();
	}
#endif
	if (s_fileBrowserNeedsRefresh)
		RefreshFileBrowser(s_fileBrowserDir);

	ImGuiIO &io = ImGui::GetIO();

	// Inset inside the safe area so the header and list don't clash
	// with the Android status bar or gesture nav.
	float insetT = (float)mobileState.layout.insets.top;
	float insetB = (float)mobileState.layout.insets.bottom;
	float insetL = (float)mobileState.layout.insets.left;
	float insetR = (float)mobileState.layout.insets.right;
	ImGui::SetNextWindowPos(ImVec2(insetL, insetT));
	ImGui::SetNextWindowSize(ImVec2(
		io.DisplaySize.x - insetL - insetR,
		io.DisplaySize.y - insetT - insetB));

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

	if (ImGui::Begin("##FileBrowser", nullptr, flags)) {
		// Header bar
		float headerH = dp(48.0f);
		ImVec2 backBtnSize(dp(48.0f), headerH);

		if (ImGui::Button("<", backBtnSize)) {
			if (s_diskMountTargetDrive >= 0) {
				// Cancelled disk mount — return to the disk manager.
				s_diskMountTargetDrive = -1;
				mobileState.currentScreen = ATMobileUIScreen::DiskManager;
			} else if (s_romFolderMode) {
				s_romFolderMode = false;
				mobileState.currentScreen = ATMobileUIScreen::Settings;
			} else {
				ATMobileUI_CloseMenu(sim, mobileState);
			}
		}
		ImGui::SameLine();

		const char *title;
		if (s_diskMountTargetDrive >= 0) {
			static char mountTitle[32];
			snprintf(mountTitle, sizeof(mountTitle),
				"Mount into D%d:", s_diskMountTargetDrive + 1);
			title = mountTitle;
		} else if (s_romFolderMode) {
			title = "Select Firmware Folder";
		} else {
			title = "Load Game";
		}
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (headerH - ImGui::GetTextLineHeight()) * 0.5f);
		ImGui::Text("%s", title);

		ImGui::Separator();

		// Current directory
		VDStringA dirU8 = VDTextWToU8(s_fileBrowserDir);
		ImGui::TextWrapped("%s", dirU8.c_str());

#ifdef __ANDROID__
		// Storage permission banner — if the user hasn't granted
		// "All files access" yet, show a prominent prompt explaining
		// the situation and offering a button that jumps straight to
		// the system Settings page.  This is the ONLY way to read
		// arbitrary .xex/.atr files outside the app-private directory
		// on Android 11+ (scoped storage) without going through SAF.
		if (!ATAndroid_HasStoragePermission()) {
			ImGui::PushStyleColor(ImGuiCol_ChildBg,
				ImVec4(0.30f, 0.12f, 0.12f, 0.85f));
			ImGui::BeginChild("PermBanner",
				ImVec2(0, dp(160.0f)), ImGuiChildFlags_Border);
			ImGui::Spacing();
			ImGui::TextColored(ImVec4(1, 1, 1, 1),
				"Storage access required");
			ImGui::TextWrapped(
				"To see ROM and disk image files in /sdcard/Download "
				"and other user folders, Altirra needs the system "
				"\"All files access\" permission.  This is a special "
				"permission you grant from Android Settings.");
			ImGui::Spacing();
			if (ImGui::Button("Open Settings to Grant Access",
				ImVec2(-1, dp(48.0f))))
			{
				ATAndroid_OpenManageStorageSettings();
				ShowInfoModal("Grant Access",
					"Find \"Altirra\" in the \"All files access\" "
					"list in Settings and enable it, then return "
					"to the app and the file list will refresh.");
			}
			ImGui::EndChild();
			ImGui::PopStyleColor();
			ImGui::Spacing();
		}
#endif

		// Quick-access shortcut bar — lets the user jump to common
		// locations from anywhere in the tree, so an accidental climb
		// into a filtered-empty folder is never a dead end.
		{
			float shortcutH = dp(40.0f);
			if (ImGui::Button("Downloads", ImVec2(dp(120.0f), shortcutH))) {
#ifdef __ANDROID__
				const char *dl = ATAndroid_GetPublicDownloadsDir();
				if (dl && *dl)
					JumpToDirectory(dl);
				else
					JumpToDirectory("/storage/emulated/0/Download");
#else
				const char *home = SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS);
				if (home && *home) JumpToDirectory(home);
#endif
			}
			ImGui::SameLine();
			if (ImGui::Button("Storage", ImVec2(dp(100.0f), shortcutH))) {
#ifdef __ANDROID__
				JumpToDirectory("/storage/emulated/0");
#else
				const char *home = SDL_GetUserFolder(SDL_FOLDER_HOME);
				if (home && *home) JumpToDirectory(home);
				else JumpToDirectory("/");
#endif
			}
			ImGui::SameLine();
			if (ImGui::Button("/", ImVec2(dp(50.0f), shortcutH))) {
				JumpToDirectory("/");
			}
		}

		// Navigation row: Up + (in ROM mode) "Select This Folder" button
		float rowBtnH = dp(48.0f);
		if (ImGui::Button(".. (Up)", ImVec2(dp(120.0f), rowBtnH)))
			NavigateUp();

		if (s_romFolderMode) {
			ImGui::SameLine();
			if (ImGui::Button("Use This Folder", ImVec2(-1, rowBtnH))) {
				// Trigger firmware scan on current directory
				s_romDir = s_fileBrowserDir;
				ATFirmwareManager *fwm = g_sim.GetFirmwareManager();
				ExecuteFirmwareScan(fwm, s_romDir);

				// Count detected firmware
				vdvector<ATFirmwareInfo> fwList;
				fwm->GetFirmwareList(fwList);
				s_romScanResult = (int)fwList.size();

				// Reload ROMs after scan so new firmware is active
				g_sim.LoadROMs();

				// Return to settings and show an info popup so the
				// user gets explicit feedback about the scan result.
				s_romFolderMode = false;
				mobileState.currentScreen = ATMobileUIScreen::Settings;

				VDStringA dirU8 = VDTextWToU8(s_romDir);
				char msg[1024];
				if (s_romScanResult > 0) {
					snprintf(msg, sizeof(msg),
						"Found %d ROM file%s in:\n\n%s\n\n"
						"These firmware images are now available.",
						s_romScanResult,
						s_romScanResult == 1 ? "" : "s",
						dirU8.c_str());
					ShowInfoModal("ROMs Imported", msg);
				} else {
					snprintf(msg, sizeof(msg),
						"No recognized Atari ROM files were found in:\n\n%s\n\n"
						"Altirra identifies firmware by content hash, so\n"
						"unmodified ROMs are detected automatically.\n"
						"The built-in replacement kernel is still available.",
						dirU8.c_str());
					ShowInfoModal("No ROMs Found", msg);
				}
			}
		}

		ImGui::Separator();

		// File/directory list
		// Touch scrolling: ImGui's default Selectable + child-scroll
		// interaction swallows the first press and highlights the row,
		// then a drag doesn't scroll because the child window only
		// scrolls when dragged in empty space.  We do two things:
		//   1) Manually scroll the list when the user drags anywhere
		//      inside the child, matching finger delta to scroll delta.
		//   2) Only treat a Selectable's "click" as an activation if
		//      the total drag distance stayed below a small threshold.
		// This gives natural touch-scroll behaviour while still letting
		// a tap select an item.
		float itemH = dp(56.0f);
		ImGui::BeginChild("FileList", ImVec2(0, 0), ImGuiChildFlags_None);

		// Install touch drag-scroll for this child window.  Shared
		// helper from touch_widgets.cpp — identical behaviour across
		// every scrollable surface in the mobile UI.
		ATTouchDragScroll();

		for (size_t i = 0; i < s_fileBrowserEntries.size(); i++) {
			const FileBrowserEntry &entry = s_fileBrowserEntries[i];
			VDStringA nameU8 = VDTextWToU8(VDStringW(entry.name));

			char label[512];
			if (entry.isDirectory)
				snprintf(label, sizeof(label), "[DIR] %s", nameU8.c_str());
			else
				snprintf(label, sizeof(label), "      %s", nameU8.c_str());

			ImGui::PushID((int)i);
			bool activated = ImGui::Selectable(label,
				(int)i == mobileState.selectedFileIdx,
				ImGuiSelectableFlags_AllowOverlap, ImVec2(0, itemH));

			// Suppress the activation if the finger moved — that was
			// a scroll drag, not a tap.
			if (activated && ATTouchIsDraggingBeyondSlop())
				activated = false;

			if (activated) {
				if (entry.isDirectory) {
					s_fileBrowserDir = entry.fullPath;
					s_fileBrowserNeedsRefresh = true;
					SaveFileBrowserDir(s_fileBrowserDir);
					mobileState.selectedFileIdx = -1;
				} else if (s_diskMountTargetDrive >= 0) {
					// Mount-into-drive path (from the mobile disk
					// manager).  Load the image into the target
					// drive and route back to the disk manager with
					// a status popup.
					int drive = s_diskMountTargetDrive;
					s_diskMountTargetDrive = -1;
					try {
						sim.GetDiskInterface(drive)
							.LoadDisk(entry.fullPath.c_str());
						VDStringA u8 = VDTextWToU8(entry.fullPath);
						char msg[1024];
						snprintf(msg, sizeof(msg),
							"Mounted into D%d:\n\n%s",
							drive + 1, u8.c_str());
						ShowInfoModal("Disk Mounted", msg);
					} catch (const MyError &e) {
						ShowInfoModal("Mount Failed", e.c_str());
					}
					mobileState.currentScreen = ATMobileUIScreen::DiskManager;
				} else if (!s_romFolderMode) {
					mobileState.selectedFileIdx = (int)i;
					VDStringA pathU8 = VDTextWToU8(VDStringW(entry.fullPath));
					ATUIPushDeferred(kATDeferred_BootImage, pathU8.c_str());
					mobileState.gameLoaded = true;
					ATMobileUI_CloseMenu(sim, mobileState);
					sim.Resume();
				}
			}
			ImGui::PopID();
		}

		ImGui::EndChild();
	}
	ImGui::End();
}

// -------------------------------------------------------------------------
// Settings
// -------------------------------------------------------------------------

static void RenderSettings(ATSimulator &sim, ATUIState &uiState,
	ATMobileUIState &mobileState, SDL_Window *window)
{
	ImGuiIO &io = ImGui::GetIO();

	float insetT = (float)mobileState.layout.insets.top;
	float insetB = (float)mobileState.layout.insets.bottom;
	float insetL = (float)mobileState.layout.insets.left;
	float insetR = (float)mobileState.layout.insets.right;
	ImGui::SetNextWindowPos(ImVec2(insetL, insetT));
	ImGui::SetNextWindowSize(ImVec2(
		io.DisplaySize.x - insetL - insetR,
		io.DisplaySize.y - insetT - insetB));

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

	if (ImGui::Begin("##MobileSettings", nullptr, flags)) {
		// Header — back arrow, title reflects current sub-page.
		float headerH = dp(48.0f);
		if (ImGui::Button("<", ImVec2(dp(48.0f), headerH))) {
			if (s_settingsPage == ATMobileSettingsPage::Firmware
				&& s_fwPicker != kATFirmwareType_Unknown)
			{
				s_fwPicker = kATFirmwareType_Unknown;
			} else if (s_settingsPage == ATMobileSettingsPage::Home) {
				mobileState.currentScreen = ATMobileUIScreen::HamburgerMenu;
			} else {
				s_settingsPage = ATMobileSettingsPage::Home;
			}
		}
		ImGui::SameLine();
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (headerH - ImGui::GetTextLineHeight()) * 0.5f);
		const char *pageTitle = "Settings";
		switch (s_settingsPage) {
		case ATMobileSettingsPage::Home:        pageTitle = "Settings"; break;
		case ATMobileSettingsPage::Machine:     pageTitle = "Machine"; break;
		case ATMobileSettingsPage::Display:     pageTitle = "Display"; break;
		case ATMobileSettingsPage::Performance: pageTitle = "Performance"; break;
		case ATMobileSettingsPage::Controls:    pageTitle = "Controls"; break;
		case ATMobileSettingsPage::SaveState:   pageTitle = "Save State"; break;
		case ATMobileSettingsPage::Firmware:    pageTitle = "Firmware"; break;
		}
		ImGui::Text("%s", pageTitle);

		ImGui::Separator();
		ImGui::Spacing();

		ImGui::BeginChild("SettingsScroll", ImVec2(0, 0), ImGuiChildFlags_None);
		ATTouchDragScroll();

		// --- Settings home: category list with subtitle previews ---
		if (s_settingsPage == ATMobileSettingsPage::Home) {
			auto hwLabel = [&](){
				switch (sim.GetHardwareMode()) {
				case kATHardwareMode_800:   return "400/800";
				case kATHardwareMode_800XL: return "800XL";
				case kATHardwareMode_130XE: return "130XE";
				case kATHardwareMode_5200:  return "5200";
				default: return "?";
				}
			};
			const char *vsLabel = (sim.GetVideoStandard() == kATVideoStandard_PAL) ? "PAL" : "NTSC";
			const char *presetLabel = "Balanced";
			switch (mobileState.performancePreset) {
			case 0: presetLabel = "Efficient"; break;
			case 1: presetLabel = "Balanced"; break;
			case 2: presetLabel = "Quality"; break;
			case 3: presetLabel = "Custom"; break;
			}

			struct CatRow {
				const char *title;
				VDStringA subtitle;
				ATMobileSettingsPage target;
			};
			CatRow cats[7];
			int n = 0;

			cats[n++] = { "Machine",
				VDStringA().sprintf("%s  \xC2\xB7  %s",
					hwLabel(), vsLabel),
				ATMobileSettingsPage::Machine };

			cats[n++] = { "Display",
				VDStringA("Filter, visual effects"),
				ATMobileSettingsPage::Display };

			cats[n++] = { "Performance",
				VDStringA().sprintf("Preset: %s", presetLabel),
				ATMobileSettingsPage::Performance };

			cats[n++] = { "Controls",
				VDStringA().sprintf("Size: %s  \xC2\xB7  Haptic: %s",
					mobileState.layoutConfig.controlSize == ATTouchControlSize::Small  ? "Small"  :
					mobileState.layoutConfig.controlSize == ATTouchControlSize::Large  ? "Large"  : "Medium",
					mobileState.layoutConfig.hapticEnabled ? "on" : "off"),
				ATMobileSettingsPage::Controls };

			cats[n++] = { "Save State",
				VDStringA().sprintf("Auto-save: %s  \xC2\xB7  Restore: %s",
					mobileState.autoSaveOnSuspend ? "on" : "off",
					mobileState.autoRestoreOnStart ? "on" : "off"),
				ATMobileSettingsPage::SaveState };

			cats[n++] = { "Firmware",
				s_romDir.empty()
					? VDStringA("(not set)")
					: VDStringA().sprintf("%s", VDTextWToU8(s_romDir).c_str()),
				ATMobileSettingsPage::Firmware };

			float rowH = dp(76.0f);
			for (int i = 0; i < n; ++i) {
				ImGui::PushID(i);
				ImVec2 cursor = ImGui::GetCursorScreenPos();
				float availW = ImGui::GetContentRegionAvail().x;
				ImDrawList *dl = ImGui::GetWindowDrawList();
				dl->AddRectFilled(cursor,
					ImVec2(cursor.x + availW, cursor.y + rowH),
					IM_COL32(30, 35, 50, 200), dp(10.0f));

				if (ImGui::InvisibleButton("##cat",
					ImVec2(availW, rowH)))
				{
					s_settingsPage = cats[i].target;
				}

				ImVec2 tcur(cursor.x + dp(16.0f), cursor.y + dp(12.0f));
				dl->AddText(tcur, IM_COL32(240, 242, 248, 255),
					cats[i].title);
				ImVec2 scur(cursor.x + dp(16.0f), cursor.y + dp(44.0f));
				dl->AddText(scur, IM_COL32(160, 175, 200, 255),
					cats[i].subtitle.c_str());

				// Right-side chevron
				ImVec2 chev(cursor.x + availW - dp(28.0f),
					cursor.y + rowH * 0.5f - dp(8.0f));
				dl->AddText(chev, IM_COL32(160, 175, 200, 255), ">");

				ImGui::Dummy(ImVec2(0, dp(10.0f)));
				ImGui::PopID();
			}

			ImGui::Dummy(ImVec2(0, dp(16.0f)));
			if (ImGui::Button("About", ImVec2(-1, dp(56.0f)))) {
				mobileState.currentScreen = ATMobileUIScreen::About;
			}

			ImGui::Dummy(ImVec2(0, dp(32.0f)));
			ImGui::EndChild();
			ImGui::End();
			return;
		}

		// --- Sub-page: Machine ---
		if (s_settingsPage == ATMobileSettingsPage::Machine) {
		ATTouchSection("Machine");

		// Hardware type.  All four modes work with the built-in HLE
		// kernel — no user-supplied ROMs required.  Changing the
		// mode triggers a cold reset inside the simulator.
		{
			static const struct {
				const char *label;
				ATHardwareMode mode;
			} kHw[] = {
				{ "400/800",    kATHardwareMode_800    },
				{ "600/800XL",  kATHardwareMode_800XL  },
				{ "130XE",      kATHardwareMode_130XE  },
				{ "5200",       kATHardwareMode_5200   },
			};
			constexpr int kNumHw = (int)(sizeof(kHw) / sizeof(kHw[0]));

			ATHardwareMode curMode = sim.GetHardwareMode();
			int curIdx = 1; // default 800XL
			for (int i = 0; i < kNumHw; ++i)
				if (kHw[i].mode == curMode) { curIdx = i; break; }

			static const char *labels[kNumHw] = {
				kHw[0].label, kHw[1].label, kHw[2].label, kHw[3].label,
			};
			if (ATTouchSegmented("Hardware", &curIdx, labels, kNumHw)) {
				sim.SetHardwareMode(kHw[curIdx].mode);
				sim.ColdReset();
			}
		}

		// Video Standard — PAL / NTSC
		{
			int current = (sim.GetVideoStandard() == kATVideoStandard_PAL) ? 0 : 1;
			static const char *items[] = { "PAL", "NTSC" };
			if (ATTouchSegmented("Video Standard", &current, items, 2))
				sim.SetVideoStandard(current == 0 ? kATVideoStandard_PAL : kATVideoStandard_NTSC);
		}

		// Memory Size
		{
			static const struct {
				const char *label;
				ATMemoryMode mode;
			} kMemModes[] = {
				{ "16K",   kATMemoryMode_16K   },
				{ "48K",   kATMemoryMode_48K   },
				{ "64K",   kATMemoryMode_64K   },
				{ "128K",  kATMemoryMode_128K  },
				{ "320K",  kATMemoryMode_320K  },
				{ "1088K", kATMemoryMode_1088K },
			};
			ATMemoryMode curMode = sim.GetMemoryMode();
			int curIdx = 4; // default 320K
			int count = (int)(sizeof(kMemModes)/sizeof(kMemModes[0]));
			for (int i = 0; i < count; i++) {
				if (kMemModes[i].mode == curMode) { curIdx = i; break; }
			}
			static const char *labels[6] = {
				kMemModes[0].label, kMemModes[1].label, kMemModes[2].label,
				kMemModes[3].label, kMemModes[4].label, kMemModes[5].label,
			};
			if (ATTouchSegmented("Memory Size", &curIdx, labels, count))
				sim.SetMemoryMode(kMemModes[curIdx].mode);
		}

		// BASIC toggle
		{
			bool basicEnabled = sim.IsBASICEnabled();
			if (ATTouchToggle("BASIC Enabled", &basicEnabled))
				sim.SetBASICEnabled(basicEnabled);
		}

		// SIO Patch toggle
		{
			bool sioEnabled = sim.IsSIOPatchEnabled();
			if (ATTouchToggle("SIO Patch", &sioEnabled))
				sim.SetSIOPatchEnabled(sioEnabled);
		}

		// ---- RANDOMIZATION (still on Machine page) ----
		ATTouchSection("Randomization");

		{
			bool randomLaunch = sim.IsRandomProgramLaunchDelayEnabled();
			if (ATTouchToggle("Randomize launch delay", &randomLaunch))
				sim.SetRandomProgramLaunchDelayEnabled(randomLaunch);
		}
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.70f, 0.78f, 1));
		ImGui::TextWrapped(
			"Delays program boot by a random number of cycles so "
			"POKEY's RNG seed varies between runs.  Default: on.");
		ImGui::PopStyleColor();

		{
			bool randomFill = sim.IsRandomFillEXEEnabled();
			if (ATTouchToggle("Randomize memory on EXE load", &randomFill))
				sim.SetRandomFillEXEEnabled(randomFill);
		}
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.70f, 0.78f, 1));
		ImGui::TextWrapped(
			"Fills uninitialised RAM with random bytes before a .xex "
			"program loads.  Helps flush out games that relied on "
			"specific power-on RAM patterns.  Default: off.");
		ImGui::PopStyleColor();
		} // end Machine page

		// --- Sub-page: Controls ---
		if (s_settingsPage == ATMobileSettingsPage::Controls) {
		ATTouchSection("Controls");

		// Joystick style
		{
			int js = (int)mobileState.layoutConfig.joystickStyle;
			static const char *styles[] = { "Analog", "D-Pad 8", "D-Pad 4" };
			if (ATTouchSegmented("Joystick Style", &js, styles, 3)) {
				mobileState.layoutConfig.joystickStyle = (ATTouchJoystickStyle)js;
				SaveMobileConfig(mobileState);
			}
		}

		// Control size
		{
			int sz = (int)mobileState.layoutConfig.controlSize;
			static const char *sizes[] = { "Small", "Medium", "Large" };
			if (ATTouchSegmented("Control Size", &sz, sizes, 3)) {
				mobileState.layoutConfig.controlSize = (ATTouchControlSize)sz;
				SaveMobileConfig(mobileState);
			}
		}

		// Control opacity — 10%-100%
		{
			int pct = (int)(mobileState.layoutConfig.controlOpacity * 100.0f + 0.5f);
			if (ATTouchSlider("Opacity", &pct, 10, 100, "%d%%")) {
				mobileState.layoutConfig.controlOpacity = pct / 100.0f;
				SaveMobileConfig(mobileState);
			}
		}

		// Haptic feedback
		if (ATTouchToggle("Haptic Feedback", &mobileState.layoutConfig.hapticEnabled)) {
			SaveMobileConfig(mobileState);
			ATTouchControls_SetHapticEnabled(mobileState.layoutConfig.hapticEnabled);
		}
		} // end Controls page

		// --- Sub-page: Save State ---
		if (s_settingsPage == ATMobileSettingsPage::SaveState) {
		ATTouchSection("Save State");

		if (ATTouchToggle("Auto-save on exit / background",
			&mobileState.autoSaveOnSuspend))
		{
			SaveMobileConfig(mobileState);
		}
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.70f, 0.78f, 1));
		ImGui::TextWrapped(
			"Snapshots the emulator whenever the app goes to "
			"background or is closed, so a swipe-away or an "
			"incoming call never loses progress.");
		ImGui::PopStyleColor();

		if (ATTouchToggle("Restore on startup",
			&mobileState.autoRestoreOnStart))
		{
			SaveMobileConfig(mobileState);
		}
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.70f, 0.78f, 1));
		ImGui::TextWrapped(
			"On launch, resume exactly where you left off "
			"(requires Auto-save above).");
		ImGui::PopStyleColor();

		ImGui::Spacing();

		// Manual save / load buttons — always available so the user
		// can checkpoint a run independently of the auto-save setting.
		float halfW = (ImGui::GetContentRegionAvail().x - dp(8.0f)) * 0.5f;
		if (ImGui::Button("Save State Now", ImVec2(halfW, dp(56.0f)))) {
			try {
				VDStringW path = QuickSaveStatePath();
				sim.SaveState(path.c_str());
				ShowInfoModal("Saved", "Emulator state saved.");
			} catch (const MyError &e) {
				ShowInfoModal("Save Failed", e.c_str());
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Load State Now", ImVec2(halfW, dp(56.0f)))) {
			VDStringW path = QuickSaveStatePath();
			if (!VDDoesPathExist(path.c_str())) {
				ShowInfoModal("No State", "No saved state available to load.");
			} else {
				try {
					ATImageLoadContext ctx{};
					if (sim.Load(path.c_str(), kATMediaWriteMode_RO, &ctx)) {
						sim.Resume();
						mobileState.gameLoaded = true;
						ShowInfoModal("Loaded", "Emulator state restored.");
					}
				} catch (const MyError &e) {
					ShowInfoModal("Load Failed", e.c_str());
				}
			}
		}

		} // end Save State page

		// --- Sub-page: Display (Filter + Visual Effects) ---
		if (s_settingsPage == ATMobileSettingsPage::Display) {
		ATTouchSection("Visual Effects");

		// Warn the user up front if the current display backend can't
		// actually render GPU-based effects.  Scanlines still work in
		// software so they're never greyed-out.
		{
			IDisplayBackend *backend = ATUIGetDisplayBackend();
			bool hwSupport = backend && backend->SupportsScreenFX();
			if (!hwSupport) {
				ImGui::PushStyleColor(ImGuiCol_Text,
					ImVec4(0.70f, 0.72f, 0.78f, 1));
				ImGui::TextWrapped(
					"Bloom and CRT distortion need the OpenGL display "
					"backend.  The SDL_Renderer fallback (currently "
					"active on this device) will accept the toggles "
					"but silently ignore those two — scanlines still "
					"work either way.");
				ImGui::PopStyleColor();
				ImGui::Spacing();
			}
		}

		// Manually toggling any visual effect moves the performance
		// preset to Custom so the user can see they've left the
		// bundle.
		auto markCustom = [&](){ mobileState.performancePreset = 3; };

		if (ATTouchToggle("Scanlines", &mobileState.fxScanlines)) {
			markCustom();
			SaveMobileConfig(mobileState);
			try { ATMobileUI_ApplyVisualEffects(mobileState); } catch (...) {}
		}

		if (ATTouchToggle("Bloom", &mobileState.fxBloom)) {
			markCustom();
			SaveMobileConfig(mobileState);
			try { ATMobileUI_ApplyVisualEffects(mobileState); } catch (...) {}
		}

		if (ATTouchToggle("CRT Distortion", &mobileState.fxDistortion)) {
			markCustom();
			SaveMobileConfig(mobileState);
			try { ATMobileUI_ApplyVisualEffects(mobileState); } catch (...) {}
		}

		ATTouchSection("Display");

		// Filter mode
		{
			ATDisplayFilterMode curFM = ATUIGetDisplayFilterMode();
			int idx = 0;
			switch (curFM) {
			case kATDisplayFilterMode_Point:        idx = 0; break;
			case kATDisplayFilterMode_Bilinear:     idx = 1; break;
			case kATDisplayFilterMode_SharpBilinear:idx = 2; break;
			default: idx = 1; break;
			}
			static const char *filters[] = { "Sharp", "Bilinear", "Sharp Bi" };
			if (ATTouchSegmented("Filter Mode", &idx, filters, 3)) {
				static const ATDisplayFilterMode kModes[] = {
					kATDisplayFilterMode_Point,
					kATDisplayFilterMode_Bilinear,
					kATDisplayFilterMode_SharpBilinear,
				};
				ATUISetDisplayFilterMode(kModes[idx]);
				mobileState.performancePreset = 3;  // Custom
				SaveMobileConfig(mobileState);
			}
		}
		} // end Display page

		// --- Sub-page: Performance (bundled preset) ---
		if (s_settingsPage == ATMobileSettingsPage::Performance) {
		ATTouchSection("Performance Preset");

		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.70f, 0.78f, 1));
		ImGui::TextWrapped(
			"Choose a preset that bundles visual effects and the "
			"display filter for a consistent trade-off.  Pick "
			"Efficient on older devices, Quality on flagships.");
		ImGui::PopStyleColor();
		ImGui::Spacing();

		{
			// When preset == 3 (Custom) we pass it through unchanged:
			// ATTouchSegmented highlights the matching index or none
			// if out of range, so Custom correctly shows no segment
			// active while the Custom label below explains why.
			int p = mobileState.performancePreset;
			static const char *items[] = { "Efficient", "Balanced", "Quality" };
			if (ATTouchSegmented("Preset", &p, items, 3)) {
				mobileState.performancePreset = p;
				SaveMobileConfig(mobileState);
				ATMobileUI_ApplyPerformancePreset(mobileState);
			}
			if (mobileState.performancePreset == 3) {
				ImGui::PushStyleColor(ImGuiCol_Text,
					ImVec4(1.0f, 0.78f, 0.30f, 1));
				ImGui::TextUnformatted(
					"Preset: Custom (you've manually changed a visual "
					"setting — pick a preset above to revert).");
				ImGui::PopStyleColor();
			}
		}
		} // end Performance page

		// --- Sub-page: Firmware ---
		if (s_settingsPage == ATMobileSettingsPage::Firmware) {
		ATFirmwareManager *fwm = g_sim.GetFirmwareManager();

		auto nameForId = [&](uint64 id) -> VDStringA {
			if (!id) return VDStringA("(internal)");
			ATFirmwareInfo info;
			if (fwm->GetFirmwareInfo(id, info))
				return VDTextWToU8(info.mName);
			return VDStringA("(unknown)");
		};

		if (s_fwPicker == kATFirmwareType_Unknown) {
			ATTouchSection("Firmware");

			if (!s_romDir.empty()) {
				VDStringA dirU8 = VDTextWToU8(s_romDir);
				ImGui::TextWrapped("ROM Directory: %s", dirU8.c_str());
			} else {
				ImGui::Text("ROM Directory: (not set)");
			}
			if (s_romScanResult >= 0)
				ImGui::Text("Status: %d ROMs found", s_romScanResult);

			ImGui::Spacing();
			if (ImGui::Button("Select Firmware Folder", ImVec2(-1, dp(56.0f)))) {
				s_romFolderMode = true;
				s_fileBrowserNeedsRefresh = true;
				mobileState.currentScreen = ATMobileUIScreen::FileBrowser;
			}

			ImGui::Dummy(ImVec2(0, dp(16.0f)));
			ATTouchSection("Kernel & BASIC");

			// Tappable card rows for each user-selectable slot.
			// Kept to the kernels + BASIC that mobile users actually
			// care about — the desktop Firmware Manager covers the
			// long tail of device ROMs.
			struct Slot { const char *title; ATFirmwareType type; };
			static const Slot kSlots[] = {
				{ "OS-B (400/800)",      kATFirmwareType_Kernel800_OSB  },
				{ "OS-A (400/800)",      kATFirmwareType_Kernel800_OSA  },
				{ "XL/XE Kernel",        kATFirmwareType_KernelXL       },
				{ "XEGS Kernel",         kATFirmwareType_KernelXEGS     },
				{ "5200 Kernel",         kATFirmwareType_Kernel5200     },
				{ "Atari BASIC",         kATFirmwareType_Basic          },
			};

			float rowH = dp(72.0f);
			for (size_t i = 0; i < sizeof(kSlots)/sizeof(kSlots[0]); ++i) {
				ImGui::PushID((int)i);
				uint64 curId = fwm->GetDefaultFirmware(kSlots[i].type);
				VDStringA curName = nameForId(curId);

				ImVec2 cursor = ImGui::GetCursorScreenPos();
				float availW = ImGui::GetContentRegionAvail().x;
				ImDrawList *dl = ImGui::GetWindowDrawList();
				dl->AddRectFilled(cursor,
					ImVec2(cursor.x + availW, cursor.y + rowH),
					IM_COL32(30, 35, 50, 200), dp(10.0f));

				if (ImGui::InvisibleButton("##fwslot", ImVec2(availW, rowH)))
					s_fwPicker = kSlots[i].type;

				dl->AddText(ImVec2(cursor.x + dp(16.0f), cursor.y + dp(10.0f)),
					IM_COL32(240, 242, 248, 255), kSlots[i].title);
				dl->AddText(ImVec2(cursor.x + dp(16.0f), cursor.y + dp(40.0f)),
					IM_COL32(160, 175, 200, 255), curName.c_str());
				dl->AddText(ImVec2(cursor.x + availW - dp(28.0f),
						cursor.y + rowH * 0.5f - dp(8.0f)),
					IM_COL32(160, 175, 200, 255), ">");

				ImGui::Dummy(ImVec2(0, dp(8.0f)));
				ImGui::PopID();
			}

			ImGui::Dummy(ImVec2(0, dp(12.0f)));
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.70f, 0.78f, 1));
			ImGui::TextWrapped(
				"Tap a slot to choose which ROM to use.  Selections "
				"apply on the next cold reset.  The built-in HLE "
				"kernel is used as a fallback if no ROM is picked.");
			ImGui::PopStyleColor();
		} else {
			// --- Firmware picker ---
			ATFirmwareType picking = s_fwPicker;
			const char *slotTitle = "Firmware";
			switch (picking) {
			case kATFirmwareType_Kernel800_OSA: slotTitle = "OS-A (400/800)"; break;
			case kATFirmwareType_Kernel800_OSB: slotTitle = "OS-B (400/800)"; break;
			case kATFirmwareType_KernelXL:      slotTitle = "XL/XE Kernel"; break;
			case kATFirmwareType_KernelXEGS:    slotTitle = "XEGS Kernel"; break;
			case kATFirmwareType_Kernel5200:    slotTitle = "5200 Kernel"; break;
			case kATFirmwareType_Basic:         slotTitle = "Atari BASIC"; break;
			default: break;
			}
			ATTouchSection(slotTitle);

			if (ImGui::Button("< Back", ImVec2(dp(120.0f), dp(48.0f))))
				s_fwPicker = kATFirmwareType_Unknown;

			ImGui::Dummy(ImVec2(0, dp(8.0f)));

			vdvector<ATFirmwareInfo> fwList;
			fwm->GetFirmwareList(fwList);

			uint64 curId = fwm->GetDefaultFirmware(picking);

			// "Use built-in HLE" row — selecting this clears the
			// default so the simulator falls back to the bundled
			// HLE kernel at next cold reset.
			{
				float rowH = dp(64.0f);
				ImVec2 cursor = ImGui::GetCursorScreenPos();
				float availW = ImGui::GetContentRegionAvail().x;
				ImDrawList *dl = ImGui::GetWindowDrawList();
				bool selected = (curId == 0);
				dl->AddRectFilled(cursor,
					ImVec2(cursor.x + availW, cursor.y + rowH),
					selected ? IM_COL32(40, 90, 160, 220)
					         : IM_COL32(30, 35, 50, 200),
					dp(10.0f));
				if (ImGui::InvisibleButton("##fwhle", ImVec2(availW, rowH))) {
					fwm->SetDefaultFirmware(picking, 0);
					ATRegistryFlushToDisk();
					g_sim.LoadROMs();
					g_sim.ColdReset();
					s_fwPicker = kATFirmwareType_Unknown;
				}
				dl->AddText(ImVec2(cursor.x + dp(16.0f),
						cursor.y + rowH * 0.5f - dp(8.0f)),
					IM_COL32(240, 242, 248, 255),
					"Built-in HLE (fallback)");
				ImGui::Dummy(ImVec2(0, dp(8.0f)));
			}

			int shown = 0;
			for (const ATFirmwareInfo &info : fwList) {
				if (info.mType != picking)
					continue;
				if (!info.mbVisible)
					continue;
				++shown;

				ImGui::PushID((int)info.mId ^ (int)(info.mId >> 32));
				float rowH = dp(64.0f);
				ImVec2 cursor = ImGui::GetCursorScreenPos();
				float availW = ImGui::GetContentRegionAvail().x;
				ImDrawList *dl = ImGui::GetWindowDrawList();
				bool selected = (curId == info.mId);
				dl->AddRectFilled(cursor,
					ImVec2(cursor.x + availW, cursor.y + rowH),
					selected ? IM_COL32(40, 90, 160, 220)
					         : IM_COL32(30, 35, 50, 200),
					dp(10.0f));
				if (ImGui::InvisibleButton("##fw", ImVec2(availW, rowH))) {
					fwm->SetDefaultFirmware(picking, info.mId);
					ATRegistryFlushToDisk();
					g_sim.LoadROMs();
					g_sim.ColdReset();
					s_fwPicker = kATFirmwareType_Unknown;
				}
				VDStringA nm = VDTextWToU8(info.mName);
				VDStringA ph = VDTextWToU8(info.mPath);
				dl->AddText(ImVec2(cursor.x + dp(16.0f), cursor.y + dp(8.0f)),
					IM_COL32(240, 242, 248, 255), nm.c_str());
				dl->AddText(ImVec2(cursor.x + dp(16.0f), cursor.y + dp(36.0f)),
					IM_COL32(160, 175, 200, 255), ph.c_str());
				ImGui::Dummy(ImVec2(0, dp(8.0f)));
				ImGui::PopID();
			}

			if (shown == 0) {
				ImGui::PushStyleColor(ImGuiCol_Text,
					ImVec4(0.65f, 0.70f, 0.78f, 1));
				ImGui::TextWrapped(
					"No ROMs of this type were found in your "
					"firmware folder.  Tap 'Select Firmware "
					"Folder' on the previous screen to scan "
					"a directory containing Atari ROM images.");
				ImGui::PopStyleColor();
			}
		}
		} // end Firmware page

		// Bottom padding so the last row isn't flush against the nav bar
		ImGui::Dummy(ImVec2(0, dp(32.0f)));

		ImGui::EndChild();
	}
	ImGui::End();
}

// -------------------------------------------------------------------------
// Mobile Disk Drive Manager — full-screen touch-first replacement for
// the desktop ATUIRenderDiskManager dialog.  Large 96dp rows per
// drive with clear Mount/Eject buttons, shows D1:-D4: by default and
// reveals D5:-D15: behind a disclosure.
// -------------------------------------------------------------------------

static const char *BasenameU8(const char *path) {
	const char *p = strrchr(path, '/');
	return p ? p + 1 : path;
}

static void RenderMobileDiskRow(ATSimulator &sim, int driveIdx,
	ATMobileUIState &mobileState)
{
	ATDiskInterface &di = sim.GetDiskInterface(driveIdx);
	bool loaded = di.IsDiskLoaded();
	bool dirty  = loaded && di.IsDirty();

	ImGui::PushID(driveIdx);

	// Row background for visual separation
	float rowH = dp(96.0f);
	ImVec2 cursor = ImGui::GetCursorScreenPos();
	float availW = ImGui::GetContentRegionAvail().x;
	ImDrawList *dl = ImGui::GetWindowDrawList();
	dl->AddRectFilled(
		cursor, ImVec2(cursor.x + availW, cursor.y + rowH),
		IM_COL32(30, 35, 50, 200), dp(10.0f));

	// --- Left column: drive label + filename ---
	float leftPad  = dp(16.0f);
	float rightPad = dp(16.0f);
	ImGui::SetCursorScreenPos(ImVec2(cursor.x + leftPad, cursor.y + dp(12.0f)));
	ImGui::SetWindowFontScale(1.25f);
	ImU32 labelCol = dirty
		? IM_COL32(255, 200, 80, 255)
		: IM_COL32(255, 255, 255, 255);
	ImGui::PushStyleColor(ImGuiCol_Text, labelCol);
	ImGui::Text("D%d:", driveIdx + 1);
	ImGui::PopStyleColor();
	ImGui::SetWindowFontScale(1.0f);

	// Filename / status, one line below the drive label.
	ImGui::SetCursorScreenPos(ImVec2(cursor.x + leftPad, cursor.y + dp(46.0f)));
	if (loaded) {
		const wchar_t *path = di.GetPath();
		if (path && *path) {
			VDStringA u8 = VDTextWToU8(VDStringW(path));
			ImGui::PushStyleColor(ImGuiCol_Text,
				ImVec4(0.80f, 0.85f, 0.92f, 1.0f));
			ImGui::Text("%s", BasenameU8(u8.c_str()));
			ImGui::PopStyleColor();
		} else {
			ImGui::PushStyleColor(ImGuiCol_Text,
				ImVec4(0.60f, 0.65f, 0.75f, 1.0f));
			ImGui::TextUnformatted("(loaded)");
			ImGui::PopStyleColor();
		}

		// Show "(modified)" tag if dirty
		if (dirty) {
			ImGui::SetCursorScreenPos(ImVec2(cursor.x + leftPad, cursor.y + dp(68.0f)));
			ImGui::PushStyleColor(ImGuiCol_Text,
				ImVec4(1.0f, 0.78f, 0.30f, 1.0f));
			ImGui::TextUnformatted("modified");
			ImGui::PopStyleColor();
		}
	} else {
		ImGui::PushStyleColor(ImGuiCol_Text,
			ImVec4(0.55f, 0.60f, 0.70f, 1.0f));
		ImGui::TextUnformatted("(empty)");
		ImGui::PopStyleColor();
	}

	// --- Right column: Mount + Eject buttons ---
	float btnW = dp(100.0f);
	float btnH = dp(56.0f);
	float btnGap = dp(8.0f);
	float btnY = cursor.y + (rowH - btnH) * 0.5f;
	float ejectX = cursor.x + availW - rightPad - btnW;
	float mountX = ejectX - btnGap - btnW;

	ImGui::SetCursorScreenPos(ImVec2(mountX, btnY));
	if (ImGui::Button("Mount", ImVec2(btnW, btnH))) {
		s_diskMountTargetDrive = driveIdx;
		s_romFolderMode = false;
		mobileState.currentScreen = ATMobileUIScreen::FileBrowser;
		s_fileBrowserNeedsRefresh = true;
	}

	ImGui::SetCursorScreenPos(ImVec2(ejectX, btnY));
	ImGui::BeginDisabled(!loaded);
	if (ImGui::Button("Eject", ImVec2(btnW, btnH))) {
		try {
			di.UnloadDisk();
		} catch (const MyError &e) {
			ShowInfoModal("Eject Failed", e.c_str());
		}
	}
	ImGui::EndDisabled();

	// Advance the cursor past the row for the next iteration
	ImGui::SetCursorScreenPos(ImVec2(cursor.x, cursor.y + rowH + dp(8.0f)));
	ImGui::PopID();
}

static void RenderMobileDiskManager(ATSimulator &sim, ATUIState &uiState,
	ATMobileUIState &mobileState, SDL_Window *window)
{
	ImGuiIO &io = ImGui::GetIO();

	// Full-screen dark background
	ImGui::GetBackgroundDrawList()->AddRectFilled(
		ImVec2(0, 0), io.DisplaySize, IM_COL32(18, 20, 28, 255));

	float insetT = (float)mobileState.layout.insets.top;
	float insetB = (float)mobileState.layout.insets.bottom;
	float insetL = (float)mobileState.layout.insets.left;
	float insetR = (float)mobileState.layout.insets.right;

	ImGui::SetNextWindowPos(ImVec2(insetL, insetT));
	ImGui::SetNextWindowSize(ImVec2(
		io.DisplaySize.x - insetL - insetR,
		io.DisplaySize.y - insetT - insetB));

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoBackground;

	if (ImGui::Begin("##MobileDiskMgr", nullptr, flags)) {
		// Header
		float headerH = dp(48.0f);
		if (ImGui::Button("<", ImVec2(dp(48.0f), headerH)))
			mobileState.currentScreen = ATMobileUIScreen::HamburgerMenu;
		ImGui::SameLine();
		ImGui::SetCursorPosY(
			ImGui::GetCursorPosY() + (headerH - ImGui::GetTextLineHeight()) * 0.5f);
		ImGui::SetWindowFontScale(1.15f);
		ImGui::TextColored(ImVec4(1, 1, 1, 1), "Disk Drives");
		ImGui::SetWindowFontScale(1.0f);

		ImGui::Separator();
		ImGui::Spacing();

		// Scrollable list of drives
		float reserveFooter = dp(140.0f);
		ImGui::BeginChild("DriveList",
			ImVec2(0, ImGui::GetContentRegionAvail().y - reserveFooter),
			ImGuiChildFlags_None);
		ATTouchDragScroll();

		// Default: D1:-D4: (the 99% case)
		int visibleDrives = s_mobileShowAllDrives ? 15 : 4;
		for (int i = 0; i < visibleDrives; ++i)
			RenderMobileDiskRow(sim, i, mobileState);

		// Show/hide additional drives
		ImGui::Spacing();
		if (ImGui::Button(
			s_mobileShowAllDrives ? "Hide drives D5:-D15:" : "Show drives D5:-D15:",
			ImVec2(-1, dp(48.0f))))
		{
			s_mobileShowAllDrives = !s_mobileShowAllDrives;
		}

		ImGui::EndChild();

		// Footer: global emulation-level segmented control
		ImGui::Spacing();
		ATTouchSection("Emulation Level");

		// Match the desktop ui_disk.cpp ordering but collapse to the
		// handful of options a mobile user actually cares about.
		static const ATDiskEmulationMode kMobileEmuValues[] = {
			kATDiskEmulationMode_Generic,
			kATDiskEmulationMode_FastestPossible,
			kATDiskEmulationMode_810,
			kATDiskEmulationMode_1050,
			kATDiskEmulationMode_Happy1050,
		};
		static const char *kMobileEmuLabels[] = {
			"Generic", "Fast", "810", "1050", "Happy",
		};
		constexpr int kNumMobileEmu =
			sizeof(kMobileEmuValues) / sizeof(kMobileEmuValues[0]);

		ATDiskEmulationMode curEmu = sim.GetDiskDrive(0).GetEmulationMode();
		int emuIdx = 0;
		for (int i = 0; i < kNumMobileEmu; ++i)
			if (kMobileEmuValues[i] == curEmu) { emuIdx = i; break; }

		if (ATTouchSegmented("Drive type", &emuIdx,
			kMobileEmuLabels, kNumMobileEmu))
		{
			for (int i = 0; i < 15; ++i)
				sim.GetDiskDrive(i).SetEmulationMode(kMobileEmuValues[emuIdx]);
		}
	}
	ImGui::End();
}

// -------------------------------------------------------------------------
// Mobile About panel — full-screen replacement for the desktop
// `About AltirraSDL` dialog (which is sized 420×220 px and looks
// tiny on a phone).  Centered title/subtitle, scrollable credits,
// and a big Close button that returns to the hamburger menu.
// -------------------------------------------------------------------------

static void RenderMobileAbout(ATSimulator &sim, ATUIState &uiState,
	ATMobileUIState &mobileState, SDL_Window *window)
{
	ImGuiIO &io = ImGui::GetIO();

	// Full-screen dark background
	ImGui::GetBackgroundDrawList()->AddRectFilled(
		ImVec2(0, 0), io.DisplaySize, IM_COL32(20, 22, 30, 255));

	float insetT = (float)mobileState.layout.insets.top;
	float insetB = (float)mobileState.layout.insets.bottom;
	float insetL = (float)mobileState.layout.insets.left;
	float insetR = (float)mobileState.layout.insets.right;

	ImGui::SetNextWindowPos(ImVec2(insetL, insetT));
	ImGui::SetNextWindowSize(ImVec2(
		io.DisplaySize.x - insetL - insetR,
		io.DisplaySize.y - insetT - insetB));

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoBackground;

	if (ImGui::Begin("##MobileAbout", nullptr, flags)) {
		float w = ImGui::GetContentRegionAvail().x;

		ImGui::Dummy(ImVec2(0, dp(24.0f)));

		// Large Altirra title
		{
			const char *title = "Altirra";
			ImGui::SetWindowFontScale(2.2f);
			float tw = ImGui::CalcTextSize(title).x;
			ImGui::SetCursorPosX((w - tw) * 0.5f);
			ImGui::TextColored(ImVec4(1, 1, 1, 1), "%s", title);
			ImGui::SetWindowFontScale(1.0f);
		}

		ImGui::Dummy(ImVec2(0, dp(4.0f)));

		// Subtitle
		{
			const char *sub = "Atari 800/XL/5200 Emulator";
			float tw = ImGui::CalcTextSize(sub).x;
			ImGui::SetCursorPosX((w - tw) * 0.5f);
			ImGui::TextColored(ImVec4(0.75f, 0.80f, 0.90f, 1), "%s", sub);
		}

		ImGui::Dummy(ImVec2(0, dp(6.0f)));

		// SDL3 / ImGui frontend identifier
		{
			const char *sub2 = "SDL3 + Dear ImGui cross-platform frontend";
			float tw = ImGui::CalcTextSize(sub2).x;
			ImGui::SetCursorPosX((w - tw) * 0.5f);
			ImGui::TextColored(ImVec4(0.60f, 0.65f, 0.75f, 1), "%s", sub2);
		}

		ImGui::Dummy(ImVec2(0, dp(24.0f)));
		ImGui::Separator();
		ImGui::Dummy(ImVec2(0, dp(16.0f)));

		// Credits block — scrollable child so long text doesn't push
		// the Close button off-screen on small phones.
		float closeH = dp(56.0f);
		float bottomReserve = closeH + dp(24.0f);
		ImGui::BeginChild("AboutCredits",
			ImVec2(0, ImGui::GetContentRegionAvail().y - bottomReserve),
			ImGuiChildFlags_None);
		ATTouchDragScroll();

		ImGui::PushTextWrapPos(w - dp(16.0f));
		ImGui::TextColored(ImVec4(0.85f, 0.88f, 0.94f, 1),
			"Altirra is an Atari 800/800XL/5200 emulator authored by "
			"Avery Lee.  This Android build uses the AltirraSDL "
			"cross-platform frontend, which replaces the original Win32 "
			"UI with SDL3 + Dear ImGui for portability.");
		ImGui::Dummy(ImVec2(0, dp(12.0f)));

		ImGui::TextColored(ImVec4(0.85f, 0.88f, 0.94f, 1),
			"The emulation core is cycle-accurate and identical across "
			"platforms — only the UI, display, audio and input layers "
			"are platform-specific.");
		ImGui::Dummy(ImVec2(0, dp(12.0f)));

		ImGui::TextColored(ImVec4(0.85f, 0.88f, 0.94f, 1),
			"Original Altirra Copyright (C) Avery Lee.\n"
			"Licensed under GNU GPL v2 or later.");
		ImGui::Dummy(ImVec2(0, dp(8.0f)));

		ImGui::TextColored(ImVec4(0.95f, 0.90f, 0.70f, 1),
			"SDL / Android port by Jakub 'Ilmenit' Debski.");
		ImGui::Dummy(ImVec2(0, dp(4.0f)));

		{
			const char *presetLabel = "Balanced";
			switch (mobileState.performancePreset) {
			case 0: presetLabel = "Efficient"; break;
			case 1: presetLabel = "Balanced"; break;
			case 2: presetLabel = "Quality"; break;
			case 3: presetLabel = "Custom"; break;
			}
			ImGui::TextColored(ImVec4(0.70f, 0.75f, 0.85f, 1),
				"Performance preset: %s (change in Settings > "
				"Performance).", presetLabel);
		}
		ImGui::Dummy(ImVec2(0, dp(12.0f)));

		ImGui::TextColored(ImVec4(0.85f, 0.88f, 0.94f, 1),
			"Third-party components:\n"
			"  - SDL3  (zlib license)\n"
			"  - Dear ImGui  (MIT license)\n"
			"  - Roboto font  (Apache 2.0)\n"
			"  - Fira Mono font  (SIL Open Font License)");
		ImGui::PopTextWrapPos();

		ImGui::EndChild();

		// Close button pinned to the bottom
		ImGui::PushStyleColor(ImGuiCol_Button,
			ImVec4(0.25f, 0.55f, 0.90f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
			ImVec4(0.30f, 0.62f, 0.95f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive,
			ImVec4(0.20f, 0.48f, 0.85f, 1.0f));
		if (ImGui::Button("Close", ImVec2(-1, closeH)))
			mobileState.currentScreen = ATMobileUIScreen::HamburgerMenu;
		ImGui::PopStyleColor(3);
	}
	ImGui::End();
}

// -------------------------------------------------------------------------
// First-run welcome wizard
// -------------------------------------------------------------------------

static void RenderFirstRunWizard(ATSimulator &sim, ATUIState &uiState,
	ATMobileUIState &mobileState, SDL_Window *window)
{
	ImGuiIO &io = ImGui::GetIO();

	// Full-screen dark background
	ImGui::GetBackgroundDrawList()->AddRectFilled(
		ImVec2(0, 0), io.DisplaySize, IM_COL32(20, 22, 30, 255));

	// Inset window inside safe area so the title doesn't disappear
	// under the status bar and the skip button doesn't hide behind
	// the nav bar.
	float insetT = (float)mobileState.layout.insets.top;
	float insetB = (float)mobileState.layout.insets.bottom;
	float insetL = (float)mobileState.layout.insets.left;
	float insetR = (float)mobileState.layout.insets.right;
	ImGui::SetNextWindowPos(ImVec2(insetL, insetT));
	ImGui::SetNextWindowSize(ImVec2(
		io.DisplaySize.x - insetL - insetR,
		io.DisplaySize.y - insetT - insetB));

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoBackground;

	if (ImGui::Begin("##FirstRun", nullptr, flags)) {
		float w = ImGui::GetContentRegionAvail().x;
		float h = ImGui::GetContentRegionAvail().y;

		// Center content vertically
		float contentH = dp(360.0f);
		float topPad = (h - contentH) * 0.5f;
		if (topPad < dp(40.0f)) topPad = dp(40.0f);
		ImGui::Dummy(ImVec2(0, topPad));

		// Title
		{
			const char *title = "Altirra";
			ImGui::SetWindowFontScale(2.0f);
			float tw = ImGui::CalcTextSize(title).x;
			ImGui::SetCursorPosX((w - tw) * 0.5f);
			ImGui::TextColored(ImVec4(1, 1, 1, 1), "%s", title);
			ImGui::SetWindowFontScale(1.0f);
		}

		ImGui::Spacing();

		// Subtitle
		{
			const char *sub = "Atari 800/XL/5200 Emulator";
			ImVec2 ts = ImGui::CalcTextSize(sub);
			ImGui::SetCursorPosX((w - ts.x) * 0.5f);
			ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.82f, 1), "%s", sub);
		}

		ImGui::Dummy(ImVec2(0, dp(24.0f)));

		// Body text
		{
			const char *body =
				"To get started, select a folder containing Atari ROM firmware,\n"
				"or skip and use the built-in replacement kernel.";
			float wrapW = w * 0.85f;
			float bodyX = (w - wrapW) * 0.5f;
			ImGui::SetCursorPosX(bodyX);
			ImGui::PushTextWrapPos(bodyX + wrapW);
			ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.90f, 1), "%s", body);
			ImGui::PopTextWrapPos();
		}

		ImGui::Dummy(ImVec2(0, dp(32.0f)));

		// Action buttons — centered, stacked
		float btnW = dp(260.0f);
		float btnH = dp(56.0f);

		ImGui::SetCursorPosX((w - btnW) * 0.5f);
		ImGui::PushStyleColor(ImGuiCol_Button,
			ImVec4(0.25f, 0.55f, 0.90f, 1));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
			ImVec4(0.30f, 0.60f, 0.95f, 1));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive,
			ImVec4(0.20f, 0.50f, 0.85f, 1));
		if (ImGui::Button("Select ROM Folder", ImVec2(btnW, btnH))) {
			s_romFolderMode = true;
			s_fileBrowserNeedsRefresh = true;
			mobileState.currentScreen = ATMobileUIScreen::FileBrowser;
			SetFirstRunComplete();
		}
		ImGui::PopStyleColor(3);

		ImGui::Dummy(ImVec2(0, dp(12.0f)));

		ImGui::SetCursorPosX((w - btnW) * 0.5f);
		// Note: avoid Unicode dashes — ImGui's default font doesn't ship
		// the U+2014 em-dash glyph, so it renders as a fallback '?'.
		if (ImGui::Button("Skip - Use Built-in Kernel", ImVec2(btnW, btnH))) {
			mobileState.currentScreen = ATMobileUIScreen::None;
			SetFirstRunComplete();
		}
	}
	ImGui::End();
}

// Compact "Load Game" prompt shown near the top of the screen when
// no game is loaded.  Intentionally small and clean so the Atari
// display (which is showing the AltirraOS boot screen behind it)
// stays visible.  Replaces the earlier centered card which was too
// big and blocked the background.
//
// Design:
//   - Positioned just below the top bar (console keys), centered
//     horizontally.  Visible without covering the display area.
//   - Solid opaque pill with an accent-tinted button.
//   - No separate title — the button label and a compact subtitle
//     carry the message.
//   - Single ImGui window with an opaque WindowBg (no ForegroundDraw
//     overlay, which would cover the button text).
static void RenderLoadGamePrompt(ATSimulator &sim, ATUIState &uiState,
	ATMobileUIState &mobileState)
{
	ImGuiIO &io = ImGui::GetIO();

	// Pill dimensions
	float btnW  = dp(220.0f);
	float btnH  = dp(52.0f);
	float padX  = dp(18.0f);
	float padY  = dp(14.0f);
	float pillW = btnW + padX * 2;
	float pillH = btnH + padY * 2 + dp(20.0f);  // + room for subtitle

	// Anchor just below the top bar so the Atari display beneath is
	// visible.  The top bar is always reserved 56dp below the safe
	// inset, so place the pill 16dp below that.
	float insetT = (float)mobileState.layout.insets.top;
	float topBarH = dp(56.0f);
	float pillX = (io.DisplaySize.x - pillW) * 0.5f;
	float pillY = insetT + topBarH + dp(16.0f);

	// Opaque dark pill with accent outline.  Drawing via WindowBg
	// avoids the earlier bug where a ForegroundDrawList overlay was
	// covering the button text.
	ImGui::SetNextWindowPos(ImVec2(pillX, pillY));
	ImGui::SetNextWindowSize(ImVec2(pillW, pillH));

	ImGuiStyle &style = ImGui::GetStyle();
	float prevRounding = style.WindowRounding;
	float prevBorder   = style.WindowBorderSize;
	style.WindowRounding   = dp(16.0f);
	style.WindowBorderSize = dp(2.0f);

	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.10f, 0.15f, 0.96f));
	ImGui::PushStyleColor(ImGuiCol_Border,    ImVec4(0.27f, 0.51f, 0.82f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.25f, 0.55f, 0.90f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.62f, 0.95f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.20f, 0.48f, 0.85f, 1.0f));

	ImGui::Begin("##LoadPrompt", nullptr,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	// Primary action button
	ImGui::SetCursorPos(ImVec2(padX, padY));
	if (ImGui::Button("Load Game", ImVec2(btnW, btnH))) {
		s_romFolderMode = false;
		mobileState.currentScreen = ATMobileUIScreen::FileBrowser;
		s_fileBrowserNeedsRefresh = true;
	}

	// Compact hint under the button
	{
		const char *hintAscii = "or tap the menu icon for more options";
		float tw = ImGui::CalcTextSize(hintAscii).x;
		ImGui::SetCursorPosX((pillW - tw) * 0.5f);
		ImGui::TextColored(ImVec4(0.70f, 0.75f, 0.82f, 1), "%s", hintAscii);
	}

	ImGui::End();

	ImGui::PopStyleColor(5);
	style.WindowRounding   = prevRounding;
	style.WindowBorderSize = prevBorder;
}

// -------------------------------------------------------------------------
// Main render entry point
// -------------------------------------------------------------------------

void ATMobileUI_Render(ATSimulator &sim, ATUIState &uiState,
	ATMobileUIState &mobileState, SDL_Window *window)
{
	// Cache content scale for dp() helper
	s_contentScale = mobileState.layoutConfig.contentScale;

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
	if (mobileState.currentScreen == ATMobileUIScreen::None
		&& !IsFirstRunComplete())
	{
		mobileState.currentScreen = ATMobileUIScreen::FirstRunWizard;
	}

	// Check for menu button tap
	if (ConsumeMenuTap() && mobileState.currentScreen == ATMobileUIScreen::None)
		ATMobileUI_OpenMenu(sim, mobileState);

	switch (mobileState.currentScreen) {
	case ATMobileUIScreen::None:
		// Render touch controls overlay
		ATTouchControls_Render(mobileState.layout, mobileState.layoutConfig);

		// If no game loaded, show a styled centered "Load Game" button
		if (!mobileState.gameLoaded)
			RenderLoadGamePrompt(sim, uiState, mobileState);
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
	}

	// Global mobile dialog sheet — serves both info popups
	// (ShowInfoModal, single OK button) and confirmation popups
	// (ShowConfirmDialog, Cancel + Confirm buttons).  Card-style
	// sheet sized to the phone display, centered in the safe area.
	const bool haveInfo    = s_infoModalOpen;
	const bool haveConfirm = s_confirmActive;
	if (haveInfo || haveConfirm) {
		// Full-screen dim backdrop.  Use the BACKGROUND draw list so
		// the rectangle renders *beneath* every ImGui window this
		// frame — otherwise the foreground list paints over the sheet
		// card and visibly darkens it.
		ImGui::GetBackgroundDrawList()->AddRectFilled(
			ImVec2(0, 0), ImGui::GetIO().DisplaySize,
			IM_COL32(0, 0, 0, 160));

		float insetL = (float)mobileState.layout.insets.left;
		float insetR = (float)mobileState.layout.insets.right;
		float insetT = (float)mobileState.layout.insets.top;
		float insetB = (float)mobileState.layout.insets.bottom;
		float availW = ImGui::GetIO().DisplaySize.x - insetL - insetR - dp(32.0f);
		float sheetW = availW < dp(520.0f) ? availW : dp(520.0f);
		if (sheetW < dp(260.0f)) sheetW = dp(260.0f);
		float sheetH = dp(260.0f);
		float sheetX = (ImGui::GetIO().DisplaySize.x - sheetW) * 0.5f;
		float areaTop = insetT;
		float areaH = ImGui::GetIO().DisplaySize.y - insetT - insetB;
		float sheetY = areaTop + (areaH - sheetH) * 0.5f;
		if (sheetY < insetT + dp(16.0f)) sheetY = insetT + dp(16.0f);

		ImGui::SetNextWindowPos(ImVec2(sheetX, sheetY));
		ImGui::SetNextWindowSize(ImVec2(sheetW, 0));

		ImGuiStyle &style = ImGui::GetStyle();
		float prevR = style.WindowRounding;
		float prevB = style.WindowBorderSize;
		style.WindowRounding = dp(14.0f);
		style.WindowBorderSize = dp(1.0f);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.12f, 0.18f, 0.98f));
		ImGui::PushStyleColor(ImGuiCol_Border,   ImVec4(0.27f, 0.51f, 0.82f, 1.0f));

		const char *winId = haveConfirm ? "##MobileConfirm" : "##MobileInfo";
		ImGui::Begin(winId, nullptr,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
			| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
			| ImGuiWindowFlags_NoSavedSettings
			| ImGuiWindowFlags_AlwaysAutoResize);

		const char *title = haveConfirm
			? s_confirmTitle.c_str() : s_infoModalTitle.c_str();
		const char *body  = haveConfirm
			? s_confirmBody.c_str()  : s_infoModalBody.c_str();

		ImGui::Dummy(ImVec2(0, dp(8.0f)));
		if (title && *title) {
			ImGui::SetWindowFontScale(1.25f);
			ImGui::PushStyleColor(ImGuiCol_Text,
				ImVec4(0.40f, 0.70f, 1.00f, 1.0f));
			ImGui::TextUnformatted(title);
			ImGui::PopStyleColor();
			ImGui::SetWindowFontScale(1.0f);
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
		}
		ImGui::PushTextWrapPos(sheetW - dp(24.0f));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.92f, 0.96f, 1));
		ImGui::TextUnformatted(body);
		ImGui::PopStyleColor();
		ImGui::PopTextWrapPos();
		ImGui::Dummy(ImVec2(0, dp(16.0f)));
		ImGui::Separator();
		ImGui::Dummy(ImVec2(0, dp(8.0f)));

		float btnH = dp(56.0f);
		float rowW = ImGui::GetContentRegionAvail().x;
		if (haveConfirm) {
			float gap = dp(12.0f);
			float halfW = (rowW - gap) * 0.5f;

			ImGui::PushStyleColor(ImGuiCol_Button,
				ImVec4(0.30f, 0.32f, 0.38f, 1));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
				ImVec4(0.38f, 0.40f, 0.48f, 1));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive,
				ImVec4(0.22f, 0.24f, 0.30f, 1));
			if (ImGui::Button("Cancel", ImVec2(halfW, btnH))) {
				s_confirmActive = false;
				s_confirmAction = nullptr;
			}
			ImGui::PopStyleColor(3);

			ImGui::SameLine(0.0f, gap);

			ImGui::PushStyleColor(ImGuiCol_Button,
				ImVec4(0.25f, 0.55f, 0.90f, 1));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
				ImVec4(0.30f, 0.62f, 0.95f, 1));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive,
				ImVec4(0.20f, 0.48f, 0.85f, 1));
			if (ImGui::Button("Confirm", ImVec2(halfW, btnH))) {
				auto act = s_confirmAction;
				s_confirmActive = false;
				s_confirmAction = nullptr;
				if (act) act();
			}
			ImGui::PopStyleColor(3);
		} else {
			ImGui::PushStyleColor(ImGuiCol_Button,
				ImVec4(0.25f, 0.55f, 0.90f, 1));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
				ImVec4(0.30f, 0.62f, 0.95f, 1));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive,
				ImVec4(0.20f, 0.48f, 0.85f, 1));
			if (ImGui::Button("OK", ImVec2(-1, btnH))) {
				s_infoModalOpen = false;
			}
			ImGui::PopStyleColor(3);
		}

		ImGui::End();
		ImGui::PopStyleColor(2);
		style.WindowRounding = prevR;
		style.WindowBorderSize = prevB;
	}
}

// -------------------------------------------------------------------------
// Event handling
// -------------------------------------------------------------------------

bool ATMobileUI_HandleEvent(const SDL_Event &ev, ATMobileUIState &mobileState) {
	// If a menu/dialog is open, let ImGui handle everything
	if (mobileState.currentScreen != ATMobileUIScreen::None)
		return false;

	// Route touch events to touch controls
	if (ev.type == SDL_EVENT_FINGER_DOWN ||
		ev.type == SDL_EVENT_FINGER_MOTION ||
		ev.type == SDL_EVENT_FINGER_UP)
	{
		return ATTouchControls_HandleEvent(ev, mobileState.layout, mobileState.layoutConfig);
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
