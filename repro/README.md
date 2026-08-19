# repro

Standalone reproducers for bugs that need an interaction wider than the unit
tests (e.g. they only fire inside a threaded/async runtime path or need an
AddressSanitizer build to observe).

## `http_loop_uaf.vyb`
Tracks a String use-after-free that fired in the async http/https fetch worker.
The runtime's String registry is an open-addressed hash table with linear
probing; it used to clear a freed buffer's slot to NULL, but linear probing
can't stop walking at a deleted slot. A live entry sitting *behind* a freed slot
in the same probe cluster could be missed by `retain` while `release` still
found it, under-counting the reference count and freeing a String the caller
still held. This http loop churns enough distinct heap buffers to make that
probe miss show up as a use-after-free on the status line (`"HTTP/1.1 200 OK"`),
which `http_status_code` re-reads via `substring(9,15)`.

```sh
VYB_STDLIB=stdlib ASAN_OPTIONS=detect_leaks=0:abort_on_error=1:halt_on_error=1 \
    ./build-asan/vyb -- repro/http_loop_uaf.vyb
```

Pre-fix: aborts ~always with
`heap-use-after-free: READ of size 6 at offset 9` / `freed by
__vyb_string_release -> __vyb_string_free` on the status-line buffer.
Post-fix: prints `ok` and exits 0 (the generated code is balanced; a stale-slot
status line is gone).

The fix is in `runtime/vyb_runtime.c`: freed slots keep a tombstone marker
instead of NULL, so probes keep walking past deleted slots and can't miss a live
entry.

## String temp / Vec<String> leaks (memory growth in long-running http clients)

Beyond the tombstone fix above, a long-running VybLynx/http client grew memory
linearly with page loads because several owned String temporaries were never
released. Fixed together under `fix(llvm): reclaim owned String temps`:

- **Mixed concat owned operand.** `"x" + i.to_string()` went through
  `__vyb_string_concat`, which copies both operands but only reclaimed the
  `to_string` *conversions* of non-String operands; a String operand that was
  itself a freshly built owned temp (`.to_string()`, `.substring()`) leaked.
  `LLVMCodegen::generateMixedStringConcatenation` now drops an owned String
  operand's reference after the copy (`freeLeft/RightOwnedTemp`).
- **Owned String temps passed by value.** A fresh String handed to a function
  (e.g. `http_parse_int(line.substring(...))`, `socket_send(fd, data.substring(...))`)
  was copied by the callee but the caller's temporary reference was never
  released. The general call path now records and frees those temps after the call.
- **Computed String receiver used once.** `header.substring(0, c).trim()` builds
  a fresh buffer for the receiver then discards it; the String-method dispatch
  now frees an owned receiver temp after the call.

- **Vec<String> element stride.** The runtime bulk helpers
  `__vyb_string_release_each` / `__vyb_string_retain_each` iterated the element
  buffer as 8-byte `char**`, but generated `Vec<String>` elements are 16-byte
  `{ ptr, len }` structs. So the helpers read every other slot (releasing only
  ~half of a Vec's String elements, and reinterpreting the `len` field of the
  skipped ones as a pointer). For a 3-header HTTP response this leaked the last
  header each round. The helpers now stride by the `{ ptr, len }` struct.

To observe the fixed behavior, loop a `Vec<String>`/mixed-concat build and watch
RSS stay flat; the http repro (`repro/http_loop_uaf.vyb`) now exits `ok` with a
flat RSS instead of growing ~1 header buffer per request.
