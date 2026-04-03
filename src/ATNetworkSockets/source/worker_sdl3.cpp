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
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#include <net/route.h>
#endif

// macOS does not have MSG_NOSIGNAL; use SO_NOSIGPIPE per-socket instead.
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#include <vd2/system/binary.h>
#include <vd2/system/error.h>
#include <vd2/system/refcount.h>
#include <vd2/system/vdstl.h>
#include <at/atnetwork/socket.h>
#include <at/atnetworksockets/internal/worker_sdl3.h>
#include <at/atnetworksockets/socketutils_posix.h>

namespace {
	bool SetNonBlockingFd(int fd) {
		int flags = fcntl(fd, F_GETFL, 0);
		if (flags < 0)
			return false;
		return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
	}
}

ATNetSockBridgeHandler::ATNetSockBridgeHandler(ATNetSockWorker *parent, int nativeFd, IATStreamSocket *s2, uint32 srcIpAddr, uint16 srcPort, uint32 dstIpAddr, uint16 dstPort)
	: mSrcIpAddr(srcIpAddr)
	, mSrcPort(srcPort)
	, mDstIpAddr(dstIpAddr)
	, mDstPort(dstPort)
	, mpParent(parent)
	, mNativeFd(nativeFd)
	, mpLocalSocket(s2)
	, mbLocalClosed(false)
	, mbNativeConnected(false)
	, mbNativeClosed(false)
	, mLocalReadAvail(0)
	, mLocalWriteAvail(0)
	, mRecvBase(0)
	, mRecvLimit(0)
	, mSendBase(0)
	, mSendLimit(0)
{
	if (!s2)
		mbNativeConnected = true;
}

void ATNetSockBridgeHandler::Shutdown() {
	AddRef();

	if (mNativeFd != -1) {
		mpParent->DeleteConnection(mNativeFd);
		::close(mNativeFd);
		mNativeFd = -1;
	}

	mbNativeConnected = false;

	if (mpLocalSocket) {
		mpLocalSocket->CloseSocket(true);
		mpLocalSocket = nullptr;
	}

	Release();
}

void ATNetSockBridgeHandler::SetLocalSocket(IATStreamSocket *s2) {
	mpLocalSocket = s2;
}

void ATNetSockBridgeHandler::SetSrcAddress(const ATSocketAddress& addr) {
	if (addr.GetType() == ATSocketAddressType::IPv4) {
		mSrcIpAddr = addr.mIPv4Address;
		mSrcPort = addr.mPort;
	}
}

void ATNetSockBridgeHandler::OnNativeSocketConnect() {
	mbNativeConnected = true;
	TryCopyToNative();
	TryCopyFromNative();
}

void ATNetSockBridgeHandler::OnNativeSocketReadReady() {
	TryCopyFromNative();
}

void ATNetSockBridgeHandler::OnNativeSocketWriteReady() {
	TryCopyToNative();
}

void ATNetSockBridgeHandler::OnNativeSocketClose() {
	vdrefptr pinSelf(this);

	mbNativeClosed = true;

	TryCopyFromNative();

	if (mRecvLimit == mRecvBase)
		mpLocalSocket->ShutdownSocket(true, false);

	if (mbLocalClosed) {
		// force a graceful close
		if (mpLocalSocket) {
			mpLocalSocket->CloseSocket(false);
			mpLocalSocket = nullptr;
		}

		Shutdown();
	}
}

void ATNetSockBridgeHandler::OnNativeSocketError() {
	Shutdown();
}

void ATNetSockBridgeHandler::OnSocketOpen() {
}

void ATNetSockBridgeHandler::OnSocketReadReady(uint32 len) {
	mLocalReadAvail = len;

	TryCopyToNative();
}

void ATNetSockBridgeHandler::OnSocketWriteReady(uint32 len) {
	mLocalWriteAvail = len;

	TryCopyFromNative();
}

void ATNetSockBridgeHandler::OnSocketClose() {
	mbLocalClosed = true;

	::shutdown(mNativeFd, SHUT_WR);

	if (mbNativeClosed)
		Shutdown();
}

void ATNetSockBridgeHandler::OnSocketError() {
	Shutdown();
}

void ATNetSockBridgeHandler::TryCopyToNative() {
	if (!mbNativeConnected)
		return;

	for(;;) {
		if (mSendBase == mSendLimit) {
			if (mbLocalClosed)
				break;

			sint32 actual = mpLocalSocket->Recv(mSendBuf, sizeof mSendBuf);

			if (actual <= 0)
				break;

			mSendBase = 0;
			mSendLimit = actual;
		}

		ssize_t actual2 = ::send(mNativeFd, mSendBuf + mSendBase, mSendLimit - mSendBase, MSG_NOSIGNAL);

		if (actual2 == 0)
			break;

		if (actual2 < 0)
			break;

		mSendBase += actual2;
	}
}

void ATNetSockBridgeHandler::TryCopyFromNative() {
	if (!mbNativeConnected || mbNativeClosed)
		return;

	for(;;) {
		if (mRecvBase == mRecvLimit) {
			ssize_t actual = ::recv(mNativeFd, mRecvBuf, sizeof mRecvBuf, 0);

			if (actual == 0) {
				if (mbNativeClosed)
					mpLocalSocket->ShutdownSocket(true, false);

				break;
			}

			if (actual < 0)		// includes EAGAIN/EWOULDBLOCK, which means no data
				break;

			mRecvBase = 0;
			mRecvLimit = actual;
		}

		sint32 actual2 = mpLocalSocket->Send(mRecvBuf + mRecvBase, mRecvLimit - mRecvBase);
		if (actual2 <= 0)
			break;

		mRecvBase += actual2;
	}
}

///////////////////////////////////////////////////////////////////////////

ATNetSockWorker::ATNetSockWorker()
	: VDThread("Net bridge worker")
{
}

ATNetSockWorker::~ATNetSockWorker() {
	Shutdown();
}

bool ATNetSockWorker::Init(IATEmuNetUdpStack *udp, IATEmuNetTcpStack *tcp, bool externalAccess, uint32 forwardingAddr, uint16 forwardingPort) {
	mpUdpStack = udp;
	mpTcpStack = tcp;
	mbAllowExternalAccess = externalAccess;

	// Bind DNS directly on the gateway, as we have to redirect it to the host's gateway.
	udp->Bind(53, this);

	mForwardingAddr = forwardingAddr;
	mForwardingPort = forwardingPort;

	if (mForwardingAddr) {
		// Create the TCP listening socket.
		int s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (s != -1) {
			SetNonBlockingFd(s);

			int reuseaddr = 1;
			setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof reuseaddr);

			sockaddr_in sin = {};
			sin.sin_family = AF_INET;
			sin.sin_port = htons(mForwardingPort);
			sin.sin_addr.s_addr = htonl(INADDR_ANY);

			if (::bind(s, (const sockaddr *)&sin, sizeof sin) == 0) {
				if (::listen(s, SOMAXCONN) == 0) {
					mTcpListeningSocket = s;
					s = -1;
				}
			}

			if (s != -1)
				::close(s);
		}
	}

	// Create self-pipe for waking the poll thread
	if (pipe(mPollWakePipe) < 0)
		return false;
	SetNonBlockingFd(mPollWakePipe[0]);
	SetNonBlockingFd(mPollWakePipe[1]);

	// Start the poll thread that monitors native sockets
	mbPollExitRequested = false;
	if (!ThreadStart()) {
		::close(mPollWakePipe[0]);
		::close(mPollWakePipe[1]);
		mPollWakePipe[0] = mPollWakePipe[1] = -1;
		return false;
	}

	return true;
}

void ATNetSockWorker::Shutdown() {
	// Signal poll thread to exit
	mbPollExitRequested = true;
	WakePollThread();
	ThreadWait();

	ResetAllConnections();

	if (mTcpListeningSocket != -1) {
		::close(mTcpListeningSocket);
		mTcpListeningSocket = -1;
	}

	if (mPollWakePipe[0] != -1) {
		::close(mPollWakePipe[0]);
		mPollWakePipe[0] = -1;
	}
	if (mPollWakePipe[1] != -1) {
		::close(mPollWakePipe[1]);
		mPollWakePipe[1] = -1;
	}

	if (mpUdpStack) {
		mpUdpStack->Unbind(53, this);
		mpUdpStack = NULL;
	}
}

void ATNetSockWorker::ResetAllConnections() {
	vdsynchronized(mBridgeMutex) {
		while(!mTcpConnections.empty()) {
			ATNetSockBridgeHandler *h = mTcpConnections.begin()->second;

			h->Shutdown();
		}

		for(UdpSocketMap::const_iterator it = mUdpSocketMap.begin(), itEnd = mUdpSocketMap.end();
			it != itEnd;
			++it)
		{
			::close(it->first);
		}

		mUdpSocketMap.clear();
		mUdpSourceMap.clear();

		if (mForwardingAddr) {
			// Create and bind a UDP socket, and set up forwarding.
			CreateUdpConnection(mForwardingAddr, mForwardingPort, 0, mForwardingPort, false);
		}
	}
}

bool ATNetSockWorker::GetHostAddressesForLocalAddress(bool tcp, uint32 srcIpAddr, uint16 srcPort, uint32 dstIpAddr, uint16 dstPort, ATSocketAddress& hostAddr, ATSocketAddress& remoteAddr) const {
	int fd = -1;

	vdsynchronized(mBridgeMutex) {
		if (tcp) {
			for(TcpConnections::const_iterator it = mTcpConnections.begin(), itEnd = mTcpConnections.end();
				it != itEnd;
				++it)
			{
				ATNetSockBridgeHandler *h = it->second;

				if (h->GetSrcIpAddr() == srcIpAddr && h->GetSrcPort() == srcPort &&
					h->GetDstIpAddr() == dstIpAddr && h->GetDstPort() == dstPort)
				{
					fd = it->first;
					break;
				}
			}
		} else {
			const UdpConnection conn = { srcIpAddr, dstIpAddr, srcPort, dstPort };

			UdpSourceMap::const_iterator it = mUdpSourceMap.find(conn);

			if (it != mUdpSourceMap.end())
				fd = it->second;
		}
	}

	if (fd != -1) {
		union {
			char buf[256];
			sockaddr sa;
		} sabuf {};
		socklen_t sa_len = sizeof sabuf;

		if (0 == getsockname(fd, &sabuf.sa, &sa_len))
			hostAddr = ATSocketFromNativeAddress(&sabuf.sa);

		memset(&sabuf, 0, sizeof sabuf);
		sa_len = sizeof sabuf;

		if (0 == getpeername(fd, &sabuf.sa, &sa_len))
			remoteAddr = ATSocketFromNativeAddress(&sabuf.sa);

		if (hostAddr.IsValid() || remoteAddr.IsValid())
			return true;
	}

	return false;
}

bool ATNetSockWorker::OnSocketIncomingConnection(uint32 srcIpAddr, uint16 srcPort, uint32 dstIpAddr, uint16 dstPort, IATStreamSocket *socket, IATSocketHandler **handler) {
	// check if we are allowed to do this connection
	uint32 redirectedDstIpAddr = dstIpAddr;
	if (mpUdpStack->GetIpStack()->IsLocalOrBroadcastAddress(dstIpAddr)) {
		redirectedDstIpAddr = VDToBE32(0x7F000001);		// 127.0.0.1
	} else if (!mbAllowExternalAccess)
		return false;

	int s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (s == -1)
		return false;

	SetNonBlockingFd(s);

#ifdef SO_NOSIGPIPE
	int nosigpipe = 1;
	setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &nosigpipe, sizeof nosigpipe);
#endif

	sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(dstPort);
	addr.sin_addr.s_addr = redirectedDstIpAddr;
	if (-1 == ::connect(s, (const sockaddr *)&addr, sizeof addr)) {
		if (errno != EINPROGRESS) {
			::close(s);
			return false;
		}
	}

	vdrefptr<ATNetSockBridgeHandler> h(new_nothrow ATNetSockBridgeHandler(this, s, socket, srcIpAddr, srcPort, dstIpAddr, dstPort));
	if (!h) {
		::close(s);
		return false;
	}

	vdsynchronized(mBridgeMutex) {
		mTcpConnections[s] = h;
		h->AddRef();
	}

	// Wake poll thread to start monitoring the new connection
	WakePollThread();

	*handler = h.release();
	return true;
}

void ATNetSockWorker::OnUdpDatagram(const ATEthernetAddr& srcHwAddr, uint32 srcIpAddr, uint16 srcPort, uint32 dstIpAddr, uint16 dstPort, const void *data, uint32 dataLen) {
	// Check if this is a DNS packet. If so, we intercept these and redirect them to the
	// host gateway.
	uint32 redirectedDstIpAddr = dstIpAddr;
	bool redirected = false;

	if (dstPort == 53) {
		// Yes, it is DNS -- first, check if it is allowed.
		if (!mbAllowExternalAccess)
			return;

		// Look up the default gateway to redirect DNS queries to the host's resolver.
#ifdef __linux__
		// Linux: parse /proc/net/route for the default gateway.
		FILE *fp = fopen("/proc/net/route", "r");
		if (fp) {
			char line[256];
			// skip header
			if (fgets(line, sizeof line, fp)) {
				while (fgets(line, sizeof line, fp)) {
					char iface[32];
					uint32 dest, gateway, flags;
					if (sscanf(line, "%31s %x %x %x", iface, &dest, &gateway, &flags) >= 4) {
						// flags & 2 = RTF_GATEWAY, dest == 0 = default route
						if (dest == 0 && (flags & 2) && gateway != 0) {
							// gateway is already in network byte order from /proc/net/route
							redirectedDstIpAddr = gateway;
							redirected = true;
							break;
						}
					}
				}
			}
			fclose(fp);
		}
#elif defined(__APPLE__)
		// macOS: use sysctl to read the routing table.
		{
			int mib[] = { CTL_NET, PF_ROUTE, 0, AF_INET, NET_RT_FLAGS, RTF_GATEWAY };
			size_t bufLen = 0;
			if (sysctl(mib, 6, nullptr, &bufLen, nullptr, 0) == 0 && bufLen > 0) {
				vdblock<char> rtBuf(bufLen);
				if (sysctl(mib, 6, rtBuf.data(), &bufLen, nullptr, 0) == 0) {
					char *ptr = rtBuf.data();
					char *end = ptr + bufLen;
					while (ptr < end) {
						struct rt_msghdr *rtm = (struct rt_msghdr *)ptr;
						if (rtm->rtm_msglen == 0)
							break;

						struct sockaddr *sa = (struct sockaddr *)(rtm + 1);
						// First sockaddr is DST, second is GATEWAY
						if (sa->sa_family == AF_INET) {
							struct sockaddr_in *dst = (struct sockaddr_in *)sa;
							if (dst->sin_addr.s_addr == 0) {
								// Default route — advance to gateway address
								sa = (struct sockaddr *)((char *)sa + ((sa->sa_len + 3) & ~3));
								if (sa->sa_family == AF_INET) {
									struct sockaddr_in *gw = (struct sockaddr_in *)sa;
									if (gw->sin_addr.s_addr != 0) {
										redirectedDstIpAddr = gw->sin_addr.s_addr;
										redirected = true;
									}
								}
							}
						}

						ptr += rtm->rtm_msglen;
						if (redirected)
							break;
					}
				}
			}
		}
#endif
	} else {
		// if this connection is aimed at the gateway, spoof the dest address to localhost
		if (mpUdpStack->GetIpStack()->IsLocalOrBroadcastAddress(dstIpAddr)) {
			redirectedDstIpAddr = VDToBE32(0x7F000001);		// 127.0.0.1
			redirected = true;
		}
	}

	int sock;
	vdsynchronized(mBridgeMutex) {
		sock = CreateUdpConnection(srcIpAddr, srcPort, dstIpAddr, dstPort, redirected);
	}

	sockaddr_in dstAddr = {0};
	dstAddr.sin_family = AF_INET;
	dstAddr.sin_port = htons(dstPort);
	dstAddr.sin_addr.s_addr = redirectedDstIpAddr;

	::sendto(sock, (const char *)data, dataLen, 0, (const sockaddr *)&dstAddr, sizeof dstAddr);
}

int ATNetSockWorker::CreateUdpConnection(uint32 srcIpAddr, uint16 srcPort, uint32 dstIpAddr, uint16 dstPort, bool redirected) {
	// see if we already have a socket set up for this source address
	UdpConnection conn = {0};
	conn.mSrcIpAddr = srcIpAddr;
	conn.mSrcPort = srcPort;

	if (redirected) {
		// if we're redirecting the connection, we must retain the original destination so
		// we can spoof it on the reply
		conn.mDstIpAddr = dstIpAddr;
		conn.mDstPort = dstPort;
	}

	UdpSourceMap::insert_return_type r = mUdpSourceMap.insert(conn);

	if (r.second) {
		// Nope -- establish a new socket.
		int s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (s == -1) {
			mUdpSourceMap.erase(r.first);
			return s;
		}

		SetNonBlockingFd(s);

		sockaddr_in bindAddr = {0};
		bindAddr.sin_family = AF_INET;
		bindAddr.sin_port = !dstIpAddr && dstPort ? htons(dstPort) : htons(0);
		bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
		::bind(s, (const sockaddr *)&bindAddr, sizeof bindAddr);

		r.first->second = s;

		mUdpSocketMap[s] = conn;

		// Wake poll thread to start monitoring the new UDP socket
		WakePollThread();
	}

	return r.first->second;
}

void ATNetSockWorker::DeleteConnection(int fd) {
	vdsynchronized(mBridgeMutex) {
		TcpConnections::iterator it = mTcpConnections.find(fd);

		if (it != mTcpConnections.end()) {
			ATNetSockBridgeHandler *h = it->second;

			mTcpConnections.erase(it);

			h->Release();
		}
	}
}

void ATNetSockWorker::ProcessUdpDatagram(int fd, uint32 srcIpAddr, uint16 srcPort, uint32 dstIpAddr, uint16 dstPort) {
	char buf[4096];
	sockaddr_in from;
	socklen_t fromlen = sizeof from;

	ssize_t len = ::recvfrom(fd, buf, sizeof buf, 0, (sockaddr *)&from, &fromlen);

	if (len > 0)
		mpUdpStack->SendDatagram(srcIpAddr ? srcIpAddr : from.sin_addr.s_addr, srcPort ? srcPort : ntohs(from.sin_port), dstIpAddr, dstPort, buf, len);
}

void ATNetSockWorker::WakePollThread() {
	if (mPollWakePipe[1] != -1) {
		char c = 1;
		(void)::write(mPollWakePipe[1], &c, 1);
	}
}

void ATNetSockWorker::ThreadRun() {
	// This thread polls all native TCP connection fds, UDP socket fds,
	// and the TCP listening socket for I/O events. It replaces the Win32
	// WSAAsyncSelect + hidden HWND event dispatch mechanism.

	for (;;) {
		if (mbPollExitRequested)
			break;

		// Build pollfd array under lock: [0] = wake pipe, then TCP connections,
		// then UDP sockets, then optionally the listening socket.
		// We snapshot the fd lists, then release the lock before calling poll().
		vdfastvector<struct pollfd> pfds;
		vdfastvector<int> tcpFds;
		vdfastvector<int> udpFds;
		bool hasListenSocket;

		{
			struct pollfd pfd {};
			pfd.fd = mPollWakePipe[0];
			pfd.events = POLLIN;
			pfds.push_back(pfd);
		}

		vdsynchronized(mBridgeMutex) {
			for (auto it = mTcpConnections.begin(); it != mTcpConnections.end(); ++it) {
				struct pollfd pfd {};
				pfd.fd = it->first;
				pfd.events = POLLIN | POLLOUT;
				pfds.push_back(pfd);
				tcpFds.push_back(it->first);
			}

			for (auto it = mUdpSocketMap.begin(); it != mUdpSocketMap.end(); ++it) {
				struct pollfd pfd {};
				pfd.fd = it->first;
				pfd.events = POLLIN;
				pfds.push_back(pfd);
				udpFds.push_back(it->first);
			}
		}

		hasListenSocket = (mTcpListeningSocket != -1);
		if (hasListenSocket) {
			struct pollfd pfd {};
			pfd.fd = mTcpListeningSocket;
			pfd.events = POLLIN;
			pfds.push_back(pfd);
		}

		int ret = ::poll(pfds.data(), pfds.size(), 50);  // 50ms timeout for periodic re-check
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			break;
		}

		if (mbPollExitRequested)
			break;

		// Check wake pipe
		if (pfds[0].revents & POLLIN) {
			char buf[64];
			while (::read(mPollWakePipe[0], buf, sizeof buf) > 0)
				;
		}

		// Process TCP connection events under lock
		vdsynchronized(mBridgeMutex) {
			size_t baseIdx = 1;
			for (size_t i = 0; i < tcpFds.size(); ++i) {
				short revents = pfds[baseIdx + i].revents;
				if (!revents)
					continue;

				auto it = mTcpConnections.find(tcpFds[i]);
				if (it == mTcpConnections.end())
					continue;

				ATNetSockBridgeHandler *h = it->second;

				if (revents & POLLERR) {
					h->OnNativeSocketError();
					continue;
				}

				if (revents & POLLOUT) {
					if (!h->IsNativeConnected()) {
						// Check connect completion
						int err = 0;
						socklen_t errlen = sizeof(err);
						getsockopt(tcpFds[i], SOL_SOCKET, SO_ERROR, &err, &errlen);
						if (err) {
							h->OnNativeSocketError();
							continue;
						}
						h->OnNativeSocketConnect();
					} else {
						h->OnNativeSocketWriteReady();
					}
				}

				if (revents & POLLIN) {
					h->OnNativeSocketReadReady();
				}

				if (revents & POLLHUP) {
					if (!h->IsNativeConnected())
						h->OnNativeSocketError();
					else
						h->OnNativeSocketClose();
				}
			}

			// Process UDP socket events
			baseIdx += tcpFds.size();
			for (size_t i = 0; i < udpFds.size(); ++i) {
				short revents = pfds[baseIdx + i].revents;
				if (!(revents & POLLIN))
					continue;

				auto it = mUdpSocketMap.find(udpFds[i]);
				if (it == mUdpSocketMap.end())
					continue;

				const UdpConnection& conn = it->second;
				// Reverse src/dst for incoming datagrams (same as Win32 version)
				ProcessUdpDatagram(it->first, conn.mDstIpAddr, conn.mDstPort, conn.mSrcIpAddr, conn.mSrcPort);
			}

			// Process TCP listening socket
			if (hasListenSocket) {
				short revents = pfds.back().revents;
				if (revents & POLLIN) {
					sockaddr addr = {};
					socklen_t addrlen = sizeof(addr);

					int newSocket = ::accept(mTcpListeningSocket, &addr, &addrlen);

					if (newSocket != -1) {
						SetNonBlockingFd(newSocket);

						if (addr.sa_family != AF_INET) {
							::close(newSocket);
						} else {
							const sockaddr_in *addr4 = (const sockaddr_in *)&addr;
							vdrefptr<ATNetSockBridgeHandler> h(new_nothrow ATNetSockBridgeHandler(this, newSocket, nullptr, addr4->sin_addr.s_addr, ntohs(addr4->sin_port), mForwardingAddr, mForwardingPort));
							if (!h) {
								::close(newSocket);
							} else {
								vdrefptr<IATStreamSocket> emuSocket;
								if (!mpTcpStack->Connect(mForwardingAddr, mForwardingPort, *h, ~emuSocket)) {
									::close(newSocket);
								} else {
									h->SetLocalSocket(emuSocket);
									h->SetSrcAddress(emuSocket->GetLocalAddress());

									mTcpConnections[newSocket] = h;
									h->AddRef();

									h->OnNativeSocketConnect();
								}
							}
						}
					}
				}
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////

void ATCreateNetSockWorker(IATEmuNetUdpStack *udp, IATEmuNetTcpStack *tcp, bool externalAccess, uint32 forwardingAddr, uint16 forwardingPort, IATNetSockWorker **pp) {
	ATNetSockWorker *p = new ATNetSockWorker;

	if (!p->Init(udp, tcp, externalAccess, forwardingAddr, forwardingPort)) {
		delete p;
		throw MyMemoryError();
	}

	p->AddRef();
	*pp = p;
}
