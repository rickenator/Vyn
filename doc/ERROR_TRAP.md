# Vyb Error Handling: Failure & Trap System

**Status:** Design Specification (v0.4.2)
**Last Updated:** October 20, 2025
**Philosophy:** Fresh thinking on error handling - no tired try/catch patterns

---

## Current Status (v0.4.2)

**✅ CORE ERROR HANDLING COMPLETE**

Vyb has a **production-ready error handling system** with:
- Runtime infrastructure: VybError struct, heap allocation, type IDs (C++ level)
- Language features: fail/trap/refail/ensure/panic keywords
- Advanced patterns: wildcard `trap (e<?>) ->`, multi-type `trap (e<A|B>) ->`
- Stack traces with source locations
- Zero-cost success path
- Comprehensive test coverage

**What's Missing**: Standard library types (Error struct, Errable aspect) to expose
the runtime system to Vyb code. See "Implementation Roadmap" section below.

### Phase 2 — Implemented (Dual Return ABI)

Codegen now lowers failable functions (`needsErrorReturn`) to a dual-return tuple:

- non-`Void`: `{ T, i8* }`
- `Void`: `{ i1, i8* }` (`i1` is a dummy payload so all failable paths share one 2-field ABI shape)

Success path returns `{ value, null }`.
Failure path returns `{ undef_or_dummy, error_ptr }`, where `error_ptr` points to heap memory with:

1. first 8 bytes = runtime type hash (`std::hash<TypeName>`, same convention used by `typeof(...)`)
2. remaining bytes = failed value payload bytes

Conceptual lowering example:

```vyb
divide(a<Int>, b<Int>)<Int> -> {
    if (b == 0) { fail<Int>(0) }
    return a / b
}
```

```llvm
define { i64, ptr } @divide(i64 %a, i64 %b) {
entry:
  ; success
  %ok = insertvalue { i64, ptr } undef, i64 %quot, 0
  %ok2 = insertvalue { i64, ptr } %ok, ptr null, 1
  ret { i64, ptr } %ok2

fail_path:
  ; allocate + write [type_hash][payload]
  ; return error tuple
  %err = insertvalue { i64, ptr } undef, i64 undef, 0
  %err2 = insertvalue { i64, ptr } %err, ptr %error_ptr, 1
  ret { i64, ptr } %err2
}
```

---

## Core Philosophy

Error handling should be:
- **Explicit but not verbose:** Errors visible at call sites without boilerplate
- **Type-safe:** Compile-time checking of error types and compatibility
- **Composable:** Easy to build error hierarchies and handlers
- **Unique to Vyb:** Fresh approach that fits Vyb's design philosophy

**Key Insight:** Blocks can fail, and failures can be trapped. No exceptions, no try/catch - just **failure** and **trap**.

---

## Basic Syntax

### Simple Failure

```vyb
divide(a<Int>, b<Int>)<Int> -> {
    if (b == 0) {
        fail DivisionByZero { dividend = a }
    }
    return a / b
}
```

### Trapping Failures

```vyb
safe_divide(a<Int>, b<Int>)<Int> -> {
    result<Int> = {
        divide(a, b)
    } trap (e<DivisionByZero>) -> {
        println("Cannot divide by zero!")
        return -1
    }
    return result
}
```

### Multiple Traps (Type-Based Dispatch)

```vyb
process_file(path<String>)<String> -> {
    content<String> = {
        file<File> = open_file(path)
        data<String> = file.read()
        data
    } trap (e<FileNotFound>) -> {
        return "File not found: " + path
    } trap (e<PermissionDenied>) -> {
        return "Permission denied: " + path
    } trap (e<IOError>) -> {
        return "IO error: " + e.message
    }

    return content
}
```

---

## Trap Clause Rules

### Rule 1: Type Compatibility
When a block's value is consumed, all trap clauses must evaluate to the same type.

```vyb
# ✅ VALID - All traps return Int
compute()<Int> -> {
    value<Int> = {
        risky_operation()
    } trap (e<Error1>) -> {
        return 0
    } trap (e<Error2>) -> {
        return -1
    }
    return value
}

# ❌ INVALID - Trap returns String, block returns Int
bad_compute()<Int> -> {
    value<Int> = {
        risky_operation()
    } trap (e<Error1>) -> {
        return "error"  # Type mismatch!
    }
    return value
}
```

### Rule 2: First Type-Compatible Trap Wins
Traps are checked in order. First matching type handles the failure.

```vyb
{
    operation()
} trap (e<SpecificError>) -> {
    # Catches SpecificError
} trap (e<GeneralError>) -> {
    # Catches GeneralError and its subtypes (except SpecificError)
} trap (e<Error>) -> {
    # Catches all other errors
}
```

### Rule 3: Untrapped Failures Crash After Cleanup
If no trap matches, the failure crashes the current task after scope cleanup.

```vyb
dangerous_operation()<Int> -> {
    resource<Resource> = acquire_resource()
    # If this fails and no trap, resource is cleaned up before crash
    result<Int> = resource.process()
    return result
}
```

---

## Propagation: `refail`

The `refail` keyword propagates the error upward without handling it.

```vyb
wrapper_function()<Int> -> {
    {
        might_fail()
    } trap (e<NonCriticalError>) -> {
        println("Non-critical error, ignoring")
        return 0
    } trap (e<CriticalError>) -> {
        println("Critical error, propagating!")
        refail  # Send error to caller
    }
}
```

### Refail with Transformation

```vyb
{
    low_level_operation()
} trap (e<LowLevelError>) -> {
    # Transform error and propagate
    fail HighLevelError {
        message = "Operation failed: " + e.details,
        cause = e
    }
}
```

---

## Cleanup: `ensure`

The `ensure` clause runs after either success or failure, similar to `finally` but cleaner.

```vyb
process_with_cleanup(path<String>)<String> -> {
    file<File> = open_file(path)

    result<String> = {
        file.read()
    } trap (e<IOError>) -> {
        return ""
    } ensure -> {
        file.close()  # Always runs, success or failure
    }

    return result
}
```

### Ensure Execution Order

```vyb
{
    resource<Resource> = acquire()
    {
        resource.use()
    } ensure -> {
        println("Inner cleanup")
    }
} trap (e<Error>) -> {
    println("Error handler")
} ensure -> {
    println("Outer cleanup")
}

# Execution on failure:
# 1. Inner cleanup
# 2. Error handler (if trap matches)
# 3. Outer cleanup
```

---

## The Error Type Hierarchy

### Base Error Type

```vyb
# Built-in base error type
struct Error {
    message<String>,
    location<SourceLocation>,
    timestamp<Int64>
}

aspect Errable {
    message(self<Self>)<String> -> { }
    details(self<Self>)<String> -> { }
}

bind Errable -> Error {
    message(self<Self>)<String> -> {
        return self.message
    }

    details(self<Self>)<String> -> {
        return self.message + " at " + self.location.to_string()
    }
}
```

### Creating Custom Errors

#### Option 1: Struct Extension (Composition)

```vyb
struct DivisionByZero {
    base<Error>,
    dividend<Int>
}

bind Errable -> DivisionByZero {
    message(self<Self>)<String> -> {
        return "Division by zero: dividend=" + self.dividend.to_string()
    }

    details(self<Self>)<String> -> {
        return self.base.details() + " [dividend=" + self.dividend.to_string() + "]"
    }
}

# Usage
if (divisor == 0) {
    fail DivisionByZero {
        base = Error {
            message = "Cannot divide by zero",
            location = here(),
            timestamp = now()
        },
        dividend = dividend
    }
}
```

#### Option 2: Aspect-Based (Duck Typing)

```vyb
struct MyError {
    msg<String>,
    code<Int>
}

bind Errable -> MyError {
    message(self<Self>)<String> -> {
        return "[" + self.code.to_string() + "] " + self.msg
    }

    details(self<Self>)<String> -> {
        return self.message() + " (custom error)"
    }
}

# Any type implementing Errable can be failed
fail MyError { msg = "Something broke", code = 42 }
```

#### Option 3: Error Macro (Syntactic Sugar)

```vyb
# Macro-generated error type
error FileError : Error {
    path<String>
}

# Expands to struct + Errable binding automatically

# Usage
fail FileError {
    message = "File operation failed",
    location = here(),
    timestamp = now(),
    path = "/tmp/file.txt",
    operation = "read"
}
```

---

## Error Hierarchy Patterns

### Inheritance-Style (Via Composition)

```vyb
struct Error {
    message<String>,
    location<SourceLocation>
}

struct IOError {
    base<Error>,
    path<String>
}

struct FileNotFound {
    base<IOError>,
    attempted_paths<Vec<String>>
}

# Trap hierarchy - most specific first
{
    open_file(path)
} trap (e<FileNotFound>) -> {
    println("Not found, tried: " + e.attempted_paths.len().to_string() + " paths")
} trap (e<IOError>) -> {
    println("IO error at: " + e.path)
} trap (e<Error>) -> {
    println("General error: " + e.message)
}
```

### Enum-Style Error Variants

```vyb
enum ParseError {
    UnexpectedToken { expected<String>, got<String> },
    UnexpectedEOF { position<Int> },
    InvalidSyntax { message<String>, line<Int>, column<Int> }
}

bind Errable -> ParseError {
    message(self<Self>)<String> -> {
        match (self) {
            UnexpectedToken -> return "Expected " + self.expected + ", got " + self.got,
            UnexpectedEOF -> return "Unexpected EOF at position " + self.position.to_string(),
            InvalidSyntax -> return self.message
        }
    }
}

# Usage
fail ParseError::UnexpectedToken { expected = "identifier", got = "number" }
```

---

## Advanced Patterns

### Panic vs Fail

```vyb
# fail - trappable, can be handled
fail UserError { message = "Invalid input" }

# panic - untrappable, always crashes (use sparingly)
panic("Invariant violated: this should never happen!")
```

### Result<T, E> Pattern (Optional)

```vyb
enum Result<T, E> {
    Ok { value<T> },
    Err { error<E> }
}

# Functional error handling without trap
parse_int(s<String>)<Result<Int, ParseError>> -> {
    if (is_valid_int(s)) {
        return Result::Ok { value = convert_to_int(s) }
    } else {
        return Result::Err { error = ParseError { message = "Invalid integer" } }
    }
}

# Usage with match
result<Result<Int, ParseError>> = parse_int("42")
match (result) {
    Ok -> println("Parsed: " + result.value.to_string()),
    Err -> println("Error: " + result.error.message)
}
```

### Freedom Blocks and Errors

```vyb
# Freedom blocks can fail too
process_pointer(ptr<loc<Int>>)<Int> -> {
    value<Int> = {
        freedom {
            if (ptr == null()) {
                fail NullPointerError { address = 0 }
            }
            at(ptr)
        }
    } trap (e<NullPointerError>) -> {
        println("Null pointer detected!")
        return 0
    }
    return value
}
```

### Async Errors

```vyb
async fetch_data(url<String>)<Future<String>> -> {
    response<String> = {
        await http_get(url)
    } trap (e<NetworkError>) -> {
        println("Network error, retrying...")
        await http_get(url)  # Retry once
    } trap (e<Timeout>) -> {
        return "Request timed out"
    }

    return response
}
```

---

## Compiler Implementation Notes

### Type Checking Rules

1. **Trap Type Compatibility:** Each trap parameter type must be compatible with failure types in the block
2. **Return Type Unification:** All trap bodies must return same type as block expression
3. **Ensure Type Validation:** Ensure blocks must return `Void` or be expression statements
4. **Refail Context:** `refail` only valid inside trap clauses
5. **Fail Type Requirement:** `fail` argument must implement `Errable` aspect

### Codegen Strategy

1. **Block Wrapping:** Wrap blocks with trap clauses in hidden try-like mechanism (LLVM exception handling)
2. **Type Dispatch:** Generate type checks for each trap clause (most specific first)
3. **Ensure Execution:** Use RAII-like patterns to guarantee ensure execution
4. **Refail:** Generate throw of current error being handled
5. **Cleanup:** Integrate with ownership system for proper resource cleanup

### Error Value Representation

```cpp
// LLVM IR representation
struct VybError {
    void* vtable;        // Type information for dispatch
    void* data;          // Error-specific data
    SourceLocation loc;  // Where error occurred
    int64_t timestamp;   // When error occurred
}
```

---

## Syntax Summary

### Keywords
- `fail` - Trigger a failure with an error value
- `trap` - Catch and handle specific error types
- `refail` - Propagate error to caller
- `ensure` - Cleanup code that always runs
- `panic` - Unrecoverable error (crashes immediately)

### Grammar Extensions

```ebnf
block_with_traps ::= block { trap_clause }* [ ensure_clause ]

trap_clause ::= 'trap' '(' IDENTIFIER '<' Type '>' ')' '->' block

ensure_clause ::= 'ensure' '->' block

fail_statement ::= 'fail' expression [';']

refail_statement ::= 'refail' [ expression ] [';']

panic_statement ::= 'panic' '(' STRING_LITERAL ')' [';']
```

---

## Examples

### Example 1: File Processing

```vyb
read_config(path<String>)<Config> -> {
    content<String> = {
        file<File> = open_file(path)
        {
            file.read_all()
        } ensure -> {
            file.close()
        }
    } trap (e<FileNotFound>) -> {
        println("Config not found, using defaults")
        return default_config_string()
    } trap (e<PermissionDenied>) -> {
        fail ConfigError {
            message = "Cannot read config: permission denied",
            path = path
        }
    }

    config<Config> = parse_config(content)
    return config
}
```

### Example 2: Transaction Processing

```vyb
process_transaction(txn<Transaction>)<Result> -> {
    {
        validate_transaction(txn)
        apply_to_database(txn)
        notify_user(txn.user_id)
    } trap (e<ValidationError>) -> {
        println("Transaction invalid: " + e.message())
        return Result { success = false, reason = "validation" }
    } trap (e<DatabaseError>) -> {
        println("Database error: " + e.message())
        rollback_transaction(txn)
        refail  # Critical - propagate to caller
    } trap (e<NotificationError>) -> {
        println("Failed to notify user, but transaction succeeded")
        # Don't fail the transaction
    } ensure -> {
        log_transaction_attempt(txn)
    }

    return Result { success = true, reason = "" }
}
```

### Example 3: Parser with Error Recovery

```vyb
parse_expression(tokens<Vec<Token>>)<Expr> -> {
    expr<Expr> = {
        parse_term(tokens)
    } trap (e<UnexpectedToken>) -> {
        println("Unexpected token, attempting recovery...")
        skip_until_semicolon(tokens)
        fail ParseError { message = "Expression parsing failed" }
    } trap (e<UnexpectedEOF>) -> {
        fail ParseError { message = "Unexpected end of input" }
    }

    return expr
}
```

---

## Design Decisions

### Why Not Try/Catch?
- Try/catch is tired and verbose
- Unclear what code might throw
- No compile-time checking of exception types
- Separates error handling from the code that might fail

### Why Trap?
- **Explicit:** Trap clauses attached to blocks make error handling visible
- **Type-safe:** Compile-time checking of error types
- **Composable:** Easy to chain and nest
- **Fresh:** Unique to Vyb, not borrowed from other languages

### Why Ensure Instead of Finally?
- Shorter, clearer keyword
- Emphasizes "ensuring" cleanup happens
- No confusion with "finalize" or "finalizer" patterns

### Why Fail Instead of Throw?
- More descriptive of what's happening
- Avoids confusion with C++ exceptions
- Pairs nicely with "trap" conceptually

---

## Stack Traces and Debugging

### Overview

When a `fail` occurs, Vyb captures a **source-level stack trace** showing the chain of Vyb function calls leading to the failure. This trace is stored with the error value and can be accessed during trap handling.

### Vyb Source Stack Trace

**Format:**
```
Error: DivisionByZero { dividend = 10 }
  at divide (math.vyb:45:9)
  at calculate_average (stats.vyb:23:15)
  at process_data (main.vyb:67:5)
  at main (main.vyb:12:5)
```

**Captured Information:**
- Function name
- File path (relative to project root)
- Line and column number
- Optionally: parameter values (for debugging)

**Capture Timing:**
- Stack is captured **at the point of `fail`**
- Minimal runtime overhead (only when errors occur)
- Stack is **preserved** through `refail` with additional frames appended

**Example with Stack Access:**
```vyb
process()<Void> -> {
    {
        risky_operation()
    } trap (e<NetworkError>) -> {
        # Access the stack trace
        println("Error occurred:")
        println(e.stack_trace())

        # Optionally refail with additional context
        refail NetworkError {
            message = "Failed in process()",
            cause = e  # Preserves original trace
        }
    }
}
```

### Native Stack Trace (Advanced)

For debugging **C FFI boundaries** or **compiler issues**, Vyb provides access to the native system stack:

**Access:**
```vyb
freedom {
    # Access native stack (DWARF-based)
    native_trace<Vec<StackFrame>> = error.native_stack()

    for (frame in native_trace) {
        println(frame.function_name + " at " + frame.address.to_string())
    }
}
```

**Use Cases:**
- Debugging crashes at C FFI boundaries
- Compiler development and verification
- Low-level systems programming

**Implementation:**
- Uses LLVM debug info (DWARF)
- Stack walking via `libunwind` or platform APIs
- Available only in `freedom` blocks (low-level access)

### Stack Trace with Ownership Cleanup

The stack trace **includes cleanup handlers** to show the full unwinding path:

**Example:**
```vyb
process_file(path<String>)<String> -> {
    {
        file<File> = File.open(path)
        defer file.close()  # Cleanup handler registered

        content<String> = file.read_all()  # May fail
        return content
    } trap (e<IOError>) -> {
        # Stack trace shows:
        # at File.read_all (file.vyb:102:9)
        # at process_file (main.vyb:45:30)
        # cleanup: file.close() at main.vyb:44:15  ← Shows defer

        println("Stack during error:")
        println(e.stack_trace())
        fail FileProcessingError { cause = e }
    }
}
```

### Performance Considerations

**Lazy Stack Capture:**
- Stack is captured **only when `fail` executes**
- No overhead for success paths
- Minimal overhead on error paths

**Debug vs Release Builds:**
- **Debug builds:** Full stack traces with parameter values
- **Release builds:** Function names and locations only
- **Compile flag:** `--strip-stack-traces` for minimal binary size

### Integration with `refail`

When refailing, the **original stack is preserved** and new frames are appended:

**Example:**
```vyb
level1()<Void> -> {
    {
        level2()
    } trap (e<DataError>) -> {
        # Refail preserves original trace
        refail ApplicationError {
            message = "Failed in level1",
            cause = e
        }
    }
}

level2()<Void> -> {
    fail DataError { field = "invalid" }
}

# Resulting stack trace:
# Error: ApplicationError { message = "Failed in level1", cause = ... }
#   at level1 (main.vyb:15:9)  ← Refail frame
# Caused by: DataError { field = "invalid" }
#   at level2 (main.vyb:23:5)  ← Original fail
#   at level1 (main.vyb:12:9)
```

### Stack Trace API

Every error value provides:

```vyb
aspect Errable {
    # Get human-readable stack trace
    stack_trace(self<their<Self>>)<String> -> { }

    # Get structured stack frames
    stack_frames(self<their<Self>>)<Vec<StackFrame>> -> { }

    # Get native stack (freedom only)
    native_stack(self<their<Self>>)<Vec<NativeFrame>> -> { }
}

struct StackFrame {
    function_name<String>,
    file_path<String>,
    line<Int>,
    column<Int>
}

struct NativeFrame {
    function_name<String>,
    address<UInt64>,
    module_name<String>
}
```

### Implementation Requirements

**LLVM Backend:**
- Generate DWARF debug info for all functions
- Include line table information
- Preserve function names even in release builds (optional)

**Runtime Support:**
- Stack walker using DWARF information
- Platform-specific unwinding (`libunwind` on Unix, SEH on Windows)
- Efficient storage for captured frames

**Memory Management:**
- Stack frames stored in error value
- Cleaned up when error value is dropped
- No leaks during unwinding

---

## Runtime Error Handler (Untrapped Failures)

### Philosophy

**Untrapped errors should not silently crash** - they should be caught by the Vyb runtime and reported with full diagnostic information. This provides:
1. **Better debugging** - See exactly what failed and where
2. **Meaningful test results** - Tests can report specific failures
3. **Production reliability** - Graceful degradation instead of segfaults

### Runtime Handler Behavior

When a `fail` occurs **without a matching trap**, the Vyb runtime automatically:

1. **Captures the complete error context:**
   - Error type and values
   - Full Vyb source stack trace
   - Ownership cleanup chain
   - Timestamp and thread info

2. **Executes all cleanup handlers:**
   - Runs `defer` statements in reverse order
   - Executes `ensure` blocks
   - Properly drops all owned values
   - Releases resources (files, memory, locks)

3. **Reports the error:**
   - Prints formatted error message to stderr
   - Shows complete stack trace
   - Optionally invokes custom error handler

4. **Terminates gracefully:**
   - Exit with non-zero status code (default: 1)
   - Allows process cleanup to complete
   - No resource leaks or corruption

### Default Error Report Format

```
┌─ UNTRAPPED FAILURE ─────────────────────────────────────────┐
│ Error: DivisionByZero { dividend = 10 }                      │
│ Thread: main                                                  │
│ Time: 2025-10-20 14:32:11.458                                │
└───────────────────────────────────────────────────────────────┘

Stack Trace:
  at divide (math.vyb:45:9)
  at calculate_average (stats.vyb:23:15)
  at process_data (main.vyb:67:5)
  at main (main.vyb:12:3)

Cleanup Executed:
  defer file.close() at main.vyb:65
  drop temp_buffer at main.vyb:64

Exit Code: 1
```

### Custom Error Handlers

Users can install custom handlers for untrapped errors:

```vyb
# Set custom handler at program start
main()<Int> -> {
    Runtime.set_untrapped_handler(my_error_handler)

    # Rest of program...
    run_application()

    return 0
}

# Custom handler signature
my_error_handler(error<Error>, trace<StackTrace>)<Void> -> {
    # Log to file
    log_file<File> = File.open("errors.log", "append")
    log_file.write("UNTRAPPED: " + error.message() + "\n")
    log_file.write(trace.to_string() + "\n")
    log_file.close()

    # Send to monitoring service
    send_to_monitoring(error, trace)

    # Note: Handler cannot prevent termination
    # Program will still exit after this runs
}
```

### Test Harness Integration

For testing, the runtime can capture untrapped errors instead of exiting:

```vyb
# In test harness
test_division_by_zero()<TestResult> -> {
    result<TestResult> = Runtime.capture_untrapped({
        divide(10, 0)  # This will fail
    })

    match (result) {
        Trapped { error = e } -> {
            # Test can verify the error
            assert(e is DivisionByZero)
            return TestResult::Pass
        },
        Success { value = v } -> {
            return TestResult::Fail { reason = "Expected error but got: " + v.to_string() }
        }
    }
}
```

### Runtime API

```vyb
aspect Runtime {
    # Set custom handler (called before program exits)
    set_untrapped_handler(handler<fn(Error, StackTrace) -> Void>)<Void>

    # Clear custom handler (back to default)
    clear_untrapped_handler()<Void>

    # For testing: capture untrapped errors instead of exiting
    capture_untrapped<T>(block<fn() -> T>)<CaptureResult<T>>
}

enum CaptureResult<T> {
    Success { value<T> },
    Trapped { error<Error>, trace<StackTrace> }
}
```

### Implementation Details

**C++ Runtime Support:**
```cpp
namespace vyb::runtime {
    // Global untrapped error handler
    struct UntrappedErrorHandler {
        // Error context
        struct ErrorContext {
            void* error_object;       // Errable value
            const char* error_type;   // Type name
            std::vector<StackFrame> stack_trace;
            uint64_t thread_id;
            uint64_t timestamp;
        };

        // Handler function pointer
        using HandlerFn = void(*)(const ErrorContext&);

        // Set custom handler
        static void set_handler(HandlerFn handler);

        // Called when untrapped error occurs
        static void handle_untrapped_error(
            void* error_object,
            const char* error_type,
            const std::vector<StackFrame>& stack_trace
        );

        // Default handler: print to stderr and exit(1)
        static void default_handler(const ErrorContext& ctx);
    };
}
```

**LLVM Codegen Integration:**
When a `fail` statement is generated without a trap, emit:
```llvm
; Call runtime error handler
call void @__vyb_runtime_untrapped_error(
    ptr %error_object,
    ptr @error_type_name,
    ptr %stack_trace_data
)

; Handler never returns (noreturn attribute)
unreachable
```

### Panic vs Untrapped Fail

**Important Distinction:**

```vyb
# UNTRAPPED FAIL - Goes through runtime handler
divide(a<Int>, b<Int>)<Int> -> {
    if (b == 0) {
        fail DivisionByZero { dividend = a }
    }
    return a / b
}
# If caller doesn't trap, runtime handler catches it

# PANIC - Immediate crash, no handler involvement
check_invariant(value<Int>)<Void> -> {
    if (value < 0) {
        panic("Invariant violated: negative value!")
        # Crashes immediately, no cleanup, no handler
    }
}
```

**When to use each:**
- **Untrapped fail**: Recoverable errors that should be handled but weren't
- **Panic**: Programming errors, corrupted state, "this should never happen"

### Exit Codes

```
0   - Success (normal exit, or any main() return value that is JSONified)
1   - Untrapped error (default; untrapped `fail` always exits with 1)
2   - Panic
101 - User-defined error (custom handler can set)
```

To exit with a specific code, use the `exit(n)` built-in:

```vyb
main() -> {
    // ... do work ...
    exit(42)   // terminates process with exit code 42; no stdout output
}
```

`exit(n)` is a noreturn built-in that works in any function context. It does NOT produce
any stdout output — use `println` explicitly before calling `exit(n)` if you need output.

> **Note:** `main()<Int>` no longer sets the exit code. As of v0.5.3, returning an `Int`
> from `main()` JSONifies it to stdout (just like Bool, Float, and String) and exits with
> code 0. Use `exit(n)` to set a custom exit code.

### Environment Variables

Control runtime behavior:
```bash
# Verbose error reporting
VYB_ERROR_VERBOSE=1 ./program

# Stack trace depth limit
VYB_STACK_TRACE_DEPTH=50 ./program

# Save errors to file
VYB_ERROR_LOG=/var/log/app.errors ./program

# Disable colors in error output
VYB_ERROR_NO_COLOR=1 ./program
```

---

## Open Questions

1. **Error Type Hierarchy:** Composition vs inheritance vs aspects?
   - **Current preference:** Aspects (most flexible, fits Vyb philosophy)

2. **Automatic Error Propagation:** Should `?` operator auto-propagate?
   ```vyb
   value<Int> = risky_operation()?  # Auto-propagates failure
   ```
   - **Decision needed:** Explicit vs implicit propagation

3. **Multiple Error Types:** Can a block fail with multiple unrelated types?
   ```vyb
   fail NetworkError | FileError  # Union type?
   ```
   - **Decision needed:** Union types or require common base

4. **Error Annotations:** Should functions declare what they can fail with?
   ```vyb
   risky_function()<Int> fails<NetworkError, TimeoutError> -> { ... }
   ```
   - **Decision needed:** Checked vs unchecked failures

---

## Implementation Roadmap

### Phase 1-6: Core Error Handling (v0.4.2 - COMPLETE ✅)
- ✅ `fail` statement parsing and semantic analysis
- ✅ `trap` clause parsing and type checking
- ✅ Multiple trap clauses with type dispatch
- ✅ `refail` statement for error propagation
- ✅ `ensure` clause for cleanup
- ✅ `panic` for unrecoverable errors
- ✅ Runtime VybError infrastructure (C++ level)
- ✅ Heap allocation with type ID + value storage
- ✅ Stack trace capture with source locations
- ✅ Wildcard patterns `trap (e<?>) -> { }`
- ✅ Multi-type patterns `trap (e<A | B>) -> { }`
- ✅ Integration with ownership system
- ✅ Comprehensive test coverage (15+ tests passing)

### Phase 7: Standard Library Error Types (v0.6.1+ - PLANNED)

Expose runtime error system to Vyb code through stdlib:

- [ ] **Errable aspect**: Define aspect for error types
  ```vyb
  aspect Errable {
      message()<String> -> {}
      code()<Int> -> {}
  }
  ```

- [ ] **Error base struct**: Standard Vyb error type
  ```vyb
  struct Error {
      message<String>,
      code<Int>,
      context<Vec<String>>
  }
  ```

- [ ] **Display aspect**: General formatting (not error-specific)
  ```vyb
  aspect Display {
      display()<String> -> {}
  }
  ```

- [ ] **bind implementations**: Implement aspects for Error and common types
  ```vyb
  bind Errable -> Error { ... }
  bind Display -> Error { ... }
  bind Display -> Int { ... }
  ```

- [ ] **Standard library errors**: IOError, ParseError, NetworkError, etc.
- [ ] **Error context chaining**: Wrap errors with additional context
- [ ] **Custom user error types**: User-defined structs implementing Errable

**NOT PLANNED**:
- ❌ Result<T,E> type (trap/fail is superior)
- ❌ `?` operator (errors propagate automatically)
- ❌ Error annotations (type system handles this)

---

## Testing Strategy

### Test Categories

1. **Basic Failure:** Simple fail/trap scenarios
2. **Type Dispatch:** Multiple traps with type hierarchy
3. **Propagation:** Refail and nested traps
4. **Cleanup:** Ensure clause execution order
5. **Integration:** Errors with ownership, async, generics
6. **Edge Cases:** Empty traps, unreachable traps, type mismatches

---

*"Errors are not exceptional - they're part of the program. Handle them with FREEDOM and clarity."*
