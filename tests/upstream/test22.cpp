#include <stdafx.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/filesys.h>
#include <vd2/system/file.h>
#include <vd2/vdjson/jsonreader.h>
#include <vd2/vdjson/jsonvalue.h>
#include <at/attest/test.h>
#include <at/atcore/vfs.h>
#include <at/atio/image.h>
#include <at/atio/vdiskimage.h>
#include <at/atio/diskimage.h>
#include <at/atio/diskfs.h>
#include "directorywatcher.h"
#include "diskvirtimage.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>
#include <memory>
#include <chrono>

struct Test { ATTestFn fn; const char *name; };
static std::vector<Test>& Tests() { static std::vector<Test> tests; return tests; }
void ATTestAddTest(ATTestFn fn, const char *name, bool) { Tests().push_back({fn, name}); }
bool ATTestShouldBreak() { return false; }
static void Check(bool value, const char *message) {
	if (!value) { fprintf(stderr, "%s\n", message); std::exit(1); }
}

// Settings migration is outside this fixture. Unexpected calls fail.
VDStringW ATSettingsGetDefaultPortablePath() { Check(false, "Unexpected settings migration"); return {}; }
bool ATSettingsIsMigrationScheduled() { Check(false, "Unexpected migration check"); return false; }
bool ATSettingsIsInPortableMode() { Check(false, "Unexpected portable mode check"); return false; }

static void CheckJSON(const void *data, size_t size, const wchar_t *expected) {
	VDJSONReader reader;
	VDJSONDocument doc;
	Check(reader.Parse(data, size, doc), "Unicode JSON parse failed");
	Check(!wcscmp(doc.Root()[L"path"].AsString(), expected), "Unicode JSON changed path");
}

int main() {
	try {
		for (const auto& test : Tests()) {
			Check(test.fn() == 0, test.name);
			printf("Passed %s\n", test.name);
		}
		const std::string longKey(9000, 'k');
		const std::string longObject = "{\"" + longKey + "\":123}";
		VDJSONReader longReader;
		VDJSONDocument longDoc;
		Check(longReader.Parse(longObject.data(), longObject.size(), longDoc), "Long member name parse");
		Check(longDoc.Root()[longKey.c_str()].AsInt64() == 123, "Long member name lookup");
		const char *raw = "{\"path\":\"\xc3\xa7\xe4\xb8\xad\xf0\x9f\x98\x80\"}";
		const wchar_t *expected = L"\u00e7\u4e2d\U0001f600";
		CheckJSON(raw, strlen(raw), expected);
		const char16_t utf16[] = u"{\"path\":\"\u00e7\u4e2d\U0001f600\"}";
		CheckJSON(utf16, sizeof(utf16) - sizeof(utf16[0]), expected);
		const char32_t utf32[] = U"{\"path\":\"\u00e7\u4e2d\U0001f600\"}";
		CheckJSON(utf32, sizeof(utf32) - sizeof(utf32[0]), expected);
		// Put a non-BMP scalar across the reader's 256-character refill boundary.
		for (size_t pad = 240; pad < 270; ++pad) {
			std::string json = "{\"path\":\"" + std::string(pad, 'a') + "\xf0\x9f\x98\x80\"}";
			std::wstring text(pad, L'a'); text += L"\U0001f600";
			CheckJSON(json.data(), json.size(), text.c_str());
			const std::u16string text16 = u"{\"path\":\"" + std::u16string(pad, u'a') + u"\U0001f600\"}";
			CheckJSON(text16.data(), text16.size() * sizeof(char16_t), text.c_str());
			const std::u32string text32 = U"{\"path\":\"" + std::u32string(pad, U'a') + U"\U0001f600\"}";
			CheckJSON(text32.data(), text32.size() * sizeof(char32_t), text.c_str());
		}
		// Windows paths on Windows, absolute POSIX roots elsewhere.
#ifdef _WIN32
		const wchar_t *url = L"file:///C:/folder/image.atr";
		Check(ATParseVFSPath(url).mBasePath == L"C:\\folder\\image.atr", "Windows file URL path");
#else
		Check(ATParseVFSPath(L"file:///tmp/image.atr").mBasePath == L"/tmp/image.atr", "POSIX file URL lost root");
		Check(VDFileGetCanonicalPath(L"../folder/../image.atr") == L"../image.atr", "Relative parent path lost");
#endif
		Check(!ATParseVFSPath(L"file://").IsValid(), "Empty file URL accepted");
		Check(!ATParseVFSPath(L"atvdisk://folder?broken").IsValid(), "Malformed query accepted");
		const auto temp = std::filesystem::temp_directory_path() / ("altirra-test22-vdisk-" +
			std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(temp);
		struct Cleanup { std::filesystem::path path; ~Cleanup() { std::filesystem::remove_all(path); } } cleanup{temp};
		const auto mountDir = temp / std::filesystem::path(u8"\u00e7 \u4e2d\U0001f600");
		std::filesystem::create_directories(mountDir);
		std::string content(4097, 'x');
		for (size_t i = 0; i < content.size(); ++i) content[i] = (char)(i % 251);
		std::ofstream(mountDir / "HELLO.TXT", std::ios::binary).write(content.data(), content.size());
		const auto path8 = mountDir.u8string();
		const auto path = VDTextU8ToW((const char *)path8.c_str(), -1);
		Check(VDDoesDirectoryExist(path.c_str()), "Portable directory lookup failed");
		for (const auto fs : {ATVirtualDiskFileSystem::DOS, ATVirtualDiskFileSystem::SDFS}) {
			for (uint32 size : {128u, 256u}) {
				const auto mount = ATMakeVirtualDiskImagePath(VDStringSpanW(path),
					fs == ATVirtualDiskFileSystem::SDFS ? 0 : 720, size, fs);
				const auto parsed = ATParseVFSPath(mount.c_str());
				auto image = ATTryLoadVirtualDiskImageFromWildFilePath(parsed);
				if (!image) image = ATLoadVirtualDiskImage(parsed);
				Check(image->GetFileSystem() == fs, "Mount lost filesystem");
				Check((image->GetSectorSize() > 128 ? 256 : 128) == size, "Mount lost sector size");
				ATImageLoadContext ctx;
				ctx.mLoadType = kATImageType_Disk;
				vdrefptr<IATImage> loaded;
				bool update = true;
				Check(ATImageLoadAuto(mount.c_str(), nullptr, parsed, &ctx, nullptr,
					nullptr, &update, ~loaded), "Disk-specific virtual mount failed");
				Check(loaded->GetImageType() == kATImageType_VDisk && !update, "Virtual mount write mode");
			}
		}
		const auto specialPath = path + L"/\u00e7 \u4e2d\U0001f600+%?!";
		const auto mount = ATMakeVirtualDiskImagePath(VDStringSpanW(specialPath), 1040, 128, ATVirtualDiskFileSystem::DOS);
		const auto parsed = ATParseVFSPath(mount.c_str());
		Check(parsed.mBasePath == specialPath && !wcscmp(parsed.FindParam(L"sectors"), L"1040"), "Virtual URL escaping");
		for (const wchar_t *root : {L"/*", L"/**"}) {
			auto image = ATTryLoadVirtualDiskImageFromWildFilePath(ATParseVFSPath(root));
			Check(image && !VDFileIsRelativePath(image->GetBasePath()), "Legacy mount lost root");
		}
		for (uint32 size : {128u, 256u}) {
			for (uint32 sectors : {720u, 1040u, 1440u, 65520u}) {
				const uint32 spt = sectors == 1040 ? 26 : 18;
				vdrefptr<IATDiskImage> image;
				ATMountDiskImageVirtualFolder(path.c_str(), sectors / spt, spt, size, ~image);
				Check(image->GetVirtualSectorCount() == sectors, "Virtual disk sector count");
				Check(image->GetSectorSize(0) == 128 && image->GetSectorSize(3) == size, "Virtual boot/data sector sizes");
				std::vector<uint8> data(size);
				Check(image->ReadVirtualSector(360, data.data(), size) == size, "Virtual directory read");
				Check(!memcmp(data.data() + 5, "HELLO   TXT", 11), "Virtual directory content");
				std::unique_ptr<IATDiskFS> fs(ATDiskMountImageDOS2(image, true));
				const auto key = fs->LookupFile(ATDiskFSKey::None, "HELLO.TXT");
				Check(key != ATDiskFSKey::None, "Virtual file missing");
				vdfastvector<uint8> bytes;
				fs->ReadFile(key, bytes);
				Check(bytes.size() == content.size() && !memcmp(bytes.data(), content.data(), bytes.size()), "Virtual file sector chain");
			}
		}
		for (uint32 size : {128u, 256u}) {
			vdrefptr<IATDiskImage> image;
			ATMountDiskImageVirtualFolderSDFS(path.c_str(), size, 1, ~image);
			Check(image->GetSectorSize() == size, "SpartaDOS sector size");
			Check(image->GetSectorSize(0) == 128 && image->GetSectorSize(3) == size, "SpartaDOS boot/data sizes");
			std::unique_ptr<IATDiskFS> fs(ATDiskMountImageSDX2(image, true));
			const auto key = fs->LookupFile(ATDiskFSKey::None, "HELLO.TXT");
			Check(key != ATDiskFSKey::None, "SpartaDOS file missing");
			vdfastvector<uint8> bytes;
			fs->ReadFile(key, bytes);
			Check(bytes.size() == content.size() && !memcmp(bytes.data(), content.data(), bytes.size()), "SpartaDOS file sector chain");
		}
		puts("Passed test22 Unicode, URL, persistence and geometry regressions");
		return 0;
	} catch (const VDException& e) {
		fprintf(stderr, "%s\n", e.c_str()); return 1;
	}
}
