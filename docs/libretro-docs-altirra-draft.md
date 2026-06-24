# Atari - 400/800/600XL/800XL/130XE/5200 (Altirra)

This is a local draft for the future `libretro/docs` core page. Copy it to
`docs/library/altirra.md` in the Libretro docs repository only after the core
has passed the readiness gates in `docs/libretro-upstream.md`.

## Background

Altirra is an Atari 8-bit family and Atari 5200 emulator originally authored by
Avery Lee. This libretro core adapts the Altirra emulator core for use in
RetroArch and other libretro frontends.

The Altirra core has been authored by:

- Jakub 'ilmenit' Debski
- Fork of Altirra by Avery Lee

The Altirra core is licensed under:

- GPLv2+

## Requirements

The core is distributed as a libretro dynamic library:

- Linux: `altirra_libretro.so`
- macOS: `altirra_libretro.dylib`
- Windows: `altirra_libretro.dll`

The matching `altirra_libretro.info` file must be installed into RetroArch's
Core Info directory for the core name, supported extensions, firmware list, and
feature metadata to show correctly.

## How To Start The Altirra Core

1. Install `altirra_libretro` into RetroArch's cores directory.
2. Install `altirra_libretro.info` into RetroArch's Core Info directory.
3. In RetroArch, select Load Core, then Altirra.
4. Select Load Content and choose supported Atari content, or select Start Core
   for no-content boot.

## BIOS

Altirra can use optional firmware files from RetroArch's system directory.
Place these files directly in the frontend system directory unless the frontend
configuration documents a different system path.

| Filename | Description | Required |
| --- | --- | --- |
| `5200.rom` | Atari 5200 BIOS | No |
| `ATARIBAS.ROM` | Atari BASIC | No |
| `ATARIOSA.ROM` | Atari 400/800 OS-A | No |
| `ATARIOSB.ROM` | Atari 400/800 OS-B | No |
| `ATARIXL.ROM` | Atari XL/XE OS | No |
| `ATARIOSC.ROM` | Atari XL/XE OS variant | No |
| `XEGAME.ROM` | XEGS Missile Command | No |

## Extensions

Content that can be loaded by the Altirra core has the following file
extensions:

- `.atr`
- `.xfd`
- `.atx`
- `.atz`
- `.dcm`
- `.pro`
- `.arc`
- `.bin`
- `.rom`
- `.car`
- `.a52`
- `.xex`
- `.exe`
- `.obx`
- `.com`
- `.bas`
- `.cas`
- `.wav`
- `.flac`
- `.ogg`
- `.sap`
- `.vgm`
- `.vgz`
- `.zip`
- `.gz`
- `.altstate`
- `.atstate2`
- `.m3u`

RetroArch databases associated with the Altirra core:

- Atari - 8-bit Family
- Atari - 5200

Do not create an Altirra-specific game database. The game database belongs to
the platform/content identity, not to this specific emulator core.

## Features

| Feature | Supported |
| --- | --- |
| Restart | Yes |
| Saves | No |
| States | Yes |
| Rewind | Yes, via serialization |
| Netplay | Not currently documented for this core |
| Core Options | Yes |
| RetroAchievements | No |
| RetroArch Cheats | No |
| Native Cheats | No |
| Controls | Yes |
| Remapping | Yes |
| Multi-Mouse | No |
| Rumble | No |
| Sensors | No |
| Camera | No |
| Location | No |
| Subsystem | No |
| Softpatching | No, because the core requires full paths |
| Disk Control | Yes |
| Username | No |
| Language | No |
| Crop Overscan | Yes |
| LEDs | No |

## Directories

The Altirra core's library name is `Altirra`.

The Altirra core requests these frontend directories:

| Frontend directory | Use |
| --- | --- |
| System directory | Optional Atari firmware lookup |
| Save directory | Core settings and generated save data under `Altirra` |
| State directory | RetroArch save states |
| Content directory | Loaded full-path content and related media |

## Geometry And Timing

Geometry and timing can change with video-standard and display-related core
options.

Typical values:

- Core-provided sample rate: 48000 Hz
- Core-provided aspect ratio: 4:3
- PAL timing: about 49.86 FPS
- NTSC timing: about 59.92 FPS
- Observed maximum geometry in smoke tests: 912x624

## Core Options

The core provides libretro core options for hardware, video, audio, media,
and input configuration.

| Option | Values | Default |
| --- | --- | --- |
| System | 800, 800xl, 1200xl, 130xe, xegs, 5200 | 800xl |
| Memory Size | 8K, 16K, 24K, 32K, 40K, 48K, 52K, 64K, 128K, 256K, 320K, 320K_Compy, 576K, 576K_Compy, 1088K | 320K |
| Video Standard | ntsc, pal, secam, ntsc50, pal60 | pal |
| BASIC | disabled, enabled | disabled |
| CPU | 6502c, 65c02, 65c816_7mhz, 65c816_21mhz | 6502c |
| Illegal Instructions | enabled, disabled | enabled |
| Randomize Launch Delay | enabled, disabled | enabled |
| Randomize EXE Memory | disabled, enabled | disabled |
| Stereo POKEY | disabled, enabled | disabled |
| VideoBoard XE (VBXE) | disabled, enabled | disabled |
| Covox | disabled, enabled | disabled |
| SoundBoard | disabled, enabled | disabled |
| Rapidus Accelerator | disabled, enabled | disabled |
| SIO Patch | off, disk, cassette, disk_and_cassette | disk_and_cassette |
| Artifacting | none, ntsc, ntschi, pal, palhi, auto | auto |
| Crop Overscan | normal, off, extended, full | normal |
| Audio Filters | enabled, disabled | enabled |
| Downmix Stereo to Mono | disabled, enabled | disabled |
| Drive Sounds | disabled, enabled | disabled |
| Input Port 1 | joystick, 5200_controller, paddle_a, paddle_b, st_mouse, light_pen, light_gun, none | joystick |
| Input Port 2 | none, joystick, paddle_a, paddle_b, st_mouse | none |

## Controls

The core supports RetroPad, analog input, keyboard input, mouse input, and
light-gun style input paths.

Controller types exposed to RetroArch:

- Atari Joystick
- Atari Paddle
- Atari ST Mouse
- Atari Light Gun/Pen
- None

Registered input descriptors:

| Device | Inputs |
| --- | --- |
| RetroPad port 1 | Joystick Up, Joystick Down, Joystick Left, Joystick Right, Trigger, START, SELECT, OPTION |
| RetroPad port 2 | Joystick 2 Up, Joystick 2 Down, Joystick 2 Left, Joystick 2 Right, Joystick 2 Trigger |
| Analog | Paddle Knob, Paddle Trigger, Paddle 2 Knob, Paddle 2 Trigger |
| Mouse | Mouse X, Mouse Y, Mouse Left Button, Mouse Right Button, Mouse 2 X, Mouse 2 Y, Mouse 2 Left Button, Mouse 2 Right Button |
| Light Gun | Light Gun X, Light Gun Y, Light Gun Trigger |
| Pointer | Pointer X, Pointer Y, Pointer Pressed |

## Notes For Upstream Submission

Before this draft is copied to `libretro/docs`:

1. Confirm the core has been accepted into `libretro-super/dist/info`.
2. Replace any "not currently documented" entries with final support status.
3. Confirm the core option and control tables still match the accepted build.
4. Add the page to `mkdocs.yml`, `docs/guides/core-list.md`,
   `docs/development/licenses.md`, and `docs/library/bios.md`.
