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

#include <stdafx.h>
#include <vd2/vdjson/jsonvalue.h>

const VDJSONValue VDJSONValue::null = { kTypeNull };

VDJSONValuePool::VDJSONValuePool(uint32 initialBlockSize, uint32 maxBlockSize, uint32 largeBlockThreshold)
	: mBlockSize(initialBlockSize)
	, mMaxBlockSize(maxBlockSize)
	, mLargeBlockThreshold(largeBlockThreshold)
{
	static_assert(kCellSize >= sizeof(uint32) && (kCellSize & (kCellSize - 1)) == 0, "cell size must be a power of two of at least 4");
	static_assert(sizeof(BlockNode) % kCellSize == 0, "alignment not preserved after block node");

	// The difference between the large block threshold and the normal block size needs to be
	// at least the overhead of the block.
	uint32 overhead = sizeof(BlockNode);

	if (mLargeBlockThreshold < mBlockSize - overhead)
		mLargeBlockThreshold = mBlockSize - overhead;
}

VDJSONValuePool::~VDJSONValuePool() {
	while(mpHead) {
		void *p = mpHead;
		mpHead = mpHead->mpNext;
		free(p);
	}
}

VDJSONValue *VDJSONValuePool::AddValue() {
	VDJSONValue *value = (VDJSONValue *)Allocate(sizeof(VDJSONValue));

	value->mType = VDJSONValue::kTypeNull;
	return value;
}

void VDJSONValuePool::AddArray(VDJSONValue& dst, const VDJSONValue *const *values, size_t n) {
	VDJSONArray *arr = (VDJSONArray *)Allocate(sizeof(VDJSONArray));
	dst.Set(arr);
	arr->mLength = n;

	auto *els = (const VDJSONValue **)Allocate(sizeof(const VDJSONValue *) * n);
	arr->mpElements = els;

	// MERGE NOTE: empty arrays have no source storage.
	if (n)
		memcpy(els, values, sizeof(const VDJSONValue *) * n);
}

VDJSONValue *VDJSONValuePool::AddObjectMember(VDJSONValue& dst, uint32 nameToken, VDJSONMember*& tail) {
	VDJSONMember *el = (VDJSONMember *)Allocate(sizeof(VDJSONMember));

	if (dst.mType != VDJSONValue::kTypeObject) {
		dst.mType = VDJSONValue::kTypeObject;
		dst.mpObject = NULL;
	}

	if (tail)
		tail->mpNext = el;
	else
		dst.mpObject = el;

	tail = el;

	el->mNameToken = nameToken;
	el->mValue.Set();
	el->mpNext = nullptr;
	return &el->mValue;
}

const VDJSONString *VDJSONValuePool::AddString(const wchar_t *s) {
	return AddString(s, wcslen(s));
}

const VDJSONString *VDJSONValuePool::AddString(const wchar_t *s, size_t len) {
	VDJSONString *str = (VDJSONString *)Allocate(sizeof(VDJSONString));
	wchar_t *t = (wchar_t *)Allocate(sizeof(wchar_t) * (len + 1));

	memcpy(t, s, len * sizeof(wchar_t));
	t[len] = 0;
	str->mpChars = t;
	str->mLength = len;

	return str;
}

void VDJSONValuePool::AddString(VDJSONValue& dst, const wchar_t *s) {
	AddString(dst, s, wcslen(s));
}

void VDJSONValuePool::AddString(VDJSONValue& dst, const wchar_t *s, size_t len) {
	dst.Set(AddString(s, len));
}

const wchar_t *VDJSONValuePool::AddRawString(const wchar_t *s, size_t len) {
	wchar_t *t = (wchar_t *)Allocate(sizeof(s[0]) * (len + 1));

	memcpy(t, s, sizeof(s[0]) * len);
	t[len] = 0;
	return t;
}

void *VDJSONValuePool::Allocate(size_t n) {
	n = (n + kCellSizeMask) & ~kCellSizeMask;

	if (mAllocLeft < n) {
		if (n >= mLargeBlockThreshold) {
			size_t lbsize = sizeof(BlockNode) + n;

			BlockNode *node = (BlockNode *)malloc(lbsize);

			// MERGE NOTE: raw member names can be the pool's first allocation.
			if (mpHead) {
				node->mpNext = mpHead->mpNext;
				mpHead->mpNext = node;
			} else {
				node->mpNext = nullptr;
				mpHead = node;
			}

			return node + 1;
		}

		BlockNode *node = (BlockNode *)malloc(mBlockSize);

		node->mpNext = mpHead;
		mpHead = node;
		mAllocLeft = mBlockSize - sizeof(BlockNode);
		mpAllocNext = (char *)(node + 1);

		// Double the block size each time so we increase efficiency as we allocate more.
		// The available space more than doubles due to overhead, so doubling the large
		// block threshold is OK as long as we haven't hit the limit.
		mBlockSize += mBlockSize;

		if (mBlockSize > mMaxBlockSize)
			mBlockSize = mMaxBlockSize;
		else
			mLargeBlockThreshold += mLargeBlockThreshold;
	}

	void *p = mpAllocNext;
	mAllocLeft -= n;
	mpAllocNext += n;

	return p;
}

///////////////////////////////////////////////////////////////////////////

size_t VDJSONValueRef::GetMemberCount() const {
	if (mpRef->mType != VDJSONValue::kTypeObject)
		return 0;

	size_t n = 0;

	for(auto *p = mpRef->mpObject; p; p = p->mpNext)
		++n;

	return n;
}

const VDJSONValueRef VDJSONValueRef::operator[](size_t index) const {
	if (mpRef->mType != VDJSONValue::kTypeArray)
		return VDJSONValueRef(mpDoc, &VDJSONValue::null);

	VDJSONArray *arr = mpRef->mpArray;
	if (index >= arr->mLength)
		return VDJSONValueRef(mpDoc, &VDJSONValue::null);

	return VDJSONValueRef(mpDoc, arr->mpElements[index]);
}

const VDJSONValueRef VDJSONValueRef::operator[](VDJSONNameToken nameToken) const {
	if (mpRef->mType == VDJSONValue::kTypeObject) {
		uint32 token = nameToken.mToken;
		if (token) {
			for(VDJSONMember *p = mpRef->mpObject; p; p = p->mpNext) {
				if (p->mNameToken == token)
					return VDJSONValueRef(mpDoc, &p->mValue);
			}
		}
	}

	return VDJSONValueRef(mpDoc, &VDJSONValue::null);
}

const VDJSONValueRef VDJSONValueRef::operator[](const char *s) const {
	return operator[](mpDoc->mNameTable.GetToken(s));
}

const VDJSONValueRef VDJSONValueRef::operator[](const wchar_t *s) const {
	return operator[](mpDoc->mNameTable.GetToken(s));
}

void VDJSONValueRef::RequireObject() const {
	if (!IsObject())
		throw VDParseException("A required object was not found.");
}

void VDJSONValueRef::RequireInt() const {
	if (!IsInt())
		throw VDParseException("A required integer was not found.");
}

void VDJSONValueRef::RequireString() const {
	if (!IsString())
		throw VDParseException("A required string was not found.");
}

const VDJSONArrayEnum VDJSONValueRef::GetRequiredArray(const char *key) const {
	const auto& node = operator[](key);
	if (!node.IsValid())
		throw VDParseException("A required array element was not found: %s", key);

	if (!node.IsArray())
		throw VDParseException("An element was not of array type: %s", key);

	return node.AsArray();
}

bool VDJSONValueRef::GetRequiredBool(const char *key) const {
	const auto& node = operator[](key);
	if (!node.IsValid())
		throw VDParseException("A required boolean element was not found: %s", key);

	if (!node.IsBool())
		throw VDParseException("An element was not of boolean type: %s", key);

	return node.AsBool();
}

sint64 VDJSONValueRef::GetRequiredInt64(const char *key) const {
	const auto& node = operator[](key);
	if (!node.IsValid())
		throw VDParseException("A required boolean element was not found: %s", key);

	if (!node.IsInt())
		throw VDParseException("An element was not of integer type: %s", key);

	return node.AsInt64();
}

const wchar_t *VDJSONValueRef::GetRequiredString(const char *key) const {
	const auto& node = operator[](key);
	if (!node.IsValid())
		throw VDParseException("A required string element was not found: %s", key);

	if (!node.IsString())
		throw VDParseException("An element was not of string type: %s", key);

	return node.AsString();
}

double VDJSONValueRef::ConvertToReal() const {
	if (mpRef->mType == VDJSONValue::kTypeInt)
		return (double)mpRef->mIntValue;

	return 0;
}
