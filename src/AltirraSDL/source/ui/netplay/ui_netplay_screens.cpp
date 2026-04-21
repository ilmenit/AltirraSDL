//	AltirraSDL - Online Play screens
//
//	All six netplay screens live in a single file because they share
//	common helpers (save-prefs-on-close, keyboard-focus stampede
//	prevention) and their bodies are small.  Each draws into a
//	mode-agnostic BeginSheet() container so the same code path works
//	on Desktop and Gaming Mode.
//
//	Navigation:
//	  Escape / Gamepad B       → Back()
//	  Enter  / Gamepad A       → primary action (context-dependent)
//	  Tab / D-pad              → ImGui's native nav handles it
//
//	Reusable primitives (SessionTile, StatusBadge, PeerChip) come
//	from ui_netplay_widgets.  No bare ImGui::Button / InputText calls
//	here — everything goes through the touch-friendly wrappers so a
//	finger on Android gets the same target size as a mouse on
//	Windows.

#include <stdafx.h>

#include "ui_netplay_state.h"
#include "ui_netplay_widgets.h"
#include "ui_netplay.h"
#include "ui_netplay_lobby_worker.h"
#include "ui_netplay_actions.h"

#include "input/touch_widgets.h"
#include "ui/core/ui_mode.h"

#include "netplay/netplay_glue.h"
#include "netplay/lobby_config.h"
#include "netplay/platform_notify.h"

#include "ui/gamelibrary/game_library.h"
#include "ui_file_dialog_sdl3.h"
#include <SDL3/SDL_dialog.h>

extern SDL_Window *g_pWindow;

#include <vd2/system/registry.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ATNetplayUI {

// Defined in ui_netplay.cpp.  Module-level (external linkage) so
// this TU and that TU link the same symbol.
LobbyWorker& GetWorker();

namespace {

// -------------------------------------------------------------------
// Nickname prompt
// -------------------------------------------------------------------

void RenderNickname() {
	State& st = GetState();

	bool open = true;
	if (!BeginSheet("Choose a Nickname", &open,
	                ImVec2(Dp(360), Dp(260)),
	                ImVec2(Dp(560), Dp(420)))) {
		return;
	}

	if (ScreenHeader("Choose a Nickname")) {
		// Nickname is the first-time gate — "back" means "close
		// Online Play" since there's no screen behind it to pop to.
		Navigate(Screen::Closed);
	}

	ATTouchSection("Your nickname");
	ATTouchMutedText(
		"Other players will see this when you host or join a game.  "
		"Pick something 1-24 characters.");

	ImGui::Spacing();

	static char buf[32] = {};
	// Seed on first visit each time the screen opens.
	if (ConsumeFocusRequest(1001)) {
		std::snprintf(buf, sizeof buf, "%s",
			st.prefs.nickname.c_str());
		ImGui::SetKeyboardFocusHere();
	}
	ImGui::PushItemWidth(-FLT_MIN);
	ImGui::InputText("##handle", buf, sizeof buf);
	ImGui::PopItemWidth();

	ImGui::Spacing();
	bool anon = st.prefs.isAnonymous;
	if (ATTouchToggle("Anonymous (random nickname each session)", &anon)) {
		st.prefs.isAnonymous = anon;
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	float btnW = (ImGui::GetContentRegionAvail().x - Dp(10)) * 0.5f;
	if (ATTouchButton("Cancel", ImVec2(btnW, Dp(48)),
	                  ATTouchButtonStyle::Neutral)) {
		Navigate(Screen::Closed);
	}
	ImGui::SameLine(0, Dp(10));
	bool valid = anon || (buf[0] != 0);
	ImGui::BeginDisabled(!valid);
	if (ATTouchButton("Save", ImVec2(btnW, Dp(48)),
	                  ATTouchButtonStyle::Accent)) {
		if (anon) st.prefs.nickname.clear();
		else      st.prefs.nickname = buf;
		SaveToRegistry();
		// Fall through to the natural landing screen for the mode
		// (hub in Gaming Mode, host-games in Desktop).
		Navigate(ATUIIsGamingMode()
			? Screen::OnlinePlayHub : Screen::MyHostedGames);
		// Request a browser refresh now that we have identity so
		// the hub's subtitle counts reflect reality on first visit.
		st.browser.refreshRequested = true;
	}
	ImGui::EndDisabled();

	if (!open) Navigate(Screen::Closed);

	EndSheet();
}

// -------------------------------------------------------------------
// Online browser
// -------------------------------------------------------------------

// Parse "http://host:port[/...]" into a LobbyEndpoint.  Shared by the
// browser-refresh and stats-refresh fan-out paths.
static ATNetplay::LobbyEndpoint EndpointForLobby(const ATNetplay::LobbyEntry& e) {
	ATNetplay::LobbyEndpoint ep;
	std::string url = e.url;
	const std::string prefix = "http://";
	if (url.compare(0, prefix.size(), prefix) == 0) url.erase(0, prefix.size());
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

void EnqueueBrowserRefresh() {
	const std::vector<ATNetplay::LobbyEntry>& lobbies = GetConfiguredLobbies();
	for (const auto& e : lobbies) {
		if (!e.enabled) continue;
		if (e.kind != ATNetplay::LobbyKind::Http) continue;  // LAN handled separately
		LobbyRequest req{};
		req.op = LobbyOp::List;
		req.endpoint = EndpointForLobby(e);
		GetWorker().Post(std::move(req), e.section);
	}
}

// v2: fan a Stats request out to every enabled HTTP lobby; the result
// pump (ui_netplay.cpp) sums them into State::aggregateStats so the
// Browser footer reflects the whole network.  No-ops if a previous
// cycle's responses haven't all landed yet — without that guard a
// late response from an earlier cycle would fall into the new cycle's
// accumulator and double-count.
void EnqueueStatsRefresh() {
	auto& a = GetState().aggregateStats;
	if (a.pendingResponses > 0) return;

	const std::vector<ATNetplay::LobbyEntry>& lobbies = GetConfiguredLobbies();
	int posted = 0;
	for (const auto& e : lobbies) {
		if (!e.enabled) continue;
		if (e.kind != ATNetplay::LobbyKind::Http) continue;
		LobbyRequest req{};
		req.op = LobbyOp::Stats;
		req.endpoint = EndpointForLobby(e);
		GetWorker().Post(std::move(req), e.section);
		++posted;
	}
	a.pendingResponses = posted;
	a.acc_sessions = 0;
	a.acc_waiting  = 0;
	a.acc_playing  = 0;
	a.acc_hosts    = 0;
}

void RenderBrowser() {
	State& st = GetState();
	Browser& br = st.browser;

	bool open = true;
	if (!BeginSheet("Browse Hosted Games", &open,
	                ImVec2(Dp(560), Dp(420)),
	                ImVec2(Dp(1100), Dp(800)))) {
		return;
	}

	if (ScreenHeader("Browse Hosted Games")) {
		Back();
	}

	// One visible action in the sub-header: Refresh.  Host / Prefs
	// live on the Online Play hub, which is one back-tap away.
	const ATMobilePalette &p = ATMobileGetPalette();
	if (ATTouchButton("Refresh", ImVec2(Dp(140), Dp(40)),
	                  ATTouchButtonStyle::Neutral)) {
		br.refreshRequested = true;
	}
	ImGui::SameLine(0, Dp(10));
	const char *line = br.statusLine.empty()
		? (br.refreshInFlight ? "Refreshing…" : "")
		: br.statusLine.c_str();
	if (line && *line) {
		ImGui::AlignTextToFramePadding();
		ImGui::PushStyleColor(ImGuiCol_Text, ATMobileCol(p.textMuted));
		ImGui::TextUnformatted(line);
		ImGui::PopStyleColor();
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	// Reachability banner — mirrors Host Games so joiners see the same
	// honest green/red state.  Retry offered because the grid below is
	// empty when the lobby is unreachable.
	LobbyStatusBanner(/*allowRetry*/ true);

	// Tiles render directly in the sheet window — no BeginChild.
	// Earlier attempts (NavFlattened child, then a separate-scope
	// child with manual FocusWindow + SetKeyboardFocusHere) both
	// failed to route DownArrow from Refresh into the first tile:
	// ImGui's native nav processed Down on the current frame and
	// couldn't find a candidate across the child boundary (cursor
	// vanished), while the manual hop applied too late and was
	// clobbered by the nav cycle's clear.  With the tiles in the
	// same window as Refresh, native nav scoring finds the first
	// tile as the "downward" target on its own — zero custom
	// steering needed.  Drag-scroll uses the sheet window's own
	// scrollbar (ImGui auto-shows when content overflows).
	ATTouchDragScroll();

	if (br.items.empty()) {
		ATTouchMutedText(br.refreshInFlight
			? "Loading sessions…"
			: "No sessions right now.  Host one!");
	} else {
		if (st.prefs.showBrowserArt) PumpArtCache();
		// Grid sizing: aim for ~240dp tiles on tablets / desktop, but
		// collapse to a single column on narrow portrait screens so
		// tiles never render at zero width (the old code passed
		// ImVec2(0,0) assuming the grid internally dictated size,
		// which left rows collapsed into a left-hand sliver).
		const ImVec2 tileSize = BeginScreenGrid(/*columns*/ 4,
			/*minTileWidth*/ Dp(240),
			/*aspect*/ 0.85f);
		for (size_t i = 0; i < br.items.size(); ++i) {
			const auto& s = br.items[i];
			TileInfo ti;
			ti.title        = s.cartName.c_str();
			ti.subtitle     = s.hostHandle.c_str();
			ti.region       = s.region.c_str();
			ti.playerCount  = (uint32_t)s.playerCount;
			ti.maxPlayers   = (uint32_t)s.maxPlayers;
			ti.isPrivate    = s.requiresCode;
			ti.isSelected   = ((int)i == br.selectedIdx);
			ti.idKey        = s.sessionId.c_str();
			if (st.prefs.showBrowserArt) {
				int aw = 0, ah = 0;
				ti.artTexId = LookupArtByGameName(
					s.cartName.c_str(), &aw, &ah);
				ti.artSize = ImVec2((float)aw, (float)ah);
			}
			if (SessionTile(ti, tileSize)) {
				br.selectedIdx = (int)i;
				st.session.joinTarget = s;
				Navigate(s.requiresCode ? Screen::JoinPrompt
				                        : Screen::JoinConfirm);
			}
		}
		EndScreenGrid();
	}

	ATTouchEndDragScroll();

	if (!open) Navigate(Screen::Closed);

	EndSheet();
}

// -------------------------------------------------------------------
// Host setup
// -------------------------------------------------------------------

void RenderHostSetup() {
	State& st = GetState();
	bool open = true;
	if (!BeginSheet("Host a Game", &open,
	                ImVec2(Dp(420), Dp(360)),
	                ImVec2(Dp(640), Dp(520))))
		return;

	if (ScreenHeader("Host a Game")) {
		Back();
	}

	ATTouchSection("Visibility");
	int vis = st.session.hostingPrivate ? 1 : 0;
	const char *visItems[] = { "Public", "Private" };
	if (ATTouchSegmented("##vis", &vis, visItems, 2)) {
		st.session.hostingPrivate = (vis == 1);
	}
	ATTouchMutedText(vis == 1
		? "Friends-only: joiners must enter the code you share."
		: "Anyone on the lobby can see and join this session.");

	if (vis == 1) {
		ImGui::Spacing();
		ATTouchSection("Entry code");
		static char code[32] = {};
		if (ConsumeFocusRequest(2001)) {
			std::snprintf(code, sizeof code, "%s",
				st.prefs.lastEntryCode.c_str());
		}
		ImGui::PushItemWidth(-FLT_MIN);
		ImGui::InputText("##code", code, sizeof code);
		ImGui::PopItemWidth();
		st.session.hostingEntryCode = code;
	}

	// Populate pendingCartName from the most-recently booted image (MRU
	// slot A, which holds the head of the "Order" string).  Any format
	// works — cart, disk, tape, .xex — we just display the basename so
	// the host knows which game they're about to share.
	if (st.session.pendingCartName.empty()) {
		VDRegistryAppKey mru("MRU List", false);
		VDStringW order;
		mru.getString("Order", order);
		if (!order.empty()) {
			char kn[2] = { (char)order[0], 0 };
			VDStringW path;
			if (mru.getString(kn, path) && !path.empty()) {
				// Manual rfind for '/' or '\\' — VDStringW lacks find_last_of.
				const wchar_t *wp = path.c_str();
				const wchar_t *last = nullptr;
				for (const wchar_t *q = wp; *q; ++q)
					if (*q == L'/' || *q == L'\\') last = q;
				VDStringW base = last ? VDStringW(last + 1) : path;
				VDStringA u8Path = VDTextWToU8(path);
				VDStringA u8Base = VDTextWToU8(base);
				st.session.pendingCartPath.assign(u8Path.c_str(), u8Path.size());
				st.session.pendingCartName.assign(u8Base.c_str(), u8Base.size());
			}
		}
	}

	ImGui::Spacing();
	ATTouchSection("Game");
	ATTouchMutedText(st.session.pendingCartName.empty()
		? "No game booted.  Boot something first, then re-open Host a Game."
		: st.session.pendingCartName.c_str());

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	float btnW = (ImGui::GetContentRegionAvail().x - Dp(10)) * 0.5f;
	if (ATTouchButton("Cancel", ImVec2(btnW, Dp(48)),
	                  ATTouchButtonStyle::Neutral)) {
		Back();
	}
	ImGui::SameLine(0, Dp(10));
	bool canHost = !st.session.pendingCartName.empty()
	               && (!st.session.hostingPrivate
	                   || !st.session.hostingEntryCode.empty());
	ImGui::BeginDisabled(!canHost);
	if (ATTouchButton("Start Hosting", ImVec2(btnW, Dp(48)),
	                  ATTouchButtonStyle::Accent)) {
		st.prefs.lastEntryCode = st.session.hostingEntryCode;
		SaveToRegistry();
		StartHostingAction();
	}
	ImGui::EndDisabled();

	if (!open) Back();
	EndSheet();
}

// -------------------------------------------------------------------
// Join prompt (private session — need entry code)
// -------------------------------------------------------------------

void RenderJoinPrompt() {
	State& st = GetState();
	bool open = true;
	if (!BeginSheet("Enter Join Code", &open,
	                ImVec2(Dp(360), Dp(240)),
	                ImVec2(Dp(520), Dp(340))))
		return;

	if (ScreenHeader("Enter Join Code")) {
		Back();
	}

	ATTouchSection("Private session");
	PeerChip(st.session.joinTarget.hostHandle.c_str(),
	         st.session.joinTarget.region.c_str(),
	         /*isPrivate*/ true);
	ATTouchMutedText(st.session.joinTarget.cartName.c_str());

	ImGui::Spacing();
	static char code[32] = {};
	if (ConsumeFocusRequest(3001)) {
		code[0] = 0;
		ImGui::SetKeyboardFocusHere();
	}
	ImGui::PushItemWidth(-FLT_MIN);
	ImGui::InputText("##code", code, sizeof code);
	ImGui::PopItemWidth();
	st.session.joinEntryCode = code;

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
	float btnW = (ImGui::GetContentRegionAvail().x - Dp(10)) * 0.5f;
	if (ATTouchButton("Cancel", ImVec2(btnW, Dp(48)),
	                  ATTouchButtonStyle::Neutral)) {
		Back();
	}
	ImGui::SameLine(0, Dp(10));
	ImGui::BeginDisabled(code[0] == 0);
	if (ATTouchButton("Join", ImVec2(btnW, Dp(48)),
	                  ATTouchButtonStyle::Accent)) {
		StartJoiningAction();
	}
	ImGui::EndDisabled();

	if (!open) Back();
	EndSheet();
}

// -------------------------------------------------------------------
// Join confirm (public session — confirm TOS + ROM)
// -------------------------------------------------------------------

void RenderJoinConfirm() {
	State& st = GetState();
	bool open = true;
	if (!BeginSheet("Join Session", &open,
	                ImVec2(Dp(420), Dp(320)),
	                ImVec2(Dp(600), Dp(480))))
		return;

	if (ScreenHeader("Join Session")) {
		Back();
	}

	ATTouchSection("Confirm");
	PeerChip(st.session.joinTarget.hostHandle.c_str(),
	         st.session.joinTarget.region.c_str(),
	         /*isPrivate*/ false);
	ATTouchMutedText(st.session.joinTarget.cartName.c_str());

	ImGui::Spacing();
	ATTouchMutedText(
		"The host will send their game file and hardware settings. "
		"Both sides cold-boot from that so the session starts "
		"identically.  Your current game will be replaced.");

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
	float btnW = (ImGui::GetContentRegionAvail().x - Dp(10)) * 0.5f;
	if (ATTouchButton("Cancel", ImVec2(btnW, Dp(48)),
	                  ATTouchButtonStyle::Neutral)) {
		Back();
	}
	ImGui::SameLine(0, Dp(10));
	if (ATTouchButton("Join", ImVec2(btnW, Dp(48)),
	                  ATTouchButtonStyle::Accent)) {
		StartJoiningAction();
	}

	if (!open) Back();
	EndSheet();
}

// -------------------------------------------------------------------
// Waiting panel
// -------------------------------------------------------------------

void RenderWaiting() {
	State& st = GetState();

	// Auto-dismiss once Lockstepping is reached — the game is now
	// running on both sides and the user needs to see the emulator,
	// not this sheet on top of it.  Mirrors DesktopWaiting.
	if (ATNetplayGlue::IsLockstepping()) {
		Navigate(Screen::Closed);
		return;
	}

	// If the joiner's coordinator hit a terminal phase, stop sitting
	// on "Waiting for peer…" and let the user dismiss / retry.
	const ATNetplayGlue::Phase jp = ATNetplayGlue::JoinPhase();
	const bool joinFailed = (jp == ATNetplayGlue::Phase::Failed ||
	                         jp == ATNetplayGlue::Phase::Ended  ||
	                         jp == ATNetplayGlue::Phase::Desynced);

	bool open = true;
	if (!BeginSheet("Waiting", &open,
	                ImVec2(Dp(420), Dp(300)),
	                ImVec2(Dp(620), Dp(460))))
		return;

	if (ScreenHeader(joinFailed ? "Could not join" : "Connecting")) {
		// Back cancels host/join and returns to the previous screen.
		// Stop coordinators first so the worker thread doesn't keep
		// retrying in the background.
		ATNetplayGlue::DisconnectActive();
		Back();
	}

	// The label depends on whether we're hosting or joining + the
	// coordinator's phase.  While handshaking we say so explicitly —
	// "Waiting for peer…" was misleading for joiners whose wrong
	// entry code triggered a silent reject retry in the background.
	const char *label = "Waiting for peer…";
	int severity = 0;
	bool spin = true;
	if (joinFailed) {
		const char *err = ATNetplayGlue::JoinLastError();
		label = (err && *err) ? err : "Connection failed.";
		severity = 2;
		spin = false;
	} else {
		switch (jp) {
			case ATNetplayGlue::Phase::Handshaking:
				label = st.session.joinTarget.requiresCode
					? "Verifying join code with host…"
					: "Contacting host…";
				break;
			case ATNetplayGlue::Phase::ReceivingSnapshot:
				label = "Downloading game from host…";
				break;
			case ATNetplayGlue::Phase::SnapshotReady:
				label = "Starting game…";
				break;
			default: break;
		}
	}
	StatusBadge(label, severity, spin);

	ImGui::Spacing();
	PeerChip(st.session.joinTarget.hostHandle.c_str(),
	         st.session.joinTarget.region.c_str(),
	         st.session.joinTarget.requiresCode);
	ATTouchMutedText(st.session.joinTarget.cartName.c_str());

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	// Terminal-failure layout: surface a specific "Change Code" button
	// for bad-code rejects so the common mistake takes one tap to fix
	// instead of a back-out and re-navigate through the Browser.
	const bool badCode = joinFailed && st.session.joinTarget.requiresCode
		&& std::strstr(ATNetplayGlue::JoinLastError(),
		               "Incorrect join code") != nullptr;

	if (badCode) {
		float bw = (ImGui::GetContentRegionAvail().x - Dp(20)) / 3.0f;
		if (ATTouchButton("Cancel", ImVec2(bw, Dp(48)),
		                  ATTouchButtonStyle::Neutral)) {
			ATNetplayGlue::DisconnectActive();
			Navigate(Screen::Browser);
		}
		ImGui::SameLine(0, Dp(10));
		if (ATTouchButton("Change Code", ImVec2(bw, Dp(48)),
		                  ATTouchButtonStyle::Accent)) {
			ATNetplayGlue::DisconnectActive();
			Navigate(Screen::JoinPrompt);
		}
		ImGui::SameLine(0, Dp(10));
		if (ATTouchButton("Try Again", ImVec2(bw, Dp(48)),
		                  ATTouchButtonStyle::Neutral)) {
			ATNetplayGlue::DisconnectActive();
			StartJoiningAction();
		}
	} else {
		float btnW = (ImGui::GetContentRegionAvail().x - Dp(10)) * 0.5f;
		if (ATTouchButton("Cancel", ImVec2(btnW, Dp(48)),
		                  ATTouchButtonStyle::Danger)) {
			StopHostingAction();
			Navigate(Screen::Browser);
		}
		ImGui::SameLine(0, Dp(10));
		if (ATTouchButton("Minimise", ImVec2(btnW, Dp(48)),
		                  ATTouchButtonStyle::Neutral)) {
			// Hide the sheet — coordinator keeps running in the background.
			Navigate(Screen::Closed);
		}
	}

	if (!open) Navigate(Screen::Closed);
	EndSheet();
}

// -------------------------------------------------------------------
// Preferences
// -------------------------------------------------------------------

void RenderPrefs() {
	State& st = GetState();
	bool open = true;
	if (!BeginSheet("Online Play Preferences", &open,
	                ImVec2(Dp(480), Dp(520)),
	                ImVec2(Dp(720), Dp(780))))
		return;

	if (ScreenHeader("Preferences")) {
		SaveToRegistry();
		Back();
	}

	// Scroll the body so portrait orientation doesn't bury options
	// below the keyboard / gesture bar.  No Done button needed —
	// the back arrow commits on exit (same as Settings).
	ImGui::BeginChild("##prefsBody",
		ImGui::GetContentRegionAvail(),
		ImGuiChildFlags_NavFlattened, 0);
	ATTouchDragScroll();

	ATTouchSection("Nickname");
	static char nameBuf[32] = {};
	if (ConsumeFocusRequest(5001)) {
		std::snprintf(nameBuf, sizeof nameBuf, "%s",
			st.prefs.nickname.c_str());
	}
	ImGui::PushItemWidth(-FLT_MIN);
	if (ImGui::InputText("##handle", nameBuf, sizeof nameBuf)) {
		st.prefs.nickname = nameBuf;
	}
	ImGui::PopItemWidth();
	bool anon = st.prefs.isAnonymous;
	if (ATTouchToggle("Anonymous (random per session)", &anon)) {
		st.prefs.isAnonymous = anon;
	}

	ImGui::Spacing();
	ATTouchSection("Accept incoming joins");
	int acc = (int)st.prefs.acceptMode;
	const char *accItems[] = { "Auto-accept", "Prompt me" };
	if (ATTouchSegmented("##acc", &acc, accItems, 2)) {
		st.prefs.acceptMode = (AcceptMode)acc;
	}

	ImGui::Spacing();
	ATTouchSection("Notifications");
	// Platform-applicable notifications only.  SDL_FlashWindow is a
	// no-op on Android (no window-manager taskbar), and the "Steal
	// focus" toggle isn't consumed by any runtime code yet — hide
	// both on mobile so the sheet only exposes options that actually
	// fire in Gaming Mode (system notification + chime are both
	// honoured on Linux / macOS / Windows / Android when a backend
	// is present).
#ifndef __ANDROID__
	ATTouchToggle("Flash window", &st.prefs.notif.flashWindow);
#endif
	ATTouchToggle("System notification", &st.prefs.notif.systemNotify);
	ATTouchToggle("Chime", &st.prefs.notif.playChime);

	ImGui::Spacing();
	ATTouchSection("Input delay");
	ATTouchSlider("LAN (frames)",
		&st.prefs.defaultInputDelayLan, 1, 10, "%d frames");
	ATTouchSlider("Internet (frames)",
		&st.prefs.defaultInputDelayInternet, 1, 10, "%d frames");

	ImGui::Spacing();
	ATTouchSection("Display");
	ATTouchToggle("Show game art in Online Play",
		&st.prefs.showBrowserArt);
	ATTouchToggle("Show in-session HUD", &st.prefs.showSessionHUD);

	ATTouchEndDragScroll();
	ImGui::EndChild();

	if (!open) { SaveToRegistry(); Back(); }
	EndSheet();
}

// -------------------------------------------------------------------
// My Hosted Games (Gaming Mode) — touch-card list.
// -------------------------------------------------------------------

namespace {
const char *OfferStateLabelMobile(HostedGameState s) {
	switch (s) {
		case HostedGameState::Draft:       return "Draft";
		case HostedGameState::Listed:      return "Listed";
		case HostedGameState::Handshaking: return "Connecting…";
		case HostedGameState::Playing:     return "Playing";
		case HostedGameState::Suspended:   return "Suspended";
		case HostedGameState::Failed:      return "Failed";
	}
	return "?";
}

int OfferStateSeverity(HostedGameState s) {
	switch (s) {
		case HostedGameState::Playing:     return 3;
		case HostedGameState::Listed:      return 3;
		case HostedGameState::Handshaking: return 1;
		case HostedGameState::Suspended:   return 1;
		case HostedGameState::Failed:      return 2;
		default:                      return 0;
	}
}
}

void RenderMyHostedGames() {
	State& st = GetState();
	bool open = true;
	if (!BeginSheet("Host Games", &open,
	                ImVec2(Dp(560), Dp(500)),
	                ImVec2(Dp(1100), Dp(800))))
		return;

	if (ScreenHeader("Host Games")) {
		Back();
	}

	// Honest lobby-reachability banner.  Colour + text reflect the
	// last List result, not just the activity state — a green "listed"
	// message while the server is unreachable would be misleading.
	LobbyStatusBanner(/*allowRetry*/ true);
	if (st.activity == UserActivity::InSession) {
		ATTouchMutedText("Playing — other hosted games suspended.");
	} else if (st.activity == UserActivity::PlayingLocal) {
		ATTouchMutedText("Single-player active — hosted games suspended.");
	}

	// Single primary action.  Browse / Preferences have moved to the
	// Online Play hub, which is one back-tap away; surfacing them
	// again here would just clutter the sub-screen.
	float btnW = Dp(220);
	bool atCap = (st.hostedGames.size() >= State::kMaxHostedGames);
	ImGui::BeginDisabled(atCap);
	if (ATTouchButton("+ Add Game", ImVec2(btnW, Dp(44)),
	                  ATTouchButtonStyle::Accent)) {
		st.editingGameId.clear();
		Navigate(Screen::AddGame);
	}
	ImGui::EndDisabled();

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	// No BeginChild — list items render directly in the sheet so
	// native arrow-key / gamepad nav flows cleanly between
	// "+ Add Game", the per-game row buttons, and the "Host all"
	// toggle below.  A child window would make each cross-boundary
	// hop (Up from first row → Add Game, Down from last row →
	// Host all) route through ImGui's unreliable cross-window nav
	// scoring, producing the "focus stuck on frame border" behaviour
	// users reported.  The sheet window provides its own scrollbar.
	ATTouchDragScroll();

	if (st.hostedGames.empty()) {
		ATTouchMutedText(
			"You haven't added any games yet.  Tap + Add Game to pick "
			"one from your library or from a file.");
	} else {
		if (st.prefs.showBrowserArt) PumpArtCache();
		std::string pendingToggle, pendingRemove;
		for (auto& o : st.hostedGames) {
			ImGui::PushID(o.id.c_str());

			// Cover art thumbnail — matched by basename against the
			// Game Library.  Rendered as its own row above the list
			// item so the ATTouchListItem's fixed-height card layout
			// stays intact (the row's clip rect would otherwise hide
			// anything mounted to its left on SameLine).
			if (st.prefs.showBrowserArt) {
				int aw = 0, ah = 0;
				uintptr_t tex = LookupArtByGameName(
					o.gameName.c_str(), &aw, &ah);
				if (tex && aw > 0 && ah > 0) {
					float thumbH = Dp(44);
					float thumbW = thumbH * (float)aw / (float)ah;
					if (thumbW > Dp(72)) thumbW = Dp(72);
					ImGui::Image((ImTextureID)tex,
						ImVec2(thumbW, thumbH));
				}
			}

			// Subtitle: single-line hardware summary + visibility.
			// Firmware sub-line is drawn below separately (the list
			// item's subtitle slot is single-line only).
			char subtitle[160];
			std::snprintf(subtitle, sizeof subtitle,
				"%s \xC2\xB7 %s",
				MachineConfigSummary(o.config),
				o.isPrivate ? "Private" : "Public");
			bool tapped = ATTouchListItem(o.gameName.c_str(), subtitle,
				false, false);
			(void)tapped;  // tile tap is currently a no-op; actions below

			// Firmware identification sub-line — shown only when the
			// offer pins at least one CRC.  Matches Desktop's row.
			if (o.config.kernelCRC32 || o.config.basicCRC32) {
				const char *osName =
					FirmwareNameForCRC(o.config.kernelCRC32);
				const char *bsName =
					FirmwareNameForCRC(o.config.basicCRC32);
				char fw[224];
				int n = 0;
				if (o.config.kernelCRC32) {
					n += std::snprintf(fw + n, sizeof fw - n,
						"OS: %s (%08X)",
						*osName ? osName : "Unknown",
						o.config.kernelCRC32);
				}
				if (o.config.basicCRC32) {
					n += std::snprintf(fw + n, sizeof fw - n,
						"%sBASIC: %s (%08X)",
						n ? "  \xC2\xB7  " : "",
						*bsName ? bsName : "Unknown",
						o.config.basicCRC32);
				}
				ATTouchMutedText(fw);
			}

			if (!o.lastError.empty()) {
				ATTouchMutedText(o.lastError.c_str());
			}

			// Severity badge for lobby state + visibility badge.
			StatusBadge(OfferStateLabelMobile(o.state),
				OfferStateSeverity(o.state),
				o.state == HostedGameState::Handshaking);
			ImGui::SameLine();
			StatusBadge(o.isPrivate ? "Private" : "Public",
				o.isPrivate ? 1 : 0, false);

			ImGui::SameLine();
			const char *enLabel = o.enabled ? "Disable" : "Enable";
			if (ATTouchButton(enLabel, ImVec2(Dp(130), Dp(40)),
			                  o.enabled ? ATTouchButtonStyle::Neutral
			                            : ATTouchButtonStyle::Accent)) {
				pendingToggle = o.id;
			}
			ImGui::SameLine();
			if (ATTouchButton("Remove", ImVec2(Dp(120), Dp(40)),
			                  ATTouchButtonStyle::Danger)) {
				pendingRemove = o.id;
			}

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			ImGui::PopID();
		}

		if (!pendingToggle.empty()) {
			HostedGame* o = FindHostedGame(pendingToggle);
			if (o) { o->enabled ? DisableHostedGame(pendingToggle)
			                    : EnableHostedGame(pendingToggle); }
		}
		if (!pendingRemove.empty()) {
			RemoveHostedGame(pendingRemove);
		}
	}

	ATTouchEndDragScroll();

	// Host-all tri-state master — rendered at the bottom of the
	// scroll area (no longer pinned) so arrow-key nav can reach it
	// from the last list item.  Users scroll down to find it; on
	// short lists both are on screen at once anyway.
	ImGui::Separator();
	if (!st.hostedGames.empty()) {
		int enCount = 0;
		for (const auto& o : st.hostedGames) if (o.enabled) ++enCount;
		const bool allEnabled = (enCount == (int)st.hostedGames.size());
		const bool anyEnabled = (enCount > 0);
		const bool mixed      = anyEnabled && !allEnabled;
		if (mixed)
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
				ImGui::GetStyle().Alpha * 0.6f);
		bool toggle = allEnabled;
		if (ATTouchToggle("Host all", &toggle)) {
			const bool target = !allEnabled;
			for (auto& o : st.hostedGames) {
				if (o.enabled != target) {
					target ? EnableHostedGame(o.id)
					       : DisableHostedGame(o.id);
				}
			}
		}
		if (mixed) ImGui::PopStyleVar();
	}

	if (!open) Navigate(Screen::Closed);
	EndSheet();
}

// -------------------------------------------------------------------
// Add Offer (Gaming Mode) — Library | File picker.
// -------------------------------------------------------------------

namespace {
VDStringW s_mobilePickedPath;
int       s_mobileAddSource = 0;
bool      s_mobileAddPrivate = false;
char      s_mobileAddCode[32] = {};
int       s_mobileLibrarySel = -1;
MachineConfig s_mobileAddConfig;
bool          s_mobileAddConfigSeeded = false;

void MobileAddOfferFileCallback(void*, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	s_mobilePickedPath = VDTextU8ToW(filelist[0], -1);
}

// File-dialog filters — same as desktop.
const SDL_DialogFileFilter kMobileAddOfferFilters[] = {
	{ "Game images",
	  "atr;xex;bin;car;rom;a52;a8s;exe;com;ucf;pro;xfd;atx;dcm;zip;cas" },
	{ "All files", "*" },
};
} // anonymous

void RenderAddOffer() {
	State& st = GetState();
	(void)st;
	if (!s_mobileAddConfigSeeded) {
		s_mobileAddConfig = CaptureCurrentMachineConfig();
		s_mobileAddConfigSeeded = true;
	}
	bool open = true;
	if (!BeginSheet("Add Game to Host", &open,
	                ImVec2(Dp(560), Dp(520)),
	                ImVec2(Dp(1100), Dp(800))))
		return;

	if (ScreenHeader("Add Game to Host")) {
		Back();
	}

	int src = s_mobileAddSource;
	const char *srcItems[] = { "From Library", "From File" };
	if (ATTouchSegmented("##src", &src, srcItems, 2)) {
		s_mobileAddSource = src;
	}
	ImGui::Spacing();

	std::string stagedPath;
	std::string stagedName;

	if (s_mobileAddSource == 0) {
		ATGameLibrary& lib = LibrarySingleton();
		const auto& entries = lib.GetEntries();
		ATTouchMutedText(entries.empty()
			? "Library is empty — use Settings to add a source."
			: "");

		ImVec2 avail = ImGui::GetContentRegionAvail();
		ImGui::BeginChild("##lib", ImVec2(avail.x, avail.y - Dp(200)),
			ImGuiChildFlags_NavFlattened, 0);
		ATTouchDragScroll();
		for (size_t i = 0; i < entries.size(); ++i) {
			const auto& e = entries[i];
			if (e.mVariants.empty()) continue;
			VDStringA nameU8 = VDTextWToU8(e.mDisplayName);
			char sub[64];
			std::snprintf(sub, sizeof sub, "%zu variant%s",
				e.mVariants.size(),
				e.mVariants.size() == 1 ? "" : "s");
			ImGui::PushID((int)i);
			bool sel = ((int)i == s_mobileLibrarySel);
			if (ATTouchListItem(nameU8.c_str(), sub, sel, true)) {
				s_mobileLibrarySel = (int)i;
			}
			ImGui::PopID();
		}
		ATTouchEndDragScroll();
		ImGui::EndChild();

		if (s_mobileLibrarySel >= 0
		    && (size_t)s_mobileLibrarySel < entries.size()) {
			const auto& e = entries[s_mobileLibrarySel];
			if (!e.mVariants.empty()) {
				VDStringA p = VDTextWToU8(e.mVariants[0].mPath);
				VDStringA n = VDTextWToU8(e.mDisplayName);
				stagedPath.assign(p.c_str(), p.size());
				stagedName.assign(n.c_str(), n.size());
			}
		}
	} else {
		if (s_mobilePickedPath.empty()) {
			ATTouchMutedText("Tap Browse to choose a file.");
		} else {
			VDStringA u8 = VDTextWToU8(s_mobilePickedPath);
			ATTouchMutedText(u8.c_str());
		}
		if (ATTouchButton("Browse...", ImVec2(Dp(200), Dp(48)),
		                  ATTouchButtonStyle::Neutral)) {
			ATUIShowOpenFileDialog('npam', MobileAddOfferFileCallback,
				nullptr, g_pWindow,
				kMobileAddOfferFilters,
				(int)(sizeof kMobileAddOfferFilters /
				      sizeof kMobileAddOfferFilters[0]),
				false);
		}
		if (!s_mobilePickedPath.empty()) {
			VDStringA pathU8 = VDTextWToU8(s_mobilePickedPath);
			const wchar_t *last = nullptr;
			for (const wchar_t *q = s_mobilePickedPath.c_str(); *q; ++q)
				if (*q == L'/' || *q == L'\\') last = q;
			VDStringW base = last ? VDStringW(last + 1) : s_mobilePickedPath;
			VDStringA baseU8 = VDTextWToU8(base);
			stagedPath.assign(pathU8.c_str(), pathU8.size());
			stagedName.assign(baseU8.c_str(), baseU8.size());
		}
	}

	ImGui::Spacing();

	// Machine configuration — applied only during a session; never
	// touches the user's saved Altirra configuration.  Gaming Mode
	// uses touch widgets that mirror the Desktop dialog.
	ATTouchMutedText("Machine:");
	ATTouchMutedText(MachineConfigSummary(s_mobileAddConfig));
	if (ATTouchButton("Copy from current emulator",
	                  ImVec2(Dp(260), Dp(40)),
	                  ATTouchButtonStyle::Neutral)) {
		s_mobileAddConfig = CaptureCurrentMachineConfig();
	}
	ATTouchToggle("BASIC enabled", &s_mobileAddConfig.basicEnabled);
	ATTouchToggle("SIO full-speed", &s_mobileAddConfig.sioPatchEnabled);

	ImGui::Spacing();
	ATTouchToggle("Private (require entry code)", &s_mobileAddPrivate);
	if (s_mobileAddPrivate) {
		ImGui::PushItemWidth(Dp(240));
		ImGui::InputText("##code", s_mobileAddCode, sizeof s_mobileAddCode);
		ImGui::PopItemWidth();
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	bool ready = !stagedPath.empty() && !stagedName.empty()
		&& (!s_mobileAddPrivate || s_mobileAddCode[0] != 0);
	float bW = (ImGui::GetContentRegionAvail().x - Dp(10)) * 0.5f;
	if (ATTouchButton("Cancel", ImVec2(bW, Dp(48)),
	                  ATTouchButtonStyle::Neutral)) {
		Back();
	}
	ImGui::SameLine(0, Dp(10));
	ImGui::BeginDisabled(!ready);
	if (ATTouchButton("Add Game", ImVec2(bW, Dp(48)),
	                  ATTouchButtonStyle::Accent)) {
		State& s = GetState();
		// Reject duplicates — same image path + same machine config.
		// Shares the signature function with the Desktop Add flow so
		// both modes enforce the same rule.
		const std::string sig =
			HostedGameSignature(stagedPath, s_mobileAddConfig);
		bool dup = false;
		for (const auto& existing : s.hostedGames) {
			if (HostedGameSignature(existing.gamePath, existing.config)
			    == sig) { dup = true; break; }
		}
		if (dup) {
			s.session.lastError =
				"This game is already added to hosting with this "
				"configuration — change the machine config or remove "
				"the existing entry first.";
			Navigate(Screen::Error);
		} else if (s.hostedGames.size() < State::kMaxHostedGames) {
			HostedGame o;
			o.id        = GenerateHostedGameId();
			o.gamePath  = stagedPath;
			o.gameName  = stagedName;
			o.isPrivate = s_mobileAddPrivate;
			o.entryCode = s_mobileAddCode;
			o.config    = s_mobileAddConfig;
			o.enabled   = true;
			s.hostedGames.push_back(std::move(o));
			s.prefs.lastAddConfig = s_mobileAddConfig;
			SaveToRegistry();
			EnableHostedGame(s.hostedGames.back().id);
			s_mobilePickedPath.clear();
			s_mobileLibrarySel = -1;
			s_mobileAddPrivate = false;
			s_mobileAddCode[0] = 0;
			// Preset intentionally retained so next Add Game opens
			// with the same choice.
		}
		Navigate(Screen::MyHostedGames);
	}
	ImGui::EndDisabled();

	if (!open) Back();
	EndSheet();
}

// -------------------------------------------------------------------
// Accept-join prompt (Gaming Mode) — host's "Allow / Deny" modal
// when acceptMode = PromptMe and a peer requests to join.  Auto-
// declines after 20 s so an AFK host doesn't block the joiner.
// -------------------------------------------------------------------
void RenderAcceptJoinPrompt() {
	State& st = GetState();
	const std::string gid = st.session.pendingAcceptGameId;
	if (gid.empty()) {
		Navigate(Screen::MyHostedGames);
		return;
	}
	const uint64_t nowMs = (uint64_t)SDL_GetTicks();
	constexpr uint64_t kAutoDeclineMs = 20000;
	const uint64_t elapsed = nowMs > st.session.pendingAcceptStartedMs
		? nowMs - st.session.pendingAcceptStartedMs : 0;
	if (elapsed >= kAutoDeclineMs) {
		ATNetplayGlue::HostRejectPending(gid.c_str());
		return;
	}
	const uint64_t remainS = (kAutoDeclineMs - elapsed + 999) / 1000;

	bool open = true;
	if (!BeginSheet("Join request", &open,
	                ImVec2(Dp(420), Dp(280)),
	                ImVec2(Dp(640), Dp(440))))
		return;

	// Back here behaves like Deny — a peer is waiting, silently
	// dismissing the prompt would leave them hanging.
	if (ScreenHeader("Join request")) {
		ATNetplayGlue::HostRejectPending(gid.c_str());
	}

	const char *handle = st.session.pendingAcceptHandle.empty()
		? "Someone" : st.session.pendingAcceptHandle.c_str();
	const char *gameName = st.session.pendingAcceptGameName.c_str();

	ATTouchSection(handle);
	ATTouchMutedText("wants to join your game:");
	ATTouchMutedText(gameName);
	ImGui::Spacing();
	// Transparency: make it explicit that Allow replaces the current
	// emulator session.  A restore-point is captured automatically
	// so the user gets their prior state back when the online
	// session ends, but they should know before clicking Allow.
	ATTouchMutedText(
		"Accepting will replace your current emulator game with "
		"this one for the online session.  Your game is saved "
		"automatically and restored when the session ends.");
	ImGui::Spacing();
	char countdown[48];
	std::snprintf(countdown, sizeof countdown,
		"Auto-decline in %llus", (unsigned long long)remainS);
	ATTouchMutedText(countdown);

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	float bW = (ImGui::GetContentRegionAvail().x - Dp(10)) * 0.5f;
	if (ATTouchButton("Deny", ImVec2(bW, Dp(48)),
	                  ATTouchButtonStyle::Danger)) {
		ATNetplayGlue::HostRejectPending(gid.c_str());
	}
	ImGui::SameLine(0, Dp(10));
	if (ATTouchButton("Allow", ImVec2(bW, Dp(48)),
	                  ATTouchButtonStyle::Accent)) {
		ATNetplayGlue::HostAcceptPending(gid.c_str());
	}

	if (!open) {
		ATNetplayGlue::HostRejectPending(gid.c_str());
	}
	EndSheet();
}

// -------------------------------------------------------------------
// Error sheet (Gaming Mode) — shared terminal screen for failures
// that the action handlers raise via `Navigate(Screen::Error)`.
// Without this, Gaming Mode's dispatcher would hit `default: break`
// and leave the user stuck on whatever screen navigated away.
// -------------------------------------------------------------------
void RenderError() {
	State& st = GetState();
	bool open = true;
	if (!BeginSheet("Online Play Error", &open,
	                ImVec2(Dp(420), Dp(260)),
	                ImVec2(Dp(640), Dp(460))))
		return;

	if (ScreenHeader("Something went wrong")) {
		st.session.lastError.clear();
		Back();
	}

	ImGui::Spacing();
	ATTouchSection("Error");
	const char *msg = st.session.lastError.empty()
		? "An unknown error occurred."
		: st.session.lastError.c_str();
	ImGui::PushTextWrapPos(0.0f);
	ATTouchMutedText(msg);
	ImGui::PopTextWrapPos();

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
	if (ATTouchButton("OK", ImVec2(-FLT_MIN, Dp(48)),
	                  ATTouchButtonStyle::Accent)) {
		st.session.lastError.clear();
		Back();
	}

	if (!open) { st.session.lastError.clear(); Back(); }
	EndSheet();
}

// -------------------------------------------------------------------
// Online Play hub (Gaming Mode entry) — three hero cards that drill
// into Host Games, Browse Hosted Games, and Preferences.  Matches the
// Settings hub → detail pattern users already know from Gaming Mode.
// -------------------------------------------------------------------
void RenderOnlinePlayHub() {
	State& st = GetState();

	bool open = true;
	if (!BeginSheet("Online Play", &open,
	                ImVec2(Dp(560), Dp(420)),
	                ImVec2(Dp(1100), Dp(800))))
		return;

	// Kick an implicit refresh only when the hub is *appearing* for
	// the first time this visit, and only if the cached listing is
	// stale or has never been fetched.  The earlier per-frame
	// "items.empty()" trigger fired every ~60ms while the lobby was
	// genuinely empty, producing a rate-limit flood on the Oracle
	// Free Tier server (60 req/min cap, 6 req/min per endpoint).
	// Gating on IsWindowAppearing + a 30 s freshness window means
	// the hub kicks at most one poll per visit, and zero polls when
	// the user re-enters within the auto-refresh cadence.
	if (ImGui::IsWindowAppearing() && !st.browser.refreshInFlight
	    && st.browser.nextRetryMs == 0) {
		const uint64_t nowMs = (uint64_t)SDL_GetTicks();
		const bool stale = (st.browser.lastFetchMs == 0)
			|| (nowMs - st.browser.lastFetchMs > 30000);
		if (stale) st.browser.refreshRequested = true;
	}

	if (ScreenHeader("Online Play")) {
		Back();
	}

	// Reachability pill + user-activity hint.  Users should know the
	// state of the lobby before they tap into any sub-screen.
	LobbyStatusBanner(/*allowRetry*/ true);
	if (st.activity == UserActivity::InSession) {
		ATTouchMutedText("Playing — other hosted games suspended.");
	} else if (st.activity == UserActivity::PlayingLocal) {
		ATTouchMutedText(
			"Single-player active — hosted games suspended.");
	}
	ImGui::Spacing();

	// Scroll the cards so the list stays usable on very short safe-
	// area heights (landscape phone with nav gestures).
	ImGui::BeginChild("##hubBody", ImGui::GetContentRegionAvail(),
		ImGuiChildFlags_NavFlattened, 0);
	ATTouchDragScroll();

	// Count enabled hosted games so the Host card can show a
	// live "n listed" subtitle — matching the Settings-hub
	// convention where each card carries a summary of current state.
	int enabledCount = 0;
	for (const auto& o : st.hostedGames) if (o.enabled) ++enabledCount;

	char hostSub[96];
	if (enabledCount == 0 && st.hostedGames.empty()) {
		std::snprintf(hostSub, sizeof hostSub,
			"Pick games to share with friends.");
	} else if (enabledCount == 0) {
		std::snprintf(hostSub, sizeof hostSub,
			"%zu game%s in draft — none listed.",
			st.hostedGames.size(),
			st.hostedGames.size() == 1 ? "" : "s");
	} else {
		std::snprintf(hostSub, sizeof hostSub,
			"%d listed on the lobby.", enabledCount);
	}

	char browseSub[96];
	if (st.browser.items.empty()) {
		std::snprintf(browseSub, sizeof browseSub,
			"See who's hosting right now.");
	} else {
		std::snprintf(browseSub, sizeof browseSub,
			"%zu session%s available.",
			st.browser.items.size(),
			st.browser.items.size() == 1 ? "" : "s");
	}

	// On the first visible frame, steer focus to the first card so
	// keyboard / gamepad users land on an actionable item without
	// needing to Tab.  IsWindowAppearing goes true exactly on the
	// first frame the window is shown.
	const bool firstFrame = ImGui::IsWindowAppearing();
	if (firstFrame) ImGui::SetKeyboardFocusHere();

	if (ATTouchListItem("Host Games", hostSub, false, true)) {
		Navigate(Screen::MyHostedGames);
	}
	ImGui::Spacing();
	if (ATTouchListItem("Browse Hosted Games", browseSub, false, true)) {
		EnqueueBrowserRefresh();
		Navigate(Screen::Browser);
	}
	ImGui::Spacing();
	if (ATTouchListItem("Preferences",
		"Nickname, notifications, input delay, art.", false, true)) {
		Navigate(Screen::Prefs);
	}

	ATTouchEndDragScroll();
	ImGui::EndChild();

	if (!open) Navigate(Screen::Closed);
	EndSheet();
}

// -------------------------------------------------------------------
// Screen dispatcher
// -------------------------------------------------------------------

} // anonymous namespace

// Public — called from the common render.
bool ATNetplayUI_DispatchScreen() {
	State& st = GetState();
	if (st.screen == Screen::Closed) return false;

	switch (st.screen) {
		case Screen::Nickname:      RenderNickname();       break;
		case Screen::OnlinePlayHub: RenderOnlinePlayHub();  break;
		case Screen::Browser:       RenderBrowser();        break;
		case Screen::MyHostedGames: RenderMyHostedGames();  break;
		case Screen::AddGame:       RenderAddOffer();       break;
		case Screen::HostSetup:     RenderHostSetup();      break;
		case Screen::JoinPrompt:    RenderJoinPrompt();     break;
		case Screen::JoinConfirm:   RenderJoinConfirm();    break;
		case Screen::Waiting:       RenderWaiting();        break;
		case Screen::Prefs:             RenderPrefs();            break;
		case Screen::Error:             RenderError();            break;
		case Screen::AcceptJoinPrompt:  RenderAcceptJoinPrompt(); break;
		default: break;
	}

	// Global Escape-to-back handler for Desktop.  Gaming Mode has
	// ScreenHeader() on every screen which owns Escape/Gamepad-B, so
	// handling it here would double-pop the back-stack.
	if (!ATUIIsGamingMode() &&
	    ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
		if (!Back()) Navigate(Screen::Closed);
	}
	return true;
}

// Exposed to the menu/refresh path.
void ATNetplayUI_EnqueueBrowserRefresh() {
	EnqueueBrowserRefresh();
}

void ATNetplayUI_EnqueueStatsRefresh() {
	EnqueueStatsRefresh();
}

} // namespace ATNetplayUI
