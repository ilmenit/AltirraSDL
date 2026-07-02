# Atari - 400/800/600XL/800XL/130XE/5200 (Altirra)

This is a local draft for the future `libretro/docs` core page. Copy it to
`docs/library/altirra.md` in the Libretro docs repository only after the core
has passed the release checks in `docs/libretro-upstream.md`.

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
| Saves | Yes, through libretro save directory sidecars |
| States | Yes |
| Rewind | Yes, via serialization |
| Netplay | Not currently documented for this core |
| Core Options | Yes |
| RetroAchievements | Yes |
| RetroArch Cheats | Yes, POKE-style memory patches |
| Native Cheats | No |
| Controls | Yes |
| Remapping | Yes |
| Multi-Mouse | No |
| Rumble | No |
| Sensors | No |
| Camera | No |
| Location | No |
| Subsystem | Yes, cartridge/program plus disk |
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
| Save directory | Core settings and generated writable disk sidecars under `Altirra` |
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
| System | auto, 800xl, 800, 1200xl, 130xe, xegs, 5200 | auto |
| Memory Size | 8K, 16K, 24K, 32K, 40K, 48K, 52K, 64K, 128K, 256K, 320K, 320K_Compy, 576K, 576K_Compy, 1088K | 320K |
| Video Standard | auto, ntsc, pal, secam, ntsc50, pal60 | auto |
| BASIC | disabled, enabled | disabled |
| OS Firmware | auto, internal | auto |
| BASIC Firmware | auto, internal | auto |
| 5200 BIOS | auto | auto |
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
| Disk Write Mode | safe_sidecar, original_rw | safe_sidecar |
| Raw Cartridge Mapper | auto, 2k, 4k, 8k, 16k, xegs_32k, xegs_64k, xegs_128k, xegs_256k, xegs_512k, xegs_1m, maxflash_128k, maxflash_1m, megacart_128k, megacart_512k, megacart_1m, 5200_4k, 5200_8k, 5200_16k, 5200_32k, oss_034m, oss_m091, williams_32k, williams_64k, db_32k, atrax_128k, sic_128k, sic_256k, blizzard_16k, blizzard_32k | auto |
| Artifacting | none, ntsc, ntschi, pal, palhi, auto | auto |
| Performance Tier | quality, balanced, performance | quality |
| Crop Overscan | normal, off, extended, full | normal |
| Aspect Ratio | 4_3, pixel_perfect, square_pixels, ntsc_par, pal_par | 4_3 |
| Audio Filters | auto, enabled, disabled | auto |
| Downmix Stereo to Mono | disabled, enabled | disabled |
| Drive Sounds | disabled, enabled | disabled |
| Input Port 1 Device | auto, joystick, 5200_controller, paddle_a, paddle_b, st_mouse, light_pen, light_gun, none | auto |
| Input Port 2 Device | none, joystick, paddle_a, paddle_b, st_mouse | none |
| RetroPad Extra Button Scheme | auto, common, joystick, flight, adventure, 5200 | auto |
| Virtual Keyboard Toggle | r_l3_select_r2, r, l3, r3, select_r2, none | r_l3_select_r2 |
| RetroPad Warm Reset Combo | select_start, select_r, start_r, none | select_start |
| RetroPad Cold Reset Combo | select_l, select_l2, select_r, none | select_l |
| RetroPad Y Emulator Input | auto, none, space, return, escape, backspace, tab, help, break, inverse, clear, warm_reset, cold_reset, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u, v, w, x, y, z, 5200_0, 5200_1, 5200_2, 5200_3, 5200_4, 5200_5, 5200_6, 5200_7, 5200_8, 5200_9, 5200_star, 5200_pound, 5200_start, 5200_pause, 5200_reset | auto |
| RetroPad X Emulator Input | auto, none, space, return, escape, backspace, tab, help, break, inverse, clear, warm_reset, cold_reset, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u, v, w, x, y, z, 5200_0, 5200_1, 5200_2, 5200_3, 5200_4, 5200_5, 5200_6, 5200_7, 5200_8, 5200_9, 5200_star, 5200_pound, 5200_start, 5200_pause, 5200_reset | auto |
| RetroPad L2 Emulator Input | auto, none, space, return, escape, backspace, tab, help, break, inverse, clear, warm_reset, cold_reset, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u, v, w, x, y, z, 5200_0, 5200_1, 5200_2, 5200_3, 5200_4, 5200_5, 5200_6, 5200_7, 5200_8, 5200_9, 5200_star, 5200_pound, 5200_start, 5200_pause, 5200_reset | auto |
| RetroPad R2 Emulator Input | auto, none, space, return, escape, backspace, tab, help, break, inverse, clear, warm_reset, cold_reset, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u, v, w, x, y, z, 5200_0, 5200_1, 5200_2, 5200_3, 5200_4, 5200_5, 5200_6, 5200_7, 5200_8, 5200_9, 5200_star, 5200_pound, 5200_start, 5200_pause, 5200_reset | auto |
| RetroPad L3 Emulator Input | auto, none, space, return, escape, backspace, tab, help, break, inverse, clear, warm_reset, cold_reset, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u, v, w, x, y, z, 5200_0, 5200_1, 5200_2, 5200_3, 5200_4, 5200_5, 5200_6, 5200_7, 5200_8, 5200_9, 5200_star, 5200_pound, 5200_start, 5200_pause, 5200_reset | auto |
| RetroPad R3 Emulator Input | auto, none, space, return, escape, backspace, tab, help, break, inverse, clear, warm_reset, cold_reset, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u, v, w, x, y, z, 5200_0, 5200_1, 5200_2, 5200_3, 5200_4, 5200_5, 5200_6, 5200_7, 5200_8, 5200_9, 5200_star, 5200_pound, 5200_start, 5200_pause, 5200_reset | auto |
| Physical Keyboard START Key | none, f2, f3, f4, f5, f6, f8, f9, f10 | none |
| Physical Keyboard SELECT Key | none, f2, f3, f4, f5, f6, f8, f9, f10 | none |
| Physical Keyboard OPTION Key | none, f2, f3, f4, f5, f6, f8, f9, f10 | none |

## Controls

The core supports RetroPad, left analog joystick movement, keyboard input,
mouse input, and light-gun style input paths. The virtual keyboard is available
from RetroPad R, RetroPad L3, or Select+R2 by default, and can be changed or
disabled with the Virtual Keyboard Toggle option.
Input Port 1 auto-selection uses a 5200 controller for the default RetroPad
when the active system is 5200, while explicit RetroArch paddle, mouse, and
light-gun device selections remain active.

Default RetroPad shortcuts:

- START, SELECT, and L map to Atari START, SELECT, and OPTION.
- Select+Start performs warm reset.
- Select+L performs cold reset on Atari 8-bit systems.
- Y, X, L2, R2, L3, and R3 send concurrent Atari keys while joystick input
  remains active. Their defaults depend on the RetroPad Extra Button Scheme
  option and can be overridden per button. Auto uses common Atari 8-bit keys,
  or the 5200 preset when the active system is 5200. If L3 or R3 is also
  selected as the virtual keyboard toggle, the toggle takes precedence.
- RetroArch remaps physical controller buttons to RetroPad buttons. The
  Altirra core options then map those RetroPad buttons to Atari computer keys
  including HELP, BREAK, Inverse/Fuji, and Clear; warm/cold reset actions; or
  explicit 5200 keypad/control targets.
- Atari computer key entries apply to Atari 8-bit systems. The explicit 5200
  entries apply to 5200 mode.
- Physical keyboard START/SELECT/OPTION shortcuts default to disabled because
  RetroArch uses common function keys such as F2/F3/F4 for frontend hotkeys.
  Enable Game Focus or adjust RetroArch hotkeys before binding those core
  options.

Controller types exposed to RetroArch:

- Atari Joystick
- Atari Paddle
- Atari ST Mouse
- Atari Light Gun/Pen
- None

Registered input descriptors are refreshed when the active control scheme or
content-aware 5200 mode changes. The rows below show the default Auto scheme
for Atari 8-bit content and the Auto scheme after 5200 content is detected.

| Device | Inputs |
| --- | --- |
| RetroPad port 1, Atari 8-bit Auto | Joystick Up, Joystick Down, Joystick Left, Joystick Right, Trigger, Space, Return, START, SELECT, OPTION, Virtual Keyboard, Esc, Return / VKBD Combo, Virtual Keyboard, Unassigned |
| RetroPad port 1, 5200 Auto | Joystick Up, Joystick Down, Joystick Left, Joystick Right, Trigger, Unassigned, Unassigned, START, SELECT, OPTION, Virtual Keyboard, Unassigned, VKBD Combo, Virtual Keyboard, Unassigned |
| RetroPad port 2 | Joystick 2 Up, Joystick 2 Down, Joystick 2 Left, Joystick 2 Right, Joystick 2 Trigger |
| Analog | Joystick Analog Y, Analog X / Paddle Knob, Paddle Trigger, Paddle 2 Knob, Paddle 2 Trigger |
| Mouse | Mouse X, Mouse Y, Mouse Left Button, Mouse Right Button, Mouse 2 X, Mouse 2 Y, Mouse 2 Left Button, Mouse 2 Right Button |
| Light Gun | Light Gun X, Light Gun Y, Light Gun Trigger |
| Pointer | Pointer X, Pointer Y, Pointer Pressed |

## Memory And Cheats

The core exposes a 64K system RAM memory descriptor for achievements and
external tooling. RetroArch cheats are accepted as byte POKEs, for example
`$0600:$7B`, `0x0600=0x7B`, or `POKE 1536,123`.

## Subsystems

The core exposes a `Cartridge + Disk` subsystem for content that needs a
cartridge or directly loaded program together with a disk image. The first
subsystem slot accepts cartridge/program content and the second slot accepts a
disk image or `.m3u` playlist.

## Notes For Upstream Submission

Before this draft is copied to `libretro/docs`:

1. Confirm the core has been accepted into `libretro-super/dist/info`.
2. Replace any "not currently documented" entries with final support status.
3. Confirm the core option and control tables still match the accepted build.
4. Add the page to `mkdocs.yml`, `docs/guides/core-list.md`,
   `docs/development/licenses.md`, and `docs/library/bios.md`.
