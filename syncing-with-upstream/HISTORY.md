# AltirraSDL Upstream Sync History

One line per sync, newest first.

Format:  `YYYY-MM-DD  <OLD> → <NEW>  <summary>`

2026-06-11  4.50-test10 → 4.50-test11  Valid source-based report regenerated from usable `src/` snapshots. Upstream `src/` delta was empty (0 upstream changes, 0 trivial copies, 0 three-way merges, 0 added/removed) because the provided `test10` and `test11` source snapshots are identical under `src/`. SDL-targeted test11 UI work for the lower-right quick bar, quick-bar settings persistence, command-state tooltips, typed quick-map commands, SIO patch command coverage, netplay guards, and side-menu Quit path review is tracked in `MERGE_PLAN.md`. Linux release `./build.sh` passed, headless startup reached the render loop, script-backed `--test-mode` UI checks passed for the quick bar, Configure System toggle/persistence, 5200 disabled states, and mobile exit confirmation path, and sync-tool invalid-report safety gates passed.

2026-04-15  4.50-test8 → 4.50-test9  166 trivial copies, 43 three-way merges, 4 added (printer PDF/SVG export). AltirraOS 3.49 kernel fixes (printer P:, SIO timeout), AMDC/Percom/SAP bug fixes, GCC portability macros (VD_COMPILER_CLANG_OR_GCC, VD_PLATFORM_WINDOWS, VD_NO_UNIQUE_ADDRESS) adopted from upstream. Kernel ROM rebuilt via MADS (kernel.rom 10K, kernelxl.rom 16K, nokernel.rom 16K) and re-embedded as C arrays under src/AltirraSDL/romdata/. Linux release build green; --test-mode ping/query_state OK.
