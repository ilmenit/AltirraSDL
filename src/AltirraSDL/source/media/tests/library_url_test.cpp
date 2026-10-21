#include <stdafx.h>
#include "library_url.h"
#include "library_extensions.h"
#include <at/atcore/vfs.h>
#include <vd2/system/zip.h>
#include <vd2/system/text.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
void ATUILoadRegistry(const wchar_t*) { std::abort(); }
void ATUISaveRegistry(const wchar_t*) { std::abort(); }
int main(int argc, char** argv) {
	if (argc < 3) return 2;
	ATLibraryURL::Progress progress;
	std::thread cancel;
	if (argc > 3) cancel = std::thread([&] {
		std::this_thread::sleep_for(std::chrono::milliseconds(std::atoi(argv[3])));
		progress.cancel = true;
	});
	const auto result = ATLibraryURL::Download(argv[1], argv[2], progress);
	if (cancel.joinable()) cancel.join();
	if (!result.error.empty()) { std::printf("ERROR %s\n", result.error.c_str()); return 1; }
	if (result.archive) {
		try {
			vdrefptr<ATVFSFileView> view;
			ATVFSOpenFileView(result.path.c_str(), false, ~view);
			auto zip = ATVFSOpenZipArchiveFromView(*view);
			for (sint32 i = 0; i < zip->GetZipArchive().GetFileCount(); ++i) {
				const auto& entry = zip->GetZipArchive().GetFileInfo(i);
				if (!entry.mbSupported || !IsLibraryGameMember(entry.mDecodedFileName.c_str(), entry.mUncompressedSize)) continue;
				auto game = zip->OpenStream(i);
				uint8 head[8];
				const auto bytes = game->GetStream().ReadData(head, sizeof head);
				std::printf("GAME_BYTES %d\n", bytes);
				break;
			}
		} catch (const MyError& error) { std::printf("ERROR VFS %s\n", error.c_str()); return 1; }
	}
	std::printf("PATH %s\n", VDTextWToU8(result.path).c_str());
	std::printf("BYTES %llu\nCOUNT %llu\nARCHIVE %d\n",
		(unsigned long long)result.downloadBytes, (unsigned long long)result.supportedFileCount, result.archive);
}
