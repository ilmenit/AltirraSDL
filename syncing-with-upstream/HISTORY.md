# AltirraSDL Upstream Sync History

One line per sync, newest first.

Format:  `YYYY-MM-DD  <OLD> → <NEW>  <summary>`

2026-07-14  4.50-test13 → 4.50-test14  Printer rasterizer/export rewrite, 1020 timing/FIFO/sound support, 1020/1029 accuracy fixes, H: device error handling, updated Windows resources/projects, and new printer PCM/vector support were merged. SDL integration added new 1020 audio resources, device configuration options, non-Windows date/error-translation support, GCC/Clang-safe int128 helpers, and merge-protection comments for the upstream printer sound-ID typo and GCC 12-incompatible static hasher operator. Linux release, libretro, and Bridge server builds passed.

2026-06-22  4.50-test12 → 4.50-test13  18 upstream-changed files: debugger trace clearing after interrupted `gt`, 1020 accurate line stepping setting, 1020 Win32/SDL config UI, upstream Win32 ATUI quick-bar submenu refresh, upstream changelog, command icon metadata/assets, and Visual Studio metadata were merged. The SDL quick bar mirrors the new View submenu with artifacting, frame blending, scanlines, overscan, and fullscreen commands. Linux build passed.

2026-06-11  4.50-test10 → 4.50-test11  Valid source-based report regenerated from usable `src/` snapshots. Upstream `src/` delta was empty (0 upstream changes, 0 trivial copies, 0 three-way merges, 0 added/removed) because the provided `test10` and `test11` source snapshots are identical under `src/`. SDL-targeted test11 UI work for the lower-right quick bar, quick-bar settings persistence, command-state tooltips, typed quick-map commands, SIO patch command coverage, netplay guards, and side-menu Quit path review is tracked in `MERGE_PLAN.md`. Linux release `./build.sh` passed, headless startup reached the render loop, script-backed `--test-mode` UI checks passed for the quick bar, Configure System toggle/persistence, 5200 disabled states, and mobile exit confirmation path, and sync-tool invalid-report safety gates passed.

2026-04-15  4.50-test8 → 4.50-test9  166 trivial copies, 43 three-way merges, 4 added (printer PDF/SVG export). AltirraOS 3.49 kernel fixes (printer P:, SIO timeout), AMDC/Percom/SAP bug fixes, GCC portability macros (VD_COMPILER_CLANG_OR_GCC, VD_PLATFORM_WINDOWS, VD_NO_UNIQUE_ADDRESS) adopted from upstream. Kernel ROM rebuilt via MADS (kernel.rom 10K, kernelxl.rom 16K, nokernel.rom 16K) and re-embedded as C arrays under src/AltirraSDL/romdata/. Linux release build green; --test-mode ping/query_state OK.
