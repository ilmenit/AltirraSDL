//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008-2022 Avery Lee
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
#include <vd2/system/vdstl.h>
#include <vd2/system/vdstl_vectorview.h>
#include <vd2/system/error.h>
#include <vd2/system/filesys.h>
#include <vd2/system/strutil.h>
#include <vd2/system/VDString.h>
#include <at/atcore/address.h>
#include <at/atdebugger/symbols.h>
#include <at/atdebugger/internal/symstore.h>
#include <algorithm>
#include <ctype.h>

ATSymbolStore::ATSymbolStore() {
}

ATSymbolStore::~ATSymbolStore() {
}

void ATSymbolStore::Init(uint32 moduleBase, uint32 moduleSize) {
	mModuleBase = moduleBase;
	mModuleSize = moduleSize;
}

void ATSymbolStore::RemoveSymbol(uint32 offset) {
	if (mbSymbolsNeedSorting) {
		std::sort(mSymbols.begin(), mSymbols.end(), SymSort());
		mbSymbolsNeedSorting = false;
	}

	Symbols::iterator it(std::lower_bound(mSymbols.begin(), mSymbols.end(), offset, SymSort()));

	if (it != mSymbols.end() && it->mOffset == offset)
		mSymbols.erase(it);
}

void ATSymbolStore::AddSymbol(uint32 offset, const char *name, uint32 size, uint32 flags, uint16 fileid, uint16 lineno) {
	Symbol sym;

	sym.mNameOffset = AddName(VDStringSpanA(name));
	sym.mOffset		= offset - mModuleBase;
	sym.mFlags		= (uint8)flags;
	sym.mSize		= size > 0xFFFF ? 0 : size;
	sym.mFileId		= fileid;
	sym.mLine		= lineno;

	mSymbols.push_back(sym);
	mbSymbolsNeedSorting = true;
}

void ATSymbolStore::AddDefaultSymbols(vdspan<const ATDefaultSymbolInfo> symbols, uint32 addressOffset) {
	for(const ATDefaultSymbolInfo& si : symbols) {
		uint32 flags = 0;

		switch(si.mType) {
			case ATDefaultSymbolType::Default:
				flags = kATSymbol_Read | kATSymbol_Write | kATSymbol_Execute;
				break;

			case ATDefaultSymbolType::ReadOnly:
				flags = kATSymbol_Read;
				break;

			case ATDefaultSymbolType::WriteOnly:
				flags = kATSymbol_Write;
				break;

			case ATDefaultSymbolType::NoExecute:
				flags = kATSymbol_Read | kATSymbol_Write;
				break;
		}

		AddSymbol(si.mAddress + addressOffset, si.mName, si.mSize, flags);
	}
}

void ATSymbolStore::AddSymbols(vdvector_view<const Symbol> symbols) {
	mSymbols.insert(mSymbols.end(), symbols.begin(), symbols.end());
	mbSymbolsNeedSorting = true;
}

void ATSymbolStore::AddReadWriteRegisterSymbol(uint32 offset, const char *writename, const char *readname) {
	if (readname)
		AddSymbol(offset, readname, 1, kATSymbol_Read);

	if (writename)
		AddSymbol(offset, writename, 1, kATSymbol_Write);
}

uint16 ATSymbolStore::AddFileName(const wchar_t *filename) {
	VDStringW tempName(filename);

	CanonicalizeFileName(tempName);

	const wchar_t *fnbase = mWideNameBytes.data();
	size_t n = mFileNameOffsets.size();
	for(size_t i=0; i<n; ++i)
		if (!vdwcsicmp(fnbase + mFileNameOffsets[i], tempName.c_str()))
			return (uint16)(i+1);

	mFileNameOffsets.push_back((uint32)mWideNameBytes.size());

	const wchar_t *pTempName = tempName.c_str();
	mWideNameBytes.insert(mWideNameBytes.end(), pTempName, pTempName + wcslen(pTempName) + 1);
	return (uint16)mFileNameOffsets.size();
}

void ATSymbolStore::AddSourceLine(uint16 fileId, uint16 line, uint32 moduleOffset, uint32 len) {
	uint32 key = (fileId << 16) + line;
	mLineToOffset.insert(LineToOffset::value_type(key, moduleOffset));
	mOffsetToLine.insert(OffsetToLine::value_type(moduleOffset, std::make_pair(key, len)));
}

uint32 ATSymbolStore::AddName(const VDStringSpanA& name) {
	uint32 offset = (uint32)mNameBytes.size();

	mNameBytes.insert(mNameBytes.end(), name.begin(), name.end());
	mNameBytes.push_back(0);

	return offset;
}

void ATSymbolStore::SetDirectives(vdvector_view<const Directive> directives) {
	mDirectives.assign(directives.begin(), directives.end());
}

void ATSymbolStore::SetBank0Global(bool enabled) {
	mbGlobalBank0 = enabled;
}

bool ATSymbolStore::LookupSymbol(uint32 moduleOffset, uint32 flags, ATSymbol& symout) {
	if (mbSymbolsNeedSorting) {
		std::sort(mSymbols.begin(), mSymbols.end(), SymSort());
		mbSymbolsNeedSorting = false;
	}

	Symbols::const_iterator itBegin(mSymbols.begin());
	Symbols::const_iterator it(std::upper_bound(mSymbols.begin(), mSymbols.end(), moduleOffset, SymSort()));

	uint32 moduleOffset2 = moduleOffset;

	if (mbGlobalBank0) {
		moduleOffset2 = UINT32_C(0) - (moduleOffset & 0xFFFF0000);
	}

	while(it != itBegin) {
		--it;
		const Symbol& sym = *it;

		if (sym.mFlags & flags) {
			if (sym.mSize && (moduleOffset - sym.mOffset) >= sym.mSize && (moduleOffset2 - sym.mOffset) >= sym.mSize)
				return false;

			symout.mpName	= mNameBytes.data() + sym.mNameOffset;
			symout.mFlags	= sym.mFlags;
			symout.mOffset	= sym.mOffset;
			symout.mFileId	= sym.mFileId;
			symout.mLine	= sym.mLine;
			return true;
		}
	}

	return false;
}

sint32 ATSymbolStore::LookupSymbol(const char *s) {
	Symbols::const_iterator it(mSymbols.begin()), itEnd(mSymbols.end());
	for(; it != itEnd; ++it) {
		const Symbol& sym = *it;

		if (!_stricmp(s, mNameBytes.data() + sym.mNameOffset))
			return sym.mOffset;
	}

	return -1;
}

const wchar_t *ATSymbolStore::GetFileName(uint16 fileid) {
	if (!fileid)
		return NULL;

	--fileid;
	if (fileid >= mFileNameOffsets.size())
		return NULL;

	return mWideNameBytes.data() + mFileNameOffsets[fileid];
}

uint16 ATSymbolStore::GetFileId(const wchar_t *fileName, int *matchQuality) {
	VDStringW tempName(fileName);

	CanonicalizeFileName(tempName);

	const wchar_t *fullPath = tempName.c_str();
	size_t l1 = wcslen(fullPath);

	size_t n = mFileNameOffsets.size();
	int bestq = 0;
	uint16 bestidx = 0;

	for(size_t i=0; i<n; ++i) {
		const wchar_t *fn = mWideNameBytes.data() + mFileNameOffsets[i];

		size_t l2 = wcslen(fn);
		size_t lm = l1 > l2 ? l2 : l1;

		// check for partial match length
		for(size_t j=1; j<=lm; ++j) {
			if (towlower(fullPath[l1 - j]) != towlower(fn[l2 - j]))
				break;

			if ((j == l1 || fullPath[l1 - j - 1] == L'\\') &&
				(j == l2 || fn[l2 - j - 1] == L'\\'))
			{
				// We factor two things into the quality score, in priority order:
				//
				// 1) How long of a suffix was matched.
				// 2) How short the original path was.
				//
				// #2 is a hack, but makes the debugger prefer foo.s over f1/foo.s
				// and f2/foo.s.

				int q = (int)(j * 10000 - l2);

				if (q > bestq) {
					bestq = q;
					bestidx = (uint16)(i + 1);
				}
			}
		}
	}

	if (matchQuality)
		*matchQuality = bestq;

	return bestidx;
}

void ATSymbolStore::GetLines(uint16 matchFileId, vdfastvector<ATSourceLineInfo>& lines) {
	OffsetToLine::const_iterator it(mOffsetToLine.begin()), itEnd(mOffsetToLine.end());
	for(; it!=itEnd; ++it) {
		uint32 offset = it->first;
		uint32 key = it->second.first;
		uint16 fileId = key >> 16;

		if (fileId == matchFileId) {
			ATSourceLineInfo& linfo = lines.push_back();
			linfo.mOffset = offset;
			linfo.mFileId = matchFileId;
			linfo.mLine = key & 0xffff;
		}
	}
}

bool ATSymbolStore::GetLineForOffset(uint32 moduleOffset, bool searchUp, ATSourceLineInfo& lineInfo) {
	OffsetToLine::const_iterator it(mOffsetToLine.upper_bound(moduleOffset));
	
	if (searchUp) {
		if (it == mOffsetToLine.end())
			return false;
	} else {
		if (it == mOffsetToLine.begin())
			return false;

		--it;
	}

	if (it->second.second && moduleOffset - it->first >= it->second.second)
		return false;

	uint32 key = it->second.first;
	lineInfo.mOffset = it->first;
	lineInfo.mFileId = key >> 16;
	lineInfo.mLine = key & 0xffff;
	return true;
}

bool ATSymbolStore::GetOffsetForLine(const ATSourceLineInfo& lineInfo, uint32& moduleOffset) {
	uint32 key = ((uint32)lineInfo.mFileId << 16) + lineInfo.mLine;

	LineToOffset::const_iterator it(mLineToOffset.find(key));

	if (it == mLineToOffset.end())
		return false;

	moduleOffset = it->second;
	return true;
}

uint32 ATSymbolStore::GetSymbolCount() const {
	return (uint32)mSymbols.size();
}

void ATSymbolStore::GetSymbol(uint32 index, ATSymbolInfo& symbol) {
	const Symbol& sym = mSymbols[index];

	symbol.mpName	= mNameBytes.data() + sym.mNameOffset;
	symbol.mFlags	= sym.mFlags;
	symbol.mOffset	= sym.mOffset;
	symbol.mLength	= sym.mSize;
}

uint32 ATSymbolStore::GetDirectiveCount() const {
	return (uint32)mDirectives.size();
}

void ATSymbolStore::GetDirective(uint32 index, ATSymbolDirectiveInfo& dirInfo) {
	const Directive& dir = mDirectives[index];

	dirInfo.mType = dir.mType;
	dirInfo.mpArguments = mNameBytes.data() + dir.mArgOffset;
	dirInfo.mOffset = dir.mOffset;
}

void ATSymbolStore::CanonicalizeFileName(VDStringW& s) {
	VDStringW::size_type pos = 0;
	while((pos = s.find(L'/', pos)) != VDStringW::npos) {
		s[pos] = L'\\';
		++pos;
	}

	// strip duplicate backslashes, except for front (may be UNC path)
	pos = 0;
	while(pos < s.size() && s[pos] == L'\\')
		++pos;

	++pos;
	while(pos < s.size()) {
		if (s[pos - 1] == L'\\' && s[pos] == L'\\')
			s.erase(pos);
		else
			++pos;
	}
}

///////////////////////////////////////////////////////////////////////////////

void ATCreateCustomSymbolStore(IATCustomSymbolStore **ppStore) {
	vdrefptr<ATSymbolStore> symstore(new ATSymbolStore);
	*ppStore = symstore.release();
}
