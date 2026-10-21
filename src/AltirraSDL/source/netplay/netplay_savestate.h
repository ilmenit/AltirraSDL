// Altirra SDL3 netplay - mid-session savestate capture / apply
//
// Two narrow helpers that bridge the netplay resync protocol
// (packets.h / coordinator.cpp) to the emulator's IATSerializable
// snapshot machinery (simulator.cpp, savestateio.h).  They are
// deliberately non-templated and kept out of coordinator.cpp so that
// the pure-lockstep code can still be exercised by tests without
// pulling in g_sim.
//
// The byte buffer is a ZIP archive that contains the same
// savestate.json object graph used by File → Save State.  Peers that
// pass the v3 BootConfig / firmware-CRC handshake are already
// guaranteed to share hardware + firmware and can therefore
// successfully apply each other's snapshots.

#pragma once

#include <cstdint>
#include <vector>

namespace ATNetplay {

// Capture the current ATSimulator state into `out`.  Returns false on
// any serialization error (in which case `out` is left empty).  The
// simulator should be paused before calling — the snapshot is taken
// at wall-tick granularity, so any Advance() happening concurrently
// would race.
bool CaptureSavestate(std::vector<uint8_t>& out);

// Apply a previously captured snapshot, preserving its exact scheduler
// and randomization state for deterministic continuation.
// Returns false on any deserialization error or if ApplySnapshot
// refused the data — in that case the caller must treat the session
// as terminally desynced.
bool ApplySavestate(const uint8_t* data, size_t len);

} // namespace ATNetplay
