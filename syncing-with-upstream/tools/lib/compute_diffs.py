#!/usr/bin/env python3
"""Compute the core diff sets used by the AltirraSDL sync pipeline.

This is called from ``tools/sync_diff.sh``. It is kept as a separate
script so the expensive file-tree walking runs in Python rather than a
bash per-line parse loop (which is pathologically slow on trees with
thousands of files).

Inputs:
    --old       OLD snapshot root
    --new       NEW snapshot root
    --fork      FORK working tree root
    --trees     space-separated list of subpaths to walk (e.g. "src")
    --report-dir destination for the .txt outputs

Outputs (tab-separated `path\tstatus`, one per line):
    01_upstream_changed.txt   OLD vs NEW
    02_fork_changed.txt       OLD vs FORK (filtered: fork-only paths dropped)
    03_three_way.txt          intersection of (1) and (2) by path
    04_trivial_copy.txt       MODIFIED entries in (1) whose path is NOT in (2)
    05_added_in_new.txt       ADDED entries in (1)  (one path per line)
    06_removed_in_new.txt     REMOVED entries in (1) (one path per line)
"""

from __future__ import annotations

import argparse
import filecmp
import os
import pathlib
import sys


def load_filemap():
    here = pathlib.Path(__file__).resolve().parent
    sys.path.insert(0, str(here))
    import filemap  # noqa: E402  (intentional late import)
    return filemap


def walk_rel(root: pathlib.Path, tree: str) -> set[str]:
    """Return the set of files (as root-relative POSIX paths) under root/tree."""
    base = root / tree
    if not base.is_dir():
        return set()
    out: set[str] = set()
    for dirpath, _dirs, files in os.walk(base):
        rel_dir = pathlib.Path(dirpath).relative_to(root)
        for f in files:
            out.add(str(rel_dir / f).replace("\\", "/"))
    return out


def compare_trees(
    left: pathlib.Path, right: pathlib.Path, trees: list[str]
) -> list[tuple[str, str]]:
    """Compare every file under each tree. Returns (path, status) with
    status ∈ {MODIFIED, ADDED, REMOVED} where:

    * ADDED    — file exists in ``right`` but not ``left``
    * REMOVED  — file exists in ``left``  but not ``right``
    * MODIFIED — file exists in both but contents differ
    """
    out: list[tuple[str, str]] = []
    for tree in trees:
        l = walk_rel(left, tree)
        r = walk_rel(right, tree)
        for p in sorted(r - l):
            out.append((p, "ADDED"))
        for p in sorted(l - r):
            out.append((p, "REMOVED"))
        common = sorted(l & r)
        for p in common:
            # Use shallow=False so we actually compare content, not mtime.
            if not filecmp.cmp(str(left / p), str(right / p), shallow=False):
                out.append((p, "MODIFIED"))
    return out


def write_tab(path: pathlib.Path, entries: list[tuple[str, str]]) -> None:
    path.write_text("\n".join(f"{p}\t{s}" for p, s in entries) + ("\n" if entries else ""))


def write_lines(path: pathlib.Path, lines: list[str]) -> None:
    path.write_text("\n".join(lines) + ("\n" if lines else ""))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--old",  type=pathlib.Path, required=True)
    ap.add_argument("--new",  type=pathlib.Path, required=True)
    ap.add_argument("--fork", type=pathlib.Path, required=True)
    ap.add_argument("--report-dir", type=pathlib.Path, required=True)
    ap.add_argument("--trees", nargs="+", required=True)
    args = ap.parse_args()

    filemap = load_filemap()
    report = args.report_dir

    upstream = compare_trees(args.old, args.new, args.trees)
    fork_raw = compare_trees(args.old, args.fork, args.trees)

    # Drop fork-only paths from the fork-changed list; they are not
    # part of upstream and must never appear in the intersection.
    fork = [(p, s) for (p, s) in fork_raw if filemap.classify(p).role != "fork-only"]

    write_tab(report / "01_upstream_changed.txt", upstream)
    write_tab(report / "02_fork_changed.txt",     fork)

    fork_paths = {p for p, _ in fork}

    three_way = [(p, s) for (p, s) in upstream if p in fork_paths]
    write_tab(report / "03_three_way.txt", three_way)

    trivial = [(p, s) for (p, s) in upstream
               if p not in fork_paths and s == "MODIFIED"]
    write_tab(report / "04_trivial_copy.txt", trivial)

    added   = [p for (p, s) in upstream if s == "ADDED"]
    removed = [p for (p, s) in upstream if s == "REMOVED"]
    write_lines(report / "05_added_in_new.txt", added)
    write_lines(report / "06_removed_in_new.txt", removed)

    print(f"[sync] upstream={len(upstream)} fork={len(fork)} "
          f"three-way={len(three_way)} trivial={len(trivial)} "
          f"added={len(added)} removed={len(removed)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
