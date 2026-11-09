//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2023 Avery Lee
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

#include "stdafx.h"
#include <chrono>
#include <array>
#include <at/atcore/audiomixer.h>
#include <at/atcore/asyncdispatcher.h>
#include <at/atcore/deviceindicators.h>
#include <at/atcore/devicepbi.h>
#include <at/atcore/scheduler.h>
#include <at/atcore/logging.h>
#include "1400xl.h"
#include "firmwaremanager.h"
#include "memorymanager.h"
#include "modem.h"

ATLogChannel g_ATLCVotrax(false, false, "VOTRAX", "Votrax SC-01 activity");

///////////////////////////////////////////////////////////////////////////////

AT1400XLPBIDevice::AT1400XLPBIDevice(AT1400XLDevice& parent)
	: mParent(parent)
{
}

void AT1400XLPBIDevice::Init() {
	mpPBIMgr = mParent.GetService<IATDevicePBIManager>();
	mpPBIMgr->AddDevice(this);
}

void AT1400XLPBIDevice::Shutdown() {
	if (mpPBIMgr) {
		mpPBIMgr->RemoveDevice(this);
		mpPBIMgr = nullptr;
	}
}

///////////////////////////////////////////////////////////////////////////////

AT1400XLVDevice::~AT1400XLVDevice() {
}

void AT1400XLVDevice::Init() {
	AT1400XLPBIDevice::Init();

	mpScheduler = mParent.GetService<IATDeviceSchedulingService>()->GetMachineScheduler();
}

void AT1400XLVDevice::Shutdown() {
	if (mpScheduler) {
		mpScheduler->UnsetEvent(mpEventVotraxIRQ);
		mpScheduler = nullptr;
	}

	AT1400XLPBIDevice::Shutdown();
}

void AT1400XLVDevice::ColdReset() {
	WarmReset();
}

void AT1400XLVDevice::WarmReset() {
	// The Votrax latch is cleared on reset. Note that the Votrax itself does not have
	// a reset line.
	SetIRQEnabled(false);
	mVotraxLatch = 0;
}

void AT1400XLVDevice::WriteByte(uint8 addr, uint8 value) {
	if (addr & 4) {
		using namespace std::literals::chrono_literals;

		static constexpr auto kPhonemeDelays =
			[]() -> std::array<uint32, 64> {
				// delays for each phoneme from the SC-01 datasheet
				constexpr std::chrono::milliseconds kRawPhonemeDelays[] = {
					 59ms,  71ms, 121ms,  47ms,  47ms,  71ms, 103ms,  90ms,
					 71ms,  55ms,  80ms, 121ms, 103ms,  70ms,  71ms,  71ms,
					 71ms, 121ms,  71ms, 146ms, 121ms, 146ms, 103ms, 185ms,
					103ms,  80ms,  47ms,  71ms,  71ms, 103ms,  55ms,  90ms,
					185ms,  65ms,  80ms,  47ms, 250ms, 103ms, 185ms, 185ms,
					185ms, 103ms,  71ms,  90ms, 185ms,  80ms, 185ms, 103ms,
					 90ms,  71ms, 103ms, 185ms,  80ms, 121ms,  59ms,  90ms,
					 80ms,  71ms, 146ms, 185ms, 121ms, 250ms, 185ms,  47ms
				};

				static_assert(vdcountof(kRawPhonemeDelays) == 64);

				std::array<uint32, 64> r{};

				for(int i=0; i<64; ++i) {
					r[i] = std::chrono::round<std::chrono::duration<uint32, std::ratio<4, 7159090>>>(kRawPhonemeDelays[i]).count();
				}

				return r;
			}();

		static constexpr const char *kPhonemeNames[] = {
			"EH3", "EH2", "EH1", "PA0", "DT", "A2", "A1", "ZH",
			"AH2", "I3", "I2", "I1", "M", "N", "B", "V",
			"CH", "SH", "Z", "AW1", "NG", "AH1", "OO1", "OO",
			"L", "K", "J", "H", "G", "F", "D", "S",
			"A", "AY", "Y1", "UH3", "AH", "P", "O", "I",
			"U", "Y", "T", "R", "E", "W", "AE", "AE1",
			"AW2", "UH2", "UH1", "UH", "O2", "O1", "IU", "U1",
			"THV", "TH", "ER", "EH", "E1", "AW", "PA1", "STOP"
		};

		static_assert(vdcountof(kPhonemeNames) == 64);

		g_ATLCVotrax("Phoneme: $%02X (%s)\n", mVotraxLatch, kPhonemeNames[mVotraxLatch]);

		const uint32 phonemeDelay = kPhonemeDelays[mVotraxLatch];
		mVotraxRequestTime = mpScheduler->GetTick64() + phonemeDelay;

		if (mbIRQEnabled) {
			mpPBIMgr->NegateIRQ(0x80);
			mpScheduler->SetEvent(phonemeDelay, this, 1, mpEventVotraxIRQ);
		}
	} else {
		const bool irqen = (value & 0x80) != 0;

		SetIRQEnabled(irqen);

		mVotraxLatch = value & 0x3F;
	}
}

void AT1400XLVDevice::OnScheduledEvent(uint32 id) {
	mpEventVotraxIRQ = nullptr;

	mpPBIMgr->AssertIRQ(0x80);
}

void AT1400XLVDevice::GetPBIDeviceInfo(ATPBIDeviceInfo& devInfo) const {
	devInfo.mDeviceId = 0x80;
	devInfo.mbHasIrq = true;
}

void AT1400XLVDevice::SelectPBIDevice(bool enable) {
	mParent.EnableFirmware(enable, 0, 0x00);
}

bool AT1400XLVDevice::IsPBIOverlayActive() const {
	return true;
}

uint8 AT1400XLVDevice::ReadPBIStatus(uint8 busData, bool debugOnly) {
	return (busData & 0x7F) | (mpScheduler->GetTick64() >= mVotraxRequestTime ? 0x80 : 0x00);
}

void AT1400XLVDevice::SetIRQEnabled(bool irqen) {
	if (mbIRQEnabled != irqen) {
		mbIRQEnabled = irqen;

		if (irqen) {
			const uint64 t = mpScheduler->GetTick64();

			if (t < mVotraxRequestTime)
				mpScheduler->SetEvent(mVotraxRequestTime - t, this, 1, mpEventVotraxIRQ);
			else
				mpPBIMgr->AssertIRQ(0x80);
		} else {
			mpScheduler->UnsetEvent(mpEventVotraxIRQ);
			mpPBIMgr->NegateIRQ(0x80);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////

AT1400XLTDevice::~AT1400XLTDevice() {
}

void AT1400XLTDevice::Init(ATModemEmulator& modemDevice) {
	AT1400XLPBIDevice::Init();

	mpModemDevice = &modemDevice;
	mpAudioMixer = mParent.GetService<IATAudioMixer>();

	auto& sch = *mParent.GetService<IATDeviceSchedulingService>();
	mpScheduler = sch.GetMachineScheduler();
	mACIA.Init(mpScheduler, sch.GetSlowScheduler());

	mACIA.SetInterruptFn(
		[this](bool assertIRQ) {
			mbIRQPending = assertIRQ;

			if (assertIRQ)
				mpPBIMgr->AssertIRQ(0x02);
			else
				mpPBIMgr->NegateIRQ(0x02);
		}
	);

	// ACIA connections:
	//
	// /DCD <= /CD from modem
	// /RTS not connected
	// /DTR not connected
	// /CTS grounded
	//
	// /DSR is driven off of a 74LS123 retriggerable one-shot. It
	// uses a 470K resistor and 0.47uF capacitor for a pulse with
	// of 0.37*470*470000 = 81733000ns, or 81ms.

	mACIA.SetDCD(false, false);
	mACIA.SetDSR(false, false);

	mACIA.SetTransmitFn(
		[this](uint8 c, uint32 baud) {
			if (mpModemDevice)
				mpModemDevice->Write(baud, c);
		}
	);

	mACIA.SetReceiveReadyFn(
		[this] {
			TryReceive();
		}
	);

	mpModemDevice->SetOnReadReady(
		[this] {
			TryReceive();
		}
	);

	mpModemDevice->SetOnStatusChange(
		[this](const ATDeviceSerialStatus& status) {
			mACIA.SetDCD(status.mbCarrierDetect, true);
			mACIA.SetDSR(status.mbRinging, true);
		}
	);

	const auto& status = mpModemDevice->GetStatus();
	mACIA.SetDCD(status.mbCarrierDetect, false);
	mACIA.SetDSR(status.mbRinging, false);
}

void AT1400XLTDevice::Shutdown() {
	if (mpScheduler) {
		mpScheduler->UnsetEvent(mpEventPickUp);
		mpScheduler = nullptr;
	}

	mACIA.SetControlFn(nullptr);
	mACIA.SetTransmitFn(nullptr);
	mACIA.SetReceiveReadyFn(nullptr);
	mACIA.Shutdown();

	mpModemDevice = nullptr;

	if (mbInternalAudioBlocked) {
		mbInternalAudioBlocked = false;

		mpAudioMixer->UnblockInternalAudio();
	}

	AT1400XLPBIDevice::Shutdown();
}

void AT1400XLTDevice::Reset() {
	WriteByte(0, 0);
	mACIA.Reset();
}

sint32 AT1400XLTDevice::DebugReadByte(uint32 addr) const {
	if (addr & 4)
		return mACIA.DebugReadByte((uint8)addr);

	return -1;
}

sint32 AT1400XLTDevice::ReadByte(uint32 addr) {
	if (addr & 4)
		return mACIA.ReadByte((uint8)addr);

	return -1;
}

void AT1400XLTDevice::WriteByte(uint32 addr, uint8 value) {
	if (addr & 4)
		mACIA.WriteByte((uint8)addr, value);
	else {
		//	D7=1: Disk controller ATTN (1450XLD only)
		//	D6=1: loop back (enable analog loopback in modem)
		//	D5=1: modem + phone (couple phone audio to audio output)
		//	D4=1: off hook (also used for pulse dialing)
		//	D3=1: phone audio enable (enable POKEY/PBI/speech audio to modem summing input)
		//	D2=1: DTMF enable (disables POKEY, PBI, and speech audio; no effect on GTIA)
		//	D1=1: Originate band (0 = answer band)
		//	D0=1: Squelch transmitter (disable modulator)
		//
		// Pulse dialing alternates between $03 and $13, with $33 between digits and after dialing.
		// Tone dialing transitions $2F -> $3F before playing tones through POKEY.

		const uint8 delta = mControlReg ^ value;

		if (delta) {
			mControlReg = value;

			// check for disk ATTN change
			if (delta & 0x80)
				mParent.SetDiskAttnState((value & 0x80) != 0);

			// check for on/off hook change
			if (delta & 0x10) {
				if (value & 0x10)
					mpModemDevice->OffHook();
				else
					mpModemDevice->OnHook();
			}

			// check for couple to phone change
			if (delta & 0x20) {
				mpModemDevice->SetAudioToPhoneEnabled((value & 0x20) != 0);
			}

			// check for audio/phone configuration change
			if (delta & 0x2C) {
				// internal audio is enabled if either the direct path is enabled
				// (D2=0), or the phone path is enabled both to phone and to the
				// audio (D3=1, D5=1)
				const bool blockInternalAudio = (value & 0x04) && (value & 0x28) != 0x28;

				if (mbInternalAudioBlocked != blockInternalAudio) {
					mbInternalAudioBlocked = blockInternalAudio;

					if (blockInternalAudio)
						mpAudioMixer->BlockInternalAudio();
					else
						mpAudioMixer->UnblockInternalAudio();
				}
			}
		}
	}
}

void AT1400XLTDevice::OnScheduledEvent(uint32 id) {
	mpEventPickUp = nullptr;

	mACIA.SetDCD(true, true);
}

void AT1400XLTDevice::GetPBIDeviceInfo(ATPBIDeviceInfo& devInfo) const {
	devInfo.mDeviceId = 0x02;
	devInfo.mbHasIrq = true;
}

void AT1400XLTDevice::SelectPBIDevice(bool enable) {
	mParent.EnableFirmware(enable, 2048, 0x08);
}

bool AT1400XLTDevice::IsPBIOverlayActive() const {
	return true;
}

uint8 AT1400XLTDevice::ReadPBIStatus(uint8 busData, bool debugOnly) {
	return (busData & 0xFD) | (mbIRQPending ? 0x02 : 0x00);
}

void AT1400XLTDevice::TryReceive() {
	if (mpModemDevice && mACIA.IsReceiveReady()) {
		uint32 baud = 0;
		uint8 ch = 0;
		if (mpModemDevice->Read(baud, ch))
			mACIA.ReceiveByte(ch, baud);
	}
}

///////////////////////////////////////////////////////////////////////////////

extern constexpr ATDeviceDefinition g_ATDeviceDef1400XL {
	"1400xl",
	"1400xl",
	L"1400XL V:/T: Hardware",
	[](const ATPropertySet& pset, IATDevice **dev) {
		*dev = vdrefptr(new AT1400XLDevice).release();
	},
	kATDeviceDefFlag_Internal
};

AT1400XLDevice::AT1400XLDevice()
	: mpModemDevice(new ATModemEmulator)
	, mTDevice { *this }
	, mVDevice { *this }
{
	mpModemDevice->Set1030Mode();
}

AT1400XLDevice::~AT1400XLDevice() {
}

void *AT1400XLDevice::AsInterface(uint32 iid) {
	if (iid == AT1400XLDevice::kTypeID)
		return this;

	if (iid == IATDeviceButtons::kTypeID)
		return static_cast<IATDeviceButtons *>(mpModemDevice);

	return ATDeviceT::AsInterface(iid);
}

void AT1400XLDevice::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDef1400XL;
}

void AT1400XLDevice::GetSettings(ATPropertySet& pset) {
	mpModemDevice->GetSettings(pset);
}

bool AT1400XLDevice::SetSettings(const ATPropertySet& pset) {
	return mpModemDevice->SetSettings(pset);
}

void AT1400XLDevice::Init() {
	IATDeviceSchedulingService *dss = GetService<IATDeviceSchedulingService>();

	mpModemDevice->Init(dss->GetMachineScheduler(), dss->GetSlowScheduler(), GetService<IATDeviceIndicatorManager>(), GetService<IATAudioMixer>(), GetService<IATAsyncDispatcher>());

	mVDevice.Init();
	mTDevice.Init(*mpModemDevice);
}

void AT1400XLDevice::Shutdown() {
	mVDevice.Shutdown();
	mTDevice.Shutdown();

	mpModemDevice->Shutdown();

	if (mpMemMgr) {
		mpMemMgr->DeleteLayerPtr(&mpMemLayerHardware);
		mpMemMgr->DeleteLayerPtr(&mpMemLayerFirmware);
		mpMemMgr = nullptr;
	}

	mpModemDevice->Shutdown();
}

void AT1400XLDevice::ColdReset() {
	mVDevice.ColdReset();
	mTDevice.Reset();
	mpModemDevice->ColdReset();
}

void AT1400XLDevice::WarmReset() {
	mVDevice.WarmReset();
	mTDevice.Reset();
}

void AT1400XLDevice::EnableFirmware(bool enable, uint32 offset, uint8 regmask) {
	if (enable)
		mpMemMgr->SetLayerMemory(mpMemLayerFirmware, mFirmware + offset);

	mpMemMgr->EnableLayer(mpMemLayerFirmware, enable);
	mpMemMgr->EnableLayer(mpMemLayerHardware, enable);
	mActiveRegMask = regmask;
}

bool AT1400XLDevice::GetDiskAttnState() const {
	return mbDiskAttn;
}

void AT1400XLDevice::SetDiskAttnState(bool attn) {
	if (mbDiskAttn != attn) {
		mbDiskAttn = attn;

		if (mpFnOnDiskAttn)
			mpFnOnDiskAttn(attn);
	}
}

void AT1400XLDevice::SetOnDiskAttn(vdfunction<void(bool)> fn) {
	mpFnOnDiskAttn = std::move(fn);

	if (mpFnOnDiskAttn)
		mpFnOnDiskAttn(mbDiskAttn);
}

sint32 AT1400XLDevice::DebugReadByte(uint32 addr) const {
	if (addr >= 0xD110)
		return -1;

	if ((addr & 8) != mActiveRegMask)
		return -1;

	if (addr & 8) {
		return mTDevice.DebugReadByte(addr);
	}

	return -1;
}

sint32 AT1400XLDevice::ReadByte(uint32 addr) {
	if (addr >= 0xD110)
		return -1;

	if ((addr & 8) != mActiveRegMask)
		return -1;

	if (addr & 8) {
		return mTDevice.ReadByte(addr);
	}

	return -1;
}

bool AT1400XLDevice::WriteByte(uint32 addr, uint8 value) {
	// D1xx and /A4 qualify address -- A5-A7 are not checked
	if (addr >= 0xD1F0 || (addr & 0x10))
		return false;

	if ((addr & 8) != mActiveRegMask)
		return false;

	// $D100-D103 Votrax data latch (D0-D5); D7=1 enables Votrax IRQ
	// $D104-D107 Votrax strobe
	// $D108-D10B Modem control latch (W), cleared to $00 on reset
	// $D10C-D10F 6551A ACIA

	if (addr & 8) {
		mTDevice.WriteByte((uint8)addr, value);
	} else {
		mVDevice.WriteByte((uint8)addr, value);
	}

	return false;
}

void AT1400XLDevice::InitFirmware(ATFirmwareManager *fwman) {
	mpFwMgr = fwman;

	ReloadFirmware();
}

bool AT1400XLDevice::ReloadFirmware() {
	mFirmwareStatus = ATDeviceFirmwareStatus::Missing;

	const auto fwid = mpFwMgr->GetFirmwareOfType(kATFirmwareType_1400XLHandler, true);

	bool changed = false;
	bool valid = false;
	mpFwMgr->LoadFirmware(fwid, mFirmware, 0, sizeof mFirmware, &changed, nullptr, nullptr, nullptr, &valid);

	if (fwid) {
		mFirmwareStatus = ATDeviceFirmwareStatus::Invalid;

		// check for PBI bytes
		if (mFirmware[3] == 0x80 && mFirmware[11] == 0x91)
			mFirmwareStatus = ATDeviceFirmwareStatus::OK;
	}

	return changed;
}

const wchar_t *AT1400XLDevice::GetWritableFirmwareDesc(uint32 idx) const {
	return nullptr;
}

bool AT1400XLDevice::IsWritableFirmwareDirty(uint32 idx) const {
	return false;
}

void AT1400XLDevice::SaveWritableFirmware(uint32 idx, IVDStream& stream) {
}

ATDeviceFirmwareStatus AT1400XLDevice::GetFirmwareStatus() const {
	return mFirmwareStatus;
}

void AT1400XLDevice::InitMemMap(ATMemoryManager *memmgr) {
	mpMemMgr = memmgr;

	mpMemLayerFirmware = mpMemMgr->CreateLayer(kATMemoryPri_PBI, mFirmware, 0xD8, 0x08, true);
	mpMemMgr->SetLayerName(mpMemLayerFirmware, "1400XL PBI Firmware");

	ATMemoryHandlerTable handlers {};
	handlers.mbPassAnticReads = true;
	handlers.mbPassReads = true;
	handlers.mbPassWrites = true;
	handlers.mpThis = this;
	handlers.BindDebugReadHandler<&AT1400XLDevice::DebugReadByte>();
	handlers.BindReadHandler<&AT1400XLDevice::ReadByte>();
	handlers.BindWriteHandler<&AT1400XLDevice::WriteByte>();

	mpMemLayerHardware = mpMemMgr->CreateLayer(kATMemoryPri_PBI, handlers, 0xD1, 0x01);
	mpMemMgr->SetLayerName(mpMemLayerHardware, "1400XL PBI Hardware");
}

bool AT1400XLDevice::GetMappedRange(uint32 index, uint32& lo, uint32& hi) const {
	// Our address ranges are only in use while PBI devices are active, so
	// we don't need to report them.
	return false;
}
