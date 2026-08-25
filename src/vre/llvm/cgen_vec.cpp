// SPDX-License-Identifier: Apache-2.0

#include "vyb/vre/llvm/codegen.hpp"
#include "vyb/parser/ast.hpp"
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/DerivedTypes.h>
#include <iostream>

namespace vyb {

void LLVMCodegen::handleVecMethod(vyb::ast::CallExpression* node, const std::string& objectName, const std::string& methodName) {
    // Look up the Vec object in namedValues
    auto it = namedValues.find(objectName);
    if (it == namedValues.end()) {
        logError(node->loc, "Undefined Vec variable: " + objectName);
        m_currentLLVMValue = nullptr;
        return;
    }

    llvm::Value* vecPtr = it->second;

    // Ensure it's the Vec struct type: { ptr, size, capacity }
    llvm::Type* vecPtrType = vecPtr->getType();
    if (!vecPtrType->isPointerTy()) {
        logError(node->loc, "Vec variable is not a pointer type");
        m_currentLLVMValue = nullptr;
        return;
    }

    // For opaque pointers in newer LLVM, we need to get the type differently
    // Assume it's the Vec struct type: { ptr, i64, i64 }
    std::vector<llvm::Type*> vecFields = {
        llvm::PointerType::get(*context, 0), // ptr to elements
        llvm::Type::getInt64Ty(*context),    // size
        llvm::Type::getInt64Ty(*context)     // capacity
    };
    llvm::Type* vecStructType = llvm::StructType::get(*context, vecFields, false);
    // Struct type validation is handled by the type construction above

    if (methodName == "push") {
        handleVecPush(node, vecPtr, vecStructType);
    } else if (methodName == "pop") {
        handleVecPop(node, vecPtr, vecStructType);
    } else if (methodName == "len") {
        handleVecLen(node, vecPtr, vecStructType);
    } else if (methodName == "get") {
        handleVecGet(node, vecPtr, vecStructType);
    } else if (methodName == "set") {
        handleVecSet(node, vecPtr, vecStructType);
    } else if (methodName == "push_array") {
        handleVecPushArray(node, vecPtr, vecStructType);
    } else if (methodName == "to_array") {
        handleVecToArray(node, vecPtr, vecStructType);
    } else if (methodName == "clear") {
        handleVecClear(node, vecPtr, vecStructType);
    } else if (methodName == "is_empty") {
        handleVecIsEmpty(node, vecPtr, vecStructType);
    } else if (methodName == "capacity") {
        handleVecCapacity(node, vecPtr, vecStructType);
    } else if (methodName == "concat") {
        handleVecConcat(node, vecPtr, vecStructType);
    } else if (methodName == "contains") {
        handleVecContains(node, vecPtr, vecStructType);
    } else if (methodName == "remove_at" || methodName == "remove") {
        handleVecRemoveAt(node, vecPtr, vecStructType);
    } else if (methodName == "resize") {
        handleVecResize(node, vecPtr, vecStructType);
    } else if (methodName == "get_array") {
        handleVecGetArray(node, vecPtr, vecStructType);
    } else if (methodName == "get_vec") {
        handleVecGetVec(node, vecPtr, vecStructType);
    } else {
        logError(node->loc, "Unknown Vec method: " + methodName);
        m_currentLLVMValue = nullptr;
    }
}

void LLVMCodegen::handleVecPush(vyb::ast::CallExpression* node, llvm::Value* vecPtr, llvm::Type* vecStructType) {
    if (node->arguments.size() != 1) {
        logError(node->loc, "Vec::push expects exactly 1 argument");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Evaluate the argument to push
    node->arguments[0]->accept(*this);
    llvm::Value* valueToAdd = m_currentLLVMValue;
    if (!valueToAdd) {
        logError(node->loc, "Failed to evaluate argument for Vec::push");
        return;
    }

    // Store a String element in one canonical form. A raw `char*` (e.g. the
    // direct result of `__vyb_string_concat` / `__vyb_int_to_string`) is wrapped
    // into the 16-byte { ptr, i64 } String struct so every Vec<String> element
    // shares the same layout (get/set/clear all stride by that struct).
    valueToAdd = normalizeVecStringElement(valueToAdd);

    // Get element type from the value being pushed
    llvm::Type* elementType = valueToAdd->getType();

    // The Vec slot will hold its own reference to a pushed String binding. A
    // freshly-created String (concat / to_string / a String-returning call)
    // hands its single owned reference to the Vec as-is; any other (borrowed)
    // source must be retained (+1) so the element outlives the producing scope.
    if (elementType && isVybStringStructType(elementType) &&
        !exprIsStringTransfer(node->arguments[0].get())) {
        retainStringValue(valueToAdd);
    }

    // Calculate the actual element size using DataLayout
    llvm::DataLayout dataLayout(module.get());
    uint64_t elementSizeBytes = dataLayout.getTypeAllocSize(elementType);

    // Get pointers to struct fields
    llvm::Value* dataFieldPtr = builder->CreateStructGEP(vecStructType, vecPtr, 0, "vec.data_ptr");
    llvm::Value* sizeFieldPtr = builder->CreateStructGEP(vecStructType, vecPtr, 1, "vec.size_ptr");
    llvm::Value* capFieldPtr = builder->CreateStructGEP(vecStructType, vecPtr, 2, "vec.cap_ptr");

    // Load current size and capacity
    llvm::Value* currentSize = builder->CreateLoad(llvm::Type::getInt64Ty(*context), sizeFieldPtr, "vec.current_size");
    llvm::Value* currentCap = builder->CreateLoad(llvm::Type::getInt64Ty(*context), capFieldPtr, "vec.current_cap");
    llvm::Value* dataPtr = builder->CreateLoad(llvm::PointerType::get(*context, 0), dataFieldPtr, "vec.data");

    // Check if we need to allocate/grow
    llvm::Value* needsAlloc = builder->CreateICmpEQ(currentCap, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0), "vec.needs_alloc");
    llvm::Value* needsGrow = builder->CreateICmpEQ(currentSize, currentCap, "vec.needs_grow");
    llvm::Value* needsRealloc = builder->CreateOr(needsAlloc, needsGrow, "vec.needs_realloc");

    llvm::BasicBlock* entryBlock = builder->GetInsertBlock();
    llvm::BasicBlock* allocBlock = llvm::BasicBlock::Create(*context, "vec.alloc", entryBlock->getParent());
    llvm::BasicBlock* copyBlock = llvm::BasicBlock::Create(*context, "vec.copy", entryBlock->getParent());
    llvm::BasicBlock* noCopyBlock = llvm::BasicBlock::Create(*context, "vec.no_copy", entryBlock->getParent());
    llvm::BasicBlock* mergeBlock = llvm::BasicBlock::Create(*context, "vec.merge", entryBlock->getParent());

    builder->CreateCondBr(needsRealloc, allocBlock, mergeBlock);

    // Alloc block - allocate or grow the array
    builder->SetInsertPoint(allocBlock);

    // New capacity: if 0, start with 4, else double it
    llvm::Value* newCap = builder->CreateSelect(
        needsAlloc,
        llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 4),
        builder->CreateMul(currentCap, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 2)),
        "vec.new_cap"
    );

    // Calculate allocation size using actual element size
    llvm::Value* elementSize = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), elementSizeBytes);
    llvm::Value* allocSize = builder->CreateMul(newCap, elementSize, "vec.alloc_size");

    // Call malloc
    llvm::FunctionType* mallocType = llvm::FunctionType::get(
        llvm::PointerType::get(*context, 0),
        {llvm::Type::getInt64Ty(*context)},
        false
    );
    llvm::Function* mallocFunc = module->getFunction("malloc");
    if (!mallocFunc) {
        mallocFunc = llvm::Function::Create(mallocType, llvm::Function::ExternalLinkage, "malloc", module.get());
    }
    llvm::Value* newDataPtr = builder->CreateCall(mallocFunc, {allocSize}, "vec.new_data");

    // If there was old data, copy it (using memcpy if size > 0)
    llvm::Value* hasData = builder->CreateICmpNE(currentSize, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0), "vec.has_data");
    builder->CreateCondBr(hasData, copyBlock, noCopyBlock);

    // Copy block
    builder->SetInsertPoint(copyBlock);
    llvm::Value* copySize = builder->CreateMul(currentSize, elementSize, "vec.copy_size");
    llvm::FunctionType* memcpyType = llvm::FunctionType::get(
        llvm::PointerType::get(*context, 0),
        {llvm::PointerType::get(*context, 0), llvm::PointerType::get(*context, 0), llvm::Type::getInt64Ty(*context)},
        false
    );
    llvm::Function* memcpyFunc = module->getFunction("memcpy");
    if (!memcpyFunc) {
        memcpyFunc = llvm::Function::Create(memcpyType, llvm::Function::ExternalLinkage, "memcpy", module.get());
    }
    builder->CreateCall(memcpyFunc, {newDataPtr, dataPtr, copySize});
    builder->CreateBr(noCopyBlock);

    // No copy block - update the Vec struct
    builder->SetInsertPoint(noCopyBlock);
    builder->CreateStore(newDataPtr, dataFieldPtr);
    builder->CreateStore(newCap, capFieldPtr);
    // Free the OLD data buffer that this growth step replaced. For String
    // elements the owned references were moved (memcpy'd, not released) into the
    // new buffer, so only the now-orphaned raw buffer is freed here — the Vec
    // keeps owning exactly the same set of elements in its new storage.
    llvm::Function* freeFunc = getOrCreateFreeFunction();
    builder->CreateCall(freeFunc, {dataPtr});
    builder->CreateBr(mergeBlock);

    // Merge block - both paths (alloc and no-alloc) meet here
    // IMPORTANT: This must be created last so it becomes func->back() and gets automatic terminator
    builder->SetInsertPoint(mergeBlock);

    // Reload data pointer (might have changed in alloc block)
    llvm::Value* finalDataPtr = builder->CreateLoad(llvm::PointerType::get(*context, 0), dataFieldPtr, "vec.final_data");

    // Reload size (shouldn't have changed, but for clarity)
    llvm::Value* reloadedSize = builder->CreateLoad(llvm::Type::getInt64Ty(*context), sizeFieldPtr, "vec.reloaded_size");

    // Calculate offset for new element using actual element size
    llvm::Value* elementSize2 = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), elementSizeBytes);
    llvm::Value* offset = builder->CreateMul(reloadedSize, elementSize2, "vec.offset");
    llvm::Value* elementPtr = builder->CreateGEP(llvm::Type::getInt8Ty(*context), finalDataPtr, offset, "vec.element_ptr");

    // Store the value - need to handle different types
    if (elementType->isStructTy()) {
        // For structs, do a memcpy from the source to destination
        // valueToAdd should be a pointer to the struct
        llvm::Value* srcPtr = valueToAdd;
        if (!valueToAdd->getType()->isPointerTy()) {
            // If valueToAdd is a struct value (not a pointer), we need to create a temporary
            llvm::Value* tempAlloca = builder->CreateAlloca(elementType, nullptr, "vec.temp_struct");
            builder->CreateStore(valueToAdd, tempAlloca);
            srcPtr = tempAlloca;
        }

        llvm::FunctionType* memcpyType2 = llvm::FunctionType::get(
            llvm::PointerType::get(*context, 0),
            {llvm::PointerType::get(*context, 0), llvm::PointerType::get(*context, 0), llvm::Type::getInt64Ty(*context)},
            false
        );
        llvm::Function* memcpyFunc2 = module->getFunction("memcpy");
        if (!memcpyFunc2) {
            memcpyFunc2 = llvm::Function::Create(memcpyType2, llvm::Function::ExternalLinkage, "memcpy", module.get());
        }

        // A struct element that owns heap data (a Vec/String/my field) must be
        // deep-copied into the slot, not memcpy'd. A shallow copy makes the arena
        // element and the source binding share one inner buffer, so when both are
        // reclaimed on scope exit the same buffer is freed twice ("free(): double
        // free detected"). generateStructDeepCopy clones each owned field so the
        // slot owns data fully independent of the pushed value.
        bool structNeedsDeepCopy =
            elementType->isStructTy() &&
            node->arguments[0]->type &&
            isKnownStructTypeNode(node->arguments[0]->type.get()) &&
            structTypeHasOwnedFields(node->arguments[0]->type.get());

        if (structNeedsDeepCopy) {
            // Generate the deep copy of the source struct value.
            llvm::Value* deepCopy = generateStructDeepCopy(
                builder->CreateLoad(elementType, srcPtr, "vec.push.struct_load"),
                node->arguments[0]->type.get(),
                llvm::cast<llvm::StructType>(elementType));
            builder->CreateStore(deepCopy, elementPtr);
        } else {
            builder->CreateCall(memcpyFunc2, {elementPtr, srcPtr, elementSize2});
        }
    } else {
        // For primitives, direct store
        builder->CreateStore(valueToAdd, elementPtr);
    }
    // A fresh owned-struct TEMP argument (e.g. `v.push(make_node(3))`) was deep-
    // copied into the slot above, so its ORIGINAL owned buffers are now dead and
    // must be reclaimed or they leak (#192). A named/borrow arg owns its own
    // cleanup and is intentionally left alone.
    reclaimFreshStructArgTemp(node, 0, valueToAdd, elementType);

    // Increment size
    llvm::Value* newSize = builder->CreateAdd(reloadedSize, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 1), "vec.new_size");
    builder->CreateStore(newSize, sizeFieldPtr);
    VYB_CDBG << "DEBUG: Vec::push() called - element stored, returning Vec for chaining" << std::endl;

    // Return the Vec pointer to enable method chaining
    m_currentLLVMValue = vecPtr;
}

void LLVMCodegen::reclaimFreshStructArgTemp(vyb::ast::CallExpression* node, unsigned argIdx,
                                            llvm::Value* value, llvm::Type* elementType) {
    if (!node || argIdx >= node->arguments.size() || !value || !elementType) return;
    // Only a fresh owned-struct TEMP (a call or object literal created at this
    // site) is reclaimed here; a named/borrowed source is owned by its own
    // cleanup and must NOT be touched (mirrors pendingStructTempReclaims).
    bool fresh = dynamic_cast<ast::CallExpression*>(node->arguments[argIdx].get()) != nullptr ||
                 dynamic_cast<ast::ObjectLiteral*>(node->arguments[argIdx].get()) != nullptr;
    if (!fresh || !node->arguments[argIdx]->type) return;
    const vyb::ast::TypeNode* at = node->arguments[argIdx]->type.get();
    if (!isKnownStructTypeNode(at) || !structTypeHasOwnedFields(at)) return;
    auto* st = llvm::dyn_cast<llvm::StructType>(elementType);
    if (!st) return;
    llvm::Value* src = value;
    if (!value->getType()->isPointerTy()) {
        llvm::Value* tA = builder->CreateAlloca(st, nullptr, "vecarg.tmp");
        builder->CreateStore(value, tA);
        src = tA;
    }
    std::set<std::string> visited;
    reclaimStructOwnedFieldsAt(src, at, st, visited);
}

void LLVMCodegen::handleVecPop(vyb::ast::CallExpression* node, llvm::Value* vecPtr, llvm::Type* vecStructType) {
    if (node->arguments.size() != 0) {
        logError(node->loc, "Vec::pop expects no arguments");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Get pointers to struct fields
    llvm::Value* dataFieldPtr = builder->CreateStructGEP(vecStructType, vecPtr, 0, "vec.data_ptr");
    llvm::Value* sizeFieldPtr = builder->CreateStructGEP(vecStructType, vecPtr, 1, "vec.size_ptr");

    // Load current size
    llvm::Value* currentSize = builder->CreateLoad(llvm::Type::getInt64Ty(*context), sizeFieldPtr, "vec.current_size");

    // Check if size > 0
    llvm::Value* isEmpty = builder->CreateICmpEQ(currentSize,
                                                llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0),
                                                "vec.is_empty");

    // For now, just decrement size if not empty
    llvm::Value* newSize = builder->CreateSub(currentSize,
                                             llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 1),
                                             "vec.new_size");

    // Use select to avoid underflow: newSize = isEmpty ? 0 : (currentSize - 1)
    llvm::Value* safeNewSize = builder->CreateSelect(isEmpty,
                                                    llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0),
                                                    newSize,
                                                    "vec.safe_new_size");

    // Store new size before returning the removed value.
    builder->CreateStore(safeNewSize, sizeFieldPtr);

    llvm::Type* elementType = nullptr;
    if (node->type) {
        elementType = codegenType(node->type.get());
    }
    if (!elementType) {
        elementType = llvm::Type::getInt64Ty(*context);
    }

    llvm::DataLayout dataLayout(module.get());
    uint64_t elementSizeBytes = dataLayout.getTypeAllocSize(elementType);
    llvm::Value* elementSize = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), elementSizeBytes);

    llvm::Function* function = builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* valueBB = llvm::BasicBlock::Create(*context, "vec.pop_value", function);
    llvm::BasicBlock* emptyBB = llvm::BasicBlock::Create(*context, "vec.pop_empty", function);
    llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*context, "vec.pop_merge", function);
    builder->CreateCondBr(isEmpty, emptyBB, valueBB);

    builder->SetInsertPoint(valueBB);
    llvm::Value* dataPtr = builder->CreateLoad(llvm::PointerType::get(*context, 0), dataFieldPtr, "vec.data");
    llvm::Value* offset = builder->CreateMul(safeNewSize, elementSize, "vec.pop_offset");
    llvm::Value* elementPtr = builder->CreateGEP(llvm::Type::getInt8Ty(*context), dataPtr, offset, "vec.pop_ptr");
    llvm::Value* poppedValue = builder->CreateLoad(elementType, elementPtr, "vec.popped_value");
    builder->CreateBr(mergeBB);
    valueBB = builder->GetInsertBlock();

    builder->SetInsertPoint(emptyBB);
    llvm::Value* emptyValue = llvm::Constant::getNullValue(elementType);
    builder->CreateBr(mergeBB);
    emptyBB = builder->GetInsertBlock();

    builder->SetInsertPoint(mergeBB);
    llvm::PHINode* result = builder->CreatePHI(elementType, 2, "vec.pop_result");
    result->addIncoming(emptyValue, emptyBB);
    result->addIncoming(poppedValue, valueBB);

    VYB_CDBG << "DEBUG: Vec::pop() called - size decremented" << std::endl;

    m_currentLLVMValue = result;
}

void LLVMCodegen::handleVecLen(vyb::ast::CallExpression* node, llvm::Value* vecPtr, llvm::Type* vecStructType) {
    if (node->arguments.size() != 0) {
        logError(node->loc, "Vec::len expects no arguments");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Get pointer to size field
    llvm::Value* sizeFieldPtr = builder->CreateStructGEP(vecStructType, vecPtr, 1, "vec.size_ptr");

    // Load and return current size
    m_currentLLVMValue = builder->CreateLoad(llvm::Type::getInt64Ty(*context), sizeFieldPtr, "vec.len");

    VYB_CDBG << "DEBUG: Vec::len() called" << std::endl;
}

void LLVMCodegen::handleVecGet(vyb::ast::CallExpression* node, llvm::Value* vecPtr, llvm::Type* vecStructType) {
    if (node->arguments.size() != 1) {
        logError(node->loc, "Vec::get expects exactly 1 argument (index)");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Evaluate the index argument
    node->arguments[0]->accept(*this);
    llvm::Value* index = m_currentLLVMValue;
    if (!index) {
        logError(node->loc, "Failed to evaluate index for Vec::get");
        return;
    }

    // Get the element type from the CallExpression's type (return type)
    // The semantic analyzer should have set this to the element type (T from Vec<T>)
    llvm::Type* elementLLVMType = nullptr;
    uint64_t elementSizeBytes = 8; // Default to 8 bytes

    if (node->type) {
        // Convert AST type to LLVM type
        elementLLVMType = codegenType(node->type.get());
        if (elementLLVMType) {
            llvm::DataLayout dataLayout(module.get());
            elementSizeBytes = dataLayout.getTypeAllocSize(elementLLVMType);
        }
    }

    // Fallback to i64 if we couldn't determine the type
    if (!elementLLVMType) {
        elementLLVMType = llvm::Type::getInt64Ty(*context);
        elementSizeBytes = 8;
    }

    // Get pointers to struct fields
    llvm::Value* dataFieldPtr = builder->CreateStructGEP(vecStructType, vecPtr, 0, "vec.data_ptr");
    llvm::Value* sizeFieldPtr = builder->CreateStructGEP(vecStructType, vecPtr, 1, "vec.size_ptr");

    // Load the data pointer and size
    llvm::Value* dataPtr = builder->CreateLoad(llvm::PointerType::get(*context, 0), dataFieldPtr, "vec.data");
    llvm::Value* size = builder->CreateLoad(llvm::Type::getInt64Ty(*context), sizeFieldPtr, "vec.size");

    // Indexes are signed Int values at the language level. An unsigned compare
    // rejects both negative indexes (which become large unsigned values) and
    // indexes at or beyond the current size before calculating an address.
    llvm::Value* indexInBounds = builder->CreateICmpULT(index, size, "vec.get.in_bounds");
    llvm::BasicBlock* boundsCheckBlock = builder->GetInsertBlock();
    llvm::BasicBlock* validBlock = llvm::BasicBlock::Create(
        *context, "vec.get.valid", boundsCheckBlock->getParent());
    llvm::BasicBlock* mergeBlock = llvm::BasicBlock::Create(
        *context, "vec.get.merge", boundsCheckBlock->getParent());
    builder->CreateCondBr(indexInBounds, validBlock, mergeBlock);

    builder->SetInsertPoint(validBlock);

    // Calculate offset: data_ptr + (index * element_size)
    llvm::Value* elementSize = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), elementSizeBytes);
    llvm::Value* offset = builder->CreateMul(index, elementSize, "vec.offset");
    llvm::Value* elementPtr = builder->CreateGEP(llvm::Type::getInt8Ty(*context), dataPtr, offset, "vec.element_ptr");

    // Load the element value based on type. The valid path rejoins the
    // out-of-bounds default below so no invalid address is ever dereferenced.
    llvm::Value* element;
    llvm::BasicBlock* validIncoming = validBlock;
    if (elementLLVMType->isStructTy()) {
        element = builder->CreateLoad(elementLLVMType, elementPtr, "vec.element_struct");
        // A struct element that owns heap data is returned by shallow load, which
        // aliases the slot's inner buffers into the caller's fresh binding. When
        // that binding is reclaimed on scope exit it frees the same buffers the
        // slot (and any other get() caller) still owns -> double free. Deep-copy
        // owned fields so the returned value owns data independent of the slot.
        if (node->type &&
            isKnownStructTypeNode(node->type.get()) &&
            structTypeHasOwnedFields(node->type.get())) {
            element = generateStructDeepCopy(
                element, node->type.get(), llvm::cast<llvm::StructType>(elementLLVMType));
            // generateStructDeepCopy may emit new blocks (e.g. a Vec-field clone
            // loop). The deep-copied value is defined in that last block, so the
            // merge PHI must treat it (not the original validBlock) as the
            // incoming predecessor, or LLVM verification fails ("PHI node entries
            // do not match predecessors").
            validIncoming = builder->GetInsertBlock();
        }
    } else {
        element = builder->CreateLoad(elementLLVMType, elementPtr, "vec.element");
    }
    builder->CreateBr(mergeBlock);

    builder->SetInsertPoint(mergeBlock);
    llvm::Value* defaultValue;
    if (elementLLVMType->isIntegerTy()) {
        defaultValue = llvm::ConstantInt::get(elementLLVMType, 0);
    } else if (elementLLVMType->isFloatingPointTy()) {
        defaultValue = llvm::ConstantFP::get(elementLLVMType, 0.0);
    } else if (elementLLVMType->isStructTy() || elementLLVMType->isArrayTy()) {
        defaultValue = llvm::ConstantAggregateZero::get(elementLLVMType);
    } else {
        defaultValue = llvm::Constant::getNullValue(elementLLVMType);
    }
    llvm::PHINode* result = builder->CreatePHI(elementLLVMType, 2, "vec.get.result");
    result->addIncoming(element, validIncoming);
    result->addIncoming(defaultValue, boundsCheckBlock);
    m_currentLLVMValue = result;

    VYB_CDBG << "DEBUG: Vec::get() called - bounds-safe element retrieval" << std::endl;
}

void LLVMCodegen::handleVecSet(vyb::ast::CallExpression* node, llvm::Value* vecPtr, llvm::Type* vecStructType) {
    if (node->arguments.size() != 2) {
        logError(node->loc, "Vec::set expects exactly 2 arguments (index, value)");
        m_currentLLVMValue = nullptr;
        return;
    }

    node->arguments[0]->accept(*this);
    llvm::Value* index = m_currentLLVMValue;
    node->arguments[1]->accept(*this);
    llvm::Value* value = m_currentLLVMValue;
    if (!index || !value) {
        logError(node->loc, "Failed to evaluate index/value for Vec::set");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Same single-layout normalization as push: a raw char* String element must
    // live in the Vec as the canonical { ptr, i64 } struct.
    value = normalizeVecStringElement(value);

    // Determine the element type from the value argument so element size is right.
    llvm::Type* elementLLVMType = llvm::Type::getInt64Ty(*context);
    uint64_t elementSizeBytes = 8;
    llvm::Type* t = value->getType();
    if (t) {
        elementLLVMType = t;
        llvm::DataLayout dataLayout(module.get());
        elementSizeBytes = dataLayout.getTypeAllocSize(elementLLVMType);
    }

    llvm::Value* dataPtr = builder->CreateLoad(
        llvm::PointerType::get(*context, 0),
        builder->CreateStructGEP(vecStructType, vecPtr, 0, "vec.set.data_ptr"),
        "vec.set.data");

    llvm::Value* size = builder->CreateLoad(
        llvm::Type::getInt64Ty(*context),
        builder->CreateStructGEP(vecStructType, vecPtr, 1, "vec.set.size_ptr"),
        "vec.set.size");
    llvm::Value* indexInBounds = builder->CreateICmpULT(index, size, "vec.set.in_bounds");
    llvm::BasicBlock* boundsCheckBlock = builder->GetInsertBlock();
    llvm::BasicBlock* validBlock = llvm::BasicBlock::Create(
        *context, "vec.set.valid", boundsCheckBlock->getParent());
    llvm::BasicBlock* mergeBlock = llvm::BasicBlock::Create(
        *context, "vec.set.merge", boundsCheckBlock->getParent());
    builder->CreateCondBr(indexInBounds, validBlock, mergeBlock);

    builder->SetInsertPoint(validBlock);

    llvm::Value* elementSize = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), elementSizeBytes);
    llvm::Value* offset = builder->CreateMul(index, elementSize, "vec.set.offset");
    llvm::Value* elementPtr = builder->CreateGEP(llvm::Type::getInt8Ty(*context), dataPtr, offset, "vec.set.element_ptr");

    // String elements: the overwritten slot's reference is dropped and the new
    // value is stored with its own reference (a fresh transfer already owns one;
    // a borrowed source must be retained).
    if (elementLLVMType && isVybStringStructType(elementLLVMType)) {
        llvm::Value* oldElem = builder->CreateLoad(elementLLVMType, elementPtr, "vec.set.old_elem");
        releaseStringValue(oldElem);
        if (!exprIsStringTransfer(node->arguments[1].get())) {
            retainStringValue(value);
        }
    } else if (elementLLVMType && elementLLVMType->isStructTy() &&
               node->arguments[1]->type &&
               isKnownStructTypeNode(node->arguments[1]->type.get()) &&
               structTypeHasOwnedFields(node->arguments[1]->type.get())) {
        // Struct element owning heap data: release the overwritten slot's owned
        // fields (it is being dropped), then store a deep copy of the new value
        // so the slot owns data independent of the assigning source (shallow copy
        // would alias and double-free on scope exit).
        std::set<std::string> visited;
        reclaimStructOwnedFieldsAt(
            elementPtr, node->arguments[1]->type.get(),
            llvm::cast<llvm::StructType>(elementLLVMType), visited);
        llvm::Value* deepCopy = generateStructDeepCopy(
            value, node->arguments[1]->type.get(),
            llvm::cast<llvm::StructType>(elementLLVMType));
        // A fresh owned-struct TEMP new-value (e.g. `v.set(i, make_node(5))`)
        // was deep-copied above; reclaim its original buffers (#192). A named
        // source owns its own cleanup and is intentionally left alone.
        reclaimFreshStructArgTemp(node, 1, value, elementLLVMType);
        value = deepCopy;
    }

    builder->CreateStore(value, elementPtr);
    builder->CreateBr(mergeBlock);

    builder->SetInsertPoint(mergeBlock);
    m_currentLLVMValue = nullptr;
}

void LLVMCodegen::handleVecPushArray(vyb::ast::CallExpression* node, llvm::Value* vecPtr, llvm::Type* vecStructType) {
    if (node->arguments.size() != 1) {
        logError(node->loc, "Vec::push_array expects exactly 1 argument (array)");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Evaluate the array argument
    node->arguments[0]->accept(*this);
    llvm::Value* arrayValue = m_currentLLVMValue;
    if (!arrayValue) {
        logError(node->loc, "Failed to evaluate array argument for Vec::push_array");
        return;
    }

    VYB_CDBG << "DEBUG: Vec::push_array() called - pushing entire array" << std::endl;

    // For now, placeholder implementation - would need proper array iteration
    // Return the Vec reference for method chaining
    m_currentLLVMValue = vecPtr;
}

llvm::Value* LLVMCodegen::normalizeVecStringElement(llvm::Value* value) {
    if (!value) return value;
    // A raw `char*` String value (from __vyb_string_concat, __vyb_int_to_string,
    // and friends) is wrapped into the canonical anonymous { ptr, i64 } String
    // struct on store. Everything else (already-struct Strings, Ints, owned
    // struct pointers, ...) is left untouched.
    if (value->getType() != int8PtrType) return value;

    llvm::StructType* strTy = llvm::StructType::get(*context,
        {int8PtrType, llvm::Type::getInt64Ty(*context)}, /*isPacked=*/false);
    llvm::Function* strlenF = module->getFunction("strlen");
    if (!strlenF) {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            llvm::Type::getInt64Ty(*context), {int8PtrType}, false);
        strlenF = llvm::Function::Create(ft, llvm::Function::ExternalLinkage,
                                         "strlen", module.get());
    }
    llvm::Value* len = builder->CreateCall(strlenF, {value}, "vec.str.len");
    llvm::Value* v = llvm::UndefValue::get(strTy);
    v = builder->CreateInsertValue(v, value, 0, "vec.str.ptr");
    v = builder->CreateInsertValue(v, len, 1, "vec.str.struct");
    return v;
}

void LLVMCodegen::handleVecToArray(vyb::ast::CallExpression* node, llvm::Value* vecPtr, llvm::Type* vecStructType) {
    if (node->arguments.size() != 1) {
        logError(node->loc, "Vec::to_array expects exactly 1 argument (array size)");
        m_currentLLVMValue = nullptr;
        return;
    }

    VYB_CDBG << "DEBUG: Vec::to_array() called - converting to fixed array" << std::endl;

    // The call's static type is the fixed array target, e.g. `[T; N]`. Recover
    // the element type from that annotation; the length N is the (compile-time)
    // size argument. Together they give a real `[N x T]` value instead of the
    // old placeholder scalar.
    llvm::ArrayType* arrayTy = nullptr;
    ast::TypeNode* resultType = node->type.get();
    if (ast::ArrayType* arrAst = dynamic_cast<ast::ArrayType*>(resultType)) {
        llvm::Type* elemTy = arrAst->elementType ? codegenType(arrAst->elementType.get()) : nullptr;
        if (elemTy) {
            llvm::Value* sizeV = nullptr;
            if (ast::IntegerLiteral* sz = dynamic_cast<ast::IntegerLiteral*>(node->arguments[0].get())) {
                uint64_t n = sz->isUnsigned ? (uint64_t)sz->uvalue : (uint64_t)sz->value;
                sizeV = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), n);
            } else {
                node->arguments[0]->accept(*this);
                sizeV = m_currentLLVMValue;
            }
            if (sizeV) {
                if (auto* ci = llvm::dyn_cast<llvm::ConstantInt>(sizeV)) {
                    uint64_t n = ci->getZExtValue();
                    if (n > 0) arrayTy = llvm::ArrayType::get(elemTy, n);
                } else {
                    arrayTy = nullptr; // runtime-sized targets need more work
                }
            }
        }
    }

    if (!arrayTy) {
        logError(node->loc, "Vec::to_array requires a fixed-size array target (e.g. `[Int; 2]`)");
        m_currentLLVMValue = nullptr;
        return;
    }

    llvm::Type* elemTy = arrayTy->getElementType();
    const uint64_t count = arrayTy->getNumElements();
    llvm::DataLayout dl(module.get());
    const uint64_t elemBytes = dl.getTypeAllocSize(elemTy);

    // Dense storage: { ptr data, i64 size, i64 capacity }. Fetch the data pointer
    // and the Vec's logical size so we copy only up to what is actually present.
    llvm::Value* dataPtr = builder->CreateLoad(
        llvm::PointerType::get(*context, 0),
        builder->CreateStructGEP(vecStructType, vecPtr, 0, "vec.toarr.data_ptr"),
        "vec.toarr.data");
    llvm::Value* vecSize = builder->CreateLoad(
        llvm::Type::getInt64Ty(*context),
        builder->CreateStructGEP(vecStructType, vecPtr, 1, "vec.toarr.size_ptr"),
        "vec.toarr.size");

    llvm::Value* result = llvm::UndefValue::get(arrayTy);
    for (unsigned i = 0; i < count; ++i) {
        llvm::Value* idx = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), i);
        llvm::Value* offset = builder->CreateMul(
            idx, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), elemBytes), "vec.toarr.off");
        llvm::Value* elemPtr = builder->CreateGEP(
            llvm::Type::getInt8Ty(*context), dataPtr, offset, "vec.toarr.elem");
        llvm::Value* loaded = builder->CreateLoad(elemTy, elemPtr, "vec.toarr.val");
        result = builder->CreateInsertValue(result, loaded, {i}, "vec.toarr.arr");
    }
    m_currentLLVMValue = result;
}

void LLVMCodegen::handleVecClear(vyb::ast::CallExpression* node, llvm::Value* vecPtr, llvm::Type* vecStructType) {
    if (node->arguments.size() != 0) {
        logError(node->loc, "Vec::clear expects no arguments");
        m_currentLLVMValue = nullptr;
        return;
    }

    // If the Vec holds String elements, each element's reference must be dropped
    // before the storage is logically emptied, or the buffers would leak.
    llvm::Type* elemType = nullptr;
    if (node->type) {
        elemType = codegenType(node->type.get());
    }
    if (elemType && isVybStringStructType(elemType)) {
        llvm::Value* dataFieldPtr = builder->CreateStructGEP(vecStructType, vecPtr, 0, "vec.clear.data_ptr");
        llvm::Value* sizeFieldPtrC = builder->CreateStructGEP(vecStructType, vecPtr, 1, "vec.clear.size_ptr");
        llvm::Value* dataPtr = builder->CreateLoad(llvm::PointerType::get(*context, 0), dataFieldPtr, "vec.clear.data");
        llvm::Value* curSize = builder->CreateLoad(llvm::Type::getInt64Ty(*context), sizeFieldPtrC, "vec.clear.size");
        releaseStringElements(dataPtr, curSize);
    }

    // Get pointer to size field and set to 0
    llvm::Value* sizeFieldPtr = builder->CreateStructGEP(vecStructType, vecPtr, 1, "vec.size_ptr");
    builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0), sizeFieldPtr);

    VYB_CDBG << "DEBUG: Vec::clear() called - size reset to 0" << std::endl;

    // Return void
    m_currentLLVMValue = nullptr;
}

void LLVMCodegen::handleVecIsEmpty(vyb::ast::CallExpression* node, llvm::Value* vecPtr, llvm::Type* vecStructType) {
    if (node->arguments.size() != 0) {
        logError(node->loc, "Vec::is_empty expects no arguments");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Get pointer to size field
    llvm::Value* sizeFieldPtr = builder->CreateStructGEP(vecStructType, vecPtr, 1, "vec.size_ptr");
    llvm::Value* currentSize = builder->CreateLoad(llvm::Type::getInt64Ty(*context), sizeFieldPtr, "vec.current_size");

    // Check if size == 0
    llvm::Value* isEmpty = builder->CreateICmpEQ(currentSize,
                                                llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0),
                                                "vec.is_empty");

    VYB_CDBG << "DEBUG: Vec::is_empty() called" << std::endl;

    // Return boolean result
    m_currentLLVMValue = isEmpty;
}

void LLVMCodegen::handleVecCapacity(vyb::ast::CallExpression* node, llvm::Value* vecPtr, llvm::Type* vecStructType) {
    if (node->arguments.size() != 0) {
        logError(node->loc, "Vec::capacity expects no arguments");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Get pointer to capacity field
    llvm::Value* capacityFieldPtr = builder->CreateStructGEP(vecStructType, vecPtr, 2, "vec.capacity_ptr");

    // Load and return current capacity
    m_currentLLVMValue = builder->CreateLoad(llvm::Type::getInt64Ty(*context), capacityFieldPtr, "vec.capacity");

    VYB_CDBG << "DEBUG: Vec::capacity() called" << std::endl;
}

void LLVMCodegen::handleVecConcat(vyb::ast::CallExpression* node, llvm::Value* vecPtr, llvm::Type* vecStructType) {
    if (node->arguments.size() != 1) {
        logError(node->loc, "Vec::concat expects exactly 1 argument (other Vec)");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Evaluate the other Vec argument
    node->arguments[0]->accept(*this);
    llvm::Value* otherVec = m_currentLLVMValue;
    if (!otherVec) {
        logError(node->loc, "Failed to evaluate Vec argument for Vec::concat");
        return;
    }

    VYB_CDBG << "DEBUG: Vec::concat() called - concatenating with another Vec" << std::endl;

    // The other Vec argument arrives as a struct value (e.g. loaded from a
    // variable); back it with a temporary alloca so both buffers can be read
    // through the { data, size, capacity } field GEPs below.
    if (!otherVec->getType()->isPointerTy()) {
        llvm::Value* tempAlloca = builder->CreateAlloca(otherVec->getType(), nullptr, "vec.concat.other");
        builder->CreateStore(otherVec, tempAlloca);
        otherVec = tempAlloca;
    }

    // Resolve the element type (and byte size) from the Vec<T> result type so
    // the combined buffer is laid out exactly like the source vectors.
    llvm::Type* elementLLVMType = nullptr;
    vyb::ast::TypeNode* elemNode = nullptr;
    if (auto* vt = dynamic_cast<ast::VecType*>(node->type.get())) {
        elemNode = vt->elementType.get();
    } else if (auto* tn = dynamic_cast<ast::TypeName*>(node->type.get())) {
        if (!tn->genericArgs.empty()) elemNode = tn->genericArgs[0].get();
    }
    if (elemNode) elementLLVMType = codegenType(elemNode);
    if (!elementLLVMType) elementLLVMType = llvm::Type::getInt64Ty(*context); // fallback
    llvm::DataLayout dataLayout(module.get());
    const uint64_t elementSizeBytes = dataLayout.getTypeAllocSize(elementLLVMType);

    // Load this Vec's data pointer and size.
    llvm::Value* thisDataPtr = builder->CreateLoad(
        llvm::PointerType::get(*context, 0),
        builder->CreateStructGEP(vecStructType, vecPtr, 0, "vec.concat.data_ptr"),
        "vec.concat.data");
    llvm::Value* thisSize = builder->CreateLoad(
        llvm::Type::getInt64Ty(*context),
        builder->CreateStructGEP(vecStructType, vecPtr, 1, "vec.concat.size_ptr"),
        "vec.concat.size");

    // Load the other Vec's data pointer and size.
    llvm::Value* otherDataPtr = builder->CreateLoad(
        llvm::PointerType::get(*context, 0),
        builder->CreateStructGEP(vecStructType, otherVec, 0, "vec.concat.other_data_ptr"),
        "vec.concat.other_data");
    llvm::Value* otherSize = builder->CreateLoad(
        llvm::Type::getInt64Ty(*context),
        builder->CreateStructGEP(vecStructType, otherVec, 1, "vec.concat.other_size_ptr"),
        "vec.concat.other_size");

    // A fresh combined vector sized exactly to hold both element ranges.
    llvm::Value* zero = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
    llvm::Value* elemSizeVal = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), elementSizeBytes);
    llvm::Value* newCap = builder->CreateAdd(thisSize, otherSize, "vec.concat.cap");
    llvm::Value* allocBytes = builder->CreateMul(newCap, elemSizeVal, "vec.concat.bytes");

    llvm::Function* currentFunc = builder->GetInsertBlock()->getParent();
    llvm::Function* mallocFunc = getOrCreateMallocFunction();
    llvm::Value* newDataPtr = builder->CreateCall(mallocFunc, {allocBytes}, "vec.concat.new_data");

    // Guarded memcpy so an empty side never reads from an uninitialized buffer.
    llvm::Value* hasThis = builder->CreateICmpSGT(thisSize, zero, "vec.concat.has_this");
    llvm::Value* hasOther = builder->CreateICmpSGT(otherSize, zero, "vec.concat.has_other");
    llvm::Value* hasData = builder->CreateOr(hasThis, hasOther, "vec.concat.has_data");

    llvm::BasicBlock* copyBB = llvm::BasicBlock::Create(*context, "vec.concat.copy", currentFunc);
    llvm::BasicBlock* copyThisBB = llvm::BasicBlock::Create(*context, "vec.concat.copy_this", currentFunc);
    llvm::BasicBlock* copyOtherBB = llvm::BasicBlock::Create(*context, "vec.concat.copy_other", currentFunc);
    llvm::BasicBlock* doneBB = llvm::BasicBlock::Create(*context, "vec.concat.done", currentFunc);
    builder->CreateCondBr(hasData, copyBB, doneBB);

    builder->SetInsertPoint(copyBB);
    llvm::Function* memcpyFunc = getOrCreateMemcpyFunction();
    llvm::Value* thisBytes = builder->CreateMul(thisSize, elemSizeVal, "vec.concat.this_bytes");
    llvm::Value* otherBytes = builder->CreateMul(otherSize, elemSizeVal, "vec.concat.other_bytes");
    builder->CreateCondBr(hasThis, copyThisBB, copyOtherBB);

    builder->SetInsertPoint(copyThisBB);
    builder->CreateCall(memcpyFunc, {newDataPtr, thisDataPtr, thisBytes});
    builder->CreateBr(copyOtherBB);

    builder->SetInsertPoint(copyOtherBB);
    llvm::Value* thisBytesPtr = builder->CreateGEP(llvm::Type::getInt8Ty(*context), newDataPtr, thisBytes, "vec.concat.other_dst");
    builder->CreateCall(memcpyFunc, {thisBytesPtr, otherDataPtr, otherBytes});
    builder->CreateBr(doneBB);

    builder->SetInsertPoint(doneBB);
    // The combined Vec is an independent holder of any String elements it now
    // references, so each retained element survives the original vectors' teardown.
    if (elementLLVMType && isVybStringStructType(elementLLVMType)) {
        retainStringElements(newDataPtr, newCap);
    }

    llvm::Value* newVec = llvm::UndefValue::get(vecStructType);
    newVec = builder->CreateInsertValue(newVec, newDataPtr, 0, "vec.concat.result.data");
    newVec = builder->CreateInsertValue(newVec, newCap, 1, "vec.concat.result.size");
    newVec = builder->CreateInsertValue(newVec, newCap, 2, "vec.concat.result.cap");
    m_currentLLVMValue = newVec;
}

void LLVMCodegen::handleVecContains(vyb::ast::CallExpression* node, llvm::Value* vecPtr, llvm::Type* vecStructType) {
    if (node->arguments.size() != 1) {
        logError(node->loc, "Vec::contains expects exactly 1 argument (value to search)");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Evaluate the search value argument
    node->arguments[0]->accept(*this);
    llvm::Value* searchValue = m_currentLLVMValue;
    if (!searchValue) {
        logError(node->loc, "Failed to evaluate search value for Vec::contains");
        return;
    }

    VYB_CDBG << "DEBUG: Vec::contains() called - searching for value" << std::endl;

    // Get the element type size (default to 8 bytes for i64)
    llvm::Type* elementLLVMType = llvm::Type::getInt64Ty(*context);
    uint64_t elementSizeBytes = 8;
    if (node->type) {
        // The contains() return type is Bool, not the element type
        // We need the element type from the Vec type context
    }
    // Try to infer element type from search value
    if (searchValue->getType()->isIntegerTy()) {
        // Ensure 64-bit for consistency
        if (searchValue->getType() != llvm::Type::getInt64Ty(*context)) {
            searchValue = builder->CreateSExtOrTrunc(searchValue, llvm::Type::getInt64Ty(*context), "contains.cast");
        }
        elementLLVMType = llvm::Type::getInt64Ty(*context);
        elementSizeBytes = 8;
    } else if (searchValue->getType()->isFloatingPointTy()) {
        elementLLVMType = searchValue->getType();
        llvm::DataLayout dl(module.get());
        elementSizeBytes = dl.getTypeAllocSize(elementLLVMType);
    } else if (searchValue->getType()->isPointerTy()) {
        elementLLVMType = searchValue->getType();
        elementSizeBytes = 8;
    }

    // Get pointers to struct fields
    llvm::Value* dataFieldPtr = builder->CreateStructGEP(vecStructType, vecPtr, 0, "vec.data_ptr");
    llvm::Value* sizeFieldPtr = builder->CreateStructGEP(vecStructType, vecPtr, 1, "vec.size_ptr");
    llvm::Value* dataPtr = builder->CreateLoad(llvm::PointerType::get(*context, 0), dataFieldPtr, "vec.data");
    llvm::Value* vecSize = builder->CreateLoad(llvm::Type::getInt64Ty(*context), sizeFieldPtr, "vec.size");

    // Generate loop: for i in 0..size, check if element[i] == searchValue
    llvm::Function* currentFunc = builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* loopHeader = llvm::BasicBlock::Create(*context, "contains.loop", currentFunc);
    llvm::BasicBlock* loopBody   = llvm::BasicBlock::Create(*context, "contains.body", currentFunc);
    llvm::BasicBlock* loopNext   = llvm::BasicBlock::Create(*context, "contains.next", currentFunc);
    llvm::BasicBlock* loopEnd    = llvm::BasicBlock::Create(*context, "contains.end", currentFunc);

    // Save entry block to add incoming values to PHI nodes
    llvm::BasicBlock* entryBlock = builder->GetInsertBlock();

    // Start loop
    builder->CreateBr(loopHeader);

    // Loop header: i = PHI(0, i+1)
    builder->SetInsertPoint(loopHeader);
    llvm::PHINode* indexPhi = builder->CreatePHI(llvm::Type::getInt64Ty(*context), 2, "contains.i");
    indexPhi->addIncoming(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0), entryBlock);

    // Check: i < size
    llvm::Value* cond = builder->CreateICmpULT(indexPhi, vecSize, "contains.cond");
    builder->CreateCondBr(cond, loopBody, loopEnd);

    // Loop body: load element and compare
    builder->SetInsertPoint(loopBody);
    llvm::Value* elemSize = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), elementSizeBytes);
    llvm::Value* offset = builder->CreateMul(indexPhi, elemSize, "contains.offset");
    llvm::Value* elemPtr = builder->CreateGEP(llvm::Type::getInt8Ty(*context), dataPtr, offset, "contains.elemptr");
    llvm::Value* elemVal = builder->CreateLoad(elementLLVMType, elemPtr, "contains.elem");

    // Compare element with searchValue
    llvm::Value* isEqual;
    if (elementLLVMType->isIntegerTy()) {
        isEqual = builder->CreateICmpEQ(elemVal, searchValue, "contains.eq");
    } else if (elementLLVMType->isFloatingPointTy()) {
        isEqual = builder->CreateFCmpOEQ(elemVal, searchValue, "contains.eq");
    } else {
        // Default: integer compare (for pointers, compare addresses)
        isEqual = builder->CreateICmpEQ(
            builder->CreatePtrToInt(elemVal, llvm::Type::getInt64Ty(*context)),
            builder->CreatePtrToInt(searchValue, llvm::Type::getInt64Ty(*context)),
            "contains.eq");
    }
    builder->CreateCondBr(isEqual, loopEnd, loopNext);

    // Loop next: i++
    builder->SetInsertPoint(loopNext);
    llvm::Value* nextIndex = builder->CreateAdd(indexPhi,
        llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 1), "contains.next_i");
    indexPhi->addIncoming(nextIndex, loopNext);
    builder->CreateBr(loopHeader);

    // End block: PHI result
    builder->SetInsertPoint(loopEnd);
    llvm::PHINode* resultPhi = builder->CreatePHI(llvm::Type::getInt1Ty(*context), 2, "contains.result");
    resultPhi->addIncoming(llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), 0), loopHeader); // false: loop ended
    resultPhi->addIncoming(llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), 1), loopBody);   // true: found

    m_currentLLVMValue = resultPhi;
}

void LLVMCodegen::handleVecRemoveAt(vyb::ast::CallExpression* node, llvm::Value* vecPtr, llvm::Type* vecStructType) {
    if (node->arguments.size() != 1) {
        logError(node->loc, "Vec::remove_at expects exactly 1 argument (index)");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Evaluate the index argument
    node->arguments[0]->accept(*this);
    llvm::Value* indexToRemove = m_currentLLVMValue;
    if (!indexToRemove) {
        logError(node->loc, "Failed to evaluate index for Vec::remove_at");
        return;
    }

    // Get element type from the CallExpression's type (return type)
    llvm::Type* elementLLVMType = nullptr;
    uint64_t elementSizeBytes = 8; // Default

    if (node->type) {
        elementLLVMType = codegenType(node->type.get());
        if (elementLLVMType) {
            llvm::DataLayout dataLayout(module.get());
            elementSizeBytes = dataLayout.getTypeAllocSize(elementLLVMType);
        }
    }

    if (!elementLLVMType) {
        elementLLVMType = llvm::Type::getInt64Ty(*context);
        elementSizeBytes = 8;
    }

    // Get pointers to struct fields
    llvm::Value* dataFieldPtr = builder->CreateStructGEP(vecStructType, vecPtr, 0, "vec.data_ptr");
    llvm::Value* sizeFieldPtr = builder->CreateStructGEP(vecStructType, vecPtr, 1, "vec.size_ptr");

    // Load current data pointer and size
    llvm::Value* dataPtr = builder->CreateLoad(llvm::PointerType::get(*context, 0), dataFieldPtr, "vec.data");
    llvm::Value* currentSize = builder->CreateLoad(llvm::Type::getInt64Ty(*context), sizeFieldPtr, "vec.size");

    // Bounds check: index < size
    llvm::Value* indexInBounds = builder->CreateICmpULT(indexToRemove, currentSize, "vec.index_in_bounds");

    llvm::BasicBlock* removeBlock = llvm::BasicBlock::Create(*context, "vec.remove_valid", builder->GetInsertBlock()->getParent());
    llvm::BasicBlock* mergeBlock = llvm::BasicBlock::Create(*context, "vec.remove_merge", builder->GetInsertBlock()->getParent());

    // The block that takes the out-of-bounds edge into the merge PHI.
    llvm::BasicBlock* boundsCheckBlock = builder->GetInsertBlock();
    builder->CreateCondBr(indexInBounds, removeBlock, mergeBlock);

    // Remove block - valid index
    builder->SetInsertPoint(removeBlock);

    // Calculate offset to the element being removed
    llvm::Value* elementSize = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), elementSizeBytes);
    llvm::Value* removeOffset = builder->CreateMul(indexToRemove, elementSize, "vec.remove_offset");
    llvm::Value* removePtr = builder->CreateGEP(llvm::Type::getInt8Ty(*context), dataPtr, removeOffset, "vec.remove_ptr");

    // Load the element being removed (for return value)
    llvm::Value* removedElement;
    if (elementLLVMType->isStructTy()) {
        removedElement = builder->CreateLoad(elementLLVMType, removePtr, "vec.removed_element");
    } else {
        removedElement = builder->CreateLoad(elementLLVMType, removePtr, "vec.removed_element");
    }

    // Shift elements after the removed index down by one
    // Elements to shift: size - index - 1
    llvm::Value* elementsAfter = builder->CreateSub(currentSize,
        builder->CreateAdd(indexToRemove, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 1)),
        "vec.elements_after");

    // Check if there are elements to shift
    llvm::Value* hasElementsToShift = builder->CreateICmpUGT(elementsAfter,
        llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0), "vec.has_elements_to_shift");

    llvm::BasicBlock* shiftBlock = llvm::BasicBlock::Create(*context, "vec.shift", builder->GetInsertBlock()->getParent());
    llvm::BasicBlock* noShiftBlock = llvm::BasicBlock::Create(*context, "vec.no_shift", builder->GetInsertBlock()->getParent());

    builder->CreateCondBr(hasElementsToShift, shiftBlock, noShiftBlock);

    // Shift block - use memmove to shift elements
    builder->SetInsertPoint(shiftBlock);

    llvm::Value* srcOffset = builder->CreateMul(
        builder->CreateAdd(indexToRemove, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 1)),
        elementSize, "vec.src_offset");
    llvm::Value* srcPtr = builder->CreateGEP(llvm::Type::getInt8Ty(*context), dataPtr, srcOffset, "vec.src_ptr");
    llvm::Value* bytesToMove = builder->CreateMul(elementsAfter, elementSize, "vec.bytes_to_move");

    // Use memmove (safer than memcpy for overlapping regions)
    llvm::FunctionType* memmoveType = llvm::FunctionType::get(
        llvm::PointerType::get(*context, 0),
        {llvm::PointerType::get(*context, 0), llvm::PointerType::get(*context, 0), llvm::Type::getInt64Ty(*context)},
        false
    );
    llvm::Function* memmoveFunc = module->getFunction("memmove");
    if (!memmoveFunc) {
        memmoveFunc = llvm::Function::Create(memmoveType, llvm::Function::ExternalLinkage, "memmove", module.get());
    }
    builder->CreateCall(memmoveFunc, {removePtr, srcPtr, bytesToMove});
    builder->CreateBr(noShiftBlock);

    // No shift block - just update size
    builder->SetInsertPoint(noShiftBlock);
    llvm::Value* newSize = builder->CreateSub(currentSize,
        llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 1), "vec.new_size");
    builder->CreateStore(newSize, sizeFieldPtr);
    builder->CreateBr(mergeBlock);

    // Merge block
    builder->SetInsertPoint(mergeBlock);
    llvm::PHINode* resultPhi = builder->CreatePHI(elementLLVMType, 2, "vec.remove_result");
    resultPhi->addIncoming(removedElement, noShiftBlock);

    // For out of bounds, return zero/default value
    llvm::Value* defaultValue;
    if (elementLLVMType->isIntegerTy()) {
        defaultValue = llvm::ConstantInt::get(elementLLVMType, 0);
    } else if (elementLLVMType->isFloatingPointTy()) {
        defaultValue = llvm::ConstantFP::get(elementLLVMType, 0.0);
    } else if (elementLLVMType->isStructTy()) {
        defaultValue = llvm::ConstantAggregateZero::get(elementLLVMType);
    } else {
        defaultValue = llvm::Constant::getNullValue(elementLLVMType);
    }
    resultPhi->addIncoming(defaultValue, boundsCheckBlock);

    VYB_CDBG << "DEBUG: Vec::remove_at() called - element removed and Vec compacted" << std::endl;

    m_currentLLVMValue = resultPhi;
}

void LLVMCodegen::handleVecGetArray(vyb::ast::CallExpression* node, llvm::Value* vecPtr, llvm::Type* vecStructType) {
    if (node->arguments.size() != 1) {
        logError(node->loc, "Vec::get_array expects exactly 1 argument (pre-allocated array)");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Evaluate the pre-allocated array argument
    node->arguments[0]->accept(*this);
    llvm::Value* arrayPtr = m_currentLLVMValue;
    if (!arrayPtr) {
        logError(node->loc, "Failed to evaluate array argument for Vec::get_array");
        return;
    }

    // Get Vec size for bounds checking
    llvm::Value* sizeFieldPtr = builder->CreateStructGEP(vecStructType, vecPtr, 1, "vec.size_ptr");
    llvm::Value* vecSize = builder->CreateLoad(llvm::Type::getInt64Ty(*context), sizeFieldPtr, "vec.size");

    VYB_CDBG << "DEBUG: Vec::get_array() called - copying to pre-allocated array" << std::endl;

    // In a full implementation, this would:
    // 1. Check array size compatibility
    // 2. Copy elements from Vec storage to the provided array
    // 3. Handle bounds checking and partial copies
    // 4. Return number of elements copied

    // For now, return the number of elements that would be copied
    m_currentLLVMValue = vecSize;
}

void LLVMCodegen::handleVecGetVec(vyb::ast::CallExpression* node, llvm::Value* vecPtr, llvm::Type* vecStructType) {
    if (node->arguments.size() != 1) {
        logError(node->loc, "Vec::get_vec expects exactly 1 argument (target Vec)");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Evaluate the target Vec argument
    node->arguments[0]->accept(*this);
    llvm::Value* targetVecPtr = m_currentLLVMValue;
    if (!targetVecPtr) {
        logError(node->loc, "Failed to evaluate target Vec argument for Vec::get_vec");
        return;
    }

    // Get source Vec size
    llvm::Value* srcSizeFieldPtr = builder->CreateStructGEP(vecStructType, vecPtr, 1, "src_vec.size_ptr");
    llvm::Value* srcSize = builder->CreateLoad(llvm::Type::getInt64Ty(*context), srcSizeFieldPtr, "src_vec.size");

    // Get source Vec data pointer
    llvm::Value* srcDataFieldPtr = builder->CreateStructGEP(vecStructType, vecPtr, 0, "src_vec.data_ptr");
    llvm::Value* srcDataPtr = builder->CreateLoad(llvm::PointerType::get(*context, 0), srcDataFieldPtr, "src_vec.data");

    // Get target Vec pointers
    llvm::Value* targetSizeFieldPtr = builder->CreateStructGEP(vecStructType, targetVecPtr, 1, "target_vec.size_ptr");
    llvm::Value* targetCapacityFieldPtr = builder->CreateStructGEP(vecStructType, targetVecPtr, 2, "target_vec.capacity_ptr");
    llvm::Value* targetDataFieldPtr = builder->CreateStructGEP(vecStructType, targetVecPtr, 0, "target_vec.data_ptr");

    VYB_CDBG << "DEBUG: Vec::get_vec() called - extracting contents from 'their Vec' to target Vec" << std::endl;

    // In a full implementation, this would:
    // 1. Allocate new storage in target Vec if needed
    // 2. Copy all elements from source Vec to target Vec
    // 3. Update target Vec's size and potentially capacity
    // 4. Clear the source Vec (since it's a 'their' Vec being consumed)
    // 5. Return number of elements transferred

    // For now, simulate the transfer:
    // Set target size to source size
    builder->CreateStore(srcSize, targetSizeFieldPtr);

    // In a real implementation, we'd copy the data and potentially free source
    // For demonstration, just return the number of elements that would be transferred
    m_currentLLVMValue = srcSize;
}

void LLVMCodegen::handleVecResize(vyb::ast::CallExpression* node, llvm::Value* vecPtr, llvm::Type* vecStructType) {
    if (node->arguments.size() != 1) {
        logError(node->loc, "Vec::resize expects exactly 1 argument (new_capacity)");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Evaluate the new capacity argument
    node->arguments[0]->accept(*this);
    llvm::Value* newCapacity = m_currentLLVMValue;
    if (!newCapacity) {
        logError(node->loc, "Failed to evaluate new_capacity for Vec::resize");
        return;
    }

    // Get element type - try to infer from context or default to i64
    llvm::Type* elementType = llvm::Type::getInt64Ty(*context);
    uint64_t elementSizeBytes = 8;

    // Try to get actual element type from Vec type parameter if available
    // For now, use default since we don't have full type parameter tracking yet
    llvm::DataLayout dataLayout(module.get());

    // Get pointers to struct fields
    llvm::Value* dataFieldPtr = builder->CreateStructGEP(vecStructType, vecPtr, 0, "vec.data_ptr");
    llvm::Value* sizeFieldPtr = builder->CreateStructGEP(vecStructType, vecPtr, 1, "vec.size_ptr");
    llvm::Value* capFieldPtr = builder->CreateStructGEP(vecStructType, vecPtr, 2, "vec.cap_ptr");

    // Load current values
    llvm::Value* oldDataPtr = builder->CreateLoad(llvm::PointerType::get(*context, 0), dataFieldPtr, "vec.old_data");
    llvm::Value* currentSize = builder->CreateLoad(llvm::Type::getInt64Ty(*context), sizeFieldPtr, "vec.size");
    llvm::Value* currentCap = builder->CreateLoad(llvm::Type::getInt64Ty(*context), capFieldPtr, "vec.old_cap");

    // Check if newCapacity > 0
    llvm::Value* hasCapacity = builder->CreateICmpUGT(newCapacity, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0), "vec.has_capacity");

    llvm::BasicBlock* allocBlock = llvm::BasicBlock::Create(*context, "vec.resize_alloc", builder->GetInsertBlock()->getParent());
    llvm::BasicBlock* freeBlock = llvm::BasicBlock::Create(*context, "vec.resize_free", builder->GetInsertBlock()->getParent());
    llvm::BasicBlock* mergeBlock = llvm::BasicBlock::Create(*context, "vec.resize_merge", builder->GetInsertBlock()->getParent());

    builder->CreateCondBr(hasCapacity, allocBlock, freeBlock);

    // Alloc block - allocate new storage
    builder->SetInsertPoint(allocBlock);

    llvm::Value* elementSize = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), elementSizeBytes);
    llvm::Value* allocSize = builder->CreateMul(newCapacity, elementSize, "vec.alloc_size");

    // Call malloc
    llvm::FunctionType* mallocType = llvm::FunctionType::get(
        llvm::PointerType::get(*context, 0),
        {llvm::Type::getInt64Ty(*context)},
        false
    );
    llvm::Function* mallocFunc = module->getFunction("malloc");
    if (!mallocFunc) {
        mallocFunc = llvm::Function::Create(mallocType, llvm::Function::ExternalLinkage, "malloc", module.get());
    }
    llvm::Value* newDataPtr = builder->CreateCall(mallocFunc, {allocSize}, "vec.new_data");

    // Copy existing data if there is any (copy min(size, newCapacity) elements)
    llvm::Value* elementsToCopy = builder->CreateSelect(
        builder->CreateICmpULT(currentSize, newCapacity),
        currentSize,
        newCapacity,
        "vec.elements_to_copy"
    );

    llvm::Value* hasOldData = builder->CreateICmpNE(oldDataPtr,
        llvm::ConstantPointerNull::get(llvm::PointerType::get(*context, 0)), "vec.has_old_data");
    llvm::Value* hasCopyData = builder->CreateAnd(hasOldData,
        builder->CreateICmpUGT(elementsToCopy, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0)),
        "vec.has_copy_data");

    llvm::BasicBlock* copyBlock = llvm::BasicBlock::Create(*context, "vec.resize_copy", builder->GetInsertBlock()->getParent());
    llvm::BasicBlock* noCopyBlock = llvm::BasicBlock::Create(*context, "vec.resize_no_copy", builder->GetInsertBlock()->getParent());

    builder->CreateCondBr(hasCopyData, copyBlock, noCopyBlock);

    // Copy block
    builder->SetInsertPoint(copyBlock);
    llvm::Value* copySize = builder->CreateMul(elementsToCopy, elementSize, "vec.copy_size");
    llvm::FunctionType* memcpyType = llvm::FunctionType::get(
        llvm::PointerType::get(*context, 0),
        {llvm::PointerType::get(*context, 0), llvm::PointerType::get(*context, 0), llvm::Type::getInt64Ty(*context)},
        false
    );
    llvm::Function* memcpyFunc = module->getFunction("memcpy");
    if (!memcpyFunc) {
        memcpyFunc = llvm::Function::Create(memcpyType, llvm::Function::ExternalLinkage, "memcpy", module.get());
    }
    builder->CreateCall(memcpyFunc, {newDataPtr, oldDataPtr, copySize});
    builder->CreateBr(noCopyBlock);

    // No copy block - free old data and update Vec
    builder->SetInsertPoint(noCopyBlock);

    // Free old data if it exists
    llvm::BasicBlock* freeOldBlock = llvm::BasicBlock::Create(*context, "vec.resize_free_old", builder->GetInsertBlock()->getParent());
    llvm::BasicBlock* noFreeBlock = llvm::BasicBlock::Create(*context, "vec.resize_no_free", builder->GetInsertBlock()->getParent());

    builder->CreateCondBr(hasOldData, freeOldBlock, noFreeBlock);

    builder->SetInsertPoint(freeOldBlock);
    llvm::FunctionType* freeType = llvm::FunctionType::get(
        llvm::Type::getVoidTy(*context),
        {llvm::PointerType::get(*context, 0)},
        false
    );
    llvm::Function* freeFunc = module->getFunction("free");
    if (!freeFunc) {
        freeFunc = llvm::Function::Create(freeType, llvm::Function::ExternalLinkage, "free", module.get());
    }
    builder->CreateCall(freeFunc, {oldDataPtr});
    builder->CreateBr(noFreeBlock);

    builder->SetInsertPoint(noFreeBlock);
    builder->CreateStore(newDataPtr, dataFieldPtr);
    builder->CreateStore(newCapacity, capFieldPtr);
    // Update size to min(currentSize, newCapacity)
    builder->CreateStore(elementsToCopy, sizeFieldPtr);
    builder->CreateBr(mergeBlock);

    // Free block - newCapacity is 0, free everything
    builder->SetInsertPoint(freeBlock);
    llvm::BasicBlock* freeClearBlock = llvm::BasicBlock::Create(*context, "vec.resize_clear", builder->GetInsertBlock()->getParent());
    llvm::BasicBlock* noFreeClearBlock = llvm::BasicBlock::Create(*context, "vec.resize_no_clear", builder->GetInsertBlock()->getParent());

    llvm::Value* hasDataToFree = builder->CreateICmpNE(oldDataPtr,
        llvm::ConstantPointerNull::get(llvm::PointerType::get(*context, 0)), "vec.has_data_to_free");
    builder->CreateCondBr(hasDataToFree, freeClearBlock, noFreeClearBlock);

    builder->SetInsertPoint(freeClearBlock);
    llvm::Function* freeFunc2 = module->getFunction("free");
    if (!freeFunc2) {
        llvm::FunctionType* freeType2 = llvm::FunctionType::get(
            llvm::Type::getVoidTy(*context),
            {llvm::PointerType::get(*context, 0)},
            false
        );
        freeFunc2 = llvm::Function::Create(freeType2, llvm::Function::ExternalLinkage, "free", module.get());
    }
    builder->CreateCall(freeFunc2, {oldDataPtr});
    builder->CreateBr(noFreeClearBlock);

    builder->SetInsertPoint(noFreeClearBlock);
    builder->CreateStore(llvm::ConstantPointerNull::get(llvm::PointerType::get(*context, 0)), dataFieldPtr);
    builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0), sizeFieldPtr);
    builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0), capFieldPtr);
    builder->CreateBr(mergeBlock);

    // Merge block
    builder->SetInsertPoint(mergeBlock);

    VYB_CDBG << "DEBUG: Vec::resize() called - Vec resized" << std::endl;

    // Return void
    m_currentLLVMValue = nullptr;
}

void LLVMCodegen::handleVecMethodOnValue(vyb::ast::CallExpression* node, llvm::Value* vecValue, const std::string& methodName, vyb::ast::Expression* objectExpr) {
    // Handle Vec method calls when we have the Vec value directly (not just a name)
    // This is used for calls like tree.nodes.push() where tree.nodes is a member expression

    llvm::Value* vecPtr = vecValue;

    // Check if the value is a pointer
    if (!vecPtr->getType()->isPointerTy()) {
        // If it's a value (not a pointer), we need to create a temporary alloca and store it
        llvm::Type* vecType = vecPtr->getType();
        llvm::Value* tempAlloca = builder->CreateAlloca(vecType, nullptr, "vec.temp");
        builder->CreateStore(vecPtr, tempAlloca);
        vecPtr = tempAlloca;
    }

    // Define Vec struct type: { ptr, i64, i64 }
    std::vector<llvm::Type*> vecFields = {
        llvm::PointerType::get(*context, 0), // ptr to elements
        llvm::Type::getInt64Ty(*context),    // size
        llvm::Type::getInt64Ty(*context)     // capacity
    };
    llvm::Type* vecStructType = llvm::StructType::get(*context, vecFields, false);

    // Dispatch to the appropriate handler
    if (methodName == "push") {
        handleVecPush(node, vecPtr, vecStructType);
    } else if (methodName == "pop") {
        handleVecPop(node, vecPtr, vecStructType);
    } else if (methodName == "len") {
        handleVecLen(node, vecPtr, vecStructType);
    } else if (methodName == "get") {
        handleVecGet(node, vecPtr, vecStructType);
    } else if (methodName == "push_array") {
        handleVecPushArray(node, vecPtr, vecStructType);
    } else if (methodName == "to_array") {
        handleVecToArray(node, vecPtr, vecStructType);
    } else if (methodName == "clear") {
        handleVecClear(node, vecPtr, vecStructType);
    } else if (methodName == "is_empty") {
        handleVecIsEmpty(node, vecPtr, vecStructType);
    } else if (methodName == "capacity") {
        handleVecCapacity(node, vecPtr, vecStructType);
    } else if (methodName == "concat") {
        handleVecConcat(node, vecPtr, vecStructType);
    } else if (methodName == "set") {
        handleVecSet(node, vecPtr, vecStructType);
    } else if (methodName == "contains") {
        handleVecContains(node, vecPtr, vecStructType);
    } else if (methodName == "remove_at" || methodName == "remove") {
        handleVecRemoveAt(node, vecPtr, vecStructType);
    } else if (methodName == "resize") {
        handleVecResize(node, vecPtr, vecStructType);
    } else if (methodName == "get_array") {
        handleVecGetArray(node, vecPtr, vecStructType);
    } else if (methodName == "get_vec") {
        handleVecGetVec(node, vecPtr, vecStructType);
    } else {
        logError(node->loc, "Unknown Vec method: " + methodName);
        m_currentLLVMValue = nullptr;
    }
}

} // namespace vyb
