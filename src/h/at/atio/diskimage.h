//	Altirra - Atari 800/800XL/5200 emulator
//	I/O library - disk image definitions
//	Copyright (C) 2008-2015 Avery Lee
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
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#ifndef f_AT_ATIO_DISKIMAGE_H
#define f_AT_ATIO_DISKIMAGE_H

#include <optional>
#include <vd2/system/function.h>
#include <vd2/system/refcount.h>
#include <at/atcore/checksum.h>
#include <at/atio/image.h>

class IVDRandomAccessStream;
class IATDiskImage;

enum ATDiskTimingMode {
	kATDiskTimingMode_Any,
	kATDiskTimingMode_UsePrecise,
	kATDiskTimingMode_UseOrdered
};

struct ATDiskVirtualSectorInfo {
	uint32	mFirstPhysSector = UINT32_MAX;
	uint32	mNumPhysSectors = 0;
};

struct ATDiskPhysicalSectorInfo {
	uint32	mImageOffset = 0;	// offset within memory image
	sint32	mDiskOffset = -1;	// offset within disk image (for rewriting)
	uint16	mImageSize = 0;		// size within image
	uint16	mPhysicalSize = 0;	// size on media
	bool	mbDirty = false;
	bool	mbMFM = false;
	float	mRotPos = 0;
	uint8	mFDCStatus = 0;		// FDC status as seen by 810 firmware (inverted)
	sint16	mWeakDataOffset = -1;
};

struct ATDiskGeometryInfo {
	uint16	mSectorSize;
	uint8	mBootSectorCount;
	uint32	mTotalSectorCount;
	uint8	mTrackCount;
	uint32	mSectorsPerTrack;
	uint8	mSideCount;

	// Returns the primary density for the disk; essentially, the density that
	// an MFM-capable drive would detect from the disk. This would usually be
	// the density used by track 0, sector 1 since otherwise the disk would not
	// boot... though there is no requirement that the disk have that sector,
	// nor that the drive uses it (some use sector 4).
	bool	mbMFM;

	// Indicates if the primary density of the disk is high density, using 2us
	// bit cells for FM or 1us bit cells for MFM for 8" or high density 5.25"/3.5".
	bool	mbHighDensity;
};

enum ATDiskImageFormat {
	kATDiskImageFormat_None,
	kATDiskImageFormat_ATR,
	kATDiskImageFormat_XFD,
	kATDiskImageFormat_P2,
	kATDiskImageFormat_P3,
	kATDiskImageFormat_ATX,
	kATDiskImageFormat_DCM
};

enum ATDiskInterleave {
	kATDiskInterleave_Default,
	kATDiskInterleave_1_1,			// 1,2,3,...
	kATDiskInterleave_SD_12_1,		// 1,8,15,4,11,18,7,14,3,10,17,6,13,2,9,16,5,12
	kATDiskInterleave_SD_9_1,		// 1,3,5,7,9,11,13,15,17,2,4,6,8,10,12,14,16,18
	kATDiskInterleave_SD_9_1_REV,	// 17,15,13,11,9,7,5,3,1,18,16,14,12,10,8,6,4,2
	kATDiskInterleave_SD_5_1,		// 4,8,12,16,1,5,9,13,17,2,6,10,14,18,3,7,11,15
	kATDiskInterleave_SD_4_1,		// 5,10,15,1,6,11,16,2,7,12,17,3,8,13,18,4,9,14 (Indus GT SuperSynchromesh)
	kATDiskInterleave_SD_2_1,
	kATDiskInterleave_ED_13_1,		// 1,3,5,7,9,11,13,15,17,19,21,23,25,2,4,6,8,10,12,14,16,18,20,22,24,26
	kATDiskInterleave_ED_12_1,		// 9,18,7,16,25,5,14,23,3,12,21,1,10,19,8,17,26,6,15,24,4,13,22,2,11,20
	kATDiskInterleave_DD_16_1,		// 1,10,9,18,8,17,7,16,6,15,5,14,4,13,3,12,2,11
	kATDiskInterleave_DD_15_1,		// 1,7,13,6,12,18,5,11,17,4,10,16,3,9,15,2,8,14
	kATDiskInterleave_DD_9_1,		// 1,3,5,7,9,11,13,15,17,2,4,6,8,10,12,14,16,18
	kATDiskInterleave_DD_7_1,		// 1,14,9,4,17,12,7,2,15,10,5,18,13,8,3,16,11,6
	kATDiskInterleave_SD26_14_1,	// 1,14,3,16,5,18,7,20,9,22,11,24,13,26,2,15,4,17,6,19,8,21,10,23,12,25
	kATDiskInterleave_DD26_23_1,	// 1,18,9,26,17,8,25,16,7,24,15,6,23,14,5,22,13,4,21,12,3,20,11,2,19,10
};

class ATDiskImagePSecIterBase {
public:
	ATDiskImagePSecIterBase() = default;
	ATDiskImagePSecIterBase(uint32 order) : mPhysOrder(order) {};

	bool operator==(const ATDiskImagePSecIterBase& other) const = default;

protected:
	uint32 mPhysOrder = 0;
};

struct ATDiskImagePSecIterView {
	uint32 mPhysIndex;
	uint32 mPhysOrder;
	ATDiskPhysicalSectorInfo mPhysInfo;
};

class ATDiskImagePSecIter final : public ATDiskImagePSecIterBase {
public:
	ATDiskImagePSecIter(const IATDiskImage& image, uint32 psec, uint32 count);

	void operator++();
	ATDiskImagePSecIterView operator*() const;

private:
	const IATDiskImage *mpImage = nullptr;
	uint32 mPhysIndex = 0;
	uint32 mPhysCount = 0;
};

class ATDiskImagePSecRange {
public:
	ATDiskImagePSecRange(const IATDiskImage& image, uint32 vsec);

	bool empty() const { return mPhysCount == 0; }
	size_t size() const { return mPhysCount; }

	ATDiskImagePSecIter begin() const { return ATDiskImagePSecIter(*mpImage, mPhysStartIndex, mPhysCount); }
	ATDiskImagePSecIterBase end() const { return ATDiskImagePSecIterBase(mPhysCount); }

private:
	const IATDiskImage *mpImage = nullptr;
	uint32 mPhysStartIndex = 0;
	uint32 mPhysCount = 0;
};

// Disk image interface
//
// IATDiskImage is an abstraction of a disk image interfaced to SIO via the standard
// disk drive protocol. It most often represents a floppy disk but may also represent
// a hard disk via SIO2PC or a PBI device driver. The most general form of image
// represented is therefore a simple linear block device.
//
// To support authentic floppy disk behavior, disk images use notions of virtual and
// physical sectors. Virtual sectors are those presented to SIO; physical sectors are
// the ones on the storage medium. Most images have 1:1 virtual to physical sectors;
// floppy disks may have 0-N physical sectors per virtual sector when there are missing
// or phantom sectors. Note that both virtual and physical sectors are indexed from 0
// here even though the SIO interface numbers sectors starting at 1.
//
// It is unspecified whether boot sectors have a size of 128 bytes or larger, so calling
// code must force 128 byte transfers as appropriate. Boot sectors may explicitly be
// larger than 128 bytes when representing the physical medium, since most double density
// formats actually store sectors 1-3 on track 0 as 256 bytes and in some cases that
// additional data must be preserved (bootable CP/M disks).
//
// The disk image interface also supports managing the persistent backing store for
// a device, including checking if there is a backing store to update, whether the
// disk image is dirty, and requesting a flush.
//
// Dynamic disk images are a special case; these are images that can change dynamically
// even without write commands to the image. This is usually because the image is
// virtual and being created on the fly.
//
class VDINTERFACE IATDiskImage : public IATImage {
public:
	enum : uint32 { kTypeID = "dsim"_vdfcctypeid };

	virtual ATDiskTimingMode GetTimingMode() const = 0;

	// Returns true if the disk image has been written to since the image was created
	// or loaded. It is unspecified whether writing a sector with contents identical to
	// the existing sector sets the dirty flag (other factors like copy-on-write, timestamp,
	// and sector error flags may require dirty to be set). Flush() is a no-op if the
	// image is not dirty.
	virtual bool IsDirty() const = 0;

	// Returns true if the disk image has a persistent backing store that can
	// be updated from the active image.
	virtual bool IsUpdatable() const = 0;

	// Returns true if the disk image has dynamic contents based on access pattern
	// or external updates, and false if the image is statically established on
	// load.
	virtual bool IsDynamic() const = 0;

	// Returns the persistent image format for the image. This may be None if the image
	// does not have an image file based backing store.
	virtual ATDiskImageFormat GetImageFormat() const = 0;

	// Flush any changes back to the persistent store. Returns true on success or no-op;
	// false if the image is not updatable. I/O exceptions may be thrown. A dirty image
	// becomes clean after a successful flush.
	virtual void Flush() = 0;

	virtual void SetPath(const wchar_t *path, ATDiskImageFormat format) = 0;
	virtual void Save(const wchar_t *path, ATDiskImageFormat format) = 0;

	virtual ATDiskGeometryInfo GetGeometry() const = 0;
	virtual uint32 GetSectorSize() const = 0;
	virtual uint32 GetSectorSize(uint32 virtIndex) const = 0;
	virtual uint32 GetBootSectorCount() const = 0;

	virtual uint32 GetPhysicalSectorCount() const = 0;
	virtual bool IsValidPhysicalSector(uint32 index) const = 0;
	virtual void GetPhysicalSectorInfo(uint32 index, ATDiskPhysicalSectorInfo& info) const = 0;
	virtual uint32 GetNextPhysicalSector(uint32 physIndex) const = 0;

	virtual void ReadPhysicalSector(uint32 index, void *data, uint32 len) = 0;
	virtual void WritePhysicalSector(uint32 index, const void *data, uint32 len, uint8 fdcStatus = 0xFF) = 0;

	virtual uint32 GetVirtualSectorCount() const = 0;
	virtual void GetVirtualSectorInfo(uint32 index, ATDiskVirtualSectorInfo& info) const = 0;

	ATDiskImagePSecRange GetPhysicalSectorsForVirtualSector(uint32 virtIndex) const {
		return ATDiskImagePSecRange(*this, virtIndex);
	}

	virtual uint32 ReadVirtualSector(uint32 index, void *data, uint32 len) = 0;
	virtual bool WriteVirtualSector(uint32 index, const void *data, uint32 len) = 0;

	// Returns true if the disk has standard sectors and can safely be reinterleaved;
	// false if it has phantom sectors that can't be safely reordered.
	virtual bool IsSafeToReinterleave() const = 0;

	// Reorder sectors with a different interleave pattern.
	virtual void Reinterleave(ATDiskInterleave interleave) = 0;
};

enum class ATDiskEditEvent : uint8 {
	PhysSectorAdded,
	PhysSectorRemoving
};

class IATEditableDiskImage : public IATDiskImage {
public:
	static constexpr auto kTypeID = "IATEditableDiskImage"_vdtypeid;

	virtual uint32 AddPhysicalSector(uint32 virtIndex, const ATDiskPhysicalSectorInfo& info, bool allocImageSpace, const void *optData, size_t dataLen) = 0;
	virtual void RemovePhysicalSector(uint32 physIndex) = 0;
	virtual void RemoveAllPhysicalSectorsForVirtualSector(uint32 virtIndex) = 0;

	// Update the parameters for a physical sector.
	//
	// If the image size is changed, the data from the smaller of the old and new
	// image sizes is kept. The image offset and dirty flags are not used.
	virtual void UpdatePhysicalSector(uint32 physIndex, const ATDiskPhysicalSectorInfo& info) = 0;

	// Attempt to resize the disk image to the given number of virtual sectors.
	// In general, this should be <65536 sectors since that is the limit of the
	// standard SIO disk protocol, but this is not enforced. New sectors are
	// filled with $00 and the disk is marked as dirty. The disk geometry is
	// auto-computed according to inference rules. An exception is thrown if
	// the disk is dynamic.
	virtual void Resize(uint32 sectors) = 0;

	// Replace a series of virtual sectors with new ones.
	virtual void FormatTrack(uint32 vsIndexStart, uint32 vsCount, const ATDiskVirtualSectorInfo *vsecs, uint32 psCount, const ATDiskPhysicalSectorInfo *psecs, const uint8 *psecData) = 0;

	using DiskEditHandler = vdfunction<void(ATDiskEditEvent, uint32 /*vsec*/, uint32 /*psec*/)>;
	virtual void AddDiskEditHandler(const DiskEditHandler *handler) = 0;
	virtual void RemoveDiskEditHandler(const DiskEditHandler *handler) = 0;
};

void ATLoadDiskImage(const wchar_t *path, IATDiskImage **ppImage);
void ATLoadDiskImage(const wchar_t *origPath, const wchar_t *imagePath, IVDRandomAccessStream& stream, IATDiskImage **ppImage);
void ATMountDiskImageVirtualFolder(const wchar_t *path, uint32 tracks, uint32 sectorsPerTrack, uint32 sectorSize, IATDiskImage **ppImage);
void ATMountDiskImageVirtualFolderSDFS(const wchar_t *path, uint32 sectorSize, uint64 uniquenessValue, IATDiskImage **ppImage);
void ATCreateDiskImage(uint32 sectorCount, uint32 bootSectorCount, uint32 sectorSize, IATDiskImage **ppImage);
void ATCreateDiskImage(const ATDiskGeometryInfo& geometry, IATDiskImage **ppImage);

void ATDiskConvertGeometryToPERCOM(uint8 percom[12], const ATDiskGeometryInfo& geom);
void ATDiskConvertPERCOMToGeometry(ATDiskGeometryInfo& geom, const uint8 percom[12]);

ATDiskInterleave ATDiskGetDefaultInterleave(const ATDiskGeometryInfo& info);
ATDiskGeometryInfo ATDiskCreateDefaultGeometry(uint32 sectorCount, uint32 sectorSize, uint32 bootSectorCount);

vdfunction<float(uint32)> ATDiskGetInterleaveFn(ATDiskInterleave interleave, const ATDiskGeometryInfo& info);

[[noreturn]] void ATThrowDiskReadOnlyException();
[[noreturn]] void ATThrowDiskUnsupportedOperation();

#endif	// f_AT_ATIO_DISKIMAGE_H
