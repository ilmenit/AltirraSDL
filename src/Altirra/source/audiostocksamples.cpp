//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2025 Avery Lee
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
#include <vd2/system/constexpr.h>
#include <vd2/system/vdstl_algorithm.h>
#include <at/ataudio/audiosamplepool.h>
#include <at/atcore/audiomixer.h>
#include "oshelper.h"
#include "resource.h"

///////////////////////////////////////////////////////////////////////////

void ATAudioRegisterStockSamples(ATAudioSamplePool& pool) {
	struct SampleSourceInfo {
		uint32 mResId;
		float mBaseVolume;
	};

	static constexpr auto dbToAmp = [](float db) {
		// dB is in power, so we need to convert it to linear amplitude. Every
		// 20dB is a factor of 10 in amplitude.
		constexpr float ln10 = 2.3025850929940456840179914546844f;

		return VDCxExp(db / 20.0f * ln10);
	};

	static constexpr SampleSourceInfo kSampleSources[]={
		{ IDR_DISK_SPIN,				0.05f	},
		{ IDR_TRACK_STEP,				0.4f	},
		{ IDR_TRACK_STEP_2,				0.8f	},
		{ IDR_TRACK_STEP_2,				0.8f	},
		{ IDR_TRACK_STEP_3,				0.4f	},
		{ IDR_SPEAKER_STEP,				1.0f	},
		{ IDR_1030RELAY,				1.0f	},
		{ IDR_PRINTER_1029_PIN,			0.2f	},
		{ IDR_PRINTER_1029_PLATEN,		0.1f	},
		{ IDR_PRINTER_1029_RETRACT,		0.1f	},
		{ IDR_PRINTER_1029_HOME,		0.2f	},
		{ IDR_PRINTER_1025_FEED,		0.05f	},
		{ IDR_PRINTER_1020_HEADMOVE,	dbToAmp(-18.0f) },
		{ IDR_PRINTER_1020_HEADREVERSE,	dbToAmp( -9.0f) },
		{ IDR_PRINTER_1020_PAPERFEED,	dbToAmp(-18.0f) },
		{ IDR_PRINTER_1020_PENDOWN,		dbToAmp(  0.0f) },
		{ IDR_PRINTER_1020_PENUP,		dbToAmp(-12.0f) },
	};

	vdfastvector<uint8> data;
	for(size_t i=0; i<vdcountof(kSampleSources); ++i) {
		ATLoadMiscResource(kSampleSources[i].mResId, data);

		size_t n = data.size() / sizeof(sint16);

		// special case
		if (i + 1 == kATAudioSampleId_DiskStep2H)
			n >>= 1;

		vdfastvector<sint16> data16(n);

		memcpy(data16.data(), data.data(), n*sizeof(sint16));

		pool.RegisterStockSample((ATAudioSampleId)(i + 1), data16, 63920.8f, kSampleSources[i].mBaseVolume);
	}
}

///////////////////////////////////////////////////////////////////////////
