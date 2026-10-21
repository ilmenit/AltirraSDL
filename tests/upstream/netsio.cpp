#include <stdafx.h>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <source_location>
#include <thread>
#include <vector>
#include <at/atcore/asyncdispatcherimpl.h>
#include <at/atcore/device.h>
#include <at/atcore/devicesio.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/scheduler.h>
#include <at/atcore/timerservice.h>
#include <at/atnetworksockets/nativesockets.h>
#include "netsio.h"

extern const ATDeviceDefinition g_ATDeviceDefNetSIO;
static void Check(bool value, const char *text) {
	if (!value) { fprintf(stderr, "%s\n", text); std::exit(1); }
}
struct Services final : IATDeviceManager, IATDeviceSIOManager, IATTimerService {
	ATAsyncDispatcher dispatcher;
	ATScheduler scheduler;
	std::thread::id mainThread = std::this_thread::get_id();
	std::vector<uint8> bytes;
	std::vector<uint32> rates;
	std::vector<uint64> times;
	IATDeviceRawSIO *raw = nullptr;
	bool command = false, motor = false, proceed = false, interrupt = false;
	uint32 clock = 0;
	uint64 timerSerial = 0;
	vdfunction<void()> timerCallback;
	Services() { scheduler.SetRate(VDFraction(1789773, 1)); }
	void *GetService(uint32 id) override {
		if (id == IATAsyncDispatcher::kTypeID) return &dispatcher;
		if (id == IATTimerService::kTypeID) return static_cast<IATTimerService*>(this);
		return nullptr;
	}
	void Request(uint64 *token, float, vdfunction<void()> fn) override {
		if (token) *token = ++timerSerial;
		timerCallback = std::move(fn);
	}
	void Cancel(uint64 *token) override {
		if (token && *token) { *token = 0; timerCallback = nullptr; }
	}
	void FireTimer() {
		auto callback = std::move(timerCallback);
		if (callback) callback();
	}
	void NotifyDeviceStatusChanged(IATDevice&) override {}
	vdrefptr<IATDeviceSIOInterface> AddDevice(IATDeviceSIO*) override { return {}; }
	sint32 GetHighSpeedIndex() const override { return -1; }
	uint32 GetCyclesPerBitRecv() const override { return 93; }
	uint32 GetRecvResetCounter() const override { return 0; }
	uint32 GetCyclesPerBitSend() const override { return 93; }
	uint32 GetCyclesPerBitBiClock() const override { return 93; }
	void AddRawDevice(IATDeviceRawSIO *dev) override { raw = dev; }
	void RemoveRawDevice(IATDeviceRawSIO *dev) override { Check(raw == dev, "Wrong raw device release"); raw = nullptr; }
	void SendRawByte(uint8 value, uint32 cpb, bool, bool, bool) override {
		Check(std::this_thread::get_id() == mainThread, "Network worker touched emulator state");
		bytes.push_back(value); rates.push_back(cpb); times.push_back(scheduler.GetTick64());
	}
	void SetRawInput(bool) override {}
	bool IsSIOCommandAsserted() const override { return command; }
	bool IsSIOMotorAsserted() const override { return motor; }
	bool IsSIOReadyAsserted() const override { return true; }
	bool IsSIOForceBreakAsserted() const override { return false; }
	void SetSIOInterrupt(IATDeviceRawSIO*, bool value) override {
		Check(std::this_thread::get_id() == mainThread, "Off-thread INTERRUPT"); interrupt = value;
	}
	void SetSIOProceed(IATDeviceRawSIO*, bool value) override {
		Check(std::this_thread::get_id() == mainThread, "Off-thread PROCEED"); proceed = value;
	}
	void SetBiClockNotifyEnabled(IATDeviceRawSIO*, bool) override {}
	void SetExternalClock(IATDeviceRawSIO*, uint32, uint32 period) override { clock = period; }
	void Advance(uint32 ticks = 20000) {
		for (uint32 i = 0; i < ticks; ++i) { ATSCHEDULER_ADVANCE(&scheduler); }
		dispatcher.RunCallbacks();
	}
};
class Peer {
public:
	vdrefptr<IATDatagramSocket> socket;
	ATSocketAddress target;
	std::mutex mutex;
	std::mutex sendMutex;
	std::vector<std::vector<uint8>> received;
	bool ack = true;
	bool duplicateAck = false;
	bool restartOnSync = false;
	uint16 writeSize = 0;
	uint8 lastSequence = 0;
	Peer(uint16 port, uint16 emulatorPort) {
		target = ATSocketAddress::CreateLocalhostIPv4(emulatorPort);
		socket = ATNetBind(ATSocketAddress::CreateLocalhostIPv4(port), false);
		Check(socket != nullptr, "UDP peer bind failed");
		socket->SetOnEvent(nullptr, [this](const ATSocketStatus& status) {
			if (!status.mbCanRead) return;
			uint8 data[1024]; ATSocketAddress from;
			for (;;) {
				const auto size = socket->RecvFrom(from, data, sizeof data);
				if (size < 0) break;
				bool doAck = false, duplicate = false, restart = false; uint16 sizeHint = 0;
				{
					std::lock_guard lock(mutex);
					received.emplace_back(data, data + size);
					if (size && (data[0] == 0x18 || data[0] == 0x09)) {
						lastSequence = data[data[0] == 0x18 ? 1 : 2];
						doAck = ack; duplicate = duplicateAck; sizeHint = writeSize;
						restart = restartOnSync;
					}
				}
				if (restart) {
					const uint8 joined = 0xC1;
					std::lock_guard sendLock(sendMutex);
					socket->SendTo(from, &joined, 1);
				} else if (doAck) {
					const uint8 reply[] = { 0x81, lastSequence, 1, 'A',
						(uint8)sizeHint, (uint8)(sizeHint >> 8) };
					std::lock_guard sendLock(sendMutex);
					socket->SendTo(from, reply, sizeof reply);
					if (duplicate) socket->SendTo(from, reply, sizeof reply);
				}
			}
		}, true);
	}
	~Peer() { socket->SetOnEvent(nullptr, nullptr, false); socket->CloseSocket(true); }
	void Send(std::initializer_list<uint8> bytes) {
		std::lock_guard sendLock(sendMutex);
		Check(socket->SendTo(target, bytes.begin(), (uint32)bytes.size()), "Peer UDP send failed");
	}
	std::vector<uint8> DataBytes() {
		std::lock_guard lock(mutex);
		std::vector<uint8> result;
		for (const auto& p : received)
			if (p.size() == 2 && p[0] == 1) result.push_back(p[1]);
		return result;
	}
	bool Saw(uint8 code) {
		std::lock_guard lock(mutex);
		for (const auto& p : received) if (!p.empty() && p[0] == code) return true;
		return false;
	}
};
template<class F> static void Wait(Services& services, F condition,
	const std::source_location location = std::source_location::current()) {
	for (int i = 0; i < 200 && !condition(); ++i) {
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
		services.Advance();
	}
	if (!condition()) {
		fprintf(stderr, "UDP fixture timed out at line %u\n", location.line());
		std::exit(1);
	}
}
int main() {
	const auto v4 = ATSocketAddress::CreateLocalhostIPv4(9997);
	Check(v4 == ATSocketAddress::CreateLocalhostIPv4(9997)
		&& v4 != ATSocketAddress::CreateLocalhostIPv4(9998)
		&& v4 != ATSocketAddress::CreateIPv4(0x7F000002, 9997),
		"IPv4 peer identity must include both address and port");
	auto v6 = ATSocketAddress::CreateLocalhostIPv6(9997);
	Check(v6 == ATSocketAddress::CreateLocalhostIPv6(9997) && v6 != v4,
		"IPv6 peer equality or address-family comparison wrong");
	auto other6 = v6; other6.mIPv6.mAddress[15] = 2;
	Check(v6 != other6, "IPv6 peer address comparison ignored bytes");
	other6 = v6; other6.mIPv6.mScopeId = 1;
	Check(v6 != other6, "IPv6 peer identity ignored scope");
	other6 = v6; other6.mPort = 9998;
	Check(v6 != other6, "IPv6 peer identity ignored port");
	Check(ATSocketInit(), "Socket initialization failed");
	{
		Services services;
		const uint16 port = (uint16)(30000 + std::chrono::steady_clock::now().time_since_epoch().count() % 10000);
		ATPropertySet props;
		props.SetUint32("port", port);
		props.SetUint32("sync_timeout_ms", 20);
		vdrefptr<IATDevice> device;
		g_ATDeviceDefNetSIO.mpFactoryFn(props, ~device);
		device->SetManager(&services);
		auto *sio = vdpoly_cast<IATDeviceSIO *>(device);
		auto *schedule = vdpoly_cast<IATDeviceScheduling *>(device);
		auto *control = vdpoly_cast<IATDeviceNetSIOControl *>(device);
		sio->InitSIO(&services);
		schedule->InitScheduling(&services.scheduler, &services.scheduler);
		device->Init();
		Wait(services, [&] { return control->GetNetSIOStatus().listening; });
		Peer peer(port + 1, port), stranger(port + 2, port);
		peer.Send({0xC2});
		Wait(services, [&] { return peer.Saw(0xC3); });
		Check(!control->GetNetSIOStatus().connected, "Ping must not attach a peer");
		peer.Send({0xC1});
		Wait(services, [&] { return control->GetNetSIOStatus().connected && peer.Saw(0xC7); });
		Check(sio->OnSerialAccelCommand({}) == IATDeviceSIO::kCmdResponse_BypassAccel,
			"External SIO must bypass unhandled acceleration");
		peer.Send({0xC4}); Wait(services, [&] { return peer.Saw(0xC5); });
		stranger.Send({0x31}); peer.Send({0x80, 0x00, 0x96, 0, 0});
		peer.Send({0x31}); peer.Send({0x41});
		Wait(services, [&] { return services.proceed && services.interrupt; });
		Check(control->GetNetSIOStatus().baud == 38400, "Peer baud ignored");
		peer.Send({0x02, 0x12, 0x34, 0x56});
		Wait(services, [&] { return services.bytes.size() >= 3; });
		Check(services.bytes[0] == 0x12 && services.bytes[2] == 0x56
			&& services.rates[0] == 47 && services.times[1] - services.times[0] >= 470,
			"Peer block ordering, baud or serial pacing wrong");
		services.command = true;
		services.raw->OnCommandStateChanged(true);
		for (uint8 b : {0x70, 0x53, 0, 0, 0xC3}) services.raw->OnReceiveByte(b, true, 93);
		{ std::lock_guard lock(peer.mutex); peer.writeSize = 3; peer.duplicateAck = true; }
		services.command = false;
		services.raw->OnCommandStateChanged(false);
		Wait(services, [&] { return services.bytes.size() >= 4; });
		Check(services.bytes.back() == 'A', "Command sync ACK lost");
		services.Advance();
		Check(services.bytes.size() == 4, "Duplicate command sync injected a second ACK");
		const size_t beforeWriteAck = services.bytes.size();
		services.raw->OnReceiveByte(0x11, false, 93);
		services.raw->OnReceiveByte(0x22, false, 93);
		services.raw->OnReceiveByte(0x33, false, 93);
		Check(peer.Saw(0x09) && services.bytes.size() == beforeWriteAck,
			"Data sync was not used, or ACK turnaround was too short");
		Wait(services, [&] { return services.bytes.size() > beforeWriteAck; });
		Check(services.bytes.back() == 'A', "Data sync ACK lost");
		{ std::lock_guard lock(peer.mutex); peer.restartOnSync = true; }
		services.command = true; services.raw->OnCommandStateChanged(true);
		services.command = false; services.raw->OnCommandStateChanged(false);
		Check(control->GetNetSIOStatus().syncTimeouts == 0,
			"Peer restart did not cancel pending synchronization");
		Check(control->GetNetSIOStatus().baud == 19200,
			"Peer restart left stale receive baud in status");
		{ std::lock_guard lock(peer.mutex); peer.restartOnSync = false; }
		{ std::lock_guard lock(peer.mutex); peer.ack = false; }
		services.command = true; services.raw->OnCommandStateChanged(true);
		services.command = false;
		const auto started = std::chrono::steady_clock::now();
		services.raw->OnCommandStateChanged(false);
		Check(control->GetNetSIOStatus().syncTimeouts == 1
			&& std::chrono::steady_clock::now() - started < std::chrono::seconds(1),
			"Missing sync ACK caused an unbounded wait");
		const auto beforeBad = services.bytes.size();
		peer.Send({0x81, 0, 1, 'X', 0, 0}); peer.Send({0x01});
		services.Advance(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); services.Advance();
		Check(services.bytes.size() == beforeBad, "Malformed/stale ACK reached POKEY");
		// Run the host side without advancing emulated time: this exercises
		// transmit backpressure in Turbo, including concurrent worker replies.
		const auto beforeTurbo = peer.DataBytes().size();
		peer.Send({0xC6, 0}); peer.Send({0xC4});
		for (unsigned i = 0; i < 2000; ++i)
			services.raw->OnReceiveByte((uint8)i, false, 14);
		Wait(services, [&] { return peer.DataBytes().size() >= beforeTurbo + 2000; });
		const auto afterTurbo = peer.DataBytes();
		for (unsigned i = 0; i < 2000; ++i)
			Check(afterTurbo[beforeTurbo + i] == (uint8)i,
				"Turbo host bytes lost/reordered in UDP send queue");
		services.motor = true; services.raw->OnMotorStateChanged(true);
		Check(services.clock > 0, "Concurrent serial clock missing");
		const auto beforeResetPackets = control->GetNetSIOStatus().rxPackets;
		peer.Send({0x88, 200, 0}); peer.Send({0x01, 0xED});
		Wait(services, [&] {
			return control->GetNetSIOStatus().rxPackets >= beforeResetPackets + 2;
		});
		const auto beforeResetBytes = services.bytes.size();
		device->WarmReset();
		Check(!services.proceed && !services.interrupt && !services.clock,
			"Reset left signals or external clock acquired");
		services.Advance(400000);
		Check(services.bytes.size() == beforeResetBytes,
			"Reset retained scheduled idle/data from the previous transaction");
		Wait(services, [&] { return peer.Saw(0xFE); });
		device->ColdReset(); Wait(services, [&] { return peer.Saw(0xFF); });
		peer.Send({0x31}); peer.Send({0x01, 0xEE}); peer.Send({0xC0});
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		Wait(services, [&] { return !control->GetNetSIOStatus().connected; });
		services.dispatcher.RunCallbacks();
		Check(!services.proceed && !services.interrupt && !services.clock,
			"Queued packets reasserted state after disconnect");
		Check(sio->OnSerialAccelCommand({}) == IATDeviceSIO::kCmdResponse_NotHandled,
			"Disconnected device still intercepts acceleration");
		control->Reconnect(); peer.Send({0xC1});
		Wait(services, [&] { return control->GetNetSIOStatus().connected; });
		// An ill-behaved peer ignoring credits must disconnect safely rather
		// than growing the FIFO indefinitely or retaining acquired signals.
		for (int i = 0; i < 80; ++i) peer.Send({0x31});
		std::this_thread::sleep_for(std::chrono::milliseconds(30));
		services.dispatcher.RunCallbacks();
		Check(!control->GetNetSIOStatus().connected && !services.proceed,
			"Receive overflow did not release the bus and disconnect");
		peer.Send({0xC1});
		Wait(services, [&] { return control->GetNetSIOStatus().connected; });
		// Native socket close is asynchronous. Rebinding must await release
		// of the previous OS handle, including rapid same-port cycles.
		for (unsigned i = 0; i < 20; ++i) {
			control->Disconnect();
			control->Reconnect();
			Wait(services, [&] { return control->GetNetSIOStatus().listening; });
			peer.Send({0xC1});
			Wait(services, [&] { return control->GetNetSIOStatus().connected; });
		}
		peer.Send({0x31}); peer.Send({0x41});
		Wait(services, [&] { return services.proceed && services.interrupt; });
		services.motor = true; services.raw->OnMotorStateChanged(true);
		std::this_thread::sleep_for(std::chrono::milliseconds(15100));
		services.FireTimer();
		Check(!control->GetNetSIOStatus().connected && !services.proceed
			&& !services.interrupt && !services.clock,
			"Silent peer expiry retained its connection or bus signals");
		const auto beforeExpiredData = services.bytes.size();
		peer.Send({0x01, 0xEF});
		services.Advance();
		peer.Send({0xC1});
		Wait(services, [&] { return control->GetNetSIOStatus().connected; });
		Check(services.bytes.size() == beforeExpiredData,
			"Expired peer injected serial data before rejoining");
		Check(control->GetNetSIOStatus().lastError.empty(), "New peer did not clear expiry error");
		device->Shutdown();
		Check(!services.timerCallback, "Shutdown retained heartbeat timer callback");
		Check(!services.raw && !services.clock, "Shutdown leaked raw device or clock");
		peer.Send({0x01, 0xEE});
		std::this_thread::sleep_for(std::chrono::milliseconds(10)); services.dispatcher.RunCallbacks();
		// Isolate the portable datagram API's full-buffer contract. Hold its
		// OS worker in a callback, then fill the write ring from this thread.
		const auto probeAddress = ATSocketAddress::CreateLocalhostIPv4(port + 3);
		auto probe = ATNetBind(probeAddress, false);
		std::mutex gateMutex;
		std::condition_variable gate;
		bool holding = true, entered = false;
		bool workerEntered = false;
		probe->SetOnEvent(nullptr, [&](const ATSocketStatus& status) {
			if (!status.mbCanRead) return;
			std::unique_lock lock(gateMutex);
			entered = true; gate.notify_all();
			gate.wait(lock, [&] { return !holding; });
		}, true);
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		uint8 payload[1024] {};
		probe->SendTo(probeAddress, payload, 1);
		{
			std::unique_lock lock(gateMutex);
			gate.wait_for(lock, std::chrono::seconds(2), [&] { return entered; });
			workerEntered = entered;
		}
		bool full = false;
		if (workerEntered) {
			for (int i = 0; i < 100; ++i)
				if (!probe->SendTo(probeAddress, payload, sizeof payload)) { full = true; break; }
		}
		{ std::lock_guard lock(gateMutex); holding = false; }
		gate.notify_all();
		probe->SetOnEvent(nullptr, nullptr, false); probe->CloseSocket(true);
		Check(workerEntered && full, "UDP SendTo must report false when its write queue is full");
	}
	ATSocketPreShutdown(); ATSocketShutdown();
	puts("Native NetSIO UDP, synchronization, pacing and cleanup tests passed");
}
