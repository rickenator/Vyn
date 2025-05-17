#pragma once
#include "vyn/parser/ast.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

namespace vyn {

struct BorrowInfo {
    std::string ownerName;
    bool isMutable;
    ast::Node* borrowNode; // The AST node that created the borrow
    // Potentially add ast::TypeNode* of the borrowed type if needed for more complex checks
};

struct SymbolInfo {
    enum class Kind { Variable, Function, Type };
    Kind kind;
    std::string name;
    bool isConst = false;
    ast::TypeNode* type = nullptr; // Changed to ast::TypeNode
    // Extend as needed (scope, function params, etc)
};

class SymbolTable {
public:
    SymbolTable(SymbolTable* parent = nullptr) : parent(parent) {}
    void add(const SymbolInfo& sym) { table[sym.name] = sym; }
    SymbolInfo* lookup(const std::string& name) {
        auto it = table.find(name);
        if (it != table.end()) return &it->second;
        if (parent) return parent->lookup(name);
        return nullptr;
    }
private:
    std::unordered_map<std::string, SymbolInfo> table;
    SymbolTable* parent;
};

class SemanticAnalyzer {
public:
    SemanticAnalyzer();
    void analyze(ast::Module* root); // Qualified with ast::
    const std::vector<std::string>& getErrors() const { return errors; }
private:
    void analyzeNode(ast::Node* node); // Qualified with ast::
    void analyzeAssignment(ast::AssignmentExpression* expr); // Qualified with ast::
    void analyzeVariableDeclaration(ast::VariableDeclaration* decl); // Qualified with ast::
    void analyzeUnaryExpression(ast::UnaryExpression* expr); // Qualified with ast::
    void analyzeBorrowExpression(ast::BorrowExprNode* expr); // Qualified with ast::
    void analyzeBlockStatement(ast::BlockStatement* block); // Qualified with ast::
    // Add other specific analyze methods if they exist and need qualification
    // void analyzeFunctionDeclaration(ast::FunctionDeclaration* decl);
    // void analyzeReturnStatement(ast::ReturnStatement* stmt);
    // void analyzeIfStatement(ast::IfStatement* stmt);
    // void analyzeWhileStatement(ast::WhileStatement* stmt);
    // void analyzeExpressionStatement(ast::ExpressionStatement* stmt);
    // void analyzeBinaryExpression(ast::BinaryExpression* expr);
    // void analyzeCallExpression(ast::CallExpression* expr);
    // void analyzeMemberAccessExpression(ast::MemberAccessExpression* expr);
    // void analyzeIndexAccessExpression(ast::IndexAccessExpression* expr);
    // void analyzeLiteral(ast::Literal* lit);
    // void analyzeIdentifier(ast::Identifier* id);
    // void analyzeTypeNode(ast::TypeNode* typeNode); // If it exists

    SymbolTable* currentScope;
    std::vector<std::string> errors;
    std::unordered_map<ast::Node*, ast::TypeNode*> expressionTypes; // Store inferred types

    ast::TypeNode* typeCheck(ast::Node* node); // Helper for type checking, returns type or nullptr
    void enterScope();
    void exitScope();
    void addError(const std::string& message, const ast::Node* node); // Use ast::Node for location
    bool isLValue(ast::Expression* expr);
    bool isRawLocationType(ast::Expression* expr); // Qualified with ast::
};

} // namespace vyn
