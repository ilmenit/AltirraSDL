//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2014 Avery Lee
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
#include <vd2/system/binary.h>
#include <vd2/system/strutil.h>
#include <at/atcore/asyncdispatcher.h>
#include <at/atcore/cio.h>
#include <at/atcore/devicecio.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/deviceserial.h>
#include <at/atcore/devicesioimpl.h>
#include <at/atcore/ksyms.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/scheduler.h>
#include <at/atcore/sioutils.h>
#include "pia.h"
#include "cpu.h"
#include "cpumemory.h"
#include "uirender.h"
#include "rs232.h"
#include "modem.h"
#include "debuggerlog.h"
#include "firmwaremanager.h"

extern ATDebuggerLogChannel g_ATLCModemData;

namespace {
	static const uint8 kParityTable[16]={
		0x00, 0x80, 0x80, 0x00,
		0x80, 0x00, 0x00, 0x80,
		0x80, 0x00, 0x00, 0x80,
		0x00, 0x80, 0x80, 0x00,
	};
}

class ATRS232Channel1030 final : public IATSchedulerCallback {
public:
	enum class DeviceType : uint8 {
		Type835,
		Type1030,
		TypeXM301
	};

	ATRS232Channel1030(DeviceType type);

	void Init(ATScheduler *sched, ATScheduler *slowsched, IATDeviceIndicatorManager *uir, IATDeviceCIOManager *ciomgr, IATDeviceSIOManager *siomgr, IATDeviceRawSIO *siodev, IATAudioMixer *mixer, IATAsyncDispatcher *asyncDispatcher);
	void Shutdown();

	void ColdReset();

	uint8 Open(uint8 aux1, uint8 aux2);
	void Close();

	uint32 GetOutputLevel() const { return mOutputLevel; }

	void GetSettings(ATPropertySet& pset);
	void SetSettings(const ATPropertySet& pset);

	void GetStatus(uint8 status[4]);
	bool GetByte(uint8& c);
	sint32 PutByte(uint8 c);

	bool IsSuspended() const { return mbSuspended; }
	void ReceiveByte(uint8 c);
	bool ExecuteDeviceCommand(uint8 c);

	IATDeviceButtons *GetDeviceButtonsInterface() {
		return mpDevice;
	}

public:
	void OnScheduledEvent(uint32 id) override;

protected:
	void OnControlStateChanged(const ATDeviceSerialStatus& status);
	void PollDeviceInput();
	void PollDeviceOutput();
	void EnqueueReceivedByte(uint8 c);
	void WaitForCarrier();
	void NotifyCommandCompletion();

	ATScheduler	*mpScheduler = nullptr;
	ATScheduler	*mpSlowScheduler = nullptr;
	ATEvent	*mpSlowInputEvent = nullptr;
	ATEvent	*mpSlowOutputEvent = nullptr;
	ATEvent	*mpSlowCommandEvent = nullptr;
	ATEvent	*mpSlowHandlerEvent = nullptr;
	vdrefptr<ATModemEmulator> mpDevice;
	IATDeviceCIOManager *mpCIOMgr = nullptr;
	IATDeviceSIOManager *mpSIOMgr = nullptr;
	IATDeviceRawSIO *mpSIODev = nullptr;

	const DeviceType mDeviceType;
	bool	mbAddLFAfterEOL = false;
	bool	mbTranslationEnabled = true;
	bool	mbTranslationHeavy = false;
	bool	mbLFPending = false;
	bool	mbConcurrentMode = false;
	bool	mbSuspended = true;
	bool	mbHandlerMode = false;

	enum class HandlerState : uint8 {
		Base,
		Escape,
		SetTranslation1,
		SetTranslation2,
		SetParity,
		Dial,
		WaitForCarrier
	} mHandlerState = HandlerState::Escape;

	uint8	mWontTranslateChar = 0;

	uint8	mOpenPerms = 0;		// Mirror of ICAX1 at open time.
	uint8	mControlState = 0;

	// These must match the error flags returned by STATUS.
	enum : uint8 {
		kErrorFlag1030_FramingError = 0x80,
		kErrorFlag1030_Overrun = 0x40,
		kErrorFlag1030_Parity = 0x20,
		kErrorFlag1030_Wraparound = 0x10,
		kErrorFlag1030_IllegalCommand = 0x01,

		kStatusFlag_CarrierDetect = 0x80,
		kStatusFlag_Loopback = 0x20,
		kStatusFlag_Answer = 0x10,
		kStatusFlag_AutoAnswer = 0x08,
		kStatusFlag_ToneDial = 0x04,
		kStatusFlag_OffHook = 0x01
	};

	enum : uint16 {
		// Memory locations defined by the T: handler.
		CMCMD = 7,
		INCNT = 0x400,
		OUTCNT = 0x401
	};

	enum : uint32 {
		kEventId_Input = 1,
		kEventId_Output,
		kEventId_Command,
		kEventId_Handler,
	};

	uint8	mErrorFlags = 0;		// handler only
	uint8	mStatusFlags = 0;		// handler only

	// These must match bits 0-1 of AUX1 as passed to XIO 38.
	enum ParityMode {
		kParityMode_None	= 0x00,
		kParityMode_Odd		= 0x01,
		kParityMode_Even	= 0x02,
		kParityMode_Mark	= 0x03
	};

	ParityMode	mInputParityMode = kParityMode_None;
	ParityMode	mOutputParityMode = kParityMode_None;

	ATDeviceSerialTerminalState	mTerminalState {};

	int		mInputReadOffset = 0;
	int		mInputWriteOffset = 0;
	int		mInputLevel = 0;
	int		mOutputReadOffset = 0;
	int		mOutputWriteOffset = 0;
	int		mOutputLevel = 0;

	bool	mbDisableThrottling = false;
	bool	mbAnalogLoopback = false;
	bool	mbPulseDialing = false;
	bool	mbToneDialing = false;
	uint8	mPulseCounter = 0;

	VDStringA mDialNumber;

	enum class ControllerPhase : uint8 {
		None,
		PulseDialing,
		WaitForCarrier,
	} mControllerPhase = ControllerPhase::None;

	VDStringA mDialAddress;
	VDStringA mDialService;

	// This buffer is 256 bytes internally for T:.
	uint8	mInputBuffer[256];
	uint8	mOutputBuffer[32];
};

ATRS232Channel1030::ATRS232Channel1030(DeviceType type)
	: mDeviceType(type)
{
	mpDevice = new ATModemEmulator;
	mpDevice->Set1030Mode();
}

void ATRS232Channel1030::Init(ATScheduler *sched, ATScheduler *slowsched, IATDeviceIndicatorManager *uir, IATDeviceCIOManager *ciomgr, IATDeviceSIOManager *siomgr, IATDeviceRawSIO *siodev, IATAudioMixer *mixer, IATAsyncDispatcher *asyncDispatcher) {
	mpScheduler = sched;
	mpSlowScheduler = slowsched;
	mpCIOMgr = ciomgr;
	mpSIOMgr = siomgr;
	mpSIODev = siodev;

	mpDevice->SetOnStatusChange([this](const ATDeviceSerialStatus& status) { this->OnControlStateChanged(status); });
	mpDevice->Init(sched, slowsched, uir, mixer, asyncDispatcher);
	mpDevice->SetPhoneToAudioEnabled(false);

	// compute initial control state
	ATDeviceSerialStatus cstate(mpDevice->GetStatus());

	mStatusFlags = 0;

	mControlState = 0;
	
	if (cstate.mbCarrierDetect)
		mControlState += 0x80;
}

void ATRS232Channel1030::Shutdown() {
	mpDevice = nullptr;

	if (mpSlowScheduler) {
		mpSlowScheduler->UnsetEvent(mpSlowInputEvent);
		mpSlowScheduler->UnsetEvent(mpSlowOutputEvent);
		mpSlowScheduler->UnsetEvent(mpSlowCommandEvent);
		mpSlowScheduler->UnsetEvent(mpSlowHandlerEvent);
		mpSlowScheduler = nullptr;
	}

	mpScheduler = nullptr;

	mpCIOMgr = nullptr;
	mpSIOMgr = nullptr;
	mpSIODev = nullptr;
}

void ATRS232Channel1030::ColdReset() {
	Close();

	if (mpDevice) {
		mpDevice->ColdReset();
		mpDevice->OnHook();
		mpDevice->SetAudioToPhoneEnabled(false);
		mpDevice->SetPhoneToAudioEnabled(false);
	}

	mbPulseDialing = false;
	mbToneDialing = false;
	mbSuspended = true;
	mControllerPhase = ControllerPhase::None;
}

uint8 ATRS232Channel1030::Open(uint8 aux1, uint8 aux2) {
	// clear input/output buffer counters 
	mpCIOMgr->WriteByte(INCNT, 0);
	mpCIOMgr->WriteByte(OUTCNT, 0);
	mpCIOMgr->WriteByte(CMCMD, 0);
	mbHandlerMode = true;
	mInputLevel = 0;
	mInputReadOffset = 0;
	mInputWriteOffset = 0;
	mOutputLevel = 0;
	mOutputReadOffset = 0;
	mOutputWriteOffset = 0;
	mErrorFlags = 0;
	mbTranslationEnabled = true;
	mbTranslationHeavy = false;
	mbAddLFAfterEOL = false;
	mInputParityMode = kParityMode_None;
	mOutputParityMode = kParityMode_None;
	mbLFPending = false;
	return kATCIOStat_Success;
}

void ATRS232Channel1030::Close() {
	mbHandlerMode = false;
}

void ATRS232Channel1030::GetSettings(ATPropertySet& props) {
	if (!mDialAddress.empty())
		props.SetString("dialaddr", VDTextAToW(mDialAddress).c_str());

	if (!mDialService.empty())
		props.SetString("dialsvc", VDTextAToW(mDialService).c_str());

	if (mbDisableThrottling)
		props.SetBool("unthrottled", true);

	mpDevice->GetSettings(props);
}

void ATRS232Channel1030::SetSettings(const ATPropertySet& props) {
	mDialAddress = VDTextWToA(props.GetString("dialaddr", L""));
	mDialService = VDTextWToA(props.GetString("dialsvc", L""));

	mbDisableThrottling = props.GetBool("unthrottled", false);

	mpDevice->SetSettings(props);
}

void ATRS232Channel1030::GetStatus(uint8 status[4]) {
	status[0] = mErrorFlags;
	mErrorFlags = 0;

	status[1] = mControlState;

	// undocumented - required by AMODEM 7.5
	status[2] = mInputLevel;
	status[3] = mOutputLevel;
}

bool ATRS232Channel1030::GetByte(uint8& c) {
	if (mbDisableThrottling)
		PollDeviceInput();

	if (!mInputLevel)
		return false;

	c = mInputBuffer[mInputReadOffset];

	if (++mInputReadOffset >= 256)
		mInputReadOffset = 0;

	--mInputLevel;
	mpCIOMgr->WriteByte(INCNT, mInputLevel);

	if (mInputParityMode) {
		uint8 d = c;

		switch(mInputParityMode) {
			case kParityMode_Even:
				if (kParityTable[(d & 0x0F) ^ (d >> 4)] != 0x00)
					mErrorFlags |= kErrorFlag1030_Parity;
				break;

			case kParityMode_Odd:
				if (kParityTable[(d & 0x0F) ^ (d >> 4)] != 0x80)
					mErrorFlags |= kErrorFlag1030_Parity;
				break;
		}

		c &= 0x7F;
	}

	if (mbTranslationEnabled) {
		// convert CR to EOL
		if (c == 0x0D)
			c = 0x9B;
		else if (mbTranslationHeavy && (uint8)(c - 0x20) > 0x5C)
			c = mWontTranslateChar;
		else {
			// strip high bit
			c &= 0x7f;
		}
	}

	return true;
}

sint32 ATRS232Channel1030::PutByte(uint8 c) {
	switch(mHandlerState) {
		case HandlerState::Base:		// waiting for ESC or byte
			// check CMCMD
			if (mpCIOMgr->ReadByte(7)) {
				if (c == 0x1B) {
					mHandlerState = HandlerState::Escape;
					return kATCIOStat_Success;
				}

				return kATCIOStat_InvalidCmd;
			}

			if (mbLFPending)
				c = 0x0A;
			else if (mbTranslationEnabled) {
				// convert EOL to CR
				if (c == 0x9B)
					c = 0x0D;

				if (mbTranslationHeavy) {
					// heavy translation -- drop bytes <$20 or >$7C
					if ((uint8)(c - 0x20) > 0x5C && c != 0x0D)
						return kATCIOStat_Success;
				} else {
					// light translation - strip high bit
					c &= 0x7f;
				}
			}

			for(;;) {
				if (mOutputLevel >= sizeof mOutputBuffer)
					return -1;
		
				uint8 d = c;

				switch(mOutputParityMode) {
					case kParityMode_None:
					default:
						break;

					case kParityMode_Even:
						d ^= kParityTable[(d & 0x0F) ^ (d >> 4)];
						break;

					case kParityMode_Odd:
						d ^= kParityTable[(d & 0x0F) ^ (d >> 4)];
						d ^= 0x80;
						break;

					case kParityMode_Mark:
						d |= 0x80;
						break;
				}

				mOutputBuffer[mOutputWriteOffset] = d;
				if (++mOutputWriteOffset >= sizeof mOutputBuffer)
					mOutputWriteOffset = 0;

				++mOutputLevel;

				if (mbDisableThrottling)
					PollDeviceOutput();

				// If we just pushed out a CR byte and add-LF is enabled, we need to
				// loop around and push out another LF byte.
				if (c != 0x0D || !mbTranslationEnabled || !mbAddLFAfterEOL)
					break;

				mbLFPending = true;
				c = 0x0A;
			}

			mbLFPending = false;
			break;

		case HandlerState::Escape:		// waiting for command letter
			mHandlerState = HandlerState::Base;

			switch(c) {
				case 0x1B:	// ESC
					mHandlerState = HandlerState::Escape;
					break;

				case 'A':	// Set Translation [p1 p2]
					mHandlerState = HandlerState::SetTranslation1;
					break;

				case 'C':	// Set Parity [p1]
					mHandlerState = HandlerState::SetParity;
					break;

				case 'E':	// End of commands
					// clear CMCMD
					mpCIOMgr->WriteByte(7, 0);
					break;

				case 'F':	// Status
					{
						uint8 statusbuf[4];
						GetStatus(statusbuf);

						mpCIOMgr->WriteMemory(ATKernelSymbols::DVSTAT, statusbuf, 4);
					}
					break;

				case 'N':	// Set Pulse Dialing
					mStatusFlags &= ~kStatusFlag_ToneDial;
					break;

				case 'O':	// Set Tone Dialing
					mStatusFlags |= kStatusFlag_ToneDial;
					break;

				case 'H':	// Send Break Signal
					break;

				case 'I':	// Set Originate Mode
					mStatusFlags &= ~kStatusFlag_Answer;
					break;

				case 'J':	// Set Answer Mode
					mStatusFlags |= kStatusFlag_Answer;
					break;

				case 'K':	// Dial
					mDialNumber.clear();
					mHandlerState = HandlerState::Dial;

					mpDevice->OffHook();
					mControlState |= kStatusFlag_OffHook;
					break;

				case 'L':	// Pick up phone
					mpDevice->OffHook();
					mControlState |= kStatusFlag_OffHook;
					break;

				case 'M':	// Put phone on hook (hang up)
					mpDevice->OnHook();
					mControlState &= ~kStatusFlag_OffHook;
					break;

				case 'P':	// Start 30 second timeout
					break;

				case 'Q':	// Reset Modem
					break;

				case 'W':	// Set Analog Loopback Test
					mStatusFlags |= kStatusFlag_Loopback;
					ExecuteDeviceCommand(c);
					break;

				case 'X':	// Clear Analog Loopback Test
					mStatusFlags &= ~kStatusFlag_Loopback;
					ExecuteDeviceCommand(c);
					break;

				case 'Y':	// Resume Modem
					ExecuteDeviceCommand(c);
					break;

				case 'Z':	// Suspend Modem
					ExecuteDeviceCommand(c);
					break;

				default:
					mErrorFlags |= kErrorFlag1030_IllegalCommand;
					return kATCIOStat_InvalidCmd;
			}
			break;

		case HandlerState::SetTranslation1:
			mbAddLFAfterEOL = (c & 0x40) != 0;
			mbTranslationEnabled = !(c & 0x20);
			mbTranslationHeavy = (c & 0x10) != 0;

			mHandlerState = HandlerState::SetTranslation2;
			break;

		case HandlerState::SetTranslation2:
			mHandlerState = HandlerState::Base;
			mWontTranslateChar = c;
			break;

		case HandlerState::SetParity:
			mInputParityMode = (ParityMode)((c & 0x0c) >> 2);
			mOutputParityMode = (ParityMode)(c & 0x03);

			mHandlerState = HandlerState::Base;
			break;

		case HandlerState::Dial:
			c &= 0x0f;

			if (c < 0x0B) {
				if (mDialNumber.size() < 32)
					mDialNumber += c == 10 ? '0' : (char)('0' + c);
			} if (c == 0x0B) {
				mHandlerState = HandlerState::WaitForCarrier;

				mControllerPhase = ControllerPhase::WaitForCarrier;
				mpSlowScheduler->SetEvent(470992, this, kEventId_Handler, mpSlowHandlerEvent);

				mpDevice->Dial(mDialAddress.c_str(), mDialService.c_str(), nullptr, mDialNumber.c_str());
			}

			break;
	}

	return kATCIOStat_Success;
}

void ATRS232Channel1030::ReceiveByte(uint8 c) {
	// If the modem is suspended, data bytes are not sent.
	// When tone or pulse dialing, the transmitter is squelched.
	// If there is no active carrier, data bytes are lost.
	if (!mbSuspended && !mbToneDialing && !mbPulseDialing && (mControlState & kStatusFlag_CarrierDetect)) {
		g_ATLCModemData("Sending byte to modem: $%02X\n", c);
		if (mbAnalogLoopback)
			EnqueueReceivedByte(c);
		else
			mpDevice->Write(300, c);
	}
}

bool ATRS232Channel1030::ExecuteDeviceCommand(uint8 c) {
	if (mbPulseDialing && c != 'Q') {
		c &= 0x0F;

		if (c >= 0x0C)
			return false;

		if (c == 0x0B) {
			mbPulseDialing = false;
			c = 'L';
		} else {
			mPulseCounter = c * 2;
			mControllerPhase = ControllerPhase::PulseDialing;
			mpSlowScheduler->SetEvent(4710, this, kEventId_Command, mpSlowCommandEvent);

			return false;
		}
	}

	// Only Q and P are accepted in tone dialing mode; all other bytes are
	// ignored without ack.
	if (mbToneDialing) {
		if (c != 'Q' && c != 'P')
			return false;
	}

	// Only Q or Y are accepted while the modem is suspended
	if (mbSuspended) {
		if (c != 'Q' && c != 'Y')
			return false;
	}

	mpSlowScheduler->UnsetEvent(mpSlowCommandEvent);
	mControllerPhase = ControllerPhase::None;

	switch(c) {
		case 'G':	// Set Auto-Answer (XM301)
			if (mDeviceType == DeviceType::TypeXM301)
				mpDevice->SetAutoAnswer(true);
			break;

		case 'H':	// Send Break Signal
			// currently ignored
			break;

		case 'I':	// Set Originate Mode
			// currently ignored
			break;

		case 'J':	// Set Answer Mode
			// currently ignored
			break;

		case 'K':	// Pulse dial
			mpDevice->SetAudioToPhoneEnabled(false);
			mpDevice->OffHook();
			mbPulseDialing = true;

			if (mDeviceType != DeviceType::Type1030)
				mpDevice->SetPhoneToAudioEnabled(true);
			break;

		case 'L':	// Pick up phone
			mpDevice->OffHook();
			WaitForCarrier();
			break;

		case 'M':	// Put phone on hook (hang up)
			mpDevice->OnHook();
			mpDevice->SetAudioToPhoneEnabled(false);
			mbToneDialing = false;
			mbPulseDialing = false;

			if (mDeviceType != DeviceType::Type1030)
				mpDevice->SetPhoneToAudioEnabled(false);
			break;

		case 'O':	// Tone dial (1030/XM301)
			if (mDeviceType != DeviceType::Type835) {
				mpDevice->OffHook();
				mpDevice->SetAudioToPhoneEnabled(true);
				mbToneDialing = true;
			}
			break;

		case 'P':	// Start 30 second timeout (1030/XM301)
			if (mDeviceType != DeviceType::Type835) {
				mbToneDialing = false;
				mpDevice->OffHook();
				mpDevice->SetAudioToPhoneEnabled(false);
				WaitForCarrier();
			}
			break;

		case 'Q':	// Reset Modem
			mpDevice->OnHook();
			mpDevice->SetAudioToPhoneEnabled(false);
			mbToneDialing = false;
			mbPulseDialing = false;
			mbSuspended = true;
			mControllerPhase = ControllerPhase::None;
			mpSlowScheduler->UnsetEvent(mpSlowInputEvent);
			mpSlowScheduler->UnsetEvent(mpSlowOutputEvent);
			mpSlowScheduler->UnsetEvent(mpSlowCommandEvent);

			if (mDeviceType != DeviceType::Type1030)
				mpDevice->SetPhoneToAudioEnabled(false);
			break;

		case 'R':	// Enable sound (835/XM301)
			if (mDeviceType != DeviceType::Type1030)
				mpDevice->SetPhoneToAudioEnabled(true);
			break;

		case 'S':	// Disable sound (835/XM301)
			if (mDeviceType != DeviceType::Type1030)
				mpDevice->SetPhoneToAudioEnabled(false);
			break;

		case 'T':	// Disable Auto-Answer (XM301)
			if (mDeviceType == DeviceType::TypeXM301)
				mpDevice->SetAutoAnswer(false);
			break;

		case 'W':	// Set Analog Loopback Test
			if (!mbAnalogLoopback) {
				mbAnalogLoopback = true;
				OnControlStateChanged(mpDevice->GetStatus());
			}
			break;

		case 'X':	// Clear Analog Loopback Test
			if (mbAnalogLoopback) {
				mbAnalogLoopback = false;
				OnControlStateChanged(mpDevice->GetStatus());
			}
			break;

		case 'Y':	// Resume Modem
			mbSuspended = false;
			if (!mpSlowInputEvent)
				mpSlowInputEvent = mpSlowScheduler->AddEvent(523, this, kEventId_Input);
			if (!mpSlowOutputEvent)
				mpSlowOutputEvent = mpSlowScheduler->AddEvent(523, this, kEventId_Output);
			break;

		case 'Z':	// Suspend Modem
			mbSuspended = true;
			mpSlowScheduler->UnsetEvent(mpSlowInputEvent);
			mpSlowScheduler->UnsetEvent(mpSlowOutputEvent);
			break;
	}

	return true;
}

void ATRS232Channel1030::OnControlStateChanged(const ATDeviceSerialStatus& status) {
	uint32 oldState = mControlState;

	if (status.mbCarrierDetect || mbAnalogLoopback)
		mControlState |= kStatusFlag_CarrierDetect;
	else
		mControlState &= ~kStatusFlag_CarrierDetect;

	if (mbHandlerMode) {
		mStatusFlags = (mStatusFlags & ~kStatusFlag_CarrierDetect) | (mControlState & kStatusFlag_CarrierDetect);

		if (mHandlerState == HandlerState::WaitForCarrier) {
			if (status.mbCarrierDetect) {
				mHandlerState = HandlerState::Base;
				mpSlowScheduler->UnsetEvent(mpSlowHandlerEvent);
			}
		}
	} else {
		if (!mbSuspended) {
			if (mDeviceType != DeviceType::Type1030 && (mControlState & kStatusFlag_CarrierDetect))
				mpDevice->SetPhoneToAudioEnabled(false);

			if (mControllerPhase == ControllerPhase::WaitForCarrier && (mControlState & kStatusFlag_CarrierDetect)) {
				mControllerPhase = ControllerPhase::None;

				mpSlowScheduler->UnsetEvent(mpSlowCommandEvent);
			}

			if ((oldState ^ mControlState) & kStatusFlag_CarrierDetect) {
				mpSIOMgr->SetSIOInterrupt(mpSIODev, true);
				mpSIOMgr->SetSIOInterrupt(mpSIODev, false);
			}
		}
	}
}

void ATRS232Channel1030::OnScheduledEvent(uint32 id) {
	if (id == kEventId_Input) {
		mpSlowInputEvent = mpSlowScheduler->AddEvent(523, this, kEventId_Input);

		PollDeviceInput();
	} else if (id == kEventId_Output) {
		mpSlowOutputEvent = mpSlowScheduler->AddEvent(523, this, kEventId_Output);

		PollDeviceOutput();
	} else if (id == kEventId_Command) {
		mpSlowCommandEvent = nullptr;

		switch(mControllerPhase) {
			case ControllerPhase::PulseDialing:
				if (mPulseCounter) {
					--mPulseCounter;

					if (mPulseCounter & 1)
						mpDevice->OnHook();
					else
						mpDevice->OffHook();

					// Make/break ratio is about 30/70.
					uint32 delay = mPulseCounter & 1 ? 471 : 1099;

					if (mPulseCounter == 0)
						delay += 4710;

					mpSlowScheduler->SetEvent(delay, this, kEventId_Command, mpSlowCommandEvent);
				} else {
					NotifyCommandCompletion();
				}
				break;

			case ControllerPhase::WaitForCarrier:
				NotifyCommandCompletion();
				mControllerPhase = ControllerPhase::None;
				mpDevice->OnHook();

				if (mDeviceType != DeviceType::Type1030)
					mpDevice->SetPhoneToAudioEnabled(false);
				break;
		}
	} else if (id == kEventId_Handler) {
		mpSlowHandlerEvent = nullptr;

		switch(mHandlerState) {
			case HandlerState::WaitForCarrier:
				mHandlerState = HandlerState::Base;
				mControlState &= ~kStatusFlag_OffHook;
				mpDevice->OnHook();
				break;
		}
	}
}

void ATRS232Channel1030::PollDeviceOutput() {
	if (mOutputLevel) {
		--mOutputLevel;

		if (mpDevice) {
			const uint8 c = mOutputBuffer[mOutputReadOffset];
			g_ATLCModemData("Sending byte to modem: $%02X\n", c);
			mpDevice->Write(300, c);
		}

		if (++mOutputReadOffset >= sizeof mOutputBuffer)
			mOutputReadOffset = 0;
	}
}

void ATRS232Channel1030::PollDeviceInput() {
	if (mpDevice) {
		uint8 c;
		bool framingError;

		if (mInputLevel < 256 && mpDevice->Read(300, c, framingError)) {
			g_ATLCModemData("Receiving byte from modem: $%02X\n", c);

			if (framingError)
				mErrorFlags |= kErrorFlag1030_FramingError;

			EnqueueReceivedByte(c);
		}
	}

	if (mInputLevel && !mbHandlerMode) {
		const uint8 c = mInputBuffer[mInputReadOffset];

		if (++mInputReadOffset >= 256)
			mInputReadOffset = 0;

		--mInputLevel;
		mpSIOMgr->SendRawByte(c, 5966);
	}
}

void ATRS232Channel1030::EnqueueReceivedByte(uint8 c) {
	mInputBuffer[mInputWriteOffset] = c;

	if (++mInputWriteOffset >= 256)
		mInputWriteOffset = 0;

	++mInputLevel;

	if (mbHandlerMode)
		mpCIOMgr->WriteByte(INCNT, mInputLevel);
}

void ATRS232Channel1030::WaitForCarrier() {
	mControllerPhase = ControllerPhase::WaitForCarrier;

	if (mControlState & kStatusFlag_CarrierDetect) {
		NotifyCommandCompletion();
		mControllerPhase = ControllerPhase::None;
		mpSlowScheduler->UnsetEvent(mpSlowCommandEvent);
	} else
		mpSlowScheduler->SetEvent(470992, this, kEventId_Command, mpSlowCommandEvent);
}

void ATRS232Channel1030::NotifyCommandCompletion() {
	mpSIOMgr->SetSIOProceed(mpSIODev, true);
	mpSIOMgr->SetSIOProceed(mpSIODev, false);
}

//////////////////////////////////////////////////////////////////////////

class ATDevice1030Modem final
	: public ATDevice
	, public IATDeviceFirmware
	, public IATDeviceScheduling
	, public IATDeviceIndicators		
	, public IATDeviceAudioOutput		
	, public IATDeviceCIO
	, public ATDeviceSIO
	, public IATDeviceRawSIO
{
	ATDevice1030Modem(const ATDevice1030Modem&) = delete;
	ATDevice1030Modem& operator=(const ATDevice1030Modem&) = delete;
public:
	using DeviceType = ATRS232Channel1030::DeviceType;

	ATDevice1030Modem(DeviceType type);
	~ATDevice1030Modem();

	void *AsInterface(uint32 id) override;

public:
	void GetDeviceInfo(ATDeviceInfo& info) override;
	void GetSettings(ATPropertySet& settings) override;
	bool SetSettings(const ATPropertySet& settings) override;
	void Init() override;
	void Shutdown() override;
	void ColdReset() override;

public:	// IATDeviceFirmware
	void InitFirmware(ATFirmwareManager *fwman) override;
	bool ReloadFirmware() override;
	const wchar_t *GetWritableFirmwareDesc(uint32 idx) const override { return nullptr; }
	bool IsWritableFirmwareDirty(uint32 idx) const override { return false; }
	void SaveWritableFirmware(uint32 idx, IVDStream& stream) override {}
	ATDeviceFirmwareStatus GetFirmwareStatus() const override;

public:	// IATDeviceScheduling
	void InitScheduling(ATScheduler *sch, ATScheduler *slowsch) override;

public:	// IATDeviceIndicators
	void InitIndicators(IATDeviceIndicatorManager *r) override;

public:	// IATDeviceAudioOutput
	void InitAudioOutput(IATAudioMixer *mixer) override;

public:	// IATDeviceCIO
	void InitCIO(IATDeviceCIOManager *mgr) override;
	void GetCIODevices(IATDeviceCIODeviceList& deviceList) const override;
	sint32 OnCIOOpen(int channel, uint8 deviceNo, uint8 aux1, uint8 aux2, const uint8 *filename) override;
	sint32 OnCIOClose(int channel, uint8 deviceNo) override;
	sint32 OnCIOGetBytes(int channel, uint8 deviceNo, void *buf, uint32 len, uint32& actual) override;
	sint32 OnCIOPutBytes(int channel, uint8 deviceNo, const void *buf, uint32 len, uint32& actual) override;
	sint32 OnCIOGetStatus(int channel, uint8 deviceNo, uint8 statusbuf[4]) override;
	sint32 OnCIOSpecial(int channel, uint8 deviceNo, uint8 cmd, uint16 bufadr, uint16 buflen, uint8 aux[6]) override;
	void OnCIOAbortAsync() override;

public:	// ATDeviceSIO
	void InitSIO(IATDeviceSIOManager *mgr) override;
	CmdResponse OnSerialBeginCommand(const ATDeviceSIOCommand& cmd) override;

public:	// IATDeviceRawSIO
	void OnCommandStateChanged(bool asserted) override;
	void OnMotorStateChanged(bool asserted) override;
	void OnReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) override;
	void OnSendReady() override;

protected:
	ATScheduler *mpScheduler = nullptr;
	ATScheduler *mpSlowScheduler = nullptr;
	IATDeviceIndicatorManager *mpUIRenderer = nullptr;
	IATAudioMixer *mpAudioMixer = nullptr;
	IATDeviceCIOManager *mpCIOMgr = nullptr;
	IATDeviceSIOManager *mpSIOMgr = nullptr;
	vdrefptr<IATDeviceSIOInterface> mpSIOInterface;
	ATFirmwareManager *mpFwMgr = nullptr;

	ATRS232Channel1030 *mpChannel = nullptr;

	AT850SIOEmulationLevel mEmulationLevel {};

	uint32 mDiskCounter = 0;
	bool mbFirmwareUsable = false;
	const DeviceType mDeviceType;

	// AUTORUN.SYS and BOOT1030.COM style loaders hardcode a handler size of 0xB30, which
	// we must use. This comes from within 0x1100-1C2F in the ModemLink firmware. We also
	// keep the boot sector prepended to this.
	uint8 mFirmware[0x2880];
};

void ATCreateDevice1030Modem(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDevice1030Modem> p(new ATDevice1030Modem(ATDevice1030Modem::DeviceType::Type1030));

	*dev = p.release();
}

void ATCreateDevice835Modem(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDevice1030Modem> p(new ATDevice1030Modem(ATDevice1030Modem::DeviceType::Type835));

	*dev = p.release();
}

void ATCreateDeviceXM301Modem(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDevice1030Modem> p(new ATDevice1030Modem(ATDevice1030Modem::DeviceType::TypeXM301));

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDef1030Modem = { "1030", "1030", L"1030 Modem", ATCreateDevice1030Modem };
extern const ATDeviceDefinition g_ATDeviceDef835Modem = { "835", "835", L"835 Modem", ATCreateDevice835Modem };
extern const ATDeviceDefinition g_ATDeviceDefXM301Modem = { "xm301", "835", L"XM301 Modem", ATCreateDeviceXM301Modem };

ATDevice1030Modem::ATDevice1030Modem(DeviceType type)
	: mEmulationLevel(type != DeviceType::Type1030 ? kAT850SIOEmulationLevel_Full : kAT850SIOEmulationLevel_None)
	, mDiskCounter(0)
	, mDeviceType(type)
{
	// We have to init this early so it can accept settings.
	mpChannel = new ATRS232Channel1030(type);
}

ATDevice1030Modem::~ATDevice1030Modem() {
	Shutdown();
}

void *ATDevice1030Modem::AsInterface(uint32 id) {
	switch(id) {
		case IATDeviceFirmware::kTypeID:	return static_cast<IATDeviceFirmware *>(this);
		case IATDeviceScheduling::kTypeID:	return static_cast<IATDeviceScheduling *>(this);
		case IATDeviceIndicators::kTypeID:	return static_cast<IATDeviceIndicators *>(this);
		case IATDeviceAudioOutput::kTypeID:	return static_cast<IATDeviceAudioOutput *>(this);
		case IATDeviceCIO::kTypeID:			return static_cast<IATDeviceCIO *>(this);
		case IATDeviceSIO::kTypeID:			return static_cast<IATDeviceSIO *>(this);
		case IATDeviceRawSIO::kTypeID:		return static_cast<IATDeviceRawSIO *>(this);

		case IATDeviceButtons::kTypeID:
			return mpChannel ? mpChannel->GetDeviceButtonsInterface() : nullptr;
	}

	return ATDevice::AsInterface(id);
}

void ATDevice1030Modem::GetDeviceInfo(ATDeviceInfo& info) {
	switch(mDeviceType) {
		case DeviceType::Type835:
			info.mpDef = &g_ATDeviceDef835Modem;
			break;

		case DeviceType::Type1030:
		default:
			info.mpDef = &g_ATDeviceDef1030Modem;
			break;

		case DeviceType::TypeXM301:
			info.mpDef = &g_ATDeviceDefXM301Modem;
			break;
	}
}

void ATDevice1030Modem::GetSettings(ATPropertySet& settings) {
	if (mpChannel)
		mpChannel->GetSettings(settings);

	if (mEmulationLevel)
		settings.SetUint32("emulevel", (uint32)mEmulationLevel);
}

bool ATDevice1030Modem::SetSettings(const ATPropertySet& settings) {
	if (mpChannel)
		mpChannel->SetSettings(settings);

	if (mDeviceType == DeviceType::Type1030) {
		uint32 emulevel = settings.GetUint32("emulevel", 0);
		if (emulevel < kAT850SIOEmulationLevelCount) {
			auto newLevel = (AT850SIOEmulationLevel)emulevel;
		
			if (mEmulationLevel != newLevel) {
				if (mpCIOMgr) {
					if (newLevel == kAT850SIOEmulationLevel_Full)
						mpCIOMgr->RemoveCIODevice(this);
					else if (mEmulationLevel == kAT850SIOEmulationLevel_Full)
						mpCIOMgr->AddCIODevice(this);
				}

				if (mpSIOMgr) {
					if (newLevel == kAT850SIOEmulationLevel_None)
						mpSIOInterface = nullptr;
					else if (mEmulationLevel == kAT850SIOEmulationLevel_None)
						mpSIOInterface = mpSIOMgr->AddDevice(this);
				}

				mEmulationLevel = newLevel;
			}
		}
	}

	return true;
}

void ATDevice1030Modem::Init() {
	mpChannel->Init(mpScheduler, mpSlowScheduler, mpUIRenderer, mpCIOMgr, mpSIOMgr, this, mpAudioMixer, GetService<IATAsyncDispatcher>());
}

void ATDevice1030Modem::Shutdown() {
	if (mpCIOMgr) {
		mpCIOMgr->RemoveCIODevice(this);
		mpCIOMgr = nullptr;
	}

	mpSIOInterface = nullptr;

	if (mpSIOMgr) {
		mpSIOMgr->RemoveRawDevice(this);
		mpSIOMgr = nullptr;
	}

	if (mpChannel) {
		mpChannel->Shutdown();
		delete mpChannel;
		mpChannel = NULL;
	}

	mpScheduler = nullptr;
	mpSlowScheduler = nullptr;
	mpFwMgr = nullptr;

	mpUIRenderer = nullptr;
	mpAudioMixer = nullptr;
}

void ATDevice1030Modem::ColdReset() {
	if (mpChannel)
		mpChannel->ColdReset();

	mDiskCounter = 0;
}

void ATDevice1030Modem::InitFirmware(ATFirmwareManager *fwmgr) {
	mpFwMgr = fwmgr;

	ReloadFirmware();
}

bool ATDevice1030Modem::ReloadFirmware() {
	bool changed = false;

	mpFwMgr->LoadFirmware(mpFwMgr->GetCompatibleFirmware(kATFirmwareType_1030Firmware), mFirmware, 0, sizeof mFirmware, &changed, nullptr, nullptr, nullptr, &mbFirmwareUsable);

	return changed;
}

ATDeviceFirmwareStatus ATDevice1030Modem::GetFirmwareStatus() const {
	return mbFirmwareUsable ? ATDeviceFirmwareStatus::OK : ATDeviceFirmwareStatus::Missing;
}

void ATDevice1030Modem::InitScheduling(ATScheduler *sch, ATScheduler *slowsch) {
	mpScheduler = sch;
	mpSlowScheduler = slowsch;
}

void ATDevice1030Modem::InitIndicators(IATDeviceIndicatorManager *r) {
	mpUIRenderer = r;
}

void ATDevice1030Modem::InitAudioOutput(IATAudioMixer *mixer) {
	mpAudioMixer = mixer;
}

void ATDevice1030Modem::InitCIO(IATDeviceCIOManager *mgr) {
	mpCIOMgr = mgr;

	if (mEmulationLevel != kAT850SIOEmulationLevel_Full)
		mpCIOMgr->AddCIODevice(this);
}

void ATDevice1030Modem::GetCIODevices(IATDeviceCIODeviceList& deviceList) const {
	deviceList.AddDevice('T');
}

sint32 ATDevice1030Modem::OnCIOOpen(int channel, uint8 deviceNo, uint8 aux1, uint8 aux2, const uint8 *filename) {
	if (deviceNo != 1)
		return kATCIOStat_UnkDevice;

	return mpChannel->Open(aux1, aux2);
}

sint32 ATDevice1030Modem::OnCIOClose(int channel, uint8 deviceNo) {
	// wait for output buffer to drain (requires assist)
	if (mpChannel->GetOutputLevel())
		return -1;

	mpChannel->Close();
	return kATCIOStat_Success;
}

sint32 ATDevice1030Modem::OnCIOGetBytes(int channel, uint8 deviceNo, void *buf, uint32 len, uint32& actual) {
	if (mpCIOMgr->IsBreakActive())
		return kATCIOStat_Break;

	while(len--) {
		uint8 c;
		if (!mpChannel->GetByte(c))
			return -1;

		++actual;
		*(uint8 *)buf = c;
		buf = (uint8 *)buf + 1;
	}

	return kATCIOStat_Success;
}

sint32 ATDevice1030Modem::OnCIOPutBytes(int channel, uint8 deviceNo, const void *buf, uint32 len, uint32& actual) {
	if (mpCIOMgr->IsBreakActive())
		return kATCIOStat_Break;

	while(len--) {
		sint32 rval = mpChannel->PutByte(*(const uint8 *)buf);
		if (rval != kATCIOStat_Success) {
			if (rval >= 0)
				++actual;

			return rval;
		}

		++actual;
		buf = (const uint8 *)buf + 1;
	}

	return kATCIOStat_Success;
}

sint32 ATDevice1030Modem::OnCIOGetStatus(int channel, uint8 deviceNo, uint8 statusbuf[4]) {
	mpChannel->GetStatus(statusbuf);
	return kATCIOStat_Success;
}

sint32 ATDevice1030Modem::OnCIOSpecial(int channel, uint8 deviceNo, uint8 cmd, uint16 bufadr, uint16 buflen, uint8 aux[6]) {
	return kATCIOStat_NotSupported;
}

void ATDevice1030Modem::OnCIOAbortAsync() {
}

void ATDevice1030Modem::InitSIO(IATDeviceSIOManager *mgr) {
	mpSIOMgr = mgr;

	if (mDeviceType == DeviceType::Type1030 && mEmulationLevel != kAT850SIOEmulationLevel_None)
		mpSIOInterface = mpSIOMgr->AddDevice(this);

	mpSIOMgr->AddRawDevice(this);
}

IATDeviceSIO::CmdResponse ATDevice1030Modem::OnSerialBeginCommand(const ATDeviceSIOCommand& cmd) {
	if (!cmd.mbStandardRate || !mpChannel->IsSuspended())
		return kCmdResponse_NotHandled;

	if (cmd.mDevice == 0x31) {
		if (cmd.mCommand == 0x53) {			// status
			// The 1030 answers on the 13th D1: status request.
			if (mDiskCounter >= 13) {
				mpSIOInterface->BeginCommand();
				mpSIOInterface->SendACK();
				mpSIOInterface->SendComplete();

				const uint8 status[4]={
					0x00, 0x00, 0xE0, 0x00
				};

				mpSIOInterface->SendData(status, 4, true);
				mpSIOInterface->EndCommand();
				return kCmdResponse_Start;
			}

			++mDiskCounter;
		} else {
			if (cmd.mCommand == 0x52 && mDiskCounter >= 13) {	// read sector
				uint32 sector = VDReadUnalignedLEU16(cmd.mAUX);

				if (sector != 1)
					return kCmdResponse_Fail_NAK;

				mpSIOInterface->BeginCommand();
				mpSIOInterface->SendACK();
				mpSIOInterface->SendComplete();
				mpSIOInterface->SendData(mFirmware, 0x80, true);
				mpSIOInterface->EndCommand();

				return kCmdResponse_Start;

			} else {
				mDiskCounter = 0;
			}
		}

		return kCmdResponse_NotHandled;
	}

	if (cmd.mDevice != 0x58)
		return kCmdResponse_NotHandled;

	// The 8050 on the 1030 runs at a master clock of 4.032MHz, which
	// results in a machine cycle rate of 4.032MHz / 15 = 268.8KHz. The
	// bit rate is precisely 19200 baud; the byte rate depends on the
	// routine sending from each block.

	// internal memory bank 0 - 157 machine cycles/byte
	static constexpr uint32 kCpbInternalMB0 = 1045;

	// internal memory bank 1 - 159 machine cycles/byte
	static constexpr uint32 kCpbInternalMB1 = 1059;

	// external, not ending page - 155 machine cycles/byte
	static constexpr uint32 kCpbExternal = 1032;

	// external, ending page - 159 machine cycles/byte
	static constexpr uint32 kCpbExternalEndingPage = 1059;

	const auto SendTHandler = [&] {
		// T: handler, internal page in bank 0
		mpSIOInterface->SetTransferRate(93, kCpbInternalMB0);
		mpSIOInterface->SendData(mFirmware + 0x80 + 0x1100, 0x100, false);

		// T: handler, internal page in bank 1
		mpSIOInterface->SetTransferRate(93, kCpbInternalMB1);
		mpSIOInterface->SendData(mFirmware + 0x80 + 0x1200, 0x780, false);

		// T: handler, external
		mpSIOInterface->SetTransferRate(93, kCpbExternal);
		mpSIOInterface->SendData(mFirmware + 0x80 + 0x1980, 0x200, false);

		// T: handler, external ending page
		mpSIOInterface->SetTransferRate(93, kCpbExternalEndingPage);
		mpSIOInterface->SendData(mFirmware + 0x80 + 0x1B80, 0xB0, false);
	};

	if (cmd.mCommand == 0x3B) {
		if (mEmulationLevel == kAT850SIOEmulationLevel_Full) {
			// ModemLink software load -- return $2800 bytes to load at $C00
			//
			// The 1030 sends data at three different rates depending on which
			// portion of the firmware is being sent. The speeds below are calibrated
			// based on measurements from the actual hardware.

			mpSIOInterface->BeginCommand();
			mpSIOInterface->SendACK();
			mpSIOInterface->SendComplete();

			if (mpSIOInterface->IsActiveCommandAccelerated()) {
				mpSIOInterface->SendData(mFirmware + 0x80, 0x2800, true);
			} else {
				// ModemLink part 1
				mpSIOInterface->SetTransferRate(93, kCpbExternal);
				mpSIOInterface->SendData(mFirmware + 0x80, 0x1100, false);

				// T: handler
				SendTHandler();

				// ModemLink part 2, external
				mpSIOInterface->SetTransferRate(93, kCpbExternal);
				mpSIOInterface->SendData(mFirmware + 0x80 + 0x1C30, 0xB40, false);

				// ModemLink part 2, external ending page
				mpSIOInterface->SetTransferRate(93, kCpbExternalEndingPage);
				mpSIOInterface->SendData(mFirmware + 0x80 + 0x2770, 0x90, false);

				const uint8 checksum = ATComputeSIOChecksum(mFirmware+0x80, 0x2800);
				mpSIOInterface->SendData(&checksum, 1, false);
			}

			mpSIOInterface->EndCommand();
			return kCmdResponse_Start;
		}
	} else if (cmd.mCommand == 0x3C) {
		// Handler load - return $B30 bytes to load at $1D00 and run at $1D0C
		mpSIOInterface->BeginCommand();
		mpSIOInterface->SendACK();
		mpSIOInterface->SendComplete();

		if (mEmulationLevel == kAT850SIOEmulationLevel_Full) {
			// Send the embedded handler within the ModemLink firmware. This is a
			// subset of the data sent by the $3B command, with the same rates.
			//
			// If an accelerated command is being processed, we can't send in chunks;
			// there would be no point anyway.

			if (mpSIOInterface->IsActiveCommandAccelerated()) {
				mpSIOInterface->SendData(mFirmware + 0x1180, 0xB30, true);
			} else {
				SendTHandler();

				const uint8 checksum = ATComputeSIOChecksum(mFirmware + 0x1180, 0xB30);
				mpSIOInterface->SendData(&checksum, 1, false);
			}

		} else {
			vdfastvector<uint8> buf(0xB30, 0);
			buf[0x0C] = 0x60;		// RTS
			mpSIOInterface->SendData(buf.data(), (uint32)buf.size(), true);
		}

		mpSIOInterface->EndCommand();
		return kCmdResponse_Start;
	}

	return kCmdResponse_NotHandled;
}

void ATDevice1030Modem::OnCommandStateChanged(bool asserted) {
}

void ATDevice1030Modem::OnMotorStateChanged(bool asserted) {
}

void ATDevice1030Modem::OnReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) {
	// check for proper 300 baud operation (divisor = 2982, 5% tolerance)
	if (cyclesPerBit > 5666 && cyclesPerBit < 6266 && mpChannel) {
		if (command) {
			if (mpChannel->ExecuteDeviceCommand(c)) {
				mpSIOMgr->SetSIOProceed(this, true);
				mpSIOMgr->SetSIOProceed(this, false);
			}
		} else
			mpChannel->ReceiveByte(c);
	}
}

void ATDevice1030Modem::OnSendReady() {
}
