# Merging with Altirra Mainline

This document catalogues recurring build failures we see in CI **after**
merging code from upstream Altirra test branches (test10 → test11 →
test12 → …) and how to fix them.

The root cause in every case is **toolchain divergence**: upstream
Altirra is developed with MSVC on Windows; AltirraSDL contributors
typically work on bleeding-edge Linux (GCC 14/15+) or macOS Apple
Silicon. Both of those toolchains tolerate language extensions and
implicit standard-library includes that our **CI floor** does not:

- **macOS CI** — Apple Clang 15 (Xcode 15 on the `macos-14` runner)
- **Linux CI** — GCC 12 (from the `ubuntu:22.04` container)

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

## Workflow recommendation

Before pushing a mainline merge:

1. **Build locally** with the most recent toolchain you have. A clean
   local build proves the code is syntactically and semantically valid
   but does **not** prove it will build on the CI floor.
2. **Run the audit recipes** from Issues 1 and 2 above. Anything they
   surface is a hard CI failure waiting to happen.
3. If you can, build inside the same ubuntu:22.04 container the Linux
   CI uses (`docker run --rm -v $PWD:/src ubuntu:22.04 …`) to catch
   anything the recipes miss.

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
