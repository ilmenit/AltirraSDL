//	VirtualDub - Video processing and capture application
//	System library component
//	Copyright (C) 1998-2011 Avery Lee, All Rights Reserved.
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

#ifndef f_VD2_SYSTEM_DATE_H
#define f_VD2_SYSTEM_DATE_H

#include <cmath>
#include <vd2/system/vdtypes.h>

class VDStringW;

template<typename T> static constexpr T kVDDateTicksToSeconds = T(0.0000001);
template<typename T> static constexpr T kVDDateSecondsToTicks = T(10000000.0);

struct VDDateInterval {
	int64 mDeltaTicks;

	constexpr VDDateInterval operator+() const { return *this; }
	constexpr VDDateInterval operator-() const { return VDDateInterval { -mDeltaTicks }; }

	constexpr bool operator==(const VDDateInterval& x) const = default;
	constexpr bool operator< (const VDDateInterval& x) const { return mDeltaTicks <  x.mDeltaTicks; }
	constexpr bool operator> (const VDDateInterval& x) const { return mDeltaTicks >  x.mDeltaTicks; }
	constexpr bool operator<=(const VDDateInterval& x) const { return mDeltaTicks <= x.mDeltaTicks; }
	constexpr bool operator>=(const VDDateInterval& x) const { return mDeltaTicks >= x.mDeltaTicks; }

	constexpr VDDateInterval Abs() const {
		return VDDateInterval { mDeltaTicks < 0 ? -mDeltaTicks : mDeltaTicks };
	}

	static constexpr VDDateInterval FromSeconds(float f) {
		if consteval {
			return VDDateInterval{ (int64)(f < 0 ? f * kVDDateSecondsToTicks<float> - 0.5f : f * kVDDateSecondsToTicks<float> + 0.5f) };
		} else {
			return VDDateInterval{ (int64)llrintf(f * kVDDateSecondsToTicks<float>) };
		}
	}

	constexpr float ToSeconds() const {
		return kVDDateTicksToSeconds<float> * (float)mDeltaTicks;
	}
};

struct VDDate {
	uint64	mTicks;

	constexpr bool operator==(const VDDate& x) const = default;
	constexpr bool operator< (const VDDate& x) const { return mTicks <  x.mTicks; }
	constexpr bool operator> (const VDDate& x) const { return mTicks >  x.mTicks; }
	constexpr bool operator<=(const VDDate& x) const { return mTicks <= x.mTicks; }
	constexpr bool operator>=(const VDDate& x) const { return mTicks >= x.mTicks; }

	constexpr VDDateInterval operator-(const VDDate& x) const {
		return VDDateInterval{ (int64)mTicks - (int64)x.mTicks };
	};

	constexpr VDDate operator+(const VDDateInterval& x) const {
		return VDDate { mTicks + x.mDeltaTicks };
	}

	constexpr VDDate operator-(const VDDateInterval& x) const {
		return VDDate { mTicks - x.mDeltaTicks };
	}
};

constexpr VDDate operator+(const VDDateInterval& x, const VDDate& y) {
	return VDDate { x.mDeltaTicks + y.mTicks };
}

// Calendar-based date expansion. This is currently modeled after Win32
// SYSTEMTIME.
struct VDExpandedDate {
	uint32	mYear;			// 1601+
	uint8	mMonth;			// 1-12
	uint8	mDayOfWeek;		// 0-6, starting with Sunday
	uint8	mDay;			// 1-31
	uint8	mHour;			// 0-23
	uint8	mMinute;		// 0-59
	uint8	mSecond;		// 0-59
	uint16	mMilliseconds;	// 0-999
};

VDDate VDGetCurrentDate();
sint64 VDGetDateAsTimeT(const VDDate& date);
VDExpandedDate VDGetUtcDate(const VDDate& date);
VDExpandedDate VDGetLocalDate(const VDDate& date);
VDDate VDDateFromLocalDate(const VDExpandedDate& date);
void VDAppendLocalDateString(VDStringW& dst, const VDExpandedDate& date);
void VDAppendLocalTimeString(VDStringW& dst, const VDExpandedDate& date);

#endif	// f_VD2_SYSTEM_DATE_H
