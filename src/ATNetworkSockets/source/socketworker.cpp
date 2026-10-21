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
#include <WinSock2.h>
#include <bitset>
#include <at/atcore/asyncdispatcher.h>
#include <at/atnetworksockets/internal/socketworker.h>
#include <at/atnetworksockets/internal/socketutils.h>
#include <at/atnetworksockets/socketutils_win32.h>

//#define ATNETSOCKET_TRACE(...) VDDEBUG(__VA_ARGS__)
#define ATNETSOCKET_TRACE(...) ((void)0)

////////////////////////////////////////////////////////////////////////////////

ATNetSocket::ATNetSocket(ATNetSocketSyncContext& syncContext)
	: mpSyncContext(&syncContext)
{
}

ATNetSocket::~ATNetSocket() {
	// we must always Shutdown() the socket before beginning dtor
	VDASSERT(mSocketHandle == INVALID_SOCKET);
}

void ATNetSocket::Shutdown() {
	vdfunction<void(const ATSocketStatus&)> eventFn;

	vdsynchronized(mpSyncContext->mCallbackMutex) {
		if (mpOnEventDispatcher) {
			mpOnEventDispatcher->Cancel(&mOnEventToken);
			mpOnEventDispatcher = nullptr;
		}

		// The event function can have a ref on its own socket when it
		// closes the socket. We need to break this dependency loop to allow
		// the socket to deallocate.
		eventFn = std::move(mpOnEventFn);
	}

	if (mSocketHandle != INVALID_SOCKET) {
		closesocket(mSocketHandle);
		mSocketHandle = INVALID_SOCKET;
	}
}

bool ATNetSocket::IsAbandoned() const {
	return mRefCount == 1;
}

bool ATNetSocket::IsHardClosing_Locked() const {
	return mState == State::Close;
}

int ATNetSocket::Release() {
	// Fork: releasing the caller's ref and notifying the collector must be
	// atomic with the worker's abandonment check. Otherwise the worker can
	// delete this socket after the decrement but before mSocketIndex is read.
	vdrefptr context(mpSyncContext);
	int rc;
	vdsynchronized(context->mMutex) {
		rc = --mRefCount;
		VDASSERT(rc >= 0);
		if (rc == 1 && mSocketIndex >= 0 && context->mpWorker)
			context->mpWorker->RequestSocketUpdate_Locked(*this);
	}

	// Never run destruction (including captured callback releases) under
	// the socket mutex. The context ref keeps the mutex alive through unlock.
	if (!rc) delete this;
	return rc;
}

void ATNetSocket::QueueError(ATSocketError error) {
	vdsynchronized(mpSyncContext->mMutex) {
		QueueError_Locked(error);
	}
}

void ATNetSocket::QueueError_Locked(ATSocketError error) {
	if (mError == ATSocketError::None) {
		mError = error;
		QueueEvent_Locked();
	}

	CloseSocket_Locked(true);
}

void ATNetSocket::QueueWinsockError() {
	QueueWinsockError(WSAGetLastError());
}

void ATNetSocket::QueueWinsockError_Locked() {
	QueueWinsockError_Locked(WSAGetLastError());
}

void ATNetSocket::QueueWinsockError(int winsockError) {
	vdsynchronized(mpSyncContext->mMutex) {
		QueueWinsockError_Locked(winsockError);
	}
}

void ATNetSocket::QueueWinsockError_Locked(int winsockError) {
	QueueError_Locked(ATSocketError::Unknown);
}

void ATNetSocket::SetOnEvent(IATAsyncDispatcher *dispatcher, vdfunction<void(const ATSocketStatus&)> fn, bool callIfReady) {
	bool callNow = false;

	ATSocketStatus status {};

	vdfunction<void(const ATSocketStatus&)> oldFn;
	bool haveFn = (fn != nullptr);

	vdsynchronized(mpSyncContext->mCallbackMutex) {
		if (mpOnEventDispatcher) {
			mpOnEventDispatcher->Cancel(&mOnEventToken);
			mpOnEventDispatcher = nullptr;
		}

		mpOnEventDispatcher = dispatcher;
		oldFn = std::move(mpOnEventFn);
		mpOnEventFn = std::move(fn);
	}

	oldFn = nullptr;

	if (callIfReady) {
		vdsynchronized(mpSyncContext->mMutex) {
			if (haveFn && (!mbWaitingForCanReadSocket || mbWaitingForCanWriteSocket)) {
				callNow = true;
				status = GetSocketStatus_Locked();
			}
		}
	}

	if (callNow) {
		vdsynchronized(mpSyncContext->mCallbackMutex) {
			if (mpOnEventFn)
				mpOnEventFn(status);
		}
	}
}

ATSocketStatus ATNetSocket::GetSocketStatus() const {
	vdsynchronized(mpSyncContext->mMutex) {
		return GetSocketStatus_Locked();
	}
}

void ATNetSocket::CloseSocket(bool force) {
	vdsynchronized(mpSyncContext->mMutex) {
		CloseSocket_Locked(force);
	}
}

void ATNetSocket::PollSocket() {
	QueueEvent();
	FlushEvent();
}

void ATNetSocket::QueueEvent() {
	vdsynchronized(mpSyncContext->mMutex) {
		QueueEvent_Locked();
	}
}

void ATNetSocket::QueueEvent_Locked() {
	mbEventPending = true;
}

void ATNetSocket::FlushEvent() {
	// check if we actually have an event pending
	vdsynchronized(mpSyncContext->mMutex) {
		if (!mbEventPending)
			return;

		mbEventPending = false;
	}

	// capture socket status
	ATSocketStatus status = GetSocketStatus();

	// issue callback
	vdsynchronized(mpSyncContext->mCallbackMutex) {
		if (mpOnEventFn) {
			if (mpOnEventDispatcher) {
				mpOnEventDispatcher->Queue(&mOnEventToken,
					[self = vdrefptr(this), status]() {
						vdsynchronized(self->mpSyncContext->mCallbackMutex) {
							if (self->mpOnEventFn)
								self->mpOnEventFn(status);
						}
					}
				);
			} else {
				if (mpOnEventFn)
					mpOnEventFn(status);
			}
		}
	}
}

void ATNetSocket::CloseSocket_Locked(bool force) {
	if (mState != State::Closed && (mState != State::Closing || force) && mState != State::Close) {
		if (force)
			mState = State::Close;
		else {
			mState = State::Closing;

			// We want this to mimic the behavior of a close() on the socket, but
			// we may have write data buffered that hasn't been sent to Winsock
			// yet, so we only close the receive side while the write buffering
			// code does its work.
			mbRequestedShutdownRecv = true;
		}

		if (mpSyncContext->mpWorker)
			mpSyncContext->mpWorker->RequestSocketUpdate_Locked(*this);
	}
}

////////////////////////////////////////////////////////////////////////////////

ATNetStreamSocket::ATNetStreamSocket(ATNetSocketSyncContext& syncContext)
	: ATNetSocketT(syncContext)
{
	mReadBuffer.resize(4096);
	mWriteBuffer.resize(4096);

	mReadLowThreshold = 2048;
	mWriteHighThreshold = 2048;
}

ATNetStreamSocket::ATNetStreamSocket(ATNetSocketSyncContext& syncContext, const ATSocketAddress& connectedAddress, SOCKET socket)
	: ATNetStreamSocket(syncContext)
{
	mConnectAddress = connectedAddress;
	mSocketHandle = socket;

	vdsynchronized(mpSyncContext->mMutex) {
		UpdateLocalAddress_Locked();

		mState = State::Accept;
	}
}

ATNetStreamSocket::~ATNetStreamSocket() {
}

void ATNetStreamSocket::Listen(const ATSocketAddress& socketAddress) {
	vdsynchronized(mpSyncContext->mMutex) {
		if (mState == State::Created) {
			mState = State::Listen;

			mConnectAddress = socketAddress;

			if (mpSyncContext->mpWorker)
				mpSyncContext->mpWorker->RequestSocketUpdate_Locked(*this);
		}
	}
}

void ATNetStreamSocket::Connect(const ATSocketAddress& socketAddress, bool dualStack) {
	vdsynchronized(mpSyncContext->mMutex) {
		if (mState == State::Created) {
			mState = State::Connect;

			mbDualStack = dualStack;
			mConnectAddress = socketAddress;

			if (mpSyncContext->mpWorker)
				mpSyncContext->mpWorker->RequestSocketUpdate_Locked(*this);
		}
	}
}

ATSocketAddress ATNetStreamSocket::GetLocalAddress() const {
	return mLocalAddress;
}

ATSocketAddress ATNetStreamSocket::GetRemoteAddress() const {
	return mConnectAddress;
}

sint32 ATNetStreamSocket::Recv(void *buf, uint32 len) {
	if (!len)
		return 0;

	uint32 actual = 0;
	uint32 bufSize = mReadBuffer.size();
	uint32 wasRead = 0;

	for(;;) {
		uint32 avail;

		vdsynchronized(mpSyncContext->mMutex) {
			if (mState == State::Connect)
				return 0;

			if (mState != State::Connecting && mState != State::Connected && mState != State::Closing)
				return -1;

			if (wasRead) {
				bool needUpdate = false;

				if (mReadLevel > mReadLowThreshold && mReadLevel - wasRead <= mReadLowThreshold)
					needUpdate = true;

				mReadLevel -= wasRead;

				if (needUpdate && mpSyncContext->mpWorker)
					mpSyncContext->mpWorker->RequestSocketUpdate_Locked(*this);

				wasRead = 0;
			}

			avail = mReadLevel;
			if (!avail)
				mbWaitingForCanReadSocket = true;
		}

		if (!avail || !len)
			break;

		const uint32 toCopy = std::min<uint32>(std::min<uint32>(avail, len), bufSize - mReadHeadOffset);

		memcpy(buf, &mReadBuffer[mReadHeadOffset], toCopy);
		buf = (char *)buf + toCopy;
		len -= toCopy;
		mReadHeadOffset += toCopy;

		if (mReadHeadOffset >= bufSize)
			mReadHeadOffset = 0;

		wasRead = toCopy;
		actual += toCopy;
	}

	return actual;
}

sint32 ATNetStreamSocket::Send(const void *buf, uint32 len) {
	if (!len)
		return 0;

	uint32 actual = 0;
	uint32 bufSize = mWriteBuffer.size();
	uint32 wasWritten = 0;

	for(;;) {
		uint32 avail;

		vdsynchronized(mpSyncContext->mMutex) {
			if (mState != State::Connecting && mState != State::Connected && mState != State::Closing)
				return -1;

			if (wasWritten) {
				bool needUpdate = false;

				if (mWriteLevel == 0)
					needUpdate = true;

				mWriteLevel += wasWritten;

				if (needUpdate && mpSyncContext->mpWorker)
					mpSyncContext->mpWorker->RequestSocketUpdate_Locked(*this);

				wasWritten = 0;
			}

			avail = bufSize - mWriteLevel;

			if (!avail && len)
				mbWaitingForCanWriteSocket = true;
		}

		if (!avail || !len)
			break;

		const uint32 toCopy = std::min<uint32>(std::min<uint32>(avail, len), bufSize - mWriteTailOffset);

		memcpy(&mWriteBuffer[mWriteTailOffset], buf, toCopy);
		buf = (const char *)buf + toCopy;
		len -= toCopy;
		mWriteTailOffset += toCopy;

		if (mWriteTailOffset >= bufSize)
			mWriteTailOffset = 0;

		wasWritten = toCopy;
		actual += toCopy;
	}

	return actual;
}

void ATNetStreamSocket::ShutdownSocket(bool send, bool receive) {
	if ((mbRequestedShutdownSend || !send) && (mbRequestedShutdownRecv || !receive))
		return;

	vdsynchronized(mpSyncContext->mMutex) {
		bool changed = false;

		if (send && !mbRequestedShutdownSend) {
			mbRequestedShutdownSend = true;
			changed = true;
		}

		if (receive && !mbRequestedShutdownRecv) {
			mbRequestedShutdownRecv = true;
			changed = true;
		}

		if (changed && mpSyncContext->mpWorker) {
			mpSyncContext->mpWorker->RequestSocketUpdate_Locked(*this);
		}
	}
}

void ATNetStreamSocket::Update() {
	bool doRead = false;
	bool doWrite = false;

	vdsynchronized(mpSyncContext->mMutex) {
		if (mState == State::Accept) {
			mState = State::Connected;

			InitSocket_Locked();
		} else if (mState == State::Connect) {
			mState = State::Connecting;

			VDASSERT(mSocketHandle == INVALID_SOCKET);

			if (mConnectAddress.mType == ATSocketAddressType::IPv4)
				mSocketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			else if (mConnectAddress.mType == ATSocketAddressType::IPv6) {
				mSocketHandle = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);

				DWORD v6only = 0;
				if (mbDualStack)
					VDVERIFY(0 == setsockopt(mSocketHandle, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&v6only, sizeof v6only));
			} else
				QueueError_Locked(ATSocketError::Unknown);

			if (mSocketHandle == INVALID_SOCKET) {
				QueueWinsockError_Locked();
			} else if (InitSocket_Locked()) {
				ATSocketNativeAddress nativeAddr(mConnectAddress);
				int r = connect(mSocketHandle, nativeAddr.GetSockAddr(), nativeAddr.GetSockAddrLen());
				if (r) {
					int err = WSAGetLastError();
					if (err != WSAEWOULDBLOCK)
						QueueWinsockError_Locked();
				} else {
					UpdateLocalAddress_Locked();
				}
			}
		} else if (mState == State::Connected || mState == State::Closing) {
			VDASSERT(mSocketHandle != INVALID_SOCKET);

			bool shutdownSend = mbRequestedShutdownSend && !mbSocketShutdownSend && mWriteLevel == 0;
			bool shutdownRecv = mbRequestedShutdownRecv && !mbSocketShutdownRecv;

			if (shutdownSend || shutdownRecv) {
				if (shutdownSend)
					mbSocketShutdownSend = true;

				if (shutdownRecv)
					mbSocketShutdownRecv = true;

				shutdown(mSocketHandle, shutdownSend ? shutdownRecv ? SD_BOTH : SD_SEND : SD_RECEIVE);
			}

			doWrite = true;
			
			if (mState == State::Closing) {
				if (mWriteLevel == 0) {
					DoClose_Locked();
					doWrite = false;
				}
			} else {
				doRead = true;
			}
		} else if (mState == State::Close) {
			if (mSocketHandle != INVALID_SOCKET) {
				// force a hard close
				LINGER linger {};
				linger.l_onoff = TRUE;
				linger.l_linger = 0;
				setsockopt(mSocketHandle, SOL_SOCKET, SO_LINGER, (const char *)&linger, sizeof linger);
			}

			DoClose_Locked();
		}
	}

	if (doRead && mbSocketCanRead)
		DoRead();

	if (doWrite && mbSocketCanWrite)
		DoWrite();

	FlushEvent();
}

void ATNetStreamSocket::HandleSocketSignal() {
	if (mSocketHandle == INVALID_SOCKET)
		return;

	WSANETWORKEVENTS events {};

	if (0 != WSAEnumNetworkEvents(mSocketHandle, mSocketSignal.getHandle(), &events)) {
		QueueWinsockError();
		FlushEvent();
		return;
	}

	if (events.lNetworkEvents & FD_CONNECT) {
		if (events.iErrorCode[FD_CONNECT_BIT]) {
			QueueWinsockError(events.iErrorCode[FD_CONNECT_BIT]);
			FlushEvent();
			return;
		}

		vdsynchronized(mpSyncContext->mMutex) {
			if (mState == State::Connecting)
				mState = State::Connected;

			UpdateLocalAddress_Locked();
			QueueEvent_Locked();
		}
	}

	if (events.lNetworkEvents & FD_READ) {
		if (events.iErrorCode[FD_READ_BIT]) {
			QueueWinsockError(events.iErrorCode[FD_READ_BIT]);
			FlushEvent();
			return;
		}

		mbSocketCanRead = true;
		DoRead();
	}

	if (events.lNetworkEvents & FD_WRITE) {
		if (events.iErrorCode[FD_WRITE_BIT]) {
			QueueWinsockError(events.iErrorCode[FD_WRITE_BIT]);
			FlushEvent();
			return;
		}

		mbSocketCanWrite = true;
		DoWrite();
	}

	if (events.lNetworkEvents & FD_CLOSE) {
		if (events.iErrorCode[FD_CLOSE_BIT]) {
			QueueWinsockError(events.iErrorCode[FD_CLOSE_BIT]);
			FlushEvent();
			return;
		}

		vdsynchronized(mpSyncContext->mMutex) {
			mbSocketRemoteClosed = true;
			QueueEvent_Locked();
		}
	}

	FlushEvent();
}

ATSocketStatus ATNetStreamSocket::GetSocketStatus_Locked() const {
	ATSocketStatus status {};

	if (mState != State::Closing) {
		status.mbCanRead = mReadLevel > 0;
		status.mbCanWrite = mWriteLevel < mWriteBuffer.size();
	}

	status.mbClosed = mState == State::Closed;

	// Created typically means that a lookup is pending, so we count it as connecting.
	status.mbConnecting = mState == State::Created || mState == State::Connect || mState == State::Connecting;

	status.mbRemoteClosed = mbSocketRemoteClosed;
	status.mError = mError;

	return status;
}

bool ATNetStreamSocket::InitSocket_Locked() {
	BOOL nodelay = TRUE;
	if (setsockopt(mSocketHandle, IPPROTO_TCP, TCP_NODELAY, (const char *)&nodelay, sizeof nodelay)) {
		VDDEBUG("Sockets: Unable to disable nagling.\n");
	}

	// make out of band data inline for reliable Telnet -- this avoids the need to try
	// to compensate for differences in TCB Urgent data handling, as well as annoyances
	// in OOB and Async interfaces at Winsock level
	BOOL oobinline = TRUE;
	setsockopt(mSocketHandle, SOL_SOCKET, SO_OOBINLINE, (const char *)&oobinline, sizeof oobinline);

	if (0 != WSAEventSelect(mSocketHandle, mSocketSignal.getHandle(), FD_READ | FD_WRITE | FD_CONNECT | FD_CLOSE)) {
		QueueWinsockError_Locked();
		return false;
	}

	return true;
}

void ATNetStreamSocket::UpdateLocalAddress_Locked() {
	if (mSocketHandle != INVALID_SOCKET) {
		union {
			char buf[256] {};
			sockaddr sa;
		} addr {};
		int addrLen = sizeof(addr);

		if (0 == getsockname(mSocketHandle, &addr.sa, &addrLen))
			mLocalAddress = ATSocketFromNativeAddress(&addr.sa);
	}
}

void ATNetStreamSocket::DoRead() {
	if (!mbSocketCanRead)
		return;

	VDASSERT(mSocketHandle != INVALID_SOCKET);

	const uint32 rsize = mReadBuffer.size();
	uint32 wasRead = 0;

	for(;;) {
		uint32 avail;

		vdsynchronized(mpSyncContext->mMutex) {
			if (wasRead) {
				mReadLevel += wasRead;
				wasRead = 0;

				if (mbWaitingForCanReadSocket) {
					mbWaitingForCanReadSocket = false;

					QueueEvent_Locked();
				}
			}

			avail = rsize - mReadLevel;
			if (!avail)
				break;
		}

		const uint32 toRead = std::min<uint32>(avail, rsize - mReadTailOffset);
		VDASSERT(toRead > 0);

		int r = recv(mSocketHandle, &mReadBuffer[mReadTailOffset], (int)toRead, 0);
		if (r < 0) {
			int err = WSAGetLastError();
			if (err == WSAEWOULDBLOCK) {
				mbSocketCanRead = false;
				break;
			}

			QueueWinsockError(err);
			return;
		}

		if (r == 0) {
			mbSocketCanRead = false;
			// soft close occurred
			break;
		}

		wasRead = r;
		mReadTailOffset += wasRead;
		if (mReadTailOffset >= rsize)
			mReadTailOffset = 0;
	}
}

void ATNetStreamSocket::DoWrite() {
	if (!mbSocketCanWrite)
		return;

	VDASSERT(mSocketHandle != INVALID_SOCKET);

	const uint32 wsize = mWriteBuffer.size();
	uint32 wasWritten = 0;

	for(;;) {
		uint32 avail;

		vdsynchronized(mpSyncContext->mMutex) {
			if (wasWritten) {
				VDASSERT(mWriteLevel >= wasWritten);
				mWriteLevel -= wasWritten;
				wasWritten = 0;
			}

			if (mbWaitingForCanWriteSocket && mWriteLevel <= mWriteHighThreshold) {
				mbWaitingForCanWriteSocket = false;

				QueueEvent_Locked();
			}

			avail = mWriteLevel;
			if (!avail) {
				if (mbRequestedShutdownSend && !mbSocketShutdownSend)
					shutdown(mSocketHandle, SD_SEND);

				if (mState == State::Closing)
					DoClose_Locked();

				break;
			}
		}

		const uint32 toWrite = std::min<uint32>(avail, wsize - mWriteHeadOffset);
		VDASSERT(toWrite > 0);

		int r = send(mSocketHandle, &mWriteBuffer[mWriteHeadOffset], (int)toWrite, 0);
		if (r < 0) {
			int err = WSAGetLastError();
			if (err == WSAEWOULDBLOCK) {
				mbSocketCanWrite = false;
				break;
			}

			QueueWinsockError(err);
			return;
		}

		if (r == 0) {
			mbSocketCanWrite = false;
			break;
		}

		wasWritten = r;
		mWriteHeadOffset += wasWritten;
		if (mWriteHeadOffset >= wsize)
			mWriteHeadOffset = 0;
	}
}

void ATNetStreamSocket::DoClose_Locked() {
	VDASSERT(mState == State::Closing || mState == State::Close);

	if (mSocketHandle != INVALID_SOCKET) {
		closesocket(mSocketHandle);
		mSocketHandle = INVALID_SOCKET;

		mSocketSignal.unsignal();
	}

	QueueEvent_Locked();
	mState = State::Closed;

	if (mpSyncContext->mpWorker)
		mpSyncContext->mpWorker->RequestSocketUpdate_Locked(*this);
}

////////////////////////////////////////////////////////////////////////////////

ATNetListenSocket::ATNetListenSocket(ATNetSocketSyncContext& syncContext, const ATSocketAddress& bindAddress, bool dualStack)
	: ATNetSocketT(syncContext)
	, mBindAddress(bindAddress)
	, mbDualStack(dualStack)
{
	vdsynchronized(mpSyncContext->mMutex) {
		mState = State::Listen;
	}
}

ATNetListenSocket::~ATNetListenSocket() {
}

vdrefptr<IATStreamSocket> ATNetListenSocket::Accept() {
	vdrefptr<IATStreamSocket> acceptedSocket;
	SOCKET pendingSocket;

	vdsynchronized(mpSyncContext->mMutex) {
		pendingSocket = std::exchange(mPendingSocket, INVALID_SOCKET);
	}

	if (pendingSocket != INVALID_SOCKET) {
		if (mpSyncContext->mpWorker)
			acceptedSocket = mpSyncContext->mpWorker->CreateStreamSocket(mPendingAddress, pendingSocket);
		else
			closesocket(pendingSocket);
	}

	// Now that we have accepted a socket, make sure we have another accept()
	// pending. We do this here instead of in TryAccept_Locked() because accept()
	// can complete synchronously, and we'd rather hold off pending connections
	// in the networking layer than pile up sockets here.
	vdsynchronized(mpSyncContext->mMutex) {
		TryAccept_Locked();

		// We are in a little bit of a dangerous situation here as we may have
		// another socket pending but definitely don't want to risk doing a
		// recursive call into the same event handler that is probably calling
		// Accept() already. Queue an update to flush the pending event.
		if (!mbEventPending && mpSyncContext->mpWorker)
			mpSyncContext->mpWorker->RequestSocketUpdate_Locked(*this);
	}

	return acceptedSocket;
}

void ATNetListenSocket::Shutdown() {
	if (mPendingSocket != INVALID_SOCKET) {
		closesocket(mPendingSocket);
		mPendingSocket = INVALID_SOCKET;
	}

	ATNetSocketT::Shutdown();
}

void ATNetListenSocket::Update() {
	vdsynchronized(mpSyncContext->mMutex) {
		if (mState == State::Listen) {
			mState = State::Listening;

			VDASSERT(mSocketHandle == INVALID_SOCKET);

			if (mBindAddress.mType == ATSocketAddressType::IPv4)
				mSocketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			else if (mBindAddress.mType == ATSocketAddressType::IPv6)
				mSocketHandle = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
			else
				QueueError_Locked(ATSocketError::Unknown);

			if (mSocketHandle == INVALID_SOCKET) {
				QueueWinsockError_Locked();
			} else {
				ATSocketNativeAddress nativeBindAddress(mBindAddress);
				DWORD v6only = 0;
				if (mbDualStack && 0 != setsockopt(mSocketHandle, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&v6only, sizeof v6only)) {
					QueueWinsockError_Locked();
				} else if (0 != bind(mSocketHandle, nativeBindAddress.GetSockAddr(), nativeBindAddress.GetSockAddrLen())) {
					QueueWinsockError_Locked();
				} else if (0 != listen(mSocketHandle, SOMAXCONN)) {
					QueueWinsockError_Locked();
				} else if (0 != WSAEventSelect(mSocketHandle, mSocketSignal.getHandle(), FD_ACCEPT)) {
					QueueWinsockError_Locked();
				} else {
					TryAccept_Locked();
				}
			}
		} else if (mState == State::Close || mState == State::Closing) {
			if (mSocketHandle != INVALID_SOCKET) {
				closesocket(mSocketHandle);
				mSocketHandle = INVALID_SOCKET;

				mSocketSignal.unsignal();
			}

			QueueEvent_Locked();
			mState = State::Closed;

			if (mpSyncContext->mpWorker)
				mpSyncContext->mpWorker->RequestSocketUpdate_Locked(*this);
		}
	}

	FlushEvent();
}

void ATNetListenSocket::HandleSocketSignal() {
	if (mSocketHandle == INVALID_SOCKET)
		return;

	WSANETWORKEVENTS events {};

	if (0 != WSAEnumNetworkEvents(mSocketHandle, mSocketSignal.getHandle(), &events)) {
		QueueWinsockError();
		FlushEvent();
		return;
	}


	if (events.lNetworkEvents & FD_ACCEPT) {
		if (events.iErrorCode[FD_ACCEPT_BIT]) {
			QueueWinsockError(events.iErrorCode[FD_ACCEPT_BIT]);
			FlushEvent();
			return;
		}

		vdsynchronized(mpSyncContext->mMutex) {
			TryAccept_Locked();
		}
	}

	FlushEvent();
}

ATSocketStatus ATNetListenSocket::GetSocketStatus_Locked() const {
	ATSocketStatus status {};

	status.mbClosed = mState == State::Closed;
	status.mbCanAccept = mPendingSocket != INVALID_SOCKET;
	status.mError = mError;

	return status;
}

void ATNetListenSocket::TryAccept_Locked() {
	if (mPendingSocket != INVALID_SOCKET)
		return;

	union {
		char buf[256] {};
		sockaddr sa;
	} addr {};
	int addrLen = sizeof(addr);

	SOCKET newSocket = accept(mSocketHandle, &addr.sa, &addrLen);
	if (newSocket == INVALID_SOCKET) {
		int err = WSAGetLastError();
		if (err != WSAEWOULDBLOCK) {
			QueueWinsockError(err);
		}

		return;
	}

	mPendingAddress = ATSocketFromNativeAddress(&addr.sa);
	mPendingSocket = newSocket;
	QueueEvent_Locked();
}

////////////////////////////////////////////////////////////////////////////////

ATNetDatagramSocket::ATNetDatagramSocket(ATNetSocketSyncContext& syncContext, const ATSocketAddress& bindAddress, bool dualStack)
	: ATNetSocketT(syncContext)
	, mbDualStack(dualStack)
{
	mReadBuffer.resize(4096);
	mWriteBuffer.resize(4096);

	mWriteHighThreshold = sizeof(uint16) + sizeof(ATSocketAddress) + kMaxDatagramSize;
	mReadLowThreshold = 4096 - mWriteHighThreshold;

	vdsynchronized(mpSyncContext->mMutex) {
		mState = State::Connect;
		mBindAddress = bindAddress;

		if (mpSyncContext->mpWorker)
			mpSyncContext->mpWorker->RequestSocketUpdate_Locked(*this);
	}
}

ATNetDatagramSocket::~ATNetDatagramSocket() {
}

sint32 ATNetDatagramSocket::RecvFrom(ATSocketAddress& address, void *data, uint32 maxlen) {
	uint32 bufSize = mReadBuffer.size();
	sint32 readLen = -1;
	uint32 wasRead = 0;

	for(;;) {
		uint32 avail;

		vdsynchronized(mpSyncContext->mMutex) {
			if (mState != State::Connect && mState != State::Connected)
				return -1;

			if (wasRead) {
				bool needUpdate = false;

				if (mReadLevel > mReadLowThreshold && mReadLevel - wasRead <= mReadLowThreshold)
					needUpdate = true;

				mReadLevel -= wasRead;

				if (needUpdate && mpSyncContext->mpWorker) {
					ATNETSOCKET_TRACE("datagram socket waking up for read space available\n");
					mpSyncContext->mpWorker->RequestSocketUpdate_Locked(*this);
				}

				wasRead = 0;
			}

			if (readLen >= 0)
				return readLen;

			avail = mReadLevel;
			if (!avail) {
				mbWaitingForCanReadSocket = true;
				return -1;
			}
		}

		VDASSERT(avail >= sizeof(uint16) + sizeof(ATSocketAddress));

		uint16 len16 = 0;

		mReadHeadOffset = SplitRead(&len16, sizeof len16, mReadBuffer.data(), mReadHeadOffset, bufSize);
		mReadHeadOffset = SplitRead(&address, sizeof address, mReadBuffer.data(), mReadHeadOffset, bufSize);

		if (len16 > maxlen) {
			mReadHeadOffset += len16;
			if (mReadHeadOffset >= bufSize)
				mReadHeadOffset -= bufSize;
		} else {
			VDASSERT(avail >= sizeof(uint16) + sizeof(ATSocketAddress) + len16); 
			mReadHeadOffset = SplitRead(data, len16, mReadBuffer.data(), mReadHeadOffset, bufSize);

			readLen = (sint32)len16;
		}

		wasRead = sizeof(uint16) + sizeof(ATSocketAddress) + len16;
	}
}

bool ATNetDatagramSocket::SendTo(const ATSocketAddress& address, const void *data, uint32 len) {
	if (len > kMaxDatagramSize) {
		VDASSERT(len <= kMaxDatagramSize);
		return false;
	}

	// If we are an IPv6 socket and trying to send to IPv4, rewrap the address for IPv6 as
	// required by the socket layer.
	ATSocketAddress address2(address);

	if (mBindAddress.mType == ATSocketAddressType::IPv6 && address2.mType == ATSocketAddressType::IPv4)
		address2 = ATSocketAddress::CreateIPv4InIPv6(mBindAddress);

	const uint32 neededLen = sizeof(uint16) + sizeof(ATSocketAddress) + len;

	uint32 bufSize = mWriteBuffer.size();
	uint32 wasWritten = 0;

	for(;;) {
		uint32 avail;

		vdsynchronized(mpSyncContext->mMutex) {
			if (mState != State::Connect && mState != State::Connected)
				return false;

			if (wasWritten) {
				bool needUpdate = false;

				if (mWriteLevel == 0)
					needUpdate = true;

				mWriteLevel += wasWritten;

				if (needUpdate && mpSyncContext->mpWorker)
					mpSyncContext->mpWorker->RequestSocketUpdate_Locked(*this);

				wasWritten = 0;
				break;
			}

			avail = bufSize - mWriteLevel;

			if (avail < neededLen) {
				mbWaitingForCanWriteSocket = true;
				// Fork: no datagram was queued; let the caller retry.
				return false;
			}
		}

		const uint16 len16 = (uint16)len;

		mWriteTailOffset = SplitWrite(mWriteBuffer.data(), mWriteTailOffset, bufSize, &len16, sizeof len16);
		mWriteTailOffset = SplitWrite(mWriteBuffer.data(), mWriteTailOffset, bufSize, &address2, sizeof address2);
		mWriteTailOffset = SplitWrite(mWriteBuffer.data(), mWriteTailOffset, bufSize, data, len);

		wasWritten = sizeof(uint16) + sizeof(ATSocketAddress) + len;
	}

	return true;
}

void ATNetDatagramSocket::Update() {
	bool doRead = false;
	bool doWrite = false;

	vdsynchronized(mpSyncContext->mMutex) {
		if (mState == State::Connect) {
			mState = State::Connected;

			VDASSERT(mSocketHandle == INVALID_SOCKET);

			if (mBindAddress.mType == ATSocketAddressType::IPv4)
				mSocketHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
			else if (mBindAddress.mType == ATSocketAddressType::IPv6)
				mSocketHandle = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
			else
				QueueError_Locked(ATSocketError::Unknown);

			if (mSocketHandle != INVALID_SOCKET) {
				if (mbDualStack) {
					DWORD v6only = 0;
					if (0 != setsockopt(mSocketHandle, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&v6only, sizeof v6only))
						QueueWinsockError_Locked();
				}

				if (mBindAddress.IsNonZero()) {
					ATSocketNativeAddress nativeBindAddress(mBindAddress);

					if (0 != bind(mSocketHandle, nativeBindAddress.GetSockAddr(), nativeBindAddress.GetSockAddrLen()))
						QueueWinsockError_Locked();
				}

				if (0 != WSAEventSelect(mSocketHandle, mSocketSignal.getHandle(), FD_READ | FD_WRITE)) {
					QueueWinsockError_Locked();
				}

				if (mError == ATSocketError::None) {
					doRead = true;
					doWrite = true;
				}
			}
		} else if (mState == State::Connected || mState == State::Closing) {
			VDASSERT(mSocketHandle != INVALID_SOCKET);

			doWrite = true;
			
			if (mState == State::Closing) {
				if (mWriteLevel == 0) {
					DoClose_Locked();
					doWrite = false;
				}
			} else {
				doRead = true;
			}
		} else if (mState == State::Close) {
			DoClose_Locked();
		}
	}

	if (doRead && mbSocketCanRead)
		DoRead();

	if (doWrite && mbSocketCanWrite)
		DoWrite();

	FlushEvent();
}

void ATNetDatagramSocket::HandleSocketSignal() {
	if (mSocketHandle == INVALID_SOCKET)
		return;

	WSANETWORKEVENTS events {};

	if (0 != WSAEnumNetworkEvents(mSocketHandle, mSocketSignal.getHandle(), &events)) {
		QueueWinsockError();
		FlushEvent();
		return;
	}

	if (events.lNetworkEvents & FD_READ) {
		if (events.iErrorCode[FD_READ_BIT]) {
			ATNETSOCKET_TRACE("datagram socket read error: %u\n", events.iErrorCode[FD_READ_BIT]);

			QueueWinsockError(events.iErrorCode[FD_READ_BIT]);
			FlushEvent();
			return;
		}

		ATNETSOCKET_TRACE("datagram socket read event\n");

		mbSocketCanRead = true;
		DoRead();
	}

	if (events.lNetworkEvents & FD_WRITE) {
		if (events.iErrorCode[FD_WRITE_BIT]) {
			QueueWinsockError(events.iErrorCode[FD_WRITE_BIT]);
			FlushEvent();
			return;
		}

		mbSocketCanWrite = true;
		DoWrite();
	}

	FlushEvent();
}

ATSocketStatus ATNetDatagramSocket::GetSocketStatus_Locked() const {
	ATSocketStatus status {};

	status.mbConnecting = mState == State::Connect;
	status.mbCanRead = mReadLevel > 0;
	status.mbCanWrite = mWriteLevel <= mWriteHighThreshold;
	status.mbClosed = mState == State::Closed;
	status.mError = mError;

	return status;
}

void ATNetDatagramSocket::DoRead() {
	if (!mbSocketCanRead)
		return;

	VDASSERT(mSocketHandle != INVALID_SOCKET);

	const uint32 rsize = mReadBuffer.size();
	uint32 wasRead = 0;

	char buf[kMaxDatagramSize] {};

	union {
		sockaddr sa;
		sockaddr_in sa4;
		sockaddr_in6 sa6;
		char buf[256];
	} sa;

	for(;;) {
		uint32 avail;

		vdsynchronized(mpSyncContext->mMutex) {
			if (wasRead) {
				mReadLevel += wasRead;
				wasRead = 0;

				if (mbWaitingForCanReadSocket) {
					mbWaitingForCanReadSocket = false;

					QueueEvent_Locked();
				}
			}

			avail = rsize - mReadLevel;
		}

		if (avail < kMaxDatagramSize + sizeof(ATSocketAddress) + sizeof(uint16)) {
			ATNETSOCKET_TRACE("datagram socket suspending read for space\n");
			break;
		}

		int fromlen = sizeof(sa);
		memset(&sa.sa, 0, sizeof sa.sa);
		int r = recvfrom(mSocketHandle, buf, sizeof buf, 0, &sa.sa, &fromlen);
		if (r < 0) {
			int err = WSAGetLastError();
			if (err == WSAEWOULDBLOCK) {
				ATNETSOCKET_TRACE("recvfrom would block\n", r);
				mbSocketCanRead = false;
				break;
			}
		
			ATNETSOCKET_TRACE("recvfrom error %u\n", err);

			QueueWinsockError(err);
			return;
		}

		ATSocketAddress fromAddr = ATSocketFromNativeAddress(&sa.sa);
		ATNETSOCKET_TRACE("recvfrom got %u bytes from %s\n", r, fromAddr.ToString().c_str());

		uint16 len16 = (uint16)r;
		mReadTailOffset = SplitWrite(mReadBuffer.data(), mReadTailOffset, rsize, &len16, sizeof len16);
		mReadTailOffset = SplitWrite(mReadBuffer.data(), mReadTailOffset, rsize, &fromAddr, sizeof fromAddr);
		mReadTailOffset = SplitWrite(mReadBuffer.data(), mReadTailOffset, rsize, buf, r);

		wasRead = sizeof(fromAddr) + sizeof(uint16) + r;
	}
}

void ATNetDatagramSocket::DoWrite() {
	if (!mbSocketCanWrite)
		return;

	VDASSERT(mSocketHandle != INVALID_SOCKET);

	const uint32 wsize = mWriteBuffer.size();
	uint32 wasWritten = 0;

	char buf[kMaxDatagramSize];

	for(;;) {
		uint32 avail;

		vdsynchronized(mpSyncContext->mMutex) {
			if (wasWritten) {
				VDASSERT(mWriteLevel >= wasWritten);
				mWriteLevel -= wasWritten;
				wasWritten = 0;
			}

			if (mbWaitingForCanWriteSocket && mWriteLevel <= mWriteHighThreshold) {
				mbWaitingForCanWriteSocket = false;

				QueueEvent_Locked();
			}

			avail = mWriteLevel;
		}

		VDASSERT(avail == 0 || avail >= sizeof(ATSocketAddress)+sizeof(uint16));

		if (avail < sizeof(ATSocketAddress)+sizeof(uint16))
			break;

		// Fork: commit the queue position only after a successful send.
		uint32 nextHead = mWriteHeadOffset;
		uint16 len16 = 0;
		ATSocketAddress toAddr;
		nextHead = SplitRead(&len16, sizeof len16, mWriteBuffer.data(), nextHead, wsize);
		nextHead = SplitRead(&toAddr, sizeof toAddr, mWriteBuffer.data(), nextHead, wsize);

		VDASSERT(len16 <= kMaxDatagramSize);
		VDASSERT(avail >= sizeof(ATSocketAddress) + sizeof(uint16) + len16);
		nextHead = SplitRead(buf, len16, mWriteBuffer.data(), nextHead, wsize);

		ATSocketNativeAddress natToAddr(toAddr);

		const int r = sendto(mSocketHandle, buf, len16, 0, natToAddr.GetSockAddr(), natToAddr.GetSockAddrLen());
		if (r < 0) {
			int err = WSAGetLastError();
			if (err == WSAEWOULDBLOCK) {
				mbSocketCanWrite = false;
				break;
			}

			QueueWinsockError(err);
			return;
		}

		mWriteHeadOffset = nextHead;
		wasWritten = sizeof(uint16) + sizeof(ATSocketAddress) + len16;
	}
}

void ATNetDatagramSocket::DoClose_Locked() {
	VDASSERT(mState == State::Closing || mState == State::Close);

	if (mSocketHandle != INVALID_SOCKET) {
		closesocket(mSocketHandle);
		mSocketHandle = INVALID_SOCKET;

		mSocketSignal.unsignal();
	}

	QueueEvent_Locked();
	mState = State::Closed;

	if (mpSyncContext->mpWorker)
		mpSyncContext->mpWorker->RequestSocketUpdate_Locked(*this);
}

uint32 ATNetDatagramSocket::SplitRead(void *dst, size_t len, const void *src, uint32 srcOffset, size_t srcLen) {
	uint32 len1 = srcLen - srcOffset;
	if (len1 >= len) {
		memcpy(dst, (char *)src + srcOffset, len);
		srcOffset = srcOffset + (uint32)len;
	} else {
		memcpy(dst, (const char *)src + srcOffset, len1);
		memcpy((char *)dst + len1, src, len - len1);
		srcOffset = (uint32)(len - len1);
	}

	return srcOffset;
}

uint32 ATNetDatagramSocket::SplitWrite(void *dst, uint32 dstOffset, size_t dstLen, const void *src, size_t len) {
	uint32 len1 = dstLen - dstOffset;
	if (len1 >= len) {
		memcpy((char *)dst + dstOffset, src, len);
		dstOffset = dstOffset + (uint32)len;
	} else {
		memcpy((char *)dst + dstOffset, src, len1);
		memcpy(dst, (const char *)src + len1, len - len1);
		dstOffset = (uint32)(len - len1);
	}

	return dstOffset;
}

////////////////////////////////////////////////////////////////////////////////

ATNetSocketWorker::ATNetSocketWorker()
	: VDThread("Net socket worker")
{
	mSocketsNeedUpdate.reset();

	mpSyncContext = new ATNetSocketSyncContext;
	mpSyncContext->mpWorker = this;
}

ATNetSocketWorker::~ATNetSocketWorker() {
	Shutdown();
}

bool ATNetSocketWorker::Init() {
	if (!ThreadStart()) {
		Shutdown();
		return false;
	}

	return true;
}

void ATNetSocketWorker::Shutdown() {
	vdsynchronized(mMutex) {
		mbExitRequested = true;
	}

	mWakeSignal.signal();

	ThreadWait();
}

vdrefptr<ATNetStreamSocket> ATNetSocketWorker::CreateStreamSocket() {
	vdrefptr<ATNetStreamSocket> s(new ATNetStreamSocket(*mpSyncContext));

	bool success;
	vdsynchronized(mpSyncContext->mMutex) {
		success = RegisterSocket_Locked(*s);
	}

	if (!success) {
		s->Shutdown();
		return nullptr;
	}

	return s;
}

vdrefptr<ATNetStreamSocket> ATNetSocketWorker::CreateStreamSocket(const ATSocketAddress& connectedAddress, SOCKET socket) {
	vdrefptr<ATNetStreamSocket> s(new ATNetStreamSocket(*mpSyncContext, connectedAddress, socket));

	bool success;
	vdsynchronized(mpSyncContext->mMutex) {
		success = RegisterSocket_Locked(*s);
	}

	if (!success) {
		s->Shutdown();
		return nullptr;
	}

	return s;
}

vdrefptr<ATNetListenSocket> ATNetSocketWorker::CreateListenSocket(const ATSocketAddress& bindAddress, bool dualStack) {
	vdrefptr<ATNetListenSocket> s(new ATNetListenSocket(*mpSyncContext, bindAddress, dualStack));

	vdsynchronized(mpSyncContext->mMutex) {
		if (!RegisterSocket_Locked(*s)) {
			s->Shutdown();
			return nullptr;
		}
	}

	return s;
}

vdrefptr<ATNetDatagramSocket> ATNetSocketWorker::CreateDatagramSocket(const ATSocketAddress& bindAddress, bool dualStack) {
	vdrefptr<ATNetDatagramSocket> s(new ATNetDatagramSocket(*mpSyncContext, bindAddress, dualStack));

	vdsynchronized(mpSyncContext->mMutex) {
		if (!RegisterSocket_Locked(*s)) {
			s->Shutdown();
			return nullptr;
		}
	}

	return s;
}

void ATNetSocketWorker::RequestSocketUpdate_Locked(const ATNetSocket& socket) {
	if (socket.mSocketIndex < 0)
		return;

	if (mSocketsNeedUpdate[socket.mSocketIndex])
		return;

	mSocketsNeedUpdate[socket.mSocketIndex] = true;

	if (!mbUpdateSockets) {
		mbUpdateSockets = true;

		mWakeSignal();
	}
}

bool ATNetSocketWorker::RegisterSocket_Locked(ATNetSocket& s) {
	vdsynchronized(mpSyncContext->mMutex) {
		if (mNumSockets >= kMaxSockets)
			return false;

		mSocketTable[mNumSockets] = &s;
		s.mSocketIndex = mNumSockets++;

		RequestSocketUpdate_Locked(s);
	}

	return true;
}

void ATNetSocketWorker::ThreadRun() {
	const VDSignalBase *signalWaitArray[kMaxSockets + 1] {};
	size_t numActiveSockets = 0;

	signalWaitArray[0] = &mWakeSignal;

	for(;;) {
		int idx = VDSignalBase::waitMultiple(signalWaitArray, numActiveSockets + 1);
		if (idx < 0)
			break;

		// check if it was the wake signal
		if (idx == 0) {
			vdvector<vdrefptr<ATNetSocket>> socketsToDestroy;
			std::bitset<kMaxSockets> socketsToUpdate;
			socketsToUpdate.reset();
			
			vdsynchronized(mpSyncContext->mMutex) {
				if (mbExitRequested)
					break;

				if (mbUpdateSockets) {
					mbUpdateSockets = false;

					// Add in any new sockets.
					while(numActiveSockets < mNumSockets) {
						signalWaitArray[numActiveSockets + 1] = &mSocketTable[numActiveSockets]->mSocketSignal;
						++numActiveSockets;
					}

					// Any added sockets will be at the end, so run over them
					// in reverse order.
					for (size_t i = 0; i < numActiveSockets; ++i) {
						if (!mSocketsNeedUpdate[i])
							continue;

						mSocketsNeedUpdate[i] = false;

						ATNetSocket *sock = mSocketTable[i];
						if (sock->IsAbandoned()) {
							--mNumSockets;
							--numActiveSockets;

							sock->mSocketIndex = -1;
							signalWaitArray[i + 1] = nullptr;

							// we have to be careful not to do a Release() on a socket in here
							// since it risks a deadlock
							socketsToDestroy.emplace_back(std::move(mSocketTable[i]));
							mSocketTable[i] = nullptr;

							if (i < mNumSockets) {
								signalWaitArray[i + 1] = signalWaitArray[mNumSockets + 1];
								signalWaitArray[mNumSockets + 1] = nullptr;

								mSocketTable[i] = std::move(mSocketTable[mNumSockets]);
								mSocketTable[i]->mSocketIndex = i;
								mSocketsNeedUpdate[i] = mSocketsNeedUpdate[mNumSockets];
								mSocketsNeedUpdate[mNumSockets] = false;
							}

							--i;

							// If the socket is abandoned, hard close it even if it has already
							// been soft closed. This prevents us from having orphaned sockets
							// in the socket table that can be held open indefinitely by a remote
							// host that isn't reading the remaining data.
							if (!sock->IsHardClosing_Locked())
								sock->CloseSocket(true);

						} else {
							socketsToUpdate[i] = true;
						}
					}
				}
			}

			// update sockets now
			for(size_t i = 0; i < numActiveSockets; ++i) {
				if (socketsToUpdate[i])
					mSocketTable[i]->Update();
			}

			while(!socketsToDestroy.empty()) {
				// If the socket has been abandoned, we may still need to process
				// a pending close.
				socketsToDestroy.back()->Update();

				socketsToDestroy.back()->Shutdown();
				socketsToDestroy.pop_back();
			}

			continue;
		}

		// it was a socket signal -- have the socket do its thing
		mSocketTable[idx - 1]->HandleSocketSignal();
	}

	// dump existing sockets
	{
		vdvector<vdrefptr<ATNetSocket>> socketsToDestroy;

		vdsynchronized(mpSyncContext->mMutex) {
			for(auto& s : mSocketTable) {
				if (s) {
					socketsToDestroy.emplace_back(std::move(s));
					s = nullptr;
				}
			}

			mNumSockets = 0;

			mpSyncContext->mpWorker = nullptr;
		}

		while(!socketsToDestroy.empty()) {
			socketsToDestroy.back()->Shutdown();
			socketsToDestroy.pop_back();
		}
	}
}
