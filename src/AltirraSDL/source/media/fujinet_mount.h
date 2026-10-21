// Managed FujiNet-PC shortcut. No emulator state is accessed by this worker.
#pragma once
#include "media_url.h"

namespace ATFujiNetMount {
enum class Phase { Preparing, Downloading, Copying, CheckingHost, Mounting, Verifying, Complete };
struct Progress : ATMediaURL::Progress {
	std::atomic<Phase> phase{Phase::Preparing};
};
struct Result {
	std::string error;
	std::string path; // Relative to the private virtual SD, beginning with '/'.
	unsigned hostSlot = 0; // Official mount JSON uses zero-based host slots.
	bool mountAttempted = false;
	bool mounted = false;
};

// Caller must own the managed runtime/state-directory lease for the whole
// call, and cancel/join this worker before stopping/replacing that runtime.
// Cancellation after mountAttempted may leave FujiNet mounted; never boot
// automatically unless mounted is true and the caller's runtime token is
// still current. Local images and HTTP(S) URLs are accepted (64 MiB limit).
Result Run(const std::string& source, const std::string& stateDirectory,
	unsigned webPort, Progress& progress);
} // namespace ATFujiNetMount
