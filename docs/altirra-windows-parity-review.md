# AltirraSDL / Windows Altirra parity review

Review date: **2026-10-20**. Reference: **Altirra 4.50-test24** Windows
sources and the corresponding AltirraSDL working tree after integration.
This is a source-based review, not an exhaustive runtime or GUI audit.
The follow-up implementation now covers arbitrary speed, alternate video
outputs, menu pausing, desktop instance reuse and supported SDL_GPU HDR.
Remaining gaps and hardware validation limits are identified below. Line
references in the original findings may move during later edits.

The objective is equivalent functionality while preserving useful SDL fork
features. Windows-only implementation details such as Direct3D selectors
need platform-appropriate equivalents, not identical backend controls.
The order below groups related implementation work; every listed feature
belongs in the parity scope.

## Speed control

### Arbitrary speed adjustment

Implemented in Configure System → Speed: a **1–800%** slider, direct
percentage entry with Ctrl+click, and the existing 1x/2x/4x/8x presets.
The slider replaces the Slow Motion checkbox. Custom values no longer
appear as the normal preset. Speed uses upstream's whole-percentage
profile persistence and retains Online Play speed locks.

Very slow rates use cooperative scheduling so event handling and rendering
remain responsive between emulated frames. Tests verify 25% core timing,
1% command responsiveness, arbitrary values and restart persistence.
Browser scheduling now permits multiple frames per animation tick, preserves
partial-frame credit, and avoids pause/catch-up debt. Its clock regression
covers 1–800% at 30/60/120/144Hz browser refresh rates.

### Slow Motion factor

The separate checkbox was removed as requested. The legacy slow-motion
accessor remains for existing command/API compatibility; the percentage
control displays the effective speed and clears that factor on adjustment.
Windows' configurable `engine.slowmo_scale` remains a distinct backend
parity gap; this change does not introduce a second speed UI control.

## Controls with incomplete runtime behavior

### Alternate video outputs

Implemented device discovery, direct selection, previous/next cycling,
autoswitch, pass-through and invalid-signal handling through the existing
video manager. Both ImGui and native macOS menus enumerate real outputs.
Device pixels reach the main display, debugger display, capture and text
selection paths. Pixel-exact outputs retain native sizing, shrinking only
when necessary to fit.

Routing regression tests cover activation, removal, cropping, packed
pixels and blank signals. Real desktop tests verify XEP80, 1090 and Bit 3
selection, cycling and removal while menu pausing is active. Native macOS
menu behavior still needs runtime validation on macOS.

### Screen-reader support

SDL's
[accessibility checkbox](../src/AltirraSDL/source/ui/sysconfig/ui_system_pages_b.cpp#L859)
updates and saves `mbAccEnabled`. No SDL runtime consumer of that flag was
found. Windows
[applies the option](../src/Altirra/source/main.cpp#L3872),
[creates its accessibility provider](../src/Altirra/source/uidisplay.cpp#L1436),
and [publishes changed emulated-screen text](../src/Altirra/source/uidisplay.cpp#L1929).

Implement platform accessibility publication of emulated text, focus and
updates. Validate with an actual supported screen reader, in addition to
testing the stored checkbox value. A visible enabled option should be
backed by its advertised behavior.

### Pause while menus are open

Implemented the saved option for ImGui menus/context menus and the native
macOS menu tracking loop. Menu pausing gates emulation without changing
the simulator's running/paused state. Online Play suppresses local menu
pausing to avoid stalling peers; bridge frame gates retain their control.

Behavioral tests verify frozen emulation ticks while menus are open,
resume after Escape and focus loss, preservation of an already-paused simulator and
continued emulation when the option is disabled. Native macOS tracking
uses a main-thread run-loop timer and needs runtime validation on macOS.

### Reuse program instance

Implemented desktop instance election and authenticated local argument
forwarding before window creation. The saved option and explicit
`--singleinstance` / `--si`, `--nosingleinstance` / `--nosi` switches are
supported. Relative media paths resolve against the sender's working
directory. Requests are applied on the main thread and deferred during
Online Play/native menu tracking. Headless, bridge and test-mode sessions
remain independent. Elevated Windows launches also remain independent.
See [instance reuse](instance-reuse.md) for the platform details.

Process tests cover concurrent startup, stale endpoints, authentication,
partial messages and multi-instance operation. A real desktop test boots
a Unicode relative XEX in the existing process and verifies its RAM,
independent launches and forwarded debugger behavior. Foregrounding uses
SDL's window activation request; compositor focus policy still applies.

### Custom display effects

SDL's [Effect file field](../src/AltirraSDL/source/ui/sysconfig/ui_system_pages_b.cpp#L1100)
calls a [setter that only stores an in-memory string](../src/AltirraSDL/stubs/uiaccessors_stubs.cpp#L61).
The reviewed path does not load an effect into a display backend.
Windows provides
[load, browse, reload and clear operations](../src/Altirra/source/uiconfiguresystem.cpp#L3020);
its existing custom-effect support is explicitly D3D9-specific.

Implement a portable effect loading and validation contract, with useful
errors, reload, clear and appropriate persistence. Establish compatibility
or a documented conversion strategy for Windows effect files. Copying
Direct3D-specific controls alone would not deliver the functionality.

## Display capabilities

### HDR output

Implemented HDR for SDL_GPU using a verified extended-linear float
swapchain, floating-point effect intermediates, calibrated Atari and UI
white levels, upstream profile settings and live capability reporting.
Unsupported backends/displays keep the SDR path and explain the limitation.
A session launched in SDR requires a restart after enabling OS HDR or
moving to an HDR display. A linear session adapts when HDR is disabled or
the window moves to SDR.

Math regressions verify SDR preservation and luminance handling. Physical
HDR presentation, Metal/D3D behavior and monitor transitions still require
HDR-capable hardware. See [HDR output and validation](hdr-output.md).

## Fork features and behavior to preserve

- **Guided joystick setup:** the explicitly requested fork extension
  creates or reuses ordinary editable maps
  ([declaration](../src/AltirraSDL/source/ui/input_config/ui_guided_input.h#L5),
  [implementation](../src/AltirraSDL/source/ui/input_config/ui_guided_input.cpp#L215)).
- **Global command/settings search:** preserve command lookup, navigation
  to settings, highlighting and search hooks
  ([search implementation](../src/AltirraSDL/source/ui/core/ui_search.cpp#L117),
  [settings-page integration](../src/AltirraSDL/source/ui/sysconfig/ui_system.cpp#L238)).
- **Game library:** retain cover art and metadata, source/archive handling,
  variants, filtering and play history
  ([cover/metadata rows](../src/AltirraSDL/source/ui/dialogs/ui_game_library.cpp#L248),
  [filter](../src/AltirraSDL/source/ui/dialogs/ui_game_library.cpp#L460),
  [history columns](../src/AltirraSDL/source/ui/dialogs/ui_game_library.cpp#L523)).
- **Online Play safeguards:** preserve session speed restrictions in both
  [controls](../src/AltirraSDL/source/ui/sysconfig/ui_system_pages_computer.cpp#L841)
  and [accessors](../src/AltirraSDL/stubs/uiaccessors_stubs.cpp#L257),
  alongside fork lobby and netplay functionality.
- **Platform pacing adaptations:** Gaming Mode deliberately selects
  integral frame rates without overwriting the user's saved desktop mode
  ([rationale and implementation](../src/AltirraSDL/source/app/main_pacer.cpp#L368)).
  Preserve the
  [adaptive pacing and swap-interval decisions](../src/AltirraSDL/source/app/main_pacer.cpp#L410)
  unless scrolling and audio measurements justify a change.
- **Window placement:** configuration dialogs reopen centered without
  stale saved positions
  ([Configure System](../src/AltirraSDL/source/ui/sysconfig/ui_system.cpp#L196));
  debugger and tool panes retain their persistent layouts.
- **SDL_GPU/OpenGL screen effects:** preserve supported effects and
  capability-aware fallback behavior
  ([backend capability check](../src/AltirraSDL/source/ui/dialogs/ui_screenfx.cpp#L301)).

## Repeatable parity audit

1. Build a versioned inventory from Windows `menu_default.txt`, command
   registration/handlers, resource dialog definitions and `ui*.cpp`.
   Record menu/dialog path, command identifier, option/range/default,
   enabled conditions, persistence category and observable effect.
2. Map each item to its SDL control and implementation. Classify it as
   equivalent, intentional fork extension/adaptation, missing, or present
   with incomplete behavior. Inspect each enabled control through its
   setter to its runtime consumer; serialization alone is insufficient.
   Keep a separate rationale for platform-specific substitutions.
3. Use SDL `--test-mode` widget inventories and `open_dialog`, `list_items`
   and `click` commands to compare labels, placement, ranges and enabled
   states. Add behavioral assertions through `query_state` or targeted
   probes; screenshots alone cannot establish functionality. Consult
   [the test-mode command list](../src/AltirraSDL/source/ui/testmode/ui_testmode.cpp#L2841)
   for current commands.
4. For stateful controls, test changes immediately, after restart and
   after profile switches. Exercise paused/running states, focus loss,
   fullscreen, keyboard/controller input and Online Play restrictions.
   Where a property is not observable, add a narrowly scoped test-mode
   query rather than accepting a checkbox-state-only test.
5. Compare Windows and SDL screenshots for the same hardware/profile/media
   state. Use native accessibility tools, multiple-display/HDR hardware
   and audio/frame-timing measurements where those are the behavior under
   review. Keep platform adaptation and fork usability improvements in
   the acceptance criteria.
6. On each upstream import, regenerate the inventory delta and recheck
   affected mappings. Do not turn a narrow source audit into a claim of
   complete Windows parity without runtime evidence.
