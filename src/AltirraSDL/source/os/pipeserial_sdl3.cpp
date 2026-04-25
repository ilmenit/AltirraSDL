//	Altirra SDL3 frontend - POSIX named-pipe serial device
//
//	Cross-platform port of src/Altirra/source/pipeserial_win32.cpp.
//	Uses a UNIX-domain SOCK_STREAM listening socket as the POSIX
//	equivalent of a Win32 duplex named pipe (full-duplex, byte-stream,
//	server-side bind + listen + accept).
//
//	Path convention: $XDG_RUNTIME_DIR/altirra-{name}.sock if set,
//	otherwise /tmp/altirra-{name}.sock. The pipe_name property is the
//	user-visible identifier; on Linux/macOS a connecting client uses
//	the same socket path. On Windows-SDL3 the pipeserial_win32.cpp is
//	linked instead so the \\.\pipe\name syntax still works for users
//	migrating from native Windows builds.
//
//	The worker thread mirrors the Win32 state machine verbatim — same
//	buffer sizes, same blocking flags, same sequence: bind/listen on
//	startup, accept the first inbound connection, ferry bytes between
//	the socket and the SIO buffers, drop the connection cleanly and
//	loop back to accept on disconnect.

#include <stdafx.h>
#include <vd2/system/atomic.h>
#include <vd2/system/thread.h>
#include <vd2/system/text.h>
#include <vd2/system/VDString.h>
#include <at/atcore/asyncdispatcher.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/deviceserial.h>
#include <at/atcore/propertyset.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

// macOS lacks MSG_NOSIGNAL on send(); use SO_NOSIGPIPE socket option
// instead. Shim the macro to 0 so the call site stays portable.
#ifndef MSG_NOSIGNAL
	#define MSG_NOSIGNAL 0
#endif

void ATCreateDevicePipeSerial(const ATPropertySet& pset, IATDevice **dev);

extern const ATDeviceDefinition g_ATDeviceDefPipeSerial = {
	"pipeserial", "pipeserial", L"Named pipe serial port", ATCreateDevicePipeSerial
};

namespace {
	int set_nonblocking(int fd) {
		int f = fcntl(fd, F_GETFL, 0);
		if (f < 0) return -1;
		return fcntl(fd, F_SETFL, f | O_NONBLOCK);
	}

	void disable_sigpipe(int fd) {
#ifdef SO_NOSIGPIPE
		int v = 1;
		setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &v, sizeof v);
#else
		(void)fd;
#endif
	}
}

class ATDevicePipeSerial final : public ATDeviceT<IATDeviceSerial>, public VDThread {
public:
	ATDevicePipeSerial();
	~ATDevicePipeSerial();

	void GetDeviceInfo(ATDeviceInfo& info) override;
	void Init() override;
	void Shutdown() override;
	void GetSettingsBlurb(VDStringW& buf) override;
	void GetSettings(ATPropertySet& pset) override;
	bool SetSettings(const ATPropertySet& pset) override;
	void ColdReset() override;
	bool GetErrorStatus(uint32 idx, VDStringW& error) override;

	void SetOnStatusChange(const vdfunction<void(const ATDeviceSerialStatus&)>& fn) override;
	void SetTerminalState(const ATDeviceSerialTerminalState&) override;
	ATDeviceSerialStatus GetStatus() override;
	void SetOnReadReady(vdfunction<void()> fn) override;
	bool Read(uint32& baudRate, uint8& c) override;
	bool Read(uint32 baudRate, uint8& c, bool& framingError) override;
	void Write(uint32 baudRate, uint8 c) override;
	void FlushBuffers() override;

private:
	void ThreadRun() override;

	void OnPipeStatusChanged();
	void UpdatePipe();
	bool InitPipe();
	void ShutdownPipe();

	bool CheckPipeName();
	void SetPipePath();
	void UpdateStatus();
	void WakeWorker();
	void DrainWakePipe();

	vdfunction<void()> mpOnReadReady;
	IATAsyncDispatcher *mpAsyncDispatcher = nullptr;
	uint64 mAsyncCallback = 0;

	bool mbCanRead = true;
	bool mbLastConnected = false;
	uint32 mBaudRate = 1;

	enum class Status : uint8 {
		NotSet,
		Error,
		NotConnected,
		Connected
	};

	Status mStatus = Status::NotSet;
	VDStringW mError;
	VDStringW mPipeName;
	VDStringW mPipePath;	// host-filesystem socket path

	int mListenFd = -1;
	int mDataFd = -1;
	int mWakeFds[2] = { -1, -1 };

	VDRWLock mMutex;
	uint32 mRecvLevel = 0;
	uint32 mRecvReadOffset = 0;
	uint32 mSendLevel = 0;
	uint32 mSendWriteOffset = 0;
	bool mbConnected = false;
	bool mbRecvReadBlocked = false;
	bool mbRecvWriteBlocked = false;
	bool mbSendReadBlocked = false;
	bool mbExitRequested = false;

	static constexpr uint32 kRecvBufferSize = 256;
	static constexpr uint32 kSendBufferSize = 256;

	uint8 mReadBuffer[kRecvBufferSize] {};
	uint8 mWriteBuffer[kSendBufferSize] {};

	static constexpr wchar_t kDefaultPipeName[] = L"AltirraSerial";
};

ATDevicePipeSerial::ATDevicePipeSerial() {
	mPipeName = kDefaultPipeName;
	SetPipePath();
}

ATDevicePipeSerial::~ATDevicePipeSerial() {
	Shutdown();
}

void ATDevicePipeSerial::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefPipeSerial;
}

void ATDevicePipeSerial::Init() {
	mpAsyncDispatcher = GetService<IATAsyncDispatcher>();
	UpdatePipe();
}

void ATDevicePipeSerial::Shutdown() {
	ShutdownPipe();

	if (mpAsyncDispatcher) {
		mpAsyncDispatcher->Cancel(&mAsyncCallback);
		mpAsyncDispatcher = nullptr;
	}
}

void ATDevicePipeSerial::GetSettingsBlurb(VDStringW& buf) {
	buf.sprintf(L"listen at %ls at %u baud", mPipePath.c_str(), mBaudRate);
}

void ATDevicePipeSerial::GetSettings(ATPropertySet& pset) {
	pset.Clear();

	if (!mPipeName.empty())
		pset.SetString("pipe_name", mPipeName.c_str());

	pset.SetUint32("baud_rate", mBaudRate);
}

bool ATDevicePipeSerial::SetSettings(const ATPropertySet& pset) {
	mBaudRate = std::clamp<uint32>(pset.GetUint32("baud_rate", 31250), 1, 1000000);

	VDStringW newPipeName(pset.GetString("pipe_name", kDefaultPipeName));
	if (mPipeName != newPipeName) {
		mPipeName = newPipeName;
		SetPipePath();
		UpdatePipe();
	}

	return true;
}

void ATDevicePipeSerial::ColdReset() {
	FlushBuffers();
}

bool ATDevicePipeSerial::GetErrorStatus(uint32 idx, VDStringW& error) {
	if (idx || mStatus == Status::Connected)
		return false;

	switch (mStatus) {
	case Status::Error:        error = mError; break;
	case Status::Connected:    break;
	case Status::NotConnected: error = L"No incoming connection"; break;
	default: break;
	}

	return true;
}

void ATDevicePipeSerial::SetOnStatusChange(const vdfunction<void(const ATDeviceSerialStatus&)>&) {}
void ATDevicePipeSerial::SetTerminalState(const ATDeviceSerialTerminalState&) {}
ATDeviceSerialStatus ATDevicePipeSerial::GetStatus() { return {}; }

void ATDevicePipeSerial::SetOnReadReady(vdfunction<void()> fn) {
	mpOnReadReady = std::move(fn);
}

bool ATDevicePipeSerial::Read(uint32& baudRate, uint8& c) {
	baudRate = 0;
	c = 0;

	if (mListenFd < 0) {
		mbCanRead = false;
		return false;
	}

	bool anyRead = false;
	bool unblockRead = false;

	vdsyncexclusive(mMutex) {
		if (mRecvLevel) {
			c = mReadBuffer[mRecvReadOffset];
			if (++mRecvReadOffset >= kRecvBufferSize)
				mRecvReadOffset = 0;

			if (--mRecvLevel == kRecvBufferSize / 2 && mbRecvWriteBlocked) {
				mbRecvWriteBlocked = false;
				unblockRead = true;
			}

			anyRead = true;
		} else if (!mbRecvReadBlocked) {
			mbRecvReadBlocked = true;
		}
	}

	if (!anyRead) {
		mbCanRead = false;
		return false;
	}

	if (unblockRead)
		WakeWorker();

	baudRate = mBaudRate;
	return true;
}

bool ATDevicePipeSerial::Read(uint32 baudRate, uint8& c, bool& framingError) {
	framingError = false;

	uint32 transmitRate;
	if (!Read(transmitRate, c))
		return false;

	if (abs((int)baudRate - (int)transmitRate) * 20 > (int)transmitRate) {
		c = 'U';
		framingError = true;
	}

	return true;
}

void ATDevicePipeSerial::Write(uint32 baudRate, uint8 c) {
	if (mListenFd < 0)
		return;

	if (baudRate && abs((int)mBaudRate - (int)baudRate) * 20 > (int)mBaudRate)
		return;

	bool unblock = false;

	vdsyncexclusive(mMutex) {
		if (mbConnected && mSendLevel < kSendBufferSize) {
			mWriteBuffer[mSendWriteOffset] = c;

			if (++mSendWriteOffset >= kSendBufferSize)
				mSendWriteOffset = 0;

			if (mSendLevel == 0 && mbSendReadBlocked) {
				mbSendReadBlocked = false;
				unblock = true;
			}

			++mSendLevel;
		}
	}

	if (unblock)
		WakeWorker();
}

void ATDevicePipeSerial::FlushBuffers() {
	uint32 baudRate = 0;
	uint8 c = 0;

	for (uint32 i = 0; i < kRecvBufferSize; ++i) {
		if (!Read(baudRate, c))
			break;
	}
}

void ATDevicePipeSerial::WakeWorker() {
	if (mWakeFds[1] >= 0) {
		uint8 b = 1;
		ssize_t r;
		do { r = ::write(mWakeFds[1], &b, 1); }
		while (r < 0 && errno == EINTR);
	}
}

void ATDevicePipeSerial::DrainWakePipe() {
	uint8 buf[64];
	for (;;) {
		ssize_t r = ::read(mWakeFds[0], buf, sizeof buf);
		if (r <= 0)
			break;
	}
}

void ATDevicePipeSerial::ThreadRun() {
	for (;;) {
		// Wait for a client connection (or exit).
		bool connected = false;

		while (!connected) {
			pollfd pfds[2] {};
			pfds[0].fd = mWakeFds[0]; pfds[0].events = POLLIN;
			pfds[1].fd = mListenFd;   pfds[1].events = POLLIN;

			int pr = ::poll(pfds, 2, -1);
			if (pr < 0) {
				if (errno == EINTR) continue;
				return;
			}

			if (pfds[0].revents & POLLIN) {
				DrainWakePipe();
				bool exit = false;
				vdsyncexclusive(mMutex) {
					exit = mbExitRequested;
				}
				if (exit) return;
			}

			if (pfds[1].revents & POLLIN) {
				int fd = ::accept(mListenFd, nullptr, nullptr);
				if (fd < 0) {
					if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
						continue;
					return;
				}
				disable_sigpipe(fd);
				set_nonblocking(fd);
				mDataFd = fd;
				connected = true;
			}
		}

		// Notify the main thread that we're connected and reset buffers.
		vdsyncexclusive(mMutex) {
			mbConnected = true;
			mRecvLevel = 0;
			mRecvReadOffset = 0;
			mSendLevel = 0;
			mSendWriteOffset = 0;
			mbRecvReadBlocked = true;
			mbRecvWriteBlocked = false;
			mbSendReadBlocked = true;
		}

		mpAsyncDispatcher->Queue(&mAsyncCallback, [this] { OnPipeStatusChanged(); });

		// Connected loop: ferry bytes both directions.
		bool stillConnected = true;
		uint32 recvWriteOffset = 0;
		uint32 sendReadOffset = 0;

		while (stillConnected) {
			short events = POLLIN;
			bool wantWrite = false;

			vdsyncexclusive(mMutex) {
				if (mSendLevel > 0)
					wantWrite = true;
				if (mRecvLevel >= kRecvBufferSize) {
					events &= ~POLLIN;	// read-blocked: buffer full
					mbRecvWriteBlocked = true;
				}
			}
			if (wantWrite)
				events |= POLLOUT;

			pollfd pfds[2] {};
			pfds[0].fd = mWakeFds[0]; pfds[0].events = POLLIN;
			pfds[1].fd = mDataFd;     pfds[1].events = events;

			int pr = ::poll(pfds, 2, -1);
			if (pr < 0) {
				if (errno == EINTR) continue;
				stillConnected = false;
				break;
			}

			if (pfds[0].revents & POLLIN) {
				DrainWakePipe();
				bool exit = false;
				vdsyncexclusive(mMutex) {
					exit = mbExitRequested;
				}
				if (exit) {
					::close(mDataFd);
					mDataFd = -1;
					return;
				}
			}

			if (pfds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
				stillConnected = false;
			}

			if (stillConnected && (pfds[1].revents & POLLIN)) {
				// Read into the circular buffer up to the wraparound.
				const uint32 freeSpace = kRecvBufferSize - std::max<uint32>(0u, recvWriteOffset);
				uint32 readLevel = 0;
				vdsyncexclusive(mMutex) { readLevel = mRecvLevel; }

				uint32 capacity = kRecvBufferSize - readLevel;
				if (capacity > freeSpace)
					capacity = freeSpace;

				if (capacity) {
					ssize_t got = ::read(mDataFd, &mReadBuffer[recvWriteOffset], capacity);
					if (got > 0) {
						bool unblock = false;
						vdsyncexclusive(mMutex) {
							if (mbRecvReadBlocked) {
								mbRecvReadBlocked = false;
								unblock = true;
							}
							mRecvLevel += (uint32)got;
						}

						recvWriteOffset += (uint32)got;
						if (recvWriteOffset >= kRecvBufferSize)
							recvWriteOffset = 0;

						if (unblock)
							mpAsyncDispatcher->Queue(&mAsyncCallback,
								[this] { OnPipeStatusChanged(); });
					} else if (got == 0) {
						stillConnected = false;
					} else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
						stillConnected = false;
					}
				}
			}

			if (stillConnected && (pfds[1].revents & POLLOUT)) {
				uint32 sendLevel = 0;
				vdsyncexclusive(mMutex) { sendLevel = mSendLevel; }

				uint32 chunk = std::min<uint32>(sendLevel, kSendBufferSize - sendReadOffset);
				if (chunk) {
					ssize_t wrote = ::send(mDataFd, &mWriteBuffer[sendReadOffset], chunk, MSG_NOSIGNAL);
					if (wrote > 0) {
						vdsyncexclusive(mMutex) {
							mSendLevel -= (uint32)wrote;
						}
						sendReadOffset += (uint32)wrote;
						if (sendReadOffset >= kSendBufferSize)
							sendReadOffset = 0;
					} else if (wrote == 0) {
						stillConnected = false;
					} else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
						stillConnected = false;
					}
				}
			}
		}

		// Disconnected: tear down the data fd, flush buffers, loop back
		// to accept the next client.
		::close(mDataFd);
		mDataFd = -1;

		vdsyncexclusive(mMutex) {
			mbConnected = false;
			mRecvLevel = 0;
			mSendLevel = 0;
			mbRecvReadBlocked = true;
			mbRecvWriteBlocked = false;
			mbSendReadBlocked = true;
		}

		mpAsyncDispatcher->Queue(&mAsyncCallback, [this] { OnPipeStatusChanged(); });
	}
}

void ATDevicePipeSerial::OnPipeStatusChanged() {
	bool canRead {};
	bool connected {};

	vdsyncexclusive(mMutex) {
		canRead = (mRecvLevel > 0);
		connected = mbConnected;
	}

	if (mbLastConnected != connected) {
		mbLastConnected = connected;
		UpdateStatus();
	}

	if (canRead && !mbCanRead) {
		mbCanRead = true;

		if (mpOnReadReady)
			mpOnReadReady();
	}
}

void ATDevicePipeSerial::UpdatePipe() {
	ShutdownPipe();

	if (!mpAsyncDispatcher)
		return;

	if (!InitPipe())
		ShutdownPipe();

	UpdateStatus();
}

bool ATDevicePipeSerial::InitPipe() {
	mError.clear();

	if (!CheckPipeName()) {
		mError = L"Pipe name is reserved and can't be used.";
		return false;
	}

	if (::pipe(mWakeFds) != 0
		|| set_nonblocking(mWakeFds[0]) < 0
		|| set_nonblocking(mWakeFds[1]) < 0)
	{
		mError = L"Unable to allocate resources.";
		return false;
	}

	mListenFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
	if (mListenFd < 0) {
		mError = L"Unable to create socket.";
		return false;
	}

	VDStringA pathU8 = VDTextWToU8(mPipePath);

	// Remove any stale socket file.
	struct stat st {};
	if (::stat(pathU8.c_str(), &st) == 0 && S_ISSOCK(st.st_mode))
		::unlink(pathU8.c_str());

	sockaddr_un addr {};
	addr.sun_family = AF_UNIX;
	if (pathU8.size() + 1 > sizeof(addr.sun_path)) {
		mError = L"Socket path too long.";
		return false;
	}
	memcpy(addr.sun_path, pathU8.c_str(), pathU8.size());

	if (::bind(mListenFd, (sockaddr *)&addr, sizeof addr) != 0) {
		mError = L"Unable to bind socket.";
		return false;
	}

	if (::listen(mListenFd, 1) != 0) {
		mError = L"Unable to listen on socket.";
		return false;
	}

	set_nonblocking(mListenFd);

	return ThreadStart();
}

void ATDevicePipeSerial::ShutdownPipe() {
	if (isThreadActive()) {
		vdsyncexclusive(mMutex) {
			mbExitRequested = true;
		}
		WakeWorker();
		ThreadWait();
	}

	if (mDataFd >= 0) {
		::close(mDataFd);
		mDataFd = -1;
	}

	if (mListenFd >= 0) {
		::close(mListenFd);
		mListenFd = -1;

		// Remove the socket file from the filesystem.
		VDStringA pathU8 = VDTextWToU8(mPipePath);
		::unlink(pathU8.c_str());
	}

	for (int i = 0; i < 2; ++i) {
		if (mWakeFds[i] >= 0) {
			::close(mWakeFds[i]);
			mWakeFds[i] = -1;
		}
	}

	mbExitRequested = false;
}

bool ATDevicePipeSerial::CheckPipeName() {
	if (mPipeName.empty())
		return false;

	bool nonPeriod = false;
	for (wchar_t c : mPipeName) {
		if (c < 32) return false;
		switch (c) {
		case L'<': case L'>': case L':': case L'"':
		case L'/': case L'\\': case L'|':
		case L'?': case L'*':
			return false;
		}
		if (c != L'.')
			nonPeriod = true;
	}

	return nonPeriod;
}

void ATDevicePipeSerial::SetPipePath() {
	const char *runtimeDir = getenv("XDG_RUNTIME_DIR");
	const char *base = (runtimeDir && *runtimeDir) ? runtimeDir : "/tmp";

	VDStringA u8(base);
	if (u8.empty() || u8.back() != '/')
		u8 += '/';
	u8 += "altirra-";
	u8 += VDTextWToU8(mPipeName);
	u8 += ".sock";

	mPipePath = VDTextU8ToW(u8);
}

void ATDevicePipeSerial::UpdateStatus() {
	Status status = Status::NotConnected;

	if (!mError.empty())
		status = Status::Error;
	else if (mListenFd >= 0 && mbLastConnected)
		status = Status::Connected;

	if (mStatus != status) {
		mStatus = status;
		NotifyStatusChanged();
	}
}

///////////////////////////////////////////////////////////////////////////

void ATCreateDevicePipeSerial(const ATPropertySet&, IATDevice **dev) {
	vdrefptr<ATDevicePipeSerial> p(new ATDevicePipeSerial);

	*dev = p;
	(*dev)->AddRef();
}
