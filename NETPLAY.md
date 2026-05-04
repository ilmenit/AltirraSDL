# AltirraSDL netplay — user guide

AltirraSDL ships a netplay feature that lets two players cold-boot
the same Atari game in lockstep over the internet or a LAN, with
input exchanged frame-by-frame for deterministic play. This document
covers:

1. [How netplay works](#how-netplay-works) — one-paragraph mental model.
2. [Playing a game online](#playing-a-game-online) — host / join walkthrough.
3. [Pointing at a different lobby](#pointing-at-a-different-lobby) —
   editing `lobby.ini`, federating across multiple lobbies.
4. [Running your own lobby](#running-your-own-lobby) — from a single
   `docker run` to a production systemd deployment.
5. [Troubleshooting](#troubleshooting).

For the protocol-level design (wire formats, determinism strategy,
packet flow), see [NETPLAY_DESIGN_PLAN.md](NETPLAY_DESIGN_PLAN.md).

---

## How netplay works

**Short version**: both peers cold-boot the same game from the same
master seed with the same machine configuration, then advance their
simulators in lockstep — each peer waits for the other's input for
frame N before committing frame N. Because the emulator is cycle
deterministic given identical inputs, both screens show pixel-exact
identical frames indefinitely.

**Less short**: AltirraSDL ships a tiny matchmaking lobby (a session
directory — see [server/lobby/](server/lobby/)) where hosts advertise
"I'm hosting Joust at 1.2.3.4:26100". Browsers pull the directory,
the joiner connects to the host's UDP port directly (the lobby
doesn't proxy), and the two peers exchange handshake + `NetWelcome`
packets carrying the host's game-file bytes and machine config. Both
peers then `ColdReset` with the same locked random seed and enter
lockstep.

Key implications:

- **Latency is real**: lockstep waits for both inputs before
  advancing. Default input delay is 4 frames (~66 ms) which adds
  ~100 ms round-trip feel on typical connections.
- **Same machine config required**: kernel ROM CRC32, BASIC ROM
  CRC32, hardware mode (800/XL/XE/5200), memory, video standard —
  all frozen into the `NetBootConfig` the host ships. The joiner
  refuses to connect if it doesn't have matching-CRC32 firmware
  installed locally.
- **Game file is transferred** from host → joiner on connect, so
  the joiner doesn't need the file in their library. This covers
  common XEX/ATR/CAR images up to ~32 MB.

---

## Playing a game online

### Host a game

1. In AltirraSDL, open **Online Play → Host Games**.
2. Click **Add Game**.
3. Pick a game file (`.xex` / `.atr` / `.car`) from your library or
   disk.
4. Under **Machine configuration**, accept the defaults (inherited
   from your running emulator — so whatever boots the game locally
   will boot it for your peer) OR pick specific hardware / OS / BASIC
   from the dropdowns. The two firmware dropdowns are populated from
   the same list you see in **System → Configure System → Firmware**.
5. Leave **Private** off for a public listing, or tick it and set an
   entry code the joiner must type.
6. **Add** — the offer appears in the Host Games table. Tick the
   **On** checkbox to announce it; untick to retract.

The lobby status line at the top of the window shows `[OK]` in green
when the lobby is reachable. Hosted games appear on *every* enabled
HTTP lobby in your `lobby.ini`, not just the first.

### Join a game

1. Open **Online Play → Browse Hosted Games**.
2. Wait for the list to populate (auto-refreshes every 10 s). Pick a
   row and click **Join**.
3. If the offer is private, enter the code you were given.
4. Your simulator unloads any current game and cold-boots the host's
   title. First frame is synchronised — you'll see the boot
   animation on both screens at the same emulated tick.

### Controlling who joins

By default AltirraSDL auto-accepts every join request that passes
basic validation (matching firmware CRCs, correct entry code).  To
require manual approval, open **Online Play → Preferences → Hosting**
and switch **When someone joins** to **Prompt me**.  From then on,
each incoming joiner raises a modal:

```
Eve wants to join your game:
  Joust

Auto-decline in 17s            [Allow]  [Deny]
```

Closing the dialog or letting the 20-second timer elapse counts as
Deny — the joiner sees a clean "host rejected: manual decline"
rather than hanging in "Connecting…".  The host's listing stays up
for the next attempt.

---

## Pointing at a different lobby

On first launch AltirraSDL writes
`~/.config/altirra/lobby.ini` with the built-in defaults:

```ini
; Altirra SDL netplay — lobby directory.
; Add more [sections] to federate across multiple lobby servers.
; Set enabled=false to skip a lobby without deleting it.

[official]
name    = Altirra Official Lobby
url     = http://lobby.atari.org.pl:8080
region  = global
enabled = true

; Backup lobby — same server, alternate DNS via DuckDNS.  Active by
; default so federation auto-falls-back: if one DNS path is
; unreachable (or its TLS cert is pending) the other still answers.
; While both names resolve to our server, a hosted game registers on
; each separately and Browse shows it twice (different sessionIds,
; same backend) — cosmetic and joiners can use either copy.  Flip
; this to `enabled = false` if you prefer single-listing hosting
; once both DNS names are stable.
[backup]
name    = Altirra Lobby (DuckDNS backup)
url     = http://altirra-lobby.duckdns.org:8080
region  = global
enabled = true

[lan]
name      = LAN
transport = udp-broadcast
port      = 26101
enabled   = true
```

**Common edits**:

- **Disable the community lobby** and use only a private one:

  ```ini
  [official]
  enabled = false

  [friends]
  name    = Our group
  url     = http://lobby.example.com:8080
  region  = eu
  enabled = true
  ```

- **Federate across multiple lobbies** — enable all the HTTP
  sections you want. The Browse list merges entries from every
  enabled lobby (de-duped by `sessionId`), and Host announces each
  offer to every enabled HTTP lobby so your game shows up everywhere
  a joiner might look.

- **LAN-only play** — leave the `[lan]` section enabled and disable
  the HTTP ones. Hosted games are announced via UDP broadcast on the
  configured port; the Browse list shows peers discovered on the
  local subnet.

Changes take effect on next launch. A Preferences-screen **Reload
lobbies** button is planned; for now restart AltirraSDL.

---

## Running your own lobby

The reference lobby is a small C++ HTTP server that keeps a list of
active sessions in memory. No database, no rendezvous, no
authentication beyond per-session tokens. It lives at
[`server/lobby/`](server/lobby/) in this repo and builds with the
rest of AltirraSDL (no extra toolchain).

### Option 1 — localhost, 30 seconds

For LAN games or just testing against yourself:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target altirra-lobby -j
./build/server/lobby/altirra-lobby
# altirra-lobby listening on 0.0.0.0:8080 (max=1000, ttl=90s, burst=120)
```

Verify:

```bash
curl -sS http://localhost:8080/healthz   # → ok sessions=0
```

Point AltirraSDL at it by editing `~/.config/altirra/lobby.ini`:

```ini
[local]
name    = My local lobby
url     = http://127.0.0.1:8080
enabled = true

[official]
enabled = false
```

Restart AltirraSDL; the Host Games window's lobby indicator should
turn green. Host a game, browse from a second AltirraSDL instance —
your own lobby is now brokering.

### Option 2 — Docker

Build an image from the repo root (the Dockerfile multi-stage
builds the C++ binary, then drops it into a small Alpine runtime
image):

```bash
docker build -f server/lobby/Dockerfile -t altirra-lobby .
docker run --rm -p 8080:8080 altirra-lobby
```

Configurable via environment variables (all optional):

| Var | Default | Purpose |
|---|---|---|
| `BIND` | `0.0.0.0:8080` | Bind address + port |
| `PORT` | `8080` | Port only (overrides `BIND` port) |
| `TTL_SECONDS` | `90` | Session expiry |
| `MAX_SESSIONS` | `1000` | Hard cap on concurrent sessions |
| `RATE_BURST` | `120` | Token-bucket capacity per source IP |

Example with custom port:

```bash
docker run --rm -p 9090:9090 -e PORT=9090 altirra-lobby
```

### Option 3 — VPS with systemd (production)

Works on any Linux VPS with at least 512 MB RAM and a C++ compiler
toolchain. Example: $5/month DigitalOcean / Hetzner / any Oracle
Always-Free instance.

**1. Clone and build on the host**:

```bash
ssh your-vps
sudo apt install -y build-essential cmake git
git clone https://github.com/ilmenit/AltirraSDL.git
cd AltirraSDL
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target altirra-lobby -j
sudo install -m 0755 build/server/lobby/altirra-lobby /usr/local/bin/
```

**2. Create a dedicated user** (the systemd unit assumes one):

```bash
sudo useradd -r -s /usr/sbin/nologin altirra
```

**3. Install the systemd unit**:

```bash
sudo install -m 0644 server/lobby/altirra-lobby.service \
    /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now altirra-lobby
```

**4. Open the firewall**:

```bash
sudo ufw allow 8080/tcp            # Ubuntu / Debian
# or
sudo firewall-cmd --permanent --add-port=8080/tcp   # RHEL / Fedora
sudo firewall-cmd --reload
```

**5. Verify**:

```bash
curl -sS http://<vps-ip>:8080/healthz    # → ok sessions=0
sudo systemctl status altirra-lobby
sudo journalctl -u altirra-lobby -f      # live logs (one JSON line per request)
```

**6. Publish the URL** to players — they add a `[myserver]`
section to their `lobby.ini` pointing at
`http://<vps-ip>:8080` and you're done.

The shipped [`altirra-lobby.service`](server/lobby/altirra-lobby.service)
enables `NoNewPrivileges`, `ProtectSystem=strict`, `PrivateTmp`,
`MemoryDenyWriteExecute`, and a system-call filter — the server
touches only its own in-memory state, so a compromised binary has
nowhere to go.

### Option 4 — behind a reverse proxy (HTTPS)

The lobby is HTTP-only by design (simpler, smaller attack surface,
TLS termination is the proxy's job). Typical nginx fronting:

```nginx
server {
    listen 443 ssl http2;
    server_name lobby.example.com;

    # Your cert & key here.
    ssl_certificate     /etc/letsencrypt/live/lobby.example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/lobby.example.com/privkey.pem;

    location / {
        proxy_pass         http://127.0.0.1:8080;
        proxy_set_header   X-Forwarded-For $remote_addr;
        proxy_read_timeout 15s;
    }
}
```

The lobby reads `X-Forwarded-For` for real client IPs, so rate
limiting still works correctly behind a proxy. Clients then use
`url = https://lobby.example.com` — AltirraSDL's HTTP client handles
HTTPS via the host system's TLS stack.

### Sizing

The reference server is stateless (in-memory) and very lean:

- ~400 KB stripped binary, ~5 MB RSS idle.
- One worker thread per active request (cpp-httplib default).
- Default per-IP rate limit: 120 req burst, 1/s refill — plenty for
  a host heartbeating every 30 s, and the Browse list refreshing
  every 10 s.
- `MAX_SESSIONS=1000` by default; a community lobby serving ~50
  simultaneous games will spend most of its memory on TCP socket
  buffers, not session state.

A $5/month VPS comfortably hosts hundreds of concurrent sessions.

---

## Troubleshooting

**Lobby indicator stuck on gray `[..]` "checking..."**

The client pings the configured lobbies on window open if no result
has arrived in the last 60 s. If the ping never lands, the lobby URL
is wrong or the host is down. Check:

```bash
curl -sS -m 3 http://<lobby-url>/healthz
```

**Lobby indicator shows red `[!!]` with `Lobby is rate-limiting`**

You've hit the per-IP token bucket. Possible causes:

- Multiple AltirraSDL instances on the same NAT / household all
  polling at once.
- A client bug causing a spin-retry (shouldn't happen on current
  builds — Browse does exponential backoff of 10s → 60s on failures).

Wait a minute and the bucket refills.

**Joiner reports "OS firmware with CRC32 XXXXXXXX is not installed"**

The host's `NetBootConfig.kernelCRC32` doesn't match any firmware
the joiner has imported. The joiner opens **System → Configure
System → Firmware** and imports the exact same ROM the host uses
(or picks a firmware both peers already have in common in the host's
Add-Game dialog).

In the Browser, sessions whose firmware you don't have are coloured
red with the OS / BASIC CRC sub-line, and the **Join** button is
disabled with a hint like `(missing OS firmware)`. Hover the row to
see the exact CRC32 you need to import.

**"Frame 0 desync detected"**

This should not happen on current builds — both peers cold-boot from
the same seed and the ColdReset is cycle-deterministic. If you see
it, save the `[NETPLAY]` log lines from both peers and open an
issue; the `frame0 breakdown` output pinpoints which subsystem
diverged.

**Host's game takes a long time to appear on Browse**

Hosts announce with a `POST /v1/session` at enable time and heartbeat
every 30 s. Lobby TTL is 90 s — if the host's last heartbeat was
more than 90 s ago, the lobby removes the entry until the next
heartbeat refreshes it.

**Joiner stuck on "Asking the host to let you in…" forever**

The handshake is direct UDP between joiner and host — the lobby is
only a directory, not a relay. If the joiner is reaching the lobby
but never completing the handshake, something is dropping the UDP
packets between the two peers. Common causes:

- **Host behind a restrictive NAT / CGNAT.** The host's router drops
  inbound UDP from the joiner's IP because the mapping was never
  established. Workaround: the host port-forwards the UDP port
  (default 26100) on their router. See the netplay-NAT doc for
  details.
- **Mobile / cellular data on the joiner.** Carrier-grade NAT
  (CGNAT) often uses symmetric mapping that breaks hole-punching.
  Switch to Wi-Fi if possible.
- **Corporate / school network on either side.** These frequently
  block outbound UDP to arbitrary ports; the only fix is to try a
  different network.

The client now sprays `NetHello` to every host candidate (LAN,
server-reflexive / public, loopback) for up to 15 seconds before
giving up, so transient packet loss is no longer a failure mode.

---

## Related docs

- [server/lobby/README.md](server/lobby/README.md) — lobby server
  build / API / test / protocol-coupling details.
- [NETPLAY_DESIGN_PLAN.md](NETPLAY_DESIGN_PLAN.md) — protocol-level
  design: packet formats, lockstep math, cold-boot determinism
  rationale.
- [BUILD.md](BUILD.md) — AltirraSDL client build instructions.
