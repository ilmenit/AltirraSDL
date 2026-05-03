//	AltirraSDL - Online Play action helpers (impl)

#include <stdafx.h>

#include "ui_netplay_actions.h"
#include "ui_netplay_state.h"
#include "ui_netplay_lobby_worker.h"
#include "ui_netplay.h"
#include "ui_netplay_widgets.h"

#include <SDL3/SDL.h>

#include "netplay/netplay_glue.h"
#include "netplay/netplay_profile.h"
#include "netplay/lobby_client.h"
#include "netplay/lobby_config.h"
#include "netplay/lobby_protocol.h"
#include "netplay/netplay_simhash.h"
#include "netplay/packets.h"
#include "netplay/nat_discovery.h"
#include "netplay/port_mapping.h"
#include "netplay/transport.h"

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

	// Build the candidate list: every usable LAN interface + loopback.
	// Server-reflexive (public) and router-mapped (NAT-PMP) endpoints
	// are added later by the lobby_worker against the target lobby's
	// reflector.
	//
	// We enumerate EVERY non-loopback, non-link-local IPv4 interface
	// rather than picking just the default-route one.  This matters
	// because:
	//   - A host with a VPN active has the VPN IP as the default
	//     route, but same-LAN joiners on the real WiFi can only
	//     reach the WiFi IP.  Publishing both gets everyone.
	//   - Tethered / dual-NIC setups (e.g. mobile hotspot + ethernet)
	//     similarly benefit from advertising all interfaces.
	//   - Android devices often have cellular + WiFi simultaneously;
	//     publishing both lets the joiner pick whichever is routable.
	std::vector<std::string> lanIps;
	ATNetplay::Transport::EnumerateLocalIPv4s(lanIps);

	cr.candidates.clear();

	// First candidate doubles as the legacy `hostEndpoint`.  Prefer
	// the default-route interface (what DiscoverLocalIPv4 returns)
	// as the primary — it's the most likely to work for random
	// internet joiners on the same ISP.
	char primaryLan[32] = {};
	bool havePrimary = ATNetplay::Transport::DiscoverLocalIPv4(
		primaryLan, sizeof primaryLan);

	char epb[64];
	if (havePrimary) {
		std::snprintf(epb, sizeof epb, "%s:%u", primaryLan,
			(unsigned)o.boundPort);
		cr.candidates.push_back(epb);
	} else if (!lanIps.empty()) {
		// Enumeration found something but default-route path didn't
		// — still publish what we found.
		std::snprintf(epb, sizeof epb, "%s:%u", lanIps.front().c_str(),
			(unsigned)o.boundPort);
		cr.candidates.push_back(epb);
	} else {
		// Completely offline — loopback only.  Session is
		// unreachable from off-box; UI should flag this.
		std::snprintf(epb, sizeof epb, "127.0.0.1:%u",
			(unsigned)o.boundPort);
		cr.candidates.push_back(epb);
	}
	cr.hostEndpoint = epb;   // v2 clients see this single best guess

	// Add every OTHER interface we found (skipping the primary
	// we already published).
	for (const auto& ip : lanIps) {
		char cand[64];
		std::snprintf(cand, sizeof cand, "%s:%u", ip.c_str(),
			(unsigned)o.boundPort);
		bool dup = false;
		for (const auto& existing : cr.candidates) {
			if (existing == cand) { dup = true; break; }
		}
		if (!dup) cr.candidates.push_back(cand);
	}

	// Loopback as the final fallback for same-box testing.
	{
		char loop[32];
		std::snprintf(loop, sizeof loop, "127.0.0.1:%u",
			(unsigned)o.boundPort);
		cr.candidates.push_back(loop);
	}

	{
		std::string joined;
		for (size_t i = 0; i < cr.candidates.size(); ++i) {
			if (i) joined += ", ";
			joined += cr.candidates[i];
		}
		g_ATLCNetplay("host candidates (pre-srflx / pre-NAT-PMP): [%s]",
			joined.c_str());
	}
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
	//
	// Resolve the "(default)" picker (kernelCRC32 == 0) to the host's
	// actual installed default-kernel CRC32 BEFORE publishing — so the
	// lobby advertises the same CRC that the wire will carry, and
	// browsers can pre-flight the firmware-availability check.
	// Without this, a default-kernel offer publishes "" (no
	// constraint), browsers cheerfully greenlight the join, and the
	// joiner only discovers the missing-firmware mismatch after
	// committing to the connect.  Same logic that BuildBootConfig
	// runs for the wire side.
	uint32_t advertiseKernelCRC = o.config.kernelCRC32;
	uint32_t advertiseBasicCRC  = o.config.basicCRC32;
	{
		ATNetplayProfile::PerGameOverrides ovTmp{};
		ovTmp.hardwareMode = (uint8_t)o.config.hardwareMode;
		ovTmp.basicEnabled = o.config.basicEnabled ? 1 : 0;
		ovTmp.kernelCRC32  = advertiseKernelCRC;
		ovTmp.basicCRC32   = advertiseBasicCRC;
		ATNetplayProfile::ResolveDefaultFirmwareCRCs(ovTmp);
		advertiseKernelCRC = ovTmp.kernelCRC32;
		advertiseBasicCRC  = ovTmp.basicCRC32;
	}

	auto hexCRC = [](uint32_t c, std::string& out) {
		if (!c) { out.clear(); return; }
		char buf[12];
		std::snprintf(buf, sizeof buf, "%08X", c);
		out = buf;
	};
	hexCRC(advertiseKernelCRC, cr.kernelCRC32);
	hexCRC(advertiseBasicCRC,  cr.basicCRC32);
	cr.hardwareMode  = HardwareModeShort(o.config.hardwareMode);
	cr.videoStandard = VideoStandardShort(o.config.videoStandard);
	cr.memoryMode    = MemoryModeShort(o.config.memoryMode);

	const uint32_t tag = OfferTag(o);

	// Bump the coord generation BEFORE posting so the responses we're
	// about to receive carry the new value.  The response handler
	// compares this against the offer's then-current coordGen and
	// treats any mismatch as orphan (immediate Delete + skip
	// register).  Pre-increment matters: posting then bumping would
	// race with a torn-down coord whose late Create response carried
	// the new (post-bump) value and would falsely match.
	++o.coordGen;
	const uint32_t coordGenAtPost = o.coordGen;

	for (const auto& L : lobbies) {
		g_ATLCNetplay("lobby Create posting for \"%s\" -> %s:%u "
			"(section \"%s\", gen=%u)",
			o.gameName.c_str(),
			L.endpoint.host.c_str(), (unsigned)L.endpoint.port,
			L.section.c_str(), (unsigned)coordGenAtPost);

		LobbyRequest req{};
		req.op        = LobbyOp::Create;
		req.endpoint  = L.endpoint;
		req.createReq = cr;
		req.tag       = tag;
		req.coordGen  = coordGenAtPost;
		GetWorker().Post(std::move(req), L.section);
	}
	o.lastHeartbeatMs = 0;    // arm first heartbeat
	// Wipe stale registrations from a previous session; Create
	// responses will repopulate as they arrive.
	o.lobbyRegistrations.clear();
}

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

	// Release the NAT-PMP / PCP mapping we acquired at Create time.
	// The router would drop it when the lease expires (up to 1 h)
	// but being polite avoids leaving orphan mappings when the user
	// cycles sessions quickly — useful both for the router's NAT
	// table pressure and for re-using the same external port on the
	// next session.  Release is a fire-and-forget single UDP packet
	// to the gateway, so we can do it from the main thread without
	// any latency concern.
	if (!o.natPmpProtocol.empty() && o.natPmpInternalPort != 0) {
		ATNetplay::PortMapping m;
		m.protocol     = o.natPmpProtocol;
		m.externalIp   = o.natPmpExternalIp;
		m.externalPort = o.natPmpExternalPort;
		m.internalPort = o.natPmpInternalPort;
		m.lifetimeSec  = 0;    // request release
		ATNetplay::ReleaseUdpPortMapping(m);
		g_ATLCNetplay("NAT-PMP: released mapping %u -> %s:%u",
			(unsigned)o.natPmpInternalPort,
			o.natPmpExternalIp.c_str(),
			(unsigned)o.natPmpExternalPort);
		o.natPmpProtocol.clear();
		o.natPmpExternalIp.clear();
		o.natPmpExternalPort = 0;
		o.natPmpInternalPort = 0;
		o.natPmpLifetimeSec  = 0;
		o.natPmpAcquiredMs   = 0;
	}
}

// Kick off an actual host coordinator for this offer, binding an
// ephemeral port and posting Create afterwards.  Idempotent relative
// to an already-running coordinator.

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

// Build a NetBootConfig from a HostedGame's MachineConfig.  v4 only
// carries the 6 per-game variables — everything else (CPU mode, SIO
// patch, master seed, etc.) is pinned by the canonical Netplay
// Session Profile (see ATNetplayProfile in
// netplay/netplay_profile.h).
ATNetplay::NetBootConfig BuildBootConfig(const HostedGame& o) {
	ATNetplay::NetBootConfig bc{};
	bc.canonicalProfileVersion = ATNetplayProfile::kCanonicalProfileVersion;
	bc.hardwareMode    = (uint8_t)o.config.hardwareMode;
	bc.memoryMode      = (uint8_t)o.config.memoryMode;
	bc.videoStandard   = (uint8_t)o.config.videoStandard;
	bc.basicEnabled    = o.config.basicEnabled ? 1 : 0;
	bc.kernelCRC32     = o.config.kernelCRC32;
	bc.basicCRC32      = o.config.basicCRC32;

	// Resolve "(Altirra default for hardware)" — the picker option that
	// stores CRC32 = 0 — to the host's actual installed default-kernel
	// CRC32 BEFORE the wire encode.  Built-in Altirra kernels routinely
	// change CRC32 between releases, and "default kernel" is each
	// peer's local firmwaremanager preference, so two peers passing 0
	// would silently cold-boot with different ROMs.  Resolving here
	// puts an explicit CRC on the wire — the joiner then runs the
	// CRC-lookup path (FindKernelByCRC) and either finds a matching
	// installed firmware or fails clean with "kernel not installed",
	// surfaced as a snapshot-apply failure.  Same for BASIC.
	{
		ATNetplayProfile::PerGameOverrides ovTmp{};
		ovTmp.hardwareMode = bc.hardwareMode;
		ovTmp.memoryMode   = bc.memoryMode;
		ovTmp.videoStandard = bc.videoStandard;
		ovTmp.basicEnabled = bc.basicEnabled;
		ovTmp.kernelCRC32  = bc.kernelCRC32;
		ovTmp.basicCRC32   = bc.basicCRC32;
		ATNetplayProfile::ResolveDefaultFirmwareCRCs(ovTmp);
		bc.kernelCRC32 = ovTmp.kernelCRC32;
		bc.basicCRC32  = ovTmp.basicCRC32;
	}

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
	// Bump the coord generation BEFORE PostLobbyDelete so any Create
	// requests that are still in flight when this teardown runs
	// (their HTTP responses haven't landed yet) get invalidated when
	// they eventually return.  Without this bump, a late Create
	// response would register a session pointing at a torn-down
	// port; the next StartCoord would post a fresh Create, the
	// dedup-on-replace logic would Delete the old one, but in the
	// gap the joiner could see the stale row in the lobby's session
	// list and target the dead port.  Bumping invalidates the
	// in-flight Create's gen so its response goes straight to
	// orphan-Delete.
	++o.coordGen;
	PostLobbyDelete(o);
	ATNetplayGlue::StopHost(o.id.c_str());
	o.boundPort = 0;
}

// -------------------------------------------------------------------
// Activity state machine — derived each frame.
// -------------------------------------------------------------------

// True iff any of our hostedGames is in a live-play coordinator phase.
//
// Resyncing and Desynced are mid-flight states from which the
// coordinator is expected to recover into Lockstepping (or surface a
// real terminal phase like Ended/Failed).  They MUST be treated as
// "in session" so the activity edge in ReconcileHostedGames doesn't
// fire ATNetplayProfile::EndSession mid-resync — that would tear
// down the canonical session profile while the resync transfer is
// still in flight.
bool AnyHostedGameInSession() {
	for (auto& o : GetState().hostedGames) {
		using P = ATNetplayGlue::Phase;
		P p = ATNetplayGlue::HostPhase(o.id.c_str());
		if (p == P::Handshaking || p == P::SendingSnapshot ||
		    p == P::ReceivingSnapshot || p == P::SnapshotReady ||
		    p == P::Lockstepping ||
		    p == P::Resyncing || p == P::Desynced) return true;
	}
	return false;
}

// True iff we joined someone's session.  See AnyHostedGameInSession
// for why Resyncing and Desynced count as "in session".
bool JoinerInSession() {
	using P = ATNetplayGlue::Phase;
	P p = ATNetplayGlue::JoinPhase();
	return p == P::Handshaking || p == P::SendingSnapshot ||
	       p == P::ReceivingSnapshot || p == P::SnapshotReady ||
	       p == P::Lockstepping ||
	       p == P::Resyncing || p == P::Desynced;
}

} // anonymous

// -------------------------------------------------------------------
// Public
// -------------------------------------------------------------------

// Public counterpart to the anon-namespace PostLobbyDelete that takes
// an entire HostedGame.  Used by the lobby Create response handler
// in ui_netplay.cpp to clean up a session that got displaced by a
// later Create response (Enable/Disable/Enable race) — the displaced
// session is no longer in any HostedGame's lobbyRegistrations, so
// the standard PostLobbyDelete can't reach it.  Without this, the
// orphan stays listed for the lobby's TTL window, advertising a UDP
// port no coord listens on, and joiners that pick it up never reach
// the live host.
void PostLobbyDeleteForSession(const std::string& section,
                               const std::string& sessionId,
                               const std::string& token) {
	if (section.empty() || sessionId.empty() || token.empty()) return;

	for (const auto& L : AllEnabledHttpLobbies()) {
		if (L.section != section) continue;
		LobbyRequest req{};
		req.op        = LobbyOp::Delete;
		req.endpoint  = L.endpoint;
		req.sessionId = sessionId;
		req.token     = token;
		GetWorker().Post(std::move(req), section);
		g_ATLCNetplay("lobby: posted Delete for orphan session %s "
			"(section \"%s\")", sessionId.c_str(), section.c_str());
		return;
	}
	// Section was removed from lobby.ini between Create and the
	// orphan-detection: nothing we can do; the lobby's TTL will
	// eventually evict the session.
	g_ATLCNetplay("lobby: cannot Delete orphan session %s — "
		"section \"%s\" no longer in lobby.ini",
		sessionId.c_str(), section.c_str());
}

void SyncDeleteAllRegistrationsForShutdown() {
	// Race we are closing: ATNetplayUI_Shutdown() previously called
	// PostLobbyDelete (async) and then immediately stopped the
	// worker.  LobbyWorker::Stop() joins the thread and clears the
	// in-queue, so any Delete that hadn't been dequeued yet was
	// silently dropped — leaving the session listed in the lobby
	// for up to its TTL (60 s).  This helper bypasses the worker
	// and calls LobbyClient::Delete inline, with a tight per-call
	// timeout, so app exit completes the deletes when reachable
	// and isn't held up when not.  Total worst case: N×timeoutMs
	// (typically << 100 ms total against the live lobby).
	auto lobbies = AllEnabledHttpLobbies();
	if (lobbies.empty()) return;

	constexpr uint32_t kShutdownDeleteTimeoutMs = 1500;

	for (auto& o : GetState().hostedGames) {
		for (const auto& reg : o.lobbyRegistrations) {
			if (reg.sessionId.empty() || reg.token.empty()) continue;

			ATNetplay::LobbyEndpoint epCopy;
			bool epFound = false;
			for (const auto& L : lobbies) {
				if (L.section == reg.section) {
					epCopy = L.endpoint;
					epFound = true;
					break;
				}
			}
			if (!epFound) continue;

			// Override the endpoint's normal 2 s timeout with our
			// tighter shutdown budget so app exit isn't held up
			// when the lobby is unreachable.
			epCopy.timeoutMs = kShutdownDeleteTimeoutMs;

			ATNetplay::LobbyClient client(epCopy);
			bool ok = client.Delete(reg.sessionId, reg.token);
			g_ATLCNetplay(
				"shutdown: sync DELETE session %s @ %s — %s (status=%d)",
				reg.sessionId.c_str(), reg.section.c_str(),
				ok ? "ok" : "failed", client.LastStatus());
		}
		// Clear so the subsequent DisableHostedGame loop in
		// ATNetplayUI_Shutdown sees nothing to delete and skips
		// the async PostLobbyDelete branch (PostLobbyDelete
		// iterates an empty vector cleanly; coord-stop and
		// NAT-PMP release still run).
		o.lobbyRegistrations.clear();
	}
}

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
	// Clear liveness fields so the list no longer claims "up Xm" for a
	// disabled row.  Re-armed on the next heartbeat when re-enabled.
	o->currentPlayers  = 0;
	o->hostStartedAtMs = 0;
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

	// Session-end edge: if we were InSession last tick and aren't
	// now, tear down the canonical netplay profile and switch back
	// to the user's pre-session profile.  Idempotent — safe even if
	// EndSession was already invoked synchronously by the explicit
	// Leave path or by the glue cleanup hook on app shutdown.
	if (prev == UserActivity::InSession &&
	    st.activity != UserActivity::InSession &&
	    ATNetplayProfile::IsActive()) {
		ATNetplayProfile::EndSession();
		// In-app feedback so the user knows their normal config
		// came back.  Platform notifications don't cover this
		// since nothing "arrived" — the session simply ended.
		PushToast("Session ended — your normal settings were restored.",
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
			// for ATNetplayProfile::EndSession when its session ends.
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

		// Push the relay context to the coordinator EVERY tick (not
		// only when the heartbeat is due).  Reason: the Create-result
		// handler in ui_netplay.cpp stamps `lastHeartbeatMs = nowMs`
		// the moment the lobby registration is received, which makes
		// the heartbeat-due block below skip its first iteration for
		// up to 30 s.  Hoisting the SetRelayContext call out of that
		// block lets MaybePrearmRelay see mLobbyRelayKnown=true on
		// the very first Poll tick after Create returns — that is
		// what makes "relay-first" actually relay-first instead of
		// waiting another 30 s for the next heartbeat.  The call is
		// idempotent (it just memcpy's sessionId + resolves the
		// lobby endpoint), so per-tick frequency is harmless.
		if (coordRunning && !o.lobbyRegistrations.empty()) {
			auto lobbiesNow = AllEnabledHttpLobbies();
			for (const auto& reg : o.lobbyRegistrations) {
				if (reg.sessionId.empty() || reg.token.empty())
					continue;
				const ATNetplay::LobbyEndpoint *ep = nullptr;
				for (const auto& L : lobbiesNow) {
					if (L.section == reg.section) { ep = &L.endpoint; break; }
				}
				if (!ep) continue;
				char lobbyHostPort[128];
				std::snprintf(lobbyHostPort, sizeof lobbyHostPort,
					"%s:%u", ep->host.c_str(),
					(unsigned)ATLobby::kReflectorPortDefault);
				ATNetplayGlue::HostSetRelayContext(
					o.id.c_str(),
					reg.sessionId.c_str(),
					lobbyHostPort);
			}
		}

		// Periodic heartbeat to every lobby this offer is registered
		// with.  A slow/dead lobby here doesn't block the others
		// because each Heartbeat request is independent.
		if (coordRunning && !o.lobbyRegistrations.empty()) {
			// Pre-Lockstepping the heartbeat is the only path that
			// carries the joiner's peer-hint to the host, and a
			// joiner's handshake timeout is kRelayFailTimeoutMs (25 s).
			// A 30 s heartbeat interval can therefore lose the race
			// outright — the joiner times out before the host's next
			// poll arrives.  Drop to 5 s while the offer is still
			// waiting for / accepting a join so peer-hints land
			// well inside the joiner's budget; bump back to 30 s once
			// Lockstepping starts since the live session no longer
			// depends on heartbeat-delivered hints (relay traffic
			// keeps everything refreshed) and we want to minimize
			// lobby HTTP load.  The lobby session TTL is 60 s, so
			// either interval keeps the listing alive.
			const bool preLockstep =
				(p == P::WaitingForJoiner ||
				 p == P::Handshaking ||
				 p == P::SendingSnapshot ||
				 p == P::ReceivingSnapshot ||
				 p == P::SnapshotReady);
			const uint64_t kHeartbeatMs = preLockstep ? 5000 : 30000;
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
				// Mirror into the HostedGame so the UI can render a
				// live "N/M players · up 5m" subtitle without reaching
				// into the lobby worker.  maxPlayers mirrors the
				// CreateRequest default of 2 (same constant used at
				// BuildCreateRequest).
				o.currentPlayers = (uint32_t)playerCount;
				o.maxPlayers     = 2;
				if (o.hostStartedAtMs == 0)
					o.hostStartedAtMs = nowMs;
				for (const auto& reg : o.lobbyRegistrations) {
					if (reg.sessionId.empty() || reg.token.empty())
						continue;
					const ATNetplay::LobbyEndpoint *ep = nullptr;
					for (const auto& L : lobbies) {
						if (L.section == reg.section) { ep = &L.endpoint; break; }
					}
					if (!ep) continue;

					// v4 two-sided punch: relay context is now
					// pushed every tick from the hoisted block
					// above, so we don't need to refresh it here.
					LobbyRequest req{};
					req.op          = LobbyOp::Heartbeat;
					req.endpoint    = *ep;
					req.sessionId   = reg.sessionId;
					req.token       = reg.token;
					req.playerCount = playerCount;
					req.state       = state;
					// Tag so the result handler can route peer-hints
					// carried in the heartbeat response back to this
					// HostedGame's coordinator (via HostIngestPeerHint).
					req.tag         = OfferTag(o);
					GetWorker().Post(std::move(req), reg.section);
				}
				o.lastHeartbeatMs = nowMs;
			}
		}

		// NAT-PMP / PCP lease refresh.  The router granted a mapping
		// with `natPmpLifetimeSec` at Create time; after that elapses
		// the external→internal forward disappears and any joiner
		// attempting to reach the mapped srflx gets no response.
		// RFC 6886 §3.7 recommends renewing at half-lifetime, which
		// gives the renew a full second half-lifetime to land before
		// the mapping would actually drop.  Only fire once per
		// half-lifetime per HostedGame — natPmpRefreshInFlight is
		// cleared when the worker result arrives (ok or error).
		if (!o.natPmpProtocol.empty() &&
		    o.natPmpInternalPort != 0 &&
		    o.natPmpLifetimeSec  != 0 &&
		    o.natPmpAcquiredMs   != 0 &&
		    !o.natPmpRefreshInFlight) {
			const uint64_t halfLifeMs =
				(uint64_t)o.natPmpLifetimeSec * 500ull;  // /2 * 1000
			const uint64_t dueAt =
				std::max<uint64_t>(
					o.natPmpAcquiredMs + halfLifeMs,
					o.natPmpRetryAfterMs);
			if (nowMs >= dueAt) {
				LobbyRequest req{};
				req.op                      = LobbyOp::PortMapRefresh;
				req.portRefreshInternalPort = o.natPmpInternalPort;
				req.portRefreshLifetimeSec  = o.natPmpLifetimeSec;
				req.tag                     = OfferTag(o);
				o.natPmpRefreshInFlight = true;
				g_ATLCNetplay("NAT-PMP: refreshing mapping for \"%s\" "
					"(port %u, %us into %us lease)",
					o.gameName.c_str(),
					(unsigned)o.natPmpInternalPort,
					(unsigned)((nowMs - o.natPmpAcquiredMs) / 1000),
					(unsigned)o.natPmpLifetimeSec);
				GetWorker().Post(std::move(req), "");
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

	// v3 NAT traversal: prefer the candidates list (LAN;srflx;loopback)
	// over the single hostEndpoint when the host published one.  The
	// coordinator sprays NetHello to each candidate in parallel and
	// locks onto the first responder.  Fall back to hostEndpoint
	// when candidates is empty (old hosts / v2 lobby entries).
	const std::string& cand = st.session.joinTarget.candidates;
	const char* target = cand.empty()
		? st.session.joinTarget.hostEndpoint.c_str()
		: cand.c_str();
	g_ATLCNetplay("joiner: StartJoin target = \"%s\" (%s)",
		target,
		cand.empty() ? "legacy hostEndpoint" : "v3 candidates");

	bool ok = ATNetplayGlue::StartJoin(
		target,
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

	// Reset the joiner-side waiting clock for THIS attempt.  The
	// per-tick reset in the actions loop only fires on the
	// non-active → active edge, which doesn't happen on a "Try
	// Again" because the previous attempt may still be sitting in a
	// terminal-but-non-None phase (Failed) when the user clicks
	// retry.  Stamping here unconditionally guarantees the on-screen
	// "(Ns)" elapsed counter starts from zero on every fresh Start.
	st.session.joinStartedMs = (uint64_t)SDL_GetTicks();

	// v4 two-sided punch: arm the relay context and POST our own
	// candidates to the lobby so the host can fire outbound NetPunch
	// probes before our Hello spray arrives.  Routed by the session's
	// sourceLobby so we hit the same lobby that surfaced the session.
	if (!st.session.joinTarget.sessionId.empty()) {
		auto lobbies = AllEnabledHttpLobbies();
		const ATNetplay::LobbyEndpoint *ep = nullptr;
		for (const auto& L : lobbies) {
			if (L.section == st.session.joinTarget.sourceLobby) {
				ep = &L.endpoint; break;
			}
		}
		if (!ep && !lobbies.empty()) ep = &lobbies.front().endpoint;
		if (ep) {
			char lobbyHostPort[128];
			std::snprintf(lobbyHostPort, sizeof lobbyHostPort,
				"%s:%u", ep->host.c_str(),
				(unsigned)ATLobby::kReflectorPortDefault);
			ATNetplayGlue::JoinerSetRelayContext(
				st.session.joinTarget.sessionId.c_str(),
				lobbyHostPort);

			char cands[512] = "";
			ATNetplayGlue::JoinerBuildLocalCandidates(cands, sizeof cands);
			char nonceHex[33] = "";
			ATNetplayGlue::JoinerGetSessionNonceHex(nonceHex);
			if (cands[0]) {
				LobbyRequest req{};
				req.op        = LobbyOp::PeerHint;
				req.endpoint  = *ep;
				req.sessionId = st.session.joinTarget.sessionId;
				req.token     = nonceHex;               // nonce hex
				req.state     = ResolvedNickname();     // joiner handle
				req.createReq.candidates.clear();
				req.createReq.candidates.push_back(cands);
				// Joiner's bound UDP port — worker prepends the srflx
				// observed by the lobby's reflector before POSTing so
				// the host receives a routable address even when the
				// joiner is behind CGNAT / a NAT that hides the
				// public endpoint from the local-IP enumeration.
				req.peerHintLocalPort =
					ATNetplayGlue::JoinerBoundPort();
				GetWorker().Post(std::move(req), ep->host);
				g_ATLCNetplay("joiner: posting peer-hint to %s:%u "
					"(nonce=%s local-cands=\"%s\" "
					"srflx-probe-port=%u)",
					ep->host.c_str(), (unsigned)ep->port,
					nonceHex, cands,
					(unsigned)req.peerHintLocalPort);
			}
		}
	}

	Navigate(Screen::Waiting);
}

// -------------------------------------------------------------------
// Join compatibility — firmware CRC32 lookup helpers.
//
// (The pre-session restore-point + ApplyMachineConfig paths from the
// v3 protocol have been replaced by the canonical Netplay Session
// Profile in netplay/netplay_profile.cpp.  Session begin/end now go
// through ATNetplayProfile::BeginSession / EndSession.)
// -------------------------------------------------------------------

namespace {

// Look up a firmware by CRC32 under a type filter.  Returns 0 if no
// match.  The ATFirmwareManager "[XXXXXXXX]" ref-string path
// (firmwaremanager.cpp:605-626) would do this too but internally
// calls LoadFirmware + CRC32 on every filter-matching firmware with
// no cache — at 60 Hz per tile that hammered the disk on Android.
// Iterating the in-memory fwList ourselves and routing the CRC
// through ComputeFirmwareCRC32 (cached by firmware ID) collapses
// the repeated scan into one-shot-per-firmware-per-session work.
using TypeFilter = bool (*)(ATFirmwareType);
uint64 FindFirmwareByCRC(ATFirmwareManager& fwm, uint32_t crc32,
                         TypeFilter typeOk) {
	if (crc32 == 0) return 0;
	vdvector<ATFirmwareInfo> list;
	fwm.GetFirmwareList(list);
	for (const auto& fw : list) {
		if (!fw.mbVisible) continue;
		if (!typeOk(fw.mType)) continue;
		if (ComputeFirmwareCRC32(fw.mId) == crc32) return fw.mId;
	}
	return 0;
}

uint64 FindKernelByCRC(ATFirmwareManager& fwm, uint32_t crc32) {
	return FindFirmwareByCRC(fwm, crc32,
		+[](ATFirmwareType t) { return ATIsKernelFirmwareType(t); });
}

uint64 FindBasicByCRC(ATFirmwareManager& fwm, uint32_t crc32) {
	return FindFirmwareByCRC(fwm, crc32,
		+[](ATFirmwareType t) { return t == kATFirmwareType_Basic; });
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

	bool missK = false, missB = false;
	if (haveK) missK = (FindKernelByCRC(*fwm, kCrc) == 0);
	if (haveB) missB = (FindBasicByCRC (*fwm, bCrc) == 0);

	// outMissingCRCHex carries a single "offending" CRC for callers
	// that render a one-liner toast ("install ROM [xxxx]").  When
	// both slots are missing, report the kernel CRC — the OS is the
	// more-fundamental requirement and tends to be the first thing
	// the user installs anyway.  The Browser row's per-token red
	// flag still shows both names because it consumes the enum
	// directly (see BuildSpecLineFromSession).
	if (outMissingCRCHex) {
		if      (missK) std::snprintf(outMissingCRCHex, 9, "%08X", kCrc);
		else if (missB) std::snprintf(outMissingCRCHex, 9, "%08X", bCrc);
	}

	if (missK && missB) return JoinCompat::MissingBoth;
	if (missK)          return JoinCompat::MissingKernel;
	if (missB)          return JoinCompat::MissingBasic;
	return JoinCompat::Compatible;
}

} // namespace ATNetplayUI
