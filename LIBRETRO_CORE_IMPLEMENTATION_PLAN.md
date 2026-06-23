# Altirra Libretro Core Implementation Plan

This document describes how to add a RetroArch/libretro core build to this
repository without weakening the existing Windows, SDL3, BridgeServer, Android,
or WASM builds.

The intended output is a new headless shared-library target:

| Platform | Artifact |
| --- | --- |
| Linux | `altirra_libretro.so` |
| Windows | `altirra_libretro.dll` |
| macOS | `altirra_libretro.dylib` |

The target is not a repackaged `AltirraSDL` executable. It is a separate
libretro frontend adapter that embeds Altirra's emulator core and exposes the
standard `retro_*` C ABI required by RetroArch.

> **Status of this document.** Every API signature, symbol name, enum value,
> and file path below has been verified against the current tree (branch
> `libretro-core`) and against the upstream `libretro.h`. Where the first draft
> of this plan guessed an API and the guess was wrong, the corrected form is
> used and the original mistake is called out in **Corrections** notes so
> reviewers can see what changed. Items that still require empirical
> measurement (frame dimensions, exact fps) are marked **MUST VERIFY**.

---

## Verified Codebase Facts (read this first)

These are the exact APIs the adapter will call. They were confirmed by reading
the source; do not re-guess them.

| Concern | Verified API | Location |
| --- | --- | --- |
| Frame stepping | `enum AdvanceResult { kAdvanceResult_Stopped, kAdvanceResult_Running, kAdvanceResult_WaitingForFrame }; AdvanceResult ATSimulator::Advance(bool dropFrame);` | `src/Altirra/h/simulator.h:457-462` |
| Content load | `bool ATSimulator::Load(const wchar_t *path, ATMediaWriteMode writeMode, ATImageLoadContext *loadCtx);` (plus stream/image/`ATMediaLoadContext&` overloads) | `simulator.h:441-444`, ctx `src/h/at/atio/image.h:60`, mode `src/h/at/atcore/media.h:27,33` (`kATMediaWriteMode_RO = 0`) |
| Lifecycle | `Init()`, `LoadROMs()`, `ColdReset()`, `Resume()`, `Pause()`, `SetRandomSeed(uint32)` | `simulator.h:129,138,401,416,419,422` |
| Frame capture | `bool ATGTIAEmulator::GetLastFrameBuffer(VDPixmapBuffer&, VDPixmap&) const;` returns false pre-boot | `src/Altirra/h/gtia.h:504` |
| Null display install | `GTIA().SetVideoOutput(IVDVideoDisplay*)`, `GTIA().SetFrameSkip(true)` | `gtia.h:416,425` |
| Frame-posted signal | null display sets `mFramePosted` in `PostBuffer()`; poll/clear via `ATBridgeNullVideoDisplayConsumeFramePosted(disp)` | `src/AltirraBridgeServer/ui_stubs.cpp:288,356` |
| Null display factory | `IVDVideoDisplay *ATBridgeCreateNullVideoDisplay();` | `ui_stubs.cpp:352` |
| Frame → XRGB8888 | `dst.init(w,h,nsVDPixmap::kPixFormat_XRGB8888); VDPixmapBlt(dst, src);` (XRGB8888 = `0x00RRGGBB`) | `src/AltirraSDL/source/bridge/bridge_commands_render.cpp:188-197` |
| Save state (mem) | `ATBridge::SaveStateToBuffer(ATSimulator&, std::vector<uint8_t>&, ATBridge::StateMetadata&);` writes a `.altstate2` zip | `src/AltirraSDL/source/bridge/bridge_savestate.cpp:163`, header `bridge_savestate.h:84` |
| Load state (mem) | `bool ATBridge::LoadStateFromBuffer(ATSimulator&, const uint8_t*, size_t);` | `bridge_savestate.cpp:203`, `bridge_savestate.h:95` |
| Audio factory | `IATAudioOutput *ATCreateAudioOutput();` | `src/h/at/ataudio/audiooutput.h:128` |
| Audio write | `void WriteAudio(const float *left, const float *right, uint32 count, bool pushAudio, bool pushStereoAsAudio, uint64 timestamp);` — **float, non-interleaved, separate L/R** | `audiooutput.h:108-111` |
| Audio mixer | `IATAudioMixer& IATAudioOutput::AsMixer();` (sync/async source registration lives here) | `audiooutput.h:83`, `src/h/at/atcore/audiomixer.h:89-110` |
| Input injection | `ATInputManager::OnButtonDown(int unit, int id)`, `OnButtonUp(int unit, int id)`, `OnAxisInput(int unit, int axis, sint32 value, sint32 deadified)` | `src/Altirra/h/inputmanager.h:270-273` |
| Joystick input codes | `kATInputCode_JoyStick1Left=0x2100, Right=0x2101, Up=0x2102, Down=0x2103, JoyButton0=0x2800` | `src/Altirra/h/inputdefs.h:155-171` |
| Console switches | `GTIA().SetConsoleSwitch(0x01=START / 0x02=SELECT / 0x04=OPTION, bool state)` | `src/Altirra/source/main.cpp:743-751` |
| 5200 keypad | `kATInputTrigger_5200_0..9` (`0x0400-0x0409`), `_Star`, `_Pound`, `_Start`, `_Pause`, `_Reset` | `inputdefs.h:231-245` |
| Video standard | `ATVideoStandard ATSimulator::GetVideoStandard();` `{NTSC,PAL,SECAM,PAL60,NTSC50}`; `bool IsVideo50Hz()` | `src/Altirra/h/constants.h:117-124`, `simulator.h:192-193` |
| Settings load | `ATSettingsLoadLastProfile(ATSettingsCategory mask)`, masks `kATSettingsCategory_All`, `_FullScreen`, `_Input`, `_InputMaps`, `kATSettingsCategory_AllCategories` | `src/Altirra/source/settings.cpp:1843`, masks in `src/h/at/.../constants` |
| VFS / state deser | `ATVFSInstallAtfsHandler()` (`src/h/at/atio/atfs.h:22`), `ATInitSaveStateDeserializer()` | per BridgeServer init |

**Source-of-truth references for behaviour and defaults:**

- `src/AltirraBridgeServer/main_bridge.cpp:359-427` — exact headless startup
  sequence (mirror this).
- `src/AltirraBridgeServer/CMakeLists.txt` — exact headless target shape, link
  set, per-file SIMD flags, MSVC `intrin.h` force-include, SDL3-strip flags.
- `src/AltirraSDL/cmake/altirra_core_sources.cmake` — `ALTIRRA_ALL_SOURCES`
  (filtered glob of `src/Altirra/source/*.cpp` minus UI/Win32/`cmd*`/platform).
- `src/Altirra/source/settings.cpp` and `cmd*.cpp` — canonical Windows defaults
  (per CLAUDE.md, never rely on member-initialiser defaults).
- `src/AltirraSDL/source/commands_sdl3.cpp`, `input/joystick_sdl3.cpp`,
  `input/keyboard_keymap_sdl3.cpp` — how the SDL3 frontend wires input.

---

## External References Checked

- Libretro core development overview:
  https://docs.libretro.com/development/cores/developing-cores/
- Canonical `libretro.h`:
  https://raw.githubusercontent.com/libretro/libretro-common/master/include/libretro.h
- Core-options V2 helper pattern (`libretro_core_options.h` +
  `retro_core_options_intl.h` + `libretro_core_options_v2_intl.h`), as used by
  most modern cores, e.g.
  https://github.com/libretro/nestopia/blob/master/libretro/libretro_core_options.h
- Disk control (extended) interface:
  https://github.com/libretro/RetroArch/blob/master/disk_control_interface.h
- Atari800 libretro docs / `.info`:
  https://docs.libretro.com/library/atari800/ ,
  https://raw.githubusercontent.com/libretro/libretro-super/master/dist/info/atari800_libretro.info

### Confirmed libretro.h constants (so the team does not have to look them up)

```
RETRO_API_VERSION                              1
RETRO_REGION_NTSC                              0
RETRO_REGION_PAL                               1
RETRO_PIXEL_FORMAT_XRGB8888                    (enum value 1)
RETRO_ENVIRONMENT_SET_PIXEL_FORMAT             10
RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO           32   // reinitialises A/V drivers
RETRO_ENVIRONMENT_SET_GEOMETRY                 37   // resize only, no A/V reinit
RETRO_ENVIRONMENT_GET_INPUT_BITMASKS           (51 | RETRO_ENVIRONMENT_EXPERIMENTAL)
RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE 58
RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2          67
RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL     68
```

Key libretro rules confirmed from the spec:

- A core is a single dynamic library; the frontend owns audio, video, input,
  timing, drivers, and UI. The core must never pace itself with sleeps or
  wall-clock timing.
- `retro_run()` must poll input **at least once** and call the video callback
  **exactly once** per invocation. Audio is emitted via the selected callback.
- `retro_serialize_size()` must **not increase** between `retro_load_game()` and
  `retro_unload_game()`. It may decrease. Pick a fixed maximum right after load.
- `SET_SYSTEM_AV_INFO` reinitialises the frontend's audio and video drivers
  (use when fps or sample rate changes, e.g. NTSC↔PAL). `SET_GEOMETRY` resizes
  the viewport with **no** driver reinit (use for overscan-crop / aspect
  changes that keep fps and sample rate constant).
- Batch audio (`retro_audio_sample_batch_t`) is preferred. Frames are
  interleaved **signed 16-bit stereo**; one "frame" is one L+R pair.
- Core option keys must be namespaced with the core name.
- The libretro API is neither reentrant nor thread-safe; the core must assume
  single-threaded use.

---

## Existing Local Architecture To Reuse

The libretro target starts from the existing headless **BridgeServer**
architecture, not from `AltirraSDL`'s SDL3/ImGui application loop. Crucially,
several BridgeServer components are reused **directly** rather than
re-implemented:

| Reuse directly | What it gives you | Caveat |
| --- | --- | --- |
| `ui_stubs.cpp` null `IVDVideoDisplay` + `VDVideoDisplayFrame` symbols + null `IATUIRenderer` | frame capture plumbing and the `IATUIRenderer` that `ATDiskInterface::Init` dereferences | **`ui_stubs.cpp` `#include`s `<SDL3/SDL.h>` (line 19).** For a SDL-free core, fork it into `libretro_video.cpp` with the SDL include removed (it is incidental), or split the null-display class into its own SDL-free TU. See **SDL3 Elimination** below. |
| `bridge_savestate.cpp` (`ATBridge::SaveStateToBuffer` / `LoadStateFromBuffer`) | `.altstate2` blobs to/from memory | Output is a **zip**, so size is content-dependent → needs a fixed-max wrapper (see Save States). |
| `bridge_commands_render.cpp` `CaptureXrgb()` | `GetLastFrameBuffer` + `VDPixmapBlt` to XRGB8888 | `bridge_commands_render.cpp` also pulls bridge-protocol headers; copy only the `CaptureXrgb` helper, not the whole TU. |
| `main_bridge.cpp` startup sequence | the canonical headless init order | Strip the socket/main-loop/sleep parts; RetroArch owns the loop. |
| `altirra_core_sources.cmake` (`ALTIRRA_ALL_SOURCES`) | the filtered platform-agnostic source list | shared verbatim. |

> **Correction vs first draft.** The first draft invented
> `ATLibretroCreateNullVideoDisplay()` and
> `ATLibretroNullVideoDisplayConsumeFramePosted()`. The real symbols are
> `ATBridgeCreateNullVideoDisplay()` and
> `ATBridgeNullVideoDisplayConsumeFramePosted()`. Reuse them (renamed only if
> the SDL-free fork is taken); do not write a third implementation.

---

## High-Level Design

Add a new source directory:

```text
src/AltirraLibretro/
  CMakeLists.txt
  libretro.cpp            # exported retro_* ABI, owns global state
  libretro_core.h         # ATLibretroCore struct + shared decls
  libretro_audio.cpp/.h   # IATAudioOutput backend feeding audio_batch_cb
  libretro_video.cpp/.h   # SDL-free null IVDVideoDisplay + CaptureXrgb + geometry
  libretro_input.cpp/.h   # RetroPad/keyboard/mouse → ATInputManager + default maps
  libretro_options.cpp/.h # core option definitions + apply logic
  libretro_dirs.cpp/.h    # RetroArch dirs + ATGetConfigDir/registry (shadows system's SDL TUs)
  libretro_time.cpp       # std::chrono time + timers (shadows system's time_sdl3.cpp)
  libretro_disk.cpp/.h    # (milestone 7) disk control ext interface + .m3u
  libretro/libretro.h     # vendored, pinned upstream header
  altirra_libretro.info
  README.md
```

`libretro.cpp` owns the exported ABI. The other files keep the adapter small
and reviewable.

Keep the emulator single-threaded. Never call simulator APIs from callbacks
that may run outside `retro_run()` (file-dialog/VFS callbacks, etc.). Capture
plain data in such callbacks and consume it synchronously in `retro_run()`
(CLAUDE.md "Assume all callbacks are called from arbitrary threads").

---

## Build System Plan

Add a top-level option in `CMakeLists.txt`:

```cmake
option(ALTIRRA_LIBRETRO "Build Altirra as a libretro core" OFF)
```

and gate the subdirectory the same way BridgeServer is gated in
`src/CMakeLists.txt:29-36` — **but without requiring `ALTIRRA_SDL3=ON`**:

```cmake
# src/CMakeLists.txt
if(ALTIRRA_LIBRETRO AND NOT ANDROID)
    add_subdirectory(AltirraLibretro)
endif()
```

> **Correction / important build coupling.** The top-level CMake currently
> fetches and builds SDL3 whenever `ALTIRRA_SDL3 OR NOT WIN32`
> (`CMakeLists.txt:285`), because the `system` library uses SDL3 on non-Windows.
> The libretro target wants to avoid SDL3 entirely (see next section). The
> implementation must add a third state to that guard so a pure-libretro,
> non-Windows configure does **not** unconditionally pull SDL3. Concretely:
> change the guard to `if(ALTIRRA_SDL3 OR (NOT WIN32 AND NOT ALTIRRA_LIBRETRO_NO_SDL3))`
> (or equivalent), and make `system` link `SDL3::SDL3` only when that target
> exists. Verify the SDL3 frontend, BridgeServer, Android, and WASM builds are
> unchanged.

`src/AltirraLibretro/CMakeLists.txt` mirrors `AltirraBridgeServer/CMakeLists.txt`:

- `add_library(AltirraLibretro SHARED ...)` with `OUTPUT_NAME "altirra_libretro"`
  and **no `lib` prefix on any platform** (`set_target_properties(... PREFIX "")`)
  — RetroArch expects `altirra_libretro.so`, not `libaltirra_libretro.so`.
- Link the same core libraries as BridgeServer:
  `ATCPU ATEmulation ATDevices ATIO ATAudio ATNetwork ATNetworkSockets
  ATDebugger ATVM ATCore Kasumi vdjson system`.
- `${ALTIRRA_ALL_SOURCES}` from `altirra_core_sources.cmake`.
- The same portable replacement/stub sources BridgeServer compiles
  (`src/AltirraSDL/stubs/console_stubs.cpp`, `oshelper_stubs.cpp`,
  `uiaccessors_stubs.cpp`, `win32_stubs.cpp`, `device_stubs.cpp`;
  `src/AltirraSDL/source/os/{directorywatcher_sdl3,midimate_sdl3}.cpp`;
  `src/AltirraSDL/source/input/keyboard_keymap_sdl3.cpp`;
  `src/Altirra/source/{uiregistry,savestateio}.cpp`;
  `src/ATUI/source/uicommandmanager.cpp`; `src/Dita/source/accel.cpp`;
  `src/VDDisplay/source/{screenfx,bloom,bicubic,displaytypes}.cpp`).
  Use the BridgeServer CMakeLists as the authoritative list — do not hand-curate
  a divergent copy.
- POSIX replacement device sources on non-Windows (as BridgeServer does):
  `modemtcp_sdl3.cpp`, `idephysdisk_sdl3.cpp`, `pipeserial_sdl3.cpp`.
- Reuse BridgeServer's per-file SIMD flags (`-mbmi`, `-mssse3`, NEON/ARM64
  guards) and the MSVC `intrin.h` force-include workaround verbatim — these are
  not optional; the Ninja+cl build fails without them.
- Inherit `CMAKE_CXX_STANDARD` from the project (currently **23**). Do not
  downgrade — Altirra headers use C++23 features.

Compile definitions:

```cmake
target_compile_definitions(AltirraLibretro PRIVATE
    AT_SDL3_PORTABLE=1     # enables the cross-platform core code paths
    ALTIRRA_LIBRETRO=1
)
```

> Do **not** define `ALTIRRA_AUDIO_NULL`. BridgeServer compiles
> `audiooutput_sdl3.cpp` *with* `ALTIRRA_AUDIO_NULL` to stub audio out. The
> libretro target instead **excludes `audiooutput_sdl3.cpp` entirely** and
> supplies its own `ATCreateAudioOutput()` in `libretro_audio.cpp` (see Audio).
> Defining `ALTIRRA_AUDIO_NULL` and also providing a real factory would be a
> duplicate-symbol error.

Hidden visibility, export only `retro_*`:

```cmake
set_target_properties(AltirraLibretro PROPERTIES
    PREFIX ""
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)
```

On Windows `RETRO_API` expands to `__declspec(dllexport)`; on ELF/Mach-O the
`retro_*` functions carry `__attribute__((visibility("default")))` (already in
the vendored `libretro.h` via `RETRO_API`). As a defence-in-depth measure on
ELF, add a version script / `--exclude-libs,ALL` so transitively linked static
libs do not re-export their symbols.

---

## SDL3 Elimination (the main build risk — read carefully)

The first draft asserted the core should have "no dynamic SDL3 dependency" and
that CI should "fail if libSDL3 appears". That goal is correct **but not free**,
and the reason is subtle:

On non-Windows, the `system` static library compiles `*_sdl3.cpp` files, three
of which actually reference SDL symbols:

| `system` object | SDL symbols referenced | Functions it defines |
| --- | --- | --- |
| `configdir_sdl3.cpp` | `SDL_GetPrefPath`, `SDL_free` | `VDStringA ATGetConfigDir()` |
| `registry_sdl3.cpp` | `SDL_GetPrefPath`, `SDL_free` (×6) | registry path helpers |
| `time_sdl3.cpp` | `SDL_GetTicks`, `SDL_GetPerformanceCounter` | `VDGetCurrentTick`, `VDGetCurrentTick64`, `VDGetPreciseTick`, `VDGetPreciseTicksPerSecond[I]`, `VDGetPreciseSecondsPerTick`, `VDGetAccurateTick`, `VDCallbackTimer`, `VDLazyTimer` |

Because the core calls `ATRegistryLoadFromDisk()`, `ATGetConfigDir()`, and tick
functions, these objects **are** pulled from the archive, so `--as-needed` /
`-dead_strip_dylibs` **cannot** drop `libSDL3` — the symbols are genuinely
referenced. (This means BridgeServer almost certainly still carries a dynamic
`libSDL3` load command on Linux/macOS despite its strip flags; do not treat
BridgeServer as proof that the strip works.)

Static-library linking is **per-object-file**, not per-function: providing a
replacement for *one* function in `time_sdl3.cpp` will not prevent the object
from being pulled if any *other* symbol it defines is referenced. To keep an
object out of the link you must satisfy its **entire** exported symbol set from
an earlier object.

### Chosen approach: additive replacement TUs, zero changes to `system`

The core must do this **properly** (a distributed RetroArch core cannot carry a
host `libSDL3` dependency) **without regressing** the AltirraSDL or BridgeServer
builds. The way to satisfy both is to be **purely additive**: do not modify
`system`, do not change which files it compiles, do not touch its `_sdl3.cpp`
sources. Those builds keep using SDL exactly as today. Instead, the libretro
target provides its own objects that *out-resolve* the SDL-using ones via static
link order.

Add two libretro TUs that, **between them, define the complete exported symbol
set** of all three SDL-using `system` objects, and list them **before** `system`
in the libretro target's link line:

1. **`libretro_time.cpp`** — reimplement the *entire* `time_sdl3.cpp` surface:
   `VDGetCurrentTick`, `VDGetCurrentTick64`, `VDGetPreciseTick`,
   `VDGetPreciseTicksPerSecond[I]`, `VDGetPreciseSecondsPerTick`,
   `VDGetAccurateTick` (via `std::chrono::steady_clock`) **and** `VDCallbackTimer`
   / `VDLazyTimer` (via `std::thread` + `std::condition_variable`). These timers
   are used by the scheduler — they must be real, not stubs. Mirror the Win32
   `time.cpp` semantics. The set must be *complete*: if even one symbol that
   `time_sdl3.o` defines is left undefined here, the linker pulls
   `time_sdl3.o` and SDL comes back.
2. **`libretro_dirs.cpp`** — define `ATGetConfigDir()` (the whole
   `configdir_sdl3.o` surface) and the registry-path helpers
   (`registry_sdl3.o` surface), rooted at RetroArch's save/system directories
   (`GET_SAVE_DIRECTORY` / `GET_SYSTEM_DIRECTORY`). This is needed **anyway** for
   firmware/NVRAM placement (see Firmware), so it is not extra work.

Why this is regression-proof and correct:

- **Static-archive semantics**: a member object is extracted only if it
  resolves an as-yet-undefined symbol. Because the libretro objects (linked
  first) already define every symbol `time_sdl3.o`/`configdir_sdl3.o`/
  `registry_sdl3.o` would provide, those members are **never pulled**, so no
  SDL symbol is referenced and `libSDL3` is not a NEEDED entry. No duplicate
  symbols, because the archive members simply aren't extracted.
- **AltirraSDL / BridgeServer / Android / WASM are untouched** — they do not
  link `libretro_time.cpp`/`libretro_dirs.cpp`, so they keep pulling the
  `_sdl3` objects and using SDL exactly as before. `system` itself is not
  edited.
- **Windows** already takes the `winmm`/`shlwapi` branch (no SDL3 in `system`),
  so these replacement TUs are only strictly required on non-Windows; compile
  them everywhere anyway for a single code path and to own the RetroArch dir
  logic.

The one maintenance hazard is **drift**: if upstream later adds a new symbol to
`time_sdl3.cpp`, the archive member would be pulled again and silently
re-introduce SDL with no compile error. The **hard CI "no libSDL3" gate**
(below) catches this immediately, which is why that gate is mandatory rather
than advisory. Add a short comment in each replacement TU naming the
`system` file whose symbol set it shadows, so the coupling is discoverable.

A combined configure that builds *both* the SDL3 frontend and the libretro core
will still fetch SDL3 (the frontend needs it); in that case the libretro core
also links it and the "no libSDL3" check is relaxed for that build only. The
shipping/CI configuration builds the libretro target **alone** with
`-DALTIRRA_LIBRETRO_NO_SDL3=ON`, where SDL3 is neither fetched nor linked and
the gate is hard.

### Interim fallback during bring-up only

Until `libretro_time.cpp`/`libretro_dirs.cpp` land (Milestone 1.5), build with
SDL3 present and keep the "no libSDL3" check **warn-only** so Milestones 0–1 are
not blocked. A non-experimental core must **not** ship in this state. Static
linking SDL3 (`-DALTIRRA_STATIC_SDL3=ON`) is a possible stopgap but is inferior
to the replacement TUs (it bakes all of SDL in for ~4 symbols and still needs a
headless-driver SDL build), so it is not the recommended path — it is only a
break-glass option if the replacement work is unexpectedly blocked.

The milestone plan sequences this explicitly (Milestone 1.5).

> **Note for Windows.** On Windows `system` takes the `winmm`/`shlwapi` branch
> and links no SDL3 at all (`src/system/CMakeLists.txt:94`), so the Windows
> core is SDL-free without any of the above.

---

## Vendoring `libretro.h`

Add a pinned copy of the canonical MIT-licensed header at
`src/AltirraLibretro/libretro/libretro.h`. Do not rely on a system install —
the libretro API is a single header and vendoring removes CI/environment
ambiguity. Record the upstream commit SHA in `README.md` and re-pin
deliberately. Do **not** vendor or depend on the rest of `libretro-common`; the
core-options V2 helper (below) is a single small `.h`/`.c` pair that can be
vendored alongside if desired, or open-coded.

---

## Libretro ABI Surface

`libretro.cpp` must implement every symbol below (all `RETRO_API`):

```cpp
unsigned retro_api_version(void);                       // return RETRO_API_VERSION (1)
void retro_set_environment(retro_environment_t);
void retro_set_video_refresh(retro_video_refresh_t);
void retro_set_audio_sample(retro_audio_sample_t);
void retro_set_audio_sample_batch(retro_audio_sample_batch_t);
void retro_set_input_poll(retro_input_poll_t);
void retro_set_input_state(retro_input_state_t);
void retro_init(void);
void retro_deinit(void);
void retro_get_system_info(struct retro_system_info *);
void retro_get_system_av_info(struct retro_system_av_info *);
void retro_set_controller_port_device(unsigned port, unsigned device);
void retro_reset(void);
bool retro_load_game(const struct retro_game_info *);
void retro_unload_game(void);
void retro_run(void);
size_t retro_serialize_size(void);
bool retro_serialize(void *data, size_t size);
bool retro_unserialize(const void *data, size_t size);
void retro_cheat_reset(void);
void retro_cheat_set(unsigned index, bool enabled, const char *code);
bool retro_load_game_special(unsigned, const struct retro_game_info *, size_t);
unsigned retro_get_region(void);
void *retro_get_memory_data(unsigned id);
size_t retro_get_memory_size(unsigned id);
```

`retro_cheat_*` are no-ops initially. `retro_load_game_special()` returns
`false` until subsystem loading is implemented. `retro_get_region()` maps
`GetVideoStandard()`/`IsVideo50Hz()` → `RETRO_REGION_PAL` for 50 Hz standards,
`RETRO_REGION_NTSC` otherwise.

---

## Core Global State

Single global instance (libretro is single-instance):

```cpp
struct ATLibretroCore {
    retro_environment_t        env          = nullptr;
    retro_video_refresh_t      video        = nullptr;
    retro_audio_sample_t       audio_sample = nullptr;
    retro_audio_sample_batch_t audio_batch  = nullptr;
    retro_input_poll_t         input_poll   = nullptr;
    retro_input_state_t        input_state  = nullptr;

    IVDVideoDisplay *nullDisplay  = nullptr;   // ATBridge null display
    bool initialized   = false;
    bool gameLoaded    = false;
    bool optionsDirty  = false;
    int  lastFrameW    = 0;     // for geometry-change detection
    int  lastFrameH    = 0;
    ATVideoStandard lastStd = kATVideoStandard_NTSC;

    ATLibretroAudio   audio;
    ATLibretroVideo   videoState;
    ATLibretroInput   input;
    ATLibretroOptions options;
};
```

> **Correction.** Do not embed an `ATSimulator` member. The existing core/stub
> paths expect a global symbol `ATSimulator g_sim;` (BridgeServer relies on
> this). Keep `g_sim` as a translation-unit global exactly as BridgeServer does
> and have `ATLibretroCore` reference it, rather than owning a simulator member.

---

## Simulator Initialization

Factor the BridgeServer startup (`main_bridge.cpp:359-427`) into reusable
helpers and call them from `retro_load_game()` (not `retro_init()` — see below):

```cpp
bool ATLibretroInitSimulator();      // one-time: Init/LoadROMs/devices/settings
void ATLibretroShutdownSimulator();
bool ATLibretroLoadContent(const char *utf8Path);
void ATLibretroUnloadContent();
```

Startup sequence (mirror BridgeServer exactly, in this order):

1. `VDRegistryAppKey::setDefaultKey("AltirraSDL")` — keep the same key unless a
   separate libretro namespace is deliberately introduced (changing it loses
   compatibility with existing settings/profiles).
2. Install the libretro directory providers (config/save dir from RetroArch)
   **before** any registry/config load — this is the `libretro_dirs.cpp`
   override that also removes the SDL config/registry dependency.
3. `ATRegistryLoadFromDisk()`.
4. `ATInitSaveStateDeserializer()`.
5. `ATVFSInstallAtfsHandler()`.
6. `g_sim.Init()`.
7. `g_sim.SetRandomSeed(seed)` — derive a deterministic-but-varied seed; do not
   call `Math.random`-style APIs. A fixed seed improves rewind/netplay
   determinism; a per-load seed matches desktop behaviour. Default to the
   BridgeServer expression but expose determinism as a later option.
8. `g_sim.LoadROMs()`.
9. Install the null display:
   `g_pNullDisplay = ATBridgeCreateNullVideoDisplay();`
   `g_sim.GetGTIA().SetVideoOutput(g_pNullDisplay);`
   `g_sim.GetGTIA().SetFrameSkip(true);`
10. `ATRegisterDevices(*g_sim.GetDeviceManager()); ATRegisterDeviceXCmds(...);`
11. `ATSocketInit();`
12. `ATLoadConfigVars();`
13. `ATOptionsLoad();`
14. Load settings via the BridgeServer path:
    `ATLoadDefaultProfiles();` then
    `ATSettingsLoadLastProfile(mask);` where
    `mask = kATSettingsCategory_All` minus `kATSettingsCategory_FullScreen`.
    > **Correction / input caveat.** BridgeServer *also* excludes
    > `kATSettingsCategory_Input` and `kATSettingsCategory_InputMaps` because it
    > is headless. The libretro core **needs input**, so it must **not** blindly
    > exclude those — either include them (to load the user's saved Atari input
    > maps) or, preferably, create default input maps programmatically (see
    > Input Backend). Excluding them and then calling `OnButtonDown` yields no
    > effect because nothing maps input codes to Atari triggers.
15. `ATInitDebugger()` — BridgeServer calls this; some linked core paths assume
    a registered debug target. Keep it even though the core exposes no debugger
    UI.
16. Apply libretro core options (machine/memory/video/BASIC/etc.) **after**
    settings load, **before** the reset that boots content.
17. `g_sim.ColdReset();`
18. `g_sim.Resume();`

Do **not** copy BridgeServer's socket polling or `sleep`-based main loop.

---

## `retro_set_environment()` and `retro_init()`

`retro_set_environment(cb)` stores `cb` and registers what can be set before a
game is loaded:

- `RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2` (+ `_V2_INTL`), with a graceful
  fallback to V1 / `SET_VARIABLES` for old frontends (use the standard
  `libretro_core_options.h` helper; see Core Options).
- `RETRO_ENVIRONMENT_SET_CONTROLLER_INFO`
- `RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME` (value **`true`** — the core boots the
  configured machine with no media, matching the Atari800 core; see below)
- `RETRO_ENVIRONMENT_GET_INPUT_BITMASKS` (query support; use bitmask polling if
  available — it is `EXPERIMENTAL`-tagged but universally supported)

In `retro_set_environment()` or `retro_init()` also wire:

- `RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK`
- `RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY`, `GET_SAVE_DIRECTORY`
- input descriptors (`RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS`)

`RETRO_ENVIRONMENT_SET_PIXEL_FORMAT` (XRGB8888) should be issued in
`retro_load_game()` (a frontend may reset pixel format between loads; setting it
at load time is the safe, widely-used convention). Bail out of
`retro_load_game()` if the frontend rejects XRGB8888 (it should not).

`retro_init()` initialises only adapter-level state (no simulator yet). With
`supports_no_game = true`, RetroArch may call `retro_load_game(NULL)` to start
the core with no media. Full simulator startup therefore happens in
`retro_load_game()` for **both** the content and the no-content case — see the
NULL handling below. (Keeping init in `retro_load_game` rather than `retro_init`
means a single startup path and lets core options that need the machine
configured run before the first boot.)

---

## `retro_get_system_info()`

```cpp
info->library_name    = "Altirra";
info->library_version = <AT_VERSION from src/Altirra/autobuild_default/version.h, e.g. "dev">;
info->valid_extensions =
    "atr|xfd|atx|atz|dcm|pro|arc|"      // disk
    "bin|rom|car|a52|"                  // cartridge
    "xex|exe|obx|com|bas|"             // executables / BASIC
    "cas|wav|flac|ogg|"                // tape
    "sap|vgm|vgz|"                     // audio/music
    "zip|gz|"                          // archives
    "altstate|atstate2";               // save states (auto-detected by Load)
info->need_fullpath  = true;
info->block_extract  = false;
```

> **Verified.** Every extension above is backed by `src/ATIO/source/image.cpp`
> (`bin/rom/car/a52` cartridge; `xfd/atr/atx/atz/dcm` disk; `cas/wav` tape;
> `xex/exe/obx/com` program; `bas` BASIC; `sap` SAP; `vgm/vgz` VGM) plus
> `src/Altirra/source/uifilefilters.cpp` (`arc`, `pro`, `flac`, `ogg`, `zip`,
> `gz`, `altstate`, `atstate2`). The library version string comes from
> `AT_VERSION` (`version.h`); prefer `git describe` injected at configure time
> for traceability.

`need_fullpath = true` for milestone 1 (Altirra's loaders are path-aware and
handle archives/companion files). Revisit `need_fullpath = false` later by
wrapping `retro_game_info::data` in an `IVDRandomAccessStream` and using the
stream `Load()` overload (enables soft-patching and frontend-managed loading).

---

## `retro_get_system_av_info()`

```cpp
info->geometry.aspect_ratio = 4.0f / 3.0f;     // tune per pixel aspect (below)
info->timing.sample_rate    = 48000.0;
info->timing.fps            = ATLibretroComputeFps(g_sim.GetVideoStandard());
// geometry base/max — MUST VERIFY (see below)
```

**fps derivation (do not hardcode 59.94/50.0 blindly).** Compute from the
active standard's master clock and frame timing rather than a literal:

- NTSC: 1789772.5 Hz / (114 cyc/line × 262 lines) ≈ **59.9227 Hz**
- PAL / SECAM: master / (114 × 312 lines) ≈ **49.86 / 50.09 Hz**
- PAL60 / NTSC50: use the actual line count for that hybrid mode.

Master clocks live in `src/h/at/atcore/constants.h`. Prefer querying the
simulator/ANTIC for the real cycles-per-frame of the current standard so the
value always matches the emulator's true frame cadence (this keeps RetroArch
audio/video sync tight). `GetVideoStandard()` + `IsVideo50Hz()`
(`simulator.h:192-193`) select the standard.

**Geometry — MUST VERIFY by measurement.** Do not ship guessed constants. After
boot, read the actual `VDPixmap` from `GetLastFrameBuffer()` for **each** mode
(NTSC, PAL, SECAM, PAL60, NTSC50) **with artifacting both off and on** (NTSC
high-res artifacting can change horizontal resolution), plus the overscan-crop
options. Set:

- `base_width` / `base_height` = the default (normal-overscan) frame size.
- `max_width` / `max_height` = the **largest** width/height any supported
  mode+artifacting+overscan combination can produce, with margin. A too-small
  `max_*` truncates the frame; too-large wastes a frontend allocation. Err
  generous (the first draft's `456×312` is almost certainly too small once
  2×-horizontal artifacting and full overscan are considered — measure before
  trusting any number).

When the active standard changes (e.g. a disk switches NTSC→PAL), call
`SET_SYSTEM_AV_INFO` (fps **and** sample rate may change → full A/V reinit).
When only the visible window changes (overscan crop, aspect) with fps and
sample rate constant, call `SET_GEOMETRY` (no reinit). Detect changes by
comparing against `lastFrameW/H/lastStd` each `retro_run()`.

---

## `retro_load_game()`

1. `game` may be **NULL** (no-content boot, because `supports_no_game = true`).
   If `game != NULL`, require `game->path` (since `need_fullpath = true`).
2. `ATLibretroInitSimulator()` if not already initialised.
3. Issue `SET_PIXEL_FORMAT(XRGB8888)`; fail if rejected.
4. Apply core options that affect hardware (machine/memory/video/BASIC) **before
   loading content**, so the right machine is configured for the media (or for
   the bare-machine boot).
5. **If `game != NULL`:**
   a. `VDStringW wpath = VDTextU8ToW(game->path);`
   b. Build an `ATImageLoadContext` (`image.h:60`) as needed; for plain media a
      default context is fine.
   c. `g_sim.Load(wpath.c_str(), kATMediaWriteMode_RO, &ctx);` — read-only for
      milestone 1 (writable media is a later, explicit option). Return `false`
      on load failure.
   **If `game == NULL`:** skip loading — the configured machine boots with no
   media (BASIC prompt / self-test / Memo Pad per machine + `altirra_basic`).
6. `g_sim.ColdReset();`
7. `g_sim.Resume();`
8. Read `GetVideoStandard()` and update `timing`/`geometry` caches; if they
   differ from the values reported by the most recent `retro_get_system_av_info`,
   issue `SET_SYSTEM_AV_INFO`.
9. Optionally prime one frame (advance until `ConsumeFramePosted`), or let the
   first `retro_run()` produce it.
10. `gameLoaded = true; return true;`

`.altstate` / `.atstate2` content is auto-detected by `g_sim.Load()` (same path
BridgeServer uses). Return `false` on any `Load()` failure and log via the
frontend log callback.

---

## `retro_run()`

Each call advances exactly one frontend frame and calls `video_cb` exactly once.

```cpp
void retro_run() {
    // 1. Apply changed core options.
    bool updated = false;
    if (env(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
        ATLibretroApplyOptions();          // only apply "live-safe" changes

    // 2. Poll input once, then translate to Atari input.
    input_poll();
    ATLibretroUpdateInput();               // RetroPad/keyboard/mouse → ATInputManager + GTIA switches

    // 3. Reset the per-frame audio accumulator.
    g_core.audio.BeginFrame();

    // 4. Advance until the null display reports a posted frame.
    for (;;) {
        ATSimulator::AdvanceResult r = g_sim.Advance(false);
        if (ATBridgeNullVideoDisplayConsumeFramePosted(g_core.nullDisplay))
            break;
        if (r == ATSimulator::kAdvanceResult_Stopped)
            break;                          // paused/halted; emit last frame
    }

    // 5. Capture + submit video.
    if (ATLibretroCaptureXrgb(g_sim, g_core.videoState.buffer)) {
        int w = g_core.videoState.buffer.w, h = g_core.videoState.buffer.h;
        ATLibretroMaybeUpdateGeometry(w, h);     // SET_GEOMETRY/AV_INFO on change
        video(g_core.videoState.buffer.data, w, h, g_core.videoState.buffer.pitch);
    } else {
        // Before the first valid frame: dupe (nullptr) tells the frontend to
        // reuse its last frame. After one valid frame exists, re-submit it.
        video(g_core.videoState.lastValid ? g_core.videoState.lastValid : nullptr,
              g_core.lastFrameW, g_core.lastFrameH, g_core.videoState.lastPitch);
    }

    // 6. Flush accumulated audio for this frame.
    g_core.audio.FlushTo(g_core.audio_batch);
}
```

Rules:

- Exactly one `video()` call per `retro_run()`. Exactly one `input_poll()`.
- No sleeping, timers, wall-clock pacing, SDL events, or ImGui anywhere in this
  path.
- `kAdvanceResult_Stopped` means the simulator is paused/halted (e.g. CPU
  jammed with halt-on-jam). Treat it as "no new frame this tick" and re-submit
  the last valid frame rather than spinning.
- `dropFrame=false` is passed to `Advance` because the null display is always
  consumed each frame; there is no display backlog to skip.

---

## Video Backend (`libretro_video.cpp`)

Responsibilities:

- Provide the null `IVDVideoDisplay` (forked from `ui_stubs.cpp`, **SDL include
  removed**) that: accepts `PostBuffer()`, flags a frame as posted, recycles
  frames via `RevokeBuffer()`, reports `GetQueuedFrames()`/`IsFramePending()`,
  and never blocks. Provide the `VDVideoDisplayFrame`
  ctor/dtor/`AddRef`/`Release` symbols (the libretro target does not link the
  Win32 `VDDisplay/source/display.cpp`).
- `ATLibretroCaptureXrgb(sim, dst)` — copy of `bridge_commands_render.cpp`'s
  `CaptureXrgb`: `GetLastFrameBuffer(srcBuf, src)` → `dst.init(src.w, src.h,
  kPixFormat_XRGB8888)` → `VDPixmapBlt(dst, src)`. Returns false pre-boot.
- Track frame size and detect geometry/standard changes for
  `SET_GEOMETRY`/`SET_SYSTEM_AV_INFO`.
- Keep a copy of the last valid frame for dupe-submission.

The XRGB8888 layout is `0x00RRGGBB` little-endian, which is exactly
`RETRO_PIXEL_FORMAT_XRGB8888`. `pitch` passed to `video()` is the destination
`VDPixmapBuffer` pitch in bytes (4 × width when tightly packed).

---

## Audio Backend (`libretro_audio.cpp`)

Implement the **full** `IATAudioOutput` surface (`audiooutput.h:69-125`) plus
its `IATAudioMixer` (returned by `AsMixer()`), and provide the factory:

```cpp
IATAudioOutput *ATCreateAudioOutput();    // returns the libretro backend
```

> **Correction — exact write signature.** The real method is
> `void WriteAudio(const float *left, const float *right, uint32 count, bool
> pushAudio, bool pushStereoAsAudio, uint64 timestamp)`. Samples are **float**
> and **non-interleaved** (separate L/R pointers) — *not* int16, *not*
> interleaved as the first draft implied (`pushStereoAsMono` was also wrong; the
> flag is `pushStereoAsAudio`).

**Resampling reality.** Altirra mixes internally at ~POKEY rate (≈64 kHz float)
and uses its **own polyphase resampler** to reach the output rate; SDL never
resamples (it is "a dumb S16 sink"). The libretro backend must therefore **keep
the resampler** and only replace the device sink:

- Copy the non-SDL mixing/resampling structure from `audiooutput_sdl3.cpp`
  (mixer, sync sources, async sources, sample pool, polyphase resampler, filter
  stage). Do **not** write a trivial fake `IATAudioOutput` that drops or
  partially mixes sources — POKEY, cassette, disk, and audio-monitor sources
  must all be audible, identical to the SDL3 build.
- `Init(scheduler)` / `SetCyclesPerSecond(cps, repeat)` configure the resampler
  exactly as the SDL3 path does, with the **output rate fixed at 48000 Hz**
  (must equal `av_info.timing.sample_rate`).
- `InitNativeAudio()` is a no-op (no OS device).
- `Pause()` / `Resume()` gate whether samples accumulate.
- The resampler output (interleaved S16 stereo) is appended to a per-frame
  `std::vector<int16_t>` instead of an SDL stream. The conversion to S16
  (clamp/round) must match `audiooutput_sdl3.cpp` so volume/clipping behaviour
  is identical.
- `BeginFrame()` clears the accumulator; `FlushTo(audio_batch_cb)` calls
  `audio_batch_cb(buf.data(), frameCount)` where `frameCount = samples / 2`.
- `GetAudioStatus()` returns sane values for any UI/debug query paths.
- `GetPipelineLatencyBytes()` returns 0 (RetroArch owns buffering).

Expose a single fixed 48000 Hz rate initially (stable AV info is more important
than a 44.1/48 choice). A sample-rate option can come later, but changing it
requires `SET_SYSTEM_AV_INFO`.

---

## Input Backend (`libretro_input.cpp`)

> **Key design correction.** A playable core must establish **input maps**.
> BridgeServer excludes `Input`/`InputMaps` settings (headless), so simply
> calling `ATInputManager::OnButtonDown` will do nothing. Two valid options:
> (a) include the `Input`/`InputMaps` settings categories so the user's saved
> Atari maps load; or (b) **create default input maps programmatically** at
> init — a joystick controller on port 1, console-switch bindings, and a
> keyboard controller — mirroring what the SDL3 frontend sets up. Option (b) is
> recommended for predictable out-of-the-box behaviour; study how
> `commands_sdl3.cpp` / `joystick_sdl3.cpp` attach controllers and build the
> equivalent default maps.

Two valid signal paths exist; use the one each Altirra subsystem expects:

- **Joystick directions / triggers / 5200 keypad** → `ATInputManager`
  input codes (`kATInputCode_JoyStick1Left=0x2100 … JoyButton0=0x2800`,
  `kATInputTrigger_5200_*`) via `OnButtonDown/Up(unit, id)` and `OnAxisInput`
  for analog (paddles). The default maps route these to the controller attached
  to the port.
- **Console switches START/SELECT/OPTION** → `GTIA().SetConsoleSwitch(0x01 /
  0x02 / 0x04, state)` directly (this is how `main.cpp:743-751` drives them once
  a trigger fires). Either bind the RetroPad buttons through the input map to
  `kATInputTrigger_Start/Select/Option`, or call `SetConsoleSwitch` directly —
  do not do both for the same button.
- **Keyboard** → reuse `keyboard_keymap_sdl3.cpp`'s canonical Win32 keymap
  (already compiled into the target). Translate libretro `RETROK_*` →
  Altirra `VK_*`/scancode → input code, then drive the keyboard controller.
  Wire both `RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK` (event style) and
  `input_state(... RETRO_DEVICE_KEYBOARD ...)` polling; pick callback mode if it
  proves reliable, else expose a poll/callback option like Atari800 does.

Every acquired input state needs a release path on **all** exit scenarios
(button up, focus loss, `retro_unload_game`, device change) — CLAUDE.md "Every
acquired state must have a release path". On `retro_unload_game` /
`retro_set_controller_port_device`, release all held directions, triggers, keys,
and console switches.

Initial RetroPad mapping (refine against verified Windows behaviour — do **not**
guess hardware constants):

| RetroPad | Atari |
| --- | --- |
| D-pad | Joystick port 1 directions |
| B / A | Trigger 1 (`kATInputCode_JoyButton0`) |
| Start | START console switch |
| Select | SELECT console switch |
| L | OPTION console switch |
| Y / X / L2 / R2 | keyboard Space / Return / Esc / Break as verified mappings (confirm each scancode in the keymap before wiring) |
| R | reserved (virtual keyboard later) |

Use `RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS` with Atari-specific labels so
RetroArch's remap UI is meaningful, and `SET_CONTROLLER_INFO` to advertise
device variants (Atari joystick, Atari keyboard, 5200 controller, paddle,
none). Implement `retro_set_controller_port_device()` to swap the attached
controller type per port.

Milestone order: (1) RetroPad→joystick port 1; (2) console switches; (3)
keyboard; (4) port 2 joystick; (5) 5200 keypad; (6) paddles (analog →
`OnAxisInput`); (7) mouse / light pen / light gun.

---

## Core Options

Use the standard libretro V2 helper layout: a `libretro_core_options.h`
defining `option_defs_us[]` (`retro_core_option_v2_definition`) plus
`option_cats_us[]` (`retro_core_option_v2_category`), an
`retro_core_options_intl.h`, and a `libretro_set_core_options(env)` that probes
`RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION` and downshifts
`SET_CORE_OPTIONS_V2_INTL → SET_CORE_OPTIONS_V2 → SET_CORE_OPTIONS →
SET_VARIABLES` as needed. This is copy-pasteable from any modern core (nestopia,
fceumm) and is the expected idiom.

Prefix every key with `altirra_`. Initial options:

| Key | Values | Default | Applies |
| --- | --- | --- | --- |
| `altirra_system` | `800`, `800xl`, `1200xl`, `130xe`, `xegs`, `5200` | `800xl` | reset |
| `altirra_memory` | `8K`,`16K`,`24K`,`32K`,`40K`,`48K`,`52K`,`64K`,`128K`,`256K`,`320K`,`320K_Compy`,`576K`,`576K_Compy`,`1088K` | per machine | reset |
| `altirra_video_standard` | `ntsc`,`pal`,`secam`,`ntsc50`,`pal60` | `ntsc` | reset; emits `SET_SYSTEM_AV_INFO` |
| `altirra_basic` | `disabled`,`enabled` | Altirra default | reset |
| `altirra_sio_patch` | `off`,`disk`,`cassette`,`disk_and_cassette` | Altirra default | live/reset by media |
| `altirra_artifacting` | `none`,`ntsc`,`ntschi`,`pal`,`palhi`,`auto` | Altirra default | live; may change geometry |
| `altirra_crop_overscan` | `off`,`normal`,`extended`,`full` | `normal` | live; `SET_GEOMETRY` |
| `altirra_audio_filters` | `disabled`,`enabled` | Altirra default | live |
| `altirra_input_port1` | `joystick`,`5200_controller`,`none` | auto by system | reset |
| `altirra_input_port2` | `joystick`,`none` | `none` | live |

Verify every value list and default against `settings.cpp` / `cmd*.cpp` /
`commands_sdl3.cpp` — per CLAUDE.md, do not rely on member-initialiser defaults
where a command handler establishes a different effective default. Mark each
option's "applies" semantics honestly (reset-required vs live) and gate
reset-required options so they take effect on the next `ColdReset`/load, not
mid-frame.

---

## Firmware And RetroArch Directories

RetroArch supplies firmware from its **system directory**. Atari800's
documented names (support these first; Altirra's firmware manager is richer and
can map to its internal IDs):

| File | Purpose |
| --- | --- |
| `5200.rom` | Atari 5200 BIOS |
| `ATARIXL.ROM` | Atari XL/XE OS |
| `ATARIBAS.ROM` | Atari BASIC |
| `ATARIOSA.ROM` | Atari 400/800 OS-A |
| `ATARIOSB.ROM` | Atari 400/800 OS-B |
| `ATARIOSC.ROM` / `BB01R4_OS.ROM` | Atari XL/XE OS variants |
| `XEGAME.ROM` | XEGS built-in Missile Command |

Plan:

1. Query `GET_SYSTEM_DIRECTORY` and `GET_SAVE_DIRECTORY` in
   `retro_set_environment()`/`retro_init()`; cache them.
2. The `libretro_dirs.cpp` provider (the same TU that removes the SDL
   config/registry dependency) points Altirra's config/firmware lookups at:
   `<system>/Altirra/` → `<system>/` → AltirraSDL config path (local-dev
   fallback).
3. Populate/override the firmware manager's paths **before** `g_sim.LoadROMs()`,
   mapping RetroArch's filenames to Altirra firmware IDs. Altirra can also use
   its own embedded fallback ROMs (the SDL3 build embeds several — see the
   `project_embedded_firmware` memory) where licensing allows, so a user with no
   external ROMs still boots to a usable state.
4. Document all firmware in `altirra_libretro.info` (see below).

**Never** ship copyrighted Atari ROMs in CI artifacts.

---

## Save States

Milestone 1 (bring-up): return 0 / false — allowed.

```cpp
size_t retro_serialize_size()                 { return 0; }
bool   retro_serialize(void*, size_t)         { return false; }
bool   retro_unserialize(const void*, size_t) { return false; }
```

Milestone 6: wrap the BridgeServer memory-blob functions.

- `ATBridge::SaveStateToBuffer(g_sim, blob, meta)` produces a **zip** (`.altstate2`),
  so its size varies with content — it cannot be the value returned by
  `retro_serialize_size()` directly (which must be a fixed maximum).
- After `retro_load_game()`, compute one conservative fixed maximum for the
  active machine/profile (e.g. largest plausible blob + headroom) and return it
  from `retro_serialize_size()` thereafter. It must never increase before
  unload.
- `retro_serialize()` writes a small wrapper, then the blob, then zero padding
  to `size`:

  ```text
  magic        : "ALTRLRST"  (8 bytes)
  version      : uint32 LE
  payload_size : uint32 LE   (actual zip length)
  payload_crc32: uint32 LE
  payload      : .altstate2 zip bytes
  padding      : zeroes to fill `size`
  ```

- `retro_unserialize()` validates magic/version/CRC, then calls
  `ATBridge::LoadStateFromBuffer(g_sim, payload, payload_size)`.

This satisfies the "size must not grow after load" rule. It is **not**
memory-efficient for rewind (each snapshot is up to the fixed max). If rewind
memory is a problem, add a deterministic, uncompressed serializer with stable
offsets later. Validate save/load after gameplay, reset, disk boot, cartridge
boot, and 5200.

---

## Disk Control And M3U (Milestone 7)

Milestone 1 omits disk control; a single disk loaded via `retro_load_game()`
still works. Later, register the **extended** interface
(`RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE`, value 58) — prefer it over
the legacy `SET_DISK_CONTROL_INTERFACE` because it adds image labels and an
initial-image hook that RetroArch's modern disk UI uses:

`retro_disk_control_ext_callback` members to implement:
`set_eject_state`, `get_eject_state`, `get_image_index`, `set_image_index`,
`get_num_images`, `replace_image_index`, `add_image_index`,
`set_initial_image`, `get_image_path`, `get_image_label`.

`.m3u` support (add `m3u` to `valid_extensions` only once disk control is
solid):

- Parse each non-comment line as a relative/absolute path (resolve relative to
  the `.m3u` location).
- Mount the selected entry into drive 1 via `ATSimulator::Load()` or the
  drive-specific mount API — never via direct filesystem hacks.
- Expose index changes through disk control; preserve drive/media semantics
  (eject = detach disk, set index = swap mounted image).

---

## Saves And Writable Media

Initial policy:

- Load content read-only (`kATMediaWriteMode_RO`).
- Do not write modified disk images back automatically.
- Add a later option `altirra_media_write_mode = read_only | write_back |
  shadow_copy`.

Persistent generated files (NVRAM, printer output, shadow-copy writable disks,
per-content settings) go under RetroArch's **save directory**, never the
frontend's executable directory and never a stray `.ini` in CWD.

---

## Memory Access And Achievements

Initial: `retro_get_memory_data/​size` return `nullptr`/`0`.

Later:

- Expose system RAM via `RETRO_MEMORY_SYSTEM_RAM` only where a stable contiguous
  base exists for the current machine.
- Add `RETRO_ENVIRONMENT_SET_MEMORY_MAPS` for richer layouts if achievements
  need them.
- Do **not** misrepresent banked RAM, extended RAM, cartridge windows, or
  hardware registers as a single flat 64K range (achievements would be wrong).
  Trace the address map to Altirra's memory manager before exposing anything.

---

## `.info` File

`src/AltirraLibretro/altirra_libretro.info` (initial draft; flip
`is_experimental` to `false` only after video/audio/input/firmware/media/CI are
stable):

```text
# Software Information
display_name = "Atari - 8-bit / 5200 (Altirra)"
authors = "Avery Lee"
supported_extensions = "atr|xfd|atx|atz|dcm|pro|arc|bin|rom|car|a52|xex|exe|obx|com|bas|cas|wav|flac|ogg|sap|vgm|vgz|zip|gz|altstate|atstate2"
corename = "Altirra"
categories = "Emulator"
license = "GPLv2+"
permissions = ""

# Hardware Information
manufacturer = "Atari"
systemname = "Atari 8-bit Family"
systemid = "atari_8bit"
display_version = "<project version>"

# Libretro Features
database = "Atari - 8-bit Family|Atari - 5200"
supports_no_game = "true"
savestate = "false"          # → "true"/"deterministic" once milestone 6 lands
savestate_features = "null"
cheats = "false"
input_descriptors = "true"
memory_descriptors = "false"
libretro_saves = "false"
core_options = "true"
core_options_version = "2.0"
load_subsystem = "false"
hw_render = "false"
needs_fullpath = "true"
disk_control = "false"       # → "true" at milestone 7
is_experimental = "true"
needs_kbd_mouse_focus = "true"

# BIOS/Firmware (all optional — Altirra has embedded fallbacks where licensing allows)
firmware_count = 7
firmware0_desc = "5200.rom (Atari 5200 BIOS)"
firmware0_path = "5200.rom"
firmware0_opt  = "true"
firmware1_desc = "ATARIBAS.ROM (Atari BASIC)"
firmware1_path = "ATARIBAS.ROM"
firmware1_opt  = "true"
firmware2_desc = "ATARIOSA.ROM (Atari 400/800 OS-A)"
firmware2_path = "ATARIOSA.ROM"
firmware2_opt  = "true"
firmware3_desc = "ATARIOSB.ROM (Atari 400/800 OS-B)"
firmware3_path = "ATARIOSB.ROM"
firmware3_opt  = "true"
firmware4_desc = "ATARIXL.ROM (Atari XL/XE OS)"
firmware4_path = "ATARIXL.ROM"
firmware4_opt  = "true"
firmware5_desc = "ATARIOSC.ROM (Atari XL/XE OS variant)"
firmware5_path = "ATARIOSC.ROM"
firmware5_opt  = "true"
firmware6_desc = "XEGAME.ROM (XEGS Missile Command)"
firmware6_path = "XEGAME.ROM"
firmware6_opt  = "true"
```

> The first draft set `systemid = "atari_5200"`; the core covers the whole 8-bit
> family, so `atari_8bit` is the better primary id with 5200 as a secondary
> database entry.

---

## GitHub CI Workflow

Add `.github/workflows/libretro-core.yml`, modelled closely on the existing
`.github/workflows/bridge-package.yml` (same `ubuntu:22.04` container floor,
same Apple Clang / GCC-12 floors, same CMake≥3.24 PyPI-wheel trick, same arm64
native runner).

Triggers:

```yaml
on:
  push:
    branches: [main]
    tags: ['v*']
  pull_request:
    branches: [main]
    paths:
      - 'src/AltirraLibretro/**'
      - 'src/AltirraSDL/cmake/altirra_core_sources.cmake'
      - 'src/AltirraBridgeServer/**'
      - 'src/AltirraSDL/stubs/**'
      - 'src/ATAudio/**'
      - 'src/system/**'
      - '.github/workflows/libretro-core.yml'
  workflow_dispatch:
```

Matrix:

| Job | Runner | Notes |
| --- | --- | --- |
| Linux x86_64 | `ubuntu-latest` → `ubuntu:22.04` container | glibc 2.35 / gcc-12 floor |
| Linux aarch64 | `ubuntu-22.04-arm` → `ubuntu:22.04` container | native arm64 |
| Windows x86_64 | `windows-latest` | MSVC / Ninja |
| macOS arm64 | `macos-14`+ | deployment target 11.0 |

Configure (Linux example, SDL-free target):

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DALTIRRA_LIBRETRO=ON \
  -DALTIRRA_SDL3=OFF \
  -DALTIRRA_LIBRETRO_NO_SDL3=ON
cmake --build build --target AltirraLibretro -j"$(nproc)"
```

> Until the SDL3-replacement TUs (Milestone 1.5) are in place, drop
> `-DALTIRRA_LIBRETRO_NO_SDL3=ON`, build with SDL3 present, and keep the
> "no libSDL3" check as a **warning** (see Smoke Testing). After Milestone 1.5
> it becomes a hard gate.

Package contents:

```text
altirra_libretro.{so,dll,dylib}
altirra_libretro.info
README.md
COPYING            # GPLv2+
```

Artifacts / prerelease: publish to a separate `nightly-libretro` rolling
prerelease; do not attach to `nightly` or `nightly-bridge` unless explicitly
desired.

```text
AltirraLibretro-nightly-linux-x86_64.tar.gz
AltirraLibretro-nightly-linux-aarch64.tar.gz
AltirraLibretro-nightly-windows-x86_64.zip
AltirraLibretro-nightly-macos-arm64.zip
```

---

## CI Smoke Testing

Build-time checks:

- Artifact exists and has no `lib` prefix (`altirra_libretro.so`).
- Exported symbols include `retro_init`, `retro_run`, `retro_load_game`,
  `retro_get_system_info`, `retro_get_system_av_info`, `retro_serialize_size`
  (`nm -D` / `llvm-objdump -T` / `dumpbin /EXPORTS`).
- **No unexpected shared deps:**
  - Linux: `ldd altirra_libretro.so` — assert no `libSDL3`. (Warning before
    Milestone 1.5; hard fail after.)
  - macOS: `otool -L altirra_libretro.dylib` — assert no SDL3.
  - Windows: `dumpbin /DEPENDENTS` (or `llvm-objdump -p`) — assert no `SDL3.dll`.
- Also assert no `RETRO_API`-external symbols leak besides `retro_*` (visibility
  check) on ELF.

Runtime smoke test — build a tiny `libretro-smoke` host
(`src/AltirraLibretro/tests/`) that `dlopen`/`LoadLibrary`s the core, sets all
callbacks, calls `retro_init` + `retro_get_system_info` +
`retro_get_system_av_info`, optionally `retro_load_game` with a small
repo-authored / GPL-compatible test XEX, runs ~60 `retro_run` frames, and
asserts: ≥1 non-null video frame with stable nonzero dimensions, nonzero audio
frames, and `retro_serialize_size` non-increasing across the session. A local
host is more deterministic than installing RetroArch in CI. **Never** include
copyrighted Atari ROMs or commercial content in tests.

---

## Implementation Milestones

### Milestone 0 — Header & skeleton
Add `src/AltirraLibretro/`, vendor `libretro.h`, add the CMake target and
`ALTIRRA_LIBRETRO` option, export the full `retro_*` ABI, return static system
info, build shared libraries on all three desktop platforms.
**Exit:** local + CI builds produce the artifact; export-symbol check passes.

### Milestone 1 — Headless boot & video
Port the BridgeServer startup helpers, install the (SDL-free fork of the) null
display, handle both content-by-path **and** no-content boot
(`retro_load_game(NULL)`, `supports_no_game = true`), implement `retro_run`
frame stepping, submit XRGB8888 via the copied `CaptureXrgb`.
**Exit:** a free/public-domain Atari image boots; the core also boots a bare
machine with no content; video has stable nonzero dimensions; no sleeps/SDL
event loops in the path.

### Milestone 1.5 — SDL3 elimination (additive, zero-regression)
Add `libretro_time.cpp` (std::chrono ticks + `VDCallbackTimer`/`VDLazyTimer`) and
`libretro_dirs.cpp` (`ATGetConfigDir`/registry/firmware dirs from RetroArch),
each defining the **complete** symbol set of the `system` object it shadows
(`time_sdl3.o` / `configdir_sdl3.o` / `registry_sdl3.o`), linked **before**
`system`. Gate the top-level CMake so `-DALTIRRA_LIBRETRO_NO_SDL3=ON` neither
fetches nor links SDL3. Do **not** edit `system`.
**Exit:** `ldd`/`otool`/`dumpbin` show no SDL3 for the standalone libretro
build; the "no libSDL3" CI check is a hard gate; the SDL3 frontend, BridgeServer,
Android, and WASM builds are byte-for-byte unaffected (verify each still
configures/links).

### Milestone 2 — Audio
Implement the full `IATAudioOutput`/`IATAudioMixer` backend with Altirra's
resampler, route all sources through `audio_batch_cb`.
**Exit:** nonzero audio frames during emulation; POKEY + cassette/drive sounds
audible; no SDL3 audio dependency.

### Milestone 3 — Basic input
Default input maps + RetroPad→joystick port 1, console switches, input
descriptors, controller-port registration.
**Exit:** joystick games respond to D-pad/fire; console switches work; RetroArch
remap UI shows Atari labels.

### Milestone 4 — Firmware & options
RetroArch system/save dir wiring, firmware lookup, core options V2 with
machine/memory/video/BASIC/SIO applied at the right time.
**Exit:** a fresh RetroArch config finds firmware in the documented dir;
per-game options change effective Altirra hardware.

### Milestone 5 — Keyboard & 5200
Keyboard callback + polling mapping, 5200 controller/keypad, controller variants
(joystick / keyboard / 5200 / paddle).
**Exit:** keyboard-heavy 8-bit software is usable; 5200 keypad games are usable.

### Milestone 6 — Save states
Fixed-max wrapper over `.altstate2` blobs; verify across gameplay/reset/disk/
cartridge/5200; enable rewind.
**Exit:** `retro_serialize_size()` nonzero and non-increasing; RetroArch
save/load + rewind work.

### Milestone 7 — Disk control & M3U
Extended disk-control interface + `.m3u`, drive index switching, eject/insert.
**Exit:** multi-disk software switches disks from RetroArch's UI; `.m3u`
resolves relative paths.

### Milestone 8 — Polish & upstream readiness
`is_experimental = "false"`; user docs; firmware/writable-media compatibility
notes; consider submitting info/build metadata upstream to libretro.

---

## Risks And Mitigations

| Risk | Mitigation |
| --- | --- |
| **`system` pulls libSDL3 on non-Windows** (the main risk) | Milestone 1.5: replace the *full* symbol set of `time_sdl3`/`configdir_sdl3`/`registry_sdl3`; gate CMake to skip SDL3. `--as-needed` alone is insufficient (symbols are referenced). |
| Accidental SDL3/ImGui dependency creep | Base on BridgeServer, never pull `AltirraSDL` frontend sources, inspect imports in CI, forbid `<SDL3/...>`/`imgui.h` includes in libretro TUs. |
| Audio backend drops non-POKEY sources | Keep the real `IATAudioMixer`/resampler; test cassette/drive/monitor paths; do not fake `IATAudioOutput`. |
| Input does nothing (no maps loaded) | Create default input maps programmatically (or include Input/InputMaps categories); do not copy BridgeServer's input-category exclusion verbatim. |
| Wrong frame geometry constants | Measure `GetLastFrameBuffer()` across all standards × artifacting × overscan before setting `base_*`/`max_*`; never ship guessed numbers. |
| Save-state size grows after load | Start disabled; later use a fixed-max wrapper and assert payload ≤ max. |
| Hardware constants guessed | Trace every keyboard/switch/paddle/5200 value to existing Altirra code (input codes/triggers verified above). |
| Firmware discovery conflicts with Altirra settings | Override firmware paths from RetroArch's system dir before `LoadROMs()`. |
| Geometry/standard change desync | Detect changes per frame; `SET_GEOMETRY` for crop/aspect, `SET_SYSTEM_AV_INFO` for fps/rate. |
| Writable media corrupts user content | Read-only by default; explicit write-mode option later. |
| Core options diverge from Windows defaults | Use `settings.cpp`/`cmd*.cpp`/`commands_sdl3.cpp` as canonical; never trust member-initialiser defaults. |
| `lib` prefix breaks RetroArch discovery | `set_target_properties(... PREFIX "")`; CI asserts the artifact name. |

---

## First Patch Set Recommendation

Keep the first PR intentionally narrow (no audio/input/disk/save-states):

1. Vendored `src/AltirraLibretro/libretro/libretro.h` (+ recorded SHA).
2. `src/AltirraLibretro/CMakeLists.txt` mirroring BridgeServer (link set, SIMD
   flags, MSVC `intrin.h`, visibility, `PREFIX ""`).
3. `libretro.cpp` skeleton implementing the **complete** exported ABI with
   static system info and stubbed run/load.
4. `libretro_video.cpp` — SDL-free null display forked from `ui_stubs.cpp`.
5. Top-level `ALTIRRA_LIBRETRO` option + `src/CMakeLists.txt` gating.
6. CI workflow that builds all platforms, checks exported symbols, and (warning
   only at this stage) reports SDL3 linkage.

Get the target shape, artifact name, and CI path correct before adding emulator
behaviour. Milestone 1 (boot+video) and Milestone 1.5 (SDL3 elimination) are the
next two PRs.
