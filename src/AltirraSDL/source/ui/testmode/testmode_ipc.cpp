//	AltirraSDL - Cross-platform IPC for test mode
//
//	POSIX:   Unix domain sockets (AF_UNIX, SOCK_STREAM)
//	Windows: Named pipes (\\.\pipe\altirra-test-<pid>)

#include <stdafx.h>
#include "testmode_ipc.h"
#include "logging.h"   // LOG_ERROR / LOG_INFO macros (source/app/logging.h)
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
// =========================================================================
// Windows implementation — named pipes
// =========================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

static constexpr DWORD kPipeBufSize = 65536;

TestModeIPC::TestModeIPC() {
	mListenPipe = INVALID_HANDLE_VALUE;
	mClientPipe = nullptr;
	mConnectEvent = nullptr;
	mConnectPending = false;
}

TestModeIPC::~TestModeIPC() {
	Shutdown();
}

std::string TestModeIPC::Init() {
	mPipeName = "\\\\.\\pipe\\altirra-test-" + std::to_string(GetCurrentProcessId());

	mConnectEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (!mConnectEvent) {
		LOG_ERROR("TestMode", "CreateEvent failed: %lu", GetLastError());
		return {};
	}

	mListenPipe = CreateNamedPipeA(
		mPipeName.c_str(),
		PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
		1,              // max instances
		kPipeBufSize,
		kPipeBufSize,
		0,              // default timeout
		nullptr         // default security
	);

	if (mListenPipe == INVALID_HANDLE_VALUE) {
		LOG_ERROR("TestMode", "CreateNamedPipe(%s) failed: %lu", mPipeName.c_str(), GetLastError());
		CloseHandle(mConnectEvent);
		mConnectEvent = nullptr;
		return {};
	}

	// Start async ConnectNamedPipe
	OVERLAPPED ov = {};
	ov.hEvent = mConnectEvent;
	if (ConnectNamedPipe(mListenPipe, &ov)) {
		// Client already connected (rare but possible)
		mClientPipe = mListenPipe;
		mConnectPending = false;
	} else {
		DWORD err = GetLastError();
		if (err == ERROR_IO_PENDING) {
			mConnectPending = true;
		} else if (err == ERROR_PIPE_CONNECTED) {
			// Client connected between Create and Connect
			mClientPipe = mListenPipe;
			mConnectPending = false;
		} else {
			LOG_ERROR("TestMode", "ConnectNamedPipe failed: %lu", err);
			CloseHandle(mListenPipe);
			mListenPipe = INVALID_HANDLE_VALUE;
			CloseHandle(mConnectEvent);
			mConnectEvent = nullptr;
			return {};
		}
	}

	LOG_INFO("TestMode", "Listening on %s", mPipeName.c_str());
	return mPipeName;
}

void TestModeIPC::Shutdown() {
	if (mListenPipe != INVALID_HANDLE_VALUE) {
		if (mConnectPending) {
			CancelIo(mListenPipe);
			mConnectPending = false;
		}
		DisconnectNamedPipe(mListenPipe);
		CloseHandle(mListenPipe);
		mListenPipe = INVALID_HANDLE_VALUE;
	}
	mClientPipe = nullptr;

	if (mConnectEvent) {
		CloseHandle(mConnectEvent);
		mConnectEvent = nullptr;
	}
	mPipeName.clear();
}

bool TestModeIPC::TryAccept() {
	if (mClientPipe || !mConnectPending)
		return false;

	DWORD result = WaitForSingleObject(mConnectEvent, 0);
	if (result != WAIT_OBJECT_0)
		return false;

	// ConnectNamedPipe completed — client is connected
	mClientPipe = mListenPipe;
	mConnectPending = false;
	LOG_INFO("TestMode", "Client connected");
	return true;
}

bool TestModeIPC::HasClient() const {
	return mClientPipe != nullptr;
}

void TestModeIPC::DisconnectClient() {
	if (!mClientPipe)
		return;

	// Disconnect and re-arm for next client
	DisconnectNamedPipe(mListenPipe);
	mClientPipe = nullptr;

	// Re-start listening
	OVERLAPPED ov = {};
	ov.hEvent = mConnectEvent;
	ResetEvent(mConnectEvent);

	if (ConnectNamedPipe(mListenPipe, &ov)) {
		mClientPipe = mListenPipe;
		mConnectPending = false;
	} else {
		DWORD err = GetLastError();
		if (err == ERROR_IO_PENDING) {
			mConnectPending = true;
		} else if (err == ERROR_PIPE_CONNECTED) {
			mClientPipe = mListenPipe;
			mConnectPending = false;
		} else {
			LOG_ERROR("TestMode", "Re-arm ConnectNamedPipe failed: %lu", err);
		}
	}
}

int TestModeIPC::Send(const void *data, size_t len) {
	if (!mClientPipe)
		return -1;

	DWORD written = 0;
	if (!WriteFile(mClientPipe, data, (DWORD)len, &written, nullptr)) {
		DWORD err = GetLastError();
		if (err == ERROR_NO_DATA || err == ERROR_BROKEN_PIPE) {
			return -1;  // client disconnected
		}
		return -1;
	}
	return (int)written;
}

int TestModeIPC::Recv(void *buf, size_t len) {
	if (!mClientPipe)
		return -1;

	// Check if data is available without blocking
	DWORD avail = 0;
	if (!PeekNamedPipe(mClientPipe, nullptr, 0, nullptr, &avail, nullptr)) {
		return -1;  // pipe broken
	}
	if (avail == 0)
		return 0;  // nothing available

	DWORD bytesRead = 0;
	if (!ReadFile(mClientPipe, buf, (DWORD)len, &bytesRead, nullptr)) {
		DWORD err = GetLastError();
		if (err == ERROR_BROKEN_PIPE || err == ERROR_NO_DATA)
			return -1;
		return -1;
	}
	return (bytesRead == 0) ? -1 : (int)bytesRead;
}

#else
// =========================================================================
// POSIX implementation — Unix domain sockets
// =========================================================================

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "logging.h"

TestModeIPC::TestModeIPC() {
}

TestModeIPC::~TestModeIPC() {
	Shutdown();
}

std::string TestModeIPC::Init() {
	mSockPath = "/tmp/altirra-test-" + std::to_string(getpid()) + ".sock";

	// Remove stale socket file if it exists
	unlink(mSockPath.c_str());

	mListenFd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (mListenFd < 0) {
		LOG_ERROR("TestMode", "socket() failed: %s", strerror(errno));
		return {};
	}

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, mSockPath.c_str(), sizeof(addr.sun_path) - 1);

	if (bind(mListenFd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		LOG_ERROR("TestMode", "bind(%s) failed: %s", mSockPath.c_str(), strerror(errno));
		close(mListenFd);
		mListenFd = -1;
		return {};
	}

	if (listen(mListenFd, 1) < 0) {
		LOG_ERROR("TestMode", "listen() failed: %s", strerror(errno));
		close(mListenFd);
		mListenFd = -1;
		unlink(mSockPath.c_str());
		return {};
	}

	// Non-blocking accept
	fcntl(mListenFd, F_SETFL, O_NONBLOCK);

	LOG_INFO("TestMode", "Listening on %s", mSockPath.c_str());
	return mSockPath;
}

void TestModeIPC::Shutdown() {
	if (mClientFd >= 0) { close(mClientFd); mClientFd = -1; }
	if (mListenFd >= 0) { close(mListenFd); mListenFd = -1; }
	if (!mSockPath.empty()) { unlink(mSockPath.c_str()); mSockPath.clear(); }
}

bool TestModeIPC::TryAccept() {
	if (mClientFd >= 0 || mListenFd < 0)
		return false;

	int fd = accept(mListenFd, nullptr, nullptr);
	if (fd < 0)
		return false;

	fcntl(fd, F_SETFL, O_NONBLOCK);
	mClientFd = fd;
	LOG_INFO("TestMode", "Client connected");
	return true;
}

bool TestModeIPC::HasClient() const {
	return mClientFd >= 0;
}

void TestModeIPC::DisconnectClient() {
	if (mClientFd >= 0) {
		close(mClientFd);
		mClientFd = -1;
	}
}

int TestModeIPC::Send(const void *data, size_t len) {
	if (mClientFd < 0)
		return -1;

#ifdef MSG_NOSIGNAL
	ssize_t sent = send(mClientFd, data, len, MSG_NOSIGNAL);
#else
	ssize_t sent = send(mClientFd, data, len, 0);
#endif

	if (sent < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return 0;  // would block
		return -1;      // real error
	}
	return (int)sent;
}

int TestModeIPC::Recv(void *buf, size_t len) {
	if (mClientFd < 0)
		return -1;

	ssize_t n = recv(mClientFd, buf, len, 0);
	if (n > 0)
		return (int)n;
	if (n == 0)
		return -1;  // client disconnected (EOF)
	if (errno == EAGAIN || errno == EWOULDBLOCK)
		return 0;
	return -1;
}

#endif
