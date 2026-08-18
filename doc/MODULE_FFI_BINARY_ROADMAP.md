# Module System, FFI, and Binary Generation Roadmap

**Status:** Planning Document - Strategic Breakdown
**Created:** October 22, 2025
**Priority:** HIGH - Foundation for production-ready Vyb

## Executive Summary

This document breaks down three interconnected systems critical for Vyb's evolution from a JIT-only language to a full systems programming language:

1. **Module System** (import/smuggle/bundles) - Code organization and visibility
2. **FFI System** (C bindings) - Integration with existing ecosystems
3. **Binary Generation** - Ahead-of-time compilation and deployment

Each system is broken into **phases** with **concrete implementation tasks** and **clear dependencies**.

---

## Part 1: Module System (import/smuggle/bundles)

### Overview

Enable multi-file Vyb programs with controlled visibility using the bundles & sharing system (see `bundles_and_sharing.md`).

### Current State

- ✅ AST nodes exist: `ImportDeclaration`, `ImportSpecifier` (see `doc/AST_Declarations.md`)
- ✅ Parser recognizes `import` keyword (basic skeleton)
- ✅ Bundle/sharing design documented (`doc/bundles_and_sharing.md`)
- ❌ No semantic analysis for imports
- ❌ No module loading/caching
- ❌ No visibility checking
- ❌ No `smuggle` implementation

### Phase 1.1: Basic Import Infrastructure (v0.5.0)

**Goal:** Load and compile other Vyb files without visibility checks.

**Tasks:**
1. **Module Registry**
   - Create `ModuleRegistry` class in semantic analyzer
   - Store: `map<string, Module>` (path → compiled module)
   - Track: module dependencies, circular import detection
   - Location: `include/vyb/semantic.hpp`, `src/vre/semantic.cpp`

2. **Module Loading**
   - Implement `loadModule(path: string) -> Module*`
   - Search paths: relative to current file, then standard library paths
   - File reading and parsing
   - Cache compiled modules (don't re-parse)
   - Error handling: file not found, parse errors

3. **Symbol Resolution**
   - Implement `resolveImport(importPath: vector<string>) -> Module*`
   - Support: `import foo::bar::baz` → find `foo/bar/baz.vyb`
   - Build symbol table: function names, struct names, global vars
   - Export symbol table from each module

4. **Naive Import (No Visibility)**
   - Make ALL symbols from imported module available
   - Update symbol lookup in semantic analyzer
   - Handle name collisions (error or fully-qualified names)
   - Test: `import test::module` → can call `module::function()`

**Deliverables:**
- Multi-file compilation works
- Basic import syntax functional
- Circular dependency detection
- Test suite: `test/modules/test_basic_import.vyb`

**Dependencies:** None (can start immediately)

---

### Phase 1.2: Bundle Declaration Parsing (v0.5.1)

**Goal:** Parse and store bundle memberships from `bundle(...)` directives.

**Tasks:**
1. **Bundle AST Node**
   - Create `BundleDeclaration` AST node
   - Fields: `vector<vector<string>> bundlePaths` (e.g., `sort.Core` → ["sort", "Core"])
   - Location: `include/vyb/ast.hpp`, `src/ast.cpp`

2. **Parser Extension**
   - Extend `parseTopLevelDeclaration()` to recognize `bundle(...)`
   - Parse comma-separated bundle paths: `bundle(sort.Core, sort.UI)`
   - Special case: `bundle(all)` → empty bundlePaths means "all"
   - Must appear before any other declarations

3. **Module Metadata**
   - Add `bundles: vector<string>` to `Module` class
   - Store flattened bundle names: `["sort.Core", "sort.UI"]`
   - Default: empty vector = unbundled module

4. **Bundle Validation**
   - Check bundle names are valid identifiers
   - Warn if multiple `bundle(...)` declarations
   - Error if `bundle(...)` appears after other declarations

**Deliverables:**
- Parser recognizes `bundle(...)` syntax
- Module stores bundle membership
- Test suite: `test/modules/test_bundle_parsing.vyb`

**Dependencies:** Phase 1.1 (Module Registry)

---

### Phase 1.3: Share Directive Parsing (v0.5.1)

**Goal:** Parse `share(...)` prefixes on declarations.

**Tasks:**
1. **Share Annotation**
   - Add `shareWith: vector<string>` to `Declaration` base class
   - Empty vector = private (not shared)
   - Special value: `"all"` = shared with everyone
   - Store bundle names: `["sort.Core", "sort.Database"]`

2. **Parser Extension**
   - Extend declaration parsing to check for `share(...)` prefix
   - Parse before `fn`/`struct`/`class`/`var` keywords
   - Support: `share(all)`, `share(bundle1, bundle2)`
   - Store in AST node's `shareWith` field

3. **Validation**
   - Verify share targets are valid bundle identifiers
   - Check share directive appears before declaration
   - Warn if sharing with bundles the module doesn't belong to

**Deliverables:**
- Parser recognizes `share(...)` syntax
- Declarations store visibility metadata
- Test suite: `test/modules/test_share_parsing.vyb`

**Dependencies:** Phase 1.2 (Bundle parsing)

---

### Phase 1.4: Visibility Checking (v0.5.2)

**Goal:** Enforce bundle-based visibility rules during import resolution.

**Tasks:**
1. **Import Validation Algorithm**
   ```cpp
   bool canImport(Module* importer, Declaration* target) {
       // No share directive → private
       if (target->shareWith.empty()) return false;

       // share(all) → always visible
       if (contains(target->shareWith, "all")) return true;

       // Check bundle overlap
       for (string& importerBundle : importer->bundles) {
           if (contains(target->shareWith, importerBundle)) {
               return true;
           }
       }

       return false;
   }
   ```

2. **Symbol Table Filtering**
   - When importing module, filter exported symbols
   - Only include symbols where `canImport()` returns true
   - Build separate public vs private symbol tables

3. **Import Error Messages**
   - Clear errors when importing invisible symbols
   - Suggest: "Symbol 'foo' is private. Did you mean to add share(yourBundle)?"
   - Show available bundles in error message

4. **Smuggle Implementation**
   - Parse `smuggle` keyword (same as `import` syntax)
   - Skip visibility checks for smuggle
   - Add all symbols from module regardless of sharing
   - Warning: "Using smuggle bypasses visibility checks"

**Deliverables:**
- Full bundle/sharing visibility enforcement
- `smuggle` keyword functional
- Comprehensive error messages
- Test suite: `test/modules/test_visibility.vyb`

**Dependencies:** Phase 1.3 (Share directives)

---

### Phase 1.5: Module Path Resolution (v0.5.3)

**Goal:** Support flexible module path strategies.

**Tasks:**
1. **Search Path Configuration**
   - Command-line flag: `--module-path /path/to/modules`
   - Environment variable: `VYB_MODULE_PATH`
   - Default paths: `./`, `../`, `/usr/lib/vyb/modules/`
   - Standard library path: `${VYB_INSTALL}/stdlib/`

2. **Path Resolution Algorithm**
   - Try: relative to current file
   - Try: each directory in module search path
   - Try: standard library directory
   - Fail with clear error: "Module 'foo::bar' not found in any search path"

3. **Module Caching**
   - Key by absolute file path (not relative)
   - Track modification times (optional: recompile on change)
   - Invalidate cache on file change (for REPL/JIT mode)

4. **Circular Dependency Detection**
   - Track import stack during compilation
   - Error if `A imports B imports C imports A`
   - Show full dependency chain in error message

**Deliverables:**
- Robust module loading with search paths
- Circular dependency protection
- Test suite: `test/modules/test_module_paths.vyb`

**Dependencies:** Phase 1.4 (Visibility checking)

---

#### Implementation snapshot (2026-05-24)

- Import loading is now managed by a source-level `ModuleRegistry` model keyed by
  canonical absolute module path.
- Registry records include source path, parsed AST root, imported module keys,
  state (`Unresolved`/`Parsing`/`Resolved`/`Failed`), and original import
  spelling for diagnostics.
- Search precedence for `import a::b::c`:
  1. Importing file directory
  2. `--module-path <dir>` (repeatable, CLI order)
  3. `VYB_MODULE_PATH` (colon-separated)
  4. Auto-discovered stdlib root
- Module path convention: try `<root>/a/b/c.vyb`, then `<root>/a/b/c/mod.vyb`.
- Stdlib auto-discovery order:
  1. `VYB_STDLIB` (if set)
  2. Relative to compiler executable (`<exe_dir>/../stdlib`, then `<exe_dir>/stdlib`)

---

### Phase 1.6: Standard Library Modules (v0.6.0)

**Goal:** Organize standard library into importable modules.

**Tasks:**
1. **Module Structure**
   ```
   stdlib/
     core/
       String.vyb     - String utilities
       Vec.vyb        - Vector operations
       Int.vyb        - Integer utilities
     io/
       File.vyb       - File operations
       Console.vyb    - stdin/stdout
     math/
       Arithmetic.vyb - Basic math
       Trig.vyb       - Trigonometry
     collections/
       HashMap.vyb
       BTree.vyb
   ```

2. **Auto-Import Core**
   - Automatically import `core::*` in every file
   - No need for explicit `import core::String`
   - Can be disabled with `#[no_auto_import]`

3. **Module Documentation**
   - Add doc comments to all stdlib functions
   - Generate HTML documentation from comments
   - Tool: `vyb doc` to build documentation

**Deliverables:**
- Organized standard library
- Core auto-import
- Documentation generation
- Test suite: Use stdlib modules in tests

**Dependencies:** Phase 1.5 (Module paths)

---

## Part 2: FFI System (C Bindings)

### Overview

Enable Vyb to call C functions and use C libraries (libc, POSIX, external deps).

### Current State

- ✅ LLVM backend can emit C-compatible function calls
- ✅ `freedom` blocks provide unsafe operations
- ❌ No `extern "C"` declarations
- ❌ No C header parsing
- ❌ No C struct layout compatibility
- ❌ No automatic C type mapping

### Phase 2.1: extern "C" Declarations (v0.5.0)

**Goal:** Manually declare C functions callable from Vyb.

**Tasks:**
1. **FFI Declaration Syntax**
   ```vyb
   extern "C" {
       fn printf(format: *i8, ...) -> Int
       fn malloc(size: i64) -> *i8
       fn free(ptr: *i8) -> Void
   }
   ```

2. **AST Extension**
   - Create `ExternDeclaration` node
   - Fields: `language: string` (only "C" supported initially)
   - Fields: `declarations: vector<FunctionDeclaration>`
   - C functions have no body (just signature)

3. **Parser Extension**
   - Recognize `extern "C" { ... }` blocks
   - Parse function signatures without bodies
   - Support C calling conventions (cdecl by default)

4. **Type Mapping**
   ```
   Vyb Type       C Type
   --------       ------
   Int            int64_t / long long
   Int32          int32_t / int
   Float          double
   Float32        float
   Bool           bool / _Bool
   *T             T* (raw pointer)
   *i8            char*
   Void           void
   ```

5. **LLVM Codegen**
   - Mark extern functions with C calling convention
   - No name mangling for extern "C" functions
   - Link against C standard library by default

**Deliverables:**
- `extern "C"` syntax functional
- Can call libc functions (printf, malloc, free)
- Test suite: `test/ffi/test_extern_c.vyb`

**Dependencies:** None (can start immediately)

---

### Phase 2.2: C Struct Interop (v0.5.1)

**Goal:** Define Vyb structs with C-compatible memory layout.

**Tasks:**
1. **C Layout Attribute**
   ```vyb
   #[repr(C)]
   struct Point {
       x: Float32,
       y: Float32
   }

   extern "C" {
       fn draw_point(p: *Point) -> Void
   }
   ```

2. **Memory Layout Rules**
   - `#[repr(C)]` forces C struct layout
   - No padding optimization (follow C ABI)
   - Field order preserved exactly as written
   - Compatible with C struct passing by pointer

3. **Parser Extension**
   - ✅ Recognize `#[repr(C)]` attribute before struct
   - ✅ Store in `StructDeclaration::reprC`
   - ✅ Validate: no generics, no ownership-qualified fields, no Vyb runtime fields such as `String`

4. **Codegen Extension**
   - ✅ LLVM struct with declaration-order unpacked target layout
   - ⏳ Use `StructLayout::offsetOfElement()` for deeper ABI reporting
   - ✅ Pass by pointer to C functions

**Deliverables:**
- ✅ C-compatible struct layout for the supported subset
- ✅ Can pass repr(C) structs to C functions by pointer
- ✅ Test suite: `test/ffi/repr_c_struct_basic.vyb`, `test/ffi/repr_c_struct_rejects_vyb_string.vyb`, `test/ffi/repr_c_struct_rejects_generic.vyb`, `test/ffi/repr_c_struct_rejects_ownership.vyb`, `test/ffi/extern_c_struct_by_pointer.vyb`

**Dependencies:** Phase 2.1 (extern "C")

---

### Phase 2.3: Variadic Function Support (v0.5.2)

**Goal:** Call C variadic functions like `printf()`.

**Tasks:**
1. **Variadic Syntax**
   ```vyb
   extern "C" {
       fn printf(format: *i8, ...) -> Int
   }

   fn main() -> Int {
       printf("Hello %s, number %d\n", "world", 42)
       return 0
   }
   ```

2. **Type Checking**
   - Variadic args bypass normal type checking
   - Warn on obvious type mismatches (Int vs Float)
   - Trust programmer (like C)

3. **LLVM Codegen**
   - Use `FunctionType::get(..., /*isVarArg=*/true)`
   - Pass variadic args as-is after fixed params
   - Follow target ABI (va_list handling)

4. **Common Wrappers**
   - Provide type-safe wrappers in stdlib
   - Example: `print(msg: String)` wraps `printf()`
   - Hide unsafe variadic calls behind safe API

**Deliverables:**
- Variadic C functions callable
- Can use printf, scanf, etc.
- Test suite: `test/ffi/test_variadic.vyb`

**Dependencies:** Phase 2.1 (extern "C")

---

### Phase 2.4: C Header Binding Generator (v0.6.0)

**Goal:** Automated tool to generate Vyb bindings from C headers.

**Tasks:**
1. **Tool: `vyb bindgen`**
   ```bash
   vyb bindgen /usr/include/stdio.h > stdio.vyb
   ```

2. **libclang Integration** *(landed as `vyb bindgen --full`; see `test/bindgen/full_preproc.vyb`)*
   - Use libclang to parse C headers
   - Extract function declarations, structs, enums
   - Generate Vyb `extern "C" { }` blocks

3. **Type Translation**
   - Automatic C → Vyb type mapping
   - Handle typedefs, function pointers
   - Generate `#[repr(C)]` structs

4. **Manual Overrides**
   - Config file for custom type mappings
   - Exclude certain symbols
   - Rename conflicting names

**Deliverables:**
- `vyb bindgen` tool functional
- Can generate bindings for any C library
- Pre-generated bindings for libc, POSIX
- Documentation: How to use bindgen

**Dependencies:** Phase 2.2 (C struct interop)

---

### Phase 2.5: Standard C Library Wrappers (v0.6.1)

**Goal:** Safe Vyb wrappers around common C functions.

**Tasks:**
1. **Core Wrappers**
   ```vyb
   // stdlib/sys/Libc.vyb
   share(all)
   module sys::Libc

   fn malloc(size: i64) -> *i8 {
       extern "C" fn c_malloc(size: i64) -> *i8
       return c_malloc(size)
   }

   fn free(ptr: *i8) {
       extern "C" fn c_free(ptr: *i8) -> Void
       c_free(ptr)
   }
   ```

2. **I/O Module**
   - File operations: open, read, write, close
   - Buffered I/O: fopen, fread, fwrite, fclose
   - Error handling: errno, strerror
   - Safe wrappers: `File::open(path: String) trap (e<IOError>)`

3. **String Module**
   - C string utilities: strlen, strcmp, strcpy
   - Safe conversions: `String::from_c_str(ptr: *i8)`
   - Memory management: auto-free on String drop

4. **Process Module**
   - System calls: fork, exec, wait
   - Environment: getenv, setenv
   - Exit codes: exit, atexit

**Deliverables:**
- Safe Vyb stdlib wrapping C
- No need for users to write `extern "C"`
- Test suite: Use C wrappers in Vyb programs

**Dependencies:** Phase 2.4 (bindgen tool)

---

## Part 3: Binary Generation (AOT Compilation)

### Overview

Compile Vyb programs to native executables instead of JIT-only.

### Current State

- ✅ LLVM IR generation works (JIT mode)
- ✅ Can link against C standard library
- ❌ No object file emission
- ❌ No executable linking
- ❌ No optimization passes

### Phase 3.1: Object File Emission (v0.5.0)

**Goal:** Compile Vyb to `.o` object files.

**Tasks:**
1. **LLVM Object File Writer**
   - Use `TargetMachine::addPassesToEmitFile()`
   - Output `.o` file with relocatable code
   - Support multiple targets: x86-64, ARM, RISC-V

2. **Command-Line Interface**
   ```bash
   vyb compile main.vyb -o main.o
   vyb compile module.vyb -o module.o
   ```

3. **Target Triple**
   - Detect host target: `x86_64-pc-linux-gnu`
   - Allow override: `--target aarch64-apple-darwin`
   - Set code model, relocation model

4. **Optimization Levels**
   - `-O0`: No optimization (fast compile)
   - `-O1`: Basic optimization
   - `-O2`: Moderate optimization (default)
   - `-O3`: Aggressive optimization

**Deliverables:**
- Can compile Vyb to `.o` files
- Multiple optimization levels
- Test: Compile and inspect with `objdump`

**Dependencies:** None (can start immediately)

---

### Phase 3.2: Static Linking (v0.5.1)

**Goal:** Link object files into standalone executables.

**Tasks:**
1. **Linker Invocation**
   ```bash
   vyb build main.vyb -o main
   # Internally:
   # 1. Compile main.vyb -> main.o
   # 2. Link: ld main.o -lc -o main
   ```

2. **System Linker Integration**
   - Use system linker (ld, lld, gold)
   - Pass object files + libraries
   - Default: link against libc, libm
   - Handle platform differences (Linux vs macOS)

3. **Static vs Dynamic Linking**
   - Default: dynamic linking (`-lc`)
   - Option: static linking (`--static`)
   - Bundle runtime if needed

4. **Multi-File Compilation**
   ```bash
   vyb build main.vyb module.vyb util.vyb -o myapp
   ```
   - Compile each file to `.o`
   - Link all `.o` files together
   - Resolve imports during linking

**Deliverables:**
- `vyb build` produces executables
- Can run without `vyb` runtime (standalone)
- Test: Run compiled binary on different machines

**Dependencies:** Phase 3.1 (Object file emission)

---

### Phase 3.3: Optimization Pipeline (v0.5.2)

**Goal:** Apply LLVM optimization passes for performance.

**Tasks:**
1. **Pass Manager Setup**
   - Create `PassManager` and `FunctionPassManager`
   - Add standard LLVM passes (mem2reg, instcombine, etc.)
   - Target-specific passes (vectorization, loop opts)

2. **Optimization Levels**
   ```
   -O0: No optimization
   -O1: Basic cleanup
   -O2: Default (moderate)
   -O3: Aggressive + vectorization
   -Os: Optimize for size
   -Oz: Optimize for minimal size
   ```

3. **LTO (Link-Time Optimization)**
   - Generate LLVM bitcode for each module
   - Run global optimization at link time
   - Flag: `--lto` or `--lto=thin`

4. **Benchmarking**
   - Compare JIT vs AOT performance
   - Measure optimization impact
   - Profile-guided optimization (future)

**Deliverables:**
- Optimized binaries significantly faster
- LTO support for whole-program optimization
- Performance benchmarks

**Dependencies:** Phase 3.2 (Static linking)

---

### Phase 3.4: Debug Information (v0.6.0)

**Goal:** Emit DWARF debug info for debuggers (gdb, lldb).

**Tasks:**
1. **LLVM DIBuilder**
   - Create `DIBuilder` for debug metadata
   - Emit line number information
   - Emit variable locations (local vars)
   - Function names and parameters

2. **Source Location Tracking**
   - Map every LLVM instruction to source location
   - Store: filename, line, column
   - Preserve through optimization passes

3. **Debugger Integration**
   ```bash
   vyb build --debug main.vyb -o main
   gdb ./main
   (gdb) break main
   (gdb) run
   (gdb) print myVar
   ```

4. **Debug Flags**
   - `-g`: Emit debug info
   - `-g0`: No debug info
   - `-g1`: Minimal info (line numbers only)
   - `-g3`: Full info (includes macros, etc.)

**Deliverables:**
- Can debug Vyb programs with gdb/lldb
- Source-level debugging (step, breakpoint, inspect)
- Test: Debug session in gdb

**Dependencies:** Phase 3.3 (Optimization pipeline)

---

### Phase 3.5: Package Building (v0.6.1)

**Goal:** Build multi-module projects with dependencies.

**Tasks:**
1. **Project Manifest: `vyb.toml`**
   ```toml
   [package]
   name = "myapp"
   version = "0.1.0"

   [dependencies]
   http = "1.0"
   json = "2.3"

   [[bin]]
   name = "myapp"
   path = "src/main.vyb"
   ```

2. **Build Tool: `vyb build`**
   - Read `vyb.toml`
   - Compile all source files
   - Download dependencies
   - Link into executable or library

3. **Dependency Resolution**
   - Central registry (like crates.io)
   - Version constraints: `"^1.0"`, `">=2.0"`
   - Lock file: `vyb.lock` (reproducible builds)

4. **Library Types**
   - Static library: `.a` (archive)
   - Dynamic library: `.so` / `.dylib` / `.dll`
   - Vyb library: `.vyblib` (pre-compiled module)

**Deliverables:**
- `vyb build` handles complex projects
- Dependency management system
- Can publish/download packages
- Test: Build multi-module app with deps

**Dependencies:** Phase 3.4 (Debug information)

---

## Implementation Strategy

### Recommended Order

**Milestone 1: Basic Modules (v0.5.0)**
- Phase 1.1: Basic Import Infrastructure
- Phase 2.1: extern "C" Declarations
- Phase 3.1: Object File Emission

**Milestone 2: Visibility & FFI (v0.5.1-0.5.2)**
- Phase 1.2: Bundle Declaration Parsing
- Phase 1.3: Share Directive Parsing
- Phase 1.4: Visibility Checking
- Phase 2.2: C Struct Interop
- Phase 2.3: Variadic Functions

**Milestone 3: Compilation Pipeline (v0.5.2-0.5.3)**
- Phase 3.2: Static Linking
- Phase 3.3: Optimization Pipeline
- Phase 1.5: Module Path Resolution

**Milestone 4: Production Ready (v0.6.0-0.6.1)**
- Phase 1.6: Standard Library Modules
- Phase 2.4: C Header Binding Generator
- Phase 2.5: Standard C Library Wrappers
- Phase 3.4: Debug Information
- Phase 3.5: Package Building

### Parallelization Opportunities

Can work on simultaneously:
- **Track A**: Module system (Phase 1.x)
- **Track B**: FFI system (Phase 2.x)
- **Track C**: Binary generation (Phase 3.x)

Phases within each track are sequential, but tracks are mostly independent until Phase 1.6/2.5 integration.

### Testing Strategy

For each phase:
1. **Unit Tests**: Test individual components in isolation
2. **Integration Tests**: Test interactions between systems
3. **End-to-End Tests**: Full programs using the feature
4. **Regression Tests**: Ensure old features still work

Example test layout:
```
test/
  modules/
    test_basic_import.vyb
    test_bundle_parsing.vyb
    test_visibility.vyb
    test_circular_deps.vyb
  ffi/
    test_extern_c.vyb
    test_c_struct.vyb
    test_variadic.vyb
  binary/
    test_object_file.sh
    test_linking.sh
    test_optimization.sh
```

---

## Open Questions

### Module System
1. **Precompiled Modules**: Should we cache compiled modules as `.vybc` files?
2. **Incremental Compilation**: How to detect which modules need recompilation?
3. **Namespacing**: How to handle symbols with same name from different modules?

### FFI System
4. **C++ Interop**: Should we support `extern "C++"` for C++ libraries?
5. **Callbacks**: How to pass Vyb closures to C functions expecting function pointers?
6. **Thread Safety**: How to handle C libraries that aren't thread-safe?

### Binary Generation
7. **Cross-Compilation**: How to simplify cross-compiling for other platforms?
8. **Stripping**: Should we automatically strip debug symbols in release builds?
9. **Code Signing**: Integration with platform code signing (macOS, Windows)?

### General
10. **Backwards Compatibility**: How to handle breaking changes in module system?
11. **Performance**: What's the acceptable compilation speed target?
12. **Tooling**: What IDE/editor integrations are priorities?

---

## Success Metrics

For each phase, track:
- **Compilation Speed**: How long to compile 1000 lines?
- **Binary Size**: How big are optimized executables?
- **Runtime Performance**: JIT vs AOT performance comparison
- **API Stability**: Breaking changes per release
- **Test Coverage**: % of features with tests

---

## Related Documents

- `doc/bundles_and_sharing.md` - Detailed bundle/sharing design
- `doc/AST_Declarations.md` - Import AST node specifications
- `doc/VRE.md` - Vyb Runtime Environment design

---

*This is a living document. Update as implementation progresses and requirements evolve.*
