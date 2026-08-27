// SPDX-License-Identifier: Apache-2.0

#include "vyb/vre/llvm/codegen.hpp"
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/DerivedTypes.h>

namespace vyb {

// Push a call frame onto the runtime call stack
void LLVMCodegen::pushCallStackFrame(const std::string& functionName, const SourceLocation& loc, llvm::Function* llvmFunc) {
    CallStackFrame frame;
    frame.functionName = functionName;
    frame.location = loc;
    frame.llvmFunction = llvmFunc;
    callStack.push_back(frame);
}

// Pop a call frame from the runtime call stack
void LLVMCodegen::popCallStackFrame() {
    if (!callStack.empty()) {
        callStack.pop_back();
    }
}

// Get or create the __vyb_runtime_push_call_frame function
static llvm::Function* getPushCallFrameFunction(llvm::Module* module, llvm::LLVMContext* context, llvm::Type* int8PtrType) {
    llvm::Function* pushFunc = module->getFunction("__vyb_runtime_push_call_frame");

    if (!pushFunc) {
        // void __vyb_runtime_push_call_frame(const char* function_name, const char* file_path, uint32_t line, uint32_t column)
        std::vector<llvm::Type*> paramTypes = {
            int8PtrType,                              // function_name
            int8PtrType,                              // file_path
            llvm::Type::getInt32Ty(*context),         // line
            llvm::Type::getInt32Ty(*context)          // column
        };

        llvm::FunctionType* pushType = llvm::FunctionType::get(
            llvm::Type::getVoidTy(*context),
            paramTypes,
            false
        );

        pushFunc = llvm::Function::Create(
            pushType,
            llvm::Function::ExternalLinkage,
            "__vyb_runtime_push_call_frame",
            module
        );

        auto args = pushFunc->arg_begin();
        args->setName("function_name");
        (++args)->setName("file_path");
        (++args)->setName("line");
        (++args)->setName("column");
    }

    return pushFunc;
}

// Get or create the __vyb_runtime_pop_call_frame function
static llvm::Function* getPopCallFrameFunction(llvm::Module* module, llvm::LLVMContext* context) {
    llvm::Function* popFunc = module->getFunction("__vyb_runtime_pop_call_frame");

    if (!popFunc) {
        // void __vyb_runtime_pop_call_frame()
        llvm::FunctionType* popType = llvm::FunctionType::get(
            llvm::Type::getVoidTy(*context),
            {},
            false
        );

        popFunc = llvm::Function::Create(
            popType,
            llvm::Function::ExternalLinkage,
            "__vyb_runtime_pop_call_frame",
            module
        );
    }

    return popFunc;
}

// Generate a call to push the current function onto the call stack
void LLVMCodegen::generatePushFrameCall(const std::string& functionName, const SourceLocation& loc) {
    // Kernel mode (issue #198): device functions have no host call-stack runtime.
    if (vyb::g_kernel_mode) return;
    llvm::Function* pushFunc = getPushCallFrameFunction(module.get(), context.get(), int8PtrType);

    // Create string constants
    llvm::Constant* funcNameStr = builder->CreateGlobalStringPtr(functionName, functionName + ".str");
    llvm::Constant* filePathStr = builder->CreateGlobalStringPtr(loc.filePath, "filepath.str");
    llvm::Value* lineVal = llvm::ConstantInt::get(builder->getInt32Ty(), loc.line);
    llvm::Value* columnVal = llvm::ConstantInt::get(builder->getInt32Ty(), loc.column);

    // Call the push function
    builder->CreateCall(pushFunc, {funcNameStr, filePathStr, lineVal, columnVal});
}

// Generate a call to pop the current function from the call stack
void LLVMCodegen::generatePopFrameCall() {
    // Kernel mode (issue #198): device functions have no host call-stack runtime.
    if (vyb::g_kernel_mode) return;
    llvm::Function* popFunc = getPopCallFrameFunction(module.get(), context.get());
    builder->CreateCall(popFunc, {});
}

} // namespace vyb
