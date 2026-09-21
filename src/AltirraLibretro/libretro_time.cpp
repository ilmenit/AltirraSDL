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

// VDGetAccurateTick and VDCallbackTimer were removed upstream in Altirra
// 4.50-test21 (nothing referenced them any more); this backend drops them
// too so it keeps matching <vd2/system/time.h>.

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
