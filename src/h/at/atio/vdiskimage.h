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

#ifndef f_AT_ATIO_VDISKIMAGE_H
#define f_AT_ATIO_VDISKIMAGE_H

#include <vd2/system/refcount.h>
#include <vd2/system/unknown.h>
#include <at/atio/image.h>

struct ATVFSParsedPath;

enum class ATVirtualDiskFileSystem : uint8 {
	DOS,
	SDFS
};

class IATVirtualDiskImage : public IATImage {
public:
	static constexpr auto kTypeID = "IATVirtualDiskImage"_vdtypeid;

	virtual const wchar_t *GetBasePath() const = 0;
	virtual ATVirtualDiskFileSystem GetFileSystem() const = 0;
	virtual uint32 GetSectorCount() const = 0;
	virtual uint32 GetSectorSize() const = 0;
};

vdrefptr<IATVirtualDiskImage> ATLoadVirtualDiskImage(const ATVFSParsedPath& parsedPath);
vdrefptr<IATVirtualDiskImage> ATTryLoadVirtualDiskImageFromWildFilePath(const ATVFSParsedPath& parsedPath);

VDStringW ATMakeVirtualDiskImagePath(const VDStringSpanW& path, uint32 sectorCount, uint32 sectorSize, ATVirtualDiskFileSystem fs);

#endif
