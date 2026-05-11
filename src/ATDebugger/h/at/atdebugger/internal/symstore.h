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

#ifndef f_AT_ATDEBUGGER_INTERNAL_SYMSTORE_H
#define f_AT_ATDEBUGGER_INTERNAL_SYMSTORE_H

#include <map>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/refcount.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl_vectorview.h>
#include <at/atdebugger/symbols.h>
#include <at/atdebugger/defsymbols.h>

class ATSymbolFileParsingException : public MyError {
public:
	ATSymbolFileParsingException(int line) : MyError("Symbol file parsing failed at line %d.", line) {}
};

class ATSymbolStore : public vdrefcounted<IATCustomSymbolStore> {
public:
	struct SymbolInfo {
		uint32 mOffset;
		const char *mpName;
		uint32 mSize;
	};

	struct Symbol {
		uint32	mNameOffset = 0;
		uint32	mOffset = 0;
		uint8	mFlags = 0;
		uint8	m_pad0 = 0;
		uint16	mSize = 0;
		uint16	mFileId = 0;
		uint16	mLine = 0;
	};

	struct Directive {
		ATSymbolDirectiveType mType;
		uint32	mOffset;
		size_t	mArgOffset;
	};

	ATSymbolStore();
	~ATSymbolStore();

	void Init(uint32 moduleBase, uint32 moduleSize);
	void RemoveSymbol(uint32 offset);
	void AddSymbol(uint32 offset, const char *name, uint32 size = 1, uint32 flags = kATSymbol_Read | kATSymbol_Write | kATSymbol_Execute, uint16 fileid = 0, uint16 lineno = 0);
	void AddDefaultSymbols(vdspan<const ATDefaultSymbolInfo> symbols, uint32 addressOffset = 0);
	void AddSymbols(vdvector_view<const Symbol> symbols);
	void AddReadWriteRegisterSymbol(uint32 offset, const char *writename, const char *readname = NULL);
	uint16 AddFileName(const wchar_t *filename);
	void AddSourceLine(uint16 fileId, uint16 line, uint32 moduleOffset, uint32 len = 0);

	uint32 AddName(const VDStringSpanA& name);
	void SetDirectives(vdvector_view<const Directive> directives);
	void SetBank0Global(bool enabled);

public:
	uint32	GetDefaultBase() const { return mModuleBase; }
	uint32	GetDefaultSize() const { return mModuleSize; }
	bool	LookupSymbol(uint32 moduleOffset, uint32 flags, ATSymbol& symbol);
	sint32	LookupSymbol(const char *s);
	const wchar_t *GetFileName(uint16 fileid);
	uint16	GetFileId(const wchar_t *fileName, int *matchQuality);
	void	GetLines(uint16 fileId, vdfastvector<ATSourceLineInfo>& lines);
	bool	GetLineForOffset(uint32 moduleOffset, bool searchUp, ATSourceLineInfo& lineInfo);
	bool	GetOffsetForLine(const ATSourceLineInfo& lineInfo, uint32& moduleOffset);
	uint32	GetSymbolCount() const;
	void	GetSymbol(uint32 index, ATSymbolInfo& symbol);
	uint32	GetDirectiveCount() const;
	void	GetDirective(uint32 index, ATSymbolDirectiveInfo& dirInfo);

protected:
	void LoadSymbols(VDTextStream& ifile);
	void LoadCC65Labels(VDTextStream& ifile);
	void LoadCC65DbgFile(VDTextStream& ifile);
	void LoadLabels(VDTextStream& ifile);
	void LoadMADSListing(VDTextStream& ifile);
	void LoadKernelListing(VDTextStream& ifile);

	void CanonicalizeFileName(VDStringW& s);

	struct SymEqPred {
		bool operator()(const Symbol& sym, uint32 offset) const {
			return sym.mOffset == offset;
		}

		bool operator()(uint32 offset, const Symbol& sym) const {
			return offset == sym.mOffset;
		}

		bool operator()(const Symbol& sym1, const Symbol& sym2) const {
			return sym1.mOffset == sym2.mOffset;
		}
	};

	struct SymSort {
		bool operator()(const Symbol& sym, uint32 offset) const {
			return sym.mOffset < offset;
		}

		bool operator()(uint32 offset, const Symbol& sym) const {
			return offset < sym.mOffset;
		}

		bool operator()(const Symbol& sym1, const Symbol& sym2) const {
			return sym1.mOffset < sym2.mOffset;
		}
	};

	uint32	mModuleBase = 0;
	uint32	mModuleSize = 0x10000;
	bool	mbSymbolsNeedSorting = false;
	bool	mbGlobalBank0 = false;

	typedef vdfastvector<Symbol> Symbols;
	Symbols					mSymbols;
	vdfastvector<char>		mNameBytes;
	vdfastvector<wchar_t>	mWideNameBytes;
	vdfastvector<uint32>	mFileNameOffsets;

	typedef vdfastvector<Directive> Directives;
	Directives	mDirectives;

	typedef std::map<uint32, std::pair<uint32, uint32> > OffsetToLine;
	typedef std::map<uint32, uint32> LineToOffset;

	OffsetToLine	mOffsetToLine;
	LineToOffset	mLineToOffset;
};

#endif
