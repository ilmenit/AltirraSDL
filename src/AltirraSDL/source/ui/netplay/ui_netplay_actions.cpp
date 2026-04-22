//	AltirraSDL - Online Play action helpers (impl)

#include <stdafx.h>

#include "ui_netplay_actions.h"
#include "ui_netplay_state.h"
#include "ui_netplay_lobby_worker.h"
#include "ui_netplay.h"
#include "ui_netplay_widgets.h"

#include "netplay/netplay_glue.h"
#include "netplay/lobby_config.h"
#include "netplay/lobby_protocol.h"
#include "netplay/netplay_simhash.h"
#include "netplay/packets.h"

#include "ui/gamelibrary/game_library.h"
#include "ui/core/ui_main.h"
#include "ui/mobile/mobile_internal.h"   // GetGameLibrary, GameBrowser_Init
#include "ui/mobile/ui_mobile.h"        // ATMobileUIState, ATMobileUIScreen

#include "simulator.h"
#include "firmwaremanager.h"
#include "constants.h"
#include "cpu.h"
#include "uiaccessors.h"  // ATUISwitchHardwareMode

#include <at/atio/image.h>       // ATStateLoadContext
#include <at/atcore/serializable.h>
#include <at/atcore/media.h>     // kATMediaWriteMode_RO
#include <at/atcore/vfs.h>       // ATVFSOpenFileView for zip:// paths

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
#include <vd2/system/date.h>

extern ATSimulator g_sim;
extern VDStringA ATGetConfigDir();
extern ATMobileUIState g_mobileState;

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ATNetplayUI {

// Defined in ui_netplay.cpp.
LobbyWorker& GetWorker();

// Shared Game Library singleton for Add-Offer pickers.
//
// Prefer the mobile Game Browser's instance (created by
// GameBrowser_Init) — it's the one that's been populated by a
// background scan and is the authoritative source for cover art,
// variants, and play history.  If the mobile browser hasn't been
// initialised yet (e.g. Desktop mode at first launch; the Online Play
// picker is opened before the user ever visits the Game Library),
// kick GameBrowser_Init so the library scans and seeds the shared
// instance.  Only fall back to a local LoadCache-only singleton if
// that initialisation fails — this fallback exists purely as a
// belt-and-braces safety net; in practice GameBrowser_Init is
// reliable.
ATGameLibrary& LibrarySingleton() {
	if (ATGameLibrary *existing = GetGameLibrary())
		return *existing;
	GameBrowser_Init();
	if (ATGameLibrary *lib = GetGameLibrary())
		return *lib;

	// Fallback — cache-only, no background scan.  Reached only when
	// GameBrowser_Init refused to allocate (OOM on a very constrained
	// device); keeps the UI from crashing even when the library is
	// degenerate.
	static ATGameLibrary fallback;
	static bool initialised = false;
	if (!initialised) {
		fallback.SetConfigDir(ATGetConfigDir());
		fallback.LoadSettingsFromRegistry();
		fallback.LoadCache();
		initialised = true;
	}
	return fallback;
}

namespace {

// Parse "http://host:port[/...]" into a LobbyEndpoint.  Returns empty
// host on malformed input.
ATNetplay::LobbyEndpoint EndpointFromUrl(const std::string& urlIn) {
	ATNetplay::LobbyEndpoint ep;
	std::string url = urlIn;
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
	return ep;
}

// Every enabled HTTP lobby from the user's lobby.ini.  Hosting fans
// out Create / Heartbeat / Delete across all of them so a game shows
// up on every lobby the user has configured.
struct EnabledHttpLobby {
	std::string                section;
	ATNetplay::LobbyEndpoint   endpoint;
};
std::vector<EnabledHttpLobby> AllEnabledHttpLobbies() {
	std::vector<EnabledHttpLobby> out;
	for (const auto& e : GetConfiguredLobbies()) {
		if (!e.enabled) continue;
		if (e.kind != ATNetplay::LobbyKind::Http) continue;
		ATNetplay::LobbyEndpoint ep = EndpointFromUrl(e.url);
		if (ep.host.empty()) continue;
		out.push_back({e.section, ep});
	}
	return out;
}

// Pick the first enabled HTTP lobby — used by paths that inherently
// target one lobby (e.g. Browse's per-lobby List is already fanned
// out by the worker; this is for single-shot callers).  Returns an
// endpoint with host="" on failure.
ATNetplay::LobbyEndpoint FirstHttpLobby(std::string& sectionOut) {
	auto all = AllEnabledHttpLobbies();
	if (all.empty()) return ATNetplay::LobbyEndpoint{};
	sectionOut = all.front().section;
	return all.front().endpoint;
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
		case P::None:              return enabled ? current : HostedGameState::Off;
		case P::Idle:              return HostedGameState::Off;
		case P::WaitingForJoiner:  return HostedGameState::Open;
		case P::Handshaking:
		case P::SendingSnapshot:
		case P::ReceivingSnapshot:
		case P::SnapshotReady:     return HostedGameState::Handshaking;
		case P::Lockstepping:      return HostedGameState::Playing;
		case P::Ended:             return enabled ? HostedGameState::Open : HostedGameState::Off;
		case P::Desynced:
		case P::Failed:            return HostedGameState::Failed;
	}
	return current;
}

// Shared by PostLobbyCreate: build the offer-stable tag that
// ATNetplayUI_Poll uses to route Create results back to the offer.
uint32_t OfferTag(const HostedGame& o) {
	uint32_t t = 0;
	for (unsigned char c : o.id) t = t * 31u + c;
	return t ? t : 1;
}

// Post a Create request for this offer to EVERY enabled HTTP lobby.
// Each Create result lands in ATNetplayUI_Poll, which appends a
// LobbyRegistration keyed by the sourceLobby (section name).  A
// lobby that fails (network error, 5xx) simply yields no
// registration — the other lobbies still accept the game.
void PostLobbyCreate(HostedGame& o) {
	auto lobbies = AllEnabledHttpLobbies();
	if (lobbies.empty()) {
		g_ATLCNetplay("lobby Create skipped for \"%s\": "
			"no HTTP lobbies configured in lobby.ini",
			o.gameName.c_str());
		return;
	}

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

	// v2: pre-flight firmware fields so joiners can colour-code the
	// Browser without round-tripping a handshake.  CRCs are 8-char
	// uppercase hex; empty means "no constraint" (joiner accepts any).
	auto hexCRC = [](uint32_t c, std::string& out) {
		if (!c) { out.clear(); return; }
		char buf[12];
		std::snprintf(buf, sizeof buf, "%08X", c);
		out = buf;
	};
	hexCRC(o.config.kernelCRC32, cr.kernelCRC32);
	hexCRC(o.config.basicCRC32,  cr.basicCRC32);
	switch (o.config.hardwareMode) {
		case kATHardwareMode_800:    cr.hardwareMode = "800";    break;
		case kATHardwareMode_800XL:  cr.hardwareMode = "800XL";  break;
		case kATHardwareMode_1200XL: cr.hardwareMode = "1200XL"; break;
		case kATHardwareMode_XEGS:   cr.hardwareMode = "XEGS";   break;
		case kATHardwareMode_5200:   cr.hardwareMode = "5200";   break;
		case kATHardwareMode_130XE:  cr.hardwareMode = "130XE";  break;
		case kATHardwareMode_1400XL: cr.hardwareMode = "1400XL"; break;
		default:                     cr.hardwareMode = "XL/XE";  break;
	}

	const uint32_t tag = OfferTag(o);

	for (const auto& L : lobbies) {
		g_ATLCNetplay("lobby Create posting for \"%s\" -> %s:%u "
			"(section \"%s\")",
			o.gameName.c_str(),
			L.endpoint.host.c_str(), (unsigned)L.endpoint.port,
			L.section.c_str());

		LobbyRequest req{};
		req.op        = LobbyOp::Create;
		req.endpoint  = L.endpoint;
		req.createReq = cr;
		req.tag       = tag;
		GetWorker().Post(std::move(req), L.section);
	}
	o.lastHeartbeatMs = 0;    // arm first heartbeat
	// Wipe stale registrations from a previous session; Create
	// responses will repopulate as they arrive.
	o.lobbyRegistrations.clear();
}

// Post a Delete for this offer to every lobby it was registered with,
// then clear the local state.  Lobbies that never accepted a Create
// (or whose Create response was lost) have no entry here and are
// skipped — the lobby's 90 s TTL will garbage-collect them.
void PostLobbyDelete(HostedGame& o) {
	auto lobbies = AllEnabledHttpLobbies();

	for (const auto& reg : o.lobbyRegistrations) {
		if (reg.sessionId.empty() || reg.token.empty()) continue;
		// Find the matching endpoint for this registration's section.
		// If the user removed the lobby from lobby.ini between
		// Create and Delete, we skip cleanly — the TTL handles it.
		const ATNetplay::LobbyEndpoint *ep = nullptr;
		for (const auto& L : lobbies) {
			if (L.section == reg.section) { ep = &L.endpoint; break; }
		}
		if (!ep) continue;

		LobbyRequest req{};
		req.op        = LobbyOp::Delete;
		req.endpoint  = *ep;
		req.sessionId = reg.sessionId;
		req.token     = reg.token;
		GetWorker().Post(std::move(req), reg.section);
	}
	o.lobbyRegistrations.clear();
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

// mtime stamp of the outer OS file behind a VFS path.  For plain
// paths this is the file itself; for zip:// and similar it's the
// outer archive.  Zero on failure (treated as "uncacheable").
static uint64_t OuterFileStamp(const std::string& vfsPath) {
	if (vfsPath.empty()) return 0;
	VDStringW wpath = VDTextU8ToW(vfsPath.c_str(), -1);
	VDStringW basePath, subPath;
	ATVFSProtocol proto = ATParseVFSPath(wpath.c_str(), basePath, subPath);
	const wchar_t *osPath = (proto == kATVFSProtocol_File)
		? wpath.c_str() : basePath.c_str();
	if (!osPath || !*osPath) return 0;
	try {
		VDFile f(osPath, nsVDFile::kRead | nsVDFile::kOpenExisting
			| nsVDFile::kDenyNone);
		return f.getLastWriteTime().mTicks;
	} catch (...) {
		return 0;
	}
}

// Compute CRC32 over the bytes of a hosted game file.  Uses the
// persisted CRC cache on HostedGame when the outer-file mtime stamp
// still matches, so relaunching or re-enabling a hosted ZIP entry
// doesn't re-decompress multi-MB archives.  Returns 0 on I/O failure
// or if the file is empty / implausibly large.
static uint32_t CRC32OfHostedGame(const HostedGame& o) {
	if (o.gamePath.empty()) return 0;

	uint64_t stamp = OuterFileStamp(o.gamePath);
	if (stamp != 0 && o.gameFileStamp == stamp && o.gameFileCRC32 != 0)
		return o.gameFileCRC32;

	try {
		vdrefptr<ATVFSFileView> view;
		ATVFSOpenFileView(VDTextU8ToW(o.gamePath.c_str(), -1).c_str(),
			false, ~view);
		if (!view) return 0;
		IVDRandomAccessStream& fs = view->GetStream();
		sint64 sz = fs.Length();
		if (sz <= 0 || sz > 32 * 1024 * 1024) return 0;
		fs.Seek(0);
		std::vector<uint8_t> buf((size_t)sz);
		fs.Read(buf.data(), (sint32)buf.size());
		uint32_t crc = VDCRCTable::CRC32.CRC(buf.data(), buf.size());
		if (stamp != 0) {
			o.gameFileCRC32 = crc;
			o.gameFileStamp = stamp;
		}
		return crc;
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
	bc.gameFileCRC32   = CRC32OfHostedGame(o);
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
	o.state = HostedGameState::Open;

	// Always prompt the host on incoming joins — the coordinator holds
	// every arriving Hello until the host clicks Allow / Deny in the
	// modal that ReconcileHostedGames raises.  Auto-accept was removed
	// because a peer should never be able to slip onto the user's
	// machine without an explicit confirmation.
	ATNetplayGlue::HostSetPromptAccept(o.id.c_str(), true);

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
	o->state = HostedGameState::Off;
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
		// In-app feedback so the user knows their prior emulator
		// state came back.  Platform notifications don't cover this
		// since nothing "arrived" — the session simply ended.
		PushToast("Session ended — your previous game was restored.",
			ToastSeverity::Info, 4000);
	}

	// Stamp the joiner-side "waiting since" clock on the None → non-None
	// edge; clear it when the joiner returns to None (or reaches a
	// terminal phase) so a retry starts fresh.
	{
		using P = ATNetplayGlue::Phase;
		const P jp = ATNetplayGlue::JoinPhase();
		const bool active = (jp != P::None && jp != P::Idle);
		if (active && st.session.joinStartedMs == 0) {
			st.session.joinStartedMs = nowMs;
		} else if (!active && st.session.joinStartedMs != 0) {
			st.session.joinStartedMs = 0;
		}
	}

	// 1b. Pending-accept queue — every tick we pull the full coord
	//     queue across all hosted games and mirror it into
	//     st.session.pendingRequests.  The UI renders one row per
	//     entry, each with its own arrival time for the "Requested Xs
	//     ago" counter.  Auto-decline, Allow, Deny are driven by the
	//     AcceptJoinPrompt screen; this loop only does mirroring.
	{
		std::vector<Session::PendingJoinRequest> next;
		for (auto& o : st.hostedGames) {
			const size_t n = ATNetplayGlue::HostPendingCount(o.id.c_str());
			for (size_t i = 0; i < n; ++i) {
				char handle[40] = {};
				uint64_t arrivedMs = 0;
				if (!ATNetplayGlue::HostPendingAt(o.id.c_str(), i,
						handle, sizeof handle, &arrivedMs)) continue;
				Session::PendingJoinRequest r;
				r.hostedGameId = o.id;
				r.gameName     = o.gameName;
				r.handle       = handle;
				r.arrivedMs    = arrivedMs;
				next.push_back(std::move(r));
			}
		}

		const bool wasEmpty = st.session.pendingRequests.empty();
		const bool isEmpty  = next.empty();

		// On any new entry (wasn't there last tick), fire a
		// per-request notification so an AFK host sees each arrival.
		if (!isEmpty) {
			for (const auto& r : next) {
				bool already = false;
				for (const auto& p : st.session.pendingRequests) {
					if (p.hostedGameId == r.hostedGameId
					    && p.handle       == r.handle
					    && p.arrivedMs    == r.arrivedMs) {
						already = true; break;
					}
				}
				if (already) continue;
				char msg[160];
				std::snprintf(msg, sizeof msg,
					"%s wants to join %s",
					r.handle.empty() ? "Someone" : r.handle.c_str(),
					r.gameName.c_str());
				ATNetplayUI_Notify("Join request", msg);
			}
		}

		st.session.pendingRequests = std::move(next);

		if (wasEmpty && !isEmpty) {
			// 0 → N: save the user's current context so a full reject
			// can put them back exactly where they were, pause the sim
			// so nothing else steals attention, and navigate to the
			// prompt screen (the Gaming-Mode overlay also renders on
			// top, but flipping screen here covers the Desktop build
			// and keeps the back-stack consistent).
			st.session.promptSavedScreen = st.screen;
			st.session.promptSavedMobile = (int)g_mobileState.currentScreen;
			st.session.promptSavedValid  = true;
			if (!g_sim.IsPaused()) {
				g_sim.Pause();
				st.session.promptPausedSim = true;
			} else {
				st.session.promptPausedSim = false;
			}
			Navigate(Screen::AcceptJoinPrompt);
		} else if (!wasEmpty && isEmpty) {
			// Last entry resolved — reject-all, joiner-cancel, accept,
			// or auto-decline timeout.  If we're still on the prompt
			// screen (Accept navigates away on its own), put the user
			// back where they were.  Otherwise just tear down our
			// saved-context bookkeeping; the Accept path is responsible
			// for RestoreSessionRestorePoint when its session ends.
			if (st.screen == Screen::AcceptJoinPrompt) {
				if (st.session.promptSavedValid) {
					st.screen = st.session.promptSavedScreen;
							g_mobileState.currentScreen =
						(ATMobileUIScreen)st.session.promptSavedMobile;
				} else {
					Back();
				}
			}
			if (st.session.promptPausedSim && g_sim.IsPaused()) {
				g_sim.Resume();
			}
			st.session.promptPausedSim = false;
			st.session.promptSavedValid = false;
		}
		// N → M with both non-empty: nothing to do; the prompt just
		// re-renders the refreshed list on its next frame.
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
		// Capture pre-update phase so the heartbeat block below can
		// detect "we just changed state" — without this, lastPhase
		// gets updated in this if-block and the later edge test
		// would always evaluate false.
		const uint8_t prevPhase = o.lastPhase;
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

				// v2: don't delete the listing — flip its state to
				// "playing" on the next heartbeat instead.  That keeps
				// the lobby visibly active (Browser greys it as
				// "In session" rather than hiding it) while still
				// preventing third parties from trying to connect.
				// The state transition is sent below in the heartbeat
				// fan-out.

				char msg[160];
				std::snprintf(msg, sizeof msg,
					"A peer is connecting to %s…",
					o.gameName.c_str());
				ATNetplayUI_Notify("Peer connecting", msg);

				// In-app toast so the host sees the event even when
				// no platform-notify backend is available (Android
				// foreground, Wayland without libnotify, etc.) and
				// even when they're buried in Settings / Game Library
				// and would otherwise miss the window-flash cue.
				char toast[192];
				std::snprintf(toast, sizeof toast,
					"Peer joined — switching to %s.",
					o.gameName.c_str());
				PushToast(toast, ToastSeverity::Success, 4000);
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

		// Coords in Ended/Desynced/Failed are *technically* alive (the
		// FindHost/HostExists check returns true) but the protocol is
		// terminal and the socket no longer accepts handshakes.  If we
		// still want this offer listed, recycle: tear the dead one
		// down and start a fresh coord.  Without this the lobby would
		// keep showing the row as "waiting" after a session ended,
		// but joiners hitting it would time out.
		const bool coordTerminal = coordRunning &&
			(p == P::Ended || p == P::Desynced || p == P::Failed);

		if (wantRunning && !coordRunning) {
			StartCoordForHostedGame(o);
		} else if (wantRunning && coordTerminal) {
			StopCoordForHostedGame(o);
			StartCoordForHostedGame(o);
		} else if (!wantRunning && coordRunning && !thisInSession) {
			StopCoordForHostedGame(o);
			o.state = HostedGameState::Paused;
		}

		// Always prompt on incoming joins — idempotent per-frame sync so
		// a coord that was started before the user flipped some future
		// setting still picks up the correct gate.
		if (coordRunning) {
			ATNetplayGlue::HostSetPromptAccept(o.id.c_str(), true);
		}

		// Periodic heartbeat to every lobby this offer is registered
		// with.  A slow/dead lobby here doesn't block the others
		// because each Heartbeat request is independent.
		if (coordRunning && !o.lobbyRegistrations.empty()) {
			const uint64_t kHeartbeatMs = 30000;
			// Edge: as soon as we transition into / out of a session
			// phase, send an immediate heartbeat with the new state so
			// the lobby's "in play" indicator updates promptly instead
			// of waiting up to 30 s for the next periodic tick.
			// Compare against prevPhase (lastPhase has already been
			// updated above to newPhase by the time we reach here).
			const bool stateEdge = (newPhase != prevPhase) &&
				(o.lastHeartbeatMs != 0);
			if (o.lastHeartbeatMs == 0
			    || nowMs - o.lastHeartbeatMs >= kHeartbeatMs
			    || stateEdge) {
				auto lobbies = AllEnabledHttpLobbies();
				const int playerCount = (p == P::Lockstepping) ? 2 : 1;
				const char *state = thisInSession
					? ATLobby::kStatePlaying : ATLobby::kStateWaiting;
				for (const auto& reg : o.lobbyRegistrations) {
					if (reg.sessionId.empty() || reg.token.empty())
						continue;
					const ATNetplay::LobbyEndpoint *ep = nullptr;
					for (const auto& L : lobbies) {
						if (L.section == reg.section) { ep = &L.endpoint; break; }
					}
					if (!ep) continue;
					LobbyRequest req{};
					req.op          = LobbyOp::Heartbeat;
					req.endpoint    = *ep;
					req.sessionId   = reg.sessionId;
					req.token       = reg.token;
					req.playerCount = playerCount;
					req.state       = state;
					GetWorker().Post(std::move(req), reg.section);
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
		o.state        = HostedGameState::Off;
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
	// Use ATVFSOpenFileView so zip://outer!inner virtual paths from the
	// Game Library ZIP scan resolve to the inner file, not a raw open.
	try {
		vdrefptr<ATVFSFileView> view;
		ATVFSOpenFileView(VDTextU8ToW(o->gamePath.c_str(), -1).c_str(),
			false, ~view);
		if (!view) {
			o->lastError = "cannot open game file";
			return;
		}
		IVDRandomAccessStream& fs = view->GetStream();
		sint64 sz = fs.Length();
		if (sz <= 0 || sz > 32 * 1024 * 1024) {
			o->lastError = "game file is empty or too large";
			return;
		}
		fs.Seek(0);
		std::vector<uint8_t> bytes((size_t)sz);
		fs.Read(bytes.data(), (sint32)bytes.size());

		uint32_t crc = VDCRCTable::CRC32.CRC(bytes.data(), bytes.size());

		// Refresh the CRC cache while we have the bytes in hand — this
		// is the one path that always reads the file end-to-end.
		uint64_t stamp = OuterFileStamp(o->gamePath);
		if (stamp != 0) {
			o->gameFileCRC32 = crc;
			o->gameFileStamp = stamp;
		}

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

JoinCompat CheckJoinCompat(const std::string& kernelHex,
                           const std::string& basicHex,
                           char *outMissingCRCHex) {
	if (outMissingCRCHex) outMissingCRCHex[0] = 0;

	auto parseHex = [](const std::string& s, uint32_t& out) -> bool {
		if (s.size() != 8) return false;
		uint32_t v = 0;
		for (char c : s) {
			v <<= 4;
			if      (c >= '0' && c <= '9') v |= (c - '0');
			else if (c >= 'a' && c <= 'f') v |= (c - 'a' + 10);
			else if (c >= 'A' && c <= 'F') v |= (c - 'A' + 10);
			else return false;
		}
		out = v;
		return true;
	};

	uint32_t kCrc = 0, bCrc = 0;
	const bool haveK = parseHex(kernelHex, kCrc);
	const bool haveB = parseHex(basicHex,  bCrc);

	// No constraint at all → host pre-dates v2 schema; we don't know.
	if (!haveK && basicHex.empty()) return JoinCompat::Unknown;

	ATFirmwareManager *fwm = g_sim.GetFirmwareManager();
	if (!fwm) return JoinCompat::Unknown;

	if (haveK) {
		uint64 id = FindKernelByCRC(*fwm, kCrc);
		if (!id) {
			if (outMissingCRCHex)
				std::snprintf(outMissingCRCHex, 9, "%08X", kCrc);
			return JoinCompat::MissingKernel;
		}
	}
	if (haveB) {
		uint64 id = FindBasicByCRC(*fwm, bCrc);
		if (!id) {
			if (outMissingCRCHex)
				std::snprintf(outMissingCRCHex, 9, "%08X", bCrc);
			return JoinCompat::MissingBasic;
		}
	}
	return JoinCompat::Compatible;
}

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
