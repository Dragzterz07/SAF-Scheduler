#!/usr/bin/env bash
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$DIR"

if ! command -v valgrind >/dev/null 2>&1; then
    echo "Error: valgrind is required for test-mem but is not installed or not in PATH." >&2
    exit 1
fi

SKIP_VALGRIND=()

VALGRIND_FLAGS=(
    --leak-check=full
    --show-leak-kinds=all
    --errors-for-leak-kinds=definite,indirect,possible
    --error-exitcode=1
)

TARGETS=("./scheduler")
if [ -d "build/tests" ]; then
    for test_bin in build/tests/test_*; do
        if [ -x "$test_bin" ]; then
            TARGETS+=("$test_bin")
        fi
    done
fi

FAILED=0
PASSED=0

echo "Running Valgrind leak checks..."
for bin in "${TARGETS[@]}"; do
    bin_name="$(basename "$bin")"
    skip=0
    for skip_item in "${SKIP_VALGRIND[@]}"; do
        if [ "$bin_name" == "$skip_item" ]; then
            skip=1
            break
        fi
    done

    if [ "$skip" -eq 1 ]; then
        echo "Skipping $bin (listed in SKIP_VALGRIND)"
        continue
    fi

    echo "Running Valgrind on $bin..."
    if valgrind "${VALGRIND_FLAGS[@]}" "$bin" > /dev/null 2>&1; then
        echo "  [PASS] $bin"
        PASSED=$((PASSED + 1))
    else
        echo "  [FAIL] $bin" >&2
        FAILED=$((FAILED + 1))
    fi
done

echo "========================================"
echo "Valgrind Summary: $PASSED passed, $FAILED failed."
if [ "$FAILED" -ne 0 ]; then
    exit 1
fi
