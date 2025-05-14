// Implementations for AST nodes

#include <sstream>
#include <stdexcept> // Required for std::runtime_error
#include "vyn/ast.hpp"
#include "vyn/token.hpp" // For token_type_to_string, assuming it exists

namespace vyn {

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

// ArrayLiteral - NEW
ArrayLiteral::ArrayLiteral(SourceLocation loc, std::vector<ExprPtr> elems)
    : Expression(loc), elements(std::move(elems)) {}

NodeType ArrayLiteral::getType() const {
    return NodeType::ARRAY_LITERAL;
}

std::string ArrayLiteral::toString() const {
    std::stringstream ss;
    ss << "ArrayLiteral([";
    for (size_t i = 0; i < elements.size(); ++i) {
        if (elements[i]) ss << elements[i]->toString(); else ss << "<null_element>";
        if (i < elements.size() - 1) ss << ", ";
    }
    ss << "])";
    return ss.str();
}

void ArrayLiteral::accept(Visitor& visitor) {
    visitor.visit(this);
}

// ObjectLiteral - NEW 
ObjectLiteral::ObjectLiteral(SourceLocation loc, std::vector<ObjectProperty> props)
    : Expression(loc), properties(std::move(props)) {}

NodeType ObjectLiteral::getType() const {
    return NodeType::OBJECT_LITERAL;
}

std::string ObjectLiteral::toString() const {
    std::stringstream ss;
    ss << "ObjectLiteral([";
    for (size_t i = 0; i < properties.size(); ++i) {
        ss << properties[i].key->toString() << ": " << properties[i].value->toString();
        if (i < properties.size() - 1) {
            ss << ", ";
        }
    }
    ss << "])";
    return ss.str();
}

void ObjectLiteral::accept(Visitor& visitor) {
    visitor.visit(this);
}

// NilLiteral
NilLiteral::NilLiteral(SourceLocation loc) : Expression(loc) {}

NodeType NilLiteral::getType() const {
    return NodeType::NIL_LITERAL;
}

std::string NilLiteral::toString() const {
    return "NilLiteral(nil)";
}

void NilLiteral::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- Expressions ---
// UnaryExpression
UnaryExpression::UnaryExpression(SourceLocation loc, const vyn::token::Token& op_token, ExprPtr oper)
    : Expression(loc), op(op_token), operand(std::move(oper)) {}

NodeType UnaryExpression::getType() const { 
    return NodeType::UNARY_EXPRESSION; 
}

std::string UnaryExpression::toString() const {
    std::stringstream ss;
    ss << "UnaryExpression(" << vyn::token_type_to_string(op.type) << ", "; 
    if (operand) ss << operand->toString(); else ss << "<null_operand>";
    ss << ")";
    return ss.str();
}

void UnaryExpression::accept(Visitor& visitor) { 
    visitor.visit(this); 
}

// BinaryExpression
BinaryExpression::BinaryExpression(SourceLocation loc, ExprPtr l, const vyn::token::Token& op_token, ExprPtr r)
    : Expression(loc), left(std::move(l)), op(op_token), right(std::move(r)) {}

NodeType BinaryExpression::getType() const { 
    return NodeType::BINARY_EXPRESSION; 
}

std::string BinaryExpression::toString() const {
    std::stringstream ss;
    ss << "BinaryExpression(";
    if (left) ss << left->toString(); else ss << "<null_left>";
    ss << ", " << vyn::token_type_to_string(op.type) << ", ";
    if (right) ss << right->toString(); else ss << "<null_right>";
    ss << ")";
    return ss.str();
}

void BinaryExpression::accept(Visitor& visitor) { 
    visitor.visit(this); 
}

// AssignmentExpression
AssignmentExpression::AssignmentExpression(SourceLocation loc, ExprPtr l, const vyn::token::Token& op_token, ExprPtr r)
    : Expression(loc), left(std::move(l)), op(op_token), right(std::move(r)) {}

NodeType AssignmentExpression::getType() const { 
    return NodeType::ASSIGNMENT_EXPRESSION; 
}

std::string AssignmentExpression::toString() const {
    std::stringstream ss;
    ss << "AssignmentExpression(";
    if (left) ss << left->toString(); else ss << "<null_left>";
    ss << ", " << vyn::token_type_to_string(op.type) << ", ";
    if (right) ss << right->toString(); else ss << "<null_right>";
    ss << ")";
    return ss.str();
}

void AssignmentExpression::accept(Visitor& visitor) { 
    visitor.visit(this); 
}

// CallExpression
CallExpression::CallExpression(SourceLocation loc, ExprPtr callee_expr, std::vector<ExprPtr> args_list)
    : Expression(loc), callee(std::move(callee_expr)), arguments(std::move(args_list)) {}

NodeType CallExpression::getType() const { 
    return NodeType::CALL_EXPRESSION; 
}

std::string CallExpression::toString() const {
    std::stringstream ss;
    ss << "CallExpression(";
    if (callee) ss << callee->toString(); else ss << "<null_callee>";
    ss << "(";
    for (size_t i = 0; i < arguments.size(); ++i) {
        if (arguments[i]) ss << arguments[i]->toString(); else ss << "<null_arg>";
        if (i < arguments.size() - 1) ss << ", ";
    }
    ss << "))";
    return ss.str();
}

void CallExpression::accept(Visitor& visitor) { 
    visitor.visit(this); 
}

// MemberExpression (was MemberAccessExpression)
MemberExpression::MemberExpression(SourceLocation loc, ExprPtr obj, ExprPtr prop, bool comp) 
    : Expression(loc), object(std::move(obj)), property(std::move(prop)), computed(comp) {} 

NodeType MemberExpression::getType() const { 
    return NodeType::MEMBER_EXPRESSION; 
}

std::string MemberExpression::toString() const {
    std::stringstream ss;
    ss << "MemberExpression(";
    if (object) ss << object->toString(); else ss << "<null_object>";
    ss << (computed ? "[" : ".");
    if (property) ss << property->toString(); else ss << "<null_property>";
    if (computed) ss << "]";
    ss << ")";
    return ss.str();
}

void MemberExpression::accept(Visitor& visitor) { 
    visitor.visit(this); 
}

// Statement Nodes
// BlockStatement (was BlockStmtNode)
BlockStatement::BlockStatement(SourceLocation loc, std::vector<StmtPtr> stmts) 
    : Statement(loc), body(std::move(stmts)) {} 

NodeType BlockStatement::getType() const { 
    return NodeType::BLOCK_STATEMENT; 
}

std::string BlockStatement::toString() const {
    std::stringstream ss;
    ss << "BlockStatement({";
    if (!body.empty()) ss << "\\n"; // Escaped newline
    for (const auto& stmt : body) {
        if (stmt) ss << stmt->toString(); else ss << "<null_stmt>";
        ss << ";\\n"; // Escaped newline
    }
    ss << "})";
    return ss.str();
}

void BlockStatement::accept(Visitor& visitor) {
    visitor.visit(this);
}

// ExpressionStatement (was ExpressionStmtNode)
ExpressionStatement::ExpressionStatement(SourceLocation loc, ExprPtr expr) 
    : Statement(loc), expression(std::move(expr)) {} 

NodeType ExpressionStatement::getType() const { 
    return NodeType::EXPRESSION_STATEMENT; 
}

std::string ExpressionStatement::toString() const {
    std::stringstream ss;
    ss << "ExpressionStatement(";
    if (expression) ss << expression->toString(); else ss << "<null_expression>";
    ss << ")";
    return ss.str();
}

void ExpressionStatement::accept(Visitor& visitor) {
    visitor.visit(this);
}

// IfStatement (was IfStmtNode)
IfStatement::IfStatement(SourceLocation loc, ExprPtr cond, StmtPtr thenB, StmtPtr elseB) 
    : Statement(loc), test(std::move(cond)), consequent(std::move(thenB)), alternate(std::move(elseB)) {} 

NodeType IfStatement::getType() const { 
    return NodeType::IF_STATEMENT; 
}

std::string IfStatement::toString() const {
    std::stringstream ss;
    ss << "IfStatement(if ";
    if (test) ss << test->toString(); else ss << "<null_test>";
    ss << " then ";
    if (consequent) ss << consequent->toString(); else ss << "<null_consequent>";
    if (alternate) {
        ss << " else " << alternate->toString();
    }
    ss << ")";
    return ss.str();
}

void IfStatement::accept(Visitor& visitor) {
    visitor.visit(this);
}

// ForStatement (was ForStmtNode) 
ForStatement::ForStatement(SourceLocation loc, NodePtr i, ExprPtr t, ExprPtr u, StmtPtr b)
    : Statement(loc), init(std::move(i)), test(std::move(t)), update(std::move(u)), body(std::move(b)) {}

NodeType ForStatement::getType() const {
    return NodeType::FOR_STATEMENT;
}
std::string ForStatement::toString() const {
    std::stringstream ss;
    ss << "ForStatement(for ";
    if (init) ss << init->toString(); else ss << "<null_init>";
    ss << "; ";
    if (test) ss << test->toString(); else ss << "<null_test>";
    ss << "; ";
    if (update) ss << update->toString(); else ss << "<null_update>";
    ss << " do ";
    if (body) ss << body->toString(); else ss << "<null_body>";
    ss << ")";
    return ss.str();
}
void ForStatement::accept(Visitor& visitor) {
    visitor.visit(this);
}

// WhileStatement (was WhileStmtNode)
WhileStatement::WhileStatement(SourceLocation loc, ExprPtr cond, StmtPtr b) 
    : Statement(loc), test(std::move(cond)), body(std::move(b)) {} 

NodeType WhileStatement::getType() const { 
    return NodeType::WHILE_STATEMENT; 
}

std::string WhileStatement::toString() const {
    std::stringstream ss;
    ss << "WhileStatement(while ";
    if (test) ss << test->toString(); else ss << "<null_test>";
    ss << " do ";
    if (body) ss << body->toString(); else ss << "<null_body>";
    ss << ")";
    return ss.str();
}
void WhileStatement::accept(Visitor& visitor) {
    visitor.visit(this);
}

// ReturnStatement (was ReturnStmtNode)
ReturnStatement::ReturnStatement(SourceLocation loc, ExprPtr val) 
    : Statement(loc), argument(std::move(val)) {} 

NodeType ReturnStatement::getType() const { 
    return NodeType::RETURN_STATEMENT; 
}

std::string ReturnStatement::toString() const {
    std::stringstream ss;
    ss << "ReturnStatement(return";
    if (argument) {
        ss << " " << argument->toString();
    }
    ss << ")";
    return ss.str();
}
void ReturnStatement::accept(Visitor& visitor) {
    visitor.visit(this);
}

// BreakStatement (was BreakStmtNode)
BreakStatement::BreakStatement(SourceLocation loc) 
    : Statement(loc) {} 

NodeType BreakStatement::getType() const { 
    return NodeType::BREAK_STATEMENT; 
}

std::string BreakStatement::toString() const {
    return "BreakStatement(break)";
}
void BreakStatement::accept(Visitor& visitor) {
    visitor.visit(this);
}

// ContinueStatement (was ContinueStmtNode)
ContinueStatement::ContinueStatement(SourceLocation loc) 
    : Statement(loc) {} 

NodeType ContinueStatement::getType() const { 
    return NodeType::CONTINUE_STATEMENT; 
}

std::string ContinueStatement::toString() const {
    return "ContinueStatement(continue)";
}
void ContinueStatement::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- Declarations ---
// VariableDeclaration (was VarDeclStmtNode)
VariableDeclaration::VariableDeclaration(SourceLocation loc, std::unique_ptr<Identifier> identifier, bool is_const, TypeNodePtr type_node, ExprPtr initial_value)
    : Declaration(loc), id(std::move(identifier)), isConst(is_const), typeNode(std::move(type_node)), init(std::move(initial_value)) {}

NodeType VariableDeclaration::getType() const {
    return NodeType::VARIABLE_DECLARATION;
}
std::string VariableDeclaration::toString() const {
    std::stringstream ss;
    ss << "VariableDeclaration(" << (isConst ? "const " : "var "); 
    if (id) ss << id->toString(); else ss << "<null_id>";
    if (typeNode) {
        ss << ": " << typeNode->toString();
    }
    if (init) {
        ss << " = " << init->toString();
    }
    ss << ")";
    return ss.str();
}
void VariableDeclaration::accept(Visitor& visitor) {
    visitor.visit(this);
}

// FunctionDeclaration (was FuncDeclNode)
FunctionDeclaration::FunctionDeclaration(SourceLocation loc, std::unique_ptr<Identifier> identifier, std::vector<FunctionParameter> func_params, std::unique_ptr<BlockStatement> func_body, bool is_async, TypeNodePtr return_type_node)
    : Declaration(loc), id(std::move(identifier)), params(std::move(func_params)), body(std::move(func_body)), isAsync(is_async), returnTypeNode(std::move(return_type_node)) {}

NodeType FunctionDeclaration::getType() const {
    return NodeType::FUNCTION_DECLARATION;
}
std::string FunctionDeclaration::toString() const {
    std::stringstream ss;
    ss << "FunctionDeclaration(" << (isAsync ? "async " : "") << "fn ";
    if (id) ss << id->toString(); else ss << "<null_id>";
    ss << "(";
    for (size_t i = 0; i < params.size(); ++i) {
        if (params[i].name) ss << params[i].name->toString(); else ss << "<null_param_name>";
        if (params[i].typeNode) ss << ": " << params[i].typeNode->toString();
        if (i < params.size() - 1) ss << ", ";
    }
    ss << ")";
    if (returnTypeNode) ss << " -> " << returnTypeNode->toString();
    if (body) ss << " " << body->toString(); else ss << " <no_body_or_extern>"; // extern functions might not have a body
    ss << ")";
    return ss.str();
}
void FunctionDeclaration::accept(Visitor& visitor) {
    visitor.visit(this);
}

// TypeAliasDeclaration (was TypeAliasDeclNode)
TypeAliasDeclaration::TypeAliasDeclaration(SourceLocation loc, std::unique_ptr<Identifier> n, TypeNodePtr aliased_type_node)
    : Declaration(loc), name(std::move(n)), aliasedTypeNode(std::move(aliased_type_node)) {}

NodeType TypeAliasDeclaration::getType() const {
    return NodeType::TYPE_ALIAS_DECLARATION;
}
std::string TypeAliasDeclaration::toString() const {
    std::stringstream ss;
    ss << "TypeAliasDeclaration(type ";
    if (name) ss << name->toString(); else ss << "<null_name>";
    ss << " = ";
    if (aliasedTypeNode) ss << aliasedTypeNode->toString(); else ss << "<null_aliased_type_node>";
    ss << ")";
    return ss.str();
}
void TypeAliasDeclaration::accept(Visitor& visitor) {
    visitor.visit(this);
}

// ImportDeclaration (was ImportDeclNode)
ImportDeclaration::ImportDeclaration(SourceLocation loc, std::unique_ptr<StringLiteral> src, std::vector<ImportSpecifier> specs, std::unique_ptr<Identifier> default_imp, std::unique_ptr<Identifier> namespace_imp)
    : Declaration(loc), source(std::move(src)), specifiers(std::move(specs)), defaultImport(std::move(default_imp)), namespaceImport(std::move(namespace_imp)) {}

NodeType ImportDeclaration::getType() const {
    return NodeType::IMPORT_DECLARATION;
}
std::string ImportDeclaration::toString() const {
    std::stringstream ss;
    ss << "ImportDeclaration(import ";
    bool needs_comma = false;
    if (defaultImport) {
        ss << defaultImport->toString();
        needs_comma = true;
    }
    if (namespaceImport) {
        if (needs_comma) ss << ", ";
        ss << "* as " << namespaceImport->toString();
        needs_comma = true;
    }
    if (!specifiers.empty()) {
        if (needs_comma) ss << ", ";
        ss << "{";
        for (size_t i = 0; i < specifiers.size(); ++i) {
            if (specifiers[i].importedName) ss << specifiers[i].importedName->toString();
            if (specifiers[i].localName) ss << " as " << specifiers[i].localName->toString();
            if (i < specifiers.size() - 1) ss << ", ";
        }
        ss << "}";
        needs_comma = true;
    }
    if (defaultImport || namespaceImport || !specifiers.empty()) {
         ss << " from ";
    }
    if (source) ss << source->toString(); else ss << "<null_source>";
    ss << ")";
    return ss.str();
}
void ImportDeclaration::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- New TypeNode Implementation ---
TypeNode::TypeNode(SourceLocation loc, TypeCategory category, bool dataIsConst, bool isOptional)
    : Node(loc), category(category), dataIsConst(dataIsConst), isOptional(isOptional),
      name(nullptr), wrappedType(nullptr), arrayElementType(nullptr), functionReturnType(nullptr) {}

TypeNodePtr TypeNode::newIdentifier(SourceLocation loc, std::unique_ptr<Identifier> name, std::vector<TypeNodePtr> genericArgs, bool dataIsConst, bool isOptional) {
    auto node = std::unique_ptr<TypeNode>(new TypeNode(loc, TypeCategory::IDENTIFIER, dataIsConst, isOptional));
    node->name = std::move(name);
    node->genericArguments = std::move(genericArgs);
    return node;
}

TypeNodePtr TypeNode::newOwnershipWrapped(SourceLocation loc, OwnershipKind ownership, TypeNodePtr wrappedType, bool dataIsConst, bool isOptional) {
    auto node = std::unique_ptr<TypeNode>(new TypeNode(loc, TypeCategory::OWNERSHIP_WRAPPED, dataIsConst, isOptional));
    node->ownership = ownership;
    node->wrappedType = std::move(wrappedType);
    return node;
}

TypeNodePtr TypeNode::newArray(SourceLocation loc, TypeNodePtr elementType, bool dataIsConst, bool isOptional) {
    auto node = std::unique_ptr<TypeNode>(new TypeNode(loc, TypeCategory::ARRAY, dataIsConst, isOptional));
    node->arrayElementType = std::move(elementType);
    return node;
}

TypeNodePtr TypeNode::newTuple(SourceLocation loc, std::vector<TypeNodePtr> elementTypes, bool dataIsConst, bool isOptional) {
    auto node = std::unique_ptr<TypeNode>(new TypeNode(loc, TypeCategory::TUPLE, dataIsConst, isOptional));
    node->tupleElementTypes = std::move(elementTypes);
    return node;
}

TypeNodePtr TypeNode::newFunctionSignature(SourceLocation loc, std::vector<TypeNodePtr> paramTypes, TypeNodePtr returnType, bool dataIsConst, bool isOptional) {
    auto node = std::unique_ptr<TypeNode>(new TypeNode(loc, TypeCategory::FUNCTION_SIGNATURE, dataIsConst, isOptional));
    node->functionParameterTypes = std::move(paramTypes);
    node->functionReturnType = std::move(returnType);
    return node;
}

NodeType TypeNode::getType() const {
    return NodeType::TYPE_NODE;
}

std::string TypeNode::toString() const {
    std::stringstream ss;
    // Placeholder: Detailed toString to be implemented based on category
    ss << "TypeNode(category=";
    switch (category) {
        case TypeCategory::IDENTIFIER:
            ss << "IDENTIFIER, name=" << (name ? name->toString() : "null");
            if (!genericArguments.empty()) {
                ss << "<";
                for (size_t i = 0; i < genericArguments.size(); ++i) {
                    ss << (genericArguments[i] ? genericArguments[i]->toString() : "null");
                    if (i < genericArguments.size() - 1) ss << ", ";
                }
                ss << ">";
            }
            break;
        case TypeCategory::OWNERSHIP_WRAPPED:
            ss << "OWNERSHIP_WRAPPED, kind=";
            switch (ownership) {
                case OwnershipKind::MY: ss << "my"; break;
                case OwnershipKind::OUR: ss << "our"; break;
                case OwnershipKind::THEIR: ss << "their"; break;
                case OwnershipKind::PTR: ss << "ptr"; break;
            }
            ss << "<" << (wrappedType ? wrappedType->toString() : "null") << ">";
            break;
        case TypeCategory::ARRAY:
            ss << "ARRAY, element_type=" << (arrayElementType ? arrayElementType->toString() : "null") << "[]";
            break;
        case TypeCategory::TUPLE:
            ss << "TUPLE, (";
            for (size_t i = 0; i < tupleElementTypes.size(); ++i) {
                ss << (tupleElementTypes[i] ? tupleElementTypes[i]->toString() : "null");
                if (i < tupleElementTypes.size() - 1) ss << ", ";
            }
            ss << ")";
            break;
        case TypeCategory::FUNCTION_SIGNATURE:
            ss << "FUNCTION_SIGNATURE, fn(";
            for (size_t i = 0; i < functionParameterTypes.size(); ++i) {
                ss << (functionParameterTypes[i] ? functionParameterTypes[i]->toString() : "null");
                if (i < functionParameterTypes.size() - 1) ss << ", ";
            }
            ss << ") -> " << (functionReturnType ? functionReturnType->toString() : "void");
            break;
        default:
            ss << "UNKNOWN";
            break;
    }
    if (dataIsConst) ss << " const";
    if (isOptional) ss << "?";
    ss << ")";
    return ss.str();
}

void TypeNode::accept(Visitor& visitor) {
    visitor.visit(this);
}


// --- New BorrowExprNode Implementation ---
BorrowExprNode::BorrowExprNode(SourceLocation loc, ExprPtr expr, BorrowKind borrow_kind) 
    : Expression(loc), kind(borrow_kind), expressionToBorrow(std::move(expr)) {}

NodeType BorrowExprNode::getType() const { 
    return NodeType::BORROW_EXPRESSION; 
}

std::string BorrowExprNode::toString() const {
    std::stringstream ss;
    ss << "BorrowExprNode(";
    switch (kind) {
        case BorrowKind::MUTABLE_BORROW:
            ss << "borrow ";
            break;
        case BorrowKind::IMMUTABLE_VIEW:
            ss << "view ";
            break;
    }
    if (expressionToBorrow) {
        ss << expressionToBorrow->toString();
    } else {
        ss << "<null_expression_to_borrow>";
    }
    ss << ")";
    return ss.str();
}

void BorrowExprNode::accept(Visitor& visitor) { 
    visitor.visit(this); 
}

// --- New Declarations ---

// FieldDeclaration
FieldDeclaration::FieldDeclaration(SourceLocation loc, std::unique_ptr<Identifier> n, TypeNodePtr tn, ExprPtr init, bool is_mut)
    : Declaration(loc), name(std::move(n)), typeNode(std::move(tn)), initializer(std::move(init)), isMutable(is_mut) {}

NodeType FieldDeclaration::getType() const {
    return NodeType::FIELD_DECLARATION;
}

std::string FieldDeclaration::toString() const {
    std::stringstream ss;
    ss << "FieldDeclaration(" << (isMutable ? "var " : "let ") << name->toString();
    ss << ": " << typeNode->toString();
    if (initializer) {
        ss << " = " << initializer->toString();
    }
    ss << ")";
    return ss.str();
}

void FieldDeclaration::accept(Visitor& visitor) {
    visitor.visit(this);
}

// StructDeclaration
StructDeclaration::StructDeclaration(SourceLocation loc, std::unique_ptr<Identifier> n, std::vector<std::unique_ptr<GenericParamNode>> gp, std::vector<std::unique_ptr<FieldDeclaration>> f)
    : Declaration(loc), name(std::move(n)), genericParams(std::move(gp)), fields(std::move(f)) {}

NodeType StructDeclaration::getType() const {
    return NodeType::STRUCT_DECLARATION;
}

std::string StructDeclaration::toString() const {
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

void StructDeclaration::accept(Visitor& visitor) {
    visitor.visit(this);
}

// ClassDeclaration
ClassDeclaration::ClassDeclaration(SourceLocation loc, std::unique_ptr<Identifier> n, std::vector<std::unique_ptr<GenericParamNode>> gp, std::vector<DeclPtr> m)
    : Declaration(loc), name(std::move(n)), genericParams(std::move(gp)), members(std::move(m)) {}

NodeType ClassDeclaration::getType() const {
    return NodeType::CLASS_DECLARATION;
}

std::string ClassDeclaration::toString() const {
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

void ClassDeclaration::accept(Visitor& visitor) {
    visitor.visit(this);
}

// ImplDeclaration
ImplDeclaration::ImplDeclaration(SourceLocation loc, std::vector<std::unique_ptr<GenericParamNode>> gp, TypeNodePtr self_tn, std::vector<std::unique_ptr<FunctionDeclaration>> meths, TypeNodePtr trait_tn)
    : Declaration(loc), genericParams(std::move(gp)), selfTypeNode(std::move(self_tn)), traitTypeNode(std::move(trait_tn)), methods(std::move(meths)) {}

NodeType ImplDeclaration::getType() const {
    return NodeType::IMPL_DECLARATION;
}

std::string ImplDeclaration::toString() const {
    std::stringstream ss;
    ss << "ImplDeclaration(";
    if (!genericParams.empty()) {
        ss << "<";
        for (size_t i = 0; i < genericParams.size(); ++i) {
            ss << genericParams[i]->toString();
            if (i < genericParams.size() - 1) ss << ", ";
        }
        ss << "> ";
    }
    if (traitTypeNode) {
        ss << traitTypeNode->toString() << " for ";
    }
    ss << selfTypeNode->toString() << " {\\\\n"; // Escaped newline
    for (const auto& method : methods) {
        ss << "  " << method->toString() << ";\\\\n"; // Escaped newline
    }
    ss << "})";
    return ss.str();
}

void ImplDeclaration::accept(Visitor& visitor) {
    visitor.visit(this);
}

// EnumVariantNode
EnumVariantNode::EnumVariantNode(SourceLocation loc, std::unique_ptr<Identifier> n, std::vector<TypeNodePtr> tns)
    : Declaration(loc), name(std::move(n)), typeNodes(std::move(tns)) {}

NodeType EnumVariantNode::getType() const {
    return NodeType::ENUM_VARIANT;
}

std::string EnumVariantNode::toString() const {
    std::stringstream ss;
    ss << "EnumVariantNode(" << name->toString();
    if (!typeNodes.empty()) {
        ss << "(";
        for (size_t i = 0; i < typeNodes.size(); ++i) {
            ss << typeNodes[i]->toString();
            if (i < typeNodes.size() - 1) ss << ", ";
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
EnumDeclaration::EnumDeclaration(SourceLocation loc, std::unique_ptr<Identifier> n, std::vector<std::unique_ptr<GenericParamNode>> gp, std::vector<std::unique_ptr<EnumVariantNode>> vars)
    : Declaration(loc), name(std::move(n)), genericParams(std::move(gp)), variants(std::move(vars)) {}

NodeType EnumDeclaration::getType() const {
    return NodeType::ENUM_DECLARATION;
}

std::string EnumDeclaration::toString() const {
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

void EnumDeclaration::accept(Visitor& visitor) {
    visitor.visit(this);
}

// GenericParamNode
GenericParamNode::GenericParamNode(SourceLocation loc, std::unique_ptr<Identifier> n, std::vector<TypeNodePtr> b)
    : Node(loc), name(std::move(n)), bounds(std::move(b)) {}

NodeType GenericParamNode::getType() const {
    return NodeType::GENERIC_PARAMETER;
}

std::string GenericParamNode::toString() const {
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

void GenericParamNode::accept(Visitor& visitor) {
    visitor.visit(this);
}

} // namespace vyn