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

#ifndef f_VD2_SYSTEM_VDSTL_ANY_H
#define f_VD2_SYSTEM_VDSTL_ANY_H

#include <cstddef>
#include <vd2/system/error.h>	// local: lowercase for case-sensitive filesystems
#include <vd2/system/vdtypes.h>

struct VDAnyTypeTraits;

struct VDAnyStorage {
	union alignas(std::max_align_t) {
		void *mpData;

		// 3 pointers is a good size as it can hold a string.
		char mInlineData[sizeof(void *) * 3];
	};

	const VDAnyTypeTraits *mpTraits = nullptr;
};

template<typename T>
concept VDAnyInlinableType
	=	(sizeof(T) <= sizeof(void *) * 3)
	&&	(alignof(T) <= alignof(std::max_align_t))
	&&	std::is_nothrow_move_constructible_v<T>;

template<typename T>
concept VDAnyTriviallyInlinableType = VDAnyInlinableType<T>
	&& std::is_trivially_move_constructible_v<T>
	&& std::is_trivially_destructible_v<T>;

struct VDAnyTypeTraits {
	void (*mpCopyConstruct)(VDAnyStorage& VDRESTRICT dst, const VDAnyStorage& src);
	void (*mpRelocate)(VDAnyStorage& VDRESTRICT dst, VDAnyStorage& src) noexcept;
	void (*mpDelete)(VDAnyStorage& dst) noexcept;
};

inline vdnothrow void VDAnyRelocateTrivial(VDAnyStorage& VDRESTRICT dst, VDAnyStorage& src) noexcept {
	dst = src;
	src.mpTraits = nullptr;
}

template<typename T>
void VDAnyDestroyHeap(VDAnyStorage& dst) noexcept {
	static_assert(sizeof(T) > 0, "T must be complete type");
	delete (T *)dst.mpData;
	dst.mpTraits = nullptr;
}

template<VDAnyInlinableType T>
void VDAnyDestroyInline(VDAnyStorage& dst) noexcept {
	std::launder((T *)dst.mpData)->~T();
	dst.mpTraits = nullptr;
}

inline void VDAnyDestroyTrivial(VDAnyStorage& dst) noexcept {
	dst.mpTraits = nullptr;
}

template<typename T>
inline constexpr VDAnyTypeTraits g_VDAnyTypeTraits = {
	[](VDAnyStorage& dst, const VDAnyStorage& src) {
		dst.mpData = new T(*(const T *)src.mpData);
		dst.mpTraits = src.mpTraits;
	},
	VDAnyRelocateTrivial,
	VDAnyDestroyHeap<T>
};

template<VDAnyInlinableType T>
inline constexpr VDAnyTypeTraits g_VDAnyTypeTraits<T> = {
	[](VDAnyStorage& VDRESTRICT dst, const VDAnyStorage& src) {
		new(dst.mInlineData) T(*std::launder((const T *)src.mInlineData));
		dst.mpTraits = src.mpTraits;
	},
	[](VDAnyStorage& VDRESTRICT dst, VDAnyStorage& src) noexcept {
		T& srcObj = *std::launder((T *)src.mInlineData);

		new(dst.mInlineData) T(std::move(srcObj));
		dst.mpTraits = src.mpTraits;

		// workaround VS2022 ~T lookup bug
		VDAnyDestroyInline<T>(src);
	},
	VDAnyDestroyInline<T>
};

template<VDAnyTriviallyInlinableType T>
inline constexpr VDAnyTypeTraits g_VDAnyTypeTraits<T> = {
	[](VDAnyStorage& VDRESTRICT dst, const VDAnyStorage& src) {
		new(dst.mInlineData) T(*std::launder((const T *)src.mInlineData));
		dst.mpTraits = src.mpTraits;
	},
	VDAnyRelocateTrivial,
	VDAnyDestroyTrivial
};

class VDAny {
public:
	VDAny() = default;

	VDAny(const VDAny& src) {
		if (src.mStorage.mpTraits)
			src.mStorage.mpTraits->mpCopyConstruct(mStorage, src.mStorage);
		else
			mStorage.mpTraits = nullptr;
	}

	vdnothrow VDAny(VDAny&& src) noexcept {
		if (src.mStorage.mpTraits)
			src.mStorage.mpTraits->mpRelocate(mStorage, src.mStorage);
		else
			mStorage.mpTraits = nullptr;
	}

	template<typename T> requires(!std::is_same_v<std::decay_t<T>, VDAny> && std::is_copy_constructible_v<std::decay_t<T>>)
	VDAny(T&& src) {
		Emplace<std::decay_t<T>>(std::forward<T>(src));
	}

	~VDAny() {
		Reset();
	}

	VDAny& operator=(const VDAny& src) {
		if (&src != this) {
			Reset();

			if (src.mStorage.mpTraits)
				src.mStorage.mpTraits->mpCopyConstruct(mStorage, src.mStorage);
		}

		return *this;
	}

	vdnothrow VDAny& operator=(VDAny&& src) noexcept {
		if (&src != this) {
			Reset();

			if (src.mStorage.mpTraits)
				src.mStorage.mpTraits->mpRelocate(mStorage, src.mStorage);
		}

		return *this;
	}

	template<typename T> requires(!std::is_same_v<std::decay_t<T>, VDAny> && std::is_copy_constructible_v<std::decay_t<T>>)
	VDAny& operator=(T&& src) {
		Emplace<std::decay_t<T>>(std::forward<T>(src));
		return *this;
	}

	template<typename T, typename... Args>
	vdnothrow void Emplace(Args&&... args) {
		Reset();

		if constexpr(VDAnyInlinableType<T>) {
			new(mStorage.mInlineData) std::decay_t<T>(std::forward<Args>(args)...);
		} else {
			mStorage.mpData = new std::decay_t<T>(std::forward<Args>(args)...);
		}

		mStorage.mpTraits = &g_VDAnyTypeTraits<std::decay_t<T>>;
	}

	vdnothrow void Reset() noexcept {
		if (mStorage.mpTraits)
			mStorage.mpTraits->mpDelete(mStorage);
	}

	bool HasValue() const {
		return mStorage.mpTraits != nullptr;
	}

	template<typename T>
	bool HasType() const {
		return mStorage.mpTraits == &g_VDAnyTypeTraits<std::decay_t<T>>;
	}

	template<typename T>
	T& Value() {
		T *p = TryGetValue<T>();

		if (!p)
			VDRaiseInternalFailure();

		return *p;
	}

	template<typename T>
	const T& Value() const {
		const T *p = TryGetValue<T>();

		if (!p)
			VDRaiseInternalFailure();

		return *p;
	}

	template<typename T>
	T *TryGetValue() {
		return const_cast<T *>(const_cast<const VDAny *>(this)->TryGetValue<T>());
	}

	template<typename T>
	const T *TryGetValue() const {
		if (!HasType<T>())
			return nullptr;

		if constexpr(VDAnyInlinableType<T>) {
			return std::launder((const T *)mStorage.mInlineData);
		} else {
			return (const T *)mStorage.mpData;
		}
	}

	template<typename T>
	T ValueOrDefault() const {
		const T *p = TryGetValue<T>();

		return p ? *p : T{};
	}

private:
	VDAnyStorage mStorage;
};

#endif
