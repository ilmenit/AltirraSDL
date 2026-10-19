//	AltirraSDL - Cross-platform IPC for test mode
//
//	Abstracts the IPC transport used by ui_testmode.cpp:
//	  - POSIX:   Unix domain sockets (/tmp/altirra-test-<pid>.sock)
//	  - Windows: Named pipes (\\.\pipe\altirra-test-<pid>)
//
//	Both provide a single-client, stream-oriented, non-blocking channel.

#pragma once

#include <string>

class TestModeIPC {
public:
	TestModeIPC();
	~TestModeIPC();

	// Create the listening endpoint.  Returns the address string
	// (socket path or pipe name) on success, empty string on failure.
	std::string Init();

	// Tear down everything — listener, client, and any OS resources.
	void Shutdown();

	// Non-blocking accept.  Returns true if a new client connected
	// (any previous client is disconnected first).
	bool TryAccept();

	// True if a client is currently connected.
	bool HasClient() const;

	// Disconnect the current client (no-op if none).
	void DisconnectClient();

	// Non-blocking send.  Returns bytes actually sent, 0 if the send
	// would block, or -1 on error (client should be considered dead).
	int Send(const void *data, size_t len);

	// Non-blocking receive.  Returns bytes read, 0 if nothing available,
	// or -1 on error / client disconnect.
	int Recv(void *buf, size_t len);

private:
#ifdef _WIN32
	// Windows named pipe handles
	void *mListenPipe = nullptr;   // HANDLE, INVALID_HANDLE_VALUE when unused
	void *mClientPipe = nullptr;   // same handle after ConnectNamedPipe succeeds
	void *mConnectEvent = nullptr; // OVERLAPPED event for async ConnectNamedPipe
	// OVERLAPPED for the pending ConnectNamedPipe. It must outlive the
	// call that starts the connect: the kernel writes the completion into
	// it when a client connects, so it cannot live on the stack.
	void *mpConnectOverlapped = nullptr;
	bool  mConnectPending = false; // true while ConnectNamedPipe is in progress

	bool StartConnect();           // arm ConnectNamedPipe; false on hard error
	std::string mPipeName;
#else
	// POSIX Unix domain socket fds
	int mListenFd = -1;
	int mClientFd = -1;
	std::string mSockPath;
#endif
};
