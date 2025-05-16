#pragma once
#include "vyn/parser/ast.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

namespace vyn {

// Forward declaration
class Node; // Add forward declaration for Node
class TypeNode; // Add forward declaration for TypeNode

struct BorrowInfo {
    std::string ownerName;
    bool isMutable;
    Node* borrowNode; // The AST node that created the borrow
    // Potentially add TypeNode* of the borrowed type if needed for more complex checks
};

struct SymbolInfo {
    enum class Kind { Variable, Function, Type };
    Kind kind;
    std::string name;
    bool isConst = false;
    TypeNode* type = nullptr;
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
    void analyze(Module* root);
    const std::vector<std::string>& getErrors() const { return errors; }
private:
    void analyzeNode(Node* node);
    void analyzeAssignment(AssignmentExpression* expr);
    void analyzeVariableDeclaration(VariableDeclaration* decl);
    void analyzeUnaryExpression(UnaryExpression* expr);
    void analyzeBorrowExpression(BorrowExprNode* expr);
    void analyzeBlockStatement(BlockStatement* block);

    void enterUnsafe();
    void exitUnsafe();
    bool inUnsafe() const;

    void checkBorrow(Node* node, const std::string& owner, bool isMutable, TypeNode* type);
    void checkLifetime(Node* node, const std::string& owner);
    void checkLocUnsafe(Node* node);
    bool isRawLocationType(Expression* expr);

    // ... more as needed ...
    std::unique_ptr<SymbolTable> symbols;
    std::vector<std::string> errors;
    std::vector<BorrowInfo> activeBorrows; // Declare activeBorrows
    int unsafeDepth = 0;
};

} // namespace vyn
