// SPDX-License-Identifier: Apache-2.0

#include "vyb/vre/llvm/codegen.hpp"
#include "vyb/parser/ast.hpp"
#include <llvm/IR/Instructions.h>
#include <llvm/IR/DerivedTypes.h>

using namespace vyb;

llvm::Type* LLVMCodegen::getPointeeTypeInfo(llvm::Value* ptr) {
    if (!ptr || !ptr->getType()->isPointerTy()) {
        return nullptr;
    }

    // Try to get type information in different ways
    if (auto* allocaInst = llvm::dyn_cast<llvm::AllocaInst>(ptr)) {
        return allocaInst->getAllocatedType();
    }

    // Try more approaches to determine type
    if (auto* gepInst = llvm::dyn_cast<llvm::GetElementPtrInst>(ptr)) {
        return gepInst->getSourceElementType();
    }

    // Check if the value has a name that might hint at its type
    std::string valueName = ptr->getName().str();
    if (!valueName.empty()) {
        // Look for type-specific naming patterns like "Point_obj"
        size_t objSuffix = valueName.find("_obj");
        if (objSuffix != std::string::npos) {
            std::string typeName = valueName.substr(0, objSuffix);
            // Check if we have this type registered
            auto it = userTypeMap.find(typeName);
            if (it != userTypeMap.end()) {
                return it->second.llvmType;
            }
        }
    }

    // For opaque pointers in LLVM IR, getting the pointee type is harder
    // We should fall back to our internal type systems or cached types

    return nullptr;
}

llvm::Function* LLVMCodegen::getLitConversionFunction() {
    // Check if the function already exists
    llvm::Function* func = module->getFunction("__vyb_convert_lit_string");
    if (func) {
        return func;
    }

    // Create the function declaration: char* __vyb_convert_lit_string(const char* str)
    llvm::FunctionType* funcType = llvm::FunctionType::get(
        int8PtrType,  // return type: char*
        {int8PtrType}, // parameter: const char*
        false // not variadic
    );

    func = llvm::Function::Create(
        funcType,
        llvm::Function::ExternalLinkage,
        "__vyb_convert_lit_string",
        module.get()
    );

    return func;
}

// Helper function to check if an expression is a lit() intrinsic call
bool LLVMCodegen::isLitIntrinsicCall(vyb::ast::Expression* expr) {
    if (!expr) return false;

    auto* callExpr = dynamic_cast<vyb::ast::CallExpression*>(expr);
    if (!callExpr) return false;

    auto* identCallee = dynamic_cast<vyb::ast::Identifier*>(callExpr->callee.get());
    if (!identCallee) return false;

    return identCallee->name == "lit";
}

// Helper function to check if a function body returns a lit() intrinsic call
bool LLVMCodegen::functionBodyReturnsLitIntrinsic(vyb::ast::BlockStatement* body) {
    if (!body) return false;

    // Look for return statements in the function body
    for (const auto& stmt : body->body) {
        if (auto* returnStmt = dynamic_cast<vyb::ast::ReturnStatement*>(stmt.get())) {
            if (isLitIntrinsicCall(returnStmt->argument.get())) {
                return true;
            }
        }
        // Could also check nested blocks, but for now we'll just check top-level returns
    }

    return false;
}

// Ensure all core intrinsic functions are declared in the module
// This prevents JIT runtime errors when functions are not found
void LLVMCodegen::ensureCoreIntrinsicFunctions() {
    // Declare all core intrinsic functions that the JIT runtime expects
    getVybPrintlnFunction();      // __vyb_println
    getSerializeToJsonFunction(); // __vyb_serialize_to_json
    getLitConversionFunction();   // __vyb_convert_lit_string
}
