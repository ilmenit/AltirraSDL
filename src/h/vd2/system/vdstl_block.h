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

#ifndef f_VD2_SYSTEM_VDSTL_BLOCK_H
#define f_VD2_SYSTEM_VDSTL_BLOCK_H

#include <string.h>		// memcpy — see docs/merging-with-altirra-mainline.md
#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl.h>

///////////////////////////////////////////////////////////////////////////
//
//	vdblock
//
//	vdblock<T> is similar to vector<T>, except:
//
//	1) May only be used with POD types.
//	2) No construction or destruction of elements is performed.
//	3) Capacity is always equal to size, and reallocation is performed
//	   whenever the size changes.
//	4) Contents are undefined after a reallocation.
//	5) No insertion or deletion operations are provided.
//
///////////////////////////////////////////////////////////////////////////

template<class T, class A = vdallocator<T> >
class vdblock : protected A {
public:
	typedef	T									value_type;
	typedef	typename A::pointer					pointer;
	typedef	typename A::const_pointer			const_pointer;
	typedef	typename A::reference				reference;
	typedef	typename A::const_reference			const_reference;
	typedef	size_t								size_type;
	typedef	ptrdiff_t							difference_type;
	typedef	pointer								iterator;
	typedef	const_pointer						const_iterator;
	typedef std::reverse_iterator<iterator>			reverse_iterator;
	typedef std::reverse_iterator<const_iterator>	const_reverse_iterator;

	vdblock(const A& alloc = A()) : A(alloc), mpBlock(NULL), mSize(0) {}
	vdblock(size_type s, const A& alloc = A()) : A(alloc), mpBlock(A::allocate(s, 0)), mSize(s) {}

	vdblock(const vdblock& src) = delete;

	vdnothrow vdblock(vdblock&& src) vdnoexcept
		: A(static_cast<A&&>(src))
		, mpBlock(src.mpBlock)
		, mSize(src.mSize)
	{
		src.mpBlock = nullptr;
		src.mSize = 0;
	}

	~vdblock() {
		if (mpBlock)
			A::deallocate(mpBlock, mSize);
	}

	vdblock& operator=(const vdblock&) = delete;

	vdnothrow vdblock& operator=(vdblock&& src) vdnoexcept {
		if (&src != this) {
			if (mpBlock)
				A::deallocate(mpBlock, mSize);

			static_cast<A&>(*this) = static_cast<A&&>(src);
			mpBlock = src.mpBlock;
			mSize = src.mSize;
			src.mpBlock = nullptr;
			src.mSize = 0;
		}

		return *this;
	}

	reference				operator[](size_type n)			{ return mpBlock[n]; }
	const_reference			operator[](size_type n) const	{ return mpBlock[n]; }
	reference				at(size_type n)					{ return n < mSize ? mpBlock[n] : throw std::length_error("n"); }
	const_reference			at(size_type n) const			{ return n < mSize ? mpBlock[n] : throw std::length_error("n"); }
	reference				front()							{ return *mpBlock; }
	const_reference			front() const					{ return *mpBlock; }
	reference				back()							{ return mpBlock[mSize-1]; }
	const_reference			back() const					{ return mpBlock[mSize-1]; }

	const_pointer			data() const	{ return mpBlock; }
	pointer					data()			{ return mpBlock; }

	const_iterator			begin() const	{ return mpBlock; }
	iterator				begin()			{ return mpBlock; }
	const_iterator			end() const		{ return mpBlock + mSize; }
	iterator				end()			{ return mpBlock + mSize; }

	const_reverse_iterator	rbegin() const	{ return const_reverse_iterator(end()); }
	reverse_iterator		rbegin()		{ return reverse_iterator(end()); }
	const_reverse_iterator	rend() const	{ return const_reverse_iterator(begin()); }
	reverse_iterator		rend()			{ return reverse_iterator(begin()); }

	bool					empty() const		{ return !mSize; }
	size_type				size() const		{ return mSize; }
	size_type				capacity() const	{ return mSize; }

	void clear() {
		if (mpBlock)
			A::deallocate(mpBlock, mSize);
		mpBlock = NULL;
		mSize = 0;
	}

	void resize(size_type s) {
		if (s != mSize) {
			if (mpBlock) {
				A::deallocate(mpBlock, mSize);
				mpBlock = NULL;
			}
			mSize = s;
			if (s)
				mpBlock = A::allocate(mSize, 0);
		}
	}

	void resize(size_type s, const T& value) {
		if (s != mSize) {
			if (mpBlock) {
				A::deallocate(mpBlock, mSize);
				mpBlock = NULL;
			}
			mSize = s;
			if (s) {
				mpBlock = A::allocate(mSize, 0);
				std::fill(mpBlock, mpBlock+s, value);
			}
		}
	}

	void assign(const T *p, const T *q) {
		resize((size_type)(q - p));
		memcpy(mpBlock, p, (const char *)q - (const char *)p);
	}

	void swap(vdblock& x) {
		std::swap(mpBlock, x.mpBlock);
		std::swap(mSize, x.mSize);
	}

protected:
	typename A::pointer		mpBlock;
	typename A::size_type	mSize;

	union PODType {
		T x;
	};
};

#endif
