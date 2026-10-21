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
#include <vd2/system/vdstl_vectorview.h>
#include <at/atcore/cio.h>
#include <at/atio/blobimage.h>
#include <at/atcore/snapshotimpl.h>
#include <at/atcore/savestate.h>
#include "hlebasicloader.h"
#include "hleutils.h"
#include "kerneldb.h"
#include "console.h"
#include "cpu.h"
#include "cpuhookmanager.h"
#include "debugger.h"
#include "simeventmanager.h"
#include "simulator.h"

class ATSaveStateHLEBasic final : public ATSnapExchangeObject<ATSaveStateHLEBasic, "ATSaveStateHLEBasic"> {
public:
	template<ATExchanger T> void Exchange(T& rw) {
		rw.Transfer("image", &mpImage);
		rw.Transfer("index", &mIndex);
		rw.Transfer("state", &mState);
		rw.Transfer("iocb", &mIOCB);
		rw.Transfer("push_tape_key", &mbPushTapeKey);
		rw.Transfer("launch_hook", &mbLaunchHook);
		rw.Transfer("launch_pending", &mbLaunchPending);
		if constexpr (T::IsReader) {
			if (mState > 2 || (mIOCB & 0x8F)
				|| (!mpImage && (mIndex || mIOCB || mState == 1)))
				throw ATInvalidSaveStateException();
		}
	}
	void PostDeserialize() override {
		// Referenced bulk-data objects are populated after Exchange.
		if (mpImage) {
			const auto& bytes = mpImage->GetReadBuffer();
			if (bytes.size() > UINT32_MAX || bytes.size() < 14
				|| mIndex > bytes.size()) throw ATInvalidSaveStateException();
		}
	}
	vdrefptr<ATSaveStateMemoryBuffer> mpImage;
	vdrefptr<IATBlobImage> mpOriginalImage;
	uint32 mIndex = 0;
	uint8 mState = 0, mIOCB = 0;
	bool mbPushTapeKey = false, mbLaunchHook = false, mbLaunchPending = false;
};

void ATHLEBasicLoader::SaveState(IATObjectState **state) {
	vdrefptr<ATSaveStateHLEBasic> saved(new ATSaveStateHLEBasic);
	if (mpImage && !mpSnapshotImage) {
		vdrefptr<ATSaveStateMemoryBuffer> bytes(new ATSaveStateMemoryBuffer);
		const uint8 *data = static_cast<const uint8 *>(mpImage->GetBuffer());
		bytes->GetWriteBuffer().assign(data, data + mpImage->GetSize());
		bytes->mpDirectName = L"hle-basic.bin";
		mpSnapshotImage = bytes;
	}
	if (mpSnapshotImage) {
		saved->mpImage = &atser_cast<ATSaveStateMemoryBuffer&>(*mpSnapshotImage);
		saved->mpImage->mpDirectName = L"hle-basic.bin";
	}
	saved->mpOriginalImage = mpImage;
	saved->mIndex = mProgramIndex;
	saved->mState = (uint8)mState;
	saved->mIOCB = mProgramIOCB;
	saved->mbPushTapeKey = mbPushKeyToLoadTape;
	saved->mbLaunchHook = mpLaunchHook != nullptr;
	saved->mbLaunchPending = mbLaunchPending;
	*state = saved.release();
}

void ATHLEBasicLoader::LoadState(const IATObjectState *state) {
	const auto& saved = atser_cast<const ATSaveStateHLEBasic&>(*state);
	if (saved.mState > 2 || (saved.mIOCB & 0x8F)
		|| (saved.mpImage && (saved.mpImage->GetReadBuffer().size() > UINT32_MAX
			|| saved.mpImage->GetReadBuffer().size() < 14
			|| saved.mIndex > saved.mpImage->GetReadBuffer().size()))
		|| (!saved.mpImage && (saved.mIndex || saved.mIOCB || saved.mState == 1)))
		throw ATInvalidSaveStateException();
	vdrefptr<IATBlobImage> image = saved.mpOriginalImage;
	if (!image && saved.mpImage) {
		const auto& bytes = saved.mpImage->GetReadBuffer();
		ATCreateBlobImage(kATImageType_BasicProgram, bytes.data(), (uint32)bytes.size(), ~image);
	}
	mpCPUHookMgr->UnsetHook(mpLaunchHook);
	vdsaferelease <<= mpImage;
	mpImage = image.release();
	mpSnapshotImage = saved.mpImage;
	mProgramIndex = saved.mIndex;
	mState = (State)saved.mState;
	mProgramIOCB = saved.mIOCB;
	mbPushKeyToLoadTape = saved.mbPushTapeKey;
	mbLaunchPending = saved.mbLaunchPending;
	if (saved.mbLaunchHook)
		mpCPUHookMgr->SetHookMethod(mpLaunchHook, kATCPUHookMode_KernelROMOnly,
			ATKernelSymbols::CIOV, 10, this, &ATHLEBasicLoader::OnCIOV);
}

ATHLEBasicLoader::ATHLEBasicLoader() {
}

ATHLEBasicLoader::~ATHLEBasicLoader() {
	Shutdown();
}

void ATHLEBasicLoader::Init(ATCPUEmulator *cpu, ATSimulatorEventManager *simEventMgr, ATSimulator *sim) {
	mpCPU = cpu;
	mpCPUHookMgr = cpu->GetHookManager();
	mpSimEventMgr = simEventMgr;
	mpSim = sim;
	mProgramIOCB = 0;
}

void ATHLEBasicLoader::Shutdown() {
	mpSnapshotImage = nullptr;
	vdsaferelease <<= mpImage;

	if (mpCPUHookMgr) {
		mpCPUHookMgr->UnsetHook(mpLaunchHook);
		mpCPUHookMgr = NULL;
	}

	mpSim = NULL;
	mpSimEventMgr = NULL;
	mpCPU = NULL;

	mbLaunchPending = false;
}

void ATHLEBasicLoader::LoadProgram(IATBlobImage *image) {
	mpSnapshotImage = nullptr;
	vdsaferelease <<= mpImage;

	mpImage = image;
	image->AddRef();

	uint32 len = image->GetSize();

	if (len < 14)
		throw MyError("Invalid BASIC program: must be at least 12 bytes.");

	mpCPUHookMgr->SetHookMethod(mpLaunchHook, kATCPUHookMode_KernelROMOnly, ATKernelSymbols::CIOV, 10, this, &ATHLEBasicLoader::OnCIOV);

	mbLaunchPending = true;
	mState = State::RunProgram;
}

void ATHLEBasicLoader::LoadTape(bool pushKeyToLoadTape) {
	mpSnapshotImage = nullptr;
	vdsaferelease <<= mpImage;

	mpCPUHookMgr->SetHookMethod(mpLaunchHook, kATCPUHookMode_KernelROMOnly, ATKernelSymbols::CIOV, 10, this, &ATHLEBasicLoader::OnCIOV);

	// We don't need to keep the BASIC loader across a cold reset for tape loads since
	// the simulator will re-create it.
	mbLaunchPending = false;

	mbPushKeyToLoadTape = pushKeyToLoadTape;

	mState = State::RunTape;
}

uint8 ATHLEBasicLoader::OnCIOV(uint16) {
	ATCPUEmulatorMemory *mem = mpCPU->GetMemory();
	ATKernelDatabase kdb(mem);

	mbLaunchPending = false;

	// check for invalid IOCB
	const uint8 iocb = mpCPU->GetX();
	if (iocb & 0x8f)
		return 0;

	const uint8 cmd = mem->ReadByte(ATKernelSymbols::ICCMD + iocb);
	if (mProgramIOCB && iocb == mProgramIOCB) {
		switch(cmd) {
			case kATCIOCmd_Open:
				mpCPU->Ldy(kATCIOStat_IOCBInUse);
				return 0x60;

			case kATCIOCmd_Close:
				mProgramIOCB = 0;
				mem->WriteByte(ATKernelSymbols::ICHID + iocb, 0xFF);
				mpCPU->Ldy(kATCIOStat_Success);
				mpCPUHookMgr->UnsetHook(mpLaunchHook);
				return 0x60;

			case kATCIOCmd_GetChars:
				if (mProgramIndex >= mpImage->GetSize()) {
					mpCPU->Ldy(kATCIOStat_EndOfFile);
				} else {
					uint32 maxRead = (uint32)(mpImage->GetSize() - mProgramIndex);
					uint32 addr = mem->ReadByte(ATKernelSymbols::ICBAL + iocb) + 256*mem->ReadByte(ATKernelSymbols::ICBAH + iocb);
					uint32 len = mem->ReadByte(ATKernelSymbols::ICBLL + iocb) + 256*mem->ReadByte(ATKernelSymbols::ICBLH + iocb);
					bool trunc = false;

					if (len > maxRead) {
						trunc = true;
						len = maxRead;
					}

					const uint8 *src = (const uint8 *)mpImage->GetBuffer();
					for(uint32 i=0; i<len; ++i)
						mem->WriteByte((uint16)(addr++), src[mProgramIndex++]);

					mem->WriteByte(ATKernelSymbols::ICBLL + iocb, (uint8)len);
					mem->WriteByte(ATKernelSymbols::ICBLH + iocb, (uint8)(len >> 8));

					mpCPU->Ldy(trunc ? kATCIOStat_TruncRecord : kATCIOStat_Success);
				}
				return 0x60;
		
			case kATCIOCmd_PutChars:
				mpCPU->Ldy(kATCIOStat_ReadOnly);
				return 0x60;

			default:
				mpCPU->Ldy(kATCIOStat_NotSupported);
				return 0x60;
		}
	} else if (mpImage && cmd == kATCIOCmd_Open) {
		uint16 lineAddr = mem->ReadByte(ATKernelSymbols::ICBAL + iocb) + 256*mem->ReadByte(ATKernelSymbols::ICBAH + iocb);

		if (mem->ReadByte(lineAddr) != '*')
			return 0;

		if ((mem->ReadByte(ATKernelSymbols::ICAX1 + iocb) & 0x0c) != 0x04) {
			mpCPU->Ldy(kATCIOStat_ReadOnly);
			return 0x60;
		}

		mProgramIOCB = iocb;
		mProgramIndex = 0;
		mpCPU->Ldy(kATCIOStat_Success);
		return 0x60;
	}

	// check that this is a get chars request on IOCB 0
	if (iocb != 0)
		return 0;

	if (cmd != kATCIOCmd_GetRecord)
		return 0;

	// write command line
	static constexpr uint8 kRunProgramCmdLine[]={ 'R', 'U', 'N', ' ', '"', '*', '"', 0x9B };
	static constexpr uint8 kRunTapeCmdLine[]={ 'R', 'U', 'N', ' ', '"', 'C', '"', 0x9B };

	uint8 fastTapeShim[] {
		0x0B, 0x04,			// 0400: 0B 04     DTA A($040C-1)
		0x00, 0x00,			// 0402: 00 00     DTA A(CLOSE-1)
		0x00, 0x00,			// 0404: 00 00     DTA A(GET-1)
		0x00, 0x00,			// 0406: 00 00     DTA A(PUT-1)
		0x00, 0x00,			// 0408: 00 00     DTA A(STATUS-1)
		0x00, 0x00,			// 040A: 00 00     DTA A(SPECIAL-1)
		0xA9, 0x80,			// 040C: A9 80     LDA #$80
		0x85, 0x2B,			// 040E: 85 2B     STA ICAX2Z
		0xA9, 0x00,			// 0410: A9 00     LDA #$00
		0x8D, 0x00, 0x03,	// 0412: 85 00 00  STA HATABS+1+n
		0xA9, 0x00,			// 0415: A9 00     LDA #$00
		0x8D, 0x00, 0x03,	// 0417: 85 00 00  STA HATABS+2+n
		0x4C, 0x00, 0x00,	// 041A: 4C 00 00  JMP $0000
	};

	uint16 lineAddr = kdb.ICBAL_ICBAH;
	uint32 lineLen = kdb.ICBLL_ICBLH;

	vdvector_view<const uint8> cmdline {};

	switch(mState) {
		default:
			return 0;

		case State::RunProgram:
			cmdline = kRunProgramCmdLine;
			mState = State::None;
			// leave hook attached for program load
			break;

		case State::RunTape:
			cmdline = kRunTapeCmdLine;
			mState = State::None;
			mpCPUHookMgr->UnsetHook(mpLaunchHook);

			if (mbPushKeyToLoadTape)
				kdb.CH = 0x21;

			// Temporarily patch C: device.
			//
			// We need to do this because while many tapes are fine with CLOAD,
			// some require RUN "C" to work. This is because they have a
			// protected loader with a zero-length immediate line that will hang
			// BASIC on a load. However, we can't use RUN "C" for all tapes
			// because that sets long IRG mode. To get around this, we install
			// a temporary shim for C: that forces on short IRG mode and then
			// chains to the standard C: handler.
			for(int i=11; i>=0; --i) {
				const uint16 centry = ATKernelSymbols::HATABS + 3*i;
				if (mem->DebugReadByte(centry) == 'C') {
					// read address of C: table
					const uint16 ctab =
						mem->DebugReadByte(centry + 1) +
						mem->DebugReadByte(centry + 2) * 256;

					// copy the C: table into the patch (since it's in ROM)
					// skip the OPEN handler, which we're shimming
					for(int j=2; j<12; ++j)
						fastTapeShim[j] = mem->ReadByte((ctab + j) & 0xFFFF);

					// patch HATABS to point to the modified table
					mem->WriteByte(centry + 1, 0x00);
					mem->WriteByte(centry + 2, 0x04);

					// patch the original C: table pointer into the shim so it
					// can restore it
					fastTapeShim[0x11] = (uint8)ctab;
					fastTapeShim[0x16] = (uint8)(ctab >> 8);

					fastTapeShim[0x13] = (uint8)(centry + 1);
					fastTapeShim[0x18] = (uint8)(centry + 2);

					// patch the JMP exit from the shim
					const uint16 copen = mem->DebugReadByte(ctab) + 256*mem->DebugReadByte(ctab+1) + 1;
					fastTapeShim[0x1B] = (uint8)copen;
					fastTapeShim[0x1C] = (uint8)(copen >> 8);

					// write the shim into the cassette buffer
					for(int j=0; j<(int)sizeof(fastTapeShim); ++j)
						mem->WriteByte(0x0400 + j, fastTapeShim[j]);

					break;
				}
			}
			break;
	}

	uint32 cmdlineLen = cmdline.size();
	if (lineLen > cmdlineLen)
		lineLen = cmdlineLen;

	for(uint32 i=0; i<lineLen; ++i)
		mem->WriteByte(lineAddr++, cmdline[i]);

	kdb.ICBLL_ICBLH = lineLen;

	uint8 status = lineLen < cmdlineLen ? kATCIOStat_TruncRecord : kATCIOStat_Success;
	kdb.STATUS = status;

	mpCPU->Ldy(status);
	return 0x60;
}
