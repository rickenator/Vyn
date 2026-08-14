#include <vyb/parser/ast.hpp>
#include <set>
#include "vyb/vre/llvm/codegen.hpp"
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Verifier.h> // For llvm::verifyFunction

namespace vyb {

// --- Statements ---

// Collect the names of owning bindings whose value is read as a whole value in
// `expr` and therefore flows out to the caller as the return value. Ownership of
// these bindings transfers to the caller, so their cleanup must be suppressed.
// Only whole-value positions are traversed (bare identifiers and select arms);
// call and member-access arguments are excluded because those reads do not
// transfer a local binding (Vec arguments are deep-copied, and member results
// are owned by their base object).
static void collectReturnTransferNames(ast::Expression* expr,
                                       std::set<std::string>& out) {
    if (!expr) return;
    if (auto* ident = dynamic_cast<ast::Identifier*>(expr)) {
        out.insert(ident->name);
        return;
    }
    if (auto* sel = dynamic_cast<ast::SelectExpression*>(expr)) {
        for (const auto& arm : sel->cases) {
            collectReturnTransferNames(arm.second.get(), out);
        }
    }
}

void LLVMCodegen::visit(vyb::ast::BlockStatement* node) {
    // Save the current namedValues for block scoping
    auto oldNamedValues = namedValues;

    // Enter new scope for ownership tracking
    enterScope();

    
    for (size_t i = 0; i < node->body.size(); ++i) {
        const auto& stmt = node->body[i];
        if (stmt) {

            stmt->accept(*this);
        }
        if (builder->GetInsertBlock() && builder->GetInsertBlock()->getTerminator()) {
            // If the block has been terminated (e.g., by a return), stop processing.
            // This can happen if a code path definitely returns.
            // However, subsequent statements might be part of a different path if there was a branch.
            // For a simple sequential block, this is fine.
            // More robust handling might involve checking if the current insert point is reachable.
            break;
        }
    }

    // Always exit scope and cleanup variables, but check if block is terminated
    // If block is terminated (e.g., by return), cleanup must happen before termination
    if (builder->GetInsertBlock() && builder->GetInsertBlock()->getTerminator()) {
        // Block is terminated - can't insert cleanup code here
        // This case should be handled by inserting cleanup before return statements
        
        if (!scopeStack.empty()) {
            scopeStack.pop_back();
        }
    } else {
        // Block not terminated - safe to insert cleanup code
        exitScope();
    }

    // Restore namedValues to outer scope
    namedValues = std::move(oldNamedValues);
}

void LLVMCodegen::emitDeferredStatementsForCurrentFunction() {
    if (!m_deferStack.empty() && !m_deferStack.back().empty()) {
        auto& defers = m_deferStack.back();
        for (auto it = defers.rbegin(); it != defers.rend(); ++it) {
            if (*it) (*it)->accept(*this);
        }
    }
}

void LLVMCodegen::emitPropagatingErrorReturn(llvm::Value* errorPtr) {
    if (!currentFunction || !currentFunctionAST || !currentFunctionAST->needsErrorReturn) {
        return;
    }

    emitDeferredStatementsForCurrentFunction();

    llvm::StructType* returnStructType = llvm::cast<llvm::StructType>(currentFunction->getReturnType());
    llvm::Value* resultStruct = llvm::UndefValue::get(returnStructType);
    llvm::Type* valueType = returnStructType->getElementType(0);
    llvm::Value* dummyValue = llvm::UndefValue::get(valueType);
    resultStruct = builder->CreateInsertValue(resultStruct, dummyValue, {0}, "error.dummy");

    llvm::Type* errorFieldTy = returnStructType->getElementType(1);
    llvm::Value* castError = errorPtr;
    if (castError && castError->getType() != errorFieldTy) {
        castError = builder->CreateBitCast(castError, errorFieldTy, "error.cast");
    }
    resultStruct = builder->CreateInsertValue(resultStruct, castError, {1}, "error.ptr");
    builder->CreateRet(resultStruct);
}

void LLVMCodegen::visit(vyb::ast::ReturnStatement *node) {
    // Emit deferred statements (LIFO) before returning
    emitDeferredStatementsForCurrentFunction();
    if (node->argument) {
        // Codegen the argument expression. The result will be in m_currentLLVMValue.
        node->argument->accept(*this);
        llvm::Value *returnValue = m_currentLLVMValue;

        if (returnValue) {
            // Debug output to see what we're returning
            VYB_CDBG << "DEBUG: ReturnStatement - Type: " << getTypeName(returnValue->getType())
                << ", Function Return Type: " << (currentFunction ? getTypeName(currentFunction->getReturnType()) : "null") << std::endl;
            
            

            // Auto-serialize main() return values when the return type was changed to void.
            // m_mainAutoSerializeOrigRetType is set in cgen_decl.cpp for Bool, Float, and
            // multi-value struct returns from main (Int and Void remain unchanged).
            if (m_mainAutoSerializeOrigRetType && currentFunction &&
                    currentFunction->getName() == "main" &&
                    !isLitIntrinsicCall(node->argument.get())) {
                // Helper: get or declare a simple function (char* → char* or T → char*)
                auto getOrDeclFunc = [&](const std::string& name, llvm::FunctionType* ft) -> llvm::Function* {
                    llvm::Function* f = module->getFunction(name);
                    if (!f) f = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, name, module.get());
                    return f;
                };
                // Helper: get __vyb_string_concat(char*, char*) -> char*
                auto getConcatFn = [&]() -> llvm::Function* {
                    llvm::FunctionType* ft = llvm::FunctionType::get(int8PtrType, {int8PtrType, int8PtrType}, false);
                    return getOrDeclFunc("__vyb_string_concat", ft);
                };
                // Helper: serialize one LLVM value to a JSON char* string
                auto serializeOne = [&](llvm::Value* val, llvm::Type* t) -> llvm::Value* {
                    if (!val || !t) {
                        
                        return builder->CreateGlobalStringPtr("null");
                    }
                    if (t->isIntegerTy(1)) {
                        // Bool → "true" or "false"
                        llvm::FunctionType* ft = llvm::FunctionType::get(int8PtrType, {int1Type}, false);
                        return builder->CreateCall(getOrDeclFunc("__vyb_bool_to_string", ft), {val}, "bool.json");
                    } else if (t->isIntegerTy()) {
                        // Int → number string
                        llvm::Value* v64 = builder->CreateSExtOrTrunc(val, int64Type, "to.i64");
                        llvm::FunctionType* ft = llvm::FunctionType::get(int8PtrType, {int64Type}, false);
                        return builder->CreateCall(getOrDeclFunc("__vyb_int_to_string", ft), {v64}, "int.json");
                    } else if (t->isFloatTy() || t->isDoubleTy()) {
                        // Float → number string
                        llvm::Value* vdbl = t->isFloatTy()
                            ? builder->CreateFPExt(val, doubleType, "to.dbl") : val;
                        llvm::FunctionType* ft = llvm::FunctionType::get(int8PtrType, {doubleType}, false);
                        return builder->CreateCall(getOrDeclFunc("__vyb_float_to_string", ft), {vdbl}, "float.json");
                    } else if (t->isStructTy()) {
                        // Check for Vyb String struct { ptr, i64 } → JSON-quoted string
                        auto* st = llvm::cast<llvm::StructType>(t);
                        if (st->getNumElements() == 2 &&
                                st->getElementType(0)->isPointerTy() &&
                                st->getElementType(1)->isIntegerTy(64)) {
                            llvm::Value* strPtr = builder->CreateExtractValue(val, 0, "str.ptr");
                            llvm::Function* concat = getConcatFn();
                            llvm::Value* openQ  = builder->CreateGlobalStringPtr("\"");
                            llvm::Value* closeQ = builder->CreateGlobalStringPtr("\"");
                            llvm::Value* tmp = builder->CreateCall(concat, {openQ, strPtr}, "str.open");
                            return builder->CreateCall(concat, {tmp, closeQ}, "str.json");
                        }
                    }
                    VYB_CDBG << "DEBUG: serializeOne - unsupported type: " << getTypeName(t)
                              << ", emitting JSON null" << std::endl;
                    return builder->CreateGlobalStringPtr("null");
                };

                llvm::Type* origType = m_mainAutoSerializeOrigRetType;
                llvm::Value* jsonStr = nullptr;

                // For Int types, serialize directly without array handling
                if (origType->isIntegerTy() && !llvm::isa<llvm::ArrayType>(returnValue->getType())) {
                    jsonStr = serializeOne(returnValue, origType);
                } else {
                    // Handle array literal returns in auto-serialization path
                // Handle both ConstantArray/ConstantDataArray values and pointers to arrays
                llvm::ArrayType* arrType = nullptr;
                bool isConstantDataArray = llvm::isa<llvm::ConstantDataArray>(returnValue);
                bool isConstantArray = llvm::isa<llvm::ConstantArray>(returnValue);
                (void)isConstantDataArray; // suppress unused warning
                bool isPtrToArray = false;
                if (isConstantDataArray || isConstantArray) {
                    arrType = llvm::cast<llvm::ArrayType>(returnValue->getType());
                } else if (llvm::isa<llvm::PointerType>(returnValue->getType())) {
                    // Pointer to array: get the pointee type
                    if (auto* ptrTy = llvm::dyn_cast<llvm::PointerType>(returnValue->getType())) {
                        if (auto* pt = llvm::dyn_cast<llvm::ArrayType>(ptrTy->getContainedType(0))) {
                            arrType = pt;
                            isPtrToArray = true;
                        }
                    }
                }
                if (arrType && (llvm::isa<llvm::ConstantArray>(returnValue) || llvm::isa<llvm::ConstantDataArray>(returnValue) || isPtrToArray)) {
                    unsigned elemCount = arrType->getNumElements();
                    llvm::Type* elemType = arrType->getElementType();
                    llvm::Function* concat = getConcatFn();
                    jsonStr = builder->CreateGlobalStringPtr("[");
                    for (unsigned i = 0; i < elemCount; i++) {
                        llvm::Value* elemJson = nullptr;
                        
                        if (isConstantDataArray) {
                            // ConstantDataArray: use getElementAsAPInt for integer elements
                            auto* cda = llvm::cast<llvm::ConstantDataArray>(returnValue);
                            llvm::APInt apVal = cda->getElementAsAPInt(i);
                            llvm::Value* elemVal = llvm::ConstantInt::get(elemType, apVal);
                            if (elemType->isIntegerTy(1)) {
                                llvm::Value* boolVal = builder->CreateICmpNE(elemVal, llvm::ConstantInt::get(int1Type, 0));
                                llvm::FunctionType* ft = llvm::FunctionType::get(int8PtrType, {int1Type}, false);
                                elemJson = builder->CreateCall(getOrDeclFunc("__vyb_bool_to_string", ft), {boolVal});
                            } else if (elemType->isIntegerTy()) {
                                llvm::Value* i64Val = llvm::ConstantInt::get(int64Type, apVal.zext(64));
                                llvm::FunctionType* ft = llvm::FunctionType::get(int8PtrType, {int64Type}, false);
                                elemJson = builder->CreateCall(getOrDeclFunc("__vyb_int_to_string", ft), {i64Val});
                            } else if (elemType->isFloatTy() || elemType->isDoubleTy()) {
                                llvm::Value* dblVal = elemType->isFloatTy()
                                    ? builder->CreateFPExt(elemVal, doubleType, "arr_elem.dbl") : elemVal;
                                llvm::FunctionType* ft = llvm::FunctionType::get(int8PtrType, {doubleType}, false);
                                elemJson = builder->CreateCall(getOrDeclFunc("__vyb_float_to_string", ft), {dblVal});
                            } else {
                                elemJson = builder->CreateGlobalStringPtr("null");
                            }
                        } else if (isConstantArray) {
                            // ConstantArray: use getOperand directly
                            llvm::Constant* elemConst = llvm::cast<llvm::ConstantArray>(returnValue)->getOperand(i);
                            if (elemType->isIntegerTy(1)) {
                                llvm::Value* boolVal = builder->CreateICmpNE(elemConst, llvm::ConstantInt::get(int1Type, 0));
                                llvm::FunctionType* ft = llvm::FunctionType::get(int8PtrType, {int1Type}, false);
                                elemJson = builder->CreateCall(getOrDeclFunc("__vyb_bool_to_string", ft), {boolVal});
                            } else if (elemType->isIntegerTy()) {
                                llvm::Value* i64Val = nullptr;
                                if (auto* ci = llvm::dyn_cast<llvm::ConstantInt>(elemConst)) {
                                    i64Val = llvm::ConstantInt::get(int64Type, ci->getValue().zext(64));
                                } else if (elemConst->getType()->isIntegerTy()) {
                                    i64Val = builder->CreateSExtOrTrunc(elemConst, int64Type, "arr_elem.i64");
                                }
                                if (!i64Val) i64Val = llvm::ConstantInt::get(int64Type, 0);
                                llvm::FunctionType* ft = llvm::FunctionType::get(int8PtrType, {int64Type}, false);
                                elemJson = builder->CreateCall(getOrDeclFunc("__vyb_int_to_string", ft), {i64Val});
                            } else if (elemType->isFloatTy() || elemType->isDoubleTy()) {
                                llvm::Value* dblVal = elemType->isFloatTy() 
                                    ? builder->CreateFPExt(elemConst, doubleType, "arr_elem.dbl") : elemConst;
                                llvm::FunctionType* ft = llvm::FunctionType::get(int8PtrType, {doubleType}, false);
                                elemJson = builder->CreateCall(getOrDeclFunc("__vyb_float_to_string", ft), {dblVal});
                            } else {
                                elemJson = builder->CreateGlobalStringPtr("null");
                            }
                        } else {
                            // Pointer to array: use GEP + Load
                            llvm::Value* idx = builder->getInt32(i);
                            llvm::Value* gep = builder->CreateInBoundsGEP(elemType, returnValue, {idx}, "arr.gep");
                            llvm::Value* elem = builder->CreateLoad(elemType, gep, "arr.load");
                            if (elemType->isIntegerTy(1)) {
                                llvm::Value* boolVal = builder->CreateICmpNE(elem, llvm::ConstantInt::get(int1Type, 0));
                                llvm::FunctionType* ft = llvm::FunctionType::get(int8PtrType, {int1Type}, false);
                                elemJson = builder->CreateCall(getOrDeclFunc("__vyb_bool_to_string", ft), {boolVal});
                            } else if (elemType->isIntegerTy()) {
                                llvm::Value* i64Val = builder->CreateSExtOrTrunc(elem, int64Type, "arr_elem.i64");
                                llvm::FunctionType* ft = llvm::FunctionType::get(int8PtrType, {int64Type}, false);
                                elemJson = builder->CreateCall(getOrDeclFunc("__vyb_int_to_string", ft), {i64Val});
                            } else if (elemType->isFloatTy() || elemType->isDoubleTy()) {
                                llvm::Value* dblVal = elemType->isFloatTy()
                                    ? builder->CreateFPExt(elem, doubleType, "arr_elem.dbl") : elem;
                                llvm::FunctionType* ft = llvm::FunctionType::get(int8PtrType, {doubleType}, false);
                                elemJson = builder->CreateCall(getOrDeclFunc("__vyb_float_to_string", ft), {dblVal});
                            } else {
                                elemJson = builder->CreateGlobalStringPtr("null");
                            }
                        }
                        
                        if (i > 0) {
                            llvm::Value* sep = builder->CreateGlobalStringPtr(", ");
                            jsonStr = builder->CreateCall(concat, {jsonStr, sep});
                        }
                        jsonStr = builder->CreateCall(concat, {jsonStr, elemJson});
                    }
                    llvm::Value* close = builder->CreateGlobalStringPtr("]");
                    jsonStr = builder->CreateCall(concat, {jsonStr, close});
                } else if (llvm::isa<llvm::ArrayType>(returnValue->getType())) {
                    // Loaded array value (not constant, not pointer) — store to temp alloca then GEP
                    auto* arrType = llvm::cast<llvm::ArrayType>(returnValue->getType());
                    unsigned elemCount = arrType->getNumElements();
                    llvm::Type* elemType = arrType->getElementType();
                    llvm::Function* concat = getConcatFn();
                    llvm::Value* arrAlloca = builder->CreateAlloca(arrType, nullptr, "arr_temp");
                    builder->CreateStore(returnValue, arrAlloca);
                    jsonStr = builder->CreateGlobalStringPtr("[");
                    for (unsigned i = 0; i < elemCount; i++) {
                        llvm::Value* idx0 = builder->getInt32(0);
                        llvm::Value* idx1 = builder->getInt32(i);
                        llvm::Value* gep = builder->CreateInBoundsGEP(arrType, arrAlloca, {idx0, idx1}, "arr.gep");
                        llvm::Value* elem = builder->CreateLoad(elemType, gep, "arr.load");
                        llvm::Value* elemJson = nullptr;
                        if (elemType->isIntegerTy(1)) {
                            llvm::Value* boolVal = builder->CreateICmpNE(elem, llvm::ConstantInt::get(int1Type, 0));
                            llvm::FunctionType* ft = llvm::FunctionType::get(int8PtrType, {int1Type}, false);
                            elemJson = builder->CreateCall(getOrDeclFunc("__vyb_bool_to_string", ft), {boolVal});
                        } else if (elemType->isIntegerTy()) {
                            llvm::Value* i64Val = builder->CreateSExtOrTrunc(elem, int64Type, "arr_elem.i64");
                            llvm::FunctionType* ft = llvm::FunctionType::get(int8PtrType, {int64Type}, false);
                            elemJson = builder->CreateCall(getOrDeclFunc("__vyb_int_to_string", ft), {i64Val});
                        } else if (elemType->isFloatTy() || elemType->isDoubleTy()) {
                            llvm::Value* dblVal = elemType->isFloatTy()
                                ? builder->CreateFPExt(elem, doubleType, "arr_elem.dbl") : elem;
                            llvm::FunctionType* ft = llvm::FunctionType::get(int8PtrType, {doubleType}, false);
                            elemJson = builder->CreateCall(getOrDeclFunc("__vyb_float_to_string", ft), {dblVal});
                        } else {
                            elemJson = builder->CreateGlobalStringPtr("null");
                        }
                        if (i > 0) jsonStr = builder->CreateCall(concat, {jsonStr, builder->CreateGlobalStringPtr(", ")});
                        jsonStr = builder->CreateCall(concat, {jsonStr, elemJson});
                    }
                    jsonStr = builder->CreateCall(concat, {jsonStr, builder->CreateGlobalStringPtr("]")});
                } else if (llvm::isa<llvm::PointerType>(returnValue->getType())) {
                    // Fallback: pointer to unknown type — try to load and serialize
                    llvm::Function* concat = getConcatFn();
                    jsonStr = builder->CreateGlobalStringPtr("[");
                    if (origType->isIntegerTy()) {
                        llvm::Value* loaded = builder->CreateLoad(origType, returnValue, "arr.load");
                        jsonStr = serializeOne(loaded, origType);
                    } else if (origType->isFloatTy() || origType->isDoubleTy()) {
                        llvm::Type* loadType = origType->isFloatTy() ? llvm::Type::getFloatTy(*context) : doubleType;
                        llvm::Value* loaded = builder->CreateLoad(loadType, returnValue, "arr.load");
                        jsonStr = serializeOne(loaded, origType);
                    } else if (origType->isStructTy()) {
                        llvm::Value* loaded = builder->CreateLoad(origType, returnValue, "struct.load");
                        auto* st = llvm::cast<llvm::StructType>(origType);
                        unsigned numElems = st->getNumElements();
                        jsonStr = builder->CreateGlobalStringPtr("[");
                        for (unsigned j = 0; j < numElems; j++) {
                            llvm::Value* elem = builder->CreateExtractValue(loaded, {j}, "elem" + std::to_string(j));
                            llvm::Value* elemJson = serializeOne(elem, st->getElementType(j));
                            if (j > 0) jsonStr = builder->CreateCall(concat, {jsonStr, builder->CreateGlobalStringPtr(", ")});
                            jsonStr = builder->CreateCall(concat, {jsonStr, elemJson});
                        }
                        jsonStr = builder->CreateCall(concat, {jsonStr, builder->CreateGlobalStringPtr("]")});
                    } else {
                        jsonStr = builder->CreateGlobalStringPtr("null");
                    }
                } else if (!origType->isStructTy()) {
                    // Single primitive value (Bool or Float)
                    jsonStr = serializeOne(returnValue, origType);
                } else {
                    // Struct (single-element or multi-value tuple): always use JSON array
                    auto* st = llvm::cast<llvm::StructType>(origType);
                    unsigned numElems = st->getNumElements();
                    llvm::Function* concat = getConcatFn();
                    jsonStr = builder->CreateGlobalStringPtr("[");
                    for (unsigned i = 0; i < numElems; i++) {
                        // If returnValue is not actually a struct (type mismatch), serialize directly
                        llvm::Value* elem;
                        if (returnValue->getType()->isStructTy()) {
                            elem = builder->CreateExtractValue(returnValue, {i}, "elem" + std::to_string(i));
                        } else {
                            elem = i == 0 ? returnValue : llvm::ConstantInt::get(st->getElementType(0), 0);
                        }
                        llvm::Value* elemJson = serializeOne(elem, st->getElementType(i));
                        if (i > 0) {
                            llvm::Value* sep = builder->CreateGlobalStringPtr(", ");
                            jsonStr = builder->CreateCall(concat, {jsonStr, sep}, "arr.sep");
                        }
                        jsonStr = builder->CreateCall(concat, {jsonStr, elemJson}, "arr.elem");
                    }
                    llvm::Value* close = builder->CreateGlobalStringPtr("]");
                    jsonStr = builder->CreateCall(concat, {jsonStr, close}, "arr.close");
                    }
                }

                // Print the JSON output
                llvm::Function* printlnFunc = getVybPrintlnFunction();
                if (jsonStr) builder->CreateCall(printlnFunc, {jsonStr});

                // Clean up scope and pop call frame, then return void
                if (!scopeStack.empty()) exitScope();
                generatePopFrameCall();
                builder->CreateRetVoid();
            } else {
                // Not in main function (or plain Int/Void main) - normal return
                VYB_CDBG << "DEBUG: Return value type: " << getTypeName(returnValue->getType())
                          << ", Function return type: " << getTypeName(currentFunction->getReturnType()) << std::endl;

                // CRITICAL: Phase 2 wrapping must happen BEFORE type checking
                // If this is a failable function, we need to wrap the return value
                // in {T, ptr} BEFORE checking type compatibility
                if (currentFunctionAST && currentFunctionAST->needsErrorReturn) {
                    

                    // Create null pointer for error (success case)
                    llvm::Value* nullErrorPtr = llvm::ConstantPointerNull::get(
                        llvm::PointerType::get(*context, 0));

                    // Create the {value, error} struct
                    llvm::StructType* returnStructType = llvm::cast<llvm::StructType>(
                        currentFunction->getReturnType());
                    llvm::Value* resultStruct = llvm::UndefValue::get(returnStructType);
                    resultStruct = builder->CreateInsertValue(resultStruct, returnValue, {0}, "result.value");
                    resultStruct = builder->CreateInsertValue(resultStruct, nullErrorPtr, {1}, "result.error");

                    returnValue = resultStruct;
                    
                }

                // Now check type compatibility (after wrapping if needed)
                if (returnValue->getType() != currentFunction->getReturnType()) {
                    // Special case: returning a single element tuple (Tuple<T>)
                    // If function returns a struct and we have a scalar, wrap it in a struct
                    if (llvm::StructType* structRetType = llvm::dyn_cast<llvm::StructType>(currentFunction->getReturnType())) {
                        if (structRetType->getNumElements() == 1 &&
                            !returnValue->getType()->isStructTy() &&
                            structRetType->getElementType(0) == returnValue->getType()) {
                            

                            // Create a single-element struct
                            llvm::Value* tupleStruct = llvm::UndefValue::get(structRetType);
                            returnValue = builder->CreateInsertValue(tupleStruct, returnValue, {0}, "tuple_wrap");

                            
                        } else {
                            // Try normal cast
                            llvm::Value* castedValue = tryCast(returnValue, currentFunction->getReturnType(), node->loc);
                            if (castedValue) {
                                
                                returnValue = castedValue;
                            } else {
                                // For member expressions (e.g., p.x) load the value if needed
                                if (returnValue->getType()->isPointerTy() &&
                                    !currentFunction->getReturnType()->isPointerTy()) {
                                    

                                    // For loading, we need to know the element type
                                    llvm::Type* elementType = nullptr;
                                    if (llvm::AllocaInst* allocaInst = llvm::dyn_cast<llvm::AllocaInst>(returnValue)) {
                                        elementType = allocaInst->getAllocatedType();
                                    } else {
                                        // Can't determine element type safely, use function return type
                                        elementType = currentFunction->getReturnType();
                                    }

                                    returnValue = builder->CreateLoad(elementType, returnValue, "member_load");
                                    
                                }
                            }
                        }
                    } else {
                        // Not a struct return type, try normal cast
                        llvm::Value* castedValue = tryCast(returnValue, currentFunction->getReturnType(), node->loc);
                        if (castedValue) {
                            
                            returnValue = castedValue;
                        } else {
                            // For member expressions (e.g., p.x) load the value if needed
                            if (returnValue->getType()->isPointerTy() &&
                                !currentFunction->getReturnType()->isPointerTy()) {
                                

                                // For loading, we need to know the element type
                                llvm::Type* elementType = nullptr;
                                if (llvm::AllocaInst* allocaInst = llvm::dyn_cast<llvm::AllocaInst>(returnValue)) {
                                    elementType = allocaInst->getAllocatedType();
                                } else {
                                    // Can't determine element type safely, use function return type
                                    elementType = currentFunction->getReturnType();
                                }

                                returnValue = builder->CreateLoad(elementType, returnValue, "member_load");
                                
                            }
                        }
                    }
                }

                // IMPORTANT: Clean up current block scope before returning
                // This prevents the block scope cleanup from happening after the terminator
                if (!scopeStack.empty()) {
                    
                    // If returning an owning variable, skip its local cleanup because
                    // ownership of that binding is transferred to the caller.
                    if (node->argument) {
                        std::set<std::string> transferNames;
                        collectReturnTransferNames(node->argument.get(), transferNames);
                        for (auto& scopeVars : scopeStack) {
                            for (auto& var : scopeVars) {
                                bool transfersOwnership =
                                    transferNames.count(var.name) &&
                                    (var.isVecWithMallocData ||
                                     var.ownership == ast::OwnershipKind::OUR ||
                                     var.ownership == ast::OwnershipKind::MILD);
                                if (transfersOwnership) {
                                    var.needsCleanup = false;
                                    var.isVecWithMallocData = false;
                                }
                            }
                        }
                    }
                    exitScope();
                }

                // Phase 6.4: Pop call frame before return
                generatePopFrameCall();

                // Return the value (already wrapped if needed)
                
                
                // If the function is declared void, discard the return value and emit ret void
                if (currentFunction->getReturnType()->isVoidTy()) {
                    builder->CreateRetVoid();
                } else if (returnValue->getType() != currentFunction->getReturnType()) {
                    // Type mismatch: handle gracefully
                    if (currentFunction && currentFunction->getName() == "main") {
                        // For main(), auto-serialize the mismatched return value
                        

                        // Define helpers inline
                        auto getOrDeclFunc = [&](const std::string& name, llvm::FunctionType* ft) -> llvm::Function* {
                            llvm::Function* f = module->getFunction(name);
                            if (!f) f = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, name, module.get());
                            return f;
                        };
                        auto getConcatFn = [&]() -> llvm::Function* {
                            llvm::FunctionType* ft = llvm::FunctionType::get(int8PtrType, {int8PtrType, int8PtrType}, false);
                            return getOrDeclFunc("__vyb_string_concat", ft);
                        };
                        auto serializeOne = [&](llvm::Value* val, llvm::Type* t) -> llvm::Value* {
                            if (!val || !t) return builder->CreateGlobalStringPtr("null");
                            if (t->isIntegerTy(1)) {
                                llvm::FunctionType* ft = llvm::FunctionType::get(int8PtrType, {int1Type}, false);
                                return builder->CreateCall(getOrDeclFunc("__vyb_bool_to_string", ft), {val}, "bool.json");
                            } else if (t->isIntegerTy()) {
                                llvm::Value* v64 = builder->CreateSExtOrTrunc(val, int64Type, "to.i64");
                                llvm::FunctionType* ft = llvm::FunctionType::get(int8PtrType, {int64Type}, false);
                                return builder->CreateCall(getOrDeclFunc("__vyb_int_to_string", ft), {v64}, "int.json");
                            } else if (t->isFloatTy() || t->isDoubleTy()) {
                                llvm::Value* vdbl = t->isFloatTy() ? builder->CreateFPExt(val, doubleType, "to.dbl") : val;
                                llvm::FunctionType* ft = llvm::FunctionType::get(int8PtrType, {doubleType}, false);
                                return builder->CreateCall(getOrDeclFunc("__vyb_float_to_string", ft), {vdbl}, "float.json");
                            }
                            return builder->CreateGlobalStringPtr("null");
                        };

                        llvm::Function* concat = getConcatFn();
                        llvm::Value* jsonStr = nullptr;

                        // Handle array types (including ConstantArray)
                        if (llvm::isa<llvm::ArrayType>(returnValue->getType())) {
                            llvm::ArrayType* arrType = llvm::cast<llvm::ArrayType>(returnValue->getType());
                            unsigned elemCount = arrType->getNumElements();
                            llvm::Type* elemType = arrType->getElementType();
                            jsonStr = builder->CreateGlobalStringPtr("[");
                            for (unsigned i = 0; i < elemCount; i++) {
                                // Get element via GEP from the array value
                                llvm::Value* idx = builder->getInt32(i);
                                llvm::Value* gep = builder->CreateInBoundsGEP(elemType, returnValue, {idx}, "arr.gep");
                                llvm::Value* elem = builder->CreateLoad(elemType, gep, "arr.load");
                                llvm::Value* elemJson = serializeOne(elem, elemType);
                                if (i > 0) {
                                    llvm::Value* sep = builder->CreateGlobalStringPtr(", ");
                                    jsonStr = builder->CreateCall(concat, {jsonStr, sep});
                                }
                                jsonStr = builder->CreateCall(concat, {jsonStr, elemJson});
                            }
                            llvm::Value* close = builder->CreateGlobalStringPtr("]");
                            jsonStr = builder->CreateCall(concat, {jsonStr, close});
                        } else {
                            // Non-array: use serializeOne
                            jsonStr = serializeOne(returnValue, returnValue->getType());
                        }

                        if (jsonStr) builder->CreateCall(getVybPrintlnFunction(), {jsonStr});
                        if (!scopeStack.empty()) exitScope();
                        generatePopFrameCall();
                        builder->CreateRetVoid();
                    } else {
                        // Non-main function: emit null return to avoid crash
                        
                        builder->CreateRet(llvm::ConstantPointerNull::get(llvm::PointerType::get(*context, 0)));
                    }
                } else {
                    builder->CreateRet(returnValue);
                }
            }
        } else {
            // Error during argument codegen or argument is null expression (should not happen for valid AST)
            // TODO: Report error (Return argument codegen failed or resulted in null)
            if (currentFunction && currentFunction->getReturnType()->isVoidTy()) {
                // IMPORTANT: Clean up current block scope before returning
                if (!scopeStack.empty()) {
                    
                    exitScope();
                }
                // Phase 6.4: Pop call frame before return
                generatePopFrameCall();
                builder->CreateRetVoid();
            } else if (currentFunction) {
                // Return undef if function expects a non-void type and codegen failed
                // IMPORTANT: Clean up current block scope before returning
                if (!scopeStack.empty()) {
                    
                    exitScope();
                }
                // Phase 6.4: Pop call frame before return
                generatePopFrameCall();
                builder->CreateRet(llvm::UndefValue::get(currentFunction->getReturnType()));
                logError(node->loc, "Return expression codegen failed, returning undef.");
            }
        }
    } else {
        // No argument, so it's a void return
        // IMPORTANT: Clean up current block scope before returning
        if (!scopeStack.empty()) {
            
            exitScope();
        }
        // Phase 6.4: Pop call frame before return
        generatePopFrameCall();
        if (currentFunctionAST && currentFunctionAST->needsErrorReturn) {
            // Failable void functions use a uniform 2-field tuple ABI: {i1 dummy, i8* err}.
            llvm::StructType* returnStructType = llvm::cast<llvm::StructType>(currentFunction->getReturnType());
            llvm::Value* nullErrorPtr = llvm::ConstantPointerNull::get(llvm::PointerType::get(*context, 0));
            llvm::Value* successStruct = llvm::UndefValue::get(returnStructType);
            successStruct = builder->CreateInsertValue(successStruct, llvm::ConstantInt::getFalse(*context), {0}, "result.void_dummy");
            successStruct = builder->CreateInsertValue(successStruct, nullErrorPtr, {1}, "result.error");
            builder->CreateRet(successStruct);
        } else if (currentAsyncState.isAsync) {
            // Async function with no return value: return null Future pointer
            builder->CreateRet(llvm::ConstantPointerNull::get(llvm::PointerType::get(*context, 0)));
        } else {
            builder->CreateRetVoid();
        }
    }
}

void LLVMCodegen::visit(vyb::ast::ExpressionStatement* node) {

    if (node->expression) {
        node->expression->accept(*this);
        // The value of the expression is m_currentLLVMValue, but it's not used by the statement itself.
    }
    if (!inTrapHandler) {
        m_currentLLVMValue = nullptr; // Expression statement doesn't produce a value for further expressions
    }
}

void LLVMCodegen::visit(vyb::ast::IfStatement* node) {
    node->test->accept(*this);
    llvm::Value* conditionValue = m_currentLLVMValue;
    if (!conditionValue) {
        logError(node->loc, "Condition of if statement is null.");
        return;
    }

    // Convert condition to i1 if necessary
    if (conditionValue->getType()->isPointerTy()) {
        // Treat pointer as true if not null, false if null
        conditionValue = builder->CreateIsNotNull(conditionValue, "ptrcond");
    } else if (conditionValue->getType()->isIntegerTy() && conditionValue->getType() != int1Type) {
        // Treat non-zero integer as true, zero as false
        conditionValue = builder->CreateICmpNE(
            conditionValue,
            llvm::ConstantInt::get(conditionValue->getType(), 0),
            "intcond_tobool"
        );
    } else if (conditionValue->getType() != int1Type) {
         // If not boolean, pointer, or integer, it's an error.
         logError(node->test->loc, "If condition is not boolean or convertible to boolean (type: " + getTypeName(conditionValue->getType()) + "). Treating as false.");
         conditionValue = llvm::ConstantInt::get(int1Type, 0); // Treat as false to prevent IR errors
    }


    llvm::Function* parentFunction = builder->GetInsertBlock()->getParent();
    if (!parentFunction) { // Should not happen if we are generating code inside a function
        logError(node->loc, "Cannot create blocks for if statement: not in a function.");
        return;
    }

    llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(*context, "then", parentFunction);
    llvm::BasicBlock* elseBB = nullptr;
    llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*context, "ifcont"); // Don't add to function yet, might not be needed if all paths return

    if (node->alternate) {
        elseBB = llvm::BasicBlock::Create(*context, "else", parentFunction);
        builder->CreateCondBr(conditionValue, thenBB, elseBB);
    } else {
        builder->CreateCondBr(conditionValue, thenBB, mergeBB);
    }

    // Emit then block
    builder->SetInsertPoint(thenBB);
    node->consequent->accept(*this);
    if (!builder->GetInsertBlock()->getTerminator()) { // If 'then' doesn't end with a return/break
        builder->CreateBr(mergeBB);
    }
    // thenBB = builder->GetInsertBlock(); // Update thenBB to the actual end block of the 'then' part

    // Emit else block
    if (node->alternate) {
        // parentFunction->getBasicBlockList().push_back(elseBB); // Create already adds it
        builder->SetInsertPoint(elseBB);
        node->alternate->accept(*this);
        if (!builder->GetInsertBlock()->getTerminator()) { // If 'else' doesn't end with a return/break
            builder->CreateBr(mergeBB);
        }
        // elseBB = builder->GetInsertBlock(); // Update elseBB
    }

    // If mergeBB is not used by any predecessors (e.g. both then and else return), it can be removed.
    // However, LLVM's dead code elimination should handle this.
    // We must add mergeBB to the function if it has predecessors.
    if (!mergeBB->use_empty() || (!node->alternate && thenBB->getTerminator() && !llvm::isa<llvm::ReturnInst>(thenBB->getTerminator()))) { // if no alternate, and thenBB doesn't return, it must branch to mergeBB
        // parentFunction->getBasicBlockList().push_back(mergeBB); // OLD - private member
        if (!mergeBB->getParent()) { // Only insert if not already part of a function
             mergeBB->insertInto(parentFunction);
        }
        builder->SetInsertPoint(mergeBB);
    } else {
        // mergeBB was never added to the function, so we can't call eraseFromParent()
        // Just let it be garbage collected or explicitly delete it
        if (mergeBB->getParent()) {
            mergeBB->eraseFromParent();
        } else {
            // mergeBB will be automatically cleaned up when it goes out of scope
            // since it was never added to the function
        }
    }

    // If IfStatement were an expression, a PHI node would be needed here.
    // For now, IfStatement does not produce a value.
    m_currentLLVMValue = nullptr;
}

void LLVMCodegen::visit(vyb::ast::WhileStatement* node) {
    llvm::Function* parentFunc = builder->GetInsertBlock()->getParent();
    if (!parentFunc) {
        logError(node->loc, "Cannot create while loop: not in a function.");
        m_currentLLVMValue = nullptr;
        return;
    }

    llvm::BasicBlock* loopHeaderBB = llvm::BasicBlock::Create(*context, "loop.header", parentFunc);
    llvm::BasicBlock* loopBodyBB = llvm::BasicBlock::Create(*context, "loop.body", parentFunc);
    llvm::BasicBlock* loopExitBB = llvm::BasicBlock::Create(*context, "loop.exit", parentFunc);

    // Jump to header
    builder->CreateBr(loopHeaderBB);

    // Populate header: condition check
    builder->SetInsertPoint(loopHeaderBB);
    node->test->accept(*this); // Evaluate condition
    llvm::Value* condVal = m_currentLLVMValue;
    if (!condVal) {
        logError(node->test->loc, "While loop condition is null.");
        // Treat as false and branch to exit to avoid crashing
        builder->CreateBr(loopExitBB);
    } else {
        if (condVal->getType() != int1Type) { // Ensure condition is i1
            // Convert to boolean if necessary (e.g., integer to bool: 0 is false, non-0 is true)
            if (condVal->getType()->isPointerTy()) {
                condVal = builder->CreateIsNotNull(condVal, "ptrcond_while");
            } else if (condVal->getType()->isIntegerTy()) {
                condVal = builder->CreateICmpNE(condVal, llvm::Constant::getNullValue(condVal->getType()), "whilecond_tobool");
            } else {
                logError(node->test->loc, "While loop condition is not boolean or convertible to boolean. Treating as false.");
                condVal = llvm::ConstantInt::get(int1Type, 0);
            }
        }
        builder->CreateCondBr(condVal, loopBodyBB, loopExitBB);
    }

    // Populate body
    builder->SetInsertPoint(loopBodyBB);
    // The LoopContext struct in codegen.hpp is {llvm::BasicBlock *loopHeader, *loopBody, *loopUpdate, *loopExit;}
    // For a 'while' loop, the 'update' block is effectively the header where the condition is re-evaluated.
    pushLoop(loopHeaderBB, loopBodyBB, loopHeaderBB /*update is header for while*/, loopExitBB);
    node->body->accept(*this); // Generate loop body
    popLoop(); // Pop loop context

    if (!builder->GetInsertBlock()->getTerminator()) { // If body doesn't end with break/return
        builder->CreateBr(loopHeaderBB); // Jump back to header
    }

    // Continue codegen in the exit block
    builder->SetInsertPoint(loopExitBB);
    m_currentLLVMValue = nullptr; // While statement itself doesn't produce a value
}

void LLVMCodegen::visit(vyb::ast::PassStatement* node) {
    // Pass statement is used inside select expression or match block arms to
    // yield a value. It stores into the enclosing expression's result slot and
    // branches to its end block.
    if (yieldContextStack_.empty()) {
        logError(node->loc, "Pass statement can only be used inside select/match expression blocks.");
        m_currentLLVMValue = nullptr;
        return;
    }

    YieldContext& currentYield = yieldContextStack_.back();

    // Codegen the pass value
    if (node->argument) {
        node->argument->accept(*this);
        llvm::Value* passValue = m_currentLLVMValue;

        // During type inference, just keep the value and return
        if (infer_types_only) {
            m_currentLLVMValue = passValue;
            return;
        }

        if (passValue && currentYield.resultAlloca) {
            // Store the value in the result alloca (wrapping char* -> String if needed)
            storeIntoResultSlot(passValue, currentYield.resultAlloca, node->loc);

            // Branch to the end block
            builder->CreateBr(currentYield.endBlock);
        } else {
            logError(node->loc, "Failed to generate code for pass value.");
        }
    } else {
        logError(node->loc, "Pass statement requires an expression.");
    }

    m_currentLLVMValue = nullptr;
}

void LLVMCodegen::visit(vyb::ast::BreakStatement* node) {
    if (loopStack.empty()) {
        logError(node->loc, "Break statement outside of a loop.");
        m_currentLLVMValue = nullptr;
        return;
    }
    LoopContext& currentLoop = loopStack.back();
    // Member name is loopExit based on struct LoopContext definition in codegen.hpp
    if (!currentLoop.loopExit) {
         logError(node->loc, "Invalid loop context: exit block is null for break.");
         m_currentLLVMValue = nullptr;
         return;
    }
    builder->CreateBr(currentLoop.loopExit);
    m_currentLLVMValue = nullptr;
    // Note: After a break, the current block is terminated.
    // We might need to create a new block if code generation is supposed to continue after the break
    // in the same scope, but typically break is the last thing in its path.
    // For simplicity, we assume subsequent code is unreachable or handled by block structure.
}

void LLVMCodegen::visit(vyb::ast::ContinueStatement* node) {
    if (loopStack.empty()) {
        logError(node->loc, "Continue statement outside of a loop.");
        m_currentLLVMValue = nullptr;
        return;
    }
    LoopContext& currentLoop = loopStack.back();
    // Member name is loopUpdate based on struct LoopContext definition in codegen.hpp
     if (!currentLoop.loopUpdate) {
         logError(node->loc, "Invalid loop context: update/header block is null for continue.");
         m_currentLLVMValue = nullptr;
         return;
    }
    builder->CreateBr(currentLoop.loopUpdate);
    m_currentLLVMValue = nullptr;
    // Similar to break, continue terminates the current path in the block.
}

void LLVMCodegen::visit(vyb::ast::ForStatement* node) {
    llvm::Function* parentFunc = builder->GetInsertBlock()->getParent();
    if (!parentFunc) {
        logError(node->loc, "Cannot create for loop: not in a function.");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Create blocks for the loop parts
    llvm::BasicBlock* initBB = llvm::BasicBlock::Create(*context, "for.init", parentFunc);
    llvm::BasicBlock* condBB = llvm::BasicBlock::Create(*context, "for.cond", parentFunc);
    llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(*context, "for.body", parentFunc);
    llvm::BasicBlock* updateBB = llvm::BasicBlock::Create(*context, "for.update", parentFunc);
    llvm::BasicBlock* exitBB = llvm::BasicBlock::Create(*context, "for.exit", parentFunc);

    // Initializer block
    builder->CreateBr(initBB);
    builder->SetInsertPoint(initBB);
    if (node->init) {
        node->init->accept(*this); // Generate initializer code
    }
    builder->CreateBr(condBB); // Fall through to condition check

    // Condition block
    builder->SetInsertPoint(condBB);
    if (node->test) {
        node->test->accept(*this); // Evaluate condition
        llvm::Value* condVal = m_currentLLVMValue;
        if (!condVal) {
            logError(node->test->loc, "For loop condition is null. Treating as false.");
            condVal = llvm::ConstantInt::get(int1Type, 0);
        }
        if (condVal->getType() != int1Type) { // Ensure condition is i1
             if (condVal->getType()->isPointerTy()) {
                condVal = builder->CreateIsNotNull(condVal, "ptrcond_for");
            } else if (condVal->getType()->isIntegerTy()) {
                condVal = builder->CreateICmpNE(condVal, llvm::Constant::getNullValue(condVal->getType()), "forcond_tobool");
            } else {
                logError(node->test->loc, "For loop condition is not boolean or convertible. Treating as false.");
                condVal = llvm::ConstantInt::get(int1Type, 0);
            }
        }
        builder->CreateCondBr(condVal, bodyBB, exitBB);
    } else {
        // No condition means infinite loop (or until break)
        builder->CreateBr(bodyBB);
    }

    // Body block
    builder->SetInsertPoint(bodyBB);
    pushLoop(condBB, bodyBB, updateBB, exitBB);
    node->body->accept(*this); // Generate loop body
    popLoop();
    if (!builder->GetInsertBlock()->getTerminator()) { // If body doesn't end with break/return
        builder->CreateBr(updateBB); // Jump to update block
    }

    // Update block
    builder->SetInsertPoint(updateBB);
    if (node->update) {
        node->update->accept(*this); // Generate update code
    }
    builder->CreateBr(condBB); // Jump back to condition check

    // Exit block
    builder->SetInsertPoint(exitBB);
    m_currentLLVMValue = nullptr; // For statement itself doesn't produce a value
}

void LLVMCodegen::visit(vyb::ast::TryStatement* node) {
    // NOTE: This is a simplified implementation without actual exception handling (e.g., landing pads).
    // It will execute the try block, and if a finally block exists, it will execute it.
    // Catch block is ignored for now as proper C++ style exception handling is complex in LLVM IR
    // without specific runtime support or a personality function.

    logError(node->loc, "TryStatement codegen is currently a stub and does not handle exceptions.");

    llvm::Function* parentFunc = builder->GetInsertBlock()->getParent();
    if (!parentFunc) {
        logError(node->loc, "Cannot create try-finally: not in a function.");
        m_currentLLVMValue = nullptr;
        return;
    }

    llvm::BasicBlock* tryBB = llvm::BasicBlock::Create(*context, "try.block", parentFunc);
    llvm::BasicBlock* finallyBB = nullptr;
    llvm::BasicBlock* contBB = llvm::BasicBlock::Create(*context, "try.cont", parentFunc);

    builder->CreateBr(tryBB);
    builder->SetInsertPoint(tryBB);
    if (node->tryBlock) {
        node->tryBlock->accept(*this);
    }
    // If try block did not terminate, branch to finally or continue
    if (!builder->GetInsertBlock()->getTerminator()) {
        if (node->finallyBlock) {
            finallyBB = llvm::BasicBlock::Create(*context, "finally.block", parentFunc);
            builder->CreateBr(finallyBB);
        } else {
            builder->CreateBr(contBB);
        }
    }

    if (node->catchBlock) {
        logError(node->loc, "Catch blocks in TryStatement are not yet supported in codegen.");
        // To make it somewhat valid, if there was a catch block, we might need another path.
        // For now, it's ignored.
    }

    if (node->finallyBlock) {
        if (!finallyBB) { // Should have been created if node->finallyBlock exists
             finallyBB = llvm::BasicBlock::Create(*context, "finally.block", parentFunc);
        }
        builder->SetInsertPoint(finallyBB);
        node->finallyBlock->accept(*this);
        // If finally block did not terminate, branch to continue block
        if (!builder->GetInsertBlock()->getTerminator()) {
            builder->CreateBr(contBB);
        }
    }

    builder->SetInsertPoint(contBB);
    m_currentLLVMValue = nullptr; // Try statement itself doesn't produce a value
}

void LLVMCodegen::visit(vyb::ast::UnsafeStatement* node) {
    // For LLVM codegen, an freedom block doesn't typically translate to specific LLVM instructions.
    // Its purpose is to bypass semantic checks in the Vyb language itself.
    // So, we just visit the inner block.
    if (node->block) {
        node->block->accept(*this);
    }
    // An freedom statement, like a regular block, doesn't produce a value for further expressions.
    m_currentLLVMValue = nullptr;
}

void LLVMCodegen::visit(vyb::ast::EmptyStatement* node) {
    // EmptyStatement doesn't produce any code or value
    // It's essentially a no-op in the LLVM IR
    m_currentLLVMValue = nullptr;
}

void LLVMCodegen::visit(vyb::ast::ThrowStatement* node) {
    // TODO: Old throw statement - consider deprecating in favor of fail
    // Get the current function
    llvm::Function* function = getCurrentFunction();
    if (!function) {
        logError(node->loc, "Throw statement outside function context");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Evaluate the expression to throw
    if (!node->expr) {
        logError(node->loc, "Throw statement missing expression");
        m_currentLLVMValue = nullptr;
        return;
    }

    node->expr->accept(*this);
    llvm::Value* exceptionValue = m_currentLLVMValue;
    if (!exceptionValue) {
        logError(node->expr->loc, "Failed to evaluate throw expression");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Get exception object type info if available
    // For now, we'll assume all exceptions are compatible with a common exception interface

    // For basic implementation, we'll call a runtime function to handle the exception
    std::vector<llvm::Type*> throwFuncParamTypes = {
        llvm::PointerType::get(*context, 0) // Generic pointer to exception object
    };

    llvm::FunctionType* throwFuncType = llvm::FunctionType::get(
        llvm::Type::getVoidTy(*context),
        throwFuncParamTypes,
        false
    );

    // Get or create the throw function
    llvm::Function* throwFunc = module->getFunction("__vyb_throw_exception");
    if (!throwFunc) {
        throwFunc = llvm::Function::Create(
            throwFuncType,
            llvm::Function::ExternalLinkage,
            "__vyb_throw_exception",
            module.get()
        );
    }

    // Cast exception value to void* if necessary
    llvm::Value* exceptionPtr = exceptionValue;
    if (!exceptionPtr->getType()->isPointerTy()) {
        // If the exception isn't already a pointer, store it in memory
        llvm::AllocaInst* temp = builder->CreateAlloca(
            exceptionValue->getType(),
            nullptr,
            "exception.tmp"
        );
        builder->CreateStore(exceptionValue, temp);
        exceptionPtr = temp;
    }

    // Cast to i8* (void*)
    exceptionPtr = builder->CreateBitCast(
        exceptionPtr,
        llvm::PointerType::get(*context, 0),
        "exception.i8ptr"
    );

    // Call the throw function
    builder->CreateCall(throwFunc, { exceptionPtr });

    // After throwing, execution doesn't continue
    builder->CreateUnreachable();

    // Create a new block for any following code (though it will be unreachable)
    llvm::BasicBlock* unreachableBB = llvm::BasicBlock::Create(*context, "after.throw", function);
    builder->SetInsertPoint(unreachableBB);

    // Throw doesn't produce a value
    m_currentLLVMValue = nullptr;

    logWarning(node->loc, "ThrowStatement implemented with basic functionality. Full exception handling support requires additional runtime support.");
}

void LLVMCodegen::codegenMatch(vyb::ast::MatchStatement* node, llvm::AllocaInst* resultAlloca) {
    // Get the current function
    llvm::Function* function = getCurrentFunction();
    if (!function) {
        logError(node->loc, "Match statement outside function context");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Evaluate the expression to match
    if (!node->expr) {
        logError(node->loc, "Match statement missing expression");
        m_currentLLVMValue = nullptr;
        return;
    }

    node->expr->accept(*this);
    llvm::Value* matchValue = m_currentLLVMValue;
    if (!matchValue) {
        logError(node->expr->loc, "Failed to evaluate match expression");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Store match value in a temporary variable if not already a simple value
    // This prevents re-evaluation if the expression has side effects
    llvm::AllocaInst* matchTemp = nullptr;
    if (!matchValue->getType()->isIntegerTy() &&
        !matchValue->getType()->isFloatingPointTy() &&
        !matchValue->getType()->isPointerTy()) {
        matchTemp = builder->CreateAlloca(
            matchValue->getType(),
            nullptr,
            "match.value"
        );
        builder->CreateStore(matchValue, matchTemp);
        matchValue = builder->CreateLoad(matchTemp->getAllocatedType(), matchTemp, "match.value.load");
    }

    // Create basic blocks for each case and the end of match
    llvm::BasicBlock* defaultBB = nullptr;
    llvm::BasicBlock* endMatchBB = llvm::BasicBlock::Create(*context, "match.end"); // Don't add to function yet

    // For a value-returning match expression, expose a yield context so that
    // `pass` inside a block arm stores into the shared result slot and branches
    // to the end block.
    if (resultAlloca) {
        yieldContextStack_.push_back(YieldContext{endMatchBB, resultAlloca});
    }

    std::vector<llvm::BasicBlock*> caseBBs;
    std::vector<llvm::BasicBlock*> caseBodyBBs;

    // Create basic blocks for all cases
    for (size_t i = 0; i < node->cases.size(); i++) {
        llvm::BasicBlock* caseBB = llvm::BasicBlock::Create(
            *context,
            "match.case." + std::to_string(i),
            function
        );
        caseBBs.push_back(caseBB);

        llvm::BasicBlock* caseBodyBB = llvm::BasicBlock::Create(
            *context,
            "match.case.body." + std::to_string(i),
            function
        );
        caseBodyBBs.push_back(caseBodyBB);
    }

    // Create default case if needed
    defaultBB = llvm::BasicBlock::Create(*context, "match.default", function);

    // Build the initial branches for pattern matching
    llvm::BasicBlock* nextCaseBB = caseBBs[0];
    builder->CreateBr(nextCaseBB);

    // Check if there's a wildcard pattern (nullptr) in the cases
    bool hasWildcard = false;
    for (size_t ci = 0; ci < node->cases.size(); ++ci) {
        // A guarded wildcard is not exhaustive, so it does not count as an
        // unconditional catch-all for the default block.
        if (!node->cases[ci].first && !(ci < node->guards.size() && node->guards[ci])) {
            hasWildcard = true;
            break;
        }
    }

    // If the matched value is a tagged-union enum (data-carrying), variant
    // patterns such as `Circle(r)` or `Unit` dispatch on the runtime tag.
    const TaggedEnumInfo* matchedEnum = (matchValue && matchValue->getType()->isStructTy())
        ? ((node->expr && node->expr->type) ? findTaggedEnum(node->expr->type.get()) : nullptr)
        : nullptr;
    if (!matchedEnum && matchValue) matchedEnum = findTaggedEnum(matchValue->getType());

    // Exhaustiveness: a match is exhaustive when it has an unguarded wildcard or,
    // for a tagged-union enum, arms that cover every variant. An exhaustive match
    // has an impossible default (no-match) path, so its default block is marked
    // `unreachable`; this lets a function whose arms all `return` compile cleanly
    // without a spurious "may not return on all paths" diagnostic.
    if (matchedEnum && !hasWildcard) {
        std::set<std::string> covered;
        for (size_t i = 0; i < node->cases.size(); ++i) {
            auto& pat = node->cases[i].first;
            if (!pat) { hasWildcard = true; break; }
            // A guarded arm covers only the subset where its guard is true, so
            // it does not establish unconditional coverage of its variant.
            bool guarded = (i < node->guards.size() && node->guards[i]);
            if (guarded) continue;
            std::string vname;
            if (auto* ctor = dynamic_cast<ast::ConstructionExpression*>(pat.get())) {
                auto* tv = ctor->constructedType
                    ? dynamic_cast<ast::TypeName*>(ctor->constructedType.get()) : nullptr;
                if (tv && tv->identifier) vname = tv->identifier->name;
            } else if (auto* idp = dynamic_cast<ast::Identifier*>(pat.get())) {
                vname = idp->name;
            }
            if (!vname.empty()) covered.insert(vname);
        }
        if (covered.size() == matchedEnum->variantTags.size()) {
            hasWildcard = true; // exhaustive: the no-match default is unreachable
        }
    }

    // Generate code for each case
    for (size_t i = 0; i < node->cases.size(); i++) {
        // Set insertion point to this case's pattern matching block
        builder->SetInsertPoint(caseBBs[i]);

        // Get the case pattern and body
        auto& casePattern = node->cases[i].first;
        auto& caseBody = node->cases[i].second;

        // Default pattern (underscore/wildcard) just branches to the body
        const ast::StructPattern* pendingStructPattern = nullptr;
        // Variant pattern holder: when a case matches a data-carrying enum
        // variant, extract its payload fields into these named bindings.
        llvm::StructType* pendingVariantPayloadTy = nullptr;
        std::vector<std::string> pendingVariantBindings;
        if (!casePattern) {
            builder->CreateBr(caseBodyBBs[i]);
        } else {
            // Check if this is a comparison pattern (e.g., >= 18, < 0)
            bool isComparisonPattern = (casePattern->getType() == vyb::ast::NodeType::COMPARISON_PATTERN);

            llvm::Value* isMatch = nullptr;

            if (auto* sp = dynamic_cast<ast::StructPattern*>(casePattern.get())) {
                // Struct destructuring: the matched value must be a struct. Vyb
                // statically types structs (no runtime type tags), so a struct
                // pattern always matches; its fields are bound in the case body.
                if (!matchValue->getType()->isStructTy()) {
                    logError(sp->loc, "Struct destructuring pattern requires a struct match value");
                    isMatch = llvm::ConstantInt::getFalse(*context);
                } else {
                    isMatch = llvm::ConstantInt::getTrue(*context);
                    pendingStructPattern = sp;
                }
            } else if (auto* range = dynamic_cast<ast::RangeExpression*>(casePattern.get())) {
                // Range pattern `start..end` (inclusive): matchValue in [start, end].
                if (!matchValue->getType()->isIntegerTy() &&
                    !matchValue->getType()->isFloatingPointTy()) {
                    logError(range->loc, "Range pattern requires integer or float values");
                    isMatch = llvm::ConstantInt::getFalse(*context);
                } else {
                    bool isInt = matchValue->getType()->isIntegerTy();
                    range->start->accept(*this);
                    llvm::Value* startValue = m_currentLLVMValue;
                    range->end->accept(*this);
                    llvm::Value* endValue = m_currentLLVMValue;
                    if (!startValue || !endValue) {
                        logError(range->loc, "Failed to evaluate range pattern bounds");
                        isMatch = llvm::ConstantInt::getFalse(*context);
                    } else {
                        llvm::Value* ge = isInt
                            ? builder->CreateICmpSGE(matchValue, startValue, "match.range.ge")
                            : builder->CreateFCmpOGE(matchValue, startValue, "match.range.fge");
                        llvm::Value* le = isInt
                            ? builder->CreateICmpSLE(matchValue, endValue, "match.range.le")
                            : builder->CreateFCmpOLE(matchValue, endValue, "match.range.fle");
                        isMatch = builder->CreateAnd(ge, le, "match.range.and");
                    }
                }
            } else if (isComparisonPattern) {
                // Handle comparison pattern
                auto* compPattern = static_cast<vyb::ast::ComparisonPattern*>(casePattern.get());

                // Evaluate the comparison value
                compPattern->value->accept(*this);
                llvm::Value* patternValue = m_currentLLVMValue;
                if (!patternValue) {
                    logError(compPattern->value->loc, "Failed to evaluate comparison pattern value");
                    m_currentLLVMValue = nullptr;
                    return;
                }

                // Perform the comparison based on the operator
                if (matchValue->getType()->isIntegerTy() && patternValue->getType()->isIntegerTy()) {
                    // Integer comparison
                    switch (compPattern->op.type) {
                        case vyb::TokenType::LT:
                            isMatch = builder->CreateICmpSLT(matchValue, patternValue, "match.cmp.lt");
                            break;
                        case vyb::TokenType::LTEQ:
                            isMatch = builder->CreateICmpSLE(matchValue, patternValue, "match.cmp.le");
                            break;
                        case vyb::TokenType::GT:
                            isMatch = builder->CreateICmpSGT(matchValue, patternValue, "match.cmp.gt");
                            break;
                        case vyb::TokenType::GTEQ:
                            isMatch = builder->CreateICmpSGE(matchValue, patternValue, "match.cmp.ge");
                            break;
                        case vyb::TokenType::EQEQ:
                            isMatch = builder->CreateICmpEQ(matchValue, patternValue, "match.cmp.eq");
                            break;
                        case vyb::TokenType::NOTEQ:
                            isMatch = builder->CreateICmpNE(matchValue, patternValue, "match.cmp.ne");
                            break;
                        default:
                            logError(compPattern->loc, "Unknown comparison operator in pattern");
                            isMatch = llvm::ConstantInt::getFalse(*context);
                            break;
                    }
                } else if (matchValue->getType()->isFloatingPointTy() && patternValue->getType()->isFloatingPointTy()) {
                    // Float comparison
                    switch (compPattern->op.type) {
                        case vyb::TokenType::LT:
                            isMatch = builder->CreateFCmpOLT(matchValue, patternValue, "match.cmp.flt");
                            break;
                        case vyb::TokenType::LTEQ:
                            isMatch = builder->CreateFCmpOLE(matchValue, patternValue, "match.cmp.fle");
                            break;
                        case vyb::TokenType::GT:
                            isMatch = builder->CreateFCmpOGT(matchValue, patternValue, "match.cmp.fgt");
                            break;
                        case vyb::TokenType::GTEQ:
                            isMatch = builder->CreateFCmpOGE(matchValue, patternValue, "match.cmp.fge");
                            break;
                        case vyb::TokenType::EQEQ:
                            isMatch = builder->CreateFCmpOEQ(matchValue, patternValue, "match.cmp.feq");
                            break;
                        case vyb::TokenType::NOTEQ:
                            isMatch = builder->CreateFCmpONE(matchValue, patternValue, "match.cmp.fne");
                            break;
                        default:
                            logError(compPattern->loc, "Unknown comparison operator in pattern");
                            isMatch = llvm::ConstantInt::getFalse(*context);
                            break;
                    }
                } else {
                    logError(compPattern->loc, "Comparison pattern requires integer or float types");
                    isMatch = llvm::ConstantInt::getFalse(*context);
                }
            } else if (matchedEnum && dynamic_cast<ast::ConstructionExpression*>(casePattern.get())) {
                // Enum variant pattern with payload: `Circle(r)`, `Rect(a, b)`.
                auto* ctor = static_cast<ast::ConstructionExpression*>(casePattern.get());
                auto* tv = ctor->constructedType ? dynamic_cast<ast::TypeName*>(ctor->constructedType.get()) : nullptr;
                if (tv && tv->identifier) {
                    auto tagIt = matchedEnum->variantTags.find(tv->identifier->name);
                    if (tagIt != matchedEnum->variantTags.end()) {
                        llvm::Value* tagVal = builder->CreateExtractValue(matchValue, 0, "enum.tag");
                        isMatch = builder->CreateICmpEQ(
                            tagVal, llvm::ConstantInt::get(int64Type, tagIt->second, true), "match.variant.tag");
                        auto payIt = matchedEnum->variantPayloadTypes.find(tv->identifier->name);
                        if (payIt != matchedEnum->variantPayloadTypes.end()) {
                            pendingVariantPayloadTy = payIt->second;
                            for (auto& arg : ctor->arguments) {
                                if (auto* b = dynamic_cast<ast::Identifier*>(arg.get())) {
                                    pendingVariantBindings.push_back(b->name);
                                }
                            }
                        }
                    } else {
                        logError(ctor->loc, "Unknown variant '" + tv->identifier->name + "' of enum being matched");
                        isMatch = llvm::ConstantInt::getFalse(*context);
                    }
                } else {
                    isMatch = llvm::ConstantInt::getFalse(*context);
                }
            } else if (matchedEnum && dynamic_cast<ast::Identifier*>(casePattern.get())) {
                // Enum unit-variant pattern: `Unit`, `None`.
                auto* pid = static_cast<ast::Identifier*>(casePattern.get());
                auto tagIt = matchedEnum->variantTags.find(pid->name);
                if (tagIt != matchedEnum->variantTags.end()) {
                    llvm::Value* tagVal = builder->CreateExtractValue(matchValue, 0, "enum.tag");
                    isMatch = builder->CreateICmpEQ(
                        tagVal, llvm::ConstantInt::get(int64Type, tagIt->second, true), "match.variant.tag");
                }
            } else {
                // Exact match pattern (literal value)
                // Evaluate the pattern
                casePattern->accept(*this);
                llvm::Value* patternValue = m_currentLLVMValue;
                if (!patternValue) {
                    logError(casePattern->loc, "Failed to evaluate match pattern");
                    m_currentLLVMValue = nullptr;
                    return;
                }

                // Compare the pattern with the match value

                // Handle different pattern types
                if (patternValue->getType()->isIntegerTy() && matchValue->getType()->isIntegerTy()) {
                    // Integer comparison
                    isMatch = builder->CreateICmpEQ(matchValue, patternValue, "match.icmp");
                } else if (patternValue->getType()->isFloatingPointTy() && matchValue->getType()->isFloatingPointTy()) {
                    // Float comparison
                    isMatch = builder->CreateFCmpOEQ(matchValue, patternValue, "match.fcmp");
                } else {
                    // For more complex types, we'd need custom comparison logic
                    // For now, just do a pointer comparison if both are pointers
                    if (patternValue->getType()->isPointerTy() && matchValue->getType()->isPointerTy()) {
                        isMatch = builder->CreateICmpEQ(
                            builder->CreatePtrToInt(matchValue, llvm::Type::getInt64Ty(*context)),
                            builder->CreatePtrToInt(patternValue, llvm::Type::getInt64Ty(*context)),
                            "match.ptrcmp"
                        );
                    } else {
                        // If we can't compare, assume no match
                        isMatch = llvm::ConstantInt::getFalse(*context);
                        logWarning(casePattern->loc, "Complex pattern matching not fully implemented");
                    }
                }
            }

            // Branch based on match result
            llvm::BasicBlock* nextBlock = (i < node->cases.size() - 1) ? caseBBs[i+1] : defaultBB;
            builder->CreateCondBr(isMatch, caseBodyBBs[i], nextBlock);
        }

        // Generate code for the case body
        builder->SetInsertPoint(caseBodyBBs[i]);
        auto savedCaseNamedValues = namedValues;
        if (pendingStructPattern) {
            bindStructPatternFields(pendingStructPattern, matchValue);
        } else if (pendingVariantPayloadTy && !pendingVariantBindings.empty()) {
            for (size_t fi = 0; fi < pendingVariantBindings.size(); ++fi) {
                const std::string& bname = pendingVariantBindings[fi];
                llvm::Value* fv = extractEnumVariantField(matchValue, pendingVariantPayloadTy, static_cast<unsigned>(fi));
                if (!fv) { logError(casePattern->loc, "Enum variant pattern binding out of range"); break; }
                llvm::AllocaInst* alloca = createEntryBlockAlloca(fv->getType(), bname);
                builder->CreateStore(fv, alloca);
                namedValues[bname] = alloca;
            }
        }

        // Optional guard clause: after the pattern matches (and destructured
        // fields are bound), evaluate the condition. A false guard falls
        // through to the next case's pattern check.
        if (i < node->guards.size() && node->guards[i]) {
            node->guards[i]->accept(*this);
            llvm::Value* guardValue = m_currentLLVMValue;
            if (!guardValue) {
                logError(node->guards[i]->loc, "Failed to evaluate match arm guard");
            } else {
                if (!guardValue->getType()->isIntegerTy(1)) {
                    guardValue = builder->CreateICmpNE(
                        guardValue,
                        llvm::ConstantInt::get(guardValue->getType(), 0),
                        "match.guard.cmp");
                }
                llvm::BasicBlock* guardPassBB = llvm::BasicBlock::Create(
                    *context, "match.guard.pass." + std::to_string(i), function);
                llvm::BasicBlock* guardNextBB =
                    (i < node->cases.size() - 1) ? caseBBs[i + 1] : defaultBB;
                builder->CreateCondBr(guardValue, guardPassBB, guardNextBB);
                builder->SetInsertPoint(guardPassBB);
            }
        }

        if (caseBody) {
            caseBody->accept(*this);
            // If we are generating a value-returning match expression, store the
            // arm's resulting value into the shared result slot.
            if (resultAlloca && m_currentLLVMValue &&
                !builder->GetInsertBlock()->getTerminator()) {
                storeIntoResultSlot(m_currentLLVMValue, resultAlloca, node->loc);
            }
        }
        // Scoped destructured field bindings per case arm.
        namedValues = std::move(savedCaseNamedValues);

        // Branch to end of match only if the case body doesn't already have a terminator
        if (!builder->GetInsertBlock()->getTerminator()) {
            builder->CreateBr(endMatchBB);
        }
    }

    // Generate code for default case (no match)
    // If there's no wildcard pattern and no match occurs, execution continues (NOP)
    builder->SetInsertPoint(defaultBB);
    m_currentLLVMValue = nullptr;
    // Only branch to endMatchBB if there's no wildcard pattern (which would have caught everything)
    if (!hasWildcard && !builder->GetInsertBlock()->getTerminator()) {
        builder->CreateBr(endMatchBB);
    } else if (hasWildcard && !builder->GetInsertBlock()->getTerminator()) {
        // If there's a wildcard, this block is unreachable, but LLVM still needs a terminator
        builder->CreateUnreachable();
    }

    // Set insertion point to end of match
    // Only add endMatchBB to the function if it has predecessors (i.e., if it's actually used)
    if (!endMatchBB->use_empty()) {
        if (!endMatchBB->getParent()) {
            endMatchBB->insertInto(function);
        }
        builder->SetInsertPoint(endMatchBB);
    } else {
        // endMatchBB was never actually branched to (all cases returned/broke)
        // Clean it up and don't set insert point to it
        if (endMatchBB->getParent()) {
            endMatchBB->eraseFromParent();
        }
        delete endMatchBB;
        // Leave the builder at the last block that was generated
        // Don't change the insertion point since all paths terminated
    }

    // If this is a value-returning match expression and a real merge point
    // exists, load the stored result. Otherwise (statement form, or every arm
    // returned/broke) the match produces no value.
    // Pop the yield context before producing the final value.
    if (resultAlloca && !yieldContextStack_.empty()) {
        yieldContextStack_.pop_back();
    }

    if (resultAlloca && !endMatchBB->use_empty()) {
        m_currentLLVMValue = builder->CreateLoad(
            resultAlloca->getAllocatedType(), resultAlloca, "match.expr.value");
    } else {
        m_currentLLVMValue = nullptr;
    }
}

void LLVMCodegen::visit(vyb::ast::MatchStatement* node) {
    codegenMatch(node, nullptr);
}

void LLVMCodegen::visit(vyb::ast::AssertStatement* node) {
    // Get the current function
    llvm::Function* function = getCurrentFunction();
    if (!function) {
        logError(node->loc, "Assert statement outside function context");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Create basic blocks for assert checking
    llvm::BasicBlock* assertPassBB = llvm::BasicBlock::Create(*context, "assert.pass", function);
    llvm::BasicBlock* assertFailBB = llvm::BasicBlock::Create(*context, "assert.fail", function);

    // Evaluate the condition
    if (!node->condition) {
        logError(node->loc, "Assert statement missing condition");
        m_currentLLVMValue = nullptr;
        return;
    }

    node->condition->accept(*this);
    llvm::Value* condValue = m_currentLLVMValue;
    if (!condValue) {
        logError(node->condition->loc, "Failed to evaluate assert condition");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Convert condition to boolean if needed
    if (condValue->getType() != llvm::Type::getInt1Ty(*context)) {
        condValue = builder->CreateICmpNE(
            condValue,
            llvm::ConstantInt::get(condValue->getType(), 0),
            "assert.cond"
        );
    }

    // Create conditional branch
    builder->CreateCondBr(condValue, assertPassBB, assertFailBB);

    // Generate assert failure handling
    builder->SetInsertPoint(assertFailBB);

    // Get the message if provided, or create a default one
    llvm::Value* messageValue;
    if (node->message) {
        node->message->accept(*this);
        messageValue = m_currentLLVMValue;

        // If the message isn't a string, convert it to a string
        if (!messageValue || !messageValue->getType()->isPointerTy()) {
            // Create a default message
            messageValue = builder->CreateGlobalStringPtr(
                "Assertion failed at " + node->loc.toString(),
                "assert.msg"
            );
        }
    } else {
        // Create a default message
        messageValue = builder->CreateGlobalStringPtr(
            "Assertion failed at " + node->loc.toString(),
            "assert.msg"
        );
    }

    // Call the assert failure handler function
    std::vector<llvm::Type*> handlerParamTypes = {
        llvm::PointerType::get(*context, 0) // Message as char*
    };

    llvm::FunctionType* handlerFuncType = llvm::FunctionType::get(
        llvm::Type::getVoidTy(*context),
        handlerParamTypes,
        false
    );

    // Get or create the assert handler function
    llvm::Function* assertHandlerFunc = module->getFunction("__vyb_assert_fail");
    if (!assertHandlerFunc) {
        assertHandlerFunc = llvm::Function::Create(
            handlerFuncType,
            llvm::Function::ExternalLinkage,
            "__vyb_assert_fail",
            module.get()
        );
    }

    // Call the handler with the message
    std::vector<llvm::Value*> args = { messageValue };
    builder->CreateCall(assertHandlerFunc, args);

    // Terminate execution after assertion failure (this will be unreachable in practice)
    builder->CreateUnreachable();

    // Continue normal execution if assertion passes
    builder->SetInsertPoint(assertPassBB);

    // Assert statements don't produce a value
    m_currentLLVMValue = nullptr;
}

void LLVMCodegen::visit(ast::YieldStatement* node) {
    // Implementation for YieldStatement
    // Yield temporarily produces a value and suspends execution until the generator is resumed

    if (!getCurrentFunction()) {
        logError(node->loc, "Yield statement outside of function context.");
        m_currentLLVMValue = nullptr;
        return;
    }

    // For a basic implementation, we need to:
    // 1. Evaluate the expression to yield (if any)
    // 2. Save the current state of execution
    // 3. Create a suspension point

    llvm::Value* yieldValue = nullptr;
    if (node->expression) {
        // Visit the expression to get its value
        node->expression->accept(*this);
        yieldValue = m_currentLLVMValue;

        if (!yieldValue) {
            logError(node->expression->loc, "Failed to evaluate yield expression.");
            m_currentLLVMValue = nullptr;
            return;
        }
    } else {
        // If no expression provided, yield 'undefined' or a default value
        yieldValue = llvm::UndefValue::get(llvm::Type::getInt32Ty(*context));
    }

    // For now, we'll create a placeholder implementation that logs the yield
    // In a full implementation, this would involve coroutine transformation
    std::vector<llvm::Type*> paramTypes = { yieldValue->getType() };
    llvm::FunctionType* logYieldType = llvm::FunctionType::get(
        llvm::Type::getVoidTy(*context),
        paramTypes,
        false
    );

    // Create or get the debug function for logging yields
    llvm::Function* logYieldFunc = module->getFunction("__vyb_debug_log_yield");
    if (!logYieldFunc) {
        logYieldFunc = llvm::Function::Create(
            logYieldType,
            llvm::Function::ExternalLinkage,
            "__vyb_debug_log_yield",
            module.get()
        );
    }

    // Call the debug function with our yield value
    std::vector<llvm::Value*> args = { yieldValue };
    builder->CreateCall(logYieldFunc, args);

    logWarning(node->loc, "YieldStatement partially implemented. Full generator functionality requires coroutine support.");

    m_currentLLVMValue = yieldValue;
}

void LLVMCodegen::visit(ast::YieldReturnStatement* node) {
    // Implementation for YieldReturnStatement
    // This represents the final return from a generator function

    if (!getCurrentFunction()) {
        logError(node->loc, "Yield return statement outside of function context.");
        m_currentLLVMValue = nullptr;
        return;
    }

    llvm::Value* returnValue = nullptr;
    llvm::Type* returnType = currentFunction->getReturnType();

    if (node->expression) {
        // Visit the expression to get its value
        node->expression->accept(*this);
        returnValue = m_currentLLVMValue;

        if (!returnValue) {
            logError(node->expression->loc, "Failed to evaluate yield return expression.");
            m_currentLLVMValue = nullptr;
            return;
        }

        // Check if the types match
        if (returnValue->getType() != returnType && !returnType->isVoidTy()) {
            // Try to cast the value to the return type
            returnValue = tryCast(returnValue, returnType, node->loc);
            if (!returnValue) {
                logError(node->expression->loc, "Cannot convert yield return value to the function's return type.");
                m_currentLLVMValue = nullptr;
                return;
            }
        }
    } else if (!returnType->isVoidTy()) {
        // No expression provided but non-void return type required
        logError(node->loc, "Yield return statement missing expression for non-void return type.");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Create a function for signaling generator completion
    std::vector<llvm::Type*> paramTypes;
    if (returnValue) {
        paramTypes.push_back(returnValue->getType());
    }

    llvm::FunctionType* completeGenType = llvm::FunctionType::get(
        llvm::Type::getVoidTy(*context),
        paramTypes,
        false
    );

    // Create or get the debug function for generator completion
    llvm::Function* completeGenFunc = module->getFunction("__vyb_debug_complete_generator");
    if (!completeGenFunc) {
        completeGenFunc = llvm::Function::Create(
            completeGenType,
            llvm::Function::ExternalLinkage,
            "__vyb_debug_complete_generator",
            module.get()
        );
    }

    // Call the debug function
    std::vector<llvm::Value*> args;
    if (returnValue) {
        args.push_back(returnValue);
    }
    builder->CreateCall(completeGenFunc, args);

    // Add a normal return statement after the yield return
    if (returnType->isVoidTy()) {
        builder->CreateRetVoid();
    } else if (returnValue) {
        builder->CreateRet(returnValue);
    }

    logWarning(node->loc, "YieldReturnStatement partially implemented. Full generator functionality requires coroutine support.");

    m_currentLLVMValue = returnValue;
}

void LLVMCodegen::visit(ast::ExternStatement* node) {
    // Implementation for ExternStatement to generate LLVM IR for external function declarations

    if (!node->name) {
        logError(node->loc, "External declaration missing name.");
        m_currentLLVMValue = nullptr;
        return;
    }

    std::vector<llvm::Type*> paramTypes;
    for (const auto& paramNode : node->parameters) {
        if (!paramNode.typeNode) {
            logError(paramNode.name->loc, "Parameter '" + paramNode.name->name +
                     "' in external declaration '" + node->name->name + "' is missing a type annotation.");
            m_currentLLVMValue = nullptr;
            return;
        }

        llvm::Type* llvmType = codegenType(paramNode.typeNode.get());
        if (!llvmType) {
            logError(paramNode.name->loc, "Could not determine LLVM type for parameter '" +
                     paramNode.name->name + "' in external declaration '" + node->name->name + "'.");
            m_currentLLVMValue = nullptr;
            return;
        }
        paramTypes.push_back(llvmType);
    }

    llvm::Type* returnType = nullptr;
    if (node->returnType) {
        returnType = codegenType(node->returnType.get());
        if (!returnType) {
            logError(node->loc, "Could not determine LLVM return type for external declaration '" +
                     node->name->name + "'.");
            m_currentLLVMValue = nullptr;
            return;
        }
    } else {
        returnType = llvm::Type::getVoidTy(*context);
    }

    llvm::FunctionType* funcType = llvm::FunctionType::get(returnType, paramTypes, false /*isVarArg*/);

    // Check for existing function
    llvm::Function* func = module->getFunction(node->name->name);
    if (func) {
        if (func->getFunctionType() != funcType) {
            logError(node->loc, "Redeclaration of external function '" + node->name->name +
                     "' with different signature.");
            m_currentLLVMValue = nullptr;
            return;
        }
        // Function already declared with matching signature, nothing more to do
    } else {
        // Create the external function declaration
        func = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, node->name->name, module.get());

        // Set parameter names for better IR readability
        unsigned idx = 0;
        for (auto &arg : func->args()) {
            if (idx < node->parameters.size()) {
                arg.setName(node->parameters[idx].name->name);
            }
            idx++;
        }
    }

    m_currentLLVMValue = func;
}

// --- Error Handling Codegen Implementations ---

void LLVMCodegen::visit(vyb::ast::FailStatement* node) {
    // Generate:
    // 1. Evaluate error expression
    // 2. Check if there's a trap handler in scope
    // 3. If no trap: call __vyb_runtime_untrapped_error() and unreachable
    // 4. If trap: store error and jump to trap landing pad

    llvm::Function* function = getCurrentFunction();
    if (!function) {
        logError(node->loc, "Fail statement outside function context");
        m_currentLLVMValue = nullptr;
        return;
    }

    if (!node->error) {
        logError(node->loc, "Fail statement missing error expression");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Evaluate the error expression
    node->error->accept(*this);
    llvm::Value* errorValue = m_currentLLVMValue;

    if (!errorValue) {
        logError(node->loc, "Error expression evaluated to null");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Construct a concrete runtime VybError object.
    std::string typeName;
    if (node->errorType) {
        typeName = node->errorType->toString();
    } else if (node->error && node->error->type) {
        typeName = node->error->type->toString();
    } else if (errorValue->getType()->isIntegerTy(1)) {
        typeName = "Bool";
    } else if (errorValue->getType()->isIntegerTy()) {
        typeName = "Int";
    } else if (errorValue->getType()->isFloatingPointTy()) {
        typeName = "Float";
    }
    if (typeName.empty()) {
        typeName = "Error";
    }

    llvm::DataLayout dataLayout(module.get());
    llvm::Type* payloadType = errorValue->getType();
    uint64_t payloadSize = dataLayout.getTypeAllocSize(payloadType);

    llvm::Value* payloadAlloca = builder->CreateAlloca(payloadType, nullptr, "fail.payload");
    builder->CreateStore(errorValue, payloadAlloca);
    llvm::Value* payloadPtr = builder->CreateBitCast(payloadAlloca, llvm::PointerType::get(*context, 0), "fail.payload.ptr");

    llvm::Function* createErrFn = module->getFunction("__vyb_runtime_create_error_ex");
    if (!createErrFn) {
        llvm::Type* i8PtrTy = llvm::PointerType::get(*context, 0);
        llvm::Type* i64Ty = builder->getInt64Ty();
        llvm::Type* i32Ty = builder->getInt32Ty();
        llvm::Type* destructorTy = llvm::PointerType::get(llvm::FunctionType::get(llvm::Type::getVoidTy(*context), {i8PtrTy}, false), 0);
        llvm::FunctionType* createErrTy = llvm::FunctionType::get(
            i8PtrTy,
            {i8PtrTy, i8PtrTy, i8PtrTy, i64Ty, destructorTy, i8PtrTy, i32Ty, i32Ty},
            false
        );
        createErrFn = llvm::Function::Create(
            createErrTy,
            llvm::Function::ExternalLinkage,
            "__vyb_runtime_create_error_ex",
            module.get()
        );
    }

    llvm::Value* typeNameValue = builder->CreateGlobalStringPtr(typeName, "fail.type_name");
    llvm::Value* nullTypeId = llvm::ConstantPointerNull::get(llvm::PointerType::get(*context, 0));
    llvm::Value* payloadSizeValue = llvm::ConstantInt::get(builder->getInt64Ty(), payloadSize);
    llvm::Type* destructorTy = createErrFn->getFunctionType()->getParamType(4);
    llvm::Value* nullDestructor = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(destructorTy));
    llvm::Value* fileValue = builder->CreateGlobalStringPtr(node->loc.filePath, "fail.file");

    llvm::Value* errorPtr = builder->CreateCall(
        createErrFn,
        {
            typeNameValue,
            nullTypeId,
            payloadPtr,
            payloadSizeValue,
            nullDestructor,
            fileValue,
            llvm::ConstantInt::get(builder->getInt32Ty(), node->loc.line),
            llvm::ConstantInt::get(builder->getInt32Ty(), node->loc.column)
        },
        "fail.error"
    );

    // Check if we're inside a trap context
    if (!trapStack.empty()) {
        // We have an active trap handler - store error and jump to landing pad
        TrapContext& trap = trapStack.back();

        // Store the error pointer in the error slot
        // Error pointer now contains type ID header, so no separate type storage needed
        builder->CreateStore(errorPtr, trap.errorSlot);

        // Jump to the landing pad for error handling
        builder->CreateBr(trap.landingPad);

    } else {
        // No trap handler in current scope

        // Phase 3: Check if we're in a failable function that can propagate errors
        if (currentFunctionAST && currentFunctionAST->needsErrorReturn) {
            
            emitPropagatingErrorReturn(errorPtr);
        } else {
            // No trap handler and not a failable function - this is an untrapped error
            llvm::Function* untrappedFn = getVybUntrappedErrorFunction();
            // Call untrapped error handler (noreturn)
            builder->CreateCall(untrappedFn, {errorPtr});

            // Mark as unreachable
            builder->CreateUnreachable();
        }
    }

    m_currentLLVMValue = nullptr;
}

void LLVMCodegen::visit(vyb::ast::TrapClause* node) {
    // TODO: Phase 1 implementation
    // TrapClause is not directly visited - it's processed as part of block expression
    // with trap clauses. The block codegen will:
    // 1. Set up landing pads for exception handling
    // 2. Generate type checks for each trap clause
    // 3. Jump to appropriate handler or continue unwinding

    logError(node->loc, "TrapClause should not be visited directly");
    m_currentLLVMValue = nullptr;
}

void LLVMCodegen::visit(vyb::ast::EnsureClause* node) {
    // TODO: Phase 1 implementation
    // EnsureClause is not directly visited - it's processed as part of block expression
    // The block codegen will:
    // 1. Register cleanup handlers in scope
    // 2. Generate cleanup code in landing pads
    // 3. Ensure cleanup runs on both success and failure paths

    logError(node->loc, "EnsureClause should not be visited directly");
    m_currentLLVMValue = nullptr;
}

void LLVMCodegen::visit(vyb::ast::RethrowStatement* node) {
    // Generate:
    // 1. If transformedError: evaluate new error expression
    // 2. Pop current trap context and rethrow to outer handler
    // 3. Mark as unreachable (no return)

    llvm::Function* function = getCurrentFunction();
    if (!function) {
        logError(node->loc, "Rethrow statement outside function context");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Semantic analysis should have ensured we're inside a trap clause
    if (trapStack.empty()) {
        logError(node->loc, "Rethrow outside trap context (should be caught by semantic analysis)");
        m_currentLLVMValue = nullptr;
        return;
    }

    llvm::Value* errorToRethrow = nullptr;

    if (node->transformedError) {
        // Evaluate the transformed error expression
        node->transformedError->accept(*this);
        errorToRethrow = m_currentLLVMValue;
    } else {
        // Rethrow the current error (load from error slot)
        TrapContext& currentTrap = trapStack.back();
        // errorSlot is now a heap pointer, always stores ptr type
        llvm::Type* errorType = llvm::PointerType::get(*context, 0);
        errorToRethrow = builder->CreateLoad(errorType, currentTrap.errorSlot, "rethrow_error");
    }

    if (!errorToRethrow) {
        logError(node->loc, "Rethrow error value is null");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Check if there's an outer trap handler
    if (trapStack.size() > 1) {
        // Store error in outer trap's error slot and jump to its landing pad
        TrapContext& outerTrap = trapStack[trapStack.size() - 2];
        builder->CreateStore(errorToRethrow, outerTrap.errorSlot);
        builder->CreateBr(outerTrap.landingPad);
    } else {
        // No outer trap - call untrapped error handler
        llvm::Function* untrappedFn = getVybUntrappedErrorFunction();

        llvm::Value* errorPtr = errorToRethrow;
        if (!errorToRethrow->getType()->isPointerTy()) {
            llvm::AllocaInst* tempAlloca = builder->CreateAlloca(errorToRethrow->getType(), nullptr, "rethrow_temp");
            builder->CreateStore(errorToRethrow, tempAlloca);
            errorPtr = builder->CreateBitCast(tempAlloca, int8PtrType, "rethrow_as_ptr");
        } else {
            errorPtr = builder->CreateBitCast(errorToRethrow, int8PtrType, "rethrow_as_ptr");
        }

        builder->CreateCall(untrappedFn, {errorPtr});
        builder->CreateUnreachable();
    }

    m_currentLLVMValue = nullptr;
}

void LLVMCodegen::visit(vyb::ast::PanicStatement* node) {
    // Generate:
    // 1. Evaluate panic message
    // 2. Call __vyb_runtime_panic(message)
    // 3. Mark as noreturn with unreachable

    llvm::Function* function = getCurrentFunction();
    if (!function) {
        logError(node->loc, "Panic statement outside function context");
        m_currentLLVMValue = nullptr;
        return;
    }

    if (!node->message) {
        logError(node->loc, "Panic statement missing message");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Evaluate panic message (should be a String)
    node->message->accept(*this);
    llvm::Value* messageValue = m_currentLLVMValue;

    if (!messageValue) {
        logError(node->loc, "Panic message evaluated to null");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Get or create the panic runtime function
    llvm::Function* panicFn = getVybPanicFunction();

    // Extract the char* pointer from the String struct
    // String literals in Vyb are { ptr, i64 } structs
    llvm::Value* messageStr = messageValue;

    if (messageValue->getType()->isStructTy()) {
        // Extract field 0 (the char* pointer) from the String struct
        messageStr = builder->CreateExtractValue(messageValue, 0, "panic_str_ptr");
    } else if (messageValue->getType()->isPointerTy()) {
        // If it's a pointer to a struct, load and extract
        llvm::Value* loadedValue = builder->CreateLoad(stringType, messageValue, "panic_str_load");
        messageStr = builder->CreateExtractValue(loadedValue, 0, "panic_str_ptr");
    }

    // Call panic function (noreturn)
    builder->CreateCall(panicFn, {messageStr});

    // Mark as unreachable - execution never continues after panic
    builder->CreateUnreachable();

    m_currentLLVMValue = nullptr;
}

void LLVMCodegen::visit(vyb::ast::ExitStatement* node) {
    // Generate: call void @exit(i32 code) then unreachable
    if (!node->code) {
        logError(node->loc, "exit statement missing exit code expression");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Evaluate the exit code expression
    node->code->accept(*this);
    llvm::Value* codeValue = m_currentLLVMValue;

    if (!codeValue) {
        logError(node->loc, "exit code expression evaluated to null");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Convert to i32 (standard exit code type)
    llvm::Value* code32 = builder->CreateTruncOrBitCast(codeValue, llvm::Type::getInt32Ty(*context), "exit.code");

    // Declare or get @exit(i32) -> void (noreturn C standard library function)
    llvm::Function* exitFn = module->getFunction("exit");
    if (!exitFn) {
        llvm::FunctionType* exitFnType = llvm::FunctionType::get(
            llvm::Type::getVoidTy(*context), {llvm::Type::getInt32Ty(*context)}, false);
        exitFn = llvm::Function::Create(
            exitFnType, llvm::Function::ExternalLinkage, "exit", module.get());
        exitFn->addFnAttr(llvm::Attribute::NoReturn);
    }

    builder->CreateCall(exitFn, {code32});
    builder->CreateUnreachable();

    m_currentLLVMValue = nullptr;
}

void LLVMCodegen::visit(vyb::ast::DeferStatement* node) {
    // DeferStatement: register the statement to run at function exit
    if (m_deferStack.empty()) {
        // No active defer scope - shouldn't happen inside a function body
        return;
    }
    // Add to the current defer scope (innermost)
    if (node->statement) {
        m_deferStack.back().push_back(node->statement.get());
    }
}



void LLVMCodegen::visit(vyb::ast::TupleDestructureAssignment* node) {
    // TupleDestructureAssignment: x, y = expr
    // RHS produces a struct value (from SequenceExpression codegen).
    // Extract each element and store to the corresponding variable's alloca.
    
    // Evaluate RHS - this produces a struct LLVM value
    node->expression->accept(*this);
    llvm::Value* structVal = m_currentLLVMValue;
    
    if (!structVal) {
        logError(node->loc, "Tuple destructure RHS produced no value");
        return;
    }
    
    // Get the struct type from the value
    llvm::StructType* structType = llvm::cast<llvm::StructType>(structVal->getType());
    
    if (structType->getNumElements() != node->identifiers.size()) {
        logError(node->loc, "Tuple destructure: expected " + std::to_string(node->identifiers.size()) +
                 " elements but RHS struct has " + std::to_string(structType->getNumElements()) + " elements");
        return;
    }
    
    // For each identifier, extract the corresponding element and store to its alloca
    for (size_t i = 0; i < node->identifiers.size(); ++i) {
        std::string varName = node->identifiers[i]->name;
        
        // Look up or create alloca for this variable
        llvm::AllocaInst* alloca;
        auto it = m_currentFunctionNamedValues.find(varName);
        if (it != m_currentFunctionNamedValues.end()) {
            alloca = it->second;
        } else {
            // Create a new alloca - infer type from the struct element type
            llvm::Type* elemType = structType->getElementType(static_cast<unsigned>(i));
            alloca = builder->CreateAlloca(elemType, nullptr, ("tuple_destruct_" + varName).c_str());
            m_currentFunctionNamedValues[varName] = alloca;
        }
        
        // Extract element i from the struct
        llvm::Value* elemVal = builder->CreateExtractValue(structVal, {static_cast<unsigned>(i)}, 
                                                           ("tuple_elem_" + std::to_string(i)).c_str());
        
        // Store to the variable's alloca
        builder->CreateStore(elemVal, alloca);
    }
    
    m_currentLLVMValue = nullptr;
}

}  // namespace vyb
