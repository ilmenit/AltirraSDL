// Altirra SDL3 netplay - LAN discovery
//
// Thin UDP-broadcast discovery so the Online browser can list sessions
// running on the same subnet with zero server involvement.  Protocol:
//
//   HOST  --[LanAd]--> 255.255.255.255:port   (every 1 s while advertising)
//   PEER  <--[LanAd]-- 127.0.0.1:ephem         (immediate on first recv)
//
// LanAd is a small fixed-layout packet sharing the same magic+version
// namespace as the netplay protocol:
//
//   u32 magic         = 'ANLD' (0x444C4E41)
//   u16 protoVersion  = 2
//   u16 hostPort      = UDP port the netplay Coordinator listens on
//   u8  visibility    = 0=public, 1=private
//   u8  requiresCode  = 0|1
//   u8  playerCount
//   u8  maxPlayers
//   u64 osRomHash
//   u64 basicRomHash
//   u64 settingsHash
//   u8  cartNameLen   (0..63)
//   u8  cartName[63]  (NUL-padded)
//   u8  hostHandleLen (0..31)
//   u8  hostHandle[31]
//   u8  cartArtHash[32]    (hex-encoded SHA1 prefix; zero-padded)
//   u8  reserved[16]
//
// Total: 4+2+2+1+1+1+1+8+8+8+1+63+1+31+32+16 = 180 bytes (well under
// the 1400-byte safe MTU).  The whole thing travels in one datagram;
// no fragmentation.
//
// The module owns one `Transport`.  `Begin()` binds the broadcast
// socket (v1 uses ephemeral port, because the advertisement is
// outbound-only; replies land on that same socket).  `Poll()` both
// emits heartbeats (when advertising) and drains received ads.
// `StopAdvertising()` stops the emitter; `Stop()` closes the socket.
//
// Opt-in only.  Turning off LAN in lobby.ini simply means the UI never
// calls Begin().

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ATNetplay {

struct LanAd {
	uint16_t    protocolVersion = 0;
	uint16_t    hostPort        = 0;
	uint8_t     visibility      = 0;  // 0 public / 1 private
	uint8_t     requiresCode    = 0;
	uint8_t     playerCount     = 0;
	uint8_t     maxPlayers      = 0;
	uint64_t    osRomHash       = 0;
	uint64_t    basicRomHash    = 0;
	uint64_t    settingsHash    = 0;
	std::string cartName;
	std::string hostHandle;
	std::string cartArtHash;
	// Filled by the receiver side; ignored by the sender.
	std::string senderIp;       // "a.b.c.d"
};

constexpr size_t kLanAdSize = 180;
constexpr uint32_t kLanAdMagic = 0x444C4E41u; // 'ANLD'

// Serialise `in` into `buf` (must be ≥ kLanAdSize).  Returns bytes
// written (always kLanAdSize).  Fields longer than the wire slot are
// truncated; callers should enforce limits in the UI.
size_t EncodeLanAd(const LanAd& in, uint8_t* buf, size_t bufLen);

// Parse `buf` (expected kLanAdSize bytes).  Returns true on success.
bool DecodeLanAd(const uint8_t* buf, size_t bufLen, LanAd& out);

class LanDiscovery {
public:
	LanDiscovery();
	~LanDiscovery();

	LanDiscovery(const LanDiscovery&) = delete;
	LanDiscovery& operator=(const LanDiscovery&) = delete;

	// Bind the broadcast socket on INADDR_ANY/ephemeral and the
	// per-LAN listen port (so peers can respond by sending to that
	// port if they want).  Returns false on failure; call LastError()
	// for details.
	bool Begin(uint16_t listenPort);

	// Stop advertising (if any) and close the socket.
	void Stop();

	// Idempotent.  Kick off periodic 1 Hz broadcast of `ad` on the
	// configured `listenPort`.  Stop with StopAdvertising().
	void Advertise(const LanAd& ad);
	void StopAdvertising();

	// Drive the socket.  Call from the main loop ~10 Hz.  Drains
	// any received ads into `out`.  Emits heartbeats when the last
	// send was ≥1 s ago.
	void Poll(uint64_t nowMs, std::vector<LanAd>& out);

	const char* LastError() const { return mLastError.c_str(); }
	uint16_t ListenPort() const   { return mListenPort; }

private:
	// Opaque socket handle (intptr_t to match Transport).
	intptr_t    mSock;
	uint16_t    mListenPort;
	bool        mAdvertising;
	LanAd       mAd;
	uint64_t    mLastSendMs;
	std::string mLastError;
};

} // namespace ATNetplay
