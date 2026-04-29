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
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

#ifndef AT_POKEY_H
#define AT_POKEY_H

#ifdef _MSC_VER
	#pragma once
#endif

#include <vd2/system/function.h>
#include <vd2/system/refcount.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>
#include <at/atcore/scheduler.h>

class IATAudioOutput;
class ATPokeyEmulator;
class ATSaveStateReader;
class ATAudioFilter;
struct ATPokeyTables;
class ATPokeyRenderer;
class ATConsoleOutput;
class IATObjectState;

class IATPokeyEmulatorConnections {
public:
	virtual void PokeyAssertIRQ(bool cpuBased) = 0;
	virtual void PokeyNegateIRQ(bool cpuBased) = 0;
	virtual void PokeyBreak() = 0;
	virtual bool PokeyIsInInterrupt() const = 0;
	virtual bool PokeyIsKeyPushOK(uint8 scanCode, bool cooldownExpired) const = 0;
};

class IATPokeySIODevice {
public:
	virtual void PokeyAttachDevice(ATPokeyEmulator *pokey) = 0;
	
	virtual void PokeyBeginWriteSIO(uint8 c, bool command, uint32 cyclesPerBit) = 0;

	// Returns true if burst I/O is allowed.
	virtual bool PokeyWriteSIO(uint8 c, bool command, uint32 cyclesPerBit, uint64 startTime, bool framingError, bool truncated) = 0;

	virtual void PokeyBeginCommand() = 0;
	virtual void PokeyEndCommand() = 0;
	virtual void PokeySerInReady() = 0;
	virtual void PokeySetBreak(bool enabled) = 0;
};

class IATPokeyCassetteDevice {
public:
	virtual void PokeyChangeSerialRate(uint32 divisor) = 0;
	virtual void PokeyResetSerialInput() = 0;
	virtual void PokeyBeginCassetteData(uint8 skctl) = 0;
	virtual bool PokeyWriteCassetteData(uint8 c, uint32 cyclesPerBit) = 0;
	virtual void PokeyChangeForceBreak(bool enabled) = 0;
};

class IATPokeyTraceOutput {
public:
	virtual void AddIRQ(uint64 start, uint64 end) = 0;
};

struct ATPokeyRegisterState {
	uint8 mReg[0x20];
};

struct ATPokeyAudioState {
	int		mChannelOutputs[4];
};

struct ATPokeyAudioLog {
	// Sampling buffer -- receives per-channel output state every N ticks, up to the given max
	// number of samples per frame. Automatically cleared per frame.
	ATPokeyAudioState	*mpStates;
	uint32	mMaxSamples;
	uint32	mCyclesPerSample;

	// Mixed sampling buffer -- receives combined output state every sample, up to the given max
	// samples per frame. This buffer is NOT automatically cleared and must be manually retriggered.
	float	*mpMixedSamples;
	uint32	mMaxMixedSamples;

	// === filled in by audio engine ===
	uint32	mFullScaleValue;
	uint32	mTicksPerSample;
	uint32	mLastFrameSampleCount;
	uint32	mNumMixedSamples;

	// === for continuous use by audio engine ===
	uint32	mStartingAudioTick;
	uint32	mLastAudioTick;
	uint32	mAccumulatedAudioTicks;
	uint32	mSampleIndex;
	uint32	mLastOutputMask;
};

class ATPokeyEmulator final : public IATSchedulerCallback {
public:
	ATPokeyEmulator(bool isSlave);
	~ATPokeyEmulator();

	void	Init(IATPokeyEmulatorConnections *mem, ATScheduler *sched, IATAudioOutput *output, ATPokeyTables *tables);
	void	ColdReset();

	bool	IsStereoEnabled() const;
	void	SetSlave(ATPokeyEmulator *slave);
	void	SetCassette(IATPokeyCassetteDevice *dev);
	void	SetAudioLog(ATPokeyAudioLog *log);
	void	SetConsoleOutput(ATConsoleOutput *output);

	void	Set5200Mode(bool enable);

	bool	IsTraceSIOEnabled() const { return mbTraceSIO; }
	void	SetTraceSIOEnabled(bool enable) { mbTraceSIO = enable; }

	void	AddSIODevice(IATPokeySIODevice *device);
	void	RemoveSIODevice(IATPokeySIODevice *device);

	void	ReceiveSIOByte(uint8 byte, uint32 cyclesPerBit, bool simulateInputPort, bool allowBurst, bool synchronous, bool forceFramingError);
	void	SetSERIN(uint8 v) { mSERIN = v; }

	void	SetAudioLine2(int v);		// used for audio from motor control line
	void	SetDataLine(bool newState, uint64 flipTime = ~uint64(0));
	void	SetCommandLine(bool newState);
	void	SetSpeaker(bool newState);
	void	SetStereoSoftEnable(bool enable);

	bool	IsStereoAsMonoEnabled() const { return mbStereoAsMono; }
	void	SetStereoAsMonoEnabled(bool enable);

	void	SetExternalSerialClock(uint32 basetime, uint32 period);
	uint32	GetSerialCyclesPerBitSend() const;
	uint32	GetSerialCyclesPerBitRecv() const;
	uint32	GetSerialBidirectionalClockPeriod() const;
	uint32	GetSerialInputResetCounter() const { return mSerialInputResetCounter; }

	bool	IsChannelEnabled(uint32 channel) const;
	void	SetChannelEnabled(uint32 channel, bool enabled);

	bool	IsSecondaryChannelEnabled(uint32 channel) const;
	void	SetSecondaryChannelEnabled(uint32 channel, bool enabled);

	bool	IsNonlinearMixingEnabled() const { return mbNonlinearMixingEnabled; }
	void	SetNonlinearMixingEnabled(bool enable);

	bool	IsSpeakerFilterEnabled() const;
	bool	IsSpeakerFilterSupported() const;
	void	SetSpeakerFilterEnabled(bool enable);
	void	SetSpeakerFilterSupported(bool enable);

	void	SetSpeakerVolumeOverride(float vol);

	bool	IsSerialNoiseEnabled() const { return mbSerialNoiseEnabled; }
	void	SetSerialNoiseEnabled(bool enable) { mbSerialNoiseEnabled = enable; }

	bool	GetShiftKeyState() const { return mbShiftKeyState; }
	void	SetShiftKeyState(bool down, bool immediate);
	bool	GetControlKeyState() const { return mbControlKeyState; }
	void	SetControlKeyState(bool down);
	void	ClearKeyQueue();
	void	PushKey(uint8 c, bool repeat, bool allowQueue = false, bool flushQueue = true, bool useCooldown = true);
	uint64	GetRawKeyMask() const;
	void	PushRawKey(uint8 c, bool immediate);
	void	ReleaseRawKey(uint8 c, bool immediate);
	void	ReleaseAllRawKeys(bool immediate);
	void	SetBreakKeyState(bool down, bool immediate);
	void	PushBreak();

	void	SetKeyMatrix(const bool matrix[64]);

	static constexpr int kPotHiPosUnconnected = 0x7FFFFFFE;
	static constexpr int kPotHiPosGrounded = 0x7FFFFFFF;

	// Set a pot input position. This value is a hires position consisting of
	// the normal pot position shifted left by 16 bits, from 1 << 16 to
	// 228 << 16. There are two other special values:
	//
	//	Unconnected:
	//		No input connection. Capacitors don't charge or discharge.
	//
	//	Grounded:
	//		Input is being actively pulled down, forcing the capacitors to
	//		discharge even if fast pot scan mode is enabled.
	//
	void	SetPotPosHires(unsigned idx, int hiPos);

	// Get/set immediate pot mode. Immediate pot mode allows the POT0-7 registers
	// to update within a frame of the last pot scan triggered via POTGO. This
	// fibs accuracy slightly for reduction in latency.
	bool	IsImmediatePotUpdateEnabled() const { return mbAllowImmediatePotUpdate; }
	void	SetImmediatePotUpdateEnabled(bool enabled) { mbAllowImmediatePotUpdate = enabled; }

	void	AdvanceScanLine();
	void	AdvanceFrame(bool pushAudio, uint64 timestamp);

	sint32	ReadBackWriteRegister(uint8 reg) const;
	uint8	DebugReadByte(uint8 reg) const;
	uint8	ReadByte(uint8 reg);
	void	WriteByte(uint8 reg, uint8 value);

	void	DumpStatus(ATConsoleOutput& out);

	void	SaveState(IATObjectState **pp);
	void	LoadState(const IATObjectState *state);
	void	LoadState(const class ATSaveStatePokey& state);
	void	PostLoadState();

	void	GetRegisterState(ATPokeyRegisterState& state) const;

	// Returns a 32-bit fingerprint of POKEY's emulation-relevant
	// internal state — poly counters, 15/64 kHz clock phase, timer
	// counters & borrows, AUDF/AUDC/AUDCTL, IRQ regs, SKCTL/SKSTAT,
	// serial in/out shift state, pot scan progress.  Tick-domain
	// fields are folded as deltas against the current scheduler tick
	// so the fingerprint is stable regardless of absolute scheduler
	// alignment.  Used by netplay to detect divergence in subsystem
	// state on the same frame it occurs, instead of waiting for it
	// to leak into RAM (which can take 20-30 frames during boot).
	uint32	GetNetplayDeterminismFingerprint() const;

	// Emit a single-line per-field dump of every value folded into
	// GetNetplayDeterminismFingerprint() for diagnostic logging.  Two
	// peers' lines can be diff'd to localize a POKEY divergence to a
	// specific field (counter, AUDF, IRQ, SKCTL, serial state, etc.)
	// instead of just seeing two different 32-bit hashes.  The buffer
	// is appended to (not cleared) so callers can prefix labels.
	void	DescribeNetplayDeterminismFingerprint(VDStringA& out) const;

	void	FlushAudio(bool pushAudio, uint64 timestamp);

	void	SetTraceOutput(IATPokeyTraceOutput *output);

	uint32	GetCyclesToTimerFire(uint32 ch) const;
	bool	IsSerialForceBreakEnabled() const;

	void	SetNotifyOnBiClockChange(vdfunction<void()> fn);

private:
	void	OnScheduledEvent(uint32 id) override;

	void	PostFrameUpdate(uint32 t);

	template<uint8 channel>
	void	FireTimer();

	uint32	UpdateLast15KHzTime();
	uint32	UpdateLast15KHzTime(uint32 t);
	uint32	UpdateLast64KHzTime();
	uint32	UpdateLast64KHzTime(uint32 t);

	void	UpdatePolyTime();

	void	OnSerialInputTick();
	void	OnSerialOutputTick();
	void	ResyncExternalOutputClock();
	bool	IsSerialOutputClockRunning() const;
	void	FlushSerialOutput();

	void	RecomputeAllowedDeferredTimers();

	template<int channel>
	void	RecomputeTimerPeriod();

	template<int channel>
	void	UpdateTimerCounter();

	void	SetupTimers(uint8 channels);
	void	FlushDeferredTimerEvents(int channel);
	void	SetupDeferredTimerEvents(int channel, uint32 t0, uint32 period);
	void	SetupDeferredTimerEventsLinked(int channel, uint32 t0, uint32 period, uint32 hit0, uint32 hiperiod, uint32 hilooffset);

	void	DumpStatus(ATConsoleOutput& out, bool isSlave);

	void	UpdateMixTable();

	void	UpdateKeyMatrix(int index, uint16 mask, uint16 state);
	void	UpdateEffectiveKeyMatrix();
	bool	CanPushKey(uint8 scanCode) const;
	void	TryPushNextKey();

	void	SetKeyboardModes(bool cooked, bool scanEnabled);
	void	UpdateKeyboardScanEvent();
	void	QueueKeyboardIRQ();
	void	AssertKeyboardIRQ();
	void	AssertBreakIRQ();
	void	AssertIrq(bool cpuBased);
	void	NegateIrq(bool cpuBased);

	void	ProcessReceivedSerialByte();
	void	SyncRenderers(ATPokeyRenderer *r);

	void	StartPotScan();
	void	UpdatePots(uint32 timeSkew);

	void	UpdateAddressDecoding();	
	void	NotifyForceBreak();

	template<unsigned T_Ch>
	int GetAUDFP1(uint32 t) const;

	template<unsigned T_Ch>
	int GetAUDFP1() const;

	template<unsigned T_Ch>
	void SetAUDFP1(int period);

	ATPokeyRenderer *mpRenderer;

	int		mTimerCounters[4];

	bool	mbCommandLineState;
	bool	mbPal;
	bool	mb5200Mode;
	bool	mbTraceSIO;
	bool	mbNonlinearMixingEnabled;
	bool	mbSerialNoiseEnabled = true;

	uint8	mKBCODE;
	uint32	mKeyCodeTimer;
	uint32	mKeyCooldownTimer;
	bool	mbKeyboardIRQPending;
	bool	mbUseKeyCooldownTimer;
	bool	mbCookedKeyMode;
	bool	mbKeyboardScanEnabled;
	bool	mbShiftKeyState;
	bool	mbShiftKeyLatchedState;
	bool	mbControlKeyState;
	bool	mbControlKeyLatchedState;
	bool	mbBreakKeyState;
	bool	mbBreakKeyLatchedState;

	uint8	mAddressMask;
	uint8	mIRQEN;
	uint8	mIRQST;
	uint8	mAUDF[4];		// $D200/2/4/6: audio frequency, channel 1/2/3/4
	uint8	mAUDC[4];		// $D201/3/5/7: audio control, channel 1/2/3/4
	uint8	mAUDCTL;		// $D208
							// bit 7: use 9-bit poly instead of 17-bit poly
							// bit 6: clock channel 1 with 1.79MHz instead of 64KHz
							// bit 5: clock channel 3 with 1.79MHz instead of 64KHz
							// bit 4: clock channel 2 with channel 1 instead of 64KHz
							// bit 3: clock channel 4 with channel 3 instead of 64KHz
							// bit 2: apply high pass filter to channel 1 using channel 3
							// bit 1: apply high pass filter to channel 2 using channel 4
							// bit 0: change 64KHz frequency to 15KHz
	uint8	mSERIN;			// $D20D: SERIN
	uint8	mSEROUT;		// $D20D: SEROUT
	uint8	mSKSTAT;		// $D20F: SKSTAT
							// bit 3: shift key depressed
							// bit 2: key depressed
	uint8	mSKCTL;			// $D20F: SKCTL
							// bit 3: shift key depressed
							// bit 2: key depressed

	ATPokeyRegisterState mState;

	// countdown timer values
	int		mAUDFP1A[4] {};		// AUDF values, plus 1 (we use these everywhere)
	int		mAUDFP1B[4] {};
	uint32	mAUDFP1Time[4] {};
	int		mCounter[4];
	int		mCounterBorrow[4];
	uint32	mTimerPeriod[4];
	uint32	mTimerFullPeriod[2];		// time for timer to count off 256 in linked mode (#1 and #3 only)

	mutable uint32	mLastPolyTime;
	mutable uint32	mPoly17Counter;
	mutable uint32	mPoly9Counter;
	uint64	mPolyShutOffTime;

	uint64	mSerialOutputStartTime;
	uint8	mSerialInputShiftRegister;
	uint8	mSerialOutputShiftRegister;
	uint8	mSerialInputCounter;
	uint8	mSerialOutputCounter;
	uint8	mSerialInputPendingStatus;
	bool	mbSerOutValid;
	bool	mbSerShiftValid;
	bool	mbSerClockPhase = false;		// Internal serial clock phase. false = init state
	bool	mbSerialOutputState;
	bool	mbSpeakerActive;
	bool	mbSerialRateChanged;
	bool	mbSerialWaitingForStartBit;
	bool	mbSerInBurstPendingIRQ1;
	bool	mbSerInBurstPendingIRQ2;
	bool	mbSerInBurstPendingData;
	bool	mbSerInDeferredLoad;
	bool	mbNotifyBiClockChange = false;
	uint32	mSerOutBurstDeadline;
	uint32	mSerialInputResetCounter = 0;

	uint32	mSerialSimulateInputBaseTime;
	uint32	mSerialSimulateInputCyclesPerBit;
	uint32	mSerialSimulateInputData;
	bool	mbSerialSimulateInputPort;

	uint32	mSerialExtBaseTime;
	uint32	mSerialExtPeriod;

	uint64	mSerialDataInFlipTime = ~(uint64)0;

	ATPokeyTables *mpTables = nullptr;

	// AUDCTL breakout
	bool	mbFastTimer1;
	bool	mbFastTimer3;
	bool	mbLinkedTimers12;
	bool	mbLinkedTimers34;
	bool	mbUse15KHzClock;

	bool	mbAllowDeferredTimer[4];

	uint32	mLast15KHzTime;
	uint32	mLast64KHzTime;

	ATEvent	*mpKeyboardIRQEvent;
	ATEvent	*mpKeyboardScanEvent;
	ATEvent	*mpAudioEvent;
	ATEvent	*mpResetTimersEvent;
	ATEvent	*mpResetTimers2Event;
	ATEvent *mpEventSerialInput;
	ATEvent *mpEventSerialOutput;
	ATEvent *mpEventResetTwoTones1 = nullptr;
	ATEvent *mpEventResetTwoTones2 = nullptr;
	ATEvent	*mpTimerBorrowEvents[4];

	bool	mbDeferredTimerEvents[4];
	uint32	mDeferredTimerStarts[4];
	uint32	mDeferredTimerPeriods[4];

	uint16	mKeyMatrix[8] = {};
	uint16	mEffectiveKeyMatrix[8] = {};

	ATScheduler *mpScheduler;

	IATPokeyEmulatorConnections *mpConn;
	IATPokeyTraceOutput *mpTraceOutput = nullptr;
	ATPokeyEmulator	*mpSlave;
	const bool	mbIsSlave;
	bool	mbIrqAsserted;

	IATAudioOutput *mpAudioOut = nullptr;
	ATConsoleOutput *mpConsoleOut = nullptr;

	typedef vdfastvector<IATPokeySIODevice *> Devices;
	Devices	mDevices;

	IATPokeyCassetteDevice *mpCassette = nullptr;

	vdfastdeque<uint8> mKeyQueue;

	uint8	mKeyScanState = 0;
	uint8	mKeyScanCode = 0;
	uint8	mKeyScanLatch = 0;

	// Hires pot position, as a POTn << 16 value, or one of the special
	// unconnected or grounded constants.
	uint32	mPotHiPositions[8] = {};

	// Capacitor charge levels. The pot counter stops updating on the cycle
	// when the charge level reaches threshold.

	static constexpr uint32 kPotChargeThreshold	= 0x40000000;	// ~2V
	static constexpr uint32 kPotChargeLimit		= 0x96666666;	// ~4.7V
	static constexpr uint32 kPotDischargeRate	= 0x03936c71;	// ~2V/10us

	uint32	mPotChargeLevels[8] {};

	// Charge rate/cycle when slow pot scan is active, or at any time during
	// fast pot scan. Units are charge/cycle.
	uint32	mPotChargeRates[8] {};

	// POT0-7 registers, after reaching threshold during scan or outside of
	// scan. Otherwise, these must be updated live.
	uint8	mPotLatches[8] = {};

	uint8	mPotGroundedMask = 0;			// mask POTn lines that are grounded
	uint8	mALLPOT = 0;					// architectural ALLPOT register, at last pot update
	uint8	mPotMasterCounter = 0;
	uint64	mPotLastScanTime = 0;			// cycle time of last write to POTGO
	uint32	mPotLastTimeFast = 0;
	uint32	mPotLastTimeSlow = 0;
	bool	mbPotScanActive = false;	// true if pot scan was active at last update

	bool	mbAllowImmediatePotUpdate = false;
	bool	mbStereoSoftEnable = true;
	bool	mbStereoAsMono = false;

	float	mSpeakerVolOverride = -1.0f;
	bool	mbSpeakerFilterEnabled = false;
	bool	mbSpeakerFilterSupported = false;

	bool	mTraceDirectionSend = false;
	uint32	mTraceByteIndex = 0;
	uint32	mTraceByteSum = 0;

	enum class TraceReceiveState : uint8 {
		None,
		Ack,
		AckCE
	} mTraceReceiveState = TraceReceiveState::None;

	uint8	mTraceCommandBuffer[5] {};

	bool	mbTraceIrqPending = false;
	uint64	mTraceIrqStart = 0;

	vdfunction<void()> mpNotifyBiClockChangeFn;
};

#endif
