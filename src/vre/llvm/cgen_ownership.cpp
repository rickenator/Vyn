#include "vyb/vre/llvm/codegen.hpp"
#include "vyb/parser/ast.hpp"
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>
#include <iostream>

using namespace vyb;

// ============================================================================
// CONTROL BLOCK STRUCTURE FOR our<T> AND mild<T>
// ============================================================================
//
// Control blocks enable mild<T> references to detect when our<T> objects
// are freed while allowing the control block to survive for weak reference
// tracking.
//
// Memory Layout:
//   struct ControlBlock {
//     i32 strong_count;    // Number of our<T> references (shared ownership)
//     i32 weak_count;      // Number of mild<T> references (weak references)
//     i1  object_freed;    // True when object destroyed (for .released() check)
//     T*  object_ptr;      // Pointer to actual object data
//   }
//
// Lifecycle:
//   1. our() allocates control block + object
//   2. soft() increments weak_count
//   3. our<T> destructor: decrements strong_count, if 0 frees object & sets object_freed
//   4. mild<T> destructor: decrements weak_count
//   5. Control block freed when BOTH strong_count==0 AND weak_count==0
//
// Thread Safety:
//   - All count increments/decrements use atomic operations
//   - object_freed reads use atomic load with acquire semantics
//
// ============================================================================

// Helper function to get or create control block struct type
llvm::StructType* LLVMCodegen::getControlBlockType(llvm::Type* objectPtrType) {
    // Control block: { i32 strong_count, i32 weak_count, i8 object_freed, T* object_ptr }
    // Note: object_freed is i8 (not i1) because LLVM requires atomic loads to be byte-sized
    std::vector<llvm::Type*> fields = {
        llvm::Type::getInt32Ty(*context),  // strong_count
        llvm::Type::getInt32Ty(*context),  // weak_count
        llvm::Type::getInt8Ty(*context),   // object_freed (i8 for atomic compatibility)
        objectPtrType                       // object_ptr (pointer to actual object)
    };

    return llvm::StructType::get(*context, fields, /*isPacked=*/false);
}

// --- Scope Management ---
void LLVMCodegen::enterScope() {
    VYB_CDBG << "DEBUG: Entering new scope (depth: " << scopeStack.size() + 1 << ")" << std::endl;
    scopeStack.emplace_back(); // Create new scope level
}

void LLVMCodegen::exitScope() {
    if (scopeStack.empty()) {
        VYB_CDBG << "WARNING: Attempted to exit scope but no scopes active" << std::endl;
        return;
    }

    VYB_CDBG << "DEBUG: Exiting scope (depth: " << scopeStack.size() << "), cleaning up "
              << scopeStack.back().size() << " variables" << std::endl;

    // Clean up all variables in current scope in reverse order (LIFO)
    auto& currentScope = scopeStack.back();
    for (auto it = currentScope.rbegin(); it != currentScope.rend(); ++it) {
        cleanupVariable(*it);
    }

    scopeStack.pop_back(); // Remove current scope
}

void LLVMCodegen::exitToFunctionBaseline() {
    // A `return` deep inside nested blocks (e.g. `while { if { return } }`) must
    // clean ALL of the function's live scopes - function body and parameters -
    // so their cleanup IR is emitted before the return's terminator (the last
    // cleanup moves the insert point into the terminator-free continue block the
    // ret is then emitted into). Without this, an empty loop-exit block would be
    // left terminated by ret and then re-cleaned later into that same block.
    //
    // LLVM codegen walks control-flow paths sequentially over ONE shared scope
    // stack, so a return inside one branch must not permanently destroy scopes
    // that sibling paths still depend on (e.g. declarations that follow an
    // `if { return }` in the same function body). We therefore clean up to the
    // function baseline, then restore the stack to its pre-return state. The
    // cleanup IR is already emitted into this branch's own basic blocks (which
    // are mutually exclusive with any continuation path), so restoring the stack
    // only affects static bookkeeping, not runtime behavior.
    auto savedStack = scopeStack;
    while (!scopeStack.empty() && scopeStack.size() > m_functionScopeBaseline) {
        exitScope();
    }
    scopeStack = std::move(savedStack);
}

void LLVMCodegen::registerVariable(const std::string& name, llvm::Value* allocaInst, llvm::Value* value,
                                  ast::OwnershipKind ownership, llvm::Type* type, bool needsCleanup) {
    if (scopeStack.empty()) {
        std::cout << "ERROR: No active scope to register variable: " << name << std::endl;
        return;
    }

    VYB_CDBG << "DEBUG: Registering variable '" << name << "' with ownership: "
              << static_cast<int>(ownership) << ", needsCleanup: " << needsCleanup << std::endl;

    ScopeVariable var;
    var.name = name;
    var.allocaInst = allocaInst;
    var.value = value;
    var.ownership = ownership;
    var.needsCleanup = needsCleanup;
    var.type = type;
    var.isVecWithMallocData = needsCleanup && isVecStructType(type);

    if (var.isVecWithMallocData) {
        VYB_CDBG << "DEBUG: Variable '" << name << "' identified as Vec with malloc'd data" << std::endl;
    }

    scopeStack.back().push_back(var);

    // Handle reference counting for our<T>
    if (ownership == ast::OwnershipKind::OUR) {
        incrementRefCount(name);
    }
}

void LLVMCodegen::cleanupVariable(const ScopeVariable& var) {
    VYB_CDBG << "DEBUG: Cleaning up variable '" << var.name << "', needsCleanup: "
              << var.needsCleanup << ", isVecWithMallocData: " << var.isVecWithMallocData << std::endl;

    // Skip cleanup if not needed
    if (!var.needsCleanup) {
        VYB_CDBG << "DEBUG: Skipping cleanup for '" << var.name << "' (needsCleanup=false)" << std::endl;
        return;
    }

    // Closure-typed variables own one reference to a capture environment.
    // needsCleanup is only true for bindings recognized as `fn` at declaration,
    // so a coincidentally {ptr, ptr}-shaped tuple is never ref-counted here.
    if (isClosureStructType(var.type)) {
        auto astIt = valueTypeMap.find(var.allocaInst);
        bool unknownAst = (astIt == valueTypeMap.end());
        bool astFn = !unknownAst && isFnTypeNode(astIt->second.get());
        if (unknownAst || astFn) {
            VYB_CDBG << "DEBUG: Performing cleanup for closure variable: " << var.name << std::endl;
            releaseClosureAlloca(var.allocaInst);
        }
        return;
    }

    switch (var.ownership) {
        case ast::OwnershipKind::MY: {
            // Unique ownership - immediate cleanup
            if (var.isVecWithMallocData) {
                VYB_CDBG << "DEBUG: Performing MY cleanup for Vec with malloc'd data: " << var.name << std::endl;

                // Safety check: ensure we have valid alloca and builder
                if (!var.allocaInst || !builder || !currentFunction) {
                    std::cout << "ERROR: Invalid state for cleanup of " << var.name << std::endl;
                    return;
                }

                // Load the Vec struct to access the data pointer
                llvm::Value* vecPtr = builder->CreateLoad(var.type, var.allocaInst, var.name + "_cleanup_load");

                // Extract the data pointer (field 0) from the Vec struct
                llvm::Value* dataPtr = builder->CreateExtractValue(vecPtr, 0, var.name + "_data_ptr");

                // Create null check before freeing
                llvm::Value* isNotNull = builder->CreateICmpNE(dataPtr,
                    llvm::ConstantPointerNull::get(llvm::PointerType::get(*context, 0)),
                    var.name + "_null_check");

                llvm::BasicBlock* freeBlock = llvm::BasicBlock::Create(*context, var.name + "_free_block", currentFunction);
                llvm::BasicBlock* continueBlock = llvm::BasicBlock::Create(*context, var.name + "_continue", currentFunction);

                builder->CreateCondBr(isNotNull, freeBlock, continueBlock);

                // Free block
                builder->SetInsertPoint(freeBlock);
                llvm::Function* freeFunc = getOrCreateFreeFunction();
                builder->CreateCall(freeFunc, {dataPtr});
                VYB_CDBG << "DEBUG: Generated free() call for " << var.name << std::endl;
                builder->CreateBr(continueBlock);

                // Continue block
                builder->SetInsertPoint(continueBlock);

                // If this cleanup is happening right before a return, we need to
                // ensure the continue block has proper termination
                // This will be handled by the subsequent return statement
            } else {
                VYB_CDBG << "DEBUG: Skipping MY cleanup for non-Vec variable: " << var.name << std::endl;
            }
            break;
        }

        case ast::OwnershipKind::OUR: {
            // Control block-based reference counting
            // The var.value is a pointer to the control block
            VYB_CDBG << "DEBUG: Cleaning up OUR ownership for variable: " << var.name << std::endl;

            // Load the control block pointer from the alloca
            // For non-pointer types (e.g. our<Int>), there is no control block to clean up
            if (!var.type->isPointerTy()) {
                VYB_CDBG << "DEBUG: Skipping OUR cleanup for non-pointer type: " << var.name << std::endl;
                return;
            }

            llvm::Value* controlBlockPtr = builder->CreateLoad(var.type, var.allocaInst, var.name + "_cb_load");

            // Check if control block is null
            llvm::Value* isNull = builder->CreateICmpEQ(controlBlockPtr,
                llvm::ConstantPointerNull::get(llvm::PointerType::get(*context, 0)),
                var.name + "_null_check");

            llvm::BasicBlock* cleanupBlock = llvm::BasicBlock::Create(*context, var.name + "_our_cleanup", currentFunction);
            llvm::BasicBlock* continueBlock = llvm::BasicBlock::Create(*context, var.name + "_our_continue", currentFunction);

            builder->CreateCondBr(isNull, continueBlock, cleanupBlock);

            // Cleanup block: decrement strong_count atomically
            builder->SetInsertPoint(cleanupBlock);

            // Reconstruct control block type: { i32, i32, i8, ptr }
            std::vector<llvm::Type*> cbFields = {
                llvm::Type::getInt32Ty(*context),  // strong_count
                llvm::Type::getInt32Ty(*context),  // weak_count
                llvm::Type::getInt8Ty(*context),   // object_freed (i8 for atomic)
                llvm::PointerType::get(*context, 0) // object_ptr
            };
            llvm::StructType* controlBlockType = llvm::StructType::get(*context, cbFields, /*isPacked=*/false);

            // Get pointer to strong_count (field 0)
            llvm::Value* strongCountPtr = builder->CreateStructGEP(controlBlockType, controlBlockPtr, 0,
                var.name + "_strong_count_ptr");

            // Atomic decrement: strong_count--
            llvm::AtomicRMWInst* decremented = builder->CreateAtomicRMW(
                llvm::AtomicRMWInst::Sub,
                strongCountPtr,
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 1),
                llvm::MaybeAlign(),
                llvm::AtomicOrdering::AcquireRelease
            );

            // Check if we just decremented to zero (the returned value is the OLD value)
            llvm::Value* wasOne = builder->CreateICmpEQ(decremented,
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 1),
                var.name + "_strong_was_one");

            llvm::BasicBlock* freeObjectBlock = llvm::BasicBlock::Create(*context, var.name + "_free_object", currentFunction);
            llvm::BasicBlock* checkCBFreeBlock = llvm::BasicBlock::Create(*context, var.name + "_check_cb_free", currentFunction);

            builder->CreateCondBr(wasOne, freeObjectBlock, checkCBFreeBlock);

            // Free object block: strong_count reached zero
            builder->SetInsertPoint(freeObjectBlock);

            // Get object pointer (field 3)
            llvm::Value* objectPtrFieldPtr = builder->CreateStructGEP(controlBlockType, controlBlockPtr, 3,
                var.name + "_obj_ptr_field_ptr");
            llvm::Value* objectPtr = builder->CreateLoad(
                llvm::PointerType::get(*context, 0),
                objectPtrFieldPtr,
                var.name + "_obj_ptr");

            // Free the object
            llvm::Function* freeFunc = getOrCreateFreeFunction();
            builder->CreateCall(freeFunc, {objectPtr});

            // Set object_freed flag to true (field 2)
            llvm::Value* objectFreedPtr = builder->CreateStructGEP(controlBlockType, controlBlockPtr, 2,
                var.name + "_obj_freed_ptr");
            builder->CreateStore(
                llvm::ConstantInt::get(llvm::Type::getInt8Ty(*context), 1),
                objectFreedPtr);

            builder->CreateBr(checkCBFreeBlock);

            // Check if control block can be freed
            builder->SetInsertPoint(checkCBFreeBlock);

            // Load weak_count (field 1)
            llvm::Value* weakCountPtr = builder->CreateStructGEP(controlBlockType, controlBlockPtr, 1,
                var.name + "_weak_count_ptr");
            llvm::Value* weakCount = builder->CreateLoad(llvm::Type::getInt32Ty(*context), weakCountPtr,
                var.name + "_weak_count");

            // Load strong_count again (might have been decremented by another thread)
            llvm::Value* strongCount = builder->CreateLoad(llvm::Type::getInt32Ty(*context), strongCountPtr,
                var.name + "_strong_count");

            // Free control block if BOTH counts are zero
            llvm::Value* strongIsZero = builder->CreateICmpEQ(strongCount,
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0),
                var.name + "_strong_is_zero");
            llvm::Value* weakIsZero = builder->CreateICmpEQ(weakCount,
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0),
                var.name + "_weak_is_zero");
            llvm::Value* bothZero = builder->CreateAnd(strongIsZero, weakIsZero, var.name + "_both_zero");

            llvm::BasicBlock* freeCBBlock = llvm::BasicBlock::Create(*context, var.name + "_free_cb", currentFunction);
            llvm::BasicBlock* doneCBBlock = llvm::BasicBlock::Create(*context, var.name + "_cb_done", currentFunction);

            builder->CreateCondBr(bothZero, freeCBBlock, doneCBBlock);

            // Free control block
            builder->SetInsertPoint(freeCBBlock);
            builder->CreateCall(freeFunc, {controlBlockPtr});
            builder->CreateBr(doneCBBlock);

            // Done with control block cleanup
            builder->SetInsertPoint(doneCBBlock);
            builder->CreateBr(continueBlock);

            // Continue block
            builder->SetInsertPoint(continueBlock);
            break;
        }

        case ast::OwnershipKind::THEIR: {
            // Borrowed reference - no cleanup needed
            VYB_CDBG << "DEBUG: No cleanup needed for borrowed reference: " << var.name << std::endl;
            break;
        }

        case ast::OwnershipKind::MILD: {
            // Weak reference - decrement weak_count in control block
            VYB_CDBG << "DEBUG: Cleaning up MILD ownership for variable: " << var.name << std::endl;

            // Load the control block pointer from the alloca
            // For non-pointer types (e.g. mild<Int>), there is no control block to clean up
            if (!var.type->isPointerTy()) {
                VYB_CDBG << "DEBUG: Skipping MILD cleanup for non-pointer type: " << var.name << std::endl;
                return;
            }

            llvm::Value* controlBlockPtr = builder->CreateLoad(var.type, var.allocaInst, var.name + "_mild_cb_load");

            // Check if control block is null
            llvm::Value* isNull = builder->CreateICmpEQ(controlBlockPtr,
                llvm::ConstantPointerNull::get(llvm::PointerType::get(*context, 0)),
                var.name + "_mild_null_check");

            llvm::BasicBlock* cleanupBlock = llvm::BasicBlock::Create(*context, var.name + "_mild_cleanup", currentFunction);
            llvm::BasicBlock* continueBlock = llvm::BasicBlock::Create(*context, var.name + "_mild_continue", currentFunction);

            builder->CreateCondBr(isNull, continueBlock, cleanupBlock);

            // Cleanup block: decrement weak_count atomically
            builder->SetInsertPoint(cleanupBlock);

            // Reconstruct control block type: { i32, i32, i8, ptr }
            std::vector<llvm::Type*> cbFields = {
                llvm::Type::getInt32Ty(*context),  // strong_count
                llvm::Type::getInt32Ty(*context),  // weak_count
                llvm::Type::getInt8Ty(*context),   // object_freed (i8 for atomic)
                llvm::PointerType::get(*context, 0) // object_ptr
            };
            llvm::StructType* controlBlockType = llvm::StructType::get(*context, cbFields, /*isPacked=*/false);

            // Get pointer to weak_count (field 1)
            llvm::Value* weakCountPtr = builder->CreateStructGEP(controlBlockType, controlBlockPtr, 1,
                var.name + "_mild_weak_count_ptr");

            // Atomic decrement: weak_count--
            llvm::AtomicRMWInst* decremented = builder->CreateAtomicRMW(
                llvm::AtomicRMWInst::Sub,
                weakCountPtr,
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 1),
                llvm::MaybeAlign(),
                llvm::AtomicOrdering::AcquireRelease
            );

            // Check if we just decremented to zero (returned value is OLD value)
            llvm::Value* wasOne = builder->CreateICmpEQ(decremented,
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 1),
                var.name + "_mild_weak_was_one");

            llvm::BasicBlock* checkCBFreeBlock = llvm::BasicBlock::Create(*context, var.name + "_mild_check_cb_free", currentFunction);
            llvm::BasicBlock* doneMildBlock = llvm::BasicBlock::Create(*context, var.name + "_mild_done", currentFunction);

            builder->CreateCondBr(wasOne, checkCBFreeBlock, doneMildBlock);

            // Check if control block can be freed (both counts must be zero)
            builder->SetInsertPoint(checkCBFreeBlock);

            // Get pointer to strong_count (field 0)
            llvm::Value* strongCountPtr = builder->CreateStructGEP(controlBlockType, controlBlockPtr, 0,
                var.name + "_mild_strong_count_ptr");

            // Load strong_count
            llvm::Value* strongCount = builder->CreateLoad(llvm::Type::getInt32Ty(*context), strongCountPtr,
                var.name + "_mild_strong_count");

            // Free control block only if strong_count is also zero
            llvm::Value* strongIsZero = builder->CreateICmpEQ(strongCount,
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0),
                var.name + "_mild_strong_is_zero");

            llvm::BasicBlock* freeCBBlock = llvm::BasicBlock::Create(*context, var.name + "_mild_free_cb", currentFunction);
            llvm::BasicBlock* skipFreeCBBlock = llvm::BasicBlock::Create(*context, var.name + "_mild_skip_free_cb", currentFunction);

            builder->CreateCondBr(strongIsZero, freeCBBlock, skipFreeCBBlock);

            // Free control block
            builder->SetInsertPoint(freeCBBlock);
            llvm::Function* freeFunc = getOrCreateFreeFunction();
            builder->CreateCall(freeFunc, {controlBlockPtr});
            builder->CreateBr(skipFreeCBBlock);

            // Skip free block
            builder->SetInsertPoint(skipFreeCBBlock);
            builder->CreateBr(doneMildBlock);

            // Done with mild cleanup
            builder->SetInsertPoint(doneMildBlock);
            builder->CreateBr(continueBlock);

            // Continue block
            builder->SetInsertPoint(continueBlock);
            break;
        }
    }
}

void LLVMCodegen::incrementRefCount(const std::string& name) {
    VYB_CDBG << "DEBUG: Incrementing refcount for: " << name << std::endl;

    // Check if refcount storage already exists
    auto it = refCountStorage.find(name);
    if (it == refCountStorage.end()) {
        // Create new refcount storage
        llvm::Type* int32Type = llvm::Type::getInt32Ty(*context);
        llvm::Value* refCountAlloca = createEntryBlockAlloca(int32Type, name + "_refcount");
        builder->CreateStore(llvm::ConstantInt::get(int32Type, 1), refCountAlloca);
        refCountStorage[name] = refCountAlloca;
        refCounts[name] = 1;
    } else {
        // Increment existing refcount
        llvm::Value* currentCount = builder->CreateLoad(llvm::Type::getInt32Ty(*context), it->second, name + "_refcount_load");
        llvm::Value* newCount = builder->CreateAdd(currentCount, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 1));
        builder->CreateStore(newCount, it->second);
        refCounts[name]++;
    }
}

void LLVMCodegen::decrementRefCount(const std::string& name) {
    VYB_CDBG << "DEBUG: Decrementing refcount for: " << name << std::endl;

    auto it = refCountStorage.find(name);
    if (it == refCountStorage.end()) {
        std::cout << "ERROR: Attempted to decrement refcount for unknown variable: " << name << std::endl;
        return;
    }

    // Load current refcount
    llvm::Value* currentCount = builder->CreateLoad(llvm::Type::getInt32Ty(*context), it->second, name + "_refcount_load");
    llvm::Value* newCount = builder->CreateSub(currentCount, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 1));
    builder->CreateStore(newCount, it->second);

    // Check if refcount reached zero
    llvm::Value* isZero = builder->CreateICmpEQ(newCount, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0));

    llvm::BasicBlock* cleanupBlock = llvm::BasicBlock::Create(*context, name + "_refcount_cleanup", currentFunction);
    llvm::BasicBlock* continueBlock = llvm::BasicBlock::Create(*context, name + "_refcount_continue", currentFunction);

    builder->CreateCondBr(isZero, cleanupBlock, continueBlock);

    // Cleanup block - perform actual cleanup when refcount hits zero
    builder->SetInsertPoint(cleanupBlock);

    // Find the variable in scope stack and perform cleanup
    for (auto& scope : scopeStack) {
        for (auto& var : scope) {
            if (var.name == name && var.isVecWithMallocData) {
                VYB_CDBG << "DEBUG: Performing OUR cleanup for Vec with malloc'd data: " << name << std::endl;

                // Same cleanup logic as MY ownership but triggered by refcount
                llvm::Value* vecPtr = builder->CreateLoad(var.type, var.allocaInst, name + "_refcount_cleanup_load");
                llvm::Value* dataPtr = builder->CreateExtractValue(vecPtr, 0, name + "_refcount_data_ptr");

                llvm::Value* isNotNull = builder->CreateICmpNE(dataPtr,
                    llvm::ConstantPointerNull::get(llvm::PointerType::get(*context, 0)),
                    name + "_refcount_null_check");

                llvm::BasicBlock* freeBlock = llvm::BasicBlock::Create(*context, name + "_refcount_free", currentFunction);
                llvm::BasicBlock* cleanupContinue = llvm::BasicBlock::Create(*context, name + "_refcount_cleanup_continue", currentFunction);

                builder->CreateCondBr(isNotNull, freeBlock, cleanupContinue);

                builder->SetInsertPoint(freeBlock);
                llvm::Function* freeFunc = getOrCreateFreeFunction();
                builder->CreateCall(freeFunc, {dataPtr});
                builder->CreateBr(cleanupContinue);

                builder->SetInsertPoint(cleanupContinue);
                break;
            }
        }
    }

    builder->CreateBr(continueBlock);

    // Continue block
    builder->SetInsertPoint(continueBlock);

    // Update local counter
    if (refCounts[name] > 0) {
        refCounts[name]--;
    }
}

// --- Helper Functions for Memory Management ---
llvm::Function* LLVMCodegen::getOrCreateFreeFunction() {
    llvm::Function* freeFunc = module->getFunction("free");
    if (!freeFunc) {
        VYB_CDBG << "DEBUG: Creating free() function declaration" << std::endl;
        llvm::FunctionType* freeFuncType = llvm::FunctionType::get(
            llvm::Type::getVoidTy(*context),                    // return type
            {llvm::PointerType::get(*context, 0)},              // parameter: void*
            false                                                // not variadic
        );
        freeFunc = llvm::Function::Create(freeFuncType, llvm::Function::ExternalLinkage, "free", module.get());
    }
    return freeFunc;
}

llvm::Function* LLVMCodegen::getOrCreateVybStringFreeFunction() {
    llvm::Function* freeFunc = module->getFunction("__vyb_string_free");
    if (!freeFunc) {
        VYB_CDBG << "DEBUG: Creating __vyb_string_free() function declaration" << std::endl;
        llvm::FunctionType* freeFuncType = llvm::FunctionType::get(
            llvm::Type::getVoidTy(*context),
            {llvm::PointerType::get(*context, 0)},
            false
        );
        freeFunc = llvm::Function::Create(freeFuncType, llvm::Function::ExternalLinkage, "__vyb_string_free", module.get());
    }
    return freeFunc;
}

llvm::Function* LLVMCodegen::getOrCreateVybStringRegisterFunction() {
    llvm::Function* regFunc = module->getFunction("__vyb_string_register");
    if (!regFunc) {
        VYB_CDBG << "DEBUG: Creating __vyb_string_register() function declaration" << std::endl;
        llvm::FunctionType* regFuncType = llvm::FunctionType::get(
            llvm::Type::getVoidTy(*context),
            {llvm::PointerType::get(*context, 0)},
            false
        );
        regFunc = llvm::Function::Create(regFuncType, llvm::Function::ExternalLinkage, "__vyb_string_register", module.get());
    }
    return regFunc;
}

llvm::Function* LLVMCodegen::getOrCreateMallocFunction() {
    llvm::Function* mallocFunc = module->getFunction("malloc");
    if (!mallocFunc) {
        VYB_CDBG << "DEBUG: Creating malloc() function declaration" << std::endl;
        llvm::FunctionType* mallocFuncType = llvm::FunctionType::get(
            llvm::PointerType::get(*context, 0),                // return type: void*
            {llvm::Type::getInt64Ty(*context)},                 // parameter: size_t
            false                                                // not variadic
        );
        mallocFunc = llvm::Function::Create(mallocFuncType, llvm::Function::ExternalLinkage, "malloc", module.get());
    }
    return mallocFunc;
}

llvm::Function* LLVMCodegen::getOrCreateMemsetFunction() {
    llvm::Function* memsetFunc = module->getFunction("memset");
    if (!memsetFunc) {
        VYB_CDBG << "DEBUG: Creating memset() function declaration" << std::endl;
        llvm::FunctionType* memsetFuncType = llvm::FunctionType::get(
            llvm::PointerType::get(*context, 0),                // return type: void*
            {llvm::PointerType::get(*context, 0),               // void* ptr
             llvm::Type::getInt32Ty(*context),                  // int value
             llvm::Type::getInt64Ty(*context)},                 // size_t size
            false                                                // not variadic
        );
        memsetFunc = llvm::Function::Create(memsetFuncType, llvm::Function::ExternalLinkage, "memset", module.get());
    }
    return memsetFunc;
}
llvm::Function* LLVMCodegen::getOrCreateMemcpyFunction() {
    llvm::Function* memcpyFunc = module->getFunction("memcpy");
    if (!memcpyFunc) {
        llvm::FunctionType* memcpyFuncType = llvm::FunctionType::get(
            llvm::PointerType::get(*context, 0),                // return type: void*
            {llvm::PointerType::get(*context, 0),               // void* dest
             llvm::PointerType::get(*context, 0),               // const void* src
             llvm::Type::getInt64Ty(*context)},                 // size_t n
            false
        );
        memcpyFunc = llvm::Function::Create(memcpyFuncType, llvm::Function::ExternalLinkage, "memcpy", module.get());
    }
    return memcpyFunc;
}

// Generate a deep copy of a Vec struct value.
// Returns an updated Vec struct value whose data field points to freshly malloc'd memory.
// The caller's original Vec is unmodified; each function invocation owns independent data.
llvm::Value* LLVMCodegen::generateVecDeepCopy(llvm::Value* vecStructValue,
                                               llvm::Type* elemType,
                                               llvm::Type* vecStructType) {
    if (!vecStructValue || !elemType || !vecStructType) return vecStructValue;

    llvm::Function* currentFunc = builder->GetInsertBlock()->getParent();
    if (!currentFunc) return vecStructValue;

    // Extract the three Vec fields: { ptr, size, capacity }
    llvm::Value* srcDataPtr = builder->CreateExtractValue(vecStructValue, 0, "vdc.src_ptr");
    llvm::Value* vecSize    = builder->CreateExtractValue(vecStructValue, 1, "vdc.size");
    llvm::Value* vecCap     = builder->CreateExtractValue(vecStructValue, 2, "vdc.cap");

    // Compute element size in bytes
    llvm::DataLayout dataLayout(module.get());
    uint64_t elemSizeBytes = dataLayout.getTypeAllocSize(elemType);
    llvm::Value* elemSizeVal = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), elemSizeBytes);

    // Total bytes to copy = size * elemSize
    llvm::Value* totalBytes = builder->CreateMul(vecSize, elemSizeVal, "vdc.bytes");

    // Malloc a new buffer
    llvm::Function* mallocFunc = getOrCreateMallocFunction();
    llvm::Value* newDataPtr = builder->CreateCall(mallocFunc, {totalBytes}, "vdc.new_ptr");

    // If size > 0, memcpy the data; otherwise leave newDataPtr (may be garbage but won't be accessed)
    llvm::BasicBlock* copyBB  = llvm::BasicBlock::Create(*context, "vdc.copy", currentFunc);
    llvm::BasicBlock* doneBB  = llvm::BasicBlock::Create(*context, "vdc.done", currentFunc);
    llvm::Value* hasData = builder->CreateICmpSGT(
        vecSize, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0), "vdc.has_data");
    builder->CreateCondBr(hasData, copyBB, doneBB);

    builder->SetInsertPoint(copyBB);
    llvm::Function* memcpyFunc = getOrCreateMemcpyFunction();
    builder->CreateCall(memcpyFunc, {newDataPtr, srcDataPtr, totalBytes});
    builder->CreateBr(doneBB);

    builder->SetInsertPoint(doneBB);

    // Build a new Vec struct with the cloned data pointer
    llvm::Value* newVecStruct = llvm::UndefValue::get(vecStructType);
    newVecStruct = builder->CreateInsertValue(newVecStruct, newDataPtr, 0, "vdc.new_vec0");
    newVecStruct = builder->CreateInsertValue(newVecStruct, vecSize,    1, "vdc.new_vec1");
    newVecStruct = builder->CreateInsertValue(newVecStruct, vecCap,     2, "vdc.new_vec2");
    return newVecStruct;
}

// Vec struct layout: { ptr, i64 (size), i64 (capacity) }
bool LLVMCodegen::isVybStringStructType(llvm::Type* type) {
    auto* st = llvm::dyn_cast<llvm::StructType>(type);
    if (!st || st->getNumElements() != 2) return false;
    return st->getElementType(0)->isPointerTy() && st->getElementType(1)->isIntegerTy(64);
}

bool LLVMCodegen::isVecStructType(llvm::Type* type) {
    auto* st = llvm::dyn_cast<llvm::StructType>(type);
    if (!st || st->getNumElements() != 3) return false;
    if (!st->getElementType(0)->isPointerTy()) return false;
    if (!st->getElementType(1)->isIntegerTy(64)) return false;
    if (!st->getElementType(2)->isIntegerTy(64)) return false;
    return true;
}

// ============================================================================
// CLOSURE ENVIRONMENT REFERENCE COUNTING
// ============================================================================
// A closure value is the uniform `struct { ptr env, ptr fn }`. The env block is
// a heap allocation whose header is `{ i64 refcount; ptr cap_dtor }` followed by
// the captured fields. Reference counting lets multiple closure values share the
// same env safely: copying a closure into a durable storage location retains the
// env (+1), and destroying that location releases it (-1). When the last ref is
// dropped the per-layout cap_dtor (if any) releases captured `our<T>` strong
// counts, then the env is freed.

llvm::Function* LLVMCodegen::getOrCreateClosureRetainFunction() {
    if (auto* f = module->getFunction("__vyb_closure_retain")) return f;
    llvm::FunctionType* ty = llvm::FunctionType::get(
        llvm::PointerType::get(*context, 0),
        {llvm::PointerType::get(*context, 0)}, false);
    return llvm::Function::Create(ty, llvm::Function::ExternalLinkage,
                                  "__vyb_closure_retain", module.get());
}

llvm::Function* LLVMCodegen::getOrCreateClosureReleaseFunction() {
    if (auto* f = module->getFunction("__vyb_closure_release")) return f;
    llvm::FunctionType* ty = llvm::FunctionType::get(
        llvm::Type::getVoidTy(*context),
        {llvm::PointerType::get(*context, 0)}, false);
    return llvm::Function::Create(ty, llvm::Function::ExternalLinkage,
                                  "__vyb_closure_release", module.get());
}

bool LLVMCodegen::isClosureStructType(llvm::Type* type) {
    auto* st = llvm::dyn_cast<llvm::StructType>(type);
    if (!st || st->getNumElements() != 2) return false;
    return st->getElementType(0)->isPointerTy() && st->getElementType(1)->isPointerTy();
}

bool LLVMCodegen::isFnTypeNode(const vyb::ast::TypeNode* tn) const {
    if (!tn) return false;
    if (dynamic_cast<const vyb::ast::FunctionType*>(tn)) return true;
    if (tn->type && dynamic_cast<const vyb::ast::FunctionType*>(tn->type.get())) return true;
    return false;
}

void LLVMCodegen::retainClosureValue(llvm::Value* closureVal) {
    if (!closureVal || !isClosureStructType(closureVal->getType())) return;
    llvm::StructType* closureTy = llvm::cast<llvm::StructType>(closureVal->getType());
    llvm::Value* env = builder->CreateExtractValue(closureVal, 0, "cl.retain.env");
    llvm::Value* isNull = builder->CreateICmpEQ(
        env, llvm::ConstantPointerNull::get(llvm::PointerType::get(*context, 0)),
        "cl.retain.null");
    llvm::BasicBlock* doRetain = llvm::BasicBlock::Create(*context, "cl.retain.yes", currentFunction);
    llvm::BasicBlock* done = llvm::BasicBlock::Create(*context, "cl.retain.done", currentFunction);
    builder->CreateCondBr(isNull, done, doRetain);
    builder->SetInsertPoint(doRetain);
    llvm::Function* retainFn = getOrCreateClosureRetainFunction();
    builder->CreateCall(retainFn, {env});
    builder->CreateBr(done);
    builder->SetInsertPoint(done);
}

void LLVMCodegen::releaseClosureValue(llvm::Value* closureVal) {
    if (!closureVal || !isClosureStructType(closureVal->getType())) return;
    llvm::StructType* closureTy = llvm::cast<llvm::StructType>(closureVal->getType());
    llvm::Value* env = builder->CreateExtractValue(closureVal, 0, "cl.release.env");
    llvm::Value* isNull = builder->CreateICmpEQ(
        env, llvm::ConstantPointerNull::get(llvm::PointerType::get(*context, 0)),
        "cl.release.null");
    llvm::BasicBlock* doRel = llvm::BasicBlock::Create(*context, "cl.release.yes", currentFunction);
    llvm::BasicBlock* done = llvm::BasicBlock::Create(*context, "cl.release.done", currentFunction);
    builder->CreateCondBr(isNull, done, doRel);
    builder->SetInsertPoint(doRel);
    llvm::Function* releaseFn = getOrCreateClosureReleaseFunction();
    builder->CreateCall(releaseFn, {env});
    builder->CreateBr(done);
    builder->SetInsertPoint(done);
}

void LLVMCodegen::releaseClosureAlloca(llvm::Value* allocaInst) {
    if (!allocaInst) return;
    llvm::Type* allocTy = nullptr;
    if (auto* ai = llvm::dyn_cast<llvm::AllocaInst>(allocaInst)) {
        allocTy = ai->getAllocatedType();
    }
    if (!allocTy || !isClosureStructType(allocTy)) return;
    llvm::Value* closureVal = builder->CreateLoad(allocTy, allocaInst, "cl.release.alloca.load");
    releaseClosureValue(closureVal);
}
