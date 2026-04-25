//	AltirraSDL - portable directory watcher.
//
//	Replaces the Win32 src/Altirra/source/directorywatcher.cpp (excluded
//	from the SDL3 build because it uses ReadDirectoryChangesW / HANDLE
//	events). The polling code path in the Win32 version was already
//	platform-agnostic; we reuse that logic verbatim and substitute a
//	std::condition_variable for the exit-event handle.
//
//	Used by:
//	  - diskvirtimage.cpp     (host directory mounted as a virtual ATR)
//	  - diskvirtimagesdfs.cpp (host directory mounted as virtual SpartaDOS FS)
//
//	Without a working watcher, files added/removed in the host directory
//	while the emulator is running stay invisible to the running Atari
//	program until manual remount. The previous device_stubs.cpp impl
//	always returned "no change", silently breaking that feature on
//	AltirraSDL Linux/macOS/Android — and the Windows-SDL3 build, since
//	the CMake glob excludes directorywatcher.cpp on every platform.

#include <stdafx.h>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <vd2/system/binary.h>
#include <vd2/system/filesys.h>
#include <vd2/system/VDString.h>
#include <at/atcore/checksum.h>
#include "directorywatcher.h"

bool ATDirectoryWatcher::sbShouldUsePolling = false;

void ATDirectoryWatcher::SetShouldUsePolling(bool enabled) {
	sbShouldUsePolling = enabled;
}

namespace {
	// Stashed in mhExitEvent so we don't have to grow the shared header.
	struct ExitState {
		std::mutex mtx;
		std::condition_variable cv;
		bool quit = false;
	};
}

ATDirectoryWatcher::ATDirectoryWatcher()
	: VDThread("Altirra directory watcher")
	, mhDir(nullptr)
	, mhExitEvent(nullptr)
	, mhDirChangeEvent(nullptr)
	, mpChangeBuffer(nullptr)
	, mChangeBufferSize(0)
	, mbRecursive(false)
	, mbAllChanged(false)
{
}

ATDirectoryWatcher::~ATDirectoryWatcher() {
	Shutdown();
}

void ATDirectoryWatcher::Init(const wchar_t *basePath, bool recursive) {
	Shutdown();

	mBasePath = VDGetLongPath(basePath);
	mbRecursive = recursive;

	// SDL3 build always uses the polling path; ReadDirectoryChangesW has
	// no portable analog and per-platform inotify/FSEvents/kqueue glue is
	// out of scope for now. mpChangeBuffer stays null so ThreadRun()
	// dispatches to RunPollThread() (see directorywatcher.cpp:128-133).
	mhExitEvent = new ExitState();

	ThreadStart();
}

void ATDirectoryWatcher::Shutdown() {
	auto *exit = static_cast<ExitState *>(mhExitEvent);

	if (isThreadAttached()) {
		if (exit) {
			std::lock_guard<std::mutex> lock(exit->mtx);
			exit->quit = true;
			exit->cv.notify_all();
		}
		ThreadWait();
	}

	delete exit;
	mhExitEvent = nullptr;
}

bool ATDirectoryWatcher::CheckForChanges() {
	bool changed = false;

	vdsynchronized(mMutex) {
		changed = mbAllChanged;

		if (changed) {
			mbAllChanged = false;
		} else if (!mChangedDirs.empty()) {
			mChangedDirs.clear();
			changed = true;
		}
	}

	return changed;
}

bool ATDirectoryWatcher::CheckForChanges(vdfastvector<wchar_t>& strheap) {
	bool allChanged = false;
	strheap.clear();

	vdsynchronized(mMutex) {
		allChanged = mbAllChanged;

		if (allChanged) {
			mbAllChanged = false;
		} else {
			for (auto it = mChangedDirs.begin(), itEnd = mChangedDirs.end();
				it != itEnd; ++it)
			{
				const VDStringW& s = *it;
				const wchar_t *t = s.c_str();
				strheap.insert(strheap.end(), t, t + s.size() + 1);
			}
		}

		mChangedDirs.clear();
	}

	return allChanged;
}

void ATDirectoryWatcher::ThreadRun() {
	// SDL3 build always polls — we never set up mpChangeBuffer.
	RunPollThread();
}

void ATDirectoryWatcher::PollDirectory(uint32 *orderIndependentChecksum, const VDStringSpanW& path, uint32 nestingLevel) {
	ATChecksumEngineSHA256 mChecksumEngine;

	VDDirectoryIterator it(VDMakePath(path, VDStringSpanW(L"*")).c_str());
	while (it.Next()) {
		const VDStringW& fullItemPath = it.GetFullPath();

		mChecksumEngine.Reset();
		mChecksumEngine.Process(fullItemPath.data(), fullItemPath.size() * sizeof(fullItemPath[0]));

		const struct MiscData {
			sint64 mSize;
			uint64 mCreationDate;
			uint64 mLastWriteDate;
			uint32 mAttributes;
			uint32 mPad;
		} miscData = {
			it.GetSize(),
			it.GetCreationDate().mTicks,
			it.GetLastWriteDate().mTicks,
			it.GetAttributes()
		};

		mChecksumEngine.Process(&miscData, sizeof miscData);
		const auto& checksum = mChecksumEngine.Finalize();

		uint32 c = 0;
		for (uint32 i = 0; i < 8; ++i) {
			uint32 x = orderIndependentChecksum[i];
			uint32 y = VDReadUnalignedU32(&checksum.mDigest[i * 4]);
			uint64 sum = (uint64)x + y + c;

			orderIndependentChecksum[i] = (uint32)sum;
			c = (uint32)(sum >> 32);
		}

		if (it.IsDirectory() && !it.IsLink() && nestingLevel < 8 && mbRecursive)
			PollDirectory(orderIndependentChecksum, fullItemPath, nestingLevel + 1);
	}
}

void ATDirectoryWatcher::RunPollThread() {
	const uint32 delayMs = 1000;
	uint32 lastChecksum[8] = {};
	bool firstPoll = true;

	auto *exit = static_cast<ExitState *>(mhExitEvent);
	if (!exit)
		return;

	for (;;) {
		uint32 newChecksum[8] = {};
		PollDirectory(newChecksum, mBasePath, 0);

		if (memcmp(newChecksum, lastChecksum, sizeof newChecksum) || firstPoll) {
			memcpy(lastChecksum, newChecksum, sizeof lastChecksum);

			if (firstPoll)
				firstPoll = false;
			else
				NotifyAllChanged();
		}

		std::unique_lock<std::mutex> lock(exit->mtx);
		if (exit->cv.wait_for(lock, std::chrono::milliseconds(delayMs),
			[exit]() { return exit->quit; }))
		{
			break;
		}
	}
}

// RunNotifyThread is the Win32-only event-driven path. The SDL3 build
// never invokes it (mpChangeBuffer stays null), but we have to keep
// the symbol so the shared header's vtable still resolves cleanly when
// the linker stitches things together.
void ATDirectoryWatcher::RunNotifyThread() {
}

void ATDirectoryWatcher::NotifyAllChanged() {
	vdsynchronized(mMutex) {
		mbAllChanged = true;
	}
}
