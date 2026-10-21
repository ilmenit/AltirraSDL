// Download integration driver; the Python fixture supplies a local HTTP server.
#include <stdafx.h>
#include "media_url.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

// Image detection pulls in the platform registry provider, but downloading
// must never load/save user settings. Fail if that boundary is crossed.
void ATUILoadRegistry(const wchar_t*) { std::abort(); }
void ATUISaveRegistry(const wchar_t*) { std::abort(); }

int main(int argc, char** argv) {
	if (argc < 3) return 2;
	ATMediaURL::Progress progress;
	std::thread cancel;
	if (argc > 3) {
		const int delay = std::atoi(argv[3]);
		cancel = std::thread([&progress, delay] {
			std::this_thread::sleep_for(std::chrono::milliseconds(delay));
			progress.cancel.store(true);
		});
	}
	const auto result = ATMediaURL::Download(argv[1], argv[2], progress);
	if (cancel.joinable()) cancel.join();
	if (!result.error.empty()) {
		std::printf("ERROR %s\n", result.error.c_str());
		return 1;
	}
	std::printf("PATH %s\n", result.path.c_str());
	std::printf("BYTES %llu\n", (unsigned long long)progress.received.load());
	return 0;
}
