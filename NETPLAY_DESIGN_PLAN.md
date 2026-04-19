# Altirra SDL3 Netplay — Design

> **Scope:** the design for Altirra SDL3's netplay feature as it exists
> today and the concrete next steps to make it robust for arbitrary
> games.  Decisions that have already been implemented are marked
> **[done]**; decisions agreed but not yet coded are **[planned]**.

---

## 1. Guiding principles

1. **Lockstep with input delay, not rollback.**  Atari multiplayer is
   60 Hz co-op (Mario Bros, Joust, M.U.L.E., Ballblazer), not
   frame-perfect 1v1 fighting.  Lockstep + 3–4 frames of input delay is
   what every shipped emulator netplay uses and players don't notice.
2. **Deterministic core → we send inputs, not state.**  Altirra's
   `Advance()` is single-threaded, has no host-clock reads, no
   host-RNG, no floating-point gameplay paths.  Given identical start
   state + identical inputs, two instances stay bit-identical forever.
3. **GPL v2+ compatible only.**  POSIX sockets + SDL3; no Epic Online
   Services, no Steamworks, no Discord GameSDK, no GGPO (rollback
   wouldn't help us anyway).
4. **Lobby is a session directory, never a game router.**  Inputs,
   snapshots, media — none of it touches the lobby.  Federated HTTP
   servers that peers can spin up in a weekend.
5. **Media travels with the session.**  Altirra savestates are
   reference-format (they store firmware/cart/disk by CRC, not bytes).
   For netplay to work with arbitrary games on arbitrary machines we
   bundle the referenced bytes into the wire format and cache them by
   hash on the joiner's disk.  See §5.2 and §6.

---

## 2. Architecture at a glance

```
                             main_sdl3.cpp main loop
                                      │
       ┌──────────────────────────────┼──────────────────────────────┐
       ▼                              ▼                              ▼
 ATNetplayGlue::Poll           ATNetplayUI_Poll                  Render
 (drains all coords)           (lobby worker,                   (ImGui)
                                ReconcileOffers,
                                deferred dispatch)

                     ATNetplayGlue owns
                     ┌────────────────────────────────┐
                     │ vector<HostSlot>   g_hosts     │ one per offer
                     │ unique_ptr<Coord>  g_joiner    │ at most one
                     └────────────────────────────────┘
                                     │
                                     ▼
                         ATSimulator (unmodified core)
```

**Files (all exist today):**

| File | Role |
|---|---|
| `src/AltirraSDL/source/netplay/transport.{h,cpp}` | UDP + framing + redundancy |
| `src/AltirraSDL/source/netplay/protocol.{h,cpp}` + `packets.h` | Wire format v2 |
| `src/AltirraSDL/source/netplay/snapshot_channel.{h,cpp}` | Chunked reliable snapshot transfer |
| `src/AltirraSDL/source/netplay/lockstep.{h,cpp}` | Input ring + frame gate |
| `src/AltirraSDL/source/netplay/coordinator.{h,cpp}` | Phase machine per host/joiner |
| `src/AltirraSDL/source/netplay/netplay_glue.{h,cpp}` | Multi-offer runtime + single joiner slot |
| `src/AltirraSDL/source/netplay/lobby_client.{h,cpp}` + worker | HTTP lobby I/O on a worker thread |
| `src/AltirraSDL/source/ui/netplay/ui_netplay_state.{h,cpp}` | `HostedOffer`, `UserActivity`, persistent prefs |
| `src/AltirraSDL/source/ui/netplay/ui_netplay_actions.{h,cpp}` | Per-offer lifecycle + `ReconcileOffers` |
| `src/AltirraSDL/source/ui/netplay/ui_netplay_activity.{h,cpp}` | Sim-event hook (EXELoad, ColdReset → activity) |
| `src/AltirraSDL/source/ui/netplay/ui_netplay_desktop.cpp` | Desktop ImGui flows |
| `src/AltirraSDL/source/ui/netplay/ui_netplay_screens.cpp` | Gaming Mode touch flows |

The emulation core has **zero** netplay changes.

---

## 3. Multi-offer hosted games (the v1 UX model)

The user does not "host a game"; they **queue up games to host**.
Adding a game creates a `HostedOffer` that, when enabled, binds its
own UDP port and registers one lobby listing.  Several offers can be
listed simultaneously — "I'm up for Joust OR M.U.L.E., ping me".

### 3.1 `HostedGame` (persisted to registry) — [done]

Renamed from `HostedOffer` — the user-facing concept is a "hosted game",
not a networking-speak "offer".  The machine config is a single
`MachinePreset` enum (§3.3), not a free-form struct: only three
preset options are offered in v1 to keep the UX tight and eliminate
per-user firmware configuration drift.

```cpp
struct HostedGame {
    // Persistent
    std::string   id;           // local uuid-ish
    std::string   gamePath;     // UTF-8 absolute path to image
    std::string   gameName;
    std::string   cartArtHash;  // optional, for lobby art matching
    bool          isPrivate = false;
    std::string   entryCode;    // cleartext local; hashed on wire
    bool          enabled   = true;
    MachinePreset preset    = MachinePreset::XLXE_NTSC;

    // Runtime
    HostedGameState state = HostedGameState::Draft;
    std::string     lobbySessionId, lobbyToken;
    uint16_t        boundPort = 0;
    uint64_t        lastHeartbeatMs = 0;
    std::string     lastError;
    bool            snapshotQueued = false;  // idempotency for boot+snapshot
    uint8_t         lastPhase = 0;           // edge detection
};
```

### 3.3 Machine presets — [done]

Three fixed configurations; no user customisation in v1.  Every
preset uses Altirra's built-in AltirraOS + AltirraBASIC ROMs, so a
fresh install with no user firmware configured can host or join any
listed game.

| Preset       | Hardware  | RAM        | Video | BASIC | Firmware        |
|--------------|-----------|------------|-------|-------|-----------------|
| `XLXE_PAL`   | 800XL     | 320K Rambo | PAL   | off   | AltirraOS-XL    |
| `XLXE_NTSC`  | 800XL     | 320K Rambo | NTSC  | off   | AltirraOS-XL    |
| `A5200`      | 5200      |  16K       | NTSC  |  n/a  | AltirraOS-5200  |

**Why only three?** Custom firmware, VBXE, alternate memory modes, and
custom profiles all introduce per-user drift ("I have the OS-B dump,
you don't").  Keeping the host and joiner on identical built-in
ROMs for v1 sidesteps the entire media-portability problem until we
build proper content-addressed media transfer (§5.2).

### 3.4 Non-destructive session lifecycle — [done]

The user's Altirra `settings.ini` on disk is **never** modified by a
netplay session.  Only the live simulator mutates, and even that is
reverted when the session ends:

```
  peer connects
       │
       ▼
  1. SaveSessionRestorePoint()      ← full in-memory g_sim.CreateSnapshot()
       │
       ▼
  2. ApplyPreset(game.preset)       ← hardware/memory/video/firmware/BASIC
       │
       ▼
  3. UnloadAll + Load(game) + ColdReset + Resume
       │
       ▼
  lockstep session runs...
       │
       ▼
  session ends (any reason)
       │
       ▼
  RestoreSessionRestorePoint()      ← g_sim.ApplySnapshot(savedState)
                                      + g_sim.Resume()
                                      — user is back exactly where
                                        they left off
```

If step 1 fails, the session is refused (host rejects the peer).
The restore point is held as a `vdrefptr<IATSerializable>` in the
actions TU.  The edge that triggers restore is
`UserActivity::InSession → !InSession` detected each frame in
`ReconcileHostedGames`.

```cpp
// ui_netplay_actions.{h,cpp}
bool        SaveSessionRestorePoint();
void        RestoreSessionRestorePoint();
bool        HasSessionRestorePoint();
std::string ApplyPreset(MachinePreset p);  // "" on success; reason on failure
```

On app restart, saved offers load **disabled** — user must explicitly
re-enable.  This makes the offer list a "favourites" list, not an
auto-advertise list.  Cap at `kMaxOffers = 5` (keeps us comfortably
under lobby rate limits).

### 3.2 `UserActivity` state machine — [done]

```
        ┌──────────┐  ColdReset/EXELoad/StateLoaded   ┌──────────────┐
        │   Idle   │ ───────────────────────────────▶ │ PlayingLocal │
        └──────────┘                                  └──────────────┘
              ▲                                              │
              │ coord reaches terminal phase                 │ coord in
              │                                              │ Handshaking/
              │                                              │ Sending…
              │                                              ▼
              │                                       ┌──────────────┐
              └────────────────────────────────────── │  InSession   │
                                                      └──────────────┘
```

`ReconcileOffers()` runs every frame and forces each offer's coord
into the desired state for the current `(activity, enabled)` tuple:

- `Idle` + `enabled` → **Listed** (coord bound, lobby announced)
- `InSession` + *not the in-session offer* → **Suspended** (coord torn
  down, lobby listing deleted — so third-party browsers don't see a
  busy host as joinable)
- `PlayingLocal` → every offer **Suspended**
- Session ends → reconcile flips enabled offers back to **Listed**

All driven idempotently from phase-edge detection, no per-event
bookkeeping.

---

## 4. Lockstep model

```
                 input-delay D = 3 frames

 wall-clock       ┌──┬──┬──┬──┬──┬──┬──┬──┬──
 (local capture)  │10│11│12│13│14│15│16│17│…
                  └──┴──┴──┴──┴──┴──┴──┴──┴──
                    └──┐ applied to emu frame 13
                       ▼
 emulation        ┌──┬──┬──┬──┬──┬──┬──┬──┬──
                  │  │  │  │13│14│15│16│17│…    3-frame warm-up
                  └──┴──┴──┴──┴──┴──┴──┴──┴──
                           ▲ advances only when BOTH peers' input
                           │ for frame 13 is in the buffer
```

**Invariants:**
1. Local input captured at wall-clock F is applied at emu frame F+D.
2. `g_sim.Advance()` for frame F runs iff the buffer holds both peers'
   input for F.  If remote is missing, main loop stalls: polls SDL,
   polls socket, renders ImGui, re-checks.  Never calls `Advance()`.
3. Each packet carries the last R=5 frames of input → any one of those
   packets arriving is enough.  No ACKs on the hot path.
4. Each peer ships a rolling state hash per frame; mismatch triggers a
   Desync ending within one RTT.

Bandwidth: 36 B × 60 Hz = **2.2 KB/s per direction.**

**Main loop hook** (main_sdl3.cpp:2038-2127) — already wired.  The
only caveat today is that input capture is stubbed with
`SubmitLocalInputZero()` while Phase 8 is pending.

---

## 5. Wire format

UDP datagrams, fixed size, little-endian, no allocations on hot path.

### 5.1 Handshake

```c
struct NetHello {             // joiner → host
    uint32_t magic, flags;
    uint16_t protocolVersion;
    uint8_t  sessionNonce[16];
    uint64_t osRomHash, basicRomHash;  // 0 for "host advertises its own"
    char     playerHandle[32];
    uint16_t acceptTos;
    uint8_t  entryCodeHash[16];         // truncated SHA-256; zero for public
};

struct NetWelcome {            // host → joiner
    uint32_t magic;
    uint16_t inputDelayFrames, playerSlot;
    NetplaySettings settings;
    char     cartName[64];
    uint32_t mediaManifestBytes;  // [planned §5.2] 0 if not bundling
    uint32_t snapshotBytes, snapshotChunks;
};

struct NetReject { uint32_t magic, reason; };
```

### 5.2 Media bundle + snapshot — [planned]

**The central protocol change to make arbitrary games work.**

Altirra savestates reference firmware/cart/disk by CRC — they don't
embed the bytes.  So applying a received snapshot on the joiner only
succeeds if the joiner *already has* every referenced file.  That's
not a reasonable assumption.

New wire phase, inserted between handshake and lockstep:

```
host              joiner
 │                  │
 │  NetWelcome  ──▶ │   includes mediaManifestBytes + snapshotBytes
 │                  │
 │ ◀── NeedBlobs ── │   joiner replies with hashes it DOESN'T have
 │                  │   (checked against its content-addressable cache)
 │                  │
 │  Manifest  ───▶  │   JSON: [{kind, hash, sizeBytes, refString?}, ...]
 │  (chunked)       │
 │                  │
 │  Blob bytes ──▶  │   only for hashes in NeedBlobs, chunked
 │  (chunked, ACKed)│
 │                  │
 │  Snapshot  ───▶  │   existing chunked snapshot channel
 │  (chunked, ACKed)│
 │                  │
 │                  ▼
 │         joiner materializes cache → registers transient firmware
 │         refs → mounts images → g_sim.Load(snapshot) → ack
 │  ◀── ApplyOk ──  │
 │                  │
 │  Lockstepping ───▶◀──  Lockstepping
```

**Manifest shape:**

```json
{
  "hardware": { "mode": "800XL", "video": "NTSC", "memory": "XL128K",
                "cpu": "6502C", "vbxe": false },
  "firmware": [
    { "slot": "KernelXL", "refString": "atariosxl", "hash": "sha256:..." },
    { "slot": "BASIC",    "refString": "ataribasic", "hash": "sha256:..." }
  ],
  "cartridge": { "mapper": 1, "hash": "sha256:...", "sizeBytes": 16384 },
  "disks":     [ { "drive": 1, "hash": "sha256:...", "sizeBytes": 133120 } ],
  "cassette":  null,
  "xex":       null
}
```

**Content-addressable cache** on the joiner:
`$configdir/netplay_cache/<kind>/<sha256>.bin`.  LRU eviction under a
user-configurable cap (default 256 MB).  Firmware rarely evicts
because entries are small; cart/disk reuse across sessions is
automatic.

**Second session with the same peer transfers zero media bytes.**
That's the whole point of caching — firmware stays resident forever;
popular carts stick around; the common case is "manifest says I need
these 3 hashes, joiner already has all of them, skip straight to
snapshot."

**Hash choice:** SHA-256 truncated to 128 bits for on-wire encoding;
store the full digest on disk.  CRC32 is too weak as a content
address (legitimate collisions between firmware dumps exist).

**Trust:** joiner verifies each received blob's hash before writing
to cache — a malicious host cannot poison the cache.  Firmware is
registered under a **transient session-scoped ref-string**, never
merged into the user's permanent firmware list without an explicit
"Save to library" gesture after the session.

### 5.3 Per-frame input — [done, see §4]

```c
struct NetInput     { uint8_t stickDir, buttons, keyScan, extFlags; };
struct NetInputPacket {
    uint32_t magic, baseFrame;
    uint16_t count, ackedFrame;
    uint32_t stateHashLow32;
    NetInput inputs[R];   // R=5
};
```

### 5.4 Disconnect

```c
struct NetBye { uint32_t magic, reason; };
```

Clean exit, desync detected, timeout, version mismatch.

---

## 6. Host boot & snapshot lifecycle

The flow when a peer connects to an enabled offer:

```
peer's Hello arrives
         │
         ▼                                        ┌─────────────────────┐
  Coordinator::HandleHelloFromJoiner              │ ReconcileOffers     │
     phase: WaitingForJoiner → SendingSnapshot    │ edge detect:        │
         │                                        │ !snapshotQueued &&  │
         ▼                                        │ nowHandshake        │
  (next main-loop tick)                           │                     │
  ReconcileOffers sees phase change   ───────────▶│   enqueue:          │
                                                  │   • NetplayHostBoot │
                                                  │   • NetplayHostSnap │
                                                  │   PostLobbyDelete(o)│
                                                  │   Notify("Peer      │
                                                  │      connecting…")  │
                                                  └─────────────────────┘
         │
         ▼ (same frame, FIFO)
 ╔══════════════════════════════════════════════════════════════════╗
 ║ kATDeferred_NetplayHostBoot — [planned §6.1 has bugs today]     ║
 ║   - Apply offer.config to sim (set hardware mode, video std,    ║
 ║     memory, VBXE, firmware refs) BEFORE Load                    ║
 ║   - UnloadAll + Load(mctx) with full retry loop (mode switch,   ║
 ║     BASIC-off on conflict, mapper resolution)                   ║
 ║   - On final failure: offer.lastError = reason, notify user,    ║
 ║     abort session (send NetReject + close coord)                ║
 ║   - ColdReset + Resume                                          ║
 ╚══════════════════════════════════════════════════════════════════╝
         │
         ▼
 ╔══════════════════════════════════════════════════════════════════╗
 ║ kATDeferred_NetplayHostSnapshot — [planned §6.1]                 ║
 ║   - If Boot failed: skip (fatal already surfaced)               ║
 ║   - Build Manifest from currently-mounted media                 ║
 ║   - CreateSnapshot() → serialise with IATSaveStateSerializer    ║
 ║   - Submit { manifest, blobs-host-has, snapshot } to coord      ║
 ╚══════════════════════════════════════════════════════════════════╝
         │
         ▼
  Coordinator exchanges NeedBlobs → blobs → snapshot
  (chunks flow; joiner side materializes + applies)
         │
         ▼
  Both sides → Lockstepping
  (main loop: SubmitLocalInput per frame, CanAdvance gates Advance)
```

### 6.1 Host-boot robustness — [planned]

Today's `kATDeferred_NetplayHostBoot` handler silently no-ops on
`Load` failure (ui_main.cpp:566 `if (Load) { … }` with no else
branch).  That matches no other boot path in the codebase — every
user-triggered boot either retries or shows a dialog.  We mirror
`kATDeferred_BootImage`'s retry loop (ui_main.cpp:269-323):

- Auto-switch 800XL↔5200 on `mbMode5200Required` / `mbModeComputerRequired`.
- Disable BASIC on `mbMemoryConflictBasic`.
- Accept incompatible disk format silently.
- On unknown cart mapper: this should never happen in netplay because
  the manifest pins the mapper — if it does, fail fast with error.

On final failure: set `offer.lastError`, notify user, abort the
session (`Coordinator::SendRejectAndClose(kRejectHostNotReady)`) so
the joiner isn't left hanging in "Connecting…".

### 6.2 Joiner snapshot-apply — [planned]

Today's `kATDeferred_NetplayJoinerApply` has the mirror bug —
silently does nothing if `g_sim.Load()` returns false, leaving the
joiner's "Connecting…" modal stuck forever and the coordinator in
`SnapshotReady`.

Fix: always call either `ATNetplayUI_JoinerSnapshotApplied()` on
success or `ATNetplayUI_JoinerSnapshotFailed(reason)` on failure.
The failure handler sends a `NetBye { reason = apply_failed }` to
the host so both sides tear down cleanly, and shows the user a
specific error ("missing firmware: atariosxl" / "memory config
mismatch").

### 6.3 Edge detection — [planned]

Current `ReconcileOffers` edge check is
`wasListening && nowHandshake` (actions.cpp:315).  `wasListening`
requires the previous tick to have seen `WaitingForJoiner`
specifically.  If the coord transitions straight from `None` →
`SendingSnapshot` within one `Poll()` batch, the edge is missed and
the boot never enqueues.

Fix: `!o.snapshotQueued && nowHandshake`.  `snapshotQueued` is
already the idempotency guard; the `wasListening` predicate is
redundant and incorrect.

---

## 7. UX / UI — Desktop reference, Gaming Mode mirror

Desktop is the reference implementation; Gaming Mode mirrors the same
state singleton via touch widgets.  Both consume the same
`ATNetplayUI::State` so switching modes mid-session keeps everything
coherent.

### 7.1 Entry flow

```
Menu: Online Play ▸
  Host Games…            → MyHostedGames screen
  Browse Sessions…       → Browser screen
  ─────────────
  Preferences…           → Netplay prefs
  Change Nickname…
  Disconnect             (disabled unless connected)
```

### 7.2 Host Games screen (Desktop)

```
  ┌─ Online Play — Host Games ──────────────────────────────────┐
  │ Ready — your games are listed on the lobby.                  │
  │                                                               │
  │ [Add Game…]  [Browse Sessions…]  [Preferences…]               │
  │                                                               │
  │ Game          Visibility  State     Port    Enabled  Actions  │
  │ ──────────────────────────────────────────────────────────── │
  │ Joust.atr     Public      Listed    26101   [✓]      [Remove] │
  │ M.U.L.E.      Private     Listed    26102   [✓]      [Remove] │
  │ Ballblazer    Public      Draft     —       [ ]      [Remove] │
  │                                                               │
  │                                                    [Close]    │
  └───────────────────────────────────────────────────────────────┘
```

**Banner text reflects activity:**
- *Idle*, offerCount=0: "Add a game to start hosting."
- *Idle*, offerCount>0: "Ready — your games are listed on the lobby."
- *PlayingLocal*: "You're playing single-player — hosted games are paused until you stop."
- *InSession*: "An online match is in progress — your other hosted games are paused while you play."

### 7.3 Add Game — Library or File — [done]

Segmented picker:

- **From Library** — grid of `ATGameLibrary::GetEntries()` with
  cached art tiles (same grid widget as the main Game Library).
- **From File** — `ATUIShowOpenFileDialog` with disk/cart/exe/cassette
  filters.

After picking, the config dialog appears **[planned §7.4]**:

```
  ┌─ Add Game — Joust.atr ──────────────────────────────────────┐
  │ Machine:        (•) 800XL   ( ) 5200   ( ) 130XE            │
  │ RAM:            [ 64K ▾]                                     │
  │ Video:          (•) NTSC   ( ) PAL                           │
  │ BASIC:          [ ] enabled                                  │
  │ VBXE:           [ ] enabled                                  │
  │ OS ROM:         [ atariosxl ▾]                               │
  │   (match against firmware manager by refString)              │
  │                                                               │
  │ Visibility:     (•) Public   ( ) Private                     │
  │  Entry code:    [________________]  [Regenerate]             │
  │   (shown only when Private is selected)                      │
  │                                                               │
  │                                  [Cancel]  [Add to my list]  │
  └───────────────────────────────────────────────────────────────┘
```

The Machine / RAM / Video / OS ROM fields default to the emulator's
current config (so most users just click Add).  They're stored on the
offer and **applied before boot** when a peer connects (§6.1).  This
is the only way a 5200 cart or a VBXE title works reliably.

### 7.4 Browser — [done, visuals need polish]

```
  ┌─ Online Sessions ───────────────────────────────────────────┐
  │ Nickname: Alice  [Change]                 [Host Games…]     │
  │                                                              │
  │  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐            │
  │  │  Joust  │ │M.U.L.E. │ │Ballblzr │ │🔒 Mario │            │
  │  │  [art]  │ │  [art]  │ │  [art]  │ │  [art]  │            │
  │  │ Bob·EU  │ │Carol·NA │ │Dan·EU   │ │Eve·LAN  │            │
  │  │  1/2    │ │  3/4    │ │  1/2    │ │  1/2 🔒 │            │
  │  └─────────┘ └─────────┘ └─────────┘ └─────────┘            │
  │                                                              │
  │ 4 sessions · refreshed 2 s ago                  [Refresh]    │
  └──────────────────────────────────────────────────────────────┘
```

- Tiles use local Game Library art matched by `cartArtHash`.
  Unknown hash → placeholder tile with the cart name.  No network art
  download.
- **Own offers are filtered out** by matching `lobbySessionId`
  (ui_netplay.cpp:130-138) so the user never sees their own listings.
- **Padlock** tile shows a Private session; clicking prompts for the
  entry code before handshake.

### 7.5 Connect flow (joiner POV) — [planned, today's flow is confusing]

Today: click tile → "Connecting — loading the game state…" modal →
auto-dismiss when `IsLockstepping` → game running.  On any failure
the modal hangs forever (§6.2).

Planned, broken into visible phases so the user knows what's
happening:

```
 ┌─ Connecting to Bob's Joust ─────────────┐
 │ ● Handshaking…                           │   (0–1 s)
 │ ○ Downloading game files (3 items, 140 KB)│  (shows progress bar;
 │ ○ Loading state                          │    skipped if cache hit)
 │ ○ Synchronising                          │
 │                                           │
 │                               [Cancel]   │
 └───────────────────────────────────────────┘
```

Four steps light up in order; current step shows a spinner + a
one-line detail ("12/24 chunks").  On error, the step turns red and
the detail becomes the reason.  No more infinite hangs.

### 7.6 Peer-accept UX — [planned]

`Prefs::acceptMode` already exists with three values (AutoAccept,
PromptMe, ReviewEach) but the code currently always auto-accepts.
Wire the modal:

```
 ┌─ Join Request ────────────────────────────┐
 │ Eve (EU, 42 ms) wants to join your         │
 │ Joust session.                              │
 │                                             │
 │ ──────────────────────────────────────────  │
 │ Auto-decline in 20 s            [Reject]   │
 │                                  [Accept]  │
 └─────────────────────────────────────────────┘
```

`ReviewEach` omits the countdown.  Paired with the notifications
already live in §7.7.

### 7.7 Host notifications — [done]

`platform_notify.{h,cpp}` delivers all three layers:

1. Taskbar/dock flash via `SDL_FlashWindow`.
2. System toast via Linux libnotify (dlopen, no hard dep) / Win32
   `Shell_NotifyIconW` / macOS `UNUserNotificationCenter`.
3. Chime: short WAV mixed into the emulator audio output (no second
   device open).

Each is individually toggleable in Preferences → Netplay.  Focus
stealing (`SDL_RaiseWindow`) is opt-in, off by default.

### 7.8 In-session HUD — [planned]

While Lockstepping, a small always-visible overlay in the top-right
corner:

```
 NETPLAY · 42 ms · frame 3841 · delay 3 · OK            [Disconnect]
```

`OK` → amber on >5 % packet loss in last second, red on stall, shows
the desync frame on failure.  Disconnect button sends `NetBye` and
tears down cleanly.

### 7.9 Port / controller mapping — [done]

Host always drives emulated Port 1; each joiner's local "Port 1"
device remaps to the next emulated port at injection time (Port 2,
Port 3 in v2, …).  The joiner never has to reconfigure controls —
"my Fire button is still Fire."

---

## 8. Determinism contract

Inputs-only netplay requires perfect determinism.  Altirra's core is
already deterministic; the netplay layer enforces and detects:

**Reject at connect if any of these differ:**
- Hardware type, RAM size, video standard (NTSC/PAL), CPU type
- OS ROM + BASIC ROM hashes
- Accelerator / SIO-patch settings that affect timing
- Any setting in `NetplaySettings::settingsHash`

**Runtime:**
- Each peer computes a 32-bit rolling hash over CPU regs + a small
  set of key sim vars, ships it in `NetInputPacket.stateHashLow32`.
- On mismatch, coord ends the session, saves both peers' snapshots
  to `$configdir/netplay_desync_<ts>.astate`, shows "Desync at frame
  F" dialog, returns to menu.

**Pre-ship tripwires (Phase 1 — pending):**
1. Two `ATSimulator` instances in one process, same snapshot + same
   input trace, assert equal hash every frame for 60 s.
2. Same over UDP loopback (exercises the serialise path).
3. Same with audio enabled (rules out audio-driven non-determinism).

Tripwire (1) is the make-or-break gate for the whole effort.

---

## 9. Federated lobbies — [done for v1.0]

`lobby.ini` under `$configdir`, one section per server, queried in
parallel and merged client-side.  Defaults shipped with the build:

```ini
[official]
name   = Altirra Official Lobby
url    = http://92.5.13.40:8080
region = global

[lan]
name      = LAN
transport = udp-broadcast
port      = 26101
```

### Lobby schema v2 — live (commit `07b98c1`, 2026-04-19)

```
POST   /v1/session        { cartName, hostHandle, hostEndpoint, region,
                            playerCount, maxPlayers, protocolVersion,
                            visibility, requiresCode, cartArtHash }
                          → { sessionId, ttlSeconds }
GET    /v1/sessions       → [sessions]
POST   /v1/session/{id}/heartbeat
DELETE /v1/session/{id}
```

(Future v2+: `POST /v1/session/{id}/join` for rendezvous /
hole-punching; same schema plus `joinerEndpoint`.)

**Lobby never sees game traffic.**  Inputs, snapshots, media — all
peer-to-peer.  Lobby is a session directory + TTL expiry, nothing
more.  Reference server: ~150 lines of Go stdlib, deployed to Oracle
Cloud Frankfurt, auto-deploy on push to `main` in the
`altirra-sdl-lobby` repo.

### NAT reality

| Host situation | Works? |
|---|---|
| Public IPv4 + UDP port-forwarded | ✅ |
| IPv6 both ends, inbound permitted | ✅ |
| UPnP-IGD enabled (v1.1 auto-map) | ✅ |
| CGNAT host | ❌ until v2 rendezvous/relay |
| Host won't or can't configure router | ❌ until UPnP or v2 |

Host dialog shows LAN + detected public address (STUN against
`stun.l.google.com:19302`, no our-server involvement) and a specific
warning on CGNAT-range detection.  Practical workaround today:
whichever friend can port-forward hosts.

---

## 10. Current status & bug ledger (2026-04-19)

### Implemented

| Area | State |
|---|---|
| Protocol v2 (`entryCodeHash`, `visibility`, `requiresCode`) | ✅ round-trip selftest passes |
| UDP transport (POSIX+Winsock) | ✅ |
| Snapshot channel (chunked, ACKed) | ✅ |
| Lockstep coordinator (multi-offer hosts + single joiner) | ✅ 120-frame selftest |
| Main-loop hook (`netplay_glue` + `main_sdl3.cpp`) | ✅ |
| Lobby client + background worker | ✅ |
| `lobby.ini` + LAN broadcast discovery | ✅ |
| Online Play UI — Desktop + Gaming Mode, same `State` | ✅ |
| Hosted offers — persistence, multi-offer, activity suspension | ✅ |
| Platform notifications + chime | ✅ |
| Peer-connect notification + own-offer filter in Browser | ✅ |
| Lobby v2 deployed | ✅ `07b98c1` |

### Known bugs to fix before real-world play

| # | File:line | Bug |
|---|---|---|
| B1 | ui_main.cpp:566 | `NetplayHostBoot` silently no-ops on `Load` fail — no retry loop, no error |
| B2 | ui_main.cpp:580 | `NetplayJoinerApply` silently no-ops on `Load` fail — joiner hangs forever |
| B3 | actions.cpp:315 | `ReconcileOffers` edge `wasListening && nowHandshake` misses direct transitions |
| B4 | actions.cpp:126-157 | Lobby `Create` hardcodes `osRomHash=basicRomHash=0`; no pre-flight mismatch detection |
| B5 | state.h:72-99 | `HostedOffer` has no machine config fields |
| B6 | protocol | No media bundling — joiner silently fails on firmware/cart mismatch |
| B7 | main_sdl3.cpp:2108 | `SubmitLocalInputZero` placeholder — real input capture is Phase 8 |

### Pending phases

| Phase | Status |
|---|---|
| 1. Determinism gate (loopback tripwires) | pending |
| 6.1–6.3. Host-boot / joiner-apply / edge-detect bug fixes | pending |
| 7.3. Per-offer config dialog + persistence (§3.1 MachineConfig) | pending |
| 7.5. Joiner connect-flow progress steps | pending |
| 7.6. Peer-accept modal + auto-decline timer | pending |
| 7.8. In-session HUD | pending |
| 5.2. Media bundle + content-addressable cache | pending |
| 8. Input capture + inject (replace `SubmitLocalInputZero`) | pending |
| 13. Desync dump writer + dialog | pending |
| 14. Two-machine testing, default delay tuning | blocked on 8 |

---

## 11. Implementation order (from here)

Each step is separately demonstrable.  Order reflects "smallest fix
that makes the next step testable":

1. **B1 + B2 + B3** — host boot retry loop, joiner apply error path,
   edge-detect simplification.  Makes normal ATR/cart games actually
   boot on host and shows real errors instead of hanging modals.
2. **Per-offer machine config (§3.1, §7.3)** — capture config at
   "Add Game" time, apply before boot.  Makes 5200/VBXE/PAL games
   work without manual host pre-config.  Solves B5.
3. **Media bundle + cache (§5.2)** — extend wire format, add
   manifest/need-blobs/blob-transfer phases, disk cache.  Solves B6
   and eliminates the "joiner must already have the files" class
   of silent failure entirely.
4. **Connect-flow progress steps (§7.5)** — wire the visible
   phase-by-phase modal on the joiner.  Replaces the infinite
   "Connecting…" hang with actionable status.
5. **Peer-accept modal (§7.6)** — wire PromptMe / ReviewEach to
   actually prompt; add 20 s countdown.
6. **In-session HUD (§7.8)** — top-right overlay + disconnect
   button.  Low risk, high UX value.
7. **Phase 8 — real input capture + inject.**  Replaces
   `SubmitLocalInputZero`.  Needs Atari stick/button bit layout
   investigation (see `CLAUDE.md` note on not guessing hardware
   constants).  **This unblocks actual gameplay.**
8. **Phase 1 — determinism tripwires.**  Gate for shipping.
9. **Desync dump writer + friendly dialog.**
10. **Two-machine testing, input-delay tuning, v1.0 ship.**

### v1.1

- UPnP / NAT-PMP auto port mapping.
- Lobby editor UI wrapping `lobby.ini`.

### v2.0

- STUN rendezvous via lobby `/join` endpoint.
- TURN relay fallback (reference: `coturn`; lobbies advertise
  relays).

---

## 12. Explicit non-goals for v1

| Feature | Why deferred |
|---|---|
| Rollback netcode (GGPO-style) | Wrong model for Atari co-op; proving determinism for snapshot/resim would be weeks of work for no perceptible benefit |
| 4-player sessions | InputBuffer already keyed by `(frame, playerIndex)`; raise `kMaxPlayers` in v2 — very few Atari titles use 4 joysticks |
| Save / load savestate during session | Host sends fresh snapshot, joiner re-applies — non-trivial pause semantics |
| Spectator mode | Requires mid-session snapshot + new "read-only" coord role |
| Chat | Out of scope; users have Discord/voice already |
| Per-session accounts / auth | Lobby is stateless by design |
| Save received game to local library | Add as v2 post-session prompt with ToS re-confirmation |

---

## 13. Decisions already made (no more debate)

- **Lockstep + input delay, not rollback.**
- **Media travels with the session, cached on disk by hash.**
- **Machine config captured per-offer**, applied before boot.
- **Lobby federation**, not a single canonical server.
- **Option (A) auto-accept stays the default**; PromptMe is a Pref.
- **App restart → offers load Disabled**; favourites list model.
- **Desktop is the reference UI**; Gaming Mode mirrors it.
- **One snapshot-apply is an atomic transaction**: either Lockstepping
  or a specific error modal, never an infinite "Connecting…".

---

## 14. Summary

Multi-offer lockstep netplay over a federated lobby, with media
bundled and cached so arbitrary games work on arbitrary peers.
Single shared state singleton drives Desktop + Gaming Mode identically.
Emulation core untouched; ~15 lines of main-loop hook.  Wire protocol
small enough for dial-up.  Every silent-failure path replaced with a
visible error modal — the user always knows what's happening.

That's the whole plan.  Everything else is phasing.
