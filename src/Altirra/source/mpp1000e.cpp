//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2024 Avery Lee
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
#include <vd2/system/binary.h>
#include <vd2/system/strutil.h>
#include <at/atcore/asyncdispatcher.h>
#include <at/atcore/audiomixer.h>
#include <at/atcore/cio.h>
#include <at/atcore/devicecio.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/deviceindicators.h>
#include <at/atcore/deviceport.h>
#include <at/atcore/deviceserial.h>
#include <at/atcore/logging.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/scheduler.h>
#include "modem.h"

extern ATLogChannel g_ATLCModemData;

//////////////////////////////////////////////////////////////////////////
// Microbits (Supra) MPP-1000E Modem emulator
//
// The MPP-1000E plugs into joystick port 2 and provides a 300 baud modem
// with auto-answer support. The CPU has to handle bit-banging the serial
// data; this is nominally done with a 4x clock (1200Hz interrupts).
// Alternative baud rates are possible with the MPP Smart Terminal software,
// but we currently don't support this.
//
// Port hookup is as follows:
//	PA7 (I): Carrier detect (1 = detected)
//	PA6 (O): Off hook (0 = off hook)
//	PA5 (I): Receive data (inverted)
//	PA4 (O): Transmit data (inverted)
//	TRIG1: Ring detect (1 = ringing)
//
class ATDeviceMPP1000E final
	: public ATDevice
	, public IATSchedulerCallback
{
public:
	ATDeviceMPP1000E();
	~ATDeviceMPP1000E();

	void *AsInterface(uint32 id) override;

public:
	void GetDeviceInfo(ATDeviceInfo& info) override;
	void GetSettings(ATPropertySet& settings) override;
	bool SetSettings(const ATPropertySet& settings) override;
	void Init() override;
	void Shutdown() override;
	void ColdReset() override;

public:	// IATSchedulerCallback
	void OnScheduledEvent(uint32 id) override;

private:
	void OnModemReadReady();
	void OnControlStateChanged(const ATDeviceSerialStatus& status);
	void OnJoystickOutputsChanged();
	void RecomputeCyclesPerBit();

	enum class EventIds : uint32 {
		RecvBit = 1,
		XmitBit
	};

	static constexpr uint8 kJoyCarrierBit = 8;
	static constexpr uint8 kJoyHookBit = 4;
	static constexpr uint8 kJoyRecvBit = 2;
	static constexpr uint8 kJoyXmitBit = 1;

	ATScheduler *mpScheduler = nullptr;
	ATEvent	*mpEventRecvBit = nullptr;
	ATEvent	*mpEventXmitBit = nullptr;
	ATModemEmulator *mpDevice = nullptr;
	vdrefptr<IATDeviceControllerPort> mpControllerPort;

	uint32 mCyclesPerBit = 1;
	uint32 mRecvShifter = 0;
	uint32 mXmitShifter = 0;
	uint8 mLastJoyOutput = 15;
};

void ATCreateDeviceMPP1000E(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceMPP1000E> p(new ATDeviceMPP1000E);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefMPP1000E = { "mpp1000e", "pocketmodem", L"MPP-1000E", ATCreateDeviceMPP1000E };

ATDeviceMPP1000E::ATDeviceMPP1000E() {
	// We have to init this early so it can accept settings.
	mpDevice = new ATModemEmulator;
	mpDevice->AddRef();
	mpDevice->Set1030Mode();

	mpDevice->SetOnReadReady([this] { OnModemReadReady(); });
}

ATDeviceMPP1000E::~ATDeviceMPP1000E() {
	Shutdown();
}

void *ATDeviceMPP1000E::AsInterface(uint32 id) {
	if (id == IATDeviceButtons::kTypeID)
		return static_cast<IATDeviceButtons *>(mpDevice);

	return ATDevice::AsInterface(id);
}

void ATDeviceMPP1000E::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefMPP1000E;
}

void ATDeviceMPP1000E::GetSettings(ATPropertySet& props) {
	mpDevice->GetSettings(props);
}

bool ATDeviceMPP1000E::SetSettings(const ATPropertySet& props) {
	mpDevice->SetSettings(props);

	return true;
}

void ATDeviceMPP1000E::Init() {
	GetService<IATDevicePortManager>()->AllocControllerPort(1, ~mpControllerPort);

	mpDevice->SetOnStatusChange([this](const ATDeviceSerialStatus& status) { this->OnControlStateChanged(status); });

	IATDeviceSchedulingService *schedSvc = GetService<IATDeviceSchedulingService>();
	mpScheduler = schedSvc->GetMachineScheduler();

	mpDevice->Init(mpScheduler, schedSvc->GetSlowScheduler(), GetService<IATDeviceIndicatorManager>(), GetService<IATAudioMixer>(), GetService<IATAsyncDispatcher>());

	RecomputeCyclesPerBit();
	OnControlStateChanged(mpDevice->GetStatus());
	mpControllerPort->SetOnDirOutputChanged(kJoyHookBit | kJoyXmitBit, [this] { OnJoystickOutputsChanged(); }, true);
}

void ATDeviceMPP1000E::Shutdown() {
	if (mpDevice) {
		mpDevice->Release();
		mpDevice = NULL;
	}

	if (mpScheduler) {
		mpScheduler->UnsetEvent(mpEventRecvBit);
		mpScheduler->UnsetEvent(mpEventXmitBit);
		mpScheduler = nullptr;
	}

	mpControllerPort = nullptr;
}

void ATDeviceMPP1000E::ColdReset() {
	if (mpDevice)
		mpDevice->ColdReset();

	mpScheduler->UnsetEvent(mpEventRecvBit);
	mpScheduler->UnsetEvent(mpEventXmitBit);

	mRecvShifter = 0;
	mXmitShifter = 0;

	mpControllerPort->SetDirInputBits(0, kJoyRecvBit);
}

void ATDeviceMPP1000E::OnScheduledEvent(uint32 id) {
	if (id == (uint32)EventIds::RecvBit) {
		mpEventRecvBit = nullptr;

		if (mRecvShifter) {
			// update receive line and shift; note that receive line is
			// inverted polarity
			mpControllerPort->SetDirInputBits(mRecvShifter & 1 ? 0 : kJoyRecvBit, kJoyRecvBit);
			mRecvShifter >>= 1;

			// re-arm timer for next bit
			mpEventRecvBit = mpScheduler->AddEvent(mCyclesPerBit, this, (uint32)EventIds::RecvBit);
		} else {
			// reload shifter from modem
			OnModemReadReady();
		}
	} else if (id == (uint32)EventIds::XmitBit) {
		mpEventXmitBit = nullptr;

		mXmitShifter >>= 1;

		if (!(mpControllerPort->GetCurrentDirOutput() & kJoyXmitBit))
			mXmitShifter += 0b1'00000000'00;

		// check for a false start bit
		if (mXmitShifter == 0b1'10000000'00)
			return;

		// check for a finished byte
		if (mXmitShifter & 1) {
			// check for framing error
			if (!(mXmitShifter & 0b1'00000000'00))
				return;

			mpDevice->Write(300, (uint8)(mXmitShifter >> 2));
		} else {
			mpEventXmitBit = mpScheduler->AddEvent(mCyclesPerBit, this, (uint32)EventIds::XmitBit);
		}
	}
}

void ATDeviceMPP1000E::OnModemReadReady() {
	if (mpEventRecvBit)
		return;

	uint8 c;
	bool framingError;

	if (mpDevice->Read(300, c, framingError)) {
		g_ATLCModemData("Receiving byte from modem: $%02X\n", c);

		mRecvShifter = 0x100 + (uint32)c;

		// raise inverted receive line for start bit
		mpControllerPort->SetDirInputBits(kJoyRecvBit, kJoyRecvBit);

		mpEventRecvBit = mpScheduler->AddEvent(mCyclesPerBit, this, (uint32)EventIds::RecvBit);
	}
}

void ATDeviceMPP1000E::OnControlStateChanged(const ATDeviceSerialStatus& status) {
	mpControllerPort->SetDirInputBits(status.mbCarrierDetect ? kJoyCarrierBit : 0, kJoyCarrierBit);
	mpControllerPort->SetTriggerDown(status.mbRinging);
}

void ATDeviceMPP1000E::OnJoystickOutputsChanged() {
	const uint8 output = mpControllerPort->GetCurrentDirOutput();
	const uint8 delta = mLastJoyOutput ^ output;
	mLastJoyOutput = output;

	if (delta & kJoyXmitBit) {
		// PA4 low = transmit 1
		const bool bit = !(output & kJoyXmitBit);

		if (bit) {
			// rising edge -- check if we're waiting for a start bit and clear it
			// as false if so
			if (mXmitShifter == 0b1'00000000'00)
				mpScheduler->UnsetEvent(mpEventXmitBit);
		} else {
			// falling edge -- begin shifting if we are not already doing so
			if (!mpEventXmitBit) {
				// set delay for 1/2 bit
				mpScheduler->SetEvent(mCyclesPerBit >> 1, this, (uint32)EventIds::XmitBit, mpEventXmitBit);

				// initialize shifter
				mXmitShifter = 0b1'00000000'00;
			}
		}
	}

	if (delta & kJoyHookBit) {
		if (output & kJoyHookBit) {
			mpDevice->OnHook();
		} else {
			mpDevice->OffHook();
		}
	}
}

void ATDeviceMPP1000E::RecomputeCyclesPerBit() {
	mCyclesPerBit = (uint32)(0.5 + mpScheduler->GetRate().asDouble() / 300.0);
}
