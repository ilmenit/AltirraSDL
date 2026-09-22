//	Altirra - Atari 800/800XL/5200 emulator
//	System library - timing functions for non-Windows (SDL3)

#include <stdafx.h>
#include <chrono>

#include <vd2/system/vdtypes.h>
#include <vd2/system/time.h>
#include <vd2/system/thread.h>

// Cooperative lazy-timer scheduler — see the VDLazyTimer block at the
// bottom of this file.  Needs vector / mutex / function / algorithm.
#include <vector>
#include <mutex>
#include <functional>
#include <algorithm>

// -------------------------------------------------------------------------
// Tick / precision timer
// -------------------------------------------------------------------------
//
// Previously backed by SDL_GetTicks / SDL_GetPerformanceCounter. Switched
// to std::chrono::steady_clock so the system library has no SDL3 call
// sites — the headless AltirraBridgeServer target depends on this.
// steady_clock is monotonic on all supported platforms (Linux/macOS use
// CLOCK_MONOTONIC, Windows uses QueryPerformanceCounter), matching SDL's
// guarantees.

// Function-local static so initialisation is thread-safe (C++11 magic
// statics) and immune to static-initialisation-order fiasco — any VD
// timing call reaching this TU before module init is still well-defined.
static std::chrono::steady_clock::time_point VDGetStartTime() {
	static const auto t0 = std::chrono::steady_clock::now();
	return t0;
}

uint32 VDGetCurrentTick() {
	using namespace std::chrono;
	return (uint32)duration_cast<milliseconds>(steady_clock::now() - VDGetStartTime()).count();
}

uint64 VDGetCurrentTick64() {
	using namespace std::chrono;
	return (uint64)duration_cast<milliseconds>(steady_clock::now() - VDGetStartTime()).count();
}

uint64 VDGetPreciseTick() {
	using namespace std::chrono;
	return (uint64)duration_cast<nanoseconds>(steady_clock::now() - VDGetStartTime()).count();
}

static uint64 sInitPreciseFreq() {
	return 1000000000ULL;  // nanoseconds-per-second, matches VDGetPreciseTick
}

uint64 VDGetPreciseTicksPerSecondI() {
	static uint64 freq = sInitPreciseFreq();
	return freq;
}

double VDGetPreciseTicksPerSecond() {
	static double freq = (double)sInitPreciseFreq();
	return freq;
}

double VDGetPreciseSecondsPerTick() {
	static double spt = 1.0 / (double)sInitPreciseFreq();
	return spt;
}

// VDGetAccurateTick and VDCallbackTimer were removed upstream in Altirra
// 4.50-test21 (nothing referenced them any more); the SDL3 backend drops
// them too so it keeps matching <vd2/system/time.h>.

// -------------------------------------------------------------------------
// VDLazyTimer
// -------------------------------------------------------------------------
//
// Lazy timers are drained cooperatively from the host's main loop rather
// than from a worker thread.  <vd2/system/time.h> documents the contract
// upstream relies on: a lazy timer callback runs on the main thread as
// part of the event loop, so mainline code and callback code need no
// synchronization.  Win32 gets that for free from SetTimer/WM_TIMER; this
// backend gets it by registering the callback in a process-wide list that
// VDLazyTimerTick() walks once per iteration of the host loop.
//
// An earlier version of this file used a detached std::thread per one-shot
// and a worker thread per periodic timer.  That broke the contract in two
// ways that matter: the callbacks (disk auto-flush, IDE flush, virtual
// folder file close) ran concurrently with the emulation thread that owns
// the same objects, and a detached one-shot could not be cancelled, so
// Stop() — including the one in ~VDLazyTimer — left a thread that would
// later call into a destroyed object.  Do not reintroduce threads here.
//
// Drain sites (each host loop must call VDLazyTimerTick() every iteration,
// or its lazy timers simply never fire):
//	 - src/AltirraSDL/source/app/main_sdl3.cpp   (desktop, Android, WASM)
//	 - src/AltirraBridgeServer/main_bridge.cpp   (headless bridge server)
//	 - src/AltirraLibretro/libretro.cpp          (retro_run; that target
//	   has its own copy of this scheduler in libretro_time.cpp)
// -------------------------------------------------------------------------

namespace {
	struct LazyTimerEntry {
		uint32                  mTimerId    = 0;   // matches VDLazyTimer::mTimerId
		uint32                  mPeriodMs   = 0;
		uint64                  mNextFireMs = 0;   // absolute ms
		bool                    mbPeriodic  = false;
		vdfunction<void()>      mFn;
	};

	// Scheduler state.  Deliberately immortal (allocated once, never
	// destroyed): VDLazyTimer objects can be reached from globals — the
	// simulator owns the disk interfaces, which own the auto-flush timers
	// — and those destructors run during static destruction in an order
	// that is not defined relative to this translation unit.  An immortal
	// state block means a late ~VDLazyTimer can always unregister safely.
	//
	// The mutex guards registration only; callbacks always run on the
	// thread that calls VDLazyTimerTick().  Registration from a non-main
	// thread is rare but legal, so the list stays locked.
	struct LazyTimerState {
		std::mutex                  mMutex;
		std::vector<LazyTimerEntry> mList;
		uint32                      mNextId = 1;
	};

	LazyTimerState& GetLazyTimerState() {
		static LazyTimerState *const state = new LazyTimerState;
		return *state;
	}

	uint32 LazyTimer_Register(const vdfunction<void()>& fn, uint32 delayMs, bool periodic) {
		LazyTimerState& st = GetLazyTimerState();
		std::lock_guard<std::mutex> lk(st.mMutex);

		LazyTimerEntry e;
		e.mTimerId    = st.mNextId++;
		e.mPeriodMs   = delayMs;
		e.mNextFireMs = VDGetCurrentTick64() + delayMs;
		e.mbPeriodic  = periodic;
		e.mFn         = fn;
		st.mList.push_back(std::move(e));
		return st.mList.back().mTimerId;
	}

	void LazyTimer_Unregister(uint32 id) {
		if (!id) return;

		LazyTimerState& st = GetLazyTimerState();
		std::lock_guard<std::mutex> lk(st.mMutex);
		st.mList.erase(
			std::remove_if(st.mList.begin(), st.mList.end(),
				[id](const LazyTimerEntry& e) { return e.mTimerId == id; }),
			st.mList.end());
	}
}

// Drain due timers.  Called once per host main-loop iteration.  Callbacks
// are invoked on copies of the entries and outside the lock, so a callback
// that stops itself, re-arms itself, or registers another timer can safely
// mutate the timer list.  Extern "C" linkage keeps the symbol addressable
// without pulling in a header just for this one call.
extern "C" void VDLazyTimerTick() {
	const uint64 now = VDGetCurrentTick64();
	LazyTimerState& st = GetLazyTimerState();

	std::vector<LazyTimerEntry> fireNow;
	{
		std::lock_guard<std::mutex> lk(st.mMutex);
		for (auto it = st.mList.begin(); it != st.mList.end(); ) {
			if (it->mNextFireMs <= now) {
				fireNow.push_back(*it);
				if (it->mbPeriodic) {
					it->mNextFireMs = now + it->mPeriodMs;
					++it;
				} else {
					it = st.mList.erase(it);
				}
			} else {
				++it;
			}
		}
	}

	for (const auto& e : fireNow) {
		if (e.mFn) e.mFn();
	}
}

VDLazyTimer::VDLazyTimer() {}

VDLazyTimer::~VDLazyTimer() {
	Stop();
}

void VDLazyTimer::SetOneShot(IVDTimerCallback *pCB, uint32 delay) {
	SetOneShotFn([=]() { pCB->TimerCallback(); }, delay);
}

void VDLazyTimer::SetOneShotFn(const vdfunction<void()>& fn, uint32 delay) {
	Stop();
	mpFn       = fn;
	mbPeriodic = false;
	mTimerId   = LazyTimer_Register(fn, delay, false);
}

void VDLazyTimer::SetPeriodic(IVDTimerCallback *pCB, uint32 delay) {
	SetPeriodicFn([=]() { pCB->TimerCallback(); }, delay);
}

void VDLazyTimer::SetPeriodicFn(const vdfunction<void()>& fn, uint32 delay) {
	Stop();
	mpFn       = fn;
	mbPeriodic = true;
	mTimerId   = LazyTimer_Register(fn, delay, true);
}

void VDLazyTimer::Stop() {
	if (mTimerId) {
		LazyTimer_Unregister(mTimerId);
		mTimerId = 0;
	}
}

// Unused outside Win32 (the header declares it for the Win32 timer proc).
void VDLazyTimer::StaticTimeCallback(VDZHWND, VDZUINT, VDZUINT_PTR, VDZDWORD) {}
