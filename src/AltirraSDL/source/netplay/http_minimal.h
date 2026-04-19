// Altirra SDL3 netplay - minimal HTTP/1.1 client
//
// Scope: one request, one response, one TCP connection.  Blocking with
// a hard overall timeout.  Used for the lobby directory, where:
//
//   - payloads are a few hundred bytes of JSON,
//   - only GET / POST / DELETE are needed,
//   - the server is plain HTTP (no TLS in v1.0),
//   - total per-call latency is bounded by the UI's 5 s spinner.
//
// This is deliberately NOT a general-purpose HTTP stack: no chunked
// transfer, no keep-alive, no redirects, no compression, no TLS.  If
// any of those become required, we should pull in a real library
// rather than extend this one.
//
// Thread safety: none.  Each Request() call opens and closes a fresh
// socket.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ATNetplay {

struct HttpRequest {
	const char* method    = "GET";  // "GET" | "POST" | "DELETE"
	const char* host      = "";     // "92.5.13.40" or "lobby.example.com"
	uint16_t    port      = 80;
	const char* path      = "/";    // starts with '/'
	const char* contentType = nullptr;  // set when body is non-empty
	const uint8_t* body   = nullptr;
	size_t      bodyLen   = 0;
	// Optional extra header line ("Name: value"), without CRLF.  nullptr
	// for none.  At most one — callers collapse multiple into one line.
	const char* extraHeader = nullptr;
	// Hard deadline for connect+send+recv, milliseconds.
	uint32_t    timeoutMs = 5000;
};

struct HttpResponse {
	int                  status = 0;     // 0 → network error; see error
	std::string          statusText;     // "OK", "Not Found", ...
	std::vector<uint8_t> body;           // raw response body bytes
	std::string          contentType;    // lower-cased, params stripped
	std::string          error;          // non-empty on status == 0
};

// Blocking HTTP/1.1 request.  Returns always — inspect out.status and
// out.error to distinguish network / HTTP / success cases.
void HttpRequestSync(const HttpRequest& in, HttpResponse& out);

// URL-percent-encode bytes that aren't in the unreserved set.  The lobby
// REST API never needs this for path segments (session IDs are hex/UUID)
// but it's useful for query-string helpers.  Appends to `out`.
void UrlPercentEncode(const char* s, size_t n, std::string& out);

} // namespace ATNetplay
