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

#include <stdafx.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/hash.h>
#include <vd2/system/strutil.h>
#include <vd2/system/zip.h>
#include <at/atcore/vfs.h>

vdfunction<void(ATVFSFileView *, const wchar_t *, ATVFSFileView **)> g_ATVFSAtfsHandler;
vdfunction<void(const wchar_t *, bool, bool, ATVFSFileView **)> g_ATVFSBlobHandler;
vdfunction<void(const wchar_t *, bool, bool, ATVFSFileView **)> g_ATVFSSpecialHandler;

ATInvalidVFSPathException::ATInvalidVFSPathException(const wchar_t *badPath)
	: VDException(L"Invalid VFS path: %s.", badPath)
{
}

ATUnsupportedVFSPathException::ATUnsupportedVFSPathException(const wchar_t *badPath)
	: VDException(L"Unsupported VFS path: %s.", badPath)
{
}

ATUnsupportedVFSPathDiskOnlyException::ATUnsupportedVFSPathDiskOnlyException(const wchar_t *badPath)
	: VDException(L"VFS path can only be used for a disk image and not a file: %s.", badPath)
{
}

ATVFSNotFoundException::ATVFSNotFoundException()
	: ATVFSExceptionT(false, "VFS path not found.")
{
}

ATVFSNotFoundException::ATVFSNotFoundException(const wchar_t *path)
	: ATVFSExceptionT(true, L"VFS path not found: %ls.", path)
{
}

ATVFSReadOnlyException::ATVFSReadOnlyException()
	: ATVFSExceptionT(false, "VFS path is read only.")
{
}

ATVFSReadOnlyException::ATVFSReadOnlyException(const wchar_t *path)
	: ATVFSExceptionT(true, L"VFS path is read only: %ls.", path)
{
}

ATVFSNotAvailableException::ATVFSNotAvailableException()
	: ATVFSExceptionT(false, "VFS path is not available.")
{
}

ATVFSNotAvailableException::ATVFSNotAvailableException(const wchar_t *path)
	: ATVFSExceptionT(true, L"VFS path is not available: %ls.", path)
{
}

///////////////////////////////////////////////////////////////////////////////

bool ATDecodeVFSString(VDStringW& dst, const VDStringSpanW& src, bool decodeAsQuery) {
	auto it = src.begin();
	const auto itEnd = src.end();

	if (it == itEnd)
		return false;

	uint32 unicode = 0;
	uint8 extsleft = 0;

	while(it != itEnd) {
		wchar_t c = *it++;

		if (c == '%') {
			if (itEnd - it < 2)
				return false;

			uint8 code = 0;
			for(int i=0; i<2; ++i) {
				c = *it++;

				code <<= 4;
				if (c >= '0' && c <= '9')
					code += (uint8)(c - '0');
				else if (c >= 'a' && c <= 'f')
					code += (uint8)((c - 'a') + 10);
				else if (c >= 'A' && c <= 'F')
					code += (uint8)((c - 'A') + 10);
				else
					return false;
			}

			if (extsleft) {
				// next byte must be an extension byte
				if (code < 0x80 || code >= 0xC0)
					return false;

				unicode = (unicode << 6) + (uint32)(code - 0x80);

				if (--extsleft)
					continue;

				// direct encoding of surrogate code points is invalid
				if ((uint32)(unicode - 0xD800) < 0x800)
					return false;

				// check if we need to emit a surrogate
				if constexpr (sizeof(wchar_t) < 4) {
					if (unicode >= 0x10000) {
						dst += (wchar_t)(0xD800 + ((unicode - 0x10000) >> 10));

						c = (wchar_t)(0xDC00 + (unicode & 0x3FF));
					} else {
						c = (wchar_t)unicode;
					}
				} else {
					c = (wchar_t)unicode;
				}
			} else if (code < 0x80)
				c = code;
			else if (code < 0xC0)
				return false;
			else if (code < 0xE0) {
				unicode = code & 0x1F;
				extsleft = 1;
				continue;
			} else if (code < 0xF0) {
				unicode = code & 0x0F;
				extsleft = 2;
				continue;
			} else if (code < 0xF8) {
				unicode = code & 0x07;
				extsleft = 3;
				continue;
			} else {
				// U+200000 or above is invalid
				return false;
			}
		} else {
			// check for unterminated UTF-8 sequence
			if (extsleft)
				return false;

			// for a query string specifically, convert + to space
			if (c == L'+' && decodeAsQuery)
				c = L' ';
		}

		dst += c;
	}

	// check for unterminated UTF-8 sequence
	if (extsleft)
		return false;

	return true;
}

void ATEncodeVFSString(VDStringW& dst, const VDStringSpanW& src, bool filepath) {
	uint32 surrogateOffset = 0;

	for(wchar_t cw : src) {
		uint32 c = cw;

		if constexpr (sizeof(wchar_t) == 2)
			c &= 0xFFFF;

		if ((uint32)(c - 0xD800) < 0x800) {
			if (c < 0xDC00) {
				surrogateOffset = ((c - 0xD800) << 10) + 0x10000;
				continue;
			}

			c = (c - 0xDC00) + surrogateOffset;
			surrogateOffset = 0;
		}

		if (filepath && c == L'\\')
			c = L'/';

		// Apply URL escaping according to RFC3986.
		bool escapingNeeded = false;
		switch(c) {
			case 0x21:	// !
			case 0x23:	// #
			case 0x24:	// $
			case 0x25:	// %
			case 0x26:	// &
			case 0x27:	// '
			case 0x28:	// (
			case 0x29:	// )
			case 0x2A:	// *
			case 0x2B:	// +
			case 0x2C:	// ,
			case 0x3B:	// ;
			case 0x3D:	// =
			case 0x3F:	// ?
			case 0x40:	// @
			case 0x5B:	// [
			case 0x5D:	// ]
				escapingNeeded = true;
				break;

			default:
				if (c < 0x20 || c > 0x7E)
					escapingNeeded = true;
				break;
		}

		if (escapingNeeded) {
			if (c >= 0x10000) {
				dst.append_sprintf(L"%%%02X%%%02X%%%02X%%%02X", 0xF0 + ((c >> 18) & 0x07), 0x80 + ((c >> 12) & 0x3F), 0x80 + ((c >> 6) & 0x3F), 0x80 + (c & 0x3F));
			} else if (c >= 0x800) {
				dst.append_sprintf(L"%%%02X%%%02X%%%02X", 0xE0 + (c >> 12), 0x80 + ((c >> 6) & 0x3F), 0x80 + (c & 0x3F));
			} else if (c >= 0x80) {
				dst.append_sprintf(L"%%%02X%%%02X", 0xC0 + (c >> 6), 0x80 + (c & 0x3F));
			} else {
				dst.append_sprintf(L"%%%02X", c);
			}
		} else {
			dst += (wchar_t)c;
		}
	}
}

const wchar_t *ATVFSParsedPath::FindParam(const wchar_t *name, const wchar_t *defaultValue) const {
	uint32 nameHash = VDHashString32I(name);

	for(const ATVFSPathParam& param : mParams) {
		if (param.mNameHash == nameHash && param.mName.comparei(name) == 0)
			return param.mValue.c_str();
	}

	return defaultValue;
}

ATVFSProtocol ATParseVFSPath(const wchar_t *s, VDStringW& basePath, VDStringW& subPath, ATVFSPathParams* params) {
	ATVFSParsedPath parsedPath = ATParseVFSPath(s);

	if (!parsedPath.IsValid())
		return kATVFSProtocol_None;

	basePath = std::move(parsedPath.mBasePath);
	subPath = std::move(parsedPath.mSubPath);

	if (params)
		*params = std::move(parsedPath.mParams);

	return parsedPath.mProtocol;
}

ATVFSParsedPath ATParseVFSPath(const wchar_t *s) {
	ATVFSParsedPath parsedPath;

	// check for a protocol
	const wchar_t *colon = wcschr(s, ':');
	if (!colon || colon - s < 2 || colon[1] != '/' || colon[2] != '/') {
		// no protocol -- assume it's a straight path
		parsedPath.mProtocol = kATVFSProtocol_File;
		parsedPath.mBasePath = VDFileGetCanonicalPath(s);

		return parsedPath;
	}

	// check for arguments
	VDStringSpanW relPath(colon + 3);

	auto argPos = relPath.rfind('?');
	if (argPos != relPath.npos) {
		// split off the arguments
		VDStringSpanW args = relPath.subspan(argPos + 1);
		relPath = relPath.subspan(0, argPos);

		// parse the arguments; if no arguments were requested, validate but discard
		while(!args.empty()) {
			const auto nextArgPos = args.find(L'&');
			const VDStringSpanW arg = args.subspan(0, nextArgPos);

			const auto argSplitPos = arg.find(L'=');
			if (argSplitPos == arg.npos)
				return ATVFSParsedPath();

			VDStringSpanW argRawName = arg.subspan(0, argSplitPos);
			VDStringSpanW argRawValue = arg.subspan(argSplitPos + 1);

			VDStringW argName;
			VDStringW argValue;
			if (!ATDecodeVFSString(argName, argRawName, true) || !ATDecodeVFSString(argValue, argRawValue, true))
				return ATVFSParsedPath();

			parsedPath.mParams.emplace_back(VDHashString32I(argName.data(), argName.size()), std::move(argName), std::move(argValue));

			if (nextArgPos == args.npos)
				break;

			args = args.subspan(nextArgPos + 1);
		}
	}

	const VDStringSpanW protocol(s, colon);

	if (protocol == L"file") {
		// file:// URLs must use a null hostname, so only file:/// is accepted.
		if (relPath.empty() || relPath[0] != L'/')
			return ATVFSParsedPath();

		// MERGE NOTE: retain the absolute root on POSIX; Windows file:///C:
		// has a URL separator before the drive letter.
#ifdef _WIN32
		const auto filePath = relPath.subspan(1);
#else
		const auto filePath = relPath;
#endif
		if (!ATDecodeVFSString(parsedPath.mBasePath, filePath))
			return ATVFSParsedPath();

		// normalize path separators to OS convention
		parsedPath.mBasePath = VDFileGetCanonicalPath(parsedPath.mBasePath.c_str());

		parsedPath.mProtocol = kATVFSProtocol_File;
	} else if (protocol == L"zip") {
		const auto splitPos = relPath.rfind(L'!');

		if (splitPos == relPath.npos)
			return ATVFSParsedPath();

		if (!ATDecodeVFSString(parsedPath.mBasePath, relPath.subspan(0, splitPos)))
			return ATVFSParsedPath();

		if (!ATDecodeVFSString(parsedPath.mSubPath, relPath.subspan(splitPos + 1)))
			return ATVFSParsedPath();

		parsedPath.mProtocol = kATVFSProtocol_Zip;
	} else if (protocol == L"gz") {
		if (!ATDecodeVFSString(parsedPath.mBasePath, relPath))
			return ATVFSParsedPath();

		parsedPath.mProtocol = kATVFSProtocol_GZip;
	} else if (protocol == L"atfs") {
		const auto splitPos = relPath.rfind(L'!');

		if (splitPos == relPath.npos)
			return ATVFSParsedPath();

		if (!ATDecodeVFSString(parsedPath.mBasePath, relPath.subspan(0, splitPos)))
			return ATVFSParsedPath();

		if (!ATDecodeVFSString(parsedPath.mSubPath, relPath.subspan(splitPos + 1)))
			return ATVFSParsedPath();

		parsedPath.mProtocol = kATVFSProtocol_Atfs;
	} else if (protocol == L"blob") {
		if (!relPath.empty() && g_ATVFSBlobHandler) {
			if (!ATDecodeVFSString(parsedPath.mBasePath, relPath))
				return ATVFSParsedPath();

			parsedPath.mProtocol = kATVFSProtocol_Blob;
		}
	} else if (protocol == L"special") {
		if (!relPath.empty() && g_ATVFSSpecialHandler) {
			if (!ATDecodeVFSString(parsedPath.mBasePath, relPath))
				return ATVFSParsedPath();

			parsedPath.mProtocol = kATVFSProtocol_Special;
		}
	} else if (protocol == L"atvdisk") {
		if (!relPath.empty()) {
			if (!ATDecodeVFSString(parsedPath.mBasePath, relPath))
				return ATVFSParsedPath();

			parsedPath.mProtocol = kATVFSProtocol_Atvdisk;
		}
	}

	return parsedPath;
}

VDStringW ATMakeVFSPath(ATVFSProtocol protocol, const wchar_t *basePath, const wchar_t *subPath) {
	VDStringW path;

	if (protocol == kATVFSProtocol_File) {
		path = basePath;

		for(wchar_t& c : path) {
			if (c == '/')
				c = '\\';
		}
	} else if (protocol == kATVFSProtocol_Zip || protocol == kATVFSProtocol_Atfs) {
		if (protocol == kATVFSProtocol_Zip)
			path = L"zip://";
		else
			path = L"atfs://";

		ATEncodeVFSString(path, VDStringSpanW(basePath), true);
		path += L'!';

		ATEncodeVFSString(path, VDStringSpanW(subPath), true);
	} else if (protocol == kATVFSProtocol_GZip) {
		path = L"gz://";

		ATEncodeVFSString(path, VDStringSpanW(basePath), true);
	}

	return path;
}

VDStringW ATMakeVFSPath(const wchar_t *protocolAndBasePath, const wchar_t *relativePath) {
	// if the relative path has a protocol, we want to resolve its inner base path against the
	// relative path
	if (wcsstr(relativePath, L"://")) {
		// split the relative path into protocol/base/subpath
		VDStringW relativeBasePath;
		VDStringW relativeSubPath;
		ATVFSProtocol protocol = ATParseVFSPath(relativePath, relativeBasePath, relativeSubPath);
		if (protocol == kATVFSProtocol_None)
			return VDStringW();

		// recursive call to resolve the base path
		relativeBasePath = ATMakeVFSPath(protocolAndBasePath, relativeBasePath.c_str());

		// reassemble the path and exit
		return ATMakeVFSPath(protocol, relativeBasePath.c_str(), relativeSubPath.c_str());
	}

	if (wcsstr(protocolAndBasePath, L"://")) {
		VDStringW path(protocolAndBasePath);

		if (path.back() != '/')
			path += '/';

		ATEncodeVFSString(path, VDStringSpanW(relativePath), true);
		return path;
	} else {
		return VDMakePath(protocolAndBasePath, relativePath);
	}
}

const wchar_t *ATVFSSplitPathFile(const wchar_t *path) {
	if (!wcsstr(path, L"://"))
		return VDFileSplitPath(path);

	const wchar_t *split = path + wcslen(path);
	while(split != path && split[-1] != '/' && split[-1] != '!')
		--split;

	return split;
}

bool ATVFSIsFilePath(const wchar_t *s) {
	VDStringW basePath, subPath;

	return kATVFSProtocol_File == ATParseVFSPath(s, basePath, subPath);
}

bool ATVFSExtractFilePath(const wchar_t *s, VDStringW *filePathOpt) {
	VDStringW nextPath;
	VDStringW basePath;
	VDStringW subPath;

	for(;;) {
		auto type = ATParseVFSPath(s, basePath, subPath);

		if (type == kATVFSProtocol_None)
			return false;

		if (type == kATVFSProtocol_File) {
			if (filePathOpt)
				*filePathOpt = std::move(basePath);

			return true;
		}

		nextPath = std::move(basePath);
		s = nextPath.c_str();
	}
}

///////////////////////////////////////////////////////////////////////////

VDStringW ATMakeVFSPathForGZipFile(const wchar_t *path) {
	return ATMakeVFSPath(kATVFSProtocol_GZip, path, nullptr);
}

VDStringW ATMakeVFSPathForZipFile(const wchar_t *path, const wchar_t *fileName) {
	return ATMakeVFSPath(kATVFSProtocol_Zip, path, fileName);
}

///////////////////////////////////////////////////////////////////////////

class ATVFSFileViewDirect : public ATVFSFileView {
public:
	ATVFSFileViewDirect(VDFileStream&& fs, const wchar_t *name, bool write, bool update)
		: mFileStream(std::move(fs))
	{
		mpStream = &mFileStream;
		mFileName =  name;
	}

	ATVFSFileViewDirect(const wchar_t *path, bool write, bool update)
		: mFileStream(path,
			write
			? update
				? nsVDFile::kOpenExisting | nsVDFile::kWrite | nsVDFile::kDenyAll
				: nsVDFile::kCreateAlways | nsVDFile::kWrite | nsVDFile::kDenyAll
			: nsVDFile::kOpenExisting | nsVDFile::kRead | nsVDFile::kDenyNone)
	{
		mpStream = &mFileStream;
		mFileName = VDFileSplitPath(path);
	}

	bool IsSourceReadOnly() const override {
		return (mFileStream.getAttributes() & kVDFileAttr_ReadOnly) != 0;
	}

private:
	VDFileStream mFileStream;
};

class ATVFSFileViewGZip : public ATVFSFileView {
public:
	ATVFSFileViewGZip(ATVFSFileView *view)
		: mMemoryStream(nullptr, 0)
	{
		mpStream = &mMemoryStream;

		// check if the filename ends in .gz, and if so, strip it off
		mFileName = view->GetFileName();
		auto len = mFileName.size();
		if (len > 3 && !vdwcsicmp(mFileName.c_str() + len - 3, L".gz"))
			mFileName.resize(len - 3);

		auto& srcStream = view->GetStream();
		vdautoptr<VDGUnzipStream> gzs(new VDGUnzipStream(&srcStream, srcStream.Length()));

		uint32 size = 0;
		for(;;) {
			// Don't gunzip beyond 256MB.
			if (size >= 0x10000000)
				throw MyError("Gzip stream is too large (exceeds 256MB in size).");

			uint32 inc = size < 1024 ? 1024 : ((size >> 1) & ~(uint32)15);

			mBuffer.resize(size + inc);

			sint32 actual = gzs->ReadData(mBuffer.data() + size, inc);
			if (actual <= 0) {
				mBuffer.resize(size);
				break;
			}

			size += actual;
		}

		mMemoryStream = VDMemoryStream(mBuffer.data(), size);
	}

	bool IsSourceReadOnly() const override { return true; }

private:
	vdfastvector<uint8> mBuffer;
	VDMemoryStream mMemoryStream;
};

class ATVFSZipArchive final : public vdrefcounted<IATVFSZipArchive> {
public:
	ATVFSZipArchive(ATVFSFileView& parentView)
		: mpZipView(&parentView)
	{
		mZipArchive.Init(&mpZipView->GetStream());
	}

	VDZipArchive& GetZipArchive() override { return mZipArchive; }

	vdrefptr<ATVFSFileView> OpenStream(sint32 idx) override;
	vdrefptr<ATVFSFileView> TryOpenStream(const wchar_t *subfile) override;

private:
	vdrefptr<ATVFSFileView> mpZipView;
	VDZipArchive mZipArchive;
};

vdrefptr<IATVFSZipArchive> ATVFSOpenZipArchiveFromView(ATVFSFileView& view) {
	return vdrefptr<IATVFSZipArchive>(new ATVFSZipArchive(view));
}

class ATVFSFileViewZip final : public ATVFSFileView {
public:
	ATVFSFileViewZip(ATVFSZipArchive& zipArch, vdfastvector<uint8>&& buffer, const wchar_t *fileName)
		: mMemoryStream(nullptr, 0)
		, mpZipArchive(&zipArch)
	{
		mBuffer = std::move(buffer);
		mMemoryStream = VDMemoryStream(mBuffer.data(), mBuffer.size());

		mpStream = &mMemoryStream;
		mFileName = fileName;
	}

	bool IsSourceReadOnly() const override { return true; }

private:
	vdfastvector<uint8> mBuffer;
	VDMemoryStream mMemoryStream;

	vdrefptr<ATVFSZipArchive> mpZipArchive;
};

vdrefptr<ATVFSFileView> ATVFSZipArchive::OpenStream(sint32 idx) {
	const VDZipArchive::FileInfo& info = mZipArchive.GetFileInfo(idx);

	vdautoptr<IVDInflateStream> zs { mZipArchive.OpenDecodedStream(idx) };
	vdfastvector<uint8> buffer;

	buffer.resize(info.mUncompressedSize);
	zs->Read(buffer.data(), info.mUncompressedSize);
	zs->VerifyCRC();

	vdrefptr<ATVFSFileView> view(new ATVFSFileViewZip(*this, std::move(buffer), info.mDecodedFileName.c_str()));
	view->SetTryOpenSibling(
		[self = vdrefptr(this)](const wchar_t *name) -> vdrefptr<ATVFSFileView> {
			return self->TryOpenStream(name);
		}
	);

	return view;
}

vdrefptr<ATVFSFileView> ATVFSZipArchive::TryOpenStream(const wchar_t *subfile) {
	sint32 idx = mZipArchive.FindFile(subfile, true);
	if (idx < 0)
		return nullptr;

	return OpenStream(idx);
}

void ATVFSFileView::SetTryOpenSibling(ATVFSOpenSiblingFn fn) {
	mpOpenSiblingFn = std::move(fn);
}

vdrefptr<ATVFSFileView> ATVFSFileView::TryOpenSibling(const wchar_t *name) const {
	if (mpOpenSiblingFn)
		return mpOpenSiblingFn(name);

	return nullptr;
}

void ATVFSOpenFileView(const wchar_t *vfsPath, bool write, ATVFSFileView **viewOut) {
	ATVFSOpenFileView(vfsPath, write, false, viewOut);
}

void ATVFSOpenFileView(const wchar_t *vfsPath, bool write, bool update, ATVFSFileView **viewOut) {
	const ATVFSParsedPath& parsedPath = ATParseVFSPath(vfsPath);

	*viewOut = ATVFSOpenFileView(vfsPath, parsedPath, write, update).release();
}

vdrefptr<ATVFSFileView> ATVFSOpenFileView(const wchar_t *vfsPath, const ATVFSParsedPath& parsedPath, bool write, bool update) {
	if (!parsedPath.IsValid())
		throw ATInvalidVFSPathException(vfsPath);

	vdrefptr<ATVFSFileView> view;

	try {
		switch(parsedPath.mProtocol) {
			case kATVFSProtocol_File:
				view = new ATVFSFileViewDirect(parsedPath.mBasePath.c_str(), write, update);
				view->SetTryOpenSibling(
					[baseDir = VDFileSplitPathLeft(parsedPath.mBasePath)](const wchar_t *name) -> vdrefptr<ATVFSFileView> {
						VDFileStream f;

						if (!f.tryOpen(VDMakePath(baseDir, VDStringSpanW(name)).c_str()))
							return nullptr;

						return vdrefptr(new ATVFSFileViewDirect(std::move(f), name, false, false));
					}
				);
				break;

			case kATVFSProtocol_Zip:
				if (write)
					throw VDException(L"Cannot open .zip file for write access: %ls", parsedPath.mBasePath.c_str());

				{
					ATVFSOpenFileView(parsedPath.mBasePath.c_str(), false, ~view);
					vdrefptr zipArchive(new ATVFSZipArchive(*view));

					view = zipArchive->TryOpenStream(parsedPath.mSubPath.c_str());

					if (!view)
						throw VDException(L"Cannot find within zip file: %ls", parsedPath.mSubPath.c_str());
				}
				break;

			case kATVFSProtocol_GZip:
				if (write)
					throw VDException(L"Cannot open .gz file for write access: %ls", parsedPath.mBasePath.c_str());

				ATVFSOpenFileView(parsedPath.mBasePath.c_str(), false, ~view);
				view = new ATVFSFileViewGZip(view);
				break;

			case kATVFSProtocol_Atfs:
				if (!g_ATVFSAtfsHandler)
					throw MyError("Inner filesystems are not supported.");

				if (write)
					throw VDException(L"Cannot open inner filesystem for write access: %ls", parsedPath.mBasePath.c_str());

				ATVFSOpenFileView(parsedPath.mBasePath.c_str(), false, ~view);
				{
					vdrefptr<ATVFSFileView> view2;
					g_ATVFSAtfsHandler(view, parsedPath.mSubPath.c_str(), ~view2);

					view = std::move(view2);
				}
				break;

			case kATVFSProtocol_Blob:
				if (!g_ATVFSBlobHandler)
					throw MyError("Blob URLs are not supported.");

				g_ATVFSBlobHandler(parsedPath.mBasePath.c_str(), write, update, ~view);
				break;

			case kATVFSProtocol_Special:
				if (!g_ATVFSSpecialHandler)
					throw ATUnsupportedVFSPathException(vfsPath);

				g_ATVFSSpecialHandler(parsedPath.mBasePath.c_str(), write, update, ~view);
				break;

			case kATVFSProtocol_Atvdisk:
				throw ATUnsupportedVFSPathDiskOnlyException(vfsPath);

			default:
				throw ATUnsupportedVFSPathException(vfsPath);
		}
	} catch(const ATVFSException& ex) {
		if (ex.HasPath())
			throw;

		ex.RethrowWithPath(vfsPath);
	}

	return view;
}

class ATVFSWrappedStreamView final : public ATVFSFileView {
public:
	ATVFSWrappedStreamView(IVDRandomAccessStream& stream, const wchar_t *imagePath) {
		mpStream = &stream;
		mFileName = imagePath;
	}

	bool IsSourceReadOnly() const override { return true; }
};

vdrefptr<ATVFSFileView> ATVFSWrapStream(IVDRandomAccessStream& stream, const wchar_t *imagePath) {
	return vdrefptr(new ATVFSWrappedStreamView(stream, imagePath));
}

void ATVFSSetAtfsProtocolHandler(vdfunction<void(ATVFSFileView *, const wchar_t *, ATVFSFileView **)> handler) {
	g_ATVFSAtfsHandler = std::move(handler);
}

void ATVFSSetBlobProtocolHandler(vdfunction<void(const wchar_t *, bool, bool, ATVFSFileView **)> handler) {
	g_ATVFSBlobHandler = std::move(handler);
}

void ATVFSSetSpecialProtocolHandler(vdfunction<void(const wchar_t *, bool, bool, ATVFSFileView **)> handler) {
	g_ATVFSSpecialHandler = std::move(handler);
}
