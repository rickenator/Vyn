# Proposal: Introduce `their` Keyword for Borrows

**Date:** May 13, 2025
**Status:** Proposed

## 1. Abstract

This proposal suggests replacing the current `&` sigil used for creating and typing non-owning references (borrows) in Vyb with the keyword `their`. The `const` keyword would continue to be used in conjunction with `their` to denote immutable borrows. This change aims to enhance syntactic clarity, reduce potential confusion with C-style address operators, and align the language more directly with the Vyb Runtime Environment (VRE) conceptual type `their<T>` for non-owning pointers.

## 2. Motivation

The primary motivations for this change are:

*   **Enhanced Clarity and Explicitness**: Using a keyword like `their` makes the act of borrowing more explicit in the syntax (e.g., `their x` instead of `&x`). This can improve code readability, especially for developers new to Vyb or those less familiar with sigil-based borrow systems.
*   **Differentiation from C-style Pointers**: The `&` symbol is strongly associated with taking an address in C and C++. While Vyb's references serve a similar purpose, they come with different semantics (e.g., safety guarantees, no pointer arithmetic). Using a distinct keyword helps to mentally separate Vyb's references from raw pointers.
*   **Alignment with VRE Terminology**: The Vyb Runtime Environment (VRE) uses the conceptual type `their<T>` (a non-owning raw pointer `T*` in its C++ implementation) for non-owning borrows. Adopting `their` in the Vyb language syntax creates a more direct and intuitive mapping between the language and its underlying runtime concepts.
*   **User Preference**: Feedback suggests a preference for a more descriptive keyword over a sigil for this core concept.

## 3. Proposed Change

We propose the following modifications to the Vyb language syntax and EBNF:

### 3.1. EBNF Modifications

1.  **`reference_type`**:
    *   **Current:** `reference_type ::= '&' [ lifetime ] [ 'const' ] type`
    *   **Proposed:** `reference_type ::= 'their' [ lifetime ] [ 'const' ] type`

2.  **`unary_expression` (for borrow operation)**:
    *   **Current:** `unary_expression ::= ... | ( '&' [ 'const' ] ) unary_expression | ...`
    *   **Proposed:** `unary_expression ::= ... | ( 'their' [ 'const' ] ) unary_expression | ...`

3.  **`pattern` (for reference patterns in `match`, `let`, etc.)**:
    *   **Current:** `pattern ::= ... | '&' [ 'const' ] pattern`
    *   **Proposed:** `pattern ::= ... | 'their' [ 'const' ] pattern`

### 3.2. Syntactic Examples

| Feature             | Current Syntax                       | Proposed Syntax                             |
| :------------------ | :----------------------------------- | :------------------------------------------ |
| Mutable Borrow Type | `&i32`                               | `their i32`                                 |
| Immutable Borrow Type| `&const string`                      | `their const string`                        |
| Create Mutable Borrow| `let r = &x;`                        | `let r = their x;`                          |
| Create Immutable Borrow| `let cr = &const y;`                 | `let cr = their const y;`                   |
| Function Parameter (mut borrow) | `fn foo(p: &MyType)`               | `fn foo(p: their MyType)`                   |
| Function Parameter (immut borrow)| `fn bar(p: &const MyType)`         | `fn bar(p: their const MyType)`             |
| Match Pattern (mut borrow) | `match v { &mv => ... }`           | `match v { their mv => ... }`               |
| Match Pattern (immut borrow)| `match v { &const cv => ... }`     | `match v { their const cv => ... }`         |

## 4. Detailed Examples

### Before (Current Syntax with `&`)

```vyb
struct Data { val: i32 }

fn process_data_mut(d_ref: &Data) {
    d_ref.val = d_ref.val * 2;
}

fn process_data_const(d_ref: &const Data) {
    // print(d_ref.val);
}

let mut_data = Data { val: 10 };

let ref_mut: &Data = &mut_data;
process_data_mut(ref_mut);

let ref_const: &const Data = &const mut_data;
process_data_const(ref_const);

const C_DATA = Data { val: 100 };
let ref_c_const: &const Data = &const C_DATA;
process_data_const(ref_c_const);
```

### After (Proposed Syntax with `their`)

```vyb
struct Data { val: i32 }

fn process_data_mut(d_ref: their Data) {
    d_ref.val = d_ref.val * 2;
}

fn process_data_const(d_ref: their const Data) {
    // print(d_ref.val);
}

let mut_data = Data { val: 10 };

let ref_mut: their Data = their mut_data;
process_data_mut(ref_mut);

let ref_const: their const Data = their const mut_data;
process_data_const(ref_const);

const C_DATA = Data { val: 100 };
let ref_c_const: their const Data = their const C_DATA;
process_data_const(ref_c_const);
```

## 5. Impact

*   **Language Semantics**: The underlying semantics of borrowing (rules around lifetimes, aliasing, mutability) remain unchanged. This is purely a syntactic alteration.
*   **Parser**: The parser will need to be updated to recognize `their` as a keyword and handle it in type definitions, expressions, and patterns.
*   **Documentation**: All language documentation, tutorials, and examples (including `RUNTIME.md` and `memory_semantics.vyb`) will need to be updated.
*   **Existing Code**: This is a breaking change. Any existing Vyb code would need to be updated to the new syntax. Tooling (e.g., a `vyb fmt` subcommand or a dedicated migration script) could assist with this.
*   **Keyword Reservation**: `their` would become a reserved keyword.

## 6. Benefits

*   **Improved Readability**: Makes borrow operations more self-documenting.
*   **Reduced Ambiguity**: Lessens potential confusion with C/C++ address-of operator.
*   **Conceptual Consistency**: Directly reflects VRE's `their<T>` non-owning pointer concept.
*   **Addresses User Feedback**: Responds to a desire for more descriptive syntax.

## 7. Potential Drawbacks / Considerations

*   **Verbosity**: Keywords are inherently more verbose than sigils. However, for a core concept like borrowing, the explicitness might be a worthwhile trade-off.
*   **Keyword Collision**: `their` is a common English word; ensuring it doesn't feel awkward or lead to parsing ambiguities in other contexts (though unlikely in its proposed usage) is important.
*   **Deviation from "Common Practice"**: Many modern languages with borrow systems (e.g., Rust, Swift with `&`) use sigils. Adopting a keyword deviates from this trend, which might be a minor learning curve adjustment for developers coming from those languages. However, Vyb is not bound to follow these conventions if a keyword offers better clarity for its specific goals.
*   **Tooling Effort**: Updating the parser, documentation, and potentially creating migration tools requires effort.

## 8. Alternatives Considered

*   **Status Quo**: Keep the `&` syntax. This is familiar to users of languages like Rust but doesn't address the motivations listed above.
*   **Other Keywords**: Other keywords could be considered (e.g., `borrow`, `ref`), but `their` has the advantage of aligning with the established VRE terminology.

## 9. Open Questions

*   Are there any unforeseen parsing ambiguities with introducing `their` as a keyword in the proposed contexts?
*   What is the community sentiment regarding this change, particularly concerning the verbosity trade-off?

## 10. Conclusion

The proposal to replace `&` with `their` for borrow syntax offers a path to a more explicit, potentially clearer, and VRE-aligned Vyb language. While it introduces a breaking change and adds a new keyword, the benefits in terms of readability and conceptual mapping are believed to be significant. Feedback on this proposal is encouraged to determine the best path forward.
