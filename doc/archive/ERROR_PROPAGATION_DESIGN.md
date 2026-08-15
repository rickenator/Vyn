# Automatic Error Propagation Design

## Overview
Implement automatic error propagation through the call stack using return value checking.
When a function hits `fail` without a trap handler, it returns the error to its caller.
Errors propagate up until caught by a trap handler or reach the top of the stack.

## Implementation Phases

### Phase 1: Track Failable Functions ✅ COMPLETE
- ✅ Semantic analysis identifies functions containing `fail` statements
- ✅ Mark function declarations as potentially failable (`canFail`, `needsErrorReturn`)
- ✅ Track error types that can be raised (`errorTypes` vector)
- **Verified**: `divide()` with fail marked `canFail=1`, `main()` marked `canFail=0`
- **Files**: ast.hpp, semantic.cpp, cgen_decl.cpp, test_canfail.vyb

### Phase 2: Dual Return Values ✅ COMPLETE
- Functions that can fail return `{ T, ptr }` instead of `T`
- First element: actual return value (or dummy if error)
- Second element: error pointer (NULL = success, non-NULL = error)
- Transparent to Vyb source code

### Phase 3: Fail Statement Codegen ✅ COMPLETE
- If trap handler in scope: store error, jump to landing pad (current behavior)
- If no trap handler: pack error into return value, return to caller
- Construct a runtime `VybError` payload via `__vyb_runtime_create_error`
- Execute registered `defer` statements before emitting the propagating return

### Phase 4: Call Site Instrumentation ✅ COMPLETE
- After calling failable function, extract { value, error } tuple
- Check if error != NULL
- If error and has trap: jump to trap.landing
- If error and no trap: propagate (return error to our caller)
- If no error: continue with value
- Semantic validation rejects untrapped failable calls from non-failable callers

### Phase 5: Top-Level Handling ✅ COMPLETE
- Functions at top of call stack (main, no caller)
- If error propagates out: call __vyb_runtime_untrapped_error()
- Clean termination with error display

## Data Structures

### Function Metadata
```cpp
struct FunctionErrorInfo {
    bool canFail;                    // Contains fail statements
    std::vector<ast::TypeNode*> errorTypes;  // Types that can be failed
    bool needsErrorPropagation;      // Calls failable functions
};
```

### LLVM Types
```llvm
; Normal function
define i64 @safe_func()

; Failable function (internal representation)
define { i64, ptr } @failable_func()
```

### Runtime `VybError` Layout (codegen-emitted)
```c
struct VybError {
    uint64_t type_hash;     // hash(typeof(error))
    const char* type_name;  // concrete error type name
    void* payload;          // copied bytes of failed value
    const char* file;       // fail site file path
    uint32_t line;          // fail site line
    uint32_t col;           // fail site column
    // legacy runtime fields retained for compatibility
};
```

## Error Propagation Flow

```
divide(10, 0)
  ↓ fail 42 (no trap in divide)
  ↓ return { 0, error_ptr }
compute(x)
  ↓ receives error from divide
  ↓ no trap in compute
  ↓ return { 0, error_ptr }
main()
  ↓ receives error from compute
  ↓ has trap handler!
  → jump to trap.landing
  → execute trap block
  → return -1
```

## Backward Compatibility

- Functions without `fail` statements: no change
- Functions with `fail` but no callers: works as before
- Opt-in: only affects functions in error path

## Testing Strategy

1. Direct fail in trap block (already works)
2. Single-level propagation (divide → main)
3. Multi-level propagation (divide → compute → main)
4. Mixed trap/no-trap scenarios
5. Multiple error types
6. Nested trap handlers

## Status Tracking

- [x] Phase 1: Semantic analysis for failable functions ✅
- [x] Phase 2: Dual return value codegen ✅
- [x] Phase 3: Fail statement returns error ✅
- [x] Phase 4: Call site error checking ✅
- [x] Phase 5: Top-level untrapped handling ✅
