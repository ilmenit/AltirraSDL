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
#include "ui_netplay_deeplink.h"

#include "input/touch_widgets.h"
#include "ui/core/ui_mode.h"
#include "ui/emotes/emote_netplay.h"
#include "settings.h"

#include "netplay/netplay_glue.h"
#include "netplay/lobby_config.h"
#include "netplay/platform_notify.h"

#include "ui/gamelibrary/game_library.h"
#include "ui/gamelibrary/game_library_art.h"
#include "ui/mobile/mobile_internal.h"  // GetGameArtCache, GameBrowser_OpenPicker
#include "ui/mobile/ui_mobile.h"        // ATMobileUI_SwitchToGameBrowser
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
#include <unordered_map>
#include <utility>
#include <vector>

#include "simulator.h"
extern ATSimulator g_sim;

namespace ATNetplayUI {

// Defined in ui_netplay.cpp.  Module-level (external linkage) so
// this TU and that TU link the same symbol.
LobbyWorker& GetWorker();

namespace {

// Format a millisecond duration as a short human-readable span
// ("42s", "5m", "3h", "2d").  Used by My Hosted Games to render
// "N/M players · up 5m" without leaking a full HH:MM:SS into the list.
std::string FormatShortDuration(uint64_t ms) {
	uint64_t s = ms / 1000;
	char buf[24];
	if (s < 60)              std::snprintf(buf, sizeof buf, "%us",  (unsigned)s);
	else if (s < 3600)       std::snprintf(buf, sizeof buf, "%um",  (unsigned)(s / 60));
	else if (s < 86400)      std::snprintf(buf, sizeof buf, "%uh",  (unsigned)(s / 3600));
	else                     std::snprintf(buf, sizeof buf, "%ud",  (unsigned)(s / 86400));
	return std::string(buf);
}

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

	BeginScreenBody(ATTouch::kFooterReserveSingle);

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
	ATTouchInputTextScrollAware("##handle", buf, sizeof buf);
	ImGui::PopItemWidth();

	ImGui::Spacing();
	bool anon = st.prefs.isAnonymous;
	if (ATTouchToggle("Anonymous (random nickname each session)", &anon)) {
		st.prefs.isAnonymous = anon;
	}

	EndScreenBody();

	ImGui::Separator();
	ImGui::Spacing();

	float btnW = (ImGui::GetContentRegionAvail().x - Dp(10)) * 0.5f;
	if (ATTouchButton("Cancel", ImVec2(btnW, Dp(ATTouch::kButtonHeightNormal)),
	                  ATTouchButtonStyle::Neutral)) {
		Navigate(Screen::Closed);
	}
	ImGui::SameLine(0, Dp(10));
	bool valid = anon || (buf[0] != 0);
	ImGui::BeginDisabled(!valid);
	if (ATTouchButton("Save", ImVec2(btnW, Dp(ATTouch::kButtonHeightNormal)),
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
// Deep-link one-click join preflight
//
// Driven entirely by the deep-link state machine in
// ui_netplay_deeplink.cpp; this renderer just picks one of four tiny
// sub-views based on GetDeepLinkUiState() and forwards user actions
// to SubmitDeepLinkNickname / RetryDeepLinkFirmware / CancelDeepLink.
// Once the state machine fires StartJoiningAction the standard
// JoinConfirm / Waiting screens push themselves over the top of this.
// -------------------------------------------------------------------

// Backing storage for the nickname input.  Defined here so both the
// input field (in the NeedsNickname case) and the footer's Continue
// button (which checks emptiness and reads the final value) share
// the same buffer.  Buffer size is 32 (lobby's kHostHandleMax).
namespace {
	char *Wiz_DeepLinkNickBuf() {
		static char buf[32] = {};
		return buf;
	}
}

void RenderDeepLinkPrep() {
	bool open = true;
	if (!BeginSheet("Joining online game", &open,
	                ImVec2(Dp(360), Dp(280)),
	                ImVec2(Dp(560), Dp(420)))) {
		return;
	}

	const DeepLinkUiState ui = GetDeepLinkUiState();

	// Track the prior phase so the NeedsNickname case can re-seed
	// its input on re-entry (e.g. after Cancel followed by a fresh
	// ?s=... navigation in the same browser tab).  Updated at the
	// top so the comparison is stable regardless of which sub-view
	// the prior frame rendered.
	static DeepLinkUiState s_prevUi = DeepLinkUiState::NotPending;
	const bool nicknamePhaseEntered = (ui == DeepLinkUiState::NeedsNickname
	                                && s_prevUi != DeepLinkUiState::NeedsNickname);
	s_prevUi = ui;

	// Title bar — Back / Cancel both abort the deep-link.  We don't
	// push DeepLinkPrep onto the back stack on advance, so a tap on
	// the system-back gesture also lands here as a single pop.
	if (ScreenHeader("Joining online game")) {
		CancelDeepLink();
	}

	BeginScreenBody(ATTouch::kFooterReserveSingle);

	switch (ui) {
		case DeepLinkUiState::NeedsNickname: {
			ATTouchSection("What name should the host see?");
			ATTouchMutedText(
				"This is the only thing they'll see before deciding "
				"to let you in.  1-24 characters.");
			ImGui::Spacing();

			char *buf = Wiz_DeepLinkNickBuf();
			State& st = GetState();
			if (nicknamePhaseEntered || ConsumeFocusRequest(2001)) {
				// Seed with whatever's already saved (re-entry case); a
				// truly fresh visitor shows an empty box.
				std::snprintf(buf, 32, "%s",
					st.prefs.nickname.c_str());
				ImGui::SetKeyboardFocusHere();
			}
			ImGui::PushItemWidth(-FLT_MIN);
			ATTouchInputTextScrollAware("##dlhandle", buf, 32);
			ImGui::PopItemWidth();
			break;
		}

		case DeepLinkUiState::DownloadingFw: {
			ATTouchSection("Setting up the emulator");
			ATTouchMutedText(
				"Downloading the Atari ROMs the host's game needs "
				"(about 26 KB — one-time per browser).");
			ImGui::Spacing();
			// Indeterminate ImGui progress bar — there's no per-mirror
			// byte progress exposed from the WASM bridge, and the fetch
			// is fast enough (sub-second on a healthy mirror) that an
			// animated bar reads better than a stalled "0%".
			ImGui::ProgressBar(-1.0f * (float)ImGui::GetTime(),
				ImVec2(-FLT_MIN, Dp(8)), "");
			break;
		}

		case DeepLinkUiState::FirmwareFailed: {
			ATTouchSection("Couldn't download the Atari ROMs");
			ATTouchMutedText(
				"All firmware mirrors failed.  Check your connection "
				"and try again, or close this dialog and run the "
				"manual setup wizard from the menu.");
			break;
		}

		case DeepLinkUiState::Looking: {
			ATTouchSection("Looking up the game");
			ATTouchMutedText(
				"Asking the lobby for the session details.  This "
				"usually takes a second or two.");
			ImGui::Spacing();
			ImGui::ProgressBar(-1.0f * (float)ImGui::GetTime(),
				ImVec2(-FLT_MIN, Dp(8)), "");
			break;
		}

		case DeepLinkUiState::Joining: {
			ATTouchSection("Joining session");
			ATTouchMutedText(
				"Waiting for the host to accept your request to join.");
			ImGui::Spacing();
			ImGui::ProgressBar(-1.0f * (float)ImGui::GetTime(),
				ImVec2(-FLT_MIN, Dp(8)), "");
			break;
		}

		case DeepLinkUiState::NotPending:
			// Drifted here without an active deep-link; pop back so
			// the user isn't stuck on an empty modal.
			Navigate(Screen::Closed);
			EndScreenBody();
			EndSheet();
			return;
	}

	EndScreenBody();

	ImGui::Separator();
	ImGui::Spacing();

	// Footer — content varies per phase.  Cancel is always present
	// so the user can never get trapped in a multi-second fetch.
	if (ui == DeepLinkUiState::NeedsNickname) {
		float btnW = (ImGui::GetContentRegionAvail().x - Dp(10)) * 0.5f;
		if (ATTouchButton("Cancel",
		                  ImVec2(btnW, Dp(ATTouch::kButtonHeightNormal)),
		                  ATTouchButtonStyle::Neutral)) {
			CancelDeepLink();
		}
		ImGui::SameLine(0, Dp(10));

		// Re-fetch the buffer the input lives in.  Static, so the
		// final value the user typed is what we read here even
		// though the input field rendered earlier this frame.
		const char *cur = Wiz_DeepLinkNickBuf();
		const bool valid = cur && cur[0] != 0;

		ImGui::BeginDisabled(!valid);
		if (ATTouchButton("Continue",
		                  ImVec2(btnW, Dp(ATTouch::kButtonHeightNormal)),
		                  ATTouchButtonStyle::Accent)) {
			SubmitDeepLinkNickname(std::string(cur));
		}
		ImGui::EndDisabled();
	}
	else if (ui == DeepLinkUiState::FirmwareFailed) {
		float btnW = (ImGui::GetContentRegionAvail().x - Dp(10)) * 0.5f;
		if (ATTouchButton("Cancel",
		                  ImVec2(btnW, Dp(ATTouch::kButtonHeightNormal)),
		                  ATTouchButtonStyle::Neutral)) {
			CancelDeepLink();
		}
		ImGui::SameLine(0, Dp(10));
		if (ATTouchButton("Try again",
		                  ImVec2(btnW, Dp(ATTouch::kButtonHeightNormal)),
		                  ATTouchButtonStyle::Accent)) {
			RetryDeepLinkFirmware();
		}
	}
	else {
		// DownloadingFw / Looking / Joining: only Cancel.
		if (ATTouchButton("Cancel",
		                  ImVec2(-FLT_MIN, Dp(ATTouch::kButtonHeightNormal)),
		                  ATTouchButtonStyle::Neutral)) {
			CancelDeepLink();
		}
	}

	if (!open) CancelDeepLink();

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

	// Scrollable body — Refresh / status / banner / grid all live
	// inside so the ScreenHeader stays pinned to the top of the sheet.
	BeginScreenBody(/*reserveBottomDp*/ 0.0f);

	// One visible action in the sub-header: Refresh.  Host / Prefs
	// live on the Online Play hub, which is one back-tap away.
	const ATMobilePalette &p = ATMobileGetPalette();
	if (ConsumeFocusRequest(4001))
		ImGui::SetKeyboardFocusHere();
	if (ATTouchButton("Refresh",
	                  ImVec2(Dp(140), Dp(ATTouch::kButtonHeightNormal)),
	                  ATTouchButtonStyle::Neutral)) {
		// Preserve scroll position across the refresh so new data
		// landing doesn't yank the list back to the top.
		br.savedScrollY = ImGui::GetScrollY();
		br.restoreScrollPending = true;
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

	// Lobby ping indicator — renders inline after the status text.
	// Helps users gauge whether relay would be acceptable from their
	// current network: a relayed session inherits at least the lobby
	// RTT, so 200 ms means at minimum 200 ms peer-to-peer.
	//
	// Thresholds: <=80 ms green / 81-150 amber / 151-300 amber-warn /
	// >300 red / no sample yet gray.  Pip + colour map to the same
	// StatusColorGood/Warn/Bad palette used in the in-session HUD so
	// the visual language is consistent across screens.
	{
		ImGui::SameLine(0, Dp(16));
		ImGui::AlignTextToFramePadding();
		const uint32_t lat = br.lobbyLatencyMs;
		const int      n   = br.lobbyLatencySampleCount;
		ImVec4 pipCol;
		const char* label = nullptr;
		char buf[48];
		if (n == 0) {
			pipCol = ImVec4(0.55f, 0.58f, 0.65f, 1.0f);
			label = "Lobby: pinging…";
		} else if (lat <= 80) {
			pipCol = ImVec4(0.40f, 0.92f, 0.45f, 1.0f);
			std::snprintf(buf, sizeof buf, "Lobby: %u ms", (unsigned)lat);
			label = buf;
		} else if (lat <= 150) {
			pipCol = ImVec4(0.95f, 0.85f, 0.40f, 1.0f);
			std::snprintf(buf, sizeof buf, "Lobby: %u ms", (unsigned)lat);
			label = buf;
		} else if (lat <= 300) {
			pipCol = ImVec4(1.00f, 0.78f, 0.30f, 1.0f);
			std::snprintf(buf, sizeof buf, "Lobby: %u ms (relay slow)",
				(unsigned)lat);
			label = buf;
		} else {
			pipCol = ImVec4(1.00f, 0.42f, 0.42f, 1.0f);
			std::snprintf(buf, sizeof buf, "Lobby: %u ms (relay poor)",
				(unsigned)lat);
			label = buf;
		}
		ImGui::PushStyleColor(ImGuiCol_Text, pipCol);
		ImGui::TextUnformatted("\xe2\x97\x8f");  // U+25CF BLACK CIRCLE
		ImGui::PopStyleColor();
		ImGui::SameLine(0, Dp(4));
		ImGui::PushStyleColor(ImGuiCol_Text, ATMobileCol(p.textMuted));
		ImGui::TextUnformatted(label);
		ImGui::PopStyleColor();
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip(
				"Round-trip time to the lobby server.\n"
				"Relay sessions go through the same host, so this is "
				"the floor of relayed peer-to-peer latency.");
		}
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	// Reachability banner — mirrors Host Games so joiners see the same
	// honest green/red state.  Retry offered because the grid below is
	// empty when the lobby is unreachable.
	LobbyStatusBanner(/*allowRetry*/ true);

	// ── Search + alphabet filter row (Gaming-Mode only) ───────────
	//
	// Mirrors the Library Picker pattern: a free-text search
	// (case-insensitive contains on handle + cart name) plus an
	// A-Z row of letter pills that restricts to cart names starting
	// with that letter.  Active in Gaming Mode only; the desktop
	// table already supports column sort + mouse navigation so a
	// second filter bar would be redundant clutter.
	if (ATUIIsGamingMode()) {
		float rowW = ImGui::GetContentRegionAvail().x;
		float searchW = std::min(rowW * 0.4f, Dp(280.0f));
		ImGui::SetNextItemWidth(searchW);
		if (ATTouchInputTextWithHintScrollAware("##browsersearch", "Search…",
			br.searchBuf, sizeof br.searchBuf))
		{
			br.letterFilter = -1;
		}
		ImGui::SameLine();
		if (ATTouchButton("Clear##browser",
			ImVec2(Dp(70), Dp(28)), ATTouchButtonStyle::Subtle))
		{
			br.searchBuf[0] = 0;
			br.letterFilter = -1;
		}

		// A-Z pills.
		const float letW = Dp(28);
		const float letH = Dp(28);
		bool allActive = (br.letterFilter < 0);
		ATTouchButtonStyle allStyle = allActive
			? ATTouchButtonStyle::Accent : ATTouchButtonStyle::Neutral;
		if (ATTouchButton("All##browserL",
			ImVec2(Dp(40), letH), allStyle))
		{
			br.letterFilter = -1;
		}
		for (int L = 0; L < 26; ++L) {
			ImGui::SameLine(0, Dp(2));
			char buf[12];
			std::snprintf(buf, sizeof buf, "%c##browserL%d",
				(char)('A' + L), L);
			bool active = (br.letterFilter == L);
			ATTouchButtonStyle st2 = active
				? ATTouchButtonStyle::Accent
				: ATTouchButtonStyle::Neutral;
			if (ATTouchButton(buf, ImVec2(letW, letH), st2))
				br.letterFilter = L;
			(void)letW;
		}
		ImGui::Spacing();
	}

	// Lower the search string once for the per-item contains check.
	char brSearchLower[sizeof br.searchBuf];
	std::snprintf(brSearchLower, sizeof brSearchLower, "%s", br.searchBuf);
	for (char *c = brSearchLower; *c; ++c)
		if (*c >= 'A' && *c <= 'Z') *c = (char)(*c + 32);
	const bool hasSearch = brSearchLower[0] != 0;
	const int  letter    = br.letterFilter;

	auto matchesFilter = [&](const ATNetplay::LobbySession& s) -> bool {
		if (hasSearch) {
			auto containsCI = [](const std::string& hay,
				const char *needleLower) -> bool
			{
				std::string h; h.reserve(hay.size());
				for (char c : hay) {
					h.push_back((c >= 'A' && c <= 'Z')
						? (char)(c + 32) : c);
				}
				return h.find(needleLower) != std::string::npos;
			};
			if (!containsCI(s.hostHandle, brSearchLower)
			    && !containsCI(s.cartName, brSearchLower))
				return false;
		}
		if (letter >= 0 && letter < 26) {
			const std::string& n = s.cartName;
			char firstLower = 0;
			for (char c : n) {
				if (c == ' ') continue;
				firstLower = (c >= 'A' && c <= 'Z')
					? (char)(c + 32) : c;
				break;
			}
			if (firstLower != (char)('a' + letter))
				return false;
		}
		return true;
	};

	// Count filtered items so we can show an accurate empty state.
	size_t filteredCount = 0;
	for (const auto& s : br.items) if (matchesFilter(s)) ++filteredCount;

	// Restore scroll position captured before the last refresh so new
	// data landing doesn't jump the list back to the top.
	if (br.restoreScrollPending) {
		ImGui::SetScrollY(br.savedScrollY);
		br.restoreScrollPending = false;
	}

	if (br.items.empty() || filteredCount == 0) {
		if (br.refreshInFlight) {
			ATTouchMutedText("Loading sessions…");
		} else if (br.items.empty()) {
			ATTouchEmptyState(
				"No sessions right now",
				"There are no public games hosted on the lobby at "
				"the moment.  Tap Refresh to check again, or host "
				"your own session from the Online Play hub.",
				nullptr, nullptr);
		} else {
			ATTouchEmptyState(
				"No sessions match your filter",
				"Clear the search box or letter filter to see every "
				"hosted game again.",
				nullptr, nullptr);
		}
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
			if (!matchesFilter(s)) continue;
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
			// Populate the machine spec row + missing-firmware flag so
			// the tile renders "130XE | PAL | 320K | OS Rev. 2 |
			// Basic Rev. C" with the correct token painted red when
			// the joiner can't satisfy a firmware CRC.
			JoinCompat compat = CheckJoinCompat(
				s.kernelCRC32, s.basicCRC32);
			SpecLine sl = BuildSpecLineFromSession(s, compat);
			ti.specTokens  = sl.tokens;
			ti.specMissing = sl.hasMissingFirmware;

			if (SessionTile(ti, tileSize)) {
				br.selectedIdx = (int)i;
				// Block Join on missing firmware.  The tile still
				// registers the click (users get the hover feedback
				// + selection) but the navigation is suppressed and
				// a toast explains why.
				if (ti.specMissing) {
					PushToast("Install the required firmware in "
						"System \xE2\x86\x92 Firmware to join.",
						ToastSeverity::Warning, 3500);
				} else if (s.state == "playing") {
					PushToast("This session is in play — wait for it "
						"to end or pick another.",
						ToastSeverity::Info, 3000);
				} else {
					st.session.joinTarget = s;
					Navigate(s.requiresCode ? Screen::JoinPrompt
					                        : Screen::JoinConfirm);
				}
			}
		}
		EndScreenGrid();
	}

	EndScreenBody();

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

	BeginScreenBody(ATTouch::kFooterReserveSingle);

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
		ATTouchInputTextScrollAware("##code", code, sizeof code);
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

	EndScreenBody();

	ImGui::Separator();
	ImGui::Spacing();

	float btnW = (ImGui::GetContentRegionAvail().x - Dp(10)) * 0.5f;
	if (ATTouchButton("Cancel", ImVec2(btnW, Dp(ATTouch::kButtonHeightNormal)),
	                  ATTouchButtonStyle::Neutral)) {
		Back();
	}
	ImGui::SameLine(0, Dp(10));
	bool canHost = !st.session.pendingCartName.empty()
	               && (!st.session.hostingPrivate
	                   || !st.session.hostingEntryCode.empty());
	ImGui::BeginDisabled(!canHost);
	if (ATTouchButton("Start Hosting", ImVec2(btnW, Dp(ATTouch::kButtonHeightNormal)),
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

	BeginScreenBody(ATTouch::kFooterReserveSingle);

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
	ATTouchInputTextScrollAware("##code", code, sizeof code);
	ImGui::PopItemWidth();
	st.session.joinEntryCode = code;

	EndScreenBody();

	ImGui::Separator();
	ImGui::Spacing();
	float btnW = (ImGui::GetContentRegionAvail().x - Dp(10)) * 0.5f;
	if (ATTouchButton("Cancel", ImVec2(btnW, Dp(ATTouch::kButtonHeightNormal)),
	                  ATTouchButtonStyle::Neutral)) {
		Back();
	}
	ImGui::SameLine(0, Dp(10));
	ImGui::BeginDisabled(code[0] == 0);
	if (ATTouchButton("Join", ImVec2(btnW, Dp(ATTouch::kButtonHeightNormal)),
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

	BeginScreenBody(ATTouch::kFooterReserveSingle);

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

	// Host configuration view.  The joiner inherits the host's
	// hardware/firmware/memory mode from the snapshot, so showing a
	// "Your emulator vs Host" diff would be misleading — the joiner's
	// own settings don't take part in the running session.  Instead
	// show the host's configuration as a single column and only
	// flag rows that the joiner physically can't satisfy: kernel /
	// BASIC firmware ROMs that are not installed locally.  The game
	// image itself is always streamed by the host so it never needs
	// to be flagged here.
	ImGui::Spacing();
	ATTouchSection("Host configuration");
	JoinCompat jcompat = CheckJoinCompat(
		st.session.joinTarget.kernelCRC32,
		st.session.joinTarget.basicCRC32);
	SpecLine remoteSpec = BuildSpecLineFromSession(
		st.session.joinTarget, jcompat);
	SpecLineRenderHostOnly(remoteSpec);

	// Surface a one-line firmware advisory so users know what to
	// install before the join can proceed.  The Browser screen
	// already gates the row's clickability on this, but the user can
	// still arrive here from a deep link / private code path so we
	// repeat the warning where the action is taken.
	if (jcompat == JoinCompat::MissingKernel
	 || jcompat == JoinCompat::MissingBasic
	 || jcompat == JoinCompat::MissingBoth)
	{
		ImGui::Spacing();
		const ATMobilePalette &p = ATMobileGetPalette();
		ImGui::PushStyleColor(ImGuiCol_Text, ATMobileCol(p.warning));
		ImGui::TextWrapped(
			"You are missing one or more of the firmware ROMs "
			"this host requires.  Install them in System "
			"Configuration → Firmware before joining.");
		ImGui::PopStyleColor();
	}

	// wssRelayOnly hosts run in a browser tab — they have no UDP
	// path, so the entire session is forced through the lobby's WS
	// bridge.  Latency is slightly higher and bandwidth is shared
	// against the lobby's per-pair token bucket (240 pps).  Surface
	// it explicitly so users understand why the connection mode
	// pip stays "via lobby" for the whole session.
	if (st.session.joinTarget.wssRelayOnly) {
		ImGui::Spacing();
		ATTouchMutedText(
			"This host is running in a browser tab.  All traffic is "
			"relayed through the lobby (no direct connection); expect "
			"slightly higher latency than a native UDP host.");
	}

	EndScreenBody();

	ImGui::Separator();
	ImGui::Spacing();
	float btnW = (ImGui::GetContentRegionAvail().x - Dp(10)) * 0.5f;
	if (ATTouchButton("Cancel", ImVec2(btnW, Dp(ATTouch::kButtonHeightNormal)),
	                  ATTouchButtonStyle::Neutral)) {
		Back();
	}
	ImGui::SameLine(0, Dp(10));
	// Block Join when the joiner is missing required firmware.  The
	// Browser screen already gates the Browser tile click on this, but
	// JoinConfirm can also be reached from the private-code path
	// (JoinPrompt → JoinConfirm) which doesn't re-check, and from
	// pop-back navigation after a transient compat change.  Without
	// this guard the user would hit Join, the worker would dispatch
	// the request, and the failure surfaces only after several
	// seconds of "Joining…" with no useful diagnostic.
	const bool firmwareMissing =
		jcompat == JoinCompat::MissingKernel
	 || jcompat == JoinCompat::MissingBasic
	 || jcompat == JoinCompat::MissingBoth;
	if (firmwareMissing) ImGui::BeginDisabled();
	if (ATTouchButton("Join", ImVec2(btnW, Dp(ATTouch::kButtonHeightNormal)),
	                  ATTouchButtonStyle::Accent)) {
		StartJoiningAction();
	}
	if (firmwareMissing) ImGui::EndDisabled();

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

	// Rejection by a prompt-me host is a distinct, user-initiated
	// outcome — surface it with its own header so the joiner doesn't
	// read it as a network failure.
	const char *jerr = ATNetplayGlue::JoinLastError();
	const bool declined = joinFailed && jerr &&
		std::strstr(jerr, "declined") != nullptr;
	const bool hostBusy = joinFailed && jerr &&
		std::strstr(jerr, "chose another") != nullptr;

	const char *headerTitle =
		  declined ? "Join request declined"
		: hostBusy ? "Another player was chosen"
		: joinFailed ? "Could not join"
		: "Joining session";
	if (ScreenHeader(headerTitle)) {
		// Back cancels host/join and returns to the previous screen.
		// Stop coordinators first so the worker thread doesn't keep
		// retrying in the background.
		ATNetplayGlue::DisconnectActive();
		Back();
	}

	// Waiting screen: footer may grow to a 3-button row on terminal-fail
	// (Change Code / Try Again / Cancel) so reserve double-height.
	BeginScreenBody(ATTouch::kFooterReserveDouble);

	// The label depends on whether we're hosting or joining + the
	// coordinator's phase.  While handshaking we say so explicitly —
	// the generic "Waiting for peer…" was misleading for joiners whose
	// wrong entry code triggered a silent reject retry in the background.
	const char *label = "Waiting for host to respond…";
	int severity = 0;
	bool spin = true;
	if (joinFailed) {
		label = (jerr && *jerr) ? jerr : "Connection failed.";
		severity = 2;
		spin = false;
	} else {
		switch (jp) {
			case ATNetplayGlue::Phase::Handshaking:
				label = st.session.joinTarget.requiresCode
					? "Verifying join code with host…"
					: "Asking the host to let you in…";
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

	// Feedback so a joiner whose host is slow to Accept doesn't stare
	// at a static "Contacting host…" — show the elapsed wait.
	if (!joinFailed && st.session.joinStartedMs != 0) {
		const uint64_t nowMs = (uint64_t)SDL_GetTicks();
		const uint64_t wait = nowMs > st.session.joinStartedMs
			? (nowMs - st.session.joinStartedMs) / 1000 : 0;
		char waitText[40];
		std::snprintf(waitText, sizeof waitText,
			"(%llus waiting)", (unsigned long long)wait);
		ATTouchMutedText(waitText);
	}

	// Connection-mode hint.  Once direct punch has been declared lost
	// and the joiner has engaged relay (PeerPath::Relay), surface
	// that explicitly so the user understands the slight latency
	// bump they'll see in-session.  Phrased as informational, not
	// a failure — relay IS the working connection.
	if (!joinFailed
	    && ATNetplayGlue::JoinerPeerPath() == ATNetplayGlue::PeerPath::Relay) {
		ImGui::Spacing();
		const ATMobilePalette &p = ATMobileGetPalette();
		ImGui::PushStyleColor(ImGuiCol_Text,
			ImVec4(1.00f, 0.78f, 0.30f, 1.0f));   // amber pip
		ImGui::TextUnformatted("\xe2\x97\x8f");
		ImGui::PopStyleColor();
		ImGui::SameLine(0, Dp(6));
		ImGui::PushStyleColor(ImGuiCol_Text, ATMobileCol(p.textMuted));
		ImGui::TextUnformatted(
			"Direct connection unavailable — using relay.");
		ImGui::PopStyleColor();
	}

	ImGui::Spacing();
	PeerChip(st.session.joinTarget.hostHandle.c_str(),
	         st.session.joinTarget.region.c_str(),
	         st.session.joinTarget.requiresCode);
	ATTouchMutedText(st.session.joinTarget.cartName.c_str());

	EndScreenBody();

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
		if (ATTouchButton("Cancel", ImVec2(bw, Dp(ATTouch::kButtonHeightNormal)),
		                  ATTouchButtonStyle::Neutral)) {
			ATNetplayGlue::DisconnectActive();
			// Pop back to Browser (it's already on the stack from the
			// natural Hub → Browser → JoinPrompt → Waiting chain).
			// Calling Navigate(Browser) here would push the failed
			// Waiting onto the stack, so a subsequent Back from
			// Browser would resurface this dead screen.
			PopTo(Screen::Browser);
		}
		ImGui::SameLine(0, Dp(10));
		if (ATTouchButton("Change Code", ImVec2(bw, Dp(ATTouch::kButtonHeightNormal)),
		                  ATTouchButtonStyle::Accent)) {
			ATNetplayGlue::DisconnectActive();
			// JoinPrompt is on the stack for private sessions (the
			// only path that reaches the bad-code branch).  Pop to
			// it so re-entering the code with a fresh attempt
			// preserves the Browser-below-JoinPrompt chain.
			PopTo(Screen::JoinPrompt);
		}
		ImGui::SameLine(0, Dp(10));
		if (ATTouchButton("Try Again", ImVec2(bw, Dp(ATTouch::kButtonHeightNormal)),
		                  ATTouchButtonStyle::Neutral)) {
			ATNetplayGlue::DisconnectActive();
			StartJoiningAction();
		}
	} else if (joinFailed) {
		// Non-code join failures (declined, host-full, other rejects):
		// offer explicit "Back to Browse" + "Try Again" instead of the
		// ambiguous hosting-oriented Cancel/Minimise pair.
		float bw = (ImGui::GetContentRegionAvail().x - Dp(10)) * 0.5f;
		if (ATTouchButton("Back to Browse", ImVec2(bw, Dp(ATTouch::kButtonHeightNormal)),
		                  ATTouchButtonStyle::Neutral)) {
			ATNetplayGlue::DisconnectActive();
			// PopTo, not Navigate — see Cancel above for rationale.
			PopTo(Screen::Browser);
		}
		ImGui::SameLine(0, Dp(10));
		if (ATTouchButton("Try Again", ImVec2(bw, Dp(ATTouch::kButtonHeightNormal)),
		                  ATTouchButtonStyle::Accent)) {
			ATNetplayGlue::DisconnectActive();
			StartJoiningAction();
		}
	} else {
		float btnW = (ImGui::GetContentRegionAvail().x - Dp(10)) * 0.5f;
		if (ATTouchButton("Cancel", ImVec2(btnW, Dp(ATTouch::kButtonHeightNormal)),
		                  ATTouchButtonStyle::Danger)) {
			// Joiner-only Waiting screen (the whole RenderWaiting
			// reads JoinPhase / joinTarget), so this is always a
			// joiner cancel.  StopJoin tears down THIS attempt;
			// don't fall through to StopHostingAction, which would
			// also disable every game the user has queued for
			// hosting — a surprising side effect for a user who
			// just changed their mind about joining.
			ATNetplayGlue::StopJoin();
			// Mid-join cancel: pop back to wherever the user came
			// from (Browser for the normal chain, JoinPrompt for
			// private sessions if they want to try again with a
			// different code).  PopTo(Browser) handles both — for
			// the private chain Browser is also on the stack.
			PopTo(Screen::Browser);
		}
		ImGui::SameLine(0, Dp(10));
		if (ATTouchButton("Minimise", ImVec2(btnW, Dp(ATTouch::kButtonHeightNormal)),
		                  ATTouchButtonStyle::Neutral)) {
			// Hide the sheet — coordinator keeps running in the background.
			Navigate(Screen::Closed);
		}
	}

	if (!open) Navigate(Screen::Closed);
	EndSheet();
}

} // anonymous namespace — close so RenderOnlinePlayPrefsBody has
  // external linkage and matches the header declaration.

// -------------------------------------------------------------------
// Preferences
// -------------------------------------------------------------------

void RenderOnlinePlayPrefsBody() {
	State& st = GetState();

	ATTouchSection("Nickname");
	{
		static char nameBuf[32] = {};
		if (ConsumeFocusRequest(5001)) {
			std::snprintf(nameBuf, sizeof nameBuf, "%s",
				st.prefs.nickname.c_str());
		}
		ImGui::PushItemWidth(-FLT_MIN);
		if (ATTouchInputTextScrollAware("##handle", nameBuf, sizeof nameBuf)) {
			st.prefs.nickname = nameBuf;
		}
		ImGui::PopItemWidth();
	}
	{
		bool anon = st.prefs.isAnonymous;
		if (ATTouchToggle("Anonymous (random per session)", &anon)) {
			st.prefs.isAnonymous = anon;
		}
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

	// Communication icons — same two toggles as Configure System >
	// Emulator > Online Play on Desktop.  Kept in sync through the
	// shared ATEmoteNetplay Get/Set accessors (registry-backed via the
	// Environment settings callback).
	ImGui::Spacing();
	ATTouchSection("Communication Icons");
	{
		bool sendEmotes = ATEmoteNetplay::GetSendEnabled();
		if (ATTouchToggle("Send icons to the other player", &sendEmotes)) {
			ATEmoteNetplay::SetSendEnabled(sendEmotes);
			ATSaveSettings(kATSettingsCategory_Environment);
		}
		bool recvEmotes = ATEmoteNetplay::GetReceiveEnabled();
		if (ATTouchToggle("Receive icons from the other player", &recvEmotes)) {
			ATEmoteNetplay::SetReceiveEnabled(recvEmotes);
			ATSaveSettings(kATSettingsCategory_Environment);
		}
	}
}

void RenderPrefs() {
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
	BeginScreenBody(/*reserveBottomDp*/ 0.0f);
	// Focus-on-open: steer keyboard/gamepad focus to the first control
	// so users don't have to click before tabbing through options.
	if (ConsumeFocusRequest(4002))
		ImGui::SetKeyboardFocusHere();
	RenderOnlinePlayPrefsBody();
	EndScreenBody();

	if (!open) { SaveToRegistry(); Back(); }
	EndSheet();
}

namespace { // reopen anonymous namespace for the rest of the screen
            // renderers that were already in it.

// -------------------------------------------------------------------
// My Hosted Games (Gaming Mode) — touch-card list.
// -------------------------------------------------------------------

const char *OfferStateLabelMobile(HostedGameState s) {
	switch (s) {
		case HostedGameState::Off:         return "Off";
		case HostedGameState::Open:        return "Open";
		case HostedGameState::Handshaking: return "Connecting…";
		case HostedGameState::Playing:     return "Playing";
		case HostedGameState::Paused:      return "Paused";
		case HostedGameState::Failed:      return "Failed";
	}
	return "?";
}

int OfferStateSeverity(HostedGameState s) {
	switch (s) {
		case HostedGameState::Playing:     return 3;
		case HostedGameState::Open:        return 3;
		case HostedGameState::Handshaking: return 1;
		case HostedGameState::Paused:      return 1;
		case HostedGameState::Failed:      return 2;
		default:                      return 0;
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

	// Pin the "Host all" toggle + separator at the bottom of the sheet
	// so the user always has a one-tap master control in reach, then
	// wrap everything else in a scrollable body that shares the same
	// swipe / gamepad / keyboard-nav plumbing as the Settings screen.
	const bool hasHostAllFooter = !GetState().hostedGames.empty();
	BeginScreenBody(hasHostAllFooter ? ATTouch::kFooterReserveSingle : 0.0f);

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

	// Counter — show enabled / saved-list / max-enabled.  Users can
	// curate a long library and rotate which 5 are advertised at a
	// time.  The lobby enforces the same enabled cap server-side
	// (kMaxHostedGamesPerHost in lobby_protocol.h).
	{
		size_t enabledCount = 0;
		for (const auto& g : st.hostedGames) if (g.enabled) ++enabledCount;
		char counter[96];
		std::snprintf(counter, sizeof counter,
			"Hosted games: %zu (%zu/%zu enabled)",
			st.hostedGames.size(),
			enabledCount,
			(size_t)State::kMaxEnabledHostedGames);
		ATTouchMutedText(counter);
	}

	ImGui::BeginDisabled(atCap);
	if (ConsumeFocusRequest(4004))
		ImGui::SetKeyboardFocusHere();
	if (ATTouchButton("+ Add Game",
	                  ImVec2(btnW, Dp(ATTouch::kButtonHeightNormal)),
	                  ATTouchButtonStyle::Accent)) {
		st.editingGameId.clear();
		Navigate(Screen::AddGame);
	}
	ImGui::EndDisabled();
	if (atCap && ImGui::IsItemHovered()) {
		ImGui::SetTooltip(
			"Saved hosted-games list is full — remove some unused "
			"entries before adding new ones.");
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	if (st.hostedGames.empty()) {
		ATTouchEmptyState(
			"No hosted games yet",
			"Tap + Add Game above to pick one from your library or "
			"from a file, then Host it to let other players join.",
			nullptr, nullptr);
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

			// Subtitle: the shared spec line — "hardware | video |
			// memory | OS | BASIC" — plus Public/Private.  Matches
			// the joiner's Browse Hosted Games row so the two views
			// read identically.  Host-own tokens are never "missing"
			// (we own the firmware we picked).
			const std::string specStr =
				SpecLineJoin(BuildSpecLineFromConfig(o.config));
			char subtitle[320];
			// When the coordinator has reported liveness append a
			// "N/M players · up 5m" fragment; otherwise fall back to
			// the bare spec + visibility pair.
			if (o.maxPlayers > 0 && o.hostStartedAtMs != 0) {
				const uint64_t nowMs = (uint64_t)SDL_GetTicks();
				const uint64_t ageMs = nowMs >= o.hostStartedAtMs
					? nowMs - o.hostStartedAtMs : 0;
				std::snprintf(subtitle, sizeof subtitle,
					"%s \xC2\xB7 %s \xC2\xB7 %u/%u players \xC2\xB7 up %s",
					specStr.c_str(),
					o.isPrivate ? "Private" : "Public",
					(unsigned)o.currentPlayers,
					(unsigned)o.maxPlayers,
					FormatShortDuration(ageMs).c_str());
			} else {
				std::snprintf(subtitle, sizeof subtitle,
					"%s \xC2\xB7 %s",
					specStr.c_str(),
					o.isPrivate ? "Private" : "Public");
			}
			bool tapped = ATTouchListItem(o.gameName.c_str(), subtitle,
				false, false);
			(void)tapped;  // tile tap is currently a no-op; actions below

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
			const char *enLabel = o.enabled ? "Stop" : "Host";
			if (ATTouchButton(enLabel,
			                  ImVec2(Dp(130), Dp(ATTouch::kButtonHeightSmall)),
			                  o.enabled ? ATTouchButtonStyle::Neutral
			                            : ATTouchButtonStyle::Accent)) {
				pendingToggle = o.id;
			}
			ImGui::SameLine();
			if (ATTouchButton("Remove",
			                  ImVec2(Dp(120), Dp(ATTouch::kButtonHeightSmall)),
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

	EndScreenBody();

	// Host-all tri-state master — pinned under the scrollable list
	// so it stays visible no matter how many games have been added.
	// NavFlattened on the body child keeps arrow-key / gamepad nav
	// flowing between the last list row and this toggle.
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

// Gaming-Mode Add-Offer form state.  The two-button form (Library /
// File) stores the active selection in `State::offerDraft` so Desktop
// and Gaming Mode share a single draft through the same struct.
// Privacy + machine-config fields stay local to Gaming Mode.

bool      s_mobileAddPrivate = false;
char      s_mobileAddCode[32] = {};
MachineConfig s_mobileAddConfig;
bool          s_mobileAddConfigSeeded = false;

void MobileAddOfferFileCallback(void*, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	VDStringW p = VDTextU8ToW(filelist[0], -1);
	if (p.empty()) return;
	const wchar_t *last = nullptr;
	for (const wchar_t *q = p.c_str(); *q; ++q)
		if (*q == L'/' || *q == L'\\') last = q;
	VDStringW base = last ? VDStringW(last + 1) : p;
	VDStringA pu = VDTextWToU8(p);
	VDStringA bu = VDTextWToU8(base);

	OfferDraft& d = GetState().offerDraft;
	d.path.assign(pu.c_str(), pu.size());
	d.displayName.assign(bu.c_str(), bu.size());
	d.source = OfferSource::File;
	d.variantLabel.clear();
	d.libraryEntryIdx = -1;
	d.libraryVariantIdx = -1;
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
	if (!s_mobileAddConfigSeeded) {
		// Remember the last-used config across Add-Game opens so a
		// serial host doesn't have to re-pick hardware every time.
		// Prefer the first existing hosted game's config; fall back to
		// `prefs.lastAddConfig`; final fallback is the live sim.  The
		// user can always click "Copy from current emulator" inside
		// RenderMachineConfigSection to re-snapshot.
		if (!st.hostedGames.empty()) {
			s_mobileAddConfig = st.hostedGames.front().config;
		} else {
			s_mobileAddConfig = st.prefs.lastAddConfig;
			// If prefs.lastAddConfig is default-constructed (first ever
			// Add-Game), synchronise with the live emulator so joiner
			// firmware CRCs line up out of the box.
			if (s_mobileAddConfig.kernelCRC32 == 0
			    && s_mobileAddConfig.basicCRC32 == 0)
			{
				s_mobileAddConfig = CaptureCurrentMachineConfig();
			}
			// Promote any remaining (Altirra default) / (None)
			// placeholders to the user's INSTALLED default ROMs, and
			// force BASIC enabled = false (per-user-default of
			// "BASIC disabled, real ROM staged for later toggle").
			PrefillHostMachineConfigDefaults(s_mobileAddConfig);
		}
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

	// Pin the Cancel / Add to Hosted action bar to the bottom of the
	// sheet so it stays visible as the user scrolls through Game,
	// Machine configuration, and Privacy sections.  Height matches a
	// Separator + Spacing + Dp(ATTouch::kButtonHeightNormal) button row with a trailing Spacing.
	BeginScreenBody(ATTouch::kFooterReserveSingle);

	// ── GAME (source + selection) ──────────────────────────────
	// A single linear form: two Browse buttons + a read-only
	// Selected/Source summary.  No tabs — both buttons open their
	// respective pickers as full-screen sheets (Library Picker for
	// Library; the SDL native file dialog for File).
	const OfferDraft& draft = st.offerDraft;
	ATTouchSection("Game");
	if (draft.source == OfferSource::None || draft.displayName.empty()) {
		ATTouchMutedText("No game selected. "
		                 "Pick one from your Library, or from a file.");
	} else {
		ImGui::TextUnformatted("Selected:");
		ImGui::SameLine();
		ImGui::TextUnformatted(draft.displayName.c_str());

		VDStringA srcLine;
		if (draft.source == OfferSource::Library) {
			srcLine = "Game Library";
			if (!draft.variantLabel.empty()) {
				srcLine += " · variant \"";
				srcLine += draft.variantLabel.c_str();
				srcLine += '"';
			}
		} else {
			srcLine = "File · ";
			srcLine += draft.path.c_str();
		}
		ATTouchMutedText(srcLine.c_str());
	}

	ImGui::Spacing();
	{
		float bW = (ImGui::GetContentRegionAvail().x - Dp(10)) * 0.5f;
		if (ConsumeFocusRequest(4003))
			ImGui::SetKeyboardFocusHere();
		if (ATTouchButton("Pick from Library…",
		                  ImVec2(bW, Dp(ATTouch::kButtonHeightNormal)),
		                  ATTouchButtonStyle::Neutral)) {
			if (ATUIIsGamingMode()) {
				// Gaming Mode — delegate the full Game Library UI
				// (grid/list toggle, letter pill, search, cover art)
				// to the main mobile Game Browser by flipping it into
				// picker mode.  Zero UI duplication between the "boot
				// a game" and "pick a game to host" flows — one
				// renderer, one code path, one set of visuals.
				GameBrowser_OpenPicker(
					[](const VDStringW& path, int entryIdx,
					   int variantIdx, const VDStringW& displayName,
					   const VDStringW& variantLabel)
					{
						VDStringA pU8 = VDTextWToU8(path);
						VDStringA nU8 = VDTextWToU8(displayName);
						VDStringA lU8 = VDTextWToU8(variantLabel);
						OfferDraft& d = GetState().offerDraft;
						d.path.assign(pU8.c_str(), pU8.size());
						d.displayName.assign(nU8.c_str(), nU8.size());
						d.source = OfferSource::Library;
						d.variantLabel.assign(lU8.c_str(), lU8.size());
						d.libraryEntryIdx = entryIdx;
						d.libraryVariantIdx = variantIdx;
						// Hand control back to the netplay AddGame
						// sheet — its BeginSheet backdrop will cover
						// the Game Browser surface on the next frame.
					},
					/*onCancel*/ nullptr,
					"Pick a game to host");
				ATMobileUI_SwitchToGameBrowser();
			} else {
				// Desktop — use the dedicated table-based modal.
				Navigate(Screen::LibraryPicker);
			}
		}
		ImGui::SameLine(0, Dp(10));
		if (ATTouchButton("Pick a File…",
		                  ImVec2(bW, Dp(ATTouch::kButtonHeightNormal)),
		                  ATTouchButtonStyle::Neutral)) {
			ATUIShowOpenFileDialog('npam', MobileAddOfferFileCallback,
				nullptr, g_pWindow,
				kMobileAddOfferFilters,
				(int)(sizeof kMobileAddOfferFilters /
				      sizeof kMobileAddOfferFilters[0]),
				false);
		}
	}

	ImGui::Spacing();

	// ── MACHINE CONFIGURATION ───────────────────────────────────
	// Applied only during a session; never touches the user's saved
	// Altirra configuration.  Shared with the Desktop Add-Game dialog
	// so both modes expose an identical set of knobs (hardware, video,
	// BASIC, SIO, firmware CRCs) from one implementation.
	ATTouchSection("Machine configuration");
	RenderMachineConfigSection(s_mobileAddConfig);

	ImGui::Spacing();
	// ── PRIVACY ─────────────────────────────────────────────────
	ATTouchSection("Privacy");
	ATTouchToggle("Private (require entry code)", &s_mobileAddPrivate);
	if (s_mobileAddPrivate) {
		ImGui::PushItemWidth(Dp(240));
		ATTouchInputTextScrollAware("##code", s_mobileAddCode, sizeof s_mobileAddCode);
		ImGui::PopItemWidth();
	}

	EndScreenBody();

	// ── Action bar — pinned to the bottom of the sheet ─────────
	ImGui::Separator();
	ImGui::Spacing();

	const std::string& stagedPath = draft.path;
	const std::string& stagedName = draft.displayName;

	bool ready = !stagedPath.empty() && !stagedName.empty()
		&& (!s_mobileAddPrivate || s_mobileAddCode[0] != 0);
	float bW = (ImGui::GetContentRegionAvail().x - Dp(10)) * 0.5f;
	if (ATTouchButton("Cancel", ImVec2(bW, Dp(ATTouch::kButtonHeightNormal)),
	                  ATTouchButtonStyle::Neutral)) {
		Back();
	}
	ImGui::SameLine(0, Dp(10));
	ImGui::BeginDisabled(!ready);
	if (ATTouchButton("Add to Hosted", ImVec2(bW, Dp(ATTouch::kButtonHeightNormal)),
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
			// Start disabled; EnableHostedGame applies the
			// kMaxEnabledHostedGames cap and only flips it live
			// when there's a free slot.
			o.enabled   = false;
			s.hostedGames.push_back(std::move(o));
			s.prefs.lastAddConfig = s_mobileAddConfig;
			SaveToRegistry();
			EnableHostedGame(s.hostedGames.back().id);
			// Reset pick state for next Add Game.
			s.offerDraft = OfferDraft();
			s_mobileAddPrivate = false;
			s_mobileAddCode[0] = 0;
			// MachineConfig preset intentionally retained so next Add
			// Game opens with the same choice.
		}
		// PopTo, not Navigate — MyHostedGames is on the stack from
		// the natural Hub → MyHostedGames → AddGame chain.  Using
		// Navigate here would push AddGame onto the stack and a
		// subsequent Back from MyHostedGames would re-open AddGame
		// with stale draft state.
		PopTo(Screen::MyHostedGames);
	}
	ImGui::EndDisabled();

	if (!open) Back();
	EndSheet();
}

// -------------------------------------------------------------------
// Accept-join prompt — host's "Allow / Deny" card, one row per queued
// joiner.  Auto-declines each entry 20 s after *its* arrival so an AFK
// host doesn't block any joiner indefinitely.  Back / close / "Deny
// all" rejects every queued entry so no one is left hanging.
//
// The same function runs in Desktop (normal ImGui window) and Gaming
// Mode (touch sheet) — Desktop's old version lived in ui_netplay_desktop
// and is now a thin pass-through to this one so both modes share the
// queue-aware logic.
// -------------------------------------------------------------------
void RenderAcceptJoinPrompt() {
	State& st = GetState();
	if (st.session.pendingRequests.empty()) {
		// Reconcile tears the screen down on the 0-entry edge; if we
		// got rendered for one frame before that, fall back gracefully.
		Back();
		return;
	}

	const uint64_t nowMs = (uint64_t)SDL_GetTicks();
	constexpr uint64_t kAutoDeclineMs = 20000;

	// Per-row auto-decline: walk the queue, fire RejectPending for
	// any entry whose elapsed time passed the threshold.  We only
	// collect indices here and act after the render so the vector
	// indices stay stable for the duration of this frame.
	std::vector<std::pair<std::string, size_t>> autoRejects;
	for (size_t i = 0; i < st.session.pendingRequests.size(); ++i) {
		const auto& r = st.session.pendingRequests[i];
		const uint64_t elapsed = nowMs > r.arrivedMs ? nowMs - r.arrivedMs : 0;
		if (elapsed >= kAutoDeclineMs) {
			// Find this entry's index *within its offer's coord queue*
			// for the reject call below.  Reconcile rebuilds the UI
			// vector in glue-order, so this i matches the coord index.
			autoRejects.emplace_back(r.hostedGameId, 0);
			// The coord queue is a FIFO with silent dedup; every time
			// we reject at index 0 the next row shifts down, so using
			// 0 for every auto-reject of the same offer is correct.
			(void)i;
		}
	}

	// Dim the emulator / underlying screen so the user can see this
	// prompt owns the display and shouldn't be ignored.  Drawn on the
	// background list so BeginSheet's own window paints over it.
	{
		const ImGuiIO &io = ImGui::GetIO();
		ImGui::GetBackgroundDrawList()->AddRectFilled(
			ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 192));
	}

	bool open = true;
	if (!BeginSheet("Join requests", &open,
	                ImVec2(Dp(480), Dp(360)),
	                ImVec2(Dp(720), Dp(640))))
		return;

	if (ScreenHeader("Join requests")) {
		// Back = "Deny all" — don't leave any queued peer hanging.
		const size_t nReq = st.session.pendingRequests.size();
		for (const auto& r : st.session.pendingRequests) {
			ATNetplayGlue::HostRejectPending(r.hostedGameId.c_str(), 0);
		}
		if (nReq > 0) {
			PushToast(nReq == 1
				? "Join request declined."
				: "All join requests declined.",
				ToastSeverity::Info, 3500);
		}
		EndSheet();
		return;
	}

	BeginScreenBody(/*reserveBottomDp*/ 0.0f);

	// Per-offer indices for the Allow/Deny buttons: the UI vector
	// lists every request across every offer in glue order, but the
	// coord's per-offer queue indexes from 0 within each offer.  We
	// rebuild the per-offer index here so row 2 of offer-A maps to
	// coord index 2, even if offer-B is interleaved in the UI list.
	std::vector<size_t> perOfferIdx(st.session.pendingRequests.size(), 0);
	{
		std::unordered_map<std::string, size_t> seen;
		for (size_t i = 0; i < st.session.pendingRequests.size(); ++i) {
			const auto& r = st.session.pendingRequests[i];
			perOfferIdx[i] = seen[r.hostedGameId]++;
		}
	}

	// Action collected during the row loop.  Applied after the loop
	// so the vector indices stay stable for the frame.
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
		// PeerChip renders handle + optional region + private marker.
		// The incoming-join request currently carries only the
		// joiner's handle; if/when the coord plumbs region + RTT into
		// the PendingJoinRequest the chip will pick them up without
		// changing this call site.  Suppresses the ping pill cleanly
		// when no data is available (PeerChip treats empty region as
		// "don't render the region token").
		PeerChip(handle, /*region*/ "", /*isPrivate*/ false);
		char line[160];
		std::snprintf(line, sizeof line, "wants to join %s", r.gameName.c_str());
		ATTouchMutedText(line);

		char timers[96];
		std::snprintf(timers, sizeof timers,
			"Requested %llus ago · auto-decline in %llus",
			(unsigned long long)elapsedS,
			(unsigned long long)remainS);
		ATTouchMutedText(timers);

		ImGui::Spacing();
		float bW = (ImGui::GetContentRegionAvail().x - Dp(10)) * 0.5f;
		if (ATTouchButton("Deny",
		                  ImVec2(bW, Dp(ATTouch::kButtonHeightNormal)),
		                  ATTouchButtonStyle::Danger)) {
			action = ActionKind::Reject;
			actionGameId = r.hostedGameId;
			actionIdx = perOfferIdx[i];
		}
		ImGui::SameLine(0, Dp(10));
		// First pending request's Allow button gets keyboard/gamepad
		// focus on screen open — this is the interrupt surface and
		// the user needs one-button acceptance via Enter / Gamepad A.
		if (i == 0 && ConsumeFocusRequest(4005))
			ImGui::SetKeyboardFocusHere();
		if (ATTouchButton("Allow",
		                  ImVec2(bW, Dp(ATTouch::kButtonHeightNormal)),
		                  ATTouchButtonStyle::Accent)) {
			action = ActionKind::Accept;
			actionGameId = r.hostedGameId;
			actionIdx = perOfferIdx[i];
		}
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		ImGui::PopID();
	}

	// Single-request case: Allow already fully explains the side-effect.
	// Multi-request case: also show the blanket Deny-all so the user
	// doesn't have to click N Denys in a row.
	if (st.session.pendingRequests.size() > 1) {
		ImGui::Spacing();
		if (ATTouchButton("Deny all",
		                  ImVec2(-FLT_MIN, Dp(ATTouch::kButtonHeightNormal)),
		                  ATTouchButtonStyle::Danger)) {
			action = ActionKind::RejectAll;
		}
	}

	// Transparency footer — what happens when the host says yes.
	ImGui::Spacing();
	ATTouchMutedText(
		"Accepting replaces your current emulator game with the "
		"hosted one for the online session.  Your game is saved "
		"automatically and restored when the session ends.");

	if (!open) action = ActionKind::RejectAll;

	EndScreenBody();

	EndSheet();

	// Apply deferred actions outside the render loop.
	for (const auto& ar : autoRejects) {
		ATNetplayGlue::HostRejectPending(ar.first.c_str(), ar.second);
	}
	switch (action) {
		case ActionKind::Accept:
			ATNetplayGlue::HostAcceptPending(actionGameId.c_str(), actionIdx);
			// Accept replaces the sim, so we don't want the
			// promptSaved* bookkeeping to fight the SessionRestorePoint
			// lifecycle when the session ends.  Clear it and let the
			// "session ended" restore handle return-to-previous.
			st.session.promptSavedValid = false;
			if (st.session.promptPausedSim && g_sim.IsPaused()) {
				g_sim.Resume();
			}
			st.session.promptPausedSim = false;
			break;
		case ActionKind::Reject:
			ATNetplayGlue::HostRejectPending(actionGameId.c_str(), actionIdx);
			PushToast("Join request declined.",
				ToastSeverity::Info, 3500);
			break;
		case ActionKind::RejectAll:
			// Reject every queued entry.  Walk the UI vector (which
			// matches glue order) but always reject at the per-offer
			// index 0, since each reject shifts the next one into slot
			// 0 within that offer's coord queue.
			{
				const size_t n = st.session.pendingRequests.size();
				for (const auto& r : st.session.pendingRequests) {
					ATNetplayGlue::HostRejectPending(r.hostedGameId.c_str(), 0);
				}
				if (n > 0) {
					PushToast(n == 1
						? "Join request declined."
						: "All join requests declined.",
						ToastSeverity::Info, 3500);
				}
			}
			break;
		case ActionKind::None:
			break;
	}
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

	// Detect the WASM transport's "Browser blocked the connection"
	// pattern (transport_wasm.cpp emits it for close-code 1006 with
	// closeMs<10ms — the synchronous-refusal signature of an ad-blocker
	// or content-filter extension stopping the WebSocket from opening).
	// When it fires, render a structured help panel instead of just
	// dumping the technical string at the user; otherwise fall back to
	// the generic plain-text rendering.
	const bool blocked = !st.session.lastError.empty()
		&& st.session.lastError.find("Browser blocked the connection")
		   != std::string::npos;

	if (ScreenHeader(blocked ? "Connection blocked"
	                         : "Something went wrong")) {
		st.session.lastError.clear();
		Back();
	}

	BeginScreenBody(ATTouch::kFooterReserveSingle);

	ImGui::Spacing();
	if (blocked) {
		ATTouchSection("What happened");
		ImGui::PushTextWrapPos(0.0f);
		ATTouchMutedText(
			"Your browser refused to open the WebSocket connection that "
			"online play uses.  This is almost always caused by an "
			"ad-blocker, privacy extension, or DNS-level filter that "
			"treats real-time game traffic as suspicious.");
		ImGui::PopTextWrapPos();

		ImGui::Spacing();
		ATTouchSection("Try this");
		ImGui::PushTextWrapPos(0.0f);
		ATTouchMutedText(
			"1.  Disable your ad-blocker for this page (uBlock Origin, "
			"AdGuard, Privacy Badger, Ghostery — click the extension "
			"icon and toggle it off for lobby.atari.org.pl).\n"
			"\n"
			"2.  Open the page in a Private / Incognito window.  Most "
			"extensions stay disabled there, so this isolates the cause "
			"in seconds.\n"
			"\n"
			"3.  Try a different browser, or temporarily switch off any "
			"DNS-level blocker (NextDNS, AdGuard Home, Pi-hole) and "
			"corporate / school proxy.");
		ImGui::PopTextWrapPos();

		ImGui::Spacing();
		ATTouchSection("Technical details");
		ImGui::PushTextWrapPos(0.0f);
		ATTouchMutedText(st.session.lastError.c_str());
		ImGui::PopTextWrapPos();
	} else {
		ATTouchSection("Error");
		const char *msg = st.session.lastError.empty()
			? "An unknown error occurred."
			: st.session.lastError.c_str();
		ImGui::PushTextWrapPos(0.0f);
		ATTouchMutedText(msg);
		ImGui::PopTextWrapPos();
	}

	EndScreenBody();

	ImGui::Separator();
	ImGui::Spacing();
	if (ATTouchButton("OK", ImVec2(-FLT_MIN, Dp(ATTouch::kButtonHeightNormal)),
	                  ATTouchButtonStyle::Accent)) {
		st.session.lastError.clear();
		Back();
	}

	if (!open) { st.session.lastError.clear(); Back(); }
	EndSheet();
}

// -------------------------------------------------------------------
// Library Picker (shared Gaming Mode + Desktop) — full-screen sheet
// that lets the user choose a game from their Game Library.  Mirrors
// the UX patterns of the main mobile Game Browser: search, A-Z letter
// filter, grid tiles with cover art, variant sub-modal for entries
// with multiple ROMs.  Desktop falls back to a dense table.
//
// The picker is driven by navigating to Screen::LibraryPicker.  It
// reuses the same ATGameLibrary singleton the netplay browsers
// already hit via `LibrarySingleton()`, and pulls cover art through
// the existing `LookupArtByGameName()` helper so no new caches are
// needed.
//
// On commit (single-variant: one tap; multi-variant: picks variant
// via sub-modal) the result lands in the Gaming-Mode draft state
// (`s_mobilePicked*`) and we `Back()` to AddGame.  Desktop's own
// AddOffer reads the same draft so both modes share one picker flow.
// -------------------------------------------------------------------

// Picker transient state.  Cleared on sheet open.
char s_libPickSearch[128] = {};
int  s_libPickLetter = -1;    // -1 = All, 0..25 = A-Z, 26 = non-alpha
bool s_libPickAvail[27] = {};
bool s_libPickAvailDirty = true;
int  s_libPickVariantEntry = -1;  // >=0 while variant sub-modal open
bool s_libPickFocusSearchNext = false;

// Canonical first-letter bucket of a library entry name.  Returns
// 0..25 for A-Z (case-insensitive), 26 for any other leading char.
int LibPickLetterBucket(const wchar_t *s) {
	if (!s) return 26;
	while (*s == L' ' || *s == L'\t') ++s;
	wchar_t c = *s;
	if (c >= L'a' && c <= L'z') return (int)(c - L'a');
	if (c >= L'A' && c <= L'Z') return (int)(c - L'A');
	return 26;
}

bool LibPickEntryMatches(const GameEntry& e, const char *searchLower,
	int letterFilter)
{
	if (e.mVariants.empty()) return false;
	if (letterFilter >= 0) {
		if (LibPickLetterBucket(e.mDisplayName.c_str()) != letterFilter)
			return false;
	}
	if (searchLower && *searchLower) {
		VDStringA name = VDTextWToU8(e.mDisplayName);
		for (auto& ch : name)
			if (ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32);
		if (std::strstr(name.c_str(), searchLower) == nullptr)
			return false;
	}
	return true;
}

void LibPickComputeAvailable(const std::vector<GameEntry>& entries) {
	for (int i = 0; i < 27; ++i) s_libPickAvail[i] = false;
	for (const auto& e : entries) {
		if (e.mVariants.empty()) continue;
		int b = LibPickLetterBucket(e.mDisplayName.c_str());
		s_libPickAvail[b] = true;
	}
	s_libPickAvailDirty = false;
}

// Small sub-modal: when the user picks a multi-variant entry, ask
// which variant to host.  Returns `true` iff the user committed a
// pick in this frame; returns via out-params.  On cancel, clears the
// picker state and returns false.
bool RenderLibVariantSubModal(ATGameLibrary& lib,
	int& outEntryIdx, int& outVariantIdx,
	VDStringW& outPath, VDStringW& outDisplay, VDStringW& outLabel)
{
	if (s_libPickVariantEntry < 0) return false;
	const auto& entries = lib.GetEntries();
	if ((size_t)s_libPickVariantEntry >= entries.size()) {
		s_libPickVariantEntry = -1;
		return false;
	}
	const GameEntry& e = entries[s_libPickVariantEntry];
	VDStringA title = VDTextWToU8(e.mDisplayName);

	ImGui::SetNextWindowSize(ImVec2(Dp(380), 0), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoSavedSettings;

	bool commit = false;
	bool open = true;
	char winTitle[256];
	std::snprintf(winTitle, sizeof winTitle,
		"Select variant — %s###libpickvariant", title.c_str());
	if (ImGui::Begin(winTitle, &open, flags)) {
		ImGui::TextUnformatted("Which variant to host?");
		ImGui::Spacing();
		for (size_t i = 0; i < e.mVariants.size(); ++i) {
			const auto& v = e.mVariants[i];
			VDStringA lab = VDTextWToU8(v.mLabel);
			char szStr[32];
			if (v.mFileSize >= 1024 * 1024)
				std::snprintf(szStr, sizeof szStr, "%.1f MB",
					v.mFileSize / (1024.0 * 1024.0));
			else if (v.mFileSize >= 1024)
				std::snprintf(szStr, sizeof szStr, "%d KB",
					(int)(v.mFileSize / 1024));
			else
				std::snprintf(szStr, sizeof szStr, "%d B",
					(int)v.mFileSize);

			char btn[320];
			std::snprintf(btn, sizeof btn, "%s    %s##vpv%zu",
				lab.c_str(), szStr, i);
			if (i == 0) ImGui::SetItemDefaultFocus();
			if (ATTouchButton(btn,
				ImVec2(-FLT_MIN, Dp(ATTouch::kButtonHeightNormal)),
				ATTouchButtonStyle::Neutral))
			{
				outEntryIdx   = s_libPickVariantEntry;
				outVariantIdx = (int)i;
				outPath       = v.mPath;
				outDisplay    = e.mDisplayName;
				outLabel      = v.mLabel;
				commit = true;
			}
		}
		ImGui::Spacing();
		if (ATTouchButton("Cancel",
			ImVec2(-FLT_MIN, Dp(ATTouch::kButtonHeightSmall)),
			ATTouchButtonStyle::Subtle))
		{
			s_libPickVariantEntry = -1;
		}
	}
	ImGui::End();
	if (commit || !open) s_libPickVariantEntry = -1;
	return commit;
}

// Render the Desktop table view of the picker.  Returns true if the
// user committed a single-variant pick this frame (result via
// out-params); false otherwise.  The variant sub-modal is rendered
// separately above.
bool RenderLibPickDesktopTable(ATGameLibrary& lib,
	int& outEntryIdx, int& outVariantIdx,
	VDStringW& outPath, VDStringW& outDisplay, VDStringW& outLabel)
{
	const auto& entries = lib.GetEntries();
	const ImGuiTableFlags tf = ImGuiTableFlags_RowBg
		| ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY
		| ImGuiTableFlags_Resizable;
	ImVec2 sz(0, ImGui::GetContentRegionAvail().y);

	char searchLower[128];
	std::snprintf(searchLower, sizeof searchLower, "%s", s_libPickSearch);
	for (char *c = searchLower; *c; ++c)
		if (*c >= 'A' && *c <= 'Z') *c = (char)(*c + 32);

	bool committed = false;
	if (ImGui::BeginTable("##libpick_tbl", 3, tf, sz)) {
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Game");
		ImGui::TableSetupColumn("Variants",
			ImGuiTableColumnFlags_WidthFixed, 80.0f);
		ImGui::TableSetupColumn("Path");
		ImGui::TableHeadersRow();

		for (size_t i = 0; i < entries.size(); ++i) {
			const auto& e = entries[i];
			if (!LibPickEntryMatches(e, searchLower, s_libPickLetter))
				continue;

			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			VDStringA n = VDTextWToU8(e.mDisplayName);
			ImGui::PushID((int)i);
			bool activated = ImGui::Selectable(n.c_str(), false,
				ImGuiSelectableFlags_SpanAllColumns
				| ImGuiSelectableFlags_AllowDoubleClick);
			bool dbl = ImGui::IsItemHovered()
				&& ImGui::IsMouseDoubleClicked(0);
			bool keyEnter = ImGui::IsItemFocused()
				&& ImGui::IsKeyPressed(ImGuiKey_Enter, false);
			ImGui::PopID();
			ImGui::TableNextColumn();
			ImGui::Text("%zu", e.mVariants.size());
			ImGui::TableNextColumn();
			VDStringA primary = e.mVariants.empty()
				? VDStringA()
				: VDTextWToU8(e.mVariants[0].mPath);
			ImGui::TextUnformatted(primary.c_str());

			if (dbl || keyEnter || activated) {
				if (e.mVariants.size() == 1) {
					outEntryIdx   = (int)i;
					outVariantIdx = 0;
					outPath       = e.mVariants[0].mPath;
					outDisplay    = e.mDisplayName;
					outLabel      = e.mVariants[0].mLabel;
					committed = true;
				} else if (e.mVariants.size() > 1) {
					s_libPickVariantEntry = (int)i;
				}
			}
		}
		ImGui::EndTable();
	}
	return committed;
}

// Render the Gaming-Mode grid view — matches the main Game Browser:
// one row of cover-art tiles per chunk of entries, art loaded on
// demand from the shared GameArtCache via `e.mArtPath` (a direct
// path-keyed lookup — O(1) per tile, unlike the O(N²) basename
// matcher used by joiner rows).  Returns true on single-variant tap
// commit.
bool RenderLibPickMobileGrid(ATGameLibrary& lib,
	int& outEntryIdx, int& outVariantIdx,
	VDStringW& outPath, VDStringW& outDisplay, VDStringW& outLabel)
{
	const auto& entries = lib.GetEntries();

	char searchLower[128];
	std::snprintf(searchLower, sizeof searchLower, "%s", s_libPickSearch);
	for (char *c = searchLower; *c; ++c)
		if (*c >= 'A' && *c <= 'Z') *c = (char)(*c + 32);

	// Pump the art cache so freshly-visible tiles decode in the
	// background.  Safe no-op when the cache isn't initialised.
	PumpArtCache();
	GameArtCache *cache = GetGameArtCache();

	const float availW = ImGui::GetContentRegionAvail().x;
	const float tileMin = Dp(140.0f);
	int cols = std::max(1, (int)(availW / tileMin));
	if (cols > 6) cols = 6;
	const float gap = Dp(8.0f);
	const float tileW = (availW - gap * (cols - 1)) / (float)cols;
	const float imageH = tileW * 0.75f;
	const float nameH  = ImGui::GetTextLineHeight() * 1.6f;
	const float tileH  = imageH + nameH + Dp(6.0f);

	bool committed = false;
	// The grid is already inside BeginScreenBody() (the sole scroll
	// host + drag-scroll driver); a nested BeginChild + second
	// ATTouchDragScroll would fight the outer scroll.  Wrap only for
	// focus scoping so keyboard/gamepad nav is flat across tiles.
	ImGui::BeginChild("##libpickgrid", ImVec2(0, 0),
		ImGuiChildFlags_NavFlattened, 0);

	int drawn = 0;
	for (size_t i = 0; i < entries.size(); ++i) {
		const GameEntry& e = entries[i];
		if (!LibPickEntryMatches(e, searchLower, s_libPickLetter))
			continue;

		if (drawn % cols != 0) ImGui::SameLine(0, gap);

		ImVec2 cursor = ImGui::GetCursorScreenPos();
		ImDrawList *dl = ImGui::GetWindowDrawList();

		char id[32];
		std::snprintf(id, sizeof id, "##ltile_%zu", i);

		ImGui::PushID((int)i);
		ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0,0,0,0));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0,0,0,0));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0,0,0,0));
		bool clicked = ImGui::Selectable(id, false,
			ImGuiSelectableFlags_None, ImVec2(tileW, tileH));
		ImGui::PopStyleColor(3);
		bool hovered = ImGui::IsItemHovered() || ImGui::IsItemFocused();
		ImGui::PopID();

		ImVec2 imgTL(cursor.x, cursor.y);
		ImVec2 imgBR(cursor.x + tileW, cursor.y + imageH);
		dl->AddRectFilled(imgTL, imgBR, IM_COL32(20, 20, 25, 255),
			Dp(4.0f));

		int artW = 0, artH = 0;
		ImTextureID artTex = (ImTextureID)0;
		if (cache && !e.mArtPath.empty())
			artTex = cache->GetTexture(e.mArtPath, &artW, &artH);

		if (artTex && artW > 0 && artH > 0) {
			float srcA = (float)artW / (float)artH;
			float tileA = tileW / imageH;
			float dW, dH;
			if (srcA > tileA) { dW = tileW; dH = tileW / srcA; }
			else              { dH = imageH; dW = imageH * srcA; }
			float ox = (tileW - dW) * 0.5f;
			float oy = (imageH - dH) * 0.5f;
			dl->AddImage(artTex,
				ImVec2(imgTL.x + ox, imgTL.y + oy),
				ImVec2(imgTL.x + ox + dW, imgTL.y + oy + dH));
		} else {
			// Placeholder glyph when no art is available.
			const char *glyph = e.mVariants.empty() ? "?" : "*";
			ImVec2 gs = ImGui::CalcTextSize(glyph);
			dl->AddText(ImVec2(cursor.x + (tileW - gs.x) * 0.5f,
				cursor.y + (imageH - gs.y) * 0.5f),
				IM_COL32(180, 180, 190, 200), glyph);
		}

		// Variant-count pill.
		if (e.mVariants.size() > 1) {
			char cnt[16];
			std::snprintf(cnt, sizeof cnt, "x%zu", e.mVariants.size());
			ImVec2 cs = ImGui::CalcTextSize(cnt);
			float pad = Dp(4.0f);
			dl->AddRectFilled(
				ImVec2(imgBR.x - cs.x - pad * 2, imgTL.y),
				ImVec2(imgBR.x, imgTL.y + cs.y + pad * 2),
				IM_COL32(0, 0, 0, 180), Dp(4.0f));
			dl->AddText(
				ImVec2(imgBR.x - cs.x - pad, imgTL.y + pad),
				IM_COL32(255, 255, 255, 220), cnt);
		}

		const ATMobilePalette &pal = ATMobileGetPalette();
		if (hovered) {
			dl->AddRect(imgTL, imgBR, pal.rowFocus, Dp(4.0f),
				0, Dp(2.0f));
		}

		// Name below tile (clipped to the tile width).
		VDStringA nameU8 = VDTextWToU8(e.mDisplayName);
		float nameY = cursor.y + imageH + Dp(4.0f);
		ImGui::PushClipRect(ImVec2(cursor.x, nameY),
			ImVec2(cursor.x + tileW, cursor.y + tileH), true);
		ImVec2 ns = ImGui::CalcTextSize(nameU8.c_str());
		float nx = cursor.x + (tileW - ns.x) * 0.5f;
		if (nx < cursor.x) nx = cursor.x;
		dl->AddText(ImVec2(nx, nameY), pal.text, nameU8.c_str());
		ImGui::PopClipRect();

		if (clicked && !ATTouchIsDraggingBeyondSlop()) {
			if (e.mVariants.size() == 1) {
				outEntryIdx   = (int)i;
				outVariantIdx = 0;
				outPath       = e.mVariants[0].mPath;
				outDisplay    = e.mDisplayName;
				outLabel      = e.mVariants[0].mLabel;
				committed = true;
			} else if (e.mVariants.size() > 1) {
				s_libPickVariantEntry = (int)i;
			}
		}
		++drawn;
	}

	if (drawn == 0) {
		ImGui::Spacing();
		if (entries.empty()) {
			ATTouchEmptyState(
				"Game Library is empty",
				"Add source folders in Settings → Game Library, then "
				"come back here to pick a game to host.",
				nullptr, nullptr);
		} else {
			ATTouchEmptyState(
				"No games match",
				"Clear the search box or letter filter to see every "
				"library entry again.",
				nullptr, nullptr);
		}
	}

	ImGui::EndChild();
	return committed;
}

} // close anonymous namespace so RenderLibraryPicker is externally
  // visible — Desktop Dispatch in ui_netplay_desktop.cpp calls into
  // this shared definition.

void RenderLibraryPicker() {
	ATGameLibrary& lib = LibrarySingleton();

	// Pump background scan results into mEntries.  In Gaming Mode the
	// Game Browser screen pumps this every frame; in Desktop the netplay
	// picker is the only surface that touches the library, so it must
	// pump itself — otherwise LoadCache's snapshot is all the user ever
	// sees, and if the on-disk cache is a zero-entry snapshot (happens
	// when a prior SaveCache ran before the scan produced results) the
	// table stays empty forever even though a full scan is in flight.
	lib.ConsumeScanResults();
	const auto& entries = lib.GetEntries();
	if (s_libPickAvailDirty || entries.empty())
		LibPickComputeAvailable(entries);
	if (!entries.empty())
		s_libPickAvailDirty = false;

	bool open = true;
	if (!BeginSheet("Pick a Game to Host", &open,
	                ImVec2(Dp(640), Dp(520)),
	                ImVec2(Dp(1400), Dp(900)))) {
		return;
	}

	if (ScreenHeader("Pick a Game to Host")) {
		Back();
	}

	// Gaming Mode wraps the whole picker body (banner + search + letter
	// row + tile grid) in the shared scrollable region so swipe + nav
	// behave like the Settings and Hosted Games screens.  Desktop keeps
	// its own BeginChild inside the grid block below.
	const bool pickerGaming = ATUIIsGamingMode();
	if (pickerGaming) BeginScreenBody(/*reserveBottomDp*/ 0.0f);

	// Banner explaining the picker context — same string Gaming Mode
	// and Desktop; reads cleanly on both.
	ATTouchMutedText(
		"Select a game from your Library to host — Esc/Back to cancel "
		"and keep the previous selection.");

	// Scan-in-progress hint so the user understands why the list may be
	// incomplete or empty on first open.
	if (lib.IsScanning()) {
		VDStringA status = lib.GetScanStatus();
		if (status.empty()) status = "Scanning game sources…";
		ATTouchMutedText(status.c_str());
	} else if (entries.empty()) {
		ATTouchMutedText(
			"Game Library is empty — add sources in Settings → "
			"Game Library, then re-open this picker.");
	}
	ImGui::Spacing();

	// ── Search + Letter row ────────────────────────────────────
	{
		float rowW = ImGui::GetContentRegionAvail().x;
		float searchW = std::min(rowW * 0.4f, Dp(280.0f));
		if (s_libPickFocusSearchNext) {
			ImGui::SetKeyboardFocusHere();
			s_libPickFocusSearchNext = false;
		}
		ImGui::SetNextItemWidth(searchW);
		if (ATTouchInputTextWithHintScrollAware("##libpicksearch", "Search…",
			s_libPickSearch, sizeof s_libPickSearch))
		{
			// Typing a search implicitly clears the letter filter so the
			// user isn't hidden from matching rows.
			s_libPickLetter = -1;
		}

		ImGui::SameLine();
		if (ATTouchButton("Clear##libpick",
			ImVec2(Dp(70), Dp(28)), ATTouchButtonStyle::Subtle))
		{
			s_libPickSearch[0] = 0;
			s_libPickLetter = -1;
		}
	}

	// A-Z buttons.  Disabled letters (no entries) are visually dim
	// but remain focusable to keep keyboard nav consistent.
	// Desktop has a real keyboard and mouse — the text filter alone is
	// faster than hunting through alphabet pills, so the letter row is
	// Gaming-Mode only.
	if (ATUIIsGamingMode()) {
		const float letW = Dp(28);
		const float letH = Dp(28);
		// "All" button
		bool allActive = (s_libPickLetter < 0);
		ATTouchButtonStyle allStyle = allActive
			? ATTouchButtonStyle::Accent : ATTouchButtonStyle::Neutral;
		if (ATTouchButton("All##libpickL",
			ImVec2(Dp(40), letH), allStyle))
		{
			s_libPickLetter = -1;
		}

		for (int L = 0; L < 26; ++L) {
			ImGui::SameLine(0, Dp(2));
			char buf[12];
			std::snprintf(buf, sizeof buf, "%c##libpickL%d",
				(char)('A' + L), L);
			bool active = (s_libPickLetter == L);
			ATTouchButtonStyle st = active
				? ATTouchButtonStyle::Accent
				: ATTouchButtonStyle::Neutral;
			bool avail = s_libPickAvail[L];
			if (!avail) ImGui::BeginDisabled();
			if (ATTouchButton(buf, ImVec2(letW, letH), st)) {
				s_libPickLetter = L;
			}
			if (!avail) ImGui::EndDisabled();
		}
		if (s_libPickAvail[26]) {
			ImGui::SameLine(0, Dp(2));
			bool active = (s_libPickLetter == 26);
			ATTouchButtonStyle st = active
				? ATTouchButtonStyle::Accent
				: ATTouchButtonStyle::Neutral;
			if (ATTouchButton("###libpickL26", ImVec2(letW, letH), st))
				s_libPickLetter = 26;
		}
	}

	ImGui::Spacing();

	// ── Body: grid (Gaming Mode) / table (Desktop) ─────────────
	int eIdx = -1, vIdx = -1;
	VDStringW resPath, resDisplay, resLabel;
	bool committed = false;
	if (ATUIIsGamingMode()) {
		committed = RenderLibPickMobileGrid(lib, eIdx, vIdx,
			resPath, resDisplay, resLabel);
	} else {
		// Reserve space at the bottom for the Close button row so the
		// table doesn't consume every remaining pixel.
		const float closeRowH = ImGui::GetFrameHeightWithSpacing()
			+ ImGui::GetStyle().ItemSpacing.y;
		ImGui::BeginChild("##libpick_body",
			ImVec2(0, ImGui::GetContentRegionAvail().y - closeRowH),
			ImGuiChildFlags_None, 0);
		committed = RenderLibPickDesktopTable(lib, eIdx, vIdx,
			resPath, resDisplay, resLabel);
		ImGui::EndChild();

		ImGui::Separator();
		if (ImGui::Button("Close", ImVec2(Dp(120), 0))) {
			Back();
		}
	}

	// Variant sub-modal — may also commit this frame.
	if (s_libPickVariantEntry >= 0) {
		bool vc = RenderLibVariantSubModal(lib, eIdx, vIdx,
			resPath, resDisplay, resLabel);
		if (vc) committed = true;
	}

	if (committed) {
		VDStringA pU8 = VDTextWToU8(resPath);
		VDStringA nU8 = VDTextWToU8(resDisplay);
		VDStringA lU8 = VDTextWToU8(resLabel);
		OfferDraft& d = GetState().offerDraft;
		d.path.assign(pU8.c_str(), pU8.size());
		d.displayName.assign(nU8.c_str(), nU8.size());
		d.source = OfferSource::Library;
		d.variantLabel.assign(lU8.c_str(), lU8.size());
		d.libraryEntryIdx = eIdx;
		d.libraryVariantIdx = vIdx;
		s_libPickVariantEntry = -1;
		Back();
	}

	if (pickerGaming) EndScreenBody();

	if (!open) Back();
	EndSheet();
}

namespace { // reopen anonymous namespace — remaining screen renderers
            // and their helpers continue to be file-local.

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

	// Scroll the whole sub-header (reachability + activity hint + the
	// three hero cards) so the list stays usable on very short safe-
	// area heights (landscape phone with nav gestures).
	BeginScreenBody(/*reserveBottomDp*/ 0.0f);

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
			"%zu game%s off — none open.",
			st.hostedGames.size(),
			st.hostedGames.size() == 1 ? "" : "s");
	} else {
		std::snprintf(hostSub, sizeof hostSub,
			"%d open on the lobby.", enabledCount);
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
		// Shortcut: delegate to the Gaming-Mode Settings tree so every
		// configuration category lives in one place.  Persist what the
		// user has already set, close the netplay overlay so the
		// Settings screen can render without a ghost underneath, and
		// leave the OpenOnlinePlaySettings helper to arrange the
		// return-to-hub back path.
		SaveToRegistry();
		Navigate(Screen::Closed);
		ATMobileUI_OpenOnlinePlaySettings();
	}

	EndScreenBody();

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

	// While the Gaming-Mode Game Browser is running in picker mode
	// (netplay's "Pick from Library" handoff), skip drawing netplay
	// sheets so the browser is fully visible.  Return true so the
	// caller still considers netplay "active" — the in-session HUD
	// and toasts continue rendering, and the screen stack is
	// preserved so the AddGame sheet reappears once the picker hands
	// back a result.
	if (GameBrowser_IsPickerActive() && ATUIIsGamingMode()) return true;

	switch (st.screen) {
		case Screen::DeepLinkPrep:  RenderDeepLinkPrep();   break;
		case Screen::Nickname:      RenderNickname();       break;
		case Screen::OnlinePlayHub: RenderOnlinePlayHub();  break;
		case Screen::Browser:       RenderBrowser();        break;
		case Screen::MyHostedGames: RenderMyHostedGames();  break;
		case Screen::AddGame:       RenderAddOffer();       break;
		case Screen::LibraryPicker: RenderLibraryPicker();  break;
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
