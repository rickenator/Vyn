# Vyn Memory Model Proposal

This document unifies design decisions and incorporates team feedback on Vyn's memory model, ownership, mutability, and safety mechanisms.

---

## 1. Overview

Vyn’s memory model combines two primary axes for safe bindings plus a third for raw pointers:

1. **Binding Mutability** (`var` vs `const`)
2. **Ownership & Data Mutability** (`my<T>` / `our<T>` / `their<T>` with optional `T const`)
3. **Raw Pointers** (`ptr<T>`, gated by `unsafe { … }`)

Safe code uses only `my`/`our`/`their`; raw pointers (`ptr<T>`) live behind explicit `unsafe { … }` blocks.

---

## 2. Binding Mutability

Vyn uses a simple two-keyword model for variable bindings:

* **`var`**: mutable binding (can be reassigned)
* **`const`**: immutable binding (rebinding prohibited)

The `const` keyword on the type itself is reserved for data immutability (`T const`).

**Example**:

```vyn
var counter: Int = 0    // mutable variable
const limit: Int = 100  // immutable binding
```

---

## 3. Ownership Qualifiers

* **`my<T>`**: unique-own pointer (like Rust’s `Box<T>`)
* **`our<T>`**: shared-own pointer (ref-counted, like `Rc<T>`/`Arc<T>`)
* **`their<T>`**: borrowed pointer (non-owning reference, like `&T`/`&mut T`)
* **`ptr<T>`**: raw pointer (`T*`), operations gated by `unsafe { … }`

Data mutability is controlled by `const` on the pointee: e.g., `my<Foo const>` means unique-own pointer to immutable data.

---

## 4. Borrow Creation Syntax

Borrow creation is a compiler intrinsic:

```vyn
// Immutable borrow (read-only)
var br1: their<Foo const> = borrow(owner)
// Mutable borrow
var br2: their<Foo> = borrow_mut(owner)
```

* `borrow(owner)`: creates `their<T const>` from a `my<T>` or `our<T>`
* `borrow_mut(owner)`: creates `their<T>` if no conflicting borrows exist

Compile-time checks enforce lifetimes and aliasing rules.

---

## 5. Shared Ownership & Concurrency

Direct mutation behind `our<T>` is allowed in single-threaded code. For thread-safe mutable sharing, wrap in sync primitives:

```vyn
var shared: our<Mutex<Bar>> = make_our(Mutex(Bar{v:1}))
```

Immutable shared data (`our<T const>`) is thread-safe by default.

---

## 6. Helper Intrinsics

* `make_our(expr)`: returns `our<T>` by allocating and ref‑counting
* `borrow(x)` / `borrow_mut(x)`: create `their<T const>` / `their<T>` borrows
* `alloc(n)` / `free(p)`: raw memory operations for `ptr<T>`
* **`sizeof(Type)`**: built‑in compile‑time operator returning the size (in bytes) of `Type`

---

## 7. Concurrency Primitives: Mutex<T>

A `Mutex<T>` provides mutual-exclusion access to `T` in multi-threaded contexts. Internally it uses a queue so threads acquire the lock roughly in FIFO order. Ownership of the lock—the “baton”—is represented by a `LockGuard<T>` which releases the lock when it goes out of scope.

### 7.1 Core APIs

```vyn
// Create a shared, thread-safe Foo
var shared_foo: our<Mutex<Foo>> = make_our(Mutex{ value: Foo{/*...*/} })

// 1. Blocking lock (wait indefinitely)
let guard: LockGuard<Foo> = shared_foo.lock()
// guard.value.do_something()   // access protected data
// `guard` drops here → lock released

// 2. Non-blocking attempt
match shared_foo.try_lock() {
  Some(guard) => { /* got the baton */ }
  None        => { /* someone else holds it */ }
}

// 3. Timed lock
match shared_foo.lock_timeout(Duration{ ms:500 }) {
  Some(guard) => { /* acquired within 500 ms */ }
  None        => { /* timeout → no deadlock hang */ }
}
```

*   **`lock()`**: Blocks the calling thread until the lock is available. Returns a `LockGuard<T>` that holds the lock.
*   **`try_lock()`**: Attempts to acquire the lock immediately. Returns `Some(LockGuard<T>)` on success or `None` if already held.
*   **`lock_timeout(dur: Duration)`**: Blocks up to `dur`. Returns `Some(LockGuard<T>)` if acquired in time, or `None` on timeout.

### 7.2 Baton Passing & Fairness

Threads queue on contention. When a `LockGuard<T>` is dropped, the next thread in line wakes and receives the baton. Starvation is unlikely under FIFO queuing, but exact fairness guarantees depend on the runtime/OS scheduler.

### 7.3 Poisoning

If a thread panics (or throws an unrecoverable error) while holding the `LockGuard<T>`, the mutex enters a poisoned state. Subsequent `lock()` or `try_lock()` attempts will return a `Result<LockGuard<T>, PoisonError>` (or a similar error-indicating mechanism) so callers can choose to recover or abort, acknowledging the potentially inconsistent state of the protected data.

### 7.4 Deadlock Avoidance

Vyn’s `Mutex<T>` does not automatically detect deadlocks (e.g., lock cycles). To prevent deadlocks:

*   **Lock ordering**: Establish and adhere to a global order in which multiple mutexes are acquired.
*   **Use timeouts**: Prefer `lock_timeout(...)` over `lock()` when deadlock is a possibility, allowing the program to react to a timeout instead of hanging indefinitely.
*   **Try-lock loops**: Implement more complex acquisition patterns, such as trying to acquire locks and releasing them if not all can be obtained.
    ```vyn
    loop {
      if let Some(g1) = m1.try_lock() {
        if let Some(g2) = m2.try_lock() {
          // Both locks acquired
          // ... use g1 and g2 ...
          break // Exit loop
        }
        // g1 is dropped here, releasing m1 before retrying
      }
      sleep(Duration{ ms:10 }) // Wait before retrying
    }
    ```
*   **Higher-level abstractions**: For closely related data, consider wrapping them in a single `Mutex<(A,B)>` to manage them under one lock, avoiding the need for nested locks.

### 7.5 Summary

The `LockGuard<T>` scope defines the "baton" of lock ownership. Timeouts via `lock_timeout` prevent indefinite blocking. Poisoning signals an unsafe state after a panic during a critical section. Deadlock is a developer responsibility, mitigated by strategies like lock ordering, timeouts, or restructuring lock acquisitions. This design aims to balance simplicity (e.g., `lock()`) with safety features (timeouts, poisoning) and clear semantics for concurrent access.

---

## 8. Unsafe-Scoped Blocks

```vyn
fn use_raw()
  unsafe {
    var p: ptr<Foo> = alloc(sizeof(Foo))
    (*p).field = 42
    free(p)
  }
// Outside, `p` can be moved or stored safely
```

**Comment:** Scoped `unsafe { … }` blocks localize undefined behavior risks.

---

## 9. Combinations Matrix

| Declaration                 | Binding   | Ownership | Data Mutability | Safe?        |
| --------------------------- | --------- | --------- | --------------- | ------------ |
| `var p: my<Foo>`            | mutable   | unique    | mutable         | ✔️           |
| `const p: my<Foo>`          | immutable | unique    | mutable         | ✔️           |
| `var p: my<Foo const>`      | mutable   | unique    | immutable       | ✔️           |
| `const p: my<Foo const>`    | immutable | unique    | immutable       | ✔️           |
| `var p: our<Foo>`           | mutable   | shared    | mutable         | ✔️           |
| `const p: our<Foo>`         | immutable | shared    | mutable         | ✔️           |
| `var p: our<Foo const>`     | mutable   | shared    | immutable       | ✔️           |
| `const p: our<Foo const>`   | immutable | shared    | immutable       | ✔️           |
| `var p: their<Foo>`         | mutable   | borrowed  | mutable         | ✔️           |
| `const p: their<Foo>`       | immutable | borrowed  | mutable         | ✔️           |
| `var p: their<Foo const>`   | mutable   | borrowed  | immutable       | ✔️           |
| `const p: their<Foo const>` | immutable | borrowed  | immutable       | ✔️           |
| `var p: ptr<Foo>`           | mutable   | raw       | mutable         | ❌ (`unsafe`) |
| `const p: ptr<Foo>`         | immutable | raw       | mutable         | ❌ (`unsafe`) |
| `var p: ptr<Foo const>`     | mutable   | raw       | immutable       | ❌ (`unsafe`) |
| `const p: ptr<Foo const>`   | immutable | raw       | immutable       | ❌ (`unsafe`) |

---

## 10. Sample Code

### 10.1 Unique Ownership (`my<T>`)

```vyn
var a: my<Foo> = Foo{x:0}
a.x = 5            // OK

const b: my<Foo const> = Foo{x:10}
// b.x = 15        // Error: data is const
```

### 10.2 Shared Ownership (`our<T>`)

```vyn
var s: our<Bar> = make_our(Bar{v:1})
var ts: our<Mutex<Bar>> = make_our(Mutex(Bar{v:2}))
```

### 10.3 Borrowed References (`their<T>`)

```vyn
var owner: my<Baz> = Baz{v:99}
var br1: their<Baz const> = borrow(owner)    // read-only borrow
var br2: their<Baz> = borrow_mut(owner)      // mutable borrow
```

### 10.4 Raw Pointers (`ptr<T>`)

```vyn
fn alloc_foo() -> ptr<Foo>
  unsafe {
    return alloc(sizeof(Foo))
  }

fn main()
  var p: ptr<Foo> = alloc_foo()
  unsafe {
    (*p).x = 123
    free(p)
  }
```

---

## 11. Grammar & EBNF Impact

```ebnf
Type       ::= BaseType [ 'const' ]
BaseType   ::= IDENTIFIER
             | OwnershipWrapper '<' Type '>'
OwnershipWrapper ::= 'my' | 'our' | 'their' | 'ptr'

BorrowExpr ::= 'borrow' '(' Expr ')'
            | 'borrow_mut' '(' Expr ')'
```

---

## 12. Safety Guarantees

1. **Unique owner** (`my<T>`) ensures unique ownership. Access to its data is governed by borrow‑checking rules when `their<T>` references are created.
