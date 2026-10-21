// Seeded transport-independent scheduling benchmark. See tests/netplay/benchmark.py.
// Synthetic state intentionally excludes emulator checkpoint/rendering cost.
#include <lockstep.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <queue>
#include <vector>

using namespace ATNetplay;

namespace {
struct Peer {
	LockstepLoop loop;
	double due = 0;
	double nextSend = 0;
	unsigned stalls = 0;
	unsigned replay = 0;
	unsigned frames = 0;
	unsigned state = 1;
	std::vector<unsigned> states;
	std::vector<double> captured;
	double latency = 0;
	unsigned samples = 0;
};

struct Delivery {
	double due;
	unsigned serial;
	int to;
	NetInputPacket packet;

	bool operator<(const Delivery& other) const {
		return due > other.due ||
			(due == other.due && serial > other.serial);
	}
};

unsigned rng = 0x12345678;
unsigned NextRandom() {
	rng ^= rng << 13;
	rng ^= rng >> 17;
	return rng ^= rng << 5;
}

unsigned Advance(Peer& peer) {
	NetInput a, b;
	peer.loop.GetInputsForCurrentFrame(a, b);
	unsigned hash = (peer.state ^ a.stickDir ^ (b.stickDir << 8))
		* 16777619u;
	return hash ? hash : 1;
}

void Benchmark(unsigned fps, unsigned rtt, bool rollback) {
#ifdef OLD
	const char* name = "old-delay4";
#else
	const char* name = rollback ? "hybrid-delay2-window8" : "fixed-delay4";
#endif
	Peer peers[2];
	const double period = 1000.0 / fps;
	const unsigned delay = rollback ? 2 : 4;
	for (int i = 0; i < 2; ++i) {
#ifndef OLD
		peers[i].loop.SetRollbackWindow(rollback ? 8 : 0);
		peers[i].loop.SetFramePeriodUs(1000000 / fps);
#endif
		peers[i].loop.Begin(i ? Slot::Joiner : Slot::Host, delay);
		peers[i].states.push_back(1);
		peers[i].captured.resize(10000, -1);
	}

	std::priority_queue<Delivery> queue;
	rng = 0x12345678;
	unsigned serial = 0;
	bool diverged = false;
	unsigned divergenceFrame = 0;
	auto send = [&](int from, double now) {
		NetInputPacket packet;
		auto emit = [&] {
			if (NextRandom() % 100 < 1) return; // independent 1% loss
			const double jitter = (int)(NextRandom() % 21) - 10;
			queue.push({now + rtt / 2.0 + jitter,
				serial++, 1 - from, packet});
		};
		peers[from].loop.BuildOutgoingInputPacket(packet);
		emit();
#ifndef OLD
		if (peers[from].loop.BuildRepairInputPacket(packet)) emit();
#endif
	};

	const auto start = std::chrono::steady_clock::now();
	// One-millisecond virtual transport clock, thirty seconds per case.
	for (unsigned tick = 0; tick < 30000; ++tick) {
		while (!queue.empty() && queue.top().due <= tick) {
			const auto delivery = queue.top();
			queue.pop();
			peers[delivery.to].loop.OnPeerInputPacket(
				delivery.packet, tick + 1);
		}
		for (int i = 0; i < 2; ++i) {
			auto& peer = peers[i];
#ifndef OLD
			if (peer.loop.RollbackFrame() >= 0) {
				const unsigned target = peer.loop.CurrentFrame();
				const unsigned back = (unsigned)peer.loop.RollbackFrame();
				if (!peer.loop.RewindTo(back)) {
					diverged = true;
					break;
				}
				peer.state = peer.states[back];
				while (peer.loop.CurrentFrame() != target) {
					const unsigned frame = peer.loop.CurrentFrame();
					if (!peer.loop.CanAdvance()) {
						diverged = true;
						break;
					}
					peer.state = Advance(peer);
					peer.states[frame + 1] = peer.state;
					peer.loop.OnFrameAdvanced(peer.state);
					++peer.replay;
				}
				if (diverged) break;
			}
#endif
			if (peer.loop.IsDesynced()) {
				diverged = true;
				divergenceFrame = (unsigned)peer.loop.DesyncFrame();
				break;
			}
			if (tick >= peer.nextSend) {
				send(i, tick);
				peer.nextSend = tick + period;
			}
			if (tick < peer.due) continue;
			const unsigned frame = peer.loop.CurrentFrame();
			bool capture = true;
#ifndef OLD
			capture = peer.loop.NeedsLocalInput();
#endif
			if (capture) {
				NetInput input{};
				input.stickDir = ((tick / 67) * (i ? 7 : 3)) % 15;
				const unsigned key = frame + peer.loop.InputDelay();
				if (key < peer.captured.size()) peer.captured[key] = tick;
				peer.loop.SubmitLocalInput(input);
			}
			if (peer.loop.CanAdvance()) {
				peer.state = Advance(peer);
				if (peer.states.size() <= frame + 1)
					peer.states.resize(frame + 2);
				peer.states[frame + 1] = peer.state;
				peer.loop.OnFrameAdvanced(peer.state);
				++peer.frames;
				send(i, tick);
				peer.nextSend = tick + period;
				if (frame < peer.captured.size() && peer.captured[frame] >= 0) {
					peer.latency += tick - peer.captured[frame];
					++peer.samples;
				}
				peer.due += period;
			} else {
				++peer.stalls;
				peer.due = tick + 1;
			}
		}
		if (diverged) break;
	}
	const auto microseconds = std::chrono::duration_cast<
		std::chrono::microseconds>(
			std::chrono::steady_clock::now() - start).count();
#ifndef OLD
	const unsigned agreed = std::min(peers[0].loop.ConfirmedFrame(),
		peers[1].loop.ConfirmedFrame());
	for (unsigned frame = 1; frame <= agreed; ++frame) {
		if (peers[0].states[frame] != peers[1].states[frame]) {
			diverged = true;
			divergenceFrame = frame - 1;
			break;
		}
	}
#endif
	const unsigned samples = peers[0].samples + peers[1].samples;
	const double latency = samples
		? (peers[0].latency + peers[1].latency) / samples : 0;
	std::printf("%s,%u,%u,%u,%u,%u,%u,%u,%.2f,%lld\n", name, fps, rtt,
		peers[0].frames, peers[1].frames,
		peers[0].stalls + peers[1].stalls,
		peers[0].replay + peers[1].replay,
		diverged ? divergenceFrame + 1 : 0,
		latency, (long long)microseconds);
}
} // namespace

int main() {
	for (unsigned fps : {50u, 60u}) {
		for (unsigned rtt : {30u, 60u, 100u, 150u}) {
			Benchmark(fps, rtt, false);
#ifndef OLD
			Benchmark(fps, rtt, true);
#endif
		}
	}
}
