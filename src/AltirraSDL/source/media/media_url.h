// AltirraSDL fork extension: download media for the normal image loader.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ATMediaURL {

constexpr size_t kMaxDownloadBytes = 64u * 1024u * 1024u;

// All counters and cancellation are optional and owned by the caller for
// the duration of Download(). No emulator or UI state is accessed here.
struct Progress {
	std::atomic<bool> cancel{false};
	std::atomic<uint64_t> received{0};
	std::atomic<uint64_t> total{0};
};

struct Result {
	std::string path;
	std::string error;
};

bool IsURL(const std::string& url);
// Blocking; call on a worker. Downloads are bounded, archive contents are
// inspected before publication, and a content-addressed cache retains files
// for recent-image paths and writable-disk state across application restarts.
Result Download(const std::string& url, const std::string& cacheDirectory,
	Progress& progress);

} // namespace ATMediaURL
