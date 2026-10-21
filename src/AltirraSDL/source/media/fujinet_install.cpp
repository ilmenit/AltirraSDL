// AltirraSDL fork: one-click download and install of the official FujiNet-PC.
// Runs on a worker thread; touches only the network and its install folder.
#include <stdafx.h>
#include "fujinet_install.h"
#include "fujinet_pc.h"
#include "http_client.h"
#include <at/atcore/checksum.h>
#include <vd2/system/binary.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/text.h>
#include <vd2/system/zip.h>
#include <vd2/vdjson/jsonreader.h>
#include <vd2/vdjson/jsonvalue.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>

namespace ATFujiNetInstall {
namespace {
namespace fs = std::filesystem;

// Official archives are 3-6 MiB and expand to about 15 MiB; the bounds only
// stop a corrupt or hostile archive from filling the disk.
constexpr uint64_t kMaxArchiveBytes = 128u * 1024u * 1024u;
constexpr uint64_t kMaxExpandedBytes = 512u * 1024u * 1024u;
constexpr unsigned kMaxEntries = 20000;

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
std::string UTF8(const wchar_t* text) {
	return VDTextWToU8(VDStringSpanW(text)).c_str();
}
VDStringW Wide(const fs::path& path) {
	return VDTextU8ToW(VDStringSpanA(UTF8(path).c_str()));
}
void CheckCancel(const Progress& progress) {
	if (progress.cancel.load(std::memory_order_relaxed))
		throw std::runtime_error("cancelled");
}
bool EndsWith(const std::string& text, const std::string& suffix) {
	return text.size() >= suffix.size() &&
		!text.compare(text.size() - suffix.size(), suffix.size(), suffix);
}
std::string Stem(const std::string& name) {
	for (const char* extension : { ".tar.gz", ".tgz", ".zip" })
		if (EndsWith(name, extension)) return name.substr(0, name.size() - strlen(extension));
	return name;
}

// Archive member names come from the network. Only plain relative paths
// below the destination are accepted.
fs::path SafeRelative(std::string name) {
	std::replace(name.begin(), name.end(), '\\', '/');
	if (name.empty() || name[0] == '/' || (name.size() > 1 && name[1] == ':'))
		throw std::runtime_error("The FujiNet-PC archive contains an absolute path.");
	fs::path result;
	size_t start = 0;
	while (start <= name.size()) {
		size_t end = name.find('/', start);
		if (end == std::string::npos) end = name.size();
		const auto part = name.substr(start, end - start);
		if (part == "..")
			throw std::runtime_error("The FujiNet-PC archive contains a parent-folder path.");
		// "D:x" in a later component would re-root the path on Windows, and
		// Windows strips trailing dots/spaces, turning "... " into "..".
		if (part.find(':') != std::string::npos ||
			(!part.empty() && part.find_first_not_of(". ") == std::string::npos && part != "."))
			throw std::runtime_error("The FujiNet-PC archive contains an invalid path.");
		if (!part.empty() && part != ".") result /= Path(part);
		start = end + 1;
	}
	if (result.has_root_name() || result.has_root_directory())
		throw std::runtime_error("The FujiNet-PC archive contains an absolute path.");
	return result;
}
// Antivirus scanners briefly hold freshly written programs open, which makes
// renaming their folder fail on Windows; retry for a few seconds.
void RenameWithRetry(const fs::path& from, const fs::path& to, std::error_code& error) {
	for (unsigned attempt = 0; ; ++attempt) {
		fs::rename(from, to, error);
		if (!error || attempt >= 20 || fs::exists(to)) return;
		std::this_thread::sleep_for(std::chrono::milliseconds(150));
	}
}

class Writer {
public:
	Writer(const fs::path& root, Progress& progress) : mRoot(root), mProgress(progress) {}
	void Directory(const std::string& name) {
		const auto relative = SafeRelative(name);
		if (!relative.empty()) fs::create_directories(mRoot / relative);
		Count(0);
	}
	template<class Read>
	void File(const std::string& name, uint64_t size, bool executable, Read&& read) {
		const auto relative = SafeRelative(name);
		if (relative.empty()) throw std::runtime_error("The FujiNet-PC archive contains an unnamed file.");
		Count(size);
		const auto target = mRoot / relative;
		fs::create_directories(target.parent_path());
		std::ofstream file(target, std::ios::binary | std::ios::trunc);
		if (!file) throw std::runtime_error("Unable to write " + UTF8(relative) + ".");
		char buffer[65536];
		for (uint64_t offset = 0; offset < size;) {
			CheckCancel(mProgress);
			const size_t count = (size_t)std::min<uint64_t>(sizeof buffer, size - offset);
			read(buffer, count);
			if (!file.write(buffer, (std::streamsize)count))
				throw std::runtime_error("Unable to write " + UTF8(relative) + ".");
			offset += count;
		}
		file.close();
		if (!file) throw std::runtime_error("Unable to write " + UTF8(relative) + ".");
#ifndef _WIN32
		if (executable)
			fs::permissions(target, fs::perms::owner_exec | fs::perms::group_exec |
				fs::perms::others_exec, fs::perm_options::add);
#else
		(void)executable;
#endif
	}
private:
	void Count(uint64_t size) {
		mBytes += size;
		if (++mEntries > kMaxEntries || mBytes > kMaxExpandedBytes)
			throw std::runtime_error("The FujiNet-PC archive is unexpectedly large.");
	}
	fs::path mRoot;
	Progress& mProgress;
	uint64_t mBytes = 0;
	unsigned mEntries = 0;
};

void ExtractZip(const fs::path& archive, Writer& writer, Progress& progress) {
	VDFileStream input(Wide(archive).c_str());
	VDZipArchive zip;
	zip.Init(&input);
	for (sint32 i = 0; i < zip.GetFileCount(); ++i) {
		CheckCancel(progress);
		const auto& info = zip.GetFileInfo(i);
		const std::string name = UTF8(info.mDecodedFileName.c_str());
		if (!name.empty() && (name.back() == '/' || name.back() == '\\')) {
			writer.Directory(name);
			continue;
		}
		if (!info.mbSupported)
			throw std::runtime_error("The FujiNet-PC archive uses an unsupported compression method.");
		// OpenDecodedStream enables CRC checking against the directory.
		vdautoptr<IVDInflateStream> decoded(zip.OpenDecodedStream(i, true));
		writer.File(name, info.mUncompressedSize, false, [&](char* buffer, size_t count) {
			decoded->Read(buffer, (sint32)count);
		});
		uint8_t extra;
		if (decoded->ReadData(&extra, 1))
			throw std::runtime_error("A FujiNet-PC archive member is longer than its directory entry.");
		decoded->VerifyCRC();
	}
}

uint64_t Octal(const uint8_t* field, size_t length) {
	if (field[0] & 0x80) throw std::runtime_error("The FujiNet-PC archive uses an unsupported tar size.");
	uint64_t value = 0;
	size_t i = 0;
	while (i < length && field[i] == ' ') ++i;
	for (; i < length && field[i] >= '0' && field[i] <= '7'; ++i) value = value * 8 + (field[i] - '0');
	return value;
}
std::string Field(const uint8_t* field, size_t length) {
	return std::string((const char*)field, strnlen((const char*)field, length));
}

void ExtractTarGz(const fs::path& archive, Writer& writer, Progress& progress) {
	VDFileStream input(Wide(archive).c_str());
	const sint64 length = input.Length();
	if (length < 18) throw std::runtime_error("The downloaded FujiNet-PC archive is truncated.");
	uint8_t trailer[8];
	input.Seek(length - 8);
	input.Read(trailer, 8);
	input.Seek(0);
	VDGUnzipStream gzip(&input, (uint64)length);
	gzip.EnableCRC();
	gzip.SetExpectedCRC(VDReadUnalignedLEU32(trailer));
	// Bound every decompressed byte, including skipped special members,
	// metadata and trailing blocks. Writer only counts extracted files.
	uint64_t expanded = 0;
	const auto readSome = [&](void* buffer, sint32 count) {
		CheckCancel(progress);
		const sint32 n = gzip.ReadData(buffer, count);
		if (n > 0) {
			expanded += (uint64_t)n;
			if (expanded > kMaxExpandedBytes)
				throw std::runtime_error("The FujiNet-PC archive is unexpectedly large.");
		}
		return n;
	};
	const auto read = [&](void* buffer, size_t count) {
		size_t done = 0;
		while (done < count) {
			const sint32 n = readSome((char*)buffer + done, (sint32)(count - done));
			if (n <= 0) throw std::runtime_error("The FujiNet-PC archive ends unexpectedly.");
			done += (size_t)n;
		}
	};
	const auto skip = [&](uint64_t count) {
		char buffer[4096];
		while (count) {
			const size_t n = (size_t)std::min<uint64_t>(sizeof buffer, count);
			read(buffer, n);
			count -= n;
		}
	};
	const auto padding = [](uint64_t size) { return (512 - size % 512) % 512; };
	std::string longName;
	for (;;) {
		CheckCancel(progress);
		uint8_t header[512];
		read(header, sizeof header);
		if (std::all_of(header, header + 512, [](uint8_t c) { return !c; })) break;
		// The checksum sums the header with its own field read as spaces.
		unsigned sum = 0;
		for (unsigned i = 0; i < 512; ++i) sum += (i >= 148 && i < 156) ? ' ' : header[i];
		if (sum != Octal(header + 148, 8))
			throw std::runtime_error("The FujiNet-PC archive is damaged (tar header checksum).");
		const uint64_t size = Octal(header + 124, 12);
		if (size > kMaxExpandedBytes - expanded)
			throw std::runtime_error("The FujiNet-PC archive is unexpectedly large.");
		const char type = (char)header[156];
		std::string name = Field(header, 100);
		// POSIX ustar ("ustar\0" + "00") has a name prefix; the older GNU
		// header ("ustar  \0") keeps times in those bytes instead.
		if (!memcmp(header + 257, "ustar\0" "00", 8)) {
			const auto prefix = Field(header + 345, 155);
			if (!prefix.empty()) name = prefix + "/" + name;
		}
		if (!longName.empty()) { name = std::move(longName); longName.clear(); }
		if (type == 'L' || type == 'x') {
			// GNU long names and pax "path" records rename the next member.
			if (size > 65536) throw std::runtime_error("The FujiNet-PC archive has an oversized header.");
			std::string data((size_t)size, '\0');
			read(data.data(), data.size());
			skip(padding(size));
			if (type == 'L') {
				longName = data.substr(0, strnlen(data.c_str(), data.size()));
			} else {
				for (size_t at = 0; at < data.size();) {
					const auto space = data.find(' ', at);
					const size_t recordLength = space == std::string::npos ? 0 :
						(size_t)strtoul(data.c_str() + at, nullptr, 10);
					if (!recordLength || at + recordLength > data.size()) break;
					const auto record = data.substr(space + 1, at + recordLength - space - 2);
					if (!record.compare(0, 5, "path=")) longName = record.substr(5);
					at += recordLength;
				}
			}
			continue;
		}
		if (type == '5') {
			writer.Directory(name);
		} else if (type == '0' || type == '\0' || type == '7') {
			const bool executable = (Octal(header + 100, 8) & 0111) != 0;
			writer.File(name, size, executable, read);
			skip(padding(size));
			continue;
		}
		// Links and special files are not needed and could point outside
		// the destination, so their data (if any) is skipped.
		skip(size + padding(size));
	}
	// Consume the remaining blocks so the gzip CRC and length are verified.
	char rest[4096];
	while (readSome(rest, sizeof rest) > 0) {}
	gzip.VerifyCRC();
}

// The extracted release keeps fujinet beside data/ inside its top folder.
fs::path FindExecutable(const fs::path& root) {
	std::error_code error;
	for (auto it = fs::recursive_directory_iterator(root, error);
		!error && it != fs::recursive_directory_iterator(); it.increment(error)) {
		if (it.depth() > 3) { it.disable_recursion_pending(); continue; }
		const auto name = UTF8(it->path().filename());
		if ((name == "fujinet" || name == "fujinet.exe") && it->is_regular_file() &&
			fs::is_directory(it->path().parent_path() / "data"))
			return it->path();
	}
	return {};
}

// One attempt; returns an error worth retrying (connection dropped or
// stalled, server error, short file), or empty on success. Permanent
// failures throw.
std::string DownloadOnce(const Asset& asset, const fs::path& target, Progress& progress) {
	std::ofstream file(target, std::ios::binary | std::ios::trunc);
	if (!file) throw std::runtime_error("Unable to save the FujiNet-PC download.");
	ATChecksumEngineSHA256 sha;
	ATHttp::Request request;
	request.url = asset.url;
	request.userAgent = "AltirraSDL";
	request.timeoutMs = 600000;
	request.maxBodyBytes = kMaxArchiveBytes;
	request.cancel = &progress.cancel;
	request.bytesReceived = &progress.received;
	request.bytesTotal = &progress.total;
	request.bodySink = [&](const void* data, size_t size) {
		sha.Process(data, size);
		return (bool)file.write((const char*)data, (std::streamsize)size);
	};
	ATHttp::Response response;
	ATHttp::Get(request, response);
	CheckCancel(progress);
	if (!response.error.empty()) return response.error;
	if (response.status >= 500) return "HTTP " + std::to_string(response.status);
	if (response.status != 200)
		throw std::runtime_error("Downloading FujiNet-PC failed: HTTP " + std::to_string(response.status) + ".");
	file.close();
	if (!file) throw std::runtime_error("Unable to save the FujiNet-PC download.");
	if (asset.size && fs::file_size(target) != asset.size) return "the file was incomplete";
	progress.phase = Phase::Verifying;
	if (!asset.sha256.empty()) {
		const auto digest = sha.Finalize();
		static constexpr char hex[] = "0123456789abcdef";
		std::string text;
		for (uint8_t c : digest.mDigest) { text += hex[c >> 4]; text += hex[c & 15]; }
		if (text != asset.sha256)
			throw std::runtime_error("The FujiNet-PC download does not match its published checksum. Try again.");
	}
	return {};
}
// A few megabytes are cheap to fetch again, so a dropped or stalled transfer
// (WinHTTP gives up after a 2 s receive stall) restarts from the beginning.
void Download(const Asset& asset, const fs::path& target, Progress& progress) {
	constexpr unsigned kAttempts = 3;
	for (unsigned attempt = 1; ; ++attempt) {
		progress.phase = Phase::Downloading;
		progress.received = 0;
		const auto error = DownloadOnce(asset, target, progress);
		if (error.empty()) return;
		if (attempt == kAttempts)
			throw std::runtime_error("Downloading FujiNet-PC failed: " + error +
				". Check the internet connection and try again.");
		for (unsigned wait = 0; wait < 10; ++wait) {
			CheckCancel(progress);
			std::this_thread::sleep_for(std::chrono::milliseconds(100 * attempt));
		}
	}
}
} // namespace

void RemoveStale(const std::string& installRoot) {
	// Staging and retired folders older than an hour belong to an install
	// that was interrupted (or to a version a scanner kept locked).
	std::error_code error;
	const auto now = fs::file_time_type::clock::now();
	fs::directory_iterator it(Path(installRoot), error), end;
	for (; !error && it != end; it.increment(error)) {
		const auto name = UTF8(it->path().filename());
		if (name.compare(0, 2, ".i") && name.compare(0, 5, ".old-")) continue;
		std::error_code timeError;
		const auto written = fs::last_write_time(it->path(), timeError);
		if (!timeError && now - written > std::chrono::hours(1)) {
			std::error_code ignored;
			fs::remove_all(it->path(), ignored);
		}
	}
}

void RemoveOtherVersions(const std::string& installRoot, const std::string& keepExecutable) {
	const auto root = Path(installRoot);
	const auto keep = Path(keepExecutable);
	std::error_code error;
	std::vector<fs::path> retired;
	fs::directory_iterator it(root, error), end;
	for (; !error && it != end; it.increment(error)) {
		const auto& path = it->path();
		if (UTF8(path.filename()).compare(0, 17, "fujinet-pc-ATARI_")) continue;
		const auto relative = keep.lexically_relative(path);
		if (!relative.empty() && relative.begin()->string() != "..") continue;
		retired.push_back(path);
	}
	for (size_t i = 0; i < retired.size(); ++i) {
		// Renaming first fails while another AltirraSDL runs that version
		// on Windows, leaving it intact instead of half deleted.
		std::error_code renamed;
		const auto trash = root / (".old-" + std::to_string(i) + "-" +
			std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		fs::rename(retired[i], trash, renamed);
		if (renamed) continue;
		std::error_code ignored;
		fs::remove_all(trash, ignored);
	}
}

bool SelectAsset(const char* json, size_t length, const std::vector<std::string>& suffixes,
	Asset& asset) {
	VDJSONDocument document;
	VDJSONReader reader;
	if (!reader.Parse(json, length, document) || !document.Root().IsArray()) return false;
	for (const auto& release : document.Root().AsArray()) {
		if (release[L"draft"].AsBool()) continue;
		for (const auto& suffix : suffixes) {
			for (const auto& candidate : release[L"assets"].AsArray()) {
				const auto name = UTF8(candidate[L"name"].AsString());
				// Only the ATARI PC build; the version sits between the prefix
				// and the platform suffix.
				if (name.compare(0, 17, "fujinet-pc-ATARI_") || !EndsWith(name, "_" + suffix))
					continue;
				// The name becomes a folder name; allow only plain characters.
				if (name.find("..") != std::string::npos || !std::all_of(name.begin(), name.end(),
					[](char c) { return isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-'; }))
					continue;
				const auto url = UTF8(candidate[L"browser_download_url"].AsString());
				if (url.empty()) continue;
				asset = {};
				asset.name = name;
				asset.url = url;
				asset.release = UTF8(release[L"name"].AsString());
				asset.id = (uint64_t)candidate[L"id"].AsInt64();
				asset.size = (uint64_t)candidate[L"size"].AsInt64();
				auto digest = UTF8(candidate[L"digest"].AsString());
				if (!digest.compare(0, 7, "sha256:")) {
					digest = digest.substr(7);
					for (char& c : digest) if (c >= 'A' && c <= 'F') c += 'a' - 'A';
					if (digest.size() == 64) asset.sha256 = digest;
				}
				return true;
			}
		}
	}
	return false;
}

void Extract(const std::string& archive, const std::string& directory, Progress& progress) {
	const auto source = Path(archive);
	const auto name = UTF8(source.filename());
	Writer writer(Path(directory), progress);
	if (EndsWith(name, ".zip")) ExtractZip(source, writer, progress);
	else if (EndsWith(name, ".tar.gz") || EndsWith(name, ".tgz")) ExtractTarGz(source, writer, progress);
	else throw std::runtime_error("Unsupported FujiNet-PC archive type.");
}

std::vector<MissingLibrary> FindMissingLibraries(const std::string& executable) {
	std::vector<MissingLibrary> missing;
	std::ifstream file(Path(executable), std::ios::binary | std::ios::ate);
	if (!file) return missing;
	const auto size = (uint64_t)file.tellg();
	if (size > kMaxArchiveBytes) return missing;
	std::string data((size_t)size, '\0');
	file.seekg(0);
	if (!file.read(data.data(), (std::streamsize)data.size())) return missing;
	for (const char* prefix : { "/opt/homebrew/opt/", "/usr/local/opt/" }) {
		for (size_t at = data.find(prefix); at != std::string::npos; at = data.find(prefix, at + 1)) {
			size_t end = at;
			while (end < data.size() && (unsigned char)data[end] > 32 && (unsigned char)data[end] < 127) ++end;
			const auto path = data.substr(at, end - at);
			if (!EndsWith(path, ".dylib")) continue;
			const auto begin = strlen(prefix);
			const auto formula = path.substr(begin, path.find('/', begin) - begin);
			std::error_code error;
			if (fs::exists(Path(path), error)) continue;
			if (std::none_of(missing.begin(), missing.end(),
				[&](const MissingLibrary& m) { return m.path == path; }))
				missing.push_back({ path, formula });
		}
	}
	return missing;
}

std::string HomebrewCommand(const std::vector<MissingLibrary>& missing) {
	std::string formulas;
	for (const auto& library : missing)
		if (formulas.find(library.formula) == std::string::npos)
			formulas += (formulas.empty() ? "" : " ") + library.formula;
	if (formulas.empty()) return {};
	std::error_code error;
	if (fs::exists("/opt/homebrew/bin/brew", error) || fs::exists("/usr/local/bin/brew", error))
		return "brew install " + formulas;
	// Homebrew's official installer; it explains itself and asks for the
	// password in Terminal. shellenv makes brew usable in the same session.
	return "/bin/bash -c \"$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
		" && eval \"$(/opt/homebrew/bin/brew shellenv 2>/dev/null || /usr/local/bin/brew shellenv)\""
		" && brew install " + formulas;
}

Result Install(const ATFujiNetPCPackage& package, const std::string& installRoot,
	Progress& progress, const std::string& releasesURL) {
	Result result;
	fs::path staging;
	try {
		if (package.assetSuffixes.empty())
			throw std::runtime_error(package.requirement.empty() ?
				"There is no official FujiNet-PC build for " + package.system + "." : package.requirement);
		if (!ATHttp::Available())
			throw std::runtime_error(std::string("This build cannot download files (") +
				ATHttp::BackendName() + "). Install libcurl, or download FujiNet-PC manually.");
		if (installRoot.empty()) throw std::runtime_error("The AltirraSDL settings folder is unavailable.");
		progress.phase = Phase::Finding;
		ATHttp::Request request;
		request.url = releasesURL;
		request.userAgent = "AltirraSDL";
		request.timeoutMs = 30000;
		request.maxBodyBytes = 16u * 1024u * 1024u;
		request.cancel = &progress.cancel;
		ATHttp::Response response;
		ATHttp::Get(request, response);
		CheckCancel(progress);
		if (!response.error.empty())
			throw std::runtime_error("Could not reach the FujiNet download site: " + response.error +
				". Check the internet connection.");
		if (response.status == 403 || response.status == 429)
			throw std::runtime_error("GitHub is limiting requests from this network. Try again in an hour, "
				"or download FujiNet-PC manually.");
		if (response.status != 200)
			throw std::runtime_error("The FujiNet download site returned HTTP " +
				std::to_string(response.status) + ".");
		Asset asset;
		if (!SelectAsset((const char*)response.body.data(), response.body.size(), package.assetSuffixes, asset))
			throw std::runtime_error("The FujiNet download site has no FujiNet-PC build for " +
				package.system + " right now. Try again later, or choose a FujiNet-PC you already have.");
		// The release list's integrity covers the checksum; keep the archive
		// on the same transport security.
		if (!releasesURL.compare(0, 8, "https://") && asset.url.compare(0, 8, "https://"))
			throw std::runtime_error("The FujiNet download site returned an insecure download link.");
		const auto root = Path(installRoot);
		const auto destination = root / Path(Stem(asset.name) + "-" + std::to_string(asset.id));
		result.version = Stem(asset.name);
		fs::create_directories(root);
		RemoveStale(installRoot);
		auto executable = FindExecutable(destination);
		if (executable.empty()) {
			// Short names keep deep release paths under Windows' 260-character
			// limit; exclusive creation keeps concurrent installers apart.
			for (unsigned attempt = 0; ; ++attempt) {
				staging = root / (".i" + std::to_string(attempt));
				if (fs::create_directory(staging)) break;
			}
			progress.phase = Phase::Downloading;
			const auto archive = staging / Path(asset.name);
			Download(asset, archive, progress);
			CheckCancel(progress);
			progress.phase = Phase::Extracting;
			const auto unpacked = staging / "c";
			fs::create_directory(unpacked);
			Extract(UTF8(archive), UTF8(unpacked), progress);
			if (FindExecutable(unpacked).empty())
				throw std::runtime_error("The downloaded archive does not contain the fujinet program.");
			CheckCancel(progress);
			// Publish only a complete folder. A leftover partial folder from
			// an interrupted older version has no program and is replaced.
			std::error_code ignored;
			if (FindExecutable(destination).empty()) fs::remove_all(destination, ignored);
			std::error_code renamed;
			RenameWithRetry(unpacked, destination, renamed);
			// Another AltirraSDL may have published the same version first.
			executable = FindExecutable(destination);
			if (executable.empty())
				throw std::runtime_error("Unable to move FujiNet-PC into place. Close programs "
					"that may be using the AltirraSDL settings folder and try again.");
		}
		const auto problem = ATFujiNetPCCheckExecutable(UTF8(executable));
		if (!problem.empty()) throw std::runtime_error(problem);
		result.executable = UTF8(executable);
		result.missing = FindMissingLibraries(result.executable);
		progress.phase = Phase::Complete;
	} catch (const MyError& error) {
		result.error = error.c_str();
	} catch (const std::exception& error) {
		result.error = error.what();
	}
	if (result.error == "cancelled") result.error = "FujiNet-PC installation was cancelled.";
	if (!result.error.empty()) result.executable.clear();
	if (!staging.empty()) {
		std::error_code ignored;
		fs::remove_all(staging, ignored);
	}
	return result;
}

} // namespace ATFujiNetInstall
