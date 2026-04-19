// Altirra SDL3 netplay - LAN discovery (impl)

#include <stdafx.h>

#include "lan_discovery.h"

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
#  define LAN_LAST_ERR()   ((int)WSAGetLastError())
#  define LAN_WOULDBLOCK(e) ((e) == WSAEWOULDBLOCK)
#  define LAN_CLOSE(s)     closesocket((SOCKET)(s))
#  define LAN_INVALID      ((intptr_t)INVALID_SOCKET)
#else
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <fcntl.h>
#  define LAN_LAST_ERR()   (errno)
#  define LAN_WOULDBLOCK(e) ((e) == EAGAIN || (e) == EWOULDBLOCK)
#  define LAN_CLOSE(s)     ::close((int)(s))
#  define LAN_INVALID      ((intptr_t)-1)
#endif

namespace ATNetplay {

// ---------------------------------------------------------------------
// Little-endian helpers (duplicated from protocol.cpp to avoid pulling
// the full protocol header into this one-datagram module).
// ---------------------------------------------------------------------

namespace {

void put_u16(uint8_t* p, uint16_t v) {
	p[0] = (uint8_t)(v & 0xFF);
	p[1] = (uint8_t)((v >> 8) & 0xFF);
}
void put_u32(uint8_t* p, uint32_t v) {
	p[0] = (uint8_t)(v & 0xFF);
	p[1] = (uint8_t)((v >> 8) & 0xFF);
	p[2] = (uint8_t)((v >> 16) & 0xFF);
	p[3] = (uint8_t)((v >> 24) & 0xFF);
}
void put_u64(uint8_t* p, uint64_t v) {
	for (int i = 0; i < 8; ++i) p[i] = (uint8_t)((v >> (i * 8)) & 0xFF);
}
uint16_t get_u16(const uint8_t* p) {
	return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
uint32_t get_u32(const uint8_t* p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
uint64_t get_u64(const uint8_t* p) {
	uint64_t v = 0;
	for (int i = 0; i < 8; ++i) v |= (uint64_t)p[i] << (i * 8);
	return v;
}

void writeFixedString(uint8_t* dst, size_t dstLen, uint8_t& lenOut,
                      const std::string& s) {
	size_t n = s.size();
	if (n > dstLen) n = dstLen;
	std::memset(dst, 0, dstLen);
	if (n) std::memcpy(dst, s.data(), n);
	lenOut = (uint8_t)n;
}

void readFixedString(const uint8_t* src, size_t srcLen, uint8_t len,
                     std::string& out) {
	size_t n = len;
	if (n > srcLen) n = srcLen;
	// Also clamp at first NUL for safety.
	size_t z = 0;
	while (z < n && src[z] != 0) ++z;
	out.assign((const char*)src, z);
}

bool SetNonBlocking(intptr_t s) {
#if defined(_WIN32)
	u_long mode = 1;
	return ioctlsocket((SOCKET)s, FIONBIO, &mode) == 0;
#else
	int f = fcntl((int)s, F_GETFL, 0);
	if (f < 0) return false;
	return fcntl((int)s, F_SETFL, f | O_NONBLOCK) == 0;
#endif
}

} // anonymous

size_t EncodeLanAd(const LanAd& in, uint8_t* buf, size_t bufLen) {
	if (!buf || bufLen < kLanAdSize) return 0;
	std::memset(buf, 0, kLanAdSize);
	uint8_t* p = buf;

	put_u32(p, kLanAdMagic);             p += 4;
	put_u16(p, in.protocolVersion);      p += 2;
	put_u16(p, in.hostPort);             p += 2;
	*p++ = in.visibility;
	*p++ = in.requiresCode;
	*p++ = in.playerCount;
	*p++ = in.maxPlayers;
	put_u64(p, in.osRomHash);            p += 8;
	put_u64(p, in.basicRomHash);         p += 8;
	put_u64(p, in.settingsHash);         p += 8;

	uint8_t cartLen = 0;
	writeFixedString(p + 1, 63, cartLen, in.cartName);
	*p = cartLen;
	p += 1 + 63;

	uint8_t handleLen = 0;
	writeFixedString(p + 1, 31, handleLen, in.hostHandle);
	*p = handleLen;
	p += 1 + 31;

	// cartArtHash: copy raw bytes up to 32, zero-padded.
	size_t n = in.cartArtHash.size();
	if (n > 32) n = 32;
	if (n) std::memcpy(p, in.cartArtHash.data(), n);
	p += 32;
	// reserved[16] already zeroed by memset.
	p += 16;

	return (size_t)(p - buf);  // == kLanAdSize
}

bool DecodeLanAd(const uint8_t* buf, size_t bufLen, LanAd& out) {
	if (!buf || bufLen < kLanAdSize) return false;
	if (get_u32(buf) != kLanAdMagic) return false;

	const uint8_t* p = buf + 4;
	out.protocolVersion = get_u16(p); p += 2;
	out.hostPort        = get_u16(p); p += 2;
	out.visibility      = *p++;
	out.requiresCode    = *p++;
	out.playerCount     = *p++;
	out.maxPlayers      = *p++;
	out.osRomHash       = get_u64(p); p += 8;
	out.basicRomHash    = get_u64(p); p += 8;
	out.settingsHash    = get_u64(p); p += 8;

	uint8_t cartLen = *p;
	readFixedString(p + 1, 63, cartLen, out.cartName);
	p += 1 + 63;

	uint8_t handleLen = *p;
	readFixedString(p + 1, 31, handleLen, out.hostHandle);
	p += 1 + 31;

	// cartArtHash: strip trailing zeros.
	size_t n = 32;
	while (n > 0 && p[n - 1] == 0) --n;
	out.cartArtHash.assign((const char*)p, n);
	p += 32;
	// skip reserved
	p += 16;

	// hostPort == 0 is a sign of a corrupt/unfilled ad; reject.
	if (out.hostPort == 0) return false;

	return true;
}

// ---------------------------------------------------------------------
// LanDiscovery
// ---------------------------------------------------------------------

LanDiscovery::LanDiscovery()
	: mSock(LAN_INVALID), mListenPort(0), mAdvertising(false),
	  mLastSendMs(0) {}

LanDiscovery::~LanDiscovery() {
	Stop();
}

bool LanDiscovery::Begin(uint16_t listenPort) {
	Stop();
	mLastError.clear();

#if defined(_WIN32)
	static bool wsa = false;
	if (!wsa) {
		WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
		wsa = true;
	}
#endif

	intptr_t s = (intptr_t)socket(AF_INET, SOCK_DGRAM, 0);
	if (s == LAN_INVALID) {
		mLastError = "socket() failed";
		return false;
	}
	int yes = 1;
	setsockopt((int)s, SOL_SOCKET, SO_BROADCAST,
		(const char*)&yes, sizeof yes);
	setsockopt((int)s, SOL_SOCKET, SO_REUSEADDR,
		(const char*)&yes, sizeof yes);

	sockaddr_in addr;
	std::memset(&addr, 0, sizeof addr);
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(listenPort);
	if (::bind((int)s, (sockaddr*)&addr, sizeof addr) != 0) {
		LAN_CLOSE(s);
		char buf[64];
		std::snprintf(buf, sizeof buf, "bind(%u) failed: %d",
			(unsigned)listenPort, LAN_LAST_ERR());
		mLastError = buf;
		return false;
	}
	if (!SetNonBlocking(s)) {
		LAN_CLOSE(s);
		mLastError = "set-nonblocking failed";
		return false;
	}

	mSock = s;
	mListenPort = listenPort;
	return true;
}

void LanDiscovery::Stop() {
	if (mSock != LAN_INVALID) {
		LAN_CLOSE(mSock);
		mSock = LAN_INVALID;
	}
	mAdvertising = false;
	mListenPort = 0;
	mLastSendMs = 0;
}

void LanDiscovery::Advertise(const LanAd& ad) {
	mAd = ad;
	mAdvertising = true;
	mLastSendMs = 0;  // force immediate emit on next Poll
}

void LanDiscovery::StopAdvertising() {
	mAdvertising = false;
}

void LanDiscovery::Poll(uint64_t nowMs, std::vector<LanAd>& out) {
	if (mSock == LAN_INVALID) return;

	// Emit heartbeat?
	if (mAdvertising) {
		const uint64_t kIntervalMs = 1000;
		if (mLastSendMs == 0 || nowMs - mLastSendMs >= kIntervalMs) {
			uint8_t buf[kLanAdSize];
			size_t n = EncodeLanAd(mAd, buf, sizeof buf);
			if (n == kLanAdSize) {
				sockaddr_in dst;
				std::memset(&dst, 0, sizeof dst);
				dst.sin_family = AF_INET;
				dst.sin_addr.s_addr = htonl(INADDR_BROADCAST);
				dst.sin_port = htons(mListenPort);
				::sendto((int)mSock, (const char*)buf, (int)n, 0,
					(sockaddr*)&dst, sizeof dst);
			}
			mLastSendMs = nowMs ? nowMs : 1;
		}
	}

	// Drain receive queue.
	for (;;) {
		uint8_t buf[kLanAdSize];
		sockaddr_in from;
		socklen_t fromLen = sizeof from;
#if defined(_WIN32)
		int n = ::recvfrom((SOCKET)mSock, (char*)buf, (int)sizeof buf, 0,
			(sockaddr*)&from, &fromLen);
#else
		ssize_t n = ::recvfrom((int)mSock, buf, sizeof buf, 0,
			(sockaddr*)&from, &fromLen);
#endif
		if (n <= 0) {
			int e = LAN_LAST_ERR();
			if (n < 0 && !LAN_WOULDBLOCK(e)) {
				// Log but keep going.
			}
			break;
		}
		if ((size_t)n < kLanAdSize) continue;

		LanAd ad;
		if (!DecodeLanAd(buf, (size_t)n, ad)) continue;

		// Drop our own ads so we don't list ourselves.
		if (mAdvertising &&
		    ad.hostPort == mAd.hostPort &&
		    ad.osRomHash == mAd.osRomHash &&
		    ad.cartName == mAd.cartName &&
		    ad.hostHandle == mAd.hostHandle) {
			continue;
		}

		char ip[INET_ADDRSTRLEN] = {0};
		inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
		ad.senderIp = ip;
		out.push_back(std::move(ad));
	}
}

} // namespace ATNetplay
