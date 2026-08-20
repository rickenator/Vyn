# Error Handling Test Suite (fail/trap)

This directory contains comprehensive tests for Vyb's error handling system using the `fail/trap/refail/ensure/panic` keywords.

## Test Organization

### Basic Tests (01-05)
- **01_simple_fail.vyb**: Basic fail statement syntax
- **02_simple_trap.vyb**: Basic trap clause catching an error
- **03_panic.vyb**: Panic statement (unrecoverable)
- **04_refail.vyb**: Refailing errors to caller
- **05_ensure.vyb**: Ensure clause for cleanup

### Multiple Handlers (06-07)
- **06_multiple_traps.vyb**: Type-based dispatch with multiple trap clauses
- **07_nested_traps.vyb**: Nested error handling contexts

### Function Integration (08-09)
- **08_fail_in_function.vyb**: Errors propagating from function calls
- **09_refail_transform.vyb**: Transforming errors before refailing

### Runtime Behavior (10-12)
- **10_untrapped_error.vyb**: Untrapped errors caught by runtime
- **11_ensure_with_trap.vyb**: Combined cleanup and error handling
- **12_defer_with_fail.vyb**: Defer cleanup with error propagation

### Advanced Integration (13-15)
- **13_match_with_trap.vyb**: Errors in match expressions
- **14_async_with_trap.vyb**: Async/await with error handling
- **15_freedom_with_trap.vyb**: Freedom blocks with errors

### Error Type System (16-17)
- **16_error_hierarchy.vyb**: Composition-based error hierarchy
- **17_panic_vs_fail.vyb**: Distinction between panic and fail

## Expected Behavior

### Fail/Trap
- `fail` statements throw typed errors
- `trap` clauses catch errors by type (first match wins)
- Untrapped errors invoke runtime handler and exit(1)

### Refail
- Simple `refail` propagates current error
- `fail NewError { cause = e }` transforms error before refailing
- Stack traces are preserved and appended

### Ensure
- `ensure` blocks always run (success or failure)
- Runs after trap handlers
- Multiple ensure blocks execute in order

### Panic
- `panic()` crashes immediately
- No trap can catch a panic
- No cleanup handlers run
- Used for invariant violations

## Running Tests

Individual test:
```bash
build/vyb test/trap/01_simple_fail.vyb
```

All trap tests:
```bash
for f in test/trap/*.vyb; do
    echo "Testing: $f"
    build/vyb "$f"
done
```

## Implementation Phases

### Phase 1: Foundation ✅
- Keywords, tokens, AST nodes
- Visitor pattern stubs

### Phase 2: Parser (Current)
- Parse fail/trap/refail/ensure/panic statements
- Parse trap clauses attached to blocks
- Parse ensure clauses

### Phase 3: Semantic Analysis
- Type checking error expressions
- Verify trap type compatibility
- Scope validation for refail

### Phase 4: Codegen
- LLVM exception handling integration
- Runtime error handler calls
- Stack trace capture
- Cleanup handler execution

## Test Expectations

Each test file should either:
1. **Succeed**: Exit code 0, expected output
2. **Fail gracefully**: Runtime catches untrapped error, exit code 1
3. **Panic**: Immediate crash, exit code 2

Tests 01-09, 11-16 should parse successfully once Phase 2 is complete.
Test 10 is designed to fail at runtime (untrapped error).
Test 17 is designed to panic (unrecoverable).
