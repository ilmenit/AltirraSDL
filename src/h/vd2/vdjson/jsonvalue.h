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

#ifndef f_VD2_VDJSON_JSONVALUE_H
#define f_VD2_VDJSON_JSONVALUE_H

#include <vd2/system/VDString.h>
#include <vd2/vdjson/jsonnametable.h>

#include <vd2/system/error.h>
class VDParseException : public MyError {
public:
	VDParseException() = default;

	template<typename... Args>
	VDParseException(const char *fmt, Args... args) : MyError(fmt, std::forward<Args>(args)...) {}
};

class VDJSONNameTable;
struct VDJSONMember;
struct VDJSONElement;
struct VDJSONNameToken;
struct VDJSONString;
struct VDJSONArray;
class VDJSONDocument;

struct VDJSONValue {
	enum Type {
		kTypeNull,
		kTypeBool,
		kTypeInt,
		kTypeReal,
		kTypeString,
		kTypeObject,
		kTypeArray
	};

	void Set() { mType = kTypeNull; }
	void Set(bool v) { mType = kTypeBool; mBoolValue = v; }
	void Set(sint64 v) { mType = kTypeInt; mIntValue = v; }
	void Set(double v) { mType = kTypeReal; mRealValue = v; }
	void Set(const VDJSONString *s) { mType = kTypeString; mpString = s; }
	void Set(VDJSONArray *arr)  { mType = kTypeArray; mpArray = arr; }

	Type	mType;

	union {
		bool	mBoolValue;
		sint64	mIntValue;
		double	mRealValue;
		const VDJSONString *mpString;
		VDJSONMember *mpObject;
		VDJSONArray *mpArray;
	};

	static const VDJSONValue null;
};

struct VDJSONArray {
	size_t mLength;
	const VDJSONValue *const *mpElements;
};

struct VDJSONString {
	size_t mLength;
	const wchar_t *mpChars;
};

struct VDJSONMember {
	VDJSONMember *mpNext;
	uint32 mNameToken;
	VDJSONValue mValue;
};

struct VDJSONElement {
	VDJSONElement *mpNext;
	VDJSONValue mValue;
};

///////////////////////////////////////////////////////////////////////

class VDJSONValuePool {
	VDJSONValuePool(const VDJSONValuePool&) = delete;
	VDJSONValuePool& operator=(const VDJSONValuePool&) = delete;
public:
	VDJSONValuePool(uint32 initialBlockSize = 1024, uint32 maxBlockSize = 32768, uint32 largeBlockThreshold = 512);
	~VDJSONValuePool();

	VDJSONValue *AddValue();
	void AddArray(VDJSONValue& dst, const VDJSONValue *const *values, size_t n);
	VDJSONValue *AddObjectMember(VDJSONValue& dst, uint32 nameToken, VDJSONMember*& tail);
	const VDJSONString *AddString(const wchar_t *s);
	const VDJSONString *AddString(const wchar_t *s, size_t len);
	void AddString(VDJSONValue& dst, const wchar_t *s);
	void AddString(VDJSONValue& dst, const wchar_t *s, size_t n);

	const wchar_t *AddRawString(const wchar_t *s, size_t len);

protected:
	static constexpr uint32 kCellSize = 8;
	static constexpr size_t kCellSizeMask = kCellSize - 1;

	// For a large block, we require one additional cell at the beginning for block size
	// and starting line number and 2 bytes at the end for line info (span/line count = 0).
	static constexpr uint32 kLargeBlockLineOverhead = kCellSize + 2;

	void *Allocate(size_t n);

	union BlockNode {
		BlockNode *mpNext;
		double mAlign;
	};

	BlockNode *mpHead = nullptr;
	char *mpAllocNext = nullptr;
	uint32 mAllocLeft = 0;

	uint32 mBlockSize = 0;
	uint32 mMaxBlockSize = 0;
	uint32 mLargeBlockThreshold = 0;
};

struct VDJSONValueRef;

struct VDJSONMemberRef {
	uint32 GetNameToken() const { return mpMember->mNameToken; }
	inline const wchar_t *GetName() const;
	inline const VDJSONValueRef GetValue() const;

	const VDJSONMember *mpMember;
	const VDJSONDocument *const mpDoc;
};

struct VDJSONMemberIterator {
	VDJSONMemberIterator(const VDJSONMember *member, const VDJSONDocument *doc)
		: mpMember(member)
		, mpDoc(doc)
	{
	}

	bool IsValid() const {
		return mpMember != nullptr;
	}

	bool operator==(const VDJSONMemberIterator& other) const {
		return mpMember == other.mpMember;
	}

	bool operator!=(const VDJSONMemberIterator& other) const {
		return mpMember != other.mpMember;
	}

	VDJSONMemberRef operator*() const { return VDJSONMemberRef { mpMember, mpDoc }; };

	VDJSONMemberIterator& operator++() {
		mpMember = mpMember->mpNext;
		return *this;
	}

	VDJSONMemberIterator operator++(int) {
		return VDJSONMemberIterator(mpMember++, mpDoc);
	}

	const VDJSONMember *mpMember;
	const VDJSONDocument *const mpDoc;
};

struct VDJSONMemberSet {
	VDJSONMemberIterator begin() const {
		return mBeginIterator;
	};

	VDJSONMemberIterator end() const {
		return VDJSONMemberIterator(nullptr, mBeginIterator.mpDoc);
	}

	VDJSONMemberIterator mBeginIterator;
};


class VDJSONArrayIterator {
public:
	typedef std::random_access_iterator_tag iterator_category;
	typedef ptrdiff_t difference_type;
	typedef VDJSONValueRef value_type;
	typedef VDJSONValueRef reference;

	VDJSONArrayIterator(const VDJSONValue *const *p, const VDJSONDocument *doc)
		: mpElement(p)
		, mpDoc(doc)
	{
	}

	ptrdiff_t operator-(const VDJSONArrayIterator& other) const { return mpElement - other.mpElement; }

	VDJSONArrayIterator operator+(ptrdiff_t d) const { return VDJSONArrayIterator(mpElement + d, mpDoc); }
	VDJSONArrayIterator operator-(ptrdiff_t d) const { return VDJSONArrayIterator(mpElement - d, mpDoc); }

	VDJSONArrayIterator& operator+=(ptrdiff_t d) { mpElement += d; return *this; }
	VDJSONArrayIterator& operator-=(ptrdiff_t d) { mpElement += d; return *this; }

	VDJSONArrayIterator& operator++() { ++mpElement; return *this; }
	VDJSONArrayIterator& operator--() { --mpElement; return *this; }
	VDJSONArrayIterator operator++(int) { return VDJSONArrayIterator(mpElement++, mpDoc); }
	VDJSONArrayIterator operator--(int) { return VDJSONArrayIterator(mpElement--, mpDoc); }

	inline VDJSONValueRef operator[](ptrdiff_t d) const;
	inline VDJSONValueRef operator*() const;
	inline VDJSONValueRef operator->() const;

	bool operator==(const VDJSONArrayIterator& other) const { return mpElement == other.mpElement; }
	bool operator!=(const VDJSONArrayIterator& other) const { return mpElement != other.mpElement; }
	bool operator< (const VDJSONArrayIterator& other) const { return mpElement <  other.mpElement; }
	bool operator<=(const VDJSONArrayIterator& other) const { return mpElement <= other.mpElement; }
	bool operator> (const VDJSONArrayIterator& other) const { return mpElement >  other.mpElement; }
	bool operator>=(const VDJSONArrayIterator& other) const { return mpElement >= other.mpElement; }

private:
	const VDJSONValue *const *mpElement;
	const VDJSONDocument *const mpDoc;
};

class VDJSONArrayEnum {
public:
	typedef size_t size_type;

	VDJSONArrayEnum() = default;
	VDJSONArrayEnum(const VDJSONValue *const *p, size_t n, const VDJSONDocument *doc)
		: mpElements(p)
		, mCount(n)
		, mpDoc(doc)
	{
	}

	bool empty() const { return !mCount; }
	size_type size() const { return mCount; }

	VDJSONArrayIterator begin() const { return VDJSONArrayIterator(mpElements, mpDoc); }
	VDJSONArrayIterator end() const { return VDJSONArrayIterator(mpElements + mCount, mpDoc); }

	inline VDJSONValueRef operator[](size_type i) const;

private:
	const VDJSONValue *const *mpElements = nullptr;
	const size_t mCount = 0;
	const VDJSONDocument *mpDoc = nullptr;
};

struct VDJSONValueRef {
	VDJSONValueRef()
		: mpDoc(nullptr)
		, mpRef(&VDJSONValue::null)
	{
	}

	VDJSONValueRef(const VDJSONDocument *doc, const VDJSONValue *ref)
		: mpDoc(doc)
		, mpRef(ref)
	{
	}

	const VDJSONValue& operator*() const { return *mpRef; }
	const VDJSONValue* operator->() const { return mpRef; }

	bool IsValid() const { return mpRef->mType != VDJSONValue::kTypeNull; }

	bool IsNull() const { return mpRef->mType == VDJSONValue::kTypeNull; }
	bool IsBool() const { return mpRef->mType == VDJSONValue::kTypeBool; }
	bool IsInt() const { return mpRef->mType == VDJSONValue::kTypeInt; }
	bool IsReal() const { return mpRef->mType == VDJSONValue::kTypeReal; }
	bool IsNumeric() const { return mpRef->mType == VDJSONValue::kTypeInt || mpRef->mType == VDJSONValue::kTypeReal; }
	bool IsString() const { return mpRef->mType == VDJSONValue::kTypeString; }
	bool IsObject() const { return mpRef->mType == VDJSONValue::kTypeObject; }
	bool IsArray() const { return mpRef->mType == VDJSONValue::kTypeArray; }

	bool AsBool() const { return mpRef->mType == VDJSONValue::kTypeBool ? mpRef->mBoolValue : false; }
	sint64 AsInt64() const { return mpRef->mType == VDJSONValue::kTypeInt ? mpRef->mIntValue : 0; }
	double AsDouble() const { return mpRef->mType == VDJSONValue::kTypeReal ? mpRef->mRealValue : ConvertToReal(); }
	const wchar_t *AsString() const { return mpRef->mType == VDJSONValue::kTypeString ? mpRef->mpString->mpChars : L""; }

	VDStringSpanW AsStringSpan() const {
		if (mpRef->mType != VDJSONValue::kTypeString)
			return VDStringSpanW();

		const VDJSONString& str = *mpRef->mpString;
		return VDStringSpanW(str.mpChars, str.mpChars + str.mLength);
	}

	VDJSONMemberSet AsObject() const { return VDJSONMemberSet{ VDJSONMemberIterator(mpRef->mType == VDJSONValue::kTypeObject ? mpRef->mpObject : nullptr, mpDoc) }; };

	VDJSONArrayEnum AsArray() const {
		if (mpRef->mType == VDJSONValue::kTypeArray)
			return VDJSONArrayEnum(mpRef->mpArray->mpElements, mpRef->mpArray->mLength, mpDoc);
		else
			return VDJSONArrayEnum();
	}

	const VDJSONDocument *GetDocument() const { return mpDoc; }
	size_t GetMemberCount() const;
	size_t GetArrayLength() const { return mpRef->mType == VDJSONValue::kTypeArray ? mpRef->mpArray->mLength : 0; }

	const VDJSONValueRef operator[](size_t index) const;
	const VDJSONValueRef operator[](VDJSONNameToken nameToken) const;
	const VDJSONValueRef operator[](const char *s) const;
	const VDJSONValueRef operator[](const wchar_t *s) const;

	void RequireObject() const;
	void RequireInt() const;
	void RequireString() const;

	const VDJSONArrayEnum GetRequiredArray(const char *key) const;
	bool GetRequiredBool(const char *key) const;
	sint64 GetRequiredInt64(const char *key) const;
	const wchar_t *GetRequiredString(const char *key) const;

protected:
	double ConvertToReal() const;

	const VDJSONDocument *mpDoc;
	const VDJSONValue *mpRef;
};

inline VDJSONValueRef VDJSONArrayIterator::operator[](ptrdiff_t d) const { return VDJSONValueRef(mpDoc, mpElement[d]); }
inline VDJSONValueRef VDJSONArrayIterator::operator*() const { return VDJSONValueRef(mpDoc, *mpElement); }
inline VDJSONValueRef VDJSONArrayIterator::operator->() const { return VDJSONValueRef(mpDoc, *mpElement); }

inline VDJSONValueRef VDJSONArrayEnum::operator[](size_type i) const {
	return VDJSONValueRef(mpDoc, mpElements[i]);
}

class VDJSONDocument {
public:
	VDJSONValue mValue;
	VDJSONValuePool mPool;
	VDJSONNameTable mNameTable { mPool };

	VDJSONValueRef Root() { return VDJSONValueRef(this, &mValue); }
	const VDJSONValueRef Root() const { return VDJSONValueRef(this, &mValue); }
};

inline const wchar_t *VDJSONMemberRef::GetName() const {
	return mpDoc->mNameTable.GetName(mpMember->mNameToken);
}

inline const VDJSONValueRef VDJSONMemberRef::GetValue() const {
	return VDJSONValueRef(mpDoc, &mpMember->mValue);
}

#endif
