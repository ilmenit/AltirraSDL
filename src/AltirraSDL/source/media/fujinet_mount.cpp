// Authorized fork extension: use the official FujiNet-PC mount API.
#include <stdafx.h>
#include "fujinet_mount.h"
#include "http_client.h"
#include <at/atcore/checksum.h>
#include <at/atio/diskimage.h>
#include <vd2/system/file.h>
#include <vd2/system/binary.h>
#include <vd2/system/refcount.h>
#include <vd2/system/text.h>
#include <vd2/vdjson/jsonreader.h>
#include <vd2/vdjson/jsonvalue.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>

namespace ATFujiNetMount {
namespace {
namespace fs = std::filesystem;
constexpr uint64_t kMaxImage = 64ULL * 1024 * 1024;
fs::path Native(const std::string& path) {
#if defined(__cpp_char8_t)
	return fs::path(std::u8string((const char8_t*)path.data(), path.size()));
#else
	return fs::u8path(path);
#endif
}
std::string UTF8(const fs::path& path) {
	const auto text = path.u8string();
	return std::string((const char*)text.data(), text.size());
}
VDStringW Wide(const fs::path& path) {
	const auto text = UTF8(path);
	return VDTextU8ToW(VDStringSpanA(text.c_str()));
}
void CheckCancel(const Progress& progress) {
	if (progress.cancel.load(std::memory_order_relaxed)) throw std::runtime_error("cancelled");
}
std::string Hex(const ATChecksumSHA256& hash) {
	constexpr char digits[] = "0123456789abcdef";
	std::string result;
	for (const auto byte : hash.mDigest) { result += digits[byte >> 4]; result += digits[byte & 15]; }
	return result;
}
std::string JSONString(const std::string& text) {
	std::string result = "\"";
	constexpr char digits[] = "0123456789abcdef";
	for (unsigned char c : text) {
		if (c == '\\' || c == '"') { result += '\\'; result += (char)c; }
		else if (c < 32) { result += "\\u00"; result += digits[c >> 4]; result += digits[c & 15]; }
		else result += (char)c;
	}
	return result + '"';
}
void Parse(const ATHttp::Response& response, VDJSONDocument& document,
	const ATHttp::Request& request) {
	if (!response.error.empty()) throw std::runtime_error("Cannot contact FujiNet at "
		+ request.url + ": " + response.error);
	if (response.status != 200)
		throw std::runtime_error("FujiNet request to " + request.url + " returned HTTP "
			+ std::to_string(response.status) + (response.status == 404
			? ". This may be another program's web server, or this runtime may lack the mount API. Try Repair automatic setup and check the runtime log."
			: ". Check the FujiNet runtime log and Web Interface for details."));
	VDJSONReader reader;
	if (response.body.empty() || !reader.Parse((const char*)response.body.data(), response.body.size(), document))
		throw std::runtime_error("The response from " + request.url
			+ " is not valid FujiNet mount API data. Check the runtime log or use Repair automatic setup.");
}
ATHttp::Request Request(unsigned port, const char* path, const Progress& progress) {
	ATHttp::Request request;
	request.url = "http://127.0.0.1:" + std::to_string(port) + path;
	request.userAgent = "AltirraSDL";
	request.timeoutMs = 10000;
	request.maxBodyBytes = 64 * 1024;
	request.cancel = &progress.cancel;
	return request;
}

// FujiNet's XEX media is a bootable disk implemented by PicoBoot. Its own
// loader handles segment framing and INIT-driven programs, so do not impose
// the general program-image parser's stricter full-file framing rules here.
std::string Validate(VDFileStream& input, const fs::path& source, Progress& progress) {
	const auto size = input.Length();
	if (size <= 0 || (uint64_t)size > kMaxImage)
		throw std::runtime_error("Choose an XEX, ATR or ATX image of 64 MiB or less.");
	uint8 header[4] {};
	input.Read(header, (sint32)std::min<sint64>(4, size));
	input.Seek(0);
	if (size >= 4 && ((header[0] == 0x96 && header[1] == 0x02) ||
		!memcmp(header, "AT8X", 4))) {
		vdrefptr<IATDiskImage> disk;
		const auto name = Wide(source);
		ATLoadDiskImage(name.c_str(), name.c_str(), input, ~disk);
		input.Seek(0);
		return header[0] == 0x96 ? ".atr" : ".atx";
	}
	if (header[0] != 0xFF || header[1] != 0xFF)
		throw std::runtime_error("FujiNet D1 boot supports XEX programs and ATR/ATX disks. Use an extracted image; cartridge ROMs and ZIP packs cannot be mounted here.");
	CheckCancel(progress);
	if (size < 6) throw std::runtime_error("The XEX header is truncated.");
	input.Seek(0);
	return ".xex";
}
std::string SafeLeaf(const fs::path& source, const std::string& extension) {
	auto name = UTF8(source.stem());
	for (char& c : name)
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == '-' || c == '_')) c = '_';
	if (name.size() > 70) name.resize(70);
	// Prefix also excludes Windows reserved names (CON, AUX, NUL, ...).
	return "game-" + name + extension;
}
bool Matches(const fs::path& file, const std::string& digest, Progress& progress) {
	if (!fs::is_regular_file(file) || fs::is_symlink(file) || fs::file_size(file) > kMaxImage) return false;
	std::ifstream input(file, std::ios::binary);
	ATChecksumEngineSHA256 hash;
	char buffer[32768];
	while (input) {
		CheckCancel(progress);
		input.read(buffer, sizeof buffer);
		hash.Process(buffer, (size_t)input.gcount());
	}
	return input.eof() && Hex(hash.Finalize()) == digest;
}
} // namespace

Result Run(const std::string& source, const std::string& stateDirectory,
	unsigned webPort, Progress& progress) {
	Result result;
	fs::path staging;
	fs::path probe;
	try {
		progress.phase = Phase::Preparing;
		progress.received = progress.total = 0;
		CheckCancel(progress);
		if (!webPort || webPort > 65535 || stateDirectory.empty())
			throw std::runtime_error("Start the managed FujiNet-PC runtime first.");
		const auto state = Native(stateDirectory);
		if (!fs::is_directory(state / "SD"))
			throw std::runtime_error("The managed FujiNet-PC virtual SD card is unavailable.");
		std::string local = source;
		if (ATMediaURL::IsURL(source)) {
			progress.phase = Phase::Downloading;
			auto download = ATMediaURL::Download(source, UTF8(state / "downloads"), progress);
			if (!download.error.empty()) throw std::runtime_error(download.error);
			local = std::move(download.path);
		}
		CheckCancel(progress);
		const auto inputPath = Native(local);
		VDFileStream input(Wide(inputPath).c_str());
		const auto extension = Validate(input, inputPath, progress);
		const auto leaf = SafeLeaf(inputPath, extension);
		const auto imports = state / "SD" / "AltirraSDL";
		if (fs::is_symlink(imports)) throw std::runtime_error("The private SD import directory must not be a symbolic link.");
		fs::create_directories(imports);
		const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
		for (unsigned attempt = 0;; ++attempt) {
			staging = imports / (".preparing-" + std::to_string(nonce) + "-" + std::to_string(attempt));
			if (fs::create_directory(staging)) break;
		}
		progress.phase = Phase::Copying;
		progress.received = 0;
		progress.total = (uint64_t)input.Length();
		ATChecksumEngineSHA256 hash;
		{
			VDFileStream output(Wide(staging / Native(leaf)).c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
			uint8 buffer[32768];
			for (;;) {
				CheckCancel(progress);
				const auto count = input.ReadData(buffer, sizeof buffer);
				if (!count) break;
				if (progress.received.load() + count > kMaxImage) throw std::runtime_error("The source image grew beyond 64 MiB while copying.");
				output.Write(buffer, count);
				hash.Process(buffer, count);
				progress.received.fetch_add(count);
			}
			output.flush();
		}
		if (progress.received.load() != progress.total.load())
			throw std::runtime_error("The source image changed while copying; retry with a stable file.");
		{
			VDFileStream copied(Wide(staging / Native(leaf)).c_str());
			if (Validate(copied, staging / Native(leaf), progress) != extension)
				throw std::runtime_error("The source image changed format while copying.");
		}
		const auto digest = Hex(hash.Finalize());
		const auto leafHash = Hex(ATComputeChecksumSHA256(leaf.data(), leaf.size()));
		std::string directory = digest + "-" + leafHash.substr(0, 16);
		auto destination = imports / directory;
		std::error_code publishError;
		fs::rename(staging, destination, publishError);
		if (!publishError) staging.clear();
		if (publishError && (fs::is_symlink(destination) || !Matches(destination / Native(leaf), digest, progress))) {
			// A previously mounted copy may have been edited. Preserve it
			// and publish a fresh copy at a new path; never overwrite it.
			directory += "-" + std::to_string(nonce);
			destination = imports / directory;
			fs::rename(staging, destination);
			staging.clear();
		}
		result.path = "/AltirraSDL/" + directory + "/" + leaf;
		CheckCancel(progress);
		progress.phase = Phase::CheckingHost;
		ATHttp::Response response;
		auto request = Request(webPort, "/api/v1/hosts", progress);
		ATHttp::Get(request, response);
		VDJSONDocument hosts;
		Parse(response, hosts, request);
		if (!hosts.Root().IsArray()) throw std::runtime_error("FujiNet returned an invalid host list.");
		bool found = false;
		for (const auto& host : hosts.Root().AsArray()) {
			const auto slot = host[L"slot"];
			if (host[L"type"].IsString() && !wcscmp(host[L"type"].AsString(), L"SD") &&
				host[L"configured"].AsBool() && slot.IsInt() && slot.AsInt64() >= 1 && slot.AsInt64() <= 8) {
				result.hostSlot = (unsigned)slot.AsInt64() - 1;
				found = true;
				break;
			}
		}
		if (!found) throw std::runtime_error("Add an SD host slot in FujiNet's Web Interface first. Existing host slots were kept.");
		CheckCancel(progress);
		// A connected NetSIO child can still fail to bind its web port if
		// another FujiNet-PC already owns it. Prove that this HTTP service
		// sees our private SD before sending any state-changing request.
		std::random_device random;
		std::string token;
		constexpr char digits[] = "0123456789abcdef";
		for (unsigned i = 0; i < 4; ++i) {
			const uint32_t word = random();
			for (unsigned shift = 0; shift < 32; shift += 4) token += digits[(word >> shift) & 15];
		}
		probe = imports / (".owner-" + token);
		if (!fs::create_directory(probe)) throw std::runtime_error("Unable to create a unique FujiNet SD ownership probe.");
		const std::string proof = "AltirraSDL managed SD " + token;
		{
			std::ofstream file(probe / "probe.txt", std::ios::binary);
			if (!file || !file.write(proof.data(), proof.size()) || !file.flush())
				throw std::runtime_error("Unable to write the FujiNet SD ownership probe.");
		}
		std::string encoded;
		ATHttp::UrlEncode("/AltirraSDL/.owner-" + token + "/probe.txt", encoded);
		const auto probeURL = "/download?hostslot=" + std::to_string(result.hostSlot) + "&filename=" + encoded;
		request = Request(webPort, probeURL.c_str(), progress);
		ATHttp::Get(request, response);
		CheckCancel(progress);
		if (!response.error.empty() || response.status != 200 ||
			std::string(response.body.begin(), response.body.end()) != proof)
			throw std::runtime_error("The web service at " + request.url
				+ " is not serving this managed runtime's SD card. Another program or FujiNet-PC may own the port. Try Repair automatic setup. No mount request was sent.");
		CheckCancel(progress);
		progress.phase = Phase::Mounting;
		request = Request(webPort, "/api/v1/drives/1/mount", progress);
		request.requestContentType = "application/json";
		request.requestBody = "{\"path\":" + JSONString(result.path) + ",\"host_slot\":" +
			std::to_string(result.hostSlot) + ",\"mode\":\"r\"}";
		result.mountAttempted = true;
		ATHttp::Post(request, response);
		VDJSONDocument mounted;
		Parse(response, mounted, request);
		if (!mounted.Root()[L"status"].IsString() || wcscmp(mounted.Root()[L"status"].AsString(), L"success"))
			throw std::runtime_error("FujiNet did not confirm mounting D1. Check its Web Interface.");
		CheckCancel(progress);
		progress.phase = Phase::Verifying;
		request = Request(webPort, "/api/v1/drives/1", progress);
		ATHttp::Get(request, response);
		VDJSONDocument drive;
		Parse(response, drive, request);
		const auto actual = drive.Root();
		const auto path = VDTextU8ToW(VDStringSpanA(result.path.c_str()));
		if (!actual[L"mounted"].AsBool() || !actual[L"path"].IsString() ||
			wcscmp(actual[L"path"].AsString(), path.c_str()) ||
			!actual[L"host_slot"].IsInt() || actual[L"host_slot"].AsInt64() != result.hostSlot ||
			!actual[L"mode"].IsString() || wcscmp(actual[L"mode"].AsString(), L"r"))
			throw std::runtime_error("FujiNet D1 changed before verification. Check its Web Interface.");
		CheckCancel(progress);
		result.mounted = true;
		progress.phase = Phase::Complete;
	} catch (const MyError& error) { result.error = error.c_str(); }
	catch (const std::exception& error) { result.error = error.what(); }
	catch (...) { result.error = "Unable to prepare this image for FujiNet."; }
	if (!result.mounted && result.mountAttempted)
		result.error += " FujiNet may already have changed D1; check its Web Interface before booting.";
	if (!staging.empty()) { std::error_code ignored; fs::remove_all(staging, ignored); }
	if (!probe.empty()) { std::error_code ignored; fs::remove_all(probe, ignored); }
	return result;
}
} // namespace ATFujiNetMount
