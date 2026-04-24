//	AltirraSDL - WebAssembly / browser bridge (header)
//
//	JS <-> C glue for the Emscripten build.  Provides three pieces:
//
//	  1. Exported C functions that the JS shell (index.html) calls when
//	     the user uploads a game or firmware file, triggers a boot, or
//	     requests a library refresh.  These run on the single JS thread,
//	     same as the emulator tick — so they just enqueue work and let
//	     the main tick consume it.
//
//	  2. A main-thread drain function (`ATWasmBridgeTick`) called once
//	     per tick from the main loop to process any pending requests.
//
//	  3. Convenience helpers callable from C++ (currently unused outside
//	     wasm_bridge.cpp; exposed in case the netplay / bridge modules
//	     want a symmetric JS-facing surface in the future).
//
//	All of this compiles on non-WASM targets too (as empty inline
//	stubs) so the main loop's `ATWasmBridgeTick()` call doesn't need
//	its own `#ifdef __EMSCRIPTEN__` guard.

#pragma once

#if defined(__EMSCRIPTEN__)

// Drain any pending upload / boot / library-refresh requests queued
// by JS via ATWasmOnFileUploaded.  No-op when the queue is empty.
// Must be called from the main tick only.
void ATWasmBridgeTick();

// Exported to JS (EMSCRIPTEN_KEEPALIVE'd in wasm_bridge.cpp).  JS
// writes the uploaded file into the virtual filesystem at `vfsPath`
// first, then calls this so the main tick picks it up.
//
//   bootNow != 0 → request a cold-boot from `vfsPath` on the next tick.
//   bootNow == 0 → just refresh the game library so the new file
//                  becomes visible in any open browser view.
extern "C" void ATWasmOnFileUploaded(const char* vfsPath, int bootNow);

// Optional — force a flush of IDBFS back to IndexedDB.  JS already
// does this after each upload in the shell; exposed here so C++ can
// request a flush at shutdown too.  Safe to call multiple times.
extern "C" void ATWasmSyncFSOut();

#else // !__EMSCRIPTEN__

inline void ATWasmBridgeTick() {}

#endif // __EMSCRIPTEN__
