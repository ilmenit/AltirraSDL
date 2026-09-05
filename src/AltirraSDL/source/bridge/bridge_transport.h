// AltirraBridge - cross-platform transport layer
//
// Three backends, selected at runtime by the address spec passed to
// Listen():
//   tcp:HOST:PORT          loopback TCP   (all platforms; default)
//   unix:/path             POSIX UDS      (Linux/macOS only)
//   unix-abstract:NAME     abstract UDS   (Linux/Android only)
//
// We deliberately use loopback TCP as the default because it works
// identically on Windows, macOS, Linux, and Android — and over Android
// `adb forward tcp:N tcp:N`, which is the only way to reach a bridge
// running inside an Android app from the host machine.
//
// All sockets are non-blocking. The server uses a single listening
// socket plus zero or one client socket at a time (Phase 1 is single-
// client; multi-client may come later if the use cases warrant it).
//
// Winsock init/teardown lives entirely in this file. Nothing else in
// the bridge knows about WSAStartup. On POSIX it is a no-op.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace ATBridge {

#if defined(_WIN32)
	using SockHandle = uintptr_t;       // SOCKET
	constexpr SockHandle kInvalidSock = (SockHandle)~0u;
#else
	using SockHandle = int;
	constexpr SockHandle kInvalidSock = -1;
#endif

// Result of a non-blocking recv/send.
enum class IoResult {
	Ok,           // bytes >= 0 transferred (0 may mean "would block" for send)
	WouldBlock,   // recv() got 0 with EAGAIN/EWOULDBLOCK — try again later
	PeerClosed,   // recv() got 0, peer cleanly closed
	Error,        // hard error, drop the connection
};

class Transport {
public:
	Transport();
	~Transport();

	// Open a listening socket according to addrSpec. On success, fills
	// boundDescription with a human-readable string like
	// "tcp:127.0.0.1:54321" or "unix:/tmp/altirra-bridge-1234.sock"
	// (used for the log line and the token file).
	bool Listen(const std::string& addrSpec, std::string& boundDescription);

	// Close the listening socket and any active client. Idempotent.
	void Shutdown();

	bool IsListening() const { return mListenFd != kInvalidSock; }
	bool HasClient()   const { return mClientFd != kInvalidSock; }

	// Try to accept a pending connection. Returns true if a new client
	// was accepted (and HasClient() now returns true). Returns false on
	// "no pending connection" or "we already have a client" — neither
	// is an error. Single-client model: only one connection at a time;
	// any second connection attempt is accepted-then-immediately-closed
	// so the client gets a clear EOF instead of hanging.
	bool TryAccept();

	// Read up to len bytes into buf. Returns IoResult and writes the
	// transferred byte count to *outBytes (0 on WouldBlock/PeerClosed/
	// Error).
	IoResult Recv(void* buf, size_t len, size_t* outBytes);

	// Send all len bytes (best-effort partial send is internally
	// retried until either everything is sent or the peer goes away;
	// in practice loopback writes complete in one call). Returns
	// IoResult: WouldBlock means the kernel buffer is full and we
	// should try again later (the unsent tail is queued internally).
	IoResult SendAll(const void* buf, size_t len);

	// Retry the tail SendAll could not fit into the kernel buffer.
	// A reply bigger than the socket buffer (HISTORY at its full
	// count is over a megabyte) needs this: the client is blocked
	// reading the reply, so no further command will arrive to
	// trigger the retry -- the server's poll loop has to.
	bool HasPendingSend() const { return !mPendingSend.empty(); }
	IoResult FlushPending();

	// Drop the active client immediately. Does not affect the
	// listening socket. Used after Shutdown of the bridge or on
	// protocol errors that warrant dropping the connection.
	void DropClient();

	// Path of the on-disk UDS socket, if any. Empty for tcp/abstract.
	// Returned so Shutdown can unlink() the file.
	const std::string& UnixPath() const { return mUnixPath; }

private:
	SockHandle  mListenFd = kInvalidSock;
	SockHandle  mClientFd = kInvalidSock;
	bool        mIsTcp = false;     // true if listening socket is AF_INET (sets TCP_NODELAY on accept)
	bool        mClientHasSpoken = false;  // true once current client has sent at least one byte
	std::string mUnixPath;          // for unix: filesystem socket cleanup
	std::string mPendingSend;       // bytes that didn't fit in the kernel buffer
};

// One-time process-global Winsock init. Idempotent. POSIX no-op.
// Called by Transport::Listen().
void InitNetSubsystem();

}  // namespace ATBridge
