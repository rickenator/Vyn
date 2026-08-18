# AST Nodes: Statements

This document describes the Abstract Syntax Tree (AST) nodes for various statements in the Vyb programming language, as defined in `include/vyb/parser/ast.hpp`. Statements are units of execution. All statement nodes inherit from `vyb::ast::Statement`.

## Common Pointer Aliases

Throughout the AST definitions, the following smart pointer aliases are used for brevity and clarity, representing `std::unique_ptr<T>`:

- `ExprPtr = std::unique_ptr<Expression>;`
- `StmtPtr = std::unique_ptr<Statement>;`
- `IdentifierPtr = std::unique_ptr<Identifier>;`
- `BlockStatementPtr = std::unique_ptr<BlockStatement>;`
- `TypeNodePtr = std::unique_ptr<TypeNode>;`

## 1. `BlockStatement`

Represents a block of statements, typically enclosed in braces `{}`.

-   **C++ Class**: `vyb::ast::BlockStatement`
-   **`NodeType`**: `BLOCK_STATEMENT`
-   **Fields**:
    -   `statements` (`std::vector<StmtPtr>`): A vector of pointers to the statements contained within the block.

```cpp
// From include/vyb/parser/ast.hpp
namespace vyb::ast {
class BlockStatement : public Statement {
public:
    std::vector<StmtPtr> statements;

    BlockStatement(SourceLocation loc, std::vector<StmtPtr> statements);
    // ... accept, getType, toString methods ...
};
} // namespace vyb::ast
```

## 2. `ExpressionStatement`

Represents a statement that consists solely of an expression.

-   **C++ Class**: `vyb::ast::ExpressionStatement`
-   **`NodeType`**: `EXPRESSION_STATEMENT`
-   **Fields**:
    -   `expression` (`ExprPtr`): The expression that forms the statement.

```cpp
// From include/vyb/parser/ast.hpp
namespace vyb::ast {
class ExpressionStatement : public Statement {
public:
    ExprPtr expression;

    ExpressionStatement(SourceLocation loc, ExprPtr expression);
    // ... accept, getType, toString methods ...
};
} // namespace vyb::ast
```

## 3. `IfStatement`

Represents an if-conditional statement.

-   **C++ Class**: `vyb::ast::IfStatement`
-   **`NodeType`**: `IF_STATEMENT`
-   **Fields**:
    -   `condition` (`ExprPtr`): The expression evaluated to determine which branch to execute.
    -   `thenBranch` (`StmtPtr`): The statement (often a `BlockStatement`) executed if the condition is true.
    -   `elseBranch` (`std::optional<StmtPtr>`): The statement executed if the condition is false.

```cpp
// From include/vyb/parser/ast.hpp
namespace vyb::ast {
class IfStatement : public Statement {
public:
    ExprPtr condition;
    StmtPtr thenBranch;
    std::optional<StmtPtr> elseBranch;

    IfStatement(SourceLocation loc, ExprPtr condition, StmtPtr thenBranch, std::optional<StmtPtr> elseBranch);
    // ... accept, getType, toString methods ...
};
} // namespace vyb::ast
```

## 4. `ForStatement`

Represents a for loop. Supports both range-based iteration and Vec<T> iteration.

-   **C++ Class**: `vyb::ast::ForStatement`
-   **`NodeType`**: `FOR_STATEMENT`
-   **Fields**:
    -   `iterator` (`IdentifierPtr`): The identifier for the loop variable.
    -   `range` (`ExprPtr`): The expression that evaluates to the range or collection.
    -   `body` (`StmtPtr`): The statement (usually a `BlockStatement`) executed for each iteration.

**Syntax Requirements (v0.4.1):**
- Parentheses are **mandatory**: `for (item in expr)` not `for item in expr`
- Supports range expressions: `for (i in 0..10)` inclusive ranges
- Supports Vec iteration: `for (item in vec)` where vec is a Vec<T>
- Break and continue statements work in all for loop variants

**Desugaring:**
Vec iteration desugars to index-based loops:
```vyb
for (item in vec) { body }
// Becomes:
for (__run_once = true; __run_once; __run_once = false) {
    var __len = vec.len();
    for (__idx = 0; __idx < __len; __idx = __idx + 1) {
        var item = vec.get(__idx);
        body;
    }
}
```

```cpp
// From include/vyb/parser/ast.hpp
namespace vyb::ast {
class ForStatement : public Statement {
public:
    IdentifierPtr iterator;
    ExprPtr range;
    StmtPtr body;

    ForStatement(SourceLocation loc, IdentifierPtr iterator, ExprPtr range, StmtPtr body);
    // ... accept, getType, toString methods ...
};
} // namespace vyb::ast
```

## 5. `WhileStatement`

Represents a while loop.

-   **C++ Class**: `vyb::ast::WhileStatement`
-   **`NodeType`**: `WHILE_STATEMENT`
-   **Fields**:
    -   `condition` (`ExprPtr`): The expression evaluated before each iteration.
    -   `body` (`StmtPtr`): The statement (usually a `BlockStatement`) executed in each iteration.

```cpp
// From include/vyb/parser/ast.hpp
namespace vyb::ast {
class WhileStatement : public Statement {
public:
    ExprPtr condition;
    StmtPtr body;

    WhileStatement(SourceLocation loc, ExprPtr condition, StmtPtr body);
    // ... accept, getType, toString methods ...
};
} // namespace vyb::ast
```

## 6. `ReturnStatement`

Represents a return statement.

-   **C++ Class**: `vyb::ast::ReturnStatement`
-   **`NodeType`**: `RETURN_STATEMENT`
-   **Fields**:
    -   `argument` (`ExprPtr`): The expression whose value is returned (can be nullptr for void returns).

```cpp
// From include/vyb/parser/ast.hpp
namespace vyb::ast {
class ReturnStatement : public Statement {
public:
    ExprPtr argument; // Optional, can be nullptr

    ReturnStatement(SourceLocation loc, ExprPtr argument = nullptr);
    // ... accept, getType, toString methods ...
};
} // namespace vyb::ast
```

### Auto-Serialization Behavior

When a `ReturnStatement` appears in a `main()` function, the Vyb runtime automatically serializes the returned value(s) to JSON format:

- **Single values**: Serialized directly (e.g., `return 42` → `42`)
- **Multiple values**: Comma-separated expressions are serialized as a JSON object with type annotations (e.g., `return 42, "hello"` → `{"Int":42,"String":"hello"}`)
- **Complex types**: Structs and other complex types are serialized with field metadata

For comprehensive documentation on auto-serialization behavior and serialization intrinsics (`lit()`, `notype()`, `bare()`, `deserial()`), see [`Auto_Serialization_Main_Returns.md`](./Auto_Serialization_Main_Returns.md).

## 7. `BreakStatement`

Represents a break statement.

-   **C++ Class**: `vyb::ast::BreakStatement`
-   **`NodeType`**: `BREAK_STATEMENT`

```cpp
// From include/vyb/parser/ast.hpp
namespace vyb::ast {
class BreakStatement : public Statement {
public:
    BreakStatement(SourceLocation loc);
    // ... accept, getType, toString methods ...
};
} // namespace vyb::ast
```

## 8. `ContinueStatement`

Represents a continue statement.

-   **C++ Class**: `vyb::ast::ContinueStatement`
-   **`NodeType`**: `CONTINUE_STATEMENT`

```cpp
// From include/vyb/parser/ast.hpp
namespace vyb::ast {
class ContinueStatement : public Statement {
public:
    ContinueStatement(SourceLocation loc);
    // ... accept, getType, toString methods ...
};
} // namespace vyb::ast
```

## 9. `MatchStatement`

Represents a pattern matching statement with `=>` syntax.

-   **C++ Class**: `vyb::ast::MatchStatement`
-   **`NodeType`**: `MATCH_STATEMENT`
-   **Fields**:
    -   `expr` (`ExprPtr`): The expression being matched against.
    -   `cases` (`std::vector<std::pair<ExprPtr, ExprPtr>>`): Vector of pattern-result pairs, where each pair contains a pattern expression and the corresponding result expression.

```cpp
// From include/vyb/parser/ast.hpp
namespace vyb::ast {
class MatchStatement : public Statement {
public:
    ExprPtr expr;
    std::vector<std::pair<ExprPtr, ExprPtr>> cases;

    MatchStatement(SourceLocation loc, ExprPtr expr, std::vector<std::pair<ExprPtr, ExprPtr>> cases);
    // ... accept, getType, toString methods ...
};
} // namespace vyb::ast
```

**Example Usage:**
```vyb
match x {
    42 => println("The answer"),
    0 => println("Zero"),
    _ => println("Something else")
}
```

The `MatchStatement` enables comprehensive pattern matching with:
- **Value matching**: Direct comparison with literals (e.g., `42`, `"hello"`)
- **Wildcard patterns**: `_` matches any value
- **Fat arrow syntax**: `=>` separates patterns from results
- **Exhaustive checking**: Compiler ensures all cases are covered

## 10. `FailExpression` / `TrapStatement`

Vyb does not have `throw`, `try`, `catch`, or `finally`. Error handling is done via
`fail`/`trap`. The `ThrowStatement` and `TryStatement` AST nodes documented in older
versions of this file are **removed from the language spec**.

### `FailExpression`

Represents `fail<ErrorType>(value)` — typed error propagation.

-   **C++ Class**: `vyb::ast::FailExpression`
-   **`NodeType`**: `FAIL_EXPRESSION`
-   **Fields**:
    -   `errorType` (`TypeNodePtr`): The error type being propagated.
    -   `value` (`ExprPtr`): The error value expression.

### `TrapStatement`

Represents a `trap` handler that intercepts a `fail`-propagated error.

-   **C++ Class**: `vyb::ast::TrapStatement`
-   **`NodeType`**: `TRAP_STATEMENT`
-   **Fields**:
    -   `body` (`BlockStatementPtr`): The block to execute, which may propagate a `fail`.
    -   `handlers` (`std::vector<TrapHandlerPtr>`): Error type → handler block pairs.

> See `doc/ERROR_TRAP.md` for the full `fail`/`trap` design.

## 11. `ScopedStatement`

Represents a statement that explicitly creates a new lexical scope.

-   **C++ Class**: `vyb::ast::ScopedStatement`
-   **`NodeType`**: `SCOPED_STATEMENT`
-   **Fields**:
    -   `body` (`StmtPtr`): The statement (typically a `BlockStatement`) executed within the new scope.

```cpp
// From include/vyb/parser/ast.hpp
namespace vyb::ast {
class ScopedStatement : public Statement {
public:
    StmtPtr body; // Likely a BlockStatement

    ScopedStatement(SourceLocation loc, StmtPtr body);
    // ... accept, getType, toString methods ...
};
} // namespace vyb::ast
```

## 12. `EnsureStatement`

Represents an `ensure` contract statement (planned).

-   **C++ Class**: `vyb::ast::EnsureStatement` (planned)
-   **`NodeType`**: `ENSURE_STATEMENT`
-   **Fields**:
    -   `condition` (`ExprPtr`): The condition that must hold.
    -   `failExpression` (`std::optional<ExprPtr>`): The `fail` expression if the condition is false.

```cpp
// Planned — not yet implemented
// ensure b != 0 else fail<DivisionError>(DivisionError { dividend: a })
```

## 13. `UnsafeBlockStatement`

Represents an freedom code block where memory operations are allowed.

-   **C++ Class**: `vyb::ast::UnsafeBlockStatement`
-   **`NodeType`**: `UNSAFE_BLOCK_STATEMENT`
-   **Fields**:
    -   `body` (`BlockStatementPtr`): A pointer to the block statement containing the code in the freedom block.

```cpp
// From include/vyb/parser/ast.hpp
namespace vyb::ast {
class UnsafeBlockStatement : public Statement {
public:
    BlockStatementPtr body;

    UnsafeBlockStatement(SourceLocation loc, BlockStatementPtr body);
    // ... accept, getType, toString methods ...
};
} // namespace vyb::ast
```

Freedom blocks are used to contain low-level memory operations that could be freedom if used incorrectly. Within an freedom block, you can:

1. Create raw pointers with `loc<T>(expr)`
2. Dereference pointers with `at(ptr)`
3. Convert between pointer types with `from<loc<T>>(addr)`

Example:
```vyb
freedom {
    var<loc<Int>> p = loc(x);
    at(p) = 99;  // Modify the value at the pointer location
    var<Int> q = at(p); // Read the value at the pointer location
}
```

These operations are freedom because they bypass Vyb's memory safety guarantees, allowing for potential errors like null pointer dereferences, dangling pointers, and memory corruption.

*Note: `PatternAssignmentStatement` is mentioned in `AST_Roadmap.md` but not currently present in `vyb/parser/ast.hpp`'s statement definitions. It will be added here once implemented.*
