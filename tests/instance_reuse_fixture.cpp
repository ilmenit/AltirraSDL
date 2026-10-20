// Desktop IPC protocol fixture. GPL v2 or later.
#include "instance_reuse.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

static void PrintHex(const std::string& text) {
	constexpr char hex[] = "0123456789abcdef";
	for (unsigned char c : text)
		std::cout << hex[c >> 4] << hex[c & 15];
	std::cout << '\n';
}

int main(int argc, char** argv) {
	const bool backlog = argc > 1 && std::string(argv[1]) == "backlog-server";
	const bool idle = backlog || (argc > 1 && std::string(argv[1]) == "idle-server");
	const bool server = idle || (argc > 1 && std::string(argv[1]) == "server");
	const bool enabled = argc <= 1 || std::string(argv[1]) != "independent";
	std::string error;
	const auto result = ATInstanceReuseInit(enabled, argc, argv, error);
	if (result == ATInstanceReuseResult::Failed) {
		std::cerr << error << '\n';
		return 2;
	}
	if (result == ATInstanceReuseResult::Forwarded) {
		std::cout << "FORWARDED\n";
		return 0;
	}
	std::cout << "CONTINUE\n" << std::flush;
	if (server) {
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(backlog ? 30 : 10);
		while (std::chrono::steady_clock::now() < deadline) {
			ATInstanceReuseRequest request;
			if (!idle && ATInstanceReusePoll(request)) {
				std::cout << "REQUEST " << request.mArguments.size() << '\n';
				PrintHex(request.mWorkingDirectory);
				for (const auto& arg : request.mArguments) PrintHex(arg);
				std::cout << std::flush;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
	ATInstanceReuseShutdown();
}
