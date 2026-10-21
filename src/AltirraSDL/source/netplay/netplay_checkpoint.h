// AltirraSDL - local, uncompressed emulator checkpoints.
#pragma once

#include <memory>
#include <cstdint>

namespace ATNetplay {

class Checkpoint final {
public:
	Checkpoint();
	~Checkpoint();
	Checkpoint(const Checkpoint&) = delete;
	Checkpoint& operator=(const Checkpoint&) = delete;

	bool Capture();
	bool Restore() const;
	uint64_t Cycle() const;

private:
	struct Impl;
	std::unique_ptr<Impl> mpImpl;
};

// Main emulation thread only, at a complete frame boundary. Unlike a
// transfer savestate these retain the native object graph without ZIP,
// JSON, preview pixels, or a hardware/profile teardown.
bool CaptureCheckpoint(std::unique_ptr<Checkpoint>& out);
bool RestoreCheckpoint(const Checkpoint& checkpoint);

} // namespace ATNetplay
