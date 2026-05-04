// Altirra SDL3 netplay - UDP transport (impl)

#include <stdafx.h>

#include "transport.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <iphlpapi.h>
#  include <windows.h>
   typedef int socklen_t;
#  define NP_LAST_ERR()     ((int)WSAGetLastError())
#  define NP_WOULDBLOCK(e)  ((e) == WSAEWOULDBLOCK)
#  define NP_CLOSE(s)       closesocket((SOCKET)(s))
#  define NP_INVALID_SOCK   ((intptr_t)INVALID_SOCKET)
#else
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <netdb.h>
#  include <net/if.h>
#  include <ifaddrs.h>
#  define NP_LAST_ERR()     (errno)
#  define NP_WOULDBLOCK(e)  ((e) == EAGAIN || (e) == EWOULDBLOCK)
#  define NP_CLOSE(s)       ::close((int)(s))
#  define NP_INVALID_SOCK   ((intptr_t)-1)
#endif

namespace ATNetplay {

// ---------------------------------------------------------------------------
// Winsock lifecycle
// ---------------------------------------------------------------------------
//
// The bridge transport (bridge_transport.cpp) also calls WSAStartup, and
// both modules exist in the same process.  WSAStartup is reference-counted
// by the OS, so repeated calls are safe — each module owns its own init
// and no cross-module coordination is required.

namespace {

void InitNetSubsystem() {
#if defined(_WIN32)
	static bool initialised = false;
	if (initialised) return;
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) {
		initialised = true;
	}
	// On failure we leave initialised=false so later calls will retry;
	// every socket op will fail until WSAStartup succeeds.
#endif
}

bool SetNonBlocking(intptr_t s) {
#if defined(_WIN32)
	u_long mode = 1;
	return ioctlsocket((SOCKET)s, FIONBIO, &mode) == 0;
#else
	int flags = fcntl((int)s, F_GETFL, 0);
	if (flags < 0) return false;
	return fcntl((int)s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

// Split "host:port" at the LAST ':' so IPv6-in-brackets can be added
// later without re-parsing.  For AF_INET-only v1.0, the input must be
// a bare "1.2.3.4:26101" or "hostname.example:26101".
bool SplitHostPort(const char* s, char* hostOut, size_t hostCap, uint16_t& portOut) {
	if (!s || hostCap == 0) return false;
	const char* lastColon = nullptr;
	for (const char* p = s; *p; ++p) {
		if (*p == ':') lastColon = p;
	}
	if (!lastColon || lastColon == s) return false;

	size_t hostLen = (size_t)(lastColon - s);
	if (hostLen + 1 > hostCap) return false;
	std::memcpy(hostOut, s, hostLen);
	hostOut[hostLen] = '\0';

	long p = std::strtol(lastColon + 1, nullptr, 10);
	if (p <= 0 || p > 65535) return false;
	portOut = (uint16_t)p;
	return true;
}

} // anonymous

// ---------------------------------------------------------------------------
// Endpoint
// ---------------------------------------------------------------------------

bool Endpoint::Equals(const Endpoint& other) const {
	if (rawLen != other.rawLen) return false;
	if (rawLen == 0) return true;
	return std::memcmp(raw, other.raw, rawLen) == 0;
}

size_t Endpoint::Format(char* buf, size_t bufSize) const {
	if (bufSize == 0) return 0;
	if (!IsValid() || rawLen < (int)sizeof(sockaddr_in)) {
		const char* dead = "<invalid>";
		size_t n = std::strlen(dead);
		if (n + 1 > bufSize) n = bufSize - 1;
		std::memcpy(buf, dead, n);
		buf[n] = '\0';
		return n;
	}

	sockaddr_in addr4;
	std::memcpy(&addr4, raw, sizeof addr4);

	char ipBuf[INET_ADDRSTRLEN] = {};
#if defined(_WIN32)
	InetNtopA(AF_INET, (void*)&addr4.sin_addr, ipBuf, sizeof ipBuf);
#else
	inet_ntop(AF_INET, &addr4.sin_addr, ipBuf, sizeof ipBuf);
#endif

	int written = std::snprintf(buf, bufSize, "%s:%u",
		ipBuf, (unsigned)ntohs(addr4.sin_port));
	if (written < 0) { buf[0] = '\0'; return 0; }
	if ((size_t)written >= bufSize) return bufSize - 1;
	return (size_t)written;
}

// ---------------------------------------------------------------------------
// UdpTransport
// ---------------------------------------------------------------------------

UdpTransport::UdpTransport()
	: mSock(NP_INVALID_SOCK)
	, mBoundPort(0)
{}

UdpTransport::~UdpTransport() {
	Close();
}

bool UdpTransport::IsOpen() const {
	return mSock != NP_INVALID_SOCK;
}

bool UdpTransport::Listen(uint16_t port) {
	Close();
	InitNetSubsystem();

#if defined(_WIN32)
	SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == INVALID_SOCKET) return false;
	intptr_t h = (intptr_t)s;
#else
	int s = ::socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0) return false;
	intptr_t h = (intptr_t)s;
#endif

	// SO_REUSEADDR lets a restarted host re-bind the same port
	// quickly after a previous instance exits (Linux keeps the port
	// in TIME_WAIT-like state for a short window).
	int yes = 1;
	::setsockopt((int)h, SOL_SOCKET, SO_REUSEADDR,
	             (const char*)&yes, sizeof yes);

	// SO_REUSEPORT (Linux 3.9+, *BSD, macOS, Android API 21+): allow a
	// co-bound NAT reflector probe socket on the same port.  Without
	// this, the probe can't share the NAT mapping the game socket
	// established, and the srflx (public) candidate published to the
	// lobby would point to a *different* external port than the game
	// socket — making cross-internet joins fail on most cone-NAT
	// routers.  Windows has no Linux-equivalent of SO_REUSEPORT; on
	// that platform the probe falls back to an ephemeral port and the
	// user may need to port-forward manually.
#ifdef SO_REUSEPORT
	::setsockopt((int)h, SOL_SOCKET, SO_REUSEPORT,
	             (const char*)&yes, sizeof yes);
#endif

	sockaddr_in addr {};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (::bind((int)h, (sockaddr*)&addr, sizeof addr) != 0) {
		NP_CLOSE(h);
		return false;
	}

	socklen_t alen = sizeof addr;
	if (::getsockname((int)h, (sockaddr*)&addr, &alen) != 0) {
		NP_CLOSE(h);
		return false;
	}
	mBoundPort = ntohs(addr.sin_port);

	if (!SetNonBlocking(h)) {
		NP_CLOSE(h);
		return false;
	}

	mSock = h;
	return true;
}

bool UdpTransport::Resolve(const char* hostPort, Endpoint& out) {
	InitNetSubsystem();

	char host[256] = {};
	uint16_t port = 0;
	if (!SplitHostPort(hostPort, host, sizeof host, port)) return false;

	addrinfo hints {};
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;

	char portStr[8];
	std::snprintf(portStr, sizeof portStr, "%u", (unsigned)port);

	addrinfo* results = nullptr;
	int rc = ::getaddrinfo(host, portStr, &hints, &results);
	if (rc != 0 || !results) return false;

	// Pick the first AF_INET result.  getaddrinfo orders by platform
	// preference; AF_INET-only hints means everything here is usable.
	out = {};
	for (addrinfo* ai = results; ai; ai = ai->ai_next) {
		if (ai->ai_family != AF_INET) continue;
		if (ai->ai_addrlen > Endpoint::kMaxRaw) continue;
		std::memcpy(out.raw, ai->ai_addr, ai->ai_addrlen);
		out.rawLen = (uint8_t)ai->ai_addrlen;
		break;
	}
	::freeaddrinfo(results);
	return out.IsValid();
}

bool UdpTransport::DiscoverLocalIPv4(char* outIp, size_t outCap) {
	if (!outIp || outCap < 16) return false;
	outIp[0] = '\0';

	InitNetSubsystem();

	// Create a transient UDP socket and "connect" to a public address.
	// No packet is actually sent — UDP connect just records the dest
	// so the kernel picks an outbound route.  getsockname then reports
	// the IP the kernel would use.  Works on Linux, macOS, Android,
	// Windows, BSD.  Fallback destination is a well-known DNS root.
	//
	// We try 8.8.8.8 first (Google Public DNS), then 1.1.1.1 (Cloudflare)
	// as a second attempt if a restrictive network filters 8.8.8.8's
	// route.  If both fail we also try 192.0.2.1 (TEST-NET-1) which
	// exists only in the route table, never on the wire.
	const char* const kProbes[] = { "8.8.8.8", "1.1.1.1", "192.0.2.1" };

	for (const char* probe : kProbes) {
#if defined(_WIN32)
		SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (s == INVALID_SOCKET) continue;
		intptr_t h = (intptr_t)s;
#else
		int s = ::socket(AF_INET, SOCK_DGRAM, 0);
		if (s < 0) continue;
		intptr_t h = (intptr_t)s;
#endif

		sockaddr_in probeAddr {};
		probeAddr.sin_family = AF_INET;
		probeAddr.sin_port = htons(53);
#if defined(_WIN32)
		InetPtonA(AF_INET, probe, &probeAddr.sin_addr);
#else
		inet_pton(AF_INET, probe, &probeAddr.sin_addr);
#endif

		if (::connect((int)h, (const sockaddr*)&probeAddr, sizeof probeAddr) != 0) {
			NP_CLOSE(h);
			continue;
		}

		sockaddr_in local {};
		socklen_t localLen = sizeof local;
		if (::getsockname((int)h, (sockaddr*)&local, &localLen) != 0) {
			NP_CLOSE(h);
			continue;
		}
		NP_CLOSE(h);

		// Reject loopback — not useful for peers.  Also reject
		// link-local / APIPA (169.254/16) which appears when DHCP
		// fails — those addresses can never reach a peer on any
		// other network.
		uint32_t ipHost = ntohl(local.sin_addr.s_addr);
		if ((ipHost & 0xFF000000u) == 0x7F000000u) continue;    // 127/8
		if ((ipHost & 0xFFFF0000u) == 0xA9FE0000u) continue;    // 169.254/16
		if (ipHost == 0) continue;

#if defined(_WIN32)
		const char* r = InetNtopA(AF_INET, &local.sin_addr, outIp, outCap);
#else
		const char* r = inet_ntop(AF_INET, &local.sin_addr, outIp, outCap);
#endif
		if (!r) continue;
		return true;
	}

	outIp[0] = '\0';
	return false;
}

bool UdpTransport::EnumerateLocalIPv4s(std::vector<std::string>& out) {
	out.clear();
	InitNetSubsystem();

#if defined(_WIN32)
	// IP Helper API route — GetAdaptersAddresses reports every
	// IPv4 unicast address on every "up" adapter.  ~16 KB buffer
	// handles the common case; the ERROR_BUFFER_OVERFLOW path
	// grows if the host has many NICs / addresses.
	ULONG bufLen = 16 * 1024;
	std::vector<uint8_t> buf(bufLen);
	ULONG rc = ::GetAdaptersAddresses(
		AF_INET,
		GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
		GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_FRIENDLY_NAME,
		nullptr,
		reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()),
		&bufLen);
	if (rc == ERROR_BUFFER_OVERFLOW) {
		buf.resize(bufLen);
		rc = ::GetAdaptersAddresses(
			AF_INET,
			GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
			GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_FRIENDLY_NAME,
			nullptr,
			reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()),
			&bufLen);
	}
	if (rc != NO_ERROR) return false;

	for (IP_ADAPTER_ADDRESSES* a =
			reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
	     a; a = a->Next) {
		if (a->OperStatus != IfOperStatusUp) continue;
		for (IP_ADAPTER_UNICAST_ADDRESS* u = a->FirstUnicastAddress;
		     u; u = u->Next) {
			if (!u->Address.lpSockaddr) continue;
			if (u->Address.lpSockaddr->sa_family != AF_INET) continue;
			auto* s4 = reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr);
			uint32_t ipHost = ntohl(s4->sin_addr.s_addr);
			if ((ipHost & 0xFF000000u) == 0x7F000000u) continue;  // 127/8
			if ((ipHost & 0xFFFF0000u) == 0xA9FE0000u) continue;  // 169.254/16
			if (ipHost == 0) continue;
			char ipBuf[INET_ADDRSTRLEN] = {};
			InetNtopA(AF_INET, &s4->sin_addr, ipBuf, sizeof ipBuf);
			if (*ipBuf) out.emplace_back(ipBuf);
		}
	}
#else
	ifaddrs* head = nullptr;
	if (::getifaddrs(&head) != 0) return false;
	for (ifaddrs* a = head; a; a = a->ifa_next) {
		if (!a->ifa_addr) continue;
		if (a->ifa_addr->sa_family != AF_INET) continue;
		// Skip interfaces that aren't up.  IFF_RUNNING means cable
		// connected / associated; IFF_UP alone can be a brought-up
		// but disconnected interface.
		if ((a->ifa_flags & IFF_UP) == 0) continue;
		if ((a->ifa_flags & IFF_RUNNING) == 0) continue;
		auto* s4 = reinterpret_cast<sockaddr_in*>(a->ifa_addr);
		uint32_t ipHost = ntohl(s4->sin_addr.s_addr);
		if ((ipHost & 0xFF000000u) == 0x7F000000u) continue;  // 127/8
		if ((ipHost & 0xFFFF0000u) == 0xA9FE0000u) continue;  // 169.254/16
		if (ipHost == 0) continue;
		char ipBuf[INET_ADDRSTRLEN] = {};
		inet_ntop(AF_INET, &s4->sin_addr, ipBuf, sizeof ipBuf);
		if (*ipBuf) {
			// Deduplicate (macOS reports the same address on
			// alias interfaces sometimes).
			bool dup = false;
			for (const auto& existing : out) {
				if (existing == ipBuf) { dup = true; break; }
			}
			if (!dup) out.emplace_back(ipBuf);
		}
	}
	::freeifaddrs(head);
#endif

	return !out.empty();
}

bool UdpTransport::SendTo(const uint8_t* bytes, size_t n, const Endpoint& to) {
	if (!IsOpen() || !to.IsValid() || n == 0 || bytes == nullptr) return false;

	// Test-only outbound packet loss.  A simple xorshift32 keeps the
	// test deterministic while staying header-only and very cheap when
	// the drop rate is 0 (the common case — production builds never
	// call SetTestDropRate).
	if (mTestDropRate > 0.0f) {
		uint32_t r = mTestRngState;
		r ^= r << 13;
		r ^= r >> 17;
		r ^= r << 5;
		mTestRngState = r;
		// Map the 32-bit output to [0, 1) and compare against the
		// drop rate.  Inclusive on the low side, exclusive on the
		// high — at dropRate=1.0 we drop everything; at 0.0 nothing.
		const float u = (float)(r & 0x00FFFFFFu) / (float)0x01000000u;
		if (u < mTestDropRate) {
			// Drop: pretend the kernel accepted the bytes.  Returning
			// true mimics a successful send so retry logic in the
			// caller fires only on absent ACKs / timeouts, not on
			// "send failed".
			return true;
		}
	}

	int rc = ::sendto((int)mSock,
	                  (const char*)bytes, (int)n, 0,
	                  (const sockaddr*)to.raw, (socklen_t)to.rawLen);
	if (rc < 0) return false;
	return (size_t)rc == n;
}

void UdpTransport::SetTestDropRate(float dropRate, uint32_t seed) {
	if (dropRate < 0.0f) dropRate = 0.0f;
	if (dropRate > 1.0f) dropRate = 1.0f;
	mTestDropRate = dropRate;
	// xorshift32 needs non-zero seed; coerce 0 to 1 silently.
	mTestRngState = seed ? seed : 1u;
}

RecvResult UdpTransport::RecvFrom(uint8_t* buf, size_t bufSize,
                               size_t& outLen, Endpoint& from) {
	outLen = 0;
	from = {};
	if (!IsOpen()) return RecvResult::Error;
	if (buf == nullptr || bufSize == 0) return RecvResult::Error;

	sockaddr_storage peer {};
	socklen_t peerLen = sizeof peer;

	int rc = ::recvfrom((int)mSock,
	                    (char*)buf, (int)bufSize, 0,
	                    (sockaddr*)&peer, &peerLen);
	if (rc < 0) {
		return NP_WOULDBLOCK(NP_LAST_ERR())
			? RecvResult::WouldBlock
			: RecvResult::Error;
	}

	outLen = (size_t)rc;
	if ((size_t)peerLen > Endpoint::kMaxRaw) {
		// Unexpected — should never happen for AF_INET peers.
		return RecvResult::Error;
	}
	std::memcpy(from.raw, &peer, peerLen);
	from.rawLen = (uint8_t)peerLen;
	return RecvResult::Ok;
}

void UdpTransport::Close() {
	if (mSock != NP_INVALID_SOCK) {
		NP_CLOSE(mSock);
		mSock = NP_INVALID_SOCK;
	}
	mBoundPort = 0;
}

} // namespace ATNetplay
