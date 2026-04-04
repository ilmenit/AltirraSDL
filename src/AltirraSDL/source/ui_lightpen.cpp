//	AltirraSDL - Light Pen/Gun configuration dialog
//	ImGui implementation matching Windows IDD_LIGHTPEN.

#include <stdafx.h>
#include <imgui.h>
#include <vd2/system/vectors.h>
#include "ui_main.h"
#include "simulator.h"
#include "inputcontroller.h"

extern ATSimulator g_sim;

void ATUIRenderLightPenDialog(ATSimulator &sim, ATUIState &state) {
	if (!state.showLightPen)
		return;

	ATLightPenPort *lpp = sim.GetLightPenPort();
	if (!lpp) {
		state.showLightPen = false;
		return;
	}

	ImGui::SetNextWindowSize(ImVec2(340, 260), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	if (!ImGui::Begin("Light Pen/Gun", &state.showLightPen,
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse))
	{
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose()) {
		state.showLightPen = false;
		ImGui::End();
		return;
	}

	// Gun offsets
	ImGui::SeparatorText("Gun offset");
	{
		vdint2 gunAdj = lpp->GetAdjust(false);
		int gunX = gunAdj.x, gunY = gunAdj.y;

		ImGui::SetNextItemWidth(120);
		if (ImGui::InputInt("Gun H", &gunX, 1, 5)) {
			gunX = std::clamp(gunX, -64, 64);
			lpp->SetAdjust(false, vdint2{gunX, gunY});
		}

		ImGui::SetNextItemWidth(120);
		if (ImGui::InputInt("Gun V", &gunY, 1, 5)) {
			gunY = std::clamp(gunY, -64, 64);
			lpp->SetAdjust(false, vdint2{gunX, gunY});
		}
	}

	// Pen offsets
	ImGui::SeparatorText("Pen offset");
	{
		vdint2 penAdj = lpp->GetAdjust(true);
		int penX = penAdj.x, penY = penAdj.y;

		ImGui::SetNextItemWidth(120);
		if (ImGui::InputInt("Pen H", &penX, 1, 5)) {
			penX = std::clamp(penX, -64, 64);
			lpp->SetAdjust(true, vdint2{penX, penY});
		}

		ImGui::SetNextItemWidth(120);
		if (ImGui::InputInt("Pen V", &penY, 1, 5)) {
			penY = std::clamp(penY, -64, 64);
			lpp->SetAdjust(true, vdint2{penX, penY});
		}
	}

	// Noise mode
	ImGui::SeparatorText("Noise");
	static const char *kNoiseModes[] = {
		"None", "Low (CX-75 + 800)", "High (CX-75 + XL/XE)"
	};
	int noiseMode = (int)lpp->GetNoiseMode();
	if (noiseMode < 0 || noiseMode >= 3) noiseMode = 0;
	if (ImGui::Combo("Noise mode", &noiseMode, kNoiseModes, 3))
		lpp->SetNoiseMode((ATLightPenNoiseMode)noiseMode);

	ImGui::End();
}
