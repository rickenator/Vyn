# repro

Standalone reproducers for bugs that need an interaction wider than the unit
tests (e.g. they only fire inside the async/http worker path and would make
the standard suite crash nondeterministically).

## `http_loop_uaf.vyb`
Enables the fetch-worker double-free / use-after-free with only stdlib `http`
(no app, no TLS, no async). Loop `http_get_full` against the local threaded
server under an AddressSanitizer build.

```sh
VYB_STDLIB=stdlib ASAN_OPTIONS=detect_leaks=0:abort_on_error=1:halt_on_error=1 \
    ./build-asan/vyb -- repro/http_loop_uaf.vyb
```

Expected: repeated

```
ERROR: AddressSanitizer: heap-use-after-free on address ...
READ of size 6 at 0x...  (memcpy)
freed by thread T0 here:
    __vyb_string_release -> __vyb_string_free
Vyb call stack at death: http_get_full -> main
```

The 16-byte region is the status line `"HTTP/1.1 200 OK"` returned by
`http_read_line`; `http_status_code` re-reads it (`substring(9,15)`). That
means a String returned from a function is being freed on return while the
caller still references it — a retain/release imbalance in the generated code
for function-return Strings. Full setup: to eyeball the JITted call stack on
any crash, run the ASAN build of the compiler (its runtime now keeps a
thread-local Vyb call stack and dumps it via an ASAN death callback).
