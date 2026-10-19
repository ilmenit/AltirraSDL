//	AltirraSDL - embedded MCP server: minimal HTTP/1.1 server
//
//	Serves the single MCP endpoint over loopback TCP. Non-blocking
//	sockets polled from the SDL main thread (same model as the
//	AltirraBridge transport); no threads.
//
//	Scope is deliberately small: POST bodies framed by Content-Length
//	(no chunked uploads), keep-alive, one request in flight per
//	connection (pipelined requests wait their turn), and responses that
//	can be "parked" -- held open until the server completes them later
//	(used by long-running tool calls such as advance_frames).

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace ATMCP {

struct HttpRequest {
	std::string method;
	std::string target;
	std::string version;
	// Header names are stored lower-cased; values have surrounding
	// whitespace trimmed. Duplicate headers are kept in order.
	std::vector<std::pair<std::string, std::string>> headers;
	std::string body;

	const std::string *FindHeader(const char *lowerName) const;
	int CountHeader(const char *lowerName) const;
};

struct HttpResponse {
	int status = 200;
	std::string contentType;        // empty = no body Content-Type
	std::string body;
	std::vector<std::pair<std::string, std::string>> headers;
	bool closeConnection = false;
};

enum class ListenResult {
	Ok,
	PortInUse,
	Error
};

class HttpServer {
public:
	// Limits (see docs/mcp-server-design.md, Security).
	// Long-lived notification streams (subscriptions/listen, the legacy GET
	// stream) each hold a connection, so leave room for normal requests.
	static constexpr size_t kMaxConnections = 8;
	static constexpr size_t kMaxHeaderBytes = 16 * 1024;
	static constexpr size_t kMaxBodyBytes = 1024 * 1024;
	static constexpr uint64_t kIdleTimeoutMs = 60 * 1000;

	// Return true when out holds the response; false parks the request
	// (the connection stays open until CompleteParked() or close).
	using RequestHandler = std::function<bool(uint32_t connId,
		const HttpRequest& req, HttpResponse& out)>;

	// Called when a connection with a parked request goes away.
	using ParkedClosedHandler = std::function<void(uint32_t connId)>;

	HttpServer();
	~HttpServer();

	HttpServer(const HttpServer&) = delete;
	HttpServer& operator=(const HttpServer&) = delete;

	// Bind 127.0.0.1:port and listen. Never binds other interfaces.
	ListenResult Listen(uint16_t port, int& osError);
	void Close();
	bool IsListening() const;

	void Poll(uint64_t nowMs, const RequestHandler& handler,
		const ParkedClosedHandler& parkedClosed);

	// Send the response for a parked request. Ignored if the connection
	// has gone away.
	void CompleteParked(uint32_t connId, const HttpResponse& resp);

	// Answer a parked request with a Server-Sent Events stream instead:
	// StartEventStream() sends the 200 text/event-stream headers, then
	// SendEvent() writes one "message" event per JSON-RPC message and
	// FinishEventStream() writes the last one and closes the connection
	// once it is flushed. Return false if the connection has gone away.
	bool StartEventStream(uint32_t connId);
	bool SendEvent(uint32_t connId, const std::string& json);
	void FinishEventStream(uint32_t connId, const std::string& finalJson);

	// SSE comment line, used as a keep-alive on idle streams.
	bool SendKeepAlive(uint32_t connId);

	// Drop a parked request without answering it (cancelled by the
	// client); the connection is closed.
	void AbortParked(uint32_t connId);

	bool IsConnectionOpen(uint32_t connId) const;

private:
	struct Connection;

	void AcceptPending(uint64_t nowMs);
	void ServiceConnection(Connection& c, uint64_t nowMs,
		const RequestHandler& handler);
	bool TryParseRequest(Connection& c, HttpRequest& req, HttpResponse& err,
		bool& haveError);
	void QueueResponse(Connection& c, const HttpResponse& resp);
	void FlushOutput(Connection& c);
	void CloseConnection(Connection& c);
	Connection *FindParked(uint32_t connId);

	uintptr_t mListenSock;
	uint32_t mNextConnId = 1;
	std::vector<Connection *> mConnections;
};

// Base64 helpers for the Mcp-Name "=?base64?...?=" sentinel.
bool DecodeHeaderSentinel(const std::string& value, std::string& decoded);

}  // namespace ATMCP
