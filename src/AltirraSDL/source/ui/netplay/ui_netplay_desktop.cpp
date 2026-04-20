//	AltirraSDL - Online Play desktop-native screens
//
//	Desktop Mode's conventions (menu bar + standard ImGui dialogs with
//	text widgets) don't match the Gaming Mode card aesthetic, so the
//	Desktop path uses this file — plain ImGui::Begin windows, Button /
//	Selectable / InputText without the big touch chrome.  Same state,
//	same actions as the mobile flow; only the widgets differ.
//
//	Selection policy mirrors e.g. Firmware Manager: table-style list
//	of sessions, toolbar above, action buttons at the bottom.

#include <stdafx.h>

#include "ui_netplay_state.h"
#include "ui_netplay.h"
#include "ui_netplay_actions.h"
#include "netplay/netplay_glue.h"
#include "ui/core/ui_main.h"
#include "ui/gamelibrary/game_library.h"
#include "ui_file_dialog_sdl3.h"
#include <SDL3/SDL_dialog.h>

#include <vd2/system/registry.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/filesys.h>

#include "simulator.h"
#include "firmwaremanager.h"

#include <imgui.h>

extern ATSimulator g_sim;

extern SDL_Window *g_pWindow;
extern VDStringA ATGetConfigDir();

#include <cstdio>
#include <cstring>

namespace ATNetplayUI {

namespace {

void CenterNext(ImVec2 size) {
	ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
}

// -----------------------------------------------------------------------
// Nickname prompt — tiny modal
// -----------------------------------------------------------------------
void DesktopNickname() {
	State& st = GetState();
	CenterNext(ImVec2(420, 200));
	bool open = true;
	if (!ImGui::Begin("Choose a Nickname##netplay", &open,
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoCollapse)) {
		ImGui::End();
		if (!open) Navigate(Screen::Closed);
		return;
	}

	ImGui::TextWrapped(
		"Other players will see this nickname when you host or join.");
	ImGui::Spacing();

	static char buf[32] = {};
	static bool seeded = false;
	if (!seeded) {
		std::snprintf(buf, sizeof buf, "%s", st.prefs.nickname.c_str());
		seeded = true;
		ImGui::SetKeyboardFocusHere();
	}
	ImGui::PushItemWidth(-FLT_MIN);
	ImGui::InputText("##name", buf, sizeof buf);
	ImGui::PopItemWidth();

	ImGui::Checkbox("Anonymous (random nickname each session)",
		&st.prefs.isAnonymous);

	ImGui::Spacing();
	ImGui::Separator();

	bool valid = st.prefs.isAnonymous || buf[0] != 0;
	ImGui::BeginDisabled(!valid);
	if (ImGui::Button("Save", ImVec2(120, 0))) {
		st.prefs.nickname = buf;
		SaveToRegistry();
		seeded = false;
		Navigate(Screen::Browser);
		st.browser.refreshRequested = true;
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0))) {
		seeded = false;
		Navigate(Screen::Closed);
	}
	if (!open) { seeded = false; Navigate(Screen::Closed); }
	ImGui::End();
}

// -----------------------------------------------------------------------
// Online browser — table-style
// -----------------------------------------------------------------------
void DesktopBrowser() {
	State& st = GetState();
	Browser& br = st.browser;

	CenterNext(ImVec2(760, 500));
	bool open = true;
	if (!ImGui::Begin("Online Play — Browse Hosted Games##netplay", &open,
		ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		if (!open) Navigate(Screen::Closed);
		return;
	}

	// Activity banner when the user is hosting / in session — the
	// session list below filters out the user's own hostedGames, so
	// without this the Browser looks empty and the "End Session"
	// button has no context.
	if (st.activity == UserActivity::InSession) {
		ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1),
			"You're currently playing online.  Open Host Games to "
			"see the match details or end the session.");
		if (ImGui::Button("Open Host Games", ImVec2(160, 0))) {
			Navigate(Screen::MyHostedGames);
		}
		ImGui::SameLine();
	}

	// Toolbar.
	if (ImGui::Button("Refresh")) br.refreshRequested = true;
	ImGui::SameLine();
	if (ImGui::Button("Host Games...")) {
		Navigate(Screen::MyHostedGames);
	}
	ImGui::SameLine();
	if (ImGui::Button("Preferences...")) Navigate(Screen::Prefs);
	ImGui::SameLine();
	if (ATNetplayGlue::IsActive()) {
		if (ImGui::Button("End Session")) {
			StopHostingAction();
		}
	}

	ImGui::SameLine();
	// Red-ish colour if the last response was an error; otherwise dim.
	bool isError = !br.statusLine.empty()
		&& (br.statusLine.find("fail") != std::string::npos
		    || br.statusLine.find("unreachable") != std::string::npos
		    || br.statusLine.find("HTTP ") != std::string::npos);
	if (isError) {
		ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1.0f), " %s",
			br.statusLine.c_str());
	} else {
		ImGui::TextDisabled(" %s",
			br.statusLine.empty()
			? (br.refreshInFlight ? "Refreshing..." : "")
			: br.statusLine.c_str());
	}

	ImGui::Separator();

	const ImGuiTableFlags tflags =
		ImGuiTableFlags_RowBg    | ImGuiTableFlags_Borders   |
		ImGuiTableFlags_Resizable | ImGuiTableFlags_SortTristate |
		ImGuiTableFlags_ScrollY   | ImGuiTableFlags_Sortable;
	ImVec2 tableSize(0, ImGui::GetContentRegionAvail().y - 50);
	if (ImGui::BeginTable("##lobby", 6, tflags, tableSize)) {
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Game",       ImGuiTableColumnFlags_WidthStretch, 2.0f);
		ImGui::TableSetupColumn("Host",       ImGuiTableColumnFlags_WidthStretch, 1.2f);
		ImGui::TableSetupColumn("Region",     ImGuiTableColumnFlags_WidthFixed,   80.0f);
		ImGui::TableSetupColumn("Players",    ImGuiTableColumnFlags_WidthFixed,   70.0f);
		ImGui::TableSetupColumn("Visibility", ImGuiTableColumnFlags_WidthFixed,   90.0f);
		ImGui::TableSetupColumn("Protocol",   ImGuiTableColumnFlags_WidthFixed,   70.0f);
		ImGui::TableHeadersRow();

		for (size_t i = 0; i < br.items.size(); ++i) {
			const auto& s = br.items[i];
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			// Full-row selectable on the first column.
			ImGui::PushID((int)i);
			bool selected = ((int)i == br.selectedIdx);
			ImGuiSelectableFlags sf = ImGuiSelectableFlags_SpanAllColumns |
			                          ImGuiSelectableFlags_AllowDoubleClick;
			if (ImGui::Selectable(s.cartName.c_str(), selected, sf)) {
				br.selectedIdx = (int)i;
				if (ImGui::IsMouseDoubleClicked(0)) {
					st.session.joinTarget = s;
					Navigate(s.requiresCode ? Screen::JoinPrompt
					                        : Screen::JoinConfirm);
				}
			}
			ImGui::PopID();

			ImGui::TableNextColumn();
			ImGui::TextUnformatted(s.hostHandle.empty()
				? "Anonymous" : s.hostHandle.c_str());
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(s.region.c_str());
			ImGui::TableNextColumn();
			ImGui::Text("%d/%d", s.playerCount, s.maxPlayers);
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(s.requiresCode ? "Private" : "Public");
			ImGui::TableNextColumn();
			ImGui::Text("v%d", s.protocolVersion);
		}
		ImGui::EndTable();
	}

	ImGui::Separator();
	bool canJoin = br.selectedIdx >= 0
	               && br.selectedIdx < (int)br.items.size()
	               && !ATNetplayGlue::IsActive();
	ImGui::BeginDisabled(!canJoin);
	if (ImGui::Button("Join", ImVec2(140, 0))) {
		st.session.joinTarget = br.items[br.selectedIdx];
		Navigate(st.session.joinTarget.requiresCode
			? Screen::JoinPrompt : Screen::JoinConfirm);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Close", ImVec2(120, 0))) Navigate(Screen::Closed);

	if (!open) Navigate(Screen::Closed);
	ImGui::End();
}

// -----------------------------------------------------------------------
// Host setup — plain form
// -----------------------------------------------------------------------
void DesktopHostSetup() {
	State& st = GetState();
	CenterNext(ImVec2(480, 320));
	bool open = true;
	if (!ImGui::Begin("Host a Game##netplay", &open,
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
		ImGui::End();
		if (!open) Back();
		return;
	}

	// Derive the "currently booted game" display from the MRU head.
	// We don't cache in st.session.pendingCartName here because the
	// user might boot a new game with the dialog still open and we
	// want the panel to refresh.
	VDStringA bootedBase;
	{
		VDRegistryAppKey mru("MRU List", false);
		VDStringW order;
		mru.getString("Order", order);
		if (!order.empty()) {
			char kn[2] = { (char)order[0], 0 };
			VDStringW path;
			if (mru.getString(kn, path) && !path.empty()) {
				const wchar_t *last = nullptr;
				for (const wchar_t *q = path.c_str(); *q; ++q)
					if (*q == L'/' || *q == L'\\') last = q;
				VDStringW base = last ? VDStringW(last + 1) : path;
				bootedBase = VDTextWToU8(base);
			}
		}
	}
	const bool hasGame = !bootedBase.empty();

	ImGui::Text("Game:");
	ImGui::SameLine();
	if (hasGame) {
		ImGui::TextColored(ImVec4(0.8f, 0.95f, 0.8f, 1.0f), "%s",
			bootedBase.c_str());
	} else {
		ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.6f, 1.0f),
			"(no game booted)");
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Boot a Game...")) {
		ATUIShowBootImageDialog(g_pWindow);
	}

	ImGui::Spacing();
	ImGui::Text("Visibility:");
	int vis = st.session.hostingPrivate ? 1 : 0;
	ImGui::RadioButton("Public", &vis, 0); ImGui::SameLine();
	ImGui::RadioButton("Private", &vis, 1);
	st.session.hostingPrivate = (vis == 1);

	if (vis == 1) {
		ImGui::Text("Entry code:");
		static char code[32] = {};
		static bool seeded = false;
		if (!seeded) {
			std::snprintf(code, sizeof code, "%s",
				st.prefs.lastEntryCode.c_str());
			seeded = true;
		}
		ImGui::PushItemWidth(240);
		ImGui::InputText("##code", code, sizeof code);
		ImGui::PopItemWidth();
		st.session.hostingEntryCode = code;
	}

	ImGui::TextWrapped(vis == 1
		? "Friends-only: joiners must enter the code you share."
		: "Anyone on the lobby can see and join this session.");

	ImGui::Spacing();
	ImGui::Separator();

	bool canHost = hasGame
		&& (!st.session.hostingPrivate
			|| !st.session.hostingEntryCode.empty());
	ImGui::BeginDisabled(!canHost);
	if (ImGui::Button("Start Hosting", ImVec2(160, 0))) {
		st.prefs.lastEntryCode = st.session.hostingEntryCode;
		SaveToRegistry();
		StartHostingAction();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0))) Back();

	if (!open) Back();
	ImGui::End();
}

// -----------------------------------------------------------------------
// Join prompt / confirm / waiting / prefs / error — minimal modals.
// -----------------------------------------------------------------------
void DesktopJoinPrompt() {
	State& st = GetState();
	CenterNext(ImVec2(400, 200));
	bool open = true;
	if (!ImGui::Begin("Enter Join Code##netplay", &open,
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
		ImGui::End();
		if (!open) Back();
		return;
	}
	ImGui::Text("Host:");
	ImGui::SameLine();
	ImGui::TextUnformatted(st.session.joinTarget.hostHandle.c_str());
	ImGui::Text("Game:");
	ImGui::SameLine();
	ImGui::TextUnformatted(st.session.joinTarget.cartName.c_str());
	ImGui::Spacing();

	static char code[32] = {};
	static bool seeded = false;
	if (!seeded) { code[0] = 0; seeded = true; ImGui::SetKeyboardFocusHere(); }
	ImGui::Text("Entry code:");
	ImGui::PushItemWidth(240);
	ImGui::InputText("##code", code, sizeof code);
	ImGui::PopItemWidth();
	st.session.joinEntryCode = code;

	ImGui::Separator();
	ImGui::BeginDisabled(code[0] == 0);
	if (ImGui::Button("Join", ImVec2(120, 0))) {
		seeded = false;
		StartJoiningAction();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0))) { seeded = false; Back(); }

	if (!open) { seeded = false; Back(); }
	ImGui::End();
}

void DesktopJoinConfirm() {
	State& st = GetState();
	CenterNext(ImVec2(460, 240));
	bool open = true;
	if (!ImGui::Begin("Join Session##netplay", &open,
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
		ImGui::End();
		if (!open) Back();
		return;
	}
	ImGui::Text("Host:");
	ImGui::SameLine();
	ImGui::TextUnformatted(st.session.joinTarget.hostHandle.c_str());
	ImGui::Text("Game:");
	ImGui::SameLine();
	ImGui::TextUnformatted(st.session.joinTarget.cartName.c_str());
	ImGui::Spacing();
	ImGui::TextWrapped(
		"The host will send a snapshot of their running emulator so both "
		"sides start from the same point. Your current game will be "
		"replaced.");

	ImGui::Separator();
	if (ImGui::Button("Join", ImVec2(120, 0))) StartJoiningAction();
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0))) Back();

	if (!open) Back();
	ImGui::End();
}

void DesktopWaiting() {
	State& st = GetState();

	// Auto-dismiss once Lockstepping is reached — the game is
	// running and the user should see it, not a dialog on top.
	// A small HUD badge (TODO) can show ongoing connection info.
	if (ATNetplayGlue::IsLockstepping()) {
		Navigate(Screen::Closed);
		return;
	}

	CenterNext(ImVec2(420, 220));
	bool open = true;
	if (!ImGui::Begin("Connecting##netplay", &open,
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
		ImGui::End();
		if (!open) Navigate(Screen::Closed);
		return;
	}

	ImGui::Text("Connecting — loading the game state…");
	ImGui::Spacing();

	ImGui::Text("Host: %s", st.session.joinTarget.hostHandle.empty()
		? ResolvedNickname().c_str()
		: st.session.joinTarget.hostHandle.c_str());
	ImGui::Text("Game: %s", st.session.joinTarget.cartName.empty()
		? st.session.pendingCartName.c_str()
		: st.session.joinTarget.cartName.c_str());
	if (!st.session.lobbySessionId.empty()) {
		ImGui::TextDisabled("Listed on lobby");
	} else if (ATNetplayGlue::IsActive() && !st.session.lastError.empty()) {
		// Host is running but lobby announce failed — show why.
		ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1.0f),
			"Lobby: %s", st.session.lastError.c_str());
		ImGui::TextDisabled("Session is up P2P — peers on the same "
			"machine can still join at 127.0.0.1:%u",
			(unsigned)st.session.boundPort);
	}

	ImGui::Separator();
	if (ImGui::Button("End Session", ImVec2(140, 0))) {
		StopHostingAction();
		Navigate(Screen::Browser);
	}
	ImGui::SameLine();
	if (ImGui::Button("Minimise", ImVec2(120, 0))) {
		Navigate(Screen::Closed);
	}
	if (!open) Navigate(Screen::Closed);
	ImGui::End();
}

void DesktopPrefs() {
	State& st = GetState();
	CenterNext(ImVec2(520, 480));
	bool open = true;
	if (!ImGui::Begin("Netplay Preferences##netplay", &open,
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
		ImGui::End();
		if (!open) Back();
		return;
	}

	ImGui::SeparatorText("Nickname");
	static char nameBuf[32] = {};
	static bool seeded = false;
	if (!seeded) {
		std::snprintf(nameBuf, sizeof nameBuf, "%s",
			st.prefs.nickname.c_str());
		seeded = true;
	}
	ImGui::PushItemWidth(260);
	if (ImGui::InputText("Nickname", nameBuf, sizeof nameBuf))
		st.prefs.nickname = nameBuf;
	ImGui::PopItemWidth();
	ImGui::Checkbox("Anonymous (random nickname each session)",
		&st.prefs.isAnonymous);

	ImGui::SeparatorText("Accept incoming joins");
	int acc = (int)st.prefs.acceptMode;
	ImGui::RadioButton("Auto-accept",  &acc, 0); ImGui::SameLine();
	ImGui::RadioButton("Prompt me",    &acc, 1); ImGui::SameLine();
	ImGui::RadioButton("Review each",  &acc, 2);
	st.prefs.acceptMode = (AcceptMode)acc;

	ImGui::SeparatorText("Notifications");
	ImGui::Checkbox("Flash window",        &st.prefs.notif.flashWindow);
	ImGui::Checkbox("System notification", &st.prefs.notif.systemNotify);
	ImGui::Checkbox("Chime",               &st.prefs.notif.playChime);
	ImGui::Checkbox("Steal focus on attention",
		&st.prefs.focusOnAttention);

	ImGui::SeparatorText("Input delay");
	ImGui::SliderInt("LAN (frames)",      &st.prefs.defaultInputDelayLan, 1, 10);
	ImGui::SliderInt("Internet (frames)", &st.prefs.defaultInputDelayInternet, 1, 10);

	ImGui::SeparatorText("Advanced");
	ImGui::Checkbox("Show manual-IP join",
		&st.prefs.advancedManualIp);

	ImGui::Separator();
	if (ImGui::Button("Done", ImVec2(120, 0))) {
		SaveToRegistry();
		seeded = false;
		Back();
	}
	if (!open) { SaveToRegistry(); seeded = false; Back(); }
	ImGui::End();
}

// -----------------------------------------------------------------------
// My Hosted Games — the user's offer list.
// -----------------------------------------------------------------------

// Map HostedGameState → short label + colour for the status cell.
const char *OfferStateLabel(HostedGameState s) {
	switch (s) {
		case HostedGameState::Draft:       return "Draft";
		case HostedGameState::Listed:      return "Listed";
		case HostedGameState::Handshaking: return "Connecting";
		case HostedGameState::Playing:     return "Playing";
		case HostedGameState::Suspended:   return "Suspended";
		case HostedGameState::Failed:      return "Failed";
	}
	return "?";
}
ImVec4 OfferStateColour(HostedGameState s) {
	const bool dark = ATUIIsDarkTheme();
	switch (s) {
		case HostedGameState::Draft:
			return dark ? ImVec4(0.70f, 0.70f, 0.70f, 1)
			            : ImVec4(0.45f, 0.45f, 0.45f, 1);
		case HostedGameState::Listed:
			return dark ? ImVec4(0.55f, 0.85f, 0.55f, 1)
			            : ImVec4(0.10f, 0.55f, 0.15f, 1);
		case HostedGameState::Handshaking:
			return dark ? ImVec4(0.95f, 0.85f, 0.40f, 1)
			            : ImVec4(0.70f, 0.50f, 0.05f, 1);
		case HostedGameState::Playing:
			return dark ? ImVec4(0.40f, 0.85f, 1.00f, 1)
			            : ImVec4(0.10f, 0.40f, 0.70f, 1);
		case HostedGameState::Suspended:
			return dark ? ImVec4(0.85f, 0.70f, 0.40f, 1)
			            : ImVec4(0.60f, 0.40f, 0.10f, 1);
		case HostedGameState::Failed:
			return dark ? ImVec4(1.00f, 0.55f, 0.45f, 1)
			            : ImVec4(0.80f, 0.10f, 0.10f, 1);
	}
	return dark ? ImVec4(1, 1, 1, 1) : ImVec4(0, 0, 0, 1);
}

const char *ActivityBannerText(UserActivity a, size_t gameCount) {
	switch (a) {
		case UserActivity::Idle:
			if (gameCount == 0) return "Add a game to start hosting.";
			return "Ready — your games are listed on the lobby.";
		case UserActivity::PlayingLocal:
			return "You're playing single-player — hosted games are paused "
				"until you stop.";
		case UserActivity::InSession:
			return "An online match is in progress — your other hosted "
				"games are paused while you play.";
	}
	return "";
}

void DesktopMyHostedGames() {
	State& st = GetState();
	CenterNext(ImVec2(820, 520));
	bool open = true;
	if (!ImGui::Begin("Online Play — Host Games##netplay", &open,
		ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		if (!open) Navigate(Screen::Closed);
		return;
	}

	// Activity banner.
	ImVec4 bc = (st.activity == UserActivity::Idle)
		? ImVec4(0.55f, 0.85f, 0.55f, 1)
		: ImVec4(0.95f, 0.75f, 0.4f, 1);
	ImGui::TextColored(bc, "%s",
		ActivityBannerText(st.activity, st.hostedGames.size()));

	ImGui::Separator();

	bool atCap = (st.hostedGames.size() >= State::kMaxHostedGames);
	ImGui::BeginDisabled(atCap);
	if (ImGui::Button("Add Game...", ImVec2(140, 0))) {
		st.editingGameId.clear();
		Navigate(Screen::AddGame);
	}
	ImGui::EndDisabled();
	if (atCap) {
		ImGui::SameLine();
		ImGui::TextDisabled("(%zu/%zu — remove one first)",
			st.hostedGames.size(), State::kMaxHostedGames);
	}

	ImGui::SameLine();
	if (ImGui::Button("Browse Hosted Games...", ImVec2(200, 0))) {
		Navigate(Screen::Browser);
	}
	ImGui::SameLine();
	if (ImGui::Button("Preferences...", ImVec2(140, 0))) {
		Navigate(Screen::Prefs);
	}

	ImGui::Separator();

	const ImGuiTableFlags tflags =
		ImGuiTableFlags_RowBg    | ImGuiTableFlags_Borders   |
		ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
	ImVec2 tableSize(0, ImGui::GetContentRegionAvail().y - 50);
	if (ImGui::BeginTable("##hostedGames", 6, tflags, tableSize)) {
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Game",       ImGuiTableColumnFlags_WidthStretch, 2.5f);
		ImGui::TableSetupColumn("Visibility", ImGuiTableColumnFlags_WidthFixed,   90.0f);
		ImGui::TableSetupColumn("State",      ImGuiTableColumnFlags_WidthFixed,  110.0f);
		ImGui::TableSetupColumn("Port",       ImGuiTableColumnFlags_WidthFixed,   70.0f);
		ImGui::TableSetupColumn("Enabled",    ImGuiTableColumnFlags_WidthFixed,   80.0f);
		ImGui::TableSetupColumn("Actions",    ImGuiTableColumnFlags_WidthFixed,  130.0f);
		ImGui::TableHeadersRow();

		// Collect pending actions to apply AFTER the loop (can't mutate
		// state.hostedGames mid-iteration).
		std::string pendingRemove;
		std::string pendingToggle;  // non-empty: flip enabled of this id

		for (auto& o : st.hostedGames) {
			ImGui::PushID(o.id.c_str());
			ImGui::TableNextRow();

			ImGui::TableNextColumn();
			ImGui::TextUnformatted(o.gameName.c_str());
			ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.70f, 1.0f),
				"  %s", MachineConfigSummary(o.config));
			if (!o.lastError.empty()) {
				ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1.0f),
					"  %s", o.lastError.c_str());
			}

			ImGui::TableNextColumn();
			ImGui::TextUnformatted(o.isPrivate ? "Private" : "Public");

			ImGui::TableNextColumn();
			ImGui::TextColored(OfferStateColour(o.state), "%s",
				OfferStateLabel(o.state));

			ImGui::TableNextColumn();
			if (o.boundPort) ImGui::Text("%u", (unsigned)o.boundPort);
			else             ImGui::TextDisabled("—");

			ImGui::TableNextColumn();
			bool en = o.enabled;
			if (ImGui::Checkbox("##en", &en)) {
				pendingToggle = o.id;
			}

			ImGui::TableNextColumn();
			if (ImGui::SmallButton("Remove")) {
				pendingRemove = o.id;
			}

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

		ImGui::EndTable();
	}

	ImGui::Separator();
	if (ImGui::Button("Close", ImVec2(120, 0))) Navigate(Screen::Closed);

	if (!open) Navigate(Screen::Closed);
	ImGui::End();
}

// -----------------------------------------------------------------------
// Add Offer modal — pick from library OR choose file.
// -----------------------------------------------------------------------

// Staged fields for the Add Offer form.  File-dialog result lands here
// from the callback.
namespace {
	VDStringW s_pickedPath;
	bool      s_pickedPathPending = false;
	int       s_addSource = 0;        // 0 = Library, 1 = File
	bool      s_addPrivate = false;
	char      s_addEntryCode[32] = {};
	int       s_librarySel = -1;
	// Seeded from the current emulator config on first Add-Game open;
	// the user can override via the Machine Config section or reset
	// via the "Copy from current emulator" button.
	MachineConfig s_addConfig;
	bool          s_addConfigSeeded = false;

	// Firmware dropdown cache.  Built lazily on first dialog open;
	// each entry caches the firmware's CRC32 so we don't re-read the
	// ROM bytes every frame.  Index 0 is a sentinel meaning
	// "Altirra default for the hardware mode" (CRC = 0 in the
	// MachineConfig → ApplyMachineConfig does GetFirmwareOfType).
	struct FirmwareChoice {
		uint64_t    id;
		uint32_t    crc32;
		std::string label;   // "Name (filename) [CRC32]"
	};
	std::vector<FirmwareChoice> s_kernelChoices;
	std::vector<FirmwareChoice> s_basicChoices;
	bool                        s_firmwareChoicesLoaded = false;

	void ReloadFirmwareChoices() {
		s_kernelChoices.clear();
		s_basicChoices.clear();
		s_kernelChoices.push_back({
			0, 0,
			std::string("(Altirra default for hardware)")});
		s_basicChoices.push_back({
			0, 0,
			std::string("(None)")});

		ATFirmwareManager *fwm = g_sim.GetFirmwareManager();
		if (!fwm) { s_firmwareChoicesLoaded = true; return; }

		vdvector<ATFirmwareInfo> fwList;
		fwm->GetFirmwareList(fwList);
		for (const auto& fw : fwList) {
			if (!fw.mbVisible) continue;

			const uint32_t crc = ComputeFirmwareCRC32(fw.mId);
			VDStringA nameU8 = VDTextWToU8(fw.mName);

			char buf[384];
			if (!fw.mPath.empty()) {
				VDStringA fnU8 = VDTextWToU8(
					VDStringW(VDFileSplitPath(fw.mPath.c_str())));
				std::snprintf(buf, sizeof buf, "%s (%s) [%08X]",
					nameU8.c_str(), fnU8.c_str(), crc);
			} else {
				std::snprintf(buf, sizeof buf, "%s (internal) [%08X]",
					nameU8.c_str(), crc);
			}

			FirmwareChoice c;
			c.id    = fw.mId;
			c.crc32 = crc;
			c.label = buf;

			if (ATIsKernelFirmwareType(fw.mType))
				s_kernelChoices.push_back(std::move(c));
			else if (fw.mType == kATFirmwareType_Basic)
				s_basicChoices.push_back(std::move(c));
		}
		s_firmwareChoicesLoaded = true;
	}

	// Render a firmware combo that writes the chosen entry's CRC32
	// into *crcOut.  If the current CRC has no matching entry (user
	// selected a firmware that was later uninstalled), a synthetic
	// "(not installed: [XXXXXXXX])" label is inserted so the UI
	// reflects the persisted value rather than silently snapping to
	// the default.
	void FirmwareCombo(const char *label,
			const std::vector<FirmwareChoice>& choices,
			uint32_t *crcOut) {
		char synth[64];
		const char *selectedLabel = choices.empty()
			? "(no firmware manager)" : choices[0].label.c_str();

		int matchIdx = -1;
		for (size_t i = 0; i < choices.size(); ++i) {
			if (choices[i].crc32 == *crcOut) {
				matchIdx = (int)i;
				selectedLabel = choices[i].label.c_str();
				break;
			}
		}
		if (matchIdx < 0 && *crcOut != 0) {
			std::snprintf(synth, sizeof synth,
				"(not installed: [%08X])", *crcOut);
			selectedLabel = synth;
		} else if (matchIdx < 0) {
			matchIdx = 0;
		}

		ImGui::PushItemWidth(440);
		if (ImGui::BeginCombo(label, selectedLabel)) {
			for (size_t i = 0; i < choices.size(); ++i) {
				bool sel = ((int)i == matchIdx);
				if (ImGui::Selectable(choices[i].label.c_str(), sel))
					*crcOut = choices[i].crc32;
				if (sel) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		ImGui::PopItemWidth();
	}

	static const SDL_DialogFileFilter kAddOfferFilters[] = {
		{ "Game images",
		  "atr;xex;bin;car;rom;a52;a8s;exe;com;ucf;pro;xfd;atx;dcm;zip;cas" },
		{ "Disk images", "atr;xfd;atx;dcm;pro" },
		{ "Executables", "xex;exe;com;ucf;a8s" },
		{ "Cartridges",  "bin;car;rom;a52" },
		{ "Cassette",    "cas" },
		{ "All files",   "*" },
	};

	void AddOfferFileCallback(void*, const char * const *filelist, int) {
		if (!filelist || !filelist[0]) return;
		s_pickedPath = VDTextU8ToW(filelist[0], -1);
		s_pickedPathPending = true;
	}

	// Lazy singleton to avoid pulling Game Library init/shutdown into
	// this TU.  Reuses the user's configured library if available.
	void CommitNewOffer(const std::string& path, const std::string& name) {
		State& st = GetState();
		if (st.hostedGames.size() >= State::kMaxHostedGames) {
			st.session.lastError = "Too many games hosted — remove one first.";
			Navigate(Screen::Error);
			return;
		}
		HostedGame o;
		o.id           = GenerateHostedGameId();
		o.gamePath     = path;
		o.gameName     = name;
		o.isPrivate    = s_addPrivate;
		o.entryCode    = s_addEntryCode;
		o.config       = s_addConfig;
		o.enabled      = true;
		o.state        = HostedGameState::Draft;
		st.hostedGames.push_back(std::move(o));
		st.prefs.lastAddConfig = s_addConfig;
		SaveToRegistry();
		EnableHostedGame(st.hostedGames.back().id);

		// Reset staged fields (preset intentionally retained so the
		// next Add Game opens with the same choice).
		s_pickedPath.clear();
		s_pickedPathPending = false;
		s_addPrivate = false;
		s_addEntryCode[0] = 0;
		s_librarySel = -1;

		Navigate(Screen::MyHostedGames);
	}
}

void DesktopAddOffer() {
	if (!s_addConfigSeeded) {
		// Seed from the current emulator so the host's Add-Game
		// always starts with a config they can actually boot.
		s_addConfig = CaptureCurrentMachineConfig();
		s_addConfigSeeded = true;
	}
	if (!s_firmwareChoicesLoaded) {
		ReloadFirmwareChoices();
	}

	CenterNext(ImVec2(720, 540));
	bool open = true;
	if (!ImGui::Begin("Add Game to Host##netplay", &open,
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
		ImGui::End();
		if (!open) Back();
		return;
	}

	// Source picker (Library / File).
	ImGui::Text("Source:");
	ImGui::SameLine();
	ImGui::RadioButton("From Library", &s_addSource, 0);
	ImGui::SameLine();
	ImGui::RadioButton("From File",    &s_addSource, 1);

	ImGui::Separator();

	std::string stagedPath;
	std::string stagedName;

	if (s_addSource == 0) {
		ATGameLibrary& lib = LibrarySingleton();
		const auto& entries = lib.GetEntries();
		ImGui::Text("Select a game from your library (%zu entries):",
			entries.size());

		const ImGuiTableFlags tf = ImGuiTableFlags_RowBg |
			ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY;
		ImVec2 sz(0, ImGui::GetContentRegionAvail().y - 150);
		if (ImGui::BeginTable("##lib", 2, tf, sz)) {
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Game");
			ImGui::TableSetupColumn("Variants",
				ImGuiTableColumnFlags_WidthFixed, 80.0f);
			ImGui::TableHeadersRow();
			for (size_t i = 0; i < entries.size(); ++i) {
				const auto& e = entries[i];
				if (e.mVariants.empty()) continue;
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				bool sel = ((int)i == s_librarySel);
				VDStringA displayU8 = VDTextWToU8(e.mDisplayName);
				ImGui::PushID((int)i);
				if (ImGui::Selectable(displayU8.c_str(), sel,
					ImGuiSelectableFlags_SpanAllColumns)) {
					s_librarySel = (int)i;
				}
				ImGui::PopID();
				ImGui::TableNextColumn();
				ImGui::Text("%zu", e.mVariants.size());
			}
			ImGui::EndTable();
		}

		if (s_librarySel >= 0 && (size_t)s_librarySel < entries.size()) {
			const auto& e = entries[s_librarySel];
			if (!e.mVariants.empty()) {
				VDStringA u8Path = VDTextWToU8(e.mVariants[0].mPath);
				VDStringA u8Name = VDTextWToU8(e.mDisplayName);
				stagedPath.assign(u8Path.c_str(), u8Path.size());
				stagedName.assign(u8Name.c_str(), u8Name.size());
			}
		}
	} else {
		// File source.
		ImGui::Text("Selected file:");
		ImGui::SameLine();
		if (s_pickedPath.empty()) {
			ImGui::TextDisabled("(none)");
		} else {
			VDStringA u8 = VDTextWToU8(s_pickedPath);
			ImGui::TextUnformatted(u8.c_str());
		}
		if (ImGui::Button("Browse...##addoffer", ImVec2(140, 0))) {
			ATUIShowOpenFileDialog('npad', AddOfferFileCallback,
				nullptr, g_pWindow,
				kAddOfferFilters,
				(int)(sizeof kAddOfferFilters / sizeof kAddOfferFilters[0]),
				false);
		}
		if (!s_pickedPath.empty()) {
			VDStringA u8Path = VDTextWToU8(s_pickedPath);
			// Basename.
			const wchar_t *last = nullptr;
			for (const wchar_t *q = s_pickedPath.c_str(); *q; ++q)
				if (*q == L'/' || *q == L'\\') last = q;
			VDStringW base = last ? VDStringW(last + 1) : s_pickedPath;
			VDStringA u8Base = VDTextWToU8(base);
			stagedPath.assign(u8Path.c_str(), u8Path.size());
			stagedName.assign(u8Base.c_str(), u8Base.size());
		}
	}

	ImGui::Separator();

	// Machine configuration — applied only while the session is
	// running.  Never modifies Altirra's persistent settings.  The
	// joiner receives this over the wire and must match firmware by
	// CRC32.
	if (ImGui::CollapsingHeader("Machine configuration",
			ImGuiTreeNodeFlags_DefaultOpen)) {
		static const ATHardwareMode kHWVals[] = {
			kATHardwareMode_800, kATHardwareMode_800XL,
			kATHardwareMode_1200XL, kATHardwareMode_130XE,
			kATHardwareMode_1400XL, kATHardwareMode_XEGS,
			kATHardwareMode_5200,
		};
		static const char *kHWLabels[] = {
			"Atari 800", "Atari 800XL", "Atari 1200XL",
			"Atari 130XE", "Atari 1400XL", "Atari XEGS",
			"Atari 5200",
		};
		int hwIdx = 1;
		for (int i = 0; i < 7; ++i)
			if (kHWVals[i] == s_addConfig.hardwareMode) { hwIdx = i; break; }
		ImGui::PushItemWidth(220);
		if (ImGui::Combo("Hardware", &hwIdx, kHWLabels, 7))
			s_addConfig.hardwareMode = kHWVals[hwIdx];
		ImGui::PopItemWidth();

		static const ATVideoStandard kVSVals[] = {
			kATVideoStandard_NTSC, kATVideoStandard_PAL,
			kATVideoStandard_SECAM, kATVideoStandard_NTSC50,
			kATVideoStandard_PAL60,
		};
		static const char *kVSLabels[] = {
			"NTSC", "PAL", "SECAM", "NTSC50", "PAL60",
		};
		const bool is5200 = (s_addConfig.hardwareMode == kATHardwareMode_5200);
		int vsIdx = 0;
		for (int i = 0; i < 5; ++i)
			if (kVSVals[i] == s_addConfig.videoStandard) { vsIdx = i; break; }
		ImGui::BeginDisabled(is5200);
		ImGui::PushItemWidth(220);
		if (ImGui::Combo("Video standard", &vsIdx, kVSLabels, 5))
			s_addConfig.videoStandard = kVSVals[vsIdx];
		ImGui::PopItemWidth();
		ImGui::EndDisabled();

		ImGui::BeginDisabled(is5200);
		ImGui::Checkbox("BASIC enabled", &s_addConfig.basicEnabled);
		ImGui::EndDisabled();

		ImGui::Checkbox("SIO full-speed acceleration",
			&s_addConfig.sioPatchEnabled);

		ImGui::Spacing();
		ImGui::TextDisabled("Firmware (shipped by CRC32 — joiner must have a matching entry)");
		FirmwareCombo("OS kernel", s_kernelChoices,
			&s_addConfig.kernelCRC32);
		FirmwareCombo("BASIC",     s_basicChoices,
			&s_addConfig.basicCRC32);

		if (ImGui::Button("Copy from current emulator")) {
			s_addConfig = CaptureCurrentMachineConfig();
		}
		ImGui::SameLine();
		if (ImGui::Button("Refresh firmware list")) {
			ReloadFirmwareChoices();
		}
		ImGui::SameLine();
		ImGui::TextDisabled("  %s", MachineConfigSummary(s_addConfig));
	}

	ImGui::Separator();
	ImGui::Checkbox("Private (require entry code)", &s_addPrivate);
	if (s_addPrivate) {
		ImGui::SameLine();
		ImGui::PushItemWidth(200);
		ImGui::InputText("Code", s_addEntryCode, sizeof s_addEntryCode);
		ImGui::PopItemWidth();
	}

	ImGui::Separator();

	bool ready = !stagedPath.empty() && !stagedName.empty()
		&& (!s_addPrivate || s_addEntryCode[0] != 0);
	ImGui::BeginDisabled(!ready);
	if (ImGui::Button("Add Game", ImVec2(160, 0))) {
		CommitNewOffer(stagedPath, stagedName);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0))) Back();

	if (!open) Back();
	ImGui::End();
}

void DesktopError() {
	State& st = GetState();
	CenterNext(ImVec2(420, 180));
	bool open = true;
	if (!ImGui::Begin("Netplay Error##netplay", &open,
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
		ImGui::End();
		if (!open) Navigate(Screen::Browser);
		return;
	}
	ImGui::TextWrapped("%s", st.session.lastError.empty()
		? "Something went wrong."
		: st.session.lastError.c_str());
	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		st.session.lastError.clear();
		Navigate(Screen::Browser);
	}
	if (!open) Navigate(Screen::Browser);
	ImGui::End();
}

} // anonymous

// Called from ui_netplay.cpp when the caller is in Desktop mode.
bool DesktopDispatch() {
	State& st = GetState();
	switch (st.screen) {
		case Screen::Closed:       return false;
		case Screen::Nickname:     DesktopNickname();    break;
		case Screen::Browser:      DesktopBrowser();     break;
		case Screen::MyHostedGames:DesktopMyHostedGames(); break;
		case Screen::AddGame:     DesktopAddOffer();    break;
		case Screen::HostSetup:    DesktopHostSetup();   break;
		case Screen::JoinPrompt:   DesktopJoinPrompt();  break;
		case Screen::JoinConfirm:  DesktopJoinConfirm(); break;
		case Screen::Waiting:      DesktopWaiting();     break;
		case Screen::Prefs:        DesktopPrefs();       break;
		case Screen::Error:        DesktopError();       break;
		default: break;
	}
	return true;
}

} // namespace ATNetplayUI
