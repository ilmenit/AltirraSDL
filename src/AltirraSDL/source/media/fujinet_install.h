// AltirraSDL fork: one-click download and install of the official FujiNet-PC.
#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

struct ATFujiNetPCPackage;

namespace ATFujiNetInstall {

// GitHub's release list; the FujiNet-PC Nightly tag is renamed with each
// version bump, so the installer always searches instead of using a tag.
constexpr char kReleasesURL[] =
	"https://api.github.com/repos/FujiNetWIFI/fujinet-firmware/releases?per_page=20";

enum class Phase : int { Finding, Downloading, Verifying, Extracting, Complete };

struct Progress {
	std::atomic<bool> cancel{false};
	std::atomic<Phase> phase{Phase::Finding};
	std::atomic<uint64_t> received{0};
	std::atomic<uint64_t> total{0};
};

struct Asset {
	std::string name;		// fujinet-pc-ATARI_<version>_<suffix>
	std::string url;
	std::string release;	// release title, e.g. "FujiNet-PC Nightly 2026-10-21"
	std::string sha256;		// lower-case hex, empty when GitHub has none
	uint64_t id = 0;
	uint64_t size = 0;
};

// A library the program links that is not installed. Only Homebrew paths
// are checked; they are the macOS builds' sole external requirement.
struct MissingLibrary {
	std::string path;		// e.g. /opt/homebrew/opt/openssl@3/lib/libcrypto.3.dylib
	std::string formula;	// e.g. openssl@3
};

struct Result {
	std::string executable;	// selected fujinet program, empty on failure
	std::string version;	// asset name without archive extension
	std::string error;
	std::vector<MissingLibrary> missing;
};

// Newest release asset for the first matching suffix, in the given order of
// preference. Draft releases are ignored. Returns false when none matches.
bool SelectAsset(const char* json, size_t length, const std::vector<std::string>& suffixes,
	Asset& asset);

// Blocking; call on a worker thread. Installs into the private
// <installRoot>/<asset name>-<asset id>/; a complete install of the same
// asset is reused. Other versions are left for RemoveOtherVersions().
Result Install(const ATFujiNetPCPackage& package, const std::string& installRoot,
	Progress& progress, const std::string& releasesURL = kReleasesURL);

// Unpack a downloaded archive into an empty directory, rejecting absolute
// paths, parent references and links, and keeping executable permissions.
void Extract(const std::string& archive, const std::string& directory, Progress& progress);

// After switching to a new version: delete the other installed versions.
// Hold the runtime state lease throughout cleanup. If another instance is
// running FujiNet-PC, retain all versions until a later cleanup.
void RemoveOtherVersions(const std::string& installRoot,
	const std::string& keepExecutable, const std::string& stateDirectory);
// Remove interrupted staging folders older than an hour.
void RemoveStale(const std::string& installRoot);

std::vector<MissingLibrary> FindMissingLibraries(const std::string& executable);

// Shell command a user can paste into Terminal to add missing libraries,
// installing Homebrew first when it is absent.
std::string HomebrewCommand(const std::vector<MissingLibrary>& missing);

} // namespace ATFujiNetInstall
