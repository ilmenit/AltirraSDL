# AltirraSDL

**Unofficial** SDL3 + Dear ImGui frontend for [Altirra](https://www.virtualdub.org/altirra.html), the cycle-accurate 8-bit Atari computer emulator by Avery Lee (phaeron).

> **This is not an official Altirra release.** It is an independent, community-maintained fork that replaces the native Windows UI with a cross-platform SDL3 + Dear ImGui frontend. It is not affiliated with, endorsed by, or supported by the original author. For the official Altirra emulator, please visit [virtualdub.org/altirra.html](https://www.virtualdub.org/altirra.html).

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

AltirraSDL is a fork that replaces the Win32-based frontend with SDL3 for graphics, input, and audio, and Dear ImGui for the user interface. The emulation core is unchanged — all accuracy and compatibility comes directly from upstream Altirra.

### Goals

- Make Altirra runnable on Linux (the primary target), macOS and Android.
- Keep the delta from upstream as small as possible to ease merging of future releases
- Provide a functional UI close to original one, but not a pixel-perfect recreation of the Windows frontend

### Non-goals

- Replacing or competing with the official Windows build of Altirra (use the real thing on Windows — it's better)

## What this fork adds on top of upstream Altirra

Because the Windows and SDL frontends have different audiences and
different constraints, AltirraSDL ships a handful of features that do
not exist in the Windows-native build. They are additive — nothing
upstream does is taken away — and most of them are only interesting
*because* you're running on Linux, macOS, or Android.

### Cross-platform foundation

- **Runs on Linux, macOS, and Android** in addition to Windows. The
  Windows `.sln` build is still preserved upstream-identical; this
  fork contributes the SDL3 + Dear ImGui frontend that lets the same
  emulation core run everywhere SDL3 does.
- **Portable settings** stored as a plain INI at
  `~/.config/altirra/settings.ini` (same format as Windows portable
  mode). No registry, no hidden state.
- **Native OS file dialogs** via SDL3 — open/save dialogs use GTK on
  Linux, Cocoa on macOS, the system picker on Android. Last-used
  directories persist across launches.
- **Window drag-and-drop** of disk, cassette, cartridge, and executable
  images directly onto the running window.

### Rendering: OpenGL + retro shader presets

- **OpenGL 3.3 display backend** as the primary renderer (Windows uses
  Direct3D). A software `SDL_Renderer` backend remains as a safe
  fallback on systems without GL support.
- **librashader integration** — load RetroArch-style shader preset
  chains (`.slangp`, `.glslp`) and tweak their runtime parameters
  from the UI. librashader is loaded dynamically at runtime, so it's
  optional: the emulator runs fine without it and the UI simply
  hides the shader options.
- **GPU-side screen effects** — scanlines, bloom, distortion, dot
  mask, and bicubic filtering are implemented as GLSL shaders in the
  GL backend rather than CPU post-processing, which keeps them cheap
  at high output resolutions.

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
  clickable keys) for entering BASIC or DOS commands on  touch devices.
- Navigable by touch, mouse, or gamepad d-pad.
- **First-run firmware setup wizard** that walks new users through
  picking an OS/BASIC/game ROM once, instead of diving straight into
  the full Configure System dialog.
- **Automatic suspend / resume on Android** — the emulator snapshots
  its state when the app goes to background and restores it on next
  launch, so incoming calls or app switches don't lose your session.

### Game library

- **Indexed game library** with a threaded background scanner that
  walks user-configured game folders, matches cover art / screenshots
  from sibling directories, and remembers last-played times and play
  counts. List and grid views at three sizes. The Windows build has
  no equivalent browser — it relies on the OS file manager.

### AltirraBridge: JSON-over-socket scripting / automation

A local IPC server exposed by the SDL build that lets tools drive the
emulator without going through the UI. This is a fork-only feature.

- Line-delimited JSON over TCP loopback (or Unix socket on non-Win).
- Covers execution control (pause/resume/step/reset), register and
  memory access (peek/poke/PEEK/POKE), input injection (joystick,
  keyboard), boot / load commands, screenshot capture, and frame
  synchronisation. Beta phases add symbol loading, disassembly,
  breakpoints, watchpoints, a profiler, save states, and history
  export.
- **Three first-party client SDKs** ship alongside: Python
  (stdlib-only, async-friendly), C (single file, libc + winsock),
  and Free Pascal — all three expose the same surface.
- **Analyzer toolkit** (Python) on top of the Bridge: hardware
  register classification, PORTB decoding, recursive-descent
  disassembly, variable cross-referencing, PC sampling, DLI chain
  tracking, memory diffing, and a MADS-source exporter that can
  round-trip a running XEX back to reassemblable 6502 source.

### Automation & headless operation

- **`--test-mode`** activates ImGui Test Engine integration and opens
  a Unix domain socket (or named pipe on Windows) at
  `/tmp/altirra-test-<pid>.sock`. A small text protocol (`ping`,
  `click <window> <label>`, `boot_image`, `wait_frames`,
  `screenshot`, `query_state`, etc.) plus automatic widget
  enumeration makes the UI scriptable for CI and agent-driven QA.
- **`--headless`** runs the full emulator without opening a window,
  intended for containerised / server-side automation paired with
  the Bridge.

### Small quality-of-life differences

- The palette solver (Color Image Reference equivalent) is also
  reachable over the Bridge via `PALETTE_LOAD_ACT`, so external
  tooling — for example palette-matching pipelines that feed
  Graph2Font — can load `.act` tables into a running emulator
  without going through the UI.
- Explicit Configure System sidebar layout rather than the Windows
  tree-sidebar, chosen to match ImGui's docking idioms. Same option
  set, same groupings, different widget style.

If something is in Windows Altirra but missing from this list, the
intent is that the fork matches upstream exactly — not that the
feature has been replaced. The upstream Altirra page is the canonical
reference for everything that is *not* called out above.

## Current status

**Work in progress.** Core emulation works. The ImGui-based UI covers most of functionality, but some elements aren't finished.

## AI disclosure

This frontend is developed heavily with the assistance of AI coding tools. Depending on your perspective, that's either a pragmatic way to tackle a large porting effort or a reason to keep it well away from a hand-crafted passion project — which is one more good reason this fork is maintained separately.

## Relationship to upstream

- The emulation core is Altirra, written by Avery Lee. All the hard work is his.
- The upstream author is aware of this fork and does not object to it, but is not involved in its development and does not provide support for it.
- Cross-compiler portability fixes in platform-agnostic files are kept minimal and behind `#ifdef` guards. They do not change behavior on Windows.
- This fork tracks upstream Altirra releases manually (diff + merge), since the upstream source is distributed as zip snapshots rather than via a git repository.

## License

Altirra is licensed under the **GNU General Public License v2 (GPLv2)**, with a GPLv2 + exemption for certain core libraries. This fork inherits the same license. See [LICENSE](LICENSE) for details.

## Links

- **Official Altirra** — [virtualdub.org/altirra.html](https://www.virtualdub.org/altirra.html)

## Acknowledgments

All credit for the emulation engine, hardware research, reimplemented firmware, debugger, and everything that makes Altirra extraordinary goes to **Avery Lee (phaeron)**. 
