# Syncing AltirraSDL with Upstream Altirra

This folder contains the documented process, tooling, and AI-agent prompt
used to bring AltirraSDL in sync with a new upstream Altirra release.

Upstream Altirra has no public git repository — it is distributed as
zipped source snapshots on
[virtualdub.org/altirra.html](https://www.virtualdub.org/altirra.html).
Each release is delivered as a self-contained folder (e.g.
`Altirra-4.50-test9-src`).

**The sync is strictly one-way: upstream → fork.** The upstream author
does not accept patches from this fork, so none of the tooling below
produces patches to send upstream, and none of it ever writes into the
OLD or NEW snapshot directories — they are read-only references used
to compute a merge result that lands exclusively inside the fork.

Sync therefore works by **three-way comparison** between:

| Role | Path (example)                         |
|------|----------------------------------------|
| OLD  | `../Altirra-4.50-test8-src` (last synced release)   |
| NEW  | `../Altirra-4.50-test9-src` (target release)        |
| FORK | `..` (the AltirraSDL root)                           |

The sync pipeline:

```
  diff(OLD, NEW)  →  pick files that changed upstream
  diff(OLD, FORK) →  find files the fork has already modified
  intersect       →  files where a 3-way merge is required
  everything else →  can be copied straight from NEW into FORK
```

The rest of this document explains how to drive that pipeline with the
tooling in `tools/` and how to hand off the result to an LLM coding agent
using `prompts/PROMPT.md`.

---

## Folder layout

```
syncing-with-upstream/
├── README.md                 ← you are here
├── GUIDE.md                  ← step-by-step manual for a human operator
├── CHECKLIST.md              ← quick punch-list for each sync
├── prompts/
│   └── PROMPT.md             ← self-contained LLM brief (fed to the agent)
├── tools/
│   ├── sync_diff.sh          ← three-way diff driver, produces reports/
│   ├── classify_changes.py   ← categorises files (core / ui / win-only / …)
│   ├── apply_trivial.py      ← copies no-conflict files from NEW to FORK
│   ├── extract_changelog.sh  ← extracts the NEW release's changes.txt delta
│   └── lib/
│       ├── common.sh         ← shared bash helpers (path resolution, etc.)
│       ├── compute_diffs.py  ← fast Python diff-set producer
│       └── filemap.py        ← path→module classification rules
└── reports/                  ← generated per-sync, git-ignored by default
    └── <OLD>__to__<NEW>/
        ├── 00_changelog.txt          ← upstream's own change log delta
        ├── 01_upstream_changed.txt   ← files where OLD≠NEW
        ├── 02_fork_changed.txt       ← files where OLD≠FORK
        ├── 03_three_way.txt          ← intersection (manual merge needed)
        ├── 04_trivial_copy.txt       ← NEW→FORK safe straight copy
        ├── 05_added_in_new.txt       ← new files to bring over
        ├── 06_removed_in_new.txt     ← files upstream deleted
        ├── 07_classified.md          ← files grouped by module / subsystem
        ├── diffs/                    ← per-file unified diffs
        │   ├── <path>.upstream.diff  ← OLD vs NEW (what upstream changed)
        │   ├── <path>.fork.diff      ← OLD vs FORK (what the fork changed)
        │   └── <path>.full.diff      ← FORK vs NEW (combined view)
        └── SUMMARY.md                ← human-readable report
```

The `tools/` are version-agnostic: they take OLD / NEW / FORK paths as
arguments and do not hard-code any Altirra version number.

---

## TL;DR

```bash
cd AltirraSDL/syncing-with-upstream

# Produce the reports/ folder for this sync
./tools/sync_diff.sh \
    --old  ../../Altirra-4.50-test8-src \
    --new  ../../Altirra-4.50-test9-src \
    --fork ../

# (Optional) auto-copy files that only changed upstream
./tools/apply_trivial.py reports/<OLD>__to__<NEW>

# Hand the rest to the LLM
cat prompts/PROMPT.md   # copy into the agent's initial message
```

See `GUIDE.md` for the full story, `CHECKLIST.md` for the condensed one.
