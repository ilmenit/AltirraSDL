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
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include <stdafx.h>
#include <algorithm>
#include <iterator>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/math.h>
#include <vd2/system/binary.h>
#include <vd2/system/strutil.h>
#include <at/atcore/cio.h>
#include <at/atcore/deviceindicators.h>
#include <at/atcore/enumparseimpl.h>
#include <at/atcore/media.h>
#include <at/atcore/randomization.h>
#include <at/atcore/sioutils.h>
#include <at/atcore/snapshotimpl.h>
#include <at/atcore/vfs.h>
#include <at/atcore/wraptime.h>
#include "disk.h"
#include "diskprofile.h"
#include "disktrace.h"
#include "console.h"
#include "cpu.h"
#include "savestate.h"
#include "simulator.h"
#include "debuggerlog.h"
#include "audiosampleplayer.h"
#include "trace.h"
#include "uirender.h"

ATLogChannel g_ATLCDisk(false, false, "DISK", "Disk activity");
ATLogChannel g_ATLCDiskCmd(false, false, "DISKCMD", "Disk commands");
ATLogChannel g_ATLCDiskData(false, false, "DISKXFR", "Disk data transfer");

namespace {
	// Cycles/second. This is only correct for NTSC, but it's close enough
	// for PAL for disk emulation purposes.
	static constexpr int kCyclesPerSecond = 7159090/4;

	static constexpr int kCyclesPerDiskRotation_288RPM = (kCyclesPerSecond * 60 + 144) / 288;

	// Cycles for a fake rotation as seen by the FDC. Neither the 810 nor the 1050
	// use the real index pulse; they fake it with the RIOT.
	//
	// 810: ~522ms
	// 1050: ~235.5ms x 2 rotations
	//
	static constexpr int kCyclesPerFakeRot_810		= (kCyclesPerSecond * 522 + 500) / 1000;
	static constexpr int kCyclesPerFakeRot_1050		= (kCyclesPerSecond * 471 + 500) / 1000;

	static constexpr int kCyclesPerSIOByte_Happy_Native_Fast = 564;			// 315 cycles/byte @ 1MHz
	static constexpr int kCyclesPerSIOBit_Happy_Native_Fast = 47;		// Native high speed (26 cycles/bit @ 1MHz)

	// Delay from end of ACK byte until FDC command is sent.
	// 810: ~1608 cycles @ 500KHz = ~5756 cycles @ 1.79MHz.
	static constexpr int kCyclesFDCCommandDelay = 5756;

	// Rotational delay in cycles when accurate sector timing is disabled.
	static constexpr int kCyclesRotationalDelay_Fast = 2000;

	// Time from end of sector read to start of Complete byte (FDC reset and checksum):
	//	810: ~2568 cycles @ 500KHz = 9192 cycles
	//	1050: ~270 cycles @ 1MHz = 483 cycles
	//
	static constexpr uint32 kCyclesPostReadDelay_Buffered = 400;
	static constexpr uint32 kCyclesPostReadDelay_Fast = 1000;
	static constexpr uint32 kCyclesPostReadDelay_810 = 9192;
	static constexpr uint32 kCyclesPostReadDelay_1050 = 483;

	enum {
		kATDiskEventMotorOff = 1
	};

	static constexpr uint8 kDefaultPERCOM[]={
	//	trk   step  spt         sd-1  enc   bps         online
		0x28, 0x01, 0x00, 0x12, 0x00, 0x00, 0x00, 0x80, 0xFF, 0x00, 0x00, 0x00
	};

	static constexpr uint8 kDefaultPERCOMED[]={
		0x28, 0x01, 0x00, 0x1A, 0x00, 0x04, 0x00, 0x80, 0xFF, 0x00, 0x00, 0x00
	};
}

///////////////////////////////////////////////////////////////////////////

AT_DEFINE_ENUM_TABLE_BEGIN(ATDiskEmulationMode)
	{ kATDiskEmulationMode_Generic,				"generic" },
	{ kATDiskEmulationMode_FastestPossible,		"fastest" },
	{ kATDiskEmulationMode_810,					"810" },
	{ kATDiskEmulationMode_1050,				"1050" },
	{ kATDiskEmulationMode_XF551,				"xf551" },
	{ kATDiskEmulationMode_USDoubler,			"usdoubler" },
	{ kATDiskEmulationMode_Speedy1050,			"speedy1050" },
	{ kATDiskEmulationMode_IndusGT,				"indusgt" },
	{ kATDiskEmulationMode_Happy1050,			"happy1050" },
	{ kATDiskEmulationMode_1050Turbo,			"1050turbo" },
	{ kATDiskEmulationMode_Generic57600,		"generic56k" },
	{ kATDiskEmulationMode_Happy810,			"happy810" },
AT_DEFINE_ENUM_TABLE_END(ATDiskEmulationMode, kATDiskEmulationMode_Generic)

///////////////////////////////////////////////////////////////////////////

ATDiskEmulator::ATDiskEmulator() {
}

ATDiskEmulator::~ATDiskEmulator() {
	Shutdown();
}

void ATDiskEmulator::Init(int unit, ATDiskInterface *dif, ATScheduler *sched, ATScheduler *slowsched, IATSyncAudioSamplePlayer& mixer) {
	mpDiskInterface = dif;
	dif->AddClient(this);

	mpAudioSyncMixer = &mixer;
	mpRotationSoundGroup = mixer.CreateGroup(ATAudioGroupDesc().Mix(kATAudioMix_Drive));
	mpStepSoundGroup = mixer.CreateGroup(ATAudioGroupDesc().Mix(kATAudioMix_Drive).RemoveSupercededSounds());

	mLastRotationUpdateCycle = ATSCHEDULER_GETTIME(sched);
	mUnit = unit;
	mpScheduler = sched;
	mpSlowScheduler = slowsched;

	mpSIOInterface = mpSIOMgr->AddDevice(this);

	ComputeSupportedProfile();
	Reset();

	OnDiskChanged(true);
	OnWriteModeChanged();
	OnTimingModeChanged();
	OnAudioModeChanged();
}

void ATDiskEmulator::Shutdown() {
	mpSIOInterface = nullptr;
	mpSIOMgr = nullptr;

	if (mpDiskInterface) {
		mpDiskInterface->RemoveClient(this);
		mpDiskInterface = nullptr;
	}

	mpRotationSoundGroup = nullptr;
	mpStepSoundGroup = nullptr;
	mpAudioSyncMixer = nullptr;
}

void ATDiskEmulator::Rename(int unit) {
	mUnit = unit;
}

void ATDiskEmulator::SetEnabled(bool enabled) {
	if (mbEnabled == enabled)
		return;

	// If the drive is being disabled, reset it so it releases status indicators on
	// the disk interface. This must be done before we flip the enabled flag.
	if (!enabled)
		Reset();

	mbEnabled = enabled;
}

void ATDiskEmulator::SetEmulationMode(ATDiskEmulationMode mode) {
	if (mEmuMode == mode)
		return;

	mEmuMode = mode;
	ComputeSupportedProfile();
}

void ATDiskEmulator::Reset() {
	AbortCommand();

	if (mpSlowScheduler)
		mpSlowScheduler->UnsetEvent(mpMotorOffEvent);

	if (mpAudioSyncMixer)
		mpRotationSoundGroup->StopAllSounds();

	mTransferLength = 0;
	mPhantomSectorCounter = 0;

	uint32 rotationOffset = ATRandomizeAdvanceFast(g_ATRandomizationSeeds.mDiskStartPos) % mpProfile->mCyclesPerDiskRotation;

	mLastRotationUpdateCycle = ATSCHEDULER_GETTIME(mpScheduler);
	mRotationalCounter = rotationOffset;
	mRotations = 0;
	mbLastOpError = false;

	if (mpRotationTracer)
		mpRotationTracer->Warp(rotationOffset);

	// Power-on status with no disk is $5F for 1050, $FF for XF551. The
	// get status command will handle the not ready bit.
	if (mEmuMode == kATDiskEmulationMode_XF551)
		mFDCStatus = 0xFF;
	else
		mFDCStatus = 0xDF;

	mActiveCommand = 0;
	mCustomCodeState = 0;

	if (mEmuMode == kATDiskEmulationMode_810)
		mCurrentTrack = mTrackCount ? mTrackCount - 1 : 0;
	else
		mCurrentTrack = 0;

	mBufferedTrack = -1;
	mLastReadSector = 0;
	mbTrackBufferingEnabled = true;

	for(ExtVirtSectors::iterator it(mExtVirtSectors.begin()), itEnd(mExtVirtSectors.end()); it!=itEnd; ++it) {
		ExtVirtSector& vsi = *it;

		vsi.mPhantomSectorCounter = 0;
	}

	mWeakBitLFSR = 1;

	ComputeSupportedProfile();

	if (mpDiskInterface->IsDiskLoaded()) {
		DetectDensity();
	} else
		memcpy(mPERCOM, kDefaultPERCOM, 12);

	// clear activity counter
	if (mbEnabled) {
		mpDiskInterface->SetShowMotorActive(false);
		mpDiskInterface->SetShowActivity(false, 0);
	}

	memset(mDriveRAM, 0, sizeof mDriveRAM);
}

namespace {
	uint8 Checksum(const uint8 *p, int len) {
		uint32 checksum = 0;
		for(int i=0; i<len; ++i) {
			checksum += p[i];
			checksum += (checksum >> 8);
			checksum &= 0xff;
		}

		return (uint8)checksum;
	}
}

void ATDiskEmulator::SetForcedPhantomSector(uint16 sector, uint8 index, int order) {
	if (!sector || sector >= mExtVirtSectors.size())
		return;

	IATDiskImage *image = mpDiskInterface->GetDiskImage();
	if (!image)
		return;

	ATDiskVirtualSectorInfo vsi;
	image->GetVirtualSectorInfo(sector - 1, vsi);

	if (index >= vsi.mNumPhysSectors)
		return;

	uint32 physIndex = vsi.mFirstPhysSector;
	for(uint32 i = 0; i < index; ++i)
		physIndex = image->GetNextPhysicalSector(physIndex);

	mExtPhysSectors[physIndex].mForcedOrder = (sint8)order;
}

int ATDiskEmulator::GetForcedPhantomSector(uint16 sector, uint8 index) {
	if (!sector || sector >= mExtVirtSectors.size())
		return -1;

	IATDiskImage *image = mpDiskInterface->GetDiskImage();
	if (!image)
		return -1;

	ATDiskVirtualSectorInfo vsi;
	image->GetVirtualSectorInfo(sector - 1, vsi);
	if (index >= vsi.mNumPhysSectors)
		return -1;

	uint32 physIndex = vsi.mFirstPhysSector;
	for(uint32 i = 0; i < index; ++i)
		physIndex = image->GetNextPhysicalSector(physIndex);

	return mExtPhysSectors[physIndex].mForcedOrder;
}

void ATDiskEmulator::SetTraceContext(ATTraceContext *context) {
	mpTraceContext = context;

	if (context) {
		ATTraceCollection *coll = context->mpCollection;

		VDStringW name;
		name.sprintf(L"Disk %u", mUnit + 1);

		ATTraceGroup *group = coll->AddGroup(name.c_str());
		mpTraceChannel = group->AddFormattedChannel(context->mBaseTime, context->mBaseTickScale, L"Commands");

		UpdateRotationalCounter();

		mpRotationTracer = new ATDiskRotationTracer;
		mpRotationTracer->Init(*mpScheduler, context->mBaseTime, mpProfile->mCyclesPerDiskRotation, *group);
		mpRotationTracer->SetDiskImage(mpDiskInterface->GetDiskImage());
		mpRotationTracer->SetMotorRunning(mpMotorOffEvent != nullptr);
		mpRotationTracer->SetTrack(mCurrentTrack);
		mpRotationTracer->Warp(mRotationalCounter);
	} else {
		if (mpRotationTracer) {
			mpRotationTracer->Shutdown();
			mpRotationTracer = nullptr;
		}

		mpTraceChannel = nullptr;
	}
}

class ATSaveStateDisk final : public ATSnapExchangeObject<ATSaveStateDisk, "ATSaveStateDisk"> {
public:
	template<typename T>
	void Exchange(T& rw) {
		rw.Transfer("idle_timer", &mIdleTimer);
		rw.Transfer("active_command_id", &mActiveCommandId);
		rw.Transfer("active_command_state", &mActiveCommandState);
		rw.Transfer("active_command_sector", &mActiveCommandSector);
		rw.Transfer("active_command_phys_sector", &mActiveCommandPhysSectorPlus1);
		rw.Transfer("active_command_timer", &mActiveCommandTimer);
		// MERGE NOTE: upstream exchanged the timer twice. Keep its numeric
		// legacy field readable and use a separate key for the missing flag.
		uint32 legacyBufferedTimer = mActiveCommandTimer;
		rw.Transfer("active_command_buffered", &legacyBufferedTimer);
		rw.Transfer("active_command_buffered_v2", &mbActiveCommandBuffered);
		rw.Transfer("active_command_buffered_read_track", &mbActiveCommandReadTrack);
		rw.Transfer("active_command_buffered_read_error", &mbActiveCommandReadError);
		rw.Transfer("active_command_buffered_write_track", &mbActiveCommandWriteTrack);
		rw.Transfer("rotational_pos", &mRotationalPos);
		rw.Transfer("current_track", &mCurrentTrack);
		rw.Transfer("buffered_track", &mBufferedTrack);
		rw.Transfer("last_read_sector", &mLastReadSector);
		rw.Transfer("active_command", &mpActiveCommand);

		// Fork extension: frame rollback also preserves drive firmware and
		// transfer state that ordinary mechanical-position snapshots omit.
		rw.Transfer("exact_runtime", &mbExactRuntime);
		rw.Transfer("runtime_firmware_profile", &mRuntimeFirmwareProfile);
		rw.Transfer("motor_event_delay", &mMotorEventDelay);
		rw.Transfer("motor_deadline_offset", &mMotorDeadlineOffset);
		rw.Transfer("rotation_counter", &mRotationalCounter);
		rw.Transfer("rotation_update_offset", &mRotationUpdateOffset);
		rw.Transfer("rotations", &mRotations);
		rw.Transfer("exact_command_start_offset", &mExactCommandStartOffset);
		rw.Transfer("exact_command_sector", &mExactCommandSector);
		rw.Transfer("exact_last_read_sector", &mExactLastReadSector);
		rw.Transfer("exact_motor_off_time_suspended", &mbMotorOffTimeSuspended);
		rw.Transfer("exact_last_accel_time_skew", &mLastAccelTimeSkew);
		rw.Transfer("exact_receive_checksum_ok", &mbReceiveChecksumOK);
		rw.Transfer("exact_transfer_length", &mTransferLength);
		rw.Transfer("exact_transfer_complete_rot_pos", &mTransferCompleteRotPos);
		rw.Transfer("exact_fdcstatus", &mFDCStatus);
		rw.Transfer("exact_original_device", &mOriginalDevice);
		rw.Transfer("exact_original_command", &mOriginalCommand);
		rw.Transfer("exact_active_command_high_speed", &mbActiveCommandHighSpeed);
		rw.Transfer("exact_active_command_wait", &mbActiveCommandWait);
		rw.Transfer("exact_active_command_start_rot_pos", &mActiveCommandStartRotPos);
		rw.Transfer("exact_active_command_search_start_rot_pos", &mActiveCommandSearchStartRotPos);
		rw.Transfer("exact_custom_code_state", &mCustomCodeState);
		rw.Transfer("exact_phantom_sector_counter", &mPhantomSectorCounter);
		rw.Transfer("exact_weak_bit_lfsr", &mWeakBitLFSR);
		rw.Transfer("exact_sectors_per_track", &mSectorsPerTrack);
		rw.Transfer("exact_track_count", &mTrackCount);
		rw.Transfer("exact_side_count", &mSideCount);
		rw.Transfer("exact_mfm", &mbMFM);
		rw.Transfer("exact_high_density", &mbHighDensity);
		rw.Transfer("exact_track_buffering_enabled", &mbTrackBufferingEnabled);
		rw.Transfer("exact_command_valid", &mbCommandValid);
		rw.Transfer("exact_command_frame_high_speed", &mbCommandFrameHighSpeed);
		rw.Transfer("exact_last_op_error", &mbLastOpError);
		rw.Transfer("exact_boot_sector_count", &mBootSectorCount);
		rw.Transfer("exact_total_sector_count", &mTotalSectorCount);
		rw.Transfer("exact_sector_size", &mSectorSize);
		rw.Transfer("exact_last_sector", &mLastSector);
		rw.Transfer("exact_format_sector_size", &mFormatSectorSize);
		rw.Transfer("exact_format_sector_count", &mFormatSectorCount);
		rw.Transfer("exact_format_boot_sector_count", &mFormatBootSectorCount);
		rw.Transfer("exact_cycles_per_siobit_current", &mCyclesPerSIOBitCurrent);
		rw.Transfer("exact_cycles_per_siobyte_current", &mCyclesPerSIOByteCurrent);
		rw.Transfer("exact_send_packet", &mSendPacket);
		rw.Transfer("exact_receive_packet", &mReceivePacket);
		rw.Transfer("exact_drive_ram", &mDriveRAM);
		rw.Transfer("exact_forced_sector_order", &mForcedSectorOrder);
		rw.Transfer("exact_phantom_sector_counters", &mPhantomSectorCounters);
		rw.TransferArray("exact_percom", mPERCOM);

		if constexpr (T::IsReader) {
			if (mbExactRuntime &&
				(mRuntimeFirmwareProfile > 2
				|| mSendPacket.size() != 8192 + 16
				|| mReceivePacket.size() != 8192 + 16
				|| mDriveRAM.size() != 8192
				|| mMotorEventDelay >= 0x80000000u
				|| mTransferLength > 8192 + 16
				|| mSectorSize < 0 || mSectorSize > 8192
				|| mBootSectorCount < 0 || mTotalSectorCount < 0
				|| mFormatSectorSize < 0 || mFormatSectorSize > 8192
				|| mFormatSectorCount < 0 || mFormatBootSectorCount < 0
				|| !mCyclesPerSIOBitCurrent || !mCyclesPerSIOByteCurrent))
				throw ATInvalidSaveStateException();
			// Allow for up to a ~10 minute idle time -- beyond that consider it broken.
			// Clamp to ~1m instead of throwing since previous versions wrote this incorrectly.
			// Up to ~30s is reasonable disk drive behavior.
			mIdleTimer = std::min<uint32>(mIdleTimer, 100'000'000);

			// Disks can only have 65,535 logical sectors -- so they will never have that
			// many tracks.
			if (mCurrentTrack >= 65535)
				throw ATInvalidSaveStateException();
		}
	}

	uint32 mIdleTimer = 0;
	uint32 mActiveCommandTimer = 0;
	uint8 mActiveCommandId = 0;
	uint32 mActiveCommandState = 0;
	uint16 mActiveCommandSector = 0;
	uint32 mActiveCommandPhysSectorPlus1 = 0;
	bool mbActiveCommandBuffered = false;
	bool mbActiveCommandReadTrack = false;
	bool mbActiveCommandReadError = false;
	bool mbActiveCommandWriteTrack = false;
	float mRotationalPos = 0;
	uint32 mCurrentTrack = 0;
	sint32 mBufferedTrack = 0;
	uint16 mLastReadSector = 0;

	// Packet sizes mirror ATDiskEmulator's existing 8K command buffers.
	bool mbExactRuntime = false;
	uint8 mRuntimeFirmwareProfile = 0;
	uint32 mMotorEventDelay = 0;
	uint32 mMotorDeadlineOffset = 0;
	uint32 mRotationalCounter = 0;
	uint32 mRotationUpdateOffset = 0;
	uint32 mRotations = 0;
	uint32 mExactCommandStartOffset = 0;
	uint32 mExactCommandSector = 0;
	uint32 mExactLastReadSector = 0;
	bool mbMotorOffTimeSuspended = {};
	uint32 mLastAccelTimeSkew = {};
	bool mbReceiveChecksumOK = {};
	uint32 mTransferLength = {};
	uint32 mTransferCompleteRotPos = {};
	uint8 mFDCStatus = {};
	uint8 mOriginalDevice = {};
	uint8 mOriginalCommand = {};
	bool mbActiveCommandHighSpeed = {};
	bool mbActiveCommandWait = {};
	float mActiveCommandStartRotPos = {};
	float mActiveCommandSearchStartRotPos = {};
	uint8 mCustomCodeState = {};
	uint32 mPhantomSectorCounter = {};
	uint32 mWeakBitLFSR = {};
	uint32 mSectorsPerTrack = {};
	uint32 mTrackCount = {};
	uint32 mSideCount = {};
	bool mbMFM = {};
	bool mbHighDensity = {};
	bool mbTrackBufferingEnabled = {};
	bool mbCommandValid = {};
	bool mbCommandFrameHighSpeed = {};
	bool mbLastOpError = {};
	int mBootSectorCount = {};
	int mTotalSectorCount = {};
	int mSectorSize = {};
	uint32 mLastSector = {};
	int mFormatSectorSize = {};
	int mFormatSectorCount = {};
	int mFormatBootSectorCount = {};
	uint32 mCyclesPerSIOBitCurrent = {};
	uint32 mCyclesPerSIOByteCurrent = {};
	vdvector<uint8> mSendPacket;
	vdvector<uint8> mReceivePacket;
	vdvector<uint8> mDriveRAM;
	vdvector<sint8> mForcedSectorOrder;
	vdvector<uint32> mPhantomSectorCounters;
	uint8 mPERCOM[12] {};

	vdrefptr<IATObjectState> mpActiveCommand;
};

void ATDiskEmulator::SaveState(IATObjectState **pp) const {
	// MERGE NOTE: timers are relative to now, not the motor deadline.
	const uint32 t = mpScheduler->GetTick();
	vdrefptr<ATSaveStateDisk> state(new ATSaveStateDisk);

	if (mpMotorOffEvent)
		state->mIdleTimer = (uint32)std::max<sint32>(1, (sint32)(mMotorOffTime - t));
	else
		state->mIdleTimer = 0;

	if (mActiveCommand)
		state->mActiveCommandTimer = t - mActiveCommandStartTime;
	else
		state->mActiveCommandTimer = 0;

	state->mRotationalPos = (float)GetUpdatedRotationalCounter() / (float)mpProfile->mCyclesPerDiskRotation;
	state->mActiveCommandId = mActiveCommand;
	state->mActiveCommandState = mActiveCommandState;
	state->mActiveCommandSector = mActiveCommandSector;
	state->mActiveCommandPhysSectorPlus1 = mActiveCommandPhysSector + 1;
	state->mbActiveCommandBuffered = mbActiveCommandBufferingEnabled;
	state->mbActiveCommandReadTrack = mbActiveCommandBufferingReadTrackDelay;
	state->mbActiveCommandReadError = mbActiveCommandBufferingReadError;
	state->mbActiveCommandWriteTrack = mbActiveCommandBufferingWriteTrackDelay;

	state->mCurrentTrack = mCurrentTrack;
	state->mBufferedTrack = mBufferedTrack;
	state->mLastReadSector = mLastReadSector;

	state->mbExactRuntime = true;
	// Indus firmware upload changes runtime timing without changing the
	// user's configured drive model. Preserve that firmware choice too.
	state->mRuntimeFirmwareProfile =
		mpProfile == &ATGetDiskProfileIndusGTSynchromesh() ? 1 :
		mpProfile == &ATGetDiskProfileIndusGTSuperSynchromesh() ? 2 : 0;
	state->mMotorEventDelay = mpMotorOffEvent
		? mpSlowScheduler->GetTicksToEvent(mpMotorOffEvent) : 0;
	state->mMotorDeadlineOffset = mMotorOffTime - t;
	state->mRotationalCounter = mRotationalCounter;
	state->mRotationUpdateOffset = mLastRotationUpdateCycle - t;
	state->mRotations = mRotations;
	state->mExactCommandStartOffset = mActiveCommandStartTime - t;
	state->mExactCommandSector = mActiveCommandSector;
	state->mExactLastReadSector = mLastReadSector;
	state->mbMotorOffTimeSuspended = mbMotorOffTimeSuspended;
	state->mLastAccelTimeSkew = mLastAccelTimeSkew;
	state->mbReceiveChecksumOK = mbReceiveChecksumOK;
	state->mTransferLength = mTransferLength;
	state->mTransferCompleteRotPos = mTransferCompleteRotPos;
	state->mFDCStatus = mFDCStatus;
	state->mOriginalDevice = mOriginalDevice;
	state->mOriginalCommand = mOriginalCommand;
	state->mbActiveCommandHighSpeed = mbActiveCommandHighSpeed;
	state->mbActiveCommandWait = mbActiveCommandWait;
	state->mActiveCommandStartRotPos = mActiveCommandStartRotPos;
	state->mActiveCommandSearchStartRotPos = mActiveCommandSearchStartRotPos;
	state->mCustomCodeState = mCustomCodeState;
	state->mPhantomSectorCounter = mPhantomSectorCounter;
	state->mWeakBitLFSR = mWeakBitLFSR;
	state->mSectorsPerTrack = mSectorsPerTrack;
	state->mTrackCount = mTrackCount;
	state->mSideCount = mSideCount;
	state->mbMFM = mbMFM;
	state->mbHighDensity = mbHighDensity;
	state->mbTrackBufferingEnabled = mbTrackBufferingEnabled;
	state->mbCommandValid = mbCommandValid;
	state->mbCommandFrameHighSpeed = mbCommandFrameHighSpeed;
	state->mbLastOpError = mbLastOpError;
	state->mBootSectorCount = mBootSectorCount;
	state->mTotalSectorCount = mTotalSectorCount;
	state->mSectorSize = mSectorSize;
	state->mLastSector = mLastSector;
	state->mFormatSectorSize = mFormatSectorSize;
	state->mFormatSectorCount = mFormatSectorCount;
	state->mFormatBootSectorCount = mFormatBootSectorCount;
	state->mCyclesPerSIOBitCurrent = mCyclesPerSIOBitCurrent;
	state->mCyclesPerSIOByteCurrent = mCyclesPerSIOByteCurrent;
	state->mSendPacket.assign(std::begin(mSendPacket), std::end(mSendPacket));
	state->mReceivePacket.assign(std::begin(mReceivePacket), std::end(mReceivePacket));
	state->mDriveRAM.assign(std::begin(mDriveRAM), std::end(mDriveRAM));
	std::copy(std::begin(mPERCOM), std::end(mPERCOM), state->mPERCOM);
	state->mForcedSectorOrder.reserve(mExtPhysSectors.size());
	for (const auto& sector : mExtPhysSectors)
		state->mForcedSectorOrder.push_back(sector.mForcedOrder);
	state->mPhantomSectorCounters.reserve(mExtVirtSectors.size());
	for (const auto& sector : mExtVirtSectors)
		state->mPhantomSectorCounters.push_back(sector.mPhantomSectorCounter);

	mpSIOInterface->SaveActiveCommandState(~state->mpActiveCommand);

	*pp = state.release();
}

void ATDiskEmulator::LoadState(const IATObjectState& state0) {
	const ATSaveStateDisk& state = atser_cast<const ATSaveStateDisk&>(state0);
	const uint32 t = mpScheduler->GetTick();

	const ATDiskProfile* restoredProfile = &ATGetDiskProfile(mEmuMode);
	if (state.mbExactRuntime && state.mRuntimeFirmwareProfile) {
		if (mEmuMode != kATDiskEmulationMode_IndusGT)
			throw ATInvalidSaveStateException();
		restoredProfile = state.mRuntimeFirmwareProfile == 1
			? &ATGetDiskProfileIndusGTSynchromesh()
			: &ATGetDiskProfileIndusGTSuperSynchromesh();
	}

	// A rollback must restore the same mounted geometry. Check before
	// installing any bytes; never resize sector metadata to untrusted sizes.
	if (state.mbExactRuntime &&
		(state.mForcedSectorOrder.size() != mExtPhysSectors.size()
		|| state.mPhantomSectorCounters.size() != mExtVirtSectors.size()
		|| state.mRotationalCounter >= restoredProfile->mCyclesPerDiskRotation))
		throw ATInvalidSaveStateException();

	if (state.mbExactRuntime)
		mpProfile = restoredProfile;

	if (state.mIdleTimer > 0) {
		TurnOnMotor();

		mMotorOffTime = t + state.mIdleTimer;
		SetMotorEvent();
	} else {
		TurnOffMotor();
	}

	uint32 rotPosCycles = (uint32)((state.mRotationalPos - floorf(state.mRotationalPos)) * (float)mpProfile->mCyclesPerDiskRotation + 0.5f);

	if (rotPosCycles < mRotationalCounter)
		++mRotations;

	mRotationalCounter = rotPosCycles;
	mLastRotationUpdateCycle = ATSCHEDULER_GETTIME(mpScheduler);

	mActiveCommand = state.mActiveCommandId;
	mActiveCommandState = state.mActiveCommandState;
	mActiveCommandSector = state.mActiveCommandSector;
	mActiveCommandPhysSector = (sint32)state.mActiveCommandPhysSectorPlus1 - 1;
	mbActiveCommandBufferingEnabled = state.mbActiveCommandBuffered;
	mbActiveCommandBufferingReadTrackDelay = state.mbActiveCommandReadTrack;
	mbActiveCommandBufferingReadError = state.mbActiveCommandReadError;
	mbActiveCommandBufferingWriteTrackDelay = state.mbActiveCommandWriteTrack;
	mCurrentTrack = state.mCurrentTrack;
	mBufferedTrack = state.mBufferedTrack;
	mLastReadSector = state.mLastReadSector;

	if (mActiveCommand)
		mActiveCommandStartTime = t - state.mActiveCommandTimer;

	mpSIOInterface->LoadActiveCommandState(state.mpActiveCommand);
	if (state.mbExactRuntime) {
		mRotationalCounter = state.mRotationalCounter;
		mLastRotationUpdateCycle = t + state.mRotationUpdateOffset;
		mRotations = state.mRotations;
		mActiveCommandStartTime = t + state.mExactCommandStartOffset;
		mActiveCommandSector = state.mExactCommandSector;
		mLastReadSector = state.mExactLastReadSector;
		mbMotorOffTimeSuspended = state.mbMotorOffTimeSuspended;
		mLastAccelTimeSkew = state.mLastAccelTimeSkew;
		mbReceiveChecksumOK = state.mbReceiveChecksumOK;
		mTransferLength = state.mTransferLength;
		mTransferCompleteRotPos = state.mTransferCompleteRotPos;
		mFDCStatus = state.mFDCStatus;
		mOriginalDevice = state.mOriginalDevice;
		mOriginalCommand = state.mOriginalCommand;
		mbActiveCommandHighSpeed = state.mbActiveCommandHighSpeed;
		mbActiveCommandWait = state.mbActiveCommandWait;
		mActiveCommandStartRotPos = state.mActiveCommandStartRotPos;
		mActiveCommandSearchStartRotPos = state.mActiveCommandSearchStartRotPos;
		mCustomCodeState = state.mCustomCodeState;
		mPhantomSectorCounter = state.mPhantomSectorCounter;
		mWeakBitLFSR = state.mWeakBitLFSR;
		mSectorsPerTrack = state.mSectorsPerTrack;
		mTrackCount = state.mTrackCount;
		mSideCount = state.mSideCount;
		mbMFM = state.mbMFM;
		mbHighDensity = state.mbHighDensity;
		mbTrackBufferingEnabled = state.mbTrackBufferingEnabled;
		mbCommandValid = state.mbCommandValid;
		mbCommandFrameHighSpeed = state.mbCommandFrameHighSpeed;
		mbLastOpError = state.mbLastOpError;
		mBootSectorCount = state.mBootSectorCount;
		mTotalSectorCount = state.mTotalSectorCount;
		mSectorSize = state.mSectorSize;
		mLastSector = state.mLastSector;
		mFormatSectorSize = state.mFormatSectorSize;
		mFormatSectorCount = state.mFormatSectorCount;
		mFormatBootSectorCount = state.mFormatBootSectorCount;
		mCyclesPerSIOBitCurrent = state.mCyclesPerSIOBitCurrent;
		mCyclesPerSIOByteCurrent = state.mCyclesPerSIOByteCurrent;
		std::copy(state.mSendPacket.begin(), state.mSendPacket.end(), mSendPacket);
		std::copy(state.mReceivePacket.begin(), state.mReceivePacket.end(), mReceivePacket);
		std::copy(state.mDriveRAM.begin(), state.mDriveRAM.end(), mDriveRAM);
		std::copy(std::begin(state.mPERCOM), std::end(state.mPERCOM), mPERCOM);
		for (size_t i = 0; i < mExtPhysSectors.size(); ++i)
			mExtPhysSectors[i].mForcedOrder = state.mForcedSectorOrder[i];
		for (size_t i = 0; i < mExtVirtSectors.size(); ++i)
			mExtVirtSectors[i].mPhantomSectorCounter = state.mPhantomSectorCounters[i];
		mMotorOffTime = t + state.mMotorDeadlineOffset;
		mpSlowScheduler->UnsetEvent(mpMotorOffEvent);
		if (state.mMotorEventDelay)
			mpSlowScheduler->SetEvent(state.mMotorEventDelay, this,
				kATDiskEventMotorOff, mpMotorOffEvent);
	}
}

uint32 ATDiskEmulator::GetNetplayDeterminismFingerprint() const {
	if (!mbEnabled)
		return 0;

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

	// Live rotational counter (advances by delta-since-last-update,
	// so deterministic relative to cold reset).  This is the field
	// that drives accurate-sector-timing — the divergence vector that
	// caused the .atr boot desync at frame 29.  Reset() (line ~199)
	// seeds mRotationalCounter from g_ATRandomizationSeeds.mDiskStartPos
	// (locked under the netplay random seed), so both peers start with
	// the same offset.
	h = fold32(h, GetUpdatedRotationalCounter());
	h = fold32(h, mRotations);

	// Active SIO command — mActiveCommand is reset to 0 by AbortCommand
	// in Reset() (line 216).  When a command IS active both peers see
	// the same sequence of SIO bytes (lockstep guarantees inputs match)
	// so it stays in sync.
	h = fold8(h, mActiveCommand);

	// FDC status + mechanical position — both set deterministically by
	// Reset() based on the emulation mode.
	h = fold8(h, mFDCStatus);
	h = fold32(h, (uint32)mCurrentTrack);

	// Active transfer length — reset to 0 by Reset() (line 196).
	h = fold32(h, mTransferLength);

	// NOTE: mActiveCommandState, mActiveCommandSector, mOriginalCommand,
	// and mOriginalDevice are deliberately NOT folded in.  Reset() does
	// not clear them (they're only overwritten when a new SIO command
	// arrives), so two peers with different pre-session SIO activity
	// would carry forward different values and trip a false-positive
	// frame-0 desync.  These fields are inert until the next command
	// — at which point they're rewritten from the SIO command frame,
	// which is byte-identical on both peers under lockstep.

	if (h == 0) h = 1;
	return h;
}

void ATDiskEmulator::OnScheduledEvent(uint32 id) {
	if (id == kATDiskEventMotorOff) {
		mpMotorOffEvent = nullptr;

		// Motor idling is driven by the main loop of the drive, and as such can't
		// occur during command processing. Furthermore, the timer is also usually
		// driven in software, and so can't even count down outside of idle. If
		// we hit the timer during a command, requeue the event and retry later;
		// once the command ends the timer will be readjusted.

		const uint32 t = mpScheduler->GetTick();
		if (mbMotorOffTimeSuspended) {
			mpSlowScheduler->SetEvent(16, this, kATDiskEventMotorOff, mpMotorOffEvent);
		} else if (ATWrapTime{t} >= mMotorOffTime)
			TurnOffMotor();
		else
			SetMotorEvent();
	}
}

void ATDiskEmulator::InitSIO(IATDeviceSIOManager *mgr) {
	mpSIOMgr = mgr;
}

IATDeviceSIO::CmdResponse ATDiskEmulator::OnSerialBeginCommand(const ATDeviceSIOCommand& cmd) {
	if (!mbEnabled)
		return kCmdResponse_NotHandled;

	// check if it's us
	if (cmd.mDevice != (uint8)(0x31 + mUnit))
		return kCmdResponse_NotHandled;
			
	// Check the speed of the command frame and make sure the transmission is at the
	// correct rate. It must be either 19,200 baud (divisor=$28) or the high speed
	// command rate for the XF551 and IndusGT. We allow up to a 5% deviation in transfer rate.
	if (cmd.mbStandardRate) {
		mbCommandFrameHighSpeed = false;
	} else {
		if (!mpProfile->mbSupportedCmdHighSpeedUSD ||
			cmd.mCyclesPerBit < mpProfile->mHighSpeedUSDCmdFrameRateLo ||
			cmd.mCyclesPerBit > mpProfile->mHighSpeedUSDCmdFrameRateHi)
		{
			g_ATLCDiskCmd("Rejecting command sent at wrong rate (cycles per bit = %d, expected [%d,%d])\n"
				, cmd.mCyclesPerBit
				, mpProfile->mHighSpeedUSDCmdFrameRateLo
				, mpProfile->mHighSpeedUSDCmdFrameRateHi
			);

			return kCmdResponse_NotHandled;
		} else {
			mbCommandFrameHighSpeed = true;
		}
	}

	mpDiskInterface->SetShowActivity(true, mLastSector);

	UpdateRotationalCounter();

	// interpret the command
	mbCommandFrameHighSpeed = !cmd.mbStandardRate;

	g_ATLCDiskCmd("Processing command: Unit %02X, Command %02X, Aux data %02X %02X%s\n"
		, cmd.mDevice
		, cmd.mCommand
		, cmd.mAUX[0]
		, cmd.mAUX[1]
		, mbCommandFrameHighSpeed ? " (high-speed command frame)" : ""
	);
	const uint8 command = cmd.mCommand;
	bool highSpeed = mbCommandFrameHighSpeed || (command & 0x80) != 0;

	UpdateRotationalCounter();
	mActiveCommandStartRotPos = (float)mRotations + (float)mRotationalCounter / (float)mpProfile->mCyclesPerDiskRotation;
	mActiveCommandStartTime = mpScheduler->GetTick();

	// check if this is a 1050 Turbo command
	mActiveCommandSector = cmd.mAUX[0] + cmd.mAUX[1] * 256;

	if (mEmuMode == kATDiskEmulationMode_1050Turbo && (mActiveCommandSector & 0x8000)) {
		switch(command) {
			case 0x4E:	// read PERCOM block
			case 0x4F:	// write PERCOM block
			case 0x52:	// read
			case 0x53:	// status (used by MyPicoDOS to autodetect)
			case 0x50:	// put (without verify)
			case 0x57:	// write (with verify)
				mActiveCommandSector &= 0x7FFF;
				mbCommandFrameHighSpeed = true;
				highSpeed = true;
				break;
		}
	}

	mOriginalDevice = cmd.mDevice;
	mOriginalCommand = command;
	mbActiveCommandHighSpeed = highSpeed;
	mActiveCommandState = 0;
	mbActiveCommandWait = false;

	mbActiveCommandBufferingEnabled = false;
	mbActiveCommandBufferingWriteTrackDelay = false;
	mbActiveCommandBufferingReadTrackDelay = false;
	mbActiveCommandBufferingReadError = false;

	mbMotorOffTimeSuspended = true;

	// must be before BeginCommand() as we will get some time skew there
	mLastAccelTimeSkew = mpSIOInterface->GetAccelTimeSkew();

	mpSIOInterface->BeginCommand();

	// reject all high speed commands if not XF551 or generic

	if (!mpProfile->mbSupportedCmdHighSpeedXF && (command & 0x80))
		goto unsupported_command;

	switch(command) {
		case 0x53:	// status
		case 0xD3:	// status (XF551 high speed)
			mActiveCommand = 0x53;
			break;

		case 0x52:	// read
		case 0xD2:	// read (XF551 high speed)
			mActiveCommand = 0x52;
			break;

		case 0x72:	// read (Happy high speed)
			mActiveCommand = 0x72;
			mbActiveCommandHighSpeed = true;
			break;

		case 0x21:	// format
		case 0x22:	// format disk medium density
		case 0x66:	// format disk skewed
		case 0xA1:	// format (high speed skew, XF551/Synchromesh)
		case 0xA2:	// format disk medium density (high speed)
		case 0xA3:	// format boot tracks with normal skew (Synchromesh only)
		case 0xE6:	// format disk skewed (high speed)
			mActiveCommand = 0x21;
			break;

		case 0x50:	// put (without verify)
		case 0xD0:	// put (without verify) (XF551 high speed)
			mActiveCommand = 0x50;
			break;

		case 0x70:	// put (without verify) (Happy high speed)
			mActiveCommand = 0x50;
			break;

		case 0x57:	// write (with verify)
		case 0xD7:	// write (with verify) (XF551 high speed)
			mActiveCommand = 0x57;
			break;

		case 0x77:	// write (with verify) (Happy high speed)
			mActiveCommand = 0x77;
			mbActiveCommandHighSpeed = true;
			break;

		case 0x4E:	// read PERCOM block
		case 0xCE:	// read PERCOM block (XF551 high speed)
			mActiveCommand = 0x4E;
			break;

		case 0x4F:	// write PERCOM block
		case 0xCF:	// write PERCOM block (XF551 high speed)
			mActiveCommand = 0x4F;
			break;

		case 0x3F:	// get high speed index
			mActiveCommand = 0x3F;
			break;

		case 0x48:
			mActiveCommand = 0x48;
			break;

		case 0x58:
			mActiveCommand = 0x58;
			break;

		case 0x54:
			mActiveCommand = 0x54;
			break;

		case 0x28:	// Happy head positioning test recalibrate
		case 0x29:	// Happy head positioning test seek
		case 0x2D:	// Happy RPM test
		case 0x51:
			mActiveCommand = command;
			break;

		default:
unsupported_command:
			ProcessUnsupportedCommand();
			break;
	}

	if (mActiveCommand)
		ProcessCommand();

	mpDiskInterface->SetShowActivity(mActiveCommand != 0, mLastSector);
	return kCmdResponse_Start;
}

void ATDiskEmulator::OnSerialAbortCommand() {
	if (mActiveCommand) {
		ExtendMotorTimeoutBy(mpScheduler->GetTick() - mActiveCommandStartTime);

		AbortCommand();
	}

	mbMotorOffTimeSuspended = false;
}

void ATDiskEmulator::OnSerialReceiveComplete(uint32 id, const void *data, uint32 len, bool checksumOK) {
	if (!checksumOK) {
		mpSIOInterface->FlushQueue();
		BeginTransferNAKData();
		EndCommand();
		return;
	}

	memcpy(mReceivePacket, data, len);
	mbReceiveChecksumOK = checksumOK;
}

void ATDiskEmulator::OnSerialFence(uint32 id) {
	if (id == (uint32)0 - 1) {
		ExtendMotorTimeoutBy(mpScheduler->GetTick() - mActiveCommandStartTime);

		UpdateAccelTimeSkew();
		mpDiskInterface->SetShowActivity(false, mLastSector);

		if (mpTraceChannel) {
			if (mOriginalCommand == 0x52) {
				mpTraceChannel->AddTickEvent(mpSIOInterface->GetCommandDeassertTime(), mpScheduler->GetTick64(),
					[sec = (uint16)mActiveCommandSector](VDStringW& s) {
						s.sprintf(L"Read %u", sec);
					},
					kATTraceColor_IO_Read
				);
			} else {
				mpTraceChannel->AddTickEvent(mpSIOInterface->GetCommandDeassertTime(), mpScheduler->GetTick64(),
					[c = (uint8)mOriginalCommand, sec = (uint16)mActiveCommandSector](VDStringW& s) {
						s.sprintf(L"%02X:%u", c, sec);
					},
					kATTraceColor_IO_Default
				);
			}
		}

		mbMotorOffTimeSuspended = false;
	} else {
		mActiveCommandState = id;
		mbActiveCommandWait = false;
		ProcessCommand();
	}
}

IATDeviceSIO::CmdResponse ATDiskEmulator::OnSerialAccelCommand(const ATDeviceSIORequest& request) {
	return OnSerialBeginCommand(request);
}

void ATDiskEmulator::OnDiskChanging() {
	if (mpRotationTracer)
		mpRotationTracer->SetDiskImage(nullptr);
}

void ATDiskEmulator::OnDiskChanged(bool mediaRemoved) {
	if (mpRotationTracer)
		mpRotationTracer->SetDiskImage(mpDiskInterface->GetDiskImage());

	InitSectorInfoArrays();
	DetectDensity();
	mCurrentTrack = mTrackCount - 1;

	// invalidate a read/write operation if it's currently happening
	mActiveCommandPhysSector = -1;
	mFDCStatus = 0xEF;

	// if no command is currently happening and the disk profile specifies
	// density detection on disk change, turn the motor on so programs can
	// sense it
	if (!mActiveCommand && mpDiskInterface->GetDiskImage() && mpProfile->mbDetectDensityOnDiskChange)
		TurnOnMotor();
}

void ATDiskEmulator::OnWriteModeChanged() {
	const auto mode = mpDiskInterface->GetWriteMode();

	mbWriteEnabled = (mode & kATMediaWriteMode_AllowWrite) != 0;
	mbFormatEnabled = (mode & kATMediaWriteMode_AllowFormat) != 0;
}

void ATDiskEmulator::OnTimingModeChanged() {
	mbAccurateSectorTiming = mpDiskInterface->IsAccurateSectorTimingEnabled();
}

void ATDiskEmulator::OnAudioModeChanged() {
	mbDriveSoundsEnabled = mpDiskInterface->AreDriveSoundsEnabled();

	if (!mbDriveSoundsEnabled && mpRotationSoundGroup)
		mpRotationSoundGroup->StopAllSounds();
}

bool ATDiskEmulator::IsImageSupported(const IATDiskImage& image) const {
	return true;
}

void ATDiskEmulator::UpdateAccelTimeSkew() {
	uint32 ats = mpSIOInterface->GetAccelTimeSkew();

	if (mLastAccelTimeSkew != ats) {
		mRotationalCounter += (ats - mLastAccelTimeSkew);

		UpdateRotationalCounter();

		if (mpRotationTracer)
			mpRotationTracer->Warp(mRotationalCounter);

		mLastAccelTimeSkew = ats;
	}
}

void ATDiskEmulator::InitSectorInfoArrays() {
	IATDiskImage *image = mpDiskInterface->GetDiskImage();
	if (!image) {
		mTotalSectorCount = 0;
		return;
	}

	const uint32 physCount = image->GetPhysicalSectorCount();
	mExtPhysSectors.resize(physCount);

	for(uint32 i = 0; i < physCount; ++i) {
		ExtPhysSector& psi = mExtPhysSectors[i];

		psi.mForcedOrder = -1;
	}

	const uint32 virtCount = image->GetVirtualSectorCount();
	mExtVirtSectors.resize(virtCount);

	mTotalSectorCount = virtCount;

	for(uint32 i=0; i<virtCount; ++i) {
		ExtVirtSector& vsi = mExtVirtSectors[i];

		vsi.mPhantomSectorCounter = 0;
	}

	mBootSectorCount = image->GetBootSectorCount();
	mSectorSize = image->GetSectorSize();
	mbAccurateSectorPrediction = (image->GetTimingMode() == kATDiskTimingMode_UsePrecise);
}

void ATDiskEmulator::SetupTransferSpeed(bool highSpeed) {
	if (highSpeed) {
		// Special case for Happy commands
		switch(mActiveCommand) {
			case 0x70:
			case 0x72:
			case 0x77:
				mCyclesPerSIOBitCurrent = kCyclesPerSIOBit_Happy_Native_Fast;
				mCyclesPerSIOByteCurrent = kCyclesPerSIOByte_Happy_Native_Fast;
				break;

			default:
				if (mbCommandFrameHighSpeed) {
					mCyclesPerSIOBitCurrent = mpProfile->mCyclesPerSIOBitHighSpeedUSD;
					mCyclesPerSIOByteCurrent = mpProfile->mCyclesPerSIOByteHighSpeedUSD;
				} else {
					mCyclesPerSIOBitCurrent = mpProfile->mCyclesPerSIOBitHighSpeedXF;
					mCyclesPerSIOByteCurrent = mpProfile->mCyclesPerSIOByteHighSpeedXF;
				}
				break;
		}
	} else {
		mCyclesPerSIOBitCurrent = mpProfile->mCyclesPerSIOBit;
		mCyclesPerSIOByteCurrent = mpProfile->mCyclesPerSIOByte;
	}

	mpSIOInterface->SetTransferRate(mCyclesPerSIOBitCurrent, mCyclesPerSIOByteCurrent);
}

void ATDiskEmulator::BeginTransferACKCmd() {
	SetupTransferSpeed(mbCommandFrameHighSpeed);
	mpSIOInterface->Delay(mpProfile->mCyclesToACKSent);
	mpSIOInterface->SendACK();
}

void ATDiskEmulator::BeginTransferACK() {
	SetupTransferSpeed(mbActiveCommandHighSpeed);
	mpSIOInterface->Delay(mpProfile->mCyclesToACKSent);
	mpSIOInterface->SendACK();
}

void ATDiskEmulator::BeginTransferComplete() {
	SetupTransferSpeed(mbActiveCommandHighSpeed);
	mpSIOInterface->SendComplete(false);
}

void ATDiskEmulator::BeginTransferError() {
	SetupTransferSpeed(mbActiveCommandHighSpeed);
	mpSIOInterface->SendError(false);
}

void ATDiskEmulator::BeginTransferNAKCommand() {
	SetupTransferSpeed(mbCommandFrameHighSpeed);

	// Typically the drive firmware does the following:
	//
	//	- Process the command.
	//	- Wait for the command line to deassert.
	//	- Send the NAK.
	//
	// It is possible for the first step to take longer than the computer to
	// deassert the command line, depending on the speed of the SIO routine.
	// Therefore, we must compute both delays from the end of the command frame
	// (precisely, the last data bit) and from the command line deasserting
	// and take the maximum of the two.
	//
	const uint64 t = mpSIOInterface->GetCommandQueueTime();
	const uint64 targetTime = std::max<uint64>(mpSIOInterface->GetCommandFrameEndTime() + mpProfile->mCyclesToNAKFromFrameEnd, mpSIOInterface->GetCommandDeassertTime() + mpProfile->mCyclesToNAKFromCmdDeassert);

	if (targetTime > t)
		mpSIOInterface->Delay((uint32)(targetTime - t));

	mpSIOInterface->SendNAK();
}

void ATDiskEmulator::BeginTransferNAKData() {
	// NAKs are only sent in response to the command itself and therefore must be sent at
	// command frame speed.
	SetupTransferSpeed(mbCommandFrameHighSpeed);
	mpSIOInterface->Delay(mpProfile->mCyclesToACKSent);
	mpSIOInterface->SendNAK();
}

void ATDiskEmulator::SendResult(bool successful, uint32 length) {
	if (successful)
		BeginTransferComplete();
	else
		BeginTransferError();

	if (mbActiveCommandHighSpeed) {
		if (mbCommandFrameHighSpeed)
			mpSIOInterface->Delay(mpProfile->mCyclesCEToDataFrameHighSpeedUSD + ((length * mpProfile->mCyclesCEToDataFrameHighSpeedUSDPBDiv256 + 128) >> 8));
		else
			mpSIOInterface->Delay(mpProfile->mCyclesCEToDataFrameHighSpeedXF + ((length * mpProfile->mCyclesCEToDataFrameHighSpeedXFPBDiv256 + 128) >> 8));
	} else
		mpSIOInterface->Delay(mpProfile->mCyclesCEToDataFrame + ((length * mpProfile->mCyclesCEToDataFramePBDiv256 + 128) >> 8));

	Send(length);
}

void ATDiskEmulator::Send(uint32 length) {
	mTransferLength = length;

	if (length) {
		SetupTransferSpeed(mbActiveCommandHighSpeed);
		mpSIOInterface->SendData(mSendPacket, length, true);
	}

	++mActiveCommandState;
}

void ATDiskEmulator::BeginReceive(uint32 len) {
	SetupTransferSpeed(mbActiveCommandHighSpeed);
	mpSIOInterface->ReceiveData(0, len, true);
	++mActiveCommandState;
}

void ATDiskEmulator::WarpOrDelay(uint32 cycles, uint32 minCycles) {
	if (!mbAccurateSectorTiming && cycles > minCycles) {
		mRotationalCounter += cycles - minCycles;
		if (mRotationalCounter >= mpProfile->mCyclesPerDiskRotation) {
			mRotationalCounter -= mpProfile->mCyclesPerDiskRotation;
			++mRotations;
		}

		if (mpRotationTracer)
			mpRotationTracer->Warp(mRotationalCounter);

		cycles = minCycles;
	}

	mpSIOInterface->Delay(cycles);
}

// This routine delays from the beginning of the stop bit of the last byte rather than
// the end of the stop bit or byte. This models optimizations in drive firmware where
// the firmware sets the output high for the stop bit and then immediately resumes
// processing, since there is no further work needed on the serial hardware. This
// simplifies specification of post-transmission delays with high speed since the
// width of the stop bit varies but the work done after the start of the stop bit
// largely doesn't.
void ATDiskEmulator::WarpOrDelayFromStopBit(uint32 cycles) {
	uint32 delay = std::max<uint32>(cycles, mCyclesPerSIOBitCurrent + 1) - mCyclesPerSIOBitCurrent;

	WarpOrDelay(delay, delay);
}

void ATDiskEmulator::Wait(uint32 nextState) {
	mbActiveCommandWait = true;
	mpSIOInterface->InsertFence(nextState);
}

void ATDiskEmulator::EndCommand() {
	mActiveCommand = 0;

	mpSIOInterface->InsertFence((uint32)0 - 1);
	mpSIOInterface->EndCommand();
}

void ATDiskEmulator::AbortCommand() {
	mActiveCommand = 0;
	mbMotorOffTimeSuspended = false;
}

uint32 ATDiskEmulator::GetUpdatedRotationalCounter() const {
	uint32 t = ATSCHEDULER_GETTIME(mpScheduler);
	uint32 dt = t - mLastRotationUpdateCycle;

	return (mRotationalCounter + dt) % mpProfile->mCyclesPerDiskRotation;
}

void ATDiskEmulator::UpdateRotationalCounter() {
	uint32 t = ATSCHEDULER_GETTIME(mpScheduler);
	uint32 dt = t - mLastRotationUpdateCycle;
	mLastRotationUpdateCycle = t;

	if (!mpMotorOffEvent)
		return;

	mRotationalCounter += dt;

	if (mRotationalCounter >= mpProfile->mCyclesPerDiskRotation) {
		uint32 rotations = mRotationalCounter / mpProfile->mCyclesPerDiskRotation;
		mRotationalCounter %= mpProfile->mCyclesPerDiskRotation;
		mRotations += rotations;
	}
}

void ATDiskEmulator::ProcessUnsupportedCommand() {
	BeginTransferNAKCommand();

	const char *extraDesc = "";

	switch(mOriginalCommand) {
		case 0x21:
		case 0x22:
		case 0x28:
		case 0x29:
		case 0x2D:
		case 0x3F:
		case 0x48:
		case 0x4E:
		case 0x4F:
		case 0x51:
		case 0x54:
		case 0x58:
		case 0x66:
		case 0x70:
		case 0x72:
		case 0x77:
			extraDesc = "; not supported by current profile";
			break;
	}

	uint8 aux[2];
	VDWriteUnalignedLEU16(aux, mActiveCommandSector);
	g_ATLCDisk("Unsupported command %02X (%s%s)\n", mOriginalCommand, ATDecodeSIOCommand(mOriginalDevice, mOriginalCommand, aux), extraDesc);

	EndCommand();
}

void ATDiskEmulator::ProcessCommand() {
	while(mActiveCommand && !mbActiveCommandWait) {
		UpdateAccelTimeSkew();

		switch(mActiveCommand) {
			case 0x21:
				ProcessCommandFormat();
				break;

			case 0x3F:
				ProcessCommandGetHighSpeedIndex();
				break;

			case 0x48:
				ProcessCommandHappy();
				break;

			case 0x4E:
				ProcessCommandReadPERCOMBlock();
				break;

			case 0x4F:
				ProcessCommandWritePERCOMBlock();
				break;

			case 0x52:
			case 0x72:
				ProcessCommandRead();
				break;

			case 0x53:
				ProcessCommandStatus();
				break;

			case 0x50:		// put
			case 0x57:		// write
			case 0x70:		// Happy high-speed put
			case 0x77:		// Happy high-speed write
				ProcessCommandWrite();
				break;

			case 0x58:		// Indus GT execute
				ProcessCommandExecuteIndusGT();
				break;

			case 0x51:
				ProcessCommandHappyQuiet();
				break;

			case 0x54:
				ProcessCommandHappyRAMTest();
				break;

			case 0x28:
			case 0x29:
				ProcessCommandHappyHeadPosTest();
				break;

			case 0x2D:
				ProcessCommandHappyRPMTest();
				break;
		}
	}
}

///////////////////////////////////////////////////////////////////////////
// Status command ($58/$D8)
//
// On the 810:
//		ACK - 322 device cycles
//		Setup - 155 device cycles
//		Transmit C + status bytes
//
// There is NO delay between C and the status bytes on the 810. The SIO
// routine must be ready to receive the data frame immediately.
//
// The 1050 issues a force interrupt command to the FDC twice before sending
// the C/E + data frame. We don't currently have timing for this.
//
void ATDiskEmulator::ProcessCommandStatus() {
	BeginTransferACKCmd();

	WarpOrDelayFromStopBit(mpProfile->mCyclesACKStopBitToStatusComplete);

	uint8 status = 0;

	// We need to check the sector size in the PERCOM block and not the physical
	// disk for this value. This is required as SmartDOS 8.2D does a Write PERCOM
	// Block command and then depends on FM/MFM selection being reflected in the
	// result.
	if (mPERCOM[6])		// sector size high byte
		status += 0x20;

	if (mbLastOpError)
		status += 0x04;

	if (!mbWriteEnabled)
		status += 0x08;

	if (mSideCount > 1)
		status += 0x40;

	if (mSectorsPerTrack == 26)
		status += 0x80;

	if (mpMotorOffEvent)
		status += 0x10;

	mSendPacket[0] = status;
	mSendPacket[1] = mFDCStatus;

	if (!mpProfile->mbSupportedNotReady)
		mSendPacket[1] |= 0x80;
	else if (mTotalSectorCount == 0)
		mSendPacket[1] &= 0x7F;

	mSendPacket[2] = mEmuMode == kATDiskEmulationMode_XF551 ? 0xfe : 0xe0;
	mSendPacket[3] = 0x00;

	SendResult(true, 4);
	EndCommand();
}

bool ATDiskEmulator::ProcessCommandReadWriteCommon(bool isWrite) {
	const uint32 sector = mActiveCommandSector;

	switch(mActiveCommandState) {
		case 10: {
			uint32 opDelay = 0;

			// check if we need to seek (spt=0 may be true if we lost the disk)
			uint32 track = mSectorsPerTrack ? (sector - 1) / mSectorsPerTrack : 0;
			int trackDelta = (int)track - (int)mCurrentTrack;
			uint32 tracksToStep = (uint32)abs(trackDelta);

			if (mpProfile->mbBufferTrackReads && mbTrackBufferingEnabled) {
				if (!isWrite) {
					mbActiveCommandBufferingEnabled = true;

					// The Happy 1050 will only force a re-read of a buffered track on request for
					// a sector with an error if that request is a re-read of the last requested
					// sector. Otherwise, it will just return a buffered read error.
					if (mLastReadSector != sector && mpProfile->mbBufferTrackReadErrors) {
						mLastReadSector = sector;
					} else {
						IATDiskImage *image = mpDiskInterface->GetDiskImage();
						ATDiskVirtualSectorInfo vsi {};
						if (image)
							image->GetVirtualSectorInfo(sector - 1, vsi);

						if (!vsi.mNumPhysSectors)
							mbActiveCommandBufferingReadError = true;
						else {
							bool anyGood = false;

							for(const auto& psec : image->GetPhysicalSectorsForVirtualSector(sector - 1)) {
								if (psec.mPhysInfo.mFDCStatus == 0xFF) {
									anyGood = true;
									break;
								}
							}

							if (!anyGood)
								mbActiveCommandBufferingReadError = true;
						}
					}

					if (!mbActiveCommandBufferingReadError && sector == 1 && mpProfile->mbBufferSector1) {
						// sector 1 is read-buffered on this firmware, don't need to
						// check or populate track read buffer
					} else if (mBufferedTrack != (sint32)track) {
						mBufferedTrack = (sint32)track;
						mbActiveCommandBufferingReadTrackDelay = true;

						g_ATLCDisk("Buffering track %d.\n", mBufferedTrack);
					}
				}
			}

			// add command delay until FDC command issued
			opDelay += kCyclesFDCCommandDelay;

			// check if mech activity needed
			if (!mbActiveCommandBufferingEnabled || mbActiveCommandBufferingReadTrackDelay || mbActiveCommandBufferingReadError || mbActiveCommandBufferingWriteTrackDelay) {
				// we need to read or write a track -- turn on the motor and set up
				// for seek
				if (TurnOnMotor())
					opDelay += 7159090/8;

				mCurrentTrack = track;

				if (mpRotationTracer)
					mpRotationTracer->SetTrack(mCurrentTrack);

				// emulate seek
				if (tracksToStep) {
					// The 1050 drive does an extra pair of half steps after a forward seek, one forward
					// and one backward. This ensures that tracks are always read or written after the
					// head has seeked backwards. The 810 does not do this.
					if (trackDelta > 0 && mpProfile->mbReverseOnForwardSeeks) {
						PlaySeekSound(opDelay, tracksToStep + 1);
						opDelay += (tracksToStep + 1) * mpProfile->mCyclesPerTrackStep;
					} else {
						PlaySeekSound(opDelay, tracksToStep);
						opDelay += tracksToStep * mpProfile->mCyclesPerTrackStep;
					}

					opDelay += mpProfile->mCyclesForHeadSettle;
				}
			}


			WarpOrDelay(opDelay);
			Wait(13);
			break;
		}

		case 11:
			// This is a vestigial state that was merged into state 10.
			mActiveCommandState = 13;
			[[fallthrough]];

		case 13:	// initial state
		case 14:	// retry states....
		case 15:
		case 16: {
			UpdateRotationalCounter();

			// get virtual sector information
			IATDiskImage *image = mpDiskInterface->GetDiskImage();

			ATDiskVirtualSectorInfo vsi {};
			if (image)
				image->GetVirtualSectorInfo(sector - 1, vsi);

			ExtVirtSector& evs = mExtVirtSectors[sector - 1];

			// check if we have any sectors
			ATDiskPhysicalSectorInfo psi;
			uint32 postSeekPosition = 0;
			uint32 physSector;
			bool foundSector = false;
			bool foundValidAddressField = false;
			bool foundInvalidAddressField = false;

			if (vsi.mNumPhysSectors) {
				// choose a physical sector
				if (mbAccurateSectorPrediction || mbAccurateSectorTiming) {
					// compute post-seek rotational position
					postSeekPosition = mRotationalCounter % mpProfile->mCyclesPerDiskRotation;
					mActiveCommandSearchStartRotPos = mRotations + (float)postSeekPosition / (float)mpProfile->mCyclesPerDiskRotation;

					uint32 bestDelay = 0xFFFFFFFFU;

					physSector = vsi.mFirstPhysSector;

					for(const auto& psec : image->GetPhysicalSectorsForVirtualSector(sector - 1)) {
						// ignore sectors that have a different density than current
						if (psec.mPhysInfo.mbMFM != mbMFM)
							continue;

						// if sector has invalid address field, temporarily flag address CRC error and skip it
						if ((psec.mPhysInfo.mFDCStatus & 0x18) == 0) {
							foundInvalidAddressField = true;
							continue;
						}

						// if sector has a missing data field AND we are doing a read, ignore the sector;
						// writes ignore this as they overwrite the data field
						if (!isWrite && (psec.mPhysInfo.mFDCStatus & 0x10) == 0)
							continue;

						foundValidAddressField = true;

						const ExtPhysSector& eps = mExtPhysSectors[psec.mPhysIndex];

						// compute sector's rotational position in cycles
						uint32 sectorPos = VDRoundToInt(psec.mPhysInfo.mRotPos * mpProfile->mCyclesPerDiskRotation);

						// compute rotational delay to sector
						uint32 delay = sectorPos < postSeekPosition ? sectorPos + mpProfile->mCyclesPerDiskRotation - postSeekPosition : sectorPos - postSeekPosition;

						if (eps.mForcedOrder == evs.mPhantomSectorCounter) {
							physSector = psec.mPhysIndex;
							mPhantomSectorCounter = psec.mPhysOrder;

							if (++evs.mPhantomSectorCounter >= vsi.mNumPhysSectors)
								evs.mPhantomSectorCounter = 0;
							break;
						}

						if (delay < bestDelay) {
							bestDelay = delay;
							foundSector = true;

							physSector = psec.mPhysIndex;
							mPhantomSectorCounter = psec.mPhysOrder;
						}
					}
				} else {
					uint32 phantomIdx = evs.mPhantomSectorCounter;

					for(const auto& psec : image->GetPhysicalSectorsForVirtualSector(sector - 1)) {
						const ExtPhysSector& eps = mExtPhysSectors[psec.mPhysIndex];

						if (eps.mForcedOrder == evs.mPhantomSectorCounter) {
							phantomIdx = psec.mPhysOrder;
							break;
						}
					}

					uint32 curPhysIndex = vsi.mFirstPhysSector;
					for(uint32 i = 0; i < phantomIdx; ++i)
						curPhysIndex = image->GetNextPhysicalSector(curPhysIndex);

					physSector = curPhysIndex;
					foundSector = true;
					foundValidAddressField = true;

					if (++evs.mPhantomSectorCounter >= vsi.mNumPhysSectors)
						evs.mPhantomSectorCounter = 0;
				}
			}

			if (foundSector) {
				image->GetPhysicalSectorInfo(physSector, psi);

				// Set FDC status.
				//
				// The lost data condition (bit 2) needs to be handled specially here, as it results
				// from reading a long sector, and the 810 and 1050 differ in behavior. The 810 reads
				// status immediately after 128 bytes, so it will see DRQ but not lost data, and not
				// a CRC error as the CRC hasn't been read yet. A 1050, on the other hand, will wait
				// until the sector has finished reading, so lost data and DRQ won't be on, but
				// the CRC flag will be correct. The Music Studio and Fight Night are sensitive to
				// behavior here.
				if (isWrite) {
					mFDCStatus = 0xFF;
				} else {
					mFDCStatus = psi.mFDCStatus;

					// Clone the deleted bit to both record type bits for the 810 for reads, since
					// currently our FDC status is for the 1050.
					switch(mEmuMode) {
						case kATDiskEmulationMode_Generic:
						case kATDiskEmulationMode_Generic57600:
						case kATDiskEmulationMode_810:
							mFDCStatus &= 0xBF;
							mFDCStatus += (mFDCStatus & 0x20) << 1;
							break;
					}

					// Record Not Found + CRC means that the address header had a CRC error. In
					// that case, we never make it to the data frame, so we need to skip this logic.
					if (!(mFDCStatus & 0x18)) {
						// Ignore long sector related errors.
						mFDCStatus |= 0x06;
					} else {
						switch(mEmuMode) {
							case kATDiskEmulationMode_Generic:
							case kATDiskEmulationMode_Generic57600:
							case kATDiskEmulationMode_810:
							case kATDiskEmulationMode_Happy810:
								if (!(mFDCStatus & 0x04)) {
									// assert BSY and DRQ -- read still pending, byte ready
									mFDCStatus &= ~0x03;

									// negate CRC (not read yet) and lost data (not two bytes passed yet)
									mFDCStatus |= 0x0C;
								}
								break;

							case kATDiskEmulationMode_FastestPossible:
							case kATDiskEmulationMode_Happy1050:
							case kATDiskEmulationMode_1050:
							case kATDiskEmulationMode_XF551:
							case kATDiskEmulationMode_USDoubler:
							case kATDiskEmulationMode_Speedy1050:
							case kATDiskEmulationMode_IndusGT:
							case kATDiskEmulationMode_1050Turbo:
								if (!(mFDCStatus & 0x04)) {
									// assert DRQ (byte not read)
									mFDCStatus &= ~0x02;

									// negate BSY (read is completed)
									mFDCStatus |= 0x01;
								}
								break;
						}
					}
				}

				// set rotational delay
				mTransferCompleteRotPos = VDRoundToInt(psi.mRotPos * mpProfile->mCyclesPerDiskRotation);
				mActiveCommandPhysSector = physSector;
			} else {
				// Indicate missing sector (record not found).
				//
				// If we found only sectors with address CRC errors, then the status will reflect
				// RNF+CRC, otherwise it'll just be RNF. It's actually the last sector that matters,
				// but we're fibbing a little bit here.
				mFDCStatus = foundInvalidAddressField && !foundValidAddressField ? 0xE7 : 0xEF;
				mActiveCommandPhysSector = -1;
				mTransferCompleteRotPos = mRotationalCounter;
			}

			uint32 secondByteDelay = 0;

			// if buffering is enabled, there is no additional rotational delay to access the
			// sector, but there may be a rotational delay to flush/read the track
			if (mbActiveCommandBufferingEnabled) {
				// modify the delays if we are actually doing a buffered read; skip it if the track
				// is buffered and we've hit read errors on a firmware that doesn't buffer read errors
				const bool firstAttempt = (mActiveCommandState == 13);
				if ((firstAttempt && mbActiveCommandBufferingReadTrackDelay)
					|| mpProfile->mbBufferTrackReadErrors
					|| !mbActiveCommandBufferingReadError)
				{
					mTransferCompleteRotPos = mRotationalCounter;

					if (mbActiveCommandBufferingReadTrackDelay || mbActiveCommandBufferingReadError) {
						if (mFDCStatus != 0xFF)
							secondByteDelay += mActiveCommandState == 13 ? (7159090*1)/4 : (7159090*5)/4;
						else
							secondByteDelay += mpProfile->mCyclesPerDiskRotation;
					}
				}
			}

			const bool missingSector = !(mFDCStatus & 0x10);

			// If we have the sector, add rotational delay from the post seek position to the
			// sector's position; otherwise, add two revs for the FDC's attempt to find it.
			if (missingSector) {
				if (mpProfile->mbRetryMode1050)
					secondByteDelay += kCyclesPerFakeRot_1050;
				else
					secondByteDelay += kCyclesPerFakeRot_810;
			} else {
				if (postSeekPosition > mTransferCompleteRotPos)
					secondByteDelay += mpProfile->mCyclesPerDiskRotation;

				secondByteDelay += mTransferCompleteRotPos - postSeekPosition;
			}

			// Check if we got an error.
			const uint32 maxRetryState = (mpProfile->mbRetryMode1050 ? 14 : 16);
			if (mFDCStatus != 0xFF && mActiveCommandState < maxRetryState) {
				// Check if we're modeling the 810 or 1050. The 810 does four tries overall
				// with a possible recalibrate between the first two and second two attempts;
				// the 1050 does two tries with a recalibrate or restep in between.
				if (!mpProfile->mbRetryMode1050) {
					// 810 -- add another (fake) rotation
					if (missingSector)
						secondByteDelay += kCyclesPerFakeRot_810;
					else
						secondByteDelay += mpProfile->mCyclesPerDiskRotation;
				}

				// Compute the restep/recalibration delay.
				if (missingSector) {
					// Missing sector -- we'll be recalibrating (once after 1 retry (1050 or 2 retries (810)).
					//
					// The 1050 has a track 0 sensor, so it only does the necessary number of steps
					// when recalibrating. The 810, on the other hand, doesn't have one and just
					// steps back 43 tracks.
					if (mpProfile->mbRetryMode1050 || mActiveCommandState == 15) {
						const uint32 restoreSteps = mpProfile->mbRetryMode1050 ? mCurrentTrack : 43;
						const uint32 seekTime1 = restoreSteps ? restoreSteps * mpProfile->mCyclesPerTrackStep + mpProfile->mCyclesForHeadSettle : 0;

						if (restoreSteps) {
							PlaySeekSound(secondByteDelay, restoreSteps, true, mCurrentTrack);
							secondByteDelay += seekTime1;
						}

						// compute time to seek back -- no rotational delay to get back to sector, it
						// doesn't exist (sectors don't magically reappear in our model).
						if (mCurrentTrack) {
							const uint32 tracksToStep = mpProfile->mbReverseOnForwardSeeks ? mCurrentTrack+1 : mCurrentTrack;

							PlaySeekSound(secondByteDelay, tracksToStep);
							secondByteDelay += tracksToStep * mpProfile->mCyclesPerTrackStep + mpProfile->mCyclesForHeadSettle;
						}
					}

					// ...and do another fake rotation.
					if (mpProfile->mbRetryMode1050)
						secondByteDelay += kCyclesPerFakeRot_1050;
					else
						secondByteDelay += kCyclesPerFakeRot_810;
				} else {
					// Found sector but read with error. Add time to have read the sector.
					secondByteDelay += GetCyclesToReadSector(psi, mSectorSize);

					if (g_ATLCDisk.IsEnabled()) {
						const uint32 rotPeriod = mpProfile->mCyclesPerDiskRotation;

						const float postReadPos = (float)((mRotationalCounter + secondByteDelay) % rotPeriod) / (float)rotPeriod;

						uint32 physOrder = 1;
						uint32 physIndex = vsi.mFirstPhysSector;

						if (image) {
							for(uint32 i = 0; i < vsi.mNumPhysSectors; ++i) {
								if (physIndex == mActiveCommandPhysSector) {
									physOrder = i + 1;
									break;
								}

								physIndex = image->GetNextPhysicalSector(physIndex);
							}
						}

						g_ATLCDisk("Retry   vsec=%3d (%d/%d) (trk=%d), psec=%3d,         rot=%.2f >> %.2f >> %.2f >> %.2f%s.\n"
								, sector
								, physOrder
								, vsi.mNumPhysSectors
								, (sector - 1) / mSectorsPerTrack
								, (uint32)mActiveCommandPhysSector
								, mActiveCommandStartRotPos
								, mActiveCommandSearchStartRotPos
								, psi.mRotPos
								, postReadPos
								,  psi.mWeakDataOffset >= 0 ? " (w/weak bits)"
									: !(mFDCStatus & 0x02) ? " (w/long sector)"		// must use DRQ as lost data differs between drives
									: !(mFDCStatus & 0x08) ? " (w/CRC error)"
									: !(mFDCStatus & 0x10) ? " (w/missing sector)"
									: !(mFDCStatus & 0x20) ? " (w/deleted sector)"
									: ""
								);
					}
					
					// For the 810, we'll just fail three more times. For the 1050, we need
					// to do a half step in and back, then retry once.
					if (mpProfile->mbRetryMode1050) {
						// seek in and out
						PlaySeekSound(secondByteDelay, 1);

						// apply seek delay
						uint32 reseekDelay = mpProfile->mCyclesPerTrackStep + mpProfile->mCyclesForHeadSettle;

						secondByteDelay += reseekDelay;
					}
				}

				// delay and then retry
				WarpOrDelay(secondByteDelay);
				Wait(mActiveCommandState + 1);
				break;
			}

			WarpOrDelay(secondByteDelay);
			Wait(20);
			break;
		}

		default:
			return false;
	}

	return true;
}

void ATDiskEmulator::ProcessCommandRead() {
	if (ProcessCommandReadWriteCommon(false))
		return;

	const uint32 sector = mActiveCommandSector;

	switch(mActiveCommandState) {
		case 0: {
			mLastSector = sector;

			mpDiskInterface->CheckSectorBreakpoint(sector);

			// check if we have a happy drive memory request
			if (mEmuMode == kATDiskEmulationMode_Happy810 && sector > 720) {
				// Address must be in $0800-1380.
				if (sector < 0x0800 || sector > 0x1380) {
					// NAK the command.
					BeginTransferNAKCommand();
					WarpOrDelay(500, 500);
					SendResult(false, mSectorSize);
					EndCommand();
					return;
				}

				// Copy data from drive RAM.
				memcpy(mSendPacket, mDriveRAM + (sector - 0x0800), mSectorSize);
				BeginTransferACKCmd();
				WarpOrDelay(500, 500);
				SendResult(true, mSectorSize);
				EndCommand();
				return;
			} else if (mEmuMode == kATDiskEmulationMode_Happy1050 && sector >= 0x8000) {
				// Address must be in $8000-97FF.
				if (sector >= 0x9800) {
					// NAK the command.
					BeginTransferNAKCommand();
					WarpOrDelay(500, 500);
					SendResult(false, mSectorSize);
					EndCommand();
					return;
				}

				// Copy data from drive RAM.
				memcpy(mSendPacket, mDriveRAM + (sector - 0x8000), mSectorSize);
				BeginTransferACKCmd();
				WarpOrDelay(500, 500);
				SendResult(true, mSectorSize);
				EndCommand();
				return;
			}

			// check if we actually have a disk; if not, we still allow sectors 1-720, but
			// report them as missing
			IATDiskImage *image = mpDiskInterface->GetDiskImage();

			if (!image && sector >= 1 && sector <= 720) {
				mbLastOpError = true;

				// A real 1050 reports 14/94 5F E0 00 when there is no disk in the
				// drive. Bit 7 of drive status depends on the last FM/MFM state;
				// bit 4 of drive status and bit 6 of FDC status updates dynamically
				// based on the write protect sensor. Bit 2 of FDC status (lost data)
				// retains the previous state.
				mFDCStatus &= 0x7F;
				mFDCStatus |= 0x32;

				// sector not found....
				BeginTransferACKCmd();

				// if the drive supports the not ready signal, return error immediately,
				// otherwise simulate rotations trying to find a sector
				if (!mpProfile->mbSupportedNotReady)
					WarpOrDelay(mpProfile->mCyclesPerDiskRotation * 2, 1000);

				// don't clear the sector buffer!
				SendResult(false, 128);

				g_ATLCDisk("Reporting missing sector %d (no disk in drive).\n", sector);
				EndCommand();
				return;
			}

			if (!sector || sector > (uint32)mTotalSectorCount) {
				// NAK the command immediately -- the 810 and 1050 both NAK commands
				// with invalid sector numbers.

				mbLastOpError = true;
				SetupTransferSpeed(mbCommandFrameHighSpeed);
				mpSIOInterface->SendNAK();
				g_ATLCDisk("Error reading sector %d.\n", sector);
				EndCommand();
				return;
			}

			BeginTransferACKCmd();
			mActiveCommandState = 10;
			break;
		}

		case 20: {
			IATDiskImage *image = mpDiskInterface->GetDiskImage();

			if (!image || mActiveCommandPhysSector >= (sint32)image->GetPhysicalSectorCount())
				mActiveCommandPhysSector = -1;

			ATDiskPhysicalSectorInfo psi = {};
			if (mActiveCommandPhysSector >= 0)
				image->GetPhysicalSectorInfo((uint32)mActiveCommandPhysSector, psi);

			// Warp disk to beginning of sector, if it isn't already there.
			UpdateRotationalCounter();

			if (mRotationalCounter != mTransferCompleteRotPos) {
				mRotationalCounter = mTransferCompleteRotPos;

				if (mpRotationTracer)
					mpRotationTracer->Warp(mRotationalCounter);
			}

			// Check if this is a boot sector. If so, force the length to 128 bytes per protocol.
			uint32 transferLength = mSectorSize;

			if (image && sector <= image->GetBootSectorCount())
				transferLength = 128;

			// Add time to read sector and compute checksum.
			const bool bufferedRead = mpProfile->mbBufferTrackReads;
			const uint32 sectorReadDelay = GetCyclesToReadSector(psi, transferLength);

			// FDC reset and checksum: ~2568 cycles @ 500KHz = 9192 cycles
			const uint32 postReadMinDelay = bufferedRead ? kCyclesPostReadDelay_Buffered : kCyclesPostReadDelay_Fast;
			const uint32 postReadDelay = bufferedRead ? postReadMinDelay : mpProfile->mCyclesPostReadToCE;

			WarpOrDelay(sectorReadDelay + postReadDelay, postReadMinDelay);

			// check for missing sector
			// note: must send ACK (41) + ERROR (45) -- BeachHead expects to get DERROR from SIO
			if (mActiveCommandPhysSector < 0 || psi.mImageSize == 0) {
				mbLastOpError = true;

				// sector not found....
				// don't clear the sector buffer!
				SendResult(false, transferLength);

				g_ATLCDisk("Reporting missing sector %d.\n", sector);
				EndCommand();
				return;
			}

			const uint32 readLength = psi.mPhysicalSize;

			try {
				image->ReadPhysicalSector((uint32)mActiveCommandPhysSector, mSendPacket, readLength);
			} catch(const MyError&) {
				// wipe sector and report CRC error
				memset(mSendPacket, 0, readLength);
				mFDCStatus = 0xF7;
			}
			
			mbLastOpError = (mFDCStatus != 0xFF);

			// check for CRC error
			// must return data on CRC error -- Koronis Rift requires this
			bool successful = true;
			if (~mFDCStatus & 0x2E) {
				successful = false;

				// Check if we should emulate weak bits.
				if (psi.mWeakDataOffset >= 0) {
					for(int i = psi.mWeakDataOffset; i < (int)readLength; ++i) {
						mSendPacket[i] ^= (uint8)mWeakBitLFSR;

						mWeakBitLFSR = (mWeakBitLFSR << 8) + (0xff & ((mWeakBitLFSR >> (28 - 8)) ^ (mWeakBitLFSR >> (31 - 8))));
					}
				}
			}

			SendResult(successful, transferLength);
			Wait(21);
			break;
		}

		case 21:
			UpdateAccelTimeSkew();

			if (g_ATLCDisk.IsEnabled()) {
				if (IATDiskImage *image = mpDiskInterface->GetDiskImage()) {
					UpdateRotationalCounter();

					ATDiskVirtualSectorInfo vsi = {};
					if (mActiveCommandSector && mActiveCommandSector <= image->GetVirtualSectorCount())
						image->GetVirtualSectorInfo(mActiveCommandSector - 1, vsi);

					ATDiskPhysicalSectorInfo psi = {};
					if (mActiveCommandPhysSector >= 0 && image->IsValidPhysicalSector((uint32)mActiveCommandPhysSector))
						image->GetPhysicalSectorInfo(mActiveCommandPhysSector, psi);

					uint32 physOrder = 1;
					uint32 physIndex = vsi.mFirstPhysSector;

					for(uint32 i = 0; i < vsi.mNumPhysSectors; ++i) {
						if (physIndex == mActiveCommandPhysSector) {
							physOrder = i + 1;
							break;
						}

						physIndex = image->GetNextPhysicalSector(physIndex);
					}

					g_ATLCDisk("Reading vsec=%3d (%d/%d) (trk=%d), psec=%3d, chk=%02X, rot=%.2f >> %.2f >> %.2f >> %.2f%s.\n"
							, sector
							, physOrder
							, vsi.mNumPhysSectors
							, (sector - 1) / mSectorsPerTrack
							, (uint32)mActiveCommandPhysSector
							, Checksum(mSendPacket, mTransferLength)
							, mActiveCommandStartRotPos
							, mActiveCommandSearchStartRotPos
							, psi.mRotPos
							, (float)mRotationalCounter / (float)mpProfile->mCyclesPerDiskRotation
							,  psi.mWeakDataOffset >= 0 ? " (w/weak bits)"
								: !(mFDCStatus & 0x02) ? " (w/long sector)"		// must use DRQ as lost data differs between drives
								: !(mFDCStatus & 0x08) ? " (w/CRC error)"
								: !(mFDCStatus & 0x10) ? " (w/missing sector)"
								: !(mFDCStatus & 0x20) ? " (w/deleted sector)"
								: ""
							);
				}
			}

			EndCommand();
			break;
	}
}

void ATDiskEmulator::ProcessCommandWrite() {
	if (ProcessCommandReadWriteCommon(true))
		return;

	switch(mActiveCommandState) {
		case 0: {
			mLastSector = mActiveCommandSector;
			mBufferedTrack = -1;

			mpDiskInterface->CheckSectorBreakpoint(mActiveCommandSector);

			// check if we have a happy drive memory request
			if (mEmuMode == kATDiskEmulationMode_Happy810 && mActiveCommandSector >= 0x0800) {
				// Address must be in $0800-137F.
				if (mActiveCommandSector < 0x0800 || mActiveCommandSector > 0x1380) {
					// NAK the command.
					mbLastOpError = true;
					BeginTransferNAKCommand();
					SendResult(false, mSectorSize);
					EndCommand();
					return;
				}

				mbLastOpError = false;
				BeginTransferACKCmd();
				BeginReceive(mSectorSize);
				Wait(60);
				return;
			} else if (mEmuMode == kATDiskEmulationMode_Happy1050 && mActiveCommandSector >= 0x8000) {
				// Address must be in $8000-97FF.
				if (mActiveCommandSector >= 0x9800) {
					// NAK the command.
					mbLastOpError = true;
					BeginTransferNAKCommand();
					SendResult(false, mSectorSize);
					EndCommand();
					return;
				}

				mbLastOpError = false;
				BeginTransferACKCmd();
				BeginReceive(mSectorSize);
				Wait(50);
				return;
			}

			IATDiskImage *image = mpDiskInterface->GetDiskImage();
			if (!image || !mActiveCommandSector || mActiveCommandSector > (uint32)mTotalSectorCount) {
				// NAK the command immediately -- the 810 and 1050 both NAK commands
				// with invalid sector numbers.
				mbLastOpError = true;
				BeginTransferNAKCommand();
				EndCommand();
				g_ATLCDisk("Error writing sector %d.\n", mActiveCommandSector);
				break;
			}

			mbLastOpError = false;
			BeginTransferACKCmd();

			// wait for remaining data
			BeginReceive(image->GetSectorSize(mActiveCommandSector - 1));

			g_ATLCDisk("Sent ACK, now waiting for write data.\n");
			// enter common path to turn on motor and seek
			Wait(10);
			break;
		}

		case 20: {
			// check if we don't have a disk anymore; if so, report record not found
			IATDiskImage *image = mpDiskInterface->GetDiskImage();
			if (!image) {
				mFDCStatus = 0xEF;
				mbLastOpError = true;

				BeginTransferError();
				EndCommand();
				return;
			}

			// check if disk is write protected -- 810 and 1050 do this post-seek
			if (!mbWriteEnabled) {
				mFDCStatus = 0xBF;
				mbLastOpError = true;

				BeginTransferError();
				EndCommand();
				return;
			}

			// fail if we had an RNF error
			if (mActiveCommandPhysSector < 0 || !image->IsValidPhysicalSector((uint32)mActiveCommandPhysSector)
				|| mActiveCommandSector > image->GetVirtualSectorCount()) {
				VDASSERT(mFDCStatus != 0xFF);
				mbLastOpError = true;

				BeginTransferError();
				EndCommand();
				return;
			}

			// get virtual sector information
			ATDiskVirtualSectorInfo vsi;
			image->GetVirtualSectorInfo(mActiveCommandSector - 1, vsi);

			ATDiskPhysicalSectorInfo psi;
			image->GetPhysicalSectorInfo(mActiveCommandPhysSector, psi);

			// commit data to physical sector
			g_ATLCDisk("Writing vsec=%3u, psec=%3u.\n", mActiveCommandSector, mActiveCommandPhysSector);

			const uint32 writeLength = psi.mPhysicalSize;

			try {
				image->WritePhysicalSector(mActiveCommandPhysSector, mReceivePacket, writeLength);

				// set FDC status
				mFDCStatus = 0xFF;
				mbLastOpError = false;
			} catch(const MyError&) {
				mFDCStatus = 0xF7;	// crc error
				mbLastOpError = true;
			}

			mpDiskInterface->OnDiskModified();

			uint32 rotDelay = kCyclesRotationalDelay_Fast;

			if (mbAccurateSectorTiming) {
				// compute rotational delay
				UpdateRotationalCounter();
				uint32 rotPos = VDRoundToInt(psi.mRotPos * mpProfile->mCyclesPerDiskRotation);

				rotDelay = rotPos < mRotationalCounter ? (rotPos - mRotationalCounter) + mpProfile->mCyclesPerDiskRotation : (rotPos - mRotationalCounter);

				// add verify delay if we're doing write w/verify
				if (mActiveCommand == 'W')
					rotDelay += mpProfile->mCyclesPerDiskRotation;

				rotDelay += 10000;	// fudge factor
			}

			// If acceleration is disabled, wait a minimum of 400us + std byte to give SIO
			// enough time to turn around between ACK and Complete. The SIO spec says 250us,
			// but the stock OS really needs more time than that because it enables the IRQ
			// too early.
			WarpOrDelay(rotDelay, (716+932) - std::min<uint32>(932, mpProfile->mCyclesPerSIOByte));

			if (mbLastOpError)
				BeginTransferError();
			else
				BeginTransferComplete();

			EndCommand();
			break;
		}

		case 50:
			BeginTransferACK();
			WarpOrDelay(500, 500);
			BeginTransferComplete();

			// Enable custom commands if the write is >=$9600; hooking requires
			// writing somewhere within $9600-97FF.
			if (mActiveCommandSector >= 0x9600)
				mCustomCodeState = 1;

			memcpy(mDriveRAM + (mActiveCommandSector - 0x8000), mReceivePacket, mSectorSize);
			EndCommand();
			break;

		case 60:
			BeginTransferACK();
			WarpOrDelay(500, 500);
			BeginTransferComplete();

			memcpy(mDriveRAM + (mActiveCommandSector - 0x0800), mReceivePacket, mSectorSize);
			EndCommand();
			break;
	}
}

void ATDiskEmulator::ProcessCommandReadPERCOMBlock() {
	if (!mpProfile->mbSupportedCmdPERCOM)
		return ProcessUnsupportedCommand();

	BeginTransferACKCmd();

	WarpOrDelayFromStopBit(mpProfile->mCyclesACKStopBitToReadPERCOMComplete);

	memcpy(mSendPacket, mPERCOM, 12);

	if (mEmuMode == kATDiskEmulationMode_XF551) {
		mSendPacket[1] = 0;		// step rate 0
		mSendPacket[8] = 1;		// drive active
	}

	const int sectorSize = VDReadUnalignedBEU16(&mPERCOM[6]);
	const int sectorCount = mPERCOM[0] * (sint32)VDReadUnalignedBEU16(&mPERCOM[2]) * (mPERCOM[4] + 1);
	g_ATLCDisk("Reading PERCOM data: %u sectors of %u bytes each, %u boot sectors\n", sectorCount, sectorSize, sectorSize > 256 ? 0 : 3);

	SendResult(true, 12);
	EndCommand();
}

void ATDiskEmulator::ProcessCommandWritePERCOMBlock() {
	switch(mActiveCommandState) {
		case 0:
			if (!mpProfile->mbSupportedCmdPERCOM)
				return ProcessUnsupportedCommand();

			BeginTransferACKCmd();
			BeginReceive(12);

			g_ATLCDisk("Sent ACK, now waiting for PERCOM block data.\n");
			Wait(1);
			break;

		case 1: {
			// validate PERCOM data
			bool valid = SetPERCOMData(mReceivePacket);

			if (!valid) {
				BeginTransferError();
				EndCommand();
				return;
			}

			if (mpProfile->mbWritePercomChangesDensity)
				UpdateDensityFromPERCOM();

			mFDCStatus = 0xFF;
			mbLastOpError = false;

			WarpOrDelay(450, 450);
			BeginTransferComplete();
			EndCommand();
			break;
		}

		default:
			ProcessUnhandledCommandState();
			break;
	}
}

void ATDiskEmulator::ProcessCommandGetHighSpeedIndex() {
	if (!mpProfile->mbSupportedCmdGetHighSpeedIndex)
		return ProcessUnsupportedCommand();

	BeginTransferACKCmd();

	mSendPacket[0] = mpProfile->mHighSpeedIndexUSD;

	WarpOrDelay(450, 450);

	SendResult(true, 1);
	EndCommand();
}

void ATDiskEmulator::ProcessCommandHappy() {
	if (mEmuMode != kATDiskEmulationMode_Happy1050)
		return ProcessUnsupportedCommand();

	BeginTransferACKCmd();

	// AUX1 >= $04 reconfigures the drive; AUX1 is the change mask, AUX2 contains
	// new mode bits.
	const uint8 aux1 = mActiveCommandSector & 0xFF;
	const uint8 aux2 = mActiveCommandSector >> 8;

	if (aux1 >= 0x04) {
		// $80 turns on unhappy mode (disable all extended functions)
		// $40 disables track buffering
		if ((aux1 & aux2) & 0x60)
			mbTrackBufferingEnabled = false;
		else
			mbTrackBufferingEnabled = true;
	}

	WarpOrDelay(450, 450);

	BeginTransferComplete();

	EndCommand();
}

void ATDiskEmulator::ProcessCommandHappyQuiet() {
	if (mEmuMode == kATDiskEmulationMode_Happy810) {
		// The "Q" command isn't actually implemented in the Happy 810 firmware; it's just
		// a vector that can be taken over when uploading code. Since we can't actually emulate
		// that, for now we pretend it's a command that sends back a sector. This lie is enough
		// to get past the diagnostics.
		BeginTransferACKCmd();

		WarpOrDelay(450, 450);

		memset(mSendPacket, 0, 0x80);
		SendResult(true, 0x80);
		EndCommand();
	} else if (mEmuMode == kATDiskEmulationMode_Happy1050) {
		BeginTransferACKCmd();

		WarpOrDelay(450, 450);

		TurnOffMotor();

		BeginTransferComplete();

		EndCommand();
	} else
		return ProcessUnsupportedCommand();
}

void ATDiskEmulator::ProcessCommandHappyRAMTest() {
	if (mEmuMode != kATDiskEmulationMode_Happy1050 || !mCustomCodeState)
		return ProcessUnsupportedCommand();

	BeginTransferACKCmd();

	WarpOrDelay(450, 450);

	memset(mSendPacket, 0, 128);

	SendResult(true, 128);
	EndCommand();
}

void ATDiskEmulator::ProcessCommandHappyHeadPosTest() {
	switch(mActiveCommandState) {
		case 0: {
			if (mEmuMode != kATDiskEmulationMode_Happy1050 || !mCustomCodeState)
				return ProcessUnsupportedCommand();

			BeginTransferACKCmd();

			WarpOrDelay(450, 450);

			uint32 track = 0;
			if (mActiveCommand == 0x29)
				track = ~mActiveCommandSector & 0xff;

			if (track >= 40) {
				BeginTransferError();
				EndCommand();
				break;
			}

			// motor on
			if (TurnOnMotor())
				WarpOrDelay(7159090/8);

			// check if we need to seek
			int trackDelta = (int)track - (int)mCurrentTrack;
			uint32 tracksToStep = (uint32)abs(trackDelta);

			mCurrentTrack = track;

			uint32 opDelay = 0;
			if (tracksToStep) {
				if (trackDelta > 0) {
					PlaySeekSound(opDelay, tracksToStep + 1);
					opDelay += (tracksToStep + 1) * mpProfile->mCyclesPerTrackStep;
				} else {
					PlaySeekSound(opDelay, tracksToStep);
					opDelay += tracksToStep * mpProfile->mCyclesPerTrackStep;
				}

				opDelay += mpProfile->mCyclesForHeadSettle;
			}

			WarpOrDelay(opDelay);
			Wait(1);
			break;
		}

		case 1:
			BeginTransferComplete();
			EndCommand();
			break;

		default:
			ProcessUnhandledCommandState();
			break;
	}
}

void ATDiskEmulator::ProcessCommandHappyRPMTest() {
	switch(mActiveCommandState) {
		case 0:
			if (mEmuMode != kATDiskEmulationMode_Happy1050 || !mCustomCodeState)
				return ProcessUnsupportedCommand();

			BeginTransferACKCmd();

			WarpOrDelay(450, 450);
			Wait(1);
			break;

		case 1:
			// Wait for the index mark, then another index mark.
			UpdateRotationalCounter();
			WarpOrDelay(mpProfile->mCyclesPerDiskRotation + (mpProfile->mCyclesPerDiskRotation - mRotationalCounter));

			memset(mSendPacket, 0, 128);

			// The return value is related as follows:
			// rval = 2000000 / (RPM - 0.25)
			VDWriteUnalignedLEU16(mSendPacket, 6952);

			SendResult(true, 128);
			EndCommand();
			break;

		default:
			ProcessUnhandledCommandState();
			break;
	}
}

void ATDiskEmulator::ProcessCommandExecuteIndusGT() {
	switch(mActiveCommandState) {
		case 0:
			if (mEmuMode != kATDiskEmulationMode_IndusGT)
				return ProcessUnsupportedCommand();

			mbLastOpError = false;
				
			// There's a lot of voodoo here that needs explaining.
			//
			// The Indus GT allows code to be uploaded to the drive at address 7F00h. This is
			// done by sending AUX1=len, AUX2=odd. The custom code is then called with
			// AUX2 even.
			//
			// Since we don't have actual Z80 emulation, we detect the code fragments uploaded
			// via AUX1 even and emulate the behavior on AUX1 odd. There are a few code fragments
			// that we are interested in:
			//
			//	Version check:
			//		Returns the major/minor version of the drive in a two-byte packet. We
			//		return $01 20 for 1.20.
			//
			//	Synchromesh loader:
			//		Uploads $0367 bytes to the drive at address 7B84h. This is done in ascending
			//		order, but strangely the first packet is the short packet ($67 bytes) and
			//		the remaining packets are $100 bytes. The last packet is an empty packet.
			//
			//	SuperSynchromesh loader:
			//		Uploads $0369 bytes to the drive at address 7B84h, similarly to the Synchromesh
			//		loader. After this, another $02F5 bytes are uploaded at 7840h to support the
			//		RamCharger.
			//
			//	SuperSynchromesh loader (SDX INDUS.SYS version):
			//		Similar to SuperSynchromesh, but with the RamCharger part omitted.

			if (mActiveCommandSector & 0x100) {
				BeginTransferACKCmd();

				mActiveCommandSector &= 0xFF;

				// wait for remaining data
				BeginReceive(mActiveCommandSector);

				g_ATLCDisk("Sent ACK, now waiting for upload data.\n");
				Wait(3);
				return;
			}
			
			if (mActiveCommandSector >= 0x100) {
				BeginTransferNAKCommand();
				EndCommand();
				break;
			}

			switch(mCustomCodeState) {
				default:
				case 0:
					BeginTransferACKCmd();
					BeginTransferError();
					EndCommand();
					return;

				case 1:
					BeginTransferACKCmd();
					mSendPacket[0] = 0x20;
					mSendPacket[1] = 0x01;
					SendResult(true, 2);
					mCustomCodeState = 0;
					EndCommand();
					return;
					break;

				case 2:
				case 3:
				case 4:
				case 5:
				case 7:
				case 8:
				case 9:
				case 10:
				case 12:
				case 13:
				case 14:
				case 16:
				case 17:
				case 18:
				case 19:
					BeginTransferACKCmd();
					mActiveCommandState = 10;
					mActiveCommandSector = 0x100;

					if (mCustomCodeState == 2)
						mActiveCommandSector = 0x67;
					if (mCustomCodeState == 7 || mCustomCodeState == 16)
						mActiveCommandSector = 0x69;
					if (mCustomCodeState == 12)
						mActiveCommandSector = 0xF5;
					break;

				case 6:
				case 11:
				case 15:
				case 20:
					BeginTransferACKCmd();
					mpSIOInterface->Delay(1000);
					BeginTransferComplete();

					if (mCustomCodeState == 6) {
						g_ATLCDisk("Firmware upload recognized. Enabling Synchromesh operation.\n");
						mpProfile = &ATGetDiskProfileIndusGTSynchromesh();
					} else if (mCustomCodeState == 11 || mCustomCodeState == 20) {
						g_ATLCDisk("Firmware upload recognized. Enabling SuperSynchromesh operation.\n");

						mpProfile = &ATGetDiskProfileIndusGTSuperSynchromesh();
						if (mCustomCodeState == 11)
							mCustomCodeState = 12;
						else
							mCustomCodeState = 0;
					} else {
						g_ATLCDisk("Firmware upload recognized. Ignoring RamCharger firmware.\n");
						mCustomCodeState = 0;
					}

					EndCommand();
					return;
			}

			// wait for remaining data
			BeginReceive(mActiveCommandSector);

			g_ATLCDisk("Sent ACK, now waiting for upload data (%u bytes).\n", mActiveCommandSector);
			Wait(10);

			break;

		case 3: {
			mCustomCodeState = 0;

			bool successful = false;

			// The Indus GT accepts arbitrary code uploads to 7F00h, but we can't
			// support that since we aren't actually emulating the Z80. Instead, we
			// cheat and identify+simulate the code upload. The fragments we recognize:
			//
			//	Length	Checksum	Desc
			//	$19		$52			Version check (returns major/minor version)
			//	$8A		$EB			Synchromesh uploader
			//	$E5		$45			SuperSynchromesh uploader
			//	$E5		$F0			SuperSynchromesh uploader w/o RamCharger (used by SDX)
			const uint8 checksum = Checksum(mReceivePacket, mActiveCommandSector);

			if (mActiveCommandSector == 0x19 && checksum == 0x52) {
				mCustomCodeState = 1;
				g_ATLCDisk("Accepting code upload: Indus GT version check.\n");
				successful = true;
			} else if (mActiveCommandSector == 0x8A && checksum == 0xEB) {
				mCustomCodeState = 2;
				g_ATLCDisk("Accepting code upload: Indus GT Synchromesh firmware loader.\n");
				successful = true;
			} else if (mActiveCommandSector == 0xE5 && checksum == 0x45) {
				mCustomCodeState = 7;
				g_ATLCDisk("Accepting code upload: Indus GT SuperSynchromesh + RamCharger firmware loader.\n");
				successful = true;
			} else if (mActiveCommandSector == 0xE5 && checksum == 0xF0) {
				mCustomCodeState = 16;
				g_ATLCDisk("Accepting code upload: Indus GT SuperSynchromesh firmware loader.\n");
				successful = true;
			} else {
				g_ATLCDisk("Rejecting unknown code upload: len=$%04X bytes, checksum=$%02X\n", mActiveCommandSector, checksum);
			}

			WarpOrDelay(1000);
			
			if (successful)
				BeginTransferComplete();
			else
				BeginTransferError();

			EndCommand();
			break;
		}

		case 10: {
			static const uint8 kCodeChecksums[] = {
				// state 2 - Synchromesh
				0x49,
				0x22,
				0xA0,
				0xCF,
				0x00,

				// state 7 - SuperSynchromesh
				0xDA,
				0xBA,
				0x62,
				0xF5,
				0x00,

				// state 12 - RamCharger
				0xA4,
				0xEE,
				0x96,
				0x00,

				// state 16 - SuperSynchromesh patched
				0x6E,
				0xBA,
				0x62,
				0xF5,
				0x00,
			};

			const uint8 checksum = Checksum(mReceivePacket, mActiveCommandSector);
			const uint8 expected = kCodeChecksums[mCustomCodeState - 2];

			if (checksum != expected) {
				g_ATLCDisk("Rejecting Synchromesh code upload: state=%d, chksum=$%02X, expected=$%02X\n", mCustomCodeState, checksum, expected);

				mCustomCodeState = 0;
				WarpOrDelay(1000);
				BeginTransferError();
				EndCommand();
				return;
			}

			++mCustomCodeState;

			WarpOrDelay(1000);
			BeginTransferComplete();
			EndCommand();
			break;
		}

		default:
			ProcessUnhandledCommandState();
			break;
	}
}

void ATDiskEmulator::ProcessCommandFormat() {
	switch(mActiveCommandState) {
		case 0:
			if (mOriginalCommand == 0xA3) {
				if (!mpProfile->mbSupportedCmdFormatBoot)
					return ProcessUnsupportedCommand();
			} else if ((mOriginalCommand & 0x7F) == 0x22) {
				if (mEmuMode == kATDiskEmulationMode_810)
					return ProcessUnsupportedCommand();
			} else if ((mOriginalCommand & 0x7F) == 0x66) {
				if (!mpProfile->mbSupportedCmdFormatSkewed)
					return ProcessUnsupportedCommand();
			} else if ((mOriginalCommand & 0x7F) == 0x21 && mEmuMode == kATDiskEmulationMode_XF551) {
				// The XF551 does not allow command $21 to format medium density, forcing single
				// density instead.
				if (mPERCOM[3] >= 26) {
					// force XF551 single density
					mPERCOM[2] = 0;		// spt high
					mPERCOM[3] = 18;	// spt low
					mPERCOM[4] = 0;		// sides minus one
					mPERCOM[5] = 0;		// FM/MFM encoding
					mPERCOM[6] = 0;		// bps high
					mPERCOM[7] = 128;	// bps low
				}
			}

			// Disable high speed operation if we're getting an XF551 command -- the high bit
			// is used for sector skew and not high speed. This must NOT be done for the Indus
			// GT since the Synchromesh and SuperSynchromesh firmwares use high speed all the
			// way through.
			if (mOriginalCommand == 0xA1 && mEmuMode == kATDiskEmulationMode_XF551)
				mbActiveCommandHighSpeed = false;

			if (!mbWriteEnabled || !mbFormatEnabled) {
				if (mbWriteEnabled)
					g_ATLCDisk("FORMAT COMMAND RECEIVED. Blocking due to format-protected disk!\n");
				else
					g_ATLCDisk("FORMAT COMMAND RECEIVED. Blocking due to read-only disk!\n");

				// The FORMAT command always sends an ACK first and then sends ERROR instead of
				// COMPLETE if the disk is write protected. In that case, we need to send a data
				// frame.
				const uint32 sectorSize = ((mOriginalCommand & 0x7F) == 0x22) ? 128 : mSectorSize;
				BeginTransferACK();
				mSendPacket[0] = 0xFF;		// Sector terminator (sector buffer data)
				mSendPacket[1] = 0xFF;
				memset(mSendPacket + 2, 0, sectorSize - 2);
				mSendPacket[sectorSize] = 0xFF;
				mbLastOpError = true;

				// Assert FDC status bit 6 (write protect).
				mFDCStatus = 0xBF;

				SendResult(false, sectorSize);
				EndCommand();
				return;
			}

			// update density from PERCOM block, if supported
			UpdateDensityFromPERCOM();

			// turn on motor -- should be after ready and WP checks
			TurnOnMotor();

			mbLastOpError = false;
			BeginTransferACKCmd();

			// Check if we are doing the format skewed command. If so, we must wait for a data frame.
			if ((mOriginalCommand & 0x7F) == 0x66) {
				// wait for remaining data
				BeginReceive(128);

				g_ATLCDisk("Sent ACK, now waiting for PERCOM and sector skew data.\n");
				Wait(2);
			} else {
				Wait(3);
			}
			break;

		case 2:
			// validate PERCOM data
			if (!SetPERCOMData(mReceivePacket)) {
				BeginTransferError();
				EndCommand();
			}

			UpdateDensityFromPERCOM();

			Wait(3);
			break;

		case 3: {
			// If we are doing this on an 810 or 1050, reset the PERCOM block to default.
			if ((mOriginalCommand & 0x7F) == 0x22) {
				memcpy(mPERCOM, kDefaultPERCOMED, sizeof mPERCOM);
			} else {
				switch(mEmuMode) {
					case kATDiskEmulationMode_810:
					case kATDiskEmulationMode_1050:
						memcpy(mPERCOM, kDefaultPERCOM, sizeof mPERCOM);
						break;
				}
			}

			int formatSectorSize = VDReadUnalignedBEU16(&mPERCOM[6]);
			int formatTracks = mPERCOM[0];
			int formatSides = mPERCOM[4] + 1;
			int formatSectorsPerTrack = VDReadUnalignedBEU16(&mPERCOM[2]);
			int formatSectorCount = formatTracks * formatSectorsPerTrack * formatSides;
			int formatBootSectorCount = formatSectorSize >= 512 ? 0 : 3;

			if (mOriginalCommand == 0xA3) {
				g_ATLCDisk("Boot track format command received. Silently ignoring as we don't support partially formatted disks.\n");
			} else {
				g_ATLCDisk("FORMAT COMMAND RECEIVED. Reformatting disk as %u sectors of %u bytes each.\n", formatSectorCount, formatSectorSize);

				ATDiskGeometryInfo geometry {};
				geometry.mSectorSize = formatSectorSize;
				geometry.mBootSectorCount = formatBootSectorCount;
				geometry.mTotalSectorCount = formatSectorCount;
				geometry.mTrackCount = formatTracks;
				geometry.mSectorsPerTrack = formatSectorsPerTrack;
				geometry.mSideCount = formatSides;
				geometry.mbMFM = (mPERCOM[5] & 4) != 0;
				geometry.mbHighDensity = (mPERCOM[5] & 2) != 0;
				mpDiskInterface->FormatDisk(geometry);
			}

			mActiveCommandSector = 0;

			if (mbAccurateSectorTiming) {
				mpSIOInterface->Delay(1000);
				Wait(4);
			} else {
				mpSIOInterface->Delay(1000000);
				Wait(5);
			}
			break;
		}

		case 4:
			{
				// Check if the number of tracks is not realistic; if not, just do a fake format.
				// Check if we're done.
				if ((mActiveCommandSector == 0 && (mTrackCount < 40 || mTrackCount > 80)) || mActiveCommandSector >= mTrackCount * 2) {
					Wait(5);
					break;
				}

				// Keep the motor running
				TurnOnMotor();

				// Seek to track
				uint32 track = mActiveCommandSector % mTrackCount;

				uint32 tracksToStep = (uint32)abs((int)mCurrentTrack - (int)track);
				PlaySeekSound(0, tracksToStep);
				mCurrentTrack = track;

				// Update activity
				mpDiskInterface->SetShowActivity(true, track);

				// update state for next track
				++mActiveCommandSector;

				// delay for at least two revs regardless; we are assuming seek delay to blow a rev or more and then index-to-index format
				uint32 delay = tracksToStep ? mpProfile->mCyclesPerTrackStep * tracksToStep + mpProfile->mCyclesForHeadSettle : 1;

				delay = ((delay - 1) / mpProfile->mCyclesPerDiskRotation + 2) * mpProfile->mCyclesPerDiskRotation;

				mpSIOInterface->Delay(delay);
				Wait(4);
			}
			break;

		case 5:
			memset(mSendPacket, 0xFF, mSectorSize);
			SendResult(true, mSectorSize);

			EndCommand();
			break;

		default:
			ProcessUnhandledCommandState();
			break;
	}
}

void ATDiskEmulator::ProcessUnhandledCommandState() {
	EndCommand();
}

void ATDiskEmulator::DetectDensity() {
	IATDiskImage *image = mpDiskInterface->GetDiskImage();

	if (image) {
		const ATDiskGeometryInfo& info = image->GetGeometry();

		mTrackCount = info.mTrackCount;
		mSideCount = info.mSideCount;
		mbMFM = info.mbMFM;
		mbHighDensity = info.mbHighDensity;
		mSectorsPerTrack = info.mSectorsPerTrack;
	} else {
		mTrackCount = 0;
		mSideCount = 0;
		mbMFM = false;
		mbHighDensity = false;
		mSectorsPerTrack = 0;
	}

	ComputePERCOMBlock();
}

void ATDiskEmulator::UpdateDensityFromPERCOM() {
	if (mpProfile->mbSupportedCmdPERCOM) {
		mbMFM = (mPERCOM[5] & 4) != 0;
		mbHighDensity = (mPERCOM[5] & 2) != 0;
	}
}

void ATDiskEmulator::ComputePERCOMBlock() {
	// Note that we do not enforce drive invariants (i.e. XF551) here; we do so in the
	// read PERCOM block command instead.

	// track count
	mPERCOM[0] = (uint8)mTrackCount;

	// step rate
	mPERCOM[1] = 0x01;

	// sectors per track
	mPERCOM[2] = (uint8)(mSectorsPerTrack >> 8);
	mPERCOM[3] = (uint8)(mSectorsPerTrack);

	// sides minus one
	mPERCOM[4] = mSideCount ? mSideCount - 1 : 0;

	// record method
	mPERCOM[5] = (mbMFM ? 4 : 0) + (mbHighDensity ? 2 : 0);

	// bytes per sector
	mPERCOM[6] = (uint8)(mSectorSize >> 8);
	mPERCOM[7] = (uint8)mSectorSize;

	// drive online
	mPERCOM[8] = 0xFF;

	// unused
	mPERCOM[9] = 0;
	mPERCOM[10] = 0;
	mPERCOM[11] = 0;
}

void ATDiskEmulator::ComputeSupportedProfile() {
	const ATDiskProfile *newProfile = &ATGetDiskProfile(mEmuMode);

	if (mpProfile != newProfile) {
		mpProfile = newProfile;

		if (mpSIOInterface) {
			mpSIOInterface->SetCommandDeassertCheckEnabled(newProfile->mbRequireCommandDeassertCheck);
			mpSIOInterface->SetCommandTruncationEnabled(newProfile->mbSupportCommandTruncation);

			if (newProfile->mCyclesToReceiveByteTimeout)
				mpSIOInterface->SetReceiveByteTimeout(newProfile->mCyclesToReceiveByteTimeout);
			else
				mpSIOInterface->SetReceiveByteTimeout();

			if (newProfile->mCyclesToReceiveFrameTimeout)
				mpSIOInterface->SetReceiveFrameTimeout(newProfile->mCyclesToReceiveFrameTimeout);
			else
				mpSIOInterface->SetReceiveFrameTimeout();
		}

		// ensure that rotational counter is in range if the disk sped up
		UpdateRotationalCounter();
	}
}

bool ATDiskEmulator::SetPERCOMData(const uint8 *data) {
	uint16 sectorSize;
	uint32 sectorCount;

	if (mEmuMode == kATDiskEmulationMode_XF551) {
		// The XF551 is very lax about PERCOM blocks: it simply checks the minimum
		// number of bytes to detect SD, ED, or DD formats.

		if (data[3] == 26) {
			// enhanced density
			mPERCOM[2] = 0;		// spt high
			mPERCOM[3] = 26;
			mPERCOM[4] = 0;		// sides minus one
			mPERCOM[5] = 4;		// FM/MFM encoding
			mPERCOM[6] = 0;		// bps high
			mPERCOM[7] = 128;		// bps low
		} else if (data[6] == 0) {
			// single density
			mPERCOM[2] = 0;		// spt high
			mPERCOM[3] = 18;		// spt low
			mPERCOM[4] = 0;		// sides minus one
			mPERCOM[5] = 0;		// FM/MFM encoding
			mPERCOM[6] = 0;		// bps high
			mPERCOM[7] = 128;		// bps low
		} else {
			if (data[4]) {
				// DSDD
				mPERCOM[ 4] = 1;		// sides minus one
			} else {
				// SSDD
				mPERCOM[ 4] = 0;		// sides minus one
			}

			mPERCOM[2] = 0;		// spt high
			mPERCOM[3] = 18;		// spt low
			mPERCOM[5] = 4;		// FM/MFM encoding
			mPERCOM[6] = 1;		// bps high
			mPERCOM[7] = 0;		// bps low
		}
		
		// force XF551 invariants
		mPERCOM[ 0] = 40;		// 40 tracks
		mPERCOM[ 1] = 0;		// step rate 0
		mPERCOM[ 8] = 1;		// drive active
		mPERCOM[ 9] = 0x41;		// reserved
		mPERCOM[10] = 0;		// reserved
		mPERCOM[11] = 0;		// reserved

		sectorSize = VDReadUnalignedBEU16(&mPERCOM[6]);
		sectorCount = mPERCOM[0] * (sint32)VDReadUnalignedBEU16(&mPERCOM[2]) * (mPERCOM[4] + 1);
	} else {
		sectorSize = VDReadUnalignedBEU16(&data[6]);
		sectorCount = data[0] * (sint32)VDReadUnalignedBEU16(&data[2]) * (data[4] + 1);

		if (data[0] == 0) {
			g_ATLCDisk("Invalid PERCOM data: tracks per sector = 0\n");
			return false;
		}
		
		if (data[2] == 0 && data[3] == 0) {
			g_ATLCDisk("Invalid PERCOM data: sectors per track = 0\n");
			return false;
		}
		
		if (data[4] >= 2) {
			g_ATLCDisk("Invalid PERCOM data: invalid sides encoded value %02x\n", data[4]);
			return false;
		}
		
		if (sectorCount > 65535) {
			g_ATLCDisk("Invalid PERCOM data: total sectors > 65535\n");
			return false;
		}
		
		if (sectorSize != 128 && sectorSize != 256 && sectorSize != 512 && sectorSize != 8192) {
			g_ATLCDisk("Invalid PERCOM data: unsupported sector size (%u)\n", sectorSize);
			return false;
		}

		memcpy(mPERCOM, data, 12);
	}

	g_ATLCDisk("Setting PERCOM data: %u sectors of %u bytes each, %u sides, %u boot sectors\n", sectorCount, sectorSize, mPERCOM[4]+1, sectorSize > 256 ? 0 : 3);
	return true;
}

void ATDiskEmulator::TurnOffMotor() {
	if (mpMotorOffEvent) {
		mpScheduler->UnsetEvent(mpMotorOffEvent);

		UpdateRotationalCounter();
	}

	mpDiskInterface->SetShowMotorActive(false);

	if (mpRotationSoundGroup)
		mpRotationSoundGroup->StopAllSounds();

	if (mEmuMode == kATDiskEmulationMode_810) {
		uint32 endTrack = mTrackCount ? mTrackCount - 1 : 0;
		PlaySeekSound(0, abs((int)endTrack - (int)mCurrentTrack));
		mCurrentTrack = endTrack;
	}

	if (mpRotationTracer)
		mpRotationTracer->SetMotorRunning(false);
}

bool ATDiskEmulator::TurnOnMotor(uint32 delay) {
	const bool spinUpDelay = !mpMotorOffEvent;
	
	ExtendMotorTimeoutTo(mpProfile->mCyclesToMotorOff);

	if (spinUpDelay) {
		mpDiskInterface->SetShowMotorActive(true);

		if (mpAudioSyncMixer && mbDriveSoundsEnabled && !mpRotationSoundGroup->IsAnySoundQueued())
			mpAudioSyncMixer->AddLoopingSound(*mpRotationSoundGroup, delay, kATAudioSampleId_DiskRotation, 1.0f);

		UpdateRotationalCounter();
		SetMotorEvent();
	}

	if (mpRotationTracer) {
		mpRotationTracer->SetMotorRunning(true);
	}

	return spinUpDelay;
}

void ATDiskEmulator::ExtendMotorTimeoutBy(uint32 additionalDelay) {
	// We explicitly do NOT turn on the motor if it isn't already on, as this is
	// called from places that are merely simulating not being able to update the
	// idle timer. We also don't update the motor event here; it'll automatically
	// requeue itself when the event fires and the deadline hasn't been met.
	mMotorOffTime += additionalDelay;
}

void ATDiskEmulator::ExtendMotorTimeoutTo(uint32 delay) {
	mMotorOffTime = mpScheduler->GetTick() + delay;

	SetMotorEvent();
}

void ATDiskEmulator::SetMotorEvent() {
	const uint32 t = mpScheduler->GetTick();

	mpSlowScheduler->SetEvent(std::max<uint32>(1, (mMotorOffTime - t + 113) / 114), this, kATDiskEventMotorOff, mpMotorOffEvent);
}

void ATDiskEmulator::PlaySeekSound(uint32 stepDelay, uint32 tracksToStep, bool bumpHead, uint32 bumpStartingTrack) {
	if (!mbDriveSoundsEnabled)
		return;

	// limit step length in case we have a synthetic disk or aren't waiting
	if (mbAccurateSectorTiming) {
		if (tracksToStep > 80)
			tracksToStep = 80;
	} else {
		if (tracksToStep > 10)
			tracksToStep = 10;
	}

	if (mpProfile->mbSeekHalfTracks) {
		for(uint32 i=0; i<tracksToStep; ++i) {
			mpAudioSyncMixer->AddSound(*mpStepSoundGroup, stepDelay, kATAudioSampleId_DiskStep3, 1.0f);

			stepDelay += mpProfile->mCyclesPerTrackStep;
		}
	} else {
		int stepPhase = 0;

		switch(mEmuMode) {
		case kATDiskEmulationMode_810:
		case kATDiskEmulationMode_Happy810:
		case kATDiskEmulationMode_Generic:
		case kATDiskEmulationMode_Generic57600:
		case kATDiskEmulationMode_FastestPossible:
			for(uint32 i=0; i<tracksToStep; ++i) {
				// When the head bumps, the head only ends up moving on two adjacent phases out
				// of the total four. Note that we need to increase the step phase only when steps
				// actually occur for bumping the head to sound correct.
				if (!bumpHead || bumpStartingTrack > i || ((i - bumpStartingTrack) & 2))
					mpAudioSyncMixer->AddSound(*mpStepSoundGroup, stepDelay, kATAudioSampleId_DiskStep1, 0.3f + 0.7f * sinf(stepPhase++ * nsVDMath::kfPi * 0.5f));

				stepDelay += mpProfile->mCyclesPerTrackStep;
			}
			break;

		default:
			for(uint32 i=0; i<tracksToStep; i += 2) {
				if (i + 2 > tracksToStep)
					mpAudioSyncMixer->AddSound(*mpStepSoundGroup, stepDelay, kATAudioSampleId_DiskStep2H, 1.0f);
				else
					mpAudioSyncMixer->AddSound(*mpStepSoundGroup, stepDelay, kATAudioSampleId_DiskStep2, 1.0f);

				stepDelay += mpProfile->mCyclesPerTrackStep * 2;
			}
			break;
		}
	}
}

uint32 ATDiskEmulator::GetCyclesToReadSector(const ATDiskPhysicalSectorInfo& psi, uint32 transferLength) const {
	if (mpProfile->mbBufferTrackReads)
		return 0;

	uint32 waitLength;
	
	if (mpProfile->mbWaitForLongSectors)
		waitLength = std::max<uint32>(psi.mPhysicalSize, transferLength);
	else
		waitLength = std::min<uint32>(psi.mPhysicalSize, transferLength);

	// sector read: ~130 bytes at 125Kbits/sec = ~8.3ms = ~14891 cycles
	return ((mbMFM ? 7445 : 14891) * waitLength + 64) / 128;
}
