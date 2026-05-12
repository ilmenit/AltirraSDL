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
#include "../tools/setup_wizard_shared.h"
#include "../../app/wipe_data.h"

extern ATSimulator g_sim;
extern VDStringA ATGetConfigDir();
extern void ATRegistryFlushToDisk();
extern IDisplayBackend *ATUIGetDisplayBackend();

void RenderMobileAbout(ATSimulator &sim, ATUIState &uiState,
	ATMobileUIState &mobileState, SDL_Window *window)
{
	ImGuiIO &io = ImGui::GetIO();
	const ATMobilePalette &pal = ATMobileGetPalette();

	// Full-screen palette-aware background so the About screen adopts
	// the current theme's window colour instead of a flat dark slab.
	ImGui::GetBackgroundDrawList()->AddRectFilled(
		ImVec2(0, 0), io.DisplaySize, pal.windowBg);

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
		// State for the destructive "Reset Altirra (delete all data)"
		// confirm overlay (declared up here so ESC/Back can dismiss
		// the overlay before exiting the About screen).
		static bool s_showResetConfirm = false;

		// ESC / B-button / Backspace: dismisses the reset overlay
		// first if it's open, otherwise returns to the hamburger
		// menu.  Without the two-step we'd leave About with the
		// modal flag still set, and the modal would reappear on
		// next About open.
		{
			bool back = ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false);
			if (!ImGui::IsAnyItemActive()) {
				back = back
					|| ImGui::IsKeyPressed(ImGuiKey_Escape, false)
					|| ImGui::IsKeyPressed(ImGuiKey_Backspace, false);
			}
			if (back) {
				if (s_showResetConfirm)
					s_showResetConfirm = false;
				else
					mobileState.currentScreen = ATMobileUIScreen::HamburgerMenu;
			}
		}

		float w = ImGui::GetContentRegionAvail().x;

		ImGui::Dummy(ImVec2(0, dp(24.0f)));

		// Large Altirra title — uses the palette's title accent so the
		// light theme gets a readable dark-blue instead of pale grey.
		{
			const char *title = "Altirra";
			ImGui::SetWindowFontScale(2.2f);
			float tw = ImGui::CalcTextSize(title).x;
			ImGui::SetCursorPosX((w - tw) * 0.5f);
			ImGui::TextColored(ATMobileCol(pal.textTitle), "%s", title);
			ImGui::SetWindowFontScale(1.0f);
		}

		ImGui::Dummy(ImVec2(0, dp(4.0f)));

		// Subtitle
		{
			const char *sub = "Atari 800/XL/5200 Emulator";
			float tw = ImGui::CalcTextSize(sub).x;
			ImGui::SetCursorPosX((w - tw) * 0.5f);
			ImGui::TextColored(ATMobileCol(pal.text), "%s", sub);
		}

		ImGui::Dummy(ImVec2(0, dp(6.0f)));

		// SDL3 / ImGui frontend identifier
		{
			const char *sub2 = "SDL3 + Dear ImGui cross-platform frontend";
			float tw = ImGui::CalcTextSize(sub2).x;
			ImGui::SetCursorPosX((w - tw) * 0.5f);
			ImGui::TextColored(ATMobileCol(pal.textMuted), "%s", sub2);
		}

		ImGui::Dummy(ImVec2(0, dp(24.0f)));
		ImGui::Separator();
		ImGui::Dummy(ImVec2(0, dp(16.0f)));

		// Credits block — scrollable child so long text doesn't push
		// the Close button off-screen on small phones.  Reserve room
		// for the three footer buttons (Reset Altirra + Debug Log +
		// Close), the gaps between them, and the ItemSpacing ImGui
		// inserts between successive items.  Reset Altirra is the
		// new bottom row added for the data-wipe action.
		float resetBtnH = dp(48.0f);
		float debugBtnH = dp(48.0f);
		float closeH = dp(56.0f);
		float gap = dp(8.0f);
		float bottomMargin = dp(16.0f);
		float itemSpacingY = ImGui::GetStyle().ItemSpacing.y;
		float bottomReserve = resetBtnH + gap
			+ debugBtnH + gap + closeH
			+ bottomMargin + itemSpacingY * 5.0f;
		// NavFlattened so the scrollable credits area doesn't trap the
		// gamepad cursor — without it, D-pad down from the back arrow
		// would land on the child window itself instead of skipping
		// over the (non-interactive) text to the Close button.
		ImGui::BeginChild("AboutCredits",
			ImVec2(0, ImGui::GetContentRegionAvail().y - bottomReserve),
			ImGuiChildFlags_NavFlattened);
		ATTouchDragScroll();

		ImGui::PushTextWrapPos(w - dp(16.0f));
		ImGui::TextColored(ATMobileCol(pal.text),
			"Altirra is an Atari 800/800XL/5200 emulator authored by "
			"Avery Lee.  This Android build uses the AltirraSDL "
			"cross-platform frontend, which replaces the original Win32 "
			"UI with SDL3 + Dear ImGui for portability.");
		ImGui::Dummy(ImVec2(0, dp(12.0f)));

		ImGui::TextColored(ATMobileCol(pal.text),
			"The emulation core is cycle-accurate and identical across "
			"platforms — only the UI, display, audio and input layers "
			"are platform-specific.");
		ImGui::Dummy(ImVec2(0, dp(12.0f)));

		ImGui::TextColored(ATMobileCol(pal.text),
			"Original Altirra Copyright (C) Avery Lee.\n"
			"Licensed under GNU GPL v2 or later.");
		ImGui::Dummy(ImVec2(0, dp(8.0f)));

		// Contribution credit — palette's accent for a subtle highlight
		// in both themes (warm amber on dark → dark-blue accent on
		// light).  Uses the accent since that's the colour already
		// reserved for "highlighted foreground".
		ImGui::TextColored(ATMobileCol(pal.accent),
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
			ImGui::TextColored(ATMobileCol(pal.textMuted),
				"Performance preset: %s (change in Settings > "
				"Performance).", presetLabel);
		}
		ImGui::Dummy(ImVec2(0, dp(8.0f)));

		// Current configuration read-out — collapsible so the credits
		// stay readable for users who don't care, but available for
		// users who want to audit what the wizard / first-run defaults
		// applied without re-running the wizard.  Default collapsed.
		{
			static bool s_showConfig = false;
			const char *label = s_showConfig
				? "Hide current configuration"
				: "Show current configuration";
			if (ATTouchButton(label, ImVec2(-1, dp(44.0f)),
				ATTouchButtonStyle::Subtle))
			{
				s_showConfig = !s_showConfig;
			}
			if (s_showConfig) {
				ImGui::Dummy(ImVec2(0, dp(6.0f)));
				Wiz_RenderConfigurationSummary(sim);
				ImGui::Dummy(ImVec2(0, dp(6.0f)));
				ATTouchMutedText(
					"Adjust any of these in Settings > Configure "
					"System.");
			}
		}
		ImGui::Dummy(ImVec2(0, dp(12.0f)));

		ImGui::TextColored(ATMobileCol(pal.text),
			"Third-party components:\n"
			"  - SDL3  (zlib license)\n"
			"  - Dear ImGui  (MIT license)\n"
			"  - Roboto font  (Apache 2.0)\n"
			"  - Fira Mono font  (SIL Open Font License)");
		ImGui::PopTextWrapPos();

		ATTouchEndDragScroll();
		ImGui::EndChild();

		// Reset Altirra button — destructive, opens a confirm overlay
		// drawn after the Close button below.  Subtle style so it
		// doesn't compete with Close as the primary action.  The
		// s_showResetConfirm flag is declared at the top of this
		// function so the ESC/Back handler can dismiss the overlay
		// before exiting the About screen.

		// Disable the underlying button row while the confirm overlay
		// is shown.  The overlay's dimmer is a draw-only rect (no
		// input blocking), and we use a regular ImGui::Begin (not
		// BeginPopupModal) so taps would otherwise pass through to
		// these buttons, leaving the modal flag dirty for the next
		// time the About screen opens.
		ImGui::BeginDisabled(s_showResetConfirm);

		if (ATTouchButton("Reset Altirra (delete all data)",
			ImVec2(-1, dp(48.0f)),
			ATTouchButtonStyle::Subtle))
		{
			s_showResetConfirm = true;
		}
		ImGui::Dummy(ImVec2(0, dp(8.0f)));

		// Debug Log button — opens the in-app log viewer.  On Android,
		// stderr is gated behind an adb pairing the user typically
		// doesn't have, so this is the only path to read NETPLAY /
		// disk / audio channel output for diagnostics.  Modal-sheet
		// styled so it reads as a secondary action below Close.
		if (ATTouchButton("Debug Log", ImVec2(-1, dp(48.0f)))) {
			uiState.showDebugLog = true;
		}
		ImGui::Dummy(ImVec2(0, dp(8.0f)));

		// Close button pinned to the bottom — Accent variant so it
		// reads as the primary action and matches the rest of Gaming
		// Mode's visual language.
		if (ATTouchButton("Close", ImVec2(-1, closeH),
			ATTouchButtonStyle::Accent))
		{
			mobileState.currentScreen = ATMobileUIScreen::HamburgerMenu;
		}

		ImGui::EndDisabled();

		// --- Reset confirmation overlay ---
		// Drawn inline (still inside the About window's Begin) as a
		// centered modal pseudo-window with a dimmer.  Input gating is
		// handled by the BeginDisabled block above, not by ImGui's popup
		// stack, because the touch UI controls its own z-order and
		// palette colours.
		if (s_showResetConfirm) {
			float dimW = io.DisplaySize.x;
			float dimH = io.DisplaySize.y;

			// The dimmer is rendered as its own full-screen ImGui
			// window submitted BEFORE the modal — relying on window
			// stack order to place it below the modal.
			//
			// Why: GetForegroundDrawList() (no args) is the GLOBAL
			// foreground list, rendered AFTER every window in the
			// frame, including the modal we Begin below — so it
			// covered the modal and made the dialog appear darkened
			// and unreadable.  A separate dimmer window stacks
			// correctly: About content < dimmer < modal.
			ImGui::SetNextWindowPos(ImVec2(0, 0));
			ImGui::SetNextWindowSize(ImVec2(dimW, dimH));
			ImGui::PushStyleColor(ImGuiCol_WindowBg,
				IM_COL32(0, 0, 0, 160));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
			ImGui::Begin("##ResetDim", nullptr,
				ImGuiWindowFlags_NoTitleBar
				| ImGuiWindowFlags_NoResize
				| ImGuiWindowFlags_NoMove
				| ImGuiWindowFlags_NoCollapse
				| ImGuiWindowFlags_NoSavedSettings
				| ImGuiWindowFlags_NoScrollbar
				| ImGuiWindowFlags_NoScrollWithMouse
				| ImGuiWindowFlags_NoBringToFrontOnFocus
				| ImGuiWindowFlags_NoFocusOnAppearing
				| ImGuiWindowFlags_NoNav
				| ImGuiWindowFlags_NoInputs);
			ImGui::End();
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor();

			float modW = dimW * 0.88f;
			if (modW > dp(420.0f)) modW = dp(420.0f);
			float modH = dp(360.0f);
			float modX = (dimW - modW) * 0.5f;
			float modY = (dimH - modH) * 0.5f;

			ImGui::SetNextWindowPos(ImVec2(modX, modY));
			ImGui::SetNextWindowSize(ImVec2(modW, modH));
			ImGui::PushStyleColor(ImGuiCol_WindowBg,
				ATMobileCol(pal.modalBg));
			ImGui::PushStyleColor(ImGuiCol_Border,
				ATMobileCol(pal.modalBorder));
			ImGuiStyle &style = ImGui::GetStyle();
			float prevRound = style.WindowRounding;
			float prevBorder = style.WindowBorderSize;
			style.WindowRounding = dp(14.0f);
			style.WindowBorderSize = dp(2.0f);

			ImGui::Begin("##ResetConfirm", nullptr,
				ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
				| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
				| ImGuiWindowFlags_NoCollapse);

			ImGui::PushStyleColor(ImGuiCol_Text, ATMobileCol(pal.textTitle));
			ImGui::SetWindowFontScale(1.3f);
			ImGui::TextUnformatted("Reset Altirra?");
			ImGui::SetWindowFontScale(1.0f);
			ImGui::PopStyleColor();
			ImGui::Dummy(ImVec2(0, dp(8.0f)));

			ImGui::PushTextWrapPos(modW - dp(24.0f));
			ImGui::PushStyleColor(ImGuiCol_Text, ATMobileCol(pal.text));
			ImGui::TextUnformatted(
				"This deletes every file Altirra saved on this device:\n"
				"settings, save state, game library, custom art, lobby "
				"cache, and crash logs.\n\n"
				"Altirra will close immediately.  Reopening starts the "
				"first-time setup again.  This cannot be undone.");
			ImGui::PopStyleColor();
			ImGui::PopTextWrapPos();

			// Push the buttons to the bottom of the modal via a
			// flexible spacer.  Avoids the off-by-WindowPadding bug
			// of computing an absolute SetCursorPosY (cursor coords
			// already start past WindowPadding.y), and keeps the
			// layout robust if the body text length changes.
			float spacerH = ImGui::GetContentRegionAvail().y
				- dp(56.0f) - ImGui::GetStyle().ItemSpacing.y;
			if (spacerH > 0.0f)
				ImGui::Dummy(ImVec2(0, spacerH));

			float btnW = (modW - dp(24.0f) - dp(8.0f)) * 0.5f;
			if (ATTouchButton("Cancel",
				ImVec2(btnW, dp(56.0f))))
			{
				s_showResetConfirm = false;
			}
			ImGui::SameLine();
			if (ATTouchButton("Reset and Exit",
				ImVec2(btnW, dp(56.0f)),
				ATTouchButtonStyle::Danger))
			{
				ATWipeAndExit();  // never returns
			}

			ImGui::End();

			style.WindowRounding = prevRound;
			style.WindowBorderSize = prevBorder;
			ImGui::PopStyleColor(2);
		}
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
	const ATMobilePalette &pal = ATMobileGetPalette();

	// Full-screen palette-aware background — themes switch cleanly.
	ImGui::GetBackgroundDrawList()->AddRectFilled(
		ImVec2(0, 0), io.DisplaySize, pal.windowBg);

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
		// Back-key handling: the first-run wizard is the root screen
		// on a fresh install (no game, no settings, nothing to go back
		// to inside the app), so Android's idiom for Back-on-root
		// applies — send the activity to the launcher rather than
		// silently swallowing the press.  The wizard state is in the
		// registry so the user resumes mid-flow when they relaunch.
		// On desktop, Back is a no-op here (no equivalent gesture).
		{
			bool back = ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false);
			if (!ImGui::IsAnyItemActive()) {
				back = back
					|| ImGui::IsKeyPressed(ImGuiKey_Escape, false)
					|| ImGui::IsKeyPressed(ImGuiKey_Backspace, false);
			}
			if (back) {
#ifdef __ANDROID__
				if (window)
					SDL_MinimizeWindow(window);
#endif
			}
		}

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
				ImGui::TextColored(ATMobileCol(pal.textTitle),
					"%s", title);
				ImGui::SetWindowFontScale(1.0f);
			}

			ImGui::Spacing();

			{
				const char *sub = "Step 1 of 2";
				ImVec2 ts = ImGui::CalcTextSize(sub);
				ImGui::SetCursorPosX((w - ts.x) * 0.5f);
				ImGui::TextColored(ATMobileCol(pal.textMuted),
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
				ImGui::TextColored(ATMobileCol(pal.text), "%s", body);
				ImGui::PopTextWrapPos();
			}

			ImGui::Dummy(ImVec2(0, dp(28.0f)));

			float pgBtnW = dp(280.0f);
			float pgBtnH = dp(56.0f);

			ImGui::SetCursorPosX((w - pgBtnW) * 0.5f);
			if (ATTouchButton("Open Android Settings",
				ImVec2(pgBtnW, pgBtnH),
				ATTouchButtonStyle::Accent))
			{
				ATAndroid_OpenManageStorageSettings();
			}

			ImGui::Dummy(ImVec2(0, dp(12.0f)));

			ImGui::SetCursorPosX((w - pgBtnW) * 0.5f);
			if (ATTouchButton("Continue Without Access",
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

		// Two persistent toggles — outlive a single render so the user's
		// choice survives if they open and close the More-options link
		// or the Android permission gate sneaks back in front of us.
		// Defaults match the silent-apply path: convenient + on.
		static int  s_experience = 0;   // 0=Convenient, 1=Authentic
		static bool s_addonsOn   = true;

		// Apply the chosen knobs to the simulator.  Idempotent — calling
		// twice is a no-op.  deferReset=false: the wizard exits to the
		// running emulator immediately after, so we want the changes
		// live before the next frame.
		//
		// On the first-run path the user never sees a Finish page with
		// the full configuration summary, so we surface the same data as
		// a short non-blocking toast after apply.  Users who want the
		// full audit can open Settings > About > Current configuration.
		auto applyChosenDefaults = []() {
			if (s_experience == 1)
				Wiz_ApplyAuthenticExperience(g_sim, /*deferReset=*/false);
			else
				Wiz_ApplyConvenientExperience(g_sim, /*deferReset=*/false);
			Wiz_ApplyHardwareAddons(g_sim, s_addonsOn, /*deferReset=*/false);

			char summary[256];
			Wiz_FormatConfigSummaryLine(g_sim, summary, sizeof(summary));
			ATTouchPushFeedback("Defaults applied", summary,
				ATTouchToastSeverity::Info);
		};

		// Layout-by-content: the wizard now has more content (toggles +
		// link), so center vertically using a generous reservation.
		// On a 720dp tablet portrait this sits comfortably above the
		// keyboard area; on a phone in landscape it keeps action buttons
		// above the soft-key band.
		float contentH = dp(560.0f);
		float topPad = (h - contentH) * 0.5f;
		if (topPad < dp(20.0f)) topPad = dp(20.0f);
		ImGui::Dummy(ImVec2(0, topPad));

		// Title
		{
			const char *title = "Altirra";
			ImGui::SetWindowFontScale(2.0f);
			float tw = ImGui::CalcTextSize(title).x;
			ImGui::SetCursorPosX((w - tw) * 0.5f);
			ImGui::TextColored(ATMobileCol(pal.textTitle), "%s", title);
			ImGui::SetWindowFontScale(1.0f);
		}

		ImGui::Spacing();

		// Subtitle
		{
			const char *sub = "Atari 800/XL/5200 Emulator";
			ImVec2 ts = ImGui::CalcTextSize(sub);
			ImGui::SetCursorPosX((w - ts.x) * 0.5f);
			ImGui::TextColored(ATMobileCol(pal.textMuted), "%s", sub);
		}

		ImGui::Dummy(ImVec2(0, dp(20.0f)));

		// Body text — kept brief; the toggles below are self-explanatory.
		{
			const char *body =
				"Welcome!  Pick how you want the emulator to behave, "
				"then choose whether to load original Atari ROM firmware "
				"or use the built-in replacement (AltirraOS).";
			float wrapW = w * 0.85f;
			float bodyX = (w - wrapW) * 0.5f;
			ImGui::SetCursorPosX(bodyX);
			ImGui::PushTextWrapPos(bodyX + wrapW);
			ImGui::TextColored(ATMobileCol(pal.text), "%s", body);
			ImGui::PopTextWrapPos();
		}

		ImGui::Dummy(ImVec2(0, dp(20.0f)));

		// ── Knob 1: Emulation experience (Convenient | Authentic) ─────
		{
			float knobW = w * 0.85f;
			float knobX = (w - knobW) * 0.5f;
			ImGui::SetCursorPosX(knobX);
			ImGui::BeginGroup();
			ImGui::PushItemWidth(knobW);
			static const char *items[] = { "Convenient", "Authentic" };
			ATTouchSegmented("Emulation experience",
				&s_experience, items, 2);
			ImGui::PopItemWidth();
			ImGui::EndGroup();

			ImGui::SetCursorPosX(knobX);
			ImGui::PushTextWrapPos(knobX + knobW);
			ImGui::TextColored(ATMobileCol(pal.textMuted),
				s_experience == 1
				? "Authentic: hardware artifacts, drive sounds, "
				  "accurate disk timing, no SIO patches."
				: "Convenient: clean display, fast SIO loading, no "
				  "drive sounds.  Right for casual play.");
			ImGui::PopTextWrapPos();
		}

		ImGui::Dummy(ImVec2(0, dp(16.0f)));

		// ── Knob 2: Hardware add-ons (On | Off) ─────────────────────
		// Skip silently for 5200 hardware mode — none of the four
		// add-ons (VBXE/Covox/Stereo POKEY/1088K) apply to it.  For
		// 5200 we just hide the toggle; the apply helpers also no-op
		// for 5200 so this is purely a UI tidy.
		const bool is5200 =
			(g_sim.GetHardwareMode() == kATHardwareMode_5200);
		if (!is5200) {
			float knobW = w * 0.85f;
			float knobX = (w - knobW) * 0.5f;
			ImGui::SetCursorPosX(knobX);
			ImGui::BeginGroup();
			ImGui::PushItemWidth(knobW);
			ATTouchToggle("Modern-demo hardware add-ons", &s_addonsOn);
			ImGui::PopItemWidth();
			ImGui::EndGroup();

			ImGui::SetCursorPosX(knobX);
			ImGui::PushTextWrapPos(knobX + knobW);
			ImGui::TextColored(ATMobileCol(pal.textMuted),
				s_addonsOn
				? "On: VBXE, Covox, Stereo POKEY, 1088 KB RAM enabled. "
				  "Required for most modern Atari demos."
				: "Off: stock 800XL/130XE hardware.  Right for "
				  "period-accurate originals.");
			ImGui::PopTextWrapPos();
		}

		ImGui::Dummy(ImVec2(0, dp(24.0f)));

		// ── Action buttons — centered, stacked ─────────────────────
		float btnW = dp(280.0f);
		float btnH = dp(56.0f);

		ImGui::SetCursorPosX((w - btnW) * 0.5f);
		if (ATTouchButton("Select Firmware ROMs Folder", ImVec2(btnW, btnH),
			ATTouchButtonStyle::Accent))
		{
			applyChosenDefaults();
			s_romFolderMode = true;
			s_fileBrowserNeedsRefresh = true;
			mobileState.currentScreen = ATMobileUIScreen::FileBrowser;
			SetFirstRunComplete();
		}

		ImGui::Dummy(ImVec2(0, dp(12.0f)));

		ImGui::SetCursorPosX((w - btnW) * 0.5f);
		// Note: avoid Unicode dashes — ImGui's default font doesn't ship
		// the U+2014 em-dash glyph, so it renders as a fallback '?'.
		if (ATTouchButton("Skip - Use Built-in AltirraOS",
			ImVec2(btnW, btnH)))
		{
			applyChosenDefaults();
			mobileState.currentScreen = ATMobileUIScreen::None;
			SetFirstRunComplete();
		}

		ImGui::Dummy(ImVec2(0, dp(16.0f)));

		// ── "More options..." link → multi-page wizard ───────────────
		// Power users who want the full configuration flow (Game
		// Library, Firmware scan, Joystick mapping, etc.) can opt into
		// it here.  Wiz_Open sets currentScreen=SetupWizard for Gaming
		// Mode, which dispatches to mobile_setup_wizard.cpp's renderer.
		// FirstRunComplete is marked here so even if the user closes
		// the multi-page wizard mid-flow, they don't get prompted by
		// this shortened wizard again.
		{
			const char *more = "More options...";
			ImVec2 ts = ImGui::CalcTextSize(more);
			ImGui::SetCursorPosX((w - ts.x) * 0.5f);
			ImGui::PushStyleColor(ImGuiCol_Text, ATMobileCol(pal.accent));
			ImGui::TextUnformatted(more);
			ImGui::PopStyleColor();
			ImVec2 linkMin = ImGui::GetItemRectMin();
			ImVec2 linkMax = ImGui::GetItemRectMax();
			// Expand hit area slightly so the link is touch-friendly
			// without making it look like a button.
			linkMin.y -= dp(8.0f);
			linkMax.y += dp(8.0f);
			linkMin.x -= dp(16.0f);
			linkMax.x += dp(16.0f);
			if (ImGui::IsMouseHoveringRect(linkMin, linkMax)
				&& ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				applyChosenDefaults();
				SetFirstRunComplete();
				extern void Wiz_Open(ATUIState &);
				Wiz_Open(uiState);
			}
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
//   - Palette-aware opaque pill with an accent button — readable on
//     both Dark and Light themes (the previous hard-coded near-black
//     pill washed out the Load Game button on a light background).
//   - No separate title — the button label and a compact subtitle
//     carry the message.
void RenderLoadGamePrompt(ATSimulator &sim, ATUIState &uiState,
	ATMobileUIState &mobileState)
{
	ImGuiIO &io = ImGui::GetIO();
	const ATMobilePalette &pal = ATMobileGetPalette();

	const char *hintAscii = mobileState.showHamburgerMenu
		? "or tap the menu icon for more options"
		: "press ESC or click Menu for more options";

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

	// Palette-driven pill background + accent border.  Using the
	// modal-sheet colours keeps Light theme legible against a bright
	// AltirraOS boot screen underneath.
	ImGui::SetNextWindowPos(ImVec2(pillX, pillY));
	ImGui::SetNextWindowSize(ImVec2(pillW, pillH));

	ImGuiStyle &style = ImGui::GetStyle();
	float prevRounding = style.WindowRounding;
	float prevBorder   = style.WindowBorderSize;
	style.WindowRounding   = dp(16.0f);
	style.WindowBorderSize = dp(2.0f);

	ImGui::PushStyleColor(ImGuiCol_WindowBg, ATMobileCol(pal.modalBg));
	ImGui::PushStyleColor(ImGuiCol_Border,   ATMobileCol(pal.modalBorder));

	ImGui::Begin("##LoadPrompt", nullptr,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	// Primary action button — centered horizontally inside the pill.
	ImGui::SetCursorPos(ImVec2((pillW - btnW) * 0.5f, padY));
	if (ATTouchButton("Load Game", ImVec2(btnW, btnH),
		ATTouchButtonStyle::Accent))
	{
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
			ImGui::TextColored(ATMobileCol(pal.textMuted),
				"%s", hintAscii);
		} else {
			ImGui::SetCursorPosX(padX);
			ImGui::PushTextWrapPos(padX + hintWrapW);
			ImGui::PushStyleColor(ImGuiCol_Text,
				ATMobileCol(pal.textMuted));
			ImGui::TextWrapped("%s", hintAscii);
			ImGui::PopStyleColor();
			ImGui::PopTextWrapPos();
		}
	}

	ImGui::End();

	ImGui::PopStyleColor(2);
	style.WindowRounding   = prevRounding;
	style.WindowBorderSize = prevBorder;
}
