**Vela Specification**

---

## 1. Introduction

Vela is a modern systems language designed for both low‑level control and high‑level expressiveness. It targets native executables through a hybrid VM+JIT backend, while remaining self‑hosted. Vela aims to combine:

* **Performance**: Ahead‑of‑time compilation to optimized native code.
* **Safety & Control**: Hybrid GC/manual memory, linear types for special resources.
* **Expressiveness**: Templates everywhere, OO+FP, terse syntax, algebraic types.

Vela uniquely blends template metaprogramming with a hybrid GC/manual memory model, providing both low-level control and high-level expressiveness.

---

## 2. Features

1. **Hybrid VM / Tiered JIT**

   * Fast startup via bytecode VM; native code tier for heavy paths.
2. **Self-hosted Compiler**

   * Vela compiler written in Vela; bootstrapped with minimal C frontend.
3. **Memory Management**

   * **Hybrid GC/Manual**: traced GC by default; `free(obj)` for manual release.
   * **Scoped & Regions**: `scoped` blocks and arenas for deterministic, scoped cleanup.
   * **Reference Counting**: `ref<T>` pointers with optional cycle detector for per-object collection.
4. **Template Metaprogramming**

   * Monomorphized generics on types and functions.
5. **Syntax & Paradigms**

   * Indentation-sensitive, semicolons optional.
   * OO classes + methods; nested functions, closures.
   * Algebraic Data Types & Pattern Matching.
6. **Concurrency & Asynchrony**

   * `async/await`, coroutines.
   * Actor model with mailboxes (`actor { ... }`).
7. **Typed Threading Model**

   * `Thread<T>` templates to spawn threads returning `T`.
   * `ThreadPool<T>` for pooling workers typed by task signature.
   * Channels `Channel<T>` for safe message passing.
8. **Debug & REPL**

   * Built-in `dbg(expr)` + `assert`; REPL support on failures.
9. **Hot-Reloadable Modules**

   * Swap VM modules at runtime without restart.
10. **Compile-time Macros**

* Hygienic meta-macros for code generation.

11. **Cross-Platform Sys Layer**

    * Thin `vela::sys` raw FFI; safe wrappers in `std` modules.
12. **Dual Target**

    * Native JIT or WebAssembly output.
13. **Property-based Testing**

    * `prop` blocks integrated into test harness.

---

## 3. Architecture

### 3.1 Compiler Pipeline

1. **Frontend**: Lexing & parsing → AST.
2. **Semantic Analysis**: Type checking, template instantiation.
3. **IR Generation**: Lower AST to V‑IR (intermediate representation).
4. **VM Bytecode & AOT**: Emit compact bytecode + optional AOT native object files.
5. **JIT Engine**: Tiered compilation at runtime, using LLVM backend for native code.
6. **REPL & Debug Server**: Hook into IR/JIT for live introspection.

### 3.2 Runtime Components

* **Bytecode VM**: Stack‑based interpreter, sandboxed.
* **JIT Compiler**: Profiles hot functions; recompiles with optimizations.
* **GC & Arenas**: Generational GC + region arenas for scoped allocators.
* **Async Scheduler**: Coroutine queue, actor mailboxes.

### 3.3 Standard Library Layout

```
vela::
  sys         // raw OS bindings
  mem         // allocators, mmap wrappers
  fs          // file I/O
  net         // sockets, HTTP client
  fmt         // formatting, serialization
  test        // unit & property testers
  sync        // atomics, channels
  time        // timers, clocks
```

---

## 4. Detailed Specification

### 4.1 Lexical Grammar

* Identifiers: `[A-Za-z_][A-Za-z0-9_]*`
* Numeric: Int, Float, Hex, Binary literals
* Strings: `"..."`, raw `r#"..."#`
* Significant whitespace: indent/dedent tokens.
* Comments: `//` single, `/* ... */` multi.

### 4.2 Syntax & AST Nodes

```bnf
program      ::= { declaration }
declaration  ::= func_decl | class_decl | enum_decl | import_decl
func_decl    ::= ["async"] "fn" ident "(" params ")" [":" type] block
class_decl   ::= "class" ident ["<" generics ">"] block
enum_decl    ::= "enum" ident ["<" generics ">"] "{" variants "}"
block        ::= ["scoped"] "{" { statement } "}"

statement    ::= var_decl | expr_stmt | if_stmt | for_stmt | match_stmt | return_stmt
```

### 4.3 Type System

* **Primitive**: `Int`, `Float`, `Bool`, `Char`, `Bytes`.
* **Tuples**: `(T1, T2, ...)`
* **Arrays**: `[T; N]`, `Vec<T>`.
* **Templates**: `Type<Args...>`.
* **Functions**: `fn(T1, T2)->R`, support for varargs.
* **Ownership**: `&T`, `&mut T`, `T` (owned), `*T` (raw pointer).
* **Lifetimes** inferred by compiler, explicit annotations optional.
* **ADT**: `enum`, with pattern matching.

### 4.4 Memory, Concurrency & Threading

#### Garbage Collection Modes & JIT Interaction

* **Default (Lazy GC)**: runs on allocation pressure or explicit `collect()` calls, minimizing pauses. In the hybrid tiered JIT, tier‑1 (interpreted/low‑opt) code defers GC aggressiveness to avoid interrupting startup, whereas tier‑2 (optimized native) regions may trigger collections more proactively at safepoints to reclaim memory during long‑running hot paths.
* **Scoped GC**: within a `scoped { ... }` block, collection is deferred until scope exit, ensuring deterministic cleanup points.
* **Reference Collection**: enable per‑object reference counting via `ref<T>` pointers; cycles broken via optional cycle detector.

#### APIs

* **GC Alloc / Collect**:

  ```vela
  let obj = alloc<T>()        // allocated under current GC mode
  collect()                   // trigger lazy GC immediately
  ```

  // Trigger GC at next safe point

* **Scoped Blocks**:

  ```vela
  scoped {
    let a = alloc<A>()        // lives until scope exit
    let b = alloc<B>()        // cleaned up at block end
    // no GC until leaving scope
  }
  // GC may run now
  ```

* **Reference Pointers**:

  ```vela
  let r: ref<Node> = make_ref(Node())  // RC-enabled object
  r2 = r.clone()                       // increments count
  drop(r)                              // decrements count
  // cycle detector runs periodically
  ```

#### Manual Memory

* **Manual Free**: `free(ptr)` removes object from GC roots and deallocates immediately if possible.

#### Arenas & Regions

* **Arenas**: `Arena::new()`, `with_arena { ... }` for fast bulk deallocation at scope exit.

#### Async & Threading

* **Async**: `async fn`, `await`, `spawn`, `actor`.
* **Threads**:

  * `Thread<T>`: template to spawn a thread that computes a `T` result.

    ```vela
    let handle: Thread<Int> = Thread::spawn(fn() -> Int { compute() })
    let result: Int = handle.join()
    ```
  * `ThreadPool<T>`: fixed-size pool for tasks returning `T`.

    ```vela
    let pool = ThreadPool<String>(4)
    let fut = pool.submit(fn() -> String { work() })
    dbg(fut.join())
    ```
* **Channels**:

  * `Channel<T>`: typed, bounded/unbounded message queues.

    ```vela
    let ch: Channel<Msg> = Channel::unbounded()
    spawn fn sender() { ch.send(Msg::Ping) }
    spawn fn receiver() {
      match ch.recv() { ... }
    }
    ```

---

### 4.5 Sys Module

```vela
module vela::sys {
  enum Prot { READ, WRITE, EXEC }
  enum Flags { PRIVATE, SHARED, ANONYMOUS }
  unsafe fn mmap(addr: *u8?, len: usize,
                 prot: Prot, flags: Flags,
                 fd: Int, off: Int) -> *u8?
  unsafe fn munmap(addr: *u8, len: usize) -> Int
  // socket, file, thread syscalls...
}
```

---

## 5. Examples

### 5.1 Hello World

```vela
fn main() {
  println("Hello, Vela!")
}
```

### 5.2 Template & GC

```vela
template Node<T> {
  var value: T
  var next: Node<T>?    // GC’d by default
}
fn push_list(mut head: Node<Int>?, v: Int) {
  head = alloc<Node<Int>>()
  head.value = v
  head.next = old_head
}
```

### 5.3 Pattern Match & Actor

```vela
enum Msg { Ping, Pong }

actor Pinger {
  fn start(self) {
    send!(other, Ping)
    match recv!() {
      Pong => dbg("Got Pong")
    }
  }
}
```

### 5.4 Scoped vs Non-Scoped Blocks

```vela
fn process_data() {
  // Non-scoped block: GC may run anytime
  {
    let tmp = alloc<Buffer>()  // allocated and may be collected
    tmp.fill()
    // Enter a nested scoped block inside non-scoped
    scoped {
      let inner = alloc<Inner>()  // lives until inner block exit
      inner.setup()
    } // inner is cleaned up here
  } // tmp may be collected here or later

  // Scoped block: allocations deferred until exit
  scoped {
    let a = alloc<A>()        // lives until block exit
    // You can nest non-scoped block inside scoped
    {
      let fast = alloc<Fast>() // may be collected anytime within outer scope
      fast.run()
    }
    let b = alloc<B>()        // cleaned up at scope end
  } // a and b are cleaned up here deterministically

  // Back to default behavior
  dbg("Process complete")       // final debug print
}
```

*Spec version 0.1 — feedback welcome.* — feedback welcome.\*
