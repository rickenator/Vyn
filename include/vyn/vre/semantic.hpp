#pragma once
#include <vyn/parser/ast.hpp> // Include ast.hpp for full type definitions
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <map> // Added missing include for std::map

namespace vyn {

// Forward declarations are mostly handled by ast.hpp
// class TypeNode; // This is vyn::ast::TypeNode, already in ast.hpp

namespace ast { // Forward declare Node from ast namespace if not fully included by ast.hpp somehow, though it should be.
    class Node;
    class TypeNode;
} // namespace ast

enum class SymbolType {
    VARIABLE,
    FUNCTION,
    TYPE
};

struct Symbol {
    SymbolType type;
    std::string name;
    ast::TypeNode* dataType; // Assuming TypeNode is the correct type for data types. Changed to ast::TypeNode
    bool isMutable;
    // Add other relevant symbol information, e.g., scope, definition location
};

class Scope {
public:
    Scope(Scope* parent = nullptr);
    Symbol* find(const std::string& name);
    void insert(const std::string& name, Symbol* symbol);
    Scope* getParent();
private:
    std::map<std::string, Symbol*> symbols;
    Scope* parent;
};

class SemanticAnalyzer {
public:
    SemanticAnalyzer();
    void analyze(ast::Module* root); // Changed to ast::Module
    std::vector<std::string> getErrors();

private:
    Scope* currentScope;
    std::vector<std::string> errors;
    bool inUnsafeBlock = false; // Track if currently inside an unsafe block

    void analyzeNode(ast::Node* node); // Changed to ast::Node
    void analyzeAssignment(ast::AssignmentExpression* expr); // Changed to ast::AssignmentExpression
    void analyzeVariableDeclaration(ast::VariableDeclaration* decl); // Changed to ast::VariableDeclaration
    void analyzeUnaryExpression(ast::UnaryExpression* expr); // Changed to ast::UnaryExpression
    void analyzeBorrowExpression(ast::BorrowExprNode* expr); // Changed to ast::BorrowExprNode
    void analyzeBlockStatement(ast::BlockStatement* block); // Changed to ast::BlockStatement
    // Add other analysis methods for different AST node types

    // Helper methods
    void enterScope();
    void exitScope();
    void checkBorrow(ast::Node* node, const std::string& owner, bool isMutableBorrow, ast::TypeNode* type); // Changed to ast::Node and ast::TypeNode
    void checkLocUnsafe(ast::Node* node); // Check if loc<T> is in an unsafe block. Changed to ast::Node
    bool isRawLocationType(ast::Expression* expr); // Changed to ast::Expression
};

} // namespace vyn
