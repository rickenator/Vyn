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
            // A data-carrying enum (Option<our<T>>, Result<..., our<T>>) owns a
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
            releaseOurControlBlock(controlBlockPtr, var.name);
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
                releaseOurControlBlock(cb, "reclaim.our");
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
void LLVMCodegen::releaseOurControlBlock(llvm::Value* controlBlockPtr, const std::string& tag) {
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

// Does a data-carrying built-in enum (Option<T>, Result<T, E>) carry an `our<T>`
// reference in one of its payloads? Only `our` refs need retain/release
// bookkeeping inside an enum: `mild` is a weak count (a copy must not re-count),
// and my/Vec/String payload ownership is handled by their own storage paths.
bool LLVMCodegen::enumPayloadHoldsOurRef(const vyb::ast::TypeNode* astType) const {
    auto* tn = dynamic_cast<const vyb::ast::TypeName*>(astType);
    if (!tn || !tn->identifier) return false;
    const std::string& base = tn->identifier->name;
    const bool isOption = (base == "Option" || base == "core::option::Option");
    const bool isResult = (base == "Result" || base == "core::result::Result");
    if (!isOption && !isResult) return false;
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
    const bool isOption = (base == "Option" || base == "core::option::Option");
    const bool isResult = (base == "Result" || base == "core::result::Result");
    if (!isOption && !isResult) return;

    const TaggedEnumInfo* info = findTaggedEnum(const_cast<vyb::ast::TypeNode*>(astType));
    if (!info) return;
    llvm::StructType* enumTy = info->llvmType;
    if (!enumTy) return;
    llvm::Value* enumVal = builder->CreateLoad(enumTy, enumPtr, "reclaim.enum");

    struct OwnedVariant { const char* variant; unsigned argIdx; };
    std::vector<OwnedVariant> variants;
    if (isOption) variants.push_back({"Some", 0});
    else { variants.push_back({"Ok", 0}); variants.push_back({"Err", 1}); }

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
            else releaseOurControlBlock(cb, "reclaim.enum.release");
        }
        builder->CreateBr(nextBB);
        builder->SetInsertPoint(nextBB);
    }
}

// Does an enum-typed initializer hand over its payload's strong ref (a "fresh
// transfer" that needs no further retain on stow), or is it a borrowed copy that
// must be retained? `grab()` and function calls returning the enum transfer;
// `Some(...)` with a fresh `our(...)`/`.grab()` payload also transfers. A bare
// `Some(owner)` (borrowing an existing `our`) is a copy and must be retained.
bool LLVMCodegen::enumInitIsOurTransfer(vyb::ast::Expression* init) {
    if (!init) return false;
    auto* call = dynamic_cast<vyb::ast::CallExpression*>(init);
    if (!call) return false;
    if (auto* id = dynamic_cast<vyb::ast::Identifier*>(call->callee.get())) {
        if (id->name == "Some" || id->name == "Ok" || id->name == "Err") {
            if (call->arguments.empty()) return true;  // unit variant (e.g. None-like)
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
