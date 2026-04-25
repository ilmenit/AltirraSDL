//	Altirra SDL3 frontend - POSIX physical disk passthrough
//
//	Cross-platform port of src/Altirra/source/idephysdisk.cpp. Replaces
//	Win32 CreateFileW + DeviceIoControl with O_RDONLY + ioctl
//	(Linux: BLKGETSIZE64; macOS: DKIOCGETBLOCKCOUNT). Used by IDE
//	device emulation (Side, KMK/JZ, MyIDE, etc.) to expose a host raw
//	block device as Atari hard-disk storage.
//
//	Read-only mirror of Win32 semantics — the original IsReadOnly()
//	returns true and WriteSectors() is a no-op. We preserve that.
//
//	Path convention: any path starting with "/dev/" is treated as a
//	physical disk on POSIX systems. (The Win32 sentinel was the
//	"\\\\?\\" prefix.) Opening such a node typically requires root
//	or appropriate udev permissions; failures throw MyError up to
//	the caller.

#include <stdafx.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/text.h>
#include <vd2/system/hash.h>
#include <at/atcore/blockdevice.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/propertyset.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

#if defined(__linux__)
	#include <sys/ioctl.h>
	#include <linux/fs.h>
#elif defined(__APPLE__)
	#include <sys/disk.h>
#endif

#include "idephysdisk.h"

namespace {
	constexpr uint32 kSectorSize = 512;
	constexpr uint32 kReadBatchSectors = 32;

	// Reinterpret an int fd through the void* mhDisk slot. -1 sentinel
	// stays distinct from any valid fd by going through intptr_t.
	int FdFromHandle(void *h) {
		return (int)(intptr_t)h;
	}
	void *HandleFromFd(int fd) {
		return (void *)(intptr_t)fd;
	}
}

bool ATIDEIsPhysicalDiskPath(const wchar_t *path) {
	if (!path)
		return false;

	VDStringA u8 = VDTextWToU8(VDStringW(path));
	// Linux/Android: /dev/sd*, /dev/nvme*, /dev/mmcblk*, /dev/loop*, ...
	// macOS: /dev/disk*, /dev/rdisk*
	return u8.size() >= 5 && memcmp(u8.data(), "/dev/", 5) == 0;
}

sint64 ATIDEGetPhysicalDiskSize(const wchar_t *path) {
	if (!path)
		return -1;

	VDStringA u8 = VDTextWToU8(VDStringW(path));
	int fd = ::open(u8.c_str(), O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;

	sint64 size = -1;

#if defined(__linux__)
	uint64_t sz = 0;
	if (::ioctl(fd, BLKGETSIZE64, &sz) == 0)
		size = (sint64)sz;
#elif defined(__APPLE__)
	uint64_t blockCount = 0;
	uint32_t blockSize = 0;
	if (::ioctl(fd, DKIOCGETBLOCKCOUNT, &blockCount) == 0
		&& ::ioctl(fd, DKIOCGETBLOCKSIZE, &blockSize) == 0)
	{
		size = (sint64)(blockCount * blockSize);
	}
#endif

	if (size < 0) {
		// Fall back to lseek SEEK_END — works for regular files, which
		// /dev/loopN report as well on Linux.
		off_t end = ::lseek(fd, 0, SEEK_END);
		if (end > 0)
			size = (sint64)end;
	}

	::close(fd);
	return size;
}

void ATCreateDeviceHardDiskPhysical(const ATPropertySet& pset, IATDevice **dev);

extern const ATDeviceDefinition g_ATDeviceDefIDEPhysDisk = {
	"hdphysdisk", "harddisk", L"Hard disk image (physical disk)",
	ATCreateDeviceHardDiskPhysical
};

ATIDEPhysicalDisk::ATIDEPhysicalDisk()
	: mhDisk(HandleFromFd(-1))
	, mpBuffer(nullptr)
	, mSectorCount(0)
{
}

ATIDEPhysicalDisk::~ATIDEPhysicalDisk() {
	Shutdown();
}

int ATIDEPhysicalDisk::AddRef() {
	return ATDevice::AddRef();
}

int ATIDEPhysicalDisk::Release() {
	return ATDevice::Release();
}

void *ATIDEPhysicalDisk::AsInterface(uint32 iid) {
	switch (iid) {
		case IATBlockDevice::kTypeID: return static_cast<IATBlockDevice *>(this);
		default:
			return ATDevice::AsInterface(iid);
	}
}

void ATIDEPhysicalDisk::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefIDEPhysDisk;
}

void ATIDEPhysicalDisk::GetSettings(ATPropertySet& settings) {
	settings.SetString("path", mPath.c_str());
}

bool ATIDEPhysicalDisk::SetSettings(const ATPropertySet&) {
	return false;
}

ATBlockDeviceGeometry ATIDEPhysicalDisk::GetGeometry() const {
	return ATBlockDeviceGeometry();
}

uint32 ATIDEPhysicalDisk::GetSerialNumber() const {
	return VDHashString32I(mPath.c_str());
}

void ATIDEPhysicalDisk::Init(const wchar_t *path) {
	Shutdown();

	mPath = path;

	VDStringA u8 = VDTextWToU8(VDStringW(path));
	int fd = ::open(u8.c_str(), O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		const int err = errno;
		throw MyError("Cannot open physical disk %ls: %s",
			path, strerror(err));
	}

	mhDisk = HandleFromFd(fd);

	mpBuffer = VDFile::AllocUnbuffer(kSectorSize * kReadBatchSectors);
	if (!mpBuffer) {
		Shutdown();
		throw MyMemoryError();
	}

	sint64 size = -1;

#if defined(__linux__)
	uint64_t sz = 0;
	if (::ioctl(fd, BLKGETSIZE64, &sz) == 0)
		size = (sint64)sz;
#elif defined(__APPLE__)
	uint64_t blockCount = 0;
	uint32_t blockSize = 0;
	if (::ioctl(fd, DKIOCGETBLOCKCOUNT, &blockCount) == 0
		&& ::ioctl(fd, DKIOCGETBLOCKSIZE, &blockSize) == 0)
	{
		size = (sint64)(blockCount * blockSize);
	}
#endif

	if (size < 0) {
		off_t end = ::lseek(fd, 0, SEEK_END);
		if (end > 0)
			size = (sint64)end;
	}

	if (size < (sint64)kSectorSize) {
		const int err = errno;
		Shutdown();
		throw MyError("Cannot get size of physical disk %ls: %s",
			path, strerror(err));
	}

	mSectorCount = (uint32)((uint64)size / kSectorSize);
}

void ATIDEPhysicalDisk::Shutdown() {
	if (mpBuffer) {
		VDFile::FreeUnbuffer(mpBuffer);
		mpBuffer = nullptr;
	}

	int fd = FdFromHandle(mhDisk);
	if (fd >= 0) {
		::close(fd);
		mhDisk = HandleFromFd(-1);
	}
}

void ATIDEPhysicalDisk::Flush() {
}

void ATIDEPhysicalDisk::ReadSectors(void *data, uint32 lba, uint32 n) {
	int fd = FdFromHandle(mhDisk);
	if (fd < 0)
		throw MyError("Physical disk not open.");

	const off_t offset = (off_t)lba * (off_t)kSectorSize;
	uint8 *out = static_cast<uint8 *>(data);
	uint64 bytesRemaining = (uint64)n * kSectorSize;
	off_t pos = offset;

	while (bytesRemaining) {
		const size_t toread = bytesRemaining > kSectorSize * kReadBatchSectors
			? kSectorSize * kReadBatchSectors
			: (size_t)bytesRemaining;

		ssize_t actual = ::pread(fd, mpBuffer, toread, pos);
		if (actual < 0) {
			if (errno == EINTR)
				continue;
			throw MyError("Error reading from physical disk: %s.", strerror(errno));
		}
		if (actual == 0)
			throw MyError("Unexpected EOF reading from physical disk.");

		memcpy(out, mpBuffer, (size_t)actual);
		out += actual;
		pos += actual;
		bytesRemaining -= (uint64)actual;
	}
}

void ATIDEPhysicalDisk::WriteSectors(const void *, uint32, uint32) {
	// Read-only by design (matches Win32 IsReadOnly()==true).
}
