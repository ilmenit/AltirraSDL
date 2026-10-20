// AltirraSDL - calibrated scRGB output. scRGB 1.0 represents 80 cd/m2.
#pragma once

#include <algorithm>
#include <cmath>

struct ATHDROutputLevels {
	float mFrameScale = 0.0f; // zero selects the unchanged SDR shader path
	float mUIScale = 1.0f;    // SDL already applies the system SDR white level
	bool mbHDR = false;
};

inline ATHDROutputLevels ATComputeHDROutputLevels(bool linearOutput,
	bool displayHDR, bool enableHDR, bool accelFX, float systemWhite,
	bool systemHDRBrightness, float hdrNits, bool systemSDRBrightness,
	float sdrNits)
{
	if (!linearOutput)
		return {};

	if (!std::isfinite(systemWhite) || systemWhite <= 0.0f)
		systemWhite = 1.0f;

	ATHDROutputLevels levels;
	levels.mFrameScale = systemWhite;
	levels.mbHDR = displayHDR && enableHDR && accelFX;
	if (levels.mbHDR) {
		const auto nits = [](float value, float fallback) {
			return std::isfinite(value) ? std::clamp(value, 1.0f, 10000.0f)
				: fallback;
		};
		levels.mFrameScale = systemHDRBrightness ? systemWhite
			: nits(hdrNits, 350.0f) / 80.0f;
		levels.mUIScale = systemSDRBrightness ? 1.0f
			: nits(sdrNits, 80.0f) / (80.0f * systemWhite);
	}
	return levels;
}
