//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2025 Avery Lee
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
#include <vd2/system/binary.h>
#include <vd2/system/file.h>
#include <vd2/system/vdstl_algorithm.h>
#include "vgmwriter.h"
#include "simulator.h"
#include "uirender.h"

class ATVgmWriter : public IATRegisterWriteLogger, public vdrefcounted<IATVgmWriter> {
	ATVgmWriter(const ATVgmWriter&) = delete;
	ATVgmWriter& operator=(const ATVgmWriter&) = delete;
public:
	ATVgmWriter();
	~ATVgmWriter();

	void Init(const wchar_t *fn, ATSimulator& sim) override;
	void Shutdown() override;

	void CheckExceptions() override;

public:	// IATRegisterWriteLogger
	void LogRegisterWrites(vdspan<const ATMemoryWriteLogEntry> entries) override;

private:
	void FlushRegisterChanges();
	void WriteRegister(uint32 reg, uint8 val);
	void Write(uint32 data, uint32 len);
	void WriteRaw(const void *p, size_t len);
	void Flush();

	uint32 mLastCycle = 0;
	uint32 mSamplesPerCycleF32 = 0;
	uint32 mSampleAccumF32 = 0;
	uint32 mSampleCount = 0;
	uint32 mBytesWrittenCount = 0;
	bool mbStereo = false;
	bool mbRecordingStarted = false;
	bool mbInitialRegistersPending = true;

	ATSimulator *mpSim = nullptr;
	IATUIRenderer *mpUIRenderer = nullptr;
	uint32 mWriteOffset = 0;
	uint32 mSecondsCounter = 0;

	uint8 mPrevRegisterValues[32] {};
	uint8 mNextRegisterValues[32] {};

	VDFileStream mFileStream;

	std::exception_ptr mPendingException;

	uint8 mHeader[256] {};

	static constexpr size_t kWriteBufferSize = 4096;
	uint8 mWriteBuffer[4096 + 8] {};
};

vdrefptr<IATVgmWriter> ATCreateVgmWriter() {
	return vdmakerefcounted<ATVgmWriter>();
}

ATVgmWriter::ATVgmWriter() {
}

ATVgmWriter::~ATVgmWriter() {
	Shutdown();
}

void ATVgmWriter::Init(const wchar_t *fn, ATSimulator& sim) {
	mpSim = &sim;
	mpUIRenderer = sim.GetUIRenderer();

	sim.SetPokeyWriteLogger(this);

	mFileStream.open(fn, nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);

	const ATPokeyEmulator& pokey = sim.GetPokey();

	mbStereo = pokey.IsStereoEnabled();

	// set signature
	mHeader[0] = 0x56;
	mHeader[1] = 0x67;
	mHeader[2] = 0x6d;
	mHeader[3] = 0x20;

	// set version (1.72)
	mHeader[8] = 0x72;
	mHeader[9] = 0x01;

	// set VGM data position to 0x100
	VDWriteUnalignedLEU32(&mHeader[0x34], 0x100 - 0x34);

	// set POKEY clock + dual POKEY bit
	double pokeyClock = sim.GetScheduler()->GetRate().asDouble();
	VDWriteUnalignedLEU32(&mHeader[0xB0], VDRoundToInt32(pokeyClock) + (mbStereo ? 1 << 30 : 0));

	mSamplesPerCycleF32 = (uint32)(0.5 + (44100.0 * 0x1p32) / pokeyClock);

	// write provisional header
	memcpy(mWriteBuffer, mHeader, 256);
	mWriteOffset = 256;

	// initialize register values
	ATPokeyRegisterState rstate {};
	pokey.GetRegisterState(rstate);

	vdcopy_checked_r(mPrevRegisterValues, rstate.mReg);
	vdcopy_checked_r(mNextRegisterValues, mPrevRegisterValues);
	
	mLastCycle = sim.GetScheduler()->GetTick();

	mpUIRenderer->SetRecordingPosition(0, 0, false);

	// check if audio is already playing and we should hot-start recording
	uint8 volumes = rstate.mReg[1] | rstate.mReg[3] | rstate.mReg[5] | rstate.mReg[7];

	if (mbStereo)
		volumes |= rstate.mReg[0x11] | rstate.mReg[0x13] | rstate.mReg[0x15] | rstate.mReg[0x17];

	if (volumes & 0x0F)
		mbRecordingStarted = true;
}

void ATVgmWriter::Shutdown() {
	if (mpSim) {
		mpSim->SetPokeyWriteLogger(nullptr);
		mpSim = nullptr;
	}

	if (mpUIRenderer) {
		mpUIRenderer->SetRecordingPosition();
		mpUIRenderer = nullptr;
	}

	try {
		// add the end token
		Write(0x66, 1);

		// fix up sample count in header
		VDWriteUnalignedLEU32(&mHeader[0x18], mSampleCount);

		// fix up GD3 offset in header
		VDWriteUnalignedLEU32(&mHeader[0x14], mBytesWrittenCount + mWriteOffset - 0x14);

		// create GD3 in UTF-16LE (VGM standard requires UTF-16)
		// On Windows, wchar_t is 2 bytes (native UTF-16).
		// On Linux/macOS, wchar_t is 4 bytes, so we build UTF-16LE explicitly.
		std::vector<uint16> gd3;
		auto gd3AddStr = [&gd3](const wchar_t *s) {
			while (*s) {
				wchar_t c = *s++;
				if (c <= 0xFFFF)
					gd3.push_back((uint16)c);
				else {
					// Encode as surrogate pair
					c -= 0x10000;
					gd3.push_back((uint16)(0xD800 | (c >> 10)));
					gd3.push_back((uint16)(0xDC00 | (c & 0x3FF)));
				}
			}
			gd3.push_back(0);
		};
		gd3AddStr(L"");			// track name (English)
		gd3AddStr(L"");			// track name (original)
		gd3AddStr(L"");			// game name (English)
		gd3AddStr(L"");			// game name (original)
		gd3AddStr(L"Atari 400/800");	// system name (English)
		gd3AddStr(L"");			// system name (original)
		gd3AddStr(L"");			// original track author (English)
		gd3AddStr(L"");			// original track author (original)
		gd3AddStr(L"");			// date of release
		gd3AddStr(L"");			// name of person who converted
		gd3AddStr(L"");			// notes

		// write GD3
		WriteRaw("Gd3 ", 4);
		Write(0x0100, 4);
		Write(gd3.size() * 2, 4);
		WriteRaw(gd3.data(), gd3.size() * 2);
		Flush();

		// fix up file length in header
		VDWriteUnalignedLEU32(&mHeader[4], mBytesWrittenCount - 4);

		// rewrite header
		memcpy(mWriteBuffer, mHeader, 256);
		mWriteOffset = 256;

		mFileStream.seek(0);
		Flush();

		mFileStream.close();
	} catch(...) {
		if (!mPendingException)
			mPendingException = std::current_exception();
	}
}

void ATVgmWriter::CheckExceptions() {
	if (mPendingException)
		std::rethrow_exception(std::move(mPendingException));
}

void ATVgmWriter::LogRegisterWrites(vdspan<const ATMemoryWriteLogEntry> entries) {
	if (entries.empty() || mPendingException)
		return;

	uint64 accum = mSampleAccumF32;
	const uint32 addrMask = mbStereo ? 0x1F : 0x0F;

	for(const ATMemoryWriteLogEntry& e : entries) {
		// filter out unnecessary POKEY registers
		switch(e.mAddress & 0x0F) {
			case 0x00:	// AUDF1
			case 0x01:	// AUDC1
			case 0x02:	// AUDF2
			case 0x03:	// AUDC2
			case 0x04:	// AUDF3
			case 0x05:	// AUDC3
			case 0x06:	// AUDF4
			case 0x07:	// AUDC4
			case 0x08:	// AUDCTL
			case 0x0F:	// SKCTL
				break;

			default:
				continue;
		}

		// mask address to canonical register
		const uint32 reg = e.mAddress & addrMask;

		// check for delay if samples have passed
		uint32 dcyc = e.mCycle - mLastCycle;
		if (dcyc) {
			mLastCycle = e.mCycle;

			accum += (uint64)dcyc * mSamplesPerCycleF32;

			uint32 dsamples = (uint32)(accum >> 32);
			accum = (uint32)accum;

			if (dsamples && mbRecordingStarted) {
				// flush all changed registers
				FlushRegisterChanges();

				// update sample count
				mSampleCount += dsamples;

				// write delay
				while(dsamples >= 65535) {
					dsamples -= 65535;

					Write(0xFFFF61, 3);
				}

				if (dsamples) {
					if (dsamples == 735) {
						Write(0x62, 1);
					} else if (dsamples == 882) {
						Write(0x63, 1);
					} else if (dsamples <= 16) {
						Write(0x6F + dsamples, 1);
					} else {
						Write(0x61 + (dsamples << 8), 3);
					}
				}

				uint32 secs = mSampleCount / 44100;

				if (mSecondsCounter != secs) {
					mSecondsCounter = secs;

					mpUIRenderer->SetRecordingPosition((float)secs, mBytesWrittenCount + mWriteOffset, false);
				}
			}
		}

		// If recording has not yet started, check if we should start -- recording
		// should start when at least one volume is above zero. Ignore any changes
		// to AUDCTL or SKCTL. This is done after the delay check as we don't want
		// to start with a delay.
		if (!mbRecordingStarted) {
			if ((e.mAddress & 0x09) == 0x01 && (e.mValue & 0x0F)) {
				mbRecordingStarted = true;
				mLastCycle = e.mCycle;
				accum = 0;
			}
		}

		// update shadow location
		mNextRegisterValues[reg] = e.mValue;
	}

	mSampleAccumF32 = accum;
}

void ATVgmWriter::FlushRegisterChanges() {
	if (mbInitialRegistersPending) {
		mbInitialRegistersPending = false;

		for(uint32 i = 0; i < 0x20; ++i) {
			mPrevRegisterValues[i] = ~mNextRegisterValues[i];

			if (i == 0x09)
				i = 0x0E;
			else if (i == 0x0F && !mbStereo)
				break;
			else if (i == 0x19)
				i = 0x1E;
		}
	}

	for(int i=0; i<32; ++i) {
		if (mPrevRegisterValues[i] != mNextRegisterValues[i]) {
			mPrevRegisterValues[i] = mNextRegisterValues[i];

			WriteRegister(i, mNextRegisterValues[i]);
		}
	}
}

void ATVgmWriter::WriteRegister(uint32 reg, uint8 val) {
	// move stereo addressing bit to VGM dual chip bit
	if (reg & 0x10)
		reg += 0x70;

	Write(0xBB + (reg << 8) + ((uint32)val << 16), 3);
}

void ATVgmWriter::Write(uint32 data, uint32 len) {
	if (mWriteOffset + len > kWriteBufferSize)
		Flush();

	VDWriteUnalignedLEU32(&mWriteBuffer[mWriteOffset], data);
	mWriteOffset += len;
}

void ATVgmWriter::WriteRaw(const void *p, size_t len) {
	while(len) {
		size_t tc = kWriteBufferSize - mWriteOffset;
		if (tc > len)
			tc = len;

		if (tc) {
			memcpy(&mWriteBuffer[mWriteOffset], p, tc);
			mWriteOffset += tc;
			len -= tc;
			p = (const char *)p + tc;
		} else
			Flush();
	}
}

void ATVgmWriter::Flush() {
	if (mWriteOffset && !mPendingException) {
		try {
			mFileStream.Write(mWriteBuffer, mWriteOffset);
			mBytesWrittenCount += mWriteOffset;
		} catch(...) {
			mPendingException = std::current_exception();
		}

		mWriteOffset = 0;
	}
}
