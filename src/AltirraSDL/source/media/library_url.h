// AltirraSDL library imports: streamed downloads, retained archives.
#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <vd2/system/VDString.h>

namespace ATLibraryURL {
struct Progress {
	std::atomic<bool> cancel{false};
	std::atomic<uint64_t> received{0}, total{0};
};
struct Result {
	VDStringW path;
	std::string error;
	bool archive = false;
	uint64_t supportedFileCount = 0;
	uint64_t downloadBytes = 0;
};
// Blocking worker operation. cacheDirectory is UTF-8. No UI/core state is
// accessed. ZIP indexes are inspected without extracting/inflating packs;
// each playable member is limited to 64 MiB, with no aggregate pack cap.
Result Download(const std::string& url, const std::string& cacheDirectory,
	Progress& progress);
} // namespace ATLibraryURL
