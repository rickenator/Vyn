# Vyb Programming Language - Changelog

All notable changes to the Vyb programming language will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [0.7.2] - 2026-08-17

### Added
- New `term` stdlib module: interactive (typing-direct) console I/O over new
  weakly-linked `__vyb_stdin_*` / `__vyb_eprint*` / `__vyb_term_*` runtime
  helpers in `runtime/vyb_runtime.c` (registered with the ORC JIT), discovered
  via `import term`.
  - stdin: `stdin_read(maxlen)`, `stdin_read_line()`, `stdin_isatty()`, and
    terminal raw mode (`stdin_raw_enable()` / `stdin_raw_disable()`) for
    single-keypress reads without echo or Enter.
  - stderr + flush: `eprint(s)`, `eprintln(s)`, `flush()`, `stderr_flush()` so
    diagnostics don't corrupt a rendered TUI page.
  - terminal control: `term_cols()`, `term_rows()`, `term_clear()`,
    `term_move_cursor(row, col)`, `term_hide_cursor()`, `term_show_cursor()`.
  - `test/term/term_io.vyb` (deterministic surface check) and
    `examples/term_input.vyb` (cooked line + raw-mode keypresses).

### Changed
- Version bumped to 0.7.2 to match the shipped `term` stdlib module
  (`CMakeLists.txt`, `README.md`, `doc/FEATURE_STATUS.md`, `TODO.md`).

---

## [0.7.1] - 2026-08-17

### Added
- Project system foundation: `vyb.toml` manifest (`[package]`, `[dependencies]`,
  `[[bin]]`), `vyb build` (build every bin from the manifest, wiring local path
  dependencies into the module search paths), `vyb new` project scaffolding, and
  `vyb.lock` written for resolved local path dependencies.

### Changed
- Native `--build` / `-b` now reliably produces runnable executables: the link
  step includes the C++ runtime atoms (`error_handling.cpp`, `intrinsics.cpp` —
  which define `__vyb_register_typename` and `__vyb_closure_retain/release`) and
  drives the final link through the C++ compiler driver so CRT, libstdc++ and
  libgcc resolve (previously native links left these symbols undefined).
- Runtime and stdlib search paths for native builds are now resolved relative to
  the compiler executable, so `vyb build` works from any project directory.
- README: projects (`vyb.toml`) documentation; version bump to 0.7.1.

## [0.7.0] - 2026-08-17

### Added
- Lossless nested-struct JSON round-trip: `.to_string()` / `T::from_string()` now
  cover `Vec<T>`, `Vec<struct>` (array of objects), and arbitrary-depth nested
  structs via a growable emitter with no fixed-size cap; deserialization never
  trusts lengths/counts from input (allocation-bomb safe).
- Native `T?` equality: `==` / `!=` compare presence and (when both present) the
  payload (`Int`, `Float`, `Bool`, `String`); ordering on optionals is now a
  clean semantic error.

### Changed
- By-value recursive structs (e.g. `struct Node { next<Node?> }`) are rejected
  with a clear error instead of a stack-overflow crash.
- `fn` / `Self` field types are documented as excluded from ser/deser.
- Programmer's Guide: full primitive catalog, main-return model ("any type"),
  and a Present/Absent explanation for the native `T?` optional.


### Changed
- Version bumped to 0.6.2 to match the shipped stdlib/async/network/TLS/HTTPS
  feature set (`CMakeLists.txt`, `README.md`, `doc/FEATURE_STATUS.md`,
  `TODO.md`).
- Reconciliation pass aligns `TODO.md` and `UPDATE_LOG.md` with the current
  source: full-suite status is now **993 tests, 989 passing** (4 pre-existing
  trap/vec failures), completed 1.0 criteria are checked off, and the
  implementation-audit backlog rows are marked SHIPPED where landed.

### Fixed
- Refman generator now preserves doc-comment indentation and full
  `Name<K, V>` declarations; regenerated module pages and graph.
- `PROGRAMMERS_GUIDE.md`: expanded `;`/`/`-crammed listings, de-blockquoted
  the teardown note, and placed each EBNF key feature on its own line.

## [Unreleased]

### Added
- **`tls` stdlib module** — TLS over OpenSSL (`import tls`). Client/server
  `SSL_CTX` from in-memory PEM (no file-path coupling), an `SSL` session bound
  to an already-connected fd, handshake (`tls_connect`/`tls_accept`), encrypted
  `tls_write`/`tls_read`, `tls_close`, and `tls_error_code`/`tls_error_message`
  diagnostics. Allocation-free `TlsContext`/`TlsStream` structs with aspect/bind
  methods. OpenSSL is linked into the binary and `dlopen`ed into global scope
  so the ORC JIT resolves libssl/libcrypto (optional via `VYB_USE_OPENSSL`).
  Covered by `test/tls/test_tls_loopback.vyb` (threaded handshake + echo) and
  `test/tls/smoke_openssl.vyb` (valgrind-clean for the TLS path).
- **Verified TLS** — `tls_client_context_verified(ca_pem)` adds peer
  verification to the `tls` module: it trusts an in-line-pinned CA cert (or the
  system default CA paths when `ca_pem` is "") and enforces the expected
  hostname against the peer certificate. The `host` passed to `tls_stream`
  doubles as the checked name. Covered by `test/tls/test_tls_verified.vyb`,
  which proves a matching (correct) hostname succeeds over a pinned self-signed
  cert while a mismatching hostname fails the handshake (valgrind-clean).
- **Verified HTTPS + DNS resolution** — `network::socket_resolve(host)` turns a
  hostname or IP literal into a dotted-quad IPv4 string via `getaddrinfo`, and
  `https_get_full_verified(host, port, path, ca_pem)` / `https_get_verified`
  chain it with `tls_client_context_verified`: the hostname is resolved to an
  IP for the connection while being verified against the peer certificate
  (SNI + hostname check), enabling real, pinned-CA (or system-default) HTTPS
  to named hosts. `https_selfhost_verified` exercises the full verified path
  offline. Covered by `test/tls/test_https_verified.vyb` (valgrind-clean).
- **`https` stdlib module** — a TLS-secured HTTP client (`import https`) that
  layers `tls` over the pure-`http` client, reusing http's status/header/int
  parsers and the shared `HttpResponse` type, with its own TLS-aware I/O.
  `https_get_full`/`https_get` do a full encrypted GET round-trip, and
  `https_selfhost` stands up a throwaway TLS server to validate the wiring from
  a single import. Covered by `test/tls/test_https_client.vyb` (valgrind-clean).
- **Pure-Vyb HTTP/1.1 client** — `http_get_full(host, port, path)` returns a
  parsed `HttpResponse` (`status`, `reason`, `headers` as a `Vec<String>`, and
  `body`) after a full GET round-trip: it connects over a socket, sends the
  request, parses the status line + headers (with `http_status_code`,
  `http_header`-style lookups and a case-insensitive `http_header_value`), and
  reads a `Content-Length` body (falling back to reading until the peer
  closes). Pure Vyb — no new FFI. Covered by
  `test/modules/test_http_client.vyb` (valgrind-clean).
- **`TcpStream` / `TcpListener` / `UdpSocket`** — ergonomic, method-bound socket
  types in the `network` module, layered on the runtime sockets. `tcp_listen` /
  `tcp_accept` / `tcp_connect` build a `TcpStream` with `.write(...)` /
  `.read(...)` / `.close()` / `.peer_*()`, and `udp_bind` builds a `UdpSocket`
  with `.send_to(...)` / `.recv_from(...)` plus `udp_last_peer_ip()/port()` to
  see where a datagram came from. Async variants (`async_tcp_*` / `async_udp_*`)
  run on the event-loop executor and suspend the fiber instead of blocking a
  worker. UDP gained new runtime `sendto`/`recvfrom` helpers and `SOCK_DGRAM` /
  `IPPROTO_UDP` constants. Covered by `test/modules/test_tcp_stream.vyb`,
  `test/modules/test_udp_socket.vyb`, and `test/async/async_net_wrappers.vyb`
  (all valgrind-clean).
- **Async I/O over the event loop** — `import asyncs` gains suspendable,
  non-blocking socket operations: `async_accept(fd)`, `async_connect(fd, ip,
  port)`, `async_send(fd, data)`, and `async_recv(fd, maxlen)`. When a socket
  isn't ready, the calling fiber suspends instead of blocking a worker: a
  dedicated poll-pump thread watches every suspended fiber's fd (plus a
  self-pipe wake), and requeues the fiber on its home worker the moment the fd
  becomes ready. The pair of an async echo session and three concurrent echo
  sessions on one listener are covered by `test/async/async_io_echo.vyb` and
  `test/async/async_io_multi.vyb` (both valgrind-clean, no leaks).
- **`async for` over channels** — `async for (item<T> in ch)` drains a channel
  as an async stream: it receives messages (Int or `String`, chosen by the
  loop-variable type) until the channel is closed and drained, then stops. It
  desugars to a `while(true)` loop over the lossless `chan_recv_opt` /
  `strchan_recv_opt` primitives, so no valid payload is reserved as a sentinel
  (a `-1` Int or empty `String` is just data). `chan_close` / `strchan_close`
  were added so a stream has a way to signal termination. Multi-threaded
  producer + close patterns are covered by `test/async/async_for_chan.vyb`
  (valgrind-clean).
- **Async lambdas** — `async |x| -> await process(x)` is now a first-class
  value. The body is compiled as a closure that runs as a cooperative task; a
  call to the lambda returns a `Future<T>` (the public type is
  `fn(...) -> Future<T>`) which `await` drives. Captures, String/zero-arg
  forms, and passing an async lambda as a future-returning closure param are
  covered by `test/async/async_lambda.vyb` (valgrind-clean).
- **Closures as async params** — the event-loop executor's last param stage: an
  `async fn(f<fn(...)->...>, ...)<Future<T>>` now snapshots a closure-typed
  argument into the task env, retaining its capture environment (+1) so it stays
  alive asynchronously and is invokable from the worker; the env's per-layout
  dtor releases that reference on task cleanup. Closures with captures (Int +
  String), nested `await` sharing a closure, and closure+String+scalar mixes are
  covered by `test/async/async_closure_param.vyb` (valgrind-clean).
- **Drop the Rust-shaped `Option<T>` enum** — the `Option<T>`/`Some`/`None`
  built-in generic enum is removed from the compiler, along with the
  transitional `core::option` bridge (`OptionInt`) and its prelude imports.
  Every former call site (chan `poll()`, map `.get()`, iterator `next()`,
  `mild.grab()`) already uses the native `T?`; `Some(x)`/`None` no longer
  compile. The built-in `Result<T,E>` (`Ok`/`Err`) enum and the ownership/
  enum-matching paths shared with it are preserved. The module import/discovery
  tests and `examples/stdlib_demo` now exercise `core::math` (`clamp`) /
  `prelude::prelude_ok` instead.
- **Native `T?` optional with `else`-default** — Vyb's own optional (`<Type>?`,
  `ast::OptionalType`) is now constructible and consumable, replacing the
  Rust-shaped `Option<T>`/`Some`/`None` idiom one surface at a time.
  `T?(v)` builds a present optional, `T?()` the absent one, and
  `optional else default` yields the payload when present or the default
  otherwise — reading as a Vyb sentence via the existing `else` keyword
  (`ensure cond else handling`). Present is the bare value and absence is
  `?`/`else`; no `Some(v)` / `.unwrap()` ceremony. Works for scalar, `String`,
  and `Float` payloads, through returns, function parameters, and
  right-associative chained defaults (`a else b else c` == `a else (b else c)`).
  Covered by `test/new_features/test_native_optional.vyb`. The `else` infix is
  disabled inside `ensure` conditions (which own a trailing `else`).
- **`HashMap`/`BTreeMap.get` now returns the native `V?`** — `m.get(key)` returns
  a native optional instead of `Option<V>`/`Some`/`None`, read as
  `m.get(key) else default` (payload when present, `default` when the key is
  absent). Generic-optional plumbing: `concreteTypeStringToNode` parses a
  trailing `?` into `OptionalType`, and monomorphization substitutes generic
  parameters inside `T?` for bound-method return types and `T?()`/`T?(v)`
  constructions (`substituteTypeParameter`, `resolveParameterTypeWithSubstitution`).
  Covered by `test/modules/test_collections_{hashmap,btreemap,growth}.vyb` and
  `test/modules/test_struct_constructors.vyb`.
- **Iterator `next()` returns the native `Item?`** — every `core::iter::Iterator`
  `next()` (the stdlib `VecIter`, `MapIter`, `HashIter`, `BTreeIter`, and the
  protocol itself) now returns a native optional (present bare payload / absent)
  instead of `Option<Item>`/`Some`/`None`. `match`/`select` gained the optional
  surface: the present arm binds the bare value (`v -> ...`) and the `?` wildcard
  is the absent arm (`? -> break`), with exhaustiveness enforced (a match must
  cover the present value and the `?` absent case). The `for (x in it)` desugar
  now emits that native-optional match (`x -> { body }` / `? -> break`) in place
  of `Some`/`None`, including the step/skip form. Covered by
  `test/modules/test_vec_iter.vyb`, `test_iterator_protocol.vyb`,
  `test_collections_iter.vyb`, and the `test_for_iter*` suites.
  The `else` operator also now tolerates an unresolved generic payload type (a
  generic free function returning `T?` whose `T` isn't resolved at the call site)
  instead of rejecting it, since codegen enforces against the concrete payload.

- **`mild<T>.grab()` returns the native `our<T>?`** — the last Rust-shaped
  `Option<T>`/`Some`/`None` call site. While the `mild` object is live, `grab()`
  yields a present optional holding the retained `our<T>` (giving the caller
  temporary borrow-like access); once released/overwritten it yields the absent
  value. Consumed via the native optional surface, e.g.
  `match (m.grab()) { o -> o.value, ? -> -1 }` or `else`. Codegen builds the
  `{ our<T> value, i1 hasValue }` optional struct, and `codegenType` now always
  lowers any `T?` to the uniform `{ value, i1 }` struct (no more bare-pointer
  collapse for pointer payloads), keeping `else`/`match`/chan-`poll`/`grab` on
  the same layout. Covered by `test/ownership/mild_grab_live.vyb`,
  `mild_grab_released.vyb`, `mild_overwrite_release.vyb`, and
  `mild_copy_semantics.vyb`.

- **Typed generic `chan<T>` channels** — a compiler-native generic,
  thread-safe channel. `chan<T>()` / `chan<T>(cap)` construct an
  unbounded/bounded channel as a single i64 runtime handle; the ergonomic
  surface is `send(v)`, `recv()` (blocking), `poll()` (non-blocking -> native `T?`;
  for scalar payloads it is absent when empty, read via `poll() else default`, replacing
  the former `Option<T>` Some/None), `len()`, `free()`, and `handle()` (raw Int for
  `chan_select`). Int-family, `Float` (IEEE bit pattern), `Bool` (0/1), and
  `Char` payloads use the int-slot channel runtime; String payloads use the
  refcounted string runtime (with the empty-string sentinel poll). A chan is
  shared by value across pthreads; scalar `poll()` is unambiguous because it
  reports readiness explicitly (`test/modules/test_chan_typed.vyb`,
  `test/modules/test_chan_threaded.vyb`, `test/modules/test_chan_scalar.vyb`).
  Non-identifier receivers now dispatch too: a chan returned by a function
  (`make().send(x)`) or held as a struct field (`h.ch.recv()` / `h.ch.poll()`)
  lowers like the named-variable path (`test/modules/test_chan_nonident.vyb`).

- **`vyb bindgen --full` non-integer function-like macros** — the libclang
  backend now binds comparison/logical, ternary, and string macro bodies as
  type-aware Vyb functions instead of skipping them. Comparisons and logical
  operators map directly (`IS_EVEN(x) (((x)%2)==0)` → `IS_EVEN(x<Int>)<Bool>`),
  C `? :` ternaries lower to Vyb `select` (`MAX(a,b) ((a)>(b)?(a):(b))` →
  `<Int>`, with nested ternaries like `CLAMP` supported), and string bodies
  become `<String>` functions (`STATUS()` → `"ok"`). Covered by
  `MAX`/`CLAMP`/`IS_EVEN`/`IS_POS`/`STATUS` in `test/bindgen/full_preproc.h`.
- **`vyb bindgen --full` C unions** — a C `union` now binds as a
- **`vyb bindgen --full` C unions** — a C `union` now binds as a
  `#[repr(C)]` struct whose highest-aligned member is the accessible anchor
  plus a `[UInt8; N]` pad to the union's total size, so the struct's size and
  alignment match the C ABI (named, pad-based, and anonymous-typedef unions;
  union fields inside structs; union parameters). Covered by
  `test/bindgen/unions.h` / `unions.vyb` / `test_unions_bindings.vyb`.
- **`vyb bindgen --full` fixed-size array struct fields** — C struct members
  declared as fixed-size arrays (direct, nested e.g. `double m[2][3]`, or via an
  array typedef like `typedef char name_t[8]`) bind as contiguous Vyb
  value-array fields (`name<[CChar; 8]>`, `values<[CInt; 4]>`,
  `matrix<[[CDouble; 3]; 2]>`) instead of a pointer to the first element.
  Flexible array members (`int data[]`) still skip their struct with a warning.
  Covered by `test/bindgen/arrstruct.h` / `arrstruct.vyb` /
  `test_arrstruct_bindings.vyb`.
- **`vyb bindgen --full` (libclang full preprocessor)** — a new
  `vyb bindgen <header.h> --full` backend expands `#include` and evaluates
  conditionals (`#if`/`#ifdef`), resolves typedefs through their canonical
  types (`int32_t` → `CInt`, `uint64_t` → `CULong` on LP64, `size_t` → `CSize`),
  and binds `#define` macros: object-like numeric/string and constant-expression
  macros (`WIDE (2 * COUNT)` → `WIDE()<CInt>`, returns 8) as shared constant
  functions, and function-like integer-arithmetic macros
  (`SQUARE(x) ((x)*(x))` → `SQUARE(x<Int>)<Int>`) as shared functions.
  `-D NAME[=VAL]` flags steer `#ifdef`/`#if`. It runs in a standalone
  `vyb-libclang` helper so libclang's LLVM command-line options cannot collide
  with the statically-linked JIT engine. Covered by `test/bindgen/full_preproc.h`
  / `full_preproc.vyb` / `test_full_preproc_bindings.vyb`.
- **Docs: consolidated `doc/`** — archived 19 obsolete/superseded documents
  (`ROADMAP.md`, `TODO_CURRENT.md`, completion markers, phase summaries,
  proposals/RFCs, duplicated syntax docs) into `doc/archive/`, rewrote
  `doc/README.md` as an index of the active docs, and repointed cross-references
  in the living docs so no markdown links to `doc/` are broken.
- **`select` as a statement** — `select (expr) -> { ... }` may be used without a
  binding target for side-effects only. Block arms without `pass` now branch to
  the select end block instead of leaving an unterminated case block. Covered by
  `test/new_features/test_select_statement.vyb`.
- **`vyb bindgen` array-parameter decay** — C function parameters written as
  arrays (`T a[]` / `T a[N]`) decay to pointers, matching the C ABI. They now
  bind as pointers instead of scalars: `char s[]` → `CString`, and any other
  element type becomes `loc<T>` (`double vals[]` → `loc<CDouble>`).
  Covered by `test/bindgen/arrayparams.h` /
  `test/bindgen/test_arrayparams_bindings.vyb`.
- **`vyb bindgen` function-pointer parameters** — C function-pointer params,
  both inline (`void (*cb)(int, void*)`) and typedef'd (`typedef int (*op_fn)(int,int)`),
  bind as `loc<fn(...) -> ...>` (`cb<loc<fn(CInt, loc<CVoid>) -> CVoid>>`), a bare
  C function pointer that ABIs to a single `ptr` and carries the callsite's
  declared name. Covered by `test/bindgen/fnpointer.h` /
  `test/bindgen/test_fnpointer_bindings.vyb`.
- **`#[repr(C)]` struct field construction** — a struct literal now casts each
  initializer to its field's declared LLVM width (e.g. truncates a Vyb `Int`
  literal to an `i32` `CInt` slot) instead of storing everything at `i64`
  width. Previously the 8-byte stores into 4-byte fields corrupted the offsets
  and values of later fields (`T { a = 10, b = 99 }` read `b` as `0`). Covered
  by `test/ffi/repr_c_struct_fields.vyb`.
- **C integer/float `toString`** — `println`/`toString` now formats any signed
  C integer width (`CShort`/i16 and friends), not just `Int8`/`Int32`/`Int`, by
  sign-extending to `i64`; unsigned sizes were already handled. Previously
  `CShort` produced "Unsupported type for toString conversion: CShort". Covered
  by `test/ffi/c_int_printing.vyb`.
- **FFI callbacks through function pointers** — a Vyb function can be passed to a
  C callback parameter (`loc<fn(...)>`), C invokes it, and a function pointer
  returned/stored from C can be called from Vyb via an indirect call. Previously
  a C callback bound as Vyb `fn(...)` (a `{ptr, ptr}` closure) and could not be
  invoked with a function argument; `loc<fn(...)>` maps to a single bare code
  pointer so the ABI lines up. Covered by `test/ffi/callback_fnptr.vyb`.
- **`vyb bindgen` `#define` constants** — object-like C macros
  (`#define NAME <number|string|float>`) bind as shared constant functions
  (`MAX_BUFSIZE()<CInt> -> 4096`), so headers' numeric/string constants are
  usable from a binder. Function-like, multi-line, and non-literal macros are
  skipped with a warning. Covered by `test/bindgen/preproc.h` /
  `test/bindgen/test_preproc_bindings.vyb`.
- **`vyb bindgen` bitfield handling** — a C struct containing bitfields packs
  bits into shared storage and cannot be ABI-represented as Vyb struct fields,
  so it is skipped from emission with a warning, while plain structs beside it
  still bind. Covered by `test/bindgen/bitfields.h` /
  `test/bindgen/test_bitfields_bindings.vyb`.
- **Nested `select`** — a `select` may now appear as an arm body of an enclosing
  `select` (naked or a block with `pass`), enabling per-branch sub-selection.
  Previously the outer select's type-inference preview ran the inner select's
  block machinery, leaving dangling unterminated `select.end`/`select.case`
  blocks that failed LLVM verification. Covered by
  `test/new_features/test_select_nested.vyb`.
- **`ensure` post-condition** — `ensure condition else <handling>` desugars to
  `if (cond) {} else { handling }`; handling may be a block, a single statement
  (e.g. `return -1`), or `fail<T>(...)`.

- **Unsigned integer literal suffix `u`** — `255u`, `0xffffu`, `0b101u`, and
  `4294967295u` parse as unsigned literals (default width `UInt64`) instead of a
  signed `Int`, and participate in the explicit-width assignment policy (a
  fitting `u`-constant still assigns to narrower unsigned types; an overflow to a
  signed type is an error). Covered by `test/expressions/test_uint_literals.vyb`
  and `test_uint_literal_range.vyb`.
- **Full-range `UInt64` literals** — `u`-suffixed literals may now span the
  entire unsigned 64-bit range, e.g. `18446744073709551615u` (2^64 − 1), which
  previously overflowed in the parser. Such constants parse, assign to `UInt64`
  (or any fitting unsigned width), and print exactly; assigning them to a signed
  type (or a narrower unsigned type) is still an out-of-range error. Covered by
  `test/expressions/test_uint_literal_fullrange.vyb` and
  `test_uint_literal_signed_overflow.vyb`.
- **Hex and binary integer literals** — `0x`/`0X` (base 16) and `0b`/`0B`
  (base 2) integer literals now parse to their true value. Previously the lexer
  emitted the raw `0x11`/`0b1100` lexeme and `std::stoll` truncated it at the
  `x`/`b`, yielding `0`. Works with all sized integer types (e.g. `UInt8 = 0x80`)
  and flows into byte-composition expressions. Covered by
  `test/expressions/test_literals.vyb`.
- **Integer `as` casts** — `value as TargetType` now converts between the sized
  integer types (`Int8`–`Int64`, `UInt8`–`UInt64`): widening sign- or
  zero-extends based on the *source* type (`Int8 as Int64` sign-extends,
  `UInt8 as Int64` zero-extends), narrowing truncates, and equal-width signedness
  changes are bit-preserving. This supports the idiomatic systems pattern of
  packing bytes into a wider integer, e.g. composing an `Int64` from eight
  `UInt8` via `(b0 as Int64) | ((b1 as Int64) << 8) | ...`. Also registers the
  `Int64` type alias at codegen (it previously errored as an unknown type).
  Covered by `test/expressions/test_int_casts.vyb`.
- **Bitwise operators** — `|` (or), `&` (and), `^` (xor), `~` (not), `<<` (left
  shift), `>>` (right shift) on `Int`, plus `&=`, `|=`, `^=`, `<<=`, `>>=`
  compound-assigns. Shifts are lexed as adjacent `<`/`>` pairs (keeping nested
  generic closes like `Vec<String>>` intact) while `<<=`/`>>=` stay single
  tokens; codegen lowers to `and`/`or`/`xor`/`shl`/`ashr`/`lshr`. The stdlib File
  I/O module now combines `FILE_*` open-mode bits with `|` instead of `+`. Covered
  by `test/expressions/test_bitwise.vyb`.
- **`String::split()` and `String::format()`** — `split(sep)` (a `StringOps`
  aspect bound to `String`, auto-imported via `core`) returns a fresh
  `Vec<String>` of the parts between each occurrence of `sep` (an empty
  separator yields a single-element Vec holding the whole string; leading,
  trailing, and consecutive separators produce empty parts). `format(args...)`
  (a built-in String method) substitutes sequential `{}` placeholders with the
  string form of each argument of any serializable type; placeholders beyond
  the supplied arguments are emitted verbatim. Covered by
  `test/string/test_str_split.vyb` and `test/string/test_str_format.vyb`.
- **`BTreeMap<K, V>` (ordered map, `import collections`)** — keyed by
  `Comparable`; keys are held in a `keys` vector kept sorted ascending (with a
  parallel `vals` vector), so `get` / `contains_key` use binary search (O(log
  n)) and `put` inserts at the sorted position (O(n) shift). `iter()` yields a
  `BTreeIter<K,V>` (Item = `MapEntry<K,V>`) that walks the entries in ascending
  key order, usable in `for (kv in bt.iter())`; a stored `BTreeIter` is itself
  iterable. Covered by `test/modules/test_collections_btreemap.vyb`.
- **Identifier iterables route onto the `Iterator` protocol** — `for (x in vec)`
  now desugars exactly like `for (x in vec.iter())`, iterating over
  `core::iter::Iterator` instead of the old index-based Vec path. Any iterable
  value exposes an `iter()` that yields an `Iterator`: Vec collections provide
  `iter()` via `import collections`, and the stdlib iterators (`VecIter`,
  `MapIter<K,V>` via `MapEntry`, `HashIter`) are now self-iterable so a stored
  iterator identifier (`for (y in storedIter)`) iterates too. The result is one
  uniform iteration story — `for` always drives `.iter().next()`. Covered by
  `test/modules/test_for_identifier.vyb`.
- **`for`-loop desugar over `Iterator`** — `for (item in <iter-expr>)` now
  desugars onto the `core::iter::Iterator` protocol whenever the iterable is a
  non-identifier expression (e.g. `ints.iter()`); the parser expands it to a
  temp-isolated `while (true) { match (it.next()) { Some(item) -> { body }
  None -> { break } } }` loop. Because the transform is parse-time and
  type-blind it keys off any iterable expression; `0..n` ranges keep the inclusive
  range path. (Identifier iterables now also route via `.iter()` — see below.)
  `break`/`continue` re-enter `next()`, and
  re-evaluating the producer each loop starts a fresh iterator. Covered by
  `test/modules/test_for_iter.vyb`.
- **`for`-loop `skip`/step parameter over `Iterator`** — `for (item in <iter-expr>, step)`
  advances the iterator `step` elements per iteration and yields indices
  `0, step, 2*step, ...` (identical to the Vec index-based path). The desugar
  exists behind `StatementParser::buildForLoopIteratorDesugar`; `break` and
  `continue` remain correct under a step. Covered by
  `test/modules/test_for_iter_skip.vyb`.
- **Nested `their<T>` view-field member access** — reading a field (or method
  call) *through* a `their<T>` view field of a generic struct now resolves. The
  member-expression codegen records each struct-field read's AST type in
  `valueTypeMap`, so chained accesses like `self.set.values.get(i)` /
  `self.map.keys.len()` work where the receiver chain crosses a nested
  `their<HashSet<K>>` / `their<HashMap<K,V>>` field. Previously only
  `their<Vec<T>>` fields had this (Vec-specific) support.
- **`HashMap`/`HashSet` iterators (`import collections`)** — `MapIter<K,V>`
  (`m.iter()`, yields key/value pairs as `MapEntry<K,V>`, read via `kv.key` /
  `kv.value`) and `HashIter<K>` (`s.iter()`, yields values) are bound to
  `core::iter::Iterator` and drive the `for`-loop desugar. They hold the
  collection by reference (no copy), so re-evaluating the producer each loop
  starts a fresh iteration. Covered by `test/modules/test_collections_iter.vyb`.
- **Depth-aware `TypePattern` argument parsing** — the generic-type splitter no
  longer chops nested generics on inner commas, so a two-parameter iterator
  `Item` such as `Option<MapEntry<String,Int>>` monomorphizes to
  `Option_MapEntry_String_Int` instead of the malformed `Option_MapEntry_Int>`.
  This unblocked `MapIter<K,V>` yielding key/value pairs rather than keys only.
- **Closure capture via a uniform environment struct** — every lambda now
  lowers to a closure value `struct { ptr env, ptr fn }` instead of a bare
  function pointer. The semantic analyzer detects free-variable references in a
  lambda body and records them; codegen copies each captured value *by value*
  into a heap-allocated environment created at closure instantiation and passes
  it to the lambda through a hidden first parameter, reloading each capture into
  the lambda's local scope. Captures work for locals, closures returned from
  functions, and closures passed as `fn` arguments to the `Vec` higher-order
  combinators (`map`/`filter`/`reduce`). Non-capturing lambdas use a null
  environment, so existing `fn`-parameter code is behaviorally unchanged. Also
  fixes `fn`-typed call dispatch so a function-typed parameter is never confused
  with an unrelated local lambda of the same name. Expression-body lambdas now
  wrap `char*` String results (e.g. `prefix + x.to_string()`) into a String
  struct on return. Covered by `test/lambda/test_closure_capture.vyb`.
- **Remaining closure capture forms** — mutable, move, and shared (`our<T>`)
  captures complete the closure capture story on top of the uniform env struct.
  - *Mutable capture*: a lambda that assigns to a captured variable stores the
    outer variable's address in the environment, snapshots its current value
    into a local alloca per invocation, and writes every assignment (plain and
    compound `+=`/`-=`/etc.) back through that address. The enclosing scope
    observes each mutation and later invocations start from the latest value
    (`test/lambda/test_closure_mutable_capture.vyb`).
  - *Move capture*: capturing a `my<T>` transfers ownership into the closure;
    the semantic analyzer records the outer variable as moved so reading it
    afterward is a use-after-move diagnostic (`test/lambda/test_closure_move_capture.vyb`).
  - *`our<T>` capture*: capturing a shared value bumps its strong count at
    closure creation so the value stays alive for the closure's lifetime. The
    heap-allocated environment is never freed (consistent with the surrounding
    heap patterns), so the count is intentionally not balanced by a closure-side
    release (`test/lambda/test_closure_our_capture.vyb`).
- **Function-typed parameters and indirect calls** — a parameter declared with a
  function type (`f<fn(Int) -> Int>`) is lowered to a function pointer stored in
  its alloca, and calling `f(args)` inside a body performs an indirect call
  through that pointer. Semantics now type a call whose callee is *any* symbol
  with a `FunctionType` (function-typed parameters and lambda-typed locals, not
  just declared `Function` symbols). Generic binds resolve `fn` parameter
  signatures under monomorphization so `bind<T> ... Vec<T>` methods can take
  element-typed `fn` arguments. Monomorphized generic bind bodies can now carry
  concrete `fn` params to codegen, unlocking higher-order combinators in the
  standard library.
- **`stdlib/collections` ships `VecHigherOps` `map` / `filter` / `reduce`** — a
  new unconstrained bind on the built-in `Vec<T>` (no `Comparable` /
  `Equatable` requirement) exposing the higher-order combinators: `map(f)`
  (elementwise transform to a fresh vector), `filter(f)` (keep only `true`
  results in a fresh vector) and `reduce(init, f)` (left fold, `acc = f(acc,
  elem)`). The `fn` arguments are *non-capturing* lambdas (bare function
  pointers); closures with an environment are a future extension. Covered by the
  expanded `test/modules/test_vec_expansion.vyb`.
- **By-ref `their<Vec<T>>` receivers power in-place Vec operations** — the
  semantic analyzer and codegen now unwrap ownership-wrapped Vec receivers
  (`their<Vec<T>>`, `my<Vec<T>>`, etc.), so a by-ref bind body can call the
  built-in Vec primitives (`len` / `get` / `set` / `pop`) directly on `self` and
  mutate through the borrowed pointer. On this foundation `stdlib/collections`
  adds the in-place forms to the existing binds: `sort_in_place()` on `VecOps`
  (Comparable-gated selection sort) and `map_in_place(f)` / `retain(f)` /
  `reverse_in_place()` on the unconstrained `VecHigherOps`. `retain` compacts
  in place and returns the surviving count. Covered by the expanded
  `test/modules/test_vec_expansion.vyb`.
- **`Option<T>` / `Result<T,E>` expose a native `.value` payload accessor** — a
  value of the built-in generic enums now supports `.value`, reading the payload
  of the primary (success) data variant (`Some(T)` for Option, `Ok(T)` for
  Result) as `T`. It compiles to a guarded extract of the tagged-union payload:
  when the runtime variant is the primary one the payload is returned, otherwise
  a default `T` is produced (matching the house "return default on invalid
  access" style of `Vec.get`). This lets `m.get(k).value` drain a `HashMap`
  lookup without an explicit `match`, while `match`/`select` remain the
  exhaustiveness-checked path. Covered by `test/units/test_enum_value_accessor.vyb` and
  `test/modules/test_collections_hashmap.vyb`.
- **`stdlib/collections` ships real `HashMap<K,V>` and `HashSet<K>`** — two
  keyed collections built on `Vec` plus the `Hashable`/`Equatable` core aspects,
  with in-place mutation via by-ref (`self<their<...>>`) bind receiver methods.
  `HashMap` provides `put`, `get` (returning `Option<V>`), `contains_key`, and
  `size`; `HashSet` provides `insert`, `contains`, and `size`.
  Covered by `test/modules/test_collections_hashmap.vyb`.
- **`collections` lookup is now hash-bucketed (chained) instead of O(n)** —
  `HashMap<K,V>` keeps parallel `keys`/`vals` vectors and adds a bucket index: a
  `head<Vec<Int>>` chain per hash and a per-key `next<Vec<Int>>` link, so a
  lookup only scans the single bucket the `Hashable` hash selects rather than
  the whole map. `HashSet<K>` uses the same bucket/chain index over its
  `values` vector. Buckets start at 16 and **auto-grow (doubling) with a full
  re-hash** once the load exceeds 2 keys per bucket, keeping average lookup
  O(1). Construct with the new `HashMap<K,V>()` / `HashSet<K>()` declared
  constructors, the pre-sizing `HashMap<K,V>(cap)` / `HashSet<K>(cap)` forms
  that skip early re-hash storms, or the **fixed-capacity** `HashMap<K,V>(cap, true)`
  / `HashSet<K>(cap, true)` forms that never re-hash or realloc — provably
  bounded memory for embedded targets. A fixed `put`/`insert` returns `false`
  when at capacity; `HashMap.put` now returns `Bool` (true on insert/update,
  false only when a fixed map is full and the key is new). The old
  `make_hash_map` / `make_hash_set` helpers are removed. Covered by
  `test/modules/test_collections_growth.vyb` and
  `test/modules/test_struct_constructors.vyb`.
- **Generic struct constructors** — `struct` declarations can now carry
  `constructor(...)` clauses inside the body (e.g. `HashMap<K,V>()`, `Vec(n)`-style
  capacity forms, or user types like `Pair<A>(x, y)`). Each constructor lowers to a
  synthetic generic function (`__ctor_<Struct>_<N>`) sharing the struct's type
  parameters, so `HashMap<K,V>(n)` dispatches through the same compile-time
  monomorphization pipeline as a generic function call. A bare `Type()` without
  explicit type arguments currently still parses as a function call, so this
  constructor syntax is intended for generic struct types.
- **Returning a struct that embeds a `Vec` no longer frees the Vec's data** —
  returning an object literal (or construction) that places an owning variable
  into a field, e.g. a constructor building `HashMap { head = d, ... }`, now
  transfers that binding's ownership to the caller instead of running its local
  cleanup and leaving the returned field pointing at freed memory. The return
  ownership-transfer walker now descends through `ObjectLiteral` /
  `ConstructionExpression` field initializers in addition to bare identifiers.

- **`Vec<T>` view helpers via `VecOps`** — `import collections` now binds a
  pure-Vyb `VecOps` aspect to the built-in `Vec<T>` (Comparable-gated, which
  refines `Equatable`) exposing non-mutating helpers written over the Vec
  primitives: `first`/`last` (head / tail element), `reversed` (a fresh
  reversed copy), `find` (index of the first `==` match, or `-1`), and the
  ordering helpers `sorted` (fresh ascending copy), `min`, and `max`. Ordering
  is dispatched through the `Comparable`-bounded `cmp_lt` helper (mirroring the
  `equals`-based `samekey`), since a direct `compare` call does not resolve on a
  generic element. `map`/`filter`/`reduce` await closure capture. Covered by
  `test/modules/test_vec_expansion.vyb`.

- **Scalar `Comparable` impls are complete** — `compare` is now pre-wired for
  `Float` (sign of difference), `Bool` (`false` < `true`), and `String`
  (lexicographic, byte-wise via `char_at`, length-tiebreak), in addition to the
  existing `Int` (`self - other`). This completes the documented core-scalar
  surface and is what makes `sorted` / `min` / `max` work on `Vec<Float>` and
  `Vec<String>`.

- **Stdlib File I/O** — `import io` now ships a real file module on top of the
  runtime's `__vyb_file_*` helpers (thin `vyb_io_*` intrinsics), replacing the
  placeholder: `File { fd<Int>, path<String> }`, `open(path, flags)`, the
  `open_read` / `open_write` / `open_append` conveniences, `close`,
  `write_str`, `read_all` (whole file into a `String`; empty on error), and the
  `error_code()` / `error_message()` diagnostic surface, plus portable
  `FILE_*` open-mode helpers. Because Vyb identifiers cannot start with `_`,
  the Vyb-facing names are `vyb_io_*`, mapped inside codegen to the runtime
  `__vyb_file_*` symbols; each runtime helper is `VYB_WEAK` and keeps the last
  error in a static `vyb_file_err`. `read_all` registers its buffer so the
  returned `String`'s release frees it. Covered by
  `test/modules/test_file_io.vyb` (write/reopen/read round-trip plus the
  missing-path error surface); leak-free under ASAN.

- **`Iterator` protocol (`core::iter`)** — the standard iteration contract:
  `aspect Iterator { type Item; next(self<their<Self>>)<Option<Self::Item>> }`.
  A type binds it (assigning the associated `type Item`) and implements `next`,
  which returns `Some(value)` per element and `None` when exhausted; the by-ref
  `their<Self>` receiver advances internal cursor state in place. Explicitly
  imported via `import core::iter` (deliberately not part of the auto-imported
  `core::aspects`, so the already-shipped associated-type tests that define a
  local `Iterator` aspect don't clash). Consumable today with an explicit
  `.next()` / `match` loop; the `for (item in it)` desugar over the protocol is
  the next compiler step. Protocol verified for `Int` and `Float` associated
  types in `test/modules/test_iterator_protocol.vyb`; leak-free under ASAN.

- **Nested `their<Vec<T>>` field method resolution** — a struct field typed
  `their<Vec<T>>` (a by-ref view of a `Vec`, e.g. an iterator holding the Vec by
  reference in `data<their<Vec<T>>>`) now resolves the built-in Vec methods.
  Previously method calls on such a field failed semantic analysis
  (`Method 'len' not found for type 'their<Vec<Int>>'`); the semantic pass now
  unwraps ownership-wrapped member-expression receivers (`their`/`my`/`our`/
  `view`/`borrow` around a `Vec`), and codegen derefs the single-pointer field
  slot (a `Vec**`) once to recover the `Vec*` before operating on it. Reads
  (`len`/`get`) and mutations (`set`/`push`) both reach the borrowed backing
  `Vec`. This unblocks iterator structs that hold a `Vec` by reference and lets a
  concrete `Iterator` bind drive `next()` over such a field. Covered by
  `test/modules/test_nested_their_vec_field.vyb`; leak-free under ASAN.

- **Generic-bind `Option<T>` construction & matching** — a generic bind method that
  returns `Option<T>` (or `Option<Self::Item>`) with a type-parameter payload now
  constructs and is matchable as the concrete enum. Two fixes: (1) semantic
  `match`/`select` on a generic-bind-returned `Option` materializes the concrete
  enum from its template (`materializeConcreteEnum`), so `Some(x)`/`None` variant
  patterns resolve — previously they failed with "Unknown type identifier: Some"
  because only the bind's loose `Option<T>` was registered, never the substituted
  `Option<Int>`; (2) codegen for bare `Some(x)`/`None` inside a monomorphized
  generic bind substitutes the active type params into the enum instantiation
  (mirroring the already-correct qualified `Option<T>::Some` path), instead of
  emitting an unresolved `Option_T`. Together they unblock a generic `VecIter<T>`.
- **`VecIter<T>` stdlib iterator (`import collections`)** — a cursor iterator over a
  `Vec<T>`: holds the Vec by reference (`data<their<Vec<T>>>`) and implements
  `core::iter`, with `next(self)<Option<Item>>` yielding `Some(v)` per element and
  `None` when done, advancing its own index through the by-ref receiver. Producer
  `v.iter()` is a `VecHigherOps` method on the built-in `Vec<T>` (generic over the
  element type, no `Comparable`/`Equatable` requirement). `Iterator` is re-exported
  from `collections` so `import collections` alone surfaces the bind. Consumed with
  an explicit `.next()` / `match` loop (for both `Int` and `String`) in
  `test/modules/test_vec_iter.vyb`; leak-free under ASAN.

### Changed
- **`FunctionType` grammar doc no longer contradicts the parser** — `vyb.hpp`
  now documents the real function-pointer type syntax `fn(params) -> Return`
  (the previously listed `(params)<Return> ->` form was never implemented in
  `type_parser.cpp`). The `fn` token is a *type* keyword only; it is unrelated
  to the removed `fn`/`func` function-declaration keywords.
- **C-like enums are now first-class typed values** — `enum Color { Red, Green,
  Blue }` no longer lowers each variant to a raw `i64`. Variants are distinct
  values of the named enum type (`r<Color> = Color::Red`), backed by a **single
  scalar `i64` tag** rather than a struct. That keeps C-like enums first-class
  while remaining ABI-compatible with a C integer-backed enum passed by value:
  an extern function parameter typed `Color` lowers to `i64`, and
  `labs(Color::Green)` → `1` across the FFI boundary (a `{ i64, [N x i8] }`
  struct would not interoperate). `println(v)` renders `Color::Red`, and
  `match` / `select` dispatch on the scalar tag by name with the same
  exhaustiveness enforcement as data enums (every variant or a wildcard).
  Structurally identical C-like enums are disambiguated by their concrete type
  name (LLVM deduplicates identical struct layouts); the raw tag remains
  available for explicit FFI use. Affected tests updated:
  `test/enum/test_import_c_like.vyb`, `test/new_features/test_enum_basic.vyb`,
  `test/new_features/test_enum_match.vyb`,
  `test/bindgen/test_libsample_bindings.vyb`; new coverage in
  `test/units/test_enum_clike_select.vyb`,
  `test/units/test_enum_clike_non_exhaustive.vyb`, and
  `test/ffi/enum_by_value.vyb`.

### Fixed
- **Unsigned integers print as unsigned** — `println_int`, `println`, and
  `"{}".format(...)` no longer sign-extend `UInt*` values, so a `UInt8` holding
  `250` prints `250` instead of `-6` (and a `UInt16` of `65535` prints `65535`,
  not `-1`). A new `__vyb_uint_to_string` runtime helper formats unsigned
  values; the codegen zero-extends them. Covered by
  `test/expressions/test_uint_print_int.vyb` and `test_uint_print_gen.vyb`.
- **String methods work on non-identifier receivers** — previously only a
  receiver bound to a named variable could call String methods (so
  `"hello ".to_upper()`, `full_name().substring(0, 5)`, or `p.name.split(",")`
  all failed with `Function ... not found` at codegen, and several, such as
  `contains`/`concat`/`len` on a literal, were misrouted to the Vec path and
  could crash). Every built-in String method now dispatches on a literal,
  call-result, or struct/array-field receiver by materializing the receiver's
  `{ ptr, len }` struct, and String-bound aspect methods (e.g. `.split()`)
  dispatch too. Covered by `test/string/test_str_method_on_value.vyb`.
- **Plain `StructName(...)` constructs correctly** — a non-generic struct
  built with bare parens (`Pt(1, 2)`, or `Pt()` for defaults) no longer parses as
  a function call. Previously any `Ident(...)` was treated as a call, so a plain
  struct constructor reached codegen as `Function Pt not found.`, yielded a null
  value, and default-constructed (`Pt()`) fields were left as uninitialized stack
  memory (reading one in a branch crashed the JIT). The parser now recognizes
  user-declared struct/class/type-alias names and emits a `ConstructionExpression`,
  and construction zero-initializes fields before filling them positionally.
  Covered by `test/new_features/test_struct_plain_construct.vyb` and
  `test/modules/test_chan_nonident.vyb`.
- **`Vec(n)` no longer emits untrackable `malloc`/`memset` symbols** —
  `emitVecConstructor` previously called `llvm::Function::Create` for a fresh
  `ExternalLinkage` `malloc`/`memset` on *every* `Vec(n)` (pre-sized) call. With
  more than one such allocation in a module, LLVM auto-suffixed each duplicate
  (`malloc.N` / `memset.N`), which the ORC JIT could not resolve at runtime
  (`Symbols not found: [ memset.21 ]`), so pre-sized vectors across
  collections/core now JIT-run correctly. It now reuses the single module-level
  declarations via `getOrCreateMallocFunction`/`getOrCreateMemsetFunction`,
  matching the Vec push/resize paths. This also clears the previously-failing
  Vec-constructor and multi-vector JIT runs.
- **Generic structs with mixed-field (Vec + scalar/Bool) layouts resolve
  correctly through by-ref receivers** — `LLVMCodegen::codegenType` memoized
  types by the raw `TypeNode*`, but transient substitution clones (created while
  monomorphizing a struct's fields) were freed and their heap addresses reused,
  so a later field could hit a stale cache entry and resolve to the wrong type
  (e.g. a trailing `Bool` field laying out as a `Vec`, crashing with an LLVM
  `ICmpInst` operand-type assertion). The cache now keys by the node pointer
  **and** validates the stored `toString()` against the current node, treating a
  stale entry at a reused address as a miss while still keeping distinct
  same-string nodes separate (context-sensitive, e.g. `Self` in trait binds).
  Covered by `test/modules/test_byref_mixed_fields.vyb`.
- **String-producing calls are wrapped when passed as arguments** — an inline
  `t.to_string()` (or `substring`/`concat` results) lowers to a raw `char*`, but
  call-argument marshalling only unwrapped String structs into char pointers, not
  the reverse. Passing a computed string directly to a normal function
  (`say(t.to_string())`) or to a bind/aspect method (`map.put(k.to_string(), v)`)
  failed with "Argument type mismatch: Expected { ptr, i64 } but got ptr". Both
  call paths now wrap a raw pointer into a Vyb `String { ptr, len }` (with a
  `strlen`-computed length) when the declared parameter is a String struct.
  Covered by `test/units/test_string_call_args.vyb`.
- **`if { return }` no longer over-pops the codegen scope stack** — a block
  whose body terminates via `return` popped twice: the return's cleanup already
  ran `exitScope()`, then the enclosing block popped once more, so each
  `if { return }` silently dropped the next outer scope. After two such ifs a
  later declaration could run with an empty scope (`No active scope to register
  variable`). Blocks now restore only the scopes they introduced (a depth
  captured before their own `enterScope`), and a `return` cleans every scope its
  function introduced (function body and parameters) before emitting its
  terminator — so an early return out of `while { if { return } }` no longer
  leaves the loop-exit block terminated and then re-cleaned into that same block —
  while restoring the stack afterwards so declarations after an `if { return }`
  keep their scopes. Covered by the enum-accessor/loop suites and a
  `if { return }`-then-declare repro.
- **`import prelude` no longer drops the internal `hash_chars` helper** — the
  `Hashable -> Float` bind body calls `hash_chars`, but it was only
  `share(all)` inside `core::aspects` and not re-exported, so pulling prelude in
  left the carried bind referencing an undefined identifier. It is now re-exported
  through `core::prelude` and the top-level `prelude` (restores
  `test/modules/stdlib_autodiscovery.vyb`).
- **Monomorphized bind methods no longer corrupt the caller's scope stack** — a
  by-ref bind method whose body returns from inside a loop (common in collection
  methods like `HashMap.get`) could over-pop the shared codegen scope stack while
  it was emitted lazily inside the caller, leaving a subsequent variable
  declaration with "no active scope" and skipping its ownership cleanup. Bind
  method emission now isolates scope tracking the same way monomorphized
  functions already did (save/clear/restore) instead of relying on a depth guard
  that cannot recover from an underflow below the caller's baseline.
- **Cross-module generic binds with nested-bound type parameters are carried** —
  `bindKeyFromLine` skipped generic parameters using the *first* `'>'`, which
  broke on a bound that itself wraps a generic (e.g.
  `bind<K<Hashable, Equatable>, V> MapOps -> HashMap<K, V>`), so those binds ran
  as if un-`share`d and were silently dropped on import. It now skips balanced
  angle brackets, so the standard-library `HashMap`/`HashSet` binds (and any
  similar user bind) retain visibility across `import`.
- **By-ref bind receivers now work — `self<their<T>>` mutates in place** — a bind
  method whose receiver is declared by reference lowers `self` to a pointer to the
  caller's object, and the call site passes the receiver's address instead of a
  by-value copy. Previously the call site always passed a by-value load, which
  mismatched the generated pointer `self` (`Call parameter type does not match
  function signature!` → segfault), and generic binds never produced a pointer
  `self` at all. Now in-place mutations to `self` (e.g. pushing into a Vec field)
  persist on the caller, for both concrete binds and generic bound binds like a
  `Map<K,V>` `put`/`size` pair. The by-ref machinery rides the existing ownership
  receiver types: `their<T>` (borrow, no cleanup) is the natural mutable receiver;
  `my<T>` remains a move/ownership-taking receiver whose scope-exit cleanup would
  free a collection's data. Covered by
  `test/aspect/test_bind_by_ref_receiver.vyb`.
- **Struct field access substitutes the receiver's concrete generic args** — a
  bare field read like `self.keys` now types against the receiver's actual generic
  arguments instead of the raw struct-template types. Inside a generic bind (or
  any concretely-typed generic value), `self.keys` on a `Map<Int, Int>` receiver
  types as `Vec<Int>` rather than `Vec<K>`, so it can be assigned to a typed local
  and `.len()` reads the correct size field; and a bounded bind's own type
  parameter now resolves a struct payload field (`self.v` on `Holder<K>` types as
  `K`), so the payload can be passed to a bounded helper like `hashit(self.v)`.
  Covered by `test/units/test_bind_self_member_resolution.vyb`.
- **Explicitly-typed `Vec` constructor `Vec<T>()` / `Vec<T>(n)` now works** — a
  typed construction (`Vec<Int>()`) parses `Vec<Int>` as a type name, so it was
  routed through generic struct-construction codegen, which returned a pointer to
  an uninitialized alloca instead of the `{ ptr, size, cap }` struct *value*.
  That broke the assignment cast (`Unsupported or invalid cast from type ptr to
  { ptr, i64, i64 }`) and segfaulted when used on a struct field. Typed Vec
  constructions now route through the same emitter as bare `Vec()` / `Vec(n)`, so
  `Vec<Int>()`, `Vec<Int>(n)`, and `Vec<String>()` all build a real empty /
  pre-allocated vector in a standalone variable or a struct field. Covered by
  `test/units/test_vec_typed_constructor.vyb`.
- **Bounded type parameters propagate into inner generic calls** — a generic
  function whose declared type parameter carries its own bound (e.g.
  `hashit<K<Hashable>>`) can now forward that parameter by value into another
  bounded generic helper (`both<K<Hashable>, L<Hashable>>` → `hashit(a)`).
  Bounds validation recognizes that a bounded type parameter itself satisfies
  the helper's requirement (no spurious "Type 'K' does not satisfy Hashable"),
  and at the call site the concrete type substitutions are threaded into the
  inner helper's type arguments so it monomorphizes against the concrete type
  (`both("alpha", "beta")` drives `hashit_String`, not `hashit_K`). Covered by
  `test/units/test_bounded_param_nested_dispatch.vyb`.
- **Generic functions returning generic struct/enum types monomorphize to the
  concrete type** — a call like `make_box<Int>(7)` (or the inferred
  `make_box("hi")`) now resolves its return type to `Box<Int>`/`Box<String>`
  instead of an unresolved `Box<T>`. Explicit type arguments are honored when
  resolving the return type, and a `ConstructionExpression` written with explicit
  generic args (`make_box<Int>(7)`) is dispatched through the generic-call path
  (mirroring LLVM codegen). Two distinct instantiations in one program (e.g.
  `Box<Int>` and `Box<String>` side by side) no longer collide: the LLVM type
  cache is bypassed while generic-function substitutions are active so shared
  template AST nodes resolve per-instantiation, and struct/enum monomorphization
  applies substitutions only when one is active so resolved type metadata is
  preserved. Covered by `test/units/test_generic_struct_return.vyb`.
- **Printing a payload enum no longer crashes** — a tagged-union enum (a
  2-element struct `{ i64 tag, [N x i8] data }`) was mistaken for a Vyb `String`
  `{ ptr, len }` during string conversion, extracting the tag as a `char*` and
  failing module verification with a segfault. String-struct detection now
  requires a pointer first field, and enum values render as readable
  `Enum::Variant(payload)` strings (covered by `test/enum/test_print_enum.vyb`).

### Added
- **`Hashable.hash` for `String`, `Float`, and `Bool`** — `core::aspects`
  now ships `hash(self)<Int>` for all four core scalar types (Int already had it),
  so any `Hashable`-bounded generic code can hash a `String`/`Float`/`Bool` key.
  `String` (and `Float`, via its decimal rendering) use a DJB2-style hash over the
  character codes; `Bool` hashes to `1`/`0`. Covered by
  `test/units/test_hashable_primitives.vyb`.

 — every enum value (C-like scalar and
  data-carrying tagged-union alike) now exposes its raw positional variant tag
  as an `Int` via `.tag`. C-like enums return the backing scalar directly; data
  enums extract field 0 of the `{ i64 tag, [N x i8] data }` struct at runtime.
  The accessor participates in normal expressions (`match`/`select`/arithmetic)
  and is covered by `test/units/test_enum_tag.vyb`.

- **`vyb bindgen` (MVP)** — new `vyb bindgen <header.h> [-o out.vyb]` subcommand that parses a C header subset (typedefs, `struct`/`enum` declarations, scalar/pointer types, trailing `...`) and emits an importable Vyb module: `share(all)` bodyless extern functions (which lower to ExternalLinkage forward declarations and resolve against the host C ABI), `#[repr(C)]` structs, and enums. Functions, structs, and enums re-export across `import` (including C-like enums as typed values once the language change landed). Covered by `test/bindgen/libsample.h`, `test/bindgen/libsample.vyb`, and `test/bindgen/test_libsample_bindings.vyb`. Full libclang/preprocessor-based parsing remains future work (see TODO FFI).

- **Variadic C functions** — a trailing `...` in an extern C parameter list now marks the declaration as variadic. Codegen emits a true LLVM vararg function, and call sites accept any number of extra arguments beyond the fixed parameters (rejecting calls with too few). Vyb `String` varargs auto-extract their `char*` data pointer so C varargs such as `printf("%s", s)` receive a C string; `printf("%d-%s", 7, "vyb")` is covered by `test/ffi/variadic_c_printf.vyb`. Variadic functions must be extern/forward declarations (a body is rejected).

- **`core::math` stdlib module** — new `stdlib/core/math.vyb` with composition
  helpers over the global math intrinsics: `clamp(value<Int>, lo<Int>, hi<Int>)<Int>`
  (inclusive range clamping via `min`/`max`) and `is_close(a<Float>, b<Float>,
  epsilon<Float>)<Bool>` (within-epsilon comparison via `abs`). Math and output
  functionality already ships as global `sqrt`/`abs`/`println`/`print`-style
  intrinsics and the built-in `Vec<T>`; the `io` and `collections` module surfaces
  are now documented accurately and remain importable (`import io::{..}` /
  `import collections::{..}`) while their deeper contents (file/network I/O,
  `HashMap`/`HashSet`, higher-order `Vec<T>` expansion, and the `Iterator` aspect)
  track their separate compiler-feature items. See
  `test/modules/stdlib_core_math.vyb` and `test/modules/stdlib_io_collections_import.vyb`.

- **Auto-import of core contracts (`core::*`)** — every non-stdlib module now
  automatically gets the core contracts module (`core::aspects`, including its
  pre-wired scalar binds) in scope, so `x.display()`, `a.equals(b)`,
  `a.compare(b)`, and `a.clone()` work on built-in scalars with no explicit
  import. Auto-import is skipped when the module already imports the contracts
  (directly or via `prelude`/`core::prelude`), when it locally redefines any core
  aspect or primitive bind, and when it opts out with the `no_core()` directive.
  Stdlib modules wire their own imports and are excluded from auto-import.

- **Binds carry across module imports** — `bind` declarations in an imported
  module are now carried to the importer (like shared structs/aspects/functions),
  so a library can ship aspect implementations that take effect on import. Binds
  are tracked by an immutable `(target, aspect)` key, respect `share(...)`, and
  are included for whole-module imports or for the requested specifier aspects
  without consuming the aspect's requested-name slot. Visibility requires a
  `share(all)` (or bundle) on the bind, matching the existing share model.

- **Pre-wired core aspects for built-in scalars** — `core::aspects` now ships
  `Display`/`Clone`/`Equatable`/`Comparable`/`Hashable` implementations for
  `Int`, `Float`, `Bool`, and `String` (Display/Equatable; Clone for Int/String;
  Comparable + Hashable for Int). Because binds carry across imports,
  `import core::aspects` (or `import core::prelude`) makes `x.display()`,
  `a.equals(b)`, `a.compare(b)`, and `a.clone()` work on those types with no
  user-authored binds. `core::prelude`'s `core::aspects` re-export was fixed to
  `share(all)` so the aspects and their binds re-export correctly.

- **Aspect binds to primitive scalar targets** — a `bind` may now target a
  primitive scalar type (`Int`, `Float`, `Bool`, `Char`, sized ints) in addition
  to structs. Primitive-bound methods dispatch unqualified (`v.display()`,
  `a.equals(b)`, real `self` arithmetic like `self * 2`) and through generic
  bounds (`<T<Display>>`, `<T<Equatable>>`), monomorphizing per concrete type.
  Codegen previously rejected any non-struct impl target (`Target type for impl
  block is not a known struct/class type`); it now accepts resolved integer and
  floating-point types, leaving `currentClassType` null and resolving `Self` via
  the impl target type node.

- **Core aspects stdlib contracts** — a new `core::aspects` stdlib module declares
  the six canonical polymorphic contracts (`Display`, `Debug`, `Clone`, `Equatable`,
  `Hashable`, `Comparable`), with `Comparable : Equatable` as a super-aspect. The
  aspects are `share(all)`-exported and re-exposed through `core::prelude` /
  the root `prelude`, so a type can `bind` them and dispatch unqualified
  (`thing.display()`, `a.equals(b)`, `a.compare(b)`, `a.clone()`), and generic
  functions can constrain with core-aspect bounds (`<T<Display>>`, `<T<Comparable>>`).
  Built-in primitive bindings (binds targeting `Int`/`String`/`Float`/`Bool`/`Char`)
  are a follow-on (binds to primitives as impl targets are not yet supported).

- **Unqualified method dispatch on bounded type parameters** — inside a generic
  function, an unqualified dot call like `thing.show()` on a receiver typed as a
  bounded type parameter (`thing<T<Display>>` / `thing<T<Named>>`) now resolves
  the method through the bound aspect and substitutes `Self` in the return type,
  without needing the qualified `Display::show(thing)` form. Method resolution
  also walks the bound aspect's transitive super-aspect chain, so an inherited
  method from a super-aspect (e.g. `thing.name()` where `Display : Named` and
  `name` lives on `Named`) is dispatchable in both the unqualified and qualified
  (`Named::name(thing)`) paths. The qualified `Aspect::method(receiver)` path now
  accepts any bound whose aspect/inheritance chain provides the requested aspect,
  and runtime dispatch still monomorphizes to the concrete type's bind method.

- A startup **Type registry** now registers every compile-time-known type name
  (primitives and user structs) keyed by its type-ID hash, so `typename(t)` on a
  runtime `Type` value (`t<Type> = typeof(42)`) resolves the actual type name at
  runtime (e.g. `"Int"`, `"ParseError"`) instead of the static `Type` label. The
  registry is populated from `main` (reliable in the JIT): `__vyb_module_init()`
  calls `__vyb_register_typename(id, name)` and `typename(t)` looks it up via
  `__vyb_get_typename(id)`.

- `Type` is now a first-class type identity: `t<Type> = typeof(42)` (and the
  compile-time `typeof<Int>()` form) declares/assigns an opaque 8-byte type ID,
  `==` / `!=` compare type IDs, `Type` values flow through function calls, and
  any other operator on a `Type` value (e.g. `+`) is a semantic error — only
  `==` / `!=` are supported, per the introspection design.

- A `trap (e<Type1 | Type2>)` multi-type union handler now catches an error of
  any listed type and binds `e` as an opaque error pointer, so the handler
  resolves the concrete payload via the safe-downcasting / introspection
  operators it already uses in wildcard traps: `g<ParseError> = e as ParseError`,
  `typeof(e) == typeof<ParseError>()`, `typename(e)`. Previously the multi-type
  form parsed and matched but the handler had no typed access to `e` (only
  Bool/Int handlers round-tripped); chained `} trap (e<Type>)` clauses already
  dispatch first-type-compatible-wins and keep working.

- `Option<T>` is now a first-class built-in generic data enum
  (`enum Option<T> { Some(T), None }`), so nullable values no longer require the
  transitional `core::option::OptionInt` bridge (kept for source-compat). It
  supports qualified construction (`Option<Int>::Some(42)`, `Option<Int>::None`)
  and type-inferred bare construction (`Some(x)` / `None` when the enclosing
  variable declaration or function return type is `Option<T>`). It is registered
  directly in the compiler (semantic generic-enum template plus a codegen
  tagged-union layout, monomorphized per payload type), so it needs no `import`.
  It integrates fully with `match`/`select` variant dispatch and exhaustiveness
  checking, unwrapping both primitive and heap (e.g. `String`) payloads.

- `Vec::new()` / `Vec::new(n)` are superseded by a vybish constructor form:
  `Vec()` builds an empty growable vector and `Vec(n)` builds an n-element,
  zero-initialized vector, both with the element type inferred from the variable
  annotation (`v<Vec<String>> = Vec()`). The legacy `Vec::new()` / `Vec::new(n)`
  forms remain as a back-compat alias so existing code keeps compiling. Tests,
  examples, demos, and the README now use the new idiom.

- `Result<T, E>` is now a first-class built-in generic data enum
  (`enum Result<T, E> { Ok(T), Err(E) }`), for fallible operations. It is
  registered directly in the compiler (semantic generic-enum template plus a
  codegen tagged-union layout, monomorphized per payload type), so it needs no
  `import`. It supports qualified construction
  (`Result<Int, String>::Ok(42)` / `Result<Int, String>::Err("boom")) and
  type-inferred bare construction (`Ok(x)` / `Err(e)` when the enclosing
  variable declaration or function return type is `Result<T, E>`). It integrates
  fully with `match`/`select` variant dispatch and exhaustiveness checking,
  unwrapping both primitive and heap (e.g. `String`) payloads in either the
  `Ok` or `Err` position.

- Enum methods via `bind` on enum target types: an aspect `bind` can now target a
  user-defined enum (concrete or generic, e.g. `bind Render -> Box<Int>`) and the
  built-in generic enums `Option<T>` / `Result<T,E>` — the semantic gate no longer
  rejects enum instantiations as unknown types, and methods dispatch on the
  concrete variant with the substituted payload (e.g. `area(self)<Float>` on
  `bind HasArea -> Shape { ... }`, matched over `Circle(r)` / `Rect(w,h)`).

- Bare `Some(x)` / `None` / `Ok(e)` / `Err(e)` now work as arbitrary call
  arguments via expected-type propagation, not only in annotated variable
  declarations and returns. The parameter's `Option<T>` / `Result<T,E>` type (or,
  for `v.push(x)`, the `Vec<T>` element type) is injected into the matching bare
  constructor before it is resolved, enabling `unwrap(Some(7))`,
  `classify(Ok(11))`, and `v.push(Some(x))` on a `Vec<Option<Int>>`.
- `as` safe-downcasting operator (`value as TargetType`), Phase 2 of the
  introspection system. It is lexed/parsed as an infix expression (new
  `AsExpression` AST node + `parse_cast_expr`), typed as the target type, and
  code-generated as a safe downcast. In a wildcard trap (`e<?>`) it extracts the
  concrete payload from the error struct so the handler can read its fields
  (`g<GErr> = e as GErr` then `g.code`), and same-type casts pass through
  (`x as Int`); incompatible static casts are a semantic error.
- `typeof(e)` / `typename(e)` now work on wildcard trap errors (`e<?>`):
  `typeof(e)` loads the error's runtime type ID and `typename(e)` its type-name
  string from the error struct, so handlers can discriminate failed errors by
  type (`trap (e<?>) -> { typeof(e) == typeof<ParseError>() }`). Adds the
  `typeof<T>()` compile-time type-hash form (e.g. `typeof<GErr>()`), and
  `typeof` / `typename` are recognized as expression-statement starts.

### Fixed
- A `trap` block used as a value (`s<String> = { risky() } trap (e<?>) -> { "hello" }`)
  now sizes its merge result slot from the handler's inferred result type, and the
  semantic analyzer stamps that type on the block expression. Aggregates like
  `String` are stored/loaded as a `{ ptr, len }` struct instead of a hardcoded
  `i64`, so a String handler round-trips correctly (`==` and `.len()`). Previously
  only Bool/Int trap handlers worked; a String handler resolved to the body's
  last-expression type (`i64`) and was rejected as "initializer type i64 is not
  assignable to String".
- A `match`/`select` arm whose value is a primitive `.to_string()` now stores a
  proper `{ ptr, len }` String into the expression's result slot (wrapping the
  raw `char*` with a `strlen`-computed length). Previously the raw pointer was
  stored and the length field stayed zero, so a String produced this way printed
  correctly but `.len()` returned 0 and `==` against a literal compared unequal.
  Affects `match` and `select` returning a String in this pattern.
- `select` now supports data-carrying enum variants directly: arms like
  `Circle(r) ->`, `Rect(w, h) ->`, and `Unit ->` dispatch on the runtime tag and
  bind payload fields as arm-scoped locals, matching the `match` behavior.
  `select` on a tagged-union enum also enforces exhaustiveness — it must cover
  every variant or include a wildcard — and reports the missing variant(s)
  otherwise. (Previously `select` could only compare literals/comparisons, and
  an enum variant was mis-parsed or unresolved.)
- Exhaustiveness now accounts for guard clauses on enum variant arms: a variant
  covered only by a guarded arm does not count as unconditionally covered, since
  the guard can be false and leave a no-match path. A match is exhaustive only
  when every variant has an unguarded arm (or a guarded arm plus a separate
  unguarded duplicate, or a wildcard). Codegen and the semantic check stay in
  sync on this rule.
- A `match` on a tagged-union enum that is not exhaustive — no unguarded
  wildcard and not covering every variant — is now rejected by the semantic
  analyser with a diagnostic listing the missing variant(s). (Previously the
  codegen treated only the exhaustive case as sound; a missing variant now fails
  to compile instead of reaching an impossible default.)
- A `match` on a tagged-union enum whose arms cover every variant is now
  recognized as exhaustive: its no-match default block is marked `unreachable`,
  so a non-void function whose final statement is such an exhaustive all-return
  `match` compiles cleanly instead of raising a spurious "may not return on all
  paths" diagnostic.
- A non-void function whose last statement is a `match` whose arms all `return`
  (with no trailing `return` after the match) no longer leaves an unterminated
  basic block that trips the LLVM verifier. The fall-through block is now
  terminated with `unreachable`, so compilation produces valid IR and reports a
  clean "may not return on all paths" diagnostic instead of crashing. Arms that
  return via a final wildcard (`? -> ...`) define a complete function and run
  cleanly.
- `fail` inside a callee (e.g. an `if`/`else` branch or an `ensure cond else
  fail<...>(...)`) is now trapped correctly by the caller. Trap contexts were a
  shared, non-function-local stack, so a `fail` in a callee could branch into the
  caller's trap landing pad and store into the caller's error slot, producing
  invalid IR and a crash. Trap contexts are now scoped per function, so a `fail`
  in a callee propagates through the failable ABI instead.
- Monomorphized generic function bodies now isolate trap context and scope
  tracking from the caller. Previously a generic callee that returned from an
  `else` branch could leave the caller's scope stack empty (spurious
  "No active scope to register variable" warnings) and a `fail` inside a generic
  callee could escape into the caller's trap context.
- Failable generic functions now use the same `{T, i8*}` error-return ABI as
  normal functions: a monomorphized body's `fail` returns the error through the
  failable ABI, and the call site detects the error and routes it to the
  caller's `trap` at runtime (or errors as an untrapped failure). Previously a
  generic `fail` was compiled as if non-failable, so it surfaced as an
  untrapped failure even when the caller trapped it.

### Added
- **Generic data enums** — `enum Box<T> { Value(T), Empty }` now builds a
  value-semantics tagged-union struct (`{ i64 tag, [N x i8] data }`) per concrete
  instantiation, constructed via explicit type arguments (`Box<Int>::Value(42)`,
  `Box<Int>::Empty`). The payload type is substituted for the type parameter
  (e.g. `Value` carries `Int` in `Box<Int>`, `String` in `Box<String>`), and
  `match`/`select` dispatch on the variant and bind the substituted payload, with
  the same exhaustiveness checks as non-generic enums.
- Tagged-union (data-carrying) enums and enum variant patterns in `match`:
  `enum Shape { Circle(Float), Rect(Float, Float), Unit }` now compiles to a
  value-semantics `{ i64 tag, [N x i8] data }` union, constructs via
  `Shape::Circle(x)` / `Shape::Rect(a, b)`, and matches on variants
  (`Circle(r) ->`, `Rect(w, h) ->`, `Unit ->`) by comparing the runtime tag and
  binding payload fields. C-like integer enums are unchanged. Generic data
  enums and `select` variant destructuring are deferred.
- Generic function calls now accept explicit type arguments, e.g.
  `probe<Int>(0, 0)`. Previously `name<Type>(...)` was mis-parsed as a variable
  declaration (`name` of type `Type`) followed by a bare `( ... )` sequence, so
  such a call silently compiled to a no-op instead of invoking the function.
  Explicit type args now flow into generic monomorphization (they are used
  directly rather than inferred, which also supports zero-argument generic
  calls), and the call uses the same failable `{T, i8*}` ABI as inferred
  generic calls so a `fail` inside is still caught by the caller's `trap`.
- `ensure` contract statements: `ensure cond else handling` runs `handling`
  whenever `cond` is false. It desugars to `if (cond) { } else { handling }`
  and so plugs directly into the `fail`/`trap` error system. The handling may
  be a block or a single statement (`return`, `fail<Error>(...)`, etc.), and
  may be followed by more statements in the same body. Also fixed `return`
  parsing so a `return` that is not the final statement in a block (e.g.
  `ensure x > 0 else return -1` followed by more code) terminates correctly.
- `match` as a value-returning expression: `r<Int> = match (v) {
  1..3 -> 10, 4..6 -> 20, ? -> 30 }` stores the matched arm's value into
  the variable. The result type is inferred from the first arm's yielded
  value and codegen shares a zero-initialized result slot across all arms.
  Naked-expression arms (including ranges, guards, and struct destructuring)
  yield their value directly, and block arms yield via `pass` (`2 -> {
  pass 20 }`), mirroring `select`. Mixed naked/block arms share the same
  slot. A plain statement-position `match` still produces no value.
  Codegen unifies the select/match yield contexts so `pass` resolves to the
  innermost enclosing value-yielding expression.
- Guard clauses in `match` arms: `pattern if condition ->` only runs the arm
  when the pattern matches AND the condition is true. The guard runs after the
  pattern matches (so it can read destructured struct fields); a false guard
  falls through to the next arm or the default. A guarded wildcard is treated as
  non-exhaustive, so later arms (and the no-match fall-through) remain reachable.
- Inclusive range patterns in `match` arms: `1..10 ->` matches a value within
  `[start, end]` (integer or float). A range whose start is greater than its end
  is statically rejected as never-matchable.
- Struct destructuring in `match` arms: a `Point { x, y }` pattern binds each
  listed field as a local variable in the arm body (extracted from the matched
  struct value). Field names are validated against the struct, and a struct
  pattern that can never match the match expression's static type
  (e.g. `Int` vs `Point { ... }`) is rejected at semantic analysis. Also fixed
  typed-struct-literal detection in `parse_primary` so `Type { ... }` is
  recognized even when a leading newline separates it from a preceding token
  (the match-arm case).
- A bind method whose return type is `Self::Item` inside the bind body now
  resolves the associated type in both concrete and generic binds. For a
  concrete bind (`bind Iterator -> CounterIter { type Item = Int; next(self)<Self::Item> }`)
  the return type resolves to the assigned type, and for a generic bind
  (`bind<T> Iterator -> Boxer<T> { type Item = T; next(self)<Self::Item> }`) the
  type parameter is substituted with the concrete type argument at the call site
  (e.g. `Boxer<Int>.next()` returns `Int`). The impl context is established before
  the monomorphized method signature is built so parameter and return types
  resolve against the specialized type and its associated-type bindings.
- Qualified aspect-method disambiguation: `Aspect::method(receiver, ...)` now
  selects a specific aspect whenever multiple bound aspects declare the same
  method name for a type (e.g. `DisplayA::show(thing)` vs `DisplayB::show(thing)`).
  Unqualified ambiguous dot-calls (`thing.show()`) remain rejected. To support
  this, bind-method symbols are emitted per `Type_Trait_Method` so distinct
  implementations coexist in the same module.
- Qualified aspect calls also work on bounded type parameters inside generic
  functions: `Aspect::method(thing)` where `thing<T<Aspect>>` resolves the return
  type from the bound aspect's signature and dispatches to the correct concrete
  bind for each instantiation.
- Default associated types: an aspect may declare `type Item = Int`, and a bind
  for that aspect can omit the explicit `type Item = ...` assignment and inherit
  the default. An explicit assignment still takes precedence. A missing assignment
  without a declared default remains a semantic error.
- Associated-type aspect bounds (`type Item<Display>` / `type Item: Display + Clone`):
  the type assigned to (or defaulted for) an associated type must implement every
  constrained aspect. Non-conforming assignments and bounds naming undefined
  aspects are rejected at bind validation.
- Associated types through generic binds: a generic bind such as
  `bind<T> Iterator -> Boxer<T> { type Item = T; next(self)<T> }` now resolves
  the associated type (and a type-parameter return type) to the concrete type at
  the call site, so `Boxer<Int>.next()` types and runs as `Int`. Generic bind
  method return types that reference a type parameter are substituted with the
  concrete type argument during semantic typing, and `Self::Item` in a bind
  method's signature is resolved symmetrically during signature matching.
- Aspect inheritance (super-aspects): `aspect Comparable : Equatable` declares a
  super-aspect. Super-aspect names are validated against defined aspects, cyclic
  super-aspect dependencies are rejected, and binding a sub-aspect requires the
  same type to also bind each super-aspect (checked in an order-independent pass
  after all binds are registered).
- Primitive ownership unwrap-on-read and move tracking:
  - Reading a `my<T>` / `our<T>` / `mild<T>` primitive now unwraps to the underlying value.
  - Compile-time move tracking for `my<T>` bindings rejects use-after-move and
    records ownership transfer on assignment, initialization, and `my<T>` argument passing.
- Aspect binds to concrete generic instantiations:
  - `bind Display -> Box<Int>` (and similar shapes) now resolve and monomorphize into executable methods.
- Error propagation Phases 3–5 for `fail`/`trap`:
  - `fail` without in-scope trap now returns the failable ABI tuple and propagates to caller.
  - Call sites of failable functions now auto-check `{value,error}` and propagate on non-null error.
  - New trap tests for propagation, defer-on-fail, untrapped main propagation, and non-failable caller rejection.
- Milestone gate coverage for current error propagation:
  - `test/error_trap/phase2` is now part of the required gate.
  - Focused trap fixtures now cover propagated calls, failable `main`, defer cleanup on propagated fail, and non-failable caller rejection.
- `Vec::pop()` now returns the removed primitive value instead of a placeholder and safely returns the default value for empty `Vec<Int>`.
- Minimal `our<T>` / `mild<T>` control-block runtime:
  - `our(expr)` allocates a payload plus strong/weak/released metadata.
  - `soft(ourValue)` creates a `mild<T>` handle by incrementing weak_count.
  - `mild<T>.released()` now observes release after the local strong owner is dropped.
  - `mild<T>.grab()` upgrades live weak handles to `our<T>` and returns a null `our<T>` placeholder for released targets until `Option<T>` exists.

### Changed
- Generic function calls now infer type arguments from the call-site arguments,
  substitute them into the return type, and validate declared aspect bounds —
  a concrete instantiation whose type does not bind the bound aspect is rejected
  with a clean diagnostic. Previously callers received the raw placeholder type
  (e.g. `T`) and unsatisfied bounds silently passed.
- Fixed a generic-function monomorphization scope imbalance: calling a second,
  distinct generic function in one body popped the caller's codegen scope, causing
  `ERROR: No active scope to register variable` for the second result. The function
  scope is now balanced only when the monomorphized body falls through.
- Bind selection precedence: when both a bounded and an unbounded generic aspect
  bind match the same type shape (`bind<T<Aspect>>` + `bind<T>`), the bounded
  (more specialized) bind now wins deterministically regardless of declaration
  order. Previously the registry keyed generic binds by pattern only, so the
  duplicate overwrote itself in last-declared-wins order.
- Ownership transfer-on-return now covers expression returns: an owning value
  (Vec with malloc'd data, `our<T>`, `mild<T>`) returned through a `select` expression
  (or any whole-value read) transfers to the caller instead of being freed first,
  fixing a `free(): double free detected` crash. Added an `@category: ownership, vec`
  regression test.
- Generic-function monomorphization hardens: caller IR insertion point is restored after
  monomorphizing a generic function, and call-frame push/pop stays balanced across monomorphized
  trait-method bindings (fixes `printItem_Point` "no terminator" crashes).
- Monomorphized trap-handler bodies get their own scope so `return` no longer pops the enclosing
  function scope (fixes `ERROR: No active scope to register variable`).
- Released `DIBuilder` during `releaseModule`/`releaseContext` so debug-metadata teardown no longer
  runs against a freed LLVM context — removes the flaky concurrent-run `SIGSEGV`.
- Runtime `__vyb_runtime_untrapped_error` now reports error type, JSON payload, fail source location, and honors `exitCode<Int>` payload fields.
- JIT `main` dispatch now checks failable-main error tuple returns and routes non-null errors to the untrapped runtime handler.
- Returning a local `our<T>` or `mild<T>` now transfers the handle to the caller instead of cleaning it up before return.
- `our<T>` member access now unwraps through the control block payload pointer before loading fields.
- Milestone minimum raised from 126 to 156 passing tests.

### Fixed

- **Integer assignment is explicit about width** — a variable, field, or
  reassignment no longer silently narrows between sized integer types. Assigning
  a typed value across different widths or signedness (`x<Int8> = wide<Int>`,
  `x<Int8> = smallU8`, `x<Int> = small<Int8>`) is now a compile error asking for
  an explicit `as`. A compile-time constant (literal or negated literal) still
  fits a sized type, but an out-of-range constant (`x<Int8> = 300`, previously a
  silent truncation to `44`) is now an error too. `Int`/`Int64` and C aliases of
  the same width/signedness remain interchangeable. Covered by
  `test/expressions/test_int_assign_policy.vyb`, `test_int_assign_range.vyb`,
  and `test_int_assign_implicit.vyb`.
- **Bitwise operators reject mismatched typed integer widths** — combining two
  typed integers of different widths with `|`/`&`/`^`/`<<`/`>>` (or their
  compound-assign forms `&=`/`|=`/`^=`/`<<=`/`>>=`) is now a compile error that
  asks for an explicit `as` cast. Previously a wide `Int` operand was silently
  truncated to the other operand's width (`x<Int8> | big<Int>` returned `3` not
  `259`). Bare integer literals still adapt to the typed operand's width, and
  compound assigns on narrow types now coerce a literal RHS correctly
  (`x<Int8> |= 8` no longer crashes with invalid IR). Covered by
  `test/expressions/test_bitwise_widths.vyb` and `test_bitwise_mismatch.vyb`.

## [0.5.0] - 2026-02-24 (freedom-1.0 series)

### Added
- **C-like Enum codegen**: Enum variants now compile to sequential `i64` integer constants.
  - `enum Direction { North, South, East, West }` declares 4 constants (0, 1, 2, 3)
  - Variant access via `Direction::North` syntax works in all expression contexts
  - Enum variant values integrate seamlessly with `match` and comparison operators
  - Semantic analysis recognizes enum type names; no false "undefined identifier" errors
  - Future: tagged unions with associated data (`Circle(Float)`) planned for v0.6

### Improved
- **Silent compiler by default**: Optimization-pass progress messages (`"Applying IR optimization passes"`, `"Skipping IR optimization"`, etc.) are now gated behind `--debug-codegen`; the compiler is quiet during normal use
- **CMakeLists.txt version**: Project version updated from `0.3.5` → `0.5.0` to match the language's actual state

### Status
Vyb v0.5.0 delivers a complete systems programming language with LLVM backend, native code generation, generics, aspect/bind polymorphism, pattern matching, defer, error propagation (fail/trap), async/await stubs, Vec<T>, String methods, and now C-like enums.

---

## [0.4.2] - 2025-10-20 (freedom-1.0 series)

### Language Philosophy
- **FREEDOM Revolution**: Replaced `unsafe` keyword with `freedom` throughout the language
  - Philosophy: Trade safety for FREEDOM - empowering programmers over compiler restrictions
  - All `unsafe` blocks → `freedom` blocks
  - `KEYWORD_UNSAFE` → `KEYWORD_FREEDOM` in lexer and parser
  - 71 files updated with global refactoring

### Major Features
- **Generic Function Monomorphization**: Complete LLVM implementation for generic functions
  - Template storage and on-demand instantiation
  - Type parameter substitution (T → ConcreteType)
  - Method resolution on generic parameters
  - Function specialization with caching
  - Works seamlessly with aspect bounds: `func<T<Display>>(item: T)`

- **Aspect System Foundation**: User-extensible aspects with bind blocks
  - Define aspects with method signatures
  - Implement aspects for types using `bind Aspect -> Type` syntax
  - Generic functions call aspect methods on bounded type parameters
  - Full semantic validation and aspect registry

### Improved
- **Documentation**: Comprehensive updates for v0.4.2
  - Updated all version banners to 0.4.2 (freedom-1.0 series)
  - Added working aspect method call examples
  - Simplified roadmap emphasizing production-ready status
  - Fixed broken documentation links

- **Code Quality**: Cleaned up temporary and test output files
  - Removed generated LLVM IR files (*.ll)
  - Removed old test results and reports
  - Removed obsolete test scripts

### Status
Vyb v0.4.2 is a **fully functional, production-ready systems programming language** with complete core features, generic functions, and aspect system foundation.

### Tagged Release
**freedom-1.0**: First release emphasizing programmer FREEDOM with generic functions and aspect system

---

## [0.4.0] - 2025-10-17

### Major Infrastructure Upgrade
- **MCJIT to ORC JIT Migration**: Complete replacement of deprecated MCJIT with modern LLVM ORC JIT
  - Migrated from `ExecutionEngine` to `LLJIT` (LLVM 18 ORC JIT infrastructure)
  - Updated symbol registration from `addGlobalMapping()` to `SymbolMap` with `ExecutorSymbolDef`
  - Enhanced memory management with `ThreadSafeModule` support
  - Added `releaseContext()` method to codegen for proper context handling
  - Comprehensive symbol registration for standard library functions (`malloc`, `free`, `memset`)
  - Proper handling of mangled symbol names with variants (.1, .2, etc.)

### Fixed
- **Segmentation Fault Resolution**: Completely resolved segmentation faults in Vec system memory management
  - Fixed crashes occurring during JIT execution of functions with malloc/free operations
  - ORC JIT provides better isolation between JIT memory and application memory
  - Vec system memory management now works perfectly with multiple object creation and cleanup
  - Proven through comprehensive testing with multiple Vec creation scenarios

### Improved
- **JIT Performance**: Modern ORC JIT provides better performance and stability
- **Memory Safety**: Enhanced separation between compiler memory and runtime memory
- **Function Execution**: Robust function pointer conversion using `ExecutorAddr` API
- **Symbol Resolution**: Better handling of runtime symbol lookup and registration

### Technical Details
- **New LLVM Headers**:
  - `llvm/ExecutionEngine/Orc/LLJIT.h`
  - `llvm/ExecutionEngine/Orc/ThreadSafeModule.h`
  - `llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h`
- **Updated Function Lookup**: Replaced `FindFunctionNamed()` with `jit->lookup()` pattern
- **Enhanced Symbol Registration**: `ExecutorAddr::fromPtr()` for proper symbol mapping
- **Memory Function Support**: Full registration of malloc/free/memset with proper mangling

### Migration Impact
- **Developer Experience**: No changes to Vyb language syntax or semantics
- **Runtime Stability**: Dramatically improved stability for memory-intensive operations
- **Vec System**: Full functionality restored with automatic cleanup working perfectly
- **Performance**: Better JIT compilation performance with modern LLVM infrastructure

### Test Results
- **Memory Safety**: Multiple Vec creation and destruction without crashes
- **Function Calls**: Complex function call chains with malloc/free operations
- **Return Values**: Proper exit code handling and complex return type serialization
- **Compilation**: Faster and more reliable JIT compilation process

### Files Modified
- `src/main.cpp` - Complete MCJIT to ORC JIT conversion with symbol registration
- `include/vyb/vre/llvm/codegen.hpp` - Added ThreadSafeModule support methods
- Standard library integration remains unchanged, maintaining API compatibility

---

## [0.3.6] - 2025-06-02

### Added
- **Complete ToString Infrastructure**: Implemented comprehensive toString functionality for proper type-aware string concatenation
  - Added 16 toString intrinsic functions for all basic types (Int, Int8-64, UInt8-64, Float, Float32, Bool, String, Char, Rune, Byte)
  - Automatic type-aware string concatenation: operations like `"User ID:" + id` now work when `id` is a type alias (e.g., `UserId` which resolves to `Int`)
  - Type alias resolution system that resolves aliases to base types for proper toString selection
  - Mixed-type string concatenation with automatic toString conversion for non-string operands
  - JIT function registration for all toString functions enabling runtime execution

### Improved
- **Enhanced String Operations**: String concatenation now handles mixed types automatically
  - Binary expression handler detects when at least one operand is a string and triggers toString conversion
  - Seamless integration with existing type system and code generation
- **Better Type Resolution**: Added helper methods for resolving type aliases to base type names
- **Runtime Integration**: Complete JIT registration system for toString functions

### Fixed
- **String Concatenation Type Errors**: Fixed issues where mixing string literals with type aliases caused compilation errors
- **Return Statement Serialization**: Enhanced serialization to handle complex tuple patterns like `{ i64, ptr, i8 }` with meaningful field names
- **Type-aware Code Generation**: Improved LLVM IR generation for mixed-type operations

### Technical Details
- **New Intrinsic Functions**:
  - `__vyb_toString_int()`, `__vyb_toString_int8()`, `__vyb_toString_int32()`, `__vyb_toString_float()`, `__vyb_toString_bool()`, `__vyb_toString_string()`
  - Extended support: `__vyb_toString_int16()`, `__vyb_toString_int64()`, `__vyb_toString_uint8-64()`, `__vyb_toString_float32()`, `__vyb_toString_char()`, `__vyb_toString_rune()`, `__vyb_toString_byte()`
- **New Helper Methods**:
  - `generateToStringCall()` - converts values to strings based on type
  - `generateMixedStringConcatenation()` - handles mixed-type concatenation
  - `resolveTypeAliasToBaseName()` - resolves type aliases to base type names
- **Enhanced Code Generation**: Modified binary expression handler in `cgen_expr.cpp` to detect string operations and trigger automatic conversion

### Test Results
- **String Concatenation**: `"User ID: 42"`, `"User Name: Alice"`, `"Score: 100"`
- **JSON Serialization**: `{"UserId":42,"UserName":"Alice","Score":100}`
- **Type Safety**: Maintains type safety while enabling intuitive string operations

### Files Modified
- `src/vre/intrinsics.cpp` - Added 16 toString functions and enhanced serialization
- `include/vyb/vre/llvm/codegen.hpp` - Added method declarations for string conversion helpers
- `src/vre/llvm/cgen_string.cpp` - Implemented helper methods for type resolution and mixed concatenation
- `src/vre/llvm/cgen_expr.cpp` - Modified PLUS case in binary expression handler
- `src/main.cpp` - Added comprehensive toString function declarations and JIT registration

---

## [0.3.5] - 2025-05-26

### Added
- **Comprehensive Auto-Serialization Capabilities**: Added full support for automatic serialization of structured data types when returned from `main()` functions
  - New serialization mode intrinsics: `lit()`, `notype()`, `bare()`, `deserial()`
  - JSON construction intrinsics: `__vyb_serialize_to_json()`, `__vyb_serialize_struct_with_names()`
  - Array and object construction functions for manual JSON building
  - Automatic activation for structured return values from `main()`
  - Comprehensive documentation in `doc/Intrinsics.md` Section 7

### Improved
- **Enhanced Parser Error Handling**: Improved error messages and handling for common syntax mistakes
- **Documentation Updates**:
  - Updated all version references from 0.3.4 to 0.3.5
  - Enhanced feature descriptions in README.md
  - Comprehensive auto-serialization documentation added to intrinsics guide
  - Updated installation guide to reference v0.3.5

### Fixed
- Parser error handling for malformed syntax constructs
- Documentation consistency across all files

### Documentation
- Updated `README.md` with enhanced feature descriptions and v0.3.5 installation guide
- Updated `doc/AST_Declarations.md` version reference to v0.3.5
- Comprehensive auto-serialization documentation added to `doc/Intrinsics.md`
- Updated project version in `CMakeLists.txt`

### Supporting Tests
The following test files validate the v0.3.5 functionality:

#### Auto-Serialization Tests
- **`test/test_auto_serialize_basic.vyb`**: Basic auto-serialization without intrinsics (multi-value return)
- **`test/test_lit_intrinsic_simple.vyb`**: Simple `lit()` intrinsic for raw JSON literal output
- **`test/test_lit_intrinsic_multiple.vyb`**: Multiple `lit()` intrinsics generating JSON array output
- **`test/test_notype_intrinsic.vyb`**: Error handling test for `notype()` with primitives (should fail)
- **`test/test_notype_struct.vyb`**: Proper `notype()` usage with structs for metadata suppression
- **`test/test_lit_primitives.vyb`**: Additional primitive type serialization tests

#### Multi-Value Return & Function Tests
- **`test/test_multi_value_return.vyb`**: Multi-value function returns with auto-serialization
- **`test/test_multi_value_parser.vyb`**: Parser validation for multi-value syntax
- **`test/simple_fn_test.vyb`**: Simple function declaration and execution
- **`test/direct_return.vyb`**: Direct return value handling

#### Parser Error Handling Tests
- **`test/test_function_syntax_error_handling.vyb`**: Enhanced error messages for common function syntax mistakes
- **`test/units/parser/test*.vyb`**: Comprehensive parser validation suite (58 test files)
- **`test/units/extracted/test*.vyb`**: Extracted test cases for edge cases (100+ test files)

#### Struct & Type System Tests
- **`test/test_struct_syntax.vyb`**: Advanced struct declarations with auto-serialization
- **`test/test_struct_syntax_simplified.vyb`**: Simplified struct syntax validation
- **`test/test_type_alias.vyb`**: Type alias functionality
- **`test/test_type_alias_simple.vyb`**: Simple type alias cases

#### Integration & Semantic Tests
- **`test/test_semantic_integration.vyb`**: Full semantic analysis integration
- **`test/debug_test.vyb`**: Debug output and analysis validation
- **`test/println_test.vyb`**: Basic output functionality

#### Relaxed Syntax Tests
- **`test/test_relaxed*.vyb`**: Relaxed syntax parsing for improved developer experience
- **`test/units/test_relaxed*.vyb`**: Additional relaxed syntax validation

**Test Statistics:**
- **Core Feature Tests**: 15+ dedicated auto-serialization and multi-value tests
- **Parser Tests**: 58 comprehensive parser validation tests
- **Extracted Tests**: 100+ edge case and regression tests
- **Integration Tests**: 10+ semantic and integration validation tests
- **Total Test Coverage**: 180+ test files ensuring robust v0.3.5 functionality

All tests include expected output validation and error condition testing where appropriate.

---

## [0.3.4] - Previous Release

Previous version with support for:
- Advanced constructs like asynchronous programming
- Generic templates and operator overloading
- Class declarations within templates
- Comprehensive test suite validation

---

*For detailed documentation on auto-serialization capabilities and configuration, see `doc/Auto_Serialization_Main_Returns.md`.*
