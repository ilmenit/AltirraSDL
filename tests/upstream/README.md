# Upstream merge regression tests

Configure with `-DALTIRRA_BUILD_TESTS=ON` and
`-DALTIRRA_REQUIRE_EXPORT_VALIDATION=ON`, then build all five targets and run:

```sh
cmake --build build/linux-release --target \
  altirra-upstream-regression-test altirra-vbxe-regression-test \
  altirra-test22-regression-test altirra-test22-cpu-test \
  altirra-display-regression-test --parallel 2
ctest --test-dir build/linux-release -R '^upstream-' \
  --output-on-failure --no-tests=error
```

The C++ fixture exercises the production FX-80, PNG and PDF exporters. It
compares selectable ROM glyph dots against raw user-RAM glyph dots for 2,280
character/style combinations (all printable ASCII, upright and italic, with
two adjacent characters), checks mid-line SO/DC4 changes, ESC J, and
master-select cancellation of proportional mode, and
exercises string-property removal. Another 540 comparisons cover nine
international character sets in normal, double-strike, underline, superscript,
and subscript modes. Additional assertions check Unicode mapping, both strike
passes, underline dots, and reduced script height. It writes fixtures in the
test build tree.

The VBXE target compiles the production emulator, priority tables, memory
manager, save-state code and trace support. A friend accessor seeds controlled
renderer state without changing the public emulator API. It tests:

- Priority combinations for cores 1.24 and 1.26 across all 32 GTIA tables.
- The 64-cell attribute index wrap, including cells beyond the 43 fetched.
- Actual 43-cell DMA fetch across the end of 512K RAM.
- RES conversion with deliberately poisoned temporary data and a rendered
  player-color assertion.
- Blit-list limits of 1, 256, 300 and 65,536, and early list termination.

Console output is captured. IRQ and palette-correction services are strict
test doubles that fail if invoked; these tests do not claim to validate those
services, hardware timings, or the debugger command parser itself.

Python 3 independently verifies PNG chunk CRCs, zlib framing, scanline filters,
and exact pixels for quick and filtered compression. If `pdftoppm` and
`pdftotext` are installed, it also compares rendered raw/selectable PDF glyph
positions (one-pixel quantization tolerance at 600 dpi) and text extraction.
With `ALTIRRA_REQUIRE_EXPORT_VALIDATION=ON`, configuration fails if Python or
Poppler is missing, and validation fails if tools disappear afterward. A
negative test verifies the latter failure. Python runs in isolated mode so
`PYTHONOPTIMIZE` cannot disable assertions. For local development only, the
option can be left OFF to permit an explicit PDF skip.

`.github/workflows/upstream-regressions.yml` runs the focused suite with
mandatory PDF validation on Linux/GCC 12, both normally and with ASan/UBSan
and leak detection. Separate Windows/MSVC and macOS native jobs run the C++
tests; PDF validation is assigned explicitly to Linux, not silently skipped
as part of those native jobs. Existing platform workflows still build the
full desktop and Android applications. Check results for the exact PR head;
local builds do not establish remote CI success.

Historical test19 verification: all 13 Linux CTest tests passed; all four
focused tests passed in an Ubuntu 22.04/GCC 12 container with ASan, UBSan and leak detection.
That sanitizer run exposed and verified fixes for empty fast-vector memory
operations and unaligned Deflate match reads. Android arm64-v8a and
armeabi-v7a native library builds passed before these final shared-library
guards; both final fixes also passed NDK syntax checks for both ABIs. Native
Windows/macOS jobs and device execution had not been run at that stage. See the test22 results below for later CI validation.

Test22 adds `altirra-test22-regression-test` and `altirra-test22-cpu-test`.
The first runs upstream's JSON suite plus Unicode strings across buffer
boundaries, a large first member name, URL and legacy mount persistence,
POSIX path roots, and actual multi-sector DOS/MyDOS and SpartaDOS file reads
from a Unicode host directory at both sector sizes. Settings migration
services are strict test doubles; directory watching and filesystem reads
use production code.

The CPU target compiles the production CPU and the existing conformance
runner. It checks debugger setters and 240 reference cases, including invalid
BCD operands, with the runner's cycle-count and bus-access assertions.
Optional debugger services are strict test doubles. The fixture comes from
[SingleStepTests/65816](https://github.com/SingleStepTests/65816), revision
`db6b10401729d5f20f2181dde5d3d7b037093a4a`: the first 12 cases in each emulation
and native file for opcodes 02, 69, E9, 78, A1, 6B, B7 and D0, plus the first
12 decimal cases with a low accumulator nibble above nine in each 69/E9 file.
Local validation also ran all 160,000 cases in those 16 files. The runner
permits internal bus cycles as documented in its implementation; this does
not replace testing IRQ timing or running programs on hardware.

Cross-platform fixture rules:

- Include the appropriate `stdafx.h` before CRT/string headers in standalone
  sources using the application headers; retain the x86/x64 intrinsic guard.
- Use native filesystem helpers for path construction and canonicalize URL
  round trips before comparing paths. Check query values independently.
- Test POSIX roots on POSIX and drive-qualified roots on Windows.
- Build and run all four targets on Windows/macOS as well as Linux;
  successive targets can expose independent compile or runtime failures.

Test22 validation completed on 2026-10-14 for
[PR #98](https://github.com/ilmenit/AltirraSDL/pull/98), head `ffa2dd82`:
all 21 GitHub CI build/test jobs passed, including native Windows/macOS and
Linux normal/sanitizer regressions, desktop packages, Android APKs,
WebAssembly and the libretro architecture matrix. Fourteen PR-only
release/deployment jobs were correctly skipped. The final local Linux build
and all 21 CTests passed; the CPU runner passed 160,000 selected reference
cases. Android device execution remains unverified.


Test24 extends the production VBXE fixture with FX 1.27 version/NTSC checks,
hires PF0, disabled IRQ completion, HR two-nibble collisions, extended add,
table lookup, raycast sampling, invalid containers, and serialized ZIP
save-state round trips with legacy absent-field defaults. IRQ and palette
services remain strict doubles except during explicit save/load assertions.
The display target checks gamma endpoints, extreme-radius bloom energy,
scalar/AVX2 dot-mask agreement and FMA dispatch. Unmodified test24 fails the
sampling comparison. All five targets run in Linux normal/sanitizer and
native Windows/macOS regression CI.

Bridge tests additionally exercise complete firmware descriptions containing
periods/slashes, relocated filename references, and a fast custom memory layer
across a high-bank boundary. Run them with `ALTIRRA_BRIDGE_SERVER=ON`.

Test25 extends the VBXE fixture with timing-table comparisons for memory and
constant sources, transformed zero bytes, X zoom and collision-active/inactive
paths on FX 1.26/1.27. HR+LR tests compare whole and split render spans across
the 512K wrap and serialize the overlay mode. Both upstream and fork raycast
snapshot fields load correctly; device settings enforce the FX 1.27
shared-memory restriction. The existing RAM cartridge fixture now exercises
SDX/Diamond/Express 64K CPU reads, non-mutating debugger reads, write banking,
and disable/reenable behavior.

`tests/ui/test_upstream_test25.py` checks the shared-memory control and loads
legacy disk snapshots with normal, expired and invalid motor-off timers.
`bridge-vbxe` runs real device configuration, bounded RAM transfers, an FX 1.27
Add-Carry blit and a live blitter snapshot through the headless server and
Python/C SDKs. Its package CI step also runs on native Windows/macOS.

Test26 adds register-decoding checks on cores 1.20/1.24/1.26/1.27: A5 read
aliases, $FF for read-side holes, write aliases, surrounding pass-through
addresses and the configuration latch. `bridge-vbxe` exercises both register
bases and starts a real blit through the mirrored control addresses. The
core regression fails on test25 before importing the test26 fix.
