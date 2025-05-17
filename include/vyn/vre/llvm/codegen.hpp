#pragma once

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <string>
#include <vector>
#include <memory>

#include <vyn/parser/ast.hpp>

namespace vyn {

// Forward declarations from ast.hpp if not fully included or for clarity
// class ASTNode; // No longer needed, ast.hpp is included
// ... other AST node forward declarations ...
// class Visitor; // Already included via ast.hpp

// Helper struct for storing information about user-defined types
struct UserTypeInfo {
    llvm::StructType* llvmType;
    std::map<std::string, unsigned> fieldIndices; // Map field name to index
    bool isStruct; // True if struct, false if class (or could be enum later)
    // Potentially: vtable, parent type info, etc.
};

// Helper struct to manage loop context
struct LoopContext {
    llvm::BasicBlock *loopHeader; // Block for the loop condition check
    llvm::BasicBlock *loopBody;   // Block for the loop body
    llvm::BasicBlock *loopUpdate; // Block for the loop increment/update
    llvm::BasicBlock *loopExit;   // Block after the loop
};


class LLVMCodegen : public vyn::ast::Visitor { // Inherit from Visitor
public:
    LLVMCodegen();
    ~LLVMCodegen();

    void generate(vyn::ast::Module* astModule, const std::string& outputFilename); // Keep this one
    void dumpIR() const;
    std::unique_ptr<llvm::Module> releaseModule(); // Added releaseModule

    // Visitor methods overridden from vyn::Visitor, corrected to match ast.hpp
    // Literals
    void visit(vyn::ast::Identifier* node) override;
    void visit(vyn::ast::IntegerLiteral* node) override;
    void visit(vyn::ast::FloatLiteral* node) override;
    void visit(vyn::ast::StringLiteral* node) override;
    void visit(vyn::ast::BooleanLiteral* node) override;
    void visit(vyn::ast::ObjectLiteral* node) override;
    void visit(vyn::ast::NilLiteral* node) override;

    // Expressions
    void visit(vyn::ast::UnaryExpression* node) override;
    void visit(vyn::ast::BinaryExpression* node) override;
    void visit(vyn::ast::CallExpression* node) override;
    void visit(vyn::ast::MemberExpression* node) override;
    void visit(vyn::ast::AssignmentExpression* node) override;
    void visit(vyn::ast::ArrayLiteralNode* node) override;
    void visit(vyn::ast::BorrowExprNode* node) override;
    void visit(vyn::ast::PointerDerefExpression* node) override;
    void visit(vyn::ast::AddrOfExpression* node) override;
    void visit(vyn::ast::FromIntToLocExpression* node) override;
    void visit(vyn::ast::ArrayElementExpression* node) override;
    void visit(vyn::ast::LocationExpression* node) override; 
    void visit(vyn::ast::ListComprehension* node) override;


    // Statements
    void visit(vyn::ast::BlockStatement* node) override;
    void visit(vyn::ast::ExpressionStatement* node) override;
    void visit(vyn::ast::IfStatement* node) override;
    void visit(vyn::ast::ForStatement* node) override;
    void visit(vyn::ast::WhileStatement* node) override;
    void visit(vyn::ast::ReturnStatement* node) override;
    void visit(vyn::ast::BreakStatement* node) override;
    void visit(vyn::ast::ContinueStatement* node) override;
    void visit(vyn::ast::TryStatement* node) override;

    // Declarations
    void visit(vyn::ast::VariableDeclaration* node) override;
    void visit(vyn::ast::FunctionDeclaration* node) override;
    void visit(vyn::ast::TypeAliasDeclaration* node) override;
    void visit(vyn::ast::ImportDeclaration* node) override;
    void visit(vyn::ast::StructDeclaration* node) override;
    void visit(vyn::ast::ClassDeclaration* node) override;
    void visit(vyn::ast::FieldDeclaration* node) override;
    void visit(vyn::ast::ImplDeclaration* node) override;
    void visit(vyn::ast::EnumDeclaration* node) override;
    void visit(vyn::ast::EnumVariantNode* node) override;
    void visit(vyn::ast::GenericParamNode* node) override;
    void visit(vyn::ast::TemplateDeclarationNode* node) override;
    
    // Other
    void visit(vyn::ast::TypeNode* node) override;
    void visit(vyn::ast::Module* node) override; // Added Module visit override

private:
    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<llvm::Module> module;
    std::unique_ptr<llvm::IRBuilder<>> builder;

    // LLVM types frequently used
    llvm::Type* voidType;
    llvm::Type* int1Type; // For booleans
    llvm::Type* int8Type;
    llvm::Type* int32Type;
    llvm::Type* int64Type;
    llvm::Type* floatType;
    llvm::Type* doubleType;
    llvm::Type* int8PtrType; // Generic pointer type (char*)
    llvm::StructType* rttiStructType; // For RTTI objects
    llvm::Type* stringType;

    // Member to store the result of visiting a TypeNode
    llvm::Type* m_currentLLVMType = nullptr; // Initialize
    // Member to store the result of the last expression/node visited
    llvm::Value* m_currentLLVMValue = nullptr; // Unified value propagation

    // Codegen state
    llvm::Function* currentFunction = nullptr; // Initialize
    llvm::StructType* currentClassType = nullptr; // Initialize
    LoopContext currentLoopContext; 
    std::vector<LoopContext> loopStack;
    std::map<std::string, llvm::AllocaInst*> m_currentFunctionNamedValues;

    // Symbol and type maps
    std::map<std::string, llvm::Value*> namedValues;
    std::map<std::string, UserTypeInfo> userTypeMap;
    std::map<std::string, llvm::Type*> typeParameterMap;
    std::map<vyn::ast::TypeNode*, llvm::Type*> m_typeCache;
    vyn::ast::TypeNode* m_currentImplTypeNode = nullptr; // Initialize
    vyn::ast::Module* m_currentVynModule = nullptr;
    bool m_isLHSOfAssignment = false;

    llvm::Type* codegenType(vyn::ast::TypeNode* typeNode); // Converts vyn::TypeNode to llvm::Type
    llvm::Function* getCurrentFunction();
    llvm::BasicBlock* getCurrentBasicBlock();

    // Helper methods
    void logError(const SourceLocation& loc, const std::string& message);
    llvm::Value* createEntryBlockAlloca(llvm::Function* func, const std::string& varName, llvm::Type* type);
    llvm::AllocaInst* createEntryBlockAlloca(llvm::Type* type, const std::string& name);
    
    // Helper to get a string representation of an LLVM type for logging
    std::string getTypeName(llvm::Type* type);
    // Helper for type casting
    llvm::Value* tryCast(llvm::Value* value, llvm::Type* targetType, const vyn::SourceLocation& loc);

    // RTTI related helpers
    llvm::StructType* getOrCreateRTTIStructType();
    llvm::Value* generateRTTIObject(const std::string& typeName, int typeId); // typeId for distinguishing types

    // Loop management
    void pushLoop(llvm::BasicBlock* header, llvm::BasicBlock* body, llvm::BasicBlock* update, llvm::BasicBlock* exit);
    void popLoop();

    // Helper for struct field access
    int getStructFieldIndex(llvm::StructType* structType, const std::string& fieldName);

};

} // namespace vyn
