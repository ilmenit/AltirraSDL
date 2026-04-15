# AltirraSDL Upstream Sync — Operator Guide

This guide is the long-form, step-by-step companion to `README.md`. It is
intentionally version-agnostic: nothing here hard-codes "test8" or
"test9". Use it for the current `test8 → test9` transition, and for any
future Altirra release (minor, major, or prerelease).

## Mental model

AltirraSDL is **not** a rewrite — it is a fork that replaces the Win32
frontend with SDL3 + Dear ImGui. The emulation core, the peripheral
device code, the debugger backend, the disk/tape/cartridge I/O and the
kernel ROMs all come straight from upstream Altirra. To stay honest to
"all the hard work is Avery Lee's" (see `README.md`), we must keep that
upstream delta as small as possible and re-apply upstream fixes promptly.

Upstream is distributed as zipped source snapshots, not as a git
repository, so syncing is a structured diff-and-merge operation rather
than a git rebase. The process below makes that operation mechanical,
auditable, and safe to redo.

### Sync direction: one-way, upstream → fork

The upstream author does not accept patches from this fork. Every
artefact this tooling produces — the diff listings, the per-file
`.upstream.diff` / `.fork.diff` / `.full.diff` files, the classification
report — exists solely to guide changes **into** the fork. The tooling
never writes into the OLD or NEW snapshot directories (they are
treated as read-only references), and there is no "send patches back
upstream" path to design for. If a future sync ever needs such a path,
it is a separate project; do not retrofit it into these scripts.

## The three inputs

Every sync has three directories:

| Alias | Role                         | Example path                          |
|-------|------------------------------|---------------------------------------|
| OLD   | Last upstream release we're currently based on | `Altirra-4.50-test8-src` |
| NEW   | Upstream release we're moving to               | `Altirra-4.50-test9-src` |
| FORK  | This repo's current working tree               | `AltirraSDL`             |

Two assumptions the tooling relies on:

1. **OLD matches the upstream snapshot we previously synced with.**
   Concretely, `diff -r OLD/src FORK/src` should only show our own
   deliberate modifications (and the files we added under
   `src/AltirraSDL/`, `tests/`, etc.).
2. **NEW is an unpacked, untouched copy of the upstream zip.** Do not
   edit NEW before running the tooling.

If assumption (1) breaks (e.g. the last sync was messy), run the tools
anyway — they will surface the discrepancies so you can clean them up.

## The four change classes

Every file the sync tool emits ends up in exactly one of these buckets:

| Bucket         | Definition (per file)              | Typical action                                  |
|----------------|------------------------------------|--------------------------------------------------|
| **trivial**    | OLD≠NEW, OLD=FORK                  | Straight copy `NEW→FORK`                         |
| **three-way**  | OLD≠NEW, OLD≠FORK                  | Manual/LLM 3-way merge                           |
| **fork-only**  | OLD=NEW, OLD≠FORK                  | Leave fork as-is (upstream untouched)            |
| **unchanged**  | OLD=NEW, OLD=FORK                  | Ignore                                            |

Files in `src/AltirraSDL/`, `cmake/`, `syncing-with-upstream/`, the
top-level `CMakeLists.txt`, `BUILD.md`, `README.md` and similar
"fork-only" paths are expected to be in the **fork-only** bucket and the
tooling filters them out of the upstream diff entirely.

## Module classification

For the three-way files, we further classify by Altirra module to drive
merge priority. This drives `reports/.../07_classified.md` and mirrors
the architecture section of `CLAUDE.md`.

| Class            | Examples                                                  | Sync effort                                    |
|------------------|-----------------------------------------------------------|------------------------------------------------|
| core-emulation   | `src/Altirra/source/{antic,gtia,pokey,cpu,simulator}.cpp`  | Always port — platform-agnostic                |
| core-lib         | `src/ATCore/**`, `src/ATEmulation/**`, `src/ATIO/**`, `src/h/at/**`, `src/h/vd2/system/**`, `src/Kasumi/**`, `src/system/**` | Always port       |
| kernel-rom       | `src/Kernel/**`                                            | Always port (rebuild ROM afterwards)           |
| cmd              | `src/Altirra/source/cmd*.cpp`                              | Reflect user-visible behaviour in ImGui menus  |
| ui-win32         | `src/Altirra/source/ui*.cpp`, `src/ATNativeUI/**`, `src/VDDisplay/**`, `src/Dita/**` | Translate to ImGui only if user-facing |
| win-only         | `src/Altirra/source/*_win32.cpp`, Direct3D, XAudio, Winsock, `src/Riza/**`, `src/Tessa/**`, `src/ATAppBase/**` | Skip — SDL build doesn't compile them |
| build-meta       | `.vcxproj`, `.filters`, `Altirra.rc`, `resource.h`         | Informational; SDL build uses its own CMake    |
| docs             | `changes.txt`, `romset.html`                               | Copy verbatim                                   |
| autogen          | `src/Altirra/autogen/**`                                   | Copy verbatim                                   |
| tests            | `src/ATTest/**`                                            | Port if test target is wired up                 |

## End-to-end procedure

### 1. Prerequisites

- Python 3.10+ (for the classification scripts)
- `diff`, `git`, `bash` (standard POSIX)
- A clean or committed working tree — you want to be able to `git diff`
  the sync as one logical unit afterwards.

### 2. Commit or stash current work

Start the sync on a dedicated branch:

```bash
git checkout -b sync/altirra-<new-version>
git status   # must be clean
```

### 3. Verify the OLD snapshot matches what we previously synced

```bash
diff -rq <OLD>/src <FORK>/src \
    | grep -v '^Only in <FORK>' \
    | grep -v 'AltirraSDL' \
    | head
```

You should see only the fork's own edits to shared files (e.g. the
couple of `#ifdef`-style portability tweaks in core files). If anything
unexpected shows up, investigate *before* running the sync — don't let
the tool paper over prior drift.

### 4. Generate the reports

```bash
cd AltirraSDL/syncing-with-upstream

./tools/sync_diff.sh \
    --old  ../../Altirra-4.50-<OLD_VERSION>-src \
    --new  ../../Altirra-4.50-<NEW_VERSION>-src \
    --fork ..
```

This writes `reports/<OLD>__to__<NEW>/` with:

- `SUMMARY.md` — start here
- `00_changelog.txt` — upstream's own change log delta
- `01_upstream_changed.txt` — every file where OLD≠NEW
- `02_fork_changed.txt` — every file where OLD≠FORK
- `03_three_way.txt` — files in both sets (merge required)
- `04_trivial_copy.txt` — safe to copy NEW→FORK
- `05_added_in_new.txt` — new files upstream added
- `06_removed_in_new.txt` — files upstream deleted
- `07_classified.md` — three-way files grouped by module
- `diffs/<path>.upstream.diff` — OLD vs NEW (the patch upstream authored)
- `diffs/<path>.fork.diff` — OLD vs FORK (the fork's local edits)
- `diffs/<path>.full.diff` — FORK vs NEW (combined, for cross-checking)

### 5. Skim the changelog first

```bash
less reports/<OLD>__to__<NEW>/00_changelog.txt
```

Upstream's `changes.txt` is the most concise description of what
actually shipped. Bullet points starting with `Display:`, `GTIA:`, `UI:`
often correspond 1:1 to files in the report.

### 6. Apply the trivial copies

```bash
./tools/apply_trivial.py reports/<OLD>__to__<NEW>
# then, if you want to also bring over files that are *new* in NEW
# (nothing in the fork for them to conflict with):
./tools/apply_trivial.py reports/<OLD>__to__<NEW> --include-added
```

By default only `04_trivial_copy.txt` is applied — those are files
upstream modified and the fork left untouched. Adding
`--include-added` additionally copies files listed in
`05_added_in_new.txt`. Directory structure is preserved. The script
refuses to touch anything the fork has edited (`02_fork_changed.txt`)
or any three-way file, so running it is safe by construction.

Commit this step on its own. Stage explicitly rather than using
`git add -A` to avoid accidentally capturing untracked local state
(build artefacts, secrets, etc.):

```bash
git add src/
git commit -m "sync(upstream): copy trivial-change files from <NEW_VERSION>"
```

### 7. Hand off the three-way merges

Open `reports/<OLD>__to__<NEW>/07_classified.md` and resolve each file.
For a heavy sync, drive it with an LLM agent using `prompts/PROMPT.md`:

```bash
# Paste this into a fresh Claude/Codex/… session:
cat prompts/PROMPT.md

# Then give the agent the path to the report:
echo "Report path: syncing-with-upstream/reports/<OLD>__to__<NEW>/"
```

The prompt instructs the agent to:

1. Read the classified report.
2. Produce a written plan for approval.
3. Only after approval, start applying changes module-by-module,
   preserving the fork's SDL-specific modifications and reflecting
   user-visible changes in the Dear ImGui frontend.

### 8. UI reflection pass

Changes the agent spots in `src/Altirra/source/cmd*.cpp` and
`src/Altirra/source/ui*.cpp` that alter *user-visible* Windows
behaviour (new menu items, dialog fields, hotkeys, etc.) must be
mirrored in the SDL ImGui frontend under `src/AltirraSDL/source/ui/`.

Rule of thumb from `CLAUDE.md`:

> The SDL3 Dear ImGui UI must replicate Windows Altirra's menus and
> dialogs faithfully — same options, same layout, same location.

The Gaming Mode frontend (`src/AltirraSDL/source/ui/mobile/`) does
**not** need every new setting — only those that are relevant to quick
gameplay (e.g. "toggle warp speed") get propagated there.

### 9. Rebuild & smoke-test

```bash
cd AltirraSDL
./build.sh       # or manual CMake invocation
./build/src/AltirraSDL/AltirraSDL   # golden-path launch test
```

If you bumped `src/Kernel/source/Shared/version.inc`, force-rebuild the
kernel ROM (MADS-based build; see `CLAUDE.md`).

### 10. Record what happened

Append a one-line summary to `syncing-with-upstream/HISTORY.md`
(create on first sync):

```
2026-04-15  4.50-test8 → 4.50-test9  123 files changed, 4 new, 0 removed
```

Commit the sync branch, open a PR, and remove the old snapshot folder
from the repo root once the PR is merged and the new snapshot becomes
the next "OLD".

## Troubleshooting

| Symptom                                | Likely cause / fix |
|---------------------------------------|--------------------|
| `02_fork_changed.txt` lists files you don't recognise as fork edits | Prior sync left drift behind. Inspect `diff OLD/<file> FORK/<file>` for each, decide whether to accept, revert, or re-sync manually before merging the current pair |
| `apply_trivial.py` prints `skip (conflicts with fork changes): …` | The path appears in `02_fork_changed.txt` or `03_three_way.txt`. That is correct behaviour — the file needs a three-way merge, not a copy |
| Kernel ROM builds differ byte-for-byte after sync | Expected if `version.inc` changed — commit the new autogen output |
| ImGui UI is missing a new menu item after sync | The agent stopped at the C++ side; re-run the UI reflection pass with `menu_default.txt` as the ground truth |
| Compiler error in `src/AltirraSDL/…` after sync | A header signature in core code changed — inspect `03_three_way.txt` for `src/h/at/**` entries, patch the SDL-side call site |
| `apply_trivial.py`: `cannot infer NEW label from report dir name` | The report dir isn't under the expected `<FORK>/syncing-with-upstream/reports/<OLD>__to__<NEW>/` layout. Pass `--new` and `--fork` explicitly |

## Design notes

**Why not just `diff -r` and merge?**
Because upstream ships 40+ files per release whose changes are
Win32-native and must be *ignored* by the SDL build, while another 40+
files are core-agnostic and must be *copied verbatim*. A raw diff
drowns the real merge work (~10–20 files per release) in noise.

**Why a Python classifier?**
Bash globs can't easily express "`src/Altirra/source/cmd*.cpp` is a
cmd, but `src/Altirra/source/cmdcart.cpp` ships gaming-mode-relevant
bits too". The classifier is a single file (`tools/lib/filemap.py`);
extend it whenever a new module appears.

**Why keep the reports under `reports/` instead of inline in the PR?**
They are reproducible from inputs and would otherwise bloat reviews.
Add `syncing-with-upstream/reports/` to `.gitignore` (the reports are
recreated on demand).
