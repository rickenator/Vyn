\
#include "vyn/vre/llvm/codegen.hpp"
#include "vyn/parser/ast.hpp" // For SourceLocation

#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instructions.h> // For AllocaInst
#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h> // For StructType, PointerType
#include <llvm/IR/Constants.h>    // For Constant
#include <llvm/Support/raw_ostream.h> // For llvm::errs() if logError uses it (logError is not moved here yet)

#include <string>
#include <vector> 
#include <map>    

using namespace vyn;
// using namespace llvm; // Uncomment if desired for brevity

// --- Helper Method Implementations ---

// New helper to get type name as string
std::string LLVMCodegen::getTypeName(llvm::Type* type) {
    std::string typeStr;
    llvm::raw_string_ostream rso(typeStr);
    if (type) type->print(rso); else rso << "nullptr";
    return rso.str();
}

llvm::Value* LLVMCodegen::tryCast(llvm::Value* value, llvm::Type* targetType, const vyn::SourceLocation& loc) {
    // Basic type casting logic (placeholder, needs to be more robust)
    // This should handle numeric casts, pointer casts, etc.
    if (!value || !targetType) return nullptr;
    if (value->getType() == targetType) return value;

    // Example: Integer to Pointer (potentially unsafe, use with care)
    if (targetType->isPointerTy() && value->getType()->isIntegerTy()) {
        return builder->CreateIntToPtr(value, targetType, "inttoptr_cast");
    }
    // Example: Pointer to Integer
    if (targetType->isIntegerTy() && value->getType()->isPointerTy()) {
        return builder->CreatePtrToInt(value, targetType, "ptrtoint_cast");
    }
    // Example: Pointer to Pointer (bitcast)
    if (targetType->isPointerTy() && value->getType()->isPointerTy()) {
        return builder->CreateBitCast(value, targetType, "ptr_bitcast");
    }
    // Example: Integer to Integer (trunc or sext/zext)
    if (targetType->isIntegerTy() && value->getType()->isIntegerTy()) {
        llvm::IntegerType* targetIntTy = llvm::dyn_cast<llvm::IntegerType>(targetType);
        llvm::IntegerType* valueIntTy = llvm::dyn_cast<llvm::IntegerType>(value->getType());
        if (targetIntTy && valueIntTy) {
            if (targetIntTy->getBitWidth() < valueIntTy->getBitWidth()) {
                return builder->CreateTrunc(value, targetType, "int_trunc");
            } else if (targetIntTy->getBitWidth() > valueIntTy->getBitWidth()) {
                // Assuming signed extension for now, Vyn semantics might specify
                return builder->CreateSExt(value, targetType, "int_sext");
            }
            // else same width, should have been caught by value->getType() == targetType
        }
    }
    // Add more casting rules as needed (float to int, int to float, etc.)

    logError(loc, "Unsupported or invalid cast from type " + getTypeName(value->getType()) + " to " + getTypeName(targetType));
    return nullptr; 
}

llvm::Value* LLVMCodegen::createEntryBlockAlloca(llvm::Function* func, const std::string& varName, llvm::Type* type) {
    if (!func) {
        // logError (some location, "Cannot create alloca: not in a function context.");
        return nullptr;
    }
    if (!type) {
        // logError (some location, "Cannot create alloca: type is null for variable " + varName);
        return nullptr;
    }
    llvm::IRBuilder<> tmpB(&func->getEntryBlock(), func->getEntryBlock().begin());
    return tmpB.CreateAlloca(type, nullptr, varName);
}

void LLVMCodegen::pushLoop(llvm::BasicBlock* header, llvm::BasicBlock* body, llvm::BasicBlock* update, llvm::BasicBlock* exit) {
    loopStack.push_back({header, body, update, exit});
}

void LLVMCodegen::popLoop() {
    if (!loopStack.empty()) {
        loopStack.pop_back();
    }
}

// Helper function to get field index
int LLVMCodegen::getStructFieldIndex(llvm::StructType* structType, const std::string& fieldName) {
    if (!structType || structType->getName().empty()) {
        // This can happen for anonymous structs (like tuples) or if structType is null.
        // For anonymous structs, field access is by index, not name.
        // logError(SourceLocation(), "getStructFieldIndex called with null or unnamed struct type.");
        return -1; 
    }
    std::string structName = structType->getName().str();
    auto it = userTypeMap.find(structName);
    if (it != userTypeMap.end()) {
        const auto& typeInfo = it->second;
        auto fieldIt = typeInfo.fieldIndices.find(fieldName);
        if (fieldIt != typeInfo.fieldIndices.end()) {
            return fieldIt->second;
        }
    } else {
        // This case means the struct type (though named in LLVM) is not in our userTypeMap.
        // This could be an externally defined struct or an issue with registration.
        // logError(SourceLocation(), "Struct type " + structName + " not found in userTypeMap.");
    }
    return -1; // Field not found or struct type not recognized by our map
}

