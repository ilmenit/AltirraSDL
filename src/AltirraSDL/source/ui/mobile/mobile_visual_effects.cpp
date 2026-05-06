//	AltirraSDL - Mobile UI: visual effects + performance presets
//	Split out of ui_mobile.cpp to keep it under 20KB.  Verbatim move;
//	behaviour identical.

#include <stdafx.h>
#include <SDL3/SDL.h>
#include "ui_mobile.h"
#include "ui_main.h"
#include "simulator.h"
#include "gtia.h"
#include <at/ataudio/pokey.h>
#include <vd2/VDDisplay/displaytypes.h>
#include "uiaccessors.h"
#include "uitypes.h"
#include "constants.h"

#include "mobile_internal.h"

extern ATSimulator g_sim;

// Push the three visual-effect toggles into the GTIA's
// ATArtifactingParams + scanlines flag.  Safe to call on any
// display backend — if the backend doesn't support GPU screen FX,
// the params are still stored but SyncScreenFXToBackend in
// main_sdl3.cpp skips the push.  Scanlines work in both the CPU and
// GL paths.
//
// Non-destructive write semantics: this helper never zeroes the
// numeric params just because the corresponding on/off flag is off.
// The renderer already gates each effect off the flag (or, for
// distortion which lacks a flag, off mDistortionViewAngleX > 0), so
// keeping the values populated lets the user toggle an effect off
// and on again — from the gaming-mode panel, the Adjust Screen
// Effects dialog, or the WASM CRT button — without losing whatever
// they had tuned.  First-enable populates from GetDefault() / a
// saved cache so the slider lands on a sensible value instead of 0.
void ATMobileUI_ApplyVisualEffects(const ATMobileUIState &mobileState) {
	ATGTIAEmulator &gtia = g_sim.GetGTIA();

	gtia.SetScanlinesEnabled(mobileState.fxScanlines);

	ATArtifactingParams params = gtia.GetArtifactingParams();
	const ATArtifactingParams defaults = ATArtifactingParams::GetDefault();

	// Bloom: the renderer keys off mbEnableBloom, so the radius /
	// intensities are inert when the flag is false — leave them
	// alone instead of zeroing.  Only seed defaults when enabling
	// for the first time (params are still at zero from a prior
	// off-state or a never-tuned profile).
	params.mbEnableBloom = mobileState.fxBloom;
	if (mobileState.fxBloom && params.mBloomRadius <= 0.0f) {
		params.mBloomRadius            = defaults.mBloomRadius;
		params.mBloomDirectIntensity   = defaults.mBloomDirectIntensity;
		params.mBloomIndirectIntensity = defaults.mBloomIndirectIntensity;
	}

	// Distortion has no separate enable flag — the renderer treats
	// mDistortionViewAngleX > 0 as "on".  Cache the last enabled
	// pair in static storage so toggling off → on restores the
	// user's tuned values instead of always snapping to defaults.
	// Same shape as ui_screenfx.cpp's saved-value pattern.
	static float sSavedDistortionX = defaults.mDistortionViewAngleX;
	static float sSavedDistortionY = defaults.mDistortionYRatio;
	if (mobileState.fxDistortion) {
		if (params.mDistortionViewAngleX <= 0.0f) {
			params.mDistortionViewAngleX = sSavedDistortionX > 0.0f
				? sSavedDistortionX : defaults.mDistortionViewAngleX;
			params.mDistortionYRatio     = sSavedDistortionY;
		}
	} else {
		if (params.mDistortionViewAngleX > 0.0f) {
			sSavedDistortionX = params.mDistortionViewAngleX;
			sSavedDistortionY = params.mDistortionYRatio;
		}
		params.mDistortionViewAngleX = 0.0f;
		params.mDistortionYRatio     = 0.0f;
	}

	params.mbEnableVignette = mobileState.fxVignette;
	if (mobileState.fxVignette && params.mVignetteIntensity <= 0.0f)
		params.mVignetteIntensity = defaults.mVignetteIntensity > 0.0f
			? defaults.mVignetteIntensity : 0.18f;

	gtia.SetArtifactingParams(params);

	VDDScreenMaskParams maskParams = gtia.GetScreenMaskParams();
	if (mobileState.fxApertureGrille) {
		auto defaults = ATGTIAEmulator::GetDefaultScreenMaskParams();
		maskParams.mType = VDDScreenMaskType::ApertureGrille;
		if (maskParams.mSourcePixelsPerDot <= 0.0f)
			maskParams.mSourcePixelsPerDot = defaults.mSourcePixelsPerDot;
		if (maskParams.mOpenness <= 0.0f)
			maskParams.mOpenness = defaults.mOpenness;
		maskParams.mbScreenMaskIntensityCompensation = defaults.mbScreenMaskIntensityCompensation;
	} else {
		maskParams.mType = VDDScreenMaskType::None;
	}
	gtia.SetScreenMaskParams(maskParams);
}

// Apply a bundled performance preset.  Efficient turns everything
// off and picks the cheapest filter.  Balanced keeps effects off
// but uses a nicer filter.  Quality enables all three CRT effects.
// Custom (3) is a no-op so the user's manual tweaks stay put.
void ATMobileUI_ApplyPerformancePreset(ATMobileUIState &mobileState) {
	int p = mobileState.performancePreset;
	if (p < 0 || p >= 3) return;  // Custom or out of range

	ATDisplayFilterMode filter = kATDisplayFilterMode_Bilinear;
	bool fastBoot       = true;
	bool interlace      = false;
	bool nonlinearMix   = true;
	bool audioMonitor   = false;
	bool driveSounds    = false;

	switch (p) {
	case 0: // Efficient
		mobileState.fxScanlines      = false;
		mobileState.fxBloom          = false;
		mobileState.fxDistortion     = false;
		mobileState.fxApertureGrille = false;
		mobileState.fxVignette       = false;
		filter        = kATDisplayFilterMode_Point;
		fastBoot      = true;
		interlace     = false;
		nonlinearMix  = false;
		driveSounds   = false;
		break;
	case 1: // Balanced
		mobileState.fxScanlines      = false;
		mobileState.fxBloom          = false;
		mobileState.fxDistortion     = false;
		mobileState.fxApertureGrille = false;
		mobileState.fxVignette       = true;
		filter        = kATDisplayFilterMode_Bilinear;
		fastBoot      = true;
		interlace     = false;
		nonlinearMix  = true;
		driveSounds   = false;
		break;
	case 2: // Quality
		mobileState.fxScanlines      = true;
		mobileState.fxBloom          = true;
		mobileState.fxDistortion     = true;
		mobileState.fxApertureGrille = true;
		mobileState.fxVignette       = true;
		filter        = kATDisplayFilterMode_SharpBilinear;
		fastBoot      = false;
		interlace     = true;
		nonlinearMix  = true;
		driveSounds   = true;
		break;
	}

	ATUISetDisplayFilterMode(filter);
	ATMobileUI_ApplyVisualEffects(mobileState);

	g_sim.SetFastBootEnabled(fastBoot);
	g_sim.GetGTIA().SetInterlaceEnabled(interlace);
	g_sim.GetPokey().SetNonlinearMixingEnabled(nonlinearMix);
	g_sim.SetAudioMonitorEnabled(audioMonitor);
	ATUISetDriveSoundsEnabled(driveSounds);
}
