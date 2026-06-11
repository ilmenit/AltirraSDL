"""Shared safety checks for upstream sync report consumers."""

from __future__ import annotations

import pathlib

DEFAULT_SYNC_TREES = ("src",)
INVALID_MARKER = "INVALID_REPORT_DO_NOT_USE.md"


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


def read_tab(path: pathlib.Path) -> list[tuple[str, str]]:
    out: list[tuple[str, str]] = []
    if not path.exists():
        return out

    for line in path.read_text().splitlines():
        line = line.strip()
        if not line:
            continue
        file_path, _, status = line.partition("\t")
        out.append((file_path, status or "MODIFIED"))

    return out


def read_report_inputs(report_dir: pathlib.Path) -> dict[str, str]:
    path = report_dir / "REPORT_INPUTS.txt"
    if not path.exists():
        return {}

    out: dict[str, str] = {}
    for line in path.read_text().splitlines():
        key, sep, value = line.partition("=")
        if sep:
            out[key] = value

    return out


def snapshot_label(path: str) -> str:
    base = pathlib.Path(path).name
    if base.startswith("Altirra-"):
        base = base[len("Altirra-"):]
    if base.endswith("-src"):
        base = base[:-len("-src")]
    return base


def report_dir_labels(report_dir: pathlib.Path) -> tuple[str, str] | None:
    old_label, sep, new_label = report_dir.name.partition("__to__")
    if not sep or not old_label or not new_label:
        return None
    return old_label, new_label


def require_report_inputs_match_dir(report_dir: pathlib.Path, report_inputs: dict[str, str]) -> None:
    labels = report_dir_labels(report_dir)
    if not labels:
        return

    old_label, new_label = labels
    if report_inputs.get("OLD") and snapshot_label(report_inputs["OLD"]) != old_label:
        raise SystemExit(
            f"error: REPORT_INPUTS.txt OLD does not match report dir label {old_label!r}"
        )
    if report_inputs.get("NEW") and snapshot_label(report_inputs["NEW"]) != new_label:
        raise SystemExit(
            f"error: REPORT_INPUTS.txt NEW does not match report dir label {new_label!r}"
        )


def report_sync_trees(report_inputs: dict[str, str]) -> tuple[str, ...]:
    trees = tuple(tree for tree in report_inputs.get("SYNC_TREES", "").split() if tree)
    return trees or DEFAULT_SYNC_TREES


def require_sync_trees(root: pathlib.Path, role: str, trees: tuple[str, ...] = DEFAULT_SYNC_TREES) -> None:
    for tree in trees:
        path = root / tree
        if not path.is_dir():
            raise SystemExit(
                f"{role} is not a usable sync root: missing {tree}/ under {root}"
            )
        if not any(p.is_file() for p in path.rglob("*")):
            raise SystemExit(
                f"{role} is not a usable sync root: no files under {path}"
            )


def require_report_paths_in_trees(paths: list[str], trees: tuple[str, ...]) -> None:
    for path in paths:
        if not path:
            raise SystemExit("error: empty report path")
        if "\\" in path:
            raise SystemExit(f"error: unsafe report path uses backslashes: {path}")
        parts = path.split("/")
        if path.startswith("/") or any(part in ("", ".", "..") for part in parts):
            raise SystemExit(f"error: unsafe report path outside sync root: {path}")
        if not any(path == tree or path.startswith(f"{tree}/") for tree in trees):
            raise SystemExit(
                f"error: report path is outside configured sync trees: {path}"
            )


def require_report_dir(report_dir: pathlib.Path) -> None:
    if not report_dir.is_dir():
        raise SystemExit(f"error: report dir not found: {report_dir}")


def require_not_marked_invalid(report_dir: pathlib.Path, action: str) -> None:
    invalid_marker = report_dir / INVALID_MARKER
    if invalid_marker.exists():
        raise SystemExit(
            f"error: refusing to {action} invalid report: {report_dir}\n"
            f"       see {invalid_marker}"
        )


def require_report_files(report_dir: pathlib.Path, names: list[str]) -> None:
    missing = [name for name in names if not (report_dir / name).is_file()]
    if missing:
        raise SystemExit(
            f"error: report dir is missing required files: {', '.join(missing)}"
        )


def report_has_only_added_upstream_changes(report_dir: pathlib.Path) -> bool:
    upstream = read_tab(report_dir / "01_upstream_changed.txt")
    return bool(upstream) and all(status == "ADDED" for _path, status in upstream)


def require_not_all_added_report(report_dir: pathlib.Path) -> None:
    if report_has_only_added_upstream_changes(report_dir):
        raise SystemExit(
            "error: refusing suspicious report: every upstream-changed path is ADDED\n"
            "       OLD snapshot probably did not contain a usable source tree; "
            "regenerate the report from a real OLD source snapshot"
        )


def validate_report_input_roots(
    report_dir: pathlib.Path,
    report_inputs: dict[str, str] | None = None,
    keys: tuple[str, ...] = ("OLD", "NEW", "FORK"),
) -> dict[str, str]:
    if report_inputs is None:
        report_inputs = read_report_inputs(report_dir)

    filtered_inputs = {
        key: value
        for key, value in report_inputs.items()
        if key not in ("OLD", "NEW", "FORK") or key in keys
    }

    require_report_inputs_match_dir(report_dir, filtered_inputs)
    trees = report_sync_trees(report_inputs)

    for key, role in (("OLD", "OLD snapshot"), ("NEW", "NEW snapshot"), ("FORK", "FORK tree")):
        if key in keys and report_inputs.get(key):
            require_sync_trees(pathlib.Path(report_inputs[key]), role, trees)

    return report_inputs
