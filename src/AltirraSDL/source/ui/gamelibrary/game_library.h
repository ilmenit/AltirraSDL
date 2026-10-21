//	AltirraSDL - Game Library
//	Data model, JSON cache, background scanner, and game-art matching.

#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>
#include <vd2/system/VDString.h>

#include "../../media/library_extensions.h"

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

// Outcome of the last online metadata lookup for an entry.  Stored in
// the cache so a re-run can skip what already succeeded and retry only
// what failed for a transient reason.
enum class GameMetaStatus : uint8_t {
	None,        // never attempted
	Matched,     // provider returned a game
	NotFound,    // provider answered, but has no such game
	Error,       // network / quota / abort — worth retrying
	// Legacy: written by a hand-edit feature that no longer exists.
	// Kept so caches from those builds still load; treated as Matched
	// everywhere ("this entry has metadata").
	UserEdited,
};

// Which downloaded image an entry shows as its tile/row thumbnail.
enum class GameArtSlot : uint8_t {
	BoxArt,
	TitleShot,
	Screenshot,
	Logo,
};

// Metadata fetched from an online provider (currently ScreenScraper).
// Media paths are stored RELATIVE to the config dir so a moved or
// synced profile directory keeps working.
struct GameMetadata {
	GameMetaStatus mStatus = GameMetaStatus::None;

	VDStringW mTitle;        // canonical title, region-preferred
	VDStringW mDescription;  // synopsis, language-preferred
	VDStringW mPublisher;
	VDStringW mDeveloper;
	VDStringW mGenre;        // comma-joined
	uint16_t  mYear       = 0;
	uint8_t   mPlayersMax = 0;
	uint8_t   mRating     = 0;   // 0..20, as the provider reports it
	VDStringW mRegion;           // region the title/date were taken from

	VDStringW mBoxArtPath;
	VDStringW mTitleShotPath;
	VDStringW mScreenshotPath;
	VDStringW mLogoPath;

	VDStringA mProvider;             // "screenscraper"
	uint32_t  mProviderGameId = 0;   // pinned after a manual match
	uint64_t  mFetchedTime    = 0;
	uint32_t  mMatchedCRC32   = 0;   // which variant produced the match

	bool HasAnyMedia() const {
		return !mBoxArtPath.empty() || !mTitleShotPath.empty()
			|| !mScreenshotPath.empty() || !mLogoPath.empty();
	}

	bool HasAnyText() const {
		return !mTitle.empty() || !mDescription.empty()
			|| !mPublisher.empty() || !mDeveloper.empty()
			|| !mGenre.empty() || mYear != 0;
	}
};

struct GameEntry {
	VDStringW                mDisplayName;
	VDStringW                mCanonicalName;
	VDStringW                mParentDir;
	std::vector<GameVariant> mVariants;
	VDStringW                mArtPath;
	uint64_t                 mLastPlayed = 0;
	uint32_t                 mPlayCount = 0;
	// Fork extension: shared profile reference; UINT32_MAX uses current setup.
	uint32_t                 mLaunchProfileId = UINT32_MAX;
	GameMetadata             mMeta;

	// True when this entry appeared in the library during this session
	// and has not been considered for an automatic metadata fetch yet.
	// Transient by design: never written to the cache, so a restart
	// cannot make the whole library look new again.
	bool                     mbNewlyAdded = false;
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
	// Show the details panel next to the game list.  One setting shared
	// by both frontends: Desktop docks it to the right of the table,
	// Gaming Mode docks it right (landscape) or below (portrait), and
	// hides it entirely when the screen is too small to carry it.  It
	// answers the same user question in both — "do I want to see the
	// artwork and description while I browse?" — so keeping it single
	// stops the two modes disagreeing about what the user asked for.
	bool mbShowDetailsPanel    = true;
};

struct CachedSourceInfo {
	VDStringW mPath;
	uint64_t  mLastScanMtime = 0;
};

// Which downloaded image an entry should show, given the user's global
// art preference (ATMetadataSettings::mArtSlot).  Returns false when the
// entry has no downloaded media at all.
//
// The preference is global rather than per-game because that is what
// makes it a *view*: switching it re-renders the whole library, so one
// left/right press answers "show me screenshots" for every game at once
// instead of for one.  Games that lack the chosen kind fall back through
// a fixed order — screenshot, box art, title screen, logo — chosen by
// how much each tells you about the game, so a switch never leaves a
// tile blank just because that one title has no cover scan.
bool ATGameResolveArtSlot(const GameEntry &entry, GameArtSlot preferred,
	GameArtSlot &outSlot);

// The relative media path for one slot, or empty when that slot is
// unset.  Handy where the caller already knows which slot it wants.
const VDStringW &ATGameArtSlotPath(const GameEntry &entry, GameArtSlot slot);

// The user's current global art preference, as a GameArtSlot.  A thin
// read of ATMetadataSettings::mArtSlot, wrapped so the library layer
// does not have to spell out the media-settings header everywhere and
// so the int-to-enum clamp lives in exactly one place.
GameArtSlot ATGameGetPreferredArtSlot();

GameMediaType ClassifyExtension(const wchar_t *ext);
bool IsSupportedGameExtension(const wchar_t *name);
bool IsSupportedImageExtension(const wchar_t *name);
bool IsArchiveExtension(const wchar_t *name);
VDStringW ExtractCanonicalName(const VDStringW &baseNameNoExt);
VDStringW CleanDisplayName(const VDStringW &name);
VDStringW BuildVariantLabel(const VDStringW &baseNameNoExt,
	const VDStringW &canonicalName, const wchar_t *ext);

// Compute the CRC32 of a game file's raw bytes.  Opens through the VFS
// so `zip://outer!inner` virtual paths from the archive scan resolve to
// the inner member's bytes — the same path FindVariantBytesForCRC32
// uses, and the value ScreenScraper matches against.
//
// Pure and thread-safe: touches no library state, so the metadata
// scraper's worker threads can call it directly.  Returns false when
// the file cannot be read.
bool ATGameComputeFileCRC32(const VDStringW &path, uint32_t &outCRC32,
	uint64_t &outSize);

class ATGameLibrary {
public:
	ATGameLibrary();
	~ATGameLibrary();

	void SetConfigDir(const VDStringA &configDir);

	bool LoadCache();
	bool SaveCache() const;

	void RecordPlay(size_t entryIndex);
	void ClearHistory();

	// Drop one entry from the library.
	//
	// A removal, not a blocklist: the entry goes and nothing is remembered
	// about it.  If the entry was its own file source — the usual shape
	// for a game added by booting it — that source is dropped as well, so
	// it does not walk straight back in on the next scan.  A game that
	// lives inside a folder or archive source is still in that source and
	// a later scan will find it again; removing the source is how you
	// stop that for good.
	//
	// The files on disk are never touched.
	//
	// Returns false when the index is out of range.
	bool RemoveEntry(size_t entryIndex);

	// True when a later scan would find this entry's files again — that
	// is, when they live inside a folder or archive source rather than
	// being their own file source.  The removal confirmations use this so
	// they promise only what they can keep: for these games "removed" is
	// until the next scan, and the user deserves to know that before
	// pressing the button rather than after.
	bool WillRescanRestore(size_t entryIndex) const;

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

	const VDStringA& GetConfigDir() const { return mConfigDir; }

	// Media downloaded by the metadata scraper is stored under
	// {configDir}/media/ and recorded RELATIVE to the config dir, so a
	// moved or synced profile keeps resolving.  These turn a stored
	// value back into something openable.  An empty input yields an
	// empty result; an absolute input is passed through unchanged so
	// older caches and user-supplied paths keep working.
	VDStringW ResolveMediaPath(const VDStringW &relative) const;

	// Which image this entry should display as its tile/row thumbnail,
	// as an absolute path (empty when the entry has none).
	//
	// Precedence, highest first:
	//   1. user-set art under custom_art/ — an explicit choice always
	//      wins, so a download never silently replaces it;
	//   2. the downloaded media slot the entry prefers, then any other
	//      downloaded slot;
	//   3. scanner-matched art found next to the ROM.
	VDStringW GetTileArtPath(const GameEntry &entry) const;

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
