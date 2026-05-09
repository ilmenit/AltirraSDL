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
#include "settings.h"

#include "mobile_internal.h"

extern ATSimulator g_sim;
extern ATUIState g_uiState;
extern void ATRegistryFlushToDisk();

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
// off and picks the cheapest filter.  Balanced keeps the heavy CRT
// effects off but leaves vignette + bilinear on for a softer look.
// Quality enables all CRT effects.  Custom (3) is a no-op so the
// user's manual tweaks stay put.
//
// Each preset also drives the master CRT switches:
//   - g_uiState.screenEffectsMode (Basic vs None) — the binary master
//     toggle the desktop View > Screen Effects menu and the WASM page
//     "CRT" button both manipulate.
//   - GTIA artifacting mode (Auto vs None) — paired with the CRT look
//     so a fresh "CRT On" via the page button or the Quality preset
//     produces NTSC/PAL color artifacts immediately, and "CRT Off" /
//     Efficient turns them off in step.  Auto (not AutoHi) is used
//     deliberately: AutoHi is heavier, and the user-facing "CRT On"
//     should land on the cheaper authentic look.
//
// Numeric shader parameters (bloom radius, distortion angle, vignette
// intensity, mask openness, ...) are deliberately NOT touched here —
// only the on/off flags.  ATMobileUI_ApplyVisualEffects keeps the
// numerics non-destructive (see the comment above it), so toggling
// the preset never wipes a value the user tuned via the Adjust Screen
// Effects dialog.  The struct defaults from
// ATArtifactingParams::GetDefault and ATGTIAEmulator::GetDefault
// ScreenMaskParams remain the source of truth for first-use values.
void ATMobileUI_ApplyPerformancePreset(ATMobileUIState &mobileState) {
	int p = mobileState.performancePreset;
	if (p < 0 || p >= 3) return;  // Custom or out of range

	ATDisplayFilterMode filter = kATDisplayFilterMode_Bilinear;
	bool fastBoot       = true;
	bool interlace      = false;
	bool nonlinearMix   = true;
	bool audioMonitor   = false;
	bool driveSounds    = false;

	// Master CRT switches — kept in sync across all three preset
	// surfaces (Desktop View menu, Gaming-Mode preset, page CRT button).
	//
	// `wantArtifacting` is the on/off intent of this preset.  The actual
	// ATArtifactMode value is only forced when we cross the on/off
	// boundary (None ↔ not-None) so a user who picked AutoHi / NTSCHi /
	// PALHi from the desktop dialog isn't silently downgraded to Auto
	// on every Gaming-Mode startup.  When the boundary is crossed we
	// land on Auto (the cheaper authentic look) for "on" — heavier
	// modes like AutoHi remain a deliberate user choice.
	ATUIState::ScreenEffectsMode screenFXMode = ATUIState::kSFXMode_Basic;
	bool wantArtifacting = true;

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
		screenFXMode    = ATUIState::kSFXMode_None;
		wantArtifacting = false;
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
		screenFXMode    = ATUIState::kSFXMode_Basic;
		wantArtifacting = true;
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
		screenFXMode    = ATUIState::kSFXMode_Basic;
		wantArtifacting = true;
		break;
	}

	ATUISetDisplayFilterMode(filter);
	ATMobileUI_ApplyVisualEffects(mobileState);

	g_sim.SetFastBootEnabled(fastBoot);
	g_sim.GetGTIA().SetInterlaceEnabled(interlace);
	g_sim.GetPokey().SetNonlinearMixingEnabled(nonlinearMix);
	g_sim.SetAudioMonitorEnabled(audioMonitor);
	ATUISetDriveSoundsEnabled(driveSounds);

	// Drive the master CRT switches.  Don't clobber a librashader
	// preset (kSFXMode_Preset) — those are an external rendering path
	// the gaming-mode preset has no opinion about, so leave that mode
	// alone.  Going from a preset back to Basic / None requires the
	// user to clear the preset explicitly via the desktop View menu.
	if (g_uiState.screenEffectsMode != ATUIState::kSFXMode_Preset)
		g_uiState.screenEffectsMode = screenFXMode;

	// Only force the artifacting mode when crossing the on/off
	// boundary.  This preserves a desktop user's deliberate choice of
	// AutoHi / NTSCHi / PALHi across Gaming-Mode startup ApplyPreset
	// calls, while still flipping artifacting on/off when the preset
	// transitions to/from Efficient.
	ATGTIAEmulator &gtia = g_sim.GetGTIA();
	const ATArtifactMode curArtifact = gtia.GetArtifactingMode();
	const bool artifactingOn = (curArtifact != ATArtifactMode::None);
	bool artifactChanged = false;
	if (wantArtifacting && !artifactingOn) {
		gtia.SetArtifactingMode(ATArtifactMode::Auto);
		artifactChanged = true;
	} else if (!wantArtifacting && artifactingOn) {
		gtia.SetArtifactingMode(ATArtifactMode::None);
		artifactChanged = true;
	}

	// Persist the artifacting mode change immediately — settings.cpp
	// owns the registry key, and the mobile lifecycle rarely reaches
	// the clean-exit ATSaveSettings call (Android kills backgrounded
	// apps without notice).  Best-effort; the suspend path retries.
	if (artifactChanged) {
		try {
			ATSaveSettings(kATSettingsCategory_View);
		} catch (...) {
		}
		try {
			ATRegistryFlushToDisk();
		} catch (...) {
		}
	}
}
