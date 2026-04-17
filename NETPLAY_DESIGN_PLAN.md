# Altirra SDL3 Netplay — Design Plan

> **Scope:** This document specifies the simplest netplay design that
> properly works for Altirra's SDL3 build.  It is deliberately minimal.
> Every feature not listed here is explicitly out of scope for v1 and
> belongs in a future revision.

---

## 1. Guiding principles

1. **Lockstep with input delay, not rollback.**  Atari games are 50/60 Hz
   co-op (Mario Bros, Joust, M.U.L.E., Dandy, Ballblazer), not
   frame-perfect 1v1 fighting games.  Rollback is ~10× the code and
   requires a full determinism audit of a cycle-accurate emulator for a
   benefit the workload does not need.  Lockstep with 2–4 frames of input
   delay is what FCEUX, Mednafen, and MAME use — it ships, and players
   don't notice.
2. **GPL v2+ compatible only.**  Altirra is GPL v2+ (see `LICENSE`).
   This rules out Epic Online Services, Steamworks, and Discord GameSDK
   regardless of their price.  v1 uses only POSIX sockets (already
   present in `src/AltirraSDL/source/bridge/bridge_transport.cpp`) —
   zero new third-party dependencies.
3. **No servers for v1.**  Direct IP connect.  The host forwards a UDP
   port; the joiner enters `host:port`.  That is how every emulator
   netplay shipped for two decades before matchmaking existed, and it
   works.  Lobby/relay/NAT traversal are deferred to v2 behind a clean
   interface.
4. **Leverage what already works.**  The simulator is single-threaded,
   synchronous, pause-capable, has a fixed-rate pacer, and has a modern
   snapshot API.  The netplay layer is a thin wrapper — it does not
   restructure emulation.

---

## 2. Why inputs-only is enough (the deterministic-lockstep thesis)

Every further decision in this plan follows from one property of the
Altirra core: it is **strictly deterministic**.  Given identical
starting state and identical input streams, two instances produce
bit-identical output forever.  No threads in `Advance()`
(`simulator.h:423`), no host wall-clock reads, no host-RNG use, no
floating-point paths in the gameplay loop.  The integration survey
confirms this for the code as it ships today.

From that single property, everything else falls out:

- **We send inputs, not state.**  One frame of one player's input is
  4 bytes (stick / buttons / scancode / flags).  The full emulator
  state is ~64 KB base, much more with disk images.  At 60 Hz that is
  ~1000× less bandwidth.  Both peers compute state independently from
  the same inputs — **state is a result, not a message**.
- **State crosses the wire exactly once.**  At session start the host
  snapshots via the existing `IATSerializable` API and ships it to
  the joiner.  After that, the state channel goes silent for the
  whole session; only inputs move.
- **Input delay gives the network time.**  Local input captured at
  wall-clock frame F is tagged for *emulation* frame F + D (D ≈ 3).
  The remote peer has one ping's worth of wall-clock time to deliver
  that input before it is needed on-screen.  Both sides delay
  identically, so there is no "local feels ahead of remote" artefact.
- **Redundancy, not retransmission.**  Each packet carries the last R
  frames of input (R = 5).  Any one of those R packets arriving in
  time is sufficient — a burst of four consecutive drops must occur
  before a stall.  For a 4-byte-per-frame stream this is strictly
  cheaper than reliable-UDP machinery like ENet.
- **Rolling hash detects divergence in one round trip.**  Every frame
  each peer hashes a small set of sim variables and ships the hash
  alongside the input.  Mismatch for frame F is visible to the
  receiver one ping later.  The hash does not *create* sync —
  determinism does — it only detects the moment determinism has been
  violated, so the session dies fast and visibly instead of drifting
  for minutes.

If the core were non-deterministic, none of this would work and the
architecture would have to look like an authoritative game server
with client prediction and entity reconciliation (Quake / Minecraft
model).  Because the core *is* deterministic, we get away with a
~2 KB/s pipe and ~300 lines of protocol code.

---

## 3. Architecture at a glance

```
                              main loop
                              (main_sdl3.cpp:1999)
                                    │
              ┌─────────────────────┼─────────────────────┐
              │                     │                     │
              ▼                     ▼                     ▼
        SDL event poll       NetplayCoordinator      render + ImGui
                                    │
              ┌─────────────────────┼─────────────────────┐
              │                     │                     │
              ▼                     ▼                     ▼
     NetplayTransport         InputBuffer           NetplayUI
     (UDP socket,             (per-frame,           (Host / Join
     redundant input          both peers)           dialog, status)
     packets)
                                    │
                                    ▼
                    g_sim.Advance(dropFrame)
                    (main_sdl3.cpp:2068, unchanged)
```

Three new files, one modified file:

| File                                          | Role |
| --------------------------------------------- | ---- |
| `src/AltirraSDL/source/netplay/transport.cpp` | UDP socket, packet framing, redundancy |
| `src/AltirraSDL/source/netplay/coordinator.cpp` | Lockstep state machine, input buffer, hash check |
| `src/AltirraSDL/source/ui/dialogs/ui_netplay.cpp` | ImGui Host/Join dialog + status HUD |
| `src/AltirraSDL/source/app/main_sdl3.cpp`       | ~15 lines: call coordinator before/after `Advance()` |

No changes to the emulation core.  No changes to `ATInputManager` or
the joystick/keyboard SDL3 adapters.

---

## 4. The lockstep model in one picture

```
                 input-delay D = 3 frames

          wall-clock frame number ──▶
          ┌──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──
 local    │10│11│12│13│14│15│16│17│18│19│…
 capture  └──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──
            └──────┐
                   │ applied as emulation input at frame 13
                   ▼
          ┌──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──
 emu.     │  │  │  │13│14│15│16│17│18│19│…   (3-frame warm-up)
 frame    └──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──
                    ▲
                    │ advances only when BOTH peers' input
                    │ for frame 13 is in the buffer
```

Two invariants:

1. **Local input for wall-clock frame F is applied to emulation frame
   F+D.**  This gives the network D frames (≈50 ms at D=3, 60 Hz) to
   deliver that input to the remote peer.
2. **`g_sim.Advance()` for emulation frame F runs iff the buffer holds
   both peers' input for F.**  If remote input is missing, the main
   loop stalls: it polls SDL, polls the socket, renders ImGui, and
   re-checks.  It does **not** call `Advance()`.

Both peers apply the same inputs on the same emulation frames.
Determinism does the rest — they stay in sync forever with zero
explicit synchronisation beyond input exchange.

---

## 5. Wire format

UDP datagrams.  Fixed size, no allocations, little-endian.

### 5.1 Handshake (once per connection, reliable-via-retry)

The handshake exchanges identity and capability, then the host's
settings and snapshot.  **The joiner does not need to have the host's
game file** — the snapshot in §5.2 carries the cart/disk contents.
The project's terms of use require that all players own a legal copy
of any title being hosted; the emulator carries state for a session,
not a permanent copy.

```c
struct NetHello {            // joiner → host
    uint32_t magic;          // 'ANPL' = 0x4C504E41
    uint16_t protocolVersion;// 1
    uint16_t flags;          // reserved
    uint8_t  sessionNonce[16];// random, used for later control-packet auth
    uint64_t osRomHash;       // xxHash64 of joiner's OS ROM
    uint64_t basicRomHash;    // xxHash64 of joiner's BASIC ROM (or 0)
    char     playerHandle[32];// display name for lobby / status HUD
    uint16_t acceptTos;       // bit 0: joiner has accepted session ToS
};

struct NetWelcome {          // host → joiner (on accept)
    uint32_t magic;           // 'ANPW'
    uint16_t inputDelayFrames;// 2–6, host's choice
    uint16_t playerSlot;      // which joystick port (1 = P2, etc.)
    NetplaySettings settings; // netplay-relevant settings (§8.2)
    char     cartName[64];    // human label shown to joiner (e.g. "Joust.atr")
    uint32_t snapshotBytes;   // size of incoming state stream
    uint32_t snapshotChunks;  // how many chunks follow in §5.2
};

struct NetReject {           // host → joiner (on reject)
    uint32_t magic;           // 'ANPR'
    uint32_t reason;          // enum: os_mismatch, basic_mismatch,
                              //       version_skew, tos_not_accepted,
                              //       host_full, host_not_ready
};
```

**What is hash-verified and what is not.**

- **OS ROM and BASIC ROM** *must* match by hash.  Altirra ships its
  own clean-room replacements, so the default-to-default case always
  matches invisibly.  Users who run a custom OS ROM must agree on
  which one before the session — if they disagree, no simulation
  anywhere can make them converge.
- **Cartridge / disk content** is *not* compared.  It arrives inside
  the snapshot (§5.2), already loaded into the emulator's memory
  state on the host side.  The joiner never needs the file locally.
- **Emulator settings** (hardware type, RAM size, video standard,
  accelerators, …) are *not* compared.  The host owns them and ships
  them inside `NetWelcome`; the joiner applies them for the session
  (see §8.2).

**ToS acceptance.**  The first time a user joins any netplay
session, a one-shot dialog asks them to confirm the session terms:
*"By joining, you confirm you own a legal copy of any software
received during this session, and that received state will not be
extracted or redistributed."*  Accepted state is remembered in
`settings.ini`; the `acceptTos` bit in `NetHello` signals compliance.
Optionally (v2), the Online tab offers **"Save received game to my
library?"** after the session ends, gated on the same ToS
acknowledgment.

### 5.2 Initial state sync (once, after handshake)

Once the joiner has the welcome packet, the host creates a live
snapshot of its *current* running state via
`ATSimulator::CreateSnapshot()` (`simulator.h`) — not a cold-boot
snapshot.  This means the joiner resumes exactly where the host
currently is: title screen, mid-level, paused menu, whatever.  There
is no "both press Start on the same frame" ceremony.

Serialised with `IATSaveStateSerializer` (`savestateio.h:33`),
chunked over UDP with per-chunk ACKs.  This is the only packet type
in the whole protocol that uses explicit reliability, because it
happens exactly once per session and is too large to rely on
redundancy.  Typical size: 64–200 KB depending on mounted media.
Joiner applies with `ApplySnapshot()` and both peers start stepping
from the same emulation frame with bit-identical state.

### 5.3 Per-frame input packet (the hot path)

Sent by each peer every local frame, containing a sliding window of the
last `R` frames of its own input (redundancy for packet loss).

```c
struct NetInput {            // 4 bytes per frame per player
    uint8_t stickDir;        // bits: up|down|left|right (low 4 bits)
    uint8_t buttons;          // trig | start | select | option | reset
    uint8_t keyScan;          // ATUIGetScanCodeForVirtualKey() or 0
    uint8_t extFlags;         // bit 0: paddle/5200 extension follows
};

struct NetInputPacket {       // 16 + R*4 bytes ≈ 36 bytes at R=5
    uint32_t magic;           // 'ANPI'
    uint32_t baseFrame;       // emulation frame of first NetInput below
    uint16_t count;           // number of NetInput entries (== R)
    uint16_t ackedFrame;      // highest frame this peer has from the other side
    uint32_t stateHashLow32;  // rolling state hash for desync detection
    NetInput inputs[R];       // inputs for [baseFrame, baseFrame+R)
};
```

**Redundancy beats retransmit.**  At 60 Hz with `R=5`, any single
packet delivered within the last 5 frames carries that frame's input.
A burst of 4 consecutive drops is needed before a stall occurs.  No
ACKs, no sequence numbers, no retransmit queues — the receiver just
files each `NetInput` in a ring indexed by frame and discards
duplicates.  For a 4-byte-per-frame stream this is strictly simpler
than ENet and uses less bandwidth.

**Bandwidth:** 36 bytes × 60 Hz = **2.2 KB/s per direction**.  Dial-up
tolerates this.

### 5.4 Disconnect

```c
struct NetBye { uint32_t magic; uint32_t reason; };
```

Reasons: clean exit, desync detected, timeout, version mismatch.

---

## 6. Integration with the existing main loop

The relevant existing code in `src/AltirraSDL/source/app/main_sdl3.cpp`:

```cpp
// line 1999: main loop
while (running) {
    HandleEvents();                             // SDL events
    …
    AdvanceResult r = g_sim.Advance(dropFrame); // line 2068
    …
    g_pacer.WaitForNextFrame();                 // line 2121
    Render();
}
```

After the change:

```cpp
while (running) {
    HandleEvents();
    gNetplay.Poll();                            // drain socket, queue inputs

    if (gNetplay.IsActive()) {
        NetInput local = gNetplay.CaptureLocalInput();   // from ATInputManager
        gNetplay.SubmitLocalInput(local);                // buffers + sends

        if (!gNetplay.CanAdvanceEmulationFrame()) {
            g_pacer.WaitForNextFrame();                  // stall, don't Advance
            Render();
            continue;
        }

        gNetplay.ApplyInputsForCurrentFrame();           // injects into ATInputManager
    }

    AdvanceResult r = g_sim.Advance(dropFrame);          // UNCHANGED

    if (gNetplay.IsActive())
        gNetplay.OnFrameAdvanced();                      // update hash, frame++

    g_pacer.WaitForNextFrame();
    Render();
}
```

That is the entire change to the main loop.  The simulator is
unmodified.  The pacer is unmodified.  The audio path
(`IATAudioOutput`, pull-based per `main_pacer.cpp:70`) is unmodified —
when we stall on missing input the pacer still runs, so audio
underruns cleanly rather than glitching.

---

## 7. Input capture and apply

This is the only place Altirra-specific thought is required.

`ATInputManager` delivers input to controllers via `OnButtonDown/Up`
and `OnAnalogInput` throughout a frame rather than as a single
snapshot.  **We do not change that.**  Instead:

**Capture (local).**  After `HandleEvents()`, read the *current
logical state* of player-1 controller inputs directly from
`ATInputManager` — the trigger/direction bits, console switches, and
the most recent keyboard scancode.  Pack into `NetInput`.  For v1
this is joystick + console switches + one scancode — covers 95 %+ of
Atari multiplayer titles.  Paddles and 5200 keypad use the
`extFlags` extension slot in v2.

**Apply (both local-delayed and remote).**  Before calling
`Advance()`, synthesise the equivalent `OnButtonDown/OnButtonUp`
calls into `ATInputManager` for each peer's controller such that
the hardware sees identical stick/button state on both sides.
Crucially, the *local* peer does this too — local input is
suppressed from the normal SDL path and re-injected via the same
delayed route as remote input.  This keeps both peers symmetric and
removes any chance of "local feels ahead of remote."

### 7.1 Suppressing the live local path

When netplay is active, the SDL joystick/keyboard adapters
(`input_sdl3.cpp`, `joystick_sdl3.cpp`) must **not** forward events
directly to `ATInputManager` for netplay-mapped controllers.
Implement this with a single branch in those adapters that asks
`gNetplay.OwnsLocalInput()` and, if true, forwards events to the
coordinator's capture path instead of the live path.  The adapter
still handles non-netplay inputs (hotkeys, UI keys, pause) normally.

---

## 8. Determinism contract

Lockstep relies on perfect determinism.  The survey confirms Altirra
is already synchronous and deterministic (no threads in `Advance()`,
no host-clock reads in the sim loop).  The remaining work is
enforcement, not refactoring.

**At connect time, reject the session if any of these differ:**

- ROM / cartridge image hash
- OS ROM + BASIC ROM hashes
- Hardware type (800 / 800XL / 130XE / 5200)
- Video standard (NTSC / PAL)
- RAM size
- Accelerator/SIO patch settings that affect timing
- Any setting in the set tracked by `settingsHash`

The set of netplay-relevant settings is defined by a single
constant in `coordinator.cpp` — one place to update when new
settings land.

**At runtime:**

- Every frame, each peer computes a 32-bit rolling hash over CPU
  registers + a handful of key sim variables and ships it in
  `stateHashLow32`.  On mismatch for frame F (visible one round
  trip later), the coordinator stops the session, displays
  "Desync at frame F", saves both peers' snapshots to
  `~/.config/altirra/netplay_desync_<ts>.astate` for post-mortem,
  and returns to the main menu.
- A cheap hash (not a full state hash) is sufficient to catch
  divergence within a frame or two — the cost of a full hash per
  frame is unnecessary overhead.

**Determinism tripwires to check before shipping (weekend's work):**

1. Host two sim instances in the same process from the same
   snapshot with the same input stream for 60 seconds; assert
   identical state hashes every frame.  This is the test that
   decides whether v1 ships.
2. Repeat across a host-loopback socket to confirm the serialise
   path is lossless.
3. Run the same test with the audio subsystem enabled to rule out
   audio-driven non-determinism.

If tripwire (1) passes, everything downstream is plumbing.
If it fails, the failing subsystem is the single thing that has to
be fixed before netplay can ship — not the whole emulator.

---

## 9. UI

One dialog, three states.

**Netplay menu item** under a new top-level *Netplay* menu in
`ui_menus.cpp` (pattern per the existing `File` / `Tools` menus):

```
Netplay ▸
  Host Session…
  Join Session…
  ─────────────
  Disconnect           (disabled unless connected)
```

**Host dialog** — single modal centred per the CLAUDE.md dialog
rule (`SetNextWindowPos(..., Appearing, 0.5f, 0.5f)` +
`NoSavedSettings`):

```
  ┌─ Host Netplay Session ──────────────────┐
  │  Port:          [26101]                  │
  │  Input delay:   [3] frames (≈50 ms)      │
  │                                           │
  │  Your address: 192.168.1.42:26101         │
  │               [Copy to clipboard]         │
  │                                           │
  │  Status: Waiting for joiner…              │
  │                                           │
  │                     [Cancel]  [Start]     │
  └───────────────────────────────────────────┘
```

**Join dialog:**

```
  ┌─ Join Netplay Session ──────────────────┐
  │  Host address: [________________]        │
  │  Example: 192.168.1.42:26101              │
  │                                           │
  │                     [Cancel]  [Connect]   │
  └───────────────────────────────────────────┘
```

**Connected status HUD** (ImGui overlay, top-right, toggle via View
menu like the existing FPS HUD):

```
  NETPLAY · 42 ms · frame 3841 · delay 3 · OK
```

`OK` turns amber on >5 % packet loss in last second, red on stall,
shows desync frame on failure.

---

## 10. Reaching other households (the networking reality)

Direct IP connect works between arbitrary households **when the host
can receive unsolicited inbound UDP on the chosen port**.  In 2026
home networking that is true for a sizeable majority of users, but
not all.  The plan is honest about this rather than hiding it.

**Who can host v1 successfully:**

| Host situation | Works? |
| -------------- | ------ |
| Real public IPv4 + UDP port forwarded on router | ✅ Yes |
| IPv6 at both ends, host router permits inbound | ✅ Yes |
| Router supports UPnP-IGD and it is enabled | ✅ Yes (once v1.1 adds a UPnP helper) |
| Host behind CGNAT (shared-IPv4 ISP, common on mobile/fibre) | ❌ No, ever, by any means without a third party |
| Host cannot or will not configure router | ❌ No, unless UPnP works |

**Joining** is almost always fine — outbound UDP is unrestricted on
essentially all consumer connections, and return traffic flows back
through the NAT that the outbound created.

In practice, roughly 50–70 % of users worldwide can act as host for
v1.  The practical workaround for the rest: **whichever of the two
friends can port-forward becomes the host.**  As long as one of them
has a real public IP and can spend thirty seconds in the router
admin UI, the session works — and most pairs of friends only ever
need to do this once.

**What v1 does to be kind about this:**

1. The Host dialog displays both the LAN address and the detected
   public address, obtained from a one-shot STUN query to a public
   endpoint (e.g. `stun.l.google.com:19302`).  This uses only a
   few bytes and requires no server of our own — STUN is purely a
   mirror that reflects back the caller's public `ip:port`.
2. If the detected public address falls in CGNAT-reserved ranges
   (`100.64.0.0/10`) or otherwise looks carrier-mapped, show an
   inline warning:
   *"Your ISP appears to use CGNAT.  Direct hosting will not work —
   ask your friend to host the session instead, or use a VPN."*
3. If a connection attempt fails within 10 s, show a specific
   error naming the three likely causes (port not forwarded,
   firewall blocking UDP, host on CGNAT), not a generic "could not
   connect."

**What v2 adds** to reach the remaining users (both peers behind
CGNAT, double-NAT, or symmetric NAT) is built on top of the
federated lobby system in §11 — the rendezvous endpoint is a lobby
server, and any lobby operator can advertise relays:

- **Hole-punching rendezvous** via the lobby's `/v1/session/{id}/join`
  endpoint (§11.2).  Each peer publishes its public `ip:port`; the
  lobby hands each side the other's, and both start sending UDP.  This
  works for ~85–90 % of NAT pairs in the wild, at the cost of a few
  extra bytes of lobby traffic — no change to the on-wire protocol
  after the handshake starts.
- **TURN-style relay fallback** for the remaining ~10 %.  Open-source
  `coturn` is the reference relay; lobby listings include relay
  endpoints operators have deployed alongside their lobby.  Adds
  ~20–40 ms of latency but always works.

The v1 wire protocol (§5) is unchanged — the rendezvous step exchanges
connection details *before* the handshake starts, and the relay is a
transport substitution under the same packet format.  See §11 for the
full federation model and the rationale for not running a single
"official" server.

---

## 11. Discovery and federated lobbies

Running a single "official" lobby server is a lifetime commitment —
someone pays for a VPS, someone handles abuse, someone keeps the
service alive for a decade.  That is not the right shape for a
community emulator project.

**The federation model**, borrowed from IRC, Matrix, and the
Fediverse, removes that commitment.  The client does not know about a
single canonical lobby; it queries a **list** of lobby servers in
parallel and merges the results.  If the "official" one goes down,
community-hosted ones keep the lights on.  If a speedrun group wants
their own private lobby, they run one and tell friends to add the URL.
Nobody owns the switch; nothing single-points-of-failure.

### 11.1 The `lobby.ini` file

Shipped alongside `settings.ini` under the user's config directory:

```ini
# lobby.ini — lobby servers this Altirra queries.  Add / remove /
# reorder freely.  The Netplay menu's "Edit lobby list…" wraps the
# same file in an ImGui dialog for users who don't want to edit text.

[official]
name    = Altirra Official Lobby
url     = https://lobby.altirra.example
region  = global

[community-eu]
name    = Altirra EU (community)
url     = https://altirra-eu.example.org
region  = europe

[lan]
name      = LAN
transport = udp-broadcast
port      = 26101
```

Defaults shipped with the build: one or two "official" entries
(operated by whoever runs the project at the time) plus the LAN
entry (no URL — it's UDP broadcast on the local subnet, zero
infrastructure).

### 11.2 The lobby protocol

Deliberately trivial HTTP/JSON so **anyone can run one in a
weekend**.  The reference implementation lives in
`server/altirra-lobby/` in this repo under the project licence; all
community servers fork from there.

```
POST   /v1/session                register a session
  { cartName, hostEndpoint, region, protocolVersion, hostHandle,
    playerCount, maxPlayers }
  → { sessionId, ttlSeconds }

GET    /v1/sessions               list active sessions
  → [{ sessionId, cartName, hostHandle, playerCount, maxPlayers,
       region, protocolVersion }]

POST   /v1/session/{id}/join      rendezvous (publish joiner endpoint,
                                   receive host endpoint)
  { joinerEndpoint }
  → { hostEndpoint, nat_traversal_hint }

POST   /v1/session/{id}/heartbeat extend TTL, update player count
DELETE /v1/session/{id}           clean shutdown
```

No accounts, no persistent storage, no chat.  A lobby is a **session
directory** with hole-punching rendezvous — nothing more.  Sessions
expire after 2 × heartbeat interval (default 60 s).

A lobby **never** sees game traffic.  Inputs, snapshots, cart content
— none of it touches the lobby.  The attack surface is a list of
`(cartName, ip:port)` pairs with short TTLs.  Very little to attack,
very little to abuse.

### 11.3 Hosting reality

Per-lobby workload is tiny: 100 concurrent sessions × 1 heartbeat /
30 s ≈ **0.7 KB/s**, memory footprint ~5 MB, zero persistent storage.

Suitable hosts, in rough order of recommendation:

- **Hetzner €3/month CAX11** — reliable, no free-tier-reclaim risk.
- **Oracle Cloud Always Free** (ARM64: 4 OCPU / 24 GB RAM / 10 TB
  egress) — absurdly generous for this workload, but account-lock
  reports exist; fine for community operators who accept the risk.
- **fly.io / Scaleway / Render free tiers** — all adequate.
- **Raspberry Pi at home** — works if the operator can expose a
  port; perfect for LAN-party groups.
- **GitHub Pages / static hosting** — *not* viable; lobbies need a
  live process.

The plan deliberately does not prescribe a host.  Publish spec +
reference implementation, ship a small default list, let the
community pick infrastructure.  This makes the project **robust to
any single operator walking away**, including the project leads.

### 11.4 Visualising lobbies in the UI

The Game Library's **Online** tab renders an aggregated list across
all configured lobbies, with a source badge per row so users see
*which directory* is vouching for each listing:

```
┌─ Online Sessions ──────────────────────────────────────────────┐
│ Querying 3 lobby servers…                        [Manage…]     │
│                                                                 │
│  Source      Host        Game          Players  Region  Ping   │
│  ───────────────────────────────────────────────────────────── │
│  Official    Alice       Joust           1/2    EU      42 ms  │
│  Official    Bob         M.U.L.E.        3/4    NA      89 ms  │
│  EU Comm.    Carlos      Ballblazer      1/2    EU      35 ms  │
│  LAN         Dana's PC   Mario Bros      1/2    —        2 ms  │
│                                                                 │
│               [Refresh]              [Host New Session]         │
└─────────────────────────────────────────────────────────────────┘
```

Per-lobby status icons in the bottom-right (green reachable / amber
slow / red unreachable).  One-click disable per lobby without
removing it from `lobby.ini`.  "Manage…" opens the `lobby.ini` editor
dialog; advanced users edit the file directly.

### 11.5 Trust and moderation

A malicious lobby can advertise fake or offensive session titles —
that is the full attack surface.  Mitigations are structural, not
infrastructural:

- The source badge names *which lobby* is advertising each entry.
  Users vote with their feet on which directories they trust.
- On actual connect, the real handshake (§5.1) and per-frame state
  hash (§8) guarantee the session is genuine.  A lobby **cannot**
  make a desynced or fraudulent session appear to work.
- One-click disable per lobby in the UI.
- Each operator moderates their own listings — classic federation
  model.  Nobody is the single bottleneck for abuse reports.

This is strictly better than a single-operator model where every
report lands on one person and burnout ends the service.

### 11.6 Rollout across versions

| Version | Discovery | Infrastructure required |
| ------- | --------- | ----------------------- |
| **v1.0** | Manual IP + in-game Host/Join | **None** |
| **v1.5** | + LAN `[lan]` entry (UDP broadcast) | None |
| **v2.0** | + `lobby.ini`, Online tab, reference server | **Community-run** (one or more small VPSes) |

v1.0 and v1.5 ship with zero ongoing operational cost.  v2.0 requires
*someone* running at least one lobby server for the public list to be
useful — but "someone" doesn't have to be the Altirra project itself,
and if it is, one lobby going down doesn't break netplay.

### 11.7 The reference server and joining the federation

The federation only works if standing up a lobby is genuinely *easy*
— easier than spinning up a Firebase project or any other proprietary
backend.  The reference server is engineered to that bar.

**Language and shape:** ~150 lines of Go using stdlib only (no
external Go modules).  Cross-compiles to a single static binary with
`go build`; no runtime, no dependencies, no `pip install` / `npm
install` rabbit hole.  Sessions are held in memory — if the lobby
restarts, hosts re-register on their next heartbeat (within 30 s),
so no database is needed.  Memory footprint: ~5 MB at 1000 sessions.

**Sketch of the entire server:**

```go
// server/altirra-lobby/main.go
package main

import ( "encoding/json"; "net/http"; "sync"; "time" )

type Session struct {
    ID, CartName, HostHandle, HostEndpoint, Region string
    PlayerCount, MaxPlayers, ProtocolVer            int
    LastSeen                                        time.Time
}

var (
    mu       sync.RWMutex
    sessions = map[string]*Session{}
)

func main() {
    go expireLoop()
    http.HandleFunc("/v1/sessions",  listSessions)
    http.HandleFunc("/v1/session",   createSession)
    http.HandleFunc("/v1/session/",  sessionRouter)  // join/heartbeat/delete
    http.ListenAndServe(getEnv("BIND", ":8080"), nil)
}

func expireLoop() {
    for range time.Tick(30 * time.Second) {
        mu.Lock()
        cutoff := time.Now().Add(-90 * time.Second)
        for k, s := range sessions {
            if s.LastSeen.Before(cutoff) { delete(sessions, k) }
        }
        mu.Unlock()
    }
}
```

**Distribution artefacts** shipped from `server/altirra-lobby/`:

- **Prebuilt binaries** in GitHub Releases for `linux-amd64`,
  `linux-arm64`, `darwin-arm64`, `darwin-amd64`, `windows-amd64`.
  Static, ~6 MB each.
- **`Dockerfile`** and **`docker-compose.yml`** for container
  operators; image size ~10 MB.
- **`altirra-lobby.service`** systemd unit for VPS operators.
- **One-click deploy buttons** in the README: *Deploy to Render*,
  *Deploy to Railway*, *Deploy to fly.io* — each free-tier
  compatible, no billing card required.
- **Operator quickstart** in `server/altirra-lobby/README.md`
  covering all of the above on one page.

**The easiest operator path is three lines:**

```sh
$ wget https://github.com/…/releases/download/v1.0/altirra-lobby-linux-amd64
$ chmod +x altirra-lobby-linux-amd64
$ ./altirra-lobby-linux-amd64
listening on :8080, max sessions 1000, ttl 90s
```

That is the entire setup.  The operator now has a lobby.

**No server-to-server protocol.**  Lobbies do not talk to each other.
Federation happens entirely in the client, which queries every URL in
`lobby.ini` in parallel and merges the results (§11.4).  This is a
deliberate simplification — email federation (SMTP) and Matrix
federation are 10× more complex because their messages must *flow
between* servers.  Ours don't.  Each lobby is a standalone directory;
the client is the only thing that needs to know about all of them.

**Joining the federation — three levels of formality.**  The
operator picks based on how public they want the lobby to be:

1. **Friends-only.**  Run the binary, send the URL to friends via
   Discord / chat, they paste it into Altirra's *Edit lobby list…*
   dialog.  Done.  Nothing is registered anywhere.
2. **Public unlisted.**  Post the URL on the Altirra forum,
   subreddit, or a Discord channel.  People who see it add it.
   Organic discovery.
3. **Community list.**  Open a PR adding the lobby to
   `community-lobbies.md` in the Altirra repo.  Once merged, the
   lobby appears in the client's *"Add community lobby…"* picker
   (which fetches the raw markdown over HTTPS at runtime).  PR
   review by project leads is the only moderation choke point — and
   it is append-only, version-controlled, and inherently auditable.

There is no API key, no certificate exchange, no DNS record we
issue, no "registration" anywhere.  **The federation is a markdown
file plus an HTTP API** — that is the entire system.

---

## 12. What is explicitly out of scope for v1

Each of these is a legitimate feature.  None is necessary for a
first working release, and including them multiplies the
integration and testing cost.  They are listed here with the
cleanest extension point so v2 does not require restructuring.

| Feature | Extension point | Status |
| ------- | --------------- | ------ |
| 4-player sessions | `InputBuffer` already keyed by `(frame, playerIndex)`; just raise `kMaxPlayers` from 2 | Deferred — very few Atari titles use 4 joysticks |
| UPnP / NAT-PMP automatic port mapping | New optional helper in `transport.cpp` | **Planned v1.1** |
| LAN discovery (UDP broadcast) | `[lan]` entry in `lobby.ini` (§11) | **Planned v1.5** |
| Federated lobby client | HTTP/JSON query per §11.2 | **Planned v2.0** |
| Reference lobby server | `server/altirra-lobby/` in repo | **Planned v2.0** |
| STUN-style UDP hole punching | Uses lobby rendezvous per §11.2 | **Planned v2.0** |
| TURN-style relay fallback | Transport learns a "send via relay" mode, lobby serves relay list | **Planned v2.1** |
| Spectator mode | Transport broadcasts inputs to an extra read-only peer | Deferred — requires thought on mid-session join |
| Rollback | Per-frame snapshot + resim audit | **Not planned** — see §1 |
| Save/load during session | Host sends new snapshot, joiner re-applies | Deferred — non-trivial pause semantics |
| "Save received game to library" post-session | ImGui prompt after session end, writes to `~/.config/altirra/netplay_received/` with ToS re-confirmation | Deferred — v2 after lobby |

Items marked "Planned" have a committed extension point and version.
"Deferred" items are worth building but not scheduled until user
demand justifies the work.  "Not planned" items are actively rejected
for reasons explained in §1 and §14.

---

## 13. Implementation order

Each phase ends in a demonstrable, shippable increment.  Do not
skip ahead.  Phase 1 is the real go/no-go gate for the whole effort.

### v1.0 — direct IP, playable

1. **Loopback determinism test.**  Two `ATSimulator` instances in
   one process, fed an identical scripted input trace, assert
   matching state hash every frame for 60 seconds.  If this fails,
   netplay cannot ship until it passes.
2. **Transport skeleton.**  `transport.cpp` with `Listen` /
   `Connect` / `Send` / `Poll`.  Unit-testable via loopback.
3. **Handshake + snapshot transfer.**  Both peers reach "ready"
   state with identical sim state; no emulation yet.
4. **Lockstep stepping, no input.**  Both peers step frames in
   lockstep driven by heartbeat packets; prove the stall logic
   and frame-hash exchange work with zero input traffic.
5. **Input capture / apply.**  Add `NetInput` exchange; play
   Mario Bros co-op across `lo0`.  First genuinely playable build.
6. **In-game UI.**  Host / Join dialogs (§9), status HUD,
   disconnect.
7. **Game Library integration.**  Right-click → "Host Online"
   on a library entry; boot + open listener in one action.
8. **Desync reporting.**  Snapshot dump on mismatch, friendly
   error message.
9. **LAN / internet testing** with a second machine.  Tune
   default input delay.  Ship v1.0.

### v1.1 — UPnP convenience

10. **UPnP / NAT-PMP port mapping.**  Best-effort automatic port
    open on routers that support it; fall back to manual
    forwarding message.

### v1.5 — LAN discovery

11. **UDP broadcast discovery.**  `[lan]` section of `lobby.ini`
    implemented; same-subnet sessions appear in the Library's
    Online tab with a "LAN" source badge.  Zero infrastructure.

### v2.0 — federated lobbies

12. **Lobby protocol + reference server.**  `server/altirra-lobby/`
    Go implementation landing in the repo; protocol spec in
    `PORTING/NETPLAY_LOBBY_PROTOCOL.md`.
13. **Lobby client.**  Parallel HTTP queries across `lobby.ini`
    entries, merged display in the Online tab, per-lobby status.
14. **Lobby editor UI.**  ImGui dialog wrapping `lobby.ini`.
15. **STUN / hole-punching rendezvous** via lobby `/join`
    endpoint.

### v2.1 — relay fallback

16. **TURN-style relay.**  `coturn` as reference relay; lobby
    advertises known relays; transport substitutes relay path
    when direct connect fails.

Phases 1–5 are each ~1–3 days; phases 6–9 total ~1 week.
Realistic v1.0 timeline: **2–3 weeks of focused effort**, with
phase 1 as the single make-or-break gate.  Everything past
v1.0 is incremental.

---

## 14. Why not GGPO, ENet, EOS, …?

| Library | Verdict |
| ------- | ------- |
| **GGPO / GGRS / backroll** | Rollback.  Wrong netcode model for Atari (§1).  Would also require proving microsecond-scale save/load of the full Altirra state — weeks of work for no perceptible benefit in co-op. |
| **ENet** | MIT, GPL-compatible, fine.  But its value (reliable ordered UDP, channels) is orthogonal to our problem: a 4-byte-per-frame input stream with redundancy needs neither ordering nor reliability.  Adding ENet means a new vendored dependency and build-system work for code we do not need. |
| **GameNetworkingSockets** | Same as ENet, heavier.  Its relay features (SDR) are Steam-only. |
| **Epic Online Services** | Proprietary SDK — linking it into GPL v2+ code is a licence violation regardless of cost.  Same applies to Steamworks and Discord GameSDK.  This is not a footnote; it rules the option out entirely. |
| **Nakama** | Full game-backend platform (accounts, matchmaking, storage, chat, leaderboards).  Vastly more than we need: our v2 lobby is a session directory + rendezvous, nothing more.  Nakama's footprint (~GB of Docker images, Postgres dependency) makes it unattractive for a volunteer running a community lobby on a Raspberry Pi.  We ship our own ~50-line protocol instead — see §11. |
| **Raw POSIX sockets (our choice)** | Already in the codebase for test/bridge IPC; zero new dependencies; license-clean; the protocol in §5 is under 300 lines. |

---

## 15. Open questions to decide before coding

These need user-level decisions, not more investigation:

1. **Default input delay.**  Proposal: 3 frames on LAN, 4 on
   internet.  User-adjustable 2–6.
2. **Should the host being paused pause the joiner?**  Proposal:
   yes — pause is a session-wide state exchanged as a control
   packet.  Avoids the "why is the other player frozen" class
   of bug.
3. **What happens on single-player pause (save/load, config
   change) during a session?**  Proposal: disallow.  Config
   dialog and state load disabled while netplay is active; grey
   the menu items with a tooltip.
4. **Does cold/warm reset propagate?**  Proposal: yes, as a
   control packet; both peers reset in sync.  Issued only by
   the host.

None of these is hard; they just need a default picked.

---

## 16. Summary

- Lockstep with input delay, not rollback.
- Direct UDP, redundant input packets, no ACKs on the hot path.
- One snapshot transfer at connect via the existing
  `IATSerializable` API.
- ~15 lines changed in the main loop; emulation core untouched.
- No third-party libraries, no servers, GPL-clean.
- Phase 1 determinism test decides the whole project; everything
  after is plumbing.

That is the simplest properly working design.  Anything smaller
breaks; anything larger is v2.
