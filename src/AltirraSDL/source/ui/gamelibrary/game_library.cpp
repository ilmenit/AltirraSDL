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

static bool IsArchiveExtension(const wchar_t *name) {
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

bool ATGameLibrary::LoadCache() {
	if (mCachePath.empty())
		return false;

	VDStringW cachePath = VDTextU8ToW(mCachePath);

	try {
		VDFileStream fs(cachePath.c_str());
		sint64 size = fs.Length();
		if (size <= 0 || size > 64 * 1024 * 1024)
			return false;

		std::vector<uint8_t> buf((size_t)size);
		fs.Read(buf.data(), (sint32)size);

		VDJSONDocument doc;
		VDJSONReader reader;
		if (!reader.Parse(buf.data(), buf.size(), doc))
			return false;

		auto root = doc.Root();
		if (!root.IsObject())
			return false;

		auto version = root[L"version"];
		if (!version.IsInt() || version.AsInt64() != 1)
			return false;

		auto lastScanTime = root[L"lastScanTime"];
		if (lastScanTime.IsInt())
			mLastScanTime = (uint64_t)lastScanTime.AsInt64();

		// Load cached source info
		mCachedSources.clear();
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
				mCachedSources.push_back(std::move(csi));
			}
		}

		// Load game entries
		mEntries.clear();
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

						entry.mVariants.push_back(std::move(var));
					}
				}

				// Rebuild canonical name and parent dir from first variant
				if (!entry.mVariants.empty()) {
					const VDStringW &path = entry.mVariants[0].mPath;
					const wchar_t *lastSlash = wcsrchr(path.c_str(), L'/');
					if (lastSlash)
						entry.mParentDir.assign(path.c_str(), lastSlash - path.c_str());

					const wchar_t *fname = lastSlash ? lastSlash + 1 : path.c_str();
					VDStringW baseName(fname);
					const wchar_t *dot = wcsrchr(baseName.c_str(), L'.');
					if (dot) baseName.resize(dot - baseName.c_str());
					entry.mCanonicalName = ExtractCanonicalName(baseName);
				}

				mEntries.push_back(std::move(entry));
			}
		}

		return true;
	} catch (...) {
		mEntries.clear();
		mCachedSources.clear();
		return false;
	}
}

// =========================================================================
// JSON cache save
// =========================================================================

bool ATGameLibrary::SaveCache() const {
	if (mCachePath.empty())
		return false;

	VDStringW cachePath = VDTextU8ToW(mCachePath);

	try {
		VDFileStream fs(cachePath.c_str(),
			nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
		VDJSONStreamOutput streamOutput(fs);
		VDJSONWriter writer;
		writer.Begin(&streamOutput, false);

		writer.OpenObject();

		writer.WriteMemberName(L"version");
		writer.WriteInt(1);

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

void ATGameLibrary::SetSettings(const GameLibrarySettings &settings) {
	mSettings = settings;
}

void ATGameLibrary::LoadSettingsFromRegistry() {
	VDRegistryAppKey key(kGameLibKey, false);
	if (!key.isReady())
		return;

	mSettings.mbRecursive = key.getBool("Recursive", true);
	mSettings.mbCrossFolderArt = key.getBool("CrossFolderArt", false);
	mSettings.mbShowOnStartup = key.getBool("ShowOnStartup", true);
	mSettings.mViewMode = key.getInt("ViewMode", 1);

	int sourceCount = key.getInt("SourceCount", 0);
	mSources.clear();
	for (int i = 0; i < sourceCount; ++i) {
		VDStringA pathKey;
		pathKey.sprintf("Source%d.Path", i);
		VDStringA archKey;
		archKey.sprintf("Source%d.IsArchive", i);

		VDStringW path;
		if (key.getString(pathKey.c_str(), path)) {
			GameSource src;
			src.mPath = std::move(path);
			src.mbIsArchive = key.getBool(archKey.c_str(), false);
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
	key.setInt("ViewMode", mSettings.mViewMode);

	key.setInt("SourceCount", (int)mSources.size());
	for (int i = 0; i < (int)mSources.size(); ++i) {
		VDStringA pathKey;
		pathKey.sprintf("Source%d.Path", i);
		VDStringA archKey;
		archKey.sprintf("Source%d.IsArchive", i);

		key.setString(pathKey.c_str(), mSources[i].mPath.c_str());
		key.setBool(archKey.c_str(), mSources[i].mbIsArchive);
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
	mScanning.store(true, std::memory_order_release);

	mScanThread = std::thread([this]() { ScanThread(); });
}

void ATGameLibrary::CancelScan() {
	if (mScanThread.joinable()) {
		mScanCancel.store(true, std::memory_order_release);
		mScanThread.join();
	}
	mScanning.store(false, std::memory_order_release);
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

		if (src.mbIsArchive) {
			ScanArchive(src.mPath, allEntries, allImages);
		} else {
			ScanFolder(src.mPath, mSettings.mbRecursive, allEntries, allImages);
		}
	}

	if (mScanCancel.load(std::memory_order_acquire))
		return;

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
				break;
			}
		}
	}
}
