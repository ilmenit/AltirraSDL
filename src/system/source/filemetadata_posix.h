// POSIX metadata conversion shared by the SDL system backends.
#pragma once
#include <sys/stat.h>
#include <vd2/system/vdtypes.h>

inline timespec VDPosixModificationTime(const struct stat& info) {
#ifdef __APPLE__
	return info.st_mtimespec;
#else
	return info.st_mtim;
#endif
}

inline timespec VDPosixChangeTime(const struct stat& info) {
#ifdef __APPLE__
	return info.st_ctimespec;
#else
	return info.st_ctim;
#endif
}

// Match Windows FILETIME/VDDate: 100ns ticks since 1601, not Unix seconds.
inline uint64 VDPosixFileTime(const timespec& time) {
	return UINT64_C(116444736000000000)
		+ (uint64)time.tv_sec * UINT64_C(10000000)
		+ (uint64)time.tv_nsec / 100;
}
