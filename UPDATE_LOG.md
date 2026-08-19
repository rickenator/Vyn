# Vyb Implementation Update Log

Tag: `implementation-audit-2026-05-23`
Audit date: 2026-05-23

- 2026-08-17: **Agents: backpressure + bounded mailboxes (Stage 5)**. Completing
  the agents design doc (all five stages shipped). `agent_start` and its
  Bool/Float/String siblings now take an optional mailbox capacity:
  `agent_start(behavior, cap)` bounds the agent\'s mailbox (0/omitted stays
  unbounded, mirroring `chan_new` vs `chan_bounded`). A full bounded send
  returns 0 immediately (non-blocking backpressure) instead of blocking; once
  the worker drains, sends are accepted again and nothing is lost. Capacity is
  threaded through `vyb_agent_spawn` into `__vyb_chan_new`/`__vyb_strchan_new`,
  the `__vyb_agent_start*` externs gained a 4th `cap` argument, and the
  agent-start codegen handler evaluates the optional capacity (sext to i64,
  default 0) with `FunctionExpression::canFail` still choosing the failable
  ABI. New test `test/agents/test_agent_bounded.vyb` covers bounded Int agents
  (cap 3, gate-gated worker, FIFO drain), bounded String agents (cap 2 strchan
  path), and unbounded (cap 0) agents; valgrind clean (0 bytes lost). Full
  unit/module/milestone suites pass (agents: 5/5).

- 2026-08-17: **Agents: failure channeling (Stage 4)**. A behavior that `fail`s is
  now captured instead of dropped. `agent_start`/`agent_start_bool`/
  `agent_start_float`/`agent_start_string` became compiler-native so codegen can
  pick the behavior\'s calling convention from its failable flag; a behavior that
  can fail is compiled with the failable `{i1, i8*}` return ABI (lambda codegen
  gained a canFail-driven path), and the runtime captures the propagated VybError
  on the agent: it marks the agent failed, closes the mailbox (senders see 0),
  and optionally notifies a dead-letter channel. New stdlib surface:
  `agent_status` (0 running / 1 stopped / 2 failed), `agent_error_code` (the
  `fail<Int>` payload, else -1), `agent_error` (a `"Type @ file:line"`
  descriptor), and `agent_dead_letter(a, ch)` (a supervisor channel that receives
  the failed agent\'s handle). Lifecycle queries re-run clean; the captured error
  is freed by `agent_free`. Valgrind clean; full unit/module/milestone suites
  pass. Test: `test/agents/test_agent_failure.vyb`.

- 2026-08-17: **Agents: isolated message-passing units (Stages 1-3)**. A new `agents`
  stdlib module (`import agents`) for lightweight message-passing units, torn
  down through `test/agents/`. Stage 1 ships the core shape: `agent_start` runs a
  behavior loop on its own worker thread over an owned mailbox; `agent_send`,
  `agent_len`, `agent_alive`, `agent_close` (lossless drain-then-stop) and
  `agent_free` (join + reclaim) complete the lifecycle. Stage 2 adds payload
  breadth: `agent_start_bool/float/string` and `agent_send_bool/float/string`;
  Int/Bool/Float ride a shared int-slot mailbox (Bool truncates nonzero, Float
  bitcasts i64 to double) while String messages use a refcounted strchan mailbox
  that transfers the buffer to the behavior as an owned reference (valgrind
  clean, 0 bytes lost). Stage 3 adds request/response and composition: a request
  may carry its own reply-to channel (the pinger pattern routes each response
  back to the right requester), workers fan-in through chan_select over reply
  channels, and `agent_mailbox` exposes a scalar agent\'s mailbox as a live
  channel handle so it can join chan_select/chan_len alongside reply channels
  (reported -1 for String agents, which use a separate mailbox type). Runtime
  lives in `runtime/vyb_runtime.c` (`__vyb_agent_*`), wrapped by
  `stdlib/agents/mod.vyb`; design authority is `doc/AGENTS_DESIGN.md` with
  Stages 4 (failure channeling) and 5 (backpressure/bounded mailboxes) pending.
  Full unit/module/milestone suites pass.

- 2026-08-15: **Module-to-module imports + capturing-worker closures**.
  Closes the two gaps that forced the threaded `http` to reach for raw
  intrinsics. (1) The module resolver now re-exposes a *plain-imported* module's
  symbols into the importing module's scope, so a stdlib/user module can
  `import threads` like any user module — previously an import only recorded a
  `share` entry when it carried an explicit `share(...)` qualifier, so the next
  import hop silently dropped plain-imported symbols (a module couldn't even
  use its own imports). Fixed in `src/module_registry.cpp` with a `carryShare`
  that inherits the origin module's visibility (or the import's `share(...)`)
  for every spliced declaration. (2) The thread runtime now *owns* a spawned
  closure environment: `__vyb_thread_spawn` retains it and the trampoline
  releases it once the body returns. Without this, a per-argument release in
  the `threads` `thread_spawn` wrapper freed the env while a worker still
  needed it, so concurrent capturing workers all observed the last capture (a
  use-after-reuse corruption — the listen fd / connection fd captured by the
  per-connection HTTP handlers). `http` now uses
  `import threads::{thread_spawn, thread_detach}` instead of the intrinsic
  calls. Regression test `test/modules/test_closure_capture.vyb` (two threads,
  distinct captures), plus the threaded http suite, pass.
  Commits: `fix(module): carry plain-imported symbol visibility across import
  hops`, `fix(runtime): give spawned threads ownership of their closure env`,
  and `refactor(http): import threads for per-connection workers`.

- 2026-08-15: **Threaded HTTP + `thread_detach`**. Two coordinated pieces that
  finally make the `http` module concurrent. (1) `threads` gains
  `thread_detach(handle)`: marks a spawned thread fire-and-forget so its slot
  self-reaps the moment its body returns (a reaper — detached workers can't
  exhaust the 256-slot table); `thread_detach` on the reaped is -1, `thread_join`
  on a detached/already-reaped handle is -2, and the runtime registers
  `__vyb_thread_detach` with the JIT (`src/main.cpp` `runtimeSymbols`). (2)
  `http` gains `http_serve(port, backlog) -> Int`: bind+listen, then a detached
  worker-thread accept loop; each accepted connection is served concurrently on
  its own detached thread (`http_serve_conn`: read head -> path -> response),
  so the server keeps accepting while earlier connections are handled. Closing
  the listen fd stops the loop. `http` calls the `vyb_thread_*` intrinsics
  directly (as it does `__vyb_net_*`) only because a stdlib module could not
  yet `import` a sibling — since resolved in the entry above, so `http` now
  uses `import threads`. Both wire through the existing `fn() -> Int` closure
  machinery, which handles captures (e.g. the listen fd / connection fd
  captured by the worker closures). Tests: `thread_detach` semantics +
  slot-reclamation past the cap in `test/modules/test_threads.vyb`; two
  concurrent clients served in `test/modules/test_http_threaded.vyb`; full suite
  passes.
  Commit: `feat(threads/http): thread_detach reaper + threaded http_serve`.

- 2026-08-15: **`vyb bindgen` emits constant enums for C integer constants**.
  Object-like integer `#define` macros (e.g. `MAX_BUFSIZE 4096`) and C enums
  whose variants all carry explicit `= <int>` values (e.g. `MODE_EXACT = 1`)
  are now emitted as constant enums instead of the `X()<Int> { return N }`
  constant-function shape, and instead of dropping explicit enum values. The
  integer macros from one header are grouped into a single const-enum named
  after the file's basename (e.g. `preproc.h` -> `enum Preproc { MAX_BUFSIZE =
  4096, ... }`, used as `Preproc::MAX_BUFSIZE`), so their names live under a
  file-scoped namespace and can't collide with each other or with other
  top-level types; explicit-value C enums keep their own name (`enum Mode {
  MODE_EXACT = 1, ... }`, used as `Mode::MODE_FAST`). This
  matches the constant-enum language feature (the `AF_INET()<Int>` shape the
  compiler now avoids). Both backends updated: the lightweight parser
  (`src/bindgen.cpp`, `vyb bindgen <header.h>`) and the libclang full-preprocessor
  backend (`src/bindgen_libclang.cpp`, `--full`). Value-less C-like enums stay
  nominal and unchanged; partially-valued enums emit positionally with a
  warning. String/Float object-like macros remain shared constant functions
  (constant-enum members are Int-only). Fixtures/consumers updated
  (`test/bindgen/{preproc,libsample,full_preproc,test_*bindings}.vyb`,
  `libsample.h` now carries an explicit-value `Mode` enum + `MAX_ITEMS` macro);
  bindgen + enum suites pass.
  Commit: `feat(bindgen): emit C integer constants as Vyb constant enums`.

- 2026-08-15: **`io` open-mode flags converted to a constant enum**. The six
  constant-functions `FILE_READ()`/`FILE_WRITE()`/... (the bindgen-style
  `X()<Int> { return N }` shape) are replaced by a `FileFlag` constant enum in
  `stdlib/io/mod.vyb`: `FileFlag::READ` is an `Int` and combines with `|`
  (`FileFlag::WRITE | FileFlag::CREATE | FileFlag::TRUNC`). `open_read` /
  `open_write` / `open_append` read cleanly. `test/modules/test_file_io.vyb`
  passes unchanged.

- 2026-08-15: **Constant enums (`Enum::Member` as scoped Int constants)**. A
  C-like enum whose variants carry explicit `= <int>` values (`enum Socket {
  AF_INET = 2, SOCK_STREAM = 1, IPPROTO_TCP = 6 }`) now yields a namespace of
  compile-time `Int` constants: `Socket::AF_INET` is `Int` 2 and flows straight
  into `Int` parameters and arithmetic — no call, no cast. This replaces the
  `AF_INET()<Int> { return 2 }` constant-function idiom (the shape `vyb bindgen`
  emits for C `#define`/enum constants) that required a call at every use site.
  Two language changes, isolated and backward compatible: (1) the parser accepts
  `= <int>` on enum variants (stored on `EnumVariant`); (2) an all-`=`-valued,
  plain, non-generic no-payload enum is treated as a constant enum — semantic
  types its member references as `Int` (and records them in `constEnumValues`)
  instead of the nominal enum type, and codegen stores the explicit i64 value in
  `enumVariantValues` rather than auto-sequencing positional tags. Value-less
  C-like enums (e.g. `enum Direction { East, West }`) are unchanged (nominal
  typed values), and mixed/partial `=` variants or `=` + data/generics are
  rejected. `network` now ships the `Socket` const enum (`Socket::AF_INET` ...)
  and `http` ships `HttpSock` (kept local+distinctly-named because a subset
  `import network::{Socket}` doesn't splice from within a module, and defining
  a shared `Socket` in both would collide); the network/http tests read cleanly.
  Covered by `test/enum/test_const_enum.vyb`; full suite passes.

- 2026-08-15: **`threads` stdlib module (pthread-backed, MVP)**. First real
  multithreading for Vyb: `import threads` runs a `fn() -> Int` closure on a
  fresh POSIX thread. `thread_spawn(work)` unpacks the closure (a uniform
  `{ ptr env, ptr fn }`) in codegen and hands both pointers to
  `__vyb_thread_spawn` in `runtime/vyb_runtime.c`, which parks them in a fixed
  slot, starts a pthread running `vyb_thread_trampoline`, and returns a handle.
  The trampoline calls the closure as `int64_t (*)(void*)` with its hidden
  environment param and stores the result; `thread_join(handle)` blocks on
  `pthread_join`, returns the closure's result, and reclaims the slot (-2 for
  an unknown/already-joined handle). `Mutex` (`new`/`lock`/`unlock`/`free`)
  round-trips a heap pthread_mutex as an Int handle. Plumbed end-to-end on the
  established intrinsic pattern: `vyb_thread_*`/`vyb_mutex_*` names added to the
  semantic `isIntrinsic` allowlist and typed Int, a codegen block maps them to
  the `__vyb_*` symbols (spawn extracts `env`/`fn` from the closure struct;
  join/mutex pass a widened i64), and the symbols are declared + registered in
  `src/main.cpp`. MVP is `fn() -> Int` (no captures → nothing to move across the
  join boundary; capturing closures, `thread_detach` with a reaper, `CondVar`,
  and `AtomicInt` are follow-ons). Verified by `test/modules/test_threads.vyb`:
  per-thread results sum correctly, three ~60ms threads joined in reverse finish
  in well under the sequential 180ms (proving concurrent execution), unknown and
  already-joined handles return -2, and a full mutex round-trip works. Full
  suite passes.

- 2026-08-15: **Thread-safe runtime refcounts (threading foundation)**. With
  full multithreading now a primary goal (pthread-backed `threads` module is
  next), the two refcount paths outside the already-atomic `our<T>` control
  block are made atomic. In `runtime/vyb_runtime.c` the heap-String registry
  store `refs` as an atomic `int64_t` with lock-free `retain`/`release` RMWs,
  and a `pthread_mutex_t` serializes slot claiming in `__vyb_string_register`
  plus the slot reset on last release (refs(1) is published before the entry
  pointer so a concurrent retain that sees the pointer also sees the init
  refcount). In `src/vre/llvm/cgen_ownership.cpp` the legacy per-name
  `Vec-With-malloc` refcounts (`incrementRefCount`/`decrementRefCount`) now emit
  LLVM `AtomicRMW` (Add/Sub, AcquireRelease) instead of plain load/add/sub/store,
  with a zero-check on the pre-decrement value. No behavior change
  single-threaded; full suite passes. This is the correctness ground the
  pthread-backed `threads` stdlib module builds on next.

- 2026-08-15: **`http` stdlib module — a pure-Vyb HTTP/1.1 server + client**.
  Everything in `stdlib/http/mod.vyb` (`import http`) is written in Vyb directly
  on top of the `__vyb_net_*` socket intrinsics, with no new FFI or C code.
  Server building blocks: `http_listen(port, backlog)`, `http_local_port`,
  `http_accept`, `http_read_head(conn, max)` (reads through the `\r\n\r\n`
  terminator), `http_request_path(head)` (extracts the target, e.g.
  `"GET /a?x=1 HTTP/1.1" -> "/a?x=1"`), `http_send_all`, `http_close`, and a
  well-formed `http_response(status, body)`. Client side: `http_request(method,
  path, host)` head builder, `http_read_rest`, and the `http_get(host, port,
  path)` round-trip that returns the body. Also added the idiomatic
  `String::index_of(needle) -> Int` as a `StringOps` core bind (first occurrence
  or `-1`) used to parse heads. One module-system finding: a module whose
  functions call each other must be imported whole (`import http` rather than
  `import http::{subset}`), because a subset import only registers the requested
  names. Concurrency note: Vyb is single-threaded with blocking sockets, so an
  in-process server answers a kernel-queued connect (interleaved send/recv, as
  in the loopback server test), while the self-contained `http_get` must talk to
  a living peer; a real event loop / async accept loop stays on the roadmap.
  Covered by `test/modules/test_http_parse.vyb` (next to the `StringOps`
  `index_of` + request/response string helpers) and
  `test/modules/test_http_server.vyb` (loopback server end-to-end). Full
  unit/module suite passes.

- 2026-08-15: **`time` stdlib module (clocks + sleep)**. A thin `import time`
  wrapper over new `__vyb_time_*` runtime intrinsics in `runtime/vyb_runtime.c`
  (POSIX `clock_gettime`/`nanosleep`), following the `io`/`network` intrinsic
  pattern end to end: Vyb-name `vyb_time_*` calls are recognized in semantic
  (typed `Int`) and mapped to the exported `__vyb_time_*` symbols in codegen,
  with the symbols declared + registered in `src/main.cpp`. Vyb surface:
  `time_epoch_secs`/`time_epoch_millis`/`time_nanos` (Unix epoch wall-clock
  timestamps), `time_mono_millis` (monotonic ms — unaffected by NTP/system clock
  changes, the right source for measuring intervals/timeouts), and
  `sleep_ms(millis)`. All values are plain `Int`, so the module is
  allocation/pointer-free. Covered by `test/modules/test_time.vyb`; full
  unit/module suites pass.

- 2026-08-15: **By-ref `their<Vec<T>>` aspect/bind in-place dispatch**. The in-place
  collection methods (`sort_in_place`, `retain`, `map_in_place`, `reverse_in_place`,
  from the `VecOps` / `VecHigherOps` binds in `stdlib/collections`) now dispatch through
  a by-ref `their<Vec<T>>` view, not just an owned receiver. Root cause was two-fold:
  (1) semantic unwrapped an ownership-wrapped Vec receiver to dispatch built-in Vec
  primitives only, so an aspect/bind method on `vr<their<Vec<Int>>>` fell through; the
  identifier-receiver path now tries `resolveAspectMethodForTypeString` on the unwrapped
  inner `Vec<Int>` type, and the member-expression path dispatches trait impls against a
  normalized unwrapped type string. (2) codegen's aspect dispatch used the wrapper
  (`their<Vec<Int>>`) as the monomorphize/mangle key and passed the receiver's alloca
  address; it now normalizes to the inner Vec type and passes the stored `Vec*` pointer
  (loaded from the wrapper alloca) to the by-ref `self<their<Vec<T>>>` parameter, so
  mutations reach the caller's backing Vec. Works through a function/borrow parameter and
  a local `view()`/`borrow()` reference, for `Int` and `String` elements. Covered by
  `test/modules/test_vec_inplace_byref.vyb`; full unit/module/ownership/aspect suites pass.

- 2026-08-15: **`network` stdlib module (TCP/IP socket MVP)**. Ships the first
  networking for Vyb as a thin stdlib wrapper (`stdlib/network/mod.vyb`,
  `import network`) over new `__vyb_net_*` runtime intrinsics in
  `runtime/vyb_runtime.c` (BSD sockets via `socket`/`bind`/`listen`/`accept`/
  `connect`/`send`/`recv`/`getsockname`/`close`, with `inet_pton`/`htons`/
  `ntohs` handled inside the runtime so the Vyb surface is string/pointer-free).
  Vyb surface: `socket_open`/`socket_close`/`socket_bind`/`socket_listen`/
  `socket_accept`/`socket_connect`/`socket_send`/`socket_recv`/`socket_local_port`/
  `socket_error_code`/`socket_error_message` plus the `AF_INET`/`SOCK_STREAM`/
  `IPPROTO_TCP` constants. Follows the `io` module's intrinsic pattern end to end:
  Vyb-name `vyb_net_*` calls are recognized in semantic (typed Int/String) and
  mapped to the exported `__vyb_net_*` symbols in codegen (String args/payloads
  extract their `{ ptr, len }` data pointer; `recv` and the error message return a
  registry-registered owned buffer so the Vyb String is reclaimed by normal
  reference counting), with the symbols declared + registered in `src/main.cpp`.
  Verified with a self-contained loopback echo (bind-to-ephemeral-port, local
  port query, connect, accept, send/recv both directions) in
  `test/modules/test_network_socket.vyb`. Full unit/module suites pass.

- 2026-08-15: **Member-receiver aspect/bind dispatch + owned closure captures**.
  (a) **Member-expression receiver dispatch**. Codegen's aspect/bind dispatch previously
  required an identifier receiver; invoking a bound method on a non-identifier receiver —
  a struct field (`h.c.bump()`), an owned `self.items<Vec<Int>>` field, or a nested
  `their<Vec<T>>` view field (`self.data.sort_in_place()`) — fell through to a stale
  "Function ... not found" diagnostic. Codegen now evaluates the member in LHS (pointer)
  mode to get the field's address, resolves the trait/monomorphized method against the
  concrete type (unwrapping ownership-wrapped Vec fields to their inner type and loading
  the `Vec*` slot), and passes the address to the bind's by-ref `self<their<Self>>`
  receiver, so in-place mutations persist on the caller. Semantic was also fixed to let a
  plain `Vec`-typed field's non-built-in methods reach the general trait dispatch instead
  of short-circuiting to the built-in-only handler (`test/aspect/test_aspect_member_receiver.vyb`).
  (b) **Ownership-qualified closure captures**. The closure prologue now records each
  captured variable's AST type on its reloaded `capAlloca`, so member access on captured
  ownership-wrapped values resolves — `our<T>` (read + write-through), `my<T>` (move), and
  `their<T>`/`view<T>` (borrow) — instead of erroring "Cannot determine struct type for
  member access" (`test/lambda/test_closure_owned_field_capture.vyb`). Both covered by
  new regression tests; full unit/module/ownership/aspect/lambda suites pass.

- 2026-08-15: **`BTreeMap<K,V>` ordered map**. Added to `stdlib/collections/mod.vyb` a
  `Comparable`-keyed ordered map bound as `BTreeMapOps`: keys live in a `keys` vector kept
  sorted ascending with a parallel `vals` vector, so `get` / `contains_key` binary search
  (O(log n)) and `put` inserts at the sorted position (O(n) shift), with duplicate keys
  updating in place. Ordering dispatches through the existing `Comparable`-bounded `cmp_lt`
  helper, so any `Comparable` key works (Int/Float/Bool/String). `iter()` yields `BTreeIter<K,V>`
  bound to `core::iter::Iterator` with `Item = MapEntry<K,V>` (reusing the pair iterator item
  pattern), so `for (kv in bt.iter())` walks entries in ascending key order and a stored
  `BTreeIter` is itself iterable. Build with `BTreeMap<K,V>()`. NOTE: the empty-`for`-iteration
  seen while prototyping was an unrelated pre-existing `String` variable `+ Int` concatenation
  bug (emit emits a `Storing ptr into location of type { ptr, i64 }` warning and yields an empty
  string), not a BTreeMap issue. Covered by `test/modules/test_collections_btreemap.vyb`;
  leak-free under ASAN; full suite 867/863 pass (same 4 pre-existing trap/vec-edge failures).

- 2026-08-15: **Identifier iterables route onto the `Iterator` protocol**. `for (x in vec)`
  now desugars exactly like `for (x in vec.iter())`: the parser's identifier branch drops the
  old index-based `__idx`/`__len` over `vec.get(i)` desugar in `StatementParser::parse_for`
  and instead wraps the identifier in `<ident>.iter()` and hands it to
  `buildForLoopIteratorDesugar`, so every loop drives `core::iter::Iterator` over
  `.iter() -> .next()`. Because this is parse-time and type-blind, the uniform rule is that an
  iterable value must expose `iter()`: Vec collections get it from `import collections`
  (`VecHigherOps`), and the stdlib iterators (`VecIter`, `MapIter<K,V>` via `MapEntry`,
  `HashIter`) gained a self-`iter()` that returns a fresh iterator over the same underlying
  collection, so a stored iterator identifier (`for (y in storedIter)`) also iterates. Effect
  on tests: the `vec_for`/`new_features` Vec loops that relied on the builtin (no-import) index
  path now `import collections`, since `.iter()`/`VecIter` live there. Covered by
  `test/modules/test_for_identifier.vyb` (identifier Vec, step, stored VecIter/MapIter,
  break/continue). Leak-free under ASAN; full suite 866/862 pass (same 4 pre-existing
  trap/vec-edge failures).

- 2026-08-15: **Two-parameter iterator `Item` monomorphization + `MapIter` key/value
  pairs**. Fixed `TypePattern::parse` in `cgen_trait_mono.cpp`, which split generic
  arguments on every comma regardless of nesting depth: `Option<MapEntry<String, Int>>`
  was parsed as two bogus arguments (`MapEntry<String` and `Int>`), mangling the
  monomorphized return type to the malformed `Option_MapEntry_Int>`, which then failed
  LLVM module verification (payload also shrunk to `[8 x i8]`). The splitter is now
  depth-aware (commas inside `<...>` are kept), so the type parses as a single
  `MapEntry<String, Int>` and mangles to the correct `Option_MapEntry_String_Int`
  (`{ i64, [24 x i8] }`). `MapIter<K,V>` now ships `type Item = MapEntry<K,V>` and
  `next` yields the key/value pair (`Some(e)`), so `for (kv in m.iter())` reads
  `kv.key` / `kv.value` directly (`test/modules/test_collections_iter.vyb`). Verified
  with `/tmp/test_mapentry_repro.vyb` (`total=3`), leak-free under ASAN; full suite
  865/861 pass (same 4 pre-existing trap/vec-edge failures).

- 2026-08-15: **Nested `their<T>` view-field member access + `HashMap`/`HashSet`
  iterators**. Root cause of the earlier "cannot read a collection through a
  `their<T>` view field" gap: cgen_expr's property-member-access path only knew a
  struct's layout when the object was an alloca or had a `valueTypeMap` entry; an
  intermediate read (e.g. `self.set` from a `their<HashSet<K>>` field) was a bare
  load with no AST type, so a further access (`self.set.values`) failed. Fix: on
  every member-read, register the loaded value's AST type (`node->type`) in
  `valueTypeMap`. That generalizes the nested-`their<Vec<T>>` support to any
  generic struct (`their<HashSet<K>>` / `their<HashMap<K,V>>`), so `HashMap` and
  `HashSet` iterator binds now compile: `MapIter<K,V>` (`m.iter()`, yields keys)
  and `HashIter<K>` (`s.iter()`, yields values), both by-ref (`their<...>` view),
  bound to `core::iter` and usable in `for (k in m.iter())` /
  `for (v in s.iter())` (`test/modules/test_collections_iter.vyb`). Leak-free
  under ASAN. Full suite 861/865 (only the 4 pre-existing trap/vec-edge failures).
  Known limit: a two-generic-param struct as the `Iterator::Item` (e.g. a
  `MapEntry<K,V>` pair) isn't monomorphized yet (mangles/hangs), so map iteration
  yields keys; value access is `m.get(k).value`.

- 2026-08-15: **`for`-loop `skip`/step over `Iterator`**. The iterator desugar
  increased: `for (item in <iter-expr>, step)` now advances the iterator `step`
  elements per iteration, yielding indices 0, step, 2*step, ... — the same
  semantics as the Vec index-based path. The parser logic moved into
  `StatementParser::buildForLoopIteratorDesugar`; with a step, the Some arm
  prepends `{ var __s=1; while (__s<__step) { match (next()){Some(__v)->{__s+=1}
  None->{__s=__step}} } }` before the user body, so the loop variable stays the
  seed of each group and `break`/`continue` target the outer while (verified).
  Full suite 860/864 (only the 4 pre-existing trap/vec-edge failures). This
  clears the last fully-shippable for-iterator follow-up; the remaining two
  (Vec identifier path onto the protocol; `HashMap`/`HashSet` iterator binds)
  are noted in TODO.md, with the latter currently blocked by a `their<T>` view
  generic-struct member-access compiler gap (reading fields / calling bound
  methods through `self.set` / `self.map` doesn't resolve outside of the
  Vec-specific nested-`their<Vec<T>>` case).

- 2026-08-15: **`for (item in col)` desugar over `Iterator`**. A `for` loop whose
  iterable is a **non-identifier expression** (e.g. `ints.iter()`) now lowers to the
  `core::iter::Iterator` protocol: the parser emits `{ var __it_<item> = <expr>;
  while (true) { match (__it_<item>.next()) { Some(item) -> { body } None -> { break }
  } } }`. The transform is parse-time and type-blind, so it keys off the expression
  shape rather than the type: plain identifiers keep the existing index-based `Vec<T>`
  path and `0..n` ranges keep the inclusive-range path (no regressions — full suite
  859/863, with only the 4 pre-existing trap/vec-edge failures). `break`/`continue`
  naturally re-enter `next()`, and re-evaluating the producer each loop starts a fresh
  iterator. The optional `skip` parameter is rejected on the iterator path (no clean
  mapping) until Vec/`HashMap`/`HashSet` binds are woken up behind it. Covered by
  `test/modules/test_for_iter.vyb`.

- 2026-08-15: **Generic-bind `Option<T>` construction & matching + `VecIter<T>`**. A
  generic bind method returning `Option<T>` (or `Option<Self::Item>`) now works for
  the type-parameter payload. Two root causes fixed: (1) semantic — a `match`/`select`
  scrutinee that is a concrete enum returned from a generic bind (e.g. `Option<Int>`)
  wasn't in `enumVariantPayloadTypes` (only the bind's loose `Option<T>` was), so
  `Some(x)`/`None` patterns fell through with "Unknown type identifier: Some";
  added `materializeConcreteEnum` to lazily register the concrete enum's payload types
  for both `match` and `select`. (2) codegen — bare `Some(x)`/`None` inside a
  monomorphized generic bind emitted `Option_T` (not `Option_Int`) because the active
  generic-bind type params weren't substituted (the qualified `Option<T>::Some` path
  already did this); applied the same `currentTypeSubstitutions` substitution to the
  bare constructors. Both generic and concrete. This unblocks the generic collection
  iterator: `import collections` now ships `VecIter<T>` (cursor over `Vec<T>` by
  reference, bound to `Iterator`) plus the `iter(self<their<Vec<T>>>)` producer, with
  `Iterator` re-exported so a whole-module import surfaces the bind. Consumed via
  `.next()` / `match` for `Int`/`String` in `test/modules/test_vec_iter.vyb`. Full
  suite 858/862 (the 4 remaining failures are the pre-existing trap/vec edge tests).
- 2026-08-15: **Nested `their<Vec<T>>` field method resolution**. A struct field
  typed `their<Vec<T>>` (a by-ref view of a Vec — e.g. an iterator holding the Vec by
  reference in `data<their<Vec<T>>>`) now resolves the built-in Vec methods. Root
  cause was two-fold: the semantic pass only unwrapped ownership-wrapped Vec receivers
  when the receiver was a plain identifier (`self.len()`), so a member expression
  (`self.data.len()`) fell through to "Method not found for type 'their<Vec<Int>>'"
  (fixed by unwrapping `their`/`my`/`our`/`view`/`borrow`-wrapped member receivers to a
  `VecType` and dispatching `handleVecMethodCallOnMember`); and codegen treated the
  LHS-mode member address `&field` (a `Vec**`, since a `their<Vec<T>>` field is a
  single-pointer slot holding the Vec address) as a `Vec*`, reading garbage for len and
  the size field for get (fixed by loading the pointer slot once before operating).
  Reads (`len`/`get`) and mutations (`set`/`push`) both reach the borrowed backing Vec,
  verified concrete and generic. New regression
  `test/modules/test_nested_their_vec_field.vyb` also drives a concrete `Iterator`
  bind (`IntVecIter { data<their<Vec<Int>>>, index }`) whose `next` reads through the
  borrowed field. Full suite 857/861 (the 4 remaining failures are the pre-existing
  trap/vec edge tests). Note: a **generic** collection iterator (`VecIter<T>` with
  `next` returning `Option<T>`) is still blocked on a separate, pre-existing gap —
  constructing/`match`-ing `Option<T>` from a type-parameter payload (`Option<T>::Some`
  / bare `Some`) inside a generic bind body does not type-resolve at the call site,
  while the concrete case (`MapOps.get`, `Option<V>::Some(self.vals.get(i))`) works.
- 2026-08-15: **`Iterator` protocol (`core::iter`)**. Shipped the standard iteration
  contract — `aspect Iterator { type Item; next(self<their<Self>>)<Option<Self::Item>> }`
  — as an explicitly imported `import core::iter` module (kept out of the auto-imported
  `core::aspects` because that contract set is auto-imported into every non-stdlib module,
  and ~6 associated-type tests already define their own local `Iterator` aspect, which
  would clash). The protocol is proven end-to-end: a type binds it (assigning `type Item`),
  `next` advances cursor state through a by-ref `their<Self>` receiver, and consumers loop
  `match (it.next()) { Some(v) -> ... None -> break }`. Verified for `Int` and `Float`
  associated types, leak-free under ASAN (`test/modules/test_iterator_protocol.vyb`).
  Two follow-ups are now explicit in `TODO.md` rather than assumed: (1) the `for (item in
  col)` desugar over this protocol needs a type-aware transform — the parser desugar is
  type-blind and currently assumes Vec/index-gets for non-range identifiers, so this is a
  semantic/codegen-level change, plus `break`/`continue`/payload-extraction handling; (2)
  binding `Vec`/`HashMap`/`HashSet` as iterators is blocked because calling `len`/`get` on
  a nested `their<Vec<T>>` struct field does not yet resolve (concrete or generic). Full
  suite 856/860 (the 4 remaining failures are the pre-existing trap/vec edge tests).
- 2026-08-15: **Stdlib File I/O (`import io`)**. Replaced the `io` placeholder with a
  real file module: `File { fd<Int>, path<String> }`, `open(path, flags)` plus
  `open_read`/`open_write`/`open_append` conveniences, `close`, `write_str`,
  `read_all` (whole file into a `String`, empty on error), and the
  `error_code()`/`error_message()` diagnostic surface with portable `FILE_*`
  open-mode helpers. Wired in the runtime (`__vyb_file_*`, all `VYB_WEAK`, last
  error in `vyb_file_err`), registered as codegen intrinsics, and exposed to Vyb
  under `vyb_io_*` names (Vyb identifiers cannot start with `_`). `read_all`
  registers its buffer so the returned `String`'s release frees it. Note that
  Vyb has no bitwise `|` operator, so `open_write`/`open_append` combine the
  non-overlapping `FILE_*` mode bits by addition. Added
  `test/modules/test_file_io.vyb` (write/reopen/read round-trip plus the
  missing-path error surface). Full suite 855/859 (remaining 4 failures are the
  pre-existing trap/vec edge tests); the new test is leak-free under ASAN.
- 2026-08-14: **Memory-leak hardening pass** over the runtime/ownership/closure paths.
  `__vyb_string_from_string` now registers its `strdup` copy so `String::from_string`
  is reclaimed like the other producers; `my<Struct>` struct fields are now reclaimed on
  scope exit and on overwrite (with a visited-type guard so self-referential linked
  structs such as a `TreeNode { left<my<TreeNode>> }` terminate instead of recursing at
  codegen time); and fresh closure-literal arguments passed to aspect/trait methods
  (`map`/`filter`/`reduce`) are retained before the call and released after so their
  transient capture environments no longer leak. Verified leak-free under ASAN on
  `conversion_test`, `test_simple_tree`, `test_closure_capture`, and the nested-`my`
  case; full suite steady at 854/858 (remaining failures pre-existing trap/vec tests).
  **Tooling note — valgrind:** ASAN/LSan reports leaks only for unreachable blocks, so
  a reachable-but-orphaned closure env or `my<T>` binding can go underreported. Once
  valgrind is installed, run `valgrind --leak-check=full --error-exitcode=1 <build>/vyb
  <file>` from the repo root (VYB_STDLIB=stdlib) as a complementary whole-process leak
  check over JIT-compiled code; add it to the memory-hygiene test loop alongside ASAN.
  Not installed on this host (`valgrind` absent), so not adopted in CI yet.
- 2026-08-13: **Aspect inheritance (super-aspects)**. `aspect Comparable : Equatable` now
  parses and registers super-aspects; super-aspect names are validated against defined
  aspects, cyclic super-aspect dependencies are rejected, and binding a sub-aspect requires
  the same type to also bind each super-aspect (enforced in an order-independent post-pass).
  Added four aspect-inheritance regressions. Full suite 752/752.
- 2026-08-13: **Generic function call typing + aspect-bound validation**.
  Generic function calls now infer type arguments from the call site, substitute them into
  the return type (callers see `Point` instead of the placeholder `T`), and reject concrete
  instantiations whose type does not bind the declared aspect(s). Also fixed a generic-function
  monomorphization scope imbalance where calling a second distinct generic function in one body
  popped the caller's scope (`ERROR: No active scope to register variable`). Added
  `test_generic_fn_return_subst` / `test_generic_fn_bound_rejected` / `test_generic_fn_multiple`
  regressions and re-affirmed bind selection precedence. Full suite 748/748.
- 2026-08-13: **Polymorphic monomorphization hardening & ownership unwrap-on-read**.
  Added unwrap-on-read for primitive `my<T>`/`our<T>`/`mild<T>` (reads with the underlying
  value's type) plus compile-time move tracking for `my<T>` that rejects use-after-move and
  records transfer on assignment/init/argument passing. Allowed binding aspects to concrete
  generic instantiations (`bind Display -> Box<Int>`). Fixed generic-function monomorphization so
  the caller IR insertion point is restored afterward and call-frame push/pop stays balanced
  (eliminated `printItem_Point` "no terminator" crashes), scoped monomorphized trap-handler bodies
  so `return` doesn't pop the enclosing function scope, and released the debug `DIBuilder` during
  module/context release to stop a timing-sensitive `SIGSEGV` under concurrent runs. Corrected
  `test/ownership/move_function_consume.vyb`, updated the README aspect example to use real bound
  struct values, and aligned `README.md`/`FEATURE_STATUS.md`/`MONOMORPHIZATION_DESIGN.md`. Full
  harness suite passes (742/742).
- 2026-08-06: **Sealed monomorphization design** (I-010). Created `doc/MONOMORPHIZATION_DESIGN.md` documenting the permanent decision: compile-time monomorphization for all generics, aspects + bind for polymorphism, no vtables/dynamic dispatch/trait objects. Updated TODO.md with sealed status. Fixed multi-argument type inference in generic function calls — each argument now maps to its corresponding type parameter by position instead of only using the first argument. Fixed trap/ensure result passing via alloca-based merging (eliminated PHI node predecessor mismatch). Removed DEBUG_CHECK and DEBUG_CRASH fprintf(stderr) debug prints. All 213 tests pass.

This log consolidates what still needs to be implemented after scanning the
repository docs, future-feature tests, and relevant compiler/runtime sources.
It is intentionally source-biased: when docs conflict, source code and
expect-fail tests are treated as stronger evidence than optimistic status text.

## Implementation Progress

- 2026-05-25: Closed the aspect-suite residual risk follow-up. Generic bind
  methods now monomorphize into executable LLVM functions with the correct
  active function/impl context, built-in `Vec<T>` receiver layout, receiver
  plus non-receiver parameter indexing, and balanced call-frame handling.
  Dot-call aspect dispatch now rejects ambiguous method names with a
  deterministic diagnostic, and explicit generic object literals validate
  nested substituted field types. Added focused `test/aspect` regressions.
- 2026-05-25: Added canonical aspect/bind receiver shorthand. Simple receiver signatures may now use `method(self)<T>`, which the parser canonicalizes to the bound `Self` receiver internally, while existing `self<Self>` and ownership-qualified receiver forms remain valid. Updated the structs/aspects demo, docs, and focused aspect regression tests.
- 2026-05-25: Advanced I-002 FFI with the next ABI slice: `#[repr(C)]`
  now parses on structs, is tracked in AST/codegen metadata, preserves
  declaration-order unpacked LLVM struct layout, and rejects generic,
  ownership-qualified, and Vyb-runtime fields that are not C ABI-stable.
  Native `--build` now accepts repeatable `--link <lib-or-path>` arguments and
  links the metadata runtime object needed by native builds. Added focused
  `test/ffi` coverage for repr(C) layout, diagnostics, and by-pointer extern C
  calls.
- 2026-05-25: Implemented the first associated-types slice for aspects/binds
  (I-007). Aspects now parse/store associated type declarations (`type Item`),
  binds accept explicit assignments (`type Item = Int`), and semantic analysis
  validates missing/unknown/duplicate associated type assignments with focused
  diagnostics that name the aspect/type bind. Added positive/negative
  `test/aspect` coverage, including `Self::Item`/`Iterator::Item` resolution
  in bind method type contexts.
- 2026-05-25: Advanced I-008 stdlib foundations by adding a canonical module
  scaffold under `stdlib/core`, `stdlib/collections`, and `stdlib/io`, wiring
  top-level `stdlib/prelude.vyb` to re-export `core::prelude`, and introducing
  a documented transitional `core::option` bridge (`OptionInt`) while generic
  `Option<T>`/`Result<T,E>` remain future work. Added stdlib module discovery
  coverage for `VYB_STDLIB`, explicit prelude behavior tests (no auto-import),
  and a runnable `examples/stdlib_demo` sample plus docs/status updates in
  `doc/stdlib_layout.md`, `doc/module_visibility.md`, `doc/FEATURE_STATUS.md`,
  and `TODO.md`.
- 2026-05-23: Implemented first-pass `extern "C" { ... }` block support for
  function signatures. Blocks now parse as declaration groups, semantic analysis
  visits their members, and the LLVM path emits external function declarations.
  Added `test/ffi/extern_c_malloc_free.vyb` as a JIT smoke test for calling C
  `malloc` and `free`.
- 2026-05-23: Extended the JIT FFI path to resolve host process symbols and
  added a narrow `String`-to-C-string call conversion. Added
  `test/ffi/extern_c_puts.vyb` to cover calling libc `puts`.
- 2026-05-23: Restricted direct calls to `extern "C"` functions to
  `freedom { }` blocks and added a negative FFI test for calls outside freedom.
- 2026-05-23: Completed a focused tests/examples/demos repair pass. Semantic
  analysis now preserves resolved function parameter, return, and struct field
  types, and ordinary function calls infer declared return types. Reworked the
  example suite so every `examples/*.vyb` file is runnable with the current
  compiler, added `demos/` for curated language demonstrations, refreshed test
  organization docs, and documented remaining ownership/runtime gaps in
  `reports/TEST_EXAMPLE_DEMO_REVIEW.md` and
  `reports/LANGUAGE_COMPLETION_REVIEW.md`.
- 2026-05-23: Began module finalization by resolving local `.vyb` imports in
  the driver before semantic analysis and codegen. `import nested::module`
  resolves to `nested/module.vyb`, `import name from "./file.vyb"` resolves
  relative file locators, imported declarations are spliced before use, and
  duplicate/circular imports are guarded. Added module runtime tests and a
  runnable example.
- 2026-05-23: Finalized the next language-contract slice: source-level
  `bundle(...)`/`share(...)` visibility, selective import aliases,
  `share(...) import ...` re-exports, lexical borrow/view conflict checks,
  C ABI aliases for extern blocks, and typed `fail<T>(value)`. Added targeted
  tests plus runnable example/demo coverage.
- 2026-05-24: Started the next quality gate by adding a milestone test runner
  that now executes and passes 157 tests while enforcing a minimum floor of 122.
  Tightened the harness to check `@expect-return` during JIT execution, repaired
  accidental basic/type/Vec tests, taught semantic analysis to type literal
  operands for `typeof`/`typename`, and added a semantic rejection for direct
  recursive struct value fields.
- 2026-05-24: Fast-forwarded to the latest GitHub ownership-syntax merge and
  repaired the affected local fixtures. String literals now lower to constant
  `String` structs even in global scope, the weak-reference smoke test was
  simplified to supported syntax, and the canonical syntax fixture is treated
  as parse-only to keep it focused on syntax rather than LLVM lowering.
- 2026-05-24: Continued I-001 by moving source-level import resolution into a
  dedicated `ModuleRegistry` metadata model (module state, canonical keys,
  dependency tracking, topo order), adding `--module-path` plus
  `VYB_MODULE_PATH` and stdlib auto-discovery (`VYB_STDLIB` and
  executable-relative probes), and upgrading diagnostics for missing modules,
  parse failures inside imports, circular imports, and duplicate splice
  symbols. Added focused `test/modules` coverage and a runnable
  `--module-path` example.
- 2026-05-24: Completed I-005 Error Propagation phases 3–5. `fail` now builds
  runtime `VybError` payloads and propagates through failable returns when no
  trap is active, call sites of semantically failable functions now auto-check
  `{value, error}` and propagate errors using the same return helper, and
  failable calls from non-failable functions without trap are now rejected by
  semantic analysis with a targeted diagnostic. The runtime
  `__vyb_runtime_untrapped_error` path now prints type, payload JSON, and fail
  source location, supports `exitCode<Int>` payload override, and the JIT entry
  path now dispatches propagated failable-`main` errors to that handler.
  Added tests: `test/trap/propagation_no_trap.vyb`,
  `test/trap/propagation_to_main.vyb`, `test/trap/defer_runs_on_fail.vyb`,
  `test/trap/non_failable_caller_rejected.vyb`.
  Follow-up: generalized JIT ABI handling for failable `main` payloads beyond
  current `{Int, i8*}` / `{i1, i8*}` specializations.
- 2026-05-24: Closed the error-propagation coverage gap by promoting the
  current Phase 2 and Phase 3-5 trap propagation fixtures into the milestone
  gate. The gate now covers propagated failable calls, failable `main`
  untrapped dispatch, defer-on-fail cleanup, and non-failable caller rejection
  in addition to the existing module/FFI/core suites. Raised the milestone
  floor to 134 and reconciled stale roadmap/status entries for ModuleRegistry,
  module path resolution, string method coverage, and completed error
  propagation phases.
- 2026-05-24: Closed a concrete Vec correctness gap: `Vec::pop()` now returns
  the removed element for the supported primitive path instead of a hardcoded
  placeholder value and no longer dereferences null storage when called on an
  empty `Vec<Int>`. Added `test/new_features/test_vec_pop_returns_value.vyb`
  and `test/new_features/test_vec_pop_empty.vyb`, then raised the milestone
  floor to 136.
- 2026-05-24: Implemented the first real `our<T>` / `mild<T>` control-block
  runtime slice. `our(expr)` now backs shared owners with a payload pointer and
  strong/weak/released metadata, `soft(ourValue)` increments weak_count,
  `mild<T>.released()` observes the released flag after the final local strong
  owner leaves scope, and `mild<T>.grab()` upgrades live weak handles by
  incrementing strong_count. Returning local `our<T>`/`mild<T>` now transfers
  that handle instead of cleaning it up before return, and `our<T>` member
  access unwraps through the control block payload pointer. Added focused
  ownership regressions for live/released `released()` and `grab()` behavior,
  promoted `test/ownership` into the milestone gate, and raised the milestone
  floor to 156. Current limitation: failed `grab()` returns a null `our<T>`
  placeholder until Vyb has first-class `Option<T>`/nullable result syntax.

## Audit Scope

Docs/status sources reviewed:

- `README.md`, `TODO.md`, `CHANGELOG.md`
- `doc/FEATURE_STATUS.md`, `doc/archive/ROADMAP.md`, `doc/archive/TODO_CURRENT.md`
- Module/FFI docs: `doc/MODULE_FFI_BINARY_ROADMAP.md`, `doc/FFI_DESIGN.md`,
  `doc/bundles_and_sharing.md`, `doc/module_visibility.md`
- Error docs: `doc/ERROR_TRAP.md`, `doc/archive/ERROR_PROPAGATION_DESIGN.md`,
  `doc/archive/ENSURE_IMPLEMENTATION_STATUS.md`, `test/trap/README.md`,
  `test/trap/TEST_RESULTS.md`
- Ownership/memory docs: `doc/OWNERSHIP_MILD.md`, `doc/Memory_Operations.md`,
  `doc/archive/mem_RFC.md`, `test/memory/README.md`, `examples/README.md`
- Aspect/generic docs: `doc/ASPECT_BOUNDS.md`,
  `doc/TRAIT_SYSTEM_DESIGN.md`, `doc/archive/SELF_RESOLUTION_COMPLETE.md`,
  `test/aspect/PHASE_6_ROADMAP.md`
- Lambda, async, string, tuple, Vec, introspection, AST, and test docs under
  `doc/` and `test/`

Source areas checked:

- Parser: `src/parser/*`, `include/vyb/parser/*`
- Semantic analysis: `src/vre/semantic.cpp`, `include/vyb/semantic.hpp`
- LLVM codegen: `src/vre/llvm/*`, `include/vyb/vre/llvm/codegen.hpp`
- Runtime: `src/runtime/*`, `include/vyb/runtime/*`, `runtime/*`
- Future tests: `test/future_features/*.vyb`

## Highest Priority Implementation Backlog

> **2026-08-17 reconciliation.** This table is the original
> implementation-audit-2026-05-23 backlog. Since then, most P0/P1 items have
> shipped (see `git log`, `CHANGELOG.md`, and `docs/refman/`); rows marked
> **SHIPPED** below are done and their "Evidence" should be read as historical.
> Open work is tracked forward in `TODO.md` (agents, FFI variadics/bindgen,
> `vyb.toml`/build/tooling, and the 4 pre-existing trap/vec test failures).
> Rows **not** marked SHIPPED (I-005/6, I-014/15/17/18/19) remain open —
> either genuinely incomplete or only partially resolved.

| ID | Area | Priority | What needs to be implemented | Evidence |
|----|------|----------|------------------------------|----------|
| I-001 | Module system | P0 | **SHIPPED** — `ModuleRegistry` metadata model, `bundle(...)`/`share(...)` visibility, selective aliases + re-exports, `VYB_MODULE_PATH` + CLI `--module-path` + stdlib discovery, and namespace-scoped per-module resolution (cross-module direct-call leaks closed). | Not yet implemented at audit time; see `src/module_registry.cpp`, `test/modules/test_{import,nested,namespace}*.vyb` (all pass). |
| I-002 | FFI | P0 | **SHIPPED (partial)** — extern C blocks, ABI scalar/pointer aliases, `#[repr(C)]`, native `--link`, and OpenSSL binding shipped; variadic calls, `String::as_c_str()`, broader C ABI validation, and bindgen/libclang remain open. | See `src/bindgen.cpp`, `test/ffi/`, and `stdlib/{tls,https}` (linked OpenSSL). |
| I-003 | Ownership runtime | P0 | **SHIPPED** — borrow checking, `my<T>` moves + temporary-owner semantics, `our<T>` refcounted copy/assignment/params, `their<T>`/by-ref receivers (incl. nested field + member-expression), and struct-owned cleanup. | `test/ownership/` (46/46 pass), `test/lambda/test_closure_{move,our}_capture.vyb`. |
| I-004 | `mild<T>` weak references | P0 | **SHIPPED** — `soft()`/`grab()`/`released()`, failed `grab()` returns native `our<T>?`, and weak handle copy/drop/cleanup accounting. | See `test/ownership/mild_*.vyb` (all pass). |
| I-005 | Error propagation/runtime errors | P0 | Finish cross-function error propagation, construct real `VybError` objects at `fail`, preserve type/data/source location, print detailed untrapped errors, and settle Result-vs-fail/trap design conflict. | `doc/archive/ERROR_PROPAGATION_DESIGN.md`; `test/trap/TEST_RESULTS.md`; `src/runtime/error_handling.cpp` says error structure is not implemented. |
| I-006 | Defer/runtime cleanup | P0 | Decide whether runtime defer/ensure stacks are needed; implement runtime defer stack if `defer` must survive fail/unwind paths. | `src/runtime/error_handling.cpp` has defer/ensure stubs; `src/vre/llvm/cgen_stmt.cpp` stores defers in a codegen stack. |
| I-007 | Aspect completion | P0 | **SHIPPED** (static dispatch) — associated types, aspect inheritance with super-aspect validation, qualified `Aspect::method` disambiguation, unqualified dispatch on bounded type params, bound-bind precedence; AST teardown leaks fixed. | See `test/aspect/` (82/82 pass); runtime `dyn` dispatch is a deliberate non-goal (`TODO.md` "Aspect objects/dyn dispatch"). |
| I-008 | Stdlib foundation | P0 | **SHIPPED** — native `T?` replaced `Option`; `Result<T,E>`; core aspects; `Iterator` + `for` desugaring; File I/O; maps/sets (`HashMap`/`HashSet`/`BTreeMap`); String/Vec helpers; collections iterators. | See `stdlib/{core,io,collections}`, `docs/refman/collections.md`, `test/modules/`. |
| I-009 | Async runtime semantics | P1 | **SHIPPED** — real cooperative executor, future storage, suspension/resumption, task spawning, async lambdas, closures-as-async-params, `async for` over channels, multi-threaded workers, async socket I/O. | Not yet implemented at audit time; now `stdlib/asyncs` + `src/runtime/async_runtime.cpp`, covered by `test/async/`. |
| I-010 | Lambda/closures | P1 | **SHIPPED** — return-type inference, closure env structs, indirect calls, mutable/move/`our` capture, owned/member-receiver capture, returned-closure env release. | Not yet implemented at audit time; see `test/lambda/` and `test/*/test_closure*`. |
| I-011 | Pattern matching/select polish | P1 | **SHIPPED** — struct destructuring, enum-variant + range + guard patterns, exhaustiveness, `match`-as-expression. | Not yet implemented at audit time; see `test/select_match/` and `test/enum/`. |
| I-012 | Enums/sum types | P1 | **SHIPPED** — tagged data enums with variants, pattern matching, exhaustiveness, and native `T?` replacing `Option`; `Result<T,E>` shipped. | Not yet implemented at audit time; see `test/enum/` and `docs/refman/types.md`. |
| I-013 | Vec correctness/polish | P1 | **SHIPPED (partial)** — bounds-checked `get`, `contains`, `pop` returns, `find`/`first`/`last`/`reversed`/`sorted`/`min`/`max`, higher-order + in-place combinators, `VecIter`. | See `test/modules/test_vec_*.vyb`; element-type edge cases remain (see the 4 pre-existing failures). |
| I-014 | Tuple completion | P1 | **IN PROGRESS** — tuple element access/serialization remain partial. | See `test/tuples/` and `docs/refman/types.md`. |
| I-015 | Generic/template monomorphization | P1 | Finish template instantiation, AST clone/substitution, constructor inference, nested generics, member template instantiation, and bounds-checked instantiation. | `src/vre/semantic.cpp` has monomorphization stubs; `test/template/generics_examples.vyb`; `doc/archive/SELF_RESOLUTION_COMPLETE.md` lists constructor/Vec issues. |
| I-016 | Introspection completion | P1 | **SHIPPED (partial)** — `typeof`/`typename` and `as` downcasting present; first-class `Type` registry still open. | See `test/introspection/` and the wildcard-trap handling in `src/vre/llvm/cgen_expr.cpp`. |
| I-017 | Auto-serialization/metadata edges | P1 | Re-enable/fix main auto-serialization where disabled, handle nested structs and Vec in metadata serialization/deserialization, and dynamic buffer sizing. | `src/main.cpp`; `src/vre/llvm/cgen_decl.cpp`; `runtime/vyb_type_metadata.c`. |
| I-018 | Build optimization pipeline | P2 | Complete LLVM pass pipeline for all optimization levels, add LTO/ThinLTO, bitcode flows, benchmarks, and linker/library flag handling. | `TODO.md`; `doc/MODULE_FFI_BINARY_ROADMAP.md`. |
| I-019 | Developer tooling | P2 | `vyb.toml`, `vyb build` project mode, `vyb test`, package resolution/lockfile, formatter, linter, LSP, REPL, and `vyb doc`. | `TODO.md`; `doc/Development_Guide.md`. |
| I-020 | Networking | P2 | **SHIPPED** — sockets, `TcpStream`/`TcpListener`/`UdpSocket`, async socket I/O, pure-Vyb HTTP client+server, TLS, verified HTTPS. | See `stdlib/{network,http,tls,https}`, `test/modules/test_{network,tcp,udp,http}*.vyb`, and `test/tls/`. |

## Source-Level TODO Hotspots

> **2026-08-17 note.** Several bullets below predate features that have since
> shipped (async is a real cooperative executor, lambdas have full closure
> codegen, select/match inference + exhaustiveness are done, `fail`/`trap`
> propagate cross-function, `defer` runs on normal exits). Kept for the
> baseline; the genuinely-open items are named inline below.

- `src/vre/semantic.cpp`: borrow/view typing, optional/result typing, template
  monomorphization, generic aspect implementation handling, Vec type validation,
  and richer type checking are incomplete.
- `src/vre/llvm/cgen_expr.cpp`: await is placeholder-level, list
  comprehensions are unimplemented, generic instantiation is TODO, `this`/`super`
  are placeholders, select type inference is incomplete, and lambda codegen lacks
  closure semantics. **Now:** await + scheduling are real; list comprehensions,
  `this`/`super` placeholders, and some generic-instantiation edges remain open.
- `src/vre/llvm/cgen_stmt.cpp`: legacy try/catch/throw codegen is stubbed or
  obsolete relative to `fail`/`trap`; untrapped `fail` does not build a full
  `VybError`.
- `src/vre/llvm/cgen_vec.cpp`: several Vec methods return placeholders or only
  simulate copies. **Now:** bounds-checked `get`, `contains`, and iterator/
  higher-order forms are in; struct-element edge cases remain.
- `src/runtime/error_handling.cpp`: untrapped error details, defer stack, and
  ensure stack are stubs. **Now:** cross-function propagation + `__vyb_runtime_untrapped_error`
  are real; runtime defer stack (surviving `fail`/unwind) remains the open gap.
- `runtime/vyb_type_metadata.c`: dynamic sizing, Vec metadata, and nested struct
  handling are TODOs.

## Documentation Conflicts To Resolve

These should be fixed before using the docs as release guidance.

1. FFI status conflict:
   `doc/FEATURE_STATUS.md` marks `extern "C"` as implemented, while
   `doc/FFI_DESIGN.md`, `doc/MODULE_FFI_BINARY_ROADMAP.md`, and
   `test/future_features/test_ffi_extern_c.vyb` show it as planned/expect-fail.
   Source appears partial, not complete.
   **2026-08-17:** `extern "C"` is now working (see I-002) — extern blocks, ABI
   aliases, `#[repr(C)]`, native `--link`, OpenSSL binding. Update the stale
   FFI design docs to reflect shipped core with variadics/bindgen open.

2. Error handling status conflict:
   `doc/ERROR_TRAP.md` says core error handling phases are complete, while
   `doc/archive/ERROR_PROPAGATION_DESIGN.md`, `test/trap/TEST_RESULTS.md`, and runtime
   source still show cross-function propagation and full `VybError` construction
   as incomplete.
   **2026-08-17:** cross-function propagation (Phases 1-5) and
   `__vyb_runtime_untrapped_error` are shipped; full `VybError` detail and
   runtime defer/ensure stacks remain the open residual.

3. `ensure` meaning conflict:
   `doc/archive/ENSURE_IMPLEMENTATION_STATUS.md` documents block cleanup
   `} ensure -> { ... }` as complete. `test/future_features/test_ensure_statement.vyb`
   and `TODO.md` describe contract-style `ensure condition else fail(...)` as
   unimplemented. These are two different features and need separate names/status.
   **2026-08-17:** block-cleanup `} ensure -> { ... }` is the implemented form;
   contract-style `ensure cond else fail` remains unimplemented/separate.

4. Aspect/Self status conflict:
   `test/aspect/PHASE_6_ROADMAP.md` says Self resolution is partially complete,
   while `doc/archive/SELF_RESOLUTION_COMPLETE.md` says it is complete but still lists
   Vec and constructor inference issues. Update Phase 6 docs to reflect current
   source behavior.
   **2026-08-17:** associated-type `Self::Item` resolution (concrete + generic
   binds) and Vec/map constructors are shipped; Phase 6 docs can be archived as
   resolved.

5. Class/OOP direction conflict:
   `doc/WHY_TRAITS_NOT_CLASSES.md` says classes are not planned, while older AST
   and trait design docs still mention classes/inheritance. Decide whether
   classes are removed or legacy parser-only support.
   **2026-08-17:** the design decision is sealed toward `struct`+`aspect`/`bind`
   (no classes); legacy class-ish syntax is only parsed for fixture compat.
   Drift docs should mark `trait`/`class` terminology as historical.

6. Terminology conflict:
   Docs mix `trait`/`impl` with `aspect`/`bind`. The implementation and README
   are mostly `aspect`/`bind`; docs should standardize or explicitly mark old
   names as historical aliases.
   **2026-08-17:** README/guide use `aspect`/`bind`; live term is `aspect`
   (a lingering `trait object` phrase in the guide was corrected). Archive/sweep
   remaining design docs.

7. Syntax conflict:
   Docs mix `fn`, colon-style parameter syntax, `=>` match arms, and
   `<T: Trait>` bounds with newer name-first syntax, `->`, and `<T<Aspect>>`.
   Update examples or mark legacy syntax as deprecated.
   **2026-08-17:** the guide/refman standardize on name-first syntax, `->`,
   and `<T<Aspect>>`; match arms use `pattern -> body` (no `=>`), and `fn`
   remains the *function type* keyword. Sweep remaining legacy-syntax examples
   in design docs.

8. Production-ready language claims:
   Several docs call Vyb production-ready, but the source audit shows major 1.0
   blockers remain. Release/status docs should use a more precise feature
   matrix and avoid broad production-ready claims until P0 items are complete.

## Suggested Implementation Order

1. Reconcile status docs and future-feature tests so the project has one
   canonical source of truth. **Done 2026-08-17** (this file + `TODO.md` +
   `docs/refman/PROGRAMMERS_GUIDE.md` reconciled against shipped commits and a
   fresh 993-test sweep).
2. Finish a minimal module system and FFI path because those unblock File I/O,
   stdlib modules, networking, and multi-file programs. **Done 2026-08-17** —
   module system (phases 1.1-1.5) and the FFI core shipped; variadics/bindgen remain.
3. Complete ownership runtime enforcement, especially `mild<T>`, before expanding
   container/resource APIs that depend on lifecycle correctness.
4. Finish error propagation and real `VybError` construction so `fail`/`trap`
   works across function boundaries and produces useful runtime diagnostics.
5. Complete aspect associated types and iterator design, then implement
   `Iterator`, `Option`, core aspects, and stdlib collections.
6. Complete lambdas, enum/sum types, and pattern matching together because they
   share closure/function-value, variant, and destructuring semantics.
7. Round out developer tooling (`vyb.toml`, `vyb build`, `vyb test`, formatter,
   LSP, REPL) after core language semantics settle.

## Verification Notes

The original implementation audit was source/documentation-focused. The
tests/examples/demos repair pass was behavior-checked with:

- `cmake --build build -j2`
- `python3 test/run_tests.py --test-dir test/new_features --vyb build/vyb --execute-jit`
- `python3 test/run_tests.py --test-dir test/ffi --vyb build/vyb --execute-jit`
- `build/vyb` over every `examples/*.vyb`
- `build/vyb` over every `demos/*.vyb`

## Open RFEs (compiler architecture)

Two behavioural footguns are logged for a future, non-urgent fix. Both are
pre-existing; neither affects current green tests or the demos, but each is a
latent wrong behaviour waiting on the right shaped input.

### RFE-A: ownership/transfer metadata in the type system, not codegen guessing

`exprProducesOwnedStringTemp` (`src/vre/llvm/cgen_expr.cpp`, ~9462) decides at
codegen time whether an expression yields a freshly-allocated owned String temp
that may be reclaimed with `__vyb_string_free` after consumption. It infers this
from syntax alone:

- `a + b` with a String operand,
- `.to_string()` / `.toString()` on a non-String receiver,
- any call whose declared result type is `String`,
- `await` of a String future,
- minus an ad-hoc `Vec`-receiver exclusion bolted on after a use-after-free.

This heuristic is deliberately conservative (unknown provenance is treated as a
borrow, so it is not freed), but it is fundamentally fragile:

- A user function that *returns a borrow* of a String is misclassified as owned,
  so the temp is freed while still referenced (`__vyb_string_free` on the buffer)
  → premature free / use-after-free.
- An owned String producer the heuristic does not recognise leaks.

Worse, `exprIsStringTransfer` (`src/vre/llvm/cgen_expr.cpp`, ~9537) is a thin
copy of the same heuristic, and it drives ownership-transfer decisions across
`Vec` push (`src/vre/llvm/cgen_vec.cpp`), variable init (`src/vre/llvm/cgen_decl.cpp`),
struct-field init (`src/vre/llvm/cgen_expr.cpp`), String return
(`src/vre/llvm/cgen_stmt.cpp`), and member access (`src/vre/llvm/cgen_expr.cpp`).
One wrong guess fans out into premature frees or leaks in several places.

Direction for a fix (someday): carry ownership/transfer metadata on the type in
the semantic layer — mark a String-returning function/`await`/concat call result
as owned-vs-borrowed — and have codegen read that flag instead of re-deriving it
from the AST shape. Not now.

### RFE-B: bind method bodies in an imported module must resolve against the
defining module's scope

A method implemented via `bind Aspect -> Type` in an *imported* module cannot
see that module's sibling `share(all)` free functions unless the consuming
module also imports those names. The design intent (`src/vre/semantic.cpp`,
`topLevelDeclarationName` comment near the bind case, ~line 139) is that a bind
body resolves against the module that defines it — so a carried bind's
supporting structs/aspects/helpers stay visible wherever the bind runs — but
that does not currently hold for imported modules.

Repro: `demos/VybLynx/src/url.vyb` implements `bind UrlOps -> Url`; its
`resolve(self, reference)` method calls sibling `ref_scheme` and
`is_absolute_scheme`. Importing `url` as `import url::{Url, UrlOps, parse_url}`
and calling `u.resolve(...)` fails semantic analysis with

```
Undefined identifier: ref_scheme
Undefined identifier: is_absolute_scheme
```

unless the importer also lists `ref_scheme`/`is_absolute_scheme` (which
`demos/VybLynx/src/main.vyb` happens to do, so the demo works). A free function
in the same module calling a sibling (the old `resolve_url` → `url_authority`)
resolves fine, so the gap is specific to bind-method bodies of imported modules.
Any future `url` consumer would otherwise need to over-import the helpers.

Direction: root the bind-method body's scope at the defining module's resolvable
scope (matching free-function bodies) rather than the importer's `currentScope`.
Not now.

### RFE-C: stdlib `qt` module — native GUI bindings

Add a stdlib `qt` module (mirroring how `curses` was folded into the standard
library) so Vyb programs can build native desktop GUIs: windows/widgets, an
event loop + signals/slots, layout, common controls, and timers. Proposal only
for now; open questions before any work:

- binding strategy (pure-Vyb wrapper vs C FFI shim over Qt5/Qt6),
- scope (Widgets vs Quick/QML),
- how the Qt event loop interacts with the stdlib `asyncs` executor and the
  existing curses-based VybLynx TUI.

Initial pass landed: `stdlib/qt/mod.vyb` over a C++ bridge
(`runtime/vyb_qt_bridge.cpp`, `__vyb_qt_*` extern "C" shims registered in
`main.cpp` like the curses shims), exposing a deterministic subset - Qt5
Widgets window + label (title/size/show/hide), a polled event loop
(`qt_process_events`), and a steady-clock repeat timer (`qt_set_timer` /
`qt_timer_fired`). No signal/slot callbacks yet; the module is poll-driven so
it is deterministic under the `offscreen` QPA platform in tests (`test/qt`),
and degrades gracefully to stub shims when Qt5 is absent. Open follow-ups:
signal/slot and layout support, richer controls, deeper asyncs interaction.
signal/slot and layout support, richer controls, deeper asyncs interaction.

Expansion plan (approved follow-through, Phases A-D):

- **A - primitive controls (fit the Int/String FFI, no new machinery):**
  button (text/enabled), text edit (`QLineEdit`: text, placeholder), checkbox
  (checked state), progress bar (max/value), and box layouts
  (`qt_vbox`/`qt_hbox`/`qt_layout_add`). Each function is one row in the cgen
  marshaling block + semantic list + `main.cpp` registration + `mod.vyb`.
- **B - polled signals (real Qt signals, no closure crossing):** the bridge
  connects each control's primary signal (clicked / textChanged / toggled) to a
  lambda that enqueues `(widget handle, event kind)`; Vyb drains the queue with
  `qt_event_count`/`qt_event_handle`/`qt_event_kind`/`qt_event_pop` and
  dispatches to its own handler map. Deterministic and `offscreen`-testable.
- **C - typed/validated handles:** a `qt_kind(handle)` introspection + a Vyb
  `QtWidget { kind, h }` wrapper so wrong-kind operations are caught early
  (the bridge already rejects via `dynamic_cast`).
- **D - event loop / asyncs integration (the long pole):** `qt_run()` driving
  `app.exec()` on the main thread while program logic runs on the `asyncs`
  fiber pool, plus a processEvents() pump on idle worker spins so paints/timers/
  signals keep flowing. Touches `cgen_async_impl.cpp`.
- **Cross-cutting:** a table-driven generator (`tools/gen_qt.py`) that emits the
  cgen block, semantic list, `main.cpp` registrations, and `mod.vyb` wrappers so
  adding a widget is one table row (kills the hand-edited four-way drift).
- **Demo:** `demos/qt_login.vyb` (edit + button + checkbox + status label, click
  handler hitting `https` on the async pool) - runs under `offscreen` in the
  harness and as a real window on a display.

Status (this round): Phases A and B landed. Controls (button, edit, checkbox,
progress) plus vbox/hbox layouts and `qt_kind` introspection were added across
the bridge (`runtime/vyb_qt_bridge.cpp`), stub (`runtime/vyb_qt_stub.cpp`), cgen
dispatch, semantic allow-list, `main.cpp` registrations, and `stdlib/qt/mod.vyb`;
signals (`clicked`/`textChanged`/`toggled`) now enqueue FIFO records drained via
`qt_event_count`/`handle`/`kind`/`pop`. Covered headlessly by
`test/qt/test_qt_controls.vyb`; `QtWidgetKind`/`QtEvent` enums are exported with
`share(all)`. Phases C (typed handles) and D (event-loop/asyncs integration)
remain open.

Honest limits: no custom painting/styling/tables/trees (compose from
primitives); remain on Qt5 for now (note a Qt6 migration: CMake package +
`QObject::connect` signature tweaks).
