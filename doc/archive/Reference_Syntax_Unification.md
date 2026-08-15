# Reference Syntax Unification for Vyb v0.4.0

## Problem Statement

Currently, Vyb has **three different syntaxes** for ownership and borrowing operations, causing confusion and inconsistency:

### Current Inconsistent Syntaxes:

1. **Function-style constructors** (examples/main.vyb, examples/binary_tree.vyb):
   ```vyb
   my(Expression)     // Used in examples
   our(Expression)
   their(Expression)
   ```

2. **make_* functions** (README.md, doc/RUNTIME.md):
   ```vyb
   my("data")    // Documented in README/docs
   our("data")
   view "data"
   ```

3. **Legacy prefix keyword-style** (deprecated; migrate to call syntax):
   ```vyb
   view(expr)         // Creates their<T const>
   borrow(expr)       // Creates their<T>
   ```

## Unified Canonical Syntax

This document establishes the **single, canonical syntax** for all ownership and borrowing operations in Vyb v0.4.0:

### ✅ **Canonical Ownership Creation Syntax**

```vyb
// OWNERSHIP TYPES - These are type annotations
name<my<Type>>      = ...    // Unique ownership type
name<our<Type>>     = ...    // Shared ownership type
name<their<Type>>   = ...    // Borrowed reference type

// OWNERSHIP CREATION - These are value constructors
my(expression)              // Create unique ownership
our(expression)             // Create shared ownership (reference counted)

// BORROWING OPERATIONS - Canonical function-call syntax
borrow(expression)          // Create mutable borrow -> their<T>
view(expression)            // Create immutable borrow -> their<T const>
```

> **Syntax Rationale**: The unified function-call syntax `my(expr)`, `our(expr)`, `view(expr)`, and `borrow(expr)` provides syntactic consistency across all ownership operations. This design makes the language more regular and predictable, simplifying both parser implementation and developer understanding. The function-call style clearly indicates these are fundamental language constructs while maintaining a clean, readable syntax.

### ✅ **Complete Example with Canonical Syntax**

```vyb
// Type declarations use ownership types
process_data(input<my<String>>)<their<String const>> -> {
    // Create owned values with constructors
    owned_copy<my<String>> = my(input.clone());
    shared_ref<our<String>> = our(input.clone());

    // Create borrowed references with operators
    immutable_view<their<String const>> = view(owned_copy);
    mutable_borrow<their<String>> = borrow(owned_copy);

    return view(owned_copy);  // Return immutable view
}

main()<Void> -> {
    data<my<String>> = my("Hello, World!");
    result<their<String const>> = process_data(data);
    println(view(result));
}
```

## Implementation Status

### ✅ **Already Implemented:**
- `my<T>`, `our<T>`, `their<T>` ownership types in type system
- `borrow(expr)` and `view(expr)` borrow expressions in parser
- `my()`, `our()` function-style constructors in examples

### ❌ **Legacy Syntax to Remove:**
- `make_my()`, `make_our()`, `make_their()` functions
- Inconsistent documentation examples

## Migration Guide

### **For Code:**
```vyb
// OLD (inconsistent):
data<my<String>> = my("text");      // ❌ Remove
shared<our<String>> = our("text");  // ❌ Remove

// NEW (canonical):
data<my<String>> = my("text");           // ✅ Use this
shared<our<String>> = our("text");       // ✅ Use this
```

### **For Documentation:**
All documentation must use the canonical syntax consistently.

## Parsing Rules

The parser recognizes these constructs with the following precedence:

1. **Type Context**: `my<T>`, `our<T>`, `their<T>` → Ownership type wrappers
2. **Expression Context**:
   - `my(expr)` → Ownership constructor (function call)
   - `our(expr)` → Ownership constructor (function call)
   - `borrow(expr)` → Borrowing function call
   - `view(expr)` → Borrowing function call

## Benefits of Unification

1. **Consistency**: Single syntax reduces cognitive load
2. **Clarity**: Clear distinction between types (`my<T>`) and values (`my(expr)`)
3. **Intuitive**: Mirrors the VRE implementation concepts directly
4. **Concise**: Shorter than `make_*` functions
5. **Composable**: Works naturally with complex expressions

## Action Items

1. **✅ Parser Implementation**: Already supports canonical syntax
2. **🔧 Documentation Update**: Update all docs to use canonical syntax
3. **🔧 Example Migration**: Update examples to use canonical syntax
4. **🔧 Remove Legacy**: Remove `make_*` references from documentation
5. **🔧 Test Updates**: Ensure all tests use canonical syntax

---

**This document serves as the authoritative reference for all ownership and borrowing syntax in Vyb v0.4.0.**