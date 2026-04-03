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
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <bitset>
#include <at/atcore/asyncdispatcher.h>
#include <at/atnetworksockets/internal/socketworker_sdl3.h>
#include <at/atnetworksockets/internal/socketutils.h>
#include <at/atnetworksockets/socketutils_posix.h>

// macOS does not have MSG_NOSIGNAL; use SO_NOSIGPIPE per-socket instead.
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

//#define ATNETSOCKET_TRACE(...) VDDEBUG(__VA_ARGS__)
#define ATNETSOCKET_TRACE(...) ((void)0)

namespace {
	bool SetNonBlocking(int fd) {
		int flags = fcntl(fd, F_GETFL, 0);
		if (flags < 0)
			return false;
		return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
	}
}

////////////////////////////////////////////////////////////////////////////////

ATNetSocket::ATNetSocket(ATNetSocketSyncContext& syncContext)
	: mpSyncContext(&syncContext)
{
}

ATNetSocket::~ATNetSocket() {
	// we must always Shutdown() the socket before beginning dtor
	VDASSERT(mSocketHandle == -1);
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

	if (mSocketHandle != -1) {
		::close(mSocketHandle);
		mSocketHandle = -1;
	}
}

bool ATNetSocket::IsAbandoned() const {
	return mRefCount == 1;
}

bool ATNetSocket::IsHardClosing_Locked() const {
	return mState == State::Close;
}

int ATNetSocket::Release() {
	const int rc = vdrefcounted::Release();

	if (rc == 1) {
		// We might be the final release -- check if we need to request a
		// socket update. If we are in the table, the table will be holding a
		// ref on the socket, so reaching 1 is final release.
		vdsynchronized(mpSyncContext->mMutex) {
			if (mSocketIndex >= 0) {
				// yup, we are registered in the table -- request an update so this socket gets collected
				if (mpSyncContext->mpWorker)
					mpSyncContext->mpWorker->RequestSocketUpdate_Locked(*this);
			}
		}
	}

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

void ATNetSocket::QueuePosixError() {
	QueuePosixError(errno);
}

void ATNetSocket::QueuePosixError_Locked() {
	QueuePosixError_Locked(errno);
}

void ATNetSocket::QueuePosixError(int posixError) {
	vdsynchronized(mpSyncContext->mMutex) {
		QueuePosixError_Locked(posixError);
	}
}

void ATNetSocket::QueuePosixError_Locked(int posixError) {
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
			// we may have write data buffered that hasn't been sent yet,
			// so we only close the receive side while the write buffering
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

ATNetStreamSocket::ATNetStreamSocket(ATNetSocketSyncContext& syncContext, const ATSocketAddress& connectedAddress, int socketFd)
	: ATNetStreamSocket(syncContext)
{
	mConnectAddress = connectedAddress;
	mSocketHandle = socketFd;

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

			VDASSERT(mSocketHandle == -1);

			if (mConnectAddress.mType == ATSocketAddressType::IPv4)
				mSocketHandle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			else if (mConnectAddress.mType == ATSocketAddressType::IPv6) {
				mSocketHandle = ::socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);

				int v6only = 0;
				if (mbDualStack)
					VDVERIFY(0 == setsockopt(mSocketHandle, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof v6only));
			} else
				QueueError_Locked(ATSocketError::Unknown);

			if (mSocketHandle == -1) {
				QueuePosixError_Locked();
			} else if (InitSocket_Locked()) {
				ATSocketNativeAddress nativeAddr(mConnectAddress);
				int r = ::connect(mSocketHandle, nativeAddr.GetSockAddr(), nativeAddr.GetSockAddrLen());
				if (r < 0) {
					if (errno != EINPROGRESS)
						QueuePosixError_Locked();
				} else {
					UpdateLocalAddress_Locked();
				}
			}
		} else if (mState == State::Connected || mState == State::Closing) {
			VDASSERT(mSocketHandle != -1);

			bool shutdownSend = mbRequestedShutdownSend && !mbSocketShutdownSend && mWriteLevel == 0;
			bool shutdownRecv = mbRequestedShutdownRecv && !mbSocketShutdownRecv;

			if (shutdownSend || shutdownRecv) {
				if (shutdownSend)
					mbSocketShutdownSend = true;

				if (shutdownRecv)
					mbSocketShutdownRecv = true;

				::shutdown(mSocketHandle, shutdownSend ? shutdownRecv ? SHUT_RDWR : SHUT_WR : SHUT_RD);
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
			if (mSocketHandle != -1) {
				// force a hard close
				struct linger ling {};
				ling.l_onoff = 1;
				ling.l_linger = 0;
				setsockopt(mSocketHandle, SOL_SOCKET, SO_LINGER, &ling, sizeof ling);
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

void ATNetStreamSocket::HandlePollEvents(short revents) {
	if (mSocketHandle == -1)
		return;

	if (revents & POLLERR) {
		int err = 0;
		socklen_t errlen = sizeof(err);
		getsockopt(mSocketHandle, SOL_SOCKET, SO_ERROR, &err, &errlen);
		QueuePosixError(err ? err : EIO);
		FlushEvent();
		return;
	}

	if (revents & POLLOUT) {
		// Check if this is a connect completion
		bool connectError = false;
		vdsynchronized(mpSyncContext->mMutex) {
			if (mState == State::Connecting) {
				int err = 0;
				socklen_t errlen = sizeof(err);
				getsockopt(mSocketHandle, SOL_SOCKET, SO_ERROR, &err, &errlen);

				if (err) {
					QueuePosixError_Locked(err);
					connectError = true;
				} else {
					mState = State::Connected;
					UpdateLocalAddress_Locked();
					QueueEvent_Locked();
				}
			}
		}

		if (connectError) {
			FlushEvent();
			return;
		}

		mbSocketCanWrite = true;
		DoWrite();
	}

	if (revents & POLLIN) {
		mbSocketCanRead = true;
		DoRead();
	}

	if (revents & POLLHUP) {
		// POLLHUP during connecting means the connection failed
		bool connectFailed = false;
		vdsynchronized(mpSyncContext->mMutex) {
			if (mState == State::Connecting) {
				int err = 0;
				socklen_t errlen = sizeof(err);
				getsockopt(mSocketHandle, SOL_SOCKET, SO_ERROR, &err, &errlen);
				QueuePosixError_Locked(err ? err : ECONNREFUSED);
				connectFailed = true;
			} else {
				mbSocketRemoteClosed = true;
				QueueEvent_Locked();
			}
		}

		if (connectFailed) {
			FlushEvent();
			return;
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
	if (!SetNonBlocking(mSocketHandle)) {
		QueuePosixError_Locked();
		return false;
	}

	int nodelay = 1;
	if (setsockopt(mSocketHandle, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof nodelay)) {
		VDDEBUG("Sockets: Unable to disable nagling.\n");
	}

	// make out of band data inline for reliable Telnet -- this avoids the need to try
	// to compensate for differences in TCB Urgent data handling
	int oobinline = 1;
	setsockopt(mSocketHandle, SOL_SOCKET, SO_OOBINLINE, &oobinline, sizeof oobinline);

#ifdef SO_NOSIGPIPE
	// macOS: prevent SIGPIPE on broken pipe (Linux uses MSG_NOSIGNAL per-send)
	int nosigpipe = 1;
	setsockopt(mSocketHandle, SOL_SOCKET, SO_NOSIGPIPE, &nosigpipe, sizeof nosigpipe);
#endif

	return true;
}

void ATNetStreamSocket::UpdateLocalAddress_Locked() {
	if (mSocketHandle != -1) {
		union {
			char buf[256] {};
			sockaddr sa;
		} addr {};
		socklen_t addrLen = sizeof(addr);

		if (0 == getsockname(mSocketHandle, &addr.sa, &addrLen))
			mLocalAddress = ATSocketFromNativeAddress(&addr.sa);
	}
}

void ATNetStreamSocket::DoRead() {
	if (!mbSocketCanRead)
		return;

	VDASSERT(mSocketHandle != -1);

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

		ssize_t r = ::recv(mSocketHandle, &mReadBuffer[mReadTailOffset], toRead, 0);
		if (r < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				mbSocketCanRead = false;
				break;
			}

			QueuePosixError();
			return;
		}

		if (r == 0) {
			mbSocketCanRead = false;
			// soft close occurred
			break;
		}

		wasRead = (uint32)r;
		mReadTailOffset += wasRead;
		if (mReadTailOffset >= rsize)
			mReadTailOffset = 0;
	}
}

void ATNetStreamSocket::DoWrite() {
	if (!mbSocketCanWrite)
		return;

	VDASSERT(mSocketHandle != -1);

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
					::shutdown(mSocketHandle, SHUT_WR);

				if (mState == State::Closing)
					DoClose_Locked();

				break;
			}
		}

		const uint32 toWrite = std::min<uint32>(avail, wsize - mWriteHeadOffset);
		VDASSERT(toWrite > 0);

		ssize_t r = ::send(mSocketHandle, &mWriteBuffer[mWriteHeadOffset], toWrite, MSG_NOSIGNAL);
		if (r < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				mbSocketCanWrite = false;
				break;
			}

			QueuePosixError();
			return;
		}

		if (r == 0) {
			mbSocketCanWrite = false;
			break;
		}

		wasWritten = (uint32)r;
		mWriteHeadOffset += wasWritten;
		if (mWriteHeadOffset >= wsize)
			mWriteHeadOffset = 0;
	}
}

void ATNetStreamSocket::DoClose_Locked() {
	VDASSERT(mState == State::Closing || mState == State::Close);

	if (mSocketHandle != -1) {
		::close(mSocketHandle);
		mSocketHandle = -1;
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
	int pendingSocket;

	vdsynchronized(mpSyncContext->mMutex) {
		pendingSocket = std::exchange(mPendingSocket, -1);
	}

	if (pendingSocket != -1) {
		if (mpSyncContext->mpWorker)
			acceptedSocket = mpSyncContext->mpWorker->CreateStreamSocket(mPendingAddress, pendingSocket);
		else
			::close(pendingSocket);
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
	if (mPendingSocket != -1) {
		::close(mPendingSocket);
		mPendingSocket = -1;
	}

	ATNetSocketT::Shutdown();
}

void ATNetListenSocket::Update() {
	vdsynchronized(mpSyncContext->mMutex) {
		if (mState == State::Listen) {
			mState = State::Listening;

			VDASSERT(mSocketHandle == -1);

			if (mBindAddress.mType == ATSocketAddressType::IPv4)
				mSocketHandle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			else if (mBindAddress.mType == ATSocketAddressType::IPv6)
				mSocketHandle = ::socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
			else
				QueueError_Locked(ATSocketError::Unknown);

			if (mSocketHandle == -1) {
				QueuePosixError_Locked();
			} else {
				// Allow address reuse
				int reuseaddr = 1;
				setsockopt(mSocketHandle, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof reuseaddr);

				if (!SetNonBlocking(mSocketHandle)) {
					QueuePosixError_Locked();
				} else {
					ATSocketNativeAddress nativeBindAddress(mBindAddress);
					int v6only = 0;
					if (mbDualStack && 0 != setsockopt(mSocketHandle, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof v6only)) {
						QueuePosixError_Locked();
					} else if (0 != ::bind(mSocketHandle, nativeBindAddress.GetSockAddr(), nativeBindAddress.GetSockAddrLen())) {
						QueuePosixError_Locked();
					} else if (0 != ::listen(mSocketHandle, SOMAXCONN)) {
						QueuePosixError_Locked();
					} else {
						TryAccept_Locked();
					}
				}
			}
		} else if (mState == State::Close || mState == State::Closing) {
			if (mSocketHandle != -1) {
				::close(mSocketHandle);
				mSocketHandle = -1;
			}

			QueueEvent_Locked();
			mState = State::Closed;

			if (mpSyncContext->mpWorker)
				mpSyncContext->mpWorker->RequestSocketUpdate_Locked(*this);
		}
	}

	FlushEvent();
}

void ATNetListenSocket::HandlePollEvents(short revents) {
	if (mSocketHandle == -1)
		return;

	if (revents & POLLERR) {
		int err = 0;
		socklen_t errlen = sizeof(err);
		getsockopt(mSocketHandle, SOL_SOCKET, SO_ERROR, &err, &errlen);
		QueuePosixError(err ? err : EIO);
		FlushEvent();
		return;
	}

	if (revents & POLLIN) {
		vdsynchronized(mpSyncContext->mMutex) {
			TryAccept_Locked();
		}
	}

	FlushEvent();
}

ATSocketStatus ATNetListenSocket::GetSocketStatus_Locked() const {
	ATSocketStatus status {};

	status.mbClosed = mState == State::Closed;
	status.mbCanAccept = mPendingSocket != -1;
	status.mError = mError;

	return status;
}

void ATNetListenSocket::TryAccept_Locked() {
	if (mPendingSocket != -1)
		return;

	union {
		char buf[256] {};
		sockaddr sa;
	} addr {};
	socklen_t addrLen = sizeof(addr);

	int newSocket = ::accept(mSocketHandle, &addr.sa, &addrLen);
	if (newSocket == -1) {
		if (errno != EAGAIN && errno != EWOULDBLOCK) {
			QueuePosixError_Locked();
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
				break;
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

			VDASSERT(mSocketHandle == -1);

			if (mBindAddress.mType == ATSocketAddressType::IPv4)
				mSocketHandle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
			else if (mBindAddress.mType == ATSocketAddressType::IPv6)
				mSocketHandle = ::socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
			else
				QueueError_Locked(ATSocketError::Unknown);

			if (mSocketHandle != -1) {
				if (!SetNonBlocking(mSocketHandle)) {
					QueuePosixError_Locked();
				} else {
					if (mbDualStack) {
						int v6only = 0;
						if (0 != setsockopt(mSocketHandle, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof v6only))
							QueuePosixError_Locked();
					}

					if (mBindAddress.IsNonZero()) {
						ATSocketNativeAddress nativeBindAddress(mBindAddress);

						if (0 != ::bind(mSocketHandle, nativeBindAddress.GetSockAddr(), nativeBindAddress.GetSockAddrLen()))
							QueuePosixError_Locked();
					}

					if (mError == ATSocketError::None) {
						doRead = true;
						doWrite = true;
					}
				}
			}
		} else if (mState == State::Connected || mState == State::Closing) {
			VDASSERT(mSocketHandle != -1);

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

void ATNetDatagramSocket::HandlePollEvents(short revents) {
	if (mSocketHandle == -1)
		return;

	if (revents & POLLERR) {
		int err = 0;
		socklen_t errlen = sizeof(err);
		getsockopt(mSocketHandle, SOL_SOCKET, SO_ERROR, &err, &errlen);
		QueuePosixError(err ? err : EIO);
		FlushEvent();
		return;
	}

	if (revents & POLLIN) {
		ATNETSOCKET_TRACE("datagram socket read event\n");

		mbSocketCanRead = true;
		DoRead();
	}

	if (revents & POLLOUT) {
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

	VDASSERT(mSocketHandle != -1);

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

		if (avail < kMaxDatagramSize + sizeof(ATSocketAddress)) {
			ATNETSOCKET_TRACE("datagram socket suspending read for space\n");
			break;
		}

		socklen_t fromlen = sizeof(sa);
		memset(&sa.sa, 0, sizeof sa.sa);
		ssize_t r = ::recvfrom(mSocketHandle, buf, sizeof buf, 0, &sa.sa, &fromlen);
		if (r < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				ATNETSOCKET_TRACE("recvfrom would block\n");
				mbSocketCanRead = false;
				break;
			}

			ATNETSOCKET_TRACE("recvfrom error %d\n", errno);

			QueuePosixError();
			return;
		}

		ATSocketAddress fromAddr = ATSocketFromNativeAddress(&sa.sa);
		ATNETSOCKET_TRACE("recvfrom got %zd bytes from %s\n", r, fromAddr.ToString().c_str());

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

	VDASSERT(mSocketHandle != -1);

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

		uint16 len16 = 0;
		ATSocketAddress toAddr;
		mWriteHeadOffset = SplitRead(&len16, sizeof len16, mWriteBuffer.data(), mWriteHeadOffset, wsize);
		mWriteHeadOffset = SplitRead(&toAddr, sizeof toAddr, mWriteBuffer.data(), mWriteHeadOffset, wsize);

		VDASSERT(len16 <= kMaxDatagramSize);
		VDASSERT(avail >= sizeof(ATSocketAddress) + sizeof(uint16) + len16);
		mWriteHeadOffset = SplitRead(buf, len16, mWriteBuffer.data(), mWriteHeadOffset, wsize);

		ATSocketNativeAddress natToAddr(toAddr);

		const ssize_t r = ::sendto(mSocketHandle, buf, len16, 0, natToAddr.GetSockAddr(), natToAddr.GetSockAddrLen());
		if (r < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				mbSocketCanWrite = false;
				break;
			}

			QueuePosixError();
			return;
		}

		wasWritten = sizeof(uint16) + sizeof(ATSocketAddress) + len16;
	}
}

void ATNetDatagramSocket::DoClose_Locked() {
	VDASSERT(mState == State::Closing || mState == State::Close);

	if (mSocketHandle != -1) {
		::close(mSocketHandle);
		mSocketHandle = -1;
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
	// Create self-pipe for waking the worker thread
	if (pipe(mWakePipe) < 0)
		return false;

	if (!SetNonBlocking(mWakePipe[0]) || !SetNonBlocking(mWakePipe[1])) {
		::close(mWakePipe[0]);
		::close(mWakePipe[1]);
		mWakePipe[0] = mWakePipe[1] = -1;
		return false;
	}

	if (!ThreadStart()) {
		::close(mWakePipe[0]);
		::close(mWakePipe[1]);
		mWakePipe[0] = mWakePipe[1] = -1;
		Shutdown();
		return false;
	}

	return true;
}

void ATNetSocketWorker::Shutdown() {
	vdsynchronized(mMutex) {
		mbExitRequested = true;
	}

	WakeWorker();

	ThreadWait();

	if (mWakePipe[0] != -1) {
		::close(mWakePipe[0]);
		mWakePipe[0] = -1;
	}
	if (mWakePipe[1] != -1) {
		::close(mWakePipe[1]);
		mWakePipe[1] = -1;
	}
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

vdrefptr<ATNetStreamSocket> ATNetSocketWorker::CreateStreamSocket(const ATSocketAddress& connectedAddress, int socketFd) {
	vdrefptr<ATNetStreamSocket> s(new ATNetStreamSocket(*mpSyncContext, connectedAddress, socketFd));

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

		WakeWorker();
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

void ATNetSocketWorker::WakeWorker() {
	if (mWakePipe[1] != -1) {
		char c = 1;
		// Write is non-blocking; if pipe is full, worker is already waking
		(void)::write(mWakePipe[1], &c, 1);
	}
}

void ATNetSocketWorker::ThreadRun() {
	// pollfd array: index 0 = wake pipe, indices 1..N = socket fds
	struct pollfd pfds[kMaxSockets + 1];
	size_t numActiveSockets = 0;

	for(;;) {
		// Build pollfd array
		pfds[0].fd = mWakePipe[0];
		pfds[0].events = POLLIN;
		pfds[0].revents = 0;

		size_t nfds = 1;
		for (size_t i = 0; i < numActiveSockets; ++i) {
			int fd = mSocketTable[i]->GetSocketFd();
			pfds[nfds].fd = fd;
			pfds[nfds].events = 0;
			if (fd != -1) {
				pfds[nfds].events = POLLIN | POLLOUT;
			}
			pfds[nfds].revents = 0;
			++nfds;
		}

		int ret = ::poll(pfds, nfds, -1);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			break;
		}

		// Check wake pipe
		if (pfds[0].revents & POLLIN) {
			// Drain the pipe
			char buf[64];
			while (::read(mWakePipe[0], buf, sizeof buf) > 0)
				;

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
						++numActiveSockets;
					}

					// Process socket updates
					for (size_t i = 0; i < numActiveSockets; ++i) {
						if (!mSocketsNeedUpdate[i])
							continue;

						mSocketsNeedUpdate[i] = false;

						ATNetSocket *sock = mSocketTable[i];
						if (sock->IsAbandoned()) {
							--mNumSockets;
							--numActiveSockets;

							sock->mSocketIndex = -1;

							// we have to be careful not to do a Release() on a socket in here
							// since it risks a deadlock
							socketsToDestroy.emplace_back(std::move(mSocketTable[i]));
							mSocketTable[i] = nullptr;

							if (i < mNumSockets) {
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
		}

		// Process socket poll events
		for (size_t i = 0; i < numActiveSockets; ++i) {
			if (pfds[i + 1].revents)
				mSocketTable[i]->HandlePollEvents(pfds[i + 1].revents);
		}
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
