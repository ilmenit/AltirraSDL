#pragma once
#include <algorithm>

// Pure policy, separate from registry, graphics APIs and platform UI.
struct ATDisplayRecoveryPolicy {
	int interruptions = 0;
	int errors = 0;
	bool notice = false;
	bool failed = false;
	bool settled = false;

	void Load(int previous, bool pending) {
		interruptions = std::clamp(previous, 0, 3);
		if (pending) interruptions = std::min(3, interruptions + 1);
	}
	bool RecommendRecovery() const { return interruptions >= 2; }
	void Observe(double milliseconds, bool healthy) {
		if (!healthy) {
			errors = std::min(3, errors + 1);
			if (errors >= 3 && !failed) { failed = true; notice = true; }
			return;
		}
		errors = 0;
		if (!settled && milliseconds >= 5000) notice = true;
	}
};
