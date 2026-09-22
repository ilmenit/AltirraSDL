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

#ifndef f_DISKVIRTIMAGE_H
#define f_DISKVIRTIMAGE_H

#include <vd2/system/time.h>
#include <at/atcore/deviceautosuggest.h>
#include "diskvirtimagebase.h"

// Virtual disk handler using the Atari DOS 2.x filesystem.
class ATDiskImageVirtualFolder final : public ATDiskImageVirtualFolderBase, public IATDeviceAutoSuggest {
public:
	ATDiskImageVirtualFolder();

	void *AsInterface(uint32 iid) override;

	void Init(const wchar_t *path);

	ATDiskGeometryInfo GetGeometry() const override;
	uint32 GetSectorSize() const override;
	uint32 GetSectorSize(uint32 virtIndex) const override;
	uint32 GetBootSectorCount() const override;

	void GetPhysicalSectorInfo(uint32 index, ATDiskPhysicalSectorInfo& info) const override;

	void ReadPhysicalSector(uint32 index, void *data, uint32 len) override;

	uint32 GetVirtualSectorCount() const override;
	void GetVirtualSectorInfo(uint32 index, ATDiskVirtualSectorInfo& info) const override;

	uint32 ReadVirtualSector(uint32 index, void *data, uint32 len) override;

	void Reinterleave(ATDiskInterleave interleave) override;

public:	// IATDeviceAutoSuggest
	void AutoSuggestCIOPaths(char cioDevice, uint8 unit, const VDStringA& path, IATDeviceAutoSuggestSink& sink) override;

private:
	void TimerCallback();

	void UpdateDirectory(bool reportNewFiles);
	static bool IsMappableHostFilename(const wchar_t *s);

	struct DirEnt {
		enum {
			kFlagDeleted	= 0x80,
			kFlagInUse		= 0x40,
			kFlagLocked		= 0x20,
			kFlagDOS2		= 0x02,
			kFlagOpenWrite	= 0x01
		};

		uint8	mFlags;
		uint8	mSectorCount[2];
		uint8	mFirstSector[2];
		uint8	mName[11];
	};

	struct XDirBaseEnt {
		VDStringW mPath;
		uint32	mSize = 0;				// File size in bytes.
		uint32	mSectorCount = 0;		// Number of data sectors in the file.
		uint32	mLockedSector = 0;		// Virtual sector number of the next data sector after the last read one, or 0 if none.
	};

	struct XDirEnt : public XDirBaseEnt {
		VDFile	mFile;
		VDStringA mDosFileName;			// Canonical DOS path. Used for autosuggest.
		bool	mbValid = false;
		uint32	mSectorsAllocated = 1;
		uint32	mNextPrealloc = 0;
	};

	struct SectorEnt {
		bool	mbInCache;				// True if sector is in the LRU cache and can be reassigned. Locked and special sectors are not.
		sint8	mFileIndex;				// File index, or -1 if not assigned to a file.
		uint16	mSectorIndex;			// 0-based index of sector in file, in file order.
		uint16	mLRUPrev;
		uint16	mLRUNext;
	};

	void PromoteDataSector(uint32 sector);
	uint32 FindDataSector(sint8 fileIndex, uint16 sectorIndex) const;
	void UnlinkDataSector(uint32 sector);
	void LinkDataSector(uint32 sector);
	uint32 FindBestNextDataSector(sint8 fileIndex, uint32 prevSectorIndex);
	void PreallocateTrack(uint32 baseSectorIndex);

	VDStringW mPath;
	uint32	mSectorCount;
	uint32	mFreeSectorCount;
	bool mbBootFilePresent;
	VDDate mBootFileLastDate;
	int mDosEntry;

	vdfunction<float(uint32)> mpInterleaveFn;
	ATDiskGeometryInfo mGeometryInfo {};

	VDLazyTimer mCloseTimer;
	ATDirectoryWatcher mDirWatcher;

	DirEnt	mDirEnt[64];
	XDirEnt	mXDirEnt[64];

	vdfastvector<SectorEnt> mSectorMap;

	uint8 mBootSectors[384];
};

#endif
