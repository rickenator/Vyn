#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include "vyn/ast.hpp" // Use canonical AST include for now

// Forward declare LLVM types
namespace llvm {
    class LLVMContext;
    class Module;
    class IRBuilderBase;
    class IRBuilderDefaultInserter;
    class IRBuilder;
    class Value;
    class Type;
    class Function;
    class BasicBlock;
}

namespace vyn {

// --- Visitor-based codegen for AST nodes ---
//
// The LLVMCodegen class now implements a visitor for the Vyn AST. Each node type gets a codegen method.
// The main codegen(Node*) dispatches to the correct method using dynamic_cast, and each method emits IR for that node.
//
// To add a new node type, add a codegen(NodeType*) method and update the dispatch logic.
//
// Example usage:
//   LLVMCodegen cg("main");
//   cg.generate(ast_root); // ast_root is a Module*
//
// The codegen methods for each node type are scaffolded in codegen.cpp.
//
// To extend: implement codegen for all relevant AST nodes (expressions, statements, declarations).
//
// This pattern keeps AST nodes as data only, and all codegen logic in the backend.
//

// Main entry point for Vyn → LLVM IR codegen
class LLVMCodegen {
public:
    LLVMCodegen(const std::string& moduleName);
    ~LLVMCodegen();

    // Entry: generate LLVM IR for a Vyn module
    void generate(Module* root);

    // Get the generated LLVM module
    std::unique_ptr<llvm::Module> takeModule();

    // ...other utility methods...

private:
    // LLVM state
    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<llvm::Module> module;
    std::unique_ptr<llvm::IRBuilder<>> builder;

    // Symbol tables, type maps, etc.
    // std::unordered_map<std::string, llvm::Value*> namedValues;
    // ...ownership/memory model helpers...

    // Helper: map Vyn types to LLVM types
    llvm::Type* codegenType(TypeNode* type);

    // Helper: codegen for each AST node type
    llvm::Value* codegen(Node* node);
    llvm::Value* codegen(Expression* expr);
    void codegen(Statement* stmt);
    void codegen(Declaration* decl);

    // ...one method per concrete AST node...
};

} // namespace vyn
