#pragma once
#include "vyn/parser/ast.hpp" // Changed from ast_fwd.hpp

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Function.h> // Added for llvm::Function
#include <llvm/IR/BasicBlock.h>
#include <map>
#include <string>
#include <vector>
#include <memory>

namespace vyn {

// Forward declarations from ast.hpp if not fully included or for clarity
// class ASTNode;
// ... other AST node forward declarations ...
// class Visitor; // Already included via ast.hpp

// Helper struct for storing information about user-defined types
struct UserTypeInfo {
    llvm::StructType* llvmType;
    std::map<std::string, unsigned> fieldIndices; // Map field name to index
    bool isStruct; // True if struct, false if class (or could be enum later)
    // Potentially: vtable, parent type info, etc.
};

// Helper struct to manage basic block context during codegen
struct BlockContext {
    llvm::BasicBlock* llvmBlock;
    llvm::Value* value; // To store the result of expressions

    BlockContext(llvm::BasicBlock* bb) : llvmBlock(bb), value(nullptr) {}
};

// Helper struct to manage loop context
struct LoopContext {
    llvm::BasicBlock *loopHeader; // Block for the loop condition check
    llvm::BasicBlock *loopBody;   // Block for the loop body
    llvm::BasicBlock *loopUpdate; // Block for the loop increment/update
    llvm::BasicBlock *loopExit;   // Block after the loop
};


class LLVMCodegen : public vyn::Visitor { // Inherit from Visitor
public:
    LLVMCodegen();
    ~LLVMCodegen();

    void generate(const std::vector<std::unique_ptr<Node>>& ast, const std::string& outputFilename); // Corrected ASTNode to Node
    void dumpIR() const;

    // Visitor methods overridden from vyn::Visitor, corrected to match ast.hpp
    // Literals
    void visit(Identifier* node) override;
    void visit(IntegerLiteral* node) override;
    void visit(FloatLiteral* node) override;
    void visit(StringLiteral* node) override;
    void visit(BooleanLiteral* node) override;
    void visit(ObjectLiteral* node) override;
    void visit(NilLiteral* node) override;

    // Expressions
    void visit(UnaryExpression* node) override;
    void visit(BinaryExpression* node) override;
    void visit(CallExpression* node) override;
    void visit(MemberExpression* node) override;
    void visit(AssignmentExpression* node) override;
    void visit(ArrayLiteralNode* node) override;
    void visit(BorrowExprNode* node) override;
    void visit(PointerDerefExpression* node) override;
    void visit(AddrOfExpression* node) override;
    void visit(FromIntToLocExpression* node) override;
    void visit(ArrayElementExpression* node) override;

    // Statements
    void visit(BlockStatement* node) override;
    void visit(ExpressionStatement* node) override;
    void visit(IfStatement* node) override;
    void visit(ForStatement* node) override;
    void visit(WhileStatement* node) override;
    void visit(ReturnStatement* node) override;
    void visit(BreakStatement* node) override;
    void visit(ContinueStatement* node) override;
    void visit(TryStatement* node) override;

    // Declarations
    void visit(VariableDeclaration* node) override;
    void visit(FunctionDeclaration* node) override;
    void visit(TypeAliasDeclaration* node) override;
    void visit(ImportDeclaration* node) override;
    void visit(StructDeclaration* node) override;
    void visit(ClassDeclaration* node) override;
    void visit(FieldDeclaration* node) override;
    void visit(ImplDeclaration* node) override;
    void visit(EnumDeclaration* node) override;
    void visit(EnumVariantNode* node) override;
    void visit(GenericParamNode* node) override;
    void visit(TemplateDeclarationNode* node) override;
    
    // Other
    void visit(TypeNode* node) override;

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
    llvm::Type* m_currentLLVMType;

    // Codegen state
    llvm::Function* currentFunction;
    llvm::StructType* currentClassType; // For 'this' pointer in methods
    BlockContext* currentBlock; // Pointer to the current block context on the stack
    std::vector<BlockContext> blockStack; // Stack for managing nested blocks
    LoopContext currentLoopContext; // For break/continue
    std::vector<LoopContext> loopStack; // Stack for nested loops
    llvm::Function* m_currentFunction = nullptr; // Added
    std::map<std::string, llvm::AllocaInst*> m_currentFunctionNamedValues; // Added

    // Symbol and type maps
    std::map<std::string, llvm::Value*> namedValues; // For local variables and parameters
    std::map<std::string, UserTypeInfo> userTypeMap; // For structs, classes, enums
    std::map<std::string, llvm::Type*> typeParameterMap; // For template type parameters
    std::map<vyn::TypeNode*, llvm::Type*> m_typeCache; // Cache for TypeNode* to llvm::Type*
    vyn::TypeNode* m_currentImplTypeNode = nullptr; // To track the type of the current 'impl' block

    // Helper methods
    void logError(const SourceLocation& loc, const std::string& message);
    llvm::Type* codegenType(TypeNode* typeNode); // Converts vyn::TypeNode to llvm::Type
    llvm::Value* createEntryBlockAlloca(llvm::Function* func, const std::string& varName, llvm::Type* type);
    llvm::AllocaInst* createEntryBlockAlloca(llvm::Type* type, const std::string& name);
    
    // Helper to get a string representation of an LLVM type for logging
    std::string getTypeName(llvm::Type* type);
    // Helper for type casting
    llvm::Value* tryCast(llvm::Value* value, llvm::Type* targetType, const vyn::Location& loc);

    // RTTI related helpers
    llvm::StructType* getOrCreateRTTIStructType();
    llvm::Value* generateRTTIObject(const std::string& typeName, int typeId); // typeId for distinguishing types

    // Block management
    void pushBlock(llvm::BasicBlock* bb);
    void popBlock();
    void setCurrentBlockValue(llvm::Value* value);
    llvm::Value* getCurrentBlockValue();

    // Loop management
    void pushLoop(llvm::BasicBlock* header, llvm::BasicBlock* body, llvm::BasicBlock* update, llvm::BasicBlock* exit);
    void popLoop();
};

} // namespace vyn
