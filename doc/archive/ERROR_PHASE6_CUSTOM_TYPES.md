# Phase 6: Custom Error Types and Error Context

## Current Status (Post-Phase 5)
✅ Phases 1-5 complete:
- Functions marked as failable
- Dual return values {T, ptr}
- Error propagation through call stack
- Trap handlers catch errors
- Multi-level propagation working

**Current limitation**: All errors are `Int` (or generic ptr), no custom error types yet

## Phase 6 Goals

### 6.1: Basic Error Struct Type ✅ IN PROGRESS
Instead of failing with bare `Int`, support structured errors:

```vyb
struct DivisionError {
    code<Int>,
    divisor<Int>,
    dividend<Int>
}

fn divide(a<Int>, b<Int>)<Int> -> {
    if (b == 0) {
        fail DivisionError { code = 42, divisor = b, dividend = a }
    }
    return a / b
}
```

**Implementation**:
- Parse fail statements with struct literals
- Allocate error struct on heap
- Return pointer to error struct
- In trap handler, cast ptr back to error type
- Access error fields via member expressions

### 6.2: Multiple Error Types per Function
Functions can fail with different error types:

```vyb
struct DivisionError { code<Int>, dividend<Int> }
struct OverflowError { code<Int>, value<Int> }

fn compute(a<Int>, b<Int>)<Int> -> {
    if (b == 0) {
        fail DivisionError { code = 1, dividend = a }
    }
    let result = a / b
    if (result > 1000) {
        fail OverflowError { code = 2, value = result }
    }
    return result
}
```

**Implementation**:
- Track multiple error types in `errorTypes` vector
- Create union type for error pointer
- Add type tag to error allocation
- Multiple trap clauses can catch different types

### 6.3: Error Type Matching in Trap Clauses
Trap clauses match specific error types:

```vyb
{
    compute(10, 0)
} trap (e<DivisionError>) -> {
    print("Division error: " + e.dividend.to_string())
    -1
} trap (e<OverflowError>) -> {
    print("Overflow: " + e.value.to_string())
    -2
}
```

**Implementation**:
- Type check in trap.landing
- Branch to appropriate handler based on error type
- Fall through if no handler matches (propagate up)

### 6.4: Generic Error Type (Future)
Base error type that all errors extend:

```vyb
struct Error {
    message<String>,
    location<SourceLocation>,
    timestamp<Int64>
}

struct DivisionError {
    base<Error>,
    dividend<Int>
}

# Can trap generic errors
} trap (e<Error>) -> {
    print(e.message)
}
```

**Deferred**: Needs inheritance or composition (not in v0.4.2 scope)

## Implementation Plan for 6.1

### Step 1: Parse struct literals in fail statements ✅
Current: `fail 42`
Target: `fail DivisionError { code = 42, divisor = b, dividend = a }`

- Already have ObjectLiteral AST node
- FailStatement.value is Expression - can be ObjectLiteral
- No parser changes needed!

### Step 2: Codegen error struct allocation
```cpp
// In codegenFailStatement:
if (auto* objLit = dynamic_cast<ast::ObjectLiteral*>(node->value.get())) {
    // 1. Generate the struct value
    objLit->accept(*this);
    llvm::Value* errorStruct = m_currentLLVMValue;

    // 2. Allocate on heap (use malloc or runtime allocator)
    llvm::Type* errorType = errorStruct->getType();
    llvm::Value* errorPtr = createHeapAlloc(errorType);

    // 3. Store struct to heap
    builder->CreateStore(errorStruct, errorPtr);

    // 4. Return as error pointer
    return createFailReturn(errorPtr);
}
```

### Step 3: Type-safe trap handlers
```cpp
// In TrapClause codegen:
// Load error pointer
llvm::Value* errorPtr = builder->CreateLoad(...);

// Cast to expected error type
llvm::Type* expectedType = codegenType(trapClause->errorType);
llvm::Value* typedError = builder->CreateBitCast(errorPtr, expectedType);

// Make available to trap handler
namedValues[trapClause->errorName->name] = typedError;
```

### Step 4: Test with custom error types
```vyb
struct DivisionError {
    code<Int>,
    dividend<Int>
}

fn divide(a<Int>, b<Int>)<Int> -> {
    if (b == 0) {
        fail DivisionError { code = 42, dividend = a }
    }
    return a / b
}

fn main()<Int> -> {
    let result = {
        divide(10, 0)
    } trap (e<DivisionError>) -> {
        return e.code + e.dividend  # Should return 52
    }
    return result
}
```

## Success Criteria

- ✅ Parse fail statements with struct literals
- [ ] Allocate error structs on heap
- [ ] Type-safe error handling in trap clauses
- [ ] Access error struct fields in trap handlers
- [ ] Memory cleanup for error structs
- [ ] Test with custom error types returning correct values

**What's Missing:**
- Aspect-based error types (Errable aspect)
- Standard library error hierarchy
