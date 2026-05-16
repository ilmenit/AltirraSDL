// AltirraBridge - cross-platform transport layer (impl)

#include <stdafx.h>

#include "bridge_transport.h"

#include "bridge_logging.h"

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
#  define BR_LAST_ERR()       (int)WSAGetLastError()
#  define BR_WOULDBLOCK(e)    ((e) == WSAEWOULDBLOCK || (e) == WSAEINPROGRESS)
#  define BR_INTR(e)          ((e) == WSAEINTR)
#  define BR_CLOSE(s)         closesocket((SOCKET)(s))
#  define BR_IS_INVALID(s)    ((SOCKET)(s) == INVALID_SOCKET)
#else
#  include <sys/socket.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <sys/un.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <netdb.h>
#  define BR_LAST_ERR()       (errno)
#  define BR_WOULDBLOCK(e)    ((e) == EAGAIN || (e) == EWOULDBLOCK || (e) == EINPROGRESS)
#  define BR_INTR(e)          ((e) == EINTR)
#  define BR_CLOSE(s)         ::close((int)(s))
#  define BR_IS_INVALID(s)    ((int)(s) < 0)
#endif

namespace ATBridge {

// ---------------------------------------------------------------------------
// Winsock lifecycle
// ---------------------------------------------------------------------------

void InitNetSubsystem() {
#if defined(_WIN32)
	static bool initialised = false;
	if (initialised) return;
	WSADATA wsa;
	int rc = WSAStartup(MAKEWORD(2, 2), &wsa);
	if (rc != 0) {
		BRIDGE_LOG_ERROR("Bridge", "WSAStartup failed: %d", rc);
		// Best-effort: leave initialised=false so the next call retries.
		return;
	}
	initialised = true;
#endif
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

bool SetNonBlocking(SockHandle s) {
#if defined(_WIN32)
	u_long mode = 1;
	return ioctlsocket((SOCKET)s, FIONBIO, &mode) == 0;
#else
	int flags = fcntl((int)s, F_GETFL, 0);
	if (flags < 0) return false;
	return fcntl((int)s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

void SetReuseAddr(SockHandle s) {
	int yes = 1;
	::setsockopt((int)s, SOL_SOCKET, SO_REUSEADDR,
		(const char*)&yes, sizeof yes);
}

bool ParseTcpSpec(const std::string& spec, std::string& host, uint16_t& port) {
	// "tcp" or "tcp:HOST:PORT". HOST is parsed but the actual bind()
	// always uses 127.0.0.1 — exposing the bridge on a non-loopback
	// address is never what we want and would be a security
	// regression. We log a warning if the user explicitly passed
	// something other than 127.0.0.1 / localhost so they're not
	// surprised that --bridge=tcp:0.0.0.0:6502 quietly bound to
	// loopback instead.
	host = "127.0.0.1";
	port = 0;
	if (spec == "tcp" || spec.empty()) return true;

	// "tcp:..."
	if (spec.size() < 4 || spec.substr(0, 4) != "tcp:") return false;
	std::string rest = spec.substr(4);

	// Optional HOST: split off everything after the LAST ':'.
	auto colon = rest.rfind(':');
	if (colon == std::string::npos) {
		// "tcp:54321"
		long p = std::strtol(rest.c_str(), nullptr, 10);
		if (p < 0 || p > 65535) return false;
		port = (uint16_t)p;
		return true;
	}

	// "tcp:HOST:PORT" — accept HOST but force it to loopback.
	std::string requestedHost = rest.substr(0, colon);
	if (!requestedHost.empty() &&
	    requestedHost != "127.0.0.1" &&
	    requestedHost != "localhost") {
		BRIDGE_LOG_INFO("Bridge",
			"address spec asked for host '%s'; binding 127.0.0.1 anyway "
			"(bridge is loopback-only by design)",
			requestedHost.c_str());
	}
	std::string portStr = rest.substr(colon + 1);
	long p = std::strtol(portStr.c_str(), nullptr, 10);
	if (p < 0 || p > 65535) return false;
	port = (uint16_t)p;
	return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------

Transport::Transport() = default;

Transport::~Transport() {
	Shutdown();
}

bool Transport::Listen(const std::string& addrSpec, std::string& boundDescription) {
	InitNetSubsystem();

	if (addrSpec.empty() || addrSpec == "tcp" || addrSpec.substr(0, 4) == "tcp:") {
		// --- TCP ---
		std::string host;
		uint16_t port = 0;
		if (!ParseTcpSpec(addrSpec, host, port)) {
			BRIDGE_LOG_ERROR("Bridge", "Bad address spec: '%s'", addrSpec.c_str());
			return false;
		}

#if defined(_WIN32)
		SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (s == INVALID_SOCKET) {
			BRIDGE_LOG_ERROR("Bridge", "socket() failed: %d", BR_LAST_ERR());
			return false;
		}
#else
		int s = ::socket(AF_INET, SOCK_STREAM, 0);
		if (s < 0) {
			BRIDGE_LOG_ERROR("Bridge", "socket() failed: %d", BR_LAST_ERR());
			return false;
		}
#endif

		SetReuseAddr((SockHandle)s);

		sockaddr_in addr {};
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		// Always loopback. Never 0.0.0.0.
		inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

		if (::bind((int)s, (sockaddr*)&addr, sizeof addr) != 0) {
			BRIDGE_LOG_ERROR("Bridge", "bind() failed: %d", BR_LAST_ERR());
			BR_CLOSE(s);
			return false;
		}

		// Read back the actually-bound port (matters when port=0).
		socklen_t alen = sizeof addr;
		if (::getsockname((int)s, (sockaddr*)&addr, &alen) != 0) {
			BRIDGE_LOG_ERROR("Bridge", "getsockname() failed: %d", BR_LAST_ERR());
			BR_CLOSE(s);
			return false;
		}
		uint16_t boundPort = ntohs(addr.sin_port);

		if (::listen((int)s, 1) != 0) {
			BRIDGE_LOG_ERROR("Bridge", "listen() failed: %d", BR_LAST_ERR());
			BR_CLOSE(s);
			return false;
		}
		if (!SetNonBlocking((SockHandle)s)) {
			BRIDGE_LOG_ERROR("Bridge", "set non-blocking failed: %d", BR_LAST_ERR());
			BR_CLOSE(s);
			return false;
		}

		mListenFd = (SockHandle)s;
		mIsTcp = true;
		char buf[64];
		std::snprintf(buf, sizeof buf, "tcp:127.0.0.1:%u", (unsigned)boundPort);
		boundDescription = buf;
		return true;
	}

#if !defined(_WIN32)
	if (addrSpec.size() > 5 && addrSpec.substr(0, 5) == "unix:") {
		// --- POSIX filesystem UDS ---
		std::string path = addrSpec.substr(5);
		if (path.size() >= sizeof(((sockaddr_un*)0)->sun_path)) {
			BRIDGE_LOG_ERROR("Bridge", "unix path too long: %s", path.c_str());
			return false;
		}
		::unlink(path.c_str());

		int s = ::socket(AF_UNIX, SOCK_STREAM, 0);
		if (s < 0) {
			BRIDGE_LOG_ERROR("Bridge", "socket(AF_UNIX) failed: %d", BR_LAST_ERR());
			return false;
		}

		sockaddr_un addr {};
		addr.sun_family = AF_UNIX;
		std::strncpy(addr.sun_path, path.c_str(), sizeof addr.sun_path - 1);

		if (::bind(s, (sockaddr*)&addr, sizeof addr) != 0) {
			BRIDGE_LOG_ERROR("Bridge", "bind(AF_UNIX) failed: %d", BR_LAST_ERR());
			BR_CLOSE(s);
			return false;
		}
		// Restrict to owner. Best-effort — UDS file permissions vary
		// by platform but on Linux this is enforced.
		::chmod(path.c_str(), 0600);

		if (::listen(s, 1) != 0) {
			BRIDGE_LOG_ERROR("Bridge", "listen(AF_UNIX) failed: %d", BR_LAST_ERR());
			BR_CLOSE(s);
			::unlink(path.c_str());
			return false;
		}
		if (!SetNonBlocking((SockHandle)s)) {
			BR_CLOSE(s);
			::unlink(path.c_str());
			return false;
		}

		mListenFd = (SockHandle)s;
		mUnixPath = path;
		boundDescription = "unix:" + path;
		return true;
	}

#  if defined(__linux__)
	if (addrSpec.size() > 14 && addrSpec.substr(0, 14) == "unix-abstract:") {
		// --- Linux abstract namespace UDS ---
		std::string name = addrSpec.substr(14);
		if (1 + name.size() >= sizeof(((sockaddr_un*)0)->sun_path)) {
			BRIDGE_LOG_ERROR("Bridge", "abstract name too long");
			return false;
		}

		int s = ::socket(AF_UNIX, SOCK_STREAM, 0);
		if (s < 0) {
			BRIDGE_LOG_ERROR("Bridge", "socket(AF_UNIX) failed: %d", BR_LAST_ERR());
			return false;
		}

		sockaddr_un addr {};
		addr.sun_family = AF_UNIX;
		// Leading nul byte = abstract namespace.
		addr.sun_path[0] = '\0';
		std::memcpy(addr.sun_path + 1, name.data(), name.size());
		socklen_t alen = (socklen_t)(offsetof(sockaddr_un, sun_path) + 1 + name.size());

		if (::bind(s, (sockaddr*)&addr, alen) != 0) {
			BRIDGE_LOG_ERROR("Bridge", "bind abstract failed: %d", BR_LAST_ERR());
			BR_CLOSE(s);
			return false;
		}
		if (::listen(s, 1) != 0) {
			BR_CLOSE(s);
			return false;
		}
		if (!SetNonBlocking((SockHandle)s)) {
			BR_CLOSE(s);
			return false;
		}

		mListenFd = (SockHandle)s;
		boundDescription = "unix-abstract:" + name;
		return true;
	}
#  endif  // __linux__
#endif  // !_WIN32

	BRIDGE_LOG_ERROR("Bridge", "Unsupported address spec: '%s'", addrSpec.c_str());
	return false;
}

void Transport::Shutdown() {
	DropClient();
	if (!BR_IS_INVALID(mListenFd)) {
		BR_CLOSE(mListenFd);
		mListenFd = kInvalidSock;
	}
	mIsTcp = false;
#if !defined(_WIN32)
	if (!mUnixPath.empty()) {
		::unlink(mUnixPath.c_str());
		mUnixPath.clear();
	}
#endif
}

bool Transport::TryAccept() {
	if (BR_IS_INVALID(mListenFd))
		return false;

	if (!BR_IS_INVALID(mClientFd)) {
		// Already have one client. Two cases:
		//
		// (a) The current client has spoken at least once (sent ≥1
		//     byte). Treat it as the real client and reject any new
		//     connections so they don't pile up in the listen backlog.
		//     The new client gets a clean EOF.
		//
		// (b) The current client has never sent a byte. On Windows
		//     this happens when Windows Defender / SmartScreen / other
		//     security software probes a newly-opened loopback listener
		//     — it completes the TCP handshake, holds the connection
		//     open, and sends nothing. Without special handling those
		//     probes permanently occupy the single-client slot and the
		//     real client gets rejected with "second client".
		//     Drop the silent holder so the real client can take over.
#if defined(_WIN32)
		SOCKET extra = accept((SOCKET)mListenFd, nullptr, nullptr);
		if (extra != INVALID_SOCKET) {
			if (mClientHasSpoken) {
				BRIDGE_LOG_INFO("Bridge", "Rejecting second client (single-client mode)");
				closesocket(extra);
				return false;
			}
			BRIDGE_LOG_INFO("Bridge", "Evicting silent client, taking new connection");
			DropClient();
			if (!SetNonBlocking((SockHandle)extra)) {
				closesocket(extra);
				return false;
			}
			if (mIsTcp) {
				int yes = 1;
				::setsockopt((int)extra, IPPROTO_TCP, TCP_NODELAY,
					(const char*)&yes, sizeof yes);
			}
			mClientFd = (SockHandle)extra;
			mPendingSend.clear();
			mClientHasSpoken = false;
			BRIDGE_LOG_INFO("Bridge", "Client connected");
			return true;
		}
#else
		int extra = ::accept((int)mListenFd, nullptr, nullptr);
		if (extra >= 0) {
			if (mClientHasSpoken) {
				BRIDGE_LOG_INFO("Bridge", "Rejecting second client (single-client mode)");
				::close(extra);
				return false;
			}
			BRIDGE_LOG_INFO("Bridge", "Evicting silent client, taking new connection");
			DropClient();
			if (!SetNonBlocking((SockHandle)extra)) {
				::close(extra);
				return false;
			}
			if (mIsTcp) {
				int yes = 1;
				::setsockopt((int)extra, IPPROTO_TCP, TCP_NODELAY,
					(const char*)&yes, sizeof yes);
			}
			mClientFd = (SockHandle)extra;
			mPendingSend.clear();
			mClientHasSpoken = false;
			BRIDGE_LOG_INFO("Bridge", "Client connected");
			return true;
		}
#endif
		return false;
	}

#if defined(_WIN32)
	SOCKET c = accept((SOCKET)mListenFd, nullptr, nullptr);
	if (c == INVALID_SOCKET) {
		int e = BR_LAST_ERR();
		if (!BR_WOULDBLOCK(e) && !BR_INTR(e))
			BRIDGE_LOG_ERROR("Bridge", "accept() failed: %d", e);
		return false;
	}
#else
	int c = ::accept((int)mListenFd, nullptr, nullptr);
	if (c < 0) {
		int e = BR_LAST_ERR();
		if (!BR_WOULDBLOCK(e) && !BR_INTR(e))
			BRIDGE_LOG_ERROR("Bridge", "accept() failed: %d", e);
		return false;
	}
#endif
	if (!SetNonBlocking((SockHandle)c)) {
		BR_CLOSE(c);
		return false;
	}

	// Disable Nagle on TCP clients. The bridge protocol is small
	// request/response — without TCP_NODELAY, Nagle interacts with
	// delayed-ACK on the client side and can stall single-byte
	// completions for tens of milliseconds even on loopback.
	// Best-effort: ignore failures (Nagle stalls just become a
	// performance issue, not a correctness issue).
	if (mIsTcp) {
		int yes = 1;
		::setsockopt((int)c, IPPROTO_TCP, TCP_NODELAY,
			(const char*)&yes, sizeof yes);
	}

	mClientFd = (SockHandle)c;
	mPendingSend.clear();
	mClientHasSpoken = false;
	BRIDGE_LOG_INFO("Bridge", "Client connected");
	return true;
}

IoResult Transport::Recv(void* buf, size_t len, size_t* outBytes) {
	*outBytes = 0;
	if (BR_IS_INVALID(mClientFd))
		return IoResult::Error;

#if defined(_WIN32)
	int n = ::recv((SOCKET)mClientFd, (char*)buf, (int)len, 0);
	if (n > 0) { *outBytes = (size_t)n; mClientHasSpoken = true; return IoResult::Ok; }
	if (n == 0) return IoResult::PeerClosed;
	int e = BR_LAST_ERR();
	if (BR_WOULDBLOCK(e) || BR_INTR(e)) return IoResult::WouldBlock;
	return IoResult::Error;
#else
	ssize_t n = ::recv((int)mClientFd, buf, len, 0);
	if (n > 0) { *outBytes = (size_t)n; mClientHasSpoken = true; return IoResult::Ok; }
	if (n == 0) return IoResult::PeerClosed;
	int e = BR_LAST_ERR();
	if (BR_WOULDBLOCK(e) || BR_INTR(e)) return IoResult::WouldBlock;
	return IoResult::Error;
#endif
}

IoResult Transport::SendAll(const void* buf, size_t len) {
	if (BR_IS_INVALID(mClientFd))
		return IoResult::Error;

	// Append to any previously-pending tail and try to flush
	// everything in one go.
	mPendingSend.append((const char*)buf, len);
	const char* p = mPendingSend.data();
	size_t remaining = mPendingSend.size();

	while (remaining > 0) {
#if defined(_WIN32)
		int n = ::send((SOCKET)mClientFd, p, (int)remaining, 0);
#else
		ssize_t n = ::send((int)mClientFd, p, remaining, 0);
#endif
		if (n > 0) {
			p += n;
			remaining -= (size_t)n;
			continue;
		}
		int e = BR_LAST_ERR();
		if (BR_WOULDBLOCK(e) || BR_INTR(e)) {
			// Save the unsent tail; the next SendAll will retry.
			mPendingSend.assign(p, remaining);
			return IoResult::WouldBlock;
		}
		// Hard error.
		mPendingSend.clear();
		return IoResult::Error;
	}
	mPendingSend.clear();
	return IoResult::Ok;
}

void Transport::DropClient() {
	if (!BR_IS_INVALID(mClientFd)) {
		BR_CLOSE(mClientFd);
		mClientFd = kInvalidSock;
		mPendingSend.clear();
		mClientHasSpoken = false;
		BRIDGE_LOG_INFO("Bridge", "Client disconnected");
	}
}

}  // namespace ATBridge
