//	Altirra SDL3 frontend - portable VDFileWatcher
//
//	Polling-based replacement for the Win32 FindFirstChangeNotification
//	implementation in filewatcher.cpp. Explicit Wait(0) polling and
//	callback timers both run on the host-loop thread. Directory fingerprints
//	include every entry so same-second edits, replacement and deletion are
//	detected even when a sibling has a newer timestamp.

#include <stdafx.h>
#include <vd2/system/filesys.h>
#include <vd2/system/filewatcher.h>
#include <vd2/system/VDString.h>
#include <vd2/system/error.h>
#include <vd2/system/text.h>
#include <sys/stat.h>
#include <dirent.h>
#include <cstring>
#include <memory>
#include <vd2/system/time.h>
#include "filemetadata_posix.h"

namespace {
	struct WatchState {
		VDLazyTimer timer;
		// Stop() cannot recall a callback already copied by the timer pump.
		// A separate token outlives that copy, and also identifies this Init.
		std::shared_ptr<bool> alive = std::make_shared<bool>(true);
		bool recursive = false;
	};

	void HashWord(uint64& hash, uint64 value) {
		for (unsigned i = 0; i < 8; ++i) {
			hash = (hash ^ (uint8)value) * UINT64_C(1099511628211);
			value >>= 8;
		}
	}

	uint64 Metadata(const struct stat& info) {
		uint64 hash = UINT64_C(14695981039346656037);
		HashWord(hash, info.st_dev);
		HashWord(hash, info.st_ino);
		HashWord(hash, info.st_mode);
		HashWord(hash, info.st_size);
		const auto modified = VDPosixModificationTime(info);
		const auto changed = VDPosixChangeTime(info);
		// Hash the components separately, avoiding nanosecond epoch overflow.
		HashWord(hash, modified.tv_sec);
		HashWord(hash, modified.tv_nsec);
		HashWord(hash, changed.tv_sec);
		HashWord(hash, changed.tv_nsec);
		return hash;
	}

	uint64 FileFingerprint(const char* path) {
		struct stat info {};
		return stat(path, &info) ? 0 : Metadata(info);
	}

	uint64 DirectoryFingerprint(const char* path, bool recursive) {
		uint64 hash = FileFingerprint(path);
		if (!hash) return 0;
		DIR* directory = opendir(path);
		if (!directory) return hash;
		while (auto* entry = readdir(directory)) {
			if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
				continue;
			VDStringA child(path);
			if (!child.empty() && child.back() != '/') child += '/';
			child += entry->d_name;
			uint64 member = UINT64_C(14695981039346656037);
			for (const auto* c = entry->d_name; *c; ++c)
				member = (member ^ (uint8)*c) * UINT64_C(1099511628211);
			struct stat info {};
			if (!stat(child.c_str(), &info)) {
				HashWord(member, Metadata(info));
				struct stat linkInfo {};
				if (recursive && S_ISDIR(info.st_mode)
					&& !lstat(child.c_str(), &linkInfo) && !S_ISLNK(linkInfo.st_mode))
					HashWord(member, DirectoryFingerprint(child.c_str(), true));
			}
			// readdir ordering is not stable. Every entry contributes, so
			// newer/future-dated siblings cannot hide an older file's edit.
			hash += member;
		}
		closedir(directory);
		return hash;
	}

	uint64 Fingerprint(const wchar_t* path, bool directory, bool recursive) {
		const auto utf8 = VDTextWToU8(VDStringW(path));
		return directory ? DirectoryFingerprint(utf8.c_str(), recursive)
			: FileFingerprint(utf8.c_str());
	}
}

VDFileWatcher::VDFileWatcher()
	: mChangeHandle(nullptr)
	, mLastWriteTime(0)
	, mbWatchDir(false)
	, mpCB(nullptr)
	, mbRepeatRequested(false)
	, mbThunksInited(false)
	, mpThunk(nullptr)
	, mTimerId(0)
{
}

VDFileWatcher::~VDFileWatcher() {
	Shutdown();
}

bool VDFileWatcher::IsActive() const {
	// The portable state owns the cooperative callback timer.
	return mChangeHandle != nullptr;
}

void VDFileWatcher::Init(const wchar_t *file, IVDFileWatcherCallback *callback) {
	Shutdown();

	mPath = file;
	mLastWriteTime = Fingerprint(mPath.c_str(), false, false);
	mpCB = callback;
	mbRepeatRequested = false;
	mbWatchDir = false;
	auto* state = new WatchState;
	mChangeHandle = state;
	if (callback) state->timer.SetPeriodicFn([this, alive = state->alive] {
		if (!*alive) return;
		if (mbRepeatRequested && mpCB) {
			const bool repeat = !mpCB->OnFileUpdated(mPath.c_str());
			// The callback can delete or reinitialize this watcher.
			if (*alive) mbRepeatRequested = repeat;
		} else Wait(0);
	}, 1000);
}

void VDFileWatcher::InitDir(const wchar_t *path, bool subdirs, IVDFileWatcherCallback *callback) {
	Shutdown();

	mPath = path;
	mLastWriteTime = Fingerprint(mPath.c_str(), true, subdirs);
	mpCB = callback;
	mbRepeatRequested = false;
	mbWatchDir = true;
	auto* state = new WatchState;
	state->recursive = subdirs;
	mChangeHandle = state;
	if (callback) state->timer.SetPeriodicFn([this, alive = state->alive] {
		if (!*alive) return;
		if (mbRepeatRequested && mpCB) {
			const bool repeat = !mpCB->OnFileUpdated(mPath.c_str());
			// The callback can delete or reinitialize this watcher.
			if (*alive) mbRepeatRequested = repeat;
		} else Wait(0);
	}, 1000);
}

void VDFileWatcher::Shutdown() {
	auto* state = static_cast<WatchState*>(mChangeHandle);
	if (state) *state->alive = false;
	mChangeHandle = nullptr;
	delete state;
	mPath.clear();
	mpCB = nullptr;
	mLastWriteTime = 0;
	mbWatchDir = false;
	mbRepeatRequested = false;
}

bool VDFileWatcher::Wait(uint32 /*delay*/) {
	if (!mChangeHandle)
		return false;

	const uint64 t = Fingerprint(mPath.c_str(), mbWatchDir,
		static_cast<WatchState*>(mChangeHandle)->recursive);

	if (t == mLastWriteTime)
		return false;

	mLastWriteTime = t;

	if (mpCB) {
		const auto alive = static_cast<WatchState*>(mChangeHandle)->alive;
		const bool repeat = !mpCB->OnFileUpdated(mPath.c_str());
		if (*alive) mbRepeatRequested = repeat;
	}

	return true;
}
