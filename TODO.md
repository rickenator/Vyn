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

## Implementation Safety and Release Priorities (2026-08-22 audit)

These items take priority over expanding the language surface. They are based on
the current compiler and runtime implementation, and should be closed with a
regression test plus documentation that describes the resulting behavior.

### P0 — Correctness and safety

- [ ] **Make `Vec` access safe and document one contract** — `Vec::get` and
  `Vec::set` currently calculate an element address without a bounds check.
  Decide whether reads/writes return `T?`/a status, trap, or return a documented
  default; implement that rule for every receiver path and add JIT + native
  regression tests for negative, equal-to-length, and empty-vector indexes.
- [ ] **Make file, network, TLS, and UDP diagnostic state concurrency-safe** —
  last-error values and the last UDP peer must be thread-local or carried in
  operation results, rather than process-global state that concurrent threads
  and async workers can overwrite.
- [ ] **Make agent `Int` payloads lossless** — agent behavior loops currently
  use the sentinel-based channel receive path, so a legitimate `-1` message is
  indistinguishable from a closed mailbox. Use the existing presence-reporting
  receive primitive and add a regression test that delivers `-1` before normal
  shutdown.
- [ ] **Prove the advertised ownership boundary** — state precisely which
  borrow/lifetime guarantees are lexical today, then add negative tests for
  borrow escape, aliasing, mutation while borrowed, closure capture, and
  cross-thread handoff. Do not describe the model as a general lifetime solver
  until those cases are enforced.
- [ ] **Add required CI execution coverage** — GitHub Actions must build Vyb
  and run the canonical suite in JIT, object/AOT, and native-link modes. Add an
  ASan job or scheduled memory-safety job; the existing refman-only check is
  not release validation.

### P1 — Toolchain and maintenance hardening

- [ ] **Remove shell-string execution from native build paths** — replace
  concatenated `system()` compile/probe commands with argument-vector process
  execution and robust path handling.
- [ ] **Reconcile the documentation hierarchy** — make the Programmer's Guide
  and generated refman authoritative; update or archive stale design/review
  documents and remove contradictory claims (including obsolete async-stub and
  `Result` placeholder text).
- [ ] **Reduce compiler coupling and source debris** — split the large semantic,
  expression-codegen, and driver units along subsystem boundaries; classify or
  remove obsolete `.bak`/`.backup` sources and inactive codegen files.
- [ ] **Finish the developer workflow** — complete remote dependency handling,
  `--version` and subcommand help, an integrated test command, formatter, LSP,
  and REPL in that order after the safety gate above.

### Audit finding coverage (2026-08-22)

The following is the complete tracking map for actionable findings from the
2026-08-22 implementation review. Architectural descriptions and verified
positive findings are intentionally not issues; every identified correctness,
contract, documentation, maintenance, or release-process gap has a dedicated
tracking issue below. Linked issues may share implementation work, but none is
being treated as an untracked footnote.

| Audit finding | Tracking issue(s) |
|---|---|
| `Vec::get` / `Vec::set` perform unchecked address calculation | [#146](https://github.com/rickenator/Vyb/issues/146) |
| File, network, TLS, and UDP diagnostic state races between threads/fibers | [#147](https://github.com/rickenator/Vyb/issues/147) |
| `-1` is lost as an `Int` agent payload | [#148](https://github.com/rickenator/Vyb/issues/148) |
| Ownership documentation overstates lexical enforcement as general lifetime safety | [#149](https://github.com/rickenator/Vyb/issues/149) |
| No required compiler/JIT/AOT/native/ASan CI validation | [#150](https://github.com/rickenator/Vyb/issues/150), [#158](https://github.com/rickenator/Vyb/issues/158) |
| Native build uses shell-concatenated runtime compile/probe commands | [#151](https://github.com/rickenator/Vyb/issues/151) |
| Canonical guide, generated reference, historical design/status reports, and roadmap claims conflict | [#152](https://github.com/rickenator/Vyb/issues/152) |
| Large compiler units, inactive codegen, backups, and generated/object debris complicate maintenance | [#153](https://github.com/rickenator/Vyb/issues/153) |
| Missing integrated developer workflow (`test`, formatter, LSP, REPL, help/version) | [#154](https://github.com/rickenator/Vyb/issues/154) |
| POSIX `ucontext` fibers, per-task 1 MiB stacks, and fixed worker/thread/agent limits need a supported-runtime policy | [#155](https://github.com/rickenator/Vyb/issues/155) |
| Opaque channel/task/agent/synchronization/socket handles can outlive their safe lifecycle | [#156](https://github.com/rickenator/Vyb/issues/156) |
| Error matching is string-based and legacy defer/ensure runtime APIs are stubs | [#157](https://github.com/rickenator/Vyb/issues/157) |
| CMake `run-tests` is not a CTest test and the claimed full-suite result lacks reproducible CI evidence | [#158](https://github.com/rickenator/Vyb/issues/158) |
| HTTP/HTTPS is not yet a general modern web stack; supported behavior needs a tested boundary | [#159](https://github.com/rickenator/Vyb/issues/159) |
| CMake build-type defaults, optimized builds, and sanitizer profiles are not explicit/reproducible | [#160](https://github.com/rickenator/Vyb/issues/160) |
| `BTreeMap` is a sorted-vector map with O(n) insertion, not a node-based B-tree | [#161](https://github.com/rickenator/Vyb/issues/161) |
| String tracking can become invisible/leak-prone when its fixed registry fills | [#162](https://github.com/rickenator/Vyb/issues/162) |
| `core::result` is retained as a source-compat façade; `Result` is a working compiler builtin ([#163](https://github.com/rickenator/Vyb/issues/163), fixed) |
| `vyb.toml` accepts only an underspecified TOML subset | [#164](https://github.com/rickenator/Vyb/issues/164) |
| Git and version dependency declarations parse but do not resolve | [#165](https://github.com/rickenator/Vyb/issues/165) |
| Generic/aspect validation and codegen contain special-case paths without a clear conformance boundary | [#166](https://github.com/rickenator/Vyb/issues/166) |
| Raw socket, async-I/O, close, timeout, and UDP-peer API semantics need an explicit contract | [#167](https://github.com/rickenator/Vyb/issues/167) |

---

## Overall Completion Estimate

| Domain | Done | Remaining |
|--------|------|-----------|
| Core parsing & lexer | ~95% | Minor edge cases |
| LLVM backend (JIT + AOT + native) | ~85% | LTO, advanced passes |
| Control flow | ~92% | Pattern guards, exhaustiveness, labeled break |
| Type system (primitives + generics) | ~75% | Higher-kinded types |
| Struct system | ~85% | repr(C) for FFI |
| Ownership types (syntax + parsing) | ~100% | `my`/`our`/`their`/`mild`, `view`/`borrow`/`soft`, and semantic enforcement shipped |
| Ownership types (runtime enforcement) | ~100% | Complete: `my<T>` moves, `our<T>` atomic refcounting, `their<T>` borrow/view, `mild<T>` weak refs, struct-owned cleanup (`test/ownership/`) |
| `mild<T>` weak references | ~100% | `soft()`/`grab()`/`released()`, failed `grab()` → `our<T>?`, weak copy/drop accounting (`test/ownership/mild_*.vyb`) |
| Aspect/bind system | ~92% | Static dispatch complete (associated types, inheritance, disambiguation, bound dispatch); `dyn` aspect objects are a marked future experiment |
| Generic monomorphization | ~85% | **SEALED**: Compile-time only. See doc/MONOMORPHIZATION_DESIGN.md |
| Async/await | ~98% | agents (message-passing units) — design doc (`doc/AGENTS_DESIGN.md`); Stages 1-5 shipped (core shape, Int/Bool/Float/String payloads, request/response + composition, failure channeling, backpressure + bounded mailboxes) (`test/agents/`, 5/5) |
| Error propagation (`fail`/`trap`/`ensure`/`refail`) | ~100% | Complete: call-site instrumentation, untrapped handler, `ensure` cleanup, first-class `refail` (`test/trap/`) |
| Lambda/closure codegen | ~90% | Closure env structs, mutable/move/`our` capture, returned-closure env release shipped; rare receiver edge cases remain |
| Module system (`import`/`smuggle`/`bundle`) | ~90% | Phases 1.1–1.5 shipped (`ModuleRegistry`, aliases, `share`/bundle visibility, path resolution); stdlib package integration / `vyb.toml` pending |
| FFI (`extern "C"`) | ~98% | Complete: extern blocks, ABI aliases, `#[repr(C)]`, native `--link`, variadics, OpenSSL binding, `vyb bindgen` (MVP + libclang `--full`) (`test/ffi/`, `test/bindgen/`); niche caveat: bindgen macros calling other macros unbound |
| Standard library | ~85% | Vec, String, HashMap/HashSet, BTreeMap, File I/O, Math, `threads`, `channels`, `tasks`, `asyncs`, `time`, `network` (TCP/UDP/`TcpStream`/`TcpListener`/`UdpSocket`), HTTP server + client, TLS, verified HTTPS client shipped |
| Introspection (`typeof`/`typename`) | ~75% | Downcasting, type assertions |
| Auto-serialization | ~80% | Edge cases remain |
| Pattern matching | ~85% | Struct destructuring, guards, range/`?`/comparison patterns, data-enum variants, `match`-as-expression shipped |
| Package manager / `vyb.toml` | ~90% | Core complete: `vyb.toml`, `vyb build` (multi-file + local path deps), `vyb new`, `vyb.lock`. Remote git/version dependency fetching + package registry are separate staged follow-ups |
| Language server (LSP) | ~0% | Not started |
| REPL | ~0% | Not started |
| Self-hosting compiler | ~0% | Long-term goal |

**Overall: approximately 60-65% complete toward a production 1.0 release.**

### Design Decisions — SEALED

- **Monomorphization vs Polymorphism**: Vyb uses compile-time monomorphization for all generics. No vtables, no dynamic dispatch, no trait objects. Aspects + bind provide polymorphism via static dispatch. See `doc/MONOMORPHIZATION_DESIGN.md`.
- **Aspects over Classes**: Structs for data, aspects for behavior contracts, bind for implementation. No class inheritance. See `doc/TRAIT_SYSTEM_DESIGN.md` and `doc/WHY_TRAITS_NOT_CLASSES.md`.

### Recently Completed
- [x] **Error propagation complete (`fail`/`trap`/`ensure`/`refail`)** — call-site auto-instrumentation, top-level untrapped handler, `ensure` cleanup blocks, and a first-class `refail` keyword (validated to live inside a `trap` clause) all shipped and covered by `test/trap/`
- [x] **FFI + `vyb bindgen` complete** — `extern "C"` blocks, ABI aliases, `#[repr(C)]`, native `--link`, variadics (incl. `printf`), OpenSSL binding, and `vyb bindgen` (lightweight MVP + libclang `--full` preprocessor/macro backend) (`test/ffi/`, `test/bindgen/`)
- [x] **Runtime-enforced ownership complete** — `my<T>` moves, `our<T>` atomic refcounting, `their<T>` borrow/view, `mild<T>` weak refs (`soft`/`grab`/`released`), and struct-owned cleanup (`test/ownership/`)
- [x] **Package manager core shipped** — `vyb.toml` manifest (`[package]`/`[dependencies]`/`[[bin]]`), `vyb build` (multi-file + local path deps), `vyb new`, and `vyb.lock`; remote git/version fetching + registry are staged follow-ups
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
- [x] **Test harness** — `--parse-only` flag forwarded to binary for `@parse-only: true` tests; `n/a` annotation values treated as "skip this check"; the canonical suite now runs **1077 tests, 1077 passing** via `test/run_tests.py`
- [x] **Vec parameter deep copy** — Vec parameters receive an independent copy of the data on function entry, eliminating double-free bugs (e.g. recursive quicksort base-case return)
- [x] **Vec mutation through borrowed struct fields** — `s.items.push(val)` where `s<their<T>>` now correctly mutates in-place; member-expression Vec calls now get a field *pointer* (not a loaded copy)
- [x] **Semantic use-after-free fix** — `handleVecMethodCallOnMember` no longer stores raw pointers from temporary `VecType` objects into `expressionTypes`; all return types are cloned into `node->type` first
- [x] **`test/new_features` 100% pass** — All new-features tests pass (quicksort, stack, insertion sort, etc.)
- [x] **C-like enum codegen** — `enum Color { Red, Green, Blue }` compiles; variants are first-class typed values (`r<Color> = Color::Red`) rendered as `Color::Red` by `println`, matching by name in `match`/`select` with the same exhaustiveness as data enums; the raw positional tag stays available for FFI
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
- [x] **Bitwise operators** — `|` (or), `&` (and), `^` (xor), `~` (not), `<<` (left
  shift), `>>` (right shift) on `Int`, plus the `&=`, `|=`, `^=`, `<<=`, `>>=`
  compound-assign forms. `<<`/`>>` are lexed as adjacent `<`/`>` tokens (not dedicated
  shift tokens) so nested generic closes like `Vec<String>>` keep working, while
  `<<=`/`>>=` remain single tokens. The stdlib File I/O module now combines its
  `FileFlag` open-mode bits with `|` (`FileFlag::WRITE | FileFlag::CREATE | FileFlag::TRUNC`)
  instead of addition. Emits LLVM `and`/`or`/`xor`/`shl`/`ashr`/`lshr` codegen; the
  `and`/`or` keywords stay distinct from the new symbol operators. Combining two
  typed integers of *different* widths is a compile error (cast explicitly via
  `as`); a bare integer literal adapts to the typed operand's width, and compound
  assigns coerce a literal RHS to the LHS width. Covered by
  `test/expressions/test_bitwise.vyb`, `test_bitwise_widths.vyb`,
  and `test_bitwise_mismatch.vyb`.
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
- [ ] **Ser/deser edges — `fn` / `Self` / recursive structs** — `fn`-typed and `Self`-typed
  fields are excluded from serialization/deserialization (guarded + documented note,
  `PROGRAMMERS_GUIDE.md` §3.21). Self-referential structs (`next<Node?>`) currently
  **segfault** the toolchain at type registration; make that a graceful disallow or a
  properly-supported path. Broader general recursion support remains a follow-on.

### Infrastructure
- [x] **CMake build system** — LLVM integration
- [x] **Test harness** — 657+ tests, parallel execution, HTML/JSON reports; `--parse-only` mode; 315 passing (47.9%)
- [x] **`println()`** — Works with all data types including Vec<T>

---

## In Progress

### Error Propagation — fail/trap/ensure/refail (DONE)
- [x] Phase 1: Semantic detection of failable functions (`canFail`)
- [x] Phase 2: Dual return value codegen `{ T, ptr }` for failable functions
- [x] Phase 3: `fail` statement returns error to caller when no trap in scope (`test/trap/propagation_no_trap.vyb`, `test/trap/defer_runs_on_fail.vyb`)
- [x] Phase 4: Call site instrumentation — auto-check `{ value, error }` tuple (`test/trap/propagation_no_trap.vyb`, `test/trap/non_failable_caller_rejected.vyb`)
- [x] Phase 5: Top-level untrapped error handler (`__vyb_runtime_untrapped_error`) (`test/trap/propagation_to_main.vyb`)

### Aspect System — Completion (HIGH PRIORITY)
- [x] Phases 1-4: Declarations, method calls, generic impls, type param substitution
- [x] **Associated types (slice: defaults + bounds + generic binds)** — `type Item` declarations, `bind` assignments (`type Item = Int`), validation for missing/unknown/duplicate assignments, default associated types (`type Item = Int`), aspect bounds (`type Item<Display>`), and resolution through generic binds (`bind<T> Iterator -> Boxer<T> { type Item = T }` substitutes the concrete type at the call site) are implemented. `Self::Item` used directly as a bind method's return type now also resolves in both concrete and generic bind bodies (the impl context is established before the monomorphized signature is built, substituting the concrete type argument at the call site). The associated-type slice is complete; runtime (dyn) dispatch is a deliberate non-goal (see `Dynamic Dispatch (`dyn Aspect`) — Marked as Future Experiment` below).
- [x] **Aspect inheritance** — `aspect Comparable : Equatable` super-aspects: declared super-aspects are validated against defined aspects, cyclic dependencies are rejected, and binding a sub-aspect requires the same type to also bind each super-aspect (order-independent).
- [x] **Qualified aspect-method disambiguation** — `DisplayA::show(thing)` selects a specific aspect whenever multiple bound aspects declare the same method name for a type; unqualified ambiguous dot-calls (`thing.show()`) remain rejected. Bind method symbols are emitted per `Type_Trait_Method` so distinct implementations coexist. Also works on bounded type parameters (`Aspect::show(thing)` for `thing<T<Aspect>>` inside generic functions).
- [x] **Unqualified method dispatch on bounded type parameters** — `thing.show()` for `thing<T<Display>>>` inside a generic function resolves the method through the bound aspect without an explicit `Aspect::` prefix, substitutes `Self` in the return type, and walks the bound's transitive super-aspect chain so inherited methods (e.g. `thing.name()` where `Display : Named` and `name` lives on `Named`) are dispatchable in both the unqualified and qualified (`Named::name(thing)`) paths. Runtime dispatch monomorphizes to the concrete type's bind.
- [x] **Monomorphization with bounds validation** — Generic function calls infer type arguments from the call site, substitute them into the return type, and reject concrete instantiations whose type does not bind the declared aspect(s).
- [x] **Bind selection precedence** — A bounded generic bind (`bind<T<Aspect>>`) deterministically takes precedence over an unbounded bind (`bind<T>`) for the same type shape, independent of declaration order.

### Pattern Matching — Completion (MEDIUM PRIORITY)
- [x] Literal patterns, wildcard `?`, comparison operators
- [x] **Struct destructuring** — `Point { x, y } ->` in match arms: a struct pattern binds each listed field as a local variable in the arm body (extracted from the matched struct value). Field names are validated against the struct, and a struct pattern whose type can never match the match expression's static type is rejected.
- [x] **Enum/sum type variant patterns** — `Circle(r) ->`, `Rect(w, h) ->`, `Unit ->` (tagged-union enums): data enums compile to a value-semantics `{ i64 tag, [N x i8] data }` union, construct via `Shape::Variant(args)`, and match arms on variants compare the runtime tag and bind payload fields. `select` variants, exhaustiveness, and generic data enums (via explicit type args, e.g. `Box<Int>::Value(x)`) are implemented.
- [x] **Range patterns** — `1..10 ->` in match arms: an inclusive `[start, end]` bound check compiled for integer/float match values; inverted (`start > end`) ranges are rejected as never-matchable.
- [x] **Guard clauses** — `pattern if condition ->` in match arms: a guard runs after the pattern matches (destructured struct fields are available to it); if false the arm is skipped and matching falls through to later arms or the default. A guarded wildcard is treated as non-exhaustive so downstream arms stay reachable.
- [x] **Exhaustiveness checking** — A `match` on a data-carrying enum must be
  exhaustive: either an unguarded wildcard or arms covering every variant. The
  semantic analyser rejects a non-exhaustive match, naming the missing variant(s);
  codegen treats an exhaustive match's no-match default as unreachable, so an
  all-return match used as the final statement compiles cleanly. A guarded arm
  does not count as unconditionally covering its variant: the variant needs an
  unguarded arm (or a guarded arm plus a separate unguarded duplicate) or a
  wildcard.
- [x] **`match` as expression** — `r<Int> = match (v) { pattern -> val, ? -> val }`
  yields the matched arm's value. The result type is inferred from the first
  arm's body expression; codegen allocates a zero-initialized result slot and
  stores the value from naked-expression arms (including ranges, guards, and
  struct destructuring). The statement form still produces no value.

### Lambda / Closures (MEDIUM PRIORITY)
- [x] Parsing — `|x, y| -> x + y` and `|x<Int>| -> { ... }`
- [x] Semantic analysis — capture detection, type inference
- [x] **Indirect call codegen** — Lambda stored in a local variable can be called; `localLambdaTypes` map tracks inferred function types; body return value coerced to declared return type
- [x] **Full LLVM closure struct codegen** — A lambda is a uniform closure value
  (`struct { ptr env, ptr fn }`); captured variables (detected by the semantic
  analyzer from free-variable references) are copied by value into a
  heap-allocated environment at creation and reloaded into the lambda's local
  alloca through a hidden first parameter. Capturing closures work standalone,
  from local/returned closures, and as `fn` arguments to the higher-order
  combinators (`test/lambda/test_closure_capture.vyb`). Non-capturing lambdas
  use a null environment, so existing `fn`-parameter code is unaffected.
- [x] **Mutable capture** — A lambda that assigns to a captured variable writes
  through to the outer variable: the environment stores the outer variable's
  address, each invocation snapshots the current value into a local alloca, and
  assignments (plain and compound) propagate back. The enclosing scope observes
  every mutation (`test/lambda/test_closure_mutable_capture.vyb`).
- [x] **Move capture** — Capturing a `my<T>` transfers ownership into the
  closure: the semantic analyzer records the outer variable as moved, so reading
  it afterward is a use-after-move diagnostic (`test/lambda/test_closure_move_capture.vyb`).
- [x] **`our<T>` capture** — Capturing an `our<T>` bumps its strong count so the
  shared value stays alive for the closure's lifetime
  (`test/lambda/test_closure_our_capture.vyb`).
- [x] **Closure env lifetime** — Closure capture environments are reference
  counted (`{ i64 refcount; ptr cap_dtor; <captures...> }`): copying a closure
  into a storage location retains the env, and variable/parameter scope exit and
  overwrite release it, freeing the env block when the last reference is dropped.
  Returned closures hand an owned reference to the caller.
- [x] **Owned/member-receiver capture** — Capturing an ownership-qualified value
  (`our<T>` shared read and write-through, `my<T>` move, `their<T>`/`view<T>`
  borrow) and reading its fields inside the closure now resolves: the reloaded
  capture alloca carries the captured variable's AST type, so field access like
  `shared.n` / `mine.n` / `vr.n` compiles instead of erroring
  (`test/lambda/test_closure_owned_field_capture.vyb`).
- [x] **Transferred `my<Struct>` capture ownership** — An immutable capture of a
  standalone `my<Struct>` now *moves* the heap object into the closure env: the
  enclosing binding's slot is nulled (so its scope-exit cleanup skips it) and the
  env's generated per-layout cap_dtor reclaims the object's owned fields and
  frees it when the env's last reference drops. This keeps the captured value
  alive (and its owned fields reclaimed) even when a closure outlives the
  enclosing scope or is returned (`test/lambda/test_closure_owned_struct_capture.vyb`).
- [x] **Returned-closure env release** — A closure handed back across a `return`
  now retains its env so the caller's later release returns it to 0 and frees the
  block. The retain was only emitted for top-level `fn` declarations (via the
  AST), not for lambda bodies (`currentFunctionAST` points at the enclosing
  declaration); the return retain now also triggers when the LLVM function's
  return type is itself a closure struct, and the expression-body lambda return
  path retains as well. Eliminates the env (24B) leak for any returned capturing
  closure and lets a transferred `my<Struct>` payload's cap_dtor run
  (verified clean under valgrind for block-body, expression-body, and `my`-owned
  returns).

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
    - [x] `core::math` — composition helpers (`clamp`, `is_close`) layered over the global math intrinsics, explicitly imported via `import core::math` (`test/modules/stdlib_core_math.vyb`)
    - [x] `collections` — `HashMap<K,V>` / `HashSet<K>` shipped (`import collections`, by-ref bind methods, auto-growing hash-bucket `Hashable` key lookup; `test/modules/test_collections_hashmap.vyb`, `test/modules/test_collections_growth.vyb`), plus the `VecOps` view/ordering helpers, the unconstrained `VecHigherOps` `map`/`filter`/`reduce`/`iter` combinators, and the generic `VecIter<T>` iterator (`v.iter()`, bound to `Iterator`, `test/modules/test_vec_iter.vyb`)
    - [x] `io` — File I/O shipped (`import io`): `File { fd, path }`, `open` + `open_read`/`open_write`/`open_append`, `close`, `write_str`, `read_all`, `error_code`/`error_message`, and the `FileFlag` constant-enum modes (`FileFlag::READ` &c., combined with `|`) over the runtime `__vyb_file_*` intrinsics (`test/modules/test_file_io.vyb`)
    - [x] `http` — Pure-Vyb HTTP/1.1 server + client (`import http`): `http_listen`/`http_local_port`/`http_accept`, head reading + `http_request_path` parsing, `http_send_all`/`http_close`, well-formed `http_response(status, body)`, and the `http_get`/`http_request` client wrappers — all layered directly on the `__vyb_net_*` socket intrinsics (no new FFI). `String::index_of` added as a `StringOps` core helper to parse request/response heads. Single-threaded blocking sockets mean a server only answers a kernel-queued connect in-process (interleaved send/recv); a living peer is needed to exercise the `http_get` round-trip (`test/modules/test_http_parse.vyb`, `test/modules/test_http_server.vyb`)
    - [x] `core::iter` — the `Iterator` aspect protocol (`type Item` + `next(self<their<Self>>)<Self::Item?>`, explicitly imported via `import core::iter` — kept out of the auto-imported `core::aspects` so the ~6 associated-type tests that define their own local `Iterator` don't clash). Consumable via explicit `.next()` / `match` loops (`test/modules/test_iterator_protocol.vyb`) and via `for (item in <iter-expr>)` for any non-identifier iterable expression (`test/modules/test_for_iter.vyb`); the desugar is parse-time (type-blind) and keys off a non-identifier iterable so the existing Vec index-based identifier path and range path are untouched.
  - [x] Auto-import of `core::*` (opt-out with directive) — the core contracts module (`core::aspects`, with its pre-wired primitive binds) is auto-imported into every non-stdlib module unless it already imports the contracts, locally redefines them, or opts out with a `no_core()` directive. This makes `x.display()`, `a.equals(b)`, `a.compare(b)`, and `a.clone()` available on built-in scalars with no import. The transitional prelude helpers (`OptionInt`, `prelude_ok`) remain explicit-import-only.
- [ ] **Known defect: imported module ASTs are single-consumer** — the splice loop in `ModuleRegistry::resolveModule` moves a dependency's declaration statements (`resolvedBody.push_back(std::move(importedStmt))`) into the *first* importer's body, so an already-imported module has no declarations left to splice for a later importer (`importedRecord.emitted` then skips re-splicing). Consequences: (a) two *subset* imports of the same module in one file drop the second subset (https previously failed with `Undefined identifier: http_status_code` because it did `import http::{HttpResponse}` then `import http::{helpers}`); (b) a shared dependency consumed by one importer is unavailable to another (`socket_close` regressed when http imported network after https already consumed it). Workaround in `stdlib/https`: combine all of a module's subset imports into one `import m::{a, b, …}` (does not need the second subset or the shared-network path). Long-term fix: deep-clone AST statements per importer (no `clone()` on `Stmt`/declarations yet) or emit each dependency module once and resolve cross-module references without moving its body; then drop the `emitted` skip so subset imports splice independently and shared deps reach every consumer.

### 2. FFI — C Interop (DONE)
- [x] **`extern` function modifier** — Individual extern function declarations compile to LLVM `ExternalLinkage` via `ExternStatement` codegen; syntax: `extern funcName(params)<ReturnType>`
- [x] **`extern "C" { }` block syntax** — Multi-declaration blocks parse, register external functions, and codegen LLVM declarations
- [x] **C type mapping** — Common C aliases (`CInt`, `CSize`, `CString`, `CPtr<T>`, `CVoid`, etc.) lower through semantic/codegen
- [x] **`#[repr(C)]` on structs** — Attribute parses on structs, preserves declaration-order unpacked LLVM layout, and rejects generic/Vyb-runtime fields that are not C ABI-stable
- [x] **Native `--link <lib-or-path>` flow** — Repeatable build flag passes `-l<lib>` or explicit library/object paths to the native linker
- [x] **Variadic C functions** — `extern "C" { printf(format<loc<Int8>>, ...)<Int> }`; a trailing `...` marks the declaration variadic, codegen emits `isVarArg`, and call sites accept any number of extra arguments (with Vyb String varargs auto-extracting their `char*` data pointer so `printf("%s", s)` works). `printf("%d-%s", 7, "vyb")` is covered by `test/ffi/variadic_c_printf.vyb`.
- [x] **`vyb bindgen` (MVP)** — `vyb bindgen <header.h> [-o out.vyb]` parses a C subset (typedefs, `struct`/`enum` declarations, scalar/pointer types, trailing `...` varargs) and emits `share(all)` bodyless extern declarations + `#[repr(C)]` structs + enums. Functions, structs, and enums (C-like constants and payload enums) re-export across `import` and resolve against the host C ABI; covered by `test/bindgen/libsample.vyb` + `test/bindgen/test_libsample_bindings.vyb` (and `test/enum/test_import_c_like.vyb` / `test_import_data_enum.vyb`).
- [x] **`vyb bindgen` `--full` (libclang full preprocessor)** — libclang-based full-preprocessor backend landed. `vyb bindgen <header.h> --full` runs a libclang backend in a standalone helper (`src/bindgen_libclang.cpp` + `src/bindgen_libclang_main.cpp`, built as `vyb-libclang`) so libclang's LLVM command-line options cannot collide with the statically-linked JIT engine. Expands `#include` and evaluates conditionals (`#if`/`#ifdef`), resolves typedefs through canonical types (`int32_t` -> `CInt`, `uint64_t` -> `CULong` on LP64, `size_t` -> `CSize`), and binds `#define` macros (object-like numeric and constant-expression macros grouped into a file-scoped constant enum named after the header's basename, e.g. `WIDE (2 * COUNT)` -> `enum FullPreproc { WIDE = 8 }` used as `FullPreproc::WIDE`; String/Float object-like macros as shared constant functions; function-like macros as type-aware shared functions, lowering C `?:` ternaries to Vyb `select` and mapping to `Int`/`Float`/`Bool`/`String` (e.g. `SQUARE(x) -> (x * x)<Int>`, `MAX(a,b) -> select<Int>`, `IS_EVEN(x) -> <Bool>`)). `-D NAME[=VAL]` flags drive conditional selection. Only declarations whose source is the input header are rebound; system/libc types are resolved but not re-emitted. Covered by `test/bindgen/full_preproc.h` / `full_preproc.vyb` / `test_full_preproc_bindings.vyb`; the lightweight hand-rolled parser remains for `vyb bindgen <header.h>` without `--full`. Fixed-size C array struct fields (direct/nested/typedef'd) bind as contiguous value-array fields (`[CChar; 8]`, `[[CDouble; 3]; 2]`); flexible array members are skipped with a warning. Covered by `test/bindgen/arrstruct.h` / `arrstruct.vyb` / `test_arrstruct_bindings.vyb`. C unions bind as `#[repr(C)]` structs with the highest-aligned member as an accessible anchor plus a `[UInt8; N]` pad to the union's total size (`test/bindgen/unions.h` / `unions.vyb` / `test_unions_bindings.vyb`). Function-like macros with comparison, logical, ternary, and string bodies now bind (covered by `MAX`, `CLAMP`, `IS_EVEN`, `IS_POS`, `STATUS` in `full_preproc.h`); macros that call other macros by name remain unsupported.

### 3. Ownership Types — Runtime Enforcement (DONE)
- [x] **`my<T>` move semantics** — Compile-time single-owner enforcement: use-after-move rejection, transfer on assignment/init/`my`-param/move-capture, revive-on-reassignment, and read/copy to plain (non-`my`) targets without moving (`test/ownership/move_*.vyb`).
- [x] **`our<T>` reference counting** — Shared control-block strong_count with full copy/assignment/parameter semantics: every binding holds its own strong ref (retained on shared copy, released on scope exit and on overwrite); transfer sources (`our()`, `grab()`, functions returning `our<T>`) hand over a single ref without extra retain (`test/ownership/our_*.vyb`).
- [x] **`their<T>` borrow checker (lexical phase)** — borrow/view require lvalues, reject overlapping mutable/view borrows, reject assignment while borrowed
- [x] **`mild<T>` control block (minimal runtime)** — `soft()` increments weak_count, `released()` observes release after strong owner scope exit, and live `grab()` upgrades by incrementing strong_count
- [x] **`view(expr)` semantic (lexical phase)** — Creates `their<T>` view and participates in borrow conflict checks
- [x] **`borrow(expr)` semantic (lexical phase)** — Creates mutable `their<T>` and participates in borrow conflict checks
- [x] **`soft(expr)` semantic** — Creates `mild<T>` from `our<T>`; enforced
- [x] **Thread-safe runtime refcounts** — The two refcount paths outside the
  (already-atomic) `our<T>` control block are now atomic. The heap-String
  registry in `runtime/vyb_runtime.c` keeps `refs` as an atomic `int64_t` with
  lock-free retain/release RMWs, and a small pthread mutex serializes slot
  claiming in `register()` plus the slot reset on last release; the legacy
  per-name `Vec-With-malloc` refcounts emitted in `cgen_ownership.cpp`
  (`incrementRefCount`/`decrementRefCount`) now use LLVM `AtomicRMW` instead of
  plain load/add/sub/store. No behavior change single-threaded; foundation for
  the pthread-backed `threads` module.

### 4. Standard Library Expansion (HIGH PRIORITY)
- [x] **`Option<T>` (removed)** — the Rust-shaped `Some`/`None` enum was superseded by the native `T?` optional and removed from the compiler (and the transitional `core::option` bridge)
- [x] **`Result<T, E>`** — `Ok(value)` / `Err(error)` for fallible operations; built-in generic enum (`core::result` placeholder module retained for source-compat)
- [x] **Core aspects** — `Display`, `Debug`, `Clone`, `Equatable`, `Comparable`, `Hashable` — the `core::aspects` stdlib module declares all six contracts with `Comparable : Equatable`, re-exported via `core::prelude`/prelude, and they are bindable to both structs and primitive scalar targets with unqualified dispatch and generic bounds. Binds now carry across module imports (visibility via `share`, dedup by `(target, aspect)`), so `core::aspects` ships pre-wired `Display`/`Clone`/`Equatable`/`Comparable`/`Hashable` impls for `Int`, `Float`, `Bool`, and `String` that take effect on `import core::aspects` / `import core::prelude` (`test/aspect/test_core_aspects_bindings.vyb`, `test_bind_primitive_target.vyb`, `test_core_aspects_primitive_impls.vyb`). Auto-import of `core::*` remains under Module System Phase 1.6.
- [x] **String methods** — `.len()`, `.contains()`, `.starts_with()`, `.ends_with()`, `.to_upper()`, `.to_lower()`, `.substring()`, `.char_at()`, `.trim()`, `.replace()`, `String::from_bytes()`
- [x] **String methods (remaining)** — `.split()`, `.format()` — `split(sep)` (a `StringOps` aspect bound to `String`) returns a fresh `Vec<String>` of the parts between each occurrence of `sep` (empty separator yields a single-element Vec; leading/trailing/consecutive separators produce empty parts), and `format(args...)` (a built-in String method) substitutes sequential `{}` placeholders with the string form of each argument of any serializable type, leaving extra placeholders verbatim (`test/string/test_str_split.vyb`, `test/string/test_str_format.vyb`)
- [x] **String formatting** — `.format()` method (Format strings or `fmt()` intrinsic)
- [x] **`HashMap<K, V>`** — Hash map with `Hashable + Equatable` bounds (parallel `keys`/`vals` vectors indexed by auto-growing hash-bucket chains; `import collections`)
- [x] **`HashSet<T>`** — Hash set (`values` vector indexed by hash-bucket chains with duplicate suppression; `import collections`)
- [x] **`BTreeMap<K, V>`** — Ordered map with `Comparable` bounds (keys in a
  sorted `keys` vector + parallel `vals`; `get`/`contains_key` binary search,
  `put` inserts at the sorted position; `iter()` walks entries in ascending key
  order via `BTreeIter` with `MapEntry<K,V>` items; `import collections`)
- [x] **File I/O** — `import io`: `File { fd, path }`, `open(path, flags)` + `open_read`/`open_write`/`open_append`, `close`, `write_str`, `read_all` (whole file into a `String`, empty on error), and the `error_code()`/`error_message()` + `FileFlag` open-mode constants (`FileFlag::READ | FileFlag::CREATE`, ...), layered on runtime `__vyb_file_*` intrinsics (`test/modules/test_file_io.vyb`)
- [x] **Math library** — `sqrt`, `sin`, `cos`, `tan`, `exp`, `log`, `log2`, `log10`, `pow`, `floor`, `ceil`, `round`, `abs`, `min`, `max`
- [x] **Time / clock** — `import time`: `time_epoch_secs`/`time_epoch_millis`/`time_nanos`
  (Unix epoch wall-clock timestamps) and `time_mono_millis` (monotonic ms, the stable choice
  for intervals/timeouts), plus `sleep_ms(millis)`, layered on runtime `__vyb_time_*`
  intrinsics (`clock_gettime`/`nanosleep`; `test/modules/test_time.vyb`)
- [x] **HTTP server + client (pure Vyb)** — `import http`: loopback/tcp serving
  (`http_listen`/`http_local_port`/`http_accept`, `http_read_head` up to
  `\r\n\r\n`, `http_request_path` target extraction, `http_send_all`/
  `http_close`, and `http_response(status, body)`) plus the `http_get(host,
  port, path)` client, all in Vyb over the `__vyb_net_*` socket intrinsics.
  Adds `String::index_of(needle) -> Int` (StringOps core bind, first occurrence
  or `-1`) used to parse heads. Single-threaded blocking sockets mean an
  in-process server answers a kernel-queued connect (interleaved send/recv) and
  a *living* peer is required to exercise `http_get`; a real async/event-loop
  accept remains on the roadmap (`test/modules/test_http_parse.vyb`,
  `test/modules/test_http_server.vyb`)
- [x] **I/O intrinsics** — `print()` (no newline), `println_int()`, `print_int()`, `println_bool()`, `print_bool()`
- [x] **`for`-loop desugar over `Iterator`** — `for (item in <iter-expr>)` now desugars onto `core::iter::Iterator` when the iterable is a **non-identifier expression** (e.g. `intsums.iter()` — the natural case, since `v.iter()` returns `VecIter<T>`). The transform emits `{ var __it_<item> = <expr>; while (true) { match (__it_<item>.next()) { item -> { body } ? -> { break } } } }`, so `break`/`continue` re-enter `next()` and re-evaluating the producer each loop starts a fresh iterator (`test/modules/test_for_iter.vyb`). This was made possible by the earlier `core::iter` protocol, nested `their<Vec<T>>` field resolution (`test/modules/test_nested_their_vec_field.vyb`), generic-bind `Result<T,E>` materialization, and the `VecIter<T>`/`v.iter()` stdlib iterator (`test/modules/test_vec_iter.vyb`). The desugar is parse-time and type-blind, so it keys off a non-identifier iterable: plain identifiers keep the existing index-based Vec path and `0..n` ranges the inclusive range path (no regressions). The optional `skip`/step parameter is supported too: `for (item in <iter-expr>, step)` advances the iterator `step` elements per iteration and yields indices 0, step, 2*step, ... (matching the Vec index path; `break`/`continue` stay correct, `test/modules/test_for_iter_skip.vyb`). The desugar lives in `StatementParser::buildForLoopIteratorDesugar` and is parse-time/type-blind, so it keys off a non-identifier iterable. **Identifier iterables now route onto the protocol too**: `for (x in vec)` desugars exactly like `for (x in vec.iter())`, replacing the old index-based `__idx`/`__len` over `vec.get(i)` path. Because the parser has no types at this point, the uniform rule is that any iterable value must expose an `iter()` that yields an `Iterator` — Vec collections provide `iter()` (`import collections`' `VecHigherOps`), and the stdlib iterators themselves are self-iterable (their `iter()` returns a fresh iterator over the same underlying collection), so a stored iterator identifier (`for (y in storedIter)`) iterates too. The standalone `for (x in vec)` without `import collections` now requires the module (`.iter()`/`VecIter` live there). **`HashMap`/`HashSet` iterator binds are done**: reading fields *through* a `their<T>` view field of a generic struct (the earlier blocker) now resolves — `cgen_expr` records each member-read value's AST type in `valueTypeMap`, so `self.set.values.get(i)` / `self.map.keys.get(i)` chain through a nested `their<HashSet<K>>` / `their<HashMap<K,V>>` field. `import collections` ships `MapIter<K,V>` (`m.iter()` yields key/value pairs as `MapEntry<K,V>`, `kv.key` / `kv.value`) and `HashIter<K>` (`s.iter()` yields values), bound to `Iterator` (`test/modules/test_collections_iter.vyb`). This required fixing the `TypePattern` argument splitter, which previously split generic arguments on every comma regardless of nesting depth — so a two-parameter iterator `Item` like `MapEntry<K,V>` mangled to a malformed type. The splitter is now depth-aware.
- [x] **`Vec<T>` expansion** — shipped via the `VecOps` bind on the built-in
  `Vec<T>` (pure Vyb; `test/modules/test_vec_expansion.vyb`):
  `find` (first matching index, or `-1`), `first`/`last` (head/tail element),
  `reversed` (fresh copy), and `sorted` / `min` / `max` (ordering dispatched
  through the `Comparable`-bounded `cmp_lt` helper — a direct `compare` call
  does not resolve on a generic element). The unconstrained `VecHigherOps`
  bind adds the higher-order `map` / `filter` / `reduce` combinators over
  non-capturing lambda `fn` arguments (any element type). By-ref
  `their<Vec<T>>` receivers now enable the in-place forms `sort_in_place`
  (VecOps), `map_in_place` / `retain` / `reverse_in_place` (VecHigherOps),
  so the built-in Vec mutating primitives (`len`/`get`/`set`/`pop`) resolve
  through the ownership wrapper in both semantic and codegen. The aspect-bound
  in-place forms now dispatch on a by-ref `their<Vec<T>>` view too (function/borrow
  parameter or a local `view()`/`borrow()` reference, `Int`/`String` elements;
  `test/modules/test_vec_inplace_byref.vyb`) — codegen passes the wrapper's stored
  `Vec*` pointer to the by-ref self.
  The in-place forms (and aspect/bind dispatch generally) now work on
  *member-expression* receivers too — a struct field like `h.c.bump()`, an owned
  `self.items<Vec<Int>>` field, or through a nested `their<Vec<T>>` view field
  (`self.data.sort_in_place()`; `test/aspect/test_aspect_member_receiver.vyb`);
  codegen evaluates the member in LHS mode (a pointer to the field) and hands it
  to the bind's by-ref `self<their<Self>>` receiver. `.contains()` is now correct.
- [x] **`Vec<T>` constructor idiom** — `Vec::new()` / `Vec::new(size)` replaced by a vybish constructor call: `Vec()` (empty growable) and `Vec(n)` (preallocate `n` elements/capacity), element type inferred from the annotation. `Vec::new()` stays as a back-compat alias.


### Threading and Concurrency (now a primary issue)

Vyb is single-threaded today. After the `http` module, the decision is that full
multithreading is a primary goal, implemented with **pthreads underneath** and a
thin, ergonomic Vyb-facing module (not raw C-shape; a C-shaped surface is exactly
the "weeds" to avoid — abstraction to pure-Vyb ergonomics is built in up front,
with only the pthread ABI beneath). The thread-safety foundation above came first.

- [x] **`threads` stdlib module (pthread-backed, MVP)** — `import threads`:
  `thread_spawn(fn() -> Int)` starts a closure on a fresh pthread and returns a
  handle, `thread_join(handle)` blocks for its result (reclaiming the slot; -2
  for an unknown/already-joined handle), `thread_detach(handle)` marks a
  fire-and-forget thread that self-reaps its slot when its body returns (a
  reaper, so detached workers can't exhaust the 256-slot table; detaching again
  → -1, joining a detached/already-reaped handle → -2), and a `Mutex`
  (`mutex_new`/`lock`/
  `unlock`/`free`). A thread-entry trampoline unpacks a Vyb `fn` (a uniform
  closure `{ptr env, ptr fn}`) and runs it as `int64_t (*)(void*)` with its
  hidden environment param. Capturing closures work across the spawn boundary:
  the runtime retains the closure env on spawn and releases it when the body
  returns, so each worker sees its own capture (regression
  `test/modules/test_closure_capture.vyb`).
  Verified with overlapping sleeps, per-thread results, per-handle detach
  semantics, slot reclamation past the 256 cap, and a mutex round-trip
  (`test/modules/test_threads.vyb`).
- [x] **CondVar** — `cond_new`/`cond_wait`/`cond_signal`/`cond_broadcast`/
  `cond_free` over a heap `pthread_cond_t`; `cond_wait(cv, m)` takes the caller's
  Mutex handle so it atomically releases `m` while sleeping and reacquires it on
  wake (mutex-guarded predicate pattern, no lost wakeup). Verified by a worker
  blocked in `cond_wait` until the producer publishes a flag under the mutex and
  signals it (`test/modules/test_cond_atomic.vyb`).
- [x] **AtomicInt** — `atomic_new`/`atomic_load`/`atomic_store`/`atomic_add`/
  `atomic_cas`/`atomic_free` over a heap lock-free seq_cst integer;
  `atomic_add` returns the *new* value and `atomic_cas` returns 1 on a successful
  swap, 0 otherwise. Verified by read-modify-write ordering checks
  (`test/modules/test_cond_atomic.vyb`).
- [x] **Typed channels, first cut (`channels` module)** — a pthread/Mutex+CondVar
  heap ring-buffer for Int values: `chan_new` (unbounded, send always grows the
  buffer) and `chan_bounded(n)`, with non-blocking `chan_send` (1/0, 0 once a
  bounded buffer is full or the channel is closed), blocking `chan_recv`,
  non-blocking `chan_try`, `chan_len`, and `chan_free`. A minimal, ergonomic
  surface in the `threads`-module style (`import channels`;
  `test/modules/test_channels.vyb` — producer threads into a shared unbounded
  channel with an order-insensitive blocking recv-sum, plus bounded capacity and
  poll behavior). Also gained **`chan_select(handles<Vec<Int>>)`** — blocks until
  one of the listed channels has a value (or is closed) and returns its index
  without consuming (a ~1ms polling first-cut; `test/modules/test_chan_select.vyb`),
  and **String-payload channels** (`strchan_new/send/recv/try/len/free`) that
  retain the string on send and transfer that reference on recv/try, with no
  buffer dangling unless a bounded channel is freed while still buffered
  (`test/modules/test_strchan.vyb`).
- [x] **Typed generic `chan<T>` (built-in)** — a compiler-native generic
  thread-safe channel carrying typed payloads: `chan<T>()` (unbounded) /
  `chan<T>(cap)` (bounded) construct a single i64 runtime handle (Intel-like
  ABI, so sharing a chan by value across pthreads references the same channel).
  Methods `send(v)`, `recv()` (blocking), `poll()` (non-blocking), `len()`,
  `free()`, and `handle()` (the raw Int for `chan_select`). Int-family scalar
  payloads use the int-slot runtime; String payloads use the refcounted string
  runtime (`test/modules/test_chan_typed.vyb`, `test/modules/test_chan_threaded.vyb`).
  Float/Bool/Char payloads are supported too (a Float travels as its IEEE bit
  pattern, Bool as 0/1, Char as its code unit), and scalar `poll()` returns
  `T?` (present when a value is queued, absent when empty) so an empty read is unambiguous (`test/modules/test_chan_scalar.vyb`).
  Non-identifier receivers now work too — a chan returned by a function
  (`make().send(x)`) or held as a struct field (`h.ch.recv()` / `h.ch.poll()`)
  resolves the same way as the named-variable path (`test/modules/test_chan_nonident.vyb`).
- [x] **Task spawn/await/poll (`tasks` module)** — policy-clean concurrency on
  the external pthread runtime: `task_spawn(fn() -> Int)` runs the closure on a
  detached worker whose result is delivered to a private capacity-1 channel; the
  handle *is* that channel, so `task_await` is a blocking recv, `task_poll` a
  non-blocking try (-1 until ready; also `-1` as a genuine result — and note a
  successful `await` empties the channel so a later `poll` reports -1), and
  `task_free` reclaims the handle. Fire-and-forget, no join/RAII obligation
  (`test/modules/test_tasks.vyb`). The pre-existing stub `AsyncRuntime`
  (`async_runtime.hpp/cpp` + `cgen_async.cpp`) was retired so concurrency lives
  on the external pthread runtime rather than a dead C++ executor.
- [x] **Threaded HTTP server (`http_serve`)** — the payoff of a worker-thread
  accept loop: `http_serve(port, backlog)` binds+listens, starts a detached
  worker running the accept loop, and serves each accepted connection
  concurrently on its own detached thread (`http_serve_conn` reads the head,
  extracts the path, and answers). The server keeps accepting while earlier
  connections are handled; closing the listen fd stops the loop. Uses the
  `threads` module's `thread_spawn` / `thread_detach` via a clean
  `import threads::{...}` (the module resolver now re-exposes plain-imported
  sibling symbols), keeping `http` free of raw pthread intrinsics.
  Covered by `test/modules/test_http_threaded.vyb`.

### 5. Sum Types / Enums (MEDIUM PRIORITY)
Vyb needs a way to express sum types. Essential for `Result<T,E>`, user data enums, and
expressive APIs. **Vyb-natural approach:** enums should integrate with the aspect system
and pattern matching, not be a separate OOP mechanism.

- [x] **Enum declaration syntax** — `enum Direction { North, South, East, West }` — variants are distinct typed values of the enum type (`Direction::North`), rendered `Direction::North`, with `match`/`select` dispatch and exhaustiveness
- [x] **Constant enums** — `enum Socket { AF_INET = 2, ... }` — a C-like enum
  whose variants carry explicit `= <int>` values becomes a scoped namespace of
  compile-time `Int` constants: `Socket::AF_INET` is `Int` 2 and flows straight
  into `Int` parameters/arithmetic (no call, no cast). Value-less C-like enums
  keep their existing nominal typed-value behavior. Replaces the clumsy
  `AF_INET()<Int> { return 2 }` constant-function idiom in `network` (the new
  `Socket` const enum) and `http` (`HttpSock`); parser (`= value` on variants),
  semantic (members typed `Int`), and codegen (explicit i64 value) only —
  `test/enum/test_const_enum.vyb`.
- [x] **Enum variants with data** — `enum Shape { Circle(Float), Rect(Float, Float) }` and generic `enum Box<T> { Value(T), Empty }`: a value-semantics `{ i64 tag, [N x i8] data }` representation is built in codegen, monomorphized per concrete type for generic enums (`Box<Int>::Value(x)` with explicit type args)
- [x] **Pattern matching on enums (match/select)** — In `match`, enum variant patterns (`Circle(r) ->`) dispatch on the runtime tag and bind payload fields; unit variants match bare (`Unit ->`), and a match must be exhaustive. `select` mirrors this, dispatching on variants and enforcing exhaustiveness the same way.
- [x] **Enum methods via `bind`** — `bind Drawable -> Shape { ... }` — an aspect `bind` can target a user-defined enum (concrete or generic, e.g. `bind Render -> Box<Int>`), and the built-in generic enum `Result<T,E>`; methods dispatch on the concrete variant with substituted payloads
- [x] **`Option<T>` (removed)** — the `Some`/`None` enum was superseded by the native `T?` optional and removed from the compiler; `Result<T,E>` (below) is the built-in generic data enum
- [x] **`Result<T,E>` ergonomics — bare `Ok(e)`/`Err(e)` as subexpressions** — bare constructors now also infer via expected-type propagation from the callee's parameter type in call arguments (e.g. `classify(Ok(11))`) and from the container element type for `Vec<T>.push` on a `Vec<Result<Int, String>>` (`v.push(Ok(x))`), in addition to annotated variable declarations and returns
- [x] **`Result<T, E>` as built-in enum** — `Ok(T)` / `Err(E)`; registered in the compiler (no `import`), constructible via `Result<Int, String>::Ok(x)` / `::Err(e)` and type-inferred bare `Ok(x)` / `Err(e)`, with match/select dispatch and exhaustiveness

### 6. Introspection System — Completion (MEDIUM PRIORITY)
- [x] `typeof(expr)` — Returns type hash as i64
- [x] `typename(expr)` — Returns type name as String
- [x] **`as` downcasting operator** — `value as TargetType`: lexed/parsed as an infix expression (`parse_cast_expr`), typed as the target type, and code-gen'd as a safe downcast. In a wildcard trap (`e<?>`) it extracts the concrete payload from the error struct (e.g. `g<GErr> = e as GErr`), and same-type casts pass through; incompatible casts are a semantic error. Also supports **integer widening/narrowing/reinterpretation** between the sized `Int`/`UInt` types: the source's signedness drives the extension (`UInt8 as Int64` zero-extends, `Int8 as Int64` sign-extends), narrowing truncates, and equal-width signedness changes are bit-preserving. This enables packing bytes into wider ints, e.g. composing an `Int64` from eight `UInt8` via `(b as Int64) | ((b1 as Int64) << 8) | ...`. (Phase 2 — see also `typeof`/type-context items below.)
  Assignment follows the same rule: a variable/field that changes integer width or
  signedness must do so through `as` — only a compile-time constant that fits the
  target range is implicit (`x<Int8> = 3`), while a typed value or an out-of-range
  constant is a compile error.
- [x] **`typeof` / `typename` in wildcard trap** — `trap (e<?>) -> { if (typeof(e) == typeof<ParseError>()) }`: on a wildcard error operand, `typeof(e)` loads the error's runtime type ID and `typename(e)` its type-name string from the error struct (so handlers can discriminate failed errors by type). Also adds the `typeof<T>()` compile-time type-hash form and recognizes `typeof`/`typename` as expression-statement starts.
- [x] **Type registry at startup** — `__vyb_module_init()` registers all types (primitives + user structs) keyed by type-ID hash, so `typename(t)` on a runtime `Type` value resolves the actual type name via `__vyb_get_typename`
- [x] **`Type` as first-class type** — `t<Type> = typeof(42)`, equality comparison; `Type` values flow through functions and only `==`/`!=` are allowed (other operators reported as errors)

### 7. Select Expressions — Polish (MEDIUM PRIORITY)
The `select` expression is a uniquely Vyb concept: pattern matching that produces a value,
with `pass` for multi-statement case bodies. Needs polishing:

- [x] **`select` exhaustiveness** — A `select` on a tagged-union enum must cover every variant or have a wildcard, or it is rejected with the missing variant(s) named
- [x] **Nested `select`** — `select` inside a `select` arm — a `select` used as
  an arm body (naked or block-with-`pass`) now compiles and runs; previously the
  enclosing select's type-inference preview ran the inner select's full block
  machinery, leaving dangling unterminated `select.end`/`select.case` blocks
  that failed LLVM verification (fixed in `cgen_expr.cpp`)
- [x] **`select` with enum variants** — Full destructuring in arms (`Circle(r) ->`, `Unit ->`) with payload fields bound as arm-scoped locals
- [x] **`select` as statement** — `select` may be used without a binding target
  (side-effects only): recognized as an expression/statement start in the parser,
  and block arms without `pass` now branch to the select end block instead of
  leaving an unterminated case block

### 8. Wildcard / Multi-Type Trap Handler (MEDIUM PRIORITY)
- [x] **`trap (e<?>)` syntax** — Catch any error type
- [x] **`typeof(e)` in wildcard handler** — Runtime type discrimination
- [x] **Multi-type trap** — `trap (e<ParseError | IOError>) -> { ... }` (Vyb-native syntax) catches an error of any listed type and binds `e` as an opaque error pointer resolved via `e as T` / `typeof(e)` / `typename(e)`

### 9. Advanced Control Flow (LOWER PRIORITY)
- [x] **`defer` statement** — `defer cleanup()` runs on scope exit (LIFO order, function-level)
- [x] **`ensure` statement** — `ensure condition else fail<Error>(...)` (post-condition) — desugars to `if (cond) {} else { handling }`; handling may be a block, a single statement (`return -1`), or `fail<T>(...)`
- [x] **Labeled `break`/`continue`** — `label: for/while` marks a loop; `break
  label` exits and `continue label` re-enters that specific enclosing loop,
  while unlabeled `break`/`continue` still target the innermost loop. Parser
  (`IDENTIFIER :` before a loop), AST (`For`/`While` carry `label`;
  `Break`/`Continue` carry an optional target), and codegen resolves the target
  against the loop stack (`test/new_features/test_labeled_break_continue.vyb`).

### 10. Async System — Completion (LOWER PRIORITY)
- [x] **Real event-loop executor — multi-threaded thread pool** — the `asyncs`
  module runs `fn() -> Int` closures as **stackful fibers** (ucontext), each on
  its own 1 MiB stack, driven by a **pool of worker threads** (lazily spawned on
  first use, sized to the CPU count) each running a FIFO ready queue + the shared
  sorted timer heap. A fresh task's context is built lazily by its worker, and
  once a fiber has run on a worker it is pinned there (ucontext is not portable
  across OS threads): spawn hands work to workers round-robin to load balance, and
  suspended tasks requeue to their own worker. Because the fibers are stackful, a
  fn() can suspend **mid-body** without a state-machine transform:
  `async_sleep_ms` (a timer, not a thread sleep), `async_yield` (round-robin), and
  `async_await` (block until a task completes) all reschedule onto the pool, so
  concurrent timers complete in ~max rather than ~sum wall
  (`test/modules/test_async.vyb`) and CPU-bound tasks run across cores
  (`test/async/async_multicore.vyb`: 4x120ms tasks finish in ~120ms, not ~480ms,
  under valgrind-clean with no leaks). Tasks stay valid across main-thread awaits;
  `async_run_all` waits for the pool to go idle, then flushes + reclaims
  everything, and an atexit hook stops/joins the workers and reclaims leftovers.
  **Vyb-level `async`/`await` syntax codegen — Stage 1/2/3 done**: an
  `async fn(params...)<Future<T>>` (T = Int, String, Void) compiles into a public
  launcher (returns the Future struct by value, spawning the body as an event-loop
  fiber) plus a hidden worker `$__async_body`; parameterized calls snapshot scalar
  args into a closure env via an `$__async_entry` trampoline, and a `String` result
  travels back as a heap slot `await` hands to the consumer as an owned transfer.
  `await` works from `main` (parks the caller, signaled by a condvar) and from
  inside a task (suspends the fiber), including nested `await` of a child task and
  the bare `await f` statement form. **The multi-threaded executor is done** (Stage
  4): workers idle on condition variables with the min timer deadline, wake each
  other when they enqueue work, and a main-thread await blocks on a condvar until
  the worker delivers. **`Float`/`Bool` futures done** (Stage 5): a `Float` result
  is passed as its f64 bit pattern and a `Bool` as 0/1 in the int64 task slot and
  decoded by `await` (bitcast / zero-extend+truncate), covering both the main await
  and the fiber-suspend (nested `await` from inside a task) paths
  (`test/async/async_float_bool.vyb`). **`String` async params done** (Stage 6): a
  `String` argument is snapshotted into the task env with the buffer retained (+1)
  and released by the env's per-layout dtor on cleanup, so it safely outlives the
  caller's scope while the worker runs on the pool (two-String envs and nested
  `await` of a String param both covered by `test/async/async_string_param.vyb`,
  valgrind-clean with no leaks). **`Vec<T>` async params done** (Stage 7): a `Vec`
  argument is deep-copied into the env so the task owns an independent copy, and
  the env dtor reclaims that storage (releasing `Vec<String>` element references)
  on cleanup; `Vec<Int>` / `Vec<String>`, nested `await` of a Vec param, and a
  mixed scalar + String + Vec env are covered by `test/async/async_vec_param.vyb`,
  valgrind-clean. **`our<T>` async params done** (Stage 8): the env snapshots the
  shared control-block pointer with an atomic strong-count retain (+1) and its
  dtor releases it on cleanup, so the shared object outlives the caller's scope
  and the caller's binding stays valid afterwards; passing the `our<T>` on to a
  child task via nested await is covered by `test/async/async_our_param.vyb`,
  valgrind-clean. **Richer await chains done** (Stage 9): the semantic analyzer now
  types an `await` expression as the awaited `Future<T>`'s inner `T`, so `await`
  results compose as values anywhere an expression does — as a method receiver,
  inside arithmetic (e.g. `await mk(2) + await mk(3)`), as a nested call argument
  (`sv(await mk(5))`), as a comparison, and on a bound future awaited later
  (`test/async/async_await_chains.vyb`). **`struct` / owned struct params done**
  (Stage 10): the launcher deep-copies a struct-typed argument into the env as an
  independent snapshot — retaining String buffers and `our`/`mild` control-block
  refs, cloning Vec buffers, and recursing into `my<Struct>` blocks and nested
  structs (mirroring `reclaimStructOwnedFieldsAt` so the env dtor's reclaim of that
  copy balances it) — and the worker merely borrows it, keeping the caller's
  binding valid across nested awaits (`test/async/async_struct_param.vyb`,
  valgrind-clean). **`fn`/closure params done** (Stage 11): the launcher
  snapshots a closure-typed param into the env, retaining its capture
  environment (+1) so it survives asynchronously and is invokable from the
  worker; the env's per-layout dtor releases that reference on task cleanup.
  Covers closures with captures (Int + String), nested `await` of a child task
  sharing the closure, and mixing with String/scalar params
  (`test/async/async_closure_param.vyb`, valgrind-clean). See
  `test/async/async_event_loop.vyb`, `async_params.vyb`, `async_nested_await.vyb`,
  `async_string.vyb`, `async_void.vyb`, `async_multicore.vyb`,
  `async_float_bool.vyb`, `async_string_param.vyb`, `async_vec_param.vyb`,
  `async_our_param.vyb`, `async_struct_param.vyb`, `async_closure_param.vyb`,
  and `async_await_chains.vyb`.
- [x] **`spawn` for concurrent tasks** — two storylines: the pthread `tasks`
  module (`t = task_spawn(fn() -> Int)`, `task_await`/`task_poll`/`task_free`)
  for real parallel workers, and the cooperative `asyncs` module above for
  non-blocking, event-loop concurrency.
- [x] **Typed channels** — `chan<T>` is a built-in generic typed channel for
  message passing between tasks (send/recv/poll/len/handle/free; test above).
- [ ] **Agents** — Lightweight isolated message-passing units (planned)
- [x] **`select` over channels** — `chan_select(handles<Vec<Int>>)` waits on many
  channels at once and returns the ready index (Vyb-natural extension; blocks
  with ~1ms wakeup, does not consume).
- [x] **Async lambdas** — `async |x| -> await process(x)` (Stage 12: the body is
  compiled as a closure running as a cooperative task; a call returns a
  `Future<T>` that `await` drives. Covers captures, String / zero-arg forms,
  and passing an async lambda as a future-returning closure param;
  `test/async/async_lambda.vyb`, valgrind-clean).
- [x] **`async for`** — Iterate over async streams (drains a `chan<T>` /
  `strchan` as an async stream via lossless `recv_opt` + `close`;
  `test/async/async_for_chan.vyb`)
- [x] **Async I/O (event-loop sockets)** — non-blocking `async_accept` /
  `async_connect` / `async_send` / `async_recv` suspend the calling fiber (via
  a background poll pump that watches every waiting fd) instead of blocking a
  worker. Echo + 3-way concurrent echo on one listener:
  `test/async/async_io_echo.vyb` / `test/async/async_io_multi.vyb`

---

## Developer Experience — Needed for 1.0

### Package Manager (core DONE — remote deps + registry staged)
- [x] **`vyb.toml`** — Project manifest with `[package]`, `[dependencies]`, `[[bin]]`
- [x] **`vyb build`** — Build multi-file projects from manifest (reuses the module registry + native compile/link pipeline; local `{ path = ... }` dependencies resolve to module search paths)
- [x] **`vyb new`** — Scaffold a new Vyb project (`vyb.toml` + `src/main.vyb`)
- [x] **Lock file** — `vyb.lock` written for resolved local path dependencies
- [ ] **Remote dependency resolution** — version/git dependency fetching + registry-gated lock pins (path deps are resolved today)
- [ ] **Package registry** — Central registry for published packages

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
- [ ] **Web landing page** — `docs/web/landing_page/index.html` is the deployed
  project site (see `vyb-lang.org` / `aniviza.com`). Update it periodically as
  needed to reflect the current feature set and project status — ad hoc, no
  fixed schedule.
- [x] **Hyperlinked refman generator** — `tools/refman.py` scans `stdlib/**/*.vyb`
  and emits `docs/refman/` (module pages + cross-indexes + `graph.json`) as an
  inter-relationship graph (import, implement, uses-type, runtime-ref, prose-ref,
  call edges), with `--check` drift/validation. Design in `docs/refman/PLAN.md`.
- [x] **Refman polish (part 1)** — per-file sections on multi-file module pages
  (`core`, `error`), module + per-symbol fan-in/fan-out views, and a CI
  `--check` target (`.github/workflows/refman-check.yml`).
- [x] **Refman polish (part 2)** — cross-module type-consumer view
  (`docs/refman/interfaces.md`), fuller `graph.json` nodes (module/file/symbol
  records), and a hand-written prose intro for the thin `stdlib/prelude.vyb`
  header.

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
- [x] **Doc consolidation** — `doc/` had overlapping files (`ROADMAP.md`, `TODO_CURRENT.md`, multiple ownership docs). Keep `TODO.md`, `doc/FEATURE_STATUS.md`, `CHANGELOG.md` as living docs; the archived `doc/archive/` subtree has been removed (history lives in git); `doc/README.md` is the index.
- [ ] **Legacy example modernization** — Remaining design/roadmap docs
  (`OWNERSHIP_MILD.md`, `MODULE_FFI_BINARY_ROADMAP.md`, `LAMBDAS.md`,
  `Intrinsics.md`, ...) still show legacy example syntax that interleaves with
  not-yet-shipped features: `fn name(...) -> Type { }` defs, `if let`, and
  `name: Type` fields/params. Unlike the verifiable shorthand (`unsafe`, object
  literal `:` which is valid, and `<T<Aspect>>` which is already canonical),
  these need per-example porting against compiled Vyb (e.g. `grab()` is used
  directly, not via `if let`). Suggested as a compiler-verified follow-up.

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

There are no `TryStatement` / `ThrowStatement` AST nodes. These are vestigial C++
vocabulary and have no place in Vyb. `fail`/`trap` provides zero-cost success path,
typed errors, and explicit propagation — the Vyb way.

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
    next(self<their<Self>>)<Self::Item?>         # present payload, absent when exhausted
}
```
Types that `bind Iterator` become usable in `for` loops. The compiler desugars
`for (item in col)` to repeated `Iterator::next()` calls. Depends on associated types
being implemented in the aspect system first (tracked separately).

### [DECIDED] Channels / Agents — Channels Shipped; Agents in Design

**Decision:** Typed `chan<T>`/`strchan` channels are shipped (typed
send/recv/poll/len, `select` over handles, `channel close`, and `async for`
streams over channels). The agent model (lightweight isolated message-passing
units) sits on top of them: a built-in generic `agent<M>` handle owning a
mailbox + behavior task + lifecycle. Brought forward from post-1.0 for design
first — see `doc/AGENTS_DESIGN.md` (draft); implementation staging (mailbox +
Int payloads, payload breadth, request/reply, failure channeling, backpressure)
is proposed there.

### [DECIDED] Dynamic Dispatch (`dyn Aspect`) — Marked as Future Experiment

**Decision:** Vyb stays fully static. No `dyn Aspect`, no vtables, no runtime
polymorphism — consistent with the sealed `doc/MONOMORPHIZATION_DESIGN.md`.

Static (compile-time) monomorphization is the core dispatch model: zero runtime
overhead, exact value types preserved, one dispatch model, and a clean fit with the
compile-time ownership concepts (`my`/`our`/`their`). The erased-polymorphism use
cases that would justify vtables — heterogeneous aspect-typed collections
(`Vec<dyn Display>`) and implementers unknown at compile time (plugins / ABI-stable
boundaries) — are not part of the 1.0 scope. Function pointers / closures across the
C FFI remain the supported escape hatch for callback-style interfaces.

`dyn Aspect` is intentionally parked as a **future experiment**, not part of the 1.0
checklist. If a concrete use case emerges, it requires a new design document and an
explicit change to the sealed monomorphization design before any implementation.

---

## Vyb-Native Ideas Worth Exploring

These are not in any current design document but feel natural given Vyb's identity and
should be prototyped or at least documented before 1.0:

### `Vec()` Constructor Idiom (`Vec::new()` → `Vec(n)`)
**Implemented (0.5.3).** `Vec::new()` read like an OOP static constructor (Rust/Java) and
didn't fit Vyb's type-as-constructor feel. Now `Vec()` and `Vec(n)` build an empty or
n-element zero-initialized growable vector, with the element type inferred from the
variable annotation:
```vyb
a<Vec<Int>> = Vec()          # empty, growable
b<Vec<Int>> = Vec(16)        # preallocate / zero-init 16 elements (and capacity)
c<Vec<String>> = Vec()       # element type inferred from the annotation
```
`Vec::new()` / `Vec::new(n)` remain as a back-compat alias. Explicit generic-typed
constructor calls like `Vec<String>()` are not yet parseable (they'd need generic
function-call support on the builtin type) and are a possible follow-up.
**Open question:** a "constant size / non-growable" flag. Recommendation is to NOT overload
`Vec` with fixed-size semantics — Vyb already has fixed arrays, and a non-growable
collection is a distinct type. Keep `Vec` growable, treating `Vec(n)` as preallocation
headroom rather than a length cap; introduce `VecFixed(n)` later only if it earns its keep.

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
**Implemented (0.5.3).** `ensure cond else handling` runs `handling` when `cond` is false,
desugaring to `if (cond) { } else { handling }` (see `test/units/test_ensure_contract.vyb`).
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

### Promote the Native `T?` Optional Over the Rust-style `Option<T>` / `Some` / `None`
**Status: foundation + channel `poll()` + map `get` + iterator `next()` migrated.**
`T?(v)` / `T?()` construction, `optional else default`, scalar/`String`/`Float`
payloads, return/parameter/chained-default paths (see
`test/new_features/test_native_optional.vyb`), scalar `chan<T>.poll()` (via
`poll() else default`; String poll keeps its empty-String sentinel),
`HashMap.get`/`BTreeMap.get` reading `m.get(key) else default`, and every
iterator `next()` returning the native `Item?` (see
`test/modules/test_chan_{typed,scalar,nonident}.vyb`,
`test/modules/test_collections_{hashmap,btreemap,growth}.vyb`,
`test/modules/test_struct_constructors.vyb`, `test_vec_iter.vyb`,
`test_iterator_protocol.vyb`). `match`/`select` gained the optional surface:
present arm binds the bare value (`v -> ...`), `?` is the absent arm, with
exhaustiveness; the `for (x in it)` desugar emits that native-optional match
(including the step form). Generic `T?` substitutes through binds, and the `else`
operator tolerates an unresolved generic payload type (codegen enforces the
concrete payload).
`Option<T>`/`Some`/`None` have been fully replaced by the native `T?` surface in
every call site (chan `poll()`, map `.get()`, iterator `next()`, and `mild<T>.grab()`),
and the Rust-shaped `Option<T>` enum is now REMOVED from the compiler entirely (including
the `core::option` bridge and its prelude imports). `Some(x)`/`None` no longer compile;
the native `T?` (`T?(v)`/`T?()` + `else`/match-`?`) is the one optional surface. The
built-in `Result<T,E>` (`Ok`/`Err`) enum and the ownership/enum-matching code shared with
`Option` are preserved.
**RESOLVED — `T?` promoted over `Option<T>`.** The `Option<T>` / `Some(v)` / `None`
vocabulary came from Rust (via Haskell's `Maybe`). Vyb already had a native optional type
in the compiler — `<Type>?` parses to `ast::OptionalType`, lowered to a
`{ bool hasValue, value }` struct in codegen (`src/parser/type_parser.cpp`,
`src/vre/llvm/cgen_types.cpp`) — but the stdlib and call sites grew the Rust-shaped
`Option<T>` enum instead. The vybey move was to promote `T?` (already Vyb's own syntax,
mirroring the `?` wildcard) and drop `Some`/`None`, not to rename the enum — carried out
above: every `Option<T>` call site now consumes the native `T?`, and the `core::option`
bridge is gone.
The design must cover every current `Option<T>` use, not just channels:
- **Readiness** — `chan<T>.poll()` -> `Option<T>` (`Some(v)`/`None` scalars, empty-String
  sentinel; `test/modules/test_chan_scalar.vyb`, `test_chan_nonident.vyb`). Readiness is a
  *flag*, best served by `?`/`else`, not a wrapped enum.
- **Existence / lookup** — DONE: `HashMap.get(key)` / `BTreeMap.get(key)` now return the
  native `V?`, read as `m.get("k") else fallback` (`stdlib/collections/mod.vyb`,
  `test_collections_hashmap.vyb`).
- **Sequence exhaustion** — DONE: every iterator's `next()` now returns the native
  `Item?` (present bare payload / absent): `VecIter`, `MapIter`, `HashIter`, `BTreeIter`
  (`stdlib/collections/mod.vyb`, `stdlib/core/iter.vyb`). End-of-stream matches the
  optional absent arm (`? -> break`) in both the explicit `.next()` loop and the
  `for (x in it)` desugar.
- **Failed weak upgrade** — DONE: `mild<T>.grab()` now returns the native `our<T>?`
  (present holder while live / absent once released) (`doc/FEATURE_STATUS.md` Ownership row).
  A distinct "already released" outcome, matched via `o ->` / `? ->` or `else`.
Common consuming surface (vybey = sentence-like, keyword-first):
- default with Vyb's existing `else` (as in `ensure cond else handling`):
  `v<Int> = ch.poll() else 0` ; `found<Int> = m.get("k") else fallback`.
- explicit branch via `select`/`match`: present arm is the bare value
  (`v<Int> ->`), absence is the existing `?` wildcard (`? ->`).
- nothing is `Void` / the `?` path — no `Some(v)` constructor ceremony, and no
  `.unwrap()`/`.expect()` method chain.
Keep Vyb's exhaustiveness guarantee (no silent `null`/`undefined` escape hatch). The
native `T?` (`OptionalType`) is now the canonical consumed surface for chan `poll()`,
map `.get()`, iterator `next()`, and `mild.grab()`, superseding the `Option<T>` enum.

---

## 1.0 Release Criteria

For Vyb to be considered production-ready at 1.0, **all of the following must be true**:

### Must-Have for 1.0
- [x] Module system core working (`import`, `smuggle`, `bundle`, `share`, module paths, stdlib discovery)
- [x] Lambda/closure codegen complete (env structs, mutable/move/`our` capture, returned-closure release)
- [x] Ownership types runtime-enforced (borrow checking, move semantics, `mild<T>` weak refs)
- [x] Minimal `mild<T>` control block implemented with `soft()`, `grab()`, and `released()`
- [x] Error propagation (Phases 2-5) complete
- [x] `Result<T, E>` built-in enum (and native `T?` optional; the `Option<T>` enum is removed)
- [x] Core aspects (`Display`, `Debug`, `Clone`, `Equatable`, `Comparable`, `Hashable`)
- [x] Iterator aspect with `for` loop desugaring (identifier, range, and non-identifier iterable forms)
- [x] Enum/sum types with pattern matching (data variants, exhaustiveness, guards, destructuring, `match`-as-expression)
- [x] **Ample Vyb-native examples** — a maintained corpus of idiomatic programs
  in `examples/idiomatic/` (see the table in `examples/README.md`) showcasing
  unique *vybey* patterns: ownership keywords, aspect/bind, `fail`/`trap`/
  `ensure`, native `T?`, enums + `match`/`select`, pure-Vyb collection
  combinators, JSON round trip, bit-packing casts, `chan<T>` concurrency, and
  `async`/`Future<T>`/`await`, not just raw stdlib capability. Demonstrates that
  Vyb syntax reads naturally before 1.0.
- [x] String methods complete (`split` and formatting done; see `.split()`/`.format()`)
- [x] `HashMap<K, V>` and basic collections (HashMap/HashSet/BTreeMap, `Vec` iterators, growth)
- [x] FFI (`extern "C"`) working — extern blocks, ABI aliases, `#[repr(C)]`, native `--link`, variadics, `vyb bindgen` (MVP + libclang `--full`)
- [x] `vyb.toml` and `vyb build` project system (foundation shipped: manifest, multi-file/path-dep build, `vyb new`, `vyb.lock`; remote git/version dependency fetching is a staged follow-up)
- [x] Wildcard trap handler (`trap (e<?>)`) with `typeof` discrimination
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
- [ ] Agents (lightweight isolated message-passing units — design doc first; channels are shipped)
- [x] **Network/socket MVP** — synchronous TCP/IP sockets shipped via the `network`
  stdlib module (`socket_open/bind/listen/accept/connect/send/recv/local_port/close`,
  `AF_INET`/`SOCK_STREAM`/`IPPROTO_TCP`; loopback echo
  `test/modules/test_network_socket.vyb`) over `__vyb_net_*` runtime intrinsics.
  Higher-level (async, streams, UDP, TLS) networking remains post-1.0 (section below).
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

A synchronous TCP/IP socket MVP is now shipped as a thin standard-library wrapper over
the runtime's `__vyb_net_*` intrinsics (`import network`; `test/modules/test_network_socket.vyb`),
which themselves rely on POSIX sockets via FFI. The notes below cover the future,
higher-level async/streaming/UDP/TLS networking layers.

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
- [x] **v0.6 — `TcpStream` / `UdpSocket`** — method-bound wrappers in `network`
  (`tcp_connect`/`tcp_listen`/`tcp_accept`/`udp_bind` + `.write`/`.read`/
  `.send_to`/`.recv_from`/`.close`); errors via the module's `error_code()`
  idiom, plus `async_tcp_*` / `async_udp_*` variants on the event loop
- [x] **v0.6 — `TcpListener`** — accept loop via `tcp_listen`/`tcp_accept`
  (blocking) and `async_tcp_accept` (event-loop fiber), integrated with the
  async runtime
- [x] **v0.6 — Async I/O** — Non-blocking socket I/O on the real executor
  (`asyncs::async_accept`/`async_connect`/`async_send`/`async_recv` suspend the
  fiber via the poll pump; no worker is ever blocked)
- [x] **v0.7 — HTTP/1.1 client** — `http_get_full()` returns a parsed
  `HttpResponse` (status/reason/headers/body) over a pure-Vyb socket round-trip,
  honoring `Content-Length` and falling back to read-until-close
- [x] **TLS (`tls` stdlib module) + HTTPS client (`https`)** — OpenSSL loaded
  and linked into the binary (dlopen'd into global scope so the ORC JIT
  resolves libssl/libcrypto), with runtime `__vyb_tls_*` shims: client/server
  `SSL_CTX` from in-memory PEM, `SSL` over an existing fd, handshake, encrypted
  read/write, close, and diagnostics. `import tls` exposes `TlsContext`/
  `TlsStream` (with `tls_client_context`, `tls_server_context`, `tls_stream`,
  `tls_connect`, `tls_accept`, `tls_write`, `tls_read`, `tls_close`,
  `tls_error_code`/`tls_error_message`); `import https` builds a TLS-secured
  HTTP client on tls + http (`https_get`/`https_get_full`, plus a
  `https_selfhost` diagnostic). Peer verification is a first-class surface:
  `tls_client_context_verified(ca_pem)` trusts an in-line-pinned CA (or the
  system default CA paths when `ca_pem` is "") and checks the expected hostname
  in the certificate, while `tls_client_context()` stays verification-free for
  self-signed loopback. `network::socket_resolve` adds hostname->IPv4
  resolution, and `https_get_full_verified` connects to the resolved IP while
  verifying the name - so real pinned/system-CA HTTPS to named hosts works.
  Covered by `test/tls/test_tls_loopback.vyb`,
  `test/tls/test_tls_verified.vyb`, `test/tls/test_https_client.vyb`,
  `test/tls/test_https_verified.vyb`, and
  `test/tls/smoke_openssl.vyb` (valgrind-clean for the TLS path).
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

*Last Updated: 2026-08-19 (v0.7.3 release)*
*Current Version: Vyb v0.7.3 (freedom-1.0 series)*
*Overall Status: ~60-65% complete toward 1.0 — 1077 tests, 1077 passing (full --execute-jit directory sweep)*
*SUGGESTIONS.md merged into this document.*
