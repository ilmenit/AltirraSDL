# Upstream merge regression tests

Configure with `-DALTIRRA_BUILD_TESTS=ON` and
`-DALTIRRA_REQUIRE_EXPORT_VALIDATION=ON`, build both
`altirra-upstream-regression-test` and `altirra-vbxe-regression-test`, then run:

```sh
ctest --test-dir build/linux-release -R upstream --output-on-failure
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
full desktop and Android applications. Adding CI jobs does not mean those
remote jobs have already run; this task does not push or dispatch workflows.

Local verification: all 13 Linux CTest tests passed; all four focused tests
passed in an Ubuntu 22.04/GCC 12 container with ASan, UBSan and leak detection.
That sanitizer run exposed and verified fixes for empty fast-vector memory
operations and unaligned Deflate match reads. Android arm64-v8a and
armeabi-v7a native library builds passed before these final shared-library
guards; both final fixes also passed NDK syntax checks for both ABIs. Native
Windows/macOS jobs and device execution remain pending.
