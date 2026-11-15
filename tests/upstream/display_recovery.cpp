#include "../../src/AltirraSDL/source/display/display_recovery_policy.h"
#include <cstdio>
#include <cstdlib>
static void Check(bool value) { if (!value) std::abort(); }
int main() {
	ATDisplayRecoveryPolicy p;
	p.Load(0, false); Check(!p.RecommendRecovery());
	p.Observe(4999, true); Check(!p.notice);
	p.Observe(5000, true); Check(p.notice && !p.failed);
	p.notice = false; p.settled = true;
	p.Observe(60000, true); Check(!p.notice);
	p.Load(0, true); Check(!p.RecommendRecovery());
	p.Load(p.interruptions, true); Check(p.RecommendRecovery());
	p.Load(-100, false); Check(!p.RecommendRecovery());
	p.Load(100, true); Check(p.interruptions == 3);
	p = {};
	p.Observe(0, false); p.Observe(0, false); Check(!p.failed);
	p.Observe(0, true); p.Observe(0, false); Check(!p.failed);
	p.Observe(0, false); p.Observe(0, false); Check(p.failed && p.notice);
	p.notice = false; p.Observe(0, false); Check(!p.notice);
	std::puts("Graphics recovery policy passed");
}
