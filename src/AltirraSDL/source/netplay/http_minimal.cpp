// Altirra SDL3 netplay - minimal HTTP/1.1 client (impl)

#include <stdafx.h>

#include "http_minimal.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
   typedef int socklen_t;
#  define HTTP_LAST_ERR()    ((int)WSAGetLastError())
#  define HTTP_WOULDBLOCK(e) ((e) == WSAEWOULDBLOCK || (e) == WSAEINPROGRESS)
#  define HTTP_CLOSE(s)      closesocket((SOCKET)(s))
#  define HTTP_INVALID_SOCK  ((intptr_t)INVALID_SOCKET)
#else
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <sys/select.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <netdb.h>
#  include <signal.h>
#  include <time.h>
#  define HTTP_LAST_ERR()    (errno)
#  define HTTP_WOULDBLOCK(e) ((e) == EAGAIN || (e) == EWOULDBLOCK || (e) == EINPROGRESS)
#  define HTTP_CLOSE(s)      ::close((int)(s))
#  define HTTP_INVALID_SOCK  ((intptr_t)-1)
#endif

namespace ATNetplay {

namespace {

#if defined(_WIN32)
void InitWinsockOnce() {
	static bool done = false;
	if (done) return;
	WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
	done = true;
}
#endif

uint64_t MonotonicMs() {
#if defined(_WIN32)
	return (uint64_t)GetTickCount64();
#else
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000);
#endif
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

bool WaitWritable(intptr_t s, uint32_t timeoutMs) {
	fd_set wfds; FD_ZERO(&wfds); FD_SET((int)s, &wfds);
	struct timeval tv;
	tv.tv_sec  = timeoutMs / 1000;
	tv.tv_usec = (timeoutMs % 1000) * 1000;
	int r = select((int)s + 1, nullptr, &wfds, nullptr, &tv);
	return r > 0 && FD_ISSET((int)s, &wfds);
}

bool WaitReadable(intptr_t s, uint32_t timeoutMs) {
	fd_set rfds; FD_ZERO(&rfds); FD_SET((int)s, &rfds);
	struct timeval tv;
	tv.tv_sec  = timeoutMs / 1000;
	tv.tv_usec = (timeoutMs % 1000) * 1000;
	int r = select((int)s + 1, &rfds, nullptr, nullptr, &tv);
	return r > 0 && FD_ISSET((int)s, &rfds);
}

intptr_t ConnectTcp(const char* host, uint16_t port,
                    uint32_t timeoutMs, std::string& err) {
	struct addrinfo hints, *res = nullptr;
	std::memset(&hints, 0, sizeof hints);
	hints.ai_family   = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	char portStr[16];
	std::snprintf(portStr, sizeof portStr, "%u", (unsigned)port);
	int rc = getaddrinfo(host, portStr, &hints, &res);
	if (rc != 0 || !res) {
		err = "getaddrinfo failed";
		return HTTP_INVALID_SOCK;
	}

	intptr_t s = (intptr_t)socket(res->ai_family, res->ai_socktype,
	                              res->ai_protocol);
	if (s == HTTP_INVALID_SOCK) {
		err = "socket() failed";
		freeaddrinfo(res);
		return HTTP_INVALID_SOCK;
	}
	if (!SetNonBlocking(s)) {
		err = "set-nonblocking failed";
		HTTP_CLOSE(s);
		freeaddrinfo(res);
		return HTTP_INVALID_SOCK;
	}

	int cr = ::connect((int)s, res->ai_addr, (socklen_t)res->ai_addrlen);
	freeaddrinfo(res);
	if (cr == 0) return s;

	int e = HTTP_LAST_ERR();
	if (!HTTP_WOULDBLOCK(e)) {
		err = "connect() failed";
		HTTP_CLOSE(s);
		return HTTP_INVALID_SOCK;
	}
	if (!WaitWritable(s, timeoutMs)) {
		err = "connect timed out";
		HTTP_CLOSE(s);
		return HTTP_INVALID_SOCK;
	}
	// Check SO_ERROR to distinguish connect-completed from connect-failed.
	int soErr = 0; socklen_t sl = sizeof soErr;
	if (getsockopt((int)s, SOL_SOCKET, SO_ERROR, (char*)&soErr, &sl) != 0
	    || soErr != 0) {
		err = "connect failed (SO_ERROR)";
		HTTP_CLOSE(s);
		return HTTP_INVALID_SOCK;
	}
	return s;
}

bool SendAll(intptr_t s, const void* buf, size_t n,
             uint32_t deadlineMs, uint64_t startMs, std::string& err) {
	const uint8_t* p = (const uint8_t*)buf;
	size_t remaining = n;
	while (remaining) {
		uint64_t now = MonotonicMs();
		uint32_t left = (now - startMs >= deadlineMs)
			? 0 : (uint32_t)(deadlineMs - (now - startMs));
		if (left == 0) { err = "send timed out"; return false; }
		if (!WaitWritable(s, left)) { err = "send timed out"; return false; }
#if defined(_WIN32)
		int sent = ::send((SOCKET)s, (const char*)p, (int)remaining, 0);
#else
		ssize_t sent = ::send((int)s, p, remaining, MSG_NOSIGNAL);
#endif
		if (sent <= 0) {
			int e = HTTP_LAST_ERR();
			if (HTTP_WOULDBLOCK(e)) continue;
			err = "send() failed";
			return false;
		}
		p += sent;
		remaining -= (size_t)sent;
	}
	return true;
}

// Parse status line "HTTP/1.1 200 OK\r\n".  Returns first CRLF offset or
// string::npos on malformed input.
size_t ParseStatusLine(const std::string& raw, int& codeOut,
                       std::string& textOut) {
	size_t eol = raw.find("\r\n");
	if (eol == std::string::npos) return std::string::npos;
	// Expect "HTTP/1.x "
	if (raw.compare(0, 5, "HTTP/") != 0) return std::string::npos;
	size_t sp1 = raw.find(' ', 5);
	if (sp1 == std::string::npos || sp1 >= eol) return std::string::npos;
	size_t sp2 = raw.find(' ', sp1 + 1);
	if (sp2 == std::string::npos || sp2 > eol) sp2 = eol;
	std::string code = raw.substr(sp1 + 1, sp2 - sp1 - 1);
	codeOut = std::atoi(code.c_str());
	if (sp2 < eol)
		textOut.assign(raw, sp2 + 1, eol - sp2 - 1);
	else
		textOut.clear();
	return eol;
}

// Case-insensitive prefix compare (for header names).
bool IHasPrefix(const char* s, size_t n, const char* prefix) {
	size_t plen = std::strlen(prefix);
	if (n < plen) return false;
	for (size_t i = 0; i < plen; ++i) {
		char a = s[i], b = prefix[i];
		if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
		if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
		if (a != b) return false;
	}
	return true;
}

std::string ExtractContentType(const std::string& value) {
	// Strip leading whitespace and "; charset=..." suffix.
	size_t a = 0;
	while (a < value.size() && (value[a] == ' ' || value[a] == '\t')) ++a;
	size_t b = value.find(';', a);
	if (b == std::string::npos) b = value.size();
	std::string ct = value.substr(a, b - a);
	for (auto& c : ct) c = (char)std::tolower((unsigned char)c);
	while (!ct.empty() && (ct.back() == ' ' || ct.back() == '\t'))
		ct.pop_back();
	return ct;
}

} // anonymous

void UrlPercentEncode(const char* s, size_t n, std::string& out) {
	static const char hex[] = "0123456789ABCDEF";
	for (size_t i = 0; i < n; ++i) {
		unsigned char c = (unsigned char)s[i];
		bool unreserved =
			(c >= 'A' && c <= 'Z') ||
			(c >= 'a' && c <= 'z') ||
			(c >= '0' && c <= '9') ||
			c == '-' || c == '_' || c == '.' || c == '~';
		if (unreserved) {
			out.push_back((char)c);
		} else {
			out.push_back('%');
			out.push_back(hex[c >> 4]);
			out.push_back(hex[c & 0xF]);
		}
	}
}

void HttpRequestSync(const HttpRequest& in, HttpResponse& out) {
	out.status = 0;
	out.statusText.clear();
	out.body.clear();
	out.contentType.clear();
	out.error.clear();

#if defined(_WIN32)
	InitWinsockOnce();
#else
	// Ignore SIGPIPE for the lifetime of this process; MSG_NOSIGNAL also
	// covers it on Linux but not macOS.  Setting it once is fine.
	static bool sigpipeIgnored = false;
	if (!sigpipeIgnored) { signal(SIGPIPE, SIG_IGN); sigpipeIgnored = true; }
#endif

	if (!in.host || !in.path || in.path[0] != '/' || !in.method) {
		out.error = "bad request";
		return;
	}

	uint64_t startMs = MonotonicMs();
	uint32_t deadline = in.timeoutMs ? in.timeoutMs : 5000;

	intptr_t s = ConnectTcp(in.host, in.port, deadline, out.error);
	if (s == HTTP_INVALID_SOCK) return;

	// Build request.
	std::string req;
	req.reserve(256 + in.bodyLen);
	req += in.method; req += ' ';
	req += in.path;   req += " HTTP/1.1\r\n";
	req += "Host: "; req += in.host;
	if (in.port != 80) {
		char pb[16]; std::snprintf(pb, sizeof pb, ":%u", (unsigned)in.port);
		req += pb;
	}
	req += "\r\n";
	req += "User-Agent: AltirraSDL-netplay/1.0\r\n";
	req += "Accept: application/json\r\n";
	req += "Connection: close\r\n";
	if (in.extraHeader && *in.extraHeader) {
		req += in.extraHeader; req += "\r\n";
	}
	// Only advertise a body if we actually have one to send.  Sending
	// Content-Length: N with no payload causes the server to stall
	// until it times out waiting for the N bytes.
	const bool hasBody = in.bodyLen > 0 && in.body != nullptr;
	if (hasBody) {
		if (in.contentType) {
			req += "Content-Type: "; req += in.contentType; req += "\r\n";
		}
		char cl[48];
		std::snprintf(cl, sizeof cl, "Content-Length: %zu\r\n", in.bodyLen);
		req += cl;
	}
	req += "\r\n";

	if (!SendAll(s, req.data(), req.size(), deadline, startMs, out.error)) {
		HTTP_CLOSE(s);
		return;
	}
	if (hasBody) {
		if (!SendAll(s, in.body, in.bodyLen, deadline, startMs, out.error)) {
			HTTP_CLOSE(s);
			return;
		}
	}

	// Receive.  Strategy: accumulate bytes until \r\n\r\n (headers end),
	// parse Content-Length, then read exactly that many body bytes (or
	// until connection close if no Content-Length).  We reject chunked.
	std::string raw;
	raw.reserve(1024);
	const size_t kMaxResponse = 256 * 1024;  // 256 KB hard cap

	size_t headerEnd = std::string::npos;
	uint8_t buf[4096];
	while (headerEnd == std::string::npos) {
		uint64_t now = MonotonicMs();
		if (now - startMs >= deadline) { out.error = "recv hdr timeout"; HTTP_CLOSE(s); return; }
		uint32_t left = (uint32_t)(deadline - (now - startMs));
		if (!WaitReadable(s, left)) { out.error = "recv hdr timeout"; HTTP_CLOSE(s); return; }
#if defined(_WIN32)
		int n = ::recv((SOCKET)s, (char*)buf, (int)sizeof buf, 0);
#else
		ssize_t n = ::recv((int)s, buf, sizeof buf, 0);
#endif
		if (n == 0) { out.error = "connection closed before headers"; HTTP_CLOSE(s); return; }
		if (n < 0) {
			int e = HTTP_LAST_ERR();
			if (HTTP_WOULDBLOCK(e)) continue;
			out.error = "recv() failed";
			HTTP_CLOSE(s);
			return;
		}
		raw.append((const char*)buf, (size_t)n);
		if (raw.size() > kMaxResponse) {
			out.error = "response too large";
			HTTP_CLOSE(s);
			return;
		}
		headerEnd = raw.find("\r\n\r\n");
	}

	int code = 0;
	std::string statusText;
	size_t eolOfStatus = ParseStatusLine(raw, code, statusText);
	if (eolOfStatus == std::string::npos) {
		out.error = "malformed status line";
		HTTP_CLOSE(s);
		return;
	}
	out.status     = code;
	out.statusText = statusText;

	// Walk header lines between eolOfStatus+2 and headerEnd.
	// int64_t (not POSIX ssize_t) — MSVC's <sys/types.h> doesn't provide ssize_t.
	int64_t contentLength = -1;
	bool chunked = false;
	size_t hpos = eolOfStatus + 2;
	while (hpos < headerEnd) {
		size_t eol = raw.find("\r\n", hpos);
		if (eol == std::string::npos || eol > headerEnd) break;
		const char* line = raw.data() + hpos;
		size_t len = eol - hpos;
		if (IHasPrefix(line, len, "Content-Length:")) {
			std::string v(line + 15, len - 15);
			contentLength = std::atoll(v.c_str());
		} else if (IHasPrefix(line, len, "Transfer-Encoding:")) {
			std::string v(line + 18, len - 18);
			for (auto& c : v) c = (char)std::tolower((unsigned char)c);
			if (v.find("chunked") != std::string::npos) chunked = true;
		} else if (IHasPrefix(line, len, "Content-Type:")) {
			std::string v(line + 13, len - 13);
			out.contentType = ExtractContentType(v);
		}
		hpos = eol + 2;
	}

	if (chunked) {
		out.error = "chunked transfer-encoding not supported";
		HTTP_CLOSE(s);
		return;
	}

	// Bytes after header terminator already in `raw`.
	size_t bodyStart = headerEnd + 4;
	size_t haveBody  = raw.size() - bodyStart;

	auto appendBody = [&](const uint8_t* p, size_t n) {
		if (out.body.size() + n > kMaxResponse) n = kMaxResponse - out.body.size();
		out.body.insert(out.body.end(), p, p + n);
	};
	if (haveBody)
		appendBody((const uint8_t*)raw.data() + bodyStart, haveBody);

	if (contentLength >= 0) {
		while ((int64_t)out.body.size() < contentLength) {
			uint64_t now = MonotonicMs();
			if (now - startMs >= deadline) { out.error = "recv body timeout"; HTTP_CLOSE(s); return; }
			uint32_t left = (uint32_t)(deadline - (now - startMs));
			if (!WaitReadable(s, left)) { out.error = "recv body timeout"; HTTP_CLOSE(s); return; }
#if defined(_WIN32)
			int n = ::recv((SOCKET)s, (char*)buf, (int)sizeof buf, 0);
#else
			ssize_t n = ::recv((int)s, buf, sizeof buf, 0);
#endif
			if (n == 0) break;  // server closed early; report partial
			if (n < 0) {
				int e = HTTP_LAST_ERR();
				if (HTTP_WOULDBLOCK(e)) continue;
				out.error = "recv() failed";
				HTTP_CLOSE(s);
				return;
			}
			appendBody(buf, (size_t)n);
			if (out.body.size() >= kMaxResponse) { out.error = "response too large"; HTTP_CLOSE(s); return; }
		}
	} else {
		// No Content-Length: read until close.
		for (;;) {
			uint64_t now = MonotonicMs();
			if (now - startMs >= deadline) break;
			uint32_t left = (uint32_t)(deadline - (now - startMs));
			if (!WaitReadable(s, left)) break;
#if defined(_WIN32)
			int n = ::recv((SOCKET)s, (char*)buf, (int)sizeof buf, 0);
#else
			ssize_t n = ::recv((int)s, buf, sizeof buf, 0);
#endif
			if (n == 0) break;
			if (n < 0) {
				int e = HTTP_LAST_ERR();
				if (HTTP_WOULDBLOCK(e)) continue;
				break;
			}
			appendBody(buf, (size_t)n);
			if (out.body.size() >= kMaxResponse) break;
		}
	}

	HTTP_CLOSE(s);
}

} // namespace ATNetplay
