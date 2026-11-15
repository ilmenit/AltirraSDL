#pragma once
#include <algorithm>
#include <cmath>

// Observe only rendering, never emulation, sleeping or total frame intervals.
struct ATDisplayAdvisorPolicy {
	unsigned warmup = 60;
	unsigned slow = 0;
	double slowTime = 0;
	bool offered = false;

	void ResetWindow() { warmup = 60; slow = 0; slowTime = 0; }
	bool Observe(double work, double present, double budget, double waitAllowance,
		bool eligible, bool optionalGraphics)
	{
		if (offered) return false;
		if (!eligible || !optionalGraphics || !std::isfinite(work)
			|| !std::isfinite(present) || !std::isfinite(budget) || budget <= 0
			|| work < 0 || present < 0) {
			ResetWindow(); return false;
		}
		if (warmup) { --warmup; return false; }
		// Allow the configured refresh interval (also for compositor waits).
		// A negative allowance means presentation timing is untrustworthy
		// (including WebGL). In that case only expensive submission counts.
		const bool knownWait = waitAllowance >= 0 && std::isfinite(waitAllowance);
		const bool expensive = knownWait
			? work + present > waitAllowance + budget * 0.75
			: work > budget * 1.25;
		// Discard isolated compilation, resize and scheduling spikes.
		if (!expensive || work + present > 250) {
			slow = 0; slowTime = 0; return false;
		}
		++slow; slowTime += work + present;
		if (slow >= 90 && slowTime >= 3000) {
			offered = true; return true;
		}
		return false;
	}
};
