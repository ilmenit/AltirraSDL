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
infrastructure at `http://158.180.27.70:8080` — see the sibling
deployment repo `github.com/ilmenit/altirra-sdl-lobby` for the
automation that runs it (Dockerfile, systemd unit, CI auto-deploy).
That deployment repo is infra-only; the server source lives here.

## Build

Via the AltirraSDL CMake build (binary lands in the CMake build
directory as `server/lobby/altirra-lobby`):

```bash
cmake -S . -B build
cmake --build build --target altirra-lobby
```

Standalone (no AltirraSDL build wiring), as the Dockerfile does:

```bash
# Run from the repo root so shared headers resolve.
cmake -B build/lobby-only -DCMAKE_BUILD_TYPE=Release .
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
BIND=:9000 ./altirra-lobby        # override bind address
PORT=9000  ./altirra-lobby        # just the port
TTL_SECONDS=60 ./altirra-lobby    # shorter session TTL
```

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
  ("800XL"|"5200"|…, ≤16 chars).
- `GET /v1/sessions` — List active sessions (newest-first,
  capped at 500). Each entry now carries an additional
  **v2** `state` field: `"waiting"` (joinable) or `"playing"`
  (in session — kept in the listing so the lobby looks alive but
  joiners suppress Join). Hosts also publish their CRC / hardware
  fields here when set.
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
cmake --build build --target altirra-lobby-test
ctest --test-dir build
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

## History

This server was originally written in Go (≈500 LoC). Rewritten in
C++23 in 2026 so client and server share a single toolchain, a
single protocol header (no cross-language schema drift), and a
smaller deployable binary. The Go source was removed once this
version passed functional parity.
