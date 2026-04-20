//	AltirraSDL - Online Play action helpers (impl)

#include <stdafx.h>

#include "ui_netplay_actions.h"
#include "ui_netplay_state.h"
#include "ui_netplay_lobby_worker.h"
#include "ui_netplay.h"

#include "netplay/netplay_glue.h"
#include "netplay/lobby_config.h"
#include "netplay/netplay_simhash.h"
#include "netplay/packets.h"

#include "ui/gamelibrary/game_library.h"
#include "ui/core/ui_main.h"

#include "simulator.h"
#include "firmwaremanager.h"
#include "constants.h"
#include "cpu.h"
#include "uiaccessors.h"  // ATUISwitchHardwareMode

#include <at/atio/image.h>       // ATStateLoadContext
#include <at/atcore/serializable.h>
#include <at/atcore/media.h>     // kATMediaWriteMode_RO

#include "savestateio.h"

#include <at/atcore/logging.h>

extern ATLogChannel g_ATLCNetplay;

// Forward decl for accessor defined in ui_system_pages_b.cpp / etc —
// no shared header, but the signature is consistent across uses.
extern void ATUIUpdateSpeedTiming();

#include <vd2/system/file.h>
#include <vd2/system/registry.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/refcount.h>
#include <vd2/system/zip.h>

extern ATSimulator g_sim;
extern VDStringA ATGetConfigDir();

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ATNetplayUI {

// Defined in ui_netplay.cpp.
LobbyWorker& GetWorker();

// Shared Game Library singleton for Add-Offer pickers.
ATGameLibrary& LibrarySingleton() {
	static ATGameLibrary lib;
	static bool initialised = false;
	if (!initialised) {
		lib.SetConfigDir(ATGetConfigDir());
		lib.LoadSettingsFromRegistry();
		lib.LoadCache();
		initialised = true;
	}
	return lib;
}

namespace {

// Pick the first enabled HTTP lobby from the user's lobby.ini (or
// the built-in defaults if the file is missing).  Returns an endpoint
// with host="" on failure.
ATNetplay::LobbyEndpoint FirstHttpLobby(std::string& sectionOut) {
	const auto& entries = GetConfiguredLobbies();
	for (const auto& e : entries) {
		if (!e.enabled) continue;
		if (e.kind != ATNetplay::LobbyKind::Http) continue;
		ATNetplay::LobbyEndpoint ep;
		std::string url = e.url;
		const std::string prefix = "http://";
		if (url.compare(0, prefix.size(), prefix) == 0)
			url.erase(0, prefix.size());
		size_t slash = url.find('/');
		if (slash != std::string::npos) url.resize(slash);
		size_t colon = url.rfind(':');
		if (colon != std::string::npos) {
			ep.host.assign(url, 0, colon);
			ep.port = (uint16_t)std::atoi(url.c_str() + colon + 1);
		} else {
			ep.host = url;
			ep.port = 80;
		}
		ep.timeoutMs = 5000;
		sectionOut = e.section;
		return ep;
	}
	return ATNetplay::LobbyEndpoint{};
}

// 16-byte SDBM/FNV-style fold of the cleartext entry code.  Matches
// the previous single-session code.  Returns nullptr (and leaves hash
// buffer untouched) if the string is empty or the offer isn't private.
const uint8_t* FoldEntryCode(const HostedGame& o, uint8_t (&hash)[16]) {
	if (!o.isPrivate || o.entryCode.empty()) return nullptr;
	uint64_t h1 = 1469598103934665603ull;
	uint64_t h2 = 0x9E3779B185EBCA87ull;
	for (unsigned char c : o.entryCode) {
		h1 = (h1 ^ c) * 1099511628211ull;
		h2 = (h2 * 31ull) + c;
	}
	for (int i = 0; i < 8; ++i) {
		hash[i]     = (uint8_t)((h1 >> (i * 8)) & 0xFF);
		hash[i + 8] = (uint8_t)((h2 >> (i * 8)) & 0xFF);
	}
	return hash;
}

// Translate glue Phase → HostedGameState for the UI.
HostedGameState PhaseToHostedGameState(ATNetplayGlue::Phase p, bool enabled,
                             HostedGameState current) {
	using P = ATNetplayGlue::Phase;
	switch (p) {
		case P::None:              return enabled ? current : HostedGameState::Draft;
		case P::Idle:              return HostedGameState::Draft;
		case P::WaitingForJoiner:  return HostedGameState::Listed;
		case P::Handshaking:
		case P::SendingSnapshot:
		case P::ReceivingSnapshot:
		case P::SnapshotReady:     return HostedGameState::Handshaking;
		case P::Lockstepping:      return HostedGameState::Playing;
		case P::Ended:             return enabled ? HostedGameState::Listed : HostedGameState::Draft;
		case P::Desynced:
		case P::Failed:            return HostedGameState::Failed;
	}
	return current;
}

// Post a Create request for one offer.  Fills lobbySessionId on the
// result path (see ATNetplayUI_Poll).
void PostLobbyCreate(HostedGame& o) {
	std::string section;
	ATNetplay::LobbyEndpoint ep = FirstHttpLobby(section);
	if (ep.host.empty()) {
		g_ATLCNetplay("lobby Create skipped for \"%s\": "
			"no HTTP lobby endpoint configured",
			o.gameName.c_str());
		return;
	}
	g_ATLCNetplay("lobby Create posting for \"%s\" → %s:%u (section \"%s\")",
		o.gameName.c_str(), ep.host.c_str(), (unsigned)ep.port,
		section.c_str());

	LobbyRequest req{};
	req.op       = LobbyOp::Create;
	req.endpoint = ep;

	ATNetplay::LobbyCreateRequest cr;
	cr.cartName        = o.gameName;
	cr.hostHandle      = ResolvedNickname();
	char epb[48];
	std::snprintf(epb, sizeof epb, "127.0.0.1:%u", (unsigned)o.boundPort);
	cr.hostEndpoint    = epb;
	cr.region          = "global";
	cr.playerCount     = 1;
	cr.maxPlayers      = 2;
	cr.protocolVersion = 2;
	cr.visibility      = o.isPrivate ? "private" : "public";
	cr.requiresCode    = o.isPrivate;
	cr.cartArtHash     = o.cartArtHash;

	req.createReq = cr;

	// Tag the lobby result so ATNetplayUI_Poll can find the offer.
	// We stash the first 8 bytes of the gameId in req.tag by hashing.
	uint32_t tag = 0;
	for (unsigned char c : o.id) tag = tag * 31u + c;
	req.tag = tag ? tag : 1;
	GetWorker().Post(std::move(req), section);
	o.lastHeartbeatMs = 0;  // arm first heartbeat
}

// Post a Delete request for one offer.  Clears local session id + token.
void PostLobbyDelete(HostedGame& o) {
	if (!o.lobbySessionId.empty() && !o.lobbyToken.empty()) {
		std::string section;
		ATNetplay::LobbyEndpoint ep = FirstHttpLobby(section);
		if (!ep.host.empty()) {
			LobbyRequest req{};
			req.op        = LobbyOp::Delete;
			req.endpoint  = ep;
			req.sessionId = o.lobbySessionId;
			req.token     = o.lobbyToken;
			GetWorker().Post(std::move(req), section);
		}
	}
	o.lobbySessionId.clear();
	o.lobbyToken.clear();
}

// Kick off an actual host coordinator for this offer, binding an
// ephemeral port and posting Create afterwards.  Idempotent relative
// to an already-running coordinator.
// Constant master seed for locked-RNG netplay; both peers reseed at
// the lockstep-entry edge in netplay_glue.cpp.
constexpr uint32_t kNetplayMasterSeed = 0xA7C0BEEFu;

// Extract the file extension from a UTF-8 path into a NUL-padded
// 8-byte field (with the leading dot kept for ATSimulator::Load's
// content-sniff via path extension).
void ExtractExtensionInto(const std::string& path, char out[8]) {
	std::memset(out, 0, 8);
	size_t dot = path.find_last_of('.');
	if (dot == std::string::npos) return;
	std::string ext = path.substr(dot);
	size_t n = ext.size();
	if (n > 7) n = 7;
	std::memcpy(out, ext.data(), n);
}

// Compute CRC32 over the bytes of a file on disk.  Returns 0 on I/O
// failure or if the file is empty / implausibly large.  Used both at
// StartHost time (to stamp BootConfig.gameFileCRC32 for the joiner's
// cache lookup) and when hashing firmware ROMs elsewhere.
static uint32_t CRC32OfFile(const std::string& path) {
	if (path.empty()) return 0;
	try {
		VDFileStream fs(VDTextU8ToW(path.c_str(), -1).c_str(),
			nsVDFile::kRead | nsVDFile::kOpenExisting | nsVDFile::kDenyNone);
		sint64 sz = fs.Length();
		if (sz <= 0 || sz > 32 * 1024 * 1024) return 0;
		std::vector<uint8_t> buf((size_t)sz);
		fs.Read(buf.data(), (long)buf.size());
		return VDCRCTable::CRC32.CRC(buf.data(), buf.size());
	} catch (...) {
		return 0;
	}
}

// Build a NetBootConfig from a HostedGame's MachineConfig.
ATNetplay::NetBootConfig BuildBootConfig(const HostedGame& o) {
	ATNetplay::NetBootConfig bc{};
	bc.hardwareMode    = (uint8_t)o.config.hardwareMode;
	bc.memoryMode      = (uint8_t)o.config.memoryMode;
	bc.videoStandard   = (uint8_t)o.config.videoStandard;
	bc.basicEnabled    = o.config.basicEnabled ? 1 : 0;
	bc.cpuMode         = (uint8_t)o.config.cpuMode;
	bc.sioAcceleration = o.config.sioPatchEnabled ? 1 : 0;
	bc.kernelCRC32     = o.config.kernelCRC32;
	bc.basicCRC32      = o.config.basicCRC32;
	bc.masterSeed      = kNetplayMasterSeed;
	bc.bootFrames      = 0;
	bc.gameFileCRC32   = CRC32OfFile(o.gamePath);
	ExtractExtensionInto(o.gamePath, bc.gameExtension);
	return bc;
}

void StartCoordForHostedGame(HostedGame& o) {
	if (ATNetplayGlue::HostExists(o.id.c_str())) return;

	uint8_t codeHash[16];
	const uint8_t* codePtr = FoldEntryCode(o, codeHash);

	ATNetplay::NetBootConfig bc = BuildBootConfig(o);

	bool ok = ATNetplayGlue::StartHost(
		o.id.c_str(),
		/*localPort*/        0,
		/*playerHandle*/     ResolvedNickname().c_str(),
		/*cartName*/         o.gameName.c_str(),
		/*osRomHash*/        0,
		/*basicRomHash*/     0,
		/*settingsHash*/     0,
		/*inputDelayFrames*/ (uint16_t)GetState().prefs.defaultInputDelayInternet,
		/*entryCodeHash*/    codePtr,
		/*bootConfig*/       bc);
	if (!ok) {
		const char* err = ATNetplayGlue::HostLastError(o.id.c_str());
		o.lastError = (err && *err) ? err : "Failed to start host";
		o.state     = HostedGameState::Failed;
		return;
	}
	o.boundPort = ATNetplayGlue::HostBoundPort(o.id.c_str());
	o.lastError.clear();
	o.state = HostedGameState::Listed;

	PostLobbyCreate(o);
}

void StopCoordForHostedGame(HostedGame& o) {
	PostLobbyDelete(o);
	ATNetplayGlue::StopHost(o.id.c_str());
	o.boundPort = 0;
}

// -------------------------------------------------------------------
// Activity state machine — derived each frame.
// -------------------------------------------------------------------

// True iff any of our hostedGames is in a live-play coordinator phase.
bool AnyHostedGameInSession() {
	for (auto& o : GetState().hostedGames) {
		using P = ATNetplayGlue::Phase;
		P p = ATNetplayGlue::HostPhase(o.id.c_str());
		if (p == P::Handshaking || p == P::SendingSnapshot ||
		    p == P::ReceivingSnapshot || p == P::SnapshotReady ||
		    p == P::Lockstepping) return true;
	}
	return false;
}

// True iff we joined someone's session.
bool JoinerInSession() {
	using P = ATNetplayGlue::Phase;
	P p = ATNetplayGlue::JoinPhase();
	return p == P::Handshaking || p == P::SendingSnapshot ||
	       p == P::ReceivingSnapshot || p == P::SnapshotReady ||
	       p == P::Lockstepping;
}

} // anonymous

// -------------------------------------------------------------------
// Public
// -------------------------------------------------------------------

void EnableHostedGame(const std::string& gameId) {
	HostedGame* o = FindHostedGame(gameId);
	if (!o) return;
	o->enabled = true;
	SaveToRegistry();
	// The reconcile loop will actually start the coordinator on the
	// next tick (or immediately via below path if caller polls now).
}

void DisableHostedGame(const std::string& gameId) {
	HostedGame* o = FindHostedGame(gameId);
	if (!o) return;
	o->enabled = false;
	StopCoordForHostedGame(*o);
	o->state = HostedGameState::Draft;
	SaveToRegistry();
}

void RemoveHostedGame(const std::string& gameId) {
	HostedGame* o = FindHostedGame(gameId);
	if (!o) return;
	StopCoordForHostedGame(*o);
	auto& v = GetState().hostedGames;
	v.erase(std::remove_if(v.begin(), v.end(),
		[&](const HostedGame& x){ return x.id == gameId; }),
		v.end());
	SaveToRegistry();
}

void ReconcileHostedGames(uint64_t nowMs) {
	State& st = GetState();

	// 1. Compute UserActivity from coordinator phases.
	UserActivity prev = st.activity;
	if (AnyHostedGameInSession() || JoinerInSession()) {
		st.activity = UserActivity::InSession;
	} else if (st.activity == UserActivity::InSession) {
		// Session just ended.  Drop back to Idle (or PlayingLocal if
		// a local-play event fires separately via
		// ActivityTrack_OnLocalPlayStart).
		st.activity = UserActivity::Idle;
	}

	// Session-end edge: if we were InSession last tick and aren't now,
	// put the user's pre-session emulator state back.  Held as a full
	// in-memory savestate so everything (mounted media, per-session
	// sim tweaks, running game, paused/running, etc.) comes back
	// exactly as they left it.  The snapshot was taken in
	// kATDeferred_NetplayHostBoot / joiner flow before the preset was
	// applied — see SaveSessionRestorePoint.
	if (prev == UserActivity::InSession &&
	    st.activity != UserActivity::InSession &&
	    HasSessionRestorePoint()) {
		RestoreSessionRestorePoint();
	}

	// 2. Per-offer reconciliation: sync coordinator / lobby to the
	//    (activity, enabled) tuple.
	for (auto& o : st.hostedGames) {
		using P = ATNetplayGlue::Phase;
		P p = ATNetplayGlue::HostPhase(o.id.c_str());
		bool coordRunning = (p != P::None);

		// Mirror phase → offer state.
		o.state = PhaseToHostedGameState(p, o.enabled, o.state);

		// If this offer is *in* a session, it MUST stay listed.
		bool thisInSession = (p == P::Handshaking ||
		                      p == P::SendingSnapshot ||
		                      p == P::ReceivingSnapshot ||
		                      p == P::SnapshotReady ||
		                      p == P::Lockstepping);

		// Edge detection on lastPhase → current phase.  First time we
		// see a non-Waiting non-None phase, a peer has connected:
		// queue up the boot+snapshot chain, fire a notification, and
		// remove the offer from the lobby (so third-party browsers
		// don't see it as joinable).
		uint8_t newPhase = (uint8_t)p;
		if (newPhase != o.lastPhase) {
			bool nowHandshake =
				newPhase == (uint8_t)P::Handshaking ||
				newPhase == (uint8_t)P::SendingSnapshot;
			// Edge: coord just transitioned into a session phase.  The
			// snapshotQueued flag is our idempotency guard — we only
			// want to boot+serialise ONCE per session.  We used to also
			// gate on `lastPhase == WaitingForJoiner`, but that missed
			// fast transitions where Poll() drains multiple packets
			// before ReconcileHostedGames sees the coord, skipping straight
			// to SendingSnapshot.
			if (nowHandshake && !o.snapshotQueued) {
				o.snapshotQueued = true;
				// Boot the offer's image first so CreateSnapshot
				// captures a running session.  The activity hook
				// sees InSession and suppresses the PlayingLocal
				// flip on the resulting EXELoad.
				if (!o.gamePath.empty()) {
					// Use the netplay-specific boot path (no compat
					// dialog gate; always Resume).  Leaving the sim
					// paused would capture a paused state into the
					// snapshot and both sides would show a frozen
					// screen after Lockstepping.  path1 = gameId,
					// path2 = gamePath so the handler can surface
					// failures back to the offer row.
					ATUIPushDeferred2(kATDeferred_NetplayHostBoot,
						o.id.c_str(), o.gamePath.c_str());
				}
				// Then serialise + submit to the coordinator.  The
				// path slot carries the offer id (UTF-8).
				ATUIPushDeferred(kATDeferred_NetplayHostSnapshot,
					o.id.c_str(), 0);

				// Remove this offer from the lobby so additional
				// browsers won't see it.  Keep the coordinator
				// running — the connected peer talks to it on the
				// bound UDP port directly.
				PostLobbyDelete(o);

				char msg[160];
				std::snprintf(msg, sizeof msg,
					"A peer is connecting to %s…",
					o.gameName.c_str());
				ATNetplayUI_Notify("Peer connecting", msg);
			}

			// Edge: host coord just entered Lockstepping.  The host
			// sim has been paused since ColdReset inside
			// kATDeferred_NetplayHostBoot so that the snapshot
			// captured for the joiner represents the same frame
			// the host is currently on.  Resume now — from here
			// on the lockstep gate in main_sdl3.cpp drives the
			// sim in lockstep with the peer.
			if (newPhase == (uint8_t)P::Lockstepping &&
			    o.lastPhase != (uint8_t)P::Lockstepping) {
				g_sim.Resume();
			}

			// Clear the queue flag when returning to a pre-session
			// phase so the next session fires boot+snapshot again.
			if (!thisInSession) {
				o.snapshotQueued = false;
			}
			o.lastPhase = newPhase;
		}

		// Decide desired state.
		bool wantRunning = o.enabled &&
			(st.activity == UserActivity::Idle || thisInSession);

		if (wantRunning && !coordRunning) {
			StartCoordForHostedGame(o);
		} else if (!wantRunning && coordRunning && !thisInSession) {
			StopCoordForHostedGame(o);
			o.state = HostedGameState::Suspended;
		}

		// Periodic heartbeat while coordinator is running.
		if (coordRunning && !o.lobbySessionId.empty()
		    && !o.lobbyToken.empty()) {
			const uint64_t kHeartbeatMs = 30000;
			if (o.lastHeartbeatMs == 0
			    || nowMs - o.lastHeartbeatMs >= kHeartbeatMs) {
				std::string section;
				ATNetplay::LobbyEndpoint ep = FirstHttpLobby(section);
				if (!ep.host.empty()) {
					LobbyRequest req{};
					req.op          = LobbyOp::Heartbeat;
					req.endpoint    = ep;
					req.sessionId   = o.lobbySessionId;
					req.token       = o.lobbyToken;
					req.playerCount = (p == P::Lockstepping) ? 2 : 1;
					GetWorker().Post(std::move(req), section);
				}
				o.lastHeartbeatMs = nowMs;
			}
		}
	}
}

void ActivityTrack_OnLocalPlayStart() {
	State& st = GetState();
	if (st.activity == UserActivity::InSession) return;
	st.activity = UserActivity::PlayingLocal;
}

void ActivityTrack_OnLocalPlayStop() {
	State& st = GetState();
	if (st.activity == UserActivity::PlayingLocal)
		st.activity = UserActivity::Idle;
}

// -------------------------------------------------------------------
// Join (unchanged single-session semantics)
// -------------------------------------------------------------------

void StartHostingAction() {
	State& st = GetState();
	if (st.session.pendingCartName.empty()) {
		st.session.lastError =
			"No game selected.  Open Host Games to add one.";
		Navigate(Screen::Error);
		return;
	}

	// Find or create an offer for this cart.  We de-dup by
	// (gamePath, isPrivate, entryCode) so the same cart added twice
	// doesn't produce duplicate lobby listings.
	HostedGame* existing = nullptr;
	for (auto& o : st.hostedGames) {
		if (o.gamePath == st.session.pendingCartPath
		    && o.isPrivate == st.session.hostingPrivate
		    && o.entryCode == st.session.hostingEntryCode) {
			existing = &o; break;
		}
	}

	std::string id;
	if (existing) {
		id = existing->id;
		existing->enabled = true;
	} else {
		if (st.hostedGames.size() >= State::kMaxHostedGames) {
			st.session.lastError = "Too many hostedGames — remove one first.";
			Navigate(Screen::Error);
			return;
		}
		HostedGame o;
		o.id           = GenerateHostedGameId();
		o.gamePath     = st.session.pendingCartPath;
		o.gameName     = st.session.pendingCartName;
		o.cartArtHash  = st.session.pendingCartArtHash;
		o.isPrivate    = st.session.hostingPrivate;
		o.entryCode    = st.session.hostingEntryCode;
		o.enabled      = true;
		o.state        = HostedGameState::Draft;
		st.hostedGames.push_back(std::move(o));
		id = st.hostedGames.back().id;
	}
	SaveToRegistry();
	EnableHostedGame(id);
	Navigate(Screen::MyHostedGames);
}

void StopHostingAction() {
	// Stop the joiner if any.
	ATNetplayGlue::StopJoin();
	// Disable every offer (user can re-enable from My Hosted Games).
	for (auto& o : GetState().hostedGames) {
		DisableHostedGame(o.id);
	}
}

void SubmitHostGameFileForGame(const char *gameId) {
	if (!gameId || !*gameId) return;
	HostedGame* o = FindHostedGame(gameId);
	if (!o) return;
	if (!ATNetplayGlue::HostExists(gameId)) return;

	// Read the game file bytes straight off disk (no savestate).  The
	// joiner will cold-boot from these bytes + our BootConfig.
	try {
		VDFileStream fs(VDTextU8ToW(o->gamePath.c_str(), -1).c_str(),
			nsVDFile::kRead | nsVDFile::kOpenExisting | nsVDFile::kDenyNone);
		sint64 sz = fs.Length();
		if (sz <= 0 || sz > 32 * 1024 * 1024) {
			o->lastError = "game file is empty or too large";
			return;
		}
		std::vector<uint8_t> bytes((size_t)sz);
		fs.Read(bytes.data(), (long)bytes.size());

		uint32_t crc = VDCRCTable::CRC32.CRC(bytes.data(), bytes.size());
		g_ATLCNetplay("host: shipping game file \"%s\" (%zu bytes, "
			"CRC32=%08X) for \"%s\"",
			o->gamePath.c_str(), bytes.size(), crc, gameId);

		ATNetplayGlue::SubmitHostSnapshot(gameId,
			bytes.data(), bytes.size());
	} catch (const MyError& e) {
		o->lastError = std::string("read game file failed: ") + e.c_str();
	} catch (...) {
		o->lastError = "read game file failed (unknown)";
	}
}

void StartJoiningAction() {
	State& st = GetState();
	if (ATNetplayGlue::IsActive()) {
		Navigate(Screen::Waiting);
		return;
	}

	uint8_t codeHash[16] = {};
	const uint8_t* codePtr = nullptr;
	if (!st.session.joinEntryCode.empty()) {
		uint64_t h1 = 1469598103934665603ull;
		uint64_t h2 = 0x9E3779B185EBCA87ull;
		for (unsigned char c : st.session.joinEntryCode) {
			h1 = (h1 ^ c) * 1099511628211ull;
			h2 = (h2 * 31ull) + c;
		}
		for (int i = 0; i < 8; ++i) {
			codeHash[i]     = (uint8_t)((h1 >> (i * 8)) & 0xFF);
			codeHash[i + 8] = (uint8_t)((h2 >> (i * 8)) & 0xFF);
		}
		codePtr = codeHash;
	}

	bool ok = ATNetplayGlue::StartJoin(
		st.session.joinTarget.hostEndpoint.c_str(),
		ResolvedNickname().c_str(),
		/*osRomHash*/    0,
		/*basicRomHash*/ 0,
		/*acceptTos*/    true,
		codePtr);
	if (!ok) {
		const char *err = ATNetplayGlue::JoinLastError();
		st.session.lastError = (err && *err) ? err
			: "Failed to join the session.";
		Navigate(Screen::Error);
		return;
	}
	Navigate(Screen::Waiting);
}

// -------------------------------------------------------------------
// Machine-preset apply + session restore-point
// -------------------------------------------------------------------

namespace {

// Full in-memory savestate of the user's pre-session simulator state.
// Populated by SaveSessionRestorePoint; consumed (and cleared) by
// RestoreSessionRestorePoint.  We keep two refs: the state proper
// plus its sidecar info object (the same pair CreateSnapshot returns).
vdrefptr<IATSerializable> g_restoreSnap;
vdrefptr<IATSerializable> g_restoreSnapInfo;

} // anonymous

bool HasSessionRestorePoint() { return g_restoreSnap != nullptr; }

bool SaveSessionRestorePoint() {
	g_restoreSnap     = nullptr;
	g_restoreSnapInfo = nullptr;
	try {
		g_sim.CreateSnapshot(~g_restoreSnap, ~g_restoreSnapInfo);
	} catch (...) {
		g_restoreSnap     = nullptr;
		g_restoreSnapInfo = nullptr;
		return false;
	}
	return g_restoreSnap != nullptr;
}

void RestoreSessionRestorePoint() {
	if (!g_restoreSnap) return;
	try {
		ATStateLoadContext ctx {};
		// Be permissive — the user's pre-session firmware / hardware
		// will match by construction (we just snapshotted it), but
		// ApplySnapshot sometimes flags advisory mismatches.
		ctx.mbAllowKernelMismatch = true;
		g_sim.ApplySnapshot(*g_restoreSnap, &ctx);
		g_sim.Resume();
	} catch (...) {
		// Best-effort; don't blow up the UI if restore fails.
	}
	g_restoreSnap     = nullptr;
	g_restoreSnapInfo = nullptr;
}

namespace {

// Look up a firmware by CRC32 under the kernel-family type filter.
// Returns 0 if no match.  Uses the ATFirmwareManager "[XXXXXXXX]"
// ref-string path (firmwaremanager.cpp:605-626).
uint64 FindKernelByCRC(ATFirmwareManager& fwm, uint32_t crc32) {
	if (crc32 == 0) return 0;
	wchar_t ref[16];
	swprintf(ref, 16, L"[%08X]", crc32);
	return fwm.GetFirmwareByRefString(ref,
		[](ATFirmwareType t) {
			return ATIsKernelFirmwareType(t);
		});
}

uint64 FindBasicByCRC(ATFirmwareManager& fwm, uint32_t crc32) {
	if (crc32 == 0) return 0;
	wchar_t ref[16];
	swprintf(ref, 16, L"[%08X]", crc32);
	return fwm.GetFirmwareByRefString(ref,
		[](ATFirmwareType t) { return t == kATFirmwareType_Basic; });
}

} // anonymous

std::string ApplyMachineConfig(const MachineConfig& cfg) {
	ATFirmwareManager *fwm = g_sim.GetFirmwareManager();
	if (!fwm) return "firmware manager unavailable";

	// Resolve firmware by CRC32 first so we fail fast before touching
	// hardware state.  Zero CRC = "use Altirra's default for this
	// hardware mode" — we resolve that via GetFirmwareOfType after
	// the hardware switch.
	uint64 kernelId = FindKernelByCRC(*fwm, cfg.kernelCRC32);
	if (cfg.kernelCRC32 != 0 && kernelId == 0) {
		char buf[96];
		std::snprintf(buf, sizeof buf,
			"OS firmware with CRC32 %08X is not installed",
			cfg.kernelCRC32);
		return buf;
	}
	uint64 basicId = FindBasicByCRC(*fwm, cfg.basicCRC32);
	if (cfg.basicCRC32 != 0 && basicId == 0) {
		char buf[96];
		std::snprintf(buf, sizeof buf,
			"BASIC firmware with CRC32 %08X is not installed",
			cfg.basicCRC32);
		return buf;
	}

	// Hardware first — ATUISwitchHardwareMode resets per-hardware
	// defaults (memory mode, kernel etc.) so everything else must be
	// set afterwards.
	if (g_sim.GetHardwareMode() != cfg.hardwareMode) {
		if (!ATUISwitchHardwareMode(nullptr, cfg.hardwareMode,
				/*switchProfile*/ true)) {
			return "could not switch hardware mode";
		}
	}

	g_sim.SetMemoryMode(cfg.memoryMode);
	g_sim.SetVideoStandard(cfg.videoStandard);
	g_sim.SetCPUMode(cfg.cpuMode, g_sim.GetCPUSubCycles());
	ATUIUpdateSpeedTiming();

	// If CRC was 0 (unset), fall back to the hardware's default
	// firmware type.  This mirrors what ATUISwitchHardwareMode does
	// internally but is explicit for documentation.
	if (kernelId == 0 && cfg.kernelCRC32 == 0) {
		ATFirmwareType defType;
		switch (cfg.hardwareMode) {
			case kATHardwareMode_5200:   defType = kATFirmwareType_Kernel5200; break;
			case kATHardwareMode_800:    defType = kATFirmwareType_Kernel800_OSB; break;
			case kATHardwareMode_XEGS:   defType = kATFirmwareType_KernelXEGS; break;
			case kATHardwareMode_1200XL: defType = kATFirmwareType_Kernel1200XL; break;
			default:                     defType = kATFirmwareType_KernelXL; break;
		}
		kernelId = fwm->GetFirmwareOfType(defType, true);
	}
	if (kernelId) g_sim.SetKernel(kernelId);

	if (basicId == 0 && cfg.basicCRC32 == 0) {
		basicId = fwm->GetFirmwareOfType(kATFirmwareType_Basic, true);
	}
	if (basicId) g_sim.SetBasic(basicId);

	g_sim.SetBASICEnabled(cfg.basicEnabled);
	g_sim.SetSIOPatchEnabled(cfg.sioPatchEnabled);

	return {};
}

} // namespace ATNetplayUI
