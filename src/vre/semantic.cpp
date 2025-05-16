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
        case NodeType::BORROW_EXPRESSION_NODE:
            analyzeBorrowExpression(static_cast<BorrowExprNode*>(node));
            break;
        case NodeType::BLOCK_STATEMENT:
            analyzeBlockStatement(static_cast<BlockStatement*>(node));
            break;
        // ... add more as needed ...
        default:
            break;
    }
}

void SemanticAnalyzer::analyzeBlockStatement(BlockStatement* block) {
    // Enter new scope for borrows and unsafe
    std::vector<BorrowInfo> savedBorrows = activeBorrows;
    int savedUnsafe = unsafeDepth;
    for (const auto& stmt : block->body) {
        // Detect unsafe block entry (syntactic marker not shown here)
        // If block is marked unsafe, call enterUnsafe()/exitUnsafe()
        analyzeNode(stmt.get());
    }
    activeBorrows = savedBorrows;
    unsafeDepth = savedUnsafe;
}

void SemanticAnalyzer::analyzeBorrowExpression(BorrowExprNode* expr) {
    // Determine owner name (assume identifier for now)
    std::string ownerName;
    TypeNode* ownerType = nullptr;
    if (auto* id = dynamic_cast<Identifier*>(expr->expression.get())) {
        ownerName = id->name;
        SymbolInfo* sym = symbols->lookup(ownerName);
        if (sym) ownerType = sym->type;
    }
    // Determine borrow kind
    bool isMutable = (expr->kind == BorrowKind::MUTABLE_BORROW);
    checkBorrow(expr, ownerName, isMutable, ownerType);
    checkLifetime(expr, ownerName);
}

void SemanticAnalyzer::analyzeVariableDeclaration(VariableDeclaration* decl) {
    SymbolInfo sym;
    sym.kind = SymbolInfo::Kind::Variable;
    sym.name = decl->id->name;
    sym.isConst = decl->isConst;
    sym.type = decl->typeNode.get();
    symbols->add(sym);
    // Analyze initializer
    if (decl->init) {
        // Special check: if declared type is loc<T>
        if (decl->typeNode && decl->typeNode->category == TypeNode::TypeCategory::OWNERSHIP_WRAPPED && decl->typeNode->ownership == OwnershipKind::MY /* treat as loc<T> */) {
            // Only allow initializer to be PointerDerefExpression, AddrOfExpression, or FromIntToLocExpression
            auto* ptrDeref = dynamic_cast<PointerDerefExpression*>(decl->init.get());
            auto* addrOf = dynamic_cast<AddrOfExpression*>(decl->init.get());
            auto* fromInt = dynamic_cast<FromIntToLocExpression*>(decl->init.get());
            if (!ptrDeref && !addrOf && !fromInt) {
                errors.push_back("Cannot assign non-location value to loc<T> variable '" + decl->id->name + "' at " + decl->loc.toString());
            }
            // If fromInt, must be in unsafe
            if (fromInt && !inUnsafe()) {
                errors.push_back("from(addr) to loc<T> is only allowed in unsafe block for variable '" + decl->id->name + "' at " + decl->loc.toString());
            }
        }
        analyzeNode(decl->init.get());
    }
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
            unary->op.type == token::TokenType::KEYWORD_AT) {
            checkLocUnsafe(unary);
            if (!isRawLocationType(unary->operand.get())) {
                errors.push_back("Cannot assign to dereferenced value: operand is not a raw location (loc<T>) at " + unary->loc.toString());
            }
        }
    }
    // Analyze right-hand side
    if (expr->right) analyzeNode(expr->right.get());
    // ... handle member/array assignment, type checks, etc ...
}

void SemanticAnalyzer::analyzeUnaryExpression(UnaryExpression* expr) {
    // Check for raw location dereference: loc(expr) or at(expr)
    if (expr->op.type == token::TokenType::KEYWORD_LOC ||
        expr->op.type == token::TokenType::KEYWORD_AT) {
        checkLocUnsafe(expr);
        if (!isRawLocationType(expr->operand.get())) {
            errors.push_back("Cannot dereference: operand is not a raw location (loc<T>) at " + expr->loc.toString());
        }
    }
    // Check for address-of: addr(loc)
    if (expr->op.type == token::TokenType::IDENTIFIER && expr->op.lexeme == "addr") {
        checkLocUnsafe(expr);
        if (!isRawLocationType(expr->operand.get())) {
            errors.push_back("Argument to addr(...) must be a raw location (loc<T>) at " + expr->loc.toString());
        }
    }
    // Analyze operand
    if (expr->operand) analyzeNode(expr->operand.get());
}

// --- Unsafe context helpers ---
void SemanticAnalyzer::enterUnsafe() { ++unsafeDepth; }
void SemanticAnalyzer::exitUnsafe() { if (unsafeDepth > 0) --unsafeDepth; }
bool SemanticAnalyzer::inUnsafe() const { return unsafeDepth > 0; }

// --- Borrow and lifetime checking ---
void SemanticAnalyzer::checkBorrow(Node* node, const std::string& owner, bool isMutable, TypeNode* type) {
    // Check for conflicting borrows
    for (const auto& b : activeBorrows) {
        if (b.ownerName == owner) {
            if (isMutable || b.isMutable) {
                errors.push_back("Conflicting mutable/immutable borrow for owner '" + owner + "' at " + node->loc.toString());
                return;
            }
        }
    }
    // Register this borrow
    activeBorrows.push_back({owner, isMutable, node});
}

void SemanticAnalyzer::checkLifetime(Node* node, const std::string& owner) {
    // TODO: Implement region/lifetime analysis to ensure borrow does not outlive owner
    // For now, just a stub
}

void SemanticAnalyzer::checkLocUnsafe(Node* node) {
    if (!inUnsafe()) {
        errors.push_back("Raw location operation (loc<T>) must be inside unsafe block at " + node->loc.toString());
    }
}

// Helper: returns true if the expression is a variable or value of type loc<T>
bool SemanticAnalyzer::isRawLocationType(Expression* expr) {
    if (auto* id = dynamic_cast<Identifier*>(expr)) {
        SymbolInfo* sym = symbols->lookup(id->name);
        if (sym && sym->type) {
            auto* typeNode = sym->type;
            if (typeNode->category == TypeNode::TypeCategory::OWNERSHIP_WRAPPED &&
                (typeNode->ownership == OwnershipKind::PTR /* legacy */ || typeNode->ownership == OwnershipKind::MY /* treat MY as loc<T> for now if needed */)) {
                return true;
            }
            if (typeNode->category == TypeNode::TypeCategory::IDENTIFIER && typeNode->name && typeNode->name->name == "loc") {
                return true;
            }
        }
    }
    return false;
}
