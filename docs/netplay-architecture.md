# AltirraSDL netplay — architecture and protocol

Companion to the user guide [`NETPLAY.md`](../NETPLAY.md). Where the
user guide explains *how to play*, this document explains *how it
works*: the topology, the wire formats, the lockstep math, and the
known limitations and restructuring directions.

This is a working reference for anyone touching code under
`src/AltirraSDL/source/netplay/` or `server/lobby/`.

---

## 1. Mental model

Two emulators, both running the same Atari machine configuration with
the same ROMs, advance their simulators in **deterministic lockstep**:
each peer must have the other peer's input for frame F before either
side commits frame F. Because the emulator is cycle-deterministic
given identical inputs, both screens stay pixel-identical
indefinitely.

What goes over the wire:
- Once: a snapshot (the host's current simulator state, ZIP-packaged)
  so the joiner starts at the same point.
- Continuously: per-frame inputs, ~36 bytes per peer per frame.
- Periodically: heartbeats and observability events.

What does **not** go over the wire:
- Game state. Both peers compute it locally from inputs.
- ROMs / firmware / cartridge content. Peers verify they hold the
  same `kernelCRC32` / `basicCRC32` and assume identical bits.
- Disk image content. Peers must mount the same image locally; only
  drive *runtime state* travels in the snapshot.

This is the same model AoE2, StarCraft, and Factorio use, scaled down
to two peers.

---

## 2. Components

| Component | Role | Code |
|---|---|---|
| **Peer (host or joiner)** | Runs the emulator + lockstep loop. | `coordinator.cpp`, `lockstep.cpp`, `netplay_input.cpp` |
| **Lobby server** | HTTP session directory. Never sees gameplay. | `server/lobby/` |
| **UDP reflector** | STUN-lite. Tells a host its public IP:port. | `server/lobby/` (reflector port, default 8081) |
| **WSS relay** | Browser bridge. Tunnels datagrams when UDP isn't possible. | Lobby-integrated; activated for WASM hosts |
| **Transport layer** | UDP socket, NAT-PMP/PCP, candidate gathering. | `transport.cpp` |

All gameplay traffic is **peer-to-peer UDP** (or peer-to-relay-to-peer
for WASM/CGNAT cases). The lobby is a directory and never carries
input streams at game rates.

---

## 3. Roles and the slot model

The lockstep loop has two **slots**:

```cpp
enum class Slot : uint8_t { Host = 1, Joiner = 2 };
```
*(`lockstep.h:44-47`)*

- **Host** advertises the session, ships the snapshot, and is the
  authoritative source for the game-config + initial seed.
- **Joiner** receives the snapshot, applies it, and joins the lockstep.

Once both are in lockstep, the roles are symmetric — neither side
"owns" the simulation, both compute it identically from the merged
input stream. This is why the protocol is mesh-shaped today: it has
exactly two participants and they're equal partners.

---

## 4. Wire protocol

### 4.1 Constants

```cpp
constexpr int      kRedundancyR        = 5;       // input-window length
constexpr uint32_t kSnapshotChunkSize  = 1200;    // bytes per NetSnapChunk
constexpr size_t   kWireInputSize      = 5;       // bytes per NetInput (v7)
constexpr size_t   kWireInputPktSize   = 41;      // NetInputPacket size (v7)
constexpr uint32_t kRingSize           = 256;     // input ring buffer depth
uint16_t           inputDelayFrames    = 3;       // initial D (ratchets up; see §11.4)
constexpr uint16_t kProtocolVersion    = 7;       // v7: dynamic-D ratchet
```
*(`packets.h:149-151,304,564`, `lockstep.h:55`)*

All multi-byte integers are **little-endian on the wire**. Each packet
type starts with a 4-byte ASCII magic (`'A','S',...`) so a malformed or
foreign datagram can be cheaply rejected.

### 4.2 Packet families

Grouped by purpose (canonical reference: `packets.h`):

**Handshake**
- `NetHello` (90 B) — joiner → host. Includes joiner handle, advertised
  `boot config hash`, advertised firmware CRCs, entry-code hash.
- `NetWelcome` — host → joiner. Carries `inputDelayFrames`,
  snapshot CRC, `NetBootConfig` (kernel/basic CRC, hardware mode,
  video standard, memory mode), and the snapshot byte length.
- `NetReject` — host → joiner. Reason codes (firmware mismatch,
  wrong code, host full, host paused, etc.).

**Snapshot delivery (one-shot at session start, or on resync)**
- `NetSnapChunk` — host → joiner. 1200-byte payload + sequence number.
- `NetAck` — joiner → host. Sliding window ACKs (window size 32
  chunks; see `snapshot_channel.h`).
- `SnapSkip` — joiner → host. "I already have this game locally,
  skip the snapshot bytes." Lets known-good local copies bypass
  transfer.

**Lockstep**
- `NetInputPacket` (36 B) — both directions, every frame. Carries the
  last R=5 frames' inputs as a sliding window — any single packet
  loss is invisible because the next packet repeats the missing frame.
- `NetResyncStart` — host → joiner. Begins a mid-session savestate
  retransmit (after a desync). Followed by `NetSnapChunk`s.
- `NetResyncDone` — joiner → host. "Snapshot applied, resume."

**Observability (v6, broadcast-friendly)**
- `NetPhase` (12 B) — phase transitions + heartbeat. Sent on state
  change and as a keepalive when no other traffic.
- `NetEventBatch` (≤230 B) — per-frame structured events (joins,
  leaves, pauses, emotes, etc.).
- `NetHeartbeat` (16 B) — RTT + loss telemetry. **RTT is measured
  here but currently only used for diagnostics**, not fed back into
  `inputDelayFrames` (see §10).

**Control**
- `NetBye` — clean disconnect.
- `Emote` — fire-and-forget UI reaction.

**NAT traversal / relay**
- `NetPunch` — STUN-lite probe used during candidate selection.
- `RelayRegister` — peer → lobby relay. "I'm at this session ID,
  forward my datagrams."
- `RelayData` — peer ↔ lobby relay envelope. Wraps any of the above
  packets when UDP can't be used directly.

### 4.3 Cap on datagram size

```cpp
constexpr size_t kMaxDatagramSize = kWireChunkHdrSize + kSnapshotChunkSize;
```
*(`packets.h:585`)* — fits comfortably under any reasonable MTU.

---

## 5. Lockstep mechanics

### 5.1 Input delay D

The host sets `inputDelayFrames = D` at session start (default **D=3**)
and ships it in `NetWelcome`. Both peers freeze D for the session
(`coordinator.cpp:95,1724,2338`).

Locally captured inputs are stamped to **emu frame `currentFrame + D`**,
not the current frame:

```
wall frame F:        emulate frame F using inputs captured at frame F-D
                     +   capture local inputs and key them at frame F+D
                     +   send those inputs over the wire
```

D is the "give the network time to deliver" budget. With D=3 at 60 fps,
each peer has ~50 ms to get its inputs to the other side. That's
adequate for LAN-quality 2-peer sessions and tight on real internet
links; see §10 for the dynamic-D discussion.

### 5.2 Ring buffer

```cpp
static constexpr uint32_t kRingSize = 256;
```
*(`lockstep.h:55`)* — large enough to absorb input delay + redundancy
window + jitter without ever wrapping into committed history. Two ring
buffers per peer hold local + peer inputs and per-frame hashes.

### 5.3 Per-frame hash

Each peer computes an FNV-1a 64-bit rolling hash of canonical
simulator state at the end of every frame. The low 32 bits are
folded into the input packet (the next-most-recently-sent input
frame, not the immediate one — that one is still in flight).

When a peer receives the other's input for frame F, it compares its
own stored hash for F against the peer's reported hash. Mismatch =
**desync detected**, escalating to:

1. Log the divergence (`coordinator.cpp:1139-1142`).
2. Trigger a mid-session resync: host snapshots, ships via
   `NetResyncStart` + chunked `NetSnapChunk`, joiner applies and
   sends `NetResyncDone`.
3. Resume lockstep at a fresh frame.

Repeated desyncs ("flap") within a short window terminate the
session — repeated resync cost > playable.

### 5.4 Redundancy (R=5)

Each `NetInputPacket` carries the last 5 frames of inputs, not just
the most recent:

```cpp
NetInput inputs[kRedundancyR] = {};   // packets.h:347
```

A single dropped packet is invisible — the next packet repeats the
missing frame. Two consecutive drops cost one frame of lockstep
hesitation. Five consecutive drops require retry (rare on UDP unless
the link is genuinely broken).

---

## 6. Session lifecycle

```
┌────────────────────────────────────────────────────────────────────┐
│ HOST                                          JOINER                │
│                                                                    │
│ POST /v1/session ──────────► lobby                                  │
│                              ◄────── GET /v1/sessions               │
│                                                                    │
│ (gather candidates)                  pick session, gather candidates│
│                                                                    │
│ ◄──────────────────── NetHello (sprayed to all candidates) ───────  │
│                                                                    │
│ ──── NetWelcome ─────────────────────────────────────────────────►  │
│                                                                    │
│ ──── NetSnapChunk × N ──────────────────────────────────────────►   │
│                              ◄────── NetAck (sliding window)        │
│                                                                    │
│ ◄────── ColdReset, both sides apply snapshot, enter Lockstepping    │
│                                                                    │
│ ╔════ LOCKSTEP ════════════════════════════════════════════════════╗│
│ ║  ──── NetInputPacket (every frame, R=5 redundancy) ────────────►║│
│ ║  ◄──── NetInputPacket (every frame, R=5 redundancy) ────────────║│
│ ║                                                                 ║│
│ ║  ──── NetHeartbeat (every ~1 s) ────────────────────────────────║│
│ ║  ──── NetPhase (state changes) ─────────────────────────────────║│
│ ║                                                                 ║│
│ ║  on desync: NetResyncStart + chunked snapshot + NetResyncDone   ║│
│ ╚═════════════════════════════════════════════════════════════════╝│
│                                                                    │
│ ──── NetBye ──────────────────────────────────────────────────────►│
└────────────────────────────────────────────────────────────────────┘
```

The Coordinator state machine (`coordinator.h:7-26`) walks: `Idle →
Connecting → SendingSnapshot → Lockstepping → (Resyncing) →
Disconnected`.

---

## 7. Lobby protocol

The lobby is a small HTTP+JSON service. Reference implementation in
`server/lobby/`. Full schema in `lobby_protocol.h`. Endpoints:

| Endpoint | Method | Purpose |
|---|---|---|
| `/v1/session` | POST | Host announces (returns sessionId + token). |
| `/v1/session/{id}` | DELETE | Host retracts. |
| `/v1/session/{id}/heartbeat` | POST | Host keepalive (every 30 s). |
| `/v1/sessions` | GET | Joiner browses (every ~10 s). |
| `/healthz` | GET | Liveness check. |
| UDP `:8081` (reflector) | datagram | STUN-lite, host learns its public IP:port. |

### Session record (v2 schema, since 2025)

```
sessionId        : opaque host-assigned ID
hostHandle       : display name
cartName         : game title (for UI)
region           : free-form ("global", "eu", ...)
state            : "waiting" | "playing"
playerCount      : current count
maxPlayers       : ceiling (kMaxPlayersLimit=8, currently cosmetic)
hostEndpoint     : primary IP:port
candidates       : ";"-separated fallback endpoints (LAN, srflx, mapped)
wssRelayOnly     : true if host has no UDP path (typical: WASM build)
visibility       : "public" | "private"
requiresCode     : entry-code hash (for private sessions)
kernelCRC32      : Atari OS ROM fingerprint
basicCRC32       : Atari BASIC ROM fingerprint
hardwareMode     : 800 / XL / XE / 5200 enum
videoStandard    : NTSC / PAL / SECAM
memoryMode       : 64K / 128K / 320K / 1088K …
```

### TTL and rate limits

- Default session TTL = 90 s. Hosts heartbeat every 30 s.
- Per-source-IP token bucket: burst 120, refill 1/s.
- Default `MAX_SESSIONS = 1000` per lobby.

The lobby is **stateless and in-memory** — restart loses sessions but
hosts re-advertise within one heartbeat. No database; no auth beyond
per-session tokens. A $5/month VPS comfortably hosts hundreds of
concurrent sessions.

---

## 8. NAT traversal

The lobby never proxies game traffic by default — once the joiner
picks a session, the two emulators talk UDP **directly**. Both peers'
routers must therefore agree to let UDP through.

The client uses the same combination of techniques BitTorrent /
WebRTC / Skype rely on:

### 8.1 NAT-PMP / PCP (RFC 6886, RFC 6887)

The host politely asks its router to install a temporary
external→internal UDP forwarding rule. When this succeeds (Apple
AirPort, OpenWrt, pfSense, many ASUS / TP-Link / Netgear out of the
box), the host's public endpoint is rock-solid — no hole-punching
required.

### 8.2 Multi-candidate advertisement

The host publishes every endpoint a joiner might reach it at:

| Candidate | Source |
|---|---|
| Router-mapped | NAT-PMP/PCP success |
| LAN | local interface address |
| Server-reflexive (srflx) | from the lobby's UDP reflector probe |
| Loopback | for same-machine testing |

These ride in the lobby's `candidates` field as a `;`-separated list.

### 8.3 Spray and lock

The joiner sends `NetHello` to **every** candidate in parallel for up
to 15 s. It locks onto whichever endpoint responds first. This makes
transient packet loss invisible and avoids guessing which endpoint
will succeed.

### 8.4 UDP reflector wire format

```
Request  (8 B): 'A' 'S' 'D' 'R' + 4 B little-endian transaction ID
Response (24 B): magic + txid + family(=4) + pad
                + port (big-endian uint16) + IPv4 (big-endian uint32)
                + 8 B reserved
```

One probe per host session. ~30 bytes total — negligible.

### 8.5 Coverage in practice

| Scenario | Works | Winning candidate |
|---|---|---|
| Both peers on same LAN | ✓ | LAN |
| Both peers on same machine | ✓ | Loopback |
| Host's router supports NAT-PMP/PCP | ✓ | router-mapped |
| Host has manual port-forward | ✓ | srflx or mapped |
| Host behind full-cone NAT, no NAT-PMP | usually ✓ | srflx |
| Host behind symmetric NAT / CGNAT | ✗ → use WSS relay |  |

For the last case the user can either set up port-forwarding manually
or rely on the WSS relay path (§9).

---

## 9. WSS relay path

Some peers cannot use UDP at all:

- **WASM/browser hosts**: browsers expose WebSocket, not raw UDP.
- **CGNAT / corporate networks**: outbound UDP to arbitrary ports is
  blocked.

For these, the lobby acts as a **datagram relay** over WebSocket
Secure (`coordinator.cpp:115-475`). Mechanics:

- Host marks itself `wssRelayOnly = true` in its session record.
- Joiner detects this flag and opens a WSS to the lobby keyed by
  session ID + token.
- All packets get wrapped in `RelayData` envelopes and tunneled
  through the lobby. The wire format below is unchanged; only the
  transport changes.
- Native joiners use a "relay-from-T=0" path; WASM joiners use
  WSS at both ends.

Cost: the lobby pays the gameplay bandwidth (~6 KB/s per session per
direction) instead of just the directory bandwidth. The community
lobby today handles this happily; a fully relay-loaded lobby would
saturate sooner.

The `NetPunch` packet still tries direct UDP first. Relay is a
fallback, not a default.

---

## 10. Determinism contract

For lockstep to hold, both peers must produce **bit-identical
simulator state** given identical inputs. AltirraSDL's deterministic
contract:

1. Both peers cold-boot with the same locked random seed (host's seed
   ships in `NetWelcome`).
2. Both peers have firmware with matching `kernelCRC32` and
   `basicCRC32`. The joiner refuses to connect if it doesn't.
3. Both peers run with the same `hardwareMode`, `videoStandard`, and
   `memoryMode` (frozen in `NetBootConfig`).
4. Both peers apply the same snapshot bytes (CRC verified).
5. Inputs from both sides are merged in the same canonical order
   (see `netplay_input.cpp:372-389`).

Things that intentionally do **not** participate in lockstep
determinism:

- Wall-clock time — the simulator uses emu cycles, not wall time.
- Audio device state — output, not input.
- Display rendering — output, not input.

Things that need extra care because they're easy to break
unintentionally:

- RNG seed — re-normalised by the savestate on apply
  (`netplay_savestate.cpp:81`).
- Floating-point — none in the 6502 simulation, so this is moot.
- Input tap from system events — the netplay path bypasses the
  local-only input registration so inputs only reach the sim via the
  lockstep pipeline (see comment at `netplay_input.h:12`).

---

## 11. Limitations and known restructuring directions

This section documents what the current architecture **does not**
support and what would be required to add it. None of these are
planned-and-promised, but the costs are well-shaped.

### 11.1 Two players only

The Slot enum has exactly two values; `NetInputPacket` carries one
player's stick + buttons; `ApplyFrameInputs()` loops `for (i = 0; i
< 2; ++i)` (`netplay_input.cpp:376-389`). The lobby's `maxPlayers`
field exists with a `kMaxPlayersLimit = 8` ceiling
(`lobby_protocol.h:109`), but this is cosmetic — it's not honored
anywhere in the lockstep path.

To support N=8 (matching the Atari MultiJoy hardware ceiling) the
required changes are:

1. Generalize `Slot` enum to 1..N.
2. Make `NetInputPacket` variable-length: header + slot bitmask +
   per-active-slot inputs. Bump protocol version.
3. Lockstep gate: advance frame F when **all active player slots**
   have inputs for F. Worst-case latency = max(RTT) across all
   peers, same problem AoE2 solved with a 250 ms turn delay.
4. Route slot K to physical port K (K=1..4) or MultiJoy multiplexer
   slot K-4 (K=5..8). The PIA-driven `SelectMultiJoy` already
   exists locally; just feed remote inputs into it.

Topology must also change: full mesh fails at N=8 (NAT-punch success
≈ 90% per pair, so 28 pairs all succeeding is statistically rare).
Hub-via-host or hub-via-relay is mandatory above N=2. The relay
infrastructure already exists for `wssRelayOnly` hosts and
generalises to "hub for >2P sessions."

### 11.2 No spectators

The Coordinator state machine accepts exactly two roles. There is no
read-only spectator path. The protocol and infrastructure are nearly
ready for it:

- The mid-session resync code already chunks and ships savestates on
  demand — exactly what a spectator joining mid-game needs.
- The v6 observability layer (`NetPhase`, `NetEventBatch`) already
  broadcasts session state for telemetry.

What's missing is a `Slot::Observer` role that:
- Receives the same input stream as players (read-only).
- Never enters the lockstep gate (always falls behind, never blocks
  players).
- Resyncs from a fresh snapshot on demand without disrupting players.

For >10 spectators the host's upstream gets uncomfortable
(~17 KB/s × N peers); the natural answer is to put the relay in
front, with periodic snapshot keyframes cached at the relay so new
spectators don't repeatedly hit the host. This is the same
architecture every esport uses for native-spectator + Twitch-stream
layering.

### 11.3 No replay file format

Because the simulation is fully deterministic given inputs +
ROMs/firmware + initial seed, a replay = `(NetBootConfig, initial
savestate, input stream, optional periodic keyframe savestates)`.
File size at 60 fps × 2 players = ~10 KB/min. There is currently no
record-to-disk pipeline; adding one is a hook in the lockstep
input-apply path.

Replay is essentially the same data plane as a spectator session,
just written to disk instead of sent over the network. Building it
first is a useful determinism-validation step on the road to
spectators.

### 11.4 Dynamic input delay D (implemented in v7)

`mInputDelay` starts at the value the host advertised in `NetWelcome`
(default D=3) and **ratchets up automatically** when the host's
smoothed RTT outgrows the current delay. RTT is measured on the host
(`mPeerRttMsEwma`) and stamped into every captured `NetInput` as a
1-byte `rttClass` quantum (4 ms per unit, capped at ~1020 ms — see
`LockstepLoop::RttClassFromMs` and `TargetDelayFromRttClass`).

Mechanism (deterministic, no coordination message):

1. **Stamp.** On the host, `Coordinator::SubmitLocalInput` writes
   `rttClass = quantize(mPeerRttMsEwma)` into the outgoing `NetInput`
   before forwarding to the lockstep loop. The joiner stamps zero;
   only the host's value is read back.
2. **Read.** Both peers fold the host's `rttClass(F)` into their
   authoritative ring (host's local ring on the host side, peer ring
   on the joiner side). Both see the same value at the same emu
   frame because the byte rides inside `NetInput` and survives
   redundancy.
3. **Threshold.** At the end of every applied frame, both peers
   compute `target = TargetDelayFromRttClass(rttClass)`. If `target
   > mInputDelay` for `kRatchetStreakFrames` (default 60 frames =
   1 s) consecutive frames, schedule a switch.
4. **Schedule.** Both peers compute the same future apply frame:
   `next emu frame F where (F % kSwitchModulus) == 0`. With the
   default modulus 1024, that's at most ~17 s of wait, mean ~8.5 s.
   No explicit message — the modulo-frame trick lets both sides
   agree on the same frame independently.
5. **Apply.** When `mCurrentFrame == switchFrame` at the start of a
   tick (in `SubmitLocalInput`, BEFORE the new capture is keyed),
   both peers raise `mInputDelay` and gap-fill the slots
   `[F+D_old, F+D_new)` of both rings by repeating the input from
   slot `F+D_old-1` (the last slot reliably ferried under the old
   delay). The capture happens with the new D.

The ratchet is **monotone**: D never lowers automatically. Lowering
would discard already-queued inputs and break determinism. A future
enhancement could allow careful ratchet-down via the resync path
(treating it like a mid-session restart).

Wire cost: NetInputPacket grows from 36 to 41 bytes (the rttClass byte
rides in each NetInput, so all R=5 redundancy slots carry it). At 60
packets/s/direction that's ~300 B/s of extra bandwidth — negligible
next to the ~2.4 KB/s baseline of input traffic.

Tests: `lockstep_selftest.cpp` exercises the threshold mapping, the
no-op case at low RTT, the convergence case under sustained high
RTT (both peers must agree on the new D and stay in lockstep), and
the switch-frame-alignment property (both peers must compute the
same future apply frame).

---

## 12. Determinism debugging

When peers desync, the diagnostic flow is:

1. Both peers log the divergence frame and their per-frame hashes
   (`g_ATLCNetplay`, `coordinator.cpp:1139`).
2. The `frame0 breakdown` log line (host + joiner) pinpoints which
   subsystem diverged at cold-boot. If frame 0 desyncs, the
   savestate didn't apply identically — usually a missing or
   mismatched accessor in the savestate format.
3. After-frame-0 desyncs are usually input-routing or a non-
   deterministic side effect of an emulator subsystem. Bisect by
   disabling subsystems on both peers until the desync stops.

Per the project rule (CLAUDE.md), if you suspect divergence in core
emulation: it's almost never the core. Check input routing first.

---

## 13. References

- User guide: [`NETPLAY.md`](../NETPLAY.md)
- Self-hosting walkthrough: [`HOSTING.md`](../HOSTING.md)
- Lobby server: [`server/lobby/README.md`](../server/lobby/README.md)
- Wire protocol authoritative source: `src/AltirraSDL/source/netplay/packets.h`
- Lockstep loop: `src/AltirraSDL/source/netplay/lockstep.{h,cpp}`
- Coordinator state machine: `src/AltirraSDL/source/netplay/coordinator.{h,cpp}`
- Lobby v2 schema: `src/AltirraSDL/source/netplay/lobby_protocol.h`
