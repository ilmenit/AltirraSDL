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
// client. Returning true means "request accepted" — the actual work
// may complete asynchronously (SDL3 path) or synchronously (headless
// path), depending on the implementation. Either way, the client
// must use FRAME N afterwards to wait for the change to settle in
// observable simulator state.

#pragma once

#include <string>

class ATSimulator;

// Boot a media file (XEX, ATR, CAS, CAR, BIN). Path is UTF-8.
bool ATBridgeDispatchBoot(ATSimulator& sim, const std::string& path);

// Mount a disk image into a drive slot (0..14, where 0 = D1:).
bool ATBridgeDispatchMount(ATSimulator& sim, int drive, const std::string& path);

// Save / load the simulator snapshot to/from a file path.
bool ATBridgeDispatchStateSave(ATSimulator& sim, const std::string& path);
bool ATBridgeDispatchStateLoad(ATSimulator& sim, const std::string& path);
