# Building Vyb — supported build profiles (#160)

Vyb uses CMake with **explicit build profiles**. `Debug` is the default, but a
caller-selected `CMAKE_BUILD_TYPE` is always respected (it is never silently
overridden). Prefer the named presets in `CMakePresets.json` — they pin the build
type, sanitizer, and module options so a build is reproducible.

## Presets (`cmake --preset <name>` / `cmake --build --preset <name>`)

| Preset | Build type | Sanitizer | Module flags | Binary |
|---|---|---|---|---|
| `debug` (default) | Debug | none | OpenSSL on, Qt off | `build/vyb` |
| `release` | Release | none | OpenSSL on, Qt off | `build-release/vyb` |
| `relwithdebinfo` | RelWithDebInfo | none | OpenSSL on, Qt off | `build-relwithdebinfo/vyb` |
| `asan` | Debug | `address,undefined` | Qt off, openssl on, libclang off | `build-asan/vyb` |
| `asan-leaks` | Debug | `address` (LSan on by default) | Qt off, openssl on, libclang off | `build-asan-leaks/vyb` |

Examples:

    cmake --preset debug && cmake --build --preset debug
    cmake --preset release && cmake --build --preset release   # optimized native
    cmake --preset asan && cmake --build --preset asan         # memory-safety

You can also configure directly (the same effective settings):

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release               # optimized
    cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
          -DVYB_SANITIZE="address,undefined" -DVYB_USE_QT5=OFF

## Sanitizer option

`-DVYB_SANITIZE=<list>` adds `-fsanitize=<s>` to every compile and link for each
listed sanitizer (plus `-fno-omit-frame-pointer`). Common values:

- `address` — ASan + LeakSanitizer (LSan is on by default for ASan on Linux).
- `address,undefined` — ASan + UBSan (the CI ASan job).
- `thread` — TSan (not wired into CI).

The full test suite is ASan/LSan-clean end to end:

    cmake --preset asan
    cmake --build --preset asan
    VYB_ASAN=1 python3 test/run_tests.py --vyb build-asan/vyb --test-dir test --execute-jit

## Build identity (#160)

Every produced `vyb` binary embeds its build configuration. It is reported by
`--version` (or `--build-info`):

    $ build-release/vyb --version
    Vyb 0.7.3 (build=Release, sanitize=none)
    $ build-asan/vyb --version
    Vyb 0.7.3 (build=Debug, sanitize=address,undefined)

So diagnostics and reported versions always identify the exact configuration the
binary was built with. The identity comes from CMake-provided macros
(`VYB_BUILD_TYPE_STR`, `VYB_SANITIZE_STR`, `VYB_PROJECT_VERSION`).

## Typical workflow

- **Develop / debug**: `debug` preset, run the suite with `build/vyb`.
- **Ship an optimized binary**: `release` preset.
- **Memory-safety gate**: `asan` preset; the LeakSanitizer output is CI's
  authoritative signal. CI runs the full suite under ASan+LSan on main push and
  nightly (see `.github/workflows/ci.yml`).

## Testing: CTest + reproducible release evidence (#158)

The canonical language suite is registered with **CTest**:

    ctest --test-dir build            # lang-jit (full JIT suite) + aot-native
    ctest --test-dir build -L jit     # just the JIT language suite
    ctest --test-dir build -L aot     # just the AOT/native compile test

Both registered tests write machine-readable JSON **and** a reproducible
`*-evidence.json` under `<build>/test-results/`:

- `jit.json` / `jit-evidence.json` — per-test + summary from `test/run_tests.py`.
- `aot-evidence.json` — per-test + summary from `test_compilation.sh --json`.

The evidence JSON records the exact compiler **revision** (`git rev-parse HEAD`),
the compiler `--version` (build type + sanitizer), the command line, totals
(total/passed/failed), wall duration, and each failing test with its reasons and
stderr — so release validation is reproducible from CI, not narrative. CI publishes
these as upload artifacts.

Focused local runs stay available on the harness directly:

    python3 test/run_tests.py --test-dir test --execute-jit          # all
    python3 test/run_tests.py --test-dir test/units --category parser # one category
    python3 test/run_tests.py --test-dir test --pattern "test_http*"  # pattern

