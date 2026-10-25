// Exercise the production HTTP transport with real loopback sockets.
#include <stdafx.h>
#include "mcp_http.h"
#include "bridge_transport.h"
#include <stdexcept>
#include <random>
#include <chrono>
#include <thread>
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
using ClientSocket = SOCKET;
static void CloseClient(ClientSocket s) { closesocket(s); }
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
using ClientSocket = int;
static void CloseClient(ClientSocket s) { close(s); }
#endif

static void Require(bool condition, const char *message) {
	if (!condition) throw std::runtime_error(message);
}

int main() {
	try {
		ATBridge::InitNetSubsystem();
		ATMCP::HttpServer server;
		std::random_device random;
		uint16_t port = 0;
		int error = 0;
		for (int i = 0; i < 100; ++i) {
			port = (uint16_t)(18000 + random() % 40000);
			if (server.Listen(port, error) == ATMCP::ListenResult::Ok) break;
		}
		Require(server.IsListening(), "listen failed");
		auto connectClient = [&] {
			ClientSocket client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			sockaddr_in addr {};
			addr.sin_family = AF_INET;
			addr.sin_port = htons(port);
			inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
			Require(connect(client, (sockaddr *)&addr, sizeof addr) == 0, "connect failed");
			return client;
		};
		// A successful send only queues bytes in the client kernel. Let
		// loopback delivery finish before asserting server dispatch state;
		// one immediate nonblocking poll is not sufficient on every OS.
		auto pollUntil = [&](auto poll, auto ready, const char *message) {
			const auto deadline = std::chrono::steady_clock::now()
				+ std::chrono::seconds(5);
			for (;;) {
				poll();
				if (ready())
					return;
				Require(std::chrono::steady_clock::now() < deadline, message);
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		};
		const std::string request = "POST /mcp HTTP/1.1\r\nHost: localhost\r\nContent-Length: 0\r\n\r\n";

		// A pipelined client that never reads must stop dispatching tools
		// when its first large reply fills the socket, then time out.
		ClientSocket client = connectClient();
		std::string pipeline;
		for (int i = 0; i < 20; ++i) pipeline += request;
		Require(send(client, pipeline.data(), (int)pipeline.size(), 0) == (int)pipeline.size(), "send failed");
		unsigned calls = 0;
		uint32_t connection = 0;
		auto respond = [&](uint32_t id, const ATMCP::HttpRequest&, ATMCP::HttpResponse& out) {
			connection = id;
			++calls;
			out.body.assign(4 * 1024 * 1024, 'x');
			return true;
		};
		pollUntil([&] { server.Poll(0, respond, {}); },
			[&] { return connection != 0; }, "request was not dispatched");
		for (int i = 0; i < 100; ++i) server.Poll(i, respond, {});
		Require(calls > 0 && calls < 20, "unread replies did not apply backpressure");
		// A delayed ACK can make one final send progress before the stall.
		// Continue advancing the fake timeout clock until that stops too.
		uint64_t timeoutNow = ATMCP::HttpServer::kIdleTimeoutMs + 200;
		pollUntil([&] {
			server.Poll(timeoutNow, respond, {});
			timeoutNow += ATMCP::HttpServer::kIdleTimeoutMs + 1;
		}, [&] { return !server.IsConnectionOpen(connection); },
			"stalled response did not time out");
		CloseClient(client);

		// Oversized SSE events must close their stream and report the
		// cancelled parked request on the next poll.
		client = connectClient();
		Require(send(client, request.data(), (int)request.size(), 0) == (int)request.size(), "send failed");
		connection = 0;
		auto park = [&](uint32_t id, const ATMCP::HttpRequest&, ATMCP::HttpResponse&) {
			connection = id;
			return false;
		};
		pollUntil([&] { server.Poll(timeoutNow, park, {}); },
			[&] { return connection != 0; }, "stream request was not dispatched");
		Require(connection != 0 && server.StartEventStream(connection), "stream did not start");
		Require(!server.SendEvent(connection, std::string(ATMCP::HttpServer::kMaxOutputBytes, 'x')),
			"oversized stream event was accepted");
		bool cancelled = false;
		server.Poll(timeoutNow + 1, park, [&](uint32_t id) { cancelled = id == connection; });
		Require(cancelled && !server.IsConnectionOpen(connection), "stream cancellation was lost");
		CloseClient(client);
		puts("MCP HTTP backpressure and cancellation passed");
		return 0;
	} catch (const std::exception& e) {
		fprintf(stderr, "%s\n", e.what());
		return 1;
	}
}
