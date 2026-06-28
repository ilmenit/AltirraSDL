# Altirra Libretro Upstream Preparation

This document tracks the project-side work needed before submitting the
Altirra libretro core to Libretro infrastructure. It is intentionally scoped to
this repository; upstream pull requests still happen in Libretro-owned repos.

## Local Source of Truth

- Core source: `src/AltirraLibretro/`
- Core metadata: `src/AltirraLibretro/altirra_libretro.info`
- Metadata validator: `scripts/validate-libretro-info.sh`
- Docs draft validator: `scripts/validate-libretro-docs.sh`
- Artifact verifier: `scripts/verify-libretro-artifact.sh`
- Package verifier: `scripts/verify-libretro-package.sh`
- Libretro docs draft: `docs/libretro-docs-altirra-draft.md`
- RetroArch readiness report template:
  `docs/libretro-readiness-report-template.md`
- Readiness report helper: `scripts/create-libretro-readiness-report.sh`
- Manual test kit helper: `scripts/create-libretro-manual-test-kit.sh`
- Readiness report validator: `scripts/validate-libretro-readiness-report.sh`
- RetroArch frontend smoke helper:
  `scripts/run-libretro-retroarch-smoke.sh`
- Upstream staging helper: `scripts/prepare-libretro-upstream.sh`
- Generated docs PR snippets:
  `build/libretro-upstream/libretro-docs/ALTIRRA-UPSTREAM-SNIPPETS.md`
- User/build docs: `src/AltirraLibretro/README.md`
- Canonical local build: `./build.sh --libretro`
- Local smoke-test build: `./build.sh --libretro --libretro-test`
- Flatpak-compatible build: `./build.sh --libretro-flatpak`
- Libretro-style wrapper: `make -f Makefile.libretro`
- Libretro-style artifact verification: `make -f Makefile.libretro verify`
- Libretro-style smoke tests: `make -f Makefile.libretro test`
- CI coverage: `.github/workflows/libretro-core.yml`

The CMake target is the only real build definition. `Makefile.libretro` is a
thin wrapper for automation that expects a Makefile entry point; do not add a
second source list there.

The GitHub workflow must stay aligned with the local validators and package
layout. CI packages should include the core, `altirra_libretro.info`,
`README.md`, `install-retroarch.sh`, `LICENSE`, and `BUILD-INFO.txt`, then run
the same verifier scripts used by local builds where the runner can inspect the
target binary.

## Readiness Gates

Keep `is_experimental = "true"` in `altirra_libretro.info` until these checks
have been repeated on current code. Use
`docs/libretro-readiness-report-template.md` for each release-candidate pass so
the results are repeatable and reviewable:

1. Native Linux core builds with `./build.sh --libretro`.
2. RetroArch Flatpak core builds with `./build.sh --libretro-flatpak`.
3. Built artifacts pass `scripts/verify-libretro-artifact.sh`, including
   required `retro_*` exports and no SDL3 dependency on platforms where the
   local toolchain can inspect those properties. On x86_64 ELF builds, the
   verifier also checks `retro_get_system_info()` for the known-bad
   RIP-relative XMM pointer-table codegen that can return invalid metadata
   pointers during RetroArch shutdown.
4. Package archives pass `scripts/verify-libretro-package.sh`, including
   required sidecar metadata, license text, README, installer, build
   provenance, packaged-core artifact checks, and a dry-run installer smoke
   test against a temporary RetroArch config.
5. `./build.sh --libretro --libretro-test` passes. The smoke host loads the
   core and runs no-content boot, disk-control, option-change, geometry,
   audio, input, and save-state paths against core-options V2, V1, and legacy
   frontend environments. It also queries `retro_get_system_info()` after
   unload/deinit to catch invalid shutdown metadata pointers.
6. `scripts/run-libretro-retroarch-smoke.sh` passes against the package under
   test and its summary is embedded in the readiness report. This proves
   no-content plus generated `.xex`, `.atr`, `.a52`, `.cas`, `.m3u`, and `.zip`
   load/run/unload paths through a real RetroArch frontend with isolated null
   video/input drivers.
7. `scripts/create-libretro-manual-test-kit.sh` creates the tester handoff
   bundle from the package, smoke summary, generated fixtures, and prefilled
   report so visible/device runs use the same package and media that passed
   frontend smoke.
8. RetroArch visible-session/device testing loads and runs representative
   `.xex`, `.atr`, `.car`/`.a52`, `.cas`, `.m3u`, compressed content, and
   no-content sessions without frontend errors or crashes. For each content
   type, test both Close Content and full frontend exit through the window
   close path, because RetroArch can query `retro_get_system_info()` again
   while saving frontend state during shutdown.
9. The readiness report's Gamepad UX table passes on a controller-only setup,
   including joystick directions, console keys, virtual keyboard typing/page
   switching, 5200 keypad access, reset bindings, and spare-button remapping.
10. The readiness report's Disk Control rows pass in a visible frontend,
    including media reporting, eject/swap/remount, and rejection of media-list
    changes while the tray is closed.
11. Save states round-trip in RetroArch for loaded content.
12. Core options can be changed at runtime without illegal libretro callbacks.
13. `altirra_libretro.info` is installed into `libretro_info_path` and
   RetroArch shows the expected name, author, firmware, extension, and feature
   information under Information / Core Information.
14. Linux packages are built against the intended ABI floor. For Flathub
   RetroArch, use the Flatpak SDK build path, not a host-native build.

## Versioning Policy

`display_version` in `altirra_libretro.info` should match the CMake project
version before an upstream metadata PR. The libretro CMake target generates a
target-local `version.h` from `PROJECT_VERSION`, so `retro_get_system_info()`
does not report the default local-build value of `dev`.

When preparing a tagged release, update the top-level CMake project version
first, then verify that the generated core info and package names use the same
version.

Run the metadata validator before packaging or copying the `.info` into an
upstream pull request:

```sh
bash scripts/validate-libretro-info.sh
```

The validator enforces `is_experimental = "true"` by default. After completed
readiness reports prove the core can be promoted, validate a deliberate
`is_experimental = "false"` change with:

```sh
ALTIRRA_LIBRETRO_ALLOW_NON_EXPERIMENTAL=1 \
  bash scripts/validate-libretro-info.sh
```

## Upstream PR Order

1. Submit `altirra_libretro.info` to `libretro-super/dist/info/`.
2. Add Libretro CI/CD wiring after maintainers confirm the expected source
   repository and buildbot entry point. Prefer a root `.gitlab-ci.yml` that
   delegates to this repository's CMake/build script or `Makefile.libretro`.
3. Add Libretro documentation:
   - `docs/library/altirra.md`
   - `mkdocs.yml`
   - `docs/guides/core-list.md`
   - `docs/development/licenses.md`
   - `docs/library/bios.md`
   - `docs/meta/see-also.md` if maintainers want cross-links to Atari800
   Start from this repository's `docs/libretro-docs-altirra-draft.md`, then
   sync the final core option and control tables from the accepted core build.
4. Reuse existing playlist/database systems for `Atari - 8-bit Family` and
   `Atari - 5200`. Do not create an Altirra-specific game database.
5. Add or update RetroArch assets only if the existing Atari playlist/content
   icons are missing or Libretro maintainers request core-specific assets.

## Upstream Validation Commands

Build and package locally:

```sh
./build.sh --libretro --package
```

Build and run smoke tests:

```sh
./build.sh --libretro --libretro-test
```

Build for RetroArch Flatpak users:

```sh
flatpak install flathub org.kde.Sdk//6.10
./build.sh --libretro-flatpak --package
```

Build through the Libretro-style wrapper:

```sh
make -f Makefile.libretro
make -f Makefile.libretro verify
make -f Makefile.libretro test
```

Validate the metadata:

```sh
bash scripts/validate-libretro-info.sh
bash scripts/validate-libretro-docs.sh
bash scripts/validate-libretro-readiness-report.sh --self-test
bash scripts/prepare-libretro-upstream.sh
bash scripts/verify-libretro-artifact.sh \
  build/linux-libretro/src/AltirraLibretro/altirra_libretro.so \
  build/linux-libretro/src/AltirraLibretro/altirra_libretro.info
bash scripts/verify-libretro-package.sh \
  build/linux-libretro/AltirraLibretro-4.40-linux-x86_64.tar.gz
bash scripts/run-libretro-retroarch-smoke.sh \
  --package build/linux-libretro/AltirraLibretro-4.40-linux-x86_64.tar.gz \
  --verify-package
```

Stage files for future Libretro repository pull requests:

```sh
bash scripts/prepare-libretro-upstream.sh
```

The staging helper validates both the source docs draft and the generated
`build/libretro-upstream/libretro-docs/docs/library/altirra.md` page, so local
submission notes are not copied into the upstream-facing Markdown by accident.
It also writes `build/libretro-upstream/libretro-docs/ALTIRRA-UPSTREAM-SNIPPETS.md`
with candidate entries for `mkdocs.yml`, `docs/guides/core-list.md`,
`docs/development/licenses.md`, `docs/library/bios.md`, and optional
`docs/meta/see-also.md` edits. Treat those as review aids; adapt them to the
current Libretro docs repository sort order and style before opening the docs
PR.

Create a prefilled RetroArch readiness report for a package under test:

```sh
bash scripts/create-libretro-readiness-report.sh \
  --package build/linux-libretro/AltirraLibretro-4.40-linux-x86_64.tar.gz \
  --retroarch-smoke-summary \
    build/libretro-readiness/retroarch-smoke-<timestamp>/summary.md \
  --verify-package
bash scripts/create-libretro-manual-test-kit.sh \
  --package build/linux-libretro/AltirraLibretro-4.40-linux-x86_64.tar.gz \
  --retroarch-smoke-summary \
    build/libretro-readiness/retroarch-smoke-<timestamp>/summary.md \
  --report build/libretro-readiness/AltirraLibretro-4.40-<commit>-<timestamp>.md
bash scripts/validate-libretro-readiness-report.sh \
  build/libretro-readiness/AltirraLibretro-4.40-<commit>-<timestamp>.md
```

Install the generated `.so`/`.dll`/`.dylib` into RetroArch's cores directory
and install `altirra_libretro.info` into RetroArch's Core Info directory. In
RetroArch, confirm the info page before submitting the `libretro-super` PR.
