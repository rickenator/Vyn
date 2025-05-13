#ifndef VYN_AST_HPP
#define VYN_AST_HPP // Added include guard define

#include <string>
#include <vector>
#include <memory>
#include <utility> // For std::pair and std::move

// Forward declaration for Vyn::TokenType
namespace Vyn {
    enum class TokenType;
}

namespace Vyn::AST {

// Forward declarations for all AST node types and Visitor
class Visitor;
class Node; class ExprNode; class StmtNode; class DeclNode; class TypeNode; class PatternNode;
class LiteralNode; // Added
class IdentifierNode; class PathNode; // Added PathNode
class IntLiteralNode; class FloatLiteralNode; class StringLiteralNode; class BoolLiteralNode; class CharLiteralNode; class NullLiteralNode; // Added Char/Null
class ArrayLiteralNode; class TraitDeclNode; class TupleTypeNode; // Added ArrayLiteralNode, TraitDeclNode, TupleTypeNode
class BinaryOpNode; class UnaryOpNode; class CallNode; class MemberAccessNode; class IndexAccessNode; class IfExprNode; class BlockNode; class ExpressionStmtNode; class VarDeclNode; class FunctionDeclNode; class ReturnStmtNode; class IfStmtNode; class WhileStmtNode; class ForStmtNode; class BreakStmtNode; class ContinueStmtNode; class StructDeclNode; class EnumDeclNode; class FieldDeclNode; class TypeNameNode; class IdentifierPatternNode; class StructPatternNode; class EnumVariantPatternNode; class ClassDeclNode; class MethodDeclNode; class ImportStmtNode; class ModuleNode;
class TupleLiteralNode; class StructLiteralNode; class GenericParamNode; class ParamNode; class ArrayTypeNode; class PointerTypeNode; class OptionalTypeNode; class EnumVariantNode; class TypeAliasDeclNode; class GlobalVarDeclNode;
class ReferenceTypeNode; class FunctionTypeNode; class GenericInstanceTypeNode; // Added forward declarations


// Type Aliases for smart pointers
using NodePtr = std::unique_ptr<Node>;
using ExprPtr = std::unique_ptr<ExprNode>;
using StmtPtr = std::unique_ptr<StmtNode>;
using DeclPtr = std::unique_ptr<DeclNode>;
using TypePtr = std::unique_ptr<TypeNode>;
using PatternPtr = std::unique_ptr<PatternNode>;
using LiteralPtr = std::unique_ptr<LiteralNode>; // Added for LiteralNode
using IdentifierPtr = std::unique_ptr<IdentifierNode>; // Added for IdentifierNode
using PathPtr = std::unique_ptr<PathNode>; // Added for PathNode


struct SourceLocation {
    std::string filePath;
    int line;
    int column;

    SourceLocation(std::string fp = "", int l = 0, int c = 0) : filePath(std::move(fp)), line(l), column(c) {} // Added initializer list
    
    std::string toString() const { // Added toString() method
        return filePath + ":" + std::to_string(line) + ":" + std::to_string(column);
    }
};

enum class NodeType {
    // Literals & Basic
    IDENTIFIER,
    PATH,
    INT_LITERAL,
    FLOAT_LITERAL,
    STRING_LITERAL,
    BOOL_LITERAL,
    CHAR_LITERAL, // Added
    NULL_LITERAL, // Added
    ARRAY_LITERAL, // Added NodeType for ArrayLiteralNode

    // Expressions
    UNARY_OP,
    BINARY_OP,
    CALL_EXPR, // Renamed from CALL for consistency, assuming CallNode is CallExprNode
    ARRAY_ACCESS,
    MEMBER_ACCESS,
    ASSIGNMENT, // Can be an expression or a statement

    // Statements
    EXPRESSION_STMT,
    BLOCK_STMT,
    VAR_DECL_STMT,
    IF_STMT,
    FOR_STMT,
    WHILE_STMT,
    RETURN_STMT,
    BREAK_STMT,
    CONTINUE_STMT,

    // Declarations
    FUNC_DECL,
    PARAM,
    STRUCT_DECL,
    FIELD_DECL,  // Added NodeType for FieldDeclNode
    CLASS_DECL,  // Added NodeType for ClassDeclNode
    IMPL_DECL,
    GENERIC_PARAM,
    ENUM_DECL,      // Added
    ENUM_VARIANT,   // Added
    TYPE_ALIAS_DECL, // Added
    GLOBAL_VAR_DECL, // Added
    TRAIT_DECL,      // Added NodeType for TraitDeclNode

    // Types
    TYPE_NAME, // This might be IdentifierTypeNode or a specific TypeNameNode
    ARRAY_TYPE,
    POINTER_TYPE,
    OPTIONAL_TYPE,
    IDENTIFIER_TYPE, // Added for IdentifierTypeNode
    TUPLE_TYPE,      // Added NodeType for TupleTypeNode
    REFERENCE_TYPE,        // Added
    FUNCTION_TYPE,         // Added
    GENERIC_INSTANCE_TYPE, // Added

    // Literals that are also expressions (or part of expressions)
    TUPLE_LITERAL,      // Added
    STRUCT_LITERAL,     // Added
    // ENUM_LITERAL,       // Added - Consider if this is needed or covered by Path + Call-like syntax

    // Patterns
    IDENTIFIER_PATTERN,
    LITERAL_PATTERN,
    WILDCARD_PATTERN,
    TUPLE_PATTERN,
    ENUM_VARIANT_PATTERN,
    STRUCT_PATTERN, // Added for StructPatternNode

    // Module
    MODULE,

    // Base/Abstract types - useful for checks but not for concrete nodes
    NODE,
    EXPR_NODE,
    STMT_NODE,
    DECL_NODE,
    TYPE_NODE,
    PATTERN_NODE,
    LITERAL_NODE // Added
};

class Node {
public:
    NodeType type;
    SourceLocation location;
    Node* parent;

    Node(NodeType t, SourceLocation loc, Node* p = nullptr) : type(t), location(std::move(loc)), parent(p) {} // Initializer list
    virtual ~Node() = default;
    virtual void accept(Visitor& visitor) = 0;
    virtual std::string toString(int indent = 0) const = 0;
};

// Intermediate base classes
class ExprNode : public Node {
public:
    ExprNode(NodeType t, SourceLocation loc, Node* p = nullptr) : Node(t, std::move(loc), p) {}
};

class StmtNode : public Node {
public:
    StmtNode(NodeType t, SourceLocation loc, Node* p = nullptr) : Node(t, std::move(loc), p) {}
};

class DeclNode : public Node {
public:
    DeclNode(NodeType t, SourceLocation loc, Node* p = nullptr) : Node(t, std::move(loc), p) {}
};

class TypeNode : public Node {
public:
    TypeNode(NodeType t, SourceLocation loc, Node* p = nullptr) : Node(t, std::move(loc), p) {}
};

class PatternNode : public Node {
public:
    PatternNode(NodeType t, SourceLocation loc, Node* p = nullptr)
        : Node(t, std::move(loc), p) {}
};

// Base class for all literals
class LiteralNode : public ExprNode {
public:
    LiteralNode(NodeType t, SourceLocation loc, Node* p = nullptr)
        : ExprNode(t, std::move(loc), p) {}
    // accept and toString are pure virtual, inherited from ExprNode (ultimately Node)
};

// Moved IdentifierNode definition earlier
class IdentifierNode : public ExprNode {
public:
    std::string name;
    IdentifierNode(SourceLocation loc, std::string n, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

// Moved PathNode definition earlier
class PathNode : public Node {
public:
    std::vector<std::unique_ptr<IdentifierNode>> segments;
    // bool is_absolute; // Optional: for paths like ::foo::bar

    PathNode(SourceLocation loc, std::vector<std::unique_ptr<IdentifierNode>> segs, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
    std::string getPathString() const;
};


// Specific Literal Node Definitions
class IntLiteralNode : public LiteralNode {
public:
    long long value;
    IntLiteralNode(SourceLocation loc, long long val, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

class FloatLiteralNode : public LiteralNode {
public:
    double value;
    FloatLiteralNode(SourceLocation loc, double val, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

class StringLiteralNode : public LiteralNode {
public:
    std::string value;
    StringLiteralNode(SourceLocation loc, std::string val, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

class BoolLiteralNode : public LiteralNode {
public:
    bool value;
    BoolLiteralNode(SourceLocation loc, bool val, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

class CharLiteralNode : public LiteralNode { // Added definition
public:
    char value;
    CharLiteralNode(SourceLocation loc, char val, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

class NullLiteralNode : public LiteralNode { // Added definition
public:
    NullLiteralNode(SourceLocation loc, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

// Added ArrayLiteralNode definition
class ArrayLiteralNode : public ExprNode {
public:
    std::vector<std::unique_ptr<ExprNode>> elements;

    ArrayLiteralNode(SourceLocation loc, std::vector<std::unique_ptr<ExprNode>> elems, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};


// Generic Parameter Node
class GenericParamNode : public Node { // Inherits Node, so has location
public:
    // SourceLocation location; // Removed, inherited from Node
    std::unique_ptr<IdentifierNode> name;
    std::vector<std::unique_ptr<TypeNode>> trait_bounds; // e.g. for T: Trait1 + Trait2

    GenericParamNode(SourceLocation loc, std::unique_ptr<IdentifierNode> param_name, std::vector<std::unique_ptr<TypeNode>> bounds, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

// Specific Pattern Nodes
class IdentifierPatternNode : public PatternNode {
public:
    std::unique_ptr<IdentifierNode> identifier;
    bool isMutable;

    IdentifierPatternNode(SourceLocation loc, std::unique_ptr<IdentifierNode> id, bool mut = false, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

class LiteralPatternNode : public PatternNode {
public:
    std::unique_ptr<ExprNode> literal; // e.g., IntLiteralNode, StringLiteralNode, BoolLiteralNode

    LiteralPatternNode(SourceLocation loc, std::unique_ptr<ExprNode> lit, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

class WildcardPatternNode : public PatternNode {
public:
    WildcardPatternNode(SourceLocation loc, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

class TuplePatternNode : public PatternNode {
public:
    std::vector<std::unique_ptr<PatternNode>> elements;

    TuplePatternNode(SourceLocation loc, std::vector<std::unique_ptr<PatternNode>> elems, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

class EnumVariantPatternNode : public PatternNode {
public:
    std::unique_ptr<PathNode> path;
    std::vector<std::unique_ptr<PatternNode>> arguments;

    EnumVariantPatternNode(SourceLocation loc, std::unique_ptr<PathNode> p, std::vector<std::unique_ptr<PatternNode>> args, Node* parent = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

struct StructPatternField {
    SourceLocation location; // This is a helper struct, might keep its own location or ensure it's passed if needed
    std::string field_name;
    std::unique_ptr<PatternNode> pattern;

    StructPatternField(SourceLocation loc, std::string name, std::unique_ptr<PatternNode> pat)
        : location(std::move(loc)), field_name(std::move(name)), pattern(std::move(pat)) {}
    
    std::string toString(int indent = 0) const;
};

class StructPatternNode : public PatternNode {
public:
    std::unique_ptr<PathNode> struct_path;
    std::vector<StructPatternField> fields;

    StructPatternNode(SourceLocation loc, std::unique_ptr<PathNode> path, std::vector<StructPatternField> flds, Node* p = nullptr);
    void accept(Visitor& visitor) override;
    std::string toString(int indent = 0) const override;
};

// Specific Expression Nodes

class TupleLiteralNode : public ExprNode {
public:
    // SourceLocation location; // Removed, inherited
    std::vector<std::unique_ptr<ExprNode>> elements;

    TupleLiteralNode(SourceLocation loc, std::vector<std::unique_ptr<ExprNode>> elems, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

struct StructLiteralField {
    SourceLocation location; // Helper struct
    std::unique_ptr<IdentifierNode> name;
    std::unique_ptr<ExprNode> value;

    StructLiteralField(SourceLocation loc, std::unique_ptr<IdentifierNode> field_name, std::unique_ptr<ExprNode> field_value);
    std::string toString(int indent = 0) const;
};

class StructLiteralNode : public ExprNode {
public:
    // SourceLocation location; // Removed, inherited
    std::unique_ptr<TypeNode> type; 
    std::vector<StructLiteralField> fields;

    StructLiteralNode(SourceLocation loc, std::unique_ptr<TypeNode> struct_type, std::vector<StructLiteralField> flds, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

class UnaryOpNode : public ExprNode {
public:
    Vyn::TokenType op; // Uses forward-declared Vyn::TokenType
    std::unique_ptr<ExprNode> operand;

    UnaryOpNode(SourceLocation loc, Vyn::TokenType o, std::unique_ptr<ExprNode> oper, Node* p = nullptr); // Changed ::TokenType to Vyn::TokenType
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

// Added full definition for BinaryOpNode
class BinaryOpNode : public ExprNode {
public:
    Vyn::TokenType op; // Uses forward-declared Vyn::TokenType
    std::unique_ptr<ExprNode> left;
    std::unique_ptr<ExprNode> right;

    BinaryOpNode(SourceLocation loc, Vyn::TokenType o, std::unique_ptr<ExprNode> l, std::unique_ptr<ExprNode> r, Node* p = nullptr); // Changed ::TokenType to Vyn::TokenType
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

// Assuming CallNode mentioned in Visitor is CallExprNode
class CallExprNode : public ExprNode { // Renamed from CallNode if it was meant to be this
public:
    std::unique_ptr<ExprNode> callee;
    std::vector<std::unique_ptr<ExprNode>> arguments;

    CallExprNode(SourceLocation loc, std::unique_ptr<ExprNode> cal, std::vector<std::unique_ptr<ExprNode>> args, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

class ArrayAccessNode : public ExprNode { // Assuming IndexAccessNode in Visitor is this
public:
    std::unique_ptr<ExprNode> array;
    std::unique_ptr<ExprNode> index;

    ArrayAccessNode(SourceLocation loc, std::unique_ptr<ExprNode> arr, std::unique_ptr<ExprNode> idx, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

class MemberAccessNode : public ExprNode {
public:
    std::unique_ptr<ExprNode> object;
    std::unique_ptr<IdentifierNode> member;

    MemberAccessNode(SourceLocation loc, std::unique_ptr<ExprNode> obj, std::unique_ptr<IdentifierNode> mem, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

class AssignmentNode : public ExprNode { // Or StmtNode if it can only be a statement
public:
    std::unique_ptr<ExprNode> left;
    std::unique_ptr<ExprNode> right;

    AssignmentNode(SourceLocation loc, std::unique_ptr<ExprNode> l, std::unique_ptr<ExprNode> r, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

// Specific Statement Nodes
class BlockStmtNode : public StmtNode { // Assuming BlockNode in Visitor is this
public:
    std::vector<std::unique_ptr<StmtNode>> statements;

    BlockStmtNode(SourceLocation loc, std::vector<std::unique_ptr<StmtNode>> stmts, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

class ReturnStmtNode : public StmtNode {
public:
    std::unique_ptr<ExprNode> value; // Can be nullptr

    ReturnStmtNode(SourceLocation loc, std::unique_ptr<ExprNode> val, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

class IfStmtNode : public StmtNode {
public:
    std::unique_ptr<ExprNode> condition;
    std::unique_ptr<BlockStmtNode> thenBranch;
    std::unique_ptr<StmtNode> elseBranch; // Can be IfStmtNode or BlockStmtNode, or nullptr

    IfStmtNode(SourceLocation loc, std::unique_ptr<ExprNode> cond, std::unique_ptr<BlockStmtNode> thenB, std::unique_ptr<StmtNode> elseB, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

class ExpressionStmtNode : public StmtNode {
public:
    std::unique_ptr<ExprNode> expression;

    ExpressionStmtNode(SourceLocation loc, std::unique_ptr<ExprNode> expr, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

class VarDeclStmtNode : public StmtNode { // Assuming VarDeclNode in Visitor is this
public:
    std::unique_ptr<PatternNode> pattern;
    std::unique_ptr<TypeNode> typeAnnotation; // Optional
    std::unique_ptr<ExprNode> initializer;    // Optional
    bool isMut; // true for 'var', false for 'let'

    VarDeclStmtNode(SourceLocation loc, std::unique_ptr<PatternNode> pat, std::unique_ptr<TypeNode> ta, std::unique_ptr<ExprNode> init, bool mut, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

class WhileStmtNode : public StmtNode {
public:
    std::unique_ptr<ExprNode> condition;
    std::unique_ptr<BlockStmtNode> body;

    WhileStmtNode(SourceLocation loc, std::unique_ptr<ExprNode> cond, std::unique_ptr<BlockStmtNode> b, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

class ForStmtNode : public StmtNode {
public:
    PatternPtr pattern;
    ExprPtr iterable;
    std::unique_ptr<BlockStmtNode> body;

    ForStmtNode(SourceLocation loc, PatternPtr pat, ExprPtr iter, std::unique_ptr<BlockStmtNode> b, Node* p = nullptr);
    void accept(Visitor& visitor) override;
    std::string toString(int indent = 0) const override;
};

class BreakStmtNode : public StmtNode {
public:
    BreakStmtNode(SourceLocation loc, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

class ContinueStmtNode : public StmtNode {
public:
    ContinueStmtNode(SourceLocation loc, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};


// Specific Declaration Nodes
class ParamNode : public DeclNode {
public:
    bool isRef;
    bool isMut;
    std::unique_ptr<IdentifierNode> name;
    std::unique_ptr<TypeNode> typeAnnotation;

    ParamNode(SourceLocation loc, bool ref, bool mut, std::unique_ptr<IdentifierNode> n, std::unique_ptr<TypeNode> ta, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};


class FuncDeclNode : public DeclNode { // Assuming FunctionDeclNode in Visitor is this
public:
    std::unique_ptr<IdentifierNode> name;
    std::vector<std::unique_ptr<GenericParamNode>> generic_params;
    std::vector<std::unique_ptr<ParamNode>> params;
    std::unique_ptr<TypeNode> return_type; // Optional
    std::unique_ptr<BlockStmtNode> body;   // Optional (for extern functions)
    bool is_async;
    bool is_extern;

    FuncDeclNode(SourceLocation loc, std::unique_ptr<IdentifierNode> func_name,
                 std::vector<std::unique_ptr<GenericParamNode>> gen_params,
                 std::vector<std::unique_ptr<ParamNode>> func_params,
                 std::unique_ptr<TypeNode> ret_type, std::unique_ptr<BlockStmtNode> func_body,
                 bool async, bool ext, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

class FieldDeclNode : public DeclNode {
public:
    std::unique_ptr<IdentifierNode> name;
    std::unique_ptr<TypeNode> type_annotation;
    std::unique_ptr<ExprNode> initializer; // Optional
    bool is_mutable;

    FieldDeclNode(SourceLocation loc,\
                  std::unique_ptr<IdentifierNode> field_name,\
                  std::unique_ptr<TypeNode> type_ann,\
                  std::unique_ptr<ExprNode> init,\
                  bool is_mut,\
                  Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

class StructDeclNode : public DeclNode {
public:
    std::unique_ptr<IdentifierNode> name;
    std::vector<std::unique_ptr<GenericParamNode>> generic_params;
    std::vector<std::unique_ptr<FieldDeclNode>> fields;


    StructDeclNode(SourceLocation loc, std::unique_ptr<IdentifierNode> struct_name,\
                   std::vector<std::unique_ptr<GenericParamNode>> gen_params,\
                   std::vector<std::unique_ptr<FieldDeclNode>> struct_fields, 
                   Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

// Added TraitDeclNode definition
class TraitDeclNode : public DeclNode {
public:
    std::unique_ptr<IdentifierNode> name;
    std::vector<std::unique_ptr<GenericParamNode>> generic_params;
    std::vector<std::unique_ptr<Node>> members; // Method signatures, associated types

    TraitDeclNode(SourceLocation loc, std::unique_ptr<IdentifierNode> n,
                  std::vector<std::unique_ptr<GenericParamNode>> gp,
                  std::vector<std::unique_ptr<Node>> mem, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};


class ClassDeclNode : public DeclNode {
public:
    std::unique_ptr<IdentifierNode> name;
    std::vector<std::unique_ptr<GenericParamNode>> generic_params;
    std::vector<std::unique_ptr<DeclNode>> members; // FieldDeclNode or FuncDeclNode (as MethodDeclNode)

    ClassDeclNode(SourceLocation loc,\
                  std::unique_ptr<IdentifierNode> class_name,\
                  std::vector<std::unique_ptr<GenericParamNode>> gen_params,\
                  std::vector<std::unique_ptr<DeclNode>> class_members,\
                  Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

class ImplDeclNode : public DeclNode {
public:
    std::unique_ptr<TypeNode> trait_type; // Optional
    std::unique_ptr<TypeNode> self_type;
    std::vector<std::unique_ptr<GenericParamNode>> generic_params;
    std::vector<std::unique_ptr<FuncDeclNode>> methods; // Or MethodDeclNode

    ImplDeclNode(SourceLocation loc, std::unique_ptr<TypeNode> tr_type, std::unique_ptr<TypeNode> slf_type,\
                 std::vector<std::unique_ptr<GenericParamNode>> gen_params,\
                 std::vector<std::unique_ptr<FuncDeclNode>> impl_methods, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

// Specific Type Nodes

class TypeNameNode : public Node { 
public:
    std::string name_str; 
    std::vector<TypeNameNode*> genericArgs; 
    bool isOptional;

    TypeNameNode(SourceLocation loc, const std::string& n, const std::vector<TypeNameNode*>& gArgs, bool isOpt, Node* p = nullptr);
    ~TypeNameNode() override; 
    void accept(Visitor& visitor) override; 
    std::string toString(int indent = 0) const override; 
};


class IdentifierTypeNode : public TypeNode {
public:
    std::unique_ptr<PathNode> path;

    IdentifierTypeNode(SourceLocation loc, std::unique_ptr<PathNode> type_path, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

// Added TupleTypeNode definition
class TupleTypeNode : public TypeNode {
public:
    std::vector<std::unique_ptr<TypeNode>> elementTypes;

    TupleTypeNode(SourceLocation loc, std::vector<std::unique_ptr<TypeNode>> types, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

class ArrayTypeNode : public TypeNode {
public:
    std::unique_ptr<TypeNode> elementType;
    std::unique_ptr<ExprNode> size; // Optional

    ArrayTypeNode(SourceLocation loc, std::unique_ptr<TypeNode> et, std::unique_ptr<ExprNode> sz, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

class PointerTypeNode : public TypeNode {
public:
    std::unique_ptr<TypeNode> pointedToType;
    bool isMutable;

    PointerTypeNode(SourceLocation loc, std::unique_ptr<TypeNode> ptt, bool mut, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

class OptionalTypeNode : public TypeNode {
public:
    std::unique_ptr<TypeNode> wrappedType;

    OptionalTypeNode(SourceLocation loc, std::unique_ptr<TypeNode> wt, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

class ReferenceTypeNode : public TypeNode {
public:
    TypePtr referenced_type;
    bool is_mutable;

    ReferenceTypeNode(SourceLocation loc, TypePtr ref_type, bool mut, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

class FunctionTypeNode : public TypeNode {
public:
    std::vector<TypePtr> param_types;
    TypePtr return_type; 

    FunctionTypeNode(SourceLocation loc, std::vector<TypePtr> p_types, TypePtr ret_type, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};

class GenericInstanceTypeNode : public TypeNode {
public:
    TypePtr base_type; 
    std::vector<TypePtr> type_arguments;

    GenericInstanceTypeNode(SourceLocation loc, TypePtr b_type, std::vector<TypePtr> t_args, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};


// Module Node
class ModuleNode : public Node {
public:
    std::vector<std::unique_ptr<Node>> body;

    ModuleNode(SourceLocation loc, std::vector<std::unique_ptr<Node>> b, Node* p = nullptr);
    std::string toString(int indent = 0) const override;
    void accept(Visitor& visitor) override;
};


// Enum Structures
class EnumVariantNode : public Node { 
public:
    std::unique_ptr<IdentifierNode> name;
    std::vector<std::unique_ptr<TypeNode>> types; 

    EnumVariantNode(SourceLocation loc, std::unique_ptr<IdentifierNode> n, std::vector<std::unique_ptr<TypeNode>> t_params, Node* p = nullptr); 
    ~EnumVariantNode() override = default;

    void accept(Visitor& visitor) override;
    std::string toString(int indent = 0) const override;
};

class EnumDeclNode : public DeclNode {
public:
    std::unique_ptr<IdentifierNode> name;
    std::vector<std::unique_ptr<GenericParamNode>> generic_params;
    std::vector<std::unique_ptr<EnumVariantNode>> variants;

    EnumDeclNode(SourceLocation loc, std::unique_ptr<IdentifierNode> n,\
                 std::vector<std::unique_ptr<GenericParamNode>> gp,\
                 std::vector<std::unique_ptr<EnumVariantNode>> v, Node* p = nullptr); 
    ~EnumDeclNode() override = default;

    void accept(Visitor& visitor) override;
    std::string toString(int indent = 0) const override;
};

class TypeAliasDeclNode : public DeclNode {
public:
    std::unique_ptr<IdentifierNode> name;
    std::vector<std::unique_ptr<GenericParamNode>> generic_params;
    std::unique_ptr<TypeNode> aliased_type;

    TypeAliasDeclNode(SourceLocation loc, \
                      std::unique_ptr<IdentifierNode> n,\
                      std::vector<std::unique_ptr<GenericParamNode>> gp,\
                      std::unique_ptr<TypeNode> at, Node* p = nullptr); 
    ~TypeAliasDeclNode() override = default;

    void accept(Visitor& visitor) override;
    std::string toString(int indent = 0) const override;
};

class GlobalVarDeclNode : public DeclNode {
public:
    std::unique_ptr<PatternNode> pattern;
    std::unique_ptr<TypeNode> type_annotation; // Optional
    std::unique_ptr<ExprNode> initializer;     // Optional
    bool is_mutable;                           // true for 'var', false for 'let' or 'const'
    bool is_const;                             // true if 'const' keyword is used

    GlobalVarDeclNode(SourceLocation loc,\
                      std::unique_ptr<PatternNode> pat,\
                      std::unique_ptr<TypeNode> ta,\
                      std::unique_ptr<ExprNode> init,\
                      bool is_mut,\
                      bool is_cst, Node* p = nullptr); 
    ~GlobalVarDeclNode() override = default;

    void accept(Visitor& visitor) override;
    std::string toString(int indent = 0) const override;
};

// Visitor Base Class Definition
class Visitor {
public:
    virtual ~Visitor() = default;

    // Base AST Node types
    virtual void visit(Node* node);
    virtual void visit(ExprNode* node);
    virtual void visit(StmtNode* node);
    virtual void visit(DeclNode* node);
    virtual void visit(PatternNode* node);
    virtual void visit(LiteralNode* node);
    virtual void visit(TypeNode* node); // For base TypeNode

    // Literals
    virtual void visit(IntLiteralNode* node);
    virtual void visit(FloatLiteralNode* node);
    virtual void visit(StringLiteralNode* node);
    virtual void visit(BoolLiteralNode* node);
    virtual void visit(CharLiteralNode* node);
    virtual void visit(NullLiteralNode* node);
    virtual void visit(ArrayLiteralNode* node);
    
    virtual void visit(IdentifierNode* node);
    virtual void visit(PathNode* node);

    // Expressions
    virtual void visit(BinaryOpNode* node); 
    virtual void visit(UnaryOpNode* node);
    virtual void visit(CallExprNode* node);
    virtual void visit(MemberAccessNode* node);
    virtual void visit(ArrayAccessNode* node);
    virtual void visit(TupleLiteralNode* node);
    virtual void visit(StructLiteralNode* node);
    virtual void visit(AssignmentNode* node);
    // virtual void visit(IfExprNode* node); // Not in current ast.hpp forward declarations/definitions

    // Statements
    virtual void visit(BlockStmtNode* node); // Ensure UNCOMMENTED
    virtual void visit(ExpressionStmtNode* node);
    virtual void visit(VarDeclStmtNode* node); // Ensure UNCOMMENTED
    virtual void visit(ReturnStmtNode* node);
    virtual void visit(IfStmtNode* node);
    virtual void visit(WhileStmtNode* node);
    virtual void visit(ForStmtNode* node);
    virtual void visit(BreakStmtNode* node);
    virtual void visit(ContinueStmtNode* node);
    // virtual void visit(ImportStmtNode* node); // Not in current ast.hpp forward declarations/definitions

    // Declarations
    virtual void visit(FuncDeclNode* node); // Ensure UNCOMMENTED
    virtual void visit(ParamNode* node);
    virtual void visit(GenericParamNode* node);
    virtual void visit(StructDeclNode* node);
    virtual void visit(EnumDeclNode* node);
    virtual void visit(EnumVariantNode* node);
    virtual void visit(FieldDeclNode* node);
    virtual void visit(TraitDeclNode* node);
    // virtual void visit(ClassDeclNode* node); // Not in current ast.hpp forward declarations/definitions
    // virtual void visit(MethodDeclNode* node); // Not in current ast.hpp forward declarations/definitions
    virtual void visit(ImplDeclNode* node);
    virtual void visit(TypeAliasDeclNode* node);
    virtual void visit(GlobalVarDeclNode* node);

    // Types
    virtual void visit(TypeNameNode* node);
    virtual void visit(IdentifierTypeNode* node);
    virtual void visit(ArrayTypeNode* node);
    virtual void visit(PointerTypeNode* node);
    virtual void visit(OptionalTypeNode* node);
    virtual void visit(TupleTypeNode* node);
    virtual void visit(ReferenceTypeNode* node);        // Added
    virtual void visit(FunctionTypeNode* node);         // Added
    virtual void visit(GenericInstanceTypeNode* node);  // Added
    // visit(TypeNode* node) is with base types

    // Patterns
    virtual void visit(IdentifierPatternNode* node);
    virtual void visit(LiteralPatternNode* node);
    virtual void visit(WildcardPatternNode* node);
    virtual void visit(TuplePatternNode* node);
    virtual void visit(StructPatternNode* node);
    virtual void visit(EnumVariantPatternNode* node);
    
    // Module
    virtual void visit(ModuleNode* node);

    // Catch-all for nodes not explicitly handled, or for specific handling in implementations
};

} // namespace Vyn::AST

#endif // VYN_AST_HPP