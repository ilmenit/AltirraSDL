// Altirra SDL3 netplay - main-loop glue
//
// Owns a singleton Coordinator and exposes a tiny surface the main
// loop in main_sdl3.cpp can call without pulling in the full
// coordinator header.  When no netplay session exists, every entry
// point here is O(1) — a single null check plus early return — so
// there is no cost to having the hook compiled in.
//
// Lifecycle:
//   1. The UI (Phase 11) creates a session by calling NetplayStartHost
//      or NetplayStartJoin.  A Coordinator is constructed on the heap
//      and stored in the glue's singleton slot.
//   2. The main loop calls NetplayPoll once per iteration.  While
//      the session is in handshake / snapshot transfer / lockstep,
//      the glue drives the coordinator.
//   3. When the coordinator reaches Ended / Desynced / Failed, the
//      glue tears down the singleton on the next NetplayPoll.
//
// Input capture / injection (Phase 8) will land as two additional
// entry points — NetplayCaptureLocal and NetplayInjectRemote — plus
// a lockstep-mode predicate NetplayOwnsInput() that the input
// adapters consult before routing events to ATInputManager.  Those
// are stubs in this phase; the coordinator accepts calls but nothing
// in the emulator feeds it real input yet.

#pragma once

#include <cstddef>
#include <cstdint>

namespace ATNetplayGlue {

// True iff a Coordinator exists in some non-terminal phase.  The
// main loop uses this as a cheap gate before doing any netplay work.
bool IsActive();

// True iff the Coordinator is currently in the lockstep phase (the
// only phase where the input adapters must defer to netplay routing).
// Safe to call when no session exists — returns false.
bool IsLockstepping();

// Drive the Coordinator's Poll().  Must be called once per main-loop
// iteration from main_sdl3.cpp.  No-op when no session exists.
void Poll(uint64_t nowMs);

// Returns false when the main loop should SKIP ATSimulator::Advance
// this tick — i.e. the coordinator is waiting for peer input before
// emulation may advance.  Returns true in every other state
// (including "no session active"), preserving existing behaviour.
bool CanAdvanceThisTick();

// Signal that the simulator just advanced one frame.  Drives the
// coordinator's lockstep hash + frame counter.  No-op outside
// lockstep.
void OnFrameAdvanced();

// Tear down any active session and release the singleton.  Safe to
// call unconditionally on shutdown.
void Shutdown();

// --- UI entry points (Phase 11 wiring) -------------------------------------
//
// These are thin wrappers over Coordinator::BeginHost / BeginJoin that
// construct the singleton.  The Online Play dialogs call them.
//
// `entryCodeHash` is 16 bytes or nullptr for public sessions.
// Returns false on failure — inspect LastError() for a human
// string.

bool StartHost(uint16_t localPort,
               const char* playerHandle,
               const char* cartName,
               uint64_t osRomHash,
               uint64_t basicRomHash,
               uint64_t settingsHash,
               uint16_t inputDelayFrames,
               const uint8_t* entryCodeHash);

bool StartJoin(const char* hostAddress,
               const char* playerHandle,
               uint64_t osRomHash,
               uint64_t basicRomHash,
               bool acceptTos,
               const uint8_t* entryCodeHash);

// Host: hand the serialised simulator state to the coordinator so it
// can stream it to the joiner.  Safe to call before or after a
// joiner appears — the coordinator holds it until needed.
void SubmitSnapshotBytes(const uint8_t* data, size_t len);

// Diagnostics / HUD.
const char* LastError();
uint16_t    BoundPort();
uint32_t    CurrentFrame();

} // namespace ATNetplayGlue
