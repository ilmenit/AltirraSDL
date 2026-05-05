//	AltirraSDL - WebAssembly / browser bridge (implementation)
//
//	See wasm_bridge.h for the big picture.  This file is only compiled
//	under ALTIRRA_WASM — src/AltirraSDL/CMakeLists.txt adds it to the
//	target sources inside an `if(ALTIRRA_WASM)` block, so the native
//	builds never link any of the code below.
//
//	Concurrency model: the browser runs the entire wasm module on a
//	single JS thread (we are building without -pthread / SharedArray-
//	Buffer for GitHub-Pages portability).  That means the JS-facing
//	exports and the main loop tick are guaranteed to run sequentially
//	— no mutex needed on the pending-request queue.  A std::mutex is
//	still used defensively so a future pthread-enabled build could
//	reuse this code unchanged.

#include <stdafx.h>

#if defined(__EMSCRIPTEN__)

#include <emscripten.h>
#include <emscripten/fetch.h>

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/text.h>
#include <vd2/system/VDString.h>
#include <vd2/system/zip.h>

#include "wasm_bridge.h"

// ATGameLibrary + GameSource types used by ATWasmRegisterGamePackSource.
// The corresponding implementation lives in
// src/AltirraSDL/source/ui/gamelibrary/game_library.cpp; the singleton
// itself lives in mobile_game_browser.cpp behind GetGameLibrary().
#include "../ui/gamelibrary/game_library.h"

// ATUIBootImage: the simulator's "load, auto-detect type, cold-boot"
// one-shot API.  Declared in Altirra's main.cpp-era header that the
// SDL3 build already pulls in for the file-dialog Open flow.
extern void ATUIBootImage(const wchar_t *path);

// ATUIDoFirmwareScan: setup wizard's per-directory firmware scanner.
// Walks the given path, auto-detects every recognised image by CRC,
// and registers the results with the live firmware manager.  Exposed
// (non-static) specifically so the WASM bridge can invoke it after
// the user uploads a firmware file.
extern void ATUIDoFirmwareScan(const char *utf8path);

// -----------------------------------------------------------------------
// Pending-request queue
// -----------------------------------------------------------------------

namespace {
	struct PendingUpload {
		std::string mVfsPath;   // UTF-8 as supplied by JS
		bool        mBootNow;   // request immediate cold-boot
	};

	std::mutex                  g_wasmQueueMutex;
	std::vector<PendingUpload>  g_wasmQueue;
}

// -----------------------------------------------------------------------
// JS-facing C exports
// -----------------------------------------------------------------------

extern "C" EMSCRIPTEN_KEEPALIVE
void ATWasmOnFileUploaded(const char* vfsPath, int bootNow) {
	if (!vfsPath || !*vfsPath) {
		fprintf(stderr, "[wasm] ATWasmOnFileUploaded: empty path, ignored\n");
		return;
	}
	fprintf(stderr, "[wasm] ATWasmOnFileUploaded: path=%s boot=%d\n",
	        vfsPath, bootNow);

	// Defensive copy; the JS caller may free the string buffer as soon
	// as we return.  string constructor makes an owned copy.
	PendingUpload p{ std::string(vfsPath), bootNow != 0 };

	std::lock_guard<std::mutex> lk(g_wasmQueueMutex);
	g_wasmQueue.push_back(std::move(p));
}

// Wrapped in EM_JS so the implementation — a call into Module.FS —
// doesn't need to round-trip through a named JS symbol.  Safe to call
// at any time; the browser shell also calls syncfs() after every
// upload, so this is a belt-and-suspenders flush.
EM_JS(void, _altirra_wasm_sync_fs_out, (), {
	if (typeof Module !== 'undefined' && Module.FS
			&& typeof Module.FS.syncfs === 'function') {
		Module.FS.syncfs(false, function (err) {
			if (err) console.warn('[altirra-wasm] syncfs(out) failed:', err);
		});
	}
});

extern "C" EMSCRIPTEN_KEEPALIVE
void ATWasmSyncFSOut() {
	_altirra_wasm_sync_fs_out();
}

extern "C" EMSCRIPTEN_KEEPALIVE
void ATWasmRescanFirmware() {
	// The Setup Wizard scanner takes a UTF-8 folder path and registers
	// every recognised firmware image it finds underneath.  Running it
	// on the dedicated /home/web_user/firmware upload directory keeps
	// the firmware manager in sync with what the user has dropped in.
	ATUIDoFirmwareScan("/home/web_user/firmware");
}

// -----------------------------------------------------------------------
// Main-thread drain
// -----------------------------------------------------------------------

void ATWasmBridgeTick() {
	// Swap the queue under the lock, then process unlocked so a
	// callback-triggered re-enqueue (hypothetical — JS is currently
	// driven by user input, not timers, but be defensive) cannot
	// deadlock on itself.
	std::vector<PendingUpload> batch;
	{
		std::lock_guard<std::mutex> lk(g_wasmQueueMutex);
		batch.swap(g_wasmQueue);
	}

	if (batch.empty())
		return;

	fprintf(stderr, "[wasm] BridgeTick: draining %zu request(s)\n", batch.size());

	for (const auto& req : batch) {
		if (req.mBootNow) {
			// VDStringW / VDTextU8ToW is the canonical UTF-8 → wchar_t
			// conversion used throughout the system library for file
			// paths.  ATUIBootImage takes the wchar_t path and triggers
			// the simulator's full load + cold-boot sequence (same path
			// the desktop file-open dialog uses).
			fprintf(stderr, "[wasm] BridgeTick: booting %s\n", req.mVfsPath.c_str());
			const VDStringW pathW = VDTextU8ToW(VDStringSpanA(req.mVfsPath.c_str()));
			try {
				ATUIBootImage(pathW.c_str());
				fprintf(stderr, "[wasm] BridgeTick: boot returned\n");
			} catch (const std::exception& ex) {
				fprintf(stderr, "[wasm] BridgeTick: boot threw std::exception: %s\n", ex.what());
			} catch (...) {
				fprintf(stderr, "[wasm] BridgeTick: boot threw unknown exception\n");
			}
		}
		// else: nothing to do on the C side; the JS shell has already
		// written the file to /home/web_user and flushed to IDBFS.
	}
}

// -----------------------------------------------------------------------
// Archive unpack
// -----------------------------------------------------------------------

namespace {
	// Reject ".." segments and absolute paths inside zip entries so a
	// malformed archive can't escape destDir.  Empty / "." segments are
	// silently dropped.  Returns true if the entry's relative path is
	// safe to materialise; the cleaned path is written to outRel.
	bool SanitiseZipEntryPath(const wchar_t *raw, VDStringW &outRel) {
		outRel.clear();
		if (!raw) return false;
		if (*raw == L'/' || *raw == L'\\') ++raw;
		while (*raw == L'/' || *raw == L'\\') ++raw;

		const wchar_t *p = raw;
		while (*p) {
			const wchar_t *seg = p;
			while (*p && *p != L'/' && *p != L'\\') ++p;
			size_t segLen = (size_t)(p - seg);
			if (segLen > 0) {
				if (segLen == 2 && seg[0] == L'.' && seg[1] == L'.') return false;
				if (!(segLen == 1 && seg[0] == L'.')) {
					if (!outRel.empty()) outRel += L'/';
					outRel.append(seg, segLen);
				}
			}
			while (*p == L'/' || *p == L'\\') ++p;
		}
		return !outRel.empty();
	}

	// Ensure every parent directory of `fullPath` exists.  `fullPath`
	// itself is the file we're about to write — only its containing
	// dirs are created.  No-op for top-level entries.
	void EnsureParentDirs(const VDStringW &fullPath) {
		size_t end = fullPath.size();
		while (end > 0 && fullPath[end - 1] != L'/' && fullPath[end - 1] != L'\\')
			--end;
		if (end == 0) return;
		VDStringW prefix;
		size_t i = 0;
		if (fullPath[0] == L'/' || fullPath[0] == L'\\') {
			prefix = L"/";
			i = 1;
		}
		while (i < end) {
			while (i < end && (fullPath[i] == L'/' || fullPath[i] == L'\\')) ++i;
			size_t segStart = i;
			while (i < end && fullPath[i] != L'/' && fullPath[i] != L'\\') ++i;
			if (segStart == i) break;
			if (!prefix.empty() && prefix.back() != L'/')
				prefix += L'/';
			prefix.append(fullPath.data() + segStart, i - segStart);
			if (!VDDoesPathExist(prefix.c_str())) {
				try { VDCreateDirectory(prefix.c_str()); }
				catch (const MyError &) { /* best-effort */ }
			}
		}
	}
}

extern "C" EMSCRIPTEN_KEEPALIVE
int ATWasmUnpackArchive(const char *zipPath, const char *destDir) {
	if (!zipPath || !*zipPath || !destDir || !*destDir) {
		fprintf(stderr, "[wasm] UnpackArchive: bad args\n");
		return -1;
	}
	fprintf(stderr, "[wasm] UnpackArchive: %s -> %s\n", zipPath, destDir);

	const VDStringW zipPathW  = VDTextU8ToW(VDStringSpanA(zipPath));
	const VDStringW destDirW0 = VDTextU8ToW(VDStringSpanA(destDir));
	VDStringW destDirW = destDirW0;
	while (!destDirW.empty()
		&& (destDirW.back() == L'/' || destDirW.back() == L'\\'))
		destDirW.pop_back();
	if (destDirW.empty()) {
		fprintf(stderr, "[wasm] UnpackArchive: empty dest after trim\n");
		return -1;
	}

	if (!VDDoesPathExist(destDirW.c_str())) {
		EnsureParentDirs(destDirW + L"/.");
		try { VDCreateDirectory(destDirW.c_str()); }
		catch (const MyError &) {
			fprintf(stderr, "[wasm] UnpackArchive: cannot create destDir\n");
			return -1;
		}
	}

	int written = 0;
	try {
		VDFileStream fs(zipPathW.c_str(),
			nsVDFile::kRead | nsVDFile::kDenyNone | nsVDFile::kOpenExisting);
		VDZipArchive zip;
		zip.Init(&fs);

		const sint32 n = zip.GetFileCount();
		for (sint32 i = 0; i < n; ++i) {
			const VDZipArchive::FileInfo &info = zip.GetFileInfo(i);

			if (!info.mDecodedFileName.empty()
				&& (info.mDecodedFileName.back() == L'/'
				 || info.mDecodedFileName.back() == L'\\'))
				continue;

			VDStringW rel;
			if (!SanitiseZipEntryPath(info.mDecodedFileName.c_str(), rel)) {
				fprintf(stderr, "[wasm] UnpackArchive: skip unsafe '%ls'\n",
					info.mDecodedFileName.c_str());
				continue;
			}

			VDStringW outPath = destDirW;
			outPath += L'/';
			outPath += rel;

			EnsureParentDirs(outPath);

			// OpenDecodedStream + Read is the canonical inflate path.
			// Calling DecompressStream(idx, buf) directly without first
			// loading the compressed bytes via ReadRawStream throws
			// VDIOReadPastEOFException because buf is empty.  Using
			// the inflate stream avoids that contract entirely.
			vdfastvector<uint8> buf;
			try {
				if (info.mUncompressedSize > 256 * 1024 * 1024) {
					fprintf(stderr, "[wasm] UnpackArchive: skip oversized '%ls' (%u bytes)\n",
						info.mDecodedFileName.c_str(), info.mUncompressedSize);
					continue;
				}
				vdautoptr<IVDInflateStream> zs(zip.OpenDecodedStream(i, true));
				if (!zs) {
					fprintf(stderr, "[wasm] UnpackArchive: OpenDecodedStream null for '%ls'\n",
						info.mDecodedFileName.c_str());
					continue;
				}
				buf.resize(info.mUncompressedSize);
				if (info.mUncompressedSize)
					zs->Read(buf.data(), info.mUncompressedSize);
			} catch (const MyError &e) {
				fprintf(stderr, "[wasm] UnpackArchive: read entry '%ls' failed: %s\n",
					info.mDecodedFileName.c_str(), e.c_str());
				continue;
			} catch (const std::exception &e) {
				fprintf(stderr, "[wasm] UnpackArchive: read entry '%ls' std::exception: %s\n",
					info.mDecodedFileName.c_str(), e.what());
				continue;
			} catch (...) {
				fprintf(stderr, "[wasm] UnpackArchive: read entry '%ls' unknown exception\n",
					info.mDecodedFileName.c_str());
				continue;
			}

			try {
				VDFile out(outPath.c_str(),
					nsVDFile::kWrite | nsVDFile::kDenyAll
					| nsVDFile::kCreateAlways);
				if (!buf.empty())
					out.write(buf.data(), (sint32)buf.size());
				out.close();
				++written;
			} catch (const MyError &e) {
				fprintf(stderr, "[wasm] UnpackArchive: write failed for '%ls': %s\n",
					outPath.c_str(), e.c_str());
			} catch (const std::exception &e) {
				fprintf(stderr, "[wasm] UnpackArchive: write '%ls' std::exception: %s\n",
					outPath.c_str(), e.what());
			} catch (...) {
				fprintf(stderr, "[wasm] UnpackArchive: write '%ls' unknown exception\n",
					outPath.c_str());
			}
		}
	} catch (const MyError &e) {
		fprintf(stderr, "[wasm] UnpackArchive: zip error: %s\n", e.c_str());
		return -1;
	} catch (const std::exception &e) {
		fprintf(stderr, "[wasm] UnpackArchive: std::exception: %s\n", e.what());
		return -1;
	} catch (...) {
		fprintf(stderr, "[wasm] UnpackArchive: unknown exception\n");
		return -1;
	}

	if (written > 0)
		_altirra_wasm_sync_fs_out();
	fprintf(stderr, "[wasm] UnpackArchive: extracted %d file(s)\n", written);
	return written;
}

// -----------------------------------------------------------------------
// Recursive delete
// -----------------------------------------------------------------------

namespace {
	int DeleteTreeImpl(const std::string &path) {
		struct stat st;
		if (lstat(path.c_str(), &st) != 0) {
			if (errno == ENOENT) return 0;
			fprintf(stderr, "[wasm] DeleteTree: stat('%s') errno=%d\n",
				path.c_str(), errno);
			return -1;
		}

		if (!S_ISDIR(st.st_mode)) {
			if (unlink(path.c_str()) != 0) {
				fprintf(stderr, "[wasm] DeleteTree: unlink('%s') errno=%d\n",
					path.c_str(), errno);
				return -1;
			}
			return 1;
		}

		// Collect names first so we don't recurse + unlink while still
		// iterating the parent DIR* — POSIX leaves that behaviour
		// undefined and Emscripten's MEMFS can skip entries.
		std::vector<std::string> children;
		{
			DIR *d = opendir(path.c_str());
			if (!d) {
				fprintf(stderr, "[wasm] DeleteTree: opendir('%s') errno=%d\n",
					path.c_str(), errno);
				return -1;
			}
			struct dirent *e;
			while ((e = readdir(d)) != nullptr) {
				const char *n = e->d_name;
				if (!strcmp(n, ".") || !strcmp(n, "..")) continue;
				children.emplace_back(n);
			}
			closedir(d);
		}

		int removed = 0;
		for (const auto &n : children) {
			std::string child = path;
			if (!child.empty() && child.back() != '/') child += '/';
			child += n;
			int sub = DeleteTreeImpl(child);
			if (sub < 0) return -1;
			removed += sub;
		}

		if (rmdir(path.c_str()) != 0) {
			fprintf(stderr, "[wasm] DeleteTree: rmdir('%s') errno=%d\n",
				path.c_str(), errno);
			return -1;
		}
		return removed + 1;
	}
}

extern "C" EMSCRIPTEN_KEEPALIVE
int ATWasmDeleteTree(const char *path) {
	if (!path || !*path) return -1;
	fprintf(stderr, "[wasm] DeleteTree: %s\n", path);
	int r = DeleteTreeImpl(std::string(path));
	fprintf(stderr, "[wasm] DeleteTree: removed %d item(s)\n", r);
	return r;
}

// -----------------------------------------------------------------------
// Recursive copy (file or directory tree)
// -----------------------------------------------------------------------
//
// Symmetric counterpart to ATWasmDeleteTree.  JS's File Manager Cut/Copy/
// Paste flow uses Module.FS.rename for cut-mode (atomic, no copy), and
// this export for copy-mode where we have to materialise a fresh subtree.
// Doing the file-by-file read+write in C++ keeps the WASM heap calm — a
// JS implementation would have to allocate a Uint8Array per file and
// shuttle bytes back through the JS↔WASM boundary, which on a multi-MB
// game-pack folder churns the heap and trips growAllocations stalls.
//
// Returns the number of *files* written (directories aren't counted), or
// -1 on a fatal failure (bad src, allocation, IO error before any
// progress).  Per-entry errors are logged but the walk continues so a
// single corrupt file doesn't abort the whole copy.

namespace {
	// Copy one regular file's bytes from `src` to `dest`.  `dest` must
	// not exist (caller is responsible for collision-renaming when the
	// destination already has an entry of the same name).  Returns true
	// on success.
	bool CopyFileImpl(const std::string &src, const std::string &dest) {
		// Stream in 64 KB chunks — small enough to avoid heap spikes,
		// large enough that even a 10 MB ROM completes in ≤160 reads.
		// MEMFS read/write are simple memmove/memcpy under the hood, so
		// throughput is bounded by allocation behaviour, not block size.
		FILE *in = fopen(src.c_str(), "rb");
		if (!in) {
			fprintf(stderr, "[wasm] CopyTree: fopen('%s', rb) errno=%d\n",
				src.c_str(), errno);
			return false;
		}
		FILE *out = fopen(dest.c_str(), "wb");
		if (!out) {
			fprintf(stderr, "[wasm] CopyTree: fopen('%s', wb) errno=%d\n",
				dest.c_str(), errno);
			fclose(in);
			return false;
		}
		constexpr size_t kBuf = 64 * 1024;
		std::vector<char> buf(kBuf);
		bool ok = true;
		for (;;) {
			size_t n = fread(buf.data(), 1, kBuf, in);
			if (n == 0) {
				if (ferror(in)) {
					fprintf(stderr,
						"[wasm] CopyTree: fread('%s') errno=%d\n",
						src.c_str(), errno);
					ok = false;
				}
				break;
			}
			if (fwrite(buf.data(), 1, n, out) != n) {
				fprintf(stderr,
					"[wasm] CopyTree: fwrite('%s') errno=%d\n",
					dest.c_str(), errno);
				ok = false;
				break;
			}
		}
		fclose(in);
		fclose(out);
		if (!ok) {
			// Best-effort rollback so a partial file doesn't pollute
			// the destination tree.
			unlink(dest.c_str());
		}
		return ok;
	}

	int CopyTreeImpl(const std::string &src, const std::string &dest) {
		struct stat st;
		if (lstat(src.c_str(), &st) != 0) {
			fprintf(stderr, "[wasm] CopyTree: stat('%s') errno=%d\n",
				src.c_str(), errno);
			return -1;
		}

		if (!S_ISDIR(st.st_mode)) {
			return CopyFileImpl(src, dest) ? 1 : -1;
		}

		// Create the destination directory.  EEXIST is fine — caller
		// may have pre-created it as part of a paste-into-existing-
		// folder collision-rename ladder.
		if (mkdir(dest.c_str(), 0755) != 0 && errno != EEXIST) {
			fprintf(stderr, "[wasm] CopyTree: mkdir('%s') errno=%d\n",
				dest.c_str(), errno);
			return -1;
		}

		// Snapshot names first to avoid undefined behaviour from
		// mutating the FS while walking it (matches DeleteTreeImpl's
		// pattern; MEMFS may skip entries otherwise).
		std::vector<std::string> children;
		{
			DIR *d = opendir(src.c_str());
			if (!d) {
				fprintf(stderr,
					"[wasm] CopyTree: opendir('%s') errno=%d\n",
					src.c_str(), errno);
				return -1;
			}
			struct dirent *e;
			while ((e = readdir(d)) != nullptr) {
				const char *n = e->d_name;
				if (!strcmp(n, ".") || !strcmp(n, "..")) continue;
				children.emplace_back(n);
			}
			closedir(d);
		}

		int copied = 0;
		for (const auto &n : children) {
			std::string s = src; if (s.back() != '/') s += '/'; s += n;
			std::string t = dest; if (t.back() != '/') t += '/'; t += n;
			int r = CopyTreeImpl(s, t);
			if (r < 0) {
				// Skip this entry but keep going — a single bad file
				// shouldn't abort a whole pack copy.
				continue;
			}
			copied += r;
		}
		return copied;
	}
}

extern "C" EMSCRIPTEN_KEEPALIVE
int ATWasmCopyTree(const char *src, const char *dest) {
	if (!src || !*src || !dest || !*dest) {
		fprintf(stderr, "[wasm] CopyTree: bad args\n");
		return -1;
	}
	// Refuse a copy onto self / into a descendant — MEMFS would
	// happily walk into an infinite directory loop otherwise.  This
	// MUST stay in C so the JS-level cycle guard isn't the only
	// safety net (e.g. if a future caller bypasses the JS clipboard
	// helpers).
	std::string s = src, d = dest;
	if (s == d) {
		fprintf(stderr, "[wasm] CopyTree: src == dest, refusing\n");
		return -1;
	}
	std::string sPlusSlash = s;
	if (sPlusSlash.empty() || sPlusSlash.back() != '/')
		sPlusSlash += '/';
	if (d.compare(0, sPlusSlash.size(), sPlusSlash) == 0) {
		fprintf(stderr,
			"[wasm] CopyTree: dest '%s' is inside src '%s', refusing\n",
			d.c_str(), s.c_str());
		return -1;
	}

	fprintf(stderr, "[wasm] CopyTree: %s -> %s\n", src, dest);
	int r = CopyTreeImpl(s, d);
	if (r > 0) _altirra_wasm_sync_fs_out();
	fprintf(stderr, "[wasm] CopyTree: copied %d file(s)\n", r);
	return r;
}

// -----------------------------------------------------------------------
// First-run ROM bootstrap
// -----------------------------------------------------------------------
//
// On every WASM start, JS asks us (once) to populate the firmware
// directory if it's empty.  We try a preferred HTTPS URL of a
// distributable ROM bundle; on failure we fall back to a HTTP mirror.
// On success we extract every ".rom" entry into /home/web_user/firmware
// and re-run the firmware scan so the manager picks them up.  A marker
// file is always written so we never retry on later loads, even if both
// URLs fail.  JS polls ATWasmGetFirstRunStatus() to drive a toast UX.

namespace {
	constexpr const char *kFirstRunFirmwareDir = "/home/web_user/firmware";
	constexpr const char *kFirstRunMarkerPath  =
		"/home/web_user/.config/altirra/.firstrun_done";
	constexpr const char *kFirstRunMarkerDir   =
		"/home/web_user/.config/altirra";

	// Mirror list for the standard ROM bundle.  Tried in order; the
	// first one to return 2xx with extractable .rom entries wins.  Add
	// or reorder URLs here — no other code paths reference these.
	//
	// **Browser CORS gates this list.**  emscripten_fetch goes through
	// the page's normal `fetch()` plumbing, so any mirror that doesn't
	// return `Access-Control-Allow-Origin: *` is rejected by the
	// browser as opaque before our success callback runs — the failure
	// surfaces here as `status=0` with a synthesised error string.
	// Result: in stock browsers, only mirrors that explicitly opt in
	// to CORS work.  As of 2026-05, that's `ifarchive.org` (and its
	// mirror network).  The other entries below are kept on purpose:
	// self-hosters who reverse-proxy the ROM bundle through their own
	// origin (HOSTING.md §3 describes the Caddyfile snippet) bypass
	// CORS entirely, and a future mirror flipping CORS on shouldn't
	// require a code change to start being used.  pcxf360.zip and
	// xf25.zip both bundle the Altirra-distributable ROM set; the
	// extractor accepts any ".rom" entry so either layout works.
	constexpr const char *kFirstRunUrls[] = {
		// CORS-permissive primary (verified 2026-05).
		"https://ifarchive.org/if-archive/emulators/atari/pcxf360.zip",

		// CORS-permissive ifarchive mirror (same content, separate
		// origin; if ifarchive.org goes down, the mirrors keep
		// answering — they typically inherit CORS too).
		"https://mirrors.ibiblio.org/pub/mirrors/interactive-fiction/emulators/atari/pcxf360.zip",

		// CORS-restricted mirrors (always fail in stock browsers but
		// work for self-hosters who reverse-proxy them, and for any
		// future runtime path that bypasses fetch's same-origin
		// policy — keep them so non-browser embedders aren't forced
		// onto a single mirror).
		"https://atariarea.krap.pl/PLus/files/xf25.zip",
		"https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/distributions/1.1/repos/emulators/xf25.zip",
		"https://ftp.fau.de/macports/distfiles/atari800/xf25.zip",
		"http://atari.vjetnam.cz/dow/emuROMs.zip",
	};
	constexpr int kFirstRunUrlCount =
		(int)(sizeof(kFirstRunUrls) / sizeof(kFirstRunUrls[0]));

	std::atomic<int>  g_firstRunState{0};
	std::atomic<int>  g_firstRunFiles{0};
	std::atomic<bool> g_firstRunStarted{false};

	bool DirHasAnyFile(const char *dir) {
		DIR *d = opendir(dir);
		if (!d) return false;
		bool any = false;
		struct dirent *e;
		while ((e = readdir(d)) != nullptr) {
			if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
			any = true;
			break;
		}
		closedir(d);
		return any;
	}

	bool EndsWithIcase(const wchar_t *name, const wchar_t *suffix) {
		size_t nl = wcslen(name), sl = wcslen(suffix);
		if (sl > nl) return false;
		const wchar_t *p = name + (nl - sl);
		for (size_t i = 0; i < sl; ++i) {
			wchar_t a = p[i], b = suffix[i];
			if (a >= L'A' && a <= L'Z') a = (wchar_t)(a - L'A' + L'a');
			if (b >= L'A' && b <= L'Z') b = (wchar_t)(b - L'A' + L'a');
			if (a != b) return false;
		}
		return true;
	}

	void WriteFirstRunMarker() {
		mkdir("/home/web_user/.config", 0755);
		mkdir(kFirstRunMarkerDir, 0755);
		FILE *f = fopen(kFirstRunMarkerPath, "wb");
		if (f) {
			static const char body[] = "1\n";
			fwrite(body, 1, sizeof(body) - 1, f);
			fclose(f);
		}
		_altirra_wasm_sync_fs_out();
	}

	// Read one zip entry's uncompressed bytes into outBuf.  Uses the
	// OpenDecodedStream API — which expects the underlying zip's
	// random-access stream to still be alive — and resizes outBuf to
	// the entry's full uncompressed length before reading.  Each
	// entry is wrapped in its own try/catch so a single corrupt /
	// truncated entry doesn't abort the whole extraction.
	bool ReadZipEntry(VDZipArchive &zip, sint32 idx,
			vdfastvector<uint8> &outBuf, const wchar_t *nameForLog) {
		try {
			const auto &fi = zip.GetFileInfo(idx);
			if (fi.mUncompressedSize > 64 * 1024 * 1024) {
				fprintf(stderr, "[wasm] FirstRun: skip oversized entry '%ls' (%u bytes)\n",
					nameForLog, fi.mUncompressedSize);
				return false;
			}
			vdautoptr<IVDInflateStream> zs(zip.OpenDecodedStream(idx));
			if (!zs) {
				fprintf(stderr, "[wasm] FirstRun: OpenDecodedStream returned null for '%ls'\n",
					nameForLog);
				return false;
			}
			outBuf.resize(fi.mUncompressedSize);
			if (fi.mUncompressedSize)
				zs->Read(outBuf.data(), fi.mUncompressedSize);
			return true;
		} catch (const MyError &e) {
			fprintf(stderr, "[wasm] FirstRun: read entry '%ls' failed: %s\n",
				nameForLog, e.c_str());
		} catch (const std::exception &e) {
			fprintf(stderr, "[wasm] FirstRun: read entry '%ls' std::exception: %s\n",
				nameForLog, e.what());
		} catch (...) {
			fprintf(stderr, "[wasm] FirstRun: read entry '%ls' unknown exception\n",
				nameForLog);
		}
		return false;
	}

	int ExtractRomsFromBuffer(const uint8_t *bytes, size_t len) {
		int written = 0;
		try {
			VDMemoryStream ms(bytes, (uint32)len);
			VDZipArchive zip;
			zip.Init(&ms);

			const sint32 n = zip.GetFileCount();
			fprintf(stderr, "[wasm] FirstRun: zip has %d entries\n", n);

			mkdir(kFirstRunFirmwareDir, 0755);

			for (sint32 i = 0; i < n; ++i) {
				const VDZipArchive::FileInfo &info = zip.GetFileInfo(i);
				if (info.mDecodedFileName.empty()) continue;
				wchar_t last = info.mDecodedFileName.back();
				if (last == L'/' || last == L'\\') continue;

				if (!EndsWithIcase(info.mDecodedFileName.c_str(), L".rom"))
					continue;

				const wchar_t *full = info.mDecodedFileName.c_str();
				const wchar_t *base = full + wcslen(full);
				while (base > full && base[-1] != L'/' && base[-1] != L'\\')
					--base;
				if (!*base) continue;

				VDStringW outPath = VDTextU8ToW(VDStringSpanA(kFirstRunFirmwareDir));
				outPath += L'/';
				outPath += base;

				vdfastvector<uint8> buf;
				if (!ReadZipEntry(zip, i, buf, base))
					continue;   // skip & keep going — corrupt entry, not a fatal zip

				try {
					VDFile out(outPath.c_str(),
						nsVDFile::kWrite | nsVDFile::kDenyAll
						| nsVDFile::kCreateAlways);
					if (!buf.empty())
						out.write(buf.data(), (sint32)buf.size());
					out.close();
					++written;
					fprintf(stderr, "[wasm] FirstRun: extracted '%ls' (%zu bytes)\n",
						base, buf.size());
				} catch (const MyError &e) {
					fprintf(stderr, "[wasm] FirstRun: write failed for '%ls': %s\n",
						base, e.c_str());
				} catch (const std::exception &e) {
					fprintf(stderr, "[wasm] FirstRun: write '%ls' std::exception: %s\n",
						base, e.what());
				} catch (...) {
					fprintf(stderr, "[wasm] FirstRun: write '%ls' unknown exception\n",
						base);
				}
			}
		} catch (const MyError &e) {
			fprintf(stderr, "[wasm] FirstRun: zip error: %s\n", e.c_str());
			return -1;
		} catch (const std::exception &e) {
			fprintf(stderr, "[wasm] FirstRun: std::exception: %s\n", e.what());
			return -1;
		} catch (...) {
			fprintf(stderr, "[wasm] FirstRun: unknown exception\n");
			return -1;
		}
		return written;
	}

	void StartFetchAt(int idx);

	// Move on to the next mirror on the list, or finalise with state 7
	// (all URLs failed) if we've run out.  The marker is written either
	// way so we don't keep retrying on every page load.
	void TryNextMirror(int currentIdx) {
		const int next = currentIdx + 1;
		if (next >= kFirstRunUrlCount) {
			fprintf(stderr, "[wasm] FirstRun: all %d mirror(s) failed\n",
				kFirstRunUrlCount);
			g_firstRunState.store(7);
			WriteFirstRunMarker();
			return;
		}
		// Keep state 2 for the very first attempt, 3 for any retry —
		// gives the JS a hint that we've moved past the primary.
		g_firstRunState.store(3);
		StartFetchAt(next);
	}

	void OnFirstRunSuccess(emscripten_fetch_t *f) {
		const int idx = (int)(intptr_t)f->userData;
		fprintf(stderr, "[wasm] FirstRun: fetch [%d] '%s' OK status=%lu bytes=%llu\n",
			idx, f->url ? f->url : "?", (unsigned long)f->status,
			(unsigned long long)f->numBytes);

		if (f->status < 200 || f->status >= 300) {
			emscripten_fetch_close(f);
			TryNextMirror(idx);
			return;
		}

		g_firstRunState.store(4);
		int n = ExtractRomsFromBuffer(
			reinterpret_cast<const uint8_t *>(f->data),
			(size_t)f->numBytes);
		emscripten_fetch_close(f);

		if (n > 0) {
			g_firstRunFiles.store(n);
			ATUIDoFirmwareScan(kFirstRunFirmwareDir);
			_altirra_wasm_sync_fs_out();
			g_firstRunState.store(5);
			WriteFirstRunMarker();
		} else if (n == 0) {
			// Got the zip but it had no .rom entries — that's the
			// archive's content, not a transport problem; don't retry.
			g_firstRunState.store(6);
			WriteFirstRunMarker();
		} else {
			// Decode error (n < 0) — try next mirror in case this one
			// served a corrupt bundle.
			TryNextMirror(idx);
		}
	}

	void OnFirstRunError(emscripten_fetch_t *f) {
		const int idx = (int)(intptr_t)f->userData;
		// f->statusText is a fixed-size char array, so a pointer-NULL
		// check is meaningless — gate on the first byte instead.
		// status=0 in emscripten_fetch is the browser's opaque-error
		// signature: CORS rejection, mixed-content block, DNS / TLS
		// failure, or network unreachable, all collapsed to a single
		// indistinguishable code.  Spell that out so future readers
		// don't chase phantom 404s like the krap.pl/ibiblio entries
		// in 2026-05 (curl saw 200, browser saw status=0 — that was
		// missing Access-Control-Allow-Origin, not "Not Found").
		const char *hint = "";
		if (f->status == 0)
			hint = " — likely CORS / mixed-content / TLS / network "
			       "(status=0 hides the real cause; check DevTools "
			       "Network tab for the response)";
		fprintf(stderr, "[wasm] FirstRun: fetch [%d] '%s' FAILED status=%lu (%s)%s\n",
			idx, f->url ? f->url : "?", (unsigned long)f->status,
			(f->statusText[0] ? f->statusText : ""), hint);
		emscripten_fetch_close(f);
		TryNextMirror(idx);
	}

	void StartFetchAt(int idx) {
		if (idx < 0 || idx >= kFirstRunUrlCount) {
			g_firstRunState.store(7);
			WriteFirstRunMarker();
			return;
		}
		emscripten_fetch_attr_t attr;
		emscripten_fetch_attr_init(&attr);
		strcpy(attr.requestMethod, "GET");
		attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
		// 20s per-mirror timeout — without this a dead URL or a slow
		// CDN could stall the toast indefinitely.  6 mirrors × 20s
		// gives ~2 min worst-case before "upload your own" appears,
		// which is reasonable; in the common case (first mirror works)
		// the user sees the success toast within a few seconds.
		attr.timeoutMSecs = 20000;
		attr.userData   = (void *)(intptr_t)idx;
		attr.onsuccess  = OnFirstRunSuccess;
		attr.onerror    = OnFirstRunError;
		fprintf(stderr, "[wasm] FirstRun: fetching [%d] %s\n",
			idx, kFirstRunUrls[idx]);
		emscripten_fetch(&attr, kFirstRunUrls[idx]);
	}
}

extern "C" EMSCRIPTEN_KEEPALIVE
void ATWasmFirstRunBootstrap() {
	bool expected = false;
	if (!g_firstRunStarted.compare_exchange_strong(expected, true)) {
		fprintf(stderr, "[wasm] FirstRun: already started, ignoring\n");
		return;
	}

	g_firstRunState.store(1);

	struct stat st;
	if (stat(kFirstRunMarkerPath, &st) == 0) {
		fprintf(stderr, "[wasm] FirstRun: marker present, skipping\n");
		g_firstRunState.store(8);
		return;
	}
	if (DirHasAnyFile(kFirstRunFirmwareDir)) {
		fprintf(stderr, "[wasm] FirstRun: firmware dir non-empty, skipping\n");
		WriteFirstRunMarker();
		g_firstRunState.store(8);
		return;
	}

	g_firstRunState.store(2);
	StartFetchAt(0);
}

extern "C" EMSCRIPTEN_KEEPALIVE
int ATWasmGetFirstRunState() { return g_firstRunState.load(); }

extern "C" EMSCRIPTEN_KEEPALIVE
int ATWasmGetFirstRunFiles() { return g_firstRunFiles.load(); }

// Reset the bootstrap state so a subsequent ATWasmFirstRunBootstrap
// call actually performs the fetch.  Called by JS in two situations:
// (a) the user accepts the firmware-download wizard after we deferred
// the silent auto-download, and (b) the user clicks the toolbar's
// "Download firmware" button to re-run the bootstrap when the marker
// already exists (e.g. after a previous session declined).  Removes
// the marker and zeroes the atomic guards so the next bootstrap call
// is allowed through.
extern "C" EMSCRIPTEN_KEEPALIVE
void ATWasmResetFirstRun() {
	g_firstRunStarted.store(false);
	g_firstRunState.store(0);
	g_firstRunFiles.store(0);
	unlink(kFirstRunMarkerPath);
	_altirra_wasm_sync_fs_out();
}

// -----------------------------------------------------------------------
// Game-pack registration
// -----------------------------------------------------------------------
//
// Self-host config.json may declare one or more game packs (zip URLs
// the page operator has chosen to ship as the "starter library").  JS
// owns the actual fetch+write step (fetch() respects browser CORS /
// mixed-content / redirect rules cleanly) and uses ATWasmUnpackArchive
// for extraction; the only piece JS can't do is make the freshly-
// extracted folder visible to the Game Library.
//
// ATWasmRegisterGamePackSource takes a UTF-8 directory path that
// already exists on the VFS, registers it as a (recursive, folder-
// kind) source on the live ATGameLibrary, persists the source set to
// the registry, and triggers a synchronous rescan so the user sees
// the new entries immediately.  The path is added at most once —
// repeated calls are idempotent — so re-running the wizard with a
// pack that's already installed is harmless.
//
// User-deletable: the source points at a normal VFS folder; the user
// can browse there from File Manager and delete games one at a time
// or wipe the whole pack via Wipe.  Removing the source itself (so
// the empty dir stops appearing in the library) is left to the user
// from the desktop UI's Library → Sources tab; the WASM mobile
// browser doesn't surface that page yet, but the source row vanishes
// automatically once its directory is empty + a rescan runs.

// C++-linkage forward declarations.  They MUST sit outside the
// extern "C" block below — the definitions in mobile_game_browser.cpp
// and registry_sdl3.cpp are C++-mangled, and re-declaring them with C
// linkage triggers wasm-ld "undefined symbol" at link time.
ATGameLibrary *GetGameLibrary();
void           ATRegistryFlushToDisk();

extern "C" EMSCRIPTEN_KEEPALIVE
int ATWasmRegisterGamePackSource(const char *utf8Path) {
	if (!utf8Path || !*utf8Path) {
		fprintf(stderr, "[wasm] RegisterGamePackSource: empty path\n");
		return -1;
	}

	ATGameLibrary *lib = GetGameLibrary();
	if (!lib) {
		fprintf(stderr,
			"[wasm] RegisterGamePackSource: game library not initialised "
			"yet — call after Gaming Mode entry\n");
		return -1;
	}

	const VDStringW pathW = VDTextU8ToW(VDStringSpanA(utf8Path));

	// Idempotent: bail if this exact path is already a source.  We
	// match case-sensitively because VFS paths are.
	const auto &cur = lib->GetSources();
	for (const auto &s : cur) {
		if (s.mPath == pathW) {
			fprintf(stderr,
				"[wasm] RegisterGamePackSource: '%s' already registered\n",
				utf8Path);
			return 0;
		}
	}

	// Make sure the directory exists before adding a source for it —
	// otherwise the very first scan will skip it silently.  Caller
	// should have created it via ATWasmUnpackArchive, but be defensive.
	if (!VDDoesPathExist(pathW.c_str())) {
		try { VDCreateDirectory(pathW.c_str()); }
		catch (const MyError &e) {
			fprintf(stderr,
				"[wasm] RegisterGamePackSource: cannot create '%s': %s\n",
				utf8Path, e.c_str());
			return -1;
		}
	}

	std::vector<GameSource> next = cur;
	GameSource newSrc;
	newSrc.mPath       = pathW;
	newSrc.mbIsArchive = false;
	newSrc.mbIsFile    = false;
	next.push_back(std::move(newSrc));
	lib->SetSources(std::move(next));

	// Persist the source list and run an immediate scan so the new
	// games show up in the browser without requiring a page reload.
	try {
		lib->SaveSettingsToRegistry();
		ATRegistryFlushToDisk();
		lib->StartScan();
	} catch (const MyError &e) {
		fprintf(stderr,
			"[wasm] RegisterGamePackSource: persist/scan threw: %s\n",
			e.c_str());
	} catch (...) {
		fprintf(stderr,
			"[wasm] RegisterGamePackSource: persist/scan threw unknown\n");
	}

	fprintf(stderr,
		"[wasm] RegisterGamePackSource: added '%s' (now %zu sources)\n",
		utf8Path, lib->GetSources().size());
	return 1;
}

#endif // __EMSCRIPTEN__
