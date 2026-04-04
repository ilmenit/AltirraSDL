//	AltirraSDL - Adjust Screen Effects dialog
//	4-tab ImGui dialog matching the Windows version's layout:
//	  Main (scanlines, distortion), Bloom, HDR, Mask.
//
//	The dialog is accessible from View > Adjust Screen Effects.
//	When the SDL_Renderer fallback is active (no GL), the menu item is
//	grayed out.  If the dialog is somehow opened without hardware support,
//	all controls are disabled with a warning banner.

#include <stdafx.h>
#include <imgui.h>
#include <cmath>

#include "ui_main.h"
#include "display_backend.h"
#include "simulator.h"
#include "gtia.h"
#include <vd2/VDDisplay/display.h>
#include <vd2/VDDisplay/displaytypes.h>

extern ATSimulator g_sim;

// Helper: logarithmic slider (for bloom radius and dot pitch)
static bool SliderFloatLog(const char *label, float *v, float vMin, float vMax, const char *format = "%.2f") {
	float logMin = logf(vMin);
	float logMax = logf(vMax);
	float logV = logf(std::max(*v, vMin));
	if (ImGui::SliderFloat(label, &logV, logMin, logMax, format)) {
		*v = expf(logV);
		return true;
	}
	return false;
}

static void RenderMainPage(ATArtifactingParams &params, bool &changed, bool hwSupport) {
	// Scanline intensity: ticks 0-8, displayed as percentage
	{
		int tick = (int)(params.mScanlineIntensity * 8.0f + 0.5f);
		if (tick < 0) tick = 0;
		if (tick > 8) tick = 8;
		ImGui::Text("Scanline Intensity: %d%%", (int)(params.mScanlineIntensity * 100.0f + 0.5f));
		if (ImGui::SliderInt("##ScanlineInt", &tick, 0, 8, "")) {
			params.mScanlineIntensity = (float)tick / 8.0f;
			changed = true;
		}
	}

	ImGui::Spacing();

	// Distortion X view angle (0-180 degrees)
	{
		float angle = params.mDistortionViewAngleX;
		ImGui::Text("Distortion X View Angle: %.0f\u00B0", angle);
		if (ImGui::SliderFloat("##DistortionX", &angle, 0.0f, 180.0f, "")) {
			params.mDistortionViewAngleX = angle;
			changed = true;
		}
	}

	// Distortion Y ratio (0-100%)
	{
		float ratio = params.mDistortionYRatio * 100.0f;
		ImGui::Text("Distortion Y Ratio: %.0f%%", ratio);
		if (ImGui::SliderFloat("##DistortionY", &ratio, 0.0f, 100.0f, "")) {
			params.mDistortionYRatio = ratio / 100.0f;
			changed = true;
		}
	}
}

static void RenderBloomPage(ATArtifactingParams &params, bool &changed) {
	if (ImGui::Checkbox("Enable Bloom", &params.mbEnableBloom))
		changed = true;

	ImGui::BeginDisabled(!params.mbEnableBloom);

	// Radius (logarithmic, 0.1-10.0)
	{
		float radius = params.mBloomRadius;
		if (radius < 0.1f) radius = 0.1f;
		ImGui::Text("Radius: %.2f", radius);
		if (SliderFloatLog("##BloomRadius", &radius, 0.1f, 10.0f)) {
			params.mBloomRadius = radius;
			changed = true;
		}
	}

	// Direct intensity (0-200%)
	{
		float intensity = params.mBloomDirectIntensity * 100.0f;
		ImGui::Text("Direct Intensity: %.2f", params.mBloomDirectIntensity);
		if (ImGui::SliderFloat("##BloomDirect", &intensity, 0.0f, 200.0f, "")) {
			params.mBloomDirectIntensity = intensity / 100.0f;
			changed = true;
		}
	}

	// Indirect intensity (0-200%)
	{
		float intensity = params.mBloomIndirectIntensity * 100.0f;
		ImGui::Text("Indirect Intensity: %.2f", params.mBloomIndirectIntensity);
		if (ImGui::SliderFloat("##BloomIndirect", &intensity, 0.0f, 200.0f, "")) {
			params.mBloomIndirectIntensity = intensity / 100.0f;
			changed = true;
		}
	}

	if (ImGui::Checkbox("Scanline Compensation", &params.mbBloomScanlineCompensation))
		changed = true;

	ImGui::EndDisabled();
}

static void RenderHDRPage(ATArtifactingParams &params, bool &changed) {
	ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
		"HDR support requires platform-specific display capabilities.\n"
		"This feature is not yet available in the SDL3 build.");
	ImGui::Spacing();

	ImGui::BeginDisabled(true);

	ImGui::Checkbox("Enable HDR", &params.mbEnableHDR);
	ImGui::Checkbox("Use System SDR Brightness", &params.mbUseSystemSDR);
	ImGui::Checkbox("Use System HDR Brightness", &params.mbUseSystemSDRAsHDR);

	float sdrNits = params.mSDRIntensity;
	ImGui::Text("SDR Brightness: %.0f nits", sdrNits);
	ImGui::SliderFloat("##SDRBright", &sdrNits, 80.0f, 500.0f, "");

	float hdrNits = params.mHDRIntensity;
	ImGui::Text("HDR Brightness: %.0f nits", hdrNits);
	ImGui::SliderFloat("##HDRBright", &hdrNits, 80.0f, 1000.0f, "");

	ImGui::Spacing();
	ImGui::Text("Monitor: (not available)");
	ImGui::Text("Current Mode: SDR");

	ImGui::EndDisabled();
}

static void RenderMaskPage(VDDScreenMaskParams &maskParams, bool &changed) {
	static const char *maskTypes[] = {
		"None",
		"Aperture grille (vertical)",
		"Dot mask",
		"Slot mask"
	};

	int maskType = (int)maskParams.mType;
	if (ImGui::Combo("Mask Type", &maskType, maskTypes, 4)) {
		maskParams.mType = (VDDScreenMaskType)maskType;
		changed = true;
	}

	ImGui::BeginDisabled(maskParams.mType == VDDScreenMaskType::None);

	// Dot pitch (logarithmic)
	{
		float pitch = maskParams.mSourcePixelsPerDot;
		if (pitch < 0.5f) pitch = 0.5f;
		ImGui::Text("Dot Pitch: %.1f color clocks", pitch);
		if (SliderFloatLog("##DotPitch", &pitch, 0.5f, 20.0f)) {
			maskParams.mSourcePixelsPerDot = pitch;
			changed = true;
		}
	}

	// Openness (25-100%)
	{
		float openness = maskParams.mOpenness * 100.0f;
		ImGui::Text("Openness: %.0f%%", openness);
		if (ImGui::SliderFloat("##Openness", &openness, 25.0f, 100.0f, "")) {
			maskParams.mOpenness = openness / 100.0f;
			changed = true;
		}
	}

	if (ImGui::Checkbox("Intensity Compensation", &maskParams.mbScreenMaskIntensityCompensation))
		changed = true;

	ImGui::EndDisabled();
}

void ATUIRenderScreenEffects(ATSimulator &sim, ATUIState &state) {
	if (!state.showScreenEffects)
		return;

	ImGui::SetNextWindowSize(ImVec2(500, 450), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	if (!ImGui::Begin("Adjust Screen Effects", &state.showScreenEffects,
		ImGuiWindowFlags_NoSavedSettings))
	{
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose())
		state.showScreenEffects = false;

	IDisplayBackend *backend = ATUIGetDisplayBackend();
	bool hwSupport = backend && backend->SupportsScreenFX();

	// Show warning banner when hardware acceleration is not available.
	// This matches the Windows dialog's IDC_WARNING behavior.
	if (!hwSupport) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.7f, 0.3f, 1.0f));
		ImGui::TextWrapped(
			"Screen effects require the OpenGL display backend. "
			"The current display is using the SDL_Renderer fallback, "
			"which does not support GPU post-processing.\n\n"
			"All controls below are disabled.");
		ImGui::PopStyleColor();
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
	}

	ATGTIAEmulator &gtia = sim.GetGTIA();
	ATArtifactingParams params = gtia.GetArtifactingParams();
	VDDScreenMaskParams maskParams = gtia.GetScreenMaskParams();
	bool changed = false;
	bool maskChanged = false;

	// Disable all controls when hardware support is not available
	ImGui::BeginDisabled(!hwSupport);

	if (ImGui::BeginTabBar("ScreenFXTabs")) {
		if (ImGui::BeginTabItem("Main")) {
			ImGui::Spacing();
			RenderMainPage(params, changed, hwSupport);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Bloom")) {
			ImGui::Spacing();
			RenderBloomPage(params, changed);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("HDR")) {
			ImGui::Spacing();
			RenderHDRPage(params, changed);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Mask")) {
			ImGui::Spacing();
			RenderMaskPage(maskParams, maskChanged);
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	ImGui::EndDisabled();

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	if (hwSupport) {
		if (ImGui::Button("Reset to Defaults")) {
			params = ATArtifactingParams::GetDefault();
			maskParams = VDDScreenMaskParams{};
			changed = true;
			maskChanged = true;
		}
	}

	// Update GTIA params — the per-frame SyncScreenFXToBackend() in main_sdl3.cpp
	// will pick up the changes and push them to the GL backend automatically.
	if (changed)
		gtia.SetArtifactingParams(params);

	if (maskChanged)
		gtia.SetScreenMaskParams(maskParams);

	ImGui::End();
}
