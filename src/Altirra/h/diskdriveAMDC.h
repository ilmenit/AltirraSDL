//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2019 Avery Lee
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

#ifndef f_AT_DISKDRIVEAMDC_H
#define f_AT_DISKDRIVEAMDC_H

#include <vd2/system/function.h>
#include <vd2/system/refcount.h>
#include <vd2/system/vdstl.h>
#include <at/atcore/devicediskdrive.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/deviceserial.h>
#include <at/atcore/devicesioimpl.h>
#include <at/atcore/scheduler.h>
#include <at/atcpu/co6809.h>
#include <at/atcpu/breakpoints.h>
#include <at/atcpu/history.h>
#include <at/atcpu/memorymap.h>
#include <at/atdebugger/breakpointsimpl.h>
#include <at/atdebugger/target.h>
#include <at/atemulation/acia6850.h>
#include "fdc.h"
#include "diskdrivefullbase.h"
#include "diskinterface.h"

class ATIRQController;

class ATDeviceDiskDriveAMDC final
	: public ATDevice
	, public IATDeviceDiskDrive
	, public ATDeviceSIO
	, public ATDiskDriveDebugTargetControl
	, public IATDeviceRawSIO
{
public:
	ATDeviceDiskDriveAMDC();
	~ATDeviceDiskDriveAMDC();

	void *AsInterface(uint32 iid) override;

	void GetDeviceInfo(ATDeviceInfo& info) override;
	void GetSettingsBlurb(VDStringW& buf) override;
	void GetSettings(ATPropertySet & settings) override;
	bool SetSettings(const ATPropertySet & settings) override;
	void Init() override;
	void Shutdown() override;
	uint32 GetComputerPowerOnDelay() const override;
	void WarmReset() override;
	void ComputerColdReset() override;
	void PeripheralColdReset() override;

public:		// IATDeviceDiskDrive
	void InitDiskDrive(IATDiskDriveManager *ddm) override;
	ATDeviceDiskDriveInterfaceClient GetDiskInterfaceClient(uint32 index) override;

public:		// ATDeviceSIO
	void InitSIO(IATDeviceSIOManager *mgr) override;

public:	// IATSchedulerCallback
	void OnScheduledEvent(uint32 id) override;

public:	// IATDeviceRawSIO
	void OnCommandStateChanged(bool asserted) override;
	void OnMotorStateChanged(bool asserted) override;
	void OnReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) override;
	void OnSendReady() override;

public:
	void OnDiskChanged(uint32 index, bool mediaRemoved);
	void OnWriteModeChanged(uint32 index);
	void OnTimingModeChanged(uint32 index);
	void OnAudioModeChanged(uint32 index);

protected:
	void Sync() override final;

	uint8 OnPrinterRead(uint32 addr);
	void OnPrinterWrite(uint32 addr, uint8 value);

	uint8 OnACIADebugRead(uint32 addr);
	uint8 OnACIARead(uint32 addr);
	void OnACIAWrite(uint32 addr, uint8 value);

	uint8 OnFDCDebugRead(uint32 addr);
	uint8 OnFDCRead(uint32 addr);
	void OnFDCWrite(uint32 addr, uint8 value);

	uint8 OnSwitchRead(uint32 addr);

	void OnControlWrite(uint32 addr, uint8 value);

	uint8 OnWatchdogDebugRead(uint32 addr) const;
	uint8 OnWatchdogRead(uint32 addr);

	void OnFDCDataRequest(bool asserted);
	void OnFDCStep(bool inward);

	void OnACIATransmit(uint8 v, uint32 cyclesPerBit);

	void SetMotorEnabled(bool enabled);
	void PlayStepSound();
	void UpdateRotationStatus();
	void UpdateDiskStatus();
	
	void SelectDrive(int index);

	void SetNmiTimeout();

	static constexpr uint32 kNumDrives = 4;
	static const uint32 kACIAClockNormal;
	static const uint32 kACIAClockFast;

	enum : uint32 {
		kEventId_DriveTimeout = kEventId_FirstCustom,
	};

	ATEvent *mpEventDriveTimeout = nullptr;
	IATDeviceSIOManager *mpSIOMgr = nullptr;
	IATDiskDriveManager *mpDiskDriveManager = nullptr;

	bool mbCommandState = false;

	int mSelectedDrive = -1;
	uint8 mAvailableDrives = 0;

	bool mbSoundsEnabled = false;
	bool mbForcedIndexPulse = false;
	bool mbMotorRunning = false;
	uint32 mLastStepSoundTime = 0;
	uint32 mLastStepPhase = 0;
	uint8 mDiskChangeState = 0;
	uint8 mDriveId = 0;
	uint32 mSwitches = 0;

	ATDiskDriveAudioPlayer mAudioPlayer;

	enum DriveType : uint32 {
		kDriveType_None,
		kDriveType_40Track,
		kDriveType_80Track,
	};

	struct Drive : public IATDiskInterfaceClient {
		ATDeviceDiskDriveAMDC *mpParent = nullptr;
		uint32 mIndex = 0;

		ATDiskInterface *mpDiskInterface = nullptr;
		uint32 mCurrentHalfTrack = 0;
		uint32 mMaxHalfTrack = 0;

		DriveType mType = kDriveType_None;

		ATDiskDriveChangeHandler mDiskChangeHandler;

		void OnDiskChanged(bool mediaRemoved) override;
		void OnWriteModeChanged() override;
		void OnTimingModeChanged() override;
		void OnAudioModeChanged() override;
		bool IsImageSupported(const IATDiskImage& image) const override;
	} mDrives[kNumDrives];

	ATCoProcReadMemNode mReadNodePrinter {};
	ATCoProcWriteMemNode mWriteNodePrinter {};
	ATCoProcReadMemNode mReadNodeACIA {};
	ATCoProcWriteMemNode mWriteNodeACIA {};
	ATCoProcReadMemNode mReadNodeFDC {};
	ATCoProcWriteMemNode mWriteNodeFDC {};
	ATCoProcReadMemNode mReadNodeSwitches {};
	ATCoProcWriteMemNode mWriteNodeControl {};
	ATCoProcReadMemNode mReadNodeWatchdog {};

	ATFDCEmulator mFDC;
	ATACIA6850Emulator mACIA;

	ATDiskDriveSerialByteTransmitQueue mSerialXmitQueue;
	ATDiskDriveSerialCommandQueue mSerialCmdQueue;

	ATDiskDriveDebugTargetProxyT<ATCoProc6809> mTargetHistoryProxy;
	
	ATCoProc6809 mCoProc;

	uint8 mROM[0x1000] = {};
	uint8 mRAM[0x400] = {};
	uint8 mDummyRead[256] = {};
	uint8 mDummyWrite[256] = {};

	ATDiskDriveFirmwareControl mFirmwareControl;
	ATDebugTargetBreakpointsImpl mBreakpointsImpl;
};

#endif
