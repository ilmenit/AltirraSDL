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

#ifndef f_VD2_SYSTEM_VDSTL_FASTDEQUE_H
#define f_VD2_SYSTEM_VDSTL_FASTDEQUE_H

#include <string.h>		// memcpy / memmove / memset — see docs/merging-with-altirra-mainline.md
#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl.h>

template<class T, int T_BlockSizeBits>
struct vdfastdeque_block {
	enum {
		kBlockSize = 1 << T_BlockSizeBits,
		kBlockSizeBits = T_BlockSizeBits
	};

	T data[kBlockSize];
};

template<class T, class T_Base, int kBlockSizeBits>
class vdfastdeque_iterator {
public:
	typedef T value_type;
	typedef T* pointer;
	typedef T& reference;
	typedef ptrdiff_t difference_type;
	typedef std::random_access_iterator_tag iterator_category;

	vdfastdeque_iterator() = default;
	vdfastdeque_iterator(const vdfastdeque_iterator&) = default;
	vdfastdeque_iterator(const vdfastdeque_iterator<T_Base, T_Base, kBlockSizeBits>&) requires (!std::is_same_v<T, T_Base>);
	vdfastdeque_iterator(vdfastdeque_block<T_Base, kBlockSizeBits> **pMapEntry, size_t index);

	vdfastdeque_iterator& operator=(const vdfastdeque_iterator&) = default;

	T& operator *() const;
	T* operator ->() const;
	T& operator [](difference_type n) const;
	vdfastdeque_iterator& operator++();
	vdfastdeque_iterator operator++(int);
	vdfastdeque_iterator& operator--();
	vdfastdeque_iterator operator--(int);
	vdfastdeque_iterator operator+(difference_type n) const;
	vdfastdeque_iterator operator-(difference_type n) const;
	difference_type operator-(const vdfastdeque_iterator& other) const;
	vdfastdeque_iterator& operator+=(difference_type n);
	vdfastdeque_iterator& operator-=(difference_type n);

public:
	typedef size_t size_type;

	vdfastdeque_block<T_Base, kBlockSizeBits> **mpMap;
	vdfastdeque_block<T_Base, kBlockSizeBits> *mpBlock;
	size_type mIndex;
};

template<class T, class T_Base, int kBlockSizeBits>
vdfastdeque_iterator<T, T_Base, kBlockSizeBits>::vdfastdeque_iterator(const vdfastdeque_iterator<T_Base, T_Base, kBlockSizeBits>& x) requires (!std::is_same_v<T, T_Base>)
	: mpMap(x.mpMap)
	, mpBlock(x.mpBlock)
	, mIndex(x.mIndex)
{
}

template<class T, class T_Base, int kBlockSizeBits>
vdfastdeque_iterator<T, T_Base, kBlockSizeBits>::vdfastdeque_iterator(vdfastdeque_block<T_Base, kBlockSizeBits> **pMapEntry, size_t index)
	: mpMap(pMapEntry)
	, mpBlock(mpMap ? *mpMap : NULL)
	, mIndex(index)
{
}

template<class T, class T_Base, int kBlockSizeBits>
T& vdfastdeque_iterator<T, T_Base, kBlockSizeBits>::operator *() const {
	return mpBlock->data[mIndex];
}

template<class T, class T_Base, int kBlockSizeBits>
T* vdfastdeque_iterator<T, T_Base, kBlockSizeBits>::operator ->() const {
	return &mpBlock->data[mIndex];
}

template<class T, class T_Base, int kBlockSizeBits>
T& vdfastdeque_iterator<T, T_Base, kBlockSizeBits>::operator [](difference_type n) const {
	return *operator+(n);
}

template<class T, class T_Base, int kBlockSizeBits>
vdfastdeque_iterator<T, T_Base, kBlockSizeBits>& vdfastdeque_iterator<T, T_Base, kBlockSizeBits>::operator++() {
	if (++mIndex >= vdfastdeque_block<T, kBlockSizeBits>::kBlockSize) {
		mIndex = 0;
		mpBlock = *++mpMap;
	}
	return *this;
}

template<class T, class T_Base, int kBlockSizeBits>
vdfastdeque_iterator<T, T_Base, kBlockSizeBits> vdfastdeque_iterator<T, T_Base, kBlockSizeBits>::operator++(int) {
	vdfastdeque_iterator r(*this);
	operator++();
	return r;
}

template<class T, class T_Base, int kBlockSizeBits>
vdfastdeque_iterator<T, T_Base, kBlockSizeBits>& vdfastdeque_iterator<T, T_Base, kBlockSizeBits>::operator--() {
	if (mIndex-- == 0) {
		mIndex = vdfastdeque_block<T, kBlockSizeBits>::kBlockSize - 1;
		mpBlock = *--mpMap;
	}
	return *this;
}

template<class T, class T_Base, int kBlockSizeBits>
vdfastdeque_iterator<T, T_Base, kBlockSizeBits> vdfastdeque_iterator<T, T_Base, kBlockSizeBits>::operator--(int) {
	vdfastdeque_iterator r(*this);
	operator--();
	return r;
}

template<class T, class T_Base, int kBlockSizeBits>
vdfastdeque_iterator<T, T_Base, kBlockSizeBits> vdfastdeque_iterator<T, T_Base, kBlockSizeBits>::operator+(difference_type n) const {
	vdfastdeque_iterator r(*this);

	r += n;
	return r;
}

template<class T, class T_Base, int kBlockSizeBits>
vdfastdeque_iterator<T, T_Base, kBlockSizeBits> vdfastdeque_iterator<T, T_Base, kBlockSizeBits>::operator-(difference_type n) const {
	return operator+(-n);
}

template<class T, class T_Base, int kBlockSizeBits>
typename vdfastdeque_iterator<T, T_Base, kBlockSizeBits>::difference_type vdfastdeque_iterator<T, T_Base, kBlockSizeBits>::operator-(const vdfastdeque_iterator& other) const {
	return ((difference_type)mIndex - (difference_type)other.mIndex) + ((mpMap - other.mpMap) << kBlockSizeBits);
}

template<class T, class T_Base, int kBlockSizeBits>
vdfastdeque_iterator<T, T_Base, kBlockSizeBits>& vdfastdeque_iterator<T, T_Base, kBlockSizeBits>::operator+=(difference_type n) {
	difference_type i = (difference_type)mIndex + n;

	mIndex = (size_type)i & (vdfastdeque_block<T, kBlockSizeBits>::kBlockSize - 1);
	mpMap += i >> kBlockSizeBits;
	mpBlock = *mpMap;

	return *this;
}

template<class T, class T_Base, int kBlockSizeBits>
vdfastdeque_iterator<T, T_Base, kBlockSizeBits>& vdfastdeque_iterator<T, T_Base, kBlockSizeBits>::operator-=(difference_type n) {
	return operator+(-n);
}

template<class T, class U, class T_Base, int kBlockSizeBits>
bool operator==(const vdfastdeque_iterator<T, T_Base, kBlockSizeBits>& x,const vdfastdeque_iterator<U, T_Base, kBlockSizeBits>& y) {
	return x.mpBlock == y.mpBlock && x.mIndex == y.mIndex;
}

template<class T, class U, class T_Base, int kBlockSizeBits>
bool operator!=(const vdfastdeque_iterator<T, T_Base, kBlockSizeBits>& x,const vdfastdeque_iterator<U, T_Base, kBlockSizeBits>& y) {
	return x.mpBlock != y.mpBlock || x.mIndex != y.mIndex;
}

template<class T, class U, class T_Base, int kBlockSizeBits>
bool operator<(const vdfastdeque_iterator<T, T_Base, kBlockSizeBits>& x,const vdfastdeque_iterator<U, T_Base, kBlockSizeBits>& y) {
	return x.mpMap < y.mpMap || (x.mpMap == y.mpMap && x.mIndex < y.mIndex);
}

template<class T, class U, class T_Base, int kBlockSizeBits>
bool operator<=(const vdfastdeque_iterator<T, T_Base, kBlockSizeBits>& x,const vdfastdeque_iterator<U, T_Base, kBlockSizeBits>& y) {
	return x.mpMap < y.mpMap || (x.mpMap == y.mpMap && x.mIndex <= y.mIndex);
}

template<class T, class U, class T_Base, int kBlockSizeBits>
bool operator>(const vdfastdeque_iterator<T, T_Base, kBlockSizeBits>& x,const vdfastdeque_iterator<U, T_Base, kBlockSizeBits>& y) {
	return x.mpMap > y.mpMap || (x.mpMap == y.mpMap && x.mIndex > y.mIndex);
}

template<class T, class U, class T_Base, int kBlockSizeBits>
bool operator>=(const vdfastdeque_iterator<T, T_Base, kBlockSizeBits>& x,const vdfastdeque_iterator<U, T_Base, kBlockSizeBits>& y) {
	return x.mpMap > y.mpMap || (x.mpMap == y.mpMap && x.mIndex >= y.mIndex);
}

///////////////////////////////////////////////////////////////////////////////

template<class T, class A = vdallocator<T>, int kBlockSizeBits = 5>
class vdfastdeque {
public:
	typedef T&					reference;
	typedef	const T&			const_reference;
	typedef	T					value_type;
	typedef A					allocator_type;
	typedef	size_t				size_type;
	typedef	ptrdiff_t			difference_type;
	typedef	vdfastdeque_iterator<T, T, kBlockSizeBits>			iterator;
	typedef vdfastdeque_iterator<const T, T, kBlockSizeBits>	const_iterator;
	typedef std::reverse_iterator<iterator>			reverse_iterator;
	typedef std::reverse_iterator<const_iterator>	const_reverse_iterator;

	vdfastdeque();
	vdfastdeque(const vdfastdeque& src) = delete;
	~vdfastdeque();

	vdfastdeque& operator=(const vdfastdeque&) = delete;

	bool				empty() const;
	size_type			size() const;

	reference			front();
	const_reference		front() const;
	reference			back();
	const_reference		back() const;

	iterator			begin();
	const_iterator		begin() const;
	const_iterator		cbegin() const;
	iterator			end();
	const_iterator		end() const;
	const_iterator		cend() const;

	reference			operator[](size_type n);
	const_reference		operator[](size_type n) const;

	void				clear();

	reference			push_front();
	void				push_front(const_reference x);
	reference			push_back();
	void				push_back(const_reference x);

	void				pop_front();
	void				pop_back();

	void				swap(vdfastdeque& x);

protected:
	void				push_front_extend();
	void				push_back_extend();
	void				validate();

	typedef vdfastdeque_block<T, kBlockSizeBits> Block;

	enum {
		kBlockSize = Block::kBlockSize,
	};

	struct M1 : public std::allocator_traits<A>::template rebind_alloc<Block *> {
		Block **mapStartAlloc;		// start of map
		Block **mapStartCommit;		// start of range of allocated blocks
		Block **mapStart;			// start of range of active blocks
		Block **mapEnd;				// end of range of active blocks
		Block **mapEndCommit;		// end of range of allocated blocks
		Block **mapEndAlloc;		// end of map
	} m;

	struct M2 : public std::allocator_traits<A>::template rebind_alloc<Block> {
		int startIndex;
		int endIndex;
	} mTails;

	union TrivialObjectConstraint {
		T obj;
	};
};

template<class T, class A, int kBlockSizeBits>
vdfastdeque<T, A, kBlockSizeBits>::vdfastdeque() {
	m.mapStartAlloc		= NULL;
	m.mapStartCommit	= NULL;
	m.mapStart			= NULL;
	m.mapEnd			= NULL;
	m.mapEndCommit		= NULL;
	m.mapEndAlloc		= NULL;
	mTails.startIndex	= 0;
	mTails.endIndex		= kBlockSize - 1;
}

template<class T, class A, int kBlockSizeBits>
vdfastdeque<T,A,kBlockSizeBits>::~vdfastdeque() {
	while(m.mapStartCommit != m.mapEndCommit) {
		mTails.deallocate(*m.mapStartCommit++, 1);
	}

	if (m.mapStartAlloc)
		m.deallocate(m.mapStartAlloc, m.mapEndAlloc - m.mapStartAlloc);
}

template<class T, class A, int kBlockSizeBits>
bool vdfastdeque<T,A,kBlockSizeBits>::empty() const {
	return size() == 0;
}

template<class T, class A, int kBlockSizeBits>
typename vdfastdeque<T,A,kBlockSizeBits>::size_type vdfastdeque<T,A,kBlockSizeBits>::size() const {
	if (m.mapEnd == m.mapStart)
		return 0;

	return kBlockSize * ((m.mapEnd - m.mapStart) - 1) + (mTails.endIndex + 1) - mTails.startIndex;
}

template<class T, class A, int kBlockSizeBits>
typename vdfastdeque<T,A,kBlockSizeBits>::reference vdfastdeque<T,A,kBlockSizeBits>::front() {
	VDASSERT(m.mapStart != m.mapEnd);
	return (*m.mapStart)->data[mTails.startIndex];
}

template<class T, class A, int kBlockSizeBits>
typename vdfastdeque<T,A,kBlockSizeBits>::const_reference vdfastdeque<T,A,kBlockSizeBits>::front() const {
	VDASSERT(m.mapStart != m.mapEnd);
	return (*m.mapStart)->data[mTails.startIndex];
}

template<class T, class A, int kBlockSizeBits>
typename vdfastdeque<T,A,kBlockSizeBits>::reference vdfastdeque<T,A,kBlockSizeBits>::back() {
	VDASSERT(m.mapStart != m.mapEnd);
	return m.mapEnd[-1]->data[mTails.endIndex];
}

template<class T, class A, int kBlockSizeBits>
typename vdfastdeque<T,A,kBlockSizeBits>::const_reference vdfastdeque<T,A,kBlockSizeBits>::back() const {
	VDASSERT(m.mapStart != m.mapEnd);
	return m.mapEnd[-1]->data[mTails.endIndex];
}

template<class T, class A, int kBlockSizeBits>
typename vdfastdeque<T,A,kBlockSizeBits>::iterator vdfastdeque<T,A,kBlockSizeBits>::begin() {
	return iterator(m.mapStart, mTails.startIndex);
}

template<class T, class A, int kBlockSizeBits>
typename vdfastdeque<T,A,kBlockSizeBits>::const_iterator vdfastdeque<T,A,kBlockSizeBits>::begin() const {
	return const_iterator(m.mapStart, mTails.startIndex);
}

template<class T, class A, int kBlockSizeBits>
typename vdfastdeque<T,A,kBlockSizeBits>::const_iterator vdfastdeque<T,A,kBlockSizeBits>::cbegin() const {
	return const_iterator(m.mapStart, mTails.startIndex);
}

template<class T, class A, int kBlockSizeBits>
typename vdfastdeque<T,A,kBlockSizeBits>::iterator vdfastdeque<T,A,kBlockSizeBits>::end() {
	if (mTails.endIndex == kBlockSize - 1)
		return iterator(m.mapEnd, 0);
	else
		return iterator(m.mapEnd - 1, mTails.endIndex + 1);
}

template<class T, class A, int kBlockSizeBits>
typename vdfastdeque<T,A,kBlockSizeBits>::const_iterator vdfastdeque<T,A,kBlockSizeBits>::end() const {
	if (mTails.endIndex == kBlockSize - 1)
		return const_iterator(m.mapEnd, 0);
	else
		return const_iterator(m.mapEnd - 1, mTails.endIndex + 1);
}

template<class T, class A, int kBlockSizeBits>
typename vdfastdeque<T,A,kBlockSizeBits>::const_iterator vdfastdeque<T,A,kBlockSizeBits>::cend() const {
	if (mTails.endIndex == kBlockSize - 1)
		return const_iterator(m.mapEnd, 0);
	else
		return const_iterator(m.mapEnd - 1, mTails.endIndex + 1);
}

template<class T, class A, int kBlockSizeBits>
typename vdfastdeque<T,A,kBlockSizeBits>::reference vdfastdeque<T,A,kBlockSizeBits>::operator[](size_type n) {
	n += mTails.startIndex;
	return m.mapStart[n >> kBlockSizeBits]->data[n & (kBlockSize - 1)];
}

template<class T, class A, int kBlockSizeBits>
typename vdfastdeque<T,A,kBlockSizeBits>::const_reference vdfastdeque<T,A,kBlockSizeBits>::operator[](size_type n) const {
	n += mTails.startIndex;
	return m.mapStart[n >> kBlockSizeBits]->data[n & (kBlockSize - 1)];
}

template<class T, class A, int kBlockSizeBits>
void vdfastdeque<T,A,kBlockSizeBits>::clear() {
	m.mapEnd			= m.mapStart;
	mTails.startIndex	= 0;
	mTails.endIndex		= kBlockSize - 1;
}

template<class T, class A, int kBlockSizeBits>
typename vdfastdeque<T,A,kBlockSizeBits>::reference vdfastdeque<T,A,kBlockSizeBits>::push_front() {
	if (mTails.startIndex <= 0) {
		push_front_extend();
	}

	--mTails.startIndex;

	VDASSERT(m.mapStart[0]);
	return m.mapStart[0]->data[mTails.startIndex];
}

template<class T, class A, int kBlockSizeBits>
void vdfastdeque<T,A,kBlockSizeBits>::push_front(const_reference x) {
	const T x2(x);
	push_front() = x2;
}

template<class T, class A, int kBlockSizeBits>
typename vdfastdeque<T,A,kBlockSizeBits>::reference vdfastdeque<T,A,kBlockSizeBits>::push_back() {
	if (mTails.endIndex >= kBlockSize - 1) {
		push_back_extend();
	}

	++mTails.endIndex;

	VDASSERT(m.mapEnd[-1]);
	reference r = m.mapEnd[-1]->data[mTails.endIndex];
	return r;
}

template<class T, class A, int kBlockSizeBits>
void vdfastdeque<T,A,kBlockSizeBits>::push_back(const_reference x) {
	const T x2(x);
	push_back() = x2;
}

template<class T, class A, int kBlockSizeBits>
void vdfastdeque<T,A,kBlockSizeBits>::pop_front() {
	if (++mTails.startIndex >= kBlockSize) {
		VDASSERT(m.mapEnd != m.mapStart);
		mTails.startIndex = 0;
		++m.mapStart;
	}
}

template<class T, class A, int kBlockSizeBits>
void vdfastdeque<T,A,kBlockSizeBits>::pop_back() {
	if (--mTails.endIndex < 0) {
		VDASSERT(m.mapEnd != m.mapStart);
		mTails.endIndex = kBlockSize - 1;
		--m.mapEnd;
	}
}

template<class T, class A, int kBlockSizeBits>
void vdfastdeque<T,A,kBlockSizeBits>::swap(vdfastdeque& x) {
	std::swap(m, x.m);
	std::swap(mTails, x.mTails);
}

/////////////////////////////////

template<class T, class A, int kBlockSizeBits>
void vdfastdeque<T,A,kBlockSizeBits>::push_front_extend() {
	validate();

	// check if we need to extend the map itself
	if (m.mapStart == m.mapStartAlloc) {
		// can we just shift the map?
		size_type currentMapSize = m.mapEndAlloc - m.mapStartAlloc;
		size_type freeAtEnd = m.mapEndAlloc - m.mapEndCommit;

		if (freeAtEnd >= 2 && (freeAtEnd + freeAtEnd) >= currentMapSize) {
			size_type shiftDistance = freeAtEnd >> 1;

			VDASSERT(!m.mapEndAlloc[-1]);
			memmove(m.mapStartAlloc + shiftDistance, m.mapStartAlloc, sizeof(Block *) * (currentMapSize - shiftDistance));
			memset(m.mapStartAlloc, 0, shiftDistance * sizeof(Block *));

			// relocate pointers
			m.mapEndCommit		+= shiftDistance;
			m.mapEnd			+= shiftDistance;
			m.mapStart			+= shiftDistance;
			m.mapStartCommit	+= shiftDistance;
		} else {
			size_type shiftDistance = currentMapSize+1;
			size_type newMapSize = currentMapSize + shiftDistance;

			Block **newMap = m.allocate(newMapSize);

			memcpy(newMap + shiftDistance, m.mapStartAlloc, currentMapSize * sizeof(Block *));
			memset(newMap, 0, shiftDistance * sizeof(Block *));

			// relocate pointers
			m.mapEndAlloc		= newMap + shiftDistance + newMapSize;
			m.mapEndCommit		= newMap + shiftDistance + (m.mapEndCommit		- m.mapStartAlloc);
			m.mapEnd			= newMap + shiftDistance + (m.mapEnd			- m.mapStartAlloc);
			m.mapStart			= newMap + shiftDistance + (m.mapStart			- m.mapStartAlloc);
			m.mapStartCommit	= newMap + shiftDistance + (m.mapStartCommit	- m.mapStartAlloc);

			m.deallocate(m.mapStartAlloc, currentMapSize);
			m.mapStartAlloc		= newMap;
		}

		validate();
	}

	VDASSERT(m.mapStart != m.mapStartAlloc);

	// check if we already have a block we can use
	--m.mapStart;
	if (!*m.mapStart) {
		// check if we can steal a block from the end
		if (m.mapEndCommit != m.mapEnd) {
			VDASSERT(m.mapEndCommit[-1]);
			*m.mapStart = m.mapEndCommit[-1];
			m.mapEndCommit[-1] = nullptr;
			--m.mapEndCommit;
		} else {
			// allocate a new block
			*m.mapStart = mTails.allocate(1);
		}

		m.mapStartCommit = m.mapStart;
	}

	validate();

	mTails.startIndex = kBlockSize;
}

template<class T, class A, int kBlockSizeBits>
void vdfastdeque<T,A,kBlockSizeBits>::push_back_extend() {
	validate();

	// check if we need to extend the map itself
	if (m.mapEnd == m.mapEndAlloc) {
		// can we just shift the map?
		size_type currentMapSize = m.mapEndAlloc - m.mapStartAlloc;
		size_type freeAtStart = m.mapStartCommit - m.mapStartAlloc;

		if (freeAtStart >= 2 && (freeAtStart + freeAtStart) >= currentMapSize) {
			size_type shiftDistance = freeAtStart >> 1;

			VDASSERT(!m.mapStartAlloc[0]);
			memmove(m.mapStartAlloc, m.mapStartAlloc + shiftDistance, sizeof(Block *) * (currentMapSize - shiftDistance));
			memset(m.mapStartAlloc + (currentMapSize - shiftDistance), 0, shiftDistance * sizeof(Block *));

			// relocate pointers
			m.mapEndCommit		-= shiftDistance;
			m.mapEnd			-= shiftDistance;
			m.mapStart			-= shiftDistance;
			m.mapStartCommit	-= shiftDistance;
			validate();
		} else {
			size_type newMapSize = currentMapSize*2+1;

			Block **newMap = m.allocate(newMapSize);

			memcpy(newMap, m.mapStartAlloc, currentMapSize * sizeof(Block *));
			memset(newMap + currentMapSize, 0, (newMapSize - currentMapSize) * sizeof(Block *));

			// relocate pointers
			m.mapEndAlloc		= newMap + newMapSize;
			m.mapEndCommit		= newMap + (m.mapEndCommit		- m.mapStartAlloc);
			m.mapEnd			= newMap + (m.mapEnd			- m.mapStartAlloc);
			m.mapStart			= newMap + (m.mapStart			- m.mapStartAlloc);
			m.mapStartCommit	= newMap + (m.mapStartCommit	- m.mapStartAlloc);

			m.deallocate(m.mapStartAlloc, currentMapSize);
			m.mapStartAlloc		= newMap;
			validate();
		}
	}

	VDASSERT(m.mapEnd != m.mapEndAlloc);

	// check if we already have a block we can use
	if (*m.mapEnd) {
		++m.mapEnd;
	} else {
		// check if we can steal a block from the beginning
		if (m.mapStartCommit != m.mapStart) {
			VDASSERT(*m.mapStartCommit);
			*m.mapEnd = *m.mapStartCommit;
			*m.mapStartCommit = nullptr;
			++m.mapStartCommit;
		} else {
			// allocate a new block
			*m.mapEnd = mTails.allocate(1);
		}

		++m.mapEnd;
		m.mapEndCommit = m.mapEnd;
	}

	validate();

	mTails.endIndex = -1;
}

template<class T, class A, int kBlockSizeBits>
void vdfastdeque<T,A,kBlockSizeBits>::validate() {
	VDASSERT(m.mapStartAlloc <= m.mapStartCommit);
	VDASSERT(m.mapStartCommit <= m.mapStart);
	VDASSERT(m.mapStart <= m.mapEnd);
	VDASSERT(m.mapEnd <= m.mapEndCommit);
	VDASSERT(m.mapEndCommit <= m.mapEndAlloc);

	VDASSERT(m.mapStartAlloc == m.mapStartCommit || !*m.mapStartAlloc);
	VDASSERT(m.mapStartCommit == m.mapEndCommit || m.mapStartCommit[0]);
	VDASSERT(m.mapStart == m.mapEnd || (m.mapStart[0] && m.mapEnd[-1]));
	VDASSERT(m.mapEndCommit == m.mapEndAlloc || !m.mapEndCommit[0]);
}

#endif
