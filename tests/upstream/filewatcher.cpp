// Portable watcher contract: metadata precision and host-loop callbacks.
#include <stdafx.h>
#include <vd2/system/filewatcher.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/date.h>
#include <vd2/system/text.h>
#include <vd2/system/time.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <stdexcept>
#include <thread>
#include <fcntl.h>
#include <sys/stat.h>

extern "C" void VDLazyTimerTick();
namespace fs = std::filesystem;
namespace {
void Check(bool condition, const char* message) {
	if (!condition) throw std::runtime_error(message);
}
void Write(const fs::path& path, const char* contents) {
	std::ofstream file(path, std::ios::binary);
	file << contents;
	Check((bool)file, "fixture write failed");
}
void Stamp(const fs::path& path, long nanoseconds, time_t seconds = 1700000000) {
	timespec times[] = { { 0, UTIME_OMIT }, { seconds, nanoseconds } };
	Check(!utimensat(AT_FDCWD, path.c_str(), times, 0), "fixture timestamp failed");
}
VDStringW Wide(const fs::path& path) {
	return VDTextU8ToW(path.string().c_str(), -1);
}
struct Callback final : IVDFileWatcherCallback {
	unsigned calls = 0;
	bool repeat = false;
	VDFileWatcher* shutdown = nullptr;
	std::thread::id thread;
	std::function<void()> action;
	bool OnFileUpdated(const wchar_t*) override {
		++calls;
		thread = std::this_thread::get_id();
		if (shutdown) shutdown->Shutdown();
		if (action) action();
		return !repeat || calls > 1;
	}
};
void PumpUntil(Callback& callback, unsigned calls) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
	while (callback.calls < calls && std::chrono::steady_clock::now() < deadline) {
		VDLazyTimerTick();
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	Check(callback.calls == calls, "host-loop watcher callback did not fire");
	Check(callback.thread == std::this_thread::get_id(), "callback left host-loop thread");
}
}
int main() {
	const auto root = fs::temp_directory_path() /
		("altirra-watcher-" + std::to_string(VDGetPreciseTick()));
	try {
		fs::create_directories(root);
		const auto file = root / "device.atdevice";
		const auto future = root / "future";
		Write(file, "first"); Stamp(file, 1000);
		Write(future, "future"); Stamp(future, 1000, 2000000000);
		const auto wideRoot = Wide(root), wideFile = Wide(file);
		VDFileWatcher watcher;
		watcher.InitDir(wideRoot.c_str(), false, nullptr);
		Check(!watcher.Wait(0), "unchanged directory reported a change");
		Write(file, "other"); Stamp(file, 2000);
		Check(watcher.Wait(0), "same-second nonmaximum file edit was missed");
		Check(!watcher.Wait(0), "stable directory reported repeated changes");
		fs::rename(file, root / "renamed");
		Check(watcher.Wait(0), "rename was missed");
		fs::remove(root / "renamed");
		Check(watcher.Wait(0), "deletion was missed");
		Write(file, "first"); Stamp(file, 3000);
		watcher.Init(wideFile.c_str(), nullptr);
		VDFile opened(wideFile.c_str());
		const auto before = opened.getLastWriteTime().mTicks;
		Check(before == VDFileGetLastWriteTime(wideFile.c_str()),
			"file timestamp APIs disagree on units");
		Write(file, "other"); Stamp(file, 6000);
		Check(watcher.Wait(0), "single-file same-second edit was missed");
		Check(opened.getLastWriteTime().mTicks - before == 30,
			"open-file timestamp lost subsecond 100ns precision");
		Check(VDFileGetLastWriteTime(wideFile.c_str()) - before == 30,
			"path timestamp lost subsecond 100ns precision");
		opened.close();
		// Even a writer preserving mtime must be noticed via size/ctime.
		Write(file, "different length"); Stamp(file, 6000);
		Check(watcher.Wait(0), "preserved-mtime file modification was missed");
		fs::remove(file);
		Check(watcher.Wait(0), "single-file removal was missed");
		Write(file, "restored");
		Check(watcher.Wait(0), "single-file recreation was missed");
		fs::create_directory(root / "subdir");
		Write(root / "subdir" / "child", "first");
		watcher.InitDir(wideRoot.c_str(), true, nullptr);
		Write(root / "subdir" / "child", "different length");
		Check(watcher.Wait(0), "recursive child edit was missed");
		Callback callback;
		callback.repeat = true;
		watcher.InitDir(wideRoot.c_str(), false, &callback);
		Write(file, "callback change");
		// No watcher.Wait(): custom-device hot reload relies on automatic
		// callbacks, including while emulated CPU execution is paused.
		PumpUntil(callback, 2);
		watcher.Shutdown();
		Write(file, "after shutdown");
		VDLazyTimerTick();
		Check(callback.calls == 2, "shutdown retained a callback");
		Callback selfStopping;
		selfStopping.shutdown = &watcher;
		watcher.InitDir(wideRoot.c_str(), false, &selfStopping);
		Write(file, "stop inside callback");
		PumpUntil(selfStopping, 1);
		Check(!watcher.IsActive(), "callback could not shut down its watcher");
		// Both timers are copied into the scheduler's due list before A runs.
		// Restarting B must invalidate its already queued old-generation call.
		Callback firstDue, oldGeneration, replacement;
		VDFileWatcher firstWatcher, secondWatcher;
		firstDue.action = [&] {
			firstWatcher.Shutdown();
			secondWatcher.Init(wideFile.c_str(), &replacement);
			Write(file, "new generation change");
		};
		firstWatcher.Init(wideFile.c_str(), &firstDue);
		secondWatcher.Init(wideFile.c_str(), &oldGeneration);
		Write(file, "both due");
		std::this_thread::sleep_for(std::chrono::milliseconds(1100));
		VDLazyTimerTick();
		Check(firstDue.calls == 1, "first due callback did not run");
		Check(!replacement.calls && !oldGeneration.calls,
			"stale queued watcher callback entered a new generation");
		PumpUntil(replacement, 1);
		secondWatcher.Shutdown();

		// A may destroy B after B's callback was copied into the due list.
		Callback deletingOther, deletedOther;
		auto victim = std::make_unique<VDFileWatcher>();
		deletingOther.action = [&] {
			firstWatcher.Shutdown();
			victim.reset();
		};
		firstWatcher.Init(wideFile.c_str(), &deletingOther);
		victim->Init(wideFile.c_str(), &deletedOther);
		Write(file, "delete queued watcher");
		std::this_thread::sleep_for(std::chrono::milliseconds(1100));
		VDLazyTimerTick();
		Check(deletingOther.calls == 1 && !deletedOther.calls && !victim,
			"destroyed watcher retained its queued callback");

		// A callback may destroy its own watcher and request another attempt.
		Callback deletingSelf;
		deletingSelf.repeat = true;
		auto self = std::make_unique<VDFileWatcher>();
		deletingSelf.action = [&] { self.reset(); };
		self->Init(wideFile.c_str(), &deletingSelf);
		Write(file, "delete self");
		PumpUntil(deletingSelf, 1);
		Check(!self, "callback did not destroy its watcher");

		// The retry path must also tolerate deletion during OnFileUpdated.
		Callback deletingOnRetry;
		deletingOnRetry.repeat = true;
		self = std::make_unique<VDFileWatcher>();
		deletingOnRetry.action = [&] {
			if (deletingOnRetry.calls == 2) self.reset();
		};
		self->Init(wideFile.c_str(), &deletingOnRetry);
		Write(file, "delete on retry");
		PumpUntil(deletingOnRetry, 2);
		Check(!self, "retry callback did not destroy its watcher");

		// A failed old callback must not mark a freshly initialized watch
		// for retry. Its replacement has not seen any file change.
		Callback restartingSelf, restarted;
		restartingSelf.repeat = true;
		restartingSelf.action = [&] {
			watcher.Init(wideFile.c_str(), &restarted);
		};
		watcher.Init(wideFile.c_str(), &restartingSelf);
		Write(file, "restart self");
		Check(watcher.Wait(0), "self-restart fixture saw no modification");
		std::this_thread::sleep_for(std::chrono::milliseconds(1100));
		VDLazyTimerTick();
		Check(!restarted.calls, "old callback requested retry on a new watch");

		watcher.InitDir(wideRoot.c_str(), false, nullptr);
		fs::remove_all(root);
		Check(watcher.Wait(0), "watched-directory removal was missed");
		std::puts("Portable file watcher metadata and callbacks passed");
		return 0;
	} catch (const std::exception& error) {
		std::fprintf(stderr, "%s\n", error.what());
		std::error_code ignored;
		fs::remove_all(root, ignored);
		return 1;
	}
}
