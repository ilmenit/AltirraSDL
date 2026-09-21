//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2020 Avery Lee
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

#ifndef f_AT_DISKVIRTIMAGEBASE_H
#define f_AT_DISKVIRTIMAGEBASE_H

#include <vd2/system/refcount.h>
#include <at/atio/diskimage.h>

class ATDiskImageVirtualFolderBase : public vdrefcounted<IATDiskImage> {
public:
	void *AsInterface(uint32 id) override;

	ATImageType GetImageType() const override { return kATImageType_Disk; }
	ATDiskTimingMode GetTimingMode() const override { return kATDiskTimingMode_Any; }

	bool IsDirty() const override { return false; }
	bool IsUpdatable() const override { return false; }
	bool IsDynamic() const override { return true; }
	ATDiskImageFormat GetImageFormat() const override { return kATDiskImageFormat_None; }

	std::optional<uint32> GetImageFileCRC() const override { return {}; }
	std::optional<ATChecksumSHA256> GetImageFileSHA256() const override { return {}; }

	void Flush() override { }

	void SetPath(const wchar_t *path, ATDiskImageFormat format) override;
	void Save(const wchar_t *path, ATDiskImageFormat format) override;

	uint32 GetPhysicalSectorCount() const final;
	bool IsValidPhysicalSector(uint32 physIndex) const final;
	uint32 GetNextPhysicalSector(uint32 physIndex) const final { return physIndex; }
	void WritePhysicalSector(uint32 index, const void *data, uint32 len, uint8 fdcStatus);

	bool WriteVirtualSector(uint32 index, const void *data, uint32 len);

	bool IsSafeToReinterleave() const override final { return true; }
};

#endif
