# Merging with Altirra Mainline

This document catalogues recurring build failures we see in CI **after**
merging code from upstream Altirra test branches (test10 → test11 →
test12 → …) and how to fix them.

Recurring causes include **toolchain divergence**, standalone-target
header initialization and platform-specific test assumptions. Upstream
Altirra is developed with MSVC on Windows; a successful build on a recent
local compiler does not establish compatibility with our **CI floor**:

- **macOS CI** — Apple Clang 15 (Xcode 15 on the `macos-14` runner)
- **Linux CI** — GCC 12 (from the `ubuntu:22.04` container)
- **Windows CI** — MSVC with Windows SDK 10.0.26100 UCRT headers;
  standalone test targets must initialize their own intrinsic headers

So code merged from mainline can build cleanly locally and break the
moment it hits CI. The failures are mechanical to fix once you know the
pattern. Read this list before pushing a mainline merge and again the
moment CI turns red.

---

## Issue 1 — `static constexpr` inside a constexpr/consteval function

### Symptom (CI log)

```
error: constexpr variable 'kFoo' must be initialized by a constant expression
note:  control flows through the definition of a static variable
```

The diagnostic points at an inner `static constexpr` declaration inside a
`constexpr` or `consteval` function, constructor, or lambda. A failed
immediate invocation may then produce a second, cascading error:

```
error: 'consteval Foo::Foo()' called in a constant expression
```

### Why CI rejects it

Mainline Altirra commonly writes immediately-invoked constexpr lambdas
that wrap a sorted/cooked table, and consteval constructors that validate
tables:

```cpp
static constexpr auto kThing = [] {
    static constexpr SomeType kRaw[] = { ... };  // ← P2647 territory
    return cookIt(kRaw);
}();
```

`static` (including `static constexpr`) variables inside a `constexpr`
function or lambda body were forbidden by C++17/20 and only allowed by
[P2647](https://wg21.link/p2647) — *"Permitting static constexpr
variables in constexpr functions"* — accepted into C++23.

- **MSVC 19.36+**, **Clang 17+**, **GCC 13.2+** implement P2647 →
  the upstream pattern just works.
- **Apple Clang 15**, **GCC 12** (our CI) **do not** implement P2647 →
  hard error.

### Fix

Drop `static` from the inner declaration:

```cpp
static constexpr auto kThing = [] {
    constexpr SomeType kRaw[] = { ... };         // ← non-static
    return cookIt(kRaw);
}();
```

Non-static `constexpr` locals are legal under C++17. The enclosing
constant evaluation still happens at compile time, so the inner array
never exists at runtime. Zero runtime cost. Do not move validation to
runtime or remove `constexpr`/`consteval` merely to silence the compiler.

### Where to look during a merge

Mandatory audit recipe:

```sh
# Review every static constexpr declaration added by the upstream merge.
git diff <pre-merge-commit>..HEAD -- '*.cpp' '*.h' \
  | grep '^+.*static constexpr'

# Also find constexpr lambdas for context; inspect their bodies for locals.
grep -rn "constexpr auto.*= \[\]" src/
```

For each added declaration, determine whether the enclosing scope is a
`constexpr` or `consteval` function, constructor, or lambda. Any `static`
qualifier on such a local variable must be removed. Add a `MERGE NOTE`
referencing this issue at every corrected upstream site so a later merge
does not silently restore it.

### Canonical fix in code

`src/ATDebugger/source/defsymbols.cpp` — the file we fixed first.
Contains an in-source `MERGE NOTE` banner above
`ATPreSortDefaultSymbolArray` reminding maintainers about this exact
pitfall. Five lambdas in that file (`kATDefaultSymbolsForOSVariables`,
`kGTIASymbols`, `kPOKEYSymbols`, `kPIASymbols`, `kANTICSymbols`) follow
the corrected pattern.

`src/Altirra/source/printerfontfx80.cpp` — the test16 merge added a
`static constexpr` width table inside a `consteval` constructor. GCC 12
rejected every Linux product that compiled this shared source. The table
must remain local `constexpr` (without `static`) so its compile-time
cross-check is preserved on the CI compiler floor.

---

## Issue 2 — Headers using libc names without explicit `<stdlib.h>` / `<string.h>`

### Symptom (CI log)

```
error: use of undeclared identifier 'malloc'
error: use of undeclared identifier 'memcpy'
error: use of undeclared identifier 'memset'
```

The error points at a header file (typically under `src/h/vd2/` or
`src/h/at/`) that uses a libc function but doesn't include the C header
that declares it. The compile unit that triggers the failure is usually
something that does **not** pull in `stdafx.h` (e.g.
`src/system/source/stdaccel.cpp`).

### Why CI rejects it

Header files in the VirtualDub/Altirra codebase have historically
relied on **transitive** standard-library includes — `<string>` pulls
in `<string.h>` indirectly, `<memory>` pulls in `<stdlib.h>`
indirectly, etc.

The transitive chain depends on the libstdc++ / libc++ implementation:

- **MSVC STL**, **libstdc++ 13+**, **libc++ 16+** still expose these
  names transitively → upstream code compiles.
- **libstdc++ 12** (Ubuntu 22.04 / our Linux CI) tightened its module
  graph and no longer leaks `malloc` / `memcpy` from `<memory>` /
  `<string>` → hard error in the same code.

### Fix

Make the offending header self-contained: explicitly `#include
<stdlib.h>` and/or `#include <string.h>` next to the existing
`#include`s. Use the C headers (`<stdlib.h>`, `<string.h>`) — not the
C++ wrappers (`<cstdlib>`, `<cstring>`) — to match the surrounding
codebase style.

Example fix from `src/h/vd2/system/vdstl.h`:

```diff
 #include <limits.h>
 #include <stdexcept>
 #include <initializer_list>
 #include <ranges>
 #include <memory>
+#include <stdlib.h>		// malloc / free — see docs/merging-with-altirra-mainline.md
 #include <string.h>
 #include <vd2/system/vdtypes.h>
```

### Where to look during a merge

Audit recipe (run from repo root):

```sh
# malloc/free without <stdlib.h>
find src/h -name '*.h' -print0 | while IFS= read -r -d '' f; do
  grep -q '\bmalloc\b\|\bfree(' "$f" 2>/dev/null || continue
  grep -q 'include <stdlib.h>\|include <cstdlib>' "$f" 2>/dev/null && continue
  echo "MALLOC w/o <stdlib.h>: $f"
done

# memcpy/memset/memmove/memcmp without <string.h>
find src/h -name '*.h' -print0 | while IFS= read -r -d '' f; do
  grep -qE '^[^/]*\b(memcpy|memset|memmove|memcmp)\(' "$f" 2>/dev/null || continue
  grep -q 'include <string.h>\|include <cstring>' "$f" 2>/dev/null && continue
  echo "MEMCPY w/o <string.h>: $f"
done
```

Both commands should produce no output on a clean tree.

### Files currently patched

These headers were missing libc includes and have been fixed. If a
mainline merge regresses any of them, restore the include rather than
relying on transitive luck:

| Header | Added include(s) | For |
|---|---|---|
| `src/h/vd2/system/vdstl.h`              | `<stdlib.h>`               | `malloc`, `free` |
| `src/h/vd2/system/vdstl_structex.h`     | `<stdlib.h>`, `<string.h>` | `malloc`, `free`, `memcpy` |
| `src/h/vd2/system/memory.h`             | `<string.h>`               | `memcpy`, `memset` |
| `src/h/vd2/system/vdstl_block.h`        | `<string.h>`               | `memcpy` |
| `src/h/vd2/system/vdstl_fastdeque.h`    | `<string.h>`               | `memcpy`, `memmove`, `memset` |
| `src/h/vd2/system/vdstl_fastvector.h`   | `<string.h>`               | `memcpy`, `memmove` |
| `src/h/vd2/system/vecmath_ref.h`        | `<string.h>`               | `memcpy` |
| `src/h/at/atcore/decmath.h`             | `<string.h>`               | `memcpy` |
| `src/h/at/atnetwork/socket.h`           | `<string.h>`               | `memset` |

`src/h/vd2/VDDisplay/direct3d.h` uses `memset` but is Windows-only and
gets it transitively via `<windows.h>` — no patch needed unless that
file ever becomes part of the cross-platform build.

---

## Issue 3 — Parenthesized aggregate construction in `emplace_back`

Apple Clang 15 reports `no matching constructor for initialization of
'ATVFSPathParam'` from `vdvector<T>::emplace_back`. The aggregate has no
three-argument constructor; the vector internally uses `T(args...)`, and
this compiler does not support that aggregate initialization form.

Construct the aggregate with braces, then move it into the vector:

```cpp
parsedPath.mParams.emplace_back(ATVFSPathParam {
    VDHashString32I(argName.data(), argName.size()),
    std::move(argName), std::move(argValue)
});
```

Review added `emplace_back` calls and the element types, not just direct
aggregate construction. Keep the `MERGE NOTE` in
`src/ATCore/source/vfs.cpp`; `RenderDot` in `printerrasterizer.cpp` has the
same compiler constraint.

## Issue 4 — Windows SDK intrinsics missing in standalone test sources

Windows SDK 10.0.26100 `wchar.h` can report undeclared `__m128i`, `__m256i`
or `_mm_movemask_epi8`. A source can build in the application target yet
fail when compiled in a standalone regression target with different header
initialization.

Include the appropriate application `stdafx.h` before CRT/string headers.
`src/Altirra/h/stdafx.h` already initializes the MSVC intrinsics and includes
`<immintrin.h>` only on x86/x64. Preserve that architecture guard for ARM64.
The test22 fixes are in `tests/upstream/test22.cpp` and
`src/AltirraSDL/source/app/cputest_runner.cpp`. Audit every source compiled
by a new test target; fixing the first source can expose the same problem
in a later target.

## Issue 5 — Tests assuming POSIX path syntax on Windows

A native Windows test can compile successfully and then fail with
`Virtual URL escaping` or `Legacy mount lost root`.

URL encoding converts backslashes to forward slashes. Build native paths
with `VDMakePath` and compare decoded URL paths after
`VDFileGetCanonicalPath`, while still asserting Unicode, escaped punctuation
and query values. Root wildcard tests use `/*` and `/**` on POSIX and
`C:/*` and `C:/**` on Windows. The Windows filesystem helpers require a
qualified drive or UNC root for their absolute-path classification.

These are test portability corrections, not reasons to change the native
filesystem implementation. See `tests/upstream/test22.cpp`.

---

## Workflow recommendation

Before pushing a mainline merge:

1. **Build locally** with the most recent toolchain you have. A clean
   local build proves the code is syntactically and semantically valid
   but does **not** prove it will build on the CI floor.
2. **Run the audits** from Issues 1–5 above, including aggregate
   construction, standalone-target header ordering and OS-specific paths.
3. If you can, build inside the same ubuntu:22.04 container the Linux
   CI uses (`docker run --rm -v $PWD:/src ubuntu:22.04 …`) to catch
   anything the recipes miss.
4. **Build every regression target**, using the commands in
   `tests/upstream/README.md`. Run the native Windows/macOS and Linux
   normal/sanitizer jobs; a Linux-only result cannot validate Windows SDK
   includes or Windows path semantics.
5. **Verify the exact PR head** passes all regression and affected platform
   build jobs before merging. Expected release/deployment skips on PR builds
   are not failures. Record CI results separately from local/device results.

A package installation failure before compilation may be external to the
code: test22 encountered Ubuntu package downloads returning HTTP 404 and
Chocolatey returning HTTP 504. Read the failed step before changing source;
retry such jobs when appropriate. GitHub may require the workflow to finish
before allowing a failed-job rerun.

After CI fails on a known pattern, fix it the way this document
prescribes, add an in-source comment if the offending code is in a
mainline-tracked file (so the next merger sees the constraint at the
diff site), and update the file table in Issue 2 if you added a new
explicit-include patch.

---

## Test12 local merge note

The test11 → test12 merge imported the 1020 Color Printer rework, but
kept one local correction in `src/Altirra/source/printer1020.cpp`:
`DrawClippedLine()` uses `-raw2.x` when clipping an exit endpoint to the
left edge (`x = 0`). Upstream test12 used `raw2.x` there, which moves
the interpolated Y coordinate in the wrong direction for lines exiting
past the left paper edge. Preserve this correction when re-syncing the
file from upstream.

Upstream test13 now carries the same `-raw2.x` correction, so this is no
longer a fork-only delta after the test12 → test13 sync. Keep this note as
historical context if future upstream snapshots touch the same clipping
block.

---

## Test15 local merge notes

The test14 → test15 merge keeps or adds five deliberate differences from
upstream:

- `src/Altirra/source/printerexport.cpp` keeps `LinkedPointHash::operator()`
  as a non-static `const` member because the upstream C++23 static call
  operator is not accepted by the GCC 12 CI floor.
- `src/Altirra/source/printeroutput.cpp` includes
  `<vd2/system/error.h>` with the filesystem's actual lowercase spelling.
- `src/Altirra/source/printerexport.cpp` removes a duplicate
  `EndSimpleGlyph()` call, correctly validates and encodes Unicode scalars,
  preserves `/Length1` only where required for TrueType streams, and keeps
  the required Type 0, CIDFont, and font descriptor names consistent while
  adding matching optional PostScript names to the embedded TrueType fonts.
- `src/Altirra/source/printer.cpp` recognizes both normal and elongated 1029
  space-character IDs when trimming trailing print data, and does not shift
  tracked characters left by one dot radius relative to the fallback dot
  renderer.
- `src/Altirra/source/printeroutput.cpp` invokes the horizontal-move callback
  rather than the vertical callback when `Clear()` reports the horizontal
  head position, and includes the left dot extent in character font bounds.

The SDL debugger's SVG exporter also uses `ExtractNextLineAsDots()` after
test15 split the old `ExtractNextLine()` API into dot-only and
dot-or-character variants. This exporter emits SVG geometry, so expanding
known characters back into dots is intentional.

---

## Test16 local merge note

`src/h/at/atcore/enumutils.h` keeps the fork's `vd_to_underlying()` helper
instead of changing the enum-flag macros to upstream's C++23
`std::to_underlying()`. The helper keeps the shared headers usable by the
fork's C++20-capable build configurations. Test16's new `constexpr` on the
operators is retained; preserve both the `constexpr` qualifiers and the local
helper on future merges.

---

## Test19 local merge notes

- POKEY save-state validation already accepts linked-channel borrow offsets
  through 6. Keep the fork's reader-only validation and quad-pair serialization.
- FX-80 selectable glyphs must preserve raw-dot geometry: enable proportional
  handling explicitly, use positive advances, and retain the full 12-column
  span for non-proportional styles. The ROM-to-user-RAM regression comparison
  exercises both rendering paths, including expanded and proportional styles.
- PDF tracked-character coordinates use the fork's physical dot centers.
  Keep the radius subtraction in composite glyphs and the matching radius
  correction in raw-dot TJ positioning. Copying test19's composite offset alone
  misaligns these paths. Poppler render comparisons cover the combined fix.
- VBXE priority combinations use named PF/PM masks; test19's numeric PF2/P2/P3
  combinations do not match those bit assignments. Retain the hires priority
  table, GTIA 9/11 input mask, and hires color-index swap in the shared renderer.
- Use `VDNOINLINE` for the renderer lambda and non-static local `constexpr`
  tables inside it. Do not reintroduce MSVC-only attributes or P2647-dependent
  static locals on the supported compiler floor.
- Keep the fork's existing PNG quick/filtered selection, PDF Unicode/font
  validation, FX-80 ESC command fixes, printer wheel fix, and POSIX H: errors.
- Follow-up review: FX-80 master select must explicitly clear proportional
  mode after ESC p; a two-character spacing regression reproduces the omitted
  reset. VBXE hires-to-lores RES conversion must write every temporary merge
  pixel, including those without PF2, to avoid stale background/player bits.
- Sanitizer follow-up: preserve empty-range guards in `vdstl_fastvector.h`;
  C memory functions reject null arguments even when the count is zero.
  Deflate match probes in `system/source/zip.cpp` must use
  `VDReadUnalignedU32`, since match offsets are only byte-aligned. Both defects
  were reproduced by GCC 12 UBSan before correction.
- Apple Clang 15 requires brace initialization for the `RenderDot` aggregate
  in `printerrasterizer.cpp`. Do not restore upstream's parenthesized aggregate
  form; it fails the macOS native regression build.

Focused tests live in `tests/upstream`, enabled by `ALTIRRA_BUILD_TESTS`.
The export validation uses Python's standard-library PNG decoder and Poppler
for PDF geometry and selectable-text checks. Regression CI requires these
tools with `ALTIRRA_REQUIRE_EXPORT_VALIDATION=ON`. These tests
do not substitute for hardware-reference VBXE or native-platform UI testing.

## Test17 local merge notes

The test16 → test17 merge preserves the fork's stricter printer corrections
where they go beyond upstream test17:

- `src/Altirra/source/printerexport.cpp` keeps scalar-range and surrogate
  validation, consistent PDF font naming/bounds, correct stream `/Length1`
  handling, and the GCC 12-compatible non-static hash call operator. Upstream
  test17 contains the surrogate subtraction and character-font descriptor name
  fixes, but not the full validation set.
- `src/Altirra/source/printer.cpp` keeps the tracked-character dot-center
  alignment correction and explanatory merge notes. Upstream test17 now also
  recognizes both normal and elongated 1029 spaces.
- `src/Altirra/source/printeroutput.cpp` keeps the lowercase include spelling,
  matching horizontal callback invocation, and left dot extent in character
  bounds while adopting test17's top-dot print-head coordinate system.

Test17 adds `ATPrinterGraphicsSpec::mBaselinePin` and uses it to position the
Windows graphical-printer cursor. The SDL debugger mirrors that behavior in
`src/AltirraSDL/source/ui/debugger/ui_dbg_printer.cpp`, including subtracting
the offset when setting the print position from a click.

## Test22 local merge notes

- Preserve the fork's 65C816 decimal arithmetic, pointer wrap, RTL stack and
  native branch fixes. Test22's 8-bit ADC overflow calculation is adopted;
  the existing 16-bit arithmetic passed all selected reference cases.
- The debugger `SetP` setter clears index high bytes when X is set, not clear.
  Preserve that correction alongside upstream's decode-table refresh.
  `SetD` must refresh alignment timing in native mode as well as emulation
  mode; the setter regression checks both directions and their cycle counts.
- Virtual images must be accepted by disk-specific and automatic loaders,
  including legacy `/*` and `/**` paths. Legacy SpartaDOS paths cannot encode
  256-byte sectors; retain URL serialization for those mounts.
- SpartaDOS boot sectors use the boot size and data sectors use the selected
  size. The old ternary had these reversed; 256-byte mounts expose the bug.
- Preserve POSIX absolute file-URL roots and relative parent components when
  adopting upstream's canonical path parsing. Implement new filesystem APIs
  in the portable backend as well as the Windows backend.
- JSON keeps upstream's empty-object and two-/three-byte UTF-8 corrections.
  Additionally retain full four-byte scalars on 32-bit wchar_t platforms,
  correct UTF-16 surrogate decoding and buffer refills, guard empty-array copies and support a
  large first allocation in the value pool. The upstream UTF-32 test helper
  must use sizeof(char32_t) for its input byte count.
- Screen masks remain anchored to physical pixels on SDL_GPU and OpenGL.
  Scanlines follow the distorted raster independently, suppressing frequencies
  above Nyquist. Rebuild SPIR-V, DXIL and MSL headers after shader changes.
- Keep fork-specific changelog entries, autosuggest validity checks, netplay
  seeding, quad POKEY support, bridge tracing and native resource adjustments.

- Construct `ATVFSPathParam` with braces before inserting it into the vector.
  Apple Clang 15 cannot use the parenthesized aggregate construction inside
  `emplace_back`. Keep this alongside the same correction for `RenderDot`.
- Regression sources that include system string headers must include
  `stdafx.h` first. It supplies the guarded x86/x64 intrinsic declarations
  required by Windows SDK 10.0.26100 UCRT `wchar.h`, without including x86
  intrinsics on ARM64.


## Test24 local merge notes

The test22 → test24 sync includes test23; the previously recorded baseline
was test22. The 20-file upstream delta retains all earlier fork corrections.

- FX 1.27's NTSC offset check must compare the stored BCD version against
  `0x27`, not decimal 127. Keep the core-version regression assertions.
- Preserve named priority masks, the 64-cell wrap on older VBXE cores,
  initialized RES pixels, and portable noinline/constexpr renderer code.
  FX 1.27 intentionally wraps the attribute map at the 43 fetched cells.
- FEATURE_CONTROL changes, resets, and version downgrades must refresh the
  active color table. Save states must preserve FEATURE_CONTROL, blitter
  submodes, raycast state, pending containers, and the collision latch.
  Optional serialized fields preserve older snapshots' defaults.
- The AVX2 dot-mask renderer must advance each vertical subsample and require
  both AVX2 and FMA at dispatch. The scalar/accelerated comparison reproduces
  the unmodified upstream sampling defect. Physical mask anchoring and SDL
  scanline behavior remain in the fork's display implementation.
- Firmware filename fallback uses a basename without extension, while
  description matching keeps the entire original string. Descriptions may
  contain periods or path separators. The bridge regression checks both
  description matching and relocated Windows filename references on POSIX.
- New VBXE choices/defaults in desktop, Gaming Mode, setup wizard and bridge
  use FX 1.27; existing explicitly selected core versions remain supported.

Focused coverage lives in `tests/upstream/vbxe.cpp` and `display.cpp`, plus
bridge firmware/custom-device tests. The parity review and proposed follow-up
work are in `docs/altirra-windows-parity-review.md`.

Test24 CI follow-up: regression targets must use `ALTIRRA_COMPAT_DIR`,
which is empty on MSVC, rather than unconditionally adding `src/compat`.
Otherwise the non-MSVC `intrin.h` shim shadows the real Windows intrinsic
header and `<future>` fails in `savestateio.cpp` with missing `_ReturnAddress`.
The display test's FMA flag check is x86/x64-only, like the production path.

## Test25 local merge notes

The test24 → test25 sync changes nine upstream files, with no additions or
removals. Snapshot input SHA-256 hashes:

- test24: `7eea262b4d3fbcf382b56dc7f7623cb689d125ee277e36f7009586442f164a3d`
- test25: `dfa61737181ff784e63e121417805ca13dba27f5cfadc3efec36e9050d5414f7`

Preserve these differences when syncing again:

- HR+LR fetches one byte per color clock. Upstream doubled the initial
  source offset but incremented it by one per clock, making split render
  spans read different pixels from an uninterrupted span. The fork removes
  that initial doubling; the regression fails on unmodified test25 and also
  checks the wrap at the end of 512K RAM.
- Keep the existing BCD-safe NTSC offset check, named priority masks,
  initialized RES pixels, older-core attribute wrap, color-table refresh,
  and portable renderer noinline/constexpr code.
- Adopt upstream's raycast snapshot fields as well as the fuller test24
  fork fields. The latter preserve pending containers, the collision latch,
  full coordinates and texture state. Load both snapshot formats, with the
  fork extension taking precedence when present. Preserve full raycast
  Y-step bits when serializing a BCB. FEATURE_CONTROL restores VIDEO_CONTROL's
  shared bit after the VIDEO_CONTROL write, including for old snapshots.
- The fork already computed disk deadlines relative to the current tick.
  Adopt upstream's legacy idle-timer clamp while preserving exact-runtime
  rollback fields and the separate buffered-command flag.
- Disable shared memory for FX 1.27 in both native and ImGui configuration,
  and enforce it in device settings for profiles and bridge clients.

The constexpr audit found only tables in ordinary runtime functions, with no
P2647-dependent locals. No aggregate `emplace_back` calls were added. New C++
regressions retain `stdafx.h` first; the standalone C SDK fixture uses only C
and SDK headers. No native path behavior or intrinsic guards changed.

AltirraBridge adds `VBXE`, `VBXE_READ`, and `VBXE_WRITE` using the production
snapshot and debugger local-memory APIs. Python, C and Pascal expose matching
helpers. The LLM workflow documents BCB/texture uploads, actual core settings,
blitter progress, collision/IRQ state and raycasting. Memory requests validate
all bytes and bounds before writing; inspection does not advance the machine
or acknowledge hardware state. Bridge package CI runs the real blitter test
on Linux, macOS and Windows; the Linux desktop job runs the new UI tests.
