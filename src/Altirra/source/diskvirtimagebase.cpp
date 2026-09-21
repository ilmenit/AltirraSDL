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

#include <stdafx.h>
#include "diskvirtimagebase.h"

void *ATDiskImageVirtualFolderBase::AsInterface(uint32 id) {
	switch(id) {
		case IATDiskImage::kTypeID: return static_cast<IATDiskImage *>(this);
	}

	return nullptr;
}

void ATDiskImageVirtualFolderBase::SetPath(const wchar_t *path, ATDiskImageFormat format) {
}

void ATDiskImageVirtualFolderBase::Save(const wchar_t *path, ATDiskImageFormat format) {
}

uint32 ATDiskImageVirtualFolderBase::GetPhysicalSectorCount() const {
	return GetVirtualSectorCount();
}

bool ATDiskImageVirtualFolderBase::IsValidPhysicalSector(uint32 physIndex) const {
	return physIndex < GetVirtualSectorCount();
}

void ATDiskImageVirtualFolderBase::WritePhysicalSector(uint32 index, const void *data, uint32 len, uint8 fdcStatus) {
	ATThrowDiskReadOnlyException();
}

bool ATDiskImageVirtualFolderBase::WriteVirtualSector(uint32 index, const void *data, uint32 len) {
	ATThrowDiskReadOnlyException();
	return false;
}
