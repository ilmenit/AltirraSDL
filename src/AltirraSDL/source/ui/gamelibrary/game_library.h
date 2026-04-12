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
};

struct GameLibrarySettings {
	bool mbRecursive      = true;
	bool mbCrossFolderArt = false;
	bool mbShowOnStartup  = true;
	int  mViewMode        = 1;
};

struct CachedSourceInfo {
	VDStringW mPath;
	uint64_t  mLastScanMtime = 0;
};

GameMediaType ClassifyExtension(const wchar_t *ext);
bool IsSupportedGameExtension(const wchar_t *name);
bool IsSupportedImageExtension(const wchar_t *name);
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

	const std::vector<GameEntry>& GetEntries() const { return mEntries; }
	std::vector<GameEntry>& GetEntries() { return mEntries; }
	size_t GetEntryCount() const { return mEntries.size(); }

	const std::vector<GameSource>& GetSources() const { return mSources; }
	void SetSources(std::vector<GameSource> sources);

	const GameLibrarySettings& GetSettings() const { return mSettings; }
	void SetSettings(const GameLibrarySettings &settings);

	void LoadSettingsFromRegistry();
	void SaveSettingsToRegistry() const;

	void StartScan();
	void CancelScan();
	bool IsScanComplete() const { return mScanComplete.load(std::memory_order_acquire); }
	bool IsScanning() const { return mScanning.load(std::memory_order_acquire); }
	int  GetScanProgress() const { return mScanProgress.load(std::memory_order_acquire); }
	void ConsumeScanResults();

	uint64_t GetLastScanTime() const { return mLastScanTime; }

private:
	void ScanThread();
	void ScanFolder(const VDStringW &path, bool recursive,
		std::vector<GameEntry> &outEntries, std::vector<VDStringW> &outImages);
	void ScanArchive(const VDStringW &path,
		std::vector<GameEntry> &outEntries, std::vector<VDStringW> &outImages);
	void GroupVariants(std::vector<GameEntry> &entries);
	void DisambiguateNames(std::vector<GameEntry> &entries);
	void MatchArt(std::vector<GameEntry> &entries,
		const std::vector<VDStringW> &imagePaths);
	void MergePlayHistory(std::vector<GameEntry> &newEntries,
		const std::vector<GameEntry> &oldEntries);

	VDStringA mConfigDir;
	VDStringA mCachePath;

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
	std::vector<GameEntry>  mScanResults;
};
