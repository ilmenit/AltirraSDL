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
typedef unsigned int SDL_AudioDeviceID;
struct SDL_AudioSpec { int format; int channels; int freq; };
#define SDL_AUDIO_S16 0
#define SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK 0
static inline SDL_AudioStream* SDL_OpenAudioDeviceStream(int, const SDL_AudioSpec*, void*, void*) { return nullptr; }
static inline void SDL_DestroyAudioStream(SDL_AudioStream*) {}
static inline void SDL_ResumeAudioStreamDevice(SDL_AudioStream*) {}
static inline void SDL_PauseAudioStreamDevice(SDL_AudioStream*) {}
static inline void SDL_SetAudioStreamFormat(SDL_AudioStream*, const SDL_AudioSpec*, const SDL_AudioSpec*) {}
static inline int SDL_GetAudioStreamQueued(SDL_AudioStream*) { return 0; }
static inline bool SDL_PutAudioStreamData(SDL_AudioStream*, const void*, int) { return true; }
static inline SDL_AudioDeviceID SDL_GetAudioStreamDevice(SDL_AudioStream*) { return 0; }
static inline bool SDL_GetAudioDeviceFormat(SDL_AudioDeviceID, SDL_AudioSpec*, int*) { return false; }
static inline const char* SDL_GetError() { return ""; }
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

	// Windows exposes a WaveOut/DSound/XAudio2/WASAPI selector, which the
	// SDL3 Audio Options dialog (ui_recording.cpp:ATUIRenderAudioOptionsDialog)
	// deliberately omits: SDL3 picks a driver at SDL_Init time via
	// SDL_HINT_AUDIO_DRIVER, and users almost always want the OS default
	// (PulseAudio/PipeWire on Linux, CoreAudio on macOS). GetApi reports
	// Auto; SetApi is accepted but has no effect.
	ATAudioApi GetApi() override { return kATAudioApi_Auto; }
	void SetApi(ATAudioApi) override {}

	void SetAudioTap(IATAudioTap* tap) override {
		mpAudioTap = tap;
		// Tap presence switches the async-player mix-rate path
		// (POKEY rate vs output rate).  Windows calls
		// RecomputeResamplingRate() here for the same reason
		// (audiooutput.cpp:488-492).
		RecomputeResamplingRate();
	}
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
		if (mLatency == ms) return;
		mLatency = ms;
		RecomputeBuffering();
	}
	int GetExtraBuffer() override { return mExtraBuffer; }
	void SetExtraBuffer(int ms) override {
		if (ms < 10) ms = 10;
		else if (ms > 500) ms = 500;
		if (mExtraBuffer == ms) return;
		mExtraBuffer = ms;
		RecomputeBuffering();
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
	void RecomputeBuffering();
	void RecomputeResamplingRate();

	// Downstream-pipeline-depth tracking.  See the comment block at
	// mBytesWritten for rationale.  These implement the Windows-style
	// EstimateHWBufferLevel semantic — "bytes we've committed to the
	// output pipeline minus bytes the device must have played by now"
	// — on top of SDL3's narrower SDL_GetAudioStreamQueued.
	//
	// ComputePendingBytes is non-const: if the wallclock-based estimate
	// overshoots mBytesWritten it re-baselines so we don't spend the
	// next N calls chasing a phantom deficit.
	uint32 ComputePendingBytes();
	bool   StreamPut(const void *data, uint32 bytes);
	void   ResetLatencyClock();
	void   PauseLatencyClock();
	void   ResumeLatencyClock();
	void   RetargetLatencyClock(uint32 oldSamplingRate);

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

	// Mix levels.  Zero-initialized so that any index not explicitly set
	// by the constructor (e.g. kATAudioMix_Pokey) has a defined value
	// until settings.cpp loads user preferences.  Previously indeterminate.
	float mMixLevels[kATAudioMixCount] {};

	// Source buffers — aligned for SIMD
	alignas(16) float mSourceBuffer[2][kSourceBufferSize] {};
	alignas(16) float mMonoMixBuffer[kBufferSize] {};

	// State
	uint32 mBufferLevel = 0;
	double mTickRate = 1;
	float mMixingRate = 0;              // POKEY mixing rate = cps / 28
	uint32 mSamplingRate = 48000;       // Device output rate (set in InitNativeAudio)
	int mLatency = 80;                  // Matches Windows settings.cpp default
	int mExtraBuffer = 100;             // Matches Windows settings.cpp default
	bool mbMute = false;
	bool mbFilterStereo = false;
	uint32 mFilterMonoSamples = 0;
	uint32 mBlockInternalAudioCount = 0;
	uint32 mPauseCount = 0;

	// Polyphase resampler state (ported verbatim from audiooutput.cpp).
	// SDL3 performs zero resampling: we feed the device rate directly as
	// S16 stereo, so Altirra's interp-filter polyphase is the only sample
	// rate converter in the chain.
	uint64 mResampleAccum = 0;          // fixed-point source position (32.32)
	sint64 mResampleRate = 0;           // fixed-point source/dest ratio
	uint32 mLatencyTargetMin = 0;       // bytes
	uint32 mLatencyTargetMax = 0;       // bytes
	uint32 mMinLevel = 0xFFFFFFFFU;     // windowed queue min (bytes)
	uint32 mMaxLevel = 0;               // windowed queue max (bytes)

	// Downstream-pipeline-depth accounting.
	//
	// SDL3's SDL_GetAudioStreamQueued() reports only the bytes sitting
	// in the stream's *input* queue — bytes we have Put but that SDL
	// has not yet consumed via its internal Get-side drain.  Once the
	// device thread pulls a chunk from the stream, those bytes drop
	// out of the queued count even though they are still buffered
	// downstream: SDL's per-device output staging buffer, the backend
	// client buffer (PipeWire SHM, PulseAudio tlength, ALSA period
	// ring), the backend server buffer, and finally the device FIFO.
	// Those hidden stages can be anywhere from ~20 ms (native PipeWire
	// at a small quantum) to several seconds (PulseAudio defaults on
	// some distros).
	//
	// The Windows backends (WaveOut, DirectSound, WASAPI) instead
	// expose EstimateHWBufferLevel, which returns the TOTAL downstream
	// byte count — everything we've committed that hasn't been played
	// yet.  The rate-control logic in InternalWriteAudio was written
	// against that semantic: drive the aggregate latency into
	// [mLatencyTargetMin, mLatencyTargetMin+mLatencyTargetMax] by
	// doubling writes on underflow, skipping on overflow, and dropping
	// blocks when the running minimum stays above target for 10 check
	// windows.
	//
	// To restore the same semantic on top of SDL3 we reconstruct the
	// count ourselves: track cumulative bytes fed to the stream, and
	// subtract bytes the device must have consumed by now based on
	// wallclock elapsed since the stream was resumed.  SDL's audio
	// thread, when a stream is bound to a playback device, drains our
	// stream at exactly the stream's src-side rate (that's what
	// SDL_AudioStream is for — the rate-matching happens inside the
	// stream), so `elapsed * mSamplingRate * 4` is the correct estimate
	// of what has left our pipeline on a wallclock schedule.  At 1.0x
	// emulation speed we produce at the same rate; turbo/slowmo are
	// compensated by mRepeatInc so the net push rate is still
	// mSamplingRate.
	//
	// Pause/resume freezes and restarts the elapsed-time accumulator
	// so a paused device doesn't appear to be draining.  Sampling-rate
	// changes checkpoint the elapsed bytes at the old rate before
	// switching, via RetargetLatencyClock().
	uint64 mBytesWritten = 0;           // cumulative SDL_PutAudioStreamData bytes since last reset
	uint64 mBytesConsumedBase = 0;      // bytes the device consumed in previously-closed intervals
	uint64 mConsumeStartNs = 0;         // SDL_GetTicksNS() at current running interval start; 0 = clock stopped

	// Status / profiling
	ATUIAudioStatus mAudioStatus {};
	uint32 mWritePosition = 0;
	uint32 mProfileCounter = 0;
	uint32 mProfileBlockStartPos = 0;
	uint64 mProfileBlockStartTime = 0;
	uint32 mCheckCounter = 0;           // gates the 15-call drop/stats window
	uint32 mUnderflowCount = 0;         // per-window, reset at window end
	uint32 mOverflowCount = 0;          // per-window, reset at window end
	uint32 mDropCounter = 0;            // Windows-style drop hysteresis

	// Speed-modifier write duplication (mRepeatInc / mRepeatAccum
	// ported from audiooutput.cpp:523 and 1007-1017). Governs how many
	// times each resampled block is pushed to SDL to keep wallclock
	// audio pace during turbo/slowmo.
	uint32 mRepeatInc = 65536;
	uint32 mRepeatAccum = 0;

	// Resampler output buffer (S16 interleaved stereo) — pushed to SDL.
	vdblock<sint16> mOutputBuffer16;

	// Async mix buffer at OUTPUT rate for the no-audio-tap path. Matches
	// Windows audiooutput.cpp:859-886. When an audio tap is present the
	// async sources are mixed at POKEY rate post-filter instead (matching
	// Windows's tap-present branch).
	vdblock<float> mAsyncMixBuffer;
	bool mbAsyncMixBufferZeroed = false;
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
	mResampleAccum = 0;

	mCheckCounter = 0;
	mMinLevel = 0xFFFFFFFFU;
	mMaxLevel = 0;
	mUnderflowCount = 0;
	mOverflowCount = 0;
	mDropCounter = 0;

	mRepeatAccum = 0;

	mWritePosition = 0;

	mProfileBlockStartPos = 0;
	mProfileBlockStartTime = VDGetPreciseTick();
	mProfileCounter = 0;

	// Provisional latency targets: RecomputeBuffering uses mSamplingRate's
	// default (48 kHz). InitNativeAudio will recompute after picking the
	// real device rate.
	RecomputeBuffering();
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
	// Open the device with a placeholder S16 stereo 48 kHz spec. SDL3
	// picks the device and its internal format; we then query the actual
	// device rate and reconfigure the stream so that SDL performs no
	// resampling — Altirra's polyphase filter is the only rate converter.
	SDL_AudioSpec spec {};
	spec.freq = 48000;
	spec.format = SDL_AUDIO_S16;
	spec.channels = 2;

	mpStream = SDL_OpenAudioDeviceStream(
		SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);

	if (!mpStream) {
		fprintf(stderr, "[ATAudioOutputSDL3] Failed to open audio: %s\n",
		        SDL_GetError());
		return;
	}

	// Query the actual device format and clamp to [44100, 48000] to keep
	// the polyphase filter in its downsampling regime (matches Windows
	// audiooutput.cpp:1082-1091). If the device rate is inside that range
	// we configure the stream 1:1 and SDL does zero conversion; if the
	// device runs at e.g. 96 kHz, SDL does a final lightweight upsample
	// from 48 kHz — much cheaper than asking SDL to do the primary
	// ~64 kHz → device conversion.
	SDL_AudioDeviceID devId = SDL_GetAudioStreamDevice(mpStream);
	SDL_AudioSpec devSpec {};
	uint32 preferredRate = 0;
	if (devId && SDL_GetAudioDeviceFormat(devId, &devSpec, nullptr))
		preferredRate = (uint32)devSpec.freq;

	if (preferredRate == 0)
		mSamplingRate = 48000;
	else if (preferredRate < 44100)
		mSamplingRate = 44100;
	else if (preferredRate > 48000)
		mSamplingRate = 48000;
	else
		mSamplingRate = preferredRate;

	// Reconfigure the stream's source format to our chosen sampling rate.
	// Passing dst = NULL leaves the device side alone, so SDL only has to
	// resample if our clamp diverged from the device rate.
	SDL_AudioSpec srcSpec {};
	srcSpec.freq = (int)mSamplingRate;
	srcSpec.format = SDL_AUDIO_S16;
	srcSpec.channels = 2;
	SDL_SetAudioStreamFormat(mpStream, &srcSpec, nullptr);

	RecomputeBuffering();
	RecomputeResamplingRate();

	// Start the pipeline-depth clock from a clean slate now that the
	// final mSamplingRate is known.  ResetLatencyClock honours mPauseCount
	// — if we are starting up already paused, the clock stays stopped and
	// will be started by the matching Resume() call.
	ResetLatencyClock();

	// Only start the stream if no outstanding Pause() request. Windows
	// ReinitAudio has the same guard (audiooutput.cpp:1113-1114).
	if (!mPauseCount)
		SDL_ResumeAudioStreamDevice(mpStream);

	fprintf(stderr, "[ATAudioOutputSDL3] Audio initialized at %u Hz stereo S16 (Altirra polyphase)\n",
	        mSamplingRate);
#endif
}

// -------------------------------------------------------------------------
// SetCyclesPerSecond — update mixing rate and sample player rates
// -------------------------------------------------------------------------

void ATAudioOutputSDL3::SetCyclesPerSecond(double cps, double repeatfactor) {
	mTickRate = cps;
	mMixingRate = (float)(cps / 28.0);
	mAudioStatus.mExpectedRate = cps / 28.0;
	RecomputeResamplingRate();

	// Speed-modifier write duplication — matches Windows audiooutput.cpp:523.
	mRepeatInc = (uint32)(repeatfactor * 65536.0 + 0.5);
}

void ATAudioOutputSDL3::RecomputeBuffering() {
	// SDL3 behaves more like WaveOut/DirectSound than like WASAPI: we push
	// data and it plays from our side of the queue, so we use the
	// non-WASAPI branch from Windows audiooutput.cpp:1032-1034.
	mLatencyTargetMin = ((mLatency * mSamplingRate + 500) / 1000) * 4;
	mLatencyTargetMax = mLatencyTargetMin + ((mExtraBuffer * mSamplingRate + 500) / 1000) * 4;
}

void ATAudioOutputSDL3::RecomputeResamplingRate() {
	mResampleRate = (sint64)(0.5 + 4294967296.0 * mAudioStatus.mExpectedRate / (double)mSamplingRate);

	float pokeyMixingRate = mMixingRate;

	if (mpSamplePlayer)
		mpSamplePlayer->SetRates(pokeyMixingRate, 1.0f, 1.0f / (float)kATCyclesPerSyncSample);

	if (mpEdgeSamplePlayer)
		mpEdgeSamplePlayer->SetRates(pokeyMixingRate, 1.0f, 1.0f / (float)kATCyclesPerSyncSample);

	// Audio tap present → async player mixes at POKEY rate post-filter
	// (tap can observe digitized sources).  No tap → async mixes at
	// output rate and is added during resampling.  Matches Windows
	// audiooutput.cpp:1050-1055.
	if (mpAsyncSamplePlayer) {
		if (mpAudioTap)
			mpAsyncSamplePlayer->SetRates(pokeyMixingRate, 1.0f, 1.0f / (float)kATCyclesPerSyncSample);
		else
			mpAsyncSamplePlayer->SetRates((float)mSamplingRate,
				pokeyMixingRate / (float)mSamplingRate,
				(double)mSamplingRate / mTickRate);
	}
}

// -------------------------------------------------------------------------
// Pipeline-depth accounting — see the comment at mBytesWritten for the
// full design rationale.  These methods are the only writers of
// mBytesWritten / mBytesConsumedBase / mConsumeStartNs.  The emulator is
// single-threaded (WriteAudio, Pause, Resume, InitNativeAudio all run on
// the main thread), so no synchronisation is needed.  SDL_GetTicksNS is
// thread-safe and monotonic.
// -------------------------------------------------------------------------

// Internal helper: fold the currently-running interval (if any) into
// mBytesConsumedBase and stop the clock.  Idempotent — safe to call when
// the clock is already stopped.
void ATAudioOutputSDL3::PauseLatencyClock() {
	if (!mConsumeStartNs)
		return;

#ifndef ALTIRRA_AUDIO_NULL
	const uint64 now = SDL_GetTicksNS();
	const uint64 elapsedNs = now - mConsumeStartNs;

	// Compute (elapsedNs * samplingRate * 4) / 1e9 in uint64 without
	// overflow by splitting elapsedNs into whole seconds and a
	// sub-second remainder.  Each sub-product fits in 64 bits for any
	// realistic session length (the seconds branch tolerates ~10^13
	// seconds ≈ 3×10^5 years at 48 kHz stereo).
	const uint64 bytesPerSec = (uint64)mSamplingRate * 4ULL;
	const uint64 secs  = elapsedNs / 1000000000ULL;
	const uint64 subNs = elapsedNs % 1000000000ULL;
	mBytesConsumedBase += secs * bytesPerSec
	                    + (subNs * bytesPerSec) / 1000000000ULL;
#endif
	mConsumeStartNs = 0;
}

// Restart the clock at the current wallclock instant.  No-op if already
// running.  Safe to call regardless of mPauseCount, but the caller is
// responsible for matching pause semantics (Resume() is the correct
// site; InitNativeAudio uses ResetLatencyClock instead).
void ATAudioOutputSDL3::ResumeLatencyClock() {
	if (mConsumeStartNs)
		return;
#ifndef ALTIRRA_AUDIO_NULL
	mConsumeStartNs = SDL_GetTicksNS();
#endif
}

// Drop all accumulated state and start fresh.  Called when the stream
// is (re)created or its format changes in a way that invalidates
// previous cumulative counts.  If the emulator is currently paused
// (mPauseCount > 0), the clock is left stopped and will be restarted
// by the matching Resume().
void ATAudioOutputSDL3::ResetLatencyClock() {
	mBytesWritten      = 0;
	mBytesConsumedBase = 0;
	mConsumeStartNs    = 0;

	if (!mPauseCount)
		ResumeLatencyClock();
}

// Checkpoint elapsed bytes at the old sampling rate, then continue
// accumulating at the new rate.  Only matters if the sampling rate
// actually changes mid-session — today's code sets mSamplingRate once
// in InitNativeAudio, but the hook is wired for forward compatibility
// with the Windows pattern of re-checking GetMixingRate() every call
// (audiooutput.cpp:838-844).
void ATAudioOutputSDL3::RetargetLatencyClock(uint32 oldSamplingRate) {
	if (!mConsumeStartNs || oldSamplingRate == mSamplingRate)
		return;

#ifndef ALTIRRA_AUDIO_NULL
	const uint64 now = SDL_GetTicksNS();
	const uint64 elapsedNs = now - mConsumeStartNs;
	const uint64 bytesPerSec = (uint64)oldSamplingRate * 4ULL;
	const uint64 secs  = elapsedNs / 1000000000ULL;
	const uint64 subNs = elapsedNs % 1000000000ULL;
	mBytesConsumedBase += secs * bytesPerSec
	                    + (subNs * bytesPerSec) / 1000000000ULL;
	mConsumeStartNs = now;
#else
	(void)oldSamplingRate;
#endif
}

// Returns the Windows-EstimateHWBufferLevel-equivalent pipeline depth:
// bytes we have committed to the output pipeline minus bytes the device
// must have played by now.  Returns zero if the clock is stopped
// (pre-InitNativeAudio or while paused), which correctly makes the
// rate-control check a no-op — we cannot be overflowing a device that
// isn't running.
//
// If the wallclock-based consumption estimate overshoots mBytesWritten
// we re-baseline the clock.  This is the correct semantic recovery for
// three scenarios where the naive `elapsed × rate` formula would lie:
//
//   (1) Startup lag.  The clock is armed in InitNativeAudio, but the
//       emulator may not call WriteAudio for hundreds of ms to multiple
//       seconds while it loads the ROM and enters the main loop.
//       Without the re-baseline, `consumed` accrues a phantom deficit
//       and every subsequent call takes the underflow branch (double
//       write) until mBytesWritten catches up — dumping the full
//       deficit worth of audio into SDL at once.  2 s of startup lag
//       produced 2 s of audio backlog; this is exactly the regression
//       you hit.
//
//   (2) Main-loop stalls (long GC, disk flush, scheduler preemption).
//       While WriteAudio is paused but the SDL clock is running, the
//       device has also starved — it cannot have consumed more than we
//       fed it before the stall.
//
//   (3) Device-side prebuffer / underrun at the OS layer (some
//       backends prefill a hardware buffer before starting playback).
//       Wallclock advances, actual playback hasn't caught up.  The
//       re-baseline settles us onto the correct slope.
//
// In steady state (producer rate == device consumption rate) the
// re-baseline never fires — mBytesWritten stays comfortably above
// consumed by exactly mLatencyTargetMin.
uint32 ATAudioOutputSDL3::ComputePendingBytes() {
	uint64 consumed = mBytesConsumedBase;

#ifndef ALTIRRA_AUDIO_NULL
	if (mConsumeStartNs) {
		const uint64 now = SDL_GetTicksNS();
		const uint64 elapsedNs = now - mConsumeStartNs;
		const uint64 bytesPerSec = (uint64)mSamplingRate * 4ULL;
		const uint64 secs  = elapsedNs / 1000000000ULL;
		const uint64 subNs = elapsedNs % 1000000000ULL;
		consumed += secs * bytesPerSec
		          + (subNs * bytesPerSec) / 1000000000ULL;
	}
#endif

	if (consumed > mBytesWritten) {
		// See function header — our estimate ran ahead of reality.
		// Pin the baseline to the only hard upper bound we have
		// (the device cannot have played more than we fed it) and
		// restart the interval so the next call starts counting
		// from a fresh, realistic anchor.
		mBytesConsumedBase = mBytesWritten;
#ifndef ALTIRRA_AUDIO_NULL
		if (mConsumeStartNs)
			mConsumeStartNs = SDL_GetTicksNS();
#endif
		return 0;
	}

	const uint64 diff = mBytesWritten - consumed;
	return diff > 0xFFFFFFFFULL ? 0xFFFFFFFFU : (uint32)diff;
}

// Single point of SDL_PutAudioStreamData + write-counter bookkeeping.
// If the call fails (out of memory, stream in an invalid state) we skip
// the bookkeeping so the pending estimate stays accurate — next call
// will correctly see a lower-than-expected pending and push harder.
bool ATAudioOutputSDL3::StreamPut(const void *data, uint32 bytes) {
	if (!mpStream || !bytes)
		return true;

	if (!SDL_PutAudioStreamData(mpStream, data, (int)bytes))
		return false;

	mBytesWritten += bytes;
	return true;
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
		PauseLatencyClock();
	}
}

void ATAudioOutputSDL3::Resume() {
	if (!--mPauseCount) {
		if (mpStream)
			SDL_ResumeAudioStreamDevice(mpStream);
		ResumeLatencyClock();
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
// Ports ATAudioOutput::InternalWriteAudio() from the Windows implementation.
// Diverges only at the very last step: the resampler output (sint16 stereo
// at mSamplingRate) is pushed to SDL_AudioStream instead of IVDAudioOutput.
// SDL performs no sample-rate conversion because we configure the stream's
// src/dst formats to match.
//
// Note: count may legitimately be zero. The outer WriteAudio loop calls
// InternalWriteAudio with tc=0 when mBufferLevel == kBufferSize, to give
// the resample/output stage a chance to drain buffered samples before the
// `if (!tc) break;` exit. The mixing steps are guarded by `if (count)`.
// -------------------------------------------------------------------------

void ATAudioOutputSDL3::InternalWriteAudio(
	const float* left,
	const float* right,
	uint32 count,
	bool pushAudio,
	bool pushStereoAsMono,
	uint64 timestamp)
{
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

	// Async sources only influence filter-chain stereo state on the tap
	// path, because that's the only path where async mixes into the
	// POKEY-rate source buffer. In the no-tap path, async sources write
	// to mAsyncMixBuffer at output rate and are added during resampling,
	// so they don't care about mbFilterStereo. Matches Windows
	// audiooutput.cpp:661-669.
	if (mpAudioTap) {
		for(IATAudioAsyncSource *src : mAsyncAudioSources) {
			if (src->RequiresStereoMixingNow()) {
				needStereo = true;
			} else {
				needMono = true;
			}
		}
	}

	// Switch to stereo filtering if needed. Matches Windows
	// audiooutput.cpp:672-676 exactly: copy the filter's internal state
	// and mBufferLevel floats of buffered data to the right channel.
	// Partial-shift semantics mean mBufferLevel >= kPreFilterOffset in
	// steady state, so the filter lookback zone is covered implicitly.
	if (needStereo && !mbFilterStereo) {
		mFilters[1].CopyState(mFilters[0]);
		memcpy(mSourceBuffer[1], mSourceBuffer[0], sizeof(float) * mBufferLevel);
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

	// ---- Step 9: Audio tap forwarding (tap path only) ----
	// Matches Windows audiooutput.cpp:814-832: when a tap is present, async
	// sources are mixed at POKEY rate post-filter and the tap observes the
	// combined stream.  When no tap is present, async sources are mixed at
	// OUTPUT rate later, during resampling (Step 10).
	if (mpAudioTap) {
		ATAudioAsyncMixInfo asyncMixInfo {};
		asyncMixInfo.mStartTime = timestamp;
		asyncMixInfo.mCount = count;
		asyncMixInfo.mNumCycles = count * kATCyclesPerSyncSample;
		asyncMixInfo.mMixingRate = mMixingRate;
		asyncMixInfo.mpLeft = mSourceBuffer[0] + mBufferLevel + kFilterOffset;
		asyncMixInfo.mpRight = mbFilterStereo
			? mSourceBuffer[1] + mBufferLevel + kFilterOffset
			: nullptr;
		asyncMixInfo.mpMixLevels = mMixLevels;

		for (IATAudioAsyncSource *asyncSource : mAsyncAudioSources)
			asyncSource->WriteAsyncAudio(asyncMixInfo);

		if (mbFilterStereo)
			mpAudioTap->WriteRawAudio(mSourceBuffer[0] + mBufferLevel + kFilterOffset,
				mSourceBuffer[1] + mBufferLevel + kFilterOffset, count, timestamp);
		else
			mpAudioTap->WriteRawAudio(mSourceBuffer[0] + mBufferLevel + kFilterOffset,
				nullptr, count, timestamp);
	}

	mBufferLevel += count;
	VDASSERT(mBufferLevel <= kBufferSize);

	// ---- Step 10: Resample + output (ported from Windows audiooutput.cpp:846-1025) ----
	//
	// Altirra's polyphase resampler converts POKEY-rate float samples to
	// S16 stereo at mSamplingRate.  SDL is used as a dumb byte sink; its
	// internal stream performs no sample-rate conversion because src and
	// dst rates are equal (or differ only by a final device upsample).

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

			// No-tap path: mix async sources at OUTPUT rate into the async
			// mix buffer, combined below during resampling via *Add16.
			if (!mpAudioTap) {
				ATAudioAsyncMixInfo asyncMixInfo {};
				asyncMixInfo.mStartTime = timestamp;
				asyncMixInfo.mCount = resampleCount;
				asyncMixInfo.mNumCycles = count * kATCyclesPerSyncSample;
				asyncMixInfo.mMixingRate = mMixingRate;
				asyncMixInfo.mpLeft = asyncLeft;
				asyncMixInfo.mpRight = asyncRight;
				asyncMixInfo.mpMixLevels = mMixLevels;

				for (IATAudioAsyncSource *asyncSource : mAsyncAudioSources) {
					if (asyncSource->WriteAsyncAudio(asyncMixInfo))
						mbAsyncMixBufferZeroed = false;
				}
			}

			if (mbMute) {
				mResampleAccum += (uint64)mResampleRate * resampleCount;
				memset(mOutputBuffer16.data(), 0, sizeof(mOutputBuffer16[0]) * resampleCount * 2);
			} else if (mbFilterStereo) {
				if (mbAsyncMixBufferZeroed)
					mResampleAccum = ATFilterResampleStereo16(mOutputBuffer16.data(),
						mSourceBuffer[0], mSourceBuffer[1], resampleCount,
						mResampleAccum, mResampleRate, true);
				else
					mResampleAccum = ATFilterResampleStereoAdd16(mOutputBuffer16.data(),
						mSourceBuffer[0], mSourceBuffer[1], asyncLeft, asyncRight,
						resampleCount, mResampleAccum, mResampleRate, true);
			} else {
				if (mbAsyncMixBufferZeroed)
					mResampleAccum = ATFilterResampleMonoToStereo16(mOutputBuffer16.data(),
						mSourceBuffer[0], resampleCount,
						mResampleAccum, mResampleRate, true);
				else
					mResampleAccum = ATFilterResampleMonoToStereoAdd16(mOutputBuffer16.data(),
						mSourceBuffer[0], asyncLeft, asyncRight, resampleCount,
						mResampleAccum, mResampleRate, true);
			}

			// Shift consumed source samples out of the buffer.  Preserves
			// mBufferLevel - shift samples PLUS kPreFilterOffset of filter
			// lookback state (matches Windows audiooutput.cpp:904-920).
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

	VDASSERT(mResampleAccum < (uint64)mOutputBuffer16.size() << (32+4));

	// ---- Status window + drop hysteresis ----
	// `bytes` is the total downstream pipeline depth, reconstructed from
	// wallclock + cumulative Put counts.  This is the SDL3 replacement
	// for Windows's EstimateHWBufferLevel — see the comment block at
	// mBytesWritten for why SDL_GetAudioStreamQueued by itself is not
	// sufficient.  ComputePendingBytes returns zero while the clock is
	// stopped (pre-InitNativeAudio and while paused), which correctly
	// suppresses the overflow branch in those states.
	uint32 bytes = ComputePendingBytes();

	if (mMinLevel > bytes)
		mMinLevel = bytes;

	if (mMaxLevel < bytes)
		mMaxLevel = bytes;

	uint32 adjustedLatencyTargetMin = mLatencyTargetMin;
	uint32 adjustedLatencyTargetMax = mLatencyTargetMax;

	bool dropBlock = false;
	if (++mCheckCounter >= 15) {
		mCheckCounter = 0;

		// None             - see if we can remove data to lower latency
		// Underflow        - do nothing; we already add data for this
		// Overflow         - do nothing; we may be in turbo
		// Under + overflow - increase spread
		bool tryDrop = false;
		if (!mUnderflowCount) {
			if (mMinLevel > adjustedLatencyTargetMin + resampleCount * 8)
				tryDrop = true;
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

		mAudioStatus.mIncomingRate = (double)(mWritePosition - mProfileBlockStartPos)
			/ (double)(t - mProfileBlockStartTime) * VDGetPreciseTicksPerSecond();

		mProfileBlockStartPos = mWritePosition;
		mProfileBlockStartTime = t;
	}

	// ---- Underflow: push an extra block ahead of the normal write ----
	// Mirrors Windows audiooutput.cpp:992-1000.  On underflow we refill
	// the queue with an extra copy of this block; control then falls
	// through to the normal write path below, so the net effect at
	// normal speed is two writes for this call instead of one.
	if (mpStream && bytes < adjustedLatencyTargetMin) {
		++mAudioStatus.mUnderflowCount;
		++mUnderflowCount;

		StreamPut(mOutputBuffer16.data(), resampleCount * 4);

		mDropCounter = 0;
		dropBlock = false;
	}

	// ---- Normal write path ----
	if (dropBlock) {
		++mAudioStatus.mDropCount;
	} else if (mpStream) {
		if (bytes < adjustedLatencyTargetMin + adjustedLatencyTargetMax) {
			mRepeatAccum += mRepeatInc;

			uint32 repeats = mRepeatAccum >> 16;
			mRepeatAccum &= 0xffff;

			if (repeats > 10)
				repeats = 10;

			while (repeats--)
				StreamPut(mOutputBuffer16.data(), resampleCount * 4);
		} else {
			++mOverflowCount;
			++mAudioStatus.mOverflowCount;
		}
	}
	(void)pushAudio;
}

// =========================================================================
// Factory function
// =========================================================================

IATAudioOutput *ATCreateAudioOutput() {
	return new ATAudioOutputSDL3();
}
