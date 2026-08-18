# Vyb FFI Design — Calling C Libraries

**Status:** Planned for v0.5 — specification complete, implementation not yet started
**Priority:** HIGH (required for stdlib File I/O, networking, and platform integration)

---

## Overview

Vyb's FFI (Foreign Function Interface) allows calling C functions from Vyb code using
`extern "C"` declaration blocks.  This document specifies the design.

Vyb targets LLVM, which already understands C calling conventions, so FFI is primarily a
*declaration + type mapping* problem rather than a code generation problem.

---

## 1. `extern "C"` Declaration Blocks

Declare C functions with their Vyb-equivalent signatures inside an `extern "C"` block:

```vyb
extern "C" {
    // Standard C I/O
    printf(fmt<loc<Int8>>, ...)<Int>        // variadic — the only supported variadic form
    puts(s<loc<Int8>>)<Int>
    fopen(path<loc<Int8>>, mode<loc<Int8>>)<loc<Void>>
    fclose(stream<loc<Void>>)<Int>
    fread(buf<loc<Void>>, size<Int>, count<Int>, stream<loc<Void>>)<Int>
    fwrite(buf<loc<Void>>, size<Int>, count<Int>, stream<loc<Void>>)<Int>

    // POSIX sockets
    socket(domain<Int>, type<Int>, protocol<Int>)<Int>
    connect(sockfd<Int>, addr<loc<Void>>, addrlen<Int>)<Int>
    send(sockfd<Int>, buf<loc<Void>>, len<Int>, flags<Int>)<Int>
    recv(sockfd<Int>, buf<loc<Void>>, len<Int>, flags<Int>)<Int>
    close(fd<Int>)<Int>

    // Memory
    malloc(size<Int>)<loc<Void>>
    free(ptr<loc<Void>>)<Void>
    memcpy(dst<loc<Void>>, src<loc<Void>>, n<Int>)<loc<Void>>
    strlen(s<loc<Int8>>)<Int>
}
```

### Rules

- All `extern "C"` declarations are **implicitly in a `freedom` context** at the call site —
  callers must be inside a `freedom { }` block to call them directly.
- Higher-level Vyb wrappers may hide this requirement from end users.
- Variadic functions (`...`) are supported only via `printf`-style calling convention;
  Vyb-side variadic arguments must be passed as LLVM varargs.

---

## 2. C Type Mapping

| Vyb type          | C equivalent      | Notes                                 |
|-------------------|-------------------|---------------------------------------|
| `Int`             | `int64_t`         | 64-bit signed integer                 |
| `Int32`           | `int32_t`         | 32-bit signed integer                 |
| `Int8`            | `int8_t` / `char` | 8-bit signed; used for C strings      |
| `UInt`            | `uint64_t`        |                                       |
| `UInt8`           | `uint8_t`         | Byte / unsigned char                  |
| `Float`           | `double`          | 64-bit IEEE 754                       |
| `Float32`         | `float`           | 32-bit IEEE 754                       |
| `Bool`            | `_Bool` / `int`   | 0 or 1                                |
| `loc<T>`          | `T*`              | Raw pointer; only valid in `freedom`  |
| `loc<Void>`       | `void*`           | Opaque pointer                        |
| `loc<Int8>`       | `char*`           | C string pointer                      |
| `Void`            | `void`            | Function returns nothing              |

### C String Interop

C strings (`char*`) map to `loc<Int8>` in Vyb.  Converting a Vyb `String` to a C string
requires a `freedom` block:

```vyb
call_puts(msg<String>)<Void> -> {
    freedom {
        // Get null-terminated byte pointer from Vyb String
        cstr<loc<Int8>> = msg.as_c_str()   // stdlib helper (v0.5)
        puts(cstr)
    }
}
```

---

## 3. `repr(C)` Structs

To pass structs to C functions, the struct layout must match C's memory layout:

```vyb
#[repr(C)]
struct SockAddrIn {
    sin_family<UInt16>,     // AF_INET = 2
    sin_port<UInt16>,       // port in network byte order
    sin_addr<UInt32>,       // IPv4 address
    sin_zero<[UInt8; 8]>    // padding
}

extern "C" {
    bind(sockfd<Int>, addr<loc<SockAddrIn>>, addrlen<Int>)<Int>
}
```

`#[repr(C)]` preserves declaration-order fields and emits the LLVM struct as an
unpacked target-layout struct. Without it, the Vyb compiler reserves the right to
change layout in future releases.

Current restrictions are intentionally conservative: `repr(C)` structs cannot be
generic, cannot contain ownership-qualified fields (`my`, `our`, `their`, `mild`,
`view`, `borrow`), and cannot contain Vyb runtime fields such as `String`, `Bytes`,
`Vec`, `Future`, tuples, or optional values. Use `CString`, `CPtr<T>`, `loc<T>`,
C scalar aliases, or another explicit C ABI representation instead.
The attribute syntax uses `#[...]` (Rust-inspired for now; may change before 1.0).

---

## 4. Calling Existing C Libraries

### Example: `strlen` via libc

```vyb
extern "C" {
    strlen(s<loc<Int8>>)<Int>
}

string_c_len(s<String>)<Int> -> {
    result<Int> = 0
    freedom {
        cstr<loc<Int8>> = s.as_c_str()
        result = strlen(cstr)
    }
    return result
}
```

### Example: File I/O via stdio

```vyb
extern "C" {
    fopen(path<loc<Int8>>, mode<loc<Int8>>)<loc<Void>>
    fclose(stream<loc<Void>>)<Int>
    fgets(buf<loc<Int8>>, n<Int>, stream<loc<Void>>)<loc<Int8>>
}

read_first_line(path<String>)<String> -> {
    buf<String> = ""
    freedom {
        cpath<loc<Int8>> = path.as_c_str()
        mode<loc<Int8>>  = "r".as_c_str()
        fp<loc<Void>>    = fopen(cpath, mode)
        if (fp != null) {
            cbuf<[Int8; 1024]>  // stack-allocated 1024-byte buffer
            fgets(loc(cbuf), 1024, fp)
            buf = String::from_bytes(loc(cbuf))
            fclose(fp)
        }
    }
    return buf
}
```

---

## 5. Linking

To link against a C library in native build mode, use the repeatable `--link` flag:

```bash
vyb myprogram.vyb --build myprogram --link c --link m  # link libc and libm
vyb myprogram.vyb --build myprogram --link ssl         # link OpenSSL
```

Bare names are passed as `-l<name>`. Arguments that already start with `-`, contain
a path separator, or name a `.a`, `.so`, `.dylib`, or `.o` file are passed through.

Alternatively, `vyb.toml` (package manager) will support:

```toml
[dependencies]
c   = { link = "c" }
ssl = { link = "ssl" }
```

---

## 6. Implementation Plan (v0.5)

| Task | Notes |
|------|-------|
| Parse `extern "C" { }` blocks | Add `ExternBlock` AST node |
| Emit LLVM `declare` for extern functions | In `cgen_decl.cpp` |
| Type mapping table | Vyb type → LLVM IR type for all C-interop types |
| `#[repr(C)]` struct attribute | Implemented for non-generic C-stable fields |
| `String::as_c_str()` stdlib method | Returns null-terminated `loc<Int8>` |
| `--link` CLI flag | Implemented for native `--build` linker inputs |
| Test: call `strlen`, `puts` | Validates end-to-end FFI |
| Test: `#[repr(C)]` struct round-trip | Pass struct to C, read back |

---

## 7. What FFI Enables

Once FFI is implemented, many other planned features become library problems rather than
language problems:

- **File I/O** — wrap `fopen`/`fclose`/`fread`/`fwrite`
- **Networking** — wrap POSIX socket API
- **Math** — expose `libm` functions beyond what's in Vyb intrinsics
- **OS signals** — `signal()`, `sigaction()`
- **Terminal** — `tcgetattr()`/`tcsetattr()`, readline
- **TLS** — OpenSSL / mbedTLS bindings
- **Graphics** — SDL2, OpenGL via thin Vyb wrappers
- **Database** — SQLite C API

FFI is the foundation that unlocks the systems programming story for Vyb.

---

*See also:* `doc/MODULE_FFI_BINARY_ROADMAP.md`
