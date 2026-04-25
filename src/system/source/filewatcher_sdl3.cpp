//	Altirra SDL3 frontend - portable VDFileWatcher
//
//	Polling-based replacement for the Win32 FindFirstChangeNotification
//	implementation in filewatcher.cpp. Driven from the main thread via
//	Wait(0) calls — the consumer (custom-device hot reload) polls each
//	frame, so no background thread is required. Behaviour mirrors
//	the Win32 version: track last write time of a single file, or last
//	mtime of the most recently changed entry in a directory.

#include <stdafx.h>
#include <vd2/system/filesys.h>
#include <vd2/system/filewatcher.h>
#include <vd2/system/VDString.h>
#include <vd2/system/error.h>
#include <vd2/system/text.h>
#include <sys/stat.h>
#include <dirent.h>

namespace {
	// Returns the most recent mtime (in nanoseconds) found in the
	// directory, or 0 on error. Non-recursive; mirrors the Win32 watcher
	// scope (the customdevice consumer passes subdirs=false).
	uint64 GetDirectoryMaxMTime(const wchar_t *path) {
		VDStringA u8 = VDTextWToU8(VDStringW(path));

		struct stat st {};
		if (stat(u8.c_str(), &st) != 0)
			return 0;

		// st_mtime (seconds) is the only mtime member portable across
		// Linux (st_mtim), macOS/BSD (st_mtimespec), and other POSIX
		// systems. Sub-second precision isn't needed for polling-based
		// hot reload — config files don't typically change twice a
		// second.
		uint64 maxT = (uint64)st.st_mtime;

		DIR *d = opendir(u8.c_str());
		if (!d)
			return maxT;

		while (struct dirent *e = readdir(d)) {
			if (e->d_name[0] == '.' &&
				(e->d_name[1] == 0 ||
				 (e->d_name[1] == '.' && e->d_name[2] == 0)))
				continue;

			VDStringA child = u8;
			if (!child.empty() && child.back() != '/')
				child += '/';
			child += e->d_name;

			struct stat cs {};
			if (stat(child.c_str(), &cs) != 0)
				continue;

			uint64 t = (uint64)cs.st_mtime;
			if (t > maxT)
				maxT = t;
		}

		closedir(d);
		return maxT;
	}
}

VDFileWatcher::VDFileWatcher()
	: mChangeHandle(nullptr)
	, mLastWriteTime(0)
	, mbWatchDir(false)
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
	// Sentinel: store a non-null marker in mChangeHandle to indicate
	// "active". On Win32 this is a real HANDLE; here we use the
	// address of mPath to distinguish active from inactive.
	return mChangeHandle != nullptr;
}

void VDFileWatcher::Init(const wchar_t *file, IVDFileWatcherCallback *callback) {
	Shutdown();

	mPath = file;
	mLastWriteTime = VDFileGetLastWriteTime(mPath.c_str());
	mpCB = callback;
	mbRepeatRequested = false;
	mbWatchDir = false;
	mChangeHandle = (void *)&mPath;
}

void VDFileWatcher::InitDir(const wchar_t *path, bool /*subdirs*/, IVDFileWatcherCallback *callback) {
	Shutdown();

	mPath = path;
	mLastWriteTime = GetDirectoryMaxMTime(mPath.c_str());
	mpCB = callback;
	mbRepeatRequested = false;
	mbWatchDir = true;
	mChangeHandle = (void *)&mPath;
}

void VDFileWatcher::Shutdown() {
	mChangeHandle = nullptr;
	mPath.clear();
	mpCB = nullptr;
	mLastWriteTime = 0;
	mbWatchDir = false;
	mbRepeatRequested = false;
}

bool VDFileWatcher::Wait(uint32 /*delay*/) {
	if (!mChangeHandle)
		return false;

	uint64 t = mbWatchDir
		? GetDirectoryMaxMTime(mPath.c_str())
		: VDFileGetLastWriteTime(mPath.c_str());

	if (t == mLastWriteTime)
		return false;

	mLastWriteTime = t;

	if (mpCB)
		mbRepeatRequested = !mpCB->OnFileUpdated(mPath.c_str());

	return true;
}
