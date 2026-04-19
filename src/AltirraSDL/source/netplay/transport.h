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

class Transport {
public:
	Transport();
	~Transport();

	Transport(const Transport&) = delete;
	Transport& operator=(const Transport&) = delete;

	// Bind a local UDP socket on AF_INET, any interface, port = `port`.
	// port == 0 asks the OS for an ephemeral port; BoundPort() reports
	// what was actually chosen.  Returns false on failure.
	bool Listen(uint16_t port);

	// True after Listen() succeeded and before Close().
	bool IsOpen() const;

	// The local port the socket is bound to.  Valid only after a
	// successful Listen().
	uint16_t BoundPort() const { return mBoundPort; }

	// Resolve "host:port" to an Endpoint via getaddrinfo().  Host may
	// be a dotted-quad IPv4 literal or a DNS name.  Returns false on
	// parse/resolution failure.  Blocking — call from the handshake
	// setup path, not the hot loop.
	static bool Resolve(const char* hostPort, Endpoint& out);

	// Non-blocking send.  Returns true if the datagram was queued to
	// the kernel; false on EWOULDBLOCK or any other error.  UDP
	// delivery is still best-effort — a `true` return only means the
	// socket accepted the bytes.
	bool SendTo(const uint8_t* bytes, size_t n, const Endpoint& to);

	// Non-blocking recv.  On RecvResult::Ok, outLen carries the byte
	// count and `from` carries the sender's endpoint.  On
	// RecvResult::WouldBlock the queue is empty.  `bufSize` must be
	// large enough for the biggest expected datagram (see
	// kMaxDatagramSize in packets.h); shorter buffers will silently
	// truncate the datagram on Linux and flag an error on Windows.
	RecvResult RecvFrom(uint8_t* buf, size_t bufSize,
	                    size_t& outLen, Endpoint& from);

	// Close the socket if open.  Idempotent.
	void Close();

private:
	// Opaque socket handle.  Typed in the .cpp to avoid pulling
	// <winsock2.h> / <sys/socket.h> into every includer.
	intptr_t mSock;
	uint16_t mBoundPort;
};

} // namespace ATNetplay
