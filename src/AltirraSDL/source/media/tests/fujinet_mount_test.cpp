#include <stdafx.h>
#include "fujinet_mount.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

void ATUILoadRegistry(const wchar_t*) { std::abort(); }
void ATUISaveRegistry(const wchar_t*) { std::abort(); }

int main(int argc, char** argv) {
	if (argc < 4) return 2;
	ATFujiNetMount::Progress progress;
	const std::string mode = argc > 4 ? argv[4] : "";
	if (mode == "pre-cancel") progress.cancel = true;
	std::atomic<bool> done{false};
	std::thread cancel;
	if (mode == "cancel-host" || mode == "cancel-post") cancel = std::thread([&] {
		const auto phase = mode == "cancel-host" ? ATFujiNetMount::Phase::CheckingHost : ATFujiNetMount::Phase::Mounting;
		while (!done.load() && progress.phase.load() != phase)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		if (!done.load()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			progress.cancel = true;
		}
	});
	const auto result = ATFujiNetMount::Run(argv[1], argv[2], (unsigned)std::strtoul(argv[3], nullptr, 10), progress);
	done = true;
	if (cancel.joinable()) cancel.join();
	std::printf("MOUNTED %d\nATTEMPTED %d\nHOST %u\nPATH %s\nERROR %s\n",
		result.mounted, result.mountAttempted, result.hostSlot,
		result.path.c_str(), result.error.c_str());
}
