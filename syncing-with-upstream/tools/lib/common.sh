# Shared bash helpers for the upstream sync tooling.
#
# Source this file from tools/*.sh:  . "$(dirname "$0")/lib/common.sh"
#
# Everything is version-agnostic — no hard-coded "test8" or "4.50".

set -eu
set -o pipefail

fatal() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

info() {
    printf '[sync] %s\n' "$*"
}

# Resolve an argument to an absolute path (portable: avoids `realpath` on
# systems where it is missing, e.g. default macOS).
abs_path() {
    local p=$1
    if [ -d "$p" ]; then
        (cd "$p" && pwd -P)
    elif [ -f "$p" ]; then
        printf '%s/%s\n' "$(cd "$(dirname "$p")" && pwd -P)" "$(basename "$p")"
    else
        fatal "path not found: $p"
    fi
}

# Extract the Altirra version label from a snapshot dir name.
# Input:  /…/Altirra-4.50-test9-src
# Output: 4.50-test9
snapshot_label() {
    local p=$1
    local base
    base=$(basename "$p")
    # Strip the Altirra- prefix and -src suffix if present.
    base=${base#Altirra-}
    base=${base%-src}
    printf '%s\n' "$base"
}

# Paths inside each snapshot that the sync tooling operates on. Everything
# else is considered noise (assets/, dist/, scripts/, etc.). If upstream
# starts shipping something new under a top-level folder, add it here.
#
# The list of paths that belong to the fork itself (src/AltirraSDL/, cmake/,
# vendor/, build/, syncing-with-upstream/, etc.) is kept in
# tools/lib/filemap.py under the ``fork-only`` rules — there is a single
# source of truth, not a bash duplicate.
SYNC_TREES="src"

require_sync_trees() {
    # $1: role label, $2: root path
    local role=$1
    local root=$2

    for tree in $SYNC_TREES; do
        [ -d "$root/$tree" ] || fatal "$role is not a usable sync root: missing $tree/ under $root"
        find "$root/$tree" -type f -print -quit | grep -q . \
            || fatal "$role is not a usable sync root: no files under $root/$tree"
    done
}

require_report_paths_in_sync_trees() {
    # $@: report files containing path or path<TAB>status lines
    local file path ok tree

    for file in "$@"; do
        [ -f "$file" ] || continue

        while IFS= read -r path || [ -n "$path" ]; do
            [ -n "$path" ] || continue
            path=${path%%	*}

            [ -n "$path" ] || fatal "empty report path"

            case "$path" in
                *\\*)
                    fatal "unsafe report path uses backslashes: $path"
                    ;;
            esac

            case "$path" in
                /*|.|./*|*/./*|*/.|../*|*/../*|*/..|..|*//*)
                    fatal "unsafe report path outside sync root: $path"
                    ;;
            esac

            ok=0
            for tree in $SYNC_TREES; do
                if [ "$path" = "$tree" ] || [ "${path#"$tree/"}" != "$path" ]; then
                    ok=1
                    break
                fi
            done

            [ "$ok" -eq 1 ] || fatal "report path is outside configured sync trees: $path"
        done < "$file"
    done
}
