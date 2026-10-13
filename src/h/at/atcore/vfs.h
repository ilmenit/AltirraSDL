//	Altirra - Atari 800/800XL/5200 emulator
//	Core library - virtualized file system support
//	Copyright (C) 2009-2016 Avery Lee
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
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

//=========================================================================
// Virtualized file system support
//
// VFS support allows subsystems to access internal resources within
// bundles using filesystem-like paths. In particular, this allows
// referencing files inside of .zip files. Allowable syntaxes:
//
//	c:\foo.bin
//	file://c:/foo.bin
//	zip://c:/foo/bar.zip!foobar
//	gz://c:/foo/bar.gz
//	atfs://c:/foo/file.atr!foo.dcm

#ifndef f_AT_ATCORE_VFS_H
#define f_AT_ATCORE_VFS_H

#include <vd2/system/error.h>
#include <vd2/system/function.h>
#include <vd2/system/refcount.h>
#include <vd2/system/VDString.h>

class IVDRandomAccessStream;
class VDZipArchive;

class ATInvalidVFSPathException final : public VDException {
public:
	ATInvalidVFSPathException(const wchar_t *badPath);
};

class ATUnsupportedVFSPathException final : public VDException {
public:
	ATUnsupportedVFSPathException(const wchar_t *badPath);
};

class ATUnsupportedVFSPathDiskOnlyException final : public VDException {
public:
	ATUnsupportedVFSPathDiskOnlyException(const wchar_t *badPath);
};

class ATVFSException : public MyError {
public:
	template<typename... Args>
	ATVFSException(bool hasPath, Args&&... args)
		: mbHasPath(hasPath), MyError(std::forward<Args>(args)...)
	{
	}

	bool HasPath() const { return mbHasPath; }

	[[noreturn]]
	virtual void RethrowWithPath(const wchar_t *path) const = 0;

protected:
	bool mbHasPath = false;
};

template<typename T>
class ATVFSExceptionT : public ATVFSException {
public:
	using ATVFSException::ATVFSException;

	[[noreturn]]
	void RethrowWithPath(const wchar_t *path) const override {
		throw T(path);
	}
};

class ATVFSNotFoundException : public ATVFSExceptionT<ATVFSNotFoundException> {
public:
	ATVFSNotFoundException();
	ATVFSNotFoundException(const wchar_t *path);
};

class ATVFSReadOnlyException : public ATVFSExceptionT<ATVFSReadOnlyException> {
public:
	ATVFSReadOnlyException();
	ATVFSReadOnlyException(const wchar_t *path);
};

class ATVFSNotAvailableException : public ATVFSExceptionT<ATVFSNotAvailableException> {
public:
	ATVFSNotAvailableException();
	ATVFSNotAvailableException(const wchar_t *path);
};

enum ATVFSProtocol : uint8 {
	kATVFSProtocol_None,

	// file:// references a regular filesystem path, but with URL encoding. The path can be either
	// relative or absolute.
	kATVFSProtocol_File,

	// zip://file.zip!innerfile references innerfile within a zip archive.
	kATVFSProtocol_Zip,

	// gz://file.gz references a GZIP-compressed file (RFC1952).
	kATVFSProtocol_GZip,

	// atfs://image!innerfile references innerfile within a supported Atari 8-bit file system.
	kATVFSProtocol_Atfs,

	// blob://id accesses an in-memory blob (currently used by unit tests).
	kATVFSProtocol_Blob,

	// special://id accesses an internal data file.
	kATVFSProtocol_Special,

	// atvdisk://basepath?args accesses a virtual disk. This is only supported by disk mounting
	// paths.
	kATVFSProtocol_Atvdisk,
};

bool ATDecodeVFSString(VDStringW& dst, const VDStringSpanW& src, bool decodeAsQuery = false);
void ATEncodeVFSString(VDStringW& dst, const VDStringSpanW& src, bool filepath);

struct ATVFSPathParam {
	uint32 mNameHash;
	VDStringW mName;
	VDStringW mValue;
};

using ATVFSPathParams = vdvector<ATVFSPathParam>;

struct ATVFSParsedPath {
	ATVFSProtocol mProtocol {};
	VDStringW mBasePath;
	VDStringW mSubPath;
	ATVFSPathParams mParams;

	bool IsValid() const { return mProtocol != kATVFSProtocol_None; }
	const wchar_t *FindParam(const wchar_t *name, const wchar_t *defaultValue = nullptr) const;
};

ATVFSProtocol ATParseVFSPath(const wchar_t *s, VDStringW& basePath, VDStringW& subPath, ATVFSPathParams* params = nullptr);
ATVFSParsedPath ATParseVFSPath(const wchar_t *s);

VDStringW ATMakeVFSPath(ATVFSProtocol protocol, const wchar_t *basePath, const wchar_t *subPath);
VDStringW ATMakeVFSPath(const wchar_t *protocolAndBasePath, const wchar_t *subPath);

// Given a path, return the path to the parent. Examples:
//
//	C:			-> C:
//	C:\			-> C:\
//	C:\x		-> C:\
//	C:\x\		-> C:\
//	file://x/y	-> file://x/
//	zip://a/b!	-> zip://a/b!
//	zip://a/b!c	-> zip://a/b!
//	zip://a/b!c/d -> zip://a/b!c/

const wchar_t *ATVFSSplitPathFile(const wchar_t *path);

// Returns true if the supplied path is a VFS file path. It must directly point to a file.
bool ATVFSIsFilePath(const wchar_t *s);

// Returns true if the supplied path contains a VFS file path. It can be a nested path that
// has a root at a file path.
bool ATVFSExtractFilePath(const wchar_t *s, VDStringW *filePathOpt);

VDStringW ATMakeVFSPathForGZipFile(const wchar_t *path);
VDStringW ATMakeVFSPathForZipFile(const wchar_t *path, const wchar_t *fileName);

class ATVFSFileView;

using ATVFSOpenSiblingFn = vdfunction<vdrefptr<ATVFSFileView>(const wchar_t *name)>;

class ATVFSFileView : public vdrefcount {
	ATVFSFileView(const ATVFSFileView&) = delete;
	ATVFSFileView& operator=(const ATVFSFileView&) = delete;

public:
	ATVFSFileView() = default;
	virtual ~ATVFSFileView() = default;

	IVDRandomAccessStream& GetStream() { return *mpStream; }
	const wchar_t *GetFileName() const { return mFileName.c_str(); }
	virtual bool IsSourceReadOnly() const = 0;

	void SetTryOpenSibling(ATVFSOpenSiblingFn fn);
	vdrefptr<ATVFSFileView> TryOpenSibling(const wchar_t *name) const;

protected:
	IVDRandomAccessStream *mpStream;
	VDStringW mFileName;

	ATVFSOpenSiblingFn mpOpenSiblingFn;
};

// View of a zip archive that is extended to provide VFS view oriented open
// methods.
class IATVFSZipArchive : public IVDRefCount {
public:
	virtual VDZipArchive& GetZipArchive() = 0;
	virtual vdrefptr<ATVFSFileView> OpenStream(sint32 idx) = 0;
	virtual vdrefptr<ATVFSFileView> TryOpenStream(const wchar_t *subfile) = 0;
};

vdrefptr<IATVFSZipArchive> ATVFSOpenZipArchiveFromView(ATVFSFileView& view);

void ATVFSOpenFileView(const wchar_t *vfsPath, bool write, ATVFSFileView **viewOut);
void ATVFSOpenFileView(const wchar_t *vfsPath, bool write, bool update, ATVFSFileView **viewOut);
vdrefptr<ATVFSFileView> ATVFSOpenFileView(const wchar_t *vfsPath, const ATVFSParsedPath& parsedPath, bool write, bool update);

vdrefptr<ATVFSFileView> ATVFSWrapStream(IVDRandomAccessStream& stream, const wchar_t *imagePath);

void ATVFSSetAtfsProtocolHandler(vdfunction<void(ATVFSFileView *, const wchar_t *, ATVFSFileView **)> handler);
void ATVFSSetBlobProtocolHandler(vdfunction<void(const wchar_t *, bool, bool, ATVFSFileView **)> handler);
void ATVFSSetSpecialProtocolHandler(vdfunction<void(const wchar_t *, bool, bool, ATVFSFileView **)> handler);

#endif
