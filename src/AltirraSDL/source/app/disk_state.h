//	AltirraSDL - per-game disk write-state management
//	==================================================
//
//	What this is
//	------------
//	When the user mounts a disk image with `Real R/W`
//	(`kATMediaWriteMode_RW`), we want their writes — high scores, level
//	progress, custom settings — to persist across sessions WITHOUT
//	modifying the source `.atr`/`.atx`/`.xfd` file the user (or Game
//	Library / WASM page / SD card) provided.  Modifying the source has
//	three concrete problems:
//
//	  1. Online play.  The lobby + LAN netplay handshake exchanges
//	     CRC32s of the canonical image bytes; once a host's local file
//	     has been written to, joiners no longer match its CRC32 and the
//	     netplay_cache lookup misses.
//
//	  2. Game Library identity.  `mGameFileCRC32` drifts the moment the
//	     game saves a high score, so on the next launch the entry
//	     looks orphaned and custom art unbinds (custom art is keyed by
//	     the canonical CRC32).
//
//	  3. Read-only sources.  Many real-world sources are not even
//	     writable — `.zip`-extracted entries, content provider URIs,
//	     iOS document picker bookmarks, `/system/` mounts, the WASM
//	     IDBFS-mapped page asset.  A naive RW write would error out.
//
//	The fix is a per-image working copy living under
//	`{configDir}/disk_state/<SHA256_HEX>/`, with two files:
//
//	    disk_state/<SHA256>/
//	        pristine{ext}    - immutable snapshot of the source bytes
//	        disk{ext}        - the mutable working copy, RW-mounted
//
//	A user's writes land in `disk{ext}`.  The `pristine{ext}` copy
//	holds the canonical bytes that identity and netplay reads use, so
//	CRC32-based lookups never drift no matter how much the user plays.
//
//	Why SHA-256 and not CRC32 for the dirname
//	-----------------------------------------
//	CRC32 has a ~1-in-4 billion collision rate per pair.  Across a 10K
//	game library that is a ~1.2% chance of *some* pair colliding; this
//	directory holds user progress, so a collision would silently
//	overwrite save data on launch.  SHA-256 makes that probability
//	cosmically negligible.  CRC32 stays unchanged where the netplay
//	protocol uses it on the wire (and in `mGameFileCRC32`, custom-art
//	keying, `netplay_cache/`) — the two hashes serve different roles.
//
//	Why the dir lives in `{configDir}` and not next to the source
//	-------------------------------------------------------------
//	The source's containing directory might be read-only (zip, SD card,
//	content URI, WASM asset).  `{configDir}` is the one location every
//	platform guarantees is writable; routing every write through it
//	makes the helper source-agnostic.
//
//	On every error path the helper falls back to the source path
//	unchanged so an I/O failure never breaks a mount that would
//	otherwise have worked.  Errors are logged through the runtime-
//	toggleable `DiskState` channel (visible in Tools → Debug Log).

#ifndef ALTIRRASDL_APP_DISK_STATE_H
#define ALTIRRASDL_APP_DISK_STATE_H

#include <vd2/system/VDString.h>
#include <at/atcore/media.h>

// Resolve the path the disk emulator should mount, based on the user's
// write-mode intent.
//
//   * For RO / VRWSafe / VRW intent → returns `sourcePath` unchanged.
//     Those modes never reach the file on disk, so the working-copy
//     dance is pure overhead.
//
//   * For RW intent → returns
//     `{configDir}/disk_state/<SHA256>/disk{ext}`, lazily creating the
//     directory + both files (pristine + working) on first call.  On
//     subsequent calls returns the existing working copy so writes
//     from prior sessions persist.
//
// Falls back to `sourcePath` on any error (hash failure, copy failure,
// path issue) so a bad I/O state never blocks the user from booting.
// All errors are logged through the `DiskState` log channel.
VDStringW ATResolveDiskMount(const wchar_t *sourcePath,
                             ATMediaWriteMode intent);

// Resolve the path identity-sensitive readers should use.
//
// Netplay's host/joiner agreement and Game Library CRC computation
// must see the canonical bytes even after the user's been writing to
// the working copy.  This function returns the pristine path when one
// exists for `sourcePath`'s SHA-256, otherwise returns `sourcePath`
// unchanged.  Never mutates the filesystem.
VDStringW ATResolveDiskCanonical(const wchar_t *sourcePath);

// Restore the working copy from the pristine snapshot.  Used by the
// "Restore Original" UI gesture.  Returns false when pristine is
// missing or the copy fails — callers should show a toast in that
// case.
bool ATRestoreDiskOriginal(const wchar_t *sourcePath);

#endif  // ALTIRRASDL_APP_DISK_STATE_H
