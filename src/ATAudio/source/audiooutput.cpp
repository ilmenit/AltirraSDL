//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2010 Avery Lee
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
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

#include <stdafx.h>
#include <windows.h>
#include <mmreg.h>
#include <vd2/system/math.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/time.h>
#include <at/atcore/audiosource.h>
#include <at/atcore/configvar.h>
#include <at/ataudio/audiofilters.h>
#include <at/ataudio/audiosampleplayer.h>
#include <at/ataudio/audioout.h>
#include <at/ataudio/audiooutput.h>
#include <at/ataudio/audiosamplepool.h>

ATConfigVarBool g_ATCVAudioResampleInterpFilter("audio.resample.interp_filter", true);

class ATSyncAudioEdgePlayer final : public IATSyncAudioEdgePlayer {
public:
	// We need one sample extra for the triangle filter, one to accommodate frame start cycle-to-sample
	// jitter, and another to accommodate end jitter.
	static constexpr int kTailLength = 3;

	bool IsStereoMixingRequired() const;

	// Render edges to the given buffer. Right is optional; if both are given, then the edges are added
	// to both left and right channels.
	//
	// Important: This requires kTailLength entries after both buffers as temporary space.
	//
	void RenderEdges(float *dstLeft, float *dstRight, uint32 n, uint32 timestamp);

	void AddEdges(const ATSyncAudioEdge *edges, size_t numEdges, float volume) override;
	void AddEdgeBuffer(ATSyncAudioEdgeBuffer *buffer) override;

protected:
	void RenderEdgeBuffer(float *dstLeft, float *dstRight, uint32 n, uint32 timestamp, const ATSyncAudioEdge *edges, size_t numEdges, float volume);

	template<bool T_RightEnabled>
	void RenderEdgeBuffer2(float *dstLeft, float *dstRight, uint32 n, uint32 timestamp, const ATSyncAudioEdge *edges, size_t numEdges, float volume);

	vdfastvector<ATSyncAudioEdge> mEdges;
	vdfastvector<ATSyncAudioEdgeBuffer *> mBuffers;
	bool mbTailHasStereo = false;

	float mLeftTail[kTailLength] {};
	float mRightTail[kTailLength] {};
};

bool ATSyncAudioEdgePlayer::IsStereoMixingRequired() const {
	if (mbTailHasStereo)
		return true;

	if (mBuffers.empty())
		return false;

	for(ATSyncAudioEdgeBuffer *buf : mBuffers) {
		if (!buf->mEdges.empty() && buf->mLeftVolume != buf->mRightVolume && buf->mLeftVolume != 0)
			return true;
	}

	return false;
}

void ATSyncAudioEdgePlayer::RenderEdges(float *dstLeft, float *dstRight, uint32 n, uint32 timestamp) {
	// zero the tail at the end of the current buffer
	memset(dstLeft + n, 0, sizeof(*dstLeft) * kTailLength);

	// add the previous tail at the start (which may overlap the current tail!)
	for(int i=0; i<kTailLength; ++i)
		dstLeft[i] += mLeftTail[i];
	
	if (dstRight) {
		memset(dstRight + n, 0, sizeof(*dstRight) * kTailLength);

		for(int i=0; i<kTailLength; ++i)
			dstRight[i] += mRightTail[i];
	}

	// render loose edges
	RenderEdgeBuffer(dstLeft, dstRight, n, timestamp, mEdges.data(), mEdges.size(), 1.0f);
	mEdges.clear();

	// render buffered edges
	while(!mBuffers.empty()) {
		ATSyncAudioEdgeBuffer *buf = mBuffers.back();
		mBuffers.pop_back();

		if (buf->mLeftVolume == buf->mRightVolume) {
			if (buf->mLeftVolume != 0)
				RenderEdgeBuffer(dstLeft, dstRight, n, timestamp, buf->mEdges.data(), buf->mEdges.size(), buf->mLeftVolume);
		} else {
			if (buf->mLeftVolume != 0)
				RenderEdgeBuffer(dstLeft, nullptr, n, timestamp, buf->mEdges.data(), buf->mEdges.size(), buf->mLeftVolume);

			if (buf->mRightVolume != 0) {
				if (dstRight) {
					RenderEdgeBuffer(dstRight, nullptr, n, timestamp, buf->mEdges.data(), buf->mEdges.size(), buf->mRightVolume);
				} else {
					VDFAIL("Stereo edge buffer submitted without stereo being active.");
					RenderEdgeBuffer(dstLeft, nullptr, n, timestamp, buf->mEdges.data(), buf->mEdges.size(), buf->mRightVolume);
				}
			}
		}

		buf->mEdges.clear();
		buf->Release();
	}

	// save off the new tails
	for(int i=0; i<kTailLength; ++i)
		mLeftTail[i] = dstLeft[n + i];

	if (dstRight) {
		for(int i=0; i<kTailLength; ++i)
			mRightTail[i] = dstRight[n + i];
	} else {
		for(int i=0; i<kTailLength; ++i)
			mRightTail[i] = dstLeft[n + i];
	}

	mbTailHasStereo = memcmp(mLeftTail, mRightTail, sizeof mLeftTail) != 0;
}

void ATSyncAudioEdgePlayer::AddEdges(const ATSyncAudioEdge *edges, size_t numEdges, float volume) {
	if (!numEdges)
		return;

	mEdges.resize(mEdges.size() + numEdges);

	const ATSyncAudioEdge *VDRESTRICT src = edges;
	ATSyncAudioEdge *VDRESTRICT dst = &*(mEdges.end() - numEdges);

	while(numEdges--) {
		dst->mTime = src->mTime;
		dst->mDeltaValue = src->mDeltaValue * volume;
		++dst;
		++src;
	}
}

void ATSyncAudioEdgePlayer::AddEdgeBuffer(ATSyncAudioEdgeBuffer *buffer) {
	if (buffer) {
		mBuffers.push_back(buffer);
		buffer->AddRef();
	}
}

void ATSyncAudioEdgePlayer::RenderEdgeBuffer(float *dstLeft, float *dstRight, uint32 n, uint32 timestamp, const ATSyncAudioEdge *edges, size_t numEdges, float volume) {
	if (dstRight)
		RenderEdgeBuffer2<true>(dstLeft, dstRight, n, timestamp, edges, numEdges, volume);
	else
		RenderEdgeBuffer2<false>(dstLeft, dstRight, n, timestamp, edges, numEdges, volume);
}

template<bool T_RightEnabled>
void ATSyncAudioEdgePlayer::RenderEdgeBuffer2(float *dstLeft, float *dstRight, uint32 n, uint32 timestamp, const ATSyncAudioEdge *edges, size_t numEdges, float volume) {
	const ATSyncAudioEdge *VDRESTRICT src = edges;
	float *VDRESTRICT dstL2 = dstLeft;
	float *VDRESTRICT dstR2 = dstRight;

	// We allow two additional samples to accommodate timing jitter during the mixing -- we allow samples
	// to be written during the frame's cycle window, but the sample window may be up to one sample earlier
	// and one sample short. These extra samples get premixed into the tail, which is then carried forward
	// to the next frame.
	const uint32 timeWindow = (n+2) * 28;

	while(numEdges--) {
		const uint32 cycleOffset = src->mTime - timestamp;
		if (cycleOffset < timeWindow) {
			const uint32 sampleOffset = cycleOffset / 28;
			const uint32 phaseOffset = cycleOffset % 28;
			const float shift = (float)phaseOffset * (1.0f / 28.0f);
			const float delta = src->mDeltaValue * volume;
			const float v1 = delta * shift;
			const float v0 = delta - v1;

			dstL2[sampleOffset+0] += v0;
			dstL2[sampleOffset+1] += v1;

			if constexpr (T_RightEnabled) {
				dstR2[sampleOffset+0] += v0;
				dstR2[sampleOffset+1] += v1;
			}
		} else {
			if (cycleOffset & 0x80000000)
				VDFAIL("Edge player has sample before allowed frame window.");
			else
				VDFAIL("Edge player has sample after allowed frame window.");
		}

		++src;
	}
}

///////////////////////////////////////////////////////////////////////////

class ATAudioOutput final : public IATAudioOutput, public IATAudioMixer, public VDAlignedObject<16> {
	ATAudioOutput(ATAudioOutput&) = delete;
	ATAudioOutput& operator=(const ATAudioOutput&) = delete;

public:
	ATAudioOutput();
	virtual ~ATAudioOutput() override;

	void Init(ATScheduler& scheduler) override;
	void InitNativeAudio() override;

	ATAudioApi GetApi() override;
	void SetApi(ATAudioApi api) override;

	void AddInternalAudioTap(IATInternalAudioTap *tap) override;
	void RemoveInternalAudioTap(IATInternalAudioTap *tap) override;

	void BlockInternalAudio() override;
	void UnblockInternalAudio() override;

	void SetAudioTap(IATAudioTap *tap) override;

	ATUIAudioStatus GetAudioStatus() const override {
		return mAudioStatus;
	}

	IATAudioMixer& AsMixer() override { return *this; }
	ATAudioSamplePool& GetPool() override { return *mpSamplePool; }

	void AddSyncAudioSource(IATSyncAudioSource *src) override;
	void RemoveSyncAudioSource(IATSyncAudioSource *src) override;

	void AddAsyncAudioSource(IATAudioAsyncSource& src) override;
	void RemoveAsyncAudioSource(IATAudioAsyncSource& src) override;

	void SetCyclesPerSecond(double cps, double repeatfactor) override;

	bool GetMute() override;
	void SetMute(bool mute) override;

	float GetVolume() override;
	void SetVolume(float vol) override;

	float GetMixLevel(ATAudioMix mix) const override;
	void SetMixLevel(ATAudioMix mix, float level) override;

	int GetLatency() override;
	void SetLatency(int ms) override;

	int GetExtraBuffer() override;
	void SetExtraBuffer(int ms) override;

	void SetFiltersEnabled(bool enable) override {
		mFilters[0].SetActiveMode(enable);
		mFilters[1].SetActiveMode(enable);
	}

	void Pause() override;
	void Resume() override;
	void SetReplayMode(bool replay) override { mbReplay = replay; }

	void WriteAudio(
		const float *left,
		const float *right,
		uint32 count, bool pushAudio, bool pushStereoAsMono, uint64 timestamp) override;

public:
	IATSyncAudioSamplePlayer& GetSamplePlayer() override { return *mpSamplePlayer; }
	IATSyncAudioSamplePlayer& GetEdgeSamplePlayer() override { return *mpEdgeSamplePlayer; }
	IATSyncAudioEdgePlayer& GetEdgePlayer() override { return *mpEdgePlayer; }
	IATSyncAudioSamplePlayer& GetAsyncSamplePlayer() override { return *mpAsyncSamplePlayer; }

protected:
	void InternalWriteAudio(const float *left, const float *right, uint32 count, bool pushAudio, bool pushStereoAsMono, uint64 timestamp);
	void RecomputeBuffering();
	void RecomputeResamplingRate();
	void ReinitAudio();
	bool ReinitAudio(ATAudioApi api);

	enum {
		// 1271 samples is the max (35568 cycles/frame / 28 cycles/sample + 1). We add a little bit here
		// to round it out. We need a 16 sample holdover in order to run the FIR filter.
		//
		// This should match the write size used by POKEY. However, if it doesn't, we just split the
		// write, so it'll still work.
		kBufferSize = 1536,

		// The filter needs to keep far enough ahead that there are enough samples to cover the
		// resampler plus the resampler step. The resampler itself needs 7 samples; we add another 9
		// samples to support about an 8:1 ratio.
		kFilterOffset = 16,

		// The prefilter needs to run ahead by the FIR kernel width (nominally 16 + 1 samples).
		kPreFilterOffset = kFilterOffset + ATAudioFilter::kFilterOverlap * 2,

		// The edge renderer wants a little extra room at the end to temporarily hold overlapped data.
		kEdgeRenderOverlap = ATSyncAudioEdgePlayer::kTailLength,

		kSourceBufferSize = (kBufferSize + kPreFilterOffset + kEdgeRenderOverlap + 15) & ~15,
	};

	uint32	mBufferLevel = 0;
	uint32	mFilteredSampleCount = 0;
	uint64	mResampleAccum = 0;
	sint64	mResampleRate = 0;
	double	mTickRate = 1;
	float	mMixingRate = 0;
	uint32	mSamplingRate = 48000;
	ATAudioApi	mSelectedApi = kATAudioApi_WaveOut;
	ATAudioApi	mActiveApi = kATAudioApi_WaveOut;
	uint32	mPauseCount = 0;
	uint32	mLatencyTargetMin = 0;
	uint32	mLatencyTargetMax = 0;
	int		mLatency = 100;
	int		mExtraBuffer = 100;
	bool	mbMute = false;
	bool mbReplay = false;
	bool	mbNativeAudioEnabled = false;
	uint32	mBlockInternalAudioCount = 0;

	bool	mbAsyncMixBufferZeroed = false;
	bool	mbFilterStereo = false;
	uint32	mFilterMonoSamples = 0;

	uint32	mRepeatAccum = 0;
	uint32	mRepeatInc = 0;

	uint32	mCheckCounter = 0;
	uint32	mMinLevel = 0xFFFFFFFFU;
	uint32	mMaxLevel = 0;
	uint32	mUnderflowCount = 0;
	uint32	mOverflowCount = 0;
	uint32	mDropCounter = 0;

	uint32	mWritePosition = 0;

	uint32	mProfileCounter = 0;
	uint32	mProfileBlockStartPos = 0;
	uint64	mProfileBlockStartTime = 0;

	vdautoptr<IVDAudioOutput>	mpAudioOut;
	vdautoptr<vdfastvector<IATInternalAudioTap *>> mpInternalAudioTaps = nullptr;
	IATAudioTap *mpAudioTap = nullptr;
	vdautoptr<ATAudioSamplePool> mpSamplePool;		// must be before players
	vdautoptr<ATAudioSamplePlayer> mpSamplePlayer;
	vdautoptr<ATAudioSamplePlayer> mpEdgeSamplePlayer;
	vdautoptr<ATAudioSamplePlayer> mpAsyncSamplePlayer;
	vdautoptr<ATSyncAudioEdgePlayer> mpEdgePlayer;
	float mPrevDCLevels[2] {};

	ATUIAudioStatus	mAudioStatus = {};

	ATAudioFilter	mFilters[2];

	typedef vdfastvector<IATSyncAudioSource *> SyncAudioSources;
	SyncAudioSources mSyncAudioSources;
	SyncAudioSources mSyncAudioSourcesStereo;

	typedef vdfastvector<IATAudioAsyncSource *> AsyncAudioSources;
	AsyncAudioSources mAsyncAudioSources;
	
	float mMixLevels[kATAudioMixCount] {};

	alignas(16) float	mSourceBuffer[2][kSourceBufferSize] {};
	alignas(16) float	mMonoMixBuffer[kBufferSize] {};

	vdblock<float> mAsyncMixBuffer;
	vdblock<sint16> mOutputBuffer16;
};

ATAudioOutput::ATAudioOutput() {
	mpEdgePlayer = new ATSyncAudioEdgePlayer;
	mpSamplePool = new ATAudioSamplePool;

	mMixLevels[kATAudioMix_Drive] = 0.8f;

	// These paths have been updated to use normalized mix levels with any necessary scaling factors included.
	mMixLevels[kATAudioMix_Other] = 1.0f;
	mMixLevels[kATAudioMix_Covox] = 1.0f;
	mMixLevels[kATAudioMix_Cassette] =  0.5f;
	mMixLevels[kATAudioMix_Modem] = 0.7f;
}

ATAudioOutput::~ATAudioOutput() {
}

void ATAudioOutput::Init(ATScheduler& scheduler) {
	mpSamplePlayer = new ATAudioSamplePlayer(*mpSamplePool, scheduler);
	mpEdgeSamplePlayer = new ATAudioSamplePlayer(*mpSamplePool, scheduler);

	AddSyncAudioSource(&mpSamplePlayer->AsSource());
	// edge sample player is special cased

	mpAsyncSamplePlayer = new ATAudioSamplePlayer(*mpSamplePool, scheduler);
	AddAsyncAudioSource(*mpAsyncSamplePlayer);

	mbFilterStereo = false;
	mFilterMonoSamples = 0;

	mBufferLevel = 0;
	mResampleAccum = 0;

	mCheckCounter = 0;
	mMinLevel = 0xFFFFFFFFU;
	mMaxLevel = 0;
	mUnderflowCount = 0;
	mOverflowCount = 0;
	mDropCounter = 0;

	mWritePosition = 0;

	mProfileBlockStartPos = 0;
	mProfileBlockStartTime = VDGetPreciseTick();
	mProfileCounter = 0;

	mLatencyTargetMin = (mSamplingRate * 10 / 1000) * 4;
	mLatencyTargetMax = (mSamplingRate * 100 / 1000) * 4;

	RecomputeBuffering();
	ReinitAudio();

	SetCyclesPerSecond(1789772.5, 1.0);
}

void ATAudioOutput::InitNativeAudio() {
	if (!mbNativeAudioEnabled) {
		mbNativeAudioEnabled = true;

		ReinitAudio();
	}
}

ATAudioApi ATAudioOutput::GetApi() {
	return mSelectedApi;
}

void ATAudioOutput::SetApi(ATAudioApi api) {
	if (mSelectedApi == api)
		return;

	mSelectedApi = api;
	ReinitAudio();
}

void ATAudioOutput::AddInternalAudioTap(IATInternalAudioTap *tap) {
	if (!mpInternalAudioTaps)
		mpInternalAudioTaps = new vdfastvector<IATInternalAudioTap *>;

	mpInternalAudioTaps->push_back(tap);
}

void ATAudioOutput::RemoveInternalAudioTap(IATInternalAudioTap *tap) {
	if (mpInternalAudioTaps) {
		auto it = std::find(mpInternalAudioTaps->begin(), mpInternalAudioTaps->end(), tap);
		
		if (it != mpInternalAudioTaps->end()) {
			*it = mpInternalAudioTaps->back();
			mpInternalAudioTaps->pop_back();

			if (mpInternalAudioTaps->empty())
				mpInternalAudioTaps = nullptr;
		}
	}
}

void ATAudioOutput::BlockInternalAudio() {
	++mBlockInternalAudioCount;
}

void ATAudioOutput::UnblockInternalAudio() {
	--mBlockInternalAudioCount;
}

void ATAudioOutput::SetAudioTap(IATAudioTap *tap) {
	mpAudioTap = tap;

	RecomputeResamplingRate();
}

void ATAudioOutput::AddSyncAudioSource(IATSyncAudioSource *src) {
	mSyncAudioSources.push_back(src);
}

void ATAudioOutput::RemoveSyncAudioSource(IATSyncAudioSource *src) {
	SyncAudioSources::iterator it(std::find(mSyncAudioSources.begin(), mSyncAudioSources.end(), src));

	if (it != mSyncAudioSources.end())
		mSyncAudioSources.erase(it);
}

void ATAudioOutput::AddAsyncAudioSource(IATAudioAsyncSource& src) {
	mAsyncAudioSources.push_back(&src);
}

void ATAudioOutput::RemoveAsyncAudioSource(IATAudioAsyncSource& src) {
	auto it = std::find(mAsyncAudioSources.begin(), mAsyncAudioSources.end(), &src);

	if (it != mAsyncAudioSources.end())
		mAsyncAudioSources.erase(it);
	
}

void ATAudioOutput::SetCyclesPerSecond(double cps, double repeatfactor) {
	mTickRate = cps;
	mMixingRate = cps / 28.0;
	mAudioStatus.mExpectedRate = cps / 28.0;
	RecomputeResamplingRate();

	mRepeatInc = VDRoundToInt(repeatfactor * 65536.0);
}

bool ATAudioOutput::GetMute() {
	return mbMute;
}

void ATAudioOutput::SetMute(bool mute) {
	mbMute = mute;
}

float ATAudioOutput::GetVolume() {
	return mFilters[0].GetScale();
}

void ATAudioOutput::SetVolume(float vol) {
	mFilters[0].SetScale(vol);
	mFilters[1].SetScale(vol);
}

float ATAudioOutput::GetMixLevel(ATAudioMix mix) const {
	return mMixLevels[mix];
}

void ATAudioOutput::SetMixLevel(ATAudioMix mix, float level) {
	mMixLevels[mix] = level;
}

int ATAudioOutput::GetLatency() {
	return mLatency;
}

void ATAudioOutput::SetLatency(int ms) {
	if (ms < 10)
		ms = 10;
	else if (ms > 500)
		ms = 500;

	if (mLatency == ms)
		return;

	mLatency = ms;

	RecomputeBuffering();
}

int ATAudioOutput::GetExtraBuffer() {
	return mExtraBuffer;
}

void ATAudioOutput::SetExtraBuffer(int ms) {
	if (ms < 10)
		ms = 10;
	else if (ms > 500)
		ms = 500;

	if (mExtraBuffer == ms)
		return;

	mExtraBuffer = ms;

	RecomputeBuffering();
}

void ATAudioOutput::Pause() {
	if (!mPauseCount++) {
		if (mpAudioOut)
			mpAudioOut->Stop();
	}
}

void ATAudioOutput::Resume() {
	if (!--mPauseCount) {
		if (mpAudioOut)
			mpAudioOut->Start();
	}
}

void ATAudioOutput::WriteAudio(
	const float *left,
	const float *right,
	uint32 count,
	bool pushAudio,
	bool pushStereoAsMono,
	uint64 timestamp)
{
	if (mbReplay) return;

	if (!count)
		return;

	mWritePosition += count;

	for(;;) {
		uint32 tc = kBufferSize - mBufferLevel;
		if (tc > count)
			tc = count;

		InternalWriteAudio(left, right, tc, pushAudio, pushStereoAsMono, timestamp);

		// exit if we can't write anything -- we only do this after a call to
		// InternalWriteAudio() as we need to try to push existing buffered
		// audio to clear buffer space
		if (!tc)
			break;

		count -= tc;
		if (!count)
			break;

		timestamp += 28 * tc;
		left += tc;
		if (right)
			right += tc;
	}
}

void ATAudioOutput::InternalWriteAudio(
	const float *left,
	const float *right,
	uint32 count,
	bool pushAudio,
	bool pushStereoAsMono,
	uint64 timestamp)
{
	VDASSERT(count > 0);
	VDASSERT(mBufferLevel + count <= kBufferSize);

	// check if any sync sources need stereo mixing
	bool needMono = false;
	bool needStereo = right != nullptr || mpEdgePlayer->IsStereoMixingRequired();

	for(IATSyncAudioSource *src : mSyncAudioSources) {
		if (src->RequiresStereoMixingNow()) {
			needStereo = true;
		} else {
			needMono = true;
		}
	}

	if (mpAudioTap) {
		for(IATAudioAsyncSource *src : mAsyncAudioSources) {
			if (src->RequiresStereoMixingNow()) {
				needStereo = true;
			} else {
				needMono = true;
			}
		}
	}

	// if we need stereo and aren't currently doing stereo filtering, copy channel state over now
	if (needStereo && !mbFilterStereo) {
		mFilters[1].CopyState(mFilters[0]);
		memcpy(mSourceBuffer[1], mSourceBuffer[0], sizeof(float) * mBufferLevel);
		mbFilterStereo = true;
	}

	if (count) {
		// notify internal audio taps if there are any
		if (mpInternalAudioTaps) {
			for(IATInternalAudioTap *taps : *mpInternalAudioTaps)
				taps->WriteInternalAudio(left, count, timestamp);
		}

		// copy in samples if internal audio not blocked
		float *const dstLeft = &mSourceBuffer[0][mBufferLevel];
		float *const dstRight = mbFilterStereo ? &mSourceBuffer[1][mBufferLevel] : nullptr;

		if (mBlockInternalAudioCount) {
			memset(dstLeft + kPreFilterOffset, 0, sizeof(float) * count);

			if (mbFilterStereo)
				memcpy(dstRight + kPreFilterOffset, 0, sizeof(float) * count);
		} else if (mbFilterStereo && pushStereoAsMono && right) {
			float *VDRESTRICT mixDstLeft = dstLeft + kPreFilterOffset;
			float *VDRESTRICT mixDstRight = dstRight + kPreFilterOffset;
			const float *VDRESTRICT mixSrcLeft = left;
			const float *VDRESTRICT mixSrcRight = right;

			for(size_t i=0; i<count; ++i)
				mixDstLeft[i] = mixDstRight[i] = (mixSrcLeft[i] + mixSrcRight[i]) * 0.5f;
		} else {
			memcpy(dstLeft + kPreFilterOffset, left, sizeof(float) * count);

			if (mbFilterStereo) {
				if (right)
					memcpy(dstRight + kPreFilterOffset, right, sizeof(float) * count);
				else
					memcpy(dstRight + kPreFilterOffset, left, sizeof(float) * count);
			}
		}


		// run audio sources
		float dcLevels[2] = { 0, 0 };

		ATSyncAudioMixInfo mixInfo {};
		mixInfo.mStartTime = timestamp;
		mixInfo.mCount = count;
		mixInfo.mNumCycles = count * kATCyclesPerSyncSample;
		mixInfo.mMixingRate = mMixingRate;
		mixInfo.mpDCLeft = &dcLevels[0];
		mixInfo.mpDCRight = &dcLevels[1];
		mixInfo.mpMixLevels = mMixLevels;

		if (mbFilterStereo) {		// mixed mono/stereo mixing
			mSyncAudioSourcesStereo.clear();

			// mix mono first
			if (needMono) {
				// clear mono buffer
				memset(mMonoMixBuffer, 0, sizeof(float) * count);

				// mix mono sources into mono buffer
				mixInfo.mpLeft = mMonoMixBuffer;
				mixInfo.mpRight = nullptr;

				for(IATSyncAudioSource *src : mSyncAudioSources) {
					if (!src->RequiresStereoMixingNow())
						src->WriteAudio(mixInfo);
					else {
						// We need to create a temporary list for this as WriteAudio() may change the
						// mixing state for a source and we don't want to double-mix a source.
						mSyncAudioSourcesStereo.push_back(src);
					}
				}

				// mix mono buffer into stereo buffers
				for(uint32 i=0; i<count; ++i) {
					float v = mMonoMixBuffer[i];

					dstLeft[kPreFilterOffset + i] += v;
					dstRight[kPreFilterOffset + i] += v;
				}

				dcLevels[1] = dcLevels[0];
			}

			// mix stereo sources
			mixInfo.mpLeft = dstLeft + kPreFilterOffset;
			mixInfo.mpRight = dstRight + kPreFilterOffset;

			for(IATSyncAudioSource *src : needMono ? mSyncAudioSourcesStereo : mSyncAudioSources) {
				src->WriteAudio(mixInfo);
			}
		} else {					// mono mixing
			mixInfo.mpLeft = dstLeft + kPreFilterOffset;
			mixInfo.mpRight = nullptr;

			for(IATSyncAudioSource *src : mSyncAudioSources)
				src->WriteAudio(mixInfo);
		}

		// prediff channels
		const int nch = mbFilterStereo ? 2 : 1;
		const ptrdiff_t prefilterPos = mBufferLevel + kPreFilterOffset;
		for(int ch=0; ch<nch; ++ch) {
			mFilters[ch].PreFilterDiff(&mSourceBuffer[ch][prefilterPos], count);
		}

		// render edges
		mixInfo.mpLeft = &mSourceBuffer[0][prefilterPos];
		mixInfo.mpRight = nullptr;

		if (nch > 1)
			mixInfo.mpRight = &mSourceBuffer[1][prefilterPos];

		mpEdgePlayer->RenderEdges(mixInfo.mpLeft, mixInfo.mpRight, count, (uint32)timestamp);

		if (mpEdgeSamplePlayer)
			mpEdgeSamplePlayer->AsSource().WriteAudio(mixInfo);

		// filter channels
		for(int ch=0; ch<nch; ++ch) {
			mFilters[ch].PreFilterEdges(&mSourceBuffer[ch][prefilterPos], count, dcLevels[ch] - mPrevDCLevels[ch]);
			mFilters[ch].Filter(&mSourceBuffer[ch][mBufferLevel + kFilterOffset], count);
		}

		mPrevDCLevels[0] = dcLevels[0];
		mPrevDCLevels[1] = dcLevels[1];
	}

	// if we're filtering stereo and getting mono, check if it's safe to switch over
	if (mbFilterStereo && !needStereo && mFilters[0].CloseTo(mFilters[1], 1e-10f)) {
		mFilterMonoSamples += count;

		if (mFilterMonoSamples >= kBufferSize)
			mbFilterStereo = false;
	} else {
		mFilterMonoSamples = 0;
	}

	// Send filtered samples to the audio tap.
	if (mpAudioTap) {
		ATAudioAsyncMixInfo asyncMixInfo {};
		asyncMixInfo.mStartTime = timestamp;
		asyncMixInfo.mCount = count;
		asyncMixInfo.mNumCycles = count * kATCyclesPerSyncSample;
		asyncMixInfo.mMixingRate = mMixingRate;
		asyncMixInfo.mpLeft = mSourceBuffer[0] + mBufferLevel + kFilterOffset;
		asyncMixInfo.mpRight = mbFilterStereo ? mSourceBuffer[1] + mBufferLevel + kFilterOffset : nullptr;
		asyncMixInfo.mpMixLevels = mMixLevels;

		for(IATAudioAsyncSource *asyncSource : mAsyncAudioSources) {
			asyncSource->WriteAsyncAudio(asyncMixInfo);
		}

		if (mbFilterStereo)
			mpAudioTap->WriteRawAudio(mSourceBuffer[0] + mBufferLevel + kFilterOffset, mSourceBuffer[1] + mBufferLevel + kFilterOffset, count, timestamp);
		else
			mpAudioTap->WriteRawAudio(mSourceBuffer[0] + mBufferLevel + kFilterOffset, NULL, count, timestamp);
	}

	mBufferLevel += count;
	VDASSERT(mBufferLevel <= kBufferSize);

	// check for a change in output mixing rate that requires us to change our sampling rate
	const uint32 outputMixingRate = mpAudioOut->GetMixingRate();
	if (mSamplingRate != outputMixingRate) {
		mSamplingRate = outputMixingRate;

		RecomputeResamplingRate();
		RecomputeBuffering();
	}

	// Determine how many samples we can produce via resampling.
	uint32 resampleAvail = mBufferLevel + kFilterOffset;
	uint32 resampleCount = 0;

	uint64 limit = ((uint64)(resampleAvail - 8) << 32) + 0xFFFFFFFFU;

	if (limit >= mResampleAccum) {
		resampleCount = (uint32)((limit - mResampleAccum) / mResampleRate + 1);

		if (resampleCount) {
			if (mOutputBuffer16.size() < resampleCount * 2)
				mOutputBuffer16.resize((resampleCount * 2 + 2047) & ~(size_t)2047);

			size_t asyncChannelLen = (resampleCount + 7) & ~7;
			if (mAsyncMixBuffer.size() < asyncChannelLen * 2) {
				mAsyncMixBuffer.resize((asyncChannelLen * 2 + 1023) & ~(size_t)1023);
				mbAsyncMixBufferZeroed = false;
			}

			if (!mbAsyncMixBufferZeroed) {
				mbAsyncMixBufferZeroed = true;
				std::fill(mAsyncMixBuffer.begin(), mAsyncMixBuffer.end(), 0.0f);
			}

			float *asyncLeft = mAsyncMixBuffer.data();
			float *asyncRight = mAsyncMixBuffer.data() + asyncChannelLen;

			if (!mpAudioTap) {
				ATAudioAsyncMixInfo asyncMixInfo {};
				asyncMixInfo.mStartTime = timestamp;
				asyncMixInfo.mCount = resampleCount;
				asyncMixInfo.mNumCycles = count * kATCyclesPerSyncSample;
				asyncMixInfo.mMixingRate = mMixingRate;
				asyncMixInfo.mpLeft = asyncLeft;
				asyncMixInfo.mpRight = asyncRight;
				asyncMixInfo.mpMixLevels = mMixLevels;

				for(IATAudioAsyncSource *asyncSource : mAsyncAudioSources) {
					if (asyncSource->WriteAsyncAudio(asyncMixInfo))
						mbAsyncMixBufferZeroed = false;
				}
			}

			if (mbMute) {
				mResampleAccum += mResampleRate * resampleCount;
				memset(mOutputBuffer16.data(), 0, sizeof(mOutputBuffer16[0]) * resampleCount * 2);
			} else if (mbFilterStereo) {
				if (mbAsyncMixBufferZeroed)
					mResampleAccum = ATFilterResampleStereo16(mOutputBuffer16.data(), mSourceBuffer[0], mSourceBuffer[1], resampleCount, mResampleAccum, mResampleRate, g_ATCVAudioResampleInterpFilter);
				else
					mResampleAccum = ATFilterResampleStereoAdd16(mOutputBuffer16.data(), mSourceBuffer[0], mSourceBuffer[1], asyncLeft, asyncRight, resampleCount, mResampleAccum, mResampleRate, g_ATCVAudioResampleInterpFilter);
			} else {
				if (mbAsyncMixBufferZeroed)
					mResampleAccum = ATFilterResampleMonoToStereo16(mOutputBuffer16.data(), mSourceBuffer[0], resampleCount, mResampleAccum, mResampleRate, g_ATCVAudioResampleInterpFilter);
				else
					mResampleAccum = ATFilterResampleMonoToStereoAdd16(mOutputBuffer16.data(), mSourceBuffer[0], asyncLeft, asyncRight, resampleCount, mResampleAccum, mResampleRate, g_ATCVAudioResampleInterpFilter);
			}

			// determine if we can now shift down the source buffer
			uint32 shift = (uint32)(mResampleAccum >> 32);

			if (shift > mBufferLevel)
				shift = mBufferLevel; 

			if (shift) {
				uint32 bytesToShift = sizeof(float) * (mBufferLevel - shift + kPreFilterOffset);

				memmove(mSourceBuffer[0], mSourceBuffer[0] + shift, bytesToShift);

				if (mbFilterStereo)
					memmove(mSourceBuffer[1], mSourceBuffer[1] + shift, bytesToShift);

				mBufferLevel -= shift;
				mResampleAccum -= (uint64)shift << 32;
			}
		}
	}
	
	// check that the resample source position isn't too far out of whack
	VDASSERT(mResampleAccum < (uint64)mOutputBuffer16.size() << (32+4));

	bool underflowDetected = false;
	uint32 bytes = mpAudioOut->EstimateHWBufferLevel(&underflowDetected);

	if (mMinLevel > bytes)
		mMinLevel = bytes;

	if (mMaxLevel < bytes)
		mMaxLevel = bytes;

	uint32 adjustedLatencyTargetMin = mLatencyTargetMin;
	uint32 adjustedLatencyTargetMax = mLatencyTargetMax;

	if (mActiveApi == kATAudioApi_XAudio2 || mActiveApi == kATAudioApi_WASAPI) {
		adjustedLatencyTargetMin += resampleCount * 4;
		adjustedLatencyTargetMax += resampleCount * 4;
	}

	bool dropBlock = false;
	if (++mCheckCounter >= 15) {
		mCheckCounter = 0;

		// None				See if we can remove data to lower latency
		// Underflow		Do nothing; we already add data for this
		// Overflow			Do nothing; we may be in turbo
		// Under+overflow	Increase spread

		bool tryDrop = false;
		if (!mUnderflowCount) {
			if (mMinLevel > adjustedLatencyTargetMin + resampleCount * 8) {
				tryDrop = true;
			}
		}

		if (tryDrop) {
			if (++mDropCounter >= 10) {
				mDropCounter = 0;
				dropBlock = true;
			}
		} else {
			mDropCounter = 0;
		}

		mAudioStatus.mMeasuredMin = mMinLevel;
		mAudioStatus.mMeasuredMax = mMaxLevel;
		mAudioStatus.mTargetMin = mLatencyTargetMin;
		mAudioStatus.mTargetMax = mLatencyTargetMax;
		mAudioStatus.mbStereoMixing = mbFilterStereo;
		mAudioStatus.mSamplingRate = mSamplingRate;

		mMinLevel = 0xFFFFFFFFU;
		mMaxLevel = 0;
		mUnderflowCount = 0;
		mOverflowCount = 0;
	}

	if (++mProfileCounter >= 200) {
		mProfileCounter = 0;
		uint64 t = VDGetPreciseTick();

		mAudioStatus.mIncomingRate = (double)(mWritePosition - mProfileBlockStartPos) / (double)(t - mProfileBlockStartTime) * VDGetPreciseTicksPerSecond();

		mProfileBlockStartPos = mWritePosition;
		mProfileBlockStartTime = t;
	}

	if (bytes < adjustedLatencyTargetMin || underflowDetected) {
		++mAudioStatus.mUnderflowCount;
		++mUnderflowCount;

		mpAudioOut->Write(mOutputBuffer16.data(), resampleCount * 4);

		mDropCounter = 0;
		dropBlock = false;
	}

	if (dropBlock) {
		++mAudioStatus.mDropCount;
	} else {
		if (bytes < adjustedLatencyTargetMin + adjustedLatencyTargetMax) {
			if (pushAudio || true) {
				mRepeatAccum += mRepeatInc;

				uint32 count = mRepeatAccum >> 16;
				mRepeatAccum &= 0xffff;

				if (count > 10)
					count = 10;

				while(count--)
					mpAudioOut->Write(mOutputBuffer16.data(), resampleCount * 4);
			}
		} else {
			++mOverflowCount;
			++mAudioStatus.mOverflowCount;
		}
	}

	mpAudioOut->Flush();
}

void ATAudioOutput::RecomputeBuffering() {
	if (mActiveApi == kATAudioApi_XAudio2 || mActiveApi == kATAudioApi_WASAPI) {
		mLatencyTargetMin = 0;
		mLatencyTargetMax = mSamplingRate / 15 * 4;
	} else {
		mLatencyTargetMin = ((mLatency * mSamplingRate + 500) / 1000) * 4;
		mLatencyTargetMax = mLatencyTargetMin + ((mExtraBuffer * mSamplingRate + 500) / 1000) * 4;
	}
}

void ATAudioOutput::RecomputeResamplingRate() {
	mResampleRate = (sint64)(0.5 + 4294967296.0 * mAudioStatus.mExpectedRate / (double)mSamplingRate);

	float pokeyMixingRate = mMixingRate;

	if (mpSamplePlayer)
		mpSamplePlayer->SetRates(pokeyMixingRate, 1.0f, 1.0f / (float)kATCyclesPerSyncSample);

	if (mpEdgeSamplePlayer)
		mpEdgeSamplePlayer->SetRates(pokeyMixingRate, 1.0f, 1.0f  / (float)kATCyclesPerSyncSample);

	// If an audio tap is present, the async sample player needs to mix at 64KHz.
	// Otherwise, it mixes at output rate.
	if (mpAsyncSamplePlayer) {
		if (mpAudioTap)
			mpAsyncSamplePlayer->SetRates(pokeyMixingRate, 1.0f, 1.0f / (float)kATCyclesPerSyncSample);
		else
			mpAsyncSamplePlayer->SetRates((float)mSamplingRate, pokeyMixingRate / (float)mSamplingRate, (double)mSamplingRate / mTickRate);
	}
}

void ATAudioOutput::ReinitAudio() {
	if (mSelectedApi == kATAudioApi_Auto) {
		if (!ReinitAudio(kATAudioApi_WASAPI))
			ReinitAudio(kATAudioApi_WaveOut);
	} else {
		ReinitAudio(mSelectedApi);
	}
}

bool ATAudioOutput::ReinitAudio(ATAudioApi api) {
	if (!mbNativeAudioEnabled)
		return true;

	if (api == kATAudioApi_WASAPI)
		mpAudioOut = VDCreateAudioOutputWASAPIW32();
	else if (api == kATAudioApi_XAudio2)
		mpAudioOut = VDCreateAudioOutputXAudio2W32();
	else if (api == kATAudioApi_DirectSound)
		mpAudioOut = VDCreateAudioOutputDirectSoundW32();
	else
		mpAudioOut = VDCreateAudioOutputWaveOutW32();

	mActiveApi = api;

	const uint32 preferredSamplingRate = mpAudioOut->GetPreferredSamplingRate(nullptr);

	if (preferredSamplingRate == 0)
		mSamplingRate = 48000;
	else if (preferredSamplingRate < 44100)
		mSamplingRate = 44100;
	else if (preferredSamplingRate > 48000)
		mSamplingRate = 48000;
	else
		mSamplingRate = preferredSamplingRate;

	WAVEFORMATEX wfex {};
	wfex.wFormatTag			= WAVE_FORMAT_PCM;
	wfex.nChannels			= 2;
	wfex.nSamplesPerSec		= mSamplingRate;
	wfex.wBitsPerSample		= 16;
	wfex.nBlockAlign		= 4;
	wfex.nAvgBytesPerSec	= mSamplingRate * 4;
	wfex.cbSize				= 0;

	bool success = mpAudioOut->Init(kBufferSize * 4, 30, (const tWAVEFORMATEX *)&wfex, NULL);

	if (!success)
		mpAudioOut->GoSilent();

	if (!mpAudioOut->Start())
		success = false;

	RecomputeBuffering();
	RecomputeResamplingRate();

	if (mPauseCount)
		mpAudioOut->Stop();

	return success;
}

///////////////////////////////////////////////////////////////////////////

IATAudioOutput *ATCreateAudioOutput() {
	return new ATAudioOutput;
}
