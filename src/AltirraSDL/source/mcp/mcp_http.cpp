//	AltirraSDL - embedded MCP server: minimal HTTP/1.1 server
//	See mcp_http.h.

#include <stdafx.h>

#include "mcp_http.h"
#include "bridge_transport.h"   // ATBridge::InitNetSubsystem (Winsock init)
#include "bridge_protocol.h"    // ATBridge::Base64Decode
#include "logging.h"

#include <algorithm>
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
#  define MCP_LAST_ERR()      (int)WSAGetLastError()
#  define MCP_WOULDBLOCK(e)   ((e) == WSAEWOULDBLOCK || (e) == WSAEINPROGRESS)
#  define MCP_INTR(e)         ((e) == WSAEINTR)
#  define MCP_ADDRINUSE(e)    ((e) == WSAEADDRINUSE || (e) == WSAEACCES)
#  define MCP_CLOSE(s)        closesocket((SOCKET)(s))
#  define MCP_SEND_FLAGS      0
#else
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <fcntl.h>
#  define MCP_LAST_ERR()      (errno)
#  define MCP_WOULDBLOCK(e)   ((e) == EAGAIN || (e) == EWOULDBLOCK || (e) == EINPROGRESS)
#  define MCP_INTR(e)         ((e) == EINTR)
#  define MCP_ADDRINUSE(e)    ((e) == EADDRINUSE || (e) == EACCES)
#  define MCP_CLOSE(s)        ::close((int)(s))
#  if defined(MSG_NOSIGNAL)
#    define MCP_SEND_FLAGS    MSG_NOSIGNAL
#  else
#    define MCP_SEND_FLAGS    0
#  endif
#endif

namespace ATMCP {

namespace {

constexpr uintptr_t kInvalidSock = ~(uintptr_t)0;

// Requests handled per connection per Poll(), so a chatty client
// cannot starve emulation.
constexpr int kMaxRequestsPerPoll = 4;

bool SetNonBlocking(uintptr_t s) {
#if defined(_WIN32)
	u_long mode = 1;
	return ioctlsocket((SOCKET)s, FIONBIO, &mode) == 0;
#else
	const int flags = fcntl((int)s, F_GETFL, 0);
	if (flags < 0)
		return false;
	return fcntl((int)s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

void ConfigureAcceptedSocket(uintptr_t s) {
	SetNonBlocking(s);

	// Request/response traffic: don't let Nagle delay small replies.
	int yes = 1;
#if defined(_WIN32)
	::setsockopt((SOCKET)s, IPPROTO_TCP, TCP_NODELAY, (const char *)&yes, sizeof yes);
#else
	::setsockopt((int)s, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof yes);
#endif

#if defined(SO_NOSIGPIPE)
	// macOS has no MSG_NOSIGNAL; suppress SIGPIPE per socket instead.
	::setsockopt((int)s, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof yes);
#endif
}

const char *ReasonPhrase(int status) {
	switch (status) {
		case 100: return "Continue";
		case 200: return "OK";
		case 202: return "Accepted";
		case 204: return "No Content";
		case 400: return "Bad Request";
		case 401: return "Unauthorized";
		case 403: return "Forbidden";
		case 404: return "Not Found";
		case 405: return "Method Not Allowed";
		case 408: return "Request Timeout";
		case 411: return "Length Required";
		case 413: return "Content Too Large";
		case 415: return "Unsupported Media Type";
		case 431: return "Request Header Fields Too Large";
		case 500: return "Internal Server Error";
		case 501: return "Not Implemented";
		case 503: return "Service Unavailable";
		case 505: return "HTTP Version Not Supported";
		default:  return "Unknown";
	}
}

std::string ToLower(std::string s) {
	for (char& c : s) {
		if (c >= 'A' && c <= 'Z')
			c = (char)(c + 32);
	}
	return s;
}

std::string Trim(const std::string& s) {
	size_t a = 0, b = s.size();
	while (a < b && (s[a] == ' ' || s[a] == '\t'))
		++a;
	while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t'))
		--b;
	return s.substr(a, b - a);
}

bool ContainsToken(const std::string& headerValue, const char *token) {
	// Comma-separated, case-insensitive token list (e.g. Connection).
	const std::string lower = ToLower(headerValue);
	size_t start = 0;
	while (start <= lower.size()) {
		size_t end = lower.find(',', start);
		if (end == std::string::npos)
			end = lower.size();
		if (Trim(lower.substr(start, end - start)) == token)
			return true;
		start = end + 1;
	}
	return false;
}

HttpResponse MakeError(int status, const char *text, bool close) {
	HttpResponse r;
	r.status = status;
	r.contentType = "text/plain; charset=utf-8";
	r.body = text;
	r.body += "\n";
	r.closeConnection = close;
	return r;
}

}  // namespace

// ---------------------------------------------------------------------------

const std::string *HttpRequest::FindHeader(const char *lowerName) const {
	for (const auto& h : headers) {
		if (h.first == lowerName)
			return &h.second;
	}
	return nullptr;
}

int HttpRequest::CountHeader(const char *lowerName) const {
	int n = 0;
	for (const auto& h : headers) {
		if (h.first == lowerName)
			++n;
	}
	return n;
}

bool DecodeHeaderSentinel(const std::string& value, std::string& decoded) {
	static const char kPrefix[] = "=?base64?";
	static const char kSuffix[] = "?=";
	const size_t pre = sizeof kPrefix - 1;
	const size_t suf = sizeof kSuffix - 1;

	if (value.size() >= pre + suf
		&& value.compare(0, pre, kPrefix) == 0
		&& value.compare(value.size() - suf, suf, kSuffix) == 0)
	{
		std::vector<uint8_t> bytes;
		if (!ATBridge::Base64Decode(value.substr(pre, value.size() - pre - suf), bytes))
			return false;
		decoded.assign(bytes.begin(), bytes.end());
		return true;
	}

	decoded = value;
	return true;
}

// ---------------------------------------------------------------------------

struct HttpServer::Connection {
	uintptr_t   sock = kInvalidSock;
	uint32_t    id = 0;
	std::string in;
	std::string out;
	uint64_t    lastActivityMs = 0;
	bool        parked = false;
	bool        closeAfterFlush = false;
	bool        peerClosed = false;
	bool        dead = false;
	bool        sentContinue = false;
	bool        streaming = false;      // answering with an SSE stream
};

HttpServer::HttpServer()
	: mListenSock(kInvalidSock)
{
}

HttpServer::~HttpServer() {
	Close();
}

ListenResult HttpServer::Listen(uint16_t port, int& osError) {
	Close();
	osError = 0;

	ATBridge::InitNetSubsystem();

#if defined(_WIN32)
	SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == INVALID_SOCKET) {
		osError = MCP_LAST_ERR();
		return ListenResult::Error;
	}

	// SO_REUSEADDR on Windows would let us bind a port another process
	// is already listening on; exclusive use makes "port in use" fail
	// as it should.
	BOOL exclusive = TRUE;
	setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&exclusive, sizeof exclusive);
#else
	int s = ::socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0) {
		osError = MCP_LAST_ERR();
		return ListenResult::Error;
	}

	// Allow an immediate rebind after a restart (TIME_WAIT). On POSIX
	// this does not allow binding over an active listener.
	int yes = 1;
	::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
#endif

	sockaddr_in addr {};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	// Always loopback. Never 0.0.0.0.
	inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

	if (::bind(s, (sockaddr *)&addr, sizeof addr) != 0) {
		osError = MCP_LAST_ERR();
		MCP_CLOSE(s);
		return MCP_ADDRINUSE(osError) ? ListenResult::PortInUse : ListenResult::Error;
	}

	if (::listen(s, 8) != 0 || !SetNonBlocking((uintptr_t)s)) {
		osError = MCP_LAST_ERR();
		MCP_CLOSE(s);
		return ListenResult::Error;
	}

	mListenSock = (uintptr_t)s;
	return ListenResult::Ok;
}

void HttpServer::Close() {
	for (Connection *c : mConnections) {
		if (c->sock != kInvalidSock)
			MCP_CLOSE(c->sock);
		delete c;
	}
	mConnections.clear();

	if (mListenSock != kInvalidSock) {
		MCP_CLOSE(mListenSock);
		mListenSock = kInvalidSock;
	}
}

bool HttpServer::IsListening() const {
	return mListenSock != kInvalidSock;
}

bool HttpServer::IsConnectionOpen(uint32_t connId) const {
	for (const Connection *c : mConnections) {
		if (c->id == connId)
			return !c->dead;
	}
	return false;
}

void HttpServer::AcceptPending(uint64_t nowMs) {
	for (;;) {
#if defined(_WIN32)
		SOCKET cs = accept((SOCKET)mListenSock, nullptr, nullptr);
		if (cs == INVALID_SOCKET)
			return;
#else
		int cs = ::accept((int)mListenSock, nullptr, nullptr);
		if (cs < 0) {
			if (MCP_INTR(errno))
				continue;
			return;
		}
#endif

		ConfigureAcceptedSocket((uintptr_t)cs);

		if (mConnections.size() >= kMaxConnections) {
			static const char kBusy[] =
				"HTTP/1.1 503 Service Unavailable\r\n"
				"Content-Type: text/plain; charset=utf-8\r\n"
				"Content-Length: 22\r\n"
				"Connection: close\r\n"
				"\r\n"
				"Too many connections\r\n";
			::send(cs, kBusy, (int)(sizeof kBusy - 1), MCP_SEND_FLAGS);
			MCP_CLOSE(cs);
			continue;
		}

		Connection *c = new Connection;
		c->sock = (uintptr_t)cs;
		c->id = mNextConnId++;
		if (!mNextConnId)
			mNextConnId = 1;
		c->lastActivityMs = nowMs;
		mConnections.push_back(c);
	}
}

bool HttpServer::TryParseRequest(Connection& c, HttpRequest& req,
	HttpResponse& err, bool& haveError)
{
	haveError = false;

	const size_t headerEnd = c.in.find("\r\n\r\n");
	if (headerEnd == std::string::npos) {
		if (c.in.size() > kMaxHeaderBytes) {
			err = MakeError(431, "Request headers too large", true);
			haveError = true;
		}
		return false;
	}

	if (headerEnd > kMaxHeaderBytes) {
		err = MakeError(431, "Request headers too large", true);
		haveError = true;
		return false;
	}

	// Request line.
	const size_t lineEnd = c.in.find("\r\n");
	const std::string requestLine = c.in.substr(0, lineEnd);
	const size_t sp1 = requestLine.find(' ');
	const size_t sp2 = sp1 == std::string::npos ? std::string::npos : requestLine.find(' ', sp1 + 1);
	if (sp1 == std::string::npos || sp2 == std::string::npos
		|| requestLine.find(' ', sp2 + 1) != std::string::npos)
	{
		err = MakeError(400, "Malformed request line", true);
		haveError = true;
		return false;
	}

	req = HttpRequest();
	req.method = requestLine.substr(0, sp1);
	req.target = requestLine.substr(sp1 + 1, sp2 - sp1 - 1);
	req.version = requestLine.substr(sp2 + 1);

	if (req.version != "HTTP/1.1" && req.version != "HTTP/1.0") {
		err = MakeError(505, "Only HTTP/1.0 and HTTP/1.1 are supported", true);
		haveError = true;
		return false;
	}

	// Header fields.
	size_t pos = lineEnd + 2;
	while (pos < headerEnd) {
		size_t eol = c.in.find("\r\n", pos);
		if (eol == std::string::npos || eol > headerEnd)
			eol = headerEnd;
		const std::string line = c.in.substr(pos, eol - pos);
		pos = eol + 2;

		if (line.empty())
			continue;

		// Obsolete line folding is not accepted (RFC 9112 5.2).
		if (line[0] == ' ' || line[0] == '\t') {
			err = MakeError(400, "Folded header lines are not supported", true);
			haveError = true;
			return false;
		}

		const size_t colon = line.find(':');
		if (colon == std::string::npos || colon == 0) {
			err = MakeError(400, "Malformed header line", true);
			haveError = true;
			return false;
		}

		std::string name = line.substr(0, colon);
		if (name.find(' ') != std::string::npos || name.find('\t') != std::string::npos) {
			err = MakeError(400, "Malformed header name", true);
			haveError = true;
			return false;
		}

		req.headers.emplace_back(ToLower(name), Trim(line.substr(colon + 1)));
	}

	if (req.FindHeader("transfer-encoding")) {
		err = MakeError(501, "Chunked request bodies are not supported; send Content-Length", true);
		haveError = true;
		return false;
	}

	size_t contentLength = 0;
	bool haveLength = false;
	for (const auto& h : req.headers) {
		if (h.first != "content-length")
			continue;

		if (h.second.empty() || h.second.size() > 10
			|| h.second.find_first_not_of("0123456789") != std::string::npos)
		{
			err = MakeError(400, "Invalid Content-Length", true);
			haveError = true;
			return false;
		}

		const size_t v = (size_t)std::strtoull(h.second.c_str(), nullptr, 10);
		if (haveLength && v != contentLength) {
			err = MakeError(400, "Conflicting Content-Length headers", true);
			haveError = true;
			return false;
		}

		contentLength = v;
		haveLength = true;
	}

	if (contentLength > kMaxBodyBytes) {
		err = MakeError(413, "Request body too large (limit 1 MB)", true);
		haveError = true;
		return false;
	}

	if (req.method == "POST" && !haveLength) {
		err = MakeError(411, "Content-Length required", true);
		haveError = true;
		return false;
	}

	const size_t bodyStart = headerEnd + 4;
	if (c.in.size() - bodyStart < contentLength) {
		// Body not complete yet. Honour Expect: 100-continue once so
		// clients like curl send the body without a delay.
		const std::string *expect = req.FindHeader("expect");
		if (expect && ToLower(*expect) == "100-continue" && !c.sentContinue) {
			c.out += "HTTP/1.1 100 Continue\r\n\r\n";
			c.sentContinue = true;
		}
		return false;
	}

	req.body = c.in.substr(bodyStart, contentLength);
	c.in.erase(0, bodyStart + contentLength);
	c.sentContinue = false;

	const std::string *conn = req.FindHeader("connection");
	if (req.version == "HTTP/1.0")
		c.closeAfterFlush = !(conn && ContainsToken(*conn, "keep-alive"));
	else
		c.closeAfterFlush = conn && ContainsToken(*conn, "close");

	return true;
}

void HttpServer::QueueResponse(Connection& c, const HttpResponse& resp) {
	const bool close = resp.closeConnection || c.closeAfterFlush;

	char statusLine[64];
	std::snprintf(statusLine, sizeof statusLine, "HTTP/1.1 %d %s\r\n",
		resp.status, ReasonPhrase(resp.status));

	std::string out = statusLine;
	if (!resp.contentType.empty()) {
		out += "Content-Type: ";
		out += resp.contentType;
		out += "\r\n";
	}

	out += "Content-Length: ";
	out += std::to_string(resp.body.size());
	out += "\r\n";
	out += "Cache-Control: no-store\r\n";

	for (const auto& h : resp.headers) {
		out += h.first;
		out += ": ";
		out += h.second;
		out += "\r\n";
	}

	out += close ? "Connection: close\r\n" : "Connection: keep-alive\r\n";
	out += "\r\n";
	out += resp.body;

	c.out += out;
	if (close)
		c.closeAfterFlush = true;
}

void HttpServer::FlushOutput(Connection& c) {
	while (!c.out.empty()) {
#if defined(_WIN32)
		const int n = ::send((SOCKET)c.sock, c.out.data(), (int)std::min<size_t>(c.out.size(), 1 << 20), 0);
#else
		const ssize_t n = ::send((int)c.sock, c.out.data(), std::min<size_t>(c.out.size(), 1 << 20), MCP_SEND_FLAGS);
#endif
		if (n > 0) {
			c.out.erase(0, (size_t)n);
			continue;
		}

		const int e = MCP_LAST_ERR();
		if (n < 0 && MCP_INTR(e))
			continue;
		if (n < 0 && MCP_WOULDBLOCK(e))
			return;

		c.dead = true;
		return;
	}

	if (c.closeAfterFlush && !c.parked)
		c.dead = true;
}

void HttpServer::CloseConnection(Connection& c) {
	if (c.sock != kInvalidSock) {
		MCP_CLOSE(c.sock);
		c.sock = kInvalidSock;
	}
	c.dead = true;
}

void HttpServer::ServiceConnection(Connection& c, uint64_t nowMs,
	const RequestHandler& handler)
{
	// Read whatever has arrived. A parked connection is still read so a
	// client disconnect (cancellation) is noticed promptly.
	char buf[16384];
	for (int i = 0; i < 64 && !c.peerClosed; ++i) {
#if defined(_WIN32)
		const int n = ::recv((SOCKET)c.sock, buf, (int)sizeof buf, 0);
#else
		const ssize_t n = ::recv((int)c.sock, buf, sizeof buf, 0);
#endif
		if (n > 0) {
			c.lastActivityMs = nowMs;
			c.in.append(buf, (size_t)n);
			if (c.in.size() > kMaxHeaderBytes + kMaxBodyBytes + 4) {
				QueueResponse(c, MakeError(413, "Request too large", true));
				c.in.clear();
				break;
			}
			continue;
		}

		if (n == 0) {
			c.peerClosed = true;
			break;
		}

		const int e = MCP_LAST_ERR();
		if (MCP_INTR(e))
			continue;
		if (!MCP_WOULDBLOCK(e)) {
			c.peerClosed = true;
			c.dead = true;
		}
		break;
	}

	// Handle complete requests in order, one in flight at a time.
	for (int i = 0; i < kMaxRequestsPerPoll && !c.parked && !c.dead && !c.closeAfterFlush; ++i) {
		HttpRequest req;
		HttpResponse resp;
		bool haveError = false;
		if (!TryParseRequest(c, req, resp, haveError)) {
			if (haveError) {
				QueueResponse(c, resp);
				c.in.clear();
			}
			break;
		}

		if (handler(c.id, req, resp))
			QueueResponse(c, resp);
		else
			c.parked = true;
	}

	if (!c.dead)
		FlushOutput(c);

	// A peer that closed with no parked request and nothing left to
	// send is done. (A parked request is reported by Poll().)
	if (c.peerClosed && !c.parked && (c.out.empty() || c.dead))
		c.dead = true;

	// Idle keep-alive connections are dropped after the timeout. A parked
	// request is the server's to finish, so it never idles out here.
	if (!c.parked && c.out.empty() && nowMs - c.lastActivityMs > kIdleTimeoutMs)
		c.dead = true;
}

void HttpServer::Poll(uint64_t nowMs, const RequestHandler& handler,
	const ParkedClosedHandler& parkedClosed)
{
	if (mListenSock == kInvalidSock)
		return;

	AcceptPending(nowMs);

	// Iterate by index: the handler never adds connections, but parked
	// completion paths may run during it.
	for (size_t i = 0; i < mConnections.size(); ++i) {
		Connection& c = *mConnections[i];
		if (!c.dead)
			ServiceConnection(c, nowMs, handler);

		// A parked request whose client hung up is cancelled.
		if (c.parked && (c.peerClosed || c.dead)) {
			c.parked = false;
			c.dead = true;
			if (parkedClosed)
				parkedClosed(c.id);
		}
	}

	for (auto it = mConnections.begin(); it != mConnections.end();) {
		Connection *c = *it;
		if (c->dead) {
			CloseConnection(*c);
			delete c;
			it = mConnections.erase(it);
		} else {
			++it;
		}
	}
}

HttpServer::Connection *HttpServer::FindParked(uint32_t connId) {
	for (Connection *c : mConnections) {
		if (c->id == connId && c->parked && !c->dead)
			return c;
	}
	return nullptr;
}

bool HttpServer::StartEventStream(uint32_t connId) {
	Connection *c = FindParked(connId);
	if (!c)
		return false;
	if (c->streaming)
		return true;

	// The stream is delimited by closing the connection (no
	// Content-Length), which every HTTP/1.1 client understands.
	c->out +=
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: text/event-stream\r\n"
		"Cache-Control: no-store\r\n"
		"X-Accel-Buffering: no\r\n"
		"Connection: close\r\n"
		"\r\n";
	c->streaming = true;
	FlushOutput(*c);
	return !c->dead;
}

bool HttpServer::SendEvent(uint32_t connId, const std::string& json) {
	Connection *c = FindParked(connId);
	if (!c || !c->streaming)
		return false;

	c->out += "event: message\ndata: ";
	c->out += json;
	c->out += "\n\n";
	FlushOutput(*c);
	return !c->dead;
}

bool HttpServer::SendKeepAlive(uint32_t connId) {
	Connection *c = FindParked(connId);
	if (!c || !c->streaming)
		return false;

	c->out += ":\n\n";
	FlushOutput(*c);
	return !c->dead;
}

void HttpServer::FinishEventStream(uint32_t connId, const std::string& finalJson) {
	if (!SendEvent(connId, finalJson))
		return;

	Connection *c = FindParked(connId);
	c->parked = false;
	c->closeAfterFlush = true;
	FlushOutput(*c);
}

void HttpServer::AbortParked(uint32_t connId) {
	if (Connection *c = FindParked(connId)) {
		c->parked = false;
		c->out.clear();
		c->dead = true;
	}
}

void HttpServer::CompleteParked(uint32_t connId, const HttpResponse& resp) {
	for (Connection *c : mConnections) {
		if (c->id != connId || c->dead)
			continue;

		c->parked = false;
		QueueResponse(*c, resp);
		FlushOutput(*c);
		return;
	}
}

}  // namespace ATMCP
