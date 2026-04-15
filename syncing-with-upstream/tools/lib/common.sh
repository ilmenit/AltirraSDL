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
