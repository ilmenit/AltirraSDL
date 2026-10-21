// Optional managed FujiNet-PC runtime. All methods run on the main thread.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct ATFujiNetPCOptions {
	std::string executable;
	std::string stateDirectory;
	uint16_t netsioPort = 9997;
	uint16_t webPort = 8000;
};

struct ATFujiNetPCStatus {
	bool running = false;
	bool stopping = false;
	bool logTruncated = false;
	int exitCode = 0;
	unsigned restarts = 0;
	std::string error;
	std::string logTail;
};

// Which official FujiNet-PC download suits a computer. The nightly release
// names its archives fujinet-pc-ATARI_<version>_<suffix>; the plain
// fujinet-ATARI-*.zip assets are ESP32 device firmware, not PC programs.
struct ATFujiNetPCHost {
	enum class OS { Windows, Linux, MacOS, Other } os = OS::Other;
	enum class Arch { X64, Arm64, Arm32, Other } arch = Arch::Other;
	int versionMajor = 0;	// macOS version, or glibc version on Linux
	int versionMinor = 0;	// 0/0 when unknown (e.g. non-glibc Linux)
	// macOS builds link Homebrew's libcrypto; prefer one already installed.
	bool homebrewOpenSSL3 = false;
	bool homebrewOpenSSL4 = false;
	// Windows on ARM runs x64 programs only from Windows 11 on.
	bool windowsX64Emulation = true;
};

struct ATFujiNetPCPackage {
	std::string system;			// e.g. "Linux, 64-bit Intel/AMD"
	std::string assetSuffix;	// preferred build; empty when none fits
	std::vector<std::string> assetSuffixes;	// all usable builds, preferred first
	std::string requirement;	// extra one-time step, may be empty
};

ATFujiNetPCHost ATFujiNetPCDetectHost();
ATFujiNetPCPackage ATFujiNetPCSelectPackage(const ATFujiNetPCHost& host);
// Plain-language reason a chosen file is not an extracted ATARI FujiNet-PC
// program, or empty when it looks right. Does not block selection.
std::string ATFujiNetPCCheckExecutable(const std::string& executable);

class ATFujiNetPC {
public:
	ATFujiNetPC();
	~ATFujiNetPC();
	ATFujiNetPC(const ATFujiNetPC&) = delete;
	ATFujiNetPC& operator=(const ATFujiNetPC&) = delete;
	static bool Available();
	bool Start(const ATFujiNetPCOptions& options);
	void Stop();
	// Called on the caller/main thread after the previous child exits and
	// before an exit-75 replacement starts; the data lease remains held.
	// The callback may reset transports, but must not reenter this controller.
	// A thrown callback aborts restart, reports the error and releases ownership.
	void Poll(const std::function<void()>& beforeRestart = {});
	// Drain output while a worker holds the managed runtime data lease.
	void PollOutput();
	void Shutdown();
	const ATFujiNetPCStatus& GetStatus() const;
private:
	struct Impl;
	std::unique_ptr<Impl> mpImpl;
};
