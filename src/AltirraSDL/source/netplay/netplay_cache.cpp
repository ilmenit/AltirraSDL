// AltirraSDL netplay - joiner-side game cache (impl)

#include <stdafx.h>

#include "netplay_cache.h"

#include <cstdio>
#include <cstring>
#include <string>

#include <SDL3/SDL.h>
#include <SDL3/SDL_filesystem.h>

#include <vd2/system/zip.h>           // VDCRCTable::CRC32

#include <at/atcore/logging.h>

extern VDStringA ATGetConfigDir();
extern ATLogChannel g_ATLCNetplay;

namespace ATNetplay {

namespace {

// Build "{configDir}/netplay_cache" — created lazily on first store.
// Reading does not need the directory to exist (SDL_LoadFile fails
// cleanly).  Writing creates it if missing.
std::string CacheDir() {
	std::string out = ATGetConfigDir().c_str();
#ifdef _WIN32
	out += '\\';
#else
	out += '/';
#endif
	out += "netplay_cache";
	return out;
}

// Trim NUL padding off the 8-byte ext field, drop any leading dot,
// and lowercase letters so cache lookups are case-insensitive.  The
// result is empty for legacy hosts that didn't populate the field.
std::string ExtSuffix(const char ext[8]) {
	std::string out;
	for (int i = 0; i < 8; ++i) {
		char c = ext[i];
		if (c == 0) break;
		if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
		out.push_back(c);
	}
	if (!out.empty() && out.front() == '.') out.erase(0, 1);
	return out;
}

std::string CacheFilePath(uint32_t crc32, const char ext[8]) {
	char hex[16];
	std::snprintf(hex, sizeof hex, "%08X", (unsigned)crc32);
	std::string path = CacheDir();
#ifdef _WIN32
	path += '\\';
#else
	path += '/';
#endif
	path += hex;
	std::string suf = ExtSuffix(ext);
	if (!suf.empty()) {
		path += '.';
		path += suf;
	}
	return path;
}

bool EnsureCacheDirExists() {
	const std::string dir = CacheDir();
	// SDL_CreateDirectory is recursive and a no-op when the directory
	// already exists, so it's safe to call every time.
	if (!SDL_CreateDirectory(dir.c_str())) {
		g_ATLCNetplay("netplay_cache: SDL_CreateDirectory(\"%s\") "
			"failed: %s", dir.c_str(), SDL_GetError());
		return false;
	}
	return true;
}

} // namespace

bool NetplayCacheLoad(uint32_t crc32,
                      const char ext[8],
                      std::vector<uint8_t>& out) {
	out.clear();
	if (crc32 == 0) return false;   // host didn't advertise; defensive

	const std::string path = CacheFilePath(crc32, ext);

	size_t loadedLen = 0;
	void *bytes = SDL_LoadFile(path.c_str(), &loadedLen);
	if (!bytes) {
		// Missing file is the common case (cache miss).  SDL leaves an
		// error string set; we don't log it — would be noisy on every
		// session-start cache miss.
		return false;
	}

	// Verify CRC.  Defends against an externally-edited or partially-
	// truncated cache file silently corrupting the session.
	uint32_t actual = VDCRCTable::CRC32.CRC(
		(const uint8_t*)bytes, loadedLen);
	if (actual != crc32) {
		g_ATLCNetplay("netplay_cache: CRC mismatch on \"%s\" — "
			"want %08X, got %08X (corrupt cache file, ignoring)",
			path.c_str(), (unsigned)crc32, (unsigned)actual);
		SDL_free(bytes);
		return false;
	}

	out.assign((const uint8_t*)bytes,
	           (const uint8_t*)bytes + loadedLen);
	SDL_free(bytes);
	g_ATLCNetplay("netplay_cache: hit %08X (%zu bytes from \"%s\")",
		(unsigned)crc32, loadedLen, path.c_str());
	return true;
}

bool NetplayCacheStore(uint32_t crc32,
                       const char ext[8],
                       const uint8_t* data,
                       size_t len) {
	if (crc32 == 0 || data == nullptr || len == 0) return false;
	if (!EnsureCacheDirExists()) return false;

	const std::string finalPath = CacheFilePath(crc32, ext);
	const std::string tmpPath   = finalPath + ".tmp";

	// Verify the bytes actually match the CRC the caller claims, so a
	// corrupt download isn't preserved as a "good" cache for next time.
	uint32_t actual = VDCRCTable::CRC32.CRC(data, len);
	if (actual != crc32) {
		g_ATLCNetplay("netplay_cache: refusing to store CRC mismatch "
			"(want %08X, got %08X, %zu bytes)",
			(unsigned)crc32, (unsigned)actual, len);
		return false;
	}

	// Atomic write: tmp + rename.  SDL_SaveFile writes the whole buffer
	// in one go and creates / truncates as needed.  Then SDL_RenamePath
	// puts the file in place atomically (within a single filesystem on
	// every platform we target — config dir is the user profile).
	if (!SDL_SaveFile(tmpPath.c_str(), data, len)) {
		g_ATLCNetplay("netplay_cache: SDL_SaveFile(\"%s\") failed: %s",
			tmpPath.c_str(), SDL_GetError());
		// Best effort cleanup so we don't leave a half-written file.
		SDL_RemovePath(tmpPath.c_str());
		return false;
	}

	if (!SDL_RenamePath(tmpPath.c_str(), finalPath.c_str())) {
		g_ATLCNetplay("netplay_cache: SDL_RenamePath(\"%s\" -> \"%s\") "
			"failed: %s", tmpPath.c_str(), finalPath.c_str(),
			SDL_GetError());
		SDL_RemovePath(tmpPath.c_str());
		return false;
	}

	g_ATLCNetplay("netplay_cache: stored %08X (%zu bytes -> \"%s\")",
		(unsigned)crc32, len, finalPath.c_str());
	return true;
}

} // namespace ATNetplay
