// Altirra SDL netplay lobby HTTP(S) adapter.
// Blocking and bounded by an overall deadline; uses the platform HTTPS
// implementation shared with media downloads. Called only from workers.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ATNetplay {

struct HttpRequest {
	const char* method    = "GET";  // "GET" | "POST" | "DELETE"
	const char* host      = "";     // "lobby.atari.org.pl" or other lobby host
	uint16_t    port      = 80;
	bool        secure    = false;
	const char* basePath  = "";
	const char* path      = "/";    // starts with '/'
	const char* contentType = nullptr;  // set when body is non-empty
	const uint8_t* body   = nullptr;
	size_t      bodyLen   = 0;
	// Optional extra header line ("Name: value"), without CRLF.  nullptr
	// for none.  At most one — callers collapse multiple into one line.
	const char* extraHeader = nullptr;
	// Hard deadline for connect+send+recv, milliseconds.
	uint32_t    timeoutMs = 5000;
	const std::atomic<bool>* cancel = nullptr;
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
