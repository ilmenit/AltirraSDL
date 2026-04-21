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
#include "netplay/packets.h"

#include "ui/core/ui_mode.h"
#include "ui/core/ui_main.h"

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

void ATNetplayUI_Initialize(SDL_Window *window) {
	ATNetplayUI::Initialize();
	ATNetplay::SetWindow(window);
	ATNetplay::Initialize("AltirraSDL");
	ATNetplayUI::GetWorker().Start();
	ATNetplayUI::Activity_Hook();
}

void ATNetplayUI_Shutdown() {
	ATNetplayUI::Activity_Unhook();
	// Best-effort Delete + coordinator teardown for every offer.
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
					ATNetplayUI::HostedGame::LobbyRegistration reg;
					reg.section   = r.sourceLobby;
					reg.sessionId = r.create.sessionId;
					reg.token     = r.create.token;
					// De-dup on section (a quick Enable/Disable/Enable
					// cycle could leave a stale entry).
					bool replaced = false;
					for (auto& e : o->lobbyRegistrations) {
						if (e.section == reg.section) {
							e = std::move(reg);
							replaced = true;
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
				// ReconcileHostedGames arms the next heartbeat on its
				// own cadence; only log failures so we surface lobby
				// outages without spamming every 30 s tick.
				if (!r.ok) {
					g_ATLCNetplay("lobby Heartbeat FAILED: status=%d raw=\"%s\"",
						r.httpStatus,
						r.error.empty() ? "(no detail)"
						                : r.error.c_str());
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
	ATNetplayUI::Navigate(ATNetplayUI::Screen::MyHostedGames);
}

namespace ATNetplayUI { bool DesktopDispatch(); }

namespace ATNetplayUI {

// Top-right in-session overlay.  Visible whenever any coordinator is
// lockstepping.  Shows frame counter, input delay, peer packet age,
// desync status, and a Disconnect button.  No window chrome, no
// saved settings — just a small translucent panel pinned to the
// viewport's top-right corner.
// Theme-aware status palette.  Dark theme can use high-value pastel
// tones but they wash out on light backgrounds; light theme wants
// deeper/saturated ones to stay readable.  Kept local so it matches
// the existing OfferStateColour palette choices in ui_netplay_desktop.
static ImVec4 StatusColorGood() {
	return ATUIIsDarkTheme()
		? ImVec4(0.55f, 0.95f, 0.55f, 1.0f)
		: ImVec4(0.10f, 0.55f, 0.15f, 1.0f);
}
static ImVec4 StatusColorBad() {
	return ATUIIsDarkTheme()
		? ImVec4(1.00f, 0.45f, 0.45f, 1.0f)
		: ImVec4(0.80f, 0.10f, 0.10f, 1.0f);
}
static ImVec4 StatusColorWarn() {
	return ATUIIsDarkTheme()
		? ImVec4(1.00f, 0.70f, 0.30f, 1.0f)
		: ImVec4(0.75f, 0.45f, 0.05f, 1.0f);
}

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
		ImGui::SetNextWindowPos(
			ImGui::GetMainViewport()->GetCenter(),
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

	const uint32_t frame = ATNetplayGlue::CurrentFrame();
	const uint32_t delay = ATNetplayGlue::CurrentInputDelay();
	int64_t desyncFrame = -1;
	const bool desynced = ATNetplayGlue::IsDesynced(&desyncFrame);

	const ImGuiViewport* vp = ImGui::GetMainViewport();
	const float pad = 10.0f;
	ImVec2 pos(vp->WorkPos.x + vp->WorkSize.x - pad,
	           vp->WorkPos.y + pad);
	ImGui::SetNextWindowPos(pos, ImGuiCond_Always, ImVec2(1.0f, 0.0f));
	ImGui::SetNextWindowBgAlpha(0.70f);

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
	                       | ImGuiWindowFlags_AlwaysAutoResize
	                       | ImGuiWindowFlags_NoSavedSettings
	                       | ImGuiWindowFlags_NoFocusOnAppearing
	                       | ImGuiWindowFlags_NoNav
	                       | ImGuiWindowFlags_NoMove;

	if (ImGui::Begin("##netplay_hud", nullptr, flags)) {
		if (desynced) {
			ImGui::PushStyleColor(ImGuiCol_Text, StatusColorBad());
			ImGui::TextUnformatted("DESYNC");
			ImGui::PopStyleColor();
			ImGui::SameLine();
			ImGui::Text("@ frame %lld", (long long)desyncFrame);
		} else if (peerAgeMs > 500 && peerAgeMs < UINT64_MAX / 4) {
			// Peer is late.  The game is paused on both sides (our
			// gate is closed waiting for their input).  We no longer
			// auto-terminate — see the Peer Unresponsive dialog below,
			// which appears at ~3 s and lets the user choose.
			ImGui::PushStyleColor(ImGuiCol_Text, StatusColorWarn());
			ImGui::Text("WAITING  (%llus)",
				(unsigned long long)(peerAgeMs / 1000));
			ImGui::PopStyleColor();
		} else {
			ImGui::PushStyleColor(ImGuiCol_Text, StatusColorGood());
			ImGui::TextUnformatted("LIVE");
			ImGui::PopStyleColor();
		}

		ImGui::Text("Frame: %u", (unsigned)frame);
		ImGui::Text("Delay: %u f", (unsigned)delay);

		if (peerAgeMs >= UINT64_MAX / 4) {
			ImGui::TextUnformatted("Peer:  —");
		} else {
			// Colour the age line red once we're past a plausible
			// "they're lagging" threshold so the user gets an at-a-
			// glance health read without a graph.
			const bool slow = peerAgeMs > 500;
			if (slow) ImGui::PushStyleColor(ImGuiCol_Text, StatusColorWarn());
			ImGui::Text("Peer:  %llu ms ago",
				(unsigned long long)peerAgeMs);
			if (slow) ImGui::PopStyleColor();
		}

		ImGui::Spacing();
		if (ImGui::Button("Disconnect##netplay_hud",
		                  ImVec2(-FLT_MIN, 0))) {
			ATNetplayGlue::DisconnectActive();
		}
	}
	ImGui::End();
}

} // namespace ATNetplayUI

void ATNetplayUI_RenderDesktop(ATSimulator &, ATUIState &, SDL_Window *) {
	if (ATUIIsGamingMode()) return;
	ATNetplayUI::DesktopDispatch();
	ATNetplayUI::RenderInSessionHUD();
	// Toasts paint on the foreground draw list so they float above
	// every Online Play window without caller bookkeeping.
	ATNetplayUI::RenderToasts();
}

bool ATNetplayUI_RenderMobile(ATSimulator &, ATUIState &,
                              ATMobileUIState &, SDL_Window *) {
	bool r = ATNetplayUI::ATNetplayUI_DispatchScreen();
	ATNetplayUI::RenderInSessionHUD();
	ATNetplayUI::RenderToasts();
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
		o->lastError = reason && *reason ? reason : "Host boot failed";
		// Flip enabled off so ReconcileHostedGames doesn't immediately
		// re-StartCoord and loop forever.  User must manually
		// re-enable after fixing whatever's wrong (e.g. install
		// firmware, adjust hardware mode).
		o->enabled        = false;
		o->snapshotQueued = false;
	}
	// Tear down the coord so the joiner sees the connection drop
	// instead of hanging in Handshaking / ReceivingSnapshot.
	ATNetplayGlue::StopHost(gameId);

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
