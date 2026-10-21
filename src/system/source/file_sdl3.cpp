//	Altirra - Atari 800/800XL/5200 emulator
//	System library - VDFile implementation for non-Windows (POSIX)

#include <stdafx.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "filemetadata_posix.h"
#include <sys/types.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <vd2/system/error.h>
#include <vd2/system/date.h>
#include <vd2/system/filesys.h>
#include <vd2/system/VDString.h>
#include <vd2/system/file.h>
#include <vd2/system/text.h>

using namespace nsVDFile;

static_assert(sizeof(off_t) >= sizeof(sint64),
	"The portable file backend requires 64-bit off_t (_FILE_OFFSET_BITS=64).");

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------

static VDDate TimeToVDDate(time_t t) {
	static const uint64 kEpochDiff = UINT64_C(116444736000000000);
	VDDate d;
	d.mTicks = kEpochDiff + (uint64)t * UINT64_C(10000000);
	return d;
}

static uint32 ThrowReadError(const wchar_t *path, int err) {
	throw MyError("Failed to read from file '%ls': %s", path ? path : L"<unknown>", strerror(err));
}

static uint32 ThrowWriteError(const wchar_t *path, int err) {
	throw MyError("Failed to write to file '%ls': %s", path ? path : L"<unknown>", strerror(err));
}

// -------------------------------------------------------------------------
// VDFile constructors / destructor
// -------------------------------------------------------------------------

VDFile::VDFile(const char *pszFileName, uint32 flags)
	: mhFile(-1)
{
	open(pszFileName, flags);
}

VDFile::VDFile(const wchar_t *pwszFileName, uint32 flags)
	: mhFile(-1)
{
	open(pwszFileName, flags);
}

VDFile::VDFile(VDFileHandle h)
	: mhFile(h)
{
	mFilePosition = (sint64)lseek(h, 0, SEEK_CUR);
}

vdnothrow VDFile::VDFile(VDFile&& other) vdnoexcept
	: mhFile(other.mhFile)
	, mpFilename(std::move(other.mpFilename))
	, mFilePosition(other.mFilePosition)
{
	other.mhFile = -1;
}

VDFile::~VDFile() {
	closeNT();
}

vdnothrow VDFile& VDFile::operator=(VDFile&& other) vdnoexcept {
	std::swap(mhFile, other.mhFile);
	std::swap(mpFilename, other.mpFilename);
	std::swap(mFilePosition, other.mFilePosition);
	return *this;
}

// -------------------------------------------------------------------------
// open_internal
// -------------------------------------------------------------------------

uint32 VDFile::open_internal(const char *pszFilename, const wchar_t *pwszFilename, uint32 flags) {
	closeNT();

	// Convert wide path to UTF-8 if needed
	VDStringA utf8;
	const char *path;
	if (pwszFilename) {
		utf8 = VDTextWToU8(pwszFilename, -1);
		path = utf8.c_str();
		mpFilename = wcsdup(pwszFilename);
	} else {
		path = pszFilename;
		if (pszFilename) {
			VDStringW wide = VDTextU8ToW(pszFilename, -1);
			mpFilename = wcsdup(wide.c_str());
		}
	}

	// Map nsVDFile flags to POSIX open flags
	int oflag = 0;

	uint32 access = flags & (kRead | kWrite);
	if (access == kRead)            oflag |= O_RDONLY;
	else if (access == kWrite)      oflag |= O_WRONLY;
	else                            oflag |= O_RDWR;

	uint32 creation = flags & kCreationMask;
	switch (creation) {
		case kOpenExisting:     break; // default (no O_CREAT)
		case kOpenAlways:       oflag |= O_CREAT; break;
		case kCreateAlways:     oflag |= O_CREAT | O_TRUNC; break;
		case kCreateNew:        oflag |= O_CREAT | O_EXCL; break;
		case kTruncateExisting: oflag |= O_TRUNC; break;
	}

	// Note: kDenyRead/kDenyWrite have no POSIX equivalent; ignore

	mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH; // 0644
	int fd = ::open(path, oflag, mode);
	if (fd < 0)
		return (uint32)errno;

	mhFile = fd;
	mFilePosition = 0;
	return 0;
}

// -------------------------------------------------------------------------
// open / openNT / tryOpen / openAlways
// -------------------------------------------------------------------------

void VDFile::open(const char *pszFilename, uint32 flags) {
	uint32 err = open_internal(pszFilename, nullptr, flags);
	if (err)
		throw MyError("Cannot open file '%s': %s", pszFilename, strerror((int)err));
}

void VDFile::open(const wchar_t *pwszFilename, uint32 flags) {
	uint32 err = open_internal(nullptr, pwszFilename, flags);
	if (err)
		throw MyError("Cannot open file '%ls': %s", pwszFilename, strerror((int)err));
}

bool VDFile::openNT(const wchar_t *pwszFilename, uint32 flags) {
	return open_internal(nullptr, pwszFilename, flags) == 0;
}

bool VDFile::tryOpen(const wchar_t *pwszFilename, uint32 flags) {
	return open_internal(nullptr, pwszFilename, flags) == 0;
}

bool VDFile::openAlways(const wchar_t *pwszFilename, uint32 flags) {
	bool existed = VDDoesPathExist(pwszFilename);
	uint32 err = open_internal(nullptr, pwszFilename, flags | kOpenAlways);
	if (err) return false;
	return existed;
}

// -------------------------------------------------------------------------
// close / truncate
// -------------------------------------------------------------------------

bool VDFile::closeNT() {
	if (mhFile >= 0) {
		::close(mhFile);
		mhFile = -1;
		return true;
	}
	return false;
}

void VDFile::close() {
	closeNT();
}

bool VDFile::truncateNT() {
	return ftruncate(mhFile, (off_t)mFilePosition) == 0;
}

void VDFile::truncate() {
	if (!truncateNT())
		throw MyError("Cannot truncate file: %s", strerror(errno));
}

bool VDFile::extendValidNT(sint64 pos) {
	// On Linux ftruncate extends with zeros if pos > current size
	return ftruncate(mhFile, (off_t)pos) == 0;
}

void VDFile::extendValid(sint64 pos) {
	if (!extendValidNT(pos))
		throw MyError("Cannot extend file: %s", strerror(errno));
}

bool VDFile::enableExtendValid() { return true; }

// -------------------------------------------------------------------------
// I/O
// -------------------------------------------------------------------------

sint64 VDFile::size() const {
	struct stat st;
	if (fstat(mhFile, &st) != 0)
		throw MyError("Cannot get file size: %s", strerror(errno));
	return (sint64)st.st_size;
}

void VDFile::read(void *buffer, sint32 length) {
	if (readData(buffer, length) != length)
		ThrowReadError(mpFilename, EIO);
}

sint32 VDFile::readData(void *buffer, sint32 length) {
	if (length <= 0) return 0;

	uint8 *dst = (uint8 *)buffer;
	sint32 total = 0;

	while (total < length) {
		ssize_t n = ::read(mhFile, dst + total, (size_t)(length - total));
		if (n < 0) {
			if (errno == EINTR) continue;
			ThrowReadError(mpFilename, errno);
		}
		if (n == 0) break; // EOF
		total += (sint32)n;
	}

	mFilePosition += total;
	return total;
}

void VDFile::write(const void *buffer, sint32 length) {
	if (writeData(buffer, length) != length)
		ThrowWriteError(mpFilename, EIO);
}

sint32 VDFile::writeData(const void *buffer, sint32 length) {
	if (length <= 0) return 0;

	const uint8 *src = (const uint8 *)buffer;
	sint32 total = 0;

	while (total < length) {
		ssize_t n = ::write(mhFile, src + total, (size_t)(length - total));
		if (n < 0) {
			if (errno == EINTR) continue;
			ThrowWriteError(mpFilename, errno);
		}
		total += (sint32)n;
	}

	mFilePosition += total;
	return total;
}

// -------------------------------------------------------------------------
// seek / tell
// -------------------------------------------------------------------------

bool VDFile::seekNT(sint64 newPos, nsVDFile::eSeekMode mode) {
	int whence;
	switch (mode) {
		case nsVDFile::kSeekStart: whence = SEEK_SET; break;
		case nsVDFile::kSeekCur:   whence = SEEK_CUR; break;
		case nsVDFile::kSeekEnd:   whence = SEEK_END; break;
		default:                   return false;
	}
	off_t result = lseek(mhFile, (off_t)newPos, whence);
	if (result < 0) return false;
	mFilePosition = (sint64)result;
	return true;
}

void VDFile::seek(sint64 newPos, nsVDFile::eSeekMode mode) {
	if (!seekNT(newPos, mode))
		throw MyError("Cannot seek in file: %s", strerror(errno));
}

bool VDFile::skipNT(sint64 delta) {
	return seekNT(delta, nsVDFile::kSeekCur);
}

void VDFile::skip(sint64 delta) {
	if (!skipNT(delta))
		throw MyError("Cannot skip in file: %s", strerror(errno));
}

sint64 VDFile::tell() const {
	return mFilePosition;
}

bool VDFile::flushNT() {
	return fsync(mhFile) == 0;
}

void VDFile::flush() {
	if (!flushNT())
		throw MyError("Cannot flush file: %s", strerror(errno));
}

bool VDFile::isOpen() const {
	return mhFile >= 0;
}

VDFileHandle VDFile::getRawHandle() const {
	return mhFile;
}

uint32 VDFile::getAttributes() const {
	struct stat st;
	if (fstat(mhFile, &st) != 0) return 0;
	uint32 attrs = 0;
	if (!(st.st_mode & S_IWUSR)) attrs |= 1; // read-only
	return attrs;
}

VDDate VDFile::getCreationTime() const {
	struct stat st;
	if (fstat(mhFile, &st) != 0) return VDDate{};
	return TimeToVDDate(st.st_ctime);
}

void VDFile::setCreationTime(const VDDate& date) {
	// Not supported on Linux
}

VDDate VDFile::getLastWriteTime() const {
	struct stat st;
	if (fstat(mhFile, &st) != 0) return VDDate{};
	return VDDate { VDPosixFileTime(VDPosixModificationTime(st)) };
}

void VDFile::setLastWriteTime(const VDDate& date) {
	// Not directly settable without utimes; skip
}

void *VDFile::AllocUnbuffer(size_t nBytes) {
	// O_DIRECT requires 512-byte alignment
	void *p = nullptr;
	posix_memalign(&p, 512, nBytes);
	return p;
}

void VDFile::FreeUnbuffer(void *p) {
	free(p);
}
