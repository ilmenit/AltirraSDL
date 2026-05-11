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

#ifndef f_ATNETWORK_SOCKET_H
#define f_ATNETWORK_SOCKET_H

#include <string.h>		// memset — see docs/merging-with-altirra-mainline.md

#include <vd2/system/function.h>
#include <vd2/system/refcount.h>

class IATAsyncDispatcher;

enum class ATSocketAddressType : uint8 {
	None,
	IPv4,
	IPv6
};

struct ATSocketAddress {
	static ATSocketAddress CreateIPv4(uint16 port = 0) {
		ATSocketAddress addr {};
		addr.mType = ATSocketAddressType::IPv4;
		addr.mIPv4Address = 0;
		addr.mPort = port;

		return addr;
	}

	static ATSocketAddress CreateIPv4(uint32 address, uint16 port) {
		ATSocketAddress addr {};
		addr.mType = ATSocketAddressType::IPv4;
		addr.mIPv4Address = address;
		addr.mPort = port;

		return addr;
	}

	static ATSocketAddress CreateLocalhostIPv4(uint16 port) {
		ATSocketAddress addr {};
		addr.mType = ATSocketAddressType::IPv4;
		addr.mIPv4Address = 0x7F000001;
		addr.mPort = port;

		return addr;
	}

	static ATSocketAddress CreateIPv6(uint16 port = 0) {
		ATSocketAddress addr {};
		addr.mType = ATSocketAddressType::IPv6;
		memset(addr.mIPv6.mAddress, 0, sizeof addr.mIPv6.mAddress);
		addr.mIPv6.mScopeId = 0;
		addr.mPort = port;

		return addr;
	}

	static ATSocketAddress CreateLocalhostIPv6(uint16 port) {
		ATSocketAddress addr {};
		addr.mType = ATSocketAddressType::IPv6;
		memset(addr.mIPv6.mAddress, 0, sizeof addr.mIPv6.mAddress);
		addr.mIPv6.mAddress[15] = 1;
		addr.mIPv6.mScopeId = 0;
		addr.mPort = port;

		return addr;
	}

	static ATSocketAddress CreateIPv4InIPv6(const ATSocketAddress& ipv4);

	ATSocketAddressType GetType() const { return mType; }

	VDStringA ToString(bool includePort = true) const;
	bool IsValid() const { return mType != ATSocketAddressType::None; }
	bool IsNonZero() const;
	bool operator==(const ATSocketAddress& other) const;
	bool operator!=(const ATSocketAddress& other) const { return !operator==(other); }

	ATSocketAddressType mType = ATSocketAddressType::None;
	uint16 mPort = 0;

	union {
		uint32 mIPv4Address;

		struct {
			uint8 mAddress[16];
			uint32 mScopeId;
		} mIPv6;
	};
};

static_assert(std::is_trivially_copyable_v<ATSocketAddress>);

enum class ATSocketError : uint8 {
	None,
	Unknown
};

struct ATSocketStatus {
	// Socket has been closed. This state is permanent.
	bool mbClosed;

	// Socket is currently connecting and has not been established. Data can be
	// queued for sending.
	bool mbConnecting;

	// An incoming connection can be accepted (listen socket only).
	bool mbCanAccept;

	// There is pending data to read.
	bool mbCanRead;

	// There is space in the write buffer.
	bool mbCanWrite;

	// The receiving flow has been closed by the remote host.
	bool mbRemoteClosed;

	// The first error that occurred, if any.
	ATSocketError mError;

	bool operator==(const ATSocketStatus&) const = default;
};

class IATSocket : public IVDRefCount {
public:
	// Set a callback to be invoked on socket state changes.
	//
	// If dispatcher is set, then the callback is invoked on the async dispatcher. Otherwise, it is
	// called from the worker thread. In either case, it is guaranteed that all callbacks are
	// serialized.
	//
	// If callIfReady = true, then the callback is invoked immediately if there is already an
	// event pending. The callback is synchronous even if a dispatcher is supplied.
	//
	// When clearing the callback, it is guaranteed that any callbacks from the worker thread have
	// finished by the time SetOnEvent() returns.
	//
	virtual void SetOnEvent(IATAsyncDispatcher *dispatcher, vdfunction<void(const ATSocketStatus&)> fn, bool callIfReady) = 0;

	// Retrieve the latest snapshot of socket state.
	virtual ATSocketStatus GetSocketStatus() const = 0;

	// Close the socket, but leave the socket object still valid to receive callbacks. If force
	// is false, then a graceful shutdown is done with the send direction closed. If force is
	// true, then the socket is hard closed.
	//
	// Note that if a socket is abandoned by final release, it will be automatically hard closed
	// by the socket system.
	virtual void CloseSocket(bool force) = 0;

	// Re-execute the event handler if there is potentially pending status for
	// it to handle. This is used in cases where the event handler did not handle
	// a received event, such as CanRead/CanWrite.
	//
	// If the event handler is bound using an async dispatcher, it is queued even if the poll
	// call occurs on the dispatcher thread -- it is not called synchronously.
	virtual void PollSocket() = 0;
};

// Interface for stream (TCP) sockets in data mode.
class IATStreamSocket : public IATSocket {
public:
	virtual ATSocketAddress GetLocalAddress() const = 0;
	virtual ATSocketAddress GetRemoteAddress() const = 0;

	// Receive (read) from the socket. Returns the number of bytes read, or -1
	// if there is an error. The number of bytes received may be reduced or 0
	// as Recv() is nonblocking.
	virtual sint32 Recv(void *buf, uint32 len) = 0;

	// Send (write) to the socket. Returns the number of bytes sent, or -1
	// if there is an error. The number of bytes sent may be reduced if there
	// is not enough buffer space, as Send() is nonblocking.
	virtual sint32 Send(const void *buf, uint32 len) = 0;

	// Shut down the sending and/or receiving directions of the connection.
	// Shutting down the send direction informs the other side that the flow
	// has been closed. Shutting down the receive direction forces an RST if
	// more data is sent.
	virtual void ShutdownSocket(bool send, bool receive) = 0;
};

// Interface for stream (TCP) sockets in accept mode.
class IATListenSocket : public IATSocket {
public:
	// Accept an incoming connection, if one is waiting. Returns null if no
	// connection is pending or it could not be accepted.
	virtual vdrefptr<IATStreamSocket> Accept() = 0;
};

// Interface for datagram (UDP) sockets.
class IATDatagramSocket : public IATSocket {
public:
	// Attempt to read a datagram. Returns the remote address and length of the datagram if
	// successful, or -1 if no datagram is available. Any datagrams larger than the provided
	// maxlen are silently discarded.
	virtual sint32 RecvFrom(ATSocketAddress& address, void *data, uint32 maxlen) = 0;

	// Attempt to send a datagram to the specified address. Returns true if successful, false
	// if there was no room in the buffer or there was an error.
	virtual bool SendTo(const ATSocketAddress& address, const void *data, uint32 len) = 0;
};

class IATSocketHandler : public IVDRefCount {
public:
	virtual void OnSocketOpen() = 0;
	virtual void OnSocketReadReady(uint32 len) = 0;
	virtual void OnSocketWriteReady(uint32 len) = 0;
	virtual void OnSocketClose() = 0;
	virtual void OnSocketError() = 0;
};

#endif	// f_ATNETWORK_SOCKET_H
