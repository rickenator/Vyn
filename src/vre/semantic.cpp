#include "vyn/vre/semantic.hpp"
#include <cassert>

using namespace vyn;

SemanticAnalyzer::SemanticAnalyzer() : symbols(std::make_unique<SymbolTable>()) {}

void SemanticAnalyzer::analyze(Module* root) {
    for (const auto& stmt : root->body) {
        analyzeNode(stmt.get());
    }
}

void SemanticAnalyzer::analyzeNode(Node* node) {
    if (!node) return;
    switch (node->getType()) {
        case NodeType::VARIABLE_DECLARATION:
            analyzeVariableDeclaration(static_cast<VariableDeclaration*>(node));
            break;
        case NodeType::ASSIGNMENT_EXPRESSION:
            analyzeAssignment(static_cast<AssignmentExpression*>(node));
            break;
        case NodeType::UNARY_EXPRESSION:
            analyzeUnaryExpression(static_cast<UnaryExpression*>(node));
            break;
        // ... add more as needed ...
        default:
            break;
    }
}

void SemanticAnalyzer::analyzeVariableDeclaration(VariableDeclaration* decl) {
    SymbolInfo sym;
    sym.kind = SymbolInfo::Kind::Variable;
    sym.name = decl->id->name;
    sym.isConst = decl->isConst; // Assume VariableDeclaration has isConst
    sym.type = decl->typeNode.get();
    symbols->add(sym);
}

void SemanticAnalyzer::analyzeAssignment(AssignmentExpression* expr) {
    // Only handle identifier assignment for now
    if (auto* id = dynamic_cast<Identifier*>(expr->left.get())) {
        SymbolInfo* sym = symbols->lookup(id->name);
        if (sym && sym->isConst) {
            errors.push_back("Cannot assign to const variable: " + id->name);
        }
    } else if (auto* unary = dynamic_cast<UnaryExpression*>(expr->left.get())) {
        // Assignment to dereferenced raw location (loc(expr) = ... or at(expr) = ...)
        if (unary->op.type == token::TokenType::KEYWORD_LOC ||
            unary->op.type == token::TokenType::KEYWORD_AT ||
            unary->op.type == token::TokenType::STAR) {
            // Check that the operand is a raw location type (loc<T>)
            // NOTE: This is a placeholder; real type checking should inspect the type of the operand
            // For now, just allow it, but in a real implementation, check type here
            // Example:
            // if (!isRawLocationType(unary->operand.get())) {
            //     errors.push_back("Cannot assign to dereferenced value: operand is not a raw location (loc<T>)");
            // }
        }
    }
    // ... handle member/array assignment, type checks, etc ...
}

void SemanticAnalyzer::analyzeUnaryExpression(UnaryExpression* expr) {
    // Check for raw location dereference: loc(expr) or at(expr)
    if (expr->op.type == token::TokenType::KEYWORD_LOC ||
        expr->op.type == token::TokenType::KEYWORD_AT ||
        expr->op.type == token::TokenType::STAR) {
        // Check that the operand is a raw location type (loc<T>)
        if (!isRawLocationType(expr->operand.get())) {
            errors.push_back("Cannot dereference: operand is not a raw location (loc<T>) at " + expr->loc.toString());
        }
    }
}

// Helper: returns true if the expression is a variable or value of type loc<T>
bool SemanticAnalyzer::isRawLocationType(Expression* expr) {
    // This is a minimal stub. In a real implementation, you would look up the type of the expression.
    // For now, accept identifiers with typeNode of OWNERSHIP_WRAPPED and ownership == PTR or LOC.
    // Here, we only check for identifiers with a typeNode named 'loc' (raw location).
    if (auto* id = dynamic_cast<Identifier*>(expr)) {
        SymbolInfo* sym = symbols->lookup(id->name);
        if (sym && sym->type) {
            auto* typeNode = sym->type;
            if (typeNode->category == TypeNode::TypeCategory::OWNERSHIP_WRAPPED &&
                (typeNode->ownership == OwnershipKind::PTR /* legacy */ || typeNode->ownership == OwnershipKind::MY /* treat MY as loc<T> for now if needed */)) {
                // Accept as raw location
                return true;
            }
            // Accept explicit loc<T> type name (future-proof)
            if (typeNode->category == TypeNode::TypeCategory::IDENTIFIER && typeNode->name && typeNode->name->name == "loc") {
                return true;
            }
        }
    }
    // TODO: Add more robust type inference for general expressions
    return false;
}
