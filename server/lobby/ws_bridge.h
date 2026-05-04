// Altirra SDL netplay lobby — WebSocket bridge.
//
// Accepts WSS connections on `localhost:<port>` (Caddy fronts the
// public TLS endpoint and reverse-proxies plain WS to us).  Each binary
// frame from the browser carries `[role:u8][inner_netplay_packet…]`.
// We look up the other peer in the same session, then forward the
// inner bytes — to a WS peer as a binary frame, or to a UDP peer as an
// ASDF-wrapped datagram on the reflector socket.
//
// The bridge knows nothing about Altirra's wire packets; it just routes
// bytes by session id and role.  Authentication uses the WS subprotocol
// header (`Sec-WebSocket-Protocol`) — query-string tokens were rejected
// because Caddy logs URIs.
//
// All mongoose state lives on a single thread (the bridge's own).
// Cross-thread sends from the reflector use `mg_wakeup()`, which is
// thread-safe by design.  The reflector socket FD is shared via
// ReflectorSocketHandle so outbound UDP from the bridge appears to the
// remote peer as coming from `lobby_ip:8081` — preserving the stable
// (IP, port) identity that `Coordinator::PeerIsLobby()` checks for
// direct-rescue from mid-session relay.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <netinet/in.h>

namespace ATLobby {

// Lock-free counters mirroring the relay metric set in server.cpp.
// Exposed via /v1/metrics in slice 6.
struct WsBridgeStats {
	std::atomic<uint64_t> connectionsTotal{0};
	std::atomic<uint64_t> upgradesRejectedAuth{0};
	std::atomic<uint64_t> upgradesRejectedConflict{0};
	std::atomic<uint64_t> messagesIn{0};
	std::atomic<uint64_t> messagesOut{0};
	std::atomic<uint64_t> bytesIn{0};
	std::atomic<uint64_t> bytesOut{0};
	std::atomic<uint64_t> forwardsCrossTransport{0};   // WS↔UDP either way
	std::atomic<uint64_t> droppedRateLimit{0};
	std::atomic<uint64_t> droppedNoPeer{0};
	std::atomic<uint64_t> droppedOversized{0};
	std::atomic<uint64_t> droppedAuth{0};
};

// Reflector socket FD, owned by the reflector thread.  Set to a valid
// non-negative FD once bind() succeeds; reset to -1 when the reflector
// is stopping.  Bridge's `sendto()` calls on this FD are thread-safe
// under the kernel's per-socket lock.
struct ReflectorSocketHandle {
	std::atomic<int> fd{-1};
};

// Result of a session-validation lookup.  Values match HTTP semantics
// so the bridge can echo them on a rejected upgrade.
enum class WsAuthResult : int {
	kOk           = 0,
	kBadRequest   = 400,    // malformed subprotocol list
	kForbidden    = 403,    // wrong token
	kGone         = 410,    // session expired between Create and upgrade
	kConflict     = 409,    // role already occupied
	kInternal     = 500,    // bug
};

// Callback: validate a session+role+token combo.  Called from the
// bridge thread during WS upgrade.  The implementation lives in
// server.cpp where Store is defined.
//
//   sidRaw  — 16 raw bytes (UUID stripped of dashes, hex-decoded)
//   role    — 0 (host) or 1 (joiner), from the existing UDP relay enum
//   tokenHex — null-terminated token string from the subprotocol header
//   ctx     — opaque pointer (Store*)
//
// Returns kOk iff the upgrade should be allowed.  The bridge also
// rejects upgrades where the (sid, role) slot is already occupied by
// another WS connection or a registered UDP relay endpoint — that
// check happens after this callback so the role-already-occupied case
// can be distinguished from token-mismatch in metrics/logs.
using WsSessionValidatorFn = WsAuthResult(*)(
	const uint8_t sidRaw[16], uint8_t role,
	const char* tokenHex, void* ctx);

// Callback: send an ASDF-wrapped UDP frame to a peer registered in the
// RelayTable.  Implementation lives in server.cpp with access to
// RelayTable (and hence the per-pair token bucket).
//
//   sidRaw      — 16 bytes
//   senderRole  — 0/1, role of the WS peer that sent the packet (so the
//                 receiver's RelayTable lookup returns the OTHER side)
//   inner       — inner packet bytes (already stripped of the WS
//                 envelope role byte)
//   innerLen    — bytes
//   ctx         — opaque pointer (RelayTable*)
//
// Returns true iff the packet was forwarded, false on NoPeer or
// rate-limit drop.  The bridge uses the bool to update metrics; on
// false the inner packet is silently dropped.
using WsUdpForwarderFn = bool(*)(
	const uint8_t sidRaw[16], uint8_t senderRole,
	const uint8_t* inner, size_t innerLen,
	void* ctx);

// Callback: when the bridge accepts a WS upgrade, registers the
// (sid, role, conn) in the WsRegistry so the reflector thread can find
// it during ASDF dispatch.  Likewise on close, unregister.  These run
// on the bridge thread; the registry is mutex-guarded internally.
struct WsRegistryHandle;  // opaque to consumers; defined in ws_bridge.cpp

// Bridge's published view of the WsRegistry, exposed so the reflector
// thread (in server.cpp) can probe whether the OTHER side of a session
// is on WS before falling back to UDP.  Accessed under the registry's
// internal mutex.
//
//   sidRaw      — 16 bytes
//   senderRole  — 0/1, role of the UDP peer; lookup returns the OTHER
//                 side (host ↔ joiner)
//   inner       — inner packet bytes (unwrapped from the ASDF frame)
//   innerLen    — bytes
//
// Returns true iff a WS peer was found and queued for delivery.  The
// queue is drained by the bridge's mongoose loop; delivery is
// best-effort but ordered per-connection.
bool WsBridgeForwardToWs(WsRegistryHandle* reg,
                         const uint8_t sidRaw[16], uint8_t senderRole,
                         const uint8_t* inner, size_t innerLen,
                         WsBridgeStats& stats);

// Returned to the caller (server.cpp's main()) so it can hand the
// registry pointer to the reflector thread.  Lifetime is tied to
// RunWsBridge's stack frame; the caller MUST join the bridge thread
// before destroying the registry.
struct WsBridgeContext {
	WsRegistryHandle* reg = nullptr;
};

struct WsBridgeConfig {
	uint16_t    port              = 8090;
	const char* listenAddr        = "127.0.0.1";
	size_t      maxMessageBytes   = 2048;     // RFC6455 frame cap
	uint64_t    pingIntervalMs    = 15000;
	uint64_t    pongTimeoutMs     = 10000;
	uint64_t    idleTimeoutMs     = 60000;    // app-level peer-silence
};

// Run the bridge.  Blocks until `stop` flips to true.  All callbacks
// are invoked on the bridge thread.  `outCtx` is populated before the
// first poll so the caller can publish the registry pointer to other
// threads.
void RunWsBridge(const WsBridgeConfig& cfg,
                 WsBridgeStats& stats,
                 ReflectorSocketHandle& reflectorFd,
                 WsSessionValidatorFn validateFn, void* validateCtx,
                 WsUdpForwarderFn udpForwardFn, void* udpForwardCtx,
                 WsBridgeContext& outCtx,
                 std::atomic<bool>& stop);

}  // namespace ATLobby
