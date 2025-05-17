\
#include "vyn/vre/llvm/codegen.hpp"
#include "vyn/parser/ast.hpp"
#include "vyn/parser/source_location.hpp" // For vyn::SourceLocation

// LLVM Headers
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"

// Standard C++ Headers
#include <iostream> // For logError, std::cerr
#include <map>
#include <memory>
#include <optional>
#include <stack>
#include <string>
#include <system_error> // For std::error_code
#include <vector>

using namespace vyn;

// Error Logging Utility
void LLVMCodegen::logError(const SourceLocation& loc, const std::string& message) {
    std::cerr << "Error at " << (!loc.filePath.empty() ? loc.filePath.c_str() : "unknown_file") << ":" << loc.line << ":" << loc.column << ": " << message << std::endl;
}

// --- RTTI Helper Implementations ---
llvm::StructType* LLVMCodegen::getOrCreateRTTIStructType() {
    if (rttiStructType) return rttiStructType;

    std::vector<llvm::Type*> rttiFields = {
        int32Type,
        int8PtrType
    };
    rttiStructType = llvm::StructType::create(*context, rttiFields, "vyn.RTTI");
    return rttiStructType;
}

llvm::Value* LLVMCodegen::generateRTTIObject(const std::string& typeName, int typeId) {
    llvm::StructType* rttiTy = getOrCreateRTTIStructType();
    llvm::Constant* typeNameGlobal = builder->CreateGlobalStringPtr(typeName);
    llvm::Constant* rttiVals[] = {
        llvm::ConstantInt::get(int32Type, typeId),
        typeNameGlobal
    };
    return llvm::ConstantStruct::get(rttiTy, rttiVals);
}

LLVMCodegen::LLVMCodegen()
    : context(std::make_unique<llvm::LLVMContext>()),
      module(std::make_unique<llvm::Module>("VynModule", *this->context)), 
      builder(std::make_unique<llvm::IRBuilder<>>(*this->context)),
      m_isLHSOfAssignment(false),
      currentFunction(nullptr),
      currentClassType(nullptr),
      m_currentLLVMValue(nullptr),
      m_currentVynModule(nullptr)
{
    voidType = llvm::Type::getVoidTy(*this->context);
    int1Type = llvm::Type::getInt1Ty(*this->context);
    int8Type = llvm::Type::getInt8Ty(*this->context);
    int32Type = llvm::Type::getInt32Ty(*this->context);
    int64Type = llvm::Type::getInt64Ty(*this->context);
    floatType = llvm::Type::getFloatTy(*this->context);
    doubleType = llvm::Type::getDoubleTy(*this->context);
    int8PtrType = llvm::PointerType::getUnqual(int8Type);

    rttiStructType = getOrCreateRTTIStructType(); 

    currentLoopContext = {nullptr, nullptr, nullptr, nullptr};
}

LLVMCodegen::~LLVMCodegen() = default;

void LLVMCodegen::generate(vyn::ast::Module* astModule, const std::string& outputFilename) {
    if (!astModule) {
        logError(SourceLocation(), "Cannot generate code: AST module is null.");
        return;
    }

    astModule->accept(*this);

    if (llvm::verifyModule(*module, &llvm::errs())) {
        logError(SourceLocation(), "LLVM module verification failed.");
        return;
    }

    std::error_code EC;
    llvm::raw_fd_ostream dest(outputFilename, EC, llvm::sys::fs::OpenFlags{});

    if (EC) {
        logError(SourceLocation(), "Could not open file: " + EC.message());
        return;
    }

    module->print(dest, nullptr);
    dest.flush();
}

void LLVMCodegen::dumpIR() const {
    if (module) {
        module->print(llvm::errs(), nullptr);
    } else {
        std::cerr << "No LLVM module available to dump." << std::endl;
    }
}

void LLVMCodegen::visit(vyn::ast::Module* node) {
    if (!node) return;

    vyn::ast::Module* previousModule = m_currentVynModule;
    m_currentVynModule = node;

    for (const auto& stmt : node->body) {
        if (stmt) {
            stmt->accept(*this);
        }
    }

    m_currentVynModule = previousModule;
    m_currentLLVMValue = nullptr;
}
