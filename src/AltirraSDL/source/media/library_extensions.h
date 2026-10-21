// Shared Game Library extension classification (scanner and URL import).
#pragma once
#include <cstdint>
enum class GameMediaType {
	Disk,
	Executable,
	Cartridge,
	Cassette,
	Unknown,
};

GameMediaType ClassifyExtension(const wchar_t *ext);
bool IsSupportedGameExtension(const wchar_t *name);
bool IsArchiveExtension(const wchar_t *name);
bool IsSupportedImageExtension(const wchar_t *name);

constexpr uint64_t kATLibraryMaxMemberBytes = 64ULL * 1024 * 1024;
bool IsLibraryGameMember(const wchar_t *name, uint64_t bytes);

GameMediaType ClassifyGameFilename(const wchar_t *name);
