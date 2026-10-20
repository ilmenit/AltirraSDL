# AltirraSDL / Windows Altirra parity review

Review date: **2026-10-20**. Reference: **Altirra 4.50-test24** Windows
sources and the corresponding AltirraSDL working tree after integration.
This is a source-based review, not an exhaustive runtime or GUI audit.
The items below are proposed follow-up work; this review does not implement
the catch-up changes. Line references describe the reviewed source revision
and may move during later edits.

The objective is equivalent functionality while preserving useful SDL fork
features. Windows-only implementation details such as Direct3D selectors
need platform-appropriate equivalents, not identical backend controls.
The order below groups related implementation work; every listed feature
belongs in the parity scope.

## Speed control

### Arbitrary speed adjustment

The SDL Speed page exposes only 1x, 2x, 4x and 8x
([SDL controls](../src/AltirraSDL/source/ui/sysconfig/ui_system_pages_computer.cpp#L860)).
Its tooltip promises slower operation, although the selection has no
below-normal value. Moreover, an arbitrary value restored from settings
falls through to the displayed normal-speed selection.

Windows uses a slider with raw positions 1–550 and a broad normal-speed
detent, mapping to **1–500%**
([range](../src/Altirra/source/uiconfiguresystem.cpp#L2641),
[mapping and percentage label](../src/Altirra/source/uiconfiguresystem.cpp#L2694)).
SDL's existing accessor accepts arbitrary modifiers
([setter](../src/AltirraSDL/stubs/uiaccessors_stubs.cpp#L255)), and its
[pacer](../src/AltirraSDL/source/app/main_pacer.cpp#L400) consumes them.

Implement the Windows percentage range and normal-speed detent through
`ATUISetSpeedModifier`, using the existing shared
[percentage persistence](../src/Altirra/source/settings.cpp#L649).
Keep the convenient existing presets available, preserve their selected
value accurately, and retain the
[Online Play speed locks](../src/AltirraSDL/source/ui/sysconfig/ui_system_pages_computer.cpp#L841).
Verify representative values such as 1%, 25%, 75%, 100%, 125% and 500%
against elapsed emulated frames and audio behavior, including restart and
profile-switch restoration.

### Slow Motion factor

SDL already has a working **Slow Motion** checkbox
([control](../src/AltirraSDL/source/ui/sysconfig/ui_system_pages_computer.cpp#L852));
it is not a missing toggle. The separate parity gap is its fixed factor:
SDL [hardcodes 0.5](../src/AltirraSDL/source/app/main_pacer.cpp#L406), while
Windows registers the Advanced Configuration variable
[`engine.slowmo_scale`](../src/Altirra/source/main.cpp#L253) and
[uses it in timing](../src/Altirra/source/main.cpp#L531).

Register and consume the same configuration variable in the SDL timing
path. Test interaction with the baseline speed modifier and warp mode.
This variable and the arbitrary-speed slider are distinct mechanisms.

## Controls with incomplete runtime behavior

### Alternate video outputs

SDL exposes a
[Video Outputs submenu and autoswitch toggle](../src/AltirraSDL/source/ui/menus/ui_menus_view.cpp#L181),
but the
[output accessors](../src/AltirraSDL/stubs/uiaccessors_stubs.cpp#L160)
hardcode availability to false and leave output selection, previous/next,
and XEP80 operations empty. Device choices already include
[1090](../src/AltirraSDL/source/ui/sysconfig/ui_system_pages_b.cpp#L82),
[Bit 3](../src/AltirraSDL/source/ui/sysconfig/ui_system_pages_b.cpp#L109),
and [XEP80](../src/AltirraSDL/source/ui/sysconfig/ui_system_pages_b.cpp#L127).
Windows implements
[output selection and routing](../src/Altirra/source/uivideodisplaywindow.cpp#L113).

Implement device-output discovery, frame presentation, direct selection,
previous/next and autoswitch. Test insertion/removal of output devices,
switching back to the computer output, saved selection behavior, and both
the ImGui and native macOS menu paths.

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

SDL exposes and saves
[`mbPauseDuringMenu`](../src/AltirraSDL/source/ui/sysconfig/ui_system_pages_b.cpp#L920),
but no runtime consumer was found in the SDL source tree. Windows applies
it in the [modal loop](../src/Altirra/source/main.cpp#L3122).

Implement menu-open pause behavior and restore the previous running state
when the last relevant menu closes. Verify escape, selection, focus loss,
an already-paused simulator, and Online Play, where pausing one peer can
stall the other. Do not confuse this with the existing mobile pause menu.

### Reuse program instance

SDL exposes and saves
[`mbSingleInstance`](../src/AltirraSDL/source/ui/sysconfig/ui_system_pages_b.cpp#L932),
but no SDL runtime consumer was found. Windows handles
[instance reuse at startup](../src/Altirra/source/main.cpp#L3723).

Implement platform instance discovery and argument forwarding, including
opening or booting media from a second invocation. Test simultaneous
startup, stale instance endpoints, and normal multi-instance operation
when the option is disabled.

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

SDL explicitly marks HDR unavailable and
[disables all HDR controls](../src/AltirraSDL/source/ui/dialogs/ui_screenfx.cpp#L196).
Windows has
[HDR capability and luminance controls](../src/Altirra/source/uiscreeneffects.cpp#L469).

Complete capability detection, output color-space/swapchain configuration,
SDR and HDR luminance handling, and monitor/status reporting for supported
SDL backends. Keep unavailable controls disabled on unsupported displays
and backends. Validate on HDR hardware, including display changes and
window/fullscreen transitions.

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
