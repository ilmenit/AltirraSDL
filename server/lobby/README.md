# altirra-lobby — reference lobby server (C++)

This is the reference session-directory server for AltirraSDL
netplay. It is **part of the AltirraSDL source tree** so the server
and client evolve together — protocol, field names, TTL values and
rate-limit parameters are defined exactly once in
`src/AltirraSDL/source/netplay/lobby_protocol.h`, which both this
server and the client `#include`.

Anyone can run their own lobby and point clients at it via
`~/.config/altirra/lobby.ini`:

```ini
[my-lobby]
name    = My private lobby
url     = http://my-host.example.com:8080
region  = eu-west
enabled = true
```

The default community lobby is hosted on Oracle Always-Free
infrastructure at `http://lobby.atari.org.pl:8080` (HTTPS via
`https://lobby.atari.org.pl`; DuckDNS backup hostname
`altirra-lobby.duckdns.org` resolves to the same box).  See the
sibling deployment repo `github.com/ilmenit/altirra-sdl-lobby` for
the automation that runs it (Dockerfile, systemd unit, CI
auto-deploy).  That deployment repo is infra-only; the server source
lives here.

## Build

Via the AltirraSDL CMake build (binary lands in the CMake build
directory as `server/lobby/altirra-lobby`):

```bash
cmake -S . -B build/lobby -DALTIRRA_BUILD_LOBBY=ON
cmake --build build/lobby --target altirra-lobby
```

Standalone (no AltirraSDL build wiring), as the Dockerfile does:

```bash
# Run from the repo root so shared headers resolve.
cmake -S server/lobby -B build/lobby-only -DCMAKE_BUILD_TYPE=Release
cmake --build build/lobby-only --target altirra-lobby
```

## Dependencies

One vendored third-party dependency: `cpp-httplib` (single-header,
vendored at `vendor/cpp-httplib/httplib.h`, MIT-licensed, upstream
`github.com/yhirose/cpp-httplib` v0.18.5). The project otherwise
keeps a "two external deps only" discipline (SDL3 + Dear ImGui for
the client); cpp-httplib is a deliberate, scoped exception for the
server only — the client's HTTP client lives in `http_minimal.cpp`
and has no overlap.

Everything else is stdlib.

## Run

```bash
./altirra-lobby
BIND=:9000 ./altirra-lobby              # override bind address
PORT=9000  ./altirra-lobby              # TCP/HTTP port
TTL_SECONDS=60 ./altirra-lobby          # shorter session TTL
UDP_REFLECTOR_PORT=9001 ./altirra-lobby # override NAT reflector port
UDP_REFLECTOR_PORT=0 ./altirra-lobby    # disable the reflector
```

The server exposes **two** sockets:

- `PORT` (TCP, default 8080) — the HTTP session directory.
- `UDP_REFLECTOR_PORT` (UDP, default 8081) — a stateless STUN-lite
  reflector used by netplay hosts to discover their public
  endpoint. See `docs/netplay-architecture.md` §8.4 in the client
  repo for the wire format.

Firewalls / security lists must permit **both** ports for netplay
hosts behind NAT to be reachable from the public internet.

Hit `/healthz` to confirm it's alive:

```bash
curl -sS http://localhost:8080/healthz   # → ok sessions=0
```

## API

- `POST /v1/session` — Create a session, returns
  `{sessionId, token, ttlSeconds}`. Validated fields: `cartName`
  (1..64), `hostHandle` (1..32), `hostEndpoint` (host:port),
  `playerCount` (1..maxPlayers), `maxPlayers` (2..8),
  `protocolVersion` (>0), optional `region` (≤32), optional
  `visibility` ("public"|"private"), optional `requiresCode` (only
  meaningful when visibility is "private"), optional `cartArtHash`
  (≤64 hex chars), optional **v2** `kernelCRC32` / `basicCRC32`
  (8-char hex; published so joiners can pre-flight firmware before
  attempting to connect), optional **v2** `hardwareMode`
  ("800XL"|"5200"|…, ≤16 chars), optional **v2** `videoStandard`
  ("NTSC"|"PAL"|…, ≤8 chars) and `memoryMode` ("320K"|"1088K"|…,
  ≤8 chars) so the browser can render a full machine spec row.
- `GET /v1/sessions` — List active sessions (newest-first,
  capped at 500). Each entry now carries an additional
  **v2** `state` field: `"waiting"` (joinable) or `"playing"`
  (in session — kept in the listing so the lobby looks alive but
  joiners suppress Join). The **v2** schema always includes
  `kernelCRC32`, `basicCRC32`, `hardwareMode`, `videoStandard`,
  `memoryMode`, and `cartArtHash`
  (empty string when the host didn't set them) so clients never
  have to distinguish "server silent" from "host placed no
  constraint".
- `GET /v1/session/{id}` — Fetch a single session.
- `POST /v1/session/{id}/heartbeat` — Keep-alive; body carries
  `{token, playerCount}` plus optional **v2** `state`
  ("waiting"|"playing") for the host to flip in/out of session
  visibility on its 30-second heartbeat cadence — no extra requests
  needed. Bad token → 401, unknown id → 404.
- `DELETE /v1/session/{id}` — Remove; `X-Session-Token` header
  required. Bad token → 401, unknown id → 404.
- **v2** `GET /v1/stats` — Aggregate counts:
  `{sessions, waiting, playing, hosts}`. Single small JSON object
  the Browser fetches once per refresh cycle so it can render a
  "12 sessions • 4 in play • 7 hosts" footer without enumerating
  the full list. O(N) under the same lock List takes; cheap with
  `kListCap = 500`.
- **v4** `GET /v1/sessions?include_awaiting=1` — opt-in flag that
  surfaces broker-pre-spawn (`awaiting_approval`) sessions in the
  list.  Hidden by default to protect pre-broker-aware clients from
  silent NetHello timeouts.  Native clients that have implemented
  the broker intent dance pass this flag.
- **v4** `POST /v1/session/{id}/intents` — joiner posts an intent
  against an `awaiting_approval` (or native auto-accepting `waiting`)
  session. A browser broker session keeps requiring the owner's decision
  after its emulator starts and publishes `waiting`.  Body `{joinerHandle, codeHash}`; returns
  `{intentId, sessionId, ttlSeconds}`.
- **v4** `GET /v1/intent/{iid}` — non-SSE polling endpoint mirroring
  the SSE stream below.  Returns
  `{intentId, sessionId, decided, accepted, reason, arrivedMs, ttlSeconds}`
  on 200, or 404 once the intent has been swept.  Native clients
  whose HTTP transport doesn't speak chunked transfer-encoding poll
  this on a 1–2 s cadence instead of opening the SSE stream.
- **v4** `GET /v1/intent/{iid}/stream` — SSE stream of decision
  events (used by the lobby web page's broker.js; native clients
  use the `GET /v1/intent/{iid}` poll above).
- **v4** `POST /v1/session/{id}/intents/{iid}/decision` — host posts
  the accept/reject decision.  Body `{token, accepted, reason?}`.
- `GET /healthz` — Liveness (also exercises the session-store mutex,
  so a deadlocked store fails its health check).

Error responses: `{"error": "<message>"}`.

CORS: reads are open; writes refuse requests that carry an `Origin`
header (prevents browser-initiated CSRF while allowing native
clients through).

Rate limit: per-source-IP token bucket, burst 120, refill 1/s. 429
on exhaust, `Retry-After: 1`.

## Protocol-version coupling

Wire field names and sizes come from:
- `src/AltirraSDL/source/netplay/lobby_protocol.h` — constants
  (TTL, rate limit, visibility literals, route paths) and JSON field
  names. Included by this server AND by the client in
  `lobby_client.cpp`.
- `src/AltirraSDL/source/netplay/json_cursor.h` — tiny JSON reader
  shared by both sides.

When changing any field on the wire, edit these files and both ends
rebuild atomically. There is no backward-compatibility wire
negotiation — this is a single-community protocol.

## Tests

```bash
cmake --build build/lobby --target altirra-lobby-test
ctest --test-dir build/lobby
```

Coverage: `/healthz`, Create happy path, Create validation (empty
cart, bad endpoint), Heartbeat good token, Heartbeat bad token,
Delete good token, Delete bad token, TTL expiry (via
`ExpireOnce` with a faked clock), rate limit 429, browser-origin
guard on writes.

## Deploying to production

The `github.com/ilmenit/altirra-sdl-lobby` repo (separate) owns:

- Oracle Cloud infrastructure (reserved IP, VM shape).
- GitHub Actions deploy workflow.
- Terraform / Ansible / whatever automation is in use there.

That repo clones **this** repo and invokes `docker build -f
server/lobby/Dockerfile .` from the AltirraSDL root. The
Dockerfile + `altirra-lobby.service` that ship here are the
current production-matching references.

## WebSocket bridge (browser netplay)

A second listener runs on `localhost:8090` and accepts WebSocket
upgrades on `/netplay`.  The public TLS edge (Caddy) reverse-proxies
WSS traffic to it; native UDP clients are unaffected.  The bridge
lets browser (WASM) builds share netplay sessions with native peers,
translating between the two transports session by session.

Vendored single-file mongoose 7.18 implements the WebSocket protocol
(`vendor/mongoose/mongoose.{c,h}`).  Caddy terminates TLS so the
bridge speaks plain WS upstream.

> **Deployment note — the public Caddyfile MUST include a
> `/netplay → localhost:8090` reverse_proxy route.** Without it,
> Caddy returns 404 on the WS upgrade and browser joiners see WS
> close code **1006**: *"could not reach lobby (DNS / TLS / CORS /
> mixed-content)"*. See `Caddyfile.example` in this directory for
> a complete reference config; copy/merge into `/etc/caddy/Caddyfile`
> on the lobby box and `sudo systemctl reload caddy`. To verify
> after applying, an unauthenticated curl probe of `/netplay` should
> return 400/403/410 (the bridge rejecting the missing subprotocol)
> rather than 404 (Caddy with no route).

### Handshake

The browser opens

    wss://lobby.atari.org.pl/netplay

with a comma-separated `Sec-WebSocket-Protocol` list:

    altirra-netplay.v1, session.<32hex>, role.<host|joiner>, token.<32hex>

The bridge validates the session id against the in-memory store,
compares the token with the canonical one returned by Create
(constant-time), and rejects the upgrade if the role slot is already
occupied.  Tokens never appear in the URL — Caddy logs URIs but not
subprotocol headers.

Possible upgrade outcomes:

| HTTP status | Reason |
|-------------|--------|
| 101 Switching Protocols | accepted |
| 400 Bad Request | malformed subprotocol list |
| 403 Forbidden  | wrong session token |
| 410 Gone       | session expired between Create and upgrade |
| 409 Conflict   | role already taken (host slot occupied, etc.) |
| 429 Too Many Requests | per-IP upgrade rate limit (60/min) |

### Frame format

Each binary WS frame carries one inner Altirra netplay packet
(`Hello`/`Welcome`/`Input`/`Bye`/etc.) prefixed with a one-byte
sender role envelope:

    [role:u8][inner_packet …]

The role byte (`0` = host, `1` = joiner) lets the bridge route by
the same `(sessionId, role)` key the existing UDP relay table uses.
The lobby strips the byte before forwarding to a native UDP peer
(the receiving native client never sees it) and rewrites it on
inbound to identify the OTHER side for browser peers.  Inbound
frames larger than 2048 bytes are rejected with WS close 1009
(Message Too Big).

### Liveness

* WS-level ping every 15 s; close on 10 s pong-timeout.
* Application-level idle: 60 s without any inbound frame closes the
  connection.
* On graceful WS close, the bridge synthesises an `ANPB` (NetBye)
  packet and delivers it to the surviving peer so its Coordinator
  transitions to `Phase::Ended` immediately instead of waiting for
  its own peer-silence timeout.

### Cross-transport bridging

The bridge maintains a `WsRegistry` keyed on `(sessionId, role)`.
On every incoming packet — whether from a UDP peer arriving at the
reflector on `:8081`, or a WS frame arriving on `:8090` — the
forwarder consults BOTH the WS registry and the UDP `RelayTable`:

1. If the OTHER side of the session is on WS, deliver the inner
   bytes as a binary WS frame (with the recipient's role byte
   prepended).
2. Otherwise, if the OTHER side is on UDP relay, wrap in ASDF and
   `sendto()` via the reflector socket FD — sharing the FD keeps
   the source as `lobby_ip:8081` so the native peer's
   `Coordinator::PeerIsLobby()` check still recognises the relay
   origin.
3. Otherwise drop (no peer registered yet).

Per-pair rate limiting reuses the existing `RelayPair::TryForward`
token bucket (240 pps / 240 pps refill) so cross-transport sessions
get the same protection as pure UDP sessions.

### Metrics

The `/v1/metrics` JSON object includes a `ws_bridge` block:

    "ws_bridge": {
      "connections_total": …,
      "upgrades_rejected_auth": …,
      "upgrades_rejected_conflict": …,
      "messages_in_total": …,
      "messages_out_total": …,
      "bytes_in_total": …,
      "bytes_out_total": …,
      "forwards_cross_transport": …,
      "dropped_no_peer": …,
      "dropped_rate_limit": …,
      "dropped_oversized": …,
      "dropped_auth": …
    }

`forwards_cross_transport` counts WS↔UDP forwards in either
direction; a non-zero value confirms a mixed-transport session is
running.  `dropped_no_peer` should remain near zero in normal
operation; sustained non-zero indicates a stale registration race
or a client that is sending after the OTHER side has disconnected.

### `wssRelayOnly` sessions

Lobby-protocol v3 adds a boolean `wssRelayOnly` field on
`/v1/session` POST and on every `/v1/sessions` entry.  Hosts running
in a browser set the flag (and submit empty `hostEndpoint` /
`candidates`) so joiners know there is no UDP path to attempt.
Native v3 joiners that see the flag skip candidate spray and go
straight to lobby-relay from T=0; pre-v3 native joiners simply fail
with a Hello timeout (which is the intended graceful degradation).

## History

This server was originally written in Go (≈500 LoC). Rewritten in
C++23 in 2026 so client and server share a single toolchain, a
single protocol header (no cross-language schema drift), and a
smaller deployable binary. The Go source was removed once this
version passed functional parity.

The WebSocket bridge was added in 2026 to let browser (WASM)
builds participate in netplay sessions; the inner Altirra wire
protocol is unchanged.

## Authenticated relay and recovery (gameplay protocol 8)

Native clients use HTTPS for the official directory. Existing shipped HTTP
entries for the two official hostnames migrate to HTTPS; custom HTTP URLs keep
their explicit configuration. API URLs may include a custom port and path prefix.
Use `PUBLIC_WEBSOCKET_URL=wss://your-host:port/path` when the WebSocket endpoint
is different from the directory origin. Without it clients derive the endpoint
from the configured origin and prefix, ending in `/netplay`.

`GET /v1/capabilities` advertises relay authorization version, UDP port, WebSocket
endpoint and supported gameplay protocol. Create responses include transport
addresses. `UDP_REFLECTOR_PORT=0` and `WS_BRIDGE_PORT=0` disable those listeners.
A disabled UDP relay still permits direct native peer connections.

A public session ID or nickname grants no authority. The host uses its private
session token for relay registration. Before joining, a client generates a fresh
128-bit attempt nonce and POSTs `{ "sessionNonce": "32 lowercase hex digits" }`
to `/v1/session/{id}/relay-ticket`. The response contains a private `ticket`, the
nonce, `relayPort`, optional `webSocketUrl`, and an idle lease of 60 seconds.
At most 16 pending routes exist per session. Requesting an occupied nonce returns
409 and never reveals its ticket. Authenticated traffic renews the lease.
Browser broker sessions additionally require `intentId` in this request. The
intent must have been explicitly accepted by the owner; an unused approval
can authorize the startup handoff for five minutes. This requirement survives
heartbeat state changes to `waiting`, and a new intent is never automatically
approved merely because the browser emulator has started. The existing private
ticket then owns the live connection independently of the startup grant.

UDP relay headers are 56 bytes: magic, session ID, role and three zero bytes,
then the private 16-byte ticket and 16-byte route nonce. Both registration and
data validate the ticket before allocating or changing an endpoint. Distinct
pending joiners have independent routes; only the same private authority can
rebind its route after a NAT/source-port change. Host responses carry the
pending/accepted nonce. WSS upgrades require `token.<ticket>` for both roles and
`nonce.<hex>` for joiners. Native hosts retain their explicit in-emulator
approval and private-code checks. Browser broker hosts auto-accept only traffic
whose private ticket was minted from the owner's approved intent. Protocol-8 session envelopes remain opaque to the relay except for
routing and optional phase/heartbeat diagnostics.

Authenticated lease heartbeats have a separate per-session rate budget, so five
waiting offers or another user behind the same NAT cannot exhaust their lease
maintenance allowance by browsing. Display-name collisions never replace another
owner's room or consume a nickname-owned quota. The client retains its own
five-offer limit; global capacity and source request limits protect the service.
Expired registrations are recreated, with stale responses ignored.

Native hosts can register with multiple directories and keep independent relay
contexts. A browser host uses one active directory because it owns one WSS
transport. Registration failure tries another enabled directory. A running
browser match is not silently migrated between services.

Tests include real loopback UDP registration/forwarding, rejected endpoint
replacement, authenticated NAT rebinding, independent simultaneous pending UDP
and WSS routes, lease maintenance after discovery-rate exhaustion, and frontend
cancellation with delayed responses. Unit tests make no public session writes;
lobby-client live checks require an explicit `LOBBY_HOST` test endpoint.
