//	Altirra - Atari 800/800XL/5200 emulator
//	SDL3 audio output — full mixer implementation
//
//	This implements IATAudioOutput + IATAudioMixer for SDL3, matching the
//	Windows ATAudioOutput mixing pipeline:
//	  POKEY → sync sources → edge player → DC removal → LPF → async sources → SDL3 stream
//
//	The key difference from Windows is that we skip the internal polyphase
//	resampler: filtered ~64kHz float samples go directly to SDL_AudioStream,
//	which handles resampling to the hardware device rate.

#include <stdafx.h>
#ifdef ALTIRRA_AUDIO_NULL
// Null audio backend — same mixer pipeline, but no SDL dependency.
// mpStream is always nullptr, so every real SDL_* call site (all guarded
// by `if (mpStream)`) is unreachable dead code. These stubs exist only
// so the file compiles without <SDL3/SDL.h>.
struct SDL_AudioStream;
struct SDL_AudioSpec { int format; int channels; int freq; };
#define SDL_AUDIO_F32 0
#define SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK 0
static inline SDL_AudioStream* SDL_OpenAudioDeviceStream(int, const SDL_AudioSpec*, void*, void*) { return nullptr; }
static inline void SDL_DestroyAudioStream(SDL_AudioStream*) {}
static inline void SDL_ResumeAudioStreamDevice(SDL_AudioStream*) {}
static inline void SDL_PauseAudioStreamDevice(SDL_AudioStream*) {}
static inline void SDL_SetAudioStreamFormat(SDL_AudioStream*, const SDL_AudioSpec*, const SDL_AudioSpec*) {}
static inline int SDL_GetAudioStreamQueued(SDL_AudioStream*) { return 0; }
static inline bool SDL_PutAudioStreamData(SDL_AudioStream*, const void*, int) { return true; }
static inline bool SDL_SetAudioStreamFrequencyRatio(SDL_AudioStream*, float) { return true; }
static inline const char* SDL_GetError() { return ""; }
static inline uint64 SDL_GetTicks() { return 0; }
#else
#include <SDL3/SDL.h>
#endif
#include <string.h>
#include <algorithm>
#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/refcount.h>
#include <vd2/system/time.h>
#include <at/atcore/audiosource.h>
#include <at/atcore/audiomixer.h>
#include <at/ataudio/audiofilters.h>
#include <at/ataudio/audiosampleplayer.h>
#include <at/ataudio/audiooutput.h>
#include <at/ataudio/audiosamplepool.h>

// =========================================================================
// ATSyncAudioEdgePlayer — copied from audiooutput.cpp (platform-independent)
//
// Converts high-rate edge transitions into audio samples via triangle
// filtering. Inserted between the differencing and integration stages
// of the DC removal filter.
// =========================================================================

class ATSyncAudioEdgePlayer final : public IATSyncAudioEdgePlayer {
public:
	static constexpr int kTailLength = 3;

	bool IsStereoMixingRequired() const;

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
	memset(dstLeft + n, 0, sizeof(*dstLeft) * kTailLength);

	for(int i=0; i<kTailLength; ++i)
		dstLeft[i] += mLeftTail[i];

	if (dstRight) {
		memset(dstRight + n, 0, sizeof(*dstRight) * kTailLength);

		for(int i=0; i<kTailLength; ++i)
			dstRight[i] += mRightTail[i];
	}

	RenderEdgeBuffer(dstLeft, dstRight, n, timestamp, mEdges.data(), mEdges.size(), 1.0f);
	mEdges.clear();

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
					RenderEdgeBuffer(dstLeft, nullptr, n, timestamp, buf->mEdges.data(), buf->mEdges.size(), buf->mRightVolume);
				}
			}
		}

		buf->mEdges.clear();
		buf->Release();
	}

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

// =========================================================================
// ATAudioOutputSDL3 — full mixer + SDL3 audio stream output
// =========================================================================

class ATAudioOutputSDL3 final : public IATAudioOutput, public IATAudioMixer {
	ATAudioOutputSDL3(const ATAudioOutputSDL3&) = delete;
	ATAudioOutputSDL3& operator=(const ATAudioOutputSDL3&) = delete;

public:
	ATAudioOutputSDL3();
	~ATAudioOutputSDL3();

	// IATAudioOutput
	void Init(ATScheduler& scheduler) override;
	void InitNativeAudio() override;

	ATAudioApi GetApi() override { return kATAudioApi_Auto; }
	void SetApi(ATAudioApi) override {}

	void SetAudioTap(IATAudioTap* tap) override { mpAudioTap = tap; }
	ATUIAudioStatus GetAudioStatus() const override { return mAudioStatus; }

	IATAudioMixer& AsMixer() override { return *this; }
	ATAudioSamplePool& GetPool() override { return *mpSamplePool; }

	void SetCyclesPerSecond(double cps, double repeatfactor) override;

	bool GetMute() override { return mbMute; }
	void SetMute(bool mute) override { mbMute = mute; }

	float GetVolume() override { return mFilters[0].GetScale(); }
	void SetVolume(float vol) override {
		mFilters[0].SetScale(vol);
		mFilters[1].SetScale(vol);
	}

	float GetMixLevel(ATAudioMix mix) const override;
	void SetMixLevel(ATAudioMix mix, float level) override;

	int GetLatency() override { return mLatency; }
	void SetLatency(int ms) override {
		if (ms < 10) ms = 10;
		else if (ms > 500) ms = 500;
		mLatency = ms;
	}
	int GetExtraBuffer() override { return mExtraBuffer; }
	void SetExtraBuffer(int ms) override {
		if (ms < 10) ms = 10;
		else if (ms > 500) ms = 500;
		mExtraBuffer = ms;
	}

	void SetFiltersEnabled(bool enable) override {
		mFilters[0].SetActiveMode(enable);
		mFilters[1].SetActiveMode(enable);
	}

	void Pause() override;
	void Resume() override;

	void WriteAudio(const float* left, const float* right,
	                uint32 count, bool pushAudio, bool pushStereoAsMono,
	                uint64 timestamp) override;

	// IATAudioMixer
	void AddSyncAudioSource(IATSyncAudioSource* src) override;
	void RemoveSyncAudioSource(IATSyncAudioSource* src) override;
	void AddAsyncAudioSource(IATAudioAsyncSource& src) override;
	void RemoveAsyncAudioSource(IATAudioAsyncSource& src) override;

	IATSyncAudioSamplePlayer& GetSamplePlayer() override { return *mpSamplePlayer; }
	IATSyncAudioSamplePlayer& GetEdgeSamplePlayer() override { return *mpEdgeSamplePlayer; }
	IATSyncAudioEdgePlayer& GetEdgePlayer() override { return *mpEdgePlayer; }
	IATSyncAudioSamplePlayer& GetAsyncSamplePlayer() override { return *mpAsyncSamplePlayer; }

	void AddInternalAudioTap(IATInternalAudioTap* tap) override;
	void RemoveInternalAudioTap(IATInternalAudioTap* tap) override;
	void BlockInternalAudio() override { ++mBlockInternalAudioCount; }
	void UnblockInternalAudio() override { --mBlockInternalAudioCount; }

private:
	void InternalWriteAudio(const float* left, const float* right,
	                        uint32 count, bool pushAudio, bool pushStereoAsMono,
	                        uint64 timestamp);

	// Buffer sizing — matches Windows ATAudioOutput exactly
	enum {
		kBufferSize = 1536,
		kFilterOffset = 16,
		kPreFilterOffset = kFilterOffset + ATAudioFilter::kFilterOverlap * 2,
		kEdgeRenderOverlap = ATSyncAudioEdgePlayer::kTailLength,
		kSourceBufferSize = (kBufferSize + kPreFilterOffset + kEdgeRenderOverlap + 15) & ~15,
	};

	// SDL3 audio stream
	SDL_AudioStream* mpStream = nullptr;

	// Mixer components
	vdautoptr<ATAudioSamplePool> mpSamplePool;
	vdautoptr<ATAudioSamplePlayer> mpSamplePlayer;
	vdautoptr<ATAudioSamplePlayer> mpEdgeSamplePlayer;
	vdautoptr<ATAudioSamplePlayer> mpAsyncSamplePlayer;
	vdautoptr<ATSyncAudioEdgePlayer> mpEdgePlayer;

	// Audio taps
	vdautoptr<vdfastvector<IATInternalAudioTap *>> mpInternalAudioTaps;
	IATAudioTap* mpAudioTap = nullptr;

	// Audio filters (DC removal + low-pass)
	ATAudioFilter mFilters[2];
	float mPrevDCLevels[2] {};

	// Source tracking
	typedef vdfastvector<IATSyncAudioSource *> SyncAudioSources;
	SyncAudioSources mSyncAudioSources;
	SyncAudioSources mSyncAudioSourcesStereo;

	typedef vdfastvector<IATAudioAsyncSource *> AsyncAudioSources;
	AsyncAudioSources mAsyncAudioSources;

	// Mix levels
	float mMixLevels[kATAudioMixCount];

	// Source buffers — aligned for SIMD
	alignas(16) float mSourceBuffer[2][kSourceBufferSize] {};
	alignas(16) float mMonoMixBuffer[kBufferSize] {};

	// State
	uint32 mBufferLevel = 0;
	double mTickRate = 1;
	float mMixingRate = 0;
	// Defaults match the Windows engine (audiooutput.cpp:329-330).  These
	// are normally overridden by ATLoadSettings shortly after construction
	// (settings.cpp:1185-1186 -> SetLatency/SetExtraBuffer with defaults
	// 80/100), so they only matter for the brief window before settings
	// load and for fresh installs without an Audio: Latency key.  The
	// previous values (40/0) gave only ~40ms of headroom which is too tight
	// for Linux PulseAudio/PipeWire scheduling jitter.
	int mLatency = 100;
	int mExtraBuffer = 100;
	bool mbMute = false;
	bool mbFilterStereo = false;
	uint32 mFilterMonoSamples = 0;
	uint32 mBlockInternalAudioCount = 0;
	uint32 mPauseCount = 0;

	// Profiling / status
	ATUIAudioStatus mAudioStatus {};
	uint32 mWritePosition = 0;
	uint32 mProfileCounter = 0;
	uint32 mProfileBlockStartPos = 0;
	uint64 mProfileBlockStartTime = 0;
	uint32 mCheckCounter = 0;
	uint32 mUnderflowCount = 0;
	uint32 mOverflowCount = 0;
	int mMixingRateInt = 63920;

	// --- Adaptive clock recovery state (Phase B port of Windows
	// RecomputeResamplingRate / mResampleAccum / mDropCounter logic) ---
	//
	// We do not have access to the OS hardware audio clock through SDL3 —
	// only to the SDL audio stream queue depth.  We close the loop by
	// observing average queue depth over a 15-call window and bending the
	// SDL stream's input/output frequency ratio so the average tracks the
	// midpoint of [latency, latency+extraBuffer].  Drift correction is
	// gentle (gain 0.05, hard-clamped to ±0.5%) to prevent oscillation.
	uint64 mQueueSampleSum = 0;       // accumulated SDL_GetAudioStreamQueued() per call
	uint32 mQueueSampleCount = 0;     // number of samples in the current window
	int    mQueueSampleMin = 0x7FFFFFFF;
	int    mQueueSampleMax = 0;
	float  mFreqRatio = 1.0f;         // last applied frequency ratio
	uint32 mDropCounter = 0;          // sustained-overflow counter (Windows hysteresis)

	// --- Speed-modifier compensation (port of Windows mRepeatInc /
	// mRepeatAccum from audiooutput.cpp:1007-1017) ---
	//
	// SetCyclesPerSecond receives a `repeatfactor = 1.0 / speedRate`
	// which encodes how many times each WriteAudio chunk should be
	// pushed to the audio device to keep audio playback at correct
	// wallclock pace.  At normal speed (rate=1.0) repeatfactor=1.0 →
	// inc=65536 → exactly one push per call.  At 2× speed
	// (rate=2.0) repeatfactor=0.5 → inc=32768 → on average 0.5 pushes
	// per call (every other call drops the chunk).  At slowmo
	// (rate=0.5) repeatfactor=2.0 → inc=131072 → 2 pushes per call.
	//
	// Without this, slowmo and Configure System speed modifiers cause
	// continuous queue underflow / overflow because POKEY's call rate
	// no longer matches realtime sample consumption.
	uint32 mRepeatInc = 65536;
	uint32 mRepeatAccum = 0;

	// Class-member interleave buffer for the output stage, so that we
	// can push the same chunk multiple times to SDL3 without redoing
	// the interleave (see write loop below).  Sized for the worst case
	// kBufferSize samples × 2 channels.
	alignas(16) float mInterleaveBuffer[kBufferSize * 2] {};

	// --- Telemetry for the once-per-second "[Audio]" diagnostic line.
	// Cumulative counters; we log the delta since the previous tick.
	uint64 mTelemetryNextMs = 0;
	uint32 mTelemetryLastUnderflow = 0;
	uint32 mTelemetryLastOverflow = 0;
	uint32 mTelemetryLastDrop = 0;
};

// -------------------------------------------------------------------------
// Construction / destruction
// -------------------------------------------------------------------------

ATAudioOutputSDL3::ATAudioOutputSDL3() {
	mpEdgePlayer = new ATSyncAudioEdgePlayer;
	mpSamplePool = new ATAudioSamplePool;

	mMixLevels[kATAudioMix_Drive] = 0.8f;
	mMixLevels[kATAudioMix_Other] = 1.0f;
	mMixLevels[kATAudioMix_Covox] = 1.0f;
	mMixLevels[kATAudioMix_Cassette] = 0.5f;
	mMixLevels[kATAudioMix_Modem] = 0.7f;
}

ATAudioOutputSDL3::~ATAudioOutputSDL3() {
	if (mpStream) {
		SDL_DestroyAudioStream(mpStream);
		mpStream = nullptr;
	}
}

// -------------------------------------------------------------------------
// Init — create sample players (matches Windows ATAudioOutput::Init)
// -------------------------------------------------------------------------

void ATAudioOutputSDL3::Init(ATScheduler& scheduler) {
	mpSamplePlayer = new ATAudioSamplePlayer(*mpSamplePool, scheduler);
	mpEdgeSamplePlayer = new ATAudioSamplePlayer(*mpSamplePool, scheduler);

	AddSyncAudioSource(&mpSamplePlayer->AsSource());
	// edge sample player is special cased — not added as sync source

	mpAsyncSamplePlayer = new ATAudioSamplePlayer(*mpSamplePool, scheduler);
	AddAsyncAudioSource(*mpAsyncSamplePlayer);

	mbFilterStereo = false;
	mFilterMonoSamples = 0;

	mBufferLevel = 0;

	mCheckCounter = 0;
	mUnderflowCount = 0;
	mOverflowCount = 0;

	// Reset adaptive clock recovery state.
	mQueueSampleSum = 0;
	mQueueSampleCount = 0;
	mQueueSampleMin = 0x7FFFFFFF;
	mQueueSampleMax = 0;
	mFreqRatio = 1.0f;
	mDropCounter = 0;

	// Reset speed-modifier accumulator.  mRepeatInc is set by the
	// SetCyclesPerSecond call below.
	mRepeatAccum = 0;

	mWritePosition = 0;

	mProfileBlockStartPos = 0;
	mProfileBlockStartTime = VDGetPreciseTick();
	mProfileCounter = 0;

	SetCyclesPerSecond(1789772.5, 1.0);
}

// -------------------------------------------------------------------------
// InitNativeAudio — create SDL3 audio stream
// -------------------------------------------------------------------------

void ATAudioOutputSDL3::InitNativeAudio() {
#ifdef ALTIRRA_AUDIO_NULL
	// Null backend: no device, no output. Samples generated by the mixer
	// are silently dropped in WriteAudio (mpStream stays nullptr).
	return;
#else
	SDL_AudioSpec spec {};
	spec.freq = mMixingRateInt;
	spec.format = SDL_AUDIO_F32;
	spec.channels = 2;

	mpStream = SDL_OpenAudioDeviceStream(
		SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);

	if (!mpStream) {
		fprintf(stderr, "[ATAudioOutputSDL3] Failed to open audio: %s\n",
		        SDL_GetError());
		return;
	}

	SDL_ResumeAudioStreamDevice(mpStream);

	fprintf(stderr, "[ATAudioOutputSDL3] Audio initialized at %d Hz stereo (full mixer)\n",
	        mMixingRateInt);
#endif
}

// -------------------------------------------------------------------------
// SetCyclesPerSecond — update mixing rate and sample player rates
// -------------------------------------------------------------------------

void ATAudioOutputSDL3::SetCyclesPerSecond(double cps, double repeatfactor) {
	mTickRate = cps;
	mMixingRate = (float)(cps / 28.0);
	mAudioStatus.mExpectedRate = cps / 28.0;

	// Apply speed-modifier compensation (matches Windows
	// audiooutput.cpp:523).  See class member comment for details.
	mRepeatInc = (uint32)(repeatfactor * 65536.0 + 0.5);

	int newRate = (int)(cps / 28.0 + 0.5);
	bool rateChanged = (newRate != mMixingRateInt);
	mMixingRateInt = newRate;

	if (rateChanged && mpStream) {
		SDL_AudioSpec srcSpec {};
		srcSpec.freq = mMixingRateInt;
		srcSpec.format = SDL_AUDIO_F32;
		srcSpec.channels = 2;
		SDL_SetAudioStreamFormat(mpStream, &srcSpec, nullptr);
	}

	// Update sample player rates — matches Windows RecomputeResamplingRate()
	float pokeyMixingRate = mMixingRate;

	if (mpSamplePlayer)
		mpSamplePlayer->SetRates(pokeyMixingRate, 1.0f, 1.0f / (float)kATCyclesPerSyncSample);

	if (mpEdgeSamplePlayer)
		mpEdgeSamplePlayer->SetRates(pokeyMixingRate, 1.0f, 1.0f / (float)kATCyclesPerSyncSample);

	// For SDL3, async player always mixes at POKEY rate (we don't resample internally).
	// This matches the Windows audio-tap-present path.
	if (mpAsyncSamplePlayer)
		mpAsyncSamplePlayer->SetRates(pokeyMixingRate, 1.0f, 1.0f / (float)kATCyclesPerSyncSample);
}

// -------------------------------------------------------------------------
// Mix levels, pause/resume
// -------------------------------------------------------------------------

float ATAudioOutputSDL3::GetMixLevel(ATAudioMix mix) const {
	if ((unsigned)mix < kATAudioMixCount)
		return mMixLevels[mix];
	return 1.0f;
}

void ATAudioOutputSDL3::SetMixLevel(ATAudioMix mix, float level) {
	if ((unsigned)mix < kATAudioMixCount)
		mMixLevels[mix] = level;
}

void ATAudioOutputSDL3::Pause() {
	if (!mPauseCount++) {
		if (mpStream)
			SDL_PauseAudioStreamDevice(mpStream);
	}
}

void ATAudioOutputSDL3::Resume() {
	if (!--mPauseCount) {
		if (mpStream)
			SDL_ResumeAudioStreamDevice(mpStream);
	}
}

// -------------------------------------------------------------------------
// Sync/async source management (IATAudioMixer)
// -------------------------------------------------------------------------

void ATAudioOutputSDL3::AddSyncAudioSource(IATSyncAudioSource* src) {
	mSyncAudioSources.push_back(src);
}

void ATAudioOutputSDL3::RemoveSyncAudioSource(IATSyncAudioSource* src) {
	auto it = std::find(mSyncAudioSources.begin(), mSyncAudioSources.end(), src);
	if (it != mSyncAudioSources.end())
		mSyncAudioSources.erase(it);
}

void ATAudioOutputSDL3::AddAsyncAudioSource(IATAudioAsyncSource& src) {
	mAsyncAudioSources.push_back(&src);
}

void ATAudioOutputSDL3::RemoveAsyncAudioSource(IATAudioAsyncSource& src) {
	auto it = std::find(mAsyncAudioSources.begin(), mAsyncAudioSources.end(), &src);
	if (it != mAsyncAudioSources.end())
		mAsyncAudioSources.erase(it);
}

// -------------------------------------------------------------------------
// Internal audio taps
// -------------------------------------------------------------------------

void ATAudioOutputSDL3::AddInternalAudioTap(IATInternalAudioTap* tap) {
	if (!mpInternalAudioTaps)
		mpInternalAudioTaps = new vdfastvector<IATInternalAudioTap *>;

	mpInternalAudioTaps->push_back(tap);
}

void ATAudioOutputSDL3::RemoveInternalAudioTap(IATInternalAudioTap* tap) {
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

// -------------------------------------------------------------------------
// WriteAudio — outer loop, splits into kBufferSize chunks
// -------------------------------------------------------------------------

void ATAudioOutputSDL3::WriteAudio(
	const float* left,
	const float* right,
	uint32 count,
	bool pushAudio,
	bool pushStereoAsMono,
	uint64 timestamp)
{
	if (!count)
		return;

	mWritePosition += count;

	for(;;) {
		uint32 tc = kBufferSize - mBufferLevel;
		if (tc > count)
			tc = count;

		InternalWriteAudio(left, right, tc, pushAudio, pushStereoAsMono, timestamp);

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

// -------------------------------------------------------------------------
// InternalWriteAudio — full mixing pipeline
//
// This closely follows ATAudioOutput::InternalWriteAudio() from the
// Windows implementation, diverging only at the output stage where we
// push float32 to SDL3 instead of resampling to sint16 for IVDAudioOutput.
// -------------------------------------------------------------------------

void ATAudioOutputSDL3::InternalWriteAudio(
	const float* left,
	const float* right,
	uint32 count,
	bool pushAudio,
	bool pushStereoAsMono,
	uint64 timestamp)
{
	VDASSERT(count > 0);
	VDASSERT(mBufferLevel + count <= kBufferSize);

	// ---- Step 1: Determine stereo requirements ----
	bool needMono = false;
	bool needStereo = right != nullptr || mpEdgePlayer->IsStereoMixingRequired();

	for(IATSyncAudioSource *src : mSyncAudioSources) {
		if (src->RequiresStereoMixingNow()) {
			needStereo = true;
		} else {
			needMono = true;
		}
	}

	// For SDL3 we always mix async sources at mixing rate (like the audio-tap path)
	for(IATAudioAsyncSource *src : mAsyncAudioSources) {
		if (src->RequiresStereoMixingNow()) {
			needStereo = true;
		} else {
			needMono = true;
		}
	}

	// Switch to stereo filtering if needed
	if (needStereo && !mbFilterStereo) {
		mFilters[1].CopyState(mFilters[0]);

		// Copy the current buffer data PLUS the kPreFilterOffset overlap zone that
		// contains filter state from the previous frame. In the Windows version,
		// mBufferLevel is typically non-zero (the resampler doesn't consume everything),
		// so the overlap zone is implicitly covered. In our SDL3 version, mBufferLevel
		// is always 0 at the start of InternalWriteAudio because we flush everything,
		// so we must explicitly copy the overlap zone.
		memcpy(mSourceBuffer[1], mSourceBuffer[0], sizeof(float) * (mBufferLevel + kPreFilterOffset));
		mbFilterStereo = true;
	}

	if (count) {
		// ---- Step 2: Notify internal audio taps ----
		if (mpInternalAudioTaps) {
			for(IATInternalAudioTap *tap : *mpInternalAudioTaps)
				tap->WriteInternalAudio(left, count, timestamp);
		}

		// ---- Step 3: Copy POKEY data into source buffer ----
		float *const dstLeft = &mSourceBuffer[0][mBufferLevel];
		float *const dstRight = mbFilterStereo ? &mSourceBuffer[1][mBufferLevel] : nullptr;

		if (mBlockInternalAudioCount) {
			memset(dstLeft + kPreFilterOffset, 0, sizeof(float) * count);

			if (mbFilterStereo)
				memset(dstRight + kPreFilterOffset, 0, sizeof(float) * count);
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

		// ---- Step 4: Mix sync audio sources ----
		float dcLevels[2] = { 0, 0 };

		ATSyncAudioMixInfo mixInfo {};
		mixInfo.mStartTime = timestamp;
		mixInfo.mCount = count;
		mixInfo.mNumCycles = count * kATCyclesPerSyncSample;
		mixInfo.mMixingRate = mMixingRate;
		mixInfo.mpDCLeft = &dcLevels[0];
		mixInfo.mpDCRight = &dcLevels[1];
		mixInfo.mpMixLevels = mMixLevels;

		if (mbFilterStereo) {
			// Mixed mono/stereo mixing
			mSyncAudioSourcesStereo.clear();

			// Mix mono sources first
			if (needMono) {
				memset(mMonoMixBuffer, 0, sizeof(float) * count);

				mixInfo.mpLeft = mMonoMixBuffer;
				mixInfo.mpRight = nullptr;

				for(IATSyncAudioSource *src : mSyncAudioSources) {
					if (!src->RequiresStereoMixingNow())
						src->WriteAudio(mixInfo);
					else
						mSyncAudioSourcesStereo.push_back(src);
				}

				// Fold mono buffer into both stereo channels
				for(uint32 i=0; i<count; ++i) {
					float v = mMonoMixBuffer[i];
					dstLeft[kPreFilterOffset + i] += v;
					dstRight[kPreFilterOffset + i] += v;
				}

				dcLevels[1] = dcLevels[0];
			}

			// Mix stereo sources
			mixInfo.mpLeft = dstLeft + kPreFilterOffset;
			mixInfo.mpRight = dstRight + kPreFilterOffset;

			for(IATSyncAudioSource *src : needMono ? mSyncAudioSourcesStereo : mSyncAudioSources) {
				src->WriteAudio(mixInfo);
			}
		} else {
			// Mono mixing
			mixInfo.mpLeft = dstLeft + kPreFilterOffset;
			mixInfo.mpRight = nullptr;

			for(IATSyncAudioSource *src : mSyncAudioSources)
				src->WriteAudio(mixInfo);
		}

		// ---- Step 5: Pre-filter differencing (DC removal stage 1) ----
		const int nch = mbFilterStereo ? 2 : 1;
		const ptrdiff_t prefilterPos = mBufferLevel + kPreFilterOffset;
		for(int ch=0; ch<nch; ++ch) {
			mFilters[ch].PreFilterDiff(&mSourceBuffer[ch][prefilterPos], count);
		}

		// ---- Step 6: Render edges ----
		mixInfo.mpLeft = &mSourceBuffer[0][prefilterPos];
		mixInfo.mpRight = nullptr;

		if (nch > 1)
			mixInfo.mpRight = &mSourceBuffer[1][prefilterPos];

		mpEdgePlayer->RenderEdges(mixInfo.mpLeft, mixInfo.mpRight, count, (uint32)timestamp);

		if (mpEdgeSamplePlayer)
			mpEdgeSamplePlayer->AsSource().WriteAudio(mixInfo);

		// ---- Step 7: Pre-filter integration + DC removal + low-pass ----
		for(int ch=0; ch<nch; ++ch) {
			mFilters[ch].PreFilterEdges(&mSourceBuffer[ch][prefilterPos], count, dcLevels[ch] - mPrevDCLevels[ch]);
			mFilters[ch].Filter(&mSourceBuffer[ch][mBufferLevel + kFilterOffset], count);
		}

		mPrevDCLevels[0] = dcLevels[0];
		mPrevDCLevels[1] = dcLevels[1];
	}

	// ---- Step 8: Check if we can switch from stereo back to mono ----
	if (mbFilterStereo && !needStereo && mFilters[0].CloseTo(mFilters[1], 1e-10f)) {
		mFilterMonoSamples += count;

		if (mFilterMonoSamples >= kBufferSize)
			mbFilterStereo = false;
	} else {
		mFilterMonoSamples = 0;
	}

	// ---- Step 9: Mix async sources at mixing rate and send to audio tap ----
	{
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

		if (mpAudioTap) {
			if (mbFilterStereo)
				mpAudioTap->WriteRawAudio(mSourceBuffer[0] + mBufferLevel + kFilterOffset, mSourceBuffer[1] + mBufferLevel + kFilterOffset, count, timestamp);
			else
				mpAudioTap->WriteRawAudio(mSourceBuffer[0] + mBufferLevel + kFilterOffset, nullptr, count, timestamp);
		}
	}

	mBufferLevel += count;
	VDASSERT(mBufferLevel <= kBufferSize);

	// ---- Step 10: Output filtered samples to SDL3 ----
	if (mpStream && mBufferLevel > 0) {
		// Sample SDL stream queue depth.  This is our only source of
		// feedback about audio consumption — there is no SDL3 equivalent
		// of Windows' EstimateHWBufferLevel().  Note: SDL_GetAudioStreamQueued
		// returns -1 on failure; clamp to 0 so we don't spuriously trip
		// the underflow path or corrupt the unsigned accumulator.
		int queued = SDL_GetAudioStreamQueued(mpStream);
		if (queued < 0) queued = 0;
		const int bytesPerSecond = mMixingRateInt * 2 * (int)sizeof(float);
		const int targetMidBytes = bytesPerSecond * (mLatency + mLatency + mExtraBuffer) / 2 / 1000;
		const int latencyBytes   = bytesPerSecond * mLatency / 1000;
		const int maxQueueBytes  = bytesPerSecond * (mLatency + mExtraBuffer + 50) / 1000;
		const int underflowThresholdBytes = latencyBytes / 4;

		// Update min/max for this status window (used for the live read-out).
		if (queued < mQueueSampleMin) mQueueSampleMin = queued;
		if (queued > mQueueSampleMax) mQueueSampleMax = queued;
		mQueueSampleSum += (uint64)queued;
		++mQueueSampleCount;

		// --- Drop hysteresis (Phase B2, mirrors Windows audiooutput.cpp:944-967) ---
		// Drop a block only if (a) we have not recently underflowed and
		// (b) the over-target condition has persisted for 10 consecutive
		// samples.  This prevents transient queue spikes (caused by audio
		// thread scheduling jitter on Linux) from being treated as real
		// overflow and producing audible glitches.  In turbo mode the
		// emulator floods WriteAudio() much faster than realtime, so the
		// 10-sample threshold is reached almost immediately and the queue
		// is still bounded — no special turbo path needed.
		bool dropBlock = false;

		if (mUnderflowCount == 0 && queued > maxQueueBytes) {
			if (++mDropCounter >= 10) {
				mDropCounter = 0;
				dropBlock = true;
			}
		} else {
			mDropCounter = 0;
		}

		// --- Earlier underflow detection (Phase B4) ---
		// Windows only flagged underflow when the queue was fully empty.
		// We flag it when queue falls below 25% of the latency target so
		// the recovery loop has something to react to before audible silence.
		if (queued < underflowThresholdBytes && count > 0) {
			++mUnderflowCount;
			++mAudioStatus.mUnderflowCount;
		}

		if (dropBlock) {
			++mOverflowCount;
			++mAudioStatus.mOverflowCount;
			++mAudioStatus.mDropCount;
		} else {
			// Output the filtered buffer level worth of samples.
			// The filtered data lives at offset kFilterOffset from the
			// start of the source buffer.
			const uint32 outputCount = mBufferLevel;
			const float *srcLeft = mSourceBuffer[0] + kFilterOffset;
			const float *srcRight = mbFilterStereo ? mSourceBuffer[1] + kFilterOffset : nullptr;

			// --- Speed-modifier write count (Phase B port of Windows
			// mRepeatAccum / mRepeatInc, audiooutput.cpp:1007-1017).
			// At normal speed mRepeatInc = 65536 → writeCount = 1.
			// In slowmo mRepeatInc > 65536 → writeCount can be 2+.
			// At fast-forward (>1×) mRepeatInc < 65536 → writeCount
			// alternates 0/1 averaging the desired write rate.
			// Capped at 10 to bound buffer fill in pathological cases. ---
			mRepeatAccum += mRepeatInc;
			uint32 writeCount = mRepeatAccum >> 16;
			mRepeatAccum &= 0xFFFF;
			if (writeCount > 10) writeCount = 10;

			if (writeCount > 0) {
				// Interleave once into the class-member buffer, then
				// push it to SDL3 writeCount times.
				if (mbMute) {
					memset(mInterleaveBuffer, 0, outputCount * 2 * sizeof(float));
				} else if (srcRight) {
					for (uint32 i = 0; i < outputCount; ++i) {
						mInterleaveBuffer[i * 2    ] = srcLeft[i];
						mInterleaveBuffer[i * 2 + 1] = srcRight[i];
					}
				} else {
					for (uint32 i = 0; i < outputCount; ++i) {
						mInterleaveBuffer[i * 2    ] = srcLeft[i];
						mInterleaveBuffer[i * 2 + 1] = srcLeft[i];
					}
				}

				const int byteCount = (int)(outputCount * 2 * sizeof(float));
				for (uint32 rep = 0; rep < writeCount; ++rep) {
					SDL_PutAudioStreamData(mpStream, mInterleaveBuffer, byteCount);
				}
			}
			// writeCount == 0 → fast-forward dropped this chunk
			// intentionally; this is the symmetric counterpart of the
			// slowmo repeat path and is correct behaviour.

			// Underflow already detected above using a more sensitive
			// threshold (latency / 4), so no tail check needed here.
		}
	}

	// Always shift the source buffer down and reset mBufferLevel, even if mpStream
	// is null (audio device failed to open) or we dropped the block. Without this,
	// mBufferLevel would grow without bound, causing buffer overflow and assert
	// failures on the next WriteAudio call.
	if (mBufferLevel > 0) {
		uint32 bytesToShift = sizeof(float) * kPreFilterOffset;

		memmove(mSourceBuffer[0], mSourceBuffer[0] + mBufferLevel, bytesToShift);

		if (mbFilterStereo)
			memmove(mSourceBuffer[1], mSourceBuffer[1] + mBufferLevel, bytesToShift);

		mBufferLevel = 0;
	}

	// ---- Step 11: Status, clock recovery and profiling ----
	if (mpStream) {
		if (++mCheckCounter >= 15) {
			mCheckCounter = 0;

			// --- Phase B1: adaptive clock recovery via SDL stream
			// frequency ratio.  This is the SDL3 analogue of Windows
			// RecomputeResamplingRate / mResampleAccum (audiooutput.cpp
			// 838-922, 1037-1056).  We bend the SDL audio stream's
			// input/output ratio so the average queue depth tracks the
			// midpoint of [latency, latency+extraBuffer], in bytes.
			//
			// Without this loop, any difference between the nominal
			// 63920 Hz POKEY mixing rate and the real OS audio device
			// clock accumulates as drift, eventually causing either
			// underflow (silence) or overflow (drops).  Linux PulseAudio
			// and PipeWire both internally resample everything and the
			// "real" device clock is rarely exactly nominal, so the
			// drift is real even on healthy hardware.
			if (mQueueSampleCount > 0) {
				const double avgQueue = (double)mQueueSampleSum / (double)mQueueSampleCount;
				const int    bytesPerSecond = mMixingRateInt * 2 * (int)sizeof(float);
				const int    targetMidBytes = bytesPerSecond * (mLatency + mLatency + mExtraBuffer) / 2 / 1000;

				if (targetMidBytes > 0) {
					// Positive error = queue too full = need to drain it
					// faster.  Per SDL3 SDL_SetAudioStreamFrequencyRatio
					// docs, ratio > 1.0 plays the audio FASTER, so input
					// data is consumed more quickly — therefore positive
					// error must INCREASE the ratio.
					const double error = (avgQueue - (double)targetMidBytes) / (double)targetMidBytes;
					constexpr double kGain = 0.05;
					constexpr float  kRatioClamp = 0.005f; // ±0.5%

					float newRatio = mFreqRatio + (float)(error * kGain);
					if (newRatio < 1.0f - kRatioClamp) newRatio = 1.0f - kRatioClamp;
					if (newRatio > 1.0f + kRatioClamp) newRatio = 1.0f + kRatioClamp;

					if (newRatio != mFreqRatio) {
						SDL_SetAudioStreamFrequencyRatio(mpStream, newRatio);
						mFreqRatio = newRatio;
					}
				}

				mAudioStatus.mMeasuredMin = mQueueSampleMin;
				mAudioStatus.mMeasuredMax = mQueueSampleMax;
			} else {
				mAudioStatus.mMeasuredMin = 0;
				mAudioStatus.mMeasuredMax = 0;
			}

			// Report target range in BYTES to match the Windows
			// convention (audiooutput.cpp:971-972).  Note that on
			// SDL3 our sample format is float stereo (8 bytes/sample)
			// so any consumer converting bytes→ms must use 8, not 4.
			const int statusBytesPerMs = mMixingRateInt * 2 * (int)sizeof(float) / 1000;
			mAudioStatus.mTargetMin = mLatency * statusBytesPerMs;
			mAudioStatus.mTargetMax = (mLatency + mExtraBuffer) * statusBytesPerMs;
			mAudioStatus.mbStereoMixing = mbFilterStereo;
			mAudioStatus.mSamplingRate = mMixingRateInt;

			// Reset windowed accumulators.
			mQueueSampleSum = 0;
			mQueueSampleCount = 0;
			mQueueSampleMin = 0x7FFFFFFF;
			mQueueSampleMax = 0;

			// mAudioStatus.mUnderflowCount and mOverflowCount are cumulative
			// (incremented directly at detection time, never reset here).
			// The local mUnderflowCount/mOverflowCount are windowed counters
			// used only for the drop logic check.
			mUnderflowCount = 0;
			mOverflowCount = 0;

			// --- Once-per-second diagnostic line ---------------------------
			// Reports:
			//   q=<min>/<max>/<target>ms    SDL queue depth window (min/max
			//                                over ~375 ms) vs target latency,
			//                                in milliseconds.
			//   ratio=<f>                    Applied SDL frequency ratio
			//                                (1.0 = nominal; ±0.5% = clock
			//                                recovery bending the rate).
			//   uf=<d> of=<d> drop=<d>       Underflow/overflow/drop events
			//                                in the last second.
			//   in=<f>k exp=<f>k             Measured vs expected POKEY
			//                                sample rate in samples/sec.
			//
			// This tells us whether the crackling is an audio-engine
			// problem (underflows, non-unity ratio, rate mismatch) or a
			// frame-pacing problem (flat audio stats → look at [Pace]).
			const uint64 nowMs = SDL_GetTicks();
			if (nowMs >= mTelemetryNextMs) {
				mTelemetryNextMs = nowMs + 1000;

				const uint32 ufDelta = mAudioStatus.mUnderflowCount - mTelemetryLastUnderflow;
				const uint32 ofDelta = mAudioStatus.mOverflowCount  - mTelemetryLastOverflow;
				const uint32 drDelta = mAudioStatus.mDropCount      - mTelemetryLastDrop;
				mTelemetryLastUnderflow = mAudioStatus.mUnderflowCount;
				mTelemetryLastOverflow  = mAudioStatus.mOverflowCount;
				mTelemetryLastDrop      = mAudioStatus.mDropCount;

				const int bps = mMixingRateInt * 2 * (int)sizeof(float);
				const int minMs = bps > 0 ? (mAudioStatus.mMeasuredMin * 1000 / bps) : 0;
				const int maxMs = bps > 0 ? (mAudioStatus.mMeasuredMax * 1000 / bps) : 0;

				(void)minMs; (void)maxMs; (void)ufDelta; (void)ofDelta; (void)drDelta;
			}
		}
	}

	if (++mProfileCounter >= 200) {
		mProfileCounter = 0;
		uint64 t = VDGetPreciseTick();

		mAudioStatus.mIncomingRate = (double)(mWritePosition - mProfileBlockStartPos) / (double)(t - mProfileBlockStartTime) * VDGetPreciseTicksPerSecond();

		mProfileBlockStartPos = mWritePosition;
		mProfileBlockStartTime = t;
	}
}

// =========================================================================
// Factory function
// =========================================================================

IATAudioOutput *ATCreateAudioOutput() {
	return new ATAudioOutputSDL3();
}
