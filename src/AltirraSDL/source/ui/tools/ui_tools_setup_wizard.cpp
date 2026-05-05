//	AltirraSDL - Tools dialog (split from ui_tools.cpp, Phase 2k)

#include <stdafx.h>
#include <algorithm>
#include <string>
#include <mutex>
#include <thread>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cwctype>
#include <vd2/system/strutil.h>
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
#include "ui_fonts.h"
#include "display_backend.h"
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
#include "inputmanager.h"
#include "inputmap.h"
#include "adaptive_input.h"
#include "setup_wizard_shared.h"

extern ATSimulator g_sim;
extern ATMobileUIState g_mobileState;

// =========================================================================
// First Time Setup Wizard
// Reference: src/Altirra/source/uisetupwizard.cpp
// =========================================================================

// Shared state — definition of the struct lives in setup_wizard_shared.h
// so the gaming-mode renderer (mobile_setup_wizard.cpp) can read/write
// the same fields when the user toggles modes mid-wizard.
SetupWizardState g_setupWiz;

void SetupWizardState::Reset() {
	page = 0;
	wentPastFirst = false;
	scanFound = 0;
	scanExisting = 0;
	scanMessage.clear();
	needsHardwareReset = false;
	joystickPageSeeded = false;
}

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
	g_setupWiz.scanMessage.sprintf("Firmware images recognized: %d (%d already present)",
		(int)detected.size(), (int)existing);

	// New ROM(s) loaded — close path needs to LoadROMs+ColdReset for them
	// to actually take effect on the running emulator.
	if (!detected.empty())
		g_setupWiz.needsHardwareReset = true;
}

// File dialog callbacks — may run on a background thread, so just store
// the path.  The actual processing happens on the main thread in
// Wiz_PumpAsync().
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

// Page 6 (Screen Effects) needs the GPU FX pipeline to do anything
// useful — the radio buttons toggle bloom/distortion/mask/vignette
// which are all SupportsScreenFX()-gated.  When the active display
// backend can't render them (WASM's SDL_Renderer, or the desktop
// SDL_Renderer fallback after a failed GL context creation), the
// page becomes a no-op, so we skip it the same way Gaming Mode does.
static bool Wiz_SkipScreenFXPage() {
	if (ATUIIsGamingMode())
		return true;
	IDisplayBackend *be = ATUIGetDisplayBackend();
	return !be || !be->SupportsScreenFX();
}

int Wiz_GetPrevPage(int page) {
	switch (page) {
		case 0:  return -1;
		case 1:  return 0;
		case 2:  return 1;
		case 5:  return 2;
		case 6:  return 5;
		// Gaming Mode merges Screen Effects (page 6) into the Appearance
		// page (5) via the performance preset, and backends without GPU
		// FX skip the page entirely, so back from Firmware jumps over
		// page 6 directly to 5 in both cases.
		case 10: return Wiz_SkipScreenFXPage() ? 5 : 6;
		case 11: return 10;
		case 20: return 11;
		case 21: return 20;
		case 30: return g_sim.GetHardwareMode() == kATHardwareMode_5200 ? 20 : 21;
		case 35: return 30;
		case 40: return 35;
		case 41: return 35;
		default: return 0;
	}
}

int Wiz_GetNextPage(int page) {
	switch (page) {
		case 0:  return 1;
		case 1:  return 2;
		case 2:  return 5;
		// Gaming Mode skips page 6 (Screen Effects) — the perf preset
		// in the merged Appearance page covers it.  Backends without
		// GPU FX (WASM, GL fallback) skip it too.
		case 5:  return Wiz_SkipScreenFXPage() ? 10 : 6;
		case 6:  return 10;
		case 10: return 11;
		case 11: return 20;
		case 20: return g_sim.GetHardwareMode() == kATHardwareMode_5200 ? 30 : 21;
		case 21: return 30;
		case 30: return 35;
		case 35: return g_sim.GetHardwareMode() == kATHardwareMode_5200 ? 41 : 40;
		default: return -1;
	}
}

void Wiz_PumpAsync() {
	std::string scanPath;
	std::string libPath;
	{
		std::lock_guard<std::mutex> lock(g_setupWiz.scanMutex);
		scanPath.swap(g_setupWiz.pendingScanPath);
		libPath.swap(g_setupWiz.pendingLibFolderPath);
	}

	if (!scanPath.empty())
		ATUIDoFirmwareScan(scanPath.c_str());

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

void Wiz_TriggerFirmwareScan(SDL_Window *window) {
#ifdef __EMSCRIPTEN__
	// Browsers can't offer a cross-platform OS folder picker.  The
	// firmware uploads always land in this fixed path, so seed the
	// pending-scan slot directly — going through FirmwareScanCallback
	// would re-enter the scanMutex from the same thread.
	fprintf(stderr, "[wasm] SetupWizard: Scan for Firmware -> /home/web_user/firmware\n");
	{
		std::lock_guard<std::mutex> lock(g_setupWiz.scanMutex);
		g_setupWiz.pendingScanPath = "/home/web_user/firmware";
	}
#else
	SDL_ShowOpenFolderDialog(FirmwareScanCallback, nullptr, window, nullptr, false);
#endif
}

void Wiz_TriggerLibFolderPicker(SDL_Window *window) {
#ifdef __EMSCRIPTEN__
	fprintf(stderr, "[wasm] SetupWizard: Add Folder -> /home/web_user/games\n");
	{
		std::lock_guard<std::mutex> lock(g_setupWiz.scanMutex);
		g_setupWiz.pendingLibFolderPath = "/home/web_user/games";
	}
#else
	SDL_ShowOpenFolderDialog(LibFolderCallback, nullptr, window, nullptr, false);
#endif
}

void Wiz_Open(ATUIState &state) {
	state.showSetupWizard = true;
	if (ATUIIsGamingMode())
		g_mobileState.currentScreen = ATMobileUIScreen::SetupWizard;
}

void Wiz_ApplyMode(ATUIMode newMode, SDL_Window *window) {
	if (newMode == ATUIGetMode())
		return;

	ATUISetMode(newMode);

	float cs = 1.0f;
	if (window) {
		cs = SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(window));
		if (cs < 1.0f) cs = 1.0f;
		if (cs > 4.0f) cs = 4.0f;
	}
	ATUIApplyModeStyle(cs);

	// Note: do not persist via ATUISaveMode() here.  The wizard's close
	// path is the single commit point, so the mode the user has on screen
	// at close time is what gets saved — even if they toggled several
	// times along the way.

	if (ATUIIsGamingMode()) {
		// Seed the mobile state so the Gaming Mode renderer takes over
		// next frame.  Effects/perf presets apply so the live preview
		// truly looks like Gaming Mode (palette, scanlines, etc.).
		GameBrowser_Init();
		ATMobileUI_ApplyVisualEffects(g_mobileState);
		ATMobileUI_ApplyPerformancePreset(g_mobileState);
		g_mobileState.currentScreen = ATMobileUIScreen::SetupWizard;
		g_sim.Pause();
	}
	// No matching cleanup when leaving Gaming → Desktop is needed: the
	// outer dispatcher in ui_main.cpp picks the renderer from
	// ATUIIsGamingMode() each frame, and Wiz_Finish clears the mobile
	// screen state if the wizard ends in Desktop Mode.
}

void Wiz_GatherPortMaps(ATInputManager &im, int portIdx,
	std::vector<WizPortMapEntry> &outEntries)
{
	outEntries.clear();

	uint32 mapCount = im.GetInputMapCount();
	for (uint32 i = 0; i < mapCount; ++i) {
		vdrefptr<ATInputMap> imap;
		if (im.GetInputMapByIndex(i, ~imap)) {
			if (imap->UsesPhysicalPort(portIdx)) {
				WizPortMapEntry e;
				e.map = imap;
				e.name = VDTextWToU8(imap->GetName(), -1);
				e.active = im.IsInputMapEnabled(imap);
				outEntries.push_back(std::move(e));
			}
		}
	}

	std::sort(outEntries.begin(), outEntries.end(),
		[](const WizPortMapEntry &a, const WizPortMapEntry &b) {
			return vdwcsicmp(a.map->GetName(), b.map->GetName()) < 0;
		});
}

void Wiz_ActivatePortMap(ATInputManager &im,
	const std::vector<WizPortMapEntry> &entries, ATInputMap *chosen)
{
	for (const auto &e : entries)
		im.ActivateInputMap(e.map, e.map == chosen);
}

void Wiz_SeedDefaultPort1Map(ATInputManager &im,
	std::vector<WizPortMapEntry> &entries)
{
	bool anyActive = false;
	for (const auto &e : entries) {
		if (e.active) { anyActive = true; break; }
	}
	if (anyActive)
		return;

	// Match by substring so a custom user map doesn't shadow the
	// canonical one as long as the canonical one is registered.
	auto containsCI = [](const wchar_t *s, const wchar_t *needle) {
		if (!s || !needle) return false;
		size_t nlen = 0; while (needle[nlen]) ++nlen;
		for (; *s; ++s) {
			size_t i = 0;
			while (i < nlen
				&& s[i]
				&& towlower((wint_t)s[i]) == towlower((wint_t)needle[i]))
				++i;
			if (i == nlen) return true;
		}
		return false;
	};

	const bool is5200 = (g_sim.GetHardwareMode() == kATHardwareMode_5200);

	ATInputMap *exact = nullptr;
	ATInputMap *fallback = nullptr;
	for (auto &e : entries) {
		const wchar_t *nm = e.map->GetName();
		if (!nm) continue;
		if (is5200) {
			// Canonical 5200 default: "Keyboard -> 5200 Controller
			// (absolute; port 1)".  Prefer absolute over relative since
			// the absolute map matches what the Windows wizard's "5200"
			// hardware path lands on.
			if (containsCI(nm, L"5200 Controller")
				&& containsCI(nm, L"absolute"))
			{
				exact = e.map;
				break;
			}
			// Fallback: any 5200 Controller map (relative/absolute/etc).
			if (!fallback && containsCI(nm, L"5200 Controller"))
				fallback = e.map;
		} else {
			// Canonical computer default: "Arrow Keys -> Joystick (port 1)".
			if (containsCI(nm, L"Arrow Keys")
				&& containsCI(nm, L"Joystick"))
			{
				exact = e.map;
				break;
			}
			if (!fallback && containsCI(nm, L"Arrow"))
				fallback = e.map;
		}
	}

	ATInputMap *chosen = exact ? exact : fallback;
	if (chosen) {
		Wiz_ActivatePortMap(im, entries, chosen);
		for (auto &x : entries) x.active = (x.map == chosen);
	}
}

void Wiz_Finish(ATSimulator &sim, ATUIState &state, SDL_Window *window) {
	(void)window;

	// Only reset the simulator when something downstream of it actually
	// changed.  Walking the wizard to glance at the Welcome / Interface
	// Mode / Game Library / Theme pages doesn't need a cold reset and
	// would pointlessly throw away a running game session.  Pages that
	// touch firmware, hardware mode, video standard, or experience-level
	// SIO patches set needsHardwareReset.
	if (g_setupWiz.wentPastFirst && g_setupWiz.needsHardwareReset) {
		sim.LoadROMs();
		sim.ColdReset();
	}

	// Persist whatever mode the user landed on.  Calling unconditionally
	// is safe — ATUISaveMode just writes the current value.
	ATUISaveMode();

	if (ATUIIsGamingMode()) {
		// Hand off to the Game Library home screen as the post-wizard target.
		GameBrowser_Init();
		ATMobileUI_ApplyVisualEffects(g_mobileState);
		ATMobileUI_ApplyPerformancePreset(g_mobileState);
		g_mobileState.currentScreen = ATMobileUIScreen::GameBrowser;
		g_sim.Pause();
	} else {
		// In case the wizard was opened in Gaming Mode and the user
		// toggled to Desktop before closing, clear the lingering mobile
		// screen so a future mode flip doesn't reopen the wizard screen.
		if (g_mobileState.currentScreen == ATMobileUIScreen::SetupWizard)
			g_mobileState.currentScreen = ATMobileUIScreen::None;
	}

	// Flush settings to disk so anything that was changed during the
	// wizard but only stored in the in-memory registry (e.g. mode
	// preference, options edits) survives a subsequent crash or
	// process kill.  Each setter has already done its part; this is a
	// catch-all that's cheap and idempotent.
	try {
		extern void ATRegistryFlushToDisk();
		ATRegistryFlushToDisk();
	} catch (...) {}

	g_setupWiz.Reset();
	state.showSetupWizard = false;
}

void ATUIRenderSetupWizard(ATSimulator &sim, ATUIState &state, SDL_Window *window) {
	// Drain async results (firmware scan path, library folder pick) on
	// the main thread before rendering — the SDL callbacks just stash
	// the path under a mutex.
	Wiz_PumpAsync();

	ImGui::SetNextWindowSize(ImVec2(620, 480), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	bool open = state.showSetupWizard;
	if (!ImGui::Begin("First Time Setup", &open,
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
		if (!open)
			Wiz_Finish(sim, state, window);
		ImGui::End();
		return;
	}

	if (!open || ATUICheckEscClose()) {
		Wiz_Finish(sim, state, window);
		ImGui::End();
		return;
	}

	float sidebarW = 140;

	// Left sidebar: step list
	{
		ImGui::BeginChild("WizSteps", ImVec2(sidebarW, -40), ImGuiChildFlags_Borders);

		// `desktopOnly` skips the Screen Effects step in Gaming Mode,
		// where the merged Appearance step's perf preset already covers
		// it.  Game Library is now offered in both modes.
		static const struct { int pageMin; int pageMax; const char *label; bool desktopOnly; } kSteps[] = {
			{ 0, 0, "Welcome", false },
			{ 1, 1, "Interface mode", false },
			{ 2, 4, "Game Library", false },
			{ 5, 5, "Appearance", false },
			{ 6, 6, "Screen Effects", true },
			{ 10, 19, "Setup firmware", false },
			{ 20, 29, "Select system", false },
			{ 30, 30, "Experience", false },
			{ 35, 35, "Joystick", false },
			{ 40, 49, "Finish", false },
		};

		for (auto &step : kSteps) {
			if (step.desktopOnly && ATUIIsGamingMode())
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

			// Live preview: clicking a radio applies the mode immediately
			// so the wizard re-renders in the new style next frame.  The
			// mode is only persisted on close (Wiz_Finish).
			int sel = (int)ATUIGetMode();
			if (ImGui::RadioButton("Desktop Mode", sel == (int)ATUIMode::Desktop))
				Wiz_ApplyMode(ATUIMode::Desktop, window);
			ImGui::TextDisabled("  Menu bar, keyboard shortcuts, mouse-driven");
			ImGui::Spacing();

			if (ImGui::RadioButton("Gaming Mode", sel == (int)ATUIMode::Gaming))
				Wiz_ApplyMode(ATUIMode::Gaming, window);
			ImGui::TextDisabled("  Large buttons, gamepad/touch navigation");

			ImGui::Spacing();
			ImGui::Spacing();
			ImGui::TextWrapped(
				"You can switch between modes at any time from the View menu (Desktop) "
				"or the hamburger menu (Gaming)."
			);
			break;
		}

		case 2: { // Game Library (available in both modes)
			if (ATUIIsGamingMode()) {
				ImGui::TextWrapped(
					"Gaming Mode uses a Game Library as your home screen. Add folders "
					"containing your Atari game files (.atr, .xex, .car, .cas, etc.) "
					"to browse and play them.\n\n"
					"You can also add more folders later from Settings > Game Library."
				);
			} else {
				ImGui::TextWrapped(
					"Add folders containing your Atari game files (.atr, .xex, .car, "
					".cas, etc.) to populate the Game Library.\n\n"
					"The Game Library is optional in Desktop Mode — you can also boot "
					"individual files via File > Boot Image.... When populated, the "
					"library is browsable from View > Game Library and shared with "
					"Gaming Mode."
				);
			}
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
			if (ImGui::Button("Add Folder..."))
				Wiz_TriggerLibFolderPicker(window);

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

			// Helper: settings.ini lives in user config; flush each
			// edit to disk so a process kill (or Wiz_Finish path that
			// only persists the mode, not options) doesn't lose the
			// user's choice.  Matches the Gaming Mode pattern.
			auto flushOptions = []() {
				extern void ATRegistryFlushToDisk();
				try { ATRegistryFlushToDisk(); } catch (...) {}
			};

			static const char *themeLabels[] = { "Use system setting", "Light", "Dark" };
			int themeIdx = (int)g_ATOptions.mThemeMode;
			if (ImGui::Combo("Theme", &themeIdx, themeLabels, 3)) {
				ATOptions prev(g_ATOptions);
				g_ATOptions.mThemeMode = (ATUIThemeMode)themeIdx;
				if (g_ATOptions != prev) {
					g_ATOptions.mbDirty = true;
					ATOptionsRunUpdateCallbacks(&prev);
					ATOptionsSave();
					flushOptions();
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
					flushOptions();
					ATUIApplyTheme();
				}
			}

			ImGui::Spacing();

			// UI font size — shares state with the Configure System
			// "Fonts" page so the slider here is the same setting,
			// just exposed earlier in the first-run flow.  Changing
			// it sets the dirty flag and the atlas is rebuilt at the
			// next frame, giving instant live preview.
			int uiPt = ATUIFontsGetUISize();
			if (ImGui::SliderInt("UI font size (pt)", &uiPt, 8, 32, "%d pt"))
				ATUIFontsSetUISize(uiPt);
			ImGui::TextDisabled(
				"  Affects menus, dialogs, and all UI text.");

			ImGui::Spacing();
			ImGui::TextWrapped(
				"These settings can be changed later from Configure System > "
				"Emulator > UI (theme, opacity) and Configure System > "
				"Emulator > Fonts (font size)."
			);
			break;
		}

		case 6: { // Screen Effects — built-in CRT effects on/off
			ImGui::TextWrapped(
				"Choose whether to apply Altirra's built-in CRT-style "
				"screen effects (scanlines, bloom, distortion, mask, "
				"vignette) to the emulated display.  Changes take "
				"effect immediately so you can preview each option."
			);
			ImGui::Spacing();

			ImGui::TextWrapped(
				"You can fine-tune individual effects later from View > "
				"Adjust Screen Effects, and load external librashader "
				"presets from View > Screen Effects."
			);
			ImGui::Spacing();
			ImGui::Spacing();

			IDisplayBackend *be = ATUIGetDisplayBackend();
			if (be && be->HasShaderPreset())
				state.screenEffectsMode = ATUIState::kSFXMode_Preset;

			int sel = (state.screenEffectsMode == ATUIState::kSFXMode_None) ? 0 : 1;

			if (ImGui::RadioButton("None", sel == 0)) {
				ATUIShaderPresetsClear(be);
				state.screenEffectsMode = ATUIState::kSFXMode_None;
			}
			ImGui::TextDisabled(
				"  Raw emulator output, no post-processing.");
			ImGui::Spacing();

			if (ImGui::RadioButton("Basic Shaders", sel == 1)) {
				ATUIShaderPresetsClear(be);
				state.screenEffectsMode = ATUIState::kSFXMode_Basic;
			}
			ImGui::TextDisabled(
				"  Built-in scanlines, bloom, distortion, mask, vignette.");

			if (state.screenEffectsMode == ATUIState::kSFXMode_Preset) {
				ImGui::Spacing();
				ImGui::TextWrapped(
					"Note: a librashader preset is currently active. "
					"Choosing None or Basic Shaders here will unload it."
				);
			}
			break;
		}

		case 10: { // Firmware
			ImGui::TextWrapped(
				"Altirra ships with built-in replacements (\"AltirraOS\") "
				"for every standard ROM, so the emulator works fine without "
				"any original ROM image. If you do have original ROMs, "
				"loading them improves compatibility with a small number of "
				"timing-sensitive titles.\n\n"
				"If you don't have ROM images or don't want to set them up now, "
				"just click Next — the built-in firmware will be used."
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
						ImGui::TextUnformatted("Original ROM");
						ImGui::PopStyleColor();
					} else {
						// AltirraOS / built-in BASIC are bundled with the
						// emulator and are used automatically when the
						// real ROM isn't available.  Calling this "Not
						// found" misleads users into thinking emulation
						// won't work, so we explicitly label it.
						ImGui::TextDisabled("Built-in (Altirra)");
					}
				}
				ImGui::EndTable();
			}

			ImGui::Spacing();
			if (ImGui::Button("Scan for firmware..."))
				Wiz_TriggerFirmwareScan(window);

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
				"Select the type of system to emulate. This can be changed later "
				"from the System menu.\n\n"
				"Both options work without original ROMs — Altirra ships with "
				"its own built-in firmware (\"AltirraOS\") covering both XL/XE "
				"and the 5200."
			);
			ImGui::Spacing();

			bool is5200 = (sim.GetHardwareMode() == kATHardwareMode_5200);
			bool isComputer = !is5200;

			if (ImGui::RadioButton("Computer (XL/XE)", isComputer) && !isComputer) {
				uint32 profileId = ATGetDefaultProfileId(kATDefaultProfile_XL);
				ATSettingsSwitchProfile(profileId);
				g_setupWiz.needsHardwareReset = true;
			}
			if (ImGui::RadioButton("Atari 5200", is5200) && !is5200) {
				uint32 profileId = ATGetDefaultProfileId(kATDefaultProfile_5200);
				ATSettingsSwitchProfile(profileId);
				g_setupWiz.needsHardwareReset = true;
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

			if (ImGui::RadioButton("NTSC (60 Hz)", isNTSC) && !isNTSC) {
				ATSetVideoStandard(kATVideoStandard_NTSC);
				g_setupWiz.needsHardwareReset = true;
			}
			if (ImGui::RadioButton("PAL (50 Hz)", !isNTSC) && isNTSC) {
				ATSetVideoStandard(kATVideoStandard_PAL);
				g_setupWiz.needsHardwareReset = true;
			}
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
				g_setupWiz.needsHardwareReset = true;
			}
			if (ImGui::RadioButton("Convenient", !isAuthentic) && isAuthentic) {
				ATUISetDriveSoundsEnabled(false);
				sim.SetCassetteSIOPatchEnabled(true);
				sim.SetDiskSIOPatchEnabled(true);
				sim.SetDiskAccurateTimingEnabled(false);
				sim.GetGTIA().SetArtifactingMode(ATArtifactMode::None);
				ATUISetDisplayFilterMode(kATDisplayFilterMode_SharpBilinear);
				ATUISetViewFilterSharpness(+1);
				g_setupWiz.needsHardwareReset = true;
			}
			break;
		}

		case 35: { // Joystick (Port 1) — mirrors Input > Port 1
			ATInputManager *pIM = sim.GetInputManager();
			if (!pIM) {
				ImGui::TextDisabled("Input manager unavailable.");
				break;
			}

			const bool is5200 =
				(sim.GetHardwareMode() == kATHardwareMode_5200);
			const bool adaptive = ATAdaptiveInput::IsEnabled();

			// Adaptive checkbox up-front — flips between the
			// "everything just works" confirmation copy and the
			// power-user manual picker below.
			bool flag = adaptive;
			if (ImGui::Checkbox(
				"Adaptive input (recommended)", &flag))
			{
				ATAdaptiveInput::SetEnabled(flag);
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(?)");
			if (ImGui::IsItemHovered()) {
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::TextUnformatted(
					"On (default): keyboard arrows / numpad / any "
					"connected gamepad / on-screen joypad in Gaming "
					"Mode all drive port 1 simultaneously.\n\n"
					"Off: lock port 1 to a single source you pick "
					"below.");
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
			ImGui::Spacing();

			if (adaptive) {
				if (is5200) {
					ImGui::TextWrapped(
						"Controls are auto-configured.  Whatever "
						"input source you have connected — keyboard "
						"or gamepad — drives the 5200 controller on "
						"port 1.\n\n"
						"You can change this any time from the Input "
						"> Port 1 menu, and define your own mappings "
						"under Input > Input Mappings."
					);
				} else {
					ImGui::TextWrapped(
						"Controls are auto-configured.  Keyboard "
						"arrows, numpad, any connected gamepad, and "
						"the on-screen joypad in Gaming Mode all "
						"drive joystick port 1 at the same time.\n\n"
						"You can change this any time from the Input "
						"> Port 1 menu, and define your own mappings "
						"under Input > Input Mappings."
					);
				}
				break;
			}

			// Adaptive off: original single-map picker for users who
			// want exclusive lock-bind on one input source.
			if (is5200) {
				ImGui::TextWrapped(
					"Pick the input mapping for 5200 controller port 1. "
					"The keyboard-to-5200-Controller map is preselected as "
					"a sensible default.\n\n"
					"You can change this any time from the Input > Port 1 "
					"menu, and define your own mappings under Input > "
					"Input Mappings."
				);
			} else {
				ImGui::TextWrapped(
					"Pick the input mapping for joystick port 1. "
					"\"Arrow Keys -> Joystick (port 1)\" is preselected as a "
					"sensible default — most Atari games use port 1 and the "
					"keyboard arrow keys feel natural for movement.\n\n"
					"You can change this any time from the Input > Port 1 "
					"menu, and define your own mappings under Input > "
					"Input Mappings."
				);
			}
			ImGui::Spacing();

			std::vector<WizPortMapEntry> entries;
			Wiz_GatherPortMaps(*pIM, 0, entries);

			// Auto-seed Arrow Keys the first time the user lands
			// here in this wizard session.  joystickPageSeeded is
			// cleared by Reset() so each new session reseeds.
			if (!g_setupWiz.joystickPageSeeded) {
				Wiz_SeedDefaultPort1Map(*pIM, entries);
				g_setupWiz.joystickPageSeeded = true;
			}

			bool anyActive = false;
			for (auto &e : entries) if (e.active) { anyActive = true; break; }

			if (ImGui::RadioButton("None", !anyActive)) {
				Wiz_ActivatePortMap(*pIM, entries, nullptr);
			}

			for (size_t i = 0; i < entries.size(); ++i) {
				auto &e = entries[i];
				ImGui::PushID((int)i);
				if (ImGui::RadioButton(e.name.c_str(), e.active))
					Wiz_ActivatePortMap(*pIM, entries, e.map);
				ImGui::PopID();
			}
			break;
		}

		case 40: // Finish (computer)
			if (ATUIIsGamingMode()) {
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
			if (ATUIIsGamingMode()) {
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
	int prevPage = Wiz_GetPrevPage(g_setupWiz.page);
	int nextPage = Wiz_GetNextPage(g_setupWiz.page);
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
			Wiz_Finish(sim, state, window);
			ImGui::End();
			return;
		}
	}

	ImGui::SameLine();
	if (ImGui::Button("Close")) {
		Wiz_Finish(sim, state, window);
		ImGui::End();
		return;
	}

	ImGui::End();
}

