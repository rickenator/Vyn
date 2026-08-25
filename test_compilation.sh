#!/bin/bash
# Test AOT compilation of Vyb examples (`--build`) -- #158.
#
# The compiled executable must exit 0 and write the program's main-return value
# (JSON-serialized) to stdout, matching the JIT contract enforced by run_tests.py.
# Pass `--json <file>` for machine-readable per-test results + totals (reproducible
# release evidence); the exit code is 0 only when every test passes.

JSON_FILE=""
if [ "$1" == "--json" ] && [ -n "$2" ]; then
    JSON_FILE="$2"
fi

echo "=========================================="
echo "Vyb AOT Compilation Test Suite v0.5.0 (#158)"
echo "=========================================="
echo

TESTS_PASSED=0
TESTS_FAILED=0
# JSON entry per test: escaped minimal ("name":{"pass":bool,"note":str})
declare -a JENTRIES

test_compile() {
    local test_file=$1
    local expected_return=$2
    local test_name=$(basename "$test_file" .vyb)

    echo "Testing: $test_name"

    if ! build/vyb "$test_file" --build "test_output_$test_name" -O2 > /dev/null 2>&1; then
        echo "✗ Build failed: $test_name"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        JENTRIES+=("\"$test_name\": {\"pass\":false, \"note\":\"build failed\"}")
        return 1
    fi

    local output
    output=$(./test_output_$test_name 2>&1)
    local actual_exit=$?

    local actual_return
    actual_return=$(printf '%s\n' "$output" | sed '/^$/d' | tail -1)

    if [ "$actual_exit" -eq 0 ] && [ "$actual_return" = "$expected_return" ]; then
        echo "✓ Pass: $test_name (exit code: 0, return: $actual_return)"
        TESTS_PASSED=$((TESTS_PASSED + 1))
        JENTRIES+=("\"$test_name\": {\"pass\":true, \"return\":\"$actual_return\"}")
        rm -f "test_output_$test_name" "test_output_$test_name.o"
        return 0
    else
        echo "✗ Fail: $test_name (expected exit 0 and return: $expected_return, got exit: $actual_exit, return: $actual_return)"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        JENTRIES+=("\"$test_name\": {\"pass\":false, \"expected\":\"$expected_return\", \"exit\":$actual_exit, \"return\":\"$actual_return\"}")
        return 1
    fi
}

echo "Running compilation tests..."
echo

test_compile "test/compilation/test_compile.vyb" 49
test_compile "test/compilation/binary_tree.vyb" 60
test_compile "test/compilation/binary_tree_complex.vyb" 350

echo
echo "=========================================="
echo "Test Summary"
echo "=========================================="
echo "Passed: $TESTS_PASSED"
echo "Failed: $TESTS_FAILED"
echo

if [ -n "$JSON_FILE" ]; then
    # Machine-readable evidence: revision + per-test results + totals.
    local_rev=$(git rev-parse HEAD 2>/dev/null || echo "unknown")
    {
        echo "{"
        echo "  \"suite\": \"aot-native\","
        echo "  \"revision\": \"$local_rev\","
        echo "  \"command\": \"test_compilation.sh --json $JSON_FILE\","
        echo "  \"tests\": {"
        for ((i = 0; i < ${#JENTRIES[@]}; i++)); do
            if [ $i -gt 0 ]; then echo ","; fi
            printf "    %s" "${JENTRIES[$i]}"
        done
        echo ""
        echo "  },"
        echo "  \"total\": $((TESTS_PASSED + TESTS_FAILED)),"
        echo "  \"passed\": $TESTS_PASSED,"
        echo "  \"failed\": $TESTS_FAILED"
        echo "}"
    } > "$JSON_FILE"
    echo "AOT evidence written to $JSON_FILE"
fi

if [ $TESTS_FAILED -eq 0 ]; then
    echo "All tests passed! ✓"
    exit 0
else
    echo "Some tests failed! ✗"
    exit 1
fi
