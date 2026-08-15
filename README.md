<img src="vyb.png" alt="Vyb Image" width="300">

[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/rickenator/Vyb)

## Vyb Programming Guide

---

## 1. Introduction

Welcome to the Vyb Programming Guide. This guide walks you through writing, building, and extending Vyb programs, from your first "Hello, Vyb!" to deep dives into the Vyb language internals and runtime. **Version 0.5.4** (freedom-1.0 series, in development) delivers a robust systems programming language with an LLVM backend, native code compilation to standalone executables, a sized type system, pattern matching with `match` and `select` expressions, comprehensive control flow including `defer`, `break`/`continue`, resizable `Vec<T>` collections, string methods, a math library, runtime type introspection (`typeof`/`typename`), unified name-first function syntax, compile-time monomorphized generics, aspect/bind polymorphism, a `fail`/`trap` error system, C-like enums, and comprehensive auto-serialization capabilities.

### 1.1 Purpose & Audience

This guide is intended for systems programmers, language designers, and developers who want:

* A compact, expressive syntax for both low-level control and high-level abstractions.
* Planned fine-grained memory management options, including GC, manual free, and scoped regions.
* Built-in concurrency primitives and customizable threading templates.
* A foundation for a self-hosted compiler and hybrid VM/JIT architecture for rapid iteration and performance tuning.

Whether you're coming from C/C++, Rust, D, or other modern systems languages, you'll find Vyb's template-driven approach familiar yet uniquely powerful.

Here's a comparison of Vyb against several modern systems languages, showing key similarities and differences:

| Language | Templates / Generics               | Memory Model                                                       | Concurrency                            | Syntax Style                      | Unique Feature                                     | Comment                                                      |
| -------- | ---------------------------------- | ------------------------------------------------------------------ | -------------------------------------- | --------------------------------- | -------------------------------------------------- | ------------------------------------------------------------ |
| **Vyb**  | Monomorphized generics everywhere | Ownership types (`my`/`our`/`mild`/`their`), reference counting | Async/await, planned actors, threads, channels | Name-first syntax `name(params)<Type> ->`, aspect/bind polymorphism | `select` expressions, `defer`, `freedom` blocks, `fail`/`trap` error system | Combines zero-cost generics with readable ownership semantics |
| **Rust** | Monomorphized generics             | Ownership/borrow checker; optional `Arc`/`Rc`                      | `async`/`await`, threads, channels     | C-style braces, macros            | Zero-cost abstractions; strong compile-time safety | No global GC; all memory safety enforced at compile time     |
| **D**    | Runtime & compile-time templates   | GC by default; `@nogc` for manual alloc/free                       | `std.concurrency` fibers, threads      | C-style; mixins                   | Compile-time function execution (CTFE)             | Blend of high-level features with systems control            |
| **C++**  | Templates & concepts (20+)         | Manual `new`/`delete`; smart pointers (`unique_ptr`, `shared_ptr`) | Threads, coroutines (`co_await`)       | C-style braces                    | Metaprogramming via templates & concepts           | Extensive ecosystem; highest portability                     |
| **Nim**  | Generics + macros                  | GC by default; optional manual `alloc`                             | Async (`async`/`await`), threads       | Python-like indentation           | Hygienic macros; optional GC or ARC                | Very concise syntax; strong metaprogramming support          |
| **Go**   | Generics (1.18+)                   | GC only                                                            | Goroutines, channels                   | C-style, minimal                  | CSP-style concurrency                              | Simple, fast compile; built-in tooling                       |

*Note:* Each language offers a different balance of safety, performance, and ergonomics. Vyb's strength lies in unifying template metaprogramming, planned flexible memory management, and a future hybrid VM/JIT in a terse, self-hosted package.

### 1.2 What is Vyb?

Vyb is a statically typed, compiled systems language targeting native code via LLVM. Its key differentiators:

* **Name-First Syntax**: `name(params)<ReturnType> ->` — no `fn` keyword noise; function names come first.
* **Aspect/Bind Polymorphism**: Aspects (`aspect`) + struct binding (`bind Aspect -> Type`) instead of classes — more composable, no inheritance pitfalls.
* **Readable Ownership**: `my<T>` (unique), `our<T>` (shared), `their<T>` (borrowed), `mild<T>` (weak) — ownership intent is explicit without cryptic lifetime annotations.
* **Programmer FREEDOM**: `freedom { ... }` blocks for low-level control; `loc<T>` raw pointers within them — no hidden tax on safe code.
* **`select` Expressions**: Pattern matching that yields a value with a `pass` keyword for multi-statement arms — uniquely Vyb, not found in other languages.
* **`fail`/`trap` Error System**: Typed error propagation with zero-cost success path; no try/catch/finally.
* **`defer` Statement**: LIFO scope-exit cleanup without RAII ceremony.
* **Monomorphized Generics**: Zero-cost generics with aspect bounds (`<T<Display>>`).
* **Concurrency Built In**: Async/await today; actors, channels, and typed streams planned.
* **Native Compilation**: Full JIT (LLVM ORC), AOT (object files), and executable generation pipeline.

**Current Version:** 0.5.4 (freedom-1.0 series, in development) 🚀 **AOT COMPILATION + CROSS-PLATFORM SUPPORT**

## Quick Start

```bash
# Clone and build
git clone https://github.com/rickenator/Vyb.git
cd Vyb
mkdir -p build && cd build && LLVM_DIR=/usr/lib/llvm-18/cmake cmake .. && make -j$(nproc) && cd ..

# Run with modern test harness (740+ tests)
python3 test_harness.py --vyb ./build/vyb --test-dirs test/new_features --workers 4

# Run your first Vyb program
echo 'main()<Int> -> { return 42 }' > hello.vyb
build/vyb hello.vyb  # Returns exit code 42

# Try select expressions with pattern matching
cat > example.vyb << 'EOF'
main()<Int> -> {
    numbers<Vec<Int>> = Vec()
    numbers.push(10)
    numbers.push(20)

    result<Int> = select(numbers.len()) -> {
        0 -> 0,
        2 -> {
            sum<Int> = numbers.get(0) + numbers.get(1)
            pass sum
        },
        ? -> -1
    }
    return result
}
EOF
build/vyb example.vyb  # Returns 30

# Complex return types with auto-serialization
echo 'main()<Int,String> -> { return 42, "Hello!" }' > tuple.vyb
build/vyb tuple.vyb  # Outputs: [42, "Hello!"]
```

### Compilation to Native Code (v0.4.3+)

Vyb provides a complete compilation pipeline from source to standalone executables:

#### Building Standalone Executables (NEW in v0.4.4!)

```bash
# Build executable (simplest form)
build/vyb hello.vyb --build hello
./hello  # Run directly!

# Build with custom name
build/vyb program.vyb --build myapp
./myapp

# Build with optimization
build/vyb program.vyb -b myapp -O3  # Maximum optimization

# Static linking (fully standalone, no runtime dependencies)
build/vyb program.vyb -b myapp --static

# What happens:
# 1. Compiles hello.vyb → hello.o (LLVM object file)
# 2. Compiles runtime/vyb_runtime.c → vyb_runtime.o (C runtime)
# 3. Links with system linker (lld/ld) + CRT files + libc/libm
# 4. Creates executable: hello

# Verify the executable
file myapp
# Output: myapp: ELF 64-bit LSB executable, x86-64, dynamically linked, with debug_info

ldd myapp
# Output:
#   linux-vdso.so.1 (0x00007fff...)
#   libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x00007f...)
#   libm.so.6 => /lib/x86_64-linux-gnu/libm.so.6 (0x00007f...)
```

#### Compiling to Object Files (v0.4.3)

```bash
# Compile to object file (default -O2 optimization)
build/vyb hello.vyb --compile hello.o

# Specify optimization level
build/vyb program.vyb -c program.o -O0  # No optimization (fast compile)
build/vyb program.vyb -c program.o -O1  # Basic optimization
build/vyb program.vyb -c program.o -O2  # Moderate optimization (default)
build/vyb program.vyb -c program.o -O3  # Aggressive optimization

# Verify object file was created
file hello.o
# Output: hello.o: ELF 64-bit LSB relocatable, x86-64, version 1 (SYSV), with debug_info, not stripped

# Inspect symbols in object file
objdump -t hello.o | grep main
# Output: 0000000000000000 g     F .text  000000000000005c main
```

#### Cross-Compilation Support

Supports 20+ architectures out of the box:
- **x86-64** (default for most systems)
- **ARM/AArch64** (ARM64, Raspberry Pi, mobile)
- **RISC-V** (open ISA, embedded systems)
- **PowerPC** (servers, embedded)
- **MIPS** (routers, embedded)
- **SPARC** (Sun/Oracle systems)
- **WebAssembly** (browsers, serverless)
- **SystemZ** (IBM mainframe)
- **Hexagon** (Qualcomm DSP)
- **LoongArch** (Chinese CPUs)
- **M68k** (retro computing)
- **Xtensa** (ESP32 microcontrollers)
- **AVR** (Arduino)
- **MSP430** (ultra-low-power MCU)
- **BPF** (Linux kernel filtering)
- **NVPTX** (NVIDIA GPUs)
- **AMDGPU** (AMD GPUs)
- **VE** (NEC Vector Engine)
- **Lanai** (research processor)
- **XCore** (XMOS processors)

**Complete Compilation Features:**
- ✅ **Executable Generation** (v0.4.4): Full build pipeline with `--build` flag
- ✅ **Runtime Library** (v0.4.4): Automatic compilation and linking of Vyb runtime
- ✅ **System Linker Integration** (v0.4.4): Platform-aware linker selection (lld, ld, ld64)
- ✅ **Dynamic Linking** (v0.4.4): Links against system libc and libm
- ✅ **Static Linking** (v0.4.4): Optional `--static` flag for standalone binaries
- ✅ **Object File Emission** (v0.4.3): AOT compilation to .o files
- ✅ **Optimization Levels** (v0.4.3): -O0 through -O3 with LLVM optimizations
- ✅ **Cross-Compilation** (v0.4.3): 20+ target architectures
- ✅ **Debug Information** (v0.4.3): Full DWARF debug metadata
- 🔜 **Optimization Pipeline** (v0.5.2): Advanced LLVM passes, LTO
- 🔜 **Multi-File Compilation**: Link multiple .vyb files together
- 🔜 **Dynamic Libraries**: Shared object (.so/.dylib) generation
- 🔜 **Package Building**: Full project management with dependency resolution

**See:** `doc/MODULE_FFI_BINARY_ROADMAP.md` for the complete compilation roadmap

### 1.3 Key Concepts & Terminology

*   **Template**: A generic type/function parameterized by types or constants, instantiated at compile time.
*   **Binding Mutability**: Variables are declared with unified syntax (mutable by default) or `const` (immutable binding, cannot be reassigned).
*   **Ownership Types**:
    *   `my<T>`: Unique ownership of data `T`.
    *   `our<T>`: Shared (reference-counted) ownership of data `T`.
    *   `their<T>`: Non-owning borrow/reference to data `T`.
    *   `mild<T>`: Mild reference that can detect when target is destroyed.
*   **Data Mutability**: Indicated by `const` on the type itself (e.g., `my<T const>` for unique ownership of immutable data).
*   **Borrowing**:
    *   `view(expr)`: Creates an immutable borrow `their<T const>`.
    *   `borrow(expr)`: Creates a mutable borrow `their<T>`.
    *   `soft(expr)`: Creates a mild reference `mild<T>` from `our<T>`.
*   **`freedom` Blocks**: Sections of code marked `freedom { ... }` where raw pointers (`loc<T>`) can be used and some compiler guarantees are relaxed. Within these blocks, operations like `at(ptr)` for dereferencing and `from<loc<T>>()` for pointer conversion are available.

### 1.4 Import vs Smuggle - Vyb's Unique Module System (v0.5.0)

Vyb introduces a distinctive approach to module imports with two keywords that serve different security and trust models:

**`import`** - Trusted, Verified Modules:
- Used for modules from signed repositories or project-local sources verified in `vyb.toml`
- Enforces security checks and version verification
- Ideal for production dependencies and standard library modules
- Optional `from "<locator>"` specifies the source URL or local path
- Example: `import std::collections::Vec`
- Example: `import utils::math::calculate from "./utils"`

**`smuggle`** - Flexible, External Sources:
- Allows including symbols from external sources (e.g., GitHub repositories) or unsigned modules
- Bypasses some security checks for rapid prototyping and third-party integration
- Perfect for experimental dependencies, development tools, or one-off utilities
- `from "<locator>"` specifies the source (local path, GitHub shorthand, or full URL)
- Example: `smuggle debug::Logger from "github.com/user/debug-tools"`

**Syntax:**
```
import <module::path> [as <alias>] [from "<locator>"] [;]
smuggle <module::path> [from "<locator>"] [as <alias>] [;]
```

**Locator formats** for `from`:
- Local path: `"./local/experiments"`
- GitHub shorthand: `"github.com/dev/tools"`
- Full URL: `"https://github.com/dev/tools"`

This dual system provides both safety for production code and flexibility for development:

```vyb
# Production imports - verified and trusted
import std::io::println
import utils::math::calculate from "./utils"

# Development/experimental - flexible but marked as such
smuggle debug::trace from "github.com/dev/tools"
smuggle experimental::feature from "./local/experiments"

main()<Int> -> {
    println("Production ready!")
    trace("Debug info from smuggled module")
    return calculate(42)
}
```

Declare dependencies in `vyb.toml`:
```toml
[dependencies]
std = "^1.0.0"  # Signed, from Vyb registry
utils = { git = "https://github.com/user/utils" }  # External, smuggled
```

This unique `import`/`smuggle` distinction makes Vyb's module system both secure and flexible, clearly marking the trust level of your dependencies. See [doc/FEATURE_STATUS.md](doc/FEATURE_STATUS.md) for implementation status.

## In This Release

Vyb **v0.5.4** (freedom-1.0 series, in development) is a mature systems programming language with **native executable generation** and a broad core feature set. It remains under active development toward the 1.0 milestone; see `doc/FEATURE_STATUS.md` for the current feature matrix.

### ✅ **Recent Milestones**
These features were completed in the current release cycle and are fully tested:

- **`defer` statement** — `defer cleanup()` executes at scope exit in LIFO order; ideal for resource cleanup
  ```vyb
  main()<Int> -> {
      defer println("cleanup done")
      println("before return")
      return 0
  }
  // Output: before return \n cleanup done
  ```
- **Math library** — `abs`, `min`, `max`, `sqrt`, `sin`, `cos`, `tan`, `exp`, `log`, `log2`, `log10`, `pow`, `floor`, `ceil`, `round`
  ```vyb
  main()<Int> -> {
      x<Float> = sqrt(2.0)
      y<Int> = abs(-5)
      println_int(y)    // 5
      return 0
  }
  ```
- **String methods** — `.len()`, `.contains()`, `.starts_with()`, `.ends_with()`, `.to_upper()`, `.to_lower()`, `.substring()`, `.char_at()`, `.split()`, `.format()`, `String::from_bytes()`
  ```vyb
  main()<Int> -> {
      s<String> = "Hello, world!"
      if (s.contains("world")) {
          println(s.to_upper())   // HELLO, WORLD!
      }
      return 0
  }
  ```
- **Type inference from initializer** — Variables without annotation infer type from RHS
  ```vyb
  x = 42          // inferred as Int
  msg = "hello"   // inferred as String
  ok = true       // inferred as Bool
  ```
- **Vec `for` loop type inference** — Compiler-generated loop variables require no explicit types
  ```vyb
  nums<Vec<Int>> = Vec()
  nums.push(10)
  for (n in nums) {
      println(n)   // n is automatically Int
  }
  ```
- **`select` expressions** — Pattern matching that yields a value; Vyb-original concept
  ```vyb
  grade<String> = select(score) -> {
      >= 90 -> "A",
      >= 80 -> "B",
      >= 70 -> "C",
      ? -> "F"
  }
  ```
- **`typeof` / `typename` intrinsics** — Runtime type introspection
  ```vyb
  x<Int> = 42
  same<Bool> = (typeof(x) == typeof(100))    // true
  name<String> = typename(x)                  // "Int"
  ```
- **Immutable bindings (`const`)** — `name<Type const>` declares an immutable variable
  ```vyb
  pi<Float const> = 3.14159
  ```

### ✅ **Binary Executable Generation (NEW in v0.4.4!)**
- **Full Compilation Pipeline**: Complete source → object → executable workflow
- **Build Command**: `vyb program.vyb --build myapp` creates standalone executables
- **Runtime Library**: Automatic compilation and linking of Vyb runtime (vyb_runtime.c)
- **System Linker Integration**: Platform-aware linker selection (lld, ld for Linux; ld64 for macOS)
- **C Runtime Initialization**: Automatic discovery and linking of CRT files (crt1.o, crti.o, crtn.o)
- **Dynamic Linking**: Links against system libc and libm by default
- **Static Linking**: Optional `--static` flag for fully standalone binaries
- **Production Ready**: Deploy native executables without any runtime dependencies
- **Cross-Platform**: Linux and macOS support with proper platform detection

### ✅ **JSON Serialization & Deserialization (NEW in v0.4.4!)**
- **Runtime Type Metadata**: Complete type registration system with field introspection
- **Automatic Serialization**: `.to_string()` method on structs generates JSON
- **JSON Deserialization**: `T::from_string(json)` creates instances from JSON strings
- **Type Safety**: Runtime validation of JSON structure against type definitions
- **Supported Types**: Int, Float, Bool, String primitives fully working
- **Round-Trip Conversion**: Full bidirectional struct ↔ JSON conversion
- **Zero Boilerplate**: No manual serialization code needed
- **Test Suite**: Comprehensive tests in `test/json/` directory

### ✅ **Native Code Compilation (v0.4.3)**
- **Object File Emission**: Compile to .o files with `--compile/-c` flag
- **Optimization Levels**: -O0 (none), -O1 (basic), -O2 (default), -O3 (aggressive)
- **Cross-Compilation**: Supports 20+ target architectures out of the box
- **Debug Information**: Full DWARF metadata for debugging compiled code
- **Standard Toolchain**: Compatible with system linkers (ld, lld, gold)
- **ELF Format**: Standard relocatable object files for linking

### ✅ **Core Language Features**
- **Functions**: `name(params)<ReturnType> -> body` with full LLVM compilation
  - **Standard parameters**: `param<Type>` or `param<Type const>`
  - **Immutable parameters**: `param<Type const>` for read-only arguments
- **Variables**: `name<Type> = value` with type inference and explicit typing
- **Immutable bindings**: `name<Type const> = value` — cannot be reassigned
- **Resizable Arrays**: `Vec<T>` with a vybish constructor (`Vec()`, `Vec(n)`) and `push()`, `pop()`, `len()`, `get()` methods
- **Vec Iteration**: `for (item in vec)` loops with inferred element type and `break`/`continue`
- **Fixed Arrays**: `[T; N]` with indexing
- **Structs**: `struct Point { x<Int>, y<Int> }` with field access (`p.x`, `p.y`)
- **Control Flow**: `if/else`, `while/for` loops, `match` statements, `break/continue`
- **`defer` statement**: `defer expr` — LIFO scope-exit execution for cleanup
- **Select Expressions**: `select(expr) -> { pattern -> result }` — pattern matching that returns a value; `pass` keyword for multi-statement arms
- **Arithmetic**: Full binary operators (`+`, `-`, `*`, `/`, `==`, `!=`, `<`, `>`, etc.)
- **Bitwise Operators**: `|`, `&`, `^`, `~`, `<<`, `>>` on `Int` with `&=`, `|=`, `^=`, `<<=`, `>>=` compound-assigns
- **Pattern Matching**: `match (expr) { pattern -> result }` with comparison patterns (`>= 90`, `< 0`) and wildcard `?`
- **Error Handling**: `fail`/`trap` system with zero-cost success path and type-safe error propagation
- **I/O**: `println()` for all types (auto-stringifies primitives); `print()` without newline
- **Math**: `sqrt`, `abs`, `min`, `max`, `sin`, `cos`, `tan`, `pow`, `log`, `floor`, `ceil`, `round`

### ✅ **Advanced Type System**
- **Multi-value returns**: `main()<Int,String> -> return 42, "hello"` — auto-serialized as JSON
- **Variadic Tuples**: `Tuple<T,U,V,...>` with inline `(T,U,V)` syntax
- **Auto-serialization**: Complex return types automatically output as JSON
- **Type inference**: Full type checking and inference — `x = 42` infers `Int`
- **Generic collections**: `Vec<Int>`, `Vec<String>` with full method support
- **Member access**: Struct field access (`obj.field`) and array indexing (`arr[index]`)
- **Ownership types** (syntax fully parsed): `my<T>`, `our<T>`, `their<T>`, `mild<T>`
- **`freedom` blocks**: `freedom { ... }` for programmer-controlled sections with raw `loc<T>` pointers

### ✅ **String Methods**
Comprehensive string manipulation built into the `String` type:

| Method | Description | Example |
|--------|-------------|---------|
| `.len()` | Length in bytes | `s.len()` → `Int` |
| `.contains(sub)` | Substring test | `s.contains("Vyb")` → `Bool` |
| `.starts_with(pre)` | Prefix test | `s.starts_with("He")` → `Bool` |
| `.ends_with(suf)` | Suffix test | `s.ends_with("!")` → `Bool` |
| `.to_upper()` | Uppercase copy | `s.to_upper()` → `String` |
| `.to_lower()` | Lowercase copy | `s.to_lower()` → `String` |
| `.substring(start, len)` | Slice | `s.substring(0, 5)` → `String` |
| `.char_at(i)` | ASCII code at index | `s.char_at(1)` → `Int` |
| `.split(sep)` | Split into `Vec<String>` of its parts | `s.split(",")` → `Vec<String>` |
| `.format(args...)` | Substitute `{}` placeholders in sequence | `s.format(42)` → `String` |
| `String::from_bytes(ptr, len)` | Construct from bytes | `String::from_bytes(p, n)` |

**String concatenation** with `+` auto-converts non-String operands:
```vyb
id<Int> = 42
msg<String> = "User ID: " + id    // "User ID: 42"
```

**Splitting** returns a fresh `Vec<String>` of the parts between each occurrence
of the separator. An empty separator yields a single-element Vec holding the
whole string; leading, trailing, and consecutive separators produce empty parts.
```vyb
csv<String> = "alpha,beta,gamma"
parts<Vec<String>> = csv.split(",")   // parts.len() == 3; parts.get(1) == "beta"
```

**Formatting** substitutes each `{}` placeholder with the string form of the
corresponding argument in order. Arguments of any serializable type (String,
Int, Float, Bool, ...) are converted automatically; placeholders beyond the
supplied arguments are emitted verbatim.
```vyb
tmpl<String> = "User {} has {} badge(s)."
msg<String> = tmpl.format("Vyb", 3)   // "User Vyb has 3 badge(s)."
```

**Method calls on value receivers** — every String method works not only on a
named variable but on any String value: a literal, a function/expression result,
or a struct/array field. Only the receiver itself still needs to be a String;
no intermediate variable is required.
```vyb
upper<String> = "hello".to_upper()                        // "HELLO"
head<String> = full_name().substring(0, 5)                // first 5 chars
count<Int> = get_csv().split(",").len()                   // number of fields
```

### ✅ **Async Programming & Debugging**

Vyb v0.4.1 features **complete async/await support** for writing concurrent programs:

#### Async Function Syntax
```vyb
// Async function returning Future<Int>
async compute_value()<Future<Int>> -> {
    println("Computing...")
    return 42
}

// Async function with await
async process_data()<Future<String>> -> {
    value<Int> = await compute_value()  // Suspend until future resolves
    println("Got value")
    return "processed"
}

// Async void function
async background_task()<Future<Void>> -> {
    println("Task running...")
    return
}
```

#### Key Features
- **async keyword**: Declares asynchronous functions that return Future<T>
- **await expressions**: Suspend execution until a Future resolves
- **Future<T> types**: Type-safe asynchronous return values
- **State machines**: Async functions compiled to efficient state machines
- **Debug support**: Full DWARF metadata for debugging async execution
- **Suspension tracking**: Debug info for continuation points and state transitions

#### Usage Example
```vyb
main()<Void> -> {
    // Create async tasks
    future1<Future<Int>> = compute_value()
    future2<Future<String>> = process_data()

    // Futures execute concurrently
    println("Tasks initiated")
    return
}
```

**See:** `test/async/async_simple.vyb` and `test/async/async_comprehensive.vyb` for working examples

**Implementation Status:**
- ✅ Async function parsing and validation
- ✅ Future<T> type checking
- ✅ await expression support
- ✅ State machine code generation
- ✅ LLVM codegen with debug metadata
- ✅ Comprehensive test coverage

### ✅ **Introspection System (v0.4.2)**

Vyb features **runtime type introspection** for self-aware programs:

#### Type Reflection Operators
```vyb
// Get runtime type hash — use for equality comparison
x<Int> = 42
same<Bool> = (typeof(x) == typeof(100))   // true — both Int

// Get type name as String
name<String> = typename(x)                // "Int"

// Works on any expression
same_expr<Bool> = (typeof(x + 5) == typeof(x * 2))   // true
```

#### Key Features
- **`typeof(expr)`**: Returns a type discriminant for runtime type comparison; same types always compare equal
- **`typename(expr)`**: Returns `String` with the human-readable type name (e.g., `"Int"`, `"Float"`, `"Bool"`)
- **Expression-based**: Works on variables, literals, arithmetic expressions
- **Zero-cost**: Type information extracted at compile-time, evaluated at runtime via std::hash
- **Note**: `typeof` result is an opaque `Type` value; use only for `==`/`!=` comparisons

#### Usage Examples
```vyb
// Type comparison
x<Int> = 42
y<Float> = 3.14
if (typeof(x) != typeof(y)) {
    println("different types")
}

// Foundation for advanced features (planned):
// - Wildcard trap: trap (e<?>) -> { if typeof(e) == typeof<IOError>() ... }
// - Type-safe downcasting: value as TargetType
// - Generic serialization based on runtime type
```

**Implementation Status:**
- ✅ `KEYWORD_TYPEOF` and `KEYWORD_TYPENAME` tokens
- ✅ `TypeofExpression` and `TypenameExpression` AST nodes
- ✅ Parser support for `typeof(expr)` and `typename(expr)` syntax
- ✅ Semantic analysis — `typeof` returns opaque `Type`, `typename` returns `String`
- ✅ LLVM codegen — `typeof` uses `std::hash`, `typename` creates a `{ptr, len}` string struct
- ✅ Comprehensive test coverage in `test/introspection/`

**See:** `test/introspection/` for working examples and `doc/INTROSPECTION_DESIGN.md` for design philosophy

### ✅ **Aspect System (v0.4.2)**

**Philosophy:** Vyb uses **aspects + structs** instead of classes and inheritance. This provides polymorphism, code reuse, and composition without the complexity and pitfalls of OOP class hierarchies. See `doc/TRAIT_SYSTEM_DESIGN.md` for detailed design and rationale.

Simple aspect receivers use `self`; the compiler treats it as the bound `Self` type. Use explicit receiver types like `self<their<Self>>` only when ownership mode matters.

#### **Complete Aspect Example**

```vyb
# Define an aspect (interface): method signatures only, no state
aspect Display {
    show(self)<String> ->
    format(self, prefix<String>)<String> ->
}

# A concrete struct - just data
struct Point {
    x<Int>,
    y<Int>
}

# Bind the aspect to the type, implementing the methods against the data
bind Display -> Point {
    show(self)<String> -> {
        return "Point at (" + self.x + ", " + self.y + ")"
    }

    format(self, prefix<String>)<String> -> {
        return prefix + ": (" + self.x + "," + self.y + ")"
    }
}

# Another type can bind the same aspect
struct Rectangle {
    width<Int>,
    height<Int>
}

bind Display -> Rectangle {
    show(self)<String> -> {
        return "Rectangle " + self.width + "x" + self.height
    }

    format(self, prefix<String>)<String> -> {
        return prefix + ": " + self.width + "x" + self.height
    }
}

main()<Int> -> {
    point<Point> = Point { x = 10, y = 20 }
    rect<Rectangle> = Rectangle { width = 5, height = 7 }

    # Dot-call the bound aspect methods - they read the struct's own fields
    println(point.show())         # "Point at (10, 20)"
    println(point.format("Item")) # "Item: (10,20)"
    println(rect.show())          # "Rectangle 5x7"
    println(rect.format("Item"))  # "Item: 5x7"

    return 0
}
```

#### **Current Implementation Status**

**✅ Fully Working:**
- **Aspect Declarations**: Define interfaces with method signatures and `Self` type
- **Bind Blocks**: Implement aspects for types using `bind Aspect -> Type` syntax
- **Generic Aspects**: Aspects support generic type parameters
- **Generic Bindings**: `bind<T> Display -> Box<T>` with type parameter extraction
- **Generic Functions**: `printItem<T<Display>>(item<T>)` with full monomorphization
- **Method Calls**: Aspect methods called through generic function parameters
- **Aspect Registry**: Semantic analyzer validates and stores aspect definitions
- **Bind Validation**: Full signature checking against aspect requirements
- **Canonical Syntax**: Arrow syntax `->` for bindings

**Why Aspects > Classes:**
- ✅ Multiple aspect implementations (no diamond problem)
- ✅ Composition over inheritance (more flexible)
- ✅ Extension without modification (bind aspects to any type)
- ✅ Static dispatch (zero-cost abstractions)
- ✅ No fragile base class problem
- ✅ Explicit binding makes relationships clear

**See:** `doc/ASPECT_SYSTEM_DESIGN.md` for complete specification and `test/aspect/` for working examples

#### Primitive Types

**Signed Integers:**
| Type | Description | Size | Range/Notes | Example |
|------|-------------|------|-------------|---------|
| `Int` / `Int64` | Signed integer (default) | 64-bit | -9,223,372,036,854,775,808 to 9,223,372,036,854,775,807 | `x<Int> = 42` |
| `Int32` | 32-bit signed integer | 32-bit | -2,147,483,648 to 2,147,483,647 | `count<Int32> = 100` |
| `Int16` | 16-bit signed integer | 16-bit | -32,768 to 32,767 | `small<Int16> = 1000` |
| `Int8` | 8-bit signed integer | 8-bit | -128 to 127 | `byte<Int8> = 127` |

**Unsigned Integers:**
| Type | Description | Size | Range/Notes | Example |
|------|-------------|------|-------------|---------|
| `UInt64` | 64-bit unsigned integer | 64-bit | 0 to 18,446,744,073,709,551,615 | `max<UInt64> = 18446744073709551615u` |
| `UInt32` | 32-bit unsigned integer | 32-bit | 0 to 4,294,967,295 | `count<UInt32> = 100` |
| `UInt16` | 16-bit unsigned integer | 16-bit | 0 to 65,535 | `port<UInt16> = 8080` |
| `UInt8` | 8-bit unsigned integer | 8-bit | 0 to 255 | `byte<UInt8> = 255` |

**Floating Point:**
| Type | Description | Size | Precision | Example |
|------|-------------|------|-----------|---------|
| `Float` / `Float64` | Double precision (default) | 64-bit | IEEE 754 double (~15-17 digits) | `pi<Float> = 3.14159` |
| `Float32` | Single precision | 32-bit | IEEE 754 single (~6-9 digits) | `ratio<Float32> = 1.5` |

**Character Types:**
| Type | Description | Size | Range/Notes | Example |
|------|-------------|------|-------------|---------|
| `Char` | UTF-8 code unit | 8-bit | Single byte (0-255) | `ch<Char> = 65` # 'A' |
| `Rune` | Unicode code point | 32-bit | Full Unicode range (U+0000 to U+10FFFF) | `emoji<Rune> = 128512` # 😀 |

**Other Types:**
| Type | Description | Size | Range/Notes | Example |
|------|-------------|------|-------------|---------|
| `Bool` | Boolean | 1-bit | `true` or `false` | `flag<Bool> = true` |
| `String` | UTF-8 string | Variable | Heap-allocated fat pointer `{ ptr, len }` | `name<String> = "Alice"` |
| `Bytes` | Raw binary data | Variable | Fat pointer for byte sequences `{ ptr, len }` | Future byte literals |
| `Void` | No value | 0-bit | Used for functions that don't return | `print()<Void> -> { ... }` |

#### Collection Types

| Type | Description | Mutability | Example |
|------|-------------|------------|---------|
| `[T; N]` | Fixed-size array | Mutable elements | `nums<[Int; 5]> = [1, 2, 3, 4, 5]` |
| `Vec<T>` | Dynamic array | Mutable elements | `items<Vec<String>> = Vec()` |
| `Tuple<T,U,...>` | Heterogeneous tuple (variadic) | Immutable | `data<Tuple<Int,String,Bool>>` |

#### Ownership Types

| Type | Description | Use Case | Example |
|------|-------------|----------|---------|
| `my<T>` | Unique ownership | Single owner, move semantics | `data<my<Person>> = my(Person{...})` |
| `our<T>` | Shared ownership | Reference counting | `config<our<Settings>> = our(Settings{...})` |
| `their<T>` | Borrowed reference | Non-owning access | `ref<their<Data>> = view(owner)` |
| `loc<T>` | Raw pointer | Freedom operations only | `ptr<loc<Int>> = loc(variable)` |

### ✅ **Canonical Ownership Syntax**

Vyb v0.4.2 features **unified canonical syntax** for ownership and borrowing operations:

#### **Type Annotations**
```vyb
# In variable declarations and function signatures
data<my<String>>     # Unique ownership type
shared<our<Config>>  # Shared ownership type
view<their<Data>>    # Borrowed reference type
```

#### **Value Construction**
```vyb
# Create owned values with my() and our() constructors
unique<my<String>>   = my("owned string")
shared<our<Config>>  = our(Config::new())
result<my<Data>>     = my(compute_data())
```

#### **Borrowing Operations**
```vyb
# Create temporary references with view/borrow/soft functions
readonly<their<String const>> = view(data)     # Immutable borrow
writable<their<String>>       = borrow(data)   # Mutable borrow
weak<mild<Node>>              = soft(shared)   # Weak reference
length<Int>                   = view(data).len()
borrow(data).clear()
```

#### **Legacy Syntax Migration**
All legacy `make_my()`, `make_our()`, and prefix `view expr` / `borrow expr` syntax can be migrated to canonical syntax using the migration tool:

```bash
# Scan for legacy syntax
python3 migrate_syntax.py --scan --directory .

# Apply migrations with backup
python3 migrate_syntax.py --migrate --directory . --backup --report
```

**Migration Results:** ✅ 346 syntax updates applied across 22 files, ensuring consistent canonical syntax throughout the entire codebase.

### ✅ **Memory Management**
- **Ownership types**: `my<T>`, `our<T>`, `their<T>`, `mild<T>` for safe memory handling
- **Mild references**: `mild<T>` created with `soft()` for breaking circular references with `grab()` and `released()` methods
- **Borrowing**: `view(expr)` and `borrow(expr)` for references
- **Freedom operations**: `loc<T>` pointers in `freedom {}` blocks
- **Raw Memory Operations**: Complete `freedom` block system with `loc<T>` pointers
- **Memory Safety**: Borrow checking and lifetime analysis prevent dangling pointers

#### Weak References with mild<T>

Vyb v0.4.4 introduces **`mild<T>`** - weak references that solve circular reference problems without preventing cleanup:

**Why mild<T>?**
- **Break Cycles**: Tree nodes with parent pointers, doubly-linked lists
- **Observer Pattern**: Subjects don't keep observers alive
- **Caches**: Entries that can be collected when memory is needed
- **Safe Access**: Can detect when target object has been destroyed

**Key Methods:**
```vyb
# grab() -> our<T>?
# Attempts to upgrade mild reference to strong reference
# Returns nil if target has been destroyed
parent_strong<our<TreeNode>> = node.parent.grab()

# released() -> Bool
# Check if target object has been freed
# Returns true if destroyed, false if still alive
is_alive<Bool> = !node.parent.released()
```

**Creating Mild References:**
```vyb
# Use soft() to create mild<T> from our<T>
shared_data<our<Config>> = our(Config::new())
shadow<mild<Config>> = soft(shared_data)  # Create mild reference
```

**Example: Tree with Parent Pointers**
```vyb
struct TreeNode {
    value<Int>,
    children<Vec<our<TreeNode>>>,  # Strong refs to children
    parent<mild<TreeNode>>          # Mild ref to parent (breaks cycle)
}

create_child(parent<our<TreeNode>>, value<Int>)<our<TreeNode>> -> {
    return our(TreeNode {
        value: value,
        children: Vec(),
        parent: soft(parent)  # Create weak reference with soft()
    })
}

access_parent(node<our<TreeNode>>)<Int> -> {
    # Safely access parent through weak reference
    if (parent<our<TreeNode>> = node.parent.grab()) {
        return parent.value  # Success - parent still alive
    } else {
        return -1  # Parent was destroyed
    }
}
```

**See:** `doc/OWNERSHIP_MILD.md` for complete documentation and `test/ownership/mild_test.vyb` for examples

### ✅ **Developer Experience**
- **LLVM backend**: JIT execution, AOT object files, and standalone executable generation
- **Comprehensive parser**: Handles all documented syntax including generics, async, aspects
- **Rich error messages**: Clear compilation feedback with file, line, and column info
- **Advanced Test Harness**: Modern parallel test runner with HTML/JSON reporting and triage analysis
- **Debug Information**: Complete DWARF debug metadata for source-level debugging in gdb/lldb
- **Git integration**: Regular commits track development progress
- **Migration Tool**: `python3 migrate_syntax.py` for automated syntax upgrades

### 🔜 **Native Bridge / FFI Status**
Vyb's native bridge to C libraries is the highest-priority upcoming feature:

| Layer | Status | Description |
|-------|--------|-------------|
| **Vyb Runtime** | ✅ Complete | `runtime/vyb_runtime.c` — GC, Vec, String, Math, I/O all linked automatically |
| **LLVM intrinsics** | ✅ Complete | `malloc`, `free`, `memset`, `printf`-style print all registered in JIT |
| **C stdlib (math)** | ✅ Complete | `libm` linked; `sqrt`, `sin`, `cos`, `pow`, etc. all working |
| **C stdlib (I/O)** | ✅ Complete | `libc` linked; I/O built on top of C runtime |
| **`extern "C"` blocks** | ✅ Complete | Declare C functions callable from Vyb (incl. variadic `printf(fmt, ...)`); maps `loc<T>`→`T*` and C aliases (`CInt`, `CString`, `CSize`, …) |
| **`#[repr(C)]` structs** | ✅ Complete | Force C-compatible struct layout for FFI |
| **`vyb bindgen` (MVP)** | ✅ | `vyb bindgen <header.h>` emits importable extern/`repr(C)`/enum bindings from a C subset; libclang-based full parsing is v0.6+ |

**Design goal**: Once `extern "C"` lands, the entire POSIX API becomes available with a thin Vyb wrapper, enabling networking, file I/O, threading, and more without any language-level changes. See `doc/FFI_DESIGN.md` for the complete design.

## Language Overview

Vyb (freedom-1.0 series) is a mature, actively developed systems programming language with name-first syntax, a sized type system (Int8–Int64, UInt8–UInt64, Float32/Float64, Char, Rune, Bytes), compile-time monomorphized generics, aspect/bind polymorphism, pattern matching, `Vec<T>`, and comprehensive collection support. The core language is stable and well tested; Vyb sits roughly midway to its 1.0 milestone (see `doc/FEATURE_STATUS.md` and `doc/ROADMAP.md`).

### Language Features Showcase

```vyb
// Complete language demonstration showing all major features

// Modern struct syntax with typed fields
struct Person {
    name<String>,
    age<Int>,
    scores<Vec<Int>>
}

// Define aspect for gradeable entities
aspect Gradeable {
    letter_grade(self)<String> -> { }
    is_passing(self)<Bool> -> { }
}

// Bind aspect to Person using match in the implementation
bind Gradeable -> Person {
    letter_grade(self)<String> -> {
        // Calculate average score
        total<Int> = 0
        i<Int> = 0
        while (i < self.scores.len()) {
            total = total + self.scores.get(i)
            i = i + 1
        }
        avg<Int> = total / self.scores.len()

        // Match statement with comparison patterns for grade ranges
        match (avg) {
            >= 90 -> return "A",
            >= 80 -> return "B",
            >= 70 -> return "C",
            >= 60 -> return "D",
            ? -> return "F"
        }
    }

    is_passing(self)<Bool> -> {
        grade<String> = self.letter_grade()
        match (grade) {
            "F" -> return false,
            ? -> return true
        }
    }
}

// select expression — Vyb-original: pattern matching that returns a value
get_grade_description(score<Int>)<String> -> {
    grade<String> = select(score) -> {
        >= 90 -> "Excellent",
        >= 80 -> "Good",
        >= 70 -> "Satisfactory",
        >= 60 -> "Passing",
        ? -> "Needs Improvement"
    }
    println(grade)
    return grade
}

// Generic function using aspect bounds
print_student_status<T<Gradeable>>(student<T>)<Void> -> {
    grade<String> = student.letter_grade()
    passing<Bool> = student.is_passing()
    println("Grade: " + grade)
    if (passing) {
        println("Status: PASSING")
    } else {
        println("Status: FAILING")
    }
}

// Resizable collections with full method support
create_person(name<String>, age<Int>)<Person> -> {
    person<Person> = Person {
        name = name,
        age = age,
        scores = Vec()
    }
    person.scores.push(85)
    person.scores.push(92)
    person.scores.push(78)
    return person
}

main()<Int> -> {
    student<Person> = create_person("Alice", 20)

    // Defer for guaranteed cleanup
    defer println("done")

    // Type introspection
    same<Bool> = (typeof(student.age) == typeof(42))

    // Call aspect method through generic function
    print_student_status(student)

    // Use select with comparison patterns
    description<String> = get_grade_description(85)

    return 0
}
```

### Memory Safety & Freedom Operations

Vyb's design philosophy: **FREEDOM over restrictions**. The language provides compiler-managed ownership by default, but empowers programmers with low-level control when needed:

```vyb
// Ownership type syntax (runtime enforcement planned for v0.5)
restricted_memory_example()<Int> -> {
    owned<my<String>> = my("unique data")         // Unique ownership
    shared<our<String>> = our("shared data")      // Shared, ref-counted
    another_ref<our<String>> = shared             // Reference count +1
    view_ref<their<String const>> = view(shared)  // Immutable borrow
    return 42
}

// freedom block — low-level pointer access
freedom_memory_example()<Int> -> {
    x<Int> = 42
    freedom {
        p<loc<Int>> = loc(x)       // Get pointer to x
        println("in freedom block")
    }
    return x
}
```

**FREEDOM Code Guidelines:**
1. Minimize the scope of `freedom` blocks
2. Document all invariants and assumptions
3. Validate pointers before dereferencing
4. Encapsulate freedom operations behind restricted abstractions
5. Test freedom code extensively

### Syntax and Literals

Vyb uses indentation-sensitive syntax with optional braces and semicolons. Whitespace defines blocks, so consistent indentation is key. The unified `name<Type>` syntax provides consistency across all language constructs.

#### Literal Forms

```vyb
// Integer literals
x<Int> = 42                       // 64-bit signed integer (default)
large<Int> = -9223372036854775808  // Full 64-bit range
red<Int> = 0xFF                   // Hexadecimal (base 16)
mask<Int> = 0b11001110            // Binary (base 2)
ud<UInt32> = 255u                 // Unsigned literal suffix
maxU<UInt64> = 18446744073709551615u  // Full UInt64 range (2^64 - 1)

// Sized integers use the same literal forms (0x80 means 128 as a byte).
b<UInt8> = 0x80

// Floating point literals
pi<Float> = 3.14159               // 64-bit IEEE 754 double precision
e<Float> = 2.71828

// Boolean literals
flag<Bool> = true
active<Bool> = false

// String literals
name<String> = "Alice"            // UTF-8 string
greeting<String> = "Hello\nVyb"  // Escape sequences supported
empty<String> = ""

// Array literals (fixed-size)
numbers<[Int; 5]> = [1, 2, 3, 4, 5]
floats<[Float; 3]> = [1.0, 2.5, -3.14]

// Dynamic arrays (Vec<T>)
items<Vec<Int>> = Vec()

// Immutable binding
PI<Float const> = 3.14159
```

#### Syntax Examples

```vyb
// Variable declarations with unified syntax
x<Int> = 42                  // Mutable variable
PI<Float const> = 3.14159    // Immutable constant (name<Type const>)

// Function declarations — name comes first
add(a<Int>, b<Int>)<Int> -> a + b

// Struct definitions with typed fields
struct Point {
    x<Float>,
    y<Float>
}

// Pattern matching with comparison operators and wildcard ?
process_value(val<Int>)<String> -> {
    match (val) {
        0 -> "zero",
        >= 1 -> "positive",
        ? -> "negative"
    }
}

// select — pattern matching that returns a value (Vyb-original)
classify(score<Int>)<String> -> {
    return select(score) -> {
        >= 90 -> "A",
        >= 80 -> "B",
        ? -> "C-or-below"
    }
}

// defer — LIFO scope-exit cleanup
open_and_process()<Int> -> {
    defer println("cleanup")
    println("working")
    return 0
}
```

#### Type System Features

**Current Primitive Types**:
- **Signed integers**: `Int`/`Int64` (default), `Int32`, `Int16`, `Int8`
- **Unsigned integers**: `UInt64`, `UInt32`, `UInt16`, `UInt8`
- **Floating point**: `Float`/`Float64` (default), `Float32`
- **Characters**: `Char` (UTF-8 code unit), `Rune` (Unicode code point)
- **Binary data**: `Bytes` (fat pointer for raw bytes)
- **Other**: `Bool`, `String`, `Void`
- **Integer casts**: `value as Type` between the sized integer types — widening
  sign/zero-extends from the source type, narrowing truncates. E.g. packing eight
  `UInt8` into an `Int64`: `(b0 as Int64) | ((b1 as Int64) << 8) | ...`.
- **Explicit integer assignment**: a variable/field may only change integer
  width or signedness through `as`; a bare in-range constant still fits
  (`x<Int8> = 3`), but an out-of-range constant or any typed value crossing
  width/signedness is a compile error (`x<Int8> = 300`, `x<Int8> = wide<Int>`).

**Current Collection Types**:
- **Fixed arrays**: `[T; N]` with compile-time size
- **Dynamic arrays**: `Vec<T>` resizable collections
- **Tuples**: `Tuple<T,U,...>` variadic heterogeneous types

**Ownership Types**: `my<T>` (unique), `our<T>` (shared), `their<T>` (borrowed), `mild<T>` (mild reference), `loc<T>` (freedom raw pointer)

**Type Aliasing**: All numeric types support multiple naming conventions:
- Vyb style: `Int32`, `Float64`, `UInt8`
- C style: `int32`, `float64`, `uint8`
- LLVM style: `i32`, `f64`, `u8`

### Basic Syntax

Vyb uses clean, expressive syntax with flexible parameter syntax:

```vyb
# Functions support both standard and shorthand parameter syntax

# Standard syntax: explicit parameter<Type> or parameter<Type const>
add_standard(a<Int>, b<Int const>)<Int> -> {
    return a + b
}

# Shorthand syntax: Type directly (implicitly mutable)
add_shorthand(Int a, Int b)<Int> -> {
    return a + b
}

# Mixed syntax: combining both forms in same function
mixed_params(x<Int>, Int y, const Int z)<Int> -> {
    return x + y + z
}

# Complex return types with auto-serialization
get_data()<Int, String, Bool> -> {
    return 42, "result", true
}
# When called from main(), outputs: {"Int":42,"String":"result","Bool":true}
```

### Function Parameters

Vyb supports two parameter syntax styles for maximum flexibility:

```vyb
# Standard syntax: explicit mutability
format_standard(prefix<String>, value<Int const>)<String> -> {
    return prefix + String::from_int(value)
}

# Shorthand syntax: type-first (more concise)
format_shorthand(String prefix, const Int value)<String> -> {
    return prefix + String::from_int(value)
}

# Both produce identical behavior and LLVM IR
```

### Variables and Types

```vyb
# Variables with type inference
x = 42          # Int
name = "Alice"  # String
flag = true     # Bool

# Explicit typing
count<Int> = 0
message<String> = "Hello"

# Immutable values (const is a type modifier)
PI<Float const> = 3.14159
MAX_SIZE<Int const> = 1000
```

### Structs and Data

```vyb
# Define a struct with modern syntax
struct Point {
    x<Int>,
    y<Int>
}

# Create and use with field access
main()<Int> -> {
    p<Point> = Point { x = 10, y = 20 }
    return p.x + p.y  # Returns 30
}
```

### Modules & Import System

Vyb's unique dual import system provides both security and flexibility:

```vyb
# Trusted imports from verified sources
import std::collections::Vec
import utils::math::calculate
import std::io::println

# Flexible imports for development and experimentation
smuggle debug::trace from "github.com/dev/tools"
smuggle experimental::parser from "./local/experiments"

# Use imported symbols
main()<Int> -> {
    numbers<Vec<Int>> = Vec()
    numbers.push(calculate(10))

    println(numbers)  # From trusted std::io
    trace("Debug info")  # From smuggled debug module

    return numbers.get(0)
}
```

**Import Types:**
- **`import`**: For signed, verified modules from registries or project dependencies
- **`smuggle`**: For external, experimental, or development-only modules

Declare dependencies in `vyb.toml`:
```toml
[dependencies]
std = "^1.0.0"                    # Verified registry package
utils = { git = "https://..." }    # External Git repository
local_tools = { path = "../tools" } # Local development dependency
```

**Planned Bundle & Sharing System:**
Fine-grained visibility control with bundles:

```vyb
# Declare module bundles
bundle(math, math.Core)

# Control symbol visibility
share(all) public_function()<Int> -> { ... }      # Public to all
share(math.UI) ui_helper()<Int> -> { ... }        # Only to math.UI bundle
internal_helper()<Int> -> { ... }                 # Private to this file
```

### Arrays and Collections

```vyb
# Resizable vectors with full method support
main()<Int> -> {
    numbers<Vec<Int>> = Vec()
    numbers.push(10)
    numbers.push(20)
    numbers.push(30)

    println(numbers)  # Outputs: { "type": "Vec<Int>", "address": "0x..." }

    length<Int> = numbers.len()    # Gets 3
    first<Int> = numbers.get(0)    # Gets 10
    last<Int> = numbers.pop()      # Gets and removes 30

    return first + last  # Returns 40
}

# Vec iteration - parentheses mandatory
iterate_example()<Int> -> {
    items<Vec<Int>> = Vec()
    items.push(1)
    items.push(2)
    items.push(3)

    sum<Int> = 0
    for (item in items) {
        sum = sum + item
    }

    return sum  # Returns 6
}

# Fixed-size arrays also supported
array_example()<Int> -> {
    fixed<[Int; 3]> = [1, 2, 3]
    return fixed[0]  # Array indexing
}
```

### Control Flow

```vyb
# Conditional expressions with modern syntax
check_sign(x<Int>)<String> -> {
    if (x > 0) {
        return "positive"
    } else if (x < 0) {
        return "negative"
    } else {
        return "zero"
    }
}

# While loops with break/continue
factorial(n<Int>)<Int> -> {
    result<Int> = 1
    i<Int> = 1
    while (i <= n) {
        if (i == 0) {
            continue
        }
        result = result * i
        i = i + 1
        if (result > 1000) {
            break
        }
    }
    return result
}

# Vec iteration with for loops (parentheses mandatory)
sum_vector(numbers<Vec<Int>>)<Int> -> {
    total<Int> = 0
    for (num in numbers) {
        if (num < 0) {
            continue  # Skip negative numbers
        }
        total = total + num
        if (total > 100) {
            break  # Stop if sum exceeds 100
        }
    }
    return total
}

# For loops over the Iterator protocol: any non-identifier iterable
# expression desugars onto `.next()` (e.g. `v.iter()`, or a custom
# `bind Iterator` type). `break`/`continue` work and re-enter `next()`.
# An optional step (`for (x in v.iter(), 2)`) yields every 2nd element.
# The range (`0..n`) and plain-Vec (`for (x in vec)`) forms above are unchanged.
for (x in numbers.iter()) {
    total = total + x
}

# Range-based for loops (inclusive)
count_to_ten()<Int> -> {
    sum<Int> = 0
    for (i in 0..10) {
        sum = sum + i
    }
    return sum  # Returns 55
}

# Pattern matching with match statements
describe_number(x<Int>)<String> -> {
    match (x) {
        0 -> return "zero",
        1 -> return "one",
        42 -> return "the answer",
        ? -> return "some number"
    }
}

# Match arms can execute any statement, including return
# The match statement itself doesn't return a value, but pattern arms can
# return from the enclosing function when the type matches

# Select expressions - elegant pattern matching that returns a value
compute_bonus(level<Int>)<Int> -> {
    # Simple select with naked expressions (auto-return)
    multiplier<Int> = select(level) -> {
        1 -> 10,
        2 -> 20,
        3 -> 30,
        ? -> 5
    };
    return multiplier * 100
}

# Select with complex blocks and explicit pass keyword
process_request(code<Int>)<Int> -> {
    result<Int> = select(code) -> {
        1 -> {
            temp<Int> = 10;
            pass temp
        },
        2 -> {
            x<Int> = 20;
            pass x
        },
        3 -> {
            msg<Int> = 300;
            println(msg);
            pass msg
        },
        ? -> {
            pass 0
        }
    };
    return result
}

# Select expressions combine the best of match and expression values:
# - Naked expressions (1 -> 10) auto-return without 'pass'
# - Complex blocks ({ ... }) use 'pass' keyword to return value
# - 'pass' returns from the block, NOT the enclosing function
# - Type inference from first case ensures type safety
# - Wildcard '?' provides default case handling
```

### Comparison Patterns (Range Matching)

Vyb's pattern matching supports **comparison patterns** for elegant range-based matching with compile-time safety:

```vyb
# Comparison operators in patterns: >, <, >=, <=, ==, !=

# Select expression - returns a value directly
classify_score(score<Int>)<String> -> {
    return select(score) -> {
        >= 90 -> "A",
        >= 80 -> "B",
        >= 70 -> "C",
        >= 60 -> "D",
        ? -> "F"
    }
}

# Match statement - returns from enclosing function
classify_score_match(score<Int>)<String> -> {
    match (score) {
        >= 90 -> return "A",
        >= 80 -> return "B",
        >= 70 -> return "C",
        >= 60 -> return "D",
        ? -> return "F"
    }
}

# Tax rate calculation with select
compute_tax_rate(income<Int>)<Float> -> {
    return select(income) -> {
        < 10000 -> 0.0,
        < 50000 -> 0.15,
        < 100000 -> 0.25,
        ? -> 0.35
    }
}

# Comparison patterns work with floats and integers
categorize_temperature(temp<Float>)<String> -> {
    match (temp) {
        < 0.0 -> return "freezing",
        < 10.0 -> return "cold",
        < 20.0 -> return "mild",
        < 30.0 -> return "warm",
        ? -> return "hot"
    }
}

# Unreachable pattern detection - compiler ERROR
invalid_patterns(x<Int>)<String> -> {
    match (x) {
        > 10 -> return "big",
        > 5 -> return "medium",  # ERROR: unreachable (subsumed by > 10)
        ? -> return "small"
    }
}

# Other unreachable pattern errors:
# - Wildcard before end: match (x) { ? -> "any", 5 -> "five" }  # ERROR
# - Duplicate patterns: match (x) { > 10 -> "a", > 10 -> "b" }  # ERROR
# - Overlapping ranges: match (x) { > 5 -> "a", >= 3 -> "b" }  # ERROR
```

**Comparison Pattern Rules:**
- **Evaluation Order**: Patterns tested top-to-bottom, first-match-wins
- **Type Safety**: Works with `Int`, `Float` types (more types planned)
- **Compile-Time Errors**: Unreachable patterns detected and rejected
- **Wildcard Position**: `?` must be last pattern or compiler ERROR
- **No Gaps Required**: `>= 90`, `>= 80`, `>= 70` is valid (evaluates top-to-bottom)

**Match vs Select with Comparison Patterns:**
- **`select`**: Expression that evaluates to a value - use naked expressions or `pass` keyword
- **`match`**: Statement for side effects - pattern arms can `return` from enclosing function
- Both support identical comparison pattern syntax and unreachable pattern detection
```

### Variadic Tuples

Vyb supports **fully variadic tuple types** that can hold any number of heterogeneous elements (1 to N):

```vyb
# Single-element tuples
get_single()<Tuple<Int>> -> {
    return 42  # Automatically wrapped in tuple struct
}

# Two-element tuples (dual syntax)
get_pair_inline()<Int, String> -> {
    return 10, "hello"  # Inline syntax
}

get_pair_generic()<Tuple<Int, String>> -> {
    return 20, "world"  # Generic syntax (equivalent)
}

# Multi-element tuples with mixed types
get_data()<Tuple<String, Int, Bool, Float>> -> {
    return "status", 200, true, 3.14
}

# Seven-element tuple demonstrating variadic support
get_complex()<Tuple<Int, Int, Bool, String, Int, Bool, Int>> -> {
    return 1, 2, true, "test", 3, false, 4
}

# Tuples work seamlessly with complex types
get_mixed()<Tuple<String, Vec<Int>, Bool>> -> {
    numbers<Vec<Int>> = Vec()
    numbers.push(1)
    numbers.push(2)
    return "data", numbers, true
}

main()<Int> -> {
    # All tuple syntaxes work identically
    single<Tuple<Int>> = get_single()
    pair<Tuple<Int,String>> = get_pair_inline()
    data<Tuple<String,Int,Bool,Float>> = get_data()

    return 0
}
```

**Tuple Features:**
- **Variadic**: Supports 1 to N type parameters (tested up to 7+ elements)
- **Dual Syntax**: Both `(T,U,V)` inline and `Tuple<T,U,V>` generic forms
- **Type Safety**: Full compile-time type checking for all elements
- **Complex Types**: Works with primitives, Strings, Vec, and custom types
- **Auto-wrapping**: Single values automatically wrapped when returning to tuple type
- **LLVM Structs**: Compiled to efficient anonymous struct types

**Current Limitations** (planned for future releases):
- No tuple element access (`.0`, `.1`, etc.)
- No tuple variables (only return values)
- No tuple destructuring
- No tuple serialization output (compiles but values not printed)

## String Theory

Vyb's String type is a production-ready fat pointer implementation with comprehensive method support and natural literal syntax. Unlike C's null-terminated strings or C++'s heavyweight `std::string`, Vyb Strings combine the best of both worlds: efficient representation with modern conveniences.

### String Structure

```vyb
# Internally, String is a fat pointer struct:
struct String {
    ptr: *i8,    # Pointer to null-terminated byte data
    len: i64     # Length (excluding null terminator)
}
```

This design provides:
- **O(1) length queries** - No strlen() scanning needed
- **C interoperability** - Null termination for printf, strstr, etc.
- **Memory efficiency** - Just 16 bytes overhead per string
- **Safe indexing** - Built-in bounds checking

### Natural String Syntax

String literals in Vyb are first-class citizens:

```vyb
# Direct literal assignment
greeting<String> = "Hello, Vyb!"

# Literal concatenation (just works™)
message<String> = "Hello" + " " + "World"

# Method calls on literals
length<Int> = "Vyb".len()                    # Returns 3
first<Int> = "Quantum".char_at(0)            # Returns 'Q' (81)
check<Bool> = "Einstein".starts_with("Ein")  # Returns true

# All without explicit constructors!
```

### Complete Method Reference

**Constructor**
```vyb
# Create from raw bytes (C interop)
name<String> = String::from_bytes("Alice", 5)
raw<String> = String::from_bytes(c_ptr, c_len)
```

**Property Access**
```vyb
msg<String> = "Hello"
length<Int> = msg.len()  # Returns 5, O(1) operation
```

**Substring Operations**
```vyb
text<String> = "Hello World"

# Extract substring (end optional)
hello<String> = text.substring(0, 5)      # "Hello"
world<String> = text.substring(6, 11)     # "World"
rest<String> = text.substring(6)          # "World" (to end)

# Safe character access with bounds checking
first<Int> = text.char_at(0)      # 72 ('H')
space<Int> = text.char_at(5)      # 32 (' ')
invalid<Int> = text.char_at(99)   # 0 (null char for out of bounds)
```

**Search and Comparison**
```vyb
sentence<String> = "The quick brown fox"

# Prefix/suffix checking
has_the<Bool> = sentence.starts_with("The")      # true
has_fox<Bool> = sentence.ends_with("fox")        # true
has_dog<Bool> = sentence.ends_with("dog")        # false

# Substring search
has_quick<Bool> = sentence.contains("quick")     # true
has_slow<Bool> = sentence.contains("slow")       # false

# Empty strings always match
always<Bool> = sentence.starts_with("")          # true
```

**Case Conversion**
```vyb
mixed<String> = "Hello World"

# ASCII case conversion (allocates new string)
upper<String> = mixed.to_upper()   # "HELLO WORLD"
lower<String> = mixed.to_lower()   # "hello world"

# Original unchanged (immutability)
println(mixed)  # Still "Hello World"
```

**String Concatenation**
```vyb
# Using + operator (most natural)
full<String> = "Hello" + " " + "World"

# Chaining operations
result<String> = "Code".to_upper() + " " + "Language".to_lower()
# Result: "CODE language"

# Mixed types (planned with toString())
# message<String> = "Count: " + 42.to_string()
```

### Practical Examples

**Text Processing**
```vyb
process_input(text<String>)<Bool> -> {
    # Validate input
    if (text.len() == 0) {
        return false
    }

    # Check for command prefix
    if (text.starts_with("/")) {
        command<String> = text.substring(1)
        println("Command: " + command)
        return true
    }

    # Search for keywords
    if (text.contains("help")) {
        println("Help requested")
        return true
    }

    return false
}
```

**String Manipulation**
```vyb
format_name(first<String>, last<String>)<String> -> {
    # Capitalize first letter of each name
    first_upper<String> = first.char_at(0).to_string().to_upper() +
                          first.substring(1).to_lower()

    last_upper<String> = last.char_at(0).to_string().to_upper() +
                         last.substring(1).to_lower()

    # Combine with space
    return first_upper + " " + last_upper
}

main()<Int> -> {
    formatted<String> = format_name("ALICE", "wonderland")
    println(formatted)  # "Alice Wonderland"
    return 0
}
```

**Data Validation**
```vyb
validate_email(email<String>)<Bool> -> {
    # Simple email validation
    if (email.len() < 3) {
        return false  # Too short
    }

    if (!email.contains("@")) {
        return false  # No @ symbol
    }

    # Check @ is not first or last
    at_pos<Int> = email.find("@")  # (planned method)
    if (at_pos == 0 || at_pos == email.len() - 1) {
        return false
    }

    return true
}
```

### Memory Management

**Allocation Strategy**
```vyb
# Read-only operations: ZERO allocations
len<Int> = "Hello".len()                    # No malloc
ch<Int> = "World".char_at(0)                # No malloc
check<Bool> = "Test".starts_with("Te")      # No malloc

# Transform operations: Allocate new strings
upper<String> = "hello".to_upper()          # malloc(6) for "HELLO\0"
sub<String> = "Hello World".substring(0, 5) # malloc(6) for "Hello\0"
concat<String> = "A" + "B"                  # malloc(3) for "AB\0"

# Ownership handles cleanup automatically
# (when ownership system is fully integrated)
```

**Bounds Safety**
```vyb
# All index operations are bounds-checked at runtime
text<String> = "Vyb"

safe<Int> = text.char_at(2)      # OK: returns 'n' (110)
safe2<Int> = text.char_at(0)     # OK: returns 'V' (86)

# Out of bounds returns safe defaults
oob1<Int> = text.char_at(-1)     # Returns 0 (null char)
oob2<Int> = text.char_at(100)    # Returns 0 (null char)

# substring returns empty string for invalid bounds
empty<String> = text.substring(10, 20)  # Returns {null, 0}
```

### Performance Characteristics

| Operation | Time | Space | Notes |
|-----------|------|-------|-------|
| `len()` | O(1) | O(1) | Just reads struct field |
| `char_at(i)` | O(1) | O(1) | Bounds-checked array access |
| `starts_with(s)` | O(k) | O(1) | k = prefix length, uses memcmp |
| `ends_with(s)` | O(k) | O(1) | k = suffix length, uses memcmp |
| `contains(s)` | O(n×k) | O(1) | Uses C strstr (KMP-like) |
| `substring(i,j)` | O(k) | O(k) | k = j-i, allocates new buffer |
| `to_upper()` | O(n) | O(n) | Allocates new buffer, ASCII only |
| `to_lower()` | O(n) | O(n) | Allocates new buffer, ASCII only |
| `split(sep)` | O(n×k) | O(n) | k = separator length; allocates a new buffer per part |
| `format(args...)` | O(n+a) | O(n+a) | n = fmt length, a = serialized argument bytes |
| `a + b` | O(n+m) | O(n+m) | Allocates new buffer |

### C Interoperability

All String methods produce null-terminated strings for C compatibility:

```vyb
# Use with C functions (planned FFI)
name<String> = "Alice"
c_str<*i8> = name.to_bytes()  # Get raw pointer

# Compatible with:
# printf("%s", c_str)
# strlen(c_str)
# strcmp(c_str1, c_str2)
# strstr(haystack, needle)
```

### Future Enhancements

**Planned Methods**
- `find(substring: String) -> Int` - Get index of first occurrence
- `parse_int() -> Option<Int>` - Parse string to integer

**Advanced Features**
- **UTF-8 support**: Unicode-aware operations (length, indexing, case)
- **Small string optimization**: Inline strings ≤15 bytes (no heap allocation)
- **String interning**: Share identical string data
- **Rope data structure**: Efficient concatenation for large strings
- **Copy-on-write**: Share data until modification


```vyb
# Simple things are simple
msg<String> = "Hello" + " " + "World"

# Complex things are possible (when needed)
advanced<String> = data
    .to_lower()
    .substring(0, 100)
    .replace("old", "new")  # (planned)
    .trim()                 # (planned)
```

**String Theory Achievement Unlocked:** You now understand Vyb Strings better than most physicists understand actual string theory! 🎻✨

## Error Handling with trap/fail/ensure

Vyb v0.4.2 features an **explicit error handling system** that combines the clarity of exceptions with the safety of Result types. The `trap`/`fail`/`ensure` trio provides compile-time error tracking with zero runtime overhead for the happy path.

### The Philosophy

Traditional error handling offers two flawed extremes:
- **Exceptions**: Hidden control flow, unclear what can fail, runtime overhead
- **Result Types**: Verbose unwrapping, easy to ignore errors, cluttered code

Vyb's approach:
- **Explicit propagation**: Errors visible in type signatures
- **Zero-cost success**: No overhead when operations succeed
- **Pattern matching**: Handle errors elegantly with full type safety
- **Heap allocation**: Errors carry rich context through pointer passing

### Error Fundamentals

Functions declare error returns in their signatures using **failable types**:

```vyb
# Functions that can fail return (T, error_ptr) tuples
divide(a<Int>, b<Int>)<Int> -> {
    if (b == 0) {
        fail 42  # Return error with code 42
    }
    return a / b
}

# The compiler wraps this as: { i64 result, i8* error } in LLVM
```

**Type System Integration:**
- Success path: Returns `(value, null_ptr)` tuple
- Error path: Returns `(undefined, error_ptr)` tuple
- Error pointers carry heap-allocated error structs
- Type IDs enable pattern matching on error types

### The fail Keyword

Create and propagate errors with `fail`:

```vyb
# Simple error codes (primitive types)
fail 404                    # Integer error
fail "not found"            # String error
fail 3.14                   # Float error

# Structured errors (custom types)
struct DivisionError {
    code<Int>,
    dividend<Int>,
    divisor<Int>
}

divide_structured(a<Int>, b<Int>)<Int> -> {
    if (b == 0) {
        fail DivisionError {
            code = 42,
            dividend = a,
            divisor = b
        }
    }
    return a / b
}
```

**Error Memory Layout:**
```
Heap-allocated error struct (16 bytes):
  Offset 0-7:  Type ID hash (i64) - For pattern matching
  Offset 8-15: Error value/data    - Primitive or struct data
```

**Type ID Hashing:**
- `Int` → hash("Int") = -3994496327427856726
- `String` → hash("String") = unique value
- `DivisionError` → hash("DivisionError") = unique value
- Enables runtime type dispatch in trap handlers

### The trap Block

Handle errors with pattern matching:

```vyb
# Basic trap with single error type
compute_safely(x<Int>, y<Int>)<Int> -> {
    result<Int> = {
        value<Int> = divide(x, y)  # May fail
        value * 2
    } trap (e<Int>) -> {
        println("Division failed with code: " + String::from_int(e))
        -1  # Return fallback value
    }
    return result
}

# Multiple error types with pattern matching
process_data(x<Int>)<String> -> {
    {
        value<Int> = risky_operation(x)
        "Success: " + String::from_int(value)
    } trap (e<Int>) -> {
        "Integer error: " + String::from_int(e)
    } trap (e<String>) -> {
        "String error: " + e
    } trap (e<DivisionError>) -> {
        "Division error code: " + String::from_int(e.code)
    }
}

# Wildcard trap - catch any error type
safe_operation(x<Int>)<Int> -> {
    result<Int> = {
        risky_call(x)
    } trap (e<DivisionError>) -> {
        println("Known error: division by zero")
        return 0
    } trap (e<?>) -> {
        println("Unknown error caught by wildcard!")
        return -1
    }
    return result
}

# Multi-type trap - catch union of error types
process_data(input<String>)<Int> -> {
    result<Int> = {
        parse_and_validate(input)
    } trap (e<ParseError | ValidationError>) -> {
        # Handler matches either ParseError or ValidationError
        println("Input processing failed")
        return -1
    } trap (e<NetworkError | TimeoutError>) -> {
        # Different handler for network-related errors
        println("Network issue, retrying...")
        return retry_operation(input)
    }
    return result
}
```

**Pattern Matching Features:**
- Type-safe: Only valid error types accepted
- Exhaustive: Compiler ensures all error types handled
- Field access: Access struct fields in trap handlers (`e.code`, `e.dividend`)
- Wildcard: Use `} trap (e<?>) -> { }` to catch any error type
- Multi-type: Use `} trap (e<Type1 | Type2>) -> { }` to catch union of types

### Error Propagation

Errors automatically propagate through call stacks:

```vyb
# Three-level error propagation
divide(a<Int>, b<Int>)<Int> -> {
    if (b == 0) {
        fail 42  # Create error at bottom
    }
    return a / b
}

compute(x<Int>, y<Int>)<Int> -> {
    result<Int> = divide(x, y)  # Propagates error if divide fails
    return result + 10           # Only reached if divide succeeds
}

main()<Int> -> {
    val1<Int> = compute(10, 2)   # OK: 10/2 + 10 = 15

    val2<Int> = {
        compute(10, 0)  # Fails: division by zero
    } trap (e<Int>) -> {
        println("Caught error: " + String::from_int(e))
        -1  # Return fallback
    }

    return val1 + val2  # Returns 15 + (-1) = 14
}
```

**Propagation Mechanics:**
1. `divide(10, 0)` allocates error on heap, returns `(undef, error_ptr)`
2. `compute` checks error pointer, finds non-null, propagates `(undef, error_ptr)`
3. `main` trap block catches error, extracts type and value
4. Error memory freed after handling

### Untrapped Errors

Errors that escape without trap handlers trigger runtime termination:

```vyb
divide(a<Int>, b<Int>)<Int> -> {
    if (b == 0) {
        fail 42
    }
    return a / b
}

main()<Int> -> {
    result<Int> = divide(10, 0)  # No trap handler!
    return result
}
```

**Runtime Output:**
```
┌─ UNTRAPPED FAILURE ──────────────────────────────────────────┐
│ Error: <runtime error>                                       │
│ Thread: 6472648897627130861                                  │
│ Time: 2025-10-21 10:01:29.810                                │
└──────────────────────────────────────────────────────────────┘

Exit Code: 1
```

**Safety Guarantees:**
- No silent failures
- No undefined behavior
- Clear error location (planned: stack traces)
- Graceful termination

### Struct Errors with Context

Rich error types carry debugging context:

```vyb
struct ValidationError {
    field_name<String>,
    expected<String>,
    actual<String>,
    line_number<Int>
}

validate_input(input<String>)<Bool> -> {
    if (input.len() == 0) {
        fail ValidationError {
            field_name = "input",
            expected = "non-empty string",
            actual = "empty string",
            line_number = 42
        }
    }
    return true
}

main()<Int> -> {
    {
        is_valid<Bool> = validate_input("")
        0
    } trap (e<ValidationError>) -> {
        println("Validation failed:")
        println("  Field: " + e.field_name)
        println("  Expected: " + e.expected)
        println("  Actual: " + e.actual)
        println("  Line: " + String::from_int(e.line_number))
        1
    }
}
```

**Output:**
```
Validation failed:
  Field: input
  Expected: non-empty string
  Actual: empty string
  Line: 42
```

### The ensure Keyword ✅ IMPLEMENTED

The `ensure` keyword provides cleanup/finally semantics, guaranteeing code runs whether the block succeeds or fails:

```vyb
# Implemented in v0.4.2
process_file(path<String>)<String> -> {
    file<File> = open_file(path)

    result<String> = {
        file.read()
    } trap (e<IOError>) -> {
        return ""
    } ensure -> {
        file.close()  # Always runs, success or failure
    }

    return result
}

# Execution order on error:
# 1. Block code executes and fails
# 2. Matching trap handler runs (if present)
# 3. ensure block always runs last

# Implementation details:
# - Parser fully supports } ensure -> { } syntax
# - Codegen inlines ensure blocks into control flow
# - Works with both success and failure paths
# - See: test/trap/test_ensure_simple.vyb
```

### LLVM Code Generation

The error handling system compiles to efficient LLVM IR:

```llvm
; Failable function returning {i64 result, i8* error}
define { i64, ptr } @divide(i64 %a, i64 %b) {
entry:
  %is_zero = icmp eq i64 %b, 0
  br i1 %is_zero, label %error_path, label %success_path

error_path:
  ; Allocate error struct: 8 bytes type_id + 8 bytes value
  %error_mem = call ptr @malloc(i64 16)

  ; Store type ID at offset 0
  %type_id = i64 -3994496327427856726  ; hash("Int")
  store i64 %type_id, ptr %error_mem

  ; Store error value at offset 8
  %value_ptr = getelementptr i8, ptr %error_mem, i64 8
  store i64 42, ptr %value_ptr

  ; Return (undef, error_ptr) tuple
  %result = insertvalue { i64, ptr } undef, ptr %error_mem, 1
  ret { i64, ptr } %result

success_path:
  %quotient = sdiv i64 %a, %b
  ; Return (quotient, null) tuple
  %success = insertvalue { i64, ptr } undef, i64 %quotient, 0
  %success2 = insertvalue { i64, ptr } %success, ptr null, 1
  ret { i64, ptr } %success2
}

; Call site with error checking
define { i64, ptr } @compute(i64 %x, i64 %y) {
entry:
  %call = call { i64, ptr } @divide(i64 %x, i64 %y)
  %value = extractvalue { i64, ptr } %call, 0
  %error = extractvalue { i64, ptr } %call, 1

  ; Check if error occurred
  %has_error = icmp ne ptr %error, null
  br i1 %has_error, label %error_propagate, label %success

error_propagate:
  ; Propagate error up the stack
  %prop = insertvalue { i64, ptr } undef, ptr %error, 1
  ret { i64, ptr } %prop

success:
  %result = add i64 %value, 10
  %ret = insertvalue { i64, ptr } undef, i64 %result, 0
  %ret2 = insertvalue { i64, ptr } %ret, ptr null, 1
  ret { i64, ptr } %ret2
}

; Trap handler with type matching
define i64 @main() {
entry:
  %trap_error = alloca ptr
  store ptr null, ptr %trap_error
  br label %try_block

try_block:
  %call = call { i64, ptr } @compute(i64 10, i64 0)
  %value = extractvalue { i64, ptr } %call, 0
  %error = extractvalue { i64, ptr } %call, 1
  %has_error = icmp ne ptr %error, null
  br i1 %has_error, label %trap_landing, label %try_success

trap_landing:
  ; Load error and check type
  %error_typeid = load i64, ptr %error
  %type_matches = icmp eq i64 %error_typeid, -3994496327427856726
  br i1 %type_matches, label %catch_handler, label %unmatched

catch_handler:
  ; Extract error value from offset 8
  %value_ptr = getelementptr i8, ptr %error, i64 8
  %error_value = load i64, ptr %value_ptr

  ; Free error memory
  call void @free(ptr %error)

  ; Handle error (return -1)
  br label %after_trap

unmatched:
  ; No matching handler - call runtime
  call void @__vyb_runtime_untrapped_error(ptr %error)
  unreachable

try_success:
  br label %after_trap

after_trap:
  %result = phi i64 [ %value, %try_success ], [ -1, %catch_handler ]
  ret i64 %result
}
```

### Performance Characteristics

**Happy Path (No Errors):**
- **Zero allocation**: No heap operations when no errors occur
- **Zero branching overhead**: Modern CPUs predict success path well
- **Single comparison**: `icmp ne ptr %error, null` per call
- **Optimal inlining**: Small functions inline away error checks

**Error Path:**
- **One malloc**: 16-byte allocation for error struct
- **Type matching**: O(1) hash comparison per trap handler
- **Single free**: Clean error memory in trap handler
- **Stack unwinding**: Return through call frames (no exception tables)

**Comparison to Alternatives:**

| Approach | Success Overhead | Error Overhead | Hidden Control Flow | Compile-time Safety |
|----------|-----------------|----------------|---------------------|---------------------|
| **Vyb trap/fail** | ~1 comparison | 1 malloc + type match | No | Yes |
| C++ exceptions | Exception tables | Stack unwinding + allocation | Yes | Partial |
| Rust Result<T,E> | Match overhead | Enum size increase | No | Yes |
| Go error returns | Comparison + check | Allocation | No | Weak (can ignore) |
| Java checked exceptions | Exception tables | Stack trace + allocation | Yes | Partial |

### Error Handling Best Practices

**1. Use Specific Error Types**
```vyb
# Good: Rich context
struct FileError {
    path<String>,
    operation<String>,
    reason<String>
}

# Less good: Generic integer codes
fail 404
```

**2. Handle Errors Close to Source**
```vyb
# Good: Handle immediately if recovery is possible
result<String> = {
    read_file(path)
} trap (e<FileError>) -> {
    println("Using default config due to: " + e.reason)
    default_config()
}

# Sometimes okay: Let errors propagate if caller should decide
data<String> = read_file(path)  # Propagates error to caller
```

**3. Document Error Conditions**
```vyb
# Read configuration from file
#
# Errors:
#   FileError { operation = "open", ... } - File doesn't exist
#   FileError { operation = "read", ... } - Permission denied
#   ParseError { ... } - Invalid TOML format
read_config(path<String>)<Config> -> {
    # Implementation...
}
```

**4. Prefer Structured Errors Over Codes**
```vyb
# Good: Self-documenting
struct NetworkError {
    url<String>,
    status_code<Int>,
    retry_after<Int>
}

# Less good: Requires external documentation
fail 503  # What does this mean?
```

### Current Implementation Status

**✅ In This Release (v0.4.2):**
- fail statements with primitives (Int, Float, String)
- fail statements with custom struct types
- Error heap allocation with type ID + value storage
- Multi-level error propagation through call stacks
- trap blocks with pattern matching on error types
- Struct field access in trap handlers
- Type-safe trap handler matching
- Automatic memory management (malloc/free)
- Untrapped error runtime handler with formatted output
- Complete LLVM codegen for all error operations
- `ensure` keyword for cleanup/finally blocks
- Stack trace capture in error structs (v0.5.1)
- Wildcard trap handlers `} trap (e<?>) -> { ... }`
- Type introspection (`typeof`, `as`) for discriminating multi-type traps

**🔜 Planned Enhancements:**

- Error context chaining (wrap errors with additional context)
- Custom error formatting with Display aspect
- Error metrics and telemetry hooks


### Example: Complete Error Handling

A comprehensive example showing all error handling features:

```vyb
# Define domain-specific error types
struct ParseError {
    input<String>,
    position<Int>,
    expected<String>
}

struct ValidationError {
    field<String>,
    constraint<String>
}

struct DatabaseError {
    query<String>,
    code<Int>
}

# Parse integer from string
parse_int(s<String>)<Int> -> {
    # Actual parsing logic would go here
    if (s.len() == 0) {
        fail ParseError {
            input = s,
            position = 0,
            expected = "non-empty string"
        }
    }
    return 42  # Simplified
}

# Validate age constraint
validate_age(age<Int>)<Bool> -> {
    if (age < 0) {
        fail ValidationError {
            field = "age",
            constraint = "must be non-negative"
        }
    }
    if (age > 150) {
        fail ValidationError {
            field = "age",
            constraint = "must be <= 150"
        }
    }
    return true
}

# Store user in database (can fail)
store_user(name<String>, age<Int>)<Int> -> {
    valid<Bool> = validate_age(age)  # Propagates ValidationError

    # Simulate database operation
    if (age == 42) {
        fail DatabaseError {
            query = "INSERT INTO users",
            code = 1062  # Duplicate entry
        }
    }

    return 1  # User ID
}

# Process user registration with comprehensive error handling
register_user(name<String>, age_str<String>)<String> -> {
    # Multi-level error handling with different error types
    result<String> = {
        # Parse age (may fail with ParseError)
        age<Int> = parse_int(age_str)

        # Store user (may fail with ValidationError or DatabaseError)
        user_id<Int> = store_user(name, age)

        "User registered with ID: " + String::from_int(user_id)

    } trap (e<ParseError>) -> {
        msg<String> = "Parse failed: " + e.expected +
                     " at position " + String::from_int(e.position)
        println(msg)
        "PARSE_ERROR"

    } trap (e<ValidationError>) -> {
        msg<String> = "Validation failed for " + e.field +
                     ": " + e.constraint
        println(msg)
        "VALIDATION_ERROR"

    } trap (e<DatabaseError>) -> {
        msg<String> = "Database error " + String::from_int(e.code) +
                     " in query: " + e.query
        println(msg)
        "DATABASE_ERROR"
    }

    return result
}

main()<Int> -> {
    # Test success path
    result1<String> = register_user("Alice", "25")
    println(result1)  # "User registered with ID: 1"

    # Test ParseError
    result2<String> = register_user("Bob", "")
    println(result2)  # "PARSE_ERROR"

    # Test ValidationError
    result3<String> = register_user("Charlie", "-5")
    println(result3)  # "VALIDATION_ERROR"

    # Test DatabaseError
    result4<String> = register_user("Dave", "42")
    println(result4)  # "DATABASE_ERROR"

    return 0
}
```

**Key Takeaways:**
- **Type Safety**: Each error type is distinct and statically checked
- **Composability**: Errors propagate through multiple function layers
- **Clarity**: Error handling is explicit in code structure
- **Performance**: Only allocates memory when errors actually occur
- **Ergonomics**: Pattern matching makes error handling elegant

The Vyb error handling system achieves the rare combination of **safety, performance, and ergonomics** that makes robust error handling a joy rather than a chore.

## Build System

Vyb uses CMake for building:

```bash
# Clean build
mkdir -p build && cd build
cmake .. && make clean && make -j

# Quick rebuild (from project root)
make -C build -j

# Run a single file
build/vyb test/string/string_test.vyb

# Run demos and examples
python3 test/run_examples.py --vyb build/vyb
cmake --build build --target run-examples

# Run the milestone gate
python3 test/run_milestone_tests.py --vyb build/vyb
cmake --build build --target run-milestone
```

## Test Harness

Vyb includes a modern, comprehensive test harness for managing 391+ test files:

### Quick Testing
```bash
# Run all tests with parallel execution
./test_harness.py

# Run specific categories
./test_harness.py --category parser,semantic

# Generate comprehensive reports
./test_harness.py --html-report report.html --json-report results.json

# Run with filtering and analysis
./test_harness.py --priority high --exclude-slow --workers 8
```

### Test Analysis and Triage
```bash
# Analyze test failures and create triage plan
./triage_tool.py results.json

# Generate markdown triage report
./triage_tool.py results.json --format markdown --output triage.md

# Focus on critical issues only
./triage_tool.py results.json --priority critical,high
```

### Test Features
- **544 Test Files**: Comprehensive coverage across all language features
- **Parallel Execution**: Multi-threaded test runner for fast feedback
- **Rich Reporting**: HTML, JSON, and console output with detailed metrics
- **Smart Categorization**: Automatic test categorization and filtering
- **Failure Analysis**: Pattern recognition and triage plan generation
- **Performance Tracking**: Execution time analysis and slow test detection

## Project Structure

```
Vyb/
├── src/              # C++ source code
│   ├── main.cpp      # Entry point with LLVM JIT
│   ├── lexer.cpp     # Tokenization
│   ├── parser.cpp    # Syntax analysis
│   └── ast.cpp       # Abstract syntax tree
├── include/vyb/      # Header files
├── test/             # Vyb test programs
├── examples/         # Example programs
├── doc/              # Documentation
└── build/            # Build output
```

## Examples

### Simple Programs

**Hello World:**
```vyb
main()<Void> -> {
    println("Hello, Vyb!")
}
```

**Mathematical Computation:**
```vyb
fibonacci(n<Int>)<Int> -> {
    if (n <= 1) {
        return n
    } else {
        return fibonacci(n - 1) + fibonacci(n - 2)
    }
}

main()<Int> -> {
    return fibonacci(10)  # Returns 55
}
```

**Data Processing with Auto-Serialization:**
```vyb
struct Result {
    success<Bool>,
    value<Int>,
    message<String>
}

process_data(x<Int>)<Result> -> {
    if (x > 0) {
        return Result {
            success = true,
            value = x * 2,
            message = "Processing successful"
        }
    } else {
        return Result {
            success = false,
            value = 0,
            message = "Invalid input"
        }
    }
}

main()<Result> -> {
    return process_data(21)
}
# Outputs structured JSON for the Result
```

## JSON Serialization & Deserialization

Vyb v0.4.4 includes a **complete JSON serialization system** with bidirectional conversion between structs and JSON:

### Automatic Serialization

```vyb
struct Person {
    name<String>,
    age<Int>,
    active<Bool>
}

main()<Int> -> {
    person<Person> = Person { name: "Alice", age: 30, active: true }

    # Serialize to JSON string
    json<String> = person.to_string()
    println(json)  # Output: {"name": "Alice", "age": 30, "active": true}

    return 0
}
```

### JSON Deserialization

```vyb
# Deserialize JSON back to struct
person2<Person> = Person::from_string(json)

# Access fields normally
println(person2.name)  # Output: Alice
println(person2.age.to_string())  # Output: 30
```

### Supported Types

- **Primitives**: Int, Float, Bool, String
- **Struct Fields**: All primitive types within structs
- **Round-Trip**: Full struct → JSON → struct → field access
- **Type Safety**: Runtime validation of JSON structure

### Auto-Serialization for main() Returns

One of Vyb's standout features is automatic serialization of complex return types from `main()`:

- **Simple integers**: Return as exit codes (`main()<Int> -> { return 42 }`)
- **Complex types**: Automatically serialize to JSON-like format
- **Tuples**: `main()<Int,String> -> { return 10, "hello" }` outputs `[10, "hello"]`
- **Structs**: Full structured output with field names and values

### Implementation Details

The JSON system is built on:
- **Runtime Type Metadata**: Global type registry with field information
- **C Runtime Functions**: `__vyb_complex_to_json()` and `__vyb_complex_from_json()`
- **Type Registration**: Automatic registration via global constructors
- **String Conversion**: Seamless char* to VybString{data, length} struct conversion

See `test/json/` for comprehensive examples and `runtime/vyb_type_metadata.c` for implementation.

This makes Vyb excellent for data processing scripts, API services, and configuration management.

## Memory Safety

Vyb provides multiple memory management strategies:

```vyb
# Unique ownership (like Rust's Box)
owned<my<String>> = my("unique data")

# Shared ownership (reference counted)
shared<our<String>> = our("shared data")
another_ref<our<String>> = shared  # Reference count incremented

# Borrowing (non-owning references)
view_ref<their<String>> = view(shared)      # Immutable borrow
mut_ref<their<String>> = borrow(owned)      # Mutable borrow

# Raw pointers for freedom operations
freedom {
    x<Int> = 42
    ptr<loc<Int>> = loc(x)  # Get pointer to x
    at(ptr) = 99           # Modify through pointer
}
```

## Future Roadmap

Vyb (freedom-1.0 series) compiles through JIT, AOT object files, and native executables via LLVM. The items below track what shipped in recent milestones and what still stands on the path to 1.0.

### 🔜 **Near-Term Priorities (v0.5)**
1. ✅ **FFI / `extern "C"`** (landed): `extern "C"` blocks, C type aliases, `#[repr(C)]` struct layout, freedom-gated JIT calls, native `--link` support, variadic C functions, and the `vyb bindgen` MVP (`vyb bindgen some.h`). Full libclang-based bindgen remains for v0.6+. See `doc/FFI_DESIGN.md`.
2. ✅ **Module System** (landed): `import`/`smuggle`/`bundle`/`share`, module-path resolution, and stdlib auto-discovery. See `doc/bundles_and_sharing.md`.
3. ✅ **Lambda/Closure Codegen** (landed): lambdas lower to a uniform closure value (`struct { env, fn }`); free variables are captured into a heap-allocated environment. Beyond by-value capture, all capture forms are implemented: **mutable** captures write back to the outer variable through its address (plain and compound assignment), **move** (`my<T>`) captures transfer ownership into the closure (use-after-move diagnostics), and **`our<T>`** captures bump the shared value's strong count. Capturing closures work standalone, returned from functions, and as `fn` arguments to the Vec `map`/`filter`/`reduce` combinators. `|x<Int>| -> x * 2`.
4. ✅ **Error Propagation Phases 2-5** (landed): `fail` propagation through call stacks and wildcard `trap (e<?>)`.
5. ✅ **Enum / Sum Types**: C-like enums (`enum Direction { North, South, East, West }`) and data-carrying tagged unions (`enum Shape { Circle(Float), Rect(Float, Float) }`) are implemented. C-like enums are first-class typed values (render as `Direction::East`) backed by a scalar `i64` tag, so an extern parameter typed `Direction` interoperates with a C integer-backed enum; every enum value also exposes its positional variant tag via `.tag`; `match`/`select` dispatch on variants with exhaustiveness enforcement.
6. ✅ **`Option<T>` and `Result<T,E>`**: Built-in sum types built on enums (`Some(T)`/`None`, `Ok(T)`/`Err(E)`) with match/select dispatch and exhaustiveness; transitional `core::option::OptionInt` bridge retained for source-compat.

### 📋 **Long-Term Goals**
- **Ownership Runtime Enforcement**: Borrow checking, move semantics, `mild<T>` control block
- **Iterator Aspect**: `aspect Iterator { type Item; next(self)<Option<Item>> }` for `for` loop generalization
- **Associated Types**: `aspect Iterator { type Item }` (requires aspect system extension)
- **Enhanced Async/Await**: Real event loop, `spawn`, typed channels, async lambdas
- **Self-Hosting**: Vyb compiler written in Vyb
- **Package Manager**: `vyb.toml`, `vyb build`, dependency resolution
- **Language Server (LSP)**: Completion, go-to-definition, diagnostics in editors
- **REPL**: `vyb repl` backed by ORC JIT

**Current Status**: All core language features work end-to-end. See `TODO.md` for the high-level 1.0 plan and `UPDATE_LOG.md` for the current source-biased implementation audit.

## Testing & Development Tools

Vyb v0.4.2 includes a **modern, comprehensive testing infrastructure** designed for efficient development and quality assurance:

### 🧪 **Modern Test Harness**

The new Python-based test harness provides enterprise-grade testing capabilities:

```bash
# Basic test run with parallel execution
python3 test_harness.py --parallel

# Full test suite with HTML reporting and triage analysis
python3 test_harness.py --parallel --html-report --triage --performance

# Test specific patterns or directories
python3 test_harness.py --filter "async" --verbose
python3 test_harness.py --directory test/units --timeout 30
```

#### **Test Harness Features**
- **Parallel Execution**: Multi-threaded test running for faster feedback
- **Rich Reporting**: HTML reports with test results, timing, and failure analysis
- **JSON Export**: Machine-readable test data for CI/CD integration
- **Triage Analysis**: Automatic failure pattern recognition and prioritization
- **Performance Tracking**: Test execution timing and performance regression detection
- **Flexible Filtering**: Run specific tests, directories, or patterns
- **Progress Tracking**: Real-time progress bars and status updates
- **Error Context**: Detailed failure information with context and suggestions

#### **Test Statistics**
- **Total Tests**: 400+ comprehensive test cases (growing)
- **Coverage Areas**: Language features, control flow, error handling, type system, math, strings, introspection
- **Test Types**: Feature tests (with `@expect: pass`), future-feature docs (with `@expect: fail`), parser tests
- **Success Rate**: >90% pass rate maintained across all implemented features

### 🔧 **Syntax Migration Tools**

Automated tools ensure codebase consistency and syntax standardization:

```bash
# Scan for legacy syntax patterns
python3 migrate_syntax.py --scan --directory . --report

# Apply canonical syntax migrations with backup
python3 migrate_syntax.py --migrate --directory . --backup
```

#### **Migration Capabilities**
- **Legacy Detection**: Identifies `make_my()`, `make_our()`, and function-call borrowing
- **Automatic Conversion**: Converts to canonical `my()`, `our()`, `view`, `borrow` syntax
- **Backup Creation**: Preserves original files before modification
- **Comprehensive Reporting**: Detailed migration reports with change summaries
- **Pattern Recognition**: Smart detection of syntax inconsistencies across file types

### 📊 **Triage Analysis Tool**

Automated failure analysis and development prioritization:

```bash
# Generate triage report from test results
python3 triage_tool.py test_results.json --output triage_report.html
```

#### **Triage Features**
- **Pattern Recognition**: Groups similar failures for efficient debugging
- **Priority Assignment**: Ranks issues by impact and frequency
- **Root Cause Analysis**: Identifies common failure patterns
- **Debugging Recommendations**: Suggests investigation strategies
- **Progress Tracking**: Monitors issue resolution over time

### 📈 **Development Workflow**

The integrated toolchain supports efficient development:

1. **Write Code**: Use canonical syntax with ownership types
2. **Run Tests**: `python3 test_harness.py --parallel --triage`
3. **Check Syntax**: `python3 migrate_syntax.py --scan`
4. **Debug Issues**: Use triage reports for prioritized debugging
5. **Commit Changes**: Regular Git commits with test validation

## Architecture

Vyb is built on solid foundations:

- **Frontend**: Hand-written recursive descent parser
- **AST**: Rich abstract syntax tree with source location tracking
- **Backend**: LLVM for code generation and ORC JIT execution
- **Type System**: Strong static typing with canonical ownership syntax
- **Runtime**: LLVM ORC JIT (LLJIT) with auto-serialization support
- **Test Infrastructure**: Modern parallel test harness with comprehensive reporting

## Contributing

Vyb is actively developed with regular commits tracking progress:

1. **Language Features**: Add new syntax, types, or operations
2. **Standard Library**: Implement core modules and utilities
3. **Testing**: Create comprehensive test cases
4. **Documentation**: Improve guides and examples
5. **Performance**: Optimize compilation and runtime

See `doc/` directory for detailed design documents and RFCs.

## Recent Progress

**v0.4.2 (freedom-1.0 series)**: Generic function monomorphization and FREEDOM blocks
- ✅ **Generic Functions**: Complete LLVM monomorphization system for generic functions with bounded type parameters
  - **Type Parameter Substitution**: `T → Point` during codegen
  - **On-Demand Instantiation**: Generate specialized functions like `printItem_Point` from templates
  - **Method Resolution**: Call aspect methods on generic parameters (`item.show()` where `item: T<Display>`)
  - **Caching**: Reuse monomorphized functions for same type combinations
  - **Integration**: Works seamlessly with aspects, traits, and generic structs
- ✅ **Select Expressions**: Pattern matching expressions with `select(expr) -> { pattern -> result };` syntax
  - **Naked expressions**: Simple values auto-return without `pass` keyword
  - **Complex blocks**: Use `pass` keyword to return from block without exiting function
  - **Type inference**: First case determines result type for entire select
  - **Pattern matching**: Exact equality patterns with wildcard `?` support
- ✅ **Canonical Syntax Unification**: Complete migration to unified `my()`/`our()` constructors and `view`/`borrow` operators
- ✅ **Modern Test Harness**: Parallel test runner managing 391+ tests with HTML/JSON reporting and failure triage
- ✅ **Syntax Migration Tools**: Automated migration from legacy to canonical syntax with comprehensive reporting
- ✅ **Match Statements**: Complete pattern matching with `->` arrow syntax and `?` wildcard; no-match results in NOP
- ✅ **Break/Continue**: Loop control flow statements working in all loop types
- ✅ **Vec<T> Collections**: Fully functional resizable arrays with all methods (`new`, `push`, `pop`, `len`, `get`)
- ✅ **Modern Struct Syntax**: Updated to `field<Type>` syntax with perfect field access
- ✅ **Complete Binary Operations**: All arithmetic, comparison, logical, and bitwise operators
- ✅ **Dual Parameter Syntax**: Both `var<Type> name` and `Type name` forms working seamlessly
- ✅ **Member Access**: Object field access (`obj.field`) and array indexing (`arr[index]`)
- ✅ **Auto-serialization**: Complex return types with smart JSON-like output
- ✅ **Async/Await**: Complete asynchronous programming support with Future<T> types
- ✅ **Debug Infrastructure**: Full LLVM debug metadata with async state machine debugging

**Language Status**: Vyb (freedom-1.0 series, tracked as v0.5.x) is an actively developed systems programming language with unified canonical syntax, a sized type system (Int8–Int64, UInt8–UInt64, Float32/Float64, Char, Rune, Bytes), compile-time monomorphized generics, aspect/bind polymorphism, a `fail`/`trap` error system, JIT/AOT/native codegen, and a modern test harness. The core language is stable and well tested; development continues toward the 1.0 milestone — see `doc/FEATURE_STATUS.md` for the current feature matrix.

## Getting Help

- **Documentation**: See `doc/` for language design and internals
- **Examples**: Check `examples/` for working programs
- **Tests**: Review `test/` for feature demonstrations
- **Issues**: Report bugs or request features on GitHub

---

## Appendix

### A. EBNF Grammar

Vyb's syntax is defined by a comprehensive EBNF grammar reflecting v0.4.2 capabilities:

```ebnf
// Conventions:
//   IDENTIFIER:        Represents a valid identifier token.
//   INTEGER_LITERAL:   Represents an integer literal token.
//   FLOAT_LITERAL:     Represents a float literal token.
//   STRING_LITERAL:    Represents a string literal token.
//   BOOLEAN_LITERAL:   Represents 'true' or 'false'.
//   'keyword':         Denotes a literal keyword.
//   { ... }:           Represents zero or more occurrences (Kleene star).
//   [ ... ]:           Represents zero or one occurrence (optional).
//   ( ... | ... ):     Represents a choice (alternation).
//   ... ::= ... :      Defines a production rule.

// Module Structure
module                 ::= { module_item } EOF
module_item            ::= import_statement
                         | smuggle_statement
                         | struct_declaration
                         | enum_declaration
                         | bind_declaration
                         | function_declaration
                         | variable_declaration
                         | constant_declaration
                         | type_alias_declaration
                         | aspect_declaration
                         | statement

// Import System (import/smuggle)
import_statement       ::= 'import' path [ 'as' IDENTIFIER ] [';']
smuggle_statement      ::= 'smuggle' path [ 'as' IDENTIFIER ] [';']
path                   ::= IDENTIFIER { ('::' | '.') IDENTIFIER }

// Type Declarations
struct_declaration     ::= [ 'pub' ] [ 'template' '<' type_parameter_list '>' ]
                           'struct' IDENTIFIER '{' { struct_field_declaration } '}'
struct_field_declaration ::= [ 'pub' ] IDENTIFIER '<' type '>' [ '=' expression ] [';']


field_declaration      ::= [ 'pub' ] IDENTIFIER '<' type '>' [ '=' expression ] [';']

enum_declaration       ::= [ 'pub' ] [ 'template' '<' type_parameter_list '>' ]
                           'enum' IDENTIFIER '{' { enum_variant } '}'
enum_variant           ::= IDENTIFIER [ '(' type_list ')' ] [ '=' expression ] ','?

bind_declaration       ::= [ 'template' '<' type_parameter_list '>' ]
                           'bind' type [ '->' type ] '{' { method_declaration } '}'

aspect_declaration     ::= [ 'pub' ] 'aspect' IDENTIFIER [ template_parameters ]
                           '{' { method_signature } '}'

// Function Declarations
function_declaration   ::= [ 'pub' ] [ 'template' '<' type_parameter_list '>' ] [ 'async' ]
                           IDENTIFIER '(' [ parameter_list ] ')' '<' type_list '>' '->'
                           ( block_statement | expression [';'] | statement )
                           [ 'throws' type_list ]

method_declaration     ::= [ 'pub' ] [ 'static' ] [ 'template' '<' type_parameter_list '>' ] [ 'async' ]
                           IDENTIFIER '(' [ parameter_list ] ')' '<' type '>' '->'
                           ( block_statement | expression [';'] ) [ 'throws' type_list ]

method_signature       ::= [ 'async' ] IDENTIFIER '(' [ parameter_list ] ')'
                           '<' type_list '>' '->' ';' [ 'throws' type_list ]

constructor_declaration::= [ 'pub' ] 'new' [ template_parameters ]
                           '(' [ parameter_list ] ')' [ 'throws' type_list ]
                           ( block_statement | '=>' expression [';'] )

// Variable Declarations
variable_declaration   ::= [ 'pub' ] IDENTIFIER '<' type '>' [ '=' expression ] [';']
constant_declaration   ::= [ 'pub' ] IDENTIFIER '<' type 'const' '>' '=' expression [';']
type_alias_declaration ::= [ 'pub' ] 'type' IDENTIFIER [ template_parameters ] '=' type [';']

// Parameters and Templates
type_parameter_list    ::= type_parameter { ',' type_parameter }
type_parameter         ::= IDENTIFIER [ ':' type_bounds ] | expression
type_bounds            ::= type { '+' type }
template_parameters    ::= '<' type_parameter_list '>'

parameter_list         ::= parameter { ',' parameter }
parameter              ::= [ 'const' ] IDENTIFIER '<' type '>' [ '=' expression ]

type_list              ::= type { ',' type }

// Statements
statement              ::= expression_statement
                         | block_statement
                         | if_statement
                         | for_statement
                         | while_statement
                         | loop_statement
                         | match_statement
                         | return_statement
                         | break_statement
                         | continue_statement
                         | pass_statement
                         | defer_statement
                         | try_statement
                         | variable_declaration
                         | constant_declaration
                         | pattern_assignment_statement
                         | scoped_statement
                         | throw_statement

expression_statement   ::= expression [';']
block_statement        ::= '{' { statement } '}'

if_statement           ::= 'if' expression ( block_statement | statement_without_block )
                           { 'else' 'if' expression ( block_statement | statement_without_block ) }
                           [ 'else' ( block_statement | statement_without_block ) ]

for_statement          ::= 'for' pattern 'in' expression block_statement
while_statement        ::= 'while' expression block_statement
loop_statement         ::= 'loop' block_statement

match_statement        ::= 'match' '(' expression ')' '{' match_arm* '}'
match_arm              ::= pattern '->' ( expression | block_statement | statement_without_block ) ','?

select_expression      ::= 'select' '(' expression ')' '->' '{' select_arm* '}' ';'
select_arm             ::= pattern '->' ( expression | block_statement ) ','?

return_statement       ::= 'return' [ expression ] [';']
break_statement        ::= 'break' [ IDENTIFIER ] [ expression ] [';']
continue_statement     ::= 'continue' [ IDENTIFIER ] [';']
pass_statement         ::= 'pass' expression [';']
defer_statement        ::= 'defer' ( expression_statement | block_statement )
throw_statement        ::= 'throw' expression [';']
scoped_statement       ::= 'scoped' block_statement

try_statement          ::= 'try' block_statement { trap_clause } [ 'finally' block_statement ]
trap_clause            ::= 'trap' '(' IDENTIFIER '<' type '>' ')' '->' block_statement

pattern_assignment_statement ::= pattern '=' expression [';']
statement_without_block ::= expression_statement | return_statement | break_statement
                          | continue_statement | throw_statement

// Patterns
pattern                ::= comparison_pattern
                         | IDENTIFIER [ '@' pattern ]
                         | literal
                         | '?'
                         | path '{' [ field_pattern { ',' field_pattern } [','] ] '}'
                         | path '(' [ pattern_list ] ')'
                         | '[' [ pattern_list ] ']'
                         | '(' pattern_list ')'
                         | '&' [ 'const' ] pattern

comparison_pattern     ::= ( '==' | '!=' | '<' | '<=' | '>' | '>=' ) expression

field_pattern          ::= IDENTIFIER ':' pattern | IDENTIFIER
pattern_list           ::= pattern { ',' pattern }

// Expressions
expression             ::= assignment_expression
assignment_expression  ::= conditional_expression [ assignment_operator assignment_expression ]
assignment_operator    ::= '=' | '+=' | '-=' | '*=' | '/=' | '%=' | '&=' | '|=' | '^=' | '<<=' | '>>='

conditional_expression ::= logical_or_expression [ '?' expression ':' conditional_expression ]
                         | if_expression

logical_or_expression  ::= logical_and_expression { '||' logical_and_expression }
logical_and_expression ::= bitwise_or_expression { '&&' bitwise_or_expression }
bitwise_or_expression  ::= bitwise_xor_expression { '|' bitwise_xor_expression }
bitwise_xor_expression ::= bitwise_and_expression { '^' bitwise_and_expression }
bitwise_and_expression ::= equality_expression { '&' equality_expression }
equality_expression    ::= relational_expression { ( '==' | '!=' ) relational_expression }
relational_expression  ::= range_expression { ( '<' | '<=' | '>' | '>=' | 'is' | 'as' ) range_expression }
range_expression       ::= shift_expression [ '..' shift_expression ]
shift_expression       ::= additive_expression { ( '<<' | '>>' ) additive_expression }
additive_expression    ::= multiplicative_expression { ( '+' | '-' ) multiplicative_expression }
multiplicative_expression ::= unary_expression { ( '*' | '/' | '%' ) unary_expression }

unary_expression       ::= ( '!' | '-' | '+' | '*' | 'await' | 'throw' ) unary_expression
                         | primary_expression

primary_expression     ::= literal
                         | path_expression
                         | '(' expression ')'
                         | call_expression
                         | member_access_expression
                         | index_access_expression
                         | list_comprehension
                         | array_literal
                         | array_construction
                         | tuple_literal
                         | struct_literal
                         | lambda_expression
                         | select_expression
                         | 'self' | 'super'

if_expression          ::= 'if' expression block_statement 'else' ( block_statement | if_expression )

literal                ::= INTEGER_LITERAL | FLOAT_LITERAL | STRING_LITERAL | BOOLEAN_LITERAL | 'null'

path_expression        ::= path [ type_arguments ]
call_expression        ::= primary_expression '(' [ argument_list ] ')' [ '?' ]
argument_list          ::= expression { ',' expression }

member_access_expression ::= primary_expression ( '.' | '?.' | '::' ) IDENTIFIER
index_access_expression  ::= primary_expression '[' expression ']' [ '?' ]

list_comprehension     ::= '[' expression 'for' pattern 'in' expression [ 'if' expression ] ']'
array_literal          ::= '[' [ expression { ',' expression } [','] ] ']'
                         | '[' expression ';' expression ']'
array_construction     ::= ArrayType '(' ')'

tuple_literal          ::= '(' [ expression { ',' expression } [ ',' ] ] ')'

struct_literal         ::= [ path_expression ] '{' [ struct_literal_field { ',' struct_literal_field } [ ',' ] ] '}'
struct_literal_field   ::= IDENTIFIER (':' | '=') expression | IDENTIFIER

lambda_expression      ::= [ 'async' ] ( '|' [ parameter_list ] '|' | IDENTIFIER ) '<' type '>'
                           ( '=>' expression | block_statement )

// Type System
Type                   ::= BaseType [ 'const' ] [ '?' ]
BaseType               ::= IDENTIFIER
                         | OwnershipWrapper '<' Type '>'
                         | ArrayType
                         | TupleType
                         | FunctionType
                         | '(' Type ')'

OwnershipWrapper       ::= 'my' | 'our' | 'their' | 'ptr' | 'loc'

ArrayType              ::= '[' Type [ ';' Expression ] ']'
TupleType              ::= '(' [ Type { ',' Type } [ ',' ] ] ')'
FunctionType           ::= [ 'async' ] '(' [ Type { ',' Type } ] ')' '<' Type '>' '->'
                           [ 'throws' TypeList ]

type_arguments         ::= '<' type_argument_list '>'
type_argument_list     ::= type_argument { ',' type_argument }
type_argument          ::= Type | Expression

// Borrowing Intrinsics
BorrowExpr             ::= 'borrow' '(' Expression ')'
                         | 'view' '(' Expression ')'
```

**Key EBNF Features:**
- **Unified Syntax**: All declarations use `name<Type>` pattern
- **Comparison Patterns**: `>= expr`, `<= expr`, etc. for match/select
- **Select Expressions**: Pattern matching that returns values with `pass` keyword
- **Ownership Types**: `my<T>`, `our<T>`, `their<T>`, `mild<T>`, `loc<T>` wrappers
- **Import/Smuggle**: Dual module system for verified vs. flexible imports
- **Async/Await**: Full async function and expression support
- **Multiple Returns**: Functions can return multiple values (tuples)

### B. Memory Model Reference

**Ownership Types:**
- `my<T>`: Unique ownership, RAII cleanup
- `our<T>`: Shared ownership, reference counted
- `their<T>`: Non-owning borrow, lifetime checked
- `mild<T>`: Mild reference, can detect destruction via `grab()` and `released()`

**Borrowing Operations:**
- `view(expr)`: Creates `their<T const>` immutable borrow
- `borrow(expr)`: Creates `their<T>` mutable borrow
- `soft(expr)`: Creates `mild<T>` mild reference from `our<T>`

**Freedom Operations:**
- `loc<T>`: Raw pointer type
- `loc(expr)`: Get pointer to expression
- `at(ptr)`: Dereference pointer (read/write)
- `from<loc<T>>(expr)`: Pointer type conversion

### C. Auto-Serialization Reference

Vyb automatically serializes complex return types from `main()`:

**Simple Returns:**
- `main()<Int> -> { return 42 }` → Exit code 42
- `main()<String> -> { return "hello" }` → Outputs: hello

**Complex Returns:**
- `main()<Int,String> -> { return 42, "hello" }` → Outputs: [42, "hello"]
- Struct returns → JSON with field names and values
- Vec returns → JSON array representation

**Customization:**
Implement `Serialize` aspect for custom serialization behavior.

## Glossary

**AST** (Abstract Syntax Tree)
Tree representation of source code structure produced by the parser.

**Borrow Checking**
Compile-time analysis ensuring references don't outlive the data they point to.

**Bundle**
Planned namespace grouping for fine-grained module visibility control.

**Import**
Secure module inclusion from verified, signed sources.

**JIT** (Just-In-Time)
Runtime compilation of code to native machine instructions for performance.

**LLVM**
Low Level Virtual Machine - compiler infrastructure used by Vyb's backend.

**Monomorphization**
Compile-time process of creating specialized versions of generic functions/types.

**Ownership**
Memory management system using `my<T>`, `our<T>`, `their<T>`, `mild<T>` types.

**Pattern Matching**
`match` statements that destructure and test values against patterns.

**Smuggle**
Flexible module inclusion from external, potentially unverified sources.

**Template**
Compile-time generic construct parameterized by types or constants.

**Vec<T>**
Resizable array collection with methods like `push()`, `pop()`, `len()`, `get()`.

---

## License

Apache License - see LICENSE file for details.

---

*Vyb (freedom-1.0 series): A mature systems programming language with a comprehensive sized type system, unified name-first syntax, pattern matching, compile-time monomorphized generics, resizable collections, an aspect/bind polymorphism system, and a unique import/smuggle module system — actively developed toward the 1.0 milestone.*
