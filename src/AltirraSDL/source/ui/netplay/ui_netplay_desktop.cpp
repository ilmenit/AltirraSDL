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
#include <SDL3/SDL_timer.h>

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

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace ATNetplayUI {

namespace {

// Theme-aware semantic colours.  The Desktop UI runs against both the
// ImGui dark and light themes; hardcoded pastel accents (light green,
// peach, pale yellow) that looked fine on the dark backdrop wash out
// to near-invisible on the light-theme white.  These helpers pick the
// right shade for the current theme.
bool IsImGuiLightTheme() {
	const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
	// Dark themes paint text near white (sum ~3.0); light themes paint
	// text near black (sum ~0.0).  A midpoint of 1.2 separates them
	// robustly for every built-in theme and any custom tweak.
	return (t.x + t.y + t.z) < 1.2f;
}
ImVec4 EmphasisGood() {
	return IsImGuiLightTheme()
		? ImVec4(0.10f, 0.45f, 0.18f, 1.0f)
		: ImVec4(0.60f, 0.95f, 0.65f, 1.0f);
}
ImVec4 EmphasisBad() {
	return IsImGuiLightTheme()
		? ImVec4(0.70f, 0.15f, 0.10f, 1.0f)
		: ImVec4(1.00f, 0.70f, 0.60f, 1.0f);
}
// Render a SpecLine as a single horizontal line of coloured tokens
// separated by " | ".  Tokens flagged `missing` are painted with the
// theme-aware red (matches the row-name incompatible colour); all
// other tokens and the separators use `cDim`.  Caller controls the
// leading indent by calling this right after a `TextColored` that
// emitted no newline, or by leaving a blank first column — the
// function draws exactly one line and advances the ImGui cursor to
// the next line on exit.
void RenderSpecLine(const SpecLine& sl, const ImVec4& cDim) {
	const bool dark = ATUIIsDarkTheme();
	const ImVec4 cBad = dark ? ImVec4(1.00f, 0.55f, 0.45f, 1.0f)
	                         : ImVec4(0.80f, 0.10f, 0.10f, 1.0f);
	// Leading two-space indent to match the previous "  OS […] · …"
	// sub-row convention.
	ImGui::TextColored(cDim, "  ");
	for (size_t i = 0; i < sl.tokens.size(); ++i) {
		ImGui::SameLine(0, 0);
		const auto& tk = sl.tokens[i];
		ImGui::TextColored(tk.missing ? cBad : cDim, "%s", tk.text.c_str());
		if (i + 1 < sl.tokens.size()) {
			ImGui::SameLine(0, 0);
			ImGui::TextColored(cDim, " | ");
		}
	}
}

ImVec4 EmphasisWarning() {
	return IsImGuiLightTheme()
		? ImVec4(0.65f, 0.45f, 0.05f, 1.0f)
		: ImVec4(1.00f, 0.85f, 0.40f, 1.0f);
}

void CenterNext(ImVec2 size) {
	ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
}

// Render a single-line lobby reachability indicator — coloured dot
// plus status text.  Shared between the Browse and Host Games windows.
// Does NOT itself trigger lobby traffic; the caller is expected to
// arrange an occasional ping (see MaybePingLobby).
void LobbyStatusIndicator(uint64_t nowMs) {
	const LobbyHealth& h = GetState().lobbyHealth;

	ImVec4 dotCol(0.55f, 0.55f, 0.55f, 1.0f);  // gray: unknown
	const char *label = "Lobby: checking...";
	char buf[128];

	const bool haveOk   = (h.lastOkMs   != 0);
	const bool haveFail = (h.lastFailMs != 0);
	const bool okIsNewer = haveOk && (!haveFail || h.lastOkMs >= h.lastFailMs);

	if (okIsNewer) {
		// Green — reachable.  Show age to make it obvious the signal
		// is live (not a stale cached "OK").
		dotCol = ImVec4(0.4f, 0.85f, 0.4f, 1.0f);
		uint64_t age = (nowMs >= h.lastOkMs) ? nowMs - h.lastOkMs : 0;
		uint64_t sec = age / 1000;
		std::snprintf(buf, sizeof buf,
			"Lobby reachable  (checked %llus ago)",
			(unsigned long long)sec);
		label = buf;
	} else if (haveFail) {
		// Red — most recent result was a failure.
		dotCol = ImVec4(0.95f, 0.45f, 0.45f, 1.0f);
		const char *reason = h.lastError.empty()
			? "unreachable" : h.lastError.c_str();
		std::snprintf(buf, sizeof buf, "Lobby: %s", reason);
		label = buf;
	} else {
		label = "Connecting to the lobby...";
	}

	// Coloured bullet (•, U+2022) in the General Punctuation range —
	// guaranteed to be in the font atlas per ui_fonts.cpp's glyph-
	// range builder.  Colour carries the green/red/grey signal.
	ImGui::TextColored(dotCol, "\xE2\x80\xA2");
	ImGui::SameLine(0.0f, 6.0f);
	if (okIsNewer)
		ImGui::TextUnformatted(label);
	else if (haveFail)
		ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1.0f), "%s", label);
	else
		ImGui::TextDisabled("%s", label);
}

// Fire a single List if nothing has told us about the lobby in the
// last kStaleMs.  Respects rate-limit backoff set by the List failure
// path in ui_netplay.cpp so the free-tier Oracle lobby doesn't get
// hammered.  Called from any window that wants to render a live
// Lobby Status indicator — without this, a user who never opens the
// Browser and has no enabled offers would see a stale "checking…"
// indicator forever.
void MaybePingLobby(uint64_t nowMs) {
	State& st = GetState();
	constexpr uint64_t kStaleMs = 60000;  // 60s

	const uint64_t last = std::max(
		st.lobbyHealth.lastOkMs, st.lobbyHealth.lastFailMs);
	const bool stale = (last == 0) || (nowMs - last >= kStaleMs);
	if (!stale) return;

	const bool backoffActive =
		st.browser.nextRetryMs != 0 && nowMs < st.browser.nextRetryMs;
	if (backoffActive) return;
	if (st.browser.refreshInFlight) return;

	// Piggy-back on the Browser's refresh path — it runs a List which
	// both confirms reachability and refreshes the session list.
	st.browser.refreshRequested = true;
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

	// Lobby reachability indicator — always on top of the window.
	// The Browser already refreshes every 10s while open so the
	// indicator stays fresh without MaybePingLobby.
	LobbyStatusIndicator(SDL_GetTicks());
	ImGui::Separator();

	// Activity banner when the user is hosting / in session — the
	// session list below filters out the user's own hostedGames, so
	// without this the Browser looks empty.
	if (st.activity == UserActivity::InSession) {
		ImGui::TextColored(EmphasisWarning(),
			"You're currently playing online.  Open Host Games to "
			"see the match details or stop hosting.");
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
	// Red-ish colour if the last response was an error; otherwise dim.
	bool isError = !br.statusLine.empty()
		&& (br.statusLine.find("fail") != std::string::npos
		    || br.statusLine.find("unreachable") != std::string::npos
		    || br.statusLine.find("HTTP ") != std::string::npos);
	if (isError) {
		ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1.0f), " %s",
			br.statusLine.c_str());
	} else {
		// Prefer aggregate stats (set after first /v1/stats reply
		// completes) — gives "12 sessions • 4 in play • 7 hosts" instead
		// of just the visible-list count, which may differ from total
		// when own-offer filtering / playing-state hide rows.
		const auto& a = st.aggregateStats;
		if (a.lastUpdateMs != 0) {
			ImGui::TextDisabled(" %d sessions  ·  %d in play  ·  %d hosts",
				a.sessions, a.playing, a.hosts);
		} else {
			ImGui::TextDisabled(" %s",
				br.statusLine.empty()
				? (br.refreshInFlight ? "Refreshing..." : "")
				: br.statusLine.c_str());
		}
	}

	ImGui::Separator();

	const ImGuiTableFlags tflags =
		ImGuiTableFlags_RowBg    | ImGuiTableFlags_Borders   |
		ImGuiTableFlags_Resizable | ImGuiTableFlags_SortTristate |
		ImGuiTableFlags_ScrollY   | ImGuiTableFlags_Sortable;
	ImVec2 tableSize(0, ImGui::GetContentRegionAvail().y - 50);
	if (ImGui::BeginTable("##lobby", 5, tflags, tableSize)) {
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Game",   ImGuiTableColumnFlags_WidthStretch, 2.0f);
		ImGui::TableSetupColumn("Open",   ImGuiTableColumnFlags_WidthFixed,   90.0f);
		ImGui::TableSetupColumn("Host",   ImGuiTableColumnFlags_WidthStretch, 1.2f);
		ImGui::TableSetupColumn("Region", ImGuiTableColumnFlags_WidthFixed,   80.0f);
		ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed,   90.0f);
		ImGui::TableHeadersRow();

		const bool dark = ATUIIsDarkTheme();
		const ImVec4 cIncompat = dark ? ImVec4(1.00f, 0.55f, 0.45f, 1.0f)
		                              : ImVec4(0.80f, 0.10f, 0.10f, 1.0f);
		const ImVec4 cPlaying  = dark ? ImVec4(0.70f, 0.70f, 0.70f, 1.0f)
		                              : ImVec4(0.45f, 0.45f, 0.45f, 1.0f);
		const ImVec4 cDim      = dark ? ImVec4(0.65f, 0.65f, 0.65f, 1.0f)
		                              : ImVec4(0.40f, 0.40f, 0.40f, 1.0f);

		for (size_t i = 0; i < br.items.size(); ++i) {
			const auto& s = br.items[i];

			char missingCRC[16] = {};
			JoinCompat compat = CheckJoinCompat(s.kernelCRC32, s.basicCRC32,
				missingCRC);
			const bool playing = (s.state == "playing");
			const bool joinable = !playing && compat != JoinCompat::MissingKernel
			                              && compat != JoinCompat::MissingBasic;

			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::PushID((int)i);
			bool selected = ((int)i == br.selectedIdx);
			ImGuiSelectableFlags sf = ImGuiSelectableFlags_SpanAllColumns |
			                          ImGuiSelectableFlags_AllowDoubleClick;
			// Game cell: cart name on top, sub-line below with OS/BASIC/HW
			// and a colour reflecting compat.
			ImVec4 nameCol = playing  ? cPlaying
			               : !joinable ? cIncompat
			               : ImGui::GetStyleColorVec4(ImGuiCol_Text);
			ImGui::PushStyleColor(ImGuiCol_Text, nameCol);
			if (ImGui::Selectable(s.cartName.c_str(), selected, sf)) {
				br.selectedIdx = (int)i;
				if (ImGui::IsMouseDoubleClicked(0) && joinable) {
					st.session.joinTarget = s;
					Navigate(s.requiresCode ? Screen::JoinPrompt
					                        : Screen::JoinConfirm);
				}
			}
			ImGui::PopStyleColor();
			// Tooltip belongs on the Selectable (full-row hover area
			// thanks to SpanAllColumns).  Must be set before any
			// subsequent item is submitted, otherwise IsItemHovered
			// would refer to that later item instead.
			if (ImGui::IsItemHovered()) {
				if (compat == JoinCompat::MissingKernel)
					ImGui::SetTooltip("Missing OS firmware [%s] — install it "
						"in System → Firmware to join.", missingCRC);
				else if (compat == JoinCompat::MissingBasic)
					ImGui::SetTooltip("Missing BASIC firmware [%s] — install "
						"it in System → Firmware to join.", missingCRC);
				else if (playing)
					ImGui::SetTooltip("This session is in play — wait for it "
						"to end or pick another.");
			}

			// Sub-row: "hardware | video | memory | OS | BASIC".
			// Tokens whose firmware the joiner is missing are painted
			// red; everything else stays dim.  Replaces the old raw-hex
			// "OS [crc] · BASIC [crc] · 130XE" line.
			RenderSpecLine(BuildSpecLineFromSession(s, compat), cDim);

			// Open (Public/Private)
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(s.requiresCode ? "Private" : "Public");

			// Host
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(s.hostHandle.empty()
				? "Anonymous" : s.hostHandle.c_str());

			// Region
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(s.region.c_str());

			// Action — per-row Join (matches double-click behaviour).
			ImGui::TableNextColumn();
			const bool rowJoinable = joinable && !ATNetplayGlue::IsActive();
			ImGui::BeginDisabled(!rowJoinable);
			if (ImGui::SmallButton(playing ? "in play" : "Join")) {
				br.selectedIdx = (int)i;
				st.session.joinTarget = s;
				Navigate(s.requiresCode ? Screen::JoinPrompt
				                        : Screen::JoinConfirm);
			}
			ImGui::EndDisabled();
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	ImGui::Separator();
	const ImVec4 cIncompat = ATUIIsDarkTheme()
		? ImVec4(1.00f, 0.55f, 0.45f, 1.0f)
		: ImVec4(0.80f, 0.10f, 0.10f, 1.0f);
	// Determine selected-row joinability up front so the button text can
	// explain *why* it's disabled.
	const ATNetplay::LobbySession *sel = (br.selectedIdx >= 0 &&
	                           br.selectedIdx < (int)br.items.size())
		? &br.items[br.selectedIdx] : nullptr;
	JoinCompat selCompat = JoinCompat::Unknown;
	if (sel) selCompat = CheckJoinCompat(sel->kernelCRC32, sel->basicCRC32);
	const bool selPlaying = sel && sel->state == "playing";
	const bool selOk = sel && !selPlaying
		&& selCompat != JoinCompat::MissingKernel
		&& selCompat != JoinCompat::MissingBasic;
	bool canJoin = sel && selOk && !ATNetplayGlue::IsActive();
	ImGui::BeginDisabled(!canJoin);
	if (ImGui::Button("Join", ImVec2(140, 0))) {
		st.session.joinTarget = *sel;
		Navigate(st.session.joinTarget.requiresCode
			? Screen::JoinPrompt : Screen::JoinConfirm);
	}
	ImGui::EndDisabled();
	if (sel && !canJoin && !ATNetplayGlue::IsActive()) {
		ImGui::SameLine();
		if (selPlaying)
			ImGui::TextDisabled("(in play)");
		else if (selCompat == JoinCompat::MissingKernel)
			ImGui::TextColored(cIncompat, "(missing OS firmware)");
		else if (selCompat == JoinCompat::MissingBasic)
			ImGui::TextColored(cIncompat, "(missing BASIC firmware)");
	}
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
		ImGui::TextColored(EmphasisGood(), "%s", bootedBase.c_str());
	} else {
		ImGui::TextColored(EmphasisBad(), "(no game booted)");
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
		"The host will send their game file and hardware settings. "
		"Both sides cold-boot from that so the session starts "
		"identically. Your current game will be replaced.");

	ImGui::Separator();
	if (ImGui::Button("Join", ImVec2(120, 0))) StartJoiningAction();
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0))) Back();

	if (!open) Back();
	ImGui::End();
}

// "<Peer> wants to join <Game>" — Allow / Deny dialog raised by
// ReconcileHostedGames whenever a queued join is awaiting a decision.
// One row per queued joiner; each has its own 20 s auto-decline and
// its own "Requested Xs ago" counter.  Dismissing the dialog rejects
// every queued entry — a peer should never be left hanging.
void DesktopAcceptJoinPrompt() {
	State& st = GetState();
	if (st.session.pendingRequests.empty()) {
		Navigate(Screen::MyHostedGames);
		return;
	}

	const uint64_t nowMs = (uint64_t)SDL_GetTicks();
	constexpr uint64_t kAutoDeclineMs = 20000;

	// Collect auto-decline rejects to fire after the render pass; the
	// coord queue always shifts down so index 0 for that offer is the
	// correct target for successive auto-rejects of the same offer.
	std::vector<std::string> autoRejectOffers;
	for (const auto& r : st.session.pendingRequests) {
		const uint64_t elapsed = nowMs > r.arrivedMs ? nowMs - r.arrivedMs : 0;
		if (elapsed >= kAutoDeclineMs) autoRejectOffers.push_back(r.hostedGameId);
	}

	CenterNext(ImVec2(520, 380));
	bool open = true;
	if (!ImGui::Begin("Join requests##netplay", &open,
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
		ImGui::End();
		if (!open) {
			for (const auto& r : st.session.pendingRequests) {
				ATNetplayGlue::HostRejectPending(r.hostedGameId.c_str(), 0);
			}
		}
		return;
	}

	// Per-offer indices so each row's Allow/Deny hits the right slot
	// in that offer's coord queue.
	std::vector<size_t> perOfferIdx(st.session.pendingRequests.size(), 0);
	{
		std::unordered_map<std::string, size_t> seen;
		for (size_t i = 0; i < st.session.pendingRequests.size(); ++i) {
			const auto& r = st.session.pendingRequests[i];
			perOfferIdx[i] = seen[r.hostedGameId]++;
		}
	}

	enum class ActionKind { None, Accept, Reject, RejectAll };
	ActionKind action = ActionKind::None;
	std::string actionGameId;
	size_t actionIdx = 0;

	for (size_t i = 0; i < st.session.pendingRequests.size(); ++i) {
		const auto& r = st.session.pendingRequests[i];
		const uint64_t elapsed = nowMs > r.arrivedMs ? nowMs - r.arrivedMs : 0;
		const uint64_t elapsedS = elapsed / 1000;
		const uint64_t remainMs = elapsed < kAutoDeclineMs
			? kAutoDeclineMs - elapsed : 0;
		const uint64_t remainS  = (remainMs + 999) / 1000;

		ImGui::PushID((int)i);
		const char *handle = r.handle.empty() ? "Someone" : r.handle.c_str();
		ImGui::TextWrapped("%s wants to join your game:", handle);
		ImGui::TextColored(EmphasisGood(), "  %s", r.gameName.c_str());
		ImGui::TextDisabled("Requested %llus ago · auto-decline in %llus",
			(unsigned long long)elapsedS,
			(unsigned long long)remainS);
		if (ImGui::Button("Allow", ImVec2(140, 0))) {
			action = ActionKind::Accept;
			actionGameId = r.hostedGameId;
			actionIdx = perOfferIdx[i];
		}
		ImGui::SameLine();
		if (ImGui::Button("Deny", ImVec2(140, 0))) {
			action = ActionKind::Reject;
			actionGameId = r.hostedGameId;
			actionIdx = perOfferIdx[i];
		}
		ImGui::Separator();
		ImGui::PopID();
	}

	if (st.session.pendingRequests.size() > 1) {
		if (ImGui::Button("Deny all", ImVec2(-FLT_MIN, 0))) {
			action = ActionKind::RejectAll;
		}
	}

	ImGui::Spacing();
	ImGui::TextDisabled(
		"Accepting replaces your current emulator game with the "
		"hosted one for the online session.  Your game is saved "
		"automatically and restored when the session ends.");

	if (!open) action = ActionKind::RejectAll;
	ImGui::End();

	// Apply deferred actions.
	for (const auto& gid : autoRejectOffers) {
		ATNetplayGlue::HostRejectPending(gid.c_str(), 0);
	}
	switch (action) {
		case ActionKind::Accept:
			ATNetplayGlue::HostAcceptPending(actionGameId.c_str(), actionIdx);
			st.session.promptSavedValid = false;
			if (st.session.promptPausedSim && g_sim.IsPaused()) {
				g_sim.Resume();
			}
			st.session.promptPausedSim = false;
			break;
		case ActionKind::Reject:
			ATNetplayGlue::HostRejectPending(actionGameId.c_str(), actionIdx);
			break;
		case ActionKind::RejectAll:
			for (const auto& r : st.session.pendingRequests) {
				ATNetplayGlue::HostRejectPending(r.hostedGameId.c_str(), 0);
			}
			break;
		case ActionKind::None:
			break;
	}
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

	// If the joiner-side coordinator has reached a terminal phase,
	// surface why instead of sitting on "Connecting…" forever.
	const ATNetplayGlue::Phase jp = ATNetplayGlue::JoinPhase();
	const bool joinFailed = (jp == ATNetplayGlue::Phase::Failed ||
	                         jp == ATNetplayGlue::Phase::Ended  ||
	                         jp == ATNetplayGlue::Phase::Desynced);

	const char *jerr = ATNetplayGlue::JoinLastError();
	const bool declined = joinFailed && jerr &&
		std::strstr(jerr, "declined") != nullptr;
	const bool hostBusy = joinFailed && jerr &&
		std::strstr(jerr, "chose another") != nullptr;

	CenterNext(ImVec2(440, 240));
	bool open = true;
	const char *title =
		  declined ? "Join request declined##netplay"
		: hostBusy ? "Another player was chosen##netplay"
		: joinFailed ? "Could not join##netplay"
		: "Joining session##netplay";
	if (!ImGui::Begin(title, &open,
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
		ImGui::End();
		if (!open) {
			if (joinFailed) ATNetplayGlue::StopJoin();
			Navigate(Screen::Closed);
		}
		return;
	}

	if (joinFailed) {
		const std::string msg = (jerr && *jerr) ? jerr : "Connection failed.";
		ImVec4 col = ATUIIsDarkTheme()
			? ImVec4(1.0f, 0.55f, 0.45f, 1.0f)
			: ImVec4(0.80f, 0.10f, 0.10f, 1.0f);
		const char *headline =
			  declined ? "The host declined your request to join."
			: hostBusy ? "The host picked a different player for this game."
			: "Could not join the game.";
		ImGui::TextColored(col, "%s", headline);
		ImGui::Spacing();
		ImGui::TextWrapped("%s", msg.c_str());
		if (!st.session.joinTarget.hostHandle.empty() ||
		    !st.session.joinTarget.cartName.empty()) {
			ImGui::Spacing();
			ImGui::TextDisabled("Host: %s   Game: %s",
				st.session.joinTarget.hostHandle.c_str(),
				st.session.joinTarget.cartName.c_str());
		}
		ImGui::Spacing();
		ImGui::Separator();
		if (ImGui::Button("Try Again", ImVec2(120, 0))) {
			ATNetplayGlue::StopJoin();
			StartJoiningAction();
		}
		ImGui::SameLine();
		if (ImGui::Button("Back to Browse", ImVec2(150, 0))) {
			ATNetplayGlue::StopJoin();
			Navigate(Screen::Browser);
		}
		ImGui::SameLine();
		if (ImGui::Button("Close", ImVec2(120, 0))) {
			ATNetplayGlue::StopJoin();
			Navigate(Screen::Closed);
		}
		if (!open) {
			ATNetplayGlue::StopJoin();
			Navigate(Screen::Closed);
		}
		ImGui::End();
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
	if (!ImGui::Begin("Online Play Preferences##netplay", &open,
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
		ImGui::End();
		if (!open) Back();
		return;
	}

	static char nameBuf[32] = {};
	static bool seeded = false;
	if (!seeded) {
		std::snprintf(nameBuf, sizeof nameBuf, "%s",
			st.prefs.nickname.c_str());
		seeded = true;
	}

	if (ImGui::BeginTabBar("##netplay_prefs_tabs")) {
		if (ImGui::BeginTabItem("General")) {
			ImGui::Spacing();
			ImGui::PushItemWidth(260);
			if (ImGui::InputText("Nickname", nameBuf, sizeof nameBuf))
				st.prefs.nickname = nameBuf;
			ImGui::PopItemWidth();
			ImGui::Checkbox("Anonymous (random nickname each session)",
				&st.prefs.isAnonymous);

			ImGui::Spacing();
			ImGui::SeparatorText("In-session HUD");
			ImGui::Checkbox("Show in-session HUD",
				&st.prefs.showSessionHUD);
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Hosting")) {
			ImGui::Spacing();
			ImGui::SeparatorText("When someone joins");
			ImGui::TextDisabled("  A modal asks you Allow / Deny.  Auto-declines after 20 s.");

			ImGui::Spacing();
			ImGui::SeparatorText("Notifications on incoming join");
			ImGui::Checkbox("Flash window",        &st.prefs.notif.flashWindow);
			ImGui::Checkbox("System notification", &st.prefs.notif.systemNotify);
			ImGui::Checkbox("Chime",               &st.prefs.notif.playChime);
			// "Steal focus on attention" toggle intentionally removed —
			// the pref is a dead field (no runtime code reads it).

			ImGui::Spacing();
			ImGui::SeparatorText("Default input delay");
			ImGui::TextDisabled("  Buffer for the joiner's input — bigger absorbs more jitter, costs feel.");
			ImGui::SliderInt("LAN (frames)",      &st.prefs.defaultInputDelayLan, 1, 10);
			ImGui::SliderInt("Internet (frames)", &st.prefs.defaultInputDelayInternet, 1, 10);

			ImGui::Spacing();
			ImGui::SeparatorText("Display");
			ImGui::Checkbox("Show game art in Online Play",
				&st.prefs.showBrowserArt);
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

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
		case HostedGameState::Off:         return "Off";
		case HostedGameState::Open:        return "Open";
		case HostedGameState::Handshaking: return "Connecting";
		case HostedGameState::Playing:     return "Playing";
		case HostedGameState::Paused:      return "Paused";
		case HostedGameState::Failed:      return "Failed";
	}
	return "?";
}
ImVec4 OfferStateColour(HostedGameState s) {
	const bool dark = ATUIIsDarkTheme();
	switch (s) {
		case HostedGameState::Off:
			return dark ? ImVec4(0.70f, 0.70f, 0.70f, 1)
			            : ImVec4(0.45f, 0.45f, 0.45f, 1);
		case HostedGameState::Open:
			return dark ? ImVec4(0.55f, 0.85f, 0.55f, 1)
			            : ImVec4(0.10f, 0.55f, 0.15f, 1);
		case HostedGameState::Handshaking:
			return dark ? ImVec4(0.95f, 0.85f, 0.40f, 1)
			            : ImVec4(0.70f, 0.50f, 0.05f, 1);
		case HostedGameState::Playing:
			return dark ? ImVec4(0.40f, 0.85f, 1.00f, 1)
			            : ImVec4(0.10f, 0.40f, 0.70f, 1);
		case HostedGameState::Paused:
			return dark ? ImVec4(0.85f, 0.70f, 0.40f, 1)
			            : ImVec4(0.60f, 0.40f, 0.10f, 1);
		case HostedGameState::Failed:
			return dark ? ImVec4(1.00f, 0.55f, 0.45f, 1)
			            : ImVec4(0.80f, 0.10f, 0.10f, 1);
	}
	return dark ? ImVec4(1, 1, 1, 1) : ImVec4(0, 0, 0, 1);
}

const char *ActivityBannerText(UserActivity a,
                               size_t gameCount, size_t enabledCount) {
	switch (a) {
		case UserActivity::Idle:
			if (gameCount == 0)   return "Add a game to start hosting.";
			if (enabledCount == 0) return "All your games are off — toggle "
				"Host to open one on the lobby.";
			if (enabledCount == 1) return "1 game open on the lobby.";
			return "Open on the lobby — peers can see and join.";
		case UserActivity::PlayingLocal:
			return "You're playing single-player — hosted games are paused "
				"until you stop.";
		case UserActivity::InSession:
			return "An online match is in progress — your other hosted "
				"games are paused while you play.";
	}
	return "";
}

namespace {
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

} // anonymous

// FirmwareNameForCRC moved to ui_netplay_state.cpp so Gaming Mode
// can reuse it without dragging in the desktop firmware-dropdown
// cache.

void DesktopMyHostedGames() {
	State& st = GetState();
	const uint64_t nowMs = (uint64_t)SDL_GetTicks();
	if (!s_firmwareChoicesLoaded) ReloadFirmwareChoices();
	CenterNext(ImVec2(820, 520));
	bool open = true;
	if (!ImGui::Begin("Online Play — Host Games##netplay", &open,
		ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		if (!open) Navigate(Screen::Closed);
		return;
	}

	// Lobby reachability indicator — always on top.  Host Games may
	// be open without the Browser ever having been visited, so fire
	// an occasional lightweight ping to keep the signal live.  Ping
	// respects backoff from lobby List failures.
	LobbyStatusIndicator(nowMs);
	MaybePingLobby(nowMs);
	ImGui::Separator();

	// Activity banner.  Colour depends on whether anything is actually
	// listable: green only when at least one game is enabled and idle.
	size_t enabledCount = 0;
	for (const auto& o : st.hostedGames) if (o.enabled) ++enabledCount;
	const bool listed = (st.activity == UserActivity::Idle && enabledCount > 0);
	ImVec4 bc = listed
		? ImVec4(0.55f, 0.85f, 0.55f, 1)
		: ImVec4(0.85f, 0.85f, 0.85f, 1);
	if (st.activity != UserActivity::Idle)
		bc = ImVec4(0.95f, 0.75f, 0.4f, 1);
	ImGui::TextColored(bc, "%s",
		ActivityBannerText(st.activity, st.hostedGames.size(), enabledCount));

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
		// Enabled first — user's primary control.
		ImGui::TableSetupColumn("On",      ImGuiTableColumnFlags_WidthFixed,   36.0f);
		ImGui::TableSetupColumn("Game",    ImGuiTableColumnFlags_WidthStretch, 2.5f);
		ImGui::TableSetupColumn("Open",    ImGuiTableColumnFlags_WidthFixed,   90.0f);
		ImGui::TableSetupColumn("State",   ImGuiTableColumnFlags_WidthFixed,  110.0f);
		ImGui::TableSetupColumn("Port",    ImGuiTableColumnFlags_WidthFixed,   70.0f);
		ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed,  130.0f);
		ImGui::TableHeadersRow();

		// Collect pending actions to apply AFTER the loop (can't mutate
		// state.hostedGames mid-iteration).
		std::string pendingRemove;
		std::string pendingToggle;  // non-empty: flip enabled of this id

		for (auto& o : st.hostedGames) {
			ImGui::PushID(o.id.c_str());
			ImGui::TableNextRow();

			ImGui::TableNextColumn();
			bool en = o.enabled;
			if (ImGui::Checkbox("##en", &en)) {
				pendingToggle = o.id;
			}

			ImGui::TableNextColumn();
			ImGui::TextUnformatted(o.gameName.c_str());
			const ImVec4 cDim = ImVec4(0.65f, 0.65f, 0.70f, 1.0f);
			// Same spec line shape the joiner sees in Browse Hosted
			// Games, so the two views align visually.  Host-side
			// tokens are never "missing" (we own this firmware).
			RenderSpecLine(BuildSpecLineFromConfig(o.config), cDim);
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

	// Master "Host all" checkbox — tri-state: checked if every game is
	// enabled, unchecked if none, dim when mixed.  Click flips them all
	// to the opposite of the majority state.
	if (!st.hostedGames.empty()) {
		int enCount = 0;
		for (const auto& o : st.hostedGames) if (o.enabled) ++enCount;
		const bool allEnabled = (enCount == (int)st.hostedGames.size());
		const bool anyEnabled = (enCount > 0);
		bool toggle = allEnabled;
		if (anyEnabled && !allEnabled)
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
				ImGui::GetStyle().Alpha * 0.6f);
		if (ImGui::Checkbox("Host all", &toggle)) {
			const bool target = !allEnabled;
			for (auto& o : st.hostedGames) {
				if (o.enabled != target) {
					target ? EnableHostedGame(o.id)
					       : DisableHostedGame(o.id);
				}
			}
		}
		if (anyEnabled && !allEnabled)
			ImGui::PopStyleVar();
		ImGui::SameLine();
	}
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
		VDStringW wp = VDTextU8ToW(filelist[0], -1);
		if (wp.empty()) return;

		const wchar_t *last = nullptr;
		for (const wchar_t *q = wp.c_str(); *q; ++q)
			if (*q == L'/' || *q == L'\\') last = q;
		VDStringW base = last ? VDStringW(last + 1) : wp;
		VDStringA pu = VDTextWToU8(wp);
		VDStringA bu = VDTextWToU8(base);

		OfferDraft& d = GetState().offerDraft;
		d.path.assign(pu.c_str(), pu.size());
		d.displayName.assign(bu.c_str(), bu.size());
		d.source = OfferSource::File;
		d.variantLabel.clear();
		d.libraryEntryIdx = -1;
		d.libraryVariantIdx = -1;

		// Keep legacy locals in sync so the rest of the Desktop file
		// keeps working while we migrate call sites.
		s_pickedPath = wp;
		s_pickedPathPending = true;
	}

	// HostedGameSignature moved to ui_netplay_state.cpp.

	// Lazy singleton to avoid pulling Game Library init/shutdown into
	// this TU.  Reuses the user's configured library if available.
	void CommitNewOffer(const std::string& path, const std::string& name) {
		State& st = GetState();
		if (st.hostedGames.size() >= State::kMaxHostedGames) {
			st.session.lastError = "Too many games hosted — remove one first.";
			Navigate(Screen::Error);
			return;
		}

		// Reject duplicates — same image path + same machine config.
		const std::string sig = HostedGameSignature(path, s_addConfig);
		for (const auto& existing : st.hostedGames) {
			if (HostedGameSignature(existing.gamePath, existing.config) == sig) {
				st.session.lastError =
					"This game is already added to hosting with this "
					"configuration — change the machine config or remove "
					"the existing entry first.";
				Navigate(Screen::Error);
				return;
			}
		}

		HostedGame o;
		o.id           = GenerateHostedGameId();
		o.gamePath     = path;
		o.gameName     = name;
		o.isPrivate    = s_addPrivate;
		o.entryCode    = s_addEntryCode;
		o.config       = s_addConfig;
		o.enabled      = true;
		o.state        = HostedGameState::Off;
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
		st.offerDraft = OfferDraft();

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

	State& st = GetState();
	const OfferDraft& draft = st.offerDraft;
	const std::string& stagedPath = draft.path;
	const std::string& stagedName = draft.displayName;

	// Reserve room for the pinned footer (separator + button row).  The
	// scrollable body fills the remaining space so long machine-config
	// content scrolls internally and the footer stays visible regardless
	// of window height.
	const float footerH = ImGui::GetFrameHeightWithSpacing() + 12.0f;
	ImGui::BeginChild("##addGameBody",
		ImVec2(0, -footerH), false, ImGuiWindowFlags_NoSavedSettings);

	// ── Game (source + selection) ─────────────────────────────
	// One linear form: read-only "Selected / Source" summary plus two
	// browse buttons.  The previous tabbed Library / File picker has
	// been replaced by a full-screen Library Picker sheet (shared with
	// Gaming Mode) that launches via the "Library…" button.
	ImGui::TextUnformatted("Game:");
	ImGui::SameLine();
	if (stagedName.empty()) {
		ImGui::TextDisabled("(none selected)");
	} else {
		ImGui::TextUnformatted(stagedName.c_str());
	}
	ImGui::SameLine();
	if (ImGui::Button("Library…##addoffer", ImVec2(120, 0))) {
		Navigate(Screen::LibraryPicker);
	}
	ImGui::SameLine();
	if (ImGui::Button("File…##addoffer", ImVec2(90, 0))) {
		ATUIShowOpenFileDialog('npad', AddOfferFileCallback,
			nullptr, g_pWindow,
			kAddOfferFilters,
			(int)(sizeof kAddOfferFilters / sizeof kAddOfferFilters[0]),
			false);
	}

	if (!stagedName.empty()) {
		VDStringA srcLine;
		if (draft.source == OfferSource::Library) {
			srcLine = "Source: Game Library";
			if (!draft.variantLabel.empty()) {
				srcLine += " · variant \"";
				srcLine += draft.variantLabel.c_str();
				srcLine += '"';
			}
		} else {
			srcLine = "Source: File · ";
			srcLine += draft.path.c_str();
		}
		ImGui::TextDisabled("%s", srcLine.c_str());
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

	ImGui::EndChild();

	// Pinned footer — always visible at the window's bottom edge.
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
	if (!ImGui::Begin("Online Play Error##netplay", &open,
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

// Defined in ui_netplay_screens.cpp — shared library-picker sheet
// that services both Gaming Mode (grid) and Desktop (table).
void RenderLibraryPicker();

// Called from ui_netplay.cpp when the caller is in Desktop mode.
bool DesktopDispatch() {
	State& st = GetState();
	switch (st.screen) {
		case Screen::Closed:       return false;
		case Screen::Nickname:     DesktopNickname();    break;
		case Screen::OnlinePlayHub:
			// The hub screen is Gaming-Mode-only.  If we land here
			// in Desktop (mode switched mid-flight, or the worker
			// nav stack recovers into Hub) pop to Host Games — the
			// desktop menu bar already exposes Browse/Preferences
			// separately so a hub would be redundant.
			Navigate(Screen::MyHostedGames);
			DesktopMyHostedGames();
			break;
		case Screen::Browser:      DesktopBrowser();     break;
		case Screen::MyHostedGames:DesktopMyHostedGames(); break;
		case Screen::AddGame:     DesktopAddOffer();    break;
		case Screen::LibraryPicker: RenderLibraryPicker(); break;
		case Screen::HostSetup:    DesktopHostSetup();   break;
		case Screen::JoinPrompt:   DesktopJoinPrompt();  break;
		case Screen::JoinConfirm:  DesktopJoinConfirm(); break;
		case Screen::Waiting:      DesktopWaiting();     break;
		case Screen::AcceptJoinPrompt: DesktopAcceptJoinPrompt(); break;
		case Screen::Prefs:        DesktopPrefs();       break;
		case Screen::Error:        DesktopError();       break;
		default: break;
	}
	return true;
}

} // namespace ATNetplayUI
