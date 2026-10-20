//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2024 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#include <stdafx.h>
#include <numeric>
#include <vd2/VDDisplay/display.h>
#include <vd2/VDDisplay/internal/bloom.h>
#include <vd2/VDDisplay/internal/options.h>

uint32 g_VDDispBloomCoeffsChanged = 0;
VDDBloomV2Settings g_VDDispBloomV2Settings;

void VDDSetBloomV2Settings(const VDDBloomV2Settings& settings) {
	g_VDDispBloomV2Settings = settings;
	++g_VDDispBloomCoeffsChanged;
}

VDDBloomV2RenderParams VDDComputeBloomV2Parameters(const VDDBloomV2ControlParams& controlParams) {
	VDDBloomV2RenderParams renderParams {};

	// The reference filter has a standard deviation of 2.2 pixels in the narrowest,
	// most dominant gaussian, and 2.572 host pixels per hires pixel. We normalize this
	// as a filter width of 0.855 hires pixels. We then convert to log base 2 to determine
	// how many pyramid levels to slide the reference filter.

	const float radius = std::max<float>(0.001f, controlParams.mAdjustRadius * controlParams.mBaseRadius / 2.2f);
	const float filterBias = logf(radius) / logf(2.0f);

	const auto filter = [](float x) {
		if (x >= 2.0f)
			return powf(2.0f, -1.5f * (x - 2.0f));
		else if (x >= 1.0f)
			return x - 1.0f;
		else
			return 0.0f;
	};

	float pyramidWeights[8] {};

	for(int i=0; i<8; ++i)
		pyramidWeights[i] = filter((float)i - filterBias);

	float pyramidWeightSum = std::accumulate(std::begin(pyramidWeights), std::end(pyramidWeights), 0.0f);

	// if the sum is zero, it means that the pyramid is too small to accommodate the blur --
	// push everything to the smallest pyramid
	if (pyramidWeightSum < 1e-10f) {
		pyramidWeights[7] = 1.0f;
		pyramidWeightSum = 1.0f;
	}

	// normalize weights
	float pyramidWeightScale = controlParams.mIndirectIntensity / pyramidWeightSum;

	for(float& weight : pyramidWeights)
		weight *= pyramidWeightScale;

	float runningScale = 1.0f;

	for(int i=4; i>=0; --i) {
		vdfloat2 blendFactors {
			(i == 4 ? pyramidWeights[i + 3] : 1.0f) * runningScale,
			pyramidWeights[i + 2]
		};

		float blendFactorSum = blendFactors.x + blendFactors.y;

		runningScale = std::clamp<float>(
			blendFactorSum,
			0.01f, 100.0f);

		renderParams.mPassBlendFactors[4 - i] = blendFactors / runningScale;
	}

	renderParams.mPassBlendFactors[5] = vdfloat2 {
		runningScale,
		0.0f
	};

	const auto cubic =
		[](float x1, float m1, float y1, float x2, float m2, float y2) -> vdfloat4 {
			if (x2 - x1 < 1e-5f)
				return vdfloat4{0, 0, 0, y1};

			float dx = x2 - x1;

			m1 *= dx;
			m2 *= dx;

			// compute cubic spline over 0-1
			vdfloat4 curve {
				2*(y1 - y2) + (m1 + m2),
				3*(y2 - y1) - 2*m1 - m2,
				m1,
				y1
			};

			// scale by dx (substitute t = s/dx)
			float idx = 1.0f / dx;
			float idx2 = idx*idx;
			float idx3 = idx2*idx;

			curve.x *= idx3;
			curve.y *= idx2;
			curve.z *= idx;

			// translate by x1 (substitute s = q - x1)
			// f(x) = A(x-x1)^3 + B(x-x1)^2 + C(x-x1) + D
			//      = A(x^3 - 3x^2*x1 + 3x*x1^2 - x1^3) + B(x^2-2x*x1+x1^2) + C(x-x1) + D
			//      = Ax^3 - 3Ax^2*x1 + 3Ax*x1^2 - Ax1^3 + Bx^2 - 2Bx*x1 + Bx1^2) + Cx - Cx1 + D
			//      = Ax^3 + (-3Ax1 + B)x^2 + (3Ax1^2 - 2Bx1 + C)x + (-Ax1^3 + Bx1^2 - Cx1 + D)
			float x1_2 = x1*x1;
			float x1_3 = x1_2*x1;
			return vdfloat4 {
				curve.x,
				-3*curve.x*x1 + curve.y,
				3*curve.x*x1_2 - 2*curve.y*x1 + curve.z,
				-curve.x*x1_3 + curve.y*x1_2 - curve.z*x1 + curve.w
			};
		};

	// Shoulder conditions:
	//	f(0) = 0 --> D = 0
	//	f'(0) = m2 --> C = m2
	//	f(1) = 1 --> A+B = -m2
	//	f'(1) = 0 --> 3A+2B = -m2
	//
	// A = m2
	// B = -m2
	// C = m2
	// D = 0
	//
	// t = (x - shoulderX)/(limitX - shoulderX)

	// f(x1) = y1	=> Ax1^3 + Bx1^2 + Cx1 + D = y1
	// f(x2) = y2	=> Ax2^3 + Bx2^2 + Cx2 + D = y2
	// f'(x1) = m1	=> 3Ax1^2 + 2Bx + C = m1
	// f'(x2) = m2	=> 3Ax1^2 + 2Bx + C = m2

	float limitX = std::max<float>(g_VDDispBloomV2Settings.mLimitX, 0.1f);
	float limitSlope = g_VDDispBloomV2Settings.mLimitSlope;
	float shoulderX = std::clamp<float>(g_VDDispBloomV2Settings.mShoulderX, 0.0f, limitX);
	float shoulderY = std::clamp<float>(g_VDDispBloomV2Settings.mShoulderY, 0.0f, 1.0f);
	float midSlope = shoulderX > 0 ? shoulderY / shoulderX : 1.0f;

	if (!controlParams.mbEnableSoftClamp) {
		renderParams.mShoulder = vdfloat4{};
		renderParams.mThresholds = vdfloat4 { midSlope, 100.0f, 100.0f, 0.0f };
	} else {
		renderParams.mShoulder = cubic(shoulderX, midSlope, shoulderY, limitX, limitSlope, 1.0f);
		renderParams.mThresholds = vdfloat4 { midSlope, shoulderX, limitX, 0.0f };
	}
	
	// The direct filtered portion of the pyramid is computed using a 9-tap filter
	// bilinearly filtering over a 5x5 region. The UV step used to space apart the
	// 3x3 tap grid interpolates between a 3x3 filter:
	//
	// |  0  0 |  0 |  0  0 |
	// |  0  1 |  2 |  1  0 |
	// +-------+----+-------+
	// |  0  2 |  4 |  2  0 | / 16
	// +-------+----+-------+
	// |  0  1 |  2 |  1  0 |
	// |  0  0 |  0 |  0  0 |
	//
	// ...and a 5x5 filter:
	//
	// |  1  4 |  6 |  4  1 |
	// |  4 16 | 24 | 16  4 |
	// +-------+----+-------+
	// |  6 24 | 36 | 24  6 | / 256
	// +-------+----+-------+
	// |  4 16 | 24 | 16  4 |
	// |  1  4 |  6 |  4  1 |
	//
	// Thus, by controlling the weights on the corners/sides/center and the
	// scale of the UV step, we can compute any weighted sum of radius-1,
	// radius-2, and radius-3 filters.

	float w12sum = pyramidWeights[0] + pyramidWeights[1];
	float w12ratio = pyramidWeights[1] / std::max<float>(w12sum, 1e-5f);

	renderParams.mBaseUVStepScale = 1.0f + 0.2f * w12ratio;

	renderParams.mBaseWeights = vdfloat4 {
		w12sum * 25.0f / 256.0f,		// corners (x4 = 100/256)
		w12sum * 30.0f / 256.0f,		// sides (x4 = 120/256)
		w12sum * 36.0f / 256.0f + controlParams.mDirectIntensity,		// center tap (36/256)
		0.0f
	};

	return renderParams;
}
