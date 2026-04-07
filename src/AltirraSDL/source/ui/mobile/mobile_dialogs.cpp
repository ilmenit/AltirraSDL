//	AltirraSDL - Mobile UI: modal info / confirm sheet renderer
//	Split out of ui_mobile.cpp to keep it under 20KB.  The Show*
//	helpers and state still live in ui_mobile.cpp; this file owns the
//	per-frame render of the bottom sheet.

#include <stdafx.h>
#include <imgui.h>
#include <SDL3/SDL.h>

#include "ui_mobile.h"
#include "mobile_internal.h"

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
