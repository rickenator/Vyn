**Vyn Programming Guide**

---

## 1. Introduction

Welcome to the Vyn Programming Guide. This guide walks you through writing, building, and extending Vyn programs, from your first “Hello, Vyn!” to deep dives into the Vyn language internals and runtime.

### 1.1 Purpose & Audience

This guide is intended for systems programmers, language designers, and developers who want:

* A compact, expressive syntax for both low-level control and high-level abstractions.
* Fine-grained memory management options, including GC, manual free, and scoped regions.
* Built-in concurrency primitives and customizable threading templates.
* A self-hosted compiler and hybrid VM/JIT architecture for rapid iteration and performance tuning.

Whether you’re coming from C/C++, Rust, D, or other modern systems languages, you’ll find Vyn’s template-driven approach familiar yet uniquely powerful.

### 1.2 What is Vyn?

Vyn is a statically typed, template-metaprogramming language that compiles to native code via a hybrid bytecode VM and tiered JIT. Its key differentiators:

* **Terse Syntax**: Indentation-based blocks, optional semicolons, clear constructs.
* **Templates Everywhere**: Monomorphized generics for types and functions.
* **Hybrid Memory Model**: Default GC, optional manual free, reference counting, and scoped cleanup.
* **Concurrency Built In**: Async/await, actors, threads, and typed channels.
* **Self-Hosting & Extensible**: The compiler is written in Vyn; add backends, macros, and modules at runtime.

### 1.3 Key Concepts & Terminology

* **Template**: A generic type/function parameterized by types or constants, instantiated at compile time.
* **Scoped Block**: Any block prefixed with `scoped` that defers GC and cleans up at block exit.
* **Reference (`ref<T>`)**: An optional per-object reference-counted pointer for deterministic deallocation.
* **Actor**: Lightweight concurrent entity with a built-in mailbox for message passing.
* **Tiered JIT**: Two levels of execution—bytecode interpreter for startup, optimized native JIT for hot code.

### 1.4 How to Use This Guide

* **Sequential Reading**: Follow from Section 2 for hands-on tutorials.
* **Reference**: Jump to Section 8 for compiler and runtime internals.
* **Examples**: Copy-and-run code snippets in the Vyn REPL or CLI.

---

## 2. Getting Started

This section guides you through installing Vyn, writing your first programs, and using the interactive tools to accelerate development.

### 2.1 Installation & Toolchain Overview

Begin by downloading the Vyn SDK for your platform. The core components include:

* **Compiler (`vyn`)**: Translates `.vyn` source files to bytecode or native binaries.
* **REPL (`vyn repl`)**: Quick execution environment for testing snippets and debugging.
* **Package Manager (`vyn pm`)**: Fetches and builds third-party modules from the Vyn registry.

Installation is as simple as extracting the archive and adding `bin/` to your PATH. On Linux/macOS, you can also use the provided shell installer:

```bash
curl -sSf https://vyn.dev/install.sh | sh
```

### 2.2 Hello, Vyn! (First Program)

Create a file `hello.vyn`:

```vyn
fn main() {
  println("Hello, Vyn!")  // print to stdout
}
```

Compile and run:

```bash
vyn build hello.vyn     # produces ./hello binary
./hello                # outputs Hello, Vyn!
```

Or launch the REPL for rapid iteration:

```bash
vyn repl
> fn greet() { println("Hi!") }
> greet()
Hi!
```

### 2.3 Project Structure & Build Workflow

A typical Vyn project has:

```
myapp/
  src/
    main.vyn
    utils.vyn
  vyn.toml          # project manifest: name, version, deps
  vyn.lock          # locked dependencies
```

Use `vyn build` to compile or `vyn run` to compile+execute in one step. Dependencies declared in `vyn.toml` under `[dependencies]` are automatically fetched and compiled.

### 2.4 REPL & Interactive Debugging

The REPL is invaluable for experimenting and debugging:

* **Auto-completion**: Tab-complete identifiers and module paths.
* **`dbg(expr)`**: Prints expression value and location.
* **Breakpoint**: In REPL mode, prefix any statement with `!` to step through code interactively.

Example:

```bash
vyn repl
> let x = vec![1,2,3]
> dbg(x.len())        # prints 3 at line 1:1
> !for i in x { println(i) }  # step through each iteration
```

## 3. Language Fundamentals

In this section we cover the core building blocks of Vyn: its syntax, basic types, and fundamental constructs. Most code you write will use these concepts daily.

### 3.1 Basic Syntax & Data Types

Vyn uses indentation-sensitive syntax with optional semicolons. Whitespace defines blocks, so consistent indentation is key. Fundamental primitive types include:

* **Int**: signed integer, 64‑bit by default (`Int64`). Aliases `Int32`, `Int16`, `Int8`, and unsigned variants (`UInt*`) are available for narrower widths.
* **Float**: 64‑bit IEEE 754 (`Float64`) by default. A `Float32` type is provided when single‑precision is sufficient.
* **Bool**: 1‑byte boolean (`true` or `false`).
* **Char**: 1‑byte UTF‑8 code unit. For full Unicode code points, use `Rune` (32‑bit).
* **Bytes**: raw sequence of `UInt8` values.

Compound types:

* **Tuples**: `(T1, T2, ...)`
* **Fixed‑size arrays**: `[T; N]`
* **Dynamic vectors**: `Vec<T>` (mutable, heap‑allocated)

Strings:

* **String**: UTF-8 text type built on `Vec<UInt8>`; ideal for general text.
* **String<Char>**: sequence of 1-byte `Char` elements; useful when working with raw code units.
* **String<Rune>**: sequence of 32-bit `Rune` code points; guarantees full Unicode support.

Literals follow familiar forms:

```vyn
let x: Int = 42                   // 64‑bit integer
let small: Int16 = 123            // 16‑bit integer
let f: Float = 3.14               // 64‑bit float
let s: Float32 = 1.5              // 32‑bit float literal
let flag: Bool = true             // boolean
let ch: Char = 'A'                // single byte char
let rune: Rune = '💡'             // full Unicode point
let raw: Bytes = [0xDE,0xAD,0xBE] // raw bytes
let arr: [Int; 3] = [1, 2, 3]     // fixed array
let list: Vec<Int> = vec![4,5,6]  // dynamic vector
```

These primitives cover most use cases; if needed, additional fixed‑width or packed types can be introduced later.

### 3.2 Control Flow (if, for, while, match)

Vyn supports standard control structures:

* **`if` expressions** return values:

  ```vyn
  let msg = if x > 0 { "pos" } else { "neg" }
  ```
* **Loops**:

  ```vyn
  for i in 0..<10 { println(i) }
  var n = 0
  while n < 5 { n += 1 }
  ```
* **Pattern Matching** with `match` on enums or tuples:

  ```vyn
  match opt {
    Some(v) => println(v)
    None    => println("empty")
  }
  ```

### 3.3 Functions & Nested Functions

Functions form the basic unit of behavior. You can nest functions inside other functions, capturing outer variables:

```vyn
fn outer(x: Int) -> Int {
  fn inner(y: Int) -> Int { x + y }
  inner(10)
}
```

* **First-class**: Pass functions as values, assign to variables, or return them.

* **Anonymous lambdas**: inline, unnamed functions useful for short callbacks or functional programming patterns. Similar to JavaScript arrow functions or Python lambdas, they let you define behavior on the fly without declaring a separate named function:

  ```vyn
  let sq = fn(v: Int) -> Int { v * v }  // 'sq' holds a function that squares its input
  let nums = vec![1, 2, 3]
  let squares = nums.map(fn(n) -> Int { n * n })  // pass lambda directly
  ```

  Under the hood, lambdas produce a function object that can capture surrounding variables by value or reference, depending on usage.

### 3.4 Modules & Imports

Organize code into modules by file or `module` declarations. Import symbols:

```vyn
// in src/utils.vyn
fn helper() { ... }

// in src/main.vyn
import utils::helper
fn main() { helper() }
```

To include third-party modules, declare them under `[dependencies]` in `vyn.toml` and run `vyn pm install`. The package manager will fetch the specified version, verify checksums via `vyn.lock`, and make symbols available via `import <module>::<item>`. For example:

```toml
[dependencies]
http = "^1.2.0"
json = "~0.8"
```

## 4. Advanced Programming Constructs

This section dives into Vyn’s advanced features, from compile-time templates to fine-grained memory controls.

### 4.1 Templates & Generics

Vyn’s template system lets you write polymorphic code without runtime cost. Templates are resolved and monomorphized at compile time, producing efficient specialized functions and types.

```vyn
// Define a generic Pair of T and U
template Pair<T, U> {
  var first: T
  var second: U
  fn swap(self: &mut Pair<T, U>) {
    let tmp = self.first
    self.first = self.second
    self.second = tmp
  }
}

// Instantiate and use
fn main() {
  let p = Pair<Int, String>(first=1, second="one")
  dbg(p.second)  // "one"
}
```

### 4.2 Classes & Object-Oriented Features

You can define classes with fields, methods, inheritance, and traits. Classes are runtime constructs that describe object layouts, support encapsulation, and can carry state and identity. In contrast, **templates** are compile-time blueprints for generating types or functions parameterized by types or constants; they do not exist at runtime but produce fully specialized code before execution.

```vyn
// Base trait
template Drawable {
  fn draw(&self)
}

// Concrete class
class Circle: Drawable {
  var radius: Float
  fn draw(&self) {
    println("Drawing circle of radius {}", self.radius)
  }
}

fn render(shape: &Drawable) {
  shape.draw()
}
```

* **Class**: defines a concrete object with fields/methods; instances live on the heap or stack depending on allocation.
* **Template**: compile-time mechanism that generates zero-cost abstractions; once instantiated, templates vanish and leave normal classes or functions.

### 4.3 Algebraic Data Types & Pattern Matching

Enums can carry data, enabling expressive sum types:

```vyn
enum Result<T, E> {
  Ok(T),
  Err(E)
}

fn safe_div(a: Int, b: Int) -> Result<Int, String> {
  if b == 0 { Err("divide by zero") } else { Ok(a / b) }
}

fn main() {
  match safe_div(10, 0) {
    Ok(v)  => println("Got {}", v)
    Err(e) => println("Error: {}", e)
  }
}
```

Pattern matching is exhaustive by default; the compiler warns if cases are missing.

### 4.4 Scoped vs. Non-Scoped Memory Blocks

Use `scoped { … }` to defer GC and cleanup at block exit:

```vyn
fn example() {
  // non-scoped: GC can run mid-block
  let a = alloc<A>()

  // scoped: no GC until end of this block
  scoped {
    let b = alloc<B>()
    // b lives here
  } // b freed deterministically here
}
```

### 4.5 Memory Management (GC, Manual, Reference)

* **Default GC**: lazy, based on allocation pressure.
* **Manual**: call `free(ptr)` to immediately release.
* **Reference Counting**: wrap objects in `ref<T>` for automatic per-object cleanup and cyclic detection.

### 4.6 Inline Macros & Metaprogramming

Vyn macros run at compile time to generate or transform code. They are hygienic and can manipulate AST nodes:

```vyn
macro log_calls(fn_def) {
  // inject dbg() at start of function body
}

@log_calls
fn compute(x: Int) -> Int {
  x * 2
}
```

Macro definitions and usage will be covered in depth in Section 8.

---

## 5. Concurrency & Parallelism

Vyn offers built-in concurrency primitives that integrate smoothly with its type system and template model, enabling safe and efficient parallel code.

### 5.1 Async/Await Fundamentals

Use `async fn` to define asynchronous routines and `await` to suspend execution until a future completes. The compiler transforms these into coroutine state machines:

```vyn
async fn fetch_url(url: String) -> Bytes {
  let resp = await http::get(url)
  return resp.body
}

fn main() {
  let data = vyn::spawn(fetch_url("https://api.example.com/data"))
  // do other work...
  let payload = await data
  println("Received {} bytes", payload.len())
}
```

* **Futures**: `async fn` returns a `Future<T>`; `await` can only be used inside `async` contexts or within the REPL.
* **Spawn**: use `spawn` to schedule coroutines on the async scheduler.

### 5.2 Actors & Message Passing

Actors are lightweight, isolated units that communicate via typed mailboxes. Define actors as templates:

```vyn
template Counter {
  var count: Int = 0
  fn handle(&mut self, msg: Int) {
    self.count += msg
    dbg(self.count)
  }
}

fn main() {
  let counter = actor Counter()
  counter.send(5)
  counter.send(3)
  // messages processed asynchronously
}
```

* **`actor MyType()`**: spawns an actor instance of template `MyType`.
* **`send` / `recv`**: methods for sending messages into an actor’s queue; `recv` blocks in an async context.

### 5.3 Threads & ThreadPool Usage

Low-level threads are available via templates:

```vyn
// Spawn a thread returning Int
let handle: Thread<Int> = Thread::spawn(fn() -> Int {
  heavy_compute()
})
let result = handle.join()
```

Use a `ThreadPool<T>` to manage a fixed worker set:

```vyn
let pool = ThreadPool<String>(8)
let futs = for i in 0..<10 {
  pool.submit(fn() -> String { process(i) })
}
for f in futs { dbg(f.join()) }
```

* **`join()`**: waits for thread completion and returns its result.
* **`ThreadPool<T>`**: reuses worker threads for tasks of signature `fn() -> T`.

### 5.4 Channels & Synchronization Primitives

Channels return an `Option<T>` on `recv()`:

* `Some(value)` wraps a received message.
* `None` indicates the channel is closed and no more messages will arrive.

Channels provide safe message queues between threads and actors:

```vyn
let ch: Channel<Int> = Channel::bounded(100)

spawn fn producer() {
  for i in 0..<50 { ch.send(i) }
}
spawn fn consumer() {
  // recv() yields Option<Int>: Some(v) until channel closes
  while let Some(v) = ch.recv() { println(v) }
  // When producer is done and channel closed, recv() returns None and loop exits
}
```

* **Bounded / Unbounded**: choose capacity at creation time.
* **`recv()`**: blocks until a message is available or channel is closed.
* **`select`**: wait on multiple channels (covered in Section 8).

---

## 6. Systems Programming

This section explores Vyn’s low-level capabilities for systems programming, from raw OS interfaces to memory mapping and safe FFI.

### 6.1 Interfacing with `vyn::sys`

Vyn’s `sys` module offers thin, `unsafe` bindings to platform APIs. You get direct access to syscalls like `mmap`, `open`, and socket operations.

```vyn
import vyn::sys

unsafe fn read_file(path: String) -> Bytes {
  let fd = sys::open(path.to_cstr(), sys::O_RDONLY, 0)
  let buf = sys::mmap(nil, 4096, sys::Prot::READ, sys::Flags::PRIVATE | sys::Flags::ANONYMOUS, fd, 0)
  sys::close(fd)
  return Bytes(buf, 4096)
}
```

* All `sys::` calls are marked `unsafe`; wrap them in safe abstractions when possible.
* Use conditional compilation (`#cfg(linux)`, `#cfg(windows)`) to target multiple OS backends.

### 6.2 File I/O & Networking APIs

Vyn’s standard library builds on `sys` to provide safe, ergonomic I/O:

```vyn
import vyn::fs
import vyn::net

fn copy_file(src: String, dst: String) {
  let data = fs::read_to_end(src)
  fs::write_all(dst, data)
}

async fn fetch_http(url: String) -> String {
  let conn = net::HttpConnection::connect(url)
  conn.get("/").await.text().await
}
```

* **`fs`**: blocking file operations (`read_to_end`, `write_all`, directory traversal).
* **`net`**: TCP/UDP sockets, HTTP client with async support.

### 6.3 Memory Mapping & Low-Level Allocators

For high-performance scenarios, map files or anonymous memory directly:

```vyn
import vyn::mem

fn map_file(path: String) {
  let region = mem::MappedRegion::open(path, mem::Prot::READ)
  // region.ptr and region.len available
  // unmap when `region` drops out of scope
}
```

* **`MappedRegion`**: RAII wrapper around `mmap`/`munmap`.
* **Custom allocators**: implement the `Allocator` trait to plug in arena or region allocators.

### 6.4 Unsafe Code & FFI

When you need to call into native libraries, use Vyn’s FFI:

```vyn
#cfg(linux) extern "C" {
  fn printf(fmt: *char, ...)
}

fn main() {
  unsafe { printf("Hello %s
", "world") }
}
```

* Declare `extern "C"` blocks for C symbols.
* All calls are `unsafe`; ensure ABI matches and manage lifetimes manually.
* Use the `libffi` module for dynamic binding if needed.

### 6.5 Wrapping `unsafe` in Safe Abstractions

Vyn encourages minimizing `unsafe` usage by providing safe wrappers. Common `unsafe` scenarios:

* Raw pointer dereferencing (`*ptr`).
* Direct syscall interfaces (`sys::mmap`, `sys::read`).
* FFI calls (`extern "C"`).

**Example: Safe file reader using `unsafe` mmap**

```vyn
// Safe wrapper around sys::mmap
fn read_all(path: String) -> Bytes {
  unsafe {
    let fd = vyn::sys::open(path.to_cstr(), vyn::sys::O_RDONLY, 0)
    let ptr = vyn::sys::mmap(nil, let size = vyn::fs::file_size(fd), vyn::sys::Prot::READ, vyn::sys::Flags::PRIVATE, fd, 0)
    vyn::sys::close(fd)
    // slice bytes safely from raw pointer
    return Bytes(ptr, size)
  }
}
```

* The `unsafe` block is confined to system calls and pointer handling.
* Call site uses `read_all` without needing `unsafe`.

---

## 7. Tooling & Ecosystem

Vyn’s tooling ecosystem empowers developers with robust debugging, testing, package management, and profiling capabilities.

### 7.1 Debugger Commands & REPL Features

* **Integrated Debugger**: launch programs under the debugger with `vyn debug <binary>`; supports breakpoints (`break <file>:<line>`), watchpoints, call stack inspection, and variable evaluation.
* **Source-level Stepping**: step over (`next`), step into (`step`), and continue (`continue`) commands work seamlessly on both bytecode and JIT-compiled code.
* **REPL Debugging**: prefix statements with `!` in `vyn repl` to execute one line at a time, inspect locals, and modify variables on the fly.
* **`dbg(expr)`**: compile-time macro that injects a print of `expr` and its source location, useful for quick tracing without a full debug session.

### 7.2 Testing Framework & Property-based Testing

* **Unit Tests**: annotate functions with `@test`:

  ```vyn
  @test
  fn test_add() {
    assert(add(2,3) == 5)
  }
  ```
* **Test Runner**: invoke `vyn test` to discover and execute all tests, outputting summaries and failures.
* **Property-based Tests**: define `@prop` blocks to generate randomized inputs and verify invariants:

  ```vyn
  @prop
  fn prop_reverse(xs: Vec<Int>) {
    assert(xs.reverse().reverse() == xs)
  }
  ```
* **Coverage Reports**: use `vyn test --coverage` to generate HTML reports highlighting untested code paths.

### 7.3 Package Manager & Modules Repository

* **Manifest (`vyn.toml`)**: declare metadata, dependencies, and compiler settings.
* **Commands**:

  * `vyn pm install`: fetch and install dependencies into local cache.
  * `vyn pm update`: update to latest versions satisfying semver ranges.
  * `vyn pm publish`: publish your package to the Vyn registry.
* **Registry**: a central index of modules with versioning, checksum verification, and dependency resolution.
* **Local Overrides**: override registry packages by adding a `[patch]` section in `vyn.toml` pointing to a Git repo or local path.

### 7.4 Profiling & Performance Tuning

* **Built-in Profiler**: run `vyn run --profile` to collect CPU and memory usage samples; output flame graphs in SVG.
* **JIT Introspection**: `vyn profile jitted --hot` shows functions promoted to native code, along with compilation times and optimization metrics.
* **GC Tracing**: enable `--gc-trace` to log allocation events, collection cycles, and per-region statistics.
* **Benchmark Harness**: annotate `@bench` functions to run performance benchmarks and compare against previous runs:

  ```vyn
  @bench
  fn bench_sort() {
    let data = random_vec(100_000)
    sort(data)
  }
  ```
* **Integration with External Tools**: export profiling data in formats compatible with `perf`, `Valgrind`, or `pprof`.

---

## 8. Detailed System Design (Reference)

This section provides in-depth technical details necessary to implement the Vyn compiler, runtime, and standard library.

### 8.1 Compiler Architecture

#### 8.1.1 Frontend & Parser

* **Lexing**:

  * Tokenize indentation (INDENT/DEDENT) using a stack of indentation levels.
  * Recognize identifiers, keywords, literals, operators, and delimiters.
* **Grammar**:

  * Implement a PEG or LL(k) parser with backtracking for constructs like `match` and `macro` definitions.
  * Parse significant whitespace into block structures, emitting explicit BRACE\_OPEN/BRACE\_CLOSE tokens.
* **AST Nodes**:

  * Define node types: `ModuleNode`, `FuncDecl`, `ClassDecl`, `EnumDecl`, `Stmt`, `Expr`, `TemplateDecl`, `MacroDecl`.
  * Attach source-location metadata for error reporting and debug mapping.

#### 8.1.2 Semantic Analyzer

* **Symbol Table & Scoping**:

  * Build a hierarchical symbol table: global, module, class, function, block scopes.
  * Support shadowing and name hiding rules.
* **Type Inference & Checking**:

  * Implement Hindley-Milner style inference for let-bindings and function return types, with template parameters explicit.
  * Enforce mutability: `&T` vs `&mut T`; track ownership and lifetimes to prevent invalid borrows.
* **Template Instantiation**:

  * When encountering a `template` use, instantiate a specialized AST by substituting type arguments.
  * Cache instantiations to avoid duplicate work; generate unique mangled names.
* **Macro Expansion**:

  * Execute hygienic macros during semantic phase; transform AST subtrees and re-run analysis on generated code.
* **Borrow and Ownership Analysis**:

  * For each function, build a borrow graph tracking references and lifetimes.
  * Insert runtime checks or optimize based on escape analysis.

#### 8.1.3 Intermediate Representation (VIR)

* **Design Goals**: SSA-like, typed IR with explicit operations for templates, closures, and GC.
* **Instruction Set**:

  * `alloc T` / `free ptr` for heap management.
  * `load ptr` / `store ptr, value` for memory access.
  * `call func, args...` / `ret value` for function calls.
  * `branch cond, true_bb, false_bb`; `jump bb` for control flow.
  * `phi []` nodes for SSA merges.
  * `safepoint` markers for GC and deoptimization boundaries.
* **Coroutine Frames**:

  * Represent `async fn` as state machines with an explicit frame object storing local variables and resume points.
  * Transform `await` into yield/callback state transitions.
* **Scoped Regions**:

  * Introduce `enter_region id` / `exit_region id` instructions framing `scoped` blocks.
  * Region allocator metadata tracks allocations per region for bulk deallocation.

#### 8.1.4 Optimization Passes

* **Inlining**: replace calls to small functions or methods with their bodies when beneficial.
* **Dead Code Elimination**: remove unreachable blocks and unused variables after CFG construction.
* **Escape Analysis**:

  * Determine if heap allocations can be stack-allocated.
  * Promote short-lived objects (in scoped blocks) onto stack frames.
* **Template Specialization**: merge identical instantiations; drop unused template parameters.
* **Loop Transformations**: unroll small loops, apply vectorization hints for numeric code.

#### 8.1.5 Code Generation

* **Bytecode Emitter**:

  * Map VIR instructions to a compact binary format with opcode + operands.
  * Emit metadata sections: function tables, type info, debug symbols, GC root maps.
* **Native JIT Backend**:

  * Use LLVM IR generator or custom assembler for target architectures (x86\_64, ARM64).
  * Translate VIR to target IR, preserving safepoints and exception unwind tables.
  * Implement tiered compilation:

    1. **Tier 0**: direct bytecode interpretation.
    2. **Tier 1**: baseline native code with minimal optimization.
    3. **Tier 2**: optimized native code after hot-spot profiling.
  * Support on-stack replacement (OSR) to swap interpreted frames with compiled frames.

### 8.2 Runtime Components

#### 8.2.1 Bytecode VM

* **Architecture**: register-based for performance; use a dispatch loop with threaded code or computed goto.
* **Frame Layout**:

  * Call frame contains return address, locals array, operand stack pointer, region stack pointer.
  * Maintain a linked list of frames for backtrace.
* **Scheduler Integration**:

  * On encountering `await`, save frame state into a promise object and switch to next runnable frame.
  * Maintain run queues for coroutines and actors.

#### 8.2.2 JIT Engine

* **Profiling**: increment execution counters on method entry/loop backedges.
* **Hot Method Threshold**: configurable count to trigger tier-1 compilation.
* **Code Cache**: map function IDs to compiled machine code pointers; patch bytecode call sites to direct calls.
* **Deoptimization**: restore interpreter frames when GC relocates objects or when debugging.

#### 8.2.3 Garbage Collector

* **Lazy GC**:

  * Generational collector with two or three generations.
  * Trigger root scanning when heap usage exceeds thresholds.
  * Use write barrier on pointer stores in older-to-younger references.
* **Scoped Regions**:

  * Region allocator allocates from a bump pointer; track region boundaries per frame.
  * On `exit_region`, reset bump pointer, optionally running destructors for objects with finalizers.
* **Reference Counting**:

  * Use atomic reference counters for `ref<T>` pointers.
  * Periodically run cycle detector: mark-and-sweep over suspected cycles.
  * Allow toggling RC mode per object or module.

#### 8.2.4 Async Scheduler

* **Work-stealing deque** for coroutine tasks; each worker thread has a local queue.
* **Task Prioritization**: FIFO for fairness, or priority queues for latency-sensitive tasks.

#### 8.2.5 Thread/Actor Runtime

* **Thread Pool**:

  * Pre-spawn worker threads; each runs a loop fetching tasks from shared queue.
  * Support thread affinity and pinning for NUMA optimization.
* **Actor Mailboxes**:

  * Lock-free MPSC queues; implement mailbox per actor instance.
  * Backpressure policy: bounded mailboxes drop or block senders.

### 8.3 Standard Library Structure

#### 8.3.1 Module Layout

```
// Core
vyn::sys       // raw FFI and syscall bindings
vyn::mem       // allocators, MappedRegion, Arenas
vyn::fmt       // formatting macros, serializers

// Data structures
vyn::collections // Vec, HashMap, BTree, Sets

// Concurrency
vyn::task      // async/await primitives, Future, Executor
vyn::sync      // Mutex, Atomic*, Channel, Thread

// I/O
vyn::fs        // file, directory operations
vyn::net       // TCP, UDP, HTTP

// Utilities
vyn::time      // clocks, timers
vyn::test      // test harness, @test, @prop
vyn::cli       // argument parsing
```

#### 8.3.2 Interface Contracts

* Each module exposes a clear public API; internal details hidden behind `internal` namespace.
* Use traits to abstract over concrete implementations (e.g., `Allocator`, `Stream`).

### 8.4 Extension Points & Plugins

#### 8.4.1 Macro Plugins

* **Plugin API**:

  * Define `proc_macro(name: String, input: AST) -> AST` functions.
  * Load via `[plugins]` section in `vyn.toml`.
* **Hygiene**:

  * Maintain unique name scopes to avoid collisions.
  * Provide `quote!` and `splice!` macros for AST construction.

#### 8.4.2 Custom Backends

* Implement `Backend` trait with methods:

  * `fn emit_bytecode(module: BytecodeModule)`
  * `fn emit_native(module: VIRModule, opt_level: OptLevel) -> MachineCode`
* Register backends via `vyn pm add-backend <name>`.

#### 8.4.3 Hot-Reload Modules

* **Load/Unload**:

  * Support dynamic linking of bytecode modules into VM address space.
  * Maintain versioned symbol tables; redirect call sites on reload.
* **State Preservation**:

  * Allow retaining global variables; require explicit migration hooks for stateful modules.

#### 8.4.4 Profiling & Instrumentation

* Insert instrumentation during VIR generation:

  * `trace_event(category, name, timestamp)` hooks.
  * Sampling counters for memory and CPU usage.
* Output formats:

  * JSON trace for Chrome Tracing
  * `pprof` protobuf for Go-style profiles

---

*End of Detailed Design*

## 9. Glossary

**AST** (Abstract Syntax Tree)
Tree representation of source code structure produced by the parser.

**Bytecode**
Compact, platform-independent instruction set executed by the VM.

**GC** (Garbage Collection)
Automated memory reclamation mechanism.

**IR** (Intermediate Representation)
Typed, SSA-like representation used for optimizations and codegen.

**JIT** (Just-In-Time)
Runtime compilation of IR or bytecode to native machine code.

**LL(k)**
Top-down parsing strategy with k-token lookahead.

**MPSC** (Multiple Producer Single Consumer)
Lock-free queue pattern used for actor mailboxes.

**OSR** (On-Stack Replacement)
Technique for swapping interpreted frames with compiled frames at runtime.

**PEG** (Parsing Expression Grammar)
Grammar formalism suitable for recursive-descent parsing with backtracking.

**RC** (Reference Counting)
Per-object count-based memory management with optional cycle detection.

**SSA** (Static Single Assignment)
IR property where each variable is assigned exactly once.

**VM** (Virtual Machine)
Runtime engine interpreting bytecode and hosting JITed code.

---


*Spec version 0.1 — feedback welcome.* — feedback welcome.\*
