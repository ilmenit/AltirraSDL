// Copy-on-write disk contents for speculative netplay frames.
#pragma once

#include <memory>
#include <vector>
#include <vd2/system/refcount.h>
#include <at/atio/diskimage.h>

namespace ATNetplay {

class DiskVersion;
struct DiskCheckpoint {
	vdrefptr<IATDiskImage> image;
	std::shared_ptr<const DiskVersion> version;
};

// The original image remains immutable. Dynamic host-backed disks cannot
// be replayed deterministically and are refused. The returned image has
// one reference owned by the caller. Already wrapped images are reused.
bool CreateRollbackDisk(IATDiskImage* base, IATDiskImage** wrapped);

// Non-proxy images produce an empty token: ordinary user checkpoints
// continue to retain their native image references through the profile.
DiskCheckpoint CaptureDiskCheckpoint(IATDiskImage* image);
bool RestoreDiskCheckpoint(const DiskCheckpoint& checkpoint);
// Stable overlay fingerprint for confirmed-frame desync checks.
uint64 HashRollbackDisk(IATDiskImage* image);
constexpr size_t kMaxDiskOverlayBytes = 32u * 1024u * 1024u;
// Portable sidecar for a network resync; parsing is atomic and does not
// install the version until RestoreDiskCheckpoint is explicitly called.
bool SerializeDiskOverlay(IATDiskImage* image, std::vector<uint8>& out);
bool ParseDiskOverlay(IATDiskImage* image, const uint8* data, size_t length,
	DiskCheckpoint& checkpoint);

} // namespace ATNetplay
