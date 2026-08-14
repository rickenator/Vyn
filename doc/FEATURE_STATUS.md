# Vyb Feature Status (v0.5.4)

This document tracks the implementation status of Vyb language features.

Legend: ✅ Implemented | 🚧 Partial / Stubbed | 📋 Planned

---

## Module System

| Feature | Status | Notes |
|---------|--------|-------|
| `import <path>` | ✅ | Parses module path and resolves local `.vyb` files (`::` or `.` separated) |
| `import <path> as <alias>` | 🚧 | Whole-module alias parses; namespace binding still planned |
| `import <path>::{symbol as alias}` | ✅ | Selective import specifiers filter and rename imported declarations |
| `import <path> from "<locator>"` | ✅ | Locator string parsed and stored in AST |
| `smuggle <path> from "<locator>"` | ✅ | Locator string parsed and stored in AST |
| `smuggle <path> as <alias>` | ✅ | Alias binding at parse level |
| `ImportKind` (TrustedImport / Smuggle) | ✅ | Captured in AST `ImportDeclaration.kind` |
| `from` keyword | ✅ | Lexed as `KEYWORD_FROM`; also valid in `from<T>(addr)` freedom-block expressions |
| Module resolution (load files) | ✅ | Resolved via `ModuleRegistry` metadata model (canonical keys + resolution states) |
| Local path loading (`from "./..."`) | ✅ | Relative locators resolve from the importing file |
| Module search paths | ✅ | Importer dir, `--module-path`, `VYB_MODULE_PATH`, then stdlib auto-discovery |
| Stdlib auto-discovery | ✅ | `VYB_STDLIB`, then executable-relative probes (`../stdlib`, `./stdlib`) |
| Stdlib module foundation (`core`/`collections`/`io`) | 🚧 | Foundation + `core::math` helpers shipped. `core::aspects` pre-wires scalar binds; `core::math` layers `clamp`/`is_close` over the global math intrinsics. `io`/`collections` surfaces are documented and importable; deeper contents (File/network I/O, `HashMap`/`HashSet`, higher-order `Vec<T>`, `Iterator`) track their compiler-feature items. `Option<T>`/`Result<T,E>` are built-in enums (transitional `core::option`/`core::result` retained for source-compat) |
| Stdlib core auto-import | ✅ | The core contracts module (`core::aspects`, incl. pre-wired primitive binds) is auto-imported into non-stdlib modules, skipped on explicit import / local redefinition, opt-out via `no_core()` directive. Transitional prelude helpers (`OptionInt`, `prelude_ok`) remain explicit-only (`import prelude` / `import core::prelude`) |
| `bundle(...)` visibility | ✅ | Source-level directives are enforced by the local resolver |
| `share(...)` exports | ✅ | `share(all)` and bundle-scoped shares export declarations/imports |
| `smuggle` visibility bypass | ✅ | Smuggled imports bypass share/bundle checks |
| URL/Git fetching (`from "github.com/..."`) | 📋 | v0.6.x |
| Module cycle detection | ✅ | Circular imports are rejected with dependency-chain diagnostics |
| Symbol re-export | ✅ | `share(...)` before an import re-exports selected imported declarations |

## Println / Output

| Feature | Status | Notes |
|---------|--------|-------|
| `println(x)` for string types | ✅ | Extracts `char*` from String struct |
| `println(x)` for Int | ✅ | Auto-converts via `__vyb_int_to_string` |
| `println(x)` for Float | ✅ | Auto-converts via `__vyb_float_to_string` |
| `println(x)` for Bool | ✅ | Auto-converts via `__vyb_bool_to_string` |
| `println(x)` for Vec/arrays | ✅ | Array serialization |
| `println(x)` for structs | ✅ | JSON/generic serialization |
| `print(x)` (no newline) | ✅ | Same auto-stringify as println |
| `println(a, b, c, ...)` multiple args | ✅ | Space-separated; all args formatted into one output call |
| `println_int()` / `println_bool()` variants | 🚧 | Still present for compatibility; prefer `println()` |

## String Concatenation

| Feature | Status | Notes |
|---------|--------|-------|
| `String + String` | ✅ | Direct struct concat |
| `String + Int` | ✅ | Auto-coercion via `generateMixedStringConcatenation` |
| `String + Float` | ✅ | Auto-coercion |
| `String + Bool` | ✅ | Auto-coercion |
| `Int + String` | ✅ | Auto-coercion |
| String literal `+` non-string | ✅ | String struct detection handles this case |

## Core Language

| Feature | Status | Notes |
|---------|--------|-------|
| Functions (name-first syntax) | ✅ | |
| Structs | ✅ | |
| Enums | ✅ | C-like enums (`enum Color { Red, Green, Blue }`) are first-class typed values backed by a single scalar `i64` tag: `r<Color> = Color::Red`, `println(r)` renders `Color::Red`, and `match`/`select` dispatch on named variants with the same exhaustiveness as data enums. Every enum value also exposes its raw positional variant tag as an `Int` via `.tag` (both C-like scalar and data-carrying enums, `test/units/test_enum_tag.vyb`). Because the value is a scalar `i64`, an extern parameter typed `Color` interoperates with a C integer-backed enum (registered by-value FFI, e.g. `labs(Color::Green)` → `1`). Data-carrying (tagged-union) enums like `enum Shape { Circle(Float), Rect(Float, Float), Unit }` and generic `enum Box<T> { Value(T), Empty }` compile to a value-semantics `{ i64 tag, [N x i8] data }` union, construct via `Shape::Variant(args)` / `Box<Int>::Value(x)` (generic enums monomorphize per concrete type with substituted payloads), and match on / `select` on variants (`Circle(r) ->`, `Unit ->`) dispatch on the runtime tag and bind payload fields. Both `match` and `select` require exhaustiveness (every variant or a wildcard); an exhaustive match's no-match default is unreachable so an all-return final-statement match compiles cleanly. `Option<T>` is now a built-in enum: constructible via `Option<Int>::Some(x)` / `Option<Int>::None` and type-inferred bare `Some(x)` / `None`, with the same match/select dispatch and exhaustiveness. `Result<T,E>` is likewise a built-in enum: `Ok(T)` / `Err(E)`, constructible via `Result<Int, String>::Ok(x)` / `::Err(e)` and type-inferred bare `Ok(x)` / `Err(e)`, with the same match/select dispatch and exhaustiveness. Aspect `bind` can attach methods to an enum target type (`bind HasArea -> Shape { ... }`) — including generic user enums (`bind Render -> Box<Int>`) and the built-in `Option<T>` / `Result<T,E>` (`bind Tag -> Option<Int>`), dispatching on the concrete variant with the substituted payload. Bare `Some(x)`/`None`/`Ok(e)`/`Err(e)` also work as call arguments via expected-type propagation (`unwrap(Some(7))`, `v.push(Some(x))` on a `Vec<Option<Int>>`). |
| Generics (monomorphization) | ✅ | Includes generic functions and current generic bind method executable monomorphization, plus call-site type-argument inference with return-type substitution and aspect-bound validation on generic function calls; failable generic functions now use the `{T, i8*}` error-return ABI so a monomorphized `fail` is caught by the caller's `trap` at runtime; explicit type-argument calls (`probe<Int>(0, 0)`) parse and monomorphize correctly; a generic function returning a generic struct/enum type (e.g. `make_box<T>(v)<Box<T>>`) resolves to the concrete `Box<Int>`/`Box<String>` for both explicit (`make_box<Int>(7)`) and inferred (`make_box("hi")`) type args, and distinct instantiations coexist in one program without sharing stale cached types (`test/units/test_generic_struct_return.vyb`); broader nested/member templates still need expansion |
| Aspect/Bind polymorphism | ✅ | Includes canonical simple receiver shorthand `method(self)<T>`, legacy/explicit `self<Self>`, ownership-qualified receivers, associated types (declarations, bind assignments, validation, defaults declared as `type Item = Int`, aspect bounds such as `type Item<Display>`, and resolution through generic binds — `bind<T> Iterator -> Boxer<T> { type Item = T }` resolves `Self::Item`/`T` to the concrete type at the call site), a bind method returning `Self::Item` resolving in both concrete and generic bind bodies, executable generic bind methods for current supported shapes, ambiguous dot-call diagnostics, qualified disambiguation (`Aspect::method(receiver)` selects a specific aspect when multiple bound aspects share a method name; bind symbols are emitted per `Type_Trait_Method`; also works on bounded type parameters inside generic functions), bind selection precedence (bounded generic bind over unbounded for the same shape), super-aspect inheritance (`aspect Sub : Super` with binding requirements and cycle detection), and unqualified bounded-type-parameter dispatch: `thing.show()` for `thing<T<Display>>` inside a generic function resolves through the bound aspect and substitutes `Self` in the return type, including inherited super-aspect methods (both unqualified `thing.name()` and qualified `Named::name(thing)` walk the bound's transitive super-aspect chain). A `core::aspects` stdlib module declares the six canonical contracts (`Display`, `Debug`, `Clone`, `Equatable`, `Hashable`, `Comparable` with `Comparable : Equatable`), re-exported via prelude, bindable to structs with unqualified dispatch and generic bounds and also bindable directly to primitive scalar targets (`Int`/`Float`/`Bool`/`Char`) with unqualified dispatch and generic bounds. `bind` declarations carry across module imports (visibility via `share`, dedup by `(target, aspect)`), so the `core::aspects` stdlib module ships pre-wired `Display`/`Clone`/`Equatable`/`Comparable`/`Hashable` implementations for `Int`/`Float`/`Bool`/`String` that are active on `import core::aspects` / `import core::prelude` |
| Ownership: `my`, `our`, `their`, `mild` | 🚧 | Lexical borrow enforcement, minimal `our<T>`/`mild<T>` control blocks, unwrap-on-read for primitive `my`/`our`/`mild<T>`, and compile-time move tracking for `my<T>` (use-after-move rejection plus transfer on assignment/init/arguments); full copy/drop semantics still planned |
| `freedom` blocks + `loc<T>` raw pointers | ✅ | |
| `match` / `select` expressions | ✅ | Literal, wildcard, comparison, struct-destructuring (`Point { x, y } ->` binds fields in the arm body; unknown fields and type mismatches rejected), inclusive range patterns (`1..10 ->`; inverted ranges rejected as never-matchable), guard clauses (`pattern if condition ->`, with access to destructured bindings), and `match` as a value-returning expression (`r = match (v) { 1..3 -> 10, ? -> 20 }` infers the result type from the first arm and stores the matched arm's value; block arms yield via `pass`) |
| `defer` | ✅ | |
| `fail` / `trap` error system | ✅ | Includes typed `fail<T>(value)`, typed traps, wildcard (`e<?>`) and multi-type (`e<Type1 | Type2>`) trap parsing, dual-return ABI `{T, i8*}` / `{i1, i8*}`, Phase 3 fail propagation returns, Phase 4 auto-propagating call-site checks, and Phase 5 untrapped runtime handler dispatch from failable `main`, and `ensure` contract statements (`ensure cond else handling`, desugaring to `if (cond) { } else { handling }`); trap context is function-local so `fail` in a callee (including inside an `if`/`else` branch or an `ensure`-`else`) propagates through the failable ABI and is caught by the caller's trap. Chained `} trap (e<Type>)` clauses dispatch first-type-compatible-wins; a multi-type union handler binds `e` as an opaque error pointer resolved via `e as T` / `typeof(e)` / `typename(e)`. A `trap` block used as a value (`s<String> = { risky() } trap (e<?>) -> { "hello" }`) infers its result type from the handler and sizes the result slot accordingly, so a String handler round-trips as a `{ ptr, len }` (not a hardcoded `i64`) |
| `async` / `await` | 🚧 | Runtime stub |
| `Vec<T>` | ✅ | |
| String methods | ✅ | `.len()`, `.contains()`, `.starts_with()`, `.ends_with()`, `.to_upper()`, `.to_lower()`, `.substring()`, `.char_at()`, `.trim()`, `.replace()` |
| Math intrinsics | ✅ | |
| `typeof` / `typename` | ✅ | Runtime type identity (`Type`) and type-name String; `typeof<T>()` compile-time type-hash form |
| `Type` (first-class identity) | ✅ | `t<Type> = typeof(42)` and `typeof<Int>()` declare/assign an opaque 8-byte type ID; `==` / `!=` compare type IDs; `Type` flows through function calls; other operators are rejected. A startup Type registry (`__vyb_module_init`) registers every known type name keyed by its hash, so `typename(t)` on a runtime `Type` value resolves the actual type name (`"Int"`, `"ParseError"`) via `__vyb_get_typename` |
| `typeof` / `typename` on wildcard trap errors | ✅ | In `trap (e<?>)`, `typeof(e)` extracts the error's runtime type ID and `typename(e)` its type-name string, so handlers can dispatch on failed-error type (`typeof(e) == typeof<ParseError>()`) |
| `as` (safe downcasting) | ✅ | `value as TargetType`, lexed/parsed as an infix expression and typed as the target type; in a wildcard trap (`e<?>`) it extracts the concrete payload from the error struct (`g<GErr> = e as GErr`) so the handler reads its fields; same-type casts pass through, incompatible casts are a semantic error |
| Templates | ✅ | |

## Vec<T>

| Feature | Status | Notes |
|---------|--------|-------|
| `Vec()` / `Vec(n)` | ✅ | Vybish constructor: empty, or `n` zero-initialized elements (legacy `Vec::new()` alias retained) |
| `Vec::push()` | ✅ | Append element |
| `Vec::pop()` | ✅ | Remove last element |
| `Vec::len()` | ✅ | Returns element count |
| `Vec::get()` | ✅ | Index access |
| `Vec::contains()` | ✅ | Fixed: now emits correct LLVM comparison loop (was hardcoded `false`) |
| `Vec::push()` on borrowed struct fields | ✅ | Fixed: `s.items.push(val)` where `s<their<T>>` now mutates in-place |
| Vec parameter deep copy | ✅ | Vec parameters receive an independent copy on function entry (fixes double-free in recursive algorithms) |
| `Vec::map()` / `filter()` / `reduce()` | 📋 | Requires lambda codegen + Iterator aspect |
| `for (item in vec)` iteration | ✅ | Compiler-generated loop with break/continue |

## Lambdas / Closures

| Feature | Status | Notes |
|---------|--------|-------|
| Lambda parsing `\|x, y\| -> expr` | ✅ | |
| Lambda parsing `\|x<Int>\| -> { block }` | ✅ | |
| Capture detection (semantic) | ✅ | |
| Type inference on lambda body | ✅ | |
| Indirect call from local variable | ✅ | `localLambdaTypes` map; return type coercion working |
| Full closure struct codegen | 📋 | Capture extraction + struct allocation |
| Move capture (`my<T>` into closure) | 📋 | |
| `our<T>` shared capture | 📋 | |

## Compiler Backend & FFI

| Feature | Status | Notes |
|---------|--------|-------|
| LLVM IR codegen | ✅ | |
| JIT execution | ✅ | |
| AOT native executable | ✅ | `--build` flag with repeatable `--link <lib-or-path>` linker inputs |
| Multi-file compilation | ✅ | ModuleRegistry resolves local imports, module paths, stdlib discovery, and dependency order |
| `extern "C"` FFI | ✅ | Extern blocks parse/codegen, freedom-gated calls, C ABI scalar/pointer aliases, conservative `#[repr(C)]` structs |
| Variadic C functions | ✅ | A trailing `...` marks an extern declaration variadic (isVarArg); call sites accept extra args and auto-extract Vyb `String` data pointers for `%s` (`test/ffi/variadic_c_printf.vyb`) |
| `vyb bindgen` (MVP) | ✅ | `vyb bindgen <header.h> [-o out.vyb]` parses a C subset and emits importable extern/`repr(C)`/enum bindings (`test/bindgen/*`). Full libclang-based parsing is future work. |

---

*Last updated: v0.5.4 (2026-08-14)*
