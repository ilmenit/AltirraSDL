#!/usr/bin/env bash
#
# sync_diff.sh — produce the three-way diff reports that drive an
# AltirraSDL upstream sync.
#
# Usage:
#   sync_diff.sh --old <OLD_SNAPSHOT> --new <NEW_SNAPSHOT> --fork <FORK_ROOT>
#
# Output goes to:   syncing-with-upstream/reports/<OLD_LABEL>__to__<NEW_LABEL>/
#
# The script is fully idempotent: rerunning it overwrites the report
# folder for that specific OLD→NEW pair.

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd -P)
. "$SCRIPT_DIR/lib/common.sh"

OLD=""
NEW=""
FORK=""

require_value() {
    # $1: flag name, $2: count of remaining args including the flag itself
    [ "$2" -ge 2 ] || fatal "$1 requires a value"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --old)  require_value "$1" "$#"; OLD=$2;  shift 2 ;;
        --new)  require_value "$1" "$#"; NEW=$2;  shift 2 ;;
        --fork) require_value "$1" "$#"; FORK=$2; shift 2 ;;
        -h|--help)
            sed -n '2,20p' "$0"
            exit 0 ;;
        *) fatal "unknown argument: $1" ;;
    esac
done

[ -n "$OLD" ]  || fatal "--old is required"
[ -n "$NEW" ]  || fatal "--new is required"
[ -n "$FORK" ] || fatal "--fork is required"

OLD=$(abs_path "$OLD")
NEW=$(abs_path "$NEW")
FORK=$(abs_path "$FORK")

require_sync_trees "OLD snapshot" "$OLD"
require_sync_trees "NEW snapshot" "$NEW"
require_sync_trees "FORK tree" "$FORK"

OLD_LABEL=$(snapshot_label "$OLD")
NEW_LABEL=$(snapshot_label "$NEW")
REPORTS_ROOT=$(cd "$SCRIPT_DIR/.." && pwd -P)/reports
REPORT_DIR=$REPORTS_ROOT/${OLD_LABEL}__to__${NEW_LABEL}

info "OLD  = $OLD   ($OLD_LABEL)"
info "NEW  = $NEW   ($NEW_LABEL)"
info "FORK = $FORK"
info "out  = $REPORT_DIR"

rm -rf "$REPORT_DIR"
mkdir -p "$REPORT_DIR/diffs"

{
    printf 'OLD=%s\n' "$OLD"
    printf 'NEW=%s\n' "$NEW"
    printf 'FORK=%s\n' "$FORK"
    printf 'SYNC_TREES=%s\n' "$SYNC_TREES"
} > "$REPORT_DIR/REPORT_INPUTS.txt"

# --- 00: changelog delta -------------------------------------------------
OLD_CHANGES=$OLD/src/Altirra/res/changes.txt
NEW_CHANGES=$NEW/src/Altirra/res/changes.txt
if [ -f "$OLD_CHANGES" ] && [ -f "$NEW_CHANGES" ]; then
    {
        printf 'Changelog delta: %s -> %s\n' "$OLD_LABEL" "$NEW_LABEL"
        printf '%.0s=' $(seq 1 60); printf '\n\n'
        diff -u "$OLD_CHANGES" "$NEW_CHANGES" || true
    } > "$REPORT_DIR/00_changelog.txt"
else
    printf 'changes.txt missing in OLD or NEW\n' > "$REPORT_DIR/00_changelog.txt"
fi

# --- 01/02/03/04/05/06: compute all diff sets via Python (fast) ----------
info "computing diff sets (this may take a few seconds) …"
python3 "$SCRIPT_DIR/lib/compute_diffs.py" \
    --old "$OLD" --new "$NEW" --fork "$FORK" \
    --report-dir "$REPORT_DIR" \
    --trees $SYNC_TREES

count_nonempty() {
    # Count non-empty lines in a file.  Returns 0 for missing files.
    [ -f "$1" ] || { printf 0; return; }
    awk 'NF' "$1" | wc -l | tr -d ' \n'
}
n_upstream=$(count_nonempty "$REPORT_DIR/01_upstream_changed.txt")
n_fork=$(    count_nonempty "$REPORT_DIR/02_fork_changed.txt")
n_three=$(   count_nonempty "$REPORT_DIR/03_three_way.txt")
n_trivial=$( count_nonempty "$REPORT_DIR/04_trivial_copy.txt")
n_added=$(   count_nonempty "$REPORT_DIR/05_added_in_new.txt")
n_removed=$( count_nonempty "$REPORT_DIR/06_removed_in_new.txt")

require_report_paths_in_sync_trees \
    "$REPORT_DIR/01_upstream_changed.txt" \
    "$REPORT_DIR/02_fork_changed.txt" \
    "$REPORT_DIR/03_three_way.txt" \
    "$REPORT_DIR/04_trivial_copy.txt" \
    "$REPORT_DIR/05_added_in_new.txt" \
    "$REPORT_DIR/06_removed_in_new.txt"

if [ "$n_upstream" -gt 0 ] && [ "$n_upstream" -eq "$n_added" ]; then
    cat > "$REPORT_DIR/INVALID_REPORT_DO_NOT_USE.md" <<EOF
# Invalid report - do not use

This report was generated with every upstream-changed path classified as
\`ADDED\`.

That usually means OLD did not contain a usable source baseline for the
configured sync trees. Replace OLD with the real upstream source snapshot and
regenerate the report before applying or classifying any broad upstream
changes.
EOF

    cat > "$REPORT_DIR/diffs/INVALID_DIFFS_DO_NOT_USE.md" <<EOF
# Invalid diffs - do not use

This report was generated with every upstream-changed path classified as
\`ADDED\`.

Any per-file diffs in this directory are derived from that invalid baseline and
must not be used for a merge. Replace OLD with the real source snapshot and
regenerate the report.
EOF

    cat > "$REPORT_DIR/07_classified.md" <<EOF
# Invalid classification - do not use

Every upstream-changed path was classified as \`ADDED\`, which indicates an
unusable OLD baseline. See \`INVALID_REPORT_DO_NOT_USE.md\`.
EOF
else
    # --- 07: per-module classification --------------------------------------
    python3 "$SCRIPT_DIR/classify_changes.py" "$REPORT_DIR"

    # --- per-file diffs for the three-way bucket ----------------------------
    # For each 3-way file we write three diffs:
    #   diffs/<safe>.upstream.diff — OLD vs NEW (what upstream changed)
    #   diffs/<safe>.fork.diff     — OLD vs FORK (what the fork changed)
    #   diffs/<safe>.full.diff     — FORK vs NEW (the combined view)
    # The first two are what you need for a three-way merge.
    info "writing per-file diffs for three-way files …"
    mkdir -p "$REPORT_DIR/diffs"
    awk -F'\t' '{print $1}' "$REPORT_DIR/03_three_way.txt" \
        | while IFS= read -r path; do
            [ -z "$path" ] && continue
            safe=${path//\//__}
            if [ -f "$OLD/$path" ] && [ -f "$NEW/$path" ]; then
                diff -u "$OLD/$path"  "$NEW/$path"  > "$REPORT_DIR/diffs/${safe}.upstream.diff" || true
            fi
            if [ -f "$OLD/$path" ] && [ -f "$FORK/$path" ]; then
                diff -u "$OLD/$path"  "$FORK/$path" > "$REPORT_DIR/diffs/${safe}.fork.diff" || true
            fi
            if [ -f "$NEW/$path" ] && [ -f "$FORK/$path" ]; then
                diff -u "$FORK/$path" "$NEW/$path"  > "$REPORT_DIR/diffs/${safe}.full.diff" || true
            fi
        done
fi

# --- SUMMARY -------------------------------------------------------------
{
    printf '# Sync report: %s → %s\n\n' "$OLD_LABEL" "$NEW_LABEL"
    printf 'Generated by `tools/sync_diff.sh`.\n\n'
    if [ -f "$REPORT_DIR/INVALID_REPORT_DO_NOT_USE.md" ]; then
        printf '> **Do not use this report.** Every upstream-changed path was classified as `ADDED`, which indicates an unusable OLD baseline. See `INVALID_REPORT_DO_NOT_USE.md`.\n\n'
    fi
    printf '## Counts\n\n'
    printf '| Bucket                 | Count |\n'
    printf '|------------------------|------:|\n'
    printf '| Upstream changed total | %5d |\n' "$n_upstream"
    printf '|   of which three-way   | %5d |\n' "$n_three"
    printf '|   of which trivial     | %5d |\n' "$n_trivial"
    printf '| Added in NEW           | %5d |\n' "$n_added"
    printf '| Removed in NEW         | %5d |\n' "$n_removed"
    printf '| Fork-modified (info)   | %5d |\n' "$n_fork"
    printf '\n'
    printf '## Next steps\n\n'
    if [ -f "$REPORT_DIR/INVALID_REPORT_DO_NOT_USE.md" ]; then
        printf '1. Replace OLD with the real upstream source snapshot containing `src/`.\n'
        printf '2. Move this stale report directory aside before regenerating it.\n'
        printf '3. Regenerate the report with `tools/sync_diff.sh`.\n'
        printf '4. Confirm the regenerated report has no `INVALID_REPORT_DO_NOT_USE.md`.\n'
        printf '5. Do not apply trivial copies or use `07_classified.md` from this invalid report.\n'
    elif [ "$n_upstream" -eq 0 ]; then
        printf '1. No upstream source files changed in the configured sync trees.\n'
        printf '2. Do not run `apply_trivial.py`; there are no trivial copies or added files to apply.\n'
        printf '3. Record the zero-diff baseline update if this report represents the intended upstream version.\n'
        printf '4. Run any required build/runtime validation for fork-side targeted work.\n'
    else
        printf '1. Read `00_changelog.txt` — it is the cheapest way to understand intent.\n'
        printf '2. Run `./tools/apply_trivial.py reports/%s__to__%s` to apply trivial copies.\n' "$OLD_LABEL" "$NEW_LABEL"
        printf '3. Resolve three-way files in `07_classified.md` (LLM or by hand).\n'
        printf '4. Mirror user-visible UI changes in `src/AltirraSDL/source/ui/`.\n'
        printf '5. Build and test.\n'
    fi
} > "$REPORT_DIR/SUMMARY.md"

info "done. see $REPORT_DIR/SUMMARY.md"
