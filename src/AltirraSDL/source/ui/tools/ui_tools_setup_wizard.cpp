//	AltirraSDL - Tools dialog (split from ui_tools.cpp, Phase 2k)

#include <stdafx.h>
#include <algorithm>
#include <string>
#include <mutex>
#include <thread>
#include <vector>
#include <cstring>
#include <cstdio>
#include <imgui.h>
#include <SDL3/SDL.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/error.h>
#include <vd2/system/date.h>
#include <vd2/system/registry.h>
#include <vd2/system/vdstl.h>
#include <at/atcore/configvar.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/media.h>
#include <at/atio/image.h>
#include <at/atio/diskimage.h>
#include <at/atio/cartridgeimage.h>
#include <at/atio/cassetteimage.h>
#include <vd2/Dita/accel.h>
#include "ui_main.h"
#include "accel_sdl3.h"
#include "simulator.h"
#include "gtia.h"
#include "constants.h"
#include "disk.h"
#include "diskinterface.h"
#include "firmwaremanager.h"
#include "firmwaredetect.h"
#include "compatengine.h"
#include "settings.h"
#include "uiaccessors.h"
#include "uikeyboard.h"
#include "uitypes.h"
#include "options.h"
#include "oshelper.h"
#include "ui_mode.h"
#include "ui_mobile.h"
#include "mobile_internal.h"
#include "../gamelibrary/game_library.h"

extern ATSimulator g_sim;
extern ATMobileUIState g_mobileState;

// =========================================================================
// First Time Setup Wizard
// Reference: src/Altirra/source/uisetupwizard.cpp
// =========================================================================

static struct SetupWizardState {
	int page = 0;
	bool wentPastFirst = false;
	bool firmwareScanned = false;
	int scanFound = 0;
	int scanExisting = 0;
	VDStringA scanMessage;

	// UI mode chosen by the user (deferred until wizard closes so the
	// desktop-style wizard dialog doesn't disappear mid-flow).
	// -1 means "no choice made, keep current mode".
	int pendingUIMode = -1;

	// Thread-safe: path stored by callback, processed on main thread
	std::mutex scanMutex;
	std::string pendingScanPath;
	std::string pendingLibFolderPath;

	void Reset() {
		page = 0;
		wentPastFirst = false;
		firmwareScanned = false;
		scanFound = 0;
		scanExisting = 0;
		scanMessage.clear();
		pendingUIMode = -1;
	}
} g_setupWiz;

// Firmware scan logic reimplemented from uifirmwarescan.cpp.
// Exposed (no `static`) so the WASM bridge can invoke the same scan
// on /home/web_user/firmware after an upload, keeping the firmware
// list in sync with what the user has actually placed there.
void ATUIDoFirmwareScan(const char *utf8path) {
	ATFirmwareManager &fwmgr = *g_sim.GetFirmwareManager();
	VDStringW path = VDTextU8ToW(utf8path, -1);
	VDStringW pattern = VDMakePath(path.c_str(), L"*.*");

	VDDirectoryIterator it(pattern.c_str());
	vdvector<VDStringW> candidates;

	while (it.Next()) {
		if (it.GetAttributes() & (kVDFileAttr_System | kVDFileAttr_Hidden))
			continue;
		if (it.IsDirectory())
			continue;
		if (!ATFirmwareAutodetectCheckSize(it.GetSize()))
			continue;
		candidates.push_back(it.GetFullPath());
	}

	ATFirmwareInfo info;
	vdvector<ATFirmwareInfo> detected;

	for (auto &fullPath : candidates) {
		try {
			VDFile f(fullPath.c_str());
			sint64 size = f.size();
			if (!ATFirmwareAutodetectCheckSize(size))
				continue;

			uint32 size32 = (uint32)size;
			vdblock<char> buf(size32);
			f.read(buf.data(), (long)buf.size());

			ATSpecificFirmwareType specificType;
			sint32 knownIdx = -1;
			if (ATFirmwareAutodetect(buf.data(), size32, info, specificType, knownIdx) == ATFirmwareDetection::SpecificImage) {
				ATFirmwareInfo &info2 = detected.push_back();
				info2 = std::move(info);
				info2.mId = ATGetFirmwareIdFromPath(fullPath.c_str());
				info2.mPath = fullPath;

				if (specificType != kATSpecificFirmwareType_None && !fwmgr.GetSpecificFirmware(specificType))
					fwmgr.SetSpecificFirmware(specificType, info2.mId);
			}
		} catch (const MyError &) {
		}
	}

	size_t existing = 0;
	for (auto &det : detected) {
		ATFirmwareInfo info2;
		if (fwmgr.GetFirmwareInfo(det.mId, info2)) {
			++existing;
			continue;
		}
		fwmgr.AddFirmware(det);
	}

	g_setupWiz.scanFound = (int)detected.size();
	g_setupWiz.scanExisting = (int)existing;
	g_setupWiz.firmwareScanned = true;
	g_setupWiz.scanMessage.sprintf("Firmware images recognized: %d (%d already present)",
		(int)detected.size(), (int)existing);
}

// File dialog callback — may run on background thread, so just store the path.
// The actual scan runs on the main thread in ATUIRenderSetupWizard().
static void FirmwareScanCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0])
		return;
	std::lock_guard<std::mutex> lock(g_setupWiz.scanMutex);
	g_setupWiz.pendingScanPath = filelist[0];
}

static void LibFolderCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0])
		return;
	std::lock_guard<std::mutex> lock(g_setupWiz.scanMutex);
	g_setupWiz.pendingLibFolderPath = filelist[0];
}

static bool IsGamingModeSelected() {
	int sel = g_setupWiz.pendingUIMode;
	if (sel < 0)
		sel = (int)ATUIGetMode();
	return sel == (int)ATUIMode::Gaming;
}

static int GetWizPrevPage(int page) {
	switch (page) {
		case 0:  return -1;
		case 1:  return 0;
		case 2:  return 1;
		case 5:  return IsGamingModeSelected() ? 2 : 1;
		case 10: return 5;
		case 11: return 10;
		case 20: return 11;
		case 21: return 20;
		case 30: return g_sim.GetHardwareMode() == kATHardwareMode_5200 ? 20 : 21;
		case 40: return 30;
		case 41: return 30;
		default: return 0;
	}
}

static int GetWizNextPage(int page) {
	switch (page) {
		case 0:  return 1;
		case 1:  return IsGamingModeSelected() ? 2 : 5;
		case 2:  return 5;
		case 5:  return 10;
		case 10: return 11;
		case 11: return 20;
		case 20: return g_sim.GetHardwareMode() == kATHardwareMode_5200 ? 30 : 21;
		case 21: return 30;
		case 30: return g_sim.GetHardwareMode() == kATHardwareMode_5200 ? 41 : 40;
		default: return -1;
	}
}

static void ApplyPendingUIMode(SDL_Window *window) {
	if (g_setupWiz.pendingUIMode >= 0) {
		ATUISetMode((ATUIMode)g_setupWiz.pendingUIMode);
		ATUISaveMode();
		float cs = SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(window));
		if (cs < 1.0f) cs = 1.0f;
		if (cs > 4.0f) cs = 4.0f;
		ATUIApplyModeStyle(cs);

		if (ATUIIsGamingMode()) {
			GameBrowser_Init();
			g_mobileState.currentScreen = ATMobileUIScreen::GameBrowser;
			ATMobileUI_ApplyVisualEffects(g_mobileState);
			ATMobileUI_ApplyPerformancePreset(g_mobileState);
			g_sim.Pause();
		}
	}
}

void ATUIRenderSetupWizard(ATSimulator &sim, ATUIState &state, SDL_Window *window) {
	// Process pending firmware scan on main thread (callback may have run on background thread)
	{
		std::string scanPath;
		{
			std::lock_guard<std::mutex> lock(g_setupWiz.scanMutex);
			scanPath.swap(g_setupWiz.pendingScanPath);
		}
		if (!scanPath.empty())
			ATUIDoFirmwareScan(scanPath.c_str());
	}

	// Process pending game library folder on main thread
	{
		std::string libPath;
		{
			std::lock_guard<std::mutex> lock(g_setupWiz.scanMutex);
			libPath.swap(g_setupWiz.pendingLibFolderPath);
		}
		if (!libPath.empty()) {
			GameBrowser_Init();
			ATGameLibrary *lib = GetGameLibrary();
			if (lib) {
				auto sources = lib->GetSources();
				GameSource src;
				src.mPath = VDTextU8ToW(libPath.c_str(), -1);
				src.mbIsArchive = false;
				sources.push_back(src);
				lib->SetSources(std::move(sources));
				lib->SaveSettingsToRegistry();
				lib->StartScan();
				extern void ATRegistryFlushToDisk();
				ATRegistryFlushToDisk();
			}
		}
	}

	ImGui::SetNextWindowSize(ImVec2(620, 480), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	bool open = state.showSetupWizard;
	if (!ImGui::Begin("First Time Setup", &open,
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
		if (!open) {
			if (g_setupWiz.wentPastFirst) {
				sim.LoadROMs();
				sim.ColdReset();
			}
			ApplyPendingUIMode(window);
			g_setupWiz.Reset();
			state.showSetupWizard = false;
		}
		ImGui::End();
		return;
	}

	if (!open || ATUICheckEscClose()) {
		if (g_setupWiz.wentPastFirst) {
			sim.LoadROMs();
			sim.ColdReset();
		}
		ApplyPendingUIMode(window);
		g_setupWiz.Reset();
		state.showSetupWizard = false;
		ImGui::End();
		return;
	}

	float sidebarW = 140;

	// Left sidebar: step list
	{
		ImGui::BeginChild("WizSteps", ImVec2(sidebarW, -40), ImGuiChildFlags_Borders);

		static const struct { int pageMin; int pageMax; const char *label; bool gamingOnly; } kSteps[] = {
			{ 0, 0, "Welcome", false },
			{ 1, 1, "Interface mode", false },
			{ 2, 4, "Game Library", true },
			{ 5, 9, "Appearance", false },
			{ 10, 19, "Setup firmware", false },
			{ 20, 29, "Select system", false },
			{ 30, 39, "Experience", false },
			{ 40, 49, "Finish", false },
		};

		for (auto &step : kSteps) {
			if (step.gamingOnly && !IsGamingModeSelected())
				continue;
			bool active = (g_setupWiz.page >= step.pageMin && g_setupWiz.page <= step.pageMax);
			if (active) {
				const auto &bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
				bool darkBg = (bg.x + bg.y + bg.z) < 1.5f;
				ImVec4 highlightColor = darkBg
					? ImVec4(1.0f, 0.85f, 0.2f, 1.0f)
					: ImVec4(0.7f, 0.5f, 0.0f, 1.0f);
				ImGui::PushStyleColor(ImGuiCol_Text, highlightColor);
				ImGui::Bullet();
				ImGui::SameLine();
				ImGui::TextUnformatted(step.label);
				ImGui::PopStyleColor();
			} else {
				ImGui::TextUnformatted(step.label);
			}
		}

		ImGui::EndChild();
	}

	ImGui::SameLine();

	// Right content area
	{
		ImGui::BeginChild("WizContent", ImVec2(0, -40));

		switch (g_setupWiz.page) {
		case 0: // Welcome
			ImGui::TextWrapped(
				"Welcome to Altirra!\n\n"
				"This wizard will help you configure the emulator for the first time. "
				"To begin, click Next.\n\n"
				"If you would like to skip the setup process, click Close to exit this "
				"wizard and start the emulator. All of the settings here can also be set "
				"up manually. You can also repeat the first time setup process via the "
				"Tools menu at any time."
			);
			break;

		case 1: { // Interface mode
			ImGui::TextWrapped(
				"Choose your preferred interface mode.\n\n"
				"Desktop Mode provides a traditional menu bar with keyboard shortcuts, "
				"suitable for mouse and keyboard, software development and debugging.\n\n"
				"Gaming Mode provides a simplified, controller-friendly interface with "
				"large buttons and gamepad navigation, suitable for gamepads and touch "
				"screens."
			);
			ImGui::Spacing();
			ImGui::Spacing();

			int sel = g_setupWiz.pendingUIMode;
			if (sel < 0)
				sel = (int)ATUIGetMode();
			if (ImGui::RadioButton("Desktop Mode", sel == (int)ATUIMode::Desktop))
				g_setupWiz.pendingUIMode = (int)ATUIMode::Desktop;
			ImGui::TextDisabled("  Menu bar, keyboard shortcuts, mouse-driven");
			ImGui::Spacing();

			if (ImGui::RadioButton("Gaming Mode", sel == (int)ATUIMode::Gaming))
				g_setupWiz.pendingUIMode = (int)ATUIMode::Gaming;
			ImGui::TextDisabled("  Large buttons, gamepad/touch navigation");

			ImGui::Spacing();
			ImGui::Spacing();
			ImGui::TextWrapped(
				"You can switch between modes at any time from the View menu (Desktop) "
				"or the hamburger menu (Gaming)."
			);
			break;
		}

		case 2: { // Game Library (Gaming Mode only)
			ImGui::TextWrapped(
				"Gaming Mode uses a Game Library as your home screen. Add folders "
				"containing your Atari game files (.atr, .xex, .car, .cas, etc.) "
				"to browse and play them.\n\n"
				"You can also add more folders later from Settings > Game Library."
			);
			ImGui::Spacing();

			GameBrowser_Init();
			ATGameLibrary *lib = GetGameLibrary();
			if (lib) {
				if (lib->IsScanComplete())
					lib->ConsumeScanResults();

				const auto &sources = lib->GetSources();
				if (!sources.empty()) {
					if (ImGui::BeginTable("LibSources", 2,
						ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
					{
						ImGui::TableSetupColumn("Folder", ImGuiTableColumnFlags_WidthStretch);
						ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 60);
						ImGui::TableHeadersRow();
						for (size_t i = 0; i < sources.size(); ++i) {
							ImGui::TableNextRow();
							ImGui::TableNextColumn();
							VDStringA pathU8 = VDTextWToU8(sources[i].mPath);
							ImGui::TextUnformatted(pathU8.c_str());
							ImGui::TableNextColumn();
							char removeId[32];
							snprintf(removeId, sizeof(removeId),
								"Remove##ls%d", (int)i);
							if (ImGui::SmallButton(removeId)) {
								auto mut = sources;
								mut.erase(mut.begin() + i);
								lib->SetSources(std::move(mut));
								lib->SaveSettingsToRegistry();
								extern void ATRegistryFlushToDisk();
								ATRegistryFlushToDisk();
							}
						}
						ImGui::EndTable();
					}
					ImGui::Spacing();
				}

				if (lib->IsScanning()) {
					int found = lib->GetScanProgress();
					ImGui::TextColored(ImVec4(0.45f, 0.65f, 0.90f, 1.0f),
						"Scanning... %d games found", found);
				} else if (!sources.empty()) {
					size_t count = lib->GetEntryCount();
					ImGui::Text("%d game%s in your library.",
						(int)count, count == 1 ? "" : "s");
				}
			}

			ImGui::Spacing();
			if (ImGui::Button("Add Folder...")) {
#ifdef __EMSCRIPTEN__
				// Browsers can't offer a cross-platform folder picker
				// that reaches into the real filesystem.  The game
				// library on WASM always lives under the fixed
				// uploads path, so just feed that straight through
				// and skip the (unreachable) native folder dialog.
				std::lock_guard<std::mutex> lock(g_setupWiz.scanMutex);
				const char *fixedLibPath[] = { "/home/web_user/games", nullptr };
				LibFolderCallback(nullptr, fixedLibPath, 0);
#else
				SDL_ShowOpenFolderDialog(LibFolderCallback, nullptr,
					window, nullptr, false);
#endif
			}

			ImGui::Spacing();
			ImGui::Spacing();
			ImGui::TextDisabled(
				"If you don't have game files yet, skip this step.");
			break;
		}

		case 5: { // Appearance — theme and transparency
			ImGui::TextWrapped(
				"Choose a visual theme for the user interface. Changes take effect "
				"immediately so you can preview each option."
			);
			ImGui::Spacing();

			static const char *themeLabels[] = { "Use system setting", "Light", "Dark" };
			int themeIdx = (int)g_ATOptions.mThemeMode;
			if (ImGui::Combo("Theme", &themeIdx, themeLabels, 3)) {
				ATOptions prev(g_ATOptions);
				g_ATOptions.mThemeMode = (ATUIThemeMode)themeIdx;
				if (g_ATOptions != prev) {
					g_ATOptions.mbDirty = true;
					ATOptionsRunUpdateCallbacks(&prev);
					ATOptionsSave();
					ATUIApplyTheme();
				}
			}

			ImGui::Spacing();

			int alphaPct = (int)(g_ATOptions.mUIAlpha * 100.0f + 0.5f);
			if (ImGui::SliderInt("Window opacity (%)", &alphaPct, 20, 100)) {
				ATOptions prev(g_ATOptions);
				g_ATOptions.mUIAlpha = alphaPct / 100.0f;
				if (g_ATOptions != prev) {
					g_ATOptions.mbDirty = true;
					ATOptionsRunUpdateCallbacks(&prev);
					ATOptionsSave();
					ATUIApplyTheme();
				}
			}

			ImGui::Spacing();
			ImGui::TextWrapped(
				"These settings can be changed later from Configure System > "
				"Emulator > UI."
			);
			break;
		}

		case 10: { // Firmware
			ImGui::TextWrapped(
				"Altirra has internal replacements for all standard ROMs. However, "
				"if you have original ROM images, you can set these up now for better "
				"compatibility.\n\n"
				"If you do not have ROM images or do not want to set them up now, just "
				"click Next."
			);
			ImGui::Spacing();

			// Firmware status table
			ATFirmwareManager &fwm = *sim.GetFirmwareManager();
			static const struct { ATFirmwareType type; const char *name; } kFirmware[] = {
				{ kATFirmwareType_Kernel800_OSB, "800 OS (OS-B)" },
				{ kATFirmwareType_KernelXL,      "XL/XE OS" },
				{ kATFirmwareType_Basic,          "BASIC" },
				{ kATFirmwareType_Kernel5200,     "5200 OS" },
			};

			if (ImGui::BeginTable("FWStatus", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
				ImGui::TableSetupColumn("ROM Image");
				ImGui::TableSetupColumn("Status");
				ImGui::TableHeadersRow();

				for (auto &fw : kFirmware) {
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(fw.name);
					ImGui::TableNextColumn();
					uint64 fwid = fwm.GetCompatibleFirmware(fw.type);
					bool present = (fwid && fwid >= kATFirmwareId_Custom);
					if (present) {
						const auto& bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
						bool darkBg = (bg.x + bg.y + bg.z) < 1.5f;
						ImVec4 okColor = darkBg
							? ImVec4(0.3f, 1.0f, 0.3f, 1.0f)
							: ImVec4(0.0f, 0.5f, 0.0f, 1.0f);
						ImGui::PushStyleColor(ImGuiCol_Text, okColor);
						ImGui::TextUnformatted("OK");
						ImGui::PopStyleColor();
					} else {
						ImGui::TextDisabled("Not found");
					}
				}
				ImGui::EndTable();
			}

			ImGui::Spacing();
			if (ImGui::Button("Scan for firmware...")) {
#ifdef __EMSCRIPTEN__
				// Same story as Add Folder above: browsers have no
				// reliable cross-platform OS folder picker, and
				// anything the user uploads via the Files overlay
				// already lands in this well-known path.
				std::lock_guard<std::mutex> lock(g_setupWiz.scanMutex);
				const char *fixedFwPath[] = { "/home/web_user/firmware", nullptr };
				FirmwareScanCallback(nullptr, fixedFwPath, 0);
#else
				SDL_ShowOpenFolderDialog(FirmwareScanCallback, nullptr, window, nullptr, false);
#endif
			}

			if (!g_setupWiz.scanMessage.empty()) {
				ImGui::SameLine();
				ImGui::TextUnformatted(g_setupWiz.scanMessage.c_str());
			}
			break;
		}

		case 11: // Post-firmware
			ImGui::TextWrapped(
				"ROM image setup is complete.\n\n"
				"If you want to set up more firmware ROM images in the future, this can "
				"be done through the menu option System > Firmware Images."
			);
			break;

		case 20: { // Select system
			ImGui::TextWrapped(
				"Select the type of system to emulate. This can be changed later from "
				"the System menu."
			);
			ImGui::Spacing();

			bool is5200 = (sim.GetHardwareMode() == kATHardwareMode_5200);
			bool isComputer = !is5200;

			if (ImGui::RadioButton("Computer (XL/XE)", isComputer) && !isComputer) {
				uint32 profileId = ATGetDefaultProfileId(kATDefaultProfile_XL);
				ATSettingsSwitchProfile(profileId);
			}
			if (ImGui::RadioButton("Atari 5200", is5200) && !is5200) {
				uint32 profileId = ATGetDefaultProfileId(kATDefaultProfile_5200);
				ATSettingsSwitchProfile(profileId);
			}
			break;
		}

		case 21: { // Video standard (skipped for 5200)
			ImGui::TextWrapped(
				"Select the video standard. NTSC (60Hz) is the North American standard. "
				"PAL (50Hz) is the European standard.\n\n"
				"This affects timing and color palette. Most software is designed for NTSC."
			);
			ImGui::Spacing();

			bool isNTSC = (sim.GetVideoStandard() == kATVideoStandard_NTSC
				|| sim.GetVideoStandard() == kATVideoStandard_PAL60);

			if (ImGui::RadioButton("NTSC (60 Hz)", isNTSC) && !isNTSC)
				ATSetVideoStandard(kATVideoStandard_NTSC);
			if (ImGui::RadioButton("PAL (50 Hz)", !isNTSC) && isNTSC)
				ATSetVideoStandard(kATVideoStandard_PAL);
			break;
		}

		case 30: { // Experience level
			ImGui::TextWrapped(
				"Select the emulation experience level.\n\n"
				"Authentic mode enables hardware artifacting, accurate disk timing, and "
				"drive sounds for a more realistic experience.\n\n"
				"Convenient mode enables SIO patches for fast loading and disables "
				"hardware artifacts for a cleaner experience."
			);
			ImGui::Spacing();

			bool isAuthentic = (sim.GetGTIA().GetArtifactingMode() != ATArtifactMode::None);

			if (ImGui::RadioButton("Authentic", isAuthentic) && !isAuthentic) {
				sim.GetGTIA().SetArtifactingMode(ATArtifactMode::AutoHi);
				sim.SetCassetteSIOPatchEnabled(false);
				sim.SetDiskSIOPatchEnabled(false);
				sim.SetDiskAccurateTimingEnabled(true);
				ATUISetDriveSoundsEnabled(true);
				ATUISetDisplayFilterMode(kATDisplayFilterMode_Bilinear);
			}
			if (ImGui::RadioButton("Convenient", !isAuthentic) && isAuthentic) {
				ATUISetDriveSoundsEnabled(false);
				sim.SetCassetteSIOPatchEnabled(true);
				sim.SetDiskSIOPatchEnabled(true);
				sim.SetDiskAccurateTimingEnabled(false);
				sim.GetGTIA().SetArtifactingMode(ATArtifactMode::None);
				ATUISetDisplayFilterMode(kATDisplayFilterMode_SharpBilinear);
				ATUISetViewFilterSharpness(+1);
			}
			break;
		}

		case 40: // Finish (computer)
			if (IsGamingModeSelected()) {
				ImGui::TextWrapped(
					"Setup is now complete.\n\n"
					"Click Finish to enter Gaming Mode. The Game Library will be your "
					"home screen — browse and launch your Atari games from there.\n\n"
					"You can add or remove game folders at any time from Settings > "
					"Game Library.\n\n"
					"To repeat this process, switch to Desktop Mode and choose "
					"Tools > First Time Setup..."
				);
			} else {
				ImGui::TextWrapped(
					"Setup is now complete.\n\n"
					"Click Finish to exit and power up the emulated computer. You can then "
					"use the File > Boot Image... menu option to boot a disk, cartridge, or "
					"cassette tape image, or start a program.\n\n"
					"If you want to repeat this process in the future, the setup wizard can "
					"be restarted via the Tools menu."
				);
			}
			break;

		case 41: // Finish (5200)
			if (IsGamingModeSelected()) {
				ImGui::TextWrapped(
					"Setup is now complete.\n\n"
					"Click Finish to enter Gaming Mode. The 5200 needs a cartridge to "
					"work — use \"Boot Game\" in the Game Library to attach and start "
					"a cartridge image.\n\n"
					"To repeat this process, switch to Desktop Mode and choose "
					"Tools > First Time Setup..."
				);
			} else {
				ImGui::TextWrapped(
					"Setup is now complete.\n\n"
					"Click Finish to exit and power up the emulated console. The 5200 needs "
					"a cartridge to work, so select File > Boot Image... to attach and start "
					"a cartridge image.\n\n"
					"You will probably want to check your controller settings. The default "
					"setup binds F2-F4, the digit key row, arrow keys, and Ctrl/Shift to "
					"joystick 1. Alternate bindings can be selected from the Input menu or "
					"new ones can be defined in Input > Input Mappings.\n\n"
					"If you want to repeat this process in the future, choose Tools > First "
					"Time Setup... from the menu."
				);
			}
			break;
		}

		ImGui::EndChild();
	}

	// Bottom buttons
	ImGui::Separator();
	int prevPage = GetWizPrevPage(g_setupWiz.page);
	int nextPage = GetWizNextPage(g_setupWiz.page);
	bool canPrev = (prevPage >= 0);
	bool canNext = (nextPage >= 0);

	ImGui::BeginDisabled(!canPrev);
	if (ImGui::Button("< Prev"))
		g_setupWiz.page = prevPage;
	ImGui::EndDisabled();

	ImGui::SameLine();

	if (canNext) {
		if (ImGui::Button("Next >")) {
			g_setupWiz.wentPastFirst = true;
			g_setupWiz.page = nextPage;
		}
	} else {
		if (ImGui::Button("Finish")) {
			if (g_setupWiz.wentPastFirst) {
				sim.LoadROMs();
				sim.ColdReset();
			}
			ApplyPendingUIMode(window);
			g_setupWiz.Reset();
			state.showSetupWizard = false;
		}
	}

	ImGui::SameLine();
	if (ImGui::Button("Close")) {
		if (g_setupWiz.wentPastFirst) {
			sim.LoadROMs();
			sim.ColdReset();
		}
		ApplyPendingUIMode(window);
		g_setupWiz.Reset();
		state.showSetupWizard = false;
	}

	ImGui::End();
}

