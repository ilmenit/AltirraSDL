//	AltirraSDL - Game Library Art Cache
//	LRU texture cache for game-art images in the Game Browser grid view.
//	Loads images on a background thread, uploads to GPU on main thread.

#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include <string>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <vd2/system/VDString.h>

class GameArtCache {
public:
	GameArtCache();
	~GameArtCache();

	void Shutdown();

	void SetCacheDir(const VDStringA &configDir);

	// Request texture for an art path. Returns non-null ImTextureID if
	// the image is loaded and ready. Queues a background load on first
	// request. outW/outH receive the texture dimensions when available.
	ImTextureID GetTexture(const VDStringW &artPath, int *outW, int *outH);

	// Must be called once per frame from the main thread to upload
	// completed image loads to GPU textures and evict stale entries.
	void ProcessPending();

	// Drop all cached textures (e.g. on display backend change).
	void Clear();

private:
	enum class EntryState {
		Empty,
		Loading,
		Ready,
		Failed,
	};

	struct CacheEntry {
		EntryState mState = EntryState::Empty;
		ImTextureID mTexID = 0;
		uint32_t mGLTexture = 0;
		SDL_Texture *mSDLTexture = nullptr;
		int mWidth = 0;
		int mHeight = 0;
		uint32_t mLastAccessFrame = 0;
		std::wstring mKey;
	};

	struct LoadResult {
		std::wstring mKey;
		SDL_Surface *mSurface = nullptr;
	};

	static constexpr size_t kMaxCacheSize = 512;
	static constexpr int kMaxUploadsPerFrame = 4;

	std::unordered_map<std::wstring, size_t> mKeyToIndex;
	std::vector<CacheEntry> mEntries;
	uint32_t mFrameCounter = 0;

	std::thread mLoaderThread;
	std::mutex mQueueMutex;
	std::condition_variable mLoaderCV;
	std::vector<std::wstring> mLoadQueue;
	std::vector<LoadResult> mResultQueue;
	std::atomic<bool> mShutdownFlag{false};

	VDStringA mThumbDir;

	void LoaderThread();
	void EvictOldest();
	void DestroyEntry(CacheEntry &entry);
	void UploadSurface(CacheEntry &entry, SDL_Surface *surf);

	VDStringA GetThumbPath(const VDStringW &artPath) const;
	SDL_Surface *LoadCachedThumb(const VDStringA &thumbPath);
	void SaveThumb(const VDStringA &thumbPath, SDL_Surface *surf);
};
