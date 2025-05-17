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
namespace ast {

// ObjectLiteral destructor
ObjectLiteral::~ObjectLiteral() = default;

// ObjectLiteral constructor implementation
ObjectLiteral::ObjectLiteral(SourceLocation loc, std::vector<ObjectProperty> properties)
    : Expression(loc), properties(std::move(properties)) {}

NodeType ObjectLiteral::getType() const {
    return NodeType::OBJECT_LITERAL_NODE;
}

std::string ObjectLiteral::toString() const {
    std::stringstream ss;
    ss << "{\\";
    for (size_t i = 0; i < properties.size(); ++i) {
        ss << properties[i].key->toString() << ": " << properties[i].value->toString();
        if (i < properties.size() - 1) {
            ss << ", ";
        }
    }
    ss << "}";
    return ss.str();
}

void ObjectLiteral::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- IntegerLiteral ---
IntegerLiteral::IntegerLiteral(SourceLocation loc, int64_t val)
    : Expression(loc), value(val) {}

NodeType IntegerLiteral::getType() const {
    return NodeType::INTEGER_LITERAL;
}

std::string IntegerLiteral::toString() const {
    return std::to_string(value);
}

void IntegerLiteral::accept(Visitor& visitor) {
    visitor.visit(this);
}

IntegerLiteral::~IntegerLiteral() = default; // Added to ensure typeinfo is generated

// --- FloatLiteral ---
FloatLiteral::FloatLiteral(SourceLocation loc, double val)
    : Expression(loc), value(val) {}

NodeType FloatLiteral::getType() const {
    return NodeType::FLOAT_LITERAL;
}

std::string FloatLiteral::toString() const {
    return std::to_string(value);
}

void FloatLiteral::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- BooleanLiteral ---
BooleanLiteral::BooleanLiteral(SourceLocation loc, bool val)
    : Expression(loc), value(val) {}

NodeType BooleanLiteral::getType() const {
    return NodeType::BOOLEAN_LITERAL;
}

std::string BooleanLiteral::toString() const {
    return value ? "true" : "false";
}

void BooleanLiteral::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- NilLiteral ---
NilLiteral::NilLiteral(SourceLocation loc)
    : Expression(loc) {}

NodeType NilLiteral::getType() const {
    return NodeType::NIL_LITERAL;
}

std::string NilLiteral::toString() const {
    return "nil";
}

void NilLiteral::accept(Visitor& visitor) {
    visitor.visit(this);
}

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

BlockStatement::BlockStatement(SourceLocation loc, std::vector<StmtPtr> body_stmts) // Renamed body to body_stmts to avoid conflict
    : Statement(loc), body(std::move(body_stmts)) {}

IfStatement::IfStatement(SourceLocation loc, ExprPtr test, StmtPtr consequent, StmtPtr alternate)
    : Statement(loc), test(std::move(test)), consequent(std::move(consequent)), alternate(std::move(alternate)) {}

WhileStatement::WhileStatement(SourceLocation loc, ExprPtr test, StmtPtr body_stmt) // Renamed body to body_stmt
    : Statement(loc), test(std::move(test)), body(std::move(body_stmt)) {}

ForStatement::ForStatement(SourceLocation loc, NodePtr init_node, ExprPtr test_expr, ExprPtr update_expr, StmtPtr body_stmt) // Renamed parameters
    : Statement(loc), init(std::move(init_node)), test(std::move(test_expr)), update(std::move(update_expr)), body(std::move(body_stmt)) {}

ReturnStatement::ReturnStatement(SourceLocation loc, ExprPtr argument_expr) // Renamed argument
    : Statement(loc), argument(std::move(argument_expr)) {}

// --- Declaration Nodes ---
VariableDeclaration::VariableDeclaration(SourceLocation loc, std::unique_ptr<Identifier> id_ptr, bool isConst_val, TypeNodePtr typeNode_ptr, ExprPtr init_expr) // Renamed parameters
    : Declaration(loc), id(std::move(id_ptr)), isConst(isConst_val), typeNode(std::move(typeNode_ptr)), init(std::move(init_expr)) {}

FunctionDeclaration::FunctionDeclaration(SourceLocation loc, std::unique_ptr<Identifier> id_ptr, std::vector<FunctionParameter> params_vec, std::unique_ptr<BlockStatement> body_stmt, bool isAsync_val, TypeNodePtr returnTypeNode_ptr) // Renamed parameters
    : Declaration(loc), id(std::move(id_ptr)), params(std::move(params_vec)), body(std::move(body_stmt)), isAsync(isAsync_val), returnTypeNode(std::move(returnTypeNode_ptr)) {}

TypeAliasDeclaration::TypeAliasDeclaration(SourceLocation loc, std::unique_ptr<Identifier> name_ptr, TypeNodePtr typeNode_ptr) // Renamed parameters
    : Declaration(loc), name(std::move(name_ptr)), typeNode(std::move(typeNode_ptr)) {}

// Module (was Program)
Module::Module(SourceLocation loc, std::vector<StmtPtr> body_stmts) 
    : Node(loc), body(std::move(body_stmts)) {}

NodeType Module::getType() const { 
    return NodeType::MODULE; 
}

std::string Module::toString() const {
    std::stringstream ss;
    ss << "Module:\\\\n";
    for (const auto& stmt : body) {
        if (stmt) {
            ss << stmt->toString();
        }
    }
    return ss.str();
}

void Module::accept(Visitor& visitor) { 
    visitor.visit(this); 
}

// Identifier
Identifier::Identifier(SourceLocation loc, std::string n_val) // Renamed n to n_val
    : Expression(loc), name(std::move(n_val)) {} 

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

// --- StringLiteral ---
StringLiteral::StringLiteral(SourceLocation loc, std::string val) 
    : Expression(loc), value(std::move(val)) {} 

NodeType StringLiteral::getType() const { 
    return NodeType::STRING_LITERAL; 
}

std::string StringLiteral::toString() const {
    std::string s = this->value;
    std::string result = "\""; // Start with a double quote character
    for (char c : s) {
        switch (c) {
            case '\"': result += "\\\""; break; // Escape double quote
            case '\\': result += "\\\\"; break; // Escape backslash
            case '\n': result += "\\n"; break;   // Escape newline
            case '\r': result += "\\r"; break;   // Escape carriage return
            case '\t': result += "\\t"; break;   // Escape tab
            // Add other common escapes if necessary
            default:   result += c;
        }
    }
    result += "\""; // End with a double quote character
    return result;
}

void StringLiteral::accept(Visitor& visitor) { 
    visitor.visit(this); 
}

// --- ArrayLiteralNode ---
ArrayLiteralNode::ArrayLiteralNode(SourceLocation loc_param, std::vector<ExprPtr> elements_param)
    : Expression(loc_param), elements(std::move(elements_param)) {}

void ArrayLiteralNode::accept(Visitor& visitor) {
    visitor.visit(this);
}

std::string ArrayLiteralNode::toString() const {
    std::stringstream ss;
    ss << "ArrayLiteralNode([";
    for (size_t i = 0; i < elements.size(); ++i) {
        if (elements[i]) ss << elements[i]->toString(); else ss << "<null_element>";
        if (i < elements.size() - 1) ss << ", ";
    }
    ss << "])";
    return ss.str();
}

// --- BorrowExprNode ---
BorrowExprNode::BorrowExprNode(SourceLocation loc_param, ExprPtr expression_param, BorrowKind kind_param)
    : Expression(loc_param), expression(std::move(expression_param)), kind(kind_param) {}

void BorrowExprNode::accept(Visitor& visitor) {
    visitor.visit(this);
}

std::string BorrowExprNode::toString() const {
    std::string prefix_str; // Renamed prefix to prefix_str
    switch (kind) {
        case BorrowKind::MUTABLE_BORROW:
            prefix_str = "borrow ";
            break;
        case BorrowKind::IMMUTABLE_VIEW:
            prefix_str = "view ";
            break;
        default: 
            prefix_str = "unknown_borrow_kind ";
            break;
    }
    if (expression) {
        return prefix_str + expression->toString();
    }
    return prefix_str + "nullptr";
}

// --- TypeNode ---
// Constructor implementation (if not already correct)
TypeNode::TypeNode(SourceLocation loc_param, TypeNode::Category category_param, bool dataIsConst_param, bool isOptional_param) // Ensure TypeNode::Category is used
    : Node(loc_param), category(category_param), loc(loc_param), dataIsConst(dataIsConst_param), isOptional(isOptional_param) {
    // Initialize other members based on category if necessary, or rely on factory methods
}

TypeNodePtr TypeNode::newIdentifier(SourceLocation loc_param, IdentifierPtr identifierName_param, std::vector<TypeNodePtr> genericArgs_param, bool dataIsConst_param, bool isOptional_param) {
        auto node = std::make_unique<TypeNode>(loc_param, TypeNode::Category::IDENTIFIER, dataIsConst_param, isOptional_param); // Ensure TypeNode::Category is used
        node->name = std::move(identifierName_param);
        node->genericArguments = std::move(genericArgs_param);
        return node;
    }

TypeNodePtr TypeNode::newTuple(SourceLocation loc_param, std::vector<TypeNodePtr> memberTypes_param, bool dataIsConst_param, bool isOptional_param) {
    auto node = std::make_unique<TypeNode>(loc_param, TypeNode::Category::TUPLE, dataIsConst_param, isOptional_param); // Ensure TypeNode::Category is used
    node->tupleElementTypes = std::move(memberTypes_param);
    return node;
}

TypeNodePtr TypeNode::newArray(SourceLocation loc_param, TypeNodePtr elementType_param, ExprPtr sizeExpression_param, bool dataIsConst_param, bool isOptional_param) {
    auto node = std::make_unique<TypeNode>(loc_param, TypeNode::Category::ARRAY, dataIsConst_param, isOptional_param); // Ensure TypeNode::Category is used
    node->arrayElementType = std::move(elementType_param);
    node->arraySizeExpression = std::move(sizeExpression_param);
    return node;
}

TypeNodePtr TypeNode::newFunctionSignature(SourceLocation loc_param, std::vector<TypeNodePtr> params_param, TypeNodePtr retType_param, bool dataIsConst_param, bool isOptional_param) {
    auto node = std::make_unique<TypeNode>(loc_param, TypeNode::Category::FUNCTION_SIGNATURE, dataIsConst_param, isOptional_param); // Ensure TypeNode::Category is used
    node->functionParameters = std::move(params_param);
    node->functionReturnType = std::move(retType_param);
    return node;
}

TypeNodePtr TypeNode::newOwnershipWrapped(SourceLocation loc_param, OwnershipKind ownKind_param, TypeNodePtr wrapped_param, bool dataIsConst_param, bool isOptional_param) {
    auto node = std::make_unique<TypeNode>(loc_param, TypeNode::Category::OWNERSHIP_WRAPPED, dataIsConst_param, isOptional_param); // Ensure TypeNode::Category is used
    node->ownership = ownKind_param;
    node->wrappedType = std::move(wrapped_param);
    return node;
}

std::string TypeNode::toString() const {
    std::string str_val; // Renamed str to str_val
    if (this->dataIsConst) { // Explicit this->
        str_val += "const ";
    }

    switch (this->category) { // Explicit this->
        case TypeNode::Category::IDENTIFIER: // Ensure TypeNode::Category is used
            if (this->name) { 
                str_val += this->name->name;
            } else {
                str_val += "<unnamed_identifier_type>";
            }
            if (!this->genericArguments.empty()) {
                    str_val += "<";
                for (size_t i = 0; i < this->genericArguments.size(); ++i) {
                    if (this->genericArguments[i]) str_val += this->genericArguments[i]->toString(); else str_val += "<null_gen_arg>";
                    if (i < this->genericArguments.size() - 1) {
                        str_val += ", ";
                    }
                }
                str_val += ">";
            }
            break;
        case TypeNode::Category::TUPLE: // Ensure TypeNode::Category is used
            str_val += "(";
            for (size_t i = 0; i < this->tupleElementTypes.size(); ++i) {
                if (this->tupleElementTypes[i]) str_val += this->tupleElementTypes[i]->toString();
                else str_val += "<invalid_tuple_element_type>";
                if (i < this->tupleElementTypes.size() - 1) {
                    str_val += ", ";
                }
            }
            str_val += ")";
            break;
        case TypeNode::Category::ARRAY: // Ensure TypeNode::Category is used
            str_val += "[";
            if (this->arrayElementType) str_val += this->arrayElementType->toString(); else str_val += "<invalid_array_element_type>";
            if (this->arraySizeExpression) {
                str_val += "; ";
                str_val += this->arraySizeExpression->toString(); 
            }
            str_val += "]";
            break;
        case TypeNode::Category::FUNCTION_SIGNATURE: // Ensure TypeNode::Category is used
            str_val += "fn(";
            for (size_t i = 0; i < this->functionParameters.size(); ++i) {
                if (this->functionParameters[i]) str_val += this->functionParameters[i]->toString();
                else str_val += "<invalid_param_type>";
                if (i < this->functionParameters.size() - 1) {
                    str_val += ", ";
                }
            }
            str_val += ")";
            if (this->functionReturnType) {
                str_val += " -> " + this->functionReturnType->toString();
            } else {
                str_val += " -> <void_or_inferred_return_type>";
            }
            break;
        case TypeNode::Category::OWNERSHIP_WRAPPED: // Ensure TypeNode::Category is used
            switch (this->ownership) {
                case OwnershipKind::MY: str_val += "my<"; break;
                case OwnershipKind::OUR: str_val += "our<"; break;
                case OwnershipKind::THEIR: str_val += "their<"; break;
                case OwnershipKind::PTR: str_val += "ptr<"; break;
            }
            if (this->wrappedType) str_val += this->wrappedType->toString() + ">"; else str_val += "<invalid_wrapped_type>>";
            break;
        default: // Added default case
            str_val += "<unknown_type_category>";
            break;
    }

    if (this->isOptional) { // Explicit this->
        str_val += "?";
    }
    if (this->isPointer) { // Explicit this->
        str_val += "*";
    }
    return str_val;
}

void TypeNode::accept(Visitor& visitor) {
    visitor.visit(this);
}


// --- New Declarations ---

// FieldDeclaration
FieldDeclaration::FieldDeclaration(SourceLocation loc_param, std::unique_ptr<Identifier> n_param, TypeNodePtr tn_param, ExprPtr init_param, bool is_mut_param) // Renamed params
    : Declaration(loc_param), name(std::move(n_param)), typeNode(std::move(tn_param)), initializer(std::move(init_param)), isMutable(is_mut_param) {}

NodeType FieldDeclaration::getType() const {
    return NodeType::FIELD_DECLARATION;
}

std::string FieldDeclaration::toString() const {
    std::stringstream ss;
    ss << "FieldDeclaration(" << (this->isMutable ? "var " : "let ") << this->name->toString(); // Explicit this->
    if (this->typeNode) ss << ": " << this->typeNode->toString(); else ss << ": <no_type_node>"; // Check typeNode
    if (this->initializer) { // Explicit this->
        ss << " = " << this->initializer->toString();
    }
    ss << ")";
    return ss.str();
}

void FieldDeclaration::accept(Visitor& visitor) {
    visitor.visit(this);
}

// StructDeclaration
StructDeclaration::StructDeclaration(SourceLocation loc_param, std::unique_ptr<Identifier> n_param, std::vector<std::unique_ptr<GenericParamNode>> gp_param, std::vector<std::unique_ptr<FieldDeclaration>> f_param) // Renamed params
    : Declaration(loc_param), name(std::move(n_param)), genericParams(std::move(gp_param)), fields(std::move(f_param)) {}

NodeType StructDeclaration::getType() const {
    return NodeType::STRUCT_DECLARATION;
}

std::string StructDeclaration::toString() const {
    std::stringstream ss;
    ss << "StructDeclaration(" << this->name->toString(); // Explicit this->
    if (!this->genericParams.empty()) { // Explicit this->
        ss << "<";
        for (size_t i = 0; i < this->genericParams.size(); ++i) {
            if (this->genericParams[i]) ss << this->genericParams[i]->toString(); else ss << "<null_gen_param>";
            if (i < this->genericParams.size() - 1) ss << ", ";
        }
        ss << ">";
    }
    ss << " {\\\\n"; 
    for (const auto& field_item : this->fields) { // Explicit this->, renamed field to field_item
        if (field_item) ss << "  " << field_item->toString() << ";\\\\n"; 
    }
    ss << "})";
    return ss.str();
}

void StructDeclaration::accept(Visitor& visitor) {
    visitor.visit(this);
}

// ClassDeclaration
ClassDeclaration::ClassDeclaration(SourceLocation loc_param, std::unique_ptr<Identifier> n_param, std::vector<std::unique_ptr<GenericParamNode>> gp_param, std::vector<DeclPtr> m_param) // Renamed params
    : Declaration(loc_param), name(std::move(n_param)), genericParams(std::move(gp_param)), members(std::move(m_param)) {}

NodeType ClassDeclaration::getType() const {
    return NodeType::CLASS_DECLARATION;
}

std::string ClassDeclaration::toString() const {
    std::stringstream ss;
    ss << "ClassDeclaration(" << this->name->toString(); // Explicit this->
    if (!this->genericParams.empty()) { // Explicit this->
        ss << "<";
        for (size_t i = 0; i < this->genericParams.size(); ++i) {
           if (this->genericParams[i]) ss << this->genericParams[i]->toString(); else ss << "<null_gen_param>";
            if (i < this->genericParams.size() - 1) ss << ", ";
        }
        ss << ">";
    }
    ss << " {\\\\n"; 
    for (const auto& member_item : this->members) { // Explicit this->, renamed member to member_item
        if (member_item) ss << "  " << member_item->toString() << ";\\\\n"; 
    }
    ss << "})";
    return ss.str();
}

void ClassDeclaration::accept(Visitor& visitor) {
    visitor.visit(this);
}

// ImplDeclaration
ImplDeclaration::ImplDeclaration(SourceLocation loc_param, TypeNodePtr selfType_param, std::vector<std::unique_ptr<FunctionDeclaration>> methods_param, std::unique_ptr<Identifier> name_param, std::vector<std::unique_ptr<GenericParamNode>> genericParams_param, TypeNodePtr traitType_param) // Renamed params
    : Declaration(loc_param), name(std::move(name_param)), genericParams(std::move(genericParams_param)), selfType(std::move(selfType_param)), traitType(std::move(traitType_param)), methods(std::move(methods_param)) {}

NodeType ImplDeclaration::getType() const {
    return NodeType::IMPL_DECLARATION;
}

std::string ImplDeclaration::toString() const {
    std::stringstream ss;
    ss << "ImplDeclaration(";
    if (this->name) { // Explicit this->
        ss << this->name->toString() << " ";
    }
    if (!this->genericParams.empty()) { // Explicit this->
        ss << "<";
        for (size_t i = 0; i < this->genericParams.size(); ++i) {
            if (this->genericParams[i]) ss << this->genericParams[i]->toString(); else ss << "<null_gen_param>";
            if (i < this->genericParams.size() - 1) ss << ", ";
        }
        ss << "> ";
    }
    if (this->traitType) { // Explicit this->
        ss << this->traitType->toString() << " for "; 
    }
    if (this->selfType) ss << this->selfType->toString(); else ss << "<no_self_type>"; // Check selfType
    ss << " {\\\\n"; 
    for (const auto& method_item : this->methods) { // Explicit this->, renamed method to method_item
        if (method_item) ss << "  " << method_item->toString() << ";\\\\n"; 
    }
    ss << "})";
    return ss.str();
}

void ImplDeclaration::accept(Visitor& visitor) {
    visitor.visit(this);
}

// EnumVariantNode
EnumVariantNode::EnumVariantNode(SourceLocation loc_param, std::unique_ptr<Identifier> n_param, std::vector<TypeNodePtr> tns_param) // Renamed params
    : Node(loc_param), name(std::move(n_param)), associatedTypes(std::move(tns_param)) {}

NodeType EnumVariantNode::getType() const {
    return NodeType::ENUM_VARIANT;
}

std::string EnumVariantNode::toString() const {
    std::stringstream ss;
    ss << "EnumVariantNode(" << this->name->toString(); // Explicit this->
    if (!this->associatedTypes.empty()) { // Explicit this->
        ss << "(";
        for (size_t i = 0; i < this->associatedTypes.size(); ++i) { 
            if (this->associatedTypes[i]) ss << this->associatedTypes[i]->toString(); else ss << "<null_assoc_type>";
            if (i < this->associatedTypes.size() - 1) ss << ", ";
        }
        ss << ")";
    }
    ss << ")";
    return ss.str();
}

void EnumVariantNode::accept(Visitor& visitor) {
    visitor.visit(this);
}

// EnumDeclaration
EnumDeclaration::EnumDeclaration(SourceLocation loc_param, std::unique_ptr<Identifier> n_param, std::vector<std::unique_ptr<GenericParamNode>> gp_param, std::vector<std::unique_ptr<EnumVariantNode>> vars_param) // Renamed params
    : Declaration(loc_param), name(std::move(n_param)), genericParams(std::move(gp_param)), variants(std::move(vars_param)) {}

NodeType EnumDeclaration::getType() const {
    return NodeType::ENUM_DECLARATION;
}

std::string EnumDeclaration::toString() const {
    std::stringstream ss;
    ss << "EnumDeclaration(" << this->name->toString(); // Explicit this->
    if (!this->genericParams.empty()) { // Explicit this->
        ss << "<";
        for (size_t i = 0; i < this->genericParams.size(); ++i) {
            if (this->genericParams[i]) ss << this->genericParams[i]->toString(); else ss << "<null_gen_param>";
            if (i < this->genericParams.size() - 1) ss << ", ";
        }
        ss << ">";
    }
    ss << " {\\\\n"; 
    for (const auto& variant_item : this->variants) { // Explicit this->, renamed variant to variant_item
       if (variant_item) ss << "  " << variant_item->toString() << ",\\\\n"; 
    }
    ss << "})";
    return ss.str();
}

void EnumDeclaration::accept(Visitor& visitor) {
    visitor.visit(this);
}

// GenericParamNode
GenericParamNode::GenericParamNode(SourceLocation loc_param, std::unique_ptr<Identifier> n_param, std::vector<TypeNodePtr> b_param) // Renamed params
    : Node(loc_param), name(std::move(n_param)), bounds(std::move(b_param)) {}

NodeType GenericParamNode::getType() const {
    return NodeType::GENERIC_PARAMETER;
}

std::string GenericParamNode::toString() const {
    std::stringstream ss;
    ss << "GenericParamNode(" << this->name->toString(); // Explicit this->
    if (!this->bounds.empty()) { // Explicit this->
        ss << ": ";
        for (size_t i = 0; i < this->bounds.size(); ++i) {
            if (this->bounds[i]) ss << this->bounds[i]->toString(); else ss << "<null_bound>";
            if (i < this->bounds.size() - 1) ss << " + ";
        }
    }
    ss << ")";
    return ss.str();
}

void GenericParamNode::accept(Visitor& visitor) {
    visitor.visit(this);
}

// TemplateDeclarationNode
TemplateDeclarationNode::TemplateDeclarationNode(SourceLocation loc_param, std::unique_ptr<Identifier> name_param, std::vector<std::unique_ptr<GenericParamNode>> generic_params_param, DeclPtr body_param) // Renamed params
    : Declaration(loc_param), name(std::move(name_param)), genericParams(std::move(generic_params_param)), body(std::move(body_param)) {}

NodeType TemplateDeclarationNode::getType() const {
    return NodeType::TEMPLATE_DECLARATION;
}

std::string TemplateDeclarationNode::toString() const {
    std::string str_val = "template " + this->name->name + "<"; // Explicit this->, use name->name
    for (size_t i = 0; i < this->genericParams.size(); ++i) { // Explicit this->
        if (this->genericParams[i]) {
            str_val += this->genericParams[i]->name->name; // Use name->name
            if (!this->genericParams[i]->bounds.empty()) {
                str_val += ": ";
                for (size_t j = 0; j < this->genericParams[i]->bounds.size(); ++j) {
                    if (this->genericParams[i]->bounds[j]) str_val += this->genericParams[i]->bounds[j]->toString(); else str_val += "<null_bound>";
                    if (j < this->genericParams[i]->bounds.size() - 1) {
                        str_val += " + ";
                    }
                }
            }
        } else {
            str_val += "<null_gen_param>";
        }
        if (i < this->genericParams.size() - 1) {
            str_val += ", ";
        }
    }
    str_val += "> {\\\\n";
    if (this->body) { // Explicit this->
        str_val += this->body->toString(); 
    }
    str_val += "\\\\n}";
    return str_val;
}

void TemplateDeclarationNode::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- AddrOfExpression implementation ---
AddrOfExpression::AddrOfExpression(SourceLocation loc, ExprPtr loc_expr) // Renamed location to loc_expr
    : Expression(loc), location(std::move(loc_expr)) {}

NodeType AddrOfExpression::getType() const { return NodeType::ADDR_OF_EXPRESSION; } // Corrected NodeType
std::string AddrOfExpression::toString() const { return "addr(" + (location ? location->toString() : "<null>") + ")"; }
void AddrOfExpression::accept(Visitor& visitor) { visitor.visit(this); }

// --- FromIntToLocExpression implementation ---
FromIntToLocExpression::FromIntToLocExpression(SourceLocation loc, ExprPtr addr_expr) // Renamed address to addr_expr
    : Expression(loc), address(std::move(addr_expr)) {}

NodeType FromIntToLocExpression::getType() const { return NodeType::FROM_INT_TO_LOC_EXPRESSION; } // Corrected NodeType
std::string FromIntToLocExpression::toString() const { return "from(" + (address ? address->toString() : "<null>") + ")"; }
void FromIntToLocExpression::accept(Visitor& visitor) { visitor.visit(this); }

// --- PointerDerefExpression implementation ---
PointerDerefExpression::PointerDerefExpression(SourceLocation loc_param, ExprPtr pointer_param) // Renamed params
    : Expression(loc_param), pointer(std::move(pointer_param)) {}

NodeType PointerDerefExpression::getType() const {
    return NodeType::POINTER_DEREF_EXPRESSION; 
}

std::string PointerDerefExpression::toString() const {
    return "at(" + (this->pointer ? this->pointer->toString() : "<null>") + ")"; // Explicit this->
}

void PointerDerefExpression::accept(Visitor& visitor) { 
    visitor.visit(this);
}

// ArrayElementExpression
// ArrayElementExpression::ArrayElementExpression(SourceLocation loc, ExprPtr arr, ExprPtr idx)
//     : Expression(loc), array(std::move(arr)), index(std::move(idx)) {}

// NodeType ArrayElementExpression::getType() const { return NodeType::ARRAY_ELEMENT_EXPRESSION; }

// std::string ArrayElementExpression::toString() const {
//     return (array ? array->toString() : "<null_array>") + "[" + (index ? index->toString() : "<null_index>") + "]";
// }

// void ArrayElementExpression::accept(Visitor& visitor) {
//     visitor.visit(this);
// }

// LocationExpression
// Definitions are already present from ast_extra.cpp, no need to duplicate.
// ListComprehension
// Definitions are already present from ast_extra.cpp, no need to duplicate.

} // namespace ast
} // namespace vyn