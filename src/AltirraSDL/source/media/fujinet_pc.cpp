// AltirraSDL fork: launch the user's extracted FujiNet-PC without a shell.
#include <stdafx.h>
#include "fujinet_pc.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
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
#if defined(__APPLE__)
#include <sys/sysctl.h>
#elif defined(__GLIBC__)
#include <gnu/libc-version.h>
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

// Loader failures are the usual first-run problem: the official builds link
// the system's OpenSSL/Expat (Linux) or Homebrew's OpenSSL (macOS).
std::string ExplainStartFailure(const std::string& log) {
	const auto library = [&](const char* marker) -> std::string {
		const auto at = log.find(marker);
		if (at == std::string::npos) return {};
		const auto begin = at + strlen(marker);
		const auto end = log.find_first_of(":\r\n", begin);
		auto text = log.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
		const auto first = text.find_first_not_of(' ');
		return first == std::string::npos ? std::string() : text.substr(first);
	};
	if (log.find("GLIBC_") != std::string::npos && log.find("not found") != std::string::npos)
		return " This FujiNet-PC build is too new for this Linux. Download the ubuntu-22.04 build instead.";
	auto missing = library("error while loading shared libraries:");
	if (!missing.empty()) {
		if (missing.find("libcrypto") != std::string::npos || missing.find("libssl") != std::string::npos)
			return " It needs OpenSSL 3 (" + missing + "). Install your distribution's OpenSSL 3 "
				"library package, for example libssl3 or openssl-libs.";
		return " It needs the system library " + missing + ". Install the package that provides it.";
	}
	missing = library("Library not loaded:");
	if (!missing.empty()) {
		const auto formula = missing.find("openssl@");
		if (formula != std::string::npos) {
			const auto name = missing.substr(formula, missing.find('/', formula) - formula);
			return " It needs Homebrew's " + name + ". Install Homebrew from https://brew.sh, then run \"brew install "
				+ name + "\" in Terminal.";
		}
		return " It needs " + missing + ", which is missing.";
	}
	return {};
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
				const auto privateRoot = fs::weakly_canonical(root);
				for (auto it = fs::recursive_directory_iterator(runtime / "SD");
					it != fs::recursive_directory_iterator(); ++it) {
					const auto& entry = *it;
					if (entry.is_symlink()) continue;
					// Private storage may be placed below the supplied SD. Do
					// not copy our lease/config/staging tree back into itself.
					if (entry.is_directory()
						&& fs::weakly_canonical(entry.path()) == privateRoot) {
						it.disable_recursion_pending();
						continue;
					}
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

ATFujiNetPCHost ATFujiNetPCDetectHost() {
	ATFujiNetPCHost host;
#if defined(__x86_64__) || defined(_M_X64)
	host.arch = ATFujiNetPCHost::Arch::X64;
#elif defined(__aarch64__) || defined(_M_ARM64)
	host.arch = ATFujiNetPCHost::Arch::Arm64;
#elif defined(__arm__)
	host.arch = ATFujiNetPCHost::Arch::Arm32;
#endif
#if defined(_WIN32)
	host.os = ATFujiNetPCHost::OS::Windows;
	if (host.arch == ATFujiNetPCHost::Arch::Arm64) {
		// IsWow64GuestMachineSupported exists from Windows 11, the first
		// ARM release able to run x64 programs.
		using Query = HRESULT (WINAPI*)(USHORT, BOOL*);
		const auto query = (Query)(void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
			"IsWow64GuestMachineSupported");
		BOOL supported = FALSE;
		host.windowsX64Emulation = query &&
			SUCCEEDED(query(IMAGE_FILE_MACHINE_AMD64, &supported)) && supported;
	}
#elif defined(__APPLE__)
	host.os = ATFujiNetPCHost::OS::MacOS;
	// An Intel build running under Rosetta should still get the native
	// Apple Silicon download.
	int translated = 0;
	size_t size = sizeof translated;
	if (!sysctlbyname("sysctl.proc_translated", &translated, &size, nullptr, 0) && translated)
		host.arch = ATFujiNetPCHost::Arch::Arm64;
	char version[32] = {};
	size = sizeof version - 1;
	if (!sysctlbyname("kern.osproductversion", version, &size, nullptr, 0))
		sscanf(version, "%d.%d", &host.versionMajor, &host.versionMinor);
	// Homebrew's prefix differs between Apple Silicon and Intel; the builds
	// link these exact paths.
	const char* prefix = host.arch == ATFujiNetPCHost::Arch::Arm64 ? "/opt/homebrew" : "/usr/local";
	std::error_code error;
	host.homebrewOpenSSL3 = fs::exists(fs::path(prefix) / "opt/openssl@3/lib/libcrypto.3.dylib", error);
	host.homebrewOpenSSL4 = fs::exists(fs::path(prefix) / "opt/openssl@4/lib/libcrypto.4.dylib", error);
#elif defined(__linux__) && !defined(__ANDROID__)
	host.os = ATFujiNetPCHost::OS::Linux;
#ifdef __GLIBC__
	sscanf(gnu_get_libc_version(), "%d.%d", &host.versionMajor, &host.versionMinor);
#endif
#endif
	return host;
}

ATFujiNetPCPackage ATFujiNetPCSelectPackage(const ATFujiNetPCHost& host) {
	using OS = ATFujiNetPCHost::OS;
	using Arch = ATFujiNetPCHost::Arch;
	ATFujiNetPCPackage package;
	const bool known = host.versionMajor > 0;
	const auto atLeast = [&](int major, int minor) {
		return host.versionMajor > major ||
			(host.versionMajor == major && host.versionMinor >= minor);
	};
	// Minimum versions come from the published binaries: macOS LC_BUILD_VERSION
	// minos, glibc symbol versions, and the Homebrew libcrypto each links.
	switch (host.os) {
	case OS::Windows:
		package.system = host.arch == Arch::Arm64 ? "Windows on ARM" : "Windows, 64-bit";
		if (host.arch == Arch::X64 || (host.arch == Arch::Arm64 && host.windowsX64Emulation))
			package.assetSuffixes = { "windows-x64.zip" };
		if (host.arch == Arch::Arm64)
			package.requirement = host.windowsX64Emulation ?
				"Windows runs this 64-bit Intel/AMD program through its built-in emulation." :
				"FujiNet-PC on Windows on ARM needs Windows 11, which can run 64-bit Intel/AMD programs.";
		else if (host.arch != Arch::X64)
			package.requirement = "Official FujiNet-PC builds need 64-bit Windows.";
		break;
	case OS::MacOS: {
		const auto brew = [](const char* formula) {
			return std::string("One-time step: install Homebrew from https://brew.sh, then run \"brew install ")
				+ formula + "\" in Terminal.";
		};
		if (host.arch == Arch::Arm64) {
			package.system = "macOS, Apple Silicon";
			if (!known || atLeast(15, 0)) {
				// The macOS 14 build (minimum 14.0) also runs on 15; choose
				// whichever OpenSSL the user already has to skip a step.
				if (host.homebrewOpenSSL3 && !host.homebrewOpenSSL4)
					package.assetSuffixes = { "macos-14-arm64.tar.gz", "macos-15-arm64.tar.gz" };
				else
					package.assetSuffixes = { "macos-15-arm64.tar.gz", "macos-14-arm64.tar.gz" };
				if (!host.homebrewOpenSSL3 && !host.homebrewOpenSSL4)
					package.requirement = brew("openssl@4");
			} else if (atLeast(14, 0)) {
				package.assetSuffixes = { "macos-14-arm64.tar.gz" };
				if (!host.homebrewOpenSSL3) package.requirement = brew("openssl@3");
			} else {
				package.requirement = "Official FujiNet-PC builds need macOS 14 or newer.";
			}
		} else {
			package.system = "macOS, Intel";
			if (host.arch == Arch::X64 && (!known || atLeast(15, 0))) {
				package.assetSuffixes = { "macos-15-x64.tar.gz" };
				if (!host.homebrewOpenSSL3) package.requirement = brew("openssl@3");
			} else {
				package.requirement = "Official FujiNet-PC builds for Intel Macs need macOS 15 or newer.";
			}
		}
		break;
	}
	case OS::Linux:
		package.system = host.arch == Arch::X64 ? "Linux, 64-bit Intel/AMD" :
			host.arch == Arch::Arm64 ? "Linux, 64-bit ARM" :
			host.arch == Arch::Arm32 ? "Linux, 32-bit ARM" : "Linux";
		if (!known) {
			// musl and other non-glibc systems cannot load these binaries.
			if (host.arch == Arch::X64) package.assetSuffixes = { "ubuntu-22.04-amd64.tar.gz" };
			package.requirement = "Official builds need a glibc-based Linux, as used by most distributions.";
		} else if (host.arch == Arch::X64 && atLeast(2, 38)) {
			// The 22.04 build needs only glibc 2.34; keep it as a fallback.
			package.assetSuffixes = { "ubuntu-24.04-amd64.tar.gz", "ubuntu-22.04-amd64.tar.gz" };
		} else if (host.arch == Arch::X64 && atLeast(2, 34)) {
			package.assetSuffixes = { "ubuntu-22.04-amd64.tar.gz" };
		} else if ((host.arch == Arch::Arm64 || host.arch == Arch::Arm32) && atLeast(2, 38)) {
			package.assetSuffixes = { host.arch == Arch::Arm64 ?
				"ubuntu-24.04-arm64.tar.gz" : "ubuntu-24.04-armhf.tar.gz" };
		} else if (host.arch == Arch::X64) {
			package.requirement = "Official builds need glibc 2.34 or newer (Ubuntu 22.04 or a similar Linux).";
		} else if (host.arch == Arch::Arm64 || host.arch == Arch::Arm32) {
			package.requirement = "Official ARM builds need glibc 2.38 or newer (Ubuntu 24.04 or a similar Linux).";
		} else {
			package.requirement = "No official FujiNet-PC build exists for this processor.";
		}
		break;
	default:
		package.system = "this system";
		package.requirement = "No official FujiNet-PC build exists for this system.";
		break;
	}
	if (!package.assetSuffixes.empty()) package.assetSuffix = package.assetSuffixes.front();
	return package;
}

std::string ATFujiNetPCCheckExecutable(const std::string& executable) {
	if (executable.empty()) return {};
	const auto path = Path(executable);
	const auto name = LowerTrim(UTF8(path.filename()));
	const auto endsWith = [&](const char* suffix) {
		const size_t n = strlen(suffix);
		return name.size() >= n && !name.compare(name.size() - n, n, suffix);
	};
	// The regular release's fujinet-ATARI-v*.zip is ESP32 firmware for the
	// physical device and is the download most often chosen by mistake.
	const auto parent = path.parent_path();
	std::error_code error;
	const bool firmwareFolder = fs::is_regular_file(parent / "firmware.bin", error) ||
		fs::is_regular_file(parent / "bootloader.bin", error) ||
		fs::is_regular_file(parent / "partitions.bin", error);
	if (firmwareFolder || endsWith(".bin") ||
		(!name.compare(0, 14, "fujinet-atari-") && endsWith(".zip")))
		return "This is firmware for a physical FujiNet device, not the FujiNet-PC program. "
			"Download the file whose name starts with fujinet-pc-ATARI from the FujiNet-PC "
			"Nightly release, extract it, and choose the fujinet program inside.";
	if (endsWith(".zip") || endsWith(".tar.gz") || endsWith(".tgz") || endsWith(".gz") || endsWith(".tar"))
		return "This is the downloaded archive. Extract (unzip) it first, then choose the "
			"fujinet program inside the extracted fujinet-pc-ATARI folder.";
	if (!name.compare(0, 11, "run-fujinet"))
		return "Choose the program named fujinet (fujinet.exe on Windows), not the run-fujinet script.";
	const auto folder = LowerTrim(UTF8(parent.filename()));
	if (!folder.compare(0, 11, "fujinet-pc-") && folder.compare(0, 16, "fujinet-pc-atari")) {
		std::string machine = UTF8(parent.filename()).substr(11);
		return "This FujiNet-PC is made for a different computer (" + machine +
			"). Download the ATARI version instead.";
	}
	if (!fs::is_regular_file(path, error))
		return "This file cannot be found. Choose the fujinet program again.";
	if (!fs::is_directory(parent / "data", error))
		return "The data folder is missing next to this file. Extract the complete archive "
			"and choose fujinet from the extracted folder.";
	const auto stem = LowerTrim(UTF8(path.stem()));
	if (stem.compare(0, 7, "fujinet") || (path.has_extension() && !endsWith(".exe")))
		return "Expected the program named fujinet (fujinet.exe on Windows) from the extracted folder.";
#ifndef _WIN32
	const auto perms = fs::status(path, error).permissions();
	if (!error && (perms & fs::perms::owner_exec) == fs::perms::none)
		return "This file is not marked as a program. Extract the archive again with its "
			"permissions, or run: chmod +x on the fujinet file.";
#endif
	return {};
}

ATFujiNetPC::ATFujiNetPC() : mpImpl(new Impl) {}
ATFujiNetPC::~ATFujiNetPC() { Shutdown(); }
bool ATFujiNetPC::Available() {
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__) || defined(SDL_PLATFORM_IOS)
	return false;
#else
	return true;
#endif
}
bool ATFujiNetPC::WithStateDirectoryLease(const std::string& directory,
	const std::function<void()>& action) {
	if (!Available()) return false;
	Impl maintenance;
	try {
		maintenance.AcquireLease(directory);
	} catch (const std::exception&) {
		return false;
	}
	struct Guard {
		Impl& owner;
		~Guard() { owner.ReleaseLease(); }
	} guard{maintenance};
	action();
	return true;
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
		// The child runs from its release directory. Resolve private paths
		// once against the caller's cwd so preparation, the lease and -c/-s
		// all refer to the same files, including with relative API inputs.
		ATFujiNetPCOptions prepared = options;
		if (!prepared.stateDirectory.empty())
			prepared.stateDirectory = UTF8(fs::absolute(Path(prepared.stateDirectory)));
		const auto root = Path(prepared.stateDirectory);
		const auto runtime = Path(prepared.executable).parent_path();
		if (fs::is_directory(root) && fs::is_directory(runtime) &&
			(fs::equivalent(root, runtime) || (fs::is_directory(runtime / "SD")
				&& fs::equivalent(root, runtime / "SD"))))
			throw std::runtime_error("Choose a private data directory separate "
				"from the FujiNet-PC release and its supplied SD directory.");
		p.AcquireLease(prepared.stateDirectory);
		Prepare(prepared);
		p.options = prepared;
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
void ATFujiNetPC::PollOutput() { mpImpl->Drain(); }
void ATFujiNetPC::Poll(const std::function<void()>& beforeRestart) {
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
			bool prepared = true;
			try {
				if (beforeRestart) beforeRestart();
			} catch (const std::exception& error) {
				p.status.error = "Unable to prepare FujiNet-PC restart: " + std::string(error.what());
				prepared = false;
			} catch (...) {
				p.status.error = "Unable to prepare FujiNet-PC restart: the restart callback failed.";
				prepared = false;
			}
			if (prepared && p.Spawn()) return;
		} else {
			p.status.error = "FujiNet-PC repeatedly requested a restart. Check its log before starting again.";
		}
	} else if (!stopped && exitCode != 0) {
		const auto reason = ExplainStartFailure(p.status.logTail);
		p.status.error = "FujiNet-PC exited with code " + std::to_string(exitCode) + "." +
			(reason.empty() ? std::string(" See the runtime log under Diagnostics.") : reason);
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
