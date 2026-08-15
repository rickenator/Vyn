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
        builder->CreateCall(memcpyFunc2, {elementPtr, srcPtr, elementSize2});
    } else {
        // For primitives, direct store
        builder->CreateStore(valueToAdd, elementPtr);
    }

    // Increment size
    llvm::Value* newSize = builder->CreateAdd(reloadedSize, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 1), "vec.new_size");
    builder->CreateStore(newSize, sizeFieldPtr);

    VYB_CDBG << "DEBUG: Vec::push() called - element stored, returning Vec for chaining" << std::endl;

    // Return the Vec pointer to enable method chaining
    m_currentLLVMValue = vecPtr;
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

    // TODO: Add bounds checking - for now assume index is valid

    // Calculate offset: data_ptr + (index * element_size)
    llvm::Value* elementSize = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), elementSizeBytes);
    llvm::Value* offset = builder->CreateMul(index, elementSize, "vec.offset");
    llvm::Value* elementPtr = builder->CreateGEP(llvm::Type::getInt8Ty(*context), dataPtr, offset, "vec.element_ptr");

    // Load the element value based on type
    if (elementLLVMType->isStructTy()) {
        // For structs, load the entire struct value (will be a copy)
        llvm::Value* structValue = builder->CreateLoad(elementLLVMType, elementPtr, "vec.element_struct");
        m_currentLLVMValue = structValue;

        VYB_CDBG << "DEBUG: Vec::get() called - returning struct value" << std::endl;
    } else {
        // For primitives, load the value directly
        llvm::Value* element = builder->CreateLoad(elementLLVMType, elementPtr, "vec.element");
        m_currentLLVMValue = element;

        VYB_CDBG << "DEBUG: Vec::get() called - element retrieved" << std::endl;
    }
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

    // Determine the element type from the value argument so element size is right.
    llvm::Type* elementLLVMType = llvm::Type::getInt64Ty(*context);
    uint64_t elementSizeBytes = 8;
    if (node->arguments[1]->type) {
        llvm::Type* t = codegenType(node->arguments[1]->type.get());
        if (t) {
            elementLLVMType = t;
            llvm::DataLayout dataLayout(module.get());
            elementSizeBytes = dataLayout.getTypeAllocSize(elementLLVMType);
        }
    }

    llvm::Value* dataPtr = builder->CreateLoad(
        llvm::PointerType::get(*context, 0),
        builder->CreateStructGEP(vecStructType, vecPtr, 0, "vec.set.data_ptr"),
        "vec.set.data");

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
    }

    builder->CreateStore(value, elementPtr);

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

void LLVMCodegen::handleVecToArray(vyb::ast::CallExpression* node, llvm::Value* vecPtr, llvm::Type* vecStructType) {
    if (node->arguments.size() != 1) {
        logError(node->loc, "Vec::to_array expects exactly 1 argument (array size)");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Evaluate the size argument
    node->arguments[0]->accept(*this);
    llvm::Value* sizeValue = m_currentLLVMValue;
    if (!sizeValue) {
        logError(node->loc, "Failed to evaluate size argument for Vec::to_array");
        return;
    }

    VYB_CDBG << "DEBUG: Vec::to_array() called - converting to fixed array" << std::endl;

    // For now, return a placeholder array
    // In full implementation, would create array from Vec elements
    m_currentLLVMValue = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
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

    // For now, placeholder implementation
    // Return the original Vec reference
    m_currentLLVMValue = vecPtr;
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

    // Create a phi node for the return value
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
    resultPhi->addIncoming(defaultValue, builder->GetInsertBlock()->getUniquePredecessor());

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
