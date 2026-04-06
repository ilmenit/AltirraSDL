//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2025 Avery Lee
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

#ifndef f_VD2_VDDISPLAY_CUSTOMPARSERUTILS_H
#define f_VD2_VDDISPLAY_CUSTOMPARSERUTILS_H

#include <optional>
#include <vd2/system/error.h>
#include <vd2/system/vdstl.h>

class VDPixmapBuffer;
class VDTextInputFile;

class VDFileParseException : public VDException {
public:
	VDFileParseException(uint32 line, const char *error);
};

////////////////////////////////////////////////////////////////////////////////

template<typename T>
class VDDTexSpecViewIterator {
public:
	using pointer = T*;
	using reference = T&;
	using difference_type = ptrdiff_t;

	VDDTexSpecViewIterator(pointer p, ptrdiff_t stride)
		: mpData(p), mStride(stride)
	{
	}

	VDDTexSpecViewIterator(const VDDTexSpecViewIterator<std::remove_const_t<T>>& other)
		requires std::is_const_v<T>
		: mpData(other.mpData), mStride(other.mStride)
	{
	}

	reference operator*() const { return *mpData; }
	pointer operator->() const { return mpData; }

	difference_type operator-(const VDDTexSpecViewIterator& other) const {
		return (mpData - other.mpData) / mStride;
	}

	VDDTexSpecViewIterator& operator++() {
		mpData = pointer((char *)mpData + mStride);
		return *this;
	}

	VDDTexSpecViewIterator operator++(int) {
		auto it = *this;
		++*this;
		return it;
	}

	VDDTexSpecViewIterator& operator--() {
		mpData = pointer((char *)mpData - mStride);
		return *this;
	}

	VDDTexSpecViewIterator operator--(int) {
		auto it = *this;
		--*this;
		return it;
	}

	bool operator==(const VDDTexSpecViewIterator& other) const {
		return other.mpData == mpData;
	}

	bool operator!=(const VDDTexSpecViewIterator& other) const {
		return other.mpData != mpData;
	}

	bool operator<(const VDDTexSpecViewIterator& other) const {
		return other.mpData < mpData;
	}

	bool operator<=(const VDDTexSpecViewIterator& other) const {
		return other.mpData <= mpData;
	}

	bool operator>(const VDDTexSpecViewIterator& other) const {
		return other.mpData > mpData;
	}

	bool operator>=(const VDDTexSpecViewIterator& other) const {
		return other.mpData >= mpData;
	}

private:
	pointer mpData;
	ptrdiff_t mStride;
};

template<typename T>
class VDDTexSpecView {
public:
	using size_type = size_t;
	using difference_type = ptrdiff_t;
	using value_type = T;
	using pointer = T*;
	using const_pointer = const T*;
	using reference = T&;
	using const_reference = const T&;
	using iterator = VDDTexSpecViewIterator<T>;
	using const_iterator = VDDTexSpecViewIterator<const T>;

	constexpr VDDTexSpecView() = default;

	VDDTexSpecView(T *p, size_t n, ptrdiff_t stride)
		: mpData((char *)p), mCount(n), mStride(stride)
	{
	}

	VDDTexSpecView(const VDDTexSpecView&) = default;
	VDDTexSpecView(VDDTexSpecView&&) = default;

	template<typename U> requires std::is_convertible_v<T(*)[], U(*)[]>
	VDDTexSpecView(const VDDTexSpecView<U>& other)
		: mpData(other.mpData), mCount(other.mCount), mStride(other.mStride)
	{
	}

	template<typename R>
	VDDTexSpecView(std::from_range_t, R&& range)
		: mpData((char *)std::data(range))
		, mCount(std::size(range))
		, mStride(sizeof(*std::begin(range)))
	{
	}

	VDDTexSpecView& operator=(const VDDTexSpecView&) = default;
	VDDTexSpecView& operator=(VDDTexSpecView&&) = default;


	constexpr bool empty() const { return !mCount; }
	constexpr size_type size() const { return mCount; }

	constexpr iterator begin() { return iterator(pointer(mpData), mStride); }
	constexpr const_iterator begin() const { return iterator(const_pointer(mpData), mStride); }
	constexpr const_iterator cbegin() const { return iterator(const_pointer(mpData), mStride); }

	constexpr iterator end() { return iterator(pointer(mpData + mCount * mStride), mStride); }
	constexpr const_iterator end() const { return const_iterator(const_pointer(mpData + mCount * mStride), mStride); }
	constexpr const_iterator cend() const { return const_iterator(const_pointer(mpData + mCount * mStride), mStride); }

	constexpr reference operator[](size_type n)	{ return *pointer(mpData + mStride*n); }
	constexpr const_reference operator[](size_type n) const { return *const_pointer(mpData + mStride*n); }

private:
	char *mpData = nullptr;
	size_t mCount = 0;
	ptrdiff_t mStride = 0;
};

////////////////////////////////////////////////////////////////////////////////

struct VDDCsPropKey {
	VDStringA mBaseName;
	uint32 mCounter = 0;

	VDDCsPropKey() = default;
	explicit VDDCsPropKey(VDStringSpanA name);
	inline VDDCsPropKey(const struct VDDCsPropKeyView& view);

	bool operator==(const VDDCsPropKey&) const = default;
};

struct VDDCsPropKeyView {
	VDStringSpanA mBaseName;
	uint32 mCounter = 0;

	constexpr VDDCsPropKeyView() = default;

	explicit VDDCsPropKeyView(VDStringSpanA name);

	VDDCsPropKeyView(const char *baseName, std::nullptr_t)
		: mBaseName(baseName), mCounter(UINT32_MAX) {}

	VDDCsPropKeyView(const char *baseName, uint32 counter)
		: mBaseName(baseName), mCounter(counter) {}

	VDDCsPropKeyView(const VDDCsPropKey& key)
		: mBaseName(key.mBaseName), mCounter(key.mCounter) {}

	constexpr bool operator==(const VDDCsPropKeyView&) const = default;

	VDStringA ToString() const;
};

inline VDDCsPropKey::VDDCsPropKey(const VDDCsPropKeyView& view)
	: mBaseName(view.mBaseName), mCounter(view.mCounter) {}

struct VDDCsPropKeyHash {
	size_t operator()(const VDDCsPropKeyView& key) const {
		return mStringHash(VDStringSpanA(key.mBaseName)) + key.mCounter * 1000037;
	}

	vdhash<VDStringA> mStringHash;
};

struct VDDCsPropKeyEq {
	bool operator()(const VDDCsPropKeyView& a, const VDDCsPropKeyView& b) const {
		return a == b;
	}

	bool operator()(const VDDCsPropKey& a, const VDDCsPropKey& b) const {
		return a == b;
	}

	bool operator()(const VDDCsPropKey& a, const VDDCsPropKeyView& b) const {
		return VDDCsPropKeyView(a) == b;
	}

	bool operator()(const VDDCsPropKeyView& a, const VDDCsPropKey& b) const {
		return a == VDDCsPropKeyView(b);
	}
};

class VDDisplayCustomShaderProps {
public:
	void ParseFromFile(VDTextInputFile& file);

	const char *GetString(const VDDCsPropKeyView& key) const;
	std::optional<int> TryGetInt(const VDDCsPropKeyView& key) const;
	bool GetBool(const VDDCsPropKeyView& key, bool defaultValue) const;

	bool Add(const VDDCsPropKeyView& key, const VDStringSpanA& value);

private:
	vdhashmap<VDDCsPropKey, VDStringA, VDDCsPropKeyHash, VDDCsPropKeyEq> mProps;
};

void VDDLoadCustomShaderTexture(VDPixmapBuffer& dst, const wchar_t *path);

#endif
