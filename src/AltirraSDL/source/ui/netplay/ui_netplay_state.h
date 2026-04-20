//	AltirraSDL - Online Play UI state (shared across desktop + gaming mode)
//
//	Phase 11 of the netplay plan.  Both the Desktop menu-bar flow and
//	the Gaming-Mode full-screen flow drive the same Netplay session,
//	so the UI state lives in a singleton here and both modes read/
//	write it.  That lets a user switch between modes mid-session
//	without losing their place (e.g. flip to Desktop to configure
//	audio, then back to Gaming Mode to keep playing).
//
//	Content:
//	  - Screen enum + active-screen stack (per mode)
//	  - Persistent preferences (nickname, accept mode, notif toggles)
//	  - Ephemeral session state (selected lobby row, last-error text)
//	  - Lobby browser cache (merged list from all configured lobbies)
//	  - Advisory host-a-game flow state (chosen cart, entry code)
//
//	Persistent fields are saved to the registry under "Netplay".  The
//	transient fields are cleared on Shutdown() so a re-launch doesn't
//	inherit leftover dialog state.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "netplay/lobby_client.h"
#include "netplay/platform_notify.h"

#include "constants.h"  // ATHardwareMode, ATMemoryMode, ATVideoStandard
#include "cpu.h"        // ATCPUMode

namespace ATNetplay { struct LobbyEntry; }

namespace ATNetplayUI {

// Which screen the user is currently looking at.  The per-mode
// renderers consult this to decide what to draw; they never mutate it
// directly — use Navigate() / Back() so the back stack stays in sync
// with the focus-return hints and the hardware-back button on
// Android.
enum class Screen {
	// Transient: the user has no netplay UI open.  The main menu (or
	// hamburger in Gaming Mode) is the only way to pop us to
	// Nickname/Browser.
	Closed,

	Nickname,         // First-time handle prompt
	Browser,          // Online sessions grid (browse peers)
	MyHostedGames,    // List of this user's own hosted hostedGames
	AddGame,         // Add-an-offer picker (Library / File)
	HostSetup,        // Pick cart → visibility → entry code (per-offer edit)
	JoinPrompt,       // Enter entry code (private session)
	JoinConfirm,      // Confirm ROM / TOS before joining public
	Waiting,          // Host is waiting for a joiner; joiner is waiting for snapshot
	Prefs,            // Preferences → Netplay
	DesyncReport,     // Post-desync "something went wrong" card
	Error,            // Generic error sheet (LastError())
};

// -----------------------------------------------------------------------
// Hosted games — the list of games a user has queued up for hosting.
// Each entry translates into one lobby listing + one UDP port when
// Enabled and the user's activity allows it.  See the netplay design
// plan (NETPLAY_DESIGN_PLAN.md) for the activity-driven state
// transitions.
// -----------------------------------------------------------------------

enum class HostedGameState : uint8_t {
	Draft,        // configured locally, not posted to lobby
	Listed,       // posted; UDP port bound; waiting for peer
	Handshaking,  // peer connected; loading game; snapshot in flight
	Playing,      // coordinator in Lockstepping
	Suspended,    // unlisted because the user is busy elsewhere
	Failed,       // last attempt failed; held locally so user can retry
};

// Full machine configuration per hosted game.  Serialised to the
// lobby Welcome packet so the joiner can reproduce the exact same
// hardware the host cold-boots.  Firmware is identified by CRC32 of
// the loaded ROM bytes; both peers must have a matching-CRC entry in
// their ATFirmwareManager or the joiner refuses.
struct MachineConfig {
	ATHardwareMode  hardwareMode    = kATHardwareMode_800XL;
	ATMemoryMode    memoryMode      = kATMemoryMode_320K;
	ATVideoStandard videoStandard   = kATVideoStandard_NTSC;
	ATCPUMode       cpuMode         = kATCPUMode_6502;
	bool            basicEnabled    = false;
	bool            sioPatchEnabled = true;   // full-speed SIO

	// CRC32 of the installed firmware ROM bytes; 0 = unset / not
	// required.  Captured on the host from the currently-selected
	// firmware at Add-Game time; verified on the joiner against
	// ATFirmwareManager::GetFirmwareByRefString(L"[XXXXXXXX]", ...).
	uint32_t        kernelCRC32     = 0;
	uint32_t        basicCRC32      = 0;
};

struct HostedGame {
	// Persistent fields --------------------------------------------------
	std::string id;                // stable local id (UUID-ish)
	std::string gamePath;          // UTF-8 absolute path to image
	std::string gameName;          // basename or library display name
	std::string cartArtHash;       // optional, hex
	bool        isPrivate = false;
	std::string entryCode;         // cleartext; hashed at StartHost time
	bool        enabled   = true;  // user toggle; when false stays Draft

	// Full machine configuration applied before cold-booting the
	// game for a joined peer.  Captured from the current sim at
	// Add-Game time unless the user picks explicit values.
	MachineConfig config;

	// Runtime fields (not persisted) ------------------------------------
	HostedGameState  state          = HostedGameState::Draft;

	// Per-lobby registration.  One entry per enabled HTTP lobby the
	// offer was announced to.  Keyed by the lobby's section name from
	// lobby.ini so heartbeats and deletes can target each lobby
	// independently — one slow lobby doesn't block the others, and
	// joiners from any lobby can still find the game.
	struct LobbyRegistration {
		std::string section;     // lobby.ini [section] key
		std::string sessionId;   // returned by POST /v1/session
		std::string token;       // required for heartbeat/delete
	};
	std::vector<LobbyRegistration> lobbyRegistrations;

	uint16_t    boundPort      = 0;
	uint64_t    lastHeartbeatMs = 0;
	std::string lastError;

	// Snapshot queuing — set once per session to avoid re-queueing the
	// boot+serialize work on every ReconcileHostedGames tick.  Cleared when
	// the coordinator goes terminal.
	bool        snapshotQueued = false;

	// Previous-tick phase (glue int) so we can detect edges like
	// WaitingForJoiner → Handshaking and fire notifications / start
	// the boot flow exactly once per session.
	uint8_t     lastPhase = 0;
};

// One-line human-readable summary of a MachineConfig — used in the
// Host Games row subtitle.  Returns static storage; caller must copy
// before calling again.  Format: "800XL · 320K · NTSC · BASIC off".
const char *MachineConfigSummary(const MachineConfig& c);

// Snapshot the currently-running simulator's config as a MachineConfig,
// with firmware CRC32s computed from the loaded ROM bytes.
MachineConfig CaptureCurrentMachineConfig();

// CRC32 of the firmware ROM bytes for the given firmware id.  Returns
// 0 if the id is unknown or the firmware can't be loaded.
uint32_t ComputeFirmwareCRC32(uint64_t firmwareId);

// Translate a raw http_minimal / lobby_client error string + HTTP
// status into user-friendly text.  Raw "recv() failed", "HTTP 429",
// "getaddrinfo: Name or service not known" etc. become the kind of
// sentence we want on a tooltip, not a stderr log line.
std::string FriendlyLobbyError(const std::string& rawError, int httpStatus);

// Load the user's lobby configuration from
// $configDir/lobby.ini, writing the built-in defaults the first
// time (so users can freely edit the URL, add mirrors, etc.).
// Falls back to in-memory defaults if the file can't be read/parsed.
// Cached after first call — call ReloadLobbyConfig to re-read.
const std::vector<ATNetplay::LobbyEntry>& GetConfiguredLobbies();

// Force a re-read of lobby.ini.  Call if the user edited the file
// out-of-band and wants the change to take effect without restart.
void ReloadLobbyConfig();

// High-level "is the user busy" flag that drives the suspension of
// every hosted game the user didn't explicitly start.
enum class UserActivity : uint8_t {
	Idle,          // no game booted, no netplay session active
	PlayingLocal,  // user booted a game for single-player
	InSession,     // a peer joined one of our hostedGames OR we joined someone
};

// Host-side "accept incoming join" policy — mirrors §9 of the design
// document.  Auto-accept is the happy path; prompt-me pops a modal
// with a 20 s timer so an AFK host doesn't block the joiner forever.
enum class AcceptMode : uint8_t {
	AutoAccept,
	PromptMe,
	ReviewEach,   // Same as PromptMe but without the 20 s auto-decline
};

struct Prefs {
	// Persistent identity.
	std::string nickname;          // "" ⇒ nickname prompt pending
	bool        isAnonymous = false;  // true ⇒ random nickname each session

	// Accept mode for incoming join requests.
	AcceptMode  acceptMode = AcceptMode::AutoAccept;

	// Notification triple — each toggle matches a field on
	// ATNetplay::NotifyPrefs that platform_notify.cpp consumes.
	ATNetplay::NotifyPrefs notif;

	// Input delay defaults (frames).  LAN peers are low-latency; the
	// Internet default absorbs an extra frame of jitter.
	int defaultInputDelayLan      = 3;
	int defaultInputDelayInternet = 4;

	// Whether the waiting panel should grab keyboard focus when a
	// notification arrives (some users prefer to keep typing).  The
	// desktop flash + system notification still fire regardless.
	bool focusOnAttention = false;

	// Last entry code the user typed into Host-a-Game.  We remember
	// this so a serial host doesn't have to re-type the same code
	// every session.  Joiners never persist codes.
	std::string lastEntryCode;

	// Advanced manual IP fallback.  Exposed only in Preferences when
	// no lobby is reachable; normal path is the browser.
	bool        advancedManualIp = false;

	// Last MachineConfig the user saved from the Add Game dialog, so
	// serial hosts don't have to re-pick hardware every time.  Not
	// serialised to the registry — rebuilt on the next Add Game from
	// the first hosted game's config, or from the live sim.
	MachineConfig lastAddConfig;

	// Whether the in-session overlay HUD (LIVE / FRAME / Peer / Disconnect)
	// is rendered.  Some users want a clean screen during recordings;
	// others want the diagnostic always visible.  Defaults ON — new
	// users need the feedback to understand what netplay is doing.
	bool showSessionHUD = true;
};

// Ephemeral state — cleared on Shutdown().
struct Session {
	// Host-a-Game flow: the chosen game-library entry.  UTF-8 path.
	std::string pendingCartPath;
	std::string pendingCartName;
	std::string pendingCartArtHash;  // hex; matches lobby.cartArtHash

	// Host visibility choice before BeginHost().
	bool        hostingPrivate = false;
	std::string hostingEntryCode;  // user-typed; hashed before sending

	// Join flow: the browser row the user clicked, copied out so the
	// lobby list refresh doesn't yank it from underneath the dialog.
	ATNetplay::LobbySession joinTarget;
	std::string             joinEntryCode;

	// Error string to display on the Error screen.  Set from failed
	// transitions; cleared when the sheet is dismissed.
	std::string lastError;

	// Desync dump path (desync screen) — absolute UTF-8 path.
	std::string desyncDumpPath;

	// Monotonic ms timestamp of the last notification so we don't spam
	// the user with chimes if peers bounce on and off quickly.
	uint64_t    lastNotifyMs = 0;

	// -- Active hosting state ---------------------------------------
	// Populated when the host has actually called StartHost() and
	// Create()'d a lobby entry.  Empty when we are idle or joining.
	std::string    lobbySessionId;
	std::string    lobbyToken;
	uint16_t       boundPort = 0;        // local UDP port the host is on
	uint64_t       lastHeartbeatMs = 0;  // monotonic ms of last OK heartbeat
	ATNetplay::LobbyEndpoint lobbyEndpoint;  // which lobby we announced to
};

struct Browser {
	std::vector<ATNetplay::LobbySession> items;

	// Last-fetched-at (monotonic ms); 0 ⇒ never fetched.
	uint64_t    lastFetchMs = 0;

	// The one-shot refresh flag.  Set by the Refresh button or the
	// 10 s auto-refresh timer; consumed by the lobby worker.
	bool        refreshRequested = true;

	// Non-empty while a refresh is in flight.  The UI shows a spinner.
	bool        refreshInFlight = false;

	// Exponential backoff on List failures.  Cleared on success.
	// Doubles on every failure (capped) so a lobby that 429s or
	// times out doesn't spin-retry at main-loop speed.
	uint64_t    nextRetryMs = 0;
	uint32_t    consecutiveFailures = 0;

	// Human-readable status line ("12 sessions" / "Lobby unreachable").
	std::string statusLine;

	// Selected row index (-1 ⇒ none).  Keyboard/gamepad nav writes
	// this; touch taps ignore it and call Join() directly.
	int         selectedIdx = -1;
};

// Cross-window lobby reachability signal.  Every lobby HTTP op
// (List, Create, Heartbeat, Delete) updates this so both the Browse
// and Host Games windows can show a consistent "lobby is up" /
// "lobby is down" indicator without independent polling — the free
// Oracle tier has a small req/min budget and we must not flood it.
struct LobbyHealth {
	// Wall-clock ms of the last successful lobby response.  0 = never.
	uint64_t    lastOkMs = 0;
	// Wall-clock ms of the last failed lobby response.  0 = never.
	uint64_t    lastFailMs = 0;
	// Friendly error text from the last failure.  Cleared on success.
	std::string lastError;
	// HTTP status code from the last response (0 = network error).
	int         lastStatus = 0;
};

struct State {
	Screen      screen      = Screen::Closed;
	Prefs       prefs;
	Session     session;
	Browser     browser;
	LobbyHealth lobbyHealth;

	// My Hosted Games — the user's advertised hostedGames.  Persisted across
	// runs; runtime fields (state/port/tokens) reset each launch.
	std::vector<HostedGame> hostedGames;

	// Cap on the number of simultaneous hostedGames.  Lobby rate limit is
	// 60 req/min; heartbeating N hostedGames every 30 s puts us at
	// 2N req/min.  Cap at 5 → 10 req/min, well under budget.
	static constexpr size_t kMaxHostedGames = 5;

	// The offer currently being edited / added (id string), so the
	// HostSetup screen knows which one to mutate.  Empty when adding
	// a new one.
	std::string editingGameId;

	// User activity — drives the Listed ↔ Suspended transitions.  Read
	// by ReconcileHostedGames() each frame.
	UserActivity activity = UserActivity::Idle;

	// Back-stack so the hardware back button (Android) / Escape key /
	// gamepad B button all pop to the previous screen instead of
	// dumping the user to the emulator.
	std::vector<Screen> backStack;

	// True while a session is active in the netplay coordinator.  The
	// UI uses this to disable "Host a Game" / "Join" while the user is
	// already in a session, and to show the End-Session path.
	bool sessionActive = false;
};

// Helper: find offer by id in State::hostedGames.  Returns nullptr if missing.
HostedGame* FindHostedGame(const std::string& id);

// Generate a new local offer id (16 hex chars).  Used when the user adds
// an offer; not a UUID, just unique per install.
std::string GenerateHostedGameId();

// Accessors.  The state lives in a translation-unit-static singleton
// in ui_netplay_state.cpp; tests can reset it via Shutdown().
State& GetState();

// One-time initialisation — loads Prefs from the registry and
// generates an anonymous nickname if the user's last session was anon.
// Safe to call more than once.
void Initialize();

// Save Prefs to the registry (Netplay/Handle, Netplay/AcceptMode, etc.).
// Called from Shutdown() and every time a pref changes via the
// Preferences sheet.
void SaveToRegistry();

// Release transient state.  Called on app teardown and from tests.
void Shutdown();

// Navigate forward — pushes the current screen to the back stack.
// Navigate(Screen::Closed) clears the stack.
void Navigate(Screen next);

// Pop one step; returns true if the stack was non-empty.  When the
// stack is empty, caller should fall through to the outer (menu / HUD).
bool Back();

// Fresh random nickname (eight char, pronounceable) used when
// Prefs::isAnonymous is true.  Each session gets a new one.
std::string GenerateAnonymousNickname();

// Returns an error-free display nickname: if nickname is empty, falls
// back to the anon generator.  The rendered name is cached for the
// session so the lobby never sees a flicker.
const std::string& ResolvedNickname();

} // namespace ATNetplayUI
