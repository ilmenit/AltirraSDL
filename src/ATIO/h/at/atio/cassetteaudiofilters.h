//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
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

#ifndef f_AT_ATIO_CASSETTEAUDIOFILTERS_H
#define f_AT_ATIO_CASSETTEAUDIOFILTERS_H

#include <at/atcore/fft.h>
#include <vd2/system/error.h>
#include <vd2/system/thread.h>

class IATAudioReader;

class IATCassetteAudioSource {
public:
	virtual uint32 ReadAudio(sint16 (*dst)[2], uint32 n) = 0;
};

////////////////////////////////////////////////////////////////////////////////

class ATCassetteAudioSource final : public IATCassetteAudioSource {
public:
	ATCassetteAudioSource(IATAudioReader& reader);

	uint32 GetLastDataPosKB() const;
	uint32 ReadAudio(sint16 (*dst)[2], uint32 n) override;

private:
	IATAudioReader& mAudioReader;
	VDAtomicInt mDataPos = 0;
};

////////////////////////////////////////////////////////////////////////////////

class ATCassetteAudioPeakMapFilter final : public IATCassetteAudioSource {
public:
	ATCassetteAudioPeakMapFilter(IATCassetteAudioSource& source, bool stereo, double inputSamplesPerPeakSample, vdfastvector<uint8>& peakMapL, vdfastvector<uint8>& peakMapR);

	uint32 ReadAudio(sint16 (*dst)[2], uint32 n) override;

private:
	void ProcessPeaks(const sint16 (*samples)[2], uint32 n);

	IATCassetteAudioSource& mSource;

	bool mbStereo = false;
	sint32 mValAccumL0 = 0;
	sint32 mValAccumL1 = 0;
	sint32 mValAccumR0 = 0;
	sint32 mValAccumR1 = 0;
	uint64 mRateAccum = UINT32_C(0x80000000);
	uint64 mRateAccumInc = 0;

	uint32 mInputSamplesLeft = 0;

	vdfastvector<uint8>& mPeakMapL;
	vdfastvector<uint8>& mPeakMapR;
};

////////////////////////////////////////////////////////////////////////////////

class ATCassetteAudioResampler final : public IATCassetteAudioSource {
public:
	ATCassetteAudioResampler(IATCassetteAudioSource& source, uint64 sampleStepF32);

	uint32 ReadAudio(sint16 (*dst)[2], uint32 n) override;

private:
	IATCassetteAudioSource& mSource;

	static constexpr uint32 kInputBufferSize = 4096;
	uint32 mInputBufferLevel = 0;
	uint32 mInputBufferOffset = 0;
	uint32 mSampleAccum = 0;
	uint64 mSampleStep = 0;
	bool mbEndOfOutputStream = false;
	bool mbEndOfSourceStream = false;

	// We need an additional 4 samples at the beginning and end of the source
	// to cover the width of the filter. Adding another 4 samples aligns things
	// nicely on a 16-byte boundary.
	alignas(16) sint16 mInputBuffer[kInputBufferSize + 16][2] {};
};

////////////////////////////////////////////////////////////////////////////////

class ATCassetteAudioFSKSpeedCompensator final : public IATCassetteAudioSource {
public:
	ATCassetteAudioFSKSpeedCompensator(IATCassetteAudioSource& source);

	uint32 ReadAudio(sint16 (*dst)[2], uint32 n) override;

private:
	struct RotationTable;

	void AnalyzeSamples(uint32 pos, uint32 n);

	IATCassetteAudioSource& mSource;
	uint32 mWindowOffset = 0;
	uint32 mInputLevel = 0;
	uint64 mOutputAccum = 0;
	bool mbSourceEnded = false;
	bool mbOutputEnded = false;
	float mRateAccum = 1.0f;
	uint32 mRateRunningSum1 = 0;
	uint32 mRateRunningSum2 = 0;

	sint32 mFFTBins[2][10+2] {};

	static constexpr uint32 kInputWindowSize = 4096;
	static constexpr uint32 kFFTSize = 72;
	static constexpr uint32 kFullWindowSize = kInputWindowSize + kFFTSize;

	sint16 mInputWindow[kInputWindowSize + kFFTSize][2] {};
	sint32 mRateWindow[kInputWindowSize + kFFTSize] {};

	static const RotationTable kRotationTable;
};

////////////////////////////////////////////////////////////////////////////////

class ATCassetteAudioCrosstalkCanceller final : public IATCassetteAudioSource {
public:
	ATCassetteAudioCrosstalkCanceller(IATCassetteAudioSource& source);

	uint32 ReadAudio(sint16 (*dst)[2], uint32 n) override;

private:
	void ProcessBlock();

	IATCassetteAudioSource& mSource;
	uint32 mWindowOffset = 0;
	uint32 mOutputOffset = 7*256;
	uint32 mOutputLeft = 0;
	uint32 mPrerollBlocks = 7;
	uint32 mTailBlocksLeft = 4;
	uint32 mSamplesPending = 0;

	ATFFT<2048> mFFT;

	alignas(16) sint16 mInputWindow[2048][2] {};
	alignas(16) float mFFTWindow[2048] {};
	alignas(16) float mWorkLeft[2048] {};
	alignas(16) float mWorkRight[2048] {};
	alignas(16) float mOutputWindow[2048] {};
};

////////////////////////////////////////////////////////////////////////////////

class ATCassetteAudioThreadedQueue final : public IATCassetteAudioSource, private VDThread {
public:
	ATCassetteAudioThreadedQueue(IATCassetteAudioSource& source);
	~ATCassetteAudioThreadedQueue();

	uint32 ReadAudio(sint16 (*dst)[2], uint32 n) override;

private:
	void QueueBlock();

	void ThreadRun() override;

	static constexpr int kBlockCount = 4;

	IATCassetteAudioSource& mSource;
	int mCurrentBlock = 0;
	uint32 mCurrentBlockOffset = 0;
	uint32 mCurrentBlockLength = 0;
	bool mbStreamEnded = false;

	VDSemaphore mBlocksQueued { 0 };
	VDSemaphore mBlocksFree { kBlockCount };
	VDAtomicInt mbExit { false };

	uint32 mBlockLengths[kBlockCount] {};
	VDException mException;

	// 1MB
	alignas(64) sint16 mBlocks[kBlockCount][65536][2] {};
};

#endif
