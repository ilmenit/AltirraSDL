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

#ifndef f_AT_CUSTOMDEVICE_H
#define f_AT_CUSTOMDEVICE_H

#include <vd2/system/filewatcher.h>
#include <vd2/system/vdstl_vectorview.h>
#include <at/atcore/devicecart.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/deviceindicators.h>
#include <at/atcore/devicesioimpl.h>
#include <at/atcore/devicepbi.h>
#include <at/atcore/scheduler.h>
#include <at/atcore/vfs.h>
#include <at/atvm/vm.h>
#include <at/atvm/compiler.h>
#include "customdevicevmtypes.h"

class ATMemoryLayer;
class ATMemoryManager;
class ATMMUEmulator;
class IATDeviceCustomNetworkEngine;
class ATVMCompiler;
class IATAsyncDispatcher;
class VDDisplayRendererSoft;
class IATDeviceControllerPort;

class ATDeviceCustomDisconnectedBus final : public IATDeviceBus {
	ATDeviceCustomDisconnectedBus(const ATDeviceCustomDisconnectedBus&) = delete;
	ATDeviceCustomDisconnectedBus& operator=(const ATDeviceCustomDisconnectedBus&) = delete;
public:
	ATDeviceCustomDisconnectedBus(IATDeviceParent& parent);
	~ATDeviceCustomDisconnectedBus();

	sint32 GetBusId() const { return mBusId; }
	void SetBusId(sint32 id) { mBusId = id; }
	bool HasChildDevices() const;

	void Shutdown();

	const wchar_t *GetBusName() const override;
	const char *GetBusTag() const override;
	const char *GetSupportedType(uint32 index) override;
	void GetChildDevices(vdfastvector<IATDevice *>& devs) override;
	void GetChildDevicePrefix(uint32 index, VDStringW& s) override;
	void AddChildDevice(IATDevice *dev) override;
	void RemoveChildDevice(IATDevice *dev) override;

private:
	IATDeviceParent& mParent;
	vdfastvector<IATDevice *> mChildDevices;
	sint32 mBusId = -1;
};

// Fork extension: diagnostics for the descriptor currently running in the VM.
// All queries run on the emulation thread. Source spans remain valid until
// reload/shutdown; large descriptors omit the source to bound diagnostics RAM.
class IATDeviceCustomDescriptor {
public:
	static constexpr uint32 kTypeID = "IATDeviceCustomDescriptor"_vdtypeid;
	static constexpr size_t kSourceLimit = 256 * 1024;
	virtual uint64 GetLoadedDescriptorRevision() const = 0;
	enum class SourceStatus { NotLoaded, Available, TooLarge };
	virtual SourceStatus GetLoadedDescriptorSource(VDStringSpanA& source) const = 0;
};

class ATDeviceCustom final
	: public ATDevice
	, public IATDeviceMemMap
	, public IATDeviceCartridge
	, public IATDeviceIndicators
	, public IATDeviceScheduling
	, public IATSchedulerCallback
	, public ATDeviceSIO
	, public IATDeviceRawSIO
	, public IATDevicePBIConnection
	, public IATDeviceParent
	, public IATDeviceDiagnostics
	, public IATPBIDevice
	, public IVDFileWatcherCallback
	, public IATDeviceCustomDescriptor
{
	friend struct ATDeviceCustomScriptCondition;

public:
	ATDeviceCustom();
	~ATDeviceCustom();

	void *AsInterface(uint32 iid) override;
	uint64 GetLoadedDescriptorRevision() const override { return mDescriptorRevision; }
	SourceStatus GetLoadedDescriptorSource(VDStringSpanA& source) const override {
		source = VDStringSpanA(mLoadedDescriptorSource.c_str(),
			mLoadedDescriptorSource.c_str() + mLoadedDescriptorSource.size());
		return !mbInitedCustomDevice ? SourceStatus::NotLoaded
			: mbDescriptorSourceCaptured ? SourceStatus::Available : SourceStatus::TooLarge;
	}

	void GetDeviceInfo(ATDeviceInfo& info) override;
	void GetSettingsBlurb(VDStringW& buf) override;
	void GetSettings(ATPropertySet& settings) override;
	bool SetSettings(const ATPropertySet& settings) override;
	const wchar_t *GetConfigError() const {
		return mLastError.empty() ? nullptr : mLastError.c_str();
	}

	void Init() override;
	void Shutdown() override;

	void ColdReset() override;
	void WarmReset() override;

	bool GetErrorStatus(uint32 idx, VDStringW& error) override;

public:		// IATDeviceMemMap
	void InitMemMap(ATMemoryManager *memmap) override;
	bool GetMappedRange(uint32 index, uint32& lo, uint32& hi) const override;

public:		// IATDeviceCartridge
	void InitCartridge(IATDeviceCartridgePort *cartPort) override;
	bool IsLeftCartActive() const override;
	void SetCartEnables(bool leftEnable, bool rightEnable, bool cctlEnable) override;
	void UpdateCartSense(bool leftActive) override;

public:
	void InitIndicators(IATDeviceIndicatorManager *r) override;

public:
	void InitScheduling(ATScheduler *sch, ATScheduler *slowsch) override;

public:
	void OnScheduledEvent(uint32 id) override;

public:
	void InitSIO(IATDeviceSIOManager *mgr) override;
	CmdResponse OnSerialAccelCommand(const ATDeviceSIORequest& request) override;
	CmdResponse OnSerialBeginCommand(const ATDeviceSIOCommand& cmd) override;
	void OnSerialAbortCommand() override;
	void OnSerialReceiveComplete(uint32 id, const void *data, uint32 len, bool checksumOK) override;
	void OnSerialFence(uint32 id) override;

public:
	void OnCommandStateChanged(bool asserted) override;
	void OnMotorStateChanged(bool asserted) override;
	void OnReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) override;
	void OnSendReady() override;

public:
	void InitPBI(IATDevicePBIManager *pbiman) override;

public:
	void GetPBIDeviceInfo(ATPBIDeviceInfo& devInfo) const override;
	void SelectPBIDevice(bool enable) override;
	bool IsPBIOverlayActive() const override;
	uint8 ReadPBIStatus(uint8 busData, bool debugOnly) override;

public:	// IATDeviceParent
	IATDeviceBus *GetDeviceBusById(sint32 id) override;
	sint32 GetDeviceBusIdByIndex(uint32 index) override;

public:	// IATDeviceDiagnostics
	void DumpStatus(ATConsoleOutput& output) override;

public:
	bool OnFileUpdated(const wchar_t *path) override;

private:
	static constexpr uint32 kMaxRawConfigSize = 256*1024*1024;
	static constexpr uint32 kMaxTotalSegmentData = 256*1024*1024;

	enum : uint32 {
		kEventId_Sleep = 1,
		kEventId_Run,
		kEventId_RawSend
	};

	using Segment = ATDeviceCustomSegment;

	using NetworkCommand = ATDeviceCustomNetworkCommand;

	enum class NetworkReply : uint8 {
		None,
		ReturnValue,
		EnableMemoryLayer,
		SetMemoryLayerOffset,
		SetMemoryLayerSegmentOffset,
		SetMemoryLayerReadOnly,
		ReadSegmentMemory,
		WriteSegmentMemory,
		CopySegmentMemory,
		ScriptInterrupt,
		GetSegmentNames,				// V2+
		GetMemoryLayerNames,			// V2+
		SetProtocolLevel,				// V2+
		FillSegmentMemory				// V2+
	};

	using AddressAction = ATDeviceCustomAddressAction;
	using SerialFenceId = ATDeviceCustomSerialFenceId;

	enum class SpecialVarIndex : uint8 {
		Address,
		Value,
	};

	enum class ThreadVarIndex : uint8 {
		Timestamp,
		Device,
		Command,
		Aux1,
		Aux2,
		Aux
	};

	using AddressBinding = ATDeviceCustomAddressBinding;

	using MemoryLayer = ATDeviceCustomMemoryLayer;
	friend MemoryLayer;

	using Network = ATDeviceCustomNetwork;
	friend Network;

	using SIO = ATDeviceCustomSIO;
	friend SIO;

	using SIOCommand = ATDeviceCustomSIOCommand;
	using SIODevice = ATDeviceCustomSIODevice;

	struct SIODeviceTable {
		SIODevice *mpDevices[256] {};
	};

	using PBIDevice = ATDeviceCustomPBIDevice;
	friend ATDeviceCustomPBIDevice;
	friend ATDeviceCustomSerialPort;
	friend ATDeviceCustomPrinterPort;

	template<bool T_DebugOnly>
	sint32 ReadControl(MemoryLayer& ml, uint32 addr);
	bool WriteControl(MemoryLayer& ml, uint32 addr, uint8 value);

	bool PostNetCommand(uint32 address, sint32 value, NetworkCommand cmd);
	sint32 SendNetCommand(uint32 address, sint32 value, NetworkCommand cmd);
	bool TryRestoreNet();
	sint32 ExecuteNetRequests(bool waitingForReply);
	void PostNetError(const char *msg);
	void OnNetRecvOOB();

	void ResetCustomDevice();
	void ShutdownCustomDevice();
	void ResetPBIInterrupt();
	void ReinitSegmentData(bool clearNonVolatile);

	void SendNextRawByte();
	void AbortRawSend(const ATVMThread& thread);

	void AbortThreadSleep(const ATVMThread& thread);
	void UpdateThreadSleep();

	void ScheduleThread(ATVMThread& thread);
	void ScheduleNextThread(ATVMThreadWaitQueue& queue);
	void ScheduleThreads(ATVMThreadWaitQueue& queue);
	void RunReadyThreads();

	void OnROMEnablesChanged();

	void ReloadConfig();

	class MemberParser;

	void ProcessDesc(const void *buf, size_t len);
	bool OnSetOption(ATVMCompiler&, const char *, const ATVMDataValue&);
	bool OnDefineCondition(ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers);
	bool OnDefineSegment(ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers);
	bool OnDefineMemoryLayer(ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers);
	bool OnDefineSIODevice(ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers);
	bool OnDefineControllerPort(ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers);
	bool OnDefinePBIDevice(ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers);
	bool OnDefineImage(ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers);
	bool OnDefineVideoOutput(ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers);
	bool OnDefineSound(ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers);
	bool OnDefineSoundParams(ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers);
	bool OnDefineSerialPort(ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers);
	bool OnDefinePrinterPort(ATVMCompiler& compiler, const char *name, const ATVMDataValue *initializers);

	const ATVMFunction *ParseScriptOpt(const ATVMTypeInfo& returnType, const ATVMDataValue *value, ATVMFunctionFlags flags);
	const ATVMFunction *ParseScript(const ATVMTypeInfo& returnType, const ATVMDataValue& value, ATVMFunctionFlags flags, ATVMConditionalMask conditionalMask);
	vdvector_view<uint8> ParseBlob(const ATVMDataValue& value);
	vdvector_view<uint8> ParseBlob16(const ATVMDataValue& value);
	uint8 ParseRequiredUint8(const ATVMDataValue& value);
	uint32 ParseRequiredUint32(const ATVMDataValue& value);
	static const char *ParseRequiredString(const ATVMDataValue& valueRef);
	static bool ParseBool(const ATVMDataValue& value);
	void LoadDependency(const ATVMDataValue& value, ATVFSFileView **view);

	void ClearFileTracking();
	void OpenViewWithTracking(const wchar_t *path, ATVFSFileView **view);
	bool CheckForTrackedChanges();
	void UpdateLayerModes(MemoryLayer& ml);

	sint32 AddDeviceBus(IATDeviceBus& bus);
	void RemoveDeviceBus(IATDeviceBus& bus);
	void TryReconnectChildDevices();

	ATMemoryManager *mpMemMan = nullptr;
	ATMMUEmulator *mpMMU = nullptr;
	ATScheduler *mpScheduler = nullptr;
	IATDeviceCartridgePort *mpCartPort = nullptr;
	IATDeviceIndicatorManager *mpIndicators = nullptr;
	uint32 mErrorSourceId = 0;
	IATDeviceSIOManager *mpSIOMgr = nullptr;
	vdrefptr<IATDeviceSIOInterface> mpSIOInterface;
	IATDevicePBIManager *mpPBIMgr = nullptr;
	bool mbInited = false;
	bool mbInitedSIO = false;
	bool mbInitedRawSIO = false;
	bool mbInitedPBI = false;
	bool mbInitedCart = false;
	bool mbInitedCustomDevice = false;
	bool mbInitedROMEnableHook = false;
	bool mbHotReload = false;
	bool mbAllowUnsafe = false;
	bool mbHasSerialPort = false;
	bool mbHasPrinterPort = false;
	uint64 mLastReloadAttempt = 0;
	uint64 mDescriptorRevision = 0;
	VDStringA mLoadedDescriptorSource;
	bool mbDescriptorSourceCaptured = false;
	uint32 mCartId = 0;

	IATAsyncDispatcher *mpAsyncDispatcher = nullptr;
	uint64 mAsyncNetCallback = 0;

	vdfastvector<MemoryLayer *> mMemoryLayers;
	vdfastvector<Segment *> mSegments;

	uint16 mNetworkPort = 0;

	vdrefptr<IATDeviceCustomNetworkEngine> mpNetworkEngine;
	uint32 mLastConnectionErrorCycle = 0;

	const SIOCommand *mpActiveCommand = nullptr;

	uint8 mPBIDeviceId = 0;
	bool mbPBIDeviceHasIrq = false;
	bool mbPBIDeviceSelected = false;
	bool mbPBIDeviceIrqAsserted = false;
	uint32 mPBIDeviceIrqBit = 0;
	ATIRQController *mpPBIDeviceIrqController = nullptr;

	ATVMCompiler *mpCompiler = nullptr;

	Segment mSIOFrameSegment {};
	SIODeviceTable *mpSIODeviceTable = nullptr;

	vdfastvector<const ATVMFunction *> mScriptFunctions;
	ATVMThread mVMThread;
	ATVMThread mVMThreadSIO;
	ATVMThread mVMThreadScriptInterrupt;

	using Domain = ATDeviceCustomDomain;

	Domain mVMDomain;

	const ATVMFunction *mpScriptEventInit = nullptr;
	const ATVMFunction *mpScriptEventColdReset = nullptr;
	const ATVMFunction *mpScriptEventWarmReset = nullptr;
	const ATVMFunction *mpScriptEventVBLANK = nullptr;
	const ATVMFunction *mpScriptEventSIOCommandChanged = nullptr;
	const ATVMFunction *mpScriptEventSIOMotorChanged = nullptr;
	const ATVMFunction *mpScriptEventSIOReceivedByte = nullptr;
	const ATVMFunction *mpScriptEventPBISelect = nullptr;
	const ATVMFunction *mpScriptEventPBIDeselect = nullptr;
	const ATVMFunction *mpScriptEventNetworkInterrupt = nullptr;

	uint32 mEventBindingVBLANK = 0;

	Network mNetwork;
	SIO mSIO;

	using Clock = ATDeviceCustomClock;
	friend Clock;

	Clock mClock;

	using Console = ATDeviceCustomConsole;
	friend Console;

	Console mConsole;

	using ControllerPort = ATDeviceCustomControllerPort;
	friend ControllerPort;

	ControllerPort *mpControllerPorts[4] {};

	using Debug = ATDeviceCustomDebug;
	friend Debug;

	Debug mDebug;
	vdrefptr<ATDeviceCustomEmulatorObj> mpEmulatorObj;

	using ScriptThread = ATDeviceCustomScriptThread;
	friend ScriptThread;

	vdfastvector<ScriptThread *> mScriptThreads;

	ATVMThreadWaitQueue mVMThreadRunQueue;

	using SleepInfo = ATDeviceCustomSleepInfo;
	using SleepInfoPred = ATDeviceCustomSleepInfoPred;

	vdfastvector<SleepInfo> mSleepHeap;
	ATEvent *mpEventThreadSleep = nullptr;
	ATEvent *mpEventThreadRun = nullptr;
	uint32 mInThreadRun = 0;

	using RawSendInfo = ATDeviceCustomRawSendInfo;

	ATVMThreadWaitQueue mVMThreadRawRecvQueue;
	ATVMThreadWaitQueue mVMThreadSIOCommandAssertQueue;
	ATVMThreadWaitQueue mVMThreadSIOCommandOffQueue;
	ATVMThreadWaitQueue mVMThreadSIOMotorChangedQueue;
	vdfastdeque<RawSendInfo> mRawSendQueue;

	ATEvent *mpEventRawSend = nullptr;

	vdfunction<void(ATVMThread&)> mpSleepAbortFn;
	vdfunction<void(ATVMThread&)> mpRawSendAbortFn;
	vdfunction<void()> mROMEnableHook;

	using Image = ATDeviceCustomImage;
	vdfastvector<Image *> mImages;

	using VideoOutput = ATDeviceCustomVideoOutput;
	friend VideoOutput;

	vdfastvector<VideoOutput *> mVideoOutputs;
	vdfastvector<ATDeviceCustomObject *> mVMObjects;

	vdfastvector<IATDeviceBus *> mBusList;
	ATDeviceCustomDisconnectedBus mDisconnectedBus;

	VDStringW mDeviceName;
	VDStringW mConfigPath;
	VDStringW mResourceBasePath;
	VDStringW mLastError;
	VDLinearAllocator mConfigAllocator;

	struct FileTrackingInfo {
		uint64 mSize;
		uint64 mTimestamp;
	};

	vdhashmap<VDStringW, FileTrackingInfo, vdhash<VDStringW>, vdstringpred> mTrackedFiles;
	VDFileWatcher mTrackedFileWatcher;
};

#endif
