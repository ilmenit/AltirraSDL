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
// Transport
// ---------------------------------------------------------------------------

Transport::Transport()
	: mSock(NP_INVALID_SOCK)
	, mBoundPort(0)
{}

Transport::~Transport() {
	Close();
}

bool Transport::IsOpen() const {
	return mSock != NP_INVALID_SOCK;
}

bool Transport::Listen(uint16_t port) {
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

bool Transport::Resolve(const char* hostPort, Endpoint& out) {
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

bool Transport::SendTo(const uint8_t* bytes, size_t n, const Endpoint& to) {
	if (!IsOpen() || !to.IsValid() || n == 0 || bytes == nullptr) return false;

	int rc = ::sendto((int)mSock,
	                  (const char*)bytes, (int)n, 0,
	                  (const sockaddr*)to.raw, (socklen_t)to.rawLen);
	if (rc < 0) return false;
	return (size_t)rc == n;
}

RecvResult Transport::RecvFrom(uint8_t* buf, size_t bufSize,
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

void Transport::Close() {
	if (mSock != NP_INVALID_SOCK) {
		NP_CLOSE(mSock);
		mSock = NP_INVALID_SOCK;
	}
	mBoundPort = 0;
}

} // namespace ATNetplay
