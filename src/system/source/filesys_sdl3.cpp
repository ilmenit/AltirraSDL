//	Altirra - Atari 800/800XL/5200 emulator
//	System library - filesystem functions for non-Windows (POSIX)

#include <stdafx.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <vector>

#include <vd2/system/vdtypes.h>
#include <vd2/system/filesys.h>
#include <vd2/system/error.h>
#include <vd2/system/text.h>
#include <vd2/system/VDString.h>
#include <vd2/system/date.h>

// -------------------------------------------------------------------------
// Path utilities (all work on wchar_t paths, convert to UTF-8 for OS calls)
// -------------------------------------------------------------------------

static VDStringA ToUTF8(const wchar_t *ws) {
	return ws ? VDTextWToU8(ws, -1) : VDStringA();
}

static VDStringW ToWide(const char *s) {
	return s ? VDTextU8ToW(s, -1) : VDStringW();
}

// -------------------------------------------------------------------------
// Path splitting (pure string ops, no OS calls needed)
// -------------------------------------------------------------------------

const wchar_t *VDFileSplitPath(const wchar_t *s) {
	const wchar_t *last = s;
	while (*s) {
		if (*s == L'/' || *s == L'\\')
			last = s + 1;
		++s;
	}
	return last;
}

const char *VDFileSplitPath(const char *s) {
	const char *last = s;
	while (*s) {
		if (*s == '/' || *s == '\\')
			last = s + 1;
		++s;
	}
	return last;
}

const wchar_t *VDFileSplitFirstDir(const wchar_t *s) {
	while (*s && *s != L'/' && *s != L'\\')
		++s;
	if (*s) ++s;
	return s;
}

const char *VDFileSplitFirstDir(const char *s) {
	while (*s && *s != '/' && *s != '\\')
		++s;
	if (*s) ++s;
	return s;
}

VDStringW VDFileSplitPathLeft(const VDStringW& s) {
	const wchar_t *base = s.c_str();
	const wchar_t *p = VDFileSplitPath(base);
	if (p == base) return VDStringW();
	return VDStringW(base, p);
}

VDStringW VDFileSplitPathRight(const VDStringW& s) {
	return VDStringW(VDFileSplitPath(s.c_str()));
}

VDString VDFileSplitPathLeft(const VDString& s) {
	const char *base = s.c_str();
	const char *p = VDFileSplitPath(base);
	if (p == base) return VDString();
	return VDString(base, p);
}

VDString VDFileSplitPathRight(const VDString& s) {
	return VDString(VDFileSplitPath(s.c_str()));
}

VDStringSpanW VDFileSplitPathLeftSpan(const VDStringSpanW& s) {
	const wchar_t *base = s.data();
	const wchar_t *end  = base + s.size();
	const wchar_t *p = base;
	const wchar_t *last = base;
	while (p != end) {
		if (*p == L'/' || *p == L'\\')
			last = p + 1;
		++p;
	}
	return VDStringSpanW(base, last);
}

VDStringSpanW VDFileSplitPathRightSpan(const VDStringSpanW& s) {
	const wchar_t *base = s.data();
	const wchar_t *end  = base + s.size();
	const wchar_t *p = base;
	const wchar_t *last = base;
	while (p != end) {
		if (*p == L'/' || *p == L'\\')
			last = p + 1;
		++p;
	}
	return VDStringSpanW(last, end);
}

VDStringSpanA VDFileSplitPathLeftSpan(const VDStringSpanA& s) {
	const char *base = s.data();
	const char *end  = base + s.size();
	const char *last = base;
	for (const char *p = base; p != end; ++p)
		if (*p == '/' || *p == '\\')
			last = p + 1;
	return VDStringSpanA(base, last);
}

VDStringSpanA VDFileSplitPathRightSpan(const VDStringSpanA& s) {
	const char *base = s.data();
	const char *end  = base + s.size();
	const char *last = base;
	for (const char *p = base; p != end; ++p)
		if (*p == '/' || *p == '\\')
			last = p + 1;
	return VDStringSpanA(last, end);
}

const wchar_t *VDFileSplitRoot(const wchar_t *s) {
	// On Linux, root is '/' at index 0
	if (s[0] == L'/') return s + 1;
	return s;
}

const char *VDFileSplitRoot(const char *s) {
	if (s[0] == '/') return s + 1;
	return s;
}

VDStringW VDFileSplitRoot(const VDStringW& s) {
	return VDStringW(VDFileSplitRoot(s.c_str()));
}

VDString VDFileSplitRoot(const VDString& s) {
	return VDString(VDFileSplitRoot(s.c_str()));
}

const wchar_t *VDFileSplitExt(const wchar_t *s) {
	const wchar_t *name = VDFileSplitPath(s);
	const wchar_t *dot  = nullptr;
	while (*name) {
		if (*name == L'.') dot = name;
		++name;
	}
	return dot ? dot : name;
}

const char *VDFileSplitExt(const char *s) {
	const char *name = VDFileSplitPath(s);
	const char *dot  = nullptr;
	while (*name) {
		if (*name == '.') dot = name;
		++name;
	}
	return dot ? dot : name;
}

VDStringW VDFileSplitExtLeft(const VDStringW& s) {
	const wchar_t *ext = VDFileSplitExt(s.c_str());
	return VDStringW(s.c_str(), ext);
}

VDStringW VDFileSplitExtRight(const VDStringW& s) {
	return VDStringW(VDFileSplitExt(s.c_str()));
}

VDString VDFileSplitExtLeft(const VDString& s) {
	const char *ext = VDFileSplitExt(s.c_str());
	return VDString(s.c_str(), ext);
}

VDString VDFileSplitExtRight(const VDString& s) {
	return VDString(VDFileSplitExt(s.c_str()));
}

// Range-based extension split: finds the '.' of the last extension within
// the half-open range [s, t), stopping at the last path separator.  If no
// extension is present, returns t.  All returned pointers are inside the
// same [s, t) range as the input, so they can be safely used to build a
// VDStringSpan without touching foreign memory.
//
// NOTE: the previous implementations of the *Span variants below constructed
// a temporary VDString/VDStringW to NUL-terminate the input, called
// VDFileSplitExt on the temporary, then built a span mixing a pointer into
// the temporary with a pointer into the caller's buffer.  That was doubly
// wrong: (1) the pointer into the temporary dangled as soon as the temporary
// was destroyed at end-of-statement, and (2) subtracting pointers from two
// unrelated allocations is undefined behaviour and produced garbage span
// lengths.  The resulting spans caused large out-of-bounds memcpy crashes in
// ATSimulator::LoadCartridge() when it tried to copy the "symbol hint path"
// (see the backtrace where LoadMountedImages -> LoadCartridge crashed in
// __memmove_avx_unaligned_erms with rdx ~= 2802 wide chars for a ~30-char
// source path).  The correct implementation mirrors filesys.cpp's range
// overload and stays entirely within the caller's buffer.
namespace {
	const char *vd_split_ext_range(const char *s, const char *t) {
		const char *const end = t;
		while (t > s) {
			--t;
			if (*t == '.')
				return t;
			if (*t == ':' || *t == '\\' || *t == '/')
				break;
		}
		return end;
	}

	const wchar_t *vd_split_ext_range(const wchar_t *s, const wchar_t *t) {
		const wchar_t *const end = t;
		while (t > s) {
			--t;
			if (*t == L'.')
				return t;
			if (*t == L':' || *t == L'\\' || *t == L'/')
				break;
		}
		return end;
	}
}

VDStringSpanA VDFileSplitExtLeftSpan(const VDStringSpanA& s) {
	const char *begin = s.data();
	const char *end   = begin + s.size();
	return VDStringSpanA(begin, vd_split_ext_range(begin, end));
}

VDStringSpanA VDFileSplitExtRightSpan(const VDStringSpanA& s) {
	const char *begin = s.data();
	const char *end   = begin + s.size();
	return VDStringSpanA(vd_split_ext_range(begin, end), end);
}

VDStringSpanW VDFileSplitExtLeftSpan(const VDStringSpanW& s) {
	const wchar_t *begin = s.data();
	const wchar_t *end   = begin + s.size();
	return VDStringSpanW(begin, vd_split_ext_range(begin, end));
}

VDStringSpanW VDFileSplitExtRightSpan(const VDStringSpanW& s) {
	const wchar_t *begin = s.data();
	const wchar_t *end   = begin + s.size();
	return VDStringSpanW(vd_split_ext_range(begin, end), end);
}

// -------------------------------------------------------------------------
// Wildcard matching
// -------------------------------------------------------------------------

static bool WildMatch(const wchar_t *p, const wchar_t *s) {
	while (*p) {
		if (*p == L'*') {
			while (*p == L'*') ++p;
			if (!*p) return true;
			while (*s) {
				if (WildMatch(p, s)) return true;
				++s;
			}
			return false;
		} else if (*p == L'?') {
			if (!*s) return false;
			++p; ++s;
		} else {
			if (towlower(*p) != towlower(*s)) return false;
			++p; ++s;
		}
	}
	return !*s;
}

static bool WildMatchA(const char *p, const char *s) {
	while (*p) {
		if (*p == '*') {
			while (*p == '*') ++p;
			if (!*p) return true;
			while (*s) {
				if (WildMatchA(p, s)) return true;
				++s;
			}
			return false;
		} else if (*p == '?') {
			if (!*s) return false;
			++p; ++s;
		} else {
			if (tolower((uint8)*p) != tolower((uint8)*s)) return false;
			++p; ++s;
		}
	}
	return !*s;
}

bool VDFileWildMatch(const wchar_t *pattern, const wchar_t *path) {
	return WildMatch(pattern, VDFileSplitPath(path));
}

bool VDFileWildMatch(const char *pattern, const char *path) {
	return WildMatchA(pattern, VDFileSplitPath(path));
}

// -------------------------------------------------------------------------
// VDParsedPath
// -------------------------------------------------------------------------

VDParsedPath::VDParsedPath()
	: mbIsRelative(true)
{
}

VDParsedPath::VDParsedPath(const wchar_t *path) {
	mbIsRelative = (*path != L'/');
	if (!mbIsRelative) {
		mRoot = L"/";
		++path;
	}
	VDStringW seg;
	while (*path) {
		if (*path == L'/' || *path == L'\\') {
			if (!seg.empty()) {
				if (seg == L"..") {
					if (!mComponents.empty()) mComponents.pop_back();
				} else if (seg != L".") {
					mComponents.push_back_as(seg.c_str());
				}
				seg.clear();
			}
		} else {
			seg += *path;
		}
		++path;
	}
	if (!seg.empty() && seg != L".")
		mComponents.push_back_as(seg.c_str());
}

VDStringW VDParsedPath::ToString() const {
	VDStringW result = mRoot;
	for (size_t i = 0; i < mComponents.size(); ++i) {
		if (i > 0 || !mRoot.empty()) result += L'/';
		result += mComponents[i].c_str();
	}
	return result;
}

// -------------------------------------------------------------------------
// Canonical / relative path
// -------------------------------------------------------------------------

VDStringW VDFileGetCanonicalPath(const wchar_t *path) {
	return VDParsedPath(path).ToString();
}

bool VDFileIsRelativePath(const wchar_t *path) {
	return path && path[0] != L'/' && path[0] != L'\\';
}

VDStringW VDFileResolvePath(const wchar_t *basePath, const wchar_t *pathToResolve) {
	if (!VDFileIsRelativePath(pathToResolve))
		return VDStringW(pathToResolve);

	VDStringW combined = VDStringW(basePath) + L"/" + VDStringW(pathToResolve);
	return VDFileGetCanonicalPath(combined.c_str());
}

VDStringW VDFileGetRelativePath(const wchar_t *basePath, const wchar_t *pathToConvert, bool allowAscent) {
	// Simple implementation: return the full path if it can't be made relative
	return VDStringW(pathToConvert);
}

// -------------------------------------------------------------------------
// Disk / directory operations
// -------------------------------------------------------------------------

sint64 VDGetDiskFreeSpace(const wchar_t *path) {
	struct statvfs sv;
	if (statvfs(ToUTF8(path).c_str(), &sv) != 0) return -1;
	return (sint64)sv.f_bavail * (sint64)sv.f_frsize;
}

void VDCreateDirectory(const wchar_t *path) {
	if (mkdir(ToUTF8(path).c_str(), 0755) != 0 && errno != EEXIST)
		throw MyError("Cannot create directory '%ls': %s", path, strerror(errno));
}

void VDRemoveDirectory(const wchar_t *path) {
	if (rmdir(ToUTF8(path).c_str()) != 0)
		throw MyError("Cannot remove directory '%ls': %s", path, strerror(errno));
}

void VDSetDirectoryCreationTime(const wchar_t *path, const VDDate& date) {
	// No creation time concept on Linux; no-op
}

bool VDRemoveFile(const wchar_t *path) {
	return unlink(ToUTF8(path).c_str()) == 0;
}

void VDRemoveFileEx(const wchar_t *path) {
	if (!VDRemoveFile(path))
		throw MyError("Cannot remove file '%ls': %s", path, strerror(errno));
}

void VDMoveFile(const wchar_t *srcPath, const wchar_t *dstPath) {
	if (rename(ToUTF8(srcPath).c_str(), ToUTF8(dstPath).c_str()) != 0)
		throw MyError("Cannot move file: %s", strerror(errno));
}

bool VDDoesPathExist(const wchar_t *fileName) {
	struct stat st;
	return stat(ToUTF8(fileName).c_str(), &st) == 0;
}

uint64 VDFileGetLastWriteTime(const wchar_t *path) {
	struct stat st;
	if (stat(ToUTF8(path).c_str(), &st) != 0) return 0;
	return (uint64)st.st_mtime;
}

VDStringW VDFileGetRootPath(const wchar_t *partialPath) {
	// On Linux, root is always "/"
	return VDStringW(L"/");
}

VDStringW VDGetFullPath(const wchar_t *partialPath) {
	VDStringA utf8 = ToUTF8(partialPath);
	char resolved[PATH_MAX];
	if (!realpath(utf8.c_str(), resolved))
		return VDStringW(partialPath);
	return ToWide(resolved);
}

VDStringW VDGetLongPath(const wchar_t *path) {
	return VDGetFullPath(path);
}

VDStringW VDMakePath(const wchar_t *base, const wchar_t *file) {
	if (!base || !*base) return VDStringW(file);
	VDStringW result(base);
	wchar_t last = result.back();
	if (last != L'/' && last != L'\\')
		result += L'/';
	result += file;
	return result;
}

VDStringW VDMakePath(const VDStringSpanW& base, const VDStringSpanW& file) {
	return VDMakePath(VDStringW(base).c_str(), VDStringW(file).c_str());
}

bool VDFileIsPathEqual(const wchar_t *path1, const wchar_t *path2) {
	// On Linux filesystems are case-sensitive
	return wcscmp(path1, path2) == 0;
}

void VDFileFixDirPath(VDStringW& path) {
	if (!path.empty() && path.back() != L'/')
		path += L'/';
}

VDStringW VDGetLocalModulePath() {
	char buf[PATH_MAX];
	ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if (n < 0) return VDStringW();
	buf[n] = '\0';
	// Return directory containing the executable
	char *slash = strrchr(buf, '/');
	if (slash) *slash = '\0';
	return ToWide(buf);
}

VDStringW VDGetProgramPath() {
	return VDGetLocalModulePath();
}

VDStringW VDGetProgramFilePath() {
	char buf[PATH_MAX];
	ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if (n < 0) return VDStringW();
	buf[n] = '\0';
	return ToWide(buf);
}

VDStringW VDGetSystemPath() {
	return ToWide("/usr/lib");
}

void VDGetRootPaths(vdvector<VDStringW>& paths) {
	paths.clear();
	paths.push_back_as(L"/");
}

VDStringW VDGetRootVolumeLabel(const wchar_t *rootPath) {
	return VDStringW();
}

// -------------------------------------------------------------------------
// File attributes
// -------------------------------------------------------------------------

static VDDate TimespecToVDDate(time_t t) {
	// VDDate is a Windows FILETIME (100ns intervals since 1601-01-01)
	// Linux time_t is seconds since 1970-01-01 (Unix epoch)
	// Difference in 100ns intervals: 116444736000000000
	static const uint64 kEpochDiff = UINT64_C(116444736000000000);
	VDDate d;
	d.mTicks = kEpochDiff + (uint64)t * UINT64_C(10000000);
	return d;
}

uint32 VDFileGetAttributes(const wchar_t *path) {
	struct stat st;
	if (stat(ToUTF8(path).c_str(), &st) != 0)
		return kVDFileAttr_Invalid;

	uint32 attrs = 0;
	if (S_ISDIR(st.st_mode)) attrs |= kVDFileAttr_Directory;
	if (!(st.st_mode & S_IWUSR)) attrs |= kVDFileAttr_ReadOnly;
	if (S_ISLNK(st.st_mode)) attrs |= kVDFileAttr_Link;
	return attrs;
}

void VDFileSetAttributes(const wchar_t *path, uint32 attrsToChange, uint32 newAttrs) {
	if (attrsToChange & kVDFileAttr_ReadOnly) {
		struct stat st;
		if (stat(ToUTF8(path).c_str(), &st) == 0) {
			mode_t mode = st.st_mode;
			if (newAttrs & kVDFileAttr_ReadOnly)
				mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
			else
				mode |= S_IWUSR;
			chmod(ToUTF8(path).c_str(), mode);
		}
	}
}

// -------------------------------------------------------------------------
// VDDirectoryIterator
// -------------------------------------------------------------------------

VDDirectoryIterator::VDDirectoryIterator(const wchar_t *path)
	: mpHandle(nullptr)
	, mbSearchComplete(false)
	, mFileSize(0)
	, mbDirectory(false)
	, mAttributes(0)
{
	// path may include a wildcard at the end (Windows-style "C:\dir\*.*")
	// On Linux: strip the trailing wildcard and use it as a filter
	VDStringW dir(path);
	VDStringW filter;

	// Find last path separator
	const wchar_t *raw = dir.c_str();
	const wchar_t *lastSepPtr = nullptr;
	for (const wchar_t *p = raw; *p; ++p)
		if (*p == L'/' || *p == L'\\')
			lastSepPtr = p;

	// Check if the last component is a wildcard
	const wchar_t *last = lastSepPtr ? lastSepPtr + 1 : raw;
	if (wcschr(last, L'*') || wcschr(last, L'?')) {
		filter = last;

		// Win32's FindFirstFile treats "*.*" as "everything", including
		// names with no dot at all -- an MS-DOS legacy quirk that lives in
		// the filesystem call, not in the wildcard matcher.  Callers written
		// against the Windows build rely on it: the H: host device searches
		// with "<dir>\*.*" and would otherwise never see a subdirectory
		// (directory names rarely contain a dot), so XIO 41 could not change
		// into one and directory listings dropped every dotless entry.
		// VDFileWildMatch is a plain portable matcher and requires the
		// literal '.', so normalise the pattern here instead.
		if (filter == L"*.*")
			filter = L"*";

		if (lastSepPtr) {
			size_t sepIdx = (size_t)(lastSepPtr - raw);
			dir.resize(sepIdx);
		} else {
			dir = L".";
		}
	}

	VDStringA utf8dir = ToUTF8(dir.c_str());
	mBasePath = dir;
	VDFileFixDirPath(mBasePath);
	mSearchPath = VDStringW(path);

	DIR *d = opendir(utf8dir.c_str());
	if (!d) { mbSearchComplete = true; return; }

	// Store DIR* and filter as a pair
	struct DirState {
		DIR *dir;
		VDStringW filter;
	};
	DirState *state = new DirState;
	state->dir = d;
	state->filter = filter;
	mpHandle = state;
}

VDDirectoryIterator::~VDDirectoryIterator() {
	if (mpHandle) {
		struct DirState { DIR *dir; VDStringW filter; };
		DirState *state = (DirState *)mpHandle;
		closedir(state->dir);
		delete state;
		mpHandle = nullptr;
	}
}

bool VDDirectoryIterator::Next() {
	if (!mpHandle || mbSearchComplete) return false;

	struct DirState { DIR *dir; VDStringW filter; };
	DirState *state = (DirState *)mpHandle;

	for (;;) {
		struct dirent *ent = readdir(state->dir);
		if (!ent) { mbSearchComplete = true; return false; }

		// Skip . and ..
		if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
			continue;

		mFilename = ToWide(ent->d_name);

		// Apply wildcard filter
		if (!state->filter.empty() && !VDFileWildMatch(state->filter.c_str(), mFilename.c_str()))
			continue;

		// Stat the entry
		VDStringW fullPath = mBasePath + mFilename;
		struct stat st;
		if (lstat(ToUTF8(fullPath.c_str()).c_str(), &st) != 0) continue;

		mFileSize   = st.st_size;
		mbDirectory = S_ISDIR(st.st_mode);
		mAttributes = 0;
		if (mbDirectory)           mAttributes |= kVDFileAttr_Directory;
		if (S_ISLNK(st.st_mode))   mAttributes |= kVDFileAttr_Link;
		if (!(st.st_mode & S_IWUSR)) mAttributes |= kVDFileAttr_ReadOnly;

		mCreationDate  = TimespecToVDDate(st.st_ctime);
		mLastWriteDate = TimespecToVDDate(st.st_mtime);

		return true;
	}
}

bool VDDirectoryIterator::ResolveLinkSize() {
	if (!(mAttributes & kVDFileAttr_Link)) return true;
	VDStringW fullPath = mBasePath + mFilename;
	struct stat st;
	if (stat(ToUTF8(fullPath.c_str()).c_str(), &st) != 0) return false;
	mFileSize = st.st_size;
	return true;
}

// -------------------------------------------------------------------------
// Date utilities (needed by file timestamps)
// -------------------------------------------------------------------------

#include <vd2/system/date.h>

// VDDate is defined in date.h — just provide the stubs needed
