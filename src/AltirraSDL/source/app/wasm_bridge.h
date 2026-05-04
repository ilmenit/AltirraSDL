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

// Re-scan the firmware directory (/home/web_user/firmware) so any
// files the user has uploaded since the last scan become visible to
// the firmware manager.  JS calls this once at startup (after the
// initial IDBFS sync completes) and again after every successful
// firmware upload.  Internally this just forwards to the Setup
// Wizard's existing ATUIDoFirmwareScan helper.
extern "C" void ATWasmRescanFirmware();

// Unpack a ZIP archive at `zipPath` into `destDir`, preserving the
// archive's directory structure.  Both paths are absolute UTF-8 VFS
// paths.  Intermediate directories under destDir are created on
// demand.  Returns the number of regular files extracted, or -1 on
// error (zip unreadable, destDir uncreatable, etc.).  IDBFS is
// flushed via ATWasmSyncFSOut() before returning on success.
extern "C" int ATWasmUnpackArchive(const char *zipPath, const char *destDir);

// Recursively unlink every file and rmdir every empty directory
// under `path`.  `path` itself is also removed unless it is the IDBFS
// mount root.  Returns the count of items removed (files + dirs), or
// -1 on error.  Caller is responsible for calling ATWasmSyncFSOut()
// after, so several wipes can be batched into a single IDBFS flush.
extern "C" int ATWasmDeleteTree(const char *path);

// First-run helper: download the standard ROM bundle if the user has
// no firmware yet.  Idempotent — bails fast if the marker file
// /home/web_user/.config/altirra/.firstrun_done already exists or if
// /home/web_user/firmware contains any files.  Otherwise issues an
// async emscripten_fetch_t for the preferred (HTTPS) URL; on failure
// retries the fallback (HTTP) URL.  On success, extracts every entry
// whose name ends in ".rom" (case-insensitive) into the firmware
// directory, runs ATUIDoFirmwareScan, flushes IDBFS, and writes the
// marker.  On both URLs failing the marker is still written so we
// don't retry on every load.  See ATWasmGetFirstRunStatus for
// progress polling from JS.
extern "C" void ATWasmFirstRunBootstrap();

// First-run status, polled by the JS shell to drive the toast UX.
//   *outState           — see kATWasmFirstRunState_* values.
//   *outFilesExtracted  — count of ROM files written so far (only
//                         meaningful when state >= 4).
// Either pointer may be NULL.
//
//   0 = idle / not started
//   1 = checking marker / firmware dir
//   2 = fetching primary URL
//   3 = fetching fallback URL
//   4 = unpacking
//   5 = done, success (>=1 file extracted)
//   6 = done, no firmware files in archive
//   7 = done, both URLs failed
//   8 = skipped (marker present or firmware dir already populated)
extern "C" void ATWasmGetFirstRunStatus(int *outState, int *outFilesExtracted);

#else // !__EMSCRIPTEN__

inline void ATWasmBridgeTick() {}

#endif // __EMSCRIPTEN__
