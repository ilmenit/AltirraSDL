//	VirtualDub - Video processing and capture application
//	System library component
//	Copyright (C) 1998-2007 Avery Lee, All Rights Reserved.
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

#ifndef VD2_SYSTEM_VDSTL_H
#define VD2_SYSTEM_VDSTL_H

#ifdef _MSC_VER
	#pragma once
#endif

#include <limits.h>
#include <stdexcept>
#include <initializer_list>
#include <ranges>
#include <memory>
#include <string.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/memory.h>

///////////////////////////////////////////////////////////////////////////

template<class T>
T *vdmove_forward(T *src1, T *src2, T *dst) {
	T *p = src1;
	while(p != src2) {
		*dst = std::move(*p);
		++dst;
		++p;
	}

	return dst;
}

template<class T>
T *vdmove_backward(T *src1, T *src2, T *dst) {
	T *p = src2;
	while(p != src1) {
		--dst;
		--p;
		*dst = std::move(*p);
	}

	return dst;
}

///////////////////////////////////////////////////////////////////////////

template<class T, size_t N> char (&VDCountOfHelper(const T(&)[N]))[N];

#define vdcountof(array) (sizeof(VDCountOfHelper(array)))

///////////////////////////////////////////////////////////////////////////

class vdallocator_base {
protected:
	void VDNORETURN throw_oom();
};

template<class T>
class vdallocator : public vdallocator_base {
public:
	typedef	size_t		size_type;
	typedef	ptrdiff_t	difference_type;
	typedef	T*			pointer;
	typedef	const T*	const_pointer;
	typedef	T&			reference;
	typedef	const T&	const_reference;
	typedef	T			value_type;

	template<class U> struct rebind { typedef vdallocator<U> other; };

	pointer			address(reference x) const			{ return &x; }
	const_pointer	address(const_reference x) const	{ return &x; }

	pointer allocate(size_type n, void *p_close = 0) {
		pointer p = (pointer)malloc(n*sizeof(T));

		if (!p)
			throw_oom();

		return p;
	}

	void deallocate(pointer p, size_type n) {
		free(p);
	}

	size_type		max_size() const throw()			{ return ((~(size_type)0) >> 1) / sizeof(T); }
};

///////////////////////////////////////////////////////////////////////////

template<class T, unsigned kAlignment = 16>
class vdaligned_alloc {
public:
	typedef	size_t		size_type;
	typedef	ptrdiff_t	difference_type;
	typedef	T*			pointer;
	typedef	const T*	const_pointer;
	typedef	T&			reference;
	typedef	const T&	const_reference;
	typedef	T			value_type;

	vdaligned_alloc() {}

	template<class U, unsigned kAlignment2>
	vdaligned_alloc(const vdaligned_alloc<U, kAlignment2>&) {}

	template<class U> struct rebind { typedef vdaligned_alloc<U, kAlignment> other; };

	pointer			address(reference x) const			{ return &x; }
	const_pointer	address(const_reference x) const	{ return &x; }

	pointer			allocate(size_type n, void *p = 0)	{ return (pointer)VDAlignedMalloc(n*sizeof(T), kAlignment); }
	void			deallocate(pointer p, size_type n)	{ VDAlignedFree(p); }
	size_type		max_size() const throw()			{ return INT_MAX; }
};

///////////////////////////////////////////////////////////////////////////////

#if defined(_DEBUG) && defined(_MSC_VER) && !defined(__clang__)
	#define VD_ACCELERATE_TEMPLATES
#endif

#ifndef VDTINLINE
	#ifdef VD_ACCELERATE_TEMPLATES
		#ifndef VDTEXTERN
			#define VDTEXTERN extern
		#endif

		#define VDTINLINE
	#else
		#define VDTINLINE inline
	#endif
#endif

///////////////////////////////////////////////////////////////////////////////

#include <vd2/system/vdstl_span.h>
#include <vd2/system/vdstl_block.h>
#include <vd2/system/vdstl_fastdeque.h>
#include <vd2/system/vdstl_fastvector.h>
#include <vd2/system/vdstl_hash.h>
#include <vd2/system/vdstl_hashmap.h>
#include <vd2/system/vdstl_ilist.h>
#include <vd2/system/vdstl_structex.h>
#include <vd2/system/vdstl_vector.h>

#endif
