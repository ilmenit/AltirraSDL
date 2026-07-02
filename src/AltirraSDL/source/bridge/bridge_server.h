// AltirraBridge - scripting/automation server
//
// Public API for the bridge server module. The bridge exposes the
// AltirraSDL emulator over a line-delimited JSON protocol on a local
// socket (loopback TCP by default, or POSIX UDS) so that external
// processes (Python clients, C SDK clients, AI agents) can drive the
// emulator: frame-step, peek/poke memory, capture screenshots, inject
// input, run the debugger, etc.
//
// All socket I/O happens on the SDL3 main thread inside Poll() which is
// called once per frame from the main loop. Non-blocking sockets, no
// background threads, no locks. The frame gate is driven by
// OnFrameCompleted() which the main loop calls each time GTIA produces
// a new frame.
//
// See AltirraBridge/docs/PROTOCOL.md for the wire contract.

#pragma once

#include <string>

class ATSimulator;
struct ATUIState;

namespace ATBridge {

// Initialise the bridge server. Parses the address spec, opens the
// listening socket, generates a session token, and writes the token
// file. Idempotent: calling Init() twice (e.g. because --bridge appears
// multiple times) is harmless. Returns false on fatal init failure
// (port already in use, address spec malformed, etc.); the caller
// should treat this as non-fatal — the rest of AltirraSDL keeps running
// without the bridge.
//
// The bridge does NOT take a long-lived reference to the simulator or
// UI state at Init() time. Both are passed per-call via Poll() and
// OnFrameCompleted() instead, which keeps the bridge stateless with
// respect to the rest of AltirraSDL and makes it possible (in tests
// or future tooling) to construct an emulator/UI pair separately.
//
// addrSpec forms accepted:
//   ""                            same as "tcp:127.0.0.1:0"
//   "tcp"                         same as "tcp:127.0.0.1:0"
//   "tcp:HOST:PORT"               TCP loopback (HOST is forced to 127.0.0.1
//                                 with a warning if non-loopback)
//   "unix:/path/to/socket"        POSIX filesystem socket (POSIX only)
//   "unix-abstract:NAME"          Linux abstract namespace (Linux only)
//
// Port 0 means "let the OS pick"; the chosen port is logged to stderr
// and written to the token file alongside the token.
bool Init(const std::string& addrSpec);

// Tear down the bridge: release injected input, drop any client,
// close the listening socket, remove the token file. The simulator
// reference is needed only to free the joystick PIA input slot
// allocated by the Phase 3 input layer; if it has not been
// initialised this is a no-op. Safe to call even if Init() was
// never called or returned false.
void Shutdown(ATSimulator& sim);

// Per-frame poll. Called from the SDL3 main loop right after
// ATTestModePollCommands(). Non-blocking; returns quickly. Does:
//   1. Accept a pending connection if any.
//   2. Read whatever the connected client has sent and dispatch any
//      complete (newline-terminated) commands, up to a per-frame cap
//      so a chatty client cannot starve emulation.
//   3. Flush queued response bytes back to the client.
// No-op if Init() was never called or returned false.
void Poll(ATSimulator& sim, ATUIState& ui);

// Frame-completion hook. Called from the SDL3 main loop immediately
// after a new GTIA frame becomes available (the `hadFrame` branch).
// Decrements the frame-gate counter; when it hits zero, calls
// sim.Pause() so the next FRAME command can step again. No-op when no
// gate is active.
void OnFrameCompleted(ATSimulator& sim);

// True while a FRAME command is waiting for produced frames. This is
// for main-loop integration only; it does not affect the wire protocol.
bool IsFrameGateActive();

// True if the user passed --bridge on the command line. Set by Init().
// Lets the main loop avoid the overhead of calling Poll() / OnFrame
// hooks when the bridge is disabled at runtime.
bool IsEnabled();

}  // namespace ATBridge
