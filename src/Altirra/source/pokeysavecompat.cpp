//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2021 Avery Lee
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
#include <vd2/system/binary.h>
#include <at/ataudio/pokey.h>
#include <at/ataudio/pokeysavestate.h>
#include "pokeysavecompat.h"
#include "savestate.h"

ATPokeyEmulatorOldStateLoader::ATPokeyEmulatorOldStateLoader(ATPokeyEmulator& emu)
	: mEmu(emu)
	, mpState(new ATSaveStatePokey)
{
}

ATPokeyEmulatorOldStateLoader::~ATPokeyEmulatorOldStateLoader() {
}

void ATPokeyEmulatorOldStateLoader::BeginLoadState(ATSaveStateReader& reader) {
	reader.RegisterHandlerMethod(kATSaveStateSection_Arch, VDMAKEFOURCC('P', 'O', 'K', 'Y'), this, &ATPokeyEmulatorOldStateLoader::LoadStateArch);
	reader.RegisterHandlerMethod(kATSaveStateSection_Private, VDMAKEFOURCC('P', 'O', 'K', 'Y'), this, &ATPokeyEmulatorOldStateLoader::LoadStatePrivate);
	reader.RegisterHandlerMethod(kATSaveStateSection_ResetPrivate, 0, this, &ATPokeyEmulatorOldStateLoader::LoadStateResetPrivate);
	reader.RegisterHandlerMethod(kATSaveStateSection_End, 0, this, &ATPokeyEmulatorOldStateLoader::EndLoadState);
}

void ATPokeyEmulatorOldStateLoader::LoadStateArch(ATSaveStateReader& reader) {
	ATSaveStatePokey& state = static_cast<ATSaveStatePokey&>(*mpState);

	for(int i=0; i<4; ++i) {
		state.mAUDF[i] = reader.ReadUint8();
		state.mAUDC[i] = reader.ReadUint8();
	}

	state.mAUDCTL = reader.ReadUint8();
	state.mIRQEN = reader.ReadUint8();
	state.mIRQST = reader.ReadUint8();
	state.mSKCTL = reader.ReadUint8();
}

void ATPokeyEmulatorOldStateLoader::LoadStatePrivate(ATSaveStateReader& reader) {
	ATSaveStatePokey& state = static_cast<ATSaveStatePokey&>(*mpState);

	if (!state.mpInternalState)
		state.mpInternalState = new ATSaveStatePokeyInternal;

	ATSaveStatePokeyInternal& pstate = *state.mpInternalState;

	state.mALLPOT = reader.ReadUint8();
	state.mKBCODE = reader.ReadUint8();

	for(int i=0; i<4; ++i)
		pstate.mTimerCounters[i] = std::clamp<uint32>(reader.ReadUint32(), 1, 256);

	for(int i=0; i<4; ++i)
		pstate.mTimerBorrowCounters[i] = reader.ReadUint32() & 3;

	pstate.mClock15Offset = reader.ReadUint8();
	pstate.mClock64Offset = reader.ReadUint8();

	pstate.mPoly9Offset = reader.ReadUint16() % 511;
	pstate.mPoly17Offset = reader.ReadUint32() % 131071;

	if (reader.CheckAvailable(8)) {
		if (reader.CheckAvailable(26))
			pstate.mPolyShutOffTime = reader.ReadUint64();

		if (reader.CheckAvailable(18)) {
			pstate.mRendererState.mPoly4Offset = reader.ReadUint8() % 15;
			pstate.mRendererState.mPoly5Offset = reader.ReadUint8() % 31;
			pstate.mRendererState.mPoly9Offset = reader.ReadUint16() % 511;
			pstate.mRendererState.mPoly17Offset = reader.ReadUint32() % 131071;
			pstate.mRendererState.mOutputFlipFlops = 0;

			for(int i=0; i<6; ++i)
				pstate.mRendererState.mOutputFlipFlops += (reader.ReadUint8() & 1) << i;

			// discard outputs (no longer needed -- high-pass XOR is done dynamically now)
			for(int i=0; i<4; ++i)
				reader.ReadUint8();
		}
	}

	mEmu.LoadState(mpState);
}

void ATPokeyEmulatorOldStateLoader::LoadStateResetPrivate(ATSaveStateReader& reader) {
	mEmu.LoadState(mpState);
}

void ATPokeyEmulatorOldStateLoader::EndLoadState(ATSaveStateReader& reader) {
	mEmu.PostLoadState();
}
