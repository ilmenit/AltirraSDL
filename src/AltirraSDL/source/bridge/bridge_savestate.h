// AltirraBridge - save-state capability (slot store + memory I/O)
//
// The path-based STATE_SAVE/LOAD verbs go through ATSimulator's file
// I/O directly. This module adds two capabilities that the analyst
// workflow needs:
//
//   1. **In-memory slots**: a session-scope map of name -> serialized
//      snapshot blob. Save once with STATE_SAVE slot=name, rewind any
//      number of times with STATE_LOAD slot=name. No disk churn. The
//      analyzer toolkit's "checkpoint, probe, rewind, repeat" loop is
//      the canonical use case.
//
//   2. **Inline blobs**: a single STATE_SAVE/LOAD round-trip that
//      keeps the blob in the wire response (base64). Required for
//      transports where client and server don't share a filesystem
//      (Android adb forward, remote ssh, etc.) -- same rationale as
//      the inline mode on SCREENSHOT/RAWSCREEN/MEMDUMP.
//
// Both modes share the same serialization pipeline as the file path
// mode (ATCreateSaveStateSerializer + IVDZipArchiveWriter over a
// memory stream), so a slot blob and a .altstate2 file are byte-
// equivalent and interchangeable -- a slot can be dumped to disk
// after the fact, or a file can be slurped into a slot for fast
// reuse.
//
// Slot storage is session-scope: cleared on simulator destruction
// and not persisted across server restarts. For persistence, use the
// path mode. Slots are unbounded in count and memory footprint; a
// future Phase 6+ option could add a global cap if needed.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

class ATSimulator;

namespace ATBridge {

// ---------------------------------------------------------------------------
// Snapshot metadata.
//
// Captured at save time and cached alongside the blob in the slot
// store. Reported back in the JSON response for STATE_SAVE,
// STATE_LOAD, and STATE_LIST so clients can identify checkpoints
// without re-loading them.
//
// All fields describe the simulator's state at the moment the
// snapshot was taken. Sizes are in bytes.
// ---------------------------------------------------------------------------

struct StateMetadata {
	uint32_t    size      = 0;   // bytes in the serialized blob
	uint64_t    cycle     = 0;   // CPU cycle counter at capture
	uint16_t    pc        = 0;   // CPU PC (insn-fetch register)
	std::string machine;         // "800XL", "5200", ...
	std::string memory;          // "320K", "64K", ...
	bool        basic     = false;
};

// Build a JSON fragment for the metadata fields. Caller is
// responsible for the surrounding object braces and the leading
// "ok":true. Format:
//   "size":N,"cycle":N,"pc":"$xxxx","machine":"...","memory":"...","basic":true|false
// Trailing comma is included so the fragment can be concatenated
// into a larger payload before StripTrailingComma.
std::string FormatMetadataFields(const StateMetadata& m);

// Capture the live simulator's metadata into m. Does not touch the
// blob; used for the in-flight save path.
void CaptureLiveMetadata(ATSimulator& sim, StateMetadata& m);

// ---------------------------------------------------------------------------
// Memory-buffer save/load.
//
// Both functions throw MyError on serialization failure. Callers
// (the command handlers) wrap in try/catch.
// ---------------------------------------------------------------------------

// Serialize the simulator's current state into a fresh .altstate2-
// format blob. Output buffer is replaced (not appended to). On
// return, m holds the captured metadata.
void SaveStateToBuffer(ATSimulator& sim,
                       std::vector<uint8_t>& outBlob,
                       StateMetadata& outMetadata);

// Apply a previously saved blob. The simulator's running state is
// preserved across the load (per the CLAUDE.md invariant). Caller
// can override via the explicit pause/resume on the live sim after
// this returns.
//
// Throws MyError or ATInvalidSaveStateException if the blob is
// malformed.
bool LoadStateFromBuffer(ATSimulator& sim,
                         const uint8_t* blob,
                         size_t blobSize);

// ---------------------------------------------------------------------------
// Slot store.
//
// Session-scope: cleared on Shutdown(). All operations are O(1)
// except List which is O(N).
// ---------------------------------------------------------------------------

struct SlotInfo {
	std::string   name;
	StateMetadata metadata;
};

// Stash a blob under a name. Replaces any existing slot of the same
// name (Save is destructive, like file overwrite). Returns true; no
// failure mode for v1.
bool SlotStore(const std::string& name,
               std::vector<uint8_t>&& blob,
               const StateMetadata& metadata);

// Retrieve a blob by name. Returns nullptr if no such slot. The
// returned pointer is valid until the next SlotStore/SlotDrop/
// SlotClear on the same name.
const std::vector<uint8_t>* SlotFind(const std::string& name);
const StateMetadata*        SlotFindMetadata(const std::string& name);

// Remove one slot. Returns true if a slot was removed.
bool SlotDrop(const std::string& name);

// Remove every slot. Returns the number of slots that were removed.
size_t SlotDropAll();

// Enumerate slots (sorted by name for determinism).
std::vector<SlotInfo> SlotList();

}  // namespace ATBridge
