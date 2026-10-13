// AltirraBridge - all state and callbacks are owned by the emulation thread.
#include <stdafx.h>
#include "bridge_commands_timing.h"
#include "bridge_protocol.h"
#include "simulator.h"
#include "constants.h"
#include "simeventmanager.h"
#include <at/atcore/scheduler.h>
#include <at/ataudio/pokey.h>
#include <deque>

namespace ATBridge {
namespace {
// ATSimulator::Advance (simulator.cpp) runs 114 machine cycles per
// ANTIC scanline, including DMA and WSYNC.
constexpr uint32 kCyclesPerScanline = 114;
constexpr uint32 kMaxStepCycles = 100000000;

struct CycleGate final : IATSchedulerCallback {
	ATSimulator *sim = nullptr;
	ATEvent *event = nullptr;
	uint64 start = 0;
	uint32 requested = 0;
	uint64 elapsed = 0;
	bool completed = false;

	void Cancel(ATSimulator& simulator) {
		if (event) elapsed = simulator.GetScheduler()->GetTick64() - start;
		simulator.GetScheduler()->UnsetEvent(event);
	}

	void OnScheduledEvent(uint32) override {
		event = nullptr;
		completed = true;
		elapsed = requested;
		// Scheduler events run before CPU/ANTIC for this cycle. Request an
		// interrupt so Advance exits AFTER completing the target cycle.
		sim->Pause();
		sim->PostInterruptingEvent(kATSimEvent_AnonymousInterrupt);
	}
} g_gate;

struct Write {
	uint64 tick;
	uint8 reg;
	uint8 value;
};
std::deque<Write> g_writes;
uint32 g_capacity = 65536;
uint64 g_dropped = 0;
uint64 g_traceStart = 0;
bool g_recording = false;
bool g_traceStarted = false;
bool g_invalidated = false;
bool g_monitorAttached = false;

struct TraceMonitor final : IATSimulatorCallback {
	void OnSimulatorEvent(ATSimulatorEvent ev) override {
		if (ev == kATSimEvent_ColdReset || ev == kATSimEvent_WarmReset
			|| ev == kATSimEvent_StateLoaded)
			g_invalidated = true;
	}
} g_traceMonitor;

void DetachTraceMonitor(ATSimulator& sim) {
	if (g_monitorAttached) {
		sim.GetEventManager()->RemoveCallback(&g_traceMonitor);
		g_monitorAttached = false;
	}
}

std::string TraceStatus() {
	return "\"recording\":" + std::string(g_recording ? "true" : "false")
		+ ",\"valid\":" + std::string(g_traceStarted && !g_invalidated && !g_dropped
			? "true" : "false")
		+ ",\"invalidated\":" + std::string(g_invalidated ? "true" : "false")
		+ ",\"start_tick\":" + std::to_string(g_traceStart)
		+ ",\"capacity\":" + std::to_string(g_capacity)
		+ ",\"queued\":" + std::to_string(g_writes.size())
		+ ",\"dropped\":" + std::to_string(g_dropped);
}

std::string InitialRegisters(const ATPokeyEmulator& pokey) {
	ATPokeyRegisterState state;
	pokey.GetRegisterState(state);
	std::string result = "[";
	for (int i = 0; i < 16; ++i) {
		if (i) result += ',';
		result += std::to_string(state.mReg[i]);
	}
	return result + ']';
}
} // namespace

bool IsTimingGateActive() { return g_gate.event != nullptr; }

void UpdateTimingCommands(ATSimulator& sim) {
	if (g_gate.event && !sim.IsRunning())
		g_gate.Cancel(sim);
}

void CleanupTimingCommands(ATSimulator& sim) {
	g_gate.Cancel(sim);
	g_gate.sim = nullptr;
	g_gate.requested = 0;
	g_gate.elapsed = 0;
	g_gate.start = 0;
	g_gate.completed = false;
	sim.GetPokey().SetRegisterWriteObserver({});
	DetachTraceMonitor(sim);
	g_recording = false;
	g_traceStarted = false;
	g_invalidated = false;
	g_writes.clear();
	g_dropped = 0;
	g_traceStart = 0;
	g_capacity = 65536;
}

std::string CmdTiming(ATSimulator& sim,
	const std::vector<std::string>& tokens) {
	const auto& verb = tokens[0];
	if (verb == "CYCLES" || verb == "SCANLINES") {
		uint32 count = 1;
		const uint32 scale = verb == "SCANLINES" ? kCyclesPerScanline : 1;
		if (tokens.size() > 2
			|| (tokens.size() == 2 && !ParseUint(tokens[1], count))
			|| !count || count > kMaxStepCycles / scale)
			return JsonError(verb + ": count must be between 1 and "
				+ std::to_string(kMaxStepCycles / scale));
		g_gate.Cancel(sim);
		g_gate.sim = &sim;
		g_gate.start = sim.GetScheduler()->GetTick64();
		g_gate.requested = count * scale;
		g_gate.completed = false;
		g_gate.elapsed = 0;
		g_gate.event = sim.GetScheduler()->AddEvent(g_gate.requested, &g_gate, 1);
		sim.Resume();
		return JsonOk("\"cycles\":" + std::to_string(g_gate.requested)
			+ ",\"start_tick\":" + std::to_string(g_gate.start)
			+ ",\"target_tick\":" + std::to_string(g_gate.start + g_gate.requested));
	}
	if (verb == "CLOCK") {
		if (tokens.size() != 1) return JsonError("CLOCK: no arguments expected");
		const uint64 tick = sim.GetScheduler()->GetTick64();
		return JsonOk("\"tick\":" + std::to_string(tick)
			+ ",\"running\":" + std::string(sim.IsRunning() ? "true" : "false")
			+ ",\"step_requested\":" + std::to_string(g_gate.requested)
			+ ",\"step_elapsed\":" + std::to_string(g_gate.elapsed)
			+ ",\"step_completed\":" + std::string(g_gate.completed ? "true" : "false"));
	}

	if (tokens.size() < 2 || tokens.size() > 3)
		return JsonError(
			"POKEY_TRACE: start [capacity] | stop | read [max] | status");
	const auto& action = tokens[1];
	if (action == "start") {
		uint32 capacity = 65536;
		if (tokens.size() == 3 && !ParseUint(tokens[2], capacity))
			return JsonError("POKEY_TRACE: bad capacity");
		if (!capacity || capacity > 1000000)
			return JsonError("POKEY_TRACE: capacity must be between 1 and 1000000");
		g_capacity = capacity;
		g_writes.clear();
		g_dropped = 0;
		g_traceStart = sim.GetScheduler()->GetTick64();
		g_recording = true;
		g_traceStarted = true;
		g_invalidated = false;
		if (!g_monitorAttached) {
			sim.GetEventManager()->AddCallback(&g_traceMonitor);
			g_monitorAttached = true;
		}
		sim.GetPokey().SetRegisterWriteObserver(
		[](uint64 tick, uint8 reg, uint8 value) {
			if (g_writes.size() == g_capacity) {
				++g_dropped;
				return;
			}
			g_writes.push_back({tick, reg, value});
		}, [] { g_invalidated = true; });
		const ATPokeyEmulator *chips[] = { &sim.GetPokey(),
			sim.GetPokey().GetSlave(1), sim.GetPokey().GetSlave(2),
			sim.GetPokey().GetSlave(3) };
		unsigned count = 4;
		while (!chips[count - 1]) --count;
		std::string initial = ",\"initial\":[";
		for (unsigned i = 0; i < count; ++i) {
			if (i) initial += ',';
			initial += chips[i] ? InitialRegisters(*chips[i]) : "null";
		}
		return JsonOk(TraceStatus() + initial + ']');
	}
	if (action == "read") {
		uint32 count = 4096;
		if ((tokens.size() == 3 && !ParseUint(tokens[2], count))
			|| !count || count > 4096)
			return JsonError("POKEY_TRACE: read count must be between 1 and 4096");
		std::string events = ",\"events\":[";
		bool first = true;
		while (count-- && !g_writes.empty()) {
			const Write w = g_writes.front();
			g_writes.pop_front();
			if (!first) events += ',';
			first = false;
			events += "{\"tick\":" + std::to_string(w.tick)
				+ ",\"chip\":" + std::to_string((w.reg >> 4) + 1)
				+ ",\"reg\":" + std::to_string(w.reg & 15)
				+ ",\"value\":" + std::to_string(w.value) + '}';
		}
		return JsonOk(TraceStatus() + events + ']');
	}
	if (tokens.size() != 2)
		return JsonError("POKEY_TRACE: unexpected argument");
	if (action == "stop") {
		sim.GetPokey().SetRegisterWriteObserver({});
		DetachTraceMonitor(sim);
		g_recording = false;
	} else if (action != "status") {
		return JsonError("POKEY_TRACE: unknown action");
	}
	return JsonOk(TraceStatus());
}
} // namespace ATBridge
