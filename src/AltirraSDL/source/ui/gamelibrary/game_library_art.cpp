//	AltirraSDL - Game Library Art Cache
//	LRU texture cache for game-art images in the Game Browser grid view.

#include <stdafx.h>
#include <algorithm>
#include <cstring>
#include <sys/stat.h>

#include <SDL3/SDL.h>
#include <imgui.h>

#ifndef ALTIRRA_NO_SDL3_IMAGE
#include <SDL3_image/SDL_image.h>
#endif

#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/file.h>
#include <vd2/system/refcount.h>
#include <at/atcore/vfs.h>
#include <at/atcore/md5.h>

#include "display_backend.h"
#include "gl_helpers.h"

#include "game_library_art.h"

extern SDL_Window *g_pWindow;
extern IDisplayBackend *ATUIGetDisplayBackend();

// =========================================================================
// Image loading (background thread)
// =========================================================================

static SDL_Surface *LoadImageFromFilesystem(const VDStringW &path) {
#ifndef ALTIRRA_NO_SDL3_IMAGE
	VDStringA u8 = VDTextWToU8(path);
	SDL_IOStream *io = SDL_IOFromFile(u8.c_str(), "rb");
	if (!io)
		return nullptr;

	SDL_Surface *surf = IMG_Load_IO(io, true);
	if (!surf)
		return nullptr;

	SDL_Surface *conv = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_BGRA32);
	SDL_DestroySurface(surf);
	return conv;
#else
	return nullptr;
#endif
}

static SDL_Surface *LoadImageFromVFS(const VDStringW &vfsPath) {
#ifndef ALTIRRA_NO_SDL3_IMAGE
	try {
		vdrefptr<ATVFSFileView> view;
		ATVFSOpenFileView(vfsPath.c_str(), false, ~view);
		if (!view)
			return nullptr;

		IVDRandomAccessStream &stream = view->GetStream();

		sint64 len = stream.Length();
		if (len <= 0 || len > 64 * 1024 * 1024)
			return nullptr;

		stream.Seek(0);
		std::vector<uint8_t> buf((size_t)len);
		stream.Read(buf.data(), (sint32)len);

		SDL_IOStream *io = SDL_IOFromConstMem(buf.data(), buf.size());
		if (!io)
			return nullptr;

		SDL_Surface *surf = IMG_Load_IO(io, true);
		if (!surf)
			return nullptr;

		SDL_Surface *conv = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_BGRA32);
		SDL_DestroySurface(surf);
		return conv;
	} catch (...) {
		return nullptr;
	}
#else
	return nullptr;
#endif
}

static bool IsVFSPath(const VDStringW &path) {
	// VFS paths contain "://" (zip://, gz://, etc.)
	const wchar_t *s = path.c_str();
	for (const wchar_t *p = s; *p; ++p) {
		if (p[0] == L':' && p[1] == L'/' && p[2] == L'/')
			return true;
	}
	return false;
}

static SDL_Surface *LoadArtImage(const VDStringW &artPath) {
	if (artPath.empty())
		return nullptr;

	if (IsVFSPath(artPath))
		return LoadImageFromVFS(artPath);
	else
		return LoadImageFromFilesystem(artPath);
}

// Scale down a surface to fit within maxW x maxH, preserving aspect ratio.
// Returns a new surface (caller must free) or nullptr on error.
static SDL_Surface *ScaleDown(SDL_Surface *src, int maxW, int maxH) {
	if (!src || src->w <= 0 || src->h <= 0)
		return nullptr;

	if (src->w <= maxW && src->h <= maxH)
		return nullptr;

	float scaleX = (float)maxW / (float)src->w;
	float scaleY = (float)maxH / (float)src->h;
	float scale = (scaleX < scaleY) ? scaleX : scaleY;

	int dstW = (int)(src->w * scale);
	int dstH = (int)(src->h * scale);
	if (dstW < 1) dstW = 1;
	if (dstH < 1) dstH = 1;

	SDL_Surface *dst = SDL_CreateSurface(dstW, dstH, src->format);
	if (!dst)
		return nullptr;

	if (!SDL_BlitSurfaceScaled(src, nullptr, dst, nullptr, SDL_SCALEMODE_LINEAR)) {
		SDL_DestroySurface(dst);
		return nullptr;
	}

	return dst;
}

// =========================================================================
// Disk thumbnail cache
// =========================================================================

void GameArtCache::SetCacheDir(const VDStringA &configDir) {
	mThumbDir = configDir;
	if (!mThumbDir.empty() && mThumbDir.back() != '/')
		mThumbDir += '/';
	mThumbDir += "thumbnails";
	mkdir(mThumbDir.c_str(), 0755);
}

VDStringA GameArtCache::GetThumbPath(const VDStringW &artPath) const {
	if (mThumbDir.empty())
		return VDStringA();

	VDStringA u8 = VDTextWToU8(artPath);
	ATMD5Digest digest = ATComputeMD5(u8.c_str(), u8.size());

	char hex[33];
	for (int i = 0; i < 16; ++i)
		snprintf(hex + i * 2, 3, "%02x", digest.digest[i]);

	VDStringA path = mThumbDir;
	path += '/';
	path += hex;
	path += ".png";
	return path;
}

SDL_Surface *GameArtCache::LoadCachedThumb(const VDStringA &thumbPath) {
#ifndef ALTIRRA_NO_SDL3_IMAGE
	if (thumbPath.empty())
		return nullptr;

	SDL_IOStream *io = SDL_IOFromFile(thumbPath.c_str(), "rb");
	if (!io)
		return nullptr;

	SDL_Surface *surf = IMG_Load_IO(io, true);
	if (!surf)
		return nullptr;

	SDL_Surface *conv = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_BGRA32);
	SDL_DestroySurface(surf);
	return conv;
#else
	return nullptr;
#endif
}

void GameArtCache::SaveThumb(const VDStringA &thumbPath, SDL_Surface *surf) {
#ifndef ALTIRRA_NO_SDL3_IMAGE
	if (thumbPath.empty() || !surf)
		return;

	SDL_Surface *rgba = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);
	if (!rgba)
		return;

	IMG_SavePNG(rgba, thumbPath.c_str());
	SDL_DestroySurface(rgba);
#endif
}

// =========================================================================
// GameArtCache
// =========================================================================

GameArtCache::GameArtCache() {
	mLoaderThread = std::thread([this]() { LoaderThread(); });
}

GameArtCache::~GameArtCache() {
	Shutdown();
}

void GameArtCache::Shutdown() {
	if (mLoaderThread.joinable()) {
		mShutdownFlag.store(true, std::memory_order_release);
		mLoaderCV.notify_one();
		mLoaderThread.join();
	}

	Clear();

	std::lock_guard<std::mutex> lock(mQueueMutex);
	for (auto &r : mResultQueue) {
		if (r.mSurface)
			SDL_DestroySurface(r.mSurface);
	}
	mResultQueue.clear();
	mLoadQueue.clear();
}

ImTextureID GameArtCache::GetTexture(const VDStringW &artPath, int *outW, int *outH) {
	if (artPath.empty())
		return (ImTextureID)0;

	std::wstring key(artPath.c_str(), artPath.size());
	auto it = mKeyToIndex.find(key);

	if (it != mKeyToIndex.end()) {
		auto &entry = mEntries[it->second];
		entry.mLastAccessFrame = mFrameCounter;
		if (entry.mState == EntryState::Ready) {
			if (outW) *outW = entry.mWidth;
			if (outH) *outH = entry.mHeight;
			return entry.mTexID;
		}
		return (ImTextureID)0;
	}

	// Cache miss: try to make room, but don't evict current-frame entries
	// (their textures may still be referenced in ImGui's draw list)
	if (mEntries.size() >= kMaxCacheSize) {
		EvictOldest();
		if (mEntries.size() >= kMaxCacheSize)
			return (ImTextureID)0;
	}

	size_t idx = mEntries.size();
	CacheEntry newEntry;
	mEntries.push_back(std::move(newEntry));
	auto &entry = mEntries.back();
	entry.mState = EntryState::Loading;
	entry.mLastAccessFrame = mFrameCounter;
	entry.mKey = key;
	mKeyToIndex[key] = idx;

	{
		std::lock_guard<std::mutex> lock(mQueueMutex);
		mLoadQueue.push_back(key);
	}
	mLoaderCV.notify_one();

	return (ImTextureID)0;
}

void GameArtCache::ProcessPending() {
	++mFrameCounter;

	std::vector<LoadResult> results;
	{
		std::lock_guard<std::mutex> lock(mQueueMutex);
		results.swap(mResultQueue);
	}

	int uploaded = 0;
	for (auto &r : results) {
		auto it = mKeyToIndex.find(r.mKey);
		if (it == mKeyToIndex.end()) {
			if (r.mSurface)
				SDL_DestroySurface(r.mSurface);
			continue;
		}

		auto &entry = mEntries[it->second];

		if (!r.mSurface) {
			entry.mState = EntryState::Failed;
			continue;
		}

		if (uploaded >= kMaxUploadsPerFrame) {
			// Re-queue for next frame
			std::lock_guard<std::mutex> lock(mQueueMutex);
			mResultQueue.push_back(std::move(r));
			continue;
		}

		UploadSurface(entry, r.mSurface);
		SDL_DestroySurface(r.mSurface);
		++uploaded;
	}
}

void GameArtCache::Clear() {
	for (auto &entry : mEntries)
		DestroyEntry(entry);
	mEntries.clear();
	mKeyToIndex.clear();
}

void GameArtCache::EvictOldest() {
	if (mEntries.empty())
		return;

	// Find least-recently-used entry, but never evict one accessed this
	// frame — its texture may still be referenced in ImGui's draw list.
	size_t oldestIdx = SIZE_MAX;
	uint32_t oldestFrame = mFrameCounter;
	for (size_t i = 0; i < mEntries.size(); ++i) {
		if (mEntries[i].mLastAccessFrame < oldestFrame) {
			oldestFrame = mEntries[i].mLastAccessFrame;
			oldestIdx = i;
		}
	}

	if (oldestIdx == SIZE_MAX)
		return;

	// Remove from key map
	mKeyToIndex.erase(mEntries[oldestIdx].mKey);
	DestroyEntry(mEntries[oldestIdx]);

	// Swap with last and pop
	size_t lastIdx = mEntries.size() - 1;
	if (oldestIdx != lastIdx) {
		auto keyIt = mKeyToIndex.find(mEntries[lastIdx].mKey);
		if (keyIt != mKeyToIndex.end())
			keyIt->second = oldestIdx;
		mEntries[oldestIdx] = std::move(mEntries[lastIdx]);
	}
	mEntries.pop_back();
}

void GameArtCache::DestroyEntry(CacheEntry &entry) {
	if (entry.mGLTexture) {
		glDeleteTextures(1, &entry.mGLTexture);
		entry.mGLTexture = 0;
	}
	if (entry.mSDLTexture) {
		SDL_DestroyTexture(entry.mSDLTexture);
		entry.mSDLTexture = nullptr;
	}
	entry.mTexID = (ImTextureID)0;
	entry.mState = EntryState::Empty;
}

void GameArtCache::UploadSurface(CacheEntry &entry, SDL_Surface *surf) {
	if (!surf || surf->w <= 0 || surf->h <= 0) {
		entry.mState = EntryState::Failed;
		return;
	}

	// Clean up any existing texture (possible if evict+re-request caused
	// two loads for the same key)
	DestroyEntry(entry);

	IDisplayBackend *backend = ATUIGetDisplayBackend();
	bool useGL = backend && backend->GetType() == DisplayBackendType::OpenGL33;

	entry.mWidth = surf->w;
	entry.mHeight = surf->h;

	if (useGL) {
		entry.mGLTexture = GLCreateTexture2D(
			surf->w, surf->h, GL_RGBA8, GL_BGRA,
			GL_UNSIGNED_INT_8_8_8_8_REV, nullptr, true);

		if (!entry.mGLTexture) {
			entry.mState = EntryState::Failed;
			return;
		}

		std::vector<uint32_t> buf(surf->w * surf->h);
		const uint8_t *src = (const uint8_t *)surf->pixels;
		for (int y = 0; y < surf->h; ++y)
			memcpy(&buf[y * surf->w], src + y * surf->pitch, surf->w * 4);

		glBindTexture(GL_TEXTURE_2D, entry.mGLTexture);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, surf->w, surf->h,
			GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, buf.data());

		entry.mTexID = (ImTextureID)(intptr_t)entry.mGLTexture;
	} else {
		SDL_Renderer *renderer = SDL_GetRenderer(g_pWindow);
		if (!renderer) {
			entry.mState = EntryState::Failed;
			return;
		}

		entry.mSDLTexture = SDL_CreateTexture(renderer,
			SDL_PIXELFORMAT_BGRA32,
			SDL_TEXTUREACCESS_STREAMING, surf->w, surf->h);

		if (!entry.mSDLTexture) {
			entry.mState = EntryState::Failed;
			return;
		}

		void *pixels = nullptr;
		int pitch = 0;
		if (!SDL_LockTexture(entry.mSDLTexture, nullptr, &pixels, &pitch)) {
			SDL_DestroyTexture(entry.mSDLTexture);
			entry.mSDLTexture = nullptr;
			entry.mState = EntryState::Failed;
			return;
		}

		const uint8_t *src = (const uint8_t *)surf->pixels;
		uint8_t *dst = (uint8_t *)pixels;
		int copyBytes = surf->w * 4;
		for (int y = 0; y < surf->h; ++y) {
			memcpy(dst, src, copyBytes);
			src += surf->pitch;
			dst += pitch;
		}
		SDL_UnlockTexture(entry.mSDLTexture);

		entry.mTexID = (ImTextureID)entry.mSDLTexture;
	}

	entry.mState = EntryState::Ready;
}

void GameArtCache::LoaderThread() {
	while (!mShutdownFlag.load(std::memory_order_acquire)) {
		std::wstring key;
		{
			std::unique_lock<std::mutex> lock(mQueueMutex);
			mLoaderCV.wait(lock, [this]() {
				return mShutdownFlag.load(std::memory_order_acquire)
					|| !mLoadQueue.empty();
			});

			if (mShutdownFlag.load(std::memory_order_acquire))
				return;

			if (mLoadQueue.empty())
				continue;

			key = std::move(mLoadQueue.back());
			mLoadQueue.pop_back();
		}

		VDStringW artPath(key.c_str(), (uint32)key.size());

		VDStringA thumbPath = GetThumbPath(artPath);
		SDL_Surface *surf = LoadCachedThumb(thumbPath);

		if (!surf) {
			surf = LoadArtImage(artPath);

			if (surf) {
				SDL_Surface *scaled = ScaleDown(surf, 320, 240);
				if (scaled) {
					SDL_DestroySurface(surf);
					surf = scaled;
				}

				SaveThumb(thumbPath, surf);
			}
		}

		{
			std::lock_guard<std::mutex> lock(mQueueMutex);
			LoadResult r;
			r.mKey = std::move(key);
			r.mSurface = surf;
			mResultQueue.push_back(std::move(r));
		}
	}
}
