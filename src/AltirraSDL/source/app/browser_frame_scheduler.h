// AltirraSDL - wall-clock pacing within a bounded browser animation tick.
#pragma once

#include <algorithm>
#include <cmath>

class ATBrowserFrameScheduler {
public:
	void Suspend(double now) {
		mLastTime = now;
		mAccumulated = 0;
		mInitialized = true;
	}

	void BeginTick(double now, double period, bool turbo) {
		if (!std::isfinite(period) || period <= 0)
			period = 1.0 / 60.0;
		if (!mInitialized || mPeriod != period || mTurbo != turbo) {
			Suspend(now);
			mPeriod = period;
			mTurbo = turbo;
			return;
		}
		mAccumulated += std::clamp(now - mLastTime, 0.0, 0.25);
		mLastTime = now;
	}

	bool IsFrameDue() const {
		return mTurbo || mAccumulated + 1e-10 >= mPeriod;
	}

	void OnFrameCompleted() {
		// Keep credit until a frame finishes: exhausting the execution
		// budget mid-frame must not charge for that frame a second time.
		mAccumulated = mTurbo ? 0.0
			: std::max(0.0, mAccumulated - mPeriod);
	}

	void OnBudgetExhausted() {
		// A slow machine must remain responsive instead of accumulating
		// an ever-growing catch-up workload. Keep the current frame due.
		mAccumulated = std::min(mAccumulated, mPeriod);
	}

private:
	double mLastTime = 0;
	double mAccumulated = 0;
	double mPeriod = 1.0 / 60.0;
	bool mInitialized = false;
	bool mTurbo = false;
};
