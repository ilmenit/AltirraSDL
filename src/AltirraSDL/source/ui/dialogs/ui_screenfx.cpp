//	AltirraSDL - Adjust Screen Effects dialog
//	5-tab ImGui dialog matching the Windows version's layout:
//	  Main (scanlines, distortion), Bloom, HDR, Mask, Vignette.
//	The Vignette tab is SDL3-exclusive; the radial-darkening stage
//	lives alongside the other built-in effects in the GL backend.
//
//	The dialog is accessible from View > Adjust Screen Effects.
//	When the SDL_Renderer fallback is active (no GL), the menu item is
//	grayed out.  If the dialog is somehow opened without hardware support,
//	all controls are disabled with a warning banner.

#include <stdafx.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>

#include "ui_main.h"
#include "display_backend.h"
#include "simulator.h"
#include "gtia.h"
#include <vd2/VDDisplay/display.h>
#include <vd2/VDDisplay/displaytypes.h>

extern ATSimulator g_sim;
void ATUIResizeDisplay();

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

static void RenderMainPage(ATGTIAEmulator &gtia, ATArtifactingParams &params, bool &changed, bool hwSupport) {
	// Scanlines enable — mirrors the checkbox in Configure System > Video.
	// This is an SDL3 UI convenience: Windows only has the intensity slider
	// here, with the enable/disable in Configure System.  We show both so
	// the user can control scanlines from a single dialog.
	{
		bool scanlines = gtia.AreScanlinesEnabled();
		if (ImGui::Checkbox("Enable Scanlines", &scanlines)) {
			gtia.SetScanlinesEnabled(scanlines);
			ATUIResizeDisplay();
		}
	}

	ImGui::BeginDisabled(!gtia.AreScanlinesEnabled());

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

	ImGui::EndDisabled();

	ImGui::Spacing();

	// Distortion enable — the renderer treats mDistortionViewAngleX == 0
	// as "off" (gtia.cpp: `distortionEnabled = ap.mDistortionViewAngleX > 0`).
	// Track the last enabled X/Y so toggling doesn't wipe the user's
	// tuned values.  Defaults match ATArtifactingParams::GetDefault().
	// Y is saved as-is — Y=0 is a legitimate "horizontal-only" distortion.
	static float sSavedDistortionX = 35.0f;
	static float sSavedDistortionY = 0.90f;

	bool distortionEnabled = params.mDistortionViewAngleX > 0.0f;
	if (ImGui::Checkbox("Enable Distortion", &distortionEnabled)) {
		if (distortionEnabled) {
			params.mDistortionViewAngleX = sSavedDistortionX > 0.0f ? sSavedDistortionX : 35.0f;
			params.mDistortionYRatio     = sSavedDistortionY;
		} else {
			// Only capture on an active → inactive transition; guards
			// against a no-op second disable clobbering the saved pair
			// with (0, 0).
			if (params.mDistortionViewAngleX > 0.0f) {
				sSavedDistortionX = params.mDistortionViewAngleX;
				sSavedDistortionY = params.mDistortionYRatio;
			}
			params.mDistortionViewAngleX = 0.0f;
			params.mDistortionYRatio     = 0.0f;
		}
		changed = true;
	}

	ImGui::BeginDisabled(!distortionEnabled);

	// Distortion X view angle (0-120 degrees)
	{
		float angle = params.mDistortionViewAngleX;
		ImGui::Text("Distortion X View Angle: %.0f\u00B0", angle);
		if (ImGui::SliderFloat("##DistortionX", &angle, 0.0f, 120.0f, "")) {
			params.mDistortionViewAngleX = angle;
			if (angle > 0.0f)
				sSavedDistortionX = angle;
			changed = true;
		}
	}

	// Distortion Y ratio (0-200%; values >100% pull the top/bottom
	// edges into a more pronounced barrel than the horizontal curve)
	{
		float ratio = params.mDistortionYRatio * 100.0f;
		ImGui::Text("Distortion Y Ratio: %.0f%%", ratio);
		if (ImGui::SliderFloat("##DistortionY", &ratio, 0.0f, 200.0f, "")) {
			params.mDistortionYRatio = ratio / 100.0f;
			// Slider is reachable only when distortion is enabled, so the
			// current Y — including 0 — is the user's live preference.
			sSavedDistortionY = params.mDistortionYRatio;
			changed = true;
		}
	}

	ImGui::EndDisabled();
}

static void RenderVignettePage(ATArtifactingParams &params, bool &changed) {
	if (ImGui::Checkbox("Enable Vignette", &params.mbEnableVignette))
		changed = true;

	ImGui::BeginDisabled(!params.mbEnableVignette);

	// Intensity 0..100%: 100% darkens the corners completely.
	{
		float intensity = params.mVignetteIntensity * 100.0f;
		ImGui::Text("Intensity: %.0f%%", intensity);
		if (ImGui::SliderFloat("##VignetteIntensity", &intensity, 0.0f, 100.0f, "")) {
			params.mVignetteIntensity = intensity / 100.0f;
			changed = true;
		}
	}

	ImGui::EndDisabled();

	ImGui::Spacing();
	ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
		"Adds a radial darkening toward the corners to mimic the\n"
		"uneven phosphor brightness of a real CRT.  A slight amount\n"
		"(~18%%) is enabled by default.");
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
		VDDScreenMaskType newType = (VDDScreenMaskType)maskType;

		// When switching from None to a mask type, populate with sane
		// defaults if the current values are zeros (from default init
		// or settings that never had a mask configured).
		if (maskParams.mType == VDDScreenMaskType::None
			&& newType != VDDScreenMaskType::None
			&& maskParams.mSourcePixelsPerDot <= 0.0f)
		{
			auto defaults = ATGTIAEmulator::GetDefaultScreenMaskParams();
			maskParams.mSourcePixelsPerDot = defaults.mSourcePixelsPerDot;
			maskParams.mOpenness = defaults.mOpenness;
			maskParams.mbScreenMaskIntensityCompensation = defaults.mbScreenMaskIntensityCompensation;
		}

		maskParams.mType = newType;
		changed = true;
	}

	ImGui::BeginDisabled(maskParams.mType == VDDScreenMaskType::None);

	// Dot pitch (logarithmic).  Windows uses a trackbar range of -60..0
	// mapped via 2^(value/20), giving a range of 0.125..1.0 color clocks.
	// We replicate the same log2 scale and range.
	{
		float pitch = maskParams.mSourcePixelsPerDot;
		if (pitch < 0.01f) pitch = 0.125f;
		ImGui::Text("Dot Pitch: %.2f color clocks", pitch);
		if (SliderFloatLog("##DotPitch", &pitch, 0.125f, 1.0f)) {
			maskParams.mSourcePixelsPerDot = std::clamp(pitch, 0.01f, 1.0f);
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

	// Show info note when an external shader preset is also active.
	if (backend && backend->HasShaderPreset()) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
		ImGui::TextWrapped(
			"A librashader preset is active and will be applied on top of "
			"these built-in effects.");
		ImGui::PopStyleColor();
		ImGui::Spacing();
	}

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
			RenderMainPage(gtia, params, changed, hwSupport);
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
		if (ImGui::BeginTabItem("Vignette")) {
			ImGui::Spacing();
			RenderVignettePage(params, changed);
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
			maskParams = ATGTIAEmulator::GetDefaultScreenMaskParams();
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
