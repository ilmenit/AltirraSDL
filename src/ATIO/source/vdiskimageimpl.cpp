//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2026 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#include <stdafx.h>
#include <vd2/system/filesys.h>
#include <vd2/system/strutil.h>
#include <at/atio/internal/vdiskimageimpl.h>
#include <at/atcore/vfs.h>

ATVirtualDiskImage::ATVirtualDiskImage(const ATVFSParsedPath& parsedPath) {
	if (parsedPath.mProtocol != kATVFSProtocol_Atvdisk)
		throw VDException(L"Path not supported for virtual disk.");

	if (!ATVFSExtractFilePath(parsedPath.mBasePath.c_str(), &mBasePath))
		throw VDException(L"The base path type is not supported for a virtual disk.");

	mBasePath = VDFileGetCanonicalPath(mBasePath.c_str());

	const VDStringSpanW fs(parsedPath.FindParam(L"fs", L"dos"));

	if (!fs.comparei(L"dos"))
		mFileSystem = ATVirtualDiskFileSystem::DOS;
	else if (!fs.comparei(L"sdfs"))
		mFileSystem = ATVirtualDiskFileSystem::SDFS;
	else
		throw VDException(L"Unsupported virtual disk filesystem: %ls", fs.data());

	mSectorCount = wcstoul(parsedPath.FindParam(L"sectors", L"0"), nullptr, 10);
	mSectorSize = wcstoul(parsedPath.FindParam(L"ssize", L"0"), nullptr, 10);
}

ATVirtualDiskImage::ATVirtualDiskImage(VDStringSpanW basePath, ATVirtualDiskFileSystem fs) {
	mBasePath = basePath;
	mFileSystem = fs;

	mSectorCount = 0;
	mSectorSize = 0;
}

void *ATVirtualDiskImage::AsInterface(uint32 iid) {
	switch(iid) {
		case IATVirtualDiskImage::kTypeID:
			return static_cast<IATVirtualDiskImage *>(this);

		default:
			return nullptr;
	}
}

const wchar_t *ATVirtualDiskImage::GetBasePath() const {
	return mBasePath.c_str();
}

ATVirtualDiskFileSystem ATVirtualDiskImage::GetFileSystem() const {
	return mFileSystem;
}

uint32 ATVirtualDiskImage::GetSectorCount() const {
	return mSectorCount;
}

uint32 ATVirtualDiskImage::GetSectorSize() const {
	return mSectorSize;
}

ATImageType ATVirtualDiskImage::GetImageType() const {
	return kATImageType_VDisk;
}

std::optional<uint32> ATVirtualDiskImage::GetImageFileCRC() const {
	return std::nullopt;
}

std::optional<ATChecksumSHA256> ATVirtualDiskImage::GetImageFileSHA256() const {
	return std::nullopt;
}

////////////////////////////////////////////////////////////////////////////////

vdrefptr<IATVirtualDiskImage> ATLoadVirtualDiskImage(const ATVFSParsedPath& parsedPath) {
	vdrefptr<ATVirtualDiskImage> image(new ATVirtualDiskImage(parsedPath));

	return image;
}

vdrefptr<IATVirtualDiskImage> ATTryLoadVirtualDiskImageFromWildFilePath(const ATVFSParsedPath& parsedPath) {
	if (parsedPath.mProtocol != kATVFSProtocol_File)
		return nullptr;

	VDStringSpanW basePath(parsedPath.mBasePath);
	const VDStringSpanW& filename = VDFileSplitPathRightSpan(basePath);
	const VDStringSpanW dir(basePath.begin(), filename.begin());

	if (filename == L"*")
		return vdmakerefcounted<ATVirtualDiskImage>(dir, ATVirtualDiskFileSystem::DOS);
	else if (filename == L"**")
		return vdmakerefcounted<ATVirtualDiskImage>(dir, ATVirtualDiskFileSystem::SDFS);
	else
		return nullptr;
}

VDStringW ATMakeVirtualDiskImagePath(const VDStringSpanW& path, uint32 sectorCount, uint32 sectorSize, ATVirtualDiskFileSystem fs) {
	// check if we can use a compatibility path
	const bool sdfs = fs == ATVirtualDiskFileSystem::SDFS;

	// MERGE NOTE: legacy /** cannot encode 256-byte sectors.
	if (sdfs && sectorCount == 0 && sectorSize <= 128)
		return VDMakePath(path, VDStringSpanW(L"**"));
	else if (!sdfs && sectorCount == 720 && sectorSize == 128)
		return VDMakePath(path, VDStringSpanW(L"*"));
	else {
		VDStringW vdiskPath(L"atvdisk://");
		ATEncodeVFSString(vdiskPath, VDStringSpanW(path), true);

		vdiskPath.append_sprintf(L"?fs=%ls&sectors=%u&ssize=%u"
			, sdfs ? L"sdfs" : L"dos"
			, std::clamp<uint32>(sectorCount, 720, 65535)
			, sectorSize > 128 ? 256 : 128
		);

		return vdiskPath;
	}
}
