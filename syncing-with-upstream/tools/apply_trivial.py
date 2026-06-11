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

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent / "lib"))
import report_safety  # noqa: E402


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

    NEW normally lives as a sibling of FORK, named
    ``Altirra-<NEW_LABEL>-src``.  For local workspaces that keep
    snapshots inside the fork checkout, a matching child directory under
    FORK is also accepted.  Ambiguous matches still require ``--new``.
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
    def snapshot_candidates(parent: pathlib.Path) -> list[pathlib.Path]:
        return [
            p for p in parent.iterdir()
            if p.is_dir() and new_label in p.name and "Altirra" in p.name
        ]

    # Prefer the documented sibling layout, but also support the in-repo
    # snapshot layout used by some local workspaces.
    candidates = snapshot_candidates(fork.parent) + snapshot_candidates(fork)
    if not candidates:
        raise SystemExit(
            f"cannot find NEW snapshot for label '{new_label}' next to or inside fork. "
            f"Pass --new explicitly."
        )
    if len(candidates) > 1:
        raise SystemExit(
            f"multiple snapshot candidates for '{new_label}': {candidates}. "
            f"Pass --new explicitly."
        )
    return candidates[0], fork


def main() -> int:
    args = parse_args()
    report_dir: pathlib.Path = args.report_dir.resolve()
    report_safety.require_report_dir(report_dir)
    report_safety.require_not_marked_invalid(report_dir, "apply")
    required = [
        "01_upstream_changed.txt",
        "02_fork_changed.txt",
        "03_three_way.txt",
        "04_trivial_copy.txt",
    ]
    if args.include_added:
        required.append("05_added_in_new.txt")
    report_safety.require_report_files(report_dir, required)
    report_safety.require_not_all_added_report(report_dir)

    report_inputs = report_safety.read_report_inputs(report_dir)
    sync_trees = report_safety.report_sync_trees(report_inputs)
    new = args.new.resolve() if args.new else None
    fork = args.fork.resolve() if args.fork else None
    if new is None and report_inputs.get("NEW"):
        new = pathlib.Path(report_inputs["NEW"]).resolve()
    if fork is None and report_inputs.get("FORK"):
        fork = pathlib.Path(report_inputs["FORK"]).resolve()
    if new is None or fork is None:
        inferred_new, inferred_fork = infer_paths(report_dir)
        new = new or inferred_new
        fork = fork or inferred_fork

    validation_inputs = dict(report_inputs)
    validation_inputs["NEW"] = str(new)
    validation_inputs["FORK"] = str(fork)
    report_safety.validate_report_input_roots(report_dir, validation_inputs, keys=("NEW", "FORK"))
    sync_trees = report_safety.report_sync_trees(validation_inputs)

    if not new.is_dir():
        print(f"error: NEW snapshot not found: {new}", file=sys.stderr)
        return 2
    if not fork.is_dir():
        print(f"error: FORK not found: {fork}", file=sys.stderr)
        return 2
    report_safety.require_sync_trees(new, "NEW snapshot", sync_trees)
    report_safety.require_sync_trees(fork, "FORK tree", sync_trees)

    print(f"[apply] NEW  = {new}")
    print(f"[apply] FORK = {fork}")
    print(f"[apply] report = {report_dir}")

    trivial = report_safety.read_list(report_dir / "04_trivial_copy.txt")
    added = report_safety.read_list(report_dir / "05_added_in_new.txt") if args.include_added else []
    report_safety.require_report_paths_in_trees(trivial + added, sync_trees)

    fork_changed = {p for p in report_safety.read_list(report_dir / "02_fork_changed.txt")}
    three_way = {p for p in report_safety.read_list(report_dir / "03_three_way.txt")}

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
