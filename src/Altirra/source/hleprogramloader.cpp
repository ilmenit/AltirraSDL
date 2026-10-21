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
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <at/atcore/memoryutils.h>
#include <at/atcore/randomization.h>
#include <at/atcore/vfs.h>
#include <at/atio/blobimage.h>
#include <at/atcore/snapshotimpl.h>
#include <at/atcore/savestate.h>
#include "hleprogramloader.h"
#include "hleutils.h"
#include "kerneldb.h"
#include "console.h"
#include "cpu.h"
#include "cpuheatmap.h"
#include "cpuhookmanager.h"
#include "debugger.h"
#include "simeventmanager.h"
#include "simulator.h"

namespace {
	bool IsDOSExecutable(const uint8 *buf, uint32 len) {
		if (len <= 28)
			return false;

		// Check for MZ signature.
		if (buf[0] != 'M' || buf[1] != 'Z')
			return false;

		// Check for last block size being <= 512. This also rules out a false positive as this
		// requirement ensures that the first four bytes would be an invalid Atari DOS 2 segment.
		if (VDReadUnalignedLEU16(&buf[2]) > 512)
			return false;

		return true;
	}

	bool IsNEExecutable(const uint8 *buf, uint32 len) {
		VDASSERT(IsDOSExecutable(buf, len));

		if (len < 64)
			return false;

		// extract NE header offset
		uint32 neOffset = VDReadUnalignedLEU32(&buf[60]);

		// check if NE header can actually fit at offset
		if (neOffset > len || len - neOffset < 64)
			return false;

		// check for NE signature
		return buf[neOffset] == 'N' && buf[neOffset+1] == 'E';
	}

	bool IsPEExecutable(const uint8 *buf, uint32 len) {
		VDASSERT(IsDOSExecutable(buf, len));

		if (len < 64)
			return false;

		// extract PE header offset
		uint32 peOffset = VDReadUnalignedLEU32(&buf[60]);

		// check if PE header can actually fit at offset
		if (peOffset > len || len - peOffset < 64)
			return false;

		// check for PE signature
		return buf[peOffset] == 'P' && buf[peOffset+1] == 'E';

	}
}

const uint8 ATHLEProgramLoader::kHandlerData[]={
	// text record
	0x00, 0x11,
		0x00, 0x00,			//	relative load address (0)
		0x00, 0x00,			//			dta		a(DevOpen-1)
		0x00, 0x00,			//			dta		a(DevClose-1)
		0x00, 0x00,			//			dta		a(DevGetByte-1)
		0x00, 0x00,			//			dta		a(DevPutByte-1)
		0x00, 0x00,			//			dta		a(DevGetStatus-1)
		0x00, 0x00,			//			dta		a(DevSpecial-1)
		0x4C, 0xFF, 0x01,	//			jmp		$01FF		;initiate launch through hook

	// end
	0x0B, 0x00, 0x00, 0x00
};

const ATHLEProgramLoader::DiskSector ATHLEProgramLoader::kDiskSectors[] {
	{ 1, {		// boot sector 1
			0x00,				// 0700: flags
			0x03,				// 0701: boot sector count
			0x00, 0x30,			// 0702: load address
			0x40, 0x07,			// 0704: init address
			0x4C, 0x80, 0x30,	// 0706: initiate launch process
			0x6A, 0x01,			// 0709: root directory sector map (362)
			0xD0, 0x02,			// 070B: total sector count (720)
			0xC2, 0x02,			// 070D: free sector count (706)
			0x01,				// 070F: bitmap sector count (1)
			0x04, 0x00,			// 0710: bitmap start sector (4)
			0x05, 0x00,			// 0712: next free data sector low (5)
			0x05, 0x00,			// 0714: next free directory sector low (5)
			'B', 'O', 'O', 'T', ' ', ' ', ' ', ' ',
								// 0716: volume name
			0x28,				// 071E: track count (1)
			0x80,				// 071F: sector count (128)
			0x21,				// 0720: filesystem version (2.1)
			0x80, 0x00,			// 0721: sector size (128)
			0x3E, 0x00,			// 0723: sector references per sector map (62)
			0x01,				// 0725: physical sectors per logical sector (1)
			0x00,				// 0726: volume sequence number
			0xA6,				// 0727: volume random ID
			0x00, 0x00,			// 0728: boot file starting sector
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00,				// 072A
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	0x00, 0x00,	// 0730
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	0x00, 0x00,	// 0738
			0x4C, 0xFF, 0x01	// 0740: JMP $01FF (initiate launch)
	}},
	{ 2, {		// boot sector 2
			0x4C, 0xFF, 0x01	// 3080: JMP $01FF (initiate launch)
	}},
	{ 4, {		// SDFS VTOC
			0x07, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x3F, 0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			0x80,
	}},
	{ 360, {	// DOS 2 VTOC
			0x02,				// version
			0xC5, 0x02,			// total allocatable sectors
			0xC3, 0x02,			// currently free sectors
			0x07, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x3F, 0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
	}},
	{ 361, {	// DOS 2 directory
			0x42, 0x01, 0x00, 0x04, 0x01,
			'S', 'D', 'F', 'S', ' ', ' ', ' ', ' ', 'D', 'A', 'T',

			0x42, 0x01, 0x00, 0x71, 0x01,
			'A', 'U', 'T', 'O', 'R', 'U', 'N', ' ', 'S', 'Y' ,'S',
	}},
	{ 362, {	// SDFS root directory sector map
			0x00, 0x00,
			0x00, 0x00,
			0x6B, 0x01,			// root directory sector (363)
	}},
	{ 363, {	// SDFS root directory
			0x28, 0x00, 0x00, 0x5C, 0x00, 0x00, 'M', 'A', 'I', 'N', ' ', ' ', ' ', ' ', ' ', ' ', ' ', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x08, 0x6D, 0x01, 0x0C, 0x00, 0x00, 'A', 'U', 'T', 'O', 'E', 'X', 'E', 'C', 'B', 'A', 'T', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x08, 0x6F, 0x01, 0x2C, 0x00, 0x00, 'A', 'U', 'T', 'O', 'R', 'U', 'N', ' ', 'S', 'Y', 'S', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x08, 0x70, 0x01, 0x80, 0x01, 0x00, 'D', 'O', 'S', 'D', 'I', 'R', ' ', ' ', 'D', 'A', 'T', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	}},
	{ 365, {	// SDFS AUTOEXEC.BAT sector map
			0x00, 0x00,
			0x00, 0x00,
			0x6E, 0x01,			// AUTOEXEC.BAT sector (366)
	}},
	{ 366, {	// SDFS AUTOEXEC.BAT
			'A', 'U', 'T', 'O', 'R', 'U', 'N', '.', 'S', 'Y', 'S', 0x9B
	}},
	{ 367, {	// SDFS AUTORUN.SYS sector map
			0x00, 0x00,
			0x00, 0x00,
			0x71, 0x01,			// AUTOEXEC.BAT sector (369)
	}},
	{ 368, {	// SDFS DOSDIR.DAT sector map
			0x00, 0x00,
			0x00, 0x00,
			0x68, 0x01,			// DOS VTOC sector (360)
			0x69, 0x01,			// DOS directory sector (361)
			0x6C, 0x01,			// DOS directory sector (364)
	}},
	{ 369, {	// AUTORUN.SYS (same as LOADEXE.XEX from Additions)
			0xFF, 0xFF,
			0x00, 0x22,
			0x1F, 0x22,
			0xA2, 0x0B,			// 2200: LDX #11
			0xBD, 0x13, 0x22,	// 2202: LDA $2211,X
			0x9D, 0x00, 0x03,	// 2205: STA DDEVIC,X
			0xCA,				// 2208: DEX
			0x10, 0xF7,			// 2209: BPL $2202
			0x20, 0x59, 0xE4,	// 220B: JSR SIOV
			0x30, 0x0F,			// 220E: BMI $221F 
			0x4C, 0x23, 0x22,	// 2210: JMP $2223
			0x7D,				// 2213: DDEVICE
			0x01,				// 2214: DUNIT
			0x26,				// 2215: DCOMND = $26
			0x40,				// 2216: DSTATS = $40 (read)
			0x13, 0x22,			// 2217: DBUFLO/HI = $2213
			0x03, 0x00,			// 2219: DTIMLO = $03
			0x80, 0x00,			// 221B: DBYTLO/HI = $0080
			0x00, 0x00,			// 221D: DAUX1/2 = 0
			0x60,				// 221F: RTS
			0xE0, 0x02, 0xE1, 0x02, 0x00, 0x22
		},
		{ 0x04, 0x00, 0x2C }
	},
};

// Fork extension: native rollback shares immutable program bytes. The same
// object embeds those bytes into portable savestates for resynchronization.
class ATSaveStateHLEProgram final : public ATSnapExchangeObject<ATSaveStateHLEProgram, "ATSaveStateHLEProgram"> {
public:
	template<ATExchanger T> void Exchange(T& rw) {
		rw.Transfer("image", &mpImage);
		rw.Transfer("load_index", &mLoadIndex);
		rw.Transfer("launch_time_present", &mbLaunchTimePresent);
		rw.Transfer("launch_time_offset", &mLaunchTimeOffset);
		rw.Transfer("launch_hook", &mbLaunchHook);
		rw.Transfer("continue_hook", &mbContinueHook);
		rw.Transfer("sio_attached", &mbSIOAttached);
		rw.Transfer("sio_command", &mpSIOCommand);
		rw.Transfer("last_kernel_enabled", &mbLastKernelEnabled);
		rw.Transfer("type3_active", &mbType3Active);
		rw.Transfer("type3_enabled", &mbType3Enabled);
		rw.Transfer("disk_boot", &mbDiskBoot);
		rw.Transfer("random_delay", &mbRandomDelay);
		rw.Transfer("random_memory", &mbRandomMemory);
		rw.Transfer("launch_pending", &mbLaunchPending);
		if constexpr (T::IsReader) {
			if (!mpImage) throw ATInvalidSaveStateException();
		}
	}
	void PostDeserialize() override {
		// Referenced bulk-data objects are populated after Exchange.
		const auto& bytes = mpImage->GetReadBuffer();
		if (bytes.size() > UINT32_MAX || mLoadIndex > bytes.size())
			throw ATInvalidSaveStateException();
	}
	vdrefptr<ATSaveStateMemoryBuffer> mpImage;
	vdrefptr<IATBlobImage> mpOriginalImage; // Native-only identity, immutable.
	vdrefptr<IATObjectState> mpSIOCommand;
	uint32 mLoadIndex = 0;
	uint64 mLaunchTimeOffset = 0;
	bool mbLaunchTimePresent = false;
	bool mbLaunchHook = false, mbContinueHook = false, mbSIOAttached = false;
	bool mbLastKernelEnabled = false, mbType3Active = false;
	bool mbType3Enabled = false, mbDiskBoot = false, mbRandomDelay = false;
	bool mbRandomMemory = false, mbLaunchPending = false;
};

void ATHLEProgramLoader::SaveState(IATObjectState **state) {
	if (!mpImage) { *state = nullptr; return; }
	if (!mpSnapshotImage) {
		vdrefptr<ATSaveStateMemoryBuffer> bytes(new ATSaveStateMemoryBuffer);
		const uint8 *data = static_cast<const uint8 *>(mpImage->GetBuffer());
		bytes->GetWriteBuffer().assign(data, data + mpImage->GetSize());
		bytes->mpDirectName = L"hle-program.bin";
		mpSnapshotImage = bytes;
	}
	vdrefptr<ATSaveStateHLEProgram> saved(new ATSaveStateHLEProgram);
	saved->mpImage = &atser_cast<ATSaveStateMemoryBuffer&>(*mpSnapshotImage);
	saved->mpImage->mpDirectName = L"hle-program.bin";
	saved->mpOriginalImage = mpImage;
	saved->mLoadIndex = mProgramLoadIndex;
	saved->mbLaunchTimePresent = mLaunchTime != 0;
	saved->mLaunchTimeOffset = mLaunchTime - mpSim->GetScheduler()->GetTick64();
	saved->mbLaunchHook = mpLaunchHook != nullptr;
	saved->mbContinueHook = mpLoadContinueHook != nullptr;
	saved->mbSIOAttached = mpSIOInterface != nullptr;
	if (mpSIOInterface) mpSIOInterface->SaveActiveCommandState(~saved->mpSIOCommand);
	saved->mbLastKernelEnabled = mbLastKernelEnabledState;
	saved->mbType3Active = mbType3PollActive;
	saved->mbType3Enabled = mbType3PollEnabled;
	saved->mbDiskBoot = mbDiskBootEnabled;
	saved->mbRandomDelay = mbRandomizeLaunchDelay;
	saved->mbRandomMemory = mbRandomizeMemoryOnLoad;
	saved->mbLaunchPending = mbLaunchPending;
	*state = saved.release();
}

void ATHLEProgramLoader::LoadState(const IATObjectState *state) {
	const auto& saved = atser_cast<const ATSaveStateHLEProgram&>(*state);
	if (!saved.mpImage) throw ATInvalidSaveStateException();
	const auto& bytes = saved.mpImage->GetReadBuffer();
	if (bytes.size() > UINT32_MAX || saved.mLoadIndex > bytes.size())
		throw ATInvalidSaveStateException();
	vdrefptr<IATBlobImage> image = saved.mpOriginalImage;
	if (!image) ATCreateBlobImage(kATImageType_Program, bytes.data(), (uint32)bytes.size(), ~image);
	mpCPUHookMgr->UnsetHook(mpLaunchHook);
	mpCPUHookMgr->UnsetHook(mpLoadContinueHook);
	mpSIOInterface = nullptr;
	vdsaferelease <<= mpImage;
	mpImage = image.release();
	mpSnapshotImage = saved.mpImage;
	mProgramLoadIndex = saved.mLoadIndex;
	mLaunchTime = saved.mbLaunchTimePresent
		? mpSim->GetScheduler()->GetTick64() + saved.mLaunchTimeOffset : 0;
	mbLastKernelEnabledState = saved.mbLastKernelEnabled;
	mbType3PollActive = saved.mbType3Active;
	mbType3PollEnabled = saved.mbType3Enabled;
	mbDiskBootEnabled = saved.mbDiskBoot;
	mbRandomizeLaunchDelay = saved.mbRandomDelay;
	mbRandomizeMemoryOnLoad = saved.mbRandomMemory;
	mbLaunchPending = saved.mbLaunchPending;
	if (saved.mbLaunchHook) {
		if (saved.mbSIOAttached)
			mpCPUHookMgr->SetHookMethod(mpLaunchHook, kATCPUHookMode_Always,
				0x1FF, 0, this, &ATHLEProgramLoader::OnDeferredLaunch);
		else
			mpCPUHookMgr->SetHookMethod(mpLaunchHook, kATCPUHookMode_KernelROMOnly,
				ATKernelSymbols::DSKINV, 10, this, &ATHLEProgramLoader::OnDSKINV);
	}
	if (saved.mbContinueHook)
		mpCPUHookMgr->SetHookMethod(mpLoadContinueHook, kATCPUHookMode_Always,
			0x01FE, 0, this, &ATHLEProgramLoader::OnLoadContinue);
	if (saved.mbSIOAttached) {
		mpSIOInterface = mpSIOMgr->AddDevice(this);
		mpSIOInterface->LoadActiveCommandState(saved.mpSIOCommand);
	}
}

ATHLEProgramLoader::ATHLEProgramLoader() {
}

ATHLEProgramLoader::~ATHLEProgramLoader() {
	Shutdown();
}

void ATHLEProgramLoader::Init(ATCPUEmulator *cpu, ATSimulatorEventManager *simEventMgr, ATSimulator *sim, IATDeviceSIOManager *sioMgr) {
	mpCPU = cpu;
	mpCPUHookMgr = cpu->GetHookManager();
	mpSimEventMgr = simEventMgr;
	mpSim = sim;
	mpSIOMgr = sioMgr;
}

void ATHLEProgramLoader::Shutdown() {
	UnloadProgramSymbols();

	mpSIOInterface = nullptr;
	mpSIOMgr = nullptr;

	if (mpCPUHookMgr) {
		mpCPUHookMgr->UnsetHook(mpLoadContinueHook);
		mpCPUHookMgr->UnsetHook(mpLaunchHook);
		mpCPUHookMgr = NULL;
	}

	mpSim = nullptr;
	mpSimEventMgr = nullptr;
	mpCPU = nullptr;

	mbLaunchPending = false;

	mpSnapshotImage = nullptr;
	vdsaferelease <<= mpImage;
}

void ATHLEProgramLoader::LoadProgram(const wchar_t *symbolHintPath, IATBlobImage *image, ATHLEProgramLoadMode launchMode, bool randomizeLaunchDelay) {
	mpSnapshotImage = nullptr;
	vdsaferelease <<= mpImage;

	mbRandomizeLaunchDelay = randomizeLaunchDelay;

	uint32 len = image->GetSize();
	const uint8 *const buf = (const uint8 *)image->GetBuffer();
	mProgramLoadIndex = 0;

	// check if this is a SpartaDOS executable by looking for a reloc block
	if (len >= 4 && (buf[0] == 0xfe || buf[0] == 0xfa) && buf[1] == 0xff)
		throw MyError("Program load failed: this program must be loaded under SpartaDOS X.");

	// check if this is a DOS or Windows executable -- we need to be conservative about this to
	// avoid false positives
	if (IsDOSExecutable(buf, len)) {
		if (IsPEExecutable(buf, len) || IsNEExecutable(buf, len))
			throw MyError("Program load failed: this program is written for Windows.");
		else
			throw MyError("Program load failed: this program is written for MS-DOS.");
	}

	mpSIOInterface = nullptr;
	mbType3PollActive = false;
	mbType3PollEnabled = false;
	mbDiskBootEnabled = false;

	if (launchMode == kATHLEProgramLoadMode_Default)
		mpCPUHookMgr->SetHookMethod(mpLaunchHook, kATCPUHookMode_KernelROMOnly, ATKernelSymbols::DSKINV, 10, this, &ATHLEProgramLoader::OnDSKINV);
	else {
		mpCPUHookMgr->SetHookMethod(mpLaunchHook, kATCPUHookMode_Always, 0x1FF, 0, this, &ATHLEProgramLoader::OnDeferredLaunch);
		mpSIOInterface = mpSIOMgr->AddDevice(this);

		if (launchMode == kATHLEProgramLoadMode_DiskBoot)
			mbDiskBootEnabled = true;
		else if (launchMode == kATHLEProgramLoadMode_Type3Poll) {
			mbType3PollActive = true;
			mbType3PollEnabled = true;
		}
	}

	// try to load symbols
	UnloadProgramSymbols();

	IATDebugger *d = ATGetDebugger();

	if (d && d->IsSymbolLoadingEnabled() && symbolHintPath) {
		static const wchar_t *const kSymExts[]={
			L".lst", L".lab", L".lbl", L".dbg", L".elf"
		};

		VDASSERTCT(vdcountof(kSymExts) == vdcountof(mProgramModuleIds));

		const wchar_t *symbolHintPathExt = VDFileSplitExt(symbolHintPath);

		VDStringW sympath;
		for(int i=0; i<vdcountof(mProgramModuleIds); ++i) {
			// .elf is special, just add it on instead of replacing extension
			if (i == vdcountof(mProgramModuleIds)-1)
				sympath = symbolHintPath;
			else
				sympath.assign(symbolHintPath, symbolHintPathExt);

			sympath += kSymExts[i];

			try {
				const uint32 target0 = 0;
				uint32 moduleId = d->LoadSymbols(sympath.c_str(), false, &target0);

				if (moduleId) {
					mProgramModuleIds[i] = moduleId;

					ATConsolePrintf("Loaded symbols %ls\n", sympath.c_str());
				}
			} catch(const MyError&) {
				// ignore
			}
		}

		// process directives AFTER all symbols have been loaded
		for(const auto& moduleId : mProgramModuleIds) {
			if (moduleId)
				d->ProcessSymbolDirectives(moduleId);
		}

		// load debugger script
		sympath.assign(symbolHintPath, symbolHintPathExt);
		sympath += L".atdbg";

		d->QueueAutoLoadBatchFile(sympath.c_str());
	}

	mbLaunchPending = true;

	image->AddRef();
	mpImage = image;
}

void ATHLEProgramLoader::InitSIO(IATDeviceSIOManager *mgr) {
}

ATHLEProgramLoader::CmdResponse ATHLEProgramLoader::OnSerialBeginCommand(const ATDeviceSIOCommand& cmd) {
	if (!cmd.mbStandardRate)
		return kCmdResponse_NotHandled;

	// check for type 3 poll
	uint8 buf[128];

	if (mbType3PollEnabled && cmd.mDevice == 0x4F && cmd.mCommand == 0x40 && cmd.mAUX[0] == cmd.mAUX[1]) {
		if (cmd.mAUX[0] == 0x00 && cmd.mPollCount == 1 && mbType3PollActive) {
			mbType3PollActive = false;

			// send back handler info
			buf[0] = (uint8)(sizeof(kHandlerData) >> 0);
			buf[1] = (uint8)(sizeof(kHandlerData) >> 8);
			buf[2] = 0x7D;
			buf[3] = 0x00;

			mpSIOInterface->BeginCommand();
			mpSIOInterface->SendACK();
			mpSIOInterface->SendComplete();
			mpSIOInterface->SendData(buf, 4, true);
			mpSIOInterface->EndCommand();

			return kCmdResponse_Start;
		} else if (cmd.mAUX[0] == 0x4F) {
			// Poll Reset -- do not respond, but re-enable poll
			mbType3PollActive = true;
			return kCmdResponse_NotHandled;
		}
	}

	if (cmd.mDevice == 0x31 && mbDiskBootEnabled) {
		if (cmd.mCommand == 0x52) {		// read sector
			// get sector number
			const uint32 sector = VDReadUnalignedLEU16(cmd.mAUX);

			// fail with NAK if outside of SD disk
			if (sector == 0 || sector > 720)
				return kCmdResponse_Fail_NAK;

			// send back sector
			memset(buf, 0, sizeof buf);

			auto it = std::lower_bound(std::begin(kDiskSectors), std::end(kDiskSectors), sector,
				[](const DiskSector& ds, uint32 sec) {
					return ds.mSector < sec;
				}
			);

			if (it != std::end(kDiskSectors) && it->mSector == sector) {
				const size_t headerLen = std::min<size_t>(it->mHeader.size(), sizeof buf);
				memcpy(buf, it->mHeader.begin(), headerLen);

				const size_t footerLen = std::min<size_t>(it->mFooter.size(), sizeof buf);
				if (footerLen)
					memcpy(buf + (sizeof(buf) - footerLen), it->mFooter.begin(), footerLen);
			}

			mpSIOInterface->BeginCommand();
			mpSIOInterface->SendACK();
			mpSIOInterface->SendComplete();
			mpSIOInterface->SendData(buf, 128, true);
			mpSIOInterface->EndCommand();

			return kCmdResponse_Start;
		} else if (cmd.mCommand == 0x53) {	// status
			buf[0] = 0x00;		// drive status
			buf[1] = 0xFF;		// inverted FDC status
			buf[2] = 0xE0;		// format timeout
			buf[3] = 0;

			mpSIOInterface->BeginCommand();
			mpSIOInterface->SendACK();
			mpSIOInterface->SendComplete();
			mpSIOInterface->SendData(buf, 4, true);
			mpSIOInterface->EndCommand();

			return kCmdResponse_Start;
		}

		return kCmdResponse_Fail_NAK;
	}

	if (cmd.mDevice == 0x7D) {
		if (cmd.mCommand == 0x26) {
			// compute block number and check if it is valid
			const uint32 block = cmd.mAUX[0];

			if (block * 128 >= sizeof kHandlerData)
				return kCmdResponse_Fail_NAK;

			// return handler data
			memset(buf, 0, sizeof buf);

			const uint32 offset = block * 128;
			memcpy(buf, kHandlerData + offset, std::min<uint32>(128, sizeof(kHandlerData) - offset));

			mpSIOInterface->BeginCommand();
			mpSIOInterface->SendACK();
			mpSIOInterface->SendComplete();
			mpSIOInterface->SendData(buf, 128, true);
			mpSIOInterface->EndCommand();

			return kCmdResponse_Start;
		}

		// eh, we don't know this command
		return kCmdResponse_Fail_NAK;
	}

	return kCmdResponse_NotHandled;
}

uint8 ATHLEProgramLoader::OnDSKINV(uint16) {
	return StartLoad();
}

uint8 ATHLEProgramLoader::OnDeferredLaunch(uint16) {
	return StartLoad();
}

uint8 ATHLEProgramLoader::StartLoad() {
	ATKernelDatabase kdb(mpCPU->GetMemory());

	mbLaunchPending = false;

	if (mbRandomizeMemoryOnLoad) {
		const uint16 memlo = kdb.MEMLO;
		const uint16 memtop = kdb.MEMTOP;

		ATConsolePrintf("EXE: Randomizing $80-FF and %04X-%04X as the randomize-on-load option is enabled\n", memlo, (memtop - 1) & 0xFFFF);

		uint32 seed = mpSim->RandomizeRawMemory(0x80, 0x80, ATRandomizeAdvanceFast(g_ATRandomizationSeeds.mProgramMemory));
		if (memlo < memtop)
			mpSim->RandomizeRawMemory(memlo, (uint32)(memtop - memlo) + 1, seed);
	}

	// reset run/init addresses to hook location near top of stack
	kdb.INITAD = 0x1FE;
	kdb.RUNAD = 0x1FE;

	// push DSKINV address on stack as return address
	mpCPU->PushWord(ATKernelSymbols::DSKINV - 1);

	// turn on load continuation hook
	mpCPUHookMgr->SetHookMethod(mpLoadContinueHook, kATCPUHookMode_Always, 0x01FE, 0, this, &ATHLEProgramLoader::OnLoadContinue);

	// reset DSKINV stub
	mpCPUHookMgr->UnsetHook(mpLaunchHook);

	// detach serial device
	mpSIOInterface = nullptr;

	// set COLDST so kernel doesn't think we were in the middle of init (required for
	// some versions of Alley Cat)
	kdb.COLDST = 0;

	// Set diskette boot flag in BOOT? so Alex's DOSINI handler runs.
	// Note that we need valid code pointed to by the DOSINI vector, since Stealth
	// crashes otherwise.
	kdb.DOSINI = 0xE4C0;		// KnownRTS

	kdb.BOOT_ |= 0x01;

	mpSimEventMgr->NotifyEvent(kATSimEvent_EXELoad);

	if (mbRandomizeLaunchDelay) {
		// A delay of 28-35K cycles is necessary for VCOUNT to be randomized, and 131K for
		// the 17-bit PRNG. The latter is 4.3 frames.
		mLaunchTime = mpSim->GetScheduler()->GetTick64() + (g_ATRandomizationSeeds.mProgramLaunchDelay % 131071);
	}

	// load next segment
	return OnLoadContinue(0);
}

uint8 ATHLEProgramLoader::OnLoadContinue(uint16 pc) {
	if (mbRandomizeLaunchDelay) {
		if (mLaunchTime > mpSim->GetScheduler()->GetTick64()) {
			mpCPU->PushWord(0x01FE - 1);
			return 0x60;
		}

		mLaunchTime = 0;
	}

	ATCPUEmulatorMemory *mem = mpCPU->GetMemory();
	ATKernelDatabase kdb(mem);

	// check for bad init segments
	bool kernelEnabled = mpSim->IsKernelROMLocation(0xFFFF);

	if (pc) {
		if (mpCPU->GetP() & AT6502::kFlagI)
			ATConsoleWrite("EXE: Warning: Init segment returned with I flag set (DOS compatibility hazard).\n");

		if (!kernelEnabled && mbLastKernelEnabledState)
			ATConsoleWrite("EXE: Warning: Kernel ROM disabled by init segment.\n");
	}

	mbLastKernelEnabledState = kernelEnabled;

	// set INITAD to known RTS
	kdb.INITAD = 0xE4C0;

	// resume loading segments
	const uint8 *src0 = (const uint8 *)mpImage->GetBuffer();
	const uint8 *src = src0 + mProgramLoadIndex;
	const uint8 *srcEnd = src0 + mpImage->GetSize();
	for(;;) {
		// check if we're done
		if (srcEnd - src < 4) {
launch:
			// Looks like we're done. Push DSKINV onto the stack and execute the run routine.

			// remove hooks
			mpCPUHookMgr->UnsetHook(mpLoadContinueHook);
 
			// Last of the Dragons requires X to be set to a particular value to avoid stomping CRITIC.
			// This is what DOS launches with, since it calls CIO right before doing the run.
			mpCPU->SetX(0x20);
			kdb.STATUS = 0x01;

			// C=1 needed by GUNDISK.XEX
			mpCPU->Ldy(0x03);
			mpCPU->SetP(0x03);

			// retrieve run address
			uint16 runAddr = kdb.RUNAD;

			mpCPU->Jump(runAddr);
			ATConsolePrintf("EXE: Launching at %04X\n", runAddr);

			mpSimEventMgr->NotifyEvent(kATSimEvent_EXERunSegment);

			mpSnapshotImage = nullptr;
	vdsaferelease <<= mpImage;
			return 0x4C;
		}

		// read start/end addresses for this segment
		uint16 start = VDReadUnalignedLEU16(src+0);
		if (start == 0xFFFF) {
			src += 2;
			continue;
		}

		uint16 end = VDReadUnalignedLEU16(src+2);
		src += 4;

		uint32 len = (uint32)(end - start) + 1;
		if (end < start || (uint32)(srcEnd - src) < len) {
			if (end >= start) {
				len = (uint32)(srcEnd - src);
				ATConsoleWrite("WARNING: Invalid Atari executable: bad start/end range.\n");
			} else {
				ATConsoleWrite("ERROR: Invalid Atari executable: bad start/end range.\n");
				goto launch;
			}
		}

		// if this is the first segment, set RUNAD to this segment as a default
		if (kdb.RUNAD == 0x01FE)
			kdb.RUNAD = start;

		// load segment data into memory
		ATConsolePrintf("EXE: Loading program %04X-%04X to %04X-%04X\n", (unsigned)(src-src0), (unsigned)(src-src0)+len-1, start, end);

		for(uint32 i=0; i<len; ++i)
			mem->WriteByte(start + i, *src++);

		if (auto *hm = mpSim->GetHeatMap())
			hm->PresetMemoryRange(start, len);

		// fake SIO read
		kdb.PBCTL = 0x3C;
		kdb.PBCTL = 0x34;
		kdb.PBCTL = 0x3C;

		ATClearPokeyTimersOnDiskIo(kdb);

		// fake DOS CIO read through IOCB#1
		mpCPU->SetA(0x00);
		mpCPU->SetX(0x10);
		mpCPU->Ldy(src == srcEnd ? 0x03 : 0x01);
		mpCPU->SetP(0x03);

		// check if INITAD has changed
		uint16 initAddr = kdb.INITAD;
		if (initAddr != 0xE4C0) {
			ATConsolePrintf("EXE: Jumping to %04X\n", initAddr);
			break;
		}
	}

	mProgramLoadIndex = src - src0;

	// push virtual load hook ($01FE-1) onto stack and jsr through (INITAD)
	mpCPU->PushWord(0x01FD);
	mpCPU->PushWord(kdb.INITAD - 1);

	mpSimEventMgr->NotifyEvent(kATSimEvent_EXEInitSegment);
	return 0x60;
}

void ATHLEProgramLoader::UnloadProgramSymbols() {
	IATDebugger *d = ATGetDebugger();

	if (!d)
		return;

	for(uint32& moduleId : mProgramModuleIds) {
		if (!moduleId)
			continue;

		d->UnloadSymbols(moduleId);	
		moduleId = 0;
	}
}
