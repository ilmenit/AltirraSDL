//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
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
#include <at/atcore/audiomixer.h>
#include <at/atcore/scheduler.h>
#include "printerbase.h"

ATPrinterSoundSource::ATPrinterSoundSource() {
	const float volume = 0.5f;

	mpCurrentEdgeBuffer = new ATSyncAudioEdgeBuffer;
	mpCurrentEdgeBuffer->mLeftVolume = volume;
	mpCurrentEdgeBuffer->mRightVolume = volume;

	mpNextEdgeBuffer = new ATSyncAudioEdgeBuffer;
	mpNextEdgeBuffer->mLeftVolume = volume;
	mpNextEdgeBuffer->mRightVolume = volume;
}

ATPrinterSoundSource::~ATPrinterSoundSource() {
	VDASSERT(!mpAudioMixer);
}

void ATPrinterSoundSource::Init(IATAudioMixer& mixer, ATScheduler& sch, const char *debugName) {
	VDASSERT(!mpAudioMixer);

	mpCurrentEdgeBuffer->mpDebugLabel = debugName;
	mpNextEdgeBuffer->mpDebugLabel = debugName;

	mpAudioMixer = &mixer;
	mpAudioMixer->AddSyncAudioSource(this);

	ATAudioGroupDesc desc;
	desc.mAudioMix = ATAudioMix::kATAudioMix_Other;
	desc.mbRemoveSupercededSounds = false;

	mpSoundGroup = mixer.GetSamplePlayer().CreateGroup(desc);

	mpScheduler = &sch;
}

void ATPrinterSoundSource::Shutdown() {
	mpSoundGroup = nullptr;

	if (mpAudioMixer) {
		mpAudioMixer->RemoveSyncAudioSource(this);
		mpAudioMixer = nullptr;
	}
}

void ATPrinterSoundSource::AddPinSound(uint32 t, int numPins) {
	uint32 playTime = t + 128;

	// don't keep readding events on the same cycle (only possible through debugger)
	if (mpSoundGroup) {
		const uint32 now = mpScheduler->GetTick();

		if (playTime >= now) {
			mpAudioMixer->GetSamplePlayer().AddSound(*mpSoundGroup, playTime - now, kATAudioSampleId_Printer1029Pin, 1.0f);
		} else {
			VDFAIL("Attempt to play printer pin sound in the past");
		}
	} else {
		if (mPinTimes.empty() || mPinTimes.back().mTime != playTime)
			mPinTimes.emplace_back(playTime, 1);
	}
}

void ATPrinterSoundSource::ScheduleSound(ATAudioSampleId sampleId, bool looping, float delay, float duration, float volume) {
	uint32 durationCycles = 0;

	if (looping) {
		durationCycles = VDRoundToInt32(duration * mpScheduler->GetRate().asDouble());
		if (durationCycles == 0)
			return;
	}

	uint32 delayCycles = VDRoundToInt32(delay * mpScheduler->GetRate().asDouble());

	auto& samplePlayer = mpAudioMixer->GetSamplePlayer();
	auto soundId = looping
		? samplePlayer.AddLoopingSound(*mpSoundGroup, delayCycles, sampleId, volume)
		: samplePlayer.AddSound(*mpSoundGroup, delayCycles, sampleId, volume);

	if (looping)
		samplePlayer.StopSound(soundId, mpScheduler->GetTick64() + durationCycles);
}

void ATPrinterSoundSource::EnablePlatenSound(bool enable) {
	if (enable) {
		if (mPlatenSoundId == ATSoundId{})
			mPlatenSoundId = mpAudioMixer->GetSamplePlayer().AddLoopingSound(*mpSoundGroup, 0, kATAudioSampleId_Printer1029Platen, 1.0f);
	} else {
		if (mPlatenSoundId != ATSoundId{}) {
			mpAudioMixer->GetSamplePlayer().StopSound(mPlatenSoundId);
			mPlatenSoundId = ATSoundId{};
		}
	}
}

void ATPrinterSoundSource::EnableRetractSound(bool enable) {
	if (enable) {
		if (mRetractSoundId == ATSoundId{})
			mRetractSoundId = mpAudioMixer->GetSamplePlayer().AddSound(*mpSoundGroup, 0, kATAudioSampleId_Printer1029Retract, 1.0f);
	} else {
		if (mRetractSoundId != ATSoundId{}) {
			// Do not change this to mPlatenSoundId when syncing from upstream:
			// test14 had that typo here. mRetractSoundId is created by this
			// function for the 1029 retract sample, while mPlatenSoundId is
			// owned by EnablePlatenSound(). Stopping the platen ID here can
			// kill a newly-started platen loop and leave mPlatenSoundId stale,
			// while the retract sound keeps playing without a valid handle.
			mpAudioMixer->GetSamplePlayer().StopSound(mRetractSoundId);
			mRetractSoundId = ATSoundId{};
		}
	}
}

void ATPrinterSoundSource::PlayHomeSound() {
	mRetractSoundId = mpAudioMixer->GetSamplePlayer().AddSound(*mpSoundGroup, 0, kATAudioSampleId_Printer1029Home, 1.0f);
}

bool ATPrinterSoundSource::RequiresStereoMixingNow() const {
	return false;
}

void ATPrinterSoundSource::WriteAudio(const ATSyncAudioMixInfo& mixInfo) {
	const uint32 baseTime = (uint32)mixInfo.mStartTime;
	const uint32 cycleWindow = mixInfo.mNumCycles;

	auto& currentEdges = mpCurrentEdgeBuffer->mEdges;
	auto& nextEdges = mpNextEdgeBuffer->mEdges;

	for(const auto& pinEvent : mPinTimes) {
		const uint32 startT = pinEvent.mTime;
		const uint32 stopT = startT + mPulseWidth;
		const uint32 relStartT = (uint32)(startT - baseTime);
		const uint32 relStopT = (uint32)(stopT - baseTime);
		const bool inWindowStart = relStartT < cycleWindow;
		const bool inWindowStop = relStopT < cycleWindow;

		VDASSERT(relStartT < cycleWindow * 2);

		auto& startEdge = (inWindowStart ? currentEdges : nextEdges).emplace_back();
		startEdge.mTime = startT;
		startEdge.mDeltaValue = (float)pinEvent.mNumPins;

		auto& stopEdge = (inWindowStop ? currentEdges : nextEdges).emplace_back();
		stopEdge.mTime = stopT;
		stopEdge.mDeltaValue = -(float)pinEvent.mNumPins;
	}

	mPinTimes.clear();

	if (!mpCurrentEdgeBuffer->mEdges.empty())
		mpAudioMixer->GetEdgePlayer().AddEdgeBuffer(mpCurrentEdgeBuffer);

	std::swap(mpCurrentEdgeBuffer, mpNextEdgeBuffer);
}
