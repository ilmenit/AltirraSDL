//	Altirra SDL3 frontend - POSIX TCP modem driver
//
//	Cross-platform port of src/Altirra/source/modemtcp.cpp. Replaces
//	Winsock2 + WSAEvent with POSIX sockets + poll() + a self-pipe for
//	the command-event channel. The telnet protocol state machine is a
//	verbatim port of the Windows logic to keep BBS compatibility
//	identical across builds.

#include <stdafx.h>
#include <bitset>
#include <vd2/system/thread.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>
#include <vd2/system/atomic.h>
#include <vd2/system/debug.h>

#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <string.h>

#include "modemtcp.h"
#include "rs232.h"

namespace ATNetTelnetOptionsSDL3 {
	enum ATTelnetOption : uint8 {
		TransmitBinary = 0,
		Echo = 1,
		SuppressGoAhead = 3,
		Status = 5,
		TimingMark = 6,
		TerminalType = 24,
		Naws = 31,
		TerminalSpeed = 32,
		ToggleFlowControl = 33,
		LineMode = 34,
		XDisplayLocation = 35,
		Environ = 36,
		Authentication = 37,
		Encrypt = 38,
		NewEnviron = 39
	};
}

namespace {
	const char *GetTelnetOptionName(uint8 c) {
		using namespace ATNetTelnetOptionsSDL3;

		switch(c) {
		case TransmitBinary:	return "TRANSMIT-BINARY";
		case Echo:				return "ECHO";
		case SuppressGoAhead:	return "SUPPRESS-GO-AHEAD";
		case Status:			return "STATUS";
		case TimingMark:		return "TIMING-MARK";
		case TerminalType:		return "TERMINAL-TYPE";
		case Naws:				return "NAWS";
		case TerminalSpeed:		return "TERMINAL-SPEED";
		case ToggleFlowControl:	return "TOGGLE-FLOW-CONTROL";
		case LineMode:			return "LINEMODE";
		case XDisplayLocation:	return "X-DISPLAY-LOCATION";
		case Environ:			return "ENVIRON";
		case Authentication:	return "AUTHENTICATION";
		case Encrypt:			return "ENCRYPT";
		case NewEnviron:		return "NEW-ENVIRON";
		default:				return "?";
		}
	}

	int set_nonblocking(int fd) {
		int flags = fcntl(fd, F_GETFL, 0);
		if (flags < 0) return -1;
		return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	}
}

class ATModemDriverTCP final : public IATModemDriver, public VDThread {
public:
	ATModemDriverTCP();
	~ATModemDriverTCP();

	bool Init(const char *address, const char *service, uint32 port, bool loggingEnabled, IATModemDriverCallback *callback) override;
	void Shutdown() override;

	bool GetLastIncomingAddress(VDStringA& address, uint32& port) override;

	void SetLoggingEnabled(bool enabled) override;
	void SetConfig(const ATRS232Config& config) override;

	uint32 Write(const void *data, uint32 len) override;
	uint32 Write(const void *data, uint32 len, bool escapeChars);

	uint32 Read(void *buf, uint32 len) override;
	bool ReadLogMessages(VDStringA& messages) override;

protected:
	void ThreadRun() override;

	void WorkerShutdown();
	void OnCommandLocked();
	void OnRead(uint32 bytes);
	void OnWrite();
	void OnError(int code);
	void QueueRead();
	void QueueWrite();
	void FlushSpecialReplies();
	void WakeWorker();
	void DrainWakePipe();

	void SendDo(uint8 c);
	void SendDont(uint8 c);
	void SendWill(uint8 c);
	void SendWont(uint8 c);
	void SendCommand(uint8 cmd, uint8 opt);

	void Log(const char *msg);
	void LogF(const char *format, ...);

	IATModemDriverCallback *mpCB = nullptr;
	VDStringA mAddress;
	VDStringA mService;
	uint32 mPort = 0;

	VDStringA mIncomingAddress;
	uint32 mIncomingPort = 0;

	VDSignal mThreadInited;

	int mSocket = -1;
	int mSocket2 = -1;

	// Self-pipe for cross-thread wake-up. Mirrors Win32's mCommandEvent.
	int mWakeFds[2] = { -1, -1 };

	bool mbReadEOF = false;
	bool mbConnected = false;
	bool mbListenIPv6 = true;
	VDStringA mTelnetTermType;

	VDAtomicInt mbTelnetEmulation { 0 };

	VDCriticalSection mMutex;
	uint32 mWriteQueuedBytes = 0;
	bool mbExit = false;

	VDStringA mLogMessages;
	bool mbLoggingEnabled = false;

	uint32 mReadIndex = 0;
	uint32 mReadLevel = 0;

	uint8 mReadBuffer[4096] {};
	uint8 mWriteBuffer[4096] {};

	vdfastvector<uint8> mSpecialReplies;
	uint32 mSpecialReplyIndex = 0;

	VDStringA mWorkerLog;
	bool mbWorkerLoggingEnabled = false;

	enum TelnetState {
		kTS_Disabled,
		kTS_WaitingForIAC,
		kTS_WaitingForCommandByte,
		kTS_WaitingForDoOptionByte,
		kTS_WaitingForDontOptionByte,
		kTS_WaitingForWillOptionByte,
		kTS_WaitingForWontOptionByte
	};

	enum TelnetSubState {
		kTSS_None,
		kTSS_SubOptionCode,
		kTSS_SubData_Discard,
		kTSS_SubData_TerminalType
	};

	TelnetState mTelnetState = kTS_WaitingForIAC;
	TelnetSubState mTelnetSubState = kTSS_None;
	bool mbTelnetListeningMode = false;
	bool mbTelnetWaitingForEchoResponse = false;
	bool mbTelnetWaitingForSGAResponse = false;

	bool mbTelnetLFConversion = false;
	bool mbTelnetSawIncomingCR = false;
	bool mbTelnetSawOutgoingCR = false;
	bool mbTelnetSawIncomingATASCII = false;
	bool mbTelnetSentTerminalType = false;
	bool mbTelnetBinaryModeIncoming = false;
	bool mbTelnetBinaryModeOutgoing = false;
	uint32 mTelnetBinaryModeIncomingPending = 0;

	std::bitset<256> mTelnetSentDoDont;
};

IATModemDriver *ATCreateModemDriverTCP() {
	return new ATModemDriverTCP;
}

ATModemDriverTCP::ATModemDriverTCP()
	: VDThread("Altirra TCP modem worker")
{
}

ATModemDriverTCP::~ATModemDriverTCP() {
	Shutdown();
}

bool ATModemDriverTCP::Init(const char *address, const char *service, uint32 port, bool loggingEnabled, IATModemDriverCallback *callback) {
	if (address)
		mAddress = address;
	else
		mAddress.clear();

	if (service)
		mService = service;
	else
		mService.clear();

	mPort = port;

	mIncomingAddress.clear();
	mIncomingPort = 0;

	mpCB = callback;
	mWriteQueuedBytes = 0;
	mReadIndex = 0;
	mReadLevel = 0;

	mbLoggingEnabled = loggingEnabled;
	mbWorkerLoggingEnabled = loggingEnabled;
	mbTelnetListeningMode = mAddress.empty();
	mbTelnetSawIncomingCR = false;
	mbTelnetSawOutgoingCR = false;
	mbTelnetSentTerminalType = false;

	mThreadInited.tryWait(0);

	mbExit = false;
	if (!ThreadStart())
		return false;

	// Wait for the worker to either signal init complete or exit.
	mThreadInited.wait();
	return true;
}

void ATModemDriverTCP::Shutdown() {
	mMutex.Lock();
	mbExit = true;
	mMutex.Unlock();
	WakeWorker();
	ThreadWait();
}

bool ATModemDriverTCP::GetLastIncomingAddress(VDStringA& address, uint32& port) {
	mMutex.Lock();
	address = mIncomingAddress;
	port = mIncomingPort;
	mMutex.Unlock();

	return !address.empty();
}

void ATModemDriverTCP::SetLoggingEnabled(bool enabled) {
	mMutex.Lock();
	mbLoggingEnabled = enabled;
	mMutex.Unlock();
	WakeWorker();
}

void ATModemDriverTCP::SetConfig(const ATRS232Config& config) {
	mbTelnetEmulation = config.mbTelnetEmulation;
	mbTelnetLFConversion = mbTelnetEmulation && config.mbTelnetLFConversion;
	mbListenIPv6 = config.mbListenForIPv6;
	mTelnetTermType = config.mTelnetTermType;

	for (auto& c : mTelnetTermType)
		c = (char)toupper((unsigned char)c);
}

uint32 ATModemDriverTCP::Read(void *buf, uint32 len) {
	if (!len)
		return 0;

	mMutex.Lock();
	uint32 tc = mReadLevel - mReadIndex;

	if (tc > len)
		tc = len;

	memcpy(buf, mReadBuffer + mReadIndex, tc);
	mReadIndex += tc;

	bool needWake = (tc && mReadIndex >= mReadLevel);
	mMutex.Unlock();

	if (needWake)
		WakeWorker();

	return tc;
}

bool ATModemDriverTCP::ReadLogMessages(VDStringA& messages) {
	mMutex.Lock();
	messages = mLogMessages;
	mLogMessages.clear();
	mMutex.Unlock();

	return !messages.empty();
}

uint32 ATModemDriverTCP::Write(const void *data, uint32 len) {
	return Write(data, len, true);
}

uint32 ATModemDriverTCP::Write(const void *data, uint32 len, bool escapeChars) {
	if (!len)
		return 0;

	mMutex.Lock();
	bool wasZero = (mWriteQueuedBytes == 0);

	uint32 tc;
	if (escapeChars) {
		const uint8 *data8 = (const uint8 *)data;

		while (len && mWriteQueuedBytes < sizeof mWriteBuffer) {
			uint8 c = *data8++;
			--len;

			if (mbTelnetEmulation) {
				if (mbTelnetLFConversion && !mbTelnetSawIncomingATASCII) {
					if (c == 0x0D)
						mbTelnetSawOutgoingCR = true;
					else if (mbTelnetSawOutgoingCR) {
						mbTelnetSawOutgoingCR = false;

						if (c == 0x0A)
							continue;
					}
				} else if (!mbTelnetBinaryModeOutgoing) {
					if (c == 0x0D) {
						if (mWriteQueuedBytes >= (sizeof mWriteBuffer) - 1)
							break;

						mWriteBuffer[mWriteQueuedBytes++] = c;
						c = 0;
					}
				}

				if (c == 0xFF) {
					if (mWriteQueuedBytes >= (sizeof mWriteBuffer) - 1)
						break;

					mWriteBuffer[mWriteQueuedBytes++] = 0xFF;
				}
			}

			mWriteBuffer[mWriteQueuedBytes++] = c;

			if (mbTelnetEmulation && mbTelnetLFConversion) {
				if (c == 0x0D && !mbTelnetSawIncomingATASCII) {
					if (mWriteQueuedBytes < sizeof mWriteBuffer)
						mWriteBuffer[mWriteQueuedBytes++] = 0x0A;
				}
			}
		}

		tc = (uint32)(data8 - (const uint8 *)data);
	} else {
		tc = sizeof mWriteBuffer - mWriteQueuedBytes;

		if (tc > len)
			tc = len;

		memcpy(mWriteBuffer + mWriteQueuedBytes, data, tc);
		mWriteQueuedBytes += tc;
	}

	mMutex.Unlock();

	if (wasZero)
		WakeWorker();

	return tc;
}

void ATModemDriverTCP::WakeWorker() {
	if (mWakeFds[1] >= 0) {
		uint8 b = 1;
		ssize_t r;
		do {
			r = ::write(mWakeFds[1], &b, 1);
		} while (r < 0 && errno == EINTR);
		// EAGAIN is fine — pipe already has a pending byte; the
		// reader will see we want to wake regardless.
	}
}

void ATModemDriverTCP::DrainWakePipe() {
	if (mWakeFds[0] < 0)
		return;
	uint8 buf[64];
	for (;;) {
		ssize_t r = ::read(mWakeFds[0], buf, sizeof buf);
		if (r <= 0)
			break;
	}
}

void ATModemDriverTCP::ThreadRun() {
	mbConnected = false;
	mbReadEOF = false;
	mTelnetState = kTS_WaitingForIAC;
	mTelnetSubState = kTSS_None;
	mbTelnetBinaryModeIncoming = false;
	mTelnetBinaryModeIncomingPending = 0;
	mbTelnetBinaryModeOutgoing = false;

	mSpecialReplies.clear();
	mSpecialReplyIndex = 0;

	if (::pipe(mWakeFds) != 0 ||
		set_nonblocking(mWakeFds[0]) < 0 ||
		set_nonblocking(mWakeFds[1]) < 0)
	{
		VDDEBUG("ModemTCP: Unable to create wake pipe.\n");
		if (mpCB)
			mpCB->OnEvent(this, kATModemPhase_Init, kATModemEvent_AllocFail);
		mThreadInited.signal();
		WorkerShutdown();
		return;
	}

	mThreadInited.signal();

	if (mAddress.empty()) {
		// Listening mode: bind both IPv4 and (optionally) IPv6 sockets,
		// then accept the first inbound connection.
		mSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (mSocket < 0) {
			VDDEBUG("ModemTCP: Unable to create socket.\n");
			if (mpCB)
				mpCB->OnEvent(this, kATModemPhase_Init, kATModemEvent_AllocFail);
			WorkerShutdown();
			return;
		}

		int reuse = 1;
		setsockopt(mSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
		set_nonblocking(mSocket);

		sockaddr_in sa {};
		sa.sin_port = htons((uint16)mPort);
		sa.sin_addr.s_addr = INADDR_ANY;
		sa.sin_family = AF_INET;
		if (::bind(mSocket, (sockaddr *)&sa, sizeof sa) != 0) {
			int err = errno;
			VDDEBUG("ModemTCP: Unable to bind socket (%s).\n", strerror(err));
			if (mpCB) {
				ATModemEvent ev = kATModemEvent_GenericError;
				if (err == EADDRINUSE)
					ev = kATModemEvent_LineInUse;
				mpCB->OnEvent(this, kATModemPhase_Listen, ev);
			}
			WorkerShutdown();
			return;
		}

		if (::listen(mSocket, 1) != 0) {
			VDDEBUG("ModemTCP: Unable to listen on socket.\n");
			if (mpCB)
				mpCB->OnEvent(this, kATModemPhase_Listen, kATModemEvent_GenericError);
			WorkerShutdown();
			return;
		}

		if (mbListenIPv6) {
			mSocket2 = ::socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
			if (mSocket2 >= 0) {
				int reuse6 = 1;
				setsockopt(mSocket2, SOL_SOCKET, SO_REUSEADDR, &reuse6, sizeof reuse6);
				int v6only = 1;
				setsockopt(mSocket2, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof v6only);
				set_nonblocking(mSocket2);

				sockaddr_in6 sa6 {};
				sa6.sin6_port = htons((uint16)mPort);
				sa6.sin6_family = AF_INET6;
				if (::bind(mSocket2, (sockaddr *)&sa6, sizeof sa6) != 0 ||
					::listen(mSocket2, 1) != 0)
				{
					::close(mSocket2);
					mSocket2 = -1;
				}
			}
		}

		// Wait for inbound connection on either socket.
		for (;;) {
			pollfd pfds[3] {};
			int nfds = 0;
			pfds[nfds].fd = mWakeFds[0]; pfds[nfds++].events = POLLIN;
			pfds[nfds].fd = mSocket;     pfds[nfds++].events = POLLIN;
			if (mSocket2 >= 0) {
				pfds[nfds].fd = mSocket2; pfds[nfds++].events = POLLIN;
			}

			int pr = ::poll(pfds, nfds, -1);
			if (pr < 0 && errno == EINTR)
				continue;
			if (pr < 0) {
				if (mpCB)
					mpCB->OnEvent(this, kATModemPhase_Accept, kATModemEvent_GenericError);
				WorkerShutdown();
				return;
			}

			// Check for command/exit wake-up first.
			if (pfds[0].revents & POLLIN) {
				DrainWakePipe();
				mMutex.Lock();
				OnCommandLocked();
				bool exit = mbExit;
				mMutex.Unlock();
				if (exit) {
					WorkerShutdown();
					return;
				}
			}

			int tryFd = -1;
			if (pfds[1].revents & POLLIN)
				tryFd = mSocket;
			else if (nfds == 3 && (pfds[2].revents & POLLIN))
				tryFd = mSocket2;

			if (tryFd < 0)
				continue;

			union {
				char buf[256];
				sockaddr addr;
				sockaddr_storage stor;
			} sa2 {};
			socklen_t salen = sizeof(sa2);
			int sock2 = ::accept(tryFd, &sa2.addr, &salen);
			if (sock2 < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK)
					continue;
				VDDEBUG("ModemTCP: accept() failed.\n");
				if (mpCB)
					mpCB->OnEvent(this, kATModemPhase_Accept, kATModemEvent_GenericError);
				WorkerShutdown();
				return;
			}

			// Got a connection — close listening sockets, swap in the
			// accepted socket, capture peer address.
			::close(mSocket);
			if (mSocket2 >= 0) {
				::close(mSocket2);
				mSocket2 = -1;
			}

			mSocket = sock2;
			set_nonblocking(mSocket);
			int nodelay = 1;
			setsockopt(mSocket, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof nodelay);

			char namebuf[NI_MAXHOST] = {};
			char servbuf[NI_MAXSERV] = {};
			int revresult = ::getnameinfo(&sa2.addr, salen,
				namebuf, sizeof namebuf, servbuf, sizeof servbuf,
				NI_NUMERICHOST | NI_NUMERICSERV);

			mMutex.Lock();
			if (!revresult) {
				mIncomingAddress = namebuf;
				mIncomingPort = (uint32)atoi(servbuf);
			} else {
				mIncomingAddress.clear();
				mIncomingPort = 0;
			}
			mMutex.Unlock();

			VDDEBUG("ModemTCP: Inbound connection accepted.\n");

			mbConnected = true;
			if (mbTelnetListeningMode && mbTelnetEmulation) {
				// Begin Telnet negotiation, mirroring the Win32 path.
				mSpecialReplies.push_back(0xFF);
				mSpecialReplies.push_back(0xFB);
				mSpecialReplies.push_back(ATNetTelnetOptionsSDL3::Echo);
				mSpecialReplies.push_back(0xFF);
				mSpecialReplies.push_back(0xFD);
				mSpecialReplies.push_back(ATNetTelnetOptionsSDL3::SuppressGoAhead);
				mSpecialReplies.push_back(0xFF);
				mSpecialReplies.push_back(0xFD);
				mSpecialReplies.push_back(ATNetTelnetOptionsSDL3::LineMode);
				mbTelnetWaitingForEchoResponse = true;
				mbTelnetWaitingForSGAResponse = true;

				FlushSpecialReplies();
			}

			if (mpCB)
				mpCB->OnEvent(this, kATModemPhase_Connected, kATModemEvent_Connected);

			break;
		}
	} else {
		// Outbound (dial) mode.
		VDDEBUG("ModemTCP: Looking up %s:%s\n", mAddress.c_str(), mService.c_str());

		addrinfo hint {};
		hint.ai_family = AF_UNSPEC;
		hint.ai_socktype = SOCK_STREAM;

		addrinfo *results = nullptr;
		if (::getaddrinfo(mAddress.c_str(), mService.c_str(), &hint, &results) != 0) {
			VDDEBUG("ModemTCP: Name lookup failed.\n");
			if (mpCB)
				mpCB->OnEvent(this, kATModemPhase_NameLookup, kATModemEvent_NameLookupFailed);
			WorkerShutdown();
			return;
		}

		bool connected = false;
		for (addrinfo *p = results; p; p = p->ai_next) {
			mMutex.Lock();
			bool exit = mbExit;
			mMutex.Unlock();
			if (exit) {
				::freeaddrinfo(results);
				WorkerShutdown();
				return;
			}

			if (p->ai_socktype != SOCK_STREAM)
				continue;
			if (p->ai_family != AF_INET && p->ai_family != AF_INET6)
				continue;

			int s = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
			if (s < 0)
				continue;

			int nodelay = 1;
			setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof nodelay);
			set_nonblocking(s);

			int cr = ::connect(s, p->ai_addr, p->ai_addrlen);
			if (cr == 0) {
				mSocket = s;
				connected = true;
				break;
			}
			if (errno == EINPROGRESS) {
				// Wait for the connect to complete (or timeout via wake).
				pollfd pf { s, POLLOUT, 0 };
				int pr = ::poll(&pf, 1, 30000);
				if (pr > 0 && (pf.revents & POLLOUT)) {
					int soerr = 0;
					socklen_t solen = sizeof soerr;
					if (::getsockopt(s, SOL_SOCKET, SO_ERROR, &soerr, &solen) == 0
						&& soerr == 0)
					{
						mSocket = s;
						connected = true;
						break;
					}
				}
			}

			::close(s);
		}

		::freeaddrinfo(results);

		if (!connected) {
			VDDEBUG("ModemTCP: Unable to connect.\n");
			if (mpCB)
				mpCB->OnEvent(this, kATModemPhase_Connecting, kATModemEvent_ConnectFailed);
			WorkerShutdown();
			return;
		}

		VDDEBUG("ModemTCP: Contacted %s\n", mAddress.c_str());

		mbConnected = true;
		if (mpCB)
			mpCB->OnEvent(this, kATModemPhase_Connected, kATModemEvent_Connected);
	}

	mbTelnetWaitingForEchoResponse = false;
	mbTelnetWaitingForSGAResponse = false;
	mTelnetSentDoDont.reset();
	mbTelnetSawIncomingCR = false;
	mbTelnetSawIncomingATASCII = false;

	QueueRead();
	QueueWrite();

	for (;;) {
		if (!mbConnected && mbReadEOF) {
			mMutex.Lock();
			bool readDone = (mReadIndex >= mReadLevel);
			mMutex.Unlock();
			if (readDone) {
				if (mpCB)
					mpCB->OnEvent(this, kATModemPhase_Connected, kATModemEvent_ConnectionDropped);
				break;
			}
		}

		short events = POLLIN;
		mMutex.Lock();
		if (mWriteQueuedBytes)
			events |= POLLOUT;
		mMutex.Unlock();

		pollfd pfds[2] {};
		pfds[0].fd = mWakeFds[0]; pfds[0].events = POLLIN;
		pfds[1].fd = mSocket;     pfds[1].events = events;

		int pr = ::poll(pfds, 2, -1);
		if (pr < 0) {
			if (errno == EINTR)
				continue;
			break;
		}

		if (pfds[0].revents & POLLIN) {
			DrainWakePipe();
			mMutex.Lock();
			OnCommandLocked();
			bool exit = mbExit;
			bool shouldWrite = mWriteQueuedBytes > 0;
			bool shouldRead = (mReadIndex >= mReadLevel) && !mbReadEOF;
			mMutex.Unlock();

			if (exit) {
				WorkerShutdown();
				return;
			}

			if (shouldWrite)
				QueueWrite();
			if (shouldRead)
				QueueRead();
		}

		if (pfds[1].revents & (POLLERR | POLLHUP)) {
			// Treat as connection close.
			mbConnected = false;
			mbReadEOF = true;
			if (mpCB)
				mpCB->OnEvent(this, kATModemPhase_Connected, kATModemEvent_ConnectionClosing);
			continue;
		}

		if (pfds[1].revents & POLLIN)
			QueueRead();

		if (pfds[1].revents & POLLOUT)
			QueueWrite();
	}

	WorkerShutdown();
}

void ATModemDriverTCP::WorkerShutdown() {
	if (mSocket2 >= 0) {
		::shutdown(mSocket2, SHUT_WR);
		::close(mSocket2);
		mSocket2 = -1;
	}

	if (mSocket >= 0) {
		::shutdown(mSocket, SHUT_WR);
		::close(mSocket);
		mSocket = -1;
	}

	for (int i = 0; i < 2; ++i) {
		if (mWakeFds[i] >= 0) {
			::close(mWakeFds[i]);
			mWakeFds[i] = -1;
		}
	}
}

void ATModemDriverTCP::OnCommandLocked() {
	mbWorkerLoggingEnabled = mbLoggingEnabled;
}

void ATModemDriverTCP::OnRead(uint32 bytes) {
	if (!bytes) {
		mbReadEOF = true;
		return;
	}

	uint8 *dst = mReadBuffer;
	TelnetState state = mTelnetState;
	TelnetSubState substate = mTelnetSubState;

	if (!mbTelnetEmulation) {
		state = kTS_WaitingForIAC;
	} else {
		for (uint32 i = 0; i < bytes; ++i) {
			uint8 c = mReadBuffer[i];

			switch (state) {
			case kTS_WaitingForIAC:
				if (c == 0xFF) {
					state = kTS_WaitingForCommandByte;
					continue;
				}
				break;

			case kTS_WaitingForCommandByte:
				switch (c) {
				case 0xF0:
					substate = kTSS_None;
					state = kTS_WaitingForIAC;
					continue;
				case 0xFA:
					substate = kTSS_SubOptionCode;
					state = kTS_WaitingForIAC;
					continue;
				case 0xFB:
					state = kTS_WaitingForWillOptionByte;
					continue;
				case 0xFC:
					state = kTS_WaitingForWontOptionByte;
					continue;
				case 0xFD:
					state = kTS_WaitingForDoOptionByte;
					continue;
				case 0xFE:
					state = kTS_WaitingForDontOptionByte;
					continue;
				case 0xFF:
					state = kTS_WaitingForIAC;
					break;
				default:
					state = kTS_WaitingForIAC;
					continue;
				}
				break;

			case kTS_WaitingForDoOptionByte:
				LogF("Received DO %u (%s)\n", c, GetTelnetOptionName(c));
				switch (c) {
				case ATNetTelnetOptionsSDL3::TransmitBinary:
					if (mbTelnetLFConversion) {
						SendWont(ATNetTelnetOptionsSDL3::TransmitBinary);
					} else {
						SendWill(ATNetTelnetOptionsSDL3::TransmitBinary);
						if (!mbTelnetBinaryModeOutgoing) {
							mbTelnetBinaryModeOutgoing = true;
							mbTelnetSawOutgoingCR = false;
							if (!mbTelnetBinaryModeIncoming) {
								SendDo(ATNetTelnetOptionsSDL3::TransmitBinary);
								++mTelnetBinaryModeIncomingPending;
							}
						}
					}
					break;
				case ATNetTelnetOptionsSDL3::Echo:
					if (mbTelnetWaitingForEchoResponse) {
						mbTelnetWaitingForEchoResponse = false;
						break;
					}
					if (mbTelnetListeningMode)
						SendWill(ATNetTelnetOptionsSDL3::Echo);
					else
						SendWont(ATNetTelnetOptionsSDL3::Echo);
					break;
				case ATNetTelnetOptionsSDL3::SuppressGoAhead:
					SendWill(ATNetTelnetOptionsSDL3::SuppressGoAhead);
					break;
				case ATNetTelnetOptionsSDL3::TerminalType:
					if (mbTelnetListeningMode || mTelnetTermType.empty())
						SendWont(ATNetTelnetOptionsSDL3::TerminalType);
					else
						SendWill(ATNetTelnetOptionsSDL3::TerminalType);
					break;
				default:
					SendWont(c);
					break;
				}
				state = kTS_WaitingForIAC;
				continue;

			case kTS_WaitingForDontOptionByte:
				LogF("Received DONT %u (%s)\n", c, GetTelnetOptionName(c));
				switch (c) {
				case ATNetTelnetOptionsSDL3::TransmitBinary:
					SendWont(ATNetTelnetOptionsSDL3::TransmitBinary);
					mbTelnetBinaryModeOutgoing = false;
					if (mbTelnetBinaryModeIncoming) {
						SendDont(ATNetTelnetOptionsSDL3::TransmitBinary);
						++mTelnetBinaryModeIncomingPending;
					}
					break;
				default:
					SendWont(c);
					break;
				}
				state = kTS_WaitingForIAC;
				continue;

			case kTS_WaitingForWillOptionByte:
				LogF("Received WILL %u (%s)\n", c, GetTelnetOptionName(c));
				switch (c) {
				case ATNetTelnetOptionsSDL3::TransmitBinary:
					if (mTelnetBinaryModeIncomingPending) {
						--mTelnetBinaryModeIncomingPending;
						mbTelnetBinaryModeIncoming = true;
						mbTelnetSawIncomingCR = false;
					}
					break;
				case ATNetTelnetOptionsSDL3::Echo:
					if (mbTelnetListeningMode)
						SendDont(ATNetTelnetOptionsSDL3::Echo);
					break;
				case ATNetTelnetOptionsSDL3::SuppressGoAhead:
					if (mbTelnetWaitingForSGAResponse) {
						mbTelnetWaitingForSGAResponse = false;
						break;
					}
					SendDo(ATNetTelnetOptionsSDL3::SuppressGoAhead);
					break;
				case ATNetTelnetOptionsSDL3::LineMode:
					if (mbTelnetListeningMode) {
						mSpecialReplies.push_back(0xFF);
						mSpecialReplies.push_back(0xFA);
						mSpecialReplies.push_back(ATNetTelnetOptionsSDL3::LineMode);
						mSpecialReplies.push_back(0x01);
						mSpecialReplies.push_back(0x00);
						mSpecialReplies.push_back(0xFF);
						mSpecialReplies.push_back(0xF0);
					}
					SendDont(c);
					break;
				default:
					SendDont(c);
					break;
				}
				state = kTS_WaitingForIAC;
				continue;

			case kTS_WaitingForWontOptionByte:
				LogF("Received WONT %u (%s)\n", c, GetTelnetOptionName(c));
				switch (c) {
				case ATNetTelnetOptionsSDL3::TransmitBinary:
					if (mbTelnetListeningMode) {
						if (mTelnetBinaryModeIncomingPending) {
							--mTelnetBinaryModeIncomingPending;
							if (mbTelnetBinaryModeIncoming)
								mbTelnetBinaryModeIncoming = false;
						}
					}
					break;
				default:
					break;
				}
				state = kTS_WaitingForIAC;
				continue;
			}

			switch (substate) {
			case kTSS_SubOptionCode:
				if (c == 0x18)
					substate = kTSS_SubData_TerminalType;
				else
					substate = kTSS_SubData_Discard;
				break;

			case kTSS_SubData_TerminalType:
				if (!mbTelnetListeningMode && c == 0x01) {
					Log("Received TERMINAL-TYPE SEND\n");
					if (mTelnetTermType.empty())
						mbTelnetSentTerminalType = true;
					const uint8 *s = mbTelnetSentTerminalType
						? (const uint8 *)"UNKNOWN"
						: (const uint8 *)mTelnetTermType.data();
					const size_t slen = mbTelnetSentTerminalType ? 7 : mTelnetTermType.size();
					mbTelnetSentTerminalType = true;
					mSpecialReplies.push_back(0xFF);
					mSpecialReplies.push_back(0xFA);
					mSpecialReplies.push_back(0x18);
					mSpecialReplies.push_back(0x00);
					mSpecialReplies.insert(mSpecialReplies.end(), s, s + slen);
					mSpecialReplies.push_back(0xFF);
					mSpecialReplies.push_back(0xF0);
				}
				substate = kTSS_SubData_Discard;
				break;

			case kTSS_SubData_Discard:
				break;

			case kTSS_None:
				if (mbTelnetLFConversion && !mbTelnetSawIncomingATASCII) {
					if (c == 0x9B)
						mbTelnetSawIncomingATASCII = true;
					else if (c == 0x0D)
						mbTelnetSawIncomingCR = true;
					else if (mbTelnetSawIncomingCR) {
						mbTelnetSawIncomingCR = false;
						if (c == 0x0A || (c == 0x00 && !mbTelnetBinaryModeIncoming))
							continue;
					}
				} else if (!mbTelnetBinaryModeIncoming) {
					if (c == 0x0D)
						mbTelnetSawIncomingCR = true;
					else if (mbTelnetSawIncomingCR) {
						mbTelnetSawIncomingCR = false;
						if (c == 0x00)
							continue;
					}
				}

				*dst++ = c;
				break;
			}
		}

		bytes = (uint32)(dst - mReadBuffer);
	}

	mTelnetState = state;
	mTelnetSubState = substate;

	bool logs = false;

	mMutex.Lock();
	mReadIndex = 0;
	mReadLevel = bytes;

	if (!mWorkerLog.empty()) {
		logs = true;
		mLogMessages.append(mWorkerLog);
		mWorkerLog.clear();
	}
	mMutex.Unlock();

	FlushSpecialReplies();

	if (mpCB && (bytes || logs))
		mpCB->OnReadAvail(this, bytes);
}

void ATModemDriverTCP::OnWrite() {
	FlushSpecialReplies();

	if (mpCB)
		mpCB->OnWriteAvail(this);
}

void ATModemDriverTCP::OnError(int err) {
	if (!err || err == EAGAIN || err == EWOULDBLOCK)
		return;

	if (mpCB) {
		ATModemEvent ev = kATModemEvent_GenericError;
		if (err == ECONNABORTED || err == ECONNRESET || err == EPIPE) {
			ev = kATModemEvent_ConnectionDropped;
			mbConnected = false;
			mbReadEOF = true;
		}
		mpCB->OnEvent(this, kATModemPhase_Connected, ev);
	}
}

void ATModemDriverTCP::QueueRead() {
	for (;;) {
		mMutex.Lock();
		if (mbReadEOF || mReadIndex < mReadLevel) {
			mMutex.Unlock();
			return;
		}
		mReadIndex = 0;
		mReadLevel = 0;
		mMutex.Unlock();

		ssize_t actual = ::recv(mSocket, mReadBuffer, sizeof mReadBuffer, 0);
		if (actual >= 0) {
			OnRead((uint32)actual);
		} else {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return;
			OnError(errno);
			return;
		}
	}
}

void ATModemDriverTCP::QueueWrite() {
	mMutex.Lock();
	for (;;) {
		if (!mbConnected) {
			mWriteQueuedBytes = 0;
			break;
		}

		if (!mWriteQueuedBytes)
			break;

		const uint32 bytesQueued = mWriteQueuedBytes;
		mMutex.Unlock();

		ssize_t actual = ::send(mSocket, mWriteBuffer, bytesQueued, MSG_NOSIGNAL);

		mMutex.Lock();

		if (actual <= 0) {
			if (actual < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK)
					break;
				mMutex.Unlock();
				OnError(errno);
				return;
			}
			break;
		}

		if ((uint32)actual >= mWriteQueuedBytes) {
			mWriteQueuedBytes = 0;
		} else {
			memmove(mWriteBuffer, mWriteBuffer + actual, mWriteQueuedBytes - actual);
			mWriteQueuedBytes -= (uint32)actual;
		}

		mMutex.Unlock();

		OnWrite();

		mMutex.Lock();
	}
	mMutex.Unlock();
}

void ATModemDriverTCP::FlushSpecialReplies() {
	uint32 sn = (uint32)mSpecialReplies.size();
	uint32 si = mSpecialReplyIndex;
	if (si < sn) {
		si += Write(mSpecialReplies.data() + si, sn - si, false);
		if (si >= sn) {
			si = 0;
			mSpecialReplies.clear();
		}
		mSpecialReplyIndex = si;
		QueueWrite();
	}
}

void ATModemDriverTCP::SendDo(uint8 c) {
	if (mTelnetSentDoDont[c])
		return;
	mTelnetSentDoDont[c] = true;
	LogF("Sending  DO %u (%s)\n", c, GetTelnetOptionName(c));
	SendCommand(0xFD, c);
}

void ATModemDriverTCP::SendDont(uint8 c) {
	if (mTelnetSentDoDont[c])
		return;
	mTelnetSentDoDont[c] = true;
	LogF("Sending  DONT %u (%s)\n", c, GetTelnetOptionName(c));
	SendCommand(0xFE, c);
}

void ATModemDriverTCP::SendWill(uint8 c) {
	LogF("Sending  WILL %u (%s)\n", c, GetTelnetOptionName(c));
	SendCommand(0xFB, c);
}

void ATModemDriverTCP::SendWont(uint8 c) {
	LogF("Sending  WONT %u (%s)\n", c, GetTelnetOptionName(c));
	SendCommand(0xFC, c);
}

void ATModemDriverTCP::SendCommand(uint8 cmd, uint8 opt) {
	uint8 c[3] = { 0xFF, cmd, opt };
	mSpecialReplies.insert(mSpecialReplies.end(), c, c + 3);
}

void ATModemDriverTCP::Log(const char *msg) {
	if (mbWorkerLoggingEnabled)
		mWorkerLog.append(msg);
}

void ATModemDriverTCP::LogF(const char *format, ...) {
	if (mbWorkerLoggingEnabled) {
		va_list val;
		va_start(val, format);
		mWorkerLog.append_vsprintf(format, val);
		va_end(val);
	}
}
