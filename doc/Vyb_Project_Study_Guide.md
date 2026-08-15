# Vyb Project Study Guide

Status: living document
Audience: practitioners, language implementers, and systems programmers
Last reviewed: 2026-05-27
Repository reviewed: `~/Projects/Vyb`

This document is the durable orientation guide for the Vyb project. It is meant
to be updated as the implementation moves, and to give serious readers a single
place to understand the language, compiler, runtime, test corpus, and current
engineering risks.

When documents disagree, prefer this evidence order:

1. Source code in `src/`, `include/`, `runtime/`, and `stdlib/`
2. Focused tests in `test/`
3. `doc/FEATURE_STATUS.md`
4. `UPDATE_LOG.md`, `CHANGELOG.md`, and recent git history
5. Older roadmap/proposal documents

## 1. Repository And Git Context

Current local checkout:

- Path: `~/Projects/Vyb`
- Git branch: `main`
- Tracking branch: `origin/main`
- Remote: `git@github.com:rickenator/Vyb.git`
- Recent head at review time: `894d189 Merge pull request #107 from rickenator/codex/aspect-receiver-suite-cleanup`

Recent work visible in git and update logs includes:

- Aspect receiver cleanup and residual monomorphization/dispatch fixes
- Module registry, module search paths, stdlib discovery, visibility, and re-export work
- FFI support for `extern "C"`, freedom-gated calls, ABI aliases, `#[repr(C)]`, and native `--link`
- Error propagation phases for `fail`/`trap`
- Minimal `our<T>` / `mild<T>` control-block runtime
- Vec correctness fixes, including `Vec::pop()` returning values
- Stdlib scaffolding under `stdlib/core`, `stdlib/collections`, and `stdlib/io`

Primary status files:

- `doc/FEATURE_STATUS.md`
- `UPDATE_LOG.md`
- `CHANGELOG.md`
- `TODO.md`
- `reports/LANGUAGE_COMPLETION_REVIEW.md`
- `reports/TEST_EXAMPLE_DEMO_REVIEW.md`

## 2. Executive Summary

Vyb is a statically typed systems programming language implemented in C++17 on
top of LLVM 18. It aims to combine native-code performance with a compact,
name-first syntax and explicit ownership vocabulary.

The language's main identity markers are:

- Name-first declarations: `name(params)<ReturnType> ->`
- Variable and field declarations: `name<Type> = value`
- Ownership syntax: `my<T>`, `our<T>`, `their<T>`, `mild<T>`
- `freedom { ... }` blocks for low-level pointer control
- Aspect/bind polymorphism instead of class-first inheritance
- Monomorphized generics
- `match` and expression-level `select`
- `defer` for scope cleanup
- `fail`/`trap` typed error propagation
- `Vec<T>`, String methods, math intrinsics, and basic stdlib scaffolding
- JIT execution, AOT object emission, and native executable generation
- Local module imports, visibility directives, stdlib discovery, and FFI

In compiler terms, Vyb is already a substantial language implementation: it has
a frontend, semantic analysis, LLVM backend, runtime support, standard-library
scaffold, examples, demos, and hundreds of tests. In release terms, it should be
studied as an active language prototype moving toward a 1.0 contract, not as a
fully settled industrial toolchain.

## 3. Current Status Snapshot

The strongest current status source is `doc/FEATURE_STATUS.md`, marked v0.5.1
and last updated 2026-05-25. `CMakeLists.txt` declares project version 0.5.0.
`README.md` still describes v0.4.4 in places, so it remains useful for language
orientation but is not the single source of truth for implementation status.

Broadly implemented:

- Lexer, parser, AST, semantic analysis, LLVM codegen
- LLVM ORC JIT execution
- AOT object and executable generation
- `--build`, `--compile`, `--emit-llvm`, `--module-path`, and linker inputs
- Functions, structs, C-like enums, variables, arrays, tuples, type aliases
- Generics and monomorphization for supported shapes
- Aspect/bind polymorphism, receiver shorthand, associated type slice, and ambiguity diagnostics
- `match`, `select`, comparison patterns, break/continue, `defer`
- `Vec<T>` core methods and vector iteration
- String methods: `len`, `contains`, `starts_with`, `ends_with`, `to_upper`,
  `to_lower`, `substring`, `char_at`, `trim`, `replace`
- Print/println auto-formatting for many types
- String concatenation with primitive auto-coercion
- `typeof` / `typename`
- `freedom` blocks and raw pointer/location operations
- `extern "C"` FFI with freedom-gated direct calls
- `#[repr(C)]` struct validation for supported ABI-stable fields
- Local module resolution, `from` locators, search paths, stdlib discovery,
  bundle/share visibility, smuggle bypass, cycle detection, and re-exports
- Typed `fail`/`trap` propagation through current failable ABI paths
- Minimal `our<T>` / `mild<T>` runtime support

Partial or planned:

- Full ownership checker and complete move/copy/drop semantics
- Full `their<T>` lifetime and mutation-boundary semantics
- Complete weak-reference accounting and Option-like failed `grab()`
- Full generic `Option<T>` and `Result<T,E>`
- Tagged union enums and enum payload patterns
- Full closure struct codegen and capture semantics
- Real async scheduling and future value storage
- Full package manager, remote `smuggle`, lockfiles, formatter, linter, LSP
- Broader stdlib: files, process, networking, maps/sets, iterators
- Full reflection, downcasting, and struct metadata coverage

## 4. Repository Map

Top-level files and directories:

- `README.md`: broad language guide and quick start
- `CHANGELOG.md`: release history and unreleased changes
- `UPDATE_LOG.md`: source-biased implementation audit and progress log
- `TODO.md`: road-to-1.0 checklist
- `CMakeLists.txt`: build setup for LLVM/Catch2/Python integration
- `include/vyb/`: public compiler/runtime headers
- `src/parser/`: lexer, parser, tokens, and AST support
- `src/vre/`: semantic analysis, value model, intrinsics, LLVM backend
- `src/runtime/`: async and error runtime support
- `runtime/`: C runtime and type metadata implementation
- `stdlib/`: early Vyb standard library modules
- `examples/`: runnable examples
- `demos/`: curated demonstrations
- `test/`: feature-oriented test corpus
- `doc/`: design/status/reference documents
- `reports/`: audit and review reports

Inventory at review time:

- 736 `.vyb` files under `test/`
- 13 top-level `.vyb` examples under `examples/`
- 5 `.vyb` demos under `demos/`
- 12 `.vyb` stdlib files under `stdlib/`

## 5. Language Surface

### 5.1 Name-First Syntax

Vyb functions put the function name first, then parameters, then return type:

```vyb
add(a<Int>, b<Int>)<Int> -> {
    return a + b
}
```

`main` is ordinary:

```vyb
main()<Int> -> {
    return 42
}
```

Variable declarations use `name<Type>`:

```vyb
count<Int> = 0
message<String> = "hello"
numbers<Vec<Int>> = Vec::new()
```

Const bindings use `const`:

```vyb
const limit<Int> = 10
```

Study:

- `doc/Declaration_Syntax.md`
- `doc/Vyb_Function_Declaration_Syntax.md`
- `doc/Canonical_Reference_Syntax.md`
- `test/parser/`
- `test/new_features/`

### 5.2 Types

Common types in the current language:

- `Int`, `Int8`, `Int16`, `Int32`, `Int64`
- `UInt8`, `UInt16`, `UInt32`, `UInt64`
- `Float`, `Float32`, `Float64`
- `Bool`
- `String`
- `Char`, `Rune`, `Byte`
- arrays such as `[Int; 5]`
- tuples such as `Tuple<Int, String>` and multi-return lists
- `Vec<T>`
- `Future<T>` syntax/stubs
- ownership wrappers: `my<T>`, `our<T>`, `their<T>`, `mild<T>`
- raw location/pointer forms for `freedom` code

Study:

- `doc/AST_Types.md`
- `test/types/`
- `test/arrays/`
- `test/tuples/`
- `test/vectors/`
- `test/string/`

### 5.3 Ownership Vocabulary

Vyb's ownership model is a core language differentiator:

- `my<T>`: unique ownership
- `our<T>`: shared ownership
- `their<T>`: borrowed/reference access
- `mild<T>`: weak/non-owning reference that can observe release
- raw pointer/location types inside `freedom`

Borrowing and weak-reference operations include:

- `view(expr)`: immutable borrowed view
- `borrow(expr)`: mutable borrowed view
- `our(expr)`: create a shared owner
- `soft(ourValue)`: create a `mild<T>` weak handle
- `mildValue.released()`: check target release
- `mildValue.grab()`: upgrade a live weak handle, currently with a placeholder
  behavior for failed grabs until first-class Option/nullability is complete

Current status: lexical borrow enforcement and initial `our<T>`/`mild<T>`
control blocks exist, but full move/copy/drop checking is still future work.

Study:

- `doc/OWNERSHIP_MILD.md`
- `doc/Memory_Operations.md`
- `doc/mem_RFC.md`
- `test/ownership/`
- `test/memory/`
- `src/vre/llvm/cgen_ownership.cpp`

### 5.4 Freedom Blocks

`freedom` marks low-level regions where raw pointer/location operations are
allowed:

```vyb
freedom {
    p<loc<Int>> = loc(x)
    at(p) = 42
}
```

Direct calls to `extern "C"` functions are also freedom-gated. Treat
`freedom` as a review boundary: keep it small and explicit.

Study:

- `doc/Memory_Operations.md`
- `test/ffi/extern_c_requires_freedom.vyb`
- `test/new_features/test_freedom_block.vyb`
- `demos/ffi_freedom.vyb`

### 5.5 Structs And C-Like Enums

Struct declaration:

```vyb
struct Point {
    x<Int>
    y<Int>
}
```

Construction:

```vyb
p<Point> = Point { x = 10, y = 20 }
```

C-like enums are first-class typed values — `Direction::East` has type
`Direction`, `println` renders `East`'s value as `Direction::East`, and
`match`/`select` dispatch on named variants with the same exhaustiveness
requirement as data-carrying enums. Each value is backed by a single scalar
`i64` tag (not a struct), so an extern function parameter typed `Direction`
interoperates with a C integer-backed enum across the FFI boundary:

```vyb
enum Direction {
    North,
    South,
    East,
    West
}

d<Direction> = Direction::East
println(d)                 // Direction::East
println_int(labs(d))       // passes the scalar tag (2) into a C long
println_int(d.tag)         // .tag exposes the raw positional tag as an Int (East -> 2)
match (d) {
    North -> "north",
    East  -> "east",
    ?     -> "other"
}
```

Every enum value exposes its raw positional variant tag as an `Int` via `.tag` —
this also holds for data-carrying (tagged-union) enums, where field 0 of the
`{ i64 tag, [N x i8] data }` union is extracted at runtime
(`Shape::Circle(2.0).tag` is `0`, `Shape::Rect(1.0, 2.0).tag` is `1`).
Data-carrying enums — `enum Shape { Circle(Float), Rect(Float, Float) }` —
compile to a value-semantics `{ i64 tag, [N x i8] data }` union and
construct/match with payloads (`Shape::Circle(2.0)`, `Circle(r) ->`).

Study:

- `test/basic/simple_struct.vyb`
- `test/new_features/test_enum_basic.vyb`
- `test/new_features/test_enum_match.vyb`
- `src/vre/llvm/cgen_decl.cpp`

### 5.6 Aspects And Binds

Vyb's trait-like system uses `aspect` and `bind`.

An aspect declares behavior:

```vyb
aspect Display {
    show(self)<Void> -> { }
}
```

A bind implements behavior for a type:

```vyb
bind Display -> Point {
    show(self)<Void> -> {
        println("Point")
    }
}
```

Then dot calls become legal:

```vyb
p<Point> = Point { x = 1, y = 2 }
p.show()
```

Generic bounds use nested angle syntax:

```vyb
printItem<T<Display>>(item<T>)<Void> -> {
    item.show()
}
```

Important current implementation points:

- Canonical receiver shorthand `method(self)<T>` is supported.
- Legacy/explicit `self<Self>` and ownership-qualified receivers remain valid.
- Associated type declarations and bind assignments have an initial supported slice.
- Ambiguous aspect method dot-calls are diagnosed.
- Generic bind methods execute for current supported shapes.
- Dynamic aspect objects, inheritance, and some bounded bind selection questions remain future work.

Study:

- `doc/ASPECT_BOUNDS.md`
- `doc/TRAIT_SYSTEM_DESIGN.md`
- `doc/WHY_TRAITS_NOT_CLASSES.md`
- `test/aspect/`
- `demos/structs_aspects.vyb`
- `src/vre/llvm/cgen_trait_mono.cpp`
- `src/vre/llvm/cgen_function_mono.cpp`

### 5.7 Generics And Monomorphization

Vyb's generics are intended to be zero-cost through compile-time
monomorphization. Current support includes generic functions and supported
generic aspect/bind method shapes.

Conceptually:

```vyb
identity<T>(x<T>)<T> -> {
    return x
}
```

The compiler stores generic templates and instantiates concrete versions such
as `identity<Int>` or `identity<String>` on demand.

Study:

- `test/template/`
- `test/new_features/test_generic_identity.vyb`
- `src/vre/llvm/cgen_monomorph.cpp`
- `src/vre/llvm/cgen_function_mono.cpp`
- `include/vyb/vre/llvm/codegen.hpp`

### 5.8 Control Flow

Implemented control flow includes:

- `if` / `else`
- `while`
- C-style and range/vector `for`
- `break`
- `continue`
- `return`
- `defer`

Range loop:

```vyb
for (i in 0..10) {
    println(i)
}
```

Vec loop:

```vyb
for (item in numbers) {
    println(item)
}
```

Defer:

```vyb
defer cleanup()
```

Current tests cover LIFO defer behavior and fail/defer interaction.

Study:

- `demos/control_flow.vyb`
- `test/new_features/test_defer_basic.vyb`
- `test/new_features/test_defer_lifo.vyb`
- `test/vec_for/`
- `test/range_for/`

### 5.9 Match And Select

`match` is statement-oriented:

```vyb
match (x) {
    0 -> println("zero")
    ? -> println("other")
}
```

`select` is expression-oriented and uses `pass` to produce a value from a block:

```vyb
result<Int> = select(score) -> {
    >= 90 -> 4,
    >= 80 -> 3,
    ? -> 0
}
```

Open areas include more advanced destructuring, enum payload patterns, guards,
and exhaustiveness.

Study:

- `doc/MATCH_SYNTAX.md`
- `test/select_match/`
- `test/new_features/test_select_expr.vyb`
- `test/new_features/test_match_basic.vyb`

### 5.10 Strings

String is implemented as a fat pointer representation, conceptually:

```cpp
struct String {
    data: *i8
    len: i64
}
```

Supported methods include:

- `len()`
- `contains()`
- `starts_with()`
- `ends_with()`
- `to_upper()`
- `to_lower()`
- `substring()`
- `char_at()`
- `trim()`
- `replace()`

String concatenation supports String plus primitive auto-coercion in current
feature status.

Study:

- `doc/STRING_IMPLEMENTATION.md`
- `test/string/`
- `src/vre/llvm/cgen_string.cpp`
- `src/vre/llvm/cgen_string_type.cpp`

### 5.11 Vec<T>

`Vec<T>` is the primary dynamic array:

```vyb
numbers<Vec<Int>> = Vec::new()
numbers.push(10)
numbers.push(20)
value<Int> = numbers.pop()
length<Int> = numbers.len()
```

Current supported methods include:

- `Vec::new()`
- `push()`
- `pop()`
- `len()`
- `get()`
- `contains()`

Future work includes higher-order methods such as `map`, `filter`, and
`reduce`, which depend on lambda and iterator completion.

Study:

- `doc/VEC_ITERATION.md`
- `test/vectors/`
- `test/vec_for/`
- `demos/collections.vyb`
- `src/vre/llvm/cgen_vec.cpp`

### 5.12 Tuples And Multi-Value Returns

Vyb supports tuple types and multi-value returns:

```vyb
main()<Int, String> -> {
    return 42, "hello"
}
```

Tuple-related tests cover literal syntax, indexing, tuple return forms, and
mixed types. Remaining areas include richer serialization/output, destructuring,
and tuple pattern matching.

Study:

- `test/tuples/`
- `test/units/test_multi_value_return.vyb`
- `doc/Auto_Serialization_Main_Returns.md`

### 5.13 Serialization And Type Metadata

The runtime has type metadata and JSON conversion support. Current feature
status notes struct serialization/deserialization support, with future work for
nested structs, Vec metadata, and broader reflection.

Relevant concepts:

- automatic main return serialization
- `lit(...)`
- `notype(...)`
- `bare(...)`
- type metadata registry
- primitive string/from-string conversion helpers
- struct JSON round trips

Study:

- `doc/Auto_Serialization_Main_Returns.md`
- `doc/INTROSPECTION_DESIGN.md`
- `test/json/`
- `runtime/vyb_type_metadata.c`
- `src/vre/llvm/cgen_metadata.cpp`
- `src/vre/llvm/cgen_json.cpp`

### 5.14 Error Handling

Vyb's error model uses failures and traps instead of ordinary exceptions.

Failure:

```vyb
divide(a<Int>, b<Int>)<Int> -> {
    if (b == 0) {
        fail 42
    }
    return a / b
}
```

Trap:

```vyb
main()<Int> -> {
    result<Int> = {
        divide(10, 0)
    } trap (e<Int>) -> {
        return -1
    }
    return result
}
```

Current feature status says the system includes:

- typed `fail<T>(value)`
- typed traps
- wildcard and multi-type trap parsing
- dual-return ABI for failable functions
- propagated call-site checks
- untrapped runtime handler dispatch from failable `main`

Remaining work includes broader payload handling, standard error type polish,
and integration with generic `Result<T,E>`.

Study:

- `doc/ERROR_TRAP.md`
- `doc/ERROR_PROPAGATION_DESIGN.md`
- `test/trap/`
- `test/error_trap/`
- `src/runtime/error_handling.cpp`
- `src/vre/llvm/cgen_stmt.cpp`
- `src/vre/llvm/cgen_trap_prealloc.cpp`

### 5.15 Modules, Visibility, And Stdlib Discovery

Current module support includes:

- `import path`
- `import path as alias` parsing, with namespace binding still limited
- selective import specifiers and renaming
- `import path from "<locator>"`
- `smuggle path from "<locator>"`
- local path loading
- module search paths: importer directory, `--module-path`, `VYB_MODULE_PATH`,
  stdlib discovery
- `bundle(...)` visibility
- `share(...)` exports and re-exports
- smuggle visibility bypass
- cycle diagnostics

Remote URL/Git fetching remains planned.

Study:

- `doc/module_visibility.md`
- `doc/MODULE_FFI_BINARY_ROADMAP.md`
- `doc/stdlib_layout.md`
- `include/vyb/module_registry.hpp`
- `src/module_registry.cpp`
- `test/modules/`
- `examples/module_import.vyb`
- `examples/module_path_demo/`

### 5.16 FFI

Current FFI support includes:

- `extern "C"` blocks
- freedom-gated direct calls
- C ABI scalar/pointer aliases
- conservative `#[repr(C)]` struct validation
- by-pointer extern C calls
- host symbol lookup in JIT
- native build `--link <lib-or-path>` inputs

Open work includes variadics, richer C string handling, binding generation,
headers, platform libraries, and broader ABI coverage.

Study:

- `doc/FFI_DESIGN.md`
- `test/ffi/`
- `examples/ffi_puts.vyb`
- `demos/ffi_freedom.vyb`
- `src/vre/llvm/cgen_decl.cpp`
- `src/main.cpp`

### 5.17 Lambdas And Closures

Current status:

- lambda parsing exists
- semantic capture detection exists
- lambda body type inference exists
- indirect local variable calls have support
- closure structs and capture extraction are implemented: a `fn` is a uniform
  `{ env, fn }` closure value, captures are copied by value into a heap
  environment, and capturing closures work as higher-order-combinator arguments
  (`test/lambda/test_closure_capture.vyb`). Move / mutable / `our` capture remain
  planned.

Study:

- `doc/LAMBDAS.md`
- `test/lambda/`
- `test/future_features/test_lambda_codegen.vyb`
- `src/vre/llvm/cgen_expr.cpp`

### 5.18 Async

Current status:

- async/await syntax and runtime stubs exist
- real scheduling, future value storage, task spawning, and async I/O remain future work

Study:

- `doc/Async_Programming_Debug_System.md`
- `test/async/`
- `include/vyb/runtime/async_runtime.hpp`
- `src/runtime/async_runtime.cpp`
- `src/vre/llvm/cgen_async.cpp`
- `src/vre/llvm/cgen_async_impl.cpp`

## 6. Compiler Architecture

The compiler pipeline is:

```text
.vyb source
  -> Lexer
  -> Parser
  -> AST
  -> ModuleRegistry / import resolution
  -> SemanticAnalyzer
  -> LLVMCodegen
  -> LLVM IR verification and optimization
  -> ORC JIT execution or object-file emission
  -> optional native linker
```

### 6.1 Lexer And Tokens

Study files:

- `include/vyb/parser/token.hpp`
- `include/vyb/parser/lexer.hpp`
- `src/parser/token.cpp`
- `src/parser/lexer.cpp`

Tokens carry source locations, which are critical for diagnostics, stack
traces, parser errors, and semantic errors.

### 6.2 Parser

The parser is recursive descent and split into layered parser classes:

- `BaseParser`: token cursor, matching, location, common helpers
- `ExpressionParser`: expression precedence, calls, members, trap/ensure clauses
- `TypeParser`: type grammar and ownership wrappers
- `StatementParser`: blocks, control flow, fail/trap, return, defer
- `DeclarationParser`: functions, structs, aspects, binds, enums, imports, externs
- `ModuleParser`: top-level module assembly
- `Parser`: wiring layer

Study files:

- `include/vyb/parser/parser.hpp`
- `src/parser/base_parser.cpp`
- `src/parser/expression_parser.cpp`
- `src/parser/type_parser.cpp`
- `src/parser/statement_parser.cpp`
- `src/parser/declaration_parser.cpp`
- `src/parser/module_parser.cpp`

### 6.3 AST

The AST contains families for:

- literals
- expressions
- statements
- declarations
- type nodes
- ownership kinds
- fail/trap/ensure nodes
- aspect/bind nodes
- imports and externs

Study:

- `include/vyb/parser/ast.hpp`
- `src/parser/ast.cpp`
- `src/parser/ast_extra.cpp`
- `doc/AST_*.md`

### 6.4 Module Registry

The module registry resolves local imports and tracks dependency state before
semantic analysis/codegen.

Study:

- `include/vyb/module_registry.hpp`
- `src/module_registry.cpp`
- `test/modules/`

### 6.5 Semantic Analysis

The semantic layer manages:

- scopes and symbol tables
- function/type/variable declarations
- type parameters and bounds
- aspect and bind registries
- associated types
- import visibility and module-spliced declarations
- borrow checks
- failable function metadata
- extern/FFI validation
- type compatibility
- Vec method validation

Study:

- `include/vyb/semantic.hpp`
- `src/vre/semantic.cpp`

### 6.6 LLVM Codegen

`LLVMCodegen` is an AST visitor that owns:

- LLVM context, module, and IRBuilder
- primitive LLVM type handles
- current function/class/impl context
- named values and function-local allocas
- scope cleanup state
- loop and select context stacks
- error/trap/ensure state
- ownership/refcount state
- type aliases and user type maps
- generic template and monomorphization caches
- type metadata globals
- debug metadata support

Study:

- `include/vyb/vre/llvm/codegen.hpp`
- `src/vre/llvm/codegen.cpp`
- `src/vre/llvm/cgen_main.cpp`
- `src/vre/llvm/cgen_types.cpp`
- `src/vre/llvm/cgen_expr.cpp`
- `src/vre/llvm/cgen_stmt.cpp`
- `src/vre/llvm/cgen_decl.cpp`
- `src/vre/llvm/cgen_function.cpp`
- `src/vre/llvm/cgen_monomorph.cpp`
- `src/vre/llvm/cgen_function_mono.cpp`
- `src/vre/llvm/cgen_trait_mono.cpp`
- `src/vre/llvm/cgen_vec.cpp`
- `src/vre/llvm/cgen_string_type.cpp`
- `src/vre/llvm/cgen_ownership.cpp`
- `src/vre/llvm/cgen_stack_trace.cpp`

### 6.7 Runtime And Intrinsics

Runtime support bridges generated code to host functionality:

- printing
- strings and conversions
- JSON and type metadata
- memory helpers
- error handling and stack traces
- async stubs
- JIT symbol registration

Study:

- `src/vre/intrinsics.cpp`
- `runtime/vyb_runtime.c`
- `runtime/vyb_type_metadata.c`
- `src/runtime/error_handling.cpp`
- `src/runtime/async_runtime.cpp`
- `src/main.cpp`

## 7. Build, CLI, And Test System

### 7.1 Build

The project expects:

- CMake 3.15+
- C++17
- LLVM 18.1.3
- Catch2
- Python3

Typical build:

```bash
mkdir -p build
cd build
LLVM_DIR=/usr/lib/llvm-18/cmake cmake ..
cmake --build . -j2
```

### 7.2 CLI

Common commands:

```bash
build/vyb program.vyb
build/vyb program.vyb --parse-only
build/vyb program.vyb --semantic-only
build/vyb program.vyb --emit-llvm
build/vyb program.vyb --compile program.o
build/vyb program.vyb --build program
build/vyb program.vyb --build program --link m
build/vyb program.vyb --module-path examples/modules
```

Default behavior is JIT compile and execute.

### 7.3 Tests

The test corpus is feature-oriented. Important directories:

- `test/new_features/`: broad milestone-style language tests
- `test/basic/`: basics and simple regression tests
- `test/parser/`: parser coverage
- `test/types/`: primitive and type coverage
- `test/arrays/`, `test/tuples/`, `test/vectors/`, `test/vec_for/`
- `test/string/`
- `test/aspect/`
- `test/template/`
- `test/trap/` and `test/error_trap/`
- `test/modules/`
- `test/ffi/`
- `test/ownership/`
- `test/memory/`
- `test/introspection/`
- `test/lambda/`
- `test/async/`

Test directives commonly include:

```vyb
// @test: Name
// @description: What this checks
// @category: runtime, parser, semantic
// @expect: pass
// @expect-return: 42
// @expect-output: hello
// @expect-error: Type mismatch
```

Useful commands:

```bash
python3 test/run_tests.py --test-dir test/new_features --vyb build/vyb --execute-jit
python3 test/run_tests.py --test-dir test/ffi --vyb build/vyb --execute-jit
python3 test/run_tests.py --test-dir test/modules --vyb build/vyb --execute-jit
python3 test/run_milestone_tests.py --vyb build/vyb
python3 test_harness.py --vyb ./build/vyb --test-dirs test/new_features --workers 4
```

Reports from 2026-05-23 say `test/new_features`, `test/ffi`, and
`test/modules` were verified runtime suites during that pass, with full-suite
triage still an ongoing debt.

## 8. Practitioner Study Path

### Phase 1: Language Orientation

Read:

- `README.md`
- `doc/FEATURE_STATUS.md`
- `doc/README.md`
- `doc/Declaration_Syntax.md`
- `doc/Canonical_Reference_Syntax.md`
- `doc/Vyb_Function_Declaration_Syntax.md`

Exercises:

- Write five simple Vyb programs.
- Convert examples from older `fn` or colon syntax to name-first syntax.
- Identify three docs whose examples are current and three whose examples look historical.

### Phase 2: Frontend

Read:

- `include/vyb/parser/token.hpp`
- `src/parser/lexer.cpp`
- `include/vyb/parser/parser.hpp`
- `include/vyb/parser/ast.hpp`

Exercises:

- Tokenize `main()<Int> -> { return 42 }` by hand.
- Trace parser calls for a function declaration.
- Draw the AST for a function with one variable and one return.
- Add a parse-only test.

### Phase 3: Semantics

Read:

- `include/vyb/semantic.hpp`
- `src/vre/semantic.cpp`
- `test/aspect/`
- `test/modules/`
- `test/ownership/`

Exercises:

- Trace a symbol from declaration to lookup.
- Trace an aspect method from declaration to dot-call validation.
- Trace an import from source file to spliced declarations.
- Add one negative semantic test.

### Phase 4: LLVM Codegen

Read:

- `include/vyb/vre/llvm/codegen.hpp`
- `src/vre/llvm/cgen_main.cpp`
- `src/vre/llvm/cgen_types.cpp`
- `src/vre/llvm/cgen_expr.cpp`
- `src/vre/llvm/cgen_stmt.cpp`

Exercises:

- Emit LLVM IR for a simple `Int` program.
- Find where `Int` maps to LLVM.
- Trace a `return` statement.
- Compare `--emit-llvm` output for a simple program before and after adding a feature.

### Phase 5: Runtime

Read:

- `src/vre/intrinsics.cpp`
- `runtime/vyb_runtime.c`
- `runtime/vyb_type_metadata.c`
- `src/runtime/error_handling.cpp`
- `src/main.cpp`

Exercises:

- Trace `println`.
- Trace String concatenation.
- Trace `fail` to runtime error handling.
- Explain why JIT symbol registration is necessary.

### Phase 6: Advanced Research Topics

Choose one:

- Ownership and borrow enforcement
- Weak references and control blocks
- Generic monomorphization
- Aspect/bind dispatch
- Associated types
- FFI ABI validation
- Module visibility
- Error propagation ABI
- Closure capture representation
- Async runtime scheduling
- Type metadata and serialization

Deliverable:

- Write a short design note.
- Add or update a focused test.
- Document one gap between implementation and docs.

## 9. Practitioner Workflow

### 9.1 Checking Whether A Feature Works

1. Read `doc/FEATURE_STATUS.md`.
2. Search tests for the feature.
3. Read the relevant parser code.
4. Read semantic handling.
5. Read codegen.
6. Run a minimal test through:

```bash
build/vyb file.vyb --parse-only
build/vyb file.vyb --semantic-only
build/vyb file.vyb --emit-llvm
build/vyb file.vyb
```

### 9.2 Adding A Language Feature

Checklist:

1. Add or update tokens and lexer handling.
2. Add AST structure if needed.
3. Add parser support.
4. Add semantic validation.
5. Add codegen.
6. Add runtime/intrinsic support if generated code calls host functions.
7. Add tests:
   - parse positive
   - semantic positive
   - semantic negative
   - runtime positive
   - AOT/native if relevant
8. Update `doc/FEATURE_STATUS.md`.
9. Update this guide if the conceptual model changes.

### 9.3 Adding Runtime Support

Checklist:

1. Implement host function in `src/vre/intrinsics.cpp`, `src/runtime/`, or `runtime/`.
2. Add codegen declarations/helpers.
3. Generate calls from `cgen_*.cpp`.
4. Register JIT symbols in `src/main.cpp` if needed.
5. Ensure native builds link the implementation.
6. Add tests for JIT and, if relevant, `--build`.

### 9.4 Debugging Failures

Likely failure layers:

- Lexer: unknown token or wrong keyword classification
- Parser: grammar mismatch or legacy syntax
- Semantic: symbol lookup, type compatibility, bounds, borrow, module visibility
- LLVM verifier: wrong value/type pairing or malformed control flow
- JIT: missing runtime symbol, ABI mismatch, invalid function pointer cast
- Runtime: cleanup, ownership, FFI, string buffer, metadata, or error payload issue
- Native build: missing runtime object or linker input

## 10. Major Risks And Open Questions

### 10.1 Ownership Contract

Vyb's ownership vocabulary is rich, but the full contract is not finished. The
big question is how to make `my`, `our`, `their`, and `mild` enforceable across
assignments, function calls, returns, aggregate fields, and module boundaries.

### 10.2 Weak Reference Semantics

The minimal `our<T>` / `mild<T>` control-block runtime exists. Remaining design
work includes Option-like failed `grab()`, assignment/copy/drop accounting, and
final control-block cleanup.

### 10.3 Error ABI Generality

Current failable returns use specialized ABI forms such as `{T, i8*}`. The next
question is how general failable payloads, typed errors, AOT builds, and
standard `Result<T,E>` interoperate.

### 10.4 Module/Package Boundary

Local module resolution is real. Package metadata, remote `smuggle`, verified
dependencies, lockfiles, and project-mode builds are still future-facing.

### 10.5 FFI ABI Surface

Extern calls and `repr(C)` structs exist for supported cases. The high-risk
work is ABI correctness across platforms, variadics, strings, ownership fields,
headers, and linker/library management.

### 10.6 Aspect System Completion

The aspect system has strong momentum. Remaining questions include dynamic
dispatch, aspect inheritance, bounded bind precedence, method disambiguation
syntax, cross-module aspect coherence, and associated type generality.

### 10.7 Async Runtime

Async syntax and stubs are not enough for a full async story. Real scheduling,
future storage, wakeups, task spawning, and async I/O need a coherent runtime
model.

### 10.8 Documentation Truthfulness

The project has optimistic docs and source-biased audits. Keep broad
"complete" claims tied to feature matrices, tests, and current source behavior.

## 11. Key Documents To Keep In Sync

Primary:

- `doc/FEATURE_STATUS.md`
- this file: `doc/Vyb_Project_Study_Guide.md`
- `README.md`
- `TODO.md`
- `CHANGELOG.md`
- `UPDATE_LOG.md`

Architecture:

- `doc/VRE.md`
- `doc/RUNTIME.md`
- `doc/Development_Guide.md`
- `doc/Test_Harness_Guide.md`
- `doc/FFI_DESIGN.md`
- `doc/MODULE_FFI_BINARY_ROADMAP.md`

Language:

- `doc/Declaration_Syntax.md`
- `doc/Vyb_Function_Declaration_Syntax.md`
- `doc/Canonical_Reference_Syntax.md`
- `doc/Reference_Syntax_Unification.md`
- `doc/OWNERSHIP_MILD.md`
- `doc/Memory_Operations.md`
- `doc/ASPECT_BOUNDS.md`
- `doc/TRAIT_SYSTEM_DESIGN.md`
- `doc/VEC_ITERATION.md`
- `doc/STRING_IMPLEMENTATION.md`
- `doc/ERROR_TRAP.md`
- `doc/ERROR_PROPAGATION_DESIGN.md`
- `doc/Auto_Serialization_Main_Returns.md`
- `doc/INTROSPECTION_DESIGN.md`
- `doc/MATCH_SYNTAX.md`
- `doc/LAMBDAS.md`
- `doc/module_visibility.md`
- `doc/stdlib_layout.md`

AST:

- `doc/AST_Overview.md`
- `doc/AST_Core.md`
- `doc/AST_Declarations.md`
- `doc/AST_Expressions.md`
- `doc/AST_Statements.md`
- `doc/AST_Types.md`
- `doc/AST_Literals.md`
- `doc/AST_Patterns.md`
- `doc/AST_Roadmap.md`

Reports:

- `reports/LANGUAGE_COMPLETION_REVIEW.md`
- `reports/TEST_EXAMPLE_DEMO_REVIEW.md`

## 12. Update Protocol

Update this file when:

- a feature moves from parse-only to semantic support
- semantic support gains codegen
- JIT support differs from AOT/native support
- a test directory becomes a milestone gate
- runtime ABI behavior changes
- ownership/aspect/error/module semantics change
- docs disagree with current implementation
- public syntax changes
- new major standard-library modules appear

For each update, prefer concrete evidence:

- file path
- test name
- command used
- observed behavior
- limitation or follow-up

If a feature's status is unclear, mark it as "needs verification" rather than
calling it complete.

## 13. Minimal Glossary

Aspect:
An interface-like behavior declaration.

Bind:
An implementation that gives an aspect to a type.

Freedom:
A block where raw memory, pointer, and some FFI operations are allowed.

JIT:
Just-in-time execution through LLVM ORC JIT.

AOT:
Ahead-of-time compilation to object files or native executables.

Monomorphization:
Compile-time specialization of generic code for concrete type arguments.

Mild reference:
Vyb's weak-reference ownership form.

Trap:
A handler for a failing block.

Ensure:
Cleanup attached to success/failure block execution.

Select:
Expression-level pattern matching with `pass` for block arms.

ModuleRegistry:
Compiler subsystem that resolves and orders imported `.vyb` modules.

VRE:
Vyb Runtime Environment, used in docs for runtime and execution design.
