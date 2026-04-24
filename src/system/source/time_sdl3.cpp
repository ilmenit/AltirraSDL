//	Altirra - Atari 800/800XL/5200 emulator
//	System library - timing functions for non-Windows (SDL3)

#include <stdafx.h>
#include <chrono>
#include <thread>

#include <vd2/system/vdtypes.h>
#include <vd2/system/time.h>
#include <vd2/system/thread.h>

#if defined(__EMSCRIPTEN__)
// WASM single-threaded timer scheduler — see the VDLazyTimer block at the
// bottom of this file.  Needs vector / mutex / function / algorithm.
#include <vector>
#include <mutex>
#include <functional>
#include <algorithm>
#endif

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

uint32 VDGetAccurateTick() {
	return VDGetCurrentTick();
}

// -------------------------------------------------------------------------
// VDCallbackTimer
// -------------------------------------------------------------------------

VDCallbackTimer::VDCallbackTimer()
	: mTimerAccuracy(0)
	, mpCB(nullptr)
	, mTimerPeriod(0)
	, mbExit(false)
	, mbPrecise(true)
{
}

VDCallbackTimer::~VDCallbackTimer() {
	Shutdown();
}

bool VDCallbackTimer::Init(IVDTimerCallback *pCB, uint32 period_ms) {
	return Init2(pCB, period_ms * 10000);
}

bool VDCallbackTimer::Init2(IVDTimerCallback *pCB, uint32 period_100ns) {
	return Init3(pCB, period_100ns, period_100ns >> 1, true);
}

bool VDCallbackTimer::Init3(IVDTimerCallback *pCB, uint32 period_100ns, uint32 /*accuracy_100ns*/, bool precise) {
	Shutdown();

	mpCB     = pCB;
	mbExit   = false;
	mbPrecise = precise;
	mTimerAccuracy  = 1;
	mTimerPeriod    = period_100ns;
	mTimerPeriodAdjustment = 0;
	mTimerPeriodDelta      = 0;

#if defined(__EMSCRIPTEN__)
	// WASM is single-threaded; VDCallbackTimer has no callers in the
	// AltirraSDL build tree (it's used by the Windows-only UI / native
	// audio pumps), so there's no scheduler to wire up.  Return false
	// with the accuracy reset so any hypothetical future caller gets a
	// clean "timer unavailable" signal instead of a silent no-op.
	mTimerAccuracy = 0;
	return false;
#else
	if (ThreadStart())
		return true;

	Shutdown();
	return false;
#endif
}

void VDCallbackTimer::Shutdown() {
	if (isThreadAttached()) {
		mbExit = true;
		msigExit.signal();
		ThreadWait();
	}
	mTimerAccuracy = 0;
}

void VDCallbackTimer::SetRateDelta(int delta_100ns) {
	mTimerPeriodDelta = delta_100ns;
}

void VDCallbackTimer::AdjustRate(int adjustment_100ns) {
	mTimerPeriodAdjustment += adjustment_100ns;
}

bool VDCallbackTimer::IsTimerRunning() const {
	return mTimerAccuracy != 0;
}

void VDCallbackTimer::ThreadRun() {
	using namespace std::chrono;

	auto period_ns = nanoseconds((uint64)mTimerPeriod * 100);
	auto next = steady_clock::now() + period_ns;
	const auto maxDelay = period_ns * 2;

	while (!mbExit) {
		auto now = steady_clock::now();
		auto remaining = next - now;

		if (remaining > nanoseconds(0)) {
			uint32 ms = (uint32)(duration_cast<milliseconds>(remaining).count() + 1);
			msigExit.tryWait(ms);
		}

		if (mbExit)
			break;

		mpCB->TimerCallback();

		int adjust   = mTimerPeriodAdjustment.xchg(0);
		int perdelta = mTimerPeriodDelta;
		uint64 ep = (uint64)mTimerPeriod + adjust + perdelta;
		period_ns = nanoseconds(ep * 100);
		next += period_ns;

		// drift guard
		auto late = steady_clock::now() - next;
		if (late > maxDelay)
			next = steady_clock::now() + period_ns;
	}
}

// -------------------------------------------------------------------------
// VDLazyTimer
// -------------------------------------------------------------------------

VDLazyTimer::VDLazyTimer() {}

VDLazyTimer::~VDLazyTimer() {
	Stop();
}

#if defined(__EMSCRIPTEN__)

// -------------------------------------------------------------------------
// WASM single-threaded VDLazyTimer
//
// The native backend uses detached std::threads that sleep_for(ms) and
// then fire the callback.  That cannot work in a browser build without
// -pthread, and even with pthreads it would be overkill: VDLazyTimer
// callbacks are infrequent, non-real-time things like "close this
// virtual-disk file N ms after the last access" or "pulse a UI status
// line".
//
// Under WASM we instead register the delay + callback in a process-
// wide list.  The main loop (main_sdl3.cpp) calls VDWASMTimerTick()
// once per frame; it walks the list and fires any due callbacks
// inline.  Periodic timers rearm themselves after firing.
//
// This scheduler is strictly single-threaded and all accesses happen
// on the main thread — the std::mutex below is belt-and-suspenders
// for any hypothetical future concurrent access and compiles to a
// no-op in practice.
// -------------------------------------------------------------------------

namespace {
	struct WASMTimerEntry {
		uint32                  mTimerId    = 0;   // matches VDLazyTimer::mTimerId
		uint32                  mPeriodMs   = 0;
		uint64                  mNextFireMs = 0;   // absolute ms
		bool                    mbPeriodic  = false;
		vdfunction<void()>      mFn;
	};

	std::mutex                          g_wasmTimerMutex;
	std::vector<WASMTimerEntry>         g_wasmTimerList;
	uint32                              g_wasmTimerNextId = 1;

	uint32 WASMTimer_Register(const vdfunction<void()>& fn, uint32 delayMs, bool periodic) {
		std::lock_guard<std::mutex> lk(g_wasmTimerMutex);
		WASMTimerEntry e;
		e.mTimerId    = g_wasmTimerNextId++;
		e.mPeriodMs   = delayMs;
		e.mNextFireMs = (uint64)VDGetCurrentTick64() + delayMs;
		e.mbPeriodic  = periodic;
		e.mFn         = fn;
		g_wasmTimerList.push_back(std::move(e));
		return e.mTimerId;
	}

	void WASMTimer_Unregister(uint32 id) {
		if (!id) return;
		std::lock_guard<std::mutex> lk(g_wasmTimerMutex);
		g_wasmTimerList.erase(
			std::remove_if(g_wasmTimerList.begin(), g_wasmTimerList.end(),
				[id](const WASMTimerEntry& e) { return e.mTimerId == id; }),
			g_wasmTimerList.end());
	}
}

// Drain due timers.  Called once per main-loop tick from main_sdl3.cpp
// under __EMSCRIPTEN__.  Callback invocation is done on a copy of the
// entry so that a callback which re-registers or stops itself doesn't
// invalidate the iteration.  Extern "C" linkage keeps the symbol
// addressable from the wider build without pulling a header in just
// for this one call.
extern "C" void VDWASMTimerTick() {
	const uint64 now = VDGetCurrentTick64();

	// Snapshot the list under the lock, then invoke each callback
	// unlocked so a callback that calls Stop() / another SetOneShot
	// can safely mutate g_wasmTimerList.
	std::vector<WASMTimerEntry> fireNow;
	{
		std::lock_guard<std::mutex> lk(g_wasmTimerMutex);
		for (auto it = g_wasmTimerList.begin(); it != g_wasmTimerList.end(); ) {
			if (it->mNextFireMs <= now) {
				fireNow.push_back(*it);
				if (it->mbPeriodic) {
					it->mNextFireMs = now + it->mPeriodMs;
					++it;
				} else {
					it = g_wasmTimerList.erase(it);
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

void VDLazyTimer::SetOneShot(IVDTimerCallback *pCB, uint32 delay) {
	SetOneShotFn([=]() { pCB->TimerCallback(); }, delay);
}

void VDLazyTimer::SetOneShotFn(const vdfunction<void()>& fn, uint32 delay) {
	Stop();
	mpFn       = fn;
	mbPeriodic = false;
	mTimerId   = WASMTimer_Register(fn, delay, false);
}

void VDLazyTimer::SetPeriodic(IVDTimerCallback *pCB, uint32 delay) {
	SetPeriodicFn([=]() { pCB->TimerCallback(); }, delay);
}

void VDLazyTimer::SetPeriodicFn(const vdfunction<void()>& fn, uint32 delay) {
	Stop();
	mpFn       = fn;
	mbPeriodic = true;
	mTimerId   = WASMTimer_Register(fn, delay, true);
}

void VDLazyTimer::Stop() {
	if (mTimerId) {
		WASMTimer_Unregister(mTimerId);
		mTimerId = 0;
	}
	// mTimerThread / mpTimerRunning are not used on the WASM backend —
	// they remain default-constructed.
}

// Unused on WASM (the header provides it for the native backends that
// route through a Win32 timer proc).  Empty stub keeps the vtable happy.
void VDLazyTimer::StaticTimeCallback(VDZHWND, VDZUINT, VDZUINT_PTR, VDZDWORD) {}

#else // !__EMSCRIPTEN__

void VDLazyTimer::SetOneShot(IVDTimerCallback *pCB, uint32 delay) {
	SetOneShotFn([=]() { pCB->TimerCallback(); }, delay);
}

void VDLazyTimer::SetOneShotFn(const vdfunction<void()>& fn, uint32 delay) {
	Stop();
	mpFn = fn;
	mbPeriodic = false;
	mTimerId = 1;

	vdfunction<void()> f = fn;
	std::thread([f, delay]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(delay));
		f();
	}).detach();
}

void VDLazyTimer::SetPeriodic(IVDTimerCallback *pCB, uint32 delay) {
	SetPeriodicFn([=]() { pCB->TimerCallback(); }, delay);
}

void VDLazyTimer::SetPeriodicFn(const vdfunction<void()>& fn, uint32 delay) {
	Stop();
	mpFn = fn;
	mbPeriodic = true;
	mTimerId = 1;

	auto running = std::make_shared<std::atomic<bool>>(true);
	mpTimerRunning = running;

	uint32 ms = delay;
	vdfunction<void()> f = fn;
	mTimerThread = std::thread([running, f, ms]() {
		while (running->load(std::memory_order_relaxed)) {
			std::this_thread::sleep_for(std::chrono::milliseconds(ms));
			if (!running->load(std::memory_order_relaxed))
				break;
			f();
		}
	});
}

void VDLazyTimer::Stop() {
	if (mpTimerRunning)
		mpTimerRunning->store(false, std::memory_order_release);

	if (mTimerThread.joinable()) {
		if (mTimerThread.get_id() == std::this_thread::get_id()) {
			// Called from the timer callback itself (e.g. OnFlushTimerFire).
			// Cannot join — detach and let the thread exit naturally after
			// the callback returns.  The shared_ptr<atomic<bool>> captured
			// by the thread keeps the running flag alive even if this
			// VDLazyTimer is destroyed before the thread finishes.
			mTimerThread.detach();
		} else {
			mTimerThread.join();
		}
	}

	mpTimerRunning.reset();
	mTimerId = 0;
}

// StaticTimeCallback is unused on non-Windows
void VDLazyTimer::StaticTimeCallback(VDZHWND, VDZUINT, VDZUINT_PTR, VDZDWORD) {}

#endif // __EMSCRIPTEN__
