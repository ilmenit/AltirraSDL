//	AltirraSDL - HTTPS client for online metadata providers
//
//	Blocking HTTP(S) GET/POST, redirects followed, cancellable; optional
//	streaming response sink for large downloads. Also used by metadata.
//
//	This is deliberately NOT the netplay client.  netplay/http_minimal.*
//	is a plain-HTTP, no-TLS, 5-second-bounded client for the lobby
//	directory, and its header explicitly says that anything needing TLS,
//	redirects or compression should use a real library rather than extend
//	it.  This module is that "real library" path: it wraps whatever HTTPS
//	facility the platform already provides, so no new bundled dependency
//	enters the tree.
//
//	  Linux / macOS   libcurl, dlopen()ed at runtime (never link-time, so
//	                  the AppImage uses the host's libcurl and therefore
//	                  the host's CA store)
//	  Windows         WinHTTP (Windows SDK, always present)
//	  Android         java.net.HttpsURLConnection via JNI
//	  WASM            unsupported — Emscripten only offers async fetch,
//	                  and Available() reports false so callers degrade
//
//	Availability is a first-class state, not an error: when Available()
//	is false the metadata feature disables itself with an explanation
//	rather than failing at every call site.
//
//	Threading: requests are blocking and reentrant. Several worker threads
//	may call it concurrently; each call owns its own connection.  The
//	one-time backend initialisation is serialised internally.

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ATHttp {

enum class Method { Get, Post };

struct Request {
	// Absolute URL.  https:// and http:// are both accepted; providers
	// are https, but media CDNs occasionally redirect through http.
	std::string url;
	Method method = Method::Get;
	std::string requestBody;
	std::string requestContentType;

	// Hard deadline for the whole transfer (connect + headers + body).
	uint32_t timeoutMs = 20000;

	// Polled during the transfer.  When it flips true the request is
	// aborted and Response::error is set to "cancelled".  Optional.
	const std::atomic<bool>* cancel = nullptr;

	// Sent as User-Agent.  Providers use this to attribute traffic.
	std::string userAgent;

	// Cap on the response body.  A media URL that unexpectedly returns
	// a huge file is truncated and reported as an error rather than
	// exhausting memory.  0 = no limit.
	uint64_t maxBodyBytes = 32u * 1024u * 1024u;

	// Optional streaming destination. Called synchronously on Get()'s
	// calling thread with decoded chunks from successful HTTP2xx responses.
	// Response::body stays empty; redirects/error bodies never reach it.
	// Return false (or throw) to abort with "download sink failed". The
	// caller owns rollback/removal of any partially written destination.
	std::function<bool(const void*, size_t)> bodySink;

	// Optional transfer counters owned by the caller. Total may remain zero
	// when the server does not supply a length (or uses compression).
	std::atomic<uint64_t>* bytesReceived = nullptr;
	std::atomic<uint64_t>* bytesTotal = nullptr;
};

struct Response {
	int                  status = 0;   // 0 => transport failure, see error
	std::vector<uint8_t> body;
	std::string          contentType;  // lower-cased, parameters stripped
	std::string          error;        // non-empty when the call failed
};

// True when this build/platform can perform HTTPS requests at all.
// Cheap after the first call; safe to poll every frame.
bool Available();

// Human-readable backend name for diagnostics and the UI's
// "unavailable" explanation ("libcurl", "WinHTTP", "Android", "none").
const char* BackendName();

// Blocking request. Perform honors method; Get/Post explicitly select a
// verb. Body sinks and progress describe the response, not the upload.
void Perform(const Request& in, Response& out);
void Get(const Request& in, Response& out);
void Post(const Request& in, Response& out);

// Percent-encode `s` into `out` (appends).  Everything outside the
// unreserved set RFC 3986 defines is escaped, so the result is safe in
// a query-string value.
void UrlEncode(const char* s, size_t n, std::string& out);
void UrlEncode(const std::string& s, std::string& out);

}  // namespace ATHttp
