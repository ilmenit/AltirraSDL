// Altirra SDL3 netplay - NAT-PMP / PCP port mapping (impl)
//
// NAT-PMP (RFC 6886) wire format — UDP to gateway:5351.
//
//   Request (external address query, 2 bytes):
//     [version=0, op=0]
//   Response (12 bytes):
//     [version=0, op=128, result_be_u16, epoch_be_u32, extIp_be_u32]
//
//   Request (map UDP port, 12 bytes):
//     [version=0, op=1, reserved_be_u16=0,
//      internalPort_be_u16, suggestedExternalPort_be_u16,
//      lifetime_be_u32]
//   Response (16 bytes):
//     [version=0, op=129, result_be_u16, epoch_be_u32,
//      internalPort_be_u16, externalPort_be_u16, lifetime_be_u32]
//
// PCP (RFC 6887) uses version=2 on the same port (5351).  Many
// modern routers support BOTH; we try PCP first (richer semantics),
// fall back to NAT-PMP, fall back to failure.  This file implements
// NAT-PMP; PCP adds an additional ~60 byte request/response but
// the practical difference for our use case (map a UDP port, get
// the external address) is small enough that NAT-PMP alone
// satisfies most routers.  PCP upgrade is a follow-up.

#include <stdafx.h>

#include "port_mapping.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <iphlpapi.h>
   typedef int socklen_t;
#  define PM_LAST_ERR()     ((int)WSAGetLastError())
#  define PM_CLOSE(s)       closesocket((SOCKET)(s))
#  define PM_INVALID_SOCK   ((intptr_t)INVALID_SOCKET)
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <poll.h>
#  include <cstdio>
#  define PM_LAST_ERR()     (errno)
#  define PM_CLOSE(s)       ::close((int)(s))
#  define PM_INVALID_SOCK   ((intptr_t)-1)
#endif

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#  include <sys/sysctl.h>
#  include <net/route.h>
#  include <net/if.h>
#  include <net/if_dl.h>
#endif

namespace ATNetplay {

namespace {

constexpr uint16_t kNatPmpPort = 5351;

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

// -------------------------------------------------------------------
// Default-gateway discovery.
//
// The router that owns our NAT mapping is the default gateway — the
// next-hop for 0.0.0.0.  Each platform answers this query
// differently; none of these calls is expensive.  Returns the
// gateway IP in network byte order (htonl form) or 0 on failure.
// -------------------------------------------------------------------

#if defined(_WIN32)

uint32_t DiscoverDefaultGateway() {
	// GetBestRoute to 8.8.8.8: Windows returns the interface + next
	// hop that would serve that destination.
	MIB_IPFORWARDROW row = {};
	DWORD rc = ::GetBestRoute(
		htonl(0x08080808),   // 8.8.8.8
		0,
		&row);
	if (rc != NO_ERROR) return 0;
	return (uint32_t)row.dwForwardNextHop;   // already network-byte-order
}

#elif defined(__linux__) || defined(__ANDROID__)

uint32_t DiscoverDefaultGateway() {
	// /proc/net/route is the simplest cross-distro way.  Columns
	// (whitespace-separated): Iface Destination Gateway Flags ...
	// — all hex, little-endian ASCII (!).  Pick the first row whose
	// Destination is 00000000 (default route).
	std::FILE* f = std::fopen("/proc/net/route", "r");
	if (!f) return 0;
	char buf[512];
	// Skip header.
	if (!std::fgets(buf, sizeof buf, f)) { std::fclose(f); return 0; }
	uint32_t gw = 0;
	while (std::fgets(buf, sizeof buf, f)) {
		char iface[64];
		unsigned long dest = 0, gateway = 0, flags = 0;
		if (std::sscanf(buf, "%63s %lx %lx %lx",
		                iface, &dest, &gateway, &flags) < 4) continue;
		if (dest == 0) {
			// /proc/net/route stores addresses in HOST byte order on
			// little-endian (the kernel just writes the bytes of the
			// __be32 as hex).  htonl would double-swap; the raw
			// uint32_t *is* already network-order.
			gw = (uint32_t)gateway;
			break;
		}
	}
	std::fclose(f);
	return gw;
}

#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)

uint32_t DiscoverDefaultGateway() {
	int mib[7] = { CTL_NET, PF_ROUTE, 0, AF_INET, NET_RT_FLAGS,
	               RTF_GATEWAY, 0 };
	size_t len = 0;
	if (sysctl(mib, 6, nullptr, &len, nullptr, 0) < 0) return 0;
	if (len == 0) return 0;
	std::vector<char> tbl(len);
	if (sysctl(mib, 6, tbl.data(), &len, nullptr, 0) < 0) return 0;

	for (char* p = tbl.data(); p < tbl.data() + len; ) {
		rt_msghdr* rtm = reinterpret_cast<rt_msghdr*>(p);
		sockaddr* sa = reinterpret_cast<sockaddr*>(rtm + 1);
		sockaddr* gw = nullptr;
		sockaddr* dst = nullptr;
		for (int i = 0; i < RTAX_MAX; ++i) {
			if (rtm->rtm_addrs & (1 << i)) {
				if (i == RTAX_DST) dst = sa;
				else if (i == RTAX_GATEWAY) gw = sa;
				sa = (sockaddr*)((char*)sa +
					((sa->sa_len + sizeof(long) - 1) &
					 ~(sizeof(long) - 1)));
			}
		}
		if (dst && dst->sa_family == AF_INET &&
		    reinterpret_cast<sockaddr_in*>(dst)->sin_addr.s_addr == 0 &&
		    gw && gw->sa_family == AF_INET) {
			uint32_t addr = reinterpret_cast<sockaddr_in*>(gw)
				->sin_addr.s_addr;
			return addr;
		}
		p += rtm->rtm_msglen;
	}
	return 0;
}

#else

uint32_t DiscoverDefaultGateway() { return 0; }

#endif

bool WaitReadable(intptr_t s, uint32_t timeoutMs) {
#if defined(_WIN32)
	fd_set rf; FD_ZERO(&rf); FD_SET((SOCKET)s, &rf);
	timeval tv;
	tv.tv_sec  = (long)(timeoutMs / 1000);
	tv.tv_usec = (long)((timeoutMs % 1000) * 1000);
	return ::select(0, &rf, nullptr, nullptr, &tv) > 0;
#else
	pollfd pfd{};
	pfd.fd = (int)s;
	pfd.events = POLLIN;
	int rc = ::poll(&pfd, 1, (int)timeoutMs);
	return rc > 0 && (pfd.revents & POLLIN);
#endif
}

// Return raw uint32 (network byte order) → dotted quad.
std::string NetworkOrderToDotted(uint32_t be) {
	in_addr ia;
	ia.s_addr = be;
	char buf[INET_ADDRSTRLEN] = {};
#if defined(_WIN32)
	InetNtopA(AF_INET, &ia, buf, sizeof buf);
#else
	inet_ntop(AF_INET, &ia, buf, sizeof buf);
#endif
	return buf;
}

uint16_t ReadBE16(const uint8_t* p) { return (uint16_t)((p[0]<<8)|p[1]); }
uint32_t ReadBE32(const uint8_t* p) {
	return ((uint32_t)p[0]<<24) | ((uint32_t)p[1]<<16)
	     | ((uint32_t)p[2]<<8)  | ((uint32_t)p[3]);
}
void WriteBE16(uint8_t* p, uint16_t v) {
	p[0] = (uint8_t)(v>>8); p[1] = (uint8_t)(v);
}
void WriteBE32(uint8_t* p, uint32_t v) {
	p[0] = (uint8_t)(v>>24); p[1] = (uint8_t)(v>>16);
	p[2] = (uint8_t)(v>>8);  p[3] = (uint8_t)(v);
}

// -------------------------------------------------------------------
// NAT-PMP exchange.
// -------------------------------------------------------------------
//
// Per RFC 6886 §3.1 the client retransmits at 250/500/1000/2000/4000
// ms if no reply arrives.  We use a shorter, 3-try budget — if the
// router doesn't answer in ~1.5 s it almost certainly doesn't speak
// NAT-PMP, and we want to move on to the reflector probe.

bool DoNatPmp(uint32_t gatewayBE, uint16_t internalPort,
              uint32_t lifetimeSec, PortMapping& out,
              std::string& err) {
	EnsureNetSubsystem();

#if defined(_WIN32)
	SOCKET raw = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (raw == INVALID_SOCKET) { err = "socket() failed"; return false; }
	intptr_t s = (intptr_t)raw;
#else
	int raw = ::socket(AF_INET, SOCK_DGRAM, 0);
	if (raw < 0) { err = "socket() failed"; return false; }
	intptr_t s = (intptr_t)raw;
#endif

	sockaddr_in gw {};
	gw.sin_family = AF_INET;
	gw.sin_port   = htons(kNatPmpPort);
	gw.sin_addr.s_addr = gatewayBE;

	// ---- Query 1: external address (op=0).  Gives us the WAN IP. --
	uint8_t addrReq[2] = { 0, 0 };
	uint8_t resp[16] = {};
	std::string extIp;

	const uint32_t kTriesMs[] = { 250, 500, 750 };
	bool gotAddr = false;
	for (uint32_t tm : kTriesMs) {
		int n = ::sendto((int)s, (const char*)addrReq, (int)sizeof addrReq,
			0, (const sockaddr*)&gw, sizeof gw);
		if (n != (int)sizeof addrReq) { err = "send failed"; break; }
		if (!WaitReadable(s, tm)) continue;

		sockaddr_in from {};
		socklen_t flen = sizeof from;
		n = ::recvfrom((int)s, (char*)resp, (int)sizeof resp, 0,
			(sockaddr*)&from, &flen);
		if (n < 12) continue;
		if (from.sin_addr.s_addr != gatewayBE) continue;
		if (resp[0] != 0 || resp[1] != 128) continue;   // version/op
		uint16_t result = ReadBE16(resp + 2);
		if (result != 0) { err = "NAT-PMP refused addr query"; break; }
		uint32_t extBE = *reinterpret_cast<const uint32_t*>(resp + 8);
		extIp = NetworkOrderToDotted(extBE);
		gotAddr = true;
		break;
	}

	if (!gotAddr) {
		if (err.empty()) err = "NAT-PMP gateway did not respond";
		PM_CLOSE(s);
		return false;
	}

	// ---- Query 2: map UDP port (op=1) with lifetime hint. ---------
	uint8_t mapReq[12] = {};
	mapReq[0] = 0;     // version
	mapReq[1] = 1;     // op = map UDP
	// reserved (2 bytes) = 0
	WriteBE16(mapReq + 4, internalPort);
	WriteBE16(mapReq + 6, internalPort);   // suggested external
	WriteBE32(mapReq + 8, lifetimeSec);

	bool gotMap = false;
	for (uint32_t tm : kTriesMs) {
		int n = ::sendto((int)s, (const char*)mapReq, (int)sizeof mapReq,
			0, (const sockaddr*)&gw, sizeof gw);
		if (n != (int)sizeof mapReq) { err = "send failed"; break; }
		if (!WaitReadable(s, tm)) continue;

		sockaddr_in from {};
		socklen_t flen = sizeof from;
		n = ::recvfrom((int)s, (char*)resp, (int)sizeof resp, 0,
			(sockaddr*)&from, &flen);
		if (n < 16) continue;
		if (from.sin_addr.s_addr != gatewayBE) continue;
		if (resp[0] != 0 || resp[1] != 129) continue;
		uint16_t result = ReadBE16(resp + 2);
		if (result != 0) { err = "NAT-PMP refused mapping"; break; }
		uint16_t internal = ReadBE16(resp + 8);
		uint16_t external = ReadBE16(resp + 10);
		uint32_t lease    = ReadBE32(resp + 12);
		(void)internal;

		out.protocol     = "NAT-PMP";
		out.externalIp   = extIp;
		out.externalPort = external;
		out.lifetimeSec  = lease;
		out.internalPort = internalPort;
		gotMap = true;
		break;
	}

	PM_CLOSE(s);
	if (!gotMap && err.empty()) err = "NAT-PMP mapping timeout";
	return gotMap;
}

// -------------------------------------------------------------------
// PCP (RFC 6887) MAP opcode.
// -------------------------------------------------------------------
//
// PCP supersedes NAT-PMP.  On port 5351 both coexist: a PCP-only
// router replies to a NAT-PMP (version 0) request with result code
// UNSUPP_VERSION, and vice-versa a NAT-PMP-only router replies to a
// PCP (version 2) request with UNSUPP_VERSION.  We try PCP first;
// fall back to NAT-PMP on UNSUPP_VERSION or silence.  Wire format per
// RFC 6887 §7 and §11: 24-byte common header + 36-byte MAP body =
// 60 bytes request, 60 bytes response.
//
// Client IP in the header must be the source address we use to reach
// the gateway — we discover it by connect()+getsockname() after
// creating the socket, since the gateway is IPv4 and we want an
// IPv4-mapped IPv6 address in the 16-byte slot.

// Write an IPv4 (network-byte-order) into an IPv6 slot as an IPv4-
// mapped IPv6 address (::ffff:a.b.c.d), per RFC 4291 §2.5.5.2.  The
// slot must be at least 16 bytes.
void WriteIPv4MappedIPv6(uint8_t slot[16], uint32_t ipv4BE) {
	std::memset(slot, 0, 10);
	slot[10] = 0xFF;
	slot[11] = 0xFF;
	std::memcpy(slot + 12, &ipv4BE, 4);
}

// Extract the IPv4 address from an IPv6 slot if it is IPv4-mapped;
// otherwise return 0.  `out` receives network-byte-order IPv4.
bool ReadIPv4FromMappedIPv6(const uint8_t slot[16], uint32_t& out) {
	for (int i = 0; i < 10; ++i) if (slot[i] != 0) return false;
	if (slot[10] != 0xFF || slot[11] != 0xFF) return false;
	std::memcpy(&out, slot + 12, 4);
	return true;
}

// Discover our own source IP for packets destined to `gatewayBE` by
// creating a connected UDP socket (no data sent) and reading back
// the kernel-assigned local address.  Returns network-byte-order
// IPv4, or 0 on failure.
uint32_t DiscoverOwnSourceIp(uint32_t gatewayBE) {
#if defined(_WIN32)
	SOCKET raw = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (raw == INVALID_SOCKET) return 0;
	intptr_t s = (intptr_t)raw;
#else
	int raw = ::socket(AF_INET, SOCK_DGRAM, 0);
	if (raw < 0) return 0;
	intptr_t s = (intptr_t)raw;
#endif
	sockaddr_in to {};
	to.sin_family = AF_INET;
	to.sin_port   = htons(kNatPmpPort);
	to.sin_addr.s_addr = gatewayBE;
	uint32_t result = 0;
	if (::connect((int)s, (const sockaddr*)&to, sizeof to) == 0) {
		sockaddr_in me {};
		socklen_t mlen = sizeof me;
		if (::getsockname((int)s, (sockaddr*)&me, &mlen) == 0) {
			result = me.sin_addr.s_addr;
		}
	}
	PM_CLOSE(s);
	return result;
}

// PCP common-header + MAP-body result codes.  Only the ones we act
// on are listed here; any other non-zero code is treated as failure.
constexpr uint8_t kPcpResultSuccess        = 0;
constexpr uint8_t kPcpResultUnsuppVersion  = 1;

bool DoPcp(uint32_t gatewayBE, uint32_t ownSourceBE, uint16_t internalPort,
           uint32_t lifetimeSec, PortMapping& out,
           std::string& err, bool& peerUnsuppVersion) {
	peerUnsuppVersion = false;
	EnsureNetSubsystem();

#if defined(_WIN32)
	SOCKET raw = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (raw == INVALID_SOCKET) { err = "socket() failed"; return false; }
	intptr_t s = (intptr_t)raw;
#else
	int raw = ::socket(AF_INET, SOCK_DGRAM, 0);
	if (raw < 0) { err = "socket() failed"; return false; }
	intptr_t s = (intptr_t)raw;
#endif

	sockaddr_in gw {};
	gw.sin_family = AF_INET;
	gw.sin_port   = htons(kNatPmpPort);
	gw.sin_addr.s_addr = gatewayBE;

	// Build a 60-byte MAP request.  Layout:
	//   [ 0] version=2
	//   [ 1] R=0, opcode=1           → 0x01
	//   [ 2] reserved (2)
	//   [ 4] requested lifetime (be32)
	//   [ 8] client IP (IPv4-mapped IPv6, 16 bytes)
	//   [24] nonce (12 bytes)
	//   [36] protocol=17 (UDP)
	//   [37] reserved (3)
	//   [40] internal port (be16)
	//   [42] suggested external port (be16)
	//   [44] suggested external IP (:: for "no preference", 16 bytes)
	uint8_t req[60] = {};
	req[0] = 2;     // version
	req[1] = 0x01;  // R=0, opcode=1 MAP
	WriteBE32(req + 4, lifetimeSec);
	WriteIPv4MappedIPv6(req + 8, ownSourceBE);

	// 96-bit nonce: seeded from wall-clock + PID so a rapid-retry loop
	// in the same process generates distinct values.  The nonce only
	// needs to match the response to correlate it — no cryptographic
	// property is required by the protocol.
	{
		uint64_t t = MonoMillis();
#if defined(_WIN32)
		uint32_t pid = (uint32_t)GetCurrentProcessId();
#else
		uint32_t pid = (uint32_t)::getpid();
#endif
		WriteBE32(req + 24 +  0, (uint32_t)(t & 0xFFFFFFFFu));
		WriteBE32(req + 24 +  4, (uint32_t)((t >> 32) & 0xFFFFFFFFu));
		WriteBE32(req + 24 +  8, pid ^ (uint32_t)(t * 0x9E3779B1u));
	}

	req[36] = 17;   // UDP
	WriteBE16(req + 40, internalPort);
	WriteBE16(req + 42, internalPort);   // suggested external
	// req[44..59] already zero (suggested external IP = ::)

	uint8_t resp[64] = {};
	const uint32_t kTriesMs[] = { 250, 500, 750 };
	bool gotMap = false;

	for (uint32_t tm : kTriesMs) {
		int n = ::sendto((int)s, (const char*)req, (int)sizeof req,
			0, (const sockaddr*)&gw, sizeof gw);
		if (n != (int)sizeof req) { err = "send failed"; break; }
		if (!WaitReadable(s, tm)) continue;

		sockaddr_in from {};
		socklen_t flen = sizeof from;
		n = ::recvfrom((int)s, (char*)resp, (int)sizeof resp, 0,
			(sockaddr*)&from, &flen);
		if (n < 60) continue;
		if (from.sin_addr.s_addr != gatewayBE) continue;
		if (resp[0] != 2) {
			// Router is NAT-PMP-only and echoed back its own v0
			// response; treat as "PCP unsupported, fall back".
			peerUnsuppVersion = true;
			break;
		}
		if (resp[1] != 0x81) continue;   // not a MAP response
		uint8_t result = resp[3];
		if (result == kPcpResultUnsuppVersion) {
			peerUnsuppVersion = true;
			break;
		}
		if (result != kPcpResultSuccess) {
			err = "PCP MAP refused (result code " + std::to_string((int)result) + ")";
			break;
		}
		// Correlate nonce (bytes 24..35 of response body at offset 24 -
		// common-header is 24 bytes so body starts at 24; nonce is
		// first 12 bytes of body).  Drop mismatches — another client
		// on the LAN might have a MAP in flight with the same port.
		if (std::memcmp(resp + 24, req + 24, 12) != 0) continue;

		uint32_t lifeGranted = ReadBE32(resp + 4);
		uint16_t assignedExt = ReadBE16(resp + 24 + 18);
		uint32_t extIpBE = 0;
		std::string extIpStr;
		if (ReadIPv4FromMappedIPv6(resp + 24 + 20, extIpBE) && extIpBE != 0) {
			extIpStr = NetworkOrderToDotted(extIpBE);
		}

		out.protocol     = "PCP";
		out.externalIp   = extIpStr;
		out.externalPort = assignedExt;
		out.lifetimeSec  = lifeGranted;
		out.internalPort = internalPort;
		gotMap = true;
		break;
	}

	PM_CLOSE(s);
	if (!gotMap && err.empty() && !peerUnsuppVersion)
		err = "PCP gateway did not respond";
	return gotMap;
}

// Release the mapping we just acquired via PCP by re-issuing MAP
// with the same nonce and lifetime=0.  Best-effort, no retry.
void DoPcpRelease(uint32_t gatewayBE, uint32_t ownSourceBE,
                  const PortMapping& m) {
	EnsureNetSubsystem();
#if defined(_WIN32)
	SOCKET raw = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (raw == INVALID_SOCKET) return;
	intptr_t s = (intptr_t)raw;
#else
	int raw = ::socket(AF_INET, SOCK_DGRAM, 0);
	if (raw < 0) return;
	intptr_t s = (intptr_t)raw;
#endif
	sockaddr_in gw {};
	gw.sin_family = AF_INET;
	gw.sin_port   = htons(kNatPmpPort);
	gw.sin_addr.s_addr = gatewayBE;

	uint8_t req[60] = {};
	req[0] = 2;
	req[1] = 0x01;
	// lifetime = 0 → release
	WriteIPv4MappedIPv6(req + 8, ownSourceBE);
	// We don't have the original nonce stashed (ReleaseUdpPortMapping's
	// PortMapping carries enough for NAT-PMP — nonce would be a new
	// field).  The PCP server's MAP-DELETE semantics key on
	// (protocol, internalPort, clientIP) + any matching nonce, so
	// using a fresh nonce with lifetime=0 tells it to find any
	// mapping for this (proto, port, client) and drop it.
	{
		uint64_t t = MonoMillis();
		WriteBE32(req + 24, (uint32_t)(t & 0xFFFFFFFFu));
		WriteBE32(req + 28, (uint32_t)((t >> 32) & 0xFFFFFFFFu));
		WriteBE32(req + 32, 0xDEADBEEFu);
	}
	req[36] = 17;
	WriteBE16(req + 40, m.internalPort);
	WriteBE16(req + 42, m.externalPort);
	::sendto((int)s, (const char*)req, (int)sizeof req, 0,
		(const sockaddr*)&gw, sizeof gw);
	PM_CLOSE(s);
}

} // anon

bool RequestUdpPortMapping(uint16_t internalPort,
                           uint32_t lifetimeSec,
                           PortMapping& out,
                           std::string& err) {
	out = {};
	err.clear();

	if (internalPort == 0) { err = "internal port is 0"; return false; }

	uint32_t gw = DiscoverDefaultGateway();
	if (gw == 0) { err = "could not discover gateway"; return false; }

	// Try PCP first — it is the current IETF standard, covers strict
	// modern routers (Apple AirPort internally, many OpenWrt/pfSense
	// builds), and its MAP response is a strict superset of
	// NAT-PMP's.  On UNSUPP_VERSION (router is NAT-PMP-only) or
	// silence (router doesn't listen on 5351 at all), fall through.
	uint32_t ownSrc = DiscoverOwnSourceIp(gw);
	bool unsuppVersion = false;
	std::string pcpErr;
	if (ownSrc != 0 &&
	    DoPcp(gw, ownSrc, internalPort, lifetimeSec, out, pcpErr,
	          unsuppVersion)) {
		return true;
	}

	// NAT-PMP (RFC 6886, version 0) — the legacy path for routers
	// that never picked up PCP.  Covers Apple AirPort (pre-PCP),
	// OpenWrt without miniupnpd, legacy TP-Link / ASUS firmwares.
	std::string pmpErr;
	if (DoNatPmp(gw, internalPort, lifetimeSec, out, pmpErr)) {
		return true;
	}

	// Neither path succeeded.  Surface whichever error is most
	// informative: if PCP saw an UNSUPP_VERSION the router speaks
	// PCP only partially, and NAT-PMP's error is what the caller
	// needs to see; otherwise prefer PCP's error (it was the newer,
	// preferred attempt).
	if (!pcpErr.empty() && !unsuppVersion) err = pcpErr;
	else if (!pmpErr.empty())              err = pmpErr;
	else                                   err = "router did not respond to PCP or NAT-PMP";
	return false;
}

void ReleaseUdpPortMapping(const PortMapping& m) {
	if (m.internalPort == 0 || m.protocol.empty()) return;

	uint32_t gw = DiscoverDefaultGateway();
	if (gw == 0) return;

	// Route to the matching protocol.  If the mapping was granted via
	// PCP, a NAT-PMP release packet would be ignored (unsupported
	// version on a PCP-strict router) and the mapping would linger
	// until its lease expired naturally.
	if (m.protocol == "PCP") {
		uint32_t ownSrc = DiscoverOwnSourceIp(gw);
		if (ownSrc != 0) DoPcpRelease(gw, ownSrc, m);
		return;
	}

	EnsureNetSubsystem();

#if defined(_WIN32)
	SOCKET raw = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (raw == INVALID_SOCKET) return;
	intptr_t s = (intptr_t)raw;
#else
	int raw = ::socket(AF_INET, SOCK_DGRAM, 0);
	if (raw < 0) return;
	intptr_t s = (intptr_t)raw;
#endif

	sockaddr_in dst {};
	dst.sin_family = AF_INET;
	dst.sin_port   = htons(kNatPmpPort);
	dst.sin_addr.s_addr = gw;

	// NAT-PMP release: same map request with lifetime=0.
	uint8_t req[12] = {};
	req[0] = 0;
	req[1] = 1;
	WriteBE16(req + 4, m.internalPort);
	WriteBE16(req + 6, m.externalPort);
	WriteBE32(req + 8, 0);   // lifetime = release
	::sendto((int)s, (const char*)req, (int)sizeof req, 0,
		(const sockaddr*)&dst, sizeof dst);
	// Best-effort: don't wait for a reply.  If the router is gone,
	// the mapping expires naturally.
	PM_CLOSE(s);
}

} // namespace ATNetplay
