// AltirraSDL fork: launch the user's extracted FujiNet-PC without a shell.
#include <stdafx.h>
#include "fujinet_pc.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace {
namespace fs = std::filesystem;
constexpr size_t kLogLimit = 8u * 1024u * 1024u;
constexpr size_t kLogTailLimit = 32768;

fs::path Path(const std::string& text) {
#ifdef __cpp_char8_t
	return fs::path(std::u8string((const char8_t*)text.data(), text.size()));
#else
	return fs::u8path(text);
#endif
}
std::string UTF8(const fs::path& path) {
	const auto text = path.u8string();
	return std::string((const char*)text.data(), text.size());
}
std::string LowerTrim(std::string text) {
	const auto first = text.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) return {};
	text = text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
	for (char& c : text) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
	return text;
}
std::string ReadConfig(const fs::path& path) {
	std::ifstream file(path, std::ios::binary);
	if (!file || fs::file_size(path) > 1024 * 1024)
		throw std::runtime_error("Cannot read FujiNet-PC configuration (maximum 1 MiB).");
	std::ostringstream contents;
	contents << file.rdbuf();
	if (file.bad()) throw std::runtime_error("Cannot read FujiNet-PC configuration.");
	return contents.str();
}

// Only our private copy is edited. Keep all host slots, disks, credentials,
// printer settings and other keys written by the FujiNet web interface.
std::string ConfigureBOIP(const std::string& original, uint16_t port) {
	std::istringstream input(original);
	std::string output, line;
	bool boip = false, found = false;
	while (std::getline(input, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		const auto normalized = LowerTrim(line);
		if (!normalized.empty() && normalized.front() == '[') {
			boip = normalized == "[boip]";
			output += line + "\n";
			if (boip) {
				found = true;
				output += "enabled=1\nhost=127.0.0.1\nport=" + std::to_string(port) + "\n";
			}
			continue;
		}
		if (boip) {
			const auto key = LowerTrim(line.substr(0, line.find('=')));
			if (key == "enabled" || key == "host" || key == "port") continue;
		}
		output += line + "\n";
	}
	if (!found)
		output += "\n[BOIP]\nenabled=1\nhost=127.0.0.1\nport=" + std::to_string(port) + "\n";
	return output;
}

void Prepare(const ATFujiNetPCOptions& options) {
	if (options.executable.empty() || options.stateDirectory.empty() ||
		!options.netsioPort || !options.webPort)
		throw std::runtime_error("Choose a FujiNet-PC executable and valid UDP/web ports.");
	const auto executable = Path(options.executable);
	const auto root = Path(options.stateDirectory);
	const auto runtime = executable.parent_path();
	if (!executable.is_absolute() || !fs::is_regular_file(executable))
		throw std::runtime_error("Choose the extracted FujiNet-PC executable, using an absolute path.");
	if (!fs::is_directory(runtime / "data"))
		throw std::runtime_error("The FujiNet-PC data folder must be beside its executable. Extract the complete release archive.");
	fs::create_directories(root);
	const auto sd = root / "SD";
	if (!fs::exists(sd)) {
		// Seed the runtime's supplied virtual SD card once. Never overwrite
		// the user's saved files on later launches or runtime upgrades. A
		// failed copy must not publish a half-initialised SD card.
		fs::path staging;
		const auto nonce = SDL_GetPerformanceCounter();
		for (unsigned attempt = 0; ; ++attempt) {
			staging = root / (".SD-initializing-" + std::to_string(nonce) +
				"-" + std::to_string(attempt));
			if (fs::create_directory(staging)) break;
		}
		try {
			if (fs::is_directory(runtime / "SD")) {
				for (const auto& entry : fs::recursive_directory_iterator(runtime / "SD")) {
					if (entry.is_symlink()) continue;
					const auto dest = staging / entry.path().lexically_relative(runtime / "SD");
					if (entry.is_directory()) fs::create_directories(dest);
					else if (entry.is_regular_file()) {
						fs::copy_file(entry.path(), dest, fs::copy_options::skip_existing);
						fs::permissions(dest, fs::perms::owner_write | fs::perms::owner_read,
							fs::perm_options::add);
					}
				}
			}
			std::error_code error;
			fs::rename(staging, sd, error);
			if (error && !fs::is_directory(sd))
				throw fs::filesystem_error("Cannot initialise FujiNet SD", staging, sd, error);
			fs::remove_all(staging, error);
		} catch (...) {
			std::error_code ignored;
			fs::remove_all(staging, ignored);
			throw;
		}
	}
	const auto config = root / "fnconfig.ini";
	const auto original = fs::exists(config) ? config : runtime / "fnconfig.ini";
	if (!fs::is_regular_file(original))
		throw std::runtime_error("The extracted runtime needs fnconfig.ini beside its executable for initial setup.");
	const auto contents = ConfigureBOIP(ReadConfig(original), options.netsioPort);
	const auto temporary = root / "fnconfig.ini.new";
	{
		std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
		if (!file || !file.write(contents.data(), contents.size()) || !file.flush())
			throw std::runtime_error("Cannot write the private FujiNet-PC configuration.");
	}
	if (!SDL_RenamePath(UTF8(temporary).c_str(), UTF8(config).c_str()))
		throw std::runtime_error(SDL_GetError());
}
} // namespace

struct ATFujiNetPC::Impl {
	ATFujiNetPCStatus status;
	ATFujiNetPCOptions options;
	SDL_Process* process = nullptr;
	SDL_IOStream* log = nullptr;
	size_t logBytes = 0;
	uint64_t stopAt = 0;
	uint64_t restartWindow = 0;
#ifdef _WIN32
	HANDLE lease = INVALID_HANDLE_VALUE;
#elif !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
	int lease = -1;
#endif
	void AcquireLease(const std::string& directory) {
		if (directory.empty()) throw std::runtime_error("The FujiNet data directory is unavailable.");
		fs::create_directories(Path(directory));
		const auto path = Path(directory) / "runtime.lock";
#ifdef _WIN32
		lease = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
			nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		const bool locked = lease != INVALID_HANDLE_VALUE;
#elif !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
		lease = open(UTF8(path).c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
		const bool locked = lease >= 0 && !flock(lease, LOCK_EX | LOCK_NB);
#else
		const bool locked = false;
#endif
		if (!locked) {
			ReleaseLease();
			throw std::runtime_error("Another managed FujiNet-PC runtime is using this data directory, or it cannot be locked.");
		}
	}
	void ReleaseLease() {
#ifdef _WIN32
		if (lease != INVALID_HANDLE_VALUE) CloseHandle(lease);
		lease = INVALID_HANDLE_VALUE;
#elif !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
		if (lease >= 0) close(lease);
		lease = -1;
#endif
		// Keep the inode/file in place. The operating system releases the
		// lock on close/crash; unlinking it could let two owners acquire
		// distinct locks for the same data directory.
	}

	void Drain() {
		if (!process) return;
		SDL_IOStream* output = SDL_GetProcessOutput(process);
		if (!output) return;
		char buffer[8192];
		// The process pipe is nonblocking. Bound per-frame work while
		// draining it even after the persistent log reaches its cap.
		for (unsigned i = 0; i < 32; ++i) {
			const auto n = SDL_ReadIO(output, buffer, sizeof buffer);
			if (!n) break;
			status.logTail.append(buffer, n);
			if (status.logTail.size() > kLogTailLimit)
				status.logTail.erase(0, status.logTail.size() - kLogTailLimit);
			if (log) {
				const auto available = std::min(n, kLogLimit - logBytes);
				if (available && SDL_WriteIO(log, buffer, available) != available) {
					status.error = "Cannot write the FujiNet-PC log.";
					SDL_CloseIO(log);
					log = nullptr;
				}
				logBytes += available;
				status.logTruncated |= available < n;
			}
		}
	}

	bool Spawn() {
		const auto runtime = Path(options.executable).parent_path();
		const std::string config = UTF8(Path(options.stateDirectory) / "fnconfig.ini");
		const std::string sd = UTF8(Path(options.stateDirectory) / "SD");
		const std::string web = "http://127.0.0.1:" + std::to_string(options.webPort);
		const char* args[] = { options.executable.c_str(), "-c", config.c_str(),
			"-s", sd.c_str(), "-u", web.c_str(), nullptr };
		const auto props = SDL_CreateProperties();
		if (!props) { status.error = SDL_GetError(); return false; }
		const bool configured =
			SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void*)args) &&
			SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING,
				UTF8(runtime).c_str()) &&
			SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL) &&
			SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP) &&
			SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
		if (configured) process = SDL_CreateProcessWithProperties(props);
		if (!process) status.error = SDL_GetError();
		SDL_DestroyProperties(props);
		status.running = process != nullptr;
		return status.running;
	}
	void Release() {
		if (process) SDL_DestroyProcess(process);
		process = nullptr;
		status.running = false;
		status.stopping = false;
	}
};

ATFujiNetPC::ATFujiNetPC() : mpImpl(new Impl) {}
ATFujiNetPC::~ATFujiNetPC() { Shutdown(); }
bool ATFujiNetPC::Available() {
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__) || defined(SDL_PLATFORM_IOS)
	return false;
#else
	return true;
#endif
}
const ATFujiNetPCStatus& ATFujiNetPC::GetStatus() const { return mpImpl->status; }

bool ATFujiNetPC::Start(const ATFujiNetPCOptions& options) {
	auto& p = *mpImpl;
	if (p.process) { p.status.error = "FujiNet-PC is already running."; return false; }
	p.status = {};
	if (!Available()) {
		p.status.error = "This platform connects to an external FujiNet; local process management is unavailable.";
		return false;
	}
	try {
		p.AcquireLease(options.stateDirectory);
		Prepare(options);
		p.options = options;
		const auto root = Path(options.stateDirectory);
		const auto log = root / "fujinet-pc.log";
		if (fs::exists(log) && !SDL_RenamePath(UTF8(log).c_str(),
			UTF8(root / "fujinet-pc.previous.log").c_str()))
			throw std::runtime_error(SDL_GetError());
		p.log = SDL_IOFromFile(UTF8(log).c_str(), "wb");
		if (!p.log) throw std::runtime_error(SDL_GetError());
		p.logBytes = 0;
		p.restartWindow = SDL_GetTicks();
		if (p.Spawn()) return true;
	} catch (const std::exception& error) {
		p.status.error = error.what();
	}
	if (p.log) SDL_CloseIO(p.log);
	p.log = nullptr;
	p.ReleaseLease();
	return false;
}
void ATFujiNetPC::Stop() {
	auto& p = *mpImpl;
	if (!p.process || p.status.stopping) return;
	p.status.stopping = true;
	p.stopAt = SDL_GetTicks();
	if (!SDL_KillProcess(p.process, false)) p.status.error = SDL_GetError();
}
void ATFujiNetPC::Poll() {
	auto& p = *mpImpl;
	if (!p.process) return;
	p.Drain();
	if (p.status.stopping && SDL_GetTicks() - p.stopAt >= 2000) {
		if (!SDL_KillProcess(p.process, true)) p.status.error = SDL_GetError();
		p.stopAt = SDL_GetTicks();
	}
	int exitCode = 0;
	if (!SDL_WaitProcess(p.process, false, &exitCode)) return;
	p.Drain();
	const bool stopped = p.status.stopping;
	p.Release();
	p.status.exitCode = exitCode;
	if (!stopped && exitCode == 75) {
		// The official launcher restarts on EX_TEMPFAIL (75), used after
		// web configuration changes. Avoid a faulty runtime restart loop.
		if (SDL_GetTicks() - p.restartWindow >= 60000) {
			p.restartWindow = SDL_GetTicks();
			p.status.restarts = 0;
		}
		if (++p.status.restarts <= 5) {
			if (p.Spawn()) return;
		} else {
			p.status.error = "FujiNet-PC repeatedly requested a restart. Check its log before starting again.";
		}
	} else if (!stopped && exitCode != 0) {
		p.status.error = "FujiNet-PC exited with code " + std::to_string(exitCode) + ". See the runtime log.";
	}
	if (p.log) SDL_CloseIO(p.log);
	p.log = nullptr;
	p.ReleaseLease();
}
void ATFujiNetPC::Shutdown() {
	if (!mpImpl) return;
	auto& p = *mpImpl;
	Stop();
	const uint64_t deadline = SDL_GetTicks() + 2200;
	while (p.process && SDL_GetTicks() < deadline) {
		Poll();
		if (p.process) SDL_Delay(10);
	}
	if (p.process) {
		SDL_KillProcess(p.process, true);
		p.Drain();
		SDL_WaitProcess(p.process, true, nullptr);
		p.Release();
	}
	if (p.log) SDL_CloseIO(p.log);
	p.log = nullptr;
	p.ReleaseLease();
}
