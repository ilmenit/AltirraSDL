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

// AltirraSDL: this translation unit has been rewritten on the fork to
// support both the Windows backend (CreateThread / WaitForSingleObject)
// AND a POSIX/std::-based backend used by the SDL3 build on Linux,
// macOS and Android. Upstream test9 only adds out-of-line definitions
// for the Win32 VDCriticalSection / VDSemaphore methods that used to be
// inline in thread.h. The fork already implements these out-of-line as
// part of the std::mutex / std::condition_variable rewrite (see
// thread.h), so upstream's added symbols correspond to functions that
// already exist here under the std:: backend. No upstream-source change
// is mechanically applicable.
#include <stdafx.h>
#include <thread>
#include <chrono>

#if defined(VD_OS_WINDOWS) || defined(_WIN32)
#include <process.h>
#include <windows.h>
#endif

#include <vd2/system/vdtypes.h>
#include <vd2/system/thread.h>
#include <vd2/system/tls.h>
#include <vd2/system/bitmath.h>

#if defined(VD_OS_WINDOWS) || defined(_WIN32)

namespace {
	//
	// This apparently came from one a talk by one of the Visual Studio
	// developers, i.e. I didn't write it.
	//
	#define MS_VC_EXCEPTION 0x406d1388

	typedef struct tagTHREADNAME_INFO
	{
		DWORD dwType;        // must be 0x1000
		LPCSTR szName;       // pointer to name (in same addr space)
		DWORD dwThreadID;    // thread ID (-1 caller thread)
		DWORD dwFlags;       // reserved for future use, most be zero
	} THREADNAME_INFO;
}

VDThreadID VDGetCurrentThreadID() {
	return (VDThreadID)GetCurrentThreadId();
}

VDProcessId VDGetCurrentProcessId() {
	return (VDProcessId)::GetCurrentProcessId();
}

uint32 VDGetLogicalProcessorCount() {
	DWORD_PTR processAffinityMask;
	DWORD_PTR systemAffinityMask;
	if (!::GetProcessAffinityMask(::GetCurrentProcess(), &processAffinityMask, &systemAffinityMask))
		return 1;

	// avoid unnecessary WTFs
	if (!processAffinityMask)
		return 1;

	// We use the process affinity mask as that's the number of logical processors we'll
	// actually be working with.
	return VDCountBits(processAffinityMask);
}

void VDSetThreadDebugName(VDThreadID tid, const char *name) {
	#ifdef VD_COMPILER_MSVC
		THREADNAME_INFO info;
		info.dwType		= 0x1000;
		info.szName		= name;
		info.dwThreadID	= tid;
		info.dwFlags	= 0;

		__try {
			RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(DWORD), (ULONG_PTR *)&info);
		} __except (EXCEPTION_CONTINUE_EXECUTION) {
		}
	#endif
}

void VDSetCurrentThreadDebugName(const char *name) {
	VDSetThreadDebugName(GetCurrentThreadId(), name);

	static const FARPROC s_pSetThreadDescription = GetProcAddress(GetModuleHandleW(L"kernel32"), "SetThreadDescription");
	if (s_pSetThreadDescription) {
		wchar_t buf[128];
		size_t i = 0;

		while(i < 127) {
			const char c = name[i];

			if (!c)
				break;

			buf[i++] = (wchar_t)(unsigned char)c;
		}

		buf[i] = 0;

		((HRESULT (WINAPI *)(HANDLE, PCWSTR))s_pSetThreadDescription)(GetCurrentThread(), buf);
	}
}

void VDThreadSleep(int milliseconds) {
	if (milliseconds > 0)
		::Sleep(milliseconds);
}

#else // non-Windows

#include <unistd.h>
#include <pthread.h>

VDThreadID VDGetCurrentThreadID() {
	return (VDThreadID)(uintptr_t)pthread_self();
}

VDProcessId VDGetCurrentProcessId() {
	return (VDProcessId)getpid();
}

uint32 VDGetLogicalProcessorCount() {
	return std::thread::hardware_concurrency();
}

void VDSetThreadDebugName(VDThreadID tid, const char *name) {
#if defined(__linux__)
	pthread_setname_np(pthread_self(), name);
#elif defined(__APPLE__)
	pthread_setname_np(name);
#endif
}

void VDSetCurrentThreadDebugName(const char *name) {
	VDSetThreadDebugName(VDGetCurrentThreadID(), name);
}

void VDThreadSleep(int milliseconds) {
	if (milliseconds > 0)
		std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

#endif

///////////////////////////////////////////////////////////////////////////

VDThread::VDThread(const char *pszDebugName)
	: mpszDebugName(pszDebugName)
	, mhThread(0)
	, mThreadID(0)
{
}

VDThread::~VDThread() throw() {
	if (isThreadAttached())
		ThreadWait();
}

#if defined(VD_OS_WINDOWS) || defined(_WIN32)

bool VDThread::ThreadStart() {
	VDASSERT(!isThreadAttached());

	if (!isThreadAttached())
		mhThread = (void *)_beginthreadex(NULL, 0, StaticThreadStart, this, 0, &mThreadID);

	return mhThread != 0;
}

void VDThread::ThreadDetach() {
	if (isThreadAttached()) {
		CloseHandle((HANDLE)mhThread);
		mhThread = NULL;
		mThreadID = 0;
	}
}

void VDThread::ThreadWait() {
	if (isThreadAttached()) {
		WaitForSingleObject((HANDLE)mhThread, INFINITE);
		ThreadDetach();
		mThreadID = 0;
	}
}

void VDThread::ThreadCancelSynchronousIo() {
	if (isThreadAttached())
		CancelSynchronousIo(mhThread);
}

bool VDThread::isThreadActive() {
	if (isThreadAttached()) {
		if (WAIT_TIMEOUT == WaitForSingleObject((HANDLE)mhThread, 0))
			return true;

		ThreadDetach();
		mThreadID = 0;
	}
	return false;
}

bool VDThread::IsCurrentThread() const {
	return GetCurrentThreadId() == mThreadID;
}

unsigned __stdcall VDThread::StaticThreadStart(void *pThisAsVoid) {
	VDThread *pThis = static_cast<VDThread *>(pThisAsVoid);

	if (pThis->mpszDebugName)
		VDSetCurrentThreadDebugName(pThis->mpszDebugName);

	VDInitThreadData(pThis->mpszDebugName);

	pThis->ThreadRun();

	VDDeinitThreadData();

	return 0;
}

#else // non-Windows

bool VDThread::ThreadStart() {
	VDASSERT(!isThreadAttached());

#if defined(__EMSCRIPTEN__)
	// WASM is built without -pthread, so pthread_create is not available
	// at link time.  Every SDL3-frontend site that previously spawned a
	// VDThread is gated at a higher layer with its own __EMSCRIPTEN__
	// branch (synchronous or main-loop-cooperative work), so this path
	// should not fire in practice.  Return false so that any caller
	// which forgot to gate itself produces a clean "thread not started"
	// outcome instead of a link-time undefined-reference.
	return false;
#else
	if (!isThreadAttached()) {
		pthread_t thread;
		if (pthread_create(&thread, nullptr, StaticThreadStart, this) == 0) {
			mhThread = (void *)thread;
			mThreadID = (VDThreadID)(uintptr_t)thread;
			return true;
		}
	}

	return false;
#endif
}

void VDThread::ThreadDetach() {
	if (isThreadAttached()) {
		mhThread = nullptr;
		mThreadID = 0;
	}
}

void VDThread::ThreadWait() {
	if (isThreadAttached()) {
#if !defined(__EMSCRIPTEN__)
		pthread_join((pthread_t)mhThread, nullptr);
#endif
		ThreadDetach();
		mThreadID = 0;
	}
}

void VDThread::ThreadCancelSynchronousIo() {
	// No direct equivalent on non-Windows
}

bool VDThread::isThreadActive() {
	// On non-Windows, we can't non-destructively poll thread status
	// without using pthread_tryjoin_np (Linux-specific). Conservatively
	// return true if attached.
	return isThreadAttached();
}

bool VDThread::IsCurrentThread() const {
#if defined(__EMSCRIPTEN__)
	// Single-threaded wasm build: every call is from "the" thread.
	return true;
#else
	return pthread_equal(pthread_self(), (pthread_t)mhThread);
#endif
}

void *VDThread::StaticThreadStart(void *pThisAsVoid) {
	VDThread *pThis = static_cast<VDThread *>(pThisAsVoid);

	if (pThis->mpszDebugName)
		VDSetCurrentThreadDebugName(pThis->mpszDebugName);

	VDInitThreadData(pThis->mpszDebugName);

	pThis->ThreadRun();

	VDDeinitThreadData();

	return nullptr;
}

#endif

///////////////////////////////////////////////////////////////////////////
// VDSignalBase - portable implementation using std::mutex + std::condition_variable

VDSignal::VDSignal() {
	mAutoReset = true;
}

VDSignalPersistent::VDSignalPersistent() {
	mAutoReset = false;
}

void VDSignalBase::signal() {
	{
		std::lock_guard<std::mutex> lock(mMutex);
		mSignaled = true;
	}
	if (mAutoReset)
		mCondVar.notify_one();
	else
		mCondVar.notify_all();
}

void VDSignalBase::wait() {
	std::unique_lock<std::mutex> lock(mMutex);
	mCondVar.wait(lock, [this]{ return mSignaled; });
	if (mAutoReset)
		mSignaled = false;
}

bool VDSignalBase::check() {
	std::lock_guard<std::mutex> lock(mMutex);
	bool was = mSignaled;
	if (was && mAutoReset)
		mSignaled = false;
	return was;
}

int VDSignalBase::wait(VDSignalBase *second) {
	// Polling implementation for multi-wait
	for (;;) {
		if (check()) return 0;
		if (second->check()) return 1;
		std::this_thread::sleep_for(std::chrono::microseconds(100));
	}
}

int VDSignalBase::wait(VDSignalBase *second, VDSignalBase *third) {
	for (;;) {
		if (check()) return 0;
		if (second->check()) return 1;
		if (third->check()) return 2;
		std::this_thread::sleep_for(std::chrono::microseconds(100));
	}
}

int VDSignalBase::waitMultiple(const VDSignalBase *const *signals, int count) {
	for (;;) {
		for (int i = 0; i < count; ++i) {
			if (const_cast<VDSignalBase *>(signals[i])->check())
				return i;
		}
		std::this_thread::sleep_for(std::chrono::microseconds(100));
	}
}

bool VDSignalBase::tryWait(uint32 timeoutMillisec) {
	std::unique_lock<std::mutex> lock(mMutex);
	bool result = mCondVar.wait_for(lock, std::chrono::milliseconds(timeoutMillisec), [this]{ return mSignaled; });
	if (result && mAutoReset)
		mSignaled = false;
	return result;
}

void VDSignalPersistent::unsignal() {
	std::lock_guard<std::mutex> lock(mMutex);
	mSignaled = false;
}

///////////////////////////////////////////////////////////////////////////
// VDSemaphore - portable implementation using std::mutex + std::condition_variable

VDSemaphore::VDSemaphore(int initial)
	: mCount(initial)
{
}

VDSemaphore::~VDSemaphore() {
}

void VDSemaphore::Reset(int count) {
	std::lock_guard<std::mutex> lock(mMutex);
	mCount = count;
	if (count > 0)
		mCondVar.notify_all();
}

void VDSemaphore::Wait() {
	std::unique_lock<std::mutex> lock(mMutex);
	mCondVar.wait(lock, [this]{ return mCount > 0; });
	--mCount;
}

bool VDSemaphore::Wait(int timeout) {
	std::unique_lock<std::mutex> lock(mMutex);
	bool result = mCondVar.wait_for(lock, std::chrono::milliseconds(timeout), [this]{ return mCount > 0; });
	if (result)
		--mCount;
	return result;
}

bool VDSemaphore::TryWait() {
	std::lock_guard<std::mutex> lock(mMutex);
	if (mCount > 0) {
		--mCount;
		return true;
	}
	return false;
}

void VDSemaphore::Post() {
	{
		std::lock_guard<std::mutex> lock(mMutex);
		++mCount;
	}
	mCondVar.notify_one();
}

///////////////////////////////////////////////////////////////////////////
// VDConditionVariable - wraps std::condition_variable_any

void VDConditionVariable::Wait(VDRWLock& rwLock) noexcept {
	mCondVar.wait(rwLock.mMutex);
}

void VDConditionVariable::NotifyOne() noexcept {
	mCondVar.notify_one();
}

void VDConditionVariable::NotifyAll() noexcept {
	mCondVar.notify_all();
}
