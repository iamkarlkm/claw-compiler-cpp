#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLAW="${SCRIPT_DIR}/../claw"
INTEGRATION_DIR="${SCRIPT_DIR}/integration"
FAILED=0
PASSED=0

if [[ ! -x "$CLAW" ]]; then
    echo "Error: claw binary not found at $CLAW"
    echo "Build it first: make claw"
    exit 1
fi

echo "Running integration tests..."
echo ""

for test_file in "$INTEGRATION_DIR"/*.claw; do
    name=$(basename "$test_file")
    echo -n "  $name ... "

    ast_output=$("$CLAW" --run "$test_file" 2>&1 | tail -n +4 | sed '$d' | sed '$d' || true)
    bc_output=$("$CLAW" --mode=bytecode "$test_file" 2>&1 | tail -n +4 | sed '$d' | sed '$d' || true)

    # Strip timing lines and blank lines for comparison
    ast_normalized=$(echo "$ast_output" | grep -v '^Total time:' | grep -v '^$' || true)
    bc_normalized=$(echo "$bc_output" | grep -v '^Total time:' | grep -v '^$' || true)

    if diff -q <(echo "$ast_normalized") <(echo "$bc_normalized") >/dev/null 2>&1; then
        echo "PASS"
        PASSED=$((PASSED + 1))
    else
        echo "FAIL"
        echo "    AST output:"
        echo "$ast_normalized" | sed 's/^/      /'
        echo "    Bytecode output:"
        echo "$bc_normalized" | sed 's/^/      /'
        FAILED=$((FAILED + 1))
    fi
done

echo ""
echo "Results: $PASSED passed, $FAILED failed"

if [[ $FAILED -gt 0 ]]; then
    exit 1
fi
