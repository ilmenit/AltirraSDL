#!/bin/bash
# Run AltirraSDL UI regression tests.
#
# Usage:
#   ./tests/ui/run_tests.sh                          # run all tests
#   ./tests/ui/run_tests.sh -k test_smoke             # run only smoke tests
#   ./tests/ui/run_tests.sh --emu-path=/path/to/bin   # custom binary path
#   ./tests/ui/run_tests.sh -x                        # stop on first failure
#
# Prerequisites:
#   pip install pytest
#   Build AltirraSDL (cmake --preset linux-release && cmake --build build/linux-release)
#
# The test suite launches a single AltirraSDL instance in --test-mode,
# connects via Unix domain socket, and exercises every UI widget.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

cd "$PROJECT_ROOT"

# Default: look for the binary in common build locations
if [ -z "${EMU_PATH:-}" ]; then
    for candidate in \
        build/linux-release/src/AltirraSDL/AltirraSDL \
        build/linux-debug/src/AltirraSDL/AltirraSDL \
        build/src/AltirraSDL/AltirraSDL; do
        if [ -x "$candidate" ]; then
            EMU_PATH="$candidate"
            break
        fi
    done
fi

if [ -z "${EMU_PATH:-}" ]; then
    echo "ERROR: AltirraSDL binary not found. Build it first or set EMU_PATH."
    exit 1
fi

echo "Using binary: $EMU_PATH"
echo "Running UI tests..."
echo

python3 -m pytest tests/ui/ \
    --emu-path="$EMU_PATH" \
    -v \
    "$@"
