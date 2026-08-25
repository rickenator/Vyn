# #149 handoff — two RFEs to close the confirmed ownership-boundary gaps

Companion to the boundary-audit comment on #149 (comment 5404314176). The five
RED regression tests landed in `test/ownership/` (commit 094f834); they turn
GREEN only when these two fixes land. Do each, run the full suite, keep it
GREEN (includes the 5 now-RED ownership tests passing again).

---

## RFE A — Reject `their<T>` borrows that outlive their owner (borrow escape)

**Goal.** A `their<T>` reference must not be usable after the scope that granted
the borrow has exited, nor returned from a function whose owner is a dead local.

**Current state (verified, HEAD 22197fc).**
- Borrows are tracked lexically in `SemanticAnalyzer::borrowScopes` (a stack of
  `map<rootName, {mutable,immutable}>`), pushed/popped by `enterScope()` /
  `exitScope()` (src/vre/semantic.cpp:699/709). `recordBorrow()`
  (semantic.cpp:885-915) already rejects mutable-while-borrowed and
  view-while-mutable.
- `borrow(x)` / `view(x)` is typed at src/vre/semantic.cpp:2442 (requires an
  lvalue; produces `their<T>`).
- **Gap.** The `their<T>` *value's* lifetime is never tied to the borrow
  registration. When an inner scope exits, `exitScope()` pops the borrow scope,
  but an escaped `their<T>` is used unchecked afterwards — reading freed stack
  (ASLR-varying garbage). `test/ownership/borrow_escape_{scope,return,struct_field}.vyb`
  are RED today and must be rejected.

**Requirements.**
1. Couple the `their<T>` result's lifetime to the scope block that contains its
   `borrow()`/`view()` call: using the value (deref, field access, `v = r`, or
   assignment into a binding that outlives it) outside that block is a
   "borrow escapes its scope" error. A scope-local approximation is fine — do
   NOT build a general region/lifetime solver.
2. Return path: a function returning `their<T>` must borrow something alive past
   the return (a parameter, an `our`/`my`/`my` that outlives), never a leaf
   local. Reject `return borrow(local)`.
3. Emit a diagnostic containing the substring `borrow` (so the new
   `@expect-error: borrow` asserts match) and fail semantically (exit 1) so the
   `@expect: fail` tests pass.
4. Update Programmer's Guide §3.13 to state exactly the lexical guarantee now
   enforced (removes the general-lifetime over-claim #149 is about).

**Acceptance.** `./build/vyb test/ownership/borrow_escape_*.vyb` exit 1 with a
`borrow` error; `borrow_scope_release.vyb`, `borrow_overlap_rejected.vyb`, and
the rest of the suite unaffected; full `python3 test/run_tests.py` GREEN.

---

## RFE B — `thread_spawn` must not capture owning/borrowing state without a Send boundary

**Goal.** A closure passed to `thread_spawn` must not silently capture a `my<T>`
(unique owner of a heap allocation) or a `their<T>` borrow of a frame that can
die on the spawning thread; today this shares non-thread-safe ownership across
threads (race / use-after-free).

**Current state (verified).**
- `thread_spawn` is a free function in stdlib/threads (tests import it by name,
  e.g. `import threads::{thread_spawn, thread_join}` in test/tls/*).
- A Vyb closure captures its environment (closure struct fields); the semantic
  captures `my<T>` by move (use-after-move on the outer var) and plain values by
  value. There is NO check at the closure→thread boundary.
- **Gap.** `test/ownership/thread_send_my_struct.vyb`: the worker reads the
  caller's `my<Node>` (heap String), rc=0 today. `thread_send_their.vyb`: the
  worker derefs a `their<Counter>` borrowed from main's frame, rc=0 today.
  Both are RED tests that must be rejected.

**Requirements.**
1. At the closure→`thread_spawn` boundary, inspect the closure's captured
   environment; reject if it holds a `my<T>` field or a `their<T>` field whose
   root is not safe to share across threads.
2. Permit explicit transferable cases: the value is copied before the boundary,
   or it is an `our<T>` (refcounted; document that mutation may race and is the
   caller's responsibility).
3. Emit a diagnostic containing `thread` (matches the new `@expect-error: thread`
   asserts) and fail semantically (exit 1).
4. Document the boundary in the threads module and §3.13: which captures are
   legal across `thread_spawn`.

**Acceptance.** `./build/vyb test/ownership/thread_send_*.vyb` exit 1 with a
`thread` diagnostic; existing threaded stdlib tests (test/threads/*, test/tls/*)
still pass (they capture no owned/borrowed state); full suite GREEN including
the 5 #149 ownership tests.

---

## Order & regression note
Land A and B independently or together; each must keep the ENTIRE suite green.
The 5 RED tests in test/ownership/ are the regression evidence for #149's
acceptance criteria — they must all be passing (green) before #149 can be closed.
