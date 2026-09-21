//	VirtualDub - Video processing and capture application
//	System library component
//	Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
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

#include <stdafx.h>
#include <new>

#include <windows.h>
#include <mmsystem.h>

#include <vd2/system/time.h>
#include <vd2/system/thread.h>
#include <vd2/system/thunk.h>

uint32 VDGetCurrentTick() {
	return (uint32)GetTickCount();
}

uint64 VDGetCurrentTick64() {
	return (uint64)GetTickCount64();
}

uint64 VDGetPreciseTick() {
	LARGE_INTEGER li;
	QueryPerformanceCounter(&li);
	return li.QuadPart;
}

namespace {
	uint64 VDGetPreciseTicksPerSecondNowI() {
		LARGE_INTEGER freq;
		QueryPerformanceFrequency(&freq);
		return freq.QuadPart;
	}

	double VDGetPreciseTicksPerSecondNow() {
		LARGE_INTEGER freq;
		QueryPerformanceFrequency(&freq);
		return (double)freq.QuadPart;
	}
}

uint64 VDGetPreciseTicksPerSecondI() {
	static uint64 ticksPerSecond = VDGetPreciseTicksPerSecondNowI();

	return ticksPerSecond;
}

double VDGetPreciseTicksPerSecond() {
	static double ticksPerSecond = VDGetPreciseTicksPerSecondNow();

	return ticksPerSecond;
}

double VDGetPreciseSecondsPerTick() {
	static double secondsPerTick = 1.0 / VDGetPreciseTicksPerSecondNow();

	return secondsPerTick;
}

///////////////////////////////////////////////////////////////////////////////

VDLazyTimer::VDLazyTimer()
	: mTimerId(0)
	, mbPeriodic(false)
{
	if (!VDInitThunkAllocator())
		throw MyError("Unable to initialize thunk allocator.");

	mpThunk = VDCreateFunctionThunkFromMethod(this, &VDLazyTimer::StaticTimeCallback, true);
	if (!mpThunk) {
		VDShutdownThunkAllocator();
		throw MyError("Unable to create timer thunk.");
	}
}

VDLazyTimer::~VDLazyTimer() {
	Stop();

	VDDestroyFunctionThunk(mpThunk);
	VDShutdownThunkAllocator();
}

void VDLazyTimer::SetOneShot(IVDTimerCallback *pCB, uint32 delay) {
	SetOneShotFn([=]() { pCB->TimerCallback(); }, delay);
}

void VDLazyTimer::SetOneShotFn(const vdfunction<void()>& fn, uint32 delay) {
	Stop();

	mbPeriodic = false;
	mpFn = fn;
	mTimerId = SetTimer(NULL, 0, delay, VDGetThunkFunction<TIMERPROC>(mpThunk));
}

void VDLazyTimer::SetPeriodic(IVDTimerCallback *pCB, uint32 delay) {
	SetPeriodicFn([=]() { pCB->TimerCallback(); }, delay);
}

void VDLazyTimer::SetPeriodicFn(const vdfunction<void()>& fn, uint32 delay) {
	Stop();

	mbPeriodic = true;
	mpFn = fn;
	mTimerId = SetTimer(NULL, 0, delay, VDGetThunkFunction<TIMERPROC>(mpThunk));
}

void VDLazyTimer::Stop() {
	if (mTimerId) {
		KillTimer(NULL, mTimerId);
		mTimerId = 0;
	}
}

void VDLazyTimer::StaticTimeCallback(VDZHWND hwnd, VDZUINT msg, VDZUINT_PTR id, VDZDWORD time) {
	if (!mbPeriodic)
		Stop();

	if (mpFn)
		mpFn();
}
