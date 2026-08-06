#include "vyb/vre/llvm/codegen.hpp"
#include "vyb/runtime/async_runtime.hpp"

// LLVM Headers
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Type.h"

namespace vyb {

// Implementation of async runtime integration functions for LLVMCodegen

llvm::Function* LLVMCodegen::getOrCreateScheduleTaskFunction() {
    const std::string funcName = "vyb_schedule_async_task";

    if (auto existingFunc = module->getFunction(funcName)) {
        return existingFunc;
    }

    // Function signature: i64 vyb_schedule_async_task(i8* function_ptr, i8* state_ptr)
    std::vector<llvm::Type*> paramTypes = { int8PtrType, int8PtrType };
    llvm::FunctionType* funcType = llvm::FunctionType::get(int64Type, paramTypes, false);

    llvm::Function* scheduleFunc = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, funcName, module.get());

    // Add a basic implementation (returns dummy task ID for now)
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(*context, "entry", scheduleFunc);
    llvm::IRBuilder<> funcBuilder(entry);
    funcBuilder.CreateRet(llvm::ConstantInt::get(int64Type, 1));

    return scheduleFunc;
}

llvm::Function* LLVMCodegen::getOrCreateAwaitTaskFunction() {
    const std::string funcName = "vyb_await_task";

    if (auto existingFunc = module->getFunction(funcName)) {
        return existingFunc;
    }

    // Function signature: void vyb_await_task(i64 task_id)
    std::vector<llvm::Type*> paramTypes = { int64Type };
    llvm::FunctionType* funcType = llvm::FunctionType::get(voidType, paramTypes, false);

    llvm::Function* awaitFunc = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, funcName, module.get());

    // Add a basic implementation (no-op for now)
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(*context, "entry", awaitFunc);
    llvm::IRBuilder<> funcBuilder(entry);
    funcBuilder.CreateRetVoid();

    return awaitFunc;
}

llvm::Function* LLVMCodegen::getOrCreateCreateFutureFunction() {
    const std::string funcName = "vyb_create_future";

    if (auto existingFunc = module->getFunction(funcName)) {
        return existingFunc;
    }

    // Function signature: i8* vyb_create_future(i64 task_id)
    std::vector<llvm::Type*> paramTypes = { int64Type };
    llvm::FunctionType* funcType = llvm::FunctionType::get(int8PtrType, paramTypes, false);

    llvm::Function* createFunc = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, funcName, module.get());

    // Add a basic implementation (returns null for now)
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(*context, "entry", createFunc);
    llvm::IRBuilder<> funcBuilder(entry);
    funcBuilder.CreateRet(llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(int8PtrType)));

    return createFunc;
}

llvm::StructType* LLVMCodegen::createFutureStructType(llvm::Type* resultType) {
    // Future<T> struct: { T* result, i32 state, i64 task_id, i8* runtime_data }
    std::vector<llvm::Type*> futureFields = {
        llvm::PointerType::get(resultType, 0),           // T* result
        int32Type,                                       // i32 state
        int64Type,                                       // i64 task_id
        int8PtrType                                      // i8* runtime_data
    };

    m_asyncResultType = resultType;  // Track result type for opaque pointer handling
    return llvm::StructType::create(*context, futureFields, "Future");
}

} // namespace vyb