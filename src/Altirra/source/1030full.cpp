//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2023 Avery Lee
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
#include <vd2/system/hash.h>
#include <vd2/system/int128.h>
#include <vd2/system/math.h>
#include <at/atcore/asyncdispatcher.h>
#include <at/atcore/audiosource.h>
#include <at/atcore/deviceserial.h>
#include <at/atcore/deviceindicators.h>
#include <at/atcore/logging.h>
#include <at/atcore/propertyset.h>
#include <at/atcpu/memorymap.h>
#include "1030full.h"
#include "audiosampleplayer.h"
#include "debuggerlog.h"
#include "firmwaremanager.h"
#include "memorymanager.h"
#include "modem.h"

void ATCreateDevice835Full(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDevice1030Full> p(new ATDevice1030Full(true));
	p->SetSettings(pset);

	*dev = p.release();
}

void ATCreateDevice1030Full(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDevice1030Full> p(new ATDevice1030Full(false));
	p->SetSettings(pset);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDef835Full = { "835full", "835full", L"835 Modem (full emulation)", ATCreateDevice835Full };
extern const ATDeviceDefinition g_ATDeviceDef1030Full = { "1030full", "1030full", L"1030 Modem (full emulation)", ATCreateDevice1030Full };

/////////////////////////////////////////////////////////////////////////////

ATDevice1030Full::TargetProxy::TargetProxy(ATDevice1030Full& parent)
	: mParent(parent)
{
}

std::pair<const uintptr *, const uintptr *> ATDevice1030Full::TargetProxy::GetReadWriteMaps() const {
	return { mParent.mDebugReadMap, mParent.mDebugWriteMap };
}

void ATDevice1030Full::TargetProxy::SetHistoryBuffer(ATCPUHistoryEntry *harray) {
	mParent.mCoProc.SetHistoryBuffer(harray);
}

uint32 ATDevice1030Full::TargetProxy::GetHistoryCounter() const {
	return mParent.mCoProc.GetHistoryCounter();
}
	
uint32 ATDevice1030Full::TargetProxy::GetTime() const {
	return mParent.mCoProc.GetTime();
}

uint32 ATDevice1030Full::TargetProxy::GetStepStackLevel() const {
	return mParent.mCoProc.GetStepStackLevel();
}

void ATDevice1030Full::TargetProxy::GetExecState(ATCPUExecState& state) const {
	mParent.mCoProc.GetExecState(state);
}

void ATDevice1030Full::TargetProxy::SetExecState(const ATCPUExecState& state) {
	mParent.mCoProc.SetExecState(state);
}

/////////////////////////////////////////////////////////////////////////////

ATDevice1030Full::ATDevice1030Full(bool mode835)
	: mTargetProxy(*this)
	, mb835Mode(mode835)
{
	mpModem = new ATModemEmulator;
	mpModem->Set1030Mode();
	mpModem->SetOnReadReady(std::bind_front(&ATDevice1030Full::OnModemReadReady, this));

	// need to raise the max cycles per bit much higher than usual to accommodate
	// 300 baud (5966 cpb).
	mSerialXmitQueue.SetMaxCyclesPerBit(7000);

	mBreakpointsImpl.BindBPHandler(mCoProc);
	mBreakpointsImpl.SetStepHandler(this);

	mCoProc.SetProgramBanks(mROM[0], mROM[1]);

	// The 8050 in the 1030 runs T-states at 4.032MHz with 15 T-states per
	// machine cycle, with the majority of instructions only 1 or 2 machine
	// cycles. To keep accounting simple we run the drive coprocessor clock
	// at 4.032MHz / 15, but need to report 4.032MHz as the displayed clock
	// speed.
	InitTargetControl(mTargetProxy, 4032000.0 / 15.0, kATDebugDisasmMode_8048, &mBreakpointsImpl, this);
	ApplyDisplayCPUClockMultiplier(15.0);
	
	mSerialCmdQueue.SetOnDriveCommandStateChanged(
		[this](bool bit) {
			if (bit)
				mCoProc.AssertIrq();
			else
				mCoProc.NegateIrq();
		}
	);
}

ATDevice1030Full::~ATDevice1030Full() {
}

void *ATDevice1030Full::AsInterface(uint32 iid) {
	switch(iid) {
		case IATDeviceScheduling::kTypeID: return static_cast<IATDeviceScheduling *>(this);
		case IATDeviceFirmware::kTypeID: return static_cast<IATDeviceFirmware *>(this);
		case IATDeviceSIO::kTypeID: return static_cast<IATDeviceSIO *>(this);

		case IATDeviceButtons::kTypeID:
			return static_cast<IATDeviceButtons *>(mpModem);
	}

	void *p = ATDiskDriveDebugTargetControl::AsInterface(iid);
	if (p)
		return p;

	return ATDevice::AsInterface(iid);
}

void ATDevice1030Full::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = mb835Mode ? &g_ATDeviceDef835Full : &g_ATDeviceDef1030Full;
}

void ATDevice1030Full::GetSettings(ATPropertySet& settings) {
	settings.Clear();
	mpModem->GetSettings(settings);
}

bool ATDevice1030Full::SetSettings(const ATPropertySet& settings) {
	mpModem->SetSettings(settings);
	return true;
}

void ATDevice1030Full::Init() {
	mDriveScheduler.SetRate(VDFraction(4032000, 15));

	// We need to do this early to ensure that the clock divisor is set before we perform init processing.
	ResetTargetControl();

	mSerialXmitQueue.Init(mpScheduler, mpSIOMgr);
	mSerialCmdQueue.Init(&mDriveScheduler, mpSIOMgr);

	if (mb835Mode) {
		mCoProc.SetPortReadHandler([this](uint8 port, uint8 output) -> uint8 { return OnReadPort_835(port, output); });
		mCoProc.SetPortWriteHandler([this](uint8 port, uint8 data) { OnWritePort_835(port, data); });
		mCoProc.SetXRAMReadHandler([this](uint8 addr) -> uint8 { return 0xFF; });
		mCoProc.SetT0ReadHandler([this]() { return OnReadT0_835(); });
	} else {
		mCoProc.SetPortReadHandler([this](uint8 port, uint8 output) -> uint8 { return OnReadPort_1030(port, output); });
		mCoProc.SetPortWriteHandler([this](uint8 port, uint8 data) { OnWritePort_1030(port, data); });
		mCoProc.SetXRAMReadHandler([this](uint8 addr) -> uint8 { return OnReadXRAM(addr); });
		mCoProc.SetT0ReadHandler([this]() { return OnReadT0_1030(); });
	}

	mCoProc.SetT1ReadHandler([this]() { return OnReadT1(); });

	// Unlike the other coprocessor types, we don't actually need read/write maps. This just
	// makes our life easier as we can leverage the default impl.
	ATCoProcMemoryMapView mmap(mDebugReadMap, mDebugWriteMap);

	mmap.Clear(mDummyRead, mDummyWrite);
	mmap.SetReadMem(0x00, 0x08, mROM[0]);
	mmap.SetReadMem(0x08, 0x08, mROM[1]);
	mmap.SetMemory(0x10, 0x01, mCoProc.GetInternalRAM());

	IATDeviceSchedulingService& schedService = *GetService<IATDeviceSchedulingService>();
	mpModem->Init(
		schedService.GetMachineScheduler(),
		schedService.GetSlowScheduler(),
		GetService<IATDeviceIndicatorManager>(),
		GetService<IATAudioMixer>(),
		GetService<IATAsyncDispatcher>()
	);
	mpModem->SetPhoneToAudioEnabled(false);

	// raise RCVD high so firmware doesn't try shifting in bogus bytes
	mModemReceiveShifter = 1;
}

void ATDevice1030Full::Shutdown() {
	mDriveScheduler.UnsetEvent(mpEventDriveReceiveBit);
	mDriveScheduler.UnsetEvent(mpEventModemReceiveBit);
	mDriveScheduler.UnsetEvent(mpEventModemTransmitBit);

	ShutdownTargetControl();

	mpFwMgr = nullptr;

	mSerialCmdQueue.Shutdown();
	mSerialXmitQueue.Shutdown();

	if (mpModem) {
		mpModem->Shutdown();
		mpModem = nullptr;
	}

	if (mpSIOMgr) {
		mpSIOMgr->RemoveRawDevice(this);
		mpSIOMgr = nullptr;
	}
}

void ATDevice1030Full::WarmReset() {
	// If the computer resets, its transmission is interrupted.
	mDriveScheduler.UnsetEvent(mpEventDriveReceiveBit);
}

void ATDevice1030Full::ComputerColdReset() {
	WarmReset();
}

void ATDevice1030Full::PeripheralColdReset() {
	mDriveScheduler.UnsetEvent(mpEventDriveReceiveBit);
	mDriveScheduler.UnsetEvent(mpEventModemTransmitBit);

	// Don't clear the modem receive event -- if the modem still has data queued
	// we need to continue processing it.
	//
	// mDriveScheduler.UnsetEvent(mpEventModemReceiveBit);
	//

	mpModem->ColdReset();

	mSerialXmitQueue.Reset();
	mSerialCmdQueue.Reset();
	
	mbDirectReceiveOutput = true;
	mbDirectTransmitOutput = true;

	if (mb835Mode) {
		OnWritePort_835(mP1, 0xFF);
		OnWritePort_835(mP2, 0xFF);
	} else {
		OnWritePort_1030(mP1, 0xFF);
		OnWritePort_1030(mP2, 0xFF);
	}

	mCoProc.ColdReset();

	ResetTargetControl();

	WarmReset();
}

void ATDevice1030Full::InitFirmware(ATFirmwareManager *fwman) {
	mpFwMgr = fwman;

	ReloadFirmware();
}

bool ATDevice1030Full::ReloadFirmware() {
	const vduint128 oldIHash = VDHash128(mROM, sizeof mROM);

	uint8 irom[4096] = {};
	uint8 xrom[8192] = {};

	uint32 len = 0;
	bool iromUsable = false;

	if (mb835Mode) {
		mpFwMgr->LoadFirmware(mpFwMgr->GetFirmwareOfType(kATFirmwareType_835, true), irom, 0, 1024, nullptr, &len, nullptr, nullptr, &iromUsable);

		memcpy(&mROM[0][0], irom, 1024);
		memcpy(&mROM[0][1024], irom, 1024);
		mROM[0][2048] = mROM[0][0];
		mROM[0][2049] = mROM[0][0];

		memcpy(mROM[1], mROM[0], sizeof mROM[1]);

		const vduint128 newIHash = VDHash128(mROM, 1024);

		mbFirmwareUsable = iromUsable;

		return oldIHash != newIHash;
	} else {
		const vduint128 oldXHash = VDHash128(mXROM, sizeof mXROM);

		bool xromUsable = false;
		mpFwMgr->LoadFirmware(mpFwMgr->GetFirmwareOfType(kATFirmwareType_1030InternalROM, true), irom, 0, sizeof irom, nullptr, &len, nullptr, nullptr, &iromUsable);
		mpFwMgr->LoadFirmware(mpFwMgr->GetFirmwareOfType(kATFirmwareType_1030ExternalROM, true), xrom, 0, sizeof xrom, nullptr, &len, nullptr, nullptr, &xromUsable);

		memcpy(mROM[0], irom, 2048);
		memcpy(mROM[1], irom + 2048, 2048);
		mROM[0][2048] = mROM[0][0];
		mROM[0][2049] = mROM[0][0];
		mROM[1][2048] = mROM[1][0];
		mROM[1][2049] = mROM[1][0];

		memcpy(mXROM, xrom, sizeof mXROM);

		const vduint128 newIHash = VDHash128(mROM, sizeof mROM);
		const vduint128 newXHash = VDHash128(mXROM, sizeof mXROM);

		mbFirmwareUsable = iromUsable && xromUsable;

		return oldIHash != newIHash || oldXHash != newXHash;
	}
}

const wchar_t *ATDevice1030Full::GetWritableFirmwareDesc(uint32 idx) const {
	return nullptr;
}

bool ATDevice1030Full::IsWritableFirmwareDirty(uint32 idx) const {
	return false;
}

void ATDevice1030Full::SaveWritableFirmware(uint32 idx, IVDStream& stream) {
}

ATDeviceFirmwareStatus ATDevice1030Full::GetFirmwareStatus() const {
	return mbFirmwareUsable ? ATDeviceFirmwareStatus::OK : ATDeviceFirmwareStatus::Missing;
}

void ATDevice1030Full::InitSIO(IATDeviceSIOManager *mgr) {
	mpSIOMgr = mgr;
	mpSIOMgr->AddRawDevice(this);
}

void ATDevice1030Full::OnScheduledEvent(uint32 id) {
	if (id == kEventId_DriveReceiveBit) {
		mReceiveShiftRegister >>= 1;
		mpEventDriveReceiveBit = nullptr;

		if (mReceiveShiftRegister >= 2) {
			mReceiveTimingAccum += mReceiveTimingStep;
			mpEventDriveReceiveBit = mDriveScheduler.AddEvent(mReceiveTimingAccum >> 10, this, kEventId_DriveReceiveBit);
			mReceiveTimingAccum &= 0x3FF;
		}
	} else if (id == kEventId_ModemReceiveBit) {
		mpEventModemReceiveBit = nullptr;

		// try to fill shifter if it is empty
		if (mModemReceiveShifter <= 1) {
			// this will also queue next event, so we're done regardless
			TryReadModemByte();
			return;
		}

		// advance to next bit
		mModemReceiveShifter >>= 1;
		mpEventModemReceiveBit = mDriveScheduler.AddEvent(kDeviceCyclesPerModemBit, this, kEventId_ModemReceiveBit);
	} else if (id == kEventId_ModemTransmitBit) {
		mpEventModemTransmitBit = nullptr;

		// shift in bit
		mModemTransmitShifter = (mModemTransmitShifter >> 1) + (mP2 & 0x80 ? 0x200 : 0);

		// check for false start bit
		if (mModemTransmitShifter == 0b0'1'11111111'1)
			return;

		// check for stop bit time
		if (!(mModemTransmitShifter & 1)) {
			// check stop bit
			if (mModemTransmitShifter & 0x200) {
				// byte is good -- send to modem
				mpModem->Write(300, (uint8)(mModemTransmitShifter >> 1));
			}

			return;
		}

		// next bit
		mpEventModemTransmitBit = mDriveScheduler.AddEvent(kDeviceCyclesPerModemBit, this, kEventId_ModemTransmitBit);
	} else
		return ATDiskDriveDebugTargetControl::OnScheduledEvent(id);
}

void ATDevice1030Full::OnCommandStateChanged(bool asserted) {
	if (mbCommandState != asserted) {
		mbCommandState = asserted;

		AddCommandEdge(asserted);
	}
}

void ATDevice1030Full::OnMotorStateChanged(bool asserted) {
}

void ATDevice1030Full::OnBeginReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) {
	Sync();

	mReceiveShiftRegister = (c + c + 0x200) * 2 + 1;

	// 154/1024 approximation to ratio between system clock (1.789MHz) and device
	// machine cycle rate (268.8KHz).
	mReceiveTimingAccum = 0x200;
	mReceiveTimingStep = cyclesPerBit * 154;

	mDriveScheduler.SetEvent(1, this, kEventId_DriveReceiveBit, mpEventDriveReceiveBit);
}

void ATDevice1030Full::OnReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) {
}

void ATDevice1030Full::OnTruncateByte() {
	mReceiveShiftRegister = 1;
	mDriveScheduler.UnsetEvent(mpEventDriveReceiveBit);
}

void ATDevice1030Full::OnSendReady() {
}

void ATDevice1030Full::Sync() {
	const uint32 limit = AccumSubCycles();

	mDriveScheduler.SetStopTime(limit);

	for(;;) {
		if (!mCoProc.GetCyclesLeft()) {
			uint32 tc = ATSCHEDULER_GETTIMETONEXT(&mDriveScheduler);
			if (tc <= 0)
				break;

			uint32 tc2 = mCoProc.GetTStatesPending();

			if (!tc2)
				tc2 = 1;

			if (tc > tc2)
				tc = tc2;

			ATSCHEDULER_ADVANCE_N(&mDriveScheduler, tc);
			mCoProc.AddCycles(tc);
		}

		mCoProc.Run();

		if (mCoProc.GetCyclesLeft()) {
			ScheduleImmediateResume();
			break;
		}
	}

	FlushStepNotifications();
}

void ATDevice1030Full::AddTransmitEdge(bool polarity) {
	mSerialXmitQueue.AddTransmitBit(DriveTimeToMasterTime() + mSerialXmitQueue.kTransmitLatency, polarity);
}

void ATDevice1030Full::AddCommandEdge(uint32 polarity) {
	mSerialCmdQueue.AddCommandEdge(MasterTimeToDriveTime(), polarity);
}

bool ATDevice1030Full::OnReadT0_835() const {
	// T0 = TMS99532 RCVD
	// If analog loopback is enabled, send TXD straight to RCVD.
	if (mP2 & 4)
		return (mP2 & 0x80) != 0;
	else
		return (mModemReceiveShifter & 1) != 0;
}

bool ATDevice1030Full::OnReadT0_1030() const {
	// T0 = TMS99532 RCVD
	// If analog loopback is enabled, send TXD straight to RCVD.
	if (mP1 & 1)
		return (mP2 & 0x80) != 0;
	else
		return (mModemReceiveShifter & 1) != 0;
}

bool ATDevice1030Full::OnReadT1() const {
	// T1 = uninverted SIO DATA OUT (computer -> peripheral).
	return (mReceiveShiftRegister & 1) != 0;
}

uint8 ATDevice1030Full::OnReadPort_835(uint8 addr, uint8 output) {
	// P1.0		SIO Data Out (uninverted)
	// P1.1		SIO Ready (1 = computer on)
	// P1.2		SIO Clock Out
	// P1.3		SIO Command
	// P1.4		SIO Proceed (uninverted)
	// P1.5		SIO Interrupt (uninverted)
	// P1.6		TMS99532 /CD (carrier detect)
	// P1.7		TMS99532 RCVD
	// P2.0		SIO Data In
	// P2.1		LED
	// P2.2		TMS99532 ALB
	// P2.3		On/off hook relay
	// P2.4		Audio enable
	// P2.5		TMS99532 SQT
	// P2.6		TMS99532 O/A
	// P2.7		TMS99532 TXD

	if (addr == 0) {
		uint8 v = mP1 | 0x40;

		// Analog loopback causes carrier detection regardless of the external
		// carrier status.
		if ((mP2 & 4) || mpModem->GetStatus().mbCarrierDetect)
			v &= 0xBF;

		if (mSerialCmdQueue.GetDriveCommandState())
			v &= 0xF7;

		return v;
	} else
		return mP2;
}

void ATDevice1030Full::OnWritePort_835(uint8 addr, uint8 output) {
	if (addr == 0) {
		const uint8 delta = mP1 ^ output;

		if (delta) {
			mP1 = output;

			// update SIO proceed
			if (delta & 0x10)
				mpSIOMgr->SetSIOProceed(this, (output & 0x10) == 0);

			// update SIO interrupt
			if (delta & 0x20)
				mpSIOMgr->SetSIOInterrupt(this, (output & 0x20) == 0);
		}
	} else {
		const uint8 delta = mP2 ^ output;

		if (delta) {
			mP2 = output;

			// update SIO data in
			if (delta & 0x01)
				AddTransmitEdge((output & 0x01) != 0);

			// update phone audio pass-through
			if (delta & 0x10)
				mpModem->SetPhoneToAudioEnabled(!(output & 0x10));

			// update on/off hook
			if (delta & 0x08) {
				if (output & 0x08)
					mpModem->OffHook();
				else
					mpModem->OnHook();
			}
			// check for start bit to modem
			if (delta & 0x80) {
				if (!(output & 0x80) && !mpEventModemTransmitBit) {
					// initialize shifter
					mModemTransmitShifter = 0x3FF;

					// set initial delay to 1/2 bit to verify start bit
					mpEventModemTransmitBit = mDriveScheduler.AddEvent(kDeviceCyclesPerModemBit / 2, this, kEventId_ModemTransmitBit);
				}
			}
		}
	}
}

uint8 ATDevice1030Full::OnReadPort_1030(uint8 addr, uint8 output) {
	// P1.0		TMS99532 ALB
	// P1.1		SIO Ready
	// P1.2		TMS99532 /ATE (answer tone)
	// P1.3		SIO Data In
	// P1.4		SIO Proceed
	// P1.5		SIO Interrupt
	// P1.6		TMS99532 /CD
	// P1.7		On/off hook relay (0 = on hook)
	// P2.0		ROM A8
	// P2.1		ROM A9
	// P2.2		ROM A10
	// P2.3		ROM A11
	// P2.4		ROM A12
	// P2.5		TMS99532 SQT
	// P2.6		TMS99532 O/A
	// P2.7		TMS99532 TXD

	if (addr == 0) {
		uint8 v = mP1 | 0x40;

		// Analog loopback causes carrier detection regardless of the external
		// carrier status.
		if ((mP1 & 1) || mpModem->GetStatus().mbCarrierDetect)
			v &= 0xBF;

		return v;
	} else
		return mP2;
}

void ATDevice1030Full::OnWritePort_1030(uint8 addr, uint8 output) {
	if (addr == 0) {
		const uint8 delta = mP1 ^ output;

		if (delta) {
			mP1 = output;

			// update SIO data in
			if (delta & 0x08)
				AddTransmitEdge((output & 0x08) != 0);

			// update SIO proceed
			if (delta & 0x10)
				mpSIOMgr->SetSIOProceed(this, (output & 0x10) == 0);

			// update SIO interrupt
			if (delta & 0x20)
				mpSIOMgr->SetSIOInterrupt(this, (output & 0x20) == 0);

			// update external audio input
			if (delta & 0x04) {
				// /ATE + SQT = EXI enabled
				mpModem->SetAudioToPhoneEnabled(!(output & 0x04) && (mP2 & 0x20));
			}

			// update on/off hook
			if (delta & 0x80) {
				if (output & 0x80)
					mpModem->OffHook();
				else
					mpModem->OnHook();
			}
		}
	} else {
		const uint8 delta = mP2 ^ output;

		if (delta) {
			mP2 = output;

			// check for squelch/unsquelch
			if (delta & 0x20) {
				// /ATE + SQT = EXI enabled
				mpModem->SetAudioToPhoneEnabled(!(output & 0x04) && (mP2 & 0x20));
			}

			// check for start bit to modem
			if (delta & 0x80) {
				if (!(output & 0x80) && !mpEventModemTransmitBit) {
					// initialize shifter
					mModemTransmitShifter = 0x3FF;

					// set initial delay to 1/2 bit to verify start bit
					mpEventModemTransmitBit = mDriveScheduler.AddEvent(kDeviceCyclesPerModemBit / 2, this, kEventId_ModemTransmitBit);
				}
			}
		}
	}
}

uint8 ATDevice1030Full::OnReadXRAM(uint8 addr) const {
	return mXROM[addr + ((mP2 & 0x1F) << 8)];
}

void ATDevice1030Full::OnModemReadReady() {
	TryReadModemByte();
}

void ATDevice1030Full::TryReadModemByte() {
	// if we are already processing a byte received from the modem, just exit.
	if (mpEventModemReceiveBit)
		return;

	uint8 ch = 0;
	bool framingError = false;
	if (!mpModem->Read(300, ch, framingError))
		return;

	// set up shifter -- bits sent LSB to MSB with start/stop bits
	mModemReceiveShifter = ((uint32)ch << 1) + 0x200;

	// queue event for next bit edge
	mpEventModemReceiveBit = mDriveScheduler.AddEvent(kDeviceCyclesPerModemBit, this, kEventId_ModemReceiveBit);
}

