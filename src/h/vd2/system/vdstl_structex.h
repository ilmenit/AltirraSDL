//	VirtualDub - Video processing and capture application
//	System library component
//	Copyright (C) 2024 Avery Lee, All Rights Reserved.
//
//	Beginning with 1.6.0, the VirtualDub system library is licensed
//	differently than the remainder of VirtualDub.  This particular file is
//	thus licensed as follows (the "zlib" license):
//
//	This software is provided 'as-is', without any express or implied
//	warranty.  In no event will the authors be held liable for any
//	damages arising from the use of this software.
//
//	Permission is granted to anyone to use this software for any purpose,
//	including commercial applications, and to alter it and redistribute it
//	freely, subject to the following restrictions:
//
//	1.	The origin of this software must not be misrepresented; you must
//		not claim that you wrote the original software. If you use this
//		software in a product, an acknowledgment in the product
//		documentation would be appreciated but is not required.
//	2.	Altered source versions must be plainly marked as such, and must
//		not be misrepresented as being the original software.
//	3.	This notice may not be removed or altered from any source
//		distribution.

#ifndef f_VD2_SYSTEM_VDSTL_STRUCTEX_H
#define f_VD2_SYSTEM_VDSTL_STRUCTEX_H

#include <stdlib.h>		// malloc / free — see docs/merging-with-altirra-mainline.md
#include <string.h>		// memcpy — see docs/merging-with-altirra-mainline.md

#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl.h>

///////////////////////////////////////////////////////////////////////////
//
//	vdstructex
//
//	vdstructex describes an extensible format structure, such as
//	BITMAPINFOHEADER or WAVEFORMATEX, without the pain-in-the-butt
//	casting normally associated with one.
//
///////////////////////////////////////////////////////////////////////////

template<class T>
class vdstructex {
public:
	typedef size_t			size_type;
	typedef T				value_type;

	vdstructex() : mpMemory(NULL), mSize(0) {}

	explicit vdstructex(size_t len) : mpMemory(NULL), mSize(0) {
		resize(len);
	}

	vdstructex(const T *pStruct, size_t len) : mSize(len), mpMemory((T*)malloc(len)) {
		memcpy(mpMemory, pStruct, len);
	}

	vdstructex(const vdstructex<T>& src) : mSize(src.mSize), mpMemory((T*)malloc(src.mSize)) {
		memcpy(mpMemory, src.mpMemory, mSize);
	}

	~vdstructex() {
		free(mpMemory);
	}

	bool		empty() const		{ return !mpMemory; }
	size_type	size() const		{ return mSize; }
	T*			data() const		{ return mpMemory; }

	T&	operator *() const	{ return *(T *)mpMemory; }
	T*	operator->() const	{ return (T *)mpMemory; }

	bool operator==(const vdstructex& x) const {
		return mSize == x.mSize && (!mSize || !memcmp(mpMemory, x.mpMemory, mSize));
	}

	bool operator!=(const vdstructex& x) const {
		return mSize != x.mSize || (mSize && memcmp(mpMemory, x.mpMemory, mSize));
	}

	vdstructex<T>& operator=(const vdstructex<T>& src) {
		assign(src.mpMemory, src.mSize);
		return *this;
	}

	void assign(const T *pStruct, size_type len) {
		if (mSize != len)
			resize(len);

		memcpy(mpMemory, pStruct, len);
	}

	void clear() {
		free(mpMemory);
		mpMemory = NULL;
		mSize = 0;
	}

	void resize(size_type len) {
		if (mSize != len)
			mpMemory = (T *)realloc(mpMemory, mSize = len);
	}

protected:
	size_type	mSize;
	T *mpMemory;
};

#endif
