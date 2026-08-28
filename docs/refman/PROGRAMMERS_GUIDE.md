# Vyb Programmer's Guide

> A forward-facing manual for working Vyb programmers. Where the top-level
> `README.md` is the overview and project pitch, this guide is the working
> reference: every language feature, the ownership model, the polymorphism
> strategy, and a full tour of the standard library.

This manual sits alongside the auto-generated reference under
[`docs/refman/`](index.md):

- [`index.md`](index.md) — module map with fan-in / fan-out.
- `<module>.md` — one page per stdlib module with every exported symbol,
  signature, and its inter-relationship list.
- [`interfaces.md`](interfaces.md) — shared cross-module types and who
  consumes them.
- [`functions.md`](functions.md), [`types.md`](types.md),
  [`aspects.md`](aspects.md), [`runtime.md`](runtime.md) — cross-indexes.

Throughout this guide, `[Module →](http.md)` style links jump to those pages
for the exact signature of every symbol. The code samples are real and run
through the test harness.

> **Authoritative source.** This Programmer's Guide and the auto-generated pages
> under `docs/refman/` are the canonical reference for the language. Design and
> review notes elsewhere (the `doc/*` tree and dated review files) are historical
> and may describe features as planned or stubbed; trust the guide + refman,
> cross-checked against a live `build/vyb` run.

---

## Contents

1. [Mental model](#1-mental-model)
2. [Getting started](#2-getting-started)
3. [Language tour](#3-language-tour)
4. [Standard library reference](#4-standard-library-reference)
5. [Concurrency and async model](#5-concurrency-and-async-model)
6. [Networking cookbook](#6-networking-cookbook)
7. [Performance and memory model](#7-performance-and-memory-model)
8. [Testing and tooling](#8-testing-and-tooling)
9. [API index](#9-api-index)
10. [Appendix A — Memory model](#appendix-a--memory-model)
11. [Appendix B — Auto-serialization](#appendix-b--auto-serialization)
12. [Appendix C — Glossary](#appendix-c--glossary)
13. [Appendix D — Grammar (EBNF)](#appendix-d--grammar-ebnf)

---
## 1. Mental model

Vyb's core conviction: **you should not have to choose between safety and
power.**

**Strong, name-first typing.** The compiler checks types *before* codegen.
The type leads the name; the value reads like plain data:

```vyb
x<Int> = 5            # the type leads, the value follows
```

**Monomorphized generics.** `Vec<T>`, `HashMap<K, V>`, and every generic
function are specialized per concrete type at compile time — no vtable, no
boxed aspect object, no runtime dispatch:

```vyb
first<T>(v<Vec<T>>)<T> -> { … }
cmp_lt<T<Comparable>>(a<T>, b<T>)<Bool> -> { … }
```

**Explicit ownership.** Every value knows whether it is `my`-owned,
`our`-shared (ref-counted), `their`-borrowed, or a `mild`/`soft` observer;
the allocator and compiler move and free values deterministically:

```vyb
increment(c<their<Counter>>)<Void> -> { c.count = c.count + 1 }
shadow<mild<Counter>> = soft(shared_data)
```

**Freedom when you need it.** `freedom { … }` blocks, raw `loc<T>` pointers,
and the FFI let you touch machine state directly. Safety is the default, not
the ceiling.

**A standard library written in Vyb.** Collections, networking, TLS, HTTP,
threads, channels, and an async executor are all `stdlib/**/*.vyb` over a
small, C-level runtime. The language dogfoods itself.

The result: native executables (or JIT), predictable performance, readable
code, and zero-cost polymorphism.

### 1.1 Core feature registry

This guide is the canonical home for **every core language feature**. The table
below maps each feature to its in-depth section — keep it updated whenever a
core feature changes, so developers and end users always know where a feature
lives.

| Area | Sections |
|---|---|
| Types, literals, `T?` | [§3.2](#32-primitives-and-literals) |
| Variables, mutability, `const` | [§3.3](#33-variables-inference-mutability) |
| Functions & parameters, `fn` types | [§3.4](#34-functions-and-parameters) |
| Closures & lambdas, capture forms | [§3.5](#35-closures-and-lambdas), [§3.5.1](#351-capture-forms) |
| Control flow (`for`/`while`/ranges) | [§3.6](#36-control-flow) |
| `match` / `select`, patterns, exhaustiveness | [§3.7](#37-pattern-matching-match-and-select) |
| Tuples & variadic tuples | [§3.8](#38-tuples-and-variadic-tuples) |
| Structs | [§3.9](#39-structs) |
| Enums & constant enums | [§3.10](#310-enums) |
| Operators (arithmetic, bitwise, compound) | [§3.11](#311-operators) |
| Casts (`as`) | [§3.12](#312-casts-with-as) |
| Ownership & accessors (`my`/`our`/`their`/`mild`/`soft`) | [§3.13](#313-ownership-and-accessors) |
| Generics & monomorphization | [§3.14](#314-generics-and-monomorphization) |
| Aspects & binds (polymorphism) | [§3.15](#315-aspects-and-binds-polymorphism) |
| Errors (`trap`/`fail`/`ensure`) | [§3.16](#316-error-handling-trap-fail-ensure) |
| Strings | [§3.17](#317-strings) |
| Collections | [§3.18](#318-collections) |
| Modules & imports (`import`/`share(all)`/`prelude`) | [§3.19](#319-modules-and-imports) |
| FFI & native bridge | [§3.20](#320-ffi-and-the-native-bridge) |
| Serialization | [§3.21](#321-serialization) |
| Introspection | [§3.22](#322-introspection) |
| Async / `await` / `Future<T>` | [§3.23](#323-asynchronous-programming), [§3.23.1–§3.23.3](#3231-async-lambdas) |
| Memory model / freedom | [§7](#7-performance-and-memory-model), [Appendix B](#appendix-b-auto-serialization) |
| Standard library tour | [§4](#4-standard-library-reference) |
| Concurrency model | [§5](#5-concurrency-and-async-model) |

**Maintenance policy.** Changes to core language features are documented
*here* first — not scattered across README or only in tests. When you add or
change a core feature, add it to this registry and update its section. The
`README.md` is the project overview and points here; the auto-generated
per-symbol pages under `docs/refman/` are the exact API surface.

---

## 2. Getting started

```bash
# Clone and build
git clone https://github.com/rickenator/Vyb.git && cd Vyb
./build.sh                      # clean configure + build

# Or an incremental rebuild from the repo root
cd build && cmake . >/dev/null && make vyb -j16
```

```vyb
# hello.vyb
main()<Int> -> {
    println("Hello, Vyb")
    return 0
}
```

```bash
./build/vyb hello.vyb          # compile and run
./build/vyb hi.vyb -c hi.o     # compile to an LLVM object file
```

The compiler compiles `*.vyb` to LLVM IR, codegens an object, links it with
the runtime (the C atoms `runtime/vyb_runtime.c` + `runtime/vyb_type_metadata.c`
plus the C++ type/closure atoms `src/runtime/error_handling.cpp` +
`src/vre/intrinsics.cpp`), libc/libm, and OpenSSL for TLS, and produces a native
executable. A single file becomes an executable with `vyb app.vyb --build app`;
multi-file packages use `vyb.toml` (see *Projects and packaging* below). Common
flags: `--compile <out.o>`, `--link <lib>`, `--static`, and `-O<0..3>`.

### Running the test suite

```bash
# 1077 .vyb tests exercised through compile + run + output/return checks
python3 test/run_tests.py --vyb ./build/vyb --test-dir test --execute-jit
```

Key test knobs (see also [§8](#8-testing-and-tooling)): `--pattern`, `--category`, `--report`,
`triage` for triage planning, and HTML report generation.

### Projects and packaging

A *project* is a multi-file package described by a `vyb.toml` manifest at its
root. A minimal manifest declares the package and one or more binaries:

```toml
[package]
name = "myapp"
version = "0.1.0"

[[bin]]
name = "myapp"
path = "src/main.vyb"

[dependencies]
mylib = { path = "../mylib" }
```

```bash
vyb new myapp           # scaffold myapp/vyb.toml + myapp/src/main.vyb
cd myapp && vyb build   # build every [[bin]] into ./target/
./target/myapp
```

`vyb build` does the following:

- Reads `vyb.toml` and builds each `[[bin]]` into `target/<name>`.
- Adds the project `src/` directory and every local `[dependencies]` path to the
  module search paths, so imports in your project resolve your own modules and
  library modules uniformly.
- Writes `vyb.lock`, pinning the resolved dependency set.
- Compiles and links through the same pipeline as a single file, into
  `target/<name>`.

Dependencies may be local paths today: `mylib = { path = "../mylib" }`. Remote
(version / git) dependency resolution is not implemented yet; a manifest that
declares one fails `vyb build` with a clear message.

`--link <lib>`, `--static`, and `-O<0..3>` pass through to the native link, and
`vyb build` also accepts an optional project directory (`vyb build path/to/proj`
or `-C path/to/proj`).

---

## 3. Language tour

### 3.1 Files, `main`, and comments

Every program has an entry point:

```vyb
main()<Int> -> { return 0 }
```

`main` may return **any** type. An integer return becomes the process exit
code; every other value — `String`, `Float`, `Bool`, structs, `Vec`s, tuples,
maps — auto-serializes on exit (complex values as JSON). Comments use `#` line
comments (and `//` in some tooling/tests):

```vyb
# A doc-ish comment above a declaration becomes that symbol's doc text.
```

The standard library follows this convention so the refman can reuse prose.

### 3.2 Primitives and literals

Vyb's primitive catalog covers sized and unsigned integers, two float widths,
Unicode scalars, raw bytes, and the scalar value types.

**Integers**

| Type | Meaning |
|---|---|
| `Int` / `Int64` | signed 64-bit integer (the default) |
| `Int32`, `Int16`, `Int8` | narrower signed integers |
| `UInt` / `UInt64` | unsigned 64-bit integer |
| `UInt32`, `UInt16`, `UInt8` | narrower unsigned integers |

**Floating point**

| Type | Meaning |
|---|---|
| `Float` / `Float64` | double-precision float (the default) |
| `Float32` | single-precision float |

**Other scalars**

| Type | Meaning |
|---|---|
| `Char` | a UTF-8 code unit |
| `Rune` | a Unicode code point |
| `Bytes` | a fat-pointer block of raw bytes |
| `Bool` | `true` / `false` |
| `String` | a fat-pointer, length-known byte string (immutable) |
| `Void` | the absence of a value |
| `T?` | a Vyb optional: present `T?(v)` or absent `T?()` |

**Integer casts** use `value as Type` between the sized integer types — widening
sign/zero-extends from the source type and narrowing truncates. For example,
packing eight `UInt8` into an `Int64`:

```vyb
b0<UInt8> = 0b11001110   # ... and b1..b7
boxed<Int64> = (b0 as Int64) | ((b1 as Int64) << 8) | ((b2 as Int64) << 16) | ...
```

**Explicit integer assignment**: a variable/field may only change integer width
or signedness through `as`. A bare in-range constant still fits (`x<Int8> = 3`),
but an out-of-range constant or any typed value crossing width/signedness is a
compile error — `x<Int8> = 300` and `x<Int8> = wide<Int>` are both rejected.

**Type aliasing**: all numeric types accept three naming conventions.

- Vyb style: `Int32`, `Float64`, `UInt8`
- C style: `int32`, `float64`, `uint8`
- LLVM style: `i32`, `f64`, `u8`

Integer literals are concise across bases and flow into any sized type:

```vyb
h<Int> = 0xFF                // hex
b<Int> = 0b11001110          // binary
b0<UInt8> = 0x80             // hex into a sized unsigned type
c<UInt32> = (b0 as UInt32) | ((1 as UInt32) << 8)
```

**Optional `T?`** is Vyb's native optional, built into the type system.

A `T?` is either **present** — carrying a live value of type `T` — or **absent** —
carrying no value at all. Vyb's data model has no `null`: a spot that may lack a
value is *typed* as `T?`, so absence is an explicit state, never a surprise read.

```vyb
a<Int?>  = Int?(5)          // present: carries 5
b<Int?>  = Int?()           // absent:  carries nothing
```

Because absence is explicit, an optional cannot be silently read as its plain
`T`. `val<Int> = a` is rejected at compile time ("Expected Int but got Int?");
every read must say what to do when the value is missing. **`else`** is that
decision — the payload when present, otherwise the fallback you provide:

```vyb
val<Int> = b else 42          // absent  -> 42
p<Int>   = a else 0           // present -> 5

makeopt(v<Int>)<Int?> -> { if (v > 0) { return Int?(v) } return Int?() }
q<Int> = makeopt(9) else -1     // 9
r<Int> = makeopt(-5) else -1    // -1
```

Reading a present optional yields its value directly — no `Some(v)` wrapper and
no `.unwrap()` ceremony. Chaining is short-circuit and right-associative
(`a else b else c` == `a else (b else c)`): the first present value wins,
otherwise the final fallback.

Optionals are what reads return when there may be no value: `v.pop()?` (absent
when empty), `v.first()` / `v.last()` / `v.get(i)` (absent on a missing
element), `grab()` on a released `mild` (absent), and channel receives
(`chan_recv` returns `Int?`, absent when the channel is closed and drained).

### 3.3 Variables, inference, mutability

```vyb
x<Int> = 5        // explicit type
y = 3.14          // inferred from the RHS (auto is optional: `auto y = 3.14`)
n<Int const> = 7  // immutable (const is a type modifier)
const<Int> c = 9  // immutability is also expressed with a const prefix
a, b, c = t       // tuple destructure: each member type is inferred
```

Declarations are type-first (`name<Type>`). `name = expr` on a **new** name
infers its type from the RHS, so the `auto` keyword is optional
(`auto y = 3.14` is equivalent to `y = 3.14`). Values default to mutable unless
typed `const` (either `name<Type const>` or `const<Type> name`).

Inference resolves literals, simple calls and arithmetic, lambdas
(`add = |x, y| -> x + y`), and tuple element access (`x = t[i]` for a constant
index `i`). A `name<Type> = value` annotation is still canonical and is required
wherever the initializer's type cannot be inferred (for example, a tuple element
accessed with a non-constant index). To update an **existing** variable, use a
bare assignment `name = value`; the compiler only treats `name = expr` as a new
declaration when `name` has not already been declared in the enclosing scope.

### 3.4 Functions and parameters

Both standard and shorthand parameter syntax are supported and produce
identical LLVM IR:

```vyb
# Standard: explicit parameter<Type>
add(a<Int>, b<Int>)<Int> -> { return a + b }

# Shorthand: type-first
mul(a<Int>, b: Int)<Int> -> { return a * b }   # or `b<Int>`
```

Return type is always bracketed: `name(params)<Ret> -> { … }`. An early
`return value` exits; a bare `-> { … }` may rely on trailing expressions in
expression contexts.

**`fn` types.** A function/closure type is written `fn(Args…) -> Ret`:

```vyb
apply(f<fn(Int, Int) -> Int>, a<Int>, b<Int>)<Int> -> { return f(a, b) }
work<fn() -> Int> = || -> 42
```

This is the parameter type used by `thread_spawn`, `task_spawn`,
`async_spawn`, and the collection higher-order methods.

**Forward references.** Within a module, name resolution is independent of
implementation order. A function may call another function — or itself —
declared later in the same file, and pairs of mutually-recursive functions
resolve regardless of which is written first. The compiler pre-registers every
top-level function before analyzing any body, so `helper()` is callable from
`main()` whether `helper` appears above or below it in the source:

```vyb
main()<Int> -> { return helper() }   # helper is declared below — fine
helper()<Int> -> { return 42 }
```

The same separation applies to **types**: a struct, enum, or type alias may be
referenced before its declaration in the same module — in another struct's
fields, a function parameter or return type, a construction, or a member
access — and mutually-referential structs resolve in either order. Type
declarations are analyzed before function bodies, and every top-level type's
*name* is pre-registered so `struct A { b<B> }` works whether `B` precedes or
follows it:

```vyb
make()<B> -> { return B { x = 5 } }   # B declared below — fine
main()<Int> -> { return make().x }
struct B { x<Int> }
```

(The LLVM codegen likewise pre-declares an opaque type per top-level struct and
fills bodies afterward, so struct-to-struct forward references lower cleanly.)

The same applies inside a shared module: a `share(all)` function may call a
sibling `share(all)` helper declared later in the same module file (the helper
must still be shared and imported by consumers, per the module contract). See
[§3.19](#319-modules-and-imports).

### 3.4.1 Parameter passing: value, borrow, and ownership

Passing an argument is not one thing: the semantics depend entirely on the
declared parameter type. Three distinct ideas — **value copy**, **borrow**,
and **ownership** — are easy to blur, and Vyb makes each explicit.

**Value parameters (`x<T>`), the default.** The argument is *copied* into the
callee's parameter; the callee works on its own copy and the caller's variable
is unaffected. Copying a value copies *it*; it does not recursively duplicate
everything the value references. What that means in practice:

- A plain scalar or a `Vec<T>` parameter is copied; `Vec` elements are
  **deep-copied**, so the callee owns independent buffers (passing a large `Vec`
  by value therefore copies it on every call — a real cost; prefer a `their`
  borrow when you only read/mutate a collection).
- A struct with owned fields (e.g. one that holds a `Vec`) is **deep-copied** on
  entry, so the callee and caller each own independent data.
- A `String` parameter shares its backing buffer (refcounted at the binding
  level) rather than duplicating the text.
- An `our<T>` parameter takes a shared strong reference (+1 refcount) rather than
  copying the payload.
- An `fn`/closure parameter retains the closure environment.

**Borrowed-reference parameters (`their<T>`).** A `their<T>` parameter does *not*
copy: it is a non-owning reference to the caller's value. `borrow(x)` supplies a
mutable `their<T>`; writes through it are visible to the caller. `view(x)` supplies
a `their<T const>` reference for reading. Borrows are scoped to the caller's
lifetime and must not escape it (a borrow-escape is rejected). (Note: the `const`
qualifier expresses read-only *intent*; const write-through on field assignment is
not yet hard-enforced by the compiler.)

```vyb
struct Counter { count<Int> }

# Value: `c` is a copy — bump() does NOT change the caller's counter.
bump(c<Counter>)<Int> -> { c.count = c.count + 1; return c.count }

# Borrow: `c` aliases the caller's counter — add() DOES change it.
add(c<their<Counter>>, n<Int>)<Int> -> { c.count = c.count + n; return c.count }

# Read-only view: `c` is a non-owning read-only reference.
peek(c<their<Counter const>>)<Int> -> { return c.count }

main()<Int> -> {
    a<Counter> = Counter { count = 5 }
    bump(a)             # a.count stays 5   (value copy)
    add(borrow(a), 3)   # a.count becomes 8  (mutable borrow, visible)
    n<Int> = peek(view(a))  # 8              (read-only view)
    println_int(a.count)    # 8
    return 0
}
```

Three things are easy to conflate and are different:
- **Mutating the referenced object** — a `their`-borrow write such as
  `c.count = …`.
- **Reassigning the callee's local handle** — giving the parameter `c` itself a
  new binding inside the callee (a local rebind; it does not touch the caller).
- **Replacing the caller's variable** — not something a borrow can do; that
  requires ownership and a `my` hand-off.

**Ownership parameters (`my<T>` / `our<T>`)** answer *who owns and frees* a
value, not *how it is passed*; neither is a synonym for a reference. `my<T>` is
unique ownership. Passing a **named** `my` owner into a `my` parameter **moves**
it: ownership transfers, the callee cleans it up exactly once, and the caller's
variable is left unusable (use-after-move is rejected). A fresh `my(...)` built
inline as an argument is reclaimed after the call. `our<T>` is shared,
ref-counted ownership: an `our` parameter takes a strong reference and the
payload is freed when the last reference drops.

**In short:** plain `x<T>` = your own copy; `their<T>` + `borrow` = read/mutate in
place with no copy; `their<T const>` + `view` = read-only; `my<T>` = unique-owner
hand-off (a move); `our<T>` = shared state.

### 3.5 Closures and lambdas

A lambda is `|| -> expr` (zero args) or `|x| -> expr`:

```vyb
double = |x| -> x * 2                     # single expression
block   = |x| -> { y<Int> = x + 1; return y * 2 }  # block body
```

Captures are explicit and typed at the call site. `fn()`-typed parameters
accept zero-capture closures (as used by the threading modules). Richer
capture forms (by-value, by-reference) are supported for closure parameters
in the collection methods (`map`, `filter`, `reduce`).

```vyb
xs.map(|x| -> x * 2)                    # fn(T) -> T
xs.filter(|x| -> x > 3)                 # fn(T) -> Bool
xs.reduce(0, |acc, x| -> acc + x)       # fn(T, T) -> T
```

### 3.5.1 Capture forms

Closures capture outer state, and the capture form follows the ownership of
the captured variable:

```vyb
# By-value (plain/scalar): a snapshot at closure creation time.
base<Int> = 10
adder = |x<Int>| -> x + base

# Returned closures keep their own environment.
make_adder(base<Int>)<fn(Int) -> Int> -> { return |x<Int>| -> x + base }

# Mutable capture: the closure reads/writes the outer variable each call.
counter<Int> = 0
bump<fn(Int) -> Int> = |u<Int>| -> counter = counter + 1   # persists

# our<T>: shared capture — read and write-through the shared state.
shared<our<Int>> = our(7)
read<fn(Int) -> Int> = |u<Int>| -> shared + 3

# my<T>: move capture — ownership transfers into the closure env.
mine<my<Counter>> = my(Counter { n: 8 })
readMine<fn(Int) -> Int> = |x<Int>| -> mine.n + x
```

- A **`my<T>` move-capture** transfers ownership into the closure; reading the
  variable afterward is a **use-after-move** error.
- A **`their<T>`/`view<T>` borrow capture** reads through the held reference
  without ownership.
- A closure that captures an owned struct **owns its payload**: the captured
  `String`/`Vec` fields stay alive even after the maker's scope exits (the env
  keeps a reference to the owned buffer).
- `|| -> …` is the zero-arg form; `|| -> { … }` a zero-arg block body; a
  `failable` body can `fail` and the caller `trap`s it.

Async closures are the [§3.23.2](#3232-async-closures-as-parameters) async-closure form (`async |x| -> await f(x)`).

### 3.6 Control flow

`if`/`while` are expression-ish and readable; `break`/`continue` work in
loops.

```vyb
if (ready) { go() } else { wait() }

while (i < n) {
    i = i + 1
    if (skip) { continue }
    if (done) { break }
}
```

**`for` loops** come in several forms. Parentheses are mandatory:

```vyb
for (x in v) { println(x) }        # plain Vec
for (i in 0..10) { … }             # inclusive range 0..10
for (x in v.iter(), 2) { … }       # every 2nd element (step)
```

`for (x in <expr>)` desugars onto `.next()` for **any** non-identifier
iterable — including custom `bind Iterator` types — so `v.iter()`, a
`VecIter`, or a user type all work identically. An optional step
(`, 2`) advances by a stride.

### 3.7 Pattern matching: `match` and `select`

`match` is the **statement-first** form (its arms can `return` from the
enclosing function, but it also works as a value expression); `select` is the
**expression-first** form that yields a value. See *Choosing between `match`
and `select`* at the end of this section.

```vyb
# match as a statement: side effects + early return from the function
describe_http(code<Int>)<String> -> {
    match (code) {
        200 -> { return "ok" }
        404 -> { return "not found" }
        ? -> { return "unknown" }
    }
}
```

```vyb
# match as a value expression: the matched arm's value becomes the result
grade(score<Int>)<String> -> {
    return match (score) {
        >= 90 -> "A",
        >= 80 -> "B",
        >= 70 -> "C",
        ? -> "F"
    }
}
```

```vyb
# select returns a value
status<String> = select(code) -> {
    200 -> "ok",
    404 -> "not found",
    ? -> "unknown"
}
```

```vyb
# larger arm bodies use pass (returns from the block, not the fn)
result<Int> = select(n) -> {
    1 -> 10,
    ? -> { v<Int> = n * 100; pass v }  # pass carries the arm value
}
```

- **Naked expressions** (`1 -> 10`) auto-return without `pass`.
- **Complex blocks** (`{ … }`) need an explicit `pass value`.
- **Type inference** comes from the first arm; the `?` wildcard gives the
  default.

**Comparison patterns** (range matching) work on `Int`/`Float`:

```vyb
tax<Float> = select(income) -> {
    < 10000 -> 0.0,
    <= 50000 -> 0.2,
    > 50000 -> 0.4,
    ? -> 0.0
}
```

The compiler **rejects unreachable patterns** — wildcard before an earlier
arm, duplicate patterns, and overlapping ranges are compile errors:

```vyb
match (x) { ? -> "any", 5 -> "five" }   # ERROR: '?' must be last
match (x) { > 10 -> "a", > 10 -> "b" }  # ERROR: duplicate
match (x) { > 5 -> "a", >= 3 -> "b" }   # ERROR: overlapping ranges
```

**Set patterns (`{ … }`)** group several discrete values into a single arm so
callers don't repeat a result per value. The arm matches when the target equals
*any* element, and first-match-wins arm ordering is preserved:

```vyb
parity<String> = select(n) -> {
    {1, 3, 5, 7, 9} -> "odd",
    {2, 4, 6, 8}    -> "even",
    ?               -> "out of bounds"
}
```

Set elements are **literals** (`Int`/`Float`/`String`/`Bool`/`null`) or **bare
enum-variant names** (`North`, `Shape::Square`). Comparison patterns,
payload-binding variants (`Circle(r)`), arbitrary expressions, and empty `{}`
are rejected at compile time. Every element must share the select target's
type, and the `?` wildcard must remain the last arm. A value listed in more
than one arm is claimed by the earliest arm (no error). Enum-variant elements
count toward `select` exhaustiveness checks on tagged-union enums.

**Choosing between `match` and `select`.** The effective difference is
**expression vs. statement**, and everything else follows from it:

- **`select` is an *expression***: it produces a value, so it can appear
  anywhere an expression can — assigned to a variable, used as an operand, or
  nested as the arm of an enclosing `select`. A result leaves the arm as a
  naked expression (`1 -> 10`) or via `pass value`, which returns from the
  arm only, not the enclosing function.
- **`match` is *statement-first*:** its statement form is about control flow —
  arms `return`/`break` out of the enclosing function or loop, so it suits an
  early-exit dispatch. It can also appear in *expression* position
  (`let r = match (x) { ... }` or `return match (x) { ... }`), where the
  matched arm's value becomes the result. Reach for it when you want richer
  patterns (below) or an early exit.
- **Pattern coverage differs.** Both accept `?`, comparison patterns
  (`>= 90`), literals, and enum-variant payloads (`Circle(r)`). `match` alone
  adds range patterns (`5..10`), struct destructuring (`Point { x, y }`), and
  guard clauses (`pattern if cond`). `select` alone adds brace set patterns
  (`{1, 3, 5}`) and `pass`.
- **When they converge.** `select` also has a bare-statement form — block arms
  without `pass` just fall through — so for pure side-effect dispatch (as in
  event loops) the two are equivalent. Code tends to favor `select` there
  because it is the one that also composes into a value, so the same idiom
  scales from replacing `if`/`else` chains to returning a computed result.

In short: `select` is pattern matching as an expression (value-first, with
`pass` and set patterns); `match` is the control-flow-first form (with richer
destructuring and guards), and both can appear as statements or value
expressions.

### 3.8 Tuples and variadic tuples

Tuples are value sequences useful for multi-return and heterogeneous groups:

```vyb
t = (1, 2)                    # two-element tuple
u = (1, "x", true)            # mixed types
v = (1, 2, 3, 4, 5, 6, 7)     # variadic
```

Element membership is accessed by zero-based index, and length via `.len()`:

```vyb
data<Tuple<Int, String, Bool>> = (1, "x", true)
num<Int>   = data[0]   # 1
text<String> = data[1] # "x"
ok<Bool>   = data[2]   # true
size<Int>  = data.len()# 3
```

The types are static, so an explicit `name<Type>` annotation or `auto`
inference (constant index) works. A caller can also pull members out without
naming types by tuple destructure: `num, text, ok = data`.

### 3.9 Structs

```vyb
struct Point {
    x<Float>
    y<Float>
}

p<Point> = Point { x = 1.0, y = 2.0 }
p.x = 3.0                        # field access (mutable receiver/owner)
```

Structs hold data only; behavior lives in **binds** ([§3.15](#315-aspects-and-binds-polymorphism)). Struct fields may
be owned, `their`-borrowed, or typed generically (`struct Pair<K, V>`).

### 3.10 Enums

Value-less enums and **constant enums** (each member a compile-time value) are
both supported. The constants-style enums power C-shaped interfaces:

```vyb
enum Color {
    RED
    GREEN
    BLUE
}

# constant enum (value = the numeric constant)
enum FileFlag {
    READ = 1
    WRITE = 2
    RDWR = 4
    CREATE = 128
    TRUNC = 512
    APPEND = 1024
}

flag<Int> = FileFlag::READ | FileFlag::WRITE   # combine with |
```

Constant members can be combined with the bitwise `|` — this is exactly how
`Socket::AF_INET`, `SOCK_STREAM`, and `IPPROTO_TCP` and the `FileFlag` set are
used in `network` and `io`.

### 3.11 Operators

```vyb
# Arithmetic / comparison
a + b, a - b, a * b, a / b, a % b, a == b, a != b, a < b, a > b, a <= b, a >= b

# Bitwise on integers (all Int/UInt widths)
a | b      a & b      a ^ b      ~a      a << b      a >> b
a |= b     a &= b     a ^= b     a <<= b     a >>= b    # compound-assign

# String concatenation (auto-converts numerics)
s = "Count: " + 42
```

Bitwise operators require matching widths; literals adapt to the operand type.
Build byte compositions with unsigned widths and `as` ([§3.12](#312-casts-with-as)), e.g. packing
four `UInt8` bytes into a `UInt32`.

### 3.12 Casts with `as`

`as` converts between numeric widths and signedness with defined semantics:

```vyb
s<Int8> = -6
n<Int64> = s as Int64          # signed widen: sign-extends -> -6
u<UInt8> = 200
p<Int>   = u as Int            # unsigned widen: zero-extends -> 200
w<Int>   = 300
q<Int8>  = w as Int8           # narrowing: 300 & 0xFF = 44
```

- Widening **signed → signed** sign-extends; **unsigned → signed/wider**
  zero-extends. Use unsigned casts when packing bytes so you never
  sign-extend a high bit (`0x80` stays `0x80`, not `0xFF…80`).
- Narrowing truncates to the destination width.
- Projection from a wider to a narrower byte lane and `as Int8` of a computed
  expression require an explicit cast (the compiler won't silently narrow).

### 3.13 Ownership and accessors

Vyb's ownership model types how a value is held. The canonical accessor words
are `my`, `our`, `their`, plus the `mild`/`soft` observers.

| Accessor | What it means |
|---|---|
| `my<T>` | unique ownership (like `Box`): you own and must free it |
| `our<T>` | shared, reference-counted ownership (auto `free` on last drop) |
| `their<T>` | a borrowed, non-owning reference to someone else's value |
| `mild<T>` | a weak reference (made with `soft(x)`); won't prevent cleanup |
| `soft(x)` | constructs a `mild<T>` from an `our<T>` |
| `view(x)` / `borrow(x)` | borrow a value -> `their<T>` non-owning reference |

Relevant syntax and helpers:

```vyb
counter<Counter> = Counter { count = 0 }
increment(c<their<Counter>>)<Void> -> { c.count = c.count + 1 }
increment(borrow(counter))          # pass a borrowed reference

shared_data<our<Counter>> = our(Counter { count = 1 })
shadow<mild<Counter>> = soft(shared_data)   # weak reference
v<our<Counter>?> = shadow.grab()            # upgrade; nil if freed
if (shadow.released()) { … }                # true once destroyed
```

- **Primitives unwrap on read.** Reading a primitive-owned value yields the
  value directly; no allocation or ceremony is required to use it.
- **`my` move semantics.** An owned value moves on transfer; temporary-owner
  paths enforce that you don't leave a dropped owner behind.
- **`our` handles memory for you.** When the last strong reference leaves
  scope the refcount hits zero and cleanup runs (`grab()` then returns absent,
  `released()` becomes true).
- **By-reference receivers (`their<Vec<T>>`).** Mutating collections in place
  is done through `their` receivers so no copies occur —
  e.g. `map_in_place(self<their<Vec<T>>>, …)`.

**Lexical enforcement (what the compiler actually checks).** These rules are
lexical and scope-based, not a general lifetime/region model:
- **Borrows do not escape.** A `their<T>` value is valid only while the scope
  that recorded its `borrow()`/`view()` is still open. Reading or reassigning it
  outside that scope — or returning a borrow of a function-local (as opposed to
  a parameter the caller owns) — is rejected as a borrow-escape error.
- **No mutation while borrowed.** Assigning to a value with an active borrow is
  rejected, as are overlapping mutable borrows.
- **`my<T>` moves; use-after-move is rejected.**
- **Thread boundary.** A `thread_spawn` closure must not capture `my<T>` or
  `their<T>` state; only ref-counted `our<T>` or by-value copies may cross a
  thread boundary.

`mild<T>` exists to break reference cycles (tree parents, observer
registries, caches) without preventing cleanup — see
`doc/OWNERSHIP_MILD.md` and `test/ownership/mild_test.vyb`.

The runtime's freeing is *deterministic* (refcount/scope-driven), and
correct teardown of owned structs and transferred values is part of the
stdlib contract (see `runtime` pages and the test suite).

### 3.14 Generics and monomorphization

Generic functions and types are monomorphized — a new specialized copy is
generated per instantiation, so dispatch has no runtime cost:

```vyb
fn first<T>(v<Vec<T>>)<T> -> { return v.get(0) }
```

Generic type parameters can be bounded by aspects with the angle-bracket form:

```vyb
cmp_lt<T<Comparable>>(a<T>, b<T>)<Bool> -> { … }
```

Bound aspects read `<T<Aspect>>` (the modern spelling, replacing the older
`T: Aspect`). This powers the collection APIs:

```vyb
struct HashMap<K, V>
bind<K<Hashable, Equatable>, V> MapOps -> HashMap<K, V> { … }
```

### 3.15 Aspects and binds (polymorphism)

**Aspects** are behavior contracts — method signatures with no state. **Binds**
connect an aspect to a concrete type, implementing the methods against that
type's data. This is Vyb's polymorphism: data is dumb, behavior is bound.

```vyb
aspect Drawable {
    draw(self)<String> -> { }
    area(self)<Float> -> { }
}

struct Circle { r<Float> }

bind Drawable -> Circle {
    draw(self)<String> -> { return "circle" }
    area(self)<Float> -> { return 3.14159 * self.r * self.r }
}
```

- An aspect can refine another: `aspect Comparable : Equatable { … }`.
- A generic bind maps an aspect onto every instantiation:
  `<K<Hashable, Equatable>, V>`, `<T>`, etc. (`Iterator -> VecIter<T>`,
  `MapOps -> HashMap<K, V>`).
- The **core aspects** — `Display`, `Debug`, `Clone`, `Equatable`,
  `Comparable`, `Hashable`, plus `StringOps` and `Iterator` — are bound to the
  primitives and used as generic bounds throughout the stdlib (see [§4.1](#41-core-contracts-math-prelude)).
- A bind's methods write against `self` (the bound type's fields) exactly like
  instance methods. Calling `account.transfer(...)` dispatches through the
  type's bound aspect.

This "aspect + bind" pair is the language's answer to interfaces/traits: type
safety and dispatch at compile time, with the freedom to bind multiple aspects
to one type.

### 3.16 Error handling: `trap` / `fail` / `ensure`

Functions that can fail return an `(T, error)` shape (the value plus an error
handle/`error_ptr`); at the LLVM level this is `{ i64 result, i8* error }`.

**`fail`** raises an error; **`trap`** catches it; **`ensure`** always runs.

```vyb
div(a<Int>, b<Int>)<Int> -> {
    if (b == 0) { fail "division by zero" }
    return a / b
}

main()<Int> -> {
    result = trap div(10, 0) { "caught" }
    ...
}
```

- `trap` with a single error type, multiple error types via pattern matching,
  a wildcard catch, or a union of types are all supported.
- On error the matching handler runs, then the `ensure` block last, on both
  the success and failure paths:

```vyb
handle() -> {
    do_work()
} ensure -> {
    cleanup()          # always runs
}
```

- Errors can carry rich context (`struct Error { … }` field sets), and
  `fail` accepts primitives or structured values.
- **Untrapped errors** propagate up the call chain; the harness and the
  serialization path treat them explicitly.

#### Optional (`T?`) shapes and escalation

Fallible openers (e.g. `stdlib/io`'s `open_read`) return a native optional
(`File?`): **absence *is* the failed open**. There is no sentinel `-1` or
`fd == -1` to test — a `File` is only ever constructed valid, so failure flows
from taking the `?` seriously rather than comparing descriptors. Unwrap with
`else` for a local default, or `match` the absent arm to escalate.
The operations on the opened value follow the same shape: `read_all` returns
`String?`, `write_str` returns `Int?` (bytes written), and `close` returns
`Bool?` -- absence *is* the failed call, never a `-1` to compare.

```vyb
import io::{open_read, read_all, close, io_error, IoError}

# `else` is lazy: the default only evaluates when the optional is absent, so a
# side-effecting default does not run on the happy path.
mk_default()<Int> -> { return -1 }
a<Int?> = Int?(7)
r<Int> = a else mk_default()     # present -> r = 7, mk_default NOT called

# `stdlib/io` ships an `IoError` plus an `io_error(...)` convenience that
# snapshots the last diagnostic, so there is no user-defined error struct.
# Escalate "absent" into a fail, letting a caller trap/recover:
open_data(path<String>)<String> -> {
    match (open_read(path)) {
        f -> {
            s<String> = read_all(f) else ""
            close(f)
            return s
        }
        ? -> {
            fail io_error("open", path)
        }
    }
}

main()<Int> -> {
    {
        s<String> = open_data("config.toml")
        println(s)
    } trap (e<IoError>) -> {
        println("could not open " + e.operation + " " + e.path + ": " + e.message)
    }
    return 0
}
```

Notes:

- `fail` is a **statement**, so a bare `x else fail<…>(…)` does not parse;
  shape the absent arm with `match (opt) { v -> …, ? -> … }` and `fail` there.
- Re-raising from inside a `trap` handler propagates to the next enclosing
  handler (the catch → report → refail shape), and `refail` re-raises the
  caught error untouched.

#### Opaque handles and `-1` sentinel conventions

Two conventions keep the stdlib's failure surfaces consistent once the `T?`
migration is complete.

**Opaque handle-creators return `Int?`.**

Functions that allocate a lightweight, index-based handle (no structured value
to carry) signal allocation/pool failure with absence, not a sentinel. This
applies to the plain-`Int` handles with no dedicated wrapper struct: mutexes
(`mutex_new`), condvars (`cond_new`), atomics (`atomic_new`), the curses
driver (`curses_init`), and Qt widget handles (`qt_*_create`).

The raw channel constructors
(`chan_new`/`chan_bounded`, `strchan_new`/`strchan_bounded`) and the agent
`agent_start*` family now return `Int?` too (absent on allocation/spawn
failure). The compiler-native typed `chan<T>` constructor already speaks `T?`,
so typed channels are the recommended path where a typed payload fits.

```vyb
m = mutex_new() else fail …            # pool exhausted → absent
```

The numeric type stays `Int` because the handle *is* an index; absence is the
failure. Structured, method-carrying resources are the exception — they get a
`T?` wrapper value (`File?`, `TcpStream?`, `TlsStream?`, `TlsContext?`) instead
of a bare `Int?`, because their operations attach to the value itself.

**`-1` as a search/parse "not found" result is intentional, not an error.**

Where `-1` means "no match" / "not a valid number" / "no key pressed" — a value
result, not a failed call — it is kept as `Int` and does *not* become `T?`.
Examples: `String::index_of`, `utf8_index`/`utf8_at`, the `http_*` parse
helpers (`http_index_of`, `http_parse_int`, `http_parse_hex`,
`http_status_code`), `regex_match` (`0`/`1`), and `curses_getch` (`-1` on
timeout). These keep the idiomatic `if (idx >= 0)` / `while ((ch = getch()) !=
-1)` test and avoid conflating "no match" with "the call failed". The rule of
thumb: **absence (`?`) means the operation failed; a sentinel value means the
operation ran but found nothing.** This carve-out is reserved for sentinels that
are *in-domain* for the function (an index, a keycode, a computed result).
Dimension/measurement getters -- `curses_rows`/`curses_cols`, `qt_window_width`/`qt_window_height`, `qt_screen_width`/`qt_screen_height`/`qt_screen_dpi` -- never meaningfully return `-1`, so they use `Int?` (absent on a bad handle, no screen, or an inactive screen) instead of a `-1` sentinel.

**Which stdlib modules use which shape.** The 0.7.3 batch pushed every
programmer-facing failure surface to native optionals:

| Module | Shapes |
|--------|--------|
| `io` | `open*` -> `File?`; `read_all` -> `String?`; `write_str` -> `Int?`; `close` -> `Bool?` |
| `term`, `env` | enabling/`set` ops -> `Bool?`; stderr writers -> `Int?`; ANSI emitters `term_clear`/`term_move_cursor`/`term_hide_cursor`/`term_show_cursor` -> `Bool?` |
| `network` (incl. oracle output) | acquisition -> `TcpStream?`/`TcpListener?`/`UdpSocket?`; `socket_*` (`Int?`/`Bool?`/`Int?`-bytes); recv surfaces `udp_recv_from`/`recv_from`/`async_tcp_read`/`async_udp_recv_from` and wrapper `TcpStreamOps::read` -> `String?`; last-peer probes `udp_last_peer_ip`/`udp_last_peer_port` -> `String?`/`Int?` |
| `tls`, `https` | `tls_stream`/`tls_client_context` -> `TlsStream?`/`TlsContext?`; `https_get_full*` -> a **present** `HttpResponse` whose `error<String?>` carries the lossless failure reason (`status -1` on failure); body wrappers `https_get`/`https_get_verified` -> `String?`; diagnostics `https_selfhost`/`https_selfhost_verified` -> `Bool?` |
| `asyncs` | `async_sleep_ms`/`async_connect` -> `Bool?`; `async_recv` -> `String?`; `async_send` -> `Int?`; `async_spawn`/`async_poll`/`async_accept` -> `Int?` |
| `curses` | `curses_init` -> `Int?`; draw/attr/window ops -> `Bool?`; `curses_rows`/`curses_cols` -> `Int?`; `curses_getch` + color/attr probes stay `Int` |
| `agents` | `agent_start*` -> `Int?` (absent on spawn failure); `agent_send*`/`agent_close`/`agent_free`/`agent_dead_letter` -> `Bool?`; probes stay `Int`/`String` |
| `channels` | typed `chan<T>` speaks `T?`; raw `chan_new`/`chan_bounded`, `strchan_new`/`strchan_bounded` -> `Int?` (absent on allocation failure); `chan_close`/`chan_free`, `strchan_close`/`strchan_free` -> `Bool?` |
| `threads`, `tasks` | `mutex_new`/`cond_new`/`atomic_new` -> `Int?`; lock/cond/atomic/task ops -> `Bool?`; `thread_join` -> `Int?`; value probes stay `Int` |
| `time` | `time_epoch_secs`/`time_epoch_millis`/`time_nanos` -> `Int?` (absent on a clock error); `time_mono_millis` stays `Int` |
| `qt` | creators AND dimension/DPI getters -> `Int?`; op-status -> `Bool?`; `qt_msg_question` -> `Int?` (absent when the GUI is not running); modal pickers `qt_file_open`/`qt_file_save`/`qt_dir_select` and `qt_dlg_selected` -> `String?` (present-empty/user-cancel, absent on no-GUI / no-result); value/probe getters stay `Int`/`String`/`Bool` |

The canonical per-module detail lives in `doc/FEATURE_STATUS.md` ("Standard
Module Error Handling").

### 3.17 Strings

`String` is a fat pointer (data + length), immutable, with runtime bounds
checking. Literal assignment and concatenation are natural:

```vyb
s = "hello"
t = s + " world"          # allocates a new String
n<Int> = "hello".len()     # methods on literals
c<String> = "abc"[1]       # bounds-checked indexing ("" out of range)
```

**Escape sequences.** String literals process backslash escapes at lex time. An
unknown escape (one not in the table) is kept verbatim — backslash plus the
character — so regex and path strings like `"\."` or `"C:\work"` need no doubling.

| Escape | Meaning |
|---|---|
| `\n` | line feed `0x0A` |
| `\r` | carriage return `0x0D` |
| `\t` | horizontal tab `0x09` |
| `\\` | a literal backslash |
| `\"` | a literal double quote |
| `\'` | a literal single quote |
| `\0` | NUL `0x00` |
| `\xHH` | a literal byte from two hexadecimal digits |

`"\r\n"` is therefore a two-byte CRLF pair — real web requests rely on this.

**Core method family** (all work on value receivers and non-identifier
receivers, e.g. `"Hello".to_upper()`):

```vyb
s.len()                     # length
s.substring(a, b?)          # substring (end optional)
s.get(i)                    # bounds-checked String (single char) at i
s.char_at(i)                # the ASCII code (Int) at i, 0 if out of range
s.starts_with(p) / s.ends_with(p) / s.contains(sub)
s.index_of(needle)          # first occurrence index; -1 if absent
s.to_upper() / s.to_lower() # ASCII case, allocates new String
s.trim() / s.split(sep)     # Vec<String>
s.format(...)               # "{}" interpolation: "{a}-{b}".format(a, b)
s.to_int() / s.to_float()   # parse to a number
String::from_bytes(ptr, len)# build a String from C bytes (FFI)
```

Read-only operations allocate nothing; transforms allocate a fresh `String`.
`+` concatenation and chaining work across mixed types (`"Count: " + 42`).
Index operations are bounds-checked and return safe defaults
(`""` for out-of-range).

**C interop.** `c_str` (`…` NUL-terminated view) lets you pass `String`s to C
`printf`, `strlen`, `strcmp`, `strstr`, etc. via the FFI.

### 3.18 Collections

All collections are `Vec<T>`-family structs with generic **aspect-bind**
methods and monomorphized internals.

```vyb
v<Vec<Int>> = Vec()          # empty, growable
v2<Vec<Int>> = Vec(8)        # preallocate capacity
v.push(1)
v.push(2)
x<Int> = v.get(0)            # bounds-checked
v.len()                      # element count
v.isEmpty()                  # true when empty
v.iter()                     # -> VecIter<T> (Iterator bind)
```

Higher-order forms (`VecHigherOps -> Vec<T>`):

```vyb
v.map(fn(Int)->Int)          .map(|x| -> x*2)
v.filter(|x| -> x > 3)
v.reduce(init, |a,b| -> a+b)
v.map_in_place(self<their<Vec<T>>>, |x| -> x+1)
v.retain(self<their<Vec<T>>>, |x| -> keep)
```

Ordered and associative maps:

```vyb
m<HashMap<String, Int>> = HashMap()
m.put("a", 1)
m.get("a")                 # 1 or default
m.contains("a"); m.keys(); m.values(); m.len(); m.iter()

h<HashSet<String>> = HashSet()
s<BTreeMap<String, Int>> = BTreeMap()   # ordered (Comparable+Equatable keys)
```

`HashMap` requires `K<Hashable, Equatable>`; `BTreeMap` requires
`K<Comparable, Equatable>`. Enumerating key/value pairs goes through the
iterator types (`MapIter<K,V>`, `HashIter<K>`, `BTreeIter<K,V>`).

Range/`Vec` iteration, `for (x in m.iter(), step)`, and the `Iterator`
protocol are all unified in [§3.6](#36-control-flow).

### 3.19 Modules and imports

**`import`** pulls verified symbols in; **`share(all) import`** re-exports a
foreign symbol from the importing module; the compiler resolves the full
namespace/closure and the refman's `import` edges model it.

```vyb
import network::{socket_open, socket_bind, socket_listen}
share(all) import tls::{TlsStream}          # re-export to my consumers
import https::{HttpResponse}                # narrow symbolic import
```

- **Module identity** is the namespace directory. `core::iter::{Iterator}`
  addresses a file+symbol inside the `core` namespace; `error` is its own
  multi-file namespace.
- **`prelude` is auto-imported.** The compiler injects the core contracts
  (`Display`, `Debug`, `Clone`, `Equatable`, `Hashable`, `Comparable`) and
  `prelude_ok` into every module unless that module imports the contracts
  itself or locally redefines one of the core aspects.
- **No cross-module leakage.** A user module must go through an explicit
  import; the resolver enforces per-module scope (an `http` module cannot call
  a `vyb_*` runtime directly unless it imports, and the stdlib itself uses
  `network` rather than reaching under it).
- **`smuggle`** (a looser, out-of-band import) is a separate, explicit
  escape hatch — not a default, and never implied by `import`.
- **`share(all)`** on a declaration exports it for consumers.

The refman's `index.md` and each module page show the real import graph,
including re-exports (e.g. `https` re-exports `HttpResponse` from `http`).

### 3.20 FFI and the native bridge

- **`extern "C"` + typed C aliases** let you call C functions; Vyb types map
  onto C (`String` ↔ `char*` via `c_str`, `Int` ↔ `i64`, structs ↔ `struct`).
- **`bindgen`** generates Vyb declarations from C headers (including
  preprocessor/macros, `#define` constants, `u`/`union` types, and function
  pointers). The `.ll` fluff is not committed; macros namespace to the header
  file so different headers can't collide (e.g. `MAX_BUFSIZE` under its own
  namespace).
- **Runtime intrinsics** — `vyb_*` calls from stdlib map 1:1 onto
  `runtime/vyb_runtime.c` (`__vyb_*`). `docs/refman/runtime.md` lists every
  intrinsic the stdlib uses, with the C source line each call resolves to.
- **`freedom` blocks** allow raw `loc<T>` pointers and relaxed guarantees;
  `at(ptr)` dereferences, `from<loc<T>>()` converts.

### 3.21 Serialization

Structs auto-serialize to JSON, as do the success-path values returned by
fallible (`fail`/`trap`) functions:

```vyb
struct User { name<String>, age<Int> }
main()<User> -> { return User { name = "a", age = 1 } }
# output: {"name":"a","age":1}
```

Automatic serialization covers structs, maps, Vecs, and nested values; the
refman/module docs index the supported type set.

Serialization is **bidirectional and lossless at arbitrary depth**. A struct's
`.to_string()` emits JSON through a growable buffer (no fixed-size cap, so long
strings, wide `Vec<T>`, and deeply nested structs are never truncated), and a
`T::from_string(json)` pass rebuilds the full value — including owned `String`
and `Vec` payloads and `Vec<struct>` arrays of objects. The serializable set is
primitives, `String`, structs, `Vec`s, maps, tuples, and data enums — **with the
exception of `fn` and `Self` types**, which do not serialize and are excluded from ser/deser.

```vyb
struct Point { x<Int>, y<Int> }
struct Track { label<String>, points<Vec<Point>> }

main()<Int> -> {
    t<Track> = Track { label = "loop", points = Vec() }
    t.points.push(Point { x = 1, y = 2 })
    j<String> = t.to_string()          # {"label":"loop","points":[{"x":1,"y":2}]}
    u<Track> = Track::from_string(j)   # lossless round-trip, incl. owned payloads
    return u.points.get(0).y == 2 ? 0 : 1
}
```

**Deserialization safety.** `T::from_string()` trusts neither lengths nor
counts found in the input: `Vec<T>` capacity grows only as elements are actually
parsed (never from a declared size), every allocation is OOM-guarded, owned
`String`/`Vec` slots are zero-initialized before fill, and unknown fields are
skipped rather than written out of bounds. Hostile or malformed JSON therefore
cannot trigger an allocation bomb from a large declared length.

`flush`/`Flush` control output behavior in CLI contexts.

### 3.22 Introspection

Vyb exposes runtime and compile-time introspection: type-of, shape/arity
probes, and module-surface status functions (e.g. `io_status_message`,
`collections_status_message`, `result_status_message`, `prelude_ok`) used by
the import-surface tests.

### 3.23 Asynchronous programming

Vyb has first-class `async`/`await`: an `async` function returns a `Future<T>`
that runs as a task on the stdlib's **multi-threaded executor** (a pool of
worker threads, one scheduler per CPU core, each running stackful fibers
pinned to its worker). Call an async function to *spawn* the task and get a
`Future<T>`; `await` it to *drive* it to completion.

```vyb
async compute()<Future<Int>> -> { return 42 }

main()<Int> -> {
    future<Future<Int>> = compute()      # spawn (doesn't block)
    value<Int> = await future            # park the caller until done
    return value
}
```

- **`Future<T>` payloads:** `Int`, `Float`, `Bool`, `String`, `Void`. A
  `String` result travels back as an owned heap slot; `Float`/`Bool` ride as
  bit patterns / 0-1 in the task's result slot.
- **`await` forms:** expression (`x = await f()`), nested (`await f(g())`,
  `f(await g())`), as a receiver/operand/argument, and **bare statement**
  (`await f`) for `Future<Void>`.
- **Context:** from `main` `await` parks the caller; inside a worker it
  suspends the fiber (a worker can `await` a child task without blocking).
- **Parameters:** scalar args are snapshotted into the task env; `String`
  retains its buffer (+1), `our<T>` retains its shared block (+1), `Vec<T>` is
  deep-copied, `struct` params are deep-copied (owning String/Vec/our/mild
  fields), and closures retain their capture env — each released by the env's
  per-layout dtor at task cleanup, so owned params safely outlive the caller.
- **Debug:** async paths carry DWARF metadata.

#### 3.23.1 Async lambdas

An **async lambda** `async |x| -> await f(x)` compiles its body as a closure
that runs as a task; calling it returns a `Future<T>` that `await` drives:

```vyb
async process(x<Int>)<Future<Int>> -> { return x * 2 }
async greet(s<String>)<Future<String>> -> { return "hi " + s }

f<fn(Int) -> Future<Int>> = async |x<Int>| -> await process(x)
r<Int> = await f(10)                        # 20

bonus<Int> = 7
cap = async |x<Int>| -> await process(x + bonus)   # immutable capture
g   = async |s<String>| -> await greet(s)          # String-returning
seven = async || -> await process(7)               # zero-arg form
```

Async lambdas keep the same `fn(…) -> Future<T>` type as async functions, so
they can be passed by name into a task that awaits them:

```vyb
async run_gated(f<fn(Int) -> Future<Int>>, x<Int>)<Future<Int>> -> {
    return await f(x)
}
k<fn(Int) -> Future<Int>> = async |x<Int>| -> await process(x)
out<Int> = await run_gated(k, 5)        # passes the async lambda into a task
```

#### 3.23.2 Async closures as parameters

Closures can be passed into async functions; the launcher snapshots the
closure into the task env, retaining its capture environment (+1) so it stays
alive asynchronously and can be invoked from the worker (including nested
`await`, and mixed with `String`/scalar params):

```vyb
make_scorer()<fn(Int) -> Int> -> {
    bonus<Int> = 5
    marker<String> = "!!"
    return |v<Int>| -> (v * 2) + bonus + marker.len()
}

async async_score(f<fn(Int) -> Int>, v<Int>)<Future<Int>> -> {
    return f(v)
}

scorer<fn(Int) -> Int> = make_scorer()
r<Int> = await async_score(scorer, 10)    # 27
```

#### 3.23.3 The executor primitives

`asyncs` (stdlib) exposes the runtime side: `async_spawn`, `async_await`,
`async_poll`, `async_run_all`, `async_yield`, `async_sleep_ms`, and the
network bridges `async_tcp_*` / `async_udp_*` (`network`).

See [the asyncs module](asyncs.md) and [§5](#5-concurrency-and-async-model).

---

## 4. Standard library reference

The stdlib lives in `stdlib/**/*.vyb` and is written entirely in Vyb over a
small C runtime. Each `<module>.md` that this guide links to is generated from
the source and lists **every** exported symbol with its exact signature,
doc text, and relationship list. The per-module digests below are the same
data, grouped for skimming.

### 4.1 `core` — contracts, math, prelude

Module page: [`core.md`](core.md).

**Core aspects** (behavior contracts bound to the primitives and used as
generic bounds): `Display`, `Debug`, `Clone`, `Equatable`, `Comparable`
(refines `Equatable: …`), `Hashable`, plus `StringOps` and `Iterator`.

Primitives get these binds (`Clone->Int`, `Display->String`,
`Hashable->String`, `Comparable->Float`, …), so `"{}".format(x)`, `.to_string()`,
`.hash()`, `.equals()`, and `<`/`>` comparisons all work on `Int`/`Float`/
`Bool`/`String` via the same widening binds.

**Math & helpers:**

```vyb
clamp(value<Int>, lo<Int>, hi<Int>)<Int>      # clamp into [lo, hi]
is_close(a<Float>, b<Float>, epsilon<Float>)<Bool>
hash_chars(s<String>)<Int>
prelude_ok()<Int>                              # surface probe
```

### 4.2 `io` — files

Module page: [`io.md`](io.md). `File` is a small struct (`fd`, `path`) and
all ops live on it.

```vyb
open(path<String>, flags<Int>)<File?>           # flags from FileFlag
open_read(path<String>)<File?>                  # convenience: read
open_write(path<String>)<File?>                 # create/truncate, write
open_append(path<String>)<File?>                # append/create
read_all(f<File>)<String?>                      # whole file; absent on failure
write_str(f<File>, s<String>)<Int?>             # bytes written, absent on failure
close(f<File>)<Bool?>                            # present on success
```

`enum FileFlag` constant members (`READ`, `WRITE`, `RDWR`, `CREATE`, `TRUNC`,
`APPEND`) combine with `|`. `io_status_message()` is the import-surface probe.
The open functions return `File?`: **absence is a failed open** — there is no
sentinel `fd == -1` to test, and `File` is only ever constructed valid. Unwrap
with `else` for a local default, or `match (open(p)) { f -> …, ? -> … }` to
shape the absent arm (e.g. into a `fail`), then diagnose via
`error_message()`.

```vyb
struct IoError { operation<String>, path<String>, message<String> }  # shared
io_error(operation<String>, path<String>)<IoError>  # snapshots error_message()
```

`IoError` is the module-provided error value for the fail/trap framework: build
it with a struct literal or via `io_error(...)`, which fills `message` from the
last diagnostic. Escalate an absent `File?` with `fail io_error("open", path)`
and let a caller `trap (e<IoError>)`. See §3.16.

### 4.3 `term` — terminal and stdin

Module page: [`term.md`](term.md). Interactive console I/O for TUI-style
programs: read from stdin / keypresses, send diagnostics to `stderr`, flush
`stdout`, and control the terminal. Every op crosses a tiny C runtime helper, so
the Vyb surface stays allocation/pointer-free.

```vyb
stdin_read(maxlen<Int>)<String>       # up to maxlen bytes (EOF-able "")
stdin_read_line()<String>             # one line, newline stripped
stdin_isatty()<Bool>                  # true when stdin is a TTY
stdin_raw_enable()<Bool?>             # raw mode: present on success, absent on failure
stdin_raw_disable()<Bool?>            # restore the saved termios
eprint(s<String>)<Int?>               # bytes written, absent on error
eprintln(s<String>)<Int?>
flush()<Bool?>                        # present on success, absent on failure
stderr_flush()<Bool?>
term_cols()<Int>                      # width, 80 fallback
term_rows()<Int>                      # height, 24 fallback
term_clear()<Bool?>                   # clear screen + home (absent on failure)
term_move_cursor(row<Int>, col<Int>)<Bool?>
term_hide_cursor()<Bool?>             term_show_cursor()<Bool?>
```

The stdin readers return a `String` (empty on EOF); errors on the
enabling/emitter ops are reported as an absent `Bool?` (e.g.
`stdin_raw_enable()` when stdin is not a TTY). `print`/`println` writes go to
`stdout`; keep prompts and diagnostics on `stderr` (via `eprint`/`eprintln`) so
they never corrupt a rendered page. `term_status_message()` is the
import-surface probe. See `examples/term_input.vyb` for a cooked-line and
raw-mode walkthrough.

### 4.4 `time` — clocks and sleep

Module page: [`time.md`](time.md). Thin, allocation-free wrappers over
`clock_gettime` / `nanosleep`. The wall-clock epoch getters can fail on a clock
error (`-1` from the runtime), so they return `Int?` -- absent on a clock error,
present holding the value otherwise; `time_mono_millis` has no failure path and
stays `Int`.

```vyb
time_epoch_secs()<Int?>        time_epoch_millis()<Int?>   time_nanos()<Int?>
time_mono_millis()<Int>        sleep_ms(millis<Int>)<Int>
```

### 4.5 `collections` — Vec, Map, Set, BTree

Module page: [`collections.md`](collections.md). Generics are bounded by the
core aspects; reads return `V?` optionals (use `else` to default).

```vyb
# Vec
v<Vec<Int>> = Vec()                # or Vec(n) to preallocate

v.push(x)                          # append
v.pop()?                           # remove last (absent when empty)
v.len()                            # size
v.get(i)                           # bounds-checked read
v.first()                          # first element (optional)
v.last()                           # last element (optional)
v.reversed()                       # fresh copy, reversed
v.sorted()                         # fresh copy, sorted
v.find(x)                          # index of first match as Int? (absent = not found)
v.min()
v.max()
v.sort_in_place()                  # in-place sort
v.reverse_in_place()              # in-place reverse
v.map_in_place(f)                  # in-place transform
v.retain(f)                        # keep matching elements
v.map(f)                           # fresh Vec of mapped values
v.filter(f)                        # fresh Vec of matching elements
v.reduce(init, f)                  # fold to a single value
v.iter()<VecIter<T>>               # iterator
```

```vyb
# HashMap (K<Hashable, Equatable>)
m<HashMap<String, Int>> = HashMap()
m.put("a", 1)
m.get("a") else -1
m.contains_key("a")
m.size()
m.iter()<MapIter<K, V>>

# HashSet and ordered BTreeMap
h<HashSet<String>> = HashSet()
h.insert("x")
h.contains("x")
h.size()
b<BTreeMap<String, Int>> = BTreeMap()   # K<Comparable, Equatable>
```

Iterator types `VecIter`, `MapIter`, `HashIter`, `BTreeIter` bind the
`Iterator` aspect, so they work directly in `for (x in …)` ([§3.6](#36-control-flow)).

### 4.6 `channels` — typed channel primitives

Module page: [`channels.md`](channels.md). Two payload kinds today: `Int`
(`chan_*`) and `String` (`strchan_*`), both unbounded or bounded.

```vyb
ch<Int>    = chan_new()            # unbounded
cb<Int>    = chan_bounded(cap)     # bounded (cap elements)
chan_send(ch, v)<Bool>             # true on success, false if closed/full
chan_recv(ch)<Int?>                # blocking; absent when closed & drained
chan_try(ch)<Int?>                 # non-blocking; absent when empty/closed
chan_len(ch) / chan_close(ch) / chan_free(ch)
chan_select(handles<Vec<Int>>)<Int>             # wait on many channels
```

The channel surface is non-sentinel (§3.16): `chan_recv` and `chan_try` return
a native `Int?` (present holding the value — which may legitimately be `-1` —
absent when there is nothing to read), and `chan_send` returns `Bool`. No
payload is reserved as a sentinel; recover the old `-1`/`""` sentinel behaviour
with `else` (`chan_recv(ch) else -1`, `strchan_try(ch) else ""`).
`chan_recv_opt` / `strchan_recv_opt` are kept as aliases of the (now-lossless)
`chan_recv` / `strchan_recv`. `strchan_*` mirror these over `String` payloads
(`String?` for the maybe-empty cases). `chan_select` returns which handle is
ready; see [§5](#5-concurrency-and-async-model) for its role in the async model.

### 4.7 `threads` — pthread, mutex, condvar, atomics

Module page: [`threads.md`](threads.md). A thread runs a `fn() -> Int`
closure (zero captures in the basic surface).

```vyb
h = thread_spawn(|| -> 40 + 2) else -1   # Int? -- present handle, absent on spawn failure
thread_join(h)<Int> / thread_detach(h)<Int>

m = mutex_new(); mutex_lock(m); mutex_unlock(m); mutex_free(m)
cv = cond_new(); cond_wait(cv, m); cond_signal(cv); cond_broadcast(cv)
a = atomic_new(0); atomic_load(a); atomic_store(a, v)
atomic_add(a, v); atomic_cas(a, exp, des)<Int>; atomic_free(a)
```

`thread_spawn` follows the engine-wide `T?` shape (§3.16): it returns `Int?`,
present holding the thread handle when the thread was created and absent when it
could not be spawned (table full / `pthread_create` failed) -- no `-1` sentinel.
`thread_join`/`thread_detach` and the mutex/cond/atomic ops keep their documented
Int status codes (they are not fallible "open"s).

### 4.8 `tasks` — fire-and-forget threads

Module page: [`tasks.md`](tasks.md). A lighter precursor to the async
executor: each spawn is its own detached pthread returning a handle.

```vyb
t<Int> = task_spawn(|| -> 99) else 0   # Int?: absent only on spawn failure
task_await(t)<Int>      # block until done, return the closure result
task_poll(t)<Int?>      # Int? -- present holding the result once ready, absent while running
task_free(t)<Int>
```

No captures/arguments in the `fn() -> Int` surface; use `asyncs` for
real parallelism with cooperative scheduling. `task_poll` is a native `Int?`
rather than the old `-1` sentinel, so a legitimate result of `-1` is never
collapsed into "not ready"; `task_await` stays a plain `Int` because on a valid
handle it always blocks until a result arrives (which may be any `Int`).

### 4.9 `asyncs` — a real executor (fibers + thread pool)

Module page: [`asyncs.md`](asyncs.md). Each `async_spawn` runs on its own
stack-fiber pinned to a worker (one scheduler per core) and can suspend
cooperatively without blocking its worker:

```vyb
import async
a<Int> = async_spawn(|| -> { async_sleep_ms(40); return 10 }) else 0
b<Int> = async_spawn(|| -> { async_sleep_ms(5);  return 32 }) else 0
async_run_all()<Int>              # drive the pool to completion
async_await(b)<Int>               # await one handle, block the caller
async_poll(t)<Int?>               # Int? -- present holding the result once done, absent while running
async_detach(t)<Int>              # reclaim one finished task early
async_sleep_ms(ms)<Int>           # cooperatively suspend (not thread sleep)
async_yield()<Int>                # round-robin to other fibers
```

`async_spawn` returns `Int?` (absent only when pool setup fails) and
`async_poll` is a native `Int?` (absent while the fiber is still running,
present holding its result, which may legitimately be `-1`) — no more
`0`/`-1` sentinel overloads. `async_await` stays a plain `Int`: on a valid
handle it always blocks until a result arrives.

**Per-task reclamation.** Tasks stay valid across main-thread awaits, so you can
spawn several and await them in any order. `async_detach(t)` reclaims a single
*completed* task early — freeing its fiber stack and captured environment — so a
long-lived program can recycle fibers instead of accumulating one per `spawn`.
Call it only after the task is done (polled or awaited); the handle must not be
used afterwards. A still-running task is marked and reclaimed by its worker as
soon as it completes (a slow fetch self-reaps). Returns `0` on success, `-1` if
already detached, `-3` if a waiter is still attached. `async_run_all()` runs
whatever is left and then reclaims every task; a leftover-task `atexit` hook
stops the worker pool and reclaims anything forgotten.

Non-`Int` futures (`Float`, `Bool`, `String`) are supported
(`Future<T>` async functions, [§3.23](#323-asynchronous-programming)).

### 4.10 `agents` — message-passing units

Module page: [`agents.md`](agents.md). An agent is a unit of concurrency that
owns a mailbox (an unbounded channel by default) and runs a behavior closure on
its own worker thread: it blocks on recv and hands each message to the closure.
Only messages cross the boundary — the agent's internal state stays private to
its behavior. The handle is an `Int` (a runtime table index).

```vyb
import agents
counter = agent_start(|v<Int>| -> println("got " + v.to_string()))
agent_send(counter, 42)          # 1 (accepted)
agent_close(counter)             # drain then stop (lossless)
agent_free(counter)              # wait for the worker and reclaim
```

**Lifecycle.** `agent_send` (non-blocking post; 1 accepted, 0 if closed/full),
`agent_len` (buffered-but-unhandled count), `agent_alive`, `agent_close`
(drain-then-stop, loses nothing), and `agent_free` (join + reclaim; closes the
mailbox first if it was never closed). `agent_mailbox` exposes a scalar agent's
mailbox as a channel handle so it can join `chan_select` for fan-in/fan-out
composition (returns -1 for String agents, which use a separate mailbox type).

**Payloads.** Int, Bool, Float, and String are supported through
`agent_start*` / `agent_send*` (`_bool`, `_float`, `_string`). Bool and Float
ride the agent's int-slot mailbox; String agents use a refcounted mailbox that
hands an owned transfer to the behavior.

**Bounded mailboxes / backpressure.** Pass a second Int argument to bound the
mailbox: `agent_start(behavior, cap)`. A full bounded send returns 0
immediately (non-blocking), so the caller applies its own backpressure — the
same semantics as `chan_bounded(cap)` vs `chan_new()`. 0/omitted stays
unbounded. Once the worker drains, sends are accepted again and nothing is lost.

**Failure channeling.** A behavior that `fail`s is captured rather than dropped:
the agent is marked failed (`agent_status` = 2, mailbox closed, senders see 0).
`agent_error_code` returns a `fail<Int>` payload (else -1), `agent_error` a
`"kind @ file:line"` descriptor, and `agent_dead_letter(a, ch)` can route the
failed agent's handle to a supervisor channel.

### 4.11 `network` — sockets, TCP, UDP

Module page: [`network.md`](network.md). Raw `socket_*` primitives, ergonomic
TCP/UDP wrappers, and `async_*` variants.

```vyb
# TCP - the ergonomic wrappers return a native optional: absence *is* a failed
# op, so callers unwrap with `match` (or `else`) and never probe a sentinel fd.
match (tcp_listen(ip, port, backlog)) {   # TcpListener?
    l -> handle(l)
    ? -> { fail net_error("listen", ip) }
}
match (tcp_connect(ip, port)) {           # TcpStream?
    s -> handle(s)
    ? -> { fail net_error("connect", ip) }
}
c = tcp_accept(l)<TcpStream?>             # absent if the accept fails

# UDP
match (udp_bind(ip, port)) {              # UdpSocket?
    u -> handle(u)
    ? -> { fail net_error("bind", ip) }
}
udp_send_to(u, ip, port, data)
udp_recv_from(u, max)<String>
udp_last_peer_ip()
udp_last_peer_port()

# raw socket (domain/type/protocol from the Socket enum constants)
fd = socket_open(Socket::AF_INET, Socket::SOCK_STREAM, Socket::IPPROTO_TCP)
socket_bind(fd, "0.0.0.0", port)
socket_listen(fd, backlog)
socket_accept(fd)
socket_send(fd, data)
socket_recv(fd, max)
socket_connect(fd, ip, port)
socket_local_port(fd)
socket_set_timeout(fd, ms)      # SO_RCVTIMEO/SO_SNDTIMEO; 0 disables
socket_resolve(host)<String>    # hostname / IPv4 / IPv6 literal -> address
socket_error_code()
socket_error_message()
```

`TcpStream`, `TcpListener`, and `UdpSocket` are structs; their method surface
comes from the matching aspect:

- `TcpStreamOps` — `write` / `read` / `close`, plus `peer_ip` / `peer_port`
- `TcpListenerOps` — `local_ip` / `local_port` / `close`
- `UdpSocketOps` — `send_to` / `recv_from`, plus `local_ip` / `local_port` / `close`

Each high-level wrapper (`tcp_listen`, `tcp_connect`, `tcp_accept`, `udp_bind`,
and the `async_tcp_*` / `async_udp_*` forms) returns a native optional that is
present on success and absent on failure, mirroring `File?` in `io`: the absent
arm shapes the failure via the shared `NetError { operation, target, message }`,
built with `net_error(op, target)` (which snapshots `socket_error_message()`).
Raise it with `fail net_error("connect", ip)` and catch it with
`trap (e<NetError>) -> { ... }`. Because absent already means "failed", a
constructed `TcpStream` / `TcpListener` / `UdpSocket` is always valid — there is
no `is_open()` probe and no `fd == -1` sentinel to test.

`enum Socket` holds the constants `AF_INET`, `SOCK_STREAM`, `SOCK_DGRAM`,
`AF_INET6`, `IPPROTO_TCP`, and `IPPROTO_UDP` used by the raw `socket_*` calls.
IP literals may be dotted-quad ("127.0.0.1") or colon-hex IPv6 ("::1");
`socket_set_timeout` bounds how long a recv/send waits on a non-responsive peer.
The `async_tcp_*` and `async_udp_*` helpers integrate sockets with the async
executor.

### 4.12 `tls` — TLS contexts, sessions, handshakes

Module page: [`tls.md`](tls.md). Layers over OpenSSL; a `TlsContext` is the
`SSL_CTX`, a `TlsStream` is a TLS session on an already-connected fd.

```vyb
# contexts and streams are native optionals -- absence IS a failure
match (tls_server_context(cert_pem, key_pem)) {         # TlsContext?
    ctx -> {
        match (tls_stream(ctx, fd, host)) {             # TlsStream?
            s   -> {
                match (s.connect()) {                   # TlsStream? (accept() server-side)
                    c   -> { tls_write(c, data); tls_read(c, max); tls_close(c) }
                    ?   -> { tls_close(s); fail tls_error("connect", host) }
                }
                tls_free_context(ctx)
            }
            ?   -> { socket_close(fd); fail tls_error("stream", host) }
        }
    }
    ? -> { fail tls_error("context", host) }
}
```

`tls_client_context()`, `tls_client_context_verified(ca_pem)`, and
`tls_server_context(cert_pem, key_pem)` each return `TlsContext?`; `tls_stream`
returns `TlsStream?`; the client handshake is `TlsStreamOps.connect()` and the
server handshake is `TlsStreamOps.accept()`, both returning `TlsStream?`
(present = the connected stream, absent = failure). A handshake failure leaves
the fd owned by the stream, so an absent `connect()`/`accept()` arm should still
`tls_close` the receiver. Low-level `tls_write`/`tls_read`/`tls_close` return
the raw byte/status as before, and `tls_error_code()` / `tls_error_message()`
report the last low-level result.

**Fail/trap shape.** Escalate a `T?` absence into the fail/trap framework with
`fail tls_error(op, host)`, which builds a shared `TlsError { op, host, message }`
snapshotting the last `tls_error_message()` diagnostic. A cross-module `trap`
can then match on it:

```vyb
{ connect_verified(host, port, cert) } trap (e<TlsError>) -> { ... e.op / e.host / e.message }
```

`TlsStreamOps` and `TlsContextOps` bind the method surface (`write`, `read`,
`connect`, `accept`, `close`, `dispose`) onto the structs. The
`https_selfhost*` helpers ([§4.14](#414-https-https-client-over-tls-http))
exercise the full wiring end-to-end, returning `Bool?` (absent on any failure).

**SIGPIPE.** The runtime installs `signal(SIGPIPE, SIG_IGN)` process-wide (a
constructor in `runtime/vyb_runtime.c`) so a `tls_write` after the peer has
reset the connection returns an error instead of raising `SIGPIPE`. Because the
install is global, any program linked against the runtime gets the non-default
`SIGPIPE` behavior; restore the kernel default with `signal(SIGPIPE, SIG_DFL)`
at startup if you need write-to-closed-pipe to terminate instead of erroring.

### 4.13 `http` — pure-Vyb HTTP/1.1 client and server

Module page: [`http.md`](http.md). Layered over `network`/`threads` (not raw
runtime calls). Acquisition ops keep the File?/net shape: `http_listen` returns a
`TcpListener?` and `http_accept` a `TcpStream?` — absence *is* failure. The full
client `http_get_full` returns a **present** `HttpResponse { status, reason,
headers, body, error<String?> }`: on success `error` is absent and `status` is the
parsed code; on failure `status` is `-1` and `error` carries the lossless phase
reason (`"resolve failed: <host>"`, `"connect <ip>:<port> failed"`,
`"bad response: no status line"`) — a failed round-trip is **never a silent
absent** and never a sentinel passed off as data. The body convenience `http_get`
returns `String?` (absent when `status < 0`).

```vyb
# client -- http_get_full always returns a response; match `error` for the reason
resp = http_get_full(host, port, path)          # HttpResponse
match (resp.error) {
    err -> { fail http_error("get", host) }     # failed round-trip; reason in err
    ?    -> { http_header(resp, "content-type") }  # success
}

# server
match (http_listen(port, backlog)) {        # TcpListener? (0 = ephemeral)
    listener -> {
        port_ = listener.local_port()
        match (http_accept(listener)) { c -> { ... } , ? -> { ... } }
        listener.close()
    }
    ? -> { fail http_error("listen", port.to_string()) }
}

# helpers (unchanged)
http_response(status, body)<String>         # well-formed response
http_request(method, path, host)<String>
http_get(host, port, path)<String?>          # body, or absent when status < 0
http_status_code(line)<Int>
http_index_of(s, needle)<Int>
http_header_value(header, name)<String>
http_read_line(fd)<String> / http_read_head / http_read_exact / http_read_all
```

The threaded `http_serve` dispatches each accepted connection to its own
detached thread (`http_serve_conn`), reading the head and echoing a
well-formed response. This is a pure-Vyb reference implementation — start
here before layering TLS.

### 4.14 `https` — HTTPS client over tls + http

Module page: [`https.md`](https.md). A client that runs the `http` request
state machine over a `TlsStream` (reusing `http`'s parsers and the shared
`HttpResponse` type, which it re-exports). Like `http_get_full`,
`https_get_full*` return a **present** `HttpResponse` whose `error<String?>`
carries the lossless reason on a failed round-trip (`"resolve failed"`,
`"connect ... failed"`, `"tls handshake failed"`, `"tls stream failed"`,
`"bad response: no status line"`) with `status -1`; the body wrappers yield
`String?` (absent when `status < 0`). Escalate with `fail http_error(...)`.

```vyb
resp = https_get_full(host, port, path)      # present HttpResponse (unverified ctx)
match (resp.error) {
    err -> { fail http_error("get", host) }  # failed round-trip; reason in err
    ?    -> { http_header(resp, "content-type") }
}
https_get_full_verified(host, port, path, ca_pem)<HttpResponse>
https_get(host, port, path)<String?>
https_get_verified(host, port, path, ca_pem)<String?>

# diagnostics: throwaway TLS server answering one request
https_selfhost(cert_pem, key_pem)<Bool?>
https_selfhost_verified(cert_pem, key_pem)<Bool?>
```

`https_get_full_verified` pins a CA and verifies the hostname; the
`https_selfhost*` pair generates a self-signed cert at runtime and drives the
whole tls+http wiring without an external server.

### 4.15 `prelude` — the auto-imported re-export surface

Module page: [`prelude.md`](prelude.md). Re-exports `Display`, `Debug`,
`Clone`, `Equatable`, `Hashable`, `Comparable`, `hash_chars`, and
`prelude_ok`. The compiler injects these into every module unless the module
imports them itself or redefines one of the core aspects.

---
---

### 4.16 `utf8` — UTF-8 codepoints over byte strings

Module page: [`utf8.md`](utf8.md). Vyb Strings carry raw bytes, so a provider
displaying multibyte web content needs codepoint-aware helpers. Everything uses
byte offsets, matching the byte-indexed String model; an out-of-range or invalid
request reads back `-1`.

```vyb
utf8_len(s<String>)<Int>            # number of codepoints
utf8_at(s<String>, byte_off<Int>)<Int>      # codepoint value at an offset
utf8_index(s<String>, cp<Int>)<Int>         # byte offset of code point cp
utf8_valid(s<String>)<Int>                  # 1 when the whole string is UTF-8
```

### 4.17 `env` — the process environment

Module page: [`env.md`](env.md). Read and write environment variables in-process;
a browser honors `HTTP_PROXY` / `HOME` / `TERM` here instead of hard-coding them.

```vyb
home<String> = env_get("HOME") else ""   # String? -- absent when unset
env_set(name<String>, value<String>)<Int>   # 0 on success
env_unset(name<String>)<Int>        # 0 on success
```

`env_get` follows the engine-wide `T?` shape (§3.16): it returns `String?`, absent
when the variable is unset and present holding its value otherwise (a variable set
to an empty string is present, unlike the old `""` sentinel). `else ""` recovers
the legacy "unset reads as empty" behavior.

### 4.18 `rand` — pseudo-random integers

Module page: [`rand.md`](rand.md). A small xorshift generator; `rand_range`
yields `[lo, hi)`, and `rand_seed` reproduces a sequence for deterministic
behavior.

```vyb
rand()<Int>                         # [0, 2^63-1]
rand_range(lo<Int>, hi<Int>)<Int>   # [lo, hi)
rand_seed(seed<Int>)<Int>           # reseed (reproducible sequence)
```

### 4.19 `process` — external commands

Module page: [`process.md`](process.md). Run a trusted command line through the
shell and read its exit status or captured stdout.

```vyb
exec_run(cmd<String>)<Int?>         # freedom-gated: child exit code (absent = launch failure)
exec_output(cmd<String>)<String?>   # freedom-gated: stdout (absent = launch failure)
exec_status()<Int>                  # exit status of the last exec_output
```

Running a shell command reaches outside the managed model, so `exec_run` and
`exec_output` are **gated behind a `freedom` block** (`freedom { exec_run(...) }`).
The read-only `exec_status` probe is callable from ordinary code. Following the
engine-wide `T?` shape, `exec_run` returns `Int?` (absent when the command could
not be launched) and `exec_output` returns `String?` (absent on launch failure,
present -- possibly empty -- when the run succeeded), so a real empty stdout is
never mistaken for a failed capture. The shared `ProcError` + `proc_error(op, cmd)`
builder lets a caller `match` the absent form and `fail` it into a typed `trap`.

### 4.20 `regex` — POSIX extended patterns

Module page: [`regex.md`](regex.md). Extended-pattern matching over String bytes,
with byte-offset find, group capture, and literal replacement. A pattern that
does not match or compile yields the "no match" form of each helper
(`0` / absent / `""` / unchanged input).

```vyb
regex_match(pattern<String>, s<String>)<Int>            # substring match?
regex_find(pattern<String>, s<String>)<Int?>            # offset (absent = no match)
regex_capture_match(pattern<String>, s<String>)<String?> # whole match text
regex_capture(pattern<String>, s<String>)<String?>      # first group
regex_replace(pattern<String>, s<String>, replacement<String>)<String>
regex_replace_all(pattern<String>, s<String>, replacement<String>)<String>
```

`regex_find` returns `Int?` (present with the byte offset even when it is 0, absent
when there is no match) and `regex_capture_match`/`regex_capture` return `String?`
(present holding the text -- possibly empty -- on a match, absent when the pattern
does not match). `regex_match` stays a `0`/`1` `Int` (a plain substring test with
no sentinel).

### 4.21 `curses` — ncurses terminal UI

Module page: [`curses.md`](curses.md). A whole-terminal TUI surface over
ncursesw (used by the VybLynx browser). The ncurses ABI stays in the runtime
shims; Vyb sees only `Int` keycodes, `Int` attributes, `Int` dimensions, and
`String` text.

```vyb
curses_init()<Int>            curses_close()<Int>            curses_ok()<Int>
curses_rows()<Int>            curses_cols()<Int>
curses_refresh()<Int>         curses_clear()<Int>
curses_move(row<Int>, col<Int>)<Int>                        curses_addstr(s<String>)<Int>
curses_move_addstr(row<Int>, col<Int>, s<String>)<Int>
curses_getch()<Int>           curses_nodelay(flag<Bool>)<Int>
curses_timeout(ms<Int>)<Int>  curses_keypad(flag<Bool>)<Int>
curses_has_color()<Int>       curses_start_color()<Int>
curses_init_pair(pair<Int>, fg<Int>, bg<Int>)<Int>          curses_color_pair(n<Int>)<Int>
curses_attr_on(attrs<Int>)<Int>   curses_attr_off(attrs<Int>)<Int>
curses_attr_normal()<Int>     curses_attr_bold()<Int>       curses_attr_underline()<Int>
curses_attr_reverse()<Int>    curses_attr_blink()<Int>
curses_show_cursor()<Int>     curses_hide_cursor()<Int>
```

Lifecycle: `curses_init()` once, draw, then `curses_close()`. The screen is a
single global owned by the active program — exactly one subsystem mutates it
(ncurses refresh is not thread-safe). Everything returns `0` on success and `-1`
on error, except the documented deviations: `curses_getch` returns the pressed
key/`KEY_*` code (or `-1` on a timeout/no-delay read with no input), `curses_ok`/
`curses_has_color` return `1`/`0`, `rows`/`cols` return the terminal size, and
`curses_color_pair(n)` yields the attribute for `curses_attr_on/off`. Input is
blocking by default; for a nonblocking UI loop call `curses_nodelay(1)` or
`curses_timeout(ms)` so `getch` polls instead of stalling. Colours and key codes
are exposed as the stable `enum CursColor` and `enum CursKey` surfaces.

### 4.22 `qt` — Qt5 Widgets native GUI

Module page: [`qt.md`](qt.md). A small, deterministic subset of Qt5 Widgets for
native-window programs, driven from the C++ bridge
(`runtime/vyb_qt_bridge.cpp`); Vyb sees only `Int` handles (qintptr-sized),
`Int` dimensions, `Bool`/`Int` status flags, and `String` text.

```vyb
qt_init()<Bool>               qt_quit()<Int>                qt_active()<Bool>
qt_process_events()<Int>      qt_set_timer(ms<Int>)<Int>    qt_timer_fired()<Bool>
qt_event_count()<Int>         qt_event_handle()<Int>        qt_event_kind()<Int>
qt_event_pop()<Int>           qt_kind(h<Int>)<Int>
qt_window_create()<Int>       qt_window_close(w<Int>)<Int>
qt_window_set_title(w<Int>, title<String>)<Int>             qt_window_title(w<Int>)<String>
qt_window_resize(w<Int>, width<Int>, height<Int>)<Int>
qt_window_width(w<Int>)<Int>  qt_window_height(w<Int>)<Int>
qt_window_show(w<Int>)<Int>   qt_window_hide(w<Int>)<Int>   qt_window_visible(w<Int>)<Bool>
qt_label_create(parent<Int>, text<String>)<Int>             qt_label_set_text(l<Int>, text<String>)<Int>
qt_label_text(l<Int>)<String>
qt_button_create(parent<Int>, text<String>)<Int>            qt_button_text(b<Int>)<String>
qt_button_set_text(b<Int>, text<String>)<Int>               qt_button_set_enabled(b<Int>, on<Bool>)<Int>
qt_edit_create(parent<Int>, text<String>)<Int>              qt_edit_text(e<Int>)<String>
qt_edit_set_text(e<Int>, text<String>)<Int>                 qt_edit_set_placeholder(e<Int>, text<String>)<Int>
qt_checkbox_create(parent<Int>, text<String>)<Int>          qt_checkbox_checked(c<Int>)<Bool>
qt_checkbox_set_checked(c<Int>, on<Bool>)<Int>
qt_progress_create(parent<Int>, max<Int>)<Int>              qt_progress_set_value(p<Int>, value<Int>)<Int>
qt_vbox(parent<Int>)<Int>     qt_hbox(parent<Int>)<Int>     qt_layout_add(layout<Int>, child<Int>)<Int>
qt_layout_add_layout(layout<Int>, sub<Int>)<Int>      qt_layout_set_stretch(layout<Int>, index<Int>, stretch<Int>)<Int>
qt_combo_create(parent<Int>)<Int>                       qt_combo_add_item(c<Int>, text<String>)<Int>
qt_combo_count(c<Int>)<Int>   qt_combo_current_index(c<Int>)<Int>
qt_combo_set_current_index(c<Int>, idx<Int>)<Int>       qt_combo_item_text(c<Int>, idx<Int>)<String>
qt_spin_create(parent<Int>, min<Int>, max<Int>)<Int>    qt_spin_value(s<Int>)<Int>
qt_spin_set_value(s<Int>, value<Int>)<Int>
qt_slider_create(parent<Int>, min<Int>, max<Int>)<Int>  qt_slider_value(s<Int>)<Int>
qt_slider_set_value(s<Int>, value<Int>)<Int>
qt_dial_create(parent<Int>, min<Int>, max<Int>)<Int>      qt_dial_value(d<Int>)<Int>
qt_dial_set_value(d<Int>, value<Int>)<Int>
qt_group_create(parent<Int>, title<String>)<Int>
qt_text_edit_create(parent<Int>)<Int>   qt_text_edit_text(e<Int>)<String>
qt_text_edit_set_text(e<Int>, text<String>)<Int>
qt_radio_create(parent<Int>, text<String>)<Int>           qt_radio_checked(r<Int>)<Bool>
qt_radio_set_checked(r<Int>, on<Bool>)<Int>
qt_widget_set_enabled(h<Int>, on<Bool>)<Int>              qt_widget_enabled(h<Int>)<Bool>
qt_grid(parent<Int>)<Int>             qt_grid_add(g<Int>, child<Int>, row<Int>, col<Int>)<Int>
qt_web_create(parent<Int>)<Int>       qt_web_load(w<Int>, url<String>)<Int>
qt_web_url(w<Int>)<String>            qt_web_title(w<Int>)<String>
qt_web_loading(w<Int>)<Bool>          qt_web_back/forward/reload(w<Int>)<Int>
qt_post_event(h<Int>, kind<Int>)<Int>
qt_widget_set_visible(h<Int>, on<Bool>)<Int>              qt_widget_visible(h<Int>)<Bool>
qt_tabs_create(parent<Int>)<Int>    qt_tabs_add(t<Int>, text<String>)<Int>
qt_tabs_count/current(t<Int>)<Int>  qt_tabs_set_current(t<Int>, idx<Int>)<Int>
qt_list_create(parent<Int>)<Int>    qt_list_add(l<Int>, text<String>)<Int>
qt_list_count/current(l<Int>)<Int>  qt_list_set_current(l<Int>, idx<Int>)<Int>
qt_list_item_text(l<Int>, idx<Int>)<String>
qt_main_window_create()<Int>      qt_menubar(mw<Int>)<Int>
qt_menu_add(mw<Int>, title<String>)<Int>  qt_action_add(menu<Int>, text<String>)<Int>
qt_action_count(menu<Int>)<Int>
qt_statusbar_message(mw<Int>, text<String>)<Int>  qt_statusbar_text(mw<Int>)<String>
qt_toolbar_create(mw<Int>, title<String>)<Int>
qt_msg_info/warn/error/about(parent<Int>, title<String>, text<String>)<Bool?>
qt_msg_question(parent<Int>, title<String>, text<String>)<Int?>
qt_file_open/save(parent<Int>, title<String>, filter<String>)<String?>
qt_dir_select(parent<Int>, title<String>)<String?>
qt_dlg_info/warn/error/about(parent<Int>, title<String>, text<String>)<Int>
qt_dlg_question(parent<Int>, title<String>, text<String>)<Int>
qt_dlg_open/save(parent<Int>, title<String>, filter<String>)<Int>
qt_dlg_dir(parent<Int>, title<String>)<Int>
qt_dlg_close(h<Int>)<Bool?>   qt_dlg_selected(h<Int>)<String?>
qt_event_result()<Int>
qt_rich_create(parent<Int>)<Int>   qt_rich_set_html/set_plain/append(e<Int>, text<String>)<Int>
qt_rich_html/plain(e<Int>)<String> qt_rich_clear(e<Int>)<Int>
qt_rich_set_text_color(e<Int>, r<Int>, g<Int>, b<Int>)<Int>
qt_widget_set_font_size(h<Int>, pt<Int>)<Int>   qt_widget_set_font_bold(h<Int>, on<Int>)<Int>
qt_widget_set_text_color(h<Int>, r<Int>, g<Int>, b<Int>)<Int>
qt_wait_event(timeout<Int>)<Bool>
qt_run()<Int>                  qt_run_stop()<Int>            qt_active()<Bool>
qt_on_event(handler<fn(Int, Int) -> Void>)<Int>
```

#### How the Qt surface is maintained

`tools/gen_qt.py` generates the public declarations and stubs, codegen dispatch,
semantic and JIT registrations, and the wrapper enums from one function table.
To add a widget, add its table entry, implement the bridge, then run
`python3 tools/gen_qt.py`; use `--check` in CI to detect drift.

#### Windows, widgets, and layouts

`qt_main_window_create` adds menus, actions, a status bar, and toolbars to a
plain `QWidget` window. Actions report `QtEvent::Click` with the action handle;
actions are not widgets and must not be passed to widget APIs. `qt_grid` creates
a `QGridLayout`; `qt_group_create` creates a titled `QGroupBox`; `qt_tabs_*` and
`qt_list_*` wrap `QTabWidget` and `QListWidget`. The generic enabled/visible
helpers work on every widget handle.

Box layouts can nest with `qt_layout_add_layout(parent, sub)`. Use
`qt_layout_set_stretch(layout, index, stretch)` to let a child such as a web view
take remaining space while a toolbar stays compact. `demos/VybWeb/` is a complete
example: a small browser with navigation controls, an address bar, and a web
view. The optional `qt_web_*` API needs `Qt5WebEngineWidgets`; without it the
stubs remain available and `qt_web_create()` returns `0`.

#### Events and event loops

Use the *polled* model for deterministic tests: call `qt_process_events`, use
`qt_set_timer(ms)` with `qt_timer_fired()`, and drain the FIFO queue through
`qt_event_count`, `qt_event_handle`, `qt_event_kind`, and `qt_event_pop`.
`qt_wait_event(timeout)` blocks efficiently until an event arrives or the timeout
expires. Text edits emit `TextChanged`; checkboxes and radios emit `Toggled`;
combo boxes, spin boxes, sliders, and dials emit `IndexChanged` or
`ValueChanged`; tabs and lists emit `CurrentChanged`.

For an application, `qt_run()` enters Qt's native loop and dispatches queued
events to the callback registered with `qt_on_event(handler<fn(Int, Int) -> Void>)`.
Call `qt_run_stop()` for a graceful return to the caller, or `qt_quit()` for full
teardown. `qt_post_event(h, kind)` is thread-safe: background `asyncs` work can
notify the main-thread UI without touching a QWidget directly.

#### Dialogs, styling, and headless operation

The modal `qt_msg_*`, `qt_file_*`, and `qt_dir_select` calls use native Qt
dialogs and block for input. `qt_msg_question` returns `Int?` (1 for Yes, 0 for
No); file and directory pickers return `String?`, using a present empty string
for a user cancellation and absence when the GUI is unavailable. Set
`VYB_QT_DIALOG_AUTO=1` to auto-answer modal dialogs during offscreen tests.

The non-blocking `qt_dlg_*` family instead returns a dialog handle and emits a
`QtEvent::dialog` when finished. Read its result with `qt_event_result()` and a
picker path with `qt_dlg_selected(handle)`; use `qt_dlg_close(handle)` to reject
it programmatically. `qt_rich_*` supports HTML and plain text in a `QTextEdit`,
while the font and palette helpers style any widget.

Call `qt_init()` once from the main thread. Qt uses xcb under a display and
`QT_QPA_PLATFORM=offscreen` for headless runs; when neither a platform nor
`$DISPLAY` is set, the bridge falls back to `offscreen`. If Qt5 is not linked,
the stubs still resolve and `qt_init()` returns `false`, so programs can degrade
cleanly without unresolved JIT symbols.

### 4.23 `archive` — gzip/DEFLATE decompression and tar extraction

Module page: [`archive.md`](archive.md). A pure-Vyb decoder for fetching and
unpacking `.tar.gz` sources: `inflate_gzip` fully decodes a gzip stream
(header, DEFLATE blocks, and the CRC32 + ISIZE trailer verified byte-for-byte),
and `extract_tar` walks a ustar archive returning its member files. Together
they turn a fetched `.tar.gz` blob into a list of `(name, data)` entries
byte-identical to the originals — with no C bridge and no runtime/ `src/`
changes; the module is written entirely in stdlib.

```vyb
inflate_gzip(gz<String>)<String?>   # full .gz decode, or null on bad input
extract_tar(tar<String>)<Vec<TarEntry>>   # ustar walk -> { name, size, data }
crc32(data<String>)<Int>            # CRC-32 (tables + POLY), also used by inflate_gzip
bstr(b<Int>)<String>                # single-byte String (alias of String::from_byte)
```

```vyb
import archive::{inflate_gzip, extract_tar}
match (inflate_gzip(gz_bytes)) {
    plain -> { entries = extract_tar(plain) }
    ?     -> { println("bad gzip") }
}
```

#### Implementation notes

The decoder uses Vyb's existing `String` primitives throughout. Read a byte with
`s.char_at(i) & 0xFF`, create one with `String::from_byte(b)`, and accumulate
output with `out.concat(chunk)`.

The DEFLATE bit cursor is functional: every step returns both its result and the
advanced cursor position, rather than mutating a caller-owned value. Canonical
Huffman decoding follows puff.c's `DECODE()` approach, binning lengths by code
width before reading symbols most-significant bit first. The tar reader also
handles GNU `L` and `K` long-name records.

`test/modules/test_archive.vyb` covers the complete path by decoding an embedded
gzip payload and walking the resulting tar archive.

---

## 5. Concurrency and async model

Vyb is fully multithreaded (pthreads underneath) with a clean, layered story:

| Primitive | Module | Cost model | When to use |
|---|---|---|---|
| `thread_spawn` + mutex/condvar/atomics | `threads` | OS thread, blocking | CPU-bound work, classic pthread patterns |
| `task_spawn` | `tasks` | detached pthread per task | fire-and-forget jobs |
| `async_spawn` / `async_await` | `asyncs` | fibers on a per-core worker pool | many concurrent, mostly-waiting tasks |
| `agent_start` / `agent_send` | `agents` | worker thread + owned mailbox | isolated message-passing, actor-style units |
| `chan_*` / `chan_select` | `channels` | typed handoff between threads | message passing, fan-out |

**Shared state** is coordinated with `mutex_*`/`cond_*`/`atomic_*`
(`threads`). `cond_wait(cv, m)` releases `m`, sleeps, and re-acquires on
signal — the classic monitor pattern, 1:1 on pthreads.

**Typed channels** so far carry `Int` and `String` payloads, unbounded or
bounded (`chan_bounded(cap)`). Use `chan_select(handles<Vec<Int>>)` to await
the first ready handle, then drain it with `chan_recv` (which returns `Int?`,
absent when the channel is closed and drained). Non-`Int` payloads
(Float/Bool/Char) are the next channel increments and will reuse the same async
codegen.

**The async executor** (`asyncs`) is not thread-per-task: a fiber suspends
cooperatively (`async_sleep_ms`) without blocking its worker, so hundreds of
sleeping connections run on a few cores. `async_await` blocks the *caller*
until a handle completes; `async_poll` probes; `async_run_all` drives a batch
to completion. `async_tcp_*`/`async_udp_*` in `network` bridge sockets into
the executor so I/O futures fit the same await model.

**Teardown discipline.** Because ownership is deterministic, free paths must
be explicit: channels (`chan_free`), mutexes/condvars/atomics
(`mutex_free`/`cond_free`/`atomic_free`), tasks (`task_free`), agents
(`agent_close`/`agent_free`), sockets
(`close`), and TLS (`tls_free_context`/`tls_close`). `our`-owned values
release automatically at last drop; leaked handles are a bug in user code, not
the runtime.

---

## 6. Networking cookbook

### TCP echo server

```vyb
import network
import threads

main()<Int> -> {
    match (tcp_listen("0.0.0.0", 9000, 16)) {   # "" = all interfaces
        l -> {
            while (true) {
                match (tcp_accept(l)) {
                    c -> {
                        h = thread_spawn(|| -> { handle(c); return 0 })
                        thread_detach(h)
                    }
                    ? -> { break }
                }
            }
            return 0
        }
        ? -> { return 1 }
    }
}
```

### UDP datagram peer

```vyb
match (udp_bind("0.0.0.0", 9001)) {
    u -> {
        udp_send_to(u, "127.0.0.1", 9002, "ping")
        reply = udp_recv_from(u, 1400)
        peer = udp_last_peer_ip() + ":" + udp_last_peer_port().to_string()
    }
    ? -> { fail net_error("bind", "0.0.0.0") }
}
```

### HTTP server

```vyb
import http
http_serve(8080, 16)          # threaded loopback reference server
```

### HTTPS client (peer-verified)

```vyb
import https
resp = https_get_full_verified("example.com", 443, "/", ca_pem)
println("{} {}: {}".format(resp.status_code, resp.headers.len(), resp.body))
```

---

## 7. Performance and memory model

- **Codegen:** Vyb → LLVM IR → native object. Generics are monomorphized, so
  generic call sites are direct calls, not erasure+dispatch.
- **Layout:** `Int` sizes are exact; `String` is a fat pointer; structs are
  plain C-layout aggregates; a function that can fail returns
  `{ value, error }` in LLVM.
- **Ownership is deterministic.** The default is deterministic
  ownership/refcounts, so embedded targets get explicit, predictable memory.
- **Allocation discipline:** string transforms allocate; read-only ops don't.
  Collection growth amortizes like any growable array; `Vec(n)` preallocates.
  Rehash vs memory on maps: deterministic growth is the embedded-friendly
  default; you control capacity up front.
- **Bounds checks** happen at runtime on string/`get`/`at` operations and
  return safe defaults rather than reading out of bounds.
- **`freedom`** is the documented escape from checked access when needed.

For profiling and leak checking the project supports both **ASan** and
**Valgrind** builds (`build-asan/`, `valgrind … ./build/vyb file.vyb`); the
runtime points a single process at a list of tests if needed.

---

## 8. Testing and tooling

### Build

```bash
./build.sh                        # clean build
./build.sh --run-tests            # build + run the suite
./build.sh --category <cat>       # filter tests by category
./build.sh --test-pattern '*.vyb' # filter by filename pattern
```

### Test harness

Canonical suite runner (wired into CTest as `run-tests`):
```bash
python3 test/run_tests.py --vyb ./build/vyb --test-dir test --execute-jit   # full suite (1077 tests)
python3 test/run_tests.py --vyb ./build/vyb --test-dir test --category async    # filter by category
```
The auxiliary parallel harness (`test_harness.py`, `triage_tool.py`) adds HTML
reporting and failure triage on top of that suite.

Tests carry metadata headers (`@test:`, `@description:`, `@category:`,
`@expect:`, `@expect-output:`, `@expect-return:`) that drive pass/fail
automatically.

### Syntax migration & triage tools

```bash
python3 migrate_syntax.py --scan            # find legacy syntax
python3 migrate_syntax.py --apply           # apply canonical forms (backs up)
python3 triage_tool.py --pattern 'failed*'  # analyse failures
```

### Reference manual generator (`tools/refman.py`)

```bash
python3 tools/refman.py --emit-dir docs/refman     # (re)generate
python3 tools/refman.py --check                    # drift/unresolved CI gate
```

`--check` (also wired into `.github/workflows/refman-check.yml`) fails if:
an imported symbol is not a `share(all)` export of its provider, a
prose-ref/uses-type target is unresolved, or a regenerate diffs from the
committed `docs/refman/`. The generator is deterministic, so a clean tree
regenerates byte-identical output.

---

## 9. API index

<!-- refman:api-index begin -->
| Area | Module page | Cross-index |
|---|---|---|
| Contracts & math | [`core`](core.md) | [aspects & binds](aspects.md) |
| Files | [`io`](io.md) | [types](types.md) |
| Terminal & stdin | [`term`](term.md) | — |
| Terminal & GUI | [`curses`](curses.md) | — |
| Native GUI | [`qt`](qt.md) | — |
| Clocks | [`time`](time.md) | — |
| Vec/Map/Set/BTree | [`collections`](collections.md) | [functions](functions.md) |
| Channels | [`channels`](channels.md) | [functions](functions.md) |
| Threads & atomics | [`threads`](threads.md) | [functions](functions.md) |
| Fire-and-forget | [`tasks`](tasks.md) | — |
| Async executor | [`asyncs`](asyncs.md) | — |
| Message-passing units | [`agents`](agents.md) | — |
| Sockets/TCP/UDP | [`network`](network.md) | [shared types](interfaces.md) |
| TLS | [`tls`](tls.md) | [shared types](interfaces.md) |
| HTTP | [`http`](http.md) | [shared types](interfaces.md) |
| HTTPS client | [`https`](https.md) | [shared types](interfaces.md) |
| Auto-imported facade | [`prelude`](prelude.md) | — |
| UTF-8 codepoints | [`utf8`](utf8.md) | — |
| Environment | [`env`](env.md) | — |
| Pseudo-random | [`rand`](rand.md) | — |
| External commands | [`process`](process.md) | — |
| Regex | [`regex`](regex.md) | — |
| archive | [`archive`](archive.md) | — |
| Runtime intrinsics | [`runtime`](runtime.md) | — |
<!-- refman:api-index end -->


















































The **shared cross-module types** (`HttpResponse`, `TcpStream`, `TlsContext`,
`TlsStream`, `Socket`) and every symbol that uses them are in
[`interfaces.md`](interfaces.md).

---

## Appendix A — Memory model

**Ownership types**
- `my<T>`: unique ownership, RAII cleanup.
- `our<T>`: shared, reference-counted ownership (auto-free at last drop).
- `their<T>`: non-owning borrow, lifetime-checked.
- `mild<T>`: weak reference that can detect destruction via `grab()` / `released()`.

**Borrowing operations**
- `view(expr)`: creates a `their<T const>` immutable borrow.
- `borrow(expr)`: creates a `their<T>` mutable borrow.
- `soft(expr)`: creates a `mild<T>` weak reference from `our<T>`.

**Freedom operations (`freedom { … }`)**
- `loc<T>`: raw pointer type.
- `loc(expr)`: take a pointer to an expression.
- `at(ptr)`: dereference a pointer (read/write).
- `from<loc<T>>(expr)`: convert a raw pointer between types.

**Primitives unwrap on read**: reading a primitive that is `my`/`our`-owned
yields the value directly, with no allocation.

## Appendix B — Auto-serialization

`main()` return values serialize automatically:

```vyb
main()<Int>                        # exit code
main()<String>                     # prints the string
main()<Int, String>                # prints [42, "hello"]
main()<struct>                     # prints the struct as JSON
main()<Vec<T>>                     # prints the Vec as a JSON array
```

Custom serialization is available by implementing the `Serialize` aspect.

## Appendix C — Glossary

- **AST** — abstract syntax tree produced by the parser.
- **Borrow checking** — compile-time analysis ensuring references don't outlive their data.
- **Bundle / share** — module visibility grouping via `bundle(...)` / `share(...)`.
- **Import** — secure module inclusion from verified, signed sources.
- **Smuggle** — flexible module inclusion from external, potentially unverified sources.
- **JIT** — just-in-time native code compilation; LLVM is Vyb's backend.
- **Monomorphization** — specializing generic functions/types per concrete type at compile time.
- **Ownership** — the `my`/`our`/`their`/`mild` memory-management model.
- **Pattern matching** — `match`/`select` that destructure and test values.
- **`T?`** — Vyb's native optional (present `T?(v)` / absent `T?()`), used with `else`.
- **Template → generics** — compile-time generic constructs parameterized by types.

## Appendix D — Grammar (EBNF)

This appendix is the single home for Vyb's formal grammar. Every production here
is the **current** compiler surface: it parses today, and it matches what the
feature sections in [§3](#3-language-tour) and [§4](#4-standard-library-reference) (and the `test/` suite) exercise. Vyb has one
unified syntax — there is no legacy alternate form to keep track of.

```ebnf
-*- mode: ebnf -*-
// Conventions:
//   IDENTIFIER:        A valid identifier token.
//   INTEGER_LITERAL:   An integer literal token.
//   FLOAT_LITERAL:     A float literal token.
//   STRING_LITERAL:    A string literal token.
//   BOOLEAN_LITERAL:   'true' or 'false'.
//   'keyword':         A literal keyword token.
//   { ... }:           Zero or more occurrences (Kleene star).
//   [ ... ]:           Zero or one occurrence (optional).
//   ( ... | ... ):     A choice (alternation).
//   ... ::= ... :      Defines a production rule.

// ------------------------------- Module structure ---------------------------

module                  ::= { module_item } EOF
module_item             ::= import_statement
                          | smuggle_statement
                          | struct_declaration
                          | enum_declaration
                          | bind_declaration
                          | aspect_declaration
                          | type_alias_declaration
                          | function_declaration
                          | variable_declaration
                          | statement

// ----------------------------- Imports / smuggle ----------------------------

import_statement        ::= 'import' import_shape [';']
smuggle_statement       ::= 'smuggle' import_shape [';']
import_shape            ::= '*' 'as' IDENTIFIER 'from' STRING_LITERAL
                          | module_path [ 'as' IDENTIFIER ] [ 'from' STRING_LITERAL ]
                          | module_path '::' '{' import_specifier { ',' import_specifier } '}'
import_specifier        ::= IDENTIFIER [ 'as' IDENTIFIER ]
module_path             ::= IDENTIFIER { ( '::' | '.' ) IDENTIFIER }

// -------------------------------- Types -------------------------------------

type                    ::= base_type { type_suffix }
base_type               ::= IDENTIFIER
                          | lifetime_type_identifier
                          | qualified_type_name
                          | '(' [ type { ',' type } [ ',' ] ] ')'        // tuple / group
                          | '[' type [ ';' expression ] ']'              // array type
                          | function_type
lifetime_type_identifier::= 'my' | 'our' | 'their' | 'mild' | 'const'
qualified_type_name     ::= IDENTIFIER { ( '::' | '.' ) IDENTIFIER }
type_suffix             ::= '<' [ type { ',' type } ] '>'   // generic args
                          | '[' ']'                         // array-of (T[])
                          | '*'                             // pointer (T*)
                          | '?'                             // native optional (T?)
                          | 'const'                         // const-qualified type
function_type           ::= 'fn' '(' [ type { ',' type } ] ')' [ '->' type ]

// ----------------------------- Declarations ---------------------------------

struct_declaration      ::= 'struct' IDENTIFIER [ '<' type_parameter_list '>' ]
                            '{' { struct_field_declaration } '}'
struct_field_declaration::= IDENTIFIER '<' type '>' [ '=' expression ] [';']

enum_declaration        ::= 'enum' IDENTIFIER [ '<' type_parameter_list '>' ]
                            '{' { enum_variant } '}'
enum_variant            ::= IDENTIFIER [ '(' type_list ')' ] [ '=' expression ] ','?

bind_declaration        ::= 'bind' [ '<' type_parameter_list '>' ] type [ '->' type ]
                            '{' { method_declaration } '}'

aspect_declaration      ::= 'aspect' IDENTIFIER [ '<' type_parameter_list '>' ]
                            [ ':' aspect_supertypes ] '{' { method_signature } '}'
aspect_supertypes       ::= IDENTIFIER { '+' IDENTIFIER }

type_alias_declaration  ::= 'type' IDENTIFIER [ '<' type_parameter_list '>' ]
                            '=' type [';']

variable_declaration    ::= var_modifier? IDENTIFIER '<' type '>' [ '=' expression ] [';']
var_modifier            ::= 'let' | 'var' | 'mut' | 'auto' | 'const'

function_declaration    ::= [ 'async' ] [ 'extern' ]
                            IDENTIFIER [ '<' type_parameter_list '>' ]
                            '(' [ parameter_list ] ')'
                            ( '<' type_list '>' | '->' type_list )?
                            function_body
method_declaration      ::= [ 'async' ] [ 'static' ]
                            IDENTIFIER [ '<' type_parameter_list '>' ]
                            '(' [ parameter_list ] ')'
                            ( '<' type_list '>' | '->' type_list )?
                            function_body
method_signature        ::= [ 'async' ] IDENTIFIER '(' [ parameter_list ] ')'
                            '<' type '>' [ '->' function_body ]
function_body           ::= block_statement
                          | statement
                          | expression_statement
                          | ';'                       // forward declaration only
operator_declaration    ::= 'operator' operator_symbol
                            '(' [ parameter_list ] ')'
                            ( '<' type_list '>' | '->' type_list )?
                            function_body
operator_symbol         ::= '+' | '-' | '*' | '/' | '%'
                          | '==' | '!=' | '<' | '<=' | '>' | '>='

// Parameters & generics
type_parameter_list     ::= type_parameter { ',' type_parameter }
type_parameter          ::= IDENTIFIER [ '<' type_bounds '>' ]
type_bounds             ::= IDENTIFIER { '+' IDENTIFIER }          // T<Aspect>
parameter               ::= [ 'const' ] IDENTIFIER ( '<' type '>' | ':' type )? [ '=' expression ]
parameter_list          ::= parameter { ',' parameter } [ ',' '...' ]
type_list               ::= type { ',' type }

// ------------------------------- Statements ---------------------------------

statement               ::= expression_statement
                          | block_statement
                          | variable_declaration
                          | if_statement
                          | ensure_statement
                          | while_statement
                          | for_statement
                          | async_for_statement
                          | match_statement
                          | return_statement
                          | break_statement
                          | continue_statement
                          | pass_statement
                          | fail_statement
                          | panic_statement
                          | exit_statement
                          | refail_statement
                          | try_statement
                          | freedom_statement
                          | defer_statement
                          | await_statement
                          | labeled_loop_statement

block_statement         ::= '{' { statement } '}'
indented_block          ::= INDENT { statement } DEDENT

if_statement            ::= 'if' '(' expression ')' block_statement
                            { 'else' 'if' '(' expression ')' block_statement }
                            [ 'else' block_statement ]
ensure_statement        ::= 'ensure' '(' expression ')' 'else' block_statement
while_statement         ::= 'while' '(' expression ')' block_statement
for_statement           ::= 'for' '(' IDENTIFIER 'in' expression [ ',' expression ] ')'
                            block_statement                       // step or skip
async_for_statement     ::= 'async' 'for' '(' IDENTIFIER [ '<' type '>' ] 'in' expression ')'
                            block_statement                       // channel stream
labeled_loop_statement  ::= IDENTIFIER ':' ( for_statement | while_statement )

match_statement         ::= 'match' '(' expression ')' '{' { match_arm } '}'
match_arm               ::= pattern [ 'if' expression ] '->' ( expression | block_statement ) ','?

return_statement        ::= 'return' [ expression { ',' expression } ] [';']
break_statement         ::= 'break' [ IDENTIFIER ] [';']
continue_statement      ::= 'continue' [ IDENTIFIER ] [';']
pass_statement          ::= 'pass' expression [';']
fail_statement          ::= 'fail' ( '<' type '>' '(' expression ')'
                                   | expression ) [';']
panic_statement         ::= 'panic' '(' expression ')' [';']
exit_statement          ::= 'exit' '(' expression ')' [';']
refail_statement         ::= 'refail' [ expression ] [';']
await_statement         ::= 'await' expression [';']
freedom_statement       ::= 'freedom' block_statement
defer_statement         ::= 'defer' ( expression_statement | block_statement )

try_statement           ::= 'try' ( block_statement | indented_block )
                            { catch_clause } [ 'finally' ( block_statement | indented_block ) ]
catch_clause            ::= 'catch' [ '(' IDENTIFIER [ ':' type ] ')' | IDENTIFIER ]
                            ( block_statement | indented_block )

// A block that may carry trap/ensure clauses (§3.16)
trappable_block         ::= block_statement { trap_clause } [ ensure_clause ]
trap_clause             ::= 'trap' '(' IDENTIFIER '<' type '>' ')' '->' block_statement
ensure_clause           ::= 'ensure' block_statement

// --------------------------------- Patterns ---------------------------------

pattern                 ::= comparison_pattern
                          | range_pattern
                          | IDENTIFIER '(' [ pattern_list ] ')'    // enum variant
                          | path '{' [ field_pattern { ',' field_pattern } [ ',' ] ] '}'
                          | literal
                          | '?'
                          | '[' [ pattern_list ] ']'
                          | '(' pattern_list ')'
comparison_pattern      ::= ( '==' | '!=' | '<' | '<=' | '>' | '>=' ) expression
range_pattern           ::= expression '..' expression
field_pattern           ::= IDENTIFIER ':' pattern | IDENTIFIER
pattern_list            ::= pattern { ',' pattern }

// ------------------------------ Expressions ---------------------------------

expression              ::= assignment_expression
assignment_expression   ::= conditional_expression
                            [ assignment_operator assignment_expression ]
assignment_operator     ::= '=' | '+=' | '-=' | '*=' | '/=' | '%=' | '&=' | '|=' | '^=' | '<<=' | '>>='

conditional_expression  ::= if_expression
                          | logical_or_expression [ '?' expression ':' conditional_expression ]

if_expression           ::= 'if' '(' expression ')' block_statement
                            [ 'else' ( block_statement | if_expression ) ]

logical_or_expression   ::= logical_and_expression { '||' logical_and_expression }
logical_and_expression  ::= bitwise_or_expression { '&&' bitwise_or_expression }
bitwise_or_expression   ::= bitwise_xor_expression { '|' bitwise_xor_expression }
bitwise_xor_expression  ::= bitwise_and_expression { '^' bitwise_and_expression }
bitwise_and_expression  ::= equality_expression { '&' equality_expression }
equality_expression     ::= relational_expression { ( '==' | '!=' ) relational_expression }
relational_expression   ::= range_expression { ( '<' | '<=' | '>' | '>=' ) range_expression }
range_expression        ::= shift_expression [ '..' shift_expression ]
shift_expression        ::= additive_expression { ( '<<' | '>>' ) additive_expression }
additive_expression     ::= multiplicative_expression { ( '+' | '-' ) multiplicative_expression }
multiplicative_expression ::= cast_expression { ( '*' | '/' | '%' ) cast_expression }

cast_expression         ::= unary_expression { 'as' type }
unary_expression        ::= ( '!' | '-' | '~' ) unary_expression
                          | 'await' unary_expression
                          | 'typeof' '(' expression ')' | 'typename' '(' expression ')'
                          | typeof_type_expression
                          | postfix_expression
typeof_type_expression  ::= 'typeof' '<' type '>' '(' ')'

postfix_expression      ::= primary_expression
                            { '(' [ argument_list ] ')' | '.' IDENTIFIER
                            | '::' IDENTIFIER | '[' expression ']' }

primary_expression      ::= literal
                          | path_expression
                          | '(' expression ')'
                          | call_expression
                          | member_expression
                          | index_expression
                          | array_literal
                          | list_comprehension
                          | tuple_literal
                          | struct_literal
                          | lambda_expression
                          | select_expression
                          | borrow_view_expression

literal                 ::= INTEGER_LITERAL | FLOAT_LITERAL | STRING_LITERAL | BOOLEAN_LITERAL
path_expression         ::= IDENTIFIER { ( '::' | '.' ) IDENTIFIER } [ '<' type_argument_list '>' ]
call_expression         ::= primary_expression '(' [ argument_list ] ')'
argument_list           ::= expression { ',' expression }
member_expression       ::= primary_expression ( '.' | '::' ) IDENTIFIER
index_expression        ::= primary_expression '[' expression ']'
type_argument_list      ::= type_argument { ',' type_argument }
type_argument           ::= type | expression

array_literal           ::= '[' [ expression { ',' expression } [ ',' ] ] ']'
                          | '[' type ';' expression ']'        // typed array literal
list_comprehension      ::= '[' expression 'for' IDENTIFIER 'in' expression
                            [ 'if' expression ] ']'
tuple_literal           ::= '(' [ expression { ',' expression } [ ',' ] ] ')'

struct_literal          ::= path_expression '{'
                            [ struct_literal_field { ',' struct_literal_field } [ ',' ] ] '}'
struct_literal_field    ::= IDENTIFIER ( ':' | '=' ) expression | IDENTIFIER

lambda_expression       ::= [ 'async' ] lambda_params '->'
                            ( expression | block_statement )
lambda_params           ::= '|' [ lambda_param { ',' lambda_param } ] '|'
                          | '||'                      // zero-arg form
lambda_param            ::= IDENTIFIER [ '<' type '>' ]

select_expression       ::= 'select' '(' expression ')' '->' '{' { select_arm } '}'
select_arm              ::= pattern '->' ( expression | trappable_block ) ','?

borrow_view_expression  ::= 'view' '(' expression ')' | 'borrow' '(' expression ')'
```

**Key EBNF features (all current):**

**Unified declarations**

Everything uses the `name<Type>` pattern, including return types —
`main()<Int> -> { … }` and generic functions like
`cmp_lt<T<Comparable>>(a<T>, b<T>)<Bool> -> { … }`.

**Native optionals**

Absence is a `T?` type — there is no null literal in the documented model;
`null`/`nil` are lexed but not part of the core surface.

**Generics with aspect bounds**

`T<Aspect>` (and `T<Comparable>`), written directly as a type-parameter
bound; exports use the `share(all)` marker.

**Errors**

`fail` / `trap` / `ensure`, plus `refail`; no `throw`/`throws`.

**Ownership wrappers**

`my<T>`, `our<T>`, `their<T>`, `mild<T>` are ordinary `name<Type>`
applications over the lifetime keywords `my`, `our`, `their`, `mild` (all
recognized as type-name starts).

**Async**

`async name(params)…` functions, `async |x| -> …` lambdas returning
`Future<T>`, `await` expressions, and `async for` over channels.

**Select/match**

Value-returning `select(expr) -> { … }` and `match (expr) { … }` over the
same pattern set.

Select arms may group several discrete values into one arm with a
brace-delimited set: the arm matches when the target equals *any* element.
Sets hold literals and bare enum-variant names only (no comparison patterns,
no payload-binding variants, no expressions); empty `{}` is rejected. The
`?` wildcard must remain the last arm.

```vyb
parity(x<Int>)<String> -> {
    return select(x) -> {
        {1, 3, 5, 7, 9} -> "odd",
        {2, 4, 6, 8}    -> "even",
        ?               -> "out of bounds"
    }
}
```

**Verification**

Verified `import` and flexible `smuggle`, both with the namespace
(`* as NS from "…"`), whole-module (`as NS`), and specifier-list
(`::{a as b, c}`) forms.
