#ifndef VYN_AST_HPP
#define VYN_AST_HPP

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <utility>
#include <cstdint> // Added for int64_t

#include "vyn/source_location.hpp" // For vyn::SourceLocation
#include "vyn/token.hpp"           // For vyn::token::Token

namespace vyn {

    // Forward declarations for Visitor pattern
    class Node;
    class Expression;
    class Statement;
    class Declaration;
    class Visitor;
    class NilLiteral; // Forward declare NilLiteral

    // Forward declarations for new nodes
    class StructDeclaration;
    class ClassDeclaration;
    class FieldDeclaration;
    class ImplDeclaration;
    class EnumDeclaration;
    class EnumVariantNode; 
    class GenericParamNode;
    class TypeNode; // Changed from TypeAnnotation
    class BorrowExprNode; // New

    // Type aliases for smart pointers
    using NodePtr = std::unique_ptr<Node>;
    using ExprPtr = std::unique_ptr<Expression>;
    using StmtPtr = std::unique_ptr<Statement>;
    using DeclPtr = std::unique_ptr<Declaration>;
    using TypeNodePtr = std::unique_ptr<TypeNode>; // Changed from TypeAnnotationPtr

    // New Enum for Type Categories
    enum class TypeCategory {
        IDENTIFIER,          // e.g., Int, String, MyStruct<T>
        OWNERSHIP_WRAPPED,   // e.g., my<T>, our<T>, their<T>, ptr<T>
        ARRAY,               // e.g., T[]
        TUPLE,               // e.g., (T1, T2)
        FUNCTION_SIGNATURE,  // e.g., fn(T1, T2) -> R
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
        ARRAY_LITERAL,
        OBJECT_LITERAL,
        NIL_LITERAL, // New

        // Expressions
        UNARY_EXPRESSION,
        BINARY_EXPRESSION,
        CALL_EXPRESSION,
        MEMBER_EXPRESSION,
        ASSIGNMENT_EXPRESSION,
        BORROW_EXPRESSION, // New

        // Statements
        BLOCK_STATEMENT,
        EXPRESSION_STATEMENT,
        IF_STATEMENT,
        FOR_STATEMENT,
        WHILE_STATEMENT,
        RETURN_STATEMENT,
        BREAK_STATEMENT,
        CONTINUE_STATEMENT,

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


        // Other
        TYPE_NODE, // Changed from TYPE_ANNOTATION
        MODULE
    };

    // Visitor Interface
    class Visitor {
    public:
        virtual ~Visitor() = default;

        // Literals
        virtual void visit(class Identifier* node) = 0;
        virtual void visit(class IntegerLiteral* node) = 0;
        virtual void visit(class FloatLiteral* node) = 0;
        virtual void visit(class StringLiteral* node) = 0;
        virtual void visit(class BooleanLiteral* node) = 0;
        virtual void visit(class ArrayLiteral* node) = 0;
        virtual void visit(class ObjectLiteral* node) = 0;
        virtual void visit(class NilLiteral* node) = 0; // New

        // Expressions
        virtual void visit(class UnaryExpression* node) = 0;
        virtual void visit(class BinaryExpression* node) = 0;
        virtual void visit(class CallExpression* node) = 0;
        virtual void visit(class MemberExpression* node) = 0;
        virtual void visit(class AssignmentExpression* node) = 0;
        virtual void visit(class BorrowExprNode* node) = 0; // New

        // Statements
        virtual void visit(class BlockStatement* node) = 0;
        virtual void visit(class ExpressionStatement* node) = 0;
        virtual void visit(class IfStatement* node) = 0;
        virtual void visit(class ForStatement* node) = 0;
        virtual void visit(class WhileStatement* node) = 0;
        virtual void visit(class ReturnStatement* node) = 0;
        virtual void visit(class BreakStatement* node) = 0;
        virtual void visit(class ContinueStatement* node) = 0;

        // Declarations
        virtual void visit(class VariableDeclaration* node) = 0;
        virtual void visit(class FunctionDeclaration* node) = 0;
        virtual void visit(class TypeAliasDeclaration* node) = 0;
        virtual void visit(class ImportDeclaration* node) = 0;
        virtual void visit(class StructDeclaration* node) = 0;
        virtual void visit(class ClassDeclaration* node) = 0;
        virtual void visit(class FieldDeclaration* node) = 0;
        virtual void visit(class ImplDeclaration* node) = 0;
        virtual void visit(class EnumDeclaration* node) = 0;
        virtual void visit(class EnumVariantNode* node) = 0;
        virtual void visit(class GenericParamNode* node) = 0;
        
        // Other
        virtual void visit(class TypeNode* node) = 0; // Changed from TypeAnnotation
        virtual void visit(class Module* node) = 0;
    };

    // Base AST Node
    class Node {
    public:
        SourceLocation loc; // This is vyn::SourceLocation from the include

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

    // --- New TypeNode Definition ---
    class TypeNode : public Node {
    public:
        TypeCategory category;
        bool dataIsConst; // True if this specific type construct is const, e.g. T const, (T1, T2) const, my<T> const
        bool isOptional;  // True if this type construct is optional, e.g. T?, my<T>?

        // Fields for IDENTIFIER category (and base for generic instantiation)
        std::unique_ptr<Identifier> name; 
        std::vector<TypeNodePtr> genericArguments;

        // Fields for OWNERSHIP_WRAPPED category
        OwnershipKind ownership; // my, our, their, ptr
        TypeNodePtr wrappedType; // The T in my<T>

        // Fields for ARRAY category
        TypeNodePtr arrayElementType; // The T in T[]

        // Fields for TUPLE category
        std::vector<TypeNodePtr> tupleElementTypes; // The T1, T2 in (T1, T2)

        // Fields for FUNCTION_SIGNATURE category
        std::vector<TypeNodePtr> functionParameterTypes;
        TypeNodePtr functionReturnType; // Can be nullptr if void or not specified yet

    private:
        // Private constructor to enforce use of static factory methods.
        // Implementations will be in ast.cpp
        TypeNode(SourceLocation loc, TypeCategory category, bool dataIsConst, bool isOptional);

    public:
        ~TypeNode() override = default;

        // Static factory methods (declarations)
        static TypeNodePtr newIdentifier(SourceLocation loc, std::unique_ptr<Identifier> name, std::vector<TypeNodePtr> genericArgs, bool dataIsConst, bool isOptional);
        static TypeNodePtr newOwnershipWrapped(SourceLocation loc, OwnershipKind ownership, TypeNodePtr wrappedType, bool dataIsConst, bool isOptional);
        static TypeNodePtr newArray(SourceLocation loc, TypeNodePtr elementType, bool dataIsConst, bool isOptional);
        static TypeNodePtr newTuple(SourceLocation loc, std::vector<TypeNodePtr> elementTypes, bool dataIsConst, bool isOptional);
        static TypeNodePtr newFunctionSignature(SourceLocation loc, std::vector<TypeNodePtr> paramTypes, TypeNodePtr returnType, bool dataIsConst, bool isOptional);

        NodeType getType() const override;
        std::string toString() const override;
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

    class ArrayLiteral : public Expression {
    public:
        std::vector<ExprPtr> elements;

        ArrayLiteral(SourceLocation loc, std::vector<ExprPtr> elements);
        NodeType getType() const override;
        std::string toString() const override;
        void accept(Visitor& visitor) override;
    };
    
    // Represents one key-value pair in an object literal
    struct ObjectProperty {
        SourceLocation loc; // Location of the property itself (key or key:value)
        std::unique_ptr<Identifier> key; // Property key (must be an identifier for now, can be extended to StringLiteral or computed)
        ExprPtr value; // Property value

        ObjectProperty(SourceLocation loc, std::unique_ptr<Identifier> key, ExprPtr value)
            : loc(loc), key(std::move(key)), value(std::move(value)) {}
    };


    class ObjectLiteral : public Expression {
    public:
        std::vector<ObjectProperty> properties;

        ObjectLiteral(SourceLocation loc, std::vector<ObjectProperty> properties);
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

    // --- Expressions ---
    class UnaryExpression : public Expression {
    public:
        vyn::token::Token op; // The operator token (full type known from token.hpp)
        ExprPtr operand;    // The expression being operated on

        // Constructor takes const reference for op, which is then copied to the member
        UnaryExpression(SourceLocation loc, const vyn::token::Token& op, ExprPtr operand);
        NodeType getType() const override;
        std::string toString() const override;
        void accept(Visitor& visitor) override;
    };

    class BinaryExpression : public Expression {
    public:
        ExprPtr left;
        vyn::token::Token op; // The operator token
        ExprPtr right;

        BinaryExpression(SourceLocation loc, ExprPtr left, const vyn::token::Token& op, ExprPtr right);
        NodeType getType() const override;
        std::string toString() const override;
        void accept(Visitor& visitor) override;
    };

    class CallExpression : public Expression {
    public:
        ExprPtr callee;
        std::vector<ExprPtr> arguments;

        CallExpression(SourceLocation loc, ExprPtr callee, std::vector<ExprPtr> arguments);
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
        NodeType getType() const override;
        std::string toString() const override;
        void accept(Visitor& visitor) override;
    };

    class AssignmentExpression : public Expression {
    public:
        ExprPtr left;  // LValue (Identifier or MemberExpression)
        vyn::token::Token op; // Assignment operator (e.g., =, +=)
        ExprPtr right; // RValue

        AssignmentExpression(SourceLocation loc, ExprPtr left, const vyn::token::Token& op, ExprPtr right);
        NodeType getType() const override;
        std::string toString() const override;
        void accept(Visitor& visitor) override;
    };

    // New Borrow Expression Node
    class BorrowExprNode : public Expression {
    public:
        BorrowKind kind; // Changed from bool isMutable
        ExprPtr expressionToBorrow; 

        BorrowExprNode(SourceLocation loc, ExprPtr expr, BorrowKind kind); // Updated constructor
        NodeType getType() const override;
        std::string toString() const override;
        void accept(Visitor& visitor) override;
    };


    // --- Statements ---
    class BlockStatement : public Statement {
    public:
        std::vector<StmtPtr> body;

        BlockStatement(SourceLocation loc, std::vector<StmtPtr> body);
        NodeType getType() const override;
        std::string toString() const override;
        void accept(Visitor& visitor) override;
    };

    class ExpressionStatement : public Statement {
    public:
        ExprPtr expression;

        ExpressionStatement(SourceLocation loc, ExprPtr expression);
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
        NodeType getType() const override;
        std::string toString() const override;
        void accept(Visitor& visitor) override;
    };

    class WhileStatement : public Statement {
    public:
        ExprPtr test;
        StmtPtr body;

        WhileStatement(SourceLocation loc, ExprPtr test, StmtPtr body);
        NodeType getType() const override;
        std::string toString() const override;
        void accept(Visitor& visitor) override;
    };

    class ReturnStatement : public Statement {
    public:
        ExprPtr argument; // Optional, can be nullptr

        ReturnStatement(SourceLocation loc, ExprPtr argument = nullptr);
        NodeType getType() const override;
        std::string toString() const override;
        void accept(Visitor& visitor) override;
    };

    class BreakStatement : public Statement {
    public:
        BreakStatement(SourceLocation loc);
        NodeType getType() const override;
        std::string toString() const override;
        void accept(Visitor& visitor) override;
    };

    class ContinueStatement : public Statement {
    public:
        ContinueStatement(SourceLocation loc);
        NodeType getType() const override;
        std::string toString() const override;
        void accept(Visitor& visitor) override;
    };
    
    // Forward declare TypeNode for use in declarations (already done above with TypeNodePtr)
    // class TypeNode; // No longer TypeAnnotation
    // using TypeNodePtr = std::unique_ptr<TypeNode>; // No longer TypeAnnotationPtr


    // --- Declarations ---
    class VariableDeclaration : public Declaration {
    public:
        std::unique_ptr<Identifier> id;
        bool isConst; // Binding const-ness (const x: T vs var x: T)
        TypeNodePtr typeNode; // Optional, type of the variable (replaces typeAnnotation)
        ExprPtr init;         // Optional

        VariableDeclaration(SourceLocation loc, std::unique_ptr<Identifier> id, bool isConst, TypeNodePtr typeNode = nullptr, ExprPtr init = nullptr);
        NodeType getType() const override;
        std::string toString() const override;
        void accept(Visitor& visitor) override;
    };
    
    struct FunctionParameter {
        SourceLocation loc;
        std::unique_ptr<Identifier> name;
        TypeNodePtr typeNode; // Type of the parameter (replaces typeAnnotation)

        FunctionParameter(SourceLocation loc, std::unique_ptr<Identifier> name, TypeNodePtr typeNode)
            : loc(loc), name(std::move(name)), typeNode(std::move(typeNode)) {}
    };


    class FunctionDeclaration : public Declaration {
    public:
        std::unique_ptr<Identifier> id;
        std::vector<FunctionParameter> params;
        std::unique_ptr<BlockStatement> body;
        TypeNodePtr returnTypeNode; // Optional, return type (replaces returnType which was TypeAnnotationPtr)
        bool isAsync;

        FunctionDeclaration(SourceLocation loc, std::unique_ptr<Identifier> id, std::vector<FunctionParameter> params, std::unique_ptr<BlockStatement> body, bool isAsync, TypeNodePtr returnTypeNode = nullptr);
        NodeType getType() const override;
        std::string toString() const override;
        void accept(Visitor& visitor) override;
    };

    class TypeAliasDeclaration : public Declaration {
    public:
        std::unique_ptr<Identifier> name;
        TypeNodePtr aliasedTypeNode; // The type being aliased (replaces typeAnnotation)

        TypeAliasDeclaration(SourceLocation loc, std::unique_ptr<Identifier> name, TypeNodePtr aliasedTypeNode);
        NodeType getType() const override;
        std::string toString() const override;
        void accept(Visitor& visitor) override;
    };

    // Helper for ImportDeclaration
    struct ImportSpecifier {
        SourceLocation loc;
        std::unique_ptr<Identifier> importedName; // The name as exported by the module
        std::unique_ptr<Identifier> localName;    // Optional: the alias, as in 'import { x as y }'

        ImportSpecifier(SourceLocation loc, std::unique_ptr<Identifier> importedName, std::unique_ptr<Identifier> localName = nullptr)
            : loc(loc), importedName(std::move(importedName)), localName(std::move(localName)) {}
    };

    class ImportDeclaration : public Declaration {
    public:
        std::unique_ptr<StringLiteral> source; // Path to the module
        
        // For named imports: import { name1, name2 as alias } from "module";
        std::vector<ImportSpecifier> specifiers;
        
        // For default import: import defaultName from "module";
        std::unique_ptr<Identifier> defaultImport; // Optional
        
        // For namespace import: import * as Namespace from "module";
        std::unique_ptr<Identifier> namespaceImport; // Optional, 'Namespace'

        ImportDeclaration(SourceLocation loc, std::unique_ptr<StringLiteral> source,
                          std::vector<ImportSpecifier> specifiers = {},
                          std::unique_ptr<Identifier> defaultImport = nullptr,
                          std::unique_ptr<Identifier> namespaceImport = nullptr);
        NodeType getType() const override;
        std::string toString() const override;
        void accept(Visitor& visitor) override;
    };


    // New Declaration Nodes based on AST.md

    class FieldDeclaration : public Declaration {
    public:
        std::unique_ptr<Identifier> name;
        TypeNodePtr typeNode; // Mandatory for fields (replaces typeAnnotation)
        ExprPtr initializer;              // Optional
        bool isMutable;                   // true for 'var', false for 'let'/'const'

        FieldDeclaration(SourceLocation loc, std::unique_ptr<Identifier> name, TypeNodePtr typeNode, ExprPtr init = nullptr, bool isMutable = false);
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

    class ImplDeclaration : public Declaration {
    public:
        std::vector<std::unique_ptr<GenericParamNode>> genericParams;
        TypeNodePtr selfTypeNode; // The type being implemented (replaces selfType)
        TypeNodePtr traitTypeNode; // Optional: if implementing a trait (replaces traitType)
        std::vector<std::unique_ptr<FunctionDeclaration>> methods;

        ImplDeclaration(SourceLocation loc, std::vector<std::unique_ptr<GenericParamNode>> genericParams, TypeNodePtr selfTypeNode, std::vector<std::unique_ptr<FunctionDeclaration>> methods, TypeNodePtr traitTypeNode = nullptr);
        NodeType getType() const override;
        std::string toString() const override;
        void accept(Visitor& visitor) override;
    };

    class EnumVariantNode : public Declaration { 
    public:
        std::unique_ptr<Identifier> name;
        std::vector<TypeNodePtr> typeNodes; // For tuple-like parameters (replaces types which was vector<TypeAnnotationPtr>)

        EnumVariantNode(SourceLocation loc, std::unique_ptr<Identifier> name, std::vector<TypeNodePtr> typeNodes = {});
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
    };

    // TypeAnnotation class is now removed and replaced by TypeNode above.
    // Remove old TypeAnnotation class definition entirely.
    /*
    class TypeAnnotation : public Node {
    public:
        // One of these will be non-null, defining the type of annotation
        std::unique_ptr<Identifier> simpleTypeName;      // e.g., 'int', 'MyClass'
        TypeAnnotationPtr arrayElementType;        // e.g., for 'int[]', this is 'int'
        
        std::unique_ptr<Identifier> genericBaseType;   // e.g., for 'List<string>', this is 'List'
        std::vector<TypeAnnotationPtr> genericTypeArguments; // e.g., for 'List<string>', this is ['string']

        // Constructors to enforce valid states
        // Simple type: MyType
        TypeAnnotation(SourceLocation loc, std::unique_ptr<Identifier> name);
        // Array type: T[] (elementType is T)
        TypeAnnotation(SourceLocation loc, TypeAnnotationPtr elementType, bool isArrayMarker); // Marker to distinguish from generic
        // Generic type: List<T> (base is List, args is <T>)
        TypeAnnotation(SourceLocation loc, std::unique_ptr<Identifier> base, std::vector<TypeAnnotationPtr> args);

        NodeType getType() const override;
        std::string toString() const override;
        void accept(Visitor& visitor) override;

        bool isSimpleType() const { return simpleTypeName && !arrayElementType && !genericBaseType; }
        bool isArrayType() const { return arrayElementType && !simpleTypeName && !genericBaseType; }
        bool isGenericType() const { return genericBaseType && !simpleTypeName && !arrayElementType; }
    };
    */
    // Module (Root of the AST)
    class Module : public Node {
    public:
        std::vector<StmtPtr> body; // Sequence of statements (including declarations)

        Module(SourceLocation loc, std::vector<StmtPtr> body);
        NodeType getType() const override;
        std::string toString() const override;
        void accept(Visitor& visitor) override;
    };

} // namespace vyn

#endif // VYN_AST_HPP