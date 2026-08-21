# Vyb Feature Status (v0.7.3)

This document tracks the implementation status of Vyb language features.

Legend: ✅ Implemented | 🚧 Partial / Stubbed | 📋 Planned

---

## Module System

| Feature | Status | Notes |
|---------|--------|-------|
| `import <path>` | ✅ | Parses module path and resolves local `.vyb` files (`::` or `.` separated) |
| `import <path> as <alias>` | ✅ | Whole-module namespace import: `import module as NS`, `import * as NS from "locator"`, and the `smuggle` equivalents bind the module's visible symbols under `NS` for qualified `NS.sym` access. The bare names stay out of the importer's unqualified scope (isolation), and carried symbols are mangled (`__ns_<NS>_<sym>`, non-user-writable) so exported functions keep working across transitive and re-export hops (`test/modules/namespace_*.vyb`) |
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
| Stdlib module foundation (`core`/`collections`/`io`) | 🚧 | Foundation + `core::math` helpers shipped. `core::aspects` pre-wires scalar binds; `core::math` layers `clamp`/`is_close` over the global math intrinsics. `collections` ships real `HashMap<K,V>`, `HashSet<K>` and the ordered `BTreeMap<K,V>` (`import collections`, by-ref bind methods; hash maps/sets use a hash-bucket (chained) key index built on `Vec`, and the `Comparable`-keyed `BTreeMap` keeps a sorted `keys` vector with binary-search `get`/`contains_key` and ascending-order `iter()` via `BTreeIter`; `test/modules/test_collections_hashmap.vyb` and `test/modules/test_collections_btreemap.vyb`). `io` ships real File I/O — `File { fd, path }`, `open` + `open_read`/`open_write`/`open_append`, `close`, `write_str`, `read_all`, `error_code`/`error_message`, and `FILE_*` mode helpers over the runtime `__vyb_file_*` intrinsics (`test/modules/test_file_io.vyb`). `core::iter` now ships the `Iterator` protocol — `aspect Iterator { type Item; next(self<their<Self>>)<Self::Item?> }`, explicitly imported, consumable via `.next()` / `match` loops (`test/modules/test_iterator_protocol.vyb`); a `for (item in <iter-expr>)` desugar drives any non-identifier iterable expression over `next()`, with an optional `skip`/step (`for (... , step)` yields indices 0, step, 2*step, ...); breaks/continues re-enter `next()`, and re-evaluating the producer each loop starts a fresh iterator (`test/modules/test_for_iter.vyb`, `test/modules/test_for_iter_skip.vyb`). Plain-identifier iterables route onto the protocol too: `for (x in vec)` desugars like `for (x in vec.iter())`, and the stdlib iterators are self-iterable (their `iter()` returns a fresh iterator), so a stored iterator identifier iterates as well (`test/modules/test_for_identifier.vyb`). Network I/O and higher-order `Vec` follow-ups still track their compiler-feature items. `collections` now also ships the generic `VecIter<T>` iterator (`v.iter()`, bound to `Iterator`, `test/modules/test_vec_iter.vyb`) and `HashMap`/`HashSet` iterator binds — `MapIter<K,V>` (`m.iter()`, yields key/value pairs as `MapEntry<K,V>` via `kv.key` / `kv.value`) and `HashIter<K>` (`s.iter()`, yields values), held by reference through a `their<...>` view and usable in `for (kv in m.iter())` / `for (v in s.iter())` (`test/modules/test_collections_iter.vyb`). This was enabled by generalized nested `their<T>` view-field member access (an intermediate member-read now records its AST type so chained accesses resolve) plus a depth-aware `TypePattern` argument parser that correctly handles a two-generic-param iterator `Item` (`MapEntry<String,Int>`). the Rust-shaped `Option<T>` enum is removed (native `V?`/`Item?` optionals used instead), and `core::result` is a placeholder retained for future `Result<T,E>` |
| Stdlib core auto-import | ✅ | The core contracts module (`core::aspects`, incl. pre-wired primitive binds) is auto-imported into non-stdlib modules, skipped on explicit import / local redefinition, opt-out via `no_core()` directive. Transitional prelude helper `prelude_ok` remains explicit-only (`import prelude` / `import core::prelude`) |
| Stdlib terminal + stdin module (`term`) | ✅ | Interactive console I/O over new `__vyb_stdin_*` / `__vyb_eprint*` / `__vyb_term_*` runtime helpers — `stdin_read(maxlen)`, `stdin_read_line()`, `stdin_isatty()`, raw mode (`stdin_raw_enable`/`stdin_raw_disable`), stderr `eprint`/`eprintln`, `flush`/`stderr_flush`, and terminal control (`term_cols`/`rows`, `term_clear`, `term_move_cursor`, `term_hide_cursor`/`term_show_cursor`); `import term` (`test/term/term_io.vyb`, `examples/term_input.vyb`) |
| Stdlib UTF-8 module (`utf8`) | ✅ | Codepoint-aware helpers over Vyb byte strings via weak runtime shims — `utf8_len`, `utf8_at` (codepoint value at a byte offset), `utf8_index` (byte offset of the n-th codepoint), `utf8_valid`; all offsets are byte-based to match the byte-indexed String model (`import utf8`, `test/utf8/test_utf8.vyb`) |
| Stdlib env module (`env`) | ✅ | In-process environment access — `env_get` ("" when unset), `env_set`, `env_unset` over `__vyb_env_*` runtime helpers; a provider reads `HTTP_PROXY`/`HOME`/`TERM` here (`import env`, `test/env/test_env.vyb`) |
| Stdlib rand module (`rand`) | ✅ | A small xorshift generator — `rand`, `rand_range(lo, hi)`, `rand_seed` for reproducible sequences (`import rand`, `test/rand/test_rand.vyb`) |
| Stdlib process module (`process`) | ✅ | Run a trusted command line through the shell — `exec_run` (exit code or -1), `exec_output` (captured stdout), `exec_status`; `exec_run`/`exec_output` are freedom-gated (must be called inside a `freedom` block), `exec_status` is ungated (`import process`, `test/process/test_process.vyb`) |
| Stdlib regex module (`regex`) | ✅ | POSIX extended patterns over String bytes — `regex_match`, `regex_find` (byte offset), `regex_capture_match`, `regex_capture` (first group), `regex_replace`, `regex_replace_all`; no-match/compile failure yields 0 / -1 / "" / unchanged (`import regex`, `test/regex/test_regex.vyb`) |
| Stdlib network IPv6 + timeouts | ✅ | `Socket::AF_INET6`, dual-stack literals ("::1"), `socket_resolve` over IPv4/IPv6, and `socket_set_timeout(fd, ms)` (SO_RCVTIMEO/SO_SNDTIMEO, 0 disables) for the raw socket layer (`test/modules/test_network_ipv6.vyb`, `test/modules/test_network_timeout.vyb`) |
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
| Enums | ✅ | C-like enums (`enum Color { Red, Green, Blue }`) are first-class typed values backed by a single scalar `i64` tag: `r<Color> = Color::Red`, `println(r)` renders `Color::Red`, and `match`/`select` dispatch on named variants with the same exhaustiveness as data enums. Every enum value also exposes its raw positional variant tag as an `Int` via `.tag` (both C-like scalar and data-carrying enums, `test/units/test_enum_tag.vyb`). Because the value is a scalar `i64`, an extern parameter typed `Color` interoperates with a C integer-backed enum (registered by-value FFI, e.g. `labs(Color::Green)` → `1`). Data-carrying (tagged-union) enums like `enum Shape { Circle(Float), Rect(Float, Float), Unit }` and generic `enum Box<T> { Value(T), Empty }` compile to a value-semantics `{ i64 tag, [N x i8] data }` union, construct via `Shape::Variant(args)` / `Box<Int>::Value(x)` (generic enums monomorphize per concrete type with substituted payloads), and match on / `select` on variants (`Circle(r) ->`, `Unit ->`) dispatch on the runtime tag and bind payload fields. Both `match` and `select` require exhaustiveness (every variant or a wildcard); an exhaustive match's no-match default is unreachable so an all-return final-statement match compiles cleanly. The Rust-shaped `Option<T>` enum is removed in favor of the native `T?` optional, and `Result<T,E>` is the built-in generic data enum: `Ok(T)` / `Err(E)`, constructible via `Result<Int, String>::Ok(x)` / `::Err(e)` and type-inferred bare `Ok(x)` / `Err(e)`, with the same match/select dispatch and exhaustiveness. Every data enum value exposes a native `.value` payload accessor (`Ok(T)`→`T` for Result; a non-primary `Err` variant yields a default `T`, `test/units/test_enum_value_accessor.vyb`), complementing `.tag` and the exhaustiveness-checked `match`/`select` path. Aspect `bind` can attach methods to an enum target type (`bind HasArea -> Shape { ... }`) — including generic user enums (`bind Render -> Box<Int>`) and the built-in `Result<T,E>` (`bind Describe -> Result<Int, String>`), dispatching on the concrete variant with the substituted payload. Bare `Ok(e)`/`Err(e)` also work as call arguments via expected-type propagation (`classify(Ok(11))`, `v.push(Ok(x))` on a `Vec<Result<Int, String>>`). Generic-bind `Result<T,E>` construction and `match`/`select` work for type-parameter payloads: a `match`/`select` scrutinee that is a concrete enum returned from a generic bind materializes its payload types (`materializeConcreteEnum`), and bare `Ok`/`Err` inside a monomorphized generic bind substitute the active type params. |
| Generics (monomorphization) | ✅ | Includes generic functions and current generic bind method executable monomorphization, plus call-site type-argument inference with return-type substitution and aspect-bound validation on generic function calls; failable generic functions now use the `{T, i8*}` error-return ABI so a monomorphized `fail` is caught by the caller's `trap` at runtime; explicit type-argument calls (`probe<Int>(0, 0)`) parse and monomorphize correctly; a generic function returning a generic struct/enum type (e.g. `make_box<T>(v)<Box<T>>`) resolves to the concrete `Box<Int>`/`Box<String>` for both explicit (`make_box<Int>(7)`) and inferred (`make_box("hi")`) type args, and distinct instantiations coexist in one program without sharing stale cached types (`test/units/test_generic_struct_return.vyb`); a bounded type parameter (`K<Hashable>`) forwarded by value into another bounded generic helper validates against the helper's bound via the parameter's own declared bound and drives the inner helper's monomorphization against the concrete type at the call site (`both<K<Hashable>, L<Hashable>>` → `hashit(a)` monomorphizes to `hashit_String`, `test/units/test_bounded_param_nested_dispatch.vyb`); broader nested/member templates still need expansion |
| Aspect/Bind polymorphism | ✅ | Includes canonical simple receiver shorthand `method(self)<T>`, legacy/explicit `self<Self>`, ownership-qualified receivers, associated types (declarations, bind assignments, validation, defaults declared as `type Item = Int`, aspect bounds such as `type Item<Display>`, and resolution through generic binds — `bind<T> Iterator -> Boxer<T> { type Item = T }` resolves `Self::Item`/`T` to the concrete type at the call site), a bind method returning `Self::Item` resolving in both concrete and generic bind bodies, executable generic bind methods for current supported shapes, ambiguous dot-call diagnostics, qualified disambiguation (`Aspect::method(receiver)` selects a specific aspect when multiple bound aspects share a method name; bind symbols are emitted per `Type_Trait_Method`; also works on bounded type parameters inside generic functions), bind selection precedence (bounded generic bind over unbounded for the same shape), super-aspect inheritance (`aspect Sub : Super` with binding requirements and cycle detection), and unqualified bounded-type-parameter dispatch: `thing.show()` for `thing<T<Display>>` inside a generic function resolves through the bound aspect and substitutes `Self` in the return type, including inherited super-aspect methods (both unqualified `thing.name()` and qualified `Named::name(thing)` walk the bound's transitive super-aspect chain). Inside a bind method body, struct field reads now substitute the receiver's concrete generic arguments (`self.keys` on a `Map<Int, Int>` receiver is `Vec<Int>`, not `Vec<K>`, and a bound generic parameter resolves a struct payload field so it can flow into bounded helpers like `hashit(self.v)`). Ownership-qualified receivers provide by-reference mutation: `self<their<T>>` (and `self<my<T>>`/etc.) lowers `self` to a pointer and the call site passes the receiver's address, so in-place mutations persist on the caller for both concrete and generic bound binds (`self<their<Map<String,Int>>>` drives `Map_String_Int_Assoc_put(ptr %self, ...)`; `their` is the borrow/no-cleanup choice, `my` is a move/ownership-taking receiver), `test/aspect/test_bind_by_ref_receiver.vyb`. Nested by-ref fields also resolve: a struct field typed `their<Vec<T>>` (e.g. an iterator holding a Vec by reference in `data<their<Vec<T>>>`) calls the built-in Vec methods (`len`/`get`/`set`/`push`) directly, with reads and mutations reaching the borrowed backing Vec — the semantic pass unwraps ownership-wrapped member-expression receivers and codegen derefs the single-pointer field slot (`test/modules/test_nested_their_vec_field.vyb`). A `core::aspects` stdlib module declares the six canonical contracts (`Display`, `Debug`, `Clone`, `Equatable`, `Hashable`, `Comparable` with `Comparable : Equatable`), re-exported via prelude, bindable to structs with unqualified dispatch and generic bounds and also bindable directly to primitive scalar targets (`Int`/`Float`/`Bool`/`Char`) with unqualified dispatch and generic bounds. `bind` declarations carry across module imports (visibility via `share`, dedup by `(target, aspect)`), so the `core::aspects` stdlib module ships pre-wired `Display`/`Clone`/`Equatable`/`Comparable`/`Hashable` implementations for `Int`/`Float`/`Bool`/`String` that are active on `import core::aspects` / `import core::prelude` |
| Ownership: `my`, `our`, `their`, `mild` | 🚧 | Lexical borrow enforcement, `our<T>` copy/assignment/parameter refcounting (shared strong refs retained per location, released on scope exit/overwrite), `mild<T>` control blocks with `our<T>?` failed-`grab()`, unwrap-on-read for primitive `my`/`our`/`mild<T>`, and compile-time `my<T>` move tracking (use-after-move rejection, transfer on assignment/init/`my`-param, revive-on-reassignment, read/copy to plain targets); full drop semantics for every exit path still planned |
| `freedom` blocks + `loc<T>` raw pointers | ✅ | |
| `match` / `select` expressions | ✅ | Literal, wildcard, comparison, struct-destructuring (`Point { x, y } ->` binds fields in the arm body; unknown fields and type mismatches rejected), inclusive range patterns (`1..10 ->`; inverted ranges rejected as never-matchable), guard clauses (`pattern if condition ->`, with access to destructured bindings), and `match` as a value-returning expression (`r = match (v) { 1..3 -> 10, ? -> 20 }` infers the result type from the first arm and stores the matched arm's value; block arms yield via `pass`) |
| `defer` | ✅ | |
| `fail` / `trap` error system | ✅ | Includes typed `fail<T>(value)`, typed traps, wildcard (`e<?>`) and multi-type (`e<Type1 | Type2>`) trap parsing, dual-return ABI `{T, i8*}` / `{i1, i8*}`, Phase 3 fail propagation returns, Phase 4 auto-propagating call-site checks, and Phase 5 untrapped runtime handler dispatch from failable `main`, and `ensure` contract statements (`ensure cond else handling`, desugaring to `if (cond) { } else { handling }`); trap context is function-local so `fail` in a callee (including inside an `if`/`else` branch or an `ensure`-`else`) propagates through the failable ABI and is caught by the caller's trap. Chained `} trap (e<Type>)` clauses dispatch first-type-compatible-wins; a multi-type union handler binds `e` as an opaque error pointer resolved via `e as T` / `typeof(e)` / `typename(e)`. A `trap` block used as a value (`s<String> = { risky() } trap (e<?>) -> { "hello" }`) infers its result type from the handler and sizes the result slot accordingly, so a String handler round-trips as a `{ ptr, len }` (not a hardcoded `i64`) |
| `chan<T>` (typed channels) | ✅ | Built-in generic thread-safe channel (single i64 handle, spatially identical to Int, so sharing a chan by value across pthreads references one channel). `chan<T>()` / `chan<T>(cap)` construct an unbounded/bounded channel; methods `send(v)`, `recv()` (blocking), `poll()` (non-blocking), `len()`, `free()`, and `handle()` (the raw Int for `chan_select`). Int-family scalar payloads use the int-slot channel runtime; String payloads use the refcounted string runtime with reference transfer (`test/modules/test_chan_typed.vyb`, `test/modules/test_chan_threaded.vyb`). Float/Bool/Char payloads are likewise supported (Float as its IEEE bit pattern,
  Bool as 0/1, Char as its code unit), and scalar `poll()` returns the native `T?`
  (absent when empty, read via `poll() else default`) (`test/modules/test_chan_scalar.vyb`).
  Non-identifier receivers dispatch too — a chan returned by a function
  (`make().send(x)`) or held as a struct field (`h.ch.recv()` / `h.ch.poll()`)
  lowers like the named-variable path (`test/modules/test_chan_nonident.vyb`).
  String payloads keep the empty-string sentinel poll. Non-identifier method
  receivers remain a follow-up |
| `async` / `await` | 🚧 | Runtime stub |
| `Vec<T>` | ✅ | Bare `Vec()` / `Vec(n)` and explicitly-typed `Vec<T>()` / `Vec<T>(n)` constructors all build a real `{ ptr, size, cap }` growable vector (typed versions work standalone and in struct fields, `test/units/test_vec_typed_constructor.vyb`) |
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
| `vyb bindgen` (MVP + libclang `--full`) | ✅ | `vyb bindgen <header.h> [-o out.vyb]` parses a C subset and emits importable extern/`repr(C)`/enum bindings (`test/bindgen/*`). `--full` adds the libclang full-preprocessor backend (`#include` expansion, conditional evaluation, object-like/expression/function-like macros (incl. comparison, C `?:` ternary, and string bodies mapped to typed Int/Float/Bool/String functions; `full_preproc.h`/`test_full_preproc_bindings.vyb`). Fixed-size C array struct fields bind as value-array fields (`arrstruct.h`/`test_arrstruct_bindings.vyb`); unions bind as `#[repr(C)]` structs (`unions.h`/`test_unions_bindings.vyb`); flexible array members skip.) |

---

## Standard Module Error Handling

| Module | Shape | Notes |
|--------|-------|-------|
| `io` | Native `T?` (lossless) | `open`/`open_read`/`open_write`/`open_append` -> `File?`; `write_str` -> `Int?` (bytes, absent on failure); `read_all` -> `String?` (a successful empty read is the present `""`); `close` -> `Bool?`. Diagnostics via `error_code()`/`error_message()` and escalation via the shared `IoError`/`io_error(op, path)`. No sentinels. |
| `network` | Native `T?` | Acquisition returns `TcpStream?`/`TcpListener?`/`UdpSocket?` (absent = failed). Every `socket_*` primitive follows the same shape: `socket_open`/`socket_accept`/`socket_local_port` -> `Int?`, `socket_close`/`socket_bind`/`socket_listen`/`socket_connect`/`socket_set_timeout` -> `Bool?`, `socket_send` -> `Int?` (bytes), `socket_recv` -> `String?` (present-empty at EOF, absent on error), `socket_resolve` -> `String?`. The wrapper method callers actually use, `TcpStreamOps::read`, now follows the same lossless shape (present-empty at EOF, absent on transport error) via the same `vyb_net_recv_opt` intrinsic. Oracle output-side is the same shape: `udp_recv_from`/`UdpSocketOps::recv_from`/`async_tcp_read`/`async_udp_recv_from` -> `String?` (absent on error), and the last-peer probes `udp_last_peer_ip`/`udp_last_peer_port` -> `String?`/`Int?` (absent until a datagram arrives). Operations on the wrappers (`write`/`send_to`/`udp_send_to`/`async_*`) return `Int?`/`Bool?`. Escalate via `NetError`/`net_error(op, target)`. |
| `tls`, `https` | Native `T?` | `tls_stream`/`tls_client_context`/`https_get`/`https_get_full` (and verified variants) return a `T?`/`HttpResponse?` (absence = failure), with `TlsError`/`HttpError` + `<mod>_error(op, target)` builders; the `https_selfhost*` diagnostics return `Bool?` (absent on any failure). |
| `term`, `env` | `Bool?` / `Int?` | `stdin_raw_enable`/`stdin_raw_disable`/`flush`/`stderr_flush` and `env_set`/`env_unset` return `Bool?` (present = ok, absent = failure); the stderr writers `eprint`/`eprintln` return `Int?` (bytes written, absent on error). The four ANSI emitters `term_clear`/`term_move_cursor`/`term_hide_cursor`/`term_show_cursor` also return `Bool?` (absent on failure). |
| `asyncs` | Native `T?` | `async_sleep_ms`/`async_connect` -> `Bool?`; `async_send` -> `Int?` (bytes written, absent on error); `async_recv` -> `String?` (present-empty at close, absent on error); `async_spawn`/`async_poll`/`async_accept` -> `Int?`. Network acquisition/IO flows through the same shapes (`async_tcp_read` -> `String?`). |
| `curses` | `Int?` / `Bool?` | `curses_init` -> `Int?` (present once the screen is active); the draw/attr/window ops `curses_close`/`refresh`/`clear`/`move`/`addstr`/`move_addstr`/`start_color`/`init_pair`/`attr_on`/`attr_off`/`nodelay`/`timeout`/`keypad`/`show_cursor`/`hide_cursor` -> `Bool?` (absent = failed). Value getters stay `Int`: `ok`/`has_color`/`color_pair`/`attr_*`; `curses_rows`/`curses_cols` -> `Int?` (present while active, absent otherwise); `curses_getch` stays `Int` (-1 "ran but found nothing" on a timeout/no-delay read, like `String::index_of`). |
| `agents` | `Bool?` | `agent_send`/`agent_send_bool`/`agent_send_float`/`agent_send_string`, `agent_close`, `agent_free`, `agent_dead_letter` -> `Bool?` (absent = agent stopped, mailbox full, or a bad handle). Probes stay `Int`/`String`: `agent_len`/`agent_alive`/`agent_status`/`agent_error_code`/`agent_mailbox`, `agent_error`. The `agent_start*` handle-creators return `Int?` (absent on spawn failure). |
| `channels` | `Bool?` / `T?` | send/recv were already native (`send`/`try_send` -> `Bool?`; `recv`/`try_recv` -> `T?`); the lifecycle ops `chan_close`/`chan_free` and `strchan_close`/`strchan_free` -> `Bool?` (absent = bad/stale handle or already closed). Raw `chan_new`/`chan_bounded`/`strchan_new`/`strchan_bounded` return `Int?` (absent on allocation failure); the compiler-native typed `chan<T>` already speaks `T?`. |
| `threads`, `tasks` | `Int?` / `Bool?` | Handle-creators `mutex_new`/`cond_new`/`atomic_new` -> `Int?`; ops `thread_detach`/`mutex_lock`/`mutex_unlock`/`mutex_free`/`cond_wait`/`cond_signal`/`cond_broadcast`/`cond_free`/`atomic_store`/`atomic_free`/`task_free` -> `Bool?`. `thread_join` -> `Int?` (a present result may legitimately be `-2`; absent = bad/detached/joined). Value probes stay `Int`: `atomic_load`/`atomic_add` (returns the new value)/`atomic_cas`, `task_await`. |
| `time` | `Int?` / `Int` | `time_epoch_secs`/`time_epoch_millis`/`time_nanos` -> `Int?` (absent on a clock error; the wall-clock getters report `-1` from `clock_gettime` on failure). `time_mono_millis` has no failure path and stays `Int`. |
| `qt` | `Int?` / `Bool?` | All widget/dialog creators (`qt_*_create`, `qt_vbox`/`qt_hbox`/`qt_grid`, `qt_menubar`/`qt_menu_add`/`qt_action_add`/`qt_toolbar_create`, `qt_tabs_add`, `qt_dlg_*`) -> `Int?` (absent = handle alloc failed); op-status funcs (`qt_quit`/`qt_process_events`/`qt_set_timer`/`qt_run_stop`/`qt_post_event`, the `set_*`/`add`/`close`/web-load/navigation helpers, modal `qt_msg_info`/`warn`/`error`/`about`, `qt_dlg_close`) -> `Bool?` (absent = failed, e.g. bad/wrong-kind handle or GUI not running). Dimension/DPI getters (`qt_screen_width`/`qt_screen_height`/`qt_screen_dpi`, `qt_window_width`/`qt_window_height`) also return `Int?` (absent on no screen / bad window handle). Value getters stay `Int`/`String`/`Bool` (`qt_*_text`/`url`/`count`/`value`/`current`/`index`, `qt_init`/`qt_active`/`qt_timer_fired`/`qt_run`/`qt_wait_event`, `qt_event_*`). The modal dialog results use native optionals: `qt_msg_question` -> `Int?` (absent when the GUI is not running; present 1 for Yes / 0 for No), and the modal pickers `qt_file_open`/`qt_file_save`/`qt_dir_select` plus `qt_dlg_selected` -> `String?` via lossless opt intrinsics (present-empty/present-path on user cancel, absent on no-GUI / no-result-yet) -- so a cancel is not conflated with failure. |

**Note (reviewbot):** the default `https_get_full`/`tls_client_context` are intentionally *unverified* (self-signed loopback / demo convenience), and the module header documents this; there is **no runtime guard** warning against using the unverified context against real hosts. Verified variants (`https_get_full_verified`, `tls_client_context_verified`) exist. Acceptable for now per the docs; a build-time `--require-tls-verify` flag or runtime warning is a future improvement.

**Runtime behavior (SIGPIPE):** the runtime installs `SIGPIPE = SIG_IGN` process-wide (a constructor in `runtime/vyb_runtime.c`) so a TLS `send` after a peer RST returns `EPIPE` instead of killing the process. This is global: any program that links the runtime gets the non-default `SIGPIPE` behavior, so a write to a closed pipe/peer reports an error rather than raising `SIGPIPE`. Applications that need the default `SIGPIPE` kill-semantics can re-install the handler at startup (`signal(SIGPIPE, SIG_DFL)`).

*Last updated: v0.7.3 (2026-08-20)*
