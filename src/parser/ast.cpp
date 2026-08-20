// SPDX-License-Identifier: Apache-2.0

// All includes at the top
#include <sstream>
#include <string>
#include <utility>
#include <optional>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include "vyb/parser/ast.hpp" // This now includes iostream, BorrowKind, OwnershipKind
#include "vyb/parser/token.hpp"

namespace vyb {
namespace ast {

// ObjectLiteral destructor
ObjectLiteral::~ObjectLiteral() = default;

// ObjectLiteral constructor implementation
ObjectLiteral::ObjectLiteral(SourceLocation loc, TypeNodePtr typePath, std::vector<ObjectProperty> properties)
    : Expression(loc), typePath(std::move(typePath)), properties(std::move(properties)) {}

NodeType ObjectLiteral::getType() const {
        return NodeType::OBJECT_LITERAL;
}

std::string ObjectLiteral::toString() const {
    std::stringstream ss;
    if (typePath) {
        ss << typePath->toString();
    }
    ss << "{"; // Removed backslash before brace
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

// --- ArrayElementExpression ---
ArrayElementExpression::ArrayElementExpression(SourceLocation loc, ExprPtr arr, ExprPtr idx)
    : Expression(loc), array(std::move(arr)), index(std::move(idx)) {}

NodeType ArrayElementExpression::getType() const {
    return NodeType::ARRAY_ELEMENT_EXPRESSION;
}

std::string ArrayElementExpression::toString() const {
    return (array ? array->toString() : "nullptr") + "[" + (index ? index->toString() : "nullptr") + "]";
}

void ArrayElementExpression::accept(Visitor& visitor) {
    visitor.visit(this);
}
ArrayElementExpression::~ArrayElementExpression() = default;

// --- LocationExpression ---
LocationExpression::LocationExpression(SourceLocation loc, ExprPtr expression)
    : Expression(loc), expression(std::move(expression)) {}

NodeType LocationExpression::getType() const {
    return NodeType::LOCATION_EXPRESSION;
}

std::string LocationExpression::toString() const {
    return "loc(" + (expression ? expression->toString() : "nullptr") + ")";
}

void LocationExpression::accept(Visitor& visitor) {
    visitor.visit(this);
}
LocationExpression::~LocationExpression() = default;

// --- ListComprehension ---
ListComprehension::ListComprehension(SourceLocation loc, ExprPtr elementExpr, IdentifierPtr loopVariable, ExprPtr iterableExpr, ExprPtr conditionExpr)
    : Expression(loc),
      elementExpr(std::move(elementExpr)),
      loopVariable(std::move(loopVariable)),
      iterableExpr(std::move(iterableExpr)),
      conditionExpr(std::move(conditionExpr)) {}

NodeType ListComprehension::getType() const {
    return NodeType::LIST_COMPREHENSION;
}

std::string ListComprehension::toString() const {
    std::string str = "[" + (elementExpr ? elementExpr->toString() : "nullptr");
    str += " for " + (loopVariable ? loopVariable->toString() : "nullptr");
    str += " in " + (iterableExpr ? iterableExpr->toString() : "nullptr");
    if (conditionExpr) {
        str += " if " + conditionExpr->toString();
    }
    str += "]";
    return str;
}

void ListComprehension::accept(Visitor& visitor) {
    visitor.visit(this);
}
ListComprehension::~ListComprehension() = default;

// --- IntegerLiteral ---
IntegerLiteral::IntegerLiteral(SourceLocation loc, int64_t val, bool unsignedLit, uint64_t uval)
    : Expression(loc), value(val), isUnsigned(unsignedLit), uvalue(uval) {}

NodeType IntegerLiteral::getType() const {
    return NodeType::INTEGER_LITERAL;
}

std::string IntegerLiteral::toString() const {
    return isUnsigned ? std::to_string(uvalue) : std::to_string(value);
}

void IntegerLiteral::accept(Visitor& visitor) {
    visitor.visit(this);
}

IntegerLiteral::~IntegerLiteral() = default;

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

// --- Identifier ---
Identifier::Identifier(SourceLocation loc, std::string name_val)
    : Expression(loc), name(std::move(name_val)) {}

NodeType Identifier::getType() const {
    return NodeType::IDENTIFIER;
}

std::string Identifier::toString() const {
    return name;
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
    // Basic string representation, might need escaping for special characters
    return "\"" + value + "\"";
}

void StringLiteral::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- ArrayLiteral ---
ArrayLiteral::ArrayLiteral(SourceLocation loc, std::vector<ExprPtr> elems)
    : Expression(loc), elements(std::move(elems)) {}

std::string ArrayLiteral::toString() const {
    std::stringstream ss;
    ss << "[";
    for (size_t i = 0; i < elements.size(); ++i) {
        if (elements[i]) {
            ss << elements[i]->toString();
        }
        if (i < elements.size() - 1) {
            ss << ", ";
        }
    }
    ss << "]";
    return ss.str();
}

void ArrayLiteral::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- BorrowExpression ---
BorrowExpression::BorrowExpression(SourceLocation loc, ExprPtr expr, BorrowKind k)
    : Expression(loc), expression(std::move(expr)), kind(k) {}

std::string BorrowExpression::toString() const {
    std::string kindStr;
    switch (kind) {
        case BorrowKind::MUTABLE_BORROW:
            kindStr = "borrow_mut";
            break;
        case BorrowKind::IMMUTABLE_VIEW:
            kindStr = "view";
            break;
        // Add other cases if BorrowKind is expanded
        default:
            kindStr = "unknown_borrow_kind";
            break;
    }
    return kindStr + "(" + (expression ? expression->toString() : "nullptr") + ")";
}

void BorrowExpression::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- PointerDerefExpression ---
PointerDerefExpression::PointerDerefExpression(SourceLocation loc, ExprPtr ptr)
    : Expression(loc), pointer(std::move(ptr)) {}

NodeType PointerDerefExpression::getType() const {
    return NodeType::POINTER_DEREF_EXPRESSION;
}

std::string PointerDerefExpression::toString() const {
    return "at(" + (pointer ? pointer->toString() : "nullptr") + ")";
}

void PointerDerefExpression::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- AddrOfExpression ---
AddrOfExpression::AddrOfExpression(SourceLocation loc, ExprPtr loc_expr)
    : Expression(loc), location(std::move(loc_expr)) {}

NodeType AddrOfExpression::getType() const {
    return NodeType::ADDR_OF_EXPRESSION;
}

std::string AddrOfExpression::toString() const {
    return "addr(" + (location ? location->toString() : "nullptr") + ")";
}

void AddrOfExpression::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- FromIntToLocExpression ---
// Constructor is defined inline in ast.hpp
// REMOVE any explicit constructor definition that was here.
// For example, the erroneous:
// FromIntToLocExpression::FromIntToLocExpression(ExprPtr addr_expr, TypeNodePtr target_ty, SourceLocation loc)
//     : Expression(loc), address_expr(std::move(addr_expr)), target_type(std::move(target_ty)) {}
NodeType FromIntToLocExpression::getType() const {
    return NodeType::FROM_INT_TO_LOC_EXPRESSION;
}

std::string FromIntToLocExpression::toString() const {
    return "from<" + (target_type ? target_type->toString() : "UnknownType") + ">(" +
           (address_expr ? address_expr->toString() : "nullptr") + ")";
}

void FromIntToLocExpression::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- UnaryExpression ---
UnaryExpression::UnaryExpression(SourceLocation loc, const token::Token& op_val, ExprPtr operand_val)
    : Expression(loc), op(op_val), operand(std::move(operand_val)) {}

UnaryExpression::~UnaryExpression() = default;

NodeType UnaryExpression::getType() const {
    return NodeType::UNARY_EXPRESSION;
}

std::string UnaryExpression::toString() const {
    return op.lexeme + (operand ? operand->toString() : "nullptr");
}

void UnaryExpression::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- BinaryExpression ---
BinaryExpression::BinaryExpression(SourceLocation loc, ExprPtr l, const token::Token& op_val, ExprPtr r)
    : Expression(loc), left(std::move(l)), op(op_val), right(std::move(r)) {}

BinaryExpression::~BinaryExpression() = default;

NodeType BinaryExpression::getType() const {
    return NodeType::BINARY_EXPRESSION;
}

std::string BinaryExpression::toString() const {
    return "(" + (left ? left->toString() : "nullptr") + " " + op.lexeme + " " +
           (right ? right->toString() : "nullptr") + ")";
}

void BinaryExpression::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- CallExpression ---
CallExpression::CallExpression(SourceLocation loc, ExprPtr callee_val, std::vector<ExprPtr> arguments_val)
    : Expression(loc), callee(std::move(callee_val)), arguments(std::move(arguments_val)) {}

CallExpression::~CallExpression() = default;

NodeType CallExpression::getType() const {
    return NodeType::CALL_EXPRESSION;
}

std::string CallExpression::toString() const {
    std::stringstream ss;
    ss << (callee ? callee->toString() : "nullptr") << "(";
    for (size_t i = 0; i < arguments.size(); ++i) {
        if (arguments[i]) {
            ss << arguments[i]->toString();
        }
        if (i < arguments.size() - 1) {
            ss << ", ";
        }
    }
    ss << ")";
    return ss.str();
}

void CallExpression::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- ConstructionExpression ---
ConstructionExpression::ConstructionExpression(SourceLocation loc, TypeNodePtr constructed_type, std::vector<ExprPtr> args)
    : Expression(loc), constructedType(std::move(constructed_type)), arguments(std::move(args)) {}

NodeType ConstructionExpression::getType() const {
    return NodeType::CONSTRUCTION_EXPRESSION;
}

std::string ConstructionExpression::toString() const {
    std::stringstream ss;
    ss << (constructedType ? constructedType->toString() : "UnknownType") << "(";
    for (size_t i = 0; i < arguments.size(); ++i) {
        if (arguments[i]) {
            ss << arguments[i]->toString();
        }
        if (i < arguments.size() - 1) {
            ss << ", ";
        }
    }
    ss << ")";
    return ss.str();
}

void ConstructionExpression::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- ArrayInitializationExpression ---
ArrayInitializationExpression::ArrayInitializationExpression(SourceLocation loc, TypeNodePtr elem_type, ExprPtr size_expr)
    : Expression(loc), elementType(std::move(elem_type)), sizeExpression(std::move(size_expr)) {}

NodeType ArrayInitializationExpression::getType() const {
    return NodeType::ARRAY_INITIALIZATION_EXPRESSION;
}

std::string ArrayInitializationExpression::toString() const {
    return "[" + (elementType ? elementType->toString() : "UnknownType") + "; " +
           (sizeExpression ? sizeExpression->toString() : "UnknownSize") + "]()";
}

void ArrayInitializationExpression::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- GenericInstantiationExpression ---
GenericInstantiationExpression::GenericInstantiationExpression(SourceLocation loc, ExprPtr base, std::vector<TypeNodePtr> args, SourceLocation lt, SourceLocation gt)
    : Expression(loc), baseExpression(std::move(base)), genericArguments(std::move(args)), lt_loc(lt), gt_loc(gt) {}

NodeType GenericInstantiationExpression::getType() const {
    return NodeType::GENERIC_INSTANTIATION_EXPRESSION;
}

std::string GenericInstantiationExpression::toString() const {
    std::stringstream ss;
    ss << (baseExpression ? baseExpression->toString() : "nullptr") << "<";
    for (size_t i = 0; i < genericArguments.size(); ++i) {
        if (genericArguments[i]) {
            ss << genericArguments[i]->toString();
        }
        if (i < genericArguments.size() - 1) {
            ss << ", ";
        }
    }
    ss << ">";
    return ss.str();
}

void GenericInstantiationExpression::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- MemberExpression ---
MemberExpression::MemberExpression(SourceLocation loc, ExprPtr obj, ExprPtr prop, bool comp)
    : Expression(loc), object(std::move(obj)), property(std::move(prop)), computed(comp) {}

MemberExpression::~MemberExpression() = default;

NodeType MemberExpression::getType() const {
    return NodeType::MEMBER_EXPRESSION;
}

std::string MemberExpression::toString() const {
    if (computed) {
        return (object ? object->toString() : "nullptr") + "[" +
               (property ? property->toString() : "nullptr") + "]";
    } else {
        return (object ? object->toString() : "nullptr") + "." +
               (property ? property->toString() : "nullptr");
    }
}

void MemberExpression::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- AssignmentExpression ---
AssignmentExpression::AssignmentExpression(SourceLocation loc, ExprPtr l, const token::Token& op_val, ExprPtr r)
    : Expression(loc), left(std::move(l)), op(op_val), right(std::move(r)) {}

AssignmentExpression::~AssignmentExpression() = default;

NodeType AssignmentExpression::getType() const {
    return NodeType::ASSIGNMENT_EXPRESSION;
}

std::string AssignmentExpression::toString() const {
    return "(" + (left ? left->toString() : "nullptr") + " " + op.lexeme + " " +
           (right ? right->toString() : "nullptr") + ")";
}

void AssignmentExpression::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- BlockStatement ---
BlockStatement::BlockStatement(SourceLocation loc, std::vector<StmtPtr> body_val)
    : Statement(loc), body(std::move(body_val)) {}

BlockStatement::~BlockStatement() = default;

NodeType BlockStatement::getType() const {
    return NodeType::BLOCK_STATEMENT;
}

std::string BlockStatement::toString() const {
    std::stringstream ss;
    ss << "{\n";
    for (const auto& stmt : body) {
        if (stmt) {
            // Basic indentation for readability, can be improved
            std::string stmtStr = stmt->toString();
            std::string line;
            std::stringstream stmtStream(stmtStr);
            while (std::getline(stmtStream, line)) {
                ss << "  " << line << "\n";
            }
        }
    }
    ss << "}";
    return ss.str();
}

void BlockStatement::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- EmptyStatement ---
EmptyStatement::EmptyStatement(SourceLocation loc)
    : Statement(loc) {}

NodeType EmptyStatement::getType() const {
    return NodeType::EMPTY_STATEMENT;
}

std::string EmptyStatement::toString() const {
    return ";";
}

void EmptyStatement::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- TryStatement ---
TryStatement::TryStatement(const SourceLocation& loc_val, std::unique_ptr<BlockStatement> try_block,
                           std::optional<std::string> catch_ident, std::unique_ptr<BlockStatement> catch_block,
                           std::unique_ptr<BlockStatement> finally_block)
    : Statement(loc_val), tryBlock(std::move(try_block)), catchIdent(std::move(catch_ident)),
      catchBlock(std::move(catch_block)), finallyBlock(std::move(finally_block)) {}

NodeType TryStatement::getType() const {
    return NodeType::TRY_STATEMENT;
}

std::string TryStatement::toString() const {
    std::string str = "try " + (tryBlock ? tryBlock->toString() : "{}");
    if (catchBlock) {
        str += " catch";
        if (catchIdent) {
            str += " (" + *catchIdent + ")";
        }
        str += " " + catchBlock->toString();
    }
    if (finallyBlock) {
        str += " finally " + finallyBlock->toString();
    }
    return str;
}

void TryStatement::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- ExpressionStatement ---
ExpressionStatement::ExpressionStatement(SourceLocation loc, ExprPtr expr)
    : Statement(loc), expression(std::move(expr)) {}

ExpressionStatement::~ExpressionStatement() = default;

NodeType ExpressionStatement::getType() const {
    return NodeType::EXPRESSION_STATEMENT;
}

std::string ExpressionStatement::toString() const {
    return (expression ? expression->toString() : "nullptr") + ";";
}

void ExpressionStatement::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- IfStatement ---
IfStatement::IfStatement(SourceLocation loc, ExprPtr t, StmtPtr cons, StmtPtr alt)
    : Statement(loc), test(std::move(t)), consequent(std::move(cons)), alternate(std::move(alt)) {}

IfStatement::~IfStatement() = default;

NodeType IfStatement::getType() const {
    return NodeType::IF_STATEMENT;
}

std::string IfStatement::toString() const {
    std::string str = "if (" + (test ? test->toString() : "nullptr") + ") " +
                      (consequent ? consequent->toString() : "{}");
    if (alternate) {
        str += " else " + alternate->toString();
    }
    return str;
}

void IfStatement::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- ForStatement ---
ForStatement::ForStatement(SourceLocation loc, NodePtr i, ExprPtr t, ExprPtr u, StmtPtr b)
    : Statement(loc), init(std::move(i)), test(std::move(t)), update(std::move(u)), body(std::move(b)), label() {}

ForStatement::~ForStatement() = default;

NodeType ForStatement::getType() const {
    return NodeType::FOR_STATEMENT;
}

std::string ForStatement::toString() const {
    std::string initStr = init ? init->toString() : ";";
    // If init is an ExpressionStatement, it already ends with a semicolon.
    // If it's a VariableDeclaration, it might not. We need to be careful here.
    // For simplicity, assume VariableDeclaration::toString() doesn't add a semicolon.
    if (init && init->getType() == NodeType::VARIABLE_DECLARATION) {
        // initStr does not end with ';'
    } else if (init && init->getType() == NodeType::EXPRESSION_STATEMENT) {
        // initStr already ends with ';', remove it for the for loop structure
        if (!initStr.empty() && initStr.back() == ';') {
            initStr.pop_back();
        }
    } else if (!init) {
        initStr = ""; // No initializer part
    }

    return (label.empty() ? "" : label + ": ") + "for (" + initStr + "; " +
           (test ? test->toString() : "") + "; " +
           (update ? update->toString() : "") + ") " +
           (body ? body->toString() : "{}");
}

void ForStatement::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- WhileStatement ---
WhileStatement::WhileStatement(SourceLocation loc, ExprPtr t, StmtPtr b)
    : Statement(loc), test(std::move(t)), body(std::move(b)), label() {}

WhileStatement::~WhileStatement() = default;

NodeType WhileStatement::getType() const {
    return NodeType::WHILE_STATEMENT;
}

std::string WhileStatement::toString() const {
    return (label.empty() ? "" : label + ": ") + "while (" + (test ? test->toString() : "nullptr") + ") " +
           (body ? body->toString() : "{}");
}

void WhileStatement::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- ReturnStatement ---
ReturnStatement::ReturnStatement(SourceLocation loc, ExprPtr arg)
    : Statement(loc), argument(std::move(arg)) {}

ReturnStatement::~ReturnStatement() = default;

NodeType ReturnStatement::getType() const {
    return NodeType::RETURN_STATEMENT;
}

std::string ReturnStatement::toString() const {
    return "return" + (argument ? " " + argument->toString() : "") + ";";
}

void ReturnStatement::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- PassStatement ---
PassStatement::PassStatement(SourceLocation loc, ExprPtr arg)
    : Statement(loc), argument(std::move(arg)) {}

PassStatement::~PassStatement() = default;

NodeType PassStatement::getType() const {
    return NodeType::PASS_STATEMENT;
}

std::string PassStatement::toString() const {
    return "pass " + (argument ? argument->toString() : "") + ";";
}

void PassStatement::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- BreakStatement ---
BreakStatement::BreakStatement(SourceLocation loc)
    : Statement(loc), label() {}

BreakStatement::~BreakStatement() = default;

NodeType BreakStatement::getType() const {
    return NodeType::BREAK_STATEMENT;
}

std::string BreakStatement::toString() const {
    return label.empty() ? "break;" : "break " + label + ";";
}

void BreakStatement::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- ContinueStatement ---
ContinueStatement::ContinueStatement(SourceLocation loc)
    : Statement(loc), label() {}

ContinueStatement::~ContinueStatement() = default;

NodeType ContinueStatement::getType() const {
    return NodeType::CONTINUE_STATEMENT;
}

std::string ContinueStatement::toString() const {
    return label.empty() ? "continue;" : "continue " + label + ";";
}

void ContinueStatement::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- GenericParameter ---
GenericParameter::GenericParameter(SourceLocation loc, std::unique_ptr<Identifier> n, std::vector<TypeNodePtr> b)
    : Node(loc), name(std::move(n)), bounds(std::move(b)) {}

NodeType GenericParameter::getType() const {
    return NodeType::GENERIC_PARAMETER;
}

std::string GenericParameter::toString() const {
    std::string str = name ? name->toString() : "";
    if (!bounds.empty()) {
        str += ": ";
        for (size_t i = 0; i < bounds.size(); ++i) {
            if (bounds[i]) str += bounds[i]->toString();
            if (i < bounds.size() - 1) str += " + ";
        }
    }
    return str;
}

void GenericParameter::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- TemplateDeclaration ---
TemplateDeclaration::TemplateDeclaration(SourceLocation loc, std::unique_ptr<Identifier> n, std::vector<std::unique_ptr<GenericParameter>> gp, DeclPtr b)
    : Declaration(loc), name(std::move(n)), genericParams(std::move(gp)), body(std::move(b)) {}

NodeType TemplateDeclaration::getType() const {
    return NodeType::TEMPLATE_DECLARATION;
}

std::string TemplateDeclaration::toString() const {
    std::stringstream ss;
    ss << "template<";
    for (size_t i = 0; i < genericParams.size(); ++i) {
        if (genericParams[i]) ss << genericParams[i]->toString();
        if (i < genericParams.size() - 1) ss << ", ";
    }
    ss << "> " << (body ? body->toString() : "");
    return ss.str();
}

void TemplateDeclaration::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- Module ---
Module::Module(SourceLocation loc, std::vector<StmtPtr> b)
    : Node(loc), body(std::move(b)) {}

NodeType Module::getType() const {
    return NodeType::MODULE;
}

std::string Module::toString() const {
    std::stringstream ss;
    for (const auto& stmt : body) {
        if (stmt) ss << stmt->toString() << "\n";
    }
    return ss.str();
}

void Module::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- IfExpression ---
IfExpression::IfExpression(SourceLocation loc, ExprPtr cond, ExprPtr then_b, ExprPtr else_b)
    : Expression(loc), condition(std::move(cond)), thenBranch(std::move(then_b)), elseBranch(std::move(else_b)) {}

NodeType IfExpression::getType() const {
    return NodeType::IF_EXPRESSION;
}

std::string IfExpression::toString() const {
    return "if (" + (condition ? condition->toString() : "nullptr") + ") { " +
           (thenBranch ? thenBranch->toString() : "nullptr") + " } else { " +
           (elseBranch ? elseBranch->toString() : "nullptr") + " }";
}

void IfExpression::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- UnsafeStatement ---
// ast.hpp: UnsafeStatement(SourceLocation loc, std::unique_ptr<BlockStatement> blockStmt)
// Member in hpp: block
// toString() is already declared in hpp.
std::string UnsafeStatement::toString() const {
    return "freedom " + (block ? block->toString() : "{}");
}

// --- TypeName ---
// ... existing code ...
TypeName::TypeName(SourceLocation loc, std::unique_ptr<Identifier> id, std::vector<TypeNodePtr> args)
    : TypeNode(loc), identifier(std::move(id)), genericArgs(std::move(args)) {}

NodeType TypeName::getType() const {
    return NodeType::TYPE_NAME;
}

std::string TypeName::toString() const {
    std::string str = identifier ? identifier->toString() : "UnknownIdentifier";
    if (!genericArgs.empty()) {
        str += "<";
        for (size_t i = 0; i < genericArgs.size(); ++i) {
            if (genericArgs[i]) str += genericArgs[i]->toString();
            if (i < genericArgs.size() - 1) str += ", ";
        }
        str += ">";
    }
    return str;
}

void TypeName::accept(Visitor& visitor) {
    visitor.visit(this);
}

bool TypeName::isIntegerTy() const {
    // Integer types use capitalized names: Int, Int8, Int16, Int32, Int64, UInt, UInt8, etc.
    if (identifier && (identifier->name == "Int" || identifier->name == "Int8" || identifier->name == "Int16" || identifier->name == "Int32" || identifier->name == "Int64" ||
                       identifier->name == "UInt" || identifier->name == "UInt8" || identifier->name == "UInt16" || identifier->name == "UInt32" || identifier->name == "UInt64")) {
        return true;
    }
    return false;
}

std::unique_ptr<TypeNode> TypeName::clone() const {
    std::vector<TypeNodePtr> clonedArgs;
    for (const auto& arg : genericArgs) {
        if (arg) {
            clonedArgs.push_back(arg->clone());
        } else {
            clonedArgs.push_back(nullptr);
        }
    }
    return std::make_unique<TypeName>(loc, identifier ? std::make_unique<Identifier>(identifier->loc, identifier->name) : nullptr, std::move(clonedArgs));
}

// --- PointerType ---
PointerType::PointerType(SourceLocation loc, TypeNodePtr pointee)
    : TypeNode(loc), pointeeType(std::move(pointee)) {}

NodeType PointerType::getType() const {
    return NodeType::POINTER_TYPE;
}

std::string PointerType::toString() const {
    return "ptr<" + (pointeeType ? pointeeType->toString() : "UnknownType") + ">";
}

void PointerType::accept(Visitor& visitor) {
    visitor.visit(this);
}

std::unique_ptr<TypeNode> PointerType::clone() const {
    return std::make_unique<PointerType>(loc, pointeeType ? pointeeType->clone() : nullptr);
}

// --- ArrayType ---
ArrayType::ArrayType(SourceLocation loc, TypeNodePtr et, ExprPtr se)
    : TypeNode(loc), elementType(std::move(et)), sizeExpression(std::move(se)) {}

NodeType ArrayType::getType() const {
    return NodeType::ARRAY_TYPE;
}

std::string ArrayType::toString() const {
    return "[" + (elementType ? elementType->toString() : "UnknownType") +
           (sizeExpression ? "; " + sizeExpression->toString() : "") + "]";
}

void ArrayType::accept(Visitor& visitor) {
    visitor.visit(this);
}

std::unique_ptr<TypeNode> ArrayType::clone() const {
    // Clone the size expression if it exists
    ExprPtr clonedSizeExpr = nullptr;
    if (sizeExpression) {
        // For now, handle only IntegerLiteral size expressions
        if (auto* intLit = dynamic_cast<IntegerLiteral*>(sizeExpression.get())) {
            clonedSizeExpr = std::make_unique<IntegerLiteral>(intLit->loc, intLit->value);
        } else {
            // For more complex expressions, we'll skip cloning for now
            // This preserves the original behavior for complex size expressions
            clonedSizeExpr = nullptr;
        }
    }

    return std::make_unique<ArrayType>(
        loc,
        elementType ? elementType->clone() : nullptr,
        std::move(clonedSizeExpr)
    );
}

// --- VecType ---
VecType::VecType(SourceLocation loc, TypeNodePtr et)
    : TypeNode(loc), elementType(std::move(et)) {}

NodeType VecType::getType() const {
    return NodeType::VEC_TYPE;
}

std::string VecType::toString() const {
    return "Vec<" + (elementType ? elementType->toString() : "UnknownType") + ">";
}

void VecType::accept(Visitor& visitor) {
    visitor.visit(this);
}

std::unique_ptr<TypeNode> VecType::clone() const {
    return std::make_unique<VecType>(
        loc,
        elementType ? elementType->clone() : nullptr
    );
}

// --- FutureType ---
FutureType::FutureType(SourceLocation loc, TypeNodePtr rt)
    : TypeNode(loc), resultType(std::move(rt)) {}

NodeType FutureType::getType() const {
    return NodeType::FUTURE_TYPE;
}

std::string FutureType::toString() const {
    return "Future<" + (resultType ? resultType->toString() : "UnknownType") + ">";
}

void FutureType::accept(Visitor& visitor) {
    visitor.visit(this);
}

std::unique_ptr<TypeNode> FutureType::clone() const {
    return std::make_unique<FutureType>(
        loc,
        resultType ? resultType->clone() : nullptr
    );
}

// --- FunctionType ---
FunctionType::FunctionType(SourceLocation loc, std::vector<TypeNodePtr> pt, TypeNodePtr rt)
    : TypeNode(loc), parameterTypes(std::move(pt)), returnType(std::move(rt)) {}

NodeType FunctionType::getType() const {
    return NodeType::FUNCTION_TYPE;
}

std::string FunctionType::toString() const {
    std::string str = "fn(";
    for (size_t i = 0; i < parameterTypes.size(); ++i) {
        if (parameterTypes[i]) str += parameterTypes[i]->toString();
        if (i < parameterTypes.size() - 1) str += ", ";
    }
    str += ") -> " + (returnType ? returnType->toString() : "void");
    return str;
}

void FunctionType::accept(Visitor& visitor) {
    visitor.visit(this);
}

std::unique_ptr<TypeNode> FunctionType::clone() const {
    std::vector<TypeNodePtr> clonedParams;
    for (const auto& param : parameterTypes) {
        if (param) {
            clonedParams.push_back(param->clone());
        } else {
            clonedParams.push_back(nullptr);
        }
    }
    return std::make_unique<FunctionType>(loc, std::move(clonedParams), returnType ? returnType->clone() : nullptr);
}

// --- OptionalType ---
OptionalType::OptionalType(SourceLocation loc, TypeNodePtr ct)
    : TypeNode(loc), containedType(std::move(ct)) {}

NodeType OptionalType::getType() const {
    return NodeType::OPTIONAL_TYPE;
}

std::string OptionalType::toString() const {
    return (containedType ? containedType->toString() : "UnknownType") + "?";
}

void OptionalType::accept(Visitor& visitor) {
    visitor.visit(this);
}

std::unique_ptr<TypeNode> OptionalType::clone() const {
    return std::make_unique<OptionalType>(loc, containedType ? containedType->clone() : nullptr);
}

// --- TupleTypeNode ---
TupleTypeNode::TupleTypeNode(SourceLocation loc, std::vector<TypeNodePtr> mt)
    : TypeNode(loc), memberTypes(std::move(mt)) {}

NodeType TupleTypeNode::getType() const {
    return NodeType::TUPLE_TYPE;
}

std::string TupleTypeNode::toString() const {
    std::string str = "(";
    for (size_t i = 0; i < memberTypes.size(); ++i) {
        if (memberTypes[i]) str += memberTypes[i]->toString();
        if (i < memberTypes.size() - 1) str += ", ";
    }
    str += ")";
    return str;
}

void TupleTypeNode::accept(Visitor& visitor) {
    visitor.visit(this);
}

std::unique_ptr<TypeNode> TupleTypeNode::clone() const {
    std::vector<TypeNodePtr> clonedMembers;
    for (const auto& member : memberTypes) {
        if (member) {
            clonedMembers.push_back(member->clone());
        } else {
            clonedMembers.push_back(nullptr);
        }
    }
    return std::make_unique<TupleTypeNode>(loc, std::move(clonedMembers));
}

// --- ImportDeclaration ---
ImportDeclaration::ImportDeclaration(SourceLocation loc_param,
                                     ImportKind kind_param,
                                     std::unique_ptr<StringLiteral> source_param,
                                     std::unique_ptr<StringLiteral> locator_param,
                                     std::vector<ImportSpecifier> specifiers_param,
                                     std::unique_ptr<Identifier> defaultImport_param,
                                     std::unique_ptr<Identifier> namespaceImport_param)
    : Declaration(loc_param),
      kind(kind_param),
      source(std::move(source_param)),
      locator(std::move(locator_param)),
      specifiers(std::move(specifiers_param)),
      defaultImport(std::move(defaultImport_param)),
      namespaceImport(std::move(namespaceImport_param)) {}

NodeType ImportDeclaration::getType() const {
    return NodeType::IMPORT_DECLARATION;
}

std::string ImportDeclaration::toString() const {
    std::string result = (kind == ImportKind::Smuggle) ? "smuggle " : "import ";
    if (source) {
        result += source->value;
    }
    if (!specifiers.empty() && specifiers[0].localName) {
        result += " as " + specifiers[0].localName->toString();
    }
    if (locator) {
        result += " from \"" + locator->value + "\"";
    }
    result += ";";
    return result;
}

void ImportDeclaration::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- VariableDeclaration ---
// ... existing code ...
VariableDeclaration::VariableDeclaration(SourceLocation loc, std::unique_ptr<Identifier> i, bool is_const, TypeNodePtr type_node, std::shared_ptr<Expression> val_init)
    : Declaration(loc), id(std::move(i)), isConst(is_const), typeNode(std::move(type_node)), init(val_init) {}

NodeType VariableDeclaration::getType() const {
    return NodeType::VARIABLE_DECLARATION;
}

std::string VariableDeclaration::toString() const {
    std::string str = isConst ? "let " : "var ";
    str += id ? id->toString() : "";
    if (typeNode) {
        str += ": " + typeNode->toString();
    }
    if (init) {
        str += " = " + init->toString();
    }
    str += ";";
    return str;
}

void VariableDeclaration::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- FunctionDeclaration ---
// ... existing code ...
FunctionDeclaration::FunctionDeclaration(SourceLocation loc, std::unique_ptr<Identifier> i, std::vector<FunctionParameter> ps, std::unique_ptr<BlockStatement> b, bool is_async, TypeNodePtr ret_type_node, bool has_default_impl, std::vector<std::unique_ptr<GenericParameter>> gps, bool variadic)
    : Declaration(loc), id(std::move(i)), genericParams(std::move(gps)), params(std::move(ps)), body(std::move(b)), isAsync(is_async), hasDefaultImpl(has_default_impl), variadic(variadic), returnTypeNode(std::move(ret_type_node)) {}

NodeType FunctionDeclaration::getType() const {
    return NodeType::FUNCTION_DECLARATION;
}

std::string FunctionDeclaration::toString() const {
    std::stringstream ss;
    if (isAsync) ss << "async ";
    ss << "fn";

    // Add return type in angle brackets (correct Vyb syntax)
    if (returnTypeNode) {
        ss << "<" << returnTypeNode->toString() << ">";
    }

    ss << " " << (id ? id->toString() : "") << "(";
    for (size_t i = 0; i < params.size(); ++i) {
        ss << params[i].name->toString();
        if (params[i].typeNode) {
            ss << ": " << params[i].typeNode->toString();
        }
        if (i < params.size() - 1) ss << ", ";
    }
    ss << ") -> " << (body ? body->toString() : "{}");
    return ss.str();
}

void FunctionDeclaration::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- TypeAliasDeclaration ---
// ... existing code ...
TypeAliasDeclaration::TypeAliasDeclaration(SourceLocation loc, std::unique_ptr<Identifier> n, TypeNodePtr tn)
    : Declaration(loc), name(std::move(n)), typeNode(std::move(tn)) {}

NodeType TypeAliasDeclaration::getType() const {
    return NodeType::TYPE_ALIAS_DECLARATION;
}

std::string TypeAliasDeclaration::toString() const {
    return "type " + (name ? name->toString() : "") + " = " + (typeNode ? typeNode->toString() : "UnknownType") + ";";
}

void TypeAliasDeclaration::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- FieldDeclaration ---
// ... existing code ...
FieldDeclaration::FieldDeclaration(SourceLocation loc, std::unique_ptr<Identifier> n, TypeNodePtr tn, ExprPtr init_val, bool is_mut)
    : Declaration(loc), name(std::move(n)), typeNode(std::move(tn)), initializer(std::move(init_val)), isMutable(is_mut) {}

NodeType FieldDeclaration::getType() const {
    return NodeType::FIELD_DECLARATION;
}

std::string FieldDeclaration::toString() const {
    std::string str = (isMutable ? "mut " : "") + (name ? name->toString() : "");
    if (typeNode) {
        str += ": " + typeNode->toString();
    }
    if (initializer) {
        str += " = " + initializer->toString();
    }
    str += ";";
    return str;
}

void FieldDeclaration::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- StructDeclaration ---
// ... existing code ...
StructDeclaration::StructDeclaration(SourceLocation loc, std::unique_ptr<Identifier> n, std::vector<std::unique_ptr<GenericParameter>> gp, std::vector<std::unique_ptr<FieldDeclaration>> flds, bool rc)
    : Declaration(loc), name(std::move(n)), genericParams(std::move(gp)), fields(std::move(flds)), reprC(rc) {}

NodeType StructDeclaration::getType() const {
    return NodeType::STRUCT_DECLARATION;
}

std::string StructDeclaration::toString() const {
    std::stringstream ss;
    if (reprC) {
        ss << "#[repr(C)]\n";
    }
    ss << "struct " << (name ? name->toString() : "");
    if (!genericParams.empty()) {
        ss << "<";
        for (size_t i = 0; i < genericParams.size(); ++i) {
            if (genericParams[i]) ss << genericParams[i]->toString();
            if (i < genericParams.size() - 1) ss << ", ";
        }
        ss << ">";
    }
    ss << " {\n";
    for (const auto& field : fields) {
        if (field) {
            // Basic indentation
            std::string fieldStr = field->toString();
            std::string line;
            std::stringstream fieldStream(fieldStr);
            while (std::getline(fieldStream, line)) {
                ss << "  " << line << "\n";
            }
        }
    }
    ss << "}";
    return ss.str();
}

void StructDeclaration::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- ClassDeclaration ---
// ... existing code ...
ClassDeclaration::ClassDeclaration(SourceLocation loc, std::unique_ptr<Identifier> n, std::vector<std::unique_ptr<GenericParameter>> gp, std::vector<DeclPtr> mems)
    : Declaration(loc), name(std::move(n)), genericParams(std::move(gp)), members(std::move(mems)) {}

NodeType ClassDeclaration::getType() const {
    return NodeType::CLASS_DECLARATION;
}

std::string ClassDeclaration::toString() const {
    std::stringstream ss;
    ss << "class " << (name ? name->toString() : "");
    if (!genericParams.empty()) {
        ss << "<";
        for (size_t i = 0; i < genericParams.size(); ++i) {
            if (genericParams[i]) ss << genericParams[i]->toString();
            if (i < genericParams.size() - 1) ss << ", ";
        }
        ss << ">";
    }
    ss << " {\n";
    for (const auto& member : members) {
        if (member) {
            // Basic indentation
            std::string memberStr = member->toString();
            std::string line;
            std::stringstream memberStream(memberStr);
            while (std::getline(memberStream, line)) {
                ss << "  " << line << "\n";
            }
        }
    }
    ss << "}";
    return ss.str();
}

void ClassDeclaration::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- BindDeclaration ---
// ... existing code ...
BindDeclaration::BindDeclaration(SourceLocation loc, TypeNodePtr self_ty, std::vector<AssociatedTypeBinding> assoc_type_bindings, std::vector<std::unique_ptr<FunctionDeclaration>> meths, std::unique_ptr<Identifier> n, std::vector<std::unique_ptr<GenericParameter>> gp, TypeNodePtr trait_ty)
    : Declaration(loc), selfType(std::move(self_ty)), associatedTypeBindings(std::move(assoc_type_bindings)), methods(std::move(meths)), name(std::move(n)), genericParams(std::move(gp)), traitType(std::move(trait_ty)) {}

NodeType BindDeclaration::getType() const {
    return NodeType::BIND_DECLARATION;
}

std::string BindDeclaration::toString() const {
    std::stringstream ss;
    ss << "bind";
    if (!genericParams.empty()) {
        ss << "<";
        for (size_t i = 0; i < genericParams.size(); ++i) {
            if (genericParams[i]) ss << genericParams[i]->toString();
            if (i < genericParams.size() - 1) ss << ", ";
        }
        ss << ">";
    }
    if (traitType) {
        ss << " " << traitType->toString();
    }
    ss << " -> " << (selfType ? selfType->toString() : "UnknownType");
    if (name) {
        ss << " as " << name->toString();
    }
    ss << " {\n";
    for (const auto& assocBinding : associatedTypeBindings) {
        if (assocBinding.name && assocBinding.valueType) {
            ss << "  type " << assocBinding.name->toString() << " = " << assocBinding.valueType->toString() << "\n";
        }
    }
    for (const auto& method : methods) {
        if (method) {
            // Basic indentation
            std::string methodStr = method->toString();
            std::string line;
            std::stringstream methodStream(methodStr);
            while (std::getline(methodStream, line)) {
                ss << "  " << line << "\n";
            }
        }
    }
    ss << "}";
    return ss.str();
}

void BindDeclaration::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- EnumVariant ---
// ... existing code ...
EnumVariant::EnumVariant(SourceLocation loc, std::unique_ptr<Identifier> n, std::vector<TypeNodePtr> assoc_types)
    : Node(loc), name(std::move(n)), associatedTypes(std::move(assoc_types)) {}

NodeType EnumVariant::getType() const {
    return NodeType::ENUM_VARIANT;
}

std::string EnumVariant::toString() const {
    std::string str = name ? name->toString() : "";
    if (!associatedTypes.empty()) {
        str += "(";
        for (size_t i = 0; i < associatedTypes.size(); ++i) {
            if (associatedTypes[i]) str += associatedTypes[i]->toString();
            if (i < associatedTypes.size() - 1) str += ", ";
        }
        str += ")";
    }
    if (hasValue) {
        str += " = " + std::to_string(value);
    }
    return str;
}

void EnumVariant::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- EnumDeclaration ---
// ... existing code ...
EnumDeclaration::EnumDeclaration(SourceLocation loc, std::unique_ptr<Identifier> n, std::vector<std::unique_ptr<GenericParameter>> gp, std::vector<std::unique_ptr<EnumVariant>> vars)
    : Declaration(loc), name(std::move(n)), genericParams(std::move(gp)), variants(std::move(vars)) {}

NodeType EnumDeclaration::getType() const {
    return NodeType::ENUM_DECLARATION;
}

std::string EnumDeclaration::toString() const {
    std::stringstream ss;
    ss << "enum " << (name ? name->toString() : "");
    if (!genericParams.empty()) {
        ss << "<";
        for (size_t i = 0; i < genericParams.size(); ++i) {
            if (genericParams[i]) ss << genericParams[i]->toString();
            if (i < genericParams.size() - 1) ss << ", ";
        }
        ss << ">";
    }
    ss << " {\n";
    for (size_t i = 0; i < variants.size(); ++i) {
        if (variants[i]) {
            // Basic indentation
            std::string variantStr = variants[i]->toString();
            std::string line;
            std::stringstream variantStream(variantStr);
            while (std::getline(variantStream, line)) {
                ss << "  " << line;
            }
        }
        if (i < variants.size() - 1) ss << ",";
        ss << "\n";
    }
    ss << "}";
    return ss.str();
}

void EnumDeclaration::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- LogicalExpression ---
LogicalExpression::LogicalExpression(SourceLocation loc, ExprPtr left, const token::Token& op, ExprPtr right)
    : Expression(loc), left(std::move(left)), op(op), right(std::move(right)) {}

NodeType LogicalExpression::getType() const {
    return NodeType::LOGICAL_EXPRESSION;
}

std::string LogicalExpression::toString() const {
    return "(" + (left ? left->toString() : "nullptr") + " " + op.lexeme + " " + (right ? right->toString() : "nullptr") + ")";
}

void LogicalExpression::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- ConditionalExpression ---
ConditionalExpression::ConditionalExpression(SourceLocation loc, ExprPtr condition, ExprPtr thenExpr, ExprPtr elseExpr)
    : Expression(loc), condition(std::move(condition)), thenExpr(std::move(thenExpr)), elseExpr(std::move(elseExpr)) {}

NodeType ConditionalExpression::getType() const {
    return NodeType::CONDITIONAL_EXPRESSION;
}

std::string ConditionalExpression::toString() const {
    return "(" + (condition ? condition->toString() : "nullptr") + " ? " + (thenExpr ? thenExpr->toString() : "nullptr") + " : " + (elseExpr ? elseExpr->toString() : "nullptr") + ")";
}

void ConditionalExpression::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- SequenceExpression ---
SequenceExpression::SequenceExpression(SourceLocation loc, std::vector<ExprPtr> expressions)
    : Expression(loc), expressions(std::move(expressions)) {}

NodeType SequenceExpression::getType() const {
    return NodeType::SEQUENCE_EXPRESSION;
}

std::string SequenceExpression::toString() const {
    std::string str = "(";
    for (size_t i = 0; i < expressions.size(); ++i) {
        str += expressions[i] ? expressions[i]->toString() : "nullptr";
        if (i < expressions.size() - 1) str += ", ";
    }
    str += ")";
    return str;
}

void SequenceExpression::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- FunctionExpression ---
FunctionExpression::FunctionExpression(SourceLocation loc, std::vector<FunctionParameter> params, ExprPtr body, bool isAsync)
    : Expression(loc), params(std::move(params)), body(std::move(body)), isAsync(isAsync) {}

NodeType FunctionExpression::getType() const {
    return NodeType::FUNCTION_EXPRESSION;
}

std::string FunctionExpression::toString() const {
    std::string str = (isAsync ? std::string("async ") : std::string("")) + "fn(";
    for (size_t i = 0; i < params.size(); ++i) {
        str += params[i].name ? params[i].name->toString() : "_";
        if (params[i].typeNode) str += ": " + params[i].typeNode->toString();
        if (i < params.size() - 1) str += ", ";
    }
    str += ") => ";
    str += body ? body->toString() : "nullptr";
    return str;
}

void FunctionExpression::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- ThisExpression ---
ThisExpression::ThisExpression(SourceLocation loc)
    : Expression(loc) {}

NodeType ThisExpression::getType() const {
    return NodeType::THIS_EXPRESSION;
}

std::string ThisExpression::toString() const {
    return "this";
}

void ThisExpression::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- SuperExpression ---
SuperExpression::SuperExpression(SourceLocation loc)
    : Expression(loc) {}

NodeType SuperExpression::getType() const {
    return NodeType::SUPER_EXPRESSION;
}

std::string SuperExpression::toString() const {
    return "super";
}

void SuperExpression::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- AwaitExpression ---
AwaitExpression::AwaitExpression(SourceLocation loc, ExprPtr expr)
    : Expression(loc), expr(std::move(expr)) {}

NodeType AwaitExpression::getType() const {
    return NodeType::AWAIT_EXPRESSION;
}

std::string AwaitExpression::toString() const {
    return "await " + (expr ? expr->toString() : "nullptr");
}

void AwaitExpression::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- RangeExpression ---
RangeExpression::RangeExpression(SourceLocation loc, ExprPtr start, ExprPtr end, ExprPtr step)
    : Expression(loc), start(std::move(start)), end(std::move(end)), step(std::move(step)) {}

NodeType RangeExpression::getType() const {
    return NodeType::RANGE_EXPRESSION;
}

std::string RangeExpression::toString() const {
    std::string result = (start ? start->toString() : "nullptr") + ".." + (end ? end->toString() : "nullptr");
    if (step) {
        result += ", " + step->toString();
    }
    return result;
}

void RangeExpression::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- BlockExpression ---
BlockExpression::BlockExpression(SourceLocation loc, std::unique_ptr<BlockStatement> block,
                                 std::vector<std::unique_ptr<TrapClause>> trapClauses,
                                 std::unique_ptr<EnsureClause> ensureClause)
    : Expression(loc), block(std::move(block)), trapClauses(std::move(trapClauses)),
      ensureClause(std::move(ensureClause)) {}

NodeType BlockExpression::getType() const {
    return NodeType::BLOCK_EXPRESSION;
}

std::string BlockExpression::toString() const {
    std::string result = "{ " + (block ? block->toString() : "nullptr") + " }";
    for (const auto& trap : trapClauses) {
        result += " " + trap->toString();
    }
    if (ensureClause) {
        result += " " + ensureClause->toString();
    }
    return result;
}

void BlockExpression::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- SelectExpression ---
SelectExpression::SelectExpression(SourceLocation loc, ExprPtr expr, std::vector<std::pair<ExprPtr, ExprPtr>> cases)
    : Expression(loc), expr(std::move(expr)), cases(std::move(cases)) {}

NodeType SelectExpression::getType() const {
    return NodeType::SELECT_EXPRESSION;
}

std::string SelectExpression::toString() const {
    std::string result = "select(" + (expr ? expr->toString() : "nullptr") + ") { ";
    for (const auto& [pattern, value] : cases) {
        result += (pattern ? pattern->toString() : "?") + " -> " + (value ? value->toString() : "nullptr") + ", ";
    }
    result += "}";
    return result;
}

void SelectExpression::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- ComparisonPattern ---
ComparisonPattern::ComparisonPattern(SourceLocation loc, token::Token op, ExprPtr value)
    : Expression(loc), op(op), value(std::move(value)) {}

NodeType ComparisonPattern::getType() const {
    return NodeType::COMPARISON_PATTERN;
}

std::string ComparisonPattern::toString() const {
    return op.lexeme + " " + (value ? value->toString() : "nullptr");
}

void ComparisonPattern::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- StructPattern ---
StructPattern::StructPattern(SourceLocation loc, TypeNodePtr typeName, std::vector<std::unique_ptr<Identifier>> bindings)
    : Expression(loc), typeName(std::move(typeName)), bindings(std::move(bindings)) {}

NodeType StructPattern::getType() const {
    return NodeType::STRUCT_PATTERN;
}

std::string StructPattern::toString() const {
    std::string str = (typeName ? typeName->toString() : "?") + " { ";
    for (size_t i = 0; i < bindings.size(); ++i) {
        if (i) str += ", ";
        str += (bindings[i] ? bindings[i]->name : "?");
    }
    str += " }";
    return str;
}

void StructPattern::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- SetPattern ---
SetPattern::SetPattern(SourceLocation loc, std::vector<ExprPtr> elements)
    : Expression(loc), elements(std::move(elements)) {}

NodeType SetPattern::getType() const {
    return NodeType::SET_PATTERN;
}

std::string SetPattern::toString() const {
    std::string str = "{ ";
    for (size_t i = 0; i < elements.size(); ++i) {
        if (i) str += ", ";
        str += elements[i] ? elements[i]->toString() : "?";
    }
    str += " }";
    return str;
}

void SetPattern::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- TypeofExpression ---
TypeofExpression::TypeofExpression(SourceLocation loc, ExprPtr operand)
    : Expression(loc), operand(std::move(operand)), operandFromWildcardError(false) {}

TypeofExpression::TypeofExpression(SourceLocation loc, TypeNodePtr typeArg)
    : Expression(loc), typeArg(std::move(typeArg)), operandFromWildcardError(false) {}

NodeType TypeofExpression::getType() const {
    return NodeType::TYPEOF_EXPRESSION;
}

std::string TypeofExpression::toString() const {
    if (typeArg) {
        return "typeof<" + typeArg->toString() + ">()";
    }
    return "typeof(" + (operand ? operand->toString() : "nullptr") + ")";
}

void TypeofExpression::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- TypenameExpression ---
TypenameExpression::TypenameExpression(SourceLocation loc, ExprPtr operand)
    : Expression(loc), operand(std::move(operand)), operandFromWildcardError(false),
      operandFromTypeValue(false) {}

NodeType TypenameExpression::getType() const {
    return NodeType::TYPENAME_EXPRESSION;
}

std::string TypenameExpression::toString() const {
    return "typename(" + (operand ? operand->toString() : "nullptr") + ")";
}

void TypenameExpression::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- AsExpression ---
NodeType AsExpression::getType() const {
    return NodeType::AS_EXPRESSION;
}

std::string AsExpression::toString() const {
    return (operand ? operand->toString() : "nullptr") + " as " +
           (targetType ? targetType->toString() : "nullptr");
}

void AsExpression::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- ThrowStatement ---
ThrowStatement::ThrowStatement(SourceLocation loc, ExprPtr expr)
    : Statement(loc), expr(std::move(expr)) {}

NodeType ThrowStatement::getType() const {
    return NodeType::THROW_STATEMENT;
}

std::string ThrowStatement::toString() const {
    return "throw " + (expr ? expr->toString() : "nullptr");
}

void ThrowStatement::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- MatchStatement ---
MatchStatement::MatchStatement(SourceLocation loc, ExprPtr expr, std::vector<std::pair<ExprPtr, ExprPtr>> cases,
                               std::vector<ExprPtr> guards)
    : Statement(loc), expr(std::move(expr)), cases(std::move(cases)), guards(std::move(guards)) {}

NodeType MatchStatement::getType() const {
    return NodeType::MATCH_STATEMENT;
}

std::string MatchStatement::toString() const {
    std::string str = "match " + (expr ? expr->toString() : "nullptr") + " { ";
    for (size_t i = 0; i < cases.size(); ++i) {
        str += (cases[i].first ? cases[i].first->toString() : "_");
        if (i < guards.size() && guards[i]) {
            str += " if " + guards[i]->toString();
        }
        str += " => " + (cases[i].second ? cases[i].second->toString() : "nullptr") + "; ";
    }
    str += "}";
    return str;
}

void MatchStatement::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- MatchExpression ---
MatchExpression::MatchExpression(SourceLocation loc, std::unique_ptr<MatchStatement> match)
    : Expression(loc), match(std::move(match)) {}

NodeType MatchExpression::getType() const {
    return NodeType::MATCH_EXPRESSION;
}

std::string MatchExpression::toString() const {
    return (match ? match->toString() : "match <missing>");
}

void MatchExpression::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- AspectDeclaration ---
AspectDeclaration::AspectDeclaration(SourceLocation loc, std::unique_ptr<Identifier> n, std::vector<std::unique_ptr<GenericParameter>> gp, std::vector<std::unique_ptr<Identifier>> sup_types, std::vector<std::unique_ptr<Identifier>> assoc_types, std::vector<TypeNodePtr> assoc_defaults, std::vector<std::vector<TypeNodePtr>> assoc_constraints, std::vector<std::unique_ptr<FunctionDeclaration>> meths)
    : Declaration(loc), name(std::move(n)), genericParams(std::move(gp)), superTypes(std::move(sup_types)), associatedTypes(std::move(assoc_types)), associatedTypeDefaults(std::move(assoc_defaults)), associatedTypeConstraints(std::move(assoc_constraints)), methods(std::move(meths)) {}

NodeType AspectDeclaration::getType() const {
    return NodeType::ASPECT_DECLARATION;
}

std::string AspectDeclaration::toString() const {
    std::stringstream ss;
    ss << "aspect " << (name ? name->toString() : "");
    if (!superTypes.empty()) {
        ss << " : ";
        for (size_t i = 0; i < superTypes.size(); ++i) {
            if (i > 0) ss << ", ";
            if (superTypes[i]) ss << superTypes[i]->toString();
        }
    }
    ss << " {\n";
    for (size_t i = 0; i < associatedTypes.size(); ++i) {
        if (associatedTypes[i]) {
            ss << "  type " << associatedTypes[i]->toString();
            if (i < associatedTypeConstraints.size() && !associatedTypeConstraints[i].empty()) {
                ss << "<";
                for (size_t c = 0; c < associatedTypeConstraints[i].size(); ++c) {
                    if (c > 0) ss << ", ";
                    if (associatedTypeConstraints[i][c]) ss << associatedTypeConstraints[i][c]->toString();
                }
                ss << ">";
            }
            if (i < associatedTypeDefaults.size() && associatedTypeDefaults[i]) {
                ss << " = " << associatedTypeDefaults[i]->toString();
            }
            ss << "\n";
        }
    }
    for (const auto& method : methods) {
        if (method) {
            std::string methodStr = method->toString();
            std::string line;
            std::stringstream methodStream(methodStr);
            while (std::getline(methodStream, line)) {
                ss << "  " << line << "\n";
            }
        }
    }
    ss << "}";
    return ss.str();
}

void AspectDeclaration::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- NamespaceDeclaration ---
NamespaceDeclaration::NamespaceDeclaration(SourceLocation loc, std::unique_ptr<Identifier> n, std::vector<DeclPtr> mems)
    : Declaration(loc), name(std::move(n)), members(std::move(mems)) {}

NodeType NamespaceDeclaration::getType() const {
    return NodeType::NAMESPACE_DECLARATION;
}

std::string NamespaceDeclaration::toString() const {
    std::stringstream ss;
    ss << "namespace " << (name ? name->toString() : "") << " {\n";
    for (const auto& member : members) {
        if (member) {
            std::string memberStr = member->toString();
            std::string line;
            std::stringstream memberStream(memberStr);
            while (std::getline(memberStream, line)) {
                ss << "  " << line << "\n";
            }
        }
    }
    ss << "}";
    return ss.str();
}

void NamespaceDeclaration::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- AssertStatement ---
AssertStatement::AssertStatement(SourceLocation loc, ExprPtr cond, ExprPtr msg)
    : Statement(loc), condition(std::move(cond)), message(std::move(msg)) {}

NodeType AssertStatement::getType() const {
    return NodeType::ASSERT_STATEMENT;
}

std::string AssertStatement::toString() const {
    std::string str = "assert(" + (condition ? condition->toString() : "") + ")";
    if (message) {
        str += ", " + message->toString();
    }
    str += ";";
    return str;
}

void AssertStatement::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- ExternStatement ---
ExternStatement::ExternStatement(
    SourceLocation loc,
    std::unique_ptr<Identifier> name,
    TypeNodePtr returnType,
    std::vector<FunctionParameter> parameters
) : Statement(loc),
    name(std::move(name)),
    returnType(std::move(returnType)),
    parameters(std::move(parameters)) {}

NodeType ExternStatement::getType() const {
    return NodeType::EXTERN_STATEMENT;
}

std::string ExternStatement::toString() const {
    std::stringstream ss;
    ss << "extern " << (name ? name->toString() : "");

    // Add parameters if this is a function declaration
    if (!parameters.empty()) {
        ss << "(";
        for (size_t i = 0; i < parameters.size(); ++i) {
            if (parameters[i].typeNode) {
                ss << parameters[i].name->toString() << ": " << parameters[i].typeNode->toString();
            } else {
                ss << parameters[i].name->toString();
            }

            if (i < parameters.size() - 1) {
                ss << ", ";
            }
        }
        ss << ")";
    }

    // Add return type if specified
    if (returnType) {
        ss << " -> " << returnType->toString();
    }

    ss << ";";
    return ss.str();
}

void ExternStatement::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- YieldStatement ---
YieldStatement::YieldStatement(SourceLocation loc, ExprPtr expression)
    : Statement(loc), expression(std::move(expression)) {}

NodeType YieldStatement::getType() const {
    return NodeType::YIELD_STATEMENT;
}

std::string YieldStatement::toString() const {
    std::stringstream ss;
    ss << "yield";
    if (expression) {
        ss << " " << expression->toString();
    }
    ss << ";";
    return ss.str();
}

void YieldStatement::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- YieldReturnStatement ---
YieldReturnStatement::YieldReturnStatement(SourceLocation loc, ExprPtr expression)
    : Statement(loc), expression(std::move(expression)) {}

NodeType YieldReturnStatement::getType() const {
    return NodeType::YIELD_RETURN_STATEMENT;
}

std::string YieldReturnStatement::toString() const {
    std::stringstream ss;
    ss << "yield return";
    if (expression) {
        ss << " " << expression->toString();
    }
    ss << ";";
    return ss.str();
}

void YieldReturnStatement::accept(Visitor& visitor) {
    visitor.visit(this);
}

// --- Error Handling Statements ---

// FailStatement implementation
FailStatement::FailStatement(SourceLocation loc, ExprPtr error, TypeNodePtr errorType)
    : Statement(loc), error(std::move(error)), errorType(std::move(errorType)) {}

NodeType FailStatement::getType() const {
    return NodeType::FAIL_STATEMENT;
}

std::string FailStatement::toString() const {
    std::stringstream ss;
    ss << "fail";
    if (errorType) {
        ss << "<" << errorType->toString() << ">";
    }
    ss << " " << (error ? error->toString() : "");
    return ss.str();
}

void FailStatement::accept(Visitor& visitor) {
    visitor.visit(this);
}

// TrapClause implementation
TrapClause::TrapClause(SourceLocation loc, std::unique_ptr<Identifier> errorName,
                       TypeNodePtr errorType, StmtPtr handler, bool isWildcard, bool isMultiType)
    : Node(loc), errorName(std::move(errorName)),
      errorType(std::move(errorType)), handler(std::move(handler)),
      isWildcard(isWildcard), isMultiType(isMultiType) {}

NodeType TrapClause::getType() const {
    return NodeType::TRAP_CLAUSE;
}

std::string TrapClause::toString() const {
    std::stringstream ss;
    ss << "trap (";
    if (errorName) {
        ss << errorName->toString();
    }
    if (isWildcard) {
        ss << "<?>";
    } else if (isMultiType && !errorTypes.empty()) {
        ss << "<";
        for (size_t i = 0; i < errorTypes.size(); ++i) {
            if (i > 0) ss << " | ";
            ss << errorTypes[i]->toString();
        }
        ss << ">";
    } else if (errorType) {
        ss << "<" << errorType->toString() << ">";
    }
    ss << ") -> ";
    if (handler) {
        ss << handler->toString();
    }
    return ss.str();
}

void TrapClause::accept(Visitor& visitor) {
    visitor.visit(this);
}

// EnsureClause implementation
EnsureClause::EnsureClause(SourceLocation loc, StmtPtr cleanupBlock)
    : Node(loc), cleanupBlock(std::move(cleanupBlock)) {}

NodeType EnsureClause::getType() const {
    return NodeType::ENSURE_CLAUSE;
}

std::string EnsureClause::toString() const {
    std::stringstream ss;
    ss << "ensure -> ";
    if (cleanupBlock) {
        ss << cleanupBlock->toString();
    }
    return ss.str();
}

void EnsureClause::accept(Visitor& visitor) {
    visitor.visit(this);
}

// RefailStatement implementation
RefailStatement::RefailStatement(SourceLocation loc, ExprPtr wrappedError)
    : Statement(loc), wrappedError(std::move(wrappedError)) {}

NodeType RefailStatement::getType() const {
    return NodeType::REFAIL_STATEMENT;
}

std::string RefailStatement::toString() const {
    if (wrappedError) {
        return "refail " + wrappedError->toString();
    }
    return "refail";
}

void RefailStatement::accept(Visitor& visitor) {
    visitor.visit(this);
}

// PanicStatement implementation
PanicStatement::PanicStatement(SourceLocation loc, ExprPtr message)
    : Statement(loc), message(std::move(message)) {}

NodeType PanicStatement::getType() const {
    return NodeType::PANIC_STATEMENT;
}

std::string PanicStatement::toString() const {
    std::stringstream ss;
    ss << "panic(";
    if (message) {
        ss << message->toString();
    }
    ss << ")";
    return ss.str();
}

void PanicStatement::accept(Visitor& visitor) {
    visitor.visit(this);
}

// ExitStatement implementation
ExitStatement::ExitStatement(SourceLocation loc, ExprPtr code)
    : Statement(loc), code(std::move(code)) {}

NodeType ExitStatement::getType() const {
    return NodeType::EXIT_STATEMENT;
}

std::string ExitStatement::toString() const {
    std::stringstream ss;
    ss << "exit(";
    if (code) {
        ss << code->toString();
    }
    ss << ")";
    return ss.str();
}

void ExitStatement::accept(Visitor& visitor) {
    visitor.visit(this);
}

// DeferStatement implementation
DeferStatement::DeferStatement(SourceLocation loc, StmtPtr statement)
    : Statement(loc), statement(std::move(statement)) {}

NodeType DeferStatement::getType() const {
    return NodeType::DEFER_STATEMENT;
}

std::string DeferStatement::toString() const {
    return "defer " + (statement ? statement->toString() : "<empty>");
}

void DeferStatement::accept(Visitor& visitor) {
    visitor.visit(this);
}


// --- TupleDestructureAssignment implementation ---
TupleDestructureAssignment::TupleDestructureAssignment(SourceLocation loc,
                                                       std::vector<std::unique_ptr<Identifier>> ids,
                                                       ExprPtr expr)
    : Statement(loc), identifiers(std::move(ids)), expression(std::move(expr)) {}

NodeType TupleDestructureAssignment::getType() const {
    return NodeType::TUPLE_DESTRUCTURE_ASSIGNMENT;
}

std::string TupleDestructureAssignment::toString() const {
    std::stringstream ss;
    for (size_t i = 0; i < identifiers.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << identifiers[i]->name;
    }
    ss << " = ";
    if (expression) {
        ss << expression->toString();
    }
    return ss.str();
}

void TupleDestructureAssignment::accept(Visitor& visitor) {
    visitor.visit(this);
}
} // namespace ast
} // namespace vyb
