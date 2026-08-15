// SPDX-License-Identifier: Apache-2.0

#include "vyb/vre/llvm/codegen.hpp"
#include "vyb/runtime/async_runtime.hpp"
#include "vyb/parser/ast.hpp"

// LLVM Headers for async codegen
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"

#include <iostream>
#include <string>

namespace vyb {

// Async state machine management for LLVMCodegen
struct AsyncState {
    llvm::Function* asyncFunction;           // The original async function being converted
    llvm::Function* stateMachineFunction;    // The state machine continuation function
    llvm::StructType* stateStructType;       // Struct to hold local variables between suspensions
    llvm::Value* stateStructInstance;        // Instance of the state struct
    llvm::Value* currentStateValue;          // Current state number (for switch statement)
    llvm::BasicBlock* resumeBlock;           // Block to resume execution
    llvm::Value* futureValue;                // The Future<T> being returned
    int stateCounter;                        // Counter for generating unique state numbers

    AsyncState() : asyncFunction(nullptr), stateMachineFunction(nullptr),
                   stateStructType(nullptr), stateStructInstance(nullptr),
                   currentStateValue(nullptr), resumeBlock(nullptr),
                   futureValue(nullptr), stateCounter(0) {}
};

// Helper functions for async codegen (will be used by LLVMCodegen)
namespace async_codegen {

// Create async runtime integration functions
llvm::Function* getOrCreateScheduleTaskFunction(llvm::Module* module, llvm::LLVMContext& context) {
    const std::string funcName = "vyb_schedule_async_task";

    if (auto existingFunc = module->getFunction(funcName)) {
        return existingFunc;
    }

    // Function signature: i64 vyb_schedule_async_task(i8* function_ptr, i8* state_ptr)
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);
    llvm::Type* int8PtrType = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);

    std::vector<llvm::Type*> paramTypes = { int8PtrType, int8PtrType };
    llvm::FunctionType* funcType = llvm::FunctionType::get(int64Type, paramTypes, false);

    return llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, funcName, module);
}

llvm::Function* getOrCreateAwaitTaskFunction(llvm::Module* module, llvm::LLVMContext& context) {
    const std::string funcName = "vyb_await_task";

    if (auto existingFunc = module->getFunction(funcName)) {
        return existingFunc;
    }

    // Function signature: i8* vyb_await_task(i64 task_id)
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);
    llvm::Type* int8PtrType = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);

    std::vector<llvm::Type*> paramTypes = { int64Type };
    llvm::FunctionType* funcType = llvm::FunctionType::get(int8PtrType, paramTypes, false);

    return llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, funcName, module);
}

llvm::Function* getOrCreateCreateFutureFunction(llvm::Module* module, llvm::LLVMContext& context) {
    const std::string funcName = "vyb_create_future";

    if (auto existingFunc = module->getFunction(funcName)) {
        return existingFunc;
    }

    // Function signature: i8* vyb_create_future(i64 task_id)
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);
    llvm::Type* int8PtrType = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);

    std::vector<llvm::Type*> paramTypes = { int64Type };
    llvm::FunctionType* funcType = llvm::FunctionType::get(int8PtrType, paramTypes, false);

    return llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, funcName, module);
}

// Generate state machine wrapper for async function
llvm::Function* generateAsyncStateMachine(llvm::Function* originalFunc, llvm::LLVMContext& context, llvm::Module* module) {
    // Create the state machine function signature: void state_machine(i8* state_data)
    llvm::Type* voidType = llvm::Type::getVoidTy(context);
    llvm::Type* int8PtrType = llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);

    std::vector<llvm::Type*> paramTypes = { int8PtrType };
    llvm::FunctionType* stateMachineType = llvm::FunctionType::get(voidType, paramTypes, false);

    std::string stateMachineName = originalFunc->getName().str() + "_state_machine";
    llvm::Function* stateMachineFunc = llvm::Function::Create(
        stateMachineType, llvm::Function::InternalLinkage, stateMachineName, module);

    // Create basic blocks for the state machine
    llvm::BasicBlock* entryBlock = llvm::BasicBlock::Create(context, "entry", stateMachineFunc);
    llvm::BasicBlock* switchBlock = llvm::BasicBlock::Create(context, "state_switch", stateMachineFunc);
    llvm::BasicBlock* exitBlock = llvm::BasicBlock::Create(context, "exit", stateMachineFunc);

    llvm::IRBuilder<> builder(context);

    // Entry block: load state and jump to switch
    builder.SetInsertPoint(entryBlock);
    llvm::Value* stateDataPtr = stateMachineFunc->getArg(0);

    // Cast state data to our state struct (will be defined per function)
    // For now, assume first i32 is the state number
    llvm::Value* stateNumberPtr = builder.CreateBitCast(stateDataPtr,
        llvm::PointerType::get(llvm::Type::getInt32Ty(context), 0));
    llvm::Value* stateNumber = builder.CreateLoad(llvm::Type::getInt32Ty(context), stateNumberPtr);

    builder.CreateBr(switchBlock);

    // Switch block: dispatch based on state
    builder.SetInsertPoint(switchBlock);
    llvm::SwitchInst* switchInst = builder.CreateSwitch(stateNumber, exitBlock);

    // State 0: Initial execution
    llvm::BasicBlock* state0Block = llvm::BasicBlock::Create(context, "state_0", stateMachineFunc);
    switchInst->addCase(llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0), state0Block);

    builder.SetInsertPoint(state0Block);
    // TODO: This is where we'd implement the actual async function logic
    // For now, just jump to exit
    builder.CreateBr(exitBlock);

    // Exit block
    builder.SetInsertPoint(exitBlock);
    builder.CreateRetVoid();

    return stateMachineFunc;
}

} // namespace async_codegen

} // namespace vyb