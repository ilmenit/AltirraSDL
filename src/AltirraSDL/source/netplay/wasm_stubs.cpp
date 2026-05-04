// Altirra SDL3 netplay - WASM-only stubs.
//
// Native builds compile platform_notify.cpp, http_minimal.cpp,
// port_mapping.cpp, nat_discovery.cpp.  WASM builds exclude those
// (browsers can't open raw sockets, can't ask the router for a NAT-PMP
// lease, can't post OS-level notifications) but their public symbols
// are still referenced by netplay code that DOES compile under
// Emscripten — the lobby_worker, coordinator, and UI sources call
// them on shared paths.  This TU provides minimal no-op
// implementations so the link resolves; the hot paths in those
// callers are guarded by `#if !defined(__EMSCRIPTEN__)` so the
// stubs are never actually invoked at runtime.
//
// If you find a stub being called at runtime in a WASM build, the
// caller is missing an `#if !__EMSCRIPTEN__` guard — fix the caller,
// don't make the stub do real work here (it can't).

#include <stdafx.h>

#include "platform_notify.h"
#include "http_minimal.h"
#include "port_mapping.h"
#include "nat_discovery.h"

#include <cstring>

namespace ATNetplay {

// ---- platform_notify.h ----------------------------------------------

void SetWindow(SDL_Window*) {}
void Initialize(const char*) {}
void Shutdown() {}

int Notify(const char* /*title*/, const char* /*body*/,
           const NotifyPrefs& /*prefs*/) {
	// Return -1 = no notification posted.  Callers ignore the value.
	return -1;
}

// ---- http_minimal.h -------------------------------------------------

void HttpRequestSync(const HttpRequest& /*in*/, HttpResponse& out) {
	// Synchronous HTTP isn't possible on the browser main thread.
	// LobbyWorker's WASM branch uses emscripten_fetch_t instead and
	// never funnels through here.  Anything else hitting this path
	// is a guard-missed bug.
	out.status = 0;
	out.body.clear();
	out.error = "HttpRequestSync not available in WASM build";
}

// ---- port_mapping.h -------------------------------------------------

void ReleaseUdpPortMapping(const PortMapping& /*m*/) {
	// No router-assisted port mapping in browser tabs.
}

// ---- nat_discovery.h ------------------------------------------------

bool ReflectorProbe::Run(const char* /*lobbyHost*/,
                         uint16_t /*lobbyPort*/,
                         uint16_t /*localGamePort*/,
                         uint32_t /*timeoutMs*/,
                         std::string& outSrflx,
                         std::string& outErr) {
	outSrflx.clear();
	outErr = "STUN-lite probe not available in WASM build";
	return false;
}

} // namespace ATNetplay
