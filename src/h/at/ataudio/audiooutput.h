//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2010 Avery Lee
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
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

#ifndef f_AT_AUDIOOUTPUT_H
#define f_AT_AUDIOOUTPUT_H

#ifdef _MSC_VER
#pragma once
#endif

#include <at/atcore/audiosource.h>
#include <at/atcore/audiomixer.h>

class IATUIRenderer;
class IATSyncAudioSource;
class IATAudioMixer;
class ATScheduler;
class ATAudioSamplePool;

class IATAudioTap {
public:
	virtual void WriteRawAudio(const float *left, const float *right, uint32 count, uint32 timestamp) = 0;
};

enum ATAudioApi {
	kATAudioApi_WaveOut,
	kATAudioApi_DirectSound,
	kATAudioApi_XAudio2,
	kATAudioApi_WASAPI,
	kATAudioApi_Auto,
	kATAudioApiCount
};

struct ATUIAudioStatus {
	int mUnderflowCount;
	int mOverflowCount;
	int mDropCount;
	int mMeasuredMin;
	int mMeasuredMax;
	int mTargetMin;
	int mTargetMax;
	double mIncomingRate;
	double mExpectedRate;
	double mSamplingRate;
	bool mbStereoMixing;
};

class IATAudioOutput {
public:
	virtual ~IATAudioOutput() = default;

	virtual void Init(ATScheduler& scheduler) = 0;

	// Create the native audio device. This must be done before writing audio. This is a separate call
	// to allow the audio engine to be pre-configured and only init the native audio once afterward, since
	// doing so is expensive time-wise.
	virtual void InitNativeAudio() = 0;

	virtual ATAudioApi GetApi() = 0;
	virtual void SetApi(ATAudioApi api) = 0;

	virtual void SetAudioTap(IATAudioTap *tap) = 0;

	virtual ATUIAudioStatus GetAudioStatus() const = 0;

	virtual IATAudioMixer& AsMixer() = 0;
	virtual ATAudioSamplePool& GetPool() = 0;

	virtual void SetCyclesPerSecond(double cps, double repeatfactor) = 0;

	virtual bool GetMute() = 0;
	virtual void SetMute(bool mute) = 0;

	virtual float GetVolume() = 0;
	virtual void SetVolume(float vol) = 0;

	virtual float GetMixLevel(ATAudioMix mix) const = 0;
	virtual void SetMixLevel(ATAudioMix mix, float level) = 0;

	virtual int GetLatency() = 0;
	virtual void SetLatency(int ms) = 0;

	virtual int GetExtraBuffer() = 0;
	virtual void SetExtraBuffer(int ms) = 0;

	virtual void SetFiltersEnabled(bool enable) = 0;

	virtual void Pause() = 0;
	virtual void Resume() = 0;

	virtual void WriteAudio(
		const float *left,
		const float *right,
		uint32 count, bool pushAudio, bool pushStereoAsAudio, uint64 timestamp) = 0;

	// Returns the current estimate of total pipeline depth in bytes
	// (everything we've committed that has not yet been played).  The
	// returned value is in the audio engine's output format (S16 stereo
	// at GetAudioStatus().mSamplingRate), i.e. 4 bytes per frame.
	//
	// Returns 0xFFFFFFFFu (UINT32_MAX) if the implementation does not
	// track this quantity.  The Windows backends do not override this;
	// they have no consumer of the signal, because Windows Altirra does
	// not do active clock recovery.  The SDL3 backend overrides it to
	// return the same quantity that drives its internal rate-control
	// check, so the SDL3 frame pacer can use it for closed-loop sync
	// (see main_pacer.cpp).
	virtual uint32 GetPipelineLatencyBytes() { return 0xFFFFFFFFu; }
};

IATAudioOutput *ATCreateAudioOutput();

#endif	// f_AT_AUDIOOUTPUT_H
