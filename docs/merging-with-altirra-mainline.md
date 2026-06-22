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

## Issue 1 — `static constexpr` inside a constexpr function/lambda

### Symptom (CI log)

```
error: constexpr variable 'kFoo' must be initialized by a constant expression
note:  control flows through the definition of a static variable
```

The note will point at an inner `static constexpr` declaration inside a
lambda whose result initialises a `constexpr` variable.

### Why CI rejects it

Mainline Altirra commonly writes immediately-invoked constexpr lambdas
that wrap a sorted/cooked table:

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

Non-static `constexpr` locals are legal under C++17. The outer lambda
is still evaluated once at compile time and the result is copied into
the surrounding constexpr variable, so the inner array never exists at
runtime. Zero runtime cost.

### Where to look during a merge

Audit recipe:

```sh
grep -rn "constexpr auto.*= \[\]" src/
```

For each match, open the file and check the lambda body. Any `static`
qualifier on a local variable inside the body must be removed.

### Canonical fix in code

`src/ATDebugger/source/defsymbols.cpp` — the file we fixed first.
Contains an in-source `MERGE NOTE` banner above
`ATPreSortDefaultSymbolArray` reminding maintainers about this exact
pitfall. Five lambdas in that file (`kATDefaultSymbolsForOSVariables`,
`kGTIASymbols`, `kPOKEYSymbols`, `kPIASymbols`, `kANTICSymbols`) follow
the corrected pattern.

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
