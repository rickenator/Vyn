# Vyb Programming Language - Changelog

All notable changes to the Vyb programming language will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Added
- Primitive ownership unwrap-on-read and move tracking:
  - Reading a `my<T>` / `our<T>` / `mild<T>` primitive now unwraps to the underlying value.
  - Compile-time move tracking for `my<T>` bindings rejects use-after-move and
    records ownership transfer on assignment, initialization, and `my<T>` argument passing.
- Aspect binds to concrete generic instantiations:
  - `bind Display -> Box<Int>` (and similar shapes) now resolve and monomorphize into executable methods.
- Error propagation Phases 3–5 for `fail`/`trap`:
  - `fail` without in-scope trap now returns the failable ABI tuple and propagates to caller.
  - Call sites of failable functions now auto-check `{value,error}` and propagate on non-null error.
  - New trap tests for propagation, defer-on-fail, untrapped main propagation, and non-failable caller rejection.
- Milestone gate coverage for current error propagation:
  - `test/error_trap/phase2` is now part of the required gate.
  - Focused trap fixtures now cover propagated calls, failable `main`, defer cleanup on propagated fail, and non-failable caller rejection.
- `Vec::pop()` now returns the removed primitive value instead of a placeholder and safely returns the default value for empty `Vec<Int>`.
- Minimal `our<T>` / `mild<T>` control-block runtime:
  - `our(expr)` allocates a payload plus strong/weak/released metadata.
  - `soft(ourValue)` creates a `mild<T>` handle by incrementing weak_count.
  - `mild<T>.released()` now observes release after the local strong owner is dropped.
  - `mild<T>.grab()` upgrades live weak handles to `our<T>` and returns a null `our<T>` placeholder for released targets until `Option<T>` exists.

### Changed
- Bind selection precedence: when both a bounded and an unbounded generic aspect
  bind match the same type shape (`bind<T<Aspect>>` + `bind<T>`), the bounded
  (more specialized) bind now wins deterministically regardless of declaration
  order. Previously the registry keyed generic binds by pattern only, so the
  duplicate overwrote itself in last-declared-wins order.
- Ownership transfer-on-return now covers expression returns: an owning value
  (Vec with malloc'd data, `our<T>`, `mild<T>`) returned through a `select` expression
  (or any whole-value read) transfers to the caller instead of being freed first,
  fixing a `free(): double free detected` crash. Added an `@category: ownership, vec`
  regression test.
- Generic-function monomorphization hardens: caller IR insertion point is restored after
  monomorphizing a generic function, and call-frame push/pop stays balanced across monomorphized
  trait-method bindings (fixes `printItem_Point` "no terminator" crashes).
- Monomorphized trap-handler bodies get their own scope so `return` no longer pops the enclosing
  function scope (fixes `ERROR: No active scope to register variable`).
- Released `DIBuilder` during `releaseModule`/`releaseContext` so debug-metadata teardown no longer
  runs against a freed LLVM context — removes the flaky concurrent-run `SIGSEGV`.
- Runtime `__vyb_runtime_untrapped_error` now reports error type, JSON payload, fail source location, and honors `exitCode<Int>` payload fields.
- JIT `main` dispatch now checks failable-main error tuple returns and routes non-null errors to the untrapped runtime handler.
- Returning a local `our<T>` or `mild<T>` now transfers the handle to the caller instead of cleaning it up before return.
- `our<T>` member access now unwraps through the control block payload pointer before loading fields.
- Milestone minimum raised from 126 to 156 passing tests.

## [0.5.0] - 2026-02-24 (freedom-1.0 series)

### Added
- **C-like Enum codegen**: Enum variants now compile to sequential `i64` integer constants.
  - `enum Direction { North, South, East, West }` declares 4 constants (0, 1, 2, 3)
  - Variant access via `Direction::North` syntax works in all expression contexts
  - Enum variant values integrate seamlessly with `match` and comparison operators
  - Semantic analysis recognizes enum type names; no false "undefined identifier" errors
  - Future: tagged unions with associated data (`Circle(Float)`) planned for v0.6

### Improved
- **Silent compiler by default**: Optimization-pass progress messages (`"Applying IR optimization passes"`, `"Skipping IR optimization"`, etc.) are now gated behind `--debug-codegen`; the compiler is quiet during normal use
- **CMakeLists.txt version**: Project version updated from `0.3.5` → `0.5.0` to match the language's actual state

### Status
Vyb v0.5.0 delivers a complete systems programming language with LLVM backend, native code generation, generics, aspect/bind polymorphism, pattern matching, defer, error propagation (fail/trap), async/await stubs, Vec<T>, String methods, and now C-like enums.

---

## [0.4.2] - 2025-10-20 (freedom-1.0 series)

### Language Philosophy
- **FREEDOM Revolution**: Replaced `unsafe` keyword with `freedom` throughout the language
  - Philosophy: Trade safety for FREEDOM - empowering programmers over compiler restrictions
  - All `unsafe` blocks → `freedom` blocks
  - `KEYWORD_UNSAFE` → `KEYWORD_FREEDOM` in lexer and parser
  - 71 files updated with global refactoring

### Major Features
- **Generic Function Monomorphization**: Complete LLVM implementation for generic functions
  - Template storage and on-demand instantiation
  - Type parameter substitution (T → ConcreteType)
  - Method resolution on generic parameters
  - Function specialization with caching
  - Works seamlessly with aspect bounds: `func<T<Display>>(item: T)`

- **Aspect System Foundation**: User-extensible aspects with bind blocks
  - Define aspects with method signatures
  - Implement aspects for types using `bind Aspect -> Type` syntax
  - Generic functions call aspect methods on bounded type parameters
  - Full semantic validation and aspect registry

### Improved
- **Documentation**: Comprehensive updates for v0.4.2
  - Updated all version banners to 0.4.2 (freedom-1.0 series)
  - Added working aspect method call examples
  - Simplified roadmap emphasizing production-ready status
  - Fixed broken documentation links

- **Code Quality**: Cleaned up temporary and test output files
  - Removed generated LLVM IR files (*.ll)
  - Removed old test results and reports
  - Removed obsolete test scripts

### Status
Vyb v0.4.2 is a **fully functional, production-ready systems programming language** with complete core features, generic functions, and aspect system foundation.

### Tagged Release
**freedom-1.0**: First release emphasizing programmer FREEDOM with generic functions and aspect system

---

## [0.4.0] - 2025-10-17

### Major Infrastructure Upgrade
- **MCJIT to ORC JIT Migration**: Complete replacement of deprecated MCJIT with modern LLVM ORC JIT
  - Migrated from `ExecutionEngine` to `LLJIT` (LLVM 18 ORC JIT infrastructure)
  - Updated symbol registration from `addGlobalMapping()` to `SymbolMap` with `ExecutorSymbolDef`
  - Enhanced memory management with `ThreadSafeModule` support
  - Added `releaseContext()` method to codegen for proper context handling
  - Comprehensive symbol registration for standard library functions (`malloc`, `free`, `memset`)
  - Proper handling of mangled symbol names with variants (.1, .2, etc.)

### Fixed
- **Segmentation Fault Resolution**: Completely resolved segmentation faults in Vec system memory management
  - Fixed crashes occurring during JIT execution of functions with malloc/free operations
  - ORC JIT provides better isolation between JIT memory and application memory
  - Vec system memory management now works perfectly with multiple object creation and cleanup
  - Proven through comprehensive testing with multiple Vec creation scenarios

### Improved
- **JIT Performance**: Modern ORC JIT provides better performance and stability
- **Memory Safety**: Enhanced separation between compiler memory and runtime memory
- **Function Execution**: Robust function pointer conversion using `ExecutorAddr` API
- **Symbol Resolution**: Better handling of runtime symbol lookup and registration

### Technical Details
- **New LLVM Headers**:
  - `llvm/ExecutionEngine/Orc/LLJIT.h`
  - `llvm/ExecutionEngine/Orc/ThreadSafeModule.h`
  - `llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h`
- **Updated Function Lookup**: Replaced `FindFunctionNamed()` with `jit->lookup()` pattern
- **Enhanced Symbol Registration**: `ExecutorAddr::fromPtr()` for proper symbol mapping
- **Memory Function Support**: Full registration of malloc/free/memset with proper mangling

### Migration Impact
- **Developer Experience**: No changes to Vyb language syntax or semantics
- **Runtime Stability**: Dramatically improved stability for memory-intensive operations
- **Vec System**: Full functionality restored with automatic cleanup working perfectly
- **Performance**: Better JIT compilation performance with modern LLVM infrastructure

### Test Results
- **Memory Safety**: Multiple Vec creation and destruction without crashes
- **Function Calls**: Complex function call chains with malloc/free operations
- **Return Values**: Proper exit code handling and complex return type serialization
- **Compilation**: Faster and more reliable JIT compilation process

### Files Modified
- `src/main.cpp` - Complete MCJIT to ORC JIT conversion with symbol registration
- `include/vyb/vre/llvm/codegen.hpp` - Added ThreadSafeModule support methods
- Standard library integration remains unchanged, maintaining API compatibility

---

## [0.3.6] - 2025-06-02

### Added
- **Complete ToString Infrastructure**: Implemented comprehensive toString functionality for proper type-aware string concatenation
  - Added 16 toString intrinsic functions for all basic types (Int, Int8-64, UInt8-64, Float, Float32, Bool, String, Char, Rune, Byte)
  - Automatic type-aware string concatenation: operations like `"User ID:" + id` now work when `id` is a type alias (e.g., `UserId` which resolves to `Int`)
  - Type alias resolution system that resolves aliases to base types for proper toString selection
  - Mixed-type string concatenation with automatic toString conversion for non-string operands
  - JIT function registration for all toString functions enabling runtime execution

### Improved
- **Enhanced String Operations**: String concatenation now handles mixed types automatically
  - Binary expression handler detects when at least one operand is a string and triggers toString conversion
  - Seamless integration with existing type system and code generation
- **Better Type Resolution**: Added helper methods for resolving type aliases to base type names
- **Runtime Integration**: Complete JIT registration system for toString functions

### Fixed
- **String Concatenation Type Errors**: Fixed issues where mixing string literals with type aliases caused compilation errors
- **Return Statement Serialization**: Enhanced serialization to handle complex tuple patterns like `{ i64, ptr, i8 }` with meaningful field names
- **Type-aware Code Generation**: Improved LLVM IR generation for mixed-type operations

### Technical Details
- **New Intrinsic Functions**:
  - `__vyb_toString_int()`, `__vyb_toString_int8()`, `__vyb_toString_int32()`, `__vyb_toString_float()`, `__vyb_toString_bool()`, `__vyb_toString_string()`
  - Extended support: `__vyb_toString_int16()`, `__vyb_toString_int64()`, `__vyb_toString_uint8-64()`, `__vyb_toString_float32()`, `__vyb_toString_char()`, `__vyb_toString_rune()`, `__vyb_toString_byte()`
- **New Helper Methods**:
  - `generateToStringCall()` - converts values to strings based on type
  - `generateMixedStringConcatenation()` - handles mixed-type concatenation
  - `resolveTypeAliasToBaseName()` - resolves type aliases to base type names
- **Enhanced Code Generation**: Modified binary expression handler in `cgen_expr.cpp` to detect string operations and trigger automatic conversion

### Test Results
- **String Concatenation**: `"User ID: 42"`, `"User Name: Alice"`, `"Score: 100"`
- **JSON Serialization**: `{"UserId":42,"UserName":"Alice","Score":100}`
- **Type Safety**: Maintains type safety while enabling intuitive string operations

### Files Modified
- `src/vre/intrinsics.cpp` - Added 16 toString functions and enhanced serialization
- `include/vyb/vre/llvm/codegen.hpp` - Added method declarations for string conversion helpers
- `src/vre/llvm/cgen_string.cpp` - Implemented helper methods for type resolution and mixed concatenation
- `src/vre/llvm/cgen_expr.cpp` - Modified PLUS case in binary expression handler
- `src/main.cpp` - Added comprehensive toString function declarations and JIT registration

---

## [0.3.5] - 2025-05-26

### Added
- **Comprehensive Auto-Serialization Capabilities**: Added full support for automatic serialization of structured data types when returned from `main()` functions
  - New serialization mode intrinsics: `lit()`, `notype()`, `bare()`, `deserial()`
  - JSON construction intrinsics: `__vyb_serialize_to_json()`, `__vyb_serialize_struct_with_names()`
  - Array and object construction functions for manual JSON building
  - Automatic activation for structured return values from `main()`
  - Comprehensive documentation in `doc/Intrinsics.md` Section 7

### Improved
- **Enhanced Parser Error Handling**: Improved error messages and handling for common syntax mistakes
- **Documentation Updates**:
  - Updated all version references from 0.3.4 to 0.3.5
  - Enhanced feature descriptions in README.md
  - Comprehensive auto-serialization documentation added to intrinsics guide
  - Updated installation guide to reference v0.3.5

### Fixed
- Parser error handling for malformed syntax constructs
- Documentation consistency across all files

### Documentation
- Updated `README.md` with enhanced feature descriptions and v0.3.5 installation guide
- Updated `doc/AST_Declarations.md` version reference to v0.3.5
- Comprehensive auto-serialization documentation added to `doc/Intrinsics.md`
- Updated project version in `CMakeLists.txt`

### Supporting Tests
The following test files validate the v0.3.5 functionality:

#### Auto-Serialization Tests
- **`test/test_auto_serialize_basic.vyb`**: Basic auto-serialization without intrinsics (multi-value return)
- **`test/test_lit_intrinsic_simple.vyb`**: Simple `lit()` intrinsic for raw JSON literal output
- **`test/test_lit_intrinsic_multiple.vyb`**: Multiple `lit()` intrinsics generating JSON array output
- **`test/test_notype_intrinsic.vyb`**: Error handling test for `notype()` with primitives (should fail)
- **`test/test_notype_struct.vyb`**: Proper `notype()` usage with structs for metadata suppression
- **`test/test_lit_primitives.vyb`**: Additional primitive type serialization tests

#### Multi-Value Return & Function Tests
- **`test/test_multi_value_return.vyb`**: Multi-value function returns with auto-serialization
- **`test/test_multi_value_parser.vyb`**: Parser validation for multi-value syntax
- **`test/simple_fn_test.vyb`**: Simple function declaration and execution
- **`test/direct_return.vyb`**: Direct return value handling

#### Parser Error Handling Tests
- **`test/test_function_syntax_error_handling.vyb`**: Enhanced error messages for common function syntax mistakes
- **`test/units/parser/test*.vyb`**: Comprehensive parser validation suite (58 test files)
- **`test/units/extracted/test*.vyb`**: Extracted test cases for edge cases (100+ test files)

#### Struct & Type System Tests
- **`test/test_struct_syntax.vyb`**: Advanced struct declarations with auto-serialization
- **`test/test_struct_syntax_simplified.vyb`**: Simplified struct syntax validation
- **`test/test_type_alias.vyb`**: Type alias functionality
- **`test/test_type_alias_simple.vyb`**: Simple type alias cases

#### Integration & Semantic Tests
- **`test/test_semantic_integration.vyb`**: Full semantic analysis integration
- **`test/debug_test.vyb`**: Debug output and analysis validation
- **`test/println_test.vyb`**: Basic output functionality

#### Relaxed Syntax Tests
- **`test/test_relaxed*.vyb`**: Relaxed syntax parsing for improved developer experience
- **`test/units/test_relaxed*.vyb`**: Additional relaxed syntax validation

**Test Statistics:**
- **Core Feature Tests**: 15+ dedicated auto-serialization and multi-value tests
- **Parser Tests**: 58 comprehensive parser validation tests
- **Extracted Tests**: 100+ edge case and regression tests
- **Integration Tests**: 10+ semantic and integration validation tests
- **Total Test Coverage**: 180+ test files ensuring robust v0.3.5 functionality

All tests include expected output validation and error condition testing where appropriate.

---

## [0.3.4] - Previous Release

Previous version with support for:
- Advanced constructs like asynchronous programming
- Generic templates and operator overloading
- Class declarations within templates
- Comprehensive test suite validation

---

*For detailed documentation on auto-serialization capabilities and configuration, see `doc/Auto_Serialization_Main_Returns.md`.*
