// AltirraSDL fork: durable, streamed Game Library URL imports.
#include <stdafx.h>
#include "library_url.h"
#include "http_client.h"
#include "media_url.h"
#include "library_extensions.h"
#include <at/atcore/checksum.h>
#include <at/atio/image.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/text.h>
#include <vd2/system/zip.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <vector>

namespace ATLibraryURL {
namespace {
namespace fs = std::filesystem;
constexpr uint64_t kMaxGameBytes = 64ULL * 1024 * 1024;
fs::path Native(const std::string& path) {
#if defined(__cpp_char8_t)
	return fs::path(std::u8string(path.begin(), path.end()));
#else
	return fs::u8path(path);
#endif
}
VDStringW Wide(const fs::path& path) {
	const auto bytes = path.u8string();
	return VDTextU8ToW(VDStringSpanA((const char*)bytes.data(),
		(const char*)bytes.data() + bytes.size()));
}
bool Cancelled(const Progress& progress) { return progress.cancel.load(); }
std::string Name(const std::string& url) {
	const auto end = url.find_first_of("?#");
	const auto path = url.substr(0, end);
	const auto slash = path.find_last_of('/');
	std::string name = path.substr(slash == std::string::npos ? 0 : slash + 1);
	const auto nibble = [](char c) -> int {
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'A' && c <= 'F') return c - 'A' + 10;
		if (c >= 'a' && c <= 'f') return c - 'a' + 10;
		return -1;
	};
	std::string safe;
	for (size_t i = 0; i < name.size(); ++i) {
		unsigned char c = (unsigned char)name[i];
		if (c == '%' && i + 2 < name.size() && nibble(name[i + 1]) >= 0 && nibble(name[i + 2]) >= 0) {
			c = (unsigned char)((nibble(name[i + 1]) << 4) | nibble(name[i + 2]));
			i += 2;
		}
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_')) c = '_';
		safe += (char)c;
	}
	if (safe.empty() || safe == "." || safe == "..") safe = "download";
	if (safe.size() > 120) safe.erase(0, safe.size() - 120);
	// Windows reserves these basenames even with an extension. Use the
	// same portable cache name on every platform, including URL-decoded names.
	std::string stem = safe.substr(0, safe.find('.'));
	for (char& c : stem) if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
	if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL"
		|| (stem.size() == 4 && stem[3] >= '1' && stem[3] <= '9'
			&& (stem.compare(0, 3, "COM") == 0 || stem.compare(0, 3, "LPT") == 0)))
		safe.insert(0, "download-");
	return safe;
}
bool HTML(std::string prefix) {
	for (char& c : prefix) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
	size_t pos = prefix.compare(0, 3, "\xef\xbb\xbf") == 0 ? 3 : 0;
	for (;;) {
		pos = prefix.find_first_not_of(" \r\n\t", pos);
		if (pos == std::string::npos) return false;
		if (prefix.compare(pos, 4, "<!--")) break;
		const auto end = prefix.find("-->", pos + 4);
		if (end == std::string::npos) return false;
		pos = end + 3;
	}
	return !prefix.compare(pos, 5, "<html") || !prefix.compare(pos, 14, "<!doctype html");
}
std::string Hex(const ATChecksumSHA256& digest) {
	constexpr char digits[] = "0123456789abcdef";
	std::string result;
	for (uint8 value : digest.mDigest) { result += digits[value >> 4]; result += digits[value & 15]; }
	return result;
}
bool ZipName(std::string name) {
	for (char& c : name) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
	return name.size() >= 4 && (name.substr(name.size() - 4) == ".zip"
		);
}
const char* ImageSuffix(ATImageType type) {
	switch (type) {
		case kATImageType_Program: return ".xex";
		case kATImageType_BasicProgram: return ".bas";
		case kATImageType_Disk: return ".atr";
		case kATImageType_Cartridge: return ".car";
		case kATImageType_Tape: return ".cas";
		default: return nullptr;
	}
}
void InspectGzip(VDFileStream& source, std::string& name, Progress& progress) {
	if (source.Length() < 18) throw std::runtime_error("Truncated gzip game image.");
	uint8 trailer[8];
	source.Seek(source.Length() - 8); source.Read(trailer, sizeof trailer);
	source.Seek(0);
	vdautoptr<VDGUnzipStream> decoded(new VDGUnzipStream(&source, source.Length()));
	decoded->EnableCRC(); decoded->SetExpectedCRC(VDReadUnalignedLEU32(trailer));
	std::vector<uint8> prefix;
	uint8 chunk[32768];
	uint64 size = 0;
	for (;;) {
		if (Cancelled(progress)) throw std::runtime_error("cancelled");
		const sint32 count = decoded->ReadData(chunk, sizeof chunk);
		if (!count) break;
		size += (uint32)count;
		if (size > kMaxGameBytes) throw std::runtime_error("Expanded gzip game exceeds 64 MiB.");
		if (prefix.size() < 65536)
			prefix.insert(prefix.end(), chunk, chunk + std::min<size_t>(count, 65536 - prefix.size()));
	}
	decoded->VerifyCRC();
	if (!size || size != VDReadUnalignedLEU32(trailer + 4))
		throw std::runtime_error("Gzip game length does not match its trailer.");
	if (ClassifyGameFilename(VDTextU8ToW(VDStringSpanA(name.c_str())).c_str()) == GameMediaType::Unknown) {
		VDMemoryStream sample(prefix.data(), (uint32)prefix.size());
		const auto* suffix = ImageSuffix(ATDetectImageType(L"download", sample));
		if (!suffix) throw std::runtime_error("The gzip download contains no recognized game image.");
		name += suffix;
		name += ".gz";
	}
}
}

Result Download(const std::string& url, const std::string& cacheDirectory,
	Progress& progress) {
	Result result;
	fs::path staging;
	try {
		if (!ATMediaURL::IsURL(url)) throw std::runtime_error("Enter a complete HTTP(S) download URL.");
		if (cacheDirectory.empty()) throw std::runtime_error("Choose a library download directory.");
		if (Cancelled(progress)) throw std::runtime_error("cancelled");
		progress.received = 0;
		progress.total = 0;
		const auto root = fs::absolute(Native(cacheDirectory)).lexically_normal();
		fs::create_directories(root);
		const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
		for (unsigned attempt = 0; ; ++attempt) {
			staging = root / (".library-download-" + std::to_string(nonce) + "-" + std::to_string(attempt));
			if (fs::create_directory(staging)) break;
		}
		const auto partial = staging / "download.part";
		VDFileStream file(Wide(partial).c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
		ATChecksumEngineSHA256 hash;
		std::string prefix, sinkError;
		uint64_t written = 0;
		const std::string originalName = Name(url);
		ATHttp::Request request;
		request.url = url;
		request.timeoutMs = 12 * 60 * 60 * 1000;
		request.maxBodyBytes = 0;
		request.userAgent = "AltirraSDL";
		request.cancel = &progress.cancel;
		request.bytesReceived = &progress.received;
		request.bytesTotal = &progress.total;
		request.bodySink = [&](const void* data, size_t size) {
			try {
				if (Cancelled(progress)) return false;
				if (size > (uint64_t)std::numeric_limits<sint64>::max() - written)
					throw std::runtime_error("Download exceeds the filesystem size limit.");
				prefix.append((const char*)data, std::min(size, 4096 - prefix.size()));
				if (HTML(prefix)) throw std::runtime_error("The URL returned a web page. Use a direct game or ZIP download link.");
				const bool zip = ZipName(originalName) || prefix.compare(0, 2, "PK") == 0;
				if (!zip && written + size > kMaxGameBytes)
					throw std::runtime_error("An individual game download exceeds 64 MiB; use a ZIP pack for a collection.");
				const auto* bytes = (const uint8*)data;
				for (size_t offset = 0; offset < size;) {
					const auto count = (sint32)std::min<size_t>(size - offset, 1024 * 1024);
					file.Write(bytes + offset, count);
					hash.Process(bytes + offset, count);
					offset += count;
				}
				written += size;
				return true;
			} catch (const MyError& error) { sinkError = error.c_str(); }
			catch (const std::exception& error) { sinkError = error.what(); }
			return false;
		};
		ATHttp::Response response;
		ATHttp::Get(request, response);
		if (Cancelled(progress)) throw std::runtime_error("cancelled");
		if (!sinkError.empty()) throw std::runtime_error(sinkError);
		if (!response.error.empty()) throw std::runtime_error(response.error);
		if (response.status != 200) throw std::runtime_error("HTTP " + std::to_string(response.status) + ": complete download required.");
		if (!written) throw std::runtime_error("The download is empty.");
		if (response.contentType == "text/html" || response.contentType == "application/xhtml+xml" || HTML(prefix))
			throw std::runtime_error("The URL returned a web page. Use a direct game or ZIP download link.");
		file.flush();
		file.close();
		std::string name = originalName;
		{
			VDFileStream source(Wide(partial).c_str());
			const VDStringW suggested = VDTextU8ToW(VDStringSpanA(name.c_str()));
			const auto type = ATDetectImageType(suggested.c_str(), source);
			result.archive = ZipName(name) || type == kATImageType_Zip;
			if (result.archive) {
				VDZipArchive zip;
				zip.Init(&source);
				for (sint32 i = 0; i < zip.GetFileCount(); ++i) {
					if (Cancelled(progress)) throw std::runtime_error("cancelled");
					const auto& entry = zip.GetFileInfo(i);
					if (!entry.mbSupported || !entry.mUncompressedSize || entry.mUncompressedSize > kMaxGameBytes) continue;
					if (IsLibraryGameMember(entry.mDecodedFileName.c_str(), entry.mUncompressedSize))
						++result.supportedFileCount;
				}
				if (!result.supportedFileCount) throw std::runtime_error("The ZIP pack contains no supported games of 64 MiB or less.");
				if (!ZipName(name)) name += ".zip";
			} else {
				if (written > kMaxGameBytes || type == kATImageType_None)
					throw std::runtime_error("The download is not a supported game image or ZIP pack.");
				if (type == kATImageType_GZip) InspectGzip(source, name, progress);
				if (ClassifyGameFilename(VDTextU8ToW(VDStringSpanA(name.c_str())).c_str()) == GameMediaType::Unknown) {
					const auto* suffix = ImageSuffix(type);
					if (!suffix) throw std::runtime_error("This download is not a supported library game.");
					name += suffix;
				}
				result.supportedFileCount = 1;
			}
		}
		if (Cancelled(progress)) throw std::runtime_error("cancelled");
		const auto directory = root / (Hex(hash.Finalize()) + "-" + Hex(ATComputeChecksumSHA256(name.data(), name.size())).substr(0, 16));
		const auto target = directory / Native(name);
		fs::rename(partial, staging / Native(name));
		std::error_code publishError;
		fs::rename(staging, directory, publishError);
		if (publishError && !fs::is_regular_file(target))
			throw fs::filesystem_error("Cannot publish library download", staging, directory, publishError);
		result.path = Wide(target);
		result.downloadBytes = written;
	} catch (const MyError& error) { result.error = error.c_str(); }
	catch (const std::exception& error) { result.error = error.what(); }
	if (!staging.empty()) { std::error_code ignored; fs::remove_all(staging, ignored); }
	return result;
}
} // namespace ATLibraryURL
