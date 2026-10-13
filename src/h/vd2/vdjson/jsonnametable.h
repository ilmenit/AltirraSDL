//	VirtualDub - Video processing and capture application
//	JSON I/O library
//	Copyright (C) 1998-2010 Avery Lee
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

#ifndef f_VD2_VDJSON_JSONNAMETABLE_H
#define f_VD2_VDJSON_JSONNAMETABLE_H

#include <vd2/system/vdstl.h>

class VDStringW;
class VDStringSpanW;
class VDJSONValuePool;

struct VDJSONNameToken {
	VDJSONNameToken(uint32 token) : mToken(token) {}

	const uint32 mToken;
};

class VDJSONNameTable final {
	VDJSONNameTable(const VDJSONNameTable&) = delete;
	VDJSONNameTable& operator=(const VDJSONNameTable&) = delete;

public:
	explicit VDJSONNameTable(VDJSONValuePool& pool);
	~VDJSONNameTable();

	const wchar_t *GetName(uint32 token) const;
	uint32 GetNameLength(uint32 token) const;

	uint32 AddName(const wchar_t *s);
	uint32 AddName(const wchar_t *s, size_t len);
	VDJSONNameToken GetToken(const char *s) const;
	VDJSONNameToken GetToken(const wchar_t *s) const;

private:
	typedef vdfastvector<VDStringSpanW> NameTable;
	NameTable mNameTable;

	typedef vdhashmap<VDStringSpanW, uint32, vdhash<VDStringW>> NameMap;
	NameMap mNameMap;

	VDJSONValuePool& mPool;
};

#endif
