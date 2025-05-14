# Vyn Runtime Design (Preliminary)

**Last Updated:** May 13, 2025

**Note:** This document outlines a preliminary design for the Vyn runtime. It is based on current understanding of the language features (as defined in `AST.md` and `vyn.hpp`), example code, and overall project goals. This design is subject to significant change and refinement as development progresses, particularly with the onset of LLVM integration.

## 1. Introduction

The Vyn runtime environment (VRE) will be responsible for executing compiled Vyn code. The primary goal is to create an efficient, safe, and potentially embeddable runtime. Initially, the focus will be on compiling Vyn to native code via LLVM, which will influence many runtime design decisions.

## 2. Core Goals

*   **Performance:** Leverage LLVM for optimization and native code generation.
*   **Safety:** Provide memory safety and type safety, aligning with Vyn's language features (e.g., ownership, optionals, results).
*   **Interoperability:** Allow seamless calling of C functions and potentially expose Vyn functions to C.
*   **Concurrency:** (Future Goal) Design with concurrency in mind, likely through async/await patterns and message passing.

## 3. Memory Management

Vyn aims for memory safety without a traditional garbage collector by default.

*   **Ownership and Borrowing:** The primary mechanism, similar to Rust. The compiler will enforce these rules at compile time.
    *   `Box<T>`: For heap-allocated data with a single owner.
    *   References (`&T`, `&mut T`): For borrowed data.
*   **Automatic Reference Counting (ARC):** For shared ownership scenarios (`Shared<T>` or similar), ARC might be implemented. This would involve runtime overhead for reference count updates.
*   **Manual Management:** Unsafe blocks might allow for manual memory management, but this should be rare.
*   **Stack Allocation:** Value types (structs, enums, primitives) will primarily be stack-allocated where possible.

## 4. Type System and Data Representation

Vyn's static type system will be mapped to efficient runtime representations.

*   **Primitive Types:**
    *   `i8, i16, i32, i64, i128, isize`: Mapped directly to LLVM integer types.
    *   `u8, u16, u32, u64, u128, usize`: Mapped directly to LLVM integer types.
    *   `f32, f64`: Mapped to LLVM floating-point types.
    *   `bool`: Mapped to `i1`.
    *   `char`: UTF-32, mapped to `i32`.
    *   `string`: Likely a struct containing a pointer to a UTF-8 encoded byte array, length, and capacity (e.g., `{ ptr: *u8, len: usize, cap: usize }`).
*   **Structs:**
    *   Represented as LLVM struct types.
    *   Memory layout will be determined by the compiler, potentially with padding for alignment.
    *   Field access will be direct memory offsets.
*   **Enums:**
    *   **Simple Enums (C-like):** Represented as integers.
    *   **Tagged Unions (Enums with data):** Represented as a struct containing a tag (integer) and a union of the possible data types. The layout will be optimized to minimize space, e.g., by placing the tag and data in a way that leverages alignment.
        ```
        // Vyn: enum Option<T> { Some(T), None }
        // Runtime (conceptual):
        // struct Option<T> {
        //   tag: u8, // 0 for None, 1 for Some
        //   data: union {
        //     // no data for None
        //     value: T, // for Some
        //   }
        // }
        ```
*   **Tuples:** Similar to structs, represented as anonymous LLVM struct types.
*   **Arrays:** Fixed-size, contiguous memory.
*   **Slices (`[T]`):** Represented as a "fat pointer" containing a pointer to the data and a length (e.g., `{ ptr: *T, len: usize }`).
*   **Function Pointers:** Direct LLVM function pointers.
*   **Traits (Interfaces):**
    *   **Static Dispatch:** For monomorphized generics, no runtime overhead.
    *   **Dynamic Dispatch (Trait Objects):** Represented as a fat pointer containing:
        1.  A pointer to the actual object data.
        2.  A pointer to a Virtual Table (vtable) containing function pointers for the trait methods.
        ```
        // Vyn: let x: dyn MyTrait = MyStruct {};
        // Runtime (conceptual):
        // struct TraitObject {
        //   data_ptr: *void,
        //   vtable_ptr: *VTable_MyTrait
        // }
        // struct VTable_MyTrait {
        //   method1_ptr: fn(*void, ...),
        //   method2_ptr: fn(*void, ...),
        //   ...
        // }
        ```
*   **`any` type:** (Future) Could be implemented similar to trait objects but with a more general type information system, potentially involving runtime type information (RTTI).

## 5. Execution Model

*   **Compilation:** Vyn source code -> Vyn AST -> LLVM IR -> Native Machine Code.
*   **Entry Point:** A `main` function will be the entry point of execution.
*   **Modules:** Compiled into object files and linked together.
*   **Function Calls:** Standard native calling conventions will be used, managed by LLVM.
    *   Arguments passed via registers or stack as per the ABI.
    *   Return values also via registers or stack.
*   **Error Handling:**
    *   `Result<T, E>`: The preferred method for recoverable errors, handled through pattern matching (e.g., `match` expressions). This is a compile-time construct with runtime representation similar to an enum.
    *   `throw` statements: For unrecoverable errors or exceptional situations. This will likely involve stack unwinding.
        *   LLVM provides mechanisms for implementing exceptions (e.g., Itanium ABI exceptions).
        *   An exception object carrying information about the error will be propagated up the stack.
        *   `try-catch` blocks (if implemented) would catch these.

## 6. Standard Library (Prelim)

A minimal standard library will be needed, providing core functionalities. This will be implemented in Vyn itself where possible, or via C FFI for OS interactions.

*   **Core Types:** `Option<T>`, `Result<T, E>`, `Box<T>`, `Shared<T>` (if ARC).
*   **Collections:** `Vec<T>` (dynamic array), `HashMap<K, V>`, `String`.
*   **I/O:** Basic console I/O (`print`, `println`), file operations.
*   **Concurrency Primitives:** (Future) Channels, mutexes, atomic operations.
*   **Utilities:** Math functions, string manipulation.

## 7. LLVM Integration Aspects

*   **Module:** Each Vyn module (`.vyn` file) could correspond to an LLVM module.
*   **Types:** Map Vyn types to `llvm::Type`.
    *   `IntegerType`, `FloatType`, `DoubleType`, `PointerType`, `StructType`, `ArrayType`, `FunctionType`.
*   **Functions:** Vyn functions map to `llvm::Function`.
    *   Function bodies will be built using `llvm::IRBuilder`.
*   **Control Flow:**
    *   `if/else`: `CreateCondBr` instruction.
    *   `loop/while/for`: `CreateBr` and `CreateCondBr` for loop constructs.
    *   `match`: Lowered to a series of conditional branches or a `SwitchInst`.
*   **Variables:**
    *   Stack variables: `CreateAlloca`.
    *   Heap allocations (`Box<T>`): Calls to allocation functions (e.g., `malloc` or a custom Vyn allocator), followed by `CreateStore`.
*   **Intrinsics:** Certain operations (e.g., some arithmetic, memory operations) might map to LLVM intrinsics for better optimization.
*   **Debug Information:** Generate DWARF debug information via LLVM for source-level debugging.

## 8. Foreign Function Interface (FFI)

*   **Calling C from Vyn:**
    *   `extern "C" fn ...` declarations in Vyn will map to LLVM function declarations with C calling conventions.
    *   Type mapping between Vyn and C types will be crucial (e.g., Vyn `string` vs. C `char*`).
*   **Calling Vyn from C:**
    *   Vyn functions marked with `#[no_mangle]` and `extern "C"` can be exposed with a C-compatible ABI.

## 9. Runtime Initialization and Teardown

*   **Initialization:**
    *   Set up any global runtime state (e.g., for a custom allocator, or ARC system).
    *   Initialize the standard library.
    *   Call the `main` function.
*   **Teardown:**
    *   Run any registered exit handlers.
    *   Clean up global resources.

## 10. Future Considerations

*   **Garbage Collection:** While the primary approach is ownership/ARC, a tracing GC might be an option for specific use cases or to simplify certain types of programming, perhaps as an opt-in feature.
*   **JIT Compilation:** For scripting or dynamic code loading.
*   **WebAssembly Target:** Compiling Vyn to WASM.
*   **Coroutine/Async Support:** Requires careful design of task scheduling and state machine transformation at the LLVM IR level.

## 11. REPL (Read-Eval-Print Loop) Integration

A REPL for Vyn would allow for interactive programming and experimentation. Its integration with the runtime would likely involve the following:

*   **Interactive Input Loop:** The REPL would read Vyn code snippets (single expressions, statements, or small blocks) from the user.
*   **Incremental Compilation and Execution:**
    *   Each snippet would be parsed into an AST fragment.
    *   This AST fragment would then be compiled into LLVM IR.
    *   **JIT (Just-In-Time) Compilation:** Instead of AOT (Ahead-Of-Time) compilation to an object file, the LLVM IR would be JIT-compiled into executable machine code in memory using an LLVM `ExecutionEngine` or a similar JIT facility.
    *   The JIT-compiled code for the snippet would be executed immediately.
*   **Persistent State:**
    *   The REPL session must maintain state across inputs. This includes declared variables, functions, types, and imported modules.
    *   The LLVM `Module` would be continuously updated with new definitions. The JIT engine would need to manage these additions and make them available for subsequent snippets.
    *   A persistent symbol table and type context, managed by the compiler frontend, would be crucial.
*   **Expression Evaluation and Output:**
    *   If the user inputs an expression, the REPL would compile and run it, then print its resulting value. This requires the runtime to be able to serialize or format values for display.
    *   For statements (like variable declarations or function definitions), the REPL might just acknowledge the definition or report errors.
*   **Error Handling:** Compile-time and runtime errors from snippets need to be caught and reported gracefully to the user without terminating the REPL session.
*   **Standard Library Access:** The REPL environment should have access to the Vyn standard library.
*   **Potential Challenges:**
    *   Managing the lifetime of JIT-compiled code and associated data.
    *   Redefinitions and shadowing of variables/functions in an interactive session.
    *   Ensuring that the JIT compilation process is fast enough for an interactive experience.

This document provides a starting point. Many details will be fleshed out during the implementation of the compiler's LLVM backend and the initial runtime components.
