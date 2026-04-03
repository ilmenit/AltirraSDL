# Network Socket Emulation

## Current Architecture (Windows)

```
ATNetwork (emulated network stack)
    |  (clean interface, no Win32 types)
    v
ATNetworkSockets
    |
    +-- nativesockets.cpp  (WSAStartup, worker thread creation)
    +-- lookupworker.cpp   (DNS via GetAddrInfoW on background thread)
    +-- socketworker.cpp   (buffered I/O, WSAEventSelect, WaitForMultipleObjects)
    +-- socketutils_win32.cpp (sockaddr conversion)
    +-- worker.cpp         (bridge: HWND message pump + WSAAsyncSelect)
    +-- vxlantunnel.cpp    (VXLAN over IATDatagramSocket)
    |
    v
Winsock2 API
```

The `ATNetwork` library (emulated Atari network stack) is completely
platform-agnostic. The `ATNetworkSockets` library bridges to real OS
sockets. It is the **most deeply Win32-entangled** core module:

- `socketworker.cpp` uses `WSAEventSelect` to link sockets to `WSAEVENT`
  objects, and `WaitForMultipleObjects` in the worker thread
- `worker.cpp` creates a hidden `HWND` and uses `WSAAsyncSelect()` to
  receive socket events as Windows messages
- `lookupworker.cpp` uses `GetAddrInfoW` for DNS resolution
- `socketutils_win32.cpp` converts between `ATSocketAddress` and
  `sockaddr_in`/`sockaddr_in6` using Winsock field names
  (`sin_addr.S_un.S_addr`, `sin6_addr.u.Byte`)

## Cross-Platform Architecture (POSIX Sockets)

```
ATNetwork (emulated network stack, unchanged)
    |
    v
ATNetworkSockets (platform-selected sources)
    |
    +-- nativesockets_sdl3.cpp  (no init needed, worker creation)
    +-- lookupworker_sdl3.cpp   (DNS via getaddrinfo on background thread)
    +-- socketworker_sdl3.cpp   (buffered I/O, poll(), self-pipe wake)
    +-- socketutils_posix.cpp   (sockaddr conversion, POSIX field names)
    +-- worker_sdl3.cpp         (bridge: poll thread replaces HWND)
    +-- vxlantunnel_sdl3.cpp    (VXLAN over IATDatagramSocket, unchanged logic)
    |
    v
POSIX Socket API (socket, connect, recv, send, poll, etc.)
```

### Why POSIX Sockets Instead of SDL3_net

The initial design proposed SDL3_net, but the socket worker's architecture
requires low-level socket access that SDL3_net does not expose:

- **`setsockopt`** for `TCP_NODELAY`, `SO_OOBINLINE`, `SO_LINGER`,
  `IPV6_V6ONLY`, `SO_NOSIGPIPE` (macOS)
- **`shutdown()`** for half-close (`SHUT_WR`, `SHUT_RD`) — required by
  the Telnet-style modem emulation
- **`getsockname()` / `getpeername()`** for local/remote address queries
- **`poll()`** for multiplexed non-blocking I/O on the worker thread
- **`fcntl(O_NONBLOCK)`** for per-socket non-blocking mode
- **Raw fd access** for the self-pipe wake mechanism

SDL3_net wraps sockets at a higher level and does not expose these
primitives. POSIX sockets are the direct equivalent of the Winsock API
used on Windows and provide full parity.

### Key Winsock to POSIX Translations

| Winsock | POSIX |
|---------|-------|
| `SOCKET` | `int` |
| `INVALID_SOCKET` | `-1` |
| `closesocket()` | `close()` |
| `WSAGetLastError()` | `errno` |
| `WSAEWOULDBLOCK` | `EAGAIN` / `EWOULDBLOCK` |
| `WSAEWOULDBLOCK` (connect) | `EINPROGRESS` |
| `WSAEventSelect` + `WaitForMultipleObjects` | `poll()` + self-pipe |
| `WSAEnumNetworkEvents` | `poll()` revents flags |
| `WSAAsyncSelect` + hidden `HWND` | `poll()` thread |
| `SD_SEND` / `SD_RECEIVE` / `SD_BOTH` | `SHUT_WR` / `SHUT_RD` / `SHUT_RDWR` |
| `BOOL` / `TRUE` | `int` / `1` |
| `sin_addr.S_un.S_addr` | `sin_addr.s_addr` |
| `sin6_addr.u.Byte` | `sin6_addr.s6_addr` |
| `GetAddrInfoW` / `FreeAddrInfoW` | `getaddrinfo` / `freeaddrinfo` (UTF-8) |
| `GetAdaptersInfo` (gateway lookup) | `/proc/net/route` (Linux), `sysctl` (macOS) |
| `MSG_NOSIGNAL` not needed | `MSG_NOSIGNAL` on `send()` (Linux); `SO_NOSIGPIPE` (macOS) |

## Socket Worker (socketworker_sdl3.cpp)

The socket worker manages a pool of up to 63 sockets
(`ATNetStreamSocket`, `ATNetListenSocket`, `ATNetDatagramSocket`) with
buffered ring-buffer I/O and event notification.

### Worker Thread Model

**Windows:** Each socket has a `WSAEVENT` object. `WSAEventSelect` links
the socket fd to the event. The worker thread calls
`VDSignalBase::waitMultiple` (wrapping `WaitForMultipleObjects`) on all
events plus a wake event. When an event fires, `HandleSocketSignal()`
calls `WSAEnumNetworkEvents` to get the specific events.

**POSIX:** The worker thread builds a `pollfd` array each iteration:
index 0 is the wake pipe read end, indices 1..N are socket fds. It
calls `poll()` with infinite timeout. When a socket has events, the
`revents` flags (POLLIN, POLLOUT, POLLERR, POLLHUP) are passed to
`HandlePollEvents()`. New sockets are picked up on the next poll cycle
after `WakeWorker()` writes a byte to the self-pipe.

### Non-Blocking Connect

**Windows:** `connect()` returns `WSAEWOULDBLOCK`; `FD_CONNECT` event
signals completion.

**POSIX:** `connect()` returns `-1` with `errno == EINPROGRESS`;
`POLLOUT` signals write-readiness (check `getsockopt(SO_ERROR)` to
distinguish success from failure). `POLLHUP` during connecting means
the connection was refused.

### Lock Ordering

The socket worker has two mutexes:
- `mMutex` (main state mutex)
- `mCallbackMutex` (callback dispatch mutex)

**Rule:** `mMutex` must never be held when acquiring `mCallbackMutex`.
`FlushEvent()` acquires both in sequence (mMutex briefly, then
mCallbackMutex), so it must always be called outside `vdsynchronized(mMutex)`.

## Bridge Worker (worker_sdl3.cpp)

The bridge worker (`ATNetSockWorker`) bridges between the emulated Atari
network stack and real host sockets. It manages TCP connections, UDP
sockets, and a TCP listening socket for port forwarding.

### Event Dispatch Model

**Windows:** Creates a hidden HWND. `WSAAsyncSelect` posts
`MYWM_TCP_SOCKET`, `MYWM_UDP_SOCKET`, and `MYWM_TCP_LISTEN_SOCKET`
messages to the window. `WndProc` dispatches to bridge handlers.
Everything runs on the main thread (single-threaded, no races).

**POSIX:** A dedicated `VDThread` ("Net bridge worker") polls all native
sockets with `poll()` and dispatches events to bridge handlers. Because
the poll thread and main thread both access shared state, a
`mBridgeMutex` (`VDCriticalSection`, recursive) protects:
- `mTcpConnections` map
- `mUdpSocketMap` / `mUdpSourceMap` maps
- Bridge handler dispatch (OnNativeSocket* callbacks)

The lock is held during fd-list snapshotting and event dispatch, but
released during the `poll()` call itself.

### DNS Gateway Lookup

The bridge worker intercepts DNS packets (port 53) and redirects them
to the host's default gateway:

- **Windows:** `GetAdaptersInfo()` enumerates network adapters and their
  gateway addresses
- **Linux:** Parses `/proc/net/route` for the default route
  (`dest == 0`, `flags & RTF_GATEWAY`)
- **macOS:** Uses `sysctl(CTL_NET, PF_ROUTE, ..., NET_RT_FLAGS, RTF_GATEWAY)`
  to walk the BSD routing table

## Interface Mapping

The existing interfaces between `ATNetwork` and `ATNetworkSockets` are:

- `IATEmuNetSocketListener` -- callback interface for socket events
- `IATStreamSocket` -- abstract stream socket
- `IATListenSocket` -- abstract listen socket
- `IATDatagramSocket` -- abstract datagram socket
- `IATNetLookupResult` -- async DNS lookup result

These are clean interfaces with no Win32 types. The POSIX implementation
provides the same interfaces using POSIX socket calls internally.

## Build-Time Selection

The `ATNetworkSockets/CMakeLists.txt` selects sources by platform:

```cmake
if(WIN32)
    set(ATNETWORKSOCKETS_SOURCES
        source/nativesockets.cpp
        source/lookupworker.cpp
        source/socketworker.cpp
        source/socketutils_win32.cpp
        source/worker.cpp
        source/vxlantunnel.cpp
        source/stdafx.cpp
    )
else()
    set(ATNETWORKSOCKETS_SOURCES
        source/nativesockets_sdl3.cpp
        source/lookupworker_sdl3.cpp
        source/socketworker_sdl3.cpp
        source/socketutils_posix.cpp
        source/worker_sdl3.cpp
        source/vxlantunnel_sdl3.cpp
        source/stdafx.cpp
    )
endif()
```

The Win32 sources are completely untouched. The `_sdl3.cpp` files are
new parallel implementations. Internal headers (`socketworker_sdl3.h`,
`worker_sdl3.h`, `vxlantunnel_sdl3.h`) replace Win32-specific types
(`SOCKET`, `HWND`, `LRESULT`) with POSIX equivalents.

## New Files

| File | Purpose |
|------|---------|
| `source/nativesockets_sdl3.cpp` | POSIX init, ATNetConnect/Listen/Bind/Lookup |
| `source/lookupworker_sdl3.cpp` | DNS via getaddrinfo (wchar_t → UTF-8) |
| `source/socketworker_sdl3.cpp` | Socket worker: poll(), POSIX sockets, buffered I/O |
| `source/socketutils_posix.cpp` | ATSocketNativeAddress, ATSocketFromNativeAddress |
| `source/worker_sdl3.cpp` | Bridge worker: poll thread, gateway lookup |
| `source/vxlantunnel_sdl3.cpp` | VXLAN tunnel (uses IATDatagramSocket) |
| `h/.../socketworker_sdl3.h` | Socket worker classes (int fd, poll-based) |
| `h/.../worker_sdl3.h` | Bridge worker classes (poll thread, mutex) |
| `h/.../vxlantunnel_sdl3.h` | Clean header (no WM_USER/WndProc) |
| `h/.../socketutils_posix.h` | POSIX sockaddr conversion |

## Dependencies

- POSIX socket API (available on Linux and macOS)
- `ATNetwork` interfaces (clean)
- `IATEmuNetSocketListener`, `IATStreamSocket`, `IATDatagramSocket` (clean)

Does **not** depend on SDL3_net, Winsock2, `windows.h`, or any
Win32/SDL headers.
