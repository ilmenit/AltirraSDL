// AltirraBridge - per-client execution context
//
// The bridge command layer is shared by more than one front end: the
// line-protocol socket (bridge_server.cpp) and the embedded MCP server
// (source/mcp/). Each front end owns one ClientContext and passes it to
// Execute(). The context holds everything that belongs to a client
// rather than to the emulator:
//
//   - authentication (only meaningful for the socket front end),
//   - its frame gate (FRAME n),
//   - the input it has injected (joystick, fire, console switches,
//     held raw keys), on its own PIA input slot.
//
// Injected input from several contexts is combined so one client
// releasing its input never releases another client's: joystick
// directions use one PIA input slot per context (the PIA ANDs slots),
// fire buttons are ORed across contexts, console switches are ANDed
// (active-low), and each context releases only the raw keys it pressed.
//
// All of this runs on the SDL main thread; there is no locking.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

class ATSimulator;

namespace ATBridge {

struct InputState {
	int     piaInputSlot = -1;     // ATPIAEmulator::AllocInput() handle
	uint8_t consol       = 0x07;   // active-low: bit 0=START, 1=SELECT, 2=OPTION, 1=released
	bool    fireDown[4]  = {};
	uint8_t dirNibble[4] = {};     // 4-bit per port: bit0=up, 1=down, 2=left, 3=right; 1=pressed
	uint64_t rawKeysDown = 0;      // bit n = matrix key n held via KEYRAW
	uint64_t rawShiftKeys = 0;     // matrix keys that requested SHIFT
	uint64_t rawCtrlKeys = 0;      // matrix keys that requested CONTROL
	bool    rawShift     = false;  // SHIFT held via KEYRAW
	bool    rawCtrl      = false;  // CONTROL held via KEYRAW
};

struct ClientContext {
	explicit ClientContext(const char *name) : mpName(name) {}
	ClientContext(const ClientContext&) = delete;
	ClientContext& operator=(const ClientContext&) = delete;

	const char *mpName;             // "bridge", "mcp" -- for logging
	bool        authenticated = false;
	bool        attached = false;   // registered and PIA slot allocated
	uint32_t    framesRemaining = 0;
	// MCP retains the timeline until it captures its completion result,
	// even after the scheduler/frame gate itself has stopped the CPU.
	bool        completionPending = false;
	// Frames of the last FRAME run that never ran because the emulator
	// stopped first (breakpoint, user pause). Reset by the next FRAME.
	uint32_t    framesInterrupted = 0;
	InputState  input;
};

// Register the context with the bridge core and allocate its PIA input
// slot. Idempotent. Execute() attaches lazily, so front ends only need
// this when they want the slot before the first command.
void AttachContext(ClientContext& ctx, ATSimulator& sim);

// Release every input this context has injected and cancel its frame
// gate. The simulator's running state is left untouched. The context
// stays attached (its PIA slot is kept for reuse).
void ReleaseContext(ClientContext& ctx, ATSimulator& sim);

// ReleaseContext() plus free the PIA input slot and unregister. Safe to
// call on a context that was never attached.
void DetachContext(ClientContext& ctx, ATSimulator& sim);

// Run one already-tokenised bridge command on behalf of ctx and return
// the single-line JSON response (newline-terminated). HELLO and
// authentication are the caller's concern: Execute() never checks
// ctx.authenticated. BOOT_BARE may return an empty string when its reply
// is deferred (socket front end only).
std::string Execute(const std::vector<std::string>& tokens, ATSimulator& sim,
	ClientContext& ctx);

// True if any asynchronous gate that blocks further commands is armed:
// any context's frame gate, a CYCLES/SCANLINES step, or a BOOT_BARE.
bool IsContextGated(const ClientContext& ctx);

// CYCLES / SCANLINES steps and BOOT_BARE complete asynchronously and are
// owned by the context that started them (only one can be in flight).
// Front ends other than the bridge socket call PollAsync() each frame:
// it returns true once ctx's step or bare boot has finished, with the
// command's final JSON reply in `reply` (a BOOT_BARE result, or "" for a
// step -- issue CLOCK for its outcome).
bool IsAsyncOwnedBy(const ClientContext& ctx);
bool IsAsyncBusy();      // a step or bare boot is in flight (any owner)
bool PollAsync(ClientContext& ctx, ATSimulator& sim, std::string& reply);

// Abandon ctx's in-flight step or bare boot (client went away).
void CancelAsync(ClientContext& ctx, ATSimulator& sim);

}  // namespace ATBridge
