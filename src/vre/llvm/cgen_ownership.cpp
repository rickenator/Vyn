// SPDX-License-Identifier: Apache-2.0

#include "vyb/vre/llvm/codegen.hpp"
#include "vyb/parser/ast.hpp"
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>
#include <iostream>

using namespace vyb;

// Forward declaration (defined later, below cleanupVariable): return the pointee
// type node of a `my<T>` wrapper, or null if not a `my`.
namespace {
const vyb::ast::TypeNode* myTypeArg(const vyb::ast::TypeNode* tn);
}

// Detect whether an AST type node is `Vec<String>` (either the VecType form or a
// TypeName "Vec" with a single `String` argument). The element identity cannot be
// recovered from the Vec struct's LLVM type ({ ptr, i64, i64 } is element-erased),
// so the AST type stored in valueTypeMap is the authoritative source.
static bool typeNodeIsVecOfString(const vyb::ast::TypeNode* tn) {
    if (!tn) return false;
    const vyb::ast::TypeNode* elem = nullptr;
    if (auto* vt = dynamic_cast<const vyb::ast::VecType*>(tn)) {
        elem = vt->elementType.get();
    } else if (auto* name = dynamic_cast<const vyb::ast::TypeName*>(tn)) {
        if (name->identifier && name->identifier->name == "Vec" && !name->genericArgs.empty()) {
            elem = name->genericArgs[0].get();
        }
    }
    if (!elem) return false;
    if (auto* en = dynamic_cast<const vyb::ast::TypeName*>(elem)) {
        if (en->identifier && en->identifier->name == "String") return true;
    }
    return false;
}

// Returns the element TypeNode of a Vec<T> AST type (T), or nullptr if `tn` is
// not a Vec type. Shared by the String and owned-struct element reclaim paths.
static const vyb::ast::TypeNode* vecElementTypeNode(const vyb::ast::TypeNode* tn) {
    if (!tn) return nullptr;
    if (auto* vt = dynamic_cast<const vyb::ast::VecType*>(tn)) {
        return vt->elementType.get();
    }
    if (auto* name = dynamic_cast<const vyb::ast::TypeName*>(tn)) {
        if (name->identifier && name->identifier->name == "Vec" && !name->genericArgs.empty()) {
            return name->genericArgs[0].get();
        }
    }
    return nullptr;
}

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

void LLVMCodegen::cleanupScopesToBaseline(size_t baseline) {
    // A `break` / `continue` escapes the loop body without reaching the body's
    // normal end-of-scope cleanup, so owned resources declared inside the body
    // (or in nested blocks within it) would leak. Release every scope stacked
    // strictly above the loop's entry baseline, then restore the stack so that
    // sibling/continuation paths still see those scopes intact. The cleanup IR
    // lands in this branch's own basic blocks, which are mutually exclusive with
    // any continuation path, so nothing is freed twice at runtime.
    if (scopeStack.size() <= baseline) return;
    auto savedStack = scopeStack;
    while (!scopeStack.empty() && scopeStack.size() > baseline) {
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
    var.isOwnedStruct = false; // resolved lazily at cleanup / return-transfer time

    if (var.isVecWithMallocData) {
        VYB_CDBG << "DEBUG: Variable '" << name << "' identified as Vec with malloc'd data" << std::endl;
    }

    scopeStack.back().push_back(var);

    // Legacy per-name refcount bookkeeping applies only to the Vec-with-malloc
    // path that predates the shared control block. Control-block `our<T>` refs
    // (Node and friends) are released via releaseOurControlBlock instead; running
    // this name-keyed path for them would collide across functions that reuse the
    // same binding name (both emitting the same "_refcount" alloca pointer).
    if (ownership == ast::OwnershipKind::OUR && var.isVecWithMallocData) {
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
            // A data-carrying enum (Result<..., our<T>>) owns a
            // strong count on the payload's control block; drop it on scope exit.
            {
                auto astIt = valueTypeMap.find(var.allocaInst);
                if (var.allocaInst && astIt != valueTypeMap.end() && astIt->second &&
                    llvm::isa<llvm::StructType>(var.type) &&
                    enumPayloadHoldsOurRef(astIt->second.get())) {
                    VYB_CDBG << "DEBUG: Releasing owned enum payload for variable: "
                              << var.name << std::endl;
                    reclaimEnumOurPayload(var.allocaInst, astIt->second.get(), /*retain=*/false);
                    return;
                }
            }
            // A struct-typed binding owns its Vec / String fields (and those of
            // any nested owning structs). Reclaim each owned field on scope exit.
            // String, Vec, and closure bindings fall through to their own branches.
            {
                auto astIt = valueTypeMap.find(var.allocaInst);
                if (var.allocaInst && astIt != valueTypeMap.end() && astIt->second) {
                    if (auto* structLLVM = llvm::dyn_cast<llvm::StructType>(var.type)) {
                        if (scopeVarIsOwnedStruct(var)) {
                            VYB_CDBG << "DEBUG: Performing owned-field cleanup for struct variable: "
                                      << var.name << std::endl;
                            reclaimOwnedStructAt(var.allocaInst, astIt->second.get(), structLLVM);
                            return;
                        }
                    }
                }
            }
            // A standalone `my<Struct>` binding stores the pointer to its
            // heap-allocated struct directly. Reclaim any owned fields inside the
            // object, then free the block and null the slot so a later overwrite
            // or return-path cleanup cannot double-reclaim it.
            {
                auto astIt = valueTypeMap.find(var.allocaInst);
                const vyb::ast::TypeNode* myPointee = nullptr;
                if (var.allocaInst && astIt != valueTypeMap.end() && astIt->second) {
                    myPointee = myTypeArg(astIt->second.get());
                }
                if (var.type && var.type->isPointerTy() && myPointee) {
                    llvm::Type* pointeeTy = codegenType(const_cast<vyb::ast::TypeNode*>(myPointee));
                    if (auto* poise = llvm::dyn_cast<llvm::StructType>(pointeeTy)) {
                        VYB_CDBG << "DEBUG: Freeing standalone my<Struct> object for variable: "
                                  << var.name << std::endl;
                        llvm::PointerType* rawPtr = llvm::PointerType::get(*context, 0);
                        llvm::Constant* nullPtr = llvm::ConstantPointerNull::get(rawPtr);
                        llvm::Value* heapPtr = builder->CreateLoad(var.type, var.allocaInst,
                                                                  var.name + "_myobj");
                        llvm::Value* isNull = builder->CreateICmpEQ(heapPtr, nullPtr,
                                                                    var.name + "_myobj_null");
                        llvm::BasicBlock* freeBB =
                            llvm::BasicBlock::Create(*context, var.name + "_myobj_free", currentFunction);
                        llvm::BasicBlock* contBB =
                            llvm::BasicBlock::Create(*context, var.name + "_myobj_cont", currentFunction);
                        builder->CreateCondBr(isNull, contBB, freeBB);
                        builder->SetInsertPoint(freeBB);
                        std::set<std::string> visited;
                        reclaimStructOwnedFieldsAt(heapPtr, myPointee, poise, visited);
                        builder->CreateCall(getOrCreateFreeFunction(), {heapPtr});
                        builder->CreateStore(nullPtr, var.allocaInst);
                        builder->CreateBr(contBB);
                        builder->SetInsertPoint(contBB);
                        return;
                    }
                }
            }
            // Fall through to String / Vec / other MY cleanup.
            // Unique ownership - immediate cleanup
            // A String binding holds one reference to a heap buffer (only when
            // needsCleanup was set for a String-typed binding); release it here.
            // Untracked pointers (literals) and shared buffers whose last holder
            // is a sibling are handled by the registry's reference counting.
            if (isVybStringStructType(var.type)) {
                if (!var.allocaInst || !builder || !currentFunction) {
                    return;
                }
                VYB_CDBG << "DEBUG: Performing String cleanup for variable: " << var.name << std::endl;
                releaseStringAlloca(var.allocaInst);
                return;
            }
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

                // A Vec<String> owns one reference per element (each push/set
                // retained its String). Before the buffer is freed, drop those
                // references so the buffers themselves are reclaimed too.
                auto astIt = valueTypeMap.find(var.allocaInst);
                bool vecHoldsStrings = astIt != valueTypeMap.end() &&
                    typeNodeIsVecOfString(astIt->second.get());

                // A Vec<struct-with-owned-fields> owns one deep copy per element
                // (each push/get/set deep-copied its owned fields). Before the
                // buffer is freed, reclaim each element's owned fields so none of
                // the deep-copied inner buffers leak.
                const vyb::ast::TypeNode* vecElemAst = nullptr;
                llvm::Type* vecElemLlvm = nullptr;
                if (astIt != valueTypeMap.end()) {
                    vecElemAst = vecElementTypeNode(astIt->second.get());
                    if (vecElemAst && isKnownStructTypeNode(vecElemAst) &&
                        structTypeHasOwnedFields(vecElemAst)) {
                        vecElemLlvm = codegenType(const_cast<vyb::ast::TypeNode*>(vecElemAst));
                    } else {
                        vecElemAst = nullptr;
                    }
                }

                // Create null check before freeing
                llvm::Value* isNotNull = builder->CreateICmpNE(dataPtr,
                    llvm::ConstantPointerNull::get(llvm::PointerType::get(*context, 0)),
                    var.name + "_null_check");

                llvm::BasicBlock* freeBlock = llvm::BasicBlock::Create(*context, var.name + "_free_block", currentFunction);
                llvm::BasicBlock* continueBlock = llvm::BasicBlock::Create(*context, var.name + "_continue", currentFunction);

                builder->CreateCondBr(isNotNull, freeBlock, continueBlock);

                // Free block
                builder->SetInsertPoint(freeBlock);
                if (vecHoldsStrings) {
                    llvm::Value* elemCount = builder->CreateExtractValue(vecPtr, 1, var.name + "_elem_count");
                    releaseStringElements(dataPtr, elemCount);
                }
                // Per-element reclaim of deep-copied owned struct fields before
                // freeing the element buffer.
                if (vecElemAst && vecElemLlvm) {
                    llvm::Value* elemCount = builder->CreateExtractValue(vecPtr, 1, var.name + "_elem_count");
                    llvm::Value* elemBytes = llvm::ConstantInt::get(
                        llvm::Type::getInt64Ty(*context),
                        (unsigned)llvm::DataLayout(module.get()).getTypeAllocSize(vecElemLlvm));
                    // Loop i = 0..elemCount: reclaim owned fields at dataPtr + i*elemBytes.
                    llvm::BasicBlock* rbHeader = llvm::BasicBlock::Create(
                        *context, var.name + "_reclaim_header", currentFunction);
                    llvm::BasicBlock* rbBody = llvm::BasicBlock::Create(
                        *context, var.name + "_reclaim_body", currentFunction);
                    llvm::BasicBlock* rbExit = llvm::BasicBlock::Create(
                        *context, var.name + "_reclaim_exit", currentFunction);
                    llvm::Value* zero = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
                    llvm::Value* idxAlloca = builder->CreateAlloca(llvm::Type::getInt64Ty(*context), nullptr, var.name + "_reclaim_idx");
                    builder->CreateStore(zero, idxAlloca);
                    builder->CreateBr(rbHeader);
                    builder->SetInsertPoint(rbHeader);
                    llvm::Value* idx = builder->CreateLoad(llvm::Type::getInt64Ty(*context), idxAlloca);
                    llvm::Value* cmp = builder->CreateICmpULT(idx, elemCount, var.name + "_reclaim_cmp");
                    builder->CreateCondBr(cmp, rbBody, rbExit);
                    builder->SetInsertPoint(rbBody);
                    llvm::Value* elemOff = builder->CreateMul(idx, elemBytes, var.name + "_reclaim_off");
                    llvm::Value* elemPtr = builder->CreateGEP(llvm::Type::getInt8Ty(*context), dataPtr, elemOff, var.name + "_reclaim_elem");
                    std::set<std::string> visited;
                    reclaimStructOwnedFieldsAt(elemPtr, vecElemAst,
                        llvm::cast<llvm::StructType>(vecElemLlvm), visited);
                    llvm::Value* next = builder->CreateAdd(idx, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 1));
                    builder->CreateStore(next, idxAlloca);
                    builder->CreateBr(rbHeader);
                    builder->SetInsertPoint(rbExit);
                }
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
            // Control block-based reference counting. The var's stored value is a
            // pointer to the control block; types without a block (e.g. our<Int>)
            // have nothing to release.
            VYB_CDBG << "DEBUG: Cleaning up OUR ownership for variable: " << var.name << std::endl;
            if (!var.type->isPointerTy()) {
                VYB_CDBG << "DEBUG: Skipping OUR cleanup for non-pointer type: " << var.name << std::endl;
                return;
            }
            llvm::Value* controlBlockPtr = builder->CreateLoad(var.type, var.allocaInst, var.name + "_cb_load");
            const vyb::ast::TypeNode* pointeeAst = nullptr;
            llvm::Type* pointeeLlvm = nullptr;
            auto astIt = valueTypeMap.find(var.allocaInst);
            if (astIt != valueTypeMap.end() && astIt->second) {
                pointeeAst = ourPointeeOf(astIt->second.get());
                if (pointeeAst) {
                    pointeeLlvm = codegenType(const_cast<vyb::ast::TypeNode*>(pointeeAst));
                }
            }
            releaseOurControlBlock(controlBlockPtr, var.name, pointeeAst, pointeeLlvm);
            break;
        }

        case ast::OwnershipKind::THEIR: {
            // Borrowed reference - no cleanup needed
            VYB_CDBG << "DEBUG: No cleanup needed for borrowed reference: " << var.name << std::endl;
            break;
        }
        case ast::OwnershipKind::MILD: {
            // Weak reference - decrement weak_count in the shared control block.
            VYB_CDBG << "DEBUG: Cleaning up MILD ownership for variable: " << var.name << std::endl;
            if (!var.type->isPointerTy()) {
                VYB_CDBG << "DEBUG: Skipping MILD cleanup for non-pointer type: " << var.name << std::endl;
                return;
            }
            llvm::Value* controlBlockPtr = builder->CreateLoad(var.type, var.allocaInst, var.name + "_mild_cb_load");
            releaseMildControlBlock(controlBlockPtr, var.name);
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
        // Atomic so a Vec shared across threads counts references without a race.
        builder->CreateAtomicRMW(
            llvm::AtomicRMWInst::Add, it->second,
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 1),
            llvm::MaybeAlign(), llvm::AtomicOrdering::AcquireRelease);
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

    // Atomic decrement; the RMW returns the pre-decrement value, so a result of 1
    // means this was the last reference (count just became 0).
    llvm::AtomicRMWInst* oldCount = builder->CreateAtomicRMW(
        llvm::AtomicRMWInst::Sub, it->second,
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 1),
        llvm::MaybeAlign(), llvm::AtomicOrdering::AcquireRelease);

    // Check if refcount reached zero
    llvm::Value* isZero = builder->CreateICmpEQ(oldCount, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 1));

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

llvm::Function* LLVMCodegen::getOrCreateVybStringRetainFunction() {
    llvm::Function* retainFunc = module->getFunction("__vyb_string_retain");
    if (!retainFunc) {
        llvm::FunctionType* retainFuncType = llvm::FunctionType::get(
            llvm::PointerType::get(*context, 0),
            {llvm::PointerType::get(*context, 0)},
            false
        );
        retainFunc = llvm::Function::Create(retainFuncType, llvm::Function::ExternalLinkage, "__vyb_string_retain", module.get());
    }
    return retainFunc;
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

namespace {
const vyb::ast::TypeName* asTypeNameNode(const vyb::ast::TypeNode* tn) {
    return dynamic_cast<const vyb::ast::TypeName*>(tn);
}
vyb::ast::TypeNodePtr substituteOwnedFieldType(const vyb::ast::TypeNode* tn,
                                               const std::map<std::string, vyb::ast::TypeNode*>& pm) {
    if (!tn) return nullptr;
    if (auto* nn = dynamic_cast<const vyb::ast::TypeName*>(tn)) {
        if (nn->identifier) {
            auto it = pm.find(nn->identifier->name);
            if (it != pm.end()) return it->second->clone();
            if (!nn->genericArgs.empty()) {
                std::vector<vyb::ast::TypeNodePtr> args;
                for (const auto& a : nn->genericArgs) {
                    args.push_back(substituteOwnedFieldType(a.get(), pm));
                }
                return std::make_unique<vyb::ast::TypeName>(
                    nn->loc,
                    std::make_unique<vyb::ast::Identifier>(nn->identifier->loc, nn->identifier->name),
                    std::move(args));
            }
        }
    }
    return tn->clone();
}
std::string ownedFieldTypeBase(const vyb::ast::TypeNode* tn) {
    if (!tn) return "";
    if (auto* nn = dynamic_cast<const vyb::ast::TypeName*>(tn)) {
        return nn->identifier ? nn->identifier->name : "";
    }
    return "";
}
bool isOwnedFieldString(const vyb::ast::TypeNode* tn) {
    std::string b = ownedFieldTypeBase(tn);
    return b == "String" || b == "string";
}
bool isRefTypeNode(const vyb::ast::TypeNode* tn, const std::string& kind) {
    return ownedFieldTypeBase(tn) == kind;
}
// For a `my<T>` type node, return the pointee type node `T` (or nullptr if the
// node is not a `my<T>` TypeName). A `my` field over a struct type is heap-backed
// (the generated code stores a `T*` in the field), so it must be reclaimed.
const vyb::ast::TypeNode* myTypeArg(const vyb::ast::TypeNode* tn) {
    if (ownedFieldTypeBase(tn) != "my") return nullptr;
    if (const auto* nn = dynamic_cast<const vyb::ast::TypeName*>(tn)) {
        if (nn->genericArgs.size() == 1) return nn->genericArgs[0].get();
    }
    return nullptr;
}
bool isVecTypeNode(const vyb::ast::TypeNode* tn) {
    if (dynamic_cast<const vyb::ast::VecType*>(tn)) return true;
    return ownedFieldTypeBase(tn) == "Vec";
}
}

bool LLVMCodegen::collectStructConcreteFieldTypes(
        const vyb::ast::TypeNode* astType, std::vector<vyb::ast::TypeNodePtr>& out) const {
    out.clear();
    if (!astType) return false;
    if (auto* st = dynamic_cast<const vyb::ast::StructType*>(astType)) {
        for (const auto& f : st->fields) {
            out.push_back(f.type ? f.type->clone() : nullptr);
        }
        return true;
    }
    if (auto* nn = dynamic_cast<const vyb::ast::TypeName*>(astType)) {
        if (!nn->identifier) return false;
        auto it = genericStructTemplates.find(nn->identifier->name);
        if (it == genericStructTemplates.end()) return false;
        vyb::ast::StructDeclaration* decl = it->second;
        std::map<std::string, vyb::ast::TypeNode*> pm;
        for (size_t i = 0; i < decl->genericParams.size() && i < nn->genericArgs.size(); ++i) {
            if (decl->genericParams[i] && decl->genericParams[i]->name) {
                pm[decl->genericParams[i]->name->name] = nn->genericArgs[i].get();
            }
        }
        for (const auto& f : decl->fields) {
            out.push_back(substituteOwnedFieldType(f && f->typeNode ? f->typeNode.get() : nullptr, pm));
        }
        return true;
    }
    return false;
}

bool LLVMCodegen::isVecOfStringTypeNode(const vyb::ast::TypeNode* tn) const {
    if (!isVecTypeNode(tn)) return false;
    const vyb::ast::TypeNode* elem = nullptr;
    if (auto* vt = dynamic_cast<const vyb::ast::VecType*>(tn)) {
        elem = vt->elementType.get();
    } else if (auto* nn = dynamic_cast<const vyb::ast::TypeName*>(tn)) {
        if (!nn->genericArgs.empty()) elem = nn->genericArgs[0].get();
    }
    return elem && isOwnedFieldString(elem);
}

bool LLVMCodegen::isKnownStructTypeNode(const vyb::ast::TypeNode* tn) const {
    std::string base = ownedFieldTypeBase(tn);
    if (base.empty() || base == "Vec" || isOwnedFieldString(tn)) return false;
    return genericStructTemplates.count(base) != 0;
}

bool LLVMCodegen::isMyOwnedStructTypeNode(const vyb::ast::TypeNode* tn) const {
    const vyb::ast::TypeNode* arg = myTypeArg(tn);
    return arg != nullptr && isKnownStructTypeNode(arg);
}

const vyb::ast::TypeNode* LLVMCodegen::myPointeeOf(const vyb::ast::TypeNode* tn) const {
    return myTypeArg(tn);
}

bool LLVMCodegen::isOurRefType(const vyb::ast::TypeNode* tn) const {
    return isRefTypeNode(tn, "our");
}

bool LLVMCodegen::isMildRefType(const vyb::ast::TypeNode* tn) const {
    return isRefTypeNode(tn, "mild");
}

const vyb::ast::TypeNode* LLVMCodegen::refPointeeOf(const vyb::ast::TypeNode* tn,
                                                    const std::string& kind) const {
    if (ownedFieldTypeBase(tn) != kind) return nullptr;
    if (const auto* nn = dynamic_cast<const vyb::ast::TypeName*>(tn)) {
        if (nn->genericArgs.size() == 1) return nn->genericArgs[0].get();
    }
    return nullptr;
}

const vyb::ast::TypeNode* LLVMCodegen::ourPointeeOf(const vyb::ast::TypeNode* tn) const {
    return refPointeeOf(tn, "our");
}

const vyb::ast::TypeNode* LLVMCodegen::mildPointeeOf(const vyb::ast::TypeNode* tn) const {
    return refPointeeOf(tn, "mild");
}

bool LLVMCodegen::structTypeHasOwnedFields(const vyb::ast::TypeNode* astType) const {
    std::vector<vyb::ast::TypeNodePtr> fields;
    if (!collectStructConcreteFieldTypes(astType, fields)) return false;
    for (const auto& f : fields) {
        if (!f) continue;
        if (isVecTypeNode(f.get()) || isOwnedFieldString(f.get())) return true;
        // A `my<Struct>` field owns a heap allocation; reclaim it on scope exit so
        // the pointed-to struct (and any of its owned fields) is freed once the
        // owning binding drops.
        if (isMyOwnedStructTypeNode(f.get())) return true;
        // our/mild ref fields claim a refcount on a shared control block; releasing
        // it on scope exit requires cleanup (so the control block is freed once the
        // last owner is dropped).
        if (isRefTypeNode(f.get(), "mild") || isRefTypeNode(f.get(), "our")) return true;
        if (isKnownStructTypeNode(f.get()) && structTypeHasOwnedFields(f.get())) return true;
    }
    return false;
}

bool LLVMCodegen::scopeVarIsOwnedStruct(const ScopeVariable& var) const {
    if (var.ownership != ast::OwnershipKind::MY || !llvm::isa<llvm::StructType>(var.type)) {
        return false;
    }
    auto astIt = valueTypeMap.find(var.allocaInst);
    if (astIt == valueTypeMap.end() || !astIt->second) return false;
    return structTypeHasOwnedFields(astIt->second.get());
}

void LLVMCodegen::reclaimOwnedStructAt(llvm::Value* structPtr,
                                       const vyb::ast::TypeNode* astType,
                                       llvm::StructType* llvmTy) {
    std::set<std::string> visited;
    reclaimStructOwnedFieldsAt(structPtr, astType, llvmTy, visited);
}

void LLVMCodegen::reclaimStructOwnedFieldsAt(llvm::Value* structPtr,
                                             const vyb::ast::TypeNode* astType,
                                             llvm::StructType* llvmTy,
                                             std::set<std::string>& visited) {
    if (!structPtr || !astType || !llvmTy || !builder || !currentFunction) return;
    // Guard against structurally self-referential `my<Struct>` graphs (e.g. a
    // linked TreeNode holding a `my<TreeNode>`). Inlining the reclaim of an
    // arbitrarily deep linked structure would recurse forever at codegen time,
    // so only descend through a given struct type once per reclaim path; a
    // repeated type is still freed, just without diving into its own owned fields.
    std::string selfBase = ownedFieldTypeBase(astType);
    if (!selfBase.empty() && visited.count(selfBase)) return;
    if (!selfBase.empty()) visited.insert(selfBase);
    std::vector<vyb::ast::TypeNodePtr> fields;
    if (collectStructConcreteFieldTypes(astType, fields)) {
    size_t n = std::min(fields.size(), (size_t)llvmTy->getNumElements());
    llvm::PointerType* rawPtr = llvm::PointerType::get(*context, 0);
    llvm::Constant* zero = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
    llvm::Constant* nullPtr = llvm::ConstantPointerNull::get(rawPtr);
    for (size_t i = 0; i < n; ++i) {
        const vyb::ast::TypeNode* f = fields[i].get();
        llvm::Type* fLLVM = llvmTy->getElementType(i);
        if (!f || !fLLVM) continue;
        llvm::Value* fptr = builder->CreateStructGEP(llvmTy, structPtr, i, "reclaim.field");
        if (isVecTypeNode(f)) {
            auto* vt = llvm::dyn_cast<llvm::StructType>(fLLVM);
            if (!vt || !isVecStructType(vt)) continue;
            llvm::Value* sl = builder->CreateLoad(vt, fptr, "reclaim.vec");
            llvm::Value* data = builder->CreateExtractValue(sl, 0, "reclaim.data");
            if (isVecOfStringTypeNode(f)) {
                llvm::Value* sz = builder->CreateExtractValue(sl, 1, "reclaim.size");
                releaseStringElements(data, sz);
            }
            llvm::Value* isNull = builder->CreateICmpEQ(
                data, llvm::ConstantPointerNull::get(rawPtr), "reclaim.isnull");
            llvm::BasicBlock* freeBB = llvm::BasicBlock::Create(*context, "reclaim.vec.free", currentFunction);
            llvm::BasicBlock* contBB = llvm::BasicBlock::Create(*context, "reclaim.vec.cont", currentFunction);
            builder->CreateCondBr(isNull, contBB, freeBB);
            builder->SetInsertPoint(freeBB);
            builder->CreateCall(getOrCreateFreeFunction(), {data});
            builder->CreateBr(contBB);
            builder->SetInsertPoint(contBB);
            llvm::Value* clr = llvm::UndefValue::get(vt);
            clr = builder->CreateInsertValue(clr, nullPtr, 0);
            clr = builder->CreateInsertValue(clr, zero, 1);
            clr = builder->CreateInsertValue(clr, zero, 2);
            builder->CreateStore(clr, fptr);
        } else if (isOwnedFieldString(f)) {
            auto* st2 = llvm::dyn_cast<llvm::StructType>(fLLVM);
            if (!st2 || !isVybStringStructType(st2)) continue;
            llvm::Value* strv = builder->CreateLoad(st2, fptr, "reclaim.str");
            releaseStringValue(strv);
            llvm::Value* clr = llvm::UndefValue::get(st2);
            clr = builder->CreateInsertValue(clr, nullPtr, 0);
            clr = builder->CreateInsertValue(clr, zero, 1);
            builder->CreateStore(clr, fptr);
        } else if (const vyb::ast::TypeNode* myArg = myTypeArg(f)) {
            // A `my<Struct>` field owns a heap allocation. Release any owned fields
            // inside the pointed-to struct, then free the heap block and null the
            // slot so an overwrite/scope-exit double-reclaim is safe.
            if (fLLVM->isPointerTy() && isMyOwnedStructTypeNode(f)) {
                llvm::Type* pointeeTy = codegenType(const_cast<vyb::ast::TypeNode*>(myArg));
                if (auto* pois = llvm::dyn_cast<llvm::StructType>(pointeeTy)) {
                    llvm::Value* heapPtr = builder->CreateLoad(rawPtr, fptr, "reclaim.myptr");
                    llvm::Value* isNull =
                        builder->CreateICmpEQ(heapPtr, nullPtr, "reclaim.mynull");
                    llvm::BasicBlock* freeBB =
                        llvm::BasicBlock::Create(*context, "reclaim.my.free", currentFunction);
                    llvm::BasicBlock* contBB =
                        llvm::BasicBlock::Create(*context, "reclaim.my.cont", currentFunction);
                    builder->CreateCondBr(isNull, contBB, freeBB);
                    builder->SetInsertPoint(freeBB);
                    reclaimStructOwnedFieldsAt(heapPtr, myArg, pois, visited);
                    builder->CreateCall(getOrCreateFreeFunction(), {heapPtr});
                    builder->CreateBr(contBB);
                    builder->SetInsertPoint(contBB);
                    builder->CreateStore(nullPtr, fptr);
                }
            }
        } else if (isRefTypeNode(f, "mild") || isRefTypeNode(f, "our")) {
            // A ref field claims a count on a shared control block. Release it on
            // scope exit so the control block is freed once the last owner drops.
            if (!fLLVM->isPointerTy()) continue;
            llvm::Value* cb = builder->CreateLoad(rawPtr, fptr, "reclaim.refcb");
            if (isRefTypeNode(f, "mild")) {
                releaseMildControlBlock(cb, "reclaim.mild");
            } else {
                const vyb::ast::TypeNode* pointeeAst = ourPointeeOf(f);
                llvm::Type* pointeeLlvm = pointeeAst
                    ? codegenType(const_cast<vyb::ast::TypeNode*>(pointeeAst)) : nullptr;
                releaseOurControlBlock(cb, "reclaim.our", pointeeAst, pointeeLlvm);
            }
            builder->CreateStore(nullPtr, fptr);
        } else if (isKnownStructTypeNode(f)) {
            if (auto* st3 = llvm::dyn_cast<llvm::StructType>(fLLVM)) {
                reclaimStructOwnedFieldsAt(fptr, f, st3, visited);
            }
        }
    }
    }
    if (!selfBase.empty()) visited.erase(selfBase);
}


// Bump the strong count of an `our<T>` control block so a newly-created shared
// reference owns one more strong ref. A storage location that will release on
// scope exit must retain on copy; a fresh transfer (`our(...)`, `grab()`, or a
// function returning `our<T>`) already hands over its own strong ref and is not
// retained. `controlBlockPtr` may be null.
void LLVMCodegen::retainOurControlBlock(llvm::Value* controlBlockPtr, const std::string& tag) {
    if (!controlBlockPtr || !builder || !currentFunction) return;
    llvm::PointerType* rawPtr = llvm::PointerType::get(*context, 0);
    llvm::Constant* nullPtr = llvm::ConstantPointerNull::get(rawPtr);

    llvm::Value* isNull = builder->CreateICmpEQ(controlBlockPtr, nullPtr, tag + "_our_retain_null");
    llvm::BasicBlock* doRetain = llvm::BasicBlock::Create(*context, tag + "_our_do_retain", currentFunction);
    llvm::BasicBlock* continueBlock = llvm::BasicBlock::Create(*context, tag + "_our_retain_done", currentFunction);
    builder->CreateCondBr(isNull, continueBlock, doRetain);

    builder->SetInsertPoint(doRetain);

    std::vector<llvm::Type*> cbFields = {
        llvm::Type::getInt32Ty(*context),  // strong_count
        llvm::Type::getInt32Ty(*context),  // weak_count
        llvm::Type::getInt8Ty(*context),   // object_freed
        rawPtr                             // object_ptr
    };
    llvm::StructType* controlBlockType = llvm::StructType::get(*context, cbFields, /*isPacked=*/false);

    llvm::Value* strongCountPtr = builder->CreateStructGEP(controlBlockType, controlBlockPtr, 0, tag + "_our_strong_count_ptr");
    builder->CreateAtomicRMW(
        llvm::AtomicRMWInst::Add, strongCountPtr,
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 1),
        llvm::MaybeAlign(), llvm::AtomicOrdering::AcquireRelease);
    builder->CreateBr(continueBlock);

    builder->SetInsertPoint(continueBlock);
}

// Decrement the strong count of an `our<T>` control block, freeing the shared
// object when the count reaches zero, and freeing the control block itself once
// both strong and weak counts are zero. `controlBlockPtr` must be the (possibly
// null) pointer stored by the binding/field being released.
void LLVMCodegen::releaseOurControlBlock(llvm::Value* controlBlockPtr, const std::string& tag,
                                         const vyb::ast::TypeNode* pointeeAst,
                                         llvm::Type* pointeeLlvm) {
    if (!controlBlockPtr || !builder || !currentFunction) return;
    llvm::PointerType* rawPtr = llvm::PointerType::get(*context, 0);
    llvm::Constant* nullPtr = llvm::ConstantPointerNull::get(rawPtr);

    llvm::Value* isNull = builder->CreateICmpEQ(controlBlockPtr, nullPtr, tag + "_cb_null");
    llvm::BasicBlock* cleanupBlock = llvm::BasicBlock::Create(*context, tag + "_our_cleanup", currentFunction);
    llvm::BasicBlock* continueBlock = llvm::BasicBlock::Create(*context, tag + "_our_continue", currentFunction);
    builder->CreateCondBr(isNull, continueBlock, cleanupBlock);

    builder->SetInsertPoint(cleanupBlock);

    std::vector<llvm::Type*> cbFields = {
        llvm::Type::getInt32Ty(*context),  // strong_count
        llvm::Type::getInt32Ty(*context),  // weak_count
        llvm::Type::getInt8Ty(*context),   // object_freed
        rawPtr                             // object_ptr
    };
    llvm::StructType* controlBlockType = llvm::StructType::get(*context, cbFields, /*isPacked=*/false);

    llvm::Value* strongCountPtr = builder->CreateStructGEP(controlBlockType, controlBlockPtr, 0, tag + "_strong_count_ptr");
    llvm::AtomicRMWInst* decremented = builder->CreateAtomicRMW(
        llvm::AtomicRMWInst::Sub, strongCountPtr,
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 1),
        llvm::MaybeAlign(), llvm::AtomicOrdering::AcquireRelease);
    llvm::Value* wasOne = builder->CreateICmpEQ(decremented,
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 1), tag + "_strong_was_one");

    llvm::BasicBlock* freeObjectBlock = llvm::BasicBlock::Create(*context, tag + "_free_object", currentFunction);
    llvm::BasicBlock* checkCBFreeBlock = llvm::BasicBlock::Create(*context, tag + "_check_cb_free", currentFunction);
    builder->CreateCondBr(wasOne, freeObjectBlock, checkCBFreeBlock);

    builder->SetInsertPoint(freeObjectBlock);
    llvm::Value* objectPtrFieldPtr = builder->CreateStructGEP(controlBlockType, controlBlockPtr, 3, tag + "_obj_ptr_field_ptr");
    llvm::Value* objectPtr = builder->CreateLoad(rawPtr, objectPtrFieldPtr, tag + "_obj_ptr");
    // A struct payload owns its fields (inner our/mild refs, Vec storage, String
    // buffers, my blocks). Reclaim them before the raw payload is freed, or those
    // nested resources leak. Only reached when the strong count dropped to zero,
    // so the object is genuinely being destroyed (not a shared, still-live one).
    if (pointeeAst && pointeeLlvm) {
        if (auto* poise = llvm::dyn_cast<llvm::StructType>(pointeeLlvm)) {
            std::set<std::string> visited;
            reclaimStructOwnedFieldsAt(objectPtr, pointeeAst, poise, visited);
        }
    }
    builder->CreateCall(getOrCreateFreeFunction(), {objectPtr});
    llvm::Value* objectFreedPtr = builder->CreateStructGEP(controlBlockType, controlBlockPtr, 2, tag + "_obj_freed_ptr");
    builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt8Ty(*context), 1), objectFreedPtr);
    builder->CreateBr(checkCBFreeBlock);

    builder->SetInsertPoint(checkCBFreeBlock);
    llvm::Value* weakCountPtr = builder->CreateStructGEP(controlBlockType, controlBlockPtr, 1, tag + "_weak_count_ptr");
    llvm::Value* weakCount = builder->CreateLoad(llvm::Type::getInt32Ty(*context), weakCountPtr, tag + "_weak_count");
    llvm::Value* strongCount = builder->CreateLoad(llvm::Type::getInt32Ty(*context), strongCountPtr, tag + "_strong_count");
    llvm::Value* strongIsZero = builder->CreateICmpEQ(strongCount,
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0), tag + "_strong_is_zero");
    llvm::Value* weakIsZero = builder->CreateICmpEQ(weakCount,
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0), tag + "_weak_is_zero");
    llvm::Value* bothZero = builder->CreateAnd(strongIsZero, weakIsZero, tag + "_both_zero");

    llvm::BasicBlock* freeCBBlock = llvm::BasicBlock::Create(*context, tag + "_free_cb", currentFunction);
    llvm::BasicBlock* doneCBBlock = llvm::BasicBlock::Create(*context, tag + "_cb_done", currentFunction);
    builder->CreateCondBr(bothZero, freeCBBlock, doneCBBlock);

    builder->SetInsertPoint(freeCBBlock);
    builder->CreateCall(getOrCreateFreeFunction(), {controlBlockPtr});
    builder->CreateBr(doneCBBlock);

    builder->SetInsertPoint(doneCBBlock);
    builder->CreateBr(continueBlock);

    builder->SetInsertPoint(continueBlock);
}

// Decrement the weak count of an `our<mild>` control block, freeing the control
// block itself once both strong and weak counts are zero. `controlBlockPtr` must
// be the (possibly null) pointer stored by the binding/field being released.
void LLVMCodegen::releaseMildControlBlock(llvm::Value* controlBlockPtr, const std::string& tag) {
    if (!controlBlockPtr || !builder || !currentFunction) return;
    llvm::PointerType* rawPtr = llvm::PointerType::get(*context, 0);
    llvm::Constant* nullPtr = llvm::ConstantPointerNull::get(rawPtr);

    llvm::Value* isNull = builder->CreateICmpEQ(controlBlockPtr, nullPtr, tag + "_mild_cb_null");
    llvm::BasicBlock* cleanupBlock = llvm::BasicBlock::Create(*context, tag + "_mild_cleanup", currentFunction);
    llvm::BasicBlock* continueBlock = llvm::BasicBlock::Create(*context, tag + "_mild_continue", currentFunction);
    builder->CreateCondBr(isNull, continueBlock, cleanupBlock);

    builder->SetInsertPoint(cleanupBlock);

    std::vector<llvm::Type*> cbFields = {
        llvm::Type::getInt32Ty(*context),  // strong_count
        llvm::Type::getInt32Ty(*context),  // weak_count
        llvm::Type::getInt8Ty(*context),   // object_freed
        rawPtr                             // object_ptr
    };
    llvm::StructType* controlBlockType = llvm::StructType::get(*context, cbFields, /*isPacked=*/false);

    llvm::Value* weakCountPtr = builder->CreateStructGEP(controlBlockType, controlBlockPtr, 1, tag + "_mild_weak_count_ptr");
    llvm::AtomicRMWInst* decremented = builder->CreateAtomicRMW(
        llvm::AtomicRMWInst::Sub, weakCountPtr,
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 1),
        llvm::MaybeAlign(), llvm::AtomicOrdering::AcquireRelease);
    llvm::Value* wasOne = builder->CreateICmpEQ(decremented,
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 1), tag + "_mild_weak_was_one");

    llvm::BasicBlock* checkCBFreeBlock = llvm::BasicBlock::Create(*context, tag + "_mild_check_cb_free", currentFunction);
    llvm::BasicBlock* doneMildBlock = llvm::BasicBlock::Create(*context, tag + "_mild_done", currentFunction);
    builder->CreateCondBr(wasOne, checkCBFreeBlock, doneMildBlock);

    builder->SetInsertPoint(checkCBFreeBlock);
    llvm::Value* strongCountPtr = builder->CreateStructGEP(controlBlockType, controlBlockPtr, 0, tag + "_mild_strong_count_ptr");
    llvm::Value* strongCount = builder->CreateLoad(llvm::Type::getInt32Ty(*context), strongCountPtr, tag + "_mild_strong_count");
    llvm::Value* strongIsZero = builder->CreateICmpEQ(strongCount,
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0), tag + "_mild_strong_is_zero");

    llvm::BasicBlock* freeCBBlock = llvm::BasicBlock::Create(*context, tag + "_mild_free_cb", currentFunction);
    llvm::BasicBlock* skipFreeCBBlock = llvm::BasicBlock::Create(*context, tag + "_mild_skip_free_cb", currentFunction);
    builder->CreateCondBr(strongIsZero, freeCBBlock, skipFreeCBBlock);

    builder->SetInsertPoint(freeCBBlock);
    builder->CreateCall(getOrCreateFreeFunction(), {controlBlockPtr});
    builder->CreateBr(skipFreeCBBlock);

    builder->SetInsertPoint(skipFreeCBBlock);
    builder->CreateBr(doneMildBlock);

    builder->SetInsertPoint(doneMildBlock);
    builder->CreateBr(continueBlock);

    builder->SetInsertPoint(continueBlock);
}

// Bump the weak count of a `mild<T>` control block so a newly-created weak
// reference (e.g. a shallow struct-field copy stored in an async env) owns one
// more weak ref. A storage location that will release on scope exit must retain
// on copy, mirroring releaseMildControlBlock. `controlBlockPtr` may be null.
void LLVMCodegen::retainMildControlBlock(llvm::Value* controlBlockPtr, const std::string& tag) {
    if (!controlBlockPtr || !builder || !currentFunction) return;
    llvm::PointerType* rawPtr = llvm::PointerType::get(*context, 0);
    llvm::Constant* nullPtr = llvm::ConstantPointerNull::get(rawPtr);

    llvm::Value* isNull = builder->CreateICmpEQ(controlBlockPtr, nullPtr, tag + "_mild_retain_null");
    llvm::BasicBlock* doRetain = llvm::BasicBlock::Create(*context, tag + "_mild_do_retain", currentFunction);
    llvm::BasicBlock* continueBlock = llvm::BasicBlock::Create(*context, tag + "_mild_retain_done", currentFunction);
    builder->CreateCondBr(isNull, continueBlock, doRetain);

    builder->SetInsertPoint(doRetain);
    std::vector<llvm::Type*> cbFields = {
        llvm::Type::getInt32Ty(*context),  // strong_count
        llvm::Type::getInt32Ty(*context),  // weak_count
        llvm::Type::getInt8Ty(*context),   // object_freed
        rawPtr                             // object_ptr
    };
    llvm::StructType* controlBlockType = llvm::StructType::get(*context, cbFields, /*isPacked=*/false);
    llvm::Value* weakCountPtr = builder->CreateStructGEP(controlBlockType, controlBlockPtr, 1, tag + "_mild_weak_count_ptr");
    builder->CreateAtomicRMW(
        llvm::AtomicRMWInst::Add, weakCountPtr,
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 1),
        llvm::MaybeAlign(), llvm::AtomicOrdering::AcquireRelease);
    builder->CreateBr(continueBlock);

    builder->SetInsertPoint(continueBlock);
}

// Does the data-carrying built-in enum (Result<T, E>) carry an `our<T>` reference
// in one of its payloads? Only `our` refs need retain/release
// bookkeeping inside an enum: `mild` is a weak count (a copy must not re-count),
// and my/Vec/String payload ownership is handled by their own storage paths.
bool LLVMCodegen::enumPayloadHoldsOurRef(const vyb::ast::TypeNode* astType) const {
    auto* tn = dynamic_cast<const vyb::ast::TypeName*>(astType);
    if (!tn || !tn->identifier) return false;
    const std::string& base = tn->identifier->name;
    const bool isResult = (base == "Result" || base == "core::result::Result");
    if (!isResult) return false;
    for (const auto& pa : tn->genericArgs) {
        if (pa && isOurRefType(pa.get())) return true;
    }
    return false;
}

// Retain (retain=true) or release (retain=false) the `our<T>` strong reference
// carried by an enum binding above. The runtime tag decides which variant (if
// any) holds the payload, so this emits a per-owned-variant guarded call and
// otherwise does nothing. `enumPtr` is the address of the enum struct value.
void LLVMCodegen::reclaimEnumOurPayload(llvm::Value* enumPtr, const vyb::ast::TypeNode* astType,
                                        bool retain) {
    if (!enumPtr || !astType || !builder || !currentFunction) return;
    auto* tn = dynamic_cast<const vyb::ast::TypeName*>(astType);
    if (!tn || !tn->identifier) return;
    const std::string& base = tn->identifier->name;
    const bool isResult = (base == "Result" || base == "core::result::Result");
    if (!isResult) return;

    const TaggedEnumInfo* info = findTaggedEnum(const_cast<vyb::ast::TypeNode*>(astType));
    if (!info) return;
    llvm::StructType* enumTy = info->llvmType;
    if (!enumTy) return;
    llvm::Value* enumVal = builder->CreateLoad(enumTy, enumPtr, "reclaim.enum");

    struct OwnedVariant { const char* variant; unsigned argIdx; };
    std::vector<OwnedVariant> variants;
    variants.push_back({"Ok", 0});
    variants.push_back({"Err", 1});

    for (const auto& v : variants) {
        if (v.argIdx >= tn->genericArgs.size() || !tn->genericArgs[v.argIdx]) continue;
        if (!isOurRefType(tn->genericArgs[v.argIdx].get())) continue;
        auto tagIt = info->variantTags.find(v.variant);
        if (tagIt == info->variantTags.end()) continue;
        auto pIt = info->variantPayloadTypes.find(v.variant);
        if (pIt == info->variantPayloadTypes.end() || !pIt->second) continue;

        llvm::Value* tag = builder->CreateExtractValue(enumVal, 0, "reclaim.enum.tag");
        llvm::Value* isThis = builder->CreateICmpEQ(
            tag,
            llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context),
                                   static_cast<int64_t>(tagIt->second), true),
            "reclaim.enum.is");
        llvm::BasicBlock* doBB = llvm::BasicBlock::Create(*context, "reclaim.enum.owned", currentFunction);
        llvm::BasicBlock* nextBB = llvm::BasicBlock::Create(*context, "reclaim.enum.next", currentFunction);
        builder->CreateCondBr(isThis, doBB, nextBB);
        builder->SetInsertPoint(doBB);

        llvm::Value* cb = extractEnumVariantField(enumVal, pIt->second, 0);
        if (cb) {
            if (retain) retainOurControlBlock(cb, "reclaim.enum.retain");
            else {
                const vyb::ast::TypeNode* pointeeAst = ourPointeeOf(tn->genericArgs[v.argIdx].get());
                llvm::Type* pointeeLlvm = pointeeAst
                    ? codegenType(const_cast<vyb::ast::TypeNode*>(pointeeAst)) : nullptr;
                releaseOurControlBlock(cb, "reclaim.enum.release", pointeeAst, pointeeLlvm);
            }
        }
        builder->CreateBr(nextBB);
        builder->SetInsertPoint(nextBB);
    }
}

// Does an enum-typed initializer hand over its payload's strong ref (a "fresh
// transfer" that needs no further retain on stow), or is it a borrowed copy that
// must be retained? `grab()` and function calls returning the enum transfer;
// `Ok(...)` with a fresh `our(...)`/`.grab()` payload also transfers. A bare
// `Ok(owner)` (borrowing an existing `our`) is a copy and must be retained.
bool LLVMCodegen::enumInitIsOurTransfer(vyb::ast::Expression* init) {
    if (!init) return false;
    auto* call = dynamic_cast<vyb::ast::CallExpression*>(init);
    if (!call) return false;
    if (auto* id = dynamic_cast<vyb::ast::Identifier*>(call->callee.get())) {
        if (id->name == "Ok" || id->name == "Err") {
            if (call->arguments.empty()) return true;  // unit variant
            return exprIsOurTransfer(call->arguments[0].get());
        }
    }
    // Any other call (grab(), a function returning the enum, ...) transfers.
    return true;
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

    // Total bytes to allocate = cap * elemSize. The deep-copied Vec advertises
    // the original capacity (vecCap, which may exceed vecSize). Allocating only
    // size*elemSize here left the new buffer smaller than its advertised
    // capacity, so a later push/push-path growth wrote past the allocation
    // ("free(): invalid next size"), corrupting the heap. We copy only vecSize
    // elements but must carve space for vecCap of them.
    llvm::Value* allocBytes =
        builder->CreateMul(vecCap, elemSizeVal, "vdc.alloc_bytes");
    llvm::Value* totalBytes = builder->CreateMul(vecSize, elemSizeVal, "vdc.bytes");

    // Malloc a new buffer sized for the advertised capacity
    llvm::Function* mallocFunc = getOrCreateMallocFunction();
    llvm::Value* newDataPtr = builder->CreateCall(mallocFunc, {allocBytes}, "vdc.new_ptr");

    // If size > 0, memcpy the data; otherwise leave newDataPtr (may be garbage but won't be accessed)
    llvm::BasicBlock* copyBB  = llvm::BasicBlock::Create(*context, "vdc.copy", currentFunc);
    llvm::BasicBlock* doneBB  = llvm::BasicBlock::Create(*context, "vdc.done", currentFunc);
    llvm::Value* hasData = builder->CreateICmpSGT(
        vecSize, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0), "vdc.has_data");
    builder->CreateCondBr(hasData, copyBB, doneBB);

    builder->SetInsertPoint(copyBB);
    llvm::Function* memcpyFunc = getOrCreateMemcpyFunction();
    builder->CreateCall(memcpyFunc, {newDataPtr, srcDataPtr, totalBytes});
    // String elements are shallow { ptr, len } values whose buffers are reference
    // counted. The deep-copied Vec is an independent holder, so it must retain
    // each element it now references (its own cleanup will release them).
    if (elemType && isVybStringStructType(elemType)) {
        retainStringElements(newDataPtr, vecSize);
    }
    builder->CreateBr(doneBB);

    builder->SetInsertPoint(doneBB);

    // Build a new Vec struct with the cloned data pointer
    llvm::Value* newVecStruct = llvm::UndefValue::get(vecStructType);
    newVecStruct = builder->CreateInsertValue(newVecStruct, newDataPtr, 0, "vdc.new_vec0");
    newVecStruct = builder->CreateInsertValue(newVecStruct, vecSize,    1, "vdc.new_vec1");
    newVecStruct = builder->CreateInsertValue(newVecStruct, vecCap,     2, "vdc.new_vec2");
    return newVecStruct;
}

// Deep-copy a struct value into an independent owned copy, mirroring the field
// categories `reclaimStructOwnedFieldsAt` releases so that reclaiming the result
// on scope exit exactly balances this copy:
//   - String fields   -> retain the buffer (+1)
//   - Vec<T> fields   -> clone the data buffer (retaining String elements)
//   - `my<Struct>`    -> malloc a fresh block and deep-copy the pointee
//   - `our`/`mild`    -> retain the shared control block (+1 strong/weak)
//   - nested structs  -> recurse
//   - scalars         -> copied by value
// Used to snapshot struct-typed async params so the task env owns an independent
// copy that survives the caller's frame. `structValue` must be a value of the
// same type as `llvmTy` (the env field layout's struct type).
llvm::Value* LLVMCodegen::generateStructDeepCopy(llvm::Value* structValue,
                                                 const vyb::ast::TypeNode* astType,
                                                 llvm::StructType* llvmTy) {
    if (!structValue || !astType || !llvmTy || !builder || !currentFunction) return structValue;
    llvm::StructType* st = llvm::dyn_cast<llvm::StructType>(structValue->getType());
    if (!st) return structValue;
    llvm::PointerType* rawPtr = llvm::PointerType::get(*context, 0);
    llvm::Constant* nullPtr = llvm::ConstantPointerNull::get(rawPtr);

    std::vector<vyb::ast::TypeNodePtr> fields;
    if (!collectStructConcreteFieldTypes(astType, fields)) return structValue;

    const size_t n = std::min(fields.size(), (size_t)st->getNumElements());
    llvm::Value* outVal = llvm::UndefValue::get(st);
    for (size_t i = 0; i < n; ++i) {
        const vyb::ast::TypeNode* f = fields[i].get();
        if (!f) continue;
        llvm::Type* fLLVM = st->getElementType(i);
        if (!fLLVM) continue;
        llvm::Value* fv = builder->CreateExtractValue(structValue, i, "sdc.field");

        if (isVecTypeNode(f)) {
            auto* vt = llvm::dyn_cast<llvm::StructType>(fLLVM);
            if (vt && isVecStructType(vt)) {
                llvm::Type* elemType = nullptr;
                if (const auto* vnode = dynamic_cast<const vyb::ast::VecType*>(f))
                    elemType = vnode->elementType
                        ? codegenType(const_cast<vyb::ast::TypeNode*>(vnode->elementType.get())) : nullptr;
                else if (const auto* nn = dynamic_cast<const vyb::ast::TypeName*>(f))
                    if (!nn->genericArgs.empty())
                        elemType = codegenType(const_cast<vyb::ast::TypeNode*>(nn->genericArgs[0].get()));
                if (elemType) {
                    if (llvm::Value* dc = generateVecDeepCopy(fv, elemType, vt)) {
                        outVal = builder->CreateInsertValue(outVal, dc, i, "sdc.vec");
                        continue;
                    }
                }
            }
        } else if (isOwnedFieldString(f)) {
            auto* st2 = llvm::dyn_cast<llvm::StructType>(fLLVM);
            if (st2 && isVybStringStructType(st2)) {
                retainStringValue(fv);
                outVal = builder->CreateInsertValue(outVal, fv, i, "sdc.str");
                continue;
            }
        } else if (const vyb::ast::TypeNode* myArg = myTypeArg(f)) {
            if (fLLVM->isPointerTy() && isMyOwnedStructTypeNode(f)) {
                llvm::Type* pointeeTy = codegenType(const_cast<vyb::ast::TypeNode*>(myArg));
                if (auto* pois = llvm::dyn_cast<llvm::StructType>(pointeeTy)) {
                    llvm::Value* p = fv;
                    llvm::Value* isNull = builder->CreateICmpEQ(p, nullPtr, "sdc.my.null");
                    llvm::BasicBlock* doBB = llvm::BasicBlock::Create(*context, "sdc.my.do", currentFunction);
                    llvm::BasicBlock* doneBB = llvm::BasicBlock::Create(*context, "sdc.my.done", currentFunction);
                    llvm::BasicBlock* entryBlock = builder->GetInsertBlock();
                    builder->CreateCondBr(isNull, doneBB, doBB);
                    builder->SetInsertPoint(doBB);
                    llvm::DataLayout dl(module.get());
                    llvm::Value* blockBytes = llvm::ConstantInt::get(
                        llvm::Type::getInt64Ty(*context), dl.getTypeAllocSize(pois));
                    llvm::Value* rawNew = builder->CreateCall(getOrCreateMallocFunction(), {blockBytes}, "sdc.my.alloc");
                    llvm::Value* newBlock = builder->CreateBitCast(rawNew, pois->getPointerTo(), "sdc.my.block");
                    llvm::Value* pointeeVal = builder->CreateLoad(pois, p, "sdc.my.load");
                    llvm::Value* copied = generateStructDeepCopy(pointeeVal, myArg, pois);
                    builder->CreateStore(copied, newBlock);
                    llvm::BasicBlock* doDoneBlock = builder->GetInsertBlock();
                    builder->CreateBr(doneBB);
                    builder->SetInsertPoint(doneBB);
                    llvm::PHINode* ph = builder->CreatePHI(fLLVM, 2, "sdc.my.phi");
                    ph->addIncoming(nullPtr, entryBlock);
                    ph->addIncoming(newBlock, doDoneBlock);
                    outVal = builder->CreateInsertValue(outVal, ph, i, "sdc.my");
                    continue;
                }
            }
        } else if (isRefTypeNode(f, "mild") || isRefTypeNode(f, "our")) {
            if (fLLVM->isPointerTy()) {
                if (isRefTypeNode(f, "mild")) retainMildControlBlock(fv, "sdc.mild");
                else retainOurControlBlock(fv, "sdc.our");
                outVal = builder->CreateInsertValue(outVal, fv, i, "sdc.ref");
                continue;
            }
        } else if (isKnownStructTypeNode(f)) {
            if (auto* st3 = llvm::dyn_cast<llvm::StructType>(fLLVM)) {
                llvm::Value* nested = generateStructDeepCopy(fv, f, st3);
                outVal = builder->CreateInsertValue(outVal, nested, i, "sdc.nested");
                continue;
            }
        }
        // Plain scalar (or an uncommon unsupported field): copy by value.
        outVal = builder->CreateInsertValue(outVal, fv, i, "sdc.scalar");
    }
    return outVal;
}

// Deep-copy a standalone `my<Struct>` payload. `myPtr` points at a heap block of
// the pointee's layout; a fresh block is allocated and the pointee is recursively
// copied (String fields retained, Vec buffers cloned, nested `my` blocks
// deep-copied, our/mild control blocks retained), mirroring what
// `reclaimStructOwnedFieldsAt` will later release. Used when a new owner of a
// `my<Struct>` is created from a borrowed `my` (a `my` parameter being moved into
// a local or returned), so the new binding owns data independent of the caller's.
// Returns the new heap pointer (null when `myPtr` is null).
llvm::Value* LLVMCodegen::deepCopyMyStruct(llvm::Value* myPtr,
                                           const vyb::ast::TypeNode* pointeeAst,
                                           llvm::StructType* pointeeTy) {
    llvm::PointerType* rawPtr = llvm::PointerType::get(*context, 0);
    llvm::Constant* nullPtr = llvm::ConstantPointerNull::get(rawPtr);
    llvm::Value* isNull = builder->CreateICmpEQ(myPtr, nullPtr, "mydc.null");
    llvm::BasicBlock* doBB = llvm::BasicBlock::Create(*context, "mydc.do", currentFunction);
    llvm::BasicBlock* doneBB = llvm::BasicBlock::Create(*context, "mydc.done", currentFunction);
    llvm::BasicBlock* entryBlock = builder->GetInsertBlock();
    builder->CreateCondBr(isNull, doneBB, doBB);
    builder->SetInsertPoint(doBB);
    llvm::DataLayout dl(module.get());
    llvm::Value* blockBytes = llvm::ConstantInt::get(
        llvm::Type::getInt64Ty(*context), dl.getTypeAllocSize(pointeeTy));
    llvm::Value* rawNew = builder->CreateCall(getOrCreateMallocFunction(), {blockBytes}, "mydc.alloc");
    llvm::Value* newBlock = builder->CreateBitCast(rawNew, pointeeTy->getPointerTo(), "mydc.block");
    llvm::Value* pointeeVal = builder->CreateLoad(pointeeTy, myPtr, "mydc.load");
    llvm::Value* copied = generateStructDeepCopy(pointeeVal, pointeeAst, pointeeTy);
    builder->CreateStore(copied, newBlock);
    llvm::BasicBlock* doDoneBlock = builder->GetInsertBlock();
    builder->CreateBr(doneBB);
    builder->SetInsertPoint(doneBB);
    llvm::PHINode* ph = builder->CreatePHI(myPtr->getType(), 2, "mydc.phi");
    ph->addIncoming(nullPtr, entryBlock);
    ph->addIncoming(newBlock, doDoneBlock);
    return ph;
}

// Vec struct layout: { ptr, i64 (size), i64 (capacity) }
bool LLVMCodegen::isVybStringStructType(llvm::Type* type) {
    auto* st = llvm::dyn_cast<llvm::StructType>(type);
    if (!st || st->getNumElements() != 2) return false;
    // A Vyb String is an anonymous inline `{ ptr, i64 }` value. A *named* struct
    // that happens to share this shape (e.g. `struct Holder { first<my<Node>>,
    // second<Int> }`) is a user type, and treating it as a String here would make
    // the ownership / retention machinery mistrust a non-String first field (a
    // `my<Node>` heap pointer), so a callee would reclaim caller-owned data and a
    // returned holder would be handed back with a dangling field (double free).
    // Named generic / enum / tuple / option structs are likewise never a String.
    if (!st->isLiteral()) return false;
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

// Build the per-layout destructor for a closure capture environment that owns
// transferred standalone `my<Struct>` payloads. The runtime calls the returned
// function with the env block when its last reference is dropped (the cap_dtor
// slot at header offset 8). For each capture field that received a transferred
// object pointer, we reclaim the pointed-to struct's owned fields (Vec/String/
// my<T>/our/mild) then free the heap block, so the payload survives exactly as
// long as the closure env that owns it. Returns null when there is nothing to
// reclaim.
llvm::Function* LLVMCodegen::generateClosureEnvDtor(
        llvm::StructType* envTy, const std::string& tag,
        const std::vector<std::pair<size_t, const vyb::ast::TypeNode*>>& ownedFields) {
    if (!envTy || ownedFields.empty()) return nullptr;

    llvm::PointerType* rawPtr = llvm::PointerType::get(*context, 0);
    llvm::FunctionType* dtorTy = llvm::FunctionType::get(
        llvm::Type::getVoidTy(*context), {rawPtr}, false);
    std::string fnName = "closure.env.dtor." + tag;
    if (llvm::Function* existing = module->getFunction(fnName)) return existing;

    llvm::Function* dtor = llvm::Function::Create(
        dtorTy, llvm::Function::InternalLinkage, fnName, module.get());
    dtor->getArg(0)->setName("closure.env.raw");

    llvm::Function* savedFunction = currentFunction;
    llvm::BasicBlock* savedBlock = builder->GetInsertBlock();
    currentFunction = dtor;
    builder->SetInsertPoint(llvm::BasicBlock::Create(*context, "entry", dtor));

    llvm::Value* envCast = builder->CreateBitCast(dtor->getArg(0), envTy->getPointerTo(), "env.dtor.ptr");
    llvm::Constant* nullPtr = llvm::ConstantPointerNull::get(rawPtr);

    for (const auto& entry : ownedFields) {
        size_t ix = entry.first;
        const vyb::ast::TypeNode* pointeeAst = entry.second;
        if (ix + 2 >= envTy->getNumElements()) continue;
        if (!pointeeAst) continue;
        llvm::Value* fieldPtr = builder->CreateStructGEP(envTy, envCast, ix + 2, "env.dtor.item");
        llvm::Value* heapPtr = builder->CreateLoad(rawPtr, fieldPtr, "env.dtor.item.ptr");
        llvm::Value* isNull = builder->CreateICmpEQ(heapPtr, nullPtr, "env.dtor.item.null");
        llvm::Type* pointeeLL = codegenType(const_cast<vyb::ast::TypeNode*>(pointeeAst));

        llvm::BasicBlock* freeBB = llvm::BasicBlock::Create(*context, "env.dtor.free", currentFunction);
        llvm::BasicBlock* contBB = llvm::BasicBlock::Create(*context, "env.dtor.cont", currentFunction);
        builder->CreateCondBr(isNull, contBB, freeBB);
        builder->SetInsertPoint(freeBB);
        if (pointeeLL && llvm::isa<llvm::StructType>(pointeeLL)) {
            std::set<std::string> visited;
            reclaimStructOwnedFieldsAt(heapPtr, pointeeAst,
                                       llvm::cast<llvm::StructType>(pointeeLL), visited);
            builder->CreateCall(getOrCreateFreeFunction(), {heapPtr});
        }
        builder->CreateBr(contBB);
        builder->SetInsertPoint(contBB);
    }
    // The runtime only frees the env block itself when the cap_dtor is null;
    // when a per-layout destructor exists, __vyb_closure_release runs it and
    // expects the destructor to free the env block after reclaiming payloads.
    // (The env was allocated with malloc, so a plain free() completes it.)
    builder->CreateCall(getOrCreateFreeFunction(), {dtor->getArg(0)});
    builder->CreateRetVoid();

    currentFunction = savedFunction;
    builder->SetInsertPoint(savedBlock);
    return dtor;
}

// Build the per-layout destructor for an async-task environment that holds
// inline owned param fields (`{ i64 refcount; ptr cap_dtor; param0; ... }`).
// The runtime calls it once the last env reference is dropped (the cap_dtor slot
// at header offset 8): each String field's buffer reference is released, and each
// Vec field's buffer is reclaimed (its String elements released when it is a
// `Vec<String>`), then the heap block is freed. The worker and launcher hold
// their own references, so this only drops the env's retained copies. Returns
// null when there is nothing to reclaim.
llvm::Function* LLVMCodegen::generateAsyncEnvDtor(
        llvm::StructType* envTy, const std::string& tag,
        const std::vector<AsyncEnvField>& fields) {
    if (!envTy || fields.empty()) return nullptr;

    llvm::PointerType* rawPtr = llvm::PointerType::get(*context, 0);
    llvm::FunctionType* dtorTy = llvm::FunctionType::get(
        llvm::Type::getVoidTy(*context), {rawPtr}, false);
    std::string fnName = "async.env.dtor." + tag;
    if (llvm::Function* existing = module->getFunction(fnName)) return existing;

    llvm::Function* dtor = llvm::Function::Create(
        dtorTy, llvm::Function::InternalLinkage, fnName, module.get());
    dtor->getArg(0)->setName("async.env.raw");

    llvm::Function* savedFunction = currentFunction;
    llvm::BasicBlock* savedBlock = builder->GetInsertBlock();
    currentFunction = dtor;
    builder->SetInsertPoint(llvm::BasicBlock::Create(*context, "entry", dtor));

    llvm::Value* envCast = builder->CreateBitCast(dtor->getArg(0), envTy->getPointerTo(),
                                                  "async.env.dtor.ptr");
    llvm::Constant* nullPtr = llvm::ConstantPointerNull::get(rawPtr);
    llvm::Constant* zero = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
    for (const AsyncEnvField& fld : fields) {
        if (fld.fieldIx >= envTy->getNumElements()) continue;
        llvm::Value* fieldPtr = builder->CreateStructGEP(envTy, envCast, fld.fieldIx,
                                                         "async.env.dtor.field");
        llvm::Type* fty = envTy->getElementType(fld.fieldIx);
        if (fld.isVec) {
            if (auto* vt = llvm::dyn_cast<llvm::StructType>(fty)) {
                if (!isVecStructType(vt)) continue;
                llvm::Value* vec = builder->CreateLoad(vt, fieldPtr, "async.env.dtor.vec");
                llvm::Value* data = builder->CreateExtractValue(vec, 0, "async.env.dtor.vec.data");
                if (fld.vecIsString) {
                    llvm::Value* sz = builder->CreateExtractValue(vec, 1, "async.env.dtor.vec.size");
                    releaseStringElements(data, sz);
                }
                llvm::Value* isNull = builder->CreateICmpEQ(
                    data, nullPtr, "async.env.dtor.vec.null");
                llvm::BasicBlock* freeBB = llvm::BasicBlock::Create(*context, "async.env.dtor.vec.free", currentFunction);
                llvm::BasicBlock* contBB = llvm::BasicBlock::Create(*context, "async.env.dtor.vec.cont", currentFunction);
                builder->CreateCondBr(isNull, contBB, freeBB);
                builder->SetInsertPoint(freeBB);
                builder->CreateCall(getOrCreateFreeFunction(), {data});
                builder->CreateBr(contBB);
                builder->SetInsertPoint(contBB);
            }
        } else if (fld.isOur) {
            if (!fty->isPointerTy()) continue;
            llvm::Value* cb = builder->CreateLoad(rawPtr, fieldPtr, "async.env.dtor.our.cb");
            releaseOurControlBlock(cb, "async.env.dtor.our");
        } else if (fld.isString) {
            if (!isVybStringStructType(fty)) continue;
            llvm::Value* strVal = builder->CreateLoad(fty, fieldPtr, "async.env.dtor.string");
            releaseStringValue(strVal);
            llvm::Value* clr = llvm::UndefValue::get(llvm::cast<llvm::StructType>(fty));
            clr = builder->CreateInsertValue(clr, nullPtr, 0);
            clr = builder->CreateInsertValue(clr, zero, 1);
            builder->CreateStore(clr, fieldPtr);
        } else if (fld.isStruct && fld.structType) {
            // An inline struct param snapshot: reclaim every owned field of the
            // deep copy (String buffers, Vec storage, `my` blocks, our/mild
            // control-block refs, nested structs) before the env block is freed.
            if (auto* st3 = llvm::dyn_cast<llvm::StructType>(fty)) {
                std::set<std::string> visited;
                reclaimStructOwnedFieldsAt(fieldPtr, fld.structType, st3, visited);
            }
        } else if (fld.isClosure) {
            // A closure param snapshot: drop the env's reference to the closure's
            // capture environment (the +1 taken at launcher snapshot time).
            if (!isClosureStructType(fty)) continue;
            llvm::Value* cv = builder->CreateLoad(fty, fieldPtr, "async.env.dtor.closure");
            releaseClosureValue(cv);
        }
    }
    builder->CreateCall(getOrCreateFreeFunction(), {dtor->getArg(0)});
    builder->CreateRetVoid();

    currentFunction = savedFunction;
    builder->SetInsertPoint(savedBlock);
    return dtor;
}

// ============================================================================
// HEAP STRING REFERENCE COUNTING
// ============================================================================
// A Vyb String is a `{ ptr, len }` value that multiple holders (variables,
// parameters, Vec elements) may reference simultaneously, so heap buffers are
// reference counted in the runtime registry. Every storage location that takes
// a String value calls __vyb_string_retain() on the incoming buffer (unless it
// is receiving a freshly-created owned transfer), and every drop — scope exit,
// overwrite, temporary consumed — calls __vyb_string_release(). Untracked
// pointers (string literals in .rodata) are runtime no-ops.

void LLVMCodegen::retainStringValue(llvm::Value* strVal) {
    if (!strVal || !isVybStringStructType(strVal->getType())) return;
    llvm::Value* data = builder->CreateExtractValue(strVal, 0, "str.retain.data");
    builder->CreateCall(getOrCreateVybStringRetainFunction(), {data}, "str.retain");
}

void LLVMCodegen::releaseStringValue(llvm::Value* strVal) {
    if (!strVal || !isVybStringStructType(strVal->getType())) return;
    llvm::Value* data = builder->CreateExtractValue(strVal, 0, "str.release.data");
    builder->CreateCall(getOrCreateVybStringFreeFunction(), {data});
}

void LLVMCodegen::releaseStringAlloca(llvm::Value* allocaInst) {
    if (!allocaInst) return;
    llvm::Type* allocTy = nullptr;
    if (auto* ai = llvm::dyn_cast<llvm::AllocaInst>(allocaInst)) {
        allocTy = ai->getAllocatedType();
    }
    if (!allocTy || !isVybStringStructType(allocTy)) return;
    llvm::Value* strVal = builder->CreateLoad(allocTy, allocaInst, "str.release.alloca.load");
    releaseStringValue(strVal);
}

// Bulk String-element helpers for Vec<String> buffers. A String element is the
// uniform `{ ptr, i64 }` struct; release each buffer's held reference (when the
// Vec's storage is dropped) or retain each (when the elements are shallow-copied
// into an independent buffer that must own its own references).
llvm::Function* LLVMCodegen::getOrCreateVybStringReleaseEachFunction() {
    if (auto* f = module->getFunction("__vyb_string_release_each")) return f;
    llvm::FunctionType* ty = llvm::FunctionType::get(
        llvm::Type::getVoidTy(*context),
        {llvm::PointerType::get(*context, 0), llvm::Type::getInt64Ty(*context)},
        false);
    return llvm::Function::Create(ty, llvm::Function::ExternalLinkage,
                                  "__vyb_string_release_each", module.get());
}

llvm::Function* LLVMCodegen::getOrCreateVybStringRetainEachFunction() {
    if (auto* f = module->getFunction("__vyb_string_retain_each")) return f;
    llvm::FunctionType* ty = llvm::FunctionType::get(
        llvm::Type::getVoidTy(*context),
        {llvm::PointerType::get(*context, 0), llvm::Type::getInt64Ty(*context)},
        false);
    return llvm::Function::Create(ty, llvm::Function::ExternalLinkage,
                                  "__vyb_string_retain_each", module.get());
}

void LLVMCodegen::releaseStringElements(llvm::Value* dataPtr, llvm::Value* count) {
    if (!dataPtr || !count) return;
    builder->CreateCall(getOrCreateVybStringReleaseEachFunction(),
                        {dataPtr, count});
}

void LLVMCodegen::retainStringElements(llvm::Value* dataPtr, llvm::Value* count) {
    if (!dataPtr || !count) return;
    builder->CreateCall(getOrCreateVybStringRetainEachFunction(),
                        {dataPtr, count});
}
