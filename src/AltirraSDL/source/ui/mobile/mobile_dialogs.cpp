//	AltirraSDL - Mobile UI: modal info / confirm sheet renderer
//	Split out of ui_mobile.cpp to keep it under 20KB.  The Show*
//	helpers and state still live in ui_mobile.cpp; this file owns the
//	per-frame render of the bottom sheet.

#include <stdafx.h>
#include <imgui.h>
#include <SDL3/SDL.h>

#include "ui_mobile.h"
#include "mobile_internal.h"
#include "touch_widgets.h"
#include "altirra_icons.h"

// Render the global mobile dialog sheet — serves both info popups
// (ShowInfoModal, single OK button) and confirmation popups
// (ShowConfirmDialog, Cancel + Confirm buttons).  Card-style sheet
// sized to the phone display, centered in the safe area.
// No-op if neither s_infoModalOpen nor s_confirmActive is set.
void RenderMobileModalSheet(const ATMobileUIState &mobileState) {
	const bool haveInfo    = s_infoModalOpen;
	const bool haveConfirm = s_confirmActive;
	if (!(haveInfo || haveConfirm))
		return;

	const ATMobilePalette &pal = ATMobileGetPalette();

	// Full-screen dim backdrop.  Use the BACKGROUND draw list so
	// the rectangle renders *beneath* every ImGui window this
	// frame — otherwise the foreground list paints over the sheet
	// card and visibly darkens it.
	ImGui::GetBackgroundDrawList()->AddRectFilled(
		ImVec2(0, 0), ImGui::GetIO().DisplaySize,
		pal.backdropDim);

	// Modal input blocker.  The dim rect is intentionally drawn behind
	// all windows so the sheet card stays bright, but it does not stop
	// underlying full-screen lists from receiving wheel/drag input.
	// This invisible window owns the viewport until the sheet closes.
	ImGui::SetNextWindowPos(ImVec2(0, 0));
	ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
	ImGui::Begin("##MobileModalBlocker", nullptr,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);
	ImGui::InvisibleButton("##MobileModalBlockerHit",
		ImGui::GetIO().DisplaySize);
	ImGui::End();

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
	// Force focus onto the modal sheet every frame it is visible.
	// Without NavEnableGamepad this is a no-op for touch users; with
	// gamepad nav enabled it traps D-pad navigation inside the sheet
	// so the user cannot accidentally focus widgets that are still
	// drawn behind the dim backdrop.  SetNextWindowFocus only
	// changes which *window* is active — the per-item focus inside
	// the modal is preserved between frames, so D-pad up/down still
	// moves between OK/Cancel/Confirm normally.
	ImGui::SetNextWindowFocus();

	ImGuiStyle &style = ImGui::GetStyle();
	float prevR = style.WindowRounding;
	float prevB = style.WindowBorderSize;
	style.WindowRounding = dp(14.0f);
	style.WindowBorderSize = dp(1.0f);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ATMobileCol(pal.modalBg));
	ImGui::PushStyleColor(ImGuiCol_Border,   ATMobileCol(pal.modalBorder));

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

	// Subtle gradient backdrop for the sheet card — a touch of depth so
	// the modal reads as a lifted surface instead of a flat rectangle.
	// Use the shared helper so the gradient respects the window's
	// rounded corners (WindowRounding = dp(14.0f) above) instead of
	// overwriting them with a rectangular bleed.
	{
		ImVec2 wpos = ImGui::GetWindowPos();
		ImVec2 wsize = ImGui::GetWindowSize();
		ATMobileDrawGradientRect(
			wpos, ImVec2(wpos.x + wsize.x, wpos.y + wsize.y),
			pal.modalBgTop, pal.modalBg, style.WindowRounding);
	}

	ImGui::Dummy(ImVec2(0, dp(8.0f)));
	if (title && *title) {
		ImGui::SetWindowFontScale(1.25f);
		ImGui::PushStyleColor(ImGuiCol_Text, ATMobileCol(pal.textTitle));
		ImGui::TextUnformatted(title);
		ImGui::PopStyleColor();
		ImGui::SetWindowFontScale(1.0f);
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
	}
	ImGui::PushTextWrapPos(sheetW - dp(24.0f));
	ImGui::PushStyleColor(ImGuiCol_Text, ATMobileCol(pal.text));
	ImGui::TextUnformatted(body);
	ImGui::PopStyleColor();
	ImGui::PopTextWrapPos();
	ImGui::Dummy(ImVec2(0, dp(16.0f)));
	ImGui::Separator();
	ImGui::Dummy(ImVec2(0, dp(8.0f)));

	// ESC / B-button dismisses the modal (Cancel for confirm, OK for info).
	{
		bool dismiss = ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false);
		if (!ImGui::IsAnyItemActive()) {
			dismiss = dismiss
				|| ImGui::IsKeyPressed(ImGuiKey_Escape, false)
				|| ImGui::IsKeyPressed(ImGuiKey_Backspace, false);
		}
		if (dismiss) {
			if (haveConfirm) {
				s_confirmActive = false;
				s_confirmAction = nullptr;
			} else {
				s_infoModalOpen = false;
			}
		}
	}

	float btnH = dp(56.0f);
	float rowW = ImGui::GetContentRegionAvail().x;
	if (haveConfirm) {
		float gap = dp(12.0f);
		float halfW = (rowW - gap) * 0.5f;

		// Cancel = neutral card surface; Confirm = accent.  Matches the
		// rest of Gaming Mode's lifted-card button silhouette.
		if (ATTouchButton("Cancel", ImVec2(halfW, btnH),
			ATTouchButtonStyle::Neutral, ICON_MD_CLOSE)) {
			s_confirmActive = false;
			s_confirmAction = nullptr;
		}

		ImGui::SameLine(0.0f, gap);

		if (ATTouchButton("Confirm", ImVec2(halfW, btnH),
			ATTouchButtonStyle::Accent, ICON_MD_CHECK))
		{
			auto act = s_confirmAction;
			s_confirmActive = false;
			s_confirmAction = nullptr;
			if (act) act();
		}
	} else {
		if (ATTouchButton("OK", ImVec2(-1, btnH),
			ATTouchButtonStyle::Accent, ICON_MD_CHECK))
		{
			s_infoModalOpen = false;
		}
	}

	ImGui::End();
	ImGui::PopStyleColor(2);
	style.WindowRounding = prevR;
	style.WindowBorderSize = prevB;
}
