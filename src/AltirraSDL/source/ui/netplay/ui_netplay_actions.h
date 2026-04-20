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

// --- Machine-preset apply + session restore-point -----------------------
//
// A hosted session is non-destructive to the user's normal emulator
// setup.  When a peer connects, the host:
//   1. Saves the live sim state via SaveSessionRestorePoint() (a full
//      in-memory savestate).  If this fails, the session is refused.
//   2. Applies the MachineConfig (hardware, memory, video, firmware)
//      via ApplyMachineConfig().
//   3. Loads the hosted game image + ColdReset + Resume.
// When the session ends (clean or error), RestoreSessionRestorePoint()
// puts the live sim back to exactly how the user left it — menu state,
// mounted media, everything.  The Altirra settings.ini on disk is NEVER
// modified by this flow.

// Save the current simulator state into an in-memory snapshot owned by
// the actions TU.  Returns false if CreateSnapshot fails.  Clears any
// previously-held restore point first — nested sessions are impossible
// by the activity state machine, but be defensive.
bool SaveSessionRestorePoint();

// Apply the in-memory snapshot (if any) back to the simulator.  Clears
// the snapshot on exit so a double-call is a no-op.  Errors are logged
// and suppressed — the user's state may be partially restored, but we
// don't want to leave them with a modal error dialog when the session
// has simply ended.
void RestoreSessionRestorePoint();

// True when a restore point is currently held.  Used by the reconcile
// loop to detect session-end edges and trigger RestoreSessionRestorePoint.
bool HasSessionRestorePoint();

// Apply a MachineConfig to the live simulator (hardware mode, memory
// mode, video standard, CPU mode, BASIC enable, SIO patch, firmware
// resolved by CRC32).  Does NOT Load a game image; that's the
// caller's job (NetplayHostBoot).  Returns empty string on success;
// on failure, a short human reason (missing firmware, unsupported
// combination).  Settings are always read from / written to the
// live simulator, never to settings.ini on disk.
std::string ApplyMachineConfig(const MachineConfig& cfg);

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
};

// outMissingCRCHex (optional, capacity >= 9) receives the offending
// CRC as 8-char uppercase hex when the result is MissingKernel/Basic.
JoinCompat CheckJoinCompat(const std::string& kernelCRC32Hex,
                           const std::string& basicCRC32Hex,
                           char *outMissingCRCHex = nullptr);

} // namespace ATNetplayUI
