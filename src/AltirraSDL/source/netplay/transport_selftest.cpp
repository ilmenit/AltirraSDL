// Altirra SDL3 netplay - transport self-test
//
// Standalone test.  Opens two UDP sockets on ephemeral ports and
// verifies Listen / Resolve / SendTo / RecvFrom / Close / Endpoint
// formatting end-to-end.  Not wired into CMake.
//
// Build (shim stdafx.h to an empty file):
//
//   mkdir -p /tmp/netplay_selftest
//   : > /tmp/netplay_selftest/stdafx.h
//   g++ -std=c++17 -I /tmp/netplay_selftest -I src/AltirraSDL/source/netplay   src/AltirraSDL/source/netplay/transport.cpp   src/AltirraSDL/source/netplay/transport_selftest.cpp   -o /tmp/netplay_selftest/transport_run
//   /tmp/netplay_selftest/transport_run
//
// Exit code 0 on success.  Each test prints a one-line verdict.

#include "transport.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

using namespace ATNetplay;

static int fails = 0;

#define CHECK(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			++fails; \
		} \
	} while (0)

static void testListenAndClose() {
	Transport t;
	CHECK(!t.IsOpen());
	CHECK(t.Listen(0));                 // ephemeral port
	CHECK(t.IsOpen());
	CHECK(t.BoundPort() != 0);
	t.Close();
	CHECK(!t.IsOpen());
	CHECK(t.BoundPort() == 0);

	// Re-listen on the same instance works.
	CHECK(t.Listen(0));
	CHECK(t.IsOpen());
}

static void testResolveValid() {
	Endpoint ep;
	CHECK(Transport::Resolve("127.0.0.1:12345", ep));
	CHECK(ep.IsValid());

	char buf[64];
	size_t n = ep.Format(buf, sizeof buf);
	CHECK(n > 0);
	CHECK(std::strcmp(buf, "127.0.0.1:12345") == 0);
}

static void testResolveInvalid() {
	Endpoint ep;
	CHECK(!Transport::Resolve("", ep));
	CHECK(!Transport::Resolve("127.0.0.1", ep));        // no port
	CHECK(!Transport::Resolve(":12345", ep));           // no host
	CHECK(!Transport::Resolve("127.0.0.1:0", ep));      // port must be > 0
	CHECK(!Transport::Resolve("127.0.0.1:65536", ep));  // port overflow
	CHECK(!Transport::Resolve("127.0.0.1:abc", ep));    // non-numeric
}

static void testRecvWouldBlock() {
	Transport t;
	CHECK(t.Listen(0));
	uint8_t buf[64];
	size_t n = 1234;
	Endpoint from;
	// No peer is sending; RecvFrom must report WouldBlock (not hang).
	CHECK(t.RecvFrom(buf, sizeof buf, n, from) == RecvResult::WouldBlock);
	CHECK(n == 0);
}

static void testLoopbackSendRecv() {
	Transport host, joiner;
	CHECK(host.Listen(0));
	CHECK(joiner.Listen(0));

	// Build a "reply to me at 127.0.0.1:<boundPort>" endpoint.
	char addr[64];
	std::snprintf(addr, sizeof addr, "127.0.0.1:%u",
		(unsigned)host.BoundPort());
	Endpoint hostEP;
	CHECK(Transport::Resolve(addr, hostEP));

	std::snprintf(addr, sizeof addr, "127.0.0.1:%u",
		(unsigned)joiner.BoundPort());
	Endpoint joinerEP;
	CHECK(Transport::Resolve(addr, joinerEP));

	// joiner -> host
	const uint8_t payload[] = { 'A', 'N', 'P', 'L', 0xDE, 0xAD, 0xBE, 0xEF };
	CHECK(joiner.SendTo(payload, sizeof payload, hostEP));

	// Drain host socket with a brief spin; UDP over localhost is
	// essentially immediate but we allow a few milliseconds before
	// concluding the send was lost.
	uint8_t rxBuf[64];
	size_t rxLen = 0;
	Endpoint from;
	RecvResult r = RecvResult::WouldBlock;
	for (int i = 0; i < 100 && r == RecvResult::WouldBlock; ++i) {
		r = host.RecvFrom(rxBuf, sizeof rxBuf, rxLen, from);
		if (r == RecvResult::WouldBlock) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
	CHECK(r == RecvResult::Ok);
	CHECK(rxLen == sizeof payload);
	CHECK(std::memcmp(rxBuf, payload, rxLen) == 0);
	CHECK(from.IsValid());

	// The 'from' endpoint must match the joiner's bound port.
	char fromBuf[64];
	from.Format(fromBuf, sizeof fromBuf);
	char expect[64];
	std::snprintf(expect, sizeof expect, "127.0.0.1:%u",
		(unsigned)joiner.BoundPort());
	CHECK(std::strcmp(fromBuf, expect) == 0);

	// Endpoint equality: resolving the same host:port twice must
	// produce byte-identical endpoints.  `addr` still holds the
	// joiner's address (last snprintf above), and joinerEP was
	// resolved from that same string.
	Endpoint copy;
	CHECK(Transport::Resolve(addr, copy));
	CHECK(copy.Equals(joinerEP));

	// A different port must not compare equal.
	Endpoint differentPort;
	char other[64];
	std::snprintf(other, sizeof other, "127.0.0.1:%u",
		(unsigned)(joiner.BoundPort() ^ 1u));
	CHECK(Transport::Resolve(other, differentPort));
	CHECK(!copy.Equals(differentPort));
}

static void testNullPointerGuards() {
	Transport t;
	CHECK(t.Listen(0));

	Endpoint peer;
	CHECK(Transport::Resolve("127.0.0.1:26101", peer));

	// Null bytes pointer with non-zero n must fail fast, not crash.
	CHECK(!t.SendTo(nullptr, 10, peer));

	// Null buf pointer on recv must fail fast.
	size_t n = 999;
	Endpoint from;
	CHECK(t.RecvFrom(nullptr, 10, n, from) == RecvResult::Error);
	CHECK(n == 0);

	// Zero-length buffer on recv.
	uint8_t unused[1];
	CHECK(t.RecvFrom(unused, 0, n, from) == RecvResult::Error);

	// SendTo to an invalid endpoint must fail.
	Endpoint bad;
	const uint8_t payload[4] = { 1, 2, 3, 4 };
	CHECK(!t.SendTo(payload, sizeof payload, bad));
}

static void testEndpointFormatInvalid() {
	Endpoint empty;
	char buf[64];
	size_t n = empty.Format(buf, sizeof buf);
	CHECK(n == 9);  // strlen("<invalid>") == 9
	CHECK(std::strcmp(buf, "<invalid>") == 0);
}

int main() {
	testListenAndClose();
	testResolveValid();
	testResolveInvalid();
	testRecvWouldBlock();
	testLoopbackSendRecv();
	testNullPointerGuards();
	testEndpointFormatInvalid();

	if (fails == 0) {
		std::printf("netplay transport selftest: OK\n");
		return 0;
	}
	std::fprintf(stderr, "netplay transport selftest: %d FAILURES\n", fails);
	return 1;
}
