//	AltirraSDL - Input Setup dialog (dead zones, power curves)
//	Split out of ui_input.cpp (Phase 2j).

#include <stdafx.h>
#include <cmath>
#include <imgui.h>
#include <vd2/system/vdtypes.h>

#include "ui_main.h"
#include "simulator.h"
#include "joystick.h"

namespace {

bool g_inputSetupWasOpen = false;
ATJoystickTransforms g_savedTransforms;
ATJoystickTransforms g_currentTransforms;

} // namespace

// =========================================================================
// Input Setup dialog — dead zones and power curves
// =========================================================================

static const float kAnalogPowerExponentScale = 0.13862943611198906188344642429164f; // ln(4)/10

void ATUIRenderInputSetup(ATSimulator &sim, ATUIState &state) {
	if (!state.showInputSetup) {
		// Clean up if dialog was closed externally while capture was active
		if (g_inputSetupWasOpen) {
			IATJoystickManager *pJoyMan = sim.GetJoystickManager();
			if (pJoyMan) {
				pJoyMan->SetTransforms(g_savedTransforms);
				pJoyMan->SetCaptureMode(false);
			}
			g_inputSetupWasOpen = false;
		}
		return;
	}

	IATJoystickManager *pJoyMan = sim.GetJoystickManager();
	if (!pJoyMan) {
		state.showInputSetup = false;
		return;
	}

	// Save transforms on first open
	if (!g_inputSetupWasOpen) {
		g_savedTransforms = pJoyMan->GetTransforms();
		g_currentTransforms = g_savedTransforms;
		g_inputSetupWasOpen = true;
		pJoyMan->SetCaptureMode(true);
	}

	ImGui::SetNextWindowSize(ImVec2(480, 500), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("Input Setup", &state.showInputSetup, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		if (!state.showInputSetup) {
			// Cancel — restore saved transforms
			pJoyMan->SetTransforms(g_savedTransforms);
			pJoyMan->SetCaptureMode(false);
			g_inputSetupWasOpen = false;
		}
		return;
	}

	if (ATUICheckEscClose()) {
		state.showInputSetup = false;
		// Cancel — restore saved transforms
		pJoyMan->SetTransforms(g_savedTransforms);
		pJoyMan->SetCaptureMode(false);
		g_inputSetupWasOpen = false;
		ImGui::End();
		return;
	}

	auto DeadZoneToSlider = [](sint32 dz) -> int {
		return (int)roundf(sqrtf((float)dz / 65536.0f) * 100.0f);
	};
	auto SliderToDeadZone = [](int v) -> sint32 {
		float f = (float)v / 100.0f;
		return (sint32)roundf(f * f * 65536.0f);
	};
	auto PowerToSlider = [](float power) -> int {
		return (int)roundf(logf(power) / kAnalogPowerExponentScale);
	};
	auto SliderToPower = [](int v) -> float {
		return expf((float)v * kAnalogPowerExponentScale);
	};

	bool changed = false;

	// Stick section
	ImGui::SeparatorText("Stick");
	{
		int adz = DeadZoneToSlider(g_currentTransforms.mStickAnalogDeadZone);
		int ddz = DeadZoneToSlider(g_currentTransforms.mStickDigitalDeadZone);
		int pwr = PowerToSlider(g_currentTransforms.mStickAnalogPower);

		ImGui::Text("Analog dead zone:");
		if (ImGui::SliderInt("##sadz", &adz, 0, 100, "%d%%")) {
			g_currentTransforms.mStickAnalogDeadZone = SliderToDeadZone(adz);
			changed = true;
		}

		ImGui::Text("Digital dead zone:");
		if (ImGui::SliderInt("##sddz", &ddz, 0, 100, "%d%%")) {
			g_currentTransforms.mStickDigitalDeadZone = SliderToDeadZone(ddz);
			changed = true;
		}

		ImGui::Text("Analog power:");
		if (ImGui::SliderInt("##spwr", &pwr, -10, 10, "%+d")) {
			g_currentTransforms.mStickAnalogPower = SliderToPower(pwr);
			changed = true;
		}
	}

	// Trigger section
	ImGui::SeparatorText("Triggers");
	{
		int adz = DeadZoneToSlider(g_currentTransforms.mTriggerAnalogDeadZone);
		int ddz = DeadZoneToSlider(g_currentTransforms.mTriggerDigitalDeadZone);
		int pwr = PowerToSlider(g_currentTransforms.mTriggerAnalogPower);

		ImGui::Text("Analog dead zone:");
		if (ImGui::SliderInt("##tadz", &adz, 0, 100, "%d%%")) {
			g_currentTransforms.mTriggerAnalogDeadZone = SliderToDeadZone(adz);
			changed = true;
		}

		ImGui::Text("Digital dead zone:");
		if (ImGui::SliderInt("##tddz", &ddz, 0, 100, "%d%%")) {
			g_currentTransforms.mTriggerDigitalDeadZone = SliderToDeadZone(ddz);
			changed = true;
		}

		ImGui::Text("Analog power:");
		if (ImGui::SliderInt("##tpwr", &pwr, -10, 10, "%+d")) {
			g_currentTransforms.mTriggerAnalogPower = SliderToPower(pwr);
			changed = true;
		}
	}

	if (changed)
		pJoyMan->SetTransforms(g_currentTransforms);

	// Live gamepad visualization
	ImGui::SeparatorText("Live Input");
	{
		uint32 n = 0;
		const ATJoystickState *states = pJoyMan->PollForCapture(n);

		ATJoystickState state = {};
		if (states && n > 0)
			state = states[0]; // Show first active controller

		// Draw stick visualization
		ImVec2 canvasPos = ImGui::GetCursorScreenPos();
		ImVec2 canvasSize(120, 120);
		ImDrawList *dl = ImGui::GetWindowDrawList();

		// Left stick
		ImGui::Text("Left Stick");
		canvasPos = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton("##lstick", canvasSize);

		dl->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
			IM_COL32(40, 40, 40, 255));
		dl->AddRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
			IM_COL32(128, 128, 128, 255));

		// Crosshairs at center
		float cx = canvasPos.x + canvasSize.x * 0.5f;
		float cy = canvasPos.y + canvasSize.y * 0.5f;
		dl->AddLine(ImVec2(canvasPos.x, cy), ImVec2(canvasPos.x + canvasSize.x, cy),
			IM_COL32(192, 192, 255, 128));
		dl->AddLine(ImVec2(cx, canvasPos.y), ImVec2(cx, canvasPos.y + canvasSize.y),
			IM_COL32(192, 192, 255, 128));

		// Position dot
		float px = cx + (float)state.mAxisVals[0] / 65536.0f * canvasSize.x * 0.5f;
		float py = cy + (float)state.mAxisVals[1] / 65536.0f * canvasSize.y * 0.5f;
		dl->AddCircleFilled(ImVec2(px, py), 4.0f, IM_COL32(0, 255, 0, 255));

		// Adjusted position dot
		float dpx = cx + (float)state.mDeadifiedAxisVals[0] / 65536.0f * canvasSize.x * 0.5f;
		float dpy = cy + (float)state.mDeadifiedAxisVals[1] / 65536.0f * canvasSize.y * 0.5f;
		dl->AddCircleFilled(ImVec2(dpx, dpy), 3.0f, IM_COL32(255, 255, 0, 255));
	}

	ImGui::Separator();

	// OK / Cancel
	bool accepted = false;
	if (ImGui::Button("OK", ImVec2(80, 0))) {
		pJoyMan->SetTransforms(g_currentTransforms);
		pJoyMan->SetCaptureMode(false);
		state.showInputSetup = false;
		g_inputSetupWasOpen = false;
		accepted = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(80, 0))) {
		pJoyMan->SetTransforms(g_savedTransforms);
		pJoyMan->SetCaptureMode(false);
		state.showInputSetup = false;
		g_inputSetupWasOpen = false;
	}

	// Handle close via X button (cancel semantics) — but not if OK was clicked
	if (!accepted && !state.showInputSetup && g_inputSetupWasOpen) {
		pJoyMan->SetTransforms(g_savedTransforms);
		pJoyMan->SetCaptureMode(false);
		g_inputSetupWasOpen = false;
	}

	ImGui::End();
}
