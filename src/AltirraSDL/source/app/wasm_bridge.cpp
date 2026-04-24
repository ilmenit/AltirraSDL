//	AltirraSDL - WebAssembly / browser bridge (implementation)
//
//	See wasm_bridge.h for the big picture.  This file is only compiled
//	under ALTIRRA_WASM — src/AltirraSDL/CMakeLists.txt adds it to the
//	target sources inside an `if(ALTIRRA_WASM)` block, so the native
//	builds never link any of the code below.
//
//	Concurrency model: the browser runs the entire wasm module on a
//	single JS thread (we are building without -pthread / SharedArray-
//	Buffer for GitHub-Pages portability).  That means the JS-facing
//	exports and the main loop tick are guaranteed to run sequentially
//	— no mutex needed on the pending-request queue.  A std::mutex is
//	still used defensively so a future pthread-enabled build could
//	reuse this code unchanged.

#include <stdafx.h>

#if defined(__EMSCRIPTEN__)

#include <emscripten.h>

#include <mutex>
#include <string>
#include <vector>

#include <vd2/system/text.h>
#include <vd2/system/VDString.h>

#include "wasm_bridge.h"

// ATUIBootImage: the simulator's "load, auto-detect type, cold-boot"
// one-shot API.  Declared in Altirra's main.cpp-era header that the
// SDL3 build already pulls in for the file-dialog Open flow.
extern void ATUIBootImage(const wchar_t *path);

// ATUIDoFirmwareScan: setup wizard's per-directory firmware scanner.
// Walks the given path, auto-detects every recognised image by CRC,
// and registers the results with the live firmware manager.  Exposed
// (non-static) specifically so the WASM bridge can invoke it after
// the user uploads a firmware file.
extern void ATUIDoFirmwareScan(const char *utf8path);

// -----------------------------------------------------------------------
// Pending-request queue
// -----------------------------------------------------------------------

namespace {
	struct PendingUpload {
		std::string mVfsPath;   // UTF-8 as supplied by JS
		bool        mBootNow;   // request immediate cold-boot
	};

	std::mutex                  g_wasmQueueMutex;
	std::vector<PendingUpload>  g_wasmQueue;
}

// -----------------------------------------------------------------------
// JS-facing C exports
// -----------------------------------------------------------------------

extern "C" EMSCRIPTEN_KEEPALIVE
void ATWasmOnFileUploaded(const char* vfsPath, int bootNow) {
	if (!vfsPath || !*vfsPath)
		return;

	// Defensive copy; the JS caller may free the string buffer as soon
	// as we return.  string constructor makes an owned copy.
	PendingUpload p{ std::string(vfsPath), bootNow != 0 };

	std::lock_guard<std::mutex> lk(g_wasmQueueMutex);
	g_wasmQueue.push_back(std::move(p));
}

// Wrapped in EM_JS so the implementation — a call into Module.FS —
// doesn't need to round-trip through a named JS symbol.  Safe to call
// at any time; the browser shell also calls syncfs() after every
// upload, so this is a belt-and-suspenders flush.
EM_JS(void, _altirra_wasm_sync_fs_out, (), {
	if (typeof Module !== 'undefined' && Module.FS
			&& typeof Module.FS.syncfs === 'function') {
		Module.FS.syncfs(false, function (err) {
			if (err) console.warn('[altirra-wasm] syncfs(out) failed:', err);
		});
	}
});

extern "C" EMSCRIPTEN_KEEPALIVE
void ATWasmSyncFSOut() {
	_altirra_wasm_sync_fs_out();
}

extern "C" EMSCRIPTEN_KEEPALIVE
void ATWasmRescanFirmware() {
	// The Setup Wizard scanner takes a UTF-8 folder path and registers
	// every recognised firmware image it finds underneath.  Running it
	// on the dedicated /home/web_user/firmware upload directory keeps
	// the firmware manager in sync with what the user has dropped in.
	ATUIDoFirmwareScan("/home/web_user/firmware");
}

// -----------------------------------------------------------------------
// Main-thread drain
// -----------------------------------------------------------------------

void ATWasmBridgeTick() {
	// Swap the queue under the lock, then process unlocked so a
	// callback-triggered re-enqueue (hypothetical — JS is currently
	// driven by user input, not timers, but be defensive) cannot
	// deadlock on itself.
	std::vector<PendingUpload> batch;
	{
		std::lock_guard<std::mutex> lk(g_wasmQueueMutex);
		batch.swap(g_wasmQueue);
	}

	for (const auto& req : batch) {
		if (req.mBootNow) {
			// VDStringW / VDTextU8ToW is the canonical UTF-8 → wchar_t
			// conversion used throughout the system library for file
			// paths.  ATUIBootImage takes the wchar_t path and triggers
			// the simulator's full load + cold-boot sequence (same path
			// the desktop file-open dialog uses).
			const VDStringW pathW = VDTextU8ToW(VDStringSpanA(req.mVfsPath.c_str()));
			ATUIBootImage(pathW.c_str());
		}
		// else: nothing to do on the C side; the JS shell has already
		// written the file to /persist and flushed to IDBFS.  A future
		// addition would be an "ATWasmOnLibraryChanged" hook that
		// refreshes the open game-library panel, but v1 lets the user
		// click Refresh.
	}
}

#endif // __EMSCRIPTEN__
