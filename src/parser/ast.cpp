// All includes at the top
#include <sstream>
#include <string>
#include <utility>
#include <optional>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include "vyn/parser/ast.hpp"
#include "vyn/parser/token.hpp"

namespace vyn {

// ObjectLiteral destructor
ObjectLiteral::~ObjectLiteral() = default;

// ObjectLiteral constructor implementation
ObjectLiteral::ObjectLiteral(SourceLocation loc, std::vector<ObjectProperty> properties)
    : Expression(loc), properties(std::move(properties)) {}

// --- TryStatement Implementation ---
TryStatement::TryStatement(const SourceLocation& loc, std::unique_ptr<BlockStatement> tryBlock,
                 std::optional<std::string> catchIdent,
                 std::unique_ptr<BlockStatement> catchBlock,
                 std::unique_ptr<BlockStatement> finallyBlock)
    : Statement(loc),
      tryBlock(std::move(tryBlock)),
      catchIdent(std::move(catchIdent)),
      catchBlock(std::move(catchBlock)),
      finallyBlock(std::move(finallyBlock)) {}

NodeType TryStatement::getType() const { return NodeType::TRY_STATEMENT; }

std::string TryStatement::toString() const {
    std::stringstream ss;
    ss << "try " << (tryBlock ? tryBlock->toString() : "<null>");
    if (catchBlock) {
        ss << " catch";
        if (catchIdent) ss << "(" << *catchIdent << ")";
        ss << " " << catchBlock->toString();
    }
    if (finallyBlock) {
        ss << " finally " << finallyBlock->toString();
    }
    return ss.str();
}

void TryStatement::accept(Visitor& visitor) {
    visitor.visit(this);
}
// --- ImportDeclaration methods ---
ImportDeclaration::ImportDeclaration(
    SourceLocation loc,
    std::unique_ptr<StringLiteral> source,
    std::vector<ImportSpecifier> specifiers,
    std::unique_ptr<Identifier> defaultImport,
    std::unique_ptr<Identifier> namespaceImport)
    : Declaration(loc),
      source(std::move(source)),
      specifiers(std::move(specifiers)),
      defaultImport(std::move(defaultImport)),
      namespaceImport(std::move(namespaceImport)) {}

NodeType ImportDeclaration::getType() const { return NodeType::IMPORT_DECLARATION; }
std::string ImportDeclaration::toString() const { return "ImportDeclaration"; }
void ImportDeclaration::accept(Visitor& v) { v.visit(this); }

// --- STUBS for missing virtuals to resolve linker errors ---
// Expressions
NodeType CallExpression::getType() const { return NodeType::CALL_EXPRESSION; }
std::string CallExpression::toString() const { return "CallExpression"; }
void CallExpression::accept(Visitor& v) { v.visit(this); }

NodeType MemberExpression::getType() const { return NodeType::MEMBER_EXPRESSION; }
std::string MemberExpression::toString() const { return "MemberExpression"; }
void MemberExpression::accept(Visitor& v) { v.visit(this); }

NodeType UnaryExpression::getType() const { return NodeType::UNARY_EXPRESSION; }
std::string UnaryExpression::toString() const { return "UnaryExpression"; }
void UnaryExpression::accept(Visitor& v) { v.visit(this); }

NodeType BinaryExpression::getType() const { return NodeType::BINARY_EXPRESSION; }
std::string BinaryExpression::toString() const { return "BinaryExpression"; }
void BinaryExpression::accept(Visitor& v) { v.visit(this); }

NodeType AssignmentExpression::getType() const { return NodeType::ASSIGNMENT_EXPRESSION; }
std::string AssignmentExpression::toString() const { return "AssignmentExpression"; }
void AssignmentExpression::accept(Visitor& v) { v.visit(this); }

// Statements
NodeType BlockStatement::getType() const { return NodeType::BLOCK_STATEMENT; }
std::string BlockStatement::toString() const { return "BlockStatement"; }
void BlockStatement::accept(Visitor& v) { v.visit(this); }

NodeType ExpressionStatement::getType() const { return NodeType::EXPRESSION_STATEMENT; }
std::string ExpressionStatement::toString() const { return "ExpressionStatement"; }
void ExpressionStatement::accept(Visitor& v) { v.visit(this); }

NodeType IfStatement::getType() const { return NodeType::IF_STATEMENT; }
std::string IfStatement::toString() const { return "IfStatement"; }
void IfStatement::accept(Visitor& v) { v.visit(this); }

NodeType WhileStatement::getType() const { return NodeType::WHILE_STATEMENT; }
std::string WhileStatement::toString() const { return "WhileStatement"; }
void WhileStatement::accept(Visitor& v) { v.visit(this); }

NodeType ForStatement::getType() const { return NodeType::FOR_STATEMENT; }
std::string ForStatement::toString() const { return "ForStatement"; }
void ForStatement::accept(Visitor& v) { v.visit(this); }

NodeType ReturnStatement::getType() const { return NodeType::RETURN_STATEMENT; }
std::string ReturnStatement::toString() const { return "ReturnStatement"; }
void ReturnStatement::accept(Visitor& v) { v.visit(this); }

NodeType BreakStatement::getType() const { return NodeType::BREAK_STATEMENT; }
std::string BreakStatement::toString() const { return "BreakStatement"; }
void BreakStatement::accept(Visitor& v) { v.visit(this); }

NodeType ContinueStatement::getType() const { return NodeType::CONTINUE_STATEMENT; }
std::string ContinueStatement::toString() const { return "ContinueStatement"; }
void ContinueStatement::accept(Visitor& v) { v.visit(this); }

// Declarations
NodeType VariableDeclaration::getType() const { return NodeType::VARIABLE_DECLARATION; }
std::string VariableDeclaration::toString() const { return "VariableDeclaration"; }
void VariableDeclaration::accept(Visitor& v) { v.visit(this); }

NodeType FunctionDeclaration::getType() const { return NodeType::FUNCTION_DECLARATION; }
std::string FunctionDeclaration::toString() const { return "FunctionDeclaration"; }
void FunctionDeclaration::accept(Visitor& v) { v.visit(this); }

NodeType TypeAliasDeclaration::getType() const { return NodeType::TYPE_ALIAS_DECLARATION; }
std::string TypeAliasDeclaration::toString() const { return "TypeAliasDeclaration"; }
void TypeAliasDeclaration::accept(Visitor& v) { v.visit(this); }

// Out-of-line destructors to ensure vtables are generated
CallExpression::~CallExpression() = default;
MemberExpression::~MemberExpression() = default;
UnaryExpression::~UnaryExpression() = default;
BinaryExpression::~BinaryExpression() = default;
AssignmentExpression::~AssignmentExpression() = default;
BreakStatement::~BreakStatement() = default;
ContinueStatement::~ContinueStatement() = default;
ExpressionStatement::~ExpressionStatement() = default;
BlockStatement::~BlockStatement() = default;
IfStatement::~IfStatement() = default;
WhileStatement::~WhileStatement() = default;
ForStatement::~ForStatement() = default;
ReturnStatement::~ReturnStatement() = default;
VariableDeclaration::~VariableDeclaration() = default;
FunctionDeclaration::~FunctionDeclaration() = default;
TypeAliasDeclaration::~TypeAliasDeclaration() = default;

// --- Expression Nodes ---
CallExpression::CallExpression(SourceLocation loc, ExprPtr callee, std::vector<ExprPtr> arguments)
    : Expression(loc), callee(std::move(callee)), arguments(std::move(arguments)) {}

MemberExpression::MemberExpression(SourceLocation loc, ExprPtr object, ExprPtr property, bool computed)
    : Expression(loc), object(std::move(object)), property(std::move(property)), computed(computed) {}

UnaryExpression::UnaryExpression(SourceLocation loc, const vyn::token::Token& op, ExprPtr operand)
    : Expression(loc), op(op), operand(std::move(operand)) {}

BinaryExpression::BinaryExpression(SourceLocation loc, ExprPtr left, const vyn::token::Token& op, ExprPtr right)
    : Expression(loc), left(std::move(left)), op(op), right(std::move(right)) {}

AssignmentExpression::AssignmentExpression(SourceLocation loc, ExprPtr left, const vyn::token::Token& op, ExprPtr right)
    : Expression(loc), left(std::move(left)), op(op), right(std::move(right)) {}

// --- Statement Nodes ---
BreakStatement::BreakStatement(SourceLocation loc)
    : Statement(loc) {}

ContinueStatement::ContinueStatement(SourceLocation loc)
    : Statement(loc) {}

ExpressionStatement::ExpressionStatement(SourceLocation loc, ExprPtr expression)
    : Statement(loc), expression(std::move(expression)) {}

BlockStatement::BlockStatement(SourceLocation loc, std::vector<StmtPtr> body)
    : Statement(loc), body(std::move(body)) {}

IfStatement::IfStatement(SourceLocation loc, ExprPtr test, StmtPtr consequent, StmtPtr alternate)
    : Statement(loc), test(std::move(test)), consequent(std::move(consequent)), alternate(std::move(alternate)) {}

WhileStatement::WhileStatement(SourceLocation loc, ExprPtr test, StmtPtr body)
    : Statement(loc), test(std::move(test)), body(std::move(body)) {}

ForStatement::ForStatement(SourceLocation loc, NodePtr init, ExprPtr test, ExprPtr update, StmtPtr body)
    : Statement(loc), init(std::move(init)), test(std::move(test)), update(std::move(update)), body(std::move(body)) {}

ReturnStatement::ReturnStatement(SourceLocation loc, ExprPtr argument)
    : Statement(loc), argument(std::move(argument)) {}

// --- Declaration Nodes ---
VariableDeclaration::VariableDeclaration(SourceLocation loc, std::unique_ptr<Identifier> id, bool isConst, TypeNodePtr typeNode, ExprPtr init)
    : Declaration(loc), id(std::move(id)), isConst(isConst), typeNode(std::move(typeNode)), init(std::move(init)) {}

FunctionDeclaration::FunctionDeclaration(SourceLocation loc, std::unique_ptr<Identifier> id, std::vector<FunctionParameter> params, std::unique_ptr<BlockStatement> body, bool isAsync, TypeNodePtr returnTypeNode)
    : Declaration(loc), id(std::move(id)), params(std::move(params)), body(std::move(body)), isAsync(isAsync), returnTypeNode(std::move(returnTypeNode)) {}

TypeAliasDeclaration::TypeAliasDeclaration(SourceLocation loc, std::unique_ptr<Identifier> name, TypeNodePtr typeNode)
    : Declaration(loc), name(std::move(name)), typeNode(std::move(typeNode)) {}

// Module (was Program)
Module::Module(SourceLocation loc, std::vector<StmtPtr> body_stmts) 
    : Node(loc), body(std::move(body_stmts)) {}

NodeType Module::getType() const { 
    return NodeType::MODULE; 
}

std::string Module::toString() const {
    std::stringstream ss;
    ss << "Module:\\n"; // Escaped newline
    for (const auto& stmt : body) {
        if (stmt) {
            ss << stmt->toString(); // Assuming no indent needed based on prior changes
        }
    }
    return ss.str();
}

void Module::accept(Visitor& visitor) { 
    visitor.visit(this); 
}

// Identifier
Identifier::Identifier(SourceLocation loc, std::string n) 
    : Expression(loc), name(std::move(n)) {} 

NodeType Identifier::getType() const { 
    return NodeType::IDENTIFIER; 
}

std::string Identifier::toString() const {
    std::stringstream ss;
    ss << "Identifier(" << name << ")";
    return ss.str();
}

void Identifier::accept(Visitor& visitor) { 
    visitor.visit(this); 
}

// Literal Nodes
// IntegerLiteral (was IntLiteral)
IntegerLiteral::IntegerLiteral(SourceLocation loc, int64_t val) 
    : Expression(loc), value(val) {} 

NodeType IntegerLiteral::getType() const { 
    return NodeType::INTEGER_LITERAL; 
}

std::string IntegerLiteral::toString() const {
    std::stringstream ss;
    ss << "IntegerLiteral(" << value << ")"; 
    return ss.str();
}

void IntegerLiteral::accept(Visitor& visitor) { 
    visitor.visit(this); 
}

// FloatLiteral
FloatLiteral::FloatLiteral(SourceLocation loc, double val) 
    : Expression(loc), value(val) {} 

NodeType FloatLiteral::getType() const { 
    return NodeType::FLOAT_LITERAL; 
}

std::string FloatLiteral::toString() const {
    std::stringstream ss;
    ss << "FloatLiteral(" << value << ")";
    return ss.str();
}

void FloatLiteral::accept(Visitor& visitor) { 
    visitor.visit(this); 
}

// StringLiteral
StringLiteral::StringLiteral(SourceLocation loc, std::string val) 
    : Expression(loc), value(std::move(val)) {} 

NodeType StringLiteral::getType() const { 
    return NodeType::STRING_LITERAL; 
}

std::string StringLiteral::toString() const {
    std::stringstream ss;
    ss << "StringLiteral(\\\"" << value << "\\\")"; // Escaped quotes and backslash
    return ss.str();
}

void StringLiteral::accept(Visitor& visitor) { 
    visitor.visit(this); 
}

// BooleanLiteral (was BoolLiteral)
BooleanLiteral::BooleanLiteral(SourceLocation loc, bool val) 
    : Expression(loc), value(val) {} 

NodeType BooleanLiteral::getType() const { 
    return NodeType::BOOLEAN_LITERAL; 
}

std::string BooleanLiteral::toString() const {
    std::stringstream ss;
    ss << "BooleanLiteral(" << (value ? "true" : "false") << ")"; 
    return ss.str();
}

void BooleanLiteral::accept(Visitor& visitor) { 
    visitor.visit(this); 
}

// ArrayLiteralNode - Corrected from ArrayLiteral
ArrayLiteralNode::ArrayLiteralNode(SourceLocation loc, std::vector<ExprPtr> elems)
    : Expression(loc), elements(std::move(elems)) {} // Corrected constructor

std::string ArrayLiteralNode::toString() const { // Corrected signature (no indent)
    std::stringstream ss;
    ss << "ArrayLiteralNode([";
    for (size_t i = 0; i < elements.size(); ++i) {
        if (elements[i]) ss << elements[i]->toString(); else ss << "<null_element>";
        if (i < elements.size() - 1) ss << ", ";
    }
    ss << "])";
    return ss.str();
}

void ArrayLiteralNode::accept(Visitor& visitor) { // Corrected signature (Visitor&)
    visitor.visit(this);
}



// ObjectLiteral implementation

NodeType ObjectLiteral::getType() const {
    return NodeType::OBJECT_LITERAL_NODE;
}

std::string ObjectLiteral::toString() const {
    std::stringstream ss;
    ss << "ObjectLiteral({";
    bool first = true;
    for (const auto& prop : properties) {
        if (!first) ss << ", ";
        ss << prop.key->name << ": " << prop.value->toString();
        first = false;
    }
    ss << "})";
    return ss.str();
}

void ObjectLiteral::accept(Visitor& visitor) {
    visitor.visit(this);
}

// NilLiteral implementation
vyn::NilLiteral::NilLiteral(vyn::SourceLocation loc) : Expression(loc) {}

vyn::NodeType vyn::NilLiteral::getType() const {
    return vyn::NodeType::NIL_LITERAL;
}

std::string vyn::NilLiteral::toString() const {
    return "NilLiteral(nil)";
}

void vyn::NilLiteral::accept(vyn::Visitor& visitor) {
    visitor.visit(this);
}


// --- BorrowExprNode implementation ---
vyn::BorrowExprNode::BorrowExprNode(vyn::SourceLocation loc, vyn::ExprPtr expr, vyn::BorrowKind borrow_kind)
    : Expression(loc), kind(borrow_kind), expression(std::move(expr)) {}

std::string vyn::BorrowExprNode::toString() const {
    std::string prefix;
    switch (kind) {
        case BorrowKind::MUTABLE_BORROW:
            prefix = "borrow ";
            break;
        case BorrowKind::IMMUTABLE_VIEW:
            prefix = "view ";
            break;
        default: // Should not happen
            prefix = "unknown_borrow_kind ";
            break;
    }
    if (expression) {
        return prefix + expression->toString();
    }
    return prefix + "nullptr";
}

void vyn::BorrowExprNode::accept(vyn::Visitor& visitor) {
    visitor.visit(this);
}


// --- TypeNode implementation ---
vyn::TypeNode::TypeNode(vyn::SourceLocation loc, vyn::TypeNode::TypeCategory category, bool dataIsConst, bool isOptional)
    : Node(loc), category(category), loc(loc), dataIsConst(dataIsConst), isOptional(isOptional), isPointer(false) {
        // Ensure all relevant members are initialized, especially pointers to nullptr if not set by factories
        // For IDENTIFIER
        // this->name = nullptr; // This will be set by newIdentifier
        // genericArguments is default initialized

        // For TUPLE
        // tupleElementTypes is default initialized

        // For OWNERSHIP_WRAPPED
        // this->wrappedType = nullptr; // This will be set by newOwnershipWrapped

        // For ARRAY
        // this->arrayElementType = nullptr; // This will be set by newArray
        this->arraySizeExpression = nullptr;

        // For FUNCTION_SIGNATURE
        // functionParameters is default initialized
        // this->functionReturnType = nullptr; // This will be set by newFunctionSignature
    }

// Static factory method implementations
vyn::TypeNodePtr vyn::TypeNode::newIdentifier(vyn::SourceLocation loc, vyn::IdentifierPtr identifierName, std::vector<vyn::TypeNodePtr> genericArgs, bool dataIsConst, bool isOptional) {
        auto node = std::make_unique<TypeNode>(loc, TypeCategory::IDENTIFIER, dataIsConst, isOptional);
        node->name = std::move(identifierName);
        node->genericArguments = std::move(genericArgs);
        return node;
    }

vyn::TypeNodePtr vyn::TypeNode::newTuple(vyn::SourceLocation loc, std::vector<vyn::TypeNodePtr> memberTypes, bool dataIsConst, bool isOptional) {
    auto node = std::make_unique<TypeNode>(loc, TypeCategory::TUPLE, dataIsConst, isOptional);
    node->tupleElementTypes = std::move(memberTypes);
    return node;
}

vyn::TypeNodePtr vyn::TypeNode::newArray(vyn::SourceLocation loc, vyn::TypeNodePtr elementType, vyn::ExprPtr sizeExpression, bool dataIsConst, bool isOptional) {
    auto node = std::make_unique<TypeNode>(loc, TypeCategory::ARRAY, dataIsConst, isOptional);
    node->arrayElementType = std::move(elementType);
    node->arraySizeExpression = std::move(sizeExpression);
    return node;
}

vyn::TypeNodePtr vyn::TypeNode::newFunctionSignature(vyn::SourceLocation loc, std::vector<vyn::TypeNodePtr> params, vyn::TypeNodePtr retType, bool dataIsConst, bool isOptional) {
    auto node = std::make_unique<TypeNode>(loc, TypeCategory::FUNCTION_SIGNATURE, dataIsConst, isOptional);
    node->functionParameters = std::move(params);
    node->functionReturnType = std::move(retType);
    return node;
}

vyn::TypeNodePtr vyn::TypeNode::newOwnershipWrapped(vyn::SourceLocation loc, vyn::OwnershipKind ownKind, vyn::TypeNodePtr wrapped, bool dataIsConst, bool isOptional) {
    auto node = std::make_unique<TypeNode>(loc, TypeCategory::OWNERSHIP_WRAPPED, dataIsConst, isOptional);
    node->ownership = ownKind;
    node->wrappedType = std::move(wrapped);
    return node;
}

std::string vyn::TypeNode::toString() const {
    std::string str;
    if (dataIsConst) {
        str += "const ";
    }

    switch (category) {
        case TypeCategory::IDENTIFIER:
            if (name) { // Check if name is not nullptr
                str += name->name;
            } else {
                str += "<unnamed_identifier_type>";
            }
            if (!genericArguments.empty()) {
                    str += "<";
                for (size_t i = 0; i < genericArguments.size(); ++i) {
                    str += genericArguments[i]->toString();
                    if (i < genericArguments.size() - 1) {
                        str += ", ";
                    }
                }
                str += ">";
            }
            break;
        case TypeCategory::TUPLE:
            str += "(";
            for (size_t i = 0; i < tupleElementTypes.size(); ++i) {
                if (tupleElementTypes[i]) str += tupleElementTypes[i]->toString();
                else str += "<invalid_tuple_element_type>";
                if (i < tupleElementTypes.size() - 1) {
                    str += ", ";
                }
            }
            str += ")";
            break;
        case TypeCategory::ARRAY:
            str += "[";
            str += arrayElementType->toString();
            if (arraySizeExpression) {
                str += "; ";
                // Assuming ExpressionNode has a toString() method
                str += arraySizeExpression->toString(); 
            }
            str += "]";
            break;
        case TypeCategory::FUNCTION_SIGNATURE:
            str += "fn(";
            for (size_t i = 0; i < functionParameters.size(); ++i) {
                if (functionParameters[i]) str += functionParameters[i]->toString();
                else str += "<invalid_param_type>";
                if (i < functionParameters.size() - 1) {
                    str += ", ";
                }
            }
            str += ")";
                if (functionReturnType) {
                    str += " -> " + functionReturnType->toString();
                } else {
                    str += " -> <void_or_inferred_return_type>"; // Or handle as error/specific representation
                }
                break;
            case TypeCategory::OWNERSHIP_WRAPPED:
                switch (ownership) {
                    case OwnershipKind::MY:
                        str += "my<";
                        break;
                    case OwnershipKind::OUR:
                        str += "our<";
                        break;
                    case OwnershipKind::THEIR:
                        str += "their<";
                        break;
                    case OwnershipKind::PTR:
                        str += "ptr<";
                        break;
                }
                str += wrappedType->toString() + ">";
                break;
        }

        if (isOptional) {
            str += "?";
        }
        if (isPointer) { // Add to string representation
            str += "*";
        }
        return str;
}

void vyn::TypeNode::accept(vyn::Visitor& visitor) {
    visitor.visit(this);
}


// --- New Declarations ---

// FieldDeclaration
vyn::FieldDeclaration::FieldDeclaration(vyn::SourceLocation loc, std::unique_ptr<vyn::Identifier> n, vyn::TypeNodePtr tn, vyn::ExprPtr init, bool is_mut)
    : Declaration(loc), name(std::move(n)), typeNode(std::move(tn)), initializer(std::move(init)), isMutable(is_mut) {}

vyn::NodeType vyn::FieldDeclaration::getType() const {
    return vyn::NodeType::FIELD_DECLARATION;
}

std::string vyn::FieldDeclaration::toString() const {
    std::stringstream ss;
    ss << "FieldDeclaration(" << (isMutable ? "var " : "let ") << name->toString();
    ss << ": " << typeNode->toString();
    if (initializer) {
        ss << " = " << initializer->toString();
    }
    ss << ")";
    return ss.str();
}

void vyn::FieldDeclaration::accept(vyn::Visitor& visitor) {
    visitor.visit(this);
}

// StructDeclaration
vyn::StructDeclaration::StructDeclaration(vyn::SourceLocation loc, std::unique_ptr<vyn::Identifier> n, std::vector<std::unique_ptr<vyn::GenericParamNode>> gp, std::vector<std::unique_ptr<vyn::FieldDeclaration>> f)
    : Declaration(loc), name(std::move(n)), genericParams(std::move(gp)), fields(std::move(f)) {}

vyn::NodeType vyn::StructDeclaration::getType() const {
    return vyn::NodeType::STRUCT_DECLARATION;
}

std::string vyn::StructDeclaration::toString() const {
    std::stringstream ss;
    ss << "StructDeclaration(" << name->toString();
    if (!genericParams.empty()) {
        ss << "<";
        for (size_t i = 0; i < genericParams.size(); ++i) {
            ss << genericParams[i]->toString();
            if (i < genericParams.size() - 1) ss << ", ";
        }
        ss << ">";
    }
    ss << " {\\\\n"; // Escaped newline
    for (const auto& field : fields) {
        ss << "  " << field->toString() << ";\\\\n"; // Escaped newline
    }
    ss << "})";
    return ss.str();
}

void vyn::StructDeclaration::accept(vyn::Visitor& visitor) {
    visitor.visit(this);
}

// ClassDeclaration
vyn::ClassDeclaration::ClassDeclaration(vyn::SourceLocation loc, std::unique_ptr<vyn::Identifier> n, std::vector<std::unique_ptr<vyn::GenericParamNode>> gp, std::vector<vyn::DeclPtr> m)
    : Declaration(loc), name(std::move(n)), genericParams(std::move(gp)), members(std::move(m)) {}

vyn::NodeType vyn::ClassDeclaration::getType() const {
    return vyn::NodeType::CLASS_DECLARATION;
}

std::string vyn::ClassDeclaration::toString() const {
    std::stringstream ss;
    ss << "ClassDeclaration(" << name->toString();
    if (!genericParams.empty()) {
        ss << "<";
        for (size_t i = 0; i < genericParams.size(); ++i) {
            ss << genericParams[i]->toString();
            if (i < genericParams.size() - 1) ss << ", ";
        }
        ss << ">";
    }
    ss << " {\\\\n"; // Escaped newline
    for (const auto& member : members) {
        ss << "  " << member->toString() << ";\\\\n"; // Escaped newline
    }
    ss << "})";
    return ss.str();
}

void vyn::ClassDeclaration::accept(vyn::Visitor& visitor) {
    visitor.visit(this);
}

// ImplDeclaration
vyn::ImplDeclaration::ImplDeclaration(vyn::SourceLocation loc, vyn::TypeNodePtr selfType, std::vector<std::unique_ptr<vyn::FunctionDeclaration>> methods, std::unique_ptr<vyn::Identifier> name, std::vector<std::unique_ptr<vyn::GenericParamNode>> genericParams, vyn::TypeNodePtr traitType)
    : Declaration(loc), name(std::move(name)), genericParams(std::move(genericParams)), selfType(std::move(selfType)), traitType(std::move(traitType)), methods(std::move(methods)) {}

vyn::NodeType vyn::ImplDeclaration::getType() const {
    return vyn::NodeType::IMPL_DECLARATION;
}

std::string vyn::ImplDeclaration::toString() const {
    std::stringstream ss;
    ss << "ImplDeclaration(";
    if (name) {
        ss << name->toString() << " ";
    }
    if (!genericParams.empty()) {
        ss << "<";
        for (size_t i = 0; i < genericParams.size(); ++i) {
            ss << genericParams[i]->toString();
            if (i < genericParams.size() - 1) ss << ", ";
        }
        ss << "> ";
    }
    if (traitType) { // Changed from traitTypeNode
        ss << traitType->toString() << " for "; // Changed from traitTypeNode
    }
    ss << selfType->toString() << " {\\\\\\\\n"; // Changed from selfTypeNode, Escaped newline
    for (const auto& method : methods) {
        ss << "  " << method->toString() << ";\\\\n"; // Escaped newline
    }
    ss << "})";
    return ss.str();
}

void vyn::ImplDeclaration::accept(vyn::Visitor& visitor) {
    visitor.visit(this);
}

// EnumVariantNode
vyn::EnumVariantNode::EnumVariantNode(vyn::SourceLocation loc, std::unique_ptr<vyn::Identifier> n, std::vector<vyn::TypeNodePtr> tns)
    : Node(loc), name(std::move(n)), associatedTypes(std::move(tns)) {} // Inherit from Node, use associatedTypes

vyn::NodeType vyn::EnumVariantNode::getType() const {
    return vyn::NodeType::ENUM_VARIANT;
}

std::string vyn::EnumVariantNode::toString() const {
    std::stringstream ss;
    ss << "EnumVariantNode(" << name->toString();
    if (!associatedTypes.empty()) { // Changed from typeNodes
        ss << "(";
        for (size_t i = 0; i < associatedTypes.size(); ++i) { // Changed from typeNodes
            ss << associatedTypes[i]->toString(); // Changed from typeNodes
            if (i < associatedTypes.size() - 1) ss << ", ";
        }
        ss << ")";
    }
    ss << ")";
    return ss.str();
}

void vyn::EnumVariantNode::accept(vyn::Visitor& visitor) {
    visitor.visit(this);
}

// EnumDeclaration
vyn::EnumDeclaration::EnumDeclaration(vyn::SourceLocation loc, std::unique_ptr<vyn::Identifier> n, std::vector<std::unique_ptr<vyn::GenericParamNode>> gp, std::vector<std::unique_ptr<vyn::EnumVariantNode>> vars)
    : Declaration(loc), name(std::move(n)), genericParams(std::move(gp)), variants(std::move(vars)) {}

vyn::NodeType vyn::EnumDeclaration::getType() const {
    return vyn::NodeType::ENUM_DECLARATION;
}

std::string vyn::EnumDeclaration::toString() const {
    std::stringstream ss;
    ss << "EnumDeclaration(" << name->toString();
    if (!genericParams.empty()) {
        ss << "<";
        for (size_t i = 0; i < genericParams.size(); ++i) {
            ss << genericParams[i]->toString();
            if (i < genericParams.size() - 1) ss << ", ";
        }
        ss << ">";
    }
    ss << " {\\\\n"; // Escaped newline
    for (const auto& variant : variants) {
        ss << "  " << variant->toString() << ",\\\\n"; // Escaped newline
    }
    ss << "})";
    return ss.str();
}

void vyn::EnumDeclaration::accept(vyn::Visitor& visitor) {
    visitor.visit(this);
}

// GenericParamNode
vyn::GenericParamNode::GenericParamNode(vyn::SourceLocation loc, std::unique_ptr<vyn::Identifier> n, std::vector<vyn::TypeNodePtr> b)
    : Node(loc), name(std::move(n)), bounds(std::move(b)) {}

vyn::NodeType vyn::GenericParamNode::getType() const {
    return vyn::NodeType::GENERIC_PARAMETER;
}

std::string vyn::GenericParamNode::toString() const {
    std::stringstream ss;
    ss << "GenericParamNode(" << name->toString();
    if (!bounds.empty()) {
        ss << ": ";
        for (size_t i = 0; i < bounds.size(); ++i) {
            ss << bounds[i]->toString();
            if (i < bounds.size() - 1) ss << " + ";
        }
    }
    ss << ")";
    return ss.str();
}

void vyn::GenericParamNode::accept(vyn::Visitor& visitor) {
    visitor.visit(this);
}

// Implementations for TemplateDeclarationNode
// Ensure TemplateDeclarationNode is fully defined in ast.hpp before this point.
// The class definition should not be a forward declaration only.

vyn::TemplateDeclarationNode::TemplateDeclarationNode(vyn::SourceLocation loc, std::unique_ptr<vyn::Identifier> name, std::vector<std::unique_ptr<vyn::GenericParamNode>> generic_params, vyn::DeclPtr body)
    : Declaration(loc), name(std::move(name)), genericParams(std::move(generic_params)), body(std::move(body)) {}

vyn::NodeType vyn::TemplateDeclarationNode::getType() const {
    return vyn::NodeType::TEMPLATE_DECLARATION;
}

std::string vyn::TemplateDeclarationNode::toString() const {
    std::string str = "template " + name->name + "<"; // Changed name->value to name->name
    for (size_t i = 0; i < genericParams.size(); ++i) {
        str += genericParams[i]->name->name; // Changed genericParams[i]->name->value to genericParams[i]->name->name
        if (!genericParams[i]->bounds.empty()) {
            str += ": ";
            for (size_t j = 0; j < genericParams[i]->bounds.size(); ++j) {
                str += genericParams[i]->bounds[j]->toString();
                if (j < genericParams[i]->bounds.size() - 1) {
                    str += " + ";
                }
            }
        }
        if (i < genericParams.size() - 1) {
            str += ", ";
        }
    }
    str += "> {\\n";
    if (body) {
        str += body->toString(); 
    }
    str += "\\n}";
    return str;
}

void vyn::TemplateDeclarationNode::accept(vyn::Visitor& visitor) {
    visitor.visit(this);
}

// --- AddrOfExpression implementation ---
AddrOfExpression::AddrOfExpression(SourceLocation loc, ExprPtr location)
    : Expression(loc), location(std::move(location)) {}

NodeType AddrOfExpression::getType() const { return NodeType::CALL_EXPRESSION; }
std::string AddrOfExpression::toString() const { return "addr(" + (location ? location->toString() : "<null>") + ")"; }
void AddrOfExpression::accept(Visitor& visitor) { visitor.visit(this); }

// --- FromIntToLocExpression implementation ---
FromIntToLocExpression::FromIntToLocExpression(SourceLocation loc, ExprPtr address)
    : Expression(loc), address(std::move(address)) {}

NodeType FromIntToLocExpression::getType() const { return NodeType::CALL_EXPRESSION; }
std::string FromIntToLocExpression::toString() const { return "from(" + (address ? address->toString() : "<null>") + ")"; }
void FromIntToLocExpression::accept(Visitor& visitor) { visitor.visit(this); }

// --- PointerDerefExpression implementation ---
vyn::PointerDerefExpression::PointerDerefExpression(SourceLocation loc, ExprPtr pointer)
    : Expression(loc), pointer(std::move(pointer)) {}

vyn::NodeType vyn::PointerDerefExpression::getType() const {
    return vyn::NodeType::CALL_EXPRESSION; // Or a new NodeType if desired
}

std::string vyn::PointerDerefExpression::toString() const {
    return "at(" + (pointer ? pointer->toString() : "<null>") + ")";
}

void vyn::PointerDerefExpression::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- ObjectLiteralNode implementation ---
vyn::ObjectLiteralNode::ObjectLiteralNode(SourceLocation loc, std::vector<std::pair<std::string, ExprPtr>> properties)
    : Expression(loc), properties(std::move(properties)) {}

vyn::NodeType vyn::ObjectLiteralNode::getType() const {
    return vyn::NodeType::OBJECT_LITERAL_NODE;
}

void vyn::ObjectLiteralNode::accept(Visitor& visitor) {
    (void)visitor;
}

std::string vyn::ObjectLiteralNode::toString() const {
    std::string result = "{";
    bool first = true;
    for (const auto& prop : properties) {
        if (!first) result += ", ";
        result += prop.first + ": " + (prop.second ? prop.second->toString() : "<null>");
        first = false;
    }
    return result + "}";
}

} // namespace vyn