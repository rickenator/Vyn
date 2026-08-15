# Proposal: Comprehensive Ownership and Borrowing Keywords (`my`, `our`, `their`)

**Date:** May 13, 2025
**Status:** Proposed

## 1. Abstract

This proposal outlines a comprehensive set of keywords—`my`, `our`, and `their`—to explicitly manage memory ownership and borrowing in the Vyb language.
- `my Type` will denote explicit unique ownership.
- `our Type` will denote shared, reference-counted ownership.
- `their Type` (and `their const Type`) will replace the `&` sigil for non-owning mutable (and immutable) borrows.
These changes aim to enhance syntactic clarity, align the language more directly with Vyb Runtime Environment (VRE) concepts (`my<T>`, `our<T>`, `their<T>`), and provide a more descriptive and intuitive memory management model.

## 2. Motivation

The Vyb language aims for a balance of performance, safety, and developer ergonomics. Explicitly representing ownership and borrowing semantics in the type system and syntax is crucial for achieving these goals. The motivations for introducing `my`, `our`, and `their` are:

*   **Enhanced Clarity and Explicitness**: Keywords make ownership and borrowing semantics more explicit than sigils or implicit conventions.
    *   `my Type` explicitly states unique ownership.
    *   `our Type` explicitly states shared ownership.
    *   `their x` (for creating a borrow) is more descriptive than `&x`.
*   **Alignment with VRE Terminology**: The VRE uses `my<T>`, `our<T>`, and `their<T>` as conceptual (and C++ implementation) types. Adopting these keywords in Vyb creates a direct and intuitive mapping.
*   **Differentiation from C-style Pointers**: Replacing `&` with `their` helps differentiate Vyb's safer, managed references from raw C/C++ pointers and their associated risks.
*   **Comprehensive Memory Model**: Providing keywords for all three core VRE ownership concepts (`my`, `our`, `their`) directly in the language offers a complete and understandable memory management story to the developer.
*   **User Preference**: Feedback indicates a desire for more descriptive keywords for these fundamental concepts.

## 3. Proposed Changes

We propose the following modifications to the Vyb language syntax and EBNF:

### 3.1. Keyword Introduction

*   `my`: Becomes a keyword, used as a type qualifier for unique ownership.
*   `our`: Becomes a keyword, used as a type qualifier for shared ownership.
*   `their`: Becomes a keyword, used as a type qualifier for non-owning borrows and as an operator to create borrows.

### 3.2. EBNF Modifications

1.  **`core_type`** (replacing and expanding previous `reference_type`):
    *   **Current (relevant parts):**
        ```ebnf
        core_type ::= named_type | ... | reference_type ...
        reference_type ::= '&' [ lifetime ] [ 'const' ] type
        ```
    *   **Proposed:**
        ```ebnf
        core_type ::= named_type
                    | ...
                    | 'my' type                 // Explicit unique ownership type
                    | 'our' type                // Shared ownership type
                    | 'their' [ lifetime ] [ 'const' ] type // Non-owning borrow types
                    | ...
        ```
    *(Note: `reference_type` production rule would be removed or merged into `core_type`)*

2.  **`unary_expression` (for borrow operation)**:
    *   **Current:** `unary_expression ::= ... | ( '&' [ 'const' ] ) unary_expression | ...`
    *   **Proposed:** `unary_expression ::= ... | ( 'their' [ 'const' ] ) unary_expression | ...`
    *(The `my` and `our` types do not introduce new unary operators for creation in this proposal; `my` is typically by-value construction, and `our` instances are proposed to be created via a standard library function like `make_our()` for now.)*

3.  **`pattern` (for reference patterns in `match`, `let`, etc.)**:
    *   **Current:** `pattern ::= ... | '&' [ 'const' ] pattern`
    *   **Proposed:** `pattern ::= ... | 'their' [ 'const' ] pattern`
    *(Patterns for `my` and `our` types would typically be direct bindings or deconstructions of the underlying type, not special patterns for the ownership wrapper itself, unless future enhancements add such capabilities.)*

### 3.3. Syntactic Examples

| Feature                     | Current/Conceptual Syntax            | Proposed Syntax                               | VRE Concept |
| :-------------------------- | :----------------------------------- | :-------------------------------------------- | :---------- |
| Unique Ownership Type       | `Data` (implicitly `my`)             | `my Data` (explicitly `my`)                   | `my<T>`     |
| Shared Ownership Type       | (Library `Shared<Data>`)             | `our Data`                                    | `our<T>`    |
| Mutable Borrow Type         | `&Data`                              | `their Data`                                  | `their<T>`  |
| Immutable Borrow Type       | `&const Data`                        | `their const Data`                            | `their<const T>`|
| Create Unique (default)     | `let d = Data{};`                    | `let d = Data{};` (still default)             |             |
| Create Unique (explicit type)| `let d: Data = Data{};`              | `let d: my Data = Data{};`                    |             |
| Create Shared               | `let s = Shared::new(Data{});`       | `let s: our Data = our(Data{});` (conceptual fn) |             |
| Create Mutable Borrow       | `let r = &d;`                        | `let r = their d;`                            |             |
| Create Immutable Borrow     | `let cr = &const d;`                 | `let cr = their const d;`                     |             |

### 3.4. Creation of `our` Instances

This proposal focuses on `our Type` as a language-recognized type. The actual creation of shared instances (which involves heap allocation and setting up reference counting) is recommended to be handled by a standard library function or a dedicated `new our ...` expression in a future proposal. For this document, we'll assume a conceptual `make_our<T>(value: T) -> our T` function.

## 4. Detailed Examples

### Example Structure
```vyb
struct Item { data: i32 }

// Conceptual standard library function for shared ownership
fn make_our<T>(value: T) -> our T { /* ... VRE intrinsic ... */ }
```

### Unique Ownership (`my`)
```vyb
// `let` implies unique ownership by default for value types.
let item1 = Item { data: 10 }; // item1 is uniquely owned (conceptually `my Item`)

// Explicitly typing with `my` for clarity:
let item2: my Item = Item { data: 20 };
// item2.data = 22; // Allowed as item2 is mutable (`let`)

fn process_uniquely(val: my Item) { // Takes ownership
    // print("Processed uniquely:", val.data);
}
// process_uniquely(item2); // item2 is moved
// print(item2.data); // Error: item2 moved
```

### Shared Ownership (`our`)
```vyb
let shared_item1: our Item = our(Item { data: 30 });
let shared_item2: our Item = shared_item1; // Reference count increases

fn use_shared_item(s_item: our Item) {
    // print("Using shared item:", s_item.data);
    // s_item.data = 33; // Mutability depends on `our const Item` vs `our Item` and if `Item` fields are const
                       // For now, assume `our Item` allows mutation if underlying data is mutable.
                       // `our const Item` would prevent mutation through that reference.
}
use_shared_item(shared_item1);
// print(shared_item1.data); // Still accessible, shared_item2 also holds a reference
```

### Borrowing (`their`)
```vyb
let mut_item = Item { data: 40 }; // Uniquely owned by mut_item

// Mutable borrow
fn modify_item(item_ref: their Item) {
    item_ref.data = 45;
}
let borrowed_mut: their Item = their mut_item;
modify_item(borrowed_mut); // mut_item.data is now 45

// Immutable borrow
fn read_item(item_ref: their const Item) {
    // print("Read item:", item_ref.data);
    // item_ref.data = 50; // Error: cannot modify through `their const`
}
let borrowed_const: their const Item = their const mut_item;
read_item(borrowed_const);

const CONST_ITEM = Item { data: 60 };
let borrowed_c_const: their const Item = their const CONST_ITEM;
read_item(borrowed_c_const);
// let borrowed_c_mut: their Item = their CONST_ITEM; // Error: cannot mutably borrow const
```

## 5. Impact

*   **Language Semantics**:
    *   `their`: Purely syntactic for borrows; underlying borrow checking rules remain.
    *   `my`: Formalizes explicit unique ownership in the type system. Default `let` bindings of value types already imply this.
    *   `our`: Introduces shared (reference-counted) ownership as a language type. This has significant semantic implications (heap allocation, reference counting, potential cycle issues if not handled with mild references – which are beyond this initial proposal).
*   **Parser**: Needs updates for new keywords (`my`, `our`, `their`) in type expressions, `their` in unary expressions and patterns.
*   **Type System**: Must recognize `my Type`, `our Type`, `their Type`, `their const Type`.
*   **VRE Integration**: `my`, `our`, `their` types in Vyb will map directly to VRE's `my<T>`, `our<T>`, `their<T>` mechanisms.
*   **Documentation**: Significant updates required for all language materials.
*   **Existing Code**: This is a breaking change. Tooling for migration would be highly beneficial.
*   **Keyword Reservation**: `my`, `our`, `their` become reserved keywords.

## 6. Benefits

*   **Unified Conceptual Model**: Language syntax directly reflects VRE ownership primitives.
*   **Enhanced Readability & Explicitness**: Clearer intent regarding memory management.
*   **Reduced Ambiguity**: `their` avoids confusion with C-style `&`.
*   **Foundation for Advanced Features**: `our Type` paves the way for safe concurrency patterns.
*   **Addresses User Feedback**: Responds to desire for more descriptive syntax.

## 7. Potential Drawbacks / Considerations

*   **Verbosity**: Keywords are more verbose than sigils (for `their` vs `&`).
*   **Keyword Collision**: `my`, `our`, `their` are common words.
*   **Complexity of `our`**: Shared ownership introduces complexities (reference counting overhead, cycles). The initial proposal is for the *type*; robust library support and potentially mild references would be needed.
*   **Tooling Effort**: Significant effort for parser, type checker, documentation, and migration tools.
*   **Learning Curve**: While aiming for clarity, three distinct ownership keywords might initially seem like more to learn than a sigil-based system combined with library types.

## 8. Alternatives Considered

*   **Status Quo for Borrows (`&`)**: Retain `&`, potentially introducing `my` and `our` as types or library features only. Less disruptive but doesn't address `&` concerns.
*   **Library-Only `my` and `our`**: Don't make `my` and `our` keywords; rely on convention for `let` (unique) and library types for shared (e.g., `Rc<T>`, `Arc<T>`). Simpler for the core language but less integrated.
*   **Different Keywords**: Other words could be used, but `my`, `our`, `their` have strong conceptual backing from the VRE.

## 9. Open Questions

*   What is the community consensus on the verbosity vs. explicitness trade-off for these keywords?
*   Are there unforeseen parsing ambiguities?
*   What should be the precise mechanism and syntax for creating `our Type` instances (e.g., `our(value)`, `new our Type`, `our { value }`)? This proposal leans towards a library function for now.
*   How should `our const T` vs `our T` interact with mutability of the underlying data? (e.g. an `our Item` where `Item` has `let` fields).
*   Should `my` be *required* for unique ownership, or is `let x = Value{}` sufficient, with `my Type` being an optional explicit annotation? (This proposal suggests the latter).

## 10. Conclusion

Introducing `my`, `our`, and `their` as keywords provides a powerful and explicit way to manage memory in Vyb, aligning the language closely with its runtime environment's philosophy. While `their` is a direct replacement for `&` to improve clarity for borrows, `my` formalizes unique ownership typing, and `our` introduces shared ownership as a first-class type concept. This comprehensive approach aims to make Vyb's memory management robust, understandable, and ergonomic. Feedback is crucial to refine these ideas.
