#!/bin/bash
# Test AOT compilation of Vyb examples (`--build`)
#
# The compiled executable must exit 0 and write the program's main-return value
# (JSON-serialized) to stdout, matching the JIT contract enforced by run_tests.py.

echo "=========================================="
echo "Vyb AOT Compilation Test Suite v0.5.0"
echo "=========================================="
echo

TESTS_PASSED=0
TESTS_FAILED=0

# Test function
test_compile() {
    local test_file=$1
    local expected_return=$2
    local test_name=$(basename "$test_file" .vyb)

    echo "Testing: $test_name"

    # Build the executable
    if ! build/vyb "$test_file" --build "test_output_$test_name" -O2 > /dev/null 2>&1; then
        echo "✗ Build failed: $test_name"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi

    # Run, check exit code is 0, and check the serialized return value on stdout
    local output
    output=$(./test_output_$test_name 2>&1)
    local actual_exit=$?

    local actual_return
    actual_return=$(printf '%s\n' "$output" | sed '/^$/d' | tail -1)

    if [ "$actual_exit" -eq 0 ] && [ "$actual_return" = "$expected_return" ]; then
        echo "✓ Pass: $test_name (exit code: 0, return: $actual_return)"
        TESTS_PASSED=$((TESTS_PASSED + 1))
        rm -f "test_output_$test_name" "test_output_$test_name.o"
        return 0
    else
        echo "✗ Fail: $test_name (expected exit 0 and return: $expected_return, got exit: $actual_exit, return: $actual_return)"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi
}

# Run tests
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

if [ $TESTS_FAILED -eq 0 ]; then
    echo "All tests passed! ✓"
    exit 0
else
    echo "Some tests failed! ✗"
    exit 1
fi
