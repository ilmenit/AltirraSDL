//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2020 Avery Lee
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
#include <ranges>
#include <vd2/system/binary.h>
#include <vd2/system/date.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <at/atcore/asyncdispatcher.h>
#include <at/atcore/audiomixer.h>	// IATAudioSoundGroup / IATAudioSampleHandle full type required for vdrefptr<>::~vdrefptr instantiation in ATDeviceCustomSound (customdevicevmtypes.h); Android clang is stricter than Linux GCC about needing the complete type at member dtor instantiation
#include <at/atcore/consoleoutput.h>
#include <at/atcore/deviceport.h>
#include <at/atcore/devicevideo.h>
#include <at/atcore/logging.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/scheduler.h>
#include <at/atcore/timerservice.h>
#include <at/atcore/vfs.h>
#include <at/atio/audioreader.h>
#include <at/atvm/compiler.h>
#include "customdevice.h"
#include "customdevice_win32.h"
#include "inputcontroller.h"
#include "irqcontroller.h"
#include "memorymanager.h"
#include "mmu.h"
#include "simulator.h"
#include "decode_png.h"

extern ATSimulator g_sim;

ATLogChannel g_ATLCCustomDev(true, false, "CUSTOMDEV", "Custom device");

void ATCreateDeviceCustom(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceCustom> p(new ATDeviceCustom);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefCustom = { "custom", "custom", L"Custom", ATCreateDeviceCustom };

/////////////////////////////////////////////////////////////////////////////

ATDeviceCustomDisconnectedBus::ATDeviceCustomDisconnectedBus(IATDeviceParent& parent)
	: mParent(parent)
{
}

ATDeviceCustomDisconnectedBus::~ATDeviceCustomDisconnectedBus() {
	Shutdown();
}

bool ATDeviceCustomDisconnectedBus::HasChildDevices() const {
	return !mChildDevices.empty();
}

void ATDeviceCustomDisconnectedBus::Shutdown() {
	while(!mChildDevices.empty()) {
		IATDevice *dev = mChildDevices.back();
		mChildDevices.pop_back();

		dev->SetParent(nullptr, -1);
		dev->Release();
	}
}

const wchar_t *ATDeviceCustomDisconnectedBus::GetBusName() const {
	return L"Disconnected devices";
}

const char *ATDeviceCustomDisconnectedBus::GetBusTag() const {
	return "disconnected";
}

const char *ATDeviceCustomDisconnectedBus::GetSupportedType(uint32 index) {
	return nullptr;
}

void ATDeviceCustomDisconnectedBus::GetChildDevices(vdfastvector<IATDevice *>& devs) {
	devs.resize(mChildDevices.size());

	std::copy(mChildDevices.begin(), mChildDevices.end(), devs.begin());
}

void ATDeviceCustomDisconnectedBus::GetChildDevicePrefix(uint32 index, VDStringW& s) {
}

void ATDeviceCustomDisconnectedBus::AddChildDevice(IATDevice *dev) {
	if (mBusId < 0)
		return;

	mChildDevices.push_back(dev);
	dev->AddRef();

	dev->SetParent(&mParent, mBusId);
}

void ATDeviceCustomDisconnectedBus::RemoveChildDevice(IATDevice *dev) {
	if (!dev)
		return;

	auto it = std::find(mChildDevices.begin(), mChildDevices.end(), dev);
	if (it == mChildDevices.end())
		return;

	mChildDevices.erase(it);

	dev->SetParent(nullptr, -1);
	dev->Release();
}

/////////////////////////////////////////////////////////////////////////////

ATDeviceCustom::ATDeviceCustom()
	: mDisconnectedBus(*this)
{
	mNetwork.mpParent = this;
	mSIO.mpParent = this;
	mSIOFrameSegment.mbReadOnly = true;
	mSIOFrameSegment.mbSpecial = true;
	mClock.mpParent = this;

	mVMDomain.mpParent = this;
	mVMDomain.mInfiniteLoopHandler = [](const char *functionName) {
		g_ATLCCustomDev("Runaway script function '%s' stopped -- possible infinite loop.\n", functionName);
	};

	mpSleepAbortFn = [this](ATVMThread& thread) {
		AbortThreadSleep(thread);
	};

	mpRawSendAbortFn = [this](ATVMThread& thread) {
		AbortRawSend(thread);
	};
}

void *ATDeviceCustom::AsInterface(uint32 iid) {
	switch(iid) {
		case IATDeviceMemMap::kTypeID: return static_cast<IATDeviceMemMap *>(this);
		case IATDeviceCartridge::kTypeID: return static_cast<IATDeviceCartridge *>(this);
		case IATDeviceIndicators::kTypeID: return static_cast<IATDeviceIndicators *>(this);
		case IATDeviceScheduling::kTypeID: return static_cast<IATDeviceScheduling *>(this);
		case IATDeviceSIO::kTypeID: return static_cast<IATDeviceSIO *>(this);
		case IATDeviceRawSIO::kTypeID: return static_cast<IATDeviceRawSIO *>(this);
		case IATDevicePBIConnection::kTypeID: return static_cast<IATDevicePBIConnection *>(this);
		case IATDeviceParent::kTypeID: return static_cast<IATDeviceParent *>(this);
		case IATDeviceDiagnostics::kTypeID: return static_cast<IATDeviceDiagnostics *>(this);
		default:
			return ATDevice::AsInterface(iid);
	}
}

void ATDeviceCustom::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefCustom;
}

void ATDeviceCustom::GetSettingsBlurb(VDStringW& buf) {
	if (!mDeviceName.empty())
		buf += mDeviceName;
	else
		buf += mConfigPath;
}

void ATDeviceCustom::GetSettings(ATPropertySet& settings) {
	settings.SetString("path", mConfigPath.c_str());
	settings.SetBool("hotreload", mbHotReload);
	settings.SetBool("allowunsafe", mbAllowUnsafe);
}

bool ATDeviceCustom::SetSettings(const ATPropertySet& settings) {
	VDStringW newPath(settings.GetString("path", L""));
	const bool hotReload = settings.GetBool("hotreload");
	const bool allowUnsafe = settings.GetBool("allowunsafe");

	if (mConfigPath != newPath || mbHotReload != hotReload || mbAllowUnsafe != allowUnsafe) {
		mConfigPath = newPath;
		mbHotReload = hotReload;
		mbAllowUnsafe = allowUnsafe;

		if (mbInited)
			ReloadConfig();
	}

	return true;
}

void ATDeviceCustom::Init() {
	const auto hardwareMode = g_sim.GetHardwareMode();
	const bool hasPort34 = kATHardwareModeTraits[hardwareMode].mbHasPort34;

	for(int i=0; i<4; ++i) {
		auto *port = mpControllerPorts[i];

		if (!port)
			continue;

		if (i < 2 || hasPort34)
			port->Enable();
		else
			port->Disable();
	}

	mpAsyncDispatcher = mpDeviceManager->GetService<IATAsyncDispatcher>();
	mpMMU = mpDeviceManager->GetService<ATMMUEmulator>();

	mbInited = true;

	ReloadConfig();

	if (mpNetworkEngine) {
		mpNetworkEngine->WaitForFirstConnectionAttempt();

		TryRestoreNet();
		ExecuteNetRequests(false);
	}
}

void ATDeviceCustom::Shutdown() {
	mbInited = false;

	ShutdownCustomDevice();

	// shutting down the custom device can stuff a bunch of child devices into
	// the disconnected bus, so we must now clean it out too
	mDisconnectedBus.Shutdown();

	mpAsyncDispatcher = nullptr;
	mpIndicators = nullptr;
	mpCartPort = nullptr;
	mpMemMan = nullptr;
	mpMMU = nullptr;
	mpScheduler = nullptr;
	mpSIOMgr = nullptr;
	mpPBIMgr = nullptr;
}

void ATDeviceCustom::ColdReset() {
	if (!mbInitedCustomDevice) {
		uint64 t = mpScheduler->GetTick64();

		if (mLastReloadAttempt != t) {
			mLastReloadAttempt = t;

			ReloadConfig();
		}
	}

	ReinitSegmentData(false);

	ResetCustomDevice();

	if (mpScriptEventColdReset) {
		mVMThread.mThreadVariables[(int)ThreadVarIndex::Timestamp] = ATSCHEDULER_GETTIME(mpScheduler);
		mVMThread.RunVoid(*mpScriptEventColdReset);
	}

	if (mpNetworkEngine) {
		if (mpNetworkEngine->IsConnected())
			SendNetCommand(0, 0, NetworkCommand::ColdReset);
		else {
			uint32 t = ATSCHEDULER_GETTIME(mpScheduler);

			if (mLastConnectionErrorCycle != t) {
				mLastConnectionErrorCycle = t;

				mpIndicators->ReportError(mErrorSourceId, L"No connection to device server. Custom device may not function properly.");
			}
		}
	}
}

void ATDeviceCustom::WarmReset() {	
	if (mpScriptEventWarmReset) {
		mVMThread.mThreadVariables[(int)ThreadVarIndex::Timestamp] = ATSCHEDULER_GETTIME(mpScheduler);
		mVMThread.RunVoid(*mpScriptEventWarmReset);
	}

	if (mpNetworkEngine)
		SendNetCommand(0, 0, NetworkCommand::WarmReset);
}

bool ATDeviceCustom::GetErrorStatus(uint32 idx, VDStringW& error) {
	if (!mLastError.empty()) {
		if (!idx--) {
			error = mLastError;
			return true;
		}
	}

	if (mpNetworkEngine && !mpNetworkEngine->IsConnected()) {
		if (!idx--) {
			error = L"No connection to device server. Custom device may not function properly.";
			return true;
		}
	}

	return false;
}

void ATDeviceCustom::InitMemMap(ATMemoryManager *memmap) {
	mpMemMan = memmap;
}

bool ATDeviceCustom::GetMappedRange(uint32 index, uint32& lo, uint32& hi) const {
	if (index >= mMemoryLayers.size())
		return false;

	const auto& ml = *mMemoryLayers[index];
	lo = ml.mAddressBase;
	hi = ml.mAddressBase + ml.mSize;
	return true;
}

void ATDeviceCustom::InitCartridge(IATDeviceCartridgePort *cartPort) {
	mpCartPort = cartPort;
}

bool ATDeviceCustom::IsLeftCartActive() const {
	for(const MemoryLayer *ml : mMemoryLayers) {
		if (ml->mbRD5Active)
			return true;
	}

	return false;
}

void ATDeviceCustom::SetCartEnables(bool leftEnable, bool rightEnable, bool cctlEnable) {
	for(const MemoryLayer *ml : mMemoryLayers) {
		if (!ml->mpPhysLayer)
			continue;

		if (ml->mbAutoRD4) {
			if (rightEnable)
				mpMemMan->ClearLayerMaskRange(ml->mpPhysLayer);
			else
				mpMemMan->SetLayerMaskRange(ml->mpPhysLayer, 0, 0);
		}

		if (ml->mbAutoRD5) {
			if (leftEnable)
				mpMemMan->ClearLayerMaskRange(ml->mpPhysLayer);
			else
				mpMemMan->SetLayerMaskRange(ml->mpPhysLayer, 0, 0);
		}

		if (ml->mbAutoCCTL) {
			if (cctlEnable)
				mpMemMan->ClearLayerMaskRange(ml->mpPhysLayer);
			else
				mpMemMan->SetLayerMaskRange(ml->mpPhysLayer, 0, 0);
		}
	}
}

void ATDeviceCustom::UpdateCartSense(bool leftActive) {
}

void ATDeviceCustom::InitIndicators(IATDeviceIndicatorManager *r) {
	mpIndicators = r;
	mErrorSourceId = r->AllocateErrorSourceId();
}

void ATDeviceCustom::InitScheduling(ATScheduler *sch, ATScheduler *slowsch) {
	mpScheduler = sch;
}

void ATDeviceCustom::OnScheduledEvent(uint32 id) {
	if (id == kEventId_Sleep) {
		mpEventThreadSleep = nullptr;

		const uint64 t = mpScheduler->GetTick64();
		while(!mSleepHeap.empty()) {
			const auto& si = mSleepHeap.front();
			if (si.mWakeTime > t)
				break;

			uint32 threadIndex = si.mThreadIndex;
			std::pop_heap(mSleepHeap.begin(), mSleepHeap.end(), SleepInfoPred());
			mSleepHeap.pop_back();

			ScheduleThread(*mVMDomain.mThreads[threadIndex]);
		}

		UpdateThreadSleep();
		RunReadyThreads();
	} else if (id == kEventId_Run) {
		mpEventThreadRun = nullptr;
		RunReadyThreads();
	} else if (id == kEventId_RawSend) {
		mpEventRawSend = nullptr;

		if (!mRawSendQueue.empty()) {
			const sint32 threadIndex = mRawSendQueue.front().mThreadIndex;
			mRawSendQueue.pop_front();
			SendNextRawByte();

			// this may be -1 if the head thread was interrupted; we needed to keep the
			// entry to avoid resuming an unrelated thread
			if (threadIndex >= 0) {
				ATVMThread& thread = *mVMDomain.mThreads[threadIndex];
				if (thread.Resume())
					ScheduleThreads(thread.mJoinQueue);
			}

			RunReadyThreads();
		}
	}
}

void ATDeviceCustom::InitSIO(IATDeviceSIOManager *mgr) {
	mpSIOMgr = mgr;
}

IATDeviceSIO::CmdResponse ATDeviceCustom::OnSerialAccelCommand(const ATDeviceSIORequest& cmd) {
	const SIODevice *device = mpSIODeviceTable->mpDevices[cmd.mDevice];
	if (!device)
		return OnSerialBeginCommand(cmd);

	if (!device->mbAllowAccel)
		return IATDeviceSIO::kCmdResponse_BypassAccel;

	const SIOCommand *cmdEntry = device->mpCommands[cmd.mCommand];
	if (cmdEntry && !cmdEntry->mbAllowAccel)
		return IATDeviceSIO::kCmdResponse_BypassAccel;

	return OnSerialBeginCommand(cmd);
}

IATDeviceSIO::CmdResponse ATDeviceCustom::OnSerialBeginCommand(const ATDeviceSIOCommand& cmd) {
	if (mbInitedRawSIO)
		return IATDeviceSIO::kCmdResponse_NotHandled;

	if (!cmd.mbStandardRate)
		return IATDeviceSIO::kCmdResponse_NotHandled;

	const SIODevice *device = mpSIODeviceTable->mpDevices[cmd.mDevice];
	if (!device)
		return IATDeviceSIO::kCmdResponse_NotHandled;

	const SIOCommand *cmdEntry = device->mpCommands[cmd.mCommand];
	if (!cmdEntry)
		return IATDeviceSIO::kCmdResponse_Fail_NAK;

	if (cmdEntry->mpAutoTransferSegment || cmdEntry->mpAutoTransferBlob) {
		mpActiveCommand = cmdEntry;

		mpSIOInterface->BeginCommand();
		mpSIOInterface->SendACK();

		if (cmdEntry->mbAutoTransferWrite) {
			mpSIOInterface->ReceiveData(0, cmdEntry->mAutoTransferLength, true);
			mpSIOInterface->SendComplete();
		} else {
			mpSIOInterface->SendComplete();

			if (cmdEntry->mpAutoTransferBlob)
				mpSIOInterface->SendData(cmdEntry->mpAutoTransferBlob, cmdEntry->mAutoTransferLength, true);
			else
				mpSIOInterface->SendData(cmdEntry->mpAutoTransferSegment->mpData + cmdEntry->mAutoTransferOffset, cmdEntry->mAutoTransferLength, true);
		}

		mpSIOInterface->EndCommand();

		return IATDeviceSIO::kCmdResponse_Start;
	}

	if (!cmdEntry->mpScript)
		return IATDeviceSIO::kCmdResponse_Send_ACK_Complete;
	
	mpActiveCommand = cmdEntry;

	mVMThreadSIO.mThreadVariables[(int)ThreadVarIndex::Command] = cmd.mCommand;
	mVMThreadSIO.mThreadVariables[(int)ThreadVarIndex::Device] = cmd.mDevice;
	mVMThreadSIO.mThreadVariables[(int)ThreadVarIndex::Aux1] = cmd.mAUX[0];
	mVMThreadSIO.mThreadVariables[(int)ThreadVarIndex::Aux2] = cmd.mAUX[1];
	mVMThreadSIO.mThreadVariables[(int)ThreadVarIndex::Aux] = VDReadUnalignedLEU16(cmd.mAUX);
	mVMThreadSIO.mThreadVariables[(int)ThreadVarIndex::Timestamp] = ATSCHEDULER_GETTIME(mpScheduler);
	
	mpSIOInterface->BeginCommand();

	mSIO.mbDataFrameReceived = false;
	mSIO.mbValid = true;
	if (mVMThreadSIO.RunVoid(*cmdEntry->mpScript))
		mpSIOInterface->EndCommand();
	mSIO.mbValid = false;

	return IATDeviceSIO::kCmdResponse_Start;
}

void ATDeviceCustom::OnSerialAbortCommand() {
	mVMThreadSIO.Abort();
	mpActiveCommand = nullptr;
}

void ATDeviceCustom::OnSerialReceiveComplete(uint32 id, const void *data, uint32 len, bool checksumOK) {
	if (!mpActiveCommand)
		return;

	if (id == (uint32)SerialFenceId::AutoReceive) {
		memcpy(mpActiveCommand->mpAutoTransferSegment->mpData + mpActiveCommand->mAutoTransferOffset, data, len);
	} else if (id == (uint32)SerialFenceId::ScriptReceive) {
		mSIOFrameSegment.mpData = (uint8 *)data;
		mSIOFrameSegment.mSize = len;
		mSIO.mbValid = true;
		
		if (mVMThreadSIO.Resume())
			mpSIOInterface->EndCommand();

		mSIO.mbValid = false;
		mSIOFrameSegment.mpData = nullptr;
		mSIOFrameSegment.mSize = 0;
	}
}

void ATDeviceCustom::OnSerialFence(uint32 id) {
	if (id == (uint32)SerialFenceId::ScriptDelay) {
		mSIO.mbValid = true;

		if (mVMThreadSIO.Resume())
			mpSIOInterface->EndCommand();

		mSIO.mbValid = false;
	}
}

void ATDeviceCustom::OnCommandStateChanged(bool asserted) {
	if (mpScriptEventSIOCommandChanged) {
		mVMThread.mThreadVariables[(int)ThreadVarIndex::Timestamp] = ATSCHEDULER_GETTIME(mpScheduler);
		mVMThread.RunVoid(*mpScriptEventSIOCommandChanged);
	}

	if (asserted) {
		ScheduleNextThread(mVMThreadSIOCommandAssertQueue);
	} else {
		ScheduleNextThread(mVMThreadSIOCommandOffQueue);
	}
}

void ATDeviceCustom::OnMotorStateChanged(bool asserted) {
	if (mpScriptEventSIOMotorChanged) {
		mVMThread.mThreadVariables[(int)ThreadVarIndex::Timestamp] = ATSCHEDULER_GETTIME(mpScheduler);
		mVMThread.RunVoid(*mpScriptEventSIOMotorChanged);
	}

	ScheduleNextThread(mVMThreadSIOMotorChangedQueue);
}

void ATDeviceCustom::OnReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) {
	if (mpScriptEventSIOReceivedByte) {
		mVMDomain.mSpecialVariables[(int)SpecialVarIndex::Value] = c;
		mVMThread.mThreadVariables[(int)ThreadVarIndex::Command] = command;
		mVMThread.mThreadVariables[(int)ThreadVarIndex::Timestamp] = ATSCHEDULER_GETTIME(mpScheduler);
		mVMThread.RunVoid(*mpScriptEventSIOReceivedByte);
	} else {
		ATVMThread *thread = mVMThreadRawRecvQueue.Pop();

		if (thread) {
			mSIO.mRecvChecksum += c;
			mSIO.mRecvLast = c;

			thread->mThreadVariables[(int)ThreadVarIndex::Aux] = cyclesPerBit;
			thread->SetResumeInt(c);
			if (thread->Resume())
				ScheduleThreads(thread->mJoinQueue);
		}
	}
}

void ATDeviceCustom::OnSendReady() {
}

void ATDeviceCustom::InitPBI(IATDevicePBIManager *pbiman) {
	mpPBIMgr = pbiman;
}

void ATDeviceCustom::GetPBIDeviceInfo(ATPBIDeviceInfo& devInfo) const {
	devInfo.mbHasIrq = mbPBIDeviceHasIrq;
	devInfo.mDeviceId = mPBIDeviceId;
}

void ATDeviceCustom::SelectPBIDevice(bool enable) {
	if (mbPBIDeviceSelected != enable) {
		mbPBIDeviceSelected = enable;

		for(MemoryLayer *ml : mMemoryLayers) {
			if (ml->mbAutoPBI)
				UpdateLayerModes(*ml);
		}

		if (enable) {
			if (mpScriptEventPBISelect) {
				mVMThread.mThreadVariables[(int)ThreadVarIndex::Timestamp] = ATSCHEDULER_GETTIME(mpScheduler);
				mVMThread.RunVoid(*mpScriptEventPBISelect);
			}
		} else {
			if (mpScriptEventPBIDeselect) {
				mVMThread.mThreadVariables[(int)ThreadVarIndex::Timestamp] = ATSCHEDULER_GETTIME(mpScheduler);
				mVMThread.RunVoid(*mpScriptEventPBIDeselect);
			}
		}
	}
}

bool ATDeviceCustom::IsPBIOverlayActive() const {
	if (!mbPBIDeviceSelected)
		return false;

	for(const auto *ml : mMemoryLayers) {
		if (ml->mbAutoPBI && ml->mAddressBase >= 0xD800 && ml->mAddressBase < 0xE000 && (ml->mEnabledModes & kATMemoryAccessMode_R))
			return true;
	}

	return false;
}

uint8 ATDeviceCustom::ReadPBIStatus(uint8 busData, bool debugOnly) {
	if (mbPBIDeviceHasIrq) {
		if (mbPBIDeviceIrqAsserted)
			busData |= mPBIDeviceId;
		else
			busData &= ~mPBIDeviceId;
	}

	return busData;
}

IATDeviceBus *ATDeviceCustom::GetDeviceBusById(sint32 id) {
	if (id >= 0 && (uint32)id < mBusList.size())
		return mBusList[id];

	return nullptr;
}

sint32 ATDeviceCustom::GetDeviceBusIdByIndex(uint32 index) {
	// This has poor asymptotic performance, but we shouldn't have too many
	// buses.
	sint32 id = 0;
	for(IATDeviceBus *bus : mBusList) {
		if (bus) {
			if (!index)
				return id;

			--index;
		}

		++id;
	}

	return -1;
}

void ATDeviceCustom::DumpStatus(ATConsoleOutput& output) {
	output <<= "Global variables:";

	size_t n = mVMDomain.mGlobalVariables.size();
	for(size_t i = 0; i < n; ++i) {
		output("    %-20s = %d", mVMDomain.mGlobalVariableNames[i], mVMDomain.mGlobalVariables[i]);
	}

	output <<= "";

	output <<= "Threads:";

	for(ATVMThread *thread : mVMDomain.mThreads) {
		output("    [%2u] %-20s%s"
			, thread->mThreadIndex
			, thread->mpDebugName ? thread->mpDebugName : "<no name>"
			, !thread->IsStarted() ? "Stopped" : "Suspended"
		);

		VDStringA s;
		for(const ATVMThread::StackFrame& stackFrame : std::views::reverse(thread->mStackFrames)) {
			s.sprintf("        %s()", stackFrame.mpFunction->mpName ? stackFrame.mpFunction->mpName : "<unnamed>");

			const uint8 *pc = stackFrame.mpPC;
			if (pc > stackFrame.mpFunction->mpByteCode)
				--pc;

			const ATVMDebugSourceReference *sourceRef = stackFrame.mpFunction->TryGetDebugSourceRefFromCodePos(pc);
			if (sourceRef)
				s.append_sprintf(" [source:%u]", sourceRef->mLineId);

			output <<= s.c_str();
		}
	}
}

bool ATDeviceCustom::OnFileUpdated(const wchar_t *path) {
	if (CheckForTrackedChanges()) {
		ReloadConfig();

		if (mbInitedCustomDevice && mpIndicators) {
			mpIndicators->SetStatusMessage(L"Custom device configuration reloaded.");
			ColdReset();
		}
	}

	return true;
}

template<bool T_DebugOnly>
sint32 ATDeviceCustom::ReadControl(MemoryLayer& ml, uint32 addr) {
	const AddressBinding& binding = ml.mpReadBindings[addr - ml.mAddressBase];
	sint32 v;

	switch(binding.mAction) {
		case AddressAction::None:
			return -1;

		case AddressAction::ConstantData:
			return binding.mByteData;

		case AddressAction::Network:
			return 0xFF & SendNetCommand(addr, 0, T_DebugOnly ? NetworkCommand::DebugReadByte : NetworkCommand::ReadByte);

		case AddressAction::Script:
			mVMDomain.mSpecialVariables[(int)SpecialVarIndex::Address] = addr;
			mVMThread.mThreadVariables[(int)ThreadVarIndex::Timestamp] = ATSCHEDULER_GETTIME(mpScheduler);

			v = 0xFF & mVMThread.RunInt(*mScriptFunctions[binding.mScriptFunctions[T_DebugOnly]]);
			return v;

		case AddressAction::Variable:
			return 0xFF & mVMDomain.mGlobalVariables[binding.mVariableIndex];
	}

	return -1;
}

bool ATDeviceCustom::WriteControl(MemoryLayer& ml, uint32 addr, uint8 value) {
	const AddressBinding& binding = ml.mpWriteBindings[addr - ml.mAddressBase];

	switch(binding.mAction) {
		case AddressAction::None:
			return false;

		case AddressAction::Block:
			return true;

		case AddressAction::Network:
			SendNetCommand(addr, value, NetworkCommand::WriteByte);
			return true;

		case AddressAction::Script:
			mVMDomain.mSpecialVariables[(int)SpecialVarIndex::Address] = addr;
			mVMDomain.mSpecialVariables[(int)SpecialVarIndex::Value] = value;
			mVMThread.mThreadVariables[(int)ThreadVarIndex::Timestamp] = ATSCHEDULER_GETTIME(mpScheduler);

			mVMThread.RunVoid(*mScriptFunctions[binding.mScriptFunctions[0]]);
			return true;

		case AddressAction::Variable:
			mVMDomain.mGlobalVariables[binding.mVariableIndex] = value;
			return true;
	}

	return false;
}

bool ATDeviceCustom::PostNetCommand(uint32 address, sint32 value, NetworkCommand cmd) {
	if (!mpNetworkEngine)
		return false;

	uint8 cmdbuf[17];
	cmdbuf[0] = (uint8)cmd;
	VDWriteUnalignedLEU32(&cmdbuf[1], address);
	VDWriteUnalignedLEU32(&cmdbuf[5], value);
	VDWriteUnalignedLEU64(&cmdbuf[9], mpScheduler->GetTick64());

	for(;;) {
		if (mpNetworkEngine->Send(cmdbuf, 17))
			break;

		if (!TryRestoreNet())
			return false;
	}

	return true;
}

sint32 ATDeviceCustom::SendNetCommand(uint32 address, sint32 value, NetworkCommand cmd) {
	if (!mpNetworkEngine)
		return value;

	if (!PostNetCommand(address, value, cmd))
		return value;

	return ExecuteNetRequests(true);
}

bool ATDeviceCustom::TryRestoreNet() {
	if (!mpNetworkEngine->Restore())
		return false;

	SendNetCommand(0, 0x7F000001, NetworkCommand::ColdReset);
	if (!mpNetworkEngine->SetRecvNotifyEnabled())
		OnNetRecvOOB();

	return true;
}

sint32 ATDeviceCustom::ExecuteNetRequests(bool waitingForReply) {
	uint8 cmdbuf[16];
	sint32 returnValue = 0;
	bool ignoreReturnValue = false;

	for(;;) {
		if (!waitingForReply) {
			if (mpNetworkEngine->SetRecvNotifyEnabled())
				break;
		}

		if (!mpNetworkEngine->Recv(cmdbuf, 1)) {
			waitingForReply = false;

			if (!mpNetworkEngine->Restore())
				return returnValue;

			// Post a variant of the cold reset to see if we can identify a v0.8+
			// server. A v0.8+ server will identify the aux2 value and interpret
			// this as init.
			PostNetCommand(0, 0x7F000001, NetworkCommand::ColdReset);
			waitingForReply = true;
			ignoreReturnValue = true;
			continue;
		}

		switch(cmdbuf[0]) {
			case (uint8)NetworkReply::None:
				break;

			case (uint8)NetworkReply::ReturnValue:
				mpNetworkEngine->Recv(cmdbuf, 4);

				// check if we had to reset the connection; if so, this is from
				// the cold reset we used to reinit the handler, and is not
				// for the incoming call
				if (ignoreReturnValue)
					returnValue = 0;
				else
					returnValue = VDReadUnalignedLEU32(&cmdbuf[0]);

				waitingForReply = false;
				break;

			case (uint8)NetworkReply::EnableMemoryLayer:
				mpNetworkEngine->Recv(cmdbuf, 2);
				if (cmdbuf[0] < mMemoryLayers.size()) {
					auto *ml = mMemoryLayers[cmdbuf[0]];

					if (ml->mpPhysLayer) {
						uint8 modes = 0;

						if (cmdbuf[1] & 0x01)
							modes |= kATMemoryAccessMode_W;

						if (cmdbuf[1] & 0x02)
							modes |= kATMemoryAccessMode_AR;

						if (ml->mEnabledModes != modes) {
							ml->mEnabledModes = modes;

							UpdateLayerModes(*ml);
						}
					}
				} else
					PostNetError("EnableMemoryLayer: Invalid memory layer index");
				break;

			case (uint8)NetworkReply::SetMemoryLayerOffset:
				mpNetworkEngine->Recv(cmdbuf, 5);
				if (cmdbuf[0] < mMemoryLayers.size()) {
					auto *ml = mMemoryLayers[cmdbuf[0]];

					if (ml->mpPhysLayer && ml->mpSegment) {
						uint32 offset = VDReadUnalignedLEU32(&cmdbuf[1]);

						if (!(offset & 0xFF) && offset <= ml->mMaxOffset) {
							ml->mSegmentOffset = offset;

							if (!ml->mbIsWriteThrough)
								mpMemMan->SetLayerMemory(ml->mpPhysLayer, ml->mpSegment->mpData + offset);
						}
					} else
						PostNetError("SetMemoryLayerOffset: Invalid memory layer offset");
				} else
					PostNetError("SetMemoryLayerOffset: Invalid memory layer index");
				break;

			case (uint8)NetworkReply::SetMemoryLayerSegmentOffset:
				mpNetworkEngine->Recv(cmdbuf, 6);
				if (cmdbuf[0] < mMemoryLayers.size() && cmdbuf[1] < mSegments.size()) {
					auto& ml = *mMemoryLayers[cmdbuf[0]];
					auto& seg = *mSegments[cmdbuf[1]];

					uint32 offset = VDReadUnalignedLEU32(&cmdbuf[2]);
					if (ml.mpPhysLayer && ml.mpSegment && !(offset & 0xFF) && offset < seg.mSize && seg.mSize - offset >= ml.mSize) {
						ml.mpSegment = &seg;
						ml.mMaxOffset = seg.mSize - ml.mSize;
						ml.mSegmentOffset = offset;

						if (!ml.mbIsWriteThrough)
							mpMemMan->SetLayerMemory(ml.mpPhysLayer, seg.mpData + offset);
					} else
						PostNetError("SetMemoryLayerSegmentOffset: Invalid memory layer range");
				} else
					PostNetError("SetMemoryLayerSegmentOffset: Invalid memory layer index");
				break;

			case (uint8)NetworkReply::SetMemoryLayerReadOnly:
				mpNetworkEngine->Recv(cmdbuf, 2);
				if (cmdbuf[0] < mMemoryLayers.size()) {
					auto *ml = mMemoryLayers[cmdbuf[0]];

					if (ml->mpPhysLayer)
						mpMemMan->SetLayerReadOnly(ml->mpPhysLayer, cmdbuf[1] != 0);
				} else
					PostNetError("SetMemoryLayerReadOnly: Invalid memory layer index");
				break;

			case (uint8)NetworkReply::ReadSegmentMemory:
				mpNetworkEngine->Recv(cmdbuf, 9);
				if (cmdbuf[0] < mSegments.size()) {
					auto& seg = *mSegments[cmdbuf[0]];
					uint32 offset = VDReadUnalignedLEU32(&cmdbuf[1]);
					uint32 len = VDReadUnalignedLEU32(&cmdbuf[5]);

					if (offset <= seg.mSize && seg.mSize - offset >= len) {
						mpNetworkEngine->Send(seg.mpData + offset, len);
					} else
						PostNetError("ReadSegmentMemory: Invalid segment range");
				} else
					PostNetError("ReadSegmentMemory: Invalid segment index");
				break;

			case (uint8)NetworkReply::WriteSegmentMemory:
				mpNetworkEngine->Recv(cmdbuf, 9);
				if (cmdbuf[0] < mSegments.size()) {
					auto& seg = *mSegments[cmdbuf[0]];
					uint32 offset = VDReadUnalignedLEU32(&cmdbuf[1]);
					uint32 len = VDReadUnalignedLEU32(&cmdbuf[5]);

					if (offset <= seg.mSize && seg.mSize - offset >= len) {
						mpNetworkEngine->Recv(seg.mpData + offset, len);
					} else
						PostNetError("WriteSegmentMemory: Invalid segment range");
				} else
					PostNetError("WriteSegmentMemory: Invalid segment index");
				break;

			case (uint8)NetworkReply::CopySegmentMemory:
				mpNetworkEngine->Recv(cmdbuf, 14);
				if (cmdbuf[0] < mSegments.size() && cmdbuf[5] < mSegments.size()) {
					auto& dstSeg = *mSegments[cmdbuf[0]];
					uint32 dstOffset = VDReadUnalignedLEU32(&cmdbuf[1]);
					auto& srcSeg = *mSegments[cmdbuf[5]];
					uint32 srcOffset = VDReadUnalignedLEU32(&cmdbuf[6]);
					uint32 len = VDReadUnalignedLEU32(&cmdbuf[10]);

					if (dstOffset <= dstSeg.mSize && dstSeg.mSize - dstOffset >= len
						&& srcOffset <= srcSeg.mSize && srcSeg.mSize - srcOffset >= len)
					{
						memmove(dstSeg.mpData + dstOffset, srcSeg.mpData + srcOffset, len);
					} else {
						PostNetError("CopySegmentMemory: Invalid segment ranges");
					}
				} else
					PostNetError("CopySegmentMemory: Invalid segment index");
				break;

			case (uint8)NetworkReply::ScriptInterrupt:
				mpNetworkEngine->Recv(cmdbuf, 8);

				if (mpScriptEventNetworkInterrupt && mVMDomain.mpActiveThread != &mVMThreadScriptInterrupt) {
					mVMThreadScriptInterrupt.mThreadVariables[(int)ThreadVarIndex::Aux1] = VDReadUnalignedLEU32(&cmdbuf[0]);
					mVMThreadScriptInterrupt.mThreadVariables[(int)ThreadVarIndex::Aux2] = VDReadUnalignedLEU32(&cmdbuf[4]);
					mVMThreadScriptInterrupt.mThreadVariables[(int)ThreadVarIndex::Timestamp] = ATSCHEDULER_GETTIME(mpScheduler);
					mVMThreadScriptInterrupt.RunVoid(*mpScriptEventNetworkInterrupt);
				}
				break;

			case (uint8)NetworkReply::GetSegmentNames:
				{
					uint8 buf[4];
					VDWriteUnalignedLEU32(buf, (uint32)mSegments.size());

					mpNetworkEngine->Send(buf, 4);

					for(Segment *seg : mSegments) {
						uint32 nameLen = (uint32)strlen(seg->mpName);

						VDWriteUnalignedLEU32(buf, nameLen);
						mpNetworkEngine->Send(buf, 4);
						mpNetworkEngine->Send(seg->mpName, nameLen);
					}
				}
				break;

			case (uint8)NetworkReply::GetMemoryLayerNames:
				{
					uint8 buf[4];
					VDWriteUnalignedLEU32(buf, (uint32)mMemoryLayers.size());

					mpNetworkEngine->Send(buf, 4);

					for(MemoryLayer *ml: mMemoryLayers) {
						uint32 nameLen = (uint32)strlen(ml->mpName);

						VDWriteUnalignedLEU32(buf, nameLen);
						mpNetworkEngine->Send(buf, 4);
						mpNetworkEngine->Send(ml->mpName, nameLen);
					}
				}
				break;

			case (uint8)NetworkReply::FillSegmentMemory:
				mpNetworkEngine->Recv(cmdbuf, 9);
				if (cmdbuf[0] < mSegments.size()) {
					auto& seg = *mSegments[cmdbuf[0]];
					uint32 offset = VDReadUnalignedLEU32(&cmdbuf[1]);
					uint8 val = cmdbuf[5];
					uint32 len = VDReadUnalignedLEU32(&cmdbuf[6]);

					if (offset <= seg.mSize && seg.mSize - offset >= len) {
						memset(seg.mpData + offset, val, len);
					} else
						PostNetError("FillSegmentMemory: Invalid segment range");
				} else
					PostNetError("FillSegmentMemory: Invalid segment index");
				break;

			case (uint8)NetworkReply::SetProtocolLevel:
				mpNetworkEngine->Recv(cmdbuf, 1);
				if (cmdbuf[0] < 2)
					PostNetError("SetProtocolLevel: Invalid protocol version");
				break;

			default:
				PostNetError("Invalid command received");
				break;
		}
	}

	return returnValue;
}

void ATDeviceCustom::PostNetError(const char *msg) {
	if (mpNetworkEngine) {
		uint32 len = (uint32)strlen(msg);

		// send the command frame directly as to avoid a loop
		uint8 cmdbuf[17];
		cmdbuf[0] = (uint8)NetworkCommand::Error;
		VDWriteUnalignedLEU32(&cmdbuf[1], 0);
		VDWriteUnalignedLEU32(&cmdbuf[5], len);
		VDWriteUnalignedLEU64(&cmdbuf[9], mpScheduler->GetTick64());
		mpNetworkEngine->Send(cmdbuf, 17);

		mpNetworkEngine->Send(msg, len);
	}

	if (mpIndicators) {
		VDStringW s;
		s.sprintf(L"Communication error with custom device server: %hs", msg);
		mpIndicators->ReportError(mErrorSourceId, s.c_str());
	}
}

void ATDeviceCustom::OnNetRecvOOB() {
	ExecuteNetRequests(false);
}

void ATDeviceCustom::ResetCustomDevice() {
	for(ATVMThread *thread : mVMDomain.mThreads)
		thread->Reset();

	mClock.Reset();

	mVMThreadRunQueue.Reset();
	mVMThreadRawRecvQueue.Reset();
	mVMThreadSIOCommandAssertQueue.Reset();
	mVMThreadSIOCommandOffQueue.Reset();
	mVMThreadSIOMotorChangedQueue.Reset();
	mSleepHeap.clear();
	mRawSendQueue.clear();

	if (mpScheduler) {
		mpScheduler->UnsetEvent(mpEventThreadRun);
		mpScheduler->UnsetEvent(mpEventThreadSleep);
		mpScheduler->UnsetEvent(mpEventRawSend);
	}

	mSIO.Reset();

	ResetPBIInterrupt();
}

void ATDeviceCustom::ResetPBIInterrupt() {
	if (mpPBIDeviceIrqController && mPBIDeviceIrqBit && mbPBIDeviceIrqAsserted) {
		mbPBIDeviceIrqAsserted = false;
		mpPBIDeviceIrqController->Negate(mPBIDeviceIrqBit, false);
	}
}

void ATDeviceCustom::ShutdownCustomDevice() {
	auto& sem = *g_sim.GetEventManager();

	if (mpScheduler) {
		mpScheduler->UnsetEvent(mpEventThreadRun);
		mpScheduler->UnsetEvent(mpEventThreadSleep);
		mpScheduler->UnsetEvent(mpEventRawSend);
	}

	if (mEventBindingVBLANK) {
		sem.RemoveEventCallback(mEventBindingVBLANK);
		mEventBindingVBLANK = 0;
	}

	mTrackedFileWatcher.Shutdown();

	mbInitedCustomDevice = false;
	mpActiveCommand = nullptr;

	mDeviceName.clear();

	if (mpNetworkEngine) {
		mpNetworkEngine->Shutdown();
		mpNetworkEngine = nullptr;
	}

	if (mAsyncNetCallback) {
		mpAsyncDispatcher->Cancel(&mAsyncNetCallback);
		mAsyncNetCallback = 0;
	}

	if (mbInitedROMEnableHook) {
		mbInitedROMEnableHook = false;

		mpMMU->RemoveROMMappingHook(&mROMEnableHook);
	}

	if (mbInitedCart) {
		mbInitedCart = false;

		mpCartPort->RemoveCartridge(mCartId, this);
	}

	if (mbInitedSIO) {
		mbInitedSIO = false;

		if (mbInitedRawSIO) {
			mbInitedRawSIO = false;

			mpSIOMgr->RemoveRawDevice(this);
		}

		mpSIOInterface = nullptr;
	}

	if (mbInitedPBI) {
		mbInitedPBI = false;

		if (mpPBIDeviceIrqController) {
			if (mPBIDeviceIrqBit) {
				mpPBIDeviceIrqController->FreeIRQ(mPBIDeviceIrqBit);
				mPBIDeviceIrqBit = 0;
			}

			mpPBIDeviceIrqController = nullptr;
		}

		mpPBIMgr->RemoveDevice(this);
	}

	while(!mVideoOutputs.empty()) {
		delete mVideoOutputs.back();
		mVideoOutputs.pop_back();
	}

	while(!mImages.empty()) {
		delete mImages.back();
		mImages.pop_back();
	}

	while(!mVMObjects.empty()) {
		auto *p = mVMObjects.back();
		mVMObjects.pop_back();

		delete p;
	}

	for(MemoryLayer *ml : mMemoryLayers) {
		if (ml->mpPhysLayer)
			mpMemMan->DeleteLayerPtr(&ml->mpPhysLayer);
	}

	for(auto *& cport : mpControllerPorts) {
		if (cport) {
			cport->Shutdown();
			cport->~ControllerPort();
			cport = nullptr;
		}
	}

	mMemoryLayers = {};
	mSegments = {};
	mpSIODeviceTable = nullptr;

	mpEmulatorObj = nullptr;
	mbHasSerialPort = false;
	mbHasPrinterPort = false;

	mpScriptEventColdReset = nullptr;
	mpScriptEventWarmReset = nullptr;
	mpScriptEventInit = nullptr;
	mpScriptEventVBLANK = nullptr;
	mpScriptEventSIOCommandChanged = nullptr;
	mpScriptEventSIOMotorChanged = nullptr;
	mpScriptEventSIOReceivedByte = nullptr;
	mpScriptEventPBISelect = nullptr;
	mpScriptEventPBIDeselect = nullptr;
	mpScriptEventNetworkInterrupt = nullptr;

	mPBIDeviceId = 0;

	mSleepHeap.clear();

	mConfigAllocator.Clear();
	mVMDomain.Clear();

	while(!mScriptThreads.empty()) {
		delete mScriptThreads.back();
		mScriptThreads.pop_back();
	}
}

void ATDeviceCustom::ReinitSegmentData(bool clearNonVolatile) {
	for(const Segment *seg : mSegments) {
		if (!clearNonVolatile && seg->mbNonVolatile)
			continue;

		uint8 *dst = seg->mpData;
		const uint32 len = seg->mSize;

		if (!dst)
			continue;
		
		const uint8 *pat = seg->mpInitData;
		const uint32 patlen = seg->mInitSize;

		if (seg->mInitSize == 1)
			if (pat)
				memset(dst, pat[0], len);
			else
				memset(dst, 0, len);
		else if (seg->mInitSize == 2) {
			VDMemset16(dst, VDReadUnalignedU16(pat), len >> 1);
			memcpy(dst + (len & ~1), pat, len & 1);
		} else if (seg->mInitSize == 4) {
			VDMemset32(dst, VDReadUnalignedU32(pat), len >> 1);
			memcpy(dst + (len & ~3), pat, len & 3);
		} else if (pat) {
			// we are guaranteed that the segment is at least as big as the pattern,
			// as this is enforced by the parser
			memcpy(dst, pat, patlen);

			const uint8 *src = dst;
			dst += patlen;

			// do a deliberately self-referencing ascending copy
			for(uint32 i = patlen; i < len; ++i)
				*dst++ = *src++;
		}
	}
}

void ATDeviceCustom::SendNextRawByte() {
	VDASSERT(!mpEventRawSend);

	if (mRawSendQueue.empty())
		return;

	RawSendInfo& rsi = mRawSendQueue.front();
	mSIO.mSendChecksum += rsi.mByte;

	mpSIOMgr->SendRawByte(rsi.mByte, rsi.mCyclesPerBit);
	mpScheduler->SetEvent(rsi.mCyclesPerBit * 10, this, kEventId_RawSend, mpEventRawSend);
}

void ATDeviceCustom::AbortRawSend(const ATVMThread& thread) {
	if (mRawSendQueue.empty())
		return;

	// we need a special case for the head thread; its send is in progress so we can't
	// remove the entry, we have to null it instead
	if (mRawSendQueue.front().mThreadIndex == thread.mThreadIndex) {
		mRawSendQueue.front().mThreadIndex = -1;
		return;
	}

	auto it = std::remove_if(mRawSendQueue.begin(), mRawSendQueue.end(),
		[threadIndex = thread.mThreadIndex](const RawSendInfo& rsi) {
			return rsi.mThreadIndex == threadIndex;
		}
	);

	if (it != mRawSendQueue.end())
		mRawSendQueue.pop_back();
}

void ATDeviceCustom::AbortThreadSleep(const ATVMThread& thread) {
	auto it = std::find_if(mSleepHeap.begin(), mSleepHeap.end(),
		[threadIndex = thread.mThreadIndex](const SleepInfo& si) {
			return si.mThreadIndex == threadIndex;
		}
	);
	
	if (it == mSleepHeap.end()) {
		VDFAIL("Trying to abort sleep for thread not in sleep heap.");
		return;
	}

	*it = mSleepHeap.back();
	mSleepHeap.pop_back();
	std::sort_heap(mSleepHeap.begin(), mSleepHeap.end(), SleepInfoPred());

	UpdateThreadSleep();
}

void ATDeviceCustom::UpdateThreadSleep() {
	if (mSleepHeap.empty()) {
		mpScheduler->UnsetEvent(mpEventThreadSleep);
	} else {
		uint64 delay = mSleepHeap.front().mWakeTime - mpScheduler->GetTick64();

		if (!mpEventThreadSleep || mpScheduler->GetTicksToEvent(mpEventThreadSleep) > delay)
			mpScheduler->SetEvent((uint32)std::min<uint64>(delay, 1000000), this, kEventId_Sleep, mpEventThreadSleep);
	}
}

void ATDeviceCustom::ScheduleThread(ATVMThread& thread) {
	mVMThreadRunQueue.Suspend(thread);

	if (!mInThreadRun) {
		if (!mpEventThreadRun)
			mpScheduler->SetEvent(1, this, kEventId_Run, mpEventThreadRun);
	}
}

void ATDeviceCustom::ScheduleNextThread(ATVMThreadWaitQueue& queue) {
	if (queue.GetNext()) {
		queue.TransferNext(mVMThreadRunQueue);

		if (!mInThreadRun) {
			if (!mpEventThreadRun)
				mpScheduler->SetEvent(1, this, kEventId_Run, mpEventThreadRun);
		}
	}
}

void ATDeviceCustom::ScheduleThreads(ATVMThreadWaitQueue& queue) {
	if (queue.GetNext()) {
		queue.TransferAll(mVMThreadRunQueue);

		if (!mInThreadRun) {
			if (!mpEventThreadRun)
				mpScheduler->SetEvent(1, this, kEventId_Run, mpEventThreadRun);
		}
	}
}

void ATDeviceCustom::RunReadyThreads() {
	++mInThreadRun;
	if (auto *thread = mVMThreadRunQueue.Pop()) {
		const sint32 t32 = (sint32)ATSCHEDULER_GETTIME(mpScheduler);

		do {
			thread->mThreadVariables[(int)ThreadVarIndex::Timestamp] = t32;

			if (thread->Resume())
				ScheduleThreads(thread->mJoinQueue);
		} while((thread = mVMThreadRunQueue.Pop()));
	}
	--mInThreadRun;
}

void ATDeviceCustom::OnROMEnablesChanged() {
	for(MemoryLayer *ml : mMemoryLayers) {
		if (ml->mbAutoOS || ml->mbAutoBASIC || ml->mbAutoSelfTest)
			UpdateLayerModes(*ml);
	}
}

void ATDeviceCustom::ReloadConfig() {
	ShutdownCustomDevice();
	ClearFileTracking();

	if (mpIndicators)
		mpIndicators->ClearErrors(mErrorSourceId);

	try {
		{
			vdrefptr<ATVFSFileView> view;
			OpenViewWithTracking(mConfigPath.c_str(), ~view);

			mResourceBasePath.assign(mConfigPath.c_str(), ATVFSSplitPathFile(mConfigPath.c_str()));

			IVDRandomAccessStream& stream = view->GetStream();
			sint64 len = stream.Length();

			if ((uint64)len >= kMaxRawConfigSize)
				throw MyError("Custom device description is too large: %llu bytes.", (unsigned long long)len);

			vdblock<char> buf;
			buf.resize((size_t)len);
			stream.Read(buf.data(), len);

			ProcessDesc(buf.data(), buf.size());
		}

		bool needCart = false;

		for(MemoryLayer *ml : mMemoryLayers) {
			if (ml->mbAutoRD4 || ml->mbAutoRD5 || ml->mbAutoCCTL) {
				needCart = true;
				break;
			}
		}

		if (needCart) {
			mpCartPort->AddCartridge(this, kATCartridgePriority_Default, mCartId);
			mpCartPort->OnLeftWindowChanged(mCartId, IsLeftCartActive());

			mbInitedCart = true;
		}

		if (mpSIODeviceTable) {
			mpSIOInterface = mpSIOMgr->AddDevice(this);

			// for compatibility with previous behavior before this was optional
			mpSIOInterface->SetCommandTruncationEnabled(true);

			mbInitedSIO = true;
		}

		if (mPBIDeviceId) {
			mpPBIMgr->AddDevice(this);
			mbInitedPBI = true;

			if (mbPBIDeviceHasIrq) {
				mpPBIDeviceIrqController = GetService<ATIRQController>();
				mPBIDeviceIrqBit = mpPBIDeviceIrqController->AllocateIRQ();
			}
		}

		ReinitSegmentData(true);

		if (mNetworkPort) {
			mpNetworkEngine = ATCreateDeviceCustomNetworkEngine(
				mNetworkPort,
				*GetService<IATTimerService>(),
				[this] {
					mpAsyncDispatcher->Queue(&mAsyncNetCallback,
						[this] {
							VDASSERT(mpNetworkEngine && mbInitedCustomDevice);

							if (!TryRestoreNet())
								OnNetRecvOOB();
						}
					);
				}
			);

			if (!mpNetworkEngine)
				throw MyError("Unable to set up local networking for custom device.");
		}

		if (mpScriptEventVBLANK) {
			mEventBindingVBLANK = g_sim.GetEventManager()->AddEventCallback(kATSimEvent_VBLANK,
				[this] {
					mVMThread.mThreadVariables[(int)ThreadVarIndex::Timestamp] = ATSCHEDULER_GETTIME(mpScheduler);
					mVMThread.RunVoid(*mpScriptEventVBLANK);
				}
			);
		}

		mbInitedCustomDevice = true;
		mLastError.clear();

		ResetCustomDevice();

		// If any memory layers require the ROM enable hook, install it now. Note that the hook will be called
		// as soon as it is installed.
		for(const MemoryLayer *ml : mMemoryLayers) {
			if (ml->mbAutoOS || ml->mbAutoBASIC || ml->mbAutoSelfTest) {
				if (!mbInitedROMEnableHook) {
					mbInitedROMEnableHook = true;

					mROMEnableHook = [this]
						{
							OnROMEnablesChanged();
						};
					mpMMU->AddROMMappingHook(&mROMEnableHook);
				}
				break;
			}
		}

		if (mpScriptEventInit) {
			mVMThread.mThreadVariables[(int)ThreadVarIndex::Timestamp] = ATSCHEDULER_GETTIME(mpScheduler);
			mVMThread.RunVoid(*mpScriptEventInit);
		}

		// make sure that we have a notification request posted to the network code
		// in case the remote server issues a request before we do
		if (mpNetworkEngine)
			ExecuteNetRequests(false);

		// go over all devices on the disconnected bus and try to add them back to current buses
		TryReconnectChildDevices();

		// if we don't need the disconnected bus, remove it now
		if (mDisconnectedBus.GetBusId() >= 0 && !mDisconnectedBus.HasChildDevices()) {
			RemoveDeviceBus(mDisconnectedBus);
			mDisconnectedBus.SetBusId(-1);
		}
	} catch(const MyError& e) {
		ShutdownCustomDevice();

		mLastError = e.wc_str();

		if (mpIndicators) {
			mpIndicators->ClearErrors(mErrorSourceId);
			mpIndicators->ReportError(mErrorSourceId, mLastError.c_str());
		}
	}

	// We need to set up tracking even if the load failed, so we can try again when the file is fixed
	if (!mTrackedFiles.empty() && mbHotReload) {
		try {
			mTrackedFileWatcher.InitDir(VDFileSplitPathLeft(mTrackedFiles.begin()->first).c_str(), false, this);
		} catch(const MyError&) {
		}
	}
}

class ATDeviceCustom::MemberParser {
public:
	MemberParser(const ATVMDataValue& val);

	const ATVMDataValue& Required(const char *name);
	const ATVMDataValue *Optional(const char *name);

	std::optional<bool> OptionalBool(const char *name);
	std::optional<sint32> OptionalSint32(const char *name);
	uint32 RequiredUint32(const char *name);
	const char *RequiredString(const char *name);
	const ATVMDataValue& RequiredStringValue(const char *name);

	void AssertNoUnused();

	const ATVMDataValue& mRootObject;
	const ATVMDataValue *mpLastMemberValue = nullptr;
	vdfastvector<const ATVMDataMember *> mMembers;
};

ATDeviceCustom::MemberParser::MemberParser(const ATVMDataValue& val)
	: mRootObject(val)
{
	if (!val.IsDataObject())
		throw ATVMCompileError(mRootObject, "Expected data object");

	size_t n = val.mLength;
	mMembers.resize(n);

	for(size_t i = 0; i < n; ++i)
		mMembers[i] = &val.mpObjectMembers[i];
}

const ATVMDataValue& ATDeviceCustom::MemberParser::Required(const char *name) {
	const ATVMDataValue *val = Optional(name);

	if (!val)
		throw ATVMCompileError::Format(mRootObject, "Required member '%s' not found", name);

	mpLastMemberValue = val;
	return *val;
}

const ATVMDataValue *ATDeviceCustom::MemberParser::Optional(const char *name) {
	auto it = std::find_if(mMembers.begin(), mMembers.end(),
		[name, hash = VDHashString32(name)](const ATVMDataMember *member) {
			return member->mNameHash == hash && !strcmp(member->mpName, name);
		}
	);

	if (it == mMembers.end())
		return nullptr;

	auto val = *it;

	mMembers.erase(it);

	mpLastMemberValue = &val->mValue;
	return &val->mValue;
}

std::optional<bool> ATDeviceCustom::MemberParser::OptionalBool(const char *name) {
	const ATVMDataValue *value = Optional(name);

	if (!value)
		return std::nullopt;

	if (!value->IsInteger())
		throw ATVMCompileError(*value, "Boolean expected");

	return value->mIntValue != 0;
}

std::optional<sint32> ATDeviceCustom::MemberParser::OptionalSint32(const char *name) {
	const ATVMDataValue *value = Optional(name);

	if (!value)
		return std::nullopt;

	if (!value->IsInteger())
		throw ATVMCompileError(*value, "Integer expected");

	return value->mIntValue;
}

uint32 ATDeviceCustom::MemberParser::RequiredUint32(const char *name) {
	auto& val = Required(name);

	if (!val.IsInteger())
		throw ATVMCompileError(val, "Integer expected");

	if (val.mIntValue < 0)
		throw ATVMCompileError(val, "Negative integer not allowed");

	return (uint32)val.mIntValue;
}

const char *ATDeviceCustom::MemberParser::RequiredString(const char *name) {
	return RequiredStringValue(name).mpStrValue;
}

const ATVMDataValue& ATDeviceCustom::MemberParser::RequiredStringValue(const char *name) {
	auto& val = Required(name);

	if (!val.IsString())
		throw ATVMCompileError(val, "String expected");

	return val;
}

void ATDeviceCustom::MemberParser::AssertNoUnused() {
	if (!mMembers.empty()) 
		throw ATVMCompileError::Format(mRootObject, "Unexpected member '%s'", (*mMembers.begin())->mpName);
}

void ATDeviceCustom::ProcessDesc(const void *buf, size_t len) {
	ATVMCompiler compiler(mVMDomain);

	mpEmulatorObj = new ATDeviceCustomEmulatorObj;

	compiler.SetUnsafeCallsAllowed(mbAllowUnsafe);

	compiler.DefineSpecialVariable("address");
	compiler.DefineSpecialVariable("value");
	compiler.DefineThreadVariable("timestamp");
	compiler.DefineThreadVariable("device");
	compiler.DefineThreadVariable("command");
	compiler.DefineThreadVariable("aux1");
	compiler.DefineThreadVariable("aux2");
	compiler.DefineThreadVariable("aux");
	compiler.DefineThreadVariable("thread");
	compiler.DefineSpecialObjectVariable("network", &mNetwork);
	compiler.DefineSpecialObjectVariable("sio_frame", &mSIOFrameSegment);
	compiler.DefineSpecialObjectVariable("sio", &mSIO);
	compiler.DefineSpecialObjectVariable("clock", &mClock);
	compiler.DefineSpecialObjectVariable("debug", &mDebug);
	compiler.DefineSpecialObjectVariable("emulator", &*mpEmulatorObj);
	compiler.DefineClass(Console::kVMObjectClass);

	ATVMCompiler::DefineInstanceFn createThreadFn(
		[this](ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers) -> bool {
			if (initializers)
				return compiler.ReportError("Thread object does not take initializers");

			vdautoptr<ScriptThread> thread { new ScriptThread };

			thread->mpParent = this;
			thread->mVMThread.Init(mVMDomain);
			thread->mVMThread.mpDebugName = mVMDomain.StoreString(name);

			bool success = compiler.DefineObjectVariable(name, thread.get());

			mScriptThreads.push_back(thread);
			thread.release();

			return success;
		}
	);

	compiler.DefineClass(ScriptThread::kVMObjectClass, &createThreadFn);

	ATVMCompiler::DefineInstanceFn createConditionFn(
		[this](ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers) -> bool {
			return OnDefineCondition(compiler, name, initializers);
		}
	);

	compiler.DefineClass(ATDeviceCustomScriptCondition::kVMObjectClass, &createConditionFn);

	ATVMCompiler::DefineInstanceFn defineSegmentFn(
		[this](ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers) -> bool {
			return OnDefineSegment(compiler, name, initializers);
		}
	);

	compiler.DefineClass(Segment::kVMObjectClass, &defineSegmentFn);

	ATVMCompiler::DefineInstanceFn defineMemoryLayerFn(
		[this](ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers) -> bool {
			return OnDefineMemoryLayer(compiler, name, initializers);
		}
	);

	compiler.DefineClass(MemoryLayer::kVMObjectClass, &defineMemoryLayerFn);

	ATVMCompiler::DefineInstanceFn defineSIODeviceFn(
		[this](ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers) -> bool {
			return OnDefineSIODevice(compiler, name, initializers);
		}
	);

	compiler.DefineClass(SIODevice::kVMObjectClass, &defineSIODeviceFn);

	ATVMCompiler::DefineInstanceFn defineControllerPortFn(
		[this](ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers) -> bool {
			return OnDefineControllerPort(compiler, name, initializers);
		}
	);

	compiler.DefineClass(ControllerPort::kVMObjectClass, &defineControllerPortFn);

	ATVMCompiler::DefineInstanceFn definePBIDeviceFn(
		[this](ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers) -> bool {
			return OnDefinePBIDevice(compiler, name, initializers);
		}
	);

	compiler.DefineClass(PBIDevice::kVMObjectClass, &definePBIDeviceFn);

	ATVMCompiler::DefineInstanceFn defineImageFn(
		[this](ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers) -> bool {
			return OnDefineImage(compiler, name, initializers);
		}
	);

	compiler.DefineClass(Image::kVMObjectClass, &defineImageFn);

	ATVMCompiler::DefineInstanceFn defineVideoOutputFn(
		[this](ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers) -> bool {
			return OnDefineVideoOutput(compiler, name, initializers);
		}
	);

	compiler.DefineClass(VideoOutput::kVMObjectClass, &defineVideoOutputFn);

	ATVMCompiler::DefineInstanceFn defineSoundFn(
		[this](ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers) -> bool {
			return OnDefineSound(compiler, name, initializers);
		}
	);

	compiler.DefineClass(ATDeviceCustomSound::kVMObjectClass, &defineSoundFn);

	ATVMCompiler::DefineInstanceFn defineSoundParamsFn(
		[this](ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers) -> bool {
			return OnDefineSoundParams(compiler, name, initializers);
		}
	);

	compiler.DefineClass(ATDeviceCustomSoundParams::kVMObjectClass, &defineSoundParamsFn);

	ATVMCompiler::DefineInstanceFn defineSerialPortFn(
		[this](ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers) -> bool {
			return OnDefineSerialPort(compiler, name, initializers);
		}
	);

	compiler.DefineClass(ATDeviceCustomSerialPort::kVMObjectClass, &defineSerialPortFn);

	ATVMCompiler::DefineInstanceFn definePrinterPortFn(
		[this](ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers) -> bool {
			return OnDefinePrinterPort(compiler, name, initializers);
		}
	);

	compiler.DefineClass(ATDeviceCustomPrinterPort::kVMObjectClass, &definePrinterPortFn);

	compiler.SetBindEventHandler(
		[this](ATVMCompiler& compiler, const char *eventName, const ATVMScriptFragment& scriptFragment) -> bool {
			const ATVMFunction *func = compiler.DeferCompile(kATVMTypeVoid, scriptFragment);
			if (!func)
				return false;

			static constexpr struct {
				const ATVMFunction *ATDeviceCustom::*const mpEvent;
				const char *mpEventName;
			} kEvents[] = {
				{ &ATDeviceCustom::mpScriptEventInit				, "init"					},
				{ &ATDeviceCustom::mpScriptEventColdReset			, "cold_reset"				},
				{ &ATDeviceCustom::mpScriptEventWarmReset			, "warm_reset"				},
				{ &ATDeviceCustom::mpScriptEventVBLANK				, "vblank"					},
				{ &ATDeviceCustom::mpScriptEventSIOCommandChanged	, "sio_command_changed"		},
				{ &ATDeviceCustom::mpScriptEventSIOMotorChanged		, "sio_motor_changed"		},
				{ &ATDeviceCustom::mpScriptEventSIOReceivedByte		, "sio_received_byte"		},
				{ &ATDeviceCustom::mpScriptEventPBISelect			, "pbi_select"				},
				{ &ATDeviceCustom::mpScriptEventPBIDeselect			, "pbi_deselect"			},
				{ &ATDeviceCustom::mpScriptEventNetworkInterrupt	, "network_interrupt"		},
			};

			for(const auto& eventDef : kEvents) {
				if (!strcmp(eventName, eventDef.mpEventName)) {
					if (this->*eventDef.mpEvent)
						return compiler.ReportErrorF("Event '%s' already bound", eventName);

					this->*eventDef.mpEvent = func;
					return true;
				}
			}

			return compiler.ReportErrorF("Unknown event '%s'", eventName);
		}
	);

	compiler.SetOptionHandler([this](ATVMCompiler& compiler, const char *name, const ATVMDataValue& value) {
		return OnSetOption(compiler, name, value);
	});

	mpCompiler = &compiler;
	mNetworkPort = 0;

	if (!compiler.CompileFile((const char *)buf, len) || !compiler.CompileDeferred()) {
		VDStringA s;

		auto [line, column] = mpCompiler->GetErrorLinePos();
		s.sprintf("%ls(%u,%u): %s", mConfigPath.c_str(), line, column, mpCompiler->GetError());
		throw MyError("%s", s.c_str());
	}

	mVMThread.Init(mVMDomain);
	mVMThreadSIO.Init(mVMDomain);
	mVMThreadScriptInterrupt.Init(mVMDomain);

	if (!mpSIODeviceTable && compiler.IsSpecialVariableReferenced("sio"))
		mpSIODeviceTable = mConfigAllocator.Allocate<SIODeviceTable>();
}

bool ATDeviceCustom::OnSetOption(ATVMCompiler& compiler, const char *name, const ATVMDataValue& value) {
	if (!strcmp(name, "name")) {
		if (!value.IsString())
			throw ATVMCompileError(value, "Option 'name' must be a string");

		mDeviceName = VDTextU8ToW(VDStringSpanA(value.AsString()));
	} else if (!strcmp(name, "network")) {
		if (!value.IsDataObject())
			throw ATVMCompileError(value, "Option 'network' must be a data object");

		MemberParser networkMembers(value);
		const ATVMDataValue& portNode = networkMembers.Required("port");
		uint32 port = ParseRequiredUint32(portNode);

		if (port < 1024 || port >= 49151)
			throw ATVMCompileError(portNode, "Invalid network port (not in 1024-49150).");

		mNetworkPort = port;

		networkMembers.AssertNoUnused();
	} else {
		throw ATVMCompileError::Format(value, "Unknown option '%s'", name);
	}

	return true;
}

bool ATDeviceCustom::OnDefineCondition(ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers) {
	if (initializers)
		return compiler.ReportError("Condition object does not take initializers");

	mVMObjects.emplace_back();

	ATDeviceCustomScriptCondition *condition = new ATDeviceCustomScriptCondition;
	condition->mpParent = this;
	mVMObjects.back() = condition;

	if (!compiler.DefineObjectVariable(name, condition))
		return false;

	return true;
}

bool ATDeviceCustom::OnDefineSegment(ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers) {
	if (!initializers)
		return compiler.ReportError("Segment object must be initialized");

	if (!initializers->IsDataObject())
		return compiler.ReportError("Segment initializer is not an object.");

	mSegments.push_back(nullptr);
	Segment& seg = *mConfigAllocator.Allocate<Segment>();
	mSegments.back() = &seg;

	size_t nameLen = strlen(name);
	char *name2 = mConfigAllocator.AllocateArray<char>(nameLen + 1);
	memcpy(name2, name, nameLen + 1);
	seg.mpName = name2;

	MemberParser segMembers(*initializers);

	uint32 size = segMembers.RequiredUint32("size");
	if (size > kMaxTotalSegmentData)
		return compiler.ReportError("Total segment size is too large.");

	seg.mpData = (uint8 *)mConfigAllocator.Allocate(size, 4);
	if (!seg.mpData)
		throw MyMemoryError();

	seg.mSize = size;
	seg.mbNonVolatile = false;
	seg.mbMappable = true;

	if (const ATVMDataValue *sourceNode = segMembers.Optional("source")) {
		vdrefptr<ATVFSFileView> sourceView;		
		LoadDependency(*sourceNode, ~sourceView);

		const ATVMDataValue *offsetNode = segMembers.Optional("source_offset");
		const uint32 offset = offsetNode ? ParseRequiredUint32(*offsetNode) : 0;
		
		IVDRandomAccessStream& stream = sourceView->GetStream();
		seg.mpInitData = (uint8 *)mConfigAllocator.Allocate(size, 4);
		seg.mInitSize = size;

		if (offset)
			stream.Seek(offset);

		stream.Read(seg.mpInitData, seg.mInitSize);
	} else if (const ATVMDataValue *initPatternNode = segMembers.Optional("init_pattern")) {
		auto pattern = ParseBlob(*initPatternNode);

		if (pattern.size() > seg.mSize)
			return compiler.ReportError("Init pattern is larger than the segment size.");

		seg.mpInitData = pattern.data();
		seg.mInitSize = (uint32)pattern.size();
	} else if (const ATVMDataValue *initPatternNode = segMembers.Optional("init_pattern_word")) {
		auto pattern = ParseBlob16(*initPatternNode);

		if (pattern.size() > seg.mSize)
			return compiler.ReportError("Init pattern is larger than the segment size.");

		seg.mpInitData = pattern.data();
		seg.mInitSize = (uint32)pattern.size();
	} else {
		seg.mInitSize = 1;
	}

	if (const ATVMDataValue *persistenceNode = segMembers.Optional("persistence")) {
		const VDStringSpanA mode(ParseRequiredString(*persistenceNode));

		if (mode == "nonvolatile") {
			seg.mbNonVolatile = true;
		} else if (mode != "volatile")
			return compiler.ReportError("Unknown segment persistence mode.");
	}

	segMembers.AssertNoUnused();

	if (!mpCompiler->DefineObjectVariable(name, &seg, Segment::kVMObjectClass))
		return false;

	return true;
}

bool ATDeviceCustom::OnDefineMemoryLayer(ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers) {
	if (!initializers)
		throw ATVMCompileError("Memory layer must be initialized with a data object.");
		
	if (!initializers->IsDataObject())
		throw ATVMCompileError(*initializers, "Memory layer must be initialized with a data object.");

	MemberParser mlMembers(*initializers);

	const uint32 address = mlMembers.RequiredUint32("address");
	const uint32 size = mlMembers.RequiredUint32("size");

	if (address >= 0x10000 || (address & 0xFF))
		throw ATVMCompileError(*initializers, "Memory layer address is invalid.");

	if (size == 0 || 0x10000 - address < size || (size & 0xFF))
		throw ATVMCompileError(*initializers, "Memory layer size is invalid.");

	MemoryLayer& ml = *mConfigAllocator.Allocate<MemoryLayer>();
	if (!compiler.DefineObjectVariable(name, &ml))
		return false;

	mMemoryLayers.push_back(&ml);

	size_t varNameLen = strlen(name);
	char *persistentVarName = mConfigAllocator.AllocateArray<char>(varNameLen + 1);
	memcpy(persistentVarName, name, varNameLen + 1);
	ml.mpName = persistentVarName;

	ml.mpParent = this;
	ml.mAddressBase = address;
	ml.mSize = size;

	bool autoEnableSet = false;

	if (const ATVMDataValue *autoEnableRef = mlMembers.Optional("auto_enable")) {
		const VDStringSpanA autoEnableStr(ParseRequiredString(*autoEnableRef));

		if (autoEnableStr == "pbi")
			ml.mbAutoPBI = true;
		else if (autoEnableStr == "osrom")
			ml.mbAutoOS = true;
		else if (autoEnableStr == "basicrom")
			ml.mbAutoBASIC = true;
		else if (autoEnableStr == "selftestrom")
			ml.mbAutoSelfTest = true;
		else
			throw ATVMCompileError(*autoEnableRef, "Unsupported memory layer auto-enable mode");

		autoEnableSet = true;
	}

	if (const ATVMDataValue *pbiModeRef = mlMembers.Optional("auto_pbi")) {
		if (ParseBool(*pbiModeRef)) {
			ml.mbAutoPBI = true;

			if (!mPBIDeviceId)
				throw ATVMCompileError(*pbiModeRef, "pbi_device must be declared for auto_pbi layers");

			if (autoEnableSet)
				throw ATVMCompileError(*pbiModeRef, "Cannot use both auto_enable and auto_pbi");

			autoEnableSet = true;
		}
	}

	int pri = kATMemoryPri_Cartridge1;

	if (ml.mbAutoPBI)
		pri = kATMemoryPri_PBI;

	// check for priority overload
	if (const ATVMDataValue *priorityRef = mlMembers.Optional("priority")) {
		const char *priorityStr = ParseRequiredString(*priorityRef);

		if (!strcmp(priorityStr, "pbi"))
			pri = kATMemoryPri_PBI;
		else if (!strcmp(priorityStr, "extsel"))
			pri = kATMemoryPri_Extsel;
		else if (!strcmp(priorityStr, "hwoverlay"))
			pri = kATMemoryPri_HardwareOverlay;
		else
			throw ATVMCompileError(*priorityRef, "Unsupported memory layer priority mode");
	}

	// see if we have a fixed mapping or control mapping
	const ATVMDataValue *fixedMappingNode = mlMembers.Optional("fixed_mapping");

	if (!fixedMappingNode)
		fixedMappingNode = mlMembers.Optional("segment");

	if (fixedMappingNode) {
		MemberParser segmentMembers(*fixedMappingNode);
		const ATVMDataValue& sourceNode = segmentMembers.Required("source");

		if (!sourceNode.IsRuntimeObject<Segment>())
			throw ATVMCompileError(sourceNode, "'source' must be a Segment object");

		Segment *srcSegment = sourceNode.AsRuntimeObject<Segment>(mVMDomain);

		// parse byte offset within segment
		const ATVMDataValue *offsetNode = segmentMembers.Optional("offset");
		uint32 offset = offsetNode ? ParseRequiredUint32(*offsetNode) : 0;

		// validate that it is page aligned and within the source segment
		if (offset & 0xFF)
			throw ATVMCompileError(*offsetNode, "Source segment offset is invalid.");

		if (offset >= srcSegment->mSize || srcSegment->mSize - offset < size)
			throw ATVMCompileError::Format(sourceNode, "Reference to %u bytes at offset %u extends outside of source segment of %u bytes.", size, offset, srcSegment->mSize);

		ml.mpSegment = srcSegment;
		ml.mMaxOffset = srcSegment->mSize - size;

		// parse mode
		const ATVMDataValue& modeNode = segmentMembers.Required("mode");
		const char *mode = ParseRequiredString(modeNode);
		ATMemoryAccessMode accessMode;
		bool isReadOnly = false;
		bool isWriteThrough = false;

		if (!strcmp(mode, "r")) {
			accessMode = kATMemoryAccessMode_AR;
		} else if (!strcmp(mode, "ro")) {
			accessMode = kATMemoryAccessMode_ARW;
			isReadOnly = true;
		} else if (!strcmp(mode, "w")) {
			accessMode = kATMemoryAccessMode_W;
		} else if (!strcmp(mode, "rw")) {
			accessMode = kATMemoryAccessMode_ARW;
		} else if (!strcmp(mode, "wt")) {
			accessMode = kATMemoryAccessMode_W;
			isWriteThrough = true;
		} else
			throw ATVMCompileError(*offsetNode, "Invalid access mode.");

		ml.mEnabledModes = accessMode;
		ml.mbIsWriteThrough = isWriteThrough;
		ml.mSegmentOffset = offset;

		if (isWriteThrough) {
			ATMemoryHandlerTable handlers {};
			handlers.mbPassAnticReads = true;
			handlers.mbPassReads = true;
			handlers.mbPassWrites = true;
			handlers.mpThis = &ml;
			handlers.mpReadHandler = [](void *thisptr, uint32 addr) -> sint32 {
				MemoryLayer& ml = *(MemoryLayer *)thisptr;

				return ml.mpSegment->mpData[ml.mSegmentOffset + (addr - ml.mAddressBase)];
			};
			handlers.mpDebugReadHandler = handlers.mpReadHandler;

			handlers.mpWriteHandler = [](void *thisptr, uint32 addr, uint8 value) -> bool {
				MemoryLayer& ml = *(MemoryLayer *)thisptr;

				ml.mpSegment->mpData[ml.mSegmentOffset + (addr - ml.mAddressBase)] = value;

				return false;
			};


			ml.mpPhysLayer = mpMemMan->CreateLayer(pri, handlers, address >> 8, size >> 8); 
		} else {
			ml.mpPhysLayer = mpMemMan->CreateLayer(pri, srcSegment->mpData + offset, address >> 8, size >> 8, isReadOnly); 
		}

		UpdateLayerModes(ml);

		segmentMembers.AssertNoUnused();
	} else if (const ATVMDataValue *controlNode = mlMembers.Optional("control")) {
		// parse address actions
		if (!controlNode->IsArray())
			throw ATVMCompileError(*controlNode, "Control item was not an object.");

		ml.mpReadBindings = mConfigAllocator.AllocateArray<AddressBinding>(size);
		ml.mpWriteBindings = mConfigAllocator.AllocateArray<AddressBinding>(size);

		uint32 memoryLayerMode = 0;

		for(const ATVMDataValue& bindingNode : vdvector_view(controlNode->mpArrayElements, controlNode->mLength)) {
			if (!bindingNode.IsDataObject())
				throw ATVMCompileError(bindingNode, "Binding item was not an object.");

			MemberParser bindingMembers(bindingNode);

			// parse address attribute and validate it
			const ATVMDataValue& bindingAddressNode = bindingMembers.Required("address");
			uint32 actionAddress = ParseRequiredUint32(bindingAddressNode);

			// parse optional size attribute
			const ATVMDataValue *sizeNode = bindingMembers.Optional("size");
			uint32 actionSize = sizeNode ? ParseRequiredUint32(*sizeNode) : 1;

			// validate that address action is within layer
			if (actionAddress < address || actionAddress >= address + size || (address + size) - actionAddress < actionSize)
				throw ATVMCompileError(bindingAddressNode, "Binding address is outside of the memory layer.");

			// parse mode
			const ATVMDataValue& bindingActionModeNode = bindingMembers.Required("mode");
			const char *actionMode = ParseRequiredString(bindingActionModeNode);
			bool actionRead = false;
			bool actionWrite = false;

			if (!strcmp(actionMode, "r"))
				actionRead = true;
			else if (!strcmp(actionMode, "w"))
				actionWrite = true;
			else if (!strcmp(actionMode, "rw"))
				actionRead = actionWrite = true;
			else
				throw ATVMCompileError(bindingActionModeNode, "Invalid binding mode.");

			// validate that actions are not already assigned
			vdvector_view<AddressBinding> actionReadBindings;
			vdvector_view<AddressBinding> actionWriteBindings;

			if (actionRead) {
				actionReadBindings = { &ml.mpReadBindings[actionAddress - address], actionSize };

				for(const auto& rb : actionReadBindings) {
					if (rb.mAction != AddressAction::None)
						throw ATVMCompileError(bindingNode, "Address conflict between two read bindings in the same layer.");
				}

				memoryLayerMode |= kATMemoryAccessMode_AR;
			}

			if (actionWrite) {
				actionWriteBindings = { &ml.mpWriteBindings[actionAddress - address], actionSize };

				for(const auto& wb : actionWriteBindings) {
					if (wb.mAction != AddressAction::None)
						throw ATVMCompileError(bindingNode, "Address conflict between two write bindings in the same layer.");
				}

				memoryLayerMode |= kATMemoryAccessMode_W;
			}

			if (const ATVMDataValue *dataNode = bindingMembers.Optional("data")) {
				const vdvector_view<uint8> pattern = ParseBlob(*dataNode);

				if (!actionRead || actionWrite)
					throw ATVMCompileError(*dataNode, "Data bindings can only be read-only.");

				if (pattern.size() == 1) {
					for(auto& rb : actionReadBindings) {
						rb.mAction = AddressAction::ConstantData;
						rb.mByteData = pattern[0];
					}
				} else {
					if (pattern.size() != actionSize)
						throw ATVMCompileError(*dataNode, "Data must either be a single byte or the same size as the memory layer.");

					for(uint32 i = 0; i < actionSize; ++i) {
						auto& arb = actionReadBindings[i];

						arb.mAction = AddressAction::ConstantData;
						arb.mByteData = pattern[i];
					}
				}
			} else if (const ATVMDataValue *actionNode = bindingMembers.Optional("action")) {
				const char *actionStr = actionNode->mpStrValue;

				if (!strcmp(actionStr, "block")) {
					if (actionRead || !actionWrite)
						throw ATVMCompileError(*actionNode, "Block bindings can only be write-only.");

					for(auto& wb : actionWriteBindings)
						wb.mAction = AddressAction::Block;
				} else if (!strcmp(actionStr, "network")) {
					if (mNetworkPort == 0)
						throw ATVMCompileError(*actionNode, "Cannot use a network binding with no network connection set up.");

					for(auto& rb : actionReadBindings)
						rb.mAction = AddressAction::Network;

					for(auto& wb : actionWriteBindings)
						wb.mAction = AddressAction::Network;
				} else
					throw ATVMCompileError(*actionNode, "Unknown action type.");
			} else if (const ATVMDataValue *variableNode = bindingMembers.Optional("variable")) {
				const char *varNameStr = ParseRequiredString(*variableNode);

				const ATVMTypeInfo *varTypeInfo = mpCompiler->GetVariable(varNameStr);

				if (!varTypeInfo)
					throw ATVMCompileError(*variableNode, "Variable not defined.");

				if (varTypeInfo->mClass != ATVMTypeClass::IntLValueVariable)
					throw ATVMCompileError(*variableNode, "Variable must be of integer type for address binding.");

				for(auto& rb : actionReadBindings) {
					rb.mAction = AddressAction::Variable;
					rb.mVariableIndex = varTypeInfo->mIndex;
				}

				for(auto& wb : actionWriteBindings) {
					wb.mAction = AddressAction::Variable;
					wb.mVariableIndex = varTypeInfo->mIndex;
				}
			} else if (const ATVMDataValue *scriptNode = bindingMembers.Optional("script")) {
				const ATVMFunction *func = ParseScript(actionRead ? kATVMTypeInt : kATVMTypeVoid, *scriptNode, ATVMFunctionFlags::None, ATVMConditionalMask::NonDebugReadOnly);

				const ATVMFunction *debugFunc = nullptr;
				if (!actionReadBindings.empty()) {
					if (const ATVMDataValue *debugScriptNode = bindingMembers.Optional("debug_script"))
						debugFunc = ParseScript(kATVMTypeInt, *debugScriptNode, ATVMFunctionFlags::None, ATVMConditionalMask::None);
					else
						debugFunc = ParseScript(actionRead ? kATVMTypeInt : kATVMTypeVoid, *scriptNode, ATVMFunctionFlags::None, ATVMConditionalMask::DebugReadOnly);
				}

				uint16 funcCode = (uint16)mScriptFunctions.size();
				mScriptFunctions.push_back(func);

				uint16 debugFuncCode = funcCode;
				if (debugFunc) {
					debugFuncCode = (uint16)mScriptFunctions.size();
					mScriptFunctions.push_back(debugFunc);
				}


				for(auto& rb : actionReadBindings) {
					rb.mAction = AddressAction::Script;
					rb.mScriptFunctions[0] = funcCode;
					rb.mScriptFunctions[1] = debugFuncCode;
				}

				for(auto& wb : actionWriteBindings) {
					wb.mAction = AddressAction::Script;
					wb.mScriptFunctions[0] = funcCode;
				}
			} else if (const ATVMDataValue *copyNode = bindingMembers.Optional("copy_from")) {
				const uint32 srcAddr = ParseRequiredUint32(*copyNode);

				if (srcAddr < address || (srcAddr - address) >= size || size - (srcAddr - address) < actionSize)
					throw ATVMCompileError(bindingNode, "Binding copy source is outside of memory layer.");

				const auto *rsrc = &ml.mpReadBindings[srcAddr - address];
				for(auto& rb : actionReadBindings)
					rb = *rsrc++;

				const auto *wsrc = &ml.mpWriteBindings[srcAddr - address];
				for(auto& wb : actionWriteBindings)
					wb = *wsrc++;
			} else
				throw ATVMCompileError(bindingNode, "No binding type specified; expected one of: data, action, variable, script, or copy_from.");

			bindingMembers.AssertNoUnused();
		}

		if (!memoryLayerMode)
			throw ATVMCompileError(*controlNode, "No address bindings were specified.");

		// create physical memory layer
		ATMemoryHandlerTable handlers {};
		handlers.mbPassAnticReads = true;
		handlers.mbPassReads = true;
		handlers.mbPassWrites = true;
		handlers.mpThis = &ml;

		handlers.mpDebugReadHandler = [](void *thisptr, uint32 addr) -> sint32 {
			MemoryLayer& ml = *(MemoryLayer *)thisptr;

			return ml.mpParent->ReadControl<true>(ml, addr);
		};

		handlers.mpReadHandler = [](void *thisptr, uint32 addr) -> sint32 {
			MemoryLayer& ml = *(MemoryLayer *)thisptr;

			return ml.mpParent->ReadControl<false>(ml, addr);
		};

		handlers.mpWriteHandler = [](void *thisptr, uint32 addr, uint8 value) -> bool {
			MemoryLayer& ml = *(MemoryLayer *)thisptr;

			return ml.mpParent->WriteControl(ml, addr, value);
		};

		ml.mpPhysLayer = mpMemMan->CreateLayer(pri, handlers, address >> 8, size >> 8);

		ml.mEnabledModes = memoryLayerMode;

		UpdateLayerModes(ml);
	} else
		throw ATVMCompileError(*initializers, "Memory layer must have a 'control' or 'segment' member.");

	if (const ATVMDataValue *cartModeRef = mlMembers.Optional("cart_mode")) {
		const VDStringSpanA cartMode(ParseRequiredString(*cartModeRef));

		if (cartMode == "left") {
			ml.mbAutoRD5 = true;
			ml.mbRD5Active = true;
		} else if (cartMode == "right")
			ml.mbAutoRD4 = true;
		else if (cartMode == "cctl")
			ml.mbAutoCCTL = true;
		else if (cartMode == "auto") {
			if (address >= 0xA000 && address < 0xC000 && 0xC000 - address <= size) {
				ml.mbAutoRD5 = true;
				ml.mbRD5Active = true;
			} else if (address >= 0x8000 && address < 0xA000 && 0xA000 - address <= size) {
				ml.mbAutoRD4 = true;
			} else if (address >= 0xD500 && address < 0xD600 && 0xD600 - address <= size) {
				ml.mbAutoCCTL = true;
			} else
				throw ATVMCompileError(*cartModeRef, "Cannot use 'auto' mode as memory layer address range does not map to a cartridge region.");
		} else
			throw ATVMCompileError(*cartModeRef, "Invalid cartridge mode.");
	}

	VDStringA layerName(mlMembers.RequiredString("name"));

	// Currently the layer name is used by the debugger, which does not expect localized
	// names -- so filter out loc chars for now. We also need to copy the name as the memory
	// manager expects a static name.
	size_t nameLen = layerName.size();
	char *name8 = (char *)mConfigAllocator.Allocate(nameLen + 1);

	for(size_t i = 0; i < nameLen; ++i) {
		char c = layerName[i];

		if (c < 0x20 || c >= 0x7F)
			c = '_';

		name8[i] = c;
	}

	name8[nameLen] = 0;

	if (ml.mpPhysLayer)
		mpMemMan->SetLayerName(ml.mpPhysLayer, name8);

	mlMembers.AssertNoUnused();
	return true;
}

bool ATDeviceCustom::OnDefineSIODevice(ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers) {
	if (!mpSIODeviceTable)
		mpSIODeviceTable = mConfigAllocator.Allocate<SIODeviceTable>();

	if (!initializers)
		throw ATVMCompileError("SIODevice object must be initialized.");
		
	if (!initializers->IsDataObject())
		throw ATVMCompileError(*initializers, "SIODevice initializer must be an object.");

	MemberParser devMembers(*initializers);

	uint32 deviceId = devMembers.RequiredUint32("device_id");
	if (deviceId >= 0x100)
		throw ATVMCompileError(*initializers, "Invalid SIO device ID.");

	const ATVMDataValue *deviceCountNode = devMembers.Optional("device_count");
	uint32 deviceIdCount = deviceCountNode ? ParseRequiredUint32(*deviceCountNode) : 1;

	if (0x100 - deviceId < deviceIdCount)
		throw ATVMCompileError(*initializers, "Invalid SIO device ID range.");

	SIODevice *device = mConfigAllocator.Allocate<SIODevice>();

	if (!mpCompiler->DefineObjectVariable(name, device))
		return false;

	device->mbAllowAccel = true;

	for(uint32 i=0; i<deviceIdCount; ++i) {
		if (mpSIODeviceTable->mpDevices[deviceId + i])
			throw ATVMCompileError(*initializers, "SIO device already defined.");

		mpSIODeviceTable->mpDevices[deviceId + i] = device;
	}

	const auto *devAllowAccelNode = devMembers.Optional("allow_accel");
	if (devAllowAccelNode)
		device->mbAllowAccel = ParseBool(*devAllowAccelNode);

	const auto& commandsNode = devMembers.Required("commands");
	if (!commandsNode.IsArray())
		throw ATVMCompileError(*initializers, "SIO device requires commands array.");

	for(const ATVMDataValue& commandNode : commandsNode.AsArray()) {
		MemberParser commandMembers(commandNode);
		const auto& commandIdNode = commandMembers.Required("id");
		bool isCopy = false;
		uint32 commandId = 0;

		SIOCommand *cmd;
		if (const ATVMDataValue *copyFromNode = commandMembers.Optional("copy_from")) {
			uint32 srcId = ParseRequiredUint32(*copyFromNode);

			if (srcId >= 0x100)
				throw ATVMCompileError(*copyFromNode, "Invalid source command ID.");

			cmd = device->mpCommands[srcId];
			if (!cmd)
				throw ATVMCompileError(*copyFromNode, "Source command is not defined.");

			isCopy = true;
		} else {
			cmd = mConfigAllocator.Allocate<SIOCommand>();
			cmd->mbAllowAccel = true;
		}

		const ATVMDataValue *cmdAllowAccelNode = commandMembers.Optional("allow_accel");
		if (cmdAllowAccelNode)
			cmd->mbAllowAccel = ParseBool(*cmdAllowAccelNode);

		if (commandIdNode.IsString() && !strcmp(commandIdNode.AsString(), "default")) {
			for(SIOCommand*& cmdEntry : device->mpCommands) {
				if (!cmdEntry)
					cmdEntry = cmd;
			}
		} else {
			commandId = ParseRequiredUint32(commandIdNode);

			if (commandId >= 0x100)
				throw ATVMCompileError(commandIdNode, "Invalid SIO command ID.");

			if (device->mpCommands[commandId])
				throw ATVMCompileError(commandIdNode, "Conflicting command ID in SIO device command list.");

			device->mpCommands[commandId] = cmd;
		}

		if (isCopy)
			continue;

		if (const ATVMDataValue *autoTransferNode = commandMembers.Optional("auto_transfer")) {
			if (autoTransferNode->IsArray()) {
				auto blob = ParseBlob(*autoTransferNode);

				cmd->mbAutoTransferWrite = false;
				cmd->mpAutoTransferBlob = blob.data();
				cmd->mAutoTransferLength = (uint32)blob.size();
			} else {
				MemberParser autoTransferMembers(*autoTransferNode);

				const auto& segmentNode = autoTransferMembers.Required("segment");

				if (!segmentNode.IsRuntimeObject<Segment>())
					throw ATVMCompileError(segmentNode, "'segment' member must be set to an object of type Segment");

				Segment *segment = segmentNode.AsRuntimeObject<Segment>(mVMDomain);

				const VDStringSpanA mode(ParseRequiredString(autoTransferMembers.Required("mode")));
				if (mode == "read") {
					cmd->mbAutoTransferWrite = false;
				} else if (mode == "write") {
					cmd->mbAutoTransferWrite = true;
				} else
					throw ATVMCompileError(*autoTransferNode, "Unknown auto-transfer mode.");

				const ATVMDataValue *offsetNode = autoTransferMembers.Optional("offset");
				const uint32 offset = offsetNode ? ParseRequiredUint32(*offsetNode) : 0;
				const uint32 length = autoTransferMembers.RequiredUint32("length");

				if (length < 1 || length > 8192)
					throw ATVMCompileError(*autoTransferNode, "Invalid transfer length (must be 1-8192 bytes).");

				if (offset >= segment->mSize || segment->mSize - offset < length)
					throw ATVMCompileError(*autoTransferNode, "Offset/length specifies range outside of segment.");

				cmd->mpAutoTransferSegment = segment;
				cmd->mAutoTransferOffset = offset;
				cmd->mAutoTransferLength = length;
			}
		} else {
			cmd->mpScript = ParseScriptOpt(kATVMTypeVoid, commandMembers.Optional("script"), ATVMFunctionFlags::AsyncSIO | ATVMFunctionFlags::AsyncRawSIO);
		}

		commandMembers.AssertNoUnused();
	}

	return true;
}

bool ATDeviceCustom::OnDefineControllerPort(ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers) {
	if (!initializers)
		throw ATVMCompileError("ControllerPort object must be initialized.");

	const uint32 portIndex = ParseRequiredUint32(*initializers);

	if (portIndex >= 4)
		throw ATVMCompileError(*initializers, "Invalid controller port index.");

	if (mpControllerPorts[portIndex])
		throw ATVMCompileError(*initializers, "Controller port is already bound.");

	ControllerPort *cport = mConfigAllocator.Allocate<ControllerPort>();
	mpControllerPorts[portIndex] = cport;

	if (!mpCompiler->DefineObjectVariable(name, cport))
		return false;

	GetService<IATDevicePortManager>()->AllocControllerPort(portIndex, ~cport->mpControllerPort);
	cport->Init();
	return true;
}

bool ATDeviceCustom::OnDefinePBIDevice(ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers) {
	if (mPBIDeviceId)
		throw ATVMCompileError("PBI device already defined");

	if (!initializers)
		throw ATVMCompileError("PBIDevice object requires initialization");

	MemberParser members(*initializers);

	uint32 deviceId = members.RequiredUint32("device_id");

	if (!deviceId || deviceId >= 0x100 || (deviceId & (deviceId - 1)))
		throw ATVMCompileError(*initializers, "PBI device ID must be a power of two byte value.");

	mPBIDeviceId = (uint8)deviceId;

	const ATVMDataValue *irqValue = members.Optional("has_irq");
	mbPBIDeviceHasIrq = irqValue && ParseBool(*irqValue);

	PBIDevice *dev = mConfigAllocator.Allocate<PBIDevice>(*this);
	if (!compiler.DefineObjectVariable(name, dev))
		return false;

	members.AssertNoUnused();
	return true;
}

bool ATDeviceCustom::OnDefineImage(ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers) {
	if (!initializers)
		throw ATVMCompileError("Image object requires initialization");

	Image *img = new Image;
	mImages.push_back(img);

	const auto validateImageSize = [](uint32 w, uint32 h) {
		if (!w || !h)
			throw ATVMCompileError("Width or height cannot be zero");

		if (w > 65536 || h > 65536)
			throw ATVMCompileError("Image width or height cannot exceed 65536");

		if ((uint64)w * h > 2048 * 2048)
			throw ATVMCompileError("Image exceeds size limit of 4M pixels");
	};

	MemberParser members(*initializers);

	if (const ATVMDataValue *sourceNode = members.Optional("source")) {
		vdrefptr<ATVFSFileView> sourceView;		
		LoadDependency(*sourceNode, ~sourceView);

		auto& stream = sourceView->GetStream();

		uint64 len = (uint64)stream.Length();
		if ((uint64)len >= 256*1024*1024)
			throw ATVMCompileError("Image source file is too large (>256MB)");

		vdblock<char> buf((size_t)len);
		stream.Read(buf.data(), (sint32)buf.size());

		int w, h;
		bool hasAlpha;
		if (!VDDecodePNGHeader(buf.data(), (uint32)buf.size(), w, h, hasAlpha))
			throw ATVMCompileError("Unsupported image file (must be PNG format)");

		validateImageSize((uint32)w, (uint32)h);

		vdautoptr decoder{VDCreateImageDecoderPNG()};
		auto err = decoder->Decode(buf.data(), (uint32)buf.size());
		if (err)
			throw ATVMCompileError("Unable to decode PNG image");

		img->Init(w, h);
		VDPixmapBlt(img->mPixmapBuffer, decoder->GetFrameBuffer());
	} else {
		const uint32 w = members.RequiredUint32("width");
		const uint32 h = members.RequiredUint32("height");

		validateImageSize(w, h);

		img->Init((sint32)w, (sint32)h);
	}

	members.AssertNoUnused();

	if (!compiler.DefineObjectVariable(name, img))
		return false;

	return true;
}

bool ATDeviceCustom::OnDefineVideoOutput(ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers) {
	if (!initializers)
		throw ATVMCompileError("VideoOutput object requires initialization");

	MemberParser members(*initializers);
	const VDStringW displayName = VDTextU8ToW(VDStringSpanA(members.RequiredString("display_name")));

	VideoOutput *vo = new VideoOutput(this, GetService<IATDeviceVideoManager>(), (VDStringA("customdevice_") + name).c_str(), displayName.c_str());
	mVideoOutputs.push_back(vo);

	vo->mpOnComposite = ParseScriptOpt(kATVMTypeVoid, members.Optional("composite"), ATVMFunctionFlags::None);
	vo->mpOnPreCopy = ParseScriptOpt(kATVMTypeVoid, members.Optional("pre_copy"), ATVMFunctionFlags::None);

	members.AssertNoUnused();

	if (!compiler.DefineObjectVariable(name, vo))
		return false;

	return true;
}

bool ATDeviceCustom::OnDefineSound(ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers) {
	if (!initializers)
		throw ATVMCompileError("Sound object requires initialization");

	mVMObjects.emplace_back();

	ATDeviceCustomSound *sound = new ATDeviceCustomSound(*this);
	mVMObjects.back() = sound;

	if (initializers) {
		MemberParser members(*initializers);

		const ATVMDataValue& sourceFileValue = members.RequiredStringValue("source_file");

		vdrefptr<ATVFSFileView> sourceView;		
		LoadDependency(sourceFileValue, ~sourceView);

		IVDRandomAccessStream& stream = sourceView->GetStream();
		vdautoptr<IATAudioReader> audioReader(ATCreateAudioReaderDetect(stream));

		if (!audioReader)
			throw ATVMCompileError(sourceFileValue, "Source audio file has unsupported format");

		vdfastvector<sint16> buffer;
		size_t n = 0;

		for(;;) {
			buffer.resize(n + 16384);

			uint32 actual = audioReader->ReadStereo16(buffer.data() + n, 8192);
			if (!actual)
				break;

			n += actual * 2;
		}

		// downmix to mono
		size_t n2 = n >> 1;
		for(size_t i = 0; i < n2; ++i)
			buffer[i] = (sint16)((buffer[i*2+0] + buffer[i*2+1] + 1) >> 1);

		sound->SetSoundData(vdspan(buffer).first(n2), (float)audioReader->GetFormatInfo().mSamplesPerSec);

		members.AssertNoUnused();
	}

	if (!compiler.DefineObjectVariable(name, sound))
		return false;

	return true;
}

bool ATDeviceCustom::OnDefineSoundParams(ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers) {
	mVMObjects.emplace_back();

	ATDeviceCustomSoundParams *soundParams = new ATDeviceCustomSoundParams;
	mVMObjects.back() = soundParams;

	if (initializers) {
		MemberParser members(*initializers);

		const auto panVal = members.OptionalSint32("pan");
		if (panVal.has_value()) {
			if (*panVal < -100 || *panVal > 100)
				throw ATVMCompileError(*members.mpLastMemberValue, "Sound panning value must be in range [-100, +100]");

			soundParams->mPan = (float)*panVal / 100.0f;
		}

		const auto volumeVal = members.OptionalSint32("volume");
		if (volumeVal.has_value()) {
			if (*volumeVal < 0 || *volumeVal > 100)
				throw ATVMCompileError(*members.mpLastMemberValue, "Sound volume value must be in range [0, 100]");

			soundParams->mVolume = (float)*volumeVal / 100.0f;
		}

		const auto rateVal = members.OptionalSint32("rate");
		if (rateVal.has_value()) {
			if (*rateVal < 10 || *rateVal > 1000)
				throw ATVMCompileError(*members.mpLastMemberValue, "Sound rate value must be in range [10, 1000]");

			soundParams->mRate = (float)*rateVal / 100.0f;
		}

		const auto loopVal = members.OptionalBool("looping");
		if (loopVal.has_value())
			soundParams->mbLooping = loopVal.value();

		members.AssertNoUnused();
	}

	if (!compiler.DefineObjectVariable(name, soundParams))
		return false;

	return true;
}

bool ATDeviceCustom::OnDefineSerialPort(ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers) {
	if (mbHasSerialPort)
		throw ATVMCompileError("Only one serial port can be defined");

	mbHasSerialPort = true;

	mVMObjects.emplace_back();

	ATDeviceCustomSerialPort *serialPort = new ATDeviceCustomSerialPort(*this);
	mVMObjects.back() = serialPort;

	if (initializers) {
		MemberParser members(*initializers);

		members.AssertNoUnused();
	}

	if (!compiler.DefineObjectVariable(name, serialPort))
		return false;

	return true;
}

bool ATDeviceCustom::OnDefinePrinterPort(ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers) {
	if (mbHasPrinterPort)
		throw ATVMCompileError("Only one printer port can be defined");

	mbHasPrinterPort = true;

	mVMObjects.emplace_back();

	ATDeviceCustomPrinterPort *serialPort = new ATDeviceCustomPrinterPort(*this);
	mVMObjects.back() = serialPort;

	if (initializers) {
		MemberParser members(*initializers);

		members.AssertNoUnused();
	}

	if (!compiler.DefineObjectVariable(name, serialPort))
		return false;

	return true;
}

const ATVMFunction *ATDeviceCustom::ParseScriptOpt(const ATVMTypeInfo& returnType, const ATVMDataValue *value, ATVMFunctionFlags flags) {
	if (!value)
		return nullptr;

	return ParseScript(returnType, *value, flags, ATVMConditionalMask::None);
}

const ATVMFunction *ATDeviceCustom::ParseScript(const ATVMTypeInfo& returnType, const ATVMDataValue& value, ATVMFunctionFlags flags, ATVMConditionalMask conditionalMask) {
	if (!value.IsScript())
		throw ATVMCompileError(value, "Expected script function");

	const ATVMFunction *func = mpCompiler->DeferCompile(returnType, *value.mpScript, flags, conditionalMask);

	if (!func)
		throw MyError("%s", mpCompiler->GetError());

	return func;
}

vdvector_view<uint8> ATDeviceCustom::ParseBlob(const ATVMDataValue& value) {
	uint32 size = 1;
	if (value.IsArray()) {
		size = value.mLength;

		if (!size)
			throw ATVMCompileError(value, "Array cannot be empty");
	} else if (value.IsString()) {
		size = strlen(value.mpStrValue);
		if (!size)
			throw ATVMCompileError(value, "String cannot be empty");
	}

	uint8 *data = (uint8 *)mConfigAllocator.Allocate(size, 4);

	if (value.IsArray()) {
		for(uint32 i = 0; i < size; ++i) {
			const ATVMDataValue& elem = value.mpArrayElements[i];

			if (!elem.IsInteger())
				throw ATVMCompileError(elem, "Integer expected");

			if (elem.mIntValue < -128 || elem.mIntValue > 255)
				throw ATVMCompileError(elem, "Value out of range");

			data[i] = (uint8)(elem.mIntValue & 0xFF);
		}
	} else if (value.IsString()) {
		const char *s = value.mpStrValue;

		for(uint32 i = 0; i < size; ++i) {
			char c = s[i];

			if (c < 0x20 || c >= 0x7F)
				throw ATVMCompileError(value, "String data must be ASCII.");

			data[i] = (uint8)c;
		}
	} else
		data[0] = ParseRequiredUint8(value);

	return vdvector_view(data, size);
}

vdvector_view<uint8> ATDeviceCustom::ParseBlob16(const ATVMDataValue& value) {
	if (!value.IsArray())
		throw ATVMCompileError(value, "Array expected");

	uint32 size = value.mLength;
	if (!size)
		throw ATVMCompileError(value, "Array cannot be empty");

	uint8 *data = (uint8 *)mConfigAllocator.Allocate(size * 2, 4);

	for(uint32 i = 0; i < size; ++i) {
		const ATVMDataValue& elem = value.mpArrayElements[i];

		if (!elem.IsInteger())
			throw ATVMCompileError(elem, "Integer expected");

		if (elem.mIntValue < -32768 || elem.mIntValue > 65535)
			throw ATVMCompileError(elem, "Value out of range");

		VDWriteUnalignedLEU16(&data[i*2], elem.mIntValue & 0xFFFF);
	}

	return vdvector_view(data, size*2);
}

uint8 ATDeviceCustom::ParseRequiredUint8(const ATVMDataValue& value) {
	const uint32 v32 = ParseRequiredUint32(value);

	if (v32 != (uint8)v32)
		throw ATVMCompileError(value, "Value out of range.");

	return (uint8)v32;
}

uint32 ATDeviceCustom::ParseRequiredUint32(const ATVMDataValue& value) {
	if (!value.IsInteger() || value.mIntValue < 0)
		throw ATVMCompileError(value, "Value out of range");

	return value.mIntValue;
}

const char *ATDeviceCustom::ParseRequiredString(const ATVMDataValue& value) {
	if (!value.IsString())
		throw ATVMCompileError(value, "Value out of range");

	return value.mpStrValue;
}

bool ATDeviceCustom::ParseBool(const ATVMDataValue& value) {
	if (!value.IsInteger())
		throw ATVMCompileError(value, "Expected boolean value");

	return value.mIntValue != 0;
}

void ATDeviceCustom::LoadDependency(const ATVMDataValue& value, ATVFSFileView **view) {
	const char *name = ParseRequiredString(value);

	const VDStringW nameW = VDTextU8ToW(VDStringSpanA(name));
	VDStringW path = nameW;
	VDStringW basePath;
	VDStringW subPath;
	for(;;) {
		auto pathType = ATParseVFSPath(path.c_str(), basePath, subPath);
		if (pathType == kATVFSProtocol_None)
			break;

		path = basePath;

		if (pathType == kATVFSProtocol_File)
			break;
	}

	if (wcschr(path.c_str(), '/') || wcschr(path.c_str(), '\\') || wcschr(path.c_str(), ':') || wcschr(path.c_str(), '%'))
		throw ATVMCompileError(value, "Source file must be a local filename with no directory component.");

	OpenViewWithTracking(ATMakeVFSPath(mResourceBasePath.c_str(), nameW.c_str()).c_str(), view);
}

void ATDeviceCustom::ClearFileTracking() {
	mTrackedFiles.clear();
}

void ATDeviceCustom::OpenViewWithTracking(const wchar_t *path, ATVFSFileView **view) {
	if (mbHotReload) {
		const wchar_t *path2 = path;

		VDStringW basePath, subPath, prevPath;
		VDFile f;

		for(;;) {
			auto protocol = ATParseVFSPath(path2, basePath, subPath);

			if (protocol == kATVFSProtocol_None)
				break;

			if (protocol == kATVFSProtocol_File) {
				FileTrackingInfo fti {};

				if (f.openNT(path2)) {
					fti.mSize = (uint64)f.size();
					fti.mTimestamp = f.getLastWriteTime().mTicks;
				}

				// store an entry even if the file open fails, as we still want to retry if the file
				// becomes readable
				mTrackedFiles.insert_as(path).first->second = fti;

				// leave the file open so the read lock persists, and maybe we can save some file open overhead
				break;
			}

			prevPath.swap(basePath);
			path2 = prevPath.c_str();
		}
	}

	ATVFSOpenFileView(path, false, view);
}

bool ATDeviceCustom::CheckForTrackedChanges() {
	if (!mbHotReload)
		return false;

	for(const auto& trackedFile : mTrackedFiles) {
		VDFile f;
		uint64 fsize = 0;
		uint64 timestamp = 0;

		if (f.openNT(trackedFile.first.c_str())) {
			fsize = (uint64)f.size();
			timestamp = f.getLastWriteTime().mTicks;
		}

		if (fsize != trackedFile.second.mSize || timestamp != trackedFile.second.mTimestamp)
			return true;
	}

	return false;
}

void ATDeviceCustom::UpdateLayerModes(MemoryLayer& ml) {
	uint8 modes = ml.mEnabledModes;

	if (ml.mbAutoPBI) {
		if (!mbPBIDeviceSelected)
			modes = 0;
	} else if (ml.mbAutoOS) {
		if (!mpMMU->IsKernelROMEnabled())
			modes = 0;
	} else if (ml.mbAutoBASIC) {
		if (!mpMMU->IsBASICROMEnabled())
			modes = 0;
	} else if (ml.mbAutoSelfTest) {
		if (!mpMMU->IsSelfTestROMEnabled())
			modes = 0;
	}

	mpMemMan->SetLayerModes(ml.mpPhysLayer, (ATMemoryAccessMode)modes);

	if (ml.mbAutoRD5) {
		bool rd5Active = (modes != 0);

		if (ml.mbRD5Active != rd5Active) {
			ml.mbRD5Active = rd5Active;
			mpCartPort->OnLeftWindowChanged(mCartId, IsLeftCartActive());
		}
	}
}

sint32 ATDeviceCustom::AddDeviceBus(IATDeviceBus& bus) {
	auto it = std::find(mBusList.begin(), mBusList.end(), nullptr);
	sint32 newBusId = (sint32)(it - mBusList.begin());
	
	if (it == mBusList.end())
		mBusList.emplace_back(&bus);
	else
		*it = &bus;

	return newBusId;
}

void ATDeviceCustom::RemoveDeviceBus(IATDeviceBus& bus) {
	// Check if the bus has any attached devices; if so, we must disconnect them,
	// thus correspondingly need the disconnected bus. To be fully consistent
	// throughout, the disconnected bus needs to be added before the outgoing
	// bus is removed.
	if (&bus != &mDisconnectedBus) {
		vdfastvector<IATDevice *> childDevices;
		bus.GetChildDevices(childDevices);

		if (!childDevices.empty()) {
			if (mDisconnectedBus.GetBusId() < 0)
				mDisconnectedBus.SetBusId(AddDeviceBus(mDisconnectedBus));

			while(!childDevices.empty()) {
				IATDevice *child = childDevices.back();
				childDevices.pop_back();

				bus.RemoveChildDevice(child);
				mDisconnectedBus.AddChildDevice(child);
			}
		}
	}

	auto it = std::find(mBusList.begin(), mBusList.end(), &bus);
	if (it != mBusList.end()) {
		*it = nullptr;

		while(!mBusList.empty() && mBusList.back() == nullptr)
			mBusList.pop_back();
	}
}

void ATDeviceCustom::TryReconnectChildDevices() {
	// disconnect all devices from the disconnect bus
	vdfastvector<IATDevice *> childDevices;
	mDisconnectedBus.GetChildDevices(childDevices);

	for(IATDevice *dev : childDevices) {
		dev->AddRef();

		mDisconnectedBus.RemoveChildDevice(dev);
	}

	// try to add them to all other buses
	for(IATDevice *& dev : childDevices) {
		for(IATDeviceBus *bus : mBusList) {
			if (bus != &mDisconnectedBus) {
				bus->AddChildDevice(dev);

				if (dev->GetParent()) {
					dev->Release();
					dev = nullptr;
					break;
				}
			}
		}
	}

	// readd any remaining devices to the disconnect bus
	for(IATDevice *dev : childDevices) {
		if (dev) {
			mDisconnectedBus.AddChildDevice(dev);
			dev->Release();
		}
	}
}

