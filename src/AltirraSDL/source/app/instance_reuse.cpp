// Altirra - SDL desktop instance hand-off. GPL v2 or later.
// The lock serializes startup and is released by the OS after a crash. Metadata
// lives in a user-private directory; the random capability authenticates TCP
// clients, including on systems where peer credentials are unavailable.
#include "instance_reuse.h"

#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
using Clock = std::chrono::steady_clock;
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket kInvalidSocket = INVALID_SOCKET;
HANDLE g_lock = INVALID_HANDLE_VALUE;
bool g_winsock = false;
void CloseSocket(Socket s) { if (s != kInvalidSocket) closesocket(s); }
bool WouldBlock() {
	return WSAGetLastError() == WSAEWOULDBLOCK || WSAGetLastError() == WSAEINTR;
}
bool SetNonblocking(Socket s) {
	u_long enabled = 1;
	return !ioctlsocket(s, FIONBIO, &enabled);
}
#else
using Socket = int;
constexpr Socket kInvalidSocket = -1;
int g_lock = -1;
void CloseSocket(Socket s) { if (s != kInvalidSocket) close(s); }
bool WouldBlock() {
	return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
}
bool SetNonblocking(Socket s) {
	return fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK) != -1;
}
#endif
Socket g_listener = kInvalidSocket;
std::filesystem::path g_metadata;
std::string g_token;
struct Client {
	Socket socket;
	std::string data;
	Clock::time_point deadline;
};
std::vector<Client> g_clients;
constexpr uint32_t kMaxPayload = 1024 * 1024;
constexpr unsigned kMaxClients = 8;
constexpr auto kTimeout = std::chrono::seconds(15);

int Send(Socket socket, const char* data, size_t size) {
#ifdef MSG_NOSIGNAL
	return (int)send(socket, data, (int)size, MSG_NOSIGNAL);
#else
	return (int)send(socket, data, (int)size, 0);
#endif
}
void ConfigureSocket(Socket socket) {
#ifdef _WIN32
	SetHandleInformation(reinterpret_cast<HANDLE>(socket), HANDLE_FLAG_INHERIT, 0);
#else
	fcntl(socket, F_SETFD, FD_CLOEXEC);
#endif
#ifdef SO_NOSIGPIPE
	int enabled = 1;
	setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof enabled);
#endif
}
void AppendWord(std::string& buffer, uint32_t value) {
	value = htonl(value);
	buffer.append(reinterpret_cast<const char*>(&value), sizeof value);
}
bool ReadWord(const std::string& buffer, size_t& offset, uint32_t& value) {
	if (buffer.size() - offset < sizeof value) return false;
	memcpy(&value, buffer.data() + offset, sizeof value);
	offset += sizeof value;
	value = ntohl(value);
	return true;
}
void AppendString(std::string& buffer, const std::string& value) {
	AppendWord(buffer, (uint32_t)value.size());
	buffer += value;
}
bool ReadString(const std::string& buffer, size_t& offset, std::string& value) {
	uint32_t length;
	if (!ReadWord(buffer, offset, length) || length > buffer.size() - offset)
		return false;
	value.assign(buffer.data() + offset, length);
	offset += length;
	return value.find('\0') == std::string::npos;
}
std::filesystem::path PathFromUTF8(const std::string& value) {
#ifdef __cpp_char8_t
	return std::filesystem::path(std::u8string(
		reinterpret_cast<const char8_t*>(value.data()), value.size()));
#else
	return std::filesystem::u8path(value);
#endif
}

bool Decode(const std::string& buffer, ATInstanceReuseRequest& request) {
	size_t offset = 4;
	std::string token;
	uint32_t count;
	if (!ReadString(buffer, offset, token) || token != g_token ||
		!ReadString(buffer, offset, request.mWorkingDirectory) ||
		!PathFromUTF8(request.mWorkingDirectory).is_absolute() ||
		!ReadWord(buffer, offset, count) || !count || count > 4096)
		return false;
	request.mArguments.clear();
	for (uint32_t i = 0; i < count; ++i) {
		std::string value;
		if (!ReadString(buffer, offset, value)) return false;
		request.mArguments.push_back(std::move(value));
	}
	return offset == buffer.size();
}

bool TryAcquireInstanceLock() {
#ifdef _WIN32
	OVERLAPPED overlap{};
	return g_lock != INVALID_HANDLE_VALUE &&
		LockFileEx(g_lock, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
			0, 1, 0, &overlap);
#else
	return g_lock >= 0 && !flock(g_lock, LOCK_EX | LOCK_NB);
#endif
}

enum class ForwardResult { Forwarded, Failed, OwnerExited };

ForwardResult Forward(int argc, char** argv, std::string& error) {
	// The owner can hold the lock before its metadata has been published.
	const auto deadline = Clock::now() + kTimeout;
	Socket socket = kInvalidSocket;
	std::string token;
	while (Clock::now() < deadline) {
		// The previous owner may exit between the initial lock attempt and
		// hand-off. Elect this launch as the replacement instead of failing.
		if (TryAcquireInstanceLock()) return ForwardResult::OwnerExited;
		std::ifstream metadata(g_metadata);
		unsigned port = 0;
		metadata >> port >> token;
		if (port && port <= 65535 && token.size() == 64) {
			socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (socket == kInvalidSocket) break;
			ConfigureSocket(socket);
			sockaddr_in address{};
			address.sin_family = AF_INET;
			address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			address.sin_port = htons((uint16_t)port);
			if (!connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof address))
				break;
			CloseSocket(socket);
			socket = kInvalidSocket;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
	if (socket == kInvalidSocket || !SetNonblocking(socket)) {
		CloseSocket(socket);
		error = "The running Altirra instance is not responding.";
		return ForwardResult::Failed;
	}
	std::error_code ec;
	auto cwd = std::filesystem::current_path(ec);
	std::string payload;
	AppendString(payload, token);
	const auto cwdUTF8 = cwd.u8string();
	AppendString(payload, std::string(
		reinterpret_cast<const char*>(cwdUTF8.data()), cwdUTF8.size()));
	AppendWord(payload, (uint32_t)argc);
	for (int i = 0; i < argc; ++i) AppendString(payload, argv[i]);
	if (ec || payload.size() > kMaxPayload) {
		CloseSocket(socket);
		error = "Unable to encode the instance command line.";
		return ForwardResult::Failed;
	}
	std::string packet;
	AppendWord(packet, (uint32_t)payload.size());
	packet += payload;
	size_t sent = 0;
	bool acknowledged = false;
	while (Clock::now() < deadline) {
		if (sent < packet.size()) {
			const int n = Send(socket, packet.data() + sent, packet.size() - sent);
			if (n > 0) sent += (size_t)n;
			else if (!n || !WouldBlock()) break;
		} else {
			char response = 0;
			const int n = (int)recv(socket, &response, 1, 0);
			if (n == 1) { acknowledged = response == 1; break; }
			if (!n || !WouldBlock()) break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	CloseSocket(socket);
	if (!acknowledged && TryAcquireInstanceLock())
		return ForwardResult::OwnerExited;
	if (!acknowledged)
		error = "The running Altirra instance did not accept the command line.";
	return acknowledged ? ForwardResult::Forwarded : ForwardResult::Failed;
}
}

ATInstanceReuseResult ATInstanceReuseInit(bool enabled, int& argc,
	char** argv, std::string& error) {
	bool forceOn = false, forceOff = false;
	for (int i = 1; i < argc;) {
		std::string arg(argv[i]);
		for (char& c : arg)
			if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
		const bool on = arg == "--singleinstance" || arg == "--si";
		const bool off = arg == "--nosingleinstance" || arg == "--nosi";
		if (!on && !off) { ++i; continue; }
		forceOn |= on;
		forceOff |= off;
		std::move(argv + i + 1, argv + argc, argv + i);
		argv[--argc] = nullptr;
	}
	enabled = forceOn || (enabled && !forceOff);
	const auto Fail = [&]() {
		ATInstanceReuseShutdown();
		return enabled ? ATInstanceReuseResult::Failed : ATInstanceReuseResult::Continue;
	};
#ifdef _WIN32
	// A user-private capability is shared across Windows integrity levels.
	// Elevated sessions must stay independent to preserve native Altirra's
	// protection against non-elevated launchers controlling an elevated app.
	HANDLE processToken = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &processToken))
		return ATInstanceReuseResult::Continue;
	TOKEN_ELEVATION elevation{};
	DWORD elevationSize = 0;
	const bool elevationKnown = GetTokenInformation(processToken, TokenElevation,
		&elevation, sizeof elevation, &elevationSize) != 0;
	CloseHandle(processToken);
	if (!elevationKnown || elevation.TokenIsElevated)
		return ATInstanceReuseResult::Continue;
#endif
	std::error_code ec;
#ifdef _WIN32
	const DWORD envLength = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
	std::vector<wchar_t> localAppData(envLength);
	if (envLength) GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData.data(), envLength);
	std::filesystem::path directory = envLength ? localAppData.data() : L"";
	if (directory.empty()) { error = "LOCALAPPDATA is unavailable."; return Fail(); }
	directory /= L"AltirraSDL";
	directory /= L"instance";
#else
	const char* home = getenv("HOME");
	if (!home || !*home) { error = "HOME is unavailable."; return Fail(); }
	std::filesystem::path directory = std::filesystem::path(home) / ".altirra-instance";
#endif
	std::filesystem::create_directories(directory, ec);
	if (ec) { error = "Unable to create the instance endpoint directory."; return Fail(); }
#ifndef _WIN32
	struct stat directoryStat{};
	if (lstat(directory.c_str(), &directoryStat) ||
		!S_ISDIR(directoryStat.st_mode) || directoryStat.st_uid != getuid() ||
		chmod(directory.c_str(), 0700)) {
		error = "The instance endpoint directory is not secure.";
		return Fail();
	}
#endif
	g_metadata = directory / "endpoint";
	const auto lockPath = directory / "lock";
#ifdef _WIN32
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa)) { error = "Unable to initialize instance sockets."; return Fail(); }
	g_winsock = true;
	g_lock = CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, nullptr);
	const bool locked = TryAcquireInstanceLock();
#else
	g_lock = open(lockPath.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
	const bool locked = TryAcquireInstanceLock();
#endif
	if (!locked) {
#ifdef _WIN32
		const bool occupied = g_lock != INVALID_HANDLE_VALUE && GetLastError() == ERROR_LOCK_VIOLATION;
#else
		const bool occupied = g_lock >= 0 && (errno == EWOULDBLOCK || errno == EAGAIN);
#endif
		if (enabled && occupied) {
			const auto result = Forward(argc, argv, error);
			if (result != ForwardResult::OwnerExited) {
				ATInstanceReuseShutdown();
				return result == ForwardResult::Forwarded
					? ATInstanceReuseResult::Forwarded : ATInstanceReuseResult::Failed;
			}
			// Forward acquired the released lock; publish a fresh endpoint.
		} else {
			ATInstanceReuseShutdown();
			if (!occupied) error = "Unable to lock the instance endpoint.";
			return !occupied && enabled
				? ATInstanceReuseResult::Failed : ATInstanceReuseResult::Continue;
		}
	}

	// Never reuse stale crash metadata while publishing our new listener.
	std::filesystem::remove(g_metadata, ec);
	g_listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (g_listener != kInvalidSocket) ConfigureSocket(g_listener);
	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
#ifdef _WIN32
	int addressSize = sizeof address;
#else
	socklen_t addressSize = sizeof address;
#endif
	if (g_listener == kInvalidSocket ||
		bind(g_listener, reinterpret_cast<sockaddr*>(&address), sizeof address) ||
		listen(g_listener, kMaxClients) || !SetNonblocking(g_listener) ||
		getsockname(g_listener, reinterpret_cast<sockaddr*>(&address), &addressSize)) {
		error = "Unable to create the instance listener.";
		return Fail();
	}
	try {
		std::random_device random;
		static constexpr char hex[] = "0123456789abcdef";
		g_token.clear();
		for (unsigned i = 0; i < 32; ++i) {
			const unsigned byte = random() & 255;
			g_token += hex[byte >> 4];
			g_token += hex[byte & 15];
		}
	} catch (...) {
		error = "Unable to create a secure instance capability.";
		return Fail();
	}
	std::ofstream metadata(g_metadata, std::ios::trunc);
	metadata << ntohs(address.sin_port) << ' ' << g_token << '\n';
	metadata.close();
	if (!metadata) {
		error = "Unable to publish the instance endpoint.";
		return Fail();
	}
	return ATInstanceReuseResult::Continue;
}

bool ATInstanceReusePoll(ATInstanceReuseRequest& request) {
	if (g_listener == kInvalidSocket) return false;
	if (g_clients.size() < kMaxClients) {
		Socket socket = accept(g_listener, nullptr, nullptr);
		if (socket != kInvalidSocket) {
			ConfigureSocket(socket);
			if (SetNonblocking(socket))
				g_clients.push_back({socket, {}, Clock::now() + kTimeout});
			else CloseSocket(socket);
		}
	}
	for (size_t i = 0; i < g_clients.size();) {
		Client& client = g_clients[i];
		char buffer[8192];
		const int n = (int)recv(client.socket, buffer, sizeof buffer, 0);
		if (n > 0) client.data.append(buffer, (size_t)n);
		bool done = !n || (n < 0 && !WouldBlock()) ||
			Clock::now() > client.deadline || client.data.size() > kMaxPayload + 4;
		bool valid = false;
		if (client.data.size() >= 4) {
			size_t offset = 0;
			uint32_t length;
			ReadWord(client.data, offset, length);
			if (length > kMaxPayload) done = true;
			else if (client.data.size() >= length + 4) {
				valid = client.data.size() == length + 4 && Decode(client.data, request);
				done = true;
				const char acknowledgement = valid ? 1 : 0;
				Send(client.socket, &acknowledgement, 1);
			}
		}
		if (done) {
			CloseSocket(client.socket);
			g_clients.erase(g_clients.begin() + i);
			if (valid) return true;
		} else ++i;
	}
	return false;
}

void ATInstanceReuseShutdown() {
	for (const Client& client : g_clients) CloseSocket(client.socket);
	g_clients.clear();
	if (g_listener != kInvalidSocket) {
		CloseSocket(g_listener);
		g_listener = kInvalidSocket;
		std::error_code ec;
		std::filesystem::remove(g_metadata, ec);
	}
#ifdef _WIN32
	if (g_lock != INVALID_HANDLE_VALUE) { CloseHandle(g_lock); g_lock = INVALID_HANDLE_VALUE; }
	if (g_winsock) { WSACleanup(); g_winsock = false; }
#else
	if (g_lock >= 0) { close(g_lock); g_lock = -1; }
#endif
}
#else
ATInstanceReuseResult ATInstanceReuseInit(bool, int&, char**, std::string&) {
	return ATInstanceReuseResult::Continue;
}
bool ATInstanceReusePoll(ATInstanceReuseRequest&) { return false; }
void ATInstanceReuseShutdown() {}
#endif
