# Vyb Programming Language - Changelog

All notable changes to the Vyb programming language will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Added
- `Option<T>` is now a first-class built-in generic data enum
  (`enum Option<T> { Some(T), None }`), so nullable values no longer require the
  transitional `core::option::OptionInt` bridge (kept for source-compat). It
  supports qualified construction (`Option<Int>::Some(42)`, `Option<Int>::None`)
  and type-inferred bare construction (`Some(x)` / `None` when the enclosing
  variable declaration or function return type is `Option<T>`). It is registered
  directly in the compiler (semantic generic-enum template plus a codegen
  tagged-union layout, monomorphized per payload type), so it needs no `import`.
  It integrates fully with `match`/`select` variant dispatch and exhaustiveness
  checking, unwrapping both primitive and heap (e.g. `String`) payloads.

- `Vec::new()` / `Vec::new(n)` are superseded by a vybish constructor form:
  `Vec()` builds an empty growable vector and `Vec(n)` builds an n-element,
  zero-initialized vector, both with the element type inferred from the variable
  annotation (`v<Vec<String>> = Vec()`). The legacy `Vec::new()` / `Vec::new(n)`
  forms remain as a back-compat alias so existing code keeps compiling. Tests,
  examples, demos, and the README now use the new idiom.

- `Result<T, E>` is now a first-class built-in generic data enum
  (`enum Result<T, E> { Ok(T), Err(E) }`), for fallible operations. It is
  registered directly in the compiler (semantic generic-enum template plus a
  codegen tagged-union layout, monomorphized per payload type), so it needs no
  `import`. It supports qualified construction
  (`Result<Int, String>::Ok(42)` / `Result<Int, String>::Err("boom")) and
  type-inferred bare construction (`Ok(x)` / `Err(e)` when the enclosing
  variable declaration or function return type is `Result<T, E>`). It integrates
  fully with `match`/`select` variant dispatch and exhaustiveness checking,
  unwrapping both primitive and heap (e.g. `String`) payloads in either the
  `Ok` or `Err` position.

- Enum methods via `bind` on enum target types: an aspect `bind` can now target a
  user-defined enum (concrete or generic, e.g. `bind Render -> Box<Int>`) and the
  built-in generic enums `Option<T>` / `Result<T,E>` — the semantic gate no longer
  rejects enum instantiations as unknown types, and methods dispatch on the
  concrete variant with the substituted payload (e.g. `area(self)<Float>` on
  `bind HasArea -> Shape { ... }`, matched over `Circle(r)` / `Rect(w,h)`).

### Fixed
- `select` now supports data-carrying enum variants directly: arms like
  `Circle(r) ->`, `Rect(w, h) ->`, and `Unit ->` dispatch on the runtime tag and
  bind payload fields as arm-scoped locals, matching the `match` behavior.
  `select` on a tagged-union enum also enforces exhaustiveness — it must cover
  every variant or include a wildcard — and reports the missing variant(s)
  otherwise. (Previously `select` could only compare literals/comparisons, and
  an enum variant was mis-parsed or unresolved.)
- Exhaustiveness now accounts for guard clauses on enum variant arms: a variant
  covered only by a guarded arm does not count as unconditionally covered, since
  the guard can be false and leave a no-match path. A match is exhaustive only
  when every variant has an unguarded arm (or a guarded arm plus a separate
  unguarded duplicate, or a wildcard). Codegen and the semantic check stay in
  sync on this rule.
- A `match` on a tagged-union enum that is not exhaustive — no unguarded
  wildcard and not covering every variant — is now rejected by the semantic
  analyser with a diagnostic listing the missing variant(s). (Previously the
  codegen treated only the exhaustive case as sound; a missing variant now fails
  to compile instead of reaching an impossible default.)
- A `match` on a tagged-union enum whose arms cover every variant is now
  recognized as exhaustive: its no-match default block is marked `unreachable`,
  so a non-void function whose final statement is such an exhaustive all-return
  `match` compiles cleanly instead of raising a spurious "may not return on all
  paths" diagnostic.
- A non-void function whose last statement is a `match` whose arms all `return`
  (with no trailing `return` after the match) no longer leaves an unterminated
  basic block that trips the LLVM verifier. The fall-through block is now
  terminated with `unreachable`, so compilation produces valid IR and reports a
  clean "may not return on all paths" diagnostic instead of crashing. Arms that
  return via a final wildcard (`? -> ...`) define a complete function and run
  cleanly.
- `fail` inside a callee (e.g. an `if`/`else` branch or an `ensure cond else
  fail<...>(...)`) is now trapped correctly by the caller. Trap contexts were a
  shared, non-function-local stack, so a `fail` in a callee could branch into the
  caller's trap landing pad and store into the caller's error slot, producing
  invalid IR and a crash. Trap contexts are now scoped per function, so a `fail`
  in a callee propagates through the failable ABI instead.
- Monomorphized generic function bodies now isolate trap context and scope
  tracking from the caller. Previously a generic callee that returned from an
  `else` branch could leave the caller's scope stack empty (spurious
  "No active scope to register variable" warnings) and a `fail` inside a generic
  callee could escape into the caller's trap context.
- Failable generic functions now use the same `{T, i8*}` error-return ABI as
  normal functions: a monomorphized body's `fail` returns the error through the
  failable ABI, and the call site detects the error and routes it to the
  caller's `trap` at runtime (or errors as an untrapped failure). Previously a
  generic `fail` was compiled as if non-failable, so it surfaced as an
  untrapped failure even when the caller trapped it.

### Added
- **Generic data enums** — `enum Box<T> { Value(T), Empty }` now builds a
  value-semantics tagged-union struct (`{ i64 tag, [N x i8] data }`) per concrete
  instantiation, constructed via explicit type arguments (`Box<Int>::Value(42)`,
  `Box<Int>::Empty`). The payload type is substituted for the type parameter
  (e.g. `Value` carries `Int` in `Box<Int>`, `String` in `Box<String>`), and
  `match`/`select` dispatch on the variant and bind the substituted payload, with
  the same exhaustiveness checks as non-generic enums.
- Tagged-union (data-carrying) enums and enum variant patterns in `match`:
  `enum Shape { Circle(Float), Rect(Float, Float), Unit }` now compiles to a
  value-semantics `{ i64 tag, [N x i8] data }` union, constructs via
  `Shape::Circle(x)` / `Shape::Rect(a, b)`, and matches on variants
  (`Circle(r) ->`, `Rect(w, h) ->`, `Unit ->`) by comparing the runtime tag and
  binding payload fields. C-like integer enums are unchanged. Generic data
  enums and `select` variant destructuring are deferred.
- Generic function calls now accept explicit type arguments, e.g.
  `probe<Int>(0, 0)`. Previously `name<Type>(...)` was mis-parsed as a variable
  declaration (`name` of type `Type`) followed by a bare `( ... )` sequence, so
  such a call silently compiled to a no-op instead of invoking the function.
  Explicit type args now flow into generic monomorphization (they are used
  directly rather than inferred, which also supports zero-argument generic
  calls), and the call uses the same failable `{T, i8*}` ABI as inferred
  generic calls so a `fail` inside is still caught by the caller's `trap`.
- `ensure` contract statements: `ensure cond else handling` runs `handling`
  whenever `cond` is false. It desugars to `if (cond) { } else { handling }`
  and so plugs directly into the `fail`/`trap` error system. The handling may
  be a block or a single statement (`return`, `fail<Error>(...)`, etc.), and
  may be followed by more statements in the same body. Also fixed `return`
  parsing so a `return` that is not the final statement in a block (e.g.
  `ensure x > 0 else return -1` followed by more code) terminates correctly.
- `match` as a value-returning expression: `r<Int> = match (v) {
  1..3 -> 10, 4..6 -> 20, ? -> 30 }` stores the matched arm's value into
  the variable. The result type is inferred from the first arm's yielded
  value and codegen shares a zero-initialized result slot across all arms.
  Naked-expression arms (including ranges, guards, and struct destructuring)
  yield their value directly, and block arms yield via `pass` (`2 -> {
  pass 20 }`), mirroring `select`. Mixed naked/block arms share the same
  slot. A plain statement-position `match` still produces no value.
  Codegen unifies the select/match yield contexts so `pass` resolves to the
  innermost enclosing value-yielding expression.
- Guard clauses in `match` arms: `pattern if condition ->` only runs the arm
  when the pattern matches AND the condition is true. The guard runs after the
  pattern matches (so it can read destructured struct fields); a false guard
  falls through to the next arm or the default. A guarded wildcard is treated as
  non-exhaustive, so later arms (and the no-match fall-through) remain reachable.
- Inclusive range patterns in `match` arms: `1..10 ->` matches a value within
  `[start, end]` (integer or float). A range whose start is greater than its end
  is statically rejected as never-matchable.
- Struct destructuring in `match` arms: a `Point { x, y }` pattern binds each
  listed field as a local variable in the arm body (extracted from the matched
  struct value). Field names are validated against the struct, and a struct
  pattern that can never match the match expression's static type
  (e.g. `Int` vs `Point { ... }`) is rejected at semantic analysis. Also fixed
  typed-struct-literal detection in `parse_primary` so `Type { ... }` is
  recognized even when a leading newline separates it from a preceding token
  (the match-arm case).
- A bind method whose return type is `Self::Item` inside the bind body now
  resolves the associated type in both concrete and generic binds. For a
  concrete bind (`bind Iterator -> CounterIter { type Item = Int; next(self)<Self::Item> }`)
  the return type resolves to the assigned type, and for a generic bind
  (`bind<T> Iterator -> Boxer<T> { type Item = T; next(self)<Self::Item> }`) the
  type parameter is substituted with the concrete type argument at the call site
  (e.g. `Boxer<Int>.next()` returns `Int`). The impl context is established before
  the monomorphized method signature is built so parameter and return types
  resolve against the specialized type and its associated-type bindings.
- Qualified aspect-method disambiguation: `Aspect::method(receiver, ...)` now
  selects a specific aspect whenever multiple bound aspects declare the same
  method name for a type (e.g. `DisplayA::show(thing)` vs `DisplayB::show(thing)`).
  Unqualified ambiguous dot-calls (`thing.show()`) remain rejected. To support
  this, bind-method symbols are emitted per `Type_Trait_Method` so distinct
  implementations coexist in the same module.
- Qualified aspect calls also work on bounded type parameters inside generic
  functions: `Aspect::method(thing)` where `thing<T<Aspect>>` resolves the return
  type from the bound aspect's signature and dispatches to the correct concrete
  bind for each instantiation.
- Default associated types: an aspect may declare `type Item = Int`, and a bind
  for that aspect can omit the explicit `type Item = ...` assignment and inherit
  the default. An explicit assignment still takes precedence. A missing assignment
  without a declared default remains a semantic error.
- Associated-type aspect bounds (`type Item<Display>` / `type Item: Display + Clone`):
  the type assigned to (or defaulted for) an associated type must implement every
  constrained aspect. Non-conforming assignments and bounds naming undefined
  aspects are rejected at bind validation.
- Associated types through generic binds: a generic bind such as
  `bind<T> Iterator -> Boxer<T> { type Item = T; next(self)<T> }` now resolves
  the associated type (and a type-parameter return type) to the concrete type at
  the call site, so `Boxer<Int>.next()` types and runs as `Int`. Generic bind
  method return types that reference a type parameter are substituted with the
  concrete type argument during semantic typing, and `Self::Item` in a bind
  method's signature is resolved symmetrically during signature matching.
- Aspect inheritance (super-aspects): `aspect Comparable : Equatable` declares a
  super-aspect. Super-aspect names are validated against defined aspects, cyclic
  super-aspect dependencies are rejected, and binding a sub-aspect requires the
  same type to also bind each super-aspect (checked in an order-independent pass
  after all binds are registered).
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
- Generic function calls now infer type arguments from the call-site arguments,
  substitute them into the return type, and validate declared aspect bounds —
  a concrete instantiation whose type does not bind the bound aspect is rejected
  with a clean diagnostic. Previously callers received the raw placeholder type
  (e.g. `T`) and unsatisfied bounds silently passed.
- Fixed a generic-function monomorphization scope imbalance: calling a second,
  distinct generic function in one body popped the caller's codegen scope, causing
  `ERROR: No active scope to register variable` for the second result. The function
  scope is now balanced only when the monomorphized body falls through.
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
