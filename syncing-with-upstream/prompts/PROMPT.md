# LLM Agent Brief — Syncing AltirraSDL with an Upstream Altirra Release

You are an LLM coding agent being handed a partially-automated upstream
sync task. Read this document in full before taking any action.

Nothing below depends on a specific Altirra version. It applies equally
to a prerelease-to-prerelease bump (e.g. `test8 → test9`) and to a
major-version jump.

---

## 1. Context you will NOT automatically have

- **AltirraSDL** is a fork of Avery Lee's Altirra emulator. It replaces
  the Win32 frontend with an SDL3 + Dear ImGui frontend.
- The emulation core is left intact; the fork only compiles a subset
  of upstream modules and ships its own frontend under
  `src/AltirraSDL/`.
- Upstream does not use git. Each release is a zipped source snapshot.
  Syncing is therefore a three-way diff-and-merge, not a `git rebase`.
- **Sync direction is strictly upstream → fork.** The upstream author
  does not accept patches from this fork. You never modify files in
  the OLD or NEW snapshot directories, and you do not produce any
  artefact intended to be sent upstream. The `.fork.diff` files in the
  report exist only to help you preserve the fork's local edits when
  applying upstream's changes — not as a patchset to ship anywhere.
- The fork's constraints (from `CLAUDE.md` at repo root):
  1. Keep the delta from upstream minimal.
  2. Platform-agnostic changes from upstream must be ported.
  3. Win32-specific UI changes must be **reflected** in the Dear ImGui
     frontend under `src/AltirraSDL/source/ui/`, not copied literally.
  4. The Gaming Mode UI (`src/AltirraSDL/source/ui/mobile/`) only
     needs user-visible changes that are **relevant to quick
     gameplay** — settings menus, non-gameplay dialogs, debugger
     features can be skipped there.
  5. No stubs, no placeholders: when you port something, port it fully
     or not at all.
  6. The Windows `.sln` build must be preserved — it is the primary
     upstream build and must never break. Fork-level portability
     tweaks to shared files must stay `#ifdef`-guarded so the code
     the Windows compiler sees is identical to upstream.

## 2. What has already been automated for you

Before you were invoked, the operator ran:

```bash
syncing-with-upstream/tools/sync_diff.sh \
    --old  ../<OLD_SNAPSHOT> \
    --new  ../<NEW_SNAPSHOT> \
    --fork ..
```

That produced
`syncing-with-upstream/reports/<OLD_LABEL>__to__<NEW_LABEL>/` with these
files. **Treat the report dir as your single source of truth — do not
re-diff manually.**

| File                      | What it contains                                                                 |
|---------------------------|----------------------------------------------------------------------------------|
| `SUMMARY.md`              | Counts + quick orientation                                                       |
| `00_changelog.txt`        | Upstream's own `changes.txt` diff                                                |
| `01_upstream_changed.txt` | Every file where OLD ≠ NEW                                                       |
| `02_fork_changed.txt`     | Every file where OLD ≠ FORK (i.e. fork-modified files)                           |
| `03_three_way.txt`        | Intersection: merge required                                                     |
| `04_trivial_copy.txt`     | OLD≠NEW but OLD=FORK — safe to overwrite (usually already applied by the operator) |
| `05_added_in_new.txt`     | Files upstream added                                                             |
| `06_removed_in_new.txt`   | Files upstream deleted                                                           |
| `07_classified.md`        | Three-way + added + removed, grouped by module and role                          |
| `diffs/<safe>.upstream.diff` | OLD vs NEW — what upstream changed for this file. **This is the patch you need to apply.** |
| `diffs/<safe>.fork.diff`     | OLD vs FORK — what the fork changed for this file. Preserve every one of these edits. |
| `diffs/<safe>.full.diff`     | FORK vs NEW — combined view, useful as a sanity check after merging.          |

The operator may or may not already have run
`apply_trivial.py` — check `git status` and ask if unsure.

## 3. Your job, in order

### 3.1 Produce a written plan for approval (DO NOT EDIT FILES YET)

Read, in this order:

1. `reports/<…>/SUMMARY.md`
2. `reports/<…>/00_changelog.txt`  ← highest information density
3. `reports/<…>/07_classified.md`
4. The root-level `CLAUDE.md` for the fork's design rules

Then produce a plan that, for **each role** in `07_classified.md`,
states:

- **port** — which files you will copy verbatim and which need a
  3-way merge because the fork has edits in them. For each merge,
  describe the intended approach (e.g. "upstream refactored function
  Foo; fork added `#ifdef __SDL3__` guards around a call site; I'll
  re-apply the guards around the new signature").
- **reflect-in-ui** — which files correspond to user-visible menus,
  dialogs or hotkeys in the Windows UI, and where the equivalent
  ImGui code lives in `src/AltirraSDL/source/ui/`. Cite specific file
  paths and function names. Do not guess — search first with Grep.
- **review-ui** — read the diff, decide whether it maps to a fork
  feature. If no, state that and move on.
- **copy-verbatim** — list the files you'll copy.
- **skip-win-only** — confirm each one is truly unbuilt by the SDL
  target (check `src/AltirraSDL/CMakeLists.txt` / the root
  `CMakeLists.txt` if unsure).
- **port-if-wired** — for each test file, check whether the
  corresponding test harness exists in the fork (look for
  `AltirraTest` / `ATTest` under `src/` — the SDL build may not
  compile them).
- **unknown** — stop and ask; extend `tools/lib/filemap.py` if the
  path is in a newly introduced module.

The plan must explicitly answer:

1. Does upstream's `changes.txt` mention a user-visible change that is
   **not** already covered by one of the files above? (e.g. a new
   hotkey handled inside `main.cpp`) — if yes, add a section for it.
2. Does `version.inc` or anything under `src/Kernel/` change? If so,
   the kernel ROM needs rebuilding; call that out as a build-step task.
3. Are there new files in `src/Altirra/source/` (e.g.
   `printerexport.cpp`, `printerrasterizer.cpp`) that must be added to
   the SDL target's source list in `src/AltirraSDL/CMakeLists.txt` or
   to the root CMake file list?
4. Are any fork files stale because upstream removed a type/API?
   (Search for symbols in `06_removed_in_new.txt` using Grep across
   `src/AltirraSDL/` and any 3-way file.)

**Present the plan. Wait for approval. Do not edit source files yet.**

### 3.2 Execute the plan

After approval, work role-by-role in this order:

1. **Copy-verbatim files** — straight `cp` from NEW to FORK.
2. **Kernel-rom** — copy the `.s`/`.inc` files; leave the build step
   for the operator.
3. **Port / core** — three-way merge each platform-agnostic file.
   Strategy:
   - Open `diffs/<safe>.upstream.diff` (OLD→NEW) — this is the
     patch upstream authored. It is usually small.
   - Open `diffs/<safe>.fork.diff`     (OLD→FORK) — this is what
     the fork has edited on top of OLD. It may be larger but is
     self-contained.
   - Start from the current FORK file; apply the upstream hunks
     from `<safe>.upstream.diff` while preserving every fork edit
     visible in `<safe>.fork.diff`.
   - If the two patches touch overlapping lines, merge by hand:
     apply the upstream change, then re-apply the fork edit on top
     (typical case is a fork `#ifdef` guard around a function that
     upstream renamed — keep the guard, point it at the new name).
   - Common fork edits are: `#ifdef`-guarded portability tweaks,
     missing Win32 API replacements, occasional bug-workarounds.
     Preserve every one of them. `<safe>.full.diff` is the final
     FORK-vs-NEW view to cross-check that only upstream's intent
     plus the fork's prior edits remain.
4. **Reflect-in-UI** — for each Win32 `ui*.cpp` or `cmd*.cpp` change,
   find the corresponding ImGui location:
   - Menus → `src/AltirraSDL/source/ui/menus/*.cpp`
   - Configure System pages → `src/AltirraSDL/source/ui/sysconfig/*.cpp`
   - Device dialogs → `src/AltirraSDL/source/ui/devconfig/*.cpp`
   - Modal dialogs → `src/AltirraSDL/source/ui/dialogs/*.cpp`
   - Debugger panes → `src/AltirraSDL/source/ui/debugger/*.cpp`
   - Mobile (Gaming Mode) → `src/AltirraSDL/source/ui/mobile/*.cpp`
     (only for gameplay-relevant additions)
   Follow the conventions in `CLAUDE.md`:
   - Use `ImGuiCond_Appearing` centering + `NoSavedSettings` for
     modal dialogs.
   - Match Windows Altirra's exact option set and layout — **do not**
     invent controls that aren't in the Windows version.
5. **Review-UI** headers — patch call sites if signatures changed.
6. **Skip-win-only** — do nothing except note in your final report
   that the change is Windows-only and explain why.
7. **Added files** — for each new `.cpp` under a cross-platform
   module, add it to the SDL CMake build.
8. **Removed files** — delete from the fork (after confirming nothing
   in `src/AltirraSDL/` references it).

### 3.3 Verify

After editing:

- Build: `cd <repo root> && ./build.sh` (or the CMake invocation
  documented in `BUILD.md`).
- Fix every compile error before calling the sync complete.
- Never use backwards-compat shims to silence errors — if upstream
  renamed a type, function, enum, or member, rename it at every fork
  call site too.
- Run the ImGui test-mode smoke check if available:
  `./build/src/AltirraSDL/AltirraSDL --test-mode` and `ping` /
  `query_state` via its Unix socket.

### 3.4 Report back

Produce a final report containing:

- One-line summary of the sync (OLD → NEW, files touched).
- Which 3-way files were non-trivial and why.
- Which upstream changelog bullets were *not* addressed and why
  (e.g. "Win32-only", "SDL fork already handles differently").
- Any newly-unknown classifier paths the operator should add to
  `tools/lib/filemap.py`.
- Build status.

Append a one-line entry to `syncing-with-upstream/HISTORY.md`.

## 4. Guard-rails

- **Never** edit files in the OLD or NEW snapshot directories. They
  are read-only references.
- **Never** touch `src/AltirraSDL/**` unless the change is (a)
  reflecting a Win32 UI change in ImGui, (b) updating a call site
  because a core signature changed upstream, or (c) adding a new
  core `.cpp` to the SDL target's source list.
- **Never** silently drop a fork edit during a 3-way merge. If you
  can't preserve it, surface the conflict in your report.
- **Never** commit without an approved plan.
- **Never** rerun `sync_diff.sh` mid-sync — the intermediate report
  files are your stable reference.
- Prefer small, focused commits: one for trivial copies, one per
  module for three-way merges, one for UI reflection, one for
  CMake/build-list updates. This makes review bearable.

## 5. Tools you will use

- `Read`, `Grep`, `Glob` — exploring FORK and the snapshot dirs.
- `Edit`, `Write` — modifying FORK files only.
- `Bash` — running the build, rerunning `classify_changes.py` after
  extending `filemap.py`, inspecting individual diffs in
  `reports/<…>/diffs/`.
- Do **not** invoke `sync_diff.sh` yourself — treat the existing
  report as authoritative.

## 6. What "done" looks like

- Every file in `03_three_way.txt` has an outcome: merged, skipped
  (with reason), or flagged.
- Every user-visible change bullet in `00_changelog.txt` is either
  reflected in the ImGui UI or explicitly deferred with a reason.
- `./build.sh` completes without errors.
- A one-line entry has been appended to `HISTORY.md`.

Good luck. The repo's core rule is worth repeating:

> The emulation core is Altirra. All the hard work is Avery Lee's.
> Keep the delta small, keep the port faithful.
