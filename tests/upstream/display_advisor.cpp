#include "../../src/AltirraSDL/source/display/display_advisor_policy.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
static void Check(bool value) { if (!value) std::abort(); }
int main() {
	ATDisplayAdvisorPolicy p;
	for (int i = 0; i < 1000; ++i) {
		Check(!p.Observe(1, 16.7, 16.7, 16.7, true, true));
		Check(!p.Observe(1, 33.4, 16.7, 33.4, true, true));
		Check(!p.Observe(1, 33.4, 16.7, 66.8, true, true));
		Check(!p.Observe(33.4, 1, 16.7, 66.8, true, true));
		Check(!p.Observe(1, 100, 16.7, -1, true, true)); // async WebGL
	}
	for (int i = 0; i < 300; ++i) Check(!p.Observe(50, 5, 16.7, 40, false, true));
	for (int i = 0; i < 300; ++i) Check(!p.Observe(50, 5, 16.7, 40, true, false));
	Check(!p.Observe(std::numeric_limits<double>::quiet_NaN(), 1, 16, 0, true, true));
	Check(!p.Observe(60, 1, 0, 0, true, true));
	for (int i = 0; i < 500; ++i) {
		Check(!p.Observe(300, 1, 16.7, 0, true, true)); // compilation spike
		Check(!p.Observe(1, 1, 16.7, 0, true, true));
	}
	bool offered = false;
	for (int i = 0; i < 240; ++i) offered |= p.Observe(50, 1, 16.7, -1, true, true);
	Check(offered && p.offered);
	for (int i = 0; i < 500; ++i) Check(!p.Observe(50, 1, 16.7, 0, true, true));
	p = {};
	for (int i = 0; i < 60; ++i) Check(!p.Observe(1, 90, 16.7, 40, true, true));
	bool native = false;
	for (int i = 0; i < 90; ++i) native |= p.Observe(1, 90, 16.7, 40, true, true);
	Check(native);
	p = {};
	for (int i = 0; i < 145; ++i) Check(!p.Observe(50, 1, 16.7, -1, true, true));
	Check(!p.Observe(50, 1, 16.7, -1, false, true));
	for (int i = 0; i < 149; ++i) Check(!p.Observe(50, 1, 16.7, -1, true, true));
	Check(p.Observe(50, 1, 16.7, -1, true, true));
	std::puts("Graphics advisor policy passed");
}
