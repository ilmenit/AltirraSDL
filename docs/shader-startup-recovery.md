# Shader compilation and renderer recovery

## Implemented correction for issue #112

Keep the bicubic kernel as bounded HLSL loops. The HDR bilinear sampling
calculation is unchanged; bicubic retains all sixteen taps, their order,
and the A=-0.75 weights. Preserve decoding before interpolation, HDR output scaling, signed
RGB, PAL, bloom, masks, gamma, and sharp filtering. Regenerate SPIR-V, DXIL,
and MSL together. Merely disabling HDR in settings cannot avoid compiling
branches controlled by uniforms.

The regression test can dump 8x8 float images using an asymmetric color
texture across pass/filter/sampler/HDR combinations. Compare against the
pre-change fragment using:

```sh
python3 tests/upstream/compare_shader_cold_cache.py \
    build/tests/tests/upstream/altirra-display-sampling-test reference.spv
```

Use the actual path to the CMake-built test. Set `VK_DRIVER_FILES` to select
a Mesa hardware or lavapipe ICD. The script disables Mesa's disk shader cache
in both child processes; it does not delete the user's cache. Timing includes
startup, pipeline compilation, execution and readback. It is diagnostic,
not an absolute speed gate: a fixed timeout or performance threshold cannot
establish correctness across Raspberry Pi, software Vulkan, and desktop GPUs.
The normal test additionally retains known-value assertions independent of
the historical shader. Native Metal and D3D testing remains necessary.

## Verification of the correction

Using the same 451-case image comparison, with Mesa disk caches disabled:

| Driver | Reference | Corrected | Maximum channel difference |
| --- | ---: | ---: | ---: |
| Intel Mesa 26.2.6 | 1.920 s | 0.767 s | 4.18e-7 |
| Debian 13 lavapipe, Mesa 25.0.7 / LLVM 19 | 9.491 s | 3.123 s | 1.05e-7 |

These are complete probe durations on the development machine, not expected
startup times on a Raspberry Pi. Native Pi, ARM mobile, Metal and D3D execution
are not covered by these measurements. The generated representations compile
for all three native shader formats (DXC 1.9.2609.5, SPIRV-Cross MSL 2.1).
A 120-frame Intel SDL renderer probe also kept steady rendering time essentially
unchanged for bilinear and bicubic (about 61 ms after first presentation);
this small probe is not a substitute for native-device game benchmarks. The SPIR-V shrinks from 283,340 to
131,960 bytes. An additional experiment looping the HDR bilinear samples was
rejected because it worsened Intel performance; that sampling code is unchanged
in the final correction.

## Implemented recovery

The SDL3 frontend measures renderer creation, first UI presentation and first
emulated-texture presentation. Five seconds triggers a one-time explanation,
not an automatic downgrade. Three consecutive SDL presentation/swap failures
trigger recovery help. Successful frames reset the consecutive-error count.

Desktop and Gaming Mode use the existing confirmation queue. Actions are
stacked, sized to the viewport, and larger in Gaming Mode. Keyboard, touch,
and gamepad navigation use the existing modal/input ownership paths. Keeping
the renderer is the initial selection; Escape and closing the prompt preserve
it. Other actions save OpenGL or compatibility graphics for the next launch.
The current session and renderer stay alive. The prompt explains lost HDR or
screen-effect capabilities without modifying those settings. Explicit CLI and
environment overrides still take precedence over the saved selection.

On desktop, machine-global `Graphics Recovery` records arm only around
renderer creation (after window creation) and first presentation. Window-manager
waits, firmware loading, setup dialogs,
audio initialization and normal emulation are outside the pending interval.
Two interrupted graphics attempts, or confirmed repeated presentation errors,
offer a native SDL recovery dialog before initializing the suspect renderer
on the next launch in Desktop Mode. Gaming Mode instead bootstraps an
alternative renderer for that session and offers the normal controller/touch
prompt, without overwriting the saved preference. Its Keep action explicitly
says "Try preferred renderer next launch". Explicit overrides bypass this
bootstrap as well as the native prompt. A pending marker is described as an interruption, not
proof of a driver fault. A successful first emulated frame clears the record.
Choosing Keep also acknowledges/reset previous interruptions. Shader-header
SHA-256, SDL revision, video backend and requested renderer scope the record;
GPU driver version alone is not currently an invalidation key.

The existing capability fallback still tries SDL_GPU, then OpenGL, then
SDL_Renderer on creation failure. Recovery reports a fallback without silently
saving it as the user's preference. An alternative can fail too; compatibility
SDL_Renderer may use the same underlying API as SDL_GPU.

### Mobile and browser handling

Android/iOS do not infer failures from interrupted process markers: normal OS
termination must not count as a graphics crash. Explicit presentation failures
can still be recorded. Backgrounding clears pending observation/error streaks
and disables startup-duration observations on resume. The shared recovery
prompt uses large, stacked actions in Gaming Mode. No watchdog accesses the
simulator or destroys graphics resources on another thread.

WebAssembly records no native recovery preferences and offers no native
renderer choices. Its slow/error prompt offers Keep or Reload. WebGL context
loss stops further graphics calls and presents an accessible DOM reload
banner, independent of the unusable ImGui canvas. Reload keeps persisted files
and settings, but the banner warns about unsaved progress. No automatic reload
or destructive storage reset is performed.

### Boundaries

A main thread blocked inside a graphics driver cannot display an ImGui prompt
or cancel that driver call. This implementation offers help after a slow call
returns, and before graphics initialization on a subsequent launch after
repeated interruptions. It does not claim to interrupt a permanent hang.
During such a hang the user must close/terminate and reopen the app; live
cross-process supervision would require a separate launcher and platform
process lifecycle work. Rendering APIs remain on their owning thread.

### Automated checks

`upstream-display-recovery` tests threshold boundaries, repeated interruptions,
success/error streaks, saturation and suppression of repeated notices.
`tests/ui/test_graphics_recovery.py` exercises actual launch delays, interrupted
startup records, Desktop/Gaming actions, saved preferences, renderer switching,
explicit overrides and repeated presentation error recovery. UI-mode-only test
hooks inject faults; they are inactive in ordinary launches. The shader's
known-value and cold Mesa checks remain independent of the recovery tests.

## Sustained rendering performance and CRT advice

This is a separate diagnosis from cold pipeline compilation. On the development
machine with software Vulkan (Mesa lavapipe), a private fresh profile at
1280x720, no vsync, and warmed pipelines produced these actual-app results:

| Screen effects | Time for 90 rendered frames | Observed frames/s |
| --- | ---: | ---: |
| Basic | 5.411 s | 16.63 |
| None | 1.453 s | 61.95 |
| Basic again | 4.811 s | 18.71 |

Every interval advanced the same 2,688,120 emulated cycles. These short probes
show a rendering bottleneck on software Vulkan, not a promise of a particular
frame rate on Raspberry Pi hardware. Startup compilation was warmed before
the measurements. The emulator's normal pacing/catch-up behavior can affect
short-window rates around 60 fps.

The default artifacting preset enables bloom, distortion and vignette;
`ATOptions` enables accelerated screen effects, and SDL3's initial UI setting
is Basic. The default filter is Any Suitable, not Bicubic. Therefore reducing
CRT work can help with default settings on a weak/software renderer without
requiring a renderer change. Defaults are preserved to retain Windows parity.

The code gives a concrete order for performance suggestions:

1. Try Screen Effects -> (None), then restore Basic if it makes no useful
   difference. This bypasses optional effects without erasing their tuned
   parameters. The existing per-profile View callback persists the toggle.
2. For finer control, disable Bloom first. Its pipeline has 15 offscreen
   passes before final presentation: linearization, six downsample levels,
   five upsample levels, base filtering, addition and soft clamp. Several
   targets cover the entire physical output, so HiDPI/fullscreen sizes matter.
3. If Bicubic is selected, try Bilinear/Sharp Bilinear. Bicubic uses a separate
   destination-sized prepass with sixteen taps; changing the CRT toggle alone
   does not remove that filter prepass.
4. Try a smaller window/output size before switching graphics APIs. Screen
   masks also use intermediate targets. Scanlines/vignette are relatively
   small amounts of math in the final shader and should not automatically be
   blamed ahead of bloom, filters, resolution or custom multipass effects.

### Implemented automatic performance advisor

The advisor measures only rendering/submission and presentation, outside core
Advance, audio waits and frame pacing. It samples new emulated frames during
foreground gameplay at normal speed. Pauses, menus, debugger work, background
windows, slow motion, turbo and non-default speed do not contribute. There is
a 60-frame warm-up after interruptions. A suggestion requires at least 90
consecutive expensive samples spanning three seconds of observed rendering.
Large isolated stalls/compilation spikes reset the slow streak.

On native platforms it accounts for the current display refresh and actual
swap interval, with an additional frame-budget margin. Even immediate mode
allows a refresh interval for compositor waiting. Unknown metadata disables
presentation-based inference. On Wasm, browser frame intervals and SwapWindow
are not GPU timers: only sustained expensive CPU-side rendering/submission is
used. This conservative path can miss purely asynchronous GPU bottlenecks;
it does not blame CRT effects for low FPS alone.

A single stacked, keyboard/controller/touch-friendly prompt per launch offers
Not Now (default), Try Screen Effects Off, and Try Bilinear Filtering when
bicubic is selected. Unsupported effects/filter suggestions are not offered.
The trial window is draggable on Desktop, uses large buttons in Gaming Mode,
fits/recenters on viewport rotation, and defaults to Restore for Enter/gamepad
activation. Its 15-second timeout restores automatically. Menus, binding
capture, other confirmations, profile changes, backgrounding and shutdown also
cancel a trial safely.

Trials override only rendering; settings getters and serialization retain the
original values. Keep explicitly commits the chosen toggle/filter using the
existing settings/callback save lifecycle. Detailed CRT/HDR parameters are
never reset. An accepted edit marks the Gaming Mode preset Custom, matching
manual edits, so a startup preset cannot undo it. The normal mobile-edit save
mask preserves other live preset settings without reasserting fullscreen or
rewriting mounted media. Temporary profiles remain temporary and do not
change the machine-global preset. No renderer or emulation speed is switched.

Implementation cost is bounded: a few CPU timestamps and counters, cached
native display metadata once per second, no GPU queries/readbacks or forced
synchronization, and no per-frame allocations for sampling. Sampling stops
after the first offer and is disabled when no optional work can be reduced.
There is no network telemetry.

Verification includes policy tests for refresh rates/swap waits, pauses,
spikes, unknown/browser presentation timing and reset boundaries; real UI
trials, persistence and cancellation in both modes; Android ARM64 and Emscripten frontend
syntax checks; and executing the policy test as WebAssembly under Node.
A live lavapipe run triggered the advisor from real rendering timings without
performance fault injection (automation supplied foreground focus). Native
Android/iOS and real-browser GPU-only execution still need device coverage.


A second live check accepted the automatic effects-off trial on lavapipe at
1280x720: 60 rendered frames ran at approximately 60.9 fps and advanced
1,792,080 emulated cycles. The stored effects mode remained Basic while the
render override was off; Restore returned to the original effects. UI review
covered portrait and landscape trial controls, including safe default focus.
The final UI run passed 39 advisor/recovery/exit/mobile-lifecycle checks.


## X11 startup stall: sleeping outputs, not shader compilation

The November 15 investigation reproduced the original stall on the host and
captured this stack before a renderer was created:

```text
SDL_CreateWindow
  SDL_FinishWindowCreation
    SDL_ShowWindow
      X11_ShowWindow
        XIfEvent -> _XReadEvents -> xcb_wait_for_event -> poll
```

The host was KDE Wayland with Xwayland, KWin 6.8.2 and SDL 3.4.20.
`kscreen-doctor --dpms show` reported the only output off; the screensaver
D-Bus `GetActive` query also reported true. Earlier stalls likewise coincided
with the lock screen. Locking alone is not the trigger: an isolated locked
virtual desktop with its output on still created windows normally.

The mechanism was reproduced without changing the user's desktop, using a
separate `dbus-run-session`, `kwin_wayland --virtual --xwayland`, and its own
Wayland socket. Turning **only that compositor's** DPMS off made a standalone
SDL shown window block, while an SDL hidden window still created immediately.
Turning that virtual output back on released the blocked call successfully.
Stopping/resuming the isolated compositor also reproduced/released the same
wait, in both the standalone probe and AltirraSDL under GDB.

The renderer-free reproducer is `tests/upstream/sdl_window_startup.cpp`:

```sh
c++ -std=c++17 tests/upstream/sdl_window_startup.cpp \
  $(pkg-config --cflags --libs sdl3) -o /tmp/sdl-window-startup
SDL_VIDEODRIVER=x11 timeout 10s /tmp/sdl-window-startup
SDL_VIDEODRIVER=x11 timeout 10s /tmp/sdl-window-startup --hidden
```

With sleeping outputs the shown probe times out, while the hidden probe
returns. With outputs awake both return. Use a separate virtual compositor
for controlled power-state changes; do not turn off the user's screen to run
this test.

This matches the dependency sources:
[KWin 6.8.2 `X11Window::updateVisibility`](https://github.com/KDE/kwin/blob/v6.8.2/src/x11window.cpp)
hides X11 windows while DPMS is not on, and
[SDL 3.4.20 `X11_ShowWindow`](https://github.com/libsdl-org/SDL/blob/release-3.4.20/src/video/x11/SDL_x11window.c)
waits synchronously for `MapNotify` without a timeout. Switching graphics APIs
cannot fix a wait that precedes every renderer. The earlier standalone probe
also exhibited the stall with SDL 3.4.14.

The frontend correction separates window creation from renderer diagnosis:

- Log window creation start/completion/failure and duration, then renderer
  creation, so the last stage identifies this wait.
- Clear pending tracking before each window attempt, arm it after a window
  exists, and exclude window wait time from slow-renderer advice. Repeated
  termination while the desktop sleeps must not trigger a renderer rescue.
- Version the recovery fingerprint so old pending records that included
  window-system waits cannot produce misleading recovery advice. Saved renderer
  preferences, profiles and tuned effects are unchanged.

The SDL synchronous mapping wait itself remains dependency behavior: waking
an output lets startup continue. Wayland remains available through SDL's
normal selection or `SDL_VIDEODRIVER=wayland`. The application does not wake or
unlock the desktop, rewrite SDL's video-driver preference, move window APIs
onto another thread, or attempt to tear down a blocked graphics call.

For automated forced-X11 testing on a sleeping desktop, use an isolated
virtual compositor with outputs on. Do not classify a test-socket startup
timeout by itself as a shader or renderer failure. The UI regression suite
includes slow/interrupted window attempts separately from slow/interrupted
renderer attempts, in Desktop and Gaming modes. An actual six-second blocked
window-map check kept `Pending=0`, then resumed to SDL_GPU with no recovery
prompt after its isolated compositor resumed.
The same actual-app check with virtual DPMS off/on also resumed successfully
without a recovery warning. X11 and Wayland each passed all 43 UI checks,
including slow startup, interrupted launches, Desktop/Gaming actions, renderer switching,
advisor trials, exit handling and mobile lifecycle behavior. The standalone
recovery/advisor policies, cold Mesa sampling and browser recovery checks pass;
Android ARM64 and Emscripten syntax checks cover the changed startup paths.
Legacy pending-record migration was also verified in Desktop and Gaming Mode.
The test-mode resize command restores fullscreen/maximized windows and uses
SDL's finite window synchronization barrier before acknowledging a resize;
this avoids stale click coordinates during asynchronous X11 window changes.
Normal rendering has no new synchronization or polling.

