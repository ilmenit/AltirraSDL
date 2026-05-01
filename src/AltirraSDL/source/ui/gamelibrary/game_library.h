//	AltirraSDL - Game Library
//	Data model, JSON cache, background scanner, and game-art matching.

#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>
#include <vd2/system/VDString.h>

enum class GameMediaType {
	Disk,
	Executable,
	Cartridge,
	Cassette,
	Unknown,
};

struct GameVariant {
	VDStringW     mPath;
	VDStringW     mArchivePath;
	GameMediaType mType = GameMediaType::Unknown;
	uint64_t      mFileSize = 0;
	uint64_t      mModTime = 0;
	VDStringW     mLabel;
	// Persistent CRC32 of the raw game-file bytes.  0 = not yet
	// computed (lazy: filled in by the netplay-joiner cache lookup
	// path on first use, then saved to the cache JSON).  Adding it
	// to the scan would slow first-time scans on large libraries
	// for no everyday user benefit; populating on demand keeps
	// scans fast while still letting netplay match library files
	// against a host's advertised gameFileCRC32 to skip downloads.
	mutable uint32_t mGameFileCRC32 = 0;
};

struct GameEntry {
	VDStringW                mDisplayName;
	VDStringW                mCanonicalName;
	VDStringW                mParentDir;
	std::vector<GameVariant> mVariants;
	VDStringW                mArtPath;
	uint64_t                 mLastPlayed = 0;
	uint32_t                 mPlayCount = 0;
};

struct GameSource {
	VDStringW mPath;
	bool      mbIsArchive = false;
	bool      mbIsFile    = false;   // standalone file entry (auto-added booted games)
};

struct GameLibrarySettings {
	bool mbRecursive           = true;
	bool mbCrossFolderArt      = true;
	bool mbShowOnStartup       = true;
	bool mbAddBootedToLibrary  = true;   // Auto-add booted games (not already in library) to the library + Recently Played
	int  mViewMode             = 1;      // 0=list, 1=grid
	int  mGridSize             = 1;      // 0=small, 1=medium, 2=large
	int  mListSize             = 0;      // 0=small, 1=medium, 2=large
};

struct CachedSourceInfo {
	VDStringW mPath;
	uint64_t  mLastScanMtime = 0;
};

GameMediaType ClassifyExtension(const wchar_t *ext);
bool IsSupportedGameExtension(const wchar_t *name);
bool IsSupportedImageExtension(const wchar_t *name);
bool IsArchiveExtension(const wchar_t *name);
VDStringW ExtractCanonicalName(const VDStringW &baseNameNoExt);
VDStringW CleanDisplayName(const VDStringW &name);
VDStringW BuildVariantLabel(const VDStringW &baseNameNoExt,
	const VDStringW &canonicalName, const wchar_t *ext);

class ATGameLibrary {
public:
	ATGameLibrary();
	~ATGameLibrary();

	void SetConfigDir(const VDStringA &configDir);

	bool LoadCache();
	bool SaveCache() const;

	void RecordPlay(size_t entryIndex);
	void ClearHistory();

	// Called by the UI when the user boots a file outside the library.
	// If the file already matches a library variant, the play history for
	// that entry is bumped and the existing index is returned.  Otherwise,
	// if addToLibrary is true (from GameLibrarySettings.mbAddBootedToLibrary),
	// a single-variant entry is created, persisted, and added as a source
	// so it survives across scans.  Returns the entry index or -1 if the
	// file extension is not recognized / could not be added.
	int  AddBootedGame(const VDStringW &path, bool addToLibrary);

	// Lookup helper: returns the index of the entry with a variant whose
	// mPath matches the given path, or -1 if none.
	int  FindEntryByVariantPath(const VDStringW &path) const;

	// Netplay joiner cache helper.  Find a variant whose raw game-file
	// bytes have CRC32 == `crc32`, with `expectedSize` bytes and an
	// extension matching `expectedExt8` (NUL-padded 8-byte field as
	// carried in NetBootConfig.gameExtension; leading dot optional).
	// Returns true on a match; on success `outBytes` is populated with
	// the file's content.
	//
	// Strategy: filter variants by (size, extension) — typically 0 or 1
	// candidates pass.  For each candidate either trust the cached
	// `mGameFileCRC32` (when non-zero) or compute it from the file
	// once and store it on the variant.  Persists the cache JSON when
	// any variant got a new CRC so subsequent calls are instant.
	//
	// Best-effort: on read failure the variant is skipped and search
	// continues.  Returns false if no match found.
	bool FindVariantBytesForCRC32(uint32_t crc32,
	                              uint64_t expectedSize,
	                              const char expectedExt8[8],
	                              std::vector<uint8_t>& outBytes);

	const std::vector<GameEntry>& GetEntries() const { return mEntries; }
	std::vector<GameEntry>& GetEntries() { return mEntries; }
	size_t GetEntryCount() const { return mEntries.size(); }

	const std::vector<GameSource>& GetSources() const { return mSources; }
	void SetSources(std::vector<GameSource> sources);
	void PurgeRemovedSourceEntries();

	const GameLibrarySettings& GetSettings() const { return mSettings; }
	void SetSettings(const GameLibrarySettings &settings);

	void LoadSettingsFromRegistry();
	void SaveSettingsToRegistry() const;

	void StartScan();
	void CancelScan();
	bool IsScanComplete() const { return mScanComplete.load(std::memory_order_acquire); }
	bool IsScanning() const { return mScanning.load(std::memory_order_acquire); }
	int  GetScanProgress() const { return mScanProgress.load(std::memory_order_acquire); }
	VDStringA GetScanStatus() const;
	void ConsumeScanResults();

	uint64_t GetLastScanTime() const { return mLastScanTime; }

private:
	bool WriteCacheFile(const VDStringW &path) const;

	void ScanThread();
	void ScanFolder(const VDStringW &path, bool recursive,
		std::vector<GameEntry> &outEntries, std::vector<VDStringW> &outImages);
	void ScanArchive(const VDStringW &path,
		std::vector<GameEntry> &outEntries, std::vector<VDStringW> &outImages);
	bool ScanFile(const VDStringW &path,
		std::vector<GameEntry> &outEntries);
	void GroupVariants(std::vector<GameEntry> &entries);
	void DisambiguateNames(std::vector<GameEntry> &entries);
	void MatchArt(std::vector<GameEntry> &entries,
		const std::vector<VDStringW> &imagePaths);
	void MergePlayHistory(std::vector<GameEntry> &newEntries,
		const std::vector<GameEntry> &oldEntries);

	VDStringA mConfigDir;
	VDStringA mCachePath;

	// True when the last successful LoadCache/SaveCache operated on
	// the primary cache file (not the .bak fallback).  SaveCache uses
	// this to decide whether to rotate the current main file into the
	// .bak slot — we must *not* overwrite a good .bak with a known-
	// corrupt main or we lose the only recoverable state.  Mutable so
	// SaveCache can stay const.
	mutable bool            mMainFileValid = false;

	std::vector<GameEntry>  mEntries;
	std::vector<GameSource> mSources;
	std::vector<CachedSourceInfo> mCachedSources;
	GameLibrarySettings     mSettings;
	uint64_t                mLastScanTime = 0;

	std::thread             mScanThread;
	std::mutex              mScanMutex;
	std::atomic<bool>       mScanComplete{false};
	std::atomic<bool>       mScanning{false};
	std::atomic<bool>       mScanCancel{false};
	std::atomic<int>        mScanProgress{0};
	VDStringA               mScanStatus;
	std::vector<GameEntry>  mScanResults;
};
