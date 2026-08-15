<!-- filepath: /home/rick/Projects/Vyb/doc/AST_Roadmap.md -->
# Vyb AST: Roadmap and Planned Features

This document outlines planned features, future extensions, and areas for improvement for the Vyb Abstract Syntax Tree (AST). It incorporates items previously marked as "planned" in the original `AST.md` and addresses review suggestions related to future development (like Suggestion 10).

## 1. Planned AST Node Types

This section consolidates nodes that were previously mentioned as "planned" or are logical extensions based on language feature goals. For each, consideration will be given to their structure, visitor integration, and impact on parsing and semantic analysis.

### 1.1. Pattern Matching Nodes

*   **Status:** Conceptualized, details in `AST_Patterns.md`.
*   **Description:** A suite of nodes to represent various patterns used in `match` expressions, `let` bindings, and function parameters.
*   **Key Nodes (see `AST_Patterns.md` for details):**
    *   `PatternNode` (base)
    *   `IdentifierPattern`
    *   `LiteralPattern`
    *   `TuplePattern`
    *   `StructPattern`
    *   `EnumVariantPattern`
    *   `WildcardPattern`
    *   `RangePattern`
    *   `OrPattern`
*   **Action:** Implement these nodes in `ast.hpp`/`ast.cpp`, update `NodeType`, and integrate with the `Visitor` pattern.

### 1.2. Asynchronous Programming Nodes

*   **Status:** Planned.
*   **Description:** Nodes to support `async` functions and `await` expressions.
*   **Key Nodes:**
    *   `AsyncFunctionDeclaration` (or a flag on `FunctionDeclaration`): To mark a function as asynchronous.
    *   `AwaitExpression`: Represents an `await` operation on a future or promise.
        *   `PExpression operand`: The expression being awaited.
*   **Action:** Define these nodes, integrate with `Visitor`, and update parser to handle `async`/`await` keywords.

### 1.3. Module System Nodes

*   **Status:** Partially implemented through `ImportDeclaration`, but needs expansion.
*   **Description:** Nodes to fully support Vyb's module system, including module declarations and more complex import/export capabilities.
*   **Key Nodes:**
    *   `ModuleDeclaration`: (Potentially at the root of an AST file or as a special node) Declares the current file as a module with a specific path.
    *   `ExportDeclaration`: Wraps a declaration (function, struct, enum, const) to mark it as exported. Could also be a flag on existing declaration nodes.
    *   Enhanced `ImportDeclaration`: Support for selective imports (`import my_module::{foo, bar};`), aliasing (`import my_module::foo as FooBar;`).
*   **Action:** Refine module-related AST nodes and parsing logic.

### 1.4. Error Handling Nodes

*   **Status:** `fail`/`trap` is the Vyb error system. `TryStatement` and `ThrowStatement`
    are **not** part of Vyb — they are vestigial C++/Java vocabulary and have been removed
    from this roadmap.
*   **Description:** Vyb uses `fail`/`trap` for error propagation. These provide a
    zero-cost success path with typed errors. There is no `throw`, no `try`, no `catch`,
    and no `finally` in Vyb. AST nodes for Vyb error handling:
*   **Key Nodes:**
    *   `FailExpression`: Represents a `fail<ErrorType>(value)` expression — typed error
        propagation.
    *   `TrapStatement`: Represents a `trap` handler — catches a `fail`-propagated error
        from a called function with `?` or explicit trap syntax.
    *   `ErrorNode` (or `ErrorExpression`, `ErrorStatement`): Represents constructs that
        failed to parse correctly but where recovery was possible. Used by the parser to
        record errors without aborting the whole parse.
*   **Action:** Implement `FailExpression` and `TrapStatement` nodes. `ErrorNode` for
    parser recovery. Do not add any try/catch/throw/finally nodes — they are not Vyb.

### 1.5. Metaprogramming / Macros

*   **Status:** Future consideration.
*   **Description:** If Vyb incorporates procedural or declarative macros, the AST will need to represent macro definitions and invocations.
*   **Key Nodes:**
    *   `MacroDefinition`
    *   `MacroInvocationExpression`
    *   `MacroInvocationStatement`
*   **Action:** Design these if/when macros become a feature. This is a significant undertaking.

### 1.6. Advanced Type System Nodes

*   **Status:** `TypeNode` is somewhat generic. `AST_Types.md` and `AST_Design_Considerations.md` discuss improvements. The C++ implementation in `vyb/parser/ast.hpp` includes `BasicTypeNode`, `PointerTypeNode`, `SizedArrayTypeNode`, `UnsizedArrayTypeNode`, `TupleTypeNode`, `FunctionTypeNode`, and `GenericTypeNode`. This section outlines further specializations or distinct types.
*   **Description:** More specialized `TypeNode` subclasses for complex types not yet covered or needing distinct representation.
*   **Key Nodes (potential subclasses of `TypeNode` or distinct type nodes):**
    *   `SliceTypeNode`: Represents a slice type (e.g., `&[T]`), potentially distinct from `UnsizedArrayTypeNode` if semantics differ (e.g., fat pointer vs. simple pointer to unsized data).
        *   `TypeNodePtr elementType`
    *   `SelfTypeNode`: Represents the `Self` keyword used as a type, typically within aspect or bind blocks, referring to the binding type.
*   **Action:** Refine `TypeNode` hierarchy as discussed in `AST_Types.md` and `AST_Design_Considerations.md`, and implement these additional specialized type nodes as language features require.

### 1.7. Conditional Expressions (Ternary Operations)

*   **Status:** Future consideration.
*   **Description:** Nodes to support C-style ternary conditional expressions.
*   **Key Nodes:**
    *   `TernaryExpression`: Represents an expression like `condition ? then_expr : else_expr`.
        *   `PExpression condition`: The condition to evaluate.
        *   `PExpression thenExpression`: The expression to evaluate if the condition is true.
        *   `PExpression elseExpression`: The expression to evaluate if the condition is false.
*   **Action:** Define the `TernaryExpression` node, integrate it with the `Visitor` pattern, and update the parser to handle the syntax.

### 1.8. Pattern-Based Assignment Statements

*   **Status:** Future consideration.
*   **Description:** Statements that allow assigning to a pattern, enabling destructuring assignment outside of initial declarations (e.g., `(a, b) = some_function_returning_tuple();`).
*   **Key Nodes:**
    *   `PatternAssignmentStatement`: Represents an assignment where the left-hand side is a pattern.
        *   `PPattern pattern`: The pattern to assign to (referencing nodes from `AST_Patterns.md`).
        *   `PExpression value`: The expression whose value is being assigned and destructured.
*   **Action:** Define the `PatternAssignmentStatement` node, integrate with the `Visitor`, and update the parser. This will likely leverage existing pattern AST nodes.

### 1.9. Aspect System Nodes

*   **Status:** Partially implemented (`AspectDeclaration`, `BindDeclaration`).
*   **Description:** Nodes to support aspect definitions, method signatures within aspects,
    and aspect bounds for generics. These are fundamental for Vyb's polymorphism. Vyb uses
    `aspect`/`bind` — not `trait`/`impl`. `BindDeclaration` for binding aspects to types
    is already part of the AST.
*   **Key Nodes:**
    *   `AspectDeclaration`: Defines a new aspect.
        *   `IdentifierPtr name`
        *   `std::vector<GenericParameterPtr> genericParameters`
        *   `std::vector<MethodSignaturePtr> methodSignatures`
    *   `MethodSignature`: Declares a method signature within an aspect.
        *   `IdentifierPtr name`
        *   `std::vector<FunctionParameterPtr> parameters`
        *   `TypeNodePtr returnType`
        *   `std::vector<GenericParameterPtr> genericParameters`
        *   *(Consider flags: `isConst`, `isAsync`, `isStatic` etc.)*
    *   `AspectBoundNode`: Specifies that a generic type parameter must bind one or more
        aspects (used in generic parameter definitions). Syntax: `<T<Comparable>>`.
        *   `TypeNodePtr constrainedType` // e.g., a `GenericTypeNode` representing `T`
        *   `std::vector<TypeNodePtr> bounds` // aspect names like `Comparable`, `Display`
*   **Action:** Design and implement these nodes in `ast.hpp`/`ast.cpp`. Update `NodeType`,
    integrate with the `Visitor` pattern, and develop parsing rules for aspect definitions
    and bind blocks. Use `<T<Aspect>>` syntax only — not `<T: Aspect>`.

### 1.10. Low-Level Pointer Intrinsics and Operations

*   **Status:** Planned. Implement them in `src/vre/intrinsics.cpp`.
*   **Description:** Provides low-level control over memory and pointers, essential for systems programming and optimization.
*   **Key Intrinsics:**
    *   `offset<T>(ptr: *T, count: isize) -> *T`: Computes a new pointer by offsetting `ptr` by `count` elements of type `T`. Handles pointer arithmetic correctly.
    *   `is_nil<T>(ptr: *T) -> bool`: Checks if a pointer is null or nil.
    *   `mem_copy<T>(dest: *mut T, src: *const T, count: usize)`: Copies `count` elements of type `T` from `src` to `dest`. Similar to `memcpy`.
        *   Consideration: Overlap semantics (like `memmove` vs `memcpy`). Default to non-overlapping, provide `mem_move` if needed.
    *   `mem_set<T>(dest: *mut T, value_byte: u8, count: usize)`: Sets `count` bytes starting at `dest` to `value_byte`. Similar to `memset`.
        *   Note: `count` is in bytes, not elements of `T`.
    *   `atomic_load<T>(ptr: *const T, ordering: AtomicOrdering) -> T`: Atomically loads a value of type `T` from `ptr`.
    *   `atomic_store<T>(ptr: *mut T, value: T, ordering: AtomicOrdering)`: Atomically stores `value` of type `T` to `ptr`.
    *   `atomic_exchange<T>(ptr: *mut T, value: T, ordering: AtomicOrdering) -> T`: Atomically exchanges the value at `ptr` with `value`.
    *   `atomic_compare_exchange<T>(ptr: *mut T, expected: T, desired: T, success_ordering: AtomicOrdering, failure_ordering: AtomicOrdering) -> (T, bool)`: Atomically compares the value at `ptr` with `expected`, and if equal, replaces it with `desired`. Returns the original value and a boolean indicating success.
    *   `fence(ordering: AtomicOrdering)`: Establishes memory ordering constraints between operations before and after the fence.
    *   `volatile_load<T>(ptr: *const T) -> T`: Performs a volatile load from `ptr`.
    *   `volatile_store<T>(ptr: *mut T, value: T)`: Performs a volatile store to `ptr`.
    *   `asm(...)`: Allows embedding platform-specific assembly code. Syntax and capabilities TBD.
*   **Action:** Define these intrinsic functions, their precise signatures, and semantics. Integrate them into the VRE and expose them to the language.

### 1.11. General-Purpose Language Intrinsics

*   **Status:** Planned. Implement them in `src/vre/intrinsics.cpp`.
*   **Description:** Core functions often provided by languages as built-ins or standard library essentials.
*   **Key Intrinsics:**
    *   `println(...)`: Prints a formatted string or values to the standard output, followed by a newline. (Variadic arguments or type-driven formatting TBD).
    *   `print(...)`: Similar to `println` but without the trailing newline.
    *   `assert(condition: bool, message: ?string)`: Asserts that a condition is true. If false, terminates the program, potentially printing an optional message.
    *   `size_of<T>() -> usize` or `size_of(value: T) -> usize`: Returns the size of a type `T` or a value in bytes.
    *   `align_of<T>() -> usize` or `align_of(value: T) -> usize`: Returns the alignment of a type `T` or a value in bytes.
    *   `type_of<T>() -> TypeInfo` or `type_of(value: T) -> TypeInfo`: Returns runtime type information for a type or value. (Details of `TypeInfo` TBD).
    *   `panic(message: string)`: Unconditionally terminates the program with a panic message.
*   **Action:** Define these intrinsic functions, their signatures, and behavior. Integrate with the VRE.

## 2. AST Infrastructure and Tooling Enhancements

### 2.1. Parent Pointers

*   **Status:** Discussed in `AST_Design_Considerations.md`.
*   **Description:** Consider adding non-owning parent pointers to `Node` to simplify upward tree traversal for some analyses.
*   **Action:** Evaluate trade-offs. If implemented, ensure no ownership cycles.

### 2.2. Enhanced Visitor Support

*   **Status:** Discussed in `AST_Design_Considerations.md`.
*   **Description:** Introduce a `BaseVisitor` with default implementations to reduce boilerplate for visitors that only care about a subset of node types.
*   **Action:** Implement `BaseVisitor` and update existing visitors if beneficial.

### 2.3. Source Location Tracking

*   **Status:** `SourceLocation` exists on `Node`.
*   **Description:** Ensure all nodes, including newly added ones, correctly and consistently store their source location (start and end). This is crucial for error reporting and tooling.
*   **Action:** Ongoing diligence during implementation of new nodes.

### 2.4. AST Serialization/Deserialization

*   **Status:** Not currently implemented.
*   **Description:** Ability to serialize the AST (e.g., to JSON, binary format) and deserialize it. Useful for debugging, caching, or inter-tool communication (e.g., language server).
*   **Action:** Plan and implement a serialization format if deemed necessary.

### 2.5. AST Pretty Printer

*   **Status:** `toString()` methods exist for debugging.
*   **Description:** A more robust AST pretty printer that can reconstruct Vyb-like source code from the AST. Useful for debugging and code generation/transformation tools.
*   **Action:** Enhance `toString()` or create a dedicated pretty-printing visitor.

## 3. Documentation and Consistency (Review Suggestion 10)

**Review Suggestion 10: Merge "planned" nodes into a roadmap section.**
> "The AST.md document lists several node types as "Planned" (e.g., `AsyncFunctionDeclaration`, `AwaitExpression`, `PatternNode` and its variants). It would be clearer to group these into a dedicated "Roadmap" or "Future Extensions" section, or even a separate document, rather than interspersing them with currently implemented nodes. This helps distinguish the current state from future aspirations."

*   **Status:** This document (`AST_Roadmap.md`) directly addresses this suggestion.
*   **Action:** Maintain this document as the central place for planned AST changes. Ensure that other AST documents (`AST_Core.md`, `AST_Expressions.md`, etc.) accurately reflect the *current* state of implemented AST nodes.

## 4. Long-Term Considerations

*   **AST Stability:** As the language evolves, aim for AST stability where possible, but be prepared to version or adapt the AST if significant language changes occur.
*   **Performance:** For large codebases, AST construction and traversal performance can be critical. Re-evaluate choices like `std::shared_ptr` vs `std::unique_ptr` and consider arena allocation (see `AST_Design_Considerations.md`).
*   **IDE Integration:** Design the AST to be amenable to consumption by language servers for features like code completion, hover information, and refactoring.

This roadmap provides a forward-looking view for the Vyb AST. Priorities and specific designs may evolve as the language implementation progresses.
