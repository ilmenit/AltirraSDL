// The production browser scheduler, tested against a simulated refresh clock.
#include "browser_frame_scheduler.h"
#include <cstdio>
#include <cstdlib>

static void Check(bool value, const char *message) {
	if (!value) {
		std::fprintf(stderr, "%s\n", message);
		std::exit(1);
	}
}

static int CountFrames(double refresh, double rate) {
	ATBrowserFrameScheduler scheduler;
	const double period = 1.0 / (60.0 * rate);
	int frames = 0;
	for (int tick = 0; tick <= (int)(refresh * 10); ++tick) {
		scheduler.BeginTick(tick / refresh, period, false);
		while (scheduler.IsFrameDue()) {
			++frames;
			scheduler.OnFrameCompleted();
		}
	}
	return frames;
}

int main() {
	for (double refresh : {30.0, 60.0, 120.0, 144.0}) {
		for (double rate : {0.01, 0.25, 0.37, 1.0, 2.0, 5.0, 8.0}) {
			const int expected = (int)std::round(600 * rate);
			Check(std::abs(CountFrames(refresh, rate) - expected) <= 1,
				"Requested speed is limited by browser refresh");
		}
	}
	ATBrowserFrameScheduler scheduler;
	scheduler.BeginTick(0, 0.01, false);
	scheduler.BeginTick(0.01, 0.01, false);
	Check(scheduler.IsFrameDue(), "First frame not due");
	// Emulation runs out of its time budget halfway through this frame.
	scheduler.OnBudgetExhausted();
	scheduler.BeginTick(0.011, 0.01, false);
	Check(scheduler.IsFrameDue(), "Partial frame charged twice");
	scheduler.OnFrameCompleted();
	Check(!scheduler.IsFrameDue(), "Completed partial frame charged incorrectly");
	scheduler.Suspend(10);
	scheduler.BeginTick(10.001, 0.01, false);
	Check(!scheduler.IsFrameDue(), "Pause accumulated a catch-up burst");
	scheduler.BeginTick(10.002, 1.0, false);
	Check(!scheduler.IsFrameDue(), "Speed change retained old pacing debt");
	scheduler.BeginTick(11.002, 1.0, true);
	Check(scheduler.IsFrameDue(), "Turbo still waits for wall-clock credit");
	scheduler.OnFrameCompleted();
	Check(scheduler.IsFrameDue(), "Turbo stopped after one frame");
	scheduler.BeginTick(11.003, 0.01, false);
	Check(!scheduler.IsFrameDue(), "Leaving turbo retained stale timing debt");
	scheduler.BeginTick(1000, 0.01, false);
	scheduler.OnBudgetExhausted();
	scheduler.OnFrameCompleted();
	Check(!scheduler.IsFrameDue(), "Budget limit retained an unbounded backlog");
}
