//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2018 Avery Lee
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
//=========================================================================
// Full disk drive emulator utilities
//
// This contains common utilities for all full disk drive emulators, i.e.
// ones that actually emulate the controller and firmware.
//

#ifndef f_AT_DISKDRIVEFULLBASE_H
#define f_AT_DISKDRIVEFULLBASE_H

#include <optional>
#include <vd2/system/function.h>
#include <at/atcore/audiomixer.h>
#include <at/atcore/device.h>
#include <at/atcore/scheduler.h>
#include <at/atcpu/history.h>
#include <at/atdebugger/breakpointsimpl.h>
#include <at/atdebugger/target.h>

class ATEvent;
class ATDebugTargetBreakpointsBase;
enum ATFirmwareType : uint32;
class IATDeviceSIOManager;

class ATDiskDriveAudioPlayer final : public IATDeviceAudioOutput
{
	ATDiskDriveAudioPlayer(const ATDiskDriveAudioPlayer&) = delete;
	ATDiskDriveAudioPlayer& operator=(const ATDiskDriveAudioPlayer&) = delete;

public:
	ATDiskDriveAudioPlayer();
	~ATDiskDriveAudioPlayer();

	void Shutdown();

	void SetRotationSoundEnabled(bool enabled);
	void PlayStepSound(ATAudioSampleId sampleId, float volume, uint32 cycleDelay = 0);

	IATAudioMixer *GetMixer() const { return mpAudioMixer; }

public:		// IATDeviceAudioOutput
	void InitAudioOutput(IATAudioMixer *mixer) override;

private:
	IATAudioMixer *mpAudioMixer = nullptr;

	vdrefptr<IATAudioSoundGroup> mpRotationSoundGroup;
	vdrefptr<IATAudioSoundGroup> mpStepSoundGroup;
};

// ATDiskDriveChangeHandler
//
// Emulates write protect and ready changes on a disk change. This includes temporarily telling
// the FDC that the drive is not ready (latch open) and overriding the write protect on when
// the disk temporarily obscures the sensor.
//
class ATDiskDriveChangeHandler final : public IATSchedulerCallback {
	ATDiskDriveChangeHandler(const ATDiskDriveChangeHandler&) = delete;
	ATDiskDriveChangeHandler& operator=(const ATDiskDriveChangeHandler&) = delete;
public:
	ATDiskDriveChangeHandler();
	~ATDiskDriveChangeHandler();

	void Init(ATScheduler *scheduler);
	void Shutdown();

	void SetOutputStateFns(vdfunction<void(std::optional<bool>)> wpOverrideChange, vdfunction<void(std::optional<bool>)> readyOverrideChange);

	void Reset();
	void ChangeDisk();
	void ForceOutputUpdate() const;

	std::optional<bool> GetWriteProtectState() const {
		return mDiskChangeState ? std::optional<bool>(mDiskChangeState == 2) : std::optional<bool>();
	}

	std::optional<bool> GetDiskReadyState() const {
		return mDiskChangeState != 0 ? std::optional<bool>(false) : std::optional<bool>();
	}

public:
	void OnScheduledEvent(uint32 id) override;

private:
	uint8 mDiskChangeState = 0;
	uint32 mCyclesPerStep = 1;

	ATScheduler *mpScheduler = nullptr;
	ATEvent *mpEvent = nullptr;

	vdfunction<void(std::optional<bool>)> mpUpdateWriteProtect;
	vdfunction<void(std::optional<bool>)> mpUpdateDiskReady;
};

// How long of a delay there is in computer cycles between the drive sending SIO data
// and the computer receiving it. This is non-realistic but allows us to batch drive
// execution. The drive must be synced at least as often as this latency value.
constexpr uint32 kATDiskDriveTransmitLatency = 128;

class ATDiskDriveSerialBitTransmitQueue final : public IATSchedulerCallback {
	ATDiskDriveSerialBitTransmitQueue(const ATDiskDriveSerialBitTransmitQueue&) = delete;
	ATDiskDriveSerialBitTransmitQueue& operator=(const ATDiskDriveSerialBitTransmitQueue&) = delete;
public:
	static constexpr uint32 kTransmitLatency = kATDiskDriveTransmitLatency;
	
	ATDiskDriveSerialBitTransmitQueue();
	~ATDiskDriveSerialBitTransmitQueue();

	void Init(ATScheduler *masterScheduler, IATDeviceSIOManager *sioMgr);
	void Shutdown();

	// Adjust max cpb high to allow for lower baud rates.
	void SetMaxCyclesPerBit(uint32 cpb);

	void Reset();

	void AddTransmitBit(uint32 masterTime, bool bit);

public:
	void OnScheduledEvent(uint32 id) override;

private:
	void OnTransmitEvent();
	void QueueNextTransmitEvent();

	static constexpr uint32 kEventId_Transmit = 1;

	struct TransmitEvent {
		uint32 mTime : 31;
		uint32 mBit : 1;
	};

	IATDeviceSIOManager *mpSIOMgr = nullptr;
	ATScheduler *mpMasterScheduler = nullptr;
	ATEvent *mpEventTransmit = nullptr;
	uint32 mTransmitHead = 0;
	uint32 mTransmitTail = 0;

	uint32 mTransmitResetCounter = 0;
	uint32 mTransmitCyclesPerBit = 0;
	uint8 mTransmitShiftRegister = 0;
	uint8 mTransmitPhase = 0;
	bool mbTransmitCurrentBit = true;

	// Default min baud rate is ~16000 baud.
	uint32 mMaxCyclesPerBit = 114;

	static constexpr uint32 kTransmitQueueSize = kTransmitLatency;
	static constexpr uint32 kTransmitQueueMask = kTransmitQueueSize - 1;
	TransmitEvent mTransmitQueue[kTransmitQueueSize] = {};
};

class ATDiskDriveSerialByteTransmitQueue final : public IATSchedulerCallback {
	ATDiskDriveSerialByteTransmitQueue(const ATDiskDriveSerialByteTransmitQueue&) = delete;
	ATDiskDriveSerialByteTransmitQueue& operator=(const ATDiskDriveSerialByteTransmitQueue&) = delete;
public:
	static constexpr uint32 kTransmitLatency = kATDiskDriveTransmitLatency;
	
	ATDiskDriveSerialByteTransmitQueue();
	~ATDiskDriveSerialByteTransmitQueue();

	void Init(ATScheduler *masterScheduler, IATDeviceSIOManager *sioMgr);
	void Shutdown();

	void Reset();

	void AddTransmitByte(uint32 masterTime, uint8 value, uint32 cyclesPerBit);

public:
	void OnScheduledEvent(uint32 id) override;

private:
	void OnTransmitEvent();
	void QueueNextTransmitEvent();

	static constexpr uint32 kEventId_Transmit = 1;

	struct TransmitEvent {
		uint32 mTime;
		uint8 mData;
		uint32 mCyclesPerBit;
	};

	IATDeviceSIOManager *mpSIOMgr = nullptr;
	ATScheduler *mpMasterScheduler = nullptr;
	ATEvent *mpEventTransmit = nullptr;
	uint32 mTransmitHead = 0;
	uint32 mTransmitTail = 0;

	static constexpr uint32 kTransmitQueueSize = kTransmitLatency;
	static constexpr uint32 kTransmitQueueMask = kTransmitQueueSize - 1;
	TransmitEvent mTransmitQueue[kTransmitQueueSize] = {};
};

class ATDiskDriveSerialCommandQueue final : public IATSchedulerCallback {
	ATDiskDriveSerialCommandQueue(const ATDiskDriveSerialCommandQueue&) = delete;
	ATDiskDriveSerialCommandQueue& operator=(const ATDiskDriveSerialCommandQueue&) = delete;
public:
	static constexpr uint32 kTransmitLatency = kATDiskDriveTransmitLatency;
	
	ATDiskDriveSerialCommandQueue();
	~ATDiskDriveSerialCommandQueue();

	void Init(ATScheduler *driveScheduler, IATDeviceSIOManager *sioMgr);
	void Shutdown();

	bool GetDriveCommandState() const { return mbDriveCommandState; }
	void SetOnDriveCommandStateChanged(vdfunction<void(bool)> fn);

	void Reset();

	void AddCommandEdge(uint32 driveTime, uint32 polarity);

public:
	void OnScheduledEvent(uint32 id) override;

private:
	void OnCommandChangeEvent();
	void QueueNextCommandEvent();

	static constexpr uint32 kEventId_DriveCommandChange = 1;

	IATDeviceSIOManager *mpSIOMgr = nullptr;
	ATScheduler *mpDriveScheduler = nullptr;
	ATEvent *mpEventDriveCommandChange = nullptr;
	bool mbDriveCommandState = false;

	struct CommandEvent {
		uint32 mTime : 31;
		uint32 mBit : 1;
	};

	vdfastdeque<CommandEvent> mCommandQueue;

	vdfunction<void(bool)> mpOnDriveCommandStateChanged;
};
///////////////////////////////////////////////////////////////////////////

class ATDiskDriveFirmwareControl final : public IATDeviceFirmware {
public:
	~ATDiskDriveFirmwareControl();

	void Init(void *mem, uint32 len, ATFirmwareType type);
	void Shutdown();

public:		// IATDeviceFirmware
	void InitFirmware(ATFirmwareManager *fwman) override;
	bool ReloadFirmware() override;
	const wchar_t *GetWritableFirmwareDesc(uint32 idx) const override;
	bool IsWritableFirmwareDirty(uint32 idx) const override;
	void SaveWritableFirmware(uint32 idx, IVDStream& stream) override;
	ATDeviceFirmwareStatus GetFirmwareStatus() const override;

private:
	ATFirmwareManager *mpFwMgr = nullptr;
	void *mpFirmware = nullptr;
	uint32 mFirmwareLen = 0;
	ATFirmwareType mFirmwareType {};
	bool mbFirmwareUsable = false;
};

///////////////////////////////////////////////////////////////////////////

class IATDiskDriveDebugTargetProxy {
public:
	virtual std::pair<const uintptr *, const uintptr *> GetReadWriteMaps() const = 0;
	virtual void SetHistoryBuffer(ATCPUHistoryEntry *harray) = 0;
	virtual uint32 GetHistoryCounter() const = 0;
	virtual uint32 GetTime() const = 0;
	virtual uint32 GetStepStackLevel() const = 0;
	virtual void GetExecState(ATCPUExecState& state) const = 0;
	virtual void SetExecState(const ATCPUExecState& state) = 0;
};

template<typename T>
class ATDiskDriveDebugTargetProxyBaseT : public IATDiskDriveDebugTargetProxy {
public:
	void Init(T& impl) {
		mpImpl = &impl;
	}

	std::pair<const uintptr *, const uintptr *> GetReadWriteMaps() const override {
		return { mpImpl->GetReadMap(), mpImpl->GetWriteMap() };
	}

	void SetHistoryBuffer(ATCPUHistoryEntry *harray) override {
		mpImpl->SetHistoryBuffer(harray);
	}

	uint32 GetHistoryCounter() const override {
		return mpImpl->GetHistoryCounter();
	}
	
	uint32 GetStepStackLevel() const override {
		return mpImpl->GetStepStackLevel();
	}

	void GetExecState(ATCPUExecState& state) const override {
		mpImpl->GetExecState(state);
	}

	void SetExecState(const ATCPUExecState& state) override {
		mpImpl->SetExecState(state);
	}

protected:
	T *mpImpl;
};

template<typename T>
class ATDiskDriveDebugTargetProxyT final : public ATDiskDriveDebugTargetProxyBaseT<T> {
public:
	uint32 GetTime() const override {
		return this->mpImpl->GetTime();
	}
};

class ATDiskDriveDebugTargetControl
	: public IATDeviceScheduling
	, public IATSchedulerCallback
	, public IATDeviceDebugTarget
	, public IATDebugTarget
	, public IATDebugTargetHistory
	, public IATDebugTargetExecutionControl
	, public IATCPUBreakpointHandler
{
public:
	~ATDiskDriveDebugTargetControl();

	void InitTargetControl(IATDiskDriveDebugTargetProxy& proxy, double timestampFrequency, ATDebugDisasmMode disasmMode, ATDebugTargetBreakpointsBase *bpImpl, IATDevice *parentDevice);
	void ApplyDisplayCPUClockMultiplier(double f);
	
	void ResetTargetControl();
	void ShutdownTargetControl();

	uint32 DriveTimeToMasterTime() const;
	uint32 MasterTimeToDriveTime() const;

	uint64 MasterTimeToDriveTime64(uint64 masterTime) const;

	uint32 AccumSubCycles();

	void FlushStepNotifications() {
		if (mbStepNotifyPending)
			FlushStepNotifications2();
	}

	void ScheduleImmediateResume();

public:
	void *AsInterface(uint32 iid) override;

public:	// IATDeviceScheduling
	void InitScheduling(ATScheduler *sch, ATScheduler *slowsch) override;

public:	// IATSchedulerCallback
	void OnScheduledEvent(uint32 id) override;

public:	// IATDeviceDebugTarget
	IATDebugTarget *GetDebugTarget(uint32 index) override;
	
public:	// IATDebugTarget
	const char *GetName() override final;
	ATDebugDisasmMode GetDisasmMode() override;
	float GetDisplayCPUClock() const override;
	sint32 GetTimeSkew() override;

	void GetExecState(ATCPUExecState& state) override;
	void SetExecState(const ATCPUExecState& state) override;

	uint8 ReadByte(uint32 address) override final;
	void ReadMemory(uint32 address, void *dst, uint32 n) override final;

	uint8 DebugReadByte(uint32 address) override final;
	void DebugReadMemory(uint32 address, void *dst, uint32 n) override final;

	void WriteByte(uint32 address, uint8 value) override final;
	void WriteMemory(uint32 address, const void *src, uint32 n) override final;

public:	// IATDebugTargetHistory
	bool GetHistoryEnabled() const override final;
	void SetHistoryEnabled(bool enable) override final;

	std::pair<uint32, uint32> GetHistoryRange() const override final;
	uint32 ExtractHistory(const ATCPUHistoryEntry **hparray, uint32 start, uint32 n) const override final;
	uint32 ConvertRawTimestamp(uint32 rawTimestamp) const override;
	double GetTimestampFrequency() const override final;

public:	// IATDebugTargetExecutionControl
	void Break() override;
	bool StepInto(const vdfunction<void(bool)>& fn) override;
	bool StepOver(const vdfunction<void(bool)>& fn) override;
	bool StepOut(const vdfunction<void(bool)>& fn) override;
	void StepUpdate() override;
	void RunUntilSynced() override;

public:	// IATCPUBreakpointHandler
	bool CheckBreakpoint(uint32 pc) override;

protected:
	virtual void Sync() = 0;
	uint32 GetLastSyncTime() const { return mLastSync; }

private:
	void CancelStep();
	void FlushStepNotifications2();

	ATEvent *mpRunEvent = nullptr;
	ATEvent *mpImmRunEvent = nullptr;

	uint32 mLastSync = 0;
	uint32 mLastSyncDriveTime = 0;
	uint32 mDriveCycleAccumF32 = 0;
	uint32 mDriveCycleLimit = 0;
	uint32 mRawTimestampToDriveAdjust = 0;
	uint64 mDriveCyclesPerSystemCycleF32 = 0;
	uint64 mSystemCyclesPerDriveCycleF32 = 0;

	uint32 mHistorySize = 0;
	uint32 mHistoryMask = 0;
	double mTimestampFrequency = 0;
	double mDisplayFrequency = 0;
	VDStringA mDisplayName;
	IATDiskDriveDebugTargetProxy *mpProxy = nullptr;

	vdfastvector<ATCPUHistoryEntry> mHistory;

	vdfunction<void(bool)> mpStepHandler = {};
	bool mbStepOut = false;
	bool mbStepNotifyPending = false;
	bool mbStepNotifyPendingBP = false;
	uint32 mStepStartTime = 0;
	uint32 mStepOutSP = 0;

	ATDebugDisasmMode mDisasmMode = {};
	const uintptr *mpReadMap = nullptr;
	const uintptr *mpWriteMap = nullptr;

	ATDebugTargetBreakpointsBase *mpBreakpointsImpl = nullptr;

protected:
	enum : uint32 {
		kEventId_Run = 1,
		kEventId_ImmRun,
		kEventId_FirstCustom
	};

	ATScheduler *mpScheduler = nullptr;
	ATScheduler *mpSlowScheduler = nullptr;
	ATScheduler mDriveScheduler;
};

#endif
