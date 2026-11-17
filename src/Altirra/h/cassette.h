//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008 Avery Lee
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

#ifndef AT_CASSETTE_H
#define AT_CASSETTE_H

#ifdef _MSC_VER
	#pragma once
#endif

#include <optional>
#include <at/atcore/audiosource.h>
#include <at/atcore/enumparse.h>
#include <at/atcore/deferredevent.h>
#include <at/atcore/scheduler.h>
#include <at/atcore/devicesio.h>
#include <at/atio/cassetteimage.h>
#include <at/ataudio/pokey.h>

class VDFile;

class ATCPUEmulatorMemory;
class IVDRandomAccessStream;
class IATAudioMixer;
class IATCassetteImage;
class IATDevicePortManager;
struct ATTraceContext;
class ATTraceChannelTape;

enum class ATCassetteTurboDecodeAlgorithm : uint8;

enum ATCassetteTurboMode : uint8 {
	kATCassetteTurboMode_None,					// FSK only; turbo never enabled
	kATCassetteTurboMode_CommandControl,		// SIO command asserted enables turbo
	kATCassetteTurboMode_ProceedSense,			// Turbo is always enabled in parallel, sensed by SIO proceed
	kATCassetteTurboMode_InterruptSense,		// Turbo is always enabled in parallel, sensed by SIO interrupt
	kATCassetteTurboMode_KSOTurbo2000,			// Turbo is always enabled in parallel, sensed by joystick port 2 (PA7); motor PA6
	kATCassetteTurboMode_TurboD,				// Turbo is always enabled in parallel, sensed by joystick port 2 (PA4); motor conventional
	kATCassetteTurboMode_DataControl,			// SIO data output low enables turbo
	kATCassetteTurboMode_Always					// Turbo always enabled
};

AT_DECLARE_ENUM_TABLE(ATCassetteTurboMode);

enum ATCassettePolarityMode : uint8 {
	kATCassettePolarityMode_Normal,
	kATCassettePolarityMode_Inverted
};

AT_DECLARE_ENUM_TABLE(ATCassettePolarityMode);

enum class ATCassetteDirectSenseMode : uint8 {
	Normal,
	LowSpeed,
	HighSpeed,
	MaxSpeed
};

AT_DECLARE_ENUM_TABLE(ATCassetteDirectSenseMode);

class ATCassetteEmulator final
	: public IATSchedulerCallback
	, public IATPokeyCassetteDevice
	, public IATSyncAudioSource
	, public IATDeviceRawSIO
{
public:
	ATCassetteEmulator();
	~ATCassetteEmulator();

	IATCassetteImage *GetImage() const { return mpImage; }
	const wchar_t *GetPath() const { return mImagePath.c_str(); }
	bool IsImagePersistent() const { return mbImagePersistent; }
	bool IsImageDirty() const { return mbImageDirty; }

	float GetLength() const;
	float GetPosition() const;
	uint32 GetSampleLen() const { return mLength; }
	uint32 GetSamplePos() const { return mPosition; }

	// Return the number of cycles since the leading edge of the current sample.
	sint32 GetSampleCycleOffset() const;

	uint64 GetLastStopCycle() const { return mLastStopCycle; }
	float GetLastStopPosition() const;
	uint32 GetLastStopSamplePos() const { return mLastStopPosition; }

	void Init(ATPokeyEmulator *pokey, ATScheduler *sched, ATScheduler *slowsched, IATAudioMixer *mixer, ATDeferredEventManager *defmgr, IATDeviceSIOManager *sioMgr, IATDevicePortManager *portMgr);
	void Shutdown();
	void ColdReset();

	bool IsLoaded() const { return mpImage != nullptr; }
	bool IsStopped() const { return !mbPlayEnable && !mbRecordEnable; }
	bool IsPlayEnabled() const { return mbPlayEnable; }
	bool IsRecordEnabled() const { return mbRecordEnable; }
	bool IsPaused() const { return mbPaused; }
	bool IsMotorEnabled() const { return mbMotorEnable; }
	bool IsMotorRunning() const { return mbMotorRunning; }
	bool IsLoadDataAsAudioEnabled() const { return mbLoadDataAsAudio; }
	bool IsAutoRewindEnabled() const { return mbAutoRewind; }

	void LoadNew();
	void Load(const wchar_t *fn);
	void Load(IATCassetteImage *image, const wchar_t *path, bool persistent);
	void Unload();
	void SetImagePersistent(const wchar_t *fn);
	void SetImageClean();
	void SetImageDirty();

	void SetLoadDataAsAudioEnable(bool enable);
	void SetRandomizedStartEnabled(bool enable);
	void SetAutoRewindEnabled(bool enable) { mbAutoRewind = enable; }

	ATCassetteTurboDecodeAlgorithm GetTurboDecodeAlgorithm() const { return mTurboDecodeAlgorithm; }
	void SetTurboDecodeAlgorithm(ATCassetteTurboDecodeAlgorithm algorithm) { mTurboDecodeAlgorithm = algorithm; }

	bool GetFSKSpeedCompensationEnabled() const { return mbFSKSpeedCompensation; }
	void SetFSKSpeedCompensationEnabled(bool enabled) { mbFSKSpeedCompensation = enabled; }

	bool GetCrosstalkReductionEnabled() const { return mbCrosstalkReduction; }
	void SetCrosstalkReductionEnabled(bool enabled) { mbCrosstalkReduction = enabled; }

	void GetLoadOptions(ATCassetteLoadContext& ctx) const;

	ATCassetteDirectSenseMode GetDirectSenseMode() const { return mDirectSenseMode; }
	void SetDirectSenseMode(ATCassetteDirectSenseMode mode);

	bool IsTurboDecodingEnabled() const { return !mbFSKDecoderEnabled || mbPortMotorState; }

	ATCassetteTurboMode GetTurboMode() const { return mTurboMode; }
	void SetTurboMode(ATCassetteTurboMode turboMode);

	ATCassettePolarityMode GetPolarityMode() const;
	void SetPolarityMode(ATCassettePolarityMode mode);

	bool IsVBIAvoidanceEnabled() const;
	void SetVBIAvoidanceEnabled(bool enable);

	void SetNextVerticalBlankTime(uint64 t);

	void SetTraceContext(ATTraceContext *context);

	void Stop();
	void Play();
	void Record();
	void SetPaused(bool paused);
	void RewindToStart();

	void SeekToTime(float seconds);
	void SeekToBitPos(uint32 bitPos);
	void SkipForward(float seconds);

	uint32 OnPreModifyTape();
	void OnPostModifyTape(uint32 newPos);

	uint8 ReadBlock(uint16 bufadr, uint16 len, ATCPUEmulatorMemory *mpMem, float timeoutSeconds);
	uint8 WriteBlock(uint16 bufadr, uint16 len, ATCPUEmulatorMemory *mpMem);

	std::optional<bool> AutodetectBasicNeeded();
	
	ATTapeSlidingWindowCursor GetFSKSampleCursor() const;
	ATTapeSlidingWindowCursor GetFSKBitCursor(uint32 samplesPerHalfBit) const;

public:
	void OnScheduledEvent(uint32 id) override;

public:
	ATDeferredEvent PositionChanged;
	ATDeferredEvent PlayStateChanged;
	ATNotifyList<const vdfunction<void()> *> TapeChanging;
	ATNotifyList<const vdfunction<void()> *> TapeChanged;
	ATDeferredEvent TapePeaksUpdated;
	ATDeferredEvent TapeDirtyStateChanged;
	ATNotifyList<const vdfunction<void(uint32 /* startBitLeadingEdgePos */, uint32 /* stopBitSamplePos */, uint8 /* dataByte */, bool /* framingError */, uint32 /* cyclesPerHalfBit */)> *> ByteDecoded;

public:
	void PokeyChangeSerialRate(uint32 divisor) override;
	void PokeyResetSerialInput() override;
	void PokeyBeginCassetteData(uint8 skctl) override;
	bool PokeyWriteCassetteData(uint8 c, uint32 cyclesPerBit) override;
	void PokeyChangeForceBreak(bool enabled) override;

public:
	bool RequiresStereoMixingNow() const override { return false; }
	void WriteAudio(const ATSyncAudioMixInfo& mixInfo) override;

public:
	void OnCommandStateChanged(bool asserted) override;
	void OnMotorStateChanged(bool asserted) override;
	void OnReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) override;
	void OnSendReady() override;

private:
	void UnloadInternal();
	void UpdateRawSIODevice();
	void UpdateMotorState();
	void UpdateInvertData();
	void UpdateFSKDecoderEnabled();

	using SampleCursor = ATTapeSampleCursor;
	using SlidingWindowCursor = ATTapeSlidingWindowCursor;

	void ResetCursors();
	void UpdateDirectSense(sint32 posOffset);

	enum BitResult {
		kBR_NoOutput,
		kBR_ByteReceived,
		kBR_FramingError
	};

	BitResult ProcessBit();

	void StartAudio();
	void StopAudio();
	void SeekAudio(uint32 pos);
	void ResyncAudio();
	
	void FlushRecording(uint32 t, bool force);
	void UpdateRecordingPosition();

	void UpdateTraceState();
	void UpdateTracePosition();
	void UpdateDirectSenseParameters();
	void ScheduleNextPortTransition();

	uint32	mAudioPosition = 0;
	uint32	mAudioLength = 0;
	uint32	mPosition = 0;
	uint32	mLength = 0;
	uint32	mLastSampleOffset = 0;
	uint32	mJitterPRNG = 0;
	uint32	mJitterSeed = 0;

	SlidingWindowCursor mDirectCursor {};
	SlidingWindowCursor mBitCursor {};
	SampleCursor	mTurboCursor {};

	bool	mbLoadDataAsAudio = false;
	bool	mbAutoRewind = true;
	bool	mbMotorEnable = false;
	bool	mbMotorRunning = false;
	bool	mbPlayEnable = false;
	bool	mbRecordEnable = false;
	bool	mbPaused = false;
	bool	mbDataLineState = false;
	bool	mbOutputBit = false;
	bool	mbInvertData = false;
	bool	mbInvertTurboData = false;
	bool	mbFSKDecoderEnabled = true;
	bool	mbCommandAsserted = true;
	bool	mbFSKControlByCommandEnabled = false;
	bool	mbFSKControlByDataEnabled = false;

	enum class SIOPhase : int {
		WaitForStartBit,
		VerifyStartBit,
		SampleDataBit0,
		SampleDataBit1,
		SampleDataBit2,
		SampleDataBit3,
		SampleDataBit4,
		SampleDataBit5,
		SampleDataBit6,
		SampleDataBit7,
		VerifyStopBit
	};

	SIOPhase mSIOPhase = SIOPhase::WaitForStartBit;

	uint8	mDataByte = 0;
	uint8	mThresholdZeroBit = 0;
	uint8	mThresholdOneBit = 0;

	ATCassetteDirectSenseMode mDirectSenseMode {};
	uint8	mDirectSenseWindow = 0;
	uint8	mDirectSenseThreshold = 0;

	bool	mbDataBitEdge = false;		// True if we are waiting for the edge of a data bit, false if we are sampling.
	int		mDataBitCounter = 0;
	int		mDataBitHalfPeriod = 0;
	uint32	mStartBitPosition = 0;
	uint32	mAveragingPeriod = 0;

	bool	mbRandomizedStartEnabled = false;
	bool	mbVBIAvoidanceEnabled = false;
	uint64	mNextVBITime = 0;

	ATEvent *mpPlayEvent = nullptr;
	ATEvent *mpRecordEvent = nullptr;
	uint32	mRecordLastTime = 0;

	uint64	mLastStopCycle = 0;
	uint32	mLastStopPosition = 0;

	ATPokeyEmulator *mpPokey = nullptr;
	ATScheduler *mpScheduler = nullptr;
	ATScheduler *mpSlowScheduler = nullptr;
	IATAudioMixer *mpAudioMixer = nullptr;

	IATCassetteImage *mpImage = nullptr;
	VDStringW mImagePath;
	bool	mbImagePersistent = false;
	bool	mbImageDirty = false;

	ATCassetteWriteCursor mWriteCursor {};

	IATDeviceSIOManager *mpSIOMgr = nullptr;
	bool	mbRegisteredRawSIO = false;

	IATDevicePortManager *mpPortMgr = nullptr;
	int		mPortInput = -1;
	int		mPortOutput = -1;
	ATEvent *mpPortUpdateEvent = nullptr;
	bool	mbPortCurrentPolarity = false;
	bool	mbPortMotorState = false;
	uint32	mPortCurrentPosition = 0;

	ATCassetteTurboMode mTurboMode = kATCassetteTurboMode_None;
	bool	mbTurboProceedAsserted = false;
	bool	mbTurboInterruptAsserted = false;
	ATCassetteTurboDecodeAlgorithm mTurboDecodeAlgorithm {};

	bool	mbFSKSpeedCompensation = false;
	bool	mbCrosstalkReduction = false;

	struct AudioEvent {
		uint64	mStartTime64;
		uint64	mStopTime64;
		uint32	mPosition;
	};

	typedef vdfastvector<AudioEvent> AudioEvents;
	AudioEvents mAudioEvents;
	bool mbAudioEventOpen = false;

	ATTraceContext *mpTraceContext = nullptr;
	ATTraceChannelTape *mpTraceChannelFSK = nullptr;
	ATTraceChannelTape *mpTraceChannelTurbo = nullptr;
	bool mbTraceMotorRunning = false;
	bool mbTraceRecord = false;
	uint32 mLogIndex = 0;

	// Slightly weird optional trinary (we may have cached that we don't know....)
	std::optional<std::optional<bool>> mNeedBasic;
};

#endif
