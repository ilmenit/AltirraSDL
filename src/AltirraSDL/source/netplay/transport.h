// Altirra SDL3 netplay - UDP transport
//
// Thin wrapper around a single non-blocking UDP socket.  The netplay
// coordinator drives this from the main loop: Poll() style, no
// threads.
//
// Scope (v1.0):
//   - IPv4 only.  IPv6 adds no emulator-user value until a host
//     actually runs v6-only, at which point we'll extend Endpoint
//     and switch to AF_UNSPEC.  Deferred to v1.1.
//   - UDP unicast only.  LAN broadcast lives in a separate
//     helper in Phase 10 of the plan.
//
// The class owns one socket.  Both host and joiner use it the
// same way: Listen() binds a local port, SendTo() emits to a
// peer endpoint, RecvFrom() drains the pending datagram queue.
// There is no "connected" state — UDP doesn't have one — the
// caller tracks who the peer is.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ATNetplay {

// Opaque peer address.  Large enough to hold a sockaddr_in today
// and a sockaddr_in6 later; layout is an implementation detail.
struct Endpoint {
	static constexpr size_t kMaxRaw = 32;    // >= sizeof(sockaddr_in)
	uint8_t raw[kMaxRaw] = {};
	uint8_t rawLen = 0;

	bool IsValid() const { return rawLen > 0; }
	bool Equals(const Endpoint& other) const;

	// Format as "a.b.c.d:port".  buf must be at least 24 bytes; returns
	// number of characters written (not counting NUL).  Returns 0 and
	// writes "<invalid>\0" on unset endpoints.
	size_t Format(char* buf, size_t bufSize) const;
};

enum class RecvResult {
	Ok,           // bytes delivered via outLen
	WouldBlock,   // no datagram queued
	Error,        // hard error; socket may still be usable
};

// Polymorphic transport interface.  Two production implementations:
//   - UdpTransport      — POSIX/Winsock UDP datagram socket (native).
//   - WasmTransport     — Emscripten WebSocket relay through the lobby
//                         (browser builds; lives in transport_wasm.cpp).
//
// Coordinator owns one INetTransport via std::unique_ptr and calls
// only the methods declared here.  UDP-only operations (LAN
// discovery, NAT-PMP, blocking name resolution) stay as static
// members on UdpTransport — the Coordinator never invokes them on
// non-UDP transports because their code paths are gated by PeerPath.
//
// Members are virtual where the SDL3 build needs to dispatch between
// UDP/WS at runtime (SendTo/RecvFrom/IsOpen/Close/BoundPort).  The
// abstract interface is intentionally narrower than UdpTransport's
// public surface — UDP-specific helpers stay statically typed on
// UdpTransport so callers that need them don't get a fake "always
// fails" override on WasmTransport.
class INetTransport {
public:
	virtual ~INetTransport() = default;

	// Bind / open the underlying transport.  For UdpTransport this
	// binds a UDP socket (port == 0 → ephemeral); for WasmTransport
	// this opens the WebSocket to the lobby (port is ignored).
	// Returns true on success — note that for non-blocking transports
	// "open" may complete asynchronously: poll IsOpen() afterwards.
	virtual bool Listen(uint16_t port) = 0;

	// True after Listen() succeeded (or, for WS, after the handshake
	// completed) and before Close().
	virtual bool IsOpen() const = 0;

	// Local UDP port if applicable; 0 for transports without a stable
	// local port concept (WS).
	virtual uint16_t BoundPort() const = 0;

	// Non-blocking send.  Returns true iff the bytes were queued to
	// the underlying transport.  For WS, `to` is ignored — the
	// connection is single-peer; the lobby routes by session+role.
	virtual bool SendTo(const uint8_t* bytes, size_t n,
	                    const Endpoint& to) = 0;

	// Non-blocking recv.  See UdpTransport docs for buffer sizing
	// requirements.  For WS, `from` is set to a synthetic endpoint
	// (currently zeroed) since there is exactly one peer per
	// connection and the Coordinator's WsRelay path doesn't dispatch
	// by source.
	virtual RecvResult RecvFrom(uint8_t* buf, size_t bufSize,
	                            size_t& outLen, Endpoint& from) = 0;

	// Idempotent close.
	virtual void Close() = 0;

	// True if this transport is fundamentally relay-only (WASM/WS).
	// The Coordinator uses this to suppress NAT-traversal machinery
	// (NAT punch, direct rescue, candidate spray) that doesn't apply
	// when the carrier itself is a session-scoped WS connection.
	// Default false; override in WasmTransport.
	virtual bool IsRelayOnly() const { return false; }
};

class UdpTransport final : public INetTransport {
public:
	UdpTransport();
	~UdpTransport() override;

	UdpTransport(const UdpTransport&) = delete;
	UdpTransport& operator=(const UdpTransport&) = delete;

	// Bind a local UDP socket on AF_INET, any interface, port = `port`.
	// port == 0 asks the OS for an ephemeral port; BoundPort() reports
	// what was actually chosen.  Returns false on failure.
	bool Listen(uint16_t port) override;

	// True after Listen() succeeded and before Close().
	bool IsOpen() const override;

	// The local port the socket is bound to.  Valid only after a
	// successful Listen().
	uint16_t BoundPort() const override { return mBoundPort; }

	// Resolve "host:port" to an Endpoint via getaddrinfo().  Host may
	// be a dotted-quad IPv4 literal or a DNS name.  Returns false on
	// parse/resolution failure.  Blocking — call from the handshake
	// setup path, not the hot loop.
	static bool Resolve(const char* hostPort, Endpoint& out);

	// Discover the primary local IPv4 address the host would use to
	// reach the public internet.  Implementation is the portable
	// "UDP connect + getsockname" trick: no packet is actually sent,
	// the kernel just picks the outbound interface based on its route
	// table.  On success, writes a dotted-quad into outIp (buffer must
	// be at least INET_ADDRSTRLEN=16 bytes) and returns true.  On
	// failure (no network, all loopback, etc.) returns false — the
	// caller should fall back to 127.0.0.1.  Synchronous but fast
	// (<1 ms in practice).
	static bool DiscoverLocalIPv4(char* outIp, size_t outCap);

	// Enumerate ALL up-and-running IPv4 interface addresses on this
	// host.  The caller should publish every result as a candidate
	// endpoint — the kernel's primary (default-route) interface may
	// not be the one that reaches a specific peer when a VPN, tether,
	// or secondary NIC is active.  Excludes loopback (127/8) and
	// link-local APIPA (169.254/16).  The result vector is cleared
	// first.  Non-empty return on at least one usable interface.
	//
	// Portable impl uses getifaddrs() on POSIX (Linux, macOS, BSD,
	// Android) and GetAdaptersAddresses() on Windows.
	static bool EnumerateLocalIPv4s(std::vector<std::string>& out);

	// Non-blocking send.  Returns true if the datagram was queued to
	// the kernel; false on EWOULDBLOCK or any other error.  UDP
	// delivery is still best-effort — a `true` return only means the
	// socket accepted the bytes.
	bool SendTo(const uint8_t* bytes, size_t n, const Endpoint& to) override;

	// Test instrumentation only.  When dropRate > 0, SendTo silently
	// discards that fraction of outbound datagrams (deterministic via
	// `seed` so test runs are reproducible).  Default 0 = real send,
	// no overhead beyond a single comparison.  Called from
	// coordinator_selftest's lossy-network regressions; not used in
	// production builds.  The drop is on the SEND side because that
	// composes correctly with two Transport instances in the same
	// process — packets dropped here never reach the peer, so the
	// peer's recv never sees them, modeling real-world packet loss.
	void SetTestDropRate(float dropRate, uint32_t seed = 1);

	// Non-blocking recv.  On RecvResult::Ok, outLen carries the byte
	// count and `from` carries the sender's endpoint.  On
	// RecvResult::WouldBlock the queue is empty.  `bufSize` must be
	// large enough for the biggest expected datagram (see
	// kMaxDatagramSize in packets.h); shorter buffers will silently
	// truncate the datagram on Linux and flag an error on Windows.
	RecvResult RecvFrom(uint8_t* buf, size_t bufSize,
	                    size_t& outLen, Endpoint& from) override;

	// Close the socket if open.  Idempotent.
	void Close() override;

private:
	// Opaque socket handle.  Typed in the .cpp to avoid pulling
	// <winsock2.h> / <sys/socket.h> into every includer.
	intptr_t mSock;
	uint16_t mBoundPort;
	// Test-only drop instrumentation.  See SetTestDropRate.  When
	// mTestDropRate is 0 (default), SendTo's hot path skips the drop
	// check entirely after a single zero-compare.
	float    mTestDropRate = 0.0f;
	uint32_t mTestRngState = 1u;
};

// Source-compatibility alias — existing code that named the old
// concrete class `Transport` keeps compiling.  New code should write
// `UdpTransport` directly when it needs UDP-specific methods, or
// `INetTransport` when it can work with any transport.
using Transport = UdpTransport;

} // namespace ATNetplay
