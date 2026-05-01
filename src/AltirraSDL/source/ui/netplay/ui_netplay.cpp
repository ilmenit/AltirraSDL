//	AltirraSDL - Online Play common render entry + lifecycle

#include <stdafx.h>

#include "ui_netplay.h"
#include "ui_netplay_state.h"
#include "ui_netplay_widgets.h"
#include "ui_netplay_lobby_worker.h"
#include "ui_netplay_actions.h"
#include "ui_netplay_activity.h"

#include "netplay/platform_notify.h"
#include "netplay/netplay_glue.h"
#include "netplay/netplay_profile.h"
#include "netplay/packets.h"

#include "ui/core/ui_mode.h"
#include "ui/core/ui_main.h"
#include "ui/emotes/emote_netplay.h"
#include "ui/emotes/emote_picker.h"
#include "ui/gamelibrary/game_library.h"
#include "ui/mobile/mobile_internal.h"   // GetGameLibrary()
#include "input/touch_widgets.h"

#include <vd2/system/file.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>

#include <imgui.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <unordered_set>

#include <at/atcore/logging.h>
extern ATLogChannel g_ATLCNetplay;

extern VDStringA ATGetConfigDir();

namespace ATNetplayUI {

// Screen dispatcher implemented in ui_netplay_screens.cpp.
bool ATNetplayUI_DispatchScreen();
void ATNetplayUI_EnqueueBrowserRefresh();
void ATNetplayUI_EnqueueStatsRefresh();

namespace {

LobbyWorker g_worker;

// Consumed by the screens file to post refreshes.
LobbyWorker& GetWorkerImpl() { return g_worker; }

// Find the offer whose id hashes to `tag`.  Used to route Create /
// Heartbeat results back to the right offer.  Collision probability
// is negligible for kMaxHostedGames=5.
HostedGame* FindHostedGameByTag(uint32_t tag) {
	if (!tag) return nullptr;
	for (auto& o : GetState().hostedGames) {
		uint32_t t = 0;
		for (unsigned char c : o.id) t = t * 31u + c;
		if ((t ? t : 1u) == tag) return &o;
	}
	return nullptr;
}

} // anonymous

// Definition of the forward-declared accessor in other TUs.
LobbyWorker& GetWorker() { return GetWorkerImpl(); }

} // namespace ATNetplayUI

// ---------------------------------------------------------------------
// Public entry points
// ---------------------------------------------------------------------

// Cleanup hook installed into the netplay glue.  Fires when the lower
// glue layer tears down a session via DisconnectActive() or Shutdown(),
// so the canonical netplay profile is unwound and the user's
// pre-session settings restored even on paths that don't go through
// the activity-edge in ReconcileHostedGames (notably app shutdown).
// Idempotent — EndSession internally guards on IsActive().
static void ATNetplayUI_GlueCleanupHook() {
	ATNetplayProfile::EndSession();
}

void ATNetplayUI_Initialize(SDL_Window *window) {
	ATNetplayUI::Initialize();
	ATNetplay::SetWindow(window);
	ATNetplay::Initialize("AltirraSDL");
	ATNetplayUI::GetWorker().Start();
	ATNetplayUI::Activity_Hook();
	ATNetplayGlue::SetSessionEndCleanupHook(&ATNetplayUI_GlueCleanupHook);

	// Item 4d/4e: feed the joiner-side cache lookup with the user's
	// Game Library.  GetGameLibrary returns a singleton; we capture
	// it here at init time (the library outlives every join attempt).
	// The lookup runs on the netplay glue thread context (always the
	// main thread in the SDL3 build) so no extra synchronization.
	ATNetplayGlue::SetLibraryLookupHook(
		[](uint32_t crc32, uint64_t expectedSize,
		   const char ext[8], std::vector<uint8_t>& out) -> bool {
			ATGameLibrary *lib = GetGameLibrary();
			if (!lib) return false;
			return lib->FindVariantBytesForCRC32(
				crc32, expectedSize, ext, out);
		});
}

void ATNetplayUI_Shutdown() {
	ATNetplayGlue::SetSessionEndCleanupHook(nullptr);
	ATNetplayUI::Activity_Unhook();
	// Synchronously delete every active lobby registration BEFORE
	// the worker thread is stopped.  The async PostLobbyDelete path
	// would race against GetWorker().Stop() — Stop() joins the
	// thread and clears the queue, so any unprocessed Delete is
	// silently dropped, leaving the session listed for its TTL.
	// SyncDeleteAllRegistrationsForShutdown calls LobbyClient::Delete
	// inline (with a tight 1.5 s per-call timeout) and clears each
	// HostedGame's lobbyRegistrations, so the DisableHostedGame
	// loop below sees nothing to delete and just tears down the
	// coordinators / NAT-PMP mappings.
	ATNetplayUI::SyncDeleteAllRegistrationsForShutdown();
	// Coordinator + NAT-PMP teardown for every offer (lobby Deletes
	// already done by the sync pass above).
	for (auto& o : ATNetplayUI::GetState().hostedGames) {
		ATNetplayUI::DisableHostedGame(o.id);
	}
	ATNetplayGlue::Shutdown();
	ATNetplayUI::GetWorker().Stop();
	ATNetplay::Shutdown();
	ATNetplayUI::Shutdown();
}

void ATNetplayUI_Poll(uint64_t nowMs) {
	auto& st = ATNetplayUI::GetState();

	// Drain completed lobby requests.
	st.browser.refreshInFlight =
		(ATNetplayUI::GetWorker().InFlightCount() > 0);
	ATNetplayUI::GetWorker().Poll(
		[&](ATNetplayUI::LobbyResult& r) {
			// Record cross-window lobby reachability — every op except
			// Stats updates this so Browse and Host Games show a
			// consistent status without each window polling on its
			// own.  Stats is a best-effort enhancement (older lobbies
			// 404 on /v1/stats); failures there must not poison the
			// global health banner.
			const bool affectsHealth =
				(r.op != ATNetplayUI::LobbyOp::Stats);
			if (affectsHealth && r.ok) {
				st.lobbyHealth.lastOkMs   = nowMs;
				st.lobbyHealth.lastStatus = r.httpStatus;
				st.lobbyHealth.lastError.clear();
			} else if (affectsHealth) {
				st.lobbyHealth.lastFailMs = nowMs;
				st.lobbyHealth.lastStatus = r.httpStatus;
				st.lobbyHealth.lastError  =
					ATNetplayUI::FriendlyLobbyError(r.error, r.httpStatus);
			}

			// -- Create: route by tag to the HostedGame that fired it.
			// Multi-lobby hosting: each enabled HTTP lobby in
			// lobby.ini gets its own Create request, so this handler
			// may fire multiple times per offer.  We append one
			// LobbyRegistration per successful lobby; failures for one
			// lobby don't affect the others (the coordinator is
			// already bound, and joiners from any lobby that accepted
			// the Create can still connect).
			if (r.op == ATNetplayUI::LobbyOp::Create) {
				ATNetplayUI::HostedGame* o =
					ATNetplayUI::FindHostedGameByTag(r.tag);
				if (!o) return;
				if (r.ok) {
					// Generation gate: if the offer's coordGen has
					// advanced since the request was posted (the coord
					// that originated this Create has been torn down +
					// recreated), the just-created lobby session points
					// at a UDP port no coord listens on.  Post a Delete
					// to remove it from the lobby immediately and SKIP
					// the local register step — otherwise a joiner that
					// browsed the lobby in this window would pick the
					// orphan, target the dead port, and fail to connect.
					// Without this gate, the orphan would be visible for
					// the lobby's TTL window (~90s).
					if (r.coordGen != o->coordGen) {
						g_ATLCNetplay("lobby Create OK for \"%s\" "
							"is STALE (req gen=%u, current gen=%u) "
							"— sessionId=%s scheduled for orphan Delete",
							o->gameName.c_str(),
							(unsigned)r.coordGen,
							(unsigned)o->coordGen,
							r.create.sessionId.c_str());
						ATNetplayUI::PostLobbyDeleteForSession(
							r.sourceLobby,
							r.create.sessionId,
							r.create.token);
						return;
					}
					ATNetplayUI::HostedGame::LobbyRegistration reg;
					reg.section   = r.sourceLobby;
					reg.sessionId = r.create.sessionId;
					reg.token     = r.create.token;
					// De-dup on section (a quick Enable/Disable/Enable
					// cycle could leave a stale entry).
					bool replaced = false;
					for (auto& e : o->lobbyRegistrations) {
						if (e.section == reg.section) {
							const std::string oldSection   = e.section;
							const std::string oldSessionId = e.sessionId;
							const std::string oldToken     = e.token;
							const std::string newSessionId = reg.sessionId;
							e = std::move(reg);
							replaced = true;
							if (!oldSessionId.empty() &&
							    oldSessionId != newSessionId) {
								ATNetplayUI::PostLobbyDeleteForSession(
									oldSection, oldSessionId, oldToken);
							}
							break;
						}
					}
					if (!replaced)
						o->lobbyRegistrations.push_back(std::move(reg));

					o->lastHeartbeatMs = nowMs;
					// Only clear lastError if no OTHER lobby is still
					// showing a failure — but for v1 we just clear on
					// any success; the next Heartbeat batch will resurface
					// per-lobby issues if they persist.
					o->lastError.clear();

					// Stash the NAT-PMP mapping (if any) so the
					// session-delete path can release it politely
					// rather than leaving it parked on the router
					// until the lease expires.  Multiple lobbies may
					// report mappings; we keep the first non-empty
					// one — they should all be identical since the
					// mapping is per-game-port, not per-lobby.
					if (!r.natPmpProtocol.empty() &&
					    o->natPmpProtocol.empty()) {
						o->natPmpProtocol     = r.natPmpProtocol;
						o->natPmpExternalIp   = r.natPmpExternalIp;
						o->natPmpExternalPort = r.natPmpExternalPort;
						o->natPmpInternalPort = r.natPmpInternalPort;
						o->natPmpLifetimeSec  = r.natPmpLifetimeSec;
						o->natPmpAcquiredMs   = nowMs;
						g_ATLCNetplay("NAT-PMP: stored mapping %s "
							"%u→%s:%u lease=%us for later release",
							r.natPmpProtocol.c_str(),
							(unsigned)r.natPmpInternalPort,
							r.natPmpExternalIp.c_str(),
							(unsigned)r.natPmpExternalPort,
							(unsigned)r.natPmpLifetimeSec);
					}

					g_ATLCNetplay("lobby Create OK for \"%s\" "
						"(section \"%s\") sessionId=%s",
						o->gameName.c_str(),
						r.sourceLobby.c_str(),
						r.create.sessionId.c_str());
				} else {
					// Per-lobby failure: surface the error but do NOT
					// disable the offer — successful Creates on other
					// lobbies are still reachable.  Prefix with the
					// lobby section so the user knows which one.
					char prefix[96];
					std::snprintf(prefix, sizeof prefix,
						"Lobby \"%s\" listing failed: ",
						r.sourceLobby.empty() ? "?" : r.sourceLobby.c_str());
					o->lastError = std::string(prefix)
						+ st.lobbyHealth.lastError;
					g_ATLCNetplay("lobby Create FAILED for \"%s\" "
						"(section \"%s\"): status=%d raw=\"%s\"",
						o->gameName.c_str(),
						r.sourceLobby.c_str(),
						r.httpStatus,
						r.error.empty() ? "(no detail)"
						                : r.error.c_str());
				}
				return;
			}
			if (r.op == ATNetplayUI::LobbyOp::Heartbeat) {
				if (!r.ok) {
					g_ATLCNetplay("lobby Heartbeat FAILED: status=%d raw=\"%s\"",
						r.httpStatus,
						r.error.empty() ? "(no detail)"
						                : r.error.c_str());
				}
				// v4 two-sided punch: hand any buffered joiner hints
				// to the host coordinator so it can fire outbound
				// NetPunch probes and pre-open its NAT pinhole before
				// the joiner's Hello spray arrives.  Routed by tag
				// (set in ReconcileHostedGames's heartbeat Post) so
				// multi-offer hosts don't cross-feed hints.
				if (r.ok && !r.hints.empty()) {
					ATNetplayUI::HostedGame* o =
						ATNetplayUI::FindHostedGameByTag(r.tag);
					if (o) {
						for (const auto& h : r.hints) {
							g_ATLCNetplay("lobby Heartbeat: peer-hint "
								"\"%s\" nonce=%s cands=\"%s\" (age %dms)",
								h.joinerHandle.c_str(),
								h.nonceHex.c_str(),
								h.candidates.c_str(),
								h.ageMs);
							ATNetplayGlue::HostIngestPeerHint(
								o->id.c_str(),
								h.nonceHex.c_str(),
								h.candidates.c_str());
						}
					}
				}
				return;
			}
			if (r.op == ATNetplayUI::LobbyOp::Delete) {
				if (!r.ok) {
					g_ATLCNetplay("lobby Delete FAILED: status=%d raw=\"%s\"",
						r.httpStatus,
						r.error.empty() ? "(no detail)"
						                : r.error.c_str());
				}
				return;
			}
			if (r.op == ATNetplayUI::LobbyOp::PortMapRefresh) {
				ATNetplayUI::HostedGame* o =
					ATNetplayUI::FindHostedGameByTag(r.tag);
				if (!o) return;
				o->natPmpRefreshInFlight = false;
				if (r.ok && !r.natPmpProtocol.empty() &&
				    r.natPmpInternalPort != 0) {
					const bool portChanged =
						(r.natPmpExternalPort != o->natPmpExternalPort) ||
						(r.natPmpExternalIp   != o->natPmpExternalIp);
					o->natPmpProtocol     = r.natPmpProtocol;
					o->natPmpExternalIp   = r.natPmpExternalIp;
					o->natPmpExternalPort = r.natPmpExternalPort;
					o->natPmpInternalPort = r.natPmpInternalPort;
					o->natPmpLifetimeSec  = r.natPmpLifetimeSec;
					o->natPmpAcquiredMs   = nowMs;
					o->natPmpRetryAfterMs = 0;
					g_ATLCNetplay("NAT-PMP: refreshed mapping for \"%s\" "
						"(%s %u→%s:%u lease=%us)%s",
						o->gameName.c_str(),
						r.natPmpProtocol.c_str(),
						(unsigned)r.natPmpInternalPort,
						r.natPmpExternalIp.c_str(),
						(unsigned)r.natPmpExternalPort,
						(unsigned)r.natPmpLifetimeSec,
						portChanged
							? " — external endpoint CHANGED"
							: "");
					// If the external port changed the lobby listing now
					// advertises a stale host endpoint; the next heartbeat
					// (≤30 s) refreshes it.  A live joiner mid-handshake
					// would fall back to the reflector-probe srflx
					// candidate already in the candidates list, so no
					// explicit recovery path is needed here.
				} else {
					g_ATLCNetplay("NAT-PMP: refresh FAILED for \"%s\": %s "
						"(existing mapping valid until natural expiry)",
						o->gameName.c_str(),
						r.error.empty() ? "no detail" : r.error.c_str());
					// Back off for 5 minutes before retrying.  Without
					// this, every tick would re-post because
					// acquiredMs + halfLifeMs is already in the past.
					// Worst-case degradation on prolonged router
					// failure: mapping eventually expires and joiners
					// fall back to the reflector-probe srflx candidate
					// that was published alongside the mapping at
					// Create time.
					o->natPmpRetryAfterMs = nowMs + 5ull * 60ull * 1000ull;
				}
				return;
			}
			if (r.op == ATNetplayUI::LobbyOp::Stats) {
				// Sum results across federated lobbies.  pendingResponses
				// was set when the cycle was kicked off; each landing
				// result accumulates and the last one (counter→0)
				// publishes the cycle's totals to the live counters.
				auto& a = st.aggregateStats;
				if (r.ok) {
					a.acc_sessions += r.stats.sessions;
					a.acc_waiting  += r.stats.waiting;
					a.acc_playing  += r.stats.playing;
					a.acc_hosts    += r.stats.hosts;
				}
				if (a.pendingResponses > 0) --a.pendingResponses;
				if (a.pendingResponses == 0) {
					a.sessions = a.acc_sessions;
					a.waiting  = a.acc_waiting;
					a.playing  = a.acc_playing;
					a.hosts    = a.acc_hosts;
					a.lastUpdateMs = nowMs;
				}
				return;
			}
			if (r.op != ATNetplayUI::LobbyOp::List) return;
			// Lobby ping update: List() round-trip stamped by the
			// worker.  EWMA with alpha = 0.3 so single-sample jitter
			// doesn't dominate the indicator but a real shift is
			// visible within a few polls.  On transport failure
			// (listLatencyMs == 0) we leave the previous EWMA alone
			// — the Browser screen will surface the failure via the
			// status line; collapsing the indicator to 0 would
			// confuse "lobby slow" with "lobby unreachable".
			if (r.listLatencyMs > 0) {
				if (st.browser.lobbyLatencySampleCount == 0) {
					st.browser.lobbyLatencyMs = r.listLatencyMs;
				} else {
					// Fixed-point EWMA: avoid float just for one update.
					// new = old*0.7 + sample*0.3
					uint64_t blended =
						(uint64_t)st.browser.lobbyLatencyMs * 7 +
						(uint64_t)r.listLatencyMs           * 3;
					st.browser.lobbyLatencyMs = (uint32_t)(blended / 10);
				}
				st.browser.lobbyLatencyLastSampleMs = (uint32_t)r.listLatencyMs;
				++st.browser.lobbyLatencySampleCount;
			}
			if (!r.ok) {
				// The lobby banner already paints the friendly error;
				// keep the browser status line informative but
				// actionable — a "Retry" button lives right above
				// this line, so point users there instead of inventing
				// a Direct-IP option that doesn't exist in the UI.
				st.browser.statusLine = st.lobbyHealth.lastError
					+ " — tap Retry to try again.";
				g_ATLCNetplay("lobby List FAILED: status=%d raw=\"%s\"",
					r.httpStatus,
					r.error.empty() ? "(no detail)"
					                : r.error.c_str());
				// Exponential backoff: 10s, 20s, 40s, 60s cap.
				// Without this, a 429 (which returns instantly) would
				// trigger another List next frame — burning tokens
				// faster than the server can refill them.
				++st.browser.consecutiveFailures;
				uint32_t n = st.browser.consecutiveFailures;
				uint64_t backoffMs = 10000ull << (n > 3 ? 3 : (n - 1));
				if (backoffMs > 60000ull) backoffMs = 60000ull;
				st.browser.lastFetchMs = nowMs;
				st.browser.nextRetryMs = nowMs + backoffMs;
				return;
			}
			st.browser.consecutiveFailures = 0;
			st.browser.nextRetryMs = 0;

			// In-place reconcile: retire entries whose sessionId is no
			// longer listed by *this* lobby, update/insert the rest.
			// Only items tagged with the responding lobby are touched —
			// entries from other federated lobbies keep their place.
			// This replaces the previous "clear items before refresh"
			// path that caused the list to visibly blink to "Loading
			// sessions..." every 10 s even when nothing had changed.
			std::unordered_set<std::string> presentIds;
			presentIds.reserve(r.sessions.size() * 2);
			for (const auto& s : r.sessions) presentIds.insert(s.sessionId);

			auto& items = st.browser.items;
			items.erase(
				std::remove_if(items.begin(), items.end(),
					[&](const ATNetplay::LobbySession& e) {
						return e.sourceLobby == r.sourceLobby
						    && presentIds.find(e.sessionId) == presentIds.end();
					}),
				items.end());

			// Merge by sessionId so multiple lobbies de-dup.  Filter
			// out our own hostedGames by matching against any of the
			// per-lobby registrations — a game hosted on N lobbies
			// appears N times in the merged list and we want all
			// copies filtered.
			for (auto& s : r.sessions) {
				bool isOwn = false;
				for (auto& own : st.hostedGames) {
					for (const auto& reg : own.lobbyRegistrations) {
						if (reg.sessionId == s.sessionId) {
							isOwn = true; break;
						}
					}
					if (isOwn) break;
				}
				if (isOwn) continue;
				s.sourceLobby = r.sourceLobby;
				bool found = false;
				for (auto& exist : st.browser.items) {
					if (exist.sessionId == s.sessionId) {
						exist = s; found = true; break;
					}
				}
				if (!found) st.browser.items.push_back(std::move(s));
			}
			char buf[64];
			std::snprintf(buf, sizeof buf, "%zu sessions",
				st.browser.items.size());
			st.browser.statusLine  = buf;
			st.browser.lastFetchMs = nowMs;
		});

	// Honour an explicit refresh request (Retry button, hub-open
	// priming, Browser auto-cadence) from any screen.  Previously
	// this was gated on `screen == Browser`, which meant a user who
	// opened Online Play and stayed on the hub never saw the first
	// poll complete — the lobby banner was stuck on "checking..."
	// because the flag sat unacted.
	const uint64_t kAutoRefreshMs = 10000;
	const bool backoffActive =
		st.browser.nextRetryMs != 0 && nowMs < st.browser.nextRetryMs;
	const bool userAsked = st.browser.refreshRequested;
	const bool onBrowser = (st.screen == ATNetplayUI::Screen::Browser);
	const bool timeForAuto = onBrowser &&
		((st.browser.lastFetchMs == 0)
		 || (nowMs - st.browser.lastFetchMs) >= kAutoRefreshMs);
	if (userAsked || (!backoffActive && timeForAuto)) {
		if (!st.browser.refreshInFlight) {
			// Do not wipe items here — the response handler reconciles
			// per-lobby (entries gone from the new response are retired,
			// entries still present are updated in-place).  A pre-wipe
			// caused the list to visibly flash "Loading sessions…" every
			// 10 s even when the set hadn't changed.
			ATNetplayUI::ATNetplayUI_EnqueueBrowserRefresh();
			// Piggy-back stats fan-out on the same cadence: one
			// extra cheap GET /v1/stats per lobby per refresh.
			// Within Oracle Free Tier rate limits (60/min cap;
			// browse=6/min, stats=6/min, well under).
			ATNetplayUI::ATNetplayUI_EnqueueStatsRefresh();
			st.browser.refreshRequested = false;
		}
	}

	// Drive the multi-offer state machine.
	ATNetplayUI::ReconcileHostedGames(nowMs);

	// Connection-mode edge detection.  Whichever coordinator is the
	// active one (joiner during SnapshotReady/Lockstepping or the
	// host offer that has a peer in lockstep) reports its current
	// PeerPath; we cache the last value in Session::connectionMode
	// and fire a toast on transitions.  Important UX:
	//   None → Direct  → silent (the existing "Connected" toast in
	//                    RenderInSessionHUD already covers this)
	//   None → Relay   → "Connected via relay (extra latency)"
	//   Direct → Relay → "Direct connection lost — using relay"
	//                    (mid-session rescue path)
	//   Relay → Direct → "Direct connection restored" (rare but nice)
	// The toast is one-shot per transition; the persistent indicator
	// is the HUD pip rendered by RenderInSessionHUD.
	{
		using PP = ATNetplayGlue::PeerPath;
		PP cur  = ATNetplayGlue::ActivePeerPath();
		PP prev = st.session.connectionMode;
		if (cur != prev) {
			st.session.connectionMode = cur;
			using TS = ATNetplayUI::ToastSeverity;
			if (prev == PP::None && cur == PP::Relay) {
				ATNetplayUI::PushToast(
					"Connected via relay — extra latency expected.",
					TS::Warning, 5000);
			} else if (prev == PP::Direct && cur == PP::Relay) {
				ATNetplayUI::PushToast(
					"Direct connection lost — switched to relay.",
					TS::Warning, 5000);
			} else if (prev == PP::Relay && cur == PP::Direct) {
				ATNetplayUI::PushToast(
					"Direct connection restored.",
					TS::Success, 3000);
			}
			// None → Direct intentionally silent: the
			// "Connected — you are playing online." toast in
			// RenderInSessionHUD already covers the happy path.
		}
	}

	// Joiner flow: when we receive SnapshotReady, write the game-file
	// bytes to a cache path and enqueue a cold-boot deferred action.
	// The deferred callback calls ATNetplayUI_JoinerSnapshotApplied()
	// which acks the coordinator → Lockstepping.
	using GP = ATNetplayGlue::Phase;
	GP jp = ATNetplayGlue::JoinPhase();
	static bool s_joinerApplyQueued = false;
	if (jp == GP::SnapshotReady && !s_joinerApplyQueued) {
		const uint8_t *data = nullptr;
		size_t len = 0;
		ATNetplayGlue::GetReceivedSnapshot(&data, &len);
		if (data && len > 0) {
			// Write to $configdir/netplay_inbound<ext> so ATSimulator
			// content-sniffs by extension.
			auto bc = ATNetplayGlue::JoinBootConfig();
			char extBuf[16] = {};
			strncpy(extBuf, bc.gameExtension, 8);
			if (extBuf[0] == 0) strcpy(extBuf, ".bin");
			VDStringA p = ATGetConfigDir();
			if (!p.empty() && p.back() != '/' && p.back() != '\\')
				p.push_back('/');
			p.append("netplay_inbound");
			p.append(extBuf);
			try {
				VDFileStream fs(VDTextU8ToW(p.c_str(), -1).c_str(),
					nsVDFile::kWrite | nsVDFile::kCreateAlways
					| nsVDFile::kDenyAll);
				fs.Write(data, (sint32)len);
			} catch (...) {
				/* surfaces via snapshot-apply failure below */
			}
			ATUIPushDeferred(kATDeferred_NetplayJoinerApply,
				p.c_str(), 0);
			s_joinerApplyQueued = true;
		}
	}
	if (jp != GP::SnapshotReady && jp != GP::ReceivingSnapshot) {
		s_joinerApplyQueued = false;
	}

	// Track aggregate session-active for menu/HUD.
	st.sessionActive = ATNetplayGlue::IsActive();
}

void ATNetplayUI_OpenBrowser() {
	auto& st = ATNetplayUI::GetState();
	if (st.prefs.nickname.empty() && !st.prefs.isAnonymous)
		ATNetplayUI::Navigate(ATNetplayUI::Screen::Nickname);
	else
		ATNetplayUI::Navigate(ATNetplayUI::Screen::Browser);
	st.browser.refreshRequested = true;
}

void ATNetplayUI_OpenPrefs() {
	ATNetplayUI::Navigate(ATNetplayUI::Screen::Prefs);
	ATNetplayUI::FocusOnceNextFrame(5001);
}

void ATNetplayUI_OpenMyHostedGames() {
	// Historical name — this is the single "open Online Play" entry
	// point shared by the main menu, the hamburger menu, and the
	// Gaming Mode boot-row button.
	//
	//   Gaming Mode → lands on the Online Play hub (Host / Browse /
	//                 Preferences cards) per the nested-navigation
	//                 pattern that matches Settings.
	//   Desktop     → lands directly on Host Games (the traditional
	//                 desktop entry; the menu bar already has Browse
	//                 / Preferences entries, so a hub would be
	//                 redundant).
	auto& st = ATNetplayUI::GetState();
	if (st.prefs.nickname.empty() && !st.prefs.isAnonymous) {
		ATNetplayUI::Navigate(ATNetplayUI::Screen::Nickname);
	} else if (ATUIIsGamingMode()) {
		ATNetplayUI::Navigate(ATNetplayUI::Screen::OnlinePlayHub);
	} else {
		ATNetplayUI::Navigate(ATNetplayUI::Screen::MyHostedGames);
	}
}

void ATNetplayUI_EndSession() {
	// "End Session" from the menu means: stop joining AND disable
	// every currently-running offer.  User keeps the list around.
	ATNetplayGlue::StopJoin();
	for (auto& o : ATNetplayUI::GetState().hostedGames) {
		ATNetplayUI::DisableHostedGame(o.id);
	}
	// Tear down the canonical netplay profile immediately rather
	// than waiting for the next ReconcileHostedGames tick to catch
	// the activity edge.  Makes the menu click feel synchronous and
	// guarantees the user's pre-session profile is restored before
	// any later poll could observe a transient phase.  Idempotent.
	ATNetplayProfile::EndSession();
	ATNetplayUI::Navigate(ATNetplayUI::Screen::MyHostedGames);
}

namespace ATNetplayUI { bool DesktopDispatch(); }

namespace ATNetplayUI {

// Top-right in-session overlay.  Visible whenever any coordinator is
// lockstepping.  Shows frame counter, input delay, peer packet age,
// desync status, and a Disconnect button.  No window chrome, no
// saved settings — just a small translucent panel pinned to the
// viewport's top-right corner.
// The in-session HUD uses a fixed dark overlay background (like a
// broadcast/streaming overlay) rather than following the theme window
// background.  This keeps the colored status dot, the DESYNC/Waiting
// inline text, and the dimmed metrics row legible in both light and
// dark themes — a translucent window that tracks the theme makes the
// metrics unreadable when the underlying game frame is bright.
static ImVec4 HudPanelBg()     { return ImVec4(0.08f, 0.09f, 0.12f, 0.82f); }
static ImVec4 HudPanelBorder() { return ImVec4(1.00f, 1.00f, 1.00f, 0.18f); }
static ImVec4 HudTextPrimary() { return ImVec4(0.96f, 0.96f, 0.98f, 1.00f); }
static ImVec4 HudTextMuted()   { return ImVec4(0.72f, 0.74f, 0.80f, 1.00f); }

// Status colors tuned to read well on the dark HUD panel.  Consistent
// across themes so a glance at the dot always means the same thing.
static ImVec4 StatusColorGood() { return ImVec4(0.40f, 0.92f, 0.45f, 1.0f); }
static ImVec4 StatusColorBad()  { return ImVec4(1.00f, 0.42f, 0.42f, 1.0f); }
static ImVec4 StatusColorWarn() { return ImVec4(1.00f, 0.78f, 0.30f, 1.0f); }

void RenderInSessionHUD() {
	// One-shot "you are live" toast on lockstep entry so both host
	// and joiner get positive confirmation that the session started,
	// even with the HUD disabled.  Re-arms when lockstep drops.
	static bool s_liveToastShown = false;
	if (ATNetplayGlue::IsLockstepping() && !s_liveToastShown) {
		PushToast("Connected — you are playing online.",
			ToastSeverity::Success, 3000);
		s_liveToastShown = true;
	}
	if (!ATNetplayGlue::IsLockstepping()) s_liveToastShown = false;

	// Resync banner.  While a mid-session state transfer is in flight,
	// both peers' sims are paused — show a modal overlay so the user
	// understands the freeze.  Positioned as a top-bar banner rather
	// than a full-screen dim so the game view underneath is still
	// visible (helpful for "did my input land?" intuition).
	static bool s_resyncToastArmed = true;   // re-armed when not resyncing
	{
		uint32_t recv = 0, expected = 0, acked = 0, total = 0;
		if (ATNetplayGlue::IsResyncing(&recv, &expected, &acked, &total)) {
			if (s_resyncToastArmed) {
				PushToast("Resynchronizing with peer…",
					ToastSeverity::Info, 2500);
				s_resyncToastArmed = false;
			}
			const uint32_t num = expected ? recv  : acked;
			const uint32_t den = expected ? expected : total;
			char line[80];
			if (den > 0) {
				std::snprintf(line, sizeof line,
					"Resynchronizing with peer… (%u / %u)",
					(unsigned)num, (unsigned)den);
			} else {
				std::snprintf(line, sizeof line,
					"Resynchronizing with peer…");
			}

			const ImVec2 safeO = ATTouchSafeOrigin();
			const ImVec2 safeS = ATTouchSafeSize();
			const float w = 420.0f;
			ImGui::SetNextWindowPos(
				ImVec2(safeO.x + safeS.x * 0.5f,
				       safeO.y + ATTouchDp(12.0f)),
				ImGuiCond_Always, ImVec2(0.5f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(w, 0), ImGuiCond_Always);
			ImGui::PushStyleColor(ImGuiCol_WindowBg,
				ImVec4(0.08f, 0.08f, 0.12f, 0.92f));
			ImGui::Begin("##NetplayResyncBanner", nullptr,
				ImGuiWindowFlags_NoTitleBar |
				ImGuiWindowFlags_NoResize |
				ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoSavedSettings |
				ImGuiWindowFlags_NoFocusOnAppearing |
				ImGuiWindowFlags_NoNav |
				ImGuiWindowFlags_AlwaysAutoResize);
			ImGui::TextUnformatted(line);
			if (den > 0) {
				ImGui::ProgressBar((float)num / (float)den,
					ImVec2(-1, 0), "");
			}
			ImGui::End();
			ImGui::PopStyleColor();
		} else {
			// Re-arm the toast for the next resync event.
			s_resyncToastArmed = true;
		}
	}

	// Always poll desync state even when the HUD is hidden — we want
	// the one-shot toast regardless of the user's HUD preference so
	// they can see that something went wrong without the diagnostics
	// overlay enabled.
	static bool s_desyncToastShown = false;
	{
		int64_t df = -1;
		const bool desyncedNow = ATNetplayGlue::IsLockstepping()
			&& ATNetplayGlue::IsDesynced(&df);
		if (desyncedNow && !s_desyncToastShown) {
			char msg[128];
			std::snprintf(msg, sizeof msg,
				"Desync detected at frame %lld — ending session.",
				(long long)df);
			PushToast(msg, ToastSeverity::Danger, 6000);
			s_desyncToastShown = true;
		}
		if (!ATNetplayGlue::IsLockstepping()) {
			// Arm the toast for the next session.
			s_desyncToastShown = false;
		}
	}

	if (!ATNetplayGlue::IsLockstepping()) return;

	const uint64_t nowMs = (uint64_t)SDL_GetTicks();
	const uint64_t peerAgeMs = ATNetplayGlue::MsSinceLastPeerPacket(nowMs);

	// Peer-unresponsive prompt.  The coordinator used to auto-end at
	// 10 s; users reported that as abrupt and — worse — it fired even
	// when the remote player had deliberately paused.  Now: at 3 s of
	// silence we show a modal on *both* sides (either peer's pause
	// freezes the other) with Keep Waiting / End Session.  The modal
	// auto-dismisses if the peer comes back.  A single per-session
	// "dismiss" latch lets a user click Keep Waiting and not see the
	// dialog re-pop every frame; we re-arm once the peer returns.
	static bool s_peerStallDismissed = false;
	if (peerAgeMs < 3000 || peerAgeMs >= UINT64_MAX / 4) {
		s_peerStallDismissed = false;
	}
	const bool showStall = (peerAgeMs >= 3000)
		&& (peerAgeMs < UINT64_MAX / 4)
		&& !s_peerStallDismissed;
	if (showStall) {
		const char *kStallId = "Peer unresponsive##netplay_stall";
		if (!ImGui::IsPopupOpen(kStallId)) ImGui::OpenPopup(kStallId);
		// Centre within the platform safe area so notches / gesture
		// pills don't clip the modal on mobile.
		const ImVec2 stallO = ATTouchSafeOrigin();
		const ImVec2 stallS = ATTouchSafeSize();
		ImGui::SetNextWindowPos(
			ImVec2(stallO.x + stallS.x * 0.5f,
			       stallO.y + stallS.y * 0.5f),
			ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(420, 0),
			ImGuiCond_Appearing);
		if (ImGui::BeginPopupModal(kStallId, nullptr,
				ImGuiWindowFlags_NoSavedSettings
				| ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::PushTextWrapPos(400);
			ImGui::TextUnformatted(
				"The other player has stopped sending input — they may "
				"have paused the game, tabbed away, or lost the network "
				"connection.  The game will stay paused until they "
				"return.");
			ImGui::Spacing();
			ImGui::Text("No input received for %llu seconds.",
				(unsigned long long)(peerAgeMs / 1000));
			ImGui::PopTextWrapPos();
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
			const float bw = (ImGui::GetContentRegionAvail().x - 10.0f) * 0.5f;
			if (ImGui::Button("Keep Waiting", ImVec2(bw, 0))) {
				s_peerStallDismissed = true;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine(0, 10);
			ImGui::PushStyleColor(ImGuiCol_Button,
				ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
			if (ImGui::Button("End Session", ImVec2(bw, 0))) {
				ATNetplayGlue::DisconnectActive();
				ImGui::CloseCurrentPopup();
			}
			ImGui::PopStyleColor();
			ImGui::EndPopup();
		}
	} else {
		// Peer came back — close any lingering popup.
		if (ImGui::IsPopupOpen("Peer unresponsive##netplay_stall")) {
			ImGui::CloseCurrentPopup();
		}
	}

	if (!GetState().prefs.showSessionHUD) return;

	const uint32_t delay = ATNetplayGlue::CurrentInputDelay();
	int64_t desyncFrame = -1;
	const bool desynced = ATNetplayGlue::IsDesynced(&desyncFrame);
	const bool peerKnown = peerAgeMs < UINT64_MAX / 4;
	const bool peerLate  = peerKnown && peerAgeMs > 500;

	// Status category drives the dot colour and short label.
	enum class Status { Live, Waiting, Desync };
	Status status = Status::Live;
	if (desynced)       status = Status::Desync;
	else if (peerLate)  status = Status::Waiting;

	const ImVec4 dotColor =
		status == Status::Desync  ? StatusColorBad()  :
		status == Status::Waiting ? StatusColorWarn() :
		                            StatusColorGood();

	const ImVec2 hudO = ATTouchSafeOrigin();
	const ImVec2 hudS = ATTouchSafeSize();
	const float pad = 10.0f;
	ImVec2 pos(hudO.x + hudS.x - pad, hudO.y + pad);
	ImGui::SetNextWindowPos(pos, ImGuiCond_Always, ImVec2(1.0f, 0.0f));

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
	                       | ImGuiWindowFlags_AlwaysAutoResize
	                       | ImGuiWindowFlags_NoSavedSettings
	                       | ImGuiWindowFlags_NoFocusOnAppearing
	                       | ImGuiWindowFlags_NoNav
	                       | ImGuiWindowFlags_NoMove;

	// Pin the HUD to the fixed dark overlay palette regardless of the
	// active ImGui theme so readability is identical in dark/light.
	ImGui::PushStyleColor(ImGuiCol_WindowBg,  HudPanelBg());
	ImGui::PushStyleColor(ImGuiCol_Border,    HudPanelBorder());
	ImGui::PushStyleColor(ImGuiCol_Text,      HudTextPrimary());
	ImGui::PushStyleVar  (ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleVar  (ImGuiStyleVar_WindowRounding,   6.0f);

	// Shared status dot — drawn via ImDrawList rather than a glyph so
	// we don't depend on the bundled font covering U+25CF.  Reserves a
	// square of em-height on the current line and paints a filled
	// circle centred in it; the caller follows with SameLine + label.
	auto drawDot = [&](const ImVec4& col) {
		const float h = ImGui::GetTextLineHeight();
		const float r = h * 0.32f;
		ImVec2 p0 = ImGui::GetCursorScreenPos();
		ImVec2 c(p0.x + h * 0.5f, p0.y + h * 0.5f);
		ImGui::GetWindowDrawList()->AddCircleFilled(
			c, r, ImGui::GetColorU32(col));
		ImGui::Dummy(ImVec2(h, h));
	};

	const bool gaming = ATUIIsGamingMode();

	if (gaming) {
		// Ultra-minimal mobile pill.  No disconnect button (hamburger
		// → Online Play → End Session handles that); no frame or delay
		// readouts (users don't need them, and every pixel over the
		// game is expensive on a phone).  Two states:
		//   ● 35ms  — peer packets arriving normally
		//   ● Off   — peer silent or desynced
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 5));
		if (ImGui::Begin("##netplay_hud", nullptr,
		                 flags | ImGuiWindowFlags_NoInputs)) {
			drawDot(dotColor);
			ImGui::SameLine(0, 6);
			if (status == Status::Live && peerKnown) {
				ImGui::Text("%llums",
					(unsigned long long)peerAgeMs);
			} else {
				ImGui::TextUnformatted("Off");
			}
		}
		ImGui::End();
		ImGui::PopStyleVar();
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(3);
		return;
	}

	// Desktop: single-row status pill, followed by a compact metrics
	// line and a small Disconnect button.  Frame counter is intentionally
	// omitted on the happy path — it's not actionable and dominates the
	// readout.  DESYNC surfaces the frame inline because it is.
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(8, 4));
	if (ImGui::Begin("##netplay_hud", nullptr, flags)) {
		drawDot(dotColor);
		ImGui::SameLine(0, 6);

		switch (status) {
		case Status::Desync:
			ImGui::PushStyleColor(ImGuiCol_Text, StatusColorBad());
			ImGui::Text("DESYNC  @ frame %lld", (long long)desyncFrame);
			ImGui::PopStyleColor();
			break;
		case Status::Waiting:
			ImGui::PushStyleColor(ImGuiCol_Text, StatusColorWarn());
			ImGui::Text("Waiting  (%llus)",
				(unsigned long long)(peerAgeMs / 1000));
			ImGui::PopStyleColor();
			break;
		case Status::Live:
			ImGui::TextUnformatted("Live");
			break;
		}

		// Metrics row: peer packet age + input delay frames.  Use
		// explicit labels ("Peer" / "Delay") so the numbers are not
		// ambiguous; the labels are muted and the numbers are bright
		// so the readout reads as "<label> <value>" at a glance.
		// ASCII only — the bundled font atlas doesn't cover
		// non-Latin-1 glyphs.
		auto labelValue = [](const char* label, const char* value) {
			ImGui::PushStyleColor(ImGuiCol_Text, HudTextMuted());
			ImGui::TextUnformatted(label);
			ImGui::PopStyleColor();
			ImGui::SameLine(0, 4);
			ImGui::PushStyleColor(ImGuiCol_Text, HudTextPrimary());
			ImGui::TextUnformatted(value);
			ImGui::PopStyleColor();
		};
		char peerBuf[32];
		char delayBuf[32];
		if (!peerKnown) {
			std::snprintf(peerBuf, sizeof peerBuf, "--");
		} else {
			std::snprintf(peerBuf, sizeof peerBuf, "%llu ms",
				(unsigned long long)peerAgeMs);
		}
		std::snprintf(delayBuf, sizeof delayBuf, "%u %s",
			(unsigned)delay, delay == 1 ? "frame" : "frames");

		labelValue("Peer", peerBuf);
		ImGui::SameLine(0, 12);
		labelValue("Delay", delayBuf);

		// Connection-mode pip: green "D" for Direct, amber "R" for
		// Relay.  Lets the user attribute mid-session stutter to
		// the right cause (relay adds an extra hop's worth of
		// latency) without opening any menu.  Hover for a tooltip
		// that spells out "Direct UDP" / "Via lobby reflector".
		{
			ImGui::SameLine(0, 12);
			using PP = ATNetplayGlue::PeerPath;
			PP path = ATNetplayGlue::ActivePeerPath();
			ImVec4 col;
			const char* glyph;
			const char* tip;
			if (path == PP::Relay) {
				col   = ImVec4(1.00f, 0.78f, 0.30f, 1.0f);
				glyph = "R";
				tip   = "Routed via lobby reflector (extra latency).";
			} else {
				col   = StatusColorGood();
				glyph = "D";
				tip   = "Direct UDP path (lowest latency).";
			}
			ImGui::PushStyleColor(ImGuiCol_Text, HudTextMuted());
			ImGui::TextUnformatted("Path");
			ImGui::PopStyleColor();
			ImGui::SameLine(0, 4);
			ImGui::PushStyleColor(ImGuiCol_Text, col);
			ImGui::TextUnformatted(glyph);
			ImGui::PopStyleColor();
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
		}

		// Connection-unstable early indicator.  The existing 3-s
		// peer-unresponsive modal (above) and the 5-s relay-rescue
		// flip both happen at points where the user is already
		// noticing the stutter; this dot fills the 1-3 s gap with
		// a quiet visual cue so the user knows "something is off"
		// before the modal pops.  Yellow • when silence is
		// 1000-3000 ms; the modal takes over from 3000 ms and the
		// "R" pip above goes amber once the relay rescue engages
		// at 5000 ms.  No new state — derived from peerAgeMs each
		// frame.  Sentinel value MsSinceLastPeerPacket returns
		// (UINT64_MAX/2) when no peer packet has ever arrived
		// (e.g. during ramp-up at lockstep entry); the upper
		// bound here keeps the dot off in that case.
		constexpr uint64_t kPeerSilenceUiWarningMs = 1000;
		constexpr uint64_t kPeerSilenceUiPanicMs   = 3000;
		const bool unstable =
			peerAgeMs >= kPeerSilenceUiWarningMs &&
			peerAgeMs <  kPeerSilenceUiPanicMs;
		if (unstable) {
			ImGui::SameLine(0, 8);
			ImGui::PushStyleColor(ImGuiCol_Text,
				ImVec4(1.00f, 0.85f, 0.25f, 1.0f));   // yellow dot
			ImGui::TextUnformatted("\xe2\x97\x8f");
			ImGui::PopStyleColor();
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip(
					"Connection unstable — peer last seen %llu ms ago.",
					(unsigned long long)peerAgeMs);
			}
		}

		// Compact Disconnect — no longer full-width, just enough to
		// read the label.  Sits on the metrics row's right edge with
		// a subtle, theme-independent tint so it doesn't disappear
		// against the dark HUD background in light mode.
		ImGui::SameLine();
		const float btnW = ImGui::CalcTextSize("Disconnect").x
		                 + ImGui::GetStyle().FramePadding.x * 2.0f;
		const float avail = ImGui::GetContentRegionAvail().x;
		if (avail > btnW) ImGui::SameLine(0, avail - btnW);
		ImGui::PushStyleColor(ImGuiCol_Button,
			ImVec4(0.30f, 0.10f, 0.10f, 0.85f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
			ImVec4(0.55f, 0.15f, 0.15f, 1.00f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive,
			ImVec4(0.70f, 0.20f, 0.20f, 1.00f));
		ImGui::PushStyleColor(ImGuiCol_Text, HudTextPrimary());
		if (ImGui::SmallButton("Disconnect##netplay_hud")) {
			ATNetplayGlue::DisconnectActive();
		}
		ImGui::PopStyleColor(4);
	}
	ImGui::End();
	ImGui::PopStyleVar(2);
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor(3);
}

// Desktop on-screen emote button.  Gaming Mode already draws the
// touch-target version inside ATTouchControls_Render; Desktop had no
// equivalent, so the emote picker was only reachable via F1 / R3 or the
// menu command.  Mirrors the mobile button's visibility rules
// (lockstepping + Send Emotes enabled) and renders on the foreground
// draw list so it floats over the game view regardless of other
// windows.  Positioned at bottom-right of the viewport work area to
// avoid the top-right session HUD and the menu bar.
static void RenderDesktopEmoteButton() {
	if (ATUIIsGamingMode()) return;
	if (!ATNetplayGlue::IsLockstepping()) return;
	if (!ATEmoteNetplay::GetSendEnabled()) return;
	if (ATEmotePicker::IsOpen()) return;

	const ImGuiViewport* vp = ImGui::GetMainViewport();
	const float pad = 16.0f;
	const float sz  = 52.0f;

	ImVec2 pos(vp->WorkPos.x + vp->WorkSize.x - pad - sz,
	           vp->WorkPos.y + vp->WorkSize.y - pad - sz);
	ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(sz, sz), ImGuiCond_Always);

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
	                       | ImGuiWindowFlags_NoSavedSettings
	                       | ImGuiWindowFlags_NoFocusOnAppearing
	                       | ImGuiWindowFlags_NoNav
	                       | ImGuiWindowFlags_NoMove
	                       | ImGuiWindowFlags_NoBackground;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	if (ImGui::Begin("##netplay_emote_btn", nullptr, flags)) {
		// Button body.
		ImGui::PushStyleColor(ImGuiCol_Button,
			ImVec4(0.08f, 0.09f, 0.12f, 0.82f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
			ImVec4(0.16f, 0.20f, 0.28f, 0.95f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive,
			ImVec4(0.24f, 0.32f, 0.44f, 1.00f));
		ImGui::PushStyleColor(ImGuiCol_Border,
			ImVec4(1.00f, 1.00f, 1.00f, 0.18f));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,  8.0f);

		const bool clicked =
			ImGui::Button("##netplay_emote_btn_inner", ImVec2(sz, sz));

		// Speech-bubble glyph drawn procedurally — matches the mobile
		// button so the visual identity is consistent between modes
		// without depending on an emoji font.
		ImVec2 p = ImGui::GetItemRectMin();
		ImVec2 q = ImGui::GetItemRectMax();
		float cx = 0.5f * (p.x + q.x);
		float cy = 0.5f * (p.y + q.y);
		float w = (q.x - p.x) * 0.55f;
		float h = (q.y - p.y) * 0.45f;
		ImU32 glyph = IM_COL32(240, 240, 244, 230);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRect(ImVec2(cx - w * 0.5f, cy - h * 0.5f),
		            ImVec2(cx + w * 0.5f, cy + h * 0.3f),
		            glyph, 4.0f, 0, 2.0f);
		dl->AddTriangleFilled(
			ImVec2(cx - w * 0.15f, cy + h * 0.3f),
			ImVec2(cx + w * 0.15f, cy + h * 0.3f),
			ImVec2(cx - w * 0.05f, cy + h * 0.55f),
			glyph);
		float dr = 1.8f;
		dl->AddCircleFilled(ImVec2(cx - w * 0.22f, cy - h * 0.1f), dr, glyph);
		dl->AddCircleFilled(ImVec2(cx,             cy - h * 0.1f), dr, glyph);
		dl->AddCircleFilled(ImVec2(cx + w * 0.22f, cy - h * 0.1f), dr, glyph);

		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(4);

		if (clicked) ATEmotePicker::Open();

		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Send emoticon (F1 / R3)");
		}
	}
	ImGui::End();
	ImGui::PopStyleVar();
}

} // namespace ATNetplayUI

void ATNetplayUI_RenderDesktop(ATSimulator &, ATUIState &, SDL_Window *) {
	if (ATUIIsGamingMode()) return;
	ATNetplayUI::DesktopDispatch();
	ATNetplayUI::RenderInSessionHUD();
	ATNetplayUI::RenderDesktopEmoteButton();
	// Note: toasts are now painted at the top-level frame loop via
	// ATTouchRenderToasts() so they remain visible even when the
	// Online Play overlay is Closed (e.g. "session ended — your
	// previous game was restored").
}

bool ATNetplayUI_RenderMobile(ATSimulator &, ATUIState &,
                              ATMobileUIState &, SDL_Window *) {
	bool r = ATNetplayUI::ATNetplayUI_DispatchScreen();
	ATNetplayUI::RenderInSessionHUD();
	// See note above — toast rendering is hoisted to the frame loop.
	return r;
}

bool ATNetplayUI_IsActive() {
	return ATNetplayUI::GetState().screen != ATNetplayUI::Screen::Closed;
}

void ATNetplayUI_Notify(const char *title, const char *body) {
	auto& st = ATNetplayUI::GetState();
	ATNetplay::Notify(title, body, st.prefs.notif);
}

// Called from the kATDeferred_NetplayJoinerApply handler after
// g_sim.Load+Resume succeed.  Acks the coordinator so it advances to
// Lockstepping.
void ATNetplayUI_JoinerSnapshotApplied() {
	ATNetplayGlue::AcknowledgeSnapshotApplied();
	ATNetplayUI_Notify("Playing Online",
		"Snapshot applied — you are now in the session.");
}

// C-linkage trampoline so ui_main.cpp's deferred handler can call the
// ATNetplayUI::-namespaced implementation without pulling the header.
void ATNetplayUI_SubmitHostGameFileForGame(const char *gameId) {
	ATNetplayUI::SubmitHostGameFileForGame(gameId);
}

void ATNetplayUI_HostBootFailed(const char *gameId, const char *reason) {
	if (!gameId || !*gameId) return;
	g_ATLCNetplay("host boot failed for \"%s\": %s", gameId,
		reason && *reason ? reason : "(no reason)");
	auto& st = ATNetplayUI::GetState();
	ATNetplayUI::HostedGame* o = ATNetplayUI::FindHostedGame(gameId);
	std::string name = o ? o->gameName : std::string("(offer)");
	if (o) {
		o->lastError     = reason && *reason ? reason : "Host boot failed";
		o->snapshotQueued = false;
	}
	// DisableHostedGame is the canonical full-teardown path: it flips
	// `enabled` off (preventing ReconcileHostedGames from immediately
	// re-StartCoord-ing into a forever loop), posts a Delete to every
	// lobby this offer is registered with, stops the coord, and saves
	// to registry.  Calling ATNetplayGlue::StopHost directly here was
	// a leak — without the lobby Delete, the failed session stayed
	// listed; a subsequent Enable + retry would post a NEW lobby
	// Create and the original entry would persist as a phantom
	// "in play" / "waiting" row in the browser until its
	// server-side liveness window expired (multiple minutes).  Two
	// failed retries → three rows visible (the original symptom).
	ATNetplayUI::DisableHostedGame(std::string(gameId));

	char msg[256];
	std::snprintf(msg, sizeof msg,
		"%s: %s",
		name.c_str(),
		reason && *reason ? reason : "boot failed");
	ATNetplayUI_Notify("Host error", msg);

	// Surface a persistent error screen too — notifications are easy
	// to miss if the user is in another window.
	st.session.lastError = msg;
	ATNetplayUI::SaveToRegistry();
	(void)st;
}

void ATNetplayUI_JoinerSnapshotFailed(const char *reason) {
	auto& st = ATNetplayUI::GetState();
	g_ATLCNetplay("joiner snapshot apply failed: %s",
		reason && *reason ? reason : "(no reason)");
	// Tear down the joiner coord — no ack will come, so without this
	// the coord stays in SnapshotReady forever.
	ATNetplayGlue::StopJoin();

	std::string body = reason && *reason
		? std::string("Could not load the received game state: ") + reason
		: std::string("Could not load the received game state.");
	ATNetplayUI_Notify("Join failed", body.c_str());

	st.session.lastError = body;
	// Pop the Waiting modal and land on the Error screen so the user
	// sees a persistent, acknowledgeable explanation.
	while (!st.backStack.empty()) st.backStack.pop_back();
	ATNetplayUI::Navigate(ATNetplayUI::Screen::Error);
}
