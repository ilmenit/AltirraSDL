//	AltirraSDL - per-game disk write-state management (impl)
//	========================================================
//
//	See disk_state.h for the design rationale.  This file implements
//	the three public helpers using SDL3 file APIs (works uniformly on
//	Windows, Linux, macOS, Android internal storage, and WASM IDBFS).
//
//	Internal layout under `{configDir}/disk_state/`:
//
//	    disk_state/
//	        <SHA256 of source bytes, 64 hex chars>/
//	            pristine{ext}    - immutable, written exactly once
//	            disk{ext}        - working copy, RW-mounted by emulator
//
//	The SHA-256 is computed from raw source bytes via the existing
//	`ATChecksumEngineSHA256` (see `at/atcore/checksum.h`); the same
//	hash is used elsewhere in the codebase by the compatibility engine
//	(`compatengine.cpp`).  We compute it ourselves rather than relying
//	on `IATImage::GetImageFileSHA256()` so the helper works before any
//	image is loaded into the simulator (the call sites resolve the
//	mount path before reaching `g_sim.Load`).

#include <stdafx.h>

#include "disk_state.h"

#include <cstdio>
#include <cstring>
#include <string>

#include <SDL3/SDL.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>

#include <vd2/system/VDString.h>
#include <vd2/system/filesys.h>     // VDFileSplitExtRightSpan, VDDoesPathExist
#include <vd2/system/text.h>        // VDTextWToU8 / VDTextU8ToW

#include <at/atcore/checksum.h>     // ATChecksumEngineSHA256
#include <at/atcore/logging.h>      // ATLogChannel
#include <at/atcore/media.h>        // ATMediaWriteMode, flag bits

extern VDStringA ATGetConfigDir();

// Runtime-toggleable log channel — shows up in Tools > Debug Log and
// can be enabled at runtime if a user reports "my high scores didn't
// save".  Disabled by default so successful mounts stay quiet.
ATLogChannel g_ATLCDiskState(false, false, "DiskState",
	"Per-game disk write-state (RW working copies, pristine snapshots)");

namespace {

// ----------------------------------------------------------------------
// Path helpers
// ----------------------------------------------------------------------

// Returns the lowercase extension of `path` including the leading dot
// (e.g. ".atr") or an empty string if the path has no extension.  The
// extension is needed for two reasons: (1) the disk emulator picks the
// loader based on the extension via `ATGetImageTypeForFileExtension`,
// so the working copy must keep one; (2) it makes the on-disk layout
// self-documenting when a developer browses `disk_state/`.
VDStringW LowerExt(const wchar_t *path) {
	VDStringSpanW spanExt = VDFileSplitExtRightSpan(VDStringSpanW(path));
	VDStringW ext(spanExt);
	for (auto &c : ext) {
		if (c >= L'A' && c <= L'Z')
			c = (wchar_t)(c + 32);
	}
	return ext;
}

// Platform-appropriate path separator.  We append '/' on POSIX/WASM
// and '\\' on Windows.  SDL itself happily takes either on Windows but
// the netplay_cache and configdir helpers in this codebase already
// branch on _WIN32 so we stay consistent.
inline char PathSep() {
#ifdef _WIN32
	return '\\';
#else
	return '/';
#endif
}

// Builds `{configDir}/disk_state` (no trailing separator).  Used both
// as a mkdir target and as the parent of the per-image subdirectories.
VDStringA DiskStateRoot() {
	VDStringA root = ATGetConfigDir();
	root += PathSep();
	root += "disk_state";
	return root;
}

VDStringA PerImageDir(const VDStringA &sha256Hex) {
	VDStringA dir = DiskStateRoot();
	dir += PathSep();
	dir += sha256Hex;
	return dir;
}

// ----------------------------------------------------------------------
// Recognise paths that are already inside the disk_state tree
// ----------------------------------------------------------------------
//
// Once a working copy has been mounted, the disk emulator caches that
// path internally — so later callers asking us to "find the canonical
// bytes for the currently-mounted disk" hand us the working-copy path,
// not the user's original source path.  Re-hashing it would yield a
// different SHA-256 (the user's writes have changed the bytes) and we'd
// never find the matching pristine.
//
// Detect that case by structure: if the path ends with
// `<root>/disk_state/<HEX64>/disk<ext>` (or pristine{ext}), return the
// directory + extension so the caller can locate the pristine sibling
// directly.  This also lets ATResolveDiskMount be idempotent — handed a
// working copy path, it short-circuits and returns the same path,
// rather than creating a second-generation working copy.
struct WorkingCopyInfo {
	bool        valid     = false;
	bool        isPristine = false;  // true when basename starts with "pristine"
	VDStringW   dirPath;              // the {root}/disk_state/<HEX>/ directory
	VDStringW   ext;                  // lowercase, with leading dot
};

// Helper: scan backwards from `endExclusive` looking for a '/' or '\\'
// in the range [path, path+endExclusive).  Returns the index or -1 if
// none found.  Hand-rolled because VDStringW doesn't expose rfind.
ptrdiff_t LastSeparator(const wchar_t *path, size_t endExclusive) {
	for (ptrdiff_t i = (ptrdiff_t)endExclusive - 1; i >= 0; --i) {
		if (path[i] == L'/' || path[i] == L'\\')
			return i;
	}
	return -1;
}

bool EqualLower(const wchar_t *a, size_t aLen, const wchar_t *b) {
	size_t i = 0;
	for (; i < aLen && b[i]; ++i) {
		wchar_t ca = a[i];
		if (ca >= L'A' && ca <= L'Z') ca = (wchar_t)(ca + 32);
		if (ca != b[i]) return false;
	}
	return i == aLen && b[i] == 0;
}

WorkingCopyInfo ParseWorkingCopyPath(const wchar_t *path) {
	WorkingCopyInfo info;
	if (!path || !*path) return info;

	const size_t pathLen = wcslen(path);

	// Walk backwards: find the three deepest path separators.
	//   parentSlash .. prevSlash = "/disk_state"
	//   prevSlash   .. lastSlash = "/<HEX64>"
	//   lastSlash+1 .. end       = "<basename>"
	const ptrdiff_t lastSlash = LastSeparator(path, pathLen);
	if (lastSlash <= 0) return info;
	const ptrdiff_t prevSlash = LastSeparator(path, (size_t)lastSlash);
	if (prevSlash <= 0) return info;
	const ptrdiff_t parentSlash = LastSeparator(path, (size_t)prevSlash);
	if (parentSlash < 0) return info;

	// Verify the "/disk_state/" segment.
	const wchar_t *parentName = path + parentSlash + 1;
	const size_t parentLen = (size_t)(prevSlash - parentSlash - 1);
	if (parentLen != wcslen(L"disk_state")) return info;
	if (wcsncmp(parentName, L"disk_state", parentLen) != 0) return info;

	// Verify <HEX64>: exactly 64 hex characters.
	const wchar_t *hex = path + prevSlash + 1;
	const size_t hexLen = (size_t)(lastSlash - prevSlash - 1);
	if (hexLen != 64) return info;
	for (size_t i = 0; i < hexLen; ++i) {
		const wchar_t c = hex[i];
		const bool isHex =
			(c >= L'0' && c <= L'9') ||
			(c >= L'a' && c <= L'f') ||
			(c >= L'A' && c <= L'F');
		if (!isHex) return info;
	}

	// Split basename into stem + extension at the rightmost dot.
	const wchar_t *base = path + lastSlash + 1;
	const size_t baseLen = pathLen - (size_t)lastSlash - 1;
	ptrdiff_t dot = -1;
	for (ptrdiff_t i = (ptrdiff_t)baseLen - 1; i >= 0; --i) {
		if (base[i] == L'.') { dot = i; break; }
	}
	const wchar_t *stem = base;
	const size_t stemLen = (dot < 0) ? baseLen : (size_t)dot;
	VDStringW ext;
	if (dot >= 0) {
		ext.assign(base + dot, baseLen - (size_t)dot);
		for (auto &c : ext) {
			if (c >= L'A' && c <= L'Z') c = (wchar_t)(c + 32);
		}
	}

	if (EqualLower(stem, stemLen, L"disk")) {
		info.valid = true;
		info.isPristine = false;
	} else if (EqualLower(stem, stemLen, L"pristine")) {
		info.valid = true;
		info.isPristine = true;
	} else {
		return info;
	}

	info.dirPath.assign(path, (size_t)lastSlash);
	info.ext = ext;
	return info;
}

// ----------------------------------------------------------------------
// SHA-256
// ----------------------------------------------------------------------

// Stream the file through ATChecksumEngineSHA256 and return the
// lowercase 64-char hex digest.  Returns an empty string on any I/O
// error (caller falls back to source path).
//
// We stream in 64 KiB chunks rather than loading the whole file because
// hard-disk images (.atx, large .atr) can be tens of megabytes and we
// don't want to allocate that into RAM just to hash it.
VDStringA ComputeFileSHA256Hex(const wchar_t *path) {
	VDStringA u8 = VDTextWToU8(VDStringSpanW(path));

	SDL_IOStream *io = SDL_IOFromFile(u8.c_str(), "rb");
	if (!io) {
		g_ATLCDiskState(
			"SHA-256: SDL_IOFromFile(\"%s\") failed: %s",
			u8.c_str(), SDL_GetError());
		return VDStringA();
	}

	ATChecksumEngineSHA256 engine;
	char buf[65536];
	for (;;) {
		size_t n = SDL_ReadIO(io, buf, sizeof buf);
		if (n == 0) {
			// SDL_ReadIO returns 0 at EOF or on error.  Distinguish via
			// SDL_GetIOStatus so a partial read doesn't yield a bogus
			// digest that we'd then key the directory on forever.
			SDL_IOStatus st = SDL_GetIOStatus(io);
			if (st != SDL_IO_STATUS_EOF) {
				g_ATLCDiskState(
					"SHA-256: read failed mid-file on \"%s\" (status=%d)",
					u8.c_str(), (int)st);
				SDL_CloseIO(io);
				return VDStringA();
			}
			break;
		}
		engine.Process(buf, n);
	}
	SDL_CloseIO(io);

	ATChecksumSHA256 digest = engine.Finalize();
	static const char hexChars[] = "0123456789abcdef";
	VDStringA hex;
	hex.resize(64);
	for (int i = 0; i < 32; ++i) {
		hex[i * 2 + 0] = hexChars[(digest.mDigest[i] >> 4) & 0xf];
		hex[i * 2 + 1] = hexChars[digest.mDigest[i]       & 0xf];
	}
	return hex;
}

// ----------------------------------------------------------------------
// File operations
// ----------------------------------------------------------------------

// Atomic copy via `{dst}.tmp` + rename.  We don't use SDL_CopyFile (it
// exists in SDL 3 but is not atomic — a crash mid-copy can leave a
// half-written file at the final path, which we'd then mount and
// almost certainly fail to parse).  Tmp + rename is the same pattern
// netplay_cache.cpp uses for the same reason.
//
// On WASM/IDBFS this still works because Emscripten implements
// `rename(2)` against the IDB-backed FS.  The next sync flushes the
// new file to IndexedDB.
bool CopyFileAtomic(const wchar_t *src, const wchar_t *dst) {
	VDStringA srcU8 = VDTextWToU8(VDStringSpanW(src));
	VDStringA dstU8 = VDTextWToU8(VDStringSpanW(dst));
	VDStringA tmpU8 = dstU8;
	tmpU8 += ".tmp";

	SDL_IOStream *in = SDL_IOFromFile(srcU8.c_str(), "rb");
	if (!in) {
		g_ATLCDiskState("copy: open src \"%s\" failed: %s",
			srcU8.c_str(), SDL_GetError());
		return false;
	}

	SDL_IOStream *out = SDL_IOFromFile(tmpU8.c_str(), "wb");
	if (!out) {
		g_ATLCDiskState("copy: open tmp \"%s\" failed: %s",
			tmpU8.c_str(), SDL_GetError());
		SDL_CloseIO(in);
		return false;
	}

	bool ok = true;
	char buf[65536];
	for (;;) {
		size_t n = SDL_ReadIO(in, buf, sizeof buf);
		if (n == 0) {
			SDL_IOStatus st = SDL_GetIOStatus(in);
			if (st != SDL_IO_STATUS_EOF) {
				g_ATLCDiskState("copy: read failed mid-file on \"%s\"",
					srcU8.c_str());
				ok = false;
			}
			break;
		}
		if (SDL_WriteIO(out, buf, n) != n) {
			g_ATLCDiskState("copy: write failed on \"%s\": %s",
				tmpU8.c_str(), SDL_GetError());
			ok = false;
			break;
		}
	}

	SDL_CloseIO(in);
	SDL_CloseIO(out);

	if (!ok) {
		// Best-effort cleanup so we don't leave a tmp file lying
		// around.  Failure to remove is non-fatal.
		SDL_RemovePath(tmpU8.c_str());
		return false;
	}

	if (!SDL_RenamePath(tmpU8.c_str(), dstU8.c_str())) {
		g_ATLCDiskState(
			"copy: rename \"%s\" -> \"%s\" failed: %s",
			tmpU8.c_str(), dstU8.c_str(), SDL_GetError());
		SDL_RemovePath(tmpU8.c_str());
		return false;
	}

	return true;
}

// ----------------------------------------------------------------------
// Write-mode classification
// ----------------------------------------------------------------------

// We only intervene for *Real* R/W (writes that the emulator's
// AutoFlush path would otherwise send straight to the source file).
// Buffered modes (VRWSafe, VRW) keep their writes in RAM and drop
// them at session end — no on-disk effect to redirect.  RO is
// trivially safe.  The discriminator is the AutoFlush bit; that is
// the only bit set in RW that's not in any other mode.
bool IsRealRW(ATMediaWriteMode mode) {
	return (mode & kATMediaWriteMode_AutoFlush) != 0;
}

// ----------------------------------------------------------------------
// Disk-like extension allowlist
// ----------------------------------------------------------------------

// The mount paths we hook (deferred BootImage, file-browser mount,
// `?wm=rw`) handle every image type — cart, tape, EXE, disk.  The
// write-mode bits only have semantic meaning for disk images though,
// so a cart loaded "RW" should NOT spawn a working-copy directory;
// the cartridge loader ignores write mode and a `disk_state/<sha>/`
// subdir would just be wasted disk space.
//
// Allowlist matches the disk extensions handled by ATIO's
// `ATGetImageTypeForFileExtension`; tape/cart/EXE/state-save
// extensions intentionally drop through.
//
// Compressed variants (`.atr.gz`) aren't listed because LowerExt
// returns the rightmost extension only (here `.gz`); the disk emulator
// transparently inflates `.gz` files through its VFS layer, but
// supporting RW on top of that wrapper is out of scope for v1.  An
// `.atr.gz` mounted RW falls back to source — the simulator will warn
// about "non-updatable image" and remount VRWSafe, matching today's
// behaviour for compressed sources.
bool IsDiskLikeExtension(const wchar_t *ext) {
	if (!ext || !*ext) return false;
	static const wchar_t *kDiskExts[] = {
		L".atr", L".atx", L".xfd", L".dcm", L".pro", L".arc", L".vfd",
	};
	for (const wchar_t *e : kDiskExts) {
		if (wcscmp(ext, e) == 0)
			return true;
	}
	return false;
}

}  // namespace

// ============================================================
// Public API
// ============================================================

VDStringW ATResolveDiskMount(const wchar_t *sourcePath,
                             ATMediaWriteMode intent)
{
	if (!sourcePath || !*sourcePath)
		return VDStringW();

	// Idempotency:  the caller may have handed us a path that's
	// already inside `disk_state/<sha>/` because the disk emulator
	// cached the previous mount.  Re-resolving would re-hash the
	// (potentially user-modified) bytes and either spawn a parallel
	// dir or fail; instead, just return the path unchanged for
	// working copies, or rebuild the working-copy sibling for a
	// pristine path.
	if (WorkingCopyInfo info = ParseWorkingCopyPath(sourcePath); info.valid) {
		if (!IsRealRW(intent))
			return VDStringW(sourcePath);
		VDStringW working = info.dirPath;
		working += PathSep();
		working += L"disk";
		working += info.ext;
		return working;
	}

	// Non-RW intents bypass the helper entirely.  Returning source
	// unchanged keeps the helper a no-op for the netplay /
	// read-protected / virtual-writable mount paths.
	if (!IsRealRW(intent))
		return VDStringW(sourcePath);

	// Non-disk media (cart, tape, EXE, state-save) ignore write-mode
	// bits in the simulator's loader; skip the working-copy machinery
	// so we don't litter `disk_state/` with subdirs we'll never use.
	VDStringW extEarly = LowerExt(sourcePath);
	if (!IsDiskLikeExtension(extEarly.c_str()))
		return VDStringW(sourcePath);

	VDStringA sha = ComputeFileSHA256Hex(sourcePath);
	if (sha.empty()) {
		// Hash failed — most likely the source disappeared between
		// the Game Library scan and the boot, or we were handed a VFS
		// pseudo-path we can't directly open.  Falling back to source
		// preserves today's behaviour (the disk emulator will surface
		// any real load error to the user).
		g_ATLCDiskState(
			"resolve: SHA-256 failed; falling back to source mount");
		return VDStringW(sourcePath);
	}

	VDStringW ext = LowerExt(sourcePath);
	// Defensive default: an extension-less source still gets a
	// working copy, but we file it as ".atr" so the disk loader picks
	// a reasonable handler.  In practice the file browser, Game
	// Library, and command-line `--disk` paths all carry an
	// extension, so this branch is only hit for hand-constructed VFS
	// names.
	if (ext.empty())
		ext = L".atr";

	// mkdir is idempotent in SDL_CreateDirectory (succeeds when the
	// dir already exists).  We create the root first so the
	// per-image directory create can't fail because the parent is
	// missing on first run.
	VDStringA rootU8 = DiskStateRoot();
	if (!SDL_CreateDirectory(rootU8.c_str())) {
		g_ATLCDiskState(
			"resolve: SDL_CreateDirectory(\"%s\") failed: %s",
			rootU8.c_str(), SDL_GetError());
		return VDStringW(sourcePath);
	}

	VDStringA dirU8 = PerImageDir(sha);
	if (!SDL_CreateDirectory(dirU8.c_str())) {
		g_ATLCDiskState(
			"resolve: SDL_CreateDirectory(\"%s\") failed: %s",
			dirU8.c_str(), SDL_GetError());
		return VDStringW(sourcePath);
	}

	VDStringW dirW      = VDTextU8ToW(VDStringSpanA(dirU8));
	VDStringW pristineW = dirW;
	pristineW += PathSep();
	pristineW += L"pristine";
	pristineW += ext;

	VDStringW workingW = dirW;
	workingW += PathSep();
	workingW += L"disk";
	workingW += ext;

	const bool pristineExists = VDDoesPathExist(pristineW.c_str());
	const bool workingExists  = VDDoesPathExist(workingW.c_str());

	// First-ever mount for this image: snapshot source bytes to
	// pristine BEFORE any write can happen, so the canonical copy is
	// guaranteed to match the bytes the user originally provided.
	if (!pristineExists) {
		if (!CopyFileAtomic(sourcePath, pristineW.c_str())) {
			g_ATLCDiskState(
				"resolve: pristine copy failed; falling back to source");
			return VDStringW(sourcePath);
		}
	}

	// Working copy: seed from pristine (not from source) so a
	// post-pristine source mutation doesn't quietly leak into the
	// working copy.  Practical scenario: user imported via Game
	// Library, later overwrote the source file with a different ROM
	// version — we want the working copy to follow the pristine the
	// user has been playing, not the mutated source.
	if (!workingExists) {
		if (!CopyFileAtomic(pristineW.c_str(), workingW.c_str())) {
			g_ATLCDiskState(
				"resolve: working copy failed; falling back to source");
			return VDStringW(sourcePath);
		}
	}

	g_ATLCDiskState("resolve: mounting working copy \"%s\"",
		VDTextWToU8(VDStringSpanW(workingW)).c_str());
	return workingW;
}

VDStringW ATResolveDiskCanonical(const wchar_t *sourcePath)
{
	if (!sourcePath || !*sourcePath)
		return VDStringW();

	// If the caller handed us a working-copy or pristine path inside
	// disk_state/, the pristine sibling IS the canonical bytes —
	// derive it from the path structure (no hashing).  This is the
	// codepath netplay actually hits: the disk emulator caches the
	// currently-mounted path, which is the working copy, and we need
	// to find the pristine without re-hashing potentially-modified
	// bytes.
	if (WorkingCopyInfo info = ParseWorkingCopyPath(sourcePath); info.valid) {
		if (info.isPristine)
			return VDStringW(sourcePath);   // already canonical
		VDStringW pristine = info.dirPath;
		pristine += PathSep();
		pristine += L"pristine";
		pristine += info.ext;
		if (VDDoesPathExist(pristine.c_str()))
			return pristine;
		// Pristine missing (user manually deleted it).  Best we can
		// do: fall back to the working copy itself; whatever bytes it
		// has are now treated as canonical.  Netplay peers' lookups
		// against the original CRC32 will fail and they'll fetch from
		// each other instead, which is the correct degradation.
		return VDStringW(sourcePath);
	}

	// Only disk images have a pristine sibling — anything else returns
	// source unchanged so callers can blindly route paths through this
	// without filtering by image type.
	VDStringW ext = LowerExt(sourcePath);
	if (!IsDiskLikeExtension(ext.c_str()))
		return VDStringW(sourcePath);

	VDStringA sha = ComputeFileSHA256Hex(sourcePath);
	if (sha.empty())
		return VDStringW(sourcePath);

	if (ext.empty())
		ext = L".atr";

	VDStringW pristineW = VDTextU8ToW(VDStringSpanA(PerImageDir(sha)));
	pristineW += PathSep();
	pristineW += L"pristine";
	pristineW += ext;

	if (VDDoesPathExist(pristineW.c_str()))
		return pristineW;

	// No pristine yet — the user never invoked RW on this image, so
	// the source bytes ARE the canonical bytes.  Netplay/Identity
	// reads should use them directly.
	return VDStringW(sourcePath);
}

bool ATRestoreDiskOriginal(const wchar_t *sourcePath)
{
	if (!sourcePath || !*sourcePath)
		return false;

	// Path inside disk_state/?  Use the sibling pristine directly
	// (no hashing needed) — the user-modified working copy bytes
	// would yield a different SHA-256 and miss the dir entirely.
	if (WorkingCopyInfo info = ParseWorkingCopyPath(sourcePath); info.valid) {
		VDStringW pristine = info.dirPath;
		pristine += PathSep();
		pristine += L"pristine";
		pristine += info.ext;
		VDStringW working = info.dirPath;
		working += PathSep();
		working += L"disk";
		working += info.ext;
		if (!VDDoesPathExist(pristine.c_str()))
			return false;
		return CopyFileAtomic(pristine.c_str(), working.c_str());
	}

	VDStringW ext = LowerExt(sourcePath);
	if (!IsDiskLikeExtension(ext.c_str()))
		return false;

	VDStringA sha = ComputeFileSHA256Hex(sourcePath);
	if (sha.empty())
		return false;

	if (ext.empty())
		ext = L".atr";

	VDStringW dirW      = VDTextU8ToW(VDStringSpanA(PerImageDir(sha)));
	VDStringW pristineW = dirW;
	pristineW += PathSep();
	pristineW += L"pristine";
	pristineW += ext;

	VDStringW workingW = dirW;
	workingW += PathSep();
	workingW += L"disk";
	workingW += ext;

	if (!VDDoesPathExist(pristineW.c_str())) {
		// Nothing to restore from.  Caller surfaces a toast.
		return false;
	}

	if (!CopyFileAtomic(pristineW.c_str(), workingW.c_str())) {
		g_ATLCDiskState("restore: copy failed");
		return false;
	}

	g_ATLCDiskState("restore: \"%s\" restored from pristine",
		VDTextWToU8(VDStringSpanW(workingW)).c_str());
	return true;
}
