// AltirraSDL fork extension: HTTP(S) media import.
#include <stdafx.h>
#include "media_url.h"
#include "http_client.h"

#include <at/atcore/checksum.h>
#include <vd2/system/refcount.h>
#include <at/atio/image.h>
#include <vd2/system/filesys.h>
#include <vd2/system/text.h>
#include <vd2/system/zip.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace ATMediaURL {
namespace {

bool Cancelled(const Progress& progress) {
	return progress.cancel.load(std::memory_order_relaxed);
}

std::string Filename(const std::string& url) {
	const size_t end = url.find_first_of("?#");
	const std::string path = url.substr(0, end);
	std::string name = path.substr(path.find_last_of('/') + 1);
	// Decode percent escapes before sanitising: image%2Ebin still needs
	// its extension for the canonical raw-cartridge format detector.
	const auto nibble = [](char c) -> int {
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'a' && c <= 'f') return c - 'a' + 10;
		if (c >= 'A' && c <= 'F') return c - 'A' + 10;
		return -1;
	};
	std::string decoded;
	for (size_t i = 0; i < name.size(); ++i) {
		if (name[i] == '%' && i + 2 < name.size() &&
			nibble(name[i + 1]) >= 0 && nibble(name[i + 2]) >= 0) {
			decoded += (char)((nibble(name[i + 1]) << 4) | nibble(name[i + 2]));
			i += 2;
		} else {
			decoded += name[i];
		}
	}
	name = std::move(decoded);
	// Never use server paths, Content-Disposition, or decoded separators as
	// local paths. Retain ordinary filenames for format detection and MRU.
	for (char& c : name) {
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_'))
			c = '_';
	}
	if (name.empty() || name == "." || name == "..")
		name = "image";
	if (name.size() > 120)
		name.erase(0, name.size() - 120);
	return name;
}

// Archives stay archives: the normal Altirra VFS chooses the image and
// resolves companion tape tracks. Nothing is extracted to host paths.
// Bound aggregate expansion as well as each member before the VFS opens it.
void CheckArchive(std::vector<uint8_t>& body, const std::wstring& name,
	Progress& progress, uint64_t& expanded, unsigned depth = 0) {
	if (Cancelled(progress))
		throw std::runtime_error("cancelled");
	if (depth > 4)
		throw std::runtime_error("Media archive nesting exceeds four levels.");
	VDMemoryStream stream(body.data(), (uint32)body.size());
	const ATImageType type = ATDetectImageType(name.c_str(), stream);
	if (type == kATImageType_None)
		throw std::runtime_error("The download does not contain a recognized emulator image.");
	if (type == kATImageType_Zip || type == kATImageType_SaveState2) {
		VDZipArchive zip;
		zip.Init(&stream);
		if (zip.GetFileCount() > 4096)
			throw std::runtime_error("Media archive contains too many files.");
		bool haveImage = false;
		for (sint32 i = 0; i < zip.GetFileCount(); ++i) {
			if (Cancelled(progress))
				throw std::runtime_error("cancelled");
			const auto& info = zip.GetFileInfo(i);
			// Modern save states use a JSON manifest plus binary members;
			// the normal loader recognises that manifest before media files.
			haveImage |= info.mDecodedFileName == L"savestate.json";
			expanded += info.mUncompressedSize;
			if (info.mUncompressedSize > kMaxDownloadBytes ||
				expanded > kMaxDownloadBytes * 2)
				throw std::runtime_error("Expanded media archive exceeds 128 MiB or a member exceeds 64 MiB.");
			// The normal ZIP loader selects media by the canonical extension
			// table. Inspect those members before the main thread loads them.
			const auto memberType = ATGetImageTypeForFileExtension(
				VDFileSplitExt(info.mDecodedFileName.c_str()));
			if (memberType == kATImageType_None)
				continue;
			haveImage = true;
			vdautoptr<IVDInflateStream> decoded(zip.OpenDecodedStream(i));
			std::vector<uint8_t> member(info.mUncompressedSize);
			if (!member.empty())
				decoded->Read(member.data(), (sint32)member.size());
			decoded->VerifyCRC();
			CheckArchive(member, info.mDecodedFileName.c_str(), progress,
				expanded, depth + 1);
		}
		if (!haveImage)
			throw std::runtime_error("The ZIP archive contains no recognized emulator image.");
	} else if (type == kATImageType_GZip) {
		if (body.size() < 18)
			throw std::runtime_error("The gzip media header or trailer is truncated.");
		vdautoptr<VDGUnzipStream> decoded(new VDGUnzipStream(&stream,
			stream.Length()));
		decoded->EnableCRC();
		decoded->SetExpectedCRC(VDReadUnalignedLEU32(
			body.data() + body.size() - 8));
		std::vector<uint8_t> member;
		uint8_t chunk[32768];
		for (;;) {
			if (Cancelled(progress))
				throw std::runtime_error("cancelled");
			const sint32 count = decoded->ReadData(chunk, sizeof chunk);
			if (!count)
				break;
			if (member.size() + count > kMaxDownloadBytes)
				throw std::runtime_error("Expanded gzip media exceeds 64 MiB.");
			expanded += count;
			if (expanded > kMaxDownloadBytes * 2)
				throw std::runtime_error("Expanded media archives exceed 128 MiB.");
			member.insert(member.end(), chunk, chunk + count);
		}
		decoded->VerifyCRC();
		if (member.size() != VDReadUnalignedLEU32(body.data() + body.size() - 4))
			throw std::runtime_error("The gzip media length does not match its trailer.");
		std::wstring innerName = name;
		if (const char* embedded = decoded->GetFilename(); embedded && *embedded)
			innerName = VDTextAToW(embedded).c_str();
		else if (innerName.size() >= 3 &&
			innerName.substr(innerName.size() - 3) == L".gz")
			innerName.resize(innerName.size() - 3);
		else
			innerName = L"image.atr";
		CheckArchive(member, innerName, progress, expanded, depth + 1);
	}
}

std::string Digest(const std::vector<uint8_t>& body) {
	ATChecksumEngineSHA256 engine;
	engine.Process(body.data(), body.size());
	const auto digest = engine.Finalize();
	static constexpr char hex[] = "0123456789abcdef";
	std::string value;
	for (const auto byte : digest.mDigest) {
		value += hex[byte >> 4];
		value += hex[byte & 15];
	}
	return value;
}

} // namespace

bool IsURL(const std::string& url) {
	const auto colon = url.find(':');
	if (colon == std::string::npos)
		return false;
	std::string scheme = url.substr(0, colon);
	for (char& c : scheme)
		if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
	if ((scheme != "http" && scheme != "https") ||
		url.compare(colon + 1, 2, "//") != 0)
		return false;
	const auto end = url.find_first_of("/?#", colon + 3);
	if (url.substr(colon + 3, end - colon - 3).empty())
		return false;
	return std::none_of(url.begin(), url.end(), [](unsigned char c) {
		return c <= 32 || c == 127;
	});
}

Result Download(const std::string& url, const std::string& cacheDirectory,
	Progress& progress) {
	Result result;
	std::filesystem::path partial;
	std::filesystem::path staging;
	try {
		if (!IsURL(url))
			throw std::runtime_error("Enter a complete HTTP or HTTPS image URL (encode spaces as %20).");
		if (cacheDirectory.empty())
			throw std::runtime_error("The media download directory is unavailable.");
		ATHttp::Request request;
		request.url = url;
		request.timeoutMs = 120000;
		request.maxBodyBytes = kMaxDownloadBytes;
		request.cancel = &progress.cancel;
		request.bytesReceived = &progress.received;
		request.bytesTotal = &progress.total;
		request.userAgent = "AltirraSDL";
		ATHttp::Response response;
		ATHttp::Get(request, response);
		if (Cancelled(progress))
			throw std::runtime_error("cancelled");
		if (!response.error.empty())
			throw std::runtime_error(response.error);
		if (response.status != 200)
			throw std::runtime_error("HTTP " + std::to_string(response.status) +
				": the server did not return a complete image.");
		if (response.body.empty())
			throw std::runtime_error("The downloaded image is empty.");
		if (response.contentType == "text/html" ||
			response.contentType == "application/xhtml+xml")
			throw std::runtime_error("The URL returned a web page. Use a direct image download link.");
		std::string prefix(response.body.begin(), response.body.begin() +
			std::min<size_t>(response.body.size(), 256));
		for (char& c : prefix)
			if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
		const auto first = prefix.find_first_not_of(" \t\r\n");
		if (first != std::string::npos &&
			(prefix.compare(first, 5, "<html") == 0 ||
			 prefix.compare(first, 14, "<!doctype html") == 0))
			throw std::runtime_error("The URL returned a web page. Use a direct image download link.");
		const std::string name = Filename(url);
		uint64_t expanded = 0;
		CheckArchive(response.body, VDTextU8ToW(VDStringSpanA(name.c_str())).c_str(), progress,
			expanded);
#ifdef _WIN32
		const auto root = std::filesystem::path(
			VDTextU8ToW(VDStringSpanA(cacheDirectory.c_str())).c_str());
#else
		const auto root = std::filesystem::path(cacheDirectory);
#endif
		const std::vector<uint8_t> nameBytes(name.begin(), name.end());
		const auto directory = root / (Digest(response.body) + "-" +
			Digest(nameBytes).substr(0, 16));
		std::filesystem::create_directories(root);
		const auto target = directory / name;
		// Keep an existing mounted image intact. Identical content has the
		// same digest; downloads are never reused based on URL alone.
		if (!std::filesystem::exists(target)) {
			// Allocate an exclusive staging directory. Separate application
			// instances downloading identical content never share a .part.
			const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
			for (unsigned attempt = 0; ; ++attempt) {
				staging = root / (".download-" + std::to_string(nonce) +
					"-" + std::to_string(attempt));
				if (std::filesystem::create_directory(staging))
					break;
			}
			partial = staging / name;
			std::ofstream file(partial, std::ios::binary | std::ios::trunc);
			if (!file || !file.write((const char*)response.body.data(),
				(std::streamsize)response.body.size()) || !file.flush())
				throw std::runtime_error("Unable to write the downloaded image cache.");
			file.close();
			if (Cancelled(progress))
				throw std::runtime_error("cancelled");
			// Publish the complete directory atomically. In particular POSIX
			// rename cannot replace a nonempty directory: a concurrent
			// downloader can never overwrite an already mounted cache file.
			// This also works on FAT/exFAT, without hard-link support.
			std::error_code publishError;
			std::filesystem::rename(staging, directory, publishError);
			if (!publishError) {
				partial.clear();
				staging.clear();
			} else if (!std::filesystem::exists(target)) {
				throw std::filesystem::filesystem_error(
					"Unable to publish the downloaded image", staging,
					directory, publishError);
			}
		}
		const auto utf8 = target.u8string();
		result.path.assign((const char*)utf8.data(), utf8.size());
	} catch (const MyError& error) {
		result.error = error.c_str();
	} catch (const std::exception& error) {
		result.error = error.what();
	} catch (...) {
		result.error = "Unable to download this image.";
	}
	if (!partial.empty()) {
		std::error_code ignored;
		std::filesystem::remove(partial, ignored);
	}
	if (!staging.empty()) {
		std::error_code ignored;
		std::filesystem::remove(staging, ignored);
	}
	return result;
}

} // namespace ATMediaURL
