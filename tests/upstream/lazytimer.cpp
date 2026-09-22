// Contract test for the fork's cooperative VDLazyTimer backend.
//
// <vd2/system/time.h> states that lazy timer callbacks are invoked on the
// main thread as part of the event loop. On Win32 that comes from
// SetTimer/WM_TIMER; on every other platform it comes from the scheduler in
// src/system/source/time_sdl3.cpp, which only dispatches when the host loop
// calls VDLazyTimerTick().
//
// The properties below are the ones the emulator actually depends on:
// disk auto-flush (ATDiskInterface::OnFlushTimerFire) re-arms a periodic
// timer and stops it from inside its own callback, IDE flush and the
// virtual-folder file close use one-shots captured on `this`, and all of
// them must be cancelled by Stop()/~VDLazyTimer so a timer cannot outlive
// the object that owns it.
//
// This test is built only for the non-Win32 backends, since it is the
// cooperative scheduler it pins down.

#include <stdafx.h>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <vd2/system/time.h>

extern "C" void VDLazyTimerTick();

namespace {
	int g_failures = 0;

	void Check(bool ok, const char *message) {
		if (!ok) {
			fprintf(stderr, "FAIL: %s\n", message);
			++g_failures;
		}
	}

	// Sleep past a timer deadline, then drain. Real time is used because
	// the scheduler reads VDGetCurrentTick64(); the margins are generous
	// so a loaded CI machine cannot flake the test.
	void SleepMs(uint32 ms) {
		std::this_thread::sleep_for(std::chrono::milliseconds(ms));
	}

	void TestOneShotFiresOnceAfterDelay() {
		int count = 0;
		VDLazyTimer t;
		t.SetOneShotFn([&count] { ++count; }, 30);

		VDLazyTimerTick();
		Check(count == 0, "one-shot fired before its delay elapsed");

		SleepMs(60);
		VDLazyTimerTick();
		Check(count == 1, "one-shot did not fire after its delay elapsed");

		SleepMs(60);
		VDLazyTimerTick();
		Check(count == 1, "one-shot fired more than once");
	}

	void TestOneShotDoesNotFireWithoutATick() {
		int count = 0;
		VDLazyTimer t;
		t.SetOneShotFn([&count] { ++count; }, 10);

		SleepMs(40);
		Check(count == 0, "callback ran without a VDLazyTimerTick() call");

		VDLazyTimerTick();
		Check(count == 1, "callback did not run on the first tick after the delay");
	}

	void TestStopCancels() {
		int count = 0;
		VDLazyTimer t;
		t.SetOneShotFn([&count] { ++count; }, 10);
		t.Stop();

		SleepMs(40);
		VDLazyTimerTick();
		Check(count == 0, "Stop() did not cancel a pending one-shot");
	}

	void TestDestructorCancels() {
		int count = 0;
		{
			VDLazyTimer t;
			t.SetOneShotFn([&count] { ++count; }, 10);
		}

		SleepMs(40);
		VDLazyTimerTick();
		Check(count == 0, "~VDLazyTimer did not cancel a pending one-shot");
	}

	// The shape ATIDEEmulator and ATDiskImageVirtualFolder use: a callback
	// that captures the owning object. If the timer outlived the object
	// this would be a use-after-free, so the value must stay untouched.
	struct Owner {
		int mFireCount = 0;
		VDLazyTimer mTimer;

		void Arm() { mTimer.SetOneShotFn([this] { ++mFireCount; }, 10); }
	};

	void TestOwnerDestructionCancels() {
		auto *owner = new Owner;
		owner->Arm();
		delete owner;

		SleepMs(40);
		VDLazyTimerTick();
		// Nothing to assert beyond not crashing / not writing into freed
		// memory; ASan builds of this test are where that pays off.
		Check(true, "unreachable");
	}

	void TestPeriodicRepeats() {
		int count = 0;
		VDLazyTimer t;
		t.SetPeriodicFn([&count] { ++count; }, 20);

		for (int i = 0; i < 3; ++i) {
			SleepMs(40);
			VDLazyTimerTick();
		}

		Check(count >= 3, "periodic timer did not repeat");

		t.Stop();
		const int afterStop = count;

		SleepMs(40);
		VDLazyTimerTick();
		Check(count == afterStop, "periodic timer kept firing after Stop()");
	}

	// ATDiskInterface::OnFlushTimerFire calls Stop() on its own timer from
	// inside the callback, and the virtual disk images re-arm a one-shot
	// from inside theirs. Both must be safe while the scheduler is walking
	// its list.
	void TestCallbackMayStopItself() {
		int count = 0;
		VDLazyTimer t;
		t.SetPeriodicFn([&count, &t] { ++count; t.Stop(); }, 10);

		SleepMs(30);
		VDLazyTimerTick();
		Check(count == 1, "self-stopping periodic callback did not run once");

		SleepMs(30);
		VDLazyTimerTick();
		Check(count == 1, "self-stopping periodic callback ran again");
	}

	void TestCallbackMayRearmItself() {
		int count = 0;
		VDLazyTimer t;
		vdfunction<void()> arm;
		arm = [&] {
			++count;
			if (count < 3)
				t.SetOneShotFn(arm, 10);
		};
		t.SetOneShotFn(arm, 10);

		for (int i = 0; i < 4; ++i) {
			SleepMs(30);
			VDLazyTimerTick();
		}

		Check(count == 3, "re-arming one-shot did not run the expected number of times");
	}

	// The scheduler must dispatch on the draining thread, never on one of
	// its own — that is the whole point of the contract.
	void TestCallbackRunsOnTickingThread() {
		std::thread::id cbThread{};
		VDLazyTimer t;
		t.SetOneShotFn([&cbThread] { cbThread = std::this_thread::get_id(); }, 10);

		SleepMs(40);
		VDLazyTimerTick();
		Check(cbThread == std::this_thread::get_id(),
			"callback did not run on the thread that called VDLazyTimerTick()");
	}
}

int main() {
	TestOneShotFiresOnceAfterDelay();
	TestOneShotDoesNotFireWithoutATick();
	TestStopCancels();
	TestDestructorCancels();
	TestOwnerDestructionCancels();
	TestPeriodicRepeats();
	TestCallbackMayStopItself();
	TestCallbackMayRearmItself();
	TestCallbackRunsOnTickingThread();

	if (g_failures) {
		fprintf(stderr, "%d lazy timer contract check(s) failed\n", g_failures);
		return 1;
	}

	printf("lazy timer contract tests passed\n");
	return 0;
}
