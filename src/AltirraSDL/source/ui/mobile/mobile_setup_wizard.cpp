//	AltirraSDL - First Time Setup Wizard, Gaming Mode renderer
//
//	The shared state (g_setupWiz, page navigation, async pump,
//	mode-swap, finish path) lives in ui_tools_setup_wizard.cpp via
//	setup_wizard_shared.h.  This file is one of two renderers and is
//	dispatched from ATMobileUI_Render when currentScreen ==
//	ATMobileUIScreen::SetupWizard.
//
//	The Desktop renderer (ATUIRenderSetupWizard) is the other half;
//	both renderers operate on the same g_setupWiz fields so that a user
//	who toggles modes mid-wizard keeps their progress.

#include <stdafx.h>
#include <algorithm>
#include <vector>
#include <imgui.h>
#include <SDL3/SDL.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/registry.h>

#include "ui_mobile.h"
#include "ui_main.h"
#include "touch_widgets.h"
#include "simulator.h"
#include "gtia.h"
#include "constants.h"
#include "firmwaremanager.h"
#include "uiaccessors.h"
#include "uitypes.h"
#include "options.h"
#include "settings.h"
#include "ui_mode.h"
#include "inputmanager.h"
#include "inputmap.h"
#include "adaptive_input.h"
#include "display_backend.h"
#include <at/atcore/media.h>

#include "mobile_internal.h"
#include "../tools/setup_wizard_shared.h"
#include "../gamelibrary/game_library.h"

extern ATSimulator g_sim;
extern ATMobileUIState g_mobileState;
extern void ATRegistryFlushToDisk();

// =========================================================================
// Helpers
// =========================================================================

// Step indicator label for the top of the wizard window.  Mirrors the
// Desktop sidebar but inline (touch UIs don't have horizontal screen
// real-estate for a permanent sidebar).
static const char *WizStepLabel(int page) {
	if (page == 0)                  return "Welcome";
	if (page == 1)                  return "Interface mode";
	if (page >= 2 && page <= 4)     return "Game Library";
	if (page == 5)                  return "Appearance";
	if (page == 6)                  return "Screen Effects";
	if (page >= 10 && page <= 19)   return "Firmware";
	if (page >= 20 && page <= 29)   return "System";
	if (page == 30)                 return "Experience";
	if (page == 35)                 return "Joystick";
	if (page >= 40 && page <= 49)   return "Finish";
	return "";
}

// Two-line radio-style choice for full-screen gaming wizards.  Wraps
// ATTouchListItem (title + subtitle card with selected accent + no
// chevron, since the choice has no further drill-down) so every
// wizard-page tile shares the same visual language as the rest of
// Gaming Mode (Settings home cards, Disk Drive rows, etc.).
static bool WizChoiceTile(const char *title, const char *subtitle,
	bool selected)
{
	return ATTouchListItem(title, subtitle, selected, /*chevron=*/false);
}

// Compact "Original ROM / Built-in" inline status used in firmware
// status rows.  AltirraOS covers every firmware type when an original
// ROM isn't loaded, so we never tell the user something is "missing"
// — calling out "Built-in (Altirra)" makes it clear emulation works
// regardless.
static void WizStatusBadge(bool present) {
	const ATMobilePalette &pal = ATMobileGetPalette();
	if (present) {
		ImGui::PushStyleColor(ImGuiCol_Text, ATMobileCol(pal.success));
		ImGui::TextUnformatted("Original ROM");
		ImGui::PopStyleColor();
	} else {
		ImGui::PushStyleColor(ImGuiCol_Text, ATMobileCol(pal.textMuted));
		ImGui::TextUnformatted("Built-in");
		ImGui::PopStyleColor();
	}
}

// =========================================================================
// Per-page rendering
// =========================================================================

static void WizMobile_Welcome() {
	ImGui::TextWrapped(
		"Welcome to Altirra!\n\n"
		"This wizard helps you set up the emulator.  Tap Next to begin, "
		"or Close to skip — every option here can also be configured "
		"later from the hamburger menu.");
}

static void WizMobile_InterfaceMode(SDL_Window *window) {
	ImGui::TextWrapped(
		"Choose your preferred interface mode.  You can switch any time "
		"from the View menu (Desktop) or the hamburger menu (Gaming).");
	ImGui::Dummy(ImVec2(0, dp(8.0f)));

	int sel = (int)ATUIGetMode();

	if (WizChoiceTile("Desktop Mode",
		"Menu bar, keyboard shortcuts, mouse-driven",
		sel == (int)ATUIMode::Desktop))
	{
		Wiz_ApplyMode(ATUIMode::Desktop, window);
	}

	ImGui::Dummy(ImVec2(0, dp(8.0f)));

	if (WizChoiceTile("Gaming Mode",
		"Large buttons, gamepad/touch navigation",
		sel == (int)ATUIMode::Gaming))
	{
		Wiz_ApplyMode(ATUIMode::Gaming, window);
	}
}

static void WizMobile_GameLibrary(SDL_Window *window) {
	(void)window;
	ImGui::TextWrapped(
		"Gaming Mode uses a Game Library as your home screen.  Add "
		"folders containing your Atari game files (.atr, .xex, .car, "
		".cas, etc.) to browse and play them.");
	ImGui::Dummy(ImVec2(0, dp(8.0f)));

	GameBrowser_Init();
	ATGameLibrary *lib = GetGameLibrary();
	if (lib) {
		if (lib->IsScanComplete())
			lib->ConsumeScanResults();

		auto sources = lib->GetSources();

		ATTouchSection("Game Folders");

		for (size_t i = 0; i < sources.size(); ++i) {
			if (sources[i].mbIsArchive || sources[i].mbIsFile)
				continue;

			ImGui::PushID((int)i);
			VDStringA pathU8 = VDTextWToU8(sources[i].mPath);
			float rowH = dp(44.0f);
			ImVec2 cursor = ImGui::GetCursorScreenPos();
			float availW = ImGui::GetContentRegionAvail().x;
			float removeW = dp(40.0f);

			ImGui::PushClipRect(ImVec2(cursor.x, cursor.y),
				ImVec2(cursor.x + availW - removeW - dp(4.0f),
					cursor.y + rowH), true);
			ImGui::SetCursorPosY(ImGui::GetCursorPosY()
				+ (rowH - ImGui::GetTextLineHeight()) * 0.5f);
			ImGui::TextUnformatted(pathU8.c_str());
			ImGui::PopClipRect();

			ImGui::SameLine(availW - removeW);
			float btnY = cursor.y + (rowH - dp(32.0f)) * 0.5f
				- ImGui::GetCursorScreenPos().y + ImGui::GetCursorPosY();
			ImGui::SetCursorPosY(btnY);
			if (ATTouchButton("X##rm", ImVec2(dp(32.0f), dp(32.0f)),
				ATTouchButtonStyle::Subtle))
			{
				sources.erase(sources.begin() + i);
				lib->SetSources(sources);
				lib->PurgeRemovedSourceEntries();
				lib->SaveSettingsToRegistry();
				lib->StartScan();
				ATRegistryFlushToDisk();
				ImGui::PopID();
				break;
			}

			ImGui::SetCursorPosY(cursor.y - ImGui::GetWindowPos().y
				+ ImGui::GetScrollY() + rowH);
			ImGui::PopID();
		}

		if (lib->IsScanning()) {
			const ATMobilePalette &pal = ATMobileGetPalette();
			ImGui::PushStyleColor(ImGuiCol_Text, ATMobileCol(pal.accent));
			ImGui::Text("Scanning... %d games found", lib->GetScanProgress());
			ImGui::PopStyleColor();
		} else if (!sources.empty()) {
			size_t count = lib->GetEntryCount();
			ImGui::Text("%d game%s in your library.",
				(int)count, count == 1 ? "" : "s");
		}

		ImGui::Dummy(ImVec2(0, dp(8.0f)));
	}

	// Use the Gaming Mode mobile folder picker (FileBrowser screen) so
	// the wizard reuses the same touch UX as Settings > Game Library.
	// On WASM the OS folder picker isn't reachable, so the WASM build
	// goes through Wiz_TriggerLibFolderPicker which seeds the well-known
	// /home/web_user/games path directly.  Strict #if/#else split so the
	// non-WASM s_folderPicker* state never gets set when the file
	// browser screen wouldn't actually open.
#ifdef __EMSCRIPTEN__
	if (ATTouchButton("+ Add Folder", ImVec2(-1, dp(48.0f)),
		ATTouchButtonStyle::Accent))
	{
		Wiz_TriggerLibFolderPicker(window);
	}
#else
	if (ATTouchButton("+ Add Folder", ImVec2(-1, dp(48.0f)),
		ATTouchButtonStyle::Accent))
	{
		// Mobile folder picker: stash a callback that updates the
		// library, return target = SetupWizard so the file browser
		// returns here when the user confirms or cancels.
		s_zipArchivePath.clear();
		s_zipInternalDir.clear();
		s_archiveFilePickerMode = false;
		s_archiveFilePickerCallback = nullptr;
		s_folderPickerMode = true;
		s_folderPickerReturnScreen = ATMobileUIScreen::SetupWizard;
		s_folderPickerCallback = [](const VDStringW &path) {
			GameBrowser_Init();
			ATGameLibrary *l = GetGameLibrary();
			if (!l) return;
			auto srcs = l->GetSources();
			GameSource src;
			src.mPath = path;
			src.mbIsArchive = false;
			srcs.push_back(std::move(src));
			l->SetSources(std::move(srcs));
			l->SaveSettingsToRegistry();
			l->StartScan();
			ATRegistryFlushToDisk();
		};
		s_fileBrowserNeedsRefresh = true;
		g_mobileState.currentScreen = ATMobileUIScreen::FileBrowser;
	}
#endif
}

static void WizMobile_Appearance() {
	ImGui::TextWrapped(
		"Customize the look of Gaming Mode.  Changes apply immediately "
		"and are saved to disk so a process kill won't lose them.");
	ImGui::Dummy(ImVec2(0, dp(8.0f)));

	ATTouchSection("Theme");

	{
		int th = (int)g_ATOptions.mThemeMode;
		static const char *themes[] = { "System", "Light", "Dark" };
		if (ATTouchSegmented("Theme", &th, themes, 3)) {
			ATOptions prev(g_ATOptions);
			g_ATOptions.mThemeMode = (ATUIThemeMode)th;
			if (g_ATOptions != prev) {
				g_ATOptions.mbDirty = true;
				ATOptionsRunUpdateCallbacks(&prev);
				ATOptionsSave();
				try { ATRegistryFlushToDisk(); } catch (...) {}
				ATUIApplyTheme();
			}
		}
		ATTouchMutedText(
			"System follows your device's dark/light preference.");
	}

	ATTouchSection("Performance preset");

	{
		int p = g_mobileState.performancePreset;
		static const char *items[] = { "Efficient", "Balanced", "Quality" };
		if (ATTouchSegmented("Preset", &p, items, 3)) {
			g_mobileState.performancePreset = p;
			SaveMobileConfig(g_mobileState);
			ATMobileUI_ApplyPerformancePreset(g_mobileState);
		}
		if (g_mobileState.performancePreset == 3) {
			const ATMobilePalette &pal = ATMobileGetPalette();
			ImGui::PushStyleColor(ImGuiCol_Text, ATMobileCol(pal.warning));
			ImGui::TextUnformatted(
				"Custom — pick a preset above to revert to bundled defaults.");
			ImGui::PopStyleColor();
		}
		ATTouchMutedText(
			"Bundles screen effects (scanlines, bloom, distortion) and "
			"the display filter so you don't have to tune each one.  "
			"Pick Efficient on older devices, Quality on flagships.");
	}
}

static void WizMobile_Firmware(SDL_Window *window) {
	ImGui::TextWrapped(
		"Altirra ships with built-in replacements (\"AltirraOS\") for "
		"every standard ROM, so emulation works fine without any "
		"original ROM image.\n\n"
		"If you have original ROM images you can scan a folder for them "
		"now for better compatibility with a small number of "
		"timing-sensitive titles.  Otherwise just tap Next.");
	ImGui::Dummy(ImVec2(0, dp(8.0f)));

	ATTouchSection("Firmware Status");

	ATFirmwareManager &fwm = *g_sim.GetFirmwareManager();
	static const struct { ATFirmwareType type; const char *name; } kFirmware[] = {
		{ kATFirmwareType_Kernel800_OSB, "800 OS (OS-B)" },
		{ kATFirmwareType_KernelXL,      "XL/XE OS" },
		{ kATFirmwareType_Basic,         "BASIC" },
		{ kATFirmwareType_Kernel5200,    "5200 OS" },
	};

	for (auto &fw : kFirmware) {
		float rowH = dp(40.0f);
		ImVec2 cursor = ImGui::GetCursorScreenPos();
		ImGui::SetCursorPosY(ImGui::GetCursorPosY()
			+ (rowH - ImGui::GetTextLineHeight()) * 0.5f);
		ImGui::TextUnformatted(fw.name);
		ImGui::SameLine(ImGui::GetContentRegionAvail().x - dp(80.0f));
		uint64 fwid = fwm.GetCompatibleFirmware(fw.type);
		bool present = (fwid && fwid >= kATFirmwareId_Custom);
		WizStatusBadge(present);
		ImGui::SetCursorPosY(cursor.y - ImGui::GetWindowPos().y
			+ ImGui::GetScrollY() + rowH);
	}

	ImGui::Dummy(ImVec2(0, dp(12.0f)));

	if (ATTouchButton("Scan for Firmware...", ImVec2(-1, dp(48.0f)),
		ATTouchButtonStyle::Accent))
	{
		Wiz_TriggerFirmwareScan(window);
	}

	if (!g_setupWiz.scanMessage.empty()) {
		ImGui::Dummy(ImVec2(0, dp(4.0f)));
		const ATMobilePalette &pal = ATMobileGetPalette();
		ImGui::PushStyleColor(ImGuiCol_Text, ATMobileCol(pal.textMuted));
		ImGui::TextWrapped("%s", g_setupWiz.scanMessage.c_str());
		ImGui::PopStyleColor();
	}
}

static void WizMobile_PostFirmware() {
	ImGui::TextWrapped(
		"ROM image setup is complete.\n\n"
		"You can add more ROM images later from "
		"Settings > Firmware.");
}

static void WizMobile_System() {
	ImGui::TextWrapped(
		"Pick the type of system to emulate.  This can be changed any "
		"time from Settings > Machine.\n\n"
		"Both options work without original ROMs — Altirra includes "
		"built-in firmware (\"AltirraOS\") that covers both XL/XE and "
		"the 5200.");
	ImGui::Dummy(ImVec2(0, dp(8.0f)));

	bool is5200 = (g_sim.GetHardwareMode() == kATHardwareMode_5200);

	if (WizChoiceTile("Computer (XL/XE)",
		"Disk drives, BASIC, the classic home computer",
		!is5200))
	{
		if (is5200) {
			uint32 profileId = ATGetDefaultProfileId(kATDefaultProfile_XL);
			ATSettingsSwitchProfile(profileId);
			g_setupWiz.needsHardwareReset = true;
		}
	}
	ImGui::Dummy(ImVec2(0, dp(8.0f)));

	if (WizChoiceTile("Atari 5200",
		"Cartridge-only console with analog joysticks",
		is5200))
	{
		if (!is5200) {
			uint32 profileId = ATGetDefaultProfileId(kATDefaultProfile_5200);
			ATSettingsSwitchProfile(profileId);
			g_setupWiz.needsHardwareReset = true;
		}
	}
}

static void WizMobile_VideoStandard() {
	ImGui::TextWrapped(
		"Pick the video standard.  NTSC (60Hz) is North American, PAL "
		"(50Hz) is European.  Most software is designed for NTSC.");
	ImGui::Dummy(ImVec2(0, dp(8.0f)));

	bool isNTSC = (g_sim.GetVideoStandard() == kATVideoStandard_NTSC
		|| g_sim.GetVideoStandard() == kATVideoStandard_PAL60);

	if (WizChoiceTile("NTSC (60 Hz)", "North American standard", isNTSC)) {
		if (!isNTSC) {
			ATSetVideoStandard(kATVideoStandard_NTSC);
			g_setupWiz.needsHardwareReset = true;
		}
	}
	ImGui::Dummy(ImVec2(0, dp(8.0f)));

	if (WizChoiceTile("PAL (50 Hz)", "European standard", !isNTSC)) {
		if (isNTSC) {
			ATSetVideoStandard(kATVideoStandard_PAL);
			g_setupWiz.needsHardwareReset = true;
		}
	}
}

static void WizMobile_Experience() {
	ImGui::TextWrapped(
		"Choose the emulation experience level.");
	ImGui::Dummy(ImVec2(0, dp(8.0f)));

	bool isAuthentic = (g_sim.GetGTIA().GetArtifactingMode() != ATArtifactMode::None);

	if (WizChoiceTile("Authentic",
		"Hardware artifacting, accurate disk timing, drive sounds",
		isAuthentic))
	{
		if (!isAuthentic) {
			g_sim.GetGTIA().SetArtifactingMode(ATArtifactMode::AutoHi);
			g_sim.SetCassetteSIOPatchEnabled(false);
			g_sim.SetDiskSIOPatchEnabled(false);
			g_sim.SetDiskAccurateTimingEnabled(true);
			ATUISetDriveSoundsEnabled(true);
			ATUISetDisplayFilterMode(kATDisplayFilterMode_Bilinear);
			g_setupWiz.needsHardwareReset = true;
		}
	}
	ImGui::Dummy(ImVec2(0, dp(8.0f)));

	if (WizChoiceTile("Convenient",
		"SIO patches for fast loading, no artifacts",
		!isAuthentic))
	{
		if (isAuthentic) {
			ATUISetDriveSoundsEnabled(false);
			g_sim.SetCassetteSIOPatchEnabled(true);
			g_sim.SetDiskSIOPatchEnabled(true);
			g_sim.SetDiskAccurateTimingEnabled(false);
			g_sim.GetGTIA().SetArtifactingMode(ATArtifactMode::None);
			ATUISetDisplayFilterMode(kATDisplayFilterMode_SharpBilinear);
			ATUISetViewFilterSharpness(+1);
			g_setupWiz.needsHardwareReset = true;
		}
	}
}

static void WizMobile_Joystick() {
	const bool is5200 =
		(g_sim.GetHardwareMode() == kATHardwareMode_5200);

	ATInputManager *pIM = g_sim.GetInputManager();
	if (!pIM) {
		ATTouchMutedText("Input manager unavailable.");
		return;
	}

	const bool adaptive = ATAdaptiveInput::IsEnabled();

	if (adaptive) {
		// Adaptive on (default).  Skip the "pick a map" exercise
		// entirely — the user's controls are already wired up.  Just
		// confirm what works and offer a way out for power users.
		if (is5200) {
			ImGui::TextWrapped(
				"Controls are auto-configured.  Whatever input source "
				"you have connected — keyboard, gamepad — drives the "
				"5200 controller on port 1.  The on-screen joypad in "
				"Gaming Mode also drives it.");
		} else {
			ImGui::TextWrapped(
				"Controls are auto-configured.  Keyboard arrows, "
				"numpad, any connected gamepad, and the on-screen "
				"joypad in Gaming Mode all drive joystick port 1 at "
				"the same time.  Just play.");
		}
		ImGui::Dummy(ImVec2(0, dp(8.0f)));

		bool flag = adaptive;
		if (ATTouchToggle("Adaptive input (recommended)", &flag))
			ATAdaptiveInput::SetEnabled(flag);
		ImGui::Dummy(ImVec2(0, dp(4.0f)));

		ATTouchMutedText(
			"Turn this off if you want exclusive control — only one "
			"input source bound to port 1.  You can change this any "
			"time from Settings > Controls.");
		return;
	}

	// Adaptive off — show the original single-map picker so power
	// users can lock port 1 to one specific source.  This matches the
	// pre-Adaptive behaviour exactly.
	if (is5200) {
		ImGui::TextWrapped(
			"Pick the input mapping for 5200 controller port 1.  A "
			"keyboard-to-5200-Controller map is preselected as a "
			"sensible default.\n\n"
			"Turn Adaptive Input back on to let every connected source "
			"drive port 1 simultaneously.");
	} else {
		ImGui::TextWrapped(
			"Pick the input mapping for joystick port 1.  \"Arrow Keys "
			"-> Joystick (port 1)\" is preselected as a sensible "
			"default.\n\n"
			"Turn Adaptive Input back on to let every connected source "
			"(keyboard, gamepad, on-screen joypad) drive port 1 "
			"simultaneously.");
	}
	ImGui::Dummy(ImVec2(0, dp(8.0f)));

	bool flag = adaptive;
	if (ATTouchToggle("Adaptive input (recommended)", &flag))
		ATAdaptiveInput::SetEnabled(flag);
	ImGui::Dummy(ImVec2(0, dp(8.0f)));

	std::vector<WizPortMapEntry> entries;
	Wiz_GatherPortMaps(*pIM, 0, entries);

	if (!g_setupWiz.joystickPageSeeded) {
		Wiz_SeedDefaultPort1Map(*pIM, entries);
		g_setupWiz.joystickPageSeeded = true;
	}

	bool anyActive = false;
	for (auto &e : entries) if (e.active) { anyActive = true; break; }

	// "None" choice — full-width touch row with subtle style when not selected.
	if (ATTouchButton("None",
		ImVec2(-1, dp(48.0f)),
		!anyActive ? ATTouchButtonStyle::Accent : ATTouchButtonStyle::Neutral))
	{
		Wiz_ActivatePortMap(*pIM, entries, nullptr);
	}
	ImGui::Dummy(ImVec2(0, dp(4.0f)));

	for (size_t i = 0; i < entries.size(); ++i) {
		auto &e = entries[i];
		ImGui::PushID((int)i);
		if (ATTouchButton(e.name.c_str(),
			ImVec2(-1, dp(48.0f)),
			e.active ? ATTouchButtonStyle::Accent
			         : ATTouchButtonStyle::Neutral))
		{
			Wiz_ActivatePortMap(*pIM, entries, e.map);
		}
		ImGui::PopID();
		ImGui::Dummy(ImVec2(0, dp(4.0f)));
	}
}

static void WizMobile_Finish() {
	bool is5200 = (g_sim.GetHardwareMode() == kATHardwareMode_5200);
	if (is5200) {
		ImGui::TextWrapped(
			"Setup is complete.\n\n"
			"Tap Done to enter Gaming Mode.  The 5200 needs a cartridge "
			"to run — pick one from the Game Library home screen.");
	} else {
		ImGui::TextWrapped(
			"Setup is complete.\n\n"
			"Tap Done to enter Gaming Mode.  The Game Library is your "
			"home screen — browse and launch your Atari games from "
			"there.");
	}
	ImGui::Dummy(ImVec2(0, dp(8.0f)));
	ATTouchMutedText(
		"You can run this wizard again from Settings > About > Repeat "
		"First Time Setup, or from Tools > First Time Setup in Desktop "
		"Mode.");
}

// =========================================================================
// Top-level renderer
// =========================================================================

void RenderMobileSetupWizard(ATSimulator &sim, ATUIState &uiState,
	ATMobileUIState &mobileState, SDL_Window *window)
{
	// Stale screen guard: if Wiz_Finish ran in another path and didn't
	// clear currentScreen (shouldn't happen, but defensive), bail and
	// rejoin the Game Library.
	if (!uiState.showSetupWizard) {
		mobileState.currentScreen = ATMobileUIScreen::GameBrowser;
		return;
	}

	// Drain async scan/folder results on the main thread.  Both
	// renderers must do this; the SDL callbacks just stash the path.
	Wiz_PumpAsync();

	ImGuiIO &io = ImGui::GetIO();
	const ATMobilePalette &pal = ATMobileGetPalette();

	// Full-screen palette-aware background — themes switch cleanly.
	ImGui::GetBackgroundDrawList()->AddRectFilled(
		ImVec2(0, 0), io.DisplaySize, pal.windowBg);

	// Inset window inside safe area.
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

	if (!ImGui::Begin("##MobileSetupWizard", nullptr, flags)) {
		ImGui::End();
		return;
	}

	// Back-key handling: B / ESC / Backspace closes the wizard, just
	// like the Desktop renderer's ESC handler.
	{
		bool back = ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false);
		if (!ImGui::IsAnyItemActive()) {
			back = back
				|| ImGui::IsKeyPressed(ImGuiKey_Escape, false)
				|| ImGui::IsKeyPressed(ImGuiKey_Backspace, false);
		}
		if (back) {
			Wiz_Finish(sim, uiState, window);
			ImGui::End();
			return;
		}
	}

	// Header — single-row "<Title>  ·  <Step>" so it occupies the same
	// 48dp band as the Settings / Disk Drives / About screens, instead
	// of stacking a large title above the step label and eating two
	// lines of vertical space.  Uses the same baseline-centring trick as
	// mobile_settings.cpp:163 so the text sits in the middle of the row
	// regardless of the line-height the current font reports.
	const float headerH = dp(48.0f);
	{
		ImVec2 hdrStart = ImGui::GetCursorPos();

		ImGui::SetCursorPosY(hdrStart.y
			+ (headerH - ImGui::GetTextLineHeight()) * 0.5f);
		ImGui::PushStyleColor(ImGuiCol_Text, ATMobileCol(pal.textTitle));
		ImGui::TextUnformatted("First Time Setup");
		ImGui::PopStyleColor();

		const char *step = WizStepLabel(g_setupWiz.page);
		if (step && *step) {
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Text, ATMobileCol(pal.textMuted));
			ImGui::TextUnformatted("  ·  ");
			ImGui::SameLine();
			ImGui::TextUnformatted(step);
			ImGui::PopStyleColor();
		}

		// Drop the cursor to just past the header band so the separator
		// lines up cleanly with the right-edge button-less header used
		// elsewhere.
		ImGui::SetCursorPos(ImVec2(hdrStart.x, hdrStart.y + headerH));
	}
	ImGui::Separator();
	ImGui::Dummy(ImVec2(0, dp(4.0f)));

	// Footer reserve — three 48dp buttons + their item spacing + a 4dp
	// top-of-footer separator gap so the body never overlaps the
	// touch-target row.  ItemSpacing.y is what ImGui inserts between
	// the body's EndChild and the Separator below it.
	const ImGuiStyle &style = ImGui::GetStyle();
	const float footerBtnH = dp(48.0f);
	const float footerH = footerBtnH
		+ style.ItemSpacing.y * 2.0f
		+ style.FramePadding.y * 2.0f
		+ dp(4.0f) /* dummy above buttons */;

	// Body: scrollable content.  GetContentRegionAvail() is recalculated
	// here, AFTER the header has been drawn — same pattern as the
	// About-screen credits child (mobile_about_wizard.cpp:127-135) so
	// the body actually fits the remaining vertical space on every
	// screen size.
	float bodyH = ImGui::GetContentRegionAvail().y - footerH;
	if (bodyH < dp(80.0f)) bodyH = dp(80.0f);  // never collapse to 0

	ImGui::BeginChild("WizMobileBody",
		ImVec2(0, bodyH),
		ImGuiChildFlags_NavFlattened);
	ATTouchDragScroll();

	switch (g_setupWiz.page) {
		case 0:  WizMobile_Welcome();          break;
		case 1:  WizMobile_InterfaceMode(window); break;
		case 2:  WizMobile_GameLibrary(window); break;
		case 5:  WizMobile_Appearance();        break;
		// Page 6 is desktop-only (skipped by Wiz_GetNextPage in Gaming);
		// if we somehow land here, render Appearance as a fallback.
		case 6:  WizMobile_Appearance();        break;
		case 10: WizMobile_Firmware(window);    break;
		case 11: WizMobile_PostFirmware();      break;
		case 20: WizMobile_System();            break;
		case 21: WizMobile_VideoStandard();     break;
		case 30: WizMobile_Experience();        break;
		case 35: WizMobile_Joystick();          break;
		case 40:
		case 41: WizMobile_Finish();            break;
		default:
			// Unknown page — show a hint and let the user back out.
			ATTouchMutedText("(Unknown wizard page.)");
			break;
	}

	ATTouchEndDragScroll();
	ImGui::EndChild();

	// --- Footer: Prev / Next-or-Done / Close ---
	ImGui::Separator();
	ImGui::Dummy(ImVec2(0, dp(4.0f)));

	int prev = Wiz_GetPrevPage(g_setupWiz.page);
	int next = Wiz_GetNextPage(g_setupWiz.page);
	bool canPrev = (prev >= 0);
	bool canNext = (next >= 0);

	// Three equal-width buttons that fully consume the footer width,
	// accounting for the two ImGui::SameLine spacings between them.
	float footerAvailW = ImGui::GetContentRegionAvail().x;
	float btnW = (footerAvailW - style.ItemSpacing.x * 2.0f) / 3.0f;
	if (btnW < dp(80.0f)) btnW = dp(80.0f);
	float btnH = footerBtnH;

	ImGui::BeginDisabled(!canPrev);
	if (ATTouchButton("< Prev", ImVec2(btnW, btnH))) {
		g_setupWiz.page = prev;
	}
	ImGui::EndDisabled();

	ImGui::SameLine();

	if (canNext) {
		if (ATTouchButton("Next >", ImVec2(btnW, btnH),
			ATTouchButtonStyle::Accent))
		{
			g_setupWiz.wentPastFirst = true;
			g_setupWiz.page = next;
		}
	} else {
		if (ATTouchButton("Done", ImVec2(btnW, btnH),
			ATTouchButtonStyle::Accent))
		{
			Wiz_Finish(sim, uiState, window);
			ImGui::End();
			return;
		}
	}

	ImGui::SameLine();
	if (ATTouchButton("Close", ImVec2(btnW, btnH))) {
		Wiz_Finish(sim, uiState, window);
		ImGui::End();
		return;
	}

	ImGui::End();
}
