// A local HTTP fixture supplies data; this driver never stores streamed bodies.
#include <stdafx.h>
#include "http_client.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <thread>

int main(int argc, char** argv) {
	if (argc != 4) return 2;
	ATHttp::Request request;
	request.url = argv[1];
	request.maxBodyBytes = std::strtoull(argv[2], nullptr, 10);
	request.timeoutMs = 15000;
	const std::string mode = argv[3];
	if (mode == "post" || mode == "post-memory" || mode == "post-perform" || mode == "get-forced") {
		request.method = ATHttp::Method::Post;
		request.requestContentType = "application/json";
		request.requestBody = "{\"path\":\"/AltirraSDL/game.xex\",\"host_slot\":0,\"mode\":\"r\"}";
	}
	if (mode == "post-binary") {
		request.requestBody.assign("POST\0DATA", 9);
		request.requestContentType = "application/octet-stream";
	}
	if (mode == "delete-auth") request.method = ATHttp::Method::Delete;
	if (mode == "delete-auth" || mode == "auth")
		request.headers.emplace_back("X-Session-Token", "private-fixture-token");
	if (mode == "bad-header")
		request.headers.emplace_back("X-Session-Token", "bad\r\nInjected: header");
	std::atomic<bool> cancel{false};
	std::atomic<uint64_t> received{123}, total{123};
	request.cancel = &cancel;
	request.bytesReceived = &received;
	request.bytesTotal = &total;
	uint64_t sinkBytes = 0, sum = 0, calls = 0;
	const bool memory = mode == "memory" || mode == "post-memory";
	if (!memory) request.bodySink = [&](const void* data, size_t size) {
		++calls;
		if (mode == "fail") return false;
		if (mode == "throw") throw std::runtime_error("fixture sink error");
		const auto* bytes = (const uint8_t*)data;
		for (size_t i = 0; i < size; ++i) sum += bytes[i];
		sinkBytes += size;
		if (mode == "cancel-in-sink") cancel.store(true);
		return true;
	};
	std::thread canceller;
	if (mode == "cancel") canceller = std::thread([&] {
		std::this_thread::sleep_for(std::chrono::milliseconds(120));
		cancel.store(true);
	});
	ATHttp::Response response;
	if (mode == "post-perform" || mode == "delete-auth") ATHttp::Perform(request, response);
	else if (mode == "post" || mode == "post-memory" || mode == "post-binary") ATHttp::Post(request, response);
	else ATHttp::Get(request, response);
	if (canceller.joinable()) canceller.join();
	if (memory)
		for (const auto byte : response.body) sum += byte;
	std::printf("STATUS %d\nERROR %s\nRECEIVED %llu\nTOTAL %llu\n"
		"SINK_BYTES %llu\nSUM %llu\nBUFFER %llu\nCALLS %llu\n",
		response.status, response.error.c_str(),
		(unsigned long long)received.load(), (unsigned long long)total.load(),
		(unsigned long long)sinkBytes, (unsigned long long)sum,
		(unsigned long long)response.body.size(), (unsigned long long)calls);
}
