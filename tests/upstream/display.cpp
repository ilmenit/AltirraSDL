#include <stdafx.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <vd2/system/cpuaccel.h>
#include <vd2/VDDisplay/displaytypes.h>
#include <vd2/VDDisplay/internal/bloom.h>
#include <vd2/VDDisplay/internal/screenfx.h>

static void Check(bool ok, const char *message) {
	if (!ok) {
		std::fprintf(stderr, "%s\n", message);
		std::exit(1);
	}
}

int main() {
	uint32 gamma[256];
	VDDisplayCreateGammaRamp(gamma, 256, false, 1.0f, 1.0f);
	Check(gamma[0] == 0 && gamma[255] == 0xFFFFFFFF,
		"Gamma ramp must preserve black and full white");
	for (int i = 0; i < 256; ++i)
		Check(gamma[i] == (uint32)i * 0x01010101,
			"Identity gamma ramp changed an input level");

	// Excessive zoom/radius previously left no pyramid weights and divided
	// by zero. Check both finite coefficients and retained indirect energy.
	for (float radius : {1.0f, 64.0f, 256.0f, 1000000.0f}) {
		VDDBloomV2ControlParams c {};
		c.mBaseRadius = radius;
		c.mAdjustRadius = 1.0f;
		c.mDirectIntensity = 1.0f;
		c.mIndirectIntensity = 0.75f;
		const auto p = VDDComputeBloomV2Parameters(c);
		float energy = 1.0f;
		for (const auto& factors : p.mPassBlendFactors) {
			Check(std::isfinite(factors.x) && std::isfinite(factors.y),
				"Bloom pyramid contains non-finite coefficients");
			energy = energy * factors.x + factors.y;
		}
		energy += 4 * p.mBaseWeights.x + 4 * p.mBaseWeights.y
			+ p.mBaseWeights.z - c.mDirectIntensity;
		Check(std::fabs(energy - c.mIndirectIntensity) < 1e-5f,
			"Bloom pyramid lost indirect intensity at extreme zoom");
	}

	const long detected = CPUCheckForExtensions();
	constexpr int w = 73, h = 29;
	std::vector<uint32> scalar(w * h), accelerated(w * h);
	for (float scale : {0.75f, 1.18f, 2.3f, 5.0f}) {
		for (float offset : {0.0f, 0.25f, 13.7f}) {
			VDDScreenMaskParams mask;
			mask.mType = VDDScreenMaskType::DotTriad;
			mask.mSourcePixelsPerDot = scale;
			mask.mOpenness = 0.75f;
			VDDisplayTriadDotMaskParams params(mask, 640.0f, 456.0f);
			CPUEnableExtensions(0);
			VDDisplayCreateTriadDotMaskTexture(scalar.data(), w * 4,
				w, h, offset, offset, w, h, params);
			CPUEnableExtensions(detected);
			VDDisplayCreateTriadDotMaskTexture(accelerated.data(), w * 4,
				w, h, offset, offset, w, h, params);
			for (size_t i = 0; i < scalar.size(); ++i) {
				for (int shift : {0, 8, 16}) {
					const int a = (scalar[i] >> shift) & 255;
					const int b = (accelerated[i] >> shift) & 255;
					Check(std::abs(a - b) <= 1,
						"Accelerated dot mask differs from scalar filtering");
				}
			}
#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
			// FMA flags only exist on x86/x64; ARM tests the scalar path above.
			// AVX2 without FMA must still use the scalar implementation.
			CPUEnableExtensions(detected & ~VDCPUF_SUPPORTS_FMA);
			VDDisplayCreateTriadDotMaskTexture(accelerated.data(), w * 4,
				w, h, offset, offset, w, h, params);
			Check(scalar == accelerated, "Dot mask dispatch requires FMA");
#endif
		}
	}
	CPUEnableExtensions(detected);
	std::puts("Display gamma, bloom, and dot-mask regressions passed");
}
