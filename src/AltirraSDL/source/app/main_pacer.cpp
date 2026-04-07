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
}

void FramePacer::UpdateRate(double fps) {
	targetSecsPerFrame = 1.0 / fps;
}

// Called after a frame is complete.  Sleeps to maintain correct rate.
void FramePacer::WaitForNextFrame() {
	uint64_t now = SDL_GetPerformanceCounter();
	int64_t elapsed = (int64_t)(now - lastFrameTime);
	int64_t targetTicks = (int64_t)(targetSecsPerFrame * (double)perfFreq);

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

	// Clamp error to ±2 frames to prevent runaway drift from clock
	// glitches (suspend/resume, NTP step).  On reset, seed with one
	// full frame of "early" credit so the next frame sleeps for a
	// full period — matches Windows main.cpp:3158 which sets
	// `error = -g_frameTicks` (one frame of negative = one frame of
	// early in their sign convention).
	int64_t errorBound = 2 * targetTicks;
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
		const double maxMs = (double)maxElapsedTicks * 1000.0 / (double)perfFreq;
		const double targetMs = targetSecsPerFrame * 1000.0;
		LOG_INFO("Pace",
			"fps=%.1f late=%u/%u maxFrame=%.1fms target=%.2fms errAcc=%+.1fms",
			(double)measuredFPS,
			lateFrameCount,
			frameCount,
			maxMs,
			targetMs,
			(double)errorAccum * 1000.0 / (double)perfFreq);

		frameCount = 0;
		lateFrameCount = 0;
		maxElapsedTicks = 0;
		telemetryStart = lastFrameTime;
	}
}

FramePacer g_pacer;

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

	if (ATUIGetFrameRateVSyncAdaptive()) {
		double refreshPeriod = GetDisplayRefreshPeriod();
		if (refreshPeriod > 0
			&& refreshPeriod > secsPerFrame * 0.98
			&& refreshPeriod < secsPerFrame * 1.02)
		{
			secsPerFrame = refreshPeriod;
		}
	}

	g_pacer.UpdateRate(1.0 / secsPerFrame);

	// Update audio output resampling rate.
	IATAudioOutput *audio = g_sim.GetAudioOutput();
	if (audio)
		audio->SetCyclesPerSecond(cyclesPerSecond, 1.0 / rate);
}
