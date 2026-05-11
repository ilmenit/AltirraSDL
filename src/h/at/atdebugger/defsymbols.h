//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2026 Avery Lee
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

#ifndef f_AT_DEFSYMBOLS_H
#define f_AT_DEFSYMBOLS_H

#include <vd2/system/vdstl.h>

enum class ATDefaultSymbolType : uint8 {
	Default,
	ReadOnly,
	WriteOnly,
	NoExecute,
};

// Symbol information for default system symbols, which are encoded more
// compactly and statically.
struct ATDefaultSymbolInfo {
	uint16 mAddress;
	uint16 mSize;
	char mName[7];
	ATDefaultSymbolType mType;
};

vdspan<const ATDefaultSymbolInfo> ATGetSortedSymbolsForOSVariables();
vdspan<const ATDefaultSymbolInfo> ATGetSortedSymbolsForANTICVariables();
vdspan<const ATDefaultSymbolInfo> ATGetSortedSymbolsForGTIAVariables();
vdspan<const ATDefaultSymbolInfo> ATGetSortedSymbolsForPIAVariables();
vdspan<const ATDefaultSymbolInfo> ATGetSortedSymbolsForPOKEYVariables();

#endif
