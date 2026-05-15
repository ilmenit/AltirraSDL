// AltirraBridge - main-program glue functions
//
// These four functions are the only edge of the bridge module that
// changes per-target. Each AltirraBridge build target provides its
// own implementation:
//
//   - The SDL3 frontend (AltirraSDL) provides bridge_main_glue_sdl3.cpp,
//     which routes the requests through ATUIPushDeferred(...) so they
//     run via the same deferred-action queue used by the menu File >
//     Open / File > Save State commands. Boot/load completion happens
//     synchronously between bridge polls.
//
//   - The headless server (AltirraBridgeServer) provides its own
//     implementation in main_bridge.cpp that calls into the simulator
//     directly (sim.Load, snapshot save/load) — no deferred queue, no
//     SDL3 main loop dependency.
//
// Returning false signals "dispatch failed (e.g. invalid path
// argument)"; the bridge command then returns a JSON error to the
// client. Returning true means "request accepted".
//
// **STATE_SAVE/LOAD now run synchronously in both targets** (no
// deferred queue dance). The bridge_savestate.cpp module owns the
// memory I/O; per-target glue is only needed for path mode, which
// reuses the existing on-disk SaveState pipeline so a slot blob and
// a .altstate2 file remain byte-equivalent. Per-target glue STILL
// runs path-mode save/load on the bridge thread synchronously --
// the returned response carries the actual outcome and metadata, so
// clients no longer need the historical "FRAME 1 to wait" pattern
// for save/load (boot/mount keep their async semantics because they
// may trigger a cold reset that takes hundreds of frames to settle).

#pragma once

#include <string>

class ATSimulator;

// Boot a media file (XEX, ATR, CAS, CAR, BIN). Path is UTF-8.
bool ATBridgeDispatchBoot(ATSimulator& sim, const std::string& path);

// Mount a disk image into a drive slot (0..14, where 0 = D1:).
bool ATBridgeDispatchMount(ATSimulator& sim, int drive, const std::string& path);

// Save the simulator snapshot to a file path. Runs synchronously.
// Returns true on success; on false the caller should report a
// "dispatch failed" error to the client.
bool ATBridgeDispatchStateSave(ATSimulator& sim, const std::string& path);

// Load a previously-saved snapshot from a file path. Runs
// synchronously. The simulator's running state is PRESERVED across
// the load (paused stays paused, running stays running) -- the
// loaded snapshot's running state is not honoured. Clients that
// want a specific running state issue PAUSE/RESUME afterwards.
bool ATBridgeDispatchStateLoad(ATSimulator& sim, const std::string& path);
