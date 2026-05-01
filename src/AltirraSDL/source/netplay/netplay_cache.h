// AltirraSDL netplay - joiner-side game cache
//
// Tiny key-value store keyed by `(gameFileCRC32, gameExtension)`.
// Used by the joiner to skip the chunked snapshot transfer when it
// already has the game from a prior session.
//
// Layout: `{ATGetConfigDir()}/netplay_cache/{CRC32_8hex}{ext}`.
// Example: `/home/u/.config/altirra/netplay_cache/7E208140.atr`.
//
// The cache is best-effort.  Load returns false on any failure
// (missing file, size mismatch, CRC mismatch) and the joiner falls
// back to the existing chunked download.  Store failures are logged
// and ignored — the user still gets the game, they just don't get
// the speedup next time.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ATNetplay {

// Try to load a cached game image matching `crc32` + `ext` (NUL-padded
// 8-byte field as carried in NetBootConfig.gameExtension; leading dot
// is preserved if present).  On success, `out` is populated with the
// raw bytes and the function returns true; the caller must still
// verify the byte count matches what the host advertised in
// NetWelcome.snapshotBytes.
//
// Implementation also re-CRCs the loaded bytes and rejects on
// mismatch — protects against an externally-edited cache file
// silently corrupting a session.
bool NetplayCacheLoad(uint32_t crc32,
                      const char ext[8],
                      std::vector<uint8_t>& out);

// Atomically write `data[len]` to `{cacheDir}/{crc32}{ext}`.
// Creates the cache directory if needed.  Uses tmp-file +
// SDL_RenamePath, so a process kill mid-write leaves either the
// previous version or no file at all (never a torn write).
// Returns false on any failure.
bool NetplayCacheStore(uint32_t crc32,
                       const char ext[8],
                       const uint8_t* data,
                       size_t len);

} // namespace ATNetplay
