#include "vyn/parser/ast.hpp"
#include <string> // For std::to_string

namespace vyn {
namespace ast { // Add ast namespace

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

} // namespace ast
} // namespace vyn
