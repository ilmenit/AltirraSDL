//	AltirraSDL - Frame pacer (split from main_sdl3.cpp Phase 3c)

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <vd2/system/vdtypes.h>
#include <at/atcore/constants.h>
#include "simulator.h"
#include "gtia.h"
#include "uiaccessors.h"
#include "app_internal.h"
#include "logging.h"

extern ATSimulator g_sim;
extern SDL_Window *g_pWindow;
#include <at/ataudio/audiooutput.h>
#include <algorithm>


// =========================================================================
// Frame pacing — matches Windows main.cpp timing architecture
// =========================================================================

// Frame period tables — mirrors Windows ATUIUpdateSpeedTiming() kPeriods[3][3].
// Rows: Hardware / Broadcast / Integral.  Cols: NTSC / PAL / SECAM.
static constexpr double kMasterClocks[3] = {
	kATMasterClock_NTSC,
	kATMasterClock_PAL,
	kATMasterClock_SECAM,
};

static constexpr double kFramePeriods[3][3] = {
	{ 1.0 / kATFrameRate_NTSC, 1.0 / kATFrameRate_PAL, 1.0 / kATFrameRate_SECAM },
	{ 1.0 / 59.9400,           1.0 / 50.0000,          1.0 / 50.0 },
	{ 1.0 / 60.0000,           1.0 / 50.0000,          1.0 / 50.0 },
};

// FramePacer struct definition lives in app_internal.h (Phase 3c).
// Method bodies are unchanged from the original in main_sdl3.cpp;
// only their containing-class form changed.

void FramePacer::Init() {
	perfFreq = SDL_GetPerformanceFrequency();
	lastFrameTime = SDL_GetPerformanceCounter();
	errorAccum = 0;
	targetSecsPerFrame = kFramePeriods[0][0]; // NTSC hardware rate
	frameCount = 0;
	telemetryStart = lastFrameTime;
	measuredFPS = 0.0f;
	lateFrameCount = 0;
	maxElapsedTicks = 0;
	clockRecoveryFactor = 1.0;
}

// =========================================================================
// Active clock recovery — deliberate divergence from Windows Altirra
// =========================================================================
//
// This is not a straight port of Windows behaviour.  Windows Altirra
// paces the emulator purely off wallclock (via a waitable timer + DXGI
// Present), accepts that the audio queue will drift anywhere inside its
// [mLatencyTargetMin, mLatencyTargetMin + mLatencyTargetMax] window,
// and lives with the resulting 80–260 ms A/V offset at default
// settings.  That's fine on Windows because WaveOut / DirectSound /
// WASAPI all expose exact hardware-side buffer-level accounting and
// their Write() calls block when the pipeline is full, so the queue
// stays bounded without any feedback loop.
//
// On SDL3 we don't have either guarantee:
//   - SDL_PutAudioStreamData never blocks.
//   - SDL_GetAudioStreamQueued only sees stream-input bytes, missing
//     SDL's per-device buffer, the backend client/server buffers, and
//     the device FIFO — anywhere from 20 ms on native PipeWire to
//     multiple seconds on PulseAudio with default tlength.
//
// audiooutput_sdl3.cpp reconstructs a Windows-equivalent "bytes pending
// downstream" signal by tracking cumulative pushes minus wallclock-
// extrapolated consumption.  That fixes *accounting* (the signal is
// right) but not *drift*: any difference between the emulator's idea of
// real-time and the device's crystal-actual consumption rate is still
// absorbed by the audio queue wandering inside its target window.
//
// Active clock recovery closes the remaining loop.  We read the
// pipeline depth once per frame, compare to the user's configured
// latency target (mLatency), and nudge the frame period by a tiny
// proportional correction:
//
//   correction = clamp(Kp * (pending - target) / target,
//                     -kMaxCorrection, +kMaxCorrection)
//
// The correction goes directly onto targetSecsPerFrame for this frame,
// so the pacer's existing sleep/error-accumulator logic naturally
// tightens or relaxes the frame rate in response.  Faster emulator =
// more POKEY samples per wallclock second = queue fills.  Slower
// emulator = queue drains.
//
// Why the ±0.5 % clamp:
//   - 0.5 % pitch shift is ≈ 8.6 cents.  Perceptibility of constant
//     pitch shifts below ~10 cents is very poor even for trained
//     listeners; on emulator-synthesised POKEY voices (square waves,
//     noise) it is essentially inaudible.  Confirmed by the same
//     reasoning in Mednafen, Dolphin, PCSX2, and other emulators that
//     apply exactly this technique.
//   - The correction is *constant within a frame*, not a ramp.  There
//     is no vibrato-style modulation that the ear is far more
//     sensitive to than slow detuning.
//   - Proportional-only control (no integral term) rules out limit-
//     cycle oscillation from integrator windup.  The audio engine's
//     own push-doubling on underflow and drop-hysteresis on overflow
//     handle large/fast deviations (startup, stalls, overflow);
//     clock recovery only needs to handle steady-state drift.
//
// Why this is a "feature" divergence, not a bug compat fix:
//   - Windows users do not get this tighter pacing.
//   - A user switching between the Windows build and this SDL3 build
//     *will* notice the SDL3 build feels ~80 ms snappier at default
//     settings (lower A/V offset, less audio lag on input).
//   - If that difference ever becomes a problem, this function can be
//     disabled by simply not calling it from WaitForNextFrame, and
//     behaviour reverts to Windows-parity.
//
// Fallback paths (clockRecoveryFactor left at 1.0):
//   - Audio output not yet created, or dummy driver.
//   - Audio output reports UINT32_MAX (signal unavailable — Windows
//     build would hit this if this code ever ran there).
//   - Sampling rate not yet known (pre-InitNativeAudio).
//   - User has latency set to 0 (degenerate target).
void FramePacer::ComputeClockRecovery() {
	IATAudioOutput *audio = g_sim.GetAudioOutput();
	if (!audio) {
		clockRecoveryFactor = 1.0;
		return;
	}

	const uint32 pendingBytes = audio->GetPipelineLatencyBytes();
	if (pendingBytes == 0xFFFFFFFFu) {
		// Signal unavailable — fall back to pure wallclock pacing.
		clockRecoveryFactor = 1.0;
		return;
	}

	const ATUIAudioStatus status = audio->GetAudioStatus();
	const uint32 samplingRate = (uint32)status.mSamplingRate;
	const int latencyMs = audio->GetLatency();

	if (samplingRate == 0 || latencyMs <= 0) {
		clockRecoveryFactor = 1.0;
		return;
	}

	// Target is the same quantity as audiooutput_sdl3's
	// mLatencyTargetMin — the lower rail of the accepting-push window.
	// We want the queue pinned at this rail, not wandering upward
	// toward min+max.
	const uint32 targetBytes =
		((uint32)latencyMs * samplingRate + 500u) / 1000u * 4u;

	if (targetBytes == 0) {
		clockRecoveryFactor = 1.0;
		return;
	}

	// Signed deviation expressed as a fraction of target.
	// -1.0 = queue empty, 0.0 = at target, +1.0 = at 2× target, etc.
	const double deviation =
		(double)((int64_t)pendingBytes - (int64_t)targetBytes)
		/ (double)targetBytes;

	// Proportional control.  With kGain == kMaxCorrection, the
	// correction saturates at |deviation| == 1.0, i.e. at queue=0 or
	// queue=2×target.  Smaller deviations give proportionally smaller
	// corrections, so steady-state at near-target error gives
	// near-zero pitch shift.
	constexpr double kMaxCorrection = 0.005; // ±0.5 %  (±8.6 cents)
	constexpr double kGain = kMaxCorrection;

	double correction = kGain * deviation;
	if (correction >  kMaxCorrection) correction =  kMaxCorrection;
	if (correction < -kMaxCorrection) correction = -kMaxCorrection;

	// deviation > 0 means queue is too full → emulator ran slightly
	// fast → we want a *longer* frame (slower pacing) → factor > 1.
	// deviation < 0 means the opposite.  Sign matches directly.
	clockRecoveryFactor = 1.0 + correction;
}

void FramePacer::UpdateRate(double fps) {
	targetSecsPerFrame = 1.0 / fps;
}

// Called after a frame is complete.  Sleeps to maintain correct rate.
void FramePacer::WaitForNextFrame() {
	// Read audio-pipeline depth and update clockRecoveryFactor for
	// this frame.  See ComputeClockRecovery for full rationale — this
	// is a deliberate divergence from Windows Altirra, enabled only on
	// the SDL3 build where the audio backend has no blocking Write()
	// and no exact hardware-side buffer accounting.  When the audio
	// output doesn't expose the feedback signal (dummy driver, pre-
	// init, etc.) the factor is left at 1.0 and this is a no-op.
	ComputeClockRecovery();

	uint64_t now = SDL_GetPerformanceCounter();
	int64_t elapsed = (int64_t)(now - lastFrameTime);
	int64_t targetTicks =
		(int64_t)(targetSecsPerFrame * clockRecoveryFactor
		          * (double)perfFreq);

	// Capture the frame start time BEFORE the sleep.  This is the
	// critical piece: the next call's `elapsed` must include the time
	// we spend sleeping here, otherwise the sleep duration never enters
	// the error accumulator and the loop systematically runs too fast.
	// Matches Windows main.cpp:3145-3146:
	//     sint64 lastFrameDuration = curTime - lastTime;
	//     lastTime = curTime;       // <- before the sleep
	// Previously we captured lastFrameTime after SDL_DelayPrecise,
	// which made elapsed on the next call reflect only the work time
	// (not work+sleep), causing errorAccum to grow without bound and
	// repeatedly hit the clamp.  Result was ~5% pacer drift, POKEY
	// over-production, audio queue overflow, audible crackling.
	lastFrameTime = now;

	// Telemetry: track frames that exceeded their wall-clock budget
	// (main loop + render took longer than one frame period).  These
	// are the frames where emulation is falling behind realtime, which
	// is the primary cause of audio underruns when it happens.
	if (elapsed > targetTicks)
		++lateFrameCount;
	if (elapsed > maxElapsedTicks)
		maxElapsedTicks = elapsed;

	// Error accumulator: positive = we finished early, need to wait.
	// Mirrors the Windows "error += lastFrameDuration - g_frameTicks"
	// logic, but with inverted sign (they track lateness, we track
	// earliness).
	errorAccum += targetTicks - elapsed;

	// Clamp error to prevent runaway drift from clock glitches
	// (suspend/resume, NTP step, compositor hiccups, GPU contention).
	// On reset, seed with one full frame of "early" credit so the next
	// frame sleeps for a full period — matches Windows main.cpp:3158
	// which sets `error = -g_frameTicks` (one frame of negative =
	// one frame of early in their sign convention).
	//
	// The bound is max(2 frames, 100 ms) to match Windows main.cpp:551:
	//     g_frameErrorBound = max(2 * g_frameTicks, secondTime * 0.1)
	// The 100 ms floor lets the accumulator absorb transient stalls
	// (window moves, GC pauses, vsync jitter on high-refresh displays)
	// without triggering a reseed that would itself cause a visible
	// judder and briefly starve the audio queue. Earlier versions used
	// 2*targetTicks unconditionally (~33 ms at 60 Hz), which is 3x
	// tighter than Windows and caused spurious reseeds whenever a
	// single loop iteration ran long.
	const int64_t minBound = perfFreq / 10; // 100 ms, matches Windows
	const int64_t errorBound = std::max<int64_t>(2 * targetTicks, minBound);
	if (errorAccum > errorBound || errorAccum < -errorBound)
		errorAccum = targetTicks;

	// If we're ahead of schedule, sleep.
	if (errorAccum > 0) {
		uint64_t waitNs = (uint64_t)((double)errorAccum / (double)perfFreq * 1e9);

		// Sanity-cap the sleep duration.  Mirrors Windows main.cpp:3199
		// (g_frameTimeout) which protects against monotonic-clock glitches:
		// system suspend/resume, NTP step, VM hypervisor pause, etc., can
		// produce a multi-second computed wait that would freeze the
		// emulator.  Cap at min(5 frames, 1 second).
		const uint64_t targetNs = (uint64_t)(targetSecsPerFrame * 1e9);
		const uint64_t maxWaitNs = std::min<uint64_t>(5 * targetNs, 1000000000ULL);
		if (waitNs > maxWaitNs) {
			// Way off — skip the sleep entirely and let the error accumulator
			// re-sync naturally on the next frame.
			errorAccum = 0;
		} else if (waitNs > 1000000) { // only bother sleeping > 1ms
			SDL_DelayPrecise(waitNs);
		}
	}

	// Update telemetry once per second.
	++frameCount;
	uint64_t telemetryElapsed = lastFrameTime - telemetryStart;
	if (telemetryElapsed >= perfFreq) {
		measuredFPS = (float)((double)frameCount * (double)perfFreq
			/ (double)telemetryElapsed);

		// Once-per-second frame-pacing diagnostic line.  Pair with
		// the [Audio] line emitted from audiooutput_sdl3.cpp to
		// classify crackling causes:
		//
		//   lateFrames > 0 AND audio underflows > 0
		//     → the main loop can't keep up with realtime; emulation
		//       + rendering take longer than a frame period, so
		//       POKEY produces audio too slowly and SDL drains faster
		//       than we push.  Render pipeline bottleneck.
		//
		//   lateFrames == 0 AND audio underflows > 0
		//     → frame pacing is healthy but the SDL audio queue is
		//       starving anyway.  Either the OS audio server is
		//       under-buffering, or clock recovery can't compensate
		//       for device drift within the ±0.5% clamp.
		//
		//   lateFrames == 0 AND audio underflows == 0
		//     → everything is healthy; crackling is elsewhere (e.g.
		//       user-space audio server bug).
		frameCount = 0;
		lateFrameCount = 0;
		maxElapsedTicks = 0;
		telemetryStart = lastFrameTime;
	}
}

FramePacer g_pacer;
int g_desiredSwapInterval = 0;  // matches startup SDL_GL_SetSwapInterval(0)

float ATUIGetMeasuredFPS() {
	return g_pacer.measuredFPS;
}

// =========================================================================
// Update frame pacer rate from current video standard
// =========================================================================

// Returns the video standard table index: 0=NTSC, 1=PAL, 2=SECAM.
// Mirrors Windows ATUIUpdateSpeedTiming() tableIndex logic.
static int GetVideoStandardIndex() {
	const auto vstd = g_sim.GetVideoStandard();
	const bool isSECAM = vstd == kATVideoStandard_SECAM;
	const bool hz50 = vstd != kATVideoStandard_NTSC && vstd != kATVideoStandard_PAL60;
	return isSECAM ? 2 : hz50 ? 1 : 0;
}

// Get the display refresh period (seconds) for the window's current display.
// Returns 0 if unavailable.
static double GetDisplayRefreshPeriod() {
	SDL_DisplayID displayID = SDL_GetDisplayForWindow(g_pWindow);
	if (!displayID)
		return 0;

	const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(displayID);
	if (!mode || mode->refresh_rate <= 0.0f)
		return 0;

	// Use precise numerator/denominator if available, otherwise float.
	if (mode->refresh_rate_numerator > 0 && mode->refresh_rate_denominator > 0)
		return (double)mode->refresh_rate_denominator / (double)mode->refresh_rate_numerator;

	return 1.0 / (double)mode->refresh_rate;
}

// Update frame pacing and audio resampling to match current video standard,
// frame rate mode, and speed settings.
// Ported from Windows ATUIUpdateSpeedTiming() (main.cpp lines 491-553).
void UpdatePacerRate() {
	const int tableIndex = GetVideoStandardIndex();
	const ATFrameRateMode frameRateMode = ATUIGetFrameRateMode();
	const double rawSecsPerFrame = kFramePeriods[frameRateMode][tableIndex];

	// Compute adjusted cycles per second for audio resampling.
	// When frame rate mode changes the effective frame period (e.g. Broadcast
	// 59.94 vs Hardware 59.9227), the audio engine must resample at a matching
	// rate so pitch stays correct.
	const double cyclesPerSecond = kMasterClocks[tableIndex]
		* kFramePeriods[0][tableIndex] / rawSecsPerFrame;

	// Speed multiplier: modifier + 1.0, with slow motion factor.
	// Windows skips modifier in turbo (audio rate is irrelevant at warp speed).
	double rate = 1.0;
	if (!g_sim.IsTurboModeEnabled()) {
		rate = std::max(0.0, (double)ATUIGetSpeedModifier() + 1.0);
		if (ATUIGetSlowMotion())
			rate *= 0.5; // Windows default: g_ATCVEngineSlowmoScale = 0.5
	}
	rate = std::clamp(rate, 0.01, 100.0);

	// Adaptive VSync: if enabled and display refresh is within 2% of the
	// target frame rate, lock to the display refresh period instead of the
	// emulation period.  This prevents the ~1 frame drop every ~12 seconds
	// that occurs when a 60Hz display runs 59.92Hz NTSC content.
	// Matches Windows ATUIUpdateSpeedTiming() lines 544-547.
	double secsPerFrame = rawSecsPerFrame / rate;

	// Check whether the display refresh rate is close enough to the target
	// frame rate for VSync to work without introducing judder.  This check
	// is independent of the adaptive-VSync user setting: adaptive VSync
	// adjusts the *frame rate* to match the display, while this controls
	// whether the GL swap interval blocks on vblank.
	//
	// When the rates are mismatched (e.g. PAL 50 Hz on a 60 Hz display),
	// blocking VSync (swap interval 1) quantises presentation to 16.67 ms
	// boundaries, creating a repeating 5:6 judder pattern.  Disabling it
	// (swap interval 0) lets the frame pacer deliver even frame spacing
	// while the desktop compositor prevents tearing — matching Windows
	// Altirra's DXGI Present(0) + DWM behaviour in windowed mode.
	bool rateMatch = false;
	double refreshPeriod = GetDisplayRefreshPeriod();
	if (refreshPeriod > 0
		&& refreshPeriod > secsPerFrame * 0.98
		&& refreshPeriod < secsPerFrame * 1.02)
	{
		rateMatch = true;
	}

	// Only enable blocking VSync (swap interval 1) when:
	// - The user has VSync enabled in settings (GTIA mbVsyncEnabled), AND
	// - The display refresh matches the target frame rate.
	// This mirrors Windows where mbVsyncEnabled controls whether
	// PresentVSync() or Present() is called (gtia.cpp:1940-1951,
	// displaydrv3d.cpp:634-648).
	g_desiredSwapInterval = (g_sim.GetGTIA().IsVsyncEnabled() && rateMatch) ? 1 : 0;

	if (ATUIGetFrameRateVSyncAdaptive() && rateMatch) {
		secsPerFrame = refreshPeriod;
	}

	g_pacer.UpdateRate(1.0 / secsPerFrame);

	// Update audio output resampling rate.
	IATAudioOutput *audio = g_sim.GetAudioOutput();
	if (audio)
		audio->SetCyclesPerSecond(cyclesPerSecond, 1.0 / rate);
}
