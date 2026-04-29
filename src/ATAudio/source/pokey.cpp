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

#include <stdafx.h>
#include <float.h>
#include <math.h>
#include <vd2/system/bitmath.h>
#include <vd2/system/binary.h>
#include <vd2/system/math.h>
#include <vd2/system/vdstl_algorithm.h>
#include <at/atcore/consoleoutput.h>
#include <at/atcore/logging.h>
#include <at/atcore/serialization.h>
#include <at/atcore/sioutils.h>
#include <at/atcore/snapshotimpl.h>
#include <at/atcore/wraptime.h>
#include <at/ataudio/audiofilters.h>
#include <at/ataudio/audiooutput.h>
#include <at/ataudio/pokey.h>
#include <at/ataudio/pokeyrenderer.h>
#include <at/ataudio/pokeytables.h>
#include <at/ataudio/pokeysavestate.h>

ATLogChannel g_ATLCSIOData(false, false, "SIODATA", "Serial I/O bus data");

#ifdef _DEBUG
	ATLogChannel g_ATLCPOKEYTI(false, false, "POKEYTI", "POKEY timer internal events");

//	#define ATLOGPOKEYTI(msg, ...) printf("%08X | " msg, mpScheduler->GetTick(), __VA_ARGS__)
	#define ATLOGPOKEYTI(msg, ...) if (!g_ATLCPOKEYTI.IsEnabled()) {} else (g_ATLCPOKEYTI(msg, __VA_ARGS__))
#else
	#define ATLOGPOKEYTI(msg, ...) ((void)0)
#endif

namespace {
	enum : uint32 {
		kATPokeyEventKeyboardIRQ = 1,
		kATPokeyEventKeyboardScan,
		kATPokeyEventTimer1Borrow,
		kATPokeyEventTimer2Borrow,
		kATPokeyEventTimer3Borrow,
		kATPokeyEventTimer4Borrow,

		// There are two separate events for this because both timers 1 and 2 can initiate this
		// reset in close proximity, and we need both events to effectively take place.
		kATPokeyEventResetTwoTones1,
		kATPokeyEventResetTwoTones2,

		kATPokeyEventResetTimers,
		kATPokeyEventResetTimers2,
		kATPokeyEventSerialOutput,
		kATPokeyEventSerialInput,
	};

	const bool kForceActiveTimers = false;
}

ATPokeyEmulator::ATPokeyEmulator(bool isSlave)
	: mpRenderer(new ATPokeyRenderer)
	, mbCommandLineState(false)
	, mb5200Mode(false)
	, mbTraceSIO(false)
	, mbNonlinearMixingEnabled(true)
	, mKBCODE(0)
	, mKeyCodeTimer(0)
	, mKeyCooldownTimer(0)
	, mbKeyboardIRQPending(false)
	, mbUseKeyCooldownTimer(true)
	, mbCookedKeyMode(false)
	, mbKeyboardScanEnabled(false)
	, mbShiftKeyState(false)
	, mbShiftKeyLatchedState(false)
	, mbControlKeyState(false)
	, mbControlKeyLatchedState(false)
	, mbBreakKeyState(false)
	, mbBreakKeyLatchedState(false)
	, mAddressMask(0x0f)
	, mIRQEN(0)
	, mIRQST(0)
	, mAUDCTL(0)
	, mSERIN(0)
	, mSEROUT(0)
	, mSKSTAT(0)
	, mSKCTL(0)
	, mLastPolyTime(0)
	, mPoly17Counter(0)
	, mPoly9Counter(0)
	, mPolyShutOffTime(0)
	, mSerialOutputStartTime(0)
	, mSerialInputShiftRegister(0)
	, mSerialOutputShiftRegister(0)
	, mSerialInputCounter(0)
	, mSerialOutputCounter(0)
	, mbSerOutValid(false)
	, mbSerShiftValid(false)
	, mbSerialOutputState(false)
	, mbSpeakerActive(false)
	, mbSerialRateChanged(false)
	, mbSerialWaitingForStartBit(true)
	, mbSerInBurstPendingIRQ1(false)
	, mbSerInBurstPendingIRQ2(false)
	, mbSerInBurstPendingData(false)
	, mbSerInDeferredLoad(false)
	, mSerialSimulateInputBaseTime(0)
	, mSerialSimulateInputCyclesPerBit(0)
	, mSerialSimulateInputData(0)
	, mbSerialSimulateInputPort(false)
	, mSerialExtBaseTime(0)
	, mSerialExtPeriod(0)
	, mbFastTimer1(false)
	, mbFastTimer3(false)
	, mbLinkedTimers12(false)
	, mbLinkedTimers34(false)
	, mbUse15KHzClock(false)
	, mLast15KHzTime(0)
	, mLast64KHzTime(0)
	, mpKeyboardScanEvent(NULL)
	, mpKeyboardIRQEvent(NULL)
	, mpResetTimersEvent(NULL)
	, mpResetTimers2Event(NULL)
	, mpEventSerialInput(NULL)
	, mpEventSerialOutput(NULL)
	, mpScheduler(NULL)
	, mpConn(NULL)
	, mpSlave(NULL)
	, mbIsSlave(isSlave)
	, mbIrqAsserted(false)
	, mpConsoleOut(&g_ATConsoleOutputNull)
{
	memset(mpTimerBorrowEvents, 0, sizeof(mpTimerBorrowEvents));
}

ATPokeyEmulator::~ATPokeyEmulator() {
	delete mpRenderer;
}

void ATPokeyEmulator::Init(IATPokeyEmulatorConnections *mem, ATScheduler *sched, IATAudioOutput *output, ATPokeyTables *tables) {
	mpConn = mem;
	mpScheduler = sched;
	mpAudioOut = output;
	mpTables = tables;
	UpdateMixTable();

	mpRenderer->Init(sched, tables, output ? &output->AsMixer().GetEdgeSamplePlayer() : nullptr);
	mpRenderer->StartBlock();

	VDASSERT(!mpKeyboardScanEvent);
	VDASSERT(!mpKeyboardIRQEvent);

	for(int i=0; i<8; ++i) {
		mPotHiPositions[i] = kPotHiPosUnconnected;
		mPotChargeRates[i] = 0;
	}

	ColdReset();
}

void ATPokeyEmulator::ColdReset() {
	mKeyScanCode = 0;
	mKeyScanState = 0;
	mbKeyboardIRQPending = false;
	mbControlKeyLatchedState = false;
	mbShiftKeyLatchedState = false;
	mbBreakKeyLatchedState = false;

	memset(&mState, 0, sizeof mState);

	mKBCODE = 0;
	mSKSTAT = mbShiftKeyState ? 0x77 : 0x7F;
	mSKCTL = 0;
	mKeyCodeTimer = 0;
	mKeyCooldownTimer = 0;
	mIRQEN = 0;
	mIRQST = 0xF7;

	mAUDCTL = 0;
	mbFastTimer1 = false;
	mbFastTimer3 = false;
	mbLinkedTimers12 = false;
	mbLinkedTimers34 = false;
	mbUse15KHzClock = false;

	for(int i=0; i<4; ++i) {
		mCounter[i] = 1;
		mCounterBorrow[i] = 0;
		mAUDFP1A[i] = 1;
		mAUDFP1B[i] = 1;
		mAUDFP1Time[i] = 0;
		mbDeferredTimerEvents[i] = false;

		mpScheduler->UnsetEvent(mpTimerBorrowEvents[i]);
	}

	RecomputeTimerPeriod<0>();
	RecomputeTimerPeriod<1>();
	RecomputeTimerPeriod<2>();
	RecomputeTimerPeriod<3>();
	RecomputeAllowedDeferredTimers();

	mpScheduler->UnsetEvent(mpKeyboardScanEvent);
	mpScheduler->UnsetEvent(mpKeyboardIRQEvent);
	mpScheduler->UnsetEvent(mpResetTimersEvent);
	mpScheduler->UnsetEvent(mpResetTimers2Event);
	mpScheduler->UnsetEvent(mpEventSerialOutput);
	mpScheduler->UnsetEvent(mpEventSerialInput);
	mpScheduler->UnsetEvent(mpEventResetTwoTones1);
	mpScheduler->UnsetEvent(mpEventResetTwoTones2);

	mpRenderer->ColdReset();

	mLastPolyTime = ATSCHEDULER_GETTIME(mpScheduler);
	mPoly17Counter = 0;
	mPoly9Counter = 0;
	mSerialOutputStartTime = 0;
	mSerialInputShiftRegister = 0;
	mSerialOutputShiftRegister = 0;
	mSerialOutputCounter = 0;
	mSerialInputCounter = 0;
	mbSerOutValid = false;
	mbSerShiftValid = false;
	mbSerClockPhase = false;
	mbSerialOutputState = false;
	mbSerialWaitingForStartBit = true;
	mbSerInBurstPendingIRQ1 = false;
	mbSerInBurstPendingIRQ2 = false;
	mbSerInBurstPendingData = false;
	mSerOutBurstDeadline = 0;
	mbSerialSimulateInputPort = false;

	memset(mAUDF, 0, sizeof mAUDF);
	memset(mAUDC, 0, sizeof mAUDC);

	mLast15KHzTime = ATSCHEDULER_GETTIME(mpScheduler);
	mLast64KHzTime = ATSCHEDULER_GETTIME(mpScheduler);

	mALLPOT = 0;

	// On power-up, this can either be $00 or $E4 on XL/XE machines.
	// $00 is much more common.
	for(auto& v : mPotLatches)
		v = 0;

	for(auto& v : mPotChargeLevels)
		v = 0;

	mPotLastTimeFast = ATSCHEDULER_GETTIME(mpScheduler);
	mPotLastTimeSlow = mPotLastTimeFast;
	mbPotScanActive = false;

	if (mbCommandLineState) {
		mbCommandLineState = false;

		for(IATPokeySIODevice *dev : mDevices)
			dev->PokeyEndCommand();
	}

	if (mpSlave)
		mpSlave->ColdReset();

	NegateIrq(false);
	NotifyForceBreak();
}

bool ATPokeyEmulator::IsStereoEnabled() const {
	return mpSlave != nullptr;
}

void ATPokeyEmulator::SetSlave(ATPokeyEmulator *slave) {
	if (mpSlave)
		mpSlave->ColdReset();

	mpSlave = slave;

	UpdateAddressDecoding();

	if (mpSlave) {
		mpSlave->ColdReset();
		mpSlave->SyncRenderers(mpRenderer);

		// If we're hot-starting a slave, let's try to get it into
		// a somewhat reasonable state.

		static const uint8 kInitRegs[][2]={
			{ 0x00, 0xFF },		// AUDF1 = $00
			{ 0x01, 0xB0 },		// AUDC1 = $B0
			{ 0x02, 0xFF },		// AUDF2 = $00
			{ 0x03, 0xB0 },		// AUDC2 = $B0
			{ 0x04, 0xFF },		// AUDF3 = $00
			{ 0x05, 0xB0 },		// AUDC3 = $B0
			{ 0x06, 0xFF },		// AUDF4 = $00
			{ 0x07, 0xB0 },		// AUDC4 = $B0
			{ 0x08, 0x00 },		// AUDCTL = $00
			{ 0x0F, 0x03 },		// SKCTL = $03
		};

		for(const auto& data : kInitRegs)
			mpSlave->WriteByte(data[0], data[1]);
	}

	UpdateMixTable();
}

void ATPokeyEmulator::SetCassette(IATPokeyCassetteDevice *dev) {
	mpCassette = dev;
	mbSerialRateChanged = true;

	NotifyForceBreak();
}

void ATPokeyEmulator::SetAudioLog(ATPokeyAudioLog *log) {
	mpRenderer->SetAudioLog(log);
}

void ATPokeyEmulator::SetConsoleOutput(ATConsoleOutput *output) {
	mpConsoleOut = output ? output : &g_ATConsoleOutputNull;
}

void ATPokeyEmulator::Set5200Mode(bool enable) {
	if (mb5200Mode == enable)
		return;

	mb5200Mode = enable;
	UpdateKeyboardScanEvent();
}

void ATPokeyEmulator::AddSIODevice(IATPokeySIODevice *device) {
	mDevices.push_back(device);
	device->PokeyAttachDevice(this);
}

void ATPokeyEmulator::RemoveSIODevice(IATPokeySIODevice *device) {
	Devices::iterator it(std::find(mDevices.begin(), mDevices.end(), device));

	if (it != mDevices.end())
		mDevices.erase(it);
}

void ATPokeyEmulator::ReceiveSIOByte(uint8 c, uint32 cyclesPerBit, bool simulateInputPort, bool allowBurst, bool synchronous, bool forceFramingError) {
	if (cyclesPerBit && mbSerialNoiseEnabled) {
		const uint32 t = ATSCHEDULER_GETTIME(mpScheduler);

		uint32 pat = c + c + 0x200;
		pat ^= (pat + pat + 1);

		for (uint32 i = 0; i < 10; ++i) {
			if (pat & (1 << i))
				mpRenderer->AddSerialNoisePulse(t + cyclesPerBit * i);
		}
	}

	if (mbTraceSIO)
		(*mpConsoleOut)("POKEY: Receiving byte (c=%02X; %02X %02X) at %u cycles/bit (%.1f baud)", c, mSERIN, mSerialInputShiftRegister, cyclesPerBit, 7159090.0f / 4.0f / (float)cyclesPerBit);

	VDStringA sioDataInfo;

	if (g_ATLCSIOData) {
		if (mTraceByteIndex >= 1000 || mTraceDirectionSend) {
			mTraceByteIndex = 0;
			mTraceDirectionSend = false;
			mTraceReceiveState = TraceReceiveState::None;
		}

		mTraceByteSum += c;
		
		const char *extraMsg = "";

		if (mTraceByteIndex == 0) {
			if ((cyclesPerBit == 93 || cyclesPerBit == 94) && c == 0x41 && mTraceReceiveState == TraceReceiveState::None)
				mTraceReceiveState = TraceReceiveState::Ack;
		} else if (mTraceByteIndex == 1) {
			if ((c == 0x43 || c == 0x45) && mTraceReceiveState == TraceReceiveState::Ack) {
				mTraceReceiveState = TraceReceiveState::AckCE;
				mTraceByteSum = 0;
			}
		} else if (mTraceByteIndex == 130) {
			if (mTraceReceiveState == TraceReceiveState::AckCE) {
				const uint8 checksum = mTraceByteSum != c ? (uint8)((mTraceByteSum - c - 1) % 255 + 1) : (uint8)0;

				if (checksum == c)
					extraMsg = " (checksum OK)";
			}
		}

		sioDataInfo.sprintf("[%3u] Receive      < $%02X     (@ %u cpb / %.1f baud)%s"
			, mTraceByteIndex
			, c
			, cyclesPerBit
			, 7159090.0f / 4.0f / (float)cyclesPerBit
			, extraMsg
		);

		++mTraceByteIndex;
	}

	// check for attempted read in init mode (partial fix -- audio not emulated)
	if (!(mSKCTL & 3)) {
		if (mbTraceSIO)
			mpConsoleOut->WriteLine("POKEY: Dropping byte due to initialization mode.");

		if (g_ATLCSIOData) {
			sioDataInfo += " [dropped - init mode]\n";
			g_ATLCSIOData <<= sioDataInfo.c_str();
		}
		return;
	}

	mbSerialSimulateInputPort = simulateInputPort;

	if (simulateInputPort) {
		VDASSERT(cyclesPerBit);
		mSerialSimulateInputBaseTime = ATSCHEDULER_GETTIME(mpScheduler);
		mSerialSimulateInputCyclesPerBit = cyclesPerBit;
		mSerialSimulateInputData = ((uint32)c << 1) + 0x200;
	}

	if (!(mSKCTL & 0x30) && !mSerialExtPeriod) {
		if (mbTraceSIO)
			(*mpConsoleOut)("POKEY: Dropping byte $%02X due to external receive mode being used with no external clock (SKCTL=$%02X).", c, mSKCTL);

		if (g_ATLCSIOData) {
			sioDataInfo += " [dropped - external mode with no clock]";
			sioDataInfo += "\n";
			g_ATLCSIOData <<= sioDataInfo.c_str();
		}

		return;
	}

	mSerialInputPendingStatus = 0xff;

	if (forceFramingError) {
		mSerialInputPendingStatus &= 0x7f;
		sioDataInfo += " [framing error]";
	}

	// check for attempted read in synchronous mode; note that external clock mode is OK as presumably that
	// is synchronized
	if ((mSKCTL & 0x30) == 0x20 && !synchronous) {
		// set the framing error bit
		mSerialInputPendingStatus &= 0x7F;

		if (mbTraceSIO)
			(*mpConsoleOut)("POKEY: Trashing byte $%02X and signaling framing error due to asynchronous input mode not being enabled (SKCTL=$%02X).", c, mSKCTL);

		if (g_ATLCSIOData)
			sioDataInfo += " [garbled - receiving in synchronous mode]";

		// blown read -- trash the byte by faking a dropped bit
		c = (c & 0x0f) + ((c & 0xe0) >> 1) + 0x80;
	}

	// check for mismatched baud rate
	if (cyclesPerBit) {
		uint32 expectedCPB = GetSerialCyclesPerBitRecv();
		uint32 margin = (expectedCPB + 7) >> 3;

		if (cyclesPerBit < expectedCPB - margin || cyclesPerBit > expectedCPB + margin) {
			// blown read -- trash the byte and assert the framing error bit
			c = 0xFF;
			mSerialInputPendingStatus &= 0x7F;

			if (mbTraceSIO)
				(*mpConsoleOut)("POKEY: Signaling framing error due to receive rate mismatch (expected %d cycles/bit, got %d)", expectedCPB, cyclesPerBit);

			if (g_ATLCSIOData)
				sioDataInfo.append_sprintf(" [garbled - expected %u cycles/bit]", expectedCPB);
		}
	}

	if (!(mSKCTL & 0x30)) {
		if (mpEventSerialInput) {
			if (mbTraceSIO)
				(*mpConsoleOut)("POKEY: Interrupting send already in progress (%u cycles, %u bits left).", c, mpScheduler->GetTicksToEvent(mpEventSerialInput), mSerialInputCounter);
		}

		mpScheduler->SetEvent(mSerialExtPeriod, this, kATPokeyEventSerialInput, mpEventSerialInput);
	} else if (mSKCTL & 0x10) {
		// Restart timers 3 and 4 immediately.
		UpdateTimerCounter<2>();
		UpdateTimerCounter<3>();

		const uint32 t = mpScheduler->GetTick();
		mCounter[2] = GetAUDFP1<2>(t);
		mCounter[3] = GetAUDFP1<3>(t);

		mbSerialWaitingForStartBit = false;
		SetupTimers(0x0c);
	}

	if (g_ATLCSIOData) {
		sioDataInfo += '\n';
		g_ATLCSIOData <<= sioDataInfo.c_str();
	}

	mSerialInputShiftRegister = c;

	if (!(mSKCTL & 0x30))
		mSerialInputCounter = 9;
	else
		mSerialInputCounter = 19;

	// assert serial input busy
	mSKSTAT &= 0xfd;

	mbSerInDeferredLoad = simulateInputPort;
	
	if (allowBurst) {
		mbSerInBurstPendingData = true;
		mbSerInBurstPendingIRQ1 = true;
		mbSerInDeferredLoad = false;
	} else {
		mbSerInBurstPendingData = false;
		mbSerInBurstPendingIRQ1 = false;
	}

	mbSerInBurstPendingIRQ2 = false;
	mSerOutBurstDeadline = 0;

	if (!mbSerInDeferredLoad)
		ProcessReceivedSerialByte();
}

void ATPokeyEmulator::SetAudioLine2(int v) {
	mpRenderer->SetAudioLine2(v);
}

void ATPokeyEmulator::SetDataLine(bool newState, uint64 flipTime) {
	if (newState)
		mSKSTAT |= 0x10;
	else
		mSKSTAT &= ~0x10;

	mSerialDataInFlipTime = flipTime;
}

void ATPokeyEmulator::SetCommandLine(bool newState) {
	if (newState == mbCommandLineState)
		return;

	mTraceByteIndex = 0;

	if (mbTraceSIO)
		(*mpConsoleOut)("POKEY: %s command line.", newState ? "asserting" : "negating");

	mbCommandLineState = newState;
	if (newState) {
		for(Devices::const_iterator it(mDevices.begin()), itEnd(mDevices.end()); it!=itEnd; ++it)
			(*it)->PokeyBeginCommand();
	} else {
		for(Devices::const_iterator it(mDevices.begin()), itEnd(mDevices.end()); it!=itEnd; ++it)
			(*it)->PokeyEndCommand();
	}
}

void ATPokeyEmulator::SetSpeaker(bool newState) {
	if (mpRenderer->SetSpeaker(newState))
		mbSpeakerActive = true;
}

void ATPokeyEmulator::SetStereoSoftEnable(bool enable) {
	if (mbStereoSoftEnable != enable) {
		mbStereoSoftEnable = enable;

		if (mpSlave)
			UpdateAddressDecoding();
	}
}

void ATPokeyEmulator::SetStereoAsMonoEnabled(bool enable) {
	mbStereoAsMono = enable;
}

void ATPokeyEmulator::SetExternalSerialClock(uint32 basetime, uint32 period) {
	mSerialExtBaseTime = basetime;
	mSerialExtPeriod = period;

	if (!period) {
		mpScheduler->UnsetEvent(mpEventSerialInput);
		mpScheduler->UnsetEvent(mpEventSerialOutput);
	} else {
		ResyncExternalOutputClock();
	}
}

bool ATPokeyEmulator::IsChannelEnabled(uint32 channel) const {
	return mpRenderer->IsChannelEnabled(channel);
}

void ATPokeyEmulator::SetChannelEnabled(uint32 channel, bool enabled) {
	mpRenderer->SetChannelEnabled(channel, enabled);
}

bool ATPokeyEmulator::IsSecondaryChannelEnabled(uint32 channel) const {
	return !mpSlave || mpSlave->mpRenderer->IsChannelEnabled(channel);
}

void ATPokeyEmulator::SetSecondaryChannelEnabled(uint32 channel, bool enabled) {
	if (mpSlave)
		mpSlave->mpRenderer->SetChannelEnabled(channel, enabled);
}

void ATPokeyEmulator::SetNonlinearMixingEnabled(bool enable) {
	if (mbNonlinearMixingEnabled != enable) {
		mbNonlinearMixingEnabled = enable;

		UpdateMixTable();

		if (mpRenderer)
			mpRenderer->SetFiltersEnabled(enable);

		if (mpAudioOut)
			mpAudioOut->SetFiltersEnabled(enable);
	}
}

bool ATPokeyEmulator::IsSpeakerFilterEnabled() const {
	return mbSpeakerFilterEnabled;
}

bool ATPokeyEmulator::IsSpeakerFilterSupported() const {
	return mbSpeakerFilterSupported;
}

void ATPokeyEmulator::SetSpeakerFilterEnabled(bool enable) {
	if (mbSpeakerFilterEnabled != enable) {
		mbSpeakerFilterEnabled = enable;

		if (mbSpeakerFilterSupported)
			mpRenderer->SetSpeakerFilterEnabled(enable);
	}
}

void ATPokeyEmulator::SetSpeakerFilterSupported(bool enable) {
	if (mbSpeakerFilterSupported != enable) {
		mbSpeakerFilterSupported = enable;

		if (mbSpeakerFilterEnabled)
			mpRenderer->SetSpeakerFilterEnabled(enable);
	}
}

void ATPokeyEmulator::SetSpeakerVolumeOverride(float vol) {
	if (mSpeakerVolOverride != vol) {
		mSpeakerVolOverride = vol;
		UpdateMixTable();
	}
}

void ATPokeyEmulator::SetShiftKeyState(bool newState, bool immediate) {
	mbShiftKeyState = newState;

	// Shift key state can only change if keyboard scan is enabled. Debounce doesn't matter.
	if (immediate && (mSKCTL & 0x02)) {
		mbShiftKeyLatchedState = newState;

		if (newState)
			mSKSTAT &= ~0x08;
		else
			mSKSTAT |= 0x08;
	}

	// Shift is on the $10-17 row with the KR2 column
	UpdateKeyMatrix(2, 0x100, newState ? 0x100 : 0);
}

void ATPokeyEmulator::SetControlKeyState(bool newState) {
	mbControlKeyState = newState;

	// Control is on the $00-07 row with the KR2 column
	UpdateKeyMatrix(0, 0x100, newState ? 0x100 : 0);
}

void ATPokeyEmulator::ClearKeyQueue() {
	mKeyQueue.clear();
}

void ATPokeyEmulator::PushKey(uint8 c, bool repeat, bool allowQueue, bool flushQueue, bool useCooldown) {
	SetKeyboardModes(true, false);

	// Discard keys that are impossible due to key matrix conflicts.
	// Codes 0xC0-C7 and 0xD0-D7 cannot be produced due to a keyboard
	// matrix conflict when Ctrl+Shift is pressed and keys on 0x00-07/10-17.
	// They ARE possible with debounce disabled, but we don't support that
	// in the cooked key path.
	if ((c & 0xE8) == 0xC0)
		return;

	mbUseKeyCooldownTimer = useCooldown;

	if (allowQueue) {
		// Queue a key if we already have keys queued, or the OS can't accept one yet.
		if (!mKeyQueue.empty() || !CanPushKey(c)) {
			mKeyQueue.push_back(c);
			return;
		}
	} else if (flushQueue) {
		mKeyQueue.clear();
	}

	// If debounce or scan is disabled, drop the key.
	if ((mSKCTL & 3) != 3)
		return;

	mKBCODE = c;
	mSKSTAT &= ~0x04;

	if (!mKeyCodeTimer || !repeat)
		QueueKeyboardIRQ();

	mKeyCodeTimer = 1;
	mKeyCooldownTimer = 0;
}

uint64 ATPokeyEmulator::GetRawKeyMask() const {
	uint64 v = 0;

	for(int i=0; i<8; ++i)
		v += (uint64)(mKeyMatrix[i] & 0xFF) << (i*8);

	return v;
}

void ATPokeyEmulator::PushRawKey(uint8 c, bool immediate) {
	if (mb5200Mode)
		return;

	SetKeyboardModes(false, !immediate);
	mKeyQueue.clear();

	int row = (c >> 3) & 7;
	uint16 colbit = 1 << (c & 7);

	if (!immediate) {
		UpdateKeyMatrix(row, colbit, 0xFF);
	} else {
		// Stomp the entire main keyboard matrix with the newly pressed key (NOT the
		// extended matrix!). We need to update the effective matrix, too. Since we
		// only have one key active in the main matrix, it is not possible to introduce
		// phantoms in Ctrl/Shift/Break. It IS possible for Ctrl/Shift/Break to interfere
		// with the main matrix if two are held down; we handle Ctrl+Shift by a check on
		// the key code and ignore the Break conflict.
		for(int i=0; i<8; ++i)
			mKeyMatrix[i] &= 0xFF00;

		mKeyMatrix[row] |= colbit;

		memcpy(mEffectiveKeyMatrix, mKeyMatrix, sizeof mEffectiveKeyMatrix);

		// If we're in immediate mode, scan and debounce are enabled, and it's
		// not a key blocked by an inherent matrix conflict, push it now.
		if ((mSKCTL & 3) == 3 && (c & 0xE8) != 0xC0) {
			mSKSTAT &= ~0x04;
			mKBCODE = c;
		
			QueueKeyboardIRQ();
		}
	}
}

void ATPokeyEmulator::ReleaseRawKey(uint8 c, bool immediate) {
	if (mb5200Mode)
		return;

	SetKeyboardModes(false, !immediate);
	mKeyQueue.clear();

	const int row = (c >> 3) & 7;
	const uint16 colbit = 1 << (c & 7);
	if (!immediate) {
		UpdateKeyMatrix(row, colbit, 0);
	} else {
		if (mKeyMatrix[row] & colbit) {
			for(int i=0; i<8; ++i)
				mKeyMatrix[i] &= 0xFF00;

			memcpy(mEffectiveKeyMatrix, mKeyMatrix, sizeof mEffectiveKeyMatrix);

			mSKSTAT |= 0x04;
		}
	}
}

void ATPokeyEmulator::ReleaseAllRawKeys(bool immediate) {
	if (mb5200Mode)
		return;

	memset(mKeyMatrix, 0, sizeof mKeyMatrix);
	memset(mEffectiveKeyMatrix, 0, sizeof mEffectiveKeyMatrix);

	if (immediate) {
		mbKeyboardIRQPending = false;
		mSKSTAT |= 0x04;
	}

	SetKeyboardModes(mbCookedKeyMode, !immediate);
}

void ATPokeyEmulator::SetBreakKeyState(bool state, bool immediate) {
	if (mbBreakKeyState == state)
		return;

	mbBreakKeyState = state;

	if (immediate) {
		if (state)
			PushBreak();
	}

	// Break is on the $30-37 row with the KR2 column
	UpdateKeyMatrix(6, 0x100, state ? 0x100 : 0);
}

void ATPokeyEmulator::PushBreak() {
	mKeyQueue.clear();

	// The keyboard scan must be enabled for Break to be detected. However, debounce
	// doesn't matter.
	if (mSKCTL & 2)
		AssertBreakIRQ();
}

void ATPokeyEmulator::SetKeyMatrix(const bool matrix[64]) {
	if (matrix) {
		uint16 *dst = mKeyMatrix;
		const bool *src = matrix;

		for(int i=0; i<8; ++i) {
			uint16 v	= (src[0] ? 0x01 : 0x00)
						+ (src[1] ? 0x02 : 0x00)
						+ (src[2] ? 0x04 : 0x00)
						+ (src[3] ? 0x08 : 0x00)
						+ (src[4] ? 0x10 : 0x00)
						+ (src[5] ? 0x20 : 0x00)
						+ (src[6] ? 0x40 : 0x00)
						+ (src[7] ? 0x80 : 0x00);

			dst[i] = (dst[i] & 0xFF00) + v;
			src += 8;
		}
	} else
		memset(mKeyMatrix, 0, sizeof mKeyMatrix);

	UpdateEffectiveKeyMatrix();
}

template<uint8 activeChannel>
void ATPokeyEmulator::FireTimer() {
	mpRenderer->AddChannelEvent(activeChannel);

	if constexpr (activeChannel == 0) {
		if (mIRQEN & 0x01) {
			mIRQST &= ~0x01;
			AssertIrq(false);
		}

		// two tone
		if ((mSKCTL & 8) && mbSerialOutputState && !(mSKCTL & 0x80)) {
			mpScheduler->SetEvent(2, this, kATPokeyEventResetTwoTones1, mpEventResetTwoTones1);
		}
	}

	// count timer 2
	if constexpr (activeChannel == 1) {
		if (mIRQEN & 0x02) {
			mIRQST &= ~0x02;
			AssertIrq(false);
		}

		// two tone
		if (mSKCTL & 8) {
			mpScheduler->SetEvent(2, this, kATPokeyEventResetTwoTones2, mpEventResetTwoTones2);
		}

		if ((mSKCTL & 0x60) == 0x60) {
			mbSerClockPhase = !mbSerClockPhase;

			if (mbSerClockPhase && mSerialOutputCounter)
				mpScheduler->SetEvent(2, this, kATPokeyEventSerialOutput, mpEventSerialOutput);
		}
	}

	// count timer 4
	if constexpr (activeChannel == 3) {
		if (mIRQEN & 0x04) {
			mIRQST &= ~0x04;
			AssertIrq(false);
		}

		if (mSKCTL & 0x30)
			OnSerialInputTick();

		switch(mSKCTL & 0x60) {
			case 0x20:
			case 0x40:
				mbSerClockPhase = !mbSerClockPhase;

				if (mbSerClockPhase && mSerialOutputCounter)
					mpScheduler->SetEvent(2, this, kATPokeyEventSerialOutput, mpEventSerialOutput);

				break;
		}
	}
}

uint32 ATPokeyEmulator::UpdateLast15KHzTime() {
	return UpdateLast15KHzTime(ATSCHEDULER_GETTIME(mpScheduler));
}

uint32 ATPokeyEmulator::UpdateLast15KHzTime(uint32 t) {
	uint32 offset = t - mLast15KHzTime;

	if (offset >= 114)
		mLast15KHzTime += offset - offset % 114;

	return mLast15KHzTime;
}

uint32 ATPokeyEmulator::UpdateLast64KHzTime() {
	return UpdateLast64KHzTime(ATSCHEDULER_GETTIME(mpScheduler));
}

uint32 ATPokeyEmulator::UpdateLast64KHzTime(uint32 t) {
	uint32 offset = t - mLast64KHzTime;

	if (offset >= 28) {
		mLast64KHzTime += 28;
		offset -= 28;

		if (offset >= 28)
			mLast64KHzTime += offset - offset % 28;
	}

	return mLast64KHzTime;
}

void ATPokeyEmulator::UpdatePolyTime() {
	uint32 t = ATSCHEDULER_GETTIME(mpScheduler);
	int polyDelta = (int)(t - mLastPolyTime);
	mPoly9Counter += polyDelta;
	mPoly17Counter += polyDelta;
	mLastPolyTime = t;

	if (mPoly9Counter >= 511)
		mPoly9Counter %= 511;

	if (mPoly17Counter >= 131071)
		mPoly17Counter %= 131071;
}

void ATPokeyEmulator::OnSerialInputTick() {
	if (!mSerialInputCounter)
		return;

	--mSerialInputCounter;

	if (!mSerialInputCounter) {
		// deassert serial input active
		mSKSTAT |= 0x02;

		mbSerialWaitingForStartBit = true;

		if ((mSKCTL & 0x10) && !mbLinkedTimers34) {
			mCounter[2] = GetAUDFP1<2>();
			SetupTimers(0x04);
		}

		if (mbSerInDeferredLoad)
			ProcessReceivedSerialByte();
	} else {
		if (!(mSKCTL & 0x30) && mSerialExtPeriod)
			mpScheduler->SetEvent(mSerialExtPeriod, this, kATPokeyEventSerialInput, mpEventSerialInput);
	}
}

void ATPokeyEmulator::OnSerialOutputTick() {
	--mSerialOutputCounter;

	if (!mSerialOutputCounter) {
		FlushSerialOutput();

		if (mbSerOutValid) {
			mSerialOutputCounter = 10;
			mSerialOutputStartTime = mpScheduler->GetTick64();

			if (mSerOutBurstDeadline && ATSCHEDULER_GETTIME(mpScheduler) - mSerOutBurstDeadline >= uint32(0x80000000))
				mSerialOutputCounter = 1;

			mSerialOutputShiftRegister = mSEROUT;
			mbSerOutValid = false;
			mbSerShiftValid = true;

			if (mpCassette)
				mpCassette->PokeyBeginCassetteData(mSKCTL);

			const uint32 cyclesPerBit = GetSerialCyclesPerBitSend();
			for(IATPokeySIODevice *dev : mDevices)
				dev->PokeyBeginWriteSIO(mSerialOutputShiftRegister, mbCommandLineState, cyclesPerBit);

			// bit 3 is special and doesn't get cleared by IRQEN
			mIRQST |= 0x08;

			if (mIRQEN & 0x10)
				mIRQST &= ~0x10;
		} else
			mIRQST &= ~0x08;

		if (mIRQEN & ~mIRQST)
			AssertIrq(false);
		else
			NegateIrq(false);
	}

	// Update serial output line. The bit order is start bit (0), then data bits LSB to MSB,
	// then stop bit (1).
	mbSerialOutputState = mSerialOutputCounter > 9 ? false
		: mSerialOutputCounter > 1 ? (mSerialOutputShiftRegister & (1 << (9 - mSerialOutputCounter))) != 0
		: true;

	// check if we must reset the tick for external clock
	if (mSerialOutputCounter && !(mSKCTL & 0x60)) {
		if (mSerialExtPeriod)
			mpScheduler->SetEvent(mSerialExtPeriod, this, kATPokeyEventSerialOutput, mpEventSerialOutput);
		else
			mpScheduler->UnsetEvent(mpEventSerialOutput);
	}
}

// Start or recalc the serial output clock event, if the external clock is
// selected as the external clock and an external clock is present.
void ATPokeyEmulator::ResyncExternalOutputClock() {
	if (!(mSKCTL & 0x60) && mSerialExtPeriod) {
		// yup -- start external clock
		uint32 delay = (ATSCHEDULER_GETTIME(mpScheduler) + 2) - mSerialExtBaseTime;

		uint32 bitPeriod = mSerialExtPeriod * 2;

		if (delay >= 0x80000000) {
			delay = (0 - delay) % bitPeriod;

			delay = bitPeriod - delay;
		} else {
			delay %= bitPeriod;

			if (!delay)
				delay = bitPeriod;
		}

		mpScheduler->SetEvent(delay, this, kATPokeyEventSerialOutput, mpEventSerialOutput);
	}
}

bool ATPokeyEmulator::IsSerialOutputClockRunning() const {
	switch(mSKCTL & 0x60) {
		default:
			VDNEVERHERE;
		case 0x00:		// external clock
			return mSerialExtPeriod != 0;

		case 0x20:		// timer 4 as transmit clock
		case 0x40:
			if (mSKCTL & 0x10) {
				// Asynchronous receive mode enabled, so clock only runs if receiving. For now, pretend
				// it's always halted for output purposes.
				return false;
			}

			// check if initialization mode is active
			if (mSKCTL & 3) {
				// nope, clocks are running
				return true;
			}

			// init mode is active -- check if 3+4 are linked
			if (mAUDCTL & 0x08) {
				// linked -- running if timer 3 is 1.79MHz
				return (mAUDCTL & 0x20) != 0;
			} else {
				// not linked -- timer 4 is halted
				return false;
			}
			break;

		case 0x60:		// timer 2 as transmit clock
			// check if initialization mode is active
			if (mSKCTL & 3) {
				// nope, clocks are running
				return true;
			}

			// init mode is active -- check if 1+2 are linked
			if (mAUDCTL & 0x10) {
				// linked -- running if timer 1 is 1.79MHz
				return (mAUDCTL & 0x40) != 0;
			} else {
				// not linked -- timer 2 is halted
				return false;
			}
			break;
	}
}

void ATPokeyEmulator::FlushSerialOutput() {
	if (!mbSerShiftValid)
		return;

	const uint8 originalCounter = mSerialOutputCounter;

	mbSerShiftValid = false;
	mSerialOutputCounter = 0;

	// check if we got out the start bit; if not, no byte would be noticed by
	// receivers
	if (mSerialOutputCounter >= 9)
		return;

	const uint32 cyclesPerBit = GetSerialCyclesPerBitSend();

	if (mbTraceSIO) {
		(*mpConsoleOut)("POKEY: Transmitted serial byte %02X to SIO bus at %u cycles/bit (%.1f baud)"
			, mSerialOutputShiftRegister
			, cyclesPerBit
			, (7159090.0f / 4.0f) / (float)cyclesPerBit
			);
	}

	bool burstOK = false;
	bool framingError = false;
	bool truncated = false;
	uint8 c = mSerialOutputShiftRegister;

	if (mSerialOutputCounter) {
		// Byte may have been truncated -- adjust it and the stop bit. Transmission
		// is LSB first, so stomp from MSBs down.
		uint8 truncationMask = 0xFF << (9 - originalCounter);

		c &= truncationMask;

		if (mbSerialNoiseEnabled)
			c |= truncationMask;

		// signal framing error if output is still low by stop bit time
		framingError = !mbSerialOutputState;
		truncated = true;
	}

	if (g_ATLCSIOData) {
		if (mTraceByteIndex >= 1000 || !mTraceDirectionSend) {
			mTraceByteIndex = 0;
			mTraceDirectionSend = true;
		}

		const char *decodedCommand = "";

		if (mbCommandLineState && (cyclesPerBit == 93 || cyclesPerBit == 94) && mTraceByteIndex < 5 && !truncated) {
			mTraceCommandBuffer[mTraceByteIndex] = c;

			if (mTraceByteIndex == 4) {
				if (ATComputeSIOChecksum(mTraceCommandBuffer, 4) == c) {
					decodedCommand = ATDecodeSIOCommand(mTraceCommandBuffer[0], mTraceCommandBuffer[1], &mTraceCommandBuffer[2]);
				}
			}
		}

		g_ATLCSIOData(
			"[%3u] Send%s  $%02X >         (@ %u cpb / %.1f baud)%s%s%s%s\n",
			mTraceByteIndex++,
			mbCommandLineState ? "Cmd" : "   ",
			mSerialOutputShiftRegister,
			cyclesPerBit,
			7159090.0f / 4.0f / (float)cyclesPerBit,
			truncated ? " (TRUNCATED)" : "",
			*decodedCommand ? " [" : "",
			decodedCommand,
			*decodedCommand ? "]" : ""
		);
	}

	for(IATPokeySIODevice *dev : mDevices) {
		if (dev->PokeyWriteSIO(c, mbCommandLineState, cyclesPerBit, mSerialOutputStartTime, framingError, truncated))
			burstOK = true;
	}

	if (mpCassette)
		mpCassette->PokeyWriteCassetteData(mSerialOutputShiftRegister, cyclesPerBit);

	if (burstOK)
		mSerOutBurstDeadline = (ATSCHEDULER_GETTIME(mpScheduler) + cyclesPerBit*10) | 1;
	else
		mSerOutBurstDeadline = 0;
}

uint32 ATPokeyEmulator::GetSerialCyclesPerBitSend() const {
	uint32 cyclesPerBit = 1;
		
	switch(mSKCTL & 0x60) {
		default:
			VDNEVERHERE;
		case 0x00:		// external clock
			cyclesPerBit = mSerialExtPeriod;
			break;

		case 0x20:		// timer 4 as transmit clock
		case 0x40:
			cyclesPerBit = mTimerPeriod[3] * 2;
			break;

		case 0x60:		// timer 2 as transmit clock
			cyclesPerBit = mTimerPeriod[1] * 2;
			break;
	}

	return cyclesPerBit;
}

uint32 ATPokeyEmulator::GetSerialCyclesPerBitRecv() const {
	if (!(mSKCTL & 0x30))
		return mSerialExtPeriod;

	const uint32 divisor = mTimerPeriod[3];

	return divisor + divisor;
}

uint32 ATPokeyEmulator::GetSerialBidirectionalClockPeriod() const {
	if ((mSKCTL & 0x30) == 0x20)
		return mTimerPeriod[3];
	else
		return 0;
}

void ATPokeyEmulator::SetPotPosHires(unsigned idx, int hipos0) {
	uint32 hipos = (uint32)std::max<int>(hipos0, 1);

	const uint8 potBit = 1 << idx;

	if (mPotHiPositions[idx] == hipos)
		return;

	UpdatePots(0);

	mPotHiPositions[idx] = hipos;

	if (hipos == kPotHiPosGrounded) {
		mPotGroundedMask |= potBit;
		mPotChargeRates[idx] = 0;
		mPotChargeLevels[idx] = 0;

		// If the pot scan is occurring, update ALLPOT to indicate that the
		// input has dropped below threshold again.
		if (mbPotScanActive) {
			mALLPOT |= potBit;
		}
	} else {
		mPotGroundedMask &= ~potBit;

		if (hipos == kPotHiPosUnconnected) {
			mPotChargeRates[idx] = 0;
		} else {
			// Compute charge per cycle. Rescaling the high position by 114/65536 converts
			// it to a cycle count, which is then divided into the threshold value to
			// produce a power value in units of charge/cycle.

			static constexpr uint64 kChargePerHiPosPerCycle = ((uint64)kPotChargeThreshold << 16) / 114;

			mPotChargeRates[idx] = (uint32)std::min<uint64>(kPotChargeThreshold, kChargePerHiPosPerCycle / hipos);
		}
	}

	if (mbAllowImmediatePotUpdate) {
		// If this pot line has already latched and it's been less than one full frame since we
		// kicked off the pot scan, update the latch immediately. Since we are NTSC/PAL agnostic
		// here, use a conservative timeout that exceeds a PAL frame.

		if (mpScheduler->GetTick64() - mPotLastScanTime < 37000
			&& !(mALLPOT & (1 << idx)))
		{
			mPotLatches[idx] = (uint8)std::min<uint32>(229, mSKCTL & 4 ? (hipos * 114) >> 16 : hipos >> 16);
		}
	}
}

void ATPokeyEmulator::AdvanceScanLine() {
	if (mbSerialRateChanged) {
		mbSerialRateChanged = false;

		if (mpCassette) {
			uint32 divisor = GetSerialCyclesPerBitRecv() >> 1;

			mpCassette->PokeyChangeSerialRate(divisor);
		}
	}

	if (mpSlave)
		mpSlave->AdvanceScanLine();
}

void ATPokeyEmulator::AdvanceFrame(bool pushAudio, uint64 timestamp) {
	UpdatePots(0);

	if (mpSlave)
		mpSlave->UpdatePots(0);

	if (mKeyCodeTimer) {
		if (!--mKeyCodeTimer) {
			mSKSTAT |= 0x04;

			mKeyCooldownTimer = 60;
		}
	} else if (mbSpeakerActive) {
		mKeyCooldownTimer = 60;
	} else {
		if (mKeyCooldownTimer)
			--mKeyCooldownTimer;

		if (!mKeyQueue.empty() && CanPushKey(mKeyQueue.front()))
			TryPushNextKey();
	}

	mbSpeakerActive = false;

	FlushAudio(pushAudio, timestamp);

	const uint32 t = ATSCHEDULER_GETTIME(mpScheduler);

	PostFrameUpdate(t);

	if (mpSlave)
		mpSlave->PostFrameUpdate(t);
}

void ATPokeyEmulator::PostFrameUpdate(uint32 t) {
	mpRenderer->RestartAudioLog();

	// Scan all of the deferred timers and push any that are too far behind. We need to do this to
	// prevent any of the timers from getting too far behind the clock (>30 bits). This calculation
	// is wrong for the low timer of a linked timer pair, but it turns out we don't use the start
	// value in that case; this module uses the high timer delay, and only the renderer does the
	// funky linked step.

	for(int i=0; i<4; ++i) {
		if (!mbDeferredTimerEvents[i])
			continue;

		// Determine what kind of lag we want to check for on the deferred timer start. This must be
		// at least 8893, which is the number of times that a 1.79MHz frequency 0 timer can count in
		// a PAL frame. However, a slow 15KHz linked timer can take as long as 7.4M clocks to cycle.
		// We keep shifting up the value until it's a bit more than a second. We do need to keep this
		// value a multiple of the original, though.
		uint32 bigPeriod = mDeferredTimerPeriods[i];

		while(bigPeriod < 2097152) {
			bigPeriod <<= 4;
		}

		// The deferred timers may start in the future for one that hasn't hit the first tick,
		// so we must check for that case.
		if ((sint32)(t - mDeferredTimerStarts[i]) <= (sint32)bigPeriod)
			continue;

		// Bump the timer up by a number of periods so it catches up. 
		mDeferredTimerStarts[i] += bigPeriod;
	}

	// Catch up 15KHz and 64Khz clocks.
	UpdateLast15KHzTime();
	UpdateLast64KHzTime();

	// Catch up poly counters.
	UpdatePolyTime();

	// Catch up the external clock, to avoid glitches at 2^32
	if (mSerialExtPeriod) {
		uint32 extsince = t - mSerialExtBaseTime;

		if (extsince >= 0x40000000) {
			extsince += mSerialExtPeriod - 1;
			mSerialExtBaseTime += extsince - extsince % mSerialExtPeriod;
		}
	}

	// Catch up double-buffered frame periods
	static constexpr uint32 kMaxPeriodLag = 8192*1024;

	const uint32 minEffectiveTime = t - kMaxPeriodLag;
	for(int i=0; i<4; ++i) {
		// note that the effective time may be slightly in the future, so we
		// must use a (wrapped) signed comparison here
		if (ATWrapTime{mAUDFP1Time[i]} < minEffectiveTime)
			mAUDFP1Time[i] = minEffectiveTime;
	}
}

void ATPokeyEmulator::OnScheduledEvent(uint32 id) {
	switch(id) {
		case kATPokeyEventKeyboardIRQ:
			mpKeyboardIRQEvent = nullptr;

			if (mbKeyboardIRQPending) {
				mbKeyboardIRQPending = false;

				if (mSKCTL & 2)
					AssertKeyboardIRQ();
			}
			break;

		case kATPokeyEventKeyboardScan:
			{
				mpKeyboardScanEvent = nullptr;

				if ((mb5200Mode || mbKeyboardScanEnabled) && (mSKCTL & 2)) {
					mpKeyboardScanEvent = mpScheduler->AddEvent(114, this, kATPokeyEventKeyboardScan);

					const uint8 kc = mKeyScanCode++ & 0x3F;

					// POKEY's keyboard circuitry is a two-bit state machine with a keyboard line and
					// a comparator as input, and result/comparator latch signals as output. The state
					// machine works as follows:
					//
					//	state 0 (00): waiting for key
					//		key pressed -> state 1, load compare latch
					//
					//	state 1 (01): waiting for key bounce
					//		key pressed, not same as compare latch -> state 0
					//		key not pressed, same as compare latch -> state 0
					//		key pressed, same as compare latch -> state 2
					//
					//	state 2 (11): waiting for key release
					//		key not pressed, same as compare latch -> state 3
					//
					//	state 3 (10): waiting for key debounce
					//		key pressed, same as compare latch -> state 2
					//		key not pressed, same as compare latch -> state 0
					//
					// If keyboard debounce (SKCTL bit 1) is disabled, the compare signal is always
					// true. SKSTAT bit 2 reads 1 for states 0 and 1, and 0 for states 2 and 3.
					//
					// The state machine and the binary counter that run the external keyboard logic
					// both run at HBLANK (15KHz) rate; this logic therefore cycles through the entire
					// keyboard 245.3 times a second (NTSC), or 4.1 times a frame. It takes two HBLANKs
					// for a key to be recognized, and since keys are mirrored four times on the 5200
					// keypad, this causes IRQs to be sent 8 times a frame.

					switch(kc) {
						case 0x00:
							mbControlKeyLatchedState = (mEffectiveKeyMatrix[0] & 0x100) != 0;
							break;

						case 0x10:
							{
								const bool shiftState = (mEffectiveKeyMatrix[2] & 0x100) != 0;

								if (mbShiftKeyLatchedState != shiftState) {
									mbShiftKeyLatchedState = shiftState;

									if (shiftState)
										mSKSTAT &= ~0x08;
									else
										mSKSTAT |= 0x08;
								}
							}
							break;

						case 0x30:
							{
								const bool breakKeyState = (mEffectiveKeyMatrix[6] & 0x100) != 0;

								if (mbBreakKeyLatchedState != breakKeyState) {
									mbBreakKeyLatchedState = breakKeyState;

									if (breakKeyState)
										AssertBreakIRQ();
								}
							}
							break;
					}

					const bool keyState = (mEffectiveKeyMatrix[kc >> 3] & (1 << (kc & 7))) != 0;

					switch(mKeyScanState) {
						case 0:		// waiting for key
							if (keyState) {
								mKeyScanLatch = kc;
								mKeyScanState = 1;
							}
							break;

						case 1:		// waiting for key bounce
							if (keyState) {
								if (kc == mKeyScanLatch || !(mSKCTL & 1)) {
									// same key down -- fire IRQ, save key, and continue
									mKeyScanState = 2;
									mKBCODE = kc | (mbShiftKeyLatchedState ? 0x40 : 0x00) | (mbControlKeyLatchedState ? 0x80 : 0x00);
									mSKSTAT &= ~0x04;
									if (mIRQEN & 0x40) {
										// If keyboard IRQ is already active, set the keyboard overrun bit.
										if (!(mIRQST & 0x40))
											mSKSTAT &= ~0x40;
										else {
											mIRQST &= ~0x40;
											AssertIrq(false);
										}
									}						
								} else {
									// different key down -- restart
									mKeyScanState = 0;
								}
							} else if (kc == mKeyScanLatch || !(mSKCTL & 1)) {
								// latched key no longer down -- restart
								mKeyScanState = 0;
							}
							break;

						case 2:		// waiting for key up
							if (kc == mKeyScanLatch || !(mSKCTL & 1)) {
								if (!keyState) {
									mKeyScanState = 3;
								}
							}
							break;

						case 3:		// waiting for debounce
							if (kc == mKeyScanLatch || !(mSKCTL & 1)) {
								if (keyState)
									mKeyScanState = 2;
								else {
									mSKSTAT |= 0x04;
									mKeyScanState = 0;
								}
							}
							break;
					}
				}
			}
			break;

		case kATPokeyEventTimer1Borrow:
			mpTimerBorrowEvents[0] = NULL;

			// Check if there is a resync12 happening at the same time. If so, we need to suppress the
			// timer tick as the resync will take priority -- there is a gate that blocks the borrow
			// in that case. This only needs to occur if the borrow event is processed first, since
			// the order is ambiguous; if the two-tone resync event is processed first, it will clear
			// the borrow events and we won't get to this point.
			if ((!mpEventResetTwoTones1 || mpScheduler->GetTicksToEvent(mpEventResetTwoTones1) != 0)
				&& (!mpEventResetTwoTones2 || mpScheduler->GetTicksToEvent(mpEventResetTwoTones2) != 0))
			{
				FireTimer<0>();
			}

			mCounterBorrow[0] = 0;
			if (!mbLinkedTimers12) {
				mCounter[0] = GetAUDFP1<0>();
			} else {
				// If we are operating at 1.79MHz, three cycles have already elapsed from the underflow to
				// when the borrow goes through.
				mCounter[0] = mbFastTimer1 ? 253 : 256;

				// We need the timer 2 counter updated so SetupTimers() can detect if a timer 2 borrow
				// is happening, which affects interpretation of the timer 1 counter.
				UpdateTimerCounter<1>();
			}

			SetupTimers(0x01);
			break;

		case kATPokeyEventTimer2Borrow:
			mpTimerBorrowEvents[1] = NULL;

			// see Timer1Borrow version
			if ((!mpEventResetTwoTones1 || mpScheduler->GetTicksToEvent(mpEventResetTwoTones1) != 0)
				&& (!mpEventResetTwoTones2 || mpScheduler->GetTicksToEvent(mpEventResetTwoTones2) != 0))
			{
				FireTimer<1>();
			}

			mCounterBorrow[1] = 0;
			mCounter[1] = GetAUDFP1<1>();
			if (mbLinkedTimers12) {
				mCounter[0] = GetAUDFP1<0>();
				mCounterBorrow[0] = 0;
				SetupTimers(0x03);
			} else
				SetupTimers(0x02);
			break;

		case kATPokeyEventTimer3Borrow:
			mpTimerBorrowEvents[2] = NULL;
			FireTimer<2>();

			mCounterBorrow[2] = 0;
			if (!mbLinkedTimers34) {
				mCounter[2] = GetAUDFP1<2>();
			} else {
				// If we are operating at 1.79MHz, three cycles have already elapsed from the underflow to
				// when the borrow goes through.
				mCounter[2] = mbFastTimer3 ? 253 : 256;

				// We need the timer 4 counter updated so SetupTimers() can detect if a timer 4 borrow
				// is happening, which affects interpretation of the timer 3 counter.
				UpdateTimerCounter<3>();
			}

			SetupTimers(0x04);
			break;

		case kATPokeyEventTimer4Borrow:
			mpTimerBorrowEvents[3] = NULL;
			FireTimer<3>();

			mCounter[3] = GetAUDFP1<3>();
			mCounterBorrow[3] = 0;
			if (mbLinkedTimers34) {
				mCounter[2] = GetAUDFP1<2>();
				mCounterBorrow[2] = 0;
				SetupTimers(0x0C);
			} else
				SetupTimers(0x08);
			break;

		case kATPokeyEventResetTwoTones1:
			mpEventResetTwoTones1 = nullptr;

			// resync timers 1 and 2
			mCounter[0] = GetAUDFP1<0>();
			mCounter[1] = GetAUDFP1<1>();
			mCounterBorrow[0] = 0;
			mCounterBorrow[1] = 0;
			SetupTimers(0x03);
			break;

		case kATPokeyEventResetTwoTones2:
			mpEventResetTwoTones2 = nullptr;

			// resync timers 1 and 2
			mCounter[0] = GetAUDFP1<0>();
			mCounter[1] = GetAUDFP1<1>();
			mCounterBorrow[0] = 0;
			mCounterBorrow[1] = 0;
			SetupTimers(0x03);
			break;

		case kATPokeyEventResetTimers:
			mpResetTimersEvent = nullptr;

			for(int i=0; i<4; ++i) {
				mCounterBorrow[i] = 0;

				// If the timer is coincident or one cycle away, don't reset it.
				if (mpTimerBorrowEvents[i]) {
					uint32 dt = mpScheduler->GetTicksToEvent(mpTimerBorrowEvents[i]);

					if (dt == 1) {
						mpScheduler->RemoveEvent(mpTimerBorrowEvents[i]);
						mpTimerBorrowEvents[i] = NULL;
					}
				}
			}

			mpScheduler->SetEvent(1, this, kATPokeyEventResetTimers2, mpResetTimers2Event);
			break;

		case kATPokeyEventResetTimers2:
			mpResetTimers2Event = nullptr;

			{
				const uint32 t = mpScheduler->GetTick();
				mCounter[0] = GetAUDFP1<0>(t);
				mCounter[1] = GetAUDFP1<1>(t);
				mCounter[2] = GetAUDFP1<2>(t);
				mCounter[3] = GetAUDFP1<3>(t);
			}

			mpRenderer->ResetTimers();

			SetupTimers(0x0f);
			break;

		case kATPokeyEventSerialOutput:
			mpEventSerialOutput = NULL;

			if (mSerialOutputCounter)
				OnSerialOutputTick();
			break;

		case kATPokeyEventSerialInput:
			mpEventSerialInput = NULL;

			if (mSerialInputCounter)
				OnSerialInputTick();
			break;
	}
}

template<int channel>
void ATPokeyEmulator::RecomputeTimerPeriod() {
	static_assert(channel >= 0 && channel <= 3);

	const bool fastTimer = (channel == 0) ? mbFastTimer1 : (channel == 2) ? mbFastTimer3 : false;
	const bool hiLinkedTimer = channel == 1 ? mbLinkedTimers12 : channel == 3 ? mbLinkedTimers34 : false;
	const bool loLinkedTimer = channel == 0 ? mbLinkedTimers12 : channel == 2 ? mbLinkedTimers34 : false;

	uint32 period;
	if (hiLinkedTimer) {
		constexpr int loChannel = channel & ~1;
		const bool fastLinkedTimer = (channel == 1) ? mbFastTimer1 : (channel == 3) ? mbFastTimer3 : false;

		// Note: It is important in this function that we always use the eventual
		// period -- we deliberately don't use GetAUDFP1<> as we want the period
		// that the timer will eventually settle into, not what it will use on
		// the next cycle.
		period = ((uint32)mAUDF[channel] << 8) + mAUDFP1B[loChannel];

		if (fastLinkedTimer) {
			period += 6;
		} else if (mbUse15KHzClock)
			period *= 114;
		else
			period *= 28;
	} else {
		period = mAUDFP1B[channel];

		if (fastTimer)
			period += 3;
		else if (mbUse15KHzClock)
			period *= 114;
		else
			period *= 28;

		if (loLinkedTimer) {
			uint32 fullPeriod;

			if (fastTimer)
				fullPeriod = 256;
			else if (mbUse15KHzClock)
				fullPeriod = 256 * 114;
			else
				fullPeriod = 256 * 28;

			mTimerFullPeriod[channel >> 1] = fullPeriod;
		}
	}

	if constexpr (channel == 3) {
		if (mTimerPeriod[channel] != period) {
			mTimerPeriod[channel] = period;

			if (mbNotifyBiClockChange && (mSKCTL & 0x30) == 0x20)
				mpNotifyBiClockChangeFn();
		}
	} else {
		mTimerPeriod[channel] = period;
	}
}

void ATPokeyEmulator::RecomputeAllowedDeferredTimers() {
	// Timer 2 IRQ, two-tone mode, or timer 2 as serial output clock prohibits deferring timer 2.
	mbAllowDeferredTimer[1] = !kForceActiveTimers
		&& !(mIRQEN & 0x02)
		&& !(mSKCTL & 8)
		&& (mSKCTL & 0x60) != 0x60;

	// Timer 1 IRQ or two-tone mode prohibits deferring timer 1.
	mbAllowDeferredTimer[0] = !kForceActiveTimers
		&& !(mIRQEN & 0x01)
		&& !(mSKCTL & 8);

	// Timer 4 IRQ, asynchronous receive mode, or using timer 4 as transmit clock prohibits deferring timer 4.
	mbAllowDeferredTimer[3] = !kForceActiveTimers
		&& !(mIRQEN & 0x04)
		&& !(mSKCTL & 0x10)
		&& (mSKCTL & 0x60) != 0x20
		&& (mSKCTL & 0x60) != 0x40;

	// Timer 3 deferring is ordinarily always possible.
	mbAllowDeferredTimer[2] = !kForceActiveTimers;

	// Check if timers are linked; both timers must be able to defer together.
	if (mbLinkedTimers12) {
		if (!mbAllowDeferredTimer[0] || !mbAllowDeferredTimer[1]) {
			mbAllowDeferredTimer[0] = false;
			mbAllowDeferredTimer[1] = false;
		}
	}

	if (mbLinkedTimers34) {
		if (!mbAllowDeferredTimer[2] || !mbAllowDeferredTimer[3]) {
			mbAllowDeferredTimer[2] = false;
			mbAllowDeferredTimer[3] = false;
		}
	}
}

template<int channel>
void ATPokeyEmulator::UpdateTimerCounter() {
	static_assert(channel >= 0 && channel <= 3);

	VDASSERT(mCounter[0] > 0);
	VDASSERT(mCounterBorrow[0] >= 0);

	const uint32 t = ATSCHEDULER_GETTIME(mpScheduler);

	mCounterBorrow[channel] = 0;

	const bool fastTimer = channel == 0 || (channel == 1 && mbLinkedTimers12) ? mbFastTimer1
		: channel == 2 || (channel == 3 && mbLinkedTimers34) ? mbFastTimer3 : false;
	const bool hiLinkedTimer = channel == 1 ? mbLinkedTimers12 : channel == 3 ? mbLinkedTimers34 : false;
	const bool loLinkedTimer = channel == 0 ? mbLinkedTimers12 : channel == 2 ? mbLinkedTimers34 : false;

	if (loLinkedTimer) {
		constexpr int hiChannel = channel | 1;

		// Compute the number of ticks until the next borrow event. The borrow occurs three cycles after
		// the timer channel underflows; 16-bit channels take 6 cycles because the borrows from the two
		// channels are cascaded. If we have a borrow event, we can simply use that time directly.

		int ticksLeft;
		if (mbDeferredTimerEvents[hiChannel]) {
			if ((sint32)(t - mDeferredTimerStarts[hiChannel]) < 0)
				ticksLeft = mDeferredTimerStarts[hiChannel] - t;
			else
				ticksLeft = mDeferredTimerPeriods[hiChannel] - (t - mDeferredTimerStarts[hiChannel]) % mDeferredTimerPeriods[hiChannel];
		} else if (mpTimerBorrowEvents[hiChannel])
			ticksLeft = mpScheduler->GetTicksToEvent(mpTimerBorrowEvents[hiChannel]);
		else
			return;

		// Compute the low timer offset from the high timer offset.
		[[maybe_unused]] int ticksLeft0 = ticksLeft;

		if (ticksLeft <= 3) {
			// Three ticks or less means that the high timer is borrowing, so we're free running. We've
			// already run through the three ticks of low timer borrow.
			if (fastTimer)
				mCounter[channel] = 256 - ticksLeft;
			else
				mCounter[channel] = 256;
		} else {
			ticksLeft -= 3;

			if (ticksLeft <= 3) {
				// Low timer borrow is in progress.
				mCounterBorrow[channel] = ticksLeft;

				if (fastTimer)
					mCounter[channel] = 256 - ticksLeft;
				else
					mCounter[channel] = 256;
			} else {
				ticksLeft = (uint32)(ticksLeft - 3) % mTimerFullPeriod[channel >> 1];

				if (!fastTimer) {
					if (mbUse15KHzClock)
						ticksLeft = (ticksLeft + 113) / 114;
					else
						ticksLeft = (ticksLeft + 27) / 28;
				}

				mCounter[channel] = ticksLeft ? ticksLeft : 256;
			}
		}

		ATLOGPOKEYTI("Counter[%d] = %03X+%d (%d ticks left, full period = %d)\n", channel, mCounter[channel], mCounterBorrow[channel], ticksLeft0, mTimerFullPeriod[channel >> 1]);

		VDASSERT(mCounter[channel] > 0 && mCounter[channel] <= 256);
		VDASSERT(mCounterBorrow[channel] >= 0);
	} else {
		// Compute the number of ticks until the next borrow event. The borrow occurs three cycles after
		// the timer channel underflows; 16-bit channels take 6 cycles because the borrows from the two
		// channels are cascaded. If we have a borrow event, we can simply use that time directly.

		int ticksLeft;
		if (mbDeferredTimerEvents[channel]) {
			if ((sint32)(t - mDeferredTimerStarts[channel]) < 0)
				ticksLeft = mDeferredTimerStarts[channel] - t;
			else
				ticksLeft = mDeferredTimerPeriods[channel] - (t - mDeferredTimerStarts[channel]) % mDeferredTimerPeriods[channel];
		} else if (mpTimerBorrowEvents[channel])
			ticksLeft = mpScheduler->GetTicksToEvent(mpTimerBorrowEvents[channel]);
		else
			return;

		// If we're three ticks or less away, we have a borrow pending. At fast clock the timer will begin
		// counting down immediately during the borrow; this is important to maintain 16-bit, 1.79MHz accuracy.
		// At slow clock this can only happen once if the slow clock just happens to fall at the right time
		// (can happen if 1.79MHz clocking is disabled).
		//
		// TODO: Handle slow tick happening in borrow land.

		[[maybe_unused]] int ticksLeft0 = ticksLeft;
		if (ticksLeft <= 3) {
			mCounterBorrow[channel] = ticksLeft;

			if (fastTimer && !hiLinkedTimer)
				mCounter[channel] = 256 - ticksLeft;
			else
				mCounter[channel] = 256;
		} else {
			ticksLeft -= 3;

			if (hiLinkedTimer) {
				if (ticksLeft <= 3) {
					mCounter[channel] = 256;
					mCounterBorrow[channel] = ticksLeft + 3;
				} else {
					ticksLeft -= 3;

					if (fastTimer)
						mCounter[channel] = (ticksLeft + 255) >> 8;
					else if (mbUse15KHzClock)
						mCounter[channel] = (ticksLeft + 114*256 - 1) / (114*256);
					else
						mCounter[channel] = (ticksLeft + 28*256 - 1) / (28*256);
				}
			} else {
				if (fastTimer)
					mCounter[channel] = ticksLeft;
				else if (mbUse15KHzClock)
					mCounter[channel] = (ticksLeft + 113) / 114;
				else
					mCounter[channel] = (ticksLeft + 27) / 28;
			}
		}

		ATLOGPOKEYTI("Counter[%d] = %03X+%d (%d ticks left)\n", channel, mCounter[channel], mCounterBorrow[channel], ticksLeft0);

		VDASSERT(mCounter[channel] > 0 && mCounter[channel] <= 256);
		VDASSERT(mCounterBorrow[channel] >= 0);
	}
}

void ATPokeyEmulator::SetupTimers(uint8 channels) {
	uint32 t = ATSCHEDULER_GETTIME(mpScheduler);
	int cyclesToNextSlowTick;

	if (mbUse15KHzClock) {
		cyclesToNextSlowTick = 114 - (t - UpdateLast15KHzTime());

		if (cyclesToNextSlowTick)
			cyclesToNextSlowTick -= 114;
	} else {
		int slowTickOffset = t - UpdateLast64KHzTime();

		cyclesToNextSlowTick = (28 - slowTickOffset);

		if (cyclesToNextSlowTick)
			cyclesToNextSlowTick -= 28;
	}

	const bool slowTickValid = (mSKCTL & 3) != 0;

	VDASSERT(!slowTickValid || (cyclesToNextSlowTick >= -114 && cyclesToNextSlowTick <= 0));

	if (channels & 0x01) {
		mpScheduler->UnsetEvent(mpTimerBorrowEvents[0]);
		FlushDeferredTimerEvents(0);

		if (!mbFastTimer1 && !slowTickValid) {
			// 15/64KHz clock is stopped and we're not running at 1.79MHz. In this case, we still
			// fire the borrow event if we have one pending.
			if (mCounterBorrow[0])
				mpTimerBorrowEvents[0] = mpScheduler->AddEvent(mCounterBorrow[0], this, kATPokeyEventTimer1Borrow);
		} else {
			// Computer number of ticks until the next borrow event. If we have a borrow pending,
			// that's easy; otherwise, we have to look at the current counter value, slow tick offset,
			// and mode.
			uint32 ticks = mCounterBorrow[0];
			
			if (!ticks) {
				// if we are linked and the high timer is borrowing, we're going to get reset soon and need
				// to use the full period counter
				if (mbLinkedTimers12 && mCounterBorrow[1]) {
					ticks = mCounterBorrow[1] + mTimerPeriod[1] - 3;
				} else {
					ticks = mCounter[0];

					if (!mbFastTimer1) {
						if (mbUse15KHzClock)
							ticks = ticks * 114;
						else
							ticks = ticks * 28;

						ticks += cyclesToNextSlowTick;
					}

					// Borrow takes place three cycles after underflow.
					ticks += 3;
				}
			}

			VDASSERT((sint32)ticks > 0);

			if (!mbAllowDeferredTimer[0]) {
				ATLOGPOKEYTI("Timer 1 - active tick in %u cycles\n", ticks);

				mpTimerBorrowEvents[0] = mpScheduler->AddEvent(ticks, this, kATPokeyEventTimer1Borrow);
			} else if (mbLinkedTimers12) {
				uint32 loTime = t + ticks;
				uint32 hiTime;

				if (mCounterBorrow[1]) {
					hiTime = t + mCounterBorrow[1];

					if (ATWrapTime{hiTime} < loTime)
						hiTime += mTimerPeriod[1];
				} else {
					hiTime = loTime + 3 + mTimerFullPeriod[0] * (mCounter[1] - 1);
				}

				ATLOGPOKEYTI("Timer 1 - passive linked tick in %u cycles, hi-tick in %u cycles (full period %u, period %u, hi-period %u)\n", ticks, hiTime - t, mTimerFullPeriod[0], mTimerPeriod[0], mTimerPeriod[1]);
				SetupDeferredTimerEventsLinked(0, loTime, mTimerFullPeriod[0], hiTime, mTimerPeriod[1], mTimerPeriod[0] - 3);
			} else {
				ATLOGPOKEYTI("Timer 1 - passive tick in %u cycles\n", ticks);
				SetupDeferredTimerEvents(0, t + ticks, mTimerPeriod[0]);
			}
		}
	}

	if (channels & 0x02) {
		mpScheduler->UnsetEvent(mpTimerBorrowEvents[1]);
		FlushDeferredTimerEvents(1);

		if (mbLinkedTimers12) {
			if (!mbFastTimer1 && !slowTickValid) {
				// 15/64KHz clock is stopped and we're not running at 1.79MHz. In this case, we still
				// fire the borrow event if we have one pending.
				if (mCounterBorrow[1])
					mpTimerBorrowEvents[1] = mpScheduler->AddEvent(mCounterBorrow[1], this, kATPokeyEventTimer2Borrow);
			} else {
				// Computer number of ticks until the next borrow event. If we have a borrow pending,
				// that's easy; otherwise, we have to look at the current counter value, slow tick offset,
				// and mode.
				uint32 ticks = mCounterBorrow[1];
				
				if (!ticks) {
					ticks = mCounterBorrow[0];

					if (!ticks) {
						ticks = mCounter[0];

						if (!mbFastTimer1) {
							if (mbUse15KHzClock)
								ticks = ticks * 114;
							else
								ticks = ticks * 28;

							ticks += cyclesToNextSlowTick;
						}

						ticks += 3;
					}

					if (mbFastTimer1)
						ticks += (mCounter[1] - 1) << 8;
					else if (mbUse15KHzClock)
						ticks += (mCounter[1] - 1) * (256 * 114);
					else
						ticks += (mCounter[1] - 1) * (256 * 28);

					// Borrow takes place three cycles after underflow.
					ticks += 3;
				}

				VDASSERT((sint32)ticks > 0);

				if (!mbAllowDeferredTimer[1]) {
					ATLOGPOKEYTI("Timer 2 - linked active tick in %u cycles -> %08X (%03X+%u:%03X+%u)\n", ticks, t + ticks, mCounter[1], mCounterBorrow[1], mCounter[0], mCounterBorrow[0]);
					mpTimerBorrowEvents[1] = mpScheduler->AddEvent(ticks, this, kATPokeyEventTimer2Borrow);
				} else {
					ATLOGPOKEYTI("Timer 2 - linked passive tick in %u cycles -> %08X (%03X+%u:%03X+%u)\n", ticks, t + ticks, mCounter[1], mCounterBorrow[1], mCounter[0], mCounterBorrow[0]);
					SetupDeferredTimerEvents(1, t + ticks, mTimerPeriod[1]);
				}
			}
		} else {
			if (!slowTickValid) {
				// 15/64KHz clock is stopped and we're not running at 1.79MHz. In this case, we still
				// fire the borrow event if we have one pending.
				if (mCounterBorrow[1])
					mpTimerBorrowEvents[1] = mpScheduler->AddEvent(mCounterBorrow[1], this, kATPokeyEventTimer2Borrow);
			} else {
				// Computer number of ticks until the next borrow event. If we have a borrow pending,
				// that easy; otherwise, we have to look at the current counter value, slow tick offset,
				// and mode.
				uint32 ticks = mCounterBorrow[1];
				
				if (!ticks) {
					ticks = mCounter[1];

					if (mbUse15KHzClock)
						ticks = ticks * 114;
					else
						ticks = ticks * 28;

					ticks += cyclesToNextSlowTick;

					// Borrow takes place three cycles after underflow.
					ticks += 3;
				}

				VDASSERT((sint32)ticks > 0);

				if (!mbAllowDeferredTimer[1]) {
					mpTimerBorrowEvents[1] = mpScheduler->AddEvent(ticks, this, kATPokeyEventTimer2Borrow);
				} else {
					SetupDeferredTimerEvents(1, t + ticks, mTimerPeriod[1]);
				}
			}
		}
	}

	if ((mSKCTL & 0x10) && mbSerialWaitingForStartBit) {
		if (channels & 0x04) {
			mpScheduler->UnsetEvent(mpTimerBorrowEvents[2]);
			FlushDeferredTimerEvents(2);

			if (mCounterBorrow[2])
				mpTimerBorrowEvents[2] = mpScheduler->AddEvent(mCounterBorrow[2], this, kATPokeyEventTimer3Borrow);
		}

		if (channels & 0x08) {
			mpScheduler->UnsetEvent(mpTimerBorrowEvents[3]);
			FlushDeferredTimerEvents(3);

			if (mCounterBorrow[3])
				mpTimerBorrowEvents[3] = mpScheduler->AddEvent(mCounterBorrow[3], this, kATPokeyEventTimer4Borrow);
		}
	} else {
		if (channels & 0x04) {
			mpScheduler->UnsetEvent(mpTimerBorrowEvents[2]);
			FlushDeferredTimerEvents(2);

			if (!mbFastTimer3 && !slowTickValid) {
				// 15/64KHz clock is stopped and we're not running at 1.79MHz. In this case, we still
				// fire the borrow event if we have one pending.
				if (mCounterBorrow[2])
					mpTimerBorrowEvents[2] = mpScheduler->AddEvent(mCounterBorrow[2], this, kATPokeyEventTimer3Borrow);
			} else {
				// Computer number of ticks until the next borrow event. If we have a borrow pending,
				// that's easy; otherwise, we have to look at the current counter value, slow tick offset,
				// and mode.
				uint32 ticks = mCounterBorrow[2];
				
				if (!ticks) {
					// if we are linked and the high timer is borrowing, we're going to get reset soon and need
					// to use the full period counter
					if (mbLinkedTimers34 && mCounterBorrow[3]) {
						ticks = mCounterBorrow[3] + mTimerPeriod[3] - 3;
					} else {
						ticks = mCounter[2];

						if (!mbFastTimer3) {
							if (mbUse15KHzClock)
								ticks = ticks * 114;
							else
								ticks = ticks * 28;

							ticks += cyclesToNextSlowTick;
						}

						// Borrow takes place three cycles after underflow.
						ticks += 3;
					}
				}

				VDASSERT((sint32)ticks > 0);

				// Check if we need an active event or if we can go to deferred mode. We only need an
				// active event if timers are linked, timer 1 IRQ is enabled, or two-tone mode is active.
				if (!mbAllowDeferredTimer[2])
					mpTimerBorrowEvents[2] = mpScheduler->AddEvent(ticks, this, kATPokeyEventTimer3Borrow);
				else if (mbLinkedTimers34) {
				uint32 loTime = t + ticks;
				uint32 hiTime;

				if (mCounterBorrow[3]) {
					hiTime = t + mCounterBorrow[3];

					if (ATWrapTime{hiTime} < loTime)
						hiTime += mTimerPeriod[3];
				} else {
					hiTime = loTime + 3 + mTimerFullPeriod[1] * (mCounter[3] - 1);
				}

					SetupDeferredTimerEventsLinked(2, loTime, mTimerFullPeriod[1], hiTime, mTimerPeriod[3], mTimerPeriod[2] - 3);
				} else
					SetupDeferredTimerEvents(2, t + ticks, mTimerPeriod[2]);
			}
		}

		if (channels & 0x08) {
			mpScheduler->UnsetEvent(mpTimerBorrowEvents[3]);
			FlushDeferredTimerEvents(3);

			if (mbLinkedTimers34) {
				if (!mbFastTimer3 && !slowTickValid) {
					// 15/64KHz clock is stopped and we're not running at 1.79MHz. In this case, we still
					// fire the borrow event if we have one pending.
					if (mCounterBorrow[3])
						mpTimerBorrowEvents[3] = mpScheduler->AddEvent(mCounterBorrow[3], this, kATPokeyEventTimer4Borrow);
				} else {
					// Computer number of ticks until the next borrow event. If we have a borrow pending,
					// that's easy; otherwise, we have to look at the current counter value, slow tick offset,
					// and mode.
					uint32 ticks = mCounterBorrow[3];
					
					if (!ticks) {
						ticks = mCounterBorrow[2];

						if (!ticks) {
							ticks = mCounter[2];

							if (!mbFastTimer3) {
								if (mbUse15KHzClock)
									ticks = ticks * 114;
								else
									ticks = ticks * 28;

								ticks += cyclesToNextSlowTick;
							}

							ticks += 3;
						}

						if (mbFastTimer3)
							ticks += (mCounter[3] - 1) << 8;
						else if (mbUse15KHzClock)
							ticks += (mCounter[3] - 1) * (114 * 256);
						else
							ticks += (mCounter[3] - 1) * (28 * 256);

						// Borrow takes place three cycles after underflow.
						ticks += 3;
					}

					VDASSERT(ticks > 0);

					if (!mbAllowDeferredTimer[3]) {
						ATLOGPOKEYTI("Timer 4 - linked active tick in %u cycles -> %08X (%03X+%u:%03X+%u)\n", ticks, t + ticks, mCounter[3], mCounterBorrow[3], mCounter[2], mCounterBorrow[2]);
						mpTimerBorrowEvents[3] = mpScheduler->AddEvent(ticks, this, kATPokeyEventTimer4Borrow);
					} else {
						ATLOGPOKEYTI("Timer 4 - linked passive tick in %u cycles -> %08X (%03X+%u:%03X+%u)\n", ticks, t + ticks, mCounter[3], mCounterBorrow[3], mCounter[2], mCounterBorrow[2]);
						SetupDeferredTimerEvents(3, t + ticks, mTimerPeriod[3]);
					}
				}
			} else {
				if (!slowTickValid) {
					// 15/64KHz clock is stopped and we're not running at 1.79MHz. In this case, we still
					// fire the borrow event if we have one pending.
					if (mCounterBorrow[3])
						mpTimerBorrowEvents[3] = mpScheduler->AddEvent(mCounterBorrow[3], this, kATPokeyEventTimer4Borrow);
				} else {
					// Computer number of ticks until the next borrow event. If we have a borrow pending,
					// that easy; otherwise, we have to look at the current counter value, slow tick offset,
					// and mode.
					uint32 ticks = mCounterBorrow[3];
					
					if (!ticks) {
						ticks = mCounter[3];

						if (mbUse15KHzClock)
							ticks = ticks * 114;
						else
							ticks = ticks * 28;

						ticks += cyclesToNextSlowTick;

						// Borrow takes place three cycles after underflow.
						ticks += 3;
					}

					VDASSERT(ticks > 0);

					// We only need the timer 4 event if the timers are linked, the timer 4 IRQ is enabled,
					// asynchronous receive mode is enabled, or timer 4 is being used as the output clock.
					if (!mbAllowDeferredTimer[3]) {
						ATLOGPOKEYTI("Timer 4 - active tick in %u cycles (%03X+%u:%03X+%u)\n", ticks, mCounter[3], mCounterBorrow[3], mCounter[2], mCounterBorrow[2]);
						mpTimerBorrowEvents[3] = mpScheduler->AddEvent(ticks, this, kATPokeyEventTimer4Borrow);
					} else {
						ATLOGPOKEYTI("Timer 4 - passive tick in %u cycles (%03X+%u:%03X+%u)\n", ticks, mCounter[3], mCounterBorrow[3], mCounter[2], mCounterBorrow[2]);
						SetupDeferredTimerEvents(3, t + ticks, mTimerPeriod[3]);
					}
				}
			}
		}
	}
}

void ATPokeyEmulator::FlushDeferredTimerEvents(int channel) {
	if (!mbDeferredTimerEvents[channel])
		return;

	// get current time
	uint32 t = ATSCHEDULER_GETTIME(mpScheduler);

	mbDeferredTimerEvents[channel] = false;

	mpRenderer->ClearChannelDeferredEvents(channel, t);
}

void ATPokeyEmulator::SetupDeferredTimerEvents(int channel, uint32 t0, uint32 period) {
	VDASSERT(!mbDeferredTimerEvents[channel]);
	mbDeferredTimerEvents[channel] = true;
	mDeferredTimerStarts[channel] = t0;
	mDeferredTimerPeriods[channel] = period;

	mpRenderer->SetChannelDeferredEvents(channel, t0, period);
}

void ATPokeyEmulator::SetupDeferredTimerEventsLinked(int channel, uint32 t0, uint32 period, uint32 hit0, uint32 hiperiod, uint32 hilooffset) {
	VDASSERT(!mbDeferredTimerEvents[channel]);
	mbDeferredTimerEvents[channel] = true;
	mDeferredTimerStarts[channel] = t0;
	mDeferredTimerPeriods[channel] = period;

	mpRenderer->SetChannelDeferredEventsLinked(channel, t0, period, hit0, hiperiod, hilooffset);
}

sint32 ATPokeyEmulator::ReadBackWriteRegister(uint8 reg) const {
	reg &= mAddressMask;

	if (mpSlave && (reg & 0x10))
		return mpSlave->ReadBackWriteRegister(reg);

	switch (reg & 0x0F) {
		case 0x00:	// $D200 AUDF1
			return mAUDF[0];
		case 0x01:	// $D201 AUDC1
			return mAUDC[0];
		case 0x02:	// $D202 AUDF2
			return mAUDF[1];
		case 0x03:	// $D203 AUDC2
			return mAUDC[1];
		case 0x04:	// $D204 AUDF3
			return mAUDF[2];
		case 0x05:	// $D205 AUDC3
			return mAUDC[2];
		case 0x06:	// $D206 AUDF4
			return mAUDF[3];
		case 0x07:	// $D207 AUDC4
			return mAUDC[3];
		case 0x08:	// $D208 AUDCTL
			return mAUDCTL;
		case 0x0D:	// $D20D SEROUT
			return mSEROUT;
		case 0x0E:	// $D20E IRQEN
			return mIRQEN;
		case 0x0F:	// $D20F SKCTL
			return mSKCTL;

		default:
			break;
	}

	return -1;
}

uint8 ATPokeyEmulator::DebugReadByte(uint8 reg) const {
	reg &= mAddressMask;

	switch(reg) {
		case 0x00:	// $D200 POT0
		case 0x01:	// $D201 POT1
		case 0x02:	// $D202 POT2
		case 0x03:	// $D203 POT3
		case 0x04:	// $D204 POT4
		case 0x05:	// $D205 POT5
		case 0x06:	// $D206 POT6
		case 0x07:	// $D207 POT7
			return const_cast<ATPokeyEmulator *>(this)->ReadByte(reg);
		case 0x08:	// $D208 ALLPOT
			if (mbPotScanActive)
				const_cast<ATPokeyEmulator *>(this)->UpdatePots(0);
			return mALLPOT;
		case 0x09:	// $D209 KBCODE
			return mKBCODE;
		case 0x0A:	// $D20A RANDOM
			return const_cast<ATPokeyEmulator *>(this)->ReadByte(reg);
		case 0x0D:	// $D20D SERIN
			return mSERIN;
		case 0x0E:
			return mIRQST;
		case 0x0F:
			return const_cast<ATPokeyEmulator *>(this)->ReadByte(reg);
	}

	if (reg & 0x10) {
		if (mpSlave)
			return mpSlave->DebugReadByte(reg & 0x0f);
		else
			return DebugReadByte(reg & 0x0f);
	}

	return 0xFF;
}

uint8 ATPokeyEmulator::ReadByte(uint8 reg) {
	reg &= mAddressMask;

	switch(reg) {
		case 0x00:	// $D200 POT0
		case 0x01:	// $D201 POT1
		case 0x02:	// $D202 POT2
		case 0x03:	// $D203 POT3
		case 0x04:	// $D204 POT4
		case 0x05:	// $D205 POT5
		case 0x06:	// $D206 POT6
		case 0x07:	// $D207 POT7
			UpdatePots(0);

			if (mALLPOT & (1 << reg)) {
				uint8 count = mPotMasterCounter;

				// Reading the count on a POTn register that is receiving updates
				// can be unstable if it is read on the cycle where the counter
				// increments. In fast pot mode, this happens on every cycle, and
				// in slow pot mode, it happens for one cycle every 114 cycles.
				// The value that is read is the bitwise AND of the pre- and post-
				// increment values, presumably due to the differing speed of
				// rising and falling transitions.
				//
				// This only affects the updating counters, as a POT register that
				// has stopped counting can latch an odd value, which is never
				// seen from a live updating counter.

				if (!(mSKCTL & 4)) {
					// slow pot scan -- check if 15KHz timer is enabled
					if (!(mSKCTL & 3)) {
						// 15KHz clock frozen -- can't increment
						return count;
					}

					if ((ATSCHEDULER_GETTIME(mpScheduler) - mLast15KHzTime) % 114 != 113)
						return count;
				}

				return count & (count + 1);
			}

			return mPotLatches[reg];

		case 0x08:	// $D208 ALLPOT
			// We only need to update the pots if the pot scan is active. Otherwise,
			// ALLPOT will always be $00 by definition.
			if (mbPotScanActive)
				UpdatePots(0);

			return mALLPOT;
		case 0x09:	// $D209 KBCODE
			return mKBCODE;
		case 0x0A:	// $D20A RANDOM
			{
				const bool initMode = !(mSKCTL & 3);
				uint8 forceMask = 0;

				if (initMode) {
					uint64 offset = mpScheduler->GetTick64() - mPolyShutOffTime;

					if (offset > 10)
						return 0xFF;

					if (offset)
						forceMask = (uint8)(0xFFE00 >> (int)(uint32)offset);
				}

				UpdatePolyTime();

				const uint8 *src = mAUDCTL & 0x80 ? &mpTables->mPolyBuffer[mPoly9Counter] : &mpTables->mPolyBuffer[mPoly17Counter];
				uint8 v = 0;

				if (mAUDCTL & 0x80) {
					for(int i=7; i>=0; --i)
						v = (v + v) + ((src[i] & 2) >> 1);
				} else {
					for(int i=7; i>=0; --i)
						v = (v + v) + (src[i] & 1);
				}

				return ~v | forceMask;
			}
		case 0x0D:	// $D20D SERIN
			{
				uint8 c = mSERIN;

				if (mbTraceSIO)
					(*mpConsoleOut)("POKEY: Reading SERIN value %02X (shiftreg: %02X)", c, mSerialInputShiftRegister);

				if (mbSerInBurstPendingData) {
					mbSerInBurstPendingData = false;

					if (!mbSerInBurstPendingIRQ1 && !mbSerInBurstPendingIRQ2) {
						for(IATPokeySIODevice *dev : mDevices) {
							if (mbSerInBurstPendingData)
								break;

							dev->PokeySerInReady();
						}
					}
				}

				return c;
			}
			break;

		case 0x0E:	// $D20E IRQST
			return mIRQST;

		case 0x0F:
			{
				uint8 c = mSKSTAT;

				if (mpScheduler->GetTick64() >= mSerialDataInFlipTime)
					c ^= 0x10;

				if (mbSerialSimulateInputPort) {
					uint32 dt = ATSCHEDULER_GETTIME(mpScheduler) - mSerialSimulateInputBaseTime;
					uint32 bitidx = dt / mSerialSimulateInputCyclesPerBit;

					if (bitidx >= 10)
						mbSerialSimulateInputPort = false;
					else {
						c &= 0xef;

						if (mSerialSimulateInputData & (1 << bitidx))
							c |= 0x10;
					}
				}

				return c;
			}
			break;

		default:
//			__debugbreak();
			break;
	}

	if (reg & 0x10)
		return mpSlave->ReadByte(reg & 0x0f);

	return 0xFF;
}

void ATPokeyEmulator::WriteByte(uint8 reg, uint8 value) {
	reg &= mAddressMask;

	mState.mReg[reg] = value;

	switch(reg) {
		case 0x00:	// $D200 AUDF1
			if (mAUDF[0] != value) {
				mAUDF[0] = value;

				SetAUDFP1<0>((int)value + 1);

				RecomputeTimerPeriod<0>();
				
				if (mbLinkedTimers12) {
					RecomputeTimerPeriod<1>();

					// Both counters must be updated before we run SetupTimers().
					UpdateTimerCounter<0>();
					UpdateTimerCounter<1>();
					SetupTimers(0x03);
				} else {
					UpdateTimerCounter<0>();
					SetupTimers(0x01);
				}
			}
			break;
		case 0x01:	// $D201 AUDC1
			if (mAUDC[0] != value) {
				mAUDC[0] = value;
				mpRenderer->SetAUDCx(0, value);
			}
			break;
		case 0x02:	// $D202 AUDF2
			if (mAUDF[1] != value) {
				mAUDF[1] = value;

				SetAUDFP1<1>((int)value + 1);

				RecomputeTimerPeriod<1>();

				if (mbLinkedTimers12) {
					// Both counters must be updated before we run SetupTimers().
					UpdateTimerCounter<0>();
					UpdateTimerCounter<1>();
					SetupTimers(0x03);
				} else {
					UpdateTimerCounter<1>();
					SetupTimers(0x02);
				}
			}
			break;
		case 0x03:	// $D203 AUDC2
			if (mAUDC[1] != value) {
				mAUDC[1] = value;
				mpRenderer->SetAUDCx(1, value);
			}
			break;
		case 0x04:	// $D204 AUDF3
			if (mAUDF[2] != value) {
				mAUDF[2] = value;

				SetAUDFP1<2>((int)value + 1);

				RecomputeTimerPeriod<2>();

				if (mbLinkedTimers34) {
					RecomputeTimerPeriod<3>();

					// Both counters must be updated before we run SetupTimers().
					UpdateTimerCounter<2>();
					UpdateTimerCounter<3>();
					SetupTimers(0x0C);
				} else {
					UpdateTimerCounter<2>();
					SetupTimers(0x04);
				}
			}
			mbSerialRateChanged = true;
			break;
		case 0x05:	// $D205 AUDC3
			if (mAUDC[2] != value) {
				mAUDC[2] = value;
				mpRenderer->SetAUDCx(2, value);
			}
			break;
		case 0x06:	// $D206 AUDF4
			if (mAUDF[3] != value) {
				mAUDF[3] = value;

				SetAUDFP1<3>((int)value + 1);

				RecomputeTimerPeriod<3>();

				if (mbLinkedTimers34) {
					// Both counters must be updated before we run SetupTimers().
					UpdateTimerCounter<2>();
					UpdateTimerCounter<3>();
					SetupTimers(0x0C);
				} else {
					UpdateTimerCounter<3>();
					SetupTimers(0x08);
				}
			}
			mbSerialRateChanged = true;
			break;
		case 0x07:	// $D207 AUDC4
			if (mAUDC[3] != value) {
				mAUDC[3] = value;
				mpRenderer->SetAUDCx(3, value);
			}
			break;
		case 0x08:	// $D208 AUDCTL
			if (mAUDCTL != value) {
				uint8 delta = mAUDCTL ^ value;
				if (delta & 0x29)
					mbSerialRateChanged = true;

				UpdateTimerCounter<0>();
				UpdateTimerCounter<1>();
				UpdateTimerCounter<2>();
				UpdateTimerCounter<3>();

				FlushDeferredTimerEvents(0);
				FlushDeferredTimerEvents(1);
				FlushDeferredTimerEvents(2);
				FlushDeferredTimerEvents(3);

				mAUDCTL = value;
				mbFastTimer1 = (mAUDCTL & 0x40) != 0;
				mbFastTimer3 = (mAUDCTL & 0x20) != 0;
				mbLinkedTimers12 = (mAUDCTL & 0x10) != 0;
				mbLinkedTimers34 = (mAUDCTL & 0x08) != 0;
				mbUse15KHzClock = (mAUDCTL & 0x01) != 0;

				mpRenderer->SetAUDCTL(value);

				if (delta & 0x18)
					RecomputeAllowedDeferredTimers();

				if (delta & 0x51) {
					RecomputeTimerPeriod<0>();
					RecomputeTimerPeriod<1>();
				}

				if (delta & 0x29) {
					RecomputeTimerPeriod<2>();
					RecomputeTimerPeriod<3>();
				}

				SetupTimers(0x0f);
			}
			break;
		case 0x09:	// $D209 STIMER
			mpScheduler->SetEvent(3, this, kATPokeyEventResetTimers, mpResetTimersEvent);
			break;
		case 0x0A:	// $D20A SKRES
			mSKSTAT |= 0xe0;
			break;
		case 0x0B:	// $D20B POTGO
			StartPotScan();
			break;
		case 0x0D:	// $D20D SEROUT
			if (mbTraceSIO)
				(*mpConsoleOut)("POKEY: Sending serial byte %02X", value);

			// The only thing that writing to SEROUT does is load the register and set a latch
			// indicating that a byte is ready. The actual load into the output shift register
			// cannot occur until another serial clock pulse arrives.
			mSEROUT = value;
			if (mbSerOutValid && mbTraceSIO)
				(*mpConsoleOut)("POKEY: Serial output overrun detected.");

			if (!mSerialOutputCounter) {
				mSerialOutputCounter = 1;

				// check if we are doing external output clock
				ResyncExternalOutputClock();
			}

			mbSerOutValid = true;
			break;
		case 0x0E:
			if (mIRQEN != value) {
				const uint8 delta = (mIRQEN ^ value);

				mIRQEN = value;

				mIRQST |= ~value & 0xF7;

				if (mbBreakKeyState && (mIRQEN & mIRQST & 0x80)) {
					mIRQST |= 0x80;
				}

				if (!(mIRQEN & ~mIRQST))
					NegateIrq(true);
				else
					AssertIrq(true);

				// Check if any of the IRQ bits are being turned on and we are currently running that timer
				// in deferred mode. If so, we need to yank it out of deferred mode. We don't do the
				// opposite here; we wait until the existing timer expires to reinit the timer into
				// deferred mode so as to not trigger on momentary clears.
				if (delta & 0x07) {
					RecomputeAllowedDeferredTimers();

					if (delta & mIRQEN & 0x07) {
						uint8 timersToChange = 0;

						// timer 1
						if ((delta & 0x01) && mbDeferredTimerEvents[0])
							timersToChange |= 0x01;

						// timer 2
						if ((delta & 0x02) && mbDeferredTimerEvents[1])
							timersToChange |= 0x02;

						// timer 4
						if ((delta & 0x04) && mbDeferredTimerEvents[3])
							timersToChange |= 0x08;

						if (timersToChange) {
							UpdateTimerCounter<0>();
							UpdateTimerCounter<1>();
							UpdateTimerCounter<2>();
							UpdateTimerCounter<3>();

							SetupTimers(timersToChange);
						}
					}
				}

				// check if the receive interrupt was toggled
				if (delta & 0x20) {
					if (value & 0x20) {
						if (mbSerInBurstPendingIRQ2) {
							mbSerInBurstPendingIRQ2 = false;
							
							if (!mbSerInBurstPendingData) {
								for(IATPokeySIODevice *dev : mDevices) {
									if (mbSerInBurstPendingData)
										break;

									dev->PokeySerInReady();
								}
							}
						}
					} else {
						if (mbSerInBurstPendingIRQ1) {
							mbSerInBurstPendingIRQ1 = false;
							mbSerInBurstPendingIRQ2 = true;
						}
					}
				}
			}
			break;
		case 0x0F:
			if (value != mSKCTL) {
				UpdateTimerCounter<0>();
				UpdateTimerCounter<1>();
				UpdateTimerCounter<2>();
				UpdateTimerCounter<3>();

				if (!(mSKCTL & 0x10) && (value & 0x10) && mbSerialWaitingForStartBit) {
					// Restart timers 3 and 4 immediately.
					mCounter[2] = GetAUDFP1<2>();
					mCounter[3] = GetAUDFP1<3>();
				}

				const uint8 delta = value ^ mSKCTL;
				const bool prvInit = (mSKCTL & 3) == 0;
				const bool newInit = (value & 3) == 0;
				const bool initChanged = (newInit != prvInit);

				// update pots if fast pot scan mode or init mode has changed
				if (initChanged || (delta & 0x04)) {
					// A time skew of two cycles is necessary to handle this case properly:
					//
					// STA POTGO
					// STY SKCTL   ;enable fast pot scan mode
					//
					// The counter is delayed by two cycles, but so is the change to fast mode.
					UpdatePots(2);
				}

				// force serial rate re-evaluation if any clocking mode bits have changed
				if (delta & 0x70) {
					mbSerialRateChanged = true;

					// 000 clocking mode also resets internal serial clock phase flip-flop
					if (!(value & 0x70))
						mbSerClockPhase = false;
				}

				if (initChanged) {
					if (newInit) {
						mpScheduler->UnsetEvent(mpKeyboardScanEvent);
						mpScheduler->UnsetEvent(mpKeyboardIRQEvent);

						// Don't reset poly counters at this point -- we need to keep them going in
						// order to emulate the shift-out.
						mPolyShutOffTime = mpScheduler->GetTick64();
					} else {
						VDASSERT(!mpKeyboardScanEvent);

						// The 64KHz polynomial counter is a 5-bit LFSR that is reset to all 1s
						// on init and XORs bits 4 and 2, if seen as shifting left. In order to
						// achieve a 28 cycle period, a one is force fed when the register
						// equals 00010. A side effect of this mechanism is that resetting the
						// register actually starts it 19 cycles in. There is, however, an
						// additional two clock delay from the shift register comparator to
						// the timers.

						// The 15KHz polynomial counter is a 7-bit LFSR that is reset to all 0s
						// on init and XNORs bits 7 and 6, if seen as shifting left. In order to
						// achieve a 114 cycle period, a one is force fed when the register
						// equals 1001001.

						mLast15KHzTime = ATSCHEDULER_GETTIME(mpScheduler) + 81 - 114;
						mLast64KHzTime = ATSCHEDULER_GETTIME(mpScheduler) + 22 - 28;

						mPoly9Counter = 0;
						mPoly17Counter = 0;
						mLastPolyTime = ATSCHEDULER_GETTIME(mpScheduler) + 1;

						mPotLastTimeSlow = mLast15KHzTime;
					}

					mpRenderer->SetInitMode(newInit);

					// Init mode effects on the serial subsystem:
					//
					// - resets input and output state machines
					// - resets the input shift register (the data is moot, but this also acts
					//   as the bit counter)
					// - does NOT reset the output shift register; it stops shifting but the
					//   output state is preserved, which is important for two-tone mode
					// - does NOT reset SERIN or SEROUT
					//
					mSerialInputShiftRegister = 0;
					mSerialOutputShiftRegister = 0;
					mSerialOutputCounter = 0;
					mSerialInputCounter = 0;
					mbSerOutValid = false;
					mbSerShiftValid = false;

					// reset burst state
					mbSerInBurstPendingData = false;
					mbSerInBurstPendingIRQ1 = false;
					mbSerInBurstPendingIRQ2 = false;
					mSerOutBurstDeadline = 0;

					// reset serial input active bit
					mSKSTAT |= 0x02;

					// assert serial output complete
					mIRQST &= ~0x08;

					if (!(mIRQEN & ~mIRQST))
						NegateIrq(true);
					else
						AssertIrq(true);

					if (mpCassette)
						mpCassette->PokeyResetSerialInput();

					++mSerialInputResetCounter;
				}

				mSKCTL = value;

				// if the pot scan clocking mode was changed during pot scan,
				// check if the slow time needs to be updated
				if ((delta & 0x04) && !(value & 0x04))
					mPotLastTimeSlow = UpdateLast15KHzTime();

				// check for bidirectional clock change
				if (mbNotifyBiClockChange && (delta & 0x30)) {
					if ((value & 0x30) == 0x20 || ((value ^ delta) & 0x30) == 0x20)
						mpNotifyBiClockChangeFn();
				}

				// check for keyboard change
				if (delta & 0x03) {
					// check for keyboard being switched in and out of normal mode, for
					// immediate scan (raw mode)
					if (!mb5200Mode && !mbKeyboardScanEnabled) {
						if (!(value & 1)) {
							// Debounce is being turned off. We can't register any keys
							// and will stop reporting a current one.
							mSKSTAT |= 0x04;

							mKeyScanState = 0;
							mKeyScanCode = 0;
						} else if ((value & 3) == 3) {
							// Returning to normal scan. Check if we have a key held and
							// register it now.
							if (mbShiftKeyState)
								mSKSTAT &= ~0x08;
							else
								mSKSTAT |= 0x08;

							for(int i=0; i<8; ++i) {
								if (mKeyMatrix[i] & 0xff) {
									mSKSTAT &= ~0x04;
									mKBCODE = (uint8)(
											i*8
										+ VDFindLowestSetBitFast(mKeyMatrix[i] & 0xFF)
										+ (mbShiftKeyState ? 0x40 : 0x00)
										+ (mbControlKeyState ? 0x80 : 0x00)
										);
									QueueKeyboardIRQ();
								}
							}
						}
					}

					// check for change in keyboard scan
					if (delta & 0x02) {
						if (!(value & 0x02)) {
							// Keyboard scan is being disabled -- this resets the keyboard state machine
							// and so bit 2 deasserts. However, this doesn't affect the shift key state
							// (bit 3).
							mSKSTAT |= 0x04;

							mKeyScanState = 0;
							mKeyScanCode = 0;
						}

						UpdateKeyboardScanEvent();
					}
				}

				RecomputeAllowedDeferredTimers();
				SetupTimers(0x0f);

				// check if serial timer is stopped and terminate output byte if needed
				if (!IsSerialOutputClockRunning())
					FlushSerialOutput();

				if (delta & 0x80)
					NotifyForceBreak();
			}
			break;

		default:
			if (reg & 0x10) {
				mpSlave->WriteByte(reg & 0x0f, value);
			}
			break;
	}
}

void ATPokeyEmulator::DumpStatus(ATConsoleOutput& out) {
	if (mpSlave) {
		out.WriteLine("Primary POKEY:");
		DumpStatus(out, false);
		out.WriteLine("");
		out.WriteLine("Secondary POKEY:");
		mpSlave->DumpStatus(out, true);
	} else {
		DumpStatus(out, false);
	}
}

void ATPokeyEmulator::SaveState(IATObjectState **pp) {
	UpdateTimerCounter<0>();
	UpdateTimerCounter<1>();
	UpdateTimerCounter<2>();
	UpdateTimerCounter<3>();
	UpdatePots(0);

	vdrefptr<ATSaveStatePokey> obj(new ATSaveStatePokey);
	vdrefptr<ATSaveStatePokeyInternal> obj2(new ATSaveStatePokeyInternal);

	for(int i=0; i<4; ++i) {
		obj->mAUDF[i] = mAUDF[i];
		obj->mAUDC[i] = mAUDC[i];
	}

	obj->mAUDCTL = mAUDCTL;
	obj->mIRQEN = mIRQEN;
	obj->mIRQST = mIRQST;
	obj->mSKCTL = mSKCTL;
	obj->mSKSTAT = mSKSTAT;
	obj->mALLPOT = mALLPOT;
	obj->mKBCODE = mKBCODE;

	for(int i=0; i<8; ++i)
		obj->mPOT[i] = mPotLatches[i];

	for(int i=0; i<4; ++i)
		obj2->mTimerCounters[i] = mCounter[i];

	for(int i=0; i<4; ++i)
		obj2->mTimerBorrowCounters[i] = mCounterBorrow[i];

	obj2->mTwoToneResetCounters[0] = mpEventResetTwoTones1 ? mpScheduler->GetTicksToEvent(mpEventResetTwoTones1) : 0;
	obj2->mTwoToneResetCounters[1] = mpEventResetTwoTones2 ? mpScheduler->GetTicksToEvent(mpEventResetTwoTones2) : 0;

	const uint32 t = ATSCHEDULER_GETTIME(mpScheduler);
	obj2->mClock15Offset = t - UpdateLast15KHzTime(t);
	VDASSERT(obj2->mClock15Offset < 114);

	obj2->mClock64Offset = t - UpdateLast64KHzTime(t);
	VDASSERT(obj2->mClock64Offset < 28);

	int polyDelta = t - mLastPolyTime;
	obj2->mPoly9Offset = (mPoly9Counter + polyDelta) % 511;
	obj2->mPoly17Offset = (mPoly17Counter + polyDelta) % 131071;
	obj2->mPolyShutOffTime = mpScheduler->GetTick64() - mPolyShutOffTime;
	obj2->mSerInCounter = mSerialInputCounter;
	obj2->mSerInShiftRegister = mSerialInputShiftRegister;
	obj2->mbSerInDeferredLoad = mbSerInDeferredLoad;
	obj2->mbSerInWaitingForStartBit = mbSerialWaitingForStartBit;

	obj2->mSerOutEventTime = mpEventSerialOutput ? mpScheduler->GetTicksToEvent(mpEventSerialOutput) : 0;
	obj2->mSerOutShiftRegister = mSerialOutputShiftRegister;
	obj2->mSerOutCounter = mSerialOutputCounter ? (mSerialOutputCounter << 1) - (mbSerClockPhase ? 0 : 1) : 0;
	obj2->mbSerOutValid = mbSerOutValid;
	obj2->mbSerOutShiftValid = mbSerShiftValid;

	obj2->mbSerClockPhase = mbSerClockPhase;

	obj2->mTraceByteIndex = mTraceByteIndex;

	vdcopy_checked_r(obj2->mPotChargeLevels, mPotChargeLevels);
	obj2->mPotMasterCounter = mPotMasterCounter;
	obj2->mbPotScanActive = mbPotScanActive;

	obj2->mRendererState = mpRenderer->SaveState();

	obj->mpInternalState = std::move(obj2);

	if (mpSlave)
		mpSlave->SaveState(~obj->mpStereoPair);

	*pp = obj.release();
}

void ATPokeyEmulator::LoadState(const IATObjectState *state) {
	if (state)
		LoadState(atser_cast<const ATSaveStatePokey&>(*state));
	else
		LoadState(ATSaveStatePokey{});
}

void ATPokeyEmulator::LoadState(const ATSaveStatePokey& pstate) {
	for(int i=0; i<4; ++i) {
		mAUDF[i] = pstate.mAUDF[i];
		mAUDC[i] = pstate.mAUDC[i];
	}

	mAUDCTL = pstate.mAUDCTL;
	mIRQEN = pstate.mIRQEN;
	mIRQST = pstate.mIRQST;
	mSKCTL = pstate.mSKCTL;

	// SKSTAT is a bit tricky to restore due to having a mix of various states.
	//
	// D7 - serial input framing error (restore)
	// D6 - keyboard overrun (restore)
	// D5 - serial input overrun (restore)
	// D4 - serial input line (do not restore - upstreams will restore)
	// D3 - latched shift key state (restore if full keyboard scan)
	// D2 - key down state (restore if full keyboard scan)
	// D1 - serial input active (restore)
	// D0 - always set (do not restore)
	//
	// Versions through 4.10 did not save SKSTAT properly. We detect this by a
	// value of 0, which is not normally possible since D0 always reads 1.
	const uint8 correctedSKSTAT = pstate.mSKSTAT ? pstate.mSKSTAT : 0x7F;
	mSKSTAT = (mSKSTAT & 0x09) + (correctedSKSTAT & 0xF6);
	mbShiftKeyLatchedState = !(mSKSTAT & 0x08);

	mALLPOT = pstate.mALLPOT;
	mKBCODE = pstate.mKBCODE;

	const uint64 t64 = mpScheduler->GetTick64();
	const uint32 t = (uint32)t64;
	mLastPolyTime = t;

	for(auto& c : mCounter)
		c = 1;

	for(auto& c : mCounterBorrow)
		c = 0;

	mLast15KHzTime = t - 1;
	mLast64KHzTime = t - 1;
	mPoly9Counter = 0;
	mPoly17Counter = 0;
	mPolyShutOffTime = t64 - 1000;
	mbSerInDeferredLoad = false;
	mbSerialWaitingForStartBit = true;
	mSerialInputCounter = 0;
	mSerialOutputCounter = 0;
	mSerialInputShiftRegister = 0;
	mSerialOutputShiftRegister = 0;
	mbSerClockPhase = false;

	mpScheduler->UnsetEvent(mpEventResetTwoTones1);
	mpScheduler->UnsetEvent(mpEventResetTwoTones2);
	mpScheduler->UnsetEvent(mpEventSerialInput);
	mpScheduler->UnsetEvent(mpEventSerialOutput);

	mTraceByteIndex = 0;

	for(int i=0; i<8; ++i) {
		mPotLatches[i] = std::min<uint8>(pstate.mPOT[i], 229);
		mPotChargeLevels[i] = 0;
	}

	mbPotScanActive = false;
	mPotMasterCounter = 0;

	if (pstate.mpInternalState) {
		const ATSaveStatePokeyInternal& pistate = *pstate.mpInternalState;

		for(int i=0; i<4; ++i)
			mCounter[i] = pistate.mTimerCounters[i];

		for(int i=0; i<4; ++i)
			mCounterBorrow[i] = pistate.mTimerBorrowCounters[i];

		mLast15KHzTime = t - pistate.mClock15Offset;
		mLast64KHzTime = t - pistate.mClock64Offset;

		mPoly9Counter = pistate.mPoly9Offset;
		mPoly17Counter = pistate.mPoly17Offset;
		mPolyShutOffTime = t64 - pistate.mPolyShutOffTime;

		mSerialInputCounter = pistate.mSerInCounter;
		mSerialInputShiftRegister = pistate.mSerInShiftRegister;
		mbSerInDeferredLoad = pistate.mbSerInDeferredLoad;
		mbSerialWaitingForStartBit = pistate.mbSerInWaitingForStartBit;

		if (pistate.mSerOutEventTime)
			mpScheduler->SetEvent(pistate.mSerOutEventTime, this, kATPokeyEventSerialOutput, mpEventSerialOutput);

		mSerialOutputShiftRegister = pistate.mSerOutShiftRegister;
		mSerialOutputCounter = std::min<uint8>((pistate.mSerOutCounter + 1) >> 1, 20);
		mbSerOutValid = pistate.mbSerOutValid;
		mbSerShiftValid = pistate.mbSerOutShiftValid;

		mbSerClockPhase = pistate.mbSerClockPhase;

		if (pistate.mTwoToneResetCounters[0])
			mpScheduler->SetEvent(pistate.mTwoToneResetCounters[0], this, kATPokeyEventResetTwoTones1, mpEventResetTwoTones1);

		if (pistate.mTwoToneResetCounters[1])
			mpScheduler->SetEvent(pistate.mTwoToneResetCounters[1], this, kATPokeyEventResetTwoTones2, mpEventResetTwoTones2);

		mTraceByteIndex = pistate.mTraceByteIndex;

		for(int i=0; i<8; ++i)
			mPotChargeLevels[i] = std::min<uint32>(kPotChargeLimit, pistate.mPotChargeLevels[i]);

		mbPotScanActive = pistate.mbPotScanActive;

		if (!mbPotScanActive)
			mALLPOT = 0;

		// The master counter only counts to 228 for slow pot scan mode at stops at
		// 229 for fast pot scan mode, but there is a chance that we update to
		// fast 229 right when the save state occurs.
		mPotMasterCounter = (uint8)std::min<uint32>(pistate.mPotMasterCounter, 229);

		mpRenderer->LoadState(pistate.mRendererState);
	} else {
		mpRenderer->LoadState({});
	}

	if (mpSlave)
		mpSlave->LoadState(pstate.mpStereoPair);

	PostLoadState();
}

void ATPokeyEmulator::PostLoadState() {
	const uint32 t = ATSCHEDULER_GETTIME(mpScheduler);

	mbFastTimer1 = (mAUDCTL & 0x40) != 0;
	mbFastTimer3 = (mAUDCTL & 0x20) != 0;
	mbLinkedTimers12 = (mAUDCTL & 0x10) != 0;
	mbLinkedTimers34 = (mAUDCTL & 0x08) != 0;
	mbUse15KHzClock = (mAUDCTL & 0x01) != 0;

	mpRenderer->SetInitMode((mSKCTL & 3) == 0);
	mpRenderer->SetAUDCTL(mAUDCTL);
	for(int i=0; i<4; ++i) {
		mAUDFP1A[i] = mAUDF[i] + 1;
		mAUDFP1B[i] = mAUDF[i] + 1;
		mpRenderer->SetAUDCx(i, mAUDC[i]);
	}

	mLastPolyTime = t;

	bool newIrqAsserted = (mIRQEN & ~mIRQST) != 0;
	mbIrqAsserted = !newIrqAsserted;

	if (newIrqAsserted)
		AssertIrq(false);
	else
		NegateIrq(false);


	uint32 keyboardTickOffset = 114 - (t - UpdateLast15KHzTime());

	if (mbKeyboardIRQPending)
		mpScheduler->SetEvent(keyboardTickOffset, this, kATPokeyEventKeyboardIRQ, mpKeyboardIRQEvent);
	else
		mpScheduler->UnsetEvent(mpKeyboardIRQEvent);

	UpdateKeyboardScanEvent();

	RecomputeTimerPeriod<0>();
	RecomputeTimerPeriod<1>();
	RecomputeTimerPeriod<2>();
	RecomputeTimerPeriod<3>();
	RecomputeAllowedDeferredTimers();

	SetupTimers(0x0f);

	if (mpSlave)
		mpSlave->PostLoadState();

	NotifyForceBreak();
}

void ATPokeyEmulator::GetRegisterState(ATPokeyRegisterState& state) const {
	state = mState;
}

uint32 ATPokeyEmulator::GetNetplayDeterminismFingerprint() const {
	// FNV-1a 32 — same family used by netplay_simhash.cpp so the
	// folded result composes cleanly into the master sim hash.
	constexpr uint32 kFnvOff = 0x811C9DC5u;
	constexpr uint32 kFnvP   = 0x01000193u;

	auto fold8 = [](uint32 h, uint8 b) -> uint32 {
		h ^= b;
		h *= kFnvP;
		return h;
	};
	auto fold32 = [&](uint32 h, uint32 v) -> uint32 {
		for (int i = 0; i < 4; ++i)
			h = fold8(h, (uint8)((v >> (i * 8)) & 0xFFu));
		return h;
	};

	uint32 h = kFnvOff;

	// Poly LFSR positions, advanced to the current tick.  We
	// intentionally compute the live counter values without mutating
	// state — the savestate code does the same thing
	// (pokey.cpp:3066-3068).
	const uint32 t32 = ATSCHEDULER_GETTIME(mpScheduler);
	const uint32 polyDelta = t32 - mLastPolyTime;
	h = fold32(h, (mPoly9Counter  + polyDelta) % 511);
	h = fold32(h, (mPoly17Counter + polyDelta) % 131071);

	// 15 kHz / 64 kHz clock phase, expressed as cycles since the
	// most recent slow-clock tick.  Both values are < their period
	// (114 / 28) by construction, so this captures phase deterministically.
	h = fold32(h, t32 - mLast15KHzTime);
	h = fold32(h, t32 - mLast64KHzTime);

	// NOTE: mPolyShutOffTime is deliberately NOT folded in.  ColdReset
	// does not reset it (it's only updated when SKCTL transitions out
	// of poly init mode, pokey.cpp:2860), so two peers with different
	// pre-session SKCTL activity carry forward different absolute-tick
	// values across the lockstep-entry rebase.  The field affects
	// only the RANDOM register's force-mask in init mode (offset > 10
	// branch returns 0xFF regardless), so divergence in this field
	// has no observable emulation effect during boot.

	// Timer counters & borrows.  Stored as int but the value range is
	// non-negative; cast safely to uint32 for the fold.
	for (int i = 0; i < 4; ++i) {
		h = fold32(h, (uint32)mCounter[i]);
		h = fold32(h, (uint32)mCounterBorrow[i]);
	}

	// Audio control regs.
	for (int i = 0; i < 4; ++i) {
		h = fold8(h, mAUDF[i]);
		h = fold8(h, mAUDC[i]);
	}
	h = fold8(h, mAUDCTL);

	// IRQ + serial / keyboard ctrl/status regs.
	h = fold8(h, mIRQEN);
	h = fold8(h, mIRQST);
	h = fold8(h, mSKCTL);
	h = fold8(h, mSKSTAT);

	// Serial in/out shift state — transient but determinism-relevant.
	h = fold8(h, mSerialInputShiftRegister);
	h = fold8(h, mSerialOutputShiftRegister);
	h = fold8(h, mSerialInputCounter);
	h = fold8(h, mSerialOutputCounter);
	h = fold8(h, mbSerOutValid          ? 1 : 0);
	h = fold8(h, mbSerShiftValid        ? 1 : 0);
	h = fold8(h, mbSerialWaitingForStartBit ? 1 : 0);
	h = fold8(h, mbSerInDeferredLoad    ? 1 : 0);

	// Pot scan progress.
	h = fold8(h, mPotMasterCounter);
	h = fold8(h, mbPotScanActive ? 1 : 0);

	// Recurse into the slave POKEY (stereo configurations) so its
	// state contributes too.  Avoid infinite recursion: a slave POKEY
	// has mpSlave == nullptr and mbIsSlave == true.
	if (mpSlave && !mbIsSlave)
		h = fold32(h, mpSlave->GetNetplayDeterminismFingerprint());

	if (h == 0) h = 1;
	return h;
}

void ATPokeyEmulator::DescribeNetplayDeterminismFingerprint(
	VDStringA& out) const
{
	const uint32 t32 = ATSCHEDULER_GETTIME(mpScheduler);
	const uint32 polyDelta = t32 - mLastPolyTime;
	const uint32 poly9Live  = (mPoly9Counter  + polyDelta) % 511;
	const uint32 poly17Live = (mPoly17Counter + polyDelta) % 131071;

	out.append_sprintf(
		"t32=%08x poly9=%u poly17=%u "
		"k15dt=%u k64dt=%u "
		"cnt=%d,%d,%d,%d brw=%d,%d,%d,%d "
		"AUDF=%02X,%02X,%02X,%02X AUDC=%02X,%02X,%02X,%02X "
		"AUDCTL=%02X IRQEN=%02X IRQST=%02X SKCTL=%02X SKSTAT=%02X "
		"siSR=%02X soSR=%02X siC=%u soC=%u "
		"sof=%d ssh=%d swsb=%d sidl=%d "
		"potC=%u potA=%d slave=%d",
		(unsigned)t32, (unsigned)poly9Live, (unsigned)poly17Live,
		(unsigned)(t32 - mLast15KHzTime),
		(unsigned)(t32 - mLast64KHzTime),
		mCounter[0], mCounter[1], mCounter[2], mCounter[3],
		mCounterBorrow[0], mCounterBorrow[1],
		mCounterBorrow[2], mCounterBorrow[3],
		mAUDF[0], mAUDF[1], mAUDF[2], mAUDF[3],
		mAUDC[0], mAUDC[1], mAUDC[2], mAUDC[3],
		mAUDCTL, mIRQEN, mIRQST, mSKCTL, mSKSTAT,
		mSerialInputShiftRegister, mSerialOutputShiftRegister,
		(unsigned)mSerialInputCounter,
		(unsigned)mSerialOutputCounter,
		mbSerOutValid ? 1 : 0,
		mbSerShiftValid ? 1 : 0,
		mbSerialWaitingForStartBit ? 1 : 0,
		mbSerInDeferredLoad ? 1 : 0,
		(unsigned)mPotMasterCounter,
		mbPotScanActive ? 1 : 0,
		(mpSlave && !mbIsSlave) ? 1 : 0);

	if (mpSlave && !mbIsSlave) {
		out += " | slave: ";
		mpSlave->DescribeNetplayDeterminismFingerprint(out);
	}
}

void ATPokeyEmulator::DumpStatus(ATConsoleOutput& out, bool isSlave) {
	uint32 t = ATSCHEDULER_GETTIME(mpScheduler);

	UpdatePots(0);

	VDStringA s;
	for(int i=0; i<4; ++i) {
		s.sprintf("AUDF%u: %02X  AUDC%u: %02X  Output: %d", i+1, mAUDF[i], i+1, mAUDC[i], mpRenderer->GetChannelOutput(i));

		if (mbDeferredTimerEvents[i]) {
			uint32 delay;

			if (mDeferredTimerStarts[i] - t - 1 < 0x7FFFFFFF)		// wrap(start > t) => wrap(start - t) > 0 => 0 < (start - t) < 80000000
				delay = mDeferredTimerStarts[i] - t;
			else
				delay = mDeferredTimerPeriods[i] - (t - mDeferredTimerStarts[i]) % mDeferredTimerPeriods[i];

			s.append_sprintf("  (%u cycles until fire) (passive: %d cycles)", delay, mDeferredTimerPeriods[i]);
		} else if (mpTimerBorrowEvents[i])
			s.append_sprintf("  (%u cycles until fire) (active)", (int)mpScheduler->GetTicksToEvent(mpTimerBorrowEvents[i]));

		out.WriteLine(s.c_str());
	}

	out("AUDCTL: %02X%s%s%s%s%s%s%s%s"
		, mAUDCTL
		, mAUDCTL & 0x80 ? ", 9-bit poly" : ", 17-bit poly"
		, mAUDCTL & 0x40 ? ", 1.79 ch1" : ""
		, mAUDCTL & 0x20 ? ", 1.79 ch3" : ""
		, mAUDCTL & 0x10 ? ", ch1+ch2" : ""
		, mAUDCTL & 0x08 ? ", ch3+ch4" : ""
		, mAUDCTL & 0x04 ? ", highpass 1+3" : ""
		, mAUDCTL & 0x02 ? ", highpass 2+4" : ""
		, mAUDCTL & 0x01 ? ", 15KHz" : ", 64KHz");

	static const char *kRecvModes[4]={
		"recv ext",
		"recv ch3+4 async",
		"recv ch4",
		"recv ch3+4 async",
	};

	static const char *kSendModes[8]={
		"send ext",
		"send ext",
		"send ch4",
		"send ch4 async (x)",
		"send ch4",
		"send ch4 async (x)",
		"send ch2",
		"send ch2",
	};

	static const char *kInitModes[4]={
		"init mode",
		"keyboard scan disabled",
		"keyboard scan enabled w/o debounce",
		"keyboard scan enabled",
	};

	out("SKCTL: %02X | %s | %s | %s | %s%s%s"
		, mSKCTL
		, kRecvModes[(mSKCTL >> 4) & 3]
		, kSendModes[(mSKCTL >> 4) & 7]
		, kInitModes[mSKCTL & 3]
		, mSKCTL & 0x80 ? " | force break" : ""
		, mSKCTL & 0x08 ? " | two-tone mode" : ""
		, mSKCTL & 0x04 ? " | fast pot scan" : ""
		);

	if (!(mSKCTL & 0x30) || !(mSKCTL & 0x60)) {
		if (mSerialExtPeriod)
			out("External clock: %d cycles/bit", mSerialExtPeriod);
		else
			out("External clock: enabled, but not present");
	}

	if (mSerialInputCounter)
		out("SERIN: %02X (shifting in %02X)", mSERIN, mSerialInputShiftRegister);
	else
		out("SERIN: %02X", mSERIN);

	out("SEROUT: %02X (%s)", mSEROUT, mbSerOutValid ? "pending" : "done");
	out("        shift register %02X (%d: %s)", mSerialOutputShiftRegister, mSerialOutputCounter, mSerialOutputCounter ? "pending" : "done");
	out("IRQEN:  %02X%s%s%s%s%s%s%s%s"
		, mIRQEN
		, mIRQEN & 0x80 ? ", break key" : ""
		, mIRQEN & 0x40 ? ", keyboard" : ""
		, mIRQEN & 0x20 ? ", serin" : ""
		, mIRQEN & 0x10 ? ", serout" : ""
		, mIRQEN & 0x08 ? ", sertrans" : ""
		, mIRQEN & 0x04 ? ", timer4" : ""
		, mIRQEN & 0x02 ? ", timer2" : ""
		, mIRQEN & 0x01 ? ", timer1" : ""
		);
	out("IRQST:  %02X%s%s%s%s%s%s%s%s"
		, mIRQST
		, mIRQST & 0x80 ? "" : ", break key"
		, mIRQST & 0x40 ? "" : ", keyboard"
		, mIRQST & 0x20 ? "" : ", serin"
		, mIRQST & 0x10 ? "" : ", serout"
		, mIRQST & 0x08 ? "" : ", sertrans"
		, mIRQST & 0x04 ? "" : ", timer4"
		, mIRQST & 0x02 ? "" : ", timer2"
		, mIRQST & 0x01 ? "" : ", timer1"
		);

	s.sprintf("KBCODE: %02X", mKBCODE);

	if (mpKeyboardScanEvent)
		s.append_sprintf(" (keyboard scan counter: %02X)", mKeyScanCode);

	out.WriteLine(s.c_str());

	s.sprintf("ALLPOT: %02X", mALLPOT);

	if (mbPotScanActive) {
		s.append_sprintf(" | pot scan %d/228 (%s)", mPotMasterCounter, mSKCTL & 4 ? "fast" : "slow");
	} else {
		if (mSKCTL & 4)
			s += " | pot scan inactive (dump transistors disabled)";
		else
			s += " | pot scan inactive (dump transistors enabled)";
	}

	out.WriteLine(s.c_str());

	s = "Pot positions: ";

	for(int i=0; i<8; ++i) {
		const auto hiPos = mPotHiPositions[i];

		if (hiPos == kPotHiPosGrounded)
			s += " -gnd-";
		else if (hiPos == kPotHiPosUnconnected)
			s += "   -  ";
		else
			s.append_sprintf(" %5.1f", hiPos / 65536.0f);
	}
	
	out.WriteLine(s.c_str());

	out("Pot capacitors: %5.3f %5.3f %5.3f %5.3f %5.3f %5.3f %5.3f %5.3f"
		, (float)mPotChargeLevels[0] / (float)kPotChargeThreshold
		, (float)mPotChargeLevels[1] / (float)kPotChargeThreshold
		, (float)mPotChargeLevels[2] / (float)kPotChargeThreshold
		, (float)mPotChargeLevels[3] / (float)kPotChargeThreshold
		, (float)mPotChargeLevels[4] / (float)kPotChargeThreshold
		, (float)mPotChargeLevels[5] / (float)kPotChargeThreshold
		, (float)mPotChargeLevels[6] / (float)kPotChargeThreshold
		, (float)mPotChargeLevels[7] / (float)kPotChargeThreshold
	);

	out.WriteLine("");
	out("Command line: %s", mbCommandLineState ? "asserted" : "negated");
}

void ATPokeyEmulator::FlushAudio(bool pushAudio, uint64 timestamp) {
	VDFastMathScope fastMathScope;

	IATSyncAudioEdgePlayer *edgePlayer = mpAudioOut ? &mpAudioOut->AsMixer().GetEdgePlayer() : nullptr;
	const auto endBlockResult = mpRenderer->EndBlock(edgePlayer);

	if (mpSlave) {
		const auto slaveEndBlockResult = mpSlave->mpRenderer->EndBlock(edgePlayer);

		VDASSERT(endBlockResult.mSamples == slaveEndBlockResult.mSamples);
		VDASSERT(endBlockResult.mTimestamp == slaveEndBlockResult.mTimestamp);
	}

	if (mpAudioOut) {
		// convert 32-bit timestamp to 64-bit
		const uint64 now64 = mpScheduler->GetTick64();
		uint64 t64 = (now64 & ~UINT64_C(0xFFFFFFFF)) + endBlockResult.mTimestamp;

		if (t64 > now64 + UINT64_C(0x80000000))
			t64 -= UINT64_C(1) << 32;

		mpAudioOut->WriteAudio(
			mpRenderer->GetOutputBuffer(),
			mpSlave && mbStereoSoftEnable ? mpSlave->mpRenderer->GetOutputBuffer() : NULL,
			endBlockResult.mSamples,
			pushAudio,
			mbStereoAsMono,
			t64);
	}

	mpRenderer->StartBlock();

	if (mpSlave)
		mpSlave->mpRenderer->StartBlock();
}

void ATPokeyEmulator::SetTraceOutput(IATPokeyTraceOutput *output) {
	mpTraceOutput = output;
	mbTraceIrqPending = false;
}

uint32 ATPokeyEmulator::GetCyclesToTimerFire(uint32 ch) const {
	VDASSERT(ch < 4);

	if (!mbDeferredTimerEvents[ch])
		return mpTimerBorrowEvents[ch] ? mpScheduler->GetTicksToEvent(mpTimerBorrowEvents[ch]) : 0;

	uint32 t = ATSCHEDULER_GETTIME(mpScheduler);
	if ((uint32)(mDeferredTimerStarts[ch] - t - 1) < 0x7FFFFFFF)		// wrap(start > t) => wrap(start - t) > 0 => 0 < (start - t) < 80000000
		return mDeferredTimerStarts[ch] - t;
	else
		return mDeferredTimerPeriods[ch] - (t - mDeferredTimerStarts[ch]) % mDeferredTimerPeriods[ch];
}

bool ATPokeyEmulator::IsSerialForceBreakEnabled() const {
	return (mSKCTL & 0x80) != 0;
}

void ATPokeyEmulator::SetNotifyOnBiClockChange(vdfunction<void()> fn) {
	mbNotifyBiClockChange = fn != nullptr;
	mpNotifyBiClockChangeFn = std::move(fn);
}

void ATPokeyEmulator::UpdateMixTable() {
	static constexpr float kHalfCycleAccumulationFactor = 1.0f;

	if (!mpTables || mbIsSlave)
		return;

	if (mbNonlinearMixingEnabled) {
		// Approximate measured voltage drops from individual POKEY volume bits, normalized so that
		// maximum volume on all four channels adds to 1. These are supposed to be uneven, as they
		// are on the actual hardware.
		static constexpr float kATPokeyVolumeBitLevels[4] = {
			0.12f / 8.24f,
			0.26f / 8.24f,
			0.56f / 8.24f,
			1.12f / 8.24f,
		};

		// This is an approximate fit to raw sample data obtained from a scope on POKEY's output
		// pin, tapped off before the amplifiers to avoid AC coupling effects. It's a simple
		// exponential curve resulting in gradual saturation, normalized to hit almost [-1, 1]
		// with the saturation clamp below and the x56 half-cycle oversampling.
		static constexpr float kLinearThreshold = 0.14f;
		static constexpr float kLinearSlope = 2.1f;
		static constexpr float kCurveFactor = 2.85f;

		float *dst = mpTables->mMixTable;
		float v23 = 0; 

		for(int i=0; i<13; ++i) {
			float v123 = v23;
			for(int j=0; j<5; ++j) {
				float x = v123;
				for(int k=0; k<5; ++k) {
					*dst++ = (x<kLinearThreshold ? x : kLinearThreshold + (1.0f - expf(-kCurveFactor*(x-kLinearThreshold)))/kCurveFactor) * (kLinearSlope * -kHalfCycleAccumulationFactor);

					x += kATPokeyVolumeBitLevels[0];
				}

				v123 += kATPokeyVolumeBitLevels[1];
			}

			v23 += kATPokeyVolumeBitLevels[2];
		}

		// Exponential decay tables
		//
		// POKEY's output is fed through an AC-coupled output circuit. The decay rate for this
		// circuit is 50% after 1.8ms on actual hardware measurements, giving a time constant
		// of 2.6ms. With our 28 cycle update rate, this means a decay of 0.6% per sample.
		// This is small enough that we can ignore on a cycle basis and just update it per sample.
		//
		static constexpr float kTau = 0.0025968510736001341332478644258f;

		mpTables->mReferenceDecayPerSample = 1.0f - expf(-(1.0f / 63920.0f) / kTau);

		// The amplifier in the analog audio circuit clamps out when the difference on its inputs
		// is too great, until the reference voltage slews enough to reduce the delta. Relative to
		// POKEY's max level, the clamp range is [-0.65, 0.5].

		mpTables->mReferenceClampLo = -0.65f;
		mpTables->mReferenceClampHi = 0.5f;
	} else {
		// When non-linear mixing is disabled, we ignore both the uneven volume bit steps and the
		// saturation curve and just map linearly.

		float *dst = mpTables->mMixTable;
		float v23 = 0; 

		for(int i=0; i<13; ++i) {
			float v123 = v23;
			for(int j=0; j<5; ++j) {
				float v0123 = v123;
				for(int k=0; k<5; ++k) {
					*dst++ = v0123;
					v0123 -= 1.0f / 60.0f * kHalfCycleAccumulationFactor;
				}

				v123 -= 2.0f / 60.0f * kHalfCycleAccumulationFactor;
			}

			v23 -= 4.0f / 60.0f * kHalfCycleAccumulationFactor;
		}

		mpTables->mReferenceDecayPerSample = 0.0f;
		mpTables->mReferenceClampLo = -1.0f;
		mpTables->mReferenceClampHi = 1.0f;
	}

	// The XL/XE speaker is about as loud peak-to-peak as a channel at volume 6.
	// However, it is added in later in the output circuitry and has different
	// audio characteristics, so we must treat it separately. We do need to
	// translate volume level 6 to a mix table index.
	mpTables->mSpeakerLevel = mSpeakerVolOverride >= 0 ? mSpeakerVolOverride : mpTables->mMixTable[30];
}

void ATPokeyEmulator::UpdateKeyMatrix(int index, uint16 mask, uint16 state) {
	uint16 delta = (mKeyMatrix[index] ^ state) & mask;

	if (delta) {
		mKeyMatrix[index] ^= delta;

		UpdateEffectiveKeyMatrix();
	}
}

// Three or more keys pressed at the same time can produce phantom keys
// by connecting column lines to the active row line by other row lines:
//
// 1   .    A<---B
//     .    |    ^
//     .    |    |
// 2 --.----.--->C   active row
//     .    |    |
//     .    |    |
// 3   .    .    .
//     .    |    |
//     .    v    v
//     1    2    3
//        sensed columns
//
// Here the connections formed by depressed keys A, B, and C cause a
// false detection of a fourth key at row 2, column 2. Trickier, however,
// is that such phantom keys can effectively serve as one of the keys
// necessary to produce other phantom keys:
//
// 1   .    A<---B
//     .    |    ^
//     .    |    |
// 2 --E----.--->C
//     |    |    |
//     |    |    |
// 3 --D----.----.   active row
//     .    |    |
//     .    v    v
//     1    2    3
//        sensed columns
//
// In this case, phantom keys at row 2, column 2 and row 3, column 3
// can be directly determined, but an additional phantom key at row
// 3, column 2 is also indirectly produced. This means that we need
// to produce the transitive closure of all interactions to get the
// full set of phantom keys.
//
// Why do we bother with this? Normally, pressing multiple keys does
// nothing because of debounce. However, there are two exceptions.
// First, Ctrl/Shift/Break participate in the matrix but are not
// affected by debounce. This causes Ctrl+Shift+X keys corresponding
// to scan codes $C0-C7 and $D0-D7 to not work. Second, the phantom
// keys ARE visible if debounce is disabled.
//
// To compute the transitive closure, we use an O(N^2) algorithm to
// compute the connected rows, and then an O(N) pass to propagate
// the unified column sets to all rows. Since only a few keys are
// pressed most of the time, we can save a lot of time by doing an
// early out for rows that have no active switches.
// 
void ATPokeyEmulator::UpdateEffectiveKeyMatrix() {
	int srcRows[8];
	bool activeKeys = false;

	memcpy(mEffectiveKeyMatrix, mKeyMatrix, sizeof mEffectiveKeyMatrix);

	// The computer line connects the control (KR2) signal via the same
	// row select lines as the main keyboard on KR1, so both the main
	// keyboard and the Ctrl/Shift/Break keys can interact to create
	// phantom keys on both sides. The 5200 just connects the top button
	// to KR2, so nothing in the main matrix can interfere.
	const uint16 crossConnectMask = mb5200Mode ? 0xFF : 0xFFFF;

	for(int i=0; i<8; ++i) {
		int srcRow = i;
		uint16 connectedColumns = mEffectiveKeyMatrix[i] & crossConnectMask;

		if (connectedColumns) {
			for(int j = i + 1; j < 8; ++j) {
				if (mEffectiveKeyMatrix[j] & connectedColumns) {
					connectedColumns |= mEffectiveKeyMatrix[j];
					srcRow = j;
				}
			}

			mEffectiveKeyMatrix[srcRow] |= connectedColumns & crossConnectMask;

			activeKeys = true;
		}

		srcRows[i] = srcRow;
	}

	if (!activeKeys)
		return;

	for(int i=0; i<8; ++i)
		mEffectiveKeyMatrix[i] |= mEffectiveKeyMatrix[srcRows[i]];
}

bool ATPokeyEmulator::CanPushKey(uint8 scanCode) const {
	// wait if keyboard IRQ is still pending (not necessarily active in IRQST yet!)
	if (mbKeyboardIRQPending)
		return false;

	// wait if keyboard IRQ is still active or disabled
	if (!(mIRQST & mIRQEN & 0x40))
		return false;

	// wait if keyboard scan is disabled
	if ((mSKCTL & 3) != 3)
		return false;

	// wait if cooldown timer is still active to dodge the speaker / keyclick, unless we have
	// credible evidence that the OS can accept another key sooner
	const bool cooldownExpired = (mbUseKeyCooldownTimer && !mKeyCooldownTimer);
	if (!mpConn->PokeyIsKeyPushOK(scanCode, cooldownExpired))
		return false;

	// looks fine to push a new key...
	return true;
}

void ATPokeyEmulator::TryPushNextKey() {
	uint8 c = mKeyQueue.front();
	mKeyQueue.pop_front();

	PushKey(c, false, false, false, mbUseKeyCooldownTimer);
}

void ATPokeyEmulator::SetKeyboardModes(bool cooked, bool scanEnabled) {
	mbCookedKeyMode = cooked;
	mbKeyboardScanEnabled = scanEnabled;

	UpdateKeyboardScanEvent();
}

void ATPokeyEmulator::UpdateKeyboardScanEvent() {
	if ((mb5200Mode || mbKeyboardScanEnabled) && (mSKCTL & 2)) {
		const uint32 t = ATSCHEDULER_GETTIME(mpScheduler);

		mpScheduler->SetEvent(114 - (t - UpdateLast15KHzTime()), this, kATPokeyEventKeyboardScan, mpKeyboardScanEvent);
	} else
		mpScheduler->UnsetEvent(mpKeyboardScanEvent);
}

void ATPokeyEmulator::QueueKeyboardIRQ() {
	const uint32 t = ATSCHEDULER_GETTIME(mpScheduler);
	if (!mpKeyboardIRQEvent)
		mpKeyboardIRQEvent = mpScheduler->AddEvent(114 - (t - UpdateLast15KHzTime()), this, kATPokeyEventKeyboardIRQ);
	mbKeyboardIRQPending = true;
}

void ATPokeyEmulator::AssertKeyboardIRQ() {
	if (mIRQEN & 0x40) {
		// If keyboard IRQ is already active, set the keyboard overrun bit.
		if (!(mIRQST & 0x40))
			mSKSTAT &= ~0x40;

		mIRQST &= ~0x40;
		AssertIrq(false);
	}
}

void ATPokeyEmulator::AssertBreakIRQ() {
	if (mIRQEN & 0x80) {
		mIRQST &= ~0x80;
		AssertIrq(false);
	}
}

void ATPokeyEmulator::AssertIrq(bool cpuBased) {
	if (!mbIrqAsserted) {
		mbIrqAsserted = true;

		mpConn->PokeyAssertIRQ(cpuBased);

		if (mpTraceOutput) {
			const uint64 t = mpScheduler->GetTick64() + (cpuBased ? 0 : -1);
			mTraceIrqStart = t;
			mbTraceIrqPending = true;
		}
	}
}

void ATPokeyEmulator::NegateIrq(bool cpuBased) {
	if (mbIrqAsserted) {
		mbIrqAsserted = false;

		mpConn->PokeyNegateIRQ(cpuBased);

		if (mpTraceOutput && mbTraceIrqPending) {
			mbTraceIrqPending = false;

			const uint64 t = mpScheduler->GetTick64() + (cpuBased ? 0 : -1);

			mpTraceOutput->AddIRQ(mTraceIrqStart, t);
		}
	}
}

void ATPokeyEmulator::ProcessReceivedSerialByte() {
	if (mbTraceSIO)
		(*mpConsoleOut)("POKEY: Reasserting serial input IRQ. IRQEN=%02X, IRQST=%02X", mIRQEN, mIRQST);

	if (mIRQEN & 0x20) {
		// check for overrun
		if (!(mIRQST & 0x20)) {
			if (mSKSTAT & 0x20)
				g_ATLCSIOData <<= "Signaling first serial input overrun.\n";

			mSKSTAT &= 0xdf;

			if (mbTraceSIO)
				(*mpConsoleOut)("POKEY: Serial input overrun detected (c=%02X; %02X %02X)", mSerialInputShiftRegister, mSERIN, mSerialInputShiftRegister);
		}

		mIRQST &= ~0x20;
		AssertIrq(false);
	}

	mSERIN = mSerialInputShiftRegister;
	mSKSTAT &= mSerialInputPendingStatus;
}

void ATPokeyEmulator::SyncRenderers(ATPokeyRenderer *r) {
	mpRenderer->SyncTo(*r);
}

void ATPokeyEmulator::StartPotScan() {
	// Update the pots now in case we're interrupting a scan.
	UpdatePots(0);

	mPotMasterCounter = 0;

	const uint32 fastTime = ATSCHEDULER_GETTIME(mpScheduler);
	const uint32 slowTime = UpdateLast15KHzTime();

	mPotLastScanTime = mpScheduler->GetTick64();
	mPotLastTimeFast = fastTime + 2;
	mPotLastTimeSlow = slowTime;
	mbPotScanActive = true;

	// Set initial ALLPOT. ALLPOT has a 1 bit for every input still below threshold.
	mALLPOT = 0;

	for(int i = 0; i < 8; ++i) {
		if (mPotChargeLevels[i] < kPotChargeThreshold)
			mALLPOT |= (1 << i);
	}
}

void ATPokeyEmulator::UpdatePots(uint32 timeSkew) {
	const uint32 curTime = ATSCHEDULER_GETTIME(mpScheduler);
	const uint32 fastTime = curTime + timeSkew;
	if (ATWrapTime{fastTime} <= mPotLastTimeFast)
		return;

	const uint32 baseTime = mPotLastTimeFast;
	uint32 cyclesToProcess = fastTime - mPotLastTimeFast;
	mPotLastTimeFast = fastTime;

	const bool fastPotScan = (mSKCTL & 4) != 0;

	while(cyclesToProcess) {
		// Check if we're outside of slow pot scan.
		if (!mbPotScanActive) {
			if (fastPotScan) {
				// Scan is done and fast pot scan is on. Keep charging the capacitors
				// since dump capacitors are off.
				for(int i=0; i<8; ++i) {
					const uint32 startingLevel = mPotChargeLevels[i];
					const uint32 chargeRate = mPotChargeRates[i];
					const uint64 chargeDelta = (uint64)chargeRate * cyclesToProcess;

					mPotChargeLevels[i] = (uint32)std::min<uint64>(startingLevel + chargeDelta, kPotChargeLimit);
				}
			} else {
				// Scan is done and fast pot scan is off. The dump capacitors are
				// therefore on, so discharge the caps.
				static const uint32 kMaxDischargeCycles = (kPotChargeLimit - 1) / kPotDischargeRate + 1;

				if (cyclesToProcess >= kMaxDischargeCycles) {
					for(auto& v : mPotChargeLevels)
						v = 0;
				} else {
					uint32 dischargeAmount = kPotDischargeRate * cyclesToProcess;

					for(auto& v : mPotChargeLevels)
						v = std::max<uint32>(v, dischargeAmount) - dischargeAmount;
				}
			}

			// Consume all remaining time.
			break;
		}

		const bool slowPotScanRunning = (mSKCTL & 3) != 0;
		const uint32 slowPotScanCycleOffset = baseTime - mPotLastTimeSlow;
		const uint32 potBaseCounter = mPotMasterCounter;

		// Compute number of cycles to charge. This will be truncated if the pot
		// scan stops early.
		uint32 chargeCycles = cyclesToProcess;

		if (fastPotScan) {
			// The counter shouldn't count above 229/228 for the mode, but we clamp it
			// just to be safe with corner cases around save state.
			uint32 maxFastCycles = mPotMasterCounter < 229 ? 229 - mPotMasterCounter : 0;

			if (chargeCycles >= maxFastCycles) {
				chargeCycles = maxFastCycles;

				mbPotScanActive = false;
				mPotMasterCounter = 229;
			} else {
				mPotMasterCounter += chargeCycles;
			}
		} else if (slowPotScanRunning) {
			// Slot pot scan with clocks running. Count up to 228 every 114
			// cycles + 1 cycle, then stop.

			const uint32 latestSlowTick = UpdateLast15KHzTime(curTime);
			uint32 slowCounts = 0;

			if (latestSlowTick > mPotLastTimeSlow) {
				slowCounts = (latestSlowTick - mPotLastTimeSlow) / 114;
				mPotLastTimeSlow = latestSlowTick;
			}

			const uint32 newRawCounter = mPotMasterCounter + slowCounts;

			if (newRawCounter >= 229 || (newRawCounter == 228 && curTime - latestSlowTick > 0)) {
				mbPotScanActive = false;

				chargeCycles = slowPotScanCycleOffset + 114 * (228 - mPotMasterCounter) + 1;
			}

			mPotMasterCounter = (uint8)std::min<uint32>(228, newRawCounter);
		} else {
			// Slow pot scan with clocks frozen -- caps will charge and pots
			// may latch, but the master counter will not count up and the
			// pot scan will not finish.
		}

		// Charge up all caps, and determine which ones have gone above threshold.
		for(int i=0; i<8; ++i) {
			const uint8 potBit = 1 << i;

			// Skip grounded lines, as no charging will occur.
			if (mPotGroundedMask & potBit)
				continue;

			// Compute how much further the caps will charge based on the
			// individual pot line charge rate, up to the limit.
			const uint32 startingLevel = mPotChargeLevels[i];
			const uint32 chargeRate = mPotChargeRates[i];
			const uint64 chargeDelta = (uint64)chargeRate * chargeCycles;
			const uint32 endingLevel = (uint32)std::min<uint64>(startingLevel + chargeDelta, kPotChargeLimit);

			mPotChargeLevels[i] = endingLevel;

			// If the line is not currently above threshold, check if it has crossed threshold.
			// We must not run this code if the input line starts above threshold, indicated by
			// the ALLPOT bit already being cleared. In that case, the POTn register does not
			// update at all and retains its value from a previous pot scan.
			if ((mALLPOT & potBit) && endingLevel >= kPotChargeThreshold) {
				// Clear ALLPOT bit to indicate above threshold / update has stopped.
				mALLPOT -= potBit;

				// Interpolate to determine relative cycle where threshold was crossed.
				const uint32 cyclesToThreshold = (kPotChargeThreshold - startingLevel - 1) / chargeRate + 1;

				if (fastPotScan) {
					// Fast pot scan -- count up 1/cycle.
					mPotLatches[i] = (uint8)std::min<uint32>(229, potBaseCounter + cyclesToThreshold);
				} else if (slowPotScanRunning) {
					// Slow pot scan with normal clock -- count up 1/114 cycles.
					mPotLatches[i] = (uint8)std::min<uint32>(228, potBaseCounter + (slowPotScanCycleOffset + cyclesToThreshold) / 114);
				} else {
					// Slow pot scan with frozen clock -- latch the counter which isn't counting.
					mPotLatches[i] = potBaseCounter;
				}
			}
		}

		cyclesToProcess -= chargeCycles;
	}

	// If pot scan has ended, for any counters that were still active, turn
	// them off and latch the maximum count value.
	if (!mbPotScanActive && mALLPOT) {
		for(int i=0; i<8; ++i) {
			if (mALLPOT & (1 << i))
				mPotLatches[i] = fastPotScan ? 229 : 228;
		}

		mALLPOT = 0;
	}
}

void ATPokeyEmulator::UpdateAddressDecoding() {
	mAddressMask = mpSlave && mbStereoSoftEnable ? 0x1F : 0x0F;
}

void ATPokeyEmulator::NotifyForceBreak() {
	if (mpCassette)
		mpCassette->PokeyChangeForceBreak(IsSerialForceBreakEnabled());

	for(IATPokeySIODevice *dev : mDevices)
		dev->PokeySetBreak((mSKCTL & 0x80) != 0);
}

template<unsigned T_Ch>
int ATPokeyEmulator::GetAUDFP1(uint32 t) const {
	static_assert(T_Ch < 4);

	return (uint32_t)(t - mAUDFP1Time[T_Ch]) < UINT32_C(0x80000000) ? mAUDFP1B[T_Ch] : mAUDFP1A[T_Ch];
}

template<unsigned T_Ch>
int ATPokeyEmulator::GetAUDFP1() const {
	static_assert(T_Ch < 4);

	return GetAUDFP1<T_Ch>(mpScheduler->GetTick());
}

template<unsigned T_Ch>
void ATPokeyEmulator::SetAUDFP1(int period) {
	const uint32 t = mpScheduler->GetTick();

	// We fire the borrow events one cycle after where the reload
	// of the timer occurs. This means that a write to AUDF1 only
	// takes effect three cycles later.
	const uint32 effectiveTime = t + 2;

	if (mAUDFP1Time[T_Ch] != effectiveTime) {
		mAUDFP1A[T_Ch] = mAUDFP1B[T_Ch];
		mAUDFP1Time[T_Ch] = effectiveTime;
	}

	mAUDFP1B[T_Ch] = period;
}
