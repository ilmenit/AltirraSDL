//	AltirraSDL - Game Library
//	Data model, JSON cache, background scanner, and game-art matching.

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <cwctype>
#include <unordered_map>

#include <SDL3/SDL.h>

#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/zip.h>
#include <vd2/system/registry.h>
#include <vd2/vdjson/jsonreader.h>
#include <vd2/vdjson/jsonvalue.h>
#include <vd2/vdjson/jsonwriter.h>
#include <vd2/vdjson/jsonoutput.h>
#include <at/atcore/vfs.h>

#include "game_library.h"

// =========================================================================
// Extension classification
// =========================================================================

GameMediaType ClassifyExtension(const wchar_t *ext) {
	VDStringW e;
	for (const wchar_t *p = ext; *p; ++p)
		e += (wchar_t)std::towlower(*p);

	if (e == L"atr" || e == L"atx" || e == L"xfd" || e == L"dcm" || e == L"pro")
		return GameMediaType::Disk;
	if (e == L"xex" || e == L"obx" || e == L"com" || e == L"exe" || e == L"bas")
		return GameMediaType::Executable;
	if (e == L"car" || e == L"rom" || e == L"bin" || e == L"a52")
		return GameMediaType::Cartridge;
	if (e == L"cas" || e == L"wav" || e == L"flac" || e == L"ogg")
		return GameMediaType::Cassette;

	return GameMediaType::Unknown;
}

bool IsSupportedGameExtension(const wchar_t *name) {
	const wchar_t *ext = wcsrchr(name, L'.');
	if (!ext) return false;
	ext++;

	VDStringW e;
	for (const wchar_t *p = ext; *p; ++p)
		e += (wchar_t)std::towlower(*p);

	static const wchar_t *kExtensions[] = {
		L"xex", L"atr", L"car", L"bin", L"rom", L"cas",
		L"dcm", L"atz", L"zip", L"gz", L"xfd", L"atx",
		L"obx", L"com", L"exe", L"pro", L"a52", L"bas",
		L"wav", L"flac", L"ogg", L"sap", L"vgm", L"vgz",
		L"arc",
		nullptr
	};

	for (const wchar_t **p = kExtensions; *p; ++p) {
		if (e == *p)
			return true;
	}
	return false;
}

bool IsArchiveExtension(const wchar_t *name) {
	const wchar_t *ext = wcsrchr(name, L'.');
	if (!ext) return false;
	ext++;

	VDStringW e;
	for (const wchar_t *p = ext; *p; ++p)
		e += (wchar_t)std::towlower(*p);

	return e == L"zip" || e == L"gz" || e == L"atz" || e == L"arc";
}

bool IsSupportedImageExtension(const wchar_t *name) {
	const wchar_t *ext = wcsrchr(name, L'.');
	if (!ext) return false;
	ext++;

	VDStringW e;
	for (const wchar_t *p = ext; *p; ++p)
		e += (wchar_t)std::towlower(*p);

	return e == L"png" || e == L"jpg" || e == L"jpeg"
		|| e == L"bmp" || e == L"webp";
}

// =========================================================================
// Canonical name extraction (variant tag stripping)
// =========================================================================

static bool IsKnownVariantToken(const VDStringW &token) {
	VDStringW t;
	for (size_t i = 0; i < token.size(); ++i)
		t += (wchar_t)std::towlower(token[i]);

	// Version: v1, v2, v10, ...
	if (t.size() >= 2 && t[0] == L'v') {
		bool allDigits = true;
		for (size_t i = 1; i < t.size(); ++i) {
			if (!std::iswdigit(t[i])) { allDigits = false; break; }
		}
		if (allDigits) return true;
	}

	// Side: s1, s2, ...
	if (t.size() >= 2 && t[0] == L's' && std::iswdigit(t[1])) {
		bool allDigits = true;
		for (size_t i = 1; i < t.size(); ++i) {
			if (!std::iswdigit(t[i])) { allDigits = false; break; }
		}
		if (allDigits) return true;
	}

	// Part: p1, p2, ...
	if (t.size() >= 2 && t[0] == L'p' && std::iswdigit(t[1])) {
		bool allDigits = true;
		for (size_t i = 1; i < t.size(); ++i) {
			if (!std::iswdigit(t[i])) { allDigits = false; break; }
		}
		if (allDigits) return true;
	}

	// Year: 4 digits
	if (t.size() == 4) {
		bool allDigits = true;
		for (size_t i = 0; i < 4; ++i) {
			if (!std::iswdigit(t[i])) { allDigits = false; break; }
		}
		if (allDigits) return true;
	}

	// Fixed keywords
	static const wchar_t *kKeywords[] = {
		L"b", L"cload", L"128", L"320", L"64",
		L"osa", L"osb", L"demo", L"source", L"fixed",
		L"mj", L"g", L"doc", L"pal", L"ntsc",
		nullptr
	};
	for (const wchar_t **kw = kKeywords; *kw; ++kw) {
		if (t == *kw) return true;
	}

	return false;
}

static bool IsVariantTagGroup(const VDStringW &content) {
	// Split by comma, check each token
	uint32 start = 0;
	while (start < content.size()) {
		uint32 comma = content.find(L',', start);
		if (comma == VDStringW::npos)
			comma = content.size();

		// Trim whitespace
		size_t tStart = start;
		size_t tEnd = comma;
		while (tStart < tEnd && std::iswspace(content[tStart])) ++tStart;
		while (tEnd > tStart && std::iswspace(content[tEnd - 1])) --tEnd;

		if (tStart == tEnd) return false;

		VDStringW token(content.data() + tStart, tEnd - tStart);
		if (!IsKnownVariantToken(token))
			return false;

		start = comma + 1;
	}
	return true;
}

VDStringW ExtractCanonicalName(const VDStringW &baseNameNoExt) {
	VDStringW result = baseNameNoExt;

	// Strip trailing ` - Disk N` suffix
	{
		const wchar_t *s = result.c_str();
		size_t len = result.size();
		// Search backwards for " - Disk "
		for (size_t i = len; i >= 8; --i) {
			if (s[i - 8] == L' ' && s[i - 7] == L'-' && s[i - 6] == L' '
				&& s[i - 5] == L'D' && s[i - 4] == L'i' && s[i - 3] == L's'
				&& s[i - 2] == L'k' && s[i - 1] == L' ')
			{
				bool allDigits = true;
				for (size_t j = i; j < len; ++j) {
					if (!std::iswdigit(s[j])) { allDigits = false; break; }
				}
				if (allDigits && i < len) {
					result.resize(i - 8);
					break;
				}
			}
		}
	}

	// Strip trailing parenthesized groups if all tokens are known
	for (;;) {
		size_t len = result.size();
		if (len == 0) break;

		// Find trailing )
		size_t end = len;
		while (end > 0 && std::iswspace(result[end - 1])) --end;
		if (end == 0 || result[end - 1] != L')') break;

		// Find matching (
		const wchar_t *openPtr = wcsrchr(result.c_str(), L'(');
		if (!openPtr || openPtr == result.c_str()) break;
		size_t openParen = (size_t)(openPtr - result.c_str());

		VDStringW content(result.data() + openParen + 1, end - openParen - 2);
		if (!IsVariantTagGroup(content))
			break;

		// Strip the trailing space(s) + parens
		size_t trimTo = openParen;
		while (trimTo > 0 && std::iswspace(result[trimTo - 1])) --trimTo;
		if (trimTo == 0) break;

		result.resize(trimTo);
	}

	// Strip trailing bracket groups [...]
	{
		size_t len = result.size();
		size_t end = len;
		while (end > 0 && std::iswspace(result[end - 1])) --end;
		if (end > 0 && result[end - 1] == L']') {
			const wchar_t *openPtr = wcsrchr(result.c_str(), L'[');
			if (openPtr && openPtr > result.c_str()) {
				size_t openBracket = (size_t)(openPtr - result.c_str());
				size_t trimTo = openBracket;
				while (trimTo > 0 && std::iswspace(result[trimTo - 1])) --trimTo;
				if (trimTo > 0)
					result.resize(trimTo);
			}
		}
	}

	return result;
}

VDStringW CleanDisplayName(const VDStringW &name) {
	VDStringW result;
	result.reserve(name.size());

	for (size_t i = 0; i < name.size(); ++i) {
		wchar_t c = name[i];
		if (c == L'_')
			result += L' ';
		else
			result += c;
	}

	// Title-case if all lowercase or all uppercase
	bool allLower = true, allUpper = true;
	for (size_t i = 0; i < result.size(); ++i) {
		wchar_t c = result[i];
		if (std::iswalpha(c)) {
			if (std::iswupper(c)) allLower = false;
			if (std::iswlower(c)) allUpper = false;
		}
	}

	if (allLower || allUpper) {
		bool wordStart = true;
		for (size_t i = 0; i < result.size(); ++i) {
			wchar_t c = result[i];
			if (std::iswalpha(c)) {
				if (wordStart)
					result[i] = (wchar_t)std::towupper(c);
				else
					result[i] = (wchar_t)std::towlower(c);
				wordStart = false;
			} else {
				wordStart = (c == L' ' || c == L'-' || c == L'.');
			}
		}
	}

	return result;
}

VDStringW BuildVariantLabel(const VDStringW &baseNameNoExt,
	const VDStringW &canonicalName, const wchar_t *ext)
{
	VDStringW label;

	// Extract the stripped tag portion
	if (baseNameNoExt.size() > canonicalName.size()) {
		VDStringW tagPart(baseNameNoExt.data() + canonicalName.size(),
			baseNameNoExt.size() - canonicalName.size());

		// Trim leading whitespace and punctuation
		size_t start = 0;
		while (start < tagPart.size() && (std::iswspace(tagPart[start])
			|| tagPart[start] == L'(' || tagPart[start] == L'['
			|| tagPart[start] == L'-'))
			++start;
		size_t end = tagPart.size();
		while (end > start && (std::iswspace(tagPart[end - 1])
			|| tagPart[end - 1] == L')' || tagPart[end - 1] == L']'))
			--end;

		if (end > start) {
			label.assign(tagPart.data() + start, end - start);
			label += L", ";
		}
	}

	// Append uppercase extension
	for (const wchar_t *p = ext; *p; ++p)
		label += (wchar_t)std::towupper(*p);

	return label;
}

// =========================================================================
// JSON serialization helpers
// =========================================================================

static const wchar_t *MediaTypeToString(GameMediaType t) {
	switch (t) {
		case GameMediaType::Disk:       return L"disk";
		case GameMediaType::Executable: return L"executable";
		case GameMediaType::Cartridge:  return L"cartridge";
		case GameMediaType::Cassette:   return L"cassette";
		default:                        return L"unknown";
	}
}

static GameMediaType MediaTypeFromString(const wchar_t *s) {
	if (wcscmp(s, L"disk") == 0)       return GameMediaType::Disk;
	if (wcscmp(s, L"executable") == 0) return GameMediaType::Executable;
	if (wcscmp(s, L"cartridge") == 0)  return GameMediaType::Cartridge;
	if (wcscmp(s, L"cassette") == 0)   return GameMediaType::Cassette;
	return GameMediaType::Unknown;
}

// =========================================================================
// ATGameLibrary
// =========================================================================

ATGameLibrary::ATGameLibrary() = default;

ATGameLibrary::~ATGameLibrary() {
	CancelScan();
}

void ATGameLibrary::SetConfigDir(const VDStringA &configDir) {
	mConfigDir = configDir;
	mCachePath = configDir;
	if (!mCachePath.empty() && mCachePath.back() != '/')
		mCachePath += '/';
	mCachePath += "gamelibrary.json";
}

// =========================================================================
// JSON cache load
// =========================================================================

// Low-level parse of a JSON cache buffer into freshly-built output
// containers.  Leaves member state untouched on failure so the caller
// (LoadCache) can try another path without corrupting live data.
static bool ParseCacheDocument(const void *buf, size_t size,
	std::vector<GameEntry> &outEntries,
	std::vector<CachedSourceInfo> &outCachedSources,
	uint64_t &outLastScanTime)
{
	VDJSONDocument doc;
	VDJSONReader reader;
	if (!reader.Parse(buf, size, doc))
		return false;

	auto root = doc.Root();
	if (!root.IsObject())
		return false;

	// Schema versions: 1 = original, 2 = adds gameFileCRC32 to variants.
	// v1 caches load fine — the new field defaults to 0 ("not yet
	// computed") so the netplay lazy-CRC path computes and persists
	// it on first use.  Reject anything else (future formats might
	// drop fields we expect).
	auto version = root[L"version"];
	if (!version.IsInt()) return false;
	const int64_t schemaVersion = version.AsInt64();
	if (schemaVersion != 1 && schemaVersion != 2) return false;

	outLastScanTime = 0;
	auto lastScanTime = root[L"lastScanTime"];
	if (lastScanTime.IsInt())
		outLastScanTime = (uint64_t)lastScanTime.AsInt64();

	outCachedSources.clear();
	auto sources = root[L"scannedSources"];
	if (sources.IsArray()) {
		for (const auto &src : sources.AsArray()) {
			CachedSourceInfo csi;
			auto path = src[L"path"];
			if (path.IsString())
				csi.mPath = path.AsString();
			auto mtime = src[L"lastScanMtime"];
			if (mtime.IsInt())
				csi.mLastScanMtime = (uint64_t)mtime.AsInt64();
			outCachedSources.push_back(std::move(csi));
		}
	}

	outEntries.clear();
	auto games = root[L"games"];
	if (games.IsArray()) {
		for (const auto &g : games.AsArray()) {
			GameEntry entry;

			auto displayName = g[L"displayName"];
			if (displayName.IsString())
				entry.mDisplayName = displayName.AsString();

			auto artPath = g[L"artPath"];
			if (artPath.IsString())
				entry.mArtPath = artPath.AsString();

			auto lastPlayed = g[L"lastPlayed"];
			if (lastPlayed.IsInt())
				entry.mLastPlayed = (uint64_t)lastPlayed.AsInt64();

			auto playCount = g[L"playCount"];
			if (playCount.IsInt())
				entry.mPlayCount = (uint32_t)playCount.AsInt64();

			auto variants = g[L"variants"];
			if (variants.IsArray()) {
				for (const auto &v : variants.AsArray()) {
					GameVariant var;

					auto vPath = v[L"path"];
					if (vPath.IsString())
						var.mPath = vPath.AsString();

					auto archivePath = v[L"archivePath"];
					if (archivePath.IsString())
						var.mArchivePath = archivePath.AsString();

					auto type = v[L"type"];
					if (type.IsString())
						var.mType = MediaTypeFromString(type.AsString());

					auto fileSize = v[L"fileSize"];
					if (fileSize.IsInt())
						var.mFileSize = (uint64_t)fileSize.AsInt64();

					auto modTime = v[L"modTime"];
					if (modTime.IsInt())
						var.mModTime = (uint64_t)modTime.AsInt64();

					auto label = v[L"label"];
					if (label.IsString())
						var.mLabel = label.AsString();

					// v2: optional persistent CRC32 of game-file
					// bytes.  Missing = not yet computed (lazy-fill
					// on first netplay cache lookup).
					auto crc = v[L"gameFileCRC32"];
					if (crc.IsInt())
						var.mGameFileCRC32 = (uint32_t)crc.AsInt64();

					entry.mVariants.push_back(std::move(var));
				}
			}

			// Rebuild canonical name and parent dir from first variant.
			if (!entry.mVariants.empty()) {
				const VDStringW &path = entry.mVariants[0].mPath;
				const wchar_t *lastSlash = wcsrchr(path.c_str(), L'/');
				if (lastSlash)
					entry.mParentDir.assign(path.c_str(),
						lastSlash - path.c_str());

				const wchar_t *fname = lastSlash
					? lastSlash + 1 : path.c_str();
				VDStringW baseName(fname);
				const wchar_t *dot = wcsrchr(baseName.c_str(), L'.');
				if (dot) baseName.resize(dot - baseName.c_str());
				entry.mCanonicalName = ExtractCanonicalName(baseName);
			}

			outEntries.push_back(std::move(entry));
		}
	}

	return true;
}

// Try to read + parse the JSON cache at the given path.  Returns
// true if the file exists, is well-formed, and was successfully
// decoded into the output parameters.  Empty/oversized/corrupt files
// return false without touching the outputs.
//
// First-run state — when gamelibrary.json doesn't exist yet — must
// stay on the silent path: VDFileStream's throwing ctor surfaces
// "Cannot open file ... No such file or directory" as a VDException,
// and on Emscripten WASM that exception escaped past the catch(...)
// here and reached the JS-level error handler, terminating the
// page.  Probe with VDDoesPathExist (a plain stat()) before opening
// so missing-file is a clean false return rather than an unwind.
// The catch(...) is kept as a safety net for true I/O errors that
// occur after open succeeds.
static bool TryLoadCacheFile(const VDStringW &path,
	std::vector<GameEntry> &outEntries,
	std::vector<CachedSourceInfo> &outCachedSources,
	uint64_t &outLastScanTime)
{
	if (!VDDoesPathExist(path.c_str()))
		return false;

	try {
		VDFileStream fs(path.c_str());
		sint64 size = fs.Length();
		if (size <= 0 || size > 64 * 1024 * 1024)
			return false;

		std::vector<uint8_t> buf((size_t)size);
		fs.Read(buf.data(), (sint32)size);

		return ParseCacheDocument(buf.data(), buf.size(),
			outEntries, outCachedSources, outLastScanTime);
	} catch (...) {
		return false;
	}
}

bool ATGameLibrary::LoadCache() {
	mMainFileValid = false;
	if (mCachePath.empty())
		return false;

	VDStringW cachePath = VDTextU8ToW(mCachePath);
	VDStringW bakPath   = cachePath;
	bakPath += L".bak";

	std::vector<GameEntry>        tmpEntries;
	std::vector<CachedSourceInfo> tmpSources;
	uint64_t                       tmpLastScanTime = 0;

	// Main file is the authoritative state.  Try it first.
	if (TryLoadCacheFile(cachePath, tmpEntries, tmpSources, tmpLastScanTime)) {
		mEntries       = std::move(tmpEntries);
		mCachedSources = std::move(tmpSources);
		mLastScanTime  = tmpLastScanTime;
		mMainFileValid = true;
		return true;
	}

	// Main was missing, truncated, or corrupt — fall back to the .bak
	// snapshot written by the previous SaveCache.  Leave mMainFileValid
	// false so the next SaveCache *won't* overwrite the good .bak with
	// the known-bad main file during rotation.
	if (TryLoadCacheFile(bakPath, tmpEntries, tmpSources, tmpLastScanTime)) {
		mEntries       = std::move(tmpEntries);
		mCachedSources = std::move(tmpSources);
		mLastScanTime  = tmpLastScanTime;
		// mMainFileValid stays false.
		return true;
	}

	mEntries.clear();
	mCachedSources.clear();
	return false;
}

// =========================================================================
// JSON cache save
// =========================================================================

// Serialise the current library state to the given filesystem path.
// Fresh file every call (kCreateAlways), JSON format matching the
// schema understood by ParseCacheDocument.
bool ATGameLibrary::WriteCacheFile(const VDStringW &path) const {
	try {
		VDFileStream fs(path.c_str(),
			nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
		VDJSONStreamOutput streamOutput(fs);
		VDJSONWriter writer;
		writer.Begin(&streamOutput, false);

		writer.OpenObject();

		writer.WriteMemberName(L"version");
		// v2 adds variant.gameFileCRC32 (lazy-filled by netplay cache
		// lookup).  ParseCacheDocument accepts both 1 and 2.
		writer.WriteInt(2);

		writer.WriteMemberName(L"lastScanTime");
		writer.WriteInt((sint64)mLastScanTime);

		// Write scanned sources
		writer.WriteMemberName(L"scannedSources");
		writer.OpenArray();
		for (const auto &src : mCachedSources) {
			writer.OpenObject();
			writer.WriteMemberName(L"path");
			writer.WriteString(src.mPath.c_str());
			writer.WriteMemberName(L"lastScanMtime");
			writer.WriteInt((sint64)src.mLastScanMtime);
			writer.Close();
		}
		writer.Close();

		// Write game entries
		writer.WriteMemberName(L"games");
		writer.OpenArray();
		for (const auto &entry : mEntries) {
			writer.OpenObject();

			writer.WriteMemberName(L"displayName");
			writer.WriteString(entry.mDisplayName.c_str());

			writer.WriteMemberName(L"artPath");
			writer.WriteString(entry.mArtPath.c_str());

			writer.WriteMemberName(L"lastPlayed");
			writer.WriteInt((sint64)entry.mLastPlayed);

			writer.WriteMemberName(L"playCount");
			writer.WriteInt((sint64)entry.mPlayCount);

			writer.WriteMemberName(L"variants");
			writer.OpenArray();
			for (const auto &var : entry.mVariants) {
				writer.OpenObject();
				writer.WriteMemberName(L"path");
				writer.WriteString(var.mPath.c_str());
				writer.WriteMemberName(L"archivePath");
				writer.WriteString(var.mArchivePath.c_str());
				writer.WriteMemberName(L"type");
				writer.WriteString(MediaTypeToString(var.mType));
				writer.WriteMemberName(L"fileSize");
				writer.WriteInt((sint64)var.mFileSize);
				writer.WriteMemberName(L"modTime");
				writer.WriteInt((sint64)var.mModTime);
				writer.WriteMemberName(L"label");
				writer.WriteString(var.mLabel.c_str());
				// v2: persistent gameFileCRC32 (0 when not yet
				// computed).  Always written so a v1 cache loaded
				// once and saved is upgraded transparently; readers
				// older than v2 ignore the unknown field.
				writer.WriteMemberName(L"gameFileCRC32");
				writer.WriteInt((sint64)var.mGameFileCRC32);
				writer.Close();
			}
			writer.Close();

			writer.Close();
		}
		writer.Close();

		writer.Close();
		writer.End();
		streamOutput.Flush();

		return true;
	} catch (...) {
		return false;
	}
}

bool ATGameLibrary::SaveCache() const {
	if (mCachePath.empty())
		return false;

	// Crash-safe save:
	//   1. Serialise JSON into gamelibrary.json.tmp
	//   2. If the current main file is known-good, copy it to
	//      gamelibrary.json.bak so a future LoadCache has a fallback
	//      (we deliberately skip this step when the main file was
	//      loaded from .bak this session — overwriting the .bak with
	//      a known-corrupt main would destroy our only recoverable
	//      state).
	//   3. Atomically rename the temp file over the main file.
	//
	// A crash at any point leaves either the old main file (steps 1/2)
	// or both a good main and a good .bak (step 3) — never a truncated
	// main with no recoverable backup.

	VDStringW cachePath = VDTextU8ToW(mCachePath);
	VDStringW tmpPath   = cachePath;
	tmpPath += L".tmp";
	VDStringW bakPath   = cachePath;
	bakPath += L".bak";

	if (!WriteCacheFile(tmpPath)) {
		// Best-effort cleanup; failures here are harmless.
		VDStringA tmpU8 = VDTextWToU8(tmpPath);
		SDL_RemovePath(tmpU8.c_str());
		return false;
	}

	VDStringA cacheU8 = VDTextWToU8(cachePath);
	VDStringA tmpU8   = VDTextWToU8(tmpPath);
	VDStringA bakU8   = VDTextWToU8(bakPath);

	// Rotate the current main -> .bak, but only if we trust main.
	if (mMainFileValid && VDDoesPathExist(cachePath.c_str()))
		SDL_CopyFile(cacheU8.c_str(), bakU8.c_str());

	if (!SDL_RenamePath(tmpU8.c_str(), cacheU8.c_str())) {
		// Rename failed (extremely unusual within the same directory).
		// Leave the temp in place so the next attempt can either reuse
		// or overwrite it.  The main file is still whatever it was
		// before — the .bak may already have been refreshed above,
		// which is fine: next successful save keeps them in sync.
		return false;
	}

	mMainFileValid = true;
	return true;
}

// =========================================================================
// Play history
// =========================================================================

void ATGameLibrary::RecordPlay(size_t entryIndex) {
	if (entryIndex >= mEntries.size())
		return;

	auto &entry = mEntries[entryIndex];
	entry.mLastPlayed = (uint64_t)std::time(nullptr);
	entry.mPlayCount++;
	SaveCache();
}

int ATGameLibrary::FindEntryByVariantPath(const VDStringW &path) const {
	if (path.empty())
		return -1;
	for (size_t i = 0; i < mEntries.size(); ++i) {
		for (const auto &var : mEntries[i].mVariants) {
			if (var.mPath == path)
				return (int)i;
		}
	}
	return -1;
}

// Lowercase + drop-leading-dot extension comparator for the netplay
// CRC lookup.  Boot-config carries a NUL-padded 8-byte field that
// usually starts with a '.'; library paths don't.
static bool MatchExt8(const wchar_t *libPath, const char expectedExt8[8]) {
	if (!libPath) return false;
	// Pull the library path's extension (after the last '.').
	const wchar_t *dot = nullptr;
	for (const wchar_t *p = libPath; *p; ++p) {
		if (*p == L'.') dot = p;
	}
	if (!dot) return false;
	const wchar_t *libExt = dot + 1;

	// Normalize the boot-config field: strip leading '.', truncate
	// at NUL, lowercase ASCII letters.
	char want[9] = {};
	int wi = 0;
	for (int i = 0; i < 8 && expectedExt8[i] != 0 && wi < 8; ++i) {
		char c = expectedExt8[i];
		if (i == 0 && c == '.') continue;
		if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
		want[wi++] = c;
	}
	if (wi == 0) return false;

	// Compare lib extension lowercase vs want.
	int li = 0;
	while (libExt[li] && li < 8) {
		wchar_t lc = libExt[li];
		if (lc >= L'A' && lc <= L'Z') lc = (wchar_t)(lc - L'A' + L'a');
		if (li >= wi) return false;
		if ((wchar_t)want[li] != lc) return false;
		++li;
	}
	if (libExt[li] != 0) return false;   // lib ext longer than want
	return li == wi;
}

bool ATGameLibrary::FindVariantBytesForCRC32(uint32_t crc32,
                                             uint64_t expectedSize,
                                             const char expectedExt8[8],
                                             std::vector<uint8_t>& outBytes) {
	outBytes.clear();
	if (crc32 == 0 || expectedSize == 0 || !expectedExt8) return false;

	bool persistedAny = false;

	// Walk all variants.  Filter by (size, ext) before reading file
	// bytes — that's what keeps this fast on large libraries (typical
	// (size, ext) bucket has 0-1 candidates).
	for (auto &entry : mEntries) {
		for (auto &var : entry.mVariants) {
			if (var.mFileSize != expectedSize) continue;
			if (!MatchExt8(var.mPath.c_str(), expectedExt8)) continue;
			if (var.mPath.empty()) continue;

			// Try cached CRC first — instant when present.
			uint32_t haveCrc = var.mGameFileCRC32;
			std::vector<uint8_t> bytes;

			if (haveCrc == 0) {
				// Read the file once, compute CRC, persist.  Use
				// VFS open so zip://outer!inner virtual paths from
				// the archive scan resolve to inner bytes (mirrors
				// the host's shipping path in
				// ui_netplay_actions.cpp:1203).
				try {
					vdrefptr<ATVFSFileView> view;
					ATVFSOpenFileView(var.mPath.c_str(), false, ~view);
					if (!view) continue;
					IVDRandomAccessStream& fs = view->GetStream();
					sint64 sz = fs.Length();
					if (sz <= 0) continue;
					// Defensive: file size on disk may differ from
					// cached mFileSize if user replaced the file
					// since the last scan.
					if ((uint64_t)sz != expectedSize) continue;
					bytes.resize((size_t)sz);
					fs.Seek(0);
					fs.Read(bytes.data(), (sint32)bytes.size());
				} catch (...) {
					continue;
				}
				haveCrc = VDCRCTable::CRC32.CRC(
					bytes.data(), bytes.size());
				var.mGameFileCRC32 = haveCrc;
				persistedAny = true;
			}

			if (haveCrc != crc32) continue;

			// Cached CRC matched (and we either already have bytes
			// from the lazy-fill path above, or we still need to
			// read them).
			if (bytes.empty()) {
				try {
					vdrefptr<ATVFSFileView> view;
					ATVFSOpenFileView(var.mPath.c_str(), false, ~view);
					if (!view) continue;
					IVDRandomAccessStream& fs = view->GetStream();
					sint64 sz = fs.Length();
					if (sz <= 0 || (uint64_t)sz != expectedSize) continue;
					bytes.resize((size_t)sz);
					fs.Seek(0);
					fs.Read(bytes.data(), (sint32)bytes.size());
					// Re-verify CRC of bytes we just read — guards
					// against a stale `mGameFileCRC32` after the
					// user externally replaced the file.
					uint32_t actual = VDCRCTable::CRC32.CRC(
						bytes.data(), bytes.size());
					if (actual != crc32) {
						// Cached CRC was wrong.  Refresh it and skip.
						var.mGameFileCRC32 = actual;
						persistedAny = true;
						continue;
					}
				} catch (...) {
					continue;
				}
			}

			// Hit.  Persist any newly-computed CRCs before returning
			// so the next lookup is instant.
			if (persistedAny) SaveCache();
			outBytes = std::move(bytes);
			return true;
		}
	}

	if (persistedAny) SaveCache();
	return false;
}

// Build a single-variant entry in-place from a real filesystem path.
// Mirrors the file-handling branch of ScanFolder's callback: stat the
// file, classify the extension, build canonical name + variant label.
static bool BuildEntryForFile(const VDStringW &path, GameEntry &outEntry) {
	if (path.empty())
		return false;

	VDStringA pathU8 = VDTextWToU8(path);
	SDL_PathInfo info;
	if (!SDL_GetPathInfo(pathU8.c_str(), &info))
		return false;
	if (info.type != SDL_PATHTYPE_FILE)
		return false;

	const wchar_t *lastSlash = wcsrchr(path.c_str(), L'/');
	VDStringW fname = lastSlash ? VDStringW(lastSlash + 1) : path;
	if (fname.empty())
		return false;

	if (!IsSupportedGameExtension(fname.c_str()))
		return false;

	const wchar_t *ext = wcsrchr(fname.c_str(), L'.');
	if (!ext) return false;
	ext++;

	GameMediaType mediaType = ClassifyExtension(ext);
	if (mediaType == GameMediaType::Unknown)
		return false;

	VDStringW baseName(fname.c_str());
	const wchar_t *dot = wcsrchr(baseName.c_str(), L'.');
	if (dot) baseName.resize(dot - baseName.c_str());

	VDStringW canonical = ExtractCanonicalName(baseName);

	VDStringW parentDir;
	if (lastSlash)
		parentDir.assign(path.c_str(), lastSlash - path.c_str());

	GameVariant var;
	var.mPath = path;
	var.mType = mediaType;
	var.mFileSize = (uint64_t)info.size;
	var.mModTime = (uint64_t)info.modify_time;

	// Single-variant entry: label is just the uppercase extension.
	VDStringW label;
	for (const wchar_t *p = ext; *p; ++p)
		label += (wchar_t)std::towupper(*p);
	var.mLabel = std::move(label);

	outEntry = GameEntry{};
	outEntry.mCanonicalName = std::move(canonical);
	outEntry.mParentDir = parentDir;
	outEntry.mDisplayName = CleanDisplayName(outEntry.mCanonicalName);
	outEntry.mVariants.push_back(std::move(var));

	// Best-effort same-folder art match: look for <base>.{png,jpg,...}
	// next to the file so the auto-added entry has cover art on first
	// display (scanner's cross-folder MatchArt runs only on full rescans).
	VDStringA parentU8 = VDTextWToU8(parentDir);
	if (!parentU8.empty()) {
		struct ArtCtx {
			VDStringW lowerBase;
			VDStringW match;
		} actx;
		for (size_t i = 0; i < outEntry.mCanonicalName.size(); ++i)
			actx.lowerBase += (wchar_t)std::towlower(outEntry.mCanonicalName[i]);

		SDL_EnumerateDirectory(parentU8.c_str(),
			[](void *ud, const char *dirname, const char *fname)
				-> SDL_EnumerationResult
			{
				auto *c = (ArtCtx *)ud;
				if (!c->match.empty())
					return SDL_ENUM_CONTINUE;
				VDStringW wname = VDTextU8ToW(VDStringA(fname));
				if (!IsSupportedImageExtension(wname.c_str()))
					return SDL_ENUM_CONTINUE;
				VDStringW base(wname);
				const wchar_t *d = wcsrchr(base.c_str(), L'.');
				if (d) base.resize(d - base.c_str());
				VDStringW baseLower;
				for (size_t i = 0; i < base.size(); ++i)
					baseLower += (wchar_t)std::towlower(base[i]);
				if (baseLower == c->lowerBase) {
					VDStringA full(dirname);
					if (!full.empty() && full.back() != '/')
						full += '/';
					full += fname;
					c->match = VDTextU8ToW(full);
				}
				return SDL_ENUM_CONTINUE;
			}, &actx);

		if (!actx.match.empty())
			outEntry.mArtPath = std::move(actx.match);
	}
	return true;
}

bool ATGameLibrary::ScanFile(const VDStringW &path,
	std::vector<GameEntry> &outEntries)
{
	GameEntry entry;
	if (!BuildEntryForFile(path, entry))
		return false;
	outEntries.push_back(std::move(entry));
	mScanProgress.fetch_add(1, std::memory_order_relaxed);
	return true;
}

int ATGameLibrary::AddBootedGame(const VDStringW &path, bool addToLibrary) {
	if (path.empty())
		return -1;

	// Already in the library?  Just bump play history.
	int existing = FindEntryByVariantPath(path);
	if (existing >= 0) {
		RecordPlay((size_t)existing);
		return existing;
	}

	if (!addToLibrary)
		return -1;

	// We're about to mutate mSources and mEntries.  Join the background
	// scanner first so it can't race on the vector while we resize it
	// (mSources has no mutex; the existing SetSources/StartScan pattern
	// races in the same way and we avoid it here).  If the scan has
	// already produced results but the UI hasn't consumed them yet,
	// fold them in now so we don't throw them away on cancel.  Remember
	// whether a scan was in flight so we can restart it on exit — we
	// don't want booting a game to drop the "Scanning..." indicator
	// halfway through a first-time scan.
	bool wasScanning = IsScanning();
	if (mScanComplete.load(std::memory_order_acquire)) {
		ConsumeScanResults();
		wasScanning = false;  // scan finished; no need to restart.
	}
	CancelScan();

	// Re-check after consume — scan results may have added the game.
	existing = FindEntryByVariantPath(path);
	if (existing >= 0) {
		RecordPlay((size_t)existing);
		if (wasScanning)
			StartScan();
		return existing;
	}

	// Try to add as a standalone file entry.  Extract the archive path
	// if this is a VFS (zip://...) reference so we can register the
	// archive itself as an archive source.
	VDStringW basePath, subPath;
	ATVFSProtocol proto = ATParseVFSPath(path.c_str(), basePath, subPath);
	bool isInsideArchive = (proto == kATVFSProtocol_Zip
		|| proto == kATVFSProtocol_GZip);

	if (isInsideArchive) {
		// Reject unrecognised extensions — the scanner ignores them
		// inside archives too, so a stub would never get merged.
		const wchar_t *lastSlashSub = wcsrchr(subPath.c_str(), L'/');
		VDStringW fname = lastSlashSub ? VDStringW(lastSlashSub + 1) : subPath;
		const wchar_t *ext = wcsrchr(fname.c_str(), L'.');
		if (!ext) {
			if (wasScanning) StartScan();
			return -1;
		}
		GameMediaType mediaType = ClassifyExtension(ext + 1);
		if (mediaType == GameMediaType::Unknown) {
			if (wasScanning) StartScan();
			return -1;
		}

		// Register the archive as a source if it isn't already, then
		// build a minimal stub entry so the play history is recorded
		// immediately.  When the background scan finishes, the scanner
		// rebuilds the full entry from the archive and MergePlayHistory
		// preserves the stub's lastPlayed/playCount via path match.
		bool alreadySource = false;
		for (const auto &s : mSources) {
			if (s.mbIsArchive && s.mPath == basePath) {
				alreadySource = true;
				break;
			}
		}
		if (!alreadySource) {
			GameSource src;
			src.mPath = basePath;
			src.mbIsArchive = true;
			mSources.push_back(std::move(src));
			SaveSettingsToRegistry();
		}

		VDStringW baseName = fname;
		const wchar_t *dot = wcsrchr(baseName.c_str(), L'.');
		if (dot) baseName.resize(dot - baseName.c_str());

		GameVariant var;
		var.mPath = path;
		var.mArchivePath = basePath;
		var.mType = mediaType;

		VDStringW upperExt;
		for (const wchar_t *p = ext + 1; *p; ++p)
			upperExt += (wchar_t)std::towupper(*p);
		var.mLabel = std::move(upperExt);

		GameEntry stub;
		stub.mCanonicalName = ExtractCanonicalName(baseName);
		stub.mDisplayName = CleanDisplayName(stub.mCanonicalName);
		stub.mParentDir = basePath;
		stub.mLastPlayed = (uint64_t)std::time(nullptr);
		stub.mPlayCount = 1;
		stub.mVariants.push_back(std::move(var));
		mEntries.push_back(std::move(stub));

		std::sort(mEntries.begin(), mEntries.end(),
			[](const GameEntry &a, const GameEntry &b) {
				return a.mDisplayName < b.mDisplayName;
			});

		SaveCache();
		StartScan();
		return FindEntryByVariantPath(path);
	}

	GameEntry entry;
	if (!BuildEntryForFile(path, entry)) {
		if (wasScanning)
			StartScan();
		return -1;
	}

	entry.mLastPlayed = (uint64_t)std::time(nullptr);
	entry.mPlayCount = 1;

	// If the file is already covered by an existing folder source (or
	// would be on a recursive scan), don't add a redundant file source —
	// the inline entry below will be replaced by the scanner's version
	// on the next rescan and play history is preserved via
	// MergePlayHistory.
	auto fileUnderFolderSource = [this, &path]() -> bool {
		for (const auto &s : mSources) {
			if (s.mbIsArchive || s.mbIsFile)
				continue;
			const VDStringW &sp = s.mPath;
			if (path.size() > sp.size()
				&& wcsncmp(path.c_str(), sp.c_str(), sp.size()) == 0
				&& (path[sp.size()] == L'/' || path[sp.size()] == L'\\'))
			{
				if (mSettings.mbRecursive)
					return true;
				// Non-recursive: only covered when directly inside.
				const wchar_t *tail = path.c_str() + sp.size() + 1;
				if (!wcschr(tail, L'/') && !wcschr(tail, L'\\'))
					return true;
			}
		}
		return false;
	};

	bool alreadyCovered = fileUnderFolderSource();
	if (!alreadyCovered) {
		bool alreadySource = false;
		for (const auto &s : mSources) {
			if (s.mbIsFile && s.mPath == path) {
				alreadySource = true;
				break;
			}
		}
		if (!alreadySource) {
			GameSource src;
			src.mPath = path;
			src.mbIsFile = true;
			mSources.push_back(std::move(src));
			SaveSettingsToRegistry();
			// Flush so a process kill before the next regular save
			// (clean exit / suspend) does not lose the source list.
			extern void ATRegistryFlushToDisk();
			try { ATRegistryFlushToDisk(); } catch (...) {}
		}
	}

	mEntries.push_back(std::move(entry));

	// Keep the alphabetical-by-display-name invariant that the scanner
	// produces, so the new entry slots into its natural position.
	std::sort(mEntries.begin(), mEntries.end(),
		[](const GameEntry &a, const GameEntry &b) {
			return a.mDisplayName < b.mDisplayName;
		});

	SaveCache();
	if (wasScanning)
		StartScan();
	return FindEntryByVariantPath(path);
}

void ATGameLibrary::ClearHistory() {
	for (auto &entry : mEntries) {
		entry.mLastPlayed = 0;
		entry.mPlayCount = 0;
	}
	SaveCache();
}

// =========================================================================
// Settings persistence (registry)
// =========================================================================

static const char kGameLibKey[] = "GameLibrary";

void ATGameLibrary::SetSources(std::vector<GameSource> sources) {
	mSources = std::move(sources);
}

void ATGameLibrary::PurgeRemovedSourceEntries() {
	if (mSources.empty()) {
		mEntries.clear();
		return;
	}

	mEntries.erase(
		std::remove_if(mEntries.begin(), mEntries.end(),
			[this](const GameEntry &entry) -> bool {
				for (const auto &var : entry.mVariants) {
					for (const auto &src : mSources) {
						if (src.mbIsFile) {
							if (var.mPath == src.mPath)
								return false;
						} else if (src.mbIsArchive) {
							if (var.mArchivePath == src.mPath)
								return false;
						} else {
							const VDStringW &sp = src.mPath;
							const VDStringW &vp = var.mPath;
							if (vp.size() > sp.size()
								&& wcsncmp(vp.c_str(), sp.c_str(), sp.size()) == 0
								&& (vp[sp.size()] == L'/' || vp[sp.size()] == L'\\'))
								return false;
						}
					}
				}
				return true;
			}),
		mEntries.end());
}

void ATGameLibrary::SetSettings(const GameLibrarySettings &settings) {
	mSettings = settings;
}

void ATGameLibrary::LoadSettingsFromRegistry() {
	VDRegistryAppKey key(kGameLibKey, false);
	if (!key.isReady())
		return;

	mSettings.mbRecursive = key.getBool("Recursive", true);
	mSettings.mbCrossFolderArt = key.getBool("CrossFolderArt", true);
	mSettings.mbShowOnStartup = key.getBool("ShowOnStartup", true);
	mSettings.mbAddBootedToLibrary = key.getBool("AddBootedToLibrary", true);
	mSettings.mViewMode = key.getInt("ViewMode", 1);
	mSettings.mGridSize = key.getInt("GridSize", 1);
	if (mSettings.mGridSize < 0 || mSettings.mGridSize > 2)
		mSettings.mGridSize = 1;
	mSettings.mListSize = key.getInt("ListSize", 0);
	if (mSettings.mListSize < 0 || mSettings.mListSize > 2)
		mSettings.mListSize = 0;

	int sourceCount = key.getInt("SourceCount", 0);
	mSources.clear();
	for (int i = 0; i < sourceCount; ++i) {
		VDStringA pathKey;
		pathKey.sprintf("Source%d.Path", i);
		VDStringA archKey;
		archKey.sprintf("Source%d.IsArchive", i);
		VDStringA fileKey;
		fileKey.sprintf("Source%d.IsFile", i);

		VDStringW path;
		if (key.getString(pathKey.c_str(), path)) {
			GameSource src;
			src.mPath = std::move(path);
			src.mbIsArchive = key.getBool(archKey.c_str(), false);
			src.mbIsFile = key.getBool(fileKey.c_str(), false);
			mSources.push_back(std::move(src));
		}
	}
}

void ATGameLibrary::SaveSettingsToRegistry() const {
	VDRegistryAppKey key(kGameLibKey, true);
	if (!key.isReady())
		return;

	key.setBool("Recursive", mSettings.mbRecursive);
	key.setBool("CrossFolderArt", mSettings.mbCrossFolderArt);
	key.setBool("ShowOnStartup", mSettings.mbShowOnStartup);
	key.setBool("AddBootedToLibrary", mSettings.mbAddBootedToLibrary);
	key.setInt("ViewMode", mSettings.mViewMode);
	key.setInt("GridSize", mSettings.mGridSize);
	key.setInt("ListSize", mSettings.mListSize);

	key.setInt("SourceCount", (int)mSources.size());
	for (int i = 0; i < (int)mSources.size(); ++i) {
		VDStringA pathKey;
		pathKey.sprintf("Source%d.Path", i);
		VDStringA archKey;
		archKey.sprintf("Source%d.IsArchive", i);
		VDStringA fileKey;
		fileKey.sprintf("Source%d.IsFile", i);

		key.setString(pathKey.c_str(), mSources[i].mPath.c_str());
		key.setBool(archKey.c_str(), mSources[i].mbIsArchive);
		key.setBool(fileKey.c_str(), mSources[i].mbIsFile);
	}
}

// =========================================================================
// Background scanner
// =========================================================================

void ATGameLibrary::StartScan() {
	CancelScan();

	mScanComplete.store(false, std::memory_order_release);
	mScanCancel.store(false, std::memory_order_release);
	mScanProgress.store(0, std::memory_order_release);
	{
		std::lock_guard<std::mutex> lock(mScanMutex);
		mScanStatus.clear();
	}
	mScanning.store(true, std::memory_order_release);

#if defined(__EMSCRIPTEN__)
	// WASM: run the scan inline on the main thread.  The library on a
	// browser build lives under the IndexedDB-backed virtual filesystem
	// (/persist/games) with at most a few dozen entries, so a synchronous
	// scan completes well under one frame.  Running it on a background
	// thread would require -pthread + SharedArrayBuffer — see
	// docs/BUILD.md "WebAssembly Build".
	ScanThread();
	// Consume the results synchronously so callers see the refreshed
	// entry list immediately (mirrors what ConsumeScanResults does on
	// desktop after the thread completes).
	ConsumeScanResults();
#else
	mScanThread = std::thread([this]() { ScanThread(); });
#endif
}

void ATGameLibrary::CancelScan() {
#if defined(__EMSCRIPTEN__)
	// No background thread to cancel — StartScan is synchronous on WASM.
	mScanCancel.store(true, std::memory_order_release);
#else
	if (mScanThread.joinable()) {
		mScanCancel.store(true, std::memory_order_release);
		mScanThread.join();
	}
#endif
	mScanning.store(false, std::memory_order_release);
}

VDStringA ATGameLibrary::GetScanStatus() const {
	std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mScanMutex));
	return mScanStatus;
}

void ATGameLibrary::ConsumeScanResults() {
	if (!mScanComplete.load(std::memory_order_acquire))
		return;

	std::vector<GameEntry> results;
	{
		std::lock_guard<std::mutex> lock(mScanMutex);
		results = std::move(mScanResults);
	}

	MergePlayHistory(results, mEntries);
	mEntries = std::move(results);
	mLastScanTime = (uint64_t)std::time(nullptr);

	// Update cached source info
	mCachedSources.clear();
	for (const auto &src : mSources) {
		CachedSourceInfo csi;
		csi.mPath = src.mPath;
		VDStringA pathU8 = VDTextWToU8(src.mPath);
		SDL_PathInfo info;
		if (SDL_GetPathInfo(pathU8.c_str(), &info))
			csi.mLastScanMtime = (uint64_t)info.modify_time;
		mCachedSources.push_back(std::move(csi));
	}

	SaveCache();
	mScanComplete.store(false, std::memory_order_release);
	mScanning.store(false, std::memory_order_release);
}

void ATGameLibrary::ScanThread() {
	std::vector<GameEntry> allEntries;
	std::vector<VDStringW> allImages;

	for (const auto &src : mSources) {
		if (mScanCancel.load(std::memory_order_acquire))
			return;

		{
			VDStringA srcU8 = VDTextWToU8(src.mPath);
			const char *name = strrchr(srcU8.c_str(), '/');
			std::lock_guard<std::mutex> lock(mScanMutex);
			mScanStatus = name ? (name + 1) : srcU8.c_str();
		}

		if (src.mbIsFile) {
			ScanFile(src.mPath, allEntries);
		} else if (src.mbIsArchive) {
			ScanArchive(src.mPath, allEntries, allImages);
		} else {
			ScanFolder(src.mPath, mSettings.mbRecursive, allEntries, allImages);
		}
	}

	if (mScanCancel.load(std::memory_order_acquire))
		return;

	{
		std::lock_guard<std::mutex> lock(mScanMutex);
		mScanStatus = "grouping variants...";
	}
	GroupVariants(allEntries);
	DisambiguateNames(allEntries);
	MatchArt(allEntries, allImages);

	std::sort(allEntries.begin(), allEntries.end(),
		[](const GameEntry &a, const GameEntry &b) {
			return a.mDisplayName < b.mDisplayName;
		});

	{
		std::lock_guard<std::mutex> lock(mScanMutex);
		mScanResults = std::move(allEntries);
		mScanStatus.clear();
	}
	mScanComplete.store(true, std::memory_order_release);
}

// =========================================================================
// Folder scanning
// =========================================================================

void ATGameLibrary::ScanFolder(const VDStringW &path, bool recursive,
	std::vector<GameEntry> &outEntries, std::vector<VDStringW> &outImages)
{
	struct ScanCtx {
		ATGameLibrary *self;
		VDStringW baseDir;
		std::vector<GameEntry> *entries;
		std::vector<VDStringW> *images;
		bool recursive;
	};

	ScanCtx ctx;
	ctx.self = this;
	ctx.baseDir = path;
	if (!ctx.baseDir.empty() && ctx.baseDir.back() != L'/')
		ctx.baseDir += L'/';
	ctx.entries = &outEntries;
	ctx.images = &outImages;
	ctx.recursive = recursive;

	VDStringA dirU8 = VDTextWToU8(path);

	auto callback = [](void *userdata, const char *dirname, const char *fname) -> SDL_EnumerationResult {
		ScanCtx *ctx = (ScanCtx *)userdata;

		if (ctx->self->mScanCancel.load(std::memory_order_acquire))
			return SDL_ENUM_FAILURE;

		if (fname[0] == '.')
			return SDL_ENUM_CONTINUE;

		VDStringA fullU8 = VDStringA(dirname);
		if (!fullU8.empty() && fullU8.back() != '/')
			fullU8 += '/';
		fullU8 += fname;

		SDL_PathInfo info;
		if (!SDL_GetPathInfo(fullU8.c_str(), &info))
			return SDL_ENUM_CONTINUE;

		VDStringW wname = VDTextU8ToW(VDStringA(fname));
		VDStringW fullPath = VDTextU8ToW(fullU8);

		if (info.type == SDL_PATHTYPE_DIRECTORY) {
			if (ctx->recursive)
				ctx->self->ScanFolder(fullPath, true, *ctx->entries, *ctx->images);
		} else if (info.type == SDL_PATHTYPE_FILE) {
			if (IsSupportedImageExtension(wname.c_str())) {
				ctx->images->push_back(fullPath);
			}

			if (IsArchiveExtension(wname.c_str())) {
				ctx->self->ScanArchive(fullPath, *ctx->entries, *ctx->images);
			} else if (IsSupportedGameExtension(wname.c_str())) {
				const wchar_t *ext = wcsrchr(wname.c_str(), L'.');
				if (!ext) return SDL_ENUM_CONTINUE;
				ext++;

				GameMediaType mediaType = ClassifyExtension(ext);
				if (mediaType == GameMediaType::Unknown)
					return SDL_ENUM_CONTINUE;

				VDStringW baseName(wname.c_str());
				const wchar_t *dot = wcsrchr(baseName.c_str(), L'.');
				if (dot) baseName.resize(dot - baseName.c_str());

				VDStringW canonical = ExtractCanonicalName(baseName);
				VDStringW parentDir;
				const wchar_t *lastSlash = wcsrchr(fullPath.c_str(), L'/');
				if (lastSlash)
					parentDir.assign(fullPath.c_str(), lastSlash - fullPath.c_str());

				GameVariant var;
				var.mPath = fullPath;
				var.mType = mediaType;
				var.mFileSize = (uint64_t)info.size;
				var.mModTime = (uint64_t)info.modify_time;
				var.mLabel = BuildVariantLabel(baseName, canonical, ext);

				GameEntry entry;
				entry.mCanonicalName = std::move(canonical);
				entry.mParentDir = std::move(parentDir);
				entry.mDisplayName = CleanDisplayName(entry.mCanonicalName);
				entry.mVariants.push_back(std::move(var));

				ctx->entries->push_back(std::move(entry));
				ctx->self->mScanProgress.fetch_add(1, std::memory_order_relaxed);
			}
		}
		return SDL_ENUM_CONTINUE;
	};

	SDL_EnumerateDirectory(dirU8.c_str(), callback, &ctx);
}

// =========================================================================
// Archive scanning
// =========================================================================

void ATGameLibrary::ScanArchive(const VDStringW &path,
	std::vector<GameEntry> &outEntries, std::vector<VDStringW> &outImages)
{
	try {
		VDFileStream fs(path.c_str());
		VDZipArchive zip;
		zip.Init(&fs);

		sint32 n = zip.GetFileCount();
		for (sint32 i = 0; i < n; ++i) {
			if (mScanCancel.load(std::memory_order_acquire))
				return;

			const VDZipArchive::FileInfo &info = zip.GetFileInfo(i);
			const VDStringW &rawName = info.mDecodedFileName;

			// Skip directories
			if (!rawName.empty() && rawName.back() == L'/')
				continue;

			// Extract just the filename
			const wchar_t *lastSlash = wcsrchr(rawName.c_str(), L'/');
			VDStringW fileName = lastSlash ? VDStringW(lastSlash + 1) : rawName;

			if (fileName.empty())
				continue;

			// Track images inside archives
			if (IsSupportedImageExtension(fileName.c_str())) {
				VDStringW vfsPath = ATMakeVFSPathForZipFile(path.c_str(), rawName.c_str());
				outImages.push_back(std::move(vfsPath));
				continue;
			}

			if (!IsSupportedGameExtension(fileName.c_str()))
				continue;
			if (IsArchiveExtension(fileName.c_str()))
				continue;

			const wchar_t *ext = wcsrchr(fileName.c_str(), L'.');
			if (!ext) continue;
			ext++;

			GameMediaType mediaType = ClassifyExtension(ext);
			if (mediaType == GameMediaType::Unknown)
				continue;

			VDStringW baseName(fileName.c_str());
			const wchar_t *dot = wcsrchr(baseName.c_str(), L'.');
			if (dot) baseName.resize(dot - baseName.c_str());

			VDStringW canonical = ExtractCanonicalName(baseName);

			// Parent dir within archive
			VDStringW parentDir;
			if (lastSlash) {
				parentDir = path;
				parentDir += L"!/";
				parentDir.append(rawName.c_str(), lastSlash - rawName.c_str());
			} else {
				parentDir = path;
			}

			GameVariant var;
			var.mPath = ATMakeVFSPathForZipFile(path.c_str(), rawName.c_str());
			var.mArchivePath = path;
			var.mType = mediaType;
			var.mFileSize = (uint64_t)info.mUncompressedSize;
			var.mModTime = 0;
			var.mLabel = BuildVariantLabel(baseName, canonical, ext);

			GameEntry entry;
			entry.mCanonicalName = std::move(canonical);
			entry.mParentDir = std::move(parentDir);
			entry.mDisplayName = CleanDisplayName(entry.mCanonicalName);
			entry.mVariants.push_back(std::move(var));

			outEntries.push_back(std::move(entry));
			mScanProgress.fetch_add(1, std::memory_order_relaxed);
		}
	} catch (...) {
		// Skip unreadable archives
	}
}

// =========================================================================
// Variant grouping
// =========================================================================

void ATGameLibrary::GroupVariants(std::vector<GameEntry> &entries) {
	// Group by (parentDir, canonicalName) — case-insensitive canonical name
	struct GroupKey {
		VDStringW parentDir;
		VDStringW canonicalLower;

		bool operator==(const GroupKey &o) const {
			return parentDir == o.parentDir && canonicalLower == o.canonicalLower;
		}
	};

	struct GroupKeyHash {
		size_t operator()(const GroupKey &k) const {
			size_t h = std::hash<std::wstring>()(
				std::wstring(k.parentDir.c_str(), k.parentDir.size()));
			h ^= std::hash<std::wstring>()(
				std::wstring(k.canonicalLower.c_str(), k.canonicalLower.size())) * 31;
			return h;
		}
	};

	std::unordered_map<GroupKey, size_t, GroupKeyHash> groupMap;
	std::vector<GameEntry> grouped;

	for (auto &entry : entries) {
		VDStringW canonLower;
		for (size_t i = 0; i < entry.mCanonicalName.size(); ++i)
			canonLower += (wchar_t)std::towlower(entry.mCanonicalName[i]);

		GroupKey key;
		key.parentDir = entry.mParentDir;
		key.canonicalLower = canonLower;

		auto it = groupMap.find(key);
		if (it != groupMap.end()) {
			auto &existing = grouped[it->second];
			for (auto &var : entry.mVariants)
				existing.mVariants.push_back(std::move(var));
		} else {
			groupMap[key] = grouped.size();
			grouped.push_back(std::move(entry));
		}
	}

	// Dedupe variants by path within each entry — an auto-added file
	// source may sit under a folder source that also picks up the file,
	// so the same physical game ends up in the group twice.
	for (auto &entry : grouped) {
		if (entry.mVariants.size() <= 1)
			continue;
		std::sort(entry.mVariants.begin(), entry.mVariants.end(),
			[](const GameVariant &a, const GameVariant &b) {
				return a.mPath < b.mPath;
			});
		entry.mVariants.erase(
			std::unique(entry.mVariants.begin(), entry.mVariants.end(),
				[](const GameVariant &a, const GameVariant &b) {
					return a.mPath == b.mPath;
				}),
			entry.mVariants.end());
	}

	// For single-variant entries, simplify the label to just the extension
	for (auto &entry : grouped) {
		if (entry.mVariants.size() == 1) {
			const wchar_t *ext = wcsrchr(entry.mVariants[0].mPath.c_str(), L'.');
			if (ext) {
				ext++;
				VDStringW label;
				for (const wchar_t *p = ext; *p; ++p)
					label += (wchar_t)std::towupper(*p);
				entry.mVariants[0].mLabel = std::move(label);
			}
		}

		// Sort variants: prefer higher version, then Disk > Exec > Cart > Cass
		std::sort(entry.mVariants.begin(), entry.mVariants.end(),
			[](const GameVariant &a, const GameVariant &b) {
				if (a.mType != b.mType)
					return (int)a.mType < (int)b.mType;
				return a.mLabel > b.mLabel;
			});
	}

	entries = std::move(grouped);
}

// =========================================================================
// Display name disambiguation
// =========================================================================

void ATGameLibrary::DisambiguateNames(std::vector<GameEntry> &entries) {
	// Find entries with duplicate display names
	std::unordered_map<std::wstring, std::vector<size_t>> nameMap;
	for (size_t i = 0; i < entries.size(); ++i) {
		std::wstring key(entries[i].mDisplayName.c_str(),
			entries[i].mDisplayName.size());
		nameMap[key].push_back(i);
	}

	for (auto &pair : nameMap) {
		if (pair.second.size() <= 1)
			continue;

		for (size_t idx : pair.second) {
			auto &entry = entries[idx];
			// Extract parent folder name from mParentDir
			const wchar_t *dir = entry.mParentDir.c_str();
			const wchar_t *lastSlash = wcsrchr(dir, L'/');
			VDStringW folderName;
			if (lastSlash && lastSlash > dir)
				folderName.assign(lastSlash + 1);
			else
				folderName = entry.mParentDir;

			if (!folderName.empty()) {
				VDStringW newName = CleanDisplayName(folderName);
				newName += L" \u2014 ";
				newName += entry.mDisplayName;
				entry.mDisplayName = std::move(newName);
			}
		}
	}
}

// =========================================================================
// Game-art matching
// =========================================================================

static std::wstring LowerWString(const VDStringW &s) {
	std::wstring out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size(); ++i)
		out += (wchar_t)std::towlower(s[i]);
	return out;
}

struct ImageInfo {
	VDStringW mPath;
	VDStringW mParentDir;
};

void ATGameLibrary::MatchArt(std::vector<GameEntry> &entries,
	const std::vector<VDStringW> &imagePaths)
{
	// Build image index: lowercase basename (no ext) → list of images with dirs
	std::unordered_map<std::wstring, std::vector<ImageInfo>> imageMap;
	for (const auto &imgPath : imagePaths) {
		const wchar_t *lastSlash = wcsrchr(imgPath.c_str(), L'/');
		VDStringW fname = lastSlash ? VDStringW(lastSlash + 1) : imgPath;

		const wchar_t *dot = wcsrchr(fname.c_str(), L'.');
		VDStringW baseName;
		if (dot)
			baseName.assign(fname.c_str(), dot - fname.c_str());
		else
			baseName = fname;

		std::wstring key = LowerWString(baseName);

		ImageInfo info;
		info.mPath = imgPath;
		if (lastSlash)
			info.mParentDir.assign(imgPath.c_str(), lastSlash - imgPath.c_str());

		imageMap[key].push_back(std::move(info));
	}

	bool crossFolder = mSettings.mbCrossFolderArt;

	for (auto &entry : entries) {
		if (!entry.mArtPath.empty())
			continue;

		// Try canonical name, then display name
		std::wstring keys[2] = {
			LowerWString(entry.mCanonicalName),
			LowerWString(entry.mDisplayName),
		};

		for (const auto &key : keys) {
			auto it = imageMap.find(key);
			if (it == imageMap.end())
				continue;

			// Prefer same-directory match
			const VDStringW *bestPath = nullptr;
			for (const auto &img : it->second) {
				if (img.mParentDir == entry.mParentDir) {
					bestPath = &img.mPath;
					break;
				}
			}

			// Also check art subdirectories of the game's parent dir
			if (!bestPath) {
				for (const auto &img : it->second) {
					// Check if image is in artwork/, screenshots/, etc.
					// under the game's parent directory
					if (img.mParentDir.size() > entry.mParentDir.size()
						&& wcsncmp(img.mParentDir.c_str(),
							entry.mParentDir.c_str(),
							entry.mParentDir.size()) == 0
						&& img.mParentDir[entry.mParentDir.size()] == L'/')
					{
						bestPath = &img.mPath;
						break;
					}
				}
			}

			// Cross-folder fallback (only if enabled)
			if (!bestPath && crossFolder && !it->second.empty())
				bestPath = &it->second[0].mPath;

			if (bestPath) {
				entry.mArtPath = *bestPath;
				break;
			}
		}
	}
}

// =========================================================================
// Play history merge
// =========================================================================

void ATGameLibrary::MergePlayHistory(std::vector<GameEntry> &newEntries,
	const std::vector<GameEntry> &oldEntries)
{
	// Build a map: variant path → old entry index
	std::unordered_map<std::wstring, size_t> pathToOld;
	for (size_t i = 0; i < oldEntries.size(); ++i) {
		for (const auto &var : oldEntries[i].mVariants) {
			std::wstring key(var.mPath.c_str(), var.mPath.size());
			pathToOld[key] = i;
		}
	}

	for (auto &entry : newEntries) {
		for (const auto &var : entry.mVariants) {
			std::wstring key(var.mPath.c_str(), var.mPath.size());
			auto it = pathToOld.find(key);
			if (it != pathToOld.end()) {
				const auto &old = oldEntries[it->second];
				entry.mLastPlayed = old.mLastPlayed;
				entry.mPlayCount = old.mPlayCount;
				// Preserve user-set art (stored under custom_art/)
				// across rescans — MatchArt never rediscovers it
				// because the custom_art dir isn't a scanned source.
				// Scanner-matched art (next-to-file images) is
				// allowed to refresh normally.
				if (!old.mArtPath.empty()
					&& wcsstr(old.mArtPath.c_str(),
						L"/custom_art/") != nullptr)
				{
					entry.mArtPath = old.mArtPath;
				}
				break;
			}
		}
	}
}
