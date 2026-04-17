//	AltirraSDL - Mobile UI (split from ui_mobile.cpp Phase 3b)
//	Verbatim move; helpers/state shared via mobile_internal.h.

#include <stdafx.h>
#include <ctime>
#include <cwctype>
#include <vector>
#include <algorithm>
#include <functional>
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

#include "mobile_internal.h"
#include "../gamelibrary/game_library.h"
#include "settings.h"
#include "options.h"

#ifndef ALTIRRA_NO_SDL3_IMAGE
#include <SDL3_image/SDL_image.h>
#endif
#include <at/atcore/md5.h>

extern ATSimulator g_sim;
extern VDStringA ATGetConfigDir();
extern void ATRegistryFlushToDisk();
extern IDisplayBackend *ATUIGetDisplayBackend();

// Write-through helper for mobile setting pages that mutate simulator
// or UI state (HardwareMode, VideoStandard, MemoryMode, BASIC, SIO,
// randomization, display filter mode, ...).  These categories are
// otherwise only serialised by the clean-exit ATSaveSettings call,
// which the OS rarely lets a backgrounded mobile app reach — so each
// edit must persist immediately.
static void ATPersistMobileEdit(uint32 categoryMask) {
	try {
		ATSaveSettings((ATSettingsCategory)categoryMask);
	} catch (...) {
		// Best effort; lifecycle handler will retry at suspend.
	}
	try {
		ATRegistryFlushToDisk();
	} catch (...) {
	}
}

void RenderSettings(ATSimulator &sim, ATUIState &uiState,
	ATMobileUIState &mobileState, SDL_Window *window)
{
	ImGuiIO &io = ImGui::GetIO();

	// Full-screen palette-aware background — matches mobile_about /
	// mobile_disk so Gaming Mode screens share one window tint.  Using
	// NoBackground here and painting pal.windowBg manually lets the
	// palette stay the single source of truth for the window colour
	// across themes.
	{
		const ATMobilePalette &bgPal = ATMobileGetPalette();
		ImGui::GetBackgroundDrawList()->AddRectFilled(
			ImVec2(0, 0), io.DisplaySize, bgPal.windowBg);
	}

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

	if (ImGui::Begin("##MobileSettings", nullptr, flags)) {
		// ESC / B-button / Backspace navigates back, same as "<" arrow.
		// Skip when a modal dialog is on top (see mobile_hamburger.cpp).
		if (!s_confirmActive && !s_infoModalOpen) {
			bool back = ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false);
			if (!ImGui::IsAnyItemActive()) {
				back = back
					|| ImGui::IsKeyPressed(ImGuiKey_Escape, false)
					|| ImGui::IsKeyPressed(ImGuiKey_Backspace, false);
			}
			if (back) {
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
		}

		// Header — back arrow, title reflects current sub-page.
		float headerH = dp(48.0f);
		if (ATTouchButton("<", ImVec2(dp(48.0f), headerH),
			ATTouchButtonStyle::Subtle))
		{
			if (s_settingsPage == ATMobileSettingsPage::Firmware
				&& s_fwPicker != kATFirmwareType_Unknown)
			{
				s_fwPicker = kATFirmwareType_Unknown;
			} else if (s_settingsPage == ATMobileSettingsPage::Home) {
				mobileState.currentScreen = s_settingsReturnScreen;
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
		case ATMobileSettingsPage::GameLibrary: pageTitle = "Game Library"; break;
		}
		ImGui::Text("%s", pageTitle);

		ImGui::Separator();
		ImGui::Spacing();

		// NavFlattened lets gamepad nav cross into the list without an
		// explicit "enter child" press — see mobile_hamburger.cpp.
		ImGui::BeginChild("SettingsScroll", ImVec2(0, 0),
			ImGuiChildFlags_NavFlattened);
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
			CatRow cats[8];
			int n = 0;

			cats[n++] = { "Machine",
				VDStringA().sprintf("%s  \xC2\xB7  %s",
					hwLabel(), vsLabel),
				ATMobileSettingsPage::Machine };

			{
				const char *scaleLabel =
					mobileState.interfaceScale == 0 ? "Small" :
					mobileState.interfaceScale == 2 ? "Large" : "Standard";
				cats[n++] = { "Display",
					VDStringA().sprintf("Size: %s  \xC2\xB7  Filter, effects", scaleLabel),
					ATMobileSettingsPage::Display };
			}

			cats[n++] = { "Performance",
				VDStringA().sprintf("Preset: %s", presetLabel),
				ATMobileSettingsPage::Performance };

			cats[n++] = { "Controls",
				mobileState.showTouchControls
					? VDStringA().sprintf("Touch: on  \xC2\xB7  Menu: %s  \xC2\xB7  Size: %s",
						mobileState.showHamburgerMenu ? "on" : "off",
						mobileState.layoutConfig.controlSize == ATTouchControlSize::Small  ? "Small"  :
						mobileState.layoutConfig.controlSize == ATTouchControlSize::Large  ? "Large"  : "Medium")
					: VDStringA().sprintf("Touch: off  \xC2\xB7  Menu: %s",
						mobileState.showHamburgerMenu ? "on" : "off"),
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

			{
				ATGameLibrary *lib = GetGameLibrary();
				int gameCount = lib ? (int)lib->GetEntryCount() : 0;
				int sourceCount = lib ? (int)lib->GetSources().size() : 0;
				cats[n++] = { "Game Library",
					sourceCount > 0
						? VDStringA().sprintf("%d games  \xC2\xB7  %d source%s",
							gameCount, sourceCount,
							sourceCount == 1 ? "" : "s")
						: VDStringA("(no sources)"),
					ATMobileSettingsPage::GameLibrary };
			}

			float rowH = dp(76.0f);
			float rowGap = dp(10.0f);
			const ATMobilePalette &pal = ATMobileGetPalette();
			for (int i = 0; i < n; ++i) {
				ImGui::PushID(i);
				ImVec2 cursor = ImGui::GetCursorScreenPos();
				float availW = ImGui::GetContentRegionAvail().x;
				ImDrawList *dl = ImGui::GetWindowDrawList();

				ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0, 0, 0, 0));
				ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
				ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0, 0, 0, 0));
				if (ImGui::Selectable("##cat",  false,
					ImGuiSelectableFlags_None,
					ImVec2(availW, rowH)))
				{
					s_settingsPage = cats[i].target;
				}
				ImGui::PopStyleColor(3);

				bool itemHovered = ImGui::IsItemHovered();
				bool itemFocused = ImGui::IsItemFocused();
				uint32 bgTop    = (itemHovered || itemFocused)
					? pal.cardBgHoverTop : pal.cardBgTop;
				uint32 bgBottom = (itemHovered || itemFocused)
					? pal.cardBgHover   : pal.cardBg;

				ImVec2 cardTL = cursor;
				ImVec2 cardBR(cursor.x + availW, cursor.y + rowH);
				// Gradient card — uses the shared helper so the
				// rounded corners stay solid bottomCol (no corner
				// bleed).  Hairline border on top for definition.
				ATMobileDrawGradientRect(cardTL, cardBR,
					bgTop, bgBottom, dp(10.0f));
				dl->AddRect(cardTL, cardBR, pal.cardBorder,
					dp(10.0f), 0, 1.0f);
				if (itemFocused) {
					dl->AddRect(cardTL, cardBR, pal.rowFocus,
						dp(10.0f), 0, dp(2.0f));
				}

				ImVec2 tcur(cursor.x + dp(16.0f), cursor.y + dp(12.0f));
				dl->AddText(tcur, pal.text, cats[i].title);
				ImVec2 scur(cursor.x + dp(16.0f), cursor.y + dp(44.0f));
				dl->AddText(scur, pal.textMuted,
					cats[i].subtitle.c_str());

				// Right-side chevron
				ImVec2 chev(cursor.x + availW - dp(28.0f),
					cursor.y + rowH * 0.5f - dp(8.0f));
				dl->AddText(chev, pal.textMuted, ">");

				ImGui::Dummy(ImVec2(0, rowGap));
				ImGui::PopID();
			}

			ImGui::Dummy(ImVec2(0, dp(16.0f)));
			if (ATTouchButton("About", ImVec2(-1, dp(56.0f)))) {
				mobileState.currentScreen = ATMobileUIScreen::About;
			}

			ImGui::Dummy(ImVec2(0, dp(32.0f)));
			ATTouchEndDragScroll();
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
				// Hardware mode change can also reset memory mode in
				// the simulator (e.g. switching to 5200 forces 16K).
				// Save Hardware so all coupled defaults stick.
				ATPersistMobileEdit(kATSettingsCategory_Hardware);
			}
		}

		// Video Standard — PAL / NTSC
		{
			int current = (sim.GetVideoStandard() == kATVideoStandard_PAL) ? 0 : 1;
			static const char *items[] = { "PAL", "NTSC" };
			if (ATTouchSegmented("Video Standard", &current, items, 2)) {
				sim.SetVideoStandard(current == 0 ? kATVideoStandard_PAL : kATVideoStandard_NTSC);
				ATPersistMobileEdit(kATSettingsCategory_Hardware);
			}
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
			if (ATTouchSegmented("Memory Size", &curIdx, labels, count)) {
				sim.SetMemoryMode(kMemModes[curIdx].mode);
				ATPersistMobileEdit(kATSettingsCategory_Hardware);
			}
		}

		// BASIC toggle
		{
			bool basicEnabled = sim.IsBASICEnabled();
			if (ATTouchToggle("BASIC Enabled", &basicEnabled)) {
				sim.SetBASICEnabled(basicEnabled);
				ATPersistMobileEdit(kATSettingsCategory_StartupConfig);
			}
		}

		// SIO Patch toggle
		{
			bool sioEnabled = sim.IsSIOPatchEnabled();
			if (ATTouchToggle("SIO Patch", &sioEnabled)) {
				sim.SetSIOPatchEnabled(sioEnabled);
				ATPersistMobileEdit(kATSettingsCategory_Acceleration);
			}
		}

		// ---- RANDOMIZATION (still on Machine page) ----
		ATTouchSection("Randomization");

		{
			bool randomLaunch = sim.IsRandomProgramLaunchDelayEnabled();
			if (ATTouchToggle("Randomize launch delay", &randomLaunch)) {
				sim.SetRandomProgramLaunchDelayEnabled(randomLaunch);
				ATPersistMobileEdit(kATSettingsCategory_Boot);
			}
		}
		ATTouchMutedText(
			"Delays program boot by a random number of cycles so "
			"POKEY's RNG seed varies between runs.  Default: on.");

		{
			bool randomFill = sim.IsRandomFillEXEEnabled();
			if (ATTouchToggle("Randomize memory on EXE load", &randomFill)) {
				sim.SetRandomFillEXEEnabled(randomFill);
				ATPersistMobileEdit(kATSettingsCategory_Boot);
			}
		}
		ATTouchMutedText(
			"Fills uninitialised RAM with random bytes before a .xex "
			"program loads.  Helps flush out games that relied on "
			"specific power-on RAM patterns.  Default: off.");
		} // end Machine page

		// --- Sub-page: Controls ---
		if (s_settingsPage == ATMobileSettingsPage::Controls) {
		ATTouchSection("Controls");

		// Show on-screen touch controls (joystick, fire, console keys)
		if (ATTouchToggle("Show Touch Controls", &mobileState.showTouchControls)) {
			SaveMobileConfig(mobileState);
		}

		// Show hamburger menu button — independent of the gameplay
		// touch controls so the user can leave the menu visible while
		// watching a demo with a gamepad or just the keyboard.
		if (ATTouchToggle("Show Menu Button", &mobileState.showHamburgerMenu)) {
			SaveMobileConfig(mobileState);
		}

		// Dependent controls — only meaningful when touch controls are visible
		if (!mobileState.showTouchControls)
			ImGui::BeginDisabled();

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

		if (!mobileState.showTouchControls)
			ImGui::EndDisabled();
		} // end Controls page

		// --- Sub-page: Save State ---
		if (s_settingsPage == ATMobileSettingsPage::SaveState) {
		ATTouchSection("Save State");

		if (ATTouchToggle("Auto-save on exit / background",
			&mobileState.autoSaveOnSuspend))
		{
			SaveMobileConfig(mobileState);
		}
		ATTouchMutedText(
			"Snapshots the emulator whenever the app goes to "
			"background or is closed, so a swipe-away or an "
			"incoming call never loses progress.");

		if (ATTouchToggle("Restore on startup",
			&mobileState.autoRestoreOnStart))
		{
			SaveMobileConfig(mobileState);
		}
		ATTouchMutedText(
			"On launch, resume exactly where you left off "
			"(requires Auto-save above).");

		ImGui::Spacing();

		// Manual save / load buttons — always available so the user
		// can checkpoint a run independently of the auto-save setting.
		float halfW = (ImGui::GetContentRegionAvail().x - dp(8.0f)) * 0.5f;
		if (ATTouchButton("Save State Now", ImVec2(halfW, dp(56.0f)),
			ATTouchButtonStyle::Accent))
		{
			try {
				VDStringW path = QuickSaveStatePath();
				sim.SaveState(path.c_str());
				ShowInfoModal("Saved", "Emulator state saved.");
			} catch (const MyError &e) {
				ShowInfoModal("Save Failed", e.c_str());
			}
		}
		ImGui::SameLine();
		if (ATTouchButton("Load State Now", ImVec2(halfW, dp(56.0f)))) {
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
				ATTouchMutedText(
					"Bloom and CRT distortion need the OpenGL display "
					"backend.  The SDL_Renderer fallback (currently "
					"active on this device) will accept the toggles "
					"but silently ignore those two — scanlines still "
					"work either way.");
				ImGui::Spacing();
			}
		}

		// Manually toggling any visual effect moves the performance
		// preset to Custom so the user can see they've left the
		// bundle.
		auto markCustom = [&](){ mobileState.performancePreset = 3; };

		// PAL/NTSC Artifacting — shares state with the desktop UI's
		// Configure System > Outputs > Artifacting combo.  ON sets
		// AutoHi (matches the Setup Wizard "Authentic" path at
		// ui_tools_setup_wizard.cpp:616); OFF sets None.  Persisted by
		// settings.cpp under "GTIA: Artifacting mode"
		// (kATSettingsCategory_View).
		{
			ATGTIAEmulator &gtia = sim.GetGTIA();
			bool artifactingOn = (gtia.GetArtifactingMode() != ATArtifactMode::None);
			if (ATTouchToggle("PAL/NTSC Artifacting", &artifactingOn)) {
				gtia.SetArtifactingMode(artifactingOn
					? ATArtifactMode::AutoHi : ATArtifactMode::None);
				ATPersistMobileEdit(kATSettingsCategory_View);
			}
		}

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

		if (ATTouchToggle("Aperture Grille", &mobileState.fxApertureGrille)) {
			markCustom();
			SaveMobileConfig(mobileState);
			try { ATMobileUI_ApplyVisualEffects(mobileState); } catch (...) {}
		}

		if (ATTouchToggle("Vignette", &mobileState.fxVignette)) {
			markCustom();
			SaveMobileConfig(mobileState);
			try { ATMobileUI_ApplyVisualEffects(mobileState); } catch (...) {}
		}

		ATTouchSection("Interface");

		// Interface scale — lets the user shrink the chrome on
		// small-screen landscape where headers + shortcut bar
		// consume most of the display, or enlarge for accessibility.
		{
			int sc = mobileState.interfaceScale;
			static const char *sizes[] = { "Small", "Standard", "Large" };
			if (ATTouchSegmented("Interface Size", &sc, sizes, 3)) {
				mobileState.interfaceScale = sc;
				SaveMobileConfig(mobileState);
			}
		}

		// Theme selector.  Mirrors the Appearance page in the Desktop
		// Settings dialog (see ui_system_pages_b.cpp / Theme combo).
		// The switch takes effect immediately via ATUIApplyTheme(); we
		// then flush Settings to the in-memory registry AND to disk
		// synchronously so the choice survives an OS-side process kill,
		// swipe-away, or crash — no dependency on clean-exit code.
		{
			int th = (int)g_ATOptions.mThemeMode;
			static const char *themes[] = { "System", "Light", "Dark" };
			if (ATTouchSegmented("Theme", &th, themes, 3)) {
				ATOptions prev(g_ATOptions);
				g_ATOptions.mThemeMode = (ATUIThemeMode)th;
				if (g_ATOptions != prev) {
					g_ATOptions.mbDirty = true;
					ATOptionsRunUpdateCallbacks(&prev);
					ATOptionsSave();                 // write in-memory registry
					try { ATRegistryFlushToDisk(); } // flush to settings.ini
					catch (...) {}
					ATUIApplyTheme();                // live restyle
				}
			}
			ATTouchMutedText(
				"System follows your desktop's dark/light preference.  "
				"Light and Dark override it.  Changes are saved "
				"immediately.");
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
				// Filter mode lives in the View settings category, not
				// in mobile-only registry — persist that too so the
				// choice survives a process kill.
				ATPersistMobileEdit(kATSettingsCategory_View);
			}
		}
		} // end Display page

		// --- Sub-page: Performance (bundled preset) ---
		if (s_settingsPage == ATMobileSettingsPage::Performance) {
		ATTouchSection("Performance Preset");

		ATTouchMutedText(
			"Choose a preset that bundles visual effects and the "
			"display filter for a consistent trade-off.  Pick "
			"Efficient on older devices, Quality on flagships.");
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
				const ATMobilePalette &warnPal = ATMobileGetPalette();
				ImGui::PushStyleColor(ImGuiCol_Text,
					ATMobileCol(warnPal.warning));
				ImGui::TextUnformatted(
					"Preset: Custom (you've manually changed a visual "
					"setting — pick a preset above to revert).");
				ImGui::PopStyleColor();
			}
		}
		} // end Performance page

		// --- Sub-page: Firmware (extracted to mobile_settings_firmware.cpp) ---
		if (s_settingsPage == ATMobileSettingsPage::Firmware) {
			RenderSettingsPage_Firmware(mobileState);
		}

		// --- Sub-page: Game Library ---
		if (s_settingsPage == ATMobileSettingsPage::GameLibrary) {
			ATGameLibrary *lib = GetGameLibrary();
			if (!lib) {
				GameBrowser_Init();
				lib = GetGameLibrary();
			}

			ATTouchSection("Game Folders");

			if (lib) {
				auto sources = lib->GetSources();
				for (int i = 0; i < (int)sources.size(); ++i) {
					if (sources[i].mbIsArchive || sources[i].mbIsFile)
						continue;

					ImGui::PushID(i);
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
						extern void GameBrowser_Invalidate();
						GameBrowser_Invalidate();
						extern void ATRegistryFlushToDisk();
						ATRegistryFlushToDisk();
						ImGui::PopID();
						break;
					}

					ImGui::SetCursorPosY(cursor.y - ImGui::GetWindowPos().y
						+ ImGui::GetScrollY() + rowH);
					ImGui::PopID();
				}
			}

			if (ATTouchButton("+ Add Folder", ImVec2(-1, dp(44.0f)),
				ATTouchButtonStyle::Accent))
			{
				// Drop any sticky zip-browsing state so the folder
				// picker opens on the real filesystem, and make sure
				// no other picker mode is still armed.
				s_zipArchivePath.clear();
				s_zipInternalDir.clear();
				s_archiveFilePickerMode = false;
				s_archiveFilePickerCallback = nullptr;
				s_folderPickerMode = true;
				s_folderPickerReturnScreen = ATMobileUIScreen::Settings;
				s_folderPickerCallback = [](const VDStringW &path) {
					ATGameLibrary *lib = GetGameLibrary();
					if (!lib) return;
					auto sources = lib->GetSources();
					GameSource src;
					src.mPath = path;
					src.mbIsArchive = false;
					sources.push_back(std::move(src));
					lib->SetSources(std::move(sources));
					lib->SaveSettingsToRegistry();
					lib->StartScan();
					extern void ATRegistryFlushToDisk();
					ATRegistryFlushToDisk();
				};
				s_settingsPage = ATMobileSettingsPage::GameLibrary;
				s_fileBrowserNeedsRefresh = true;
				mobileState.currentScreen = ATMobileUIScreen::FileBrowser;
			}

			ImGui::Spacing();
			ATTouchSection("Game Archives");

			if (lib) {
				auto sources = lib->GetSources();
				for (int i = 0; i < (int)sources.size(); ++i) {
					if (!sources[i].mbIsArchive)
						continue;

					ImGui::PushID(1000 + i);
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
						extern void GameBrowser_Invalidate();
						GameBrowser_Invalidate();
						extern void ATRegistryFlushToDisk();
						ATRegistryFlushToDisk();
						ImGui::PopID();
						break;
					}

					ImGui::SetCursorPosY(cursor.y - ImGui::GetWindowPos().y
						+ ImGui::GetScrollY() + rowH);
					ImGui::PopID();
				}
			}

			if (ATTouchButton("+ Add Archive (ZIP)",
				ImVec2(-1, dp(44.0f)), ATTouchButtonStyle::Accent))
			{
				// Archive-file-picker mode — the user selects a single
				// archive file (.zip/.atz/.gz/.arc).  Tapping the archive
				// does NOT enter it; the callback receives the ZIP's path.
				// Clear any leftover zip-browsing state so the picker
				// opens on the real filesystem, not a zip the user had
				// drilled into during a prior Load Game session, and
				// make sure no other picker mode is still armed.
				s_zipArchivePath.clear();
				s_zipInternalDir.clear();
				s_folderPickerMode = false;
				s_folderPickerCallback = nullptr;
				s_archiveFilePickerMode = true;
				s_archiveFilePickerReturnScreen =
					ATMobileUIScreen::Settings;
				s_archiveFilePickerCallback =
					[](const VDStringW &archivePath)
				{
					ATGameLibrary *lib = GetGameLibrary();
					if (!lib) return;

					// Skip if this archive is already in the list.
					auto sources = lib->GetSources();
					for (const auto &s : sources) {
						if (s.mbIsArchive && s.mPath == archivePath)
							return;
					}

					GameSource src;
					src.mPath = archivePath;
					src.mbIsArchive = true;
					sources.push_back(std::move(src));
					lib->SetSources(std::move(sources));
					lib->SaveSettingsToRegistry();
					lib->StartScan();
					extern void ATRegistryFlushToDisk();
					ATRegistryFlushToDisk();
				};
				s_settingsPage = ATMobileSettingsPage::GameLibrary;
				s_fileBrowserNeedsRefresh = true;
				mobileState.currentScreen = ATMobileUIScreen::FileBrowser;
			}

			ImGui::Spacing();
			ATTouchSection("Game Files");

			if (lib) {
				auto sources = lib->GetSources();
				for (int i = 0; i < (int)sources.size(); ++i) {
					if (!sources[i].mbIsFile)
						continue;

					ImGui::PushID(2000 + i);
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
						extern void GameBrowser_Invalidate();
						GameBrowser_Invalidate();
						extern void ATRegistryFlushToDisk();
						ATRegistryFlushToDisk();
						ImGui::PopID();
						break;
					}

					ImGui::SetCursorPosY(cursor.y - ImGui::GetWindowPos().y
						+ ImGui::GetScrollY() + rowH);
					ImGui::PopID();
				}
			}

			ImGui::Spacing();
			ImGui::Spacing();
			ATTouchSection("Options");

			if (lib) {
				GameLibrarySettings settings = lib->GetSettings();
				bool changed = false;

				if (ATTouchToggle("Scan subfolders recursively",
					&settings.mbRecursive))
					changed = true;

				if (ATTouchToggle("Match game-art from other folders",
					&settings.mbCrossFolderArt))
					changed = true;

				if (ATTouchToggle("Add booted games to library",
					&settings.mbAddBootedToLibrary))
					changed = true;

				{
					static const char *sizes[] = { "Small", "Medium", "Large" };
					if (ATTouchSegmented("Grid tile size",
						&settings.mGridSize, sizes, 3))
						changed = true;
				}

				{
					static const char *sizes[] = { "Compact", "Medium", "Large" };
					if (ATTouchSegmented("List row size",
						&settings.mListSize, sizes, 3))
						changed = true;
				}

				if (changed) {
					lib->SetSettings(settings);
					lib->SaveSettingsToRegistry();
					extern void ATRegistryFlushToDisk();
					ATRegistryFlushToDisk();
				}
			}

			ImGui::Spacing();
			ImGui::Spacing();
			ATTouchSection("Library");

			if (lib) {
				ImGui::Text("Games found: %d",
					(int)lib->GetEntryCount());

				if (lib->GetLastScanTime() > 0) {
					uint64_t ago = (uint64_t)std::time(nullptr)
						- lib->GetLastScanTime();
					if (ago < 60)
						ImGui::Text("Last scan: just now");
					else if (ago < 3600)
						ImGui::Text("Last scan: %d min ago",
							(int)(ago / 60));
					else
						ImGui::Text("Last scan: %d hours ago",
							(int)(ago / 3600));
				}

				ImGui::Spacing();

				if (lib->IsScanning()) {
					ImGui::BeginDisabled();
					ATTouchButton("Scanning...",
						ImVec2(-1, dp(44.0f)));
					ImGui::EndDisabled();
				} else {
					if (ATTouchButton("Rescan Now",
						ImVec2(-1, dp(44.0f)),
						ATTouchButtonStyle::Accent))
					{
						lib->StartScan();
					}
				}

				ImGui::Spacing();
				{
					bool canSet = mobileState.gameLoaded
						&& GameBrowser_HasCurrentGame();
					if (!canSet)
						ImGui::BeginDisabled();
					if (ATTouchButton("Save Screenshot as Game Art",
						ImVec2(-1, dp(44.0f))))
					{
						VDStringA err = GameBrowser_SetCurrentFrameAsArt();
						if (!err.empty())
							ShowInfoModal("Save Game Art Failed",
								err.c_str());
						else
							ShowInfoModal("Game Art Saved",
								"The current screenshot is now the "
								"cover art for this game.");
					}
					if (!canSet)
						ImGui::EndDisabled();
				}

				ImGui::Spacing();
				if (ATTouchButton("Clear Play History",
					ImVec2(-1, dp(44.0f))))
				{
					lib->ClearHistory();
					GameBrowser_Invalidate();
				}

				ImGui::Spacing();
				// Destructive action — danger variant so it clearly
				// reads as different from the other neutral buttons
				// on the page.
				if (ATTouchButton("Clear Entire Library",
					ImVec2(-1, dp(44.0f)),
					ATTouchButtonStyle::Danger))
				{
					ShowConfirmDialog(
						"Clear Library",
						"Remove all game sources and cached data?  "
						"This does not delete your game files.",
						[&mobileState]() {
							ATGameLibrary *lib = GetGameLibrary();
							if (!lib) return;
							lib->SetSources({});
							lib->GetEntries().clear();
							lib->SaveSettingsToRegistry();
							lib->SaveCache();
							extern void GameBrowser_Invalidate();
							GameBrowser_Invalidate();
							extern void ATRegistryFlushToDisk();
							ATRegistryFlushToDisk();
						});
				}
			}
		}

		// Bottom padding so the last row isn't flush against the nav bar
		ImGui::Dummy(ImVec2(0, dp(32.0f)));

		ATTouchEndDragScroll();
		ImGui::EndChild();
	}
	ImGui::End();
}
