// Altirra SDL3 netplay - NAT reflector client (impl)

#include <stdafx.h>

#include "nat_discovery.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
   typedef int socklen_t;
#  define ND_LAST_ERR()     ((int)WSAGetLastError())
#  define ND_WOULDBLOCK(e)  ((e) == WSAEWOULDBLOCK)
#  define ND_CLOSE(s)       closesocket((SOCKET)(s))
#  define ND_INVALID_SOCK   ((intptr_t)INVALID_SOCKET)
#else
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <netdb.h>
#  include <poll.h>
#  define ND_LAST_ERR()     (errno)
#  define ND_WOULDBLOCK(e)  ((e) == EAGAIN || (e) == EWOULDBLOCK)
#  define ND_CLOSE(s)       ::close((int)(s))
#  define ND_INVALID_SOCK   ((intptr_t)-1)
#endif

namespace ATNetplay {

namespace {

void EnsureNetSubsystem() {
#if defined(_WIN32)
	static std::mutex lock;
	std::lock_guard<std::mutex> guard(lock);
	static bool ok = false;
	if (ok) return;
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) ok = true;
#endif
}

uint64_t MonoMillis() {
	using namespace std::chrono;
	return (uint64_t)duration_cast<milliseconds>(
		steady_clock::now().time_since_epoch()).count();
}

bool WaitReadable(intptr_t s, uint32_t timeoutMs) {
#if defined(_WIN32)
	fd_set rf; FD_ZERO(&rf); FD_SET((SOCKET)s, &rf);
	timeval tv;
	tv.tv_sec  = (long)(timeoutMs / 1000);
	tv.tv_usec = (long)((timeoutMs % 1000) * 1000);
	int rc = ::select(0, &rf, nullptr, nullptr, &tv);
	return rc > 0;
#else
	pollfd pfd{};
	pfd.fd = (int)s;
	pfd.events = POLLIN;
	int rc = ::poll(&pfd, 1, (int)timeoutMs);
	return rc > 0 && (pfd.revents & POLLIN);
#endif
}

bool ResolveHost(const char* host, sockaddr_in& out, std::string& err) {
	addrinfo hints {};
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;

	addrinfo* res = nullptr;
	int rc = ::getaddrinfo(host, nullptr, &hints, &res);
	if (rc != 0 || !res) {
		err = "DNS failed";
		return false;
	}

	bool found = false;
	for (addrinfo* ai = res; ai; ai = ai->ai_next) {
		if (ai->ai_family == AF_INET &&
		    ai->ai_addrlen >= sizeof(sockaddr_in)) {
			std::memcpy(&out, ai->ai_addr, sizeof out);
			found = true;
			break;
		}
	}
	::freeaddrinfo(res);
	if (!found) err = "no IPv4 address";
	return found;
}

} // anon

bool ReflectorProbe::Run(const char* lobbyHost, uint16_t reflectorPort,
                         uint16_t localPort, uint32_t timeoutMs,
                         std::string& outIpPort, std::string& err) {
	outIpPort.clear();
	err.clear();

	if (!lobbyHost || !*lobbyHost || reflectorPort == 0 || timeoutMs == 0) {
		err = "bad args";
		return false;
	}

	EnsureNetSubsystem();

	sockaddr_in dst {};
	if (!ResolveHost(lobbyHost, dst, err)) return false;
	dst.sin_family = AF_INET;
	dst.sin_port = htons(reflectorPort);

#if defined(_WIN32)
	SOCKET raw = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (raw == INVALID_SOCKET) { err = "socket() failed"; return false; }
	intptr_t s = (intptr_t)raw;
#else
	int raw = ::socket(AF_INET, SOCK_DGRAM, 0);
	if (raw < 0) { err = "socket() failed"; return false; }
	intptr_t s = (intptr_t)raw;
#endif

	// Share the game socket's port mapping on cone NATs.  On most
	// home routers this yields the same public mapping as the main
	// game socket — symmetric NAT is the exception we can't help with
	// at this layer (documented in NETPLAY docs).
	int yes = 1;
	::setsockopt((int)s, SOL_SOCKET, SO_REUSEADDR,
	             (const char*)&yes, sizeof yes);
#ifdef SO_REUSEPORT
	// Linux / macOS require this in addition to REUSEADDR for two
	// UDP sockets to co-bind the same port.  Harmless if unsupported.
	::setsockopt((int)s, SOL_SOCKET, SO_REUSEPORT,
	             (const char*)&yes, sizeof yes);
#endif

	sockaddr_in local {};
	local.sin_family = AF_INET;
	local.sin_port = htons(localPort);
	local.sin_addr.s_addr = htonl(INADDR_ANY);
	bool coBound = true;
	if (::bind((int)s, (const sockaddr*)&local, sizeof local) != 0) {
		// Same-port co-bind failed — typically because Transport::Listen
		// didn't set SO_REUSEPORT (Windows) or the platform doesn't
		// support it.  Fall back to ephemeral; srflx will point to a
		// different NAT mapping than the game socket, which may or may
		// not work depending on NAT type (full-cone: IP is still right
		// but port differs; restricted / symmetric: mostly broken).
		// Log so users investigating a failure see the NAT situation.
		local.sin_port = 0;
		coBound = false;
		if (::bind((int)s, (const sockaddr*)&local, sizeof local) != 0) {
			err = "bind() failed";
			ND_CLOSE(s);
			return false;
		}
	}

	// Critical: connect() the probe socket to the reflector under
	// SO_REUSEPORT.  Linux's SO_REUSEPORT hash-load-balances packets
	// between sockets that share a port, but a connect()-ed socket
	// *takes precedence* for 4-tuple matches — so reflector responses
	// land on THIS socket, not on the unconnected game socket.
	// Without this call, responses might be delivered to the game
	// socket (which silently drops unknown magics) and the probe
	// times out on those routers.
	if (::connect((int)s, (const sockaddr*)&dst, sizeof dst) != 0) {
		err = "connect() failed";
		ND_CLOSE(s);
		return false;
	}
	(void)coBound;

	// Build request: magic + txid.  Txid ties the response back to us
	// so we discard anything that arrived for an older probe on the
	// same ephemeral socket.
	uint32_t txid = (uint32_t)(MonoMillis() & 0xFFFFFFFFu);
	uint8_t req[kReflectorReqLen];
	req[0] = 'A'; req[1] = 'S'; req[2] = 'D'; req[3] = 'R';
	req[4] = (uint8_t)(txid        & 0xFF);
	req[5] = (uint8_t)((txid >> 8)  & 0xFF);
	req[6] = (uint8_t)((txid >> 16) & 0xFF);
	req[7] = (uint8_t)((txid >> 24) & 0xFF);

	// Retry up to 3 times within timeoutMs total — single-packet UDP
	// across the internet occasionally drops.
	uint64_t start = MonoMillis();
	uint32_t perAttempt = timeoutMs / 3;
	if (perAttempt < 200) perAttempt = timeoutMs;
	int attempts = 0;
	while (MonoMillis() - start < timeoutMs && attempts < 3) {
		++attempts;
		// Connected socket: send() routes to the peer we connect()-ed
		// to.  Errors propagate as ECONNREFUSED quickly if the
		// reflector isn't running (a big UX win over silent timeouts).
		int sent = ::send((int)s, (const char*)req, (int)sizeof req, 0);
		if (sent != (int)sizeof req) {
			err = "send() failed";
			ND_CLOSE(s);
			return false;
		}

		uint64_t deadline = MonoMillis() + perAttempt;
		while (MonoMillis() < deadline) {
			uint32_t left = (uint32_t)(deadline - MonoMillis());
			if (!WaitReadable(s, left)) break;

			uint8_t buf[128];
			// Connected recv() — the kernel guarantees the datagram
			// came from the connected peer (reflector), so we don't
			// need to re-check the source address.
			int n = ::recv((int)s, (char*)buf, (int)sizeof buf, 0);
			if (n < (int)kReflectorRespLen) continue;

			// Validate magic + txid.
			if (buf[0] != 'A' || buf[1] != 'S' ||
			    buf[2] != 'D' || buf[3] != 'R') continue;
			uint32_t rtx = (uint32_t)buf[4]
				| ((uint32_t)buf[5] << 8)
				| ((uint32_t)buf[6] << 16)
				| ((uint32_t)buf[7] << 24);
			if (rtx != txid) continue;

			uint8_t family = buf[8];
			if (family != 4) { err = "non-IPv4 response"; break; }

			uint16_t portBE = ((uint16_t)buf[10] << 8) | (uint16_t)buf[11];
			uint32_t ipBE   = ((uint32_t)buf[12] << 24)
				| ((uint32_t)buf[13] << 16)
				| ((uint32_t)buf[14] << 8)
				| ((uint32_t)buf[15]);

			char ipBuf[INET_ADDRSTRLEN] = {};
			in_addr ipAddr;
			ipAddr.s_addr = htonl(ipBE);
#if defined(_WIN32)
			InetNtopA(AF_INET, &ipAddr, ipBuf, sizeof ipBuf);
#else
			inet_ntop(AF_INET, &ipAddr, ipBuf, sizeof ipBuf);
#endif
			char out[64];
			std::snprintf(out, sizeof out, "%s:%u", ipBuf,
				(unsigned)portBE);
			outIpPort = out;
			ND_CLOSE(s);
			return true;
		}
	}

	err = "reflector timeout";
	ND_CLOSE(s);
	return false;
}

std::string DiscoverReflexiveEndpoint(const char* lobbyHost,
                                      uint16_t reflectorPort,
                                      uint16_t localPort,
                                      uint32_t timeoutMs) {
	ReflectorProbe p;
	std::string out, err;
	p.Run(lobbyHost, reflectorPort, localPort, timeoutMs, out, err);
	return out;
}

} // namespace ATNetplay
