#!/usr/bin/env python3
"""Read a report dir produced by ``sync_diff.sh`` and write
``07_classified.md``: the three-way files grouped by module and role.

Usage:
    classify_changes.py <report_dir>
"""

from __future__ import annotations

import pathlib
import sys
from collections import defaultdict


def load_filemap():
    here = pathlib.Path(__file__).resolve().parent
    sys.path.insert(0, str(here / "lib"))
    import filemap  # noqa: E402  (intentional late import)
    return filemap


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent / "lib"))
import report_safety  # noqa: E402


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: classify_changes.py <report_dir>", file=sys.stderr)
        return 2

    report_dir = pathlib.Path(sys.argv[1]).resolve()
    report_safety.require_report_dir(report_dir)
    report_safety.require_not_marked_invalid(report_dir, "classify")
    required = [
        "01_upstream_changed.txt",
        "03_three_way.txt",
        "04_trivial_copy.txt",
        "05_added_in_new.txt",
        "06_removed_in_new.txt",
    ]
    report_safety.require_report_files(report_dir, required)
    report_safety.require_not_all_added_report(report_dir)
    report_inputs = report_safety.validate_report_input_roots(report_dir)
    sync_trees = report_safety.report_sync_trees(report_inputs)

    filemap = load_filemap()

    three_way = report_safety.read_tab(report_dir / "03_three_way.txt")
    added     = report_safety.read_tab(report_dir / "05_added_in_new.txt")
    removed   = report_safety.read_tab(report_dir / "06_removed_in_new.txt")
    trivial   = report_safety.read_tab(report_dir / "04_trivial_copy.txt")
    report_safety.require_report_paths_in_trees(
        [path for path, _status in three_way + added + removed + trivial],
        sync_trees,
    )

    # Group three-way files by (role, module).
    grouped: dict[tuple[str, str], list[tuple[str, str, str]]] = defaultdict(list)
    role_desc = {
        "port":           "**Port:** apply upstream changes into the fork (platform-agnostic).",
        "reflect-in-ui":  "**Reflect in UI:** understand change, mirror user-visible behaviour in Dear ImGui frontend.",
        "review-ui":      "**Review UI:** may or may not have SDL equivalents; inspect before porting.",
        "copy-verbatim":  "**Copy verbatim:** upstream-authoritative; just copy.",
        "skip-win-only":  "**Skip:** Win32-only path, not built on SDL — but read for context.",
        "inform-only":    "**Inform-only:** e.g. .vcxproj; SDL build uses CMake.",
        "port-if-wired":  "**Port-if-wired:** port when the corresponding target exists in AltirraSDL.",
        "unknown":        "**Unknown:** unclassified — extend `tools/lib/filemap.py`.",
    }

    for path, status in three_way:
        cls = filemap.classify(path)
        grouped[(cls.role, cls.module)].append((path, status, cls.note))

    # Also classify added/removed so they appear in the same view.
    for path, _ in added:
        cls = filemap.classify(path)
        grouped[(cls.role, cls.module)].append((path, "ADDED", cls.note))
    for path, _ in removed:
        cls = filemap.classify(path)
        grouped[(cls.role, cls.module)].append((path, "REMOVED", cls.note))

    lines: list[str] = []
    lines.append("# Three-way files by role and module")
    lines.append("")
    lines.append(f"Report dir: `{report_dir.name}`")
    lines.append("")
    lines.append(f"- Three-way files: **{len(three_way)}**")
    lines.append(f"- Added in NEW:    **{len(added)}**")
    lines.append(f"- Removed in NEW:  **{len(removed)}**")
    lines.append(f"- Trivial copies:  **{len(trivial)}** (see `04_trivial_copy.txt`)")
    lines.append("")

    role_order = [
        "port", "reflect-in-ui", "review-ui", "copy-verbatim",
        "port-if-wired", "skip-win-only", "inform-only", "unknown",
    ]

    for role in role_order:
        modules = sorted({m for (r, m) in grouped if r == role})
        if not modules:
            continue
        lines.append(f"## {role}")
        lines.append("")
        lines.append(role_desc.get(role, ""))
        lines.append("")
        for module in modules:
            entries = sorted(grouped[(role, module)])
            lines.append(f"### {module}  ({len(entries)})")
            lines.append("")
            for path, status, note in entries:
                suffix = f" — {note}" if note else ""
                lines.append(f"- `{path}`  _{status}_{suffix}")
            lines.append("")

    (report_dir / "07_classified.md").write_text("\n".join(lines) + "\n")
    print(f"[sync] classified report -> {report_dir / '07_classified.md'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
