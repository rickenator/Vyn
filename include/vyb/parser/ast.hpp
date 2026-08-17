// SPDX-License-Identifier: Apache-2.0

#ifndef VYB_PARSER_AST_HPP
#define VYB_PARSER_AST_HPP

#include <string>
#include <vector>
#include <memory>
#include <variant>
#include <optional>
#include <iostream> // Added for std::cout
#include <cstdint>
#include "token.hpp"
#include "source_location.hpp"

namespace vyb {
namespace ast {

// Forward declarations - ALL AST NODE TYPES must be declared here
class Node;
class Module;
class Expression;
class Statement;
class Declaration;
class Visitor;

// Literals
class Identifier;
class IntegerLiteral;
class FloatLiteral;
class StringLiteral;
class BooleanLiteral;
class ArrayLiteral;
class ObjectLiteral;
class NilLiteral;

// Expressions
class UnaryExpression;
class BinaryExpression;
class CallExpression;
class MemberExpression;
class AssignmentExpression;
class BorrowExpression;
class PointerDerefExpression;
class AddrOfExpression;
class FromIntToLocExpression;
class ArrayElementExpression;
class LocationExpression;
class ListComprehension;
class IfExpression;
class ConstructionExpression;
class ArrayInitializationExpression;
class GenericInstantiationExpression;
class LogicalExpression; // Added
class ConditionalExpression; // Added
class SequenceExpression; // Added
class FunctionExpression; // Added
class ThisExpression; // Added
class SuperExpression; // Added
class AwaitExpression; // Added
class RangeExpression; // Added for range-based for loops
class BlockExpression; // Block as expression for match arms
class SelectExpression; // Select expression for pattern matching with returns
class ComparisonPattern; // Comparison pattern for match/select (e.g., >= 18)
class StructPattern; // Struct destructuring pattern (e.g., Point { x, y })
class TypeofExpression; // Introspection: typeof(expr) returns Type
class TypenameExpression; // Introspection: typename(expr) returns String
class AsExpression; // Introspection/safe downcasting: value as TargetType

// Statements
class BlockStatement;
class ExpressionStatement;
class IfStatement;
class ForStatement;
class WhileStatement;
class ReturnStatement;
class PassStatement;
class BreakStatement;
class ContinueStatement;
class TryStatement;
class UnsafeStatement;
class EmptyStatement;
class ExternStatement; // Added
class ThrowStatement; // Added
class MatchStatement; // Added
class MatchExpression; // match used as a value-returning expression
class YieldStatement; // Added
class YieldReturnStatement; // Added
class AssertStatement; // Added
class FailStatement; // Error handling: fail with error value
class TrapClause; // Error handling: trap clause for handling failures
class EnsureClause; // Error handling: ensure clause for cleanup
class RethrowStatement; // Error handling: rethrow current error
class PanicStatement; // Error handling: unrecoverable panic
class ExitStatement;  // Process exit with code: exit(n)
class DeferStatement; // Deferred execution at scope exit
class TupleDestructureAssignment; // Tuple/multi-var destructuring assignment

// Declarations
class VariableDeclaration;
class FunctionDeclaration;
class TypeAliasDeclaration;
class ImportDeclaration;
class StructDeclaration;
class ClassDeclaration;
class FieldDeclaration;
class BindDeclaration;
class EnumDeclaration;
class EnumVariant;
class GenericParameter;
class TemplateDeclaration;
class AspectDeclaration; // Added
class NamespaceDeclaration; // Added

// Types (These are typically part of TypeNode or used as type specifiers,
// but if they are distinct visitable AST nodes, they need to be here)
class TypeNode;
class TypeName; // Added
class PointerType; // Added
class ArrayType; // Added
class VecType; // Added
class FutureType; // Added for async/await support
class FunctionType; // Added
class OptionalType; // Added
class TupleTypeNode; // ADDED FORWARD DECLARATION


// --- Enums used by AST nodes ---
enum class BorrowKind {
    MUTABLE_BORROW,
    IMMUTABLE_VIEW
    // Add other kinds if necessary, e.g., UNIQUE_OWNERSHIP_TRANSFER
};

enum class OwnershipKind {
    MY,    // Unique ownership
    OUR,   // Shared ownership (e.g., reference counted)
    THEIR, // Borrowed/Viewed (non-owning), further specified by BorrowKind if applicable
    MILD,  // Weak reference (non-owning, can detect if target is released)
    PTR    // Raw pointer (potentially non-owning, freedom)
    // Add other kinds as needed
};


// --- Type aliases for smart pointers ---
using NodePtr = std::unique_ptr<Node>;
using ExprPtr = std::unique_ptr<Expression>;
using StmtPtr = std::unique_ptr<Statement>;
using DeclPtr = std::unique_ptr<Declaration>;
using TypeNodePtr = std::unique_ptr<TypeNode>;
using IdentifierPtr = std::unique_ptr<Identifier>;
using ArrayLiteralPtr = std::unique_ptr<ArrayLiteral>;
using BorrowExpressionPtr = std::unique_ptr<BorrowExpression>;

// --- Helper structs ---
// ... (FunctionParameter, ImportSpecifier) ...
// (These should be fine as long as they don't depend on AST node classes not yet declared)
struct FunctionParameter {
    std::unique_ptr<Identifier> name;
    TypeNodePtr typeNode;
    bool isMutable; // Whether this is a var or const parameter

    FunctionParameter(std::unique_ptr<Identifier> n, TypeNodePtr tn = nullptr, bool isMut = true)
        : name(std::move(n)), typeNode(std::move(tn)), isMutable(isMut) {}
};

struct ObjectProperty {
    SourceLocation loc;
    IdentifierPtr key; // Property key
    ExprPtr value;     // Property value

    ObjectProperty(SourceLocation loc, IdentifierPtr key, ExprPtr value)
        : loc(loc), key(std::move(key)), value(std::move(value)) {}
};

struct ImportSpecifier {
    std::unique_ptr<Identifier> importedName;
    std::unique_ptr<Identifier> localName;
    ImportSpecifier(std::unique_ptr<Identifier> imported, std::unique_ptr<Identifier> local = nullptr)
        : importedName(std::move(imported)), localName(std::move(local)) {}
};

// enum class NodeType must be defined before Visitor
enum class NodeType {
    // Literals
    IDENTIFIER,
    INTEGER_LITERAL,
    FLOAT_LITERAL,
    STRING_LITERAL,
    BOOLEAN_LITERAL,
    ARRAY_LITERAL,
    OBJECT_LITERAL,
    NIL_LITERAL,

    // Expressions
    UNARY_EXPRESSION,
    BINARY_EXPRESSION,
    CALL_EXPRESSION,
    MEMBER_EXPRESSION,
    ASSIGNMENT_EXPRESSION,
    BORROW_EXPRESSION,
    POINTER_DEREF_EXPRESSION,
    ADDR_OF_EXPRESSION,
    FROM_INT_TO_LOC_EXPRESSION,
    ARRAY_ELEMENT_EXPRESSION,
    LOCATION_EXPRESSION,
    LIST_COMPREHENSION,
    IF_EXPRESSION,
    GENERIC_INSTANTIATION_EXPRESSION,
    CONSTRUCTION_EXPRESSION,
    ARRAY_INITIALIZATION_EXPRESSION,
    LOGICAL_EXPRESSION, // Added
    CONDITIONAL_EXPRESSION, // Added
    SEQUENCE_EXPRESSION, // Added
    FUNCTION_EXPRESSION, // Added
    THIS_EXPRESSION, // Added
    SUPER_EXPRESSION, // Added
    AWAIT_EXPRESSION, // Added
    RANGE_EXPRESSION, // Added for range-based for loops
    BLOCK_EXPRESSION, // Block as expression for match arms
    SELECT_EXPRESSION, // Select expression for pattern matching
    COMPARISON_PATTERN, // Comparison pattern for match/select
    STRUCT_PATTERN, // Struct destructuring pattern (e.g., Point { x, y })
    TYPEOF_EXPRESSION, // Introspection: typeof(expr) returns Type
    TYPENAME_EXPRESSION, // Introspection: typename(expr) returns String
    AS_EXPRESSION, // Safe downcasting: value as TargetType


    // Statements
    BLOCK_STATEMENT,
    EXPRESSION_STATEMENT,
    IF_STATEMENT,
    FOR_STATEMENT,
    WHILE_STATEMENT,
    RETURN_STATEMENT,
    PASS_STATEMENT,
    BREAK_STATEMENT,
    CONTINUE_STATEMENT,
    TRY_STATEMENT,
    UNSAFE_STATEMENT,
    EMPTY_STATEMENT,
    EXTERN_STATEMENT, // Added
    THROW_STATEMENT, // Added
    MATCH_STATEMENT, // Added
    MATCH_EXPRESSION, // match as a value-returning expression
    YIELD_STATEMENT, // Added
    YIELD_RETURN_STATEMENT, // Added
    ASSERT_STATEMENT, // Added
    FAIL_STATEMENT, // Error handling: fail
    TRAP_CLAUSE, // Error handling: trap clause
    ENSURE_CLAUSE, // Error handling: ensure clause
    RETHROW_STATEMENT, // Error handling: rethrow
    PANIC_STATEMENT, // Error handling: panic
    EXIT_STATEMENT,  // Process exit with code
    DEFER_STATEMENT, // Deferred execution at scope exit
    TUPLE_DESTRUCTURE_ASSIGNMENT, // Tuple/multi-var destructuring assignment

    // Declarations
    VARIABLE_DECLARATION,
    FUNCTION_DECLARATION,
    TYPE_ALIAS_DECLARATION,
    IMPORT_DECLARATION,
    STRUCT_DECLARATION,
    CLASS_DECLARATION,
    FIELD_DECLARATION,
    BIND_DECLARATION,
    ENUM_DECLARATION,
    ENUM_VARIANT,
    GENERIC_PARAMETER,
    TEMPLATE_DECLARATION,
    ASPECT_DECLARATION, // Added
    NAMESPACE_DECLARATION, // Added

    // Other
    TYPE_NODE,
    MODULE,
    // Node types for TypeName, PointerType etc. if they are distinct visitable nodes
    TYPE_NAME, // Added
    POINTER_TYPE, // Added
    ARRAY_TYPE, // Added
    VEC_TYPE, // Added
    FUTURE_TYPE, // Added for async/await support
    FUNCTION_TYPE, // Added
    OPTIONAL_TYPE, // Added
    TUPLE_TYPE // ADDED
};


// Visitor Interface
class Visitor {
public:
    virtual ~Visitor() = default;

    // Literals
    virtual void visit(Identifier* node) = 0;
    virtual void visit(IntegerLiteral* node) = 0;
    virtual void visit(FloatLiteral* node) = 0;
    virtual void visit(StringLiteral* node) = 0;
    virtual void visit(BooleanLiteral* node) = 0;
    virtual void visit(ObjectLiteral* node) = 0;
    virtual void visit(NilLiteral* node) = 0;

    // Expressions
    virtual void visit(UnaryExpression* node) = 0;
    virtual void visit(BinaryExpression* node) = 0;
    virtual void visit(CallExpression* node) = 0;
    virtual void visit(MemberExpression* node) = 0;
    virtual void visit(AssignmentExpression* node) = 0;
    virtual void visit(ArrayLiteral* node) = 0;
    virtual void visit(BorrowExpression* node) = 0;
    virtual void visit(PointerDerefExpression* node) = 0;
    virtual void visit(AddrOfExpression* node) = 0;
    virtual void visit(FromIntToLocExpression* node) = 0;
    virtual void visit(ArrayElementExpression* node) = 0;
    virtual void visit(LocationExpression* node) = 0;
    virtual void visit(ListComprehension* node) = 0;
    virtual void visit(IfExpression* node) = 0;
    virtual void visit(ConstructionExpression* node) = 0;
    virtual void visit(ArrayInitializationExpression* node) = 0;
    virtual void visit(GenericInstantiationExpression* node) = 0;
    virtual void visit(LogicalExpression* node) = 0;
    virtual void visit(ConditionalExpression* node) = 0;
    virtual void visit(SequenceExpression* node) = 0;
    virtual void visit(FunctionExpression* node) = 0;
    virtual void visit(ThisExpression* node) = 0;
    virtual void visit(SuperExpression* node) = 0;
    virtual void visit(AwaitExpression* node) = 0;
    virtual void visit(RangeExpression* node) = 0;
    virtual void visit(BlockExpression* node) = 0;
    virtual void visit(SelectExpression* node) = 0;
    virtual void visit(ComparisonPattern* node) = 0;
    virtual void visit(StructPattern* node) = 0;
    virtual void visit(TypeofExpression* node) = 0;
    virtual void visit(TypenameExpression* node) = 0;
    virtual void visit(AsExpression* node) {}

    // Statements
    virtual void visit(BlockStatement* node) = 0;
    virtual void visit(ExpressionStatement* node) = 0;
    virtual void visit(IfStatement* node) = 0;
    virtual void visit(ForStatement* node) = 0;
    virtual void visit(WhileStatement* node) = 0;
    virtual void visit(ReturnStatement* node) = 0;
    virtual void visit(PassStatement* node) = 0;
    virtual void visit(BreakStatement* node) = 0;
    virtual void visit(ContinueStatement* node) = 0;
    virtual void visit(TryStatement* node) = 0;
    virtual void visit(UnsafeStatement* node) = 0;
    virtual void visit(EmptyStatement* node) = 0;
    virtual void visit(ExternStatement* node) = 0;
    virtual void visit(ThrowStatement* node) = 0;
    virtual void visit(MatchStatement* node) = 0;
    virtual void visit(MatchExpression* node) = 0;
    virtual void visit(YieldStatement* node) = 0;
    virtual void visit(YieldReturnStatement* node) = 0;
    virtual void visit(AssertStatement* node) = 0;

    // Error Handling
    virtual void visit(FailStatement* node) = 0;
    virtual void visit(TrapClause* node) = 0;
    virtual void visit(EnsureClause* node) = 0;
    virtual void visit(RethrowStatement* node) = 0;
    virtual void visit(PanicStatement* node) = 0;
    virtual void visit(ExitStatement* node) = 0;
    virtual void visit(DeferStatement* node) = 0;

    // Tuple Destructure Assignment
    virtual void visit(TupleDestructureAssignment* node) = 0;

    // Declarations
    virtual void visit(VariableDeclaration* node) = 0;
    virtual void visit(FunctionDeclaration* node) = 0;
    virtual void visit(TypeAliasDeclaration* node) = 0;
    virtual void visit(ImportDeclaration* node) = 0;
    virtual void visit(StructDeclaration* node) = 0;
    virtual void visit(ClassDeclaration* node) = 0;
    virtual void visit(FieldDeclaration* node) = 0;
    virtual void visit(BindDeclaration* node) = 0;
    virtual void visit(EnumDeclaration* node) = 0;
    virtual void visit(EnumVariant* node) = 0;
    virtual void visit(GenericParameter* node) = 0;
    virtual void visit(TemplateDeclaration* node) = 0;
    virtual void visit(AspectDeclaration* node) = 0;
    virtual void visit(NamespaceDeclaration* node) = 0;

    // Other
    virtual void visit(TypeNode* node) = 0;
    virtual void visit(Module* node) = 0;

    // Types (if they are distinct visitable nodes)
    virtual void visit(TypeName* node) = 0;
    virtual void visit(PointerType* node) = 0;
    virtual void visit(ArrayType* node) = 0;
    virtual void visit(VecType* node) = 0;
    virtual void visit(FutureType* node) = 0;
    virtual void visit(FunctionType* node) = 0;
    virtual void visit(OptionalType* node) = 0;
    virtual void visit(TupleTypeNode* node) = 0;
};

// Base AST Node
class Node {
public:
    SourceLocation loc;
    std::string inferredTypeName;
    std::shared_ptr<TypeNode> type;  // Add type member

    Node(SourceLocation loc) : loc(loc) {}
    virtual ~Node() = default;
    virtual NodeType getType() const = 0;
    virtual std::string toString() const = 0;
    virtual void accept(Visitor& visitor) = 0;
};

// Base Expression Node
class Expression : public Node {
public:
    Expression(SourceLocation loc) : Node(loc) {}
};

// Base Statement Node
class Statement : public Node {
public:
    Statement(SourceLocation loc) : Node(loc) {}
};

// Base Declaration Node (Declarations are Statements)
class Declaration : public Statement {
public:
    Declaration(SourceLocation loc) : Statement(loc) {}
};


// --- START OF AST NODE CLASS DEFINITIONS ---
// (Ensure all classes forward-declared above are defined here or in included files)

// --- Literals ---
class Identifier : public Expression {
public:
    std::string name;

    Identifier(SourceLocation loc, std::string name);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

class IntegerLiteral : public Expression {
public:
    int64_t value;
    bool isUnsigned; // true for a `u`-suffixed literal, e.g. `255u`
    uint64_t uvalue; // full value for `u`-suffixed literals (valid when isUnsigned)

    IntegerLiteral(SourceLocation loc, int64_t value, bool isUnsigned = false, uint64_t uvalue = 0);
    virtual ~IntegerLiteral(); // Added destructor declaration
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

class FloatLiteral : public Expression {
public:
    double value;

    FloatLiteral(SourceLocation loc, double value);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

class StringLiteral : public Expression {
public:
    std::string value;

    StringLiteral(SourceLocation loc, std::string value);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

class BooleanLiteral : public Expression {
public:
    bool value;

    BooleanLiteral(SourceLocation loc, bool value);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// Represents an array literal expression: [elem1, elem2, ...]
class ArrayLiteral : public Expression { // Renamed from ArrayLiteralNode
public:
    std::vector<ExprPtr> elements;

    ArrayLiteral(SourceLocation loc, std::vector<ExprPtr> elements); // Renamed from ArrayLiteralNode
    NodeType getType() const override { return NodeType::ARRAY_LITERAL; } // Updated NodeType
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// New: ObjectLiteral
class ObjectLiteral : public Expression {
public:
    TypeNodePtr typePath; // Optional type path for typed object literals
    std::vector<ObjectProperty> properties;

    ObjectLiteral(SourceLocation loc, TypeNodePtr typePath, std::vector<ObjectProperty> properties);
    ~ObjectLiteral() override; // Ensure virtual destructor
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};


// Represents a borrow or view expression: borrow(expr), view(expr)
class BorrowExpression : public Expression { // Renamed from BorrowExprNode
public:
    ExprPtr expression;
    BorrowKind kind; // Uses the globally defined BorrowKind

    BorrowExpression(SourceLocation loc, ExprPtr expression, BorrowKind kind); // Renamed from BorrowExprNode
    NodeType getType() const override { return NodeType::BORROW_EXPRESSION; } // Updated NodeType
    void accept(Visitor& visitor) override;
    std::string toString() const override;
};

// Represents pointer dereference: at(ptr)
class PointerDerefExpression : public Expression {
public:
    ExprPtr pointer;
    PointerDerefExpression(SourceLocation loc, ExprPtr pointer);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// Represents address-of: addr(loc)
class AddrOfExpression : public Expression {
    ExprPtr location;
public:
    AddrOfExpression(SourceLocation loc, ExprPtr location);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
    ExprPtr& getLocation() { return location; }
    const ExprPtr& getLocation() const { return location; }
};

// Represents conversion from integer to loc<T>: from<Type>(addr)
class FromIntToLocExpression : public Expression {
public:
    FromIntToLocExpression(const SourceLocation& loc, ExprPtr addr_expr, TypeNodePtr target_ty)
        : Expression(loc), address_expr(std::move(addr_expr)), target_type(std::move(target_ty)) {}

    const ExprPtr& getAddressExpression() const { return address_expr; }
    const TypeNodePtr& getTargetType() const { return target_type; }

    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;

private:
    ExprPtr address_expr;
    TypeNodePtr target_type;
};

// New: ArrayElementExpression - Represents element access: array[index]
class ArrayElementExpression : public Expression {
public:
    ExprPtr array;  // The array expression
    ExprPtr index;  // The index expression

    ArrayElementExpression(SourceLocation loc, ExprPtr array, ExprPtr index);
    ~ArrayElementExpression() override; // Was: default;
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// New: LocationExpression - Represents loc(expression)
class LocationExpression : public Expression {
public:
    ExprPtr expression; // The expression whose location is being taken

    LocationExpression(SourceLocation loc, ExprPtr expression);
    ~LocationExpression() override; // Was: default;
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};


// New NilLiteral Node
class NilLiteral : public Expression {
public:
    NilLiteral(SourceLocation loc);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// New: Represents list comprehension: [expr for var in iterable if condition]
class ListComprehension : public Expression {
public:
    ExprPtr elementExpr;
    IdentifierPtr loopVariable;
    ExprPtr iterableExpr;
    ExprPtr conditionExpr;

    ListComprehension(SourceLocation loc, ExprPtr elementExpr, IdentifierPtr loopVariable, ExprPtr iterableExpr, ExprPtr conditionExpr = nullptr);
    ~ListComprehension() override;
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// --- Expressions ---
class UnaryExpression : public Expression {
public:
    token::Token op; // The operator token (full type known from token.hpp)
    ExprPtr operand;    // The expression being operated on

    // Constructor takes const reference for op, which is then copied to the member
    UnaryExpression(SourceLocation loc, const token::Token& op, ExprPtr operand);
    virtual ~UnaryExpression();
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

class BinaryExpression : public Expression {
public:
    ExprPtr left;
    token::Token op; // The operator token
    ExprPtr right;

    BinaryExpression(SourceLocation loc, ExprPtr left, const token::Token& op, ExprPtr right);
    virtual ~BinaryExpression();
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

class CallExpression : public Expression {
public:
    ExprPtr callee;
    std::vector<ExprPtr> arguments;
    // Explicit generic type arguments recorded when a call is written with
    // explicit type args (e.g. `probe<Int>(0, 0)`). Empty when the callee was
    // a plain identifier, in which case type args are inferred at the call site.
    std::vector<TypeNodePtr> explicitTypeArgs;

    CallExpression(SourceLocation loc, ExprPtr callee, std::vector<ExprPtr> arguments);
    virtual ~CallExpression();
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

class ConstructionExpression : public Expression {
public:
    TypeNodePtr constructedType; // The type being constructed (e.g., MyStruct, my_module::MyType<T>)
    std::vector<ExprPtr> arguments;

    ConstructionExpression(SourceLocation loc, TypeNodePtr constructedType, std::vector<ExprPtr> arguments);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

class ArrayInitializationExpression : public Expression {
public:
    TypeNodePtr elementType;    // The type of the elements in the array
    ExprPtr sizeExpression;     // The expression defining the size of the array
    // For [Type; Size](), arguments are implicit (default initialization)

    ArrayInitializationExpression(SourceLocation loc, TypeNodePtr elementType, ExprPtr sizeExpression);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

class GenericInstantiationExpression : public Expression {
public:
    ExprPtr baseExpression; // The expression being genericized (e.g., 'myFunc', 'MyType')
    std::vector<TypeNodePtr> genericArguments;
    SourceLocation lt_loc; // Location of '<'
    SourceLocation gt_loc; // Location of '>'

    GenericInstantiationExpression(SourceLocation loc, ExprPtr base, std::vector<TypeNodePtr> args, SourceLocation lt_loc, SourceLocation gt_loc);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

class MemberExpression : public Expression {
public:
    ExprPtr object;   // The object whose member is being accessed
    ExprPtr property; // The property being accessed (Identifier or Expression if computed)
    bool computed;    // True if property is accessed with [], false for .

    MemberExpression(SourceLocation loc, ExprPtr object, ExprPtr property, bool computed);
    virtual ~MemberExpression();
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

class AssignmentExpression : public Expression {
public:
    ExprPtr left;  // LValue (Identifier or MemberExpression)
    token::Token op; // Assignment operator (e.g., =, +=)
    ExprPtr right; // RValue

    AssignmentExpression(SourceLocation loc, ExprPtr left, const token::Token& op, ExprPtr right);
    virtual ~AssignmentExpression();
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};


// --- Statements ---
class BlockStatement : public Statement {
public:
    std::vector<StmtPtr> body;

    BlockStatement(SourceLocation loc, std::vector<StmtPtr> body);
    virtual ~BlockStatement();
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// New EmptyStatement AST node
class EmptyStatement : public Statement {
public:
    EmptyStatement(SourceLocation loc);
    ~EmptyStatement() override = default;

    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// TryStatement AST node (define after BlockStatement)
class TryStatement : public Statement {
public:
    std::unique_ptr<BlockStatement> tryBlock;
    std::optional<std::string> catchIdent;
    std::unique_ptr<BlockStatement> catchBlock;
    std::unique_ptr<BlockStatement> finallyBlock;

    TryStatement(const SourceLocation& loc, std::unique_ptr<BlockStatement> tryBlock,
                 std::optional<std::string> catchIdent,
                 std::unique_ptr<BlockStatement> catchBlock,
                 std::unique_ptr<BlockStatement> finallyBlock);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

class ExpressionStatement : public Statement {
public:
    ExprPtr expression;

    ExpressionStatement(SourceLocation loc, ExprPtr expression);
    virtual ~ExpressionStatement();
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

class IfStatement : public Statement {
public:
    ExprPtr test;
    StmtPtr consequent;
    StmtPtr alternate; // Optional, can be nullptr

    IfStatement(SourceLocation loc, ExprPtr test, StmtPtr consequent, StmtPtr alternate = nullptr);
    virtual ~IfStatement();
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

class ForStatement : public Statement {
public:
    NodePtr init;   // VariableDeclaration or ExpressionStatement or nullptr
    ExprPtr test;   // Expression or nullptr
    ExprPtr update; // Expression or nullptr
    StmtPtr body;
    std::string label; // optional loop label ("" = none) for labeled break/continue

    ForStatement(SourceLocation loc, NodePtr init, ExprPtr test, ExprPtr update, StmtPtr body);
    virtual ~ForStatement();
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

class WhileStatement : public Statement {
public:
    ExprPtr test;
    StmtPtr body;
    std::string label; // optional loop label ("" = none) for labeled break/continue

    WhileStatement(SourceLocation loc, ExprPtr test, StmtPtr body);
    virtual ~WhileStatement();
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// --- ReturnStatement ---
class ReturnStatement : public Statement {
public:
    ExprPtr argument; // Optional, can be nullptr

    ReturnStatement(SourceLocation loc, ExprPtr argument = nullptr);
    virtual ~ReturnStatement();
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// --- PassStatement ---
class PassStatement : public Statement {
public:
    ExprPtr argument; // Required - must pass a value

    PassStatement(SourceLocation loc, ExprPtr argument);
    virtual ~PassStatement();
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// --- BreakStatement ---
class BreakStatement : public Statement {
public:
    std::string label; // optional target label ("" = innermost loop)
    BreakStatement(SourceLocation loc);
    virtual ~BreakStatement();
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// --- ContinueStatement ---
class ContinueStatement : public Statement {
public:
    std::string label; // optional target label ("" = innermost loop)
    ContinueStatement(SourceLocation loc);
    virtual ~ContinueStatement();
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// Forward declare TypeNode for use in declarations (already done above with TypeNodePtr)
// class TypeNode; // No longer TypeAnnotation
// using TypeNodePtr = std::unique_ptr<TypeNode>; // No longer TypeAnnotationPtr


// --- Other ---

// Generic Parameter Node
class GenericParameter : public Node { // Renamed from GenericParamNode
public:
    std::unique_ptr<Identifier> name;
    std::vector<TypeNodePtr> bounds; // e.g. T: Bound1 + Bound2 (replaces TypeAnnotationPtr)

    GenericParameter(SourceLocation loc, std::unique_ptr<Identifier> name, std::vector<TypeNodePtr> bounds = {}); // Renamed from GenericParamNode
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
}; // End of GenericParameter


// --- Full Class Definition for TemplateDeclaration ---
// Placed after Node, Statement, Declaration, NodeType, Visitor, Identifier, GenericParameter are defined.
class TemplateDeclaration : public Declaration { // Renamed from TemplateDeclarationNode
public:
    std::unique_ptr<Identifier> name;
    std::vector<std::unique_ptr<GenericParameter>> genericParams;
    DeclPtr body;

    TemplateDeclaration(SourceLocation loc, std::unique_ptr<Identifier> name, std::vector<std::unique_ptr<GenericParameter>> genericParams, DeclPtr body); // Renamed from TemplateDeclarationNode
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};
// Define Ptr alias after the class is fully defined.
using TemplateDeclarationPtr = std::unique_ptr<TemplateDeclaration>; // Renamed from TemplateDeclarationNodePtr


// Module (Root of the AST)
class Module : public Node {
public:
    std::vector<StmtPtr> body; // Sequence of statements (including declarations)

    Module(SourceLocation loc, std::vector<StmtPtr> body);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// --- New IfExpression Definition ---
class IfExpression : public Expression {
public:
    ExprPtr condition;
    ExprPtr thenBranch;
    ExprPtr elseBranch; // Vyb requires else for if-expressions

    IfExpression(SourceLocation loc, ExprPtr condition, ExprPtr thenBranch, ExprPtr elseBranch);
    ~IfExpression() override = default; // Or implement if needed

    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// Remove inline toString() for ObjectLiteral, NilLiteral, ListComprehension, ConstructionExpression, ArrayInitializationExpression, GenericInstantiationExpression, IfExpression, UnsafeStatement

// New UnsafeStatement AST node
class UnsafeStatement : public Statement {
public:
    std::unique_ptr<BlockStatement> block;

    UnsafeStatement(SourceLocation loc, std::unique_ptr<BlockStatement> blockStmt)
        : Statement(loc), block(std::move(blockStmt)) {}

    NodeType getType() const override { return NodeType::UNSAFE_STATEMENT; }
    std::string toString() const override;
    void accept(Visitor& visitor) override { visitor.visit(this); }
};

// --- Full Class Definition for TypeNode ---
class TypeNode : public Node {
public:
    // Define TypeCategory as a nested enum
    enum class Category {
        IDENTIFIER,
        POINTER,
        ARRAY,
        VEC,
        FUTURE,
        FUNCTION,
        TUPLE,
        OPTIONAL,
        REFERENCE,
        SLICE,
        STRUCT,
        UNKNOWN
    };

    TypeNode(SourceLocation loc) : Node(loc) {}
    virtual ~TypeNode() = default;

    // For debugging: print the category of the type node
    void printCategory() const {
        // This function is just for debugging purposes and can be removed if not needed
        switch (getCategory()) {
            case Category::IDENTIFIER:     std::cout << "TypeNode Category: IDENTIFIER\n"; break;
            case Category::POINTER:        std::cout << "TypeNode Category: POINTER\n"; break;
            case Category::ARRAY:          std::cout << "TypeNode Category: ARRAY\n"; break;
            case Category::VEC:            std::cout << "TypeNode Category: VEC\n"; break;
            case Category::FUTURE:         std::cout << "TypeNode Category: FUTURE\n"; break;
            case Category::FUNCTION:       std::cout << "TypeNode Category: FUNCTION\n"; break;
            case Category::TUPLE:          std::cout << "TypeNode Category: TUPLE\n"; break;
            case Category::OPTIONAL:       std::cout << "TypeNode Category: OPTIONAL\n"; break;
            case Category::REFERENCE:      std::cout << "TypeNode Category: REFERENCE\n"; break;
            case Category::SLICE:          std::cout << "TypeNode Category: SLICE\n"; break;
            case Category::STRUCT:         std::cout << "TypeNode Category: STRUCT\n"; break;
            case Category::UNKNOWN:        std::cout << "TypeNode Category: UNKNOWN\n"; break;
        }
    }

    virtual Category getCategory() const = 0; // Pure virtual function for getting the category
    // NodeType getType() const override { return NodeType::TYPE_NODE; } // Each derived type should specify

    virtual bool isIntegerTy() const { return false; }
    virtual bool isLocationTy() const { return false; }
    virtual std::unique_ptr<TypeNode> clone() const = 0; // Add pure virtual clone method
};

// --- Full Class Definition for TypeName ---
class TypeName : public TypeNode {
public:
    std::unique_ptr<Identifier> identifier;
    std::vector<TypeNodePtr> genericArgs; // For generics like Vec<T>

    TypeName(SourceLocation loc, std::unique_ptr<Identifier> id, std::vector<TypeNodePtr> args = {});
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;

    Category getCategory() const override { return Category::IDENTIFIER; } // TypeName is an identifier type
    bool isIntegerTy() const override; // Override
    std::unique_ptr<TypeNode> clone() const override; // Override clone
};
// --- End of TypeName Definition ---

// --- Full Class Definition for PointerType ---
class PointerType : public TypeNode {
public:
    TypeNodePtr pointeeType; // The type being pointed to

    PointerType(SourceLocation loc, TypeNodePtr pointee);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;

    Category getCategory() const override { return Category::POINTER; }
    bool isLocationTy() const override { return true; } // Override
    std::unique_ptr<TypeNode> clone() const override; // Override clone
};
// --- End of PointerType Definition ---

// --- Full Class Definition for ArrayType ---
class ArrayType : public TypeNode {
public:
    TypeNodePtr elementType; // The type of the array elements
    ExprPtr sizeExpression;  // The size of the array, if known

    ArrayType(SourceLocation loc, TypeNodePtr elementType, ExprPtr sizeExpression = nullptr);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;

    Category getCategory() const override { return Category::ARRAY; }
    std::unique_ptr<TypeNode> clone() const override; // Override clone
};
// --- End of ArrayType Definition ---

// --- Full Class Definition for VecType ---
class VecType : public TypeNode {
public:
    TypeNodePtr elementType; // The type of the vector elements

    VecType(SourceLocation loc, TypeNodePtr elementType);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;

    Category getCategory() const override { return Category::VEC; }
    std::unique_ptr<TypeNode> clone() const override; // Override clone
};
// --- End of VecType Definition ---

// --- Full Class Definition for FutureType ---
class FutureType : public TypeNode {
public:
    TypeNodePtr resultType; // The type of the future result

    FutureType(SourceLocation loc, TypeNodePtr resultType);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;

    Category getCategory() const override { return Category::FUTURE; }
    std::unique_ptr<TypeNode> clone() const override; // Override clone
};
// --- End of FutureType Definition ---

// --- Full Class Definition for FunctionType ---
class FunctionType : public TypeNode {
public:
    std::vector<TypeNodePtr> parameterTypes; // The types of the function parameters
    TypeNodePtr returnType;                 // The return type of the function

    FunctionType(SourceLocation loc, std::vector<TypeNodePtr> params, TypeNodePtr returnType);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;

    Category getCategory() const override { return Category::FUNCTION; }
    std::unique_ptr<TypeNode> clone() const override; // Override clone
};
// --- End of FunctionType Definition ---

// --- Full Class Definition for OptionalType ---
class OptionalType : public TypeNode {
public:
    TypeNodePtr containedType; // The type contained within the optional

    OptionalType(SourceLocation loc, TypeNodePtr containedType);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;

    Category getCategory() const override { return Category::OPTIONAL; }
    std::unique_ptr<TypeNode> clone() const override; // Override clone
};
// --- End of OptionalType Definition ---

// --- Full Class Definition for TupleTypeNode --- // ADDED
class TupleTypeNode : public TypeNode {
public:
    std::vector<TypeNodePtr> memberTypes;

    TupleTypeNode(SourceLocation loc, std::vector<TypeNodePtr> members);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
    Category getCategory() const override { return Category::TUPLE; }
    std::unique_ptr<TypeNode> clone() const override; // Override clone
};
// --- End of TupleTypeNode Definition --- // ADDED


// --- Declarations (ensure full definitions are here) ---

// ImportDeclaration (Example, ensure others follow suit if not already complete)
enum class ImportKind { TrustedImport, Smuggle };

class ImportDeclaration : public Declaration {
public:
    ImportKind kind;                                       // TrustedImport or Smuggle
    std::unique_ptr<StringLiteral> source;                 // module path (e.g. "std::io::println")
    std::unique_ptr<StringLiteral> locator;                // optional: from "..." locator string
    std::vector<ImportSpecifier> specifiers; // For named imports: { A, B as C }
    std::unique_ptr<Identifier> defaultImport; // For default import: import X from ...
    std::unique_ptr<Identifier> namespaceImport; // For namespace import: import * as M from ...

    ImportDeclaration(SourceLocation loc,
                      ImportKind kind,
                      std::unique_ptr<StringLiteral> source,
                      std::unique_ptr<StringLiteral> locator = nullptr,
                      std::vector<ImportSpecifier> specifiers = {},
                      std::unique_ptr<Identifier> defaultImport = nullptr,
                      std::unique_ptr<Identifier> namespaceImport = nullptr);
    ~ImportDeclaration() override = default;
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// VariableDeclaration
class VariableDeclaration : public Declaration {
public:
    std::unique_ptr<Identifier> id;
    bool isConst; // true for 'let', false for 'var'
    TypeNodePtr typeNode; // Optional type annotation
    std::shared_ptr<Expression> init;  // Optional initializer (shared_ptr for multi-var support)

    VariableDeclaration(SourceLocation loc, std::unique_ptr<Identifier> id, bool isConst, TypeNodePtr typeNode = nullptr, std::shared_ptr<Expression> init = nullptr);
    ~VariableDeclaration() override = default;
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// FunctionDeclaration
class FunctionDeclaration : public Declaration {
public:
    std::unique_ptr<Identifier> id;
    std::vector<std::unique_ptr<GenericParameter>> genericParams; // Generic type parameters
    std::vector<FunctionParameter> params;
    std::unique_ptr<BlockStatement> body;
    bool isAsync;
    bool hasDefaultImpl; // true if method has arrow (-> {...}), false if mandatory (no arrow)
    bool variadic;       // true for variadic C functions (e.g. `printf(fmt: *i8, ...)`)
    TypeNodePtr returnTypeNode; // Optional return type annotation

    // Error propagation metadata (set during semantic analysis)
    bool canFail = false;  // Contains fail statements
    bool needsErrorReturn = false;  // Returns { T, error_ptr } instead of T
    std::vector<std::string> errorTypes;  // Types that can be failed (for type checking)

    FunctionDeclaration(SourceLocation loc, std::unique_ptr<Identifier> id, std::vector<FunctionParameter> params, std::unique_ptr<BlockStatement> body, bool isAsync = false, TypeNodePtr returnTypeNode = nullptr, bool hasDefaultImpl = true, std::vector<std::unique_ptr<GenericParameter>> genericParams = std::vector<std::unique_ptr<GenericParameter>>(), bool variadic = false);
    ~FunctionDeclaration() override = default;
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// TypeAliasDeclaration
class TypeAliasDeclaration : public Declaration {
public:
    std::unique_ptr<Identifier> name;
    TypeNodePtr typeNode; // The type being aliased

    TypeAliasDeclaration(SourceLocation loc, std::unique_ptr<Identifier> name, TypeNodePtr typeNode);
    ~TypeAliasDeclaration() override = default;
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// FieldDeclaration (typically part of Struct/Class)
class FieldDeclaration : public Declaration { // Or Node if not a standalone statement
public:
    std::unique_ptr<Identifier> name;
    TypeNodePtr typeNode;
    ExprPtr initializer; // Optional default value
    bool isMutable; // Or some other way to denote mutability/visibility

    FieldDeclaration(SourceLocation loc, std::unique_ptr<Identifier> name, TypeNodePtr typeNode, ExprPtr initializer = nullptr, bool isMutable = false);
    ~FieldDeclaration() override = default;
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// StructDeclaration
class StructDeclaration : public Declaration {
public:
    std::unique_ptr<Identifier> name;
    std::vector<std::unique_ptr<GenericParameter>> genericParams;
    std::vector<std::unique_ptr<FieldDeclaration>> fields;
    // Declared constructors (`constructor(cap<Int>) -> { ... }` inside the struct
    // body). Each is stored as a FunctionDeclaration that lower to a synthetic
    // generic function (`__ctor_<Struct>_<N>`) sharing the struct's generic
    // parameters, so `HashMap<K,V>(n)` dispatches like any other generic call.
    std::vector<std::unique_ptr<FunctionDeclaration>> constructors;
    bool reprC = false;

    StructDeclaration(SourceLocation loc, std::unique_ptr<Identifier> name, std::vector<std::unique_ptr<GenericParameter>> genericParams, std::vector<std::unique_ptr<FieldDeclaration>> fields, bool reprC = false);
    ~StructDeclaration() override = default;
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// ClassDeclaration
class ClassDeclaration : public Declaration {
public:
    std::unique_ptr<Identifier> name;
    std::vector<std::unique_ptr<GenericParameter>> genericParams;
    // Members can be FieldDeclarations or FunctionDeclarations (methods)
    std::vector<DeclPtr> members; // Using DeclPtr to hold various member types

    ClassDeclaration(SourceLocation loc, std::unique_ptr<Identifier> name, std::vector<std::unique_ptr<GenericParameter>> genericParams, std::vector<DeclPtr> members);
    ~ClassDeclaration() override = default;
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// BindDeclaration
class BindDeclaration : public Declaration {
public:
    struct AssociatedTypeBinding {
        std::unique_ptr<Identifier> name;
        TypeNodePtr valueType;
    };

    std::unique_ptr<Identifier> name; // Optional name for the bind block (less common)
    std::vector<std::unique_ptr<GenericParameter>> genericParams;
    TypeNodePtr traitType; // Optional: if implementing an aspect (e.g., bind MyAspect -> MyType)
    TypeNodePtr selfType;  // The type for which methods are being implemented (e.g., MyType)
    std::vector<AssociatedTypeBinding> associatedTypeBindings;
    std::vector<std::unique_ptr<FunctionDeclaration>> methods;

    BindDeclaration(SourceLocation loc, TypeNodePtr selfType, std::vector<AssociatedTypeBinding> associatedTypeBindings = {}, std::vector<std::unique_ptr<FunctionDeclaration>> methods = {}, std::unique_ptr<Identifier> name = nullptr, std::vector<std::unique_ptr<GenericParameter>> genericParams = {}, TypeNodePtr traitType = nullptr);
    ~BindDeclaration() override = default;
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// EnumVariant (typically part of EnumDeclaration)
class EnumVariant : public Node { // Not a Declaration itself, but a component
public:
    std::unique_ptr<Identifier> name;
    std::vector<TypeNodePtr> associatedTypes; // e.g., Option::Some(T) -> T is an associated type
    int64_t value = 0;   // explicit constant value (`A = 4`) for constant enums
    bool hasValue = false; // true when the variant was declared with `= <int>`

    EnumVariant(SourceLocation loc, std::unique_ptr<Identifier> name, std::vector<TypeNodePtr> associatedTypes = {});
    ~EnumVariant() override = default;
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// EnumDeclaration
class EnumDeclaration : public Declaration {
public:
    std::unique_ptr<Identifier> name;
    std::vector<std::unique_ptr<GenericParameter>> genericParams;
    std::vector<std::unique_ptr<EnumVariant>> variants;

    EnumDeclaration(SourceLocation loc, std::unique_ptr<Identifier> name, std::vector<std::unique_ptr<GenericParameter>> genericParams, std::vector<std::unique_ptr<EnumVariant>> variants);
    ~EnumDeclaration() override = default;
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// Add StructType class
class StructType : public TypeNode {
public:
    struct Field {
        std::string name;
        std::shared_ptr<TypeNode> type;
    };
    std::vector<Field> fields;

    StructType(const SourceLocation& loc_) : TypeNode(loc_) {}

    void accept(Visitor& visitor) override {
        visitor.visit(this);
    }

    std::string toString() const override {
        std::string result = "struct { ";
        for (size_t i = 0; i < fields.size(); ++i) {
            if (i > 0) result += ", ";
            result += fields[i].name + ": " + fields[i].type->toString();
        }
        result += " }";
        return result;
    }

    Category getCategory() const override {
        return Category::STRUCT;
    }
};

// --- LogicalExpression ---
class LogicalExpression : public Expression {
public:
    ExprPtr left;
    token::Token op;
    ExprPtr right;
    LogicalExpression(SourceLocation loc, ExprPtr left, const token::Token& op, ExprPtr right);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// --- ConditionalExpression ---
class ConditionalExpression : public Expression {
public:
    ExprPtr condition;
    ExprPtr thenExpr;
    ExprPtr elseExpr;
    ConditionalExpression(SourceLocation loc, ExprPtr condition, ExprPtr thenExpr, ExprPtr elseExpr);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// --- SequenceExpression ---
class SequenceExpression : public Expression {
public:
    std::vector<ExprPtr> expressions;
    SequenceExpression(SourceLocation loc, std::vector<ExprPtr> expressions);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// --- FunctionExpression ---
class FunctionExpression : public Expression {
public:
    std::vector<FunctionParameter> params;
    ExprPtr body;
    bool isAsync;
    // Whether the lambda body can propagate a failure (`fail` statement). Set
    // during semantic analysis so codegen can pick the failable return ABI
    // (e.g. for agent behaviors that `fail`).
    bool canFail = false;
    // Names of variables the lambda captures from its enclosing scope (filled in
    // during semantic analysis). Codegen copies each captured value by reference
    // into the closure's environment at creation time.
    std::vector<std::string> capturedVariables;
    // Captured vars that the lambda body writes to (mutable context). Codegen
    // stores the outer variable's address in the env so writes propagate back.
    std::vector<std::string> mutableCapturedVariables;
    // Captured vars that are `our<T>` (shared). Codegen bumps their strong count
    // at capture so the shared value stays alive for the life of the closure.
    std::vector<std::string> ourCapturedVariables;
    FunctionExpression(SourceLocation loc, std::vector<FunctionParameter> params, ExprPtr body, bool isAsync = false);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// --- ThisExpression ---
class ThisExpression : public Expression {
public:
    ThisExpression(SourceLocation loc);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// --- SuperExpression ---
class SuperExpression : public Expression {
public:
    SuperExpression(SourceLocation loc);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// --- AwaitExpression ---
class AwaitExpression : public Expression {
public:
    ExprPtr expr;
    AwaitExpression(SourceLocation loc, ExprPtr expr);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// --- RangeExpression ---
class RangeExpression : public Expression {
public:
    ExprPtr start;
    ExprPtr end;
    ExprPtr step; // Optional step value (nullptr means step=1)

    RangeExpression(SourceLocation loc, ExprPtr start, ExprPtr end, ExprPtr step = nullptr);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// --- BlockExpression ---
class BlockExpression : public Expression {
public:
    std::unique_ptr<BlockStatement> block;
    std::vector<std::unique_ptr<TrapClause>> trapClauses;  // trap clauses attached to block
    std::unique_ptr<EnsureClause> ensureClause;            // optional ensure clause

    BlockExpression(SourceLocation loc, std::unique_ptr<BlockStatement> block,
                    std::vector<std::unique_ptr<TrapClause>> trapClauses = {},
                    std::unique_ptr<EnsureClause> ensureClause = nullptr);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// --- SelectExpression ---
class SelectExpression : public Expression {
public:
    ExprPtr expr;
    std::vector<std::pair<ExprPtr, ExprPtr>> cases;

    SelectExpression(SourceLocation loc, ExprPtr expr, std::vector<std::pair<ExprPtr, ExprPtr>> cases);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// --- ComparisonPattern ---
// Represents comparison patterns like >= 18, < 0, == 5 in match/select
class ComparisonPattern : public Expression {
public:
    token::Token op;  // Comparison operator (LT, LE, GT, GE, EQEQ, BANGEQ)
    ExprPtr value;    // Value to compare against

    ComparisonPattern(SourceLocation loc, token::Token op, ExprPtr value);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// --- StructPattern ---
// Represents struct destructuring `Type { a, b }` in match/select arms.
class StructPattern : public Expression {
public:
    ast::TypeNodePtr typeName;                     // Struct type being destructured
    std::vector<std::unique_ptr<Identifier>> bindings; // Bound variable names (== field names)

    StructPattern(SourceLocation loc, ast::TypeNodePtr typeName, std::vector<std::unique_ptr<Identifier>> bindings);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// --- TypeofExpression ---
// Returns runtime type identity as opaque Type value
// typeof(value) -> Type (8-byte type ID hash)
class TypeofExpression : public Expression {
public:
    ExprPtr operand;       // Expression to get type of (nullptr for typeof<T>())
    TypeNodePtr typeArg;   // Compile-time type argument for typeof<T>()
    bool operandFromWildcardError; // Set by semantic: operand is a wildcard trap `e<?>`

    // typeof(expr)
    TypeofExpression(SourceLocation loc, ExprPtr operand);
    // typeof<T>()
    TypeofExpression(SourceLocation loc, TypeNodePtr typeArg);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// --- TypenameExpression ---
// Returns human-readable type name as String
// typename(value) -> String (e.g., "Int", "ParseError")
class TypenameExpression : public Expression {
public:
    ExprPtr operand;  // Expression to get type name of
    bool operandFromWildcardError; // Set by semantic: operand is a wildcard trap `e<?>`
    bool operandFromTypeValue;     // Set by semantic: operand's static type is `Type`

    TypenameExpression(SourceLocation loc, ExprPtr operand);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// --- AsExpression ---
// Safe downcasting: value as TargetType. Types the expression as TargetType and,
// for a wildcard trap error operand, extracts the concrete payload from the error
// struct so the handler can access its fields.
class AsExpression : public Expression {
public:
    ExprPtr operand;       // The value being downcast
    TypeNodePtr targetType; // The type to downcast to
    bool operandIsWildcardError; // Set by semantic when operand is a wildcard `e<?>`

    AsExpression(SourceLocation loc, ExprPtr operand, TypeNodePtr targetType)
        : Expression(loc), operand(std::move(operand)), targetType(std::move(targetType)),
          operandIsWildcardError(false) {}
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// --- ThrowStatement ---
class ThrowStatement : public Statement {
public:
    ExprPtr expr;
    ThrowStatement(SourceLocation loc, ExprPtr expr);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// --- MatchStatement ---
class MatchStatement : public Statement {
public:
    ExprPtr expr;
    std::vector<std::pair<ExprPtr, ExprPtr>> cases;
    // Optional guard clause per case (index-aligned with cases; nullptr = no guard).
    // A guard `pattern if condition` only matches when the pattern matches AND
    // the condition evaluates to true.
    std::vector<ExprPtr> guards;
    MatchStatement(SourceLocation loc, ExprPtr expr, std::vector<std::pair<ExprPtr, ExprPtr>> cases,
                   std::vector<ExprPtr> guards = {});
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};


// --- MatchExpression ---
// `match` used as a value-returning expression. Wraps a MatchStatement whose
// arms yield values; the matched arm's value becomes the expression's result.
class MatchExpression : public Expression {
public:
    std::unique_ptr<MatchStatement> match; // The underlying match statement
    TypeNodePtr resultType;                // Inferred result type (set by semantic analysis)

    MatchExpression(SourceLocation loc, std::unique_ptr<MatchStatement> match);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// --- AspectDeclaration ---
class AspectDeclaration : public Declaration {
public:
    std::unique_ptr<Identifier> name;
    std::vector<std::unique_ptr<GenericParameter>> genericParams;
    std::vector<std::unique_ptr<Identifier>> superTypes; // Super-aspects (inheritance)
    std::vector<std::unique_ptr<Identifier>> associatedTypes;
    // Optional default types, index-aligned with associatedTypes (nullptr = no default).
    std::vector<TypeNodePtr> associatedTypeDefaults;
    // Optional aspect bounds for each associated type, index-aligned with
    // associatedTypes (empty vector = no bound declared), e.g. type Item<Display>.
    std::vector<std::vector<TypeNodePtr>> associatedTypeConstraints;
    std::vector<std::unique_ptr<FunctionDeclaration>> methods;

    AspectDeclaration(SourceLocation loc, std::unique_ptr<Identifier> name, std::vector<std::unique_ptr<GenericParameter>> genericParams, std::vector<std::unique_ptr<Identifier>> superTypes, std::vector<std::unique_ptr<Identifier>> associatedTypes, std::vector<TypeNodePtr> associatedTypeDefaults, std::vector<std::vector<TypeNodePtr>> associatedTypeConstraints, std::vector<std::unique_ptr<FunctionDeclaration>> methods);
    ~AspectDeclaration() override = default;
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// --- NamespaceDeclaration ---
class NamespaceDeclaration : public Declaration {
public:
    std::unique_ptr<Identifier> name;
    std::vector<DeclPtr> members;

    NamespaceDeclaration(SourceLocation loc, std::unique_ptr<Identifier> name, std::vector<DeclPtr> members);
    ~NamespaceDeclaration() override = default;
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// --- AssertStatement ---
class AssertStatement : public Statement {
public:
    ExprPtr condition;
    ExprPtr message; // Optional message

    AssertStatement(SourceLocation loc, ExprPtr condition, ExprPtr message = nullptr);
    ~AssertStatement() override = default;
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// ExternStatement definition
class ExternStatement : public Statement {
public:
    std::unique_ptr<Identifier> name;          // The name of the external entity
    TypeNodePtr returnType;                    // Optional return type
    std::vector<FunctionParameter> parameters; // Parameters if this is a function

    ExternStatement(
        SourceLocation loc,
        std::unique_ptr<Identifier> name,
        TypeNodePtr returnType = nullptr,
        std::vector<FunctionParameter> parameters = {}
    );
    ~ExternStatement() override = default;
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// YieldStatement definition
class YieldStatement : public Statement {
public:
    ExprPtr expression; // Optional expression to yield

    YieldStatement(SourceLocation loc, ExprPtr expression = nullptr);
    ~YieldStatement() override = default;
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// YieldReturnStatement definition
class YieldReturnStatement : public Statement {
public:
    ExprPtr expression; // Optional expression to return

    YieldReturnStatement(SourceLocation loc, ExprPtr expression = nullptr);
    ~YieldReturnStatement() override = default;
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// --- Error Handling Statements ---

// FailStatement - Trigger a failure with an error value
class FailStatement : public Statement {
public:
    ExprPtr error; // The error value to fail with (must implement Errable aspect)
    TypeNodePtr errorType; // Optional explicit fail<T>(value) type

    FailStatement(SourceLocation loc, ExprPtr error, TypeNodePtr errorType = nullptr);
    ~FailStatement() override = default;
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// TrapClause - Error handler clause (attached to blocks)
class TrapClause : public Node {
public:
    std::unique_ptr<Identifier> errorName; // Error parameter name (e.g., 'e')
    TypeNodePtr errorType;                  // Error type (e.g., NetworkError), nullptr if wildcard (backward compat)
    std::vector<TypeNodePtr> errorTypes;    // Multiple error types for union (Type1 | Type2 | Type3)
    StmtPtr handler;                        // Handler block
    bool isWildcard;                        // True if trap (e<?>) - catch any error
    bool isMultiType;                       // True if trap (e<Type1 | Type2>) - union of types

    TrapClause(SourceLocation loc, std::unique_ptr<Identifier> errorName,
               TypeNodePtr errorType, StmtPtr handler, bool isWildcard = false, bool isMultiType = false);
    ~TrapClause() override = default;
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// EnsureClause - Cleanup clause that always runs
class EnsureClause : public Node {
public:
    StmtPtr cleanupBlock; // Cleanup code that always executes

    EnsureClause(SourceLocation loc, StmtPtr cleanupBlock);
    ~EnsureClause() override = default;
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// RethrowStatement - Propagate current error to caller
class RethrowStatement : public Statement {
public:
    ExprPtr transformedError; // Optional: transform error before rethrowing

    RethrowStatement(SourceLocation loc, ExprPtr transformedError = nullptr);
    ~RethrowStatement() override = default;
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// PanicStatement - Unrecoverable error, immediate crash
class PanicStatement : public Statement {
public:
    ExprPtr message; // Panic message (typically a string literal)

    PanicStatement(SourceLocation loc, ExprPtr message);
    ~PanicStatement() override = default;
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// ExitStatement - Terminate process with exit code: exit(n)
class ExitStatement : public Statement {
public:
    ExprPtr code; // Exit code expression (must be Int)

    ExitStatement(SourceLocation loc, ExprPtr code);
    ~ExitStatement() override = default;
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// DeferStatement - Deferred execution at function exit (LIFO order)
class DeferStatement : public Statement {
public:
    StmtPtr statement; // The statement to defer

    DeferStatement(SourceLocation loc, StmtPtr statement);
    ~DeferStatement() override = default;
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// --- Tuple Destructure Assignment ---
class TupleDestructureAssignment : public Statement {
public:
    std::vector<std::unique_ptr<Identifier>> identifiers; // Variables to assign to
    ExprPtr expression; // Right-hand side expression (must produce a tuple/struct)

    TupleDestructureAssignment(SourceLocation loc,
                                std::vector<std::unique_ptr<Identifier>> ids,
                                ExprPtr expr);
    ~TupleDestructureAssignment() override = default;
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

} // namespace ast
} // namespace vyb

#endif // VYB_PARSER_AST_HPP
