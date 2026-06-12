//	Altirra - Atari 800/800XL/5200 emulator
//	POKEY audio monitor
//	Copyright (C) 2008-2011 Avery Lee
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
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#ifndef f_AT_AUDIOMONITOR_H
#define f_AT_AUDIOMONITOR_H

#include <at/ataudio/pokey.h>

class IATUIRenderer;

class ATAudioMonitor {
	ATAudioMonitor(const ATAudioMonitor&) = delete;
	ATAudioMonitor& operator=(const ATAudioMonitor&) = delete;
public:
	ATAudioMonitor();
	~ATAudioMonitor();

	// channelIndex: 0 = primary (P1), 1 = secondary (P2), 2/3 = PokeyMax quad
	// P3/P4. Selects which audio display/scope slot this monitor drives.
	void Init(ATPokeyEmulator *pokey, IATUIRenderer *uir, uint32 channelIndex);
	void Shutdown();

	void SetMixedSampleCount(uint32 len);

	uint8 Update(ATPokeyAudioLog **log, ATPokeyRegisterState **rstate);

protected:
	ATPokeyEmulator		*mpPokey;
	IATUIRenderer		*mpUIRenderer;
	uint32				mChannelIndex;

	ATPokeyAudioLog		mLog;
	ATPokeyRegisterState	mRegisterState;

	ATPokeyAudioState	mAudioStates[312];

	vdfastvector<float>	mMixedSampleBuffer;
};

#endif	// f_AT_AUDIOMONITOR_H
