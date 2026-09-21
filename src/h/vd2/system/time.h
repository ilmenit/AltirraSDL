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

#ifndef f_VD2_SYSTEM_TIME_H
#define f_VD2_SYSTEM_TIME_H

#include <vd2/system/vdtypes.h>
#include <vd2/system/atomic.h>
#include <vd2/system/function.h>
#include <vd2/system/thread.h>
#include <vd2/system/win32/miniwindows.h>

class VDFunctionThunkInfo;

// VDGetCurrentTick: Retrieve current process timer, in milliseconds.  Should only
// be used for sparsing updates/checks, and not for precision timing.  Approximate
// resolution is 55ms under Win9x and 10-15ms under WinNT. The advantage of this
// call is that it is usually extremely fast (just reading from the PEB).
uint32 VDGetCurrentTick();
uint64 VDGetCurrentTick64();

// VDGetPreciseTick: Retrieves high-performance timer (QueryPerformanceCounter in
// Win32). This is very precise, often <1us, but often suffers from various bugs.
// that make it undesirable for high-accuracy requirements. On x64 Windows it
// can run at 1/2 speed when CPU throttling is enabled, and on some older buggy
// CPUs it can skip around occasionally (Athlon 64 X2 era, particularly). On
// modern CPUs it is better behaved and driven from a stable lock.
uint64 VDGetPreciseTick();
uint64 VDGetPreciseTicksPerSecondI();
double VDGetPreciseTicksPerSecond();
double VDGetPreciseSecondsPerTick();

class VDINTERFACE IVDTimerCallback {
public:
	virtual void TimerCallback() = 0;
};

// Lazy timers are used for low priority tasks that aren't too critical and can
// accommodate timing slop, such as periodically flushing data to disk. Timer
// precision is low (>1 frame) and callbacks can be skipped if the thread is
// busy.
//
// Lazy timers must be created on the main thread and are always invoked on
// the main thread as part of the event loop; no synchronization is needed
// between mainline and callback code.
//
// AltirraSDL: the non-Win32 backends honour that contract with a
// cooperative scheduler that the host loop drains by calling
// VDLazyTimerTick() once per iteration — see src/system/source/time_sdl3.cpp
// (and src/AltirraLibretro/libretro_time.cpp) for the implementation and the
// list of drain sites.  Preserve this note on upstream resync.
//
class VDLazyTimer {
	VDLazyTimer(const VDLazyTimer&) = delete;
	VDLazyTimer& operator=(const VDLazyTimer&) = delete;
public:
	VDLazyTimer();
	~VDLazyTimer();

	void SetOneShot(IVDTimerCallback *pCB, uint32 delay);
	void SetOneShotFn(const vdfunction<void()>& fn, uint32 delay);
	void SetPeriodic(IVDTimerCallback *pCB, uint32 delay);
	void SetPeriodicFn(const vdfunction<void()>& fn, uint32 delay);
	void Stop();

protected:
	void StaticTimeCallback(VDZHWND hwnd, VDZUINT msg, VDZUINT_PTR id, VDZDWORD time);

	uint32				mTimerId = 0;
	bool				mbPeriodic = false;
#if defined(VD_OS_WINDOWS) || defined(_WIN32)
	VDFunctionThunkInfo	*mpThunk = nullptr;
#endif
	vdfunction<void()>	mpFn;
};

#endif
