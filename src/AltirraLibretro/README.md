# Altirra Libretro Core

This directory contains the RetroArch/libretro adapter for Altirra.

The target builds a shared library named `altirra_libretro` with no platform
library prefix:

- Linux: `altirra_libretro.so`
- Windows: `altirra_libretro.dll`
- macOS: `altirra_libretro.dylib`

Build it with:

```sh
./build.sh --libretro
```

Build and run the libretro smoke tests with:

```sh
./build.sh --libretro --libretro-test
```

The smoke tests cover no-content boot, disk control, option changes, geometry,
audio, input, virtual-keyboard overlay display, achievement memory exposure,
cheat POKEs, disk sidecar save-back and reload, `.a52` content-aware 5200
loading, the exported `retro_reset()` path, keyboard/controller reset bindings,
save states, metadata capability flags, and a post-shutdown
`retro_get_system_info()` query.
The build script also validates the `.info` metadata and verifies the built
core artifact before reporting success.

## Controls

The core is usable from a controller-only RetroArch device.

Default Atari 8-bit RetroPad controls:

| RetroPad | Atari input |
|---|---|
| D-pad | Joystick |
| Left analog | Joystick |
| B | Trigger |
| A | Second trigger |
| Y | Space |
| X | Return |
| Start | START console key |
| Select | SELECT console key |
| L | OPTION console key |
| R or L3 | Toggle virtual keyboard |
| L2 | Escape |
| R2 | Return |
| R3 | Unassigned by default |
| Select+R2 | Toggle virtual keyboard fallback for stickless pads |
| Select+Start | Warm reset |
| Select+L | Cold reset |

The face/shoulder key bindings are concurrent with the joystick: holding a
direction while pressing `Y`, `X`, `L2`, `R2`, `L3`, or `R3` keeps joystick
input active and also sends the mapped Atari key. If a stick-click is also
selected as the virtual-keyboard toggle, the toggle takes precedence.

RetroArch remaps decide which physical controller button becomes each RetroPad
button. The Altirra core options then decide what emulator-side input those
RetroPad buttons send. For example, an Xbox controller can use the left
stick/D-pad as the Atari joystick, map physical `A` to the RetroPad trigger in
RetroArch, and use the Altirra `RetroPad Y/X/L2/R2/L3/R3 Emulator Input`
options for Return, `T`, `B`, or other Atari computer keys.

Use the `RetroPad Extra Button Scheme` core option to switch the spare-button
defaults between auto, common keys, joystick-only, flight/space-sim keys,
keyboard-heavy/adventure keys, and 5200. The default `Auto` scheme uses common
Atari 8-bit keys unless the active system is 5200, where spare keyboard
bindings stay unassigned and the 5200 keypad is available from the virtual
keyboard. The `RetroPad Y/X/L2/R2/L3/R3 Emulator Input` core options can
override each spare button individually with Atari computer letters, digits,
Return, Space, Escape, Backspace, Tab, or explicit 5200 keypad/control targets.
Atari computer key entries apply to Atari 8-bit systems; explicit 5200 entries
apply to 5200 mode. `Auto` follows the selected scheme.
The `Virtual Keyboard Toggle`, `RetroPad Warm Reset Combo`, and `RetroPad Cold
Reset Combo` options can rebind or disable the controller-only system actions
while keeping the handheld-safe defaults above.
Physical keyboard console-key shortcuts default to disabled because RetroArch
uses function keys such as F2/F3/F4 for frontend hotkeys. If you want physical
keyboard START/SELECT/OPTION shortcuts, enable Game Focus or adjust RetroArch's
hotkeys, then set the `Physical Keyboard START/SELECT/OPTION Key` core options.

While the virtual keyboard is open, the pad controls the keyboard instead of
the joystick so no joystick direction or console key can stick:

| RetroPad | Virtual keyboard action |
|---|---|
| D-pad | Move selection |
| B | Press selected key |
| A | Close keyboard |
| X | Toggle alpha/console page |
| Y | Toggle 5200 keypad page in 5200 mode |
| R, L3, or Select+R2 | Close keyboard |

The virtual keyboard includes letters, numbers, Shift, Ctrl, Escape, Space,
Return, Backspace, cursor/function keys, START/SELECT/OPTION, warm/cold reset,
and a 5200 keypad page with `0`-`9`, `*`, `#`, START, PAUSE, and RESET.

The `System` core option defaults to `Auto`: `.a52` cartridges and headered
5200 cartridge images start as Atari 5200 content, while other extensions start
as an Atari 800XL. Ambiguous raw `.bin`/`.rom` images are not guessed as 5200
unless their cartridge header proves it. Selecting a specific system overrides
auto-detection. `Input Port 1 Device` also defaults to `Auto`, using a 5200
controller for the default RetroPad when the active system is 5200 and a
joystick otherwise; explicit RetroArch device selections such as paddle, mouse,
and light gun remain honored.

The `Aspect Ratio` core option defaults to `4:3`. `Pixel Perfect` and
`Square Pixels` report the rendered frame's native pixel ratio to RetroArch;
`NTSC PAR` reports `3:2`, and `PAL PAR` reports `7:5`.

The `Performance Tier` option defaults to `Quality`. `Performance` keeps
emulation behavior unchanged but chooses lighter default presentation settings:
automatic artifacting resolves to none, and `Audio Filters = Auto` disables
the audio filter chain. Explicit artifacting and audio-filter choices override
the tier.

RetroArch achievements can read the core's CPU-visible 64K system memory via
`RETRO_MEMORY_SYSTEM_RAM` and the published memory descriptor map. RetroArch
cheats support POKE-style byte writes such as `POKE 1536,123`, `$0600:$7B`, or
`0x0600=0x7B`; enabled cheats are applied each frame.

Disk images mount in virtual read/write mode by default. The original content
file is not modified; if a disk is changed, the core saves a sidecar disk image
under RetroArch's save directory in `Altirra/saves/` and reloads that sidecar on
the next launch. The `Disk Write Mode` core option can be changed from the
default `Safe Sidecar` mode to `Write Original` when you explicitly want writes
to go through to the loaded disk image.

The core also exposes a `Cartridge + Disk` subsystem for RetroArch frontends
that support multi-file loading. The first file is a cartridge/program image
and the second file is a disk image or `.m3u`; the disk uses the same sidecar
save-back path as normal disk loading.

Runtime disk-control replacement accepts disk images and `.m3u` playlists. A
playlist replacement expands into its listed disk images, while non-disk
content is rejected immediately instead of failing later when the tray closes.

Libretro-style automation can also call the thin repository-root wrapper:

```sh
make -f Makefile.libretro
make -f Makefile.libretro verify
make -f Makefile.libretro test
```

This wrapper delegates to CMake and must stay thin; the CMake target is the
only real build definition for the core.

For the Flathub/Flatpak RetroArch build, do not distribute a core built on the
host system. Build inside the matching Flatpak SDK so the shared library links
against the same glibc/libstdc++ ABI that RetroArch will load:

```sh
flatpak install flathub org.kde.Sdk//6.10
./build.sh --libretro-flatpak
```

Use `./build.sh --libretro-flatpak --package` to create a package named with a
`flatpak-kde610` suffix. This artifact is intended for RetroArch Flatpak users;
keep it separate from the generic host-native Linux libretro package.

Packaged builds include the core, `altirra_libretro.info`, this README, the
installer helper, build provenance, and the GPLv2 license text.

Use the build that matches the RetroArch distribution:

- Flathub/Flatpak RetroArch: `./build.sh --libretro-flatpak --package`
- Locally installed RetroArch on the same or newer Linux distribution:
  `./build.sh --libretro --package`
- Cross-distro Linux distribution: build in the oldest Linux runtime/sysroot
  you intend to support, then package that output separately.

Before shipping a Linux core, check the required symbol versions:

```sh
readelf --version-info altirra_libretro.so | grep -E 'GLIBC_|GLIBCXX_'
```

The Flatpak build script rejects outputs that require a glibc newer than the
RetroArch Flatpak runtime. A host-native Linux build does not, because it is
intended for local/native use and may legitimately depend on the host ABI.

GitHub Actions packages the standalone core for:

- Linux x86_64
- Linux aarch64
- Linux ARMv7 hard-float (`armv7hf`)
- Android ARM64 (`arm64-v8a`)
- Android ARMv7 (`armeabi-v7a`)
- macOS arm64 and x86_64
- Windows x86_64 and ARM64

Local cross-build presets are also available:

```sh
# Requires a GCC 12+ cross toolchain (the shared headers use C++23
# `if consteval`), e.g. gcc-12-arm-linux-gnueabihf /
# g++-12-arm-linux-gnueabihf on Ubuntu 22.04.
cmake --preset linux-libretro-armv7hf
cmake --build --preset linux-libretro-armv7hf

# Requires ANDROID_NDK_HOME to point at an Android NDK.
cmake --preset android-libretro-arm64
cmake --build --preset android-libretro-arm64

cmake --preset android-libretro-armv7a
cmake --build --preset android-libretro-armv7a
```

Android `armv7hf` Linux builds are not compatible with Android. Use the
`armeabi-v7a` package for 32-bit Android RetroArch and the `arm64-v8a` package
for 64-bit Android RetroArch. The CI verifier checks the Android artifacts'
ELF identity explicitly: `armeabi-v7a` must be `ELF32`/`ARM`, and `arm64-v8a`
must be `ELF64`/`AArch64`.

The build output directory contains both files RetroArch needs:

- `altirra_libretro.so` / `altirra_libretro.dll` / `altirra_libretro.dylib`
- `altirra_libretro.info`

Validate the source `.info` before copying it to Libretro infrastructure:

```sh
bash scripts/validate-libretro-info.sh
```

The validator intentionally requires `is_experimental = "true"` by default.
For an intentional promotion after real RetroArch testing, run it with
`ALTIRRA_LIBRETRO_ALLOW_NON_EXPERIMENTAL=1`.

Verify a built core and its sidecar `.info`:

```sh
bash scripts/verify-libretro-artifact.sh \
  build/linux-libretro/src/AltirraLibretro/altirra_libretro.so \
  build/linux-libretro/src/AltirraLibretro/altirra_libretro.info
```

For cross-built ELF packages, set `ALTIRRA_LIBRETRO_EXPECT_ELF_CLASS` and
`ALTIRRA_LIBRETRO_EXPECT_ELF_MACHINE` to require an exact `readelf -h` match.

On x86_64 ELF builds, the artifact verifier also rejects the known-bad
`retro_get_system_info()` code shape where the compiler packs metadata
pointers through an RIP-relative XMM table load. That optimization has produced
invalid `library_name` pointers during RetroArch shutdown, so keep the
implementation's named static strings and noinline accessors unless the
shutdown regression is re-tested with the verifier updated accordingly.

Verify a package archive:

```sh
bash scripts/verify-libretro-package.sh \
  build/linux-libretro/AltirraLibretro-4.40-linux-x86_64.tar.gz
```

The package verifier also runs the packaged installer in `--dry-run` mode
against a temporary RetroArch config, so installer option regressions are
caught before a package is handed to testers.

Run the package in a real RetroArch frontend with an isolated config:

```sh
bash scripts/run-libretro-retroarch-smoke.sh \
  --package build/linux-libretro/AltirraLibretro-4.40-linux-x86_64.tar.gz \
  --verify-package
```

For Flathub/Flatpak RetroArch, use a package built with
`./build.sh --libretro-flatpak --package` and pass `--retroarch flatpak`. The
smoke runner installs the selected core and `.info` under
`build/libretro-smoke/`, launches RetroArch with `--max-frames`, and checks
the log for core load, no-content support, disk-control registration, geometry
initialization, content handoff, content-specific save paths, and clean unload.
By default it runs no-content plus generated `.xex`, `.atr`, `.a52`, `.cas`,
`.m3u`, and `.zip` fixtures; pass `--no-content-only` to restrict it to the
frontend no-content path. The default smoke uses RetroArch's `null` video/input
drivers for deterministic automation; pass explicit drivers such as
`--video-driver gl --input-driver udev` when intentionally checking a visible
desktop session. This is a frontend smoke test, not a replacement for trying
the package like a user on the devices/controllers you care about.

Install the shared library into RetroArch's cores directory and install
`altirra_libretro.info` into RetroArch's Core Info directory. If the `.info`
file is missing or in the wrong directory, RetroArch can still load the shared
library directly, but it will show the filename instead of the configured
display name (`Atari - 400/800/600XL/800XL/130XE/5200 (Altirra)`) and its
content browser may not filter for Atari extensions.

RetroArch's `Install or Restore a Core` action may import only the shared
library. It does not reliably install a sidecar `.info` file from the same
directory. For packaged builds, run:

```sh
./install-retroarch.sh
```

On the Flatpak RetroArch build, check the active paths in
`~/.var/app/org.libretro.RetroArch/config/retroarch/retroarch.cfg`:

```ini
libretro_directory = "..."
libretro_info_path = "..."
```

Logs are written under the configured `logs` directory only when RetroArch
logging is enabled.

Before submitting the core to Libretro infrastructure, follow the local
checklist in `docs/libretro-upstream.md`. After the checks pass,
`bash scripts/prepare-libretro-upstream.sh` stages the `.info` and docs draft
in a local directory matching the Libretro repository layouts.

A practical release-candidate pass is:

```sh
bash scripts/run-libretro-retroarch-smoke.sh \
  --package build/linux-libretro/AltirraLibretro-4.40-linux-x86_64.tar.gz \
  --verify-package
```

Then install the same package in RetroArch, test the main content types and
controller-only controls, and keep short notes with the RetroArch version,
device, controller, package path, and pass/fail issues. That is enough for
project-side promotion decisions; no formal generated report is required.

`libretro/libretro.h` is a vendored subset of the canonical libretro ABI header
covering only the interfaces used by this adapter:

https://github.com/libretro/libretro-common/blob/master/include/libretro.h

The subset was checked against the upstream `master` header during this
implementation pass on 2026-06-23. The local browser/API path did not expose a
verifiable commit SHA, so no SHA is recorded here. When refreshing this file,
fetch the canonical header from upstream, record the exact commit SHA in this
README, copy only the required ABI declarations/constants, and keep the MIT
license notice in the header comment.

After refreshing `libretro.h`, rebuild the standalone core, run the smoke test,
and verify the exported symbol list remains limited to `retro_*`.
