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

#include <cstdio>

#include <at/atcore/logging.h>
extern ATLogChannel g_ATLCNetplay;

extern VDStringA ATGetConfigDir();

namespace ATNetplayUI {

// Screen dispatcher implemented in ui_netplay_screens.cpp.
bool ATNetplayUI_DispatchScreen();
void ATNetplayUI_EnqueueBrowserRefresh();

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
			// Record cross-window lobby reachability — every op
			// updates this so Browse and Host Games show a consistent
			// status without each window polling on its own.
			if (r.ok) {
				st.lobbyHealth.lastOkMs   = nowMs;
				st.lobbyHealth.lastStatus = r.httpStatus;
				st.lobbyHealth.lastError.clear();
			} else {
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
			if (r.op != ATNetplayUI::LobbyOp::List) return;
			if (!r.ok) {
				st.browser.statusLine = st.lobbyHealth.lastError
					+ " - use Direct IP in Preferences";
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

	// Auto-refresh cadence while the browser is visible.
	const uint64_t kAutoRefreshMs = 10000;
	if (st.screen == ATNetplayUI::Screen::Browser) {
		const bool backoffActive =
			st.browser.nextRetryMs != 0 && nowMs < st.browser.nextRetryMs;
		const bool userAsked = st.browser.refreshRequested;
		const bool timeForAuto = (st.browser.lastFetchMs == 0)
		    || (nowMs - st.browser.lastFetchMs) >= kAutoRefreshMs;
		if (userAsked || (!backoffActive && timeForAuto)) {
			if (!st.browser.refreshInFlight) {
				st.browser.items.clear();
				ATNetplayUI::ATNetplayUI_EnqueueBrowserRefresh();
				st.browser.refreshRequested = false;
			}
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
	auto& st = ATNetplayUI::GetState();
	if (st.prefs.nickname.empty() && !st.prefs.isAnonymous)
		ATNetplayUI::Navigate(ATNetplayUI::Screen::Nickname);
	else
		ATNetplayUI::Navigate(ATNetplayUI::Screen::MyHostedGames);
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
	if (!ATNetplayGlue::IsLockstepping()) return;
	if (!GetState().prefs.showSessionHUD) return;

	const uint64_t nowMs = (uint64_t)SDL_GetTicks();
	const uint32_t frame = ATNetplayGlue::CurrentFrame();
	const uint32_t delay = ATNetplayGlue::CurrentInputDelay();
	int64_t desyncFrame = -1;
	const bool desynced = ATNetplayGlue::IsDesynced(&desyncFrame);
	const uint64_t peerAgeMs = ATNetplayGlue::MsSinceLastPeerPacket(nowMs);

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
			// gate is closed waiting for their input).  Show a
			// countdown to the 10 s timeout so the user knows how
			// long we'll wait before declaring the peer dead.
			constexpr uint64_t kTimeoutMs = 10000;
			ImGui::PushStyleColor(ImGuiCol_Text, StatusColorWarn());
			if (peerAgeMs >= kTimeoutMs) {
				ImGui::TextUnformatted("TIMEOUT");
			} else {
				const uint64_t remainS = (kTimeoutMs - peerAgeMs + 999) / 1000;
				ImGui::Text("WAITING  (timeout in %llus)",
					(unsigned long long)remainS);
			}
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
}

bool ATNetplayUI_RenderMobile(ATSimulator &, ATUIState &,
                              ATMobileUIState &, SDL_Window *) {
	bool r = ATNetplayUI::ATNetplayUI_DispatchScreen();
	ATNetplayUI::RenderInSessionHUD();
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
