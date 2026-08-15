# Vyb Language Roadmap

**Last Updated:** February 2026 (v0.5.1)

> For actionable sprint-organized suggestions, see [`../SUGGESTIONS.md`](../SUGGESTIONS.md).

This document outlines the completed features, ongoing development, and future considerations for the Vyb programming language.

## Current Status (v0.5.1)

**✅ MAJOR ACHIEVEMENTS COMPLETED:**
1.  **LLVM Backend:** Fully functional LLVM IR generation and JIT execution
2.  **Auto-Serialization:** Complex return types automatically serialize to JSON-like output
3.  **Type System:** Working functions, variables, structs, control flow
4.  **Memory Safety:** Ownership types (`my<T>`, `our<T>`, `their<T>`) with borrowing
5.  **Parser:** Comprehensive syntax support including templates, async, aspects
6.  **✅ Pattern Matching (COMPLETED):** Match statements with `->` arrow syntax and `?` wildcard; no-match continues as NOP
7.  **✅ Loop Control Flow (COMPLETED):** Break and continue statements working in all loop constructs
8.  **✅ Resizable Collections (COMPLETED):** Vec<T> with full method support (new, push, pop, len, get)
9.  **✅ Vec Iteration (COMPLETED v0.4.1):** `for (item in vec)` with mandatory parentheses, full break/continue support
10. **✅ Range-Based For Loops (COMPLETED v0.4.1):** `for (i in 0..10)` inclusive ranges with optional step
11. **✅ Member Access (COMPLETED):** Object field access (obj.field) and array indexing (arr[index])
13. **✅ Binary Operations (COMPLETED):** Complete operator precedence system with all arithmetic, comparison, and logical operators
14. **✅ Variadic Tuples (COMPLETED v0.4.0):** `Tuple<T,U,V,...>` with full 1-N type parameter support and dual syntax
15. **✅ Introspection System (COMPLETED v0.4.2):** typeof/typename operators for runtime type reflection
16. **✅ Aspect System (COMPLETED v0.4.2):** User-extensible aspects with bind blocks for polymorphism
17. **✅ Object File Emission (COMPLETED v0.4.3):** AOT compilation to native .o files with optimization levels
18. **✅ Static Linking (COMPLETED v0.4.4):** Full executable generation with runtime library and system linker

### Variadic Tuple System (v0.4.0)

Vyb now supports **fully variadic tuple types** with comprehensive 1-to-N type parameter support:

**Features Implemented:**
- ✅ **Dual Syntax Support:**
  - Inline syntax: `main()<Int, String, Bool>`
  - Generic syntax: `main()<Tuple<Int, String, Bool>>`
  - Both produce identical LLVM anonymous struct types

- ✅ **Complete Variadic Range:**
  - Single-element tuples: `Tuple<Int>` with auto-wrapping
  - Multi-element tuples: 2-7+ elements tested and verified
  - No theoretical upper limit on element count

- ✅ **Type System Integration:**
  - Generic type parameter processing in `cgen_types.cpp`
  - Sequence expression handling in `cgen_expr.cpp`
  - Return statement auto-wrapping in `cgen_stmt.cpp`
  - Full type safety and compile-time checking

- ✅ **Complex Type Support:**
  - Primitive types: Int, Float, Bool
  - String types with proper memory management
  - Collection types: Vec<T> in tuples
  - Custom struct types (planned)

- ✅ **Comprehensive Testing:**
  - 9 test files covering edge cases
  - Single-element tuple edge case verified
  - Mixed type combinations tested
  - All tests pass without crashes or type errors

**LLVM Representation:**
- Tuples compile to anonymous struct types: `{ T1, T2, ..., TN }`
- Efficient memory layout with no overhead
- Compatible with C ABI for struct returns

**Current Limitations** (future enhancements):
- ⏳ Tuple serialization/output (compiles but no JSON output yet)
- ⏳ Tuple pattern matching in match expressions
- ⏳ Tuple element access via `.0`, `.1`, `.2` syntax

See `test/tuples/README.md` for detailed documentation and examples.

## Core Development Focus

### 🔥 HIGH PRIORITY (v0.4.2)
The immediate focus for the next release:

1. **✅ Template Scaffolding (COMPLETED v0.4.1):** Template declarations with type parameter recognition
2. **✅ Generic Function Monomorphization (COMPLETED v0.4.1):** Full LLVM support for generic functions
   - Template storage and on-demand instantiation
   - Type parameter substitution (T → ConcreteType)
   - Method resolution on generic parameters
   - Function specialization with caching
   - Works with aspect bounds: `func<T<Display>>(item: T)`
3. **✅ Aspect System (COMPLETED v0.4.2):** User-extensible aspects with bind blocks
   - ✅ Define aspects with method signatures
   - ✅ Implement aspects for types (bind blocks)
   - ✅ Call aspect methods on values (value.method())
   - ✅ Validate aspect bounds in generics
   - See `doc/ASPECT_SYSTEM_DESIGN.md` for full specification
4. **Generic Struct Instantiation:** Monomorphization for generic structs

### 📋 COMPLETED IN v0.4.3

1. **✅ Object File Emission (COMPLETED v0.4.3):** AOT compilation to native object files
   - Compile to .o files with `--compile/-c` flag
   - Support all optimization levels: -O0, -O1, -O2 (default), -O3
   - Full LLVM target machine integration for all architectures
   - Cross-compilation support (x86-64, ARM, AArch64, RISC-V, etc.)
   - ELF relocatable object files with debug info (DWARF)
   - LLVM 18 API compatibility (CodeGenFileType, CodeGenOptLevel)
   - Foundation for static linking and standalone executables
   - See `doc/MODULE_FFI_BINARY_ROADMAP.md` Phase 3.1

2. **✅ Error Handling System (COMPLETED v0.4.2):** Production-ready `fail`/`trap` error handling
   - Zero-cost success path with heap-allocated error contexts
   - Type-safe pattern matching with struct field access
   - Multi-level error propagation through call stacks
   - Heap allocation with type ID + value storage (16 bytes)
   - Untrapped error runtime handler with formatted output
   - Complete LLVM codegen implementation
   - See "Error Handling Roadmap" section below for future enhancements

### 📋 COMPLETED IN v0.4.4

1. **✅ Static Linking (COMPLETED v0.4.4):** Link .o files into standalone executables
   - ✅ Invoke system linker (ld, lld) with platform detection
   - ✅ Link against libc and runtime libraries
   - ✅ Runtime library implementation (vyb_runtime.c)
   - ✅ Command: `vyb program.vyb --build myapp`
   - ✅ Full compilation pipeline: source → object → executable
   - See `doc/MODULE_FFI_BINARY_ROADMAP.md` Phase 3.2 ✅ COMPLETED

2. **✅ JSON Serialization & Deserialization (COMPLETED v0.4.4):** Bidirectional struct-JSON conversion
   - ✅ Runtime type metadata system with field introspection
   - ✅ Global type registry with automatic registration
   - ✅ `.to_string()` method on structs generates JSON
   - ✅ `T::from_string(json)` creates instances from JSON strings
   - ✅ Support for Int, Float, Bool, String primitive types
   - ✅ Full round-trip conversion: struct → JSON → struct → field access
   - ✅ String conversion: char* to VybString{data, length} struct
   - ✅ Member access fix: always load field values (critical bug resolved)
   - ✅ Comprehensive test suite in `test/json/`
   - ✅ Production-ready with clean codebase (debug output removed)
   - See `runtime/vyb_type_metadata.c` for implementation
   - Future: Nested structs, Vec<T> serialization, aspect metadata

### 📋 CURRENT FOCUS (v0.5.0+)

1. **Optimization Pipeline (v0.5.2):** LLVM optimization pass configuration
   - Apply optimization passes for -O0 through -O3
   - Implement LTO (Link-Time Optimization)
   - Benchmark JIT vs AOT performance
   - See `doc/MODULE_FFI_BINARY_ROADMAP.md` Phase 3.3

3. **Range Patterns in Match/Select:** Implement `a..b` range matching for elegant numeric range handling
   - Syntax: `1..10 -> "single digit"` in match/select expressions
   - Note: Conflicts with comparison patterns (`>= 90`) need resolution
   - Design decision: May use different syntax or restrict to one pattern type

4. **Standard Library Expansion:** Building core modules for collections, I/O, math

5. **Enhanced Error Messages:** More detailed compilation feedback and suggestions

6. **Tuple Element Access:** `.0`, `.1`, `.2` syntax for accessing tuple elements

7. **✅ String Comparison Operators (COMPLETED v0.4.2):** Lexical ordering for String types with all 6 operators (==, !=, <, <=, >, >=) using efficient memcmp-based comparison

## Project Structure and Organization

### Proposed Refactoring (Future Task)
As the Vyb project grows, a more structured directory layout will be beneficial for maintainability, readability, and scalability.

-   **Goal:** Improve overall project organization and clearly separate components.
-   **Proposed Source Structure (`src/`):**
    -   `ast/`: Implementations of AST (Abstract Syntax Tree) nodes.
    -   `parser/`: Lexer, parser logic, and related utilities.
    -   `sema/` or `analyzer/`: Semantic analysis, type checking.
    -   `codegen/` or `llvm_backend/`: LLVM IR generation and compilation logic.
    -   `vre/`: Vyb Runtime Environment components (core runtime, standard library C++ implementations).
    -   `core/` or `common/`: Common utilities, data structures, error reporting used across the compiler.
    -   `main/`: Main executable entry point, REPL implementation.
-   **Proposed Include Structure (`include/vyb/`):**
    -   `ast/`: Header files for AST node definitions.
    -   `parser/`: Header files for parser interfaces, token definitions.
    -   `sema/` or `analyzer/`: Header files for semantic analysis components.
    -   `codegen/` or `llvm_backend/`: Header files for code generation interfaces.
    -   `vre/`: Header files for VRE interfaces and public APIs.
    -   `core/` or `common/`: Header files for common utilities.
-   **Considerations:**
    -   This refactoring will require significant updates to `CMakeLists.txt` to correctly identify source files and manage include directories.
    -   All `#include` paths within the project files will need to be updated.
    -   **Timing:** This is a desirable refactoring but should likely be undertaken after initial VRE/LLVM milestones are achieved, or during a dedicated refactoring phase, to avoid disrupting the current development momentum on core features.

#### Codebase Cleanup and Organization

-   **Address Duplicate Files**: Resolve the duplication of header and source files identified in `/include/vyb/` and `/src/` directories by consolidating them into the `/parser/` subdirectories as planned.
-   **Implement Proposed Directory Structure**: Refactor the codebase to follow the proposed `src/` and `include/vyb/` directory layouts (`ast/`, `parser/`, `sema/`, `codegen/`, `vre/`, `core/`).
-   **Review for Stale/Unused Code**: Conduct a thorough review of the codebase to identify and remove any other stale or unused files and code segments.

### Build System and Includes

-   **`vyb.hpp` as a Central Include:**
    -   The file `include/vyb/vyb.hpp` was initially conceived not just to house the EBNF grammar but to serve as a primary, top-level include file for essential Vyb definitions, core types, and widely used utilities.
    -   Utilities such as `source_location.hpp` and potentially `token.hpp` should ideally be directly included by `vyb.hpp` or be part of a core module that `vyb.hpp` exposes. This approach simplifies include management for different parts of the Vyb compiler and for any external tools that might interact with Vyb's core components.
    -   The EBNF grammar, if kept in `vyb.hpp`, should be clearly demarcated (e.g., within a large comment block) if the file also serves as an active header for code. Alternatively, the EBNF could reside purely in a design document like `AST.md`.

## Future Language & System Considerations

### Type System Implementation

-   **Complete Type Library**: Implement and optimize the full type system including:
    -   **Core Primitives**: Complete implementation of `Int` variants (`Int8`, `Int16`, `Int32`), `Float` variants (`Float32`, `Float64`), `Char`, `Rune`, `Bool`, `Bytes`, and `Void`.
    -   **Compound Types**: Complete implementation of tuples `(T1, T2, ...)`, fixed-size arrays `[T; N]`, dynamic vectors `Vec<T>`.
    -   **✅ String Type (COMPLETED v0.4.0, enhanced v0.4.2)**: Base `String` type complete with fat pointer struct `{ptr: *i8, len: i64}`, natural literal syntax, comprehensive methods (len, substring, char_at, starts_with, ends_with, contains, to_upper, to_lower, +), and full comparison operators (==, !=, <, <=, >, >=) with lexical ordering. Future: UTF-8 operations, `String<Char>` for raw code units, `String<Rune>` for Unicode support.
-   **Performance Optimization**: Investigate performance optimizations for primitive types, particularly in tight loops and math-intensive operations.
-   **Memory Layout**: Define and document memory layout guarantees for all types, ensuring consistent behavior across platforms.
-   **FFI Compatibility**: Ensure all primitive types have well-defined mappings to C/C++ equivalents for FFI interoperability.

### Refinement of Memory Model and Freedom Operations

-   **Complete LLVM Codegen**: Finish the implementation and refinement of LLVM code generation for `LocationExpression`, `PointerDerefExpression`, `AddrOfExpression`, and `FromIntToLocExpression` in `src/vre/llvm/cgen_expr.cpp`.
-   **Enhance Semantic Analysis**: Strengthen semantic checks related to memory operations, including more robust type validation and analysis within `freedom` blocks.
-   **Runtime Checks (Optional)**: Investigate adding optional runtime checks for null pointer dereferences and out-of-bounds access, potentially controlled by build flags.

### Formalization of Compiler Intrinsics

-   **Document Intrinsics Mechanism**: Clearly document the process by which compiler intrinsics (like memory operations) are recognized and handled throughout the compiler pipeline (parsing, semantic analysis, code generation).
-   **Define Interface for New Intrinsics**: Establish a clear interface or pattern for adding new compiler intrinsics in the future.

### Standard Library Development

-   **Core Modules**: Begin implementing core standard library modules for common data structures (e.g., lists, maps), I/O operations, threading primitives, and utility functions.
-   **Integrate with Compiler**: Ensure seamless integration between standard library components and the compiler, especially concerning ownership, borrowing, and generics.

### Improved Error Handling and Reporting

-   **Develop Error Framework**: Implement a more structured error handling and reporting framework beyond simple console logging.
-   **Contextual Error Messages**: Provide more detailed and contextual error messages, including source locations, to aid developers in debugging.

### Testing Infrastructure

-   **Expand Test Coverage**: Write comprehensive unit and integration tests for all language features, with a focus on complex areas like memory management, ownership, concurrency, and generics.
-   **Automated Testing**: Set up automated testing in the build pipeline.

### Documentation Improvements

-   **User Guides and Tutorials**: Create user-friendly guides and tutorials covering language features, build process, and using the standard library.
-   **API Reference**: Generate or manually create a reference documentation for the standard library and core compiler interfaces.
-   **Compiler Internals Documentation**: Add more detailed documentation on compiler internals, including the AST, semantic analysis, and code generation phases.

### Select Expressions

✅ **COMPLETED in v0.4.1** - A powerful expression-based pattern matching construct:

- **Core Concept**: `select` is an **expression** (not a statement) that pattern-matches on a value and returns a result
  - Auto-infers return type from first case
  - Uses `pass` keyword to explicitly return values from blocks
  - Naked expressions auto-return without `pass` keyword
  - Requires semicolon terminator after closing brace

- **Syntax**:
  ```vyb
  result<Int> = select(value) -> {
      1 -> 10,              // Naked expression (auto-returns)
      2 -> 20,
      3 -> {                // Block with statements
          temp<Int> = 300;
          println(temp);
          pass temp         // Explicit return from block
      },
      ? -> 0                // Wildcard default
  };                        // Semicolon required
  ```

- **Key Features**:
  - **Type Inference**: Return type inferred from first case evaluation
  - **Dual Syntax**: Naked expressions OR blocks with `pass`
  - **Expression-Based**: Can be used anywhere an expression is valid
  - **SelectContext Stack**: Supports nested select expressions
  - **Value Equality**: Pattern matching uses exact equality (ICmpEQ)
  - **Wildcard Support**: `?` matches anything

- **Implementation Details**:
  - ✅ PassStatement AST node for explicit returns from blocks
  - ✅ SelectExpression AST node for the expression
  - ✅ Parser architecture: ExpressionParser ↔ StatementParser bidirectional linkage
  - ✅ Type inference with temporary basic block and infer_types_only flag
  - ✅ SelectContext stack tracking endBlock and resultAlloca for nested selects
  - ✅ Semantic analysis checking for pass outside select context

- **Distinctions from Match**:
  - `match`: Statement-based, used for control flow, no return value
  - `select`: Expression-based, returns a value, can be assigned
  - `match` arms execute statements directly
  - `select` arms either auto-return (naked) or use `pass` (blocks)
  - `pass` returns from the **block**, NOT the enclosing function

- **Benefits**:
  - More expressive than nested ternary operators
  - Cleaner than long if-else-if chains
  - Type-safe pattern matching with value returns
  - Natural fit for functional-style programming
  - Supports complex logic in blocks while maintaining expression semantics

**Status**: ✅ Fully implemented and tested with test_select_simple.vyb and test_select_pass.vyb.

#### Range/Comparison Patterns ✅

**Status**: ✅ **Fully implemented in v0.4.1** (October 2025)

An enhancement to both `match` and `select` to support comparison-based patterns for elegant range handling.

- **Motivation**: Enable elegant handling of numeric ranges and ordered comparisons
  - Works with any comparable type (Int, Float currently; String future)
  - Reduces verbose if-else chains for range-based logic
  - Complements exact-match patterns with relational operators

- **Syntax Examples**:
  ```vyb
  // Select expression with comparison patterns
  grade<String> = select(score) -> {
      >= 90 -> { pass "A" },
      >= 80 -> { pass "B" },
      >= 70 -> { pass "C" },
      >= 60 -> { pass "D" },
      ?     -> { pass "F" }
  }

  // Match statement with comparison patterns
  match (temperature) {
      < 0   -> println("Freezing"),
      < 20  -> println("Cold"),
      < 30  -> println("Comfortable"),
      >= 30 -> println("Hot")
  }

  // Mixed comparison operators
  result<Int> = select(value) -> {
      == 0  -> 1,        // Exact equality
      != -1 -> 2,        // Not equal
      > 100 -> 3,        // Greater than
      >= 50 -> 4,        // Greater or equal
      < 10  -> 5,        // Less than
      <= 0  -> 6,        // Less or equal
      ?     -> 7         // Wildcard catch-all
  }
  ```

- **Supported Operators**:
  - `==` - Exact equality
  - `!=` - Not equal
  - `<`  - Less than
  - `<=` - Less than or equal
  - `>`  - Greater than
  - `>=` - Greater than or equal

- **Evaluation Semantics**:
  - Patterns evaluated **top-to-bottom** (order matters!)
  - **First matching pattern wins** (early exit)
  - Wildcard `?` acts as catch-all (typically last)
  - Comparison: `matched_value op pattern_value`
  - Example: Pattern `>= 18` means "is matched_value >= 18?"

- **Unreachable Pattern Detection** ⚠️:

  The semantic analyzer enforces pattern reachability as **compile-time errors**:

  1. **Wildcard in Middle**: `?` followed by any pattern
     ```vyb
     match (x) {
         >= 90 -> "A",
         ? -> "other",      // Catches everything
         >= 80 -> "B"       // ERROR: Unreachable (already caught by ?)
     }
     ```

  2. **Subsumed Comparisons**: Broader pattern before narrower
     ```vyb
     select(score) -> {
         >= 80 -> 1,        // Catches 80+
         >= 90 -> 2         // ERROR: 90+ already caught by >= 80
     }
     ```

  3. **Duplicate Patterns**: Same pattern appearing twice
     ```vyb
     match (x) {
         == 75 -> "first",
         == 75 -> "second"  // ERROR: Duplicate pattern
     }
     ```

  4. **Range Subsumption**: Exact match covered by range
     ```vyb
     select(n) -> {
         >= 70 -> 1,        // Catches 70+
         == 75 -> 2         // ERROR: 75 already caught by >= 70
     }
     ```

  **Error Messages**:
  - `"Pattern after wildcard in case N is unreachable"`
  - `"Pattern '>= 90' in case N is subsumed by earlier '>= 80' (case M)"`
  - `"Pattern '== 75' in case N is duplicate pattern (case M)"`
  - `"Pattern '== 75' in case N is covered by earlier '>= 70' (case M)"`

  **Design Philosophy**:
  - Flexible pattern ordering (no forced monotonic constraints)
  - But **no tolerance for sloppy code** that can never execute
  - Catches common logic errors at compile time
  - Clear, actionable error messages

- **Type Requirements**:
  - Type must support the comparison operator used
  - **Current Support**: Int, Float (built-in comparisons)
  - **Future Support**: String (lexical ordering), Char, Rune
  - **Struct Support**: Requires explicit operator overloading (future feature)

- **Benefits**:
  - Natural expression of range-based logic
  - Compile-time detection of unreachable code
  - Avoids verbose chained if-else statements
  - Works without complex type system (aspects)
  - Clear, readable code for categorization problems

- **Implementation Details**:
  1. ✅ Extended pattern AST with ComparisonPattern node
  2. ✅ Comparison codegen for Int and Float types (LLVM icmp/fcmp)
  3. ✅ Top-to-bottom evaluation order guaranteed in codegen
  4. ✅ Unreachable pattern analysis in semantic analyzer
  5. ⏳ String comparison operators (future: lexical ordering)
  6. ⏳ Full type checking for comparison compatibility

- **Test Coverage**:
  - `test/select_match/test_comparison_simple.vyb` - Basic comparison patterns
  - `test/select_match/test_comparison_patterns.vyb` - All six operators
  - `test/select_match/test_unreachable_patterns.vyb` - Error detection

**Status**: ✅ Fully implemented for Int/Float types with unreachable pattern detection. String comparison operators remain as future enhancement.

### Error Handling System ✅

**Status**: ✅ **Fully implemented in v0.4.2** (October 2025)

A production-ready error handling system combining explicit propagation with zero-cost success paths.

#### **Core Philosophy**

- **Explicit propagation**: Errors visible in type signatures
- **Zero-cost success**: No overhead when operations succeed (Happy Path optimization!)
- **Pattern matching**: Type-safe error handling with full struct field access
- **Heap allocation**: Rich error contexts through 16-byte error structs (type_id + value)

#### **Implemented Features** ✅

**fail Statement:**
```vyb
# Primitive errors
fail 42                    # Integer error code
fail "not found"           # String error message
fail 3.14                  # Float error value

# Structured errors with context
struct DivisionError {
    code<Int>,
    dividend<Int>,
    divisor<Int>
}
fail DivisionError { code = 42, dividend = 10, divisor = 0 }
```

**trap Handlers:**
```vyb
# Postfix syntax with pattern matching
result<Int> = {
    risky_operation()
} trap (e<ErrorType>) -> {
    handle_error(e)
    fallback_value
}

# Multiple error types
{
    operation()
} trap (e<Int>) -> {
    handle_int_error(e)
} trap (e<String>) -> {
    handle_string_error(e)
} trap (e<CustomError>) -> {
    handle_custom_error(e.field)  # Field access!
}
```

**Error Memory Layout:**
```
Heap-allocated struct (16 bytes):
  Offset 0-7:  Type ID hash (i64) - Runtime type dispatch
  Offset 8-15: Error value/data    - Primitive or struct data
```

**Type ID Hashing:**
- `Int` → hash("Int") = -3994496327427856726
- `String` → hash("String") = unique value
- Custom types → hash(type_name) for pattern matching

**Features:**
- ✅ Multi-level error propagation through call stacks
- ✅ Type-safe trap handler matching with compile-time validation
- ✅ Struct field access in trap handlers (`e.code`, `e.dividend`)
- ✅ Automatic memory management (malloc on fail, free in trap)
- ✅ Untrapped error runtime handler with formatted output boxes
- ✅ Complete LLVM codegen with efficient IR generation
- ✅ Zero allocation on success path (Happy Path!)

#### **Planned Enhancements** 🔜

**Phase 6.3 - ensure Keyword: ✅ COMPLETED v0.4.2**
```vyb
# Cleanup/finally blocks that always execute
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

# Execution order:
# 1. Block code executes
# 2. On error: trap handler runs (if matches)
# 3. ensure block always runs last
# 4. Resources guaranteed to be cleaned up

# ✅ Implementation Status (v0.4.2):
# - Parser: Full support for } ensure -> { } syntax
# - AST: EnsureClause with cleanupBlock
# - Semantic: Validates ensure placement
# - Codegen: Inlined control flow (block.normal → block.ensure → block.continue)
# - Test: test/trap/test_ensure_simple.vyb demonstrates working implementation
```

**Phase 6.4 - Stack Trace Capture (Planned for v0.4.4):** 📍 HIGH PRIORITY
```vyb
# Capture source-level stack traces on fail
divide(a<Int>, b<Int>)<Int> -> {
    if (b == 0) {
        fail DivisionError { dividend = a, divisor = b }
    }
    return a / b
}

# Access stack trace in trap handler
{
    result<Int> = compute()
} trap (e<DivisionError>) -> {
    println("Error occurred:")
    println(e.stack_trace())  # Shows full call chain

    # Output:
    # Error: DivisionError { dividend = 10, divisor = 0 }
    #   at divide (math.vyb:45:9)
    #   at compute (calc.vyb:23:15)
    #   at main (main.vyb:12:5)

    -1
}

# Stack trace API on all errors
aspect Errable {
    stack_trace(self<their<Self>>)<String> -> { }
    stack_frames(self<their<Self>>)<Vec<StackFrame>> -> { }
}
```

**Phase 6.5 - Wildcard Pattern (Planned for v0.4.5):**
```vyb
# Catch-all handler for any error type
{
    operation()
} trap (e<?>) -> {
    # Handle any error type
    println("Unknown error occurred")
    println("Error type: " + typename(e))  # ✅ Introspection now available!
    default_value
}
```

**Note**: ✅ Phase 1 introspection (typeof/typename) completed in v0.4.2, enabling wildcard trap implementation. Wildcard patterns now ready for implementation.

**Phase 6.6 - Multi-Type Trap Block (v0.6.0):**
```vyb
# Single trap handler for multiple error types
{
    operation()
} trap (e<Int | String | CustomError>) -> {
    # Handle multiple types in one block
    # Requires introspection to determine actual type
    match (typeof(e)) {
        Int -> handle_int(e as Int),
        String -> handle_string(e as String),
        CustomError -> handle_custom(e as CustomError)
    }
}
```

**Phase 6.7 - Standard Library Error Types (v0.6.1+):**

Note: Runtime error infrastructure (VybError struct, heap allocation, type IDs) already exists.
This phase is about exposing it to Vyb code through standard library types:

- **Errable aspect**: Define aspect for types that can be used as errors
- **Error base type**: Standard Vyb struct wrapping runtime VybError
- **Display aspect**: General formatting aspect for all types
- **bind implementations**: Implement Errable and Display for common error types
- **Error context chaining**: Wrap errors with additional context (needs aspect system)
- **Custom error formatting**: User-defined error display (needs Display aspect)
- **Error metrics hooks**: Optional telemetry integration

**NOT NEEDED**: Result<T,E> - Vyb's trap/fail system is superior for systems programming

#### **Performance Characteristics**

**Happy Path (No Errors):**
- Zero allocation overhead
- Single pointer comparison per call
- Modern CPU branch prediction optimized
- Small functions inline away checks

**Error Path:**
- Single 16-byte malloc allocation
- O(1) type hash comparison
- Single free operation in trap handler
- Stack-based propagation (no exception tables)

**Comparison Table:**
| Approach | Success Overhead | Error Overhead | Hidden Control Flow | Compile-time Safety |
|----------|-----------------|----------------|---------------------|---------------------|
| **Vyb trap/fail** | ~1 comparison | 1 malloc + type match | No | Yes |
| C++ exceptions | Exception tables | Stack unwinding + alloc | Yes | Partial |
| Rust Result<T,E> | Match overhead | Enum size increase | No | Yes |
| Go error returns | Comparison + check | Allocation | No | Weak |

#### **Test Coverage**

Complete test suite in `test/trap/`:
- `test_multilevel_propagation.vyb` - Error propagation through 3 levels
- `test_custom_error.vyb` - Struct errors with field access
- `test_multiple_error_types.vyb` - Multiple trap handlers
- `test_untrapped.vyb` - Runtime error termination
- Plus 11 additional comprehensive tests

**All 15 tests passing** ✅

### Introspection System ✅

**Status**: ✅ **COMPLETED in v0.4.2** (October 2025) - Phase 1 runtime type reflection

A runtime type introspection system providing typeof and typename operators for self-aware programs.

#### **Core Concepts**

**Motivation:**
- Enable multi-type trap handlers with type discrimination
- Support wildcard `?` pattern across language features (match, select, trap)
- Provide foundation for serialization, debugging, and metaprogramming
- Enable safe downcasting and type narrowing

**Fundamental Operations:**
```vyb
# Get runtime type information
type_info<Type> = typeof(value)

# Type comparison
if (typeof(value) == typeof<Int>()) {
    println("It's an integer")
}

# Safe downcasting
if (typeof(error) == typeof<CustomError>()) {
    custom<CustomError> = error as CustomError
    println(custom.field)
}

# Type name as string
name<String> = typename(value)  # "Int", "String", "CustomError"
```

#### **Integration Points**

**1. Wildcard Pattern Support:**
```vyb
# In match/select
result<String> = select(value) -> {
    1 -> "one",
    2 -> "two",
    ? -> "other"  # Wildcard for any remaining value
}

# In trap handlers
{
    operation()
} trap (e<?>) -> {
    # Handle any error type
    println("Error type: " + typename(e))
    match (typeof(e)) {
        typeof<Int>() -> handle_int(e as Int),
        typeof<String>() -> handle_string(e as String),
        ? -> handle_unknown(e)
    }
}
```

**2. Multi-Type Trap Blocks:**
```vyb
# Single handler for multiple types
{
    operation()
} trap (e<ParseError | ValidationError | DatabaseError>) -> {
    # Introspection determines actual type
    name<String> = typename(e)

    if (typeof(e) == typeof<ParseError>()) {
        parse_err<ParseError> = e as ParseError
        println("Parse error at position: " + String::from_int(parse_err.position))
    } else if (typeof(e) == typeof<ValidationError>()) {
        valid_err<ValidationError> = e as ValidationError
        println("Validation error: " + valid_err.constraint)
    } else {
        db_err<DatabaseError> = e as DatabaseError
        println("Database error code: " + String::from_int(db_err.code))
    }
}
```

**3. Generic Serialization:**
```vyb
# Auto-derive serialization using introspection
serialize<T>(value<T>)<String> -> {
    match (typeof(value)) {
        typeof<Int>() -> String::from_int(value as Int),
        typeof<String>() -> value as String,
        typeof<Bool>() -> if (value as Bool) { "true" } else { "false" },
        ? -> {
            # Use struct introspection for custom types
            fields<Vec<FieldInfo>> = reflect_fields(value)
            serialize_struct(value, fields)
        }
    }
}
```

#### **Implementation Phases**

**✅ Phase 1: Basic Type Information (COMPLETED v0.4.2)**
- ✅ `typeof(expr)` operator returning i64 type hash
- ✅ `typename(expr)` returning String type name
- ✅ Type equality comparison (hash-based `==`, `!=`)
- ✅ Hash-based type ID system using std::hash<std::string>
- ✅ LLVM codegen with string struct creation for typename
- ✅ Comprehensive test suite in test/introspection/
- ✅ println() fixed to properly output Vyb string types

**Phase 2: Safe Downcasting (v0.5.0)**
- `as` operator for safe type narrowing
- Compile-time validation of cast compatibility
- Runtime checks for type safety
- Integration with error handling system

**Phase 3: Struct Reflection (v0.5.1)**
- `reflect_fields(value)` for struct introspection
- Field name, type, and offset information
- Read/write field values dynamically
- Enable generic serialization/deserialization

**Phase 4: Advanced Features (v0.6.0)**
- Function signature reflection
- Trait/aspect implementation queries
- Generic type parameter inspection
- Compile-time reflection (const evaluation)

#### **Design Considerations**

**Type Objects:**
- Lightweight runtime representation of types
- Pointer-sized values (8 bytes on x86-64)
- Efficient hash-based comparison
- Integration with existing type ID system

**Safety Guarantees:**
- All downcasts validated at runtime
- Compile-time checks where possible
- Clear error messages for failed casts
- No undefined behavior from incorrect casts

**Performance:**
- Opt-in introspection (no cost if unused)
- Inline typeof() for known types
- Cache type information at function level
- Minimal overhead for dynamic dispatch

**Integration with `?` Wildcard:**
- Consistent behavior across match, select, trap
- Type-safe access to wildcard-matched values
- Compiler enforces introspection usage
- Clear error if accessing without type check

#### **Benefits**

1. **Error Handling**: Multi-type trap blocks reduce boilerplate
2. **Debugging**: Runtime type inspection for diagnostics
3. **Serialization**: Generic serialization without manual code
4. **Metaprogramming**: Foundation for advanced compile-time features
5. **Flexibility**: Safe downcasting when needed
6. **Consistency**: Unified `?` wildcard across language

**Dependencies:**
- Foundation for wildcard trap handlers (Phase 6.5)
- Required for multi-type trap blocks (Phase 6.6)
- Enables generic serialization system
- Supports advanced debugging and tooling

**Status**: Design phase - implementation planned for v0.5.2 alongside error handling Phase 6.5

### Bundles & Sharing System

A planned feature to introduce fine-grained control over module visibility using bundles and sharing:

- **Core Concepts**:
  - **`bundle(...)`**: Declares which bundles (packages/subsystems) a module belongs to
  - **`share(...)`**: Controls which bundles can see each declaration
  - **Import validation**: Ensures modules import only symbols shared with their bundles

- **Visibility Control**:
  - `bundle(sort, sort.Core)` at file level declares package membership
  - `share(all)` exports a symbol to all modules
  - `share(sort.UI, sort.Database)` exports only to specific bundles
  - No `share` prefix keeps symbols private to the file

- **Import Rules**:
  - `import` succeeds only if your bundle list overlaps the target's share list
  - `smuggle` bypasses visibility checks for special cases

- **Benefits**:
  - Fine-grained modular packaging without external configuration
  - Self-documenting module boundaries
  - Compile-time validation of dependencies
  - Flexible opt-out via `smuggle`

See `doc/bundles_and_sharing.md` for detailed documentation.

### Auto-Serialization & Runner Behavior

✅ **IMPLEMENTED in v0.3.7** - Zero-boilerplate JSON serialization for data structures returned from `main()`:

- **Core Functionality**: The Vyb compiler/runtime automatically:
  - Calls `main()` and inspects the return type `T`
  - If `T` is a simple integer, uses it as the process exit code
  - If `T` is complex (tuples, structs), outputs structured JSON-like data
  - Prevents segmentation faults through smart type detection
  - Handles multi-value returns like `main()<Int,String>` perfectly

- **Built-in Auto-Derive**:
  - Primitives (`Int`, `Float`, `Bool`, `String`) will have built-in `to_json`
  - Collections (`[T]`, `Map<K,V>`, `Maybe<T>`) will auto-serialize if their elements do
  - Structs will have auto-derived `ToJson` implementations

- **Customization**:
  - `#[jsonIgnore]` attribute to skip specific fields
  - `#[jsonName="..."]` to specify custom key names
  - Manual implementation of `ToJson` to override auto-derivation

- **Type Safety**:
  - Non-serializable types (functions, raw pointers) will trigger compile-time errors
  - Clear error messages guiding developers to convert or extract serializable data

- **Runner Behavior**:
  - Standard mode: `vyb run program.vyb` will auto-print JSON or exit code
  - Explicit JSON mode: `vyb run --json program.vyb` (future feature)
  - Proper error handling for exceptions and serialization failures

This feature will enable scripts and API-style binaries to return structured data without manual printing logic, enhancing Vyb's utility for data processing and service development.

### Function Syntax Investigation

✅ **IMPLEMENTED in v0.4.0** - **Unified Syntax Revolution**: Successfully replaced keyword-heavy syntax with clean `name<Type>` patterns:
    ```vyb
    example()<Int> -> { ... }  // Modern unified syntax (v0.4.0+)
    fn<Int> example() -> { ... }  // Legacy syntax (still supported)
    ```
-   **Casting Operations**: Formalize and implement casting operations, including:
    -   `cast<T>(expr)`: Explicit casting of expression to type T
    -   Safe versus freedom casts, with appropriate compiler warnings or errors
    -   Rules for implicit conversions between compatible types

### Polymorphism and Type System

-   **Advanced Polymorphism**: Define and implement advanced polymorphic features including:
    -   Dynamic dispatch mechanisms for aspect objects
    -   Performance considerations for virtual method tables versus monomorphization
    -   Aspect object safety rules

### Aspect System Implementation (v0.4.2+)

**HIGH PRIORITY** - Comprehensive aspect system for user-extensible polymorphism.
Vyb uses `aspect`/`bind` — not `trait`/`impl` (Rust vocabulary).

#### Phase 1: Aspect Declarations (v0.4.2) — COMPLETED
- **Aspect Definition Syntax**: `aspect Comparable { lt(self<their<Self>>, other<their<Self>>)<Bool> -> }`
- **Aspect Registry**: Store aspect definitions with method signatures
- **Aspect Validation**: Check method signatures and return types
- **Aspect Composition**: `aspect Comparable requires Equatable { ... }`
- **Default Methods**: Aspect methods with default implementations

#### Phase 2: Aspect Binding (v0.4.2) — IN PROGRESS
- **Bind Block Syntax**: `bind Comparable -> Point { ... }`
- **Binding Validation**: Verify all required methods implemented
- **Aspect Method Calls**: Enable `value.method()` syntax for bound methods
- **Generic Bindings**: `bind<T<Comparable>> Comparable -> Vec<T> { ... }`

#### Phase 3: Generic Monomorphization (v0.4.3)
- **Monomorphization**: Generate specialized code for concrete types
- **Aspect Bound Validation**: Check `<T<Comparable>>` constraints during instantiation
- **Instantiation Cache**: Avoid duplicate code generation
- **Error Messages**: Clear feedback on constraint violations

#### Phase 4: Advanced Aspect Features (v0.5.0)
- **Associated Types**: `aspect Iterator { type Item; ... }`
- **Associated Constants**: `aspect Numeric { const ZERO: Self; ... }`
- **Aspect Objects**: Dynamic dispatch with `dyn Comparable`
- **Multiple Aspect Bounds**: `<T<Comparable><Hashable>>`

#### Phase 5: Higher-Order Features (v0.6.0+)
- **Higher-Kinded Types**: Aspects over type constructors
- **Visibility System**: Module-level privacy for encapsulation
- **Default Type Parameters**: `struct Vec<T, Alloc = DefaultAllocator>`

**Note**: Vyb deliberately avoids classes and inheritance hierarchies. The aspect system
provides all necessary polymorphism and code reuse without the complexity and pitfalls of
OOP inheritance. See `doc/WHY_ASPECTS_NOT_CLASSES.md` for detailed rationale.

See `doc/TRAIT_SYSTEM_DESIGN.md` for complete specification and design rationale.

### Future Development Priorities

The following features are planned for future releases (no particular priority order):

1. **Self-Hosted Standard Library**: Bootstrap a pure Vyb stdlib implementation
   - Core data structures (Vec, Map, Set) in native Vyb
   - I/O primitives and file handling
   - String manipulation utilities
   - Math and numeric operations
   - Eventually replace C++ VRE components

2. **Complete String Implementation**: Finish robust String type
   - UTF-8 encoding/decoding support
   - String interpolation syntax
   - Advanced string methods (split, join, replace, regex)
   - Efficient substring operations

3. **Import/Smuggle System**: Module visibility and dependency management
   - `import` for normal module imports
   - `smuggle` for bypassing visibility restrictions
   - Bundle-based sharing system integration
   - Circular dependency detection
   - Module caching and precompilation

4. **C Bindings and FFI**: Foreign function interface for C interoperability
   - `extern "C"` function declarations
   - C struct layout compatibility
   - Calling C libraries (libc, POSIX, etc.)
   - Enable: I/O, networking, filesystem, process control
   - Safe wrappers around freedom (unsafe) C operations

5. **Robust Async/Await and Threading**: Complete concurrency model
   - Full thread primitive support
   - Cancellation points for cooperative multitasking
   - Signal handling integration
   - Mutex, semaphore, condition variables
   - Timed await with timeouts
   - Thread-local storage
   - Work stealing scheduler (future)

6. **Package Scoping Architecture**: Module system and package management
   - Package definition and structure
   - Dependency resolution
   - Version constraints
   - Package registry integration
   - Build system integration
   - Bundle system implementation (see `doc/bundles_and_sharing.md`)

7. **Additional Considerations**: TBD - user feedback and evolution

### Other Language Considerations

The following points were previously noted in `ROADMAP.txt` and are retained here for future planning:

-   **Template Placement**: Explore allowing template/generic declarations in more contexts beyond just module-level (e.g., within functions, nested scopes) if deemed beneficial for advanced metaprogramming scenarios. Currently, templates are primarily module-level items.
-   **Dedicated Header Files (`.vyh` or similar)**: Investigate the potential need for dedicated header files (e.g., `.vyh`) for separating public interfaces, public template/generic definitions, and type declarations from implementation files (`.vyb`). This could improve organization, reduce compilation dependencies, and potentially speed up compile times for larger Vyb projects by allowing for more explicit module boundaries.

## Documentation
Key design and planning documents for Vyb:

-   `doc/AST.md`: Detailed description of the Vyb Abstract Syntax Tree nodes and structure.
-   `doc/VRE.md`: Preliminary design for the Vyb Runtime Environment.
-   `doc/ROADMAP.md`: This document, outlining project direction and future plans.

---
*This roadmap is a living document and is subject to change as development progresses.*
