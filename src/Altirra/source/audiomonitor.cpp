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

#include <stdafx.h>
#include "audiomonitor.h"
#include "uirender.h"

ATAudioMonitor::ATAudioMonitor()
	: mpPokey(NULL)
	, mpUIRenderer(NULL)
	, mChannelIndex(0)
{
}

ATAudioMonitor::~ATAudioMonitor() {
	Shutdown();
}

void ATAudioMonitor::Init(ATPokeyEmulator *pokey, IATUIRenderer *uir, uint32 channelIndex) {
	mpPokey = pokey;
	mpUIRenderer = uir;
	mChannelIndex = channelIndex;

	memset(mAudioStates, 0, sizeof mAudioStates);

	mLog = {};
	mLog.mpStates = mAudioStates;
	mLog.mMaxSamples = vdcountof(mAudioStates);
	mLog.mCyclesPerSample = 114;

	pokey->SetAudioLog(&mLog);
	uir->SetAudioMonitor(mChannelIndex, this);
}

void ATAudioMonitor::Shutdown() {
	if (mpPokey) {
		mpPokey->SetAudioLog(NULL);
		mpPokey = NULL;
	}

	if (mpUIRenderer) {
		mpUIRenderer->SetAudioMonitor(mChannelIndex, NULL);
		mpUIRenderer = NULL;

	}
}

void ATAudioMonitor::SetMixedSampleCount(uint32 len) {
	mMixedSampleBuffer.resize(len);

	mLog.mpMixedSamples = mMixedSampleBuffer.data();

	if (mLog.mNumMixedSamples > len)
		mLog.mNumMixedSamples = len;

	mLog.mMaxMixedSamples = len;
}

uint8 ATAudioMonitor::Update(ATPokeyAudioLog **log, ATPokeyRegisterState **rstate) {
	mpPokey->GetRegisterState(mRegisterState);

	*log = &mLog;
	*rstate = &mRegisterState;

	uint8 chanMask = 0;

	for(int i=0; i<4; ++i) {
		if (mpPokey->IsChannelEnabled(i))
			chanMask |= (1 << i);
	}

	return chanMask;
}
