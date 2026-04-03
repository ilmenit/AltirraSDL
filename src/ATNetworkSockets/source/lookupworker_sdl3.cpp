//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2023 Avery Lee
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

#include <stdafx.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <at/atcore/asyncdispatcher.h>
#include <vd2/system/text.h>
#include <at/atnetworksockets/socketutils_posix.h>
#include <at/atnetworksockets/internal/lookupworker.h>

ATNetLookupResult::ATNetLookupResult(ATNetSyncContext& syncContext, const wchar_t *nodename, const wchar_t *service)
	: mSyncContext(syncContext)
	, mState((int)State::Pending)
	, mNodeName(nodename)
	, mService(service)
{
}

bool ATNetLookupResult::IsAbandoned() const {
	return mRefCount == 1;
}

void ATNetLookupResult::SetAddress(const ATSocketAddress& addr) {
	vdfunction<void(const ATSocketAddress&)> fn;

	vdsynchronized(mSyncContext.mMutex) {
		mAddress = addr;
		mState = (int)State::Completed;

		if (mpOnCompleteDispatcher) {
			mpOnCompleteDispatcher->Queue(&mOnCompleteDispatcherToken,
				[self = vdrefptr(this)]() {
					vdfunction<void(const ATSocketAddress&)> fn2;

					vdsynchronized(self->mSyncContext.mMutex) {
						self->mpOnCompleteDispatcher = nullptr;
						fn2 = std::move(self->mpOnCompleteFn);
					}

					if (fn2)
						fn2(self->mAddress);
				}
			);
		} else
			fn = std::move(mpOnCompleteFn);
	}

	if (fn)
		fn(mAddress);
}

void ATNetLookupResult::SetOnCompleted(IATAsyncDispatcher *dispatcher, vdfunction<void(const ATSocketAddress&)> fn, bool callIfReady) {
	vdsynchronized(mSyncContext.mMutex) {
		if (Completed()) {
			if (callIfReady && fn)
				fn(mAddress);
		} else {
			if (mpOnCompleteDispatcher) {
				mpOnCompleteDispatcher->Cancel(&mOnCompleteDispatcherToken);
				mpOnCompleteDispatcher = nullptr;
			}

			mpOnCompleteDispatcher = dispatcher;
			mpOnCompleteFn = std::move(fn);
		}
	}
}

bool ATNetLookupResult::Completed() const {
	return (State)(int)mState == State::Completed;
}

bool ATNetLookupResult::Succeeded() const {
	return (State)(int)mState == State::Completed && mAddress.IsValid();
}

const ATSocketAddress& ATNetLookupResult::Address() const {
	return mAddress;
}

////////////////////////////////////////////////////////////////////////////////

ATNetLookupWorker::ATNetLookupWorker()
	: VDThread("Net lookup worker")
{
	mpSyncContext = new ATNetSyncContext;
}

ATNetLookupWorker::~ATNetLookupWorker() {
	Shutdown();
}

bool ATNetLookupWorker::Init() {
	return ThreadStart();
}

void ATNetLookupWorker::Shutdown() {
	vdsynchronized(mMutex) {
		mbExitRequested = true;
	}

	mWakeSignal.signal();

	ThreadWait();

	while(!mPendingQueue.empty()) {
		auto *p = mPendingQueue.back();
		mPendingQueue.pop_back();

		p->Release();
	}
}

vdrefptr<IATNetLookupResult> ATNetLookupWorker::Lookup(const wchar_t *nodename, const wchar_t *service) {
	vdrefptr p(new ATNetLookupResult(*mpSyncContext, nodename, service));

	bool signal = false;

	vdsynchronized(mMutex) {
		signal = mPendingQueue.empty();
		mPendingQueue.push_back(p);
	}

	p->AddRef();

	if (signal)
		mWakeSignal.signal();

	return std::move(p);
}

void ATNetLookupWorker::ThreadRun() {
	for(;;) {
		vdrefptr<ATNetLookupResult> result;

		vdsynchronized(mMutex) {
			if (mbExitRequested)
				break;

			if (!mPendingQueue.empty()) {
				result.set(mPendingQueue.front());
				mPendingQueue.pop_front();
			}
		}

		if (!result) {
			mWakeSignal.wait();
			continue;
		}

		if (result->IsAbandoned())
			continue;

		// Convert wide strings to UTF-8 for getaddrinfo
		VDStringA nodeNameU8 = VDTextWToU8(result->mNodeName);
		VDStringA serviceU8 = VDTextWToU8(result->mService);

		struct addrinfo *addrResult = nullptr;
		ATSocketAddress foundAddr;

		if (0 == getaddrinfo(nodeNameU8.c_str(), serviceU8.c_str(), nullptr, &addrResult) && addrResult) {
			for(struct addrinfo *addr = addrResult; addr; addr = addr->ai_next) {
				ATSocketAddress foundAddr2;

				foundAddr2 = ATSocketFromNativeAddress(addr->ai_addr);

				if (foundAddr2.GetType() != ATSocketAddressType::None) {
					foundAddr = foundAddr2;

					if (foundAddr2.GetType() == ATSocketAddressType::IPv6)
						break;
				}
			}
		}

		if (addrResult) {
			freeaddrinfo(addrResult);
		}

		result->SetAddress(foundAddr);
	}
}
