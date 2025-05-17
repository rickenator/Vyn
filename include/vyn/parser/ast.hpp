#ifndef VYN_PARSER_AST_HPP
#define VYN_PARSER_AST_HPP

#include <string>
#include <vector>
#include <memory>
#include <variant>
#include <optional> // Ensure this is present
#include "token.hpp" // For Token
#include "source_location.hpp" // For SourceLocation

namespace vyn {
namespace ast {

// Forward declarations
class Node;
class Module;
class Identifier;
class Expression;
class Statement;
class Declaration;
class Visitor;
class NilLiteral;
class StructDeclaration;
class ClassDeclaration;
class FieldDeclaration;
class ImplDeclaration;
class EnumDeclaration;
class EnumVariantNode;
class GenericParamNode;
class TypeNode;
class ArrayLiteralNode; // Corrected: Was ArrayLiteral in some places, ensure consistency
class BorrowExprNode;
class TryStatement;
class IntegerLiteral;
class FloatLiteral;
class StringLiteral;
class BooleanLiteral;
class ObjectLiteral;
class UnaryExpression;
class BinaryExpression;
class CallExpression;
class MemberExpression;
class AssignmentExpression;
class BlockStatement;
class ExpressionStatement;
class IfStatement;
class ForStatement;
class WhileStatement;
class ReturnStatement;
class BreakStatement;
class ContinueStatement;
class VariableDeclaration;
class FunctionDeclaration;
class TypeAliasDeclaration;
class ImportDeclaration;
class TemplateDeclarationNode;
class PointerDerefExpression;
class AddrOfExpression;
class FromIntToLocExpression;
class ArrayElementExpression; // Added forward declaration
class LocationExpression; // Added forward declaration for LocationExpression
class ListComprehension; // Added forward declaration for ListComprehension

// --- Type aliases for smart pointers (must appear after all forward declarations) ---
using NodePtr = std::unique_ptr<Node>;
using ExprPtr = std::unique_ptr<Expression>;
using StmtPtr = std::unique_ptr<Statement>;
using DeclPtr = std::unique_ptr<Declaration>;
using TypeNodePtr = std::unique_ptr<TypeNode>;
using IdentifierPtr = std::unique_ptr<Identifier>;
using ArrayLiteralNodePtr = std::unique_ptr<ArrayLiteralNode>;
using BorrowExprNodePtr = std::unique_ptr<BorrowExprNode>;

// --- Helper structs (must appear after all forward declarations) ---
struct FunctionParameter {
    std::unique_ptr<Identifier> name;
    TypeNodePtr typeNode; // Optional
    // Add other relevant fields like isConst, isSpread, etc. if needed

    FunctionParameter(std::unique_ptr<Identifier> n, TypeNodePtr tn = nullptr)
        : name(std::move(n)), typeNode(std::move(tn)) {}
};

struct ImportSpecifier {
    std::unique_ptr<Identifier> importedName;
    std::unique_ptr<Identifier> localName; // Optional, for 'as'

    ImportSpecifier(std::unique_ptr<Identifier> imported, std::unique_ptr<Identifier> local = nullptr)
        : importedName(std::move(imported)), localName(std::move(local)) {}
};

// New Enum for Type Categories
enum class TypeCategory {
    IDENTIFIER,          // e.g., Int, String, MyStruct<T>
    OWNERSHIP_WRAPPED,   // e.g., my<T>, our<T>, their<T>, ptr<T>
    ARRAY,               // e.g., T[]
    TUPLE,               // e.g., (T1, T2)
    FUNCTION_SIGNATURE,  // e.g., fn(T1, T2) -> R
    OPTIONAL             // e.g., T?
};

// New Enum for Ownership Kinds (as per RFC)
enum class OwnershipKind {
    MY,
    OUR,
    THEIR,
    PTR
};

// New Enum for Borrow Kinds
enum class BorrowKind {
    MUTABLE_BORROW, // for 'borrow' keyword
    IMMUTABLE_VIEW  // for 'view' keyword
};

// enum class NodeType must be defined before Visitor
enum class NodeType {
    // Literals
    IDENTIFIER,
    INTEGER_LITERAL,
    FLOAT_LITERAL,
    STRING_LITERAL,
    BOOLEAN_LITERAL,
    ARRAY_LITERAL_NODE, // Renamed from ARRAY_LITERAL to avoid conflict if ArrayLiteral class existed
    OBJECT_LITERAL_NODE, // Renamed to match other node naming
    NIL_LITERAL, // New

    // Expressions
    UNARY_EXPRESSION,
    BINARY_EXPRESSION,
    CALL_EXPRESSION,
    MEMBER_EXPRESSION,
    ASSIGNMENT_EXPRESSION,
    BORROW_EXPRESSION_NODE, // Renamed from BORROW_EXPRESSION
    POINTER_DEREF_EXPRESSION, // Added
    ADDR_OF_EXPRESSION,       // Added
    FROM_INT_TO_LOC_EXPRESSION, // Added
    ARRAY_ELEMENT_EXPRESSION, // Added
    LOCATION_EXPRESSION, // Added NodeType for LocationExpression
    LIST_COMPREHENSION, // Added NodeType for ListComprehension

    // Statements
    BLOCK_STATEMENT,
    EXPRESSION_STATEMENT,
    IF_STATEMENT,
    FOR_STATEMENT,
    WHILE_STATEMENT,
    RETURN_STATEMENT,
    BREAK_STATEMENT,
    CONTINUE_STATEMENT,
    TRY_STATEMENT,


    // Declarations
    VARIABLE_DECLARATION,
    FUNCTION_DECLARATION,
    TYPE_ALIAS_DECLARATION,
    IMPORT_DECLARATION,
    STRUCT_DECLARATION,
    CLASS_DECLARATION,
    FIELD_DECLARATION,
    IMPL_DECLARATION,
    ENUM_DECLARATION,
    ENUM_VARIANT,
    GENERIC_PARAMETER,
    TEMPLATE_DECLARATION, // This should match TemplateDeclarationNode if that's the class name


    // Other
    TYPE_NODE, 
    MODULE,
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
    virtual void visit(NilLiteral* node) = 0; // New

    // Expressions
    virtual void visit(UnaryExpression* node) = 0;
    virtual void visit(BinaryExpression* node) = 0;
    virtual void visit(CallExpression* node) = 0;
    virtual void visit(MemberExpression* node) = 0;
    virtual void visit(AssignmentExpression* node) = 0;
    virtual void visit(ArrayLiteralNode* node) = 0; 
    virtual void visit(BorrowExprNode* node) = 0; 
    virtual void visit(PointerDerefExpression* node) = 0;
    virtual void visit(AddrOfExpression* node) = 0;
    virtual void visit(FromIntToLocExpression* node) = 0;
    virtual void visit(ArrayElementExpression* node) = 0; // Added
    virtual void visit(LocationExpression* node) = 0; // Added visit method for LocationExpression
    virtual void visit(ListComprehension* node) = 0; // Added visit method for ListComprehension

    // Statements
    virtual void visit(BlockStatement* node) = 0;
    virtual void visit(ExpressionStatement* node) = 0;
    virtual void visit(IfStatement* node) = 0;
    virtual void visit(ForStatement* node) = 0;
    virtual void visit(WhileStatement* node) = 0;
    virtual void visit(ReturnStatement* node) = 0;
    virtual void visit(BreakStatement* node) = 0;
    virtual void visit(ContinueStatement* node) = 0;
    virtual void visit(TryStatement* node) = 0;

    // Declarations
    virtual void visit(VariableDeclaration* node) = 0;
    virtual void visit(FunctionDeclaration* node) = 0;
    virtual void visit(TypeAliasDeclaration* node) = 0;
    virtual void visit(ImportDeclaration* node) = 0;
    virtual void visit(StructDeclaration* node) = 0;
    virtual void visit(ClassDeclaration* node) = 0;
    virtual void visit(FieldDeclaration* node) = 0;
    virtual void visit(ImplDeclaration* node) = 0;
    virtual void visit(EnumDeclaration* node) = 0;
    virtual void visit(EnumVariantNode* node) = 0;
    virtual void visit(GenericParamNode* node) = 0;
    virtual void visit(TemplateDeclarationNode* node) = 0; 
    
    // Other
    virtual void visit(TypeNode* node) = 0; 
    virtual void visit(Module* node) = 0;
};

// Base AST Node
class Node {
public:
    SourceLocation loc; 
    std::string inferredTypeName; // Added for type checking/codegen hints

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
    

// --- Declarations ---
// (Ensure FunctionParameter and ImportSpecifier are defined above this section)

class VariableDeclaration : public Declaration {
public:
    std::unique_ptr<Identifier> id;
    bool isConst; // Binding const-ness (const x: T vs var x: T)
    TypeNodePtr typeNode; // Optional, type of the variable (replaces typeAnnotation)
    ExprPtr init;         // Optional

    VariableDeclaration(SourceLocation loc, std::unique_ptr<Identifier> id, bool isConst, TypeNodePtr typeNode = nullptr, ExprPtr init = nullptr);
    virtual ~VariableDeclaration();
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

class TypeAliasDeclaration : public Declaration {
public:
    std::unique_ptr<Identifier> name;
    TypeNodePtr typeNode; // The actual type being aliased

    TypeAliasDeclaration(SourceLocation loc, std::unique_ptr<Identifier> name, TypeNodePtr typeNode);
    virtual ~TypeAliasDeclaration();
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

class FunctionDeclaration : public Declaration {
public:
    std::unique_ptr<Identifier> id;
    std::vector<FunctionParameter> params; // Uses FunctionParameter
    std::unique_ptr<class BlockStatement> body; // Forward declare BlockStatement if full def is later
    bool isAsync;
    TypeNodePtr returnTypeNode; // Optional

    FunctionDeclaration(SourceLocation loc, std::unique_ptr<Identifier> id, std::vector<FunctionParameter> params, std::unique_ptr<BlockStatement> body, bool isAsync = false, TypeNodePtr returnTypeNode = nullptr);
    virtual ~FunctionDeclaration();
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

class ImportDeclaration : public Declaration {
public:
    std::unique_ptr<StringLiteral> source;
    std::vector<ImportSpecifier> specifiers; // Uses ImportSpecifier
    std::unique_ptr<Identifier> defaultImport; // Optional
    std::unique_ptr<Identifier> namespaceImport; // Optional, for import * as ns

    ImportDeclaration(SourceLocation loc, std::unique_ptr<StringLiteral> source, std::vector<ImportSpecifier> specifiers = {}, std::unique_ptr<Identifier> defaultImport = nullptr, std::unique_ptr<Identifier> namespaceImport = nullptr);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

class StructDeclaration : public Declaration {
public:
    std::unique_ptr<Identifier> name;
    std::vector<std::unique_ptr<GenericParamNode>> genericParams;
    std::vector<std::unique_ptr<FieldDeclaration>> fields;

    StructDeclaration(SourceLocation loc, std::unique_ptr<Identifier> name, std::vector<std::unique_ptr<GenericParamNode>> genericParams, std::vector<std::unique_ptr<FieldDeclaration>> fields);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

class ClassDeclaration : public Declaration {
public:
    std::unique_ptr<Identifier> name;
    std::vector<std::unique_ptr<GenericParamNode>> genericParams;
    std::vector<DeclPtr> members; // Can be FieldDeclaration or FunctionDeclaration

    ClassDeclaration(SourceLocation loc, std::unique_ptr<Identifier> name, std::vector<std::unique_ptr<GenericParamNode>> genericParams, std::vector<DeclPtr> members);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

class FieldDeclaration : public Declaration { // Or Node, if fields are not standalone statements
public:
    std::unique_ptr<Identifier> name;
    TypeNodePtr typeNode;
    ExprPtr initializer; // Optional
    bool isMutable;

    FieldDeclaration(SourceLocation loc, std::unique_ptr<Identifier> name, TypeNodePtr typeNode, ExprPtr initializer = nullptr, bool isMutable = true);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

class ImplDeclaration : public Declaration {
public:
    std::unique_ptr<Identifier> name; // Name of the impl block (optional)
    std::vector<std::unique_ptr<GenericParamNode>> genericParams;
    TypeNodePtr selfType; // The type being implemented (e.g., MyStruct<T>)
    TypeNodePtr traitType; // Optional: the trait being implemented (e.g., MyTrait<U>)
    std::vector<std::unique_ptr<FunctionDeclaration>> methods;

    ImplDeclaration(SourceLocation loc, TypeNodePtr selfType, std::vector<std::unique_ptr<FunctionDeclaration>> methods, std::unique_ptr<Identifier> name = nullptr, std::vector<std::unique_ptr<GenericParamNode>> genericParams = {}, TypeNodePtr traitType = nullptr);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

class EnumVariantNode : public Node { // Enum Variants are not declarations themselves but parts of one
public:
    std::unique_ptr<Identifier> name;
    std::vector<TypeNodePtr> associatedTypes; // e.g. Some(String, Int)

    EnumVariantNode(SourceLocation loc, std::unique_ptr<Identifier> name, std::vector<TypeNodePtr> associatedTypes = {});
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

class EnumDeclaration : public Declaration {
public:
    std::unique_ptr<Identifier> name;
    std::vector<std::unique_ptr<GenericParamNode>> genericParams;
    std::vector<std::unique_ptr<EnumVariantNode>> variants;

    EnumDeclaration(SourceLocation loc, std::unique_ptr<Identifier> name, std::vector<std::unique_ptr<GenericParamNode>> genericParams, std::vector<std::unique_ptr<EnumVariantNode>> variants);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

// --- New TypeNode Definition ---
class TypeNode : public Node {
public:
    // Type categories
    enum class Category { // Renamed from TypeCategory
        IDENTIFIER,
        TUPLE,
        ARRAY,
        FUNCTION_SIGNATURE,
        OWNERSHIP_WRAPPED // For my<T>, their<T>, etc.
    };

    Category category; // Use the renamed enum
    SourceLocation loc;

    // For IDENTIFIER
    IdentifierPtr name; // Changed from std::unique_ptr<Identifier> to IdentifierPtr
    std::vector<TypeNodePtr> genericArguments;

    // For TUPLE
    std::vector<TypeNodePtr> tupleElementTypes; // Added for tuple types

    // For OWNERSHIP_WRAPPED
    OwnershipKind ownership; // my, our, their, ptr
    TypeNodePtr wrappedType; // The T in my<T>

    // For ARRAY
    TypeNodePtr arrayElementType;
    ExprPtr arraySizeExpression; // Added for [Type; Size]

    // For FUNCTION_SIGNATURE
    std::vector<TypeNodePtr> functionParameters;
    TypeNodePtr functionReturnType; // Added for function return type

    // Common properties
    bool dataIsConst = false; // True if the data pointed to/held is const (e.g. Foo const, or [Int const])
    bool isOptional = false;  // True if the type is optional (e.g. Foo?)
    bool isPointer = false; // True if the type is a raw pointer (e.g. Foo*)

public: // Changed from private to public
    // Private constructor to enforce use of static factory methods.
    // Implementations will be in ast.cpp
    TypeNode(SourceLocation loc, Category category, bool dataIsConst, bool isOptional); // Update constructor

public:
    ~TypeNode() override = default;

    // Static factory methods (declarations)
    static TypeNodePtr newIdentifier(SourceLocation loc, IdentifierPtr name, std::vector<TypeNodePtr> genericArgs = {}, bool dataIsConst = false, bool isOptional = false);
    static TypeNodePtr newTuple(SourceLocation loc, std::vector<TypeNodePtr> memberTypes, bool dataIsConst = false, bool isOptional = false);
    static TypeNodePtr newArray(SourceLocation loc, TypeNodePtr elementType, ExprPtr sizeExpression, bool dataIsConst = false, bool isOptional = false);
    static TypeNodePtr newFunctionSignature(SourceLocation loc, std::vector<TypeNodePtr> params, TypeNodePtr returnType, bool dataIsConst = false, bool isOptional = false);
    static TypeNodePtr newOwnershipWrapped(SourceLocation loc, OwnershipKind ownership, TypeNodePtr wrappedType, bool dataIsConst = false, bool isOptional = false);

    std::string toString() const override;
    NodeType getType() const override { return NodeType::TYPE_NODE; } // Corrected to TYPE_NODE
    void accept(Visitor& visitor) override;
};


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

    IntegerLiteral(SourceLocation loc, int64_t value);
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
class ArrayLiteralNode : public Expression { 
public:
    std::vector<ExprPtr> elements;

    ArrayLiteralNode(SourceLocation loc, std::vector<ExprPtr> elements);
    NodeType getType() const override { return NodeType::ARRAY_LITERAL_NODE; } // Updated NodeType
    void accept(Visitor& visitor) override; 
    std::string toString() const override; 
};

// Represents a borrow or view expression: borrow expr, view expr
class BorrowExprNode : public Expression { 
public:
    ExprPtr expression; 
    BorrowKind kind;

    BorrowExprNode(SourceLocation loc, ExprPtr expression, BorrowKind kind);
    NodeType getType() const override { return NodeType::BORROW_EXPRESSION_NODE; } // Updated NodeType
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

// Represents conversion from integer to loc<T>: from(addr)
class FromIntToLocExpression : public Expression {
    ExprPtr address;
public:
    FromIntToLocExpression(SourceLocation loc, ExprPtr address);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
    ExprPtr& getAddress() { return address; }
    const ExprPtr& getAddress() const { return address; }
};

// New: Represents loc(expression)
class LocationExpression : public Expression {
    ExprPtr expression;
public:
    LocationExpression(SourceLocation loc, ExprPtr expression);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
    ExprPtr& getExpression() { return expression; }
    const ExprPtr& getExpression() const { return expression; }
};

// --- Full Class Definition for ArrayElementExpression ---
// Placed after Node, Statement, Declaration, NodeType, Visitor, Identifier are defined.
class ArrayElementExpression : public Expression {
public:
    ExprPtr object; // The array or pointer object
    ExprPtr index;  // The index expression

    ArrayElementExpression(SourceLocation loc, ExprPtr object, ExprPtr index)
        : Expression(loc), object(std::move(object)), index(std::move(index)) {}

    NodeType getType() const override { return NodeType::ARRAY_ELEMENT_EXPRESSION; }
    std::string toString() const override {
        // Ensure object and index are not null before calling toString on them
        std::string objStr = object ? object->toString() : "null_obj";
        std::string idxStr = index ? index->toString() : "null_idx";
        return objStr + "[" + idxStr + "]";
    }
    void accept(Visitor& visitor) override { visitor.visit(this); }
};
// --- End of ArrayElementExpression Definition ---

// Represents one key-value pair in an object literal
struct ObjectProperty {
    SourceLocation loc; // Location of the property itself (key or key:value)
    std::unique_ptr<Identifier> key; // Property key (must be an identifier for now, can be extended to StringLiteral or computed)
    ExprPtr value; // Property value

    // Make members public for direct access from ObjectLiteral::toString()
public:
    ObjectProperty(SourceLocation loc, std::unique_ptr<Identifier> key, ExprPtr value)
        : loc(loc), key(std::move(key)), value(std::move(value)) {}
};


class ObjectLiteral : public Expression {
public:
    std::vector<ObjectProperty> properties;

    ObjectLiteral(SourceLocation loc, std::vector<ObjectProperty> properties);
    virtual ~ObjectLiteral();
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
    ExprPtr elementExpr;        // The expression for each element
    IdentifierPtr loopVariable;   // The variable in the loop (e.g., 'x' in 'for x in ...')
    ExprPtr iterableExpr;       // The expression for the iterable (e.g., 'my_list')
    ExprPtr conditionExpr;      // Optional condition (e.g., 'if x > 0')

    ListComprehension(SourceLocation loc, ExprPtr elementExpr, IdentifierPtr loopVariable, ExprPtr iterableExpr, ExprPtr conditionExpr = nullptr);
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

    CallExpression(SourceLocation loc, ExprPtr callee, std::vector<ExprPtr> arguments);
    virtual ~CallExpression();
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

    WhileStatement(SourceLocation loc, ExprPtr test, StmtPtr body);
    virtual ~WhileStatement();
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

class ReturnStatement : public Statement {
public:
    ExprPtr argument; // Optional, can be nullptr

    ReturnStatement(SourceLocation loc, ExprPtr argument = nullptr);
    virtual ~ReturnStatement();
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

class BreakStatement : public Statement {
public:
    BreakStatement(SourceLocation loc);
    virtual ~BreakStatement();
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

class ContinueStatement : public Statement {
public:
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
class GenericParamNode : public Node { 
public:
    std::unique_ptr<Identifier> name;
    std::vector<TypeNodePtr> bounds; // e.g. T: Bound1 + Bound2 (replaces TypeAnnotationPtr)

    GenericParamNode(SourceLocation loc, std::unique_ptr<Identifier> name, std::vector<TypeNodePtr> bounds = {});
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
}; // End of GenericParamNode


// --- Full Class Definition for TemplateDeclarationNode ---
// Placed after Node, Statement, Declaration, NodeType, Visitor, Identifier, GenericParamNode are defined.
class TemplateDeclarationNode : public Declaration {
public:
    std::unique_ptr<Identifier> name;
    std::vector<std::unique_ptr<GenericParamNode>> genericParams; 
    DeclPtr body;

    TemplateDeclarationNode(SourceLocation loc, std::unique_ptr<Identifier> name, std::vector<std::unique_ptr<GenericParamNode>> genericParams, DeclPtr body);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};
// Define Ptr alias after the class is fully defined.
using TemplateDeclarationNodePtr = std::unique_ptr<TemplateDeclarationNode>;


// Module (Root of the AST)
class Module : public Node {
public:
    std::vector<StmtPtr> body; // Sequence of statements (including declarations)

    Module(SourceLocation loc, std::vector<StmtPtr> body);
    NodeType getType() const override;
    std::string toString() const override;
    void accept(Visitor& visitor) override;
};

} // namespace ast
} // namespace vyn

#endif // VYN_PARSER_AST_HPP