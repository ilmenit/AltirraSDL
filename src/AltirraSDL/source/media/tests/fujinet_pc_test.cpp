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
	if (configuration.find("test-mode=restart") != std::string::npos) {
		const auto marker = fs::path(argv[4]) / "restarted";
		if (!fs::exists(marker)) { Write(marker, "once"); return 75; }
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
		Pump(runtime, [&] { return runtime.GetStatus().restarts == 1 &&
			runtime.GetStatus().running; });
		runtime.Shutdown();
		Check(!runtime.GetStatus().running, "shutdown left a child running");
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
