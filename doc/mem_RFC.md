# Vyn Memory Model Proposal

This document unifies design decisions and incorporates team feedback on Vyn's memory model, ownership, mutability, and safety mechanisms.

---

## 1. Overview

Vyn’s memory model combines two primary axes for safe bindings plus a third for raw locations:

1. **Binding Mutability** (`var` vs `const`)
2. **Ownership & Data Mutability** (`my<T>` / `our<T>` / `their<T>` with optional `T const`)
3. **Raw Locations** (`loc<T>`, gated by `unsafe { … }`)

Safe code uses only `my`/`our`/`their`; raw locations (`loc<T>`) live behind explicit `unsafe { … }` blocks.

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
* **`loc<T>`**: raw location (`T*`), operations gated by `unsafe { … }`

Data mutability is controlled by `const` on the pointee: e.g., `my<Foo const>` means unique-own pointer to immutable data.

---

## 4. Borrow Creation Syntax

Borrow creation is achieved using the `borrow` and `view` keywords, which act as prefix operators:

```vyn
// Immutable borrow (read-only view)
var v: their<Foo const> = view owner_expr // or view(owner_expr) for clarity
// Mutable borrow
var b: their<Foo> = borrow owner_expr   // or borrow(owner_expr) for clarity
```

* `view <expr>`: creates `their<T const>` (an immutable view) from a `my<T>`, `our<T>`, or another `their<T>`. This is equivalent to an immutable borrow.
* `borrow <expr>`: creates `their<T>` (a mutable borrow) if no conflicting borrows exist. This is equivalent to a mutable borrow.

Compile-time checks enforce lifetimes and aliasing rules (e.g., one mutable borrow or multiple immutable views, but not both simultaneously).

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
* `view <expr>`: creates `their<T const>` (immutable view/borrow)
* `borrow <expr>`: creates `their<T>` (mutable borrow)
* `alloc(n)` / `free(l)`: raw memory operations for `loc<T>`
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
    var l: loc<Foo> = alloc(sizeof(Foo))
    (*l).field = 42
    free(l)
  }
// Outside, `l` can be moved or stored safely
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
| `var l: loc<Foo>`           | mutable   | raw       | mutable         | ❌ (`unsafe`) |
| `const l: loc<Foo>`         | immutable | raw       | mutable         | ❌ (`unsafe`) |
| `var l: loc<Foo const>`     | mutable   | raw       | immutable       | ❌ (`unsafe`) |
| `const l: loc<Foo const>`   | immutable | raw       | immutable       | ❌ (`unsafe`) |

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
var v1: their<Baz const> = view owner    // read-only view (immutable borrow)
var b1: their<Baz> = borrow owner      // mutable borrow

// Example with an existing borrow
var b2: their<Baz> = borrow b1         // re-borrowing (mutable)
var v2: their<Baz const> = view b1     // creating an immutable view from a mutable borrow
var v3: their<Baz const> = view v1     // creating an immutable view from an immutable view

// Invalid: cannot mutably borrow while an immutable view exists (or vice-versa if b1 was still active)
// var b3: their<Baz> = borrow v1 // This would be a compile-time error if v1 is still live and used.
```

### 10.4 Raw Locations (`loc<T>`)

```vyn
fn alloc_foo() -> loc<Foo>
  unsafe {
    return alloc(sizeof(Foo))
  }

fn main()
  var l: loc<Foo> = alloc_foo()
  unsafe {
    (*l).x = 123
    free(l)
  }
```

---

## 11. Grammar & EBNF Impact

```ebnf
Type       ::= BaseType [ 'const' ]
BaseType   ::= IDENTIFIER
             | OwnershipWrapper '<' Type '>'
OwnershipWrapper ::= 'my' | 'our' | 'their' | 'loc'

Expr       ::= ...
             | BorrowExpr
             | ViewExpr
             | ...

BorrowExpr ::= 'borrow' Expr // Precedence similar to other unary operators
ViewExpr   ::= 'view' Expr   // Precedence similar to other unary operators
```

---

## 12. Safety Guarantees

1. **Unique owner** (`my<T>`) ensures unique ownership. Access to its data is governed by borrow‑checking rules when `their<T>` references are created.
