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
		// Fall through to the browser.
		Navigate(Screen::Browser);
		// Request a browser refresh now that we have identity.
		st.browser.refreshRequested = true;
	}
	ImGui::EndDisabled();

	if (!open) Navigate(Screen::Closed);

	EndSheet();
}

// -------------------------------------------------------------------
// Online browser
// -------------------------------------------------------------------

void EnqueueBrowserRefresh() {
	const std::vector<ATNetplay::LobbyEntry>& lobbies = GetConfiguredLobbies();
	for (const auto& e : lobbies) {
		if (!e.enabled) continue;
		if (e.kind != ATNetplay::LobbyKind::Http) continue;  // LAN handled separately
		// Parse "http://host:port" into endpoint.
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

		LobbyRequest req{};
		req.op = LobbyOp::List;
		req.endpoint = ep;
		GetWorker().Post(std::move(req), e.section);
	}
}

void RenderBrowser() {
	State& st = GetState();
	Browser& br = st.browser;

	bool open = true;
	if (!BeginSheet("Online Play", &open,
	                ImVec2(Dp(560), Dp(420)),
	                ImVec2(Dp(1100), Dp(800)))) {
		return;
	}

	// Header row: title + refresh + prefs + host button.
	const ATMobilePalette &p = ATMobileGetPalette();
	ImGui::PushFont(nullptr);

	ImGui::PushStyleColor(ImGuiCol_Text, ATMobileCol(p.text));
	ImGui::TextUnformatted("Browse hosted games");
	ImGui::PopStyleColor();

	ImGui::SameLine();
	const char *line = br.statusLine.empty()
		? (br.refreshInFlight ? "Refreshing…" : "")
		: br.statusLine.c_str();
	ImGui::PushStyleColor(ImGuiCol_Text, ATMobileCol(p.textMuted));
	ImGui::Text("  %s", line);
	ImGui::PopStyleColor();

	// Top-right action bar.
	float rightBtnW = Dp(96);
	ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX()
		- rightBtnW * 3 - Dp(20));
	if (ATTouchButton("Refresh", ImVec2(rightBtnW, Dp(40)),
	                  ATTouchButtonStyle::Neutral)) {
		br.refreshRequested = true;
	}
	ImGui::SameLine(0, Dp(6));
	if (ATTouchButton("Prefs", ImVec2(rightBtnW, Dp(40)),
	                  ATTouchButtonStyle::Neutral)) {
		Navigate(Screen::Prefs);
	}
	ImGui::SameLine(0, Dp(6));
	if (ATTouchButton("Host", ImVec2(rightBtnW, Dp(40)),
	                  ATTouchButtonStyle::Accent)) {
		Navigate(Screen::HostSetup);
	}
	ImGui::PopFont();

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	// Grid of tiles.
	ImVec2 avail = ImGui::GetContentRegionAvail();
	ImGui::BeginChild("##sessions", avail, false,
		ImGuiWindowFlags_HorizontalScrollbar);
	ATTouchDragScroll();

	if (br.items.empty()) {
		ATTouchMutedText(br.refreshInFlight
			? "Loading sessions…"
			: "No sessions right now.  Host one!");
	} else {
		BeginScreenGrid(/*columns*/ 4, /*minTileWidth*/ Dp(220),
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
			if (SessionTile(ti, ImGui::GetContentRegionAvail().x > 0
			                ? ImVec2(0, 0)  // unused — grid dictates size
			                : ImVec2(Dp(220), Dp(200)))) {
				br.selectedIdx = (int)i;
				st.session.joinTarget = s;
				Navigate(s.requiresCode ? Screen::JoinPrompt
				                        : Screen::JoinConfirm);
			}
		}
		EndScreenGrid();
	}

	ATTouchEndDragScroll();
	ImGui::EndChild();

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

	ATTouchSection("Confirm");
	PeerChip(st.session.joinTarget.hostHandle.c_str(),
	         st.session.joinTarget.region.c_str(),
	         /*isPrivate*/ false);
	ATTouchMutedText(st.session.joinTarget.cartName.c_str());

	ImGui::Spacing();
	ATTouchMutedText(
		"The host will send a snapshot of their running emulator so "
		"both sides start from the same point.  Your current game "
		"will be replaced.");

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
	bool open = true;
	if (!BeginSheet("Waiting", &open,
	                ImVec2(Dp(420), Dp(300)),
	                ImVec2(Dp(620), Dp(460))))
		return;

	// The label depends on whether we're hosting or joining + the
	// coordinator's phase.  Pull phase via the glue module.
	const char *label = "Waiting for peer…";
	int severity = 0;
	bool spin = true;
	if (ATNetplayGlue::IsLockstepping()) {
		label = "Playing Online";
		severity = 3;
		spin = false;
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

	if (!open) Navigate(Screen::Closed);
	EndSheet();
}

// -------------------------------------------------------------------
// Preferences
// -------------------------------------------------------------------

void RenderPrefs() {
	State& st = GetState();
	bool open = true;
	if (!BeginSheet("Netplay Preferences", &open,
	                ImVec2(Dp(480), Dp(520)),
	                ImVec2(Dp(720), Dp(780))))
		return;

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
	const char *accItems[] = { "Auto-accept", "Prompt me", "Review each" };
	if (ATTouchSegmented("##acc", &acc, accItems, 3)) {
		st.prefs.acceptMode = (AcceptMode)acc;
	}

	ImGui::Spacing();
	ATTouchSection("Notifications");
	ATTouchToggle("Flash window", &st.prefs.notif.flashWindow);
	ATTouchToggle("System notification", &st.prefs.notif.systemNotify);
	ATTouchToggle("Chime", &st.prefs.notif.playChime);
	ATTouchToggle("Steal focus on attention",
		&st.prefs.focusOnAttention);

	ImGui::Spacing();
	ATTouchSection("Input delay");
	ATTouchSlider("LAN (frames)",
		&st.prefs.defaultInputDelayLan, 1, 10, "%d frames");
	ATTouchSlider("Internet (frames)",
		&st.prefs.defaultInputDelayInternet, 1, 10, "%d frames");

	ImGui::Spacing();
	ATTouchSection("Advanced");
	ATTouchToggle("Show manual-IP join (offline fallback)",
		&st.prefs.advancedManualIp);

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
	if (ATTouchButton("Done", ImVec2(-FLT_MIN, Dp(48)),
	                  ATTouchButtonStyle::Accent)) {
		SaveToRegistry();
		Back();
	}

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

	ATTouchSection(st.activity == UserActivity::Idle
		? "Listed on the lobby"
		: (st.activity == UserActivity::InSession
		   ? "Playing — other hostedGames suspended"
		   : "Single-player — hostedGames suspended"));

	// Top toolbar.
	float btnW = Dp(200);
	bool atCap = (st.hostedGames.size() >= State::kMaxHostedGames);
	ImGui::BeginDisabled(atCap);
	if (ATTouchButton("+ Add Game", ImVec2(btnW, Dp(44)),
	                  ATTouchButtonStyle::Accent)) {
		st.editingGameId.clear();
		Navigate(Screen::AddGame);
	}
	ImGui::EndDisabled();
	ImGui::SameLine(0, Dp(8));
	if (ATTouchButton("Browse Hosted Games", ImVec2(btnW, Dp(44)),
	                  ATTouchButtonStyle::Neutral)) {
		Navigate(Screen::Browser);
	}
	ImGui::SameLine(0, Dp(8));
	if (ATTouchButton("Preferences", ImVec2(btnW, Dp(44)),
	                  ATTouchButtonStyle::Neutral)) {
		Navigate(Screen::Prefs);
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::BeginChild("##hostedGames", ImGui::GetContentRegionAvail(),
		false, 0);
	ATTouchDragScroll();

	if (st.hostedGames.empty()) {
		ATTouchMutedText(
			"You haven't added any games yet.  Tap + Add Game to pick "
			"one from your library or from a file.");
	} else {
		std::string pendingToggle, pendingRemove;
		for (auto& o : st.hostedGames) {
			ImGui::PushID(o.id.c_str());

			char subtitle[200];
			std::snprintf(subtitle, sizeof subtitle, "%s \xC2\xB7 %s \xC2\xB7 %s",
				MachineConfigSummary(o.config),
				o.isPrivate ? "Private" : "Public",
				OfferStateLabelMobile(o.state));
			bool tapped = ATTouchListItem(o.gameName.c_str(), subtitle,
				false, false);
			(void)tapped;  // tile tap is currently a no-op; actions below

			if (!o.lastError.empty()) {
				ATTouchMutedText(o.lastError.c_str());
			}

			// Severity badge + quick actions.
			StatusBadge(OfferStateLabelMobile(o.state),
				OfferStateSeverity(o.state),
				o.state == HostedGameState::Handshaking);

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
	ImGui::EndChild();

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
			false, 0);
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
		if (s.hostedGames.size() < State::kMaxHostedGames) {
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
// Screen dispatcher
// -------------------------------------------------------------------

} // anonymous namespace

// Public — called from the common render.
bool ATNetplayUI_DispatchScreen() {
	State& st = GetState();
	if (st.screen == Screen::Closed) return false;

	switch (st.screen) {
		case Screen::Nickname:     RenderNickname();    break;
		case Screen::Browser:      RenderBrowser();     break;
		case Screen::MyHostedGames:RenderMyHostedGames();break;
		case Screen::AddGame:     RenderAddOffer();    break;
		case Screen::HostSetup:    RenderHostSetup();   break;
		case Screen::JoinPrompt:   RenderJoinPrompt();  break;
		case Screen::JoinConfirm:  RenderJoinConfirm(); break;
		case Screen::Waiting:      RenderWaiting();     break;
		case Screen::Prefs:        RenderPrefs();       break;
		default: break;
	}

	// Global Escape-to-back handler.  Matches other dialogs: press
	// Escape to pop; no-op if the back-stack is empty.
	if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
		if (!Back()) Navigate(Screen::Closed);
	}
	return true;
}

// Exposed to the menu/refresh path.
void ATNetplayUI_EnqueueBrowserRefresh() {
	EnqueueBrowserRefresh();
}

} // namespace ATNetplayUI
