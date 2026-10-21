// AltirraSDL native FujiNet/NetSIO transport. GPL-2.0-or-later.
// Protocol source: FujiNetWIFI/fujinet-emulator-bridge/netsio.md and
// fujinet-firmware lib/bus/sio/NetSIO.{h,cpp}. The table/header, rather
// than the swapped PROCEED/INTERRUPT prose in netsio.md, defines signal IDs.
#include <stdafx.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <vector>
#include <at/atcore/asyncdispatcher.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/devicesio.h>
#include <at/atcore/devicesnapshot.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/scheduler.h>
#include <at/atcore/snapshotimpl.h>
#include <at/atcore/timerservice.h>
#include <at/atnetworksockets/nativesockets.h>
#include <vd2/system/error.h>
#include <vd2/system/text.h>
#include "netsio.h"

namespace {
enum : uint8 {
	Data = 0x01, Block = 0x02, DataSync = 0x09,
	CommandOff = 0x10, CommandOn = 0x11, CommandSync = 0x18,
	MotorOff = 0x20, MotorOn = 0x21,
	ProceedOff = 0x30, ProceedOn = 0x31,
	InterruptOff = 0x40, InterruptOn = 0x41,
	Speed = 0x80, SyncReply = 0x81, Idle = 0x88,
	Left = 0xC0, Joined = 0xC1, Ping = 0xC2, Pong = 0xC3,
	Alive = 0xC4, AliveReply = 0xC5, Credit = 0xC6, Grant = 0xC7,
	Warm = 0xFE, Cold = 0xFF
};
constexpr size_t kQueueLimit = 64;
using Clock = std::chrono::steady_clock;

uint32 Read32(const uint8 *p) {
	return p[0] | ((uint32)p[1] << 8) | ((uint32)p[2] << 16)
		| ((uint32)p[3] << 24);
}
bool ParseIPv4(const wchar_t *text, uint32& address) {
	address = 0;
	for (unsigned part = 0; part < 4; ++part) {
		if (*text < L'0' || *text > L'9') return false;
		unsigned value = 0, digits = 0;
		while (*text >= L'0' && *text <= L'9') {
			if (++digits > 3) return false;
			value = value * 10 + (*text++ - L'0');
		}
		if (value > 255) return false;
		address = (address << 8) | value;
		if (part != 3 && *text++ != L'.') return false;
	}
	return !*text;
}
bool ValidPacket(const uint8 *p, size_t n) {
	if (!n) return false;
	switch (p[0]) {
		case Data: return n == 2;
		case Block: return n >= 2 && n <= 513;
		case Speed: return n == 5 && Read32(p + 1) >= 50
			&& Read32(p + 1) <= 2000000;
		case SyncReply: return n == 6 && p[2] <= 1;
		case Idle: return n == 3;
		case Credit: return n == 2;
		case ProceedOff: case ProceedOn: case InterruptOff:
		case InterruptOn: case Left: case Joined: case Ping:
		case Alive: return n == 1;
		default: return false;
	}
}
}

class ATDeviceNetSIO final : public ATDeviceT<IATDeviceScheduling,
	IATDeviceSIO, IATDeviceRawSIO, IATDeviceNetSIOControl,
	IATDeviceSnapshot>, public IATSchedulerCallback {
public:
	~ATDeviceNetSIO() { Shutdown(); }
	void GetDeviceInfo(ATDeviceInfo&) override;
	void GetSettings(ATPropertySet&) override;
	bool SetSettings(const ATPropertySet&) override;
	void GetSettingsBlurb(VDStringW&) override;
	bool GetErrorStatus(uint32, VDStringW&) override;
	void Init() override;
	void Shutdown() override;
	void ColdReset() override { ResetPeer(true); }
	void ComputerColdReset() override { ClearBus(); }
	void WarmReset() override { ResetPeer(false); }
	void InitScheduling(ATScheduler *s, ATScheduler*) override { mpScheduler = s; }
	void InitSIO(IATDeviceSIOManager *mgr) override {
		mpSIO = mgr;
		mpSIOInterface = mgr->AddDevice(this);
	}
	CmdResponse OnSerialBeginCommand(const ATDeviceSIOCommand&) override {
		return kCmdResponse_NotHandled;
	}
	CmdResponse OnSerialAccelCommand(const ATDeviceSIORequest&) override;
	void OnSerialAbortCommand() override {}
	void OnSerialReceiveComplete(uint32, const void*, uint32, bool) override {}
	void OnSerialFence(uint32) override {}
	void OnCommandStateChanged(bool) override;
	void OnMotorStateChanged(bool) override;
	void OnReadyStateChanged(bool asserted) override {
		if (!asserted) ClearBus();
	}
	void OnReceiveByte(uint8, bool, uint32) override;
	void OnSendReady() override {}
	void OnScheduledEvent(uint32) override;
	Status GetNetSIOStatus() const override;
	void Reconnect() override;
	void Disconnect() override;
	void ResetPeer(bool) override;
	void GetSnapshotStatus(ATSnapshotStatus& status) const override {
		// External network/filesystem state cannot be rewound by a snapshot.
		status.mbPartialAccuracy = true;
	}
	vdrefptr<IATObjectState> SaveState(ATSnapshotContext&) const override;
	void LoadState(const IATObjectState*, ATSnapshotContext&) override { ClearBus(); }

private:
	struct Packet {
		std::vector<uint8> bytes;
		uint64 generation = 0;
		size_t position = 1;
		bool credit = false;
		bool dataSync = false;
	};
	void OnSocketEvent(const ATSocketStatus&);
	void DrainTransport();
	void Pump();
	void ClearBus();
	void Heartbeat();
	void QueueWake();
	void FinishPacket();
	bool Send(const uint8*, size_t);
	bool SendDatagram(const ATSocketAddress&, const uint8*, size_t, bool wait);
	void SendCode(uint8 code) { Send(&code, 1); }
	void SetTransmitRate(uint32 cyclesPerBit);
	void Sync(uint8 kind, uint8 value = 0);
	bool Connected() const;
	double Rate() const;

	ATScheduler *mpScheduler = nullptr;
	IATDeviceSIOManager *mpSIO = nullptr;
	vdrefptr<IATDeviceSIOInterface> mpSIOInterface;
	IATAsyncDispatcher *mpDispatcher = nullptr;
	IATTimerService *mpTimers = nullptr;
	vdrefptr<IATDatagramSocket> mpSocket;
	ATEvent *mpEvent = nullptr;
	uint64 mWakeToken = 0, mTimerToken = 0;
	bool mbRawRegistered = false;
	bool mbWantListening = false;
	uint32 mPort = 9997, mBindAddress = 0x7F000001;
	VDStringW mBindText { L"127.0.0.1" };
	uint32 mSyncTimeout = 100;

	// The socket worker owns no simulator state. Only this bounded inbox,
	// peer bookkeeping and sync notification cross the thread boundary.
	mutable std::mutex mMutex;
	std::mutex mSendMutex;
	std::condition_variable mSyncCondition;
	std::deque<Packet> mInbox;
	ATSocketAddress mPeer {};
	Status mStatus;
	Clock::time_point mLastPacket = Clock::now();
	uint64 mGeneration = 0;
	size_t mOutstanding = 0;
	int mWaitSync = -1;
	bool mbSyncDone = false;
	bool mbWaitDataSync = false;
	bool mbCreditRequested = false;
	uint8 mSyncNumber = 0;

	// Emulation-thread state only.
	std::deque<Packet> mEvents;
	uint64 mActiveGeneration = 0;
	uint32 mReceiveBaud = 19200, mTransmitBaud = 0;
	uint32 mWriteRemaining = 0;
	bool mbSending = false, mbCommand = false, mbMotor = false;
};

void ATCreateDeviceNetSIO(const ATPropertySet& settings, IATDevice **out) {
	vdrefptr dev { new ATDeviceNetSIO };
	dev->SetSettings(settings);
	*out = dev.release();
}
extern const ATDeviceDefinition g_ATDeviceDefNetSIO {
	"netsio", "netsio", L"FujiNet / NetSIO", ATCreateDeviceNetSIO
};
void ATDeviceNetSIO::GetDeviceInfo(ATDeviceInfo& info) { info.mpDef = &g_ATDeviceDefNetSIO; }
void ATDeviceNetSIO::GetSettings(ATPropertySet& settings) {
	settings.SetUint32("port", mPort);
	settings.SetString("bind_address", mBindText.c_str());
	settings.SetUint32("sync_timeout_ms", mSyncTimeout);
}
bool ATDeviceNetSIO::SetSettings(const ATPropertySet& settings) {
	const uint32 port = settings.GetUint32("port", 9997);
	const wchar_t *text = settings.GetString("bind_address", L"127.0.0.1");
	uint32 address = 0;
	if (!port || port > 65535 || !ParseIPv4(text, address))
		throw MyError("NetSIO requires an IPv4 bind address and a UDP port from 1 to 65535.");
	const uint32 timeout = std::clamp(settings.GetUint32("sync_timeout_ms", 100), 10U, 1000U);
	if (mpSocket && (port != mPort || address != mBindAddress)) return false;
	mPort = port;
	mBindAddress = address;
	mBindText = text;
	mSyncTimeout = timeout;
	return true;
}
IATDeviceNetSIOControl::Status ATDeviceNetSIO::GetNetSIOStatus() const {
	std::lock_guard lock(mMutex);
	auto status = mStatus;
	status.port = (uint16)mPort;
	if (mpSocket) {
		const auto socketStatus = mpSocket->GetSocketStatus();
		status.listening = status.listening && !socketStatus.mbConnecting
			&& !socketStatus.mbClosed && socketStatus.mError == ATSocketError::None;
	}
	return status;
}
void ATDeviceNetSIO::GetSettingsBlurb(VDStringW& text) {
	const auto status = GetNetSIOStatus();
	if (status.connected)
		text.sprintf(L"Connected to %ls", VDTextU8ToW(status.peer).c_str());
	else
		text.sprintf(L"%ls UDP %u (%ls)", mBindText.c_str(), mPort,
			status.listening ? L"waiting for FujiNet" : L"disconnected");
}
bool ATDeviceNetSIO::GetErrorStatus(uint32 index, VDStringW& text) {
	if (index) return false;
	text = GetNetSIOStatus().lastError;
	return !text.empty();
}
void ATDeviceNetSIO::Init() {
	mpDispatcher = GetService<IATAsyncDispatcher>();
	mpTimers = GetService<IATTimerService>();
	if (!mpDispatcher || !mpScheduler || !mpSIO)
		throw MyError("NetSIO requires the emulation dispatcher, scheduler and SIO services.");
	mpSIO->AddRawDevice(this);
	mbRawRegistered = true;
	Reconnect();
	Heartbeat();
}
void ATDeviceNetSIO::Shutdown() {
	Disconnect();
	if (mpTimers) mpTimers->Cancel(&mTimerToken);
	if (mpDispatcher) mpDispatcher->Cancel(&mWakeToken);
	if (mpSIO && mbRawRegistered) {
		mpSIO->RemoveRawDevice(this);
		mbRawRegistered = false;
	}
	mpSIOInterface = nullptr;
	mpSIO = nullptr;
	mpScheduler = nullptr;
	mpTimers = nullptr;
	mpDispatcher = nullptr;
}
void ATDeviceNetSIO::Reconnect() {
	// Keep a healthy UDP listener bound. Close/rebind is asynchronous in
	// the portable socket layer and can otherwise lose the first handshake
	// or briefly collide with the previous socket's pending close.
	if (mpSocket) {
		const auto status = mpSocket->GetSocketStatus();
		if (!status.mbClosed && status.mError == ATSocketError::None) {
			SendCode(Left);
			{
				std::lock_guard lock(mMutex);
				mStatus.connected = false;
				mStatus.peer.clear();
				mStatus.lastError.clear();
				++mGeneration;
			}
			ClearBus();
			mbWantListening = true;
			return;
		}
	}
	Disconnect();
	mbWantListening = true;
#ifdef __EMSCRIPTEN__
	std::lock_guard lock(mMutex);
	mStatus.lastError = L"Native UDP NetSIO is unavailable in a web browser.";
#else
	mpSocket = ATNetBind(ATSocketAddress::CreateIPv4(mBindAddress, (uint16)mPort), false);
	{
		std::lock_guard lock(mMutex);
		mStatus.listening = mpSocket != nullptr;
		mStatus.lastError = mpSocket ? L"" : L"Unable to bind NetSIO UDP port; check whether another program is using it.";
	}
	if (mpSocket)
		mpSocket->SetOnEvent(nullptr,
			[this](const ATSocketStatus& status) { OnSocketEvent(status); }, true);
#endif
}
void ATDeviceNetSIO::Disconnect() {
	mbWantListening = false;
	SendCode(Left);
	// Clearing the socket callback joins any running worker callback before
	// destroying the inbox, dispatcher token or device.
	if (mpSocket) {
		mpSocket->SetOnEvent(nullptr, nullptr, false);
		mpSocket->CloseSocket(true);
		mpSocket = nullptr;
	}
	if (mpDispatcher) mpDispatcher->Cancel(&mWakeToken);
	{
		std::lock_guard lock(mMutex);
		mStatus.listening = mStatus.connected = false;
		mStatus.peer.clear();
		mPeer = {};
		++mGeneration;
		mInbox.clear();
		mOutstanding = 0;
		mbCreditRequested = false;
		mWaitSync = -1;
		mbSyncDone = false;
	}
	mSyncCondition.notify_all();
	ClearBus();
}
bool ATDeviceNetSIO::Connected() const {
	std::lock_guard lock(mMutex);
	return mStatus.connected;
}
double ATDeviceNetSIO::Rate() const {
	const double rate = mpScheduler ? mpScheduler->GetRate().asDouble() : 0;
	return rate > 0 ? rate : 1789772.5;
}
bool ATDeviceNetSIO::Send(const uint8 *bytes, size_t length) {
	ATSocketAddress peer;
	{
		std::lock_guard lock(mMutex);
		if (!mStatus.connected || !mpSocket) return false;
		peer = mPeer;
	}
	return SendDatagram(peer, bytes, length, true);
}
bool ATDeviceNetSIO::SendDatagram(const ATSocketAddress& peer,
	const uint8 *bytes, size_t length, bool wait)
{
	const auto deadline = Clock::now() + std::chrono::milliseconds(wait ? mSyncTimeout : 0);
	for (;;) {
		bool sent = false;
		{
			// The native UDP ring buffer is single-producer. Serialize main
			// thread SIO bytes with worker-thread protocol/keepalive replies.
			std::lock_guard sendLock(mSendMutex);
			// Main-thread byte traffic observes the conservative high-water
			// mark, leaving headroom for small worker-side grants/keepalives.
			if (mpSocket && (!wait || mpSocket->GetSocketStatus().mbCanWrite))
				sent = mpSocket->SendTo(peer, bytes, (uint32)length);
		}
		if (sent) {
			std::lock_guard lock(mMutex);
			++mStatus.txPackets;
			return true;
		}
		std::unique_lock lock(mMutex);
		if (!wait || !mStatus.listening || !mpSocket || Clock::now() >= deadline) {
			++mStatus.droppedPackets;
			if (wait)
				mStatus.lastError = L"NetSIO UDP transmit queue did not become writable before the timeout.";
			return false;
		}
		// Never block the socket worker: only the emulator can wait, with
		// its clock paused, while the OS worker drains outbound UDP traffic.
		mSyncCondition.wait_until(lock, deadline, [this] {
			return !mStatus.listening || !mpSocket
				|| mpSocket->GetSocketStatus().mbCanWrite;
		});
	}
}
void ATDeviceNetSIO::QueueWake() {
	mpDispatcher->Queue(&mWakeToken, [this] { DrainTransport(); });
}
void ATDeviceNetSIO::OnSocketEvent(const ATSocketStatus& status) {
	if (status.mbCanWrite) mSyncCondition.notify_all();
	if (status.mbClosed || status.mError != ATSocketError::None) {
		{
			std::lock_guard lock(mMutex);
			mStatus.listening = mStatus.connected = false;
			mStatus.lastError = L"NetSIO UDP socket failed; check the bind address and port.";
			++mGeneration;
			mInbox.clear();
			mOutstanding = 0;
			mWaitSync = -1;
		}
		mSyncCondition.notify_all();
		QueueWake();
		return;
	}
	uint8 buffer[514];
	ATSocketAddress from;
	for (;;) {
		const sint32 size = mpSocket->RecvFrom(from, buffer, sizeof buffer);
		if (size < 0) break;
		std::vector<uint8> reply;
		bool wake = false, sync = false;
		{
			std::lock_guard lock(mMutex);
			++mStatus.rxPackets;
			if (!ValidPacket(buffer, (size_t)size)) {
				++mStatus.droppedPackets;
				continue;
			}
			if (buffer[0] == Ping) reply = { Pong };
			else if (buffer[0] == Joined && (!mStatus.connected || from == mPeer)) {
				mPeer = from;
				mStatus.connected = true;
				mStatus.peer = from.ToString();
				mStatus.lastError.clear();
				mLastPacket = Clock::now();
				++mGeneration;
				mInbox.clear();
				mOutstanding = 0;
				mInbox.push_back({ { Joined }, mGeneration });
				reply = { Grant, 3 };
				wake = true;
			} else if (!mStatus.connected || from != mPeer) {
				++mStatus.droppedPackets;
			} else {
				mLastPacket = Clock::now();
				if (buffer[0] == Alive) reply = { AliveReply };
				else if (buffer[0] == Credit) {
					// Credit is an absolute packet allowance, not a byte count.
					// Retain headroom for packets already in the network socket.
					const size_t free = kQueueLimit - std::min(kQueueLimit, mOutstanding);
					mbCreditRequested = free < 2;
					if (free >= 2) reply = { Grant, (uint8)std::min<size_t>(3, free) };
				} else if (buffer[0] == Left) {
					mStatus.connected = false;
					++mGeneration;
					mInbox.clear();
					mOutstanding = 0;
					mInbox.push_back({ { Left }, mGeneration });
					mWaitSync = -1;
					wake = sync = true;
				} else if (mOutstanding >= kQueueLimit) {
					++mStatus.droppedPackets;
					mStatus.connected = false;
					mStatus.lastError = L"NetSIO peer exceeded receive credits; reconnect required.";
					++mGeneration;
					mInbox.clear();
					mOutstanding = 0;
					mWaitSync = -1;
					wake = sync = true;
				} else if (buffer[0] == SyncReply
					&& (buffer[1] != mWaitSync || mbSyncDone)) {
					// UDP can duplicate a datagram. A sync sequence contributes
					// one ACK even before the emulation thread resumes its wait.
					++mStatus.droppedPackets;
				} else {
					Packet packet;
					packet.bytes.assign(buffer, buffer + size);
					packet.generation = mGeneration;
					packet.credit = true;
					packet.dataSync = buffer[0] == SyncReply && mbWaitDataSync;
					mInbox.push_back(std::move(packet));
					++mOutstanding;
					if (buffer[0] == SyncReply && buffer[1] == mWaitSync) {
						mbSyncDone = true;
						mStatus.lastError.clear();
						sync = true;
					}
					wake = true;
				}
			}
		}
		if (!reply.empty()) SendDatagram(from, reply.data(), reply.size(), false);
		if (sync) mSyncCondition.notify_all();
		if (wake) QueueWake();
	}
}
void ATDeviceNetSIO::ClearBus() {
	if (mpScheduler) mpScheduler->UnsetEvent(mpEvent);
	mbSending = false;
	mWriteRemaining = 0;
	mReceiveBaud = 19200;
	mTransmitBaud = 0;
	mbCommand = false;
	mbMotor = false;
	mEvents.clear();
	{
		std::lock_guard lock(mMutex);
		mInbox.clear();
		mOutstanding = 0;
		mbCreditRequested = false;
		mStatus.baud = 19200;
		mWaitSync = -1;
		mbSyncDone = false;
		++mSyncNumber;
		mActiveGeneration = mGeneration;
	}
	if (mpSIO && mbRawRegistered) {
		mpSIO->SetSIOProceed(this, false);
		mpSIO->SetSIOInterrupt(this, false);
		mpSIO->SetExternalClock(this, 0, 0);
	}
}
void ATDeviceNetSIO::DrainTransport() {
	std::deque<Packet> incoming;
	uint64 generation;
	bool connected;
	{
		std::lock_guard lock(mMutex);
		incoming.swap(mInbox);
		generation = mGeneration;
		connected = mStatus.connected;
	}
	if (generation != mActiveGeneration || !connected) {
		if (mpScheduler) mpScheduler->UnsetEvent(mpEvent);
		mEvents.clear();
		mbSending = false;
		mWriteRemaining = 0;
		mReceiveBaud = 19200;
		mTransmitBaud = 0;
		mActiveGeneration = generation;
		if (mpSIO) {
			mpSIO->SetSIOProceed(this, false);
			mpSIO->SetSIOInterrupt(this, false);
			mpSIO->SetExternalClock(this, 0, 0);
		}
	}
	// Packets captured before disconnect/error/overflow must not reassert
	// a released signal or inject bytes into the next connection.
	if (!connected) return;
	for (auto& packet : incoming) {
		if (packet.generation != generation) continue;
		if (packet.bytes[0] == Joined) {
			OnMotorStateChanged(mpSIO->IsSIOMotorAsserted());
			continue;
		}
		if (packet.bytes[0] != Left) mEvents.push_back(std::move(packet));
	}
	Pump();
}
void ATDeviceNetSIO::FinishPacket() {
	const bool credit = mEvents.front().credit;
	const uint64 generation = mEvents.front().generation;
	mEvents.pop_front();
	bool grant = false;
	if (credit) {
		std::lock_guard lock(mMutex);
		if (generation == mGeneration && mOutstanding) --mOutstanding;
		if (mStatus.connected && mbCreditRequested && mOutstanding <= kQueueLimit - 3) {
			mbCreditRequested = false;
			grant = true;
		}
	}
	if (grant) { const uint8 packet[] = { Grant, 3 }; Send(packet, sizeof packet); }
}
void ATDeviceNetSIO::Pump() {
	if (mpEvent || !mpSIO || !mpScheduler) return;
	while (!mEvents.empty()) {
		auto& packet = mEvents.front();
		const uint8 *p = packet.bytes.data();
		switch (p[0]) {
			case Speed:
				mReceiveBaud = Read32(p + 1);
				if (mbMotor)
					mpSIO->SetExternalClock(this, 0,
						std::max<uint32>(1, (uint32)(Rate() / mReceiveBaud + 0.5)));
				{ std::lock_guard lock(mMutex); mStatus.baud = mReceiveBaud; }
				FinishPacket();
				break;
			case ProceedOn: case ProceedOff:
				mpSIO->SetSIOProceed(this, p[0] == ProceedOn);
				FinishPacket(); break;
			case InterruptOn: case InterruptOff:
				mpSIO->SetSIOInterrupt(this, p[0] == InterruptOn);
				FinishPacket(); break;
			case Idle: {
				const uint32 ms = p[1] | ((uint32)p[2] << 8);
				mpScheduler->SetEvent(std::max<uint32>(1,
					(uint32)(Rate() * ms / 1000.0)), this, 2, mpEvent);
				return;
			}
			case SyncReply:
				if (p[1] != mSyncNumber || !p[2]) { FinishPacket(); break; }
				if (packet.dataSync && packet.position == 1) {
					// Data ACK requires 850us-16ms turnaround. The reference
					// bridge uses 1ms; command-frame ACK does not need this.
					packet.position = 2;
					mpScheduler->SetEvent(std::max<uint32>(1,
						(uint32)(Rate() / 1000.0)), this, 3, mpEvent);
					return;
				}
				mWriteRemaining = p[4] | ((uint32)p[5] << 8);
				packet.position = 3;
				[[fallthrough]];
			case Data: case Block: {
				if (mpSIO->IsSIOCommandAsserted()) { FinishPacket(); break; }
				const uint32 cpb = std::max<uint32>(1,
					(uint32)(Rate() / mReceiveBaud + 0.5));
				const uint8 value = packet.bytes[packet.position++];
				mpSIO->SendRawByte(value, cpb);
				mbSending = true;
				mpScheduler->SetEvent(cpb * 10 + 3, this, 1, mpEvent);
				return;
			}
			default: FinishPacket(); break;
		}
	}
}
void ATDeviceNetSIO::OnScheduledEvent(uint32 id) {
	mpEvent = nullptr;
	if (mEvents.empty()) return;
	if (id == 3) { Pump(); return; }
	if (id == 2 || mEvents.front().bytes[0] == SyncReply
		|| mEvents.front().position >= mEvents.front().bytes.size())
		FinishPacket();
	mbSending = false;
	Pump();
}
void ATDeviceNetSIO::SetTransmitRate(uint32 cyclesPerBit) {
	if (!cyclesPerBit) return;
	const uint32 baud = std::clamp((uint32)(Rate() / cyclesPerBit + 0.5), 50U, 2000000U);
	if (baud == mTransmitBaud) return;
	mTransmitBaud = baud;
	uint8 packet[] = { Speed, (uint8)baud, (uint8)(baud >> 8),
		(uint8)(baud >> 16), (uint8)(baud >> 24) };
	Send(packet, sizeof packet);
}
void ATDeviceNetSIO::Sync(uint8 kind, uint8 value) {
	uint8 number;
	{
		std::lock_guard lock(mMutex);
		if (!mStatus.connected) return;
		number = ++mSyncNumber;
		mWaitSync = number;
		mbSyncDone = false;
		mbWaitDataSync = kind == DataSync;
	}
	uint8 packet[] = { kind, kind == CommandSync ? number : value, number };
	if (!Send(packet, kind == CommandSync ? 2 : 3)) {
		std::lock_guard lock(mMutex);
		mWaitSync = -1;
		return;
	}
	{
		std::unique_lock lock(mMutex);
		// The worker only queues packets and wakes this bounded wait. No
		// core state is touched off-thread. Pausing emulation here implements
		// NetSIO's defined command/data ACK synchronization (16ms SIO limit).
		if (!mSyncCondition.wait_for(lock, std::chrono::milliseconds(mSyncTimeout),
			[this, number] { return mbSyncDone || mWaitSync != number || !mStatus.connected; })) {
			++mStatus.syncTimeouts;
			mStatus.lastError = L"FujiNet did not acknowledge an SIO transaction before the synchronization timeout.";
		}
		mWaitSync = -1;
	}
	DrainTransport();
}
IATDeviceSIO::CmdResponse ATDeviceNetSIO::OnSerialAccelCommand(const ATDeviceSIORequest&) {
	// Earlier local disk/device interfaces retain their own acceleration.
	// Unhandled requests must reach the real serial bus for the external peer.
	return Connected() ? kCmdResponse_BypassAccel : kCmdResponse_NotHandled;
}
void ATDeviceNetSIO::OnCommandStateChanged(bool asserted) {
	DrainTransport();
	mbCommand = asserted;
	mWriteRemaining = 0;
	if (!Connected()) return;
	if (asserted) {
		if (mpScheduler) mpScheduler->UnsetEvent(mpEvent);
		while (!mEvents.empty()) FinishPacket();
		mbSending = false;
		SendCode(CommandOn);
	} else Sync(CommandSync);
}
void ATDeviceNetSIO::OnMotorStateChanged(bool asserted) {
	mbMotor = asserted;
	// FujiNet concurrent modem/Netstream clients use MOTOR and POKEY's
	// external serial clock. Internal-clock SIO transfers ignore this clock.
	if (mpSIO && mbRawRegistered)
		mpSIO->SetExternalClock(this, 0, asserted && Connected()
			? std::max<uint32>(1, (uint32)(Rate() / mReceiveBaud + 0.5)) : 0);
	if (Connected()) SendCode(asserted ? MotorOn : MotorOff);
}
void ATDeviceNetSIO::OnReceiveByte(uint8 value, bool command, uint32 cyclesPerBit) {
	if (!Connected()) return;
	SetTransmitRate(cyclesPerBit);
	if (!command && mWriteRemaining && --mWriteRemaining == 0) {
		Sync(DataSync, value);
		return;
	}
	// Individual bytes deliberately avoid the released legacy firmware's
	// DATA_BLOCK trailing-byte quirk and remain valid for corrected peers.
	const uint8 packet[] = { Data, value };
	Send(packet, sizeof packet);
}
void ATDeviceNetSIO::ResetPeer(bool cold) {
	ClearBus();
	SendCode(cold ? Cold : Warm);
}
void ATDeviceNetSIO::Heartbeat() {
	bool lost = false;
	{
		std::lock_guard lock(mMutex);
		if (mStatus.connected && Clock::now() - mLastPacket > std::chrono::seconds(15)) {
			mStatus.connected = false;
			mStatus.peer.clear();
			mStatus.lastError = L"FujiNet connection timed out; waiting for it to reconnect.";
			++mGeneration;
			lost = true;
		}
	}
	if (lost) ClearBus();
	if (mbWantListening && (!mpSocket
		|| mpSocket->GetSocketStatus().mbClosed
		|| mpSocket->GetSocketStatus().mError != ATSocketError::None))
		Reconnect();
	if (mpTimers) mpTimers->Request(&mTimerToken, 1.0f, [this] { Heartbeat(); });
}
struct ATSaveStateNetSIO final
	: ATSnapExchangeObject<ATSaveStateNetSIO, "ATSaveStateNetSIO"> {
	template<ATExchanger T> void Exchange(T&) {}
};
vdrefptr<IATObjectState> ATDeviceNetSIO::SaveState(ATSnapshotContext&) const {
	return vdrefptr<IATObjectState>(new ATSaveStateNetSIO);
}
