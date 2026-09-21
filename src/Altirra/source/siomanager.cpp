//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2012 Avery Lee
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
#include <vd2/system/vdstl_algorithm.h>
#include <at/atcore/cio.h>
#include <at/atcore/enumparseimpl.h>
#include <at/atcore/snapshotimpl.h>
#include "siomanager.h"
#include "simulator.h"
#include "cpu.h"
#include "cpuhookmanager.h"
#include "disk.h"
#include "kerneldb.h"
#include "debuggerlog.h"
#include "uirender.h"
#include "hleutils.h"
#include "cassette.h"
#include "savestate.h"
#include "trace.h"
#include <at/atcore/sioutils.h>

ATDebuggerLogChannel g_ATLCHookSIOReqs(false, false, "HOOKSIOREQS", "OS SIO hook requests");
ATDebuggerLogChannel g_ATLCHookSIO(false, false, "HOOKSIO", "OS SIO hook messages");
ATDebuggerLogChannel g_ATLCSIOCmd(false, false, "SIOCMD", "SIO bus commands");
ATDebuggerLogChannel g_ATLCSIOAccel(false, false, "SIOACCEL", "SIO command acceleration");
ATDebuggerLogChannel g_ATLCSIOSteps(false, false, "SIOSTEPS", "SIO command steps");
ATDebuggerLogChannel g_ATLCSIOReply(false, false, "SIOREPLY", "SIO command reply");

AT_DECLARE_ENUM_TABLE(ATSIOManager::StepType);

AT_DEFINE_ENUM_TABLE_BEGIN(ATSIOManager::StepType)
	{ ATSIOManager::kStepType_None,							"none" },
	{ ATSIOManager::kStepType_Delay,						"delay" },
	{ ATSIOManager::kStepType_Send,							"send" },
	{ ATSIOManager::kStepType_SendAutoProtocol,				"sendauto" },
	{ ATSIOManager::kStepType_Receive,						"receive" },
	{ ATSIOManager::kStepType_ReceiveAutoProtocol,			"receiveauto" },
	{ ATSIOManager::kStepType_SetTransferRate,				"setxferrate" },
	{ ATSIOManager::kStepType_SetSynchronousTransmit,		"setsyncxfer" },
	{ ATSIOManager::kStepType_Fence,						"fence" },
	{ ATSIOManager::kStepType_EndCommand,					"end" },
AT_DEFINE_ENUM_TABLE_END(ATSIOManager::StepType, ATSIOManager::kStepType_None);

////////////////////////////////////////////////////////////////////////////////

class ATSaveStateSioManager final : public ATSnapExchangeObject<ATSaveStateSioManager, "ATSaveStateSioManager"> {
public:
	template<typename T>
	void Exchange(T& rw) {
		rw.Transfer("command_cycles_per_bit", &mCommandCyclesPerBit);
		rw.Transfer("command_buffer_index", &mCommandBufferIndex);
		rw.TransferArray("command_buffer", mCommandBuffer);

		if constexpr (T::IsReader) {
			if (mCommandBufferIndex > 5)
				throw ATInvalidSaveStateException();
		}
	}

	uint32 mCommandCyclesPerBit = 0;
	uint8 mCommandBufferIndex = 0;
	uint8 mCommandBuffer[5] {};
};

class ATSaveStateSioCommandStep; // forward declaration for ATSaveStateSioActiveCommand

class ATSaveStateSioActiveCommand final : public ATSnapExchangeObject<ATSaveStateSioActiveCommand, "ATSaveStateSioActiveCommand"> {
public:
	template<typename T>
	void Exchange(T& rw) {
		rw.Transfer("device_id", &mDeviceId);
		rw.Transfer("transfer_start_time", &mTransferStartTime);
		rw.Transfer("transfer_index", &mTransferIndex);
		rw.Transfer("transfer_error", &mbTransferError);
		rw.Transfer("transfer_cycles_per_bit", &mTransferCyclesPerBit);
		rw.Transfer("transfer_cycles_per_byte", &mTransferCyclesPerByte);
		rw.Transfer("transmit_synchronous", &mbTransmitSynchronous);
		rw.Transfer("current_step", &mpCurrentStep);
		rw.Transfer("step_delay", &mStepDelay);
		rw.Transfer("steps", &mSteps);
		rw.Transfer("queue_time", &mQueueTime);
		rw.Transfer("queue_cycles_per_byte", &mQueueCyclesPerByte);
		rw.Transfer("command_frame_end_time", &mCommandFrameEndTime);
		rw.Transfer("command_deassert_time", &mCommandDeassertTime);

		if constexpr (T::IsReader) {
			if (mTransferIndex > ATSIOManager::kMaxTransferSize)
				throw ATInvalidSaveStateException();
		}
	}

	uint8 mDeviceId = 0;
	sint32 mTransferStartTime = 0;
	uint32 mTransferIndex = 0;
	bool mbTransferError = false;
	uint32 mStepDelay = 0;
	uint32 mTransferCyclesPerBit = 0;
	uint32 mTransferCyclesPerByte = 0;
	bool mbTransmitSynchronous = false;
	sint64 mQueueTime = 0;
	uint32 mQueueCyclesPerByte = 0;
	sint64 mCommandFrameEndTime = 0;
	sint64 mCommandDeassertTime = 0;
	vdvector<vdrefptr<ATSaveStateSioCommandStep>> mSteps;
	vdrefptr<ATSaveStateSioCommandStep> mpCurrentStep;
};

class ATSaveStateSioCommandStep final : public ATSnapExchangeObject<ATSaveStateSioCommandStep, "ATSaveStateSioCommandStep"> {
public:
	ATSaveStateSioCommandStep() = default;
	ATSaveStateSioCommandStep(const ATSIOManager::Step& step)
		: mStep(step)
	{
	}

	template<typename T>
	void Exchange(T& rw) {
		ATSIOManager::StepType stepType;
		uint32 arg1 = 0;
		uint32 arg2 = 0;

		if constexpr (T::IsWriter) {
			stepType = mStep.mType;
			arg1 = 0;
			arg2 = 0;

			switch(mStep.mType) {
				case ATSIOManager::kStepType_Delay:
					arg1 = mStep.mDelayTicks;
					break;

				case ATSIOManager::kStepType_Send:
				case ATSIOManager::kStepType_SendAutoProtocol:
					arg1 = mStep.mTransferLength;
					break;

				case ATSIOManager::kStepType_Receive:
				case ATSIOManager::kStepType_ReceiveAutoProtocol:
					arg1 = mStep.mTransferLength;
					arg2 = mStep.mTransferId;
					break;

				case ATSIOManager::kStepType_SetTransferRate:
					arg1 = mStep.mTransferCyclesPerByte;
					arg2 = mStep.mTransferCyclesPerBit;
					break;

				case ATSIOManager::kStepType_SetSynchronousTransmit:
					arg1 = mStep.mbEnable;
					break;

				case ATSIOManager::kStepType_Fence:
					arg1 = mStep.mFenceId;
					break;

				case ATSIOManager::kStepType_EndCommand:
					break;
			}
		}

		rw.TransferEnum("step_type", &stepType);
		rw.Transfer("arg1", &arg1);
		rw.Transfer("arg2", &arg2);
		rw.Transfer("transfer_data", &mTransferData);

		if constexpr (T::IsReader) {
			mStep.mType = stepType;

			switch(stepType) {
				case ATSIOManager::kStepType_Delay:
					mStep.mDelayTicks = arg1;
					break;

				case ATSIOManager::kStepType_Send:
				case ATSIOManager::kStepType_SendAutoProtocol:
					mStep.mTransferLength = arg1;

					if (mTransferData.size() != arg1)
						throw ATInvalidSaveStateException();
					break;

				case ATSIOManager::kStepType_Receive:
				case ATSIOManager::kStepType_ReceiveAutoProtocol:
					mStep.mTransferLength = arg1;
					mStep.mTransferId = arg2;
					break;

				case ATSIOManager::kStepType_SetTransferRate:
					mStep.mTransferCyclesPerByte = arg1;
					mStep.mTransferCyclesPerBit = arg2;

					if (!mStep.mTransferCyclesPerByte || !mStep.mTransferCyclesPerBit)
						throw ATInvalidSaveStateException();
					break;

				case ATSIOManager::kStepType_SetSynchronousTransmit:
					mStep.mbEnable = arg1 != 0;
					break;

				case ATSIOManager::kStepType_Fence:
					mStep.mFenceId = arg1;
					break;

				case ATSIOManager::kStepType_EndCommand:
					break;
			}
		}
	}

	ATSIOManager::Step mStep {};
	vdfastvector<uint8> mTransferData;
};

////////////////////////////////////////////////////////////////////////////////

class ATSIOManager::RawDeviceListLock {
public:
	RawDeviceListLock(ATSIOManager *parent);
	~RawDeviceListLock();

protected:
	ATSIOManager *const mpParent;
};

ATSIOManager::RawDeviceListLock::RawDeviceListLock(ATSIOManager *parent)
	: mpParent(parent)
{
	parent->mSIORawDevicesBusy += 2;
}

ATSIOManager::RawDeviceListLock::~RawDeviceListLock() {
	mpParent->mSIORawDevicesBusy -= 2;

	if (mpParent->mSIORawDevicesBusy < 0) {
		mpParent->mSIORawDevicesBusy = 0;

		// remove dead devices
		mpParent->mSIORawDevices.erase(
			std::remove_if(mpParent->mSIORawDevices.begin(), mpParent->mSIORawDevices.end(),
				[](IATDeviceRawSIO *p) { return p == nullptr; }
			)
		);

		// add new devices
		mpParent->mSIORawDevices.insert(mpParent->mSIORawDevices.end(), mpParent->mSIORawDevicesNew.begin(), mpParent->mSIORawDevicesNew.end());
		mpParent->mSIORawDevicesNew.clear();
	}
}

///////////////////////////////////////////////////////////////////////////

class ATSIOManager::SIOInterface final : public IATDeviceSIOInterface, public IATSchedulerCallback {
public:
	SIOInterface(ATSIOManager& parent, IATDeviceSIO& dev);
	~SIOInterface();

	bool IsCommandActive() const;

	void Shutdown();

	int AddRef() override;
	int Release() override;

	uint32 GetAccelTimeSkew() const override { return mAccelTimeSkew; }
	uint64 GetCommandQueueTime() const override { return mCommandQueueTime; }
	uint64 GetCommandFrameEndTime() const override { return mCommandFrameEndTime; }
	uint64 GetCommandDeassertTime() const override { return mCommandDeassertTime; }

	bool IsActiveCommandAccelerated() const override { return mParent.mpAccelRequest != nullptr; }

	void SetCommandDeassertCheckEnabled(bool enabled) override { mbCommandDeassertCheckEnabled = enabled; }
	void SetCommandTruncationEnabled(bool enabled) override { mbCommandTruncationEnabled = enabled; }
	void SetReceiveByteTimeout(uint64 cycles) override { mReceiveByteTimeout = cycles; }
	void SetReceiveFrameTimeout(uint64 cycles) override { mReceiveFrameTimeout = cycles; }

	void BeginCommand() override;
	void SendData(const void *data, uint32 len, bool addChecksum) override;
	void SendACK() override;
	void SendNAK() override;
	void SendComplete(bool autoDelay) override;
	void SendError(bool autoDelay) override;
	void ReceiveData(uint32 id, uint32 len, bool autoProtocol) override;
	void SetTransferRate(uint32 cyclesPerBit, uint32 cyclesPerByte) override;
	void SetSynchronousTransmit(bool enable) override;
	void Delay(uint32 ticks) override;
	void InsertFence(uint32 id) override;
	void FlushQueue() override;
	void EndCommand() override;
	void HandleCommand(const void *data, uint32 len, bool succeeded) override;

	void SaveActiveCommandState(IATObjectState **state) const override;
	void LoadActiveCommandState(IATObjectState *state) override;

	enum class AccelResult {
		NotHandled,
		BypassAccel,
		Handled
	};

	AccelResult TryAccelCommand(const ATDeviceSIORequest& devreq);
	bool TryProcessCommand(const ATDeviceSIOCommand& cmd);
	void CancelCommand();

	bool OnReceive(uint8 c, uint32 cyclesPerBit, bool framingError, bool truncated);
	bool OnSendReady();

	void ExecuteNextStep();
	void ResetTransfer();
	void ResetTransferParams();
	void UpdateTransferRateDerivedValues();
	void ShiftTransmitBuffer();

public:
	void OnScheduledEvent(uint32 id) override;

public:
	enum {
		kEventId_Delay = 1,
		kEventId_Send
	};

	ATSIOManager& mParent;
	IATDeviceSIO& mDevice;

	uint32 mTransferCyclesPerBit = 0;
	uint32 mTransferCyclesPerBitRecvMin = 0;
	uint32 mTransferCyclesPerBitRecvMax = 0;
	uint32 mTransferCyclesPerByte = 0;
	uint64 mTransferStartTime = 0;	// starting cycle timestamp for transfer
	uint32 mTransferBurstOffset = 0;
	uint32 mTransferLastBurstOffset = 0;
	
	uint32 mTransferStart = 0;		// Starting offset for current transfer.
	uint32 mTransferIndex = 0;		// Next byte to send/receive for current transfer.
	uint32 mTransferEnd = 0;		// Stopping offset for current transfer.

	bool mbTransferSend = false;	// current transfer is a send operation
	bool mbTransferError = false;	// current transfer has a framing or baud rate error
	bool mbTransmitSynchronous = false;	// true if sends are being done as synchronous to the receive clock

	bool mbCommandActive = false;	// true if a command is in progress on this interface

	bool mbCommandDeassertCheckEnabled = false;
	bool mbCommandTruncationEnabled = false;

	bool mbActiveDeviceDisk = false;
	uint8 mActiveDeviceId = 0;

	ATEvent *mpDelayEvent = nullptr;
	ATEvent *mpTransferEvent = nullptr;

	// Counts equivalent cycles that would have been passed by the events that
	// occurred during request acceleration.
	uint32 mAccelTimeSkew = 0;

	// Cycles until a receive operation times out, for each byte.
	uint64 mReceiveByteTimeout = UINT64_MAX;

	// Cycles until a receive operation times out, for the entire command/data frame.
	uint64 mReceiveFrameTimeout = UINT64_MAX;

	// Time base from which to estimate receive byte timeout timer.
	uint64 mReceiveTimeoutBase = 0;

	uint64 mCommandFrameEndTime = 0;
	uint64 mCommandDeassertTime = 0;
	uint64 mCommandQueueTime = 0;
	uint32 mCommandQueueCyclesPerByte = 0;

	Step mCurrentStep = {};

	vdfastdeque<Step> mStepQueue;

	vdfastvector<uint8> mTransferBuffer;

	VDAtomicInt mRefCount { 0 };
};

ATSIOManager::SIOInterface::SIOInterface(ATSIOManager& parent, IATDeviceSIO& dev)
	: mParent(parent)
	, mDevice(dev)
{
	mCurrentStep.mType = kStepType_None;
}

ATSIOManager::SIOInterface::~SIOInterface() {
	Shutdown();
}

bool ATSIOManager::SIOInterface::IsCommandActive() const {
	return mbCommandActive;
}

void ATSIOManager::SIOInterface::Shutdown() {
	mParent.mpScheduler->UnsetEvent(mpDelayEvent);
	mParent.mpScheduler->UnsetEvent(mpTransferEvent);
}

int ATSIOManager::SIOInterface::AddRef() {
	return ++mRefCount;
}

int ATSIOManager::SIOInterface::Release() {
	int rc = --mRefCount;
	if (!rc) {
		mParent.RemoveInterface(*this);
		delete this;
	}

	return rc;
}

void ATSIOManager::SIOInterface::BeginCommand() {
	// we may need to cancel a previous active command if truncation is enabled
	if (mbCommandActive)
		CancelCommand();

	VDASSERT(mStepQueue.empty());

	mCommandDeassertTime = mParent.mCommandDeassertTime;
	mCommandFrameEndTime = mParent.mCommandFrameEndTime;
	mCommandQueueTime = mParent.mpScheduler->GetTick64();

	mTransferBuffer = std::move(mParent.mCachedTransferBuffer);
	mTransferBuffer.clear();

	mStepQueue.swap(mParent.mCachedStepQueue);
	mStepQueue.clear();

	mParent.BeginCommand();

	if (mParent.mpAccelRequest) {
		// Apply acceleration skew for the time it takes to send the command:
		// - approx 1.5ms from command assert to start of command frame
		// - 5 bytes of 94 cycles/bit
		// - approx 0.58 ms from end of command frame to command deassert
		mAccelTimeSkew += 2685 + 940 * 5 + 1040;
	}

	VDASSERT(!mParent.mActiveCommandInterfaces.Contains(this));
	mParent.mActiveCommandInterfaces.Add(this);

	mActiveDeviceId = mParent.mPendingDeviceId;
	mbActiveDeviceDisk = mParent.mbPendingDeviceDisk;

	ResetTransferParams();

	mTransferStart = 0;
	mTransferIndex = 0;
	mTransferEnd = 0;

	mbCommandActive = true;
}

void ATSIOManager::SIOInterface::SendData(const void *data, uint32 len, bool addChecksum) {
	if (!IsCommandActive())
		return;

	VDASSERT(mTransferIndex <= mTransferBuffer.size());

	if (!len)
		return;

	uint32 spaceRequired = len;

	if (addChecksum)
		++spaceRequired;

	if (kMaxTransferSize - mTransferBuffer.size() < spaceRequired) {
		// The transmit buffer is full -- let's see if we can shift it down.
		ShiftTransmitBuffer();

		// check again
		if (kMaxTransferSize - mTransferBuffer.size() < spaceRequired) {
			VDASSERT(!"No room left in transfer buffer.");
			return;
		}
	}

	mCommandQueueTime += (len + (addChecksum ? 1 : 0)) * mCommandQueueCyclesPerByte;

	Step& step = mStepQueue.push_back();
	step.mType = addChecksum ? kStepType_SendAutoProtocol : kStepType_Send;
	step.mTransferLength = spaceRequired;
	
	const uint8 *data8 = (const uint8 *)data;
	mTransferBuffer.insert_range(mTransferBuffer.end(), vdspan(data8, len));

	if (addChecksum)
		mTransferBuffer.push_back(ATComputeSIOChecksum(data8, len));

	ExecuteNextStep();
}

void ATSIOManager::SIOInterface::SendACK() {
	// This command, and all other commands, need to be silently ignored when no device
	// is active. We need to support this to allow an EndCommand() to be preemptively
	// inserted into a command stream off of a receive callback. In that case, surrounding
	// code that is being unwound may still attempt to push a couple of additional commands.
	if (!IsCommandActive())
		return;

	if (mParent.mpAccelRequest) {
		Step& step = mStepQueue.push_back();
		step.mType = kStepType_AccelSendACK;
	} else {
		SendData("A", 1, false);
	}
}

void ATSIOManager::SIOInterface::SendNAK() {
	if (!IsCommandActive())
		return;

	if (mParent.mpAccelRequest) {
		Step& step = mStepQueue.push_back();
		step.mType = kStepType_AccelSendNAK;
	} else {
		SendData("N", 1, false);
	}

	if (g_ATLCSIOReply.IsEnabled())
		g_ATLCSIOReply("Device %02X > NAK\n", mActiveDeviceId);
}

void ATSIOManager::SIOInterface::SendComplete(bool autoDelay) {
	if (!IsCommandActive())
		return;

	// SIO protocol requires minimum 250us delay here.
	if (autoDelay)
		Delay(450);

	if (mParent.mpAccelRequest) {
		Step& step = mStepQueue.push_back();
		step.mType = kStepType_AccelSendComplete;
	} else {
		SendData("C", 1, false);
	}

	if (g_ATLCSIOReply.IsEnabled())
		g_ATLCSIOReply("Device %02X > Complete\n", mActiveDeviceId);
}

void ATSIOManager::SIOInterface::SendError(bool autoDelay) {
	if (!IsCommandActive())
		return;

	// SIO protocol requires minimum 250us delay here.
	if (autoDelay)
		Delay(450);

	if (mParent.mpAccelRequest) {
		Step& step = mStepQueue.push_back();
		step.mType = kStepType_AccelSendError;
	} else {
		SendData("E", 1, false);
	}

	if (g_ATLCSIOReply.IsEnabled())
		g_ATLCSIOReply("Device %02X > Error\n", mActiveDeviceId);
}

void ATSIOManager::SIOInterface::ReceiveData(uint32 id, uint32 len, bool autoProtocol) {
	if (!IsCommandActive())
		return;

	if (autoProtocol)
		++len;

	if (kMaxTransferSize - mTransferBuffer.size() < len) {
		// The transmit buffer is full -- let's see if we can shift it down.
		ShiftTransmitBuffer();

		// check again
		if (kMaxTransferSize - mTransferBuffer.size() < len) {
			VDASSERT(!"No room left in transfer buffer.");
			return;
		}
	}

	mTransferBuffer.resize(mTransferBuffer.size() + len, 0);

	Step& step = mStepQueue.push_back();
	step.mType = autoProtocol ? kStepType_ReceiveAutoProtocol : kStepType_Receive;
	step.mTransferLength = len;
	step.mTransferId = id;

	if (autoProtocol) {
		// SIO protocol requires 850us minimum delay.
		Delay(1530);
		SendACK();
	}

	ExecuteNextStep();
}

void ATSIOManager::SIOInterface::SetTransferRate(uint32 cyclesPerBit, uint32 cyclesPerByte) {
	Step& step = mStepQueue.push_back();
	step.mType = kStepType_SetTransferRate;
	step.mTransferCyclesPerBit = cyclesPerBit;
	step.mTransferCyclesPerByte = cyclesPerByte;

	mCommandQueueCyclesPerByte = cyclesPerByte;
	ExecuteNextStep();
}

void ATSIOManager::SIOInterface::SetSynchronousTransmit(bool enable) {
	if (!IsCommandActive())
		return;

	Step& step = mStepQueue.push_back();
	step.mType = kStepType_SetSynchronousTransmit;
	step.mbEnable = enable;

	ExecuteNextStep();
}

void ATSIOManager::SIOInterface::Delay(uint32 ticks) {
	if (!IsCommandActive())
		return;

	if (!ticks)
		return;

	Step& step = mStepQueue.push_back();
	step.mType = kStepType_Delay;
	step.mDelayTicks = ticks;

	mCommandQueueTime += ticks;

	ExecuteNextStep();
}

void ATSIOManager::SIOInterface::InsertFence(uint32 id) {
	if (!IsCommandActive())
		return;

	Step& step = mStepQueue.push_back();
	step.mType = kStepType_Fence;
	step.mFenceId = id;
}

void ATSIOManager::SIOInterface::FlushQueue() {
	mStepQueue.clear();
}

void ATSIOManager::SIOInterface::EndCommand() {
	if (!IsCommandActive())
		return;

	Step& step = mStepQueue.push_back();
	step.mType = kStepType_EndCommand;
}

void ATSIOManager::SIOInterface::HandleCommand(const void *data, uint32 len, bool succeeded) {
	BeginCommand();
	SendACK();

	if (succeeded)
		SendComplete(true);
	else
		SendError(true);

	if (len)
		SendData(data, len, true);

	EndCommand();
}

void ATSIOManager::SIOInterface::SaveActiveCommandState(IATObjectState **ppState) const {
	if (!IsCommandActive()) {
		*ppState = nullptr;
		return;
	}

	vdrefptr state(new ATSaveStateSioActiveCommand);

	uint32 transferPos = mTransferEnd;
	if (mCurrentStep.mType) {
		ATSaveStateSioCommandStep *savedCurStep = new ATSaveStateSioCommandStep(mCurrentStep);

		state->mpCurrentStep = vdrefptr(savedCurStep);

		switch(mCurrentStep.mType) {
			case kStepType_Send:
			case kStepType_SendAutoProtocol:
				savedCurStep->mTransferData.assign_range(vdspan(mTransferBuffer).subspan(mTransferStart, mCurrentStep.mTransferLength));
				break;

			case kStepType_Receive:
			case kStepType_ReceiveAutoProtocol:
				savedCurStep->mTransferData.assign_range(vdspan(mTransferBuffer).subspan(mTransferStart, mTransferIndex - mTransferStart));
				break;

			default:
				break;
		}
	}

	state->mSteps.reserve(mStepQueue.size());

	for(const Step& step : mStepQueue) {
		ATSaveStateSioCommandStep *savedStep = new ATSaveStateSioCommandStep(step);

		state->mSteps.emplace_back(vdrefptr(savedStep));

		if (step.mType == kStepType_Send || step.mType == kStepType_SendAutoProtocol) {
			savedStep->mTransferData.assign_range(vdspan(mTransferBuffer).subspan(transferPos, step.mTransferLength));

			transferPos += step.mTransferLength;
		}
	}

	state->mDeviceId = mActiveDeviceId;
	state->mTransferIndex = mTransferIndex - mTransferStart;
	state->mbTransferError = mbTransferError;
	state->mTransferCyclesPerByte = mTransferCyclesPerByte;
	state->mTransferCyclesPerBit = mTransferCyclesPerBit;
	state->mbTransmitSynchronous = mbTransmitSynchronous;

	const uint64 t64 = mParent.mpScheduler->GetTick64();
	state->mTransferStartTime = mpTransferEvent ? (sint32)(mTransferStartTime - (uint32)t64) : 0;
	state->mQueueTime = (sint64)(mCommandQueueTime - t64);
	state->mQueueCyclesPerByte = mCommandQueueCyclesPerByte;
	state->mCommandFrameEndTime = (sint64)(mCommandFrameEndTime - t64);
	state->mCommandDeassertTime = (sint64)(mCommandDeassertTime - t64);

	if (mpTransferEvent)
		state->mStepDelay = mParent.mpScheduler->GetTicksToEvent(mpTransferEvent);
	else if (mpDelayEvent)
		state->mStepDelay = mParent.mpScheduler->GetTicksToEvent(mpDelayEvent);

	*ppState = state.release();
}

void ATSIOManager::SIOInterface::LoadActiveCommandState(IATObjectState *state0) {
	if (!state0)
		return;

	try {
		const ATSaveStateSioActiveCommand& state = atser_cast<const ATSaveStateSioActiveCommand&>(*state0);

		CancelCommand();
		ResetTransferParams();

		if (state.mTransferCyclesPerByte)
			mTransferCyclesPerByte = state.mTransferCyclesPerByte;

		if (state.mTransferCyclesPerBit)
			mTransferCyclesPerBit = state.mTransferCyclesPerBit;

		mbCommandActive = true;
		mActiveDeviceId = state.mDeviceId;
		mbTransmitSynchronous = state.mbTransmitSynchronous;
		mTransferStart = 0;
		mTransferEnd = 0;
		mbTransferError = state.mbTransferError;
		mbTransferSend = false;

		const uint64 t64 = mParent.mpScheduler->GetTick64();
		mTransferStartTime = (uint64)state.mTransferStartTime + t64;
		mCommandQueueTime = (uint64)state.mQueueTime + t64;
		mCommandQueueCyclesPerByte = state.mQueueCyclesPerByte;
		mCommandFrameEndTime = (uint64)state.mCommandFrameEndTime + t64;
		mCommandDeassertTime = (uint64)state.mCommandDeassertTime + t64;

		mTransferBuffer.clear();

		if (state.mpCurrentStep) {
			mCurrentStep = state.mpCurrentStep->mStep;

			switch(mCurrentStep.mType) {
				case kStepType_Receive:
				case kStepType_ReceiveAutoProtocol:
					mTransferBuffer.assign_range(state.mpCurrentStep->mTransferData);
					mTransferBuffer.resize(mCurrentStep.mTransferLength, 0);

					mTransferEnd = mCurrentStep.mTransferLength;
					mbTransferSend = false;
					mReceiveTimeoutBase = mParent.mpScheduler->GetTick64();
					mParent.BeginReceive(*this);
					break;

				case kStepType_Send:
				case kStepType_SendAutoProtocol:
					mTransferEnd = mCurrentStep.mTransferLength;
					mbTransferSend = true;

					if (state.mStepDelay)
						mParent.mpScheduler->SetEvent(state.mStepDelay, this, kEventId_Send, mpTransferEvent);

					mTransferBuffer.assign_range(state.mpCurrentStep->mTransferData);

					mParent.BeginSend(*this);
					break;

				case kStepType_Delay:
					if (state.mStepDelay)
						mParent.mpScheduler->SetEvent(state.mStepDelay, this, kEventId_Delay, mpDelayEvent);
					break;

				default:
					break;
			}
		}

		mTransferIndex = state.mTransferIndex;
		
		if (mTransferIndex > mTransferEnd)
			throw ATInvalidSaveStateException();

		for(const auto& step : state.mSteps) {
			if (step)
				mStepQueue.push_back(step->mStep);

			switch(step->mStep.mType) {
				case kStepType_Send:
				case kStepType_SendAutoProtocol:
					mTransferBuffer.append_range(step->mTransferData);
					break;

				case kStepType_Receive:
				case kStepType_ReceiveAutoProtocol:
					mTransferBuffer.resize(mTransferBuffer.size() + step->mStep.mTransferLength, 0);
					break;
			}
		}


		// sanity check transfer parameters
		if (mTransferCyclesPerBit > 100000 || mTransferCyclesPerByte > 1000000)
			throw ATInvalidSaveStateException();

		if (mTransferCyclesPerByte < mTransferCyclesPerBit * 8)
			throw ATInvalidSaveStateException();

		// derived parameter cleanup
		mbActiveDeviceDisk = IsDiskDevice(mActiveDeviceId);
		UpdateTransferRateDerivedValues();

		mParent.mActiveCommandInterfaces.AddUnique(this);

	} catch(...) {
		CancelCommand();
		ResetTransferParams();
		UpdateTransferRateDerivedValues();
		throw;
	}
}

ATSIOManager::SIOInterface::AccelResult ATSIOManager::SIOInterface::TryAccelCommand(const ATDeviceSIORequest& devreq) {
	IATDeviceSIO::CmdResponse response = mDevice.OnSerialAccelCommand(devreq);

	switch(response) {
		case IATDeviceSIO::kCmdResponse_NotHandled:
		default:
			return AccelResult::NotHandled;

		case IATDeviceSIO::kCmdResponse_BypassAccel:
			return AccelResult::BypassAccel;

		case IATDeviceSIO::kCmdResponse_Fail_NAK:
			BeginCommand();
			SendNAK();
			EndCommand();
			break;

		case IATDeviceSIO::kCmdResponse_Send_ACK_Complete:
			BeginCommand();
			SendACK();
			SendComplete(true);
			EndCommand();

		case IATDeviceSIO::kCmdResponse_Start:
			break;
	}

	ExecuteNextStep();
	return AccelResult::Handled;
}

bool ATSIOManager::SIOInterface::TryProcessCommand(const ATDeviceSIOCommand& cmd) {
	// if this device interface can't process new commands while already receiving
	// one, ignore the command
	if (IsCommandActive()) {
		bool cancelled = false;

		if (!mbTransferSend && mTransferIndex < mTransferEnd) {
			const uint64 t = mParent.mpScheduler->GetTick64();

			if (t - mTransferStartTime >= mReceiveFrameTimeout) {
				cancelled = true;
				CancelCommand();
			}

			if (!cancelled && t - mReceiveTimeoutBase >= mReceiveByteTimeout) {
				cancelled = true;
				CancelCommand();
			}
		}

		if (!cancelled && !mbCommandTruncationEnabled)
			return false;
	}

	// if this device interface requires command to be asserted at the end of the
	// command and that's not the case, ignore the command
	if (mbCommandDeassertCheckEnabled && cmd.mbEarlyCmdDeassert)
		return false;

	IATDeviceSIO::CmdResponse response = mDevice.OnSerialBeginCommand(cmd);

	switch(response) {
		case IATDeviceSIO::kCmdResponse_NotHandled:
		default:
			if (mbCommandActive) {
				VDFAIL("Device implementation queued a command and returned NotHandled.");
				CancelCommand();
			}

			return false;

		case IATDeviceSIO::kCmdResponse_BypassAccel:
			VDFAIL("OnSerialBeginCommand() must not return BypassAccel");
			return false;

		case IATDeviceSIO::kCmdResponse_Start:
			return true;

		case IATDeviceSIO::kCmdResponse_Send_ACK_Complete:
			BeginCommand();
			SendACK();
			SendComplete(true);
			EndCommand();
			return true;

		case IATDeviceSIO::kCmdResponse_Fail_NAK:
			BeginCommand();
			SendNAK();
			EndCommand();
			return true;
	}
}

void ATSIOManager::SIOInterface::CancelCommand() {
	if (!IsCommandActive())
		return;

	mActiveDeviceId = 0;

	ResetTransfer();

	mDevice.OnSerialAbortCommand();

	mStepQueue.clear();
	mCurrentStep.mType = kStepType_None;

	mParent.mActiveCommandInterfaces.Remove(this);

	mbCommandActive = false;
}

bool ATSIOManager::SIOInterface::OnReceive(uint8 c, uint32 cyclesPerBit, bool framingError, bool truncated) {
	if (mTransferIndex >= mTransferEnd || mbTransferSend) {
		mParent.EndReceive(*this);
		return false;
	}

	if (mReceiveFrameTimeout != UINT64_MAX) {
		const uint64 t = mParent.mpScheduler->GetTick64();

		if (t - mTransferStartTime >= mReceiveFrameTimeout) {
			CancelCommand();
			return false;
		}
	}

	if (mReceiveByteTimeout != UINT64_MAX) {
		const uint64 t = mParent.mpScheduler->GetTick64();

		if (t - mReceiveTimeoutBase >= mReceiveByteTimeout) {
			CancelCommand();
			return false;
		}

		mReceiveTimeoutBase = t;
	}

	// flag error if baud rate is off
	if (cyclesPerBit < mTransferCyclesPerBitRecvMin || cyclesPerBit > mTransferCyclesPerBitRecvMax)
		mbTransferError = true;

	// We place the byte into the buffer even if a framing error occurs because
	// typically drives do not check the stop bit (810, 1050, XF551, Indus GT).
	mTransferBuffer[mTransferIndex++] = c;

	// check if we just finished the transfer
	if (mTransferIndex >= mTransferEnd) {
		const uint32 transferLen = mTransferEnd - mTransferStart;
		const uint8 *data = mTransferBuffer.data() + mTransferStart;
		const bool checksumOK = !mbTransferError && (!transferLen || ATComputeSIOChecksum(data, transferLen - 1) == data[transferLen - 1]);

		mTransferStart = mTransferEnd;

		// Adjust queue time for the time actually taken during the transfer. Note that this
		// is an offset instead of just resetting the queue time as there can be commands already
		// queued afterward, especially for auto-protocol receives.
		mCommandQueueTime += mParent.mpScheduler->GetTick64() - mTransferStartTime;

		if (mCurrentStep.mType == kStepType_ReceiveAutoProtocol) {
			if (!checksumOK) {
				mStepQueue.clear();
				mCurrentStep.mType = kStepType_None;

				// SIO protocol requires 850us minimum delay.
				Delay(1530);
				SendNAK();
				EndCommand();

				ExecuteNextStep();
				return false;
			}

			mDevice.OnSerialReceiveComplete(mCurrentStep.mTransferId, data, transferLen - 1, true);
		} else {
			mDevice.OnSerialReceiveComplete(mCurrentStep.mTransferId, data, transferLen, checksumOK);
		}

		mParent.EndReceive(*this);

		mCurrentStep.mType = kStepType_None;
		ExecuteNextStep();
	}
	
	return mbActiveDeviceDisk ? mParent.mbDiskBurstTransfersEnabled : mParent.mbBurstTransfersEnabled;
}

bool ATSIOManager::SIOInterface::OnSendReady() {
	if (!mpTransferEvent)
		return false;

	if (!mbTransferSend)
		return false;

	if (mTransferIndex - mTransferStart < 3)
		return true;

	uint32 existingDelay = mParent.mpScheduler->GetTicksToEvent(mpTransferEvent);
	if (existingDelay > 50) {
		// Whenever we shorten a byte delay for burst, we extend the next byte
		// delay by that amount. This means that we need to make sure not to
		// double-add that amount on the next run.
		mAccelTimeSkew -= mTransferLastBurstOffset;
		mTransferBurstOffset = (existingDelay - 50);
		mAccelTimeSkew += mTransferBurstOffset;

		mParent.mpScheduler->SetEvent(50, this, kEventId_Send, mpTransferEvent);
	}

	return true;
}

void ATSIOManager::SIOInterface::ExecuteNextStep() {
	while(!mCurrentStep.mType && !mStepQueue.empty()) {
		mCurrentStep = mStepQueue.front();
		mStepQueue.pop_front();

		switch(mCurrentStep.mType) {
			case kStepType_Send:
			case kStepType_SendAutoProtocol:
				if (!mParent.mpAccelRequest)
					g_ATLCSIOSteps("Sending %u bytes (%02X)\n", mCurrentStep.mTransferLength, mTransferBuffer[mTransferIndex]);

				mbTransferSend = true;
				mTransferStart = mTransferIndex;
				mTransferEnd = mTransferStart + mCurrentStep.mTransferLength;
				mTransferBurstOffset = 0;
				mTransferLastBurstOffset = 0;
				VDASSERT(mTransferEnd <= mTransferBuffer.size());

				if (mParent.mpAccelRequest) {
					if (mParent.mpAccelRequest->mMode & 0x40) {
						const uint32 len = mCurrentStep.mTransferLength + (mCurrentStep.mType == kStepType_SendAutoProtocol ? -1 : 0);
						const uint32 reqLen = mParent.mpAccelRequest->mLength;
						const uint32 minLen = std::min<uint32>(len, reqLen);
						const uint8 *src = mTransferBuffer.data() + mTransferIndex;

						for(uint32 i=0; i<minLen; ++i)
							mParent.mpMemory->WriteByte(mParent.mAccelBufferAddress + i, src[i]);

						mAccelTimeSkew += (minLen + 1) * mTransferCyclesPerByte;

						uint8 checksum = ATComputeSIOChecksum(src, minLen);

						if (len < reqLen) {
							// We sent less data than SIO was expecting. This will cause a timeout.
							*mParent.mpAccelStatus = 0x8A;
						} else if (len > reqLen) {
							// We sent more data than SIO was expecting. This may cause a checksum
							// error.
							if (checksum != src[reqLen])
								*mParent.mpAccelStatus = 0x8F;
						}

						mParent.mpMemory->WriteByte(ATKernelSymbols::CHKSUM, checksum);

						const uint32 endAddr = mParent.mAccelBufferAddress + minLen;
						mParent.mpMemory->WriteByte(ATKernelSymbols::BUFRLO, (uint8)endAddr);
						mParent.mpMemory->WriteByte(ATKernelSymbols::BUFRHI, (uint8)(endAddr >> 8));
					}

					mTransferIndex = mTransferEnd;
					mCurrentStep.mType = kStepType_None;
				} else {
					mParent.BeginSend(*this);
					OnScheduledEvent(kEventId_Send);
				}
				break;

			case kStepType_Receive:
			case kStepType_ReceiveAutoProtocol:
				if (!mParent.mpAccelRequest)
					g_ATLCSIOSteps("Receiving %u bytes\n", mCurrentStep.mTransferLength);

				mbTransferSend = false;
				mbTransferError = false;
				mTransferStart = mTransferIndex;
				mTransferEnd = mTransferStart + mCurrentStep.mTransferLength;

				UpdateTransferRateDerivedValues();

				if (mParent.mpAccelRequest) {
					const uint32 id = mCurrentStep.mTransferId;
					const uint32 len = mCurrentStep.mTransferLength + (mCurrentStep.mType == kStepType_ReceiveAutoProtocol ? -1 : 0);
					const uint32 reqLen = mParent.mpAccelRequest->mMode & 0x80 ? mParent.mpAccelRequest->mLength : 0;
					const uint32 minLen = std::min<uint32>(len, reqLen);

					for(uint32 i=0; i<minLen; ++i)
						mTransferBuffer[mTransferStart + i] = mParent.mpMemory->ReadByte(mParent.mAccelBufferAddress + i);

					mAccelTimeSkew += (minLen + 1) * (mTransferCyclesPerBit * 10);

					if (reqLen < len) {
						// SIO provided less data than we were expecting. Hmm. On an 810,
						// this will hang the drive indefinitely until the expected bytes
						// are provided. For now, we report a broken checksum to the device.
						memset(mTransferBuffer.data() + mTransferStart + minLen, 0, len - minLen);

						mDevice.OnSerialReceiveComplete(id, mTransferBuffer.data() + mTransferStart, len, false);
					} else if (reqLen > len) {
						// SIO provided more data than we were expecting. This may cause
						// a checksum error.
						const uint8 checksum = mParent.mpMemory->ReadByte(mParent.mAccelBufferAddress + minLen);

						mDevice.OnSerialReceiveComplete(id, mTransferBuffer.data() + mTransferStart, len, checksum == ATComputeSIOChecksum(mTransferBuffer.data() + mTransferStart, minLen));
					} else {
						mDevice.OnSerialReceiveComplete(id, mTransferBuffer.data() + mTransferStart, len, true);
					}

					mTransferIndex = mTransferEnd;
					mCurrentStep.mType = kStepType_None;
				} else {
					mParent.BeginReceive(*this);
					mReceiveTimeoutBase = mParent.mpScheduler->GetTick64();
					mTransferStartTime = mReceiveTimeoutBase;
				}
				break;

			case kStepType_SetTransferRate:
				mTransferCyclesPerBit = mCurrentStep.mTransferCyclesPerBit;
				mTransferCyclesPerByte = mCurrentStep.mTransferCyclesPerByte;
				mCurrentStep.mType = kStepType_None;
				break;

			case kStepType_SetSynchronousTransmit:
				mbTransmitSynchronous = mCurrentStep.mbEnable;
				mCurrentStep.mType = kStepType_None;
				break;

			case kStepType_Delay:
				VDASSERT(mCurrentStep.mDelayTicks);

				if (mParent.mpAccelRequest) {
					mAccelTimeSkew += mCurrentStep.mDelayTicks;
					mCurrentStep.mType = kStepType_None;
				} else {
					g_ATLCSIOSteps("Delaying for %u ticks\n", mCurrentStep.mDelayTicks);
					mParent.mpScheduler->SetEvent(mCurrentStep.mDelayTicks, this, kEventId_Delay, mpDelayEvent);
				}
				break;

			case kStepType_Fence:
				mCurrentStep.mType = kStepType_None;
				mDevice.OnSerialFence(mCurrentStep.mFenceId);
				break;

			case kStepType_EndCommand:
				if (!mParent.mpAccelRequest)
					g_ATLCSIOSteps <<= "Ending command\n";

				mbCommandActive = false;
				mCurrentStep.mType = kStepType_None;

				mStepQueue.clear();
				mTransferBuffer.clear();

				mParent.mCachedTransferBuffer.swap(mTransferBuffer);
				mParent.mCachedStepQueue.swap(mStepQueue);
				mParent.mActiveCommandInterfaces.Remove(this);
				return;

			case kStepType_AccelSendACK:
			case kStepType_AccelSendComplete:
				mAccelTimeSkew += mTransferCyclesPerByte;
				mCurrentStep.mType = kStepType_None;
				break;

			case kStepType_AccelSendNAK:
				*mParent.mpAccelStatus = 0x8B;		// NAK error
				mCurrentStep.mType = kStepType_None;
				mAccelTimeSkew += mTransferCyclesPerByte;
				break;

			case kStepType_AccelSendError:
				*mParent.mpAccelStatus = 0x90;		// Device error
				mCurrentStep.mType = kStepType_None;
				mAccelTimeSkew += mTransferCyclesPerByte;
				break;

			default:
				VDFAIL("Unknown step in step queue.");
				break;
		}
	}
}

void ATSIOManager::SIOInterface::ResetTransfer() {
	mTransferStart = 0;
	mTransferIndex = 0;
	mTransferEnd = 0;
	mbTransferSend = false;

	mParent.mpScheduler->UnsetEvent(mpDelayEvent);
	mParent.mpScheduler->UnsetEvent(mpTransferEvent);

	mParent.EndReceive(*this);
	mParent.EndSend(*this);
}

void ATSIOManager::SIOInterface::ResetTransferParams() {
	mTransferCyclesPerByte = 932;
	mTransferCyclesPerBit = 93;
	mbTransmitSynchronous = false;

	mCommandQueueCyclesPerByte = mTransferCyclesPerByte;
}

void ATSIOManager::SIOInterface::UpdateTransferRateDerivedValues() {
	mTransferCyclesPerBitRecvMin = mTransferCyclesPerBit - (mTransferCyclesPerBit + 19)/20;
	mTransferCyclesPerBitRecvMax = mTransferCyclesPerBit + (mTransferCyclesPerBit + 19)/20;
}

void ATSIOManager::SIOInterface::ShiftTransmitBuffer() {
	if (mTransferStart > 0) {
		mTransferBuffer.erase(mTransferBuffer.begin(), mTransferBuffer.begin() + mTransferStart);

		mTransferEnd -= mTransferStart;
		mTransferIndex -= mTransferStart;
		mTransferStart = 0;
	}
}

void ATSIOManager::SIOInterface::OnScheduledEvent(uint32 id) {
	switch(id) {
		case kEventId_Delay:
			mpDelayEvent = nullptr;
			mCurrentStep.mType = kStepType_None;
			ExecuteNextStep();
			break;

		case kEventId_Send:
			mpTransferEvent = nullptr;
			if (mTransferIndex < mTransferEnd) {
				VDASSERT(mParent.mSendingInterfaces.Contains(this));
				uint8 c = mTransferBuffer[mTransferIndex++];

				mParent.mpPokey->ReceiveSIOByte(c, mTransferCyclesPerBit, true, mbActiveDeviceDisk ? mParent.mbDiskBurstTransfersEnabled : mParent.mbBurstTransfersEnabled, false, false);
				mParent.TraceReceive(c, mTransferCyclesPerBit, false);

				mParent.mpScheduler->SetEvent(mTransferCyclesPerByte + mTransferBurstOffset, this, kEventId_Send, mpTransferEvent);
				mTransferLastBurstOffset = mTransferBurstOffset;
				mTransferBurstOffset = 0;
			} else {
				mTransferStart = mTransferEnd;
				mCurrentStep.mType = kStepType_None;

				mParent.EndSend(*this);

				ExecuteNextStep();
			}
			break;
	}
}

///////////////////////////////////////////////////////////////////////////

ATSIOManager::ATSIOManager() {
}

ATSIOManager::~ATSIOManager() {
}

void ATSIOManager::SetSIOPatchEnabled(bool enable) {
	if (mbSIOPatchEnabled == enable)
		return;

	mbSIOPatchEnabled = enable;

	if (mpCPU)
		ReinitHooks();
}

void ATSIOManager::SetDiskSIOAccelEnabled(bool enabled) {
	if (mbDiskSIOAccelEnabled == enabled)
		return;

	mbDiskSIOAccelEnabled = enabled;

	if (mpCPU)
		ReinitHooks();
}

void ATSIOManager::SetOtherSIOAccelEnabled(bool enabled) {
	if (mbOtherSIOAccelEnabled == enabled)
		return;

	mbOtherSIOAccelEnabled = enabled;

	if (mpCPU)
		ReinitHooks();
}

void ATSIOManager::SetDebuggerDeviceId(uint8 id) {
	if (mDebuggerDeviceId != id) {
		mDebuggerDeviceId = id;

		if (mpCPU)
			ReinitHooks();
	}
}

void ATSIOManager::Init(ATCPUEmulator *cpu, ATSimulator *sim) {
	mpCPU = cpu;
	mpMemory = cpu->GetMemory();
	mpSim = sim;
	mpUIRenderer = sim->GetUIRenderer();
	mpScheduler = sim->GetScheduler();
	mpPokey = &mpSim->GetPokey();
	mpPIA = &mpSim->GetPIA();
	mPIAOutput = mpPIA->AllocOutput(
		[](void *p, uint32 state) {
			((ATSIOManager *)p)->OnMotorStateChanged((state & kATPIAOutput_CA2) == 0);
		},
		this,
		kATPIAOutput_CA2);

	mpPokey->AddSIODevice(this);

	ReinitHooks();
}

void ATSIOManager::Shutdown() {
	UninitHooks();

	mSIOInterfaces.NotifyAllDirect(
		[](SIOInterface *iface) {
			iface->Shutdown();
		}
	);

	if (mpPokey) {
		mpPokey->SetNotifyOnBiClockChange(nullptr);
		mpPokey->RemoveSIODevice(this);
		mpPokey = nullptr;
	}

	if (mpPIA) {
		mpPIA->FreeOutput(mPIAOutput);
		mpPIA = nullptr;
	}

	mpScheduler = nullptr;
	mpUIRenderer = NULL;
	mpSim = NULL;
	mpMemory = NULL;
	mpCPU = NULL;
}

void ATSIOManager::ColdReset() {
	mbLoadingState = false;

	CancelAllCommands();

	mCommandBufferIndex = 0;
	PokeyEndCommand();

	mbMotorState = false;
	mpPendingInterface = nullptr;
	mPendingDeviceId = 0;

	WarmReset();
}

void ATSIOManager::WarmReset() {
	mAccessedDisks = 0;
}

vdrefptr<IATObjectState> ATSIOManager::SaveState() const {
	if (!mbCommandState && (mCommandBufferIndex == 0 || mCommandBufferIndex == 5))
		return nullptr;

	vdrefptr state(new ATSaveStateSioManager);

	state->mCommandCyclesPerBit = mCommandCyclesPerBit;
	state->mCommandBufferIndex = mCommandBufferIndex;
	vdcopy_checked_r(state->mCommandBuffer, mCommandBuffer);

	return state;
}

void ATSIOManager::ReinitHooks() {
	UninitHooks();

	ATCPUHookManager& hookmgr = *mpCPU->GetHookManager();

	if (mbSIOPatchEnabled || mDebuggerDeviceId) {
		if (mbOtherSIOAccelEnabled || mbDiskSIOAccelEnabled || mpSim->IsCassetteSIOPatchEnabled() || mpSim->IsFastBootEnabled() || mDebuggerDeviceId)
			hookmgr.SetHookMethod(mpSIOVHook, kATCPUHookMode_KernelROMOnly, ATKernelSymbols::SIOV, 0, this, &ATSIOManager::OnHookSIOV);
	}
}

void ATSIOManager::UninitHooks() {
	if (mpCPU) {
		ATCPUHookManager& hookmgr = *mpCPU->GetHookManager();

		hookmgr.UnsetHook(mpSIOVHook);
	}
}

void ATSIOManager::SetTraceContext(ATTraceContext *context) {
	mpTraceContext = context;

	if (context) {
		ATTraceCollection *coll = context->mpCollection;
		ATTraceGroup *group = coll->AddGroup(L"SIO Bus");
		const double invCycleRate = mpScheduler->GetRate().AsInverseDouble();

		mpTraceChannelBusCommand = group->AddSimpleChannel(context->mBaseTime, invCycleRate, L"Command");
		mpTraceChannelBusSend = group->AddFormattedChannel(context->mBaseTime, invCycleRate, L"Send");
		mpTraceChannelBusReceive = group->AddFormattedChannel(context->mBaseTime, invCycleRate, L"Receive");
		mTraceCommandStartTime = mpScheduler->GetTick64();
	} else {
		mpTraceChannelBusCommand = nullptr;
		mpTraceChannelBusSend = nullptr;
		mpTraceChannelBusReceive = nullptr;
	}
}

void ATSIOManager::TryAccelPBIRequest(bool enabled) {
	if (enabled && OnHookSIOV(0))
		mpCPU->SetP(mpCPU->GetP() | AT6502::kFlagC);
	else
		mpCPU->SetP(mpCPU->GetP() & ~AT6502::kFlagC);
}

bool ATSIOManager::TryAccelRequest(const ATSIORequest& req, bool pbi) {
	g_ATLCHookSIOReqs("Checking SIOV request: Device $%02X | Command $%02X | Mode $%02X | Address $%04X | Length $%04X | AUX $%02X%02X\n"
		, req.mDevice
		, req.mCommand
		, req.mMode
		, req.mAddress
		, req.mLength
		, req.mAUX[1]
		, req.mAUX[0]
		);

	// Check if we already have a command in progress. If so, bail.
	if (!mActiveCommandInterfaces.IsEmpty() || mbCommandState)
		return false;

	// Abort read acceleration if the buffer overlaps the parameter region.
	// Yes, there is game stupid enough to do this (Formula 1 Racing). Specifically,
	// we need to check if TIMFLG is in the buffer as that will cause the read to
	// prematurely abort.
	if (req.mAddress <= ATKernelSymbols::TIMFLG && (ATKernelSymbols::TIMFLG - req.mAddress) < req.mLength)
		return false;

	// Bypass acceleration if the Break key has been pressed.
	ATKernelDatabase kdb(mpMemory);
	if (kdb.BRKKEY == 0)
		return false;

	// Check if the I flag is set -- if so, bail. This will hang in SIO
	// if not intercepted by a PBI device.
	if (mpCPU->GetP() & AT6502::kFlagI)
		return false;

	// Check if both read and write mode bits are set. This is not well defined.
	if ((req.mMode & 0xC0) == 0xC0)
		return false;

	uint8 status = 0x01;
	
	SetPendingDeviceId(req.mDevice);

	bool allowAccel = mbOtherSIOAccelEnabled || (mDebuggerDeviceId && mDebuggerDeviceId == req.mDevice);

	if (mbPendingDeviceDisk) {
		allowAccel = mbDiskSIOAccelEnabled;

		if (allowAccel && mpSim->IsDiskSIOOverrideDetectEnabled() && !(mAccessedDisks & (1 << (req.mDevice - 0x31))))
			return false;
	}
	
	while(allowAccel) {
		// Convert the request to a device request.
		ATDeviceSIORequest devreq = {};
		devreq.mDevice = req.mDevice;
		devreq.mCommand = req.mCommand;
		devreq.mAUX[0] = req.mAUX[0];
		devreq.mAUX[1] = req.mAUX[1];
		devreq.mCyclesPerBit = 93;
		devreq.mbStandardRate = true;
		devreq.mbEarlyCmdDeassert = false;
		devreq.mPollCount = mPollCount;
		devreq.mMode = req.mMode;
		devreq.mTimeout = req.mTimeout;
		devreq.mLength = req.mLength;
		devreq.mSector = req.mSector;
		
		mCommandFrameEndTime = mCommandDeassertTime = mpScheduler->GetTick64();

		// Run down the device chain and see if anyone is interested in this request.
		SIOInterface::AccelResult response = SIOInterface::AccelResult::NotHandled;

		mSIOInterfaces.NotifyWhileDirect(
			[&, this](SIOInterface *iface) -> bool {
				mpAccelRequest = &devreq;
				mpAccelStatus = &status;
				mAccelBufferAddress = req.mAddress;

				mpPendingInterface = iface;

				response = iface->TryAccelCommand(devreq);

				mpPendingInterface = nullptr;
				mPendingDeviceId = 0;
				mpAccelRequest = nullptr;
				mpAccelStatus = nullptr;

				return response == SIOInterface::AccelResult::NotHandled;
			}
		);

		// check if we were specifically asked not to accelerate this request
		if (response == SIOInterface::AccelResult::BypassAccel)
			return false;

		// check if the device handled it
		if (response == SIOInterface::AccelResult::Handled)
			goto handled;

		// Check if the command is a type 3 poll command and we have fast boot enabled.
		// If so, keep looping until we hit 26 retries. This gives accelerated devices
		// a chance to intercept fast boot.
		if (mpSim->IsFastBootEnabled()) {
			// type 3 poll (??/40/00/00)
			if (devreq.mCommand == 0x40 && devreq.mAUX[0] == 0x00 && devreq.mAUX[1] == 0x00) {
				++mPollCount;

				if (mPollCount <= 26)
					continue;
			}
		}

		break;
	}

	// Check hard-coded devices.
	if (req.mDevice >= 0x31 && req.mDevice <= 0x3F) {
		const uint32 diskIndex = req.mDevice - 0x31;
		ATDiskEmulator& disk = mpSim->GetDiskDrive(diskIndex);

		if (!disk.IsEnabled() && mpSim->GetDiskInterface(diskIndex).GetClientCount() < 2 && mpSim->IsFastBootEnabled())
			goto fastbootignore;

		return false;
	} else if (req.mDevice == 0x4F) {
		if (!mpSim->IsFastBootEnabled())
			return false;

fastbootignore:
		// return timeout
		status = 0x8A;
	} else if (req.mDevice == 0x5F) {
		if (!mpSim->IsCassetteSIOPatchEnabled())
			return false;

		ATCassetteEmulator& cassette = mpSim->GetCassette();

		// Check if a read or write is requested.
		// 
		// Atomia issues a read with DSTATS=$01, so this needs to only check bit 7 -- cassette ops
		// don't support requests without a data frame.

		if (!(req.mMode & 0x80)) {
			// DTIMOT=0 effectively disables the timeout. This is relied on by Bang! Bank!, which has a block exceeding four
			// minutes.
			status = cassette.ReadBlock(req.mAddress, req.mLength, mpMemory, !req.mTimeout ? -1.0f : (float)((uint32)req.mTimeout * 64 * (double)mCyclesPerFrame * mpScheduler->GetRate().AsInverseDouble()));

			mpUIRenderer->PulseStatusFlags(1 << 16);

			g_ATLCHookSIO("Intercepted cassette SIO read: buf=%04X, len=%04X, status=%02X\n", req.mAddress, req.mLength, status);
		} else {
			status = cassette.WriteBlock(req.mAddress, req.mLength, mpMemory);

			mpUIRenderer->PulseStatusFlags(1 << 16);

			g_ATLCHookSIO("Intercepted cassette SIO write: buf=%04X, len=%04X, status=%02X\n", req.mAddress, req.mLength, status);
		}
	} else {
		return false;
	}

handled:
	UpdatePollState(req.mCommand, req.mAUX[0], req.mAUX[1]);

	// If this is anything other than a cassette request, reset timers 3+4 to 19200 baud and set
	// asynchronous receive mode. Wayout needs this to not play garbage on channels 3+4 on the
	// title screen.
	if (req.mDevice != 0x5F) {
		kdb.AUDF3 = 0x28;
		kdb.AUDF4 = 0;
		kdb.SKCTL = 0x13;
		kdb.SSKCTL = 0x13;
	}

	ATClearPokeyTimersOnDiskIo(kdb);

	// Set CDTMA1 to dummy address (KnownRTS) if it is not already set -- SIO is documented as setting
	// this on every call. This is required by Ankh, which uses OS timer 1 for a delay but doesn't
	// bother setting up CDTMV1.
	if (kdb.CDTMA1 == 0)
		kdb.CDTMA1 = 0xE4C0;

	// Set checksum sent flag. This is relied on by Apple Panic, which blindly turns on the serial
	// output complete interrupt after reading sector 404. If this isn't done, the SEROC handler
	// doesn't clear the interrupt and the CPU gets stuck in an IRQ loop.
	kdb.CHKSNT = 0xFF;

	// Clear CRITIC. We must only do this for SIOV hooks; for PBI hooks we are being called
	// from either SIO or SDX, and in the latter case clearing CRITIC causes an underflow
	// later on when SDX decrements it.
	if (!pbi)
		kdb.CRITIC = 0;

	kdb.STATUS = status;
	kdb.DSTATS = status;

	// Set or clear TIMFLG depending on whether there was a timeout. Some Polish tape versions of
	// Boulder Dash require this due to a load stage that relies on TIMFLG from the last successful
	// read. Per the OS Manual, the flag is set to $01 initially and then cleared by the VBI
	// on a timeout.
	kdb.TIMFLG = (status == kATCIOStat_Timeout) ? 0 : 1;
	
	// Set carry depending on last status. Micropainter depends on the state of the carry flag
	// after issuing a call to DSKINV, which in turn leaves the carry flag set from the call to
	// SIOV. The carry is set by two compares within SIO on the status, first to $01 (success)
	// and then to $8A (timeout).
	uint8 carry = AT6502::kFlagC;

	if (status != 0x01 && status < 0x8A)
		carry = 0;

	mpCPU->SetP((mpCPU->GetP() & ~AT6502::kFlagC) + carry);

	// Set A=0. SIOV sets this on the way out as part of clearing CRITIC.
	mpCPU->SetA(0);

	// Set X to typical return value for determinism.
	mpCPU->SetX(0xFE);

	mpCPU->Ldy(status);

	return true;
}

void ATSIOManager::PokeyAttachDevice(ATPokeyEmulator *pokey) {
}

void ATSIOManager::PokeyBeginWriteSIO(uint8 c, bool command, uint32 cyclesPerBit) {
	RawDeviceListLock lock(this);

	for(auto *rawdev : mSIORawDevices) {
		if (rawdev)
			rawdev->OnBeginReceiveByte(c, command, cyclesPerBit);
	}
}

bool ATSIOManager::PokeyWriteSIO(uint8 c, bool command, uint32 cyclesPerBit, uint64 startTime, bool framingError, bool truncated) {
	// drop all cyclesPerBit = 0, which are essentially truncated bytes
	if (!cyclesPerBit)
		return false;

	if (mpTraceContext) {
		const uint64 t = mpScheduler->GetTick64();

		mpTraceChannelBusSend->TruncateLastEvent(startTime);
		mpTraceChannelBusSend->AddTickEvent(startTime, t,
			[ch = (uint8)c](VDStringW& s) {
				s.sprintf(L"%02X", ch);
			},
			kATTraceColor_IO_Write
		);
	}

	if ((mbCommandState || mCommandBufferIndex > 0) && mCommandBufferIndex < 5) {
		mCommandCyclesPerBit = cyclesPerBit;

		mCommandBuffer[mCommandBufferIndex] = c;

		if (++mCommandBufferIndex >= 5) {
			mCommandFrameEndTime = mpScheduler->GetTick64();
			if (!mbCommandState)
				ProcessCommandFrame();
		}
	}

	bool burst = false;

	mReceivingInterfaces.NotifyAllDirect(
		[=, this, &burst](SIOInterface *iface) {
			if (iface->OnReceive(c, cyclesPerBit, framingError, truncated))
				burst = true;
		}
	);

	{
		RawDeviceListLock lock(this);

		for(auto *rawdev : mSIORawDevices) {
			if (rawdev) {
				rawdev->OnReceiveByte(c, command, cyclesPerBit);

				if (truncated)
					rawdev->OnTruncateByte();
			}
		}
	}

	return burst;
}

void ATSIOManager::PokeyBeginCommand() {
	if (!mbLoadingState)
		mCommandBufferIndex = 0;
	
	if (mpTraceChannelBusCommand)
		mTraceCommandStartTime = mpScheduler->GetTick64();
	
	if (!mbCommandState) {
		mbCommandState = true;

		RawDeviceListLock lock(this);
		for(auto *rawdev : mSIORawDevices) {
			if (rawdev)
				rawdev->OnCommandStateChanged(mbCommandState);
		}
	}
}

void ATSIOManager::PokeyEndCommand() {
	if (!mbCommandState)
		return;
	
	mbCommandState = false;

	if (mbLoadingState)
		return;

	const uint64 t = mpScheduler->GetTick64();
	mCommandDeassertTime = t;

	if (mpTraceChannelBusCommand)
		mpTraceChannelBusCommand->AddTickEvent(mTraceCommandStartTime, t, L"", 0xFFC02020);

	if (mCommandBufferIndex >= 5)
		ProcessCommandFrame();

	{
		RawDeviceListLock lock(this);

		for(auto *rawdev : mSIORawDevices) {
			if (rawdev)
				rawdev->OnCommandStateChanged(mbCommandState);
		}
	}
}

void ATSIOManager::PokeySerInReady() {
	{
		RawDeviceListLock lock(this);

		for(auto *rawdev : mSIORawDevices) {
			if (rawdev)
				rawdev->OnSendReady();
		}
	}

	mSendingInterfaces.NotifyAllDirect(
		[this](SIOInterface *iface) {
			if (!iface->OnSendReady())
				mSendingInterfaces.Remove(iface);
		}
	);
}

void ATSIOManager::PokeySetBreak(bool enabled) {
	{
		RawDeviceListLock lock(this);

		for(auto *rawdev : mSIORawDevices) {
			if (rawdev)
				rawdev->OnBreakStateChanged(enabled);
		}
	}
}

vdrefptr<IATDeviceSIOInterface> ATSIOManager::AddDevice(IATDeviceSIO *dev) {
	vdrefptr p(new SIOInterface(*this, *dev));

	mSIOInterfaces.Add(p);

	return p;
}

void ATSIOManager::BeginCommand() {
	if (!mpPendingInterface)
		return;

	if (mbPendingDeviceDisk)
		mAccessedDisks |= (1 << (mPendingDeviceId - 0x31));

	if (mpAccelRequest) {
		g_ATLCSIOAccel("Accelerating device %02X cmd %02X aux %02X%02X (%04X) buffer %04X length %04X\n"
			, mpAccelRequest->mDevice
			, mpAccelRequest->mCommand
			, mpAccelRequest->mAUX[0]
			, mpAccelRequest->mAUX[1]
			, mpAccelRequest->mSector
			, mAccelBufferAddress
			, mpAccelRequest->mLength);
	}
}

uint32 ATSIOManager::GetCyclesPerBitRecv() const {
	return mpPokey->GetSerialCyclesPerBitRecv();
}

uint32 ATSIOManager::GetRecvResetCounter() const {
	return mpPokey->GetSerialInputResetCounter();
}

uint32 ATSIOManager::GetCyclesPerBitSend() const {
	return mpPokey->GetSerialCyclesPerBitSend();
}

uint32 ATSIOManager::GetCyclesPerBitBiClock() const {
	// Double period because we need a full cycle hi+lo per bit.
	return mpPokey->GetSerialBidirectionalClockPeriod() * 2;
}

void ATSIOManager::SetFrameTime(uint32 cyclesPerFrame) {
	mCyclesPerFrame = cyclesPerFrame;
}

void ATSIOManager::SetReadyState(bool ready) {
	if (mbReadyState != ready) {
		mbReadyState = ready;

		RawDeviceListLock lock(this);

		for(auto *rawdev : mSIORawDevices) {
			if (rawdev)
				rawdev->OnReadyStateChanged(mbReadyState);
		}
	}
}

void ATSIOManager::PreLoadState() {
	CancelAllCommands();

	mbLoadingState = true;
	mCommandBufferIndex = 0;
}

void ATSIOManager::LoadState(IATObjectState *obj) {
	if (obj) {
		const ATSaveStateSioManager& state = atser_cast<const ATSaveStateSioManager&>(*obj);

		mCommandBufferIndex = std::min<uint8>(5, state.mCommandBufferIndex);
		mCommandCyclesPerBit = state.mCommandCyclesPerBit;

		vdcopy_checked_r(mCommandBuffer, state.mCommandBuffer);
	}
}

void ATSIOManager::PostLoadState() {
	mbLoadingState = false;
}

void ATSIOManager::AddRawDevice(IATDeviceRawSIO *dev) {
	if (std::find(mSIORawDevices.begin(), mSIORawDevices.end(), dev) != mSIORawDevices.end())
		return;

	if (std::find(mSIORawDevicesNew.begin(), mSIORawDevicesNew.end(), dev) != mSIORawDevicesNew.end())
		return;

	if (mSIORawDevicesBusy)
		mSIORawDevicesNew.push_back(dev);
	else
		mSIORawDevices.push_back(dev);
}

void ATSIOManager::RemoveRawDevice(IATDeviceRawSIO *dev) {
	SetSIOInterrupt(dev, false);
	SetSIOProceed(dev, false);
	SetBiClockNotifyEnabled(dev, false);

	SetExternalClock(dev, 0, 0);

	auto it = std::find(mSIORawDevicesNew.begin(), mSIORawDevicesNew.end(), dev);
	if (it != mSIORawDevicesNew.end()) {
		mSIORawDevicesNew.erase(it);
		return;
	}

	auto it2 = std::find(mSIORawDevices.begin(), mSIORawDevices.end(), dev);
	if (it2 != mSIORawDevices.end()) {
		if (mSIORawDevicesBusy) {
			if (!(mSIORawDevicesBusy & 1))
				--mSIORawDevicesBusy;

			*it2 = nullptr;
		} else
			mSIORawDevices.erase(it2);
	}
}

void ATSIOManager::SendRawByte(uint8 byte, uint32 cyclesPerBit, bool synchronous, bool forceFramingError, bool simulateInput) {
	mpPokey->ReceiveSIOByte(byte, cyclesPerBit, simulateInput, false, synchronous, forceFramingError);

	TraceReceive(byte, cyclesPerBit, !simulateInput);
}

void ATSIOManager::SetRawInput(bool input) {
	mpPokey->SetDataLine(input);
}

bool ATSIOManager::IsSIOCommandAsserted() const {
	return mbCommandState;
}

bool ATSIOManager::IsSIOMotorAsserted() const {
	return mbMotorState;
}

bool ATSIOManager::IsSIOReadyAsserted() const {
	return mbReadyState;
}

bool ATSIOManager::IsSIOForceBreakAsserted() const {
	return mpPokey->IsSerialForceBreakEnabled();
}

void ATSIOManager::SetSIOInterrupt(IATDeviceRawSIO *dev, bool state) {
	auto it = std::lower_bound(mSIOInterruptActive.begin(), mSIOInterruptActive.end(), dev);

	if (state) {
		if (it == mSIOInterruptActive.end() || *it != dev) {
			if (mSIOInterruptActive.empty())
				mpPIA->SetCB1(false);

			mSIOInterruptActive.insert(it, dev);
		}
	} else {
		if (it != mSIOInterruptActive.end()) {
			mSIOInterruptActive.erase(it);

			if (mSIOInterruptActive.empty())
				mpPIA->SetCB1(true);
		}
	}
}

void ATSIOManager::SetSIOProceed(IATDeviceRawSIO *dev, bool state) {
	auto it = std::lower_bound(mSIOProceedActive.begin(), mSIOProceedActive.end(), dev);

	if (state) {
		if (it == mSIOProceedActive.end() || *it != dev) {
			if (mSIOProceedActive.empty())
				mpPIA->SetCA1(false);

			mSIOProceedActive.insert(it, dev);
		}
	} else {
		if (it != mSIOProceedActive.end()) {
			mSIOProceedActive.erase(it);

			if (mSIOProceedActive.empty())
				mpPIA->SetCA1(true);
		}
	}
}

void ATSIOManager::SetBiClockNotifyEnabled(IATDeviceRawSIO *dev, bool enabled) {
	if (!dev)
		return;

	auto it = std::lower_bound(mNotifyBiClockDevices.begin(), mNotifyBiClockDevices.end(), dev);

	if (enabled) {
		if (it != mNotifyBiClockDevices.end() && *it == dev)
			return;

		if (mNotifyBiClockDevices.empty()) {
			mpPokey->SetNotifyOnBiClockChange(
				[this] { OnSerialOutputClockChanged(); }
			);
		}

		mNotifyBiClockDevices.insert(it, dev);
	} else {
		if (it == mNotifyBiClockDevices.end() || *it != dev)
			return;

		mNotifyBiClockDevices.erase(it);

		if (mNotifyBiClockDevices.empty())
			mpPokey->SetNotifyOnBiClockChange(nullptr);
	}
}

void ATSIOManager::SetExternalClock(IATDeviceRawSIO *dev, uint32 initialOffset, uint32 period) {
	bool updatePOKEY = false;

	VDASSERT(!period || std::find(mSIORawDevices.begin(), mSIORawDevices.end(), dev) != mSIORawDevices.end()
		|| std::find(mSIORawDevicesNew.begin(), mSIORawDevicesNew.end(), dev) != mSIORawDevicesNew.end());

	auto it = std::find_if(mExternalClocks.begin(), mExternalClocks.end(),
		[=](const ExternalClock& x) { return x.mpDevice == dev; });

	if (period) {
		uint32 timeBase = initialOffset + ATSCHEDULER_GETTIME(mpScheduler);

		if (it != mExternalClocks.end()) {
			if (it->mPeriod == period && it->mTimeBase == timeBase)
				return;

			mExternalClocks.erase(it);

			if (it == mExternalClocks.begin())
				updatePOKEY = true;
		}

		const ExternalClock newEntry { dev, timeBase, period };
		auto it2 = std::lower_bound(mExternalClocks.begin(), mExternalClocks.end(), newEntry,
			[](const ExternalClock& x, const ExternalClock& y) {
				return x.mPeriod < y.mPeriod;
			}
		);

		if (it2 == mExternalClocks.begin())
			updatePOKEY = true;

		mExternalClocks.insert(it2, newEntry);
	} else {
		if (it != mExternalClocks.end()) {
			if (it == mExternalClocks.begin())
				updatePOKEY = true;

			mExternalClocks.erase(it);
		}
	}

	if (updatePOKEY && mpPokey) {
		if (mExternalClocks.empty())
			mpPokey->SetExternalSerialClock(0, 0);
		else {
			auto clk = mExternalClocks.front();

			mpPokey->SetExternalSerialClock(clk.mTimeBase, clk.mPeriod);
		}
	}
}

void ATSIOManager::RemoveInterface(SIOInterface& iface) {
	if (iface.IsCommandActive())
		iface.CancelCommand();

	mSIOInterfaces.Remove(&iface);

	EndReceive(iface);
	EndSend(iface);

	if (mpPendingInterface == &iface)
		mpPendingInterface = nullptr;
}

void ATSIOManager::BeginReceive(SIOInterface& iface) {
	mReceivingInterfaces.AddUnique(&iface);
}

void ATSIOManager::EndReceive(SIOInterface& iface) {
	mReceivingInterfaces.Remove(&iface);
}

void ATSIOManager::BeginSend(SIOInterface& iface) {
	mSendingInterfaces.AddUnique(&iface);
}

void ATSIOManager::EndSend(SIOInterface& iface) {
	mSendingInterfaces.Remove(&iface);
}

uint8 ATSIOManager::OnHookSIOV(uint16 pc) {
	ATKernelDatabase kdb(mpMemory);

	// read out SIO block
	uint8 siodata[12];

	for(int i=0; i<12; ++i)
		siodata[i] = mpMemory->ReadByte(ATKernelSymbols::DDEVIC + i);

	// assemble parameter block
	ATSIORequest req;

	req.mDevice		= siodata[0] + siodata[1] - 1;
	req.mCommand	= siodata[2];
	req.mMode		= siodata[3];
	req.mTimeout	= siodata[6];
	req.mAddress	= VDReadUnalignedLEU16(&siodata[4]);
	req.mLength		= VDReadUnalignedLEU16(&siodata[8]);
	req.mSector		= VDReadUnalignedLEU16(&siodata[10]);

	for(int i=0; i<2; ++i)
		req.mAUX[i] = siodata[i + 10];

	return TryAccelRequest(req, pc == 0) ? 0x60 : 0;
}

void ATSIOManager::CancelAllCommands() {
	mActiveCommandInterfaces.NotifyAllDirect(
		[](SIOInterface *iface) {
			iface->CancelCommand();
		}
	);
}

void ATSIOManager::SetPendingDeviceId(uint8 deviceId) {
	mPendingDeviceId = deviceId;
	mbPendingDeviceDisk = IsDiskDevice(deviceId);
}

bool ATSIOManager::IsDiskDevice(uint8 deviceId) {
	return deviceId >= 0x31 && deviceId <= 0x3F;
}

void ATSIOManager::OnMotorStateChanged(bool asserted) {
	if (mbMotorState != asserted) {
		mbMotorState = asserted;

		RawDeviceListLock lock(this);

		for(auto *rawdev : mSIORawDevices) {
			if (rawdev)
				rawdev->OnMotorStateChanged(mbMotorState);
		}
	}
}

void ATSIOManager::OnSerialOutputClockChanged() {
	const uint32 cpb = GetCyclesPerBitBiClock();

	for(IATDeviceRawSIO *dev : mNotifyBiClockDevices)
		dev->OnSerialBiClockChanged(cpb);
}

void ATSIOManager::TraceReceive(uint8 c, uint32 cyclesPerBit, bool postReceive) {
	if (mpTraceContext) {
		const uint64 t = mpScheduler->GetTick64();
		uint64 byteTime = (uint64)cyclesPerBit * 10;
		uint64 tStart = t;
		uint64 tEnd = t;

		// If we are getting a post-receive event, the current time is in the
		// middle of the stop bit and so the byte range is [t-9.5*bit, t+0.5*bit].
		// For a pre-receive event, the current time is the leading edge of
		// the start bit and thus the range is [t, t+10*bit].
		if (postReceive) {
			const uint64 stopOffset = cyclesPerBit >> 1;
			const uint64 startOffset = byteTime - stopOffset;

			tStart = std::max<uint64>(tStart, startOffset) - startOffset;
			tEnd += stopOffset;
		} else
			tEnd += byteTime;

		mpTraceChannelBusReceive->TruncateLastEvent(tStart);
		mpTraceChannelBusReceive->AddTickEvent(tStart, tEnd,
			[c](VDStringW& s) { s.sprintf(L"%02X", c); },
			kATTraceColor_IO_Read
		);
	}
}

void ATSIOManager::UpdatePollState(uint8 cmd, uint8 aux1, uint8 aux2) {
	if (cmd == 0x40 && aux1 == aux2) {
		if (aux1 == 0x00)		// Type 3 poll (??/40/00/00) - increment counter
			++mPollCount;
		else {
			// Any command that is not a type 3 poll command resets the counter. This
			// This includes the null poll (??/40/4E/4E) and poll reset (??/40/4F/4F).
			mPollCount = 0;
		}
	} else {
		// Any command to any dveice that is not a type 3 poll command resets
		// the counter.
		mPollCount = 0;
	}
}

void ATSIOManager::ProcessCommandFrame() {
	if (ATComputeSIOChecksum(mCommandBuffer, 4) != mCommandBuffer[4])
		return;

	ATDeviceSIOCommand cmd = {};
	cmd.mDevice = mCommandBuffer[0];
	cmd.mCommand = mCommandBuffer[1];
	cmd.mAUX[0] = mCommandBuffer[2];
	cmd.mAUX[1] = mCommandBuffer[3];
	cmd.mCyclesPerBit = mCommandCyclesPerBit;
	cmd.mbStandardRate = (mCommandCyclesPerBit >= 91 && mCommandCyclesPerBit <= 98);
	cmd.mbEarlyCmdDeassert = mCommandDeassertTime < mCommandFrameEndTime;
	cmd.mPollCount = mPollCount;

	// Check if this is a type 3 poll command -- we provide assistance for these.
	// Note that we've already recorded the poll count above.
	UpdatePollState(cmd.mCommand, cmd.mAUX[0], cmd.mAUX[1]);

	if (g_ATLCSIOCmd.IsEnabled())
		g_ATLCSIOCmd("Device %02X | Command %02X | %02X %02X (%s)%s\n", cmd.mDevice, cmd.mCommand, cmd.mAUX[0], cmd.mAUX[1], ATDecodeSIOCommand(cmd.mDevice, cmd.mCommand, cmd.mAUX), cmd.mbStandardRate ? "" : " (high-speed command frame)");

	SetPendingDeviceId(cmd.mDevice);

	mSIOInterfaces.NotifyWhileDirect(
		[&](SIOInterface *iface) {
			mpPendingInterface = iface;

			if (iface->TryProcessCommand(cmd))
				return false;

			return true;
		}
	);

	mpPendingInterface = nullptr;
}
