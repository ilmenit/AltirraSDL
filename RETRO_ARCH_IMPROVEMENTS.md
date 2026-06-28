# RetroArch / libretro Core — Improvement Plan

Status: **implemented locally; external readiness gates pending** — 2026-06-28
Scope: `src/AltirraLibretro/` (the `altirra_libretro` core) and its CI
(`.github/workflows/libretro-core.yml`).

This document defines how to take the Altirra libretro core from "works on a
desktop with a keyboard" to a **proper, polished core that behaves well on
every device class RetroArch runs on**: desktops, living-room/TV setups,
retro handhelds, and phones/tablets. It is grounded in an audit of the
current implementation (file/line references throughout), compares against the
de-facto-standard `atari800` core that our users already know, and lays out a
prioritized, phased roadmap with concrete acceptance criteria.

---

## 0. Current implementation status

This section tracks the current worktree state. The original audit and roadmap
below are retained as historical context, but the gaps called out there have
mostly been closed in `src/AltirraLibretro/`.

| Item | Status | Evidence |
|---|---|---|
| A1 Virtual keyboard | Implemented | `libretro_vkbd.cpp/.h`; rendered into XRGB frames; smoke test detects overlay |
| A2 Reset binding | Implemented | F5 / Shift+F5 plus rebindable pad combos; VKBD WARM/COLD keys |
| A3 Keyboard focus metadata | Implemented | `needs_kbd_mouse_focus = "false"` in `.info`; validator requires it |
| A4 Spare pad buttons | Implemented | Y/X/L2/R2/L3/R3 concurrent key bindings and descriptors |
| A5 Analog-to-joystick | Implemented | Left analog drives digital joystick directions; smoke test polls analog Y |
| A6 Concurrent button-to-key | Implemented | Spare pad buttons Y/X/L2/R2/L3/R3 inject Atari computer keys through the canonical keyboard-event path while joystick remains live; 5200 keypad uses input-map controller triggers |
| A7 Rebindable handheld-safe toggles | Implemented | `altirra_vkbd_toggle`, `altirra_warm_reset_combo`, `altirra_cold_reset_combo` |
| A8 5200 keypad from pad | Implemented | VKBD 5200 page emits canonical `kATInputTrigger_5200_*` triggers through a 5200 controller map |
| A9 Keyboard console-key remap | Implemented | `altirra_key_start`, `altirra_key_select`, `altirra_key_option` |
| B1 Achievements RAM | Implemented | 64K `RETRO_MEMORY_SYSTEM_RAM` plus memory map descriptor |
| B2 RetroArch cheats | Implemented | POKE-style cheats, applied each frame; `.info cheats = "true"` |
| B3 Disk save-back | Implemented | Disk images default to VRWSafe sidecars under `Altirra/saves/`; `altirra_disk_write_mode = original_rw` opts into write-through |
| B4 Subsystem loading | Implemented | `Cartridge + Disk` subsystem and `retro_load_game_special()` |
| C1 Content-aware machine default | Implemented | `altirra_system = auto`; `.a52` and headered 5200 carts select 5200 |
| C2 Performance tier | Implemented | `altirra_performance_tier`; performance disables auto artifacting/audio filters |
| D1 Aspect ratio option | Implemented | `altirra_aspect` with 4:3, square/pixel-perfect, NTSC PAR, PAL PAR |
| D2 Geometry audit | Locally verified | Smoke matrix covers standards/artifacting/crop against advertised 912x624 max |
| D3 Software render | Confirmed intentional | `.info hw_render = "false"`; artifact validator checks metadata |
| E1 Android `armeabi-v7a` | Locally verified; CI/device proof pending | Workflow matrix and `android-libretro-armv7a` preset added; local NDK 27.2 builds pass artifact verification for `ELF32`/`ARM` (`armeabi-v7a`) and `ELF64`/`AArch64` (`arm64-v8a`) |
| E2 Graduate `is_experimental` | Pending external readiness | Kept `is_experimental = "true"` until RetroArch/device readiness reports pass |
| E3 iOS/tvOS | Out of scope | Stretch item only |
| E4 `.info` metadata | Implemented for landed features | Validator requires cheats/memory descriptors/saves/subsystem/no keyboard focus |
| F1 Smoke coverage | Implemented for local contract | Options, descriptors, VKBD overlay plus key injection visible in Atari OS RAM, achievements RAM, cheats, `.a52` content-aware 5200 load, exported `retro_reset`, F5/Shift+F5 and Select+Start/Select+L reset state-change semantics, concurrent joystick+key polling with Atari OS key-state visibility, XEX+SIO disk save-back sidecar write/reload round-trip, failed disk/subsystem-load cleanup, disk control including invalid replacement rejection, `.m3u` expansion, and closed-tray list-mutation rejection, geometry, package/artifact checks |
| F2 User docs/descriptors | Implemented | README controls, docs draft, README/docs validators, and active/content-aware scheme descriptor refresh |

Local validation currently passing:

- `cmake --build build/linux-libretro --target AltirraLibretro -j$(nproc)`
- `cmake --build build/linux-libretro-smoke --target AltirraLibretro AltirraLibretroSmoke -j$(nproc)`
- `ctest --test-dir build/linux-libretro-smoke/src/AltirraLibretro --output-on-failure -R AltirraLibretroSmoke`
- `bash scripts/validate-libretro-info.sh`
- `bash scripts/validate-libretro-docs.sh`
- `bash scripts/validate-libretro-readiness-report.sh --self-test`
- `bash scripts/verify-libretro-artifact.sh build/linux-libretro-smoke/src/AltirraLibretro/altirra_libretro.so build/linux-libretro-smoke/src/AltirraLibretro/altirra_libretro.info`
- `ANDROID_NDK_HOME=/home/ilm/Android/Sdk/ndk/27.2.12479018 cmake --build --preset android-libretro-arm64 -j$(nproc)`
- `ANDROID_NDK_HOME=/home/ilm/Android/Sdk/ndk/27.2.12479018 cmake --build --preset android-libretro-armv7a -j$(nproc)`
- Android artifact verification with exact `ELF64`/`AArch64` and `ELF32`/`ARM`
- `./build.sh --libretro-flatpak --package --jobs $(nproc)`
- `bash scripts/prepare-libretro-upstream.sh`
- `./build.sh --libretro --package --jobs $(nproc)`
- `bash scripts/run-libretro-retroarch-smoke.sh --package build/linux-libretro-flatpak-kde610/AltirraLibretro-4.40-linux-x86_64-flatpak-kde610.tar.gz --retroarch flatpak --verify-package --frames 120 --timeout 30`
- `bash scripts/create-libretro-readiness-report.sh --package build/linux-libretro-flatpak-kde610/AltirraLibretro-4.40-linux-x86_64-flatpak-kde610.tar.gz --verify-package --retroarch-smoke-summary build/libretro-readiness/retroarch-smoke-20260628-071654/summary.md --output build/libretro-readiness/generated-with-smoke.md`
- `bash scripts/create-libretro-manual-test-kit.sh --package build/linux-libretro-flatpak-kde610/AltirraLibretro-4.40-linux-x86_64-flatpak-kde610.tar.gz --retroarch-smoke-summary build/libretro-readiness/retroarch-smoke-20260628-071654/summary.md --report build/libretro-readiness/generated-with-smoke.md --output-dir build/libretro-readiness/manual-kit-current`
- `bash scripts/validate-libretro-readiness-report.sh --self-test` now enforces
  embedded seven-case RetroArch smoke evidence in completed reports.
- `git diff --check`

Additional frontend evidence:

- `bash scripts/run-libretro-retroarch-smoke.sh --package build/linux-libretro-flatpak-kde610/AltirraLibretro-4.40-linux-x86_64-flatpak-kde610.tar.gz --retroarch flatpak --verify-package --frames 120 --timeout 30` produced clean no-content, generated `.xex`, generated `.atr`, generated `.a52`, generated `.cas`, generated `.m3u`, and generated `.zip` load/run/unload summaries at `build/libretro-readiness/retroarch-smoke-20260628-071654/summary.md` with no coredump and no normal-profile config path references.
- `scripts/create-libretro-readiness-report.sh` can now embed that smoke
  summary in prefilled readiness reports via `--retroarch-smoke-summary`;
  `build/libretro-readiness/generated-with-smoke.md` records the package check
  and frontend smoke evidence while intentionally leaving manual signoff fields
  empty.
- `scripts/validate-libretro-readiness-report.sh` now requires the embedded
  smoke summary and all seven generated cases before accepting a completed
  readiness report.
- `scripts/validate-libretro-readiness-report.sh` now also requires the
  readiness report's Gamepad UX table, so promotion cannot be signed off
  without controller-only joystick, console-key, VKBD, 5200 keypad, reset, and
  spare-button-remap evidence.
- `scripts/validate-libretro-docs.sh` now validates the README's critical
  controller-only controls, reset shortcuts, 5200 VKBD page, safe disk
  save-back, and runtime Disk Control guidance in addition to the upstream docs
  draft.
- `scripts/validate-libretro-readiness-report.sh` now requires explicit
  manual Disk Control checks for eject/swap/remount behavior and rejection of
  media-list changes while the tray is closed.
- `scripts/create-libretro-manual-test-kit.sh` now creates a tester handoff
  bundle containing the package, generated smoke fixtures, a second swap disk
  fixture for Disk Control testing, smoke summary, and prefilled report; the
  current kit is at
  `build/libretro-readiness/manual-kit-current/`.
- `.github/workflows/libretro-core.yml` now tracks and syntax-checks the
  RetroArch smoke and manual readiness kit helpers, so changes to promotion
  gate scripts trigger libretro CI preflight instead of bypassing it.
- The smoke runner now defaults to RetroArch's `null` video/input drivers for
  deterministic automation. Visible Flatpak/Wayland GL runs on this desktop
  have also exposed a frontend-side `--max-frames` stall after core load and
  geometry setup; the runner has a hard timeout and fails with logs instead of
  hanging when non-null drivers are requested.

Remaining proof before removing `is_experimental = "true"`:

- CI should independently build and verify both Android ABIs; local NDK builds
  now pass exact Android ELF class/machine checks (`ELF32`/`ARM` for
  `armeabi-v7a`, `ELF64`/`AArch64` for `arm64-v8a`).
- A completed real RetroArch readiness report must pass
  `scripts/validate-libretro-readiness-report.sh`. Flatpak RetroArch
  launch/unload smoke now has a deterministic headless path for no-content,
  generated executable, generated disk, generated 5200 cartridge, generated
  cassette, generated playlist, and generated compressed-content fixtures, but
  visible-session, controls, and device behavior still require manual matrix
  testing.
- Manual/device testing must confirm load/run/reset/close/exit behavior across
  the content matrix in `docs/libretro-readiness-report-template.md`.
- Local smoke coverage now exercises the surrounding options, descriptors, VKBD
  overlay plus key injection visible in Atari OS RAM, concurrent joystick+key
  polling with Atari OS key-state visibility, XEX+SIO disk save-back sidecar
  write/reload round-trip with original-image preservation, `.a52` content-aware 5200
  loading, exported `retro_reset`, F5/Shift+F5 and Select+Start/Select+L reset
  state-change semantics, VKBD-toggle descriptor rebinding, stale option-value
  fallback, memory maps, cheats, failed disk/subsystem-load cleanup, disk
  control API, geometry, save states plus corrupted-state rejection, metadata,
  artifacts, and packaging.

---

## 1. Goals

1. **Gamepad-first.** Every essential function (console keys, Reset, typing,
   disk swap) must be reachable with **only a controller** — no keyboard
   required. This is the single biggest UX gap today and the precondition for
   handhelds, TV boxes, and phones being usable at all.
2. **Match the conventions users expect.** Where the `atari800` core has set a
   convention (L3 = virtual keyboard, Select/Start = console keys), follow it
   so muscle memory transfers. Diverge only with a documented reason.
3. **Run everywhere we ship a binary.** Cover the real ABIs in the wild,
   including 32-bit Android (`armeabi-v7a`) for TV boxes and budget handhelds.
4. **Full feature parity with the libretro contract.** Cheats, achievements
   memory, save-back, and content-aware defaults should actually work, not be
   declared and stubbed.
5. **Never regress accuracy.** All of this rides on the battle-tested Altirra
   core; UX layers sit *around* it and must not alter emulation behavior.

---

## 2. Target device classes and what "great UX" means for each

| Class | Input reality | Display | Perf budget | Top needs |
|---|---|---|---|---|
| **Desktop** (Linux/Win/macOS) | Keyboard + mouse + optional pad | Any res, shaders | High | Already good; needs Reset + cheats + save-back |
| **TV / living room** (Android TV box, Steam/console-likes) | Gamepad only, no keyboard | 1080p+, 10-ft UI | Mid (TV boxes often weak 32-bit ARM) | Virtual keyboard, gamepad console keys, `armeabi-v7a` build |
| **Retro handhelds** (Anbernic/Powkiddy/RG-series; muOS, ROCKNIX, Knulli; some Android) | D-pad + face + shoulders, maybe analog, **no keyboard** | Small 4:3/16:9 panels | Low–mid (artifacting is expensive) | Virtual keyboard, performance-safe defaults, correct aspect, analog→joystick |
| **Phones / tablets** (Android/iOS) | Touchscreen + optional BT pad | Variable, portrait/landscape | Mid | Virtual keyboard, touch overlays, gamepad parity |

**Implication:** the work is dominated by *input/UX* and *packaging*, not by
emulation. The core already emulates faithfully (see §3); the device classes
above all fail today for the same reason — **you can't fully drive the machine
without a keyboard.**

---

## 3. Baseline state before this implementation

Historical inventory from the original audit, retained so future reviewers can
see what changed:

- **Core options v2 with categories** — System / Hardware / Media / Video /
  Audio / Input sidebar (`libretro.cpp:650`). 21 options incl. machine model,
  memory size, video standard, CPU, artifacting, overscan, SIO patch, VBXE,
  SoundBoard, Covox, stereo POKEY, drive sounds, audio filters
  (keys enumerated at `libretro.cpp:776+`).
- **Wide machine coverage** — 800 / 800XL / 1200XL / 130XE / XEGS / 5200,
  memory 8K→1088K (`libretro.cpp:671-698`).
- **Save states** — `retro_serialize`/`unserialize` implemented with a
  fixed-size cache (`libretro.cpp:2819-2839`); `savestate = "true"` in
  `altirra_libretro.info`.
- **Disk control (ext) interface** — multi-disk + `.m3u` swap and eject
  (`libretro.cpp:630`, `RegisterDiskControl`; `LoadM3U` at `:465`).
- **Rich input devices** — joystick, paddle (analog), ST mouse, light gun,
  light pen, pointer, keyboard (callback + polled fallback)
  (`libretro.cpp:2284-2341` descriptors / controller info).
- **Console keys partially wired** — RetroPad Start→START, Select→SELECT,
  L→OPTION (`libretro.cpp:1400-1410`, `2233-2247`); keyboard F2/F3/F4→console,
  F7/Pause→Break via the canonical Windows keymap
  (`uikeyboard.cpp:497-523`, routed through `HandleKeyboardEvent`,
  `libretro.cpp:1798`).
- **Region/timing** — NTSC/PAL fps switch + region report
  (`FillAvInfo` `:2378`, `retro_get_region` `:2851`).
- **Cross-platform CI** — Linux x86_64/aarch64/armv7hf, macOS arm64/x86_64,
  Windows x86_64/arm64, **Android arm64-v8a**
  (`.github/workflows/libretro-core.yml`).
- **Achievements declared** — `RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS`
  (`libretro.cpp:2282`).
- **`supports_no_game`, input descriptors, disk control, core options 2.0**
  all advertised in `altirra_libretro.info`.

---

## 4. Original gap analysis (prioritized)

Legend: **P0** = blocks a device class / advertised-but-broken; **P1** =
major UX; **P2** = polish / completeness; **P3** = nice-to-have.

These gaps are the original work items. See section 0 for current completion
status and remaining external proof gates.

### A. Input & on-device control (the core problem)

| # | Gap | Evidence | Pri |
|---|---|---|---|
| A1 | **No virtual / on-screen keyboard.** No overlay, no toggle. Gamepad-only devices cannot type — fatal for BASIC, disk menus, many games. `atari800` toggles one with **L3**. | no `SET_KEYBOARD_OVERLAY`/overlay code anywhere in core | **P0** |
| A2 | **Reset is completely unbound.** No RetroPad button (`kConsoleRetroIds` has only Start/Select/L, `:1400`) and no keyboard scancode (enum is Start/Select/Option/Break only, `uikeyboard.h:78-83`; F5 = a Windows *UI command*, never wired). Only reachable via a core-option restart. | `:1400`, `uikeyboard.h:78`, `uikeyboard.cpp:941` | **P0** |
| A3 | **`needs_kbd_mouse_focus = "true"`** in `.info` → RetroArch tells users this core *needs* a keyboard. Until A1 lands this is honest; after A1 it should be removed so the core stops scaring off gamepad users. | `altirra_libretro.info` | P1 |
| A4 | **Unused pad buttons.** X, Y, L2, R2, L3, R3 are unmapped (`kRetropadButtonMap` `:1388`). `atari800` uses Y=Space, X=Return, L3=keyboard. We waste them. | `:1388` | P1 |
| A5 | **Analog stick not mapped to the digital joystick.** Only paddle uses analog (`:2223`). Handhelds/pads with sticks can't drive a normal joystick from the stick. | `:1388`, `:2223` | P1 |
| A6 | **No concurrent button→key / button→console binding.** A single gamepad can act as a joystick *or* (via a modal virtual keyboard) a keyboard, **but not both at once**. Games that need the stick *and* keys simultaneously — Star Raiders, flight sims (Solo Flight, ACE), many adventures/RPGs — are unplayable on a pad. The engine **already supports this**: `kATInputControllerType_Keyboard` and `_Console` are valid input-map targets (`inputdefs.h:194,205`) and the core builds maps via `map->AddMapping(...)` (`:1544`). The core simply never exposes per-button key/console assignments, so spare buttons can't send keys while the stick stays a joystick. | `inputdefs.h:194,205`; `:1544` | **P1** |
| A7 | **Toggle/Reset bindings assume clickable sticks.** The natural homes (L3 for VKBD à la `atari800`, R3 for Reset) don't exist on many **retro handhelds** — lots have no stick-click, some no sticks at all. Any toggle must be **rebindable** and reachable via a **button combo** fallback, not a single stick-click only. | (design) | **P1** |
| A8 | **5200 numeric keypad unreachable from a pad.** In 5200 mode the pad already drives the console keys correctly (Start→5200 START, Select→PAUSE, **L→5200 RESET**, `:1394-1396`) — so 5200 *does* have a pad Reset while 8-bit does not (see A2). But the 5200 keypad (`0`–`9`, `*`, `#`), required by many 5200 titles, has no gamepad path and needs the virtual keyboard / button assignment. | `:1388-1398`, `Is5200PortEnabled` `:1422` | P1 |
| A9 | **Keyboard console keys not remappable; need Game Focus.** F2/F3/F4 are hardcoded to the Windows keymap; no core-option to rebind, and on most setups they're swallowed by RetroArch hotkeys unless **Game Focus** is enabled. The button-assignment system (A6) supersedes this for pad users. | `HandleKeyboardEvent` `:1798` | P2 |

### B. libretro contract: declared-but-stubbed

| # | Gap | Evidence | Pri |
|---|---|---|---|
| B1 | **Achievements advertised but no RAM exposed.** `SET_SUPPORT_ACHIEVEMENTS` is set (`:2282`) yet `retro_get_memory_data/size` return `null/0` (`:2857-2862`) and no `SET_MEMORY_MAPS`. RetroAchievements cannot read memory → hardcore/standard cheevos won't work. | `:2282`, `:2857` | **P0** (advertised-broken) |
| B2 | **Cheats are stubs.** `retro_cheat_set`/`reset` are empty (`:2841-2845`); `.info` says `cheats = "false"`. No POKE/Atari cheat support that users expect from RetroArch's cheat UI. | `:2841` | P1 |
| B3 | **Disks mount read-only → no save-back.** `g_sim.Load(..., kATMediaWriteMode_RO, ...)` (`:2739`, `:503`). Games/apps that write to disk (high scores, save disks, productivity) lose data; `libretro_saves = "false"`. No `RETRO_MEMORY_SAVE_RAM`. | `:2739` | **P0** (data loss) |
| B4 | **No subsystem / multi-file load.** `retro_load_game_special` returns `false` (`:2847`). Can't load e.g. cart + disk, or pick a profile, via subsystems. | `:2847` | P2 |

### C. Content & defaults

| # | Gap | Evidence | Pri |
|---|---|---|---|
| C1 | **No content-aware machine selection.** Hardware mode comes *only* from the `altirra_system` option (`MapHardwareMode` `:1035-1045`). Loading a `.a52`/5200 cart while the option says "800XL" does not auto-switch to the 5200. Users must know to flip the option. | `:1035` | **P1** |
| C2 | **No performance-tier defaults for weak devices.** Software render + NTSC-High artifacting is heavy on low-end 32-bit ARM (TV boxes, budget handhelds). No "performance" preset / guidance; no dynamic frameskip. | `kArtifactingValues` `:737`; `retro_run` `:2785` | P2 |

### D. Display

| # | Gap | Evidence | Pri |
|---|---|---|---|
| D1 | **Aspect ratio hardcoded 4:3** for every mode (`MakeGeometry` `:2350`). No correct pixel-aspect (PAR) option, no square-pixel/pixel-perfect choice, no NTSC-vs-PAL PAR difference. Handheld panels especially benefit from an explicit aspect option. | `:2344-2351` | P1 |
| D2 | **Verify `max_width/height = 912×624`** covers VBXE hi-res / "full" overscan; if VBXE exceeds it, geometry clamps. Needs a measured check. | `:2348-2349` | P2 |
| D3 | **Software render only** (`hw_render = "false"`). Acceptable (RetroArch shaders still apply), but worth confirming it's intentional for all targets. | `.info` | P3 |

### E. Packaging & platforms

| # | Gap | Evidence | Pri |
|---|---|---|---|
| E1 | **No Android `armeabi-v7a` build.** Android job hardcodes `-DANDROID_ABI=arm64-v8a` only (`libretro-core.yml`). 32-bit Android TV boxes / old phones are unsupported. `armv7hf` is **Linux glibc**, *not* Android Bionic — it will not run on Android. | workflow Android job | **P0** (explicit user request) |
| E2 | **`is_experimental = "true"`.** Once P0/P1 land, graduate the core. | `.info` | P2 |
| E3 | **No iOS/tvOS build** (RetroArch exists there). Out of scope for now; note as future. | — | P3 |
| E4 | **`.info` metadata trails reality** — `cheats`, `memory_descriptors`, `libretro_saves`, `needs_kbd_mouse_focus` all need updating as features land. | `.info` | track-with-feature |

### F. Testing & docs

| # | Gap | Evidence | Pri |
|---|---|---|---|
| F1 | Smoke tests cover boot/disk/options/geometry/audio/input/save-state, but **no coverage for the new UX** (virtual keyboard, Reset, cheats, save-back, content-aware machine). | `README.md` test list, `tests/` | P1 |
| F2 | **No end-user control reference.** Users don't know F2/F3/F4 = console keys, or any pad mapping. Needs a README "Controls" table + RetroArch input descriptors that match. | `.info`, README | P1 |

---

## 5. Design principles & constraints

- **Do not touch emulation correctness.** All work is in `src/AltirraLibretro/`
  plus the shared keymap helpers; the Win32 `.sln` and SDL3 frontend must keep
  building. Follow the repo's `// AltirraSDL:` annotation rule when editing any
  upstream-tracked file (see `MEMORY.md` → upstream merge markers).
- **Reuse the canonical tables.** Console-key scancodes, keymaps, and input
  codes already exist (`uikeyboard.cpp`, `inputdefs.h`). The virtual keyboard
  must drive the *same* `HandleKeyboardEvent` path so behavior is identical to
  a physical key — no parallel mapping that can drift (cf. the
  "single constant, don't hand-roll" rule in `CLAUDE.md`).
- **Gamepad mappings are additive and overridable.** Defaults must be sane on a
  bare controller, but RetroArch's per-core/per-game remap and input
  descriptors stay authoritative; never fight the frontend.
- **Use the engine input path that matches the target device.** Altirra already
  resolves multiple `ATInputMap`s per physical device every frame, with
  `Joystick`, `Console`, `5200Controller`, `Paddle`, … as targets
  (`inputdefs.h:190-208`). Build hardware-controller bindings as input maps
  (`map->AddMapping(physicalCode, controller, trigger)`, cf. `:1544`) rather
  than ad-hoc polling. Normal Atari computer keys are different: the
  `Keyboard` controller target models external keyboard-controller hardware,
  so libretro pad-to-key bindings should drive the same
  `HandleKeyboardEvent` path as a physical keyboard or virtual keyboard. This
  still permits **simultaneous joystick + keyboard** because the spare-button
  key state is updated every frame while the joystick input map remains live.
  Prefer migrating the current ad-hoc console-switch polling (`:2233`) onto a
  `Console` input map for consistency.
- **Match `atari800` conventions** (see Appendix B) unless we have a reason —
  and where we deviate (e.g. not relying on L3/R3, which many handhelds lack),
  say so in code comments and the README.
- **Every advertised capability must work.** If `.info` says a feature is on,
  the code must back it (cf. "keep representation and behaviour in sync").

---

## 6. Phased roadmap

Each item lists *approach → key files → acceptance criteria*. Phases are
ordered so each one independently makes a device class usable.

Current phase status:

| Phase | Status |
|---|---|
| Phase 0 — playable with only a controller | Implemented locally; Android ABIs build with local NDK and await CI/device proof |
| Phase 1 — feels native on a controller | Implemented locally, including descriptor sync for active and content-aware control schemes |
| Phase 2 — completeness and weak-device polish | Implemented locally except promotion out of experimental, which is gated on readiness reports |
| Phase 3 — stretch | Not in scope for this implementation |

### Phase 0 — "Playable with only a controller" (P0)

**0.1 Reset binding (A2, A7).**
- Approach: add **rebindable** pad bindings for warm and cold reset with
  handheld-safe defaults that need no stick-click. Proposed defaults: warm
  reset = **Select+Start** combo (universally reachable); cold reset = a second
  combo (e.g. Select+L) or a core option. Also expose a Reset key on the
  virtual-keyboard console page (0.2) so it's always reachable. Add a keyboard
  path: F5 → `g_sim.WarmReset()`, Shift+F5 → `g_sim.ColdReset()` (mirroring the
  Windows `System.WarmReset/ColdReset` commands at `uikeyboard.cpp:941-942`).
  Note `ATSimulator` also offers `ColdResetComputerOnly()` if a "reset computer
  but keep peripherals" option is wanted later.
- Verified API: `simulator.h:416-418` declares `ColdReset()`,
  `ColdResetComputerOnly()`, `WarmReset()`.
- Files: `libretro.cpp` (`kRetropadButtonMap`/console polling `:1388-1410`,
  `:2233`; `HandleKeyboardEvent` `:1798`); reuse `ATSimulator::WarmReset/ColdReset`.
- Accept: with no keyboard *and no stick-click*, a documented combo performs
  warm reset; cold reset reachable; both verified in a smoke test. **Care:** in
  5200 mode `L` already maps to 5200 RESET (`:1396`) — don't double-bind.

**0.2 Virtual keyboard (A1, A7, A8).**
- Approach: implement an on-screen keyboard rendered into the emulated frame
  (software overlay composited in `SubmitCurrentFrame`). Default toggle =
  **L3** (matches `atari800`) **plus a rebindable button-combo fallback**
  (e.g. Select+R2) so stickless handhelds can still open it. Navigate with
  D-pad, select with B; provide Shift/Ctrl modifier keys and a second page for
  console keys (Start/Select/Option/Reset), cursor/F-keys, and — in 5200 mode —
  the **5200 keypad** (`0`–`9`, `*`, `#`, Start/Pause/Reset) which has no other
  pad path (A8). Each key press routes through the existing
  `HandleKeyboardEvent` / `PushKeyboardCharacter` /
  `HandleKeyboardSpecialScanCode` path (`libretro.cpp:1772-1896`) so emulation
  sees identical events.
- Files: new `libretro_vkbd.cpp/.h`; hooks in `retro_run`/`SubmitCurrentFrame`
  (`:2785`, `:2806`) and input polling (`UpdateInput` `:2166`).
- Accept: on a gamepad-only, stickless device you can open the keyboard, type a
  BASIC line, and trigger Start/Select/Option/Reset (and the 5200 keypad in
  5200 mode). While the VKBD is open, pad-as-joystick input is suspended
  (every-acquired-state-has-a-release: release any held joystick/console bits
  when the VKBD opens and on close, so directions/fire don't stick). Note this
  modal behaviour is *time-shared*; for games needing the stick and keys at the
  **same time**, see 1.1 (concurrent button→key binding).

**0.3 Android `armeabi-v7a` build (E1).**
- Approach: convert the single Android job into a matrix over
  `[arm64-v8a, armeabi-v7a]`. The NDK toolchain is already wired
  (`ANDROID_TOOLCHAIN`, `ANDROID_PLATFORM=android-23` already supports 32-bit);
  only `ANDROID_ABI` varies. Name artifacts distinctly. Confirm the C++23
  `if consteval` usage in shared headers compiles under NDK clang for 32-bit
  (it does for arm64; verify for v7a).
- Files: `.github/workflows/libretro-core.yml` (Android job), optional
  `CMakePresets.json` preset `android-libretro-armv7a`.
- Accept: CI produces `altirra_libretro.so` for `armeabi-v7a`, verifier
  confirms `ELF32`/`ARM`, and it loads in an Android TV box / 32-bit
  RetroArch.
- Note for user: **most modern Android TV boxes are arm64** — confirm the
  target box's ABI first; it may already run the existing arm64 build.

**0.4 Disk save-back (B3).**
- Approach: mount with a virtual-read-write mode instead of `_RO`, and persist
  changes through libretro's save directory. The real enum (verified in
  `media.h:33-36`) is:
  `kATMediaWriteMode_RO` (current) · `kATMediaWriteMode_VRWSafe`
  (= `AllowWrite`; in-memory writes, **original file untouched**, no on-disk
  format) · `kATMediaWriteMode_VRW` (= `AllowWrite|AllowFormat`) ·
  `kATMediaWriteMode_RW` (= `AllowWrite|AutoFlush|AllowFormat`, writes through
  to the user's file).
  Implemented default: **`kATMediaWriteMode_VRWSafe`** (never mutates the
  source image), persisting modified content into the libretro save dir on
  unload/close. `altirra_disk_write_mode = original_rw` opts into true
  `kATMediaWriteMode_RW`.
- Files: `libretro.cpp:503`, `:2739`; save-dir helpers (`libretro_dirs.cpp`);
  `retro_get_memory_data/size` (`:2857`) if using SAVE_RAM.
- Accept: a game that writes a high-score to disk retains it across a
  reload; the user's original image is not corrupted by default;
  `libretro_saves` reflects reality in `.info`.

**0.5 Achievements memory exposure (B1).**
- Approach: expose system RAM via `retro_get_memory_data/size` with
  `RETRO_MEMORY_SYSTEM_RAM`, and/or publish `SET_MEMORY_MAPS` descriptors
  covering Atari RAM. Keep `SET_SUPPORT_ACHIEVEMENTS` only once RAM is real.
- Files: `:2282`, `:2857-2862`; pull the RAM base/size from the memory manager.
- Accept: RetroAchievements can read main RAM; `memory_descriptors = "true"`
  in `.info` if descriptors are used.

### Phase 1 — "Feels native on a controller" (P1)

- **1.1 Concurrent button→key / button→console binding (A6) — the dual-control
  feature.** This is the headline of Phase 1. Let any pad button (and the spare
  ones — Y, X, L2, R2, L3, R3) be assigned to an **Atari keyboard key** or
  **console key** that fires *while the joystick stays live*. Implementation:
  alongside the joystick input map, build a second `Keyboard`/`Console` input
  map on the same physical device and add per-button mappings
  (`map->AddMapping(physicalCode, keyboardController, keyTrigger)`, cf.
  `:1532-1556`); the input manager already resolves both each frame, so stick +
  keys work together. Expose the assignments as core options (e.g.
  `altirra_pad_y_key = space|return|esc|...`) and/or ship them via presets
  (1.2). This makes **Star Raiders, flight sims, and adventures playable on a
  pad** without opening the modal VKBD. Files: `InitDefaultInputMaps` /
  `AddLibretro*Map` (`:1532`+), option specs (`:776`+).
  - Accept: with the stick assigned to the joystick, pressing an assigned face
    button injects the mapped key (verified by a POKEY key event) **in the same
    frame** that a held direction still moves the joystick.
- **1.2 Control-scheme & configuration presets (C-series, A-series).** Ship a
  small set of selectable presets so users don't hand-build maps — see
  §6a and Appendix D. At minimum: *Joystick only*, *Joystick + common keys*,
  *Flight/Space sim* (Star-Raiders-style key set), *Keyboard-heavy / adventure*,
  *5200*. Delivered as (a) a single `altirra_control_scheme` core option that
  installs a default button→key map, and (b) bundled RetroArch remap/opt files
  for power users. Pairs with content-aware defaults (1.4).
- **1.3 Convenience pad mappings + analog stick (A4, A5):** default Y→Space,
  X→Return (match `atari800`); keep B=fire1, A=fire2; left analog stick drives
  the digital joystick with a deadzone (right stick reserved for paddle/mouse).
  `:1388`, `:2223`.
- **1.4 Content-aware machine default (C1):** on load, detect 5200 carts
  (`.a52`, 5200 `.bin`/header) and auto-select 5200 hardware + 5200 controller
  (and the *5200* control preset) unless the user has explicitly overridden the
  option; same idea for cartridge vs disk profiles. `MapHardwareMode` `:1035`,
  `retro_load_game` `:2716`.
- **1.5 Correct aspect ratio (D1):** add an `altirra_aspect` core option
  (Pixel Perfect / 4:3 / NTSC PAR / PAL PAR), feed `geometry.aspect_ratio`
  per video standard. `:2344`.
- **1.6 Drop `needs_kbd_mouse_focus` (A3)** once the VKBD + button-key binding
  exist; update `.info`.
- **1.7 Cheats (B2):** implement `retro_cheat_set/reset` against Altirra's
  memory write hooks (RAM POKEs; optionally frozen writes each frame).
  `:2841`. Flip `cheats = "true"`.
- **1.8 Controls documentation + descriptor sync (F2):** README "Controls"
  table + ensure input descriptors name every used button incl. the new ones.

### 6a. Configuration model & presets

The core should make the common cases one-click and the rare cases possible,
on every device:

- **Layered config (least-surprise order):** core-option defaults →
  `altirra_control_scheme` preset → content-aware override (1.4) →
  RetroArch per-core / per-game option & remap overrides (always wins). Never
  fight the frontend's per-game overrides.
- **Control-scheme presets** (one core option) install a sensible button→key
  map for a genre; see Appendix D. Users pick *Flight/Space sim* for Star
  Raiders, *Adventure* for keyboard-driven games, etc.
- **Hardware presets are already mostly covered** by existing options (model,
  memory, video standard, CPU, VBXE, …). Add a **performance tier** for weak
  devices (2.1) and consider a few convenience bundles (e.g. "800XL + BASIC",
  "stock 130XE").
- **Per-content profiles:** lean on RetroArch's per-game option/remap files and
  the `.info` database mapping (8-bit vs 5200) rather than inventing a parallel
  profile store. Document the workflow in the README.
- **Discoverability:** every preset and assignable-key option must use core
  options v2 categories so they're browsable in the 10-ft UI, and input
  descriptors must reflect the active scheme.

### Phase 2 — Completeness & weak-device polish (P2)

- **2.1 Performance tier / defaults (C2):** document a "performance" config
  (artifacting=None or Auto-low, lighter audio filters) and consider an
  `audio_buffer_status` dynamic-frameskip hook (`SET_AUDIO_BUFFER_STATUS_CALLBACK`)
  for low-end ARM.
- **2.2 Keyboard console-key remap + port handling (A9, A8):** optional core
  options to rebind the F2/F3/F4 keyboard console keys; port-1 console-key
  path; confirm 5200 keypad coverage from the VKBD.
- **2.3 Subsystems (B4):** `retro_load_game_special` for cart+disk / profile
  loads.
- **2.4 VBXE/full-overscan geometry audit (D2).**
- **2.5 Graduate `is_experimental` (E2)** and refresh all `.info` flags (E4).

### Phase 3 — Stretch

- iOS/tvOS packaging (E3); rumble (`SET_RUMBLE`); LED interface; touch
  overlays tuned for phones; per-platform default overlays (`.cfg`/`.lay`).

---

## 7. Per-platform build & packaging matrix (target)

| Platform | ABI(s) today | ABI(s) target | Action |
|---|---|---|---|
| Linux | x86_64, aarch64, armv7hf (glibc) | same | — |
| Windows | x86_64, arm64 | same | — |
| macOS | arm64, x86_64 | same (universal optional) | P3 |
| **Android** | arm64-v8a | **arm64-v8a + armeabi-v7a** | **0.3** |
| iOS/tvOS | none | optional | P3 |

`armv7hf` (Linux hardfloat ELF) and Android `armeabi-v7a` (Bionic) are
**different ABIs and not interchangeable** — see audit in §4/E1.

---

## 8. Testing & CI

- Extend the libretro smoke tests (`tests/`, run via `./build.sh --libretro
  --libretro-test`) to cover the local libretro contract. Current smoke
  coverage checks option registration/defaults, descriptor refresh, VKBD overlay
  rendering and key injection visible in Atari OS RAM, concurrent joystick+key
  polling with Atari OS key-state visibility, XEX+SIO disk save-back sidecar
  write/reload round-trip with original-image preservation, `.a52` content-aware 5200
  loading, exported `retro_reset`, F5/Shift+F5 and Select+Start/Select+L reset
  state-change semantics, stale option-value fallback, cheats poking RAM,
  achievements RAM pointer/size, memory maps, failed disk/subsystem-load
  cleanup, disk control API, geometry bounds, save states, metadata, artifact
  verification, and packaging. It also rejects corrupted serialized states
  without damaging the active session.
- Keep the Apple-Clang-15 / GCC-12 CI floor green; run the
  `docs/merging-with-altirra-mainline.md` audit before any upstream merge that
  touches shared keymap/input headers.
- Add the `armeabi-v7a` artifact to the build matrix and verify it with exact
  NDK `llvm-readelf` ELF class/machine checks (`ELF32`/`ARM` for v7a,
  `ELF64`/`AArch64` for arm64).

---

## 9. Documentation & metadata to update (track with each feature)

- `altirra_libretro.info`: `cheats`, `memory_descriptors`, `libretro_saves`,
  `needs_kbd_mouse_focus`, `is_experimental` — flip each as its feature lands.
- `README.md`: add a **Controls** section (pad map, VKBD usage, console keys,
  Reset) and an Android ABI note.
- This file: check off items as completed.

---

## 10. Decisions made

1. **Reset binding choice:** warm reset defaults to Select+Start; cold reset
   defaults to Select+L on Atari 8-bit systems; both are rebindable and also
   available from the VKBD console page. F5 and Shift+F5 mirror Windows
   warm/cold reset commands.
2. **VKBD toggle binding:** default is R, L3, or Select+R2. This keeps the
   `atari800` L3 convention while preserving stickless handheld access.
3. **Virtual keyboard v1 scope:** implemented as alpha, console/function, and
   5200 keypad pages with Shift/Ctrl and real keyboard/5200 controller routing.
4. **Button-to-key assignment surface:** implemented as both
   `altirra_control_scheme` presets and per-button overrides for
   Y/X/L2/R2/L3/R3.
5. **Default control scheme:** `Auto` is the default; it resolves to
   `Joystick + common keys` for Atari 8-bit systems and to the 5200 preset
   when the active system is 5200. 5200-specific keypad access is provided by
   the VKBD 5200 page while `altirra_input_port1 = auto` selects the 5200
   controller in 5200 mode.
6. **Disk write policy default:** disk images use `kATMediaWriteMode_VRWSafe`
   and persist changed images as save-directory sidecars, leaving source images
   untouched by default. `altirra_disk_write_mode = original_rw` explicitly
   opts into write-through `kATMediaWriteMode_RW`.
7. **Android targets:** CI is configured for both `arm64-v8a` and
   `armeabi-v7a`, with exact ELF class/machine verification for each ABI; local
   NDK 27.2 builds verify `ELF64`/`AArch64` and `ELF32`/`ARM` artifacts.

---

## Appendix A — current vs proposed default pad mapping

All "proposed" bindings are **rebindable** (core options + RetroArch remap) and
chosen to be reachable on a minimal handheld (D-pad, A/B/X/Y, L/R, Start,
Select — **no stick-click required**). Spare buttons default to user-assignable
keys via the 1.1 button→key system.

| Button | Current | Proposed default | Notes |
|---|---|---|---|
| D-pad | Joystick dir | Joystick dir | |
| Left analog | (paddle only) | Joystick dir (deadzone) | **new** (1.3) |
| B | Fire 1 | Fire 1 | |
| A | Fire 2 | Fire 2 | |
| Y | — | Space | assignable key (1.1) |
| X | — | Return | assignable key (1.1) |
| L | OPTION | OPTION | (5200: 5200 RESET, `:1396`) |
| R | — | Virtual keyboard toggle | single button, reachable everywhere |
| Start | START | START | |
| Select | SELECT | SELECT | |
| L2 / R2 | — | (unbound, assignable) | assignable keys (1.1) |
| R3 | — | (unbound, assignable) | assignable key (1.1) |
| L3 *(if present)* | — | alt VKBD toggle | matches `atari800`; the R button + combo are the fallbacks |
| Select+Start | — | Warm Reset | combo (0.1) |
| Select+L | — | Cold Reset | combo (0.1) |

Rationale for deviating from `atari800` (which puts VKBD on L3): many retro
handhelds have no stick-click, so the primary VKBD/Reset bindings live on
always-present buttons/combos, with L3 offered as an optional alias.

## Appendix B — `atari800` core conventions (reference)

Standard `atari800` 8-bit mapping users already know:
D-pad=joystick · B=Fire1 · A=Fire2 · Y=Space/Fire3 · X=Return · L=Option ·
R=core menu · L2=Esc · R2=Help · **L3=virtual keyboard** · Select=Select ·
Start=Start. Its virtual keyboard has Atari font, Shift/Ctrl, and a second
page with console buttons + cursor + F1–F4. Aligning with this minimizes
user friction when switching cores.

## Appendix C — key source locations (for implementers)

- Pad/console map: `libretro.cpp:1388-1410`; console polling `:2233-2247`.
- Keyboard routing: `HandleKeyboardEvent` `:1798`; special scancodes
  `:1772`; char push `:1784`; keymap `uikeyboard.cpp:497-523`,
  `ATUIGetScanCodeForVirtualKey` `:631`; scancode enum `uikeyboard.h:78-83`;
  input codes = Win32 VK codes by design `inputdefs.h:26+`.
- Load/reset: `retro_load_game` `:2716`; `retro_reset` `:2705`;
  `MapHardwareMode` `:1035`; disk RO mount `:503`, `:2739`.
- Display: `MakeGeometry` `:2344`; `FillAvInfo` `:2378`; `ReportGeometry` `:2396`.
- Former stubs now implemented: cheats, memory exposure, and subsystem loading
  live in `libretro.cpp`.
- Options: categories `:650`; specs `:776+`; `.info` metadata file.
- CI: `.github/workflows/libretro-core.yml` (Android job); presets
  `CMakePresets.json`.
- Input engine (dual control): controller-type targets
  `inputdefs.h:190-208` (incl. `Console` `:194`, `5200Controller` `:199`);
  input-map construction `AddLibretroPaddleMap`/`AddMapping` `:1532-1556`;
  computer keyboard event routing uses `HandleKeyboardEvent`, not the external
  keyboard-controller target; input codes = Win32 VK codes by design
  `inputdefs.h:26+`.
- Reset/media APIs (verified): `simulator.h:416-418`
  (`ColdReset`/`ColdResetComputerOnly`/`WarmReset`); write modes
  `media.h:33-36` (`_RO`/`_VRWSafe`/`_VRW`/`_RW`).

## Appendix D — example control-scheme presets

Concrete defaults for the `altirra_control_scheme` option (1.2). Spare buttons
(Y, X, L2, R2, L3, R3) send Atari keys *concurrently* with the joystick (1.1);
all are rebindable. Console keys and Reset are always also on the VKBD.

| Scheme | Stick/D-pad | B / A | Y | X | L | R | L2 | R2 |
|---|---|---|---|---|---|---|---|---|
| **Joystick only** | Joystick | Fire1 / Fire2 | — | — | Option | VKBD | — | — |
| **Auto** *(default)* | Content-aware | Fire1 / Fire2 | Common keys on 8-bit; 5200 keypad via VKBD on 5200 | Common keys on 8-bit; 5200 keypad via VKBD on 5200 | Option / 5200 Reset | VKBD | Esc on 8-bit | Return or VKBD combo |
| **Joystick + common keys** | Joystick | Fire1 / Fire2 | Space | Return | Option | VKBD | Esc | Return |
| **Flight / Space sim** (e.g. Star Raiders) | Joystick | Fire1 / Fire2 | F (fore view) | A (aft view) | G (galactic chart) | VKBD | M (map/long-range) | S (shields) |
| **Keyboard-heavy / adventure** | Joystick | Fire1 / Return | Space | Esc | Y (yes) | VKBD | N (no) | Return |
| **5200** | Analog→5200 stick | Bottom / Top fire | (keypad via VKBD) | (keypad via VKBD) | 5200 Reset | VKBD | — | VKBD combo |

Notes:
- The *Flight/Space sim* key set is illustrative — the value is the
  *mechanism* (assignable, simultaneous keys) more than the exact letters;
  Star Raiders' real keys are F/A/G/M/S/C/L/T etc., which is why per-button
  overrides (open decision #4) matter.
- In *5200*, the keypad (`0`–`9`, `*`, `#`) lives on the VKBD's 5200 page (A8);
  START/PAUSE/RESET are on Start/Select/L through the 5200 controller map.
- Presets are implemented as core options. RetroArch per-core/per-game remaps
  remain available through the frontend; no bundled `.rmp` files are required
  for the implemented behavior.
