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
#include "netplay/lobby_protocol.h"   // ATLobby::kMaxHostedGamesPerHost
#include "netplay/netplay_glue.h"     // ATNetplayGlue::PeerPath
#include "netplay/platform_notify.h"

#include "constants.h"  // ATHardwareMode, ATMemoryMode, ATVideoStandard

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

	DeepLinkPrep,     // One-click join (?s=...) preflight: nickname,
	                  // firmware download, lobby fetch — in that
	                  // sequence; takes the user straight from URL
	                  // click to in-game.  Driven by the deep-link
	                  // state machine in ui_netplay_deeplink.cpp.
	Nickname,         // First-time handle prompt
	OnlinePlayHub,    // Hub: Host Games / Browse / Preferences cards
	Browser,          // Online sessions grid (browse peers)
	MyHostedGames,    // List of this user's own hosted hostedGames
	AddGame,         // Add-an-offer picker (Library / File)
	LibraryPicker,    // Full-screen Game Library picker (drives AddGame)
	HostSetup,        // Pick cart → visibility → entry code (per-offer edit)
	JoinPrompt,       // Enter entry code (private session)
	JoinConfirm,      // Confirm ROM / TOS before joining public
	Waiting,          // Host is waiting for a joiner; joiner is waiting for snapshot
	AcceptJoinPrompt, // Host's "<peer> wants to join <game>" Allow/Deny modal
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
	Off,          // configured locally, not posted to lobby
	Open,         // posted; UDP port bound; waiting for peer
	Handshaking,  // peer connected; loading game; snapshot in flight
	Playing,      // coordinator in Lockstepping
	Paused,       // unlisted because the user is busy elsewhere
	Failed,       // last attempt failed; held locally so user can retry
};

// Per-game machine configuration — the 6 variables a host can pick
// per hosted game.  Everything else is pinned by the canonical
// Netplay Session Profile (see ATNetplayProfile in
// netplay/netplay_profile.h): both peers force a fixed
// deterministic configuration for the duration of a session, so
// CPU model, SIO patch flag, accuracy options, attached devices,
// memory clear pattern, etc. are not negotiable per session.
//
// Firmware is identified by CRC32 of the loaded ROM bytes; both
// peers must have a matching-CRC entry in their ATFirmwareManager
// or the joiner refuses to apply.  CRC32 = 0 means "use the
// canonical default for this hardware mode" (e.g. AltirraOS-XL for
// 800XL).
struct MachineConfig {
	ATHardwareMode  hardwareMode    = kATHardwareMode_800XL;
	ATMemoryMode    memoryMode      = kATMemoryMode_320K;
	ATVideoStandard videoStandard   = kATVideoStandard_NTSC;
	bool            basicEnabled    = false;

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
	bool        enabled   = true;  // user toggle; when false stays Off

	// Full machine configuration applied before cold-booting the
	// game for a joined peer.  Captured from the current sim at
	// Add-Game time unless the user picks explicit values.
	MachineConfig config;

	// Cached CRC32 of the game-file bytes, keyed by the outer-file
	// mtime tick stamp.  Zero stamp means "not cached yet".  Mutable
	// because it's a transparent cache — recomputed when the outer
	// file changes on disk, invisible to logical state.  Persisted so
	// we don't re-decompress on every launch / host-enable.
	mutable uint32_t gameFileCRC32 = 0;
	mutable uint64_t gameFileStamp = 0;

	// Runtime fields (not persisted) ------------------------------------
	HostedGameState  state          = HostedGameState::Off;

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

	// Lobby-observed session liveness — populated from the /v1/sessions
	// poll response so the My Hosted Games list can render a live
	// "N/M players · up 5m" subtitle instead of the bare handle.  All
	// three are best-effort; zero means "unknown / not reported yet".
	uint32_t    currentPlayers = 0;
	uint32_t    maxPlayers     = 0;
	uint64_t    hostStartedAtMs = 0;

	// NAT-PMP / PCP router-assisted port mapping acquired at Create
	// time.  Stored so the session-Delete path can release the
	// mapping politely instead of waiting for the router's lease
	// (up to 1 hour) to expire.  Empty protocol string = no
	// mapping was acquired (router didn't speak NAT-PMP or the
	// request timed out).  The fields mirror ATNetplay::PortMapping
	// one-for-one — we store them flat to avoid pulling the header
	// into this TU for a couple of fields.
	std::string natPmpProtocol;      // "NAT-PMP" | "PCP" | ""
	std::string natPmpExternalIp;
	uint16_t    natPmpExternalPort = 0;
	uint16_t    natPmpInternalPort = 0;
	uint32_t    natPmpLifetimeSec  = 0;
	uint64_t    natPmpAcquiredMs   = 0;
	// True while a PortMapRefresh request is in flight on the worker
	// thread.  Prevents the per-tick refresh check from spamming the
	// worker with duplicate requests before the first one completes.
	bool        natPmpRefreshInFlight = false;
	// Earliest monotonic-ms at which a failed refresh may retry.  The
	// natural trigger is `natPmpAcquiredMs + lifetime/2` — on the
	// happy path retryAfterMs stays 0 and that is the only gate; on
	// failure we set it to `now + 5 min` so a broken/unreachable
	// router doesn't cause us to re-post every main-loop tick until
	// the original mapping finally expires.
	uint64_t    natPmpRetryAfterMs = 0;

	// Coord lifecycle generation — bumped on every StartCoord /
	// StopCoord transition so the lobby Create response handler can
	// detect stale responses.  Each lobby Create request snapshots
	// the current value into LobbyRequest::coordGen; when the
	// response comes back, the handler compares the echoed value
	// against this field and treats any mismatch as orphan.
	//
	// Why this matters: a host that bound ephemeral UDP port X and
	// posted a Create can be torn down before the Create response
	// lands (Disable+Enable race, reconcile recycle on a coord that
	// went terminal, app shutdown).  Without gen gating, the
	// response registers a session pointing at port X which no coord
	// listens on; the lobby keeps it visible for its TTL window
	// (~90s) and joiners that pick it sit in "Connecting…" forever
	// while the host never sees the join.  With gen gating, the
	// stale response triggers an immediate lobby Delete + skips the
	// local register step, collapsing the orphan window from
	// minutes to one HTTP round-trip.
	uint32_t    coordGen = 0;

	// Snapshot queuing — set once per session to avoid re-queueing the
	// boot+serialize work on every ReconcileHostedGames tick.  Cleared when
	// the coordinator goes terminal.
	bool        snapshotQueued = false;

	// True while a lobby Create POST is in flight on the worker.  Set
	// by PostLobbyCreate, cleared when the response (success or
	// failure) is received in ATNetplayUI_Poll.  Gates
	// StartCoordForHostedGame so the reconcile loop doesn't fire a
	// fresh Create on every frame while the previous one is still in
	// flight — without this the WASM host would post 8+ Creates in
	// 60 ms before the first response arrived (each tick sees the
	// coord still missing and re-fires), DDoS-ing the lobby.
	bool        createInFlight = false;
	// Earliest monotonic-ms at which a failed Create may be retried.
	// On a Create failure (4xx/5xx, network error) the response
	// handler arms this to `now + 30 s` so a misconfigured offer or a
	// down lobby doesn't trigger a new Create on every reconcile tick.
	// Zero means "no backoff".
	uint64_t    createRetryAfterMs = 0;

	// Previous-tick phase (glue int) so we can detect edges like
	// WaitingForJoiner → Handshaking and fire notifications / start
	// the boot flow exactly once per session.
	uint8_t     lastPhase = 0;
};

// One-line human-readable summary of a MachineConfig — used in the
// Host Games row subtitle.  Returns static storage; caller must copy
// before calling again.  Format: "800XL · 320K · NTSC · BASIC off".
const char *MachineConfigSummary(const MachineConfig& c);

// Short wire-format labels for the three machine enums — also used on
// the host side to populate the lobby's hardwareMode / videoStandard /
// memoryMode fields so joiners can render the spec row without having
// to map enum values themselves.  Returns static / literal storage.
const char *HardwareModeShort (ATHardwareMode  m);
const char *MemoryModeShort   (ATMemoryMode    m);
const char *VideoStandardShort(ATVideoStandard v);

// ---------------------------------------------------------------------
// Browser / Host Games spec line — shared formatting for the
// "hardware | video | memory | OS | BASIC" sub-row rendered below the
// cart name in both the joiner's Browser and the host's own Hosted
// Games list.
//
// The builder normalises raw wire / config values into display tokens:
//   - CRC==0 or empty-hex → "default OS" / "BASIC off".
//   - CRC present + FirmwareNameForCRC resolves → friendly name.
//   - CRC present + lookup fails → "[XXXXXXXX]" (raw hex).
//   - Empty video/memory (old host) → "?" (doesn't collapse the column,
//     keeps vertical alignment across rows).
//
// `missing` on a token marks that specific slot as "joiner doesn't have
// this firmware installed" so the row renderer can colour just that
// token red.  It is only ever set on the session-side builder, and only
// when the caller's JoinCompat agrees (MissingKernel for OS,
// MissingBasic for BASIC).  The host-own builder never marks tokens as
// missing — hosts by definition have their own firmware.
// ---------------------------------------------------------------------

// Forward-decl so we don't have to #include "ui_netplay_actions.h"
// here and risk the header cycle (actions.h already pulls us in).
enum class JoinCompat : uint8_t;

struct SpecLineToken {
	std::string text;
	bool        missing = false;
};

struct SpecLine {
	// Always five tokens in fixed order:
	//   [0] hardware  (e.g. "130XE")
	//   [1] video     (e.g. "PAL")
	//   [2] memory    (e.g. "320K")
	//   [3] OS        (name / "default OS" / "[hex]")
	//   [4] BASIC     (name / "BASIC off" / "[hex]")
	std::vector<SpecLineToken> tokens;
	bool hasMissingFirmware = false;
};

SpecLine BuildSpecLineFromSession(const ATNetplay::LobbySession& s,
                                  JoinCompat compat);
SpecLine BuildSpecLineFromConfig (const MachineConfig& c);

// Concatenate a SpecLine's tokens with " | " separators.  Callers that
// render into a single-colour widget (mobile muted text, debug log
// lines) use this; callers that want per-token colouring walk the
// tokens directly.
std::string SpecLineJoin(const SpecLine& sl);

// Snapshot the currently-running simulator's config as a MachineConfig,
// with firmware CRC32s computed from the loaded ROM bytes.
MachineConfig CaptureCurrentMachineConfig();

// First-time Add-Game-to-Host pre-fill: promotes a "(Altirra default)"
// kernel/BASIC selection (CRC 0) to the user's INSTALLED default ROMs
// (resolved via ATFirmwareManager), and forces basicEnabled = false.
// Idempotent: non-zero CRCs are left alone, so once the user has
// customised a hosted game's config it is never re-overwritten.
void PrefillHostMachineConfigDefaults(MachineConfig &cfg);

// CRC32 of the firmware ROM bytes for the given firmware id.  Returns
// 0 if the id is unknown or the firmware can't be loaded.
uint32_t ComputeFirmwareCRC32(uint64_t firmwareId);

// Resolve a firmware CRC32 back to a short human-readable name
// ("AltirraOS-XL", "Original XL", ...) by scanning the installed
// firmware list.  Returns "" for crc=0 (meaning the offer doesn't
// pin a specific ROM), or "Unknown" when no local firmware matches.
// Thread-affine: callers on the UI thread only.
// Resolve a 32-bit CRC to a user-readable firmware name.  Prefers a
// locally-installed ROM (whatever the user configured in Firmware
// Manager); falls back to Altirra's built-in ATKnownFirmware table so
// ROMs the user hasn't installed (yet) still render as
// "Atari 400/800 OS-B NTSC" instead of "[0e86d61d]".  Returns
// "Unknown" when the CRC is in neither place.
//
// `outInstalledLocally` (optional): set to true iff the returned name
// came from the user's installed firmware list; false for the
// known-table fallback or "Unknown".  Browser rows use this to paint
// the token red when the host needs a ROM the user doesn't have.
const char *FirmwareNameForCRC(uint32_t crc,
                               bool *outInstalledLocally = nullptr);

// Stable fingerprint of a HostedGame — combines the image path with
// every joiner-visible MachineConfig knob (hardware mode, memory,
// video standard, CPU mode, BASIC enable, SIO patch, kernel/BASIC
// CRCs).  Used to reject duplicate Add-Game entries.  Deliberately
// excludes Name, privacy, and entry code — two listings of the same
// game (one public, one private) are a legitimate use case.
std::string HostedGameSignature(const std::string& path,
                                const MachineConfig& c);

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

// Incoming-join policy: always prompt the host with a modal carrying
// a 20 s auto-decline timer (after which the join is rejected and the
// joiner gets a clean "host declined" message).  Auto-accept used to
// be an option but was removed — a peer should never be able to slip
// onto the user's machine without explicit confirmation.

struct Prefs {
	// Persistent identity.
	std::string nickname;          // "" ⇒ nickname prompt pending
	bool        isAnonymous = false;  // true ⇒ random nickname each session

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

	// Show Game Library cover art next to rows in Browser + Host
	// Games.  Matched by basename (filename stem of the hosted cart
	// against the local library index).  Defaults ON — art provides
	// strong visual identity on mobile / Gaming Mode screens.
	bool showBrowserArt = true;
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

	// Pending-accept queue.  Rebuilt every tick by ReconcileHostedGames
	// from the coordinator's per-offer queue so the UI can render one
	// row per queued joiner.  Each entry carries its own arrival time
	// so every row drives its own "Requested Xs ago" counter and its
	// own 20 s auto-decline.
	struct PendingJoinRequest {
		std::string hostedGameId;   // which offer the joiner is hitting
		std::string gameName;       // copy at enqueue time for display
		std::string handle;         // joiner's player handle (NUL-term)
		uint64_t    arrivedMs = 0;  // host-local SDL_GetTicks ms
	};
	std::vector<PendingJoinRequest> pendingRequests;

	// Snapshot of where the user was when the first request arrived,
	// so "Deny all" (or every request being declined / cancelled) can
	// put them back instead of leaving them stranded on the prompt.
	// Captured by ReconcileHostedGames on the 0 → N transition.  The
	// Accept path is handled separately via SessionRestorePoint.
	bool             promptSavedValid  = false;
	Screen           promptSavedScreen = Screen::Closed;
	// Gaming-Mode mobile-shell screen (g_mobileState.currentScreen)
	// at the moment the prompt was raised.  Stored as int to avoid
	// pulling the mobile UI header into this one — the value is a
	// plain ATMobileUIScreen cast.
	int              promptSavedMobile = 0;
	// True iff we called g_sim.Pause() when the prompt went up (so we
	// only Resume() what we paused — the user may have been paused
	// explicitly via hamburger menu before the prompt arrived).
	bool             promptPausedSim   = false;

	// Timestamp of the first tick where the joiner coordinator had a
	// non-idle phase (host-local ms via SDL_GetTicks).  Cleared when
	// the joiner returns to None.  Used to render "(Xs waiting)" on
	// the Connecting screen so the joiner gets feedback during a host
	// that's taking a long time to approve.
	uint64_t         joinStartedMs     = 0;

	// -- Active hosting state ---------------------------------------
	// Populated when the host has actually called StartHost() and
	// Create()'d a lobby entry.  Empty when we are idle or joining.
	std::string    lobbySessionId;
	std::string    lobbyToken;
	uint16_t       boundPort = 0;        // local UDP port the host is on
	uint64_t       lastHeartbeatMs = 0;  // monotonic ms of last OK heartbeat
	ATNetplay::LobbyEndpoint lobbyEndpoint;  // which lobby we announced to

	// -- Connection mode tracking -----------------------------------
	// Mirror of the active coordinator's PeerPath.  Edge-detected by
	// ATNetplayUI_Poll: on Direct→Relay or Relay→Direct transition we
	// fire a toast.  Persisted between polls so a state change is
	// detectable across frames.  Mode == None when no coordinator is
	// active (cleared on session end).
	ATNetplayGlue::PeerPath connectionMode = ATNetplayGlue::PeerPath::None;
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

	// Search/filter state — mirrors the Library Picker UX (text input +
	// A-Z alphabet pills).  `searchBuf` is user-typed free text matched
	// case-insensitive against the handle and cart name;
	// `letterFilter` < 0 is "All", 0..25 selects 'A'..'Z'.  Scroll
	// position is captured before a refresh so the list doesn't jump
	// back to the top when new data lands.
	char        searchBuf[96] = {0};
	int         letterFilter = -1;
	float       savedScrollY = 0.0f;
	bool        restoreScrollPending = false;

	// Lobby reachability ping — RTT of the most recent List() call,
	// smoothed with an EWMA (alpha = 0.3) so a single jittery sample
	// doesn't dominate the indicator.  0 with sampleCount==0 means
	// "never measured"; the UI shows a "pinging…" pill until the
	// first sample lands.  Updated from the LobbyOp::List result
	// handler in ATNetplayUI_Poll.
	uint32_t    lobbyLatencyMs           = 0;
	uint32_t    lobbyLatencyLastSampleMs = 0;
	int         lobbyLatencySampleCount  = 0;
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

// Aggregated /v1/stats across every enabled HTTP lobby in lobby.ini.
// Updated as Stats results land in the worker pump; values are summed
// across federated lobbies so the footer "12 sessions • 4 in play"
// reflects the whole network from the user's POV.
struct AggregateStats {
	int      sessions = 0;
	int      waiting  = 0;
	int      playing  = 0;
	int      hosts    = 0;
	uint64_t lastUpdateMs = 0;  // 0 = never
	// In-flight bookkeeping: number of lobbies we've posted Stats to
	// for the current refresh cycle, and a running accumulator that
	// resets on the first Stats result of a new cycle.  Without this
	// tracking we'd add the new cycle's totals on top of the previous
	// cycle's instead of replacing them.
	int      pendingResponses = 0;
	int      acc_sessions = 0;
	int      acc_waiting  = 0;
	int      acc_playing  = 0;
	int      acc_hosts    = 0;
};

// Active-draft state for the Add-Game form + Library Picker.  Shared
// across Gaming Mode and Desktop so both renderings read/write the
// same pending selection.  Cleared on successful commit; retained
// while the user flips between the Library picker and the Add-Game
// sheet.
enum class OfferSource : uint8_t { None = 0, Library, File };

struct OfferDraft {
	// Absolute UTF-8 path to the chosen image (Library variant path or
	// File picker path).  Empty while no selection is staged.
	std::string path;
	// Display name — game library name for Library picks, basename for
	// File picks.  Shown in the Add-Game form's "Selected: …" row.
	std::string displayName;
	// Source classification for the summary line + for recording
	// Library indices below.
	OfferSource source = OfferSource::None;
	// Library variant label (e.g. "NTSC") when source == Library; empty
	// otherwise.  Purely informational — the lobby wire protocol has
	// no awareness of variants; the path is the source of truth.
	std::string variantLabel;
	// Library indices (cached so re-opening the Library Picker can
	// steer focus/selection back to the last choice).  -1 when the
	// pick came from File.
	int libraryEntryIdx   = -1;
	int libraryVariantIdx = -1;
};

struct State {
	Screen         screen      = Screen::Closed;
	Prefs          prefs;
	Session        session;
	Browser        browser;
	LobbyHealth    lobbyHealth;
	AggregateStats aggregateStats;

	// My Hosted Games — the user's advertised hostedGames.  Persisted across
	// runs; runtime fields (state/port/tokens) reset each launch.
	std::vector<HostedGame> hostedGames;

	// Cap on the number of simultaneous hostedGames.  Single source of
	// truth lives in lobby_protocol.h (shared with the server, which
	// enforces the same number on POST /v1/session); this alias keeps
	// the existing State::kMaxHostedGames spelling for the dozen
	// references already in the UI.
	static constexpr size_t kMaxHostedGames =
		(size_t)ATLobby::kMaxHostedGamesPerHost;

	// The offer currently being edited / added (id string), so the
	// HostSetup screen knows which one to mutate.  Empty when adding
	// a new one.
	std::string editingGameId;

	// User activity — drives the Open ↔ Paused transitions.  Read
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

	// Pending selection from the Add-Game form / Library Picker.
	// Shared between modes so the Library Picker (which may navigate
	// away from Desktop AddOffer / Gaming AddOffer) can hand back a
	// result via common state rather than file-local statics.
	OfferDraft offerDraft;
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

// Save Prefs to the registry (Netplay/Handle, etc.).
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

// Pop the back stack until `target` becomes the current screen.  Use
// this for "Back to Browse" / "Back to My Games" buttons that are
// semantically a multi-step Back rather than a forward Navigate.
// Calling Navigate(target) instead would push the current (typically
// abandoned, e.g. failed Waiting) screen onto the stack, so a
// subsequent Back would land the user back on that abandoned screen.
//
// If `target` is not currently on the back stack, the stack is reset
// and the user is placed on `target` as a fresh entry — same end
// state as Navigate(Closed) followed by Navigate(target).
void PopTo(Screen target);

// Fresh random nickname (eight char, pronounceable) used when
// Prefs::isAnonymous is true.  Each session gets a new one.
std::string GenerateAnonymousNickname();

// Returns an error-free display nickname: if nickname is empty, falls
// back to the anon generator.  The rendered name is cached for the
// session so the lobby never sees a flicker.
const std::string& ResolvedNickname();

// Render the Online Play preferences body (sections + controls, no
// window/sheet wrapping).  Extracted out of RenderPrefs() so the
// Gaming-Mode Settings "Online Play" page and the old netplay Prefs
// sheet share exactly the same option list.  The caller is
// responsible for the surrounding scroll container and for calling
// SaveToRegistry() when the screen closes; widgets inside only
// mutate the in-memory Prefs struct.
void RenderOnlinePlayPrefsBody();

} // namespace ATNetplayUI
