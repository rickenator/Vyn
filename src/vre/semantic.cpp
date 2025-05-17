#include "vyn/vre/semantic.hpp" // Moved to vre subdirectory
#include "vyn/parser/token.hpp"
#include "vyn/parser/ast.hpp" // Added include for AST nodes
#include <stdexcept> // For std::runtime_error

namespace vyn {

void SemanticAnalyzer::analyzeNode(ast::Node* node) { // Changed to ast::Node
    if (!node) return;

    // Pre-visit actions (e.g., enter scope for blocks)
    if (node->getType() == ast::NodeType::BLOCK_STATEMENT) { // Changed to ast::NodeType
        enterScope();
    }

    switch (node->getType()) { // Changed to ast::NodeType
        case ast::NodeType::VARIABLE_DECLARATION: // Changed to ast::NodeType
            analyzeVariableDeclaration(static_cast<ast::VariableDeclaration*>(node)); // Changed to ast::VariableDeclaration
            break;
        case ast::NodeType::ASSIGNMENT_EXPRESSION: // Changed to ast::NodeType
            analyzeAssignment(static_cast<ast::AssignmentExpression*>(node)); // Changed to ast::AssignmentExpression
            break;
        case ast::NodeType::UNARY_EXPRESSION: // Changed to ast::NodeType
            analyzeUnaryExpression(static_cast<ast::UnaryExpression*>(node)); // Changed to ast::UnaryExpression
            break;
        case ast::NodeType::BORROW_EXPRESSION_NODE: // Changed to ast::NodeType
            analyzeBorrowExpression(static_cast<ast::BorrowExprNode*>(node)); // Changed to ast::BorrowExprNode
            break;
        case ast::NodeType::BLOCK_STATEMENT: // Changed to ast::NodeType
            analyzeBlockStatement(static_cast<ast::BlockStatement*>(node)); // Changed to ast::BlockStatement
            break;
        // Add cases for other node types
        default:
            // For nodes with children, recursively call analyzeNode
            // This is a simplified approach; specific node types might need custom traversal
            if (auto m = dynamic_cast<ast::Module*>(node)) { for (auto& stmt : m->body) analyzeNode(stmt.get()); }
            // else if (auto fd = dynamic_cast<ast::FunctionDeclaration*>(node)) { if(fd->body) analyzeNode(fd->body.get()); /* and params, return type */ }
            // ... and so on for other container nodes
            break;
    }

    // Post-visit actions (e.g., exit scope for blocks)
    if (node->getType() == ast::NodeType::BLOCK_STATEMENT) { // Changed to ast::NodeType
        exitScope();
    }
}

void SemanticAnalyzer::analyze(ast::Module* root) { // Changed to ast::Module
    if (!root) return;
    currentScope = new Scope(); // Global scope
    for (const auto& stmt : root->body) {
        analyzeNode(stmt.get());
    }
    // Clean up global scope if necessary, or it can be owned by the analyzer
}

void SemanticAnalyzer::analyzeBlockStatement(ast::BlockStatement* block) { // Changed to ast::BlockStatement
    enterScope();
    for (const auto& stmt : block->body) {
        analyzeNode(stmt.get());
    }
    exitScope();
}

void SemanticAnalyzer::analyzeBorrowExpression(ast::BorrowExprNode* expr) { // Changed to ast::BorrowExprNode
    // Example: Ensure the borrowed expression is a valid L-value (e.g., variable, member access)
    // This requires more context about what constitutes an L-value in Vyn
    analyzeNode(expr->expression.get());

    // Determine owner and type for borrow checking
    // This is highly dependent on how you resolve identifiers and types
    std::string owner_name = ""; // Placeholder
    ast::TypeNode* type_info = nullptr; // Placeholder. Changed to ast::TypeNode

    // if (auto ident = dynamic_cast<ast::Identifier*>(expr->expression.get())) {
    //     owner_name = ident->name;
    //     Symbol* symbol = currentScope->find(owner_name);
    //     if (symbol) type_info = symbol->dataType;
    // }
    // else if (auto member_expr = dynamic_cast<ast::MemberExpression*>(expr->expression.get())) {
        // Handle member expressions to get the base owner and type
    // }

    // checkBorrow(expr, owner_name, expr->kind == ast::BorrowKind::MUTABLE_BORROW, type_info);
}

void SemanticAnalyzer::analyzeVariableDeclaration(ast::VariableDeclaration* decl) { // Changed to ast::VariableDeclaration
    if (decl->init) {
        analyzeNode(decl->init.get());
        // Type checking: decl->typeNode vs type of decl->init
        // If decl->typeNode is null, infer it from decl->init
    }
    // Add symbol to current scope
    // Symbol* sym = new Symbol{SymbolType::VARIABLE, decl->id->name, decl->typeNode.get(), !decl->isConst};
    // currentScope->insert(decl->id->name, sym);
}

void SemanticAnalyzer::analyzeAssignment(ast::AssignmentExpression* expr) { // Changed to ast::AssignmentExpression
    // Ensure expr->left is an L-value
    // Analyze expr->left and expr->right
    analyzeNode(expr->left.get());
    analyzeNode(expr->right.get());
    // Type checking: type of expr->left vs type of expr->right
    // Mutability check: if expr->left is const
}

void SemanticAnalyzer::analyzeUnaryExpression(ast::UnaryExpression* expr) { // Changed to ast::UnaryExpression
    analyzeNode(expr->operand.get());
    // Type checking based on operator and operand type
    // e.g., '!' expects boolean, '-' expects number
}

// ... (rest of the methods, ensure they use ast:: prefixed types where appropriate)

// Helper to check if an expression results in a raw location type (e.g. loc<T>)
// This is a placeholder and needs to be implemented based on your type system.
bool SemanticAnalyzer::isRawLocationType(ast::Expression* expr) { // Changed to ast::Expression
    if (!expr) return false;
    // Example: Check if expr is a CallExpression to a specific 'loc' intrinsic
    // or if its inferred type is a location type.
    // This depends heavily on how type information is stored and accessed.
    // For now, let's assume it's not a raw location type.
    // if (auto callExpr = dynamic_cast<ast::CallExpression*>(expr)) {
    //     if (auto calleeIdent = dynamic_cast<ast::Identifier*>(callExpr->callee.get())) {
    //         if (calleeIdent->name == "loc" || calleeIdent->name == "__loc") { // Assuming 'loc' or '__loc' is the intrinsic
    //             return true;
    //         }
    //     }
    // }
    // // Or, if type inference has run and marked the expression node:
    // if (expr->inferredTypeName.rfind("loc<", 0) == 0) { // Checks if inferredTypeName starts with "loc<"
    //     return true;
    // }
    return false;
}

void SemanticAnalyzer::checkLocUnsafe(ast::Node* node) { // Changed to ast::Node
    if (!inUnsafeBlock) {
        // errors.push_back("Raw location operation (e.g., loc<T>, at, addr, from) must be inside an unsafe block at " + node->loc.toString());
    }
}

// ... (Scope methods, constructor, getErrors, etc.)

Scope::Scope(Scope* p) : parent(p) {}

Symbol* Scope::find(const std::string& name) {
    auto it = symbols.find(name);
    if (it != symbols.end()) {
        return it->second;
    }
    if (parent) {
        return parent->find(name);
    }
    return nullptr;
}

void Scope::insert(const std::string& name, Symbol* symbol) {
    symbols[name] = symbol;
}

Scope* Scope::getParent() {
    return parent;
}

SemanticAnalyzer::SemanticAnalyzer() : currentScope(nullptr) {}

std::vector<std::string> SemanticAnalyzer::getErrors() {
    return errors;
}

void SemanticAnalyzer::enterScope() {
    currentScope = new Scope(currentScope);
}

void SemanticAnalyzer::exitScope() {
    Scope* oldScope = currentScope;
    currentScope = currentScope->getParent();
    delete oldScope;
}

// Placeholder for checkBorrow - implementation depends on ownership/borrowing rules
void SemanticAnalyzer::checkBorrow(ast::Node* node, const std::string& owner, bool isMutableBorrow, ast::TypeNode* type) { // Changed to ast::Node and ast::TypeNode
    // This is a complex part of the semantic analysis.
    // It needs to track active borrows for each owner in the current scope
    // and potentially outer scopes, checking for conflicts:
    // 1. Multiple mutable borrows of the same owner.
    // 2. A mutable borrow while an immutable borrow exists.
    // 3. An immutable borrow while a mutable borrow exists.
    // This often involves a borrow checker state within the SemanticAnalyzer.

    // Example placeholder logic:
    // if (activeBorrows.count(owner)) {
    //     const auto& existingBorrows = activeBorrows[owner];
    //     if (isMutableBorrow) {
    //         if (!existingBorrows.empty()) {
    //             errors.push_back("Conflicting mutable borrow for owner '" + owner + "' at " + node->loc.toString());
    //         }
    //     } else { // Immutable borrow
    //         for (const auto& borrow : existingBorrows) {
    //             if (borrow.isMutable) {
    //                 errors.push_back("Conflicting immutable borrow (mutable already exists) for owner '" + owner + "' at " + node->loc.toString());
    //                 break;
    //             }
    //         }
    //     }
    // }
    // Add the new borrow to activeBorrows (with its scope, so it can be removed when scope ends)
}

}
