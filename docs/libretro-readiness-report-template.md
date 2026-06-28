# Altirra Libretro Readiness Test Report

Copy this template for each release-candidate validation pass. Keep completed
reports outside the source tree unless they are intentionally being archived.
Use `bash scripts/create-libretro-readiness-report.sh` to create a prefilled
copy from the current checkout and package. Pass `--verify-package` to record
the package verifier output in the report. Pass
`--retroarch-smoke-summary build/libretro-readiness/retroarch-smoke-*/summary.md`
to embed automated frontend evidence in the diagnostics section. Run
`bash scripts/run-libretro-retroarch-smoke.sh --package PACKAGE --verify-package`
to capture real RetroArch no-content, generated `.xex`, generated `.atr`,
generated `.a52`, generated `.cas`, generated `.m3u`, and generated `.zip`
launch/unload logs before manual testing. The automated smoke defaults to
RetroArch's `null` video/input drivers; record separate visible-session or
device runs in the matrix below. Use
`bash scripts/create-libretro-manual-test-kit.sh --package PACKAGE --retroarch-smoke-summary SUMMARY --report REPORT`
to bundle the package, generated smoke fixtures, smoke summary, and prefilled
report for testers. Completed reports must include this embedded smoke summary
and completed manual/device rows. Run
`bash scripts/validate-libretro-readiness-report.sh REPORT.md` on completed
reports before changing `is_experimental`.

## Build Under Test

- Date:
- Tester:
- Git commit:
- Altirra version:
- Build command:
- Smoke test command:
- Smoke test result:
- Package path:
- Package verifier command:
- Package verifier result:
- Host OS and version:
- CPU architecture:
- RetroArch distribution: native / Flatpak / Steam / other
- RetroArch version:
- RetroArch video driver:
- RetroArch audio driver:

## Installed Files

- Core path:
- Core Info path:
- `altirra_libretro.info` installed: yes / no
- Core Information page shows display name: yes / no
- Core Information page shows author: yes / no
- Core Information page shows firmware list: yes / no
- Core Information page shows supported extensions: yes / no
- Core Information page shows save-state support: yes / no

## Content Matrix

Use known-good test media. Record filename, expected machine type if relevant,
and whether the core loads, runs, resets, unloads, and survives frontend exit
without errors or coredumps.

| Type | File | Load | Run 60s | Reset | Close Content | Exit RetroArch | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| No-content boot | none |  |  |  |  |  |  |
| Executable | `.xex` |  |  |  |  |  |  |
| Disk | `.atr` |  |  |  |  |  |  |
| Cartridge | `.car` |  |  |  |  |  |  |
| Cassette | `.cas` |  |  |  |  |  |  |
| Playlist | `.m3u` |  |  |  |  |  |  |
| Compressed content | `.zip` or `.gz` |  |  |  |  |  |  |

## Runtime Features

| Feature | Pass | Notes |
| --- | --- | --- |
| Save state creates successfully |  |  |
| Save state loads successfully |  |  |
| Save state survives close/reload content |  |  |
| Core options can change before loading content |  |  |
| Core options can change while content is running |  |  |
| Video standard option changes geometry/timing as expected |  |  |
| Input Port 1 joystick works |  |  |
| Keyboard focus works for Atari keyboard input |  |  |
| Disk Control interface opens and reports media |  |  |
| Disk Control can eject, swap, and remount media |  |  |
| Disk Control rejects media-list changes while tray is closed |  |  |
| Audio is present and stable |  |  |
| RetroArch logs contain no Altirra errors |  |  |
| No coredump or frontend crash produced |  |  |
| Alt+F4 / window close exits without crash |  |  |

## Gamepad UX

Use a controller-only setup, or ignore/unplug the keyboard after launching the
frontend. Record pass/fail for the user-facing controller path, not just the
automated libretro input API.

| Check | Pass | Notes |
| --- | --- | --- |
| D-pad drives joystick input |  |  |
| Left analog drives joystick input |  |  |
| START console key reachable from pad |  |  |
| SELECT console key reachable from pad |  |  |
| OPTION console key reachable from pad |  |  |
| Virtual keyboard opens and closes from pad |  |  |
| Virtual keyboard types Atari keys from pad |  |  |
| Virtual keyboard page switching works from pad |  |  |
| 5200 keypad page works for 5200 content |  |  |
| Warm reset binding works from pad |  |  |
| Cold reset binding works from pad |  |  |
| Spare button key mappings can be changed from core options |  |  |

## Logs And Diagnostics

RetroArch log location:

```sh
find ~/.var/app/org.libretro.RetroArch/config/retroarch/logs \
  -maxdepth 1 -type f -print -exec tail -120 {} \;
```

Native RetroArch log location:

```sh
find ~/.config/retroarch/logs \
  -maxdepth 1 -type f -print -exec tail -120 {} \;
```

Coredump check:

```sh
coredumpctl list retroarch
```

Attach relevant log excerpts here:

```text

```

## Verdict

- Ready for `is_experimental = "false"`: yes / no
- Ready for `libretro-super` `.info` PR: yes / no
- Ready for `libretro/docs` PR: yes / no
- Blocking issues:
- Follow-up issues:
