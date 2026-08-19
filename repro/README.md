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
