//	VirtualDub - Video processing and capture application
//	System library component (SDL3/Linux port)
//	Copyright (C) 1998-2024 Avery Lee, All Rights Reserved.
//
//	Licensed under the zlib license.

#include <stdafx.h>
#include <time.h>
#include <vd2/system/date.h>
#include <vd2/system/VDString.h>

// VDDate uses Windows FILETIME ticks: 100ns intervals since Jan 1 1601.
// Unix epoch offset: 11644473600 seconds = 116444736000000000 ticks.
static constexpr uint64 kWindowsToUnixTickOffset = UINT64_C(116444736000000000);

VDDate VDGetCurrentDate() {
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);

	// Convert to 100ns ticks from Windows epoch
	uint64 ticks = (uint64)ts.tv_sec * 10000000ULL + (uint64)(ts.tv_nsec / 100) + kWindowsToUnixTickOffset;

	VDDate r;
	r.mTicks = ticks;
	return r;
}

sint64 VDGetDateAsTimeT(const VDDate& date) {
	return ((sint64)date.mTicks - (sint64)kWindowsToUnixTickOffset) / 10000000;
}

VDExpandedDate VDGetUtcDate(const VDDate& date) {
	VDExpandedDate r = {};

	time_t t = VDGetDateAsTimeT(date);
	struct tm gt;
	if (gmtime_r(&t, &gt)) {
		r.mYear        = (uint32)(gt.tm_year + 1900);
		r.mMonth       = (uint8)(gt.tm_mon + 1);
		r.mDayOfWeek   = (uint8)gt.tm_wday;
		r.mDay         = (uint8)gt.tm_mday;
		r.mHour        = (uint8)gt.tm_hour;
		r.mMinute      = (uint8)gt.tm_min;
		r.mSecond      = (uint8)gt.tm_sec;
		r.mMilliseconds = (uint16)((date.mTicks / 10000) % 1000);
	}

	return r;
}

VDExpandedDate VDGetLocalDate(const VDDate& date) {
	VDExpandedDate r = {};

	time_t t = VDGetDateAsTimeT(date);
	struct tm lt;
	if (localtime_r(&t, &lt)) {
		r.mYear        = (uint32)(lt.tm_year + 1900);
		r.mMonth       = (uint8)(lt.tm_mon + 1);
		r.mDayOfWeek   = (uint8)lt.tm_wday;
		r.mDay         = (uint8)lt.tm_mday;
		r.mHour        = (uint8)lt.tm_hour;
		r.mMinute      = (uint8)lt.tm_min;
		r.mSecond      = (uint8)lt.tm_sec;
		r.mMilliseconds = (uint16)((date.mTicks / 10000) % 1000);
	}

	return r;
}

VDDate VDDateFromLocalDate(const VDExpandedDate& date) {
	struct tm lt = {};
	lt.tm_year  = (int)date.mYear - 1900;
	lt.tm_mon   = (int)date.mMonth - 1;
	lt.tm_mday  = (int)date.mDay;
	lt.tm_hour  = (int)date.mHour;
	lt.tm_min   = (int)date.mMinute;
	lt.tm_sec   = (int)date.mSecond;
	lt.tm_isdst = -1;

	time_t t = mktime(&lt);
	if (t == (time_t)-1) {
		return VDDate{0};
	}

	uint64 ticks = (uint64)t * 10000000ULL + (uint64)date.mMilliseconds * 10000ULL + kWindowsToUnixTickOffset;

	VDDate r;
	r.mTicks = ticks;
	return r;
}

void VDAppendLocalDateString(VDStringW& dst, const VDExpandedDate& ed) {
	wchar_t buf[32];
	swprintf(buf, 32, L"%04u-%02u-%02u", ed.mYear, (unsigned)ed.mMonth, (unsigned)ed.mDay);
	dst += buf;
}

void VDAppendLocalTimeString(VDStringW& dst, const VDExpandedDate& ed) {
	wchar_t buf[32];
	swprintf(buf, 32, L"%02u:%02u:%02u", (unsigned)ed.mHour, (unsigned)ed.mMinute, (unsigned)ed.mSecond);
	dst += buf;
}
