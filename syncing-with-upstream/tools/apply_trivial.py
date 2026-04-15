#!/usr/bin/env python3
"""Copy trivial-bucket files from NEW into FORK.

"Trivial" here means: upstream modified the file, but the fork has left
it untouched since the last sync.  In that case we can just overwrite
the fork's copy with NEW.  The script is deliberately conservative —
it refuses to copy anything that is also in the fork-changed list, and
it refuses to run if the report is inconsistent with the file system.

Usage:
    apply_trivial.py <report_dir> [--dry-run] [--include-added]

    <report_dir>     Path to reports/<OLD>__to__<NEW>/
    --dry-run        Print what would happen, do not touch files.
    --include-added  Also copy files from 05_added_in_new.txt (off by default).

The report dir must have been produced by ``sync_diff.sh``.  When no
``--new`` / ``--fork`` arguments are given, both are inferred from the
report dir's position on disk, assuming the layout ``sync_diff.sh``
creates:

    <FORK>/syncing-with-upstream/reports/<OLD_LABEL>__to__<NEW_LABEL>/

FORK is therefore three parents up from the report dir, and NEW is
expected as a sibling of FORK whose basename contains both
``Altirra`` and the <NEW_LABEL>.  If that layout doesn't match, pass
``--new`` and ``--fork`` explicitly.
"""

from __future__ import annotations

import argparse
import pathlib
import shutil
import sys


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("report_dir", type=pathlib.Path)
    p.add_argument("--new", type=pathlib.Path, default=None,
                   help="Path to NEW snapshot (default: inferred from report dir name)")
    p.add_argument("--fork", type=pathlib.Path, default=None,
                   help="Path to FORK root (default: inferred from report dir position)")
    p.add_argument("--include-added", action="store_true",
                   help="Also copy files listed in 05_added_in_new.txt")
    p.add_argument("--dry-run", action="store_true")
    return p.parse_args()


def infer_paths(report_dir: pathlib.Path) -> tuple[pathlib.Path, pathlib.Path]:
    """Guess NEW and FORK paths from the report dir location.

    Layout expected (the one ``sync_diff.sh`` creates):
        <FORK>/syncing-with-upstream/reports/<OLD>__to__<NEW>/

    NEW lives as a sibling of FORK, named ``Altirra-<NEW_LABEL>-src``.
    """
    report_dir = report_dir.resolve()
    fork = report_dir.parent.parent.parent  # …/<FORK>
    name = report_dir.name
    _, sep, new_label = name.partition("__to__")
    if not sep or not new_label:
        raise SystemExit(
            f"cannot infer NEW label from report dir name: {name!r} "
            "(expected '<OLD>__to__<NEW>')"
        )
    # Look for a sibling directory whose basename contains the label.
    parent = fork.parent
    candidates = [p for p in parent.iterdir() if p.is_dir() and new_label in p.name and "Altirra" in p.name]
    if not candidates:
        raise SystemExit(
            f"cannot find NEW snapshot for label '{new_label}' next to fork. "
            f"Pass --new explicitly."
        )
    if len(candidates) > 1:
        raise SystemExit(
            f"multiple snapshot candidates for '{new_label}': {candidates}. "
            f"Pass --new explicitly."
        )
    return candidates[0], fork


def read_list(path: pathlib.Path) -> list[str]:
    if not path.exists():
        return []
    out: list[str] = []
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line:
            continue
        file_path, _, _status = line.partition("\t")
        out.append(file_path)
    return out


def main() -> int:
    args = parse_args()
    report_dir: pathlib.Path = args.report_dir.resolve()
    if not report_dir.is_dir():
        print(f"error: report dir not found: {report_dir}", file=sys.stderr)
        return 2

    new = args.new.resolve() if args.new else None
    fork = args.fork.resolve() if args.fork else None
    if new is None or fork is None:
        inferred_new, inferred_fork = infer_paths(report_dir)
        new = new or inferred_new
        fork = fork or inferred_fork

    if not new.is_dir():
        print(f"error: NEW snapshot not found: {new}", file=sys.stderr)
        return 2
    if not fork.is_dir():
        print(f"error: FORK not found: {fork}", file=sys.stderr)
        return 2

    print(f"[apply] NEW  = {new}")
    print(f"[apply] FORK = {fork}")
    print(f"[apply] report = {report_dir}")

    trivial = read_list(report_dir / "04_trivial_copy.txt")
    added = read_list(report_dir / "05_added_in_new.txt") if args.include_added else []

    fork_changed = {p for p in read_list(report_dir / "02_fork_changed.txt")}
    three_way = {p for p in read_list(report_dir / "03_three_way.txt")}

    to_copy: list[str] = []
    for path in trivial + added:
        if path in fork_changed or path in three_way:
            print(f"  skip (conflicts with fork changes): {path}")
            continue
        to_copy.append(path)

    if not to_copy:
        print("[apply] nothing to do")
        return 0

    if args.dry_run:
        print(f"[apply] DRY RUN — would copy {len(to_copy)} files:")
        for p in to_copy:
            print(f"  {p}")
        return 0

    copied = 0
    for path in to_copy:
        src = new / path
        dst = fork / path
        if not src.exists():
            print(f"  missing in NEW, skipping: {path}")
            continue
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
        copied += 1
    print(f"[apply] copied {copied} files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
