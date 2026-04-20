# altirra-lobby — reference lobby server

This is the reference session-directory server for AltirraSDL
netplay. It is **part of the AltirraSDL source tree** so the server
and client evolve together — protocol / field / rate-limit changes
are a single atomic commit across both sides.

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
That deployment repo is infra-only; the Go source lives **here**.

## Build

Standalone (no CMake involvement):

```bash
cd server/lobby
go build -o altirra-lobby .
```

Via the AltirraSDL CMake build (requires the Go toolchain):

```bash
cmake -S . -B build -DALTIRRA_BUILD_LOBBY=ON
cmake --build build --target altirra-lobby
# binary ends up in build/altirra-lobby
```

## Run

```bash
./altirra-lobby                   # binds :8080 by default
PORT=9000 ./altirra-lobby         # override port via env
```

Hit `/healthz` to confirm it's alive:

```bash
curl -sS http://localhost:8080/healthz   # → ok sessions=0
```

## API

- `POST /v1/session`              — Create a session, returns `{sessionId, token, ttlSeconds}`.
- `GET  /v1/sessions`             — List active sessions.
- `POST /v1/session/<id>/heartbeat` — Keep-alive (token-gated).
- `DELETE /v1/session/<id>`       — Remove (token-gated, header `X-Session-Token`).
- `GET  /healthz`                 — Liveness.

See `server.go` for the full schema and behaviour. The server is
intentionally stateless — sessions live in-memory, 90 s TTL, sweeper
runs every 30 s.

## Protocol version coupling

The client-side equivalent types live in:
- `src/AltirraSDL/source/netplay/lobby_client.{h,cpp}` — typed facade.
- `src/AltirraSDL/source/netplay/lobby_config.{h,cpp}` — `lobby.ini` parser.

When changing any field on the wire (e.g. adding a `cartArtHash`),
update **both** sides in the same commit. There is no backward
compatibility wire negotiation — this is a single-community protocol
and the client will be rebuilt with the server change.

## Tests

```bash
cd server/lobby
go test ./...
```

The tests exercise the full HTTP surface against an in-process
instance (no external network required).

## Deploying to production

The `github.com/ilmenit/altirra-sdl-lobby` repo (separate) owns:

- Oracle Cloud infrastructure (reserved IP, VM shape).
- `Dockerfile`, `altirra-lobby.service` for Oracle host.
- GitHub Actions deploy workflow (SSH + systemd restart).

That repo builds the binary from **this** source tree — check its
README for the exact pull mechanism (submodule / sparse checkout /
CI clone). The copies of `Dockerfile` and `altirra-lobby.service`
that ship here are the current production-matching references; the
deploy repo should stay in sync with them.
