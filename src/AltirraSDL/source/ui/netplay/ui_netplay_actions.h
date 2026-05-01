//	AltirraSDL - Online Play action helpers (session lifecycle)
//
//	Imperative layer that sits between the UI screens and the
//	Coordinator + LobbyWorker.  The user-facing list of "My Hosted
//	Games" (vector<HostedGame> in State) is driven through the
//	four public entry points here:
//
//	  EnableHostedGame(id)   — add offer to the lobby + bind a UDP port
//	  DisableHostedGame(id)  — unlist + tear down the coordinator
//	  RemoveHostedGame(id)   — DisableHostedGame + drop from State::hostedGames
//	  ReconcileHostedGames() — idempotent; brings every offer in line
//	                       with (activity, enabled, state) tuple;
//	                       called from the UI Poll each frame
//
//	Plus join + sim-event hooks unchanged from v1.

#pragma once

#include <cstdint>
#include <string>

#include "ui_netplay_state.h"  // MachineConfig

class ATGameLibrary;

namespace ATNetplayUI {

// Shared ATGameLibrary instance used by the Add-Offer pickers in both
// Desktop and Gaming Mode.  Lazy-initialised on first call; no unload
// on shutdown (the library's life is scoped to the app).
ATGameLibrary& LibrarySingleton();


// --- Per-offer host lifecycle -------------------------------------------

// Post to the lobby + bind UDP + start a coordinator for this offer.
// If activity != Idle, the offer is marked Suspended instead.  Idempotent.
void EnableHostedGame(const std::string& gameId);

// Remove from lobby (Delete), tear down coordinator, drop port.  Does
// NOT remove the offer from State::hostedGames — the user can re-enable.
// Idempotent.
void DisableHostedGame(const std::string& gameId);

// Disable + erase from State::hostedGames.  Saves the registry.
void RemoveHostedGame(const std::string& gameId);

// Called once per frame from ATNetplayUI_Poll.  Synchronises the
// runtime state of every offer with its (activity, enabled) tuple:
//   - If activity == Idle AND enabled: ensure offer is Listed.
//   - If activity != Idle OR !enabled: ensure offer is Suspended
//     (unless it's the one in session).
//   - Consume coordinator phase transitions (reading HostPhase) and
//     update the offer's local HostedGameState + flag UserActivity changes.
//   - Sends Heartbeats every 30 s per Listed/Playing offer.
void ReconcileHostedGames(uint64_t nowMs);

// --- Activity tracking --------------------------------------------------

// Called by the simulator event callback on ColdReset / EXELoad /
// StateLoaded / GameBrowser_OnBootedGame.  Transitions activity from
// Idle → PlayingLocal.  No-op if already PlayingLocal / InSession.
void ActivityTrack_OnLocalPlayStart();

// Called when the user explicitly returns to no-booted-game state.
// Today there's no such event from the sim, so ReconcileHostedGames also
// computes it implicitly from "no coordinator in session AND no
// recent EXELoad".  Exposed for future hooks.
void ActivityTrack_OnLocalPlayStop();

// --- Join (single-session) ----------------------------------------------

void StartJoiningAction();

// Read the game file bytes from disk and hand them to the host
// coordinator for the given offer id.  Called from the
// kATDeferred_NetplayHostSnapshot deferred action on the UI thread.
// No-op if the id is unknown.
void SubmitHostGameFileForGame(const char *gameId);

// --- Session lifecycle -------------------------------------------------
//
// A hosted session is non-destructive to the user's normal emulator
// setup.  When a peer connects, the host calls
// ATNetplayProfile::BeginSession (see netplay/netplay_profile.h),
// which:
//   1. Snapshots the user's current profile id to a crash-recovery
//      lock file.
//   2. ATSaveSettings(All) — flushes the user's live state into
//      their saved profile.
//   3. Switches the active profile to the canonical Netplay Session
//      Profile and applies its fixed deterministic baseline +
//      per-game overrides directly to the live simulator.
//   4. SetLockedRandomSeed + ColdReset.
// The caller (kATDeferred_NetplayHostBoot / NetplayJoinerApply)
// then UnloadAlls + Loads the hosted game image.
// When the session ends (clean Bye, peer disconnect, snapshot apply
// failure, app shutdown, process crash), ATNetplayProfile::EndSession
// switches the active profile back to the user's id and ATLoadSettings
// restores their exact saved state.

// --- Back-compat shims for the old single-session Host Setup screen ----
//
// "Start Hosting" on the old screen now means: take the cart in
// st.session.pendingCart{Path,Name}, find or create a HostedGame that
// matches, enable it, and navigate to My Hosted Games.
void StartHostingAction();
// "Cancel" / "End Session" — disables whichever offer is currently in
// session, and stops any join.
void StopHostingAction();

// --- Lobby v2 pre-flight ------------------------------------------------
//
// Compute compatibility of a lobby session against the joiner's
// installed firmware.  The lobby publishes the host's kernel + BASIC
// CRC32s so we can colour-code the Browser without having to round-trip
// a handshake.

enum class JoinCompat : uint8_t {
	Unknown,        // host pre-dates v2 schema (no CRCs published)
	Compatible,     // both required CRCs resolve in our firmware mgr
	MissingKernel,  // OS ROM not installed locally
	MissingBasic,   // BASIC ROM not installed locally
	MissingBoth,    // both OS and BASIC ROMs not installed locally
};

// outMissingCRCHex (optional, capacity >= 9) receives the offending
// CRC as 8-char uppercase hex when the result is MissingKernel/Basic.
JoinCompat CheckJoinCompat(const std::string& kernelCRC32Hex,
                           const std::string& basicCRC32Hex,
                           char *outMissingCRCHex = nullptr);

// Post a one-shot lobby Delete for an explicit session id + token,
// resolving the lobby endpoint from `section` (matched against
// lobby.ini's enabled sections).  Used to clean up orphaned sessions
// that are not in any HostedGame's lobbyRegistrations — e.g. when a
// quick Enable/Disable/Enable race lets a stale Create's response
// land after a fresh Create's response, the de-dup in the Create
// handler must Delete the displaced session instead of dropping its
// id+token on the floor (otherwise the lobby keeps it listed for its
// TTL window, advertising a UDP port no coord listens on).  Safe to
// call with empty inputs (no-op).  Fire-and-forget — failure here
// just means the lobby's TTL eventually cleans up.
void PostLobbyDeleteForSession(const std::string& section,
                               const std::string& sessionId,
                               const std::string& token);

// Synchronous best-effort lobby delete for app-shutdown.  Used by
// ATNetplayUI_Shutdown right before the worker thread is stopped:
// the worker queue is about to be cleared, so any pending async
// Deletes posted via PostLobbyDelete would be lost.  This call
// bypasses the worker and invokes LobbyClient::Delete directly
// with a tight per-call timeout so the app exit isn't held up
// when the lobby is unreachable.  Iterates every offer's
// `lobbyRegistrations`; clears the vector after.
void SyncDeleteAllRegistrationsForShutdown();

} // namespace ATNetplayUI
