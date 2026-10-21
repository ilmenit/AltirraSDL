#include <stdafx.h>
#include "library_extensions.h"
#include <vd2/system/VDString.h>
#include <cwctype>
#include <cwchar>

GameMediaType ClassifyExtension(const wchar_t *ext) {
	VDStringW e;
	for (const wchar_t *p = ext; *p; ++p)
		e += (wchar_t)std::towlower(*p);

	if (e == L"atr" || e == L"atx" || e == L"xfd" || e == L"dcm" || e == L"pro")
		return GameMediaType::Disk;
	if (e == L"xex" || e == L"obx" || e == L"com" || e == L"exe" || e == L"bas")
		return GameMediaType::Executable;
	if (e == L"car" || e == L"rom" || e == L"bin" || e == L"a52")
		return GameMediaType::Cartridge;
	if (e == L"cas" || e == L"wav" || e == L"flac" || e == L"ogg")
		return GameMediaType::Cassette;

	return GameMediaType::Unknown;
}

bool IsSupportedGameExtension(const wchar_t *name) {
	const wchar_t *ext = wcsrchr(name, L'.');
	if (!ext) return false;
	ext++;

	VDStringW e;
	for (const wchar_t *p = ext; *p; ++p)
		e += (wchar_t)std::towlower(*p);

	static const wchar_t *kExtensions[] = {
		L"xex", L"atr", L"car", L"bin", L"rom", L"cas",
		L"dcm", L"atz", L"zip", L"gz", L"xfd", L"atx",
		L"obx", L"com", L"exe", L"pro", L"a52", L"bas",
		L"wav", L"flac", L"ogg", L"sap", L"vgm", L"vgz",
		L"arc",
		nullptr
	};

	for (const wchar_t **p = kExtensions; *p; ++p) {
		if (e == *p)
			return true;
	}
	return false;
}

bool IsArchiveExtension(const wchar_t *name) {
	const wchar_t *ext = wcsrchr(name, L'.');
	if (!ext) return false;
	ext++;

	VDStringW e;
	for (const wchar_t *p = ext; *p; ++p)
		e += (wchar_t)std::towlower(*p);

	return e == L"zip" || e == L"gz" || e == L"atz" || e == L"arc";
}

bool IsSupportedImageExtension(const wchar_t *name) {
	const wchar_t *ext = wcsrchr(name, L'.');
	if (!ext) return false;
	ext++;

	VDStringW e;
	for (const wchar_t *p = ext; *p; ++p)
		e += (wchar_t)std::towlower(*p);

	return e == L"png" || e == L"jpg" || e == L"jpeg"
		|| e == L"bmp" || e == L"webp";
}


bool IsLibraryGameMember(const wchar_t *name, uint64_t bytes) {
	return bytes && bytes <= kATLibraryMaxMemberBytes && IsSupportedGameExtension(name)
		&& ClassifyGameFilename(name) != GameMediaType::Unknown;
}

GameMediaType ClassifyGameFilename(const wchar_t *name) {
	VDStringW lower(name);
	for (auto& c : lower) c = (wchar_t)std::towlower(c);
	if (lower.size() >= 4 && !wcscmp(lower.c_str() + lower.size() - 4, L".atz")) return GameMediaType::Disk;
	if (lower.size() >= 3 && !wcscmp(lower.c_str() + lower.size() - 3, L".gz")) lower.resize(lower.size() - 3);
	const auto* ext = wcsrchr(lower.c_str(), L'.');
	return ext ? ClassifyExtension(ext + 1) : GameMediaType::Unknown;
}
