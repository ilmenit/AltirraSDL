// Altirra SDL3 netplay - WASM (browser) transport over WebSocket.
//
// Browsers cannot open UDP sockets, so the WASM build replaces
// UdpTransport with a relay-only WebSocket transport.  Both endpoints
// of the connection (the browser and the lobby on Oracle Cloud) speak
// the SAME inner Altirra wire packets the native UDP path uses; the
// only difference is the carrier.  The lobby's WS bridge (slice 1)
// translates between WS frames and UDP datagrams when the OTHER peer
// is on UDP, so this transport interoperates with native peers
// transparently.
//
// Connection lifetime:
//   ctor                -> nothing on the wire
//   Listen(port)        -> open WSS to the lobby; handshake is async,
//                          IsOpen() flips to true on `onopen`
//   SendTo(...)         -> queue if not yet open; otherwise WS-send
//   RecvFrom(...)       -> drain inbound queue; WouldBlock if empty
//   Close()             -> WS close 1000
//
// All emscripten_websocket_* state is owned by this object.  The
// Coordinator sees a normal INetTransport — no protocol-level
// awareness of WS vs. UDP.

#pragma once

#include "transport.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#if defined(__EMSCRIPTEN__)
// Pulled in here so the static-callback declarations below see the
// real struct types (with their `_Nonnull` annotations) and the
// typedef-d `em_websocket_*_callback_func` types match.  Forward-
// declaring the structs would create them in our namespace and
// produce subtle ODR / signature-mismatch errors at the
// emscripten_websocket_set_*_callback registration sites.
#include <emscripten/websocket.h>
#endif

namespace ATNetplay {

class WasmTransport final : public INetTransport {
public:
	// Authenticated WS handshake parameters.  All four are required.
	//
	//   lobbyHost  — `altirra-lobby.duckdns.org` (no scheme, no port).
	//                The transport always uses wss:// on port 443.
	//   sidHex32   — 32-char lowercase hex form of the session id (the
	//                UUIDv4 returned by Create with dashes stripped).
	//   role       — 0 = host, 1 = joiner.  Mirrors the existing UDP
	//                kRelayRole* constants in packets.h.
	//   tokenHex   — 32-char hex token returned by Create() (or
	//                published by the host out-of-band for joiners
	//                who already know the session id).
	WasmTransport(std::string lobbyHost,
	              std::string sidHex32,
	              uint8_t     role,
	              std::string tokenHex);
	~WasmTransport() override;

	WasmTransport(const WasmTransport&) = delete;
	WasmTransport& operator=(const WasmTransport&) = delete;

	// INetTransport.  See base class for semantics.  `port` is ignored
	// (WS doesn't bind locally).
	bool Listen(uint16_t port) override;
	bool IsOpen() const override;
	uint16_t BoundPort() const override { return 0; }
	bool SendTo(const uint8_t* bytes, size_t n,
	            const Endpoint& to) override;
	RecvResult RecvFrom(uint8_t* buf, size_t bufSize,
	                    size_t& outLen, Endpoint& from) override;
	void Close() override;

	// WS bridge IS the relay; no NAT punch, no direct probes.
	bool IsRelayOnly() const override { return true; }

	// Did the WebSocket error or close abnormally?  See INetTransport.
	// True after the browser fires onerror or onclose with a non-1000
	// close code — meaning the session can no longer carry inner
	// frames and the Coordinator should fail the joiner / host.
	//
	// Also fires the synthetic "handshake timeout" failure path: if
	// Listen() was called more than kHandshakeTimeoutMs ago and the
	// browser still hasn't delivered onopen, we treat that as a
	// silent-timeout failure (DNS / TLS / Caddy route missing /
	// firewall) and surface a friendly message to the UI rather than
	// letting the joiner sit on "Waiting for host…" forever.  The
	// Coordinator polls HasFailed()/FailureReason() each Poll() so the
	// fail flips through promptly.
	bool HasFailed() const override;
	const char *FailureReason() const override {
		return mFailureSummary.c_str();
	}
	uint16_t      CloseCode()   const { return mCloseCode; }
	const char *  CloseReason() const { return mCloseReason.c_str(); }
	bool          CloseWasClean() const { return mCloseWasClean; }

private:
	// Emscripten provides `EMSCRIPTEN_WEBSOCKET_T` which is an int
	// handle, not a pointer.  We store it as int32_t so the header
	// doesn't pull in <emscripten/websocket.h>.
	int32_t mWs = 0;
	bool    mOpen = false;
	bool    mClosed = false;

	std::string mLobbyHost;
	std::string mSidHex;
	std::string mTokenHex;
	uint8_t     mRole = 0;

	// Inbound queue of fully-received netplay packets.  Each entry is
	// the inner bytes only — the WS envelope role byte is stripped at
	// queue insertion.  Capped at kInboundCap; on overflow the OLDEST
	// entry is dropped and a one-shot warning is logged.
	std::deque<std::vector<uint8_t>> mInbound;

	// Outbound queue used between construction and `onopen` arrival.
	// Once mOpen flips to true the queue is drained in arrival order.
	std::deque<std::vector<uint8_t>> mOutbound;

	// One-shot inbound-cap warning gate.
	bool mWarnedInboundOverflow = false;

	// Failure state set by OnError / OnClose so the Coordinator can
	// detect WS-level failures (auth reject, lobby down, mixed-content
	// block) and surface a clear error to the UI.  Without this the
	// joiner sits at "Waiting for host…" forever.
	//
	// `mutable` because HasFailed() is `const` (interface contract) but
	// has to lazily flip mFailed when the handshake-timeout deadline
	// passes — the alternative is a separate Tick() method on the
	// transport interface that every other transport has to implement
	// as a no-op.
	mutable bool        mFailed = false;
	mutable uint16_t    mCloseCode = 0;
	mutable std::string mCloseReason;
	bool                mCloseWasClean = false;
	// Human-readable summary of the failure, generated in OnCloseCb
	// from mCloseCode / mCloseReason and exposed via FailureReason().
	mutable std::string mFailureSummary;

	// Pre-handshake deadline.  emscripten_get_now() returns ms since
	// page load, so this is a millisecond timestamp recorded in
	// Listen() and consulted from HasFailed().  Zero before Listen()
	// or after the handshake completes — the timeout only fires while
	// mOpen is still false.  10 s is generous for slow phone networks
	// (median TLS+WS handshake on 4G is ~1.5s) but still snappy enough
	// that the UI doesn't sit on "Waiting for host…" indefinitely when
	// the lobby is unreachable.
	double mHandshakeStartMs = 0.0;
	static constexpr double kHandshakeTimeoutMs = 10000.0;

	// Emscripten event callbacks.  Match the canonical typedef
	// signatures (including `_Nonnull` annotations) from
	// <emscripten/websocket.h> exactly — anything looser produces a
	// "no matching function for call to set_*_callback" error at
	// the registration site.
#if defined(__EMSCRIPTEN__)
	static bool OnOpenCb(int eventType,
		const EmscriptenWebSocketOpenEvent* _Nonnull e, void* userData);
	static bool OnMessageCb(int eventType,
		const EmscriptenWebSocketMessageEvent* _Nonnull e, void* userData);
	static bool OnErrorCb(int eventType,
		const EmscriptenWebSocketErrorEvent* _Nonnull e, void* userData);
	static bool OnCloseCb(int eventType,
		const EmscriptenWebSocketCloseEvent* _Nonnull e, void* userData);
#endif

	// Drain mOutbound after handshake completes.  Called from OnOpen.
	void DrainOutboundLocked();
};

} // namespace ATNetplay
