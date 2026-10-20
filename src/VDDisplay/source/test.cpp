//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2026 Avery Lee
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
#include <at/attest/test.h>

#ifdef AT_TESTS_ENABLED
#include <vd2/system/time.h>
#include <vd2/system/vdstl.h>
#include <vd2/VDDisplay/displaytypes.h>
#include <vd2/VDDisplay/internal/screenfx.h>

AT_DEFINE_TEST_NONAUTO(Display_ScreenFX_DotMaskBench) {
	vdfastvector<uint32> screen(2560 * 1440);

	VDDScreenMaskParams maskParams;
	maskParams.mType = VDDScreenMaskType::DotTriad;
	maskParams.mSourcePixelsPerDot = 1.18f;
	maskParams.mOpenness = 0.75f;

	VDDisplayTriadDotMaskParams dotMaskParams(maskParams, 2560.0f, 1440.0f);
	double sumTime = 0;
	double minTime = 0;

	for(int i=0; i<100; ++i) {
		const auto t0 = VDGetPreciseTick();
		VDDisplayCreateTriadDotMaskTexture(screen.data(), 2560 * 4, 2560, 1440, 0.0f, 0.0f, 2560.0, 1440.0f, dotMaskParams);
		const double dt = (double)(VDGetPreciseTick() - t0) * VDGetPreciseSecondsPerTick();

		sumTime += dt;
		if (!i)
			minTime = dt;
		else
			minTime = std::min(minTime, dt);
	}

	printf("2560x1440 mask generated in %.2fms average | %.2fms min\n", sumTime / 100.0 * 1000.0, minTime * 1000.0);

	return 0;
}

#endif	// AT_TESTS_ENABLED
