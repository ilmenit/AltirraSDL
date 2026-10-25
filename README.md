# AltirraSDL

**Unofficial** SDL3 + Dear ImGui fork of [Altirra](https://www.virtualdub.org/altirra.html), the cycle-accurate 8-bit Atari computer emulator by Avery Lee (phaeron).

> **This is not an official Altirra release.** It is an independent, community-maintained fork that replaces the native Windows UI with a cross-platform SDL3 + Dear ImGui frontend, and expands the capabilities into new directions. It is not affiliated with, endorsed by, or supported by the original author. For the official Altirra emulator, please visit [virtualdub.org/altirra.html](https://www.virtualdub.org/altirra.html).

## What is Altirra?

Altirra is a best-in-class 8-bit Atari computer emulator featuring:

- Cycle-exact emulation of Atari 400/800, 1200XL, 600/800XL, 130XE, XEGS, and 5200 systems
- Accurate emulation of undocumented hardware behavior, including undocumented 6502 instructions, precise DMA timing, and hardware bugs
- Reimplemented OS, BASIC, and handler ROMs — no Atari ROM images required
- Emulation of three decades of hardware expansions, disk drives, modems, and accelerators
- A powerful debugger with source-level stepping, conditional breakpoints, execution history, and profiling
- Support for most popular 8-bit image formats: ATR, ATX, DCM, XFD, PRO, BAS, ROM, CAS, SAP, and more

For the full feature list and documentation, see the [official Altirra page](https://www.virtualdub.org/altirra.html).

## What is AltirraSDL?

AltirraSDL provides an SDL3 frontend for graphics, input, and audio,
and Dear ImGui for the user interface. It uses upstream Altirra’s
emulation engine, with cross-platform adaptations
and targeted fixes maintained in this fork. The current upstream baseline is
**Altirra 4.50 test22** (project/package version **4.50.22**).

### Goals

- Make Altirra runnable on Linux (the primary target), macOS, Windows,
  Android, and in web browsers.
- Keep the delta from upstream as small as possible to ease merging of future releases
- Match the native Windows frontend’s functionality, menu structure,
  and configuration options using SDL3 + Dear ImGui, while preserving
  explicitly added fork features.

### Non-goals

- Replacing or competing with the official Windows build of Altirra (use the real thing on Windows — it's better)

## What this fork adds on top of upstream Altirra

AltirraSDL adds cross-platform frontends, scripting, online play, and
additional tools. Full native Windows functionality remains the goal;
the SDL frontend is still a work in progress, and feature availability
depends on the platform and build options.

### Cross-platform foundation

- **Runs on Linux, macOS, and Android** in addition to Windows. The
  native Windows `.sln` build is preserved alongside the CMake build;
  the SDL3 + Dear ImGui frontend uses the same emulation engine.
- **Portable settings on Linux/macOS SDL3 builds** stored as a plain
  INI at `$XDG_CONFIG_HOME/altirra/settings.ini`, defaulting to
  `~/.config/altirra/settings.ini` (same format as Windows portable mode). The current Windows SDL3 build uses the same Windows
  registry-backed settings store as native Altirra. Android uses its
  app configuration directory; browser builds use persistent browser storage.
- **Native OS file dialogs** via SDL3 and platform integrations, with
  a built-in file picker available as a fallback. Last-used
  directories persist across launches.
- **Window drag-and-drop** of disk, cassette, cartridge, and executable
  images directly onto the running window.
- **Optional exit confirmation** under Configure System → Emulator → UI →
  Confirm on Exit, also `ui.confirm_exit` in Advanced Configuration. It
  defaults to on. Disable it for IDE launches to close immediately;
  unsaved changes are discarded without asking.

### Rendering: SDL_GPU with OpenGL fallback

- **SDL_GPU display backend** as the primary desktop renderer, using Metal
  on macOS and Vulkan or another native GPU API where supported.
- **OpenGL 3.3 fallback** retains the complete built-in effects pipeline
  when SDL_GPU initialization or shader support is unavailable. A basic
  `SDL_Renderer` backend remains the final compatibility fallback.
- **GPU-side screen effects** — scanlines, bloom, distortion, dot mask,
  and bicubic filtering run through native SDL_GPU shaders or the OpenGL
  fallback rather than CPU post-processing.
- **Custom display effects** use portable `.altfx` shader packages on
  SDL_GPU and OpenGL/GLES. Configure System → Display Effects supports
  browsing, reloading and clearing effects. Native Windows `.cgp` presets
  need conversion; see the [effect format and porting guide](docs/custom-display-effects.md).
- The macOS loop drains an autorelease pool every frame to prevent unbounded
  memory growth in graphics and window-system calls.

### Gaming Mode UI (touch / mobile)

A second, coexisting UI optimised for handheld and gamepad-first
usage. The full desktop UI is still available on the same build; the
Gaming Mode layout is picked up automatically on Android and can be
toggled on desktop.

- **Slide-in hamburger drawer** with full-screen panels for file
  browsing, settings, disk manager, and a game library.
- **On-screen Atari joystick, d-pad, fire buttons and console keys**,
  with three size presets, three stick styles (analog / 8-way / 4-way
  d-pad), haptic feedback on Android, and layouts that respect
  status-bar / nav-bar / notch safe-area insets.
- **Full gamepad navigation** of every menu and dialog (ImGui
  `NavEnableGamepad`), so the emulator is usable on a phone with a
  clip-on controller and no touchscreen interaction at all.
- **Virtual Atari XL/XE keyboard** overlay (virtual keyboard,
  clickable keys) for entering BASIC or DOS commands on touch devices. Navigable by touch, mouse, or gamepad d-pad.
- **First-run firmware setup wizard** that walks new users through
  picking an OS/BASIC/game ROM once, instead of diving straight into
  the full Configure System dialog.
- **Automatic suspend / resume on Android** — the emulator snapshots
  its state when the app goes to background and can restore it on next
  launch after process termination. An explicit boot image takes precedence.
  See the [background-recovery notes](docs/android-background-recovery.md)
  for tested behavior and remaining device checks.

### Game library

- **Indexed game library** with a threaded background scanner that
  walks user-configured game folders, matches cover art / screenshots
  from sibling directories, and remembers last-played times and play
  counts. List and grid views at three sizes. The Windows build has
  no equivalent browser — it relies on the OS file manager. Games can
  also be assigned reusable launch profiles, including shared profiles
  for multiple titles; temporary game sessions protect saved settings.

### FujiNet: internet software, network games and BBS

Open **Tools → FujiNet → Set up FujiNet automatically** on a desktop
build. AltirraSDL downloads and verifies the official FujiNet-PC runtime,
then starts it and boots the Atari. No physical FujiNet is required.

After setup, choose **Network games / Lobby** or **BBS directory** to
launch the official Atari clients. FujiNet-PC starts automatically when
needed; progress and cancellation stay in the tool window. Setup moves
under **Setup and full controls**, where the connection, runtime, web
interface and local-file/URL boot tools remain available. Local disk
images, unsaved disk changes and BASIC are preserved.

The managed runtime automatically avoids occupied web ports. **Repair
automatic setup** rebuilds its local connection and restarts FujiNet-PC
while keeping the server list, SD files and current Atari game.

FujiNet's Game Lobby is separate from AltirraSDL's **Online Play** below;
its network games need software written for FujiNet. The shortcuts use
the managed desktop runtime. Android can connect to an external FujiNet;
browser builds cannot use NetSIO. See the [FujiNet guide](docs/fujinet.md)
for prerequisites, manual connections, CONFIG browsing and troubleshooting.

### Online play (netplay)

Two-player netplay over the internet or LAN — a fork-only
feature with no upstream equivalent. Peers boot with matching machine
configuration and synchronize inputs frame by frame. Native clients
use UDP with NAT traversal and a lobby relay fallback; browser clients
use a WebSocket bridge to play with browser or native peers.

- **Rollback with deterministic replay** — both peers cold-boot the same
  game from the same master seed. **Automatic** mode uses two frames of
  input delay (~33 ms NTSC / 40 ms PAL), predicts up to eight missing
  frames, and replays corrections when remote input arrives. **Delay only**
  waits for both inputs. Choose the mode in Online Play → Preferences →
  Hosting before hosting. Protocol 9 requires both peers to update.
- **Local session restoration** — leaving online play restores the
  original game, media, unsaved disk changes and paused/running state.
- **Per-game controller layout** — separate joysticks by default,
  or a shared joystick for games that use port 1 for both players' turns.
  Each player uses their local joystick 1 controls in either layout.
- **Matchmaking via an HTTP lobby** — hosts advertise their games
  to a session directory. Native peers attempt a direct UDP connection
  and can fall back to the lobby relay. A public community lobby is
  configured out of the box.
- **Browser netplay** — the lobby’s WebSocket bridge carries browser
  traffic over WSS and translates it for native UDP peers. Self-hosted
  lobbies need the WebSocket listener and reverse-proxy route; see the
  [server guide](server/lobby/README.md#websocket-bridge-browser-netplay).
- **Federated lobbies** — `~/.config/altirra/lobby.ini` lists any
  number of lobby servers; Browse merges entries from all enabled
  ones and Host announces to all of them. LAN discovery via UDP
  broadcast is supported alongside HTTP lobbies.
- **Reference lobby server** ships in-tree ([`server/lobby/`](server/lobby/))
  — a C++ service with in-memory state, no database, a Dockerfile,
  and a hardened systemd unit.
- **Game file transfer on connect** — the host ships the XEX / ATR /
  CAR bytes (up to ~32 MB) to the joiner as part of the handshake,
  so the joiner doesn't need the ROM in their own library.
- **Machine-config lock** — kernel ROM CRC32, BASIC ROM CRC32,
  hardware model (800 / XL / XE / 5200), memory, and video standard
  are frozen into the host's `NetBootConfig` and verified on the
  joiner. Sessions whose firmware you don't have are flagged in the
  Browse list with the exact CRC32 to import.
- **Public or private offers** — per-offer entry code; hosts can
  choose **auto-accept** or **prompt on each join** with a 20-second
  modal (Allow / Deny / timeout).
- **HTTPS** supported via a reverse proxy (nginx example included);
  the lobby reads `X-Forwarded-For` so per-IP rate limiting still
  works correctly behind a proxy.

See [NETPLAY.md](NETPLAY.md) for the user and operator guide, and
[server/lobby/README.md](server/lobby/README.md) for relay and browser
transport details.

### AltirraBridge: JSON-over-socket scripting / automation

A local IPC server exposed by the SDL build that lets tools drive the
emulator without going through the UI. This is a fork-only feature.

- Line-delimited JSON over TCP loopback (or Unix socket on non-Win).
- Covers execution control (pause/resume/step/reset), register and
  memory access (peek/poke/PEEK/POKE), input injection (joystick,
  keyboard), boot / load commands, screenshot capture, and frame
  synchronisation, symbol loading, disassembly, breakpoints,
  watchpoints, profiling, save states, and execution history.
- **Three first-party client SDKs** ship alongside: Python
  (stdlib-only), C (libc + Winsock on Windows),
  and Free Pascal. See the SDK documentation for each client’s API.
- **Analyzer toolkit** (Python) on top of the Bridge: hardware
  register classification, PORTB decoding, recursive-descent
  disassembly, variable cross-referencing, PC sampling, DLI chain
  tracking, memory diffing, and a MADS-source exporter that can
  round-trip a running XEX back to reassemblable 6502 source.
- **Standalone `AltirraBridgeServer`** target provides the same protocol
  without the ImGui frontend. It uses private, deterministic settings
  by default; `--settings=user` reads a private copy of the user’s
  settings without writing them back. Build it with
  `-DALTIRRA_BRIDGE_SERVER=ON`; the GUI bridge is enabled by default
  and activated with `--bridge`.

See the [Bridge overview](src/AltirraSDL/AltirraBridge/README.md),
[getting started guide](src/AltirraSDL/AltirraBridge/docs/GETTING_STARTED.md),
and [command reference](src/AltirraSDL/AltirraBridge/docs/COMMANDS.md).

### Automation & headless operation

- **`--test-mode`** activates ImGui Test Engine integration and opens
  a Unix domain socket at `/tmp/altirra-test-<pid>.sock` on POSIX
  platforms, or a named pipe at `\\.\pipe\altirra-test-<pid>` on Windows.
  A small text protocol (`ping`,
  `click <window> <label>`, `boot_image`, `wait_frames`,
  `screenshot`, `query_state`, etc.) plus automatic widget
  enumeration makes the UI scriptable for CI and agent-driven QA.
- **`--headless`** runs the full emulator without opening a window,
  intended for containerised / server-side automation paired with
  the Bridge. It disables wall-clock frame pacing so emulation runs
  at host CPU speed while preserving internal PAL/NTSC timing. Add
  `--pacing=realtime` to keep PAL/NTSC wall-clock speed in headless
  automation.
- **MCP server** for AI clients (Model Context Protocol, Streamable
  HTTP on `http://127.0.0.1:7750/mcp`): enable it under Configure
  System > Emulator > MCP Server, or run the headless
  `AltirraBridgeServer --mcp[=PORT] [--mcp-token=TOKEN]`.

### Desktop search, fonts, and Explorer tools

- **Search actions and settings** with **Ctrl+K** (default shortcut),
  including device names and descriptions. Results execute registered
  commands or open and highlight existing configuration controls.
- **Configurable UI and debugger fonts**, with separate family, style,
  and size selections, installed-font discovery, and bundled fallbacks.
- **Disk, XEX, and Cartridge Explorers** under **Tools > Explorers**,
  with independent document windows, byte previews, multiple selection,
  and exports. Disk Explorer also supports filesystem editing, sector
  inspection, and running embedded executables.
- **Native macOS menu bar** integrates desktop menus with the system
  menu bar.
- **Emulated-screen accessibility** publishes Atari text to Windows UI
  Automation, macOS VoiceOver and Linux AT-SPI. Enable it in Configure
  System → Emulator → Accessibility. See the [screen-reader guide](docs/emulated-screen-accessibility.md)
  for dependencies and coverage; this setting covers the Atari display.

See [desktop search and profiles](docs/desktop-search-profiles.md) and
[Explorer tools](docs/sdl-explorers.md) for usage and verification details.

### Browser and libretro targets

- **Experimental WebAssembly build** runs the SDL frontend in a browser,
  with uploaded games and firmware kept in persistent browser storage.
  It uses SDL_Renderer/WebGL2 and supports netplay through the lobby’s
  WebSocket bridge. Native screen effects and AltirraBridge are
  unavailable in this target. See
  [BUILD.md](BUILD.md#webassembly-build-experimental-browser-target)
  and the [embedding kit](src/AltirraSDL/cmake/embed_kit/README.md).
- **RetroArch/libretro core** builds independently of the SDL frontend
  on Linux, macOS, Windows, and Android. It includes controller mappings,
  a virtual Atari/5200 keyboard, save states, disk playlists, safe disk
  sidecar saves, and memory access for achievements and POKE cheats.
  Build with `./build.sh --libretro`; see the
  [core guide](src/AltirraLibretro/README.md) for controls and options.

### Small quality-of-life differences

- The palette solver (Color Image Reference equivalent) is also
  reachable over the Bridge via `PALETTE_LOAD_ACT`, so external
  tooling — for example palette-matching pipelines that feed
  Graph2Font — can load `.act` tables into a running emulator
  without going through the UI.
- Explicit Configure System sidebar layout rather than the Windows
  tree-sidebar, retaining the native category groupings with ImGui
  widgets. Configuration pages stay together in Configure System.

The [official Altirra documentation](https://www.virtualdub.org/altirra.html)
remains the reference for the emulation engine and native Windows frontend.
The linked repository guides describe SDL-specific behavior and limitations.

## Current status

**Work in progress.** The SDL frontend runs the emulation engine and
provides desktop and Gaming Mode interfaces, configuration pages, debugger
panes, and tools. Full parity with the native Windows UI is still being
worked on; this README does not imply that every Windows command or
device dialog has been ported. WebAssembly remains experimental.

CI builds desktop packages, Android APKs, the standalone Bridge server,
WebAssembly, and libretro cores. Regression checks cover upstream CPU,
JSON, and virtual-disk behavior, with native Windows/macOS and Linux
sanitizer jobs, alongside UI and subsystem tests. Build success does not
establish runtime coverage on every device.

## Build and run

For a desktop release build on the current platform:

```sh
./build.sh
```

For example, on Linux the executable is
`build/linux-release/src/AltirraSDL/AltirraSDL`. The Linux CMake preset
can also be used directly:

```sh
cmake --preset linux-release
cmake --build --preset linux-release --parallel
./build/linux-release/src/AltirraSDL/AltirraSDL
```

Requirements include **CMake 3.24+**, a **C++23 compiler**, Git, and
platform build dependencies. SDL3, SDL3_image, and Dear ImGui are fetched
as needed. Default Linux/macOS desktop presets also build static FFmpeg
and libx264 for MP4 recording; this requires `pkg-config` and a
Make-compatible build tool. See [BUILD.md](BUILD.md) for system packages,
recording options, packaging, and Android/browser builds.

The native Windows build remains available through `src/Altirra.sln`.

## AI disclosure

This frontend is developed heavily with the assistance of AI coding tools. Depending on your perspective, that's either a pragmatic way to tackle a large porting effort or a reason to keep it well away from a hand-crafted passion project — which is one more good reason this fork is maintained separately.

## Relationship to upstream

- The emulation core is Altirra, written by Avery Lee. All the hard work is his.
- The upstream author is aware of this fork and does not object to it, but is not involved in its development and does not provide support for it.
- Cross-compiler portability adaptations are kept minimal. Targeted core
  bug fixes and their regression tests are recorded in the
  [upstream sync history](syncing-with-upstream/HISTORY.md).
- This fork tracks upstream Altirra releases manually (diff + merge), since the upstream source is distributed as zip snapshots rather than via a git repository.

## License

Altirra is licensed under the **GNU General Public License v2 or later (GPLv2+)**, with a GPLv2 + exemption for certain core libraries. This fork inherits the same license. See [LICENSE](LICENSE) for details.

## Documentation

- [BUILD.md](BUILD.md) — building AltirraSDL from source on Linux,
  macOS, Windows (CMake), Android, and WebAssembly.
- [NETPLAY.md](NETPLAY.md) — playing online: host a game, join a
  game, point at a different lobby, and step-by-step instructions
  for running your own lobby server (localhost, Docker, VPS with
  systemd, and behind an HTTPS reverse proxy).
- [FujiNet](docs/fujinet.md) — automatic setup, FujiNet Game Lobby,
  BBS directory, local/URL software and external devices.
- [Custom display effects](docs/custom-display-effects.md) — portable
  shader packages and conversion from native Windows presets.
- [Emulated-screen accessibility](docs/emulated-screen-accessibility.md) —
  desktop screen-reader support, dependencies and limitations.
- [Android background recovery](docs/android-background-recovery.md) —
  suspend/resume behavior and GPU/device validation limits.
- [server/lobby/README.md](server/lobby/README.md) — lobby server
  build / API reference / protocol coupling with the client.
- [AltirraBridge](src/AltirraSDL/AltirraBridge/README.md) — scripting,
  standalone headless server, SDKs, and examples.
- [Libretro core](src/AltirraLibretro/README.md) — RetroArch controls,
  core options, disk handling, and builds.
- [Desktop search and profiles](docs/desktop-search-profiles.md) —
  finding settings, font selection, and reusable game configurations.
- [Explorer tools](docs/sdl-explorers.md) — disk, executable, and
  cartridge inspection and editing.
- [Browser embedding kit](src/AltirraSDL/cmake/embed_kit/README.md) —
  embedding and configuring the WebAssembly frontend.
- [Upstream synchronization](syncing-with-upstream/README.md) and
  [history](syncing-with-upstream/HISTORY.md) — merge workflow and baseline.
- [Regression tests](tests/upstream/README.md) — production-code tests
  and platform validation.

## Links

- **Official Altirra** — [virtualdub.org/altirra.html](https://www.virtualdub.org/altirra.html)

## Acknowledgments

All credit for the emulation engine, hardware research, reimplemented firmware, debugger, and everything that makes Altirra extraordinary goes to **Avery Lee (phaeron)**.
