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
    ss << "ObjectLiteral({";
    for (size_t i = 0; i < properties.size(); ++i) {
        if (properties[i].key) ss << properties[i].key->toString(); else ss << "<null_key>";
        ss << ": ";
        if (properties[i].value) ss << properties[i].value->toString(); else ss << "<null_value>";
        if (i < properties.size() - 1) ss << ", ";
    }
    ss << "})";
    return ss.str();
}

void ObjectLiteral::accept(Visitor& visitor) {
    visitor.visit(this);
}


// Expression Nodes
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
VariableDeclaration::VariableDeclaration(SourceLocation loc, std::unique_ptr<Identifier> identifier, bool is_const, TypeAnnotationPtr type_ann, ExprPtr initial_value)
    : Declaration(loc), id(std::move(identifier)), isConst(is_const), typeAnnotation(std::move(type_ann)), init(std::move(initial_value)) {}

NodeType VariableDeclaration::getType() const {
    return NodeType::VARIABLE_DECLARATION;
}
std::string VariableDeclaration::toString() const {
    std::stringstream ss;
    ss << "VariableDeclaration(" << (isConst ? "const " : "let "); // Assuming 'let' for non-const
    if (id) ss << id->toString(); else ss << "<null_id>";
    if (typeAnnotation) {
        ss << ": " << typeAnnotation->toString();
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
FunctionDeclaration::FunctionDeclaration(SourceLocation loc, std::unique_ptr<Identifier> identifier, std::vector<FunctionParameter> func_params, std::unique_ptr<BlockStatement> func_body, bool is_async, TypeAnnotationPtr return_type)
    : Declaration(loc), id(std::move(identifier)), params(std::move(func_params)), body(std::move(func_body)), isAsync(is_async), returnType(std::move(return_type)) {}

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
        if (params[i].typeAnnotation) ss << ": " << params[i].typeAnnotation->toString();
        if (i < params.size() - 1) ss << ", ";
    }
    ss << ")";
    if (returnType) ss << " -> " << returnType->toString();
    if (body) ss << " " << body->toString(); else ss << " <no_body_or_extern>"; // extern functions might not have a body
    ss << ")";
    return ss.str();
}
void FunctionDeclaration::accept(Visitor& visitor) {
    visitor.visit(this);
}

// TypeAliasDeclaration (was TypeAliasDeclNode)
TypeAliasDeclaration::TypeAliasDeclaration(SourceLocation loc, std::unique_ptr<Identifier> n, TypeAnnotationPtr ta)
    : Declaration(loc), name(std::move(n)), typeAnnotation(std::move(ta)) {}

NodeType TypeAliasDeclaration::getType() const {
    return NodeType::TYPE_ALIAS_DECLARATION;
}
std::string TypeAliasDeclaration::toString() const {
    std::stringstream ss;
    ss << "TypeAliasDeclaration(type ";
    if (name) ss << name->toString(); else ss << "<null_name>";
    ss << " = ";
    if (typeAnnotation) ss << typeAnnotation->toString(); else ss << "<null_type_annotation>";
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

// --- Other ---
// TypeAnnotation 
// Constructor for simple type: MyType
TypeAnnotation::TypeAnnotation(SourceLocation loc, std::unique_ptr<Identifier> name)
    : Node(loc), simpleTypeName(std::move(name)), arrayElementType(nullptr), genericBaseType(nullptr) {}

// Constructor for array type: T[]
TypeAnnotation::TypeAnnotation(SourceLocation loc, TypeAnnotationPtr elementType, bool /*isArrayMarker*/) 
    : Node(loc), simpleTypeName(nullptr), arrayElementType(std::move(elementType)), genericBaseType(nullptr) {}

// Constructor for generic type: List<T>
TypeAnnotation::TypeAnnotation(SourceLocation loc, std::unique_ptr<Identifier> base, std::vector<TypeAnnotationPtr> args)
    : Node(loc), simpleTypeName(nullptr), arrayElementType(nullptr), genericBaseType(std::move(base)), genericTypeArguments(std::move(args)) {}


NodeType TypeAnnotation::getType() const {
    return NodeType::TYPE_ANNOTATION;
}

std::string TypeAnnotation::toString() const {
    std::stringstream ss;
    if (isSimpleType() && simpleTypeName) {
        ss << simpleTypeName->toString();
    } else if (isArrayType() && arrayElementType) {
        ss << arrayElementType->toString() << "[]";
    } else if (isGenericType() && genericBaseType) {
        ss << genericBaseType->toString() << "<";
        for (size_t i = 0; i < genericTypeArguments.size(); ++i) {
            if (genericTypeArguments[i]) ss << genericTypeArguments[i]->toString(); else ss << "<null_type_arg>";
            if (i < genericTypeArguments.size() - 1) ss << ", ";
        }
        ss << ">";
    } else {
        // This case should ideally not be reached if constructors are used properly
        ss << "<invalid_type_annotation_state>";
    }
    return ss.str();
}

void TypeAnnotation::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- New Declaration Node Implementations ---

// FieldDeclaration
FieldDeclaration::FieldDeclaration(SourceLocation loc, std::unique_ptr<Identifier> n, TypeAnnotationPtr ta, ExprPtr init, bool mut)
    : Declaration(loc), name(std::move(n)), typeAnnotation(std::move(ta)), initializer(std::move(init)), isMutable(mut) {}

NodeType FieldDeclaration::getType() const {
    return NodeType::FIELD_DECLARATION;
}

std::string FieldDeclaration::toString() const {
    std::stringstream ss;
    ss << "FieldDeclaration(" << (isMutable ? "mut " : "let ");
    if (name) ss << name->toString(); else ss << "<null_name>";
    if (typeAnnotation) {
        ss << ": " << typeAnnotation->toString();
    } else {
        ss << ": <null_type_annotation>";
    }
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
StructDeclaration::StructDeclaration(SourceLocation loc, std::unique_ptr<Identifier> n, std::vector<std::unique_ptr<GenericParamNode>> gps, std::vector<std::unique_ptr<FieldDeclaration>> flds)
    : Declaration(loc), name(std::move(n)), genericParams(std::move(gps)), fields(std::move(flds)) {}

NodeType StructDeclaration::getType() const {
    return NodeType::STRUCT_DECLARATION;
}

std::string StructDeclaration::toString() const {
    std::stringstream ss;
    ss << "StructDeclaration(" << (name ? name->toString() : "<null_name>");
    if (!genericParams.empty()) {
        ss << "<";
        for (size_t i = 0; i < genericParams.size(); ++i) {
            if (genericParams[i]) ss << genericParams[i]->toString(); else ss << "<null_generic_param>";
            if (i < genericParams.size() - 1) ss << ", ";
        }
        ss << ">";
    }
    ss << " {\\\\\\n"; // Escaped newline
    for (const auto& field : fields) {
        if (field) {
            ss << "  " << field->toString() << ";\\\\\\n"; // Escaped newline
        } else {
            ss << "  <null_field>;\\\\\\n"; // Escaped newline
        }
    }
    ss << "})";
    return ss.str();
}

void StructDeclaration::accept(Visitor& visitor) {
    visitor.visit(this);
}

// ClassDeclaration
ClassDeclaration::ClassDeclaration(SourceLocation loc, std::unique_ptr<Identifier> n, std::vector<std::unique_ptr<GenericParamNode>> gps, std::vector<DeclPtr> membs)
    : Declaration(loc), name(std::move(n)), genericParams(std::move(gps)), members(std::move(membs)) {}

NodeType ClassDeclaration::getType() const {
    return NodeType::CLASS_DECLARATION;
}

std::string ClassDeclaration::toString() const {
    std::stringstream ss;
    ss << "ClassDeclaration(" << (name ? name->toString() : "<null_name>");
    if (!genericParams.empty()) {
        ss << "<";
        for (size_t i = 0; i < genericParams.size(); ++i) {
            if (genericParams[i]) ss << genericParams[i]->toString(); else ss << "<null_generic_param>";
            if (i < genericParams.size() - 1) ss << ", ";
        }
        ss << ">";
    }
    ss << " {\\\\\\n"; // Escaped newline
    for (const auto& member : members) {
        if (member) {
            ss << "  " << member->toString() << ";\\\\\\n"; // Escaped newline
        } else {
            ss << "  <null_member>;\\\\\\n"; // Escaped newline
        }
    }
    ss << "})";
    return ss.str();
}

void ClassDeclaration::accept(Visitor& visitor) {
    visitor.visit(this);
}

// ImplDeclaration
ImplDeclaration::ImplDeclaration(SourceLocation loc, std::vector<std::unique_ptr<GenericParamNode>> gps, TypeAnnotationPtr self_type, std::vector<std::unique_ptr<FunctionDeclaration>> meths, TypeAnnotationPtr trait_type)
    : Declaration(loc), genericParams(std::move(gps)), selfType(std::move(self_type)), traitType(std::move(trait_type)), methods(std::move(meths)) {}

NodeType ImplDeclaration::getType() const {
    return NodeType::IMPL_DECLARATION;
}

std::string ImplDeclaration::toString() const {
    std::stringstream ss;
    ss << "ImplDeclaration(impl";
    if (!genericParams.empty()) {
        ss << "<";
        for (size_t i = 0; i < genericParams.size(); ++i) {
            if (genericParams[i]) ss << genericParams[i]->toString(); else ss << "<null_generic_param>";
            if (i < genericParams.size() - 1) ss << ", ";
        }
        ss << ">";
    }
    if (traitType) {
        ss << " " << traitType->toString() << " for";
    }
    if (selfType) {
        ss << " " << selfType->toString();
    } else {
        ss << " <null_self_type>";
    }
    ss << " {\\\\\\n"; // Escaped newline
    for (const auto& method : methods) {
        if (method) {
            ss << "  " << method->toString() << "\\\\\\n"; // Escaped newline
        } else {
            ss << "  <null_method>;\\\\\\n"; // Escaped newline
        }
    }
    ss << "})";
    return ss.str();
}

void ImplDeclaration::accept(Visitor& visitor) {
    visitor.visit(this);
}

// EnumVariantNode
EnumVariantNode::EnumVariantNode(SourceLocation loc, std::unique_ptr<Identifier> n, std::vector<TypeAnnotationPtr> typs)
    : Declaration(loc), name(std::move(n)), types(std::move(typs)) {}

NodeType EnumVariantNode::getType() const {
    return NodeType::ENUM_VARIANT;
}

std::string EnumVariantNode::toString() const {
    std::stringstream ss;
    ss << "EnumVariantNode(";
    if (name) ss << name->toString(); else ss << "<null_name>";
    if (!types.empty()) {
        ss << "(";
        for (size_t i = 0; i < types.size(); ++i) {
            if (types[i]) {
                ss << types[i]->toString();
            } else {
                ss << "<null_type_arg>";
            }
            if (i < types.size() - 1) {
                ss << ", ";
            }
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
EnumDeclaration::EnumDeclaration(SourceLocation loc, std::unique_ptr<Identifier> n, std::vector<std::unique_ptr<GenericParamNode>> gps, std::vector<std::unique_ptr<EnumVariantNode>> vars)
    : Declaration(loc), name(std::move(n)), genericParams(std::move(gps)), variants(std::move(vars)) {}

NodeType EnumDeclaration::getType() const {
    return NodeType::ENUM_DECLARATION;
}

std::string EnumDeclaration::toString() const {
    std::stringstream ss;
    ss << "EnumDeclaration(" << (name ? name->toString() : "<null_name>");
    if (!genericParams.empty()) {
        ss << "<";
        for (size_t i = 0; i < genericParams.size(); ++i) {
            if (genericParams[i]) ss << genericParams[i]->toString(); else ss << "<null_generic_param>";
            if (i < genericParams.size() - 1) ss << ", ";
        }
        ss << ">";
    }
    ss << " {\\\\\\n"; // Escaped newline
    for (const auto& variant : variants) {
        if (variant) {
            ss << "  " << variant->toString() << ",\\\\\\n"; // Escaped newline
        } else {
            ss << "  <null_variant>,\\\\\\n"; // Escaped newline
        }
    }
    ss << "})";
    return ss.str();
}

void EnumDeclaration::accept(Visitor& visitor) {
    visitor.visit(this);
}

// GenericParamNode
GenericParamNode::GenericParamNode(SourceLocation loc, std::unique_ptr<Identifier> n, std::vector<TypeAnnotationPtr> bnds)
    : Node(loc), name(std::move(n)), bounds(std::move(bnds)) {}

NodeType GenericParamNode::getType() const {
    return NodeType::GENERIC_PARAMETER;
}

std::string GenericParamNode::toString() const {
    std::stringstream ss;
    ss << (name ? name->toString() : "<null_name>");
    if (!bounds.empty()) {
        ss << ": ";
        for (size_t i = 0; i < bounds.size(); ++i) {
            if (bounds[i]) {
                ss << bounds[i]->toString();
            } else {
                ss << "<null_bound>";
            }
            if (i < bounds.size() - 1) {
                ss << " + "; // Assuming '+' syntax for multiple bounds
            }
        }
    }
    return ss.str();
}

void GenericParamNode::accept(Visitor& visitor) {
    visitor.visit(this);
}

} // namespace vyn

/* Remaining original ast.cpp content is commented out below as it does not
   directly map to the provided ast.hpp or requires more information.

... A lot of commented out code ...

    // // Pattern Nodes
    // IdentifierPatternNode::IdentifierPatternNode(SourceLocation loc, std::unique_ptr<IdentifierNode> id, bool mut, Node* p)
    //     : PatternNode(NodeType::IDENTIFIER_PATTERN, std::move(loc), p), identifier(std::move(id)), isMutable(mut) {}
    // void IdentifierPatternNode::accept(Visitor& visitor) {
    //     visitor.visit(this);
    // }
*/
