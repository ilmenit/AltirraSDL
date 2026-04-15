//	AltirraSDL - Mobile UI (split from ui_mobile.cpp Phase 3b)
//	Verbatim move; helpers/state shared via mobile_internal.h.

#include <stdafx.h>
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
#include "ui_mode.h"

extern ATSimulator g_sim;
extern VDStringA ATGetConfigDir();
extern void ATRegistryFlushToDisk();
extern IDisplayBackend *ATUIGetDisplayBackend();

void RenderHamburgerMenu(ATSimulator &sim, ATUIState &uiState,
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

	// ESC / B-button / Backspace closes the hamburger menu, matching
	// the BACK gamepad button and the "<" header arrow.
	// Skip when a modal dialog is showing on top — ESC should dismiss
	// the modal first (handled by mobile_dialogs.cpp), not the menu.
	bool closeFromBack = false;
	if (!s_confirmActive && !s_infoModalOpen) {
		bool back = ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false);
		if (!ImGui::IsAnyItemActive()) {
			back = back
				|| ImGui::IsKeyPressed(ImGuiKey_Escape, false)
				|| ImGui::IsKeyPressed(ImGuiKey_Backspace, false);
		}
		if (back) closeFromBack = true;
	}

	bool closeFromHeader = false;
	if (ImGui::Begin("##MobileMenu", nullptr, flags)) {
		// Material-style app-bar header: a full-width 56dp row with a
		// large back-arrow on the left and the app title next to it.
		// The whole row is the close affordance — much bigger than
		// the previous 32dp "X" in the corner and consistent with the
		// back arrow used by every sub-screen (file browser, disk
		// manager, settings, about).
		const float headerH = dp(56.0f);
		const float backW   = dp(56.0f);
		const float headerTopY = ImGui::GetCursorPosY();
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
		ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.08f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1, 1, 1, 0.16f));
		// Use ASCII "<" to match the back-arrow on every other mobile
		// sub-screen (disk manager, file browser, settings).  The
		// loaded UI font only carries Latin / Cyrillic / Greek glyph
		// ranges, so a Unicode arrow (U+2190) would render as tofu.
		if (ImGui::Button("<", ImVec2(backW, headerH)))
			closeFromHeader = true;
		ImGui::PopStyleColor(3);
		ImGui::PopStyleVar();
		ImGui::SameLine();
		// Vertically centre the title text inside the 56dp header row.
		ImGui::SetCursorPosY(headerTopY
			+ (headerH - ImGui::GetTextLineHeight()) * 0.5f);
		ImGui::Text("Altirra");
		ImGui::SetCursorPosY(headerTopY + headerH);
		ImGui::Separator();
		ImGui::Spacing();

		// Install touch-drag scrolling for the menu items area so a
		// short phone or landscape orientation can still reach every
		// option.  Scoped to a child window so the header bar stays
		// pinned at the top.
		// NavFlattened so that gamepad/keyboard nav crosses the parent
		// → child border without an explicit "enter the list" press.
		// Without it the user has to press A on the child window itself
		// to focus its first item; with it, D-pad down from the back
		// arrow lands directly on Resume.
		ImGui::BeginChild("##MobileMenuItems", ImVec2(0, 0),
			ImGuiChildFlags_NavFlattened);
		ATTouchDragScroll();

		// Menu button height scaled for touch
		float btnH = dp(56.0f);
		ImVec2 btnSize(-1, btnH);

		// Resume — also serves as the default gamepad focus target so
		// a controller user can press A immediately to dismiss the
		// menu without D-padding first.
		if (ImGui::Button("Resume", btnSize))
			ATMobileUI_CloseMenu(sim, mobileState);
		ImGui::SetItemDefaultFocus();
		ImGui::Spacing();

		// Game Library — returns to the library home screen
		if (ImGui::Button("Game Library", btnSize)) {
			sim.Pause();
			mobileState.currentScreen = ATMobileUIScreen::GameBrowser;
		}
		ImGui::Spacing();

		// Save Screenshot as Game Art — quick-action shortcut, shown
		// only when the booted game is in the library and has no cover
		// art yet.  Same label and behaviour as the "Save Screenshot
		// as Game Art" button on the Game Library settings page, which
		// is the authoritative path for replacing art that's already
		// set.
		//
		// We deliberately do NOT auto-close the menu on success — the
		// user opened the hamburger on purpose (which paused the game)
		// and resuming behind their back breaks that intent.  Since
		// GameBrowser_CurrentEntryNeedsArt() re-evaluates every frame,
		// the button disappears from the menu on the next render once
		// the entry has art, and the user can dismiss the info modal
		// and tap Resume (or back) when they're ready.
		if (mobileState.gameLoaded
			&& GameBrowser_CurrentEntryNeedsArt())
		{
			if (ImGui::Button("Save Screenshot as Game Art", btnSize)) {
				VDStringA err = GameBrowser_SetCurrentFrameAsArt();
				if (!err.empty())
					ShowInfoModal("Save Game Art Failed", err.c_str());
				else
					ShowInfoModal("Game Art Saved",
						"The current screenshot is now the cover "
						"art for this game.");
			}
			ImGui::Spacing();
		}

		// Disk Drives — mobile-friendly full-screen manager
		if (ImGui::Button("Disk Drives", btnSize)) {
			mobileState.currentScreen = ATMobileUIScreen::DiskManager;
		}
		ImGui::Spacing();

		// Virtual Keyboard toggle
		{
			const char *kbdLabel = uiState.showVirtualKeyboard ? "Keyboard: ON" : "Keyboard: OFF";
			if (ImGui::Button(kbdLabel, btnSize)) {
				uiState.showVirtualKeyboard = !uiState.showVirtualKeyboard;
				ATMobileUI_CloseMenu(sim, mobileState);
				sim.Resume();
			}
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
			s_settingsReturnScreen = ATMobileUIScreen::HamburgerMenu;
			mobileState.currentScreen = ATMobileUIScreen::Settings;
		}
		ImGui::Spacing();

		ImGui::Separator();
		ImGui::Spacing();

		// About — mobile-friendly full-screen panel
		if (ImGui::Button("About", btnSize)) {
			mobileState.currentScreen = ATMobileUIScreen::About;
		}
		ImGui::Spacing();

#ifndef __ANDROID__
		ImGui::Separator();
		ImGui::Spacing();

		if (ImGui::Button("Switch to Desktop Mode", btnSize)) {
			ATUISetMode(ATUIMode::Desktop);
			ATUISaveMode();
			ATMobileUI_CloseMenu(sim, mobileState);
			float cs = SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(window));
			if (cs < 1.0f) cs = 1.0f;
			if (cs > 4.0f) cs = 4.0f;
			ATUIApplyModeStyle(cs);
		}
#endif

		ATTouchEndDragScroll();
		ImGui::EndChild();
	}
	ImGui::End();

	// Defer the header close-action until after End() so the click
	// animation on the back button completes for this frame and the
	// ImGui style stack is guaranteed balanced regardless of any
	// future edits to the header section.
	if (closeFromHeader || closeFromBack) {
		ATMobileUI_CloseMenu(sim, mobileState);
		return;
	}

	// Tap outside menu panel to close
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		ImVec2 mousePos = ImGui::GetMousePos();
		if (mousePos.x < io.DisplaySize.x - insetR - menuW)
			ATMobileUI_CloseMenu(sim, mobileState);
	}
}
