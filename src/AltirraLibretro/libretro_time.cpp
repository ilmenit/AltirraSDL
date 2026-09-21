// Altirra libretro core - standalone timer replacement TU.
//
// This provides the system timing symbols needed by the emulator without
// pulling the SDL3-oriented system timer object out of libsystem.a.

#include <stdafx.h>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <vector>

#include <vd2/system/function.h>
#include <vd2/system/thread.h>
#include <vd2/system/time.h>
#include <vd2/system/vdtypes.h>

#include "libretro_common.h"

namespace {
	std::chrono::steady_clock::time_point ATLibretroTimerStart() {
		static const auto start = std::chrono::steady_clock::now();
		return start;
	}
}

uint32 VDGetCurrentTick() {
	using namespace std::chrono;
	return (uint32)duration_cast<milliseconds>(
		steady_clock::now() - ATLibretroTimerStart()).count();
}

uint64 VDGetCurrentTick64() {
	using namespace std::chrono;
	return (uint64)duration_cast<milliseconds>(
		steady_clock::now() - ATLibretroTimerStart()).count();
}

uint64 VDGetPreciseTick() {
	using namespace std::chrono;
	return (uint64)duration_cast<nanoseconds>(
		steady_clock::now() - ATLibretroTimerStart()).count();
}

uint64 VDGetPreciseTicksPerSecondI() {
	return 1000000000ULL;
}

double VDGetPreciseTicksPerSecond() {
	return 1000000000.0;
}

double VDGetPreciseSecondsPerTick() {
	return 1.0 / 1000000000.0;
}

// VDGetAccurateTick and VDCallbackTimer were removed upstream in Altirra
// 4.50-test21 (nothing referenced them any more); this backend drops them
// too so it keeps matching <vd2/system/time.h>.

// -------------------------------------------------------------------------
// VDLazyTimer
// -------------------------------------------------------------------------
//
// Same cooperative scheduler as src/system/source/time_sdl3.cpp (this
// target deliberately does not link libsystem's timer TU, so the code is
// duplicated rather than shared).  <vd2/system/time.h> requires lazy timer
// callbacks to run on the main thread as part of the host loop, which for
// the libretro core means retro_run: ATLibretroLazyTimerTick() is called
// once per retro_run, and lazy timers do not fire while the frontend has
// the core paused — matching what a message loop does on Win32.
//
// Callbacks used to run on detached worker threads here, which raced the
// emulation thread over the disk image during auto-flush and could not be
// cancelled by Stop().  Do not reintroduce threads.
// -------------------------------------------------------------------------

namespace {
	struct LazyTimerEntry {
		uint32             mTimerId    = 0;
		uint32             mPeriodMs   = 0;
		uint64             mNextFireMs = 0;
		bool               mbPeriodic  = false;
		vdfunction<void()> mFn;
	};

	// Immortal scheduler state — same reasoning as the SDL3 backend: the
	// simulator is a global and owns disk interfaces that own flush
	// timers, so ~VDLazyTimer can run after this TU's statics would have
	// been destroyed.
	struct LazyTimerState {
		std::mutex                  mMutex;
		std::vector<LazyTimerEntry> mList;
		uint32                      mNextId = 1;
	};

	LazyTimerState& GetLazyTimerState() {
		static LazyTimerState *const state = new LazyTimerState;
		return *state;
	}

	uint32 LazyTimer_Register(const vdfunction<void()>& fn, uint32 delayMs,
		bool periodic)
	{
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
		if (!id)
			return;

		LazyTimerState& st = GetLazyTimerState();
		std::lock_guard<std::mutex> lk(st.mMutex);
		st.mList.erase(
			std::remove_if(st.mList.begin(), st.mList.end(),
				[id](const LazyTimerEntry& e) { return e.mTimerId == id; }),
			st.mList.end());
	}
}

// Drains due timers; called once per retro_run.  Callbacks run on copies of
// the entries and outside the lock so a callback may stop, re-arm, or add
// timers.
void ATLibretroLazyTimerTick() {
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
		if (e.mFn)
			e.mFn();
	}
}

VDLazyTimer::VDLazyTimer() {
}

VDLazyTimer::~VDLazyTimer() {
	Stop();
}

void VDLazyTimer::SetOneShot(IVDTimerCallback *pCB, uint32 delay) {
	SetOneShotFn([=]() { pCB->TimerCallback(); }, delay);
}

void VDLazyTimer::SetOneShotFn(const vdfunction<void()>& fn, uint32 delay) {
	Stop();
	mpFn = fn;
	mbPeriodic = false;
	mTimerId = LazyTimer_Register(fn, delay, false);
}

void VDLazyTimer::SetPeriodic(IVDTimerCallback *pCB, uint32 delay) {
	SetPeriodicFn([=]() { pCB->TimerCallback(); }, delay);
}

void VDLazyTimer::SetPeriodicFn(const vdfunction<void()>& fn, uint32 delay) {
	Stop();
	mpFn = fn;
	mbPeriodic = true;
	mTimerId = LazyTimer_Register(fn, delay, true);
}

void VDLazyTimer::Stop() {
	if (mTimerId) {
		LazyTimer_Unregister(mTimerId);
		mTimerId = 0;
	}
}

void VDLazyTimer::StaticTimeCallback(VDZHWND, VDZUINT, VDZUINT_PTR,
	VDZDWORD)
{
}
