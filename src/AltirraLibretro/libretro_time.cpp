// Altirra libretro core - standalone timer replacement TU.
//
// This provides the system timing symbols needed by the emulator without
// pulling the SDL3-oriented system timer object out of libsystem.a.

#include <stdafx.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include <vd2/system/function.h>
#include <vd2/system/thread.h>
#include <vd2/system/time.h>
#include <vd2/system/vdtypes.h>

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

uint32 VDGetAccurateTick() {
	return VDGetCurrentTick();
}

VDCallbackTimer::VDCallbackTimer()
	: mpCB(nullptr)
	, mTimerAccuracy(0)
	, mTimerPeriod(0)
	, mTimerPeriodDelta(0)
	, mTimerPeriodAdjustment(0)
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

bool VDCallbackTimer::Init3(IVDTimerCallback *pCB, uint32 period_100ns,
	uint32, bool precise)
{
	Shutdown();

	mpCB = pCB;
	mTimerAccuracy = 1;
	mTimerPeriod = period_100ns;
	mTimerPeriodDelta = 0;
	mTimerPeriodAdjustment = 0;
	mbExit = false;
	mbPrecise = precise;

	if (ThreadStart())
		return true;

	Shutdown();
	return false;
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

	auto periodNs = nanoseconds((uint64)mTimerPeriod * 100);
	auto next = steady_clock::now() + periodNs;
	const auto maxDelay = periodNs * 2;

	while (!mbExit) {
		const auto now = steady_clock::now();
		const auto remaining = next - now;

		if (remaining > nanoseconds(0)) {
			const uint32 ms = (uint32)(
				duration_cast<milliseconds>(remaining).count() + 1);
			msigExit.tryWait(ms);
		}

		if (mbExit)
			break;

		if (mpCB)
			mpCB->TimerCallback();

		const int adjust = mTimerPeriodAdjustment.xchg(0);
		const int perdelta = mTimerPeriodDelta;
		const uint64 ep = (uint64)mTimerPeriod + adjust + perdelta;
		periodNs = nanoseconds(ep * 100);
		next += periodNs;

		const auto late = steady_clock::now() - next;
		if (late > maxDelay)
			next = steady_clock::now() + periodNs;
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
	mTimerId = 1;

	auto running = std::make_shared<std::atomic<bool>>(true);
	mpTimerRunning = running;

	vdfunction<void()> f = fn;
	mTimerThread = std::thread([running, f, delay]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(delay));
		if (running->load(std::memory_order_acquire))
			f();
	});
	mTimerThread.detach();
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

	vdfunction<void()> f = fn;
	mTimerThread = std::thread([running, f, delay]() {
		while (running->load(std::memory_order_acquire)) {
			std::this_thread::sleep_for(std::chrono::milliseconds(delay));
			if (!running->load(std::memory_order_acquire))
				break;
			f();
		}
	});
}

void VDLazyTimer::Stop() {
	if (mpTimerRunning)
		mpTimerRunning->store(false, std::memory_order_release);

	if (mTimerThread.joinable()) {
		if (mTimerThread.get_id() == std::this_thread::get_id())
			mTimerThread.detach();
		else
			mTimerThread.join();
	}

	mpTimerRunning.reset();
	mTimerId = 0;
}

void VDLazyTimer::StaticTimeCallback(VDZHWND, VDZUINT, VDZUINT_PTR,
	VDZDWORD)
{
}
