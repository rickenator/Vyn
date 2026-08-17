// SPDX-License-Identifier: Apache-2.0

#include "vyb/vre/llvm/codegen.hpp"
#include "vyb/parser/ast.hpp"
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/DerivedTypes.h>
#include <iostream>

namespace vyb {

// String struct: { ptr: *i8, len: i64 }
// This represents a Vyb String type as a fat pointer with length

void LLVMCodegen::handleStringMethod(vyb::ast::CallExpression* node, const std::string& objectName, const std::string& methodName) {
    // Look up the String object in namedValues
    auto it = namedValues.find(objectName);
    if (it == namedValues.end()) {
        logError(node->loc, "Undefined String variable: " + objectName);
        m_currentLLVMValue = nullptr;
        return;
    }

    llvm::Value* strPtr = it->second;

    // Ensure it's a pointer type
    if (!strPtr->getType()->isPointerTy()) {
        logError(node->loc, "String variable is not a pointer type");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Define String struct type: { ptr: *i8, len: i64 }
    std::vector<llvm::Type*> strFields = {
        llvm::PointerType::get(*context, 0), // ptr to bytes
        llvm::Type::getInt64Ty(*context)     // length
    };
    llvm::Type* strStructType = llvm::StructType::get(*context, strFields, false);

    dispatchStringMethod(node, strPtr, strStructType, methodName);
}

// String method call where the receiver is an already-materialized pointer to the
// { ptr, len } String struct (not looked up by variable name). Used for literal
// and computed/value receivers that have no namedValues entry.
void LLVMCodegen::handleStringMethodOnValue(vyb::ast::CallExpression* node, llvm::Value* strPtr, const std::string& methodName) {
    if (!strPtr || !strPtr->getType()->isPointerTy()) {
        logError(node->loc, "String receiver is not a pointer type");
        m_currentLLVMValue = nullptr;
        return;
    }
    std::vector<llvm::Type*> strFields = {
        llvm::PointerType::get(*context, 0),
        llvm::Type::getInt64Ty(*context)
    };
    llvm::Type* strStructType = llvm::StructType::get(*context, strFields, false);
    dispatchStringMethod(node, strPtr, strStructType, methodName);
}

void LLVMCodegen::dispatchStringMethod(vyb::ast::CallExpression* node, llvm::Value* strPtr, llvm::Type* strStructType, const std::string& methodName) {
    if (methodName == "len" || methodName == "length") {
        handleStringLen(node, strPtr, strStructType);
    } else if (methodName == "concat") {
        handleStringConcat(node, strPtr, strStructType);
    } else if (methodName == "substring" || methodName == "substr") {
        handleStringSubstring(node, strPtr, strStructType);
    } else if (methodName == "char_at") {
        handleStringCharAt(node, strPtr, strStructType);
    } else if (methodName == "to_bytes") {
        handleStringToBytes(node, strPtr, strStructType);
    } else if (methodName == "from_bytes") {
        handleStringFromBytes(node, strPtr, strStructType);
    } else if (methodName == "starts_with") {
        handleStringStartsWith(node, strPtr, strStructType);
    } else if (methodName == "ends_with") {
        handleStringEndsWith(node, strPtr, strStructType);
    } else if (methodName == "contains") {
        handleStringContains(node, strPtr, strStructType);
    } else if (methodName == "to_upper") {
        handleStringToUpper(node, strPtr, strStructType);
    } else if (methodName == "to_lower") {
        handleStringToLower(node, strPtr, strStructType);
    } else if (methodName == "trim" || methodName == "strip") {
        handleStringTrim(node, strPtr, strStructType);
    } else if (methodName == "replace") {
        handleStringReplace(node, strPtr, strStructType);
    } else if (methodName == "format") {
        handleStringFormat(node, strPtr, strStructType);
    } else {
        logError(node->loc, "Unknown String method: " + methodName);
        m_currentLLVMValue = nullptr;
    }
}

void LLVMCodegen::handleStringLen(vyb::ast::CallExpression* node, llvm::Value* strPtr, llvm::Type* strStructType) {
    if (node->arguments.size() != 0) {
        logError(node->loc, "String::len expects no arguments");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Get pointer to length field (index 1)
    llvm::Value* lenFieldPtr = builder->CreateStructGEP(strStructType, strPtr, 1, "str.len_ptr");

    // Load and return length
    m_currentLLVMValue = builder->CreateLoad(llvm::Type::getInt64Ty(*context), lenFieldPtr, "str.len");

    VYB_CDBG << "DEBUG: String::len() called" << std::endl;
}

void LLVMCodegen::handleStringConcat(vyb::ast::CallExpression* node, llvm::Value* strPtr, llvm::Type* strStructType) {
    if (node->arguments.size() != 1) {
        logError(node->loc, "String::concat expects exactly 1 argument (other String)");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Evaluate the other string argument
    node->arguments[0]->accept(*this);
    llvm::Value* otherStr = m_currentLLVMValue;
    if (!otherStr) {
        logError(node->loc, "Failed to evaluate argument for String::concat");
        return;
    }

    // Get fields from first string
    llvm::Value* str1DataPtr = builder->CreateStructGEP(strStructType, strPtr, 0, "str1.data_ptr");
    llvm::Value* str1LenPtr = builder->CreateStructGEP(strStructType, strPtr, 1, "str1.len_ptr");
    llvm::Value* str1Data = builder->CreateLoad(llvm::PointerType::get(*context, 0), str1DataPtr, "str1.data");
    llvm::Value* str1Len = builder->CreateLoad(llvm::Type::getInt64Ty(*context), str1LenPtr, "str1.len");

    // Get fields from second string (otherStr should be a String struct value or pointer)
    llvm::Value* str2DataPtr;
    llvm::Value* str2LenPtr;
    llvm::Value* str2Data;
    llvm::Value* str2Len;

    if (otherStr->getType()->isPointerTy()) {
        str2DataPtr = builder->CreateStructGEP(strStructType, otherStr, 0, "str2.data_ptr");
        str2LenPtr = builder->CreateStructGEP(strStructType, otherStr, 1, "str2.len_ptr");
        str2Data = builder->CreateLoad(llvm::PointerType::get(*context, 0), str2DataPtr, "str2.data");
        str2Len = builder->CreateLoad(llvm::Type::getInt64Ty(*context), str2LenPtr, "str2.len");
    } else if (otherStr->getType()->isStructTy()) {
        // Extract values from struct
        str2Data = builder->CreateExtractValue(otherStr, 0, "str2.data");
        str2Len = builder->CreateExtractValue(otherStr, 1, "str2.len");
    } else {
        logError(node->loc, "Invalid type for String::concat argument");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Calculate new length
    llvm::Value* newLen = builder->CreateAdd(str1Len, str2Len, "str.new_len");

    // Allocate new buffer (+1 for null terminator)
    llvm::Value* allocSize = builder->CreateAdd(newLen, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 1), "str.alloc_size");

    llvm::FunctionType* mallocType = llvm::FunctionType::get(
        llvm::PointerType::get(*context, 0),
        {llvm::Type::getInt64Ty(*context)},
        false
    );
    llvm::Function* mallocFunc = module->getFunction("malloc");
    if (!mallocFunc) {
        mallocFunc = llvm::Function::Create(mallocType, llvm::Function::ExternalLinkage, "malloc", module.get());
    }
    llvm::Value* newData = builder->CreateCall(mallocFunc, {allocSize}, "str.new_data");
    builder->CreateCall(getOrCreateVybStringRegisterFunction(), {newData});

    // Copy first string
    llvm::FunctionType* memcpyType = llvm::FunctionType::get(
        llvm::PointerType::get(*context, 0),
        {llvm::PointerType::get(*context, 0), llvm::PointerType::get(*context, 0), llvm::Type::getInt64Ty(*context)},
        false
    );
    llvm::Function* memcpyFunc = module->getFunction("memcpy");
    if (!memcpyFunc) {
        memcpyFunc = llvm::Function::Create(memcpyType, llvm::Function::ExternalLinkage, "memcpy", module.get());
    }
    builder->CreateCall(memcpyFunc, {newData, str1Data, str1Len});

    // Copy second string at offset
    llvm::Value* offset = builder->CreateGEP(llvm::Type::getInt8Ty(*context), newData, str1Len, "str.offset");
    builder->CreateCall(memcpyFunc, {offset, str2Data, str2Len});

    // Add null terminator
    llvm::Value* nullTermPos = builder->CreateGEP(llvm::Type::getInt8Ty(*context), newData, newLen, "str.null_pos");
    builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt8Ty(*context), 0), nullTermPos);

    // Create new String struct
    llvm::Value* resultStr = llvm::UndefValue::get(strStructType);
    resultStr = builder->CreateInsertValue(resultStr, newData, 0, "str.result_data");
    resultStr = builder->CreateInsertValue(resultStr, newLen, 1, "str.result_len");

    VYB_CDBG << "DEBUG: String::concat() called" << std::endl;

    m_currentLLVMValue = resultStr;
}

void LLVMCodegen::handleStringSubstring(vyb::ast::CallExpression* node, llvm::Value* strPtr, llvm::Type* strStructType) {
    if (node->arguments.size() < 1 || node->arguments.size() > 2) {
        logError(node->loc, "String::substring expects 1 or 2 arguments (start, [end])");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Evaluate start index
    node->arguments[0]->accept(*this);
    llvm::Value* startIdx = m_currentLLVMValue;
    if (!startIdx) {
        logError(node->arguments[0]->loc, "Failed to evaluate start index for substring");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Load string fields
    llvm::Value* dataPtr = builder->CreateStructGEP(strStructType, strPtr, 0, "str.data_ptr");
    llvm::Value* lenPtr = builder->CreateStructGEP(strStructType, strPtr, 1, "str.len_ptr");
    llvm::Value* data = builder->CreateLoad(llvm::PointerType::get(*context, 0), dataPtr, "str.data");
    llvm::Value* len = builder->CreateLoad(llvm::Type::getInt64Ty(*context), lenPtr, "str.len");

    llvm::Value* endIdx;
    if (node->arguments.size() == 2) {
        // Use provided end index
        node->arguments[1]->accept(*this);
        endIdx = m_currentLLVMValue;
        if (!endIdx) {
            logError(node->arguments[1]->loc, "Failed to evaluate end index for substring");
            m_currentLLVMValue = nullptr;
            return;
        }
    } else {
        // End is the string length
        endIdx = len;
    }

    // Bounds checking
    llvm::BasicBlock* boundsOkBlock = llvm::BasicBlock::Create(*context, "bounds_ok", currentFunction);
    llvm::BasicBlock* boundsFailBlock = llvm::BasicBlock::Create(*context, "bounds_fail", currentFunction);
    llvm::BasicBlock* mergeBlock = llvm::BasicBlock::Create(*context, "substr_merge", currentFunction);

    // Check: start >= 0 && start <= len && end >= start && end <= len
    llvm::Value* startGEZero = builder->CreateICmpSGE(startIdx, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0));
    llvm::Value* startLELen = builder->CreateICmpSLE(startIdx, len);
    llvm::Value* endGEStart = builder->CreateICmpSGE(endIdx, startIdx);
    llvm::Value* endLELen = builder->CreateICmpSLE(endIdx, len);

    llvm::Value* boundsOk = builder->CreateAnd(startGEZero, startLELen);
    boundsOk = builder->CreateAnd(boundsOk, endGEStart);
    boundsOk = builder->CreateAnd(boundsOk, endLELen);

    builder->CreateCondBr(boundsOk, boundsOkBlock, boundsFailBlock);

    // Bounds fail: return empty string
    builder->SetInsertPoint(boundsFailBlock);
    llvm::Value* emptyStr = llvm::UndefValue::get(strStructType);
    emptyStr = builder->CreateInsertValue(emptyStr, llvm::ConstantPointerNull::get(llvm::PointerType::get(*context, 0)), 0);
    emptyStr = builder->CreateInsertValue(emptyStr, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0), 1);
    builder->CreateBr(mergeBlock);

    // Bounds OK: create substring
    builder->SetInsertPoint(boundsOkBlock);
    llvm::Value* subLen = builder->CreateSub(endIdx, startIdx, "substr.len");

    // Allocate new buffer (+1 for null terminator)
    llvm::Value* allocSize = builder->CreateAdd(subLen, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 1));
    llvm::FunctionType* mallocType = llvm::FunctionType::get(
        llvm::PointerType::get(*context, 0),
        {llvm::Type::getInt64Ty(*context)},
        false
    );
    llvm::Function* mallocFunc = module->getFunction("malloc");
    if (!mallocFunc) {
        mallocFunc = llvm::Function::Create(mallocType, llvm::Function::ExternalLinkage, "malloc", module.get());
    }
    llvm::Value* newData = builder->CreateCall(mallocFunc, {allocSize}, "substr.data");
    builder->CreateCall(getOrCreateVybStringRegisterFunction(), {newData});

    // Copy substring
    llvm::Value* srcOffset = builder->CreateGEP(llvm::Type::getInt8Ty(*context), data, startIdx, "src.offset");
    llvm::FunctionType* memcpyType = llvm::FunctionType::get(
        llvm::PointerType::get(*context, 0),
        {llvm::PointerType::get(*context, 0), llvm::PointerType::get(*context, 0), llvm::Type::getInt64Ty(*context)},
        false
    );
    llvm::Function* memcpyFunc = module->getFunction("memcpy");
    if (!memcpyFunc) {
        memcpyFunc = llvm::Function::Create(memcpyType, llvm::Function::ExternalLinkage, "memcpy", module.get());
    }
    builder->CreateCall(memcpyFunc, {newData, srcOffset, subLen});

    // Add null terminator
    llvm::Value* nullPos = builder->CreateGEP(llvm::Type::getInt8Ty(*context), newData, subLen);
    builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt8Ty(*context), 0), nullPos);

    // Create result string
    llvm::Value* resultStr = llvm::UndefValue::get(strStructType);
    resultStr = builder->CreateInsertValue(resultStr, newData, 0);
    resultStr = builder->CreateInsertValue(resultStr, subLen, 1);
    builder->CreateBr(mergeBlock);

    // Merge
    builder->SetInsertPoint(mergeBlock);
    llvm::PHINode* phi = builder->CreatePHI(strStructType, 2, "substr.result");
    phi->addIncoming(emptyStr, boundsFailBlock);
    phi->addIncoming(resultStr, boundsOkBlock);

    VYB_CDBG << "DEBUG: String::substring() called" << std::endl;
    m_currentLLVMValue = phi;
}

void LLVMCodegen::handleStringCharAt(vyb::ast::CallExpression* node, llvm::Value* strPtr, llvm::Type* strStructType) {
    if (node->arguments.size() != 1) {
        logError(node->loc, "String::char_at expects exactly 1 argument (index)");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Evaluate index
    node->arguments[0]->accept(*this);
    llvm::Value* idx = m_currentLLVMValue;
    if (!idx) {
        logError(node->arguments[0]->loc, "Failed to evaluate index for char_at");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Load string fields
    llvm::Value* dataPtr = builder->CreateStructGEP(strStructType, strPtr, 0, "str.data_ptr");
    llvm::Value* lenPtr = builder->CreateStructGEP(strStructType, strPtr, 1, "str.len_ptr");
    llvm::Value* data = builder->CreateLoad(llvm::PointerType::get(*context, 0), dataPtr, "str.data");
    llvm::Value* len = builder->CreateLoad(llvm::Type::getInt64Ty(*context), lenPtr, "str.len");

    // Bounds checking
    llvm::BasicBlock* inBoundsBlock = llvm::BasicBlock::Create(*context, "in_bounds", currentFunction);
    llvm::BasicBlock* outOfBoundsBlock = llvm::BasicBlock::Create(*context, "out_of_bounds", currentFunction);
    llvm::BasicBlock* mergeBlock = llvm::BasicBlock::Create(*context, "char_at_merge", currentFunction);

    llvm::Value* idxGEZero = builder->CreateICmpSGE(idx, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0));
    llvm::Value* idxLTLen = builder->CreateICmpSLT(idx, len);
    llvm::Value* inBounds = builder->CreateAnd(idxGEZero, idxLTLen);

    builder->CreateCondBr(inBounds, inBoundsBlock, outOfBoundsBlock);

    // Out of bounds: return 0
    builder->SetInsertPoint(outOfBoundsBlock);
    llvm::Value* defaultChar = llvm::ConstantInt::get(llvm::Type::getInt8Ty(*context), 0);
    builder->CreateBr(mergeBlock);

    // In bounds: load character
    builder->SetInsertPoint(inBoundsBlock);
    llvm::Value* charPtr = builder->CreateGEP(llvm::Type::getInt8Ty(*context), data, idx, "char.ptr");
    llvm::Value* charVal = builder->CreateLoad(llvm::Type::getInt8Ty(*context), charPtr, "char.val");
    builder->CreateBr(mergeBlock);

    // Merge
    builder->SetInsertPoint(mergeBlock);
    llvm::PHINode* phi = builder->CreatePHI(llvm::Type::getInt8Ty(*context), 2, "char.result");
    phi->addIncoming(defaultChar, outOfBoundsBlock);
    phi->addIncoming(charVal, inBoundsBlock);

    VYB_CDBG << "DEBUG: String::char_at() called" << std::endl;
    m_currentLLVMValue = phi;
}

void LLVMCodegen::handleStringToBytes(vyb::ast::CallExpression* node, llvm::Value* strPtr, llvm::Type* strStructType) {
    if (node->arguments.size() != 0) {
        logError(node->loc, "String::to_bytes expects no arguments");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Get data pointer from string
    llvm::Value* dataPtr = builder->CreateStructGEP(strStructType, strPtr, 0, "str.data_ptr");
    llvm::Value* data = builder->CreateLoad(llvm::PointerType::get(*context, 0), dataPtr, "str.data");

    VYB_CDBG << "DEBUG: String::to_bytes() called" << std::endl;

    // Return the byte array pointer
    m_currentLLVMValue = data;
}

void LLVMCodegen::handleStringFromBytes(vyb::ast::CallExpression* node, llvm::Value* strPtr, llvm::Type* strStructType) {
    if (node->arguments.size() != 2) {
        logError(node->loc, "String::from_bytes expects exactly 2 arguments (byte_ptr, length)");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Evaluate arguments
    node->arguments[0]->accept(*this);
    llvm::Value* bytePtr = m_currentLLVMValue;

    node->arguments[1]->accept(*this);
    llvm::Value* length = m_currentLLVMValue;

    if (!bytePtr || !length) {
        logError(node->loc, "Failed to evaluate arguments for String::from_bytes");
        return;
    }

    // Create String struct from bytes
    llvm::Value* resultStr = llvm::UndefValue::get(strStructType);
    resultStr = builder->CreateInsertValue(resultStr, bytePtr, 0, "str.from_bytes_data");
    resultStr = builder->CreateInsertValue(resultStr, length, 1, "str.from_bytes_len");

    VYB_CDBG << "DEBUG: String::from_bytes() called" << std::endl;

    m_currentLLVMValue = resultStr;
}

void LLVMCodegen::handleStringStartsWith(vyb::ast::CallExpression* node, llvm::Value* strPtr, llvm::Type* strStructType) {
    if (node->arguments.size() != 1) {
        logError(node->loc, "String::starts_with expects exactly 1 argument (prefix string)");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Evaluate prefix argument
    node->arguments[0]->accept(*this);
    llvm::Value* prefixStr = m_currentLLVMValue;
    if (!prefixStr) {
        logError(node->arguments[0]->loc, "Failed to evaluate prefix for starts_with");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Load main string fields
    llvm::Value* dataPtr = builder->CreateStructGEP(strStructType, strPtr, 0, "str.data_ptr");
    llvm::Value* lenPtr = builder->CreateStructGEP(strStructType, strPtr, 1, "str.len_ptr");
    llvm::Value* data = builder->CreateLoad(llvm::PointerType::get(*context, 0), dataPtr, "str.data");
    llvm::Value* len = builder->CreateLoad(llvm::Type::getInt64Ty(*context), lenPtr, "str.len");

    // Load prefix string fields
    llvm::Value* prefixData = builder->CreateExtractValue(prefixStr, 0, "prefix.data");
    llvm::Value* prefixLen = builder->CreateExtractValue(prefixStr, 1, "prefix.len");

    // Check if prefix length > string length
    llvm::BasicBlock* lenOkBlock = llvm::BasicBlock::Create(*context, "len_ok", currentFunction);
    llvm::BasicBlock* returnFalseBlock = llvm::BasicBlock::Create(*context, "return_false", currentFunction);
    llvm::BasicBlock* compareBlock = llvm::BasicBlock::Create(*context, "compare", currentFunction);
    llvm::BasicBlock* mergeBlock = llvm::BasicBlock::Create(*context, "starts_with_merge", currentFunction);

    llvm::Value* lenOk = builder->CreateICmpSLE(prefixLen, len);
    builder->CreateCondBr(lenOk, lenOkBlock, returnFalseBlock);

    // If prefix is empty, return true
    builder->SetInsertPoint(lenOkBlock);
    llvm::Value* prefixIsEmpty = builder->CreateICmpEQ(prefixLen, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0));
    builder->CreateCondBr(prefixIsEmpty, mergeBlock, compareBlock);

    // Compare prefix with start of string using memcmp
    builder->SetInsertPoint(compareBlock);
    llvm::FunctionType* memcmpType = llvm::FunctionType::get(
        llvm::Type::getInt32Ty(*context),
        {llvm::PointerType::get(*context, 0), llvm::PointerType::get(*context, 0), llvm::Type::getInt64Ty(*context)},
        false
    );
    llvm::Function* memcmpFunc = module->getFunction("memcmp");
    if (!memcmpFunc) {
        memcmpFunc = llvm::Function::Create(memcmpType, llvm::Function::ExternalLinkage, "memcmp", module.get());
    }
    llvm::Value* cmpResult = builder->CreateCall(memcmpFunc, {data, prefixData, prefixLen}, "cmp.result");
    llvm::Value* isMatch = builder->CreateICmpEQ(cmpResult, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0));
    builder->CreateBr(mergeBlock);

    // Return false
    builder->SetInsertPoint(returnFalseBlock);
    builder->CreateBr(mergeBlock);

    // Merge
    builder->SetInsertPoint(mergeBlock);
    llvm::PHINode* phi = builder->CreatePHI(llvm::Type::getInt1Ty(*context), 3, "starts_with.result");
    phi->addIncoming(llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), 0), returnFalseBlock);
    phi->addIncoming(llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), 1), lenOkBlock);
    phi->addIncoming(isMatch, compareBlock);

    VYB_CDBG << "DEBUG: String::starts_with() called" << std::endl;
    m_currentLLVMValue = phi;
}

void LLVMCodegen::handleStringEndsWith(vyb::ast::CallExpression* node, llvm::Value* strPtr, llvm::Type* strStructType) {
    if (node->arguments.size() != 1) {
        logError(node->loc, "String::ends_with expects exactly 1 argument (suffix string)");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Evaluate suffix argument
    node->arguments[0]->accept(*this);
    llvm::Value* suffixStr = m_currentLLVMValue;
    if (!suffixStr) {
        logError(node->arguments[0]->loc, "Failed to evaluate suffix for ends_with");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Load main string fields
    llvm::Value* dataPtr = builder->CreateStructGEP(strStructType, strPtr, 0, "str.data_ptr");
    llvm::Value* lenPtr = builder->CreateStructGEP(strStructType, strPtr, 1, "str.len_ptr");
    llvm::Value* data = builder->CreateLoad(llvm::PointerType::get(*context, 0), dataPtr, "str.data");
    llvm::Value* len = builder->CreateLoad(llvm::Type::getInt64Ty(*context), lenPtr, "str.len");

    // Load suffix string fields
    llvm::Value* suffixData = builder->CreateExtractValue(suffixStr, 0, "suffix.data");
    llvm::Value* suffixLen = builder->CreateExtractValue(suffixStr, 1, "suffix.len");

    // Check if suffix length > string length
    llvm::BasicBlock* lenOkBlock = llvm::BasicBlock::Create(*context, "len_ok", currentFunction);
    llvm::BasicBlock* returnFalseBlock = llvm::BasicBlock::Create(*context, "return_false", currentFunction);
    llvm::BasicBlock* compareBlock = llvm::BasicBlock::Create(*context, "compare", currentFunction);
    llvm::BasicBlock* mergeBlock = llvm::BasicBlock::Create(*context, "ends_with_merge", currentFunction);

    llvm::Value* lenOk = builder->CreateICmpSLE(suffixLen, len);
    builder->CreateCondBr(lenOk, lenOkBlock, returnFalseBlock);

    // If suffix is empty, return true
    builder->SetInsertPoint(lenOkBlock);
    llvm::Value* suffixIsEmpty = builder->CreateICmpEQ(suffixLen, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0));
    builder->CreateCondBr(suffixIsEmpty, mergeBlock, compareBlock);

    // Compare suffix with end of string using memcmp
    builder->SetInsertPoint(compareBlock);
    llvm::Value* offset = builder->CreateSub(len, suffixLen, "end.offset");
    llvm::Value* endPtr = builder->CreateGEP(llvm::Type::getInt8Ty(*context), data, offset, "end.ptr");

    llvm::FunctionType* memcmpType = llvm::FunctionType::get(
        llvm::Type::getInt32Ty(*context),
        {llvm::PointerType::get(*context, 0), llvm::PointerType::get(*context, 0), llvm::Type::getInt64Ty(*context)},
        false
    );
    llvm::Function* memcmpFunc = module->getFunction("memcmp");
    if (!memcmpFunc) {
        memcmpFunc = llvm::Function::Create(memcmpType, llvm::Function::ExternalLinkage, "memcmp", module.get());
    }
    llvm::Value* cmpResult = builder->CreateCall(memcmpFunc, {endPtr, suffixData, suffixLen}, "cmp.result");
    llvm::Value* isMatch = builder->CreateICmpEQ(cmpResult, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0));
    builder->CreateBr(mergeBlock);

    // Return false
    builder->SetInsertPoint(returnFalseBlock);
    builder->CreateBr(mergeBlock);

    // Merge
    builder->SetInsertPoint(mergeBlock);
    llvm::PHINode* phi = builder->CreatePHI(llvm::Type::getInt1Ty(*context), 3, "ends_with.result");
    phi->addIncoming(llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), 0), returnFalseBlock);
    phi->addIncoming(llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), 1), lenOkBlock);
    phi->addIncoming(isMatch, compareBlock);

    VYB_CDBG << "DEBUG: String::ends_with() called" << std::endl;
    m_currentLLVMValue = phi;
}

void LLVMCodegen::handleStringContains(vyb::ast::CallExpression* node, llvm::Value* strPtr, llvm::Type* strStructType) {
    if (node->arguments.size() != 1) {
        logError(node->loc, "String::contains expects exactly 1 argument (substring)");
        m_currentLLVMValue = nullptr;
        return;
    }

    // For now, use a simple implementation that calls strstr
    // A more efficient implementation would use a substring search algorithm

    // Load main string fields
    llvm::Value* dataPtr = builder->CreateStructGEP(strStructType, strPtr, 0, "str.data_ptr");
    llvm::Value* data = builder->CreateLoad(llvm::PointerType::get(*context, 0), dataPtr, "str.data");

    // Evaluate substring argument
    node->arguments[0]->accept(*this);
    llvm::Value* substringStr = m_currentLLVMValue;
    if (!substringStr) {
        logError(node->arguments[0]->loc, "Failed to evaluate substring for contains");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Load substring fields
    llvm::Value* substringData = builder->CreateExtractValue(substringStr, 0, "substring.data");

    // Call strstr
    llvm::FunctionType* strstrType = llvm::FunctionType::get(
        llvm::PointerType::get(*context, 0),
        {llvm::PointerType::get(*context, 0), llvm::PointerType::get(*context, 0)},
        false
    );
    llvm::Function* strstrFunc = module->getFunction("strstr");
    if (!strstrFunc) {
        strstrFunc = llvm::Function::Create(strstrType, llvm::Function::ExternalLinkage, "strstr", module.get());
    }
    llvm::Value* result = builder->CreateCall(strstrFunc, {data, substringData}, "strstr.result");

    // Check if result is not NULL
    llvm::Value* isFound = builder->CreateICmpNE(result, llvm::ConstantPointerNull::get(llvm::PointerType::get(*context, 0)), "contains.result");

    VYB_CDBG << "DEBUG: String::contains() called" << std::endl;
    m_currentLLVMValue = isFound;
}

void LLVMCodegen::handleStringToUpper(vyb::ast::CallExpression* node, llvm::Value* strPtr, llvm::Type* strStructType) {
    if (!node->arguments.empty()) {
        logError(node->loc, "String::to_upper expects no arguments");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Load string fields
    llvm::Value* dataPtr = builder->CreateStructGEP(strStructType, strPtr, 0, "str.data_ptr");
    llvm::Value* lenPtr = builder->CreateStructGEP(strStructType, strPtr, 1, "str.len_ptr");
    llvm::Value* data = builder->CreateLoad(llvm::PointerType::get(*context, 0), dataPtr, "str.data");
    llvm::Value* len = builder->CreateLoad(llvm::Type::getInt64Ty(*context), lenPtr, "str.len");

    // Allocate new buffer (len + 1 for null terminator)
    llvm::Value* bufferSize = builder->CreateAdd(len, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 1), "buffer.size");
    llvm::FunctionType* mallocType = llvm::FunctionType::get(
        llvm::PointerType::get(*context, 0),
        {llvm::Type::getInt64Ty(*context)},
        false
    );
    llvm::Function* mallocFunc = module->getFunction("malloc");
    if (!mallocFunc) {
        mallocFunc = llvm::Function::Create(mallocType, llvm::Function::ExternalLinkage, "malloc", module.get());
    }
    llvm::Value* newData = builder->CreateCall(mallocFunc, {bufferSize}, "new.data");
    builder->CreateCall(getOrCreateVybStringRegisterFunction(), {newData});

    // Create loop to convert each character
    llvm::BasicBlock* loopCondBlock = llvm::BasicBlock::Create(*context, "loop.cond", currentFunction);
    llvm::BasicBlock* loopBodyBlock = llvm::BasicBlock::Create(*context, "loop.body", currentFunction);
    llvm::BasicBlock* loopEndBlock = llvm::BasicBlock::Create(*context, "loop.end", currentFunction);

    // Initialize index
    llvm::Value* indexPtr = builder->CreateAlloca(llvm::Type::getInt64Ty(*context), nullptr, "index.ptr");
    builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0), indexPtr);
    builder->CreateBr(loopCondBlock);

    // Loop condition: index < len
    builder->SetInsertPoint(loopCondBlock);
    llvm::Value* index = builder->CreateLoad(llvm::Type::getInt64Ty(*context), indexPtr, "index");
    llvm::Value* cond = builder->CreateICmpSLT(index, len, "loop.cond");
    builder->CreateCondBr(cond, loopBodyBlock, loopEndBlock);

    // Loop body: convert character and store
    builder->SetInsertPoint(loopBodyBlock);
    llvm::Value* srcPtr = builder->CreateGEP(llvm::Type::getInt8Ty(*context), data, index, "src.ptr");
    llvm::Value* ch = builder->CreateLoad(llvm::Type::getInt8Ty(*context), srcPtr, "ch");

    // toupper: if (ch >= 'a' && ch <= 'z') ch = ch - 32
    llvm::Value* isLower = builder->CreateAnd(
        builder->CreateICmpSGE(ch, llvm::ConstantInt::get(llvm::Type::getInt8Ty(*context), 97)),  // 'a'
        builder->CreateICmpSLE(ch, llvm::ConstantInt::get(llvm::Type::getInt8Ty(*context), 122)), // 'z'
        "is.lower"
    );
    llvm::Value* upperCh = builder->CreateSub(ch, llvm::ConstantInt::get(llvm::Type::getInt8Ty(*context), 32), "upper.ch");
    llvm::Value* convertedCh = builder->CreateSelect(isLower, upperCh, ch, "converted.ch");

    llvm::Value* dstPtr = builder->CreateGEP(llvm::Type::getInt8Ty(*context), newData, index, "dst.ptr");
    builder->CreateStore(convertedCh, dstPtr);

    // Increment index
    llvm::Value* nextIndex = builder->CreateAdd(index, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 1), "next.index");
    builder->CreateStore(nextIndex, indexPtr);
    builder->CreateBr(loopCondBlock);

    // After loop: add null terminator
    builder->SetInsertPoint(loopEndBlock);
    llvm::Value* nullTermPtr = builder->CreateGEP(llvm::Type::getInt8Ty(*context), newData, len, "null.term.ptr");
    builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt8Ty(*context), 0), nullTermPtr);

    // Create result String struct
    llvm::Value* resultStr = llvm::UndefValue::get(strStructType);
    resultStr = builder->CreateInsertValue(resultStr, newData, 0, "result.data");
    resultStr = builder->CreateInsertValue(resultStr, len, 1, "result.len");

    VYB_CDBG << "DEBUG: String::to_upper() called" << std::endl;
    m_currentLLVMValue = resultStr;
}

void LLVMCodegen::handleStringToLower(vyb::ast::CallExpression* node, llvm::Value* strPtr, llvm::Type* strStructType) {
    if (!node->arguments.empty()) {
        logError(node->loc, "String::to_lower expects no arguments");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Load string fields
    llvm::Value* dataPtr = builder->CreateStructGEP(strStructType, strPtr, 0, "str.data_ptr");
    llvm::Value* lenPtr = builder->CreateStructGEP(strStructType, strPtr, 1, "str.len_ptr");
    llvm::Value* data = builder->CreateLoad(llvm::PointerType::get(*context, 0), dataPtr, "str.data");
    llvm::Value* len = builder->CreateLoad(llvm::Type::getInt64Ty(*context), lenPtr, "str.len");

    // Allocate new buffer (len + 1 for null terminator)
    llvm::Value* bufferSize = builder->CreateAdd(len, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 1), "buffer.size");
    llvm::FunctionType* mallocType = llvm::FunctionType::get(
        llvm::PointerType::get(*context, 0),
        {llvm::Type::getInt64Ty(*context)},
        false
    );
    llvm::Function* mallocFunc = module->getFunction("malloc");
    if (!mallocFunc) {
        mallocFunc = llvm::Function::Create(mallocType, llvm::Function::ExternalLinkage, "malloc", module.get());
    }
    llvm::Value* newData = builder->CreateCall(mallocFunc, {bufferSize}, "new.data");
    builder->CreateCall(getOrCreateVybStringRegisterFunction(), {newData});

    // Create loop to convert each character
    llvm::BasicBlock* loopCondBlock = llvm::BasicBlock::Create(*context, "loop.cond", currentFunction);
    llvm::BasicBlock* loopBodyBlock = llvm::BasicBlock::Create(*context, "loop.body", currentFunction);
    llvm::BasicBlock* loopEndBlock = llvm::BasicBlock::Create(*context, "loop.end", currentFunction);

    // Initialize index
    llvm::Value* indexPtr = builder->CreateAlloca(llvm::Type::getInt64Ty(*context), nullptr, "index.ptr");
    builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0), indexPtr);
    builder->CreateBr(loopCondBlock);

    // Loop condition: index < len
    builder->SetInsertPoint(loopCondBlock);
    llvm::Value* index = builder->CreateLoad(llvm::Type::getInt64Ty(*context), indexPtr, "index");
    llvm::Value* cond = builder->CreateICmpSLT(index, len, "loop.cond");
    builder->CreateCondBr(cond, loopBodyBlock, loopEndBlock);

    // Loop body: convert character and store
    builder->SetInsertPoint(loopBodyBlock);
    llvm::Value* srcPtr = builder->CreateGEP(llvm::Type::getInt8Ty(*context), data, index, "src.ptr");
    llvm::Value* ch = builder->CreateLoad(llvm::Type::getInt8Ty(*context), srcPtr, "ch");

    // tolower: if (ch >= 'A' && ch <= 'Z') ch = ch + 32
    llvm::Value* isUpper = builder->CreateAnd(
        builder->CreateICmpSGE(ch, llvm::ConstantInt::get(llvm::Type::getInt8Ty(*context), 65)),  // 'A'
        builder->CreateICmpSLE(ch, llvm::ConstantInt::get(llvm::Type::getInt8Ty(*context), 90)),  // 'Z'
        "is.upper"
    );
    llvm::Value* lowerCh = builder->CreateAdd(ch, llvm::ConstantInt::get(llvm::Type::getInt8Ty(*context), 32), "lower.ch");
    llvm::Value* convertedCh = builder->CreateSelect(isUpper, lowerCh, ch, "converted.ch");

    llvm::Value* dstPtr = builder->CreateGEP(llvm::Type::getInt8Ty(*context), newData, index, "dst.ptr");
    builder->CreateStore(convertedCh, dstPtr);

    // Increment index
    llvm::Value* nextIndex = builder->CreateAdd(index, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 1), "next.index");
    builder->CreateStore(nextIndex, indexPtr);
    builder->CreateBr(loopCondBlock);

    // After loop: add null terminator
    builder->SetInsertPoint(loopEndBlock);
    llvm::Value* nullTermPtr = builder->CreateGEP(llvm::Type::getInt8Ty(*context), newData, len, "null.term.ptr");
    builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt8Ty(*context), 0), nullTermPtr);

    // Create result String struct
    llvm::Value* resultStr = llvm::UndefValue::get(strStructType);
    resultStr = builder->CreateInsertValue(resultStr, newData, 0, "result.data");
    resultStr = builder->CreateInsertValue(resultStr, len, 1, "result.len");

    VYB_CDBG << "DEBUG: String::to_lower() called" << std::endl;
    m_currentLLVMValue = resultStr;
}

// String::trim() — remove leading and trailing ASCII whitespace
void LLVMCodegen::handleStringTrim(vyb::ast::CallExpression* node, llvm::Value* strPtr, llvm::Type* strStructType) {
    if (!node->arguments.empty()) {
        logError(node->loc, "String::trim expects no arguments");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Load string fields
    llvm::Value* dataPtr = builder->CreateStructGEP(strStructType, strPtr, 0, "str.data_ptr");
    llvm::Value* lenPtr  = builder->CreateStructGEP(strStructType, strPtr, 1, "str.len_ptr");
    llvm::Value* data    = builder->CreateLoad(llvm::PointerType::get(*context, 0), dataPtr, "str.data");
    llvm::Value* len     = builder->CreateLoad(llvm::Type::getInt64Ty(*context), lenPtr, "str.len");

    // Declare helper intrinsics (libc)
    llvm::FunctionType* isspaceTy = llvm::FunctionType::get(
        llvm::Type::getInt32Ty(*context),
        {llvm::Type::getInt32Ty(*context)}, false);
    llvm::FunctionCallee isspaceFunc = module->getOrInsertFunction("isspace", isspaceTy);

    llvm::FunctionType* mallocTy = llvm::FunctionType::get(
        llvm::PointerType::get(*context, 0),
        {llvm::Type::getInt64Ty(*context)}, false);
    llvm::FunctionCallee mallocFunc = module->getOrInsertFunction("malloc", mallocTy);

    llvm::FunctionType* memcpyTy = llvm::FunctionType::get(
        llvm::PointerType::get(*context, 0),
        {llvm::PointerType::get(*context, 0),
         llvm::PointerType::get(*context, 0),
         llvm::Type::getInt64Ty(*context)}, false);
    llvm::FunctionCallee memcpyFunc = module->getOrInsertFunction("memcpy", memcpyTy);

    llvm::Type* i64 = llvm::Type::getInt64Ty(*context);
    llvm::Type* i32 = llvm::Type::getInt32Ty(*context);
    llvm::Type* i8  = llvm::Type::getInt8Ty(*context);
    llvm::Value* zero64 = llvm::ConstantInt::get(i64, 0);
    llvm::Value* one64  = llvm::ConstantInt::get(i64, 1);

    // Find start (first non-whitespace)
    llvm::Value* startPtr = builder->CreateAlloca(i64, nullptr, "trim.start");
    builder->CreateStore(zero64, startPtr);
    llvm::BasicBlock* startCond = llvm::BasicBlock::Create(*context, "trim.start.cond", currentFunction);
    llvm::BasicBlock* startBody = llvm::BasicBlock::Create(*context, "trim.start.body", currentFunction);
    llvm::BasicBlock* endFind   = llvm::BasicBlock::Create(*context, "trim.end.find", currentFunction);
    builder->CreateBr(startCond);

    builder->SetInsertPoint(startCond);
    llvm::Value* si  = builder->CreateLoad(i64, startPtr, "si");
    llvm::Value* siLtLen = builder->CreateICmpSLT(si, len, "si.lt.len");
    builder->CreateCondBr(siLtLen, startBody, endFind);

    builder->SetInsertPoint(startBody);
    llvm::Value* siChar = builder->CreateGEP(i8, data, si, "si.char.ptr");
    llvm::Value* siCh   = builder->CreateLoad(i8, siChar, "si.ch");
    llvm::Value* siCh32 = builder->CreateZExt(siCh, i32, "si.ch32");
    llvm::Value* siIsWs = builder->CreateCall(isspaceFunc, {siCh32}, "si.is.ws");
    llvm::Value* siIsWsBool = builder->CreateICmpNE(siIsWs, llvm::ConstantInt::get(i32, 0), "si.is.ws.bool");
    llvm::BasicBlock* incSi    = llvm::BasicBlock::Create(*context, "trim.inc.si", currentFunction);
    llvm::BasicBlock* stopSi   = llvm::BasicBlock::Create(*context, "trim.stop.si", currentFunction);
    builder->CreateCondBr(siIsWsBool, incSi, stopSi);

    builder->SetInsertPoint(incSi);
    builder->CreateStore(builder->CreateAdd(si, one64), startPtr);
    builder->CreateBr(startCond);

    builder->SetInsertPoint(stopSi);
    builder->CreateBr(endFind);

    // Find end (last non-whitespace)
    builder->SetInsertPoint(endFind);
    llvm::Value* endIdx = builder->CreateAlloca(i64, nullptr, "trim.end");
    builder->CreateStore(len, endIdx);
    llvm::BasicBlock* endCond = llvm::BasicBlock::Create(*context, "trim.end.cond", currentFunction);
    llvm::BasicBlock* endBody = llvm::BasicBlock::Create(*context, "trim.end.body", currentFunction);
    llvm::BasicBlock* doTrim  = llvm::BasicBlock::Create(*context, "trim.do", currentFunction);
    builder->CreateBr(endCond);

    builder->SetInsertPoint(endCond);
    llvm::Value* startVal = builder->CreateLoad(i64, startPtr);
    llvm::Value* ei       = builder->CreateLoad(i64, endIdx, "ei");
    llvm::Value* eiGtStart = builder->CreateICmpSGT(ei, startVal, "ei.gt.start");
    builder->CreateCondBr(eiGtStart, endBody, doTrim);

    builder->SetInsertPoint(endBody);
    llvm::Value* eiM1  = builder->CreateSub(ei, one64, "ei.m1");
    llvm::Value* eiChar = builder->CreateGEP(i8, data, eiM1, "ei.char.ptr");
    llvm::Value* eiCh   = builder->CreateLoad(i8, eiChar, "ei.ch");
    llvm::Value* eiCh32 = builder->CreateZExt(eiCh, i32, "ei.ch32");
    llvm::Value* eiIsWs = builder->CreateCall(isspaceFunc, {eiCh32}, "ei.is.ws");
    llvm::Value* eiIsWsBool = builder->CreateICmpNE(eiIsWs, llvm::ConstantInt::get(i32, 0), "ei.is.ws.bool");
    llvm::BasicBlock* decEi  = llvm::BasicBlock::Create(*context, "trim.dec.ei", currentFunction);
    llvm::BasicBlock* stopEi = llvm::BasicBlock::Create(*context, "trim.stop.ei", currentFunction);
    builder->CreateCondBr(eiIsWsBool, decEi, stopEi);

    builder->SetInsertPoint(decEi);
    builder->CreateStore(eiM1, endIdx);
    builder->CreateBr(endCond);

    builder->SetInsertPoint(stopEi);
    builder->CreateBr(doTrim);

    // Allocate result and copy [start, end)
    builder->SetInsertPoint(doTrim);
    llvm::Value* sv  = builder->CreateLoad(i64, startPtr, "sv");
    llvm::Value* ev  = builder->CreateLoad(i64, endIdx,   "ev");
    llvm::Value* newLen = builder->CreateSub(ev, sv, "trim.new.len");
    llvm::Value* allocSz = builder->CreateAdd(newLen, one64, "trim.alloc.sz");
    llvm::Value* newData = builder->CreateCall(mallocFunc, {allocSz}, "trim.new.data");
    builder->CreateCall(getOrCreateVybStringRegisterFunction(), {newData});
    llvm::Value* srcStart = builder->CreateGEP(i8, data, sv, "trim.src.start");
    builder->CreateCall(memcpyFunc, {newData, srcStart, newLen});
    // Null terminator
    llvm::Value* nullPtr = builder->CreateGEP(i8, newData, newLen, "trim.null.ptr");
    builder->CreateStore(llvm::ConstantInt::get(i8, 0), nullPtr);

    llvm::Value* resultStr = llvm::UndefValue::get(strStructType);
    resultStr = builder->CreateInsertValue(resultStr, newData, 0, "trim.result.data");
    resultStr = builder->CreateInsertValue(resultStr, newLen, 1, "trim.result.len");

    VYB_CDBG << "DEBUG: String::trim() called" << std::endl;
    m_currentLLVMValue = resultStr;
}

// String::replace(old<String>, new<String>) -> String
// Replaces all non-overlapping occurrences of 'old' with 'new'.
void LLVMCodegen::handleStringReplace(vyb::ast::CallExpression* node, llvm::Value* strPtr, llvm::Type* strStructType) {
    if (node->arguments.size() != 2) {
        logError(node->loc, "String::replace expects exactly 2 arguments (old, new)");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Evaluate old and new string arguments
    node->arguments[0]->accept(*this);
    llvm::Value* oldStr = m_currentLLVMValue;
    if (!oldStr) { logError(node->loc, "replace: failed to evaluate 'old' argument"); return; }

    node->arguments[1]->accept(*this);
    llvm::Value* newStr = m_currentLLVMValue;
    if (!newStr) { logError(node->loc, "replace: failed to evaluate 'new' argument"); return; }

    // Extract fields
    llvm::Value* srcDataPtr = builder->CreateStructGEP(strStructType, strPtr, 0, "src.data_ptr");
    llvm::Value* srcLenPtr  = builder->CreateStructGEP(strStructType, strPtr, 1, "src.len_ptr");
    llvm::Value* srcData    = builder->CreateLoad(llvm::PointerType::get(*context, 0), srcDataPtr, "src.data");
    llvm::Value* srcLen     = builder->CreateLoad(llvm::Type::getInt64Ty(*context), srcLenPtr, "src.len");

    llvm::Type* i8ptr = llvm::PointerType::get(*context, 0);
    llvm::Type* i64   = llvm::Type::getInt64Ty(*context);
    llvm::Type* i8    = llvm::Type::getInt8Ty(*context);

    // Declare strstr and malloc/memcpy/strlen
    llvm::FunctionCallee strstrFunc = module->getOrInsertFunction(
        "strstr", llvm::FunctionType::get(i8ptr, {i8ptr, i8ptr}, false));
    llvm::FunctionCallee mallocFunc = module->getOrInsertFunction(
        "malloc", llvm::FunctionType::get(i8ptr, {i64}, false));
    llvm::FunctionCallee memcpyFunc = module->getOrInsertFunction(
        "memcpy", llvm::FunctionType::get(i8ptr, {i8ptr, i8ptr, i64}, false));
    llvm::FunctionCallee strlenFunc = module->getOrInsertFunction(
        "strlen", llvm::FunctionType::get(i64, {i8ptr}, false));

    // For simplicity, delegate to a runtime helper implemented in C.
    // Declare: char* __vyb_string_replace(const char* src, int64_t src_len,
    //                                     const char* old_s, const char* new_s,
    //                                     int64_t* out_len)
    llvm::FunctionType* replaceRT = llvm::FunctionType::get(
        i8ptr,
        {i8ptr, i64, i8ptr, i8ptr, i8ptr},
        false);
    llvm::FunctionCallee replaceFunc = module->getOrInsertFunction("__vyb_string_replace", replaceRT);

    // Extract old and new data pointers
    llvm::Value* oldData, *newData2;
    if (oldStr->getType()->isPointerTy()) {
        llvm::Value* odp = builder->CreateStructGEP(strStructType, oldStr, 0);
        oldData = builder->CreateLoad(i8ptr, odp);
    } else {
        oldData = builder->CreateExtractValue(oldStr, 0, "old.data");
    }
    if (newStr->getType()->isPointerTy()) {
        llvm::Value* ndp = builder->CreateStructGEP(strStructType, newStr, 0);
        newData2 = builder->CreateLoad(i8ptr, ndp);
    } else {
        newData2 = builder->CreateExtractValue(newStr, 0, "new.data");
    }

    // Allocate an i64 for out_len on the stack
    llvm::Value* outLenPtr = builder->CreateAlloca(i64, nullptr, "replace.out_len");
    builder->CreateStore(llvm::ConstantInt::get(i64, 0), outLenPtr);

    llvm::Value* resultData = builder->CreateCall(
        replaceFunc, {srcData, srcLen, oldData, newData2, outLenPtr}, "replace.data");
    builder->CreateCall(getOrCreateVybStringRegisterFunction(), {resultData});
    llvm::Value* resultLen = builder->CreateLoad(i64, outLenPtr, "replace.len");

    llvm::Value* resultStr = llvm::UndefValue::get(strStructType);
    resultStr = builder->CreateInsertValue(resultStr, resultData, 0, "replace.result.data");
    resultStr = builder->CreateInsertValue(resultStr, resultLen, 1, "replace.result.len");

    VYB_CDBG << "DEBUG: String::replace() called" << std::endl;
    m_currentLLVMValue = resultStr;
}


// String::format(arg0, arg1, ...) -> String
// Replaces sequential `{}` placeholders in the receiver with the string form of
// each argument (String/Int/Float/Bool/...). Delegates the substitution to the
// runtime `__vyb_string_format` helper; any placeholder beyond the supplied args
// is kept verbatim. Works for a variable number of arguments.
void LLVMCodegen::handleStringFormat(vyb::ast::CallExpression* node, llvm::Value* strPtr, llvm::Type* strStructType) {
    llvm::Type* i8ptr = llvm::PointerType::get(*context, 0);
    llvm::Type* i8ptrpt = llvm::PointerType::get(i8ptr, 0);

    // Extract receiver (format template) fields.
    llvm::Value* fmtDataPtr = builder->CreateStructGEP(strStructType, strPtr, 0, "fmt.data_ptr");
    llvm::Value* fmtLenPtr  = builder->CreateStructGEP(strStructType, strPtr, 1, "fmt.len_ptr");
    llvm::Value* fmtData    = builder->CreateLoad(i8ptr, fmtDataPtr, "fmt.data");
    llvm::Value* fmtLen     = builder->CreateLoad(llvm::Type::getInt64Ty(*context), fmtLenPtr, "fmt.len");

    size_t argc = node->arguments.size();
    llvm::Value* argsAlloca = builder->CreateAlloca(
        i8ptr, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), (argc == 0 ? 1 : argc)), "fmt.args");
    std::vector<llvm::Value*> ownedTemps;

    for (size_t a = 0; a < argc; ++a) {
        node->arguments[a]->accept(*this);
        llvm::Value* arg = m_currentLLVMValue;
        if (!arg) { logError(node->arguments[a]->loc, "format: failed to evaluate argument"); m_currentLLVMValue = nullptr; return; }

        llvm::Value* serialized = nullptr;
        bool owned = false;
        bool isString = false;
        if (node->arguments[a]->type) {
            std::string typeStr = resolveTypeAliasToBaseName(node->arguments[a]->type.get());
            if (typeStr.empty()) typeStr = node->arguments[a]->type->toString();
            if (typeStr == "String" || typeStr == "string") isString = true;
        }
        if (isString) {
            if (arg->getType() && arg->getType()->isStructTy()) {
                serialized = builder->CreateExtractValue(arg, 0, "fmt.str.ptr");
            } else if (arg->getType() && arg->getType()->isPointerTy()) {
                serialized = arg;
            }
            if (serialized) owned = exprProducesOwnedStringTemp(node->arguments[a].get());
            else { logError(node->arguments[a]->loc, "format: could not serialize String argument"); m_currentLLVMValue = nullptr; return; }
        } else if (arg->getType() && arg->getType()->isPointerTy() && arg->getType() == i8ptr) {
            serialized = arg;  // already a char* (CString)
        } else {
            serialized = generateToStringCall(arg, arg->getType(), node->arguments[a]->type.get(), node->loc);
            if (!serialized) serialized = generateGenericSerialization(arg, node->arguments[a]->type.get());
            owned = true;
        }
        if (!serialized) { logError(node->arguments[a]->loc, "format: could not serialize argument"); m_currentLLVMValue = nullptr; return; }
        if (owned) ownedTemps.push_back(serialized);
        llvm::Value* slot = builder->CreateGEP(
            i8ptr, argsAlloca, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), (int64_t)a), "fmt.args.slot");
        builder->CreateStore(serialized, slot);
    }

    llvm::Value* countVal = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), (int64_t)argc);
    llvm::Value* outLenPtr = builder->CreateAlloca(llvm::Type::getInt64Ty(*context), nullptr, "fmt.out_len");
    builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0), outLenPtr);

    llvm::FunctionType* fmtTy = llvm::FunctionType::get(
        i8ptr, {i8ptr, llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context), i8ptrpt, i8ptr}, false);
    llvm::FunctionCallee fmtFunc = module->getOrInsertFunction("__vyb_string_format", fmtTy);
    llvm::Value* resultData = builder->CreateCall(fmtFunc, {fmtData, fmtLen, countVal, argsAlloca, outLenPtr}, "fmt.data");
    builder->CreateCall(getOrCreateVybStringRegisterFunction(), {resultData});
    llvm::Value* resultLen = builder->CreateLoad(llvm::Type::getInt64Ty(*context), outLenPtr, "fmt.len");

    // Release any freshly-allocated serialization temporaries (scalars, to_string,
    // struct/array serializations). String operands that yielded a fresh temp are
    // also released; the receiver and borrowed/named-string buffers are not.
    for (llvm::Value* t : ownedTemps) {
        builder->CreateCall(getOrCreateVybStringFreeFunction(), {t});
    }

    llvm::Value* resultStr = llvm::UndefValue::get(strStructType);
    resultStr = builder->CreateInsertValue(resultStr, resultData, 0, "fmt.result.data");
    resultStr = builder->CreateInsertValue(resultStr, resultLen, 1, "fmt.result.len");
    m_currentLLVMValue = resultStr;
}


// Built-in channel method emitter. `handle` is the receiver's i64 channel handle
// (already loaded). `isString` selects the refcounted string channel runtime vs.
// the int-slot channel runtime. Covers the ergonomic `chan<T>` surface: send,
// recv, try, len, free, and handle (the raw Int handle for `chan_select`).
void LLVMCodegen::emitChannelMethod(vyb::ast::CallExpression* node, llvm::Value* handle,
                                    const std::string& methodName, bool isString,
                                    const vyb::ast::TypeNode* elem) {
    if (!handle) { m_currentLLVMValue = nullptr; return; }
    llvm::Value* h64 = handle;
    if (h64->getType()->isIntegerTy() && !h64->getType()->isIntegerTy(64))
        h64 = builder->CreateSExt(h64, int64Type, "chan.toi64");

    if (methodName == "handle") {
        m_currentLLVMValue = h64;
        return;
    }
    if (methodName == "len" || methodName == "free") {
        const char* rn = (methodName == "len") ?
            (isString ? "__vyb_strchan_len" : "__vyb_chan_len") :
            (isString ? "__vyb_strchan_free" : "__vyb_chan_free");
        llvm::Function* f = module->getFunction(rn);
        if (!f) {
            llvm::FunctionType* ft = llvm::FunctionType::get(int64Type, {int64Type}, false);
            f = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, rn, module.get());
        }
        m_currentLLVMValue = builder->CreateCall(f, {h64}, "chan.op");
        return;
    }
    if (methodName == "send") {
        if (node->arguments.size() != 1 || !node->arguments[0]) {
            logError(node->loc, "chan<T>.send expects exactly 1 argument (the payload)");
            m_currentLLVMValue = nullptr;
            return;
        }
        node->arguments[0]->accept(*this);
        llvm::Value* payload = m_currentLLVMValue;
        if (!payload) return;
        if (isString) {
            llvm::Value* dataPtr = payload;
            llvm::Value* dataLen = llvm::ConstantInt::get(int64Type, 0);
            if (payload->getType()->isStructTy()) {
                dataPtr = builder->CreateExtractValue(payload, 0, "ch.strptr");
                dataLen = builder->CreateExtractValue(payload, 1, "ch.strlen");
            }
            llvm::Function* f = module->getFunction("__vyb_strchan_send");
            if (!f) {
                llvm::FunctionType* ft = llvm::FunctionType::get(int64Type, {int64Type, int8PtrType, int64Type}, false);
                f = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "__vyb_strchan_send", module.get());
            }
            m_currentLLVMValue = builder->CreateCall(f, {h64, dataPtr, dataLen}, "chan.sent");
        } else {
            llvm::Function* f = module->getFunction("__vyb_chan_send");
            if (!f) {
                llvm::FunctionType* ft = llvm::FunctionType::get(int64Type, {int64Type, int64Type}, false);
                f = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "__vyb_chan_send", module.get());
            }
            llvm::Value* v64 = encodeChannelScalar(payload, elem);
            m_currentLLVMValue = builder->CreateCall(f, {h64, v64}, "chan.sent");
        }
        return;
    }
    if (methodName == "recv") {
        if (isString) {
            std::vector<llvm::Type*> sf = {int8PtrType, int64Type};
            llvm::StructType* st = llvm::StructType::get(*context, sf, false);
            llvm::Function* f = module->getFunction("__vyb_strchan_recv");
            if (!f) {
                llvm::FunctionType* ft = llvm::FunctionType::get(st, {int64Type}, false);
                f = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "__vyb_strchan_recv", module.get());
            }
            m_currentLLVMValue = builder->CreateCall(f, {h64}, "chan.recv");
        } else {
            llvm::Function* f = module->getFunction("__vyb_chan_recv");
            if (!f) {
                llvm::FunctionType* ft = llvm::FunctionType::get(int64Type, {int64Type}, false);
                f = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "__vyb_chan_recv", module.get());
            }
            llvm::Value* raw = builder->CreateCall(f, {h64}, "chan.recv");
            m_currentLLVMValue = decodeChannelScalar(raw, elem);
        }
        return;
    }
    if (methodName == "poll") {
        if (isString) {
            // String payloads keep the empty-string sentinel poll (existing behavior).
            std::vector<llvm::Type*> sf = {int8PtrType, int64Type};
            llvm::StructType* st = llvm::StructType::get(*context, sf, false);
            llvm::Function* f = module->getFunction("__vyb_strchan_try");
            if (!f) {
                llvm::FunctionType* ft = llvm::FunctionType::get(st, {int64Type}, false);
                f = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "__vyb_strchan_try", module.get());
            }
            m_currentLLVMValue = builder->CreateCall(f, {h64}, "chan.tryed");
            return;
        }
        // Scalar payloads: non-blocking poll reports readiness explicitly (no -1
        // sentinel), returning the native `T?` (present decoded value / absent when
        // empty).
        // This guarantees a Bool channel can't read empty as `true`, a Char channel
        // as 0xFF, or a Float channel as a NaN bit pattern.
        llvm::Function* pf = module->getFunction("__vyb_chan_poll");
        if (!pf) {
            llvm::FunctionType* ft = llvm::FunctionType::get(
                int64Type, {int64Type, llvm::PointerType::get(int64Type, 0)}, false);
            pf = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "__vyb_chan_poll", module.get());
        }
        llvm::AllocaInst* outAlloca = builder->CreateAlloca(int64Type, nullptr, "chan.poll.out");
        llvm::Value* ready = builder->CreateCall(pf, {h64, outAlloca}, "chan.poll.ready");
        llvm::Value* isReady = builder->CreateTrunc(ready, llvm::Type::getInt1Ty(*context), "chan.poll.bool");

        // Native `T?` result: an OptionalType struct `{ payload, bool ready }`
        // (codegenType layout, value at 0 / flag at 1), consumed via
        // `poll() else default` — the native `T?` consuming surface.
        std::unique_ptr<ast::OptionalType> optNode;
        if (elem) {
            optNode = std::make_unique<ast::OptionalType>(
                node->loc, std::unique_ptr<ast::TypeNode>(elem->clone()));
        }
        llvm::StructType* optStruct = nullptr;
        llvm::Type* valueTy = int64Type;
        if (optNode) {
            llvm::Type* t = codegenType(optNode.get());
            if (t) optStruct = llvm::dyn_cast<llvm::StructType>(t);
        }
        if (!optStruct) {
            logError(node->loc, "Failed to resolve T? type for chan poll");
            m_currentLLVMValue = nullptr;
            return;
        }
        valueTy = optStruct->getElementType(0);
        llvm::Value* undefOpt = llvm::UndefValue::get(optStruct);

        llvm::Function* parent = builder->GetInsertBlock()->getParent();
        llvm::BasicBlock* rBlock = llvm::BasicBlock::Create(*context, "chan.poll.ready", parent);
        llvm::BasicBlock* nBlock = llvm::BasicBlock::Create(*context, "chan.poll.none", parent);
        llvm::BasicBlock* merge = llvm::BasicBlock::Create(*context, "chan.poll.merge", parent);
        builder->CreateCondBr(isReady, rBlock, nBlock);

        builder->SetInsertPoint(rBlock);
        llvm::Value* outVal = builder->CreateLoad(int64Type, outAlloca, "chan.poll.val");
        llvm::Value* decoded = decodeChannelScalar(outVal, elem);
        if (decoded->getType() != valueTy) {
            decoded = tryCast(decoded, valueTy, node->loc);
            if (!decoded) { m_currentLLVMValue = nullptr; return; }
        }
        llvm::Value* presentVal = builder->CreateInsertValue(undefOpt, decoded, 0, "opt.v");
        presentVal = builder->CreateInsertValue(presentVal, builder->getInt1(true), 1, "opt.p");
        builder->CreateBr(merge);

        builder->SetInsertPoint(nBlock);
        llvm::Value* absentVal = builder->CreateInsertValue(
            undefOpt, llvm::Constant::getNullValue(valueTy), 0, "opt.empty");
        absentVal = builder->CreateInsertValue(absentVal, builder->getInt1(false), 1, "opt.not");
        builder->CreateBr(merge);

        builder->SetInsertPoint(merge);
        llvm::PHINode* phi = builder->CreatePHI(optStruct, 2, "chan.poll.opt");
        phi->addIncoming(presentVal, rBlock);
        phi->addIncoming(absentVal, nBlock);
        m_currentLLVMValue = phi;
        return;
    }
    logError(node->loc, "Unknown channel method '" + methodName + "'");
    m_currentLLVMValue = nullptr;
}


// Encode a scalar channel payload into the int64 transport slot. Floats are
// stored as their IEEE bit pattern; Bool as 0/1; integer-family types truncate
// to their natural width. The int-slot channel stores all payloads as i64.
llvm::Value* LLVMCodegen::encodeChannelScalar(llvm::Value* payload, const vyb::ast::TypeNode* elem) {
    if (!payload) return nullptr;
    llvm::Type* elemTy = elem ? codegenType(const_cast<vyb::ast::TypeNode*>(elem)) : nullptr;
    if (!elemTy || elemTy == int64Type) return payload;
    if (elemTy->isFloatingPointTy()) {
        // f64 -> i64 (bitcast); f32 -> i32 (bitcast) then zero-extend to i64.
        if (elemTy->getPrimitiveSizeInBits() == 64) {
            return builder->CreateBitCast(payload, int64Type, "chan.enc.bitcast");
        }
        llvm::Type* i32T = llvm::Type::getInt32Ty(*context);
        llvm::Value* asI32 = builder->CreateBitCast(payload, i32T, "chan.enc.f32");
        return builder->CreateZExt(asI32, int64Type, "chan.enc.f32ze");
    }
    if (elemTy->isIntegerTy()) {
        return builder->CreateZExt(payload, int64Type, "chan.enc.zext");
    }
    return payload;
}

// Decode an int64 channel slot back into the payload element type (inverse of
// encodeChannelScalar): bitcast back for Float, truncate for Bool/Char/int-width.
llvm::Value* LLVMCodegen::decodeChannelScalar(llvm::Value* raw, const vyb::ast::TypeNode* elem) {
    if (!raw) return nullptr;
    llvm::Type* elemTy = elem ? codegenType(const_cast<vyb::ast::TypeNode*>(elem)) : nullptr;
    if (!elemTy || elemTy == int64Type) return raw;
    if (elemTy->isFloatingPointTy()) {
        if (elemTy->getPrimitiveSizeInBits() == 64) {
            return builder->CreateBitCast(raw, elemTy, "chan.dec.bitcast");
        }
        llvm::Type* i32T = llvm::Type::getInt32Ty(*context);
        llvm::Value* asI32 = builder->CreateTrunc(raw, i32T, "chan.dec.f32trunc");
        return builder->CreateBitCast(asI32, elemTy, "chan.dec.f32");
    }
    if (elemTy->isIntegerTy()) {
        return builder->CreateTrunc(raw, elemTy, "chan.dec.trunc");
    }
    return raw;
}

} // namespace vyb
