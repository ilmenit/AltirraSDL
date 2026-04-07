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
void ATMobileUI_ApplyVisualEffects(const ATMobileUIState &mobileState) {
	ATGTIAEmulator &gtia = g_sim.GetGTIA();

	gtia.SetScanlinesEnabled(mobileState.fxScanlines);

	ATArtifactingParams params = gtia.GetArtifactingParams();

	params.mbEnableBloom = mobileState.fxBloom;
	if (mobileState.fxBloom) {
		params.mBloomRadius            = 8.0f;
		params.mBloomDirectIntensity   = 1.00f;
		params.mBloomIndirectIntensity = 0.80f;
	} else {
		params.mBloomRadius            = 0.0f;
		params.mBloomDirectIntensity   = 0.0f;
		params.mBloomIndirectIntensity = 0.0f;
	}

	if (mobileState.fxDistortion) {
		params.mDistortionViewAngleX = 85.0f;
		params.mDistortionYRatio     = 0.50f;
	} else {
		params.mDistortionViewAngleX = 0.0f;
		params.mDistortionYRatio     = 0.0f;
	}

	gtia.SetArtifactingParams(params);
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
		mobileState.fxScanlines  = false;
		mobileState.fxBloom      = false;
		mobileState.fxDistortion = false;
		filter        = kATDisplayFilterMode_Point;
		fastBoot      = true;
		interlace     = false;
		nonlinearMix  = false;
		driveSounds   = false;
		break;
	case 1: // Balanced
		mobileState.fxScanlines  = false;
		mobileState.fxBloom      = false;
		mobileState.fxDistortion = false;
		filter        = kATDisplayFilterMode_Bilinear;
		fastBoot      = true;
		interlace     = false;
		nonlinearMix  = true;
		driveSounds   = false;
		break;
	case 2: // Quality
		mobileState.fxScanlines  = true;
		mobileState.fxBloom      = true;
		mobileState.fxDistortion = true;
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
