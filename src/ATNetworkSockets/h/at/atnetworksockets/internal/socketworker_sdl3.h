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

#ifndef f_AT_ATNETWORKSOCKETS_INTERNAL_SOCKETWORKER_SDL3_H
#define f_AT_ATNETWORKSOCKETS_INTERNAL_SOCKETWORKER_SDL3_H

#include <bitset>
#include <vd2/system/refcount.h>
#include <vd2/system/thread.h>
#include <at/atnetwork/socket.h>
#include <at/atnetworksockets/internal/socketutils.h>

class ATNetSocketWorker;
class IATAsyncDispatcher;

struct ATNetSocketSyncContext final : public vdrefcount {
	VDCriticalSection mMutex;

	// Mutex used exclusively for callbacks. The main mutex must never be held
	// while locking this.
	VDCriticalSection mCallbackMutex;

	ATNetSocketWorker *mpWorker = nullptr;
};

class ATNetSocket : public vdrefcounted<IATSocket> {
public:
	ATNetSocket(ATNetSocketSyncContext& syncContext);
	~ATNetSocket();

	virtual void Shutdown();

	bool IsAbandoned() const;
	bool IsHardClosing_Locked() const;
	void QueueError(ATSocketError error);
	void QueueError_Locked(ATSocketError error);
	void QueuePosixError();
	void QueuePosixError_Locked();
	void QueuePosixError(int posixError);
	void QueuePosixError_Locked(int posixError);

	int Release() override;

	void SetOnEvent(IATAsyncDispatcher *dispatcher, vdfunction<void(const ATSocketStatus&)> fn, bool callIfReady) override;

	ATSocketStatus GetSocketStatus() const override;
	void CloseSocket(bool force) override;

	void PollSocket() override;

	virtual void Update() {}
	virtual void HandlePollEvents(short revents) {}

	int GetSocketFd() const { return mSocketHandle; }

protected:
	friend class ATNetSocketWorker;

	void QueueEvent();
	void QueueEvent_Locked();
	void FlushEvent();

	void CloseSocket_Locked(bool force);
	virtual ATSocketStatus GetSocketStatus_Locked() const = 0;

	vdrefptr<ATNetSocketSyncContext> mpSyncContext;

	enum class State {
		Created,
		Listen,
		Listening,
		Accept,
		Connect,
		Connecting,
		Connected,
		Close,
		Closing,
		Closed
	} mState = State::Created;

	int mSocketHandle = -1;

	bool mbRequestedShutdownSend = false;
	bool mbRequestedShutdownRecv = false;
	bool mbSocketShutdownSend = false;
	bool mbSocketShutdownRecv = false;
	bool mbSocketCanRead = false;
	bool mbSocketCanWrite = false;

	// These are set when the caller is waiting to be able to Read() or Write().
	// They are set when Read() runs out of data or Write() runs out of space,
	// and reset when the socket transfers data and triggers notifications.
	bool mbWaitingForCanReadSocket = true;
	bool mbWaitingForCanWriteSocket = true;
	bool mbEventPending = false;
	ATSocketError mError = ATSocketError::None;

	int mSocketIndex = -1;

	IATAsyncDispatcher *mpOnEventDispatcher = nullptr;
	uint64 mOnEventToken = 0;
	vdfunction<void(const ATSocketStatus&)> mpOnEventFn;
};

template<typename T>
class ATNetSocketT : public ATNetSocket, public T {
public:
	using ATNetSocket::ATNetSocket;

	int AddRef() override { return ATNetSocket::AddRef(); }
	int Release() override { return ATNetSocket::Release(); }
	void CloseSocket(bool force) override { return ATNetSocket::CloseSocket(force); }
	ATSocketStatus GetSocketStatus() const override { return ATNetSocket::GetSocketStatus(); }

	void SetOnEvent(IATAsyncDispatcher *dispatcher, vdfunction<void(const ATSocketStatus&)> fn, bool callIfReady) override {
		ATNetSocket::SetOnEvent(dispatcher, std::move(fn), callIfReady);
	}

	void PollSocket() override {
		ATNetSocket::PollSocket();
	}
};

class ATNetStreamSocket final : public ATNetSocketT<IATStreamSocket> {
public:
	ATNetStreamSocket(ATNetSocketSyncContext& syncContext);
	ATNetStreamSocket(ATNetSocketSyncContext& syncContext, const ATSocketAddress& connectedAddress, int socketFd);
	~ATNetStreamSocket();

	void Listen(const ATSocketAddress& socketAddress);
	void Connect(const ATSocketAddress& socketAddress, bool dualStack);

	ATSocketAddress GetLocalAddress() const override;
	ATSocketAddress GetRemoteAddress() const override;

	sint32 Recv(void *buf, uint32 len) override;
	sint32 Send(const void *buf, uint32 len) override;

	void ShutdownSocket(bool send, bool receive) override;

	void Update() override;
	void HandlePollEvents(short revents) override;
	ATSocketStatus GetSocketStatus_Locked() const override;

private:
	bool InitSocket_Locked();
	void UpdateLocalAddress_Locked();
	void DoRead();
	void DoWrite();
	void DoClose_Locked();

	bool mbSocketRemoteClosed = false;
	bool mbDualStack = false;

	ATSocketAddress mLocalAddress;
	ATSocketAddress mConnectAddress;

	vdblock<char> mReadBuffer;
	uint32 mReadLevel = 0;
	uint32 mReadHeadOffset = 0;
	uint32 mReadTailOffset = 0;
	uint32 mReadLowThreshold = 0;		// notify socket engine to recv() when above this threshold

	vdblock<char> mWriteBuffer;
	uint32 mWriteLevel = 0;
	uint32 mWriteHeadOffset = 0;
	uint32 mWriteTailOffset = 0;
	uint32 mWriteHighThreshold = 0;		// fire event to request Write() when below this threshold
};

class ATNetListenSocket final : public ATNetSocketT<IATListenSocket> {
public:
	ATNetListenSocket(ATNetSocketSyncContext& syncContext, const ATSocketAddress& bindAddress, bool dualStack);
	~ATNetListenSocket();

	vdrefptr<IATStreamSocket> Accept() override;

	void Shutdown() override;
	void Update() override;
	void HandlePollEvents(short revents) override;
	ATSocketStatus GetSocketStatus_Locked() const override;

private:
	void TryAccept_Locked();

	ATSocketAddress mBindAddress;
	ATSocketAddress mPendingAddress;
	int mPendingSocket = -1;
	bool mbDualStack = false;
};

class ATNetDatagramSocket final : public ATNetSocketT<IATDatagramSocket> {
public:
	ATNetDatagramSocket(ATNetSocketSyncContext& syncContext, const ATSocketAddress& bindAddress, bool dualStack);
	~ATNetDatagramSocket();

	sint32 RecvFrom(ATSocketAddress& address, void *data, uint32 maxlen) override;
	bool SendTo(const ATSocketAddress& address, const void *data, uint32 len) override;

	void Update() override;
	void HandlePollEvents(short revents) override;
	ATSocketStatus GetSocketStatus_Locked() const override;

private:
	static constexpr uint32 kMaxDatagramSize = 1536;

	void DoRead();
	void DoWrite();
	void DoClose_Locked();

	static uint32 SplitRead(void *dst, size_t len, const void *src, uint32 srcOffset, size_t srcLen);
	static uint32 SplitWrite(void *dst, uint32 dstOffset, size_t dstLen, const void *src, size_t len);

	bool mbSocketCanRead = false;
	bool mbSocketCanWrite = true;

	vdfunction<void()> mpOnEventFn;

	ATSocketAddress mBindAddress;
	bool mbDualStack = false;

	vdblock<char> mReadBuffer;
	uint32 mReadLevel = 0;
	uint32 mReadHeadOffset = 0;
	uint32 mReadTailOffset = 0;
	uint32 mReadLowThreshold = 0;		// notify socket engine to recv() when above this threshold

	vdblock<char> mWriteBuffer;
	uint32 mWriteLevel = 0;
	uint32 mWriteHeadOffset = 0;
	uint32 mWriteTailOffset = 0;
	uint32 mWriteHighThreshold = 0;
};

class ATNetSocketWorker final : public VDThread {
public:
	ATNetSocketWorker();
	~ATNetSocketWorker();

	bool Init();
	void Shutdown();

	vdrefptr<ATNetStreamSocket> CreateStreamSocket();
	vdrefptr<ATNetStreamSocket> CreateStreamSocket(const ATSocketAddress& connectedAddress, int socketFd);
	vdrefptr<ATNetListenSocket> CreateListenSocket(const ATSocketAddress& bindAddress, bool dualStack);
	vdrefptr<ATNetDatagramSocket> CreateDatagramSocket(const ATSocketAddress& bindAddress, bool dualStack);

	void RequestSocketUpdate_Locked(const ATNetSocket& socket);

private:
	static constexpr size_t kMaxSockets = 63;

	bool RegisterSocket_Locked(ATNetSocket& s);

	void ThreadRun() override;
	void WakeWorker();

	vdrefptr<ATNetSocketSyncContext> mpSyncContext;

	VDCriticalSection mMutex;
	bool mbExitRequested = false;
	bool mbUpdateSockets = false;

	// Self-pipe for waking the worker thread from poll()
	int mWakePipe[2] = {-1, -1};

	uint8 mNumSockets = 0;
	vdrefptr<ATNetSocket> mSocketTable[kMaxSockets];

	std::bitset<kMaxSockets> mSocketsNeedUpdate;
};

#endif
