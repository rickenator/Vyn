# Vyb Language — Road to 1.0

> **What is Vyb?** A statically typed, systems programming language with an LLVM backend,
> ownership semantics expressed as readable keywords (`my`, `our`, `their`, `mild`),
> programmer-first memory control via `freedom` blocks, a struct + aspect model for
> polymorphism, and a uniquely clean name-first function syntax—no `fn` keyword noise.
> Vyb is not Rust. It is not C++. It is its own thing.

---

## Implementation Audit Tag

See `UPDATE_LOG.md` (`implementation-audit-2026-05-23`) for the current
source-biased implementation backlog, source TODO hotspots, and documentation
status conflicts. This roadmap remains the high-level 1.0 plan; the update log
is the working audit for what needs to be implemented next.

---

## Overall Completion Estimate

| Domain | Done | Remaining |
|--------|------|-----------|
| Core parsing & lexer | ~95% | Minor edge cases |
| LLVM backend (JIT + AOT + native) | ~85% | LTO, advanced passes |
| Control flow | ~92% | Pattern guards, exhaustiveness, labeled break |
| Type system (primitives + generics) | ~75% | Higher-kinded types |
| Struct system | ~85% | repr(C) for FFI |
| Ownership types (syntax + parsing) | ~80% | Semantic enforcement |
| Ownership types (runtime enforcement) | ~45% | Full move/copy/drop checking; a first-pass compile-time move/use-after-move checker for `my<T>` is in progress (uncommitted) |
| `mild<T>` weak references | ~60% | Minimal control blocks done; Option-like failed upgrade and full copy/drop semantics remain |
| Aspect/bind system | ~72% | Aspect objects/dyn dispatch, inheritance, qualified disambiguation |
| Generic monomorphization | ~85% | **SEALED**: Compile-time only. See doc/MONOMORPHIZATION_DESIGN.md |
| Async/await | ~80% | Real scheduler/executor |
| Error propagation (`fail`/`trap`) | ~80% | Standard error aspects, `rethrow`, ensure contracts |
| Lambda/closure codegen | ~50% | Full closure struct, captured-var codegen |
| Module system (`import`/`smuggle`/`bundle`) | ~70% | Local/module-path resolution, aliases, bundle/share visibility done; stdlib modules/package integration pending |
| FFI (`extern "C"`) | ~35% | Extern blocks, C aliases, freedom-gated JIT calls done; repr(C), variadics, linker flow pending |
| Standard library | ~45% | Vec, String, I/O, Math done; HashMap, File I/O needed |
| Introspection (`typeof`/`typename`) | ~75% | Downcasting, type assertions |
| Auto-serialization | ~80% | Edge cases remain |
| Pattern matching | ~60% | Destructuring, guards, enum variants |
| Package manager / `vyb.toml` | ~0% | Not started |
| Language server (LSP) | ~0% | Not started |
| REPL | ~0% | Not started |
| Self-hosting compiler | ~0% | Long-term goal |

**Overall: approximately 60-65% complete toward a production 1.0 release.**

### Design Decisions — SEALED

- **Monomorphization vs Polymorphism**: Vyb uses compile-time monomorphization for all generics. No vtables, no dynamic dispatch, no trait objects. Aspects + bind provide polymorphism via static dispatch. See `doc/MONOMORPHIZATION_DESIGN.md`.
- **Aspects over Classes**: Structs for data, aspects for behavior contracts, bind for implementation. No class inheritance. See `doc/TRAIT_SYSTEM_DESIGN.md` and `doc/WHY_TRAITS_NOT_CLASSES.md`.

### Recently Completed
- [x] **`defer` statement** — LIFO scope-exit deferred execution
- [x] **Math library** — `abs`, `min`, `max`, `sqrt`, `sin`, `cos`, `tan`, `exp`, `log`, `log2`, `log10`, `pow`, `floor`, `ceil`, `round`
- [x] **I/O intrinsics** — `print()` (no newline), `println_int()`, `print_int()`, `println_bool()`, `print_bool()`
- [x] **String methods** — `.len()`, `.contains()`, `.starts_with()`, `.ends_with()`, `.to_upper()`, `.to_lower()`, `.substring()`, `.char_at()`, `String::from_bytes()`
- [x] **Type inference from initializer** — Variables without annotation infer type from RHS
- [x] **Vec `for` loop type inference** — Compiler-generated loop variables no longer require explicit types
- [x] **`Vec::contains()`** — Fixed: was returning hardcoded `false`; now emits correct LLVM loop with element comparison
- [x] **Lambda indirect calls** — Lambdas stored in local variables can now be invoked; `localLambdaTypes` map tracks inferred function types for correct indirect call codegen
- [x] **`println()`/`print()` with multiple arguments** — Space-separated output; all args formatted into a single call
- [x] **Semantic type recognition** — `Int16`, `Int32`, `Int64`, `UInt8`–`UInt64`, `Float32`, `Float64`, `Char`, `Rune` now fully recognized in semantic analysis (were silently rejected)
- [x] **Relaxed struct field syntax** — C-style `Type fieldName` accepted alongside canonical `fieldName<Type>`; helps parse legacy/interop fixtures
- [x] **Test harness** — `--parse-only` flag forwarded to binary for `@parse-only: true` tests; `n/a` annotation values treated as "skip this check"; test count now **657 tests, 315 passing (47.9%)**
- [x] **Vec parameter deep copy** — Vec parameters receive an independent copy of the data on function entry, eliminating double-free bugs (e.g. recursive quicksort base-case return)
- [x] **Vec mutation through borrowed struct fields** — `s.items.push(val)` where `s<their<T>>` now correctly mutates in-place; member-expression Vec calls now get a field *pointer* (not a loaded copy)
- [x] **Semantic use-after-free fix** — `handleVecMethodCallOnMember` no longer stores raw pointers from temporary `VecType` objects into `expressionTypes`; all return types are cloned into `node->type` first
- [x] **`test/new_features` 100% pass** — All new-features tests pass (quicksort, stack, insertion sort, etc.)
- [x] **C-like enum codegen** — `enum Color { Red, Green, Blue }` compiles; variants become sequential `i64` constants (0, 1, 2); `Color::Red` resolves correctly in all expression contexts including `match`
- [x] **Silence optimization pass messages** — `"Applying IR optimization passes"` / `"Skipping IR optimization"` now gated behind `--debug-codegen`; compiler is quiet in normal use

---

## What Is Done Today

### Core Language
- [x] **Lexer/tokenizer** — Complete with all documented token types; `freedom` keyword works
- [x] **Name-first function syntax** — `name(params)<ReturnType> -> body`, no `fn` noise
- [x] **Multi-value returns** — `main()<Int,String> -> return 42, "hello"`
- [x] **Variable declarations** — Unified `name<Type> = value` with type inference
- [x] **Struct declarations** — `struct Point { x<Int>, y<Int> }` with generic params `struct Box<T>`
- [x] **Struct construction** — `Point{ x: 1, y: 2 }`
- [x] **All primitive types** — `Int`, `Int8/16/32/64`, `UInt8/16/32/64`, `Float32/64`, `Bool`, `Char`, `Rune`, `String`
- [x] **Type inference** — Local variable types inferred from initializer
- [x] **`const` bindings** — Immutable bindings via `const`

### Control Flow
- [x] **`if`/`else`** — With expressions and blocks
- [x] **`while` loops** — Standard while loops
- [x] **C-style `for` loops** — `for (i = 0; i < n; i = i + 1)`
- [x] **Range-based `for` loops** — `for (i in 0..10)` (inclusive)
- [x] **`for (item in vec)` iteration** — Vec<T> iteration with break/continue
- [x] **`break`/`continue`** — In all loop constructs
- [x] **`match` statements** — `match (expr) { pattern -> result, ? -> default }`
  - `->` arrow (consistent with function syntax, not `=>` which is Rust)
  - `?` wildcard (not `_` — more visible and intentional)
  - Comparison patterns (`>= 90`, `< 0`, etc.)
  - Literal patterns (int, float, string)
- [x] **`select` expressions** — Pattern matching that yields a value, with `pass` for
  explicit multi-statement returns (Vyb-original concept, no equivalent in other languages)

### Type System
- [x] **Generic types** — `<T>`, `<K, V>` with proper scoping and substitution
- [x] **Generic structs** — `struct Box<T> { value<T> }`
- [x] **Ownership type syntax** — `my<T>`, `our<T>`, `their<T>`, `mild<T>` parse correctly
- [x] **`Vec<T>`** — Dynamic array with `new()`, `push()`, `pop()`, `len()`, `get()`
- [x] **Fixed arrays** — `[T; N]` with indexing
- [x] **Tuples** — `Tuple<T,U,V>` and `(T,U,V)` syntax
- [x] **`Future<T>`** — Async return types with type checking

### Expressions & Operations
- [x] **Binary operations** — `+`, `-`, `*`, `/`, `==`, `!=`, `<`, `<=`, `>`, `>=`, `&&`, `||`
- [x] **Unary operations** — `!`, `-`
- [x] **Member access** — `obj.field`, `arr[index]`
- [x] **String concatenation** — `str1 + str2` with mixed-type auto-`toString`
- [x] **`toString` intrinsics** — All primitive types
- [x] **`typeof(expr)`** — Runtime type hash (8-byte i64) — uniquely Vyb introspection
- [x] **`typename(expr)`** — Type name as `String`

### Memory & Ownership
- [x] **`freedom` blocks** — `freedom { ... }` for programmer-controlled sections (not `unsafe`)
- [x] **`loc<T>` raw pointers** — Scoped to `freedom` blocks only
- [x] **`view(expr)`/`borrow(expr)`** — Canonical call syntax with lexical borrow validation
- [x] **`soft(expr)`** — Creates `mild<T>` from `our<T>` and attaches to the shared control block

### Aspect/Bind System (Vyb's Polymorphism)
- [x] **`aspect` declarations** — Method signatures, optional default implementations
- [x] **Receiver shorthand** — `method(self)<T>` is canonical sugar for simple `self<Self>` aspect/bind receivers
- [x] **`bind Aspect -> Type { ... }`** — Unbounded bind for concrete types
- [x] **`bind<T> Aspect -> Type<T> { ... }`** — Generic unbounded bind
- [x] **`bind<T<Aspect1, Aspect2>> Aspect -> Type<T> { ... }`** — Bounded bind
- [x] **Aspect bounds in generic functions** — `fn<T<Display>>(item<T>)<Void> ->`
- [x] **Semantic validation** — Bounds checked against aspect registry
- [x] **Monomorphization Phase 5** — Generic function specialization by concrete type

### Async/Await
- [x] **`async` functions** — `async name()<Future<T>> -> { ... }`
- [x] **`await` expressions** — `value<T> = await future_val`
- [x] **`Future<T>` type system** — Proper type checking throughout
- [x] **State machine codegen** — Async functions lowered to LLVM state machines
- [x] **DWARF debug metadata** — Suspension points, continuation markers

### Error Handling
- [x] **`fail` statement** — `fail<ErrorType>(value)` raises typed errors
- [x] **`trap` handler Phase 1** — `{ ... } trap (e<ErrorType>) -> { ... }`
- [x] **Error type system** — Type-tagged error objects with type hashes
- [x] **Failable function detection** — Semantic analysis marks `canFail` functions

### LLVM Backend
- [x] **JIT execution** — LLVM ORC JIT (migrated from MCJIT in v0.4.0)
- [x] **AOT object file emission** — `--compile` / `-c` flag
- [x] **Native executable generation** — `--build` / `-b` flag (v0.4.4)
- [x] **Optimization levels** — `-O0` through `-O3`
- [x] **Cross-compilation** — 20+ target architectures via LLVM
- [x] **Static linking** — `--static` flag
- [x] **DWARF debug info** — Source-level debugging in compiled output

### Auto-Serialization
- [x] **`main()` return serialization** — Complex types auto-output as JSON
- [x] **`lit()`, `notype()`, `bare()`, `deserial()` intrinsics** — Serialization control
- [x] **JSON construction intrinsics** — `__vyb_serialize_to_json()`, struct metadata
- [x] **JSON deserialization** — `T::from_string(json)` round-trip (v0.4.4)

### Infrastructure
- [x] **CMake build system** — LLVM integration
- [x] **Test harness** — 657+ tests, parallel execution, HTML/JSON reports; `--parse-only` mode; 315 passing (47.9%)
- [x] **`println()`** — Works with all data types including Vec<T>

---

## In Progress

### Error Propagation — Phases 2-5 (HIGH PRIORITY)
- [x] Phase 1: Semantic detection of failable functions (`canFail`)
- [x] Phase 2: Dual return value codegen `{ T, ptr }` for failable functions
- [x] Phase 3: `fail` statement returns error to caller when no trap in scope (`test/trap/propagation_no_trap.vyb`, `test/trap/defer_runs_on_fail.vyb`)
- [x] Phase 4: Call site instrumentation — auto-check `{ value, error }` tuple (`test/trap/propagation_no_trap.vyb`, `test/trap/non_failable_caller_rejected.vyb`)
- [x] Phase 5: Top-level untrapped error handler (`__vyb_runtime_untrapped_error`) (`test/trap/propagation_to_main.vyb`)

### Aspect System — Completion (HIGH PRIORITY)
- [x] Phases 1-4: Declarations, method calls, generic impls, type param substitution
- [x] **Associated types (first practical slice)** — `aspect Iterator { type Item }` declarations, `bind` assignments (`type Item = Int`), and semantic validation for missing/unknown/duplicate assignments are implemented. Remaining work: defaults, where-style constraints, dyn-dispatch integration.
- [ ] **Aspect objects / dynamic dispatch** — `dyn Aspect` for runtime polymorphism
- [x] **Aspect inheritance** — `aspect Comparable : Equatable` super-aspects: declared super-aspects are validated against defined aspects, cyclic dependencies are rejected, and binding a sub-aspect requires the same type to also bind each super-aspect (order-independent).
- [x] **Monomorphization with bounds validation** — Generic function calls infer type arguments from the call site, substitute them into the return type, and reject concrete instantiations whose type does not bind the declared aspect(s).
- [x] **Bind selection precedence** — A bounded generic bind (`bind<T<Aspect>>`) deterministically takes precedence over an unbounded bind (`bind<T>`) for the same type shape, independent of declaration order.

### Pattern Matching — Completion (MEDIUM PRIORITY)
- [x] Literal patterns, wildcard `?`, comparison operators
- [ ] **Struct destructuring** — `Point { x, y } ->` in match arms
- [ ] **Enum/sum type variant patterns** — `Some(value) ->`, `None ->`
- [ ] **Range patterns** — `1..10 ->` in match arms
- [ ] **Guard clauses** — `pattern if condition ->`
- [ ] **Exhaustiveness checking** — Compiler rejects non-exhaustive match
- [ ] **`match` as expression** — Return a value from match directly

### Lambda / Closures (MEDIUM PRIORITY)
- [x] Parsing — `|x, y| -> x + y` and `|x<Int>| -> { ... }`
- [x] Semantic analysis — capture detection, type inference
- [x] **Indirect call codegen** — Lambda stored in a local variable can be called; `localLambdaTypes` map tracks inferred function types; body return value coerced to declared return type
- [ ] **Full LLVM closure struct codegen** — Generate closure structs with capture fields, function pointers
- [ ] **Move capture** — Transfer ownership of `my<T>` into closure
- [ ] **Mutable capture** — Captured variables in mutable context
- [ ] **`our<T>` capture** — Atomic ref-count increment for shared captures

---

## Planned — Needed for 1.0

### 1. Module System (HIGH PRIORITY)
Vyb's `import`/`smuggle`/`bundle`/`share` system is a unique approach to module visibility.
See `doc/bundles_and_sharing.md` and `doc/MODULE_FFI_BINARY_ROADMAP.md`.

- [x] **Phase 1.1 — Import Parsing** — `import <path>`, `import <path> as <alias>`, `import <path> from "<locator>"`, `smuggle <path> as <alias>`, `ImportKind` (TrustedImport / Smuggle) captured in AST
- [x] **Phase 1.1 — Local Module Resolution** — local `import nested::module`, `import name from "./file.vyb"`, recursive loading, import deduping, and circular dependency detection before semantic analysis/codegen
- [x] **Phase 1.1b — Alias/specifier imports and re-export semantics** — `import module::{symbol as alias}`, explicit `share(...) import ...` re-exports
- [x] **Phase 1.2 — `bundle(...)` Directives** — Source-level module bundle metadata accepted by local resolver
- [x] **Phase 1.3 — `share(...)` Directives** — `share(all)` and `share(bundle1, bundle2)` accepted on declarations and imports
- [x] **Phase 1.4 — Visibility Checking** — Bundle overlap enforced during import resolution; `smuggle` bypasses visibility
- [x] **Phase 1.4b — Formal ModuleRegistry model** — source-level resolver moved into `ModuleRegistry` metadata/cache API with canonical keys, resolution states, topo order, and dependency diagnostics
- [x] **Phase 1.5 — Module Path Resolution**
  - `VYB_MODULE_PATH` environment variable
  - `--module-path` CLI flag
  - Standard library auto-discovery
- [ ] **Phase 1.6 — Standard Library as Modules**
  - [x] Foundation scaffold landed: `stdlib/core/`, `stdlib/io/`, `stdlib/collections/`, top-level/core preludes, transitional `core::option` bridge, and placeholder `core::result`
  - [ ] Expand with full module contents (`math`, collections, io, iterator/core aspects)
  - Auto-import of `core::*` (opt-out with directive)

### 2. FFI — C Interop (HIGH PRIORITY)
- [x] **`extern` function modifier** — Individual extern function declarations compile to LLVM `ExternalLinkage` via `ExternStatement` codegen; syntax: `extern funcName(params)<ReturnType>`
- [x] **`extern "C" { }` block syntax** — Multi-declaration blocks parse, register external functions, and codegen LLVM declarations
- [x] **C type mapping** — Common C aliases (`CInt`, `CSize`, `CString`, `CPtr<T>`, `CVoid`, etc.) lower through semantic/codegen
- [x] **`#[repr(C)]` on structs** — Attribute parses on structs, preserves declaration-order unpacked LLVM layout, and rejects generic/Vyb-runtime fields that are not C ABI-stable
- [x] **Native `--link <lib-or-path>` flow** — Repeatable build flag passes `-l<lib>` or explicit library/object paths to the native linker
- [ ] **Variadic C functions** — `printf(format: *i8, ...) -> Int`
- [ ] **`vyb bindgen`** — Tool to generate Vyb bindings from C headers (v0.6+)

### 3. Ownership Types — Runtime Enforcement (HIGH PRIORITY)
- [ ] **`my<T>` move semantics** — Enforce single-owner at compile time; error on copy. *Status: first-pass compile-time move tracking is in progress (working tree, uncommitted, tests in `test/ownership/move_*.vyb` incomplete).*
- [ ] **`our<T>` reference counting** — Minimal control-block strong_count exists for `our()`/`grab()`/scope cleanup; full copy/assignment/parameter semantics remain
- [x] **`their<T>` borrow checker (lexical phase)** — borrow/view require lvalues, reject overlapping mutable/view borrows, reject assignment while borrowed
- [x] **`mild<T>` control block (minimal runtime)** — `soft()` increments weak_count, `released()` observes release after strong owner scope exit, and live `grab()` upgrades by incrementing strong_count
- [x] **`view(expr)` semantic (lexical phase)** — Creates `their<T>` view and participates in borrow conflict checks
- [x] **`borrow(expr)` semantic (lexical phase)** — Creates mutable `their<T>` and participates in borrow conflict checks
- [x] **`soft(expr)` semantic** — Creates `mild<T>` from `our<T>`; enforced

### 4. Standard Library Expansion (HIGH PRIORITY)
- [ ] **`Option<T>`** — `Some(value)` / `None` for nullable values (transitional `core::option::OptionInt` bridge exists)
- [ ] **`Result<T, E>`** — `Ok(value)` / `Err(error)` for fallible operations (`core::result` placeholder module exists)
- [ ] **Core aspects** — `Display`, `Debug`, `Clone`, `Equatable`, `Comparable`, `Hashable`
- [x] **String methods** — `.len()`, `.contains()`, `.starts_with()`, `.ends_with()`, `.to_upper()`, `.to_lower()`, `.substring()`, `.char_at()`, `.trim()`, `.replace()`, `String::from_bytes()`
- [ ] **String methods (remaining)** — `.split()`, `.format()`
- [ ] **String formatting** — Format strings or `fmt()` intrinsic
- [ ] **`HashMap<K, V>`** — Hash map with `Hashable + Equatable` bounds
- [ ] **`HashSet<T>`** — Hash set
- [ ] **`BTreeMap<K, V>`** — Ordered map with `Comparable` bounds
- [ ] **File I/O** — `File::open()`, `File::read()`, `File::write()`, `File::close()`
- [x] **Math library** — `sqrt`, `sin`, `cos`, `tan`, `exp`, `log`, `log2`, `log10`, `pow`, `floor`, `ceil`, `round`, `abs`, `min`, `max`
- [x] **I/O intrinsics** — `print()` (no newline), `println_int()`, `print_int()`, `println_bool()`, `print_bool()`
- [ ] **Iterator aspect** — `next(self)<Option<Item>>` protocol for `for` loop integration
- [ ] **`Vec<T>` expansion** — `.map()`, `.filter()`, `.reduce()`, `.find()`, `.sort()`; `.contains()` is now correctly implemented

### 5. Sum Types / Enums (MEDIUM PRIORITY)
Vyb needs a way to express sum types. Essential for `Option<T>`, `Result<T,E>`, and
expressive APIs. **Vyb-natural approach:** enums should integrate with the aspect system
and pattern matching, not be a separate OOP mechanism.

- [x] **Enum declaration syntax** — `enum Direction { North, South, East, West }` — variants compile to sequential `i64` integer constants (0, 1, 2, …); access via `Direction::North`
- [ ] **Enum variants with data** — `enum Shape { Circle(Float), Rect(Float, Float) }` (tagged unions; future)
- [ ] **Pattern matching on enums** — In `match`, `select`, and destructuring (integer match works today)
- [ ] **Enum methods via `bind`** — `bind Drawable -> Shape { ... }` (natural fit!)
- [ ] **`Option<T>` as built-in enum** — `Some(T)` / `None`
- [ ] **`Result<T, E>` as built-in enum** — `Ok(T)` / `Err(E)`

### 6. Introspection System — Completion (MEDIUM PRIORITY)
- [x] `typeof(expr)` — Returns type hash as i64
- [x] `typename(expr)` — Returns type name as String
- [ ] **`as` downcasting operator** — `value as TargetType` (Phase 2)
- [ ] **`typeof` in wildcard trap** — `trap (e<?>) -> { if typeof(e) == typeof<ParseError>() }`
- [ ] **Type registry at startup** — `__vyb_module_init()` registers all types
- [ ] **`Type` as first-class type** — `t<Type> = typeof(42)`, equality comparison

### 7. Select Expressions — Polish (MEDIUM PRIORITY)
The `select` expression is a uniquely Vyb concept: pattern matching that produces a value,
with `pass` for multi-statement case bodies. Needs polishing:

- [ ] **`select` exhaustiveness** — Warn when no `?` wildcard and possible no-match
- [ ] **Nested `select`** — `select` inside a `select` arm
- [ ] **`select` with enum variants** — Full destructuring in arms
- [ ] **`select` as statement** — Allow `select` without a binding target (side-effects only)

### 8. Wildcard Trap Handler (MEDIUM PRIORITY)
- [ ] **`trap (e<?>)` syntax** — Catch any error type
- [ ] **`typeof(e)` in wildcard handler** — Runtime type discrimination
- [ ] **Multi-type trap** — `trap (e<ParseError | IOError>) -> { ... }` (Vyb-native syntax)

### 9. Advanced Control Flow (LOWER PRIORITY)
- [x] **`defer` statement** — `defer cleanup()` runs on scope exit (LIFO order, function-level)
- [ ] **`ensure` statement** — `ensure condition else fail<Error>(...)` (post-condition)
- [ ] **Labeled `break`/`continue`** — Break from outer loops by label

### 10. Async System — Completion (LOWER PRIORITY)
- [ ] **Real event loop / executor** — Single-threaded and multi-threaded runtimes
- [ ] **`spawn` for concurrent tasks** — `task<Future<T>> = spawn compute()`
- [ ] **Typed channels** — `chan<T>` for message passing between tasks (planned)
- [ ] **Actors** — Lightweight isolated concurrency units (planned)
- [ ] **`select` over channels** — Wait on multiple channels (Vyb-natural extension)
- [ ] **Async lambdas** — `async |x| -> await process(x)`
- [ ] **`async for`** — Iterate over async streams

---

## Developer Experience — Needed for 1.0

### Package Manager
- [ ] **`vyb.toml`** — Project manifest with `[package]`, `[dependencies]`, `[[bin]]`
- [ ] **`vyb build`** — Build multi-file projects from manifest
- [ ] **Dependency resolution** — Version constraints and lock file (`vyb.lock`)
- [ ] **Package registry** — Central registry for published packages
- [ ] **`vyb new`** — Scaffold a new Vyb project

### Language Server Protocol (LSP)
- [ ] **Go-to-definition** — Jump to symbol definitions across files
- [ ] **Hover documentation** — Show type signatures and doc comments
- [ ] **Completion** — Aspect method names, struct fields, imports
- [ ] **Diagnostics** — Real-time error reporting in editors
- [ ] **`vyb lsp`** — Launch LSP server mode

### REPL
- [ ] **Interactive mode** — `vyb repl` launches a read-eval-print loop
- [ ] **JIT-backed** — Reuse existing ORC JIT infrastructure
- [ ] **History + multiline** — Standard readline-style editing
- [ ] **`:type` command** — Print the type of an expression

### Documentation Tools
- [ ] **Doc comments** — `/// comment` on declarations
- [ ] **`vyb doc`** — Generate HTML documentation from source
- [ ] **Online reference** — Language reference manual (derived from existing docs)

### Testing & Tooling
- [ ] **`vyb test`** — Run test files alongside source (`*.test.vyb`)
- [ ] **Code formatter** — `vyb fmt` for canonical formatting
- [ ] **Linter** — `vyb check` for warnings beyond errors
- [ ] **Debugger integration** — `gdb`/`lldb` with Vyb source stepping (DWARF done, validate end-to-end)

### Polish — Silent by Default (HIGH PRIORITY)
The compiler must be silent in normal use. DEBUG output makes the language feel unfinished.

- [x] **Silence codegen DEBUG output** — All `std::cout << "DEBUG: ..."` and `std::cerr << "DEBUG: ..."` in `src/vre/` and `src/vre/llvm/` (~320 statements) are now gated behind `g_debug_codegen` (default `false`) via the `VYB_CDBG` macro. Enable with `--debug-codegen` CLI flag.
- [x] **Silence parser trace output** — Parser `[PEEK]`/`[CONSUME]`/`[EXPECT]` traces gated behind `#ifdef VERBOSE` and `VERBOSE` no longer defined globally in `CMakeLists.txt`; off by default. Re-enable with `-DVERBOSE` in the build.
- [x] **Silence optimization pass messages** — `"Skipping IR optimization"` / `"Applying IR optimization passes"` now gated behind `--debug-codegen` (same flag as all other debug output).
- [ ] **Doc consolidation** — `doc/` has overlapping files (`ROADMAP.md`, `TODO_CURRENT.md`, multiple ownership docs). Keep `TODO.md`, `doc/FEATURE_STATUS.md`, `CHANGELOG.md` as living docs; archive or delete the rest into `doc/archive/`; update `doc/README.md` as index.
- [ ] **Canonical syntax audit** — Audit all `*.md` files for outdated syntax (`unsafe` → `freedom`, old `:` field syntax → `<Type>`, `T: Aspect` → `<T<Aspect>>`).

---

## Architecture & Technical Debt

These are architectural improvements that will pay dividends as the codebase grows toward 1.0.

### A. Separate Semantic Analysis from AST Mutation
The semantic analyzer currently mutates AST nodes directly (sets `node->type`,
`expressionTypes[node]`, etc.), creating fragile cross-pass dependencies.

- [ ] Use an immutable AST + a separate `TypeTable` (map from node ID → type)
- [ ] Avoid raw pointer storage in `expressionTypes` (use stable IDs or `shared_ptr`)

### B. IR Optimization as a Separate Phase
IR optimization is currently applied inline during JIT setup.

- [ ] Extract into an explicit `optimize(module)` step controllable per compilation target
- [ ] Expose `-O0`–`-O3` flags consistently for both JIT and AOT paths

### C. Error Recovery in Parsing
The parser throws on the first error. Adding error recovery would allow reporting
multiple errors per file — a significant developer experience improvement.

- [ ] Synchronize to the next statement/declaration boundary on parse error
- [ ] Collect and report all errors before aborting

---

## Resolved Design Decisions

These items had conflicting designs and have been resolved. They are documented here for
reference so that contributors do not re-open them.

### [DECIDED] Class System vs. Struct + Aspect

**Decision:** Vyb has no class system. Struct + aspect composition is the only model.

Classes are an anti-pattern in Vyb's design. Inheritance-like patterns are achieved via
aspect composition + `bind`. The `class` keyword is not planned, not accepted as a
proposal, and should not be re-proposed. `doc/TRAIT_SYSTEM_DESIGN.md` Phase 5 (class
system) has been removed. All references to an optional class system have been excised
from the roadmap.

### [DECIDED] `trait`/`impl` vs. `aspect`/`bind` Terminology

**Decision:** Vyb uses `aspect`/`bind`. `trait`/`impl` is Rust vocabulary.

`aspect` captures that it adds a dimension of behavior. `bind` clearly expresses that you
are attaching that aspect to a type. All documentation uses `aspect`/`bind`. The `impl`
keyword may be accepted as an alias for backward compatibility only; `bind` is the
idiomatic Vyb path. All `trait`/`impl` examples in documentation have been updated to
`aspect`/`bind`. `doc/TRAIT_SYSTEM_DESIGN.md` and `doc/WHY_TRAITS_NOT_CLASSES.md` have
been updated accordingly.

### [DECIDED] `fn` Keyword Backward Compatibility

**Decision:** `fn` syntax is deprecated as of v0.5 and will be removed in v1.0.

The name-first syntax `name(params)<ReturnType> ->` is cleaner and uniquely Vyb. One
syntax is better than two. The `fn` keyword is legacy. New code must use name-first
syntax. The README note about `fn` support is historical; it will not persist to 1.0.

### [DECIDED] `try`/`catch`/`finally` vs. `fail`/`trap`

**Decision:** Vyb uses `fail`/`trap`. There is no `try`, `catch`, `finally`, or `throw`.

`TryStatement` and `ThrowStatement` have been removed from `doc/AST_Roadmap.md`. These
are vestigial C++ vocabulary and have no place in Vyb. `fail`/`trap` provides zero-cost
success path, typed errors, and explicit propagation — the Vyb way. `doc/AST_Roadmap.md`
has been updated to reflect this.

### [DECIDED] Generic Bound Syntax

**Decision:** `<T<Aspect>>` only. `T: Aspect` is Rust syntax and is not Vyb.

All documentation now uses `<T<Aspect>>` exclusively. This is consistent with Vyb's
unified `name<Type>` syntax. All `<T: Trait>` examples have been removed from docs.

### [DECIDED] Iterator Protocol: Aspect-Based

**Decision:** `Iterator` is a standard library aspect. `for` loops desugar to aspect calls.

The `Iterator` aspect is defined in the standard library:
```vyb
aspect Iterator {
    type Item                                    # associated type — what the iterator yields
    next(self<their<Self>>)<Option<Self::Item>>  # returns next value or None
}
```
Types that `bind Iterator` become usable in `for` loops. The compiler desugars
`for (item in col)` to repeated `Iterator::next()` calls. Depends on associated types
being implemented in the aspect system first (tracked separately).

### [DECIDED] Channels / Actors — Deferred to Post-1.0

**Decision:** Channels and actors are not part of the 1.0 release.

A solid 1.0 with `async`/`await` + structured concurrency is more valuable than an
under-designed actor model. Channels and actors require a full design document before
implementation. The README has been updated to remove them from the v1.0 scope.

---

## Vyb-Native Ideas Worth Exploring

These are not in any current design document but feel natural given Vyb's identity and
should be prototyped or at least documented before 1.0:

### `pipe` Operator (`|>`)
Functional pipelines without deep nesting:
```vyb
result<String> = data
    |> filter(|x| -> x > 0)
    |> map(|x| -> x * 2)
    |> to_string()
```
The `|>` pipe operator threads the left-hand value as the first argument to the right-hand
function. Fits Vyb's clean aesthetic; makes functional chains readable without a method
chain API requirement.

### `ensure` Contracts
Unlike bare `assert` (C-style), `ensure` integrates with the `fail`/`trap` system:
```vyb
divide(a<Int>, b<Int>)<Int> -> {
    ensure b != 0 else fail<DivisionError>(DivisionError { dividend: a })
    return a / b
}
```
More expressive than `if (condition) { fail ... }` and reads like a contract. Does not
clash with the error handling design — it IS the error handling design.

### `with` Scope Blocks (Resource Management)
A managed scope that calls cleanup when exiting — better than bare `defer` for resources:
```vyb
with file<File> = File::open("data.txt") {
    content<String> = file.read_all()
    println(content)
}  # file.drop() called automatically
```
Implemented by the compiler calling a `Drop` aspect method on scope exit. Does not need GC.
Aligns with `mild<T>` and `our<T>` lifecycle semantics.

### Named Arguments at Call Sites
Vyb's `name<Type>` syntax already names parameters. Callers should be allowed to use names:
```vyb
result<Int> = add(x: 10, y: 20)   # Named args, order-independent
```
Improves readability for functions with many parameters without requiring overloaded forms.

### `select` Over Error Results
Extend `select` to dispatch on `Result<T, E>`:
```vyb
result<Int> = select(risky_operation()) -> {
    ok(value)        -> value * 2,
    err(e<ParseError>) -> -1,
    err(e<IOError>)    -> -2,
    ?                  -> 0
};
```
Unifies error handling with pattern matching in a uniquely Vyb way. No try-catch pyramid.

---

## 1.0 Release Criteria

For Vyb to be considered production-ready at 1.0, **all of the following must be true**:

### Must-Have for 1.0
- [x] Module system core working (`import`, `smuggle`, `bundle`, `share`, module paths, stdlib discovery)
- [ ] Lambda/closure codegen complete
- [ ] Ownership types runtime-enforced (borrow checking, move semantics)
- [x] Minimal `mild<T>` control block implemented with `soft()`, `grab()`, and `released()`
- [x] Error propagation (Phases 2-5) complete
- [ ] `Option<T>` and `Result<T, E>` in stdlib
- [ ] Core aspects (`Display`, `Debug`, `Clone`, `Equatable`, `Comparable`, `Hashable`)
- [ ] Iterator aspect with `for` loop desugaring
- [ ] Enum/sum types with pattern matching
- [ ] String methods complete (`split` and formatting remain)
- [ ] `HashMap<K, V>` and basic collections
- [ ] FFI (`extern "C"`) working
- [ ] `vyb.toml` and `vyb build` project system
- [ ] Wildcard trap handler (`trap (e<?>)`) with `typeof` discrimination
- [ ] All open contradictions resolved (see section above)

### Should-Have for 1.0
- [ ] REPL (`vyb repl`)
- [ ] Language server (LSP) — at least basic completion and diagnostics
- [ ] `vyb fmt` code formatter
- [ ] `vyb doc` documentation generator
- [ ] Comprehensive language reference manual
- [ ] Test suite covering all 1.0 features
- [ ] `vyb test` integrated test runner
- [ ] Debugger integration validated end-to-end with `gdb`/`lldb`

### Post-1.0 Roadmap
- [ ] Channels + actors (design doc first!)
- [ ] Networking + Sockets (see section below)
- [ ] Self-hosting compiler (Vyb written in Vyb)
- [ ] Macros / metaprogramming
- [ ] Package registry
- [ ] `vyb bindgen` for C header automation
- [ ] Higher-kinded types
- [ ] Compile-time function evaluation (CTFE)
- [ ] `pipe` operator (`|>`)
- [ ] `with` scope blocks

---

## Networking and Sockets

Networking is a post-1.0 feature that **depends entirely on FFI being complete first**.
Raw sockets are POSIX system calls (`socket`, `connect`, `bind`, `recv`, `send`), so Vyb
networking is FFI + a thin standard-library wrapper — not a language feature per se.

### Design Approach (Vyb-native)

Once `extern "C"` FFI lands (v0.5), networking follows naturally:

```vyb
// stdlib/net/tcp.vyb — thin wrapper over POSIX sockets
extern "C" {
    socket(domain<Int>, type<Int>, protocol<Int>)<Int>
    connect(sockfd<Int>, addr<loc<SockAddr>>, addrlen<Int>)<Int>
    send(sockfd<Int>, buf<loc<Void>>, len<Int>, flags<Int>)<Int>
    recv(sockfd<Int>, buf<loc<Void>>, len<Int>, flags<Int>)<Int>
    close(fd<Int>)<Int>
}

struct TcpStream {
    fd<Int>
}

// Higher-level async API (requires real event loop in async runtime)
async tcp_connect(host<String>, port<Int>)<TcpStream> -> {
    // ... resolve address, call connect(), wrap in TcpStream
}
```

### Networking Roadmap

- [ ] **v0.5 — FFI foundation** (`extern "C"` blocks, C type mapping)
- [ ] **v0.5 — Raw socket FFI bindings** — `stdlib/net/raw.vyb` wrapping POSIX socket API
- [ ] **v0.6 — `TcpStream` / `UdpSocket`** — Safe Vyb wrappers with `fail`/`trap` error handling
- [ ] **v0.6 — `TcpListener`** — Server-side accept loop integrated with async runtime
- [ ] **v0.6 — Async I/O** — Non-blocking socket I/O using the async executor (requires real event loop)
- [ ] **v0.7 — HTTP/1.1 client** — Built on `TcpStream`, pure Vyb implementation
- [ ] **Post-1.0 — TLS** — Via `extern "C"` bindings to OpenSSL or mbedTLS
- [ ] **Post-1.0 — UDP multicast, raw packets** — Advanced socket options

### Key Design Decisions

**Q: Is networking a language feature or a library?**
Networking is *stdlib*, not a language feature. The only language feature required is
`extern "C"` FFI, which is planned for v0.5. Everything else is library code written in Vyb.

**Q: How do errors surface?**
Socket errors surface as Vyb `fail`/`trap`: failable functions return `{value, error}` pairs.
There is no exception for network I/O — the `fail`/`trap` system handles it uniformly.

**Q: What about async I/O?**
Async socket I/O requires a real event loop in the Vyb async runtime (currently a stub).
Non-blocking I/O (epoll/kqueue/IOCP) integration is planned for v0.6 alongside `TcpStream`.

## Consistency Work

1. Treat i32/i64 as internal only (surface types are Int/Float/Bool/String/UInt).
2. Standardize tests/examples to use auto-stringifying `println` and Java-like string concatenation.
3. Reconcile docs vs runtime regarding `println_int`/`println_bool` intrinsics.

### Syntax Consistency
- [ ] **`Vec::last()` / `Vec::peek()`** — `pop()` removes the last element but there is no non-removing accessor. Add `Vec::last()` or `Vec::peek()`.
- [ ] **String indexing** — `str.char_at(i)` vs `str[i]` — pick one canonical form and document the other as deprecated.
- [ ] **Struct construction** — Document whether both named-field `Point { x = 1, y = 2 }` and positional `Point(1, 2)` are supported, or only the named form.
- [ ] **`for (item in vec)` mutation** — Iteration currently copies each element. Document copy semantics clearly. Consider `for (ref item in vec)` or `for (borrow item in vec)` syntax for mutable iteration.

### Implementation Consistency
- [ ] **`borrow` prefix vs `borrow()` function call** — Both syntaxes work. The canonical form per `Canonical_Reference_Syntax.md` is `borrow(expr)`. Document `borrow expr` prefix as deprecated.
- [ ] **`their<T>` nested member access** — The semantic analyzer sometimes fails to dereference through `their<T>` for nested member access. Audit all transitive field access paths.
- [x] **Vec cleanup on return** — Transfer-on-return now walks whole-value reads (bare identifiers and `select` arms), so owning values (Vec with malloc'd data, `our<T>`, `mild<T>`) returned via expressions transfer to the caller instead of being freed first.

### Action Items

- Keep `UPDATE_LOG.md` current as the source-biased implementation audit.
- Fix or move non-Vyb *.vyb fixtures (e.g. extracted tests containing C++ snippets).
- Pick a single canonical test runner.

---

*Last Updated: August 2026*
*Current Version: Vyb v0.5.2 (freedom-1.0 series)*
*Overall Status: ~60-65% complete toward 1.0 — 742 tests passing (harness, 100%)*
*SUGGESTIONS.md merged into this document.*
