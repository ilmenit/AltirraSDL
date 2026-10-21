# AltirraSDL netplay architecture

The user guide is [NETPLAY.md](../NETPLAY.md). This document describes
protocol **8** and its implementation in `source/netplay` and `server/lobby`.
Older peers must update together: the handshake rejects incompatible versions.

## 1. Simulation and input

Two emulators run the same machine configuration and firmware with a locked
random seed. The host transfers the selected game image; both peers cold-boot
it under the temporary netplay profile. Firmware is checked by CRC and is
never transferred. Each peer owns one of two controller slots.

The host chooses a mode before a session starts:

- **Automatic:** two frames of local input delay and up to eight frames of
  remote prediction. Missing remote input repeats the last held controller,
  keyboard matrix and analog position, without repeating a keyboard edge.
- **Delay only:** wait for both authoritative inputs before advancing. The
  configured input delay can rise in response to sustained network latency.

`Loop` captures each local input once. A stalled frame or a replay must not
consume another live keyboard event or change an already transmitted input.
The confirmed cursor is the first frame without both authoritative inputs;
only confirmed state hashes are published. If late input disagrees with a
prediction, the main loop restores the checkpoint before that frame, replays
through the previous simulation cursor, then resumes ordinary advancement.
Prediction stops when the negotiated bound is exhausted.

The input application guard runs once per emulated frame, even though
`ATSimulator::Advance` may return after only part of a frame. Replayed frames
run emulation without UI event polling, frame pacing or duplicate audio
submission. Already submitted speculative audio cannot be retracted.

## 2. Checkpoints and media

`netplay_checkpoint` retains the native serializable object graph. Local
rollback does not encode JSON, compress a ZIP or copy the preview image.
Exact runtime metadata includes both scheduler clocks, randomization seeds
and PIA floating-input timers. It also preserves CPU interrupt/microcycle
state, POKEY keyboard/serial/pot phases, disk buffers and mechanical timing,
SIO transfer queues, startup console-key holds, and pending XEX/BASIC loader
images/cursors/hooks. ANTIC retains in-flight DMA buffer positions and queued
register writes; MMU snapshots preserve banked BASIC/self-test ROM latch history.
Legacy save states remain readable.

Checkpoints preserve applied controller state separately from live local
input queues. Controller restoration precedes chip-state restoration, so
port callbacks cannot overwrite the restored POKEY/PIA state.

Session disk images use copy-on-write sector overlays. A checkpoint retains
an immutable overlay version; a write shares unchanged sectors and changes
only the new version. Rewinding restores data, image size and FDC status.
The original image and its host file remain unchanged. Dynamic host-backed
disks cannot provide this deterministic history and are refused.

Network resynchronization uses the ZIP/JSON save-state path plus bounded
session disk sidecars. The receiver validates sizes and records before
application. The resync control packet carries the authoritative delay and
payload CRC. Both input history and local checkpoints restart at the agreed
frame; resync does not run the initial cold-boot transition again.

Before entering netplay, the profile layer retains the original media objects
and a machine checkpoint. On leaving, it restores the original profile,
media objects, machine state, running/paused state and frontend mode. This
also preserves unsaved local disk changes. Input capture must end before
restoring the original machine.

## 3. Wire format and repair

`packets.h` and `protocol.cpp` define fixed, explicitly encoded little-endian
packets; C++ struct layout is never sent directly.

| Item | Protocol 8 size |
|---|---:|
| One controller input | 22 bytes |
| Input packet, including five redundant inputs | 132 bytes |
| Session envelope (`ANPU`) | 24 bytes |
| Relay envelope | 56 bytes |
| Resync-start packet | 32 bytes |

Inputs carry joystick/buttons, console/modifier flags, an explicit keyboard
event flag, a 64-bit held matrix and two signed analog positions. Atari scan
code zero is a real key. Hash and acknowledgement frame numbers use 32 bits;
session hashes continue beyond frame 65,535.

Every input packet acknowledges the contiguous remote input cursor. Peers
send the fresh redundant window and, when needed, a separate oldest-unacked
repair window. Sending only old repairs would throttle new input to five
frames per round trip. Timed retransmission continues while emulation is
stalled. Receivers bound accepted frame windows and snapshot allocation.

Post-handshake traffic is scoped by the random session nonce and epoch in
the session envelope. The coordinator also validates the sender endpoint.
Resync changes the epoch, so queued packets from the previous history are
ignored. This is session isolation, not encrypted peer transport.

## 4. Connection paths

Native peers prefer UDP. Candidate discovery includes LAN/public addresses,
NAT mapping and the lobby reflector. Relayed paths support restrictive NAT
and browser peers. Browser gameplay uses ordered WebSocket transport;
rollback does not remove TCP head-of-line delays. WebRTC is not implemented.

The native HTTP adapter uses the shared platform HTTP(S) backend: libcurl
loaded at runtime on Linux/macOS, WinHTTP on Windows and platform Java HTTP
on Android. WASM uses its asynchronous browser request path. Configuration
supports custom ports and base paths; relay ports come from lobby responses.
WebSocket endpoints may include an explicit scheme, port and path.

## 5. Lobby authority and relay routing

A listing's private create token authorizes its owner. Public host names,
game titles and session IDs do not authorize deleting or replacing listings.
Authenticated heartbeats have a separate per-session budget from public
browse/create requests, so several offers behind one IP do not starve each
other. If a registration expires or the service restarts, the client creates
a new listing without replacing the live coordinator.

A joiner requests a short-lived relay ticket with
`POST /v1/session/{sid}/relay-ticket`, supplying its fresh session nonce.
The lobby returns a private role ticket and relay endpoint. Tickets bind the
session, role, nonce and lease; guessing a public session ID cannot claim a
relay slot. Browser-broker hosts also require the owner's accepted join
intent when a ticket is issued, including after the emulator has started. The host uses its private listing token. UDP registration/data
and WebSocket admission validate this authority before routing. Multiple
pending joiners have separate nonce-bound routes. Native hosts can advertise
across several relays; a browser host advertises on one active directory
because its transport has one WebSocket connection.

The website broker invalidates asynchronous work when an attempt is cancelled.
A delayed ticket/connection response cannot resurrect a cancelled join.
Gameplay remains two-player; spectators and additional controller slots are
not implemented by this change.

## 6. Verification and measurements

Standalone CTests cover protocol encoding, frame history, repair, prediction,
long-session hash cursors, snapshot bounds, UDP transport, coordinator races,
input capture, disk versions and lobby operations. Server tests include actual
UDP and WebSocket relay admission/routing. UI test mode exposes checkpoint,
input/profile and session telemetry for real-emulator integration tests.

`tests/netplay/benchmark.py` compares the previous released core with repaired
delay-only and hybrid modes under seeded RTT, jitter and loss. Its synthetic
state checks input histories and scheduling; its CPU measurement is not the
cost of emulating an Atari game. Actual checkpoint/replay costs and two-process
results belong in the accompanying measurement report. Neither a synthetic
pass nor a Linux build establishes cross-platform runtime coverage.
