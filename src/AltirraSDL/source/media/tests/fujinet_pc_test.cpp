// Self-hosted child fixture: exercises SDL process ownership without a shell.
#include <stdafx.h>
#include "fujinet_pc.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#ifndef _WIN32
#include <csignal>
#endif

namespace fs = std::filesystem;
namespace {
std::string UTF8(const fs::path& path) {
	const auto s = path.u8string();
	return std::string((const char*)s.data(), s.size());
}
std::string Read(const fs::path& path) {
	std::ifstream file(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(file), {});
}
void Write(const fs::path& path, const std::string& value) {
	std::ofstream file(path, std::ios::binary);
	if (!file || !file.write(value.data(), value.size()))
		throw std::runtime_error("test fixture file write failed");
}
void Check(bool condition, const char* message) {
	if (!condition) throw std::runtime_error(message);
}
int Child(int argc, char** argv) {
	if (argc != 7 || std::string(argv[1]) != "-c" ||
		std::string(argv[3]) != "-s" || std::string(argv[5]) != "-u")
		return 91;
	const auto configuration = Read(argv[2]);
#ifndef _WIN32
	if (configuration.find("test-mode=ignore") != std::string::npos)
		std::signal(SIGTERM, SIG_IGN);
#endif
	Write(fs::path(argv[4]) / "received-arguments.txt",
		std::string(argv[2]) + "\n" + argv[4] + "\n" + argv[6] + "\n" + UTF8(fs::current_path()));
	std::puts("FujiNet test child started");
	std::fflush(stdout);
	if (configuration.find("test-mode=exit42") != std::string::npos) return 42;
	if (configuration.find("test-mode=loop") != std::string::npos) return 75;
	if (configuration.find("test-mode=restart-trigger") != std::string::npos) {
		const auto directory = fs::path(argv[4]);
		if (!fs::exists(directory / "restart-triggered")) {
			while (!fs::exists(directory / "restart-now")) SDL_Delay(5);
			Write(directory / "restart-triggered", "once");
			return 75;
		}
	} else if (configuration.find("test-mode=restart") != std::string::npos) {
		const auto marker = fs::path(argv[4]) / "restarted";
		if (!fs::exists(marker)) { Write(marker, "once"); return 75; }
		Write(fs::path(argv[4]) / "after-restart", "replacement started");
	}
	if (configuration.find("test-mode=flood") != std::string::npos) {
		const std::string block(8192, 'x');
		for (unsigned i = 0; i < 1152; ++i)
			std::fwrite(block.data(), 1, block.size(), stdout);
		std::puts("FLOOD END");
		return 0;
	}
	for (;;) SDL_Delay(10);
}
template<class Predicate>
void Pump(ATFujiNetPC& runtime, Predicate predicate, uint64_t timeout = 10000) {
	const auto start = SDL_GetTicks();
	while (SDL_GetTicks() - start < timeout) {
		runtime.Poll();
		if (predicate()) return;
		SDL_Delay(5);
	}
	throw std::runtime_error("timed out waiting for runtime fixture");
}
} // namespace

int main(int argc, char** argv) {
	if (!SDL_Init(0)) return 2;
	if (argc > 1 && std::string(argv[1]) == "-c") return Child(argc, argv);
	const fs::path root = fs::temp_directory_path() /
		("altirra FujiNet process " + std::to_string(SDL_GetPerformanceCounter()));
	int result = 0;
	try {
		const auto runtimeDirectory = root / "runtime with spaces";
		fs::create_directories(runtimeDirectory / "data");
		fs::create_directories(runtimeDirectory / "SD");
		Write(runtimeDirectory / "SD" / "seed.txt", "seeded SD contents");
		const auto executable = runtimeDirectory / fs::absolute(argv[0]).filename();
		fs::copy_file(fs::absolute(argv[0]), executable);
		const auto templateFile = runtimeDirectory / "fnconfig.ini";
		const std::string base = "[Host1]\ntype=TNFS\nname=original.example\n[BOIP]\nenabled=0\nhost=old\nport=123\n";
		Write(templateFile, base);
		ATFujiNetPCOptions options;
		options.executable = UTF8(executable);
		options.stateDirectory = UTF8(root / "private data with spaces");
		options.netsioPort = 29997;
		options.webPort = 18000;
		ATFujiNetPC runtime;
		Check(runtime.Start(options), "runtime start failed");
		const auto state = fs::path(options.stateDirectory);
		const auto received = state / "SD" / "received-arguments.txt";
		Pump(runtime, [&] { return fs::exists(received); });
		const auto arguments = Read(received);
		Check(arguments.find(options.stateDirectory) != std::string::npos,
			"private paths with spaces did not round trip");
		Check(arguments.find("http://127.0.0.1:18000") != std::string::npos,
			"private web endpoint not passed");
		Check(arguments.find(UTF8(runtimeDirectory)) != std::string::npos,
			"runtime working directory not set");
		Check(Read(state / "fnconfig.ini").find("port=29997") != std::string::npos,
			"BOIP listener port not applied");
		Check(Read(state / "SD" / "seed.txt") == "seeded SD contents", "SD seed missing");
		ATFujiNetPC otherOwner;
		const auto beforeLeaseAttempt = Read(state / "fnconfig.ini");
		Check(!otherOwner.Start(options) && !otherOwner.GetStatus().error.empty(),
			"two managed runtimes acquired the same state directory");
		Check(Read(state / "fnconfig.ini") == beforeLeaseAttempt,
			"a rejected second owner modified the live runtime configuration");
		runtime.Stop();
		Pump(runtime, [&] { return !runtime.GetStatus().running; });
		Check(otherOwner.Start(options), "state-directory lease was not released after stop");
		otherOwner.Shutdown();
		Check(Read(templateFile) == base, "the distribution config was modified");
		Write(state / "fnconfig.ini", base + "\n[Host2]\nname=user.example\n");
		Check(runtime.Start(options), "second runtime start failed");
		runtime.Shutdown();
		Check(Read(state / "fnconfig.ini").find("name=user.example") != std::string::npos,
			"private host slots were lost");
		Check(fs::is_regular_file(state / "fujinet-pc.previous.log"), "log was not rotated");

		Write(templateFile, ";test-mode=exit42\n" + base);
		options.stateDirectory = UTF8(root / "exit test");
		Check(runtime.Start(options), "exit fixture start failed");
		Pump(runtime, [&] { return !runtime.GetStatus().running; });
		Check(runtime.GetStatus().exitCode == 42 && !runtime.GetStatus().error.empty(),
			"unexpected runtime exit was not reported");

		Write(templateFile, ";test-mode=restart\n" + base);
		options.stateDirectory = UTF8(root / "restart test");
		Check(runtime.Start(options), "restart fixture start failed");
		const auto mainThread = std::this_thread::get_id();
		unsigned restartCallbacks = 0;
		const auto restartSD = fs::path(options.stateDirectory) / "SD";
		const auto restartDeadline = SDL_GetTicks() + 10000;
		while (!fs::exists(restartSD / "after-restart") && SDL_GetTicks() < restartDeadline) {
			runtime.Poll([&] {
				++restartCallbacks;
				Check(std::this_thread::get_id() == mainThread && !runtime.GetStatus().running,
					"restart preparation did not run on main after reaping the child");
				Check(!fs::exists(restartSD / "after-restart"), "replacement started before restart preparation");
				Check(!otherOwner.Start(options), "restart preparation released the state-directory lease");
			});
			SDL_Delay(5);
		}
		Check(restartCallbacks == 1 && fs::exists(restartSD / "after-restart")
			&& runtime.GetStatus().running, "restart preparation was not followed by one replacement");
		runtime.Shutdown();
		Check(!runtime.GetStatus().running, "shutdown left a child running");
		options.stateDirectory = UTF8(root / "restart callback failure test");
		Check(runtime.Start(options), "restart failure fixture start failed");
		const auto failureDeadline = SDL_GetTicks() + 10000;
		while (runtime.GetStatus().running && SDL_GetTicks() < failureDeadline) {
			runtime.Poll([] { throw std::runtime_error("fixture callback failure"); });
			SDL_Delay(5);
		}
		Check(!runtime.GetStatus().running && runtime.GetStatus().error.find("fixture callback failure") != std::string::npos,
			"restart preparation failure was not reported");
		Check(otherOwner.Start(options), "restart preparation failure leaked the state-directory lease");
		otherOwner.Shutdown();
		Write(templateFile, ";test-mode=loop\n" + base);
		options.stateDirectory = UTF8(root / "restart loop test");
		Check(runtime.Start(options), "restart-loop fixture start failed");
		Pump(runtime, [&] { return !runtime.GetStatus().running; });
		Check(runtime.GetStatus().restarts == 6 && !runtime.GetStatus().error.empty(),
			"a restart loop was not bounded");

		Write(templateFile, ";test-mode=flood\n" + base);
		options.stateDirectory = UTF8(root / "flood test");
		Check(runtime.Start(options), "flood fixture start failed");
		Pump(runtime, [&] { return !runtime.GetStatus().running; });
		Check(runtime.GetStatus().logTruncated, "runtime log cap was not enforced");
		Check(runtime.GetStatus().logTail.find("FLOOD END") != std::string::npos,
			"the process pipe stopped draining at the log cap");
		Check(fs::file_size(fs::path(options.stateDirectory) / "fujinet-pc.log") <= 8 * 1024 * 1024,
			"persistent runtime log exceeded its cap");
#ifndef _WIN32
		Write(templateFile, ";test-mode=ignore\n" + base);
		options.stateDirectory = UTF8(root / "force stop test");
		Check(runtime.Start(options), "ignore fixture start failed");
		Pump(runtime, [&] { return fs::exists(fs::path(options.stateDirectory) / "SD" / "received-arguments.txt"); });
		const auto start = SDL_GetTicks();
		runtime.Shutdown();
		Check(SDL_GetTicks() - start >= 1900 && SDL_GetTicks() - start < 4000 && !runtime.GetStatus().running,
			"shutdown did not force-stop a child ignoring SIGTERM");
#endif
		Write(templateFile, base);
		const auto relativeState = root / "relative state";
		options.stateDirectory = "relative state";
		const auto callerDirectory = fs::current_path();
		fs::current_path(root);
		const bool relativeStarted = runtime.Start(options);
		fs::current_path(callerDirectory);
		Check(relativeStarted, "relative state fixture start failed");
		Pump(runtime, [&] { return fs::exists(relativeState / "SD" / "received-arguments.txt")
			|| !runtime.GetStatus().running; });
		Check(fs::exists(relativeState / "SD" / "received-arguments.txt"),
			"relative state path was resolved against the child working directory");
		Check(Read(relativeState / "SD" / "received-arguments.txt").find(UTF8(relativeState))
			!= std::string::npos, "child did not receive absolute private paths");
		runtime.Shutdown();
		const auto nestedState = runtimeDirectory / "SD" / "private state";
		options.stateDirectory = UTF8(nestedState);
		Check(runtime.Start(options), "private state below the supplied SD failed to start");
		Pump(runtime, [&] { return fs::exists(nestedState / "SD" / "received-arguments.txt"); });
		Check(Read(nestedState / "SD" / "seed.txt") == "seeded SD contents",
			"nested private SD did not receive the release seed");
		Check(!fs::exists(nestedState / "SD" / "private state"),
			"SD seeding recursively copied its own private state directory");
		runtime.Shutdown();
		options.stateDirectory = UTF8(runtimeDirectory);
		Check(!runtime.Start(options), "distribution directory accepted as private state");
		Check(Read(templateFile) == base, "rejected private path rewrote the release config");
		Check(!fs::exists(runtimeDirectory / "runtime.lock"),
			"rejected private path created a lease in the release directory");
		options.stateDirectory = UTF8(runtimeDirectory / "SD");
		Check(!runtime.Start(options), "supplied SD accepted as its own private state");
		Check(Read(runtimeDirectory / "SD" / "seed.txt") == "seeded SD contents",
			"rejected private path changed the release SD");
		Check(!fs::exists(runtimeDirectory / "SD" / "runtime.lock"),
			"rejected private path created a lease in the release SD");
		options.executable = UTF8(root / "does not exist");
		Check(!runtime.Start(options) && !runtime.GetStatus().error.empty(),
			"missing runtime was silently accepted");
		std::puts("FujiNet-PC process tests passed");
	} catch (const std::exception& error) {
		std::fprintf(stderr, "%s\n", error.what());
		result = 1;
	}
	std::error_code ignored;
	fs::remove_all(root, ignored);
	SDL_Quit();
	return result;
}
