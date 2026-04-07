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

extern ATSimulator g_sim;
extern VDStringA ATGetConfigDir();
extern void ATRegistryFlushToDisk();
extern IDisplayBackend *ATUIGetDisplayBackend();

void RenderMobileAbout(ATSimulator &sim, ATUIState &uiState,
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

		ATTouchEndDragScroll();
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

void RenderFirstRunWizard(ATSimulator &sim, ATUIState &uiState,
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

#ifdef __ANDROID__
		static bool s_permGateDismissed = false;
		// Step 1 (Android only): storage permission gate.  Without
		// MANAGE_EXTERNAL_STORAGE the file browser cannot see ROMs in
		// /sdcard/Download and other user folders, so the very first
		// thing the user sees after install (the "Select ROM Folder"
		// browser) would appear empty.  Show a dedicated permission
		// step before the ROM-folder step with a button that jumps
		// straight to the right Settings page; if they continue
		// without it, the in-browser banner gives them a second
		// chance to grant access.
		if (!s_permGateDismissed && !ATAndroid_HasStoragePermission()) {
			float contentH = dp(420.0f);
			float topPad = (h - contentH) * 0.5f;
			if (topPad < dp(40.0f)) topPad = dp(40.0f);
			ImGui::Dummy(ImVec2(0, topPad));

			{
				const char *title = "Storage Access";
				ImGui::SetWindowFontScale(2.0f);
				float tw = ImGui::CalcTextSize(title).x;
				ImGui::SetCursorPosX((w - tw) * 0.5f);
				ImGui::TextColored(ImVec4(1, 1, 1, 1), "%s", title);
				ImGui::SetWindowFontScale(1.0f);
			}

			ImGui::Spacing();

			{
				const char *sub = "Step 1 of 2";
				ImVec2 ts = ImGui::CalcTextSize(sub);
				ImGui::SetCursorPosX((w - ts.x) * 0.5f);
				ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.82f, 1),
					"%s", sub);
			}

			ImGui::Dummy(ImVec2(0, dp(20.0f)));

			{
				const char *body =
					"Altirra needs \"All files access\" to read Atari "
					"ROMs and disk images (.atr, .xex, .car, ...) "
					"from your Downloads folder and other storage "
					"locations.\n\n"
					"Android only allows you to grant this from the "
					"system Settings screen. The button below opens "
					"that screen directly — find Altirra in the list "
					"and enable the toggle, then return here.";
				float wrapW = w * 0.88f;
				float bodyX = (w - wrapW) * 0.5f;
				ImGui::SetCursorPosX(bodyX);
				ImGui::PushTextWrapPos(bodyX + wrapW);
				ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.90f, 1),
					"%s", body);
				ImGui::PopTextWrapPos();
			}

			ImGui::Dummy(ImVec2(0, dp(28.0f)));

			float pgBtnW = dp(280.0f);
			float pgBtnH = dp(56.0f);

			ImGui::SetCursorPosX((w - pgBtnW) * 0.5f);
			ImGui::PushStyleColor(ImGuiCol_Button,
				ImVec4(0.25f, 0.55f, 0.90f, 1));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
				ImVec4(0.30f, 0.60f, 0.95f, 1));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive,
				ImVec4(0.20f, 0.50f, 0.85f, 1));
			if (ImGui::Button("Open Android Settings",
				ImVec2(pgBtnW, pgBtnH)))
			{
				ATAndroid_OpenManageStorageSettings();
			}
			ImGui::PopStyleColor(3);

			ImGui::Dummy(ImVec2(0, dp(12.0f)));

			ImGui::SetCursorPosX((w - pgBtnW) * 0.5f);
			if (ImGui::Button("Continue Without Access",
				ImVec2(pgBtnW, pgBtnH)))
			{
				// Advance to the ROM-folder step.  The file browser
				// will still show its own in-app banner so the user
				// has another chance to grant access from there.
				s_permGateDismissed = true;
			}

			ImGui::End();
			return;
		}
#endif

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
void RenderLoadGamePrompt(ATSimulator &sim, ATUIState &uiState,
	ATMobileUIState &mobileState)
{
	ImGuiIO &io = ImGui::GetIO();

	const char *hintAscii = "or tap the menu icon for more options";

	// Pill dimensions — width adapts so the hint never clips, but
	// always stays inside the display with a comfortable side margin.
	float btnW  = dp(220.0f);
	float btnH  = dp(52.0f);
	float padX  = dp(18.0f);
	float padY  = dp(14.0f);
	float sideMargin = dp(16.0f);

	float hintSingleLineW = ImGui::CalcTextSize(hintAscii).x;
	float pillW = btnW + padX * 2;
	float wantedW = hintSingleLineW + padX * 2;
	if (wantedW > pillW) pillW = wantedW;
	float maxPillW = io.DisplaySize.x - sideMargin * 2;
	if (pillW > maxPillW) pillW = maxPillW;
	float minPillW = btnW + padX * 2;
	if (pillW < minPillW) pillW = minPillW;

	// Compute how tall the (potentially wrapped) hint will render so
	// the pill always fits its content even on narrow phones where
	// the hint must wrap to two lines.
	float hintWrapW = pillW - padX * 2;
	if (hintWrapW < dp(40.0f)) hintWrapW = dp(40.0f);
	ImVec2 hintSize = ImGui::CalcTextSize(hintAscii, nullptr, false, hintWrapW);
	float pillH = btnH + padY * 2 + dp(6.0f) + hintSize.y + dp(4.0f);

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

	// Primary action button — centered horizontally inside the pill.
	ImGui::SetCursorPos(ImVec2((pillW - btnW) * 0.5f, padY));
	if (ImGui::Button("Load Game", ImVec2(btnW, btnH))) {
		s_romFolderMode = false;
		mobileState.currentScreen = ATMobileUIScreen::FileBrowser;
		s_fileBrowserNeedsRefresh = true;
	}

	// Compact hint under the button — wrapped so it never clips on
	// narrow displays, and centered when it fits on a single line.
	{
		ImGui::Dummy(ImVec2(0, dp(6.0f)));
		if (hintSize.y <= ImGui::GetTextLineHeight() + 1.0f) {
			ImGui::SetCursorPosX((pillW - hintSingleLineW) * 0.5f);
			ImGui::TextColored(ImVec4(0.70f, 0.75f, 0.82f, 1),
				"%s", hintAscii);
		} else {
			ImGui::SetCursorPosX(padX);
			ImGui::PushTextWrapPos(padX + hintWrapW);
			ImGui::PushStyleColor(ImGuiCol_Text,
				ImVec4(0.70f, 0.75f, 0.82f, 1));
			ImGui::TextWrapped("%s", hintAscii);
			ImGui::PopStyleColor();
			ImGui::PopTextWrapPos();
		}
	}

	ImGui::End();

	ImGui::PopStyleColor(5);
	style.WindowRounding   = prevRounding;
	style.WindowBorderSize = prevBorder;
}
