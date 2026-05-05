//	AltirraSDL - Online Play deep-link state machine.
//
//	See ui_netplay_deeplink.h for the contract.  This file owns:
//	  - the pending sessionId / entry code stash (set by the WASM URL
//	    bridge or the native --join-session flag, before any netplay
//	    worker exists)
//	  - the per-frame state machine that consumes the stash once the
//	    rest of the app is ready, by issuing GET /v1/session/<id> via
//	    the lobby worker and then calling the standard
//	    StartJoiningAction.
//
//	Threading: the setters can be invoked from any startup thread.
//	DriveDeepLinkJoin() and OnDeepLinkLobbyResult() run on the main
//	thread from the netplay tick.  The async lobby fetch is enqueued on
//	the existing lobby worker; its result comes back through
//	ATNetplayUI_Poll's drain on the main thread the same way Browser
//	List() results do.

#include <stdafx.h>

#include "ui_netplay_deeplink.h"
#include "ui_netplay_state.h"
#include "ui_netplay_actions.h"
#include "ui_netplay_lobby_worker.h"
#include "ui_netplay.h"

#include "netplay/netplay_glue.h"

#include "ui/core/ui_mode.h"

#include <at/atcore/logging.h>

#include <vd2/system/registry.h>

#include <SDL3/SDL_video.h>

#include <mutex>

extern ATLogChannel g_ATLCNetplay;

#if defined(__EMSCRIPTEN__)
// WASM auto-fetch: forward-declare the wasm_bridge.cpp exports rather
// than #including the bridge header (which pulls emscripten.h).  The
// signatures are stable: ATWasmGetFirstRunState() returns the state
// machine value documented in wasm_bridge.cpp's StartFetchAt path
// (5/6/8 = ready, 7 = all mirrors failed, 0..4 = in progress).
extern "C" int  ATWasmGetFirstRunState();
extern "C" void ATWasmFirstRunBootstrap();
extern "C" void ATWasmResetFirstRun();
#endif

// We don't link against ui_mobile.cpp's SetFirstRunComplete() —
// duplicating the registry write here keeps this module's dependency
// surface narrow (and lets it compile on Desktop UI builds that
// don't include the mobile sources).  The "Mobile" / "FirstRunComplete"
// pair is the same one IsFirstRunComplete() above reads, so the two
// stay in sync.
extern void ATRegistryFlushToDisk();

namespace ATNetplayUI {

// Defined in ui_netplay.cpp.  Forward-declared here (not in the
// public header) to keep the worker singleton an implementation
// detail of the netplay UI module.
LobbyWorker& GetWorker();

namespace {

// Module-local stash + state machine bookkeeping.
//
// The setters (Set*DeepLink*) can fire arbitrarily early — for native
// builds, before main() has even reached the SDL init pump; for WASM,
// before the emscripten runtime has booted.  Only a mutex-guarded
// pair of strings is touched at that point.
//
// The state machine fields below (g_phase, g_fetchTag) are only
// touched on the main thread, from DriveDeepLinkJoin and
// OnDeepLinkLobbyResult.  They don't need the mutex.
std::mutex     g_deepLinkMu;
std::string    g_pendingSessionId;     // protected by g_deepLinkMu
std::string    g_pendingEntryCode;     // protected by g_deepLinkMu

// Internal state-machine phase.  The public DeepLinkUiState (in the
// header) is a thinner enum the renderer consults; this internal one
// also tracks transient transitions (Idle → reevaluate next tick).
enum class Phase {
	Idle,                // nothing pending or last attempt finished;
	                     // DriveDeepLinkJoin will (re)evaluate the
	                     // gates on the next call.
	WaitingForNickname,  // first-time visitor; SubmitDeepLinkNickname
	                     // will advance to WaitingForFirmware.
	WaitingForFirmware,  // WASM auto-firmware-fetch in flight; tick
	                     // polls ATWasmGetFirstRunState until it's
	                     // 5/6/8 (ready) or 7 (failed).
	FirmwareFailed,      // all mirrors failed; UI offers Retry which
	                     // calls RetryDeepLinkFirmware().
	FetchInFlight,       // GetById posted, waiting for
	                     // OnDeepLinkLobbyResult.
	Done,                // terminal — error already routed or join
	                     // fired; ClearPendingDeepLink has been
	                     // called.
};
Phase    g_phase    = Phase::Idle;
uint32_t g_fetchTag = 0;
bool     g_firmwareKicked = false;   // ATWasmFirstRunBootstrap call gate

// Distinct tag for our GetById posts so the result handler can
// recognise its own work and ignore other ops that happen to share
// the worker queue.  The high bit avoids collision with the offer-
// hash tags that ReconcileHostedGames uses for per-game Create.
//
// Reset to 0 on Cancel so a late lobby response (cancelled mid-fetch
// but the request was already on the wire) does NOT match — it would
// otherwise either re-fire StartJoiningAction (popping the user back
// into the flow they cancelled) or route them to the Error screen
// after they navigated away.
constexpr uint32_t kDeepLinkTagBase = 0x5DEA1ED0u;
constexpr uint32_t kDeepLinkTagNone = 0u;

// Read the same registry key the Gaming Mode setup wizard uses to
// signal "user has been through first-run".  We don't link against
// ui_mobile.cpp's IsFirstRunComplete() to keep this module's
// dependency surface narrow (and to compile on builds that don't
// even include the mobile UI sources).
bool IsFirstRunComplete() {
	VDRegistryAppKey key("Mobile", false);
	if (!key.isReady()) return false;
	return key.getBool("FirstRunComplete", false);
}

void RouteToError(const std::string& msg) {
	State& st = GetState();
	st.session.lastError = msg;
	Navigate(Screen::Error);
}

// Duplicate of ui_mobile.cpp's SetFirstRunComplete — see comment above
// the extern.  Writes "Mobile/FirstRunComplete=true" and flushes the
// registry to disk so the value survives the WASM page-close that
// often happens before any natural flush opportunity.
void MarkFirstRunComplete() {
	VDRegistryAppKey key("Mobile", true);
	if (key.isReady())
		key.setBool("FirstRunComplete", true);
	try { ATRegistryFlushToDisk(); } catch (...) {}
}

// Same-page navigation helper — only Navigate if we aren't already
// on the target.  Avoids pushing a second copy of DeepLinkPrep onto
// the back stack each frame the prep screen is active.
void EnsureOnScreen(Screen target) {
	State& st = GetState();
	if (st.screen != target)
		Navigate(target);
}

} // namespace anonymous

void SetPendingDeepLinkSessionId(const std::string& sessionId) {
	if (sessionId.empty()) return;
	{
		std::lock_guard<std::mutex> lk(g_deepLinkMu);
		g_pendingSessionId = sessionId;
	}
	g_ATLCNetplay("deeplink: set pending session=%.8s...",
		sessionId.c_str());
}

void SetPendingDeepLinkCode(const std::string& entryCode) {
	{
		std::lock_guard<std::mutex> lk(g_deepLinkMu);
		g_pendingEntryCode = entryCode;
	}
	g_ATLCNetplay("deeplink: set pending entry-code (%zu chars)",
		entryCode.size());
}

void ClearPendingDeepLink() {
	std::lock_guard<std::mutex> lk(g_deepLinkMu);
	g_pendingSessionId.clear();
	g_pendingEntryCode.clear();
}

bool HasPendingDeepLink() {
	std::lock_guard<std::mutex> lk(g_deepLinkMu);
	return !g_pendingSessionId.empty();
}

void DriveDeepLinkJoin() {
	// Cheap early-out: nothing to do unless the user actually arrived
	// via a deep-link URL.
	std::string sessionId, entryCode;
	{
		std::lock_guard<std::mutex> lk(g_deepLinkMu);
		sessionId = g_pendingSessionId;
		entryCode = g_pendingEntryCode;
	}
	if (sessionId.empty()) return;

	// Once we've terminated (error routed or join fired), don't try
	// again — even if the stash still has a leftover value.  A fresh
	// SetPendingDeepLinkSessionId() would be needed (e.g. a future
	// in-app "open this URL" flow).
	if (g_phase == Phase::Done)             return;
	if (g_phase == Phase::FetchInFlight)    return;  // wait for response

	// Phase 0 — Gaming Mode handoff.  Arriving via ?s=<id> is by
	// definition the user saying "I clicked Join, take me into the
	// game", which is the canonical Gaming Mode use case (touch HUD,
	// large-tap UI, no debugger menus).  Critically: the DeepLinkPrep
	// renderer (nickname / "Downloading game files…" / "Looking up the
	// game") lives only in the Gaming-Mode dispatcher; the Desktop
	// dispatcher in ui_netplay_desktop.cpp has no case for
	// Screen::DeepLinkPrep, so leaving the user in Desktop UI parks the
	// state machine in WaitingForNickname forever — the user sees a
	// bare emulator with no UI feedback, and any menu navigation
	// they attempt is bounced back to DeepLinkPrep on the next frame
	// by EnsureOnScreen().  Switching here, before any phase logic,
	// ensures the prep flow renders.  The user can switch back via the
	// hamburger after the join finishes.
	if (!ATUIIsGamingMode()) {
		g_ATLCNetplay("deeplink: switching to Gaming Mode for one-click join");
		ATUISetMode(ATUIMode::Gaming);
		// Re-apply ImGui style under the new mode.  ATUISetMode flips
		// only a flag; the visual sizing (ScaleAllSizes for big touch
		// targets) is applied lazily.  Without this call the first few
		// DeepLinkPrep frames would render at Desktop sizes — small
		// buttons, tight padding — until something else triggers a
		// restyle.  We don't have an SDL_Window* here, but the primary
		// display has the same content scale as the window for the
		// single-display configurations where deep-link join applies
		// (WASM-in-browser is the primary target; native desktop and
		// Android also work because they use the primary display too).
		SDL_DisplayID disp = SDL_GetPrimaryDisplay();
		float cs = (disp != 0) ? SDL_GetDisplayContentScale(disp) : 1.0f;
		if (cs < 1.0f) cs = 1.0f;
		if (cs > 4.0f) cs = 4.0f;
		ATUIApplyModeStyle(cs);
	}

	// Phase 1 — nickname.  When the user arrives via deep-link we
	// skip the full Gaming Mode setup wizard and just ask for a name
	// (so the host's "X wants to join" prompt shows a real handle,
	// not "Player_AB12").  The firmware fetch runs in parallel from
	// the WASM bootstrap so it's already a head-start by the time
	// the user types.
	if (!IsFirstRunComplete()) {
		if (g_phase != Phase::WaitingForNickname) {
			g_ATLCNetplay("deeplink: first-run gate open — asking for nickname");
		}
		g_phase = Phase::WaitingForNickname;
		EnsureOnScreen(Screen::DeepLinkPrep);
		return;
	}

	// Phase 2 — firmware (WASM only).  Wait until the auto-fetch
	// (kFirstRunUrls cascade) has produced an extracted ROM.  States
	// 5/6/8 mean "ready"; 7 means "all mirrors failed"; 0..4 mean
	// "in progress".  Idempotent: ATWasmFirstRunBootstrap uses an
	// atomic guard so a second call from here is a no-op if JS
	// already kicked it.  We log only on transitions (not every
	// frame poll) to keep the log readable.
#if defined(__EMSCRIPTEN__)
	{
		const int s = ATWasmGetFirstRunState();
		if (s == 7) {
			if (g_phase != Phase::FirmwareFailed) {
				g_ATLCNetplay("deeplink: firmware fetch failed (state=7) — "
					"all mirrors exhausted");
			}
			g_phase = Phase::FirmwareFailed;
			EnsureOnScreen(Screen::DeepLinkPrep);
			return;
		}
		if (s >= 0 && s <= 4) {
			if (!g_firmwareKicked) {
				g_ATLCNetplay("deeplink: kicking auto-firmware fetch "
					"(state=%d → bootstrap)", s);
				ATWasmFirstRunBootstrap();
				g_firmwareKicked = true;
			}
			if (g_phase != Phase::WaitingForFirmware) {
				g_ATLCNetplay("deeplink: waiting for firmware (state=%d)", s);
			}
			g_phase = Phase::WaitingForFirmware;
			EnsureOnScreen(Screen::DeepLinkPrep);
			return;
		}
		if (g_phase == Phase::WaitingForFirmware) {
			g_ATLCNetplay("deeplink: firmware ready (state=%d) — "
				"continuing to lobby fetch", s);
		}
		// 5 / 6 / 8 = firmware ready (or already-present); fall through.
	}
#endif

	// Phase 3 — gates: worker + no-active-session.  No screen change
	// here; we should already be on DeepLinkPrep showing "Looking up
	// the game…" once the fetch is in flight.
	if (!GetWorker().IsRunning())   return;
	if (ATNetplayGlue::IsActive())  return;

	// Phase 4 — issue the lobby fetch.  We pick the first enabled
	// HTTP lobby; if the user has multiple federated lobbies
	// configured, the deep-link assumes the one that minted the URL
	// is the same one configured first (which is the default —
	// lobby.atari.org.pl).  A failed fetch falls through to the
	// Error screen, so a wrong-lobby URL surfaces as "session not
	// found" rather than silently retrying forever.
	auto lobbies = AllEnabledHttpLobbies();
	if (lobbies.empty()) {
		RouteToError(
			"Online Play isn't configured.  Open Online Play → Settings "
			"to add a lobby, then try the link again.");
		ClearPendingDeepLink();
		g_phase = Phase::Done;
		return;
	}

	LobbyRequest req;
	req.op        = LobbyOp::GetById;
	req.endpoint  = lobbies.front().endpoint;
	req.sessionId = sessionId;
	req.tag       = kDeepLinkTagBase;
	g_fetchTag    = req.tag;

	if (!GetWorker().Post(std::move(req), lobbies.front().section)) {
		g_ATLCNetplay("deeplink: lobby Post() failed (worker overloaded?) "
			"— routing to error");
		RouteToError("Couldn't reach the lobby — try again in a moment.");
		ClearPendingDeepLink();
		g_phase = Phase::Done;
		return;
	}
	g_ATLCNetplay("deeplink: lobby fetch posted (sid=%.8s..., section=%s)",
		sessionId.c_str(), lobbies.front().section.c_str());
	g_phase = Phase::FetchInFlight;
	EnsureOnScreen(Screen::DeepLinkPrep);
}

bool OnDeepLinkLobbyResult(const LobbyResult& r) {
	if (r.op != LobbyOp::GetById)   return false;
	// Match against our latest tag — Cancel resets g_fetchTag to 0 so
	// a late response from a cancelled request fails this check and
	// drops silently (no StartJoiningAction re-fire, no error screen
	// after the user already navigated away).
	if (g_fetchTag == kDeepLinkTagNone) return false;
	if (r.tag != g_fetchTag)        return false;

	// Disarm the tag immediately so a (defensive) duplicate response
	// from the worker cannot re-enter this handler.  The worker
	// shouldn't deliver duplicates, but it's cheap to be defensive.
	g_fetchTag = kDeepLinkTagNone;
	g_phase = Phase::Idle;  // clear in-flight; next branch sets terminal

	State& st = GetState();
	if (!r.ok || r.sessions.empty()) {
		g_ATLCNetplay("deeplink: lobby fetch failed (http=%d, error=\"%s\")",
			r.httpStatus, r.error.c_str());
		// Friendly translation by HTTP status.
		if (r.httpStatus == 404) {
			RouteToError(
				"That game is no longer being hosted.  The host may have "
				"ended the session or restarted.");
		} else if (r.httpStatus == 0) {
			RouteToError(
				"Couldn't reach the lobby.  Check your connection and "
				"try the link again.");
		} else if (r.httpStatus == 429 || r.httpStatus == 503
		        || r.httpStatus == 504) {
			RouteToError(
				"The lobby is currently busy or temporarily unavailable "
				"(HTTP " + std::to_string(r.httpStatus) +
				").  Wait a few seconds and try the link again.");
		} else {
			std::string msg = "Couldn't load the deep-link session";
			if (!r.error.empty()) {
				msg += " — ";
				msg += r.error;
			} else if (r.httpStatus > 0) {
				char buf[64];
				std::snprintf(buf, sizeof buf, " (HTTP %d)", r.httpStatus);
				msg += buf;
			}
			msg += ".";
			RouteToError(msg);
		}
		ClearPendingDeepLink();
		g_phase = Phase::Done;
		return true;
	}

	// Populate the standard join target so StartJoiningAction can run
	// the same code path a Browser-row click does.
	st.session.joinTarget = r.sessions.front();
	st.session.joinTarget.sourceLobby = r.sourceLobby;

	// Apply any entry-code captured by --join-code / ?code=…
	{
		std::lock_guard<std::mutex> lk(g_deepLinkMu);
		st.session.joinEntryCode = g_pendingEntryCode;
	}

	// Wipe the stash before we hand off — a successful join should
	// not re-trigger if the user later navigates back and forth.
	ClearPendingDeepLink();
	g_phase = Phase::Done;

	// Gaming Mode was already turned on at the start of
	// DriveDeepLinkJoin (Phase 0).  No second switch needed here.

	g_ATLCNetplay("deeplink: lobby fetch OK — handing off to "
		"StartJoiningAction (cart=\"%s\", host=\"%s\")",
		st.session.joinTarget.cartName.c_str(),
		st.session.joinTarget.hostHandle.c_str());
	StartJoiningAction();
	return true;
}

DeepLinkUiState GetDeepLinkUiState() {
	// Phase is touched only on the main thread; no mutex needed for
	// the read.  The pending-session string is mutated from the URL
	// bridge on the main thread too (CLI parser runs before the loop),
	// so reading it without the mutex here is safe in practice — but
	// we still take it briefly because SetPendingDeepLinkSessionId can
	// fire from a future altirra:// handler thread.
	switch (g_phase) {
		case Phase::Idle:                return DeepLinkUiState::NotPending;
		case Phase::WaitingForNickname:  return DeepLinkUiState::NeedsNickname;
		case Phase::WaitingForFirmware:  return DeepLinkUiState::DownloadingFw;
		case Phase::FirmwareFailed:      return DeepLinkUiState::FirmwareFailed;
		case Phase::FetchInFlight:       return DeepLinkUiState::Looking;
		// Done means StartJoiningAction has already navigated us off
		// DeepLinkPrep onto JoinConfirm/Waiting.  If the user backs
		// onto DeepLinkPrep via the back stack, we want it to
		// auto-close (NotPending → renderer calls Navigate(Closed))
		// rather than show a stale "Joining…" indicator that the
		// Cancel button can no longer rewind.
		case Phase::Done:                return DeepLinkUiState::NotPending;
	}
	return DeepLinkUiState::NotPending;
}

void SubmitDeepLinkNickname(const std::string& nickIn) {
	// Trim and clamp to <=24 chars to match the regular Nickname
	// screen's contract.  An empty string after trim is treated as
	// "no name" — caller should disable the Continue button so this
	// doesn't fire.  Empty-but-anonymous mode isn't offered on the
	// deep-link path: the host's "X wants to join" prompt is much
	// more useful with a real name.
	std::string nick = nickIn;
	while (!nick.empty() && (unsigned char)nick.front() <= 0x20)
		nick.erase(nick.begin());
	while (!nick.empty() && (unsigned char)nick.back()  <= 0x20)
		nick.pop_back();
	if (nick.size() > 24) nick.resize(24);
	if (nick.empty()) return;

	State& st = GetState();
	st.prefs.nickname    = nick;
	st.prefs.isAnonymous = false;
	SaveToRegistry();
	MarkFirstRunComplete();
	g_ATLCNetplay("deeplink: nickname submitted (\"%s\") — first-run "
		"complete, advancing", nick.c_str());

	// Re-enter the state machine from Idle on the next tick — it'll
	// either advance to WaitingForFirmware (if firmware is missing)
	// or straight to FetchInFlight.
	g_phase = Phase::Idle;
}

void CancelDeepLink() {
	{
		std::lock_guard<std::mutex> lk(g_deepLinkMu);
		if (g_pendingSessionId.empty() && g_phase == Phase::Done) {
			// Already cancelled / completed — silent no-op so the
			// log doesn't get noisy if the renderer pings Cancel
			// twice (window-close + button click).
			return;
		}
	}
	g_ATLCNetplay("deeplink: cancelled by user");
	ClearPendingDeepLink();
	g_phase = Phase::Done;
	g_firmwareKicked = false;
	// Disarm the in-flight tag — see kDeepLinkTagNone for why.  The
	// late lobby response (if it arrives) drops in
	// OnDeepLinkLobbyResult's first guard.
	g_fetchTag = kDeepLinkTagNone;
	State& st = GetState();
	if (st.screen == Screen::DeepLinkPrep)
		Navigate(Screen::Closed);
}

void RetryDeepLinkFirmware() {
#if defined(__EMSCRIPTEN__)
	g_ATLCNetplay("deeplink: user requested firmware retry — "
		"resetting bootstrap state");
	ATWasmResetFirstRun();
	g_firmwareKicked = false;
	// State machine drops back to Idle so the next DriveDeepLinkJoin
	// call re-checks ATWasmGetFirstRunState (which is now 0 after
	// reset), kicks the bootstrap, and parks us back on the
	// "Downloading…" UI.
	if (g_phase == Phase::FirmwareFailed)
		g_phase = Phase::Idle;
#endif
}

} // namespace ATNetplayUI
