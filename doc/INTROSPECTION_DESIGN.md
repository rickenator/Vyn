# Introspection System Design - Phase 1: Basic Type Information

**Version**: 0.5.0
**Status**: 🏗️ In Progress
**Date**: October 2025

## Philosophy: "Know Thyself" - The Zen of Type Awareness

> *"Before code can reason about the world, it must first reason about itself."*

Introspection in Vyb is not merely a feature—it's a fundamental capability that allows code to understand its own nature. Like a zen monk examining their own consciousness, Vyb programs can examine their own type structure.

## The Nature of Types

In Vyb, every value has a **type identity**—an immutable essence that defines what it *is*. This identity exists at both compile-time and runtime:

- **Compile-time**: The parser and semantic analyzer know types
- **Runtime**: Values carry their type identity as a hash (8 bytes)

The introspection system bridges these realms, allowing runtime code to access compile-time knowledge.

## Core Design Principles

### 1. **Lightweight by Nature**
Type information should be as light as the concept itself:
- Type ID: 8 bytes (uint64_t hash of type name)
- Type name: Pointer to string literal (8 bytes)
- Total: 16 bytes for complete type information

### 2. **Hash-Based Identity**
Types are identified by `std::hash<std::string>{}(typeName)`:
```cpp
uint64_t typeHash = std::hash<std::string>{}("Int");        // 0x...
uint64_t errorHash = std::hash<std::string>{}("ParseError"); // 0x...
```

This system is **already implemented** for error types—we're extending it to all types.

### 3. **No Magic, Only Truth**
Introspection reveals what *is*, not what might be:
- `typeof(value)` returns the **actual runtime type**, not declared type
- Generic type parameters resolved to concrete types
- No reflection on uninstantiated generics

### 4. **Integration with Existing Systems**
The error handling system (Phase 6) already uses type IDs:
```cpp
struct VybError {
    const char* type_name;  // "ParseError"
    void* type_id;          // Hash as void*
    // ...
};
```

We extend this pattern to **all Vyb values**.

## Phase 1: The Three Operators

### Operator 1: `typeof(expr)` - The Question "What Is?"

**Syntax**: `typeof(expression)`
**Returns**: `Type` object (opaque 8-byte type ID)
**Purpose**: Get runtime type identity

**Examples**:
```vyb
x<Int> = 42
t<Type> = typeof(x)  # Type representing Int

# Type comparison
if (typeof(x) == typeof<Int>()) {
    println("It's an integer!")
}

# Wildcard trap handler
{
    operation()
} trap (e<?>) -> {
    if (typeof(e) == typeof<ParseError>()) {
        println("Parse error occurred")
    } else if (typeof(e) == typeof<IOError>()) {
        println("IO error occurred")
    }
}
```

**Implementation**:
- At compile-time, semantic analyzer resolves expression type
- At codegen, generate hash of the type name: `std::hash<std::string>{}(typeName)`
- Return as opaque 8-byte value (LLVM `i64`)
- Store in variable of type `Type`

**AST Node**:
```cpp
class TypeofExpression : public Expression {
    ExprPtr operand;  // Expression to get type of
    // At semantic phase, resolved_type is set
};
```

---

### Operator 2: `typename(expr)` - The Name of Being

**Syntax**: `typename(expression)`
**Returns**: `String` with type name
**Purpose**: Get human-readable type name

**Examples**:
```vyb
x<Int> = 42
name<String> = typename(x)  # "Int"

error<ParseError> = ParseError { line: 10, column: 5 }
println(typename(error))  # "ParseError"

# Debug printing
println("Value is of type: " + typename(value))
```

**Implementation**:
- At compile-time, resolve expression type name
- At codegen, generate pointer to string literal containing type name
- Wrap in Vyb String struct: `VybString { data: "Int", length: 3 }`
- Return as String value

**AST Node**:
```cpp
class TypenameExpression : public Expression {
    ExprPtr operand;
    // Semantic phase resolves to type name string
};
```

---

### Operator 3: Type Equality - The Test of Sameness

**Syntax**: `typeof(a) == typeof(b)` or `typeof(a) == typeof<T>()`
**Returns**: `Bool`
**Purpose**: Compare type identities

**Examples**:
```vyb
x<Int> = 42
y<String> = "hello"

if (typeof(x) == typeof(y)) {
    println("Same type")  # Won't print
}

# Compare against known type
if (typeof(x) == typeof<Int>()) {
    println("It's an Int!")  # Will print
}

# In wildcard handlers
{
    operation()
} trap (e<?>) -> {
    match (typeof(e)) {
        typeof<ParseError>() -> handle_parse(e as ParseError),
        typeof<IOError>() -> handle_io(e as IOError),
        ? -> println("Unknown error: " + typename(e))
    }
}
```

**Implementation**:
- `typeof<T>()` generates hash of type name T at compile-time
- Comparison is simple integer equality of two `i64` values
- No special AST node needed—uses existing BinaryExpression with EQEQ/NOTEQ

---

## Type Registry System

To support `typename()`, we need a runtime mapping from type ID → type name.

### Global Type Registry

**C++ Implementation**:
```cpp
// In runtime or codegen
std::unordered_map<uint64_t, const char*> g_type_registry;

void __vyb_register_type(uint64_t type_id, const char* type_name) {
    g_type_registry[type_id] = type_name;
}

const char* __vyb_get_typename(uint64_t type_id) {
    auto it = g_type_registry.find(type_id);
    return it != g_type_registry.end() ? it->second : "Unknown";
}
```

**Initialization**:
At module initialization, register all types:
```cpp
void __vyb_module_init() {
    __vyb_register_type(std::hash<std::string>{}("Int"), "Int");
    __vyb_register_type(std::hash<std::string>{}("String"), "String");
    __vyb_register_type(std::hash<std::string>{}("Bool"), "Bool");
    __vyb_register_type(std::hash<std::string>{}("ParseError"), "ParseError");
    // ... all user-defined types
}
```

This is called once at program startup, before `main()`.

---

## The `Type` Type

**Nature**: Opaque primitive type representing a type identity
**Size**: 8 bytes (uint64_t)
**Operations**: Equality comparison (`==`, `!=`)

**Usage**:
```vyb
# Declare variable of type Type
t<Type> = typeof(42)

# Compare types
if (t == typeof<Int>()) {
    println("It's an Int")
}

# Cannot do arithmetic or other ops on Type values
# t + 1  # ERROR: cannot add Type and Int
```

**Implementation**:
- Add `Type` as primitive type in semantic analyzer
- In LLVM codegen, represent as `i64`
- Only allow `==` and `!=` operators on Type values

---

## Implementation Phases

### Step 1: Lexer & Tokens ✅
Add new keywords:
```cpp
KEYWORD_TYPEOF,   // "typeof"
KEYWORD_TYPENAME, // "typename"
```

Update lexer to recognize these keywords in `src/parser/lexer.cpp`.

### Step 2: AST Nodes
Create new expression types:
```cpp
class TypeofExpression : public Expression {
public:
    ExprPtr operand;

    TypeofExpression(SourceLocation loc, ExprPtr operand)
        : Expression(loc), operand(std::move(operand)) {}

    NodeType getType() const override { return NodeType::TYPEOF_EXPRESSION; }
    void accept(Visitor* visitor) const override;
    std::string toString() const override;
};

class TypenameExpression : public Expression {
public:
    ExprPtr operand;

    TypenameExpression(SourceLocation loc, ExprPtr operand)
        : Expression(loc), operand(std::move(operand)) {}

    NodeType getType() const override { return NodeType::TYPENAME_EXPRESSION; }
    void accept(Visitor* visitor) const override;
    std::string toString() const override;
};
```

Add to NodeType enum:
```cpp
TYPEOF_EXPRESSION,
TYPENAME_EXPRESSION,
```

### Step 3: Parser
Parse `typeof(expr)` and `typename(expr)`:
```cpp
ExprPtr Parser::parsePrimaryExpression() {
    // ... existing code ...

    if (match(TokenType::KEYWORD_TYPEOF)) {
        SourceLocation loc = previous().location;
        consume(TokenType::LPAREN, "Expected '(' after 'typeof'");
        ExprPtr operand = parseExpression();
        consume(TokenType::RPAREN, "Expected ')' after typeof operand");
        return std::make_unique<TypeofExpression>(loc, std::move(operand));
    }

    if (match(TokenType::KEYWORD_TYPENAME)) {
        SourceLocation loc = previous().location;
        consume(TokenType::LPAREN, "Expected '(' after 'typename'");
        ExprPtr operand = parseExpression();
        consume(TokenType::RPAREN, "Expected ')' after typename operand");
        return std::make_unique<TypenameExpression>(loc, std::move(operand));
    }

    // ... existing code ...
}
```

### Step 4: Semantic Analysis
Add `Type` as primitive type:
```cpp
// In semantic analyzer initialization
registerPrimitiveType("Type");
```

For `TypeofExpression`:
- Analyze operand expression to determine its type
- Set expression result type to `Type`

For `TypenameExpression`:
- Analyze operand expression to determine its type
- Set expression result type to `String`

### Step 5: Code Generation

**For `TypeofExpression`**:
```cpp
void LLVMCodegen::visit(const TypeofExpression* node) {
    // Get the type name of the operand
    std::string typeName = getExpressionTypeName(node->operand.get());

    // Generate type hash as compile-time constant
    uint64_t typeHash = std::hash<std::string>{}(typeName);
    llvm::Value* typeId = llvm::ConstantInt::get(builder->getInt64Ty(), typeHash);

    m_currentLLVMValue = typeId;
}
```

**For `TypenameExpression`**:
```cpp
void LLVMCodegen::visit(const TypenameExpression* node) {
    // Get the type name of the operand
    std::string typeName = getExpressionTypeName(node->operand.get());

    // Create global string constant
    llvm::Constant* typeNameStr = builder->CreateGlobalStringPtr(typeName);

    // Create VybString struct
    llvm::Value* stringStruct = createVybString(typeName);

    m_currentLLVMValue = stringStruct;
}
```

**Type Registry Initialization**:
```cpp
void LLVMCodegen::generateModuleInit() {
    // Create __vyb_module_init function
    llvm::FunctionType* initType = llvm::FunctionType::get(voidType, {}, false);
    llvm::Function* initFunc = llvm::Function::Create(
        initType,
        llvm::Function::ExternalLinkage,
        "__vyb_module_init",
        module.get()
    );

    llvm::BasicBlock* entry = llvm::BasicBlock::Create(*context, "entry", initFunc);
    builder->SetInsertPoint(entry);

    // Register all types
    for (const auto& [typeName, typeInfo] : typeRegistry) {
        uint64_t typeHash = std::hash<std::string>{}(typeName);
        llvm::Value* typeId = llvm::ConstantInt::get(builder->getInt64Ty(), typeHash);
        llvm::Value* nameStr = builder->CreateGlobalStringPtr(typeName);

        // Call __vyb_register_type(type_id, type_name)
        llvm::Function* registerFn = getOrDeclareFunction("__vyb_register_type",
            llvm::FunctionType::get(voidType, {builder->getInt64Ty(), int8PtrType}, false));
        builder->CreateCall(registerFn, {typeId, nameStr});
    }

    builder->CreateRetVoid();
}
```

Call `__vyb_module_init()` from main or module constructor.

### Step 6: Runtime Support

Add to `include/vyb/runtime/type_info.hpp`:
```cpp
#pragma once

#include <cstdint>

extern "C" {

// Type registration
void __vyb_register_type(uint64_t type_id, const char* type_name);

// Type name lookup
const char* __vyb_get_typename(uint64_t type_id);

// Type comparison (inline, just integer equality)
inline bool __vyb_type_equals(uint64_t type_a, uint64_t type_b) {
    return type_a == type_b;
}

} // extern "C"
```

Implement in `src/runtime/type_info.cpp`:
```cpp
#include "vyb/runtime/type_info.hpp"
#include <unordered_map>
#include <cstring>

static std::unordered_map<uint64_t, const char*> g_type_registry;

extern "C" {

void __vyb_register_type(uint64_t type_id, const char* type_name) {
    g_type_registry[type_id] = type_name;
}

const char* __vyb_get_typename(uint64_t type_id) {
    auto it = g_type_registry.find(type_id);
    if (it != g_type_registry.end()) {
        return it->second;
    }
    return "Unknown";
}

} // extern "C"
```

---

## Integration with Error Handling

The error handling system **already** stores type IDs:
```cpp
struct VybError {
    void* type_id;  // Stored as void* (8 bytes)
    // ...
};
```

In wildcard trap handlers, we can now use introspection:
```vyb
{
    risky_operation()
} trap (e<?>) -> {
    # e is opaque pointer—we need to extract type
    error_type<Type> = __vyb_error_typeof(e)  # Built-in function

    if (error_type == typeof<ParseError>()) {
        println("It's a parse error")
    } else if (error_type == typeof<IOError>()) {
        println("It's an IO error")
    }
}
```

**Built-in Function**:
```cpp
// Extract type from error pointer
uint64_t __vyb_error_typeof(VybError* error) {
    return (uint64_t)error->type_id;
}
```

Or, we extend `typeof()` to work directly on wildcard error values:
```vyb
{
    operation()
} trap (e<?>) -> {
    if (typeof(e) == typeof<ParseError>()) {
        # Compiler knows to extract type_id from error struct
        println("Parse error!")
    }
}
```

The codegen for `typeof(e)` when `e` is a wildcard error:
```cpp
// e is VybError* pointer
// Load type_id field (offset 8 bytes after type_name pointer)
llvm::Value* typeIdPtr = builder->CreateStructGEP(errorType, errorPtr, 1);
llvm::Value* typeId = builder->CreateLoad(builder->getInt64Ty(), typeIdPtr);
return typeId;
```

---

## Testing Strategy

### Test 1: Basic typeof
```vyb
# test/introspection/typeof_basic.vyb
fn main()<Int> -> {
    x<Int> = 42
    t<Type> = typeof(x)

    if (t == typeof<Int>()) {
        return 0  # Success
    }
    return 1  # Failure
}
```

### Test 2: Basic typename
```vyb
# test/introspection/typename_basic.vyb
fn main()<Int> -> {
    x<Int> = 42
    name<String> = typename(x)

    # Compare string (need string equality)
    if (name.length == 3) {
        return 0  # Success (typename is "Int")
    }
    return 1
}
```

### Test 3: Type comparison
```vyb
# test/introspection/type_comparison.vyb
fn main()<Int> -> {
    x<Int> = 42
    y<String> = "hello"

    if (typeof(x) == typeof(y)) {
        return 1  # Should not happen
    }

    if (typeof(x) == typeof<Int>()) {
        return 0  # Success
    }

    return 2
}
```

### Test 4: Wildcard trap with typeof
```vyb
# test/introspection/wildcard_typeof.vyb
fn risky()<Int> -> {
    fail<ParseError>(ParseError { line: 10, column: 5 })
}

fn main()<Int> -> {
    {
        risky()
    } trap (e<?>) -> {
        if (typeof(e) == typeof<ParseError>()) {
            return 0  # Success
        }
        return 1
    }
    return 2  # Should not reach
}
```

### Test 5: Custom struct types
```vyb
# test/introspection/struct_typeof.vyb
struct Point {
    x<Int>,
    y<Int>
}

fn main()<Int> -> {
    p<Point> = Point { x: 10, y: 20 }

    if (typename(p) == "Point") {  # String comparison
        return 0
    }
    return 1
}
```

---

## Documentation Updates

### README.md
Add section on introspection:
```markdown
### Introspection

Vyb provides runtime type information through three operators:

**typeof(expr)** - Get runtime type identity:
```vyb
t<Type> = typeof(42)  # Type representing Int
```

**typename(expr)** - Get type name as string:
```vyb
name<String> = typename(42)  # "Int"
```

**Type comparison**:
```vyb
if (typeof(x) == typeof<Int>()) {
    println("It's an integer!")
}
```

These operators enable powerful patterns like wildcard error handling:
```vyb
{
    operation()
} trap (e<?>) -> {
    if (typeof(e) == typeof<ParseError>()) {
        println("Parse error: " + typename(e))
    }
}
```
```

### Status
Mark Phase 1 as complete, update status (`../TODO.md` for the living roadmap).

---

## Open Questions & Future Work

### Q1: Generics with typeof?
```vyb
fn identity<T>(value<T>)<T> -> {
    println(typename(value))  # What gets printed?
    return value
}

x<Int> = identity<Int>(42)  # Prints "Int"
```

**Answer**: Yes, `typename()` returns the **concrete type** after monomorphization.

### Q2: typeof on functions?
```vyb
fn add(a<Int>, b<Int>)<Int> -> { return a + b }

t<Type> = typeof(add)  # Type representing "fn(Int, Int) -> Int"
```

**Answer**: Phase 1 supports only value types. Function types deferred to Phase 4.

### Q3: Collision handling for hash?
`std::hash<std::string>{}` can theoretically collide.

**Answer**: In practice, extremely rare for reasonable type names. If needed, Phase 3 can switch to perfect hashing or type ID generation at compile-time.

### Q4: Performance impact?
Each `typeof()` call generates a compile-time constant—zero runtime cost.
Each `typename()` call loads a string pointer—minimal cost (one load + string wrap).

**Answer**: Performance impact is negligible.

---

## Summary: The Three Truths

1. **typeof(value)** reveals the essence: *What is this?*
2. **typename(value)** names the essence: *What is it called?*
3. **typeof(a) == typeof(b)** compares essences: *Are these the same?*

Together, these form the foundation of self-aware code—code that can examine its own nature and make decisions accordingly.

**Phase 1 Complete**: Basic type information ✅
**Next**: Phase 2 - Safe Downcasting with `as` operator 🔮

---

*"To understand the world, first understand thyself."* - Ancient Zen Proverb (and also good software engineering)
