# Vyb AST: Design Considerations

This document discusses various design choices, alternatives considered, and rationale behind the Vyb AST structure. It also covers topics like memory management, error handling, and potential future enhancements based on review feedback and language goals.

## 1. Memory Management

*(Original AST.md did not explicitly detail this, but it's a crucial consideration. This section is based on common C++ AST practices and review suggestion 7.)*

**Current Approach (Assumed):**
Typically, AST nodes in C++ are dynamically allocated on the heap. Smart pointers, particularly `std::unique_ptr`, are often used to manage the lifecycle of these nodes. For Vyb, `PNode`, `PExpression`, `PStatement`, etc., are typedefs for `std::shared_ptr<Node>`, `std::shared_ptr<Expression>`, etc., as seen in `ast.hpp`.

**Considerations for `std::shared_ptr`:**
-   **Pros:**
    -   Simplifies ownership in complex scenarios where multiple parts of the compiler might temporarily need access to AST subtrees (e.g., during transformations or analysis passes that don't modify the tree but hold references).
    -   Can prevent dangling pointers if nodes are referenced from multiple places (though this should be minimized in a tree structure).
-   **Cons:**
    -   Higher overhead (both memory and performance) compared to `std::unique_ptr` due to reference counting.
    -   Can lead to cyclic dependencies if not careful (e.g., parent pointers holding `shared_ptr` to children, and children holding `shared_ptr` to parent), though parent pointers are not currently implemented.

**Alternative: `std::unique_ptr`:**
-   **Pros:**
    -   Clear ownership model: each node is owned by its parent or the primary AST structure (e.g., a vector of statements in a block).
    -   Lower overhead than `std::shared_ptr`.
-   **Cons:**
    -   Requires careful management when passing nodes around. Non-owning raw pointers or references (`Node*`, `Node&`) would be used for observation or non-modifying access by other compiler phases.

**Review Suggestion 7: Clarify Memory Ownership**
> "The document should explicitly state the memory ownership model for AST nodes. Are they owned by `std::unique_ptr` within their parent nodes? Or is there a central arena allocator? Who is responsible for deleting them? Given the use of `std::shared_ptr` (e.g. `PExpression`), this implies shared ownership, which is unusual for a primary tree structure and can have performance implications. Is this intentional, and why?"

**Clarification:**
The Vyb AST currently uses `std::shared_ptr` for its node pointers (`PNode`, `PExpression`, etc.). This choice was likely made to simplify early development and potentially to accommodate complex tree transformations or analyses where subtrees might be temporarily referenced from multiple places. However, the performance implications and the risk of cycles (especially if parent pointers were introduced) are valid concerns.

**Future Direction:**
-   Re-evaluate the use of `std::shared_ptr`. For a strict tree structure, `std::unique_ptr` is generally preferred for ownership, with raw pointers or references for non-owning access.
-   Consider an arena allocator for AST nodes. This can significantly improve allocation performance and simplify deallocation (free the entire arena at once after compilation).

## 2. Parent Pointers and Scope Links

*(Based on review suggestion 4)*

**Review Suggestion 4: Consider Parent Pointers or Scope Links**
> "The `Node` base class doesn't include a `parent` pointer. While this simplifies construction and memory management (no cycles with `unique_ptr`), it makes navigating upwards in the tree or finding enclosing scopes/nodes more complex. Has this been considered? What are the strategies for contextual analysis (e.g., resolving identifiers, type checking) without direct parent links? Perhaps a separate symbol table or scope stack is used?"

**Current Status:**
The `vyb::ast::Node` class in `ast.hpp` does not currently include a `parent` pointer.

**Discussion:**
-   **Pros of No Parent Pointers:**
    -   Simpler node structure and construction.
    -   Avoids potential `std::shared_ptr` cycles if parent pointers were also `shared_ptr`.
    -   Makes tree transformations (like replacing a node) slightly easier as only child pointers need updating.
-   **Cons of No Parent Pointers:**
    -   Navigating upwards in the tree (e.g., to find an enclosing function or scope) requires passing down context or using external mechanisms.
    -   Some analyses might be more complex.

**Strategies for Contextual Analysis without Parent Pointers:**
-   **Visitor with Context:** Pass necessary contextual information (e.g., current scope, symbol table, expected type) down the tree as parameters to visitor methods.
-   **External Scope/Symbol Table Stack:** Maintain a separate stack of symbol tables or scope objects during semantic analysis. As the visitor enters/exits scopes (e.g., `BlockStatement`, `FunctionDeclaration`), the stack is pushed/popped.
-   **Post-hoc Tree Annotation:** An analysis pass could annotate nodes with relevant information derived from their context, without storing direct parent pointers.

**Future Consideration:**
While the current approach relies on visitors passing context, the utility of parent pointers (perhaps non-owning raw pointers to avoid ownership cycles) could be revisited if specific analysis phases prove to be overly complex without them. For now, the explicit passing of context or use of symbol table stacks is the intended method.

## 3. Error Handling and `ErrorNode`

*(Based on review suggestion 8)*

**Review Suggestion 8: Consider `ErrorNode`**
> "How are parsing errors that still allow for partial AST construction handled? For example, if a statement is malformed but the parser can recover and parse subsequent statements. Is there a concept of an `ErrorNode` or similar to represent these invalid constructs in the tree, allowing later phases to be aware of them or ignore them?"

**Current Status:**
The AST definition in `ast.hpp` does not explicitly include an `ErrorNode` or a similar mechanism for representing parsing errors directly within the tree structure that allow for partial recovery.

**Discussion:**
-   **Parser Error Recovery:** The Vyb parser (`Parser` class and its components) attempts to recover from errors to provide multiple diagnostics. However, how these recovered-but-still-erroneous constructs are represented in the AST is key.
-   **Benefits of an `ErrorNode`:**
    -   Allows the parser to insert a placeholder in the AST when it encounters a construct it can't fully parse but can recover from.
    -   Enables later compilation phases (e.g., semantic analysis) to be aware of these errors. They can choose to skip over `ErrorNode`s or report that they cannot proceed due to earlier parsing failures.
    -   Can improve IDE integration by allowing a partially correct AST to still be useful for features like outlining or partial code completion.
-   **Implementation Options for `ErrorNode`:**
    -   A specific `ErrorNode` class inheriting from `Node` (or `Expression`/`Statement` if the error occurred in such a context).
    -   A flag or special state on existing nodes (less clean).

**Future Direction:**
Introducing an `ErrorNode` (or multiple types, like `ErrorExpression`, `ErrorStatement`) is a valuable enhancement. This would allow the parser to produce a more complete AST even in the presence of errors, which can aid subsequent analysis and tooling. The `ErrorNode` could store information about the error, such as an error message and the tokens consumed while attempting to parse the erroneous construct.

## 4. Visitor Pattern and Boilerplate

*(Based on review suggestion 9)*

**Review Suggestion 9: Reduce Visitor Boilerplate**
> "The Visitor pattern is good, but the interface requires a `visit` method for every concrete node type. As the number of nodes grows, this becomes a lot of boilerplate. Have alternatives like a single `visit(Node&)` with dynamic dispatch (e.g., `node.dispatch(this)`) or using `if-else if` with `dynamic_cast` or `node.getType()` in a default `visit(Node&)` method been considered to reduce boilerplate for visitors that only care about a few node types?"

**Current Visitor Implementation:**
The `Visitor` class in `ast.hpp` defines a pure virtual `visit` method for each concrete AST node type.

**Discussion:**
-   **Pros of Current Approach (Acyclic Visitor):**
    -   Type-safe: The correct `visit` overload is called by the node's `accept` method.
    -   Explicit: Clearly shows all node types that must be handled.
-   **Cons:**
    -   Boilerplate: Visitors must implement all `visit` methods, even if many are empty or delegate to a default handler.
    -   Adding new node types requires updating all visitor interfaces and implementations.

**Alternatives to Reduce Boilerplate:**
1.  **Default Visit Methods in Base Visitor:**
    Create a base `Visitor` class where `visit` methods for specific nodes can delegate to more general handlers (e.g., `visit(Expression&)` or a `defaultVisit(Node&)`).
    ```cpp
    // Example BaseVisitor
    class BaseVisitor : public Visitor {
    public:
        virtual void defaultVisit(Node& node) { /* common logic or ignore */ }
        virtual void visit(Identifier& node) override { defaultVisit(node); }
        virtual void visit(IntegerLiteral& node) override { defaultVisit(node); }
        // ... and so on for all nodes, delegating to defaultVisit
    };
    ```
    Concrete visitors would then inherit from `BaseVisitor` and override only the methods for nodes they care about.

2.  **Single `visit(Node&)` with `getType()`:**
    A single `visit(Node& node)` method in the visitor, which then uses `node.getType()` in a `switch` statement or `if-else if` chain to handle different node types. This loses some type safety at compile time for the dispatch itself.

3.  **CRTP (Curiously Recurring Template Pattern) for Visitors:**
    Can be used to automatically provide default implementations or routing.

**Future Direction:**
Providing a `BaseVisitor` (as in option 1) with default implementations (e.g., that recursively visit children or do nothing) is a common and effective way to reduce boilerplate for visitors that only need to handle a subset of node types. This maintains type safety while improving ease of use.

## 5. `PathNode` Implementation and Usage

*(Based on review suggestion 3)*

**Review Suggestion 3: Clarify `PathNode` Implementation**
> "The EBNF mentions `path_expression` (e.g., `foo::bar::Baz`) which is common for qualified names. How is this represented in the AST? Is there a dedicated `PathNode` or `QualifiedIdentifierNode`? The `MemberExpression` seems to be for `object.member`. If `MemberExpression` is used for `foo::bar`, it might be confusing. `TypeNode` also has a `name` string which might store a full path."

**Current Status:**
-   `MemberExpression` (`object.member`) is defined for field/method access.
-   `TypeNode` has a `std::string name` which could store a simple name or a fully qualified name.
-   The parser logic, especially in `TypeParser` and `ExpressionParser`, handles name resolution.
-   There isn't an explicit, separate `PathNode` or `QualifiedIdentifierNode` detailed in the main AST node list, though `ObjectLiteral` now has an optional `PExpression typePath` which could be an `Identifier` or a sequence of them.

**Discussion:**
Representing qualified paths (e.g., `module::type`, `enum::variant`) is crucial.
-   **Option 1: Re-purpose `MemberExpression`:** Using `MemberExpression` for `foo::bar` could work if `foo` is treated as an expression evaluating to a module or namespace object, and `bar` is its member. This might be semantically overloaded.
-   **Option 2: String in `Identifier` or `TypeNode`:** Store the full path `"foo::bar::Baz"` as a string. This is simple but requires parsing the path string during semantic analysis.
-   **Option 3: Dedicated `PathExpression` or `QualifiedIdentifier` Node:**
    A `PathExpression` node could hold a sequence of `Identifier`s representing the segments of the path.
    ```cpp
    // Conceptual PathExpression
    class PathExpression : public Expression {
    public:
        std::vector<PIdentifier> segments;
        // bool isAbsolute; // Optional: for paths starting with ::
    };
    ```
    This provides a structured representation.
-   **Option 4: `PExpression` for type paths:** As seen with `ObjectLiteral::typePath`, using a generic `PExpression` allows flexibility. The parser would likely produce an `Identifier` for simple names or a chain of `MemberExpression`s (if used for `::`) or a dedicated `PathExpression` if implemented.

**Current Implementation Detail (from `expression_parser.cpp` changes):**
The parser was updated to use `TypeParser::parse_path()` which returns a `PExpression`. This `PExpression` is likely an `Identifier` for single segment paths or a chain of `MemberExpression`s if `::` is parsed similarly to `.` for paths.

**Clarification & Future Direction:**
-   The use of `PExpression` for `typePath` in `ObjectLiteral` and for `typeName` in `ConstructionExpression` suggests that paths are treated as general expressions. The parser likely constructs these as `Identifier` nodes for simple names or a chain of `MemberExpression`s where the `object` is the preceding part of the path and `member` is the next segment.
-   While this works, a dedicated `PathExpression` node could offer a clearer semantic distinction for name qualification versus object member access. This would be particularly useful for type names, module paths, and enum variant paths.
-   For now, the convention seems to be that `MemberExpression` might be used by the parser for `foo::bar` by treating `::` similarly to `.`, or `TypeParser::parse_path()` might be constructing a specific internal representation that resolves to an `Identifier` with a potentially qualified name string.
-   The documentation for `MemberExpression` should clarify if it's also intended for static path resolution (`foo::bar`) in addition to dynamic member access (`instance.field`). If not, a `PathExpression` should be formally introduced.

## 6. `TypeNode` Grammar vs. AST Reconciliation

*(Based on review suggestion 6)*

**Review Suggestion 6: Reconcile `TypeNode` Grammar vs. AST**
> "The `TypeNode` in AST.md has `name` and `parameters` (for generics). The EBNF grammar likely supports more complex types (pointers `*T`, arrays `[T; N]`, tuples `(A, B)`, function types `fn(A)->B`). How are these mapped to `TypeNode`? Are there subclasses of `TypeNode` (e.g., `PointerTypeNode`, `ArrayTypeNode`) or are these distinctions encoded within `TypeNode` (e.g., flags, conventional names like `name="*int"`)?"

**Current `TypeNode` Structure:**
`vyb::ast::TypeNode` has `std::string name` and `std::vector<PTypeNode> parameters`.

**Discussion:**
This was partially addressed in `AST_Types.md`. The core issue is how the single `TypeNode` class represents the diversity of type constructs found in the language grammar:
-   **Pointers (`*T`, `*mut T`):** Could be `TypeNode { name: "*T" }` or `TypeNode { name: "T", isPointer: true }`.
-   **Arrays (`[T; N]`, `[T]`):** Could be `TypeNode { name: "[T; N]" }` or `TypeNode { name: "T", isArray: true, size: N }`.
-   **Tuples (`(A, B)`):** Could be `TypeNode { name: "(A,B)" }` or a dedicated `TupleTypeNode` holding a list of `PTypeNode`.
-   **Function Types (`fn(A) -> B`):** Could be `TypeNode { name: "fn(A)->B" }` or a dedicated `FunctionTypeNode` with fields for parameters and return type.

**Current Approach (Inferred from `ast.hpp` and typical parsers):**
-   The `TypeParser` is responsible for parsing type syntax and constructing `TypeNode` instances.
-   It's likely that complex types are either:
    1.  **Normalized into a string form for `TypeNode::name`**: e.g., `*int`, `[int; 5]`. This is simple for the AST structure but defers parsing of the type structure to semantic analysis.
    2.  **Handled by specific fields if they exist**: If `TypeNode` had `isPointer`, `isArray`, etc., flags.
    3.  The `parameters` field is primarily for generic arguments like `List<int>` (`TypeNode { name: "List", parameters: [TypeNode { name: "int" }] }`).

**Future Direction/Clarification:**
-   The `AST_Types.md` document should be the primary source for how `TypeNode` represents these. If the C++ `TypeNode` is simple, it should be stated that the `name` field often carries structured information that semantic analysis will decode.
-   Introducing specialized `TypeNode` subclasses (e.g., `PointerType`, `ArrayType`, `TupleType`, `FunctionType`) inheriting from a base `TypeNode` would create a more explicit and structured AST for types. This would make semantic analysis more straightforward as it could dispatch on the specific type node kind.
-   The choice depends on the trade-off between AST simplicity and explicitness for later phases. Given Vyb's feature set, a more structured type representation in the AST (subclasses) is likely beneficial in the long run.

## 7. Pattern Matching Nodes and `accept()`

*(Based on review suggestion 5)*

**Review Suggestion 5: Sketch `accept()` for Pattern Nodes**
> "The EBNF implies pattern matching (e.g., in `let` or `match`). While `PatternNode` is mentioned as conceptual, if/when these are added, they will also need `accept()` methods for visitors. It might be good to sketch what a few pattern nodes might look like (e.g., `IdentifierPattern`, `TuplePattern`, `StructPattern`) and how they'd fit into the Visitor pattern."

**Current Status:**
Pattern nodes are planned but not yet implemented as concrete AST nodes with visitor support. `AST_Patterns.md` is a placeholder.

**Conceptual Pattern Nodes:**
-   `IdentifierPattern`: Matches a value and binds it to an identifier (e.g., `x` in `let x = ...`). May include `isMutable`.
-   `LiteralPattern`: Matches a specific literal value (e.g., `1`, `"hello"`, `true`).
-   `TuplePattern`: Destructures a tuple (e.g., `(a, b)`).
    -   Fields: `std::vector<PPattern> elements;`
-   `StructPattern`: Destructures a struct (e.g., `Point { x, y }`).
    -   Fields: `PExpression structName; std::vector<std::pair<PIdentifier, PPattern>> fields; bool ignoreRest;`
-   `EnumVariantPattern`: Matches and destructures an enum variant (e.g., `Shape::Circle(r)`).
    -   Fields: `PExpression enumPath; PIdentifier variantName; std::vector<PPattern> arguments;`
-   `WildcardPattern`: Matches anything without binding (`_`).
-   `RangePattern`: Matches a value within a range (e.g., `1..=5`).

**Visitor Integration:**
Each concrete pattern node would inherit from a base `PatternNode` (which inherits from `vyb::ast::Node`).
```cpp
// Conceptual base
class PatternNode : public Node { /* ... */ };

// Example: IdentifierPattern
class IdentifierPattern : public PatternNode {
public:
    PIdentifier name;
    bool isMutable;
    // ... constructor ...
    NodeType getType() const override { return NodeType::IDENTIFIER_PATTERN; } // New NodeType needed
    void accept(Visitor& visitor) override { visitor.visit(*this); }
};
```

The `Visitor` interface would need corresponding `visit` methods:
```cpp
class Visitor {
public:
    // ... other visit methods ...
    virtual void visit(IdentifierPattern& node) = 0;
    virtual void visit(LiteralPattern& node) = 0;
    virtual void visit(TuplePattern& node) = 0;
    virtual void visit(StructPattern& node) = 0;
    // ... etc. for all pattern types
};
```

**Future Direction:**
When pattern matching is implemented, these nodes and their visitor methods will need to be added to `ast.hpp`, `ast.cpp`, `NodeType`, and all visitor implementations. `AST_Patterns.md` will need to be fully populated.

This covers the main design considerations based on the review and general AST practices. These points should guide the ongoing development and refinement of the Vyb AST.
