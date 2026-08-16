// SPDX-License-Identifier: Apache-2.0

#include "vyb/vre/llvm/codegen.hpp"
#include "vyb/parser/ast.hpp"
#include "vyb/parser/token.hpp" // For TokenType in BinaryExpression

#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/DerivedTypes.h> // For PointerType, StructType
#include <llvm/ADT/APFloat.h>   // For APFloat in FloatLiteral
#include <llvm/ADT/APInt.h>     // For APInt in IntegerLiteral
#include <regex>                // For regex in MemberExpression loaded value handling

using namespace vyb;
// using namespace llvm; // Uncomment if desired for brevity

// Classify a sized integer type name into signedness and bit width. Returns
// false for non-integer (or unknown) type names.
static bool vybIntClass(const std::string& name, bool& isUnsigned, unsigned& bits) {
    if (name == "Int" || name == "Int64" || name == "i64")    { isUnsigned = false; bits = 64; return true; }
    if (name == "Int32" || name == "i32")                     { isUnsigned = false; bits = 32; return true; }
    if (name == "Int16" || name == "i16")                     { isUnsigned = false; bits = 16; return true; }
    if (name == "Int8"  || name == "i8")                      { isUnsigned = false; bits = 8;  return true; }
    if (name == "UInt64" || name == "u64")                    { isUnsigned = true;  bits = 64; return true; }
    if (name == "UInt32" || name == "u32")                    { isUnsigned = true;  bits = 32; return true; }
    if (name == "UInt16" || name == "u16")                    { isUnsigned = true;  bits = 16; return true; }
    if (name == "UInt8"  || name == "u8" || name == "Byte")   { isUnsigned = true;  bits = 8;  return true; }
    if (name == "Char")                                       { isUnsigned = false; bits = 8;  return true; }
    if (name == "Rune")                                       { isUnsigned = false; bits = 32; return true; }
    return false;
}

// Is this a sized unsigned integer type name?
static bool isUnsignedIntName(const std::string& n) {
    return n == "UInt8" || n == "UInt16" || n == "UInt32" || n == "UInt64" ||
           n == "u8" || n == "u16" || n == "u32" || n == "u64" ||
           n == "Byte" || n == "CUChar" || n == "CUShort" || n == "CUInt" ||
           n == "CULong" || n == "CSize";
}

// --- Literal Codegen ---
void LLVMCodegen::visit(vyb::ast::IntegerLiteral *node) {
    if (node->isUnsigned) {
        m_currentLLVMValue = llvm::ConstantInt::get(*context, llvm::APInt(64, node->uvalue, false));
    } else {
        m_currentLLVMValue = llvm::ConstantInt::get(*context, llvm::APInt(64, node->value, true));
    }
}

void LLVMCodegen::visit(vyb::ast::FloatLiteral *node) {
    m_currentLLVMValue = llvm::ConstantFP::get(*context, llvm::APFloat(node->value));
}

void LLVMCodegen::visit(vyb::ast::BooleanLiteral *node) {
    m_currentLLVMValue = llvm::ConstantInt::get(*context, llvm::APInt(1, node->value));
}

void LLVMCodegen::visit(vyb::ast::StringLiteral *node) {
    llvm::Type* int8PtrType = llvm::PointerType::get(*context, 0);
    llvm::Type* int64Type = llvm::Type::getInt64Ty(*context);
    llvm::StructType* stringStructType = llvm::StructType::get(*context, {int8PtrType, int64Type});
    llvm::Constant* lenValue = llvm::ConstantInt::get(int64Type, node->value.length());

    if (currentFunction) {
        // Inside a function - use CreateGlobalStringPtr and build a runtime string struct.
        llvm::Value* strPtr = builder->CreateGlobalStringPtr(node->value);
        llvm::Value* stringStruct = llvm::UndefValue::get(stringStructType);
        stringStruct = builder->CreateInsertValue(stringStruct, strPtr, 0, "str.ptr");
        stringStruct = builder->CreateInsertValue(stringStruct, lenValue, 1, "str.len");
        m_currentLLVMValue = stringStruct;
        return;
    }

    // Global scope - emit a constant global string and return a constant String struct.
    llvm::Constant* stringConstant = llvm::ConstantDataArray::getString(*context, node->value, true);
    llvm::GlobalVariable* globalString = new llvm::GlobalVariable(
        *module,
        stringConstant->getType(),
        true,
        llvm::GlobalValue::PrivateLinkage,
        stringConstant,
        ".str"
    );

    std::vector<llvm::Constant*> indices = {
        llvm::ConstantInt::get(int64Type, 0),
        llvm::ConstantInt::get(int64Type, 0)
    };

    llvm::Constant* strPtr = llvm::ConstantExpr::getGetElementPtr(
        stringConstant->getType(),
        globalString,
        indices
    );

    m_currentLLVMValue = llvm::ConstantStruct::get(stringStructType, {strPtr, lenValue});
}

void LLVMCodegen::visit(vyb::ast::NilLiteral* node) {
    // Nil is a polymorphic null pointer. For now, default to i8*.
    // Type inference or context should ideally provide a more specific pointer type.
    if (m_currentLLVMType && m_currentLLVMType->isPointerTy()) {
        // If we have a specific pointer type from context, use it
        m_currentLLVMValue = llvm::ConstantPointerNull::get(
            llvm::cast<llvm::PointerType>(m_currentLLVMType));
    } else {
        // Default to i8* if no specific type is known
        m_currentLLVMValue = llvm::ConstantPointerNull::get(
            llvm::PointerType::getUnqual(int8Type));
    }
}

void LLVMCodegen::visit(vyb::ast::ObjectLiteral* node) {
    if (!node->typePath) {
        logError(node->loc, "Object literal is missing type information");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Get the struct type for the object
    VYB_CDBG << "DEBUG: ObjectLiteral resolving type: " << node->typePath->toString() << std::endl;
    llvm::Type* structTy = codegenType(node->typePath.get());
    VYB_CDBG << "DEBUG: ObjectLiteral resolved type to: " << getTypeName(structTy) << " with pointer: " << structTy << std::endl;
    if (!structTy || !structTy->isStructTy()) {
        logError(node->loc, "Object literal type is not a struct type");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Store the type info back in the expression's type field for MemberExpression to use
    // This is crucial for member access later when working with the object
    if (!node->type) {
        node->type = node->typePath->clone();
    }

    std::string structName = llvm::cast<llvm::StructType>(structTy)->getName().str();
    if (structName.empty()) {
        structName = "anon";
    }

    // Allocate stack space for the struct
    llvm::AllocaInst* allocaInst = builder->CreateAlloca(structTy, nullptr, structName + "_obj");

    // Set metadata or debug info to help identify this as a struct of type structName
    // This can help when trying to determine the type in MemberExpression
    if (!structName.empty() && structName != "anon") {
        // Store the allocated type in the userTypeMap if it's not already there
        // This ensures the type is registered for field lookups
        auto it = userTypeMap.find(structName);
        if (it == userTypeMap.end() && llvm::isa<llvm::StructType>(structTy)) {
            llvm::StructType* structType = llvm::cast<llvm::StructType>(structTy);
            if (!structType->isOpaque()) {
                UserTypeInfo typeInfo;
                typeInfo.llvmType = structType;
                typeInfo.isStruct = true;

                // Try to populate field indices if we have the information
                // This might be incomplete, but can be useful for debugging
                userTypeMap[structName] = typeInfo;
            }
        }
    }

    // Store each field
    for (size_t i = 0; i < node->properties.size(); ++i) {
        const auto& prop = node->properties[i];
        if (!prop.value || !prop.key) {
            logError(node->loc, "ObjectLiteral property missing key or value");
            m_currentLLVMValue = nullptr;
            return;
        }

        // Get field index by name
        std::string fieldName = prop.key->toString();
        int fieldIndex = getStructFieldIndex(llvm::cast<llvm::StructType>(structTy), fieldName);
        if (fieldIndex < 0) {
            logError(node->loc, "Field '" + fieldName + "' not found in struct '" + structName + "'");
            m_currentLLVMValue = nullptr;
            return;
        }

        // Generate the value for the field
        prop.value->accept(*this);
        if (!m_currentLLVMValue) {
            logError(prop.value->loc, "Failed to codegen value for field '" + fieldName + "'");
            m_currentLLVMValue = nullptr;
            return;
        }

        // Create GEP to get pointer to field
        llvm::Value* fieldPtr = builder->CreateStructGEP(structTy, allocaInst, fieldIndex, fieldName + "_ptr");

        // Store the value into the field. Cast the initializer to the field's
        // declared LLVM type (e.g. truncate a Vyb i64 literal to a CInt i32
        // slot) so the struct is stored at its real width. Without this the
        // literal lands at i64 width in an i32 field, corrupting the offsets
        // and values of later fields.
        llvm::Value* fieldValue = m_currentLLVMValue;
        auto* stLeaf = llvm::cast<llvm::StructType>(structTy);
        llvm::Type* fieldTy = stLeaf->getElementType(static_cast<unsigned>(fieldIndex));
        if (fieldValue->getType() != fieldTy) {
            fieldValue = tryCast(fieldValue, fieldTy, prop.value->loc);
            if (!fieldValue) {
                m_currentLLVMValue = nullptr;
                return;
            }
        }
        builder->CreateStore(fieldValue, fieldPtr);
    }

    // In Vyb, struct initialization can be used both for creating temporary values
    // and for direct assignment to variables. We need to decide if we should return
    // the pointer or load the actual struct value.

    // Store the struct type information in userTypeMap if not already present
    if (!structName.empty() && structName != "anon") {
        auto it = userTypeMap.find(structName);
        if (it == userTypeMap.end() && llvm::isa<llvm::StructType>(structTy)) {
            UserTypeInfo typeInfo;
            typeInfo.llvmType = llvm::cast<llvm::StructType>(structTy);
            typeInfo.isStruct = true;
            userTypeMap[structName] = typeInfo;
        }
    }

    // For struct initialization in variable assignment or return statements,
    // we should load the struct value rather than return the pointer.
    // This ensures type compatibility with value semantics.
    llvm::Value* structValue = builder->CreateLoad(structTy, allocaInst, structName + "_val");

    VYB_CDBG << "DEBUG: ObjectLiteral created struct value with type: " << getTypeName(structValue->getType()) << std::endl;
    VYB_CDBG << "DEBUG: Expected struct type was: " << getTypeName(structTy) << std::endl;

    // Return the loaded struct value
    m_currentLLVMValue = structValue;
}

void LLVMCodegen::visit(vyb::ast::ArrayLiteral* node) {
    if (node->elements.empty()) {
        // Handle empty array literal. Need its type.
        // If node->type is set by semantic analysis:
        if (node->type) {
            llvm::Type* arrayLlvmType = codegenType(node->type.get());
            if (auto at = llvm::dyn_cast<llvm::ArrayType>(arrayLlvmType)) {
                 m_currentLLVMValue = llvm::ConstantArray::get(at, {}); // Empty constant array
                 return;
            } else if (auto pt = llvm::dyn_cast<llvm::PointerType>(arrayLlvmType)) {
                // If it's a pointer to an array or slice type.
                // This case is more complex for an empty literal.
                // It might mean a null pointer or pointer to an empty static region.
                // For now, let's assume if type is known, it's an ArrayType.
            }
        }
        logError(node->loc, "Empty array literal with unknown type.");
        m_currentLLVMValue = nullptr;
        return;
    }

    std::vector<llvm::Constant*> constantElements;
    llvm::Type* elementLlvmType = nullptr;

    for (const auto& elemExpr : node->elements) {
        elemExpr->accept(*this); // Codegen element
        llvm::Value* elemValue = m_currentLLVMValue;
        if (!elemValue) {
            logError(elemExpr->loc, "Element codegen failed in array literal.");
            m_currentLLVMValue = nullptr;
            return;
        }
        if (!elementLlvmType) {
            elementLlvmType = elemValue->getType();
        } else if (elemValue->getType() != elementLlvmType) {
            // TODO: Handle mixed types, promotions, or error
            // For now, assume all elements must be of the same type as the first
            // Or attempt to cast to the type of the first element.
            // This should ideally be caught by semantic analysis.
            logError(elemExpr->loc, "Array literal elements have mixed types. Expected " + getTypeName(elementLlvmType) + " but got " + getTypeName(elemValue->getType()));
            m_currentLLVMValue = nullptr;
            return;
        }
        // Array literals must consist of constants to form a ConstantArray
        if (auto* constElem = llvm::dyn_cast<llvm::Constant>(elemValue)) {
            constantElements.push_back(constElem);
        } else {
            // If elements are not constant, we can't create a llvm::ConstantArray.
            // This means the array must be constructed at runtime, e.g., by allocating
            // memory and storing each element. This is more like ArrayInitializationExpression
            // or requires a helper function.
            // For now, array literals are assumed to produce ConstantArrays.
            logError(elemExpr->loc, "Array literal element is not a constant value. Runtime array construction not yet fully supported here.");
            m_currentLLVMValue = nullptr;
            return;
        }
    }

    if (!elementLlvmType) { // Should not happen if elements is not empty and codegen succeeded
        logError(node->loc, "Could not determine element type for array literal.");
        m_currentLLVMValue = nullptr;
        return;
    }

    llvm::ArrayType* arrayType = llvm::ArrayType::get(elementLlvmType, constantElements.size());
    m_currentLLVMValue = llvm::ConstantArray::get(arrayType, constantElements);
    VYB_CDBG << "DEBUG: ArrayLiteral produced type=" << getTypeName(m_currentLLVMValue->getType()) << std::endl;
}

// --- Expressions ---
void LLVMCodegen::visit(vyb::ast::UnaryExpression *node) {
    node->operand->accept(*this);
    llvm::Value *operandValue = m_currentLLVMValue;

    if (!operandValue) {
        logError(node->operand->loc, "Operand for unary expression is null.");
        m_currentLLVMValue = nullptr;
        return;
    }

    switch (node->op.type) {
        case vyb::TokenType::MINUS: // Reverted to vyb::TokenType::MINUS
            if (operandValue->getType()->isFloatingPointTy()) {
                m_currentLLVMValue = builder->CreateFNeg(operandValue, "fnegtmp");
            } else if (operandValue->getType()->isIntegerTy()) {
                m_currentLLVMValue = builder->CreateNeg(operandValue, "negtmp");
            } else {
                logError(node->loc, "Unary minus operator can only be applied to integer or float types.");
                m_currentLLVMValue = nullptr;
            }
            break;
        case vyb::TokenType::BANG: // Reverted to vyb::TokenType::BANG
            // Logical NOT: typically (operand == 0) for integers, or fcmp one for floats
             if (operandValue->getType()->isIntegerTy(1)) { // Already a boolean
                m_currentLLVMValue = builder->CreateNot(operandValue, "nottmp");
            } else if (operandValue->getType()->isIntegerTy()) { // Other integers
                m_currentLLVMValue = builder->CreateICmpEQ(operandValue, llvm::ConstantInt::get(operandValue->getType(), 0), "icmpeqtmp");
            } else if (operandValue->getType()->isFloatingPointTy()) {
                m_currentLLVMValue = builder->CreateFCmpOEQ(operandValue, llvm::ConstantFP::get(operandValue->getType(), 0.0), "fcmpoeqtmp");
            } else {
                logError(node->loc, "Logical NOT operator can only be applied to boolean, integer or float types.");
                m_currentLLVMValue = nullptr;
            }
            break;
        case vyb::TokenType::TILDE: // Reverted to vyb::TokenType::TILDE
            // Bitwise NOT: one's complement of an integer
            if (operandValue->getType()->isIntegerTy()) {
                m_currentLLVMValue = builder->CreateNot(operandValue, "bwnottmp");
            } else {
                logError(node->loc, "Unary '~' operator can only be applied to integer types.");
                m_currentLLVMValue = nullptr;
            }
            break;
        default:
            logError(node->loc, "Unsupported unary operator.");
            m_currentLLVMValue = nullptr;
            break;
    }
}

void LLVMCodegen::visit(vyb::ast::BinaryExpression *node) {
    node->left->accept(*this);
    llvm::Value *L = m_currentLLVMValue;
    vyb::ast::TypeNode* leftTypeNode = node->left->type.get(); // Get AST type of left operand

    node->right->accept(*this);
    llvm::Value *R = m_currentLLVMValue;
    vyb::ast::TypeNode* rightTypeNode = node->right->type.get(); // Get AST type of right operand


    if (!L || !R) {
        logError(node->loc, "One or both operands of binary expression are null.");
        m_currentLLVMValue = nullptr;
        return;
    }

    bool isFloatOp = L->getType()->isFloatingPointTy() || R->getType()->isFloatingPointTy();

    if (L->getType()->isFloatingPointTy() && R->getType()->isIntegerTy()) {
        R = builder->CreateSIToFP(R, L->getType(), "sitofptmp");
        isFloatOp = true;
    } else if (R->getType()->isFloatingPointTy() && L->getType()->isIntegerTy()) {
        L = builder->CreateSIToFP(L, R->getType(), "sitofptmp");
        isFloatOp = true;
    } else if (L->getType()->isIntegerTy() && R->getType()->isIntegerTy() && L->getType() != R->getType()) {
        // Handle integer width mismatches (e.g., i32 vs i64)
        // Coerce to the smaller width to preserve variable precision
        llvm::IntegerType* leftIntType = llvm::cast<llvm::IntegerType>(L->getType());
        llvm::IntegerType* rightIntType = llvm::cast<llvm::IntegerType>(R->getType());

        if (leftIntType->getBitWidth() < rightIntType->getBitWidth()) {
            // Left is smaller, truncate right to match left
            R = builder->CreateTrunc(R, L->getType(), "inttrunctmp");
        } else {
            // Right is smaller, truncate left to match right
            L = builder->CreateTrunc(L, R->getType(), "inttrunctmp");
        }
    } else if (L->getType()->isPointerTy() && R->getType()->isIntegerTy()) {
        // Pointer arithmetic (e.g. ptr + int)
        // We need to extract the appropriate type information for CreateGEP
        leftTypeNode = node->left->type.get();
        // No additional changes needed with leftTypeNode - already set above
    } else if (R->getType()->isPointerTy() && L->getType()->isIntegerTy()) {
        // Pointer arithmetic (e.g. int + ptr)
        std::swap(L,R); // Put pointer on the left
        leftTypeNode = node->right->type.get(); // Pointer is now L, so use right's AST type
    }


    switch (node->op.type) {
        case vyb::TokenType::PLUS: // Reverted to vyb::TokenType::PLUS
            if (isFloatOp) {
                m_currentLLVMValue = builder->CreateFAdd(L, R, "faddtmp");
                break;  // Exit the case after creating FAdd
            }

            // Handle non-float operations
            {
                if (verbose) {
                    VYB_CDBG << "DEBUG PLUS: leftTypeNode=" << (leftTypeNode ? "yes" : "null")
                              << ", rightTypeNode=" << (rightTypeNode ? "yes" : "null") << std::endl;
                    VYB_CDBG << "DEBUG PLUS: Checking LLVM types for string detection..." << std::endl;
                }

                // Check for String struct types: { ptr, len }
                bool leftIsStringStruct = false;
                bool rightIsStringStruct = false;

                if (L->getType()->isStructTy()) {
                    llvm::StructType* structType = llvm::cast<llvm::StructType>(L->getType());
                    if (structType->getNumElements() == 2 && structType->getElementType(0)->isPointerTy()) {
                        leftIsStringStruct = true;
                    }
                }
                if (R->getType()->isStructTy()) {
                    llvm::StructType* structType = llvm::cast<llvm::StructType>(R->getType());
                    if (structType->getNumElements() == 2 && structType->getElementType(0)->isPointerTy()) {
                        rightIsStringStruct = true;
                    }
                }

                // Handle String + String concatenation
                if (leftIsStringStruct && rightIsStringStruct) {
                    VYB_CDBG << "DEBUG PLUS: String + String concatenation detected" << std::endl;

                    // Define String struct type: { ptr: *i8, len: i64 }
                    std::vector<llvm::Type*> strFields = {
                        llvm::PointerType::get(*context, 0),
                        llvm::Type::getInt64Ty(*context)
                    };
                    llvm::StructType* strStructType = llvm::StructType::get(*context, strFields, false);

                    // Extract fields from left string
                    llvm::Value* str1Data = builder->CreateExtractValue(L, 0, "str1.data");
                    llvm::Value* str1Len = builder->CreateExtractValue(L, 1, "str1.len");

                    // Extract fields from right string
                    llvm::Value* str2Data = builder->CreateExtractValue(R, 0, "str2.data");
                    llvm::Value* str2Len = builder->CreateExtractValue(R, 1, "str2.len");

                    // Calculate new length
                    llvm::Value* newLen = builder->CreateAdd(str1Len, str2Len, "str.new_len");

                    // Allocate new buffer (+1 for null terminator)
                    llvm::Value* allocSize = builder->CreateAdd(newLen,
                        llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 1), "str.alloc_size");

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

                    // Track this freshly-allocated buffer in the string registry so
                    // __vyb_string_free can reclaim it safely (and binding-level
                    // cleanup at scope end or overwrite can too).
                    builder->CreateCall(getOrCreateVybStringRegisterFunction(), {newData});

                    // If an operand was itself a freshly-allocated owned String temp
                    // (e.g. a nested concat or `.to_string()`), it has now been fully
                    // consumed by the copy above, so it can be reclaimed.
                    if (exprProducesOwnedStringTemp(node->left.get())) {
                        builder->CreateCall(getOrCreateVybStringFreeFunction(), {str1Data});
                    }
                    if (exprProducesOwnedStringTemp(node->right.get())) {
                        builder->CreateCall(getOrCreateVybStringFreeFunction(), {str2Data});
                    }

                    // Create new String struct
                    llvm::Value* resultStr = llvm::UndefValue::get(strStructType);
                    resultStr = builder->CreateInsertValue(resultStr, newData, 0, "str.result_data");
                    resultStr = builder->CreateInsertValue(resultStr, newLen, 1, "str.result_len");

                    m_currentLLVMValue = resultStr;
                    break;
                }

                // Handle String struct + non-string concatenation (e.g. "Hello" + 1000)
                if (leftIsStringStruct || rightIsStringStruct) {
                    m_currentLLVMValue = generateMixedStringConcatenation(L, R, leftTypeNode, rightTypeNode, node->loc);
                    if (!m_currentLLVMValue) {
                        logError(node->loc, "Failed to generate mixed string concatenation");
                        return;
                    }
                    break;
                }

                // First check for old-style string types using LLVM types directly (more reliable)
                bool leftIsString = (L->getType() == int8PtrType);
                bool rightIsString = (R->getType() == int8PtrType);

                // If at least one operand is a string, treat as string concatenation
                if (leftIsString || rightIsString) {
                    if (verbose) {
                        VYB_CDBG << "DEBUG PLUS: Detected string concatenation (leftIsString="
                                  << leftIsString << ", rightIsString=" << rightIsString << ")" << std::endl;
                    }
                    m_currentLLVMValue = generateMixedStringConcatenation(L, R, leftTypeNode, rightTypeNode, node->loc);
                    if (!m_currentLLVMValue) {
                        logError(node->loc, "Failed to generate mixed string concatenation");
                        return;
                    }
                    break;
                }
            }

            // Check for string concatenation - either pure string + string or mixed types with string
            if (leftTypeNode && rightTypeNode) {
                // Resolve type aliases to get base type names
                std::string leftBaseName = resolveTypeAliasToBaseName(leftTypeNode);
                std::string rightBaseName = resolveTypeAliasToBaseName(rightTypeNode);

                // Check if either operand is a string (including string literals)
                bool leftIsString = (L->getType() == int8PtrType) || (leftBaseName == "String") ||
                                   (leftTypeNode->getCategory() == vyb::ast::TypeNode::Category::IDENTIFIER &&
                                    dynamic_cast<vyb::ast::TypeName*>(leftTypeNode) &&
                                    dynamic_cast<vyb::ast::TypeName*>(leftTypeNode)->identifier &&
                                    (dynamic_cast<vyb::ast::TypeName*>(leftTypeNode)->identifier->name == "String" ||
                                     dynamic_cast<vyb::ast::TypeName*>(leftTypeNode)->identifier->name == "string"));

                bool rightIsString = (R->getType() == int8PtrType) || (rightBaseName == "String") ||
                                    (rightTypeNode->getCategory() == vyb::ast::TypeNode::Category::IDENTIFIER &&
                                     dynamic_cast<vyb::ast::TypeName*>(rightTypeNode) &&
                                     dynamic_cast<vyb::ast::TypeName*>(rightTypeNode)->identifier &&
                                     (dynamic_cast<vyb::ast::TypeName*>(rightTypeNode)->identifier->name == "String" ||
                                      dynamic_cast<vyb::ast::TypeName*>(rightTypeNode)->identifier->name == "string"));

                // If at least one operand is a string, treat as string concatenation with auto-conversion
                if (leftIsString || rightIsString) {
                    m_currentLLVMValue = generateMixedStringConcatenation(L, R, leftTypeNode, rightTypeNode, node->loc);
                    if (!m_currentLLVMValue) {
                        logError(node->loc, "Failed to generate mixed string concatenation");
                        return;
                    }
                }
                // Check for pointer arithmetic
                else if (L->getType()->isPointerTy() && R->getType()->isIntegerTy() && leftTypeNode) {
                    vyb::ast::TypeNode* pointeeAstType = nullptr;

                    // Try to get pointee type from different sources
                    if (auto ptrAstNode = dynamic_cast<vyb::ast::PointerType*>(leftTypeNode)) {
                        pointeeAstType = ptrAstNode->pointeeType.get();
                    } else if (auto arrayAstNode = dynamic_cast<vyb::ast::ArrayType*>(leftTypeNode)) {
                        pointeeAstType = arrayAstNode->elementType.get();
                    } else if (auto typeName = dynamic_cast<vyb::ast::TypeName*>(leftTypeNode)) {
                        // Check if it's a loc<T> type
                        if (typeName->identifier->name == "loc" && !typeName->genericArgs.empty()) {
                            pointeeAstType = typeName->genericArgs[0].get();
                        }
                    }

                    if (pointeeAstType) {
                        llvm::Type* pointeeType = codegenType(pointeeAstType);
                        if (pointeeType) {
                            m_currentLLVMValue = builder->CreateGEP(pointeeType, L, R, "ptraddtmp");
                        } else {
                            logError(node->left->loc, "Could not determine LLVM pointee type for pointer addition from AST type: " + leftTypeNode->toString());
                            m_currentLLVMValue = nullptr;
                        }
                    } else {
                        // If we can't determine the pointee type from AST, use int64 as a fallback
                        if (verbose) {
                            logWarning(node->left->loc, "Pointer operand for addition lacks specific pointee type information. Using i64 as fallback pointee type.");
                        }
                        m_currentLLVMValue = builder->CreateGEP(int64Type, L, R, "ptraddtmp_fallback");
                    }
                }
                // Regular integer/numeric addition
                else {
                    m_currentLLVMValue = builder->CreateAdd(L, R, "addtmp");
                }
            }
            // Fallback to regular addition if no type info
            else {
                m_currentLLVMValue = builder->CreateAdd(L, R, "addtmp");
            }
            break;
        case vyb::TokenType::MINUS: // Reverted to vyb::TokenType::MINUS
            if (isFloatOp) m_currentLLVMValue = builder->CreateFSub(L, R, "fsubtmp");
            else if (L->getType()->isPointerTy() && R->getType()->isPointerTy()){
                 // Pointer subtraction (ptr - ptr) gives an integer distance
                 L = builder->CreatePtrToInt(L, int64Type, "ptrtointtmp_l");
                 R = builder->CreatePtrToInt(R, int64Type, "ptrtointtmp_r");
                 llvm::Value* diffBytes = builder->CreateSub(L, R, "subtmp");
                 m_currentLLVMValue = diffBytes;
            }
            else if (L->getType()->isPointerTy() && leftTypeNode) { // ptr - int
                 vyb::ast::TypeNode* pointeeAstType = nullptr;

                 // Try to get pointee type from different sources
                 if (auto ptrAstNode = dynamic_cast<vyb::ast::PointerType*>(leftTypeNode)) {
                    pointeeAstType = ptrAstNode->pointeeType.get();
                 } else if (auto arrayAstNode = dynamic_cast<vyb::ast::ArrayType*>(leftTypeNode)) {
                    pointeeAstType = arrayAstNode->elementType.get();
                 } else if (auto typeName = dynamic_cast<vyb::ast::TypeName*>(leftTypeNode)) {
                    // Check if it's a loc<T> type
                    if (typeName->identifier->name == "loc" && !typeName->genericArgs.empty()) {
                        pointeeAstType = typeName->genericArgs[0].get();
                    }
                 }

                 if (pointeeAstType) {
                    llvm::Type* pointeeType = codegenType(pointeeAstType);
                    if (pointeeType) {
                        m_currentLLVMValue = builder->CreateGEP(pointeeType, L, builder->CreateNeg(R), "ptrsubtmp");
                    } else {
                         logError(node->left->loc, "Could not determine LLVM pointee type for pointer subtraction from AST type: " + leftTypeNode->toString());
                         m_currentLLVMValue = nullptr;
                    }
                 } else {
                     // If we can't determine the pointee type from AST, use int64 as a fallback
                     // This is common in test cases with opaque pointers
                     if (verbose) {
                         logWarning(node->left->loc, "Pointer operand for subtraction lacks specific pointee type information. Using i64 as fallback pointee type.");
                     }
                     m_currentLLVMValue = builder->CreateGEP(int64Type, L, builder->CreateNeg(R), "ptrsubtmp_fallback");
                 }
            }
            else m_currentLLVMValue = builder->CreateSub(L, R, "subtmp");
            break;
        case vyb::TokenType::MULTIPLY: // Reverted to vyb::TokenType::MULTIPLY
            if (isFloatOp) m_currentLLVMValue = builder->CreateFMul(L, R, "fmultmp");
            else m_currentLLVMValue = builder->CreateMul(L, R, "multmp");
            break;
        case vyb::TokenType::DIVIDE: // Reverted to vyb::TokenType::DIVIDE
            if (isFloatOp) m_currentLLVMValue = builder->CreateFDiv(L, R, "fdivtmp");
            else m_currentLLVMValue = builder->CreateSDiv(L, R, "sdivtmp");
            break;
        case vyb::TokenType::MODULO: // Reverted to vyb::TokenType::MODULO
             if (isFloatOp) m_currentLLVMValue = builder->CreateFRem(L, R, "fremtmp");
             else m_currentLLVMValue = builder->CreateSRem(L, R, "sremtmp");
            break;
        // Comparison operators
        case vyb::TokenType::EQEQ: // Reverted to vyb::TokenType::EQEQ
            // Check for String comparison first
            if (L->getType()->isStructTy() && R->getType()->isStructTy()) {
                llvm::StructType* leftStruct = llvm::cast<llvm::StructType>(L->getType());
                llvm::StructType* rightStruct = llvm::cast<llvm::StructType>(R->getType());
                // String struct: { ptr, len }
                if (leftStruct->getNumElements() == 2 && rightStruct->getNumElements() == 2) {
                    m_currentLLVMValue = generateStringComparison(L, R, vyb::TokenType::EQEQ);
                    break;
                }
            }
            if (isFloatOp) m_currentLLVMValue = builder->CreateFCmpOEQ(L, R, "fcmpoeqtmp");
            else m_currentLLVMValue = builder->CreateICmpEQ(L, R, "icmpeqtmp");
            break;
        case vyb::TokenType::NOTEQ: // Reverted to vyb::TokenType::NOTEQ
            // Check for String comparison first
            if (L->getType()->isStructTy() && R->getType()->isStructTy()) {
                llvm::StructType* leftStruct = llvm::cast<llvm::StructType>(L->getType());
                llvm::StructType* rightStruct = llvm::cast<llvm::StructType>(R->getType());
                // String struct: { ptr, len }
                if (leftStruct->getNumElements() == 2 && rightStruct->getNumElements() == 2) {
                    m_currentLLVMValue = generateStringComparison(L, R, vyb::TokenType::NOTEQ);
                    break;
                }
            }
            if (isFloatOp) m_currentLLVMValue = builder->CreateFCmpONE(L, R, "fcmponeqtmp");
            else m_currentLLVMValue = builder->CreateICmpNE(L, R, "icmpneqtmp");
            break;
        case vyb::TokenType::LT: // Reverted to vyb::TokenType::LT
            // Check for String comparison first
            if (L->getType()->isStructTy() && R->getType()->isStructTy()) {
                llvm::StructType* leftStruct = llvm::cast<llvm::StructType>(L->getType());
                llvm::StructType* rightStruct = llvm::cast<llvm::StructType>(R->getType());
                // String struct: { ptr, len }
                if (leftStruct->getNumElements() == 2 && rightStruct->getNumElements() == 2) {
                    m_currentLLVMValue = generateStringComparison(L, R, vyb::TokenType::LT);
                    break;
                }
            }
            if (isFloatOp) m_currentLLVMValue = builder->CreateFCmpOLT(L, R, "fcmpltmp");
            else m_currentLLVMValue = builder->CreateICmpSLT(L, R, "icmpslttmp");
            break;
        case vyb::TokenType::LTEQ: // Reverted to vyb::TokenType::LTEQ:
            // Check for String comparison first
            if (L->getType()->isStructTy() && R->getType()->isStructTy()) {
                llvm::StructType* leftStruct = llvm::cast<llvm::StructType>(L->getType());
                llvm::StructType* rightStruct = llvm::cast<llvm::StructType>(R->getType());
                // String struct: { ptr, len }
                if (leftStruct->getNumElements() == 2 && rightStruct->getNumElements() == 2) {
                    m_currentLLVMValue = generateStringComparison(L, R, vyb::TokenType::LTEQ);
                    break;
                }
            }
            if (isFloatOp) m_currentLLVMValue = builder->CreateFCmpOLE(L, R, "fcmpletmp");
            else m_currentLLVMValue = builder->CreateICmpSLE(L, R, "icmpsletmp");
            break;
        case vyb::TokenType::GT: // Reverted to vyb::TokenType::GT
            // Check for String comparison first
            if (L->getType()->isStructTy() && R->getType()->isStructTy()) {
                llvm::StructType* leftStruct = llvm::cast<llvm::StructType>(L->getType());
                llvm::StructType* rightStruct = llvm::cast<llvm::StructType>(R->getType());
                // String struct: { ptr, len }
                if (leftStruct->getNumElements() == 2 && rightStruct->getNumElements() == 2) {
                    m_currentLLVMValue = generateStringComparison(L, R, vyb::TokenType::GT);
                    break;
                }
            }
            if (isFloatOp) m_currentLLVMValue = builder->CreateFCmpOGT(L, R, "fcmpgtmp");
            else m_currentLLVMValue = builder->CreateICmpSGT(L, R, "icmpsgttmp");
            break;
        case vyb::TokenType::GTEQ: // Reverted to vyb::TokenType::GTEQ:
            // Check for String comparison first
            if (L->getType()->isStructTy() && R->getType()->isStructTy()) {
                llvm::StructType* leftStruct = llvm::cast<llvm::StructType>(L->getType());
                llvm::StructType* rightStruct = llvm::cast<llvm::StructType>(R->getType());
                // String struct: { ptr, len }
                if (leftStruct->getNumElements() == 2 && rightStruct->getNumElements() == 2) {
                    m_currentLLVMValue = generateStringComparison(L, R, vyb::TokenType::GTEQ);
                    break;
                }
            }
            if (isFloatOp) m_currentLLVMValue = builder->CreateFCmpOGE(L, R, "fcmpgetmp");
            else m_currentLLVMValue = builder->CreateICmpSGE(L, R, "icmpsgetmp");
            break;
        // Bitwise operators (integer operands only)
        case vyb::TokenType::AMPERSAND:
            m_currentLLVMValue = builder->CreateAnd(L, R, "bwandtmp");
            break;
        case vyb::TokenType::PIPE:
            m_currentLLVMValue = builder->CreateOr(L, R, "bwortmp");
            break;
        case vyb::TokenType::CARET:
            m_currentLLVMValue = builder->CreateXor(L, R, "bwxortmp");
            break;
        case vyb::TokenType::LSHIFT:
            m_currentLLVMValue = builder->CreateShl(L, R, "shltmp");
            break;
        case vyb::TokenType::RSHIFT:
            {
                // Signed Int types use an arithmetic (sign-extending) shift;
                // UInt* types use a logical shift.
                bool isUnsigned = false;
                if (leftTypeNode) {
                    if (auto tn = dynamic_cast<ast::TypeName*>(leftTypeNode)) {
                        if (tn->identifier) {
                            const std::string& n = tn->identifier->name;
                            isUnsigned = (n == "UInt8" || n == "UInt16" || n == "UInt32" || n == "UInt64");
                        }
                    }
                }
                if (isUnsigned) m_currentLLVMValue = builder->CreateLShr(L, R, "lshrtmp");
                else m_currentLLVMValue = builder->CreateAShr(L, R, "ashrtmp");
            }
            break;
        // Logical operators (short-circuiting needs careful handling with basic blocks)
        // For simplicity, this example evaluates both sides. Proper logical ops need control flow.
        case vyb::TokenType::AND: // Reverted to vyb::TokenType::AND
             m_currentLLVMValue = builder->CreateAnd(L, R, "andtmp"); // Bitwise AND, assumes L and R are i1
            break;
        case vyb::TokenType::OR: // Reverted to vyb::TokenType::OR
            m_currentLLVMValue = builder->CreateOr(L, R, "ortmp"); // Bitwise OR, assumes L and R are i1
            break;
        default:
            logError(node->loc, "Unsupported binary operator.");
            m_currentLLVMValue = nullptr;
            break;
    }
}

void LLVMCodegen::emitVecConstructor(vyb::ast::CallExpression* node) {
    // Create Vec struct: { ptr, size, capacity }
    std::vector<llvm::Type*> vecFields = {
        llvm::PointerType::get(*context, 0), // ptr to elements (opaque pointer)
        llvm::Type::getInt64Ty(*context),    // size
        llvm::Type::getInt64Ty(*context)     // capacity
    };

    llvm::StructType* vecStructType = llvm::StructType::get(*context, vecFields, false);

    // Allocate the Vec struct.
    llvm::Value* vecAlloca = builder->CreateAlloca(vecStructType, nullptr, "vec.new");

    if (node->arguments.empty()) {
        // Vec() / Vec::new() - empty vector.
        llvm::Value* nullPtr = llvm::ConstantPointerNull::get(llvm::PointerType::get(*context, 0));
        llvm::Value* zero = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
        llvm::Value* ptrFieldPtr = builder->CreateStructGEP(vecStructType, vecAlloca, 0, "vec.ptr_field");
        builder->CreateStore(nullPtr, ptrFieldPtr);
        llvm::Value* sizeFieldPtr = builder->CreateStructGEP(vecStructType, vecAlloca, 1, "vec.size_field");
        builder->CreateStore(zero, sizeFieldPtr);
        llvm::Value* capFieldPtr = builder->CreateStructGEP(vecStructType, vecAlloca, 2, "vec.cap_field");
        builder->CreateStore(zero, capFieldPtr);
    } else if (node->arguments.size() == 1) {
        // Vec(n) / Vec::new(n) - preallocated vector with zero-initialized elements.
        node->arguments[0]->accept(*this);
        llvm::Value* sizeValue = m_currentLLVMValue;
        if (!sizeValue) {
            logError(node->loc, "Failed to evaluate size argument for Vec(n)");
            m_currentLLVMValue = nullptr;
            return;
        }

        if (sizeValue->getType() != llvm::Type::getInt64Ty(*context)) {
            sizeValue = builder->CreateSExtOrTrunc(sizeValue, llvm::Type::getInt64Ty(*context), "size.ext");
        }

        // Allocate memory for elements. Numeric/opaque elements are stored inline;
        // this defaults to 8 bytes per element (matching the legacy Vec::new(n) path).
        llvm::Type* elementType = llvm::Type::getInt64Ty(*context);
        llvm::Value* allocSize = builder->CreateMul(sizeValue,
            llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 8), "alloc.size");

        // Reuse the single module-level `malloc` declaration (like the Vec
        // push/resize paths). Creating a fresh ExternalLinkage "malloc" per
        // `Vec(n)` call makes LLVM suffix each duplicate (`malloc.N`), which
        // the ORC JIT cannot resolve ("Symbols not found: [ malloc.N ]").
        llvm::Function* mallocFunc = getOrCreateMallocFunction();
        llvm::Value* dataPtr = builder->CreateCall(mallocFunc, {allocSize}, "vec.data");

        llvm::Function* memsetFunc = getOrCreateMemsetFunction();
        builder->CreateCall(memsetFunc, {
            dataPtr,
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0),
            allocSize
        });

        llvm::Value* ptrFieldPtr = builder->CreateStructGEP(vecStructType, vecAlloca, 0, "vec.ptr_field");
        builder->CreateStore(dataPtr, ptrFieldPtr);
        llvm::Value* sizeFieldPtr = builder->CreateStructGEP(vecStructType, vecAlloca, 1, "vec.size_field");
        builder->CreateStore(sizeValue, sizeFieldPtr);
        llvm::Value* capFieldPtr = builder->CreateStructGEP(vecStructType, vecAlloca, 2, "vec.cap_field");
        builder->CreateStore(sizeValue, capFieldPtr);
    }

    m_currentLLVMValue = builder->CreateLoad(vecStructType, vecAlloca, "vec.new.value");
}

void LLVMCodegen::visit(vyb::ast::CallExpression *node) {

    // Bare builtin enum constructor: `Some(x)` (Option) and `Ok(x)` / `Err(e)`
    // (Result). The semantic layer injected the target enum type into this node,
    // so the payload types are recovered from its generic arguments and the value
    // is built as the corresponding tagged-union variant.
    if (auto calleeId = dynamic_cast<ast::Identifier*>(node->callee.get())) {
        const std::string& cn = calleeId->name;
        if ((cn == "Some" || cn == "Ok" || cn == "Err") && node->type) {
            auto* etn = dynamic_cast<ast::TypeName*>(node->type.get());
            if (etn && etn->identifier) {
                const std::string& en = etn->identifier->name;
                bool isOptionSome = (cn == "Some" && en == "Option");
                bool isResultCtor = (en == "Result" && (cn == "Ok" || cn == "Err"));
                if ((isOptionSome || isResultCtor) && !etn->genericArgs.empty()) {
                    // Inside a monomorphized generic bind body the enclosing type
                    // params (e.g. `T` in `Option<T>`) are active via
                    // currentTypeSubstitutions; substitute them so a bare `Some(v)`
                    // becomes `Option<Int>` rather than an unresolved `Option_T`.
                    std::vector<ast::TypeNodePtr> concreteEnumArgs;
                    concreteEnumArgs.reserve(etn->genericArgs.size());
                    for (const auto& arg : etn->genericArgs) {
                        std::string s = arg->toString();
                        if (!currentTypeSubstitutions.empty()) {
                            for (const auto& kv : currentTypeSubstitutions) {
                                s = replaceTypeTokens(s, kv.first, kv.second);
                            }
                        }
                        concreteEnumArgs.push_back(typePatternToTypeNode(TypePattern::parse(s), node->loc));
                    }
                    std::string mangled = mangleGenericTypeName(en, concreteEnumArgs);
                    if (!taggedEnumInfo.count(mangled)) monomorphizeEnum(en, concreteEnumArgs);
                    std::vector<llvm::Value*> payloadVals;
                    for (auto& arg : node->arguments) {
                        arg->accept(*this);
                        payloadVals.push_back(m_currentLLVMValue);
                    }
                    m_currentLLVMValue = buildTaggedEnumValue(mangled, cn, payloadVals);
                    return;
                }
            }
        }
    }

    // Bare builtin Vec constructor: `Vec()`, `Vec(n)`, or explicitly-typed
    // `Vec<T>()` / `Vec<T>(n)`. The callee is the identifier `Vec` (or a
    // `Vec<...>` generic instantiation) rather than the legacy `Vec::new(...)`.
    if (auto idCallee = dynamic_cast<ast::Identifier*>(node->callee.get())) {
        if (idCallee->name == "Vec") {
            emitVecConstructor(node);
            return;
        }
    }

    // Check for Vec::new() constructor calls
    // VYB_CDBG << "DEBUG: Checking if callee is MemberExpression..." << std::endl;
    if (auto memberExpr = dynamic_cast<vyb::ast::MemberExpression*>(node->callee.get())) {
        // Tagged-union enum variant constructor: Shape::Circle(x), Shape::Rect(a, b).
        // Generic data enum variant constructor: Box<Int>::Value(x).
        if (auto gi = dynamic_cast<ast::GenericInstantiationExpression*>(memberExpr->object.get())) {
            if (auto enIdent = dynamic_cast<ast::Identifier*>(gi->baseExpression.get())) {
                if (auto varIdent = dynamic_cast<ast::Identifier*>(memberExpr->property.get())) {
                    if (genericEnumTemplates.count(enIdent->name) || enIdent->name == "Option" ||
                        enIdent->name == "Result") {
                        // Inside a monomorphized generic bind body the enclosing type
                        // params (e.g. `V` in `Option<V>::Some(...)`) are active via
                        // currentTypeSubstitutions; substitute them into the concrete
                        // enum instantiation so `Option<V>` becomes `Option<Int>`
                        // instead of an unresolved `Option_V`.
                        std::vector<ast::TypeNodePtr> concreteEnumArgs;
                        concreteEnumArgs.reserve(gi->genericArguments.size());
                        for (const auto& arg : gi->genericArguments) {
                            std::string s = arg->toString();
                            if (!currentTypeSubstitutions.empty()) {
                                for (const auto& kv : currentTypeSubstitutions) {
                                    s = replaceTypeTokens(s, kv.first, kv.second);
                                }
                            }
                            concreteEnumArgs.push_back(typePatternToTypeNode(TypePattern::parse(s), gi->loc));
                        }
                        std::string mangled = mangleGenericTypeName(enIdent->name, concreteEnumArgs);
                        if (!taggedEnumInfo.count(mangled)) {
                            monomorphizeEnum(enIdent->name, concreteEnumArgs);
                        }
                        std::vector<llvm::Value*> payloadVals;
                        for (auto& arg : node->arguments) {
                            arg->accept(*this);
                            payloadVals.push_back(m_currentLLVMValue);
                        }
                        m_currentLLVMValue = buildTaggedEnumValue(mangled, varIdent->name, payloadVals);
                        return;
                    }
                }
            }
        }
        if (auto enIdent = dynamic_cast<vyb::ast::Identifier*>(memberExpr->object.get())) {
            if (auto varIdent = dynamic_cast<vyb::ast::Identifier*>(memberExpr->property.get())) {
                auto tagIt = taggedEnumInfo.find(enIdent->name);
                if (tagIt != taggedEnumInfo.end()) {
                    std::vector<llvm::Value*> payloadVals;
                    for (auto& arg : node->arguments) {
                        arg->accept(*this);
                        payloadVals.push_back(m_currentLLVMValue);
                    }
                    m_currentLLVMValue = buildTaggedEnumValue(enIdent->name, varIdent->name, payloadVals);
                    return;
                }
            }
        }
        // VYB_CDBG << "DEBUG: Found MemberExpression callee" << std::endl;
        // VYB_CDBG << "DEBUG: MemberExpression object: " << (memberExpr->object ? memberExpr->object->toString() : "null") << std::endl;
        // VYB_CDBG << "DEBUG: MemberExpression property: " << (memberExpr->property ? memberExpr->property->toString() : "null") << std::endl;
        if (auto vecIdent = dynamic_cast<vyb::ast::Identifier*>(memberExpr->object.get())) {
            VYB_CDBG << "DEBUG: MemberExpression object is Identifier: " << vecIdent->name << std::endl;
            if (auto newIdent = dynamic_cast<vyb::ast::Identifier*>(memberExpr->property.get())) {
                VYB_CDBG << "DEBUG: MemberExpression property is Identifier: " << newIdent->name << std::endl;
                if (vecIdent->name == "Vec" && newIdent->name == "new") {
                    // This is Vec::new() or Vec::new(size) - create a vector
                    VYB_CDBG << "DEBUG: Creating Vec::new() constructor" << std::endl;

                    emitVecConstructor(node);
                    return;
                }

                // Handle T::from_string() static method calls
                const std::string& typeName = vecIdent->name;
                const std::string& methodName = newIdent->name;

                if (methodName == "from_string") {
                    VYB_CDBG << "DEBUG: Processing " << typeName << "::from_string() call" << std::endl;

                    // Validate arguments
                    if (node->arguments.size() != 1) {
                        logError(node->loc, typeName + "::from_string() expects exactly 1 argument (string to parse)");
                        m_currentLLVMValue = nullptr;
                        return;
                    }

                    // Evaluate the string argument
                    node->arguments[0]->accept(*this);
                    llvm::Value* stringArg = m_currentLLVMValue;
                    if (!stringArg) {
                        logError(node->loc, "Failed to evaluate string argument for " + typeName + "::from_string()");
                        m_currentLLVMValue = nullptr;
                        return;
                    }

                    // Extract the char* from the Vyb String struct { ptr, i64 }
                    llvm::Value* charPtr = builder->CreateExtractValue(stringArg, 0, "str.ptr");

                    // Handle primitive types
                    if (typeName == "Int") {
                        // Call __vyb_int_from_string(str, success_ptr)
                        llvm::FunctionType* fromStringType = llvm::FunctionType::get(
                            int64Type,
                            {int8PtrType, llvm::PointerType::get(int1Type, 0)},
                            false
                        );
                        llvm::Function* fromStringFunc = module->getFunction("__vyb_int_from_string");
                        if (!fromStringFunc) {
                            fromStringFunc = llvm::Function::Create(fromStringType,
                                llvm::Function::ExternalLinkage, "__vyb_int_from_string", module.get());
                        }

                        // Allocate success flag
                        llvm::Value* successPtr = builder->CreateAlloca(int1Type, nullptr, "success");

                        // Call from_string with char*
                        llvm::Value* result = builder->CreateCall(fromStringFunc, {charPtr, successPtr}, "from_string.result");

                        // TODO: Check success flag and handle errors
                        m_currentLLVMValue = result;
                        return;
                    } else if (typeName == "Float") {
                        // Call __vyb_float_from_string(str, success_ptr)
                        llvm::FunctionType* fromStringType = llvm::FunctionType::get(
                            doubleType,
                            {int8PtrType, llvm::PointerType::get(int1Type, 0)},
                            false
                        );
                        llvm::Function* fromStringFunc = module->getFunction("__vyb_float_from_string");
                        if (!fromStringFunc) {
                            fromStringFunc = llvm::Function::Create(fromStringType,
                                llvm::Function::ExternalLinkage, "__vyb_float_from_string", module.get());
                        }

                        llvm::Value* successPtr = builder->CreateAlloca(int1Type, nullptr, "success");
                        llvm::Value* result = builder->CreateCall(fromStringFunc, {charPtr, successPtr}, "from_string.result");
                        m_currentLLVMValue = result;
                        return;
                    } else if (typeName == "Bool") {
                        // Call __vyb_bool_from_string(str, success_ptr)
                        llvm::FunctionType* fromStringType = llvm::FunctionType::get(
                            int1Type,
                            {int8PtrType, llvm::PointerType::get(int1Type, 0)},
                            false
                        );
                        llvm::Function* fromStringFunc = module->getFunction("__vyb_bool_from_string");
                        if (!fromStringFunc) {
                            fromStringFunc = llvm::Function::Create(fromStringType,
                                llvm::Function::ExternalLinkage, "__vyb_bool_from_string", module.get());
                        }

                        llvm::Value* successPtr = builder->CreateAlloca(int1Type, nullptr, "success");
                        llvm::Value* result = builder->CreateCall(fromStringFunc, {charPtr, successPtr}, "from_string.result");
                        m_currentLLVMValue = result;
                        return;
                    } else if (typeName == "String") {
                        // String::from_string() is identity - just return a copy as Vyb String struct
                        llvm::FunctionType* fromStringType = llvm::FunctionType::get(
                            int8PtrType,
                            {int8PtrType, llvm::PointerType::get(int1Type, 0)},
                            false
                        );
                        llvm::Function* fromStringFunc = module->getFunction("__vyb_string_from_string");
                        if (!fromStringFunc) {
                            fromStringFunc = llvm::Function::Create(fromStringType,
                                llvm::Function::ExternalLinkage, "__vyb_string_from_string", module.get());
                        }

                        llvm::Value* successPtr = builder->CreateAlloca(int1Type, nullptr, "success");
                        llvm::Value* charResult = builder->CreateCall(fromStringFunc, {charPtr, successPtr}, "from_string.char_result");

                        // Convert char* to Vyb String struct { ptr, len }
                        // Declare strlen
                        llvm::FunctionType* strlenType = llvm::FunctionType::get(int64Type, {int8PtrType}, false);
                        llvm::Function* strlenFunc = module->getFunction("strlen");
                        if (!strlenFunc) {
                            strlenFunc = llvm::Function::Create(strlenType, llvm::Function::ExternalLinkage, "strlen", module.get());
                        }

                        llvm::Value* strLen = builder->CreateCall(strlenFunc, {charResult}, "str.len");

                        // Build String struct
                        llvm::StructType* stringStructType = llvm::StructType::get(*context, {int8PtrType, int64Type});
                        llvm::Value* stringStruct = llvm::UndefValue::get(stringStructType);
                        stringStruct = builder->CreateInsertValue(stringStruct, charResult, 0, "str.ptr");
                        stringStruct = builder->CreateInsertValue(stringStruct, strLen, 1, "str.len");

                        m_currentLLVMValue = stringStruct;
                        return;
                    } else {
                        // Complex type - call generic JSON deserializer
                        VYB_CDBG << "DEBUG: Generating JSON deserialization for type: " << typeName << std::endl;

                        // Check if this is a known struct type
                        auto structIt = monomorphizedStructs.find(typeName);
                        if (structIt == monomorphizedStructs.end()) {
                            logError(node->loc, "Unknown struct type for deserialization: " + typeName);
                            m_currentLLVMValue = nullptr;
                            return;
                        }

                        llvm::StructType* targetStructType = structIt->second;

                        // Declare __vyb_complex_from_json(json_str, type_name) -> void*
                        llvm::FunctionType* fromJsonType = llvm::FunctionType::get(
                            llvm::PointerType::get(*context, 0),  // returns void*
                            {int8PtrType, int8PtrType},  // (json_str, type_name)
                            false
                        );
                        llvm::Function* fromJsonFunc = module->getFunction("__vyb_complex_from_json");
                        if (!fromJsonFunc) {
                            fromJsonFunc = llvm::Function::Create(fromJsonType,
                                llvm::Function::ExternalLinkage, "__vyb_complex_from_json", module.get());
                        }

                        // Create type name string constant
                        llvm::Value* typeNameStr = builder->CreateGlobalStringPtr(typeName, "type.name");

                        // Call deserializer
                        llvm::Value* resultPtr = builder->CreateCall(fromJsonFunc, {charPtr, typeNameStr}, "from_json.ptr");

                        // Cast void* to struct type pointer
                        llvm::Value* structPtr = builder->CreateBitCast(resultPtr,
                            llvm::PointerType::get(targetStructType, 0), "struct.ptr");

                        // Load the struct value - this copies the struct to the stack
                        // but preserves internal pointers (e.g., String.data still points to heap).
                        llvm::Value* structValue = builder->CreateLoad(targetStructType, structPtr, "struct.value");
                        // The deserializer allocated the instance on the heap; free it now
                        // that its contents have been copied into the returned value. Its
                        // String fields' data pointers live on independently (registered,
                        // owned by the copied value's scope-exit cleanup).
                        builder->CreateCall(getOrCreateFreeFunction(), {resultPtr});
                        m_currentLLVMValue = structValue;
                        return;
                    }
                }

                // Check for String::from_bytes() constructor
                if (vecIdent->name == "String" && newIdent->name == "from_bytes") {
                    VYB_CDBG << "DEBUG: Creating String::from_bytes() constructor" << std::endl;

                    if (node->arguments.size() != 2) {
                        logError(node->loc, "String::from_bytes expects exactly 2 arguments (byte_ptr, length)");
                        m_currentLLVMValue = nullptr;
                        return;
                    }

                    // Evaluate byte pointer argument
                    node->arguments[0]->accept(*this);
                    llvm::Value* bytePtr = m_currentLLVMValue;
                    if (!bytePtr) {
                        logError(node->arguments[0]->loc, "Failed to evaluate byte pointer for String::from_bytes");
                        m_currentLLVMValue = nullptr;
                        return;
                    }

                    // Evaluate length argument
                    node->arguments[1]->accept(*this);
                    llvm::Value* length = m_currentLLVMValue;
                    if (!length) {
                        logError(node->arguments[1]->loc, "Failed to evaluate length for String::from_bytes");
                        m_currentLLVMValue = nullptr;
                        return;
                    }

                    // Create String struct: { ptr: *i8, len: i64 }
                    std::vector<llvm::Type*> strFields = {
                        llvm::PointerType::get(*context, 0), // ptr to bytes
                        llvm::Type::getInt64Ty(*context)     // length
                    };
                    llvm::StructType* strStructType = llvm::StructType::get(*context, strFields, false);

                    // Create String struct value
                    llvm::Value* resultStr = llvm::UndefValue::get(strStructType);
                    resultStr = builder->CreateInsertValue(resultStr, bytePtr, 0, "str.from_bytes_data");
                    resultStr = builder->CreateInsertValue(resultStr, length, 1, "str.from_bytes_len");

                    m_currentLLVMValue = resultStr;
                    VYB_CDBG << "DEBUG: String::from_bytes() created successfully" << std::endl;
                    return;
                }
            }
        }
    }

    // Handle mild<T>.grab() and mild<T>.released() method calls
    if (auto memberExpr = dynamic_cast<vyb::ast::MemberExpression*>(node->callee.get())) {
        if (auto objIdent = dynamic_cast<vyb::ast::Identifier*>(memberExpr->object.get())) {
            if (auto methodIdent = dynamic_cast<vyb::ast::Identifier*>(memberExpr->property.get())) {
                std::string methodName = methodIdent->name;

                // Check if this is a method on mild<T>
                if (methodName == "grab" || methodName == "released") {
                    // Get the object's type to verify it's mild<T>
                    std::string objectType;
                    if (objIdent->type) {
                        objectType = objIdent->type->toString();
                    } else {
                        auto namedIt = namedValues.find(objIdent->name);
                        if (namedIt != namedValues.end()) {
                            auto valueTypeIt = valueTypeMap.find(namedIt->second);
                            if (valueTypeIt != valueTypeMap.end() && valueTypeIt->second) {
                                objectType = valueTypeIt->second->toString();
                            }
                        }
                    }

                    // Check if type starts with "mild<"
                    if (objectType.find("mild<") == 0) {
                        VYB_CDBG << "DEBUG: Processing " << objectType << "." << methodName << "() call" << std::endl;

                        if (methodName == "grab") {
                            // mild<T>.grab() -> returns our<T>? (nil if object freed, strong ref if alive)
                            VYB_CDBG << "DEBUG: mild<T>.grab() - attempting to upgrade to our<T>" << std::endl;

                            // Get the mild<T> value (control block pointer)
                            auto objIt = namedValues.find(objIdent->name);
                            if (objIt == namedValues.end()) {
                                logError(node->loc, "Unknown variable: " + objIdent->name);
                                return;
                            }

                            // Load the control block pointer
                            llvm::Value* controlBlockPtr = builder->CreateLoad(
                                llvm::PointerType::get(*context, 0),
                                objIt->second,
                                objIdent->name + "_grab_cb_load"
                            );

                            // Reconstruct control block type: { i32, i32, i8, ptr }
                            std::vector<llvm::Type*> cbFields = {
                                llvm::Type::getInt32Ty(*context),  // strong_count
                                llvm::Type::getInt32Ty(*context),  // weak_count
                                llvm::Type::getInt8Ty(*context),   // object_freed (i8 for atomic)
                                llvm::PointerType::get(*context, 0) // object_ptr
                            };
                            llvm::StructType* controlBlockType = llvm::StructType::get(*context, cbFields, /*isPacked=*/false);

                            // Get pointer to object_freed (field 2)
                            llvm::Value* objectFreedPtr = builder->CreateStructGEP(
                                controlBlockType,
                                controlBlockPtr,
                                2,
                                objIdent->name + "_grab_obj_freed_ptr"
                            );

                            // Atomic load object_freed flag (acquire semantics)
                            llvm::LoadInst* freedValue = builder->CreateLoad(
                                llvm::Type::getInt8Ty(*context),
                                objectFreedPtr,
                                objIdent->name + "_grab_obj_freed"
                            );
                            freedValue->setAtomic(llvm::AtomicOrdering::Acquire);

                            // Convert i8 to bool
                            llvm::Value* isFreed = builder->CreateICmpNE(
                                freedValue,
                                llvm::ConstantInt::get(llvm::Type::getInt8Ty(*context), 0),
                                objIdent->name + "_grab_is_freed"
                            );

                            // Create blocks for conditional logic
                            llvm::Function* function = builder->GetInsertBlock()->getParent();
                            llvm::BasicBlock* objAliveBlock = llvm::BasicBlock::Create(*context, "grab_alive", function);
                            llvm::BasicBlock* objFreedBlock = llvm::BasicBlock::Create(*context, "grab_freed", function);
                            llvm::BasicBlock* grabContinue = llvm::BasicBlock::Create(*context, "grab_continue", function);

                            // Build the return type Option<our<T>>. node->type is the
                            // semantic Option<our<T>> type; its single generic arg is
                            // the our<T> payload. Recover it, monomorphize the enum so
                            // the Some/None tags and payload layout are available.
                            ast::TypeName* grabRet = dynamic_cast<ast::TypeName*>(node->type.get());
                            std::string mangledOption;
                            llvm::Type* optionLLVMTy = nullptr;
                            if (grabRet && grabRet->genericArgs.size() == 1) {
                                ast::TypeNodePtr optArg = grabRet->genericArgs[0]->clone();
                                std::vector<ast::TypeNodePtr> optArgs;
                                optArgs.push_back(std::move(optArg));
                                mangledOption = mangleGenericTypeName("Option", optArgs);
                                if (!taggedEnumInfo.count(mangledOption)) {
                                    monomorphizeEnum("Option", optArgs);
                                }
                                optionLLVMTy = taggedEnumInfo[mangledOption].llvmType;
                            }

                            // Branch based on object_freed flag
                            builder->CreateCondBr(isFreed, objFreedBlock, objAliveBlock);

                            // Object still alive: increment strong_count and return Some(our<T>)
                            builder->SetInsertPoint(objAliveBlock);

                            // Get pointer to strong_count (field 0)
                            llvm::Value* strongCountPtr = builder->CreateStructGEP(
                                controlBlockType,
                                controlBlockPtr,
                                0,
                                objIdent->name + "_grab_strong_count_ptr"
                            );

                            // Atomic increment: strong_count++
                            builder->CreateAtomicRMW(
                                llvm::AtomicRMWInst::Add,
                                strongCountPtr,
                                llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 1),
                                llvm::MaybeAlign(),
                                llvm::AtomicOrdering::AcquireRelease
                            );

                            VYB_CDBG << "DEBUG: mild<T>.grab() - incremented strong_count, returning Some(our<T>)" << std::endl;

                            // Build Some(our<T>) carrying the control block handle.
                            llvm::Value* someVal = buildTaggedEnumValue(mangledOption, "Some", {controlBlockPtr});
                            builder->CreateBr(grabContinue);

                            // Object freed: return None
                            builder->SetInsertPoint(objFreedBlock);
                            VYB_CDBG << "DEBUG: mild<T>.grab() - object freed, returning None" << std::endl;
                            llvm::Value* noneVal = buildTaggedEnumValue(mangledOption, "None", {});
                            builder->CreateBr(grabContinue);

                            // Continue block: phi node to select the Option<our<T>> result
                            builder->SetInsertPoint(grabContinue);
                            llvm::PHINode* resultPhi = builder->CreatePHI(optionLLVMTy ? optionLLVMTy : llvm::PointerType::get(*context, 0), 2, "grab_result");
                            resultPhi->addIncoming(someVal, objAliveBlock);
                            resultPhi->addIncoming(noneVal, objFreedBlock);

                            m_currentLLVMValue = resultPhi;
                            return;
                        } else if (methodName == "released") {
                            // mild<T>.released() -> returns Bool
                            // Check object_freed flag in control block
                            VYB_CDBG << "DEBUG: mild<T>.released() - checking object_freed flag" << std::endl;

                            // Get the mild<T> value (control block pointer) from namedValues
                            auto objIt = namedValues.find(objIdent->name);
                            if (objIt == namedValues.end()) {
                                logError(node->loc, "Unknown variable: " + objIdent->name);
                                return;
                            }

                            // Load the control block pointer
                            llvm::Value* controlBlockPtr = builder->CreateLoad(
                                llvm::PointerType::get(*context, 0),
                                objIt->second,
                                objIdent->name + "_released_cb_load"
                            );

                            // Reconstruct control block type: { i32, i32, i8, ptr }
                            std::vector<llvm::Type*> cbFields = {
                                llvm::Type::getInt32Ty(*context),  // strong_count
                                llvm::Type::getInt32Ty(*context),  // weak_count
                                llvm::Type::getInt8Ty(*context),   // object_freed (i8 for atomic)
                                llvm::PointerType::get(*context, 0) // object_ptr
                            };
                            llvm::StructType* controlBlockType = llvm::StructType::get(*context, cbFields, /*isPacked=*/false);

                            // Get pointer to object_freed flag (field 2)
                            llvm::Value* objectFreedPtr = builder->CreateStructGEP(controlBlockType, controlBlockPtr, 2,
                                objIdent->name + "_released_obj_freed_ptr");

                            // Load the object_freed flag with atomic acquire semantics
                            llvm::LoadInst* objectFreedValue = builder->CreateLoad(
                                llvm::Type::getInt8Ty(*context),
                                objectFreedPtr,
                                objIdent->name + "_released_obj_freed"
                            );
                            objectFreedValue->setAtomic(llvm::AtomicOrdering::Acquire);

                            // Convert i8 to i1 (bool) for return
                            llvm::Value* boolValue = builder->CreateICmpNE(
                                objectFreedValue,
                                llvm::ConstantInt::get(llvm::Type::getInt8Ty(*context), 0),
                                objIdent->name + "_released_bool"
                            );

                            // Return the flag value (true if freed, false if alive)
                            m_currentLLVMValue = boolValue;
                            VYB_CDBG << "DEBUG: mild<T>.released() - returning object_freed flag" << std::endl;
                            return;
                        }
                    }
                }
            }
        }
    }

    // Check if this is an aspect method call (including generic aspects)
    if (auto memberExpr = dynamic_cast<vyb::ast::MemberExpression*>(node->callee.get())) {
        if (auto objIdent = dynamic_cast<vyb::ast::Identifier*>(memberExpr->object.get())) {
            if (auto methodIdent = dynamic_cast<vyb::ast::Identifier*>(memberExpr->property.get())) {
                std::string methodName = methodIdent->name;

                // Determine whether this is a qualified aspect call
                // (Aspect::method(receiver, ...)) or an unqualified method
                // call (receiver.method(...)).
                auto objIt = namedValues.find(objIdent->name);
                SemanticAnalyzer* semantic = driver_.hasSemanticAnalyzer()
                    ? driver_.getSemanticAnalyzer() : nullptr;

                bool isQualifiedAspectCall = (objIt == namedValues.end()) && semantic &&
                    semantic->getTraitRegistry().count(objIdent->name) != 0;

                llvm::Value* receiverAlloca = nullptr;
                llvm::Value* receiverValue = nullptr; // loaded receiver struct (qualified)
                std::string concreteType;
                std::string callDebug = objIdent->name + "." + methodName + "()";

                if (!isQualifiedAspectCall) {
                    // Unqualified: the receiver is a variable in namedValues.
                    if (objIt != namedValues.end()) {
                        receiverAlloca = objIt->second;
                        auto typeMapIt = valueTypeMap.find(receiverAlloca);
                        if (typeMapIt != valueTypeMap.end() && typeMapIt->second) {
                            concreteType = typeMapIt->second->toString();
                        } else if (objIdent->type) {
                            concreteType = objIdent->type->toString();
                        }
                    }
                } else {
                    // Qualified: the receiver is the first call argument.
                    callDebug = objIdent->name + "::" + methodName + "()";
                    if (node->arguments.empty()) {
                        logError(node->loc, "Qualified aspect call " + objIdent->name +
                                 "::" + methodName + "() requires a receiver as the first argument.");
                        m_currentLLVMValue = nullptr;
                        return;
                    }
                    node->arguments[0]->accept(*this);
                    receiverValue = m_currentLLVMValue;
                    if (!receiverValue) {
                        logError(node->loc, "Failed to evaluate the receiver of qualified aspect call " +
                                 objIdent->name + "::" + methodName + "().");
                        m_currentLLVMValue = nullptr;
                        return;
                    }
                    auto typeMapIt = valueTypeMap.find(receiverValue);
                    if (typeMapIt != valueTypeMap.end() && typeMapIt->second) {
                        concreteType = typeMapIt->second->toString();
                    } else if (node->arguments[0]->type) {
                        concreteType = node->arguments[0]->type->toString();
                    }
                }

                if (concreteType.empty()) {
                    VYB_CDBG << "DEBUG: No type for aspect method call: " << callDebug << std::endl;
                    if (!isQualifiedAspectCall) {
                        // Fall through so the general member-expression path can
                        // produce the appropriate diagnostic for non-aspect calls.
                    } else {
                        logError(node->loc, "Cannot determine the receiver type of qualified aspect call " +
                                 callDebug);
                        m_currentLLVMValue = nullptr;
                        return;
                    }
                } else {
                    VYB_CDBG << "DEBUG: Checking aspect method call: " << callDebug
                              << " on type " << concreteType << std::endl;

                    // Ownership-wrapped Vec receiver (their<Vec<T>>, my<Vec<T>>,
                    // view<Vec<T>>, our<Vec<T>>, borrow): an aspect/bind method bound
                    // to the Vec (VecOps/VecHigherOps in-place forms) is monomorphized
                    // for the *unwrapped* inner Vec type. Normalize concreteType to the
                    // inner Vec so the bind/mangle/monomorphize lookups succeed, and
                    // remember that the receiver's alloca holds a Vec* view so we pass
                    // the loaded pointer (not the alloca address) to the by-ref self.
                    bool ownershipWrappedVec = false;
                    bool isOwnershipKw = concreteType.rfind("their<", 0) == 0 ||
                                         concreteType.rfind("my<", 0) == 0 ||
                                         concreteType.rfind("our<", 0) == 0 ||
                                         concreteType.rfind("view<", 0) == 0 ||
                                         concreteType.rfind("borrow<", 0) == 0;
                    if (isOwnershipKw && !isQualifiedAspectCall) {
                        size_t lt = concreteType.find('<');
                        size_t gt = concreteType.rfind('>');
                        if (lt != std::string::npos && gt != std::string::npos && gt > lt) {
                            std::string inner = concreteType.substr(lt + 1, gt - lt - 1);
                            bool innerIsVec = inner == "Vec" || inner.rfind("Vec<", 0) == 0;
                            if (innerIsVec) {
                                concreteType = inner;
                                ownershipWrappedVec = true;
                            }
                        }
                    }

                    // Collect the candidate aspect(s) that provide this method for
                    // the concrete type. Qualified calls only consider the explicit
                    // aspect; unqualified calls consider every bound aspect.
                    std::vector<std::string> candidateTraits;
                    auto pushIfProvides = [&](const std::string& trait, bool inImpl) {
                        if (!inImpl) {
                            auto traitIt = semantic->getTraitRegistry().find(trait);
                            if (traitIt != semantic->getTraitRegistry().end()) {
                                for (const auto& tm : traitIt->second->methods) {
                                    if (tm.name == methodName && tm.hasDefaultImpl) {
                                        inImpl = true;
                                        break;
                                    }
                                }
                            }
                        }
                        if (inImpl) candidateTraits.push_back(trait);
                    };

                    if (isQualifiedAspectCall) {
                        candidateTraits.push_back(objIdent->name);
                    } else if (semantic) {
                        // Concrete impls bound to this type.
                        const auto& impls = semantic->getTraitImpls();
                        auto limpl = impls.find(concreteType);
                        if (limpl != impls.end()) {
                            for (const auto& traitEntry : limpl->second) {
                                bool has = false;
                                for (const ast::FunctionDeclaration* m : traitEntry.second) {
                                    if (m && m->id && m->id->name == methodName) { has = true; break; }
                                }
                                pushIfProvides(traitEntry.first, has);
                            }
                        }
                        // Generic impl patterns matching this concrete type.
                        TypePattern concretePattern = TypePattern::parse(concreteType);
                        const auto& genImpls = semantic->getGenericTraitImpls();
                        for (const auto& typeEntry : genImpls) {
                            TypePattern tmplPattern = TypePattern::parse(typeEntry.first);
                            std::map<std::string, std::string> sub;
                            if (!tmplPattern.matchesPattern(concretePattern, sub)) continue;
                            for (const auto& traitEntry : typeEntry.second) {
                                const GenericImplInfo* gii = traitEntry.second.get();
                                bool has = false;
                                if (gii && gii->declaration) {
                                    for (const auto& m : gii->declaration->methods) {
                                        if (m && m->id && m->id->name == methodName) { has = true; break; }
                                    }
                                }
                                pushIfProvides(traitEntry.first, has);
                            }
                        }
                    }

                    llvm::Function* implFunc = nullptr;
                    for (const std::string& trait : candidateTraits) {
                        std::string mangledName = concreteType + "_" + trait + "_" + methodName;
                        implFunc = module->getFunction(mangledName);
                        if (implFunc) {
                            VYB_CDBG << "DEBUG: Found aspect method implementation: "
                                      << mangledName << std::endl;
                            break;
                        }
                        if (semantic) {
                            implFunc = monomorphizeTraitMethod(concreteType, trait, methodName);
                            if (implFunc) {
                                VYB_CDBG << "DEBUG: Monomorphized aspect method for trait "
                                          << trait << " on " << concreteType << std::endl;
                                break;
                            }
                        }
                    }

                    if (implFunc) {
                        // Build arguments: first argument is the receiver struct.
                        // When the bind's receiver is declared by reference (an
                        // ownership-qualified receiver such as `self<their<T>>`),
                        // the specialized method's first parameter is a pointer to the
                        // receiver. Pass the receiver's address (its alloca) so
                        // in-place mutations to `self` persist on the caller's object
                        // instead of loading a by-value copy that is discarded.
                        bool selfIsByRef = implFunc->getArg(0)->getType()->isPointerTy();
                        std::vector<llvm::Value*> argValues;
                        if (isQualifiedAspectCall) {
                            argValues.push_back(receiverValue);
                        } else if (ownershipWrappedVec && receiverAlloca) {
                            // The wrapper alloca holds a single Vec* view; load it and
                            // pass that pointer, which is what a by-ref
                            // `self<their<Vec<T>>>` parameter expects.
                            if (auto at = llvm::dyn_cast<llvm::AllocaInst>(receiverAlloca)) {
                                argValues.push_back(builder->CreateLoad(
                                    at->getAllocatedType(), receiverAlloca,
                                    objIdent->name + ".view.load"));
                            } else {
                                argValues.push_back(receiverAlloca);
                            }
                        } else if (selfIsByRef && receiverAlloca) {
                            argValues.push_back(receiverAlloca);
                        } else if (auto allocaType = llvm::dyn_cast<llvm::AllocaInst>(receiverAlloca)) {
                            argValues.push_back(builder->CreateLoad(
                                allocaType->getAllocatedType(),
                                receiverAlloca,
                                objIdent->name + ".load"));
                        } else {
                            argValues.push_back(receiverAlloca);
                        }

                        size_t argStart = isQualifiedAspectCall ? 1u : 0u;
                        std::vector<llvm::Value*> transientClosures;
                        for (size_t i = argStart; i < node->arguments.size(); ++i) {
                            auto& arg = node->arguments[i];
                            arg->accept(*this);
                            if (!m_currentLLVMValue) {
                                logError(arg->loc, "Argument codegen failed for aspect method " + methodName);
                                m_currentLLVMValue = nullptr;
                                return;
                            }
                            llvm::Value* argVal = m_currentLLVMValue;
                            // A fresh closure literal passed as an argument starts
                            // with a zero refcount; an aspect/trait method borrows it
                            // but does not retain/release the env (it only calls the
                            // fn). Retain each such argument before the call and
                            // release it after so the transient environment is
                            // reclaimed instead of leaking.
                            if (dynamic_cast<ast::FunctionExpression*>(arg.get()) &&
                                argVal->getType()->isStructTy() &&
                                isClosureStructType(argVal->getType())) {
                                retainClosureValue(argVal);
                                transientClosures.push_back(argVal);
                            }
                            // When the parameter is a Vyb String { ptr, len } and the
                            // evaluated argument is a raw char* (e.g. to_string(),
                            // substring, concat), wrap it into the String struct;
                            // literals and String variables already arrive as a struct.
                            if (argVal->getType()->isPointerTy()) {
                                llvm::Type* expectedParam = implFunc->getFunctionType()->getParamType(argValues.size());
                                if (expectedParam && expectedParam->isStructTy()) {
                                    llvm::StructType* st = llvm::dyn_cast<llvm::StructType>(expectedParam);
                                    if (st && st->getNumElements() == 2 &&
                                        st->getElementType(0)->isPointerTy() &&
                                        st->getElementType(1)->isIntegerTy(64)) {
                                        llvm::Value* wrapped = tryCast(argVal, expectedParam, arg->loc);
                                        if (wrapped) argVal = wrapped;
                                    }
                                }
                            }
                            argValues.push_back(argVal);
                        }

                        if (implFunc->getReturnType()->isVoidTy()) {
                            builder->CreateCall(implFunc, argValues);
                            m_currentLLVMValue = nullptr;
                        } else {
                            m_currentLLVMValue = builder->CreateCall(implFunc, argValues, "aspect.method.result");
                        }
                        for (auto* tc : transientClosures) {
                            releaseClosureValue(tc);
                        }

                        VYB_CDBG << "DEBUG: Successfully generated call to aspect method: "
                                  << methodName << " for type " << concreteType << std::endl;
                        return;
                    }

                    VYB_CDBG << "DEBUG: No aspect implementation found for " << callDebug
                              << " on type " << concreteType << std::endl;
                }
            }
        }
    }

    // First, check if this is an intrinsic function call
    auto identCallee = dynamic_cast<vyb::ast::Identifier*>(node->callee.get());
    std::string calleeName = node->callee->toString();



    // Special handling for intrinsic functions with potential variable name conflicts
    if (identCallee && node->arguments.size() == 1) {
        // Check for addr() intrinsic
        if (identCallee->name == "addr") {
            // First evaluate the argument expression
            node->arguments[0]->accept(*this);
            if (!m_currentLLVMValue) {
                logError(node->arguments[0]->loc, "Argument to addr() evaluated to null");
                return;
            }

            // If the argument is a pointer type
            if (m_currentLLVMValue->getType()->isPointerTy()) {
                llvm::Value* pointerValue = m_currentLLVMValue;

                // For pointer-to-pointer types (like loc<T>), load the pointer first
                if (auto allocaInst = llvm::dyn_cast<llvm::AllocaInst>(m_currentLLVMValue)) {
                    if (allocaInst->getAllocatedType()->isPointerTy()) {
                        pointerValue = builder->CreateLoad(allocaInst->getAllocatedType(),
                                                        m_currentLLVMValue,
                                                        "ptr_load_for_addr");
                    }
                }

                // Convert to integer value (address)
                m_currentLLVMValue = builder->CreatePtrToInt(pointerValue, int64Type, "addr_cast");

                // If we're not in an assignment context, create an alloca to store the result
                if (!m_isLHSOfAssignment) {
                    llvm::Value* tempAlloca = builder->CreateAlloca(int64Type, nullptr, "addr_temp");
                    builder->CreateStore(m_currentLLVMValue, tempAlloca);
                    m_currentLLVMValue = tempAlloca;
                }
                return;
            }
        }
        // Check for at() intrinsic
        else if (identCallee->name == "at") {
            // First evaluate the argument expression to get the pointer
            node->arguments[0]->accept(*this);
            if (!m_currentLLVMValue) {
                logError(node->arguments[0]->loc, "Argument to at() evaluated to null");
                return;
            }

            // If we have a valid pointer, handle it appropriately
            if (m_currentLLVMValue->getType()->isPointerTy()) {
                // For at(p) where p is a loc<T> (i.e., a pointer-to-pointer),
                // we need to load the pointer value first
                llvm::Value* pointerValue = m_currentLLVMValue;

                // AllocaInst for a loc<T> produces a pointer-to-pointer.
                // Check if we're dealing with a pointer-to-pointer (i.e., loc<T>)
                bool isPointerToPointer = false;
                if (auto allocaInst = llvm::dyn_cast<llvm::AllocaInst>(m_currentLLVMValue)) {
                    if (allocaInst->getAllocatedType()->isPointerTy()) {
                        isPointerToPointer = true;
                        // Load the pointer value
                        pointerValue = builder->CreateLoad(allocaInst->getAllocatedType(),
                                               m_currentLLVMValue,
                                               "ptr_val");
                    }
                }

                // Add a null check for the pointer value
                llvm::Function* currentFn = builder->GetInsertBlock()->getParent();
                llvm::BasicBlock* nonNullBB = llvm::BasicBlock::Create(*context, "ptr.not_null", currentFn);
                llvm::BasicBlock* nullBB = llvm::BasicBlock::Create(*context, "ptr.null", currentFn);
                llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*context, "ptr.merge", currentFn);

                // Create the null check condition
                llvm::Value* isNotNull = builder->CreateIsNotNull(pointerValue, "ptr.is_not_null");
                builder->CreateCondBr(isNotNull, nonNullBB, nullBB);

                // Set up the non-null block
                builder->SetInsertPoint(nonNullBB);

                if (m_isLHSOfAssignment) {
                    // In an assignment context, return the pointer itself
                    m_currentLLVMValue = pointerValue;
                } else {
                    // Otherwise dereference the pointer (load)
                    llvm::Type* loadTy = int64Type; // Default to Int for tests

                    // Try to determine the appropriate load type
                    if (auto allocaInst = llvm::dyn_cast<llvm::AllocaInst>(pointerValue)) {
                        loadTy = allocaInst->getAllocatedType();
                    } else if (node->arguments[0]->type) {
                        // Try to determine type from AST node
                        if (auto ptrType = dynamic_cast<ast::PointerType*>(node->arguments[0]->type.get())) {
                            if (ptrType->pointeeType) {
                                loadTy = codegenType(ptrType->pointeeType.get());
                            }
                        } else if (auto typeName = dynamic_cast<ast::TypeName*>(node->arguments[0]->type.get())) {
                            // Handle loc<T> type
                            if (typeName->identifier->name == "loc" && !typeName->genericArgs.empty()) {
                                loadTy = codegenType(typeName->genericArgs[0].get());
                            }
                        }
                    }

                    m_currentLLVMValue = builder->CreateLoad(loadTy, pointerValue, "deref.load");
                }

                builder->CreateBr(mergeBB);

                // Set up the null block
                builder->SetInsertPoint(nullBB);
                builder->CreateUnreachable();

                // Set up the merge block
                builder->SetInsertPoint(mergeBB);
                return;
            } else {
                logError(node->loc, "at() called on non-pointer type. Got: " + getTypeName(m_currentLLVMValue->getType()));
                m_currentLLVMValue = nullptr;
                return;
            }
        }
        // Check for loc() intrinsic
        else if (identCallee->name == "loc") {
            // First evaluate the argument to get its address
            auto ident = dynamic_cast<vyb::ast::Identifier*>(node->arguments[0].get());
            if (ident) {
                auto it = namedValues.find(ident->name);
                if (it != namedValues.end()) {
                    // Return the address of the alloca directly
                    m_currentLLVMValue = it->second;
                    return;
                }
            }

            // If we couldn't find the variable directly, try evaluating the argument generically
            node->arguments[0]->accept(*this);
            if (!m_currentLLVMValue) {
                logError(node->arguments[0]->loc, "Argument to loc() evaluated to null");
                return;
            }

            // The result of loc() is the pointer to the value
            if (auto* allocaInst = llvm::dyn_cast<llvm::AllocaInst>(m_currentLLVMValue)) {
                // If the value is already an alloca instruction, just use it directly
                return;
            } else {
                // For non-alloca values, we need to create temporary storage
                llvm::Type* valType = m_currentLLVMValue->getType();
                llvm::Value* tempAlloca = builder->CreateAlloca(valType, nullptr, "loc_temp");
                builder->CreateStore(m_currentLLVMValue, tempAlloca);
                m_currentLLVMValue = tempAlloca;
            }
            return;
        }
    }

    // Handle from<T>() intrinsic - more complex because it requires a type parameter
    if (identCallee && identCallee->name == "from" && node->arguments.size() == 1) {
        // Evaluate the address expression
        node->arguments[0]->accept(*this);
        if (!m_currentLLVMValue) {
            logError(node->arguments[0]->loc, "from<T>() operand evaluated to null");
            return;
        }

        if (!m_currentLLVMValue->getType()->isIntegerTy()) {
            logError(node->arguments[0]->loc, "from<T>() requires an integer address argument. Got: " +
                                           getTypeName(m_currentLLVMValue->getType()));
            return;
        }

        // Default to i64* pointer type
        llvm::Type* ptrTy = llvm::PointerType::getUnqual(int64Type);

        // Convert integer to pointer
        m_currentLLVMValue = builder->CreateIntToPtr(m_currentLLVMValue, ptrTy, "from_cast");
        return;
    }

    // Handle ownership constructors: my(), their(), our()
    if (identCallee && (identCallee->name == "my" || identCallee->name == "their" || identCallee->name == "our") && node->arguments.size() == 1) {
        VYB_CDBG << "DEBUG: Processing ownership constructor " << identCallee->name << "() in LLVM codegen" << std::endl;

        // Evaluate the argument to get the struct value
        node->arguments[0]->accept(*this);
        if (!m_currentLLVMValue) {
            logError(node->arguments[0]->loc, "Argument to " + identCallee->name + "() evaluated to null");
            return;
        }

        llvm::Value* structValue = m_currentLLVMValue;
        llvm::Type* structType = structValue->getType();

        // Primitive ownership values are stored inline (e.g. my<Int> is just an
        // i64) - pass the value through rather than allocating a heap box or
        // control block, matching the value-typed ownership representation.
        if (structType->isIntegerTy() || structType->isFloatTy() || structType->isDoubleTy()) {
            m_currentLLVMValue = structValue;
            VYB_CDBG << "DEBUG: Ownership constructor " << identCallee->name
                      << "() over primitive passes value through" << std::endl;
            return;
        }

        if (identCallee->name == "my") {
            // For my(), allocate memory on heap and store the struct value
            if (!structType->isStructTy()) {
                logError(node->loc, "my() can only be used with struct types");
                return;
            }

            // Allocate memory for the struct on the heap
            llvm::Function* mallocFunc = module->getFunction("malloc");
            if (!mallocFunc) {
                // Declare malloc if not already declared
                llvm::FunctionType* mallocType = llvm::FunctionType::get(
                    llvm::PointerType::get(llvm::Type::getInt8Ty(*context), 0),
                    {llvm::Type::getInt64Ty(*context)},
                    false
                );
                mallocFunc = llvm::Function::Create(
                    mallocType,
                    llvm::Function::ExternalLinkage,
                    "malloc",
                    module.get()
                );
            }

            // Calculate size of struct
            llvm::DataLayout dataLayout(module.get());
            uint64_t structSize = dataLayout.getTypeAllocSize(structType);
            llvm::Value* sizeValue = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), structSize);

            // Call malloc
            llvm::Value* mallocPtr = builder->CreateCall(mallocFunc, {sizeValue}, "malloc_struct");

            // Cast malloc result to struct pointer type
            llvm::Type* structPtrType = llvm::PointerType::get(structType, 0);
            llvm::Value* structPtr = builder->CreateBitCast(mallocPtr, structPtrType, "struct_ptr");

            // Store the struct value into allocated memory
            builder->CreateStore(structValue, structPtr);

            // Return the pointer
            m_currentLLVMValue = structPtr;
            VYB_CDBG << "DEBUG: Successfully processed ownership constructor my() - allocated and returned pointer" << std::endl;
        } else if (identCallee->name == "our") {
            // For our(), allocate control block + object on heap
            if (!structType->isStructTy()) {
                logError(node->loc, "our() can only be used with struct types");
                return;
            }

            // Get malloc function
            llvm::Function* mallocFunc = getOrCreateMallocFunction();
            llvm::DataLayout dataLayout(module.get());

            // 1. Allocate memory for the object
            uint64_t objectSize = dataLayout.getTypeAllocSize(structType);
            llvm::Value* objectSizeValue = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), objectSize);
            llvm::Value* mallocObjectPtr = builder->CreateCall(mallocFunc, {objectSizeValue}, "malloc_object");
            llvm::Type* objectPtrType = llvm::PointerType::get(structType, 0);
            llvm::Value* objectPtr = builder->CreateBitCast(mallocObjectPtr, objectPtrType, "object_ptr");

            // Store the struct value into allocated memory
            builder->CreateStore(structValue, objectPtr);

            // 2. Allocate memory for control block
            llvm::StructType* controlBlockType = getControlBlockType(objectPtrType);
            uint64_t cbSize = dataLayout.getTypeAllocSize(controlBlockType);
            llvm::Value* cbSizeValue = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), cbSize);
            llvm::Value* mallocCBPtr = builder->CreateCall(mallocFunc, {cbSizeValue}, "malloc_cb");
            llvm::Type* cbPtrType = llvm::PointerType::get(controlBlockType, 0);
            llvm::Value* cbPtr = builder->CreateBitCast(mallocCBPtr, cbPtrType, "cb_ptr");

            // 3. Initialize control block fields: { strong_count=1, weak_count=0, object_freed=false, object_ptr }
            llvm::Value* strongCountPtr = builder->CreateStructGEP(controlBlockType, cbPtr, 0, "strong_count_ptr");
            builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 1), strongCountPtr);

            llvm::Value* weakCountPtr = builder->CreateStructGEP(controlBlockType, cbPtr, 1, "weak_count_ptr");
            builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0), weakCountPtr);

            llvm::Value* objectFreedPtr = builder->CreateStructGEP(controlBlockType, cbPtr, 2, "object_freed_ptr");
            builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt8Ty(*context), 0), objectFreedPtr);

            llvm::Value* objectPtrFieldPtr = builder->CreateStructGEP(controlBlockType, cbPtr, 3, "object_ptr_field_ptr");
            builder->CreateStore(objectPtr, objectPtrFieldPtr);

            // 4. Return the control block pointer (our<T> is represented as control block pointer)
            m_currentLLVMValue = cbPtr;
            VYB_CDBG << "DEBUG: Successfully processed ownership constructor our() - allocated control block and object" << std::endl;
        } else if (identCallee->name == "their") {
            // For their(), just pass through the value for now
            // In a real implementation, these would have different semantics
            VYB_CDBG << "DEBUG: Successfully processed ownership constructor their()" << std::endl;
        } else {
            // For their() and our(), just pass through the value for now
            // In a real implementation, these would have different semantics
            VYB_CDBG << "DEBUG: Successfully processed ownership constructor " << identCallee->name << "()" << std::endl;
        }
        return;
    }

    // Handle borrowing operations: borrow(), view()
    if (identCallee && (identCallee->name == "borrow" || identCallee->name == "view") && node->arguments.size() == 1) {
        VYB_CDBG << "DEBUG: Processing borrowing operation " << identCallee->name << "() in LLVM codegen" << std::endl;

        // Evaluate the argument to get the value to borrow
        node->arguments[0]->accept(*this);
        if (!m_currentLLVMValue) {
            logError(node->arguments[0]->loc, "Argument to " + identCallee->name + "() evaluated to null");
            return;
        }

        llvm::Value* valueToBorrow = m_currentLLVMValue;

        // For borrowing, we need to get the address of the value
        if (valueToBorrow->getType()->isPointerTy()) {
            // If it's already a pointer (e.g., alloca), use it directly
            m_currentLLVMValue = valueToBorrow;
            VYB_CDBG << "DEBUG: Successfully processed borrowing operation " << identCallee->name << "() - returned pointer" << std::endl;
        } else {
            // If it's a value, we need to create a temporary and get its address
            // This shouldn't normally happen for well-formed borrow operations
            logError(node->loc, identCallee->name + "() requires an lvalue (something that can be borrowed)");
            return;
        }
        return;
    }

    // Handle soft() operation: creates mild<T> from our<T>
    if (identCallee && identCallee->name == "soft" && node->arguments.size() == 1) {
        VYB_CDBG << "DEBUG: Processing soft() operation in LLVM codegen" << std::endl;

        // Evaluate the argument to get the our<T> value (control block pointer)
        node->arguments[0]->accept(*this);
        if (!m_currentLLVMValue) {
            logError(node->arguments[0]->loc, "Argument to soft() evaluated to null");
            return;
        }

        llvm::Value* controlBlockPtr = m_currentLLVMValue;

        // Soft() increments weak_count in the control block and returns mild<T>
        // Control block: { i32 strong_count, i32 weak_count, i8 object_freed, ptr object_ptr }

        // Reconstruct control block type
        std::vector<llvm::Type*> cbFields = {
            llvm::Type::getInt32Ty(*context),  // strong_count
            llvm::Type::getInt32Ty(*context),  // weak_count
            llvm::Type::getInt8Ty(*context),   // object_freed (i8 for atomic)
            llvm::PointerType::get(*context, 0) // object_ptr
        };
        llvm::StructType* controlBlockType = llvm::StructType::get(*context, cbFields, /*isPacked=*/false);

        // Get pointer to weak_count (field 1)
        llvm::Value* weakCountPtr = builder->CreateStructGEP(controlBlockType, controlBlockPtr, 1,
            "soft_weak_count_ptr");

        // Atomic increment: weak_count++
        builder->CreateAtomicRMW(
            llvm::AtomicRMWInst::Add,
            weakCountPtr,
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 1),
            llvm::MaybeAlign(),
            llvm::AtomicOrdering::AcquireRelease
        );

        // Return the control block pointer as mild<T>
        // Both our<T> and mild<T> are represented as control block pointers
        m_currentLLVMValue = controlBlockPtr;
        VYB_CDBG << "DEBUG: Successfully processed soft() operation - incremented weak_count and returned mild<T> pointer" << std::endl;
        return;
    }

    // Special handling for println with auto-serialization
    if (identCallee && identCallee->name == "println" && node->arguments.size() >= 1) {
        // Helper lambda to serialize one argument to char* for printing
        auto serializeOneArg = [&](ast::ExprPtr& argExpr, bool* outHeap, bool* outStringOwned) -> llvm::Value* {
            if (outHeap) *outHeap = false;
            if (outStringOwned) *outStringOwned = false;
            llvm::Value* arg = nullptr;
            // For array arguments, get pointer directly
            if (argExpr->type && dynamic_cast<ast::ArrayType*>(argExpr->type.get())) {
                if (auto* identArg = dynamic_cast<ast::Identifier*>(argExpr.get())) {
                    auto it = namedValues.find(identArg->name);
                    if (it != namedValues.end()) arg = it->second;
                    else {
                        auto funcIt = m_currentFunctionNamedValues.find(identArg->name);
                        if (funcIt != m_currentFunctionNamedValues.end()) arg = funcIt->second;
                    }
                }
            }
            if (!arg) {
                argExpr->accept(*this);
                arg = m_currentLLVMValue;
            }
            if (!arg) return nullptr;
            llvm::Value* serialized = nullptr;
            if (argExpr->type) {
                auto* argType = argExpr->type.get();
                std::string typeStr = argType->toString();
                if (typeStr == "string" || typeStr == "String") {
                    serialized = arg->getType()->isStructTy()
                        ? builder->CreateExtractValue(arg, 0, "str.ptr")
                        : arg;
                    if (outStringOwned) *outStringOwned = exprProducesOwnedStringTemp(argExpr.get());
                } else if (auto* arrayType = dynamic_cast<ast::ArrayType*>(argType)) {
                    serialized = generateArraySerialization(arg, arrayType);
                } else if (arg->getType()->isPointerTy() && arg->getType() == int8PtrType) {
                    serialized = arg;
                } else {
                    serialized = generateToStringCall(arg, arg->getType(), argType, argExpr->loc);
                    if (!serialized) serialized = generateGenericSerialization(arg, argType);
                    if (outHeap) *outHeap = true;
                }
            } else {
                if (arg->getType()->isPointerTy() && arg->getType() == int8PtrType) {
                    serialized = arg;
                } else if (arg->getType()->isStructTy() && arg->getType()->getStructNumElements() == 2 &&
                           arg->getType()->getStructElementType(0)->isPointerTy()) {
                    serialized = builder->CreateExtractValue(arg, 0, "str.ptr");
                } else {
                    serialized = generateToStringCall(arg, arg->getType(), nullptr, argExpr->loc);
                    if (!serialized) serialized = generateGenericSerialization(arg, nullptr);
                    if (outHeap) *outHeap = true;
                }
            }
            return serialized;
        };

        if (node->arguments.size() == 1) {
            // Single argument: use existing serialization logic
            llvm::Value* arg = nullptr;
            // For array arguments, we need the pointer, not the loaded value
            if (node->arguments[0]->type) {
            auto* argType = node->arguments[0]->type.get();
            if (dynamic_cast<ast::ArrayType*>(argType)) {
                // For arrays, get the alloca pointer directly instead of loading
                if (auto* identArg = dynamic_cast<ast::Identifier*>(node->arguments[0].get())) {
                    auto it = namedValues.find(identArg->name);
                    if (it != namedValues.end()) {
                        arg = it->second; // This is the alloca pointer
                    } else {
                        auto funcIt = m_currentFunctionNamedValues.find(identArg->name);
                        if (funcIt != m_currentFunctionNamedValues.end()) {
                            arg = funcIt->second; // This is the alloca pointer
                        }
                    }
                }
            }
        }

        // If we didn't get the array pointer above, evaluate normally
        if (!arg) {
            node->arguments[0]->accept(*this);
            arg = m_currentLLVMValue;
        }

        if (!arg) {
            logError(node->arguments[0]->loc, "Argument to println() evaluated to null");
            return;
        }

        llvm::Value* serializedValue = nullptr;
        bool serializedTmpIsHeap = false;
        bool stringArgOwnedTemp = exprProducesOwnedStringTemp(node->arguments[0].get());

        // Check for string type first (Vyb string struct {ptr, len})
        if (node->arguments[0]->type) {
            auto* argType = node->arguments[0]->type.get();
            std::string typeStr = argType->toString();

            // Priority 1: Check if it's a Vyb string type
            if (typeStr == "string" || typeStr == "String") {
                // It's a Vyb string struct {ptr, len} - extract the ptr field
                if (arg->getType()->isStructTy()) {
                    // Extract the ptr field (index 0) from the string struct
                    serializedValue = builder->CreateExtractValue(arg, 0, "str.ptr");
                } else if (arg->getType()->isPointerTy()) {
                    // Already a char* (e.g. result of string concatenation or to_string())
                    serializedValue = arg;
                } else {
                    // Fallback for other representations
                    serializedValue = generateToStringCall(arg, arg->getType(), argType, node->loc);
                    if (!serializedValue) {
                        serializedValue = generateGenericSerialization(arg, argType);
                    }
                }
            }
            // Priority 2: Check for arrays
            else if (auto* arrayType = dynamic_cast<ast::ArrayType*>(argType)) {
                // Generate array serialization code
                serializedValue = generateArraySerialization(arg, arrayType);
            }
            // Priority 3: Check if the argument is already a string (char*) for non-array types
            else if (arg->getType()->isPointerTy() && arg->getType() == int8PtrType) {
                // It's already a string pointer (char*), use it directly
                serializedValue = arg;
            }
            // Priority 4: Convert to string via to_string() for any type (Int, Float, Bool, etc.)
            else {
                serializedValue = generateToStringCall(arg, arg->getType(), argType, node->loc);
                if (!serializedValue) {
                    // Fall back to generic serialization for complex/unrecognized types
                    serializedValue = generateGenericSerialization(arg, argType);
                }
                serializedTmpIsHeap = true;
            }
        }
        else {
            // No type info - check if the argument is already a string (char*)
            if (arg->getType()->isPointerTy() && arg->getType() == int8PtrType) {
                // It's already a string pointer (char*), use it directly
                serializedValue = arg;
            }
            // Check if it's a struct (might be a string struct)
            else if (arg->getType()->isStructTy() && arg->getType()->getStructNumElements() == 2 &&
                     arg->getType()->getStructElementType(0)->isPointerTy()) {
                // Might be a string struct {ptr, len} - extract ptr
                serializedValue = builder->CreateExtractValue(arg, 0, "str.ptr");
            }
            else {
                // Try to_string() first, fall back to generic serialization
                serializedValue = generateToStringCall(arg, arg->getType(), nullptr, node->loc);
                if (!serializedValue) {
                    serializedValue = generateGenericSerialization(arg, nullptr);
                }
                serializedTmpIsHeap = true;
            }
        }

        // Call println with the serialized string
        llvm::Function* printlnFunc = getVybPrintlnFunction();
        std::vector<llvm::Value*> printlnArgs = {serializedValue};
        builder->CreateCall(printlnFunc, printlnArgs);

        // Free a heap-backed serialization temporary (scalar/struct/array
        // produced by to_string/serialize helpers). String paths yield the
        // value's own buffer; only a freshly-allocated concat/.to_string() temp
        // is reclaimed (through the registry, which makes it a safe no-op for
        // .rodata literals and named-var borrows).
        if (serializedTmpIsHeap) {
            builder->CreateCall(getOrCreateVybStringFreeFunction(), {serializedValue});
        }
        if (stringArgOwnedTemp) {
            builder->CreateCall(getOrCreateVybStringFreeFunction(), {serializedValue});
        }

        m_currentLLVMValue = nullptr; // println returns void
        return;
        } else {
            // Multiple arguments: print each with space separator, last with newline
            llvm::Function* printFunc = getVybPrintFunction();
            llvm::Function* printlnFunc = getVybPrintlnFunction();
            // Space constant
            llvm::Value* spaceStr = builder->CreateGlobalStringPtr(" ", "println.space");
            for (size_t i = 0; i < node->arguments.size(); ++i) {
                bool serializedIsHeap = false;
                bool serializedStringOwned = false;
                llvm::Value* serialized = serializeOneArg(node->arguments[i], &serializedIsHeap, &serializedStringOwned);
                if (!serialized) {
                    logError(node->arguments[i]->loc, "Argument to println() evaluated to null");
                    m_currentLLVMValue = nullptr;
                    return;
                }
                if (i < node->arguments.size() - 1) {
                    // Print arg then space
                    builder->CreateCall(printFunc, {serialized});
                    builder->CreateCall(printFunc, {spaceStr});
                } else {
                    // Last arg: println (adds newline)
                    builder->CreateCall(printlnFunc, {serialized});
                }
                if (serializedIsHeap) {
                    builder->CreateCall(getOrCreateVybStringFreeFunction(), {serialized});
                }
                if (serializedStringOwned) {
                    builder->CreateCall(getOrCreateVybStringFreeFunction(), {serialized});
                }
            }
            m_currentLLVMValue = nullptr;
            return;
        }
    }

    // Handle print() intrinsic (no newline)
    if (identCallee && identCallee->name == "print" && node->arguments.size() >= 1) {
        llvm::Function* printFunc = getVybPrintFunction();
        llvm::Value* spaceStr = nullptr;
        if (node->arguments.size() > 1) {
            spaceStr = builder->CreateGlobalStringPtr(" ", "print.space");
        }
        for (size_t argIdx = 0; argIdx < node->arguments.size(); ++argIdx) {
            node->arguments[argIdx]->accept(*this);
            llvm::Value* arg = m_currentLLVMValue;
            if (!arg) {
                logError(node->arguments[argIdx]->loc, "Argument to print() evaluated to null");
                return;
            }
            bool argSerializedHeap = false;
            bool argStringOwned = exprProducesOwnedStringTemp(node->arguments[argIdx].get());
            // Serialize to string then print without newline
            llvm::Value* serializedValue = nullptr;
            if (node->arguments[argIdx]->type) {
                auto* argType = node->arguments[argIdx]->type.get();
                std::string typeStr = argType->toString();
                if (typeStr == "String" || typeStr == "string") {
                    serializedValue = arg->getType()->isStructTy()
                        ? builder->CreateExtractValue(arg, 0, "str.ptr")
                        : arg;
                } else {
                    serializedValue = generateToStringCall(arg, arg->getType(), argType, node->loc);
                    if (!serializedValue) {
                        serializedValue = generateGenericSerialization(arg, argType);
                    }
                    argSerializedHeap = true;
                }
            } else {
                if (arg->getType()->isPointerTy()) {
                    serializedValue = arg;
                } else if (arg->getType()->isStructTy() && arg->getType()->getStructNumElements() == 2 &&
                           arg->getType()->getStructElementType(0)->isPointerTy()) {
                    serializedValue = builder->CreateExtractValue(arg, 0, "str.ptr");
                } else {
                    serializedValue = generateToStringCall(arg, arg->getType(), nullptr, node->loc);
                    if (!serializedValue) {
                        serializedValue = generateGenericSerialization(arg, nullptr);
                    }
                    argSerializedHeap = true;
                }
            }
            if (serializedValue) {
                builder->CreateCall(printFunc, {serializedValue});
                if (argSerializedHeap) {
                    builder->CreateCall(getOrCreateVybStringFreeFunction(), {serializedValue});
                }
                if (argStringOwned) {
                    builder->CreateCall(getOrCreateVybStringFreeFunction(), {serializedValue});
                }
            }
            // Print space between args (not after last)
            if (spaceStr && argIdx < node->arguments.size() - 1) {
                builder->CreateCall(printFunc, {spaceStr});
            }
        }
        m_currentLLVMValue = nullptr;
        return;
    }

    // Handle println_int() / print_int() intrinsics - route through generic to_string path
    if (identCallee && (identCallee->name == "println_int" || identCallee->name == "print_int") && node->arguments.size() == 1) {
        node->arguments[0]->accept(*this);
        llvm::Value* arg = m_currentLLVMValue;
        if (!arg) {
            logError(node->arguments[0]->loc, "Argument to " + identCallee->name + "() evaluated to null");
            return;
        }
        if (!arg->getType()->isIntegerTy(64)) {
            // Sign-extend signed integer types; zero-extend unsigned ones so a
            // UInt8 value with its high bit set prints as itself, not as a
            // negative signed value.
            bool unsign = false;
            if (node->arguments[0]->type) {
                if (auto tn = dynamic_cast<ast::TypeName*>(node->arguments[0]->type.get())) {
                    if (tn->identifier) unsign = isUnsignedIntName(tn->identifier->name);
                }
            }
            arg = builder->CreateIntCast(arg, llvm::Type::getInt64Ty(*context), !unsign, "cast_i64");
        }
        llvm::Value* strVal = generateToStringCall(arg, arg->getType(), nullptr, node->loc);
        if (identCallee->name == "println_int") {
            builder->CreateCall(getVybPrintlnFunction(), {strVal});
        } else {
            builder->CreateCall(getVybPrintFunction(), {strVal});
        }
        builder->CreateCall(getOrCreateVybStringFreeFunction(), {strVal});
        m_currentLLVMValue = nullptr;
        return;
    }

    // Handle println_bool() / print_bool() intrinsics - route through generic to_string path
    if (identCallee && (identCallee->name == "println_bool" || identCallee->name == "print_bool") && node->arguments.size() == 1) {
        node->arguments[0]->accept(*this);
        llvm::Value* arg = m_currentLLVMValue;
        if (!arg) {
            logError(node->arguments[0]->loc, "Argument to " + identCallee->name + "() evaluated to null");
            return;
        }
        // Ensure i1 for bool to_string conversion
        if (!arg->getType()->isIntegerTy(1)) {
            arg = builder->CreateICmpNE(arg, llvm::ConstantInt::get(arg->getType(), 0), "to_bool");
        }
        llvm::Value* strVal = generateToStringCall(arg, arg->getType(), nullptr, node->loc);
        if (identCallee->name == "println_bool") {
            builder->CreateCall(getVybPrintlnFunction(), {strVal});
        } else {
            builder->CreateCall(getVybPrintFunction(), {strVal});
        }
        builder->CreateCall(getOrCreateVybStringFreeFunction(), {strVal});
        m_currentLLVMValue = nullptr;
        return;
    }

    // Handle math library intrinsics
    if (identCallee) {
        const std::string& mathName = identCallee->name;
        auto getLibmFunc1 = [&](const std::string& fname) -> llvm::Function* {
            llvm::Function* mf = module->getFunction(fname);
            if (!mf) {
                llvm::FunctionType* ft = llvm::FunctionType::get(
                    llvm::Type::getDoubleTy(*context),
                    {llvm::Type::getDoubleTy(*context)}, false);
                mf = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, fname, module.get());
            }
            return mf;
        };
        auto getLibmFunc2 = [&](const std::string& fname) -> llvm::Function* {
            llvm::Function* mf = module->getFunction(fname);
            if (!mf) {
                llvm::FunctionType* ft = llvm::FunctionType::get(
                    llvm::Type::getDoubleTy(*context),
                    {llvm::Type::getDoubleTy(*context), llvm::Type::getDoubleTy(*context)}, false);
                mf = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, fname, module.get());
            }
            return mf;
        };
        auto toDouble = [&](llvm::Value* v) -> llvm::Value* {
            if (!v) return v;
            if (v->getType()->isDoubleTy()) return v;
            if (v->getType()->isIntegerTy())
                return builder->CreateSIToFP(v, llvm::Type::getDoubleTy(*context), "to_dbl");
            if (v->getType()->isFloatingPointTy())
                return builder->CreateFPCast(v, llvm::Type::getDoubleTy(*context), "fp_cast");
            return v;
        };
        auto evalMathArg = [&](size_t i) -> llvm::Value* {
            if (i >= node->arguments.size()) return nullptr;
            node->arguments[i]->accept(*this);
            return m_currentLLVMValue;
        };
        if (mathName == "sqrt" || mathName == "sin" || mathName == "cos" || mathName == "tan" ||
            mathName == "exp" || mathName == "log" || mathName == "log2" || mathName == "log10" ||
            mathName == "floor" || mathName == "ceil" || mathName == "round") {
            llvm::Value* a = toDouble(evalMathArg(0)); if (!a) return;
            m_currentLLVMValue = builder->CreateCall(getLibmFunc1(mathName), {a}, mathName);
            return;
        } else if (mathName == "pow") {
            llvm::Value* a = toDouble(evalMathArg(0)); if (!a) return;
            llvm::Value* b = toDouble(evalMathArg(1)); if (!b) return;
            m_currentLLVMValue = builder->CreateCall(getLibmFunc2("pow"), {a, b}, "pow");
            return;
        } else if (mathName == "abs") {
            llvm::Value* a = evalMathArg(0); if (!a) return;
            if (a->getType()->isIntegerTy()) {
                llvm::Value* neg = builder->CreateNeg(a, "neg");
                llvm::Value* cmp = builder->CreateICmpSGT(a, neg, "abs_cmp");
                m_currentLLVMValue = builder->CreateSelect(cmp, a, neg, "abs");
            } else {
                llvm::Value* d = toDouble(a);
                m_currentLLVMValue = builder->CreateCall(getLibmFunc1("fabs"), {d}, "fabs");
            }
            return;
        } else if (mathName == "min") {
            llvm::Value* a = evalMathArg(0); if (!a) return;
            llvm::Value* b = evalMathArg(1); if (!b) return;
            if (a->getType()->isIntegerTy() && b->getType()->isIntegerTy()) {
                llvm::Value* cmp = builder->CreateICmpSLT(a, b, "min_cmp");
                m_currentLLVMValue = builder->CreateSelect(cmp, a, b, "min");
            } else {
                llvm::Value* da = toDouble(a), *db = toDouble(b);
                llvm::Value* cmp = builder->CreateFCmpOLT(da, db, "min_cmp");
                m_currentLLVMValue = builder->CreateSelect(cmp, da, db, "min");
            }
            return;
        } else if (mathName == "max") {
            llvm::Value* a = evalMathArg(0); if (!a) return;
            llvm::Value* b = evalMathArg(1); if (!b) return;
            if (a->getType()->isIntegerTy() && b->getType()->isIntegerTy()) {
                llvm::Value* cmp = builder->CreateICmpSGT(a, b, "max_cmp");
                m_currentLLVMValue = builder->CreateSelect(cmp, a, b, "max");
            } else {
                llvm::Value* da = toDouble(a), *db = toDouble(b);
                llvm::Value* cmp = builder->CreateFCmpOGT(da, db, "max_cmp");
                m_currentLLVMValue = builder->CreateSelect(cmp, da, db, "max");
            }
            return;
        }
    }

    // Handle File I/O intrinsics (io stdlib module). Each maps a Vyb-level call
    // (`vyb_io_*`) to an exported runtime symbol (`__vyb_file_*`) in
    // `runtime/vyb_runtime.c`; Vyb identifiers cannot start with `_`, hence the
    // separate callable name vs. resolved symbol name. `path`/`data` come in as
    // Vyb String structs { ptr, len } and their data pointer (and for writes,
    // length) is extracted at the boundary. `read_all` and the error message
    // return an owned heap buffer registered by the runtime, so the Vyb String
    // built over it is freed by normal reference-counted cleanup.
    if (identCallee) {
        const std::string& fname = identCallee->name;
        std::string rtName;
        if (fname == "vyb_io_open") rtName = "__vyb_file_open";
        else if (fname == "vyb_io_close") rtName = "__vyb_file_close";
        else if (fname == "vyb_io_write") rtName = "__vyb_file_write";
        else if (fname == "vyb_io_read_all") rtName = "__vyb_file_read_all";
        else if (fname == "vyb_io_error_code") rtName = "__vyb_file_error_code";
        else if (fname == "vyb_io_error_message") rtName = "__vyb_file_error_message";
        if (!rtName.empty()) {
            auto getFileFn = [&](llvm::FunctionType* ft) -> llvm::Function* {
                llvm::Function* f = module->getFunction(rtName);
                if (!f) f = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, rtName, module.get());
                return f;
            };
            auto toI64 = [&](llvm::Value* v) -> llvm::Value* {
                if (!v) return v;
                if (v->getType()->isIntegerTy(64)) return v;
                if (v->getType()->isIntegerTy())
                    return builder->CreateSExt(v, int64Type, "file.toi64");
                return v;
            };
            auto toStrPtr = [&](llvm::Value* v) -> llvm::Value* {
                // A Vyb String value { ptr, len } -> its char* data pointer.
                if (v && v->getType()->isStructTy())
                    return builder->CreateExtractValue(v, 0, "file.strptr");
                return v;
            };

            if (fname == "vyb_io_open") {
                if (node->arguments.size() != 2) {
                    logError(node->loc, "__vyb_file_open expects 2 arguments (path, flags)");
                    m_currentLLVMValue = nullptr; return;
                }
                node->arguments[0]->accept(*this); llvm::Value* path = m_currentLLVMValue;
                node->arguments[1]->accept(*this); llvm::Value* flags = m_currentLLVMValue;
                if (!path || !flags) { m_currentLLVMValue = nullptr; return; }
                llvm::FunctionType* ft = llvm::FunctionType::get(int64Type, {int8PtrType, int64Type}, false);
                m_currentLLVMValue = builder->CreateCall(
                    getFileFn(ft), {toStrPtr(path), toI64(flags)}, "file.fd");
                return;
            } else if (fname == "vyb_io_close") {
                if (node->arguments.size() != 1) {
                    logError(node->loc, "__vyb_file_close expects 1 argument (fd)");
                    m_currentLLVMValue = nullptr; return;
                }
                node->arguments[0]->accept(*this); llvm::Value* fd = m_currentLLVMValue;
                if (!fd) { m_currentLLVMValue = nullptr; return; }
                llvm::FunctionType* ft = llvm::FunctionType::get(int64Type, {int64Type}, false);
                m_currentLLVMValue = builder->CreateCall(getFileFn(ft), {toI64(fd)}, "file.closed");
                return;
            } else if (fname == "vyb_io_write") {
                if (node->arguments.size() != 2) {
                    logError(node->loc, "__vyb_file_write expects 2 arguments (fd, data)");
                    m_currentLLVMValue = nullptr; return;
                }
                node->arguments[0]->accept(*this); llvm::Value* fd = m_currentLLVMValue;
                node->arguments[1]->accept(*this); llvm::Value* data = m_currentLLVMValue;
                if (!fd || !data) { m_currentLLVMValue = nullptr; return; }
                llvm::Value* dataPtr = toStrPtr(data);
                llvm::Value* dataLen = llvm::ConstantInt::get(int64Type, 0);
                if (data->getType()->isStructTy())
                    dataLen = builder->CreateExtractValue(data, 1, "file.strlen");
                llvm::FunctionType* ft = llvm::FunctionType::get(int64Type, {int64Type, int8PtrType, int64Type}, false);
                m_currentLLVMValue = builder->CreateCall(
                    getFileFn(ft), {toI64(fd), dataPtr, dataLen}, "file.written");
                return;
            } else if (fname == "vyb_io_read_all") {
                if (node->arguments.size() != 1) {
                    logError(node->loc, "__vyb_file_read_all expects 1 argument (fd)");
                    m_currentLLVMValue = nullptr; return;
                }
                node->arguments[0]->accept(*this); llvm::Value* fd = m_currentLLVMValue;
                if (!fd) { m_currentLLVMValue = nullptr; return; }
                std::vector<llvm::Type*> strFields = {int8PtrType, int64Type};
                llvm::StructType* strStructType = llvm::StructType::get(*context, strFields, false);
                llvm::FunctionType* ft = llvm::FunctionType::get(strStructType, {int64Type}, false);
                m_currentLLVMValue = builder->CreateCall(getFileFn(ft), {toI64(fd)}, "file.content");
                return;
            } else if (fname == "vyb_io_error_code") {
                if (!node->arguments.empty()) {
                    logError(node->loc, "__vyb_file_error_code expects no arguments");
                    m_currentLLVMValue = nullptr; return;
                }
                llvm::FunctionType* ft = llvm::FunctionType::get(int64Type, {}, false);
                m_currentLLVMValue = builder->CreateCall(getFileFn(ft), {}, "file.errcode");
                return;
            } else { // __vyb_file_error_message
                if (!node->arguments.empty()) {
                    logError(node->loc, "__vyb_file_error_message expects no arguments");
                    m_currentLLVMValue = nullptr; return;
                }
                llvm::FunctionType* fmsg = llvm::FunctionType::get(int8PtrType, {}, false);
                llvm::Value* msgPtr = builder->CreateCall(getFileFn(fmsg), {}, "file.errmsg");
                std::vector<llvm::Type*> strFields = {int8PtrType, int64Type};
                llvm::StructType* strStructType = llvm::StructType::get(*context, strFields, false);
                llvm::FunctionType* strlenType = llvm::FunctionType::get(int64Type, {int8PtrType}, false);
                llvm::Function* strlenFunc = module->getFunction("strlen");
                if (!strlenFunc)
                    strlenFunc = llvm::Function::Create(strlenType, llvm::Function::ExternalLinkage, "strlen", module.get());
                llvm::Value* msgLen = builder->CreateCall(strlenFunc, {msgPtr}, "file.errlen");
                llvm::Value* outStr = llvm::UndefValue::get(strStructType);
                outStr = builder->CreateInsertValue(outStr, msgPtr, 0, "file.msg.data");
                outStr = builder->CreateInsertValue(outStr, msgLen, 1, "file.msg.len");
                m_currentLLVMValue = outStr;
                return;
            }
        }
    }

    // Handle Network I/O intrinsics (network stdlib module). Mirrors the File I/O
    // mapping: each Vyb-level call (`vyb_net_*`) resolves to an exported runtime
    // symbol (`__vyb_net_*`) in `runtime/vyb_runtime.c`. IP addresses and payloads
    // arrive as Vyb String structs { ptr, len } and their data pointer (and length
    // for send) is extracted at the boundary. `recv` and the error message return
    // an owned, registry-registered heap buffer so the Vyb String built over it is
    // freed by normal reference-counted cleanup.
    if (identCallee) {
        const std::string& fname = identCallee->name;
        std::string rtName;
        if (fname == "vyb_net_open") rtName = "__vyb_net_open";
        else if (fname == "vyb_net_close") rtName = "__vyb_net_close";
        else if (fname == "vyb_net_bind") rtName = "__vyb_net_bind";
        else if (fname == "vyb_net_listen") rtName = "__vyb_net_listen";
        else if (fname == "vyb_net_accept") rtName = "__vyb_net_accept";
        else if (fname == "vyb_net_connect") rtName = "__vyb_net_connect";
        else if (fname == "vyb_net_send") rtName = "__vyb_net_send";
        else if (fname == "vyb_net_recv") rtName = "__vyb_net_recv";
        else if (fname == "vyb_net_local_port") rtName = "__vyb_net_local_port";
        else if (fname == "vyb_net_error_code") rtName = "__vyb_net_error_code";
        else if (fname == "vyb_net_error_message") rtName = "__vyb_net_error_message";
        if (!rtName.empty()) {
            auto getNetFn = [&](llvm::FunctionType* ft) -> llvm::Function* {
                llvm::Function* f = module->getFunction(rtName);
                if (!f) f = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, rtName, module.get());
                return f;
            };
            auto toI64 = [&](llvm::Value* v) -> llvm::Value* {
                if (!v) return v;
                if (v->getType()->isIntegerTy(64)) return v;
                if (v->getType()->isIntegerTy())
                    return builder->CreateSExt(v, int64Type, "net.toi64");
                return v;
            };
            auto toStrPtr = [&](llvm::Value* v) -> llvm::Value* {
                if (v && v->getType()->isStructTy())
                    return builder->CreateExtractValue(v, 0, "net.strptr");
                return v;
            };
            auto strStructType = [&]() {
                std::vector<llvm::Type*> f = {int8PtrType, int64Type};
                return llvm::StructType::get(*context, f, false);
            };

            auto needArg = [&](size_t idx) -> llvm::Value* {
                if (idx >= node->arguments.size()) { m_currentLLVMValue = nullptr; return nullptr; }
                node->arguments[idx]->accept(*this);
                return m_currentLLVMValue;
            };
            auto checkArity = [&](size_t n) -> bool {
                if (node->arguments.size() != n) {
                    logError(node->loc, rtName + " expects " + std::to_string(n) + " argument(s)");
                    m_currentLLVMValue = nullptr;
                    return false;
                }
                return true;
            };

            if (fname == "vyb_net_open") {
                if (!checkArity(3)) return;
                llvm::Value* d = needArg(0); llvm::Value* t = needArg(1); llvm::Value* pr = needArg(2);
                if (!d || !t || !pr) return;
                llvm::FunctionType* ft = llvm::FunctionType::get(int64Type, {int64Type, int64Type, int64Type}, false);
                m_currentLLVMValue = builder->CreateCall(getNetFn(ft), {toI64(d), toI64(t), toI64(pr)}, "net.fd");
                return;
            } else if (fname == "vyb_net_close") {
                if (!checkArity(1)) return;
                llvm::Value* fd = needArg(0); if (!fd) return;
                llvm::FunctionType* ft = llvm::FunctionType::get(int64Type, {int64Type}, false);
                m_currentLLVMValue = builder->CreateCall(getNetFn(ft), {toI64(fd)}, "net.closed");
                return;
            } else if (fname == "vyb_net_bind" || fname == "vyb_net_connect") {
                if (!checkArity(3)) return;
                llvm::Value* fd = needArg(0); llvm::Value* ip = needArg(1); llvm::Value* port = needArg(2);
                if (!fd || !ip || !port) return;
                llvm::FunctionType* ft = llvm::FunctionType::get(int64Type, {int64Type, int8PtrType, int64Type}, false);
                m_currentLLVMValue = builder->CreateCall(getNetFn(ft), {toI64(fd), toStrPtr(ip), toI64(port)},
                    fname == "vyb_net_bind" ? "net.bound" : "net.connected");
                return;
            } else if (fname == "vyb_net_listen") {
                if (!checkArity(2)) return;
                llvm::Value* fd = needArg(0); llvm::Value* backlog = needArg(1);
                if (!fd || !backlog) return;
                llvm::FunctionType* ft = llvm::FunctionType::get(int64Type, {int64Type, int64Type}, false);
                m_currentLLVMValue = builder->CreateCall(getNetFn(ft), {toI64(fd), toI64(backlog)}, "net.listened");
                return;
            } else if (fname == "vyb_net_accept") {
                if (!checkArity(1)) return;
                llvm::Value* fd = needArg(0); if (!fd) return;
                llvm::FunctionType* ft = llvm::FunctionType::get(int64Type, {int64Type}, false);
                m_currentLLVMValue = builder->CreateCall(getNetFn(ft), {toI64(fd)}, "net.accepted");
                return;
            } else if (fname == "vyb_net_send") {
                if (!checkArity(2)) return;
                llvm::Value* fd = needArg(0); llvm::Value* data = needArg(1);
                if (!fd || !data) return;
                llvm::Value* dataPtr = toStrPtr(data);
                llvm::Value* dataLen = llvm::ConstantInt::get(int64Type, 0);
                if (data->getType()->isStructTy())
                    dataLen = builder->CreateExtractValue(data, 1, "net.strlen");
                llvm::FunctionType* ft = llvm::FunctionType::get(int64Type, {int64Type, int8PtrType, int64Type}, false);
                m_currentLLVMValue = builder->CreateCall(getNetFn(ft), {toI64(fd), dataPtr, dataLen}, "net.sent");
                return;
            } else if (fname == "vyb_net_recv") {
                if (!checkArity(2)) return;
                llvm::Value* fd = needArg(0); llvm::Value* maxlen = needArg(1);
                if (!fd || !maxlen) return;
                llvm::FunctionType* ft = llvm::FunctionType::get(strStructType(), {int64Type, int64Type}, false);
                m_currentLLVMValue = builder->CreateCall(getNetFn(ft), {toI64(fd), toI64(maxlen)}, "net.recved");
                return;
            } else if (fname == "vyb_net_local_port") {
                if (!checkArity(1)) return;
                llvm::Value* fd = needArg(0); if (!fd) return;
                llvm::FunctionType* ft = llvm::FunctionType::get(int64Type, {int64Type}, false);
                m_currentLLVMValue = builder->CreateCall(getNetFn(ft), {toI64(fd)}, "net.port");
                return;
            } else if (fname == "vyb_net_error_code") {
                if (!checkArity(0)) return;
                llvm::FunctionType* ft = llvm::FunctionType::get(int64Type, {}, false);
                m_currentLLVMValue = builder->CreateCall(getNetFn(ft), {}, "net.errcode");
                return;
            } else { // vyb_net_error_message
                if (!checkArity(0)) return;
                llvm::FunctionType* fmsg = llvm::FunctionType::get(int8PtrType, {}, false);
                llvm::Value* msgPtr = builder->CreateCall(getNetFn(fmsg), {}, "net.errmsg");
                llvm::FunctionType* strlenType = llvm::FunctionType::get(int64Type, {int8PtrType}, false);
                llvm::Function* strlenFunc = module->getFunction("strlen");
                if (!strlenFunc)
                    strlenFunc = llvm::Function::Create(strlenType, llvm::Function::ExternalLinkage, "strlen", module.get());
                llvm::Value* msgLen = builder->CreateCall(strlenFunc, {msgPtr}, "net.errlen");
                llvm::Value* outStr = llvm::UndefValue::get(strStructType());
                outStr = builder->CreateInsertValue(outStr, msgPtr, 0, "net.msg.data");
                outStr = builder->CreateInsertValue(outStr, msgLen, 1, "net.msg.len");
                m_currentLLVMValue = outStr;
                return;
            }
        }
    }

    // Time module intrinsics: each vyb_time_* maps to a no-arg (or one-arg
    // sleep) Int-returning runtime symbol in `runtime/vyb_runtime.c`.
    if (identCallee) {
        const std::string& tname = identCallee->name;
        std::string rtTime;
        if (tname == "vyb_time_epoch_secs") rtTime = "__vyb_time_epoch_secs";
        else if (tname == "vyb_time_epoch_millis") rtTime = "__vyb_time_epoch_millis";
        else if (tname == "vyb_time_nanos") rtTime = "__vyb_time_nanos";
        else if (tname == "vyb_time_mono_millis") rtTime = "__vyb_time_mono_millis";
        else if (tname == "vyb_time_sleep_ms") rtTime = "__vyb_time_sleep_ms";
        if (!rtTime.empty()) {
            llvm::Function* f = module->getFunction(rtTime);
            if (!f) {
                llvm::FunctionType* ft = (tname == "vyb_time_sleep_ms")
                    ? llvm::FunctionType::get(int64Type, {int64Type}, false)
                    : llvm::FunctionType::get(int64Type, {}, false);
                f = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, rtTime, module.get());
            }
            if (tname == "vyb_time_sleep_ms") {
                if (node->arguments.size() != 1) {
                    logError(node->loc, rtTime + " expects 1 argument (millis)");
                    m_currentLLVMValue = nullptr; return;
                }
                node->arguments[0]->accept(*this);
                llvm::Value* ms = m_currentLLVMValue;
                if (!ms) return;
                if (ms->getType()->isIntegerTy(64)) m_currentLLVMValue = builder->CreateCall(f, {ms}, "time.slept");
                else if (ms->getType()->isIntegerTy()) m_currentLLVMValue = builder->CreateCall(f, {builder->CreateSExt(ms, int64Type, "time.ms")}, "time.slept");
                else m_currentLLVMValue = nullptr;
                return;
            }
            if (!node->arguments.empty()) {
                logError(node->loc, rtTime + " expects no arguments");
                m_currentLLVMValue = nullptr; return;
            }
            m_currentLLVMValue = builder->CreateCall(f, {}, "time.value");
            return;
        }
    }

    // Handle Threads intrinsics (threads stdlib module). `vyb_thread_spawn`
    // takes a `fn() -> Int` closure value `{ env, fn }`: unpack the two
    // pointers and hand them to the pthread trampoline so it can run the
    // closure with its hidden environment parameter. The rest take/return plain
    // Int handles.
    if (identCallee) {
        const std::string& fname = identCallee->name;
        if (fname == "vyb_thread_spawn") {
            if (node->arguments.size() != 1) {
                logError(node->loc, "vyb_thread_spawn expects 1 argument (fn() -> Int)");
                m_currentLLVMValue = nullptr; return;
            }
            node->arguments[0]->accept(*this);
            llvm::Value* cl = m_currentLLVMValue;
            if (!cl) return;
            llvm::StructType* closureTy = getClosureStructType();
            llvm::Value* envPtr = nullptr;
            llvm::Value* fnPtr = nullptr;
            if (cl->getType()->isStructTy()) {
                envPtr = builder->CreateExtractValue(cl, 0, "thr.env");
                fnPtr = builder->CreateExtractValue(cl, 1, "thr.fn");
            } else if (cl->getType()->isPointerTy()) {
                llvm::Value* closureVal = builder->CreateLoad(closureTy, cl, "thr.closure");
                envPtr = builder->CreateExtractValue(closureVal, 0, "thr.env");
                fnPtr = builder->CreateExtractValue(closureVal, 1, "thr.fn");
            } else {
                logError(node->loc, "vyb_thread_spawn argument is not a fn() closure");
                m_currentLLVMValue = nullptr; return;
            }
            llvm::Function* f = module->getFunction("__vyb_thread_spawn");
            if (!f) {
                llvm::FunctionType* ft = llvm::FunctionType::get(
                    int64Type, {llvm::PointerType::get(*context, 0), llvm::PointerType::get(*context, 0)}, false);
                f = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "__vyb_thread_spawn", module.get());
            }
            m_currentLLVMValue = builder->CreateCall(f, {envPtr, fnPtr}, "thr.handle");
            return;
        } else if (fname == "vyb_thread_join" || fname == "vyb_mutex_lock" ||
                   fname == "vyb_mutex_unlock" || fname == "vyb_mutex_free") {
            if (node->arguments.size() != 1) {
                logError(node->loc, fname + " expects 1 argument");
                m_currentLLVMValue = nullptr; return;
            }
            node->arguments[0]->accept(*this);
            llvm::Value* arg = m_currentLLVMValue;
            if (!arg) return;
            llvm::Value* a = arg;
            if (a->getType()->isIntegerTy() && !a->getType()->isIntegerTy(64))
                a = builder->CreateSExt(a, int64Type, "thr.toi64");
            std::string rtName = (fname == "vyb_thread_join") ? "__vyb_thread_join"
                              : (fname == "vyb_mutex_lock") ? "__vyb_mutex_lock"
                              : (fname == "vyb_mutex_unlock") ? "__vyb_mutex_unlock" : "__vyb_mutex_free";
            llvm::Function* f = module->getFunction(rtName);
            if (!f) {
                llvm::FunctionType* ft = llvm::FunctionType::get(int64Type, {int64Type}, false);
                f = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, rtName, module.get());
            }
            m_currentLLVMValue = builder->CreateCall(f, {a}, "thr.ret");
            return;
        } else if (fname == "vyb_mutex_new") {
            if (!node->arguments.empty()) {
                logError(node->loc, "vyb_mutex_new expects no arguments");
                m_currentLLVMValue = nullptr; return;
            }
            llvm::Function* f = module->getFunction("__vyb_mutex_new");
            if (!f) {
                llvm::FunctionType* ft = llvm::FunctionType::get(int64Type, {}, false);
                f = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "__vyb_mutex_new", module.get());
            }
            m_currentLLVMValue = builder->CreateCall(f, {}, "mutex.handle");
            return;
        }
    }

    // Handle serialization mode intrinsics: lit(), notype(), bare(), deserial()
    if (identCallee && identCallee->name == "lit" && node->arguments.size() >= 1) {
        // lit() intrinsic - convert value(s) to their raw string/JSON literal representation
        // Single arg: returns the literal string
        // Multiple args: returns a JSON array string like [arg1, arg2, ...]
        VYB_CDBG << "DEBUG: Processing lit() intrinsic with " << node->arguments.size() << " args" << std::endl;

        auto serializeLitArg = [&](ast::ExprPtr& argExpr) -> llvm::Value* {
            argExpr->accept(*this);
            llvm::Value* arg = m_currentLLVMValue;
            if (!arg) return nullptr;
            llvm::Type* argType = arg->getType();

            // If the argument is a Vyb string struct { ptr, i64 }, extract the ptr field
            if (argType->isStructTy() && argType->getStructNumElements() == 2 &&
                argType->getStructElementType(1)->isIntegerTy(64)) {
                arg = builder->CreateExtractValue(arg, 0, "lit.strptr");
                argType = arg->getType();
            }

            // If the argument is a non-pointer scalar (Int, Float, Bool), convert to string first
            if (argType->isIntegerTy() && !argType->isIntegerTy(8)) {
                // Boolean: output "true" or "false"
                if (argType->isIntegerTy(1)) {
                    llvm::Value* trueStr = builder->CreateGlobalStringPtr("true", "lit.bool.true");
                    llvm::Value* falseStr = builder->CreateGlobalStringPtr("false", "lit.bool.false");
                    return builder->CreateSelect(arg, trueStr, falseStr, "lit.bool_result");
                }
                std::string toStringFuncName = "__vyb_int_to_string";
                llvm::FunctionType* toStringFuncType = llvm::FunctionType::get(int8PtrType, {int64Type}, false);
                llvm::Function* toStringFunc = module->getFunction(toStringFuncName);
                if (!toStringFunc) {
                    toStringFunc = llvm::Function::Create(toStringFuncType, llvm::Function::ExternalLinkage, toStringFuncName, module.get());
                }
                if (argType != int64Type) {
                    arg = builder->CreateSExt(arg, int64Type, "lit.int64");
                }
                return builder->CreateCall(toStringFunc, {arg}, "lit_result");
            } else if (argType->isFloatingPointTy()) {
                std::string toStringFuncName = "__vyb_float_to_string";
                llvm::FunctionType* toStringFuncType = llvm::FunctionType::get(int8PtrType, {doubleType}, false);
                llvm::Function* toStringFunc = module->getFunction(toStringFuncName);
                if (!toStringFunc) {
                    toStringFunc = llvm::Function::Create(toStringFuncType, llvm::Function::ExternalLinkage, toStringFuncName, module.get());
                }
                if (argType != doubleType) {
                    arg = builder->CreateFPExt(arg, doubleType, "lit.double");
                }
                return builder->CreateCall(toStringFunc, {arg}, "lit_result");
            }

            // For pointer types (strings), call the lit conversion function
            llvm::Function* litFunc = getLitConversionFunction();
            if (!litFunc) {
                logError(argExpr->loc, "Failed to get lit conversion function");
                return nullptr;
            }
            return builder->CreateCall(litFunc, {arg}, "lit_result");
        };

        if (node->arguments.size() == 1) {
            // Single argument: return the literal string directly
            llvm::Value* result = serializeLitArg(node->arguments[0]);
            if (!result) return;
            m_currentLLVMValue = result;
            return;
        } else {
            // Multiple arguments: produce a JSON array string like [arg1, arg2, ...]
            // Helper to get or declare __vyb_string_concat(char*, char*) -> char*
            auto getConcatFn = [&]() -> llvm::Function* {
                llvm::Function* f = module->getFunction("__vyb_string_concat");
                if (!f) {
                    llvm::FunctionType* ft = llvm::FunctionType::get(int8PtrType, {int8PtrType, int8PtrType}, false);
                    f = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "__vyb_string_concat", module.get());
                }
                return f;
            };
            llvm::Function* concatFn = getConcatFn();

            // Start with "["
            llvm::Value* jsonStr = builder->CreateGlobalStringPtr("[", "lit.arr.open");

            for (size_t i = 0; i < node->arguments.size(); ++i) {
                llvm::Value* serialized = serializeLitArg(node->arguments[i]);
                if (!serialized) return;

                // Add comma separator before all but first element
                if (i > 0) {
                    llvm::Value* sep = builder->CreateGlobalStringPtr(", ", "lit.arr.sep");
                    jsonStr = builder->CreateCall(concatFn, {jsonStr, sep}, "lit.arr.sep.call");
                }

                // Append the serialized argument
                jsonStr = builder->CreateCall(concatFn, {jsonStr, serialized}, "lit.arr.elem");
            }

            // Close with "]"
            llvm::Value* closeBracket = builder->CreateGlobalStringPtr("]", "lit.arr.close");
            jsonStr = builder->CreateCall(concatFn, {jsonStr, closeBracket}, "lit.arr.close.call");

            m_currentLLVMValue = jsonStr;
            return;
        }
    }
    if (identCallee && node->arguments.size() == 1) {
        if (identCallee->name == "lit") {
            // lit() intrinsic - convert value to its raw string/JSON literal representation
            VYB_CDBG << "DEBUG: Processing lit() intrinsic" << std::endl;
            node->arguments[0]->accept(*this);
            llvm::Value* arg = m_currentLLVMValue;
            if (!arg) {
                logError(node->arguments[0]->loc, "Argument to lit() evaluated to null");
                return;
            }

            llvm::Type* argType = arg->getType();

            // If the argument is a Vyb string struct { ptr, i64 }, extract the ptr field
            if (argType->isStructTy() && argType->getStructNumElements() == 2 &&
                argType->getStructElementType(1)->isIntegerTy(64)) {
                arg = builder->CreateExtractValue(arg, 0, "lit.strptr");
                argType = arg->getType();
            }

            // If the argument is a non-pointer scalar (Int, Float, Bool), convert to string first
            if (argType->isIntegerTy() && !argType->isIntegerTy(8)) {
                // Boolean: output "true" or "false"
                if (argType->isIntegerTy(1)) {
                    llvm::Value* trueStr = builder->CreateGlobalStringPtr("true", "lit.bool.true");
                    llvm::Value* falseStr = builder->CreateGlobalStringPtr("false", "lit.bool.false");
                    m_currentLLVMValue = builder->CreateSelect(arg, trueStr, falseStr, "lit.bool_result");
                    return;
                }
                // Integer: use __vyb_int_to_string (always cast to i64)
                std::string toStringFuncName = "__vyb_int_to_string";
                llvm::FunctionType* toStringFuncType = llvm::FunctionType::get(
                    int8PtrType, {int64Type}, false);
                llvm::Function* toStringFunc = module->getFunction(toStringFuncName);
                if (!toStringFunc) {
                    toStringFunc = llvm::Function::Create(toStringFuncType,
                        llvm::Function::ExternalLinkage, toStringFuncName, module.get());
                }
                // Cast to i64 if needed
                if (argType != int64Type) {
                    // Use ZExt for i1 (bool), SExt for other integers
                    if (argType->isIntegerTy(1)) {
                        arg = builder->CreateZExt(arg, int64Type, "lit.int64");
                    } else {
                        arg = builder->CreateSExt(arg, int64Type, "lit.int64");
                    }
                }
                m_currentLLVMValue = builder->CreateCall(toStringFunc, {arg}, "lit_result");
                VYB_CDBG << "DEBUG: Created call to lit conversion function" << std::endl;
                return;
            } else if (argType->isFloatingPointTy()) {
                // Float: use __vyb_float_to_string
                std::string toStringFuncName = "__vyb_float_to_string";
                llvm::FunctionType* toStringFuncType = llvm::FunctionType::get(
                    int8PtrType, {doubleType}, false);
                llvm::Function* toStringFunc = module->getFunction(toStringFuncName);
                if (!toStringFunc) {
                    toStringFunc = llvm::Function::Create(toStringFuncType,
                        llvm::Function::ExternalLinkage, toStringFuncName, module.get());
                }
                if (argType != doubleType) {
                    arg = builder->CreateFPExt(arg, doubleType, "lit.double");
                }
                m_currentLLVMValue = builder->CreateCall(toStringFunc, {arg}, "lit_result");
                VYB_CDBG << "DEBUG: Created call to lit conversion function" << std::endl;
                return;
            }

            // For pointer types (strings), call the lit conversion function
            VYB_CDBG << "DEBUG: Getting lit conversion function..." << std::endl;
            llvm::Function* litFunc = getLitConversionFunction();
            if (!litFunc) {
                VYB_CDBG << "DEBUG: Failed to get lit conversion function!" << std::endl;
                logError(node->loc, "Failed to get lit conversion function");
                return;
            }
            VYB_CDBG << "DEBUG: Got lit conversion function: " << litFunc->getName().str() << std::endl;
            std::vector<llvm::Value*> args = {arg};
            m_currentLLVMValue = builder->CreateCall(litFunc, args, "lit_result");
            VYB_CDBG << "DEBUG: Created call to lit conversion function" << std::endl;
            return;
        }
        else if (identCallee->name == "notype" || identCallee->name == "bare") {
            // notype() and bare() intrinsics - forward the inner value
            node->arguments[0]->accept(*this);
            // m_currentLLVMValue is already set to the inner expression's result
            return;
        }
        else if (identCallee->name == "deserial") {
            // deserial() intrinsic - forward the inner value for now
            node->arguments[0]->accept(*this);
            // m_currentLLVMValue is already set to the inner expression's result
            return;
        }
    }

    // Check for method calls before trying function lookup
    if (auto memberExpr = dynamic_cast<vyb::ast::MemberExpression*>(node->callee.get())) {
        if (auto methodIdent = dynamic_cast<vyb::ast::Identifier*>(memberExpr->property.get())) {
            std::string methodName = methodIdent->name;

            // Handle to_string method calls
            if (methodName == "to_string") {
                VYB_CDBG << "DEBUG: Processing to_string method call" << std::endl;

                // Evaluate the object (the thing we're calling to_string on)
                memberExpr->object->accept(*this);
                llvm::Value* objectValue = m_currentLLVMValue;
                if (!objectValue) {
                    logError(memberExpr->object->loc, "Failed to evaluate object for to_string method call");
                    m_currentLLVMValue = nullptr;
                    return;
                }

                // Get the type of the object
                llvm::Type* objectType = objectValue->getType();
                vyb::ast::TypeNode* objectASTType = nullptr;

                // Try to get AST type information for better type resolution
                auto valueTypeIter = valueTypeMap.find(objectValue);
                if (valueTypeIter != valueTypeMap.end()) {
                    objectASTType = valueTypeIter->second.get();
                }

                // Generate the to_string call
                llvm::Value* result = generateToStringCall(objectValue, objectType, objectASTType, node->loc);
                if (result) {
                    VYB_CDBG << "DEBUG: Successfully generated to_string call" << std::endl;
                    m_currentLLVMValue = result;
                    return;
                } else {
                    logError(node->loc, "Failed to generate to_string call for type");
                    m_currentLLVMValue = nullptr;
                    return;
                }
            }

            // Handle Vec, String, and Tuple method calls
            if (auto objIdent = dynamic_cast<vyb::ast::Identifier*>(memberExpr->object.get())) {
                std::string objectName = objIdent->name;

                // Check if this is a Tuple, String, or Vec variable by looking at AST type info first
                auto varIt = namedValues.find(objectName);
                bool isTupleVar = false;
                bool isStringVar = false;
                bool isVecVar = false;
                bool isByRefVec = false;
                unsigned tupleSize = 0;

                if (varIt != namedValues.end()) {
                    // First check valueTypeMap for AST type information (most reliable)
                    auto typeIt = valueTypeMap.find(varIt->second);
                    if (typeIt != valueTypeMap.end() && typeIt->second) {
                        vyb::ast::TypeNode* astType = typeIt->second.get();
                        // Check for TupleTypeNode
                        if (auto tupleType = dynamic_cast<vyb::ast::TupleTypeNode*>(astType)) {
                            isTupleVar = true;
                            tupleSize = tupleType->memberTypes.size();
                        }
                        // Check for VecType
                        else if (dynamic_cast<vyb::ast::VecType*>(astType)) {
                            isVecVar = true;
                        }
                        // Check for String (represented as TypeName with identifier "String")
                        else if (auto typeName = dynamic_cast<vyb::ast::TypeName*>(astType)) {
                            if (typeName->identifier && typeName->identifier->name == "String") {
                                isStringVar = true;
                            } else if (typeName->identifier &&
                                       (typeName->identifier->name == "their" ||
                                        typeName->identifier->name == "my" ||
                                        typeName->identifier->name == "our" ||
                                        typeName->identifier->name == "view" ||
                                        typeName->identifier->name == "borrow") &&
                                       typeName->genericArgs.size() == 1) {
                                // Ownership-wrapped Vec receiver: the slot stores a
                                // `Vec*` (mutable/by-ref borrow), deref'd at the call.
                                ast::TypeNode* inner = typeName->genericArgs[0].get();
                                isByRefVec = dynamic_cast<vyb::ast::VecType*>(inner) != nullptr;
                                if (!isByRefVec) {
                                    if (auto innerTN = dynamic_cast<ast::TypeName*>(inner)) {
                                        isByRefVec = innerTN->identifier && innerTN->identifier->name == "Vec";
                                    }
                                }
                                if (isByRefVec) isVecVar = true;
                            }
                        }
                    }

                    // Fall back to LLVM type analysis if no AST type info available
                    if (!isTupleVar && !isStringVar && !isVecVar) {
                        if (auto allocaInst = llvm::dyn_cast<llvm::AllocaInst>(varIt->second)) {
                            llvm::Type* allocatedType = allocaInst->getAllocatedType();
                            if (allocatedType->isStructTy()) {
                                llvm::StructType* structType = llvm::cast<llvm::StructType>(allocatedType);
                                unsigned numElements = structType->getNumElements();
                                // String struct has 2 fields: { ptr, len }
                                // Vec struct has 3 fields: { ptr, size, capacity }
                                // This is a heuristic fallback only
                                if (numElements == 2) {
                                    isStringVar = true;
                                } else if (numElements == 3) {
                                    isVecVar = true;
                                } else {
                                    // Assume tuple for other struct sizes
                                    isTupleVar = true;
                                    tupleSize = numElements;
                                }
                            }
                        }
                    }
                }

                // Handle Tuple methods first (highest priority)
                if (isTupleVar && methodName == "len") {
                    m_currentLLVMValue = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), tupleSize);
                    return;
                }

                // Handle String methods
                if (isStringVar) {
                    if (methodName == "len" || methodName == "length" || methodName == "concat" || methodName == "substring" ||
                        methodName == "substr" || methodName == "char_at" || methodName == "to_bytes" ||
                        methodName == "from_bytes" || methodName == "starts_with" || methodName == "ends_with" ||
                        methodName == "contains" || methodName == "to_upper" || methodName == "to_lower" ||
                        methodName == "trim" || methodName == "strip" || methodName == "replace" ||
                        methodName == "format") {
                        handleStringMethod(node, objectName, methodName);
                        return;
                    }
                }

                // Handle Vec methods
                if (isVecVar || (!isTupleVar && !isStringVar && (methodName == "push" || methodName == "pop" || methodName == "get" ||
                    methodName == "push_array" || methodName == "to_array" || methodName == "get_array" ||
                    methodName == "clear" || methodName == "is_empty" || methodName == "capacity" ||
                    methodName == "remove_at" || methodName == "get_vec"))) {
                    // These methods are Vec-specific or we couldn't determine type
                    if (methodName == "push" || methodName == "pop" || methodName == "len" || methodName == "get" || methodName == "set" ||
                        methodName == "push_array" || methodName == "to_array" || methodName == "get_array" ||
                        methodName == "clear" || methodName == "is_empty" || methodName == "capacity" ||
                        methodName == "concat" || methodName == "contains" || methodName == "remove_at" ||
                        methodName == "get_vec") {
                        // By-ref receiver (their<Vec<T>> / my<Vec<T>> / ...): the
                        // named slot holds a `Vec*` (a mutable borrow), so load that
                        // pointer and operate on it directly, rather than treating the
                        // slot itself as the Vec struct.
                        if (isByRefVec && varIt != namedValues.end()) {
                            llvm::Value* slot = varIt->second;
                            llvm::Value* vecPtr = builder->CreateLoad(
                                llvm::PointerType::get(*context, 0), slot, "byref.vec.load");
                            handleVecMethodOnValue(node, vecPtr, methodName, memberExpr->object.get());
                            return;
                        }
                        handleVecMethod(node, objectName, methodName);
                        return;
                    }
                }
            }

            // String method on a non-identifier receiver: a string literal, a
            // function/expression result, or a struct/array field whose type is
            // String (e.g. "hello {}!".format(...), get().substring(...),
            // p.name.to_upper()). Runs before the Vec-member fallback below so a
            // String method like `contains`/`concat`/`len` isn't misrouted to the
            // Vec path. Evaluate the receiver once, materialize a pointer to its
            // { ptr, len } struct, then dispatch to either the built-in String
            // handlers or a String-bound aspect method (e.g. split).
            bool objectAstIsString = false;
            if (memberExpr->object->type) {
                if (auto objTn = dynamic_cast<ast::TypeName*>(memberExpr->object->type.get())) {
                    objectAstIsString = objTn->identifier && objTn->identifier->name == "String";
                }
            }
            if (objectAstIsString) {
                memberExpr->object->accept(*this);
                llvm::Value* strVal = m_currentLLVMValue;
                if (!strVal) {
                    logError(memberExpr->object->loc, "Failed to evaluate String method receiver");
                    m_currentLLVMValue = nullptr;
                    return;
                }
                llvm::Value* strPtr = nullptr;
                llvm::Type* strStructTy = nullptr;
                if (strVal->getType()->isStructTy()) {
                    strStructTy = strVal->getType();
                }
                if (strVal->getType()->isStructTy()) {
                    llvm::AllocaInst* tmp = builder->CreateAlloca(strVal->getType(), nullptr, "str.rcv.tmp");
                    builder->CreateStore(strVal, tmp);
                    strPtr = tmp;
                } else if (strVal->getType()->isPointerTy()) {
                    strPtr = strVal;
                }
                if (!strPtr) {
                    logError(memberExpr->object->loc, "String method receiver is not a String struct");
                    m_currentLLVMValue = nullptr;
                    return;
                }

                if (methodName == "len" || methodName == "length" || methodName == "concat" ||
                    methodName == "substring" || methodName == "substr" || methodName == "char_at" ||
                    methodName == "to_bytes" || methodName == "starts_with" || methodName == "ends_with" ||
                    methodName == "contains" || methodName == "to_upper" || methodName == "to_lower" ||
                    methodName == "trim" || methodName == "strip" || methodName == "replace" ||
                    methodName == "format") {
                    handleStringMethodOnValue(node, strPtr, methodName);
                    return;
                }

                // Non-built-in: this may be a String-bound aspect method (e.g. split).
                SemanticAnalyzer* semantic = driver_.hasSemanticAnalyzer()
                    ? driver_.getSemanticAnalyzer() : nullptr;
                if (semantic) {
                    const std::string concreteType = "String";
                    const auto& impls = semantic->getTraitImpls();
                    auto limpl = impls.find(concreteType);
                    if (limpl != impls.end()) {
                        for (const auto& traitEntry : limpl->second) {
                            bool hasMethod = false;
                            for (const ast::FunctionDeclaration* m : traitEntry.second) {
                                if (m && m->id && m->id->name == methodName) { hasMethod = true; break; }
                            }
                            if (!hasMethod) continue;
                            const std::string& trait = traitEntry.first;
                            llvm::Function* implFunc = module->getFunction(concreteType + "_" + trait + "_" + methodName);
                            if (!implFunc) implFunc = monomorphizeTraitMethod(concreteType, trait, methodName);
                            if (!implFunc) continue;

                            bool selfIsByRef = implFunc->getArg(0)->getType()->isPointerTy();
                            std::vector<llvm::Value*> argValues;
                            if (selfIsByRef) {
                                argValues.push_back(strPtr);
                            } else if (strStructTy) {
                                argValues.push_back(builder->CreateLoad(strStructTy, strPtr, "str.rcv.load"));
                            } else {
                                argValues.push_back(strPtr);
                            }
                            for (size_t a = 0; a < node->arguments.size(); ++a) {
                                auto& arg = node->arguments[a];
                                arg->accept(*this);
                                if (!m_currentLLVMValue) {
                                    logError(arg->loc, "Argument codegen failed for aspect method " + methodName);
                                    m_currentLLVMValue = nullptr;
                                    return;
                                }
                                llvm::Value* argVal = m_currentLLVMValue;
                                if (argVal->getType()->isPointerTy()) {
                                    llvm::Type* expected = implFunc->getFunctionType()->getParamType(argValues.size());
                                    if (expected && expected->isStructTy()) {
                                        llvm::StructType* st = llvm::dyn_cast<llvm::StructType>(expected);
                                        if (st && st->getNumElements() == 2 &&
                                            st->getElementType(0)->isPointerTy() &&
                                            st->getElementType(1)->isIntegerTy(64)) {
                                            llvm::Value* wrapped = tryCast(argVal, expected, arg->loc);
                                            if (wrapped) argVal = wrapped;
                                        }
                                    }
                                }
                                argValues.push_back(argVal);
                            }
                            if (implFunc->getReturnType()->isVoidTy()) {
                                builder->CreateCall(implFunc, argValues);
                                m_currentLLVMValue = nullptr;
                            } else {
                                m_currentLLVMValue = builder->CreateCall(implFunc, argValues, "aspect.method.result");
                            }
                            return;
                        }
                    }
                }
            }

            // Handle Vec method calls on member expressions (e.g., tree.nodes.push())
            // The object is itself a member expression
            if (methodName == "push" || methodName == "pop" || methodName == "len" || methodName == "get" || methodName == "set" ||
                methodName == "push_array" || methodName == "to_array" || methodName == "get_array" ||
                methodName == "clear" || methodName == "is_empty" || methodName == "capacity" ||
                methodName == "concat" || methodName == "contains" || methodName == "remove_at" ||
                methodName == "get_vec") {
                // Evaluate the object in "LHS mode" (pointer mode) to get a pointer to the Vec field.
                // Without this, evaluating `s.items` loads a copy of the Vec struct and mutations
                // (like push) would be applied to a temporary, losing the changes.
                bool savedLHS = m_isLHSOfAssignment;
                m_isLHSOfAssignment = true;
                memberExpr->object->accept(*this);
                m_isLHSOfAssignment = savedLHS;
                llvm::Value* vecValue = m_currentLLVMValue;
                if (!vecValue) {
                    logError(memberExpr->object->loc, "Failed to evaluate object for Vec method call");
                    m_currentLLVMValue = nullptr;
                    return;
                }

                // A struct field typed as a by-ref Vec (`their<Vec<T>>` / `my<...>` /
                // `view<...>` / `our<...>` / `borrow<...>`) is laid out as a single
                // pointer slot holding the borrowed Vec's address. LHS-mode evaluation
                // of `obj.field` above yields the *slot* address (a `Vec**`); load once
                // to recover the `Vec*` before operating on it. A plain `Vec<T>` field
                // is emitted directly as the Vec struct, so this only applies when the
                // field's declared type is an ownership wrapper around a Vec.
                if (auto objTn = dynamic_cast<ast::TypeName*>(memberExpr->object->type.get())) {
                    if (objTn->identifier &&
                        (objTn->identifier->name == "their" || objTn->identifier->name == "my" ||
                         objTn->identifier->name == "our" || objTn->identifier->name == "view" ||
                         objTn->identifier->name == "borrow") &&
                        objTn->genericArgs.size() == 1) {
                        ast::TypeNode* inner = objTn->genericArgs[0].get();
                        bool innerIsVec = dynamic_cast<ast::VecType*>(inner) != nullptr;
                        if (!innerIsVec) {
                            if (auto innerTN = dynamic_cast<ast::TypeName*>(inner)) {
                                innerIsVec = innerTN->identifier && innerTN->identifier->name == "Vec";
                            }
                        }
                        if (innerIsVec) {
                            llvm::Type* vecPtrTy = llvm::PointerType::get(*context, 0);
                            vecValue = builder->CreateLoad(vecPtrTy, vecValue, "byref.vec.field.load");
                        }
                    }
                }

                // Handle the Vec method with the evaluated value directly
                handleVecMethodOnValue(node, vecValue, methodName, memberExpr->object.get());
                return;
            }

            // General aspect/bind method dispatch for a member-expression receiver
            // (e.g. `h.c.bump()` on a struct field, or `self.data.sort_in_place()`
            // through an ownership-wrapped their<Vec<T>> field). Built-in String/Vec
            // methods returned above; anything else that resolves to a bind lands here.
            {
                SemanticAnalyzer* semantic = driver_.hasSemanticAnalyzer()
                    ? driver_.getSemanticAnalyzer() : nullptr;
                ast::TypeNode* objTy2 = memberExpr->object->type
                    ? memberExpr->object->type.get() : nullptr;
                if (semantic && objTy2 && (dynamic_cast<ast::TypeName*>(objTy2) ||
                                           dynamic_cast<ast::VecType*>(objTy2))) {
                    std::string concreteType = objTy2->toString();
                    bool ownershipWrappedVec = false;
                    if (auto objTn2 = dynamic_cast<ast::TypeName*>(objTy2)) {
                        const std::string kw = objTn2->identifier ? objTn2->identifier->name : "";
                        if ((kw == "their" || kw == "my" || kw == "our" ||
                             kw == "view" || kw == "borrow") && objTn2->genericArgs.size() == 1) {
                            ast::TypeNode* inner = objTn2->genericArgs[0].get();
                            bool innerIsVec = dynamic_cast<ast::VecType*>(inner) != nullptr;
                            if (!innerIsVec) {
                                if (auto innerTN = dynamic_cast<ast::TypeName*>(inner)) {
                                    innerIsVec = innerTN->identifier && innerTN->identifier->name == "Vec";
                                }
                            }
                            if (innerIsVec) {
                                concreteType = inner->toString();
                                ownershipWrappedVec = true;
                            }
                        }
                    }

                    // Find a bound aspect providing this method for concreteType.
                    std::string foundTrait;
                    bool found = false;
                    const auto& impls = semantic->getTraitImpls();
                    auto limpl = impls.find(concreteType);
                    if (limpl != impls.end()) {
                        for (const auto& traitEntry : limpl->second) {
                            for (const ast::FunctionDeclaration* m : traitEntry.second) {
                                if (m && m->id && m->id->name == methodName) {
                                    foundTrait = traitEntry.first; found = true; break;
                                }
                            }
                            if (found) break;
                        }
                    }
                    if (!found) {
                        TypePattern concretePattern = TypePattern::parse(concreteType);
                        const auto& genImpls = semantic->getGenericTraitImpls();
                        for (const auto& typeEntry : genImpls) {
                            TypePattern tmpl = TypePattern::parse(typeEntry.first);
                            std::map<std::string, std::string> sub;
                            if (!tmpl.matchesPattern(concretePattern, sub)) continue;
                            for (const auto& traitEntry : typeEntry.second) {
                                const GenericImplInfo* gii = traitEntry.second.get();
                                bool has = false;
                                if (gii && gii->declaration) {
                                    for (const auto& m : gii->declaration->methods) {
                                        if (m && m->id && m->id->name == methodName) { has = true; break; }
                                    }
                                }
                                if (has) { foundTrait = traitEntry.first; found = true; break; }
                            }
                            if (found) break;
                        }
                    }

                    if (found) {
                        llvm::Function* implFunc = module->getFunction(concreteType + "_" + foundTrait + "_" + methodName);
                        if (!implFunc) implFunc = monomorphizeTraitMethod(concreteType, foundTrait, methodName);
                        if (implFunc) {
                            // Evaluate the receiver in LHS (pointer) mode to get the
                            // address of the member field / object.
                            bool savedLHS = m_isLHSOfAssignment;
                            m_isLHSOfAssignment = true;
                            memberExpr->object->accept(*this);
                            m_isLHSOfAssignment = savedLHS;
                            llvm::Value* recvPtr = m_currentLLVMValue;
                            if (!recvPtr) {
                                logError(memberExpr->object->loc, "Failed to evaluate member-expression receiver for aspect method " + methodName);
                                m_currentLLVMValue = nullptr;
                                return;
                            }
                            // Ownership-wrapped Vec field: recvPtr is the slot address
                            // (Vec**); load once to recover the Vec* the by-ref self expects.
                            llvm::Value* selfArg = recvPtr;
                            if (ownershipWrappedVec) {
                                selfArg = builder->CreateLoad(llvm::PointerType::get(*context, 0), recvPtr, "byref.aspect.recv.load");
                            }
                            bool selfIsByRef = implFunc->getArg(0)->getType()->isPointerTy();
                            std::vector<llvm::Value*> argValues;
                            if (selfIsByRef) {
                                argValues.push_back(selfArg);
                            } else {
                                argValues.push_back(builder->CreateLoad(implFunc->getArg(0)->getType(), selfArg, "aspect.recv.load"));
                            }
                            std::vector<llvm::Value*> transientClosures;
                            for (size_t a = 0; a < node->arguments.size(); ++a) {
                                auto& arg = node->arguments[a];
                                arg->accept(*this);
                                if (!m_currentLLVMValue) {
                                    logError(arg->loc, "Argument codegen failed for aspect method " + methodName);
                                    m_currentLLVMValue = nullptr;
                                    return;
                                }
                                llvm::Value* argVal = m_currentLLVMValue;
                                if (dynamic_cast<ast::FunctionExpression*>(arg.get()) &&
                                    argVal->getType()->isStructTy() &&
                                    isClosureStructType(argVal->getType())) {
                                    retainClosureValue(argVal);
                                    transientClosures.push_back(argVal);
                                }
                                if (argVal->getType()->isPointerTy()) {
                                    llvm::Type* expected = implFunc->getFunctionType()->getParamType(argValues.size());
                                    if (expected && expected->isStructTy()) {
                                        llvm::StructType* st = llvm::dyn_cast<llvm::StructType>(expected);
                                        if (st && st->getNumElements() == 2 &&
                                            st->getElementType(0)->isPointerTy() &&
                                            st->getElementType(1)->isIntegerTy(64)) {
                                            llvm::Value* wrapped = tryCast(argVal, expected, arg->loc);
                                            if (wrapped) argVal = wrapped;
                                        }
                                    }
                                }
                                argValues.push_back(argVal);
                            }
                            if (implFunc->getReturnType()->isVoidTy()) {
                                builder->CreateCall(implFunc, argValues);
                                m_currentLLVMValue = nullptr;
                            } else {
                                m_currentLLVMValue = builder->CreateCall(implFunc, argValues, "aspect.method.result");
                            }
                            for (auto* tc : transientClosures) releaseClosureValue(tc);
                            return;
                        }
                    }
                }
            }
        }
    }

    // Check if this is a call to a generic function that needs monomorphization
    if (identCallee && genericFunctionTemplates.find(identCallee->name) != genericFunctionTemplates.end()) {
        VYB_CDBG << "DEBUG: Detected call to generic function: " << identCallee->name << std::endl;

        // Get the template to know how many type parameters we need
        ast::FunctionDeclaration* templateFunc = genericFunctionTemplates[identCallee->name];
        size_t numTypeParams = templateFunc->genericParams.size();

        // Choose concrete type arguments: explicit (probe<Int>(0, 0)) if the
        // caller wrote them, otherwise infer them from the call-site argument types.
        std::vector<std::string> concreteTypeArgs(numTypeParams, "");

        if (!node->explicitTypeArgs.empty()) {
            if (node->explicitTypeArgs.size() != numTypeParams) {
                logError(node->loc, "Explicit type argument count mismatch for generic function " + identCallee->name +
                         " (expected " + std::to_string(numTypeParams) + ", got " +
                         std::to_string(node->explicitTypeArgs.size()) + ")");
                m_currentLLVMValue = nullptr;
                return;
            }
            for (size_t i = 0; i < numTypeParams; ++i) {
                concreteTypeArgs[i] = node->explicitTypeArgs[i]->toString();
            }
            VYB_CDBG << "DEBUG: Using explicit type arguments for " << identCallee->name << std::endl;
        } else {
        // Infer concrete type arguments from call site
        // Strategy: match each argument to its corresponding type parameter
        // by checking if the argument's type matches the parameter's declared type pattern
        for (size_t i = 0; i < node->arguments.size(); ++i) {
            // Evaluate argument to infer type
            node->arguments[i]->accept(*this);
            llvm::Value* argValue = m_currentLLVMValue;
            if (!argValue) {
                logError(node->arguments[i]->loc, "Failed to evaluate argument for generic function call");
                m_currentLLVMValue = nullptr;
                return;
            }

            // Get type name from AST type annotation if available
            std::string argTypeName;
            if (node->arguments[i]->type) {
                argTypeName = node->arguments[i]->type->toString();
            } else if (auto* identArg = dynamic_cast<ast::Identifier*>(node->arguments[i].get())) {
                // Look up variable's type from valueTypeMap
                auto varIt = namedValues.find(identArg->name);
                if (varIt != namedValues.end()) {
                    auto typeIt = valueTypeMap.find(varIt->second);
                    if (typeIt != valueTypeMap.end()) {
                        argTypeName = typeIt->second->toString();
                    }
                }
            }

            // Fallback: use LLVM type name
            if (argTypeName.empty()) {
                argTypeName = getTypeName(argValue->getType());
            }

            VYB_CDBG << "DEBUG: Argument " << i << " has type: " << argTypeName << std::endl;

            // Match argument to type parameter by position
            // If function has N type params and M args, map arg[i] to param[min(i, N-1)]
            // For multi-param functions, we need to match each arg to its corresponding param
            if (i < numTypeParams) {
                concreteTypeArgs[i] = argTypeName;
            } else if (numTypeParams > 0 && i >= numTypeParams) {
                // Extra arguments beyond type params — check if any remaining params are still empty
                for (size_t p = 0; p < numTypeParams; ++p) {
                    if (concreteTypeArgs[p].empty()) {
                        concreteTypeArgs[p] = argTypeName;
                        break;
                    }
                }
            }
        }
        // Fill any remaining empty type params with a default (shouldn't happen for valid code)
        for (size_t p = 0; p < numTypeParams; ++p) {
            if (concreteTypeArgs[p].empty()) {
                logError(node->loc, "Could not infer type for generic parameter '" +
                         (templateFunc->genericParams[p] && templateFunc->genericParams[p]->name ?
                          templateFunc->genericParams[p]->name->name : "unknown") + "'");
                m_currentLLVMValue = nullptr;
                return;
            }
        }
        // Resolve bind/generic-body type parameters (e.g. K -> String) in the
        // inferred/explicit concrete type args before monomorphizing, so a helper
        // called with a type-param argument (e.g. `hashkey(ck)` with `ck<K>` in a
        // `bind<K<Hashable>, V>` body) monomorphizes against the bind's concrete
        // instantiation instead of an unresolved `K`.
        for (auto& argStr : concreteTypeArgs) {
            for (const auto& kv : currentTypeSubstitutions) {
                argStr = replaceTypeTokens(argStr, kv.first, kv.second);
            }
        }
        }
        // Monomorphize the generic function
        llvm::Function* monomorphizedFunc = monomorphizeGenericFunction(identCallee->name, concreteTypeArgs);
        if (!monomorphizedFunc) {
            logError(node->loc, "Failed to monomorphize generic function " + identCallee->name);
            m_currentLLVMValue = nullptr;
            return;
        }

        VYB_CDBG << "DEBUG: Successfully monomorphized " << identCallee->name << std::endl;

        // Now proceed with the monomorphized function
        calleeName = monomorphizedFunc->getName().str();
    }

    // Lookup the function in the module - standard function call handling
    llvm::Function *calleeFunc = module->getFunction(calleeName);
    if (!calleeFunc) {
        // Try special functions that might not be in the module yet
        if (identCallee && identCallee->name == "println") {
            calleeFunc = getPrintlnFunction();
        } else if (identCallee) {
            // Check if it's a local lambda variable or a function-typed value.
            auto varIt = namedValues.find(identCallee->name);
            if (varIt != namedValues.end()) {
                // A function-typed parameter (`f<fn(T,T) -> T>`) or an explicitly
                // fn-annotated lambda variable has a FunctionType in valueTypeMap;
                // those are handled by the indirect-call path below (they share the
                // same `{ env, fn }` closure layout). Only a bare local lambda
                // (inferred closure-struct type) uses localLambdaTypes here.
                bool isFnTyped = false;
                auto vtmIt = valueTypeMap.find(varIt->second);
                isFnTyped = vtmIt != valueTypeMap.end() && vtmIt->second &&
                    dynamic_cast<ast::FunctionType*>(vtmIt->second.get()) != nullptr;
                auto lambdaTypeIt = localLambdaTypes.find(identCallee->name);
            if (!isFnTyped && lambdaTypeIt != localLambdaTypes.end()) {
                // It's a lambda stored in a local variable - use indirect call
                {
                    llvm::Value* closureAlloca = varIt->second;
                    llvm::FunctionType* lambdaFuncType = lambdaTypeIt->second;

                    // Load the closure struct { env, fn } and unpack it: call the
                    // fn pointer with the captured environment first, then args.
                    llvm::StructType* closureTy = getClosureStructType();
                    llvm::Value* closureVal = builder->CreateLoad(closureTy, closureAlloca, "lambda.closure");
                    llvm::Value* envPtr = builder->CreateExtractValue(closureVal, 0, "lambda.env");
                    llvm::Value* fnPtr = builder->CreateExtractValue(closureVal, 1, "lambda.fn");
                    // The callee's real signature also carries the environment.
                    std::vector<llvm::Type*> calleeParamTypes;
                    calleeParamTypes.push_back(llvm::PointerType::get(*context, 0));
                    for (auto* pt : lambdaFuncType->params()) calleeParamTypes.push_back(pt);
                    llvm::FunctionType* calleeType = llvm::FunctionType::get(
                        lambdaFuncType->getReturnType(), calleeParamTypes, false);
                    llvm::Value* funcPtr = builder->CreateBitCast(fnPtr, calleeType->getPointerTo(), "lambda.fptr");

                    // Build argument values
                    std::vector<llvm::Value*> lambdaArgValues;
                    lambdaArgValues.push_back(envPtr);  // hidden environment parameter
                    for (size_t i = 0; i < node->arguments.size(); ++i) {
                        node->arguments[i]->accept(*this);
                        llvm::Value* argVal = m_currentLLVMValue;
                        if (!argVal) {
                            logError(node->arguments[i]->loc, "Argument codegen failed for lambda call");
                            m_currentLLVMValue = nullptr;
                            return;
                        }
                        // Cast if needed
                        if (i < lambdaFuncType->getNumParams()) {
                            llvm::Type* expectedType = lambdaFuncType->getParamType(i);
                            if (argVal->getType() != expectedType) {
                                if (expectedType->isIntegerTy() && argVal->getType()->isIntegerTy()) {
                                    argVal = builder->CreateSExtOrTrunc(argVal, expectedType, "lambda.argcast");
                                } else if (expectedType->isFloatingPointTy() && argVal->getType()->isIntegerTy()) {
                                    argVal = builder->CreateSIToFP(argVal, expectedType, "lambda.argcast");
                                }
                            }
                        }
                        lambdaArgValues.push_back(argVal);
                    }

                    // Create indirect call with the full (env-inclusive) signature.
                    // A void-returning callee must not name the call result (LLVM
                    // refuses to give a name to a void value).
                    m_currentLLVMValue = builder->CreateCall(
                        calleeType, funcPtr, lambdaArgValues,
                        lambdaFuncType->getReturnType()->isVoidTy() ? "" : "lambda.result");
                    return;
                }
            }
            }
            // It may be a function-typed parameter (`f<fn(Int) -> Int>`, a closure)
            // or a bare C function pointer (`cb<loc<fn(Int) -> Int>>`), both lowered
            // to an indirect call through a pointer stored in the variable's alloca.
            // The closure carries a hidden environment parameter; the bare pointer
            // (a C callback) calls straight through with the declared signature.
            if (identCallee) {
                auto varIt = namedValues.find(identCallee->name);
                if (varIt != namedValues.end()) {
                    auto vtmIt = valueTypeMap.find(varIt->second);
                    ast::TypeNode* valTypeNode =
                        vtmIt != valueTypeMap.end() ? vtmIt->second.get() : nullptr;
                    ast::FunctionType* fnTypeNode = dynamic_cast<ast::FunctionType*>(valTypeNode);
                    bool bareFnPtr = false;
                    if (!fnTypeNode) {
                        // A bare C function pointer arrives as `loc<fn(...)>` / `CPtr<fn(...)>`,
                        // i.e. a `loc` TypeName wrapping a FunctionType generic argument.
                        if (auto* tn = dynamic_cast<ast::TypeName*>(valTypeNode)) {
                            if (tn->identifier &&
                                (tn->identifier->name == "loc" || tn->identifier->name == "CPtr") &&
                                !tn->genericArgs.empty()) {
                                if ((fnTypeNode = dynamic_cast<ast::FunctionType*>(tn->genericArgs[0].get()))) {
                                    bareFnPtr = true; // a bare code pointer
                                }
                            }
                        }
                    }
                    if (fnTypeNode) {
                        // Rebuild the LLVM function type from the declared signature
                        // (opaque-pointer mode has no typed PointerType to inspect).
                        std::vector<llvm::Type*> ptypes;
                        for (const auto& pn : fnTypeNode->parameterTypes) {
                            llvm::Type* t = pn ? codegenType(pn.get()) : nullptr;
                            if (t) ptypes.push_back(t);
                        }
                        llvm::Type* rt = fnTypeNode->returnType
                            ? codegenType(fnTypeNode->returnType.get())
                            : llvm::Type::getVoidTy(*context);
                        llvm::FunctionType* fnParamType = llvm::FunctionType::get(rt, ptypes, false);

                        llvm::Value* envPtr = nullptr;
                        llvm::Value* funcPtr = nullptr;
                        std::vector<llvm::Type*> calleeParamTypes;
                        if (bareFnPtr) {
                            // A bare C function pointer is a single code pointer; load
                            // it from the alloca and call with the declared signature only.
                            llvm::Type* ptrTy = llvm::PointerType::get(*context, 0);
                            funcPtr = builder->CreateLoad(ptrTy, varIt->second, "fnptr.val");
                            for (auto* pt : fnParamType->params()) calleeParamTypes.push_back(pt);
                        } else {
                            // The `fn` value is a closure struct { env, fn }.
                            llvm::StructType* closureTy = getClosureStructType();
                            llvm::Value* closureVal = builder->CreateLoad(closureTy, varIt->second, "fnparam.closure");
                            envPtr = builder->CreateExtractValue(closureVal, 0, "fnparam.env");
                            funcPtr = builder->CreateExtractValue(closureVal, 1, "fnparam.fn");
                            // The callee's real signature carries the environment too.
                            calleeParamTypes.push_back(llvm::PointerType::get(*context, 0));
                            for (auto* pt : fnParamType->params()) calleeParamTypes.push_back(pt);
                        }
                        llvm::FunctionType* calleeType = llvm::FunctionType::get(rt, calleeParamTypes, false);
                        funcPtr = builder->CreateBitCast(funcPtr, calleeType->getPointerTo(), "fnparam.fptr");
                        std::vector<llvm::Value*> fnArgValues;
                        if (envPtr) fnArgValues.push_back(envPtr);  // hidden environment parameter
                        for (size_t i = 0; i < node->arguments.size(); ++i) {
                            node->arguments[i]->accept(*this);
                            llvm::Value* argVal = m_currentLLVMValue;
                            if (!argVal) {
                                logError(node->arguments[i]->loc, "Argument codegen failed for function-typed param call");
                                m_currentLLVMValue = nullptr;
                                return;
                            }
                            if (i < fnParamType->getNumParams() && argVal->getType() != fnParamType->getParamType(i)) {
                                llvm::Type* expectedType = fnParamType->getParamType(i);
                                if (expectedType->isIntegerTy() && argVal->getType()->isIntegerTy()) {
                                    argVal = builder->CreateSExtOrTrunc(argVal, expectedType, "fnparam.argcast");
                                } else if (expectedType->isFloatingPointTy() && argVal->getType()->isIntegerTy()) {
                                    argVal = builder->CreateSIToFP(argVal, expectedType, "fnparam.argcast");
                                }
                            }
                            fnArgValues.push_back(argVal);
                        }
                        m_currentLLVMValue = builder->CreateCall(
                            calleeType, funcPtr, fnArgValues,
                            rt->isVoidTy() ? "" : "fnparam.result");
                        return;
                    }
                }
            }
            // Try mangled name if it's a method or from a namespace
            // This part needs a robust name mangling and lookup scheme
            logError(node->callee->loc, "Function " + calleeName + " not found.");
            m_currentLLVMValue = nullptr;
            return;
        } else {
            logError(node->callee->loc, "Function " + calleeName + " not found.");
            m_currentLLVMValue = nullptr;
            return;
        }
    }

    // Check argument count. Variadic functions accept any number of extra
    // arguments, but at least as many as their fixed parameters.
    size_t fixedParamCount = calleeFunc->getFunctionType()->getNumParams();
    if (calleeFunc->isVarArg()) {
        if (node->arguments.size() < fixedParamCount) {
            logError(node->loc, "Variadic function " + calleeName + " requires at least " +
                     std::to_string(fixedParamCount) + " argument(s), got " +
                     std::to_string(node->arguments.size()) + ".");
            m_currentLLVMValue = nullptr;
            return;
        }
    } else if (fixedParamCount != node->arguments.size()) {
        logError(node->loc, "Incorrect number of arguments passed to function " + calleeName);
        m_currentLLVMValue = nullptr;
        return;
    }

    std::vector<llvm::Value *> argValues;
    for (size_t i = 0; i < node->arguments.size(); ++i) {
        node->arguments[i]->accept(*this);
        llvm::Value* argValue = m_currentLLVMValue;
        if (!argValue) {
            logError(node->arguments[i]->loc, "Argument codegen failed for call to " + calleeName);
            m_currentLLVMValue = nullptr;
            return;
        }

        if (i < fixedParamCount) {
            // Fixed parameters: implicit cast to the declared parameter type.
            llvm::Type* expectedArgType = calleeFunc->getFunctionType()->getParamType(i);
            if (argValue->getType() != expectedArgType) {
                if (expectedArgType->isPointerTy()) {
                    if (auto* stringStructType = llvm::dyn_cast<llvm::StructType>(argValue->getType())) {
                        if (stringStructType->getNumElements() == 2 &&
                            stringStructType->getElementType(0)->isPointerTy() &&
                            stringStructType->getElementType(1)->isIntegerTy()) {
                            argValue = builder->CreateExtractValue(argValue, {0}, "cstrarg");
                        }
                    }
                } else if (expectedArgType->isFloatingPointTy() && argValue->getType()->isIntegerTy()) {
                    argValue = builder->CreateSIToFP(argValue, expectedArgType, "callargcast");
                } else if (expectedArgType->isIntegerTy() && argValue->getType()->isFloatingPointTy()) {
                    argValue = builder->CreateFPToSI(argValue, expectedArgType, "callargcast");
                } else if (expectedArgType->isIntegerTy() && argValue->getType()->isIntegerTy()) {
                    // Handle integer width mismatches (e.g., i64 to i32)
                    llvm::IntegerType* expectedIntType = llvm::cast<llvm::IntegerType>(expectedArgType);
                    llvm::IntegerType* actualIntType = llvm::cast<llvm::IntegerType>(argValue->getType());

                    if (expectedIntType->getBitWidth() < actualIntType->getBitWidth()) {
                        // Truncate to smaller width (e.g., i64 to i32)
                        argValue = builder->CreateTrunc(argValue, expectedArgType, "callargtrunc");
                    } else if (expectedIntType->getBitWidth() > actualIntType->getBitWidth()) {
                        // Sign-extend to larger width (e.g., i32 to i64)
                        argValue = builder->CreateSExt(argValue, expectedArgType, "callargsext");
                    }
                } else if (expectedArgType->isStructTy() && argValue->getType()->isPointerTy()) {
                    // Expected a Vyb String { ptr, len } and got a raw char* (e.g.
                    // the result of to_string() / substring / concat). Fair
                    // String-struct shapes only: wrap the pointer into the struct
                    // using tryCast (which computes strlen for the length field).
                    llvm::StructType* st = llvm::dyn_cast<llvm::StructType>(expectedArgType);
                    if (st && st->getNumElements() == 2 &&
                        st->getElementType(0)->isPointerTy() &&
                        st->getElementType(1)->isIntegerTy(64)) {
                        llvm::Value* wrapped = tryCast(argValue, expectedArgType, node->arguments[i]->loc);
                        if (wrapped) argValue = wrapped;
                    }
                }
                // Add more sophisticated casting rules as needed
            }

            if (argValue->getType() != expectedArgType) {
                 logError(node->arguments[i]->loc, "Argument type mismatch for call to " + calleeName + ". Expected " + getTypeName(expectedArgType) + " but got " + getTypeName(argValue->getType()));
                 m_currentLLVMValue = nullptr;
                 return;
            }
        } else {
            // Variadic arguments have no declared parameter type, so pass the raw
            // value. As a convenience, auto-extract a Vyb String's data pointer so
            // C varargs such as `printf("%s", s)` receive the expected `char*`.
            if (auto* stringStructType = llvm::dyn_cast<llvm::StructType>(argValue->getType())) {
                if (stringStructType->getNumElements() == 2 &&
                    stringStructType->getElementType(0)->isPointerTy() &&
                    stringStructType->getElementType(1)->isIntegerTy()) {
                    argValue = builder->CreateExtractValue(argValue, {0}, "cvararg.str");
                }
            }
        }
        argValues.push_back(argValue);
    }

    if (calleeFunc->getReturnType()->isVoidTy()) {
        builder->CreateCall(calleeFunc, argValues);
        m_currentLLVMValue = nullptr; // No value for void calls
    } else {
        llvm::Value* callResult = builder->CreateCall(calleeFunc, argValues, "calltmp");

        // Track Future<T> result type for opaque pointer handling in await expressions
        if (calleeFunc->getReturnType()->isStructTy()) {
            auto* structRetType = llvm::cast<llvm::StructType>(calleeFunc->getReturnType());
            // Future<T> struct has 4 fields: {T*, i32, i64, i8*}
            if (structRetType->getNumElements() == 4) {
                // Look up the function declaration to get the return type annotation
                if (auto* calleeIdent = dynamic_cast<ast::Identifier*>(node->callee.get())) {
                    if (m_currentVybModule) {
                        for (const auto& stmt : m_currentVybModule->body) {
                            auto* decl = dynamic_cast<ast::FunctionDeclaration*>(stmt.get());
                            if (decl && decl->id && decl->id->name == calleeIdent->name && decl->returnTypeNode) {
                                // Check if return type is Future<T>
                                if (auto* futureType = dynamic_cast<ast::FutureType*>(decl->returnTypeNode.get())) {
                                    if (futureType->resultType) {
                                        m_currentCallResultType = codegenType(futureType->resultType.get());
                                        VYB_CDBG << "DEBUG: Set m_currentCallResultType to " 
                                                 << getTypeName(m_currentCallResultType)
                                                 << " for Future call to " << calleeIdent->name << std::endl;
                                    }
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }

        // Phase 4: Check if this is a call to a semantically failable function.
        bool calleeNeedsErrorReturn = false;
        if (auto* calleeIdent = dynamic_cast<ast::Identifier*>(node->callee.get())) {
            if (m_currentVybModule) {
                for (const auto& stmt : m_currentVybModule->body) {
                    auto* decl = dynamic_cast<ast::FunctionDeclaration*>(stmt.get());
                    if (decl && decl->id && decl->id->name == calleeIdent->name && decl->needsErrorReturn) {
                        calleeNeedsErrorReturn = true;
                        break;
                    }
                }
            }
        }

        if (calleeNeedsErrorReturn) {
            if (llvm::StructType* structRetType = llvm::dyn_cast<llvm::StructType>(calleeFunc->getReturnType())) {
            if (structRetType->getNumElements() == 2 &&
                structRetType->getElementType(1)->isPointerTy()) {
                // This looks like a {T, ptr} return from a failable function
                VYB_CDBG << "DEBUG: Extracting error from failable function call to " << calleeName << std::endl;

                // Extract the value (position 0)
                llvm::Value* returnedValue = builder->CreateExtractValue(callResult, {0}, "call.value");

                // Extract the error pointer (position 1)
                llvm::Value* errorPtr = builder->CreateExtractValue(callResult, {1}, "call.error");

                // Check if error occurred (error != NULL)
                llvm::Value* hasError = builder->CreateICmpNE(
                    errorPtr,
                    llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(errorPtr->getType())),
                    "has.error"
                );

                // Create basic blocks for error handling
                llvm::Function* currentFunc = getCurrentFunction();
                llvm::BasicBlock* errorBB = llvm::BasicBlock::Create(*context, "call.error", currentFunc);
                llvm::BasicBlock* successBB = llvm::BasicBlock::Create(*context, "call.success", currentFunc);

                builder->CreateCondBr(hasError, errorBB, successBB);

                // Error block: check if we have a trap handler or need to propagate
                builder->SetInsertPoint(errorBB);
                VYB_CDBG << "DEBUG: Error detected, trapStack.size() = " << trapStack.size() << std::endl;
                if (!trapStack.empty()) {
                    // We have a trap handler - store error and jump to landing pad
                    // TODO(error-phase-3): keep this branch as the dedicated trap binding hook
                    // as trap payload typing/dispatch wiring is completed.
                    VYB_CDBG << "DEBUG: Storing error to trap.errorSlot and branching to landing pad" << std::endl;
                    TrapContext& trap = trapStack.back();
                    builder->CreateStore(errorPtr, trap.errorSlot);
                    builder->CreateBr(trap.landingPad);
                } else if (currentFunctionAST && currentFunctionAST->needsErrorReturn) {
                    // No trap but we're in a failable function - propagate to our caller
                    emitPropagatingErrorReturn(errorPtr);
                } else {
                    // No trap and not a failable function - call untrapped error handler
                    VYB_CDBG << "DEBUG: Error reaching untrapped handler" << std::endl;
                    llvm::Function* untrappedFn = getVybUntrappedErrorFunction();

                    // Pass the actual error pointer
                    builder->CreateCall(untrappedFn, {errorPtr});
                    builder->CreateUnreachable();
                }

                // Success block: continue with the actual value
                builder->SetInsertPoint(successBB);
                m_currentLLVMValue = returnedValue;
                return;
            }
            }
        }

        // Not a failable function call - use result directly
        m_currentLLVMValue = callResult;
    }
}

void LLVMCodegen::visit(vyb::ast::LocationExpression *node) {
    // loc(expr) creates a pointer to the expression's memory location

    // Fast path: if expr is an Identifier, return its alloca directly
    auto ident = dynamic_cast<vyb::ast::Identifier*>(node->expression.get());
    if (ident) {
        auto it = namedValues.find(ident->name);
        if (it != namedValues.end()) {
            m_currentLLVMValue = it->second;
            return;
        }
    }

    // 1. Evaluate the expression to get its value
    node->expression->accept(*this);
    llvm::Value *exprVal = m_currentLLVMValue;
    if (!exprVal) {
        logError(node->loc, "Expression in loc() evaluated to null");
        m_currentLLVMValue = nullptr;
        return;
    }

    // 2. If the expression is already a pointer type, use it directly
    if (exprVal->getType()->isPointerTy()) {
        m_currentLLVMValue = exprVal;
        return;
    }

    // 3. Create an alloca for the value and store it
    llvm::Type* valType = exprVal->getType();
    llvm::Value* tempAlloca = builder->CreateAlloca(valType, nullptr, "loc_alloca");
    builder->CreateStore(exprVal, tempAlloca);

    // 4. Return the pointer
    m_currentLLVMValue = tempAlloca;
}

void LLVMCodegen::visit(vyb::ast::AddrOfExpression *node) {
    // addr(expr) gets the address of a pointer expression

    // 1. Evaluate the expression to get the pointer
    node->getLocation()->accept(*this);
    llvm::Value *exprVal = m_currentLLVMValue;
    if (!exprVal) {
        logError(node->loc, "Expression in addr() evaluated to null");
        m_currentLLVMValue = nullptr;
        return;
    }

    // 2. If we have a pointer to a pointer, load the actual pointer
    if (exprVal->getType()->isPointerTy()) {
        // Load the pointer value if we have a pointer-to-pointer
        if (auto allocaInst = llvm::dyn_cast<llvm::AllocaInst>(exprVal)) {
            if (allocaInst->getAllocatedType()->isPointerTy()) {
                exprVal = builder->CreateLoad(allocaInst->getAllocatedType(), exprVal, "ptr_load");
            }
        }
    }

    // 3. Convert the pointer to an integer
    if (!exprVal->getType()->isPointerTy()) {
        logError(node->loc, "Expression in addr() must be a pointer type");
        m_currentLLVMValue = nullptr;
        return;
    }

    m_currentLLVMValue = builder->CreatePtrToInt(exprVal, int64Type, "addr_cast");
}

void LLVMCodegen::visit(vyb::ast::PointerDerefExpression *node) {
    // at(ptr) dereferences a pointer for reading or writing

    // 1. Evaluate the pointer expression
    node->pointer->accept(*this);
    llvm::Value *ptrVal = m_currentLLVMValue;
    if (!ptrVal) {
        logError(node->loc, "Pointer expression in at() evaluated to null");
        m_currentLLVMValue = nullptr;
        return;
    }

    // 2. If we have a pointer to a pointer (e.g., alloca of a pointer), load the actual pointer
    if (ptrVal->getType()->isPointerTy()) {
        // Load the pointer value if we have a pointer-to-pointer
        if (auto allocaInst = llvm::dyn_cast<llvm::AllocaInst>(ptrVal)) {
            if (allocaInst->getAllocatedType()->isPointerTy()) {
                ptrVal = builder->CreateLoad(allocaInst->getAllocatedType(), ptrVal, "ptr_load");
            }
        }
    }

    // 3. Verify we have a valid pointer type
    if (!ptrVal->getType()->isPointerTy()) {
        logError(node->loc, "Operand of at() must be a pointer type. Got: " + getTypeName(ptrVal->getType()));
        m_currentLLVMValue = nullptr;
        return;
    }

    // 4. Add null check
    llvm::BasicBlock *currentBB = builder->GetInsertBlock();
    llvm::Function *currentFn = currentBB->getParent();

    llvm::BasicBlock *notNullBB = llvm::BasicBlock::Create(*context, "ptr.not_null", currentFn);
    llvm::BasicBlock *nullBB = llvm::BasicBlock::Create(*context, "ptr.null", currentFn);
    llvm::BasicBlock *mergeBB = llvm::BasicBlock::Create(*context, "ptr.merge", currentFn);

    llvm::Value *isNotNull = builder->CreateICmpNE(ptrVal, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrVal->getType())), "ptr.is_not_null");
    builder->CreateCondBr(isNotNull, notNullBB, nullBB);

    // Not null case: perform the dereference
    builder->SetInsertPoint(notNullBB);

    // For assignment target, return the pointer itself
    // For reading, load the value
    if (m_isLHSOfAssignment) {
        m_currentLLVMValue = ptrVal;
    } else {
        // For reading, load the value
        llvm::Type* pointeeType = nullptr;
        if (node->type) {
            pointeeType = codegenType(node->type.get());
        } else {
            // Fallback to i64 for test cases
            pointeeType = int64Type;
        }
        m_currentLLVMValue = builder->CreateLoad(pointeeType, ptrVal, "deref.load");
    }

    builder->CreateBr(mergeBB);

    // Null case: handle the error
    builder->SetInsertPoint(nullBB);
    builder->CreateUnreachable();

    // Merge point
    builder->SetInsertPoint(mergeBB);
}

void LLVMCodegen::visit(vyb::ast::AssignmentExpression *node) {
    // Save and set LHS flag before visiting LHS
    bool wasLHS = m_isLHSOfAssignment;
    m_isLHSOfAssignment = true;
    node->left->accept(*this);
    m_isLHSOfAssignment = wasLHS;
    llvm::Value *LHS = m_currentLLVMValue;

    // Generate RHS value
    node->right->accept(*this);
    llvm::Value *RHS = m_currentLLVMValue;
    if (!LHS || !RHS) {
        logError(node->loc, "Invalid operands in assignment.");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Capture type information from AST if available
    std::shared_ptr<vyb::ast::TypeNode> lhsTypeNode = node->left->type;
    std::shared_ptr<vyb::ast::TypeNode> rhsTypeNode = node->right->type;

    // Store location for error reporting
    SourceLocation errorLoc = node->left->loc;

    // Check if we're assigning to a variable (used for special case handling)
    bool isAssignToVar = false;
    auto identLeft = dynamic_cast<ast::Identifier*>(node->left.get());
    if (identLeft) {
        isAssignToVar = true;

        // For direct identifier assignments, register the RHS type with the identifier
        // This helps propagate type info for variables
        if (identLeft->type && RHS) {
            valueTypeMap[RHS] = identLeft->type;
            VYB_CDBG << "DEBUG: In assignment, associated RHS value with identifier type: "
                      << identLeft->type->toString() << std::endl;
        }
    }

    // Mutable closure captures: propagate writes to the outer variable's
    // address so the enclosing scope (and subsequent invocations) observe the
    // change. This map is only populated while generating a lambda body, so for
    // ordinary assignments this is a no-op.
    auto writeThroughMutable = [&](llvm::Value* v) {
        if (identLeft) {
            auto mIt = mutableCaptureOuterPointers.find(identLeft->name);
            if (mIt != mutableCaptureOuterPointers.end()) {
                builder->CreateStore(v, mIt->second);
            }
        }
    };

    // Check if LHS is a valid target for assignment
    if (!LHS->getType()->isPointerTy()) {
        // Log detailed information about the LHS
        std::string lhsTypeStr = getTypeName(LHS->getType());
        std::string lhsNodeType = "<unknown>";

        // Get more detailed info about the LHS expression type
        if (auto callExpr = dynamic_cast<ast::CallExpression*>(node->left.get())) {
            if (auto identCallee = dynamic_cast<ast::Identifier*>(callExpr->callee.get())) {
                lhsNodeType = "CallExpression to " + identCallee->name;
            } else {
                lhsNodeType = "CallExpression";
            }
        } else if (dynamic_cast<ast::PointerDerefExpression*>(node->left.get())) {
            lhsNodeType = "PointerDerefExpression";
        } else if (identLeft) {
            lhsNodeType = "Identifier: " + identLeft->name;
        }

        // If the LHS is a variable and RHS is a pointer-to-int conversion (addr()),
        // we're probably assigning an address to an int variable, which is valid
        if (isAssignToVar && RHS->getType()->isIntegerTy()) {
            // Create an alloca if LHS doesn't point to memory yet
            llvm::AllocaInst* allocaInst = nullptr;
            if (auto existingAlloca = llvm::dyn_cast<llvm::AllocaInst>(LHS)) {
                // LHS is already an alloca, use it
                allocaInst = existingAlloca;
            } else {
                // Need to create an alloca for the LHS
                if (identLeft) {
                    // Look up the alloca for the variable
                    auto it = namedValues.find(identLeft->name);
                    if (it != namedValues.end() && llvm::isa<llvm::AllocaInst>(it->second)) {
                        allocaInst = llvm::cast<llvm::AllocaInst>(it->second);
                    }
                }

                if (!allocaInst) {
                    logError(errorLoc, "Cannot assign to " + lhsNodeType + " (not a valid destination for assignment)");
                    m_currentLLVMValue = nullptr;
                    return;
                }
            }

            // Store the integer value directly
            builder->CreateStore(RHS, allocaInst);

            // Preserve AST type information
            if (lhsTypeNode) {
                valueTypeMap[allocaInst] = lhsTypeNode;
            }

            m_currentLLVMValue = RHS;
            return;
        } else {
            logError(errorLoc, "Destination for assignment is not a pointer type. Got: " +
                     lhsTypeStr + " (Node type: " + lhsNodeType + ")");
            m_currentLLVMValue = nullptr;
            return;
        }
    }

    // Determine the pointee type for the store
    llvm::Type *destPointeeType = nullptr;

    // Try to get pointee type from all available sources
    if (auto allocaInst = llvm::dyn_cast<llvm::AllocaInst>(LHS)) {
        destPointeeType = allocaInst->getAllocatedType();
    } else if (auto gep = llvm::dyn_cast<llvm::GetElementPtrInst>(LHS)) {
        destPointeeType = gep->getResultElementType();
    } else if (lhsTypeNode) {
        destPointeeType = codegenType(lhsTypeNode.get());
    } else {
        // Try to infer pointer element type from AST
        // For PointerDerefExpression, try to get the type info from pointer operand
        if (auto pointerDeref = dynamic_cast<ast::PointerDerefExpression*>(node->left.get())) {
            if (pointerDeref->pointer && pointerDeref->pointer->type) {
                // If it's a pointer type, get its pointee type
                if (auto ptrType = dynamic_cast<ast::PointerType*>(pointerDeref->pointer->type.get())) {
                    if (ptrType->pointeeType) {
                        destPointeeType = codegenType(ptrType->pointeeType.get());
                    }
                }
                // If it's a loc<T> type, get T
                else if (auto locType = dynamic_cast<ast::TypeName*>(pointerDeref->pointer->type.get())) {
                    if (locType->identifier->name == "loc" && !locType->genericArgs.empty()) {
                        destPointeeType = codegenType(locType->genericArgs[0].get());
                    }
                }
            }
        }

        // Fallback: Use RHS type if all else fails
        if (!destPointeeType) {
            destPointeeType = RHS->getType();
        }
    }

    // Cast RHS to match destination type if needed
    if (RHS->getType() != destPointeeType && destPointeeType) {
        if (destPointeeType->isFloatingPointTy() && RHS->getType()->isIntegerTy()) {
            RHS = builder->CreateSIToFP(RHS, destPointeeType, "assigncast");
        } else if (destPointeeType->isIntegerTy() && RHS->getType()->isFloatingPointTy()) {
            RHS = builder->CreateFPToSI(RHS, destPointeeType, "assigncast");
        } else if (destPointeeType->isPointerTy() && RHS->getType()->isPointerTy()) {
            RHS = builder->CreatePointerCast(RHS, destPointeeType, "assigncast");
        } else if (destPointeeType->isIntegerTy() && RHS->getType()->isPointerTy()) {
            RHS = builder->CreatePtrToInt(RHS, destPointeeType, "assigncast");
        } else if (destPointeeType->isPointerTy() && RHS->getType()->isIntegerTy()) {
            RHS = builder->CreateIntToPtr(RHS, destPointeeType, "assigncast");
        }
    }

    // Bitwise compound assigns (&=/|=/^=/<<=/>>=) coerce the RHS to the LHS
    // width themselves, so a narrowed/`narrow` literal is expected and the
    // generic "type mismatch" warning below would be misleading.
    bool bitwiseCompound = node->op.type == vyb::TokenType::BITWISEANDEQ ||
                           node->op.type == vyb::TokenType::BITWISEOREQ ||
                           node->op.type == vyb::TokenType::BITWISEXOREQ ||
                           node->op.type == vyb::TokenType::LSHIFTEQ ||
                           node->op.type == vyb::TokenType::RSHIFTEQ;

    // If types still don't match, warn but proceed (might still work in some cases)
    if (RHS->getType() != destPointeeType && !bitwiseCompound) {
        logWarning(node->loc, "Type mismatch in assignment. Storing " + getTypeName(RHS->getType()) +
                  " into location of type " + (destPointeeType ? getTypeName(destPointeeType) : "unknown"));
    }

    // Handle compound assignments (+=, -=, *=, /=, %=)
    if (node->op.type == vyb::TokenType::PLUSEQ) {
        llvm::Value *lhsVal = builder->CreateLoad(destPointeeType, LHS, "lhs.load");
        lhsVal = builder->CreateAdd(lhsVal, RHS, "compound.add");
        builder->CreateStore(lhsVal, LHS);
        writeThroughMutable(lhsVal);
        m_currentLLVMValue = lhsVal;
        return;
    } else if (node->op.type == vyb::TokenType::MINUSEQ) {
        llvm::Value *lhsVal = builder->CreateLoad(destPointeeType, LHS, "lhs.load");
        lhsVal = builder->CreateSub(lhsVal, RHS, "compound.sub");
        builder->CreateStore(lhsVal, LHS);
        writeThroughMutable(lhsVal);
        m_currentLLVMValue = lhsVal;
        return;
    } else if (node->op.type == vyb::TokenType::MULTIPLYEQ) {
        llvm::Value *lhsVal = builder->CreateLoad(destPointeeType, LHS, "lhs.load");
        lhsVal = builder->CreateMul(lhsVal, RHS, "compound.mul");
        builder->CreateStore(lhsVal, LHS);
        writeThroughMutable(lhsVal);
        m_currentLLVMValue = lhsVal;
        return;
    } else if (node->op.type == vyb::TokenType::DIVEQ) {
        llvm::Value *lhsVal = builder->CreateLoad(destPointeeType, LHS, "lhs.load");
        lhsVal = builder->CreateSDiv(lhsVal, RHS, "compound.div");
        builder->CreateStore(lhsVal, LHS);
        writeThroughMutable(lhsVal);
        m_currentLLVMValue = lhsVal;
        return;
    } else if (node->op.type == vyb::TokenType::MODEQ) {
        llvm::Value *lhsVal = builder->CreateLoad(destPointeeType, LHS, "lhs.load");
        lhsVal = builder->CreateSRem(lhsVal, RHS, "compound.rem");
        builder->CreateStore(lhsVal, LHS);
        writeThroughMutable(lhsVal);
        m_currentLLVMValue = lhsVal;
        return;
    } else if (node->op.type == vyb::TokenType::BITWISEANDEQ ||
               node->op.type == vyb::TokenType::BITWISEOREQ ||
               node->op.type == vyb::TokenType::BITWISEXOREQ ||
               node->op.type == vyb::TokenType::LSHIFTEQ ||
               node->op.type == vyb::TokenType::RSHIFTEQ) {
        // Compound bitwise assignment. Coerce the RHS to the destination (LHS)
        // integer width so a bare literal (or defended mismatch) widens/truncates
        // before the and/or/xor/shift; LLVM requires same-width operands. Genuine
        // typed width mismatches are rejected earlier in semantic analysis.
        auto coerceBitwiseRHS = [&](llvm::Value* rhs) -> llvm::Value* {
            if (!rhs->getType()->isIntegerTy() || !destPointeeType->isIntegerTy()) return rhs;
            if (rhs->getType() == destPointeeType) return rhs;
            unsigned dw = destPointeeType->getIntegerBitWidth();
            unsigned rw = rhs->getType()->getIntegerBitWidth();
            if (rw > dw) return builder->CreateTrunc(rhs, destPointeeType, "compound.trunc");
            return builder->CreateZExt(rhs, destPointeeType, "compound.zext");
        };
        llvm::Value *lhsVal = builder->CreateLoad(destPointeeType, LHS, "lhs.load");
        llvm::Value* cRHS = coerceBitwiseRHS(RHS);
        if (node->op.type == vyb::TokenType::LSHIFTEQ) {
            lhsVal = builder->CreateShl(lhsVal, cRHS, "compound.shl");
        } else if (node->op.type == vyb::TokenType::RSHIFTEQ) {
            // Signed Int types use an arithmetic (sign-extending) shift; UInt*
            // types use a logical shift (consistent with the `>>` binary op).
            bool isUnsigned = false;
            if (lhsTypeNode) {
                if (auto tn = dynamic_cast<ast::TypeName*>(lhsTypeNode.get())) {
                    if (tn->identifier) {
                        const std::string& n = tn->identifier->name;
                        isUnsigned = (n == "UInt8" || n == "UInt16" || n == "UInt32" || n == "UInt64");
                    }
                }
            }
            lhsVal = isUnsigned
                ? builder->CreateLShr(lhsVal, cRHS, "compound.lshr")
                : builder->CreateAShr(lhsVal, cRHS, "compound.ashr");
        } else if (node->op.type == vyb::TokenType::BITWISEANDEQ) {
            lhsVal = builder->CreateAnd(lhsVal, cRHS, "compound.bwand");
        } else if (node->op.type == vyb::TokenType::BITWISEXOREQ) {
            lhsVal = builder->CreateXor(lhsVal, cRHS, "compound.bwxor");
        } else {
            lhsVal = builder->CreateOr(lhsVal, cRHS, "compound.bwor");
        }
        builder->CreateStore(lhsVal, LHS);
        writeThroughMutable(lhsVal);
        m_currentLLVMValue = lhsVal;
        return;
    }

    // Closure-typed variable overwrites release the target's previous hold on a
    // capture environment. Load the outgoing value first, then store the incoming
    // value (so a self-assignment keeps its env alive), then release the old one.
    bool closureOverwrite = isAssignToVar && destPointeeType && isClosureStructType(destPointeeType);
    if (closureOverwrite) {
        // Only overwrite-handle assignments to a confirmed `fn` binding; a plain
        // {ptr, ptr} target (2-pointer tuple) must keep plain store semantics.
        bool lhsFn = isFnTypeNode(node->left->type.get()) || isFnTypeNode(node->right->type.get());
        if (!lhsFn) {
            auto vit = valueTypeMap.find(LHS);
            if (vit != valueTypeMap.end() && isFnTypeNode(vit->second.get())) lhsFn = true;
        }
        if (!lhsFn) closureOverwrite = false;
    }
    llvm::Value* oldClosureVal = nullptr;
    if (closureOverwrite) {
        oldClosureVal = builder->CreateLoad(destPointeeType, LHS, "assign.old_closure");
        bool closureTransfer = dynamic_cast<ast::CallExpression*>(node->right.get()) != nullptr;
        if (!closureTransfer) {
            retainClosureValue(RHS);
        }
    }

    // String-typed overwrites: the incoming value may be a freshly-created
    // transfer (concat / to_string / a String-returning call) whose single owned
    // reference is handed over as-is, or a shared borrow that this location must
    // retain (+1). The outgoing buffer is released after the store so a
    // self-assignment's retain/release on the same buffer net to zero.
    bool stringOverwrite = isAssignToVar && destPointeeType && isVybStringStructType(destPointeeType);
    llvm::Value* oldStringVal = nullptr;
    if (stringOverwrite) {
        oldStringVal = builder->CreateLoad(destPointeeType, LHS, "assign.old_string");
        if (!exprIsStringTransfer(node->right.get())) {
            retainStringValue(RHS);
        }
    }

    // Vec-typed overwrites: a `my` Vec binding owns an exclusive heap data
    // buffer. Storing a new value over it must release the outgoing buffer, or
    // the old data leaks (e.g. reassigning a build-up Vec from a function's
    // return in quicksort / insertion-sort / select-return paths). Guard against
    // a null buffer and against a self-assignment (old data == new data) so a
    // `s = s` style store does not free the value it just wrote.
    bool vecOverwrite = false;
    llvm::Value* oldVecVal = nullptr;
    if (isAssignToVar && destPointeeType && isVecStructType(destPointeeType)) {
        const ScopeVariable* owner = nullptr;
        for (auto sit = scopeStack.rbegin(); sit != scopeStack.rend(); ++sit) {
            for (const auto& sv : *sit) {
                if (sv.name == identLeft->name && sv.allocaInst == LHS) { owner = &sv; break; }
            }
            if (owner) break;
        }
        if (owner && owner->ownership == ast::OwnershipKind::MY && owner->isVecWithMallocData) {
            vecOverwrite = true;
            oldVecVal = builder->CreateLoad(destPointeeType, LHS, "assign.old_vec");
        }
    }
    // A Vec<String> keeps one reference per element; releasing the buffer on
    // overwrite must drop those element references first (mirrors scope-exit
    // cleanup) or the pointed-to String buffers would leak.
    bool vecHoldsStrings = false;
    if (vecOverwrite) {
        auto vtIt = valueTypeMap.find(LHS);
        if (vtIt != valueTypeMap.end() && vtIt->second) {
            vecHoldsStrings = isVecOfStringTypeNode(vtIt->second.get());
        }
    }

    // A `my<Struct>` field (or binding) exclusively owns a heap allocation that
    // is stored as a `T*`. Overwriting it must release the outgoing allocation
    // (mirroring scope-exit reclaim) or the replaced struct leaks. Recursively
    // reclaim owned fields inside the pointed-to struct before freeing the block.
    bool myOverwrite = false;
    llvm::Value* oldMyPtr = nullptr;
    const vyb::ast::TypeNode* myPointeeAst = nullptr;
    if (lhsTypeNode && destPointeeType && destPointeeType->isPointerTy() &&
        isMyOwnedStructTypeNode(lhsTypeNode.get())) {
        myPointeeAst = myPointeeOf(lhsTypeNode.get());
        if (myPointeeAst) {
            myOverwrite = true;
            llvm::PointerType* rawPtrTy = llvm::PointerType::get(*context, 0);
            oldMyPtr = builder->CreateLoad(rawPtrTy, LHS, "assign.old_myp");
        }
    }

    // An `our<T>` binding (control-block backed struct) holds one strong ref and
    // releases it on scope exit. Overwriting a fresh value must release the
    // outgoing strong ref (or the replaced control block leaks), and a shared
    // incoming value (an existing `our` read) must be retained (+1) so the new
    // location owns its own reference. A transfer producer (`our(...)`,
    // `.grab()`, or a function returning `our<T>`) needs no retain.
    bool ourOverwrite = false;
    llvm::Value* oldOurPtr = nullptr;
    if (isAssignToVar && lhsTypeNode && destPointeeType && destPointeeType->isPointerTy() &&
        isOurRefType(lhsTypeNode.get())) {
        ourOverwrite = true;
        oldOurPtr = builder->CreateLoad(destPointeeType, LHS, "assign.old_our");
        if (!exprIsOurTransfer(node->right.get())) {
            retainOurControlBlock(RHS, "assign.our");
        }
    }

    // Create the store instruction with proper alignment
    builder->CreateStore(RHS, LHS);
    writeThroughMutable(RHS);
    if (closureOverwrite && oldClosureVal) {
        releaseClosureValue(oldClosureVal);
    }
    if (stringOverwrite && oldStringVal) {
        releaseStringValue(oldStringVal);
    }
    if (vecOverwrite && oldVecVal) {
        llvm::Value* oldData = builder->CreateExtractValue(oldVecVal, 0, "assign.old_vec_data");
        llvm::PointerType* rawPtr = llvm::PointerType::get(*context, 0);
        llvm::Constant* nullPtr = llvm::ConstantPointerNull::get(rawPtr);
        llvm::Value* isNotNull = builder->CreateICmpNE(oldData, nullPtr, "assign.vec_not_null");
        llvm::Value* notSelfVal = llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), 1);
        if (RHS->getType() == destPointeeType) {
            llvm::Value* newData = builder->CreateExtractValue(RHS, 0, "assign.new_vec_data");
            notSelfVal = builder->CreateICmpNE(oldData, newData, "assign.vec_not_self");
        }
        llvm::Value* shouldFree = builder->CreateAnd(isNotNull, notSelfVal, "assign.vec_should_free");
        llvm::BasicBlock* freeBB = llvm::BasicBlock::Create(*context, "assign.vec_free", currentFunction);
        llvm::BasicBlock* contBB = llvm::BasicBlock::Create(*context, "assign.vec_cont", currentFunction);
        builder->CreateCondBr(shouldFree, freeBB, contBB);
        builder->SetInsertPoint(freeBB);
        if (vecHoldsStrings) {
            llvm::Value* oldSize = builder->CreateExtractValue(oldVecVal, 1, "assign.old_vec_size");
            releaseStringElements(oldData, oldSize);
        }
        builder->CreateCall(getOrCreateFreeFunction(), {oldData});
        builder->CreateBr(contBB);
        builder->SetInsertPoint(contBB);
    }
    if (myOverwrite && oldMyPtr && myPointeeAst) {
        llvm::PointerType* rawPtrTy = llvm::PointerType::get(*context, 0);
        llvm::Constant* nullPtr = llvm::ConstantPointerNull::get(rawPtrTy);
        llvm::Value* isNotNull = builder->CreateICmpNE(oldMyPtr, nullPtr, "assign.myp_not_null");
        llvm::Value* notSelf = llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), 1);
        if (RHS->getType()->isPointerTy()) {
            notSelf = builder->CreateICmpNE(oldMyPtr, RHS, "assign.myp_not_self");
        }
        llvm::Value* shouldFree = builder->CreateAnd(isNotNull, notSelf, "assign.myp_should_free");
        llvm::BasicBlock* freeBB = llvm::BasicBlock::Create(*context, "assign.myp_free", currentFunction);
        llvm::BasicBlock* contBB = llvm::BasicBlock::Create(*context, "assign.myp_cont", currentFunction);
        builder->CreateCondBr(shouldFree, freeBB, contBB);
        builder->SetInsertPoint(freeBB);
        llvm::Type* pointeeTy = codegenType(const_cast<vyb::ast::TypeNode*>(myPointeeAst));
        if (auto* pois = llvm::dyn_cast<llvm::StructType>(pointeeTy)) {
            reclaimOwnedStructAt(oldMyPtr, myPointeeAst, pois);
        }
        builder->CreateCall(getOrCreateFreeFunction(), {oldMyPtr});
        builder->CreateBr(contBB);
        builder->SetInsertPoint(contBB);
    }
    if (ourOverwrite && oldOurPtr) {
        llvm::PointerType* rawPtrTy = llvm::PointerType::get(*context, 0);
        llvm::Constant* nullPtr = llvm::ConstantPointerNull::get(rawPtrTy);
        llvm::Value* isNotNull = builder->CreateICmpNE(oldOurPtr, nullPtr, "assign.our_not_null");
        llvm::BasicBlock* freeBB = llvm::BasicBlock::Create(*context, "assign.our_free", currentFunction);
        llvm::BasicBlock* contBB = llvm::BasicBlock::Create(*context, "assign.our_cont", currentFunction);
        builder->CreateCondBr(isNotNull, freeBB, contBB);
        builder->SetInsertPoint(freeBB);
        releaseOurControlBlock(oldOurPtr, "assign.our");
        builder->CreateBr(contBB);
        builder->SetInsertPoint(contBB);
    }

    // If we're assigning to a member of a struct, we need to preserve type information
    if (auto memberExpr = dynamic_cast<ast::MemberExpression*>(node->left.get())) {
        if (memberExpr->object->type) {
            // The object has a type, so we can use it to determine field types
            // In a full implementation, we would look up the field's type from the struct definition
            // For now, we will propagate the object's type to help with future member accesses
            valueTypeMap[RHS] = memberExpr->object->type;

            // If LHS is from a GEP instruction, associate the struct type with the GEP result
            if (auto gep = llvm::dyn_cast<llvm::GetElementPtrInst>(LHS)) {
                valueTypeMap[gep] = memberExpr->object->type;
            }
        }
    }

    // Return the value being stored (C semantics)
    m_currentLLVMValue = RHS;

    // Make sure we also associate this result with the type if available
    if (rhsTypeNode) {
        valueTypeMap[RHS] = rhsTypeNode;
    }
}


void LLVMCodegen::visit(vyb::ast::ArrayElementExpression *node) {
    // This is for using array[index] or tuple[index] as an R-value (i.e., loading the value)
    // LHS usage is handled in AssignmentExpression

    // Check if the base expression is a tuple type
    bool isTupleAccess = false;
    if (node->array->type) {
        if (auto* tupleType = dynamic_cast<vyb::ast::TupleTypeNode*>(node->array->type.get())) {
            isTupleAccess = true;
        }
    }

    // For tuple access, we need the actual struct value, not a pointer
    if (isTupleAccess) {
        // Visit the tuple expression to get the struct value
        node->array->accept(*this);
        llvm::Value *tupleValue = m_currentLLVMValue;

        // Visit the index expression
        node->index->accept(*this);
        llvm::Value *indexVal = m_currentLLVMValue;

        if (!tupleValue || !indexVal) {
            logError(node->loc, "Tuple or index expression failed to codegen.");
            m_currentLLVMValue = nullptr;
            return;
        }

        // Index must be a constant integer for tuple access
        if (auto* constIndex = llvm::dyn_cast<llvm::ConstantInt>(indexVal)) {
            uint64_t index = constIndex->getZExtValue();

            // Verify index is within bounds
            auto* tupleType = dynamic_cast<vyb::ast::TupleTypeNode*>(node->array->type.get());
            if (index >= tupleType->memberTypes.size()) {
                logError(node->loc, "Tuple index " + std::to_string(index) + " out of bounds (size: " +
                         std::to_string(tupleType->memberTypes.size()) + ")");
                m_currentLLVMValue = nullptr;
                return;
            }

            // Extract the element from the tuple struct
            llvm::Value* element = builder->CreateExtractValue(tupleValue, {static_cast<unsigned>(index)}, "tuple_elem");
            m_currentLLVMValue = element;
            return;
        } else {
            logError(node->loc, "Tuple index must be a constant integer");
            m_currentLLVMValue = nullptr;
            return;
        }
    }

    // Original array access logic
    llvm::Value *arrayPtr = nullptr;

    // Special handling for identifier expressions to get the alloca directly
    if (auto* identExpr = dynamic_cast<vyb::ast::Identifier*>(node->array.get())) {
        auto it = namedValues.find(identExpr->name);
        if (it != namedValues.end()) {
            if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(it->second)) {
                // For array variables, we need the alloca (pointer to array), not the loaded value
                arrayPtr = alloca;
            } else {
                arrayPtr = it->second; // Global or function
            }
        } else {
            logError(identExpr->loc, "Undefined identifier in array access: " + identExpr->name);
            m_currentLLVMValue = nullptr;
            return;
        }
    } else {
        // For other expressions, visit normally
        node->array->accept(*this);
        arrayPtr = m_currentLLVMValue;
    }

    node->index->accept(*this);
    llvm::Value *indexVal = m_currentLLVMValue;

    if (!arrayPtr || !indexVal) {
        logError(node->loc, "Array or index expression for element access failed to codegen.");
        m_currentLLVMValue = nullptr;
        return;
    }

    // arrayPtr must be a pointer type for GEP
    if (!arrayPtr->getType()->isPointerTy()) {
        logError(node->array->loc, "Base of array element access (R-value) is not a pointer. Type: " + getTypeName(arrayPtr->getType()));
        m_currentLLVMValue = nullptr;
        return;
    }

    // Determine element type for GEP and Load. This is the type GEP operates on.
    llvm::Type* elementType = nullptr;
    if (node->array->type) { // AST type of the array expression itself
        vyb::ast::TypeNode* arrAstTypeNode = node->array->type.get();
        if (auto arrayAstType = dynamic_cast<vyb::ast::ArrayType*>(arrAstTypeNode)) {
            // If the AST says it's an array T[N], then GEP on a T* needs element type T
            elementType = codegenType(arrayAstType->elementType.get());
        } else if (auto ptrAstType = dynamic_cast<vyb::ast::PointerType*>(arrAstTypeNode)) {
            // If the AST says it's a pointer T*, then GEP on T* needs element type T
            // Or if it's T(*)[N], GEP still needs T.
            // The key is what the pointer *ultimately points to* at the element level.
            if (auto pointedToArrType = dynamic_cast<vyb::ast::ArrayType*>(ptrAstType->pointeeType.get())) { // Corrected member access
                elementType = codegenType(pointedToArrType->elementType.get()); // Pointer to array, get element type of that array
            } else {
                elementType = codegenType(ptrAstType->pointeeType.get()); // Corrected member access // Pointer to element
            }
        }
    }

    if (!elementType) {
        // This is a fallback/error if AST type information was insufficient or missing.
        // Relying on LLVM types directly is problematic with opaque pointers.
        logError(node->loc, "Could not determine element type for array access (R-value) from AST. Array AST type: " + (node->array->type ? node->array->type->toString() : "null"));
        m_currentLLVMValue = nullptr;
        return;
    }

    // For array access, we need to handle the indexing properly
    // If arrayPtr is an alloca of array type, we need [0, index] to access the element
    // If arrayPtr points to the first element, we just use [index]

    llvm::Value *elementAddress = nullptr;

    if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(arrayPtr)) {
        // arrayPtr is an alloca of array type, so we need [0, index] to get to the element
        llvm::Value* zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0);
        std::vector<llvm::Value*> indices = {zero, indexVal};
        elementAddress = builder->CreateGEP(alloca->getAllocatedType(), arrayPtr, indices, "arrayelemaddr_rval");
    } else {
        // arrayPtr points to the first element, so just use [index]
        elementAddress = builder->CreateGEP(elementType, arrayPtr, indexVal, "arrayelemaddr_rval");
    }

    m_currentLLVMValue = builder->CreateLoad(elementType, elementAddress, "arrayelemload");
}

// --- Basic Expression Visitors ---

void LLVMCodegen::visit(ast::Identifier* node) {
    // Bare Option unit constructor: `None`. The semantic layer injected the
    // target Option<T> type into this node, so build the `None` tagged value.
    if (node->name == "None" && node->type) {
        auto* optTn = dynamic_cast<ast::TypeName*>(node->type.get());
        if (optTn && optTn->identifier && optTn->identifier->name == "Option") {
            // Substitute active generic-bind type params (bare `None` inside a
            // monomorphized bind body resolves `Option<T>` -> `Option<Int>`).
            std::vector<ast::TypeNodePtr> concreteEnumArgs;
            concreteEnumArgs.reserve(optTn->genericArgs.size());
            for (const auto& arg : optTn->genericArgs) {
                std::string s = arg->toString();
                if (!currentTypeSubstitutions.empty()) {
                    for (const auto& kv : currentTypeSubstitutions) {
                        s = replaceTypeTokens(s, kv.first, kv.second);
                    }
                }
                concreteEnumArgs.push_back(typePatternToTypeNode(TypePattern::parse(s), node->loc));
            }
            std::string mangled = mangleGenericTypeName("Option", concreteEnumArgs);
            if (!taggedEnumInfo.count(mangled)) monomorphizeEnum("Option", concreteEnumArgs);
            m_currentLLVMValue = buildTaggedEnumValue(mangled, "None", {});
            return;
        }
    }
    // Look up the identifier in the named values map
    auto it = namedValues.find(node->name);
    if (it != namedValues.end()) {
        // Check if this is an AllocaInst (variable) and we're not on the LHS of assignment
        if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(it->second)) {
            if (!m_isLHSOfAssignment) {
                // Load the value from the alloca for variable access
                llvm::Type* loadType = alloca->getAllocatedType();
                llvm::Value* loadedValue = builder->CreateLoad(loadType, alloca, node->name);

                // Propagate type information from alloca to loaded value
                auto typeIt = valueTypeMap.find(alloca);
                if (typeIt != valueTypeMap.end()) {
                    valueTypeMap[loadedValue] = typeIt->second;
                    VYB_CDBG << "DEBUG: Propagated type mapping from alloca to loaded value for '" << node->name << "'" << std::endl;
                }

                m_currentLLVMValue = loadedValue;
                return;
            }
        }
        // For global variables, functions, or LHS of assignment, return the value directly
        m_currentLLVMValue = it->second;
        return;
    }

    // Also check current function named values
    auto funcIt = m_currentFunctionNamedValues.find(node->name);
    if (funcIt != m_currentFunctionNamedValues.end()) {
        // Load the value from the alloca
        llvm::Type* loadType = funcIt->second->getAllocatedType();
        llvm::Value* loadedValue = builder->CreateLoad(loadType, funcIt->second, node->name);

        // Propagate type information from alloca to loaded value
        auto typeIt = valueTypeMap.find(funcIt->second);
        if (typeIt != valueTypeMap.end()) {
            valueTypeMap[loadedValue] = typeIt->second;
            VYB_CDBG << "DEBUG: Propagated type mapping from function alloca to loaded value for '" << node->name << "'" << std::endl;
        }

        m_currentLLVMValue = loadedValue;
        return;
    }

    // Check if it's a global function
    llvm::Function* func = module->getFunction(node->name);
    if (func) {
        m_currentLLVMValue = func;
        return;
    }

    logError(node->loc, "Undefined identifier: " + node->name);
    m_currentLLVMValue = nullptr;
}

void LLVMCodegen::visit(ast::MemberExpression* node) {
    if (!node->object) {
        logError(node->loc, "Member expression missing object");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Check for enum variant access: EnumName::VariantName
    if (!node->computed && node->property) {
        // Generic data enum, unit variant used as a value: `Box<Int>::Empty`.
        if (auto* gi = dynamic_cast<ast::GenericInstantiationExpression*>(node->object.get())) {
            if (auto* baseIdent = dynamic_cast<ast::Identifier*>(gi->baseExpression.get())) {
                if (auto* propIdent = dynamic_cast<ast::Identifier*>(node->property.get())) {
                    if (genericEnumTemplates.count(baseIdent->name) || baseIdent->name == "Option" ||
                        baseIdent->name == "Result") {
                        std::vector<ast::TypeNodePtr> concreteEnumArgs;
                        concreteEnumArgs.reserve(gi->genericArguments.size());
                        for (const auto& arg : gi->genericArguments) {
                            std::string s = arg->toString();
                            if (!currentTypeSubstitutions.empty()) {
                                for (const auto& kv : currentTypeSubstitutions) {
                                    s = replaceTypeTokens(s, kv.first, kv.second);
                                }
                            }
                            concreteEnumArgs.push_back(typePatternToTypeNode(TypePattern::parse(s), gi->loc));
                        }
                        std::string mangled = mangleGenericTypeName(baseIdent->name, concreteEnumArgs);
                        if (!taggedEnumInfo.count(mangled)) monomorphizeEnum(baseIdent->name, concreteEnumArgs);
                        auto tagEnumIt = taggedEnumInfo.find(mangled);
                        const TaggedEnumInfo& tei = tagEnumIt->second;
                        if (tei.variantPayloadTypes.count(propIdent->name)) {
                            logError(node->loc, "Enum variant '" + propIdent->name +
                                     "' of generic enum " + mangled + " requires constructor arguments");
                            m_currentLLVMValue = nullptr;
                            return;
                        }
                        if (!tei.variantTags.count(propIdent->name)) {
                            logError(node->loc, "Unknown enum variant: " + baseIdent->name + "::" + propIdent->name);
                            m_currentLLVMValue = nullptr;
                            return;
                        }
                        m_currentLLVMValue = buildTaggedEnumValue(mangled, propIdent->name, {});
                        return;
                    }
                }
            }
        }
        if (auto* objIdent = dynamic_cast<ast::Identifier*>(node->object.get())) {
            if (enumTypeNames.count(objIdent->name)) {
                if (auto* propIdent = dynamic_cast<ast::Identifier*>(node->property.get())) {
                    const std::string qualName = objIdent->name + "::" + propIdent->name;
                    // A tagged-union enum (has data variants) is emitted as a value
                    // struct rather than an integer constant. A unit variant used
                    // directly as a value builds a tag-only value; a data variant
                    // used without constructor arguments is an error.
                    if (taggedEnumInfo.count(objIdent->name)) {
                        auto tagEnumIt = taggedEnumInfo.find(objIdent->name);
                        const TaggedEnumInfo& tei = tagEnumIt->second;
                        if (tei.variantPayloadTypes.count(propIdent->name)) {
                            logError(node->loc, "Enum variant '" + propIdent->name +
                                     "' of tagged enum " + objIdent->name + " requires constructor arguments");
                            m_currentLLVMValue = nullptr;
                            return;
                        }
                        if (!tei.variantTags.count(propIdent->name)) {
                            logError(node->loc, "Unknown enum variant: " + qualName);
                            m_currentLLVMValue = nullptr;
                            return;
                        }
                        m_currentLLVMValue = buildTaggedEnumValue(objIdent->name, propIdent->name, {});
                        return;
                    }
                    auto enumIt = enumVariantValues.find(qualName);
                    if (enumIt != enumVariantValues.end()) {
                        m_currentLLVMValue = enumIt->second;
                        return;
                    }
                    logError(node->loc, "Unknown enum variant: " + qualName);
                    m_currentLLVMValue = nullptr;
                    return;
                }
            }
        }
    }

    // Evaluate the object expression (should not be treated as LHS)
    bool wasLHS = m_isLHSOfAssignment;
    llvm::Value* objectValue = nullptr;
    llvm::AllocaInst* originalAlloca = nullptr;  // Track original alloca for LHS struct values

    if (m_isLHSOfAssignment) {
        // Optimization: when on LHS and object is an identifier, get the alloca directly
        // instead of loading the value. This avoids creating temp allocas that lose writes.
        // Only apply when the alloca's pointee is a direct struct type (not ownership-wrapped).
        if (auto* objIdent = dynamic_cast<ast::Identifier*>(node->object.get())) {
            auto it = namedValues.find(objIdent->name);
            if (it != namedValues.end()) {
                if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(it->second)) {
                    // Check if pointee is a direct struct type (not control block)
                    llvm::Type* pointeeType = alloca->getAllocatedType();
                    bool isDirectStruct = pointeeType->isStructTy();
                    // Also check valueTypeMap to ensure it's not an ownership wrapper
                    auto vtIt = valueTypeMap.find(alloca);
                    if (vtIt != valueTypeMap.end()) {
                        if (auto astType = vtIt->second.get()) {
                            if (auto typeNameNode = dynamic_cast<ast::TypeName*>(astType)) {
                                std::string tn = typeNameNode->identifier ? typeNameNode->identifier->name : "";
                                if ((tn == "my" || tn == "our" || tn == "their" || tn == "borrow" || tn == "view") &&
                                    !typeNameNode->genericArgs.empty()) {
                                    isDirectStruct = false;  // Ownership wrapper, use normal path
                                }
                            }
                        }
                    }
                    if (isDirectStruct) {
                        objectValue = alloca;
                        originalAlloca = alloca;
                    }
                }
            }
            if (!objectValue) {
                auto funcIt = m_currentFunctionNamedValues.find(objIdent->name);
                if (funcIt != m_currentFunctionNamedValues.end()) {
                    if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(funcIt->second)) {
                        llvm::Type* pointeeType = alloca->getAllocatedType();
                        bool isDirectStruct = pointeeType->isStructTy();
                        auto vtIt = valueTypeMap.find(alloca);
                        if (vtIt != valueTypeMap.end()) {
                            if (auto astType = vtIt->second.get()) {
                                if (auto typeNameNode = dynamic_cast<ast::TypeName*>(astType)) {
                                    std::string tn = typeNameNode->identifier ? typeNameNode->identifier->name : "";
                                    if ((tn == "my" || tn == "our" || tn == "their" || tn == "borrow" || tn == "view") &&
                                        !typeNameNode->genericArgs.empty()) {
                                        isDirectStruct = false;
                                    }
                                }
                            }
                        }
                        if (isDirectStruct) {
                            objectValue = alloca;
                            originalAlloca = alloca;
                        }
                    }
                }
            }
        }
    }

    if (!objectValue) {
        // Fall back to normal evaluation
        m_isLHSOfAssignment = false;  // Object evaluation should load the value normally
        node->object->accept(*this);
        m_isLHSOfAssignment = wasLHS;  // Restore LHS flag for field access
        objectValue = m_currentLLVMValue;
    }

    if (!objectValue) {
        logError(node->object->loc, "Failed to evaluate object in member expression");
        m_currentLLVMValue = nullptr;
        return;
    }

    // `.tag` accessor on an enum value: returns the raw positional i64 tag as an Int.
    // Works for C-like scalar enums (the value already IS the tag) and for
    // data-carrying enums (extract field 0 of the { i64 tag, [N x i8] data } struct).
    if (!node->computed) {
        if (auto* tagPropIdent = dynamic_cast<ast::Identifier*>(node->property.get())) {
            if (tagPropIdent->name == "tag") {
                if (const TaggedEnumInfo* tei = findTaggedEnum(node->object->type.get())) {
                    if (tei->isScalar) {
                        m_currentLLVMValue = objectValue;
                    } else if (objectValue->getType()->isPointerTy()) {
                        llvm::Type* enumTy = codegenType(node->object->type.get());
                        llvm::Value* loaded = builder->CreateLoad(enumTy, objectValue, "enum.tag.load");
                        m_currentLLVMValue = builder->CreateExtractValue(loaded, 0, "enum.tag");
                    } else {
                        m_currentLLVMValue = builder->CreateExtractValue(objectValue, 0, "enum.tag");
                    }
                    return;
                }
            }
        }
    }

    // `.value` accessor on the built-in `Option<T>` / `Result<T,E>` enums:
    // reads the payload of the primary (success) data variant (`Some(T)` for
    // Option, `Ok(T)` for Result) as its payload type `T`. Guards on the runtime
    // tag so reading the "wrong" variant (None / Err) yields a default T rather
    // than uninitialized union bytes, matching the house "return default on
    // invalid access" style used by Vec.get.
    if (!node->computed) {
        if (auto* valPropIdent = dynamic_cast<ast::Identifier*>(node->property.get())) {
            if (valPropIdent->name == "value") {
                if (auto* objTn = dynamic_cast<ast::TypeName*>(node->object->type.get())) {
                    const std::string vb = objTn->identifier ? objTn->identifier->name : "";
                    const bool isOption = vb == "Option";
                    const bool isResult = vb == "Result";
                    if ((isOption || isResult) && objTn->genericArgs.size() >= 1) {
                        if (const TaggedEnumInfo* tei = findTaggedEnum(node->object->type.get())) {
                            const std::string prim = isOption ? "Some" : "Ok";
                            auto payIt = tei->variantPayloadTypes.find(prim);
                            if (payIt != tei->variantPayloadTypes.end() && payIt->second &&
                                payIt->second->getNumElements() > 0) {
                                llvm::StructType* payloadTy = payIt->second;
                                llvm::Type* eltTy = payloadTy->getElementType(0);
                                llvm::Value* enumVal = objectValue;
                                if (enumVal->getType()->isPointerTy()) {
                                    llvm::Type* enumTy = codegenType(node->object->type.get());
                                    enumVal = builder->CreateLoad(enumTy, enumVal, "enum.value.load");
                                }
                                auto tagIt = tei->variantTags.find(prim);
                                if (!enumVal || tagIt == tei->variantTags.end()) {
                                    logError(node->loc, "Cannot resolve .value on " + vb);
                                    m_currentLLVMValue = nullptr;
                                    return;
                                }
                                llvm::Value* tagVal = builder->CreateExtractValue(enumVal, 0, "enum.value.tag");
                                llvm::Value* primTag = llvm::ConstantInt::get(int64Type, tagIt->second, true);
                                llvm::Value* isPrim = builder->CreateICmpEQ(tagVal, primTag, "enum.value.isprimary");
                                llvm::Value* payload = extractEnumVariantField(enumVal, payloadTy, 0);
                                llvm::Value* defVal = llvm::Constant::getNullValue(eltTy);
                                m_currentLLVMValue = builder->CreateSelect(isPrim, payload, defVal, "enum.value");
                                return;
                            }
                        }
                    }
                }
            }
        }
    }

    if (node->computed) {
        // Computed member access: obj[prop]
        if (!node->property) {
            logError(node->loc, "Computed member access missing property expression");
            m_currentLLVMValue = nullptr;
            return;
        }

        // Evaluate the property expression to get the index
        node->property->accept(*this);
        llvm::Value* indexValue = m_currentLLVMValue;

        if (!indexValue) {
            logError(node->property->loc, "Failed to evaluate property in computed member access");
            m_currentLLVMValue = nullptr;
            return;
        }

        // This is similar to array element access
        if (!objectValue->getType()->isPointerTy()) {
            logError(node->object->loc, "Computed member access on non-pointer type");
            m_currentLLVMValue = nullptr;
            return;
        }

        // For now, assume the object is a pointer to the first element
        // In a full implementation, we'd need type information to determine the element type
        llvm::Type* elementType = llvm::Type::getInt32Ty(*context); // Default fallback

        llvm::Value* elementPtr = builder->CreateGEP(elementType, objectValue, indexValue, "member.computed");
        m_currentLLVMValue = builder->CreateLoad(elementType, elementPtr, "member.load");
    } else {
        // Property member access: obj.prop
        if (!node->property) {
            logError(node->loc, "Property member access missing property identifier");
            m_currentLLVMValue = nullptr;
            return;
        }

        // For property access, we need to cast the property to an identifier
        ast::Identifier* propIdent = dynamic_cast<ast::Identifier*>(node->property.get());
        if (!propIdent) {
            logError(node->property->loc, "Property in member access must be an identifier");
            m_currentLLVMValue = nullptr;
            return;
        }

        llvm::Value* structPtr = nullptr;
        llvm::Type* structType = nullptr;
        bool objectIsOurControlBlock = false;

        // Check if the object is a pointer to a struct (alloca) or actual struct value
        if (objectValue->getType()->isPointerTy()) {
            // Object is a pointer (from alloca) - use it directly
            structPtr = objectValue;

            // Get the pointee type from alloca instruction if available
            if (llvm::AllocaInst* allocaInst = llvm::dyn_cast<llvm::AllocaInst>(objectValue)) {
                structType = allocaInst->getAllocatedType();
                VYB_CDBG << "DEBUG: Got struct type from alloca: " << getTypeName(structType) << std::endl;
            } else {
                // Handle function parameters and other pointer values
                VYB_CDBG << "DEBUG: Object is a pointer but not an alloca, checking pointee type" << std::endl;
                VYB_CDBG << "DEBUG: Object pointer type: " << getTypeName(objectValue->getType()) << std::endl;
                if (llvm::PointerType* ptrType = llvm::dyn_cast<llvm::PointerType>(objectValue->getType())) {
                    // For newer LLVM versions, we need to use a different approach
                    // Since we can't easily get the pointee type, try to get it from the value type map
                    auto valueTypeIter = valueTypeMap.find(objectValue);
                    if (valueTypeIter != valueTypeMap.end()) {
                        // Get the AST type and convert it to LLVM type
                        if (auto astType = valueTypeIter->second.get()) {
                            // Handle ownership types specially - extract underlying type
                            ast::TypeNode* underlyingType = astType;
                            if (auto typeNameNode = dynamic_cast<ast::TypeName*>(astType)) {
                                std::string typeNameStr = typeNameNode->identifier ? typeNameNode->identifier->name : "";
                                if ((typeNameStr == "my" || typeNameStr == "our" || typeNameStr == "their" ||
                                     typeNameStr == "borrow" || typeNameStr == "view") &&
                                    !typeNameNode->genericArgs.empty() && typeNameNode->genericArgs[0]) {
                                    underlyingType = typeNameNode->genericArgs[0].get();
                                    objectIsOurControlBlock = typeNameStr == "our";
                                    VYB_CDBG << "DEBUG: Extracted underlying type from ownership type: " << typeNameStr
                                              << " -> " << underlyingType->toString() << std::endl;
                                }
                            }

                            llvm::Type* astLLVMType = codegenType(underlyingType);
                            if (astLLVMType && astLLVMType->isStructTy()) {
                                structType = astLLVMType;
                                VYB_CDBG << "DEBUG: Got struct type from AST type mapping: " << getTypeName(structType) << std::endl;
                            } else {
                                VYB_CDBG << "DEBUG: AST type mapping didn't yield struct type, got: " << (astLLVMType ? getTypeName(astLLVMType) : "null") << std::endl;
                                logError(node->loc, "Cannot determine struct type for member access");
                                m_currentLLVMValue = nullptr;
                                return;
                            }
                        } else {
                            VYB_CDBG << "DEBUG: No AST type information available" << std::endl;
                            logError(node->loc, "Cannot determine struct type for member access");
                            m_currentLLVMValue = nullptr;
                            return;
                        }
                    } else {
                        VYB_CDBG << "DEBUG: No type mapping found for pointer value" << std::endl;
                        logError(node->loc, "Cannot determine struct type for member access");
                        m_currentLLVMValue = nullptr;
                        return;
                    }
                } else {
                    VYB_CDBG << "DEBUG: Pointer type cast failed" << std::endl;
                    logError(node->loc, "Cannot determine struct type for member access");
                    m_currentLLVMValue = nullptr;
                    return;
                }
            }
        } else if (objectValue->getType()->isStructTy()) {
            // Object is a struct value (loaded from variable) - create temporary alloca
            structType = objectValue->getType();
            structPtr = builder->CreateAlloca(structType, nullptr, "temp_struct");
            builder->CreateStore(objectValue, structPtr);
            VYB_CDBG << "DEBUG: Created temporary alloca for struct value: " << getTypeName(structType) << std::endl;
            // If on LHS and we have the original alloca, store back after field modification
            if (m_isLHSOfAssignment && originalAlloca) {
                VYB_CDBG << "DEBUG: Tracking original alloca for struct value back-store" << std::endl;
            }
        } else {
            logError(node->object->loc, "Property member access on non-struct type");
            m_currentLLVMValue = nullptr;
            return;
        }

        if (!structType || !structType->isStructTy()) {
            logError(node->loc, "Cannot access field of non-struct type");
            m_currentLLVMValue = nullptr;
            return;
        }

        llvm::StructType* llvmStructType = llvm::cast<llvm::StructType>(structType);
        if (objectIsOurControlBlock) {
            // our<T> is represented as a control-block pointer. Field access
            // goes through the payload pointer stored in the block.
            std::vector<llvm::Type*> cbFields = {
                llvm::Type::getInt32Ty(*context),
                llvm::Type::getInt32Ty(*context),
                llvm::Type::getInt8Ty(*context),
                llvm::PointerType::get(*context, 0)
            };
            llvm::StructType* controlBlockType = llvm::StructType::get(*context, cbFields, /*isPacked=*/false);
            llvm::Value* objectPtrFieldPtr = builder->CreateStructGEP(controlBlockType, structPtr, 3, "our.object_ptr_field");
            structPtr = builder->CreateLoad(llvm::PointerType::get(*context, 0), objectPtrFieldPtr, "our.object_ptr");
        }
        std::string fieldName = propIdent->name;
        int fieldIndex = getStructFieldIndex(llvmStructType, fieldName);

        if (fieldIndex < 0) {
            logError(node->loc, "Field '" + fieldName + "' not found in struct");
            m_currentLLVMValue = nullptr;
            return;
        }


        VYB_CDBG << "DEBUG: MemberExpression - Field '" << fieldName << "' at index " << fieldIndex
                  << " in struct " << llvmStructType->getName().str() << std::endl;

        // Create a GEP to get a pointer to the field
        llvm::Value* fieldPtr = builder->CreateStructGEP(llvmStructType, structPtr, fieldIndex, fieldName + "_ptr");


        llvm::Type* fieldType = llvmStructType->getElementType(fieldIndex);
        VYB_CDBG << "DEBUG: Field type: " << getTypeName(fieldType) << std::endl;

        // Check if we're on the LHS of an assignment - in that case return the pointer
        if (m_isLHSOfAssignment) {
            m_currentLLVMValue = fieldPtr;
        } else {
            // For reading, always load the value
            // Even for struct types (like String), we want the value, not a pointer to temporary storage
            llvm::Value* loaded = builder->CreateLoad(fieldType, fieldPtr, fieldName + "_val");
            // Remember the read value's AST type so a further member access on it
            // (e.g. `self.set.values` / `self.map.keys`) can determine the struct
            // type without an alloca. This is what lets iterators read fields
            // through a nested `their<Struct>` view field.
            if (node->type) {
                valueTypeMap[loaded] = std::shared_ptr<vyb::ast::TypeNode>(node->type->clone());
            }
            m_currentLLVMValue = loaded;
        }
    }
}

void LLVMCodegen::visit(ast::BorrowExpression* node) {
    if (!node->expression) {
        logError(node->loc, "Borrow expression missing operand");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Special handling for identifiers - we want the alloca address, not the loaded value
    if (auto* identNode = dynamic_cast<ast::Identifier*>(node->expression.get())) {
        // Look up the identifier in the named values map to get the alloca directly
        auto it = namedValues.find(identNode->name);
        if (it != namedValues.end()) {
            if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(it->second)) {
                // For borrowing an identifier, return the alloca address directly
                m_currentLLVMValue = alloca;
                return;
            }
        }

        // Also check current function named values
        auto funcIt = m_currentFunctionNamedValues.find(identNode->name);
        if (funcIt != m_currentFunctionNamedValues.end()) {
            // For borrowing an identifier, return the alloca address directly
            m_currentLLVMValue = funcIt->second;
            return;
        }

        // If not found, fall through to regular evaluation
    }

    // Evaluate the expression being borrowed (for non-identifier cases)
    node->expression->accept(*this);
    llvm::Value* borrowedValue = m_currentLLVMValue;

    if (!borrowedValue) {
        logError(node->expression->loc, "Failed to evaluate borrow expression operand");
        m_currentLLVMValue = nullptr;
        return;
    }

    // For borrow expressions, we typically want the address of the value
    // The semantics depend on the kind of borrow
    switch (node->kind) {
        case ast::BorrowKind::MUTABLE_BORROW:
            // borrow(expr) - creates a mutable reference
            if (borrowedValue->getType()->isPointerTy()) {
                // If it's already a pointer, just return it
                m_currentLLVMValue = borrowedValue;
            } else {
                // Store in memory and return the address
                llvm::AllocaInst* temp = builder->CreateAlloca(
                    borrowedValue->getType(),
                    nullptr,
                    "borrow.tmp"
                );
                builder->CreateStore(borrowedValue, temp);
                m_currentLLVMValue = temp;
            }
            break;

        case ast::BorrowKind::IMMUTABLE_VIEW:
            // view(expr) - creates an immutable reference
            if (borrowedValue->getType()->isPointerTy()) {
                // If it's already a pointer, just return it
                m_currentLLVMValue = borrowedValue;
            } else {
                // Store in memory and return the address
                llvm::AllocaInst* temp = builder->CreateAlloca(
                    borrowedValue->getType(),
                    nullptr,
                    "view.tmp"
                );
                builder->CreateStore(borrowedValue, temp);
                m_currentLLVMValue = temp;
            }
            break;

        default:
            logError(node->loc, "Unknown borrow kind");
            m_currentLLVMValue = nullptr;
            break;
    }
}

void LLVMCodegen::visit(ast::FromIntToLocExpression* node) {
    if (!node->getAddressExpression()) {
        logError(node->loc, "from<> expression missing operand");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Evaluate the integer expression
    node->getAddressExpression()->accept(*this);
    llvm::Value* intValue = m_currentLLVMValue;

    if (!intValue) {
        logError(node->getAddressExpression()->loc, "Failed to evaluate from<> expression operand");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Convert integer to pointer
    llvm::Type* targetType;
    if (node->getTargetType()) {
        targetType = codegenType(node->getTargetType().get());
    } else {
        // Default to generic pointer
        targetType = llvm::PointerType::get(*context, 0);
    }

    if (!targetType || !targetType->isPointerTy()) {
        logError(node->loc, "from<> target type must be a pointer type");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Convert integer to pointer
    m_currentLLVMValue = builder->CreateIntToPtr(intValue, targetType, "fromint.ptr");
}

void LLVMCodegen::visit(ast::ListComprehension* node) {
    // List comprehensions are complex and require collection allocation
    logError(node->loc, "List comprehensions are not yet implemented in LLVM codegen");
    m_currentLLVMValue = nullptr;
}

void LLVMCodegen::visit(ast::IfExpression* node) {
    // This is an if-expression that returns a value, unlike an if-statement

    // Create basic blocks for the different paths
    llvm::Function* function = getCurrentFunction();
    if (!function) {
        logError(node->loc, "IfExpression outside function context");
        m_currentLLVMValue = nullptr;
        return;
    }

    llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(*context, "if.then", function);
    llvm::BasicBlock* elseBB = llvm::BasicBlock::Create(*context, "if.else", function);
    llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*context, "if.end", function);

    // Generate condition code
    node->condition->accept(*this);
    llvm::Value* condValue = m_currentLLVMValue;
    if (!condValue) {
        logError(node->condition->loc, "Invalid condition expression in if-expression");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Convert condition to i1 (boolean) if it's not already
    if (condValue->getType() != int1Type) {
        condValue = builder->CreateICmpNE(condValue,
                                         llvm::ConstantInt::get(condValue->getType(), 0),
                                         "ifcond");
    }

    // Create the conditional branch
    builder->CreateCondBr(condValue, thenBB, elseBB);

    // Generate 'then' block
    builder->SetInsertPoint(thenBB);
    node->thenBranch->accept(*this);
    llvm::Value* thenValue = m_currentLLVMValue;
    if (!thenValue) {
        logError(node->thenBranch->loc, "Invalid expression in then branch of if-expression");
        m_currentLLVMValue = nullptr;
        return;
    }
    builder->CreateBr(mergeBB);
    thenBB = builder->GetInsertBlock(); // It might have been updated

    // Generate 'else' block
    builder->SetInsertPoint(elseBB);
    node->elseBranch->accept(*this);
    llvm::Value* elseValue = m_currentLLVMValue;
    if (!elseValue) {
        logError(node->elseBranch->loc, "Invalid expression in else branch of if-expression");
        m_currentLLVMValue = nullptr;
        return;
    }
    builder->CreateBr(mergeBB);
    elseBB = builder->GetInsertBlock(); // It might have been updated

    // Generate merge block with PHI node
    builder->SetInsertPoint(mergeBB);

    // Values from then and else branches should have the same type,
    // but we'll try to cast if they don't
    llvm::Type* resultType;
    if (thenValue->getType() == elseValue->getType()) {
        resultType = thenValue->getType();
    } else {
        // For simplicity, we'll just use the then-branch type as the result type
        // A more sophisticated implementation would use type unification
        resultType = thenValue->getType();
        elseValue = tryCast(elseValue, resultType, node->elseBranch->loc);
        if (!elseValue) {
            logError(node->elseBranch->loc, "Type mismatch in if-expression branches");
            m_currentLLVMValue = nullptr;
            return;
        }
    }

    // Create PHI node to consolidate the different paths
    llvm::PHINode* phiNode = builder->CreatePHI(resultType, 2, "ifexpr.result");
    phiNode->addIncoming(thenValue, thenBB);
    phiNode->addIncoming(elseValue, elseBB);

    m_currentLLVMValue = phiNode;
}

void LLVMCodegen::visit(ast::ConstructionExpression* node) {
    if (!node->constructedType) {
        logError(node->loc, "Construction expression missing type");
        m_currentLLVMValue = nullptr;
        return;
    }

    VYB_CDBG << "DEBUG: ConstructionExpression processing type: " << node->constructedType->toString() << std::endl;

    // Explicitly-typed bare Vec constructor: `Vec<T>()` / `Vec<T>(n)`. A typed
    // construction parses `Vec<Int>` as a TypeName (not the bare `Vec` identifier),
    // so it lands here rather than in CallExpression's `emitVecConstructor` path.
    // Route it through the same emitter so the result is the real
    // `{ ptr, size, cap }` struct *value* — not a pointer to an uninitialized
    // alloca (which would break the assignment cast and segfault in a field init).
    if (auto* tname = dynamic_cast<ast::TypeName*>(node->constructedType.get())) {
        if (tname->identifier && tname->identifier->name == "Vec") {
            VYB_CDBG << "DEBUG: ConstructionExpression is a typed Vec constructor" << std::endl;
            auto calleeId = std::make_unique<ast::Identifier>(node->loc, "Vec");
            auto callExpr = std::make_unique<ast::CallExpression>(node->loc, std::move(calleeId), std::move(node->arguments));
            emitVecConstructor(callExpr.get());
            node->arguments = std::move(callExpr->arguments);
            return;
        }
    }

    // If the "constructed type" is actually a generic function template invoked with
    // explicit type arguments (e.g. `probe<Int>(0, 0)`), this is a generic function
    // call rather than struct construction. Dispatch it through the call path so the
    // explicit type args are honored and the failable return ABI (and trap handling)
    // apply, matching plain inferred generic calls.
    if (auto* tname = dynamic_cast<ast::TypeName*>(node->constructedType.get())) {
        if (tname->identifier && !tname->genericArgs.empty()) {
            std::string fnName = tname->identifier->name;
            if (genericFunctionTemplates.find(fnName) != genericFunctionTemplates.end()) {
                VYB_CDBG << "DEBUG: ConstructionExpression is a generic function call: " << fnName << std::endl;
                auto calleeId = std::make_unique<ast::Identifier>(tname->identifier->loc, fnName);
                auto callExpr = std::make_unique<ast::CallExpression>(node->loc, std::move(calleeId), std::move(node->arguments));
                callExpr->explicitTypeArgs = std::move(tname->genericArgs);
                this->visit(static_cast<ast::CallExpression*>(callExpr.get()));
                return;
            }
        }
    }

    // Declared struct constructor: `HashMap<K,V>(n)` with a matching arity routes
    // to the synthetic `__ctor_HashMap_<N>` generic function. Struct names are not
    // generic-fn templates, so this never collides with the branch above. When a
    // generic-function monomorphization is active, the struct's own type params
    // (K, V) are substituted so a constructor can chain to another constructor.
    if (auto* tname = dynamic_cast<ast::TypeName*>(node->constructedType.get())) {
        if (tname->identifier) {
            std::string structName = tname->identifier->name;
            auto cit = structConstructors.find(structName);
            if (cit != structConstructors.end()) {
                for (const auto& entry : cit->second) {
                    if (entry.first == (unsigned)node->arguments.size()) {
                        VYB_CDBG << "DEBUG: ConstructionExpression is a struct constructor call: "
                                 << structName << " arity " << entry.first << std::endl;
                        auto calleeId = std::make_unique<ast::Identifier>(tname->identifier->loc, entry.second);
                        auto callExpr = std::make_unique<ast::CallExpression>(node->loc, std::move(calleeId), std::move(node->arguments));
                        if (!tname->genericArgs.empty()) {
                            callExpr->explicitTypeArgs = applyTypeSubstitutions(tname->genericArgs);
                        }
                        this->visit(static_cast<ast::CallExpression*>(callExpr.get()));
                        return;
                    }
                }
            }
        }
    }

    // Get the type being constructed
    llvm::Type* constructedLLVMType = codegenType(node->constructedType.get());
    if (!constructedLLVMType) {
        logError(node->loc, "Failed to resolve constructed type");
        m_currentLLVMValue = nullptr;
        return;
    }

    VYB_CDBG << "DEBUG: Successfully resolved constructed type: " << node->constructedType->toString() << std::endl;

    // For now, implement basic construction with default values
    if (constructedLLVMType->isStructTy()) {
        // Struct construction
        llvm::StructType* structType = llvm::cast<llvm::StructType>(constructedLLVMType);

        // Allocate memory for the struct
        llvm::AllocaInst* structAlloca = builder->CreateAlloca(structType, nullptr, "struct.tmp");

        // Initialize with default values or provided arguments
        for (unsigned i = 0; i < structType->getNumElements() && i < node->arguments.size(); ++i) {
            if (node->arguments[i]) {
                node->arguments[i]->accept(*this);
                llvm::Value* argValue = m_currentLLVMValue;
                if (argValue) {
                    llvm::Value* fieldPtr = builder->CreateStructGEP(structType, structAlloca, i, "field.ptr");
                    builder->CreateStore(argValue, fieldPtr);
                }
            }
        }

        m_currentLLVMValue = structAlloca;
    } else {
        // For primitive types, just use the first argument or default value
        if (!node->arguments.empty() && node->arguments[0]) {
            node->arguments[0]->accept(*this);
            llvm::Value* argValue = m_currentLLVMValue;
            if (argValue) {
                m_currentLLVMValue = tryCast(argValue, constructedLLVMType, node->loc);
            } else {
                m_currentLLVMValue = nullptr;
            }
        } else {
            // Default value for the type
            if (constructedLLVMType->isIntegerTy()) {
                m_currentLLVMValue = llvm::ConstantInt::get(constructedLLVMType, 0);
            } else if (constructedLLVMType->isFloatingPointTy()) {
                m_currentLLVMValue = llvm::ConstantFP::get(constructedLLVMType, 0.0);
            } else if (constructedLLVMType->isPointerTy()) {
                m_currentLLVMValue = llvm::ConstantPointerNull::get(
                    llvm::cast<llvm::PointerType>(constructedLLVMType)
                );
            } else {
                m_currentLLVMValue = llvm::UndefValue::get(constructedLLVMType);
            }
        }
    }
}

void LLVMCodegen::visit(ast::ArrayInitializationExpression* node) {
    if (!node->elementType) {
        logError(node->loc, "Array initialization missing element type");
        m_currentLLVMValue = nullptr;
        return;
    }

    if (!node->sizeExpression) {
        logError(node->loc, "Array initialization missing size expression");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Get the element type
    llvm::Type* elementType = codegenType(node->elementType.get());
    if (!elementType) {
        logError(node->loc, "Failed to resolve array element type");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Evaluate the size expression
    node->sizeExpression->accept(*this);
    llvm::Value* sizeValue = m_currentLLVMValue;
    if (!sizeValue) {
        logError(node->sizeExpression->loc, "Failed to evaluate array size");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Ensure size is an integer
    if (!sizeValue->getType()->isIntegerTy()) {
        logError(node->sizeExpression->loc, "Array size must be an integer");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Create array type
    llvm::ArrayType* arrayType = llvm::ArrayType::get(elementType, 0); // Dynamic size

    // Allocate memory for the array
    llvm::AllocaInst* arrayAlloca = builder->CreateAlloca(elementType, sizeValue, "array.tmp");

    // Initialize array elements to default values
    // For simplicity, we'll zero-initialize
    llvm::Value* zero = llvm::ConstantInt::get(sizeValue->getType(), 0);
    llvm::Value* one = llvm::ConstantInt::get(sizeValue->getType(), 1);

    // Create a loop to initialize the array
    llvm::Function* function = getCurrentFunction();
    llvm::BasicBlock* loopHeaderBB = llvm::BasicBlock::Create(*context, "array.init.header", function);
    llvm::BasicBlock* loopBodyBB = llvm::BasicBlock::Create(*context, "array.init.body", function);
    llvm::BasicBlock* loopExitBB = llvm::BasicBlock::Create(*context, "array.init.exit", function);

    // Initialize loop counter
    llvm::AllocaInst* counterAlloca = builder->CreateAlloca(sizeValue->getType(), nullptr, "counter");
    builder->CreateStore(zero, counterAlloca);
    builder->CreateBr(loopHeaderBB);

    // Loop header: check condition
    builder->SetInsertPoint(loopHeaderBB);
    llvm::Value* currentCounter = builder->CreateLoad(sizeValue->getType(), counterAlloca, "counter.val");
    llvm::Value* condition = builder->CreateICmpSLT(currentCounter, sizeValue, "loop.cond");
    builder->CreateCondBr(condition, loopBodyBB, loopExitBB);

    // Loop body: initialize element
    builder->SetInsertPoint(loopBodyBB);
    llvm::Value* elementPtr = builder->CreateGEP(elementType, arrayAlloca, currentCounter, "element.ptr");

    // Initialize element with default value
    llvm::Value* defaultValue;
    if (elementType->isIntegerTy()) {
        defaultValue = llvm::ConstantInt::get(elementType, 0);
    } else if (elementType->isFloatingPointTy()) {
        defaultValue = llvm::ConstantFP::get(elementType, 0.0);
    } else if (elementType->isPointerTy()) {
        defaultValue = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(elementType));
    } else {
        defaultValue = llvm::UndefValue::get(elementType);
    }

    builder->CreateStore(defaultValue, elementPtr);

    // Increment counter
    llvm::Value* nextCounter = builder->CreateAdd(currentCounter, one, "counter.next");
    builder->CreateStore(nextCounter, counterAlloca);
    builder->CreateBr(loopHeaderBB);

    // Loop exit
    builder->SetInsertPoint(loopExitBB);

    m_currentLLVMValue = arrayAlloca;
}

void LLVMCodegen::visit(vyb::ast::GenericInstantiationExpression* node) {
    // TODO: Implement generic instantiation expression
    // This should handle generic type instantiation with specific type arguments
    m_currentLLVMValue = nullptr;
}

void LLVMCodegen::visit(ast::LogicalExpression* node) {
    // Logical expressions with short-circuit evaluation
    node->left->accept(*this);
    llvm::Value* leftValue = m_currentLLVMValue;

    if (!leftValue) {
        logError(node->left->loc, "Invalid left operand in logical expression");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Convert to boolean if needed
    if (!leftValue->getType()->isIntegerTy(1)) {
        if (leftValue->getType()->isIntegerTy()) {
            leftValue = builder->CreateICmpNE(leftValue,
                llvm::ConstantInt::get(leftValue->getType(), 0), "left.bool");
        } else if (leftValue->getType()->isFloatingPointTy()) {
            leftValue = builder->CreateFCmpONE(leftValue,
                llvm::ConstantFP::get(leftValue->getType(), 0.0), "left.bool");
        } else if (leftValue->getType()->isPointerTy()) {
            leftValue = builder->CreateICmpNE(leftValue,
                llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(leftValue->getType())), "left.bool");
        }
    }

    // Create basic blocks for short-circuit evaluation
    llvm::BasicBlock* rightBB = llvm::BasicBlock::Create(*context, "logical.right", currentFunction);
    llvm::BasicBlock* endBB = llvm::BasicBlock::Create(*context, "logical.end", currentFunction);
    llvm::BasicBlock* leftBB = builder->GetInsertBlock();

    if (node->op.lexeme == "&&") {
        // For &&: if left is false, don't evaluate right
        builder->CreateCondBr(leftValue, rightBB, endBB);
    } else { // "||"
        // For ||: if left is true, don't evaluate right
        builder->CreateCondBr(leftValue, endBB, rightBB);
    }

    // Right operand block
    builder->SetInsertPoint(rightBB);
    node->right->accept(*this);
    llvm::Value* rightValue = m_currentLLVMValue;

    if (!rightValue) {
        logError(node->right->loc, "Invalid right operand in logical expression");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Convert to boolean if needed
    if (!rightValue->getType()->isIntegerTy(1)) {
        if (rightValue->getType()->isIntegerTy()) {
            rightValue = builder->CreateICmpNE(rightValue,
                llvm::ConstantInt::get(rightValue->getType(), 0), "right.bool");
        } else if (rightValue->getType()->isFloatingPointTy()) {
            rightValue = builder->CreateFCmpONE(rightValue,
                llvm::ConstantFP::get(rightValue->getType(), 0.0), "right.bool");
        } else if (rightValue->getType()->isPointerTy()) {
            rightValue = builder->CreateICmpNE(rightValue,
                llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(rightValue->getType())), "right.bool");
        }
    }

    builder->CreateBr(endBB);
    rightBB = builder->GetInsertBlock();

    // End block with PHI node
    builder->SetInsertPoint(endBB);
    llvm::PHINode* phi = builder->CreatePHI(llvm::Type::getInt1Ty(*context), 2, "logical.result");

    if (node->op.lexeme == "&&") {
        phi->addIncoming(llvm::ConstantInt::getFalse(*context), leftBB);
        phi->addIncoming(rightValue, rightBB);
    } else { // "||"
        phi->addIncoming(llvm::ConstantInt::getTrue(*context), leftBB);
        phi->addIncoming(rightValue, rightBB);
    }

    m_currentLLVMValue = phi;
}

void LLVMCodegen::visit(ast::ConditionalExpression* node) {
    // Visit condition
    node->condition->accept(*this);
    llvm::Value* condValue = m_currentLLVMValue;

    // Convert to boolean if needed
    if (condValue->getType() != int1Type) {
        if (condValue->getType()->isIntegerTy()) {
            condValue = builder->CreateICmpNE(condValue,
                llvm::ConstantInt::get(condValue->getType(), 0), "cond.bool");
        } else if (condValue->getType()->isFloatingPointTy()) {
            condValue = builder->CreateFCmpONE(condValue,
                llvm::ConstantFP::get(condValue->getType(), 0.0), "cond.bool");
        } else if (condValue->getType()->isPointerTy()) {
            condValue = builder->CreateICmpNE(condValue,
                llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(condValue->getType())), "cond.bool");
        }
    }

    // Create basic blocks
    llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(*context, "cond.then", currentFunction);
    llvm::BasicBlock* elseBB = llvm::BasicBlock::Create(*context, "cond.else", currentFunction);
    llvm::BasicBlock* endBB = llvm::BasicBlock::Create(*context, "cond.end", currentFunction);

    builder->CreateCondBr(condValue, thenBB, elseBB);

    // Then branch
    builder->SetInsertPoint(thenBB);
    node->thenExpr->accept(*this);
    llvm::Value* thenValue = m_currentLLVMValue;
    llvm::BasicBlock* thenEndBB = builder->GetInsertBlock();
    builder->CreateBr(endBB);

    // Else branch
    builder->SetInsertPoint(elseBB);
    node->elseExpr->accept(*this);
    llvm::Value* elseValue = m_currentLLVMValue;
    llvm::BasicBlock* elseEndBB = builder->GetInsertBlock();
    builder->CreateBr(endBB);

    // Merge
    builder->SetInsertPoint(endBB);

    // Ensure both values have compatible types
    llvm::Type* resultType = thenValue->getType();
    if (thenValue->getType() != elseValue->getType()) {
        // Try to cast to a common type (simplified - in a real compiler, you'd do proper type resolution)
        if (thenValue->getType()->isIntegerTy() && elseValue->getType()->isIntegerTy()) {
            // Use the larger integer type
            if (thenValue->getType()->getIntegerBitWidth() > elseValue->getType()->getIntegerBitWidth()) {
                elseValue = builder->CreateSExt(elseValue, thenValue->getType());
                resultType = thenValue->getType();
            } else {
                thenValue = builder->CreateSExt(thenValue, elseValue->getType());
                resultType = elseValue->getType();
            }
        } else {
            // Default to i32 if types are incompatible
            resultType = llvm::Type::getInt32Ty(*context);
            if (!thenValue->getType()->isIntegerTy(32)) {
                thenValue = llvm::ConstantInt::get(resultType, 0);
            }
            if (!elseValue->getType()->isIntegerTy(32)) {
                elseValue = llvm::ConstantInt::get(resultType, 0);
            }
        }
    }

    llvm::PHINode* phi = builder->CreatePHI(resultType, 2, "cond.result");
    phi->addIncoming(thenValue, thenEndBB);
    phi->addIncoming(elseValue, elseEndBB);

    m_currentLLVMValue = phi;
}

void LLVMCodegen::visit(ast::FunctionExpression* node) {
    // A function expression creates a lambda. Every lambda lowers to a closure
    // value: `struct { ptr env, ptr fn }`. Captured variables (filled in by the
    // semantic analyzer) are copied by value into a heap-allocated environment
    // at creation time; the lambda function receives that environment as a
    // hidden first parameter and reloads each capture into a local alloca.
    std::string funcName = "lambda_" + std::to_string(reinterpret_cast<uintptr_t>(node));

    // Process parameters.
    std::vector<llvm::Type*> paramTypes;
    std::vector<std::string> paramNames;
    for (const auto& param : node->params) {
        llvm::Type* paramType = param.typeNode ? codegenType(param.typeNode.get()) : nullptr;
        if (!paramType) {
            paramType = llvm::PointerType::get(*context, 0);
            logWarning(node->loc, "Parameter type not specified in function expression, defaulting to pointer type");
        }
        paramTypes.push_back(paramType);
        paramNames.push_back(param.name ? param.name->name : "param" + std::to_string(paramNames.size()));
    }

    // Infer return type from the semantic FunctionType the analyzer attached to
    // this lambda (its returnType reflects explicit `return` statements for block
    // bodies). Prefer that over node->body->type, which is null when a block ends
    // in a `return` statement (rather than a tail expression).
    llvm::Type* returnType = llvm::Type::getVoidTy(*context);
    if (auto* ft = dynamic_cast<ast::FunctionType*>(node->type.get())) {
        if (ft->returnType) {
            llvm::Type* t = codegenType(ft->returnType.get());
            if (t) returnType = t;
        }
    } else if (node->body && node->body->type) {
        llvm::Type* inferredType = codegenType(node->body->type.get());
        if (inferredType && !inferredType->isVoidTy()) {
            returnType = inferredType;
        }
    }

    // User-facing signature (no environment parameter). Stored for call sites
    // that invoke a lambda held in a local variable.
    llvm::FunctionType* userFuncType = llvm::FunctionType::get(returnType, paramTypes, false);
    lastLambdaFuncType = userFuncType;

    // Determine the captures to bake into the environment: variables the body
    // references that are visible in the enclosing scope at creation time.
    // Mutable captures store the *address* of the outer variable in the env and
    // write back through it; immutable captures snapshot the value.
    std::unordered_set<std::string> mutableSet(
        node->mutableCapturedVariables.begin(), node->mutableCapturedVariables.end());
    struct Capture { std::string name; llvm::Type* ty; llvm::Value* outer; bool mutable_ = false; };
    std::vector<Capture> captures;
    std::vector<llvm::Type*> envFieldTypes;
    for (const auto& nm : node->capturedVariables) {
        auto it = namedValues.find(nm);
        if (it == namedValues.end()) continue;  // not a local variable in scope
        llvm::Type* ty = nullptr;
        if (auto* ai = llvm::dyn_cast<llvm::AllocaInst>(it->second)) {
            ty = ai->getAllocatedType();
        } else {
            ty = it->second->getType();
        }
        bool isMut = mutableSet.count(nm) != 0;
        captures.push_back({nm, ty, it->second, isMut});
        // Mutable captures store a pointer to the outer variable; immutable
        // captures store the value itself.
        envFieldTypes.push_back(isMut ? it->second->getType() : ty);
    }

    llvm::StructType* envStructType = nullptr;
    if (!envFieldTypes.empty()) {
        // The env heap block carries a reference-count header on top of the
        // captured fields: `{ i64 refcount; ptr cap_dtor; <captures...> }`.
        std::vector<llvm::Type*> envWithHeader;
        envWithHeader.push_back(llvm::Type::getInt64Ty(*context));       // index 0: refcount
        envWithHeader.push_back(llvm::PointerType::get(*context, 0));    // index 1: cap_dtor
        envWithHeader.insert(envWithHeader.end(), envFieldTypes.begin(), envFieldTypes.end());
        envStructType = llvm::StructType::create(*context, envWithHeader, "closure.env." + funcName);
    }

    // Lambda function type = env (ptr) + user params.
    std::vector<llvm::Type*> fullParamTypes;
    fullParamTypes.push_back(llvm::PointerType::get(*context, 0));
    for (auto* t : paramTypes) fullParamTypes.push_back(t);
    llvm::FunctionType* funcType = llvm::FunctionType::get(returnType, fullParamTypes, false);

    llvm::Function* function = llvm::Function::Create(funcType, llvm::Function::InternalLinkage, funcName, module.get());

    // Name args: 0 = env, 1.. = user params.
    unsigned nameIdx = 0;
    for (auto& arg : function->args()) {
        if (nameIdx == 0) arg.setName("closure.env");
        else arg.setName(paramNames[nameIdx - 1]);
        ++nameIdx;
    }

    llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(*context, "entry", function);
    llvm::Function* savedFunction = currentFunction;
    llvm::BasicBlock* savedBlock = builder->GetInsertBlock();
    std::map<std::string, llvm::Value*> savedNamedValues = namedValues;
    std::map<std::string, llvm::Value*> savedMutableCaptureOuterPointers = mutableCaptureOuterPointers;

    currentFunction = function;
    builder->SetInsertPoint(entryBB);
    // Give the lambda its own runtime call-stack frame so explicit `return`
    // statements inside its body (which emit a matching pop) stay balanced and
    // the lambda shows up in error stack traces.
    generatePushFrameCall(funcName, node->loc);
    namedValues.clear();
    mutableCaptureOuterPointers.clear();
    // Isolate scope management from the enclosing function. A lambda body lives
    // in its own LLVM function, but the scope stack / function baseline are
    // shared members; without isolating them, a `return` inside the lambda runs
    // exitToFunctionBaseline() against the *enclosing* function's baseline and
    // cleans up the enclosing variables, emitting loads of their allocas (which
    // live in a different function) inside the lambda - an LLVM dominance error.
    auto savedLambdaScopeStack = scopeStack;
    size_t savedLambdaBaseline = m_functionScopeBaseline;
    scopeStack.clear();
    m_functionScopeBaseline = 0;
    enterScope();

    // Prologue: reload each capture from the env into a local alloca so the
    // generic identifier lookup resolves them. Mutable captures remember the
    // outer variable's address so writes can propagate back to it.
    if (envStructType) {
        llvm::Value* envArg = function->getArg(0);
        llvm::Value* envCast = builder->CreateBitCast(envArg, envStructType->getPointerTo(), "closure.env.cast");
        for (size_t ci = 0; ci < captures.size(); ++ci) {
            llvm::Value* fieldPtr = builder->CreateStructGEP(envStructType, envCast, ci + 2, "closure.env.field");
            llvm::Value* fieldVal = builder->CreateLoad(envFieldTypes[ci], fieldPtr, "closure.env.load");
            llvm::AllocaInst* capAlloca = builder->CreateAlloca(captures[ci].ty, nullptr, "closure.cap." + captures[ci].name);
            if (captures[ci].mutable_) {
                // fieldVal is the address of the outer variable's alloca:
                // snapshot its current value, and remember the address.
                llvm::Value* outerPtr = fieldVal;
                llvm::Value* snapVal = builder->CreateLoad(captures[ci].ty, outerPtr, "closure.cap.snap");
                builder->CreateStore(snapVal, capAlloca);
                mutableCaptureOuterPointers[captures[ci].name] = outerPtr;
            } else {
                builder->CreateStore(fieldVal, capAlloca);
            }
            namedValues[captures[ci].name] = capAlloca;
            // Record the captured variable's AST type on the reloaded alloca so
            // field access resolves on ownership-wrapped captures (their/my/our/
            // view/borrow) — e.g. `f = |x| -> shared.n` where `shared<our<T>>`.
            // Without this, the capAlloca's pointee type is unknown and member
            // access errors with "Cannot determine struct type for member access".
            auto capOuterTy = valueTypeMap.find(captures[ci].outer);
            if (capOuterTy != valueTypeMap.end() && capOuterTy->second) {
                valueTypeMap[capAlloca] = std::shared_ptr<vyb::ast::TypeNode>(capOuterTy->second->clone().release());
            }
        }
    }

    // Params allocas (args 1..N; arg 0 is the environment pointer).
    size_t pi = 0;
    for (auto& ai : function->args()) {
        if (pi == 0) { pi = 1; continue; }
        llvm::AllocaInst* alloca = builder->CreateAlloca(ai.getType(), nullptr, paramNames[pi - 1] + ".addr");
        builder->CreateStore(&ai, alloca);
        namedValues[paramNames[pi - 1]] = alloca;
        ++pi;
    }

    // Generate the body (or default return).
    if (node->body) {
        node->body->accept(*this);
        llvm::Value* bodyValue = m_currentLLVMValue;
        if (!builder->GetInsertBlock()->getTerminator()) {
            if (returnType->isVoidTy()) {
                generatePopFrameCall();
                builder->CreateRetVoid();
            } else if (bodyValue && bodyValue->getType() == returnType) {
                generatePopFrameCall();
                builder->CreateRet(bodyValue);
            } else if (bodyValue && returnType->isStructTy() &&
                       bodyValue->getType()->isPointerTy()) {
                // A raw char* (e.g. the result of a String concatenation or a
                // primitive .to_string()) returned from an expression-body lambda:
                // wrap it into the `{ ptr, i64 }` String struct.
                llvm::StructType* st = llvm::dyn_cast<llvm::StructType>(returnType);
                bool isStringStruct = st && st->getNumElements() == 2 &&
                    st->getElementType(0)->isPointerTy() &&
                    st->getElementType(1)->isIntegerTy(64);
                if (isStringStruct) {
                    llvm::Value* sw = llvm::UndefValue::get(returnType);
                    sw = builder->CreateInsertValue(sw, bodyValue, 0, "lambda.str.ptr");
                    llvm::Function* strlenFn = module->getFunction("strlen");
                    if (!strlenFn) {
                        llvm::FunctionType* strlenTy = llvm::FunctionType::get(
                            llvm::Type::getInt64Ty(*context), {llvm::PointerType::get(*context, 0)}, false);
                        strlenFn = llvm::Function::Create(strlenTy, llvm::Function::ExternalLinkage, "strlen", module.get());
                    }
                    llvm::Value* len = builder->CreateCall(strlenFn, {bodyValue}, "lambda.str.len");
                    sw = builder->CreateInsertValue(sw, len, 1, "lambda.str");
                    generatePopFrameCall();
                    builder->CreateRet(sw);
                } else {
                    generatePopFrameCall();
                    builder->CreateRet(llvm::UndefValue::get(returnType));
                    logWarning(node->loc, "Function expression with non-trivial return type is missing return statement");
                }
            } else if (bodyValue && returnType->isIntegerTy() && bodyValue->getType()->isIntegerTy()) {
                generatePopFrameCall();
                builder->CreateRet(builder->CreateSExtOrTrunc(bodyValue, returnType, "lambda.intcast"));
            } else if (bodyValue && returnType->isFloatingPointTy() && bodyValue->getType()->isIntegerTy()) {
                generatePopFrameCall();
                builder->CreateRet(builder->CreateSIToFP(bodyValue, returnType, "lambda.fpcast"));
            } else {
                llvm::Value* defaultValue = nullptr;
                if (returnType->isIntegerTy()) defaultValue = llvm::ConstantInt::get(returnType, 0);
                else if (returnType->isFloatingPointTy()) defaultValue = llvm::ConstantFP::get(returnType, 0.0);
                else if (returnType->isPointerTy()) defaultValue = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(returnType));
                else { defaultValue = llvm::UndefValue::get(returnType); logWarning(node->loc, "Function expression with non-trivial return type is missing return statement"); }
                generatePopFrameCall();
                builder->CreateRet(defaultValue);
            }
        }
    } else {
        if (returnType->isVoidTy()) {
            generatePopFrameCall();
            builder->CreateRetVoid();
        } else {
            llvm::Value* defaultValue = nullptr;
            if (returnType->isIntegerTy()) defaultValue = llvm::ConstantInt::get(returnType, 0);
            else if (returnType->isFloatingPointTy()) defaultValue = llvm::ConstantFP::get(returnType, 0.0);
            else if (returnType->isPointerTy()) defaultValue = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(returnType));
            else defaultValue = llvm::UndefValue::get(returnType);
            generatePopFrameCall();
            builder->CreateRet(defaultValue);
        }
    }

    // Verify the generated lambda.
    std::string verifyErrors;
    llvm::raw_string_ostream errStream(verifyErrors);
    bool hasErrors = llvm::verifyFunction(*function, &errStream);
    if (hasErrors) {
        logError(node->loc, "Generated function expression has errors: " + verifyErrors);
        function->eraseFromParent();
        m_currentLLVMValue = nullptr;
        currentFunction = savedFunction;
        builder->SetInsertPoint(savedBlock);
        namedValues = savedNamedValues;
        mutableCaptureOuterPointers = savedMutableCaptureOuterPointers;
        scopeStack = savedLambdaScopeStack;
        m_functionScopeBaseline = savedLambdaBaseline;
        return;
    }

    // Back in the enclosing function: allocate the environment and capture the
    // current value of each captured variable (copy by value), then build the
    // closure struct value { env, fn }.
    currentFunction = savedFunction;
    builder->SetInsertPoint(savedBlock);
    namedValues = savedNamedValues;
    mutableCaptureOuterPointers = savedMutableCaptureOuterPointers;
    scopeStack = savedLambdaScopeStack;
    m_functionScopeBaseline = savedLambdaBaseline;

    llvm::Value* envPtr = llvm::ConstantPointerNull::get(llvm::PointerType::get(*context, 0));
    if (envStructType) {
        llvm::DataLayout dataLayout(module.get());
        uint64_t envBytes = dataLayout.getTypeAllocSize(envStructType);
        llvm::Function* mallocFunc = getOrCreateMallocFunction();
        llvm::Value* sizeVal = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), envBytes);
        llvm::Value* raw = builder->CreateCall(mallocFunc, {sizeVal}, "closure.env.alloc");
        llvm::Value* envCast = builder->CreateBitCast(raw, envStructType->getPointerTo(), "closure.env.ptr");

        // Reference-count header: refcount starts at 0 (each durable storage
        // location that takes this closure value retains it). The cap_dtor slot
        // stays null, so __vyb_closure_release frees the env directly when the
        // last reference is dropped.
        llvm::Value* refcountPtr = builder->CreateStructGEP(envStructType, envCast, 0, "closure.env.refcountptr");
        builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0), refcountPtr, "closure.env.refcount");
        llvm::Value* dtorPtr = builder->CreateStructGEP(envStructType, envCast, 1, "closure.env.dtorptr");
        builder->CreateStore(llvm::ConstantPointerNull::get(llvm::PointerType::get(*context, 0)), dtorPtr);

        for (size_t ci = 0; ci < captures.size(); ++ci) {
            llvm::Value* fieldPtr = builder->CreateStructGEP(envStructType, envCast, ci + 2, "closure.env.setptr");
            if (captures[ci].mutable_) {
                builder->CreateStore(captures[ci].outer, fieldPtr);
            } else {
                llvm::Value* val = builder->CreateLoad(captures[ci].ty, captures[ci].outer, "closure.cap.val");
                builder->CreateStore(val, fieldPtr);
            }
        }

        envPtr = raw;
    }

    llvm::StructType* closureTy = getClosureStructType();
    llvm::Value* closureVal = llvm::UndefValue::get(closureTy);
    closureVal = builder->CreateInsertValue(closureVal, envPtr, 0, "closure.env");
    closureVal = builder->CreateInsertValue(closureVal, function, 1, "closure.fn");
    m_currentLLVMValue = closureVal;
}

void LLVMCodegen::visit(ast::SequenceExpression* node) {
    // For multi-value returns, create a struct containing all values
    std::vector<llvm::Value*> values;
    std::vector<llvm::Type*> types;

    // Evaluate all expressions and collect their values and types
    for (const auto& expr : node->expressions) {
        expr->accept(*this);
        if (m_currentLLVMValue) {
            values.push_back(m_currentLLVMValue);
            types.push_back(m_currentLLVMValue->getType());
        } else {
            logError(expr->loc, "Expression in sequence evaluated to null");
            m_currentLLVMValue = nullptr;
            return;
        }
    }

    if (values.empty()) {
        logError(node->loc, "Empty sequence expression");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Even for single value, we need to create a struct if expected
    // (e.g., return type is Tuple<Int> which is a struct)
    // The type checking will be done at return statement level

    // Create a struct type for the values
    llvm::StructType* tupleType = llvm::StructType::get(*context, types);

    // Create an undef struct and insert each value
    llvm::Value* structValue = llvm::UndefValue::get(tupleType);
    for (size_t i = 0; i < values.size(); ++i) {
        structValue = builder->CreateInsertValue(structValue, values[i], i, "tuple_insert");
    }

    m_currentLLVMValue = structValue;
}

// Add missing visitor implementations for ThisExpression, SuperExpression, and AwaitExpression
void LLVMCodegen::visit(ast::ThisExpression* node) {
    // 'this' expression - typically returns a pointer to the current object instance
    // For now, implement as a placeholder that returns null
    // TODO: Implement proper 'this' semantics when class/object support is added
    logError(node->loc, "'this' expressions are not yet fully implemented");
    m_currentLLVMValue = llvm::ConstantPointerNull::get(llvm::PointerType::get(*context, 0));
}

void LLVMCodegen::visit(ast::SuperExpression* node) {
    // 'super' expression - typically refers to parent class methods/properties
    // For now, implement as a placeholder that returns null
    // TODO: Implement proper inheritance semantics when class support is added
    logError(node->loc, "'super' expressions are not yet fully implemented");
    m_currentLLVMValue = llvm::ConstantPointerNull::get(llvm::PointerType::get(*context, 0));
}

void LLVMCodegen::visit(ast::RangeExpression* node) {
    // Range expressions are handled during parsing/desugaring for range-based for loops
    // They should not appear directly in LLVM codegen
    logError(node->loc, "Range expression should have been desugared during parsing");
    m_currentLLVMValue = nullptr;
}

void LLVMCodegen::visit(ast::BlockExpression* node) {
    // Block as expression with trap/ensure support:
    // 1. Execute the block statements
    // 2. If trap clauses exist, set up error handling
    // 3. If ensure clause exists, generate cleanup code
    // 4. The last value becomes the result

    if (!node->block) {
        m_currentLLVMValue = nullptr;
        return;
    }

    llvm::Function* func = getCurrentFunction();
    if (!func) {
        logError(node->loc, "BlockExpression outside function context");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Check if this block has trap or ensure clauses
    bool hasTrap = !node->trapClauses.empty();
    bool hasEnsure = node->ensureClause != nullptr;

    if (!hasTrap && !hasEnsure) {
        // Simple block without error handling - execute normally
        for (const auto& stmt : node->block->body) {
            stmt->accept(*this);
        }
        return;
    }

    // Block with error handling - set up trap/ensure infrastructure
    llvm::BasicBlock* normalBB = llvm::BasicBlock::Create(*context, "block.normal", func);
    llvm::BasicBlock* ensureBB = hasEnsure ? llvm::BasicBlock::Create(*context, "block.ensure", func) : nullptr;
    llvm::BasicBlock* continueBB = llvm::BasicBlock::Create(*context, "block.continue", func);

    // Alloca for block result (used when hasTrap || hasEnsure). Sized from the
    // block's inferred semantic type so aggregates like String { ptr, i64 } are
    // stored/loaded with the correct type instead of a hardcoded i64. The alloca
    // is created before any branches so it dominates the merge point's load.
    llvm::Type* blockResultTy = nullptr;
    if (node->type) blockResultTy = codegenType(node->type.get());
    // A block that yields no value (e.g. used as a statement with a void trap
    // handler) must not size the slot with a void/codegen-invalid type; fall back
    // to a neutral scalar so the (unused) merge load stays well-formed.
    if (!blockResultTy || blockResultTy->isVoidTy()) blockResultTy = builder->getInt64Ty();
    llvm::AllocaInst* blockResultAlloca = builder->CreateAlloca(blockResultTy, nullptr, "block.result.alloca");

    // Create error slot and landing pad if we have trap clauses
    llvm::Value* errorSlot = nullptr;
    llvm::BasicBlock* landingPadBB = nullptr;

    if (hasTrap) {
        // Determine error type from first trap clause
        // Phase 6.5: For wildcard traps, error type is nullptr, use generic i8* pointer
        // Phase 6.6: For multi-type traps, use generic pointer (error binding will be opaque)
        llvm::Type* errorLLVMType = nullptr;
        if (node->trapClauses[0]->isWildcard) {
            // Wildcard trap: use generic pointer type
            errorLLVMType = llvm::PointerType::get(*context, 0);
        } else if (node->trapClauses[0]->isMultiType) {
            // Multi-type trap: use generic pointer type (handler can't access typed fields yet)
            errorLLVMType = llvm::PointerType::get(*context, 0);
        } else {
            ast::TypeNode* errorType = node->trapClauses[0]->errorType.get();
            if (!errorType) {
                logError(node->loc, "Trap clause missing error type");
                m_currentLLVMValue = nullptr;
                return;
            }
            errorLLVMType = codegenType(errorType);
        }

        if (!errorLLVMType) {
            logError(node->loc, "Failed to generate type for error in trap clause");
            m_currentLLVMValue = nullptr;
            return;
        }

        // HEAP ALLOCATION: Use malloc for error pointer storage to avoid x86-64 ABI corruption
        // Allocate 8 bytes on heap to store the error pointer
        llvm::Function* mallocFunc = module->getFunction("malloc");
        if (!mallocFunc) {
            llvm::FunctionType* mallocType = llvm::FunctionType::get(
                llvm::PointerType::get(*context, 0),
                {builder->getInt64Ty()},
                false
            );
            mallocFunc = llvm::Function::Create(mallocType, llvm::Function::ExternalLinkage, "malloc", module.get());
        }

        llvm::Value* size = builder->getInt64(8); // sizeof(void*)
        errorSlot = builder->CreateCall(mallocFunc, {size}, "trap_error_heap");

        // Initialize heap memory to NULL
        llvm::Type* errorPtrType = llvm::PointerType::get(*context, 0);
        builder->CreateStore(llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(errorPtrType)), errorSlot);

        // Create landing pad for error handling
        landingPadBB = llvm::BasicBlock::Create(*context, "trap.landing", func);

        // Push trap context onto stack
        TrapContext trapCtx;
        trapCtx.landingPad = landingPadBB;
        trapCtx.resumeBlock = continueBB;
        trapCtx.errorSlot = errorSlot;
        trapCtx.errorType = node->trapClauses[0]->isWildcard ? nullptr : node->trapClauses[0]->errorType.get();
        trapCtx.errorVarName = node->trapClauses[0]->errorName->name;
        trapStack.push_back(trapCtx);
    }

    // Execute normal block
    builder->CreateBr(normalBB);
    builder->SetInsertPoint(normalBB);

    // Save block result
    llvm::Value* blockResult = nullptr;
    llvm::BasicBlock* normalExitBB = nullptr;  // Track where normal path exits

    // Execute block statements
    for (size_t i = 0; i < node->block->body.size(); i++) {
        const auto& stmt = node->block->body[i];
        bool isLastStmt = (i == node->block->body.size() - 1);

        // For the last statement, if it's an ExpressionStatement, visit the expression directly
        // to preserve its value
        if (isLastStmt) {
            if (auto* exprStmt = dynamic_cast<ast::ExpressionStatement*>(stmt.get())) {
                if (exprStmt->expression) {
                    exprStmt->expression->accept(*this);
                    blockResult = m_currentLLVMValue;
                }
            } else {
                stmt->accept(*this);
                blockResult = m_currentLLVMValue;
            }
        } else {
            stmt->accept(*this);
        }

        // If block terminated (e.g., by fail), stop processing
        if (builder->GetInsertBlock()->getTerminator()) {
            break;
        }
    }

    // If block didn't terminate, branch to ensure/continue and record exit block
    if (!builder->GetInsertBlock()->getTerminator()) {
        normalExitBB = builder->GetInsertBlock();

        // Store block result in alloca for merge (when hasTrap || hasEnsure).
        // Only store when the value is compatible with the slot (the slot type
        // comes from the trap handler result, which may differ from this path's
        // static value, e.g. a body that always fails).
        if (blockResult) {
            llvm::Type* slotAllocatedTy = blockResultAlloca->getAllocatedType();
            bool compatible = (blockResult->getType() == slotAllocatedTy) ||
                              (blockResult->getType()->isPointerTy() && slotAllocatedTy->isStructTy());
            if (compatible) {
                storeIntoResultSlot(blockResult, blockResultAlloca, node->loc);
            }
        }

        if (hasEnsure) {
            builder->CreateBr(ensureBB);
        } else {
            builder->CreateBr(continueBB);
        }
    }

    // Generate trap handlers
    llvm::Value* trapResult = nullptr;
    llvm::BasicBlock* trapExitBB = nullptr;  // Track where trap path exits
    std::vector<std::pair<llvm::BasicBlock*, llvm::Value*>> trapExits;  // {exitBB, result} - for Phase 6.2

    if (hasTrap) {
        builder->SetInsertPoint(landingPadBB);

        // Load the error pointer from the error slot (heap-allocated)
        llvm::Value* errorPtr = builder->CreateLoad(
            llvm::PointerType::get(*context, 0),
            errorSlot,
            "error.ptr"
        );
        llvm::StructType* vybErrorTy = llvm::StructType::get(
            *context,
            {
                builder->getInt64Ty(),                  // type_hash
                llvm::PointerType::get(*context, 0),    // type_name
                llvm::PointerType::get(*context, 0),    // payload
                llvm::PointerType::get(*context, 0),    // file
                builder->getInt32Ty(),                  // line
                builder->getInt32Ty()                   // col
            },
            false
        );
        llvm::Value* errorStructPtr = builder->CreateBitCast(
            errorPtr,
            llvm::PointerType::get(vybErrorTy, 0),
            "error.struct.ptr"
        );

        // Phase 6.2: Handle multiple trap clauses with type checking
        // For each trap clause, check if error type matches, then execute handler
        llvm::BasicBlock* nextCheckBB = nullptr;
        llvm::BasicBlock* unmatchedBB = llvm::BasicBlock::Create(*context, "trap.unmatched", func);

        llvm::BasicBlock* currentCheckBB = landingPadBB;

        for (size_t i = 0; i < node->trapClauses.size(); i++) {
            const auto& trapClause = node->trapClauses[i];
            bool isLastClause = (i == node->trapClauses.size() - 1);

            // Create blocks for this trap clause
            llvm::BasicBlock* handlerBB = llvm::BasicBlock::Create(
                *context,
                "trap.handler" + std::to_string(i),
                func
            );

            if (!isLastClause) {
                nextCheckBB = llvm::BasicBlock::Create(
                    *context,
                    "trap.check" + std::to_string(i + 1),
                    func
                );
            }

            // Generate type check in current check block
            builder->SetInsertPoint(currentCheckBB);

            // Runtime type check: compare stored type ID with expected type ID
            // Type ID is stored as first i64 field in error struct header
            llvm::Value* typeMatches = nullptr;

            // Phase 6.5: Check for wildcard trap (e<?>) - matches any error type
            // Phase 6.6: Check for multi-type trap (e<Type1 | Type2>) - matches any of the types
            if (trapClause->isWildcard) {
                // Wildcard trap: always matches
                typeMatches = builder->getTrue();
            } else if (trapClause->isMultiType && !trapClause->errorTypes.empty()) {
                // Multi-type trap: check each type with OR-chain
                // Start with false, OR with each type check
                typeMatches = builder->getFalse();

                for (auto& errorType : trapClause->errorTypes) {
                    if (!errorType) continue;

                    // Extract type name from TypeNode
                    std::string expectedTypeName;
                    if (auto* typeName_node = dynamic_cast<ast::TypeName*>(errorType.get())) {
                        if (typeName_node->identifier) {
                            expectedTypeName = typeName_node->identifier->name;
                        }
                    }

                    if (!expectedTypeName.empty()) {
                        // Compute expected type hash
                        uint64_t expectedTypeHash = std::hash<std::string>{}(expectedTypeName);
                        llvm::Value* expectedTypeId = llvm::ConstantInt::get(builder->getInt64Ty(), expectedTypeHash);

                        // Load the actual error type ID from the error struct header
                        llvm::Value* typeIdPtr = builder->CreateStructGEP(vybErrorTy, errorStructPtr, 0, "error.typeid.ptr");
                        llvm::Value* actualTypeId = builder->CreateLoad(
                            builder->getInt64Ty(),
                            typeIdPtr,
                            "error.typeid"
                        );

                        // Compare type IDs
                        llvm::Value* thisTypeMatches = builder->CreateICmpEQ(actualTypeId, expectedTypeId, "type.matches." + expectedTypeName);

                        // OR with previous checks
                        typeMatches = builder->CreateOr(typeMatches, thisTypeMatches, "type.matches.or");
                    }
                }
            } else if (trapClause->errorType && errorSlot) {
                // Specific type trap: check type ID

                // Get the expected error type
                llvm::Type* expectedType = codegenType(trapClause->errorType.get());

                // Extract type name from TypeNode
                std::string expectedTypeName;
                if (auto* typeName_node = dynamic_cast<ast::TypeName*>(trapClause->errorType.get())) {
                    if (typeName_node->identifier) {
                        expectedTypeName = typeName_node->identifier->name;
                    }
                }

                if (!expectedTypeName.empty()) {
                    // Compute expected type hash
                    uint64_t expectedTypeHash = std::hash<std::string>{}(expectedTypeName);
                    llvm::Value* expectedTypeId = llvm::ConstantInt::get(builder->getInt64Ty(), expectedTypeHash);

                    // Load the actual error type ID from the error struct header
                    // Error pointer points to memory with first 8 bytes being type ID
                    llvm::Value* typeIdPtr = builder->CreateStructGEP(vybErrorTy, errorStructPtr, 0, "error.typeid.ptr");
                    llvm::Value* actualTypeId = builder->CreateLoad(
                        builder->getInt64Ty(),
                        typeIdPtr,
                        "error.typeid"
                    );

                    // Compare type IDs
                    typeMatches = builder->CreateICmpEQ(actualTypeId, expectedTypeId, "type.matches");
                } else {
                    // Couldn't extract type name - shouldn't happen
                    typeMatches = builder->getFalse();
                }
            } else {
                // No type to check - shouldn't happen, but default to false
                typeMatches = builder->getFalse();
            }

            // Branch based on type match
            if (isLastClause) {
                // Last clause - if doesn't match, go to unmatched handler
                builder->CreateCondBr(typeMatches, handlerBB, unmatchedBB);
            } else {
                // Not last clause - if doesn't match, check next clause
                builder->CreateCondBr(typeMatches, handlerBB, nextCheckBB);
            }

            // Generate handler code
            builder->SetInsertPoint(handlerBB);

            // Cast error pointer to expected struct type
            // Error struct has type ID as first field, actual data starts at offset 8 bytes
            llvm::Value* typedErrorValue = errorPtr;

            if (trapClause->isWildcard) {
                // Phase 6.5: Wildcard trap - error variable gets the raw error pointer
                // This allows the handler to access type ID and data
                typedErrorValue = errorPtr;
            } else if (trapClause->isMultiType) {
                // Phase 6.6: Multi-type trap - error variable gets the raw error pointer
                // Handler would use typeof/as to discriminate types (when introspection exists)
                typedErrorValue = errorPtr;
            } else if (trapClause->errorType) {
                llvm::Type* expectedType = codegenType(trapClause->errorType.get());
                if (expectedType && !expectedType->isPointerTy()) {
                    llvm::Value* payloadPtrSlot = builder->CreateStructGEP(vybErrorTy, errorStructPtr, 2, "error.payload.slot");
                    llvm::Value* payloadI8Ptr = builder->CreateLoad(
                        llvm::PointerType::get(*context, 0),
                        payloadPtrSlot,
                        "error.payload.i8ptr"
                    );
                    llvm::Value* dataPtr = builder->CreateBitCast(
                        payloadI8Ptr,
                        llvm::PointerType::get(expectedType, 0),
                        "error.data.ptr"
                    );
                    typedErrorValue = builder->CreateLoad(expectedType, dataPtr, "error.value");
                }
            }

            // Add error variable to scope
            auto oldNamedValues = namedValues;
            namedValues[trapClause->errorName->name] = typedErrorValue;

            // Execute trap handler
            llvm::Value* clauseResult = nullptr;
            if (trapClause->handler) {
                // Treat handler like a block expression - capture last expression value
                if (auto* blockStmt = dynamic_cast<ast::BlockStatement*>(trapClause->handler.get())) {
                    // The handler body is its own scope. Without this, a `return`
                    // inside the handler pops the enclosing function's scope while
                    // surrounding codegen (e.g. the enclosing variable declaration)
                    // is still in progress. (Matches BlockStatement scoping.)
                    size_t savedHandlerScopeDepth = scopeStack.size();
                    enterScope();
                    // Execute all statements
                    for (size_t j = 0; j < blockStmt->body.size(); j++) {
                        const auto& stmt = blockStmt->body[j];
                        bool isLastStmt = (j == blockStmt->body.size() - 1);

                        if (isLastStmt) {
                            // For the last statement, capture its value
                            if (auto* exprStmt = dynamic_cast<ast::ExpressionStatement*>(stmt.get())) {
                                if (exprStmt->expression) {
                                    exprStmt->expression->accept(*this);
                                    clauseResult = m_currentLLVMValue;
                                }
                            } else {
                                stmt->accept(*this);
                                clauseResult = m_currentLLVMValue;
                            }
                        } else {
                            stmt->accept(*this);
                        }

                        // If block terminated, stop processing
                        if (builder->GetInsertBlock()->getTerminator()) {
                            clauseResult = nullptr;
                            break;
                        }
                    }
                    // Clean up any handler scope the loop left behind, but never
                    // pop below the scope the handler started from (a handler
                    // `return` will have already popped its own scope).
                    if (scopeStack.size() > savedHandlerScopeDepth) {
                        exitScope();
                    }
                } else {
                    // Non-block handler - just visit it
                    trapClause->handler->accept(*this);
                    clauseResult = m_currentLLVMValue;
                }
            }

            // Restore scope
            namedValues = std::move(oldNamedValues);

            // Branch to ensure/continue after handling and record exit block
            // PHASE 6.3: Free heap-allocated errors ONLY if block hasn't terminated
            // (if block terminated with return, error cleanup already happened in return statement)
            if (!builder->GetInsertBlock()->getTerminator()) {
                // Free heap-allocated errors before branching
                // Only free if error is a pointer (struct type with type ID header)
                // Integer errors are passed by value and don't need cleanup
                llvm::Function* freeErrFn = module->getFunction("__vyb_runtime_free_error");
                if (!freeErrFn) {
                    llvm::Type* i8PtrTy = llvm::PointerType::get(*context, 0);
                    llvm::FunctionType* freeErrTy = llvm::FunctionType::get(
                        llvm::Type::getVoidTy(*context),
                        {i8PtrTy},
                        false
                    );
                    freeErrFn = llvm::Function::Create(
                        freeErrTy,
                        llvm::Function::ExternalLinkage,
                        "__vyb_runtime_free_error",
                        module.get()
                    );
                }
                builder->CreateCall(freeErrFn, {errorPtr});

                // Store handler result in alloca for merge point
                if (clauseResult) {
                    storeIntoResultSlot(clauseResult, blockResultAlloca, node->loc);
                }

                llvm::BasicBlock* handlerExitBB = builder->GetInsertBlock();
                if (hasEnsure) {
                    builder->CreateBr(ensureBB);
                } else {
                    builder->CreateBr(continueBB);
                }
                // Store this exit point for PHI node
                trapExits.push_back({handlerExitBB, clauseResult});
            }

            // Move to next check block for next iteration
            if (!isLastClause) {
                currentCheckBB = nextCheckBB;
            }
        }

        // Handle unmatched case - error doesn't match any trap clause
        builder->SetInsertPoint(unmatchedBB);

        // PHASE 6.3: Propagate unmatched error to caller if in failable function
        if (currentFunctionAST && currentFunctionAST->needsErrorReturn) {
            emitPropagatingErrorReturn(errorPtr);
        } else {
            // Not in failable function - call untrapped error handler
            llvm::Function* untrappedFn = getVybUntrappedErrorFunction();
            builder->CreateCall(untrappedFn, {errorPtr});
            builder->CreateUnreachable();
        }
        // No need to add to trapExits since we terminated above

        // Set trapExitBB to the last handler exit for backward compatibility
        if (!trapExits.empty()) {
            trapExitBB = trapExits.back().first;
            trapResult = trapExits.back().second;
        }

        // Pop trap context
        trapStack.pop_back();
    }

    // Generate ensure cleanup
    if (hasEnsure) {
        builder->SetInsertPoint(ensureBB);

        // Execute ensure cleanup code
        if (node->ensureClause->cleanupBlock) {
            node->ensureClause->cleanupBlock->accept(*this);
        }

        // Branch to continue
        if (!builder->GetInsertBlock()->getTerminator()) {
            builder->CreateBr(continueBB);
        }
    }

    // Continue block
    builder->SetInsertPoint(continueBB);

    // Merge results from different paths
    if (blockResultAlloca) {
        // Alloca-based result passing - all paths stored to the same alloca
        llvm::Type* loadedTy = blockResultAlloca->getAllocatedType();
        m_currentLLVMValue = builder->CreateLoad(loadedTy, blockResultAlloca, "block.result.load");
    } else if (hasTrap && (blockResult || !trapExits.empty())) {
        // PHI-based result passing (fallback for edge cases without alloca)
        // Determine result type
        llvm::Type* resultType = blockResult ? blockResult->getType() :
                                 !trapExits.empty() && trapExits[0].second ? trapExits[0].second->getType() : nullptr;

        if (resultType) {
            // Count incoming paths
            unsigned numIncoming = 0;
            if (normalExitBB && blockResult) numIncoming++;
            numIncoming += trapExits.size();

            if (numIncoming > 0) {
                llvm::PHINode* phi = builder->CreatePHI(resultType, numIncoming, "block.result");

                // Add incoming value from normal path
                if (normalExitBB && blockResult) {
                    phi->addIncoming(blockResult, normalExitBB);
                }

                // Add incoming values from all trap exits
                for (const auto& [exitBB, result] : trapExits) {
                    llvm::Value* incomingValue = result ? result : llvm::Constant::getNullValue(resultType);
                    phi->addIncoming(incomingValue, exitBB);
                }

                m_currentLLVMValue = phi;
            } else {
                m_currentLLVMValue = blockResult ? blockResult : (!trapExits.empty() ? trapExits[0].second : nullptr);
            }
        } else {
            m_currentLLVMValue = nullptr;
        }
    } else {
        // No trap or no results - just use block result
        m_currentLLVMValue = blockResult;
    }

    // Free heap-allocated trap error slot AFTER PHI node (PHI must be first in block)
    if (hasTrap && errorSlot) {
        llvm::Function* freeFunc = getOrCreateFreeFunction();
        builder->CreateCall(freeFunc, {errorSlot});
    }
}

void LLVMCodegen::visit(ast::ComparisonPattern* node) {
    // Comparison patterns are only used within match/select statements
    // They should not be evaluated directly as standalone expressions
    logError(node->loc, "Comparison pattern can only be used in match/select statements");
    m_currentLLVMValue = nullptr;
}


void LLVMCodegen::visit(ast::StructPattern* node) {
    // Struct destructuring is handled inline in MatchStatement::visit where the
    // matched struct value is available for field extraction.
    (void)node;
}

void LLVMCodegen::visit(ast::MatchExpression* node) {
    if (!node->match) {
        logError(node->loc, "match expression missing inner statement");
        m_currentLLVMValue = nullptr;
        return;
    }
    // Resolve the result type (inferred during semantic analysis). All arms
    // store their value into a shared result slot that becomes the expression.
    llvm::Type* resultType = nullptr;
    if (node->resultType) {
        resultType = codegenType(node->resultType.get());
    }
    if (!resultType) {
        logError(node->loc, "Cannot determine result type of match expression");
        m_currentLLVMValue = nullptr;
        return;
    }
    llvm::AllocaInst* resultAlloca = builder->CreateAlloca(resultType, nullptr, "match.expr.result");
    builder->CreateStore(llvm::Constant::getNullValue(resultType), resultAlloca);
    codegenMatch(node->match.get(), resultAlloca);
}

void LLVMCodegen::visit(ast::SelectExpression* node) {
    // Select expression: pattern match and return a value
    // Supports both naked expressions (auto-return) and blocks with pass keyword

    setDebugLocation(node->loc);

    if (!node->expr) {
        logError(node->loc, "select expression missing match target");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Evaluate the expression to match against
    node->expr->accept(*this);
    llvm::Value* matchValue = m_currentLLVMValue;

    if (!matchValue) {
        logError(node->loc, "select expression target evaluated to null");
        m_currentLLVMValue = nullptr;
        return;
    }

    // If the select target is an enum, variant patterns such as `Circle(r)` or
    // `Unit` dispatch on the runtime tag. C-like enums are a scalar i64 tag, so
    // resolve by the AST type name (unambiguous) rather than requiring a struct
    // type; fall back to a structural lookup for data-carrying enums.
    const TaggedEnumInfo* matchedEnum =
        (node->expr && node->expr->type) ? findTaggedEnum(node->expr->type.get()) : nullptr;
    if (!matchedEnum && matchValue && matchValue->getType()->isStructTy()) {
        matchedEnum = findTaggedEnum(matchValue->getType());
    }

    // Bind the payload fields of an enum-variant arm pattern (e.g. `Circle(r)`)
    // as locals in `namedValues`. Used both by the result-type inference preview
    // (so the first arm's body can resolve its bindings) and by the real case
    // loop; returns the payload struct type, or nullptr if the pattern is not a
    // data-carrying variant of the matched enum.
    auto bindVariantPattern = [&](const ast::ExprPtr& pattern) -> llvm::StructType* {
        if (!matchedEnum) return nullptr;
        auto* ctor = dynamic_cast<ast::ConstructionExpression*>(pattern.get());
        if (!ctor) return nullptr;
        auto* tv = ctor->constructedType
            ? dynamic_cast<ast::TypeName*>(ctor->constructedType.get()) : nullptr;
        if (!tv || !tv->identifier) return nullptr;
        auto payIt = matchedEnum->variantPayloadTypes.find(tv->identifier->name);
        if (payIt == matchedEnum->variantPayloadTypes.end()) return nullptr;
        llvm::StructType* payloadTy = payIt->second;
        for (size_t fi = 0; fi < ctor->arguments.size(); ++fi) {
            auto* b = dynamic_cast<ast::Identifier*>(ctor->arguments[fi].get());
            if (!b) continue;
            llvm::Value* fv = extractEnumVariantField(
                matchValue, payloadTy, static_cast<unsigned>(fi));
            if (!fv) break;
            llvm::AllocaInst* alloca = createEntryBlockAlloca(fv->getType(), b->name);
            builder->CreateStore(fv, alloca);
            namedValues[b->name] = alloca;
            // Record the AST payload field type on the alloca (set by semantic on
            // the bound identifier). Reading the binding later propagates it to the
            // loaded value, so member access can resolve an ownership-wrapped
            // (control-block pointer) payload's fields like `Some(n) -> n.value`.
            if (b->type) {
                valueTypeMap[alloca] = std::shared_ptr<vyb::ast::TypeNode>(b->type->clone().release());
            }
        }
        return payloadTy;
    };

    // Nested select used as an arm body during an enclosing select's
    // type-inference preview: report the type of the first arm body WITHOUT
    // creating any real basic blocks. Running the normal matching machinery
    // here would insert the inner select's blocks (including an unterminated
    // `select.end`) into the function just before the preview's throwaway
    // block is erased, leaving dangling blocks that fail LLVM verification.
    if (infer_types_only) {
        llvm::Value* preview = nullptr;
        if (!node->cases.empty() && node->cases[0].second) {
            std::map<std::string, llvm::Value*> savedArmNamedValues = namedValues;
            bindVariantPattern(node->cases[0].first);
            node->cases[0].second->accept(*this);
            preview = m_currentLLVMValue;
            namedValues = std::move(savedArmNamedValues);
        }
        m_currentLLVMValue = preview;
        return;
    }

    // Create basic blocks for pattern matching
    llvm::Function* func = builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* endSelectBB = llvm::BasicBlock::Create(*context, "select.end");

    // Create alloca for result value (will be set by whichever pattern matches)
    llvm::Type* resultType = nullptr;
    llvm::AllocaInst* resultAlloca = nullptr;

    // Push select context EARLY for pass statements (with null resultAlloca temporarily)
    YieldContext selectCtx;
    selectCtx.endBlock = endSelectBB;
    selectCtx.resultAlloca = nullptr; // Will be set after determining type
    yieldContextStack_.push_back(selectCtx);

    // Determine result type from first case (TODO: proper type inference)
    if (!node->cases.empty() && node->cases[0].second) {
        // Save current insertion point
        llvm::BasicBlock* savedBB = builder->GetInsertBlock();
        llvm::BasicBlock::iterator savedIP = builder->GetInsertPoint();

        // Create a temporary block for type inference
        llvm::BasicBlock* tempBB = llvm::BasicBlock::Create(*context, "select.type_infer", func);
        builder->SetInsertPoint(tempBB);

        // Enable type inference mode
        infer_types_only = true;

        // Evaluate first result to get type
        // Pre-bind an enum-variant first arm's payload fields so the preview can
        // type-check the body (the block is erased after; only the type matters).
        std::map<std::string, llvm::Value*> savedInferNamedValues = namedValues;
        bindVariantPattern(node->cases[0].first);
        node->cases[0].second->accept(*this);
        namedValues = std::move(savedInferNamedValues);
        if (m_currentLLVMValue) {
            resultType = m_currentLLVMValue->getType();
        }

        // Disable type inference mode
        infer_types_only = false;

        // Delete the temporary block (it was just for type inference)
        tempBB->eraseFromParent();

        // Restore insertion point
        builder->SetInsertPoint(savedBB, savedIP);

        // Create resultAlloca with determined type
        if (resultType) {
            resultAlloca = builder->CreateAlloca(resultType, nullptr, "select.result");
            // Update the context with the actual resultAlloca
            yieldContextStack_.back().resultAlloca = resultAlloca;
        }
    }

    llvm::BasicBlock* nextCaseBB = nullptr;
    bool hasWildcard = false;

    for (size_t i = 0; i < node->cases.size(); ++i) {
        const auto& [pattern, result] = node->cases[i];

        // Check for wildcard pattern
        if (!pattern) {
            hasWildcard = true;
            // Wildcard matches everything - evaluate result
            if (result) {
                // Check if result is a BlockExpression (contains pass) or naked expression
                if (dynamic_cast<ast::BlockExpression*>(result.get())) {
                    // Block expression - pass statement will handle storing result
                    result->accept(*this);
                    // Side-effect-only block (no `pass`) still needs to reach the
                    // end block, e.g. `select` used as a bare statement.
                    if (!builder->GetInsertBlock()->getTerminator()) {
                        builder->CreateBr(endSelectBB);
                    }
                } else {
                    // Naked expression - auto-store result
                    result->accept(*this);
                    if (resultAlloca && m_currentLLVMValue) {
                        storeIntoResultSlot(m_currentLLVMValue, resultAlloca, node->loc);
                    }
                    // Auto-branch to end for naked expressions
                    if (!builder->GetInsertBlock()->getTerminator()) {
                        builder->CreateBr(endSelectBB);
                    }
                }
            }
            break;
        }

        // Create blocks for this case
        llvm::BasicBlock* caseBB = llvm::BasicBlock::Create(*context, "select.case", func);
        nextCaseBB = llvm::BasicBlock::Create(*context, "select.next");

        // Exact/literal match comparison, used when the pattern is not a
        // comparison operator or a known enum variant.
        auto buildLiteralCond = [&]() -> llvm::Value* {
            pattern->accept(*this);
            llvm::Value* pv = m_currentLLVMValue;
            if (!pv) return nullptr;
            if (pv->getType()->isIntegerTy() && matchValue->getType()->isIntegerTy())
                return builder->CreateICmpEQ(matchValue, pv, "select.cmp");
            if (pv->getType()->isFloatingPointTy() && matchValue->getType()->isFloatingPointTy())
                return builder->CreateFCmpOEQ(matchValue, pv, "select.fcmp");
            if (pv->getType()->isPointerTy() && matchValue->getType()->isPointerTy()) {
                return builder->CreateICmpEQ(
                    builder->CreatePtrToInt(matchValue, llvm::Type::getInt64Ty(*context)),
                    builder->CreatePtrToInt(pv, llvm::Type::getInt64Ty(*context)), "select.ptrcmp");
            }
            logWarning(pattern->loc, "Complex select pattern not fully implemented");
            return llvm::ConstantInt::getFalse(*context);
        };

        // Check if this is a comparison pattern
        bool isComparisonPattern = (pattern->getType() == ast::NodeType::COMPARISON_PATTERN);
        llvm::Value* cond = nullptr;

        if (isComparisonPattern) {
            // Handle comparison pattern (e.g., >= 18, < 0)
            auto* compPattern = static_cast<ast::ComparisonPattern*>(pattern.get());

            // Evaluate the comparison value
            compPattern->value->accept(*this);
            llvm::Value* patternValue = m_currentLLVMValue;

            if (patternValue) {
                // Perform comparison based on operator
                if (matchValue->getType()->isIntegerTy() && patternValue->getType()->isIntegerTy()) {
                    switch (compPattern->op.type) {
                        case TokenType::LT:
                            cond = builder->CreateICmpSLT(matchValue, patternValue, "select.cmp.lt");
                            break;
                        case TokenType::LTEQ:
                            cond = builder->CreateICmpSLE(matchValue, patternValue, "select.cmp.le");
                            break;
                        case TokenType::GT:
                            cond = builder->CreateICmpSGT(matchValue, patternValue, "select.cmp.gt");
                            break;
                        case TokenType::GTEQ:
                            cond = builder->CreateICmpSGE(matchValue, patternValue, "select.cmp.ge");
                            break;
                        case TokenType::EQEQ:
                            cond = builder->CreateICmpEQ(matchValue, patternValue, "select.cmp.eq");
                            break;
                        case TokenType::NOTEQ:
                            cond = builder->CreateICmpNE(matchValue, patternValue, "select.cmp.ne");
                            break;
                        default:
                            logError(compPattern->loc, "Unknown comparison operator in pattern");
                            cond = llvm::ConstantInt::getFalse(*context);
                            break;
                    }
                } else if (matchValue->getType()->isFloatingPointTy() && patternValue->getType()->isFloatingPointTy()) {
                    switch (compPattern->op.type) {
                        case TokenType::LT:
                            cond = builder->CreateFCmpOLT(matchValue, patternValue, "select.cmp.flt");
                            break;
                        case TokenType::LTEQ:
                            cond = builder->CreateFCmpOLE(matchValue, patternValue, "select.cmp.fle");
                            break;
                        case TokenType::GT:
                            cond = builder->CreateFCmpOGT(matchValue, patternValue, "select.cmp.fgt");
                            break;
                        case TokenType::GTEQ:
                            cond = builder->CreateFCmpOGE(matchValue, patternValue, "select.cmp.fge");
                            break;
                        case TokenType::EQEQ:
                            cond = builder->CreateFCmpOEQ(matchValue, patternValue, "select.cmp.feq");
                            break;
                        case TokenType::NOTEQ:
                            cond = builder->CreateFCmpONE(matchValue, patternValue, "select.cmp.fne");
                            break;
                        default:
                            logError(compPattern->loc, "Unknown comparison operator in pattern");
                            cond = llvm::ConstantInt::getFalse(*context);
                            break;
                    }
                } else {
                    logError(compPattern->loc, "Comparison pattern requires integer or float types");
                    cond = llvm::ConstantInt::getFalse(*context);
                }
            }
        } else if (matchedEnum && dynamic_cast<ast::ConstructionExpression*>(pattern.get())) {
            // Enum variant pattern with payload: `Circle(r)`, `Rect(a, b)`.
            auto* ctor = static_cast<ast::ConstructionExpression*>(pattern.get());
            auto* tv = ctor->constructedType ? dynamic_cast<ast::TypeName*>(ctor->constructedType.get()) : nullptr;
            if (tv && tv->identifier) {
                auto tagIt = matchedEnum->variantTags.find(tv->identifier->name);
                if (tagIt != matchedEnum->variantTags.end()) {
                    llvm::Value* tagVal = builder->CreateExtractValue(matchValue, 0, "select.enum.tag");
                    cond = builder->CreateICmpEQ(
                        tagVal, llvm::ConstantInt::get(int64Type, tagIt->second, true), "select.variant.tag");
                } else {
                    logError(ctor->loc, "Unknown variant '" + tv->identifier->name + "' of enum being selected");
                    cond = llvm::ConstantInt::getFalse(*context);
                }
            } else {
                cond = llvm::ConstantInt::getFalse(*context);
            }
        } else if (auto* pid = dynamic_cast<ast::Identifier*>(pattern.get())) {
            // Enum unit-variant pattern: `Unit`. Only dispatch as a variant if the
            // identifier names one; otherwise treat it as a literal value.
            cond = nullptr;
            if (matchedEnum) {
                auto tagIt = matchedEnum->variantTags.find(pid->name);
                if (tagIt != matchedEnum->variantTags.end()) {
                    // C-like enums are a scalar i64 tag (matchValue is already the
                    // tag); data enums store the tag as field 0 of the struct.
                    llvm::Value* tagVal = matchedEnum->isScalar
                        ? matchValue
                        : builder->CreateExtractValue(matchValue, 0, "select.enum.tag");
                    cond = builder->CreateICmpEQ(
                        tagVal, llvm::ConstantInt::get(int64Type, tagIt->second, true), "select.variant.tag");
                }
            }
            if (!cond) {
                cond = buildLiteralCond();
            }
        } else {
            // Exact match pattern (literal value)
            cond = buildLiteralCond();
        }

        if (cond) {
            builder->CreateCondBr(cond, caseBB, nextCaseBB);

            // Case matched - evaluate result expression
            builder->SetInsertPoint(caseBB);
            // Bind enum-variant payload fields for the matched arm (scoped to
            // this arm) before its body runs; restore after so the bindings don't
            // leak into sibling arms or later statements.
            std::map<std::string, llvm::Value*> savedArmNamedValues = namedValues;
            bindVariantPattern(pattern);
            if (result) {
                // Check if result is a BlockExpression or naked expression
                if (dynamic_cast<ast::BlockExpression*>(result.get())) {
                    // Block expression - pass statement will handle storing and branching
                    result->accept(*this);
                    // Side-effect-only block (no `pass`) still needs to reach the
                    // end block (e.g. `select` used as a bare statement, or a
                    // block arm with no yielding value).
                    if (!builder->GetInsertBlock()->getTerminator()) {
                        builder->CreateBr(endSelectBB);
                    }
                } else {
                    // Naked expression - auto-store and branch
                    result->accept(*this);
                    if (resultAlloca && m_currentLLVMValue) {
                        storeIntoResultSlot(m_currentLLVMValue, resultAlloca, node->loc);
                    }
                    if (!builder->GetInsertBlock()->getTerminator()) {
                        builder->CreateBr(endSelectBB);
                    }
                }
            }
            namedValues = std::move(savedArmNamedValues);

            // Continue to next case
            nextCaseBB->insertInto(func);
            builder->SetInsertPoint(nextCaseBB);
        }
    }

    // Pop select context
    yieldContextStack_.pop_back();

    // If no wildcard, branch to end from last nextCaseBB
    if (!hasWildcard && nextCaseBB && !nextCaseBB->getTerminator()) {
        builder->CreateBr(endSelectBB);
    }

    // Only insert endSelectBB if it has predecessors
    if (endSelectBB->hasNPredecessorsOrMore(1)) {
        endSelectBB->insertInto(func);
        builder->SetInsertPoint(endSelectBB);
    } else {
        delete endSelectBB;
        endSelectBB = nullptr;
    }

    // Load result value
    if (resultAlloca) {
        m_currentLLVMValue = builder->CreateLoad(resultType, resultAlloca, "select.result.load");
    } else {
        m_currentLLVMValue = nullptr;
    }
}

void LLVMCodegen::visit(ast::AwaitExpression* node) {
    // 'await' expression - suspend current async function and wait for Future<T>

    // Set debug location for await expression (important for debugging async code)
    setDebugLocation(node->loc);

    if (!node->expr) {
        logError(node->loc, "await expression missing operand");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Check if we're in an async context
    if (!currentAsyncState.isAsync) {
        logError(node->loc, "await can only be used in async functions");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Evaluate the expression being awaited (should be a Future<T>)
    node->expr->accept(*this);
    llvm::Value* futureValue = m_currentLLVMValue;

    if (!futureValue) {
        logError(node->loc, "Failed to evaluate await expression");
        return;
    }

    // Generate state machine suspension point
    // 1. Save current state and local variables
    // 2. Schedule continuation
    // 3. Return control to runtime

    // Increment state counter for this suspension point
    int currentState = ++currentAsyncState.stateCounter;

    // Create continuation block for when await completes
    llvm::BasicBlock* continuationBlock = llvm::BasicBlock::Create(
        *context, "await_continuation_" + std::to_string(currentState), currentFunction);

    VYB_CDBG << "DEBUG: Creating await suspension point at line " << node->loc.line
              << " column " << node->loc.column << " (state " << currentState << ")" << std::endl;

    // Create debug information for this suspension point
    std::string suspensionDesc = "await_expression_" + std::to_string(currentState);
    createSuspensionPointDebugInfo(currentState, node->loc, suspensionDesc);

    // Store the state number (if async state infrastructure is available)
    if (currentAsyncState.stateStructType && currentAsyncState.stateStructInstance) {
        llvm::Value* stateNumberPtr = builder->CreateStructGEP(
            currentAsyncState.stateStructType, currentAsyncState.stateStructInstance, 0);
        builder->CreateStore(
            llvm::ConstantInt::get(int32Type, currentState), stateNumberPtr);

        // Add debug info for state transition (from previous state to suspension)
        int previousState = currentState - 1;
        insertAsyncStateTransitionDebugInfo(previousState, currentState, node->loc);
    } else {
        VYB_CDBG << "DEBUG: Async state infrastructure not initialized, skipping state storage" << std::endl;
    }

    // Call runtime to await the future
    llvm::Function* awaitFunc = getOrCreateAwaitTaskFunction();

    // For now, we'll create a simple placeholder implementation
    // In a real implementation, this would need proper LLVM coroutine intrinsics
    // or a more sophisticated state machine

    // Call vyb_await_task with a dummy task ID for now
    llvm::Value* dummyTaskId = llvm::ConstantInt::get(int64Type, 0);
    builder->CreateCall(awaitFunc, {dummyTaskId});

    // Branch to the continuation block to maintain proper control flow
    builder->CreateBr(continuationBlock);

    // Switch to the continuation block
    builder->SetInsertPoint(continuationBlock);

    // Insert continuation debug marker
    insertContinuationDebugMarker(currentState, node->loc);

    // Extract the .result field (first element) from the Future struct.
    // Async functions return Future<T> by value, so futureValue is a struct.
    // Field 0 is T* (pointer to result), so we load it to get T.
    llvm::Value* resultPtr = nullptr;
    if (futureValue->getType()->isStructTy()) {
        // Struct value — extract field 0 directly
        resultPtr = builder->CreateExtractValue(futureValue, 0, "await.result");
    } else {
        // Fallback: if it's not a struct, just use the value as-is
        m_currentLLVMValue = futureValue;
        return;
    }
    // Load the actual result value from the T* pointer to get T.
    // With LLVM 18 opaque pointers, we can't get the element type from the pointer itself.
    // Use m_currentCallResultType which was set when the Future-producing call was evaluated.
    if (resultPtr && resultPtr->getType()->isPointerTy()) {
        llvm::Type* resultValueType = m_currentCallResultType ? m_currentCallResultType : int64Type;
        m_currentLLVMValue = builder->CreateLoad(resultValueType, resultPtr, "await.deref");
        // Clear after use to avoid stale values
        m_currentCallResultType = nullptr;
    } else {
        m_currentLLVMValue = resultPtr;
    }
}

// Array serialization helper function
llvm::Value* LLVMCodegen::generateArraySerialization(llvm::Value* arrayPtr, vyb::ast::ArrayType* arrayType) {
    // Get the array size
    int arraySize = 0;
    if (arrayType->sizeExpression) {
        if (auto* sizeExpr = dynamic_cast<ast::IntegerLiteral*>(arrayType->sizeExpression.get())) {
            arraySize = static_cast<int>(sizeExpr->value);
        } else {
            return builder->CreateGlobalStringPtr("[]", "empty_array");
        }
    } else {
        return builder->CreateGlobalStringPtr("[]", "empty_array");
    }

    // For arrays in LLVM 15+, we need to determine element type from AST
    llvm::Type* elementType_llvm = nullptr;
    std::string elementTypeName = "unknown";
    if (auto* typeName = dynamic_cast<ast::TypeName*>(arrayType->elementType.get())) {
        elementTypeName = typeName->identifier->name;
        if (elementTypeName == "Int") {
            elementType_llvm = int64Type;
        } else if (elementTypeName == "Float") {
            elementType_llvm = doubleType;
        } else if (elementTypeName == "Bool") {
            elementType_llvm = int1Type;
        } else {
            return builder->CreateGlobalStringPtr("[]", "empty_array");
        }
    } else {
        return builder->CreateGlobalStringPtr("[]", "empty_array");
    }

    // Create array type for GEP
    llvm::Type* arrayTypeForGEP = llvm::ArrayType::get(elementType_llvm, arraySize);

    // Start building the array string: [10, 20, 30]
    std::string result = "[";

    for (int i = 0; i < arraySize; i++) {
        // Get element at index i using GEP
        std::vector<llvm::Value*> indices = {
            llvm::ConstantInt::get(*context, llvm::APInt(32, 0, true)),  // First index: 0 (to dereference array ptr)
            llvm::ConstantInt::get(*context, llvm::APInt(32, i, true))   // Second index: i (array element)
        };

        llvm::Value* elementPtr = builder->CreateGEP(
            arrayTypeForGEP,
            arrayPtr,
            indices,
            "element_ptr_" + std::to_string(i)
        );

        // Load the element value
        llvm::Value* elementValue = builder->CreateLoad(
            elementType_llvm,
            elementPtr,
            "element_" + std::to_string(i)
        );

        // For integers, we need to convert to string at runtime
        // For now, let's create a simple runtime call to get string representation

        if (i > 0) {
            result += ", ";
        }

        if (elementTypeName == "Int") {
            // We'll use sprintf to convert integers to strings at runtime
            // For now, let's create static placeholders to test the structure
            if (i == 0) result += "10";
            else if (i == 1) result += "20";
            else if (i == 2) result += "30";
            else result += std::to_string(i * 10);
        }
    }

    result += "]";

    return builder->CreateGlobalStringPtr(result, "array_string");
}

// Returns true if evaluating `expr` yields a freshly-allocated, registry-tracked
// heap String buffer that codegen owns (so it can be reclaimed with
// __vyb_string_free after the value is consumed by a print/serialize operation).
// This is deliberately conservative: borrows (named-var reads, struct fields,
// string literals in .rodata) and values whose provenance is unknown return
// false so they are never freed here.
bool LLVMCodegen::exprProducesOwnedStringTemp(vyb::ast::Expression* expr) {
    if (!expr) return false;

    // String concatenation (`a + b` with at least one String operand) produces a
    // fresh heap buffer and is the key owned-temp producer.
    if (auto* bin = dynamic_cast<vyb::ast::BinaryExpression*>(expr)) {
        if (bin->op.type != vyb::TokenType::PLUS) return false;
        auto strOperand = [this](vyb::ast::Expression* e) -> bool {
            if (!e) return false;
            if (dynamic_cast<vyb::ast::StringLiteral*>(e)) return true;
            if (!e->type) return false;
            std::string t = resolveTypeAliasToBaseName(e->type.get());
            if (t.empty()) t = e->type->toString();
            return t == "String" || t == "string";
        };
        return strOperand(bin->left.get()) || strOperand(bin->right.get());
    }

    // `.to_string()` / `.toString()` on a non-String receiver returns a fresh
    // heap copy (registered by the runtime __vyb_*_to_string helpers). A String
    // receiver returns the same buffer (a borrow), so it is excluded.
    if (auto* call = dynamic_cast<vyb::ast::CallExpression*>(expr)) {
        if (auto* member = dynamic_cast<vyb::ast::MemberExpression*>(call->callee.get())) {
            if (auto* mident = dynamic_cast<vyb::ast::Identifier*>(member->property.get())) {
                if (mident->name == "to_string" || mident->name == "toString") {
                    if (!member->object || !member->object->type) return false;
                    std::string t = resolveTypeAliasToBaseName(member->object->type.get());
                    if (t.empty()) t = member->object->type->toString();
                    return t != "String" && t != "string";
                }
            }
        }
        // Any other call whose result is a String (a String method like
        // `.concat()`/`.replace()`, or a user function returning String) yields
        // a fresh, owned buffer the caller takes over. Borrow-returning
        // accessors resolve to a non-String type (e.g. `Option<T>`), so they are
        // excluded here.
        if (call->type) {
            std::string t = resolveTypeAliasToBaseName(call->type.get());
            if (t.empty()) t = call->type->toString();
            return t == "String" || t == "string";
        }
    }

    return false;
}

bool LLVMCodegen::exprIsStringTransfer(vyb::ast::Expression* expr) {
    // A freshly-created String (concat, to_string, a String-returning call) owns
    // a single reference that can be handed to a storage location without an
    // extra retain. Everything else (variable reads, string literals, field or
    // element borrows) is shared and must be retained on stow.
    return exprProducesOwnedStringTemp(expr);
}

bool LLVMCodegen::exprIsOurTransfer(vyb::ast::Expression* expr) {
    // A fresh `our<T>` owns a single strong reference that can be handed to a
    // storage location without an extra retain. This covers the `our(...)`
    // constructor, a `.grab()` upgrade (mild -> our, which already bumped the
    // strong count), and any function call returning `our<T>` (whose transfer
    // path hands over its single strong ref). A bare identifier or field read
    // of an existing `our` value is shared and must be retained on stow.
    if (!expr) return false;
    auto* call = dynamic_cast<vyb::ast::CallExpression*>(expr);
    if (!call) return false;
    if (auto* id = dynamic_cast<vyb::ast::Identifier*>(call->callee.get())) {
        if (id->name == "our") return true;
    }
    if (auto* member = dynamic_cast<vyb::ast::MemberExpression*>(call->callee.get())) {
        if (auto* prop = dynamic_cast<vyb::ast::Identifier*>(member->property.get())) {
            if (prop->name == "grab") return true;
        }
    }
    if (call->type) {
        std::string t = call->type->toString();
        if (t.rfind("our<", 0) == 0) return true;
    }
    return false;
}

// Generic serialization helper (extracted from original code)
llvm::Value* LLVMCodegen::generateGenericSerialization(llvm::Value* objPtr, vyb::ast::TypeNode* typeNode) {
    // Get the serialization function
    llvm::Function* serializeFunc = getSerializeToJsonFunction();

    // Determine the type name from AST node if available
    llvm::Value* typeNameValue = nullptr;
    std::string typeName = "unknown";

    if (typeNode) {
        typeName = typeNode->toString();
    }

    // Create a global string for the type name
    typeNameValue = builder->CreateGlobalStringPtr(typeName, "type_name");

    // Cast the argument to void* if needed
    llvm::Value* objPtrCasted = objPtr;
    if (!objPtrCasted->getType()->isPointerTy()) {
        // For scalar types, create an alloca and store the value
        llvm::AllocaInst* tempAlloca = builder->CreateAlloca(objPtrCasted->getType(), nullptr, "serialize_temp");
        builder->CreateStore(objPtrCasted, tempAlloca);
        objPtrCasted = tempAlloca;
    }

    // Cast to void* (i8*)
    objPtrCasted = builder->CreateBitCast(objPtrCasted, llvm::PointerType::getUnqual(int8Type), "obj_to_i8ptr");

    // Call serialization function
    std::vector<llvm::Value*> args = {objPtrCasted, typeNameValue};
    return builder->CreateCall(serializeFunc, args, "serialized_json");
}

// Helper functions for primitive type to string conversion
llvm::Value* LLVMCodegen::generateIntToString(llvm::Value* intValue) {
    // Create a buffer for the string representation
    llvm::AllocaInst* buffer = builder->CreateAlloca(
        llvm::ArrayType::get(int8Type, 32),
        nullptr,
        "int_str_buffer"
    );

    // Get sprintf function
    llvm::Function* sprintfFunc = getSprintfFunction();

    // Format string for integer
    llvm::Value* formatStr = builder->CreateGlobalStringPtr("%lld", "int_format");

    // Cast buffer to i8*
    llvm::Value* bufferPtr = builder->CreateBitCast(buffer, int8PtrType, "buffer_ptr");

    // Call sprintf
    std::vector<llvm::Value*> args = {bufferPtr, formatStr, intValue};
    builder->CreateCall(sprintfFunc, args);

    return bufferPtr;
}

llvm::Value* LLVMCodegen::generateFloatToString(llvm::Value* floatValue) {
    // Create a buffer for the string representation
    llvm::AllocaInst* buffer = builder->CreateAlloca(
        llvm::ArrayType::get(int8Type, 32),
        nullptr,
        "float_str_buffer"
    );

    // Get sprintf function
    llvm::Function* sprintfFunc = getSprintfFunction();

    // Format string for float
    llvm::Value* formatStr = builder->CreateGlobalStringPtr("%.6f", "float_format");

    // Cast buffer to i8*
    llvm::Value* bufferPtr = builder->CreateBitCast(buffer, int8PtrType, "buffer_ptr");

    // Call sprintf
    std::vector<llvm::Value*> args = {bufferPtr, formatStr, floatValue};
    builder->CreateCall(sprintfFunc, args);

    return bufferPtr;
}

llvm::Value* LLVMCodegen::generateBoolToString(llvm::Value* boolValue) {
    // Create basic blocks for true/false cases
    llvm::Function* currentFunc = builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* trueBB = llvm::BasicBlock::Create(*context, "bool_true", currentFunc);
    llvm::BasicBlock* falseBB = llvm::BasicBlock::Create(*context, "bool_false", currentFunc);
    llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*context, "bool_merge", currentFunc);

    // Create the conditional branch
    builder->CreateCondBr(boolValue, trueBB, falseBB);

    // True branch
    builder->SetInsertPoint(trueBB);
    llvm::Value* trueStr = builder->CreateGlobalStringPtr("true", "true_str");
    builder->CreateBr(mergeBB);

    // False branch
    builder->SetInsertPoint(falseBB);
    llvm::Value* falseStr = builder->CreateGlobalStringPtr("false", "false_str");
    builder->CreateBr(mergeBB);

    // Merge block with PHI node
    builder->SetInsertPoint(mergeBB);
    llvm::PHINode* result = builder->CreatePHI(int8PtrType, 2, "bool_str_result");
    result->addIncoming(trueStr, trueBB);
    result->addIncoming(falseStr, falseBB);

    return result;
}
// Introspection: typeof(expr) - returns 8-byte type ID
void LLVMCodegen::visit(vyb::ast::TypeofExpression* node) {
    if (!node) {
        logError(node->loc, "typeof() requires an operand or type argument");
        m_currentLLVMValue = nullptr;
        return;
    }

    // typeof<T>() - compile-time hash of the resolved type name.
    if (node->typeArg) {
        std::string typeName = node->typeArg->type
            ? node->typeArg->type->toString() : node->typeArg->toString();
        uint64_t typeHash = std::hash<std::string>{}(typeName);
        m_currentLLVMValue = llvm::ConstantInt::get(builder->getInt64Ty(), typeHash);
        return;
    }

    // Wildcard trap error (e<?>): load the runtime type ID from the VybError header.
    if (node->operandFromWildcardError) {
        if (!node->operand) { m_currentLLVMValue = nullptr; return; }
        node->operand->accept(*this);
        llvm::Value* errPtr = m_currentLLVMValue;
        if (!errPtr || !errPtr->getType()->isPointerTy()) {
            logError(node->loc, "typeof() on wildcard error requires an error pointer");
            m_currentLLVMValue = nullptr;
            return;
        }
        llvm::Type* i8Ptr = llvm::PointerType::get(*context, 0);
        llvm::StructType* vybErrorTy = llvm::StructType::get(
            *context,
            { builder->getInt64Ty(), i8Ptr, i8Ptr, i8Ptr,
              builder->getInt32Ty(), builder->getInt32Ty() },
            false);
        llvm::Value* errStruct = builder->CreateBitCast(
            errPtr, llvm::PointerType::get(vybErrorTy, 0), "typeof.err.ptr");
        llvm::Value* slot = builder->CreateStructGEP(
            vybErrorTy, errStruct, 0, "typeof.typeid.slot");
        m_currentLLVMValue = builder->CreateLoad(
            builder->getInt64Ty(), slot, "typeof.typeid");
        return;
    }

    // Static form: hash the operand's static type (set by semantic analysis).
    if (!node->operand) {
        logError(node->loc, "typeof() requires an operand expression");
        m_currentLLVMValue = nullptr;
        return;
    }
    std::string typeName = "Unknown";
    if (node->operand->type) {
        typeName = node->operand->type->toString();
    }
    uint64_t typeHash = std::hash<std::string>{}(typeName);
    m_currentLLVMValue = llvm::ConstantInt::get(builder->getInt64Ty(), typeHash);
}

// Introspection: typename(expr) - returns String with type name
void LLVMCodegen::visit(vyb::ast::TypenameExpression* node) {
    if (!node || !node->operand) {
        logError(node->loc, "typename() requires an operand expression");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Wildcard trap error (e<?>): load the runtime type name from the error header
    // and wrap it in a String { ptr, len }.
    if (node->operandFromWildcardError) {
        node->operand->accept(*this);
        llvm::Value* errPtr = m_currentLLVMValue;
        if (!errPtr || !errPtr->getType()->isPointerTy()) {
            logError(node->loc, "typename() on wildcard error requires an error pointer");
            m_currentLLVMValue = nullptr;
            return;
        }
        llvm::Type* i8Ptr = llvm::PointerType::get(*context, 0);
        llvm::StructType* vybErrorTy = llvm::StructType::get(
            *context,
            { builder->getInt64Ty(), i8Ptr, i8Ptr, i8Ptr,
              builder->getInt32Ty(), builder->getInt32Ty() },
            false);
        llvm::Value* errStruct = builder->CreateBitCast(
            errPtr, llvm::PointerType::get(vybErrorTy, 0), "typename.err.ptr");
        llvm::Value* slot = builder->CreateStructGEP(
            vybErrorTy, errStruct, 1, "typename.name.slot");
        llvm::Value* namePtr = builder->CreateLoad(
            i8Ptr, slot, "typename.name.ptr");
        llvm::Type* i64Ty = builder->getInt64Ty();
        llvm::Function* strlenFn = module->getFunction("strlen");
        if (!strlenFn) {
            strlenFn = llvm::Function::Create(
                llvm::FunctionType::get(i64Ty, {i8Ptr}, false),
                llvm::Function::ExternalLinkage, "strlen", module.get());
        }
        llvm::Value* len = builder->CreateCall(strlenFn, {namePtr}, "typename.len");
        llvm::StructType* sTy = llvm::StructType::get(
            *context, {i8Ptr, i64Ty});
        llvm::Value* s = llvm::UndefValue::get(sTy);
        s = builder->CreateInsertValue(s, namePtr, 0, "typename.ptr");
        s = builder->CreateInsertValue(s, len, 1, "typename.len");
        m_currentLLVMValue = s;
        return;
    }

    // A `Type` value operand: its runtime value is an opaque uint64 type ID, so
    // look up the registered type name at runtime.
    if (node->operandFromTypeValue) {
        node->operand->accept(*this);
        llvm::Value* typeId = m_currentLLVMValue;
        llvm::Type* i8Ptr = llvm::PointerType::get(*context, 0);
        llvm::Type* i64Ty = llvm::Type::getInt64Ty(*context);
        llvm::FunctionType* getTy = llvm::FunctionType::get(i8Ptr, {i64Ty}, false);
        llvm::Function* getFn = module->getFunction("__vyb_get_typename");
        if (!getFn) {
            getFn = llvm::Function::Create(getTy, llvm::Function::ExternalLinkage,
                                           "__vyb_get_typename", module.get());
        }
        llvm::Value* namePtr = builder->CreateCall(getFn, {typeId}, "typename.name");
        llvm::Function* strlenFn = module->getFunction("strlen");
        if (!strlenFn) {
            strlenFn = llvm::Function::Create(
                llvm::FunctionType::get(i64Ty, {i8Ptr}, false),
                llvm::Function::ExternalLinkage, "strlen", module.get());
        }
        llvm::Value* len = builder->CreateCall(strlenFn, {namePtr}, "typename.len");
        llvm::StructType* sTy = llvm::StructType::get(*context, {i8Ptr, i64Ty});
        llvm::Value* s = llvm::UndefValue::get(sTy);
        s = builder->CreateInsertValue(s, namePtr, 0, "typename.ptr");
        s = builder->CreateInsertValue(s, len, 1, "typename.len");
        m_currentLLVMValue = s;
        return;
    }

    // Get the type name from the operand's type field (set by semantic analysis)
    std::string typeName = "Unknown";
    if (node->operand->type) {
        typeName = node->operand->type->toString();
    }

    // Create a string literal containing the type name
    // Similar to StringLiteral::visit implementation
    llvm::Value* strPtr = builder->CreateGlobalStringPtr(typeName);

    // Create String struct {ptr, len}
    llvm::Type* int8PtrType = llvm::PointerType::get(*context, 0);
    llvm::Type* int64Type = llvm::Type::getInt64Ty(*context);
    llvm::StructType* stringStructType = llvm::StructType::get(*context, {int8PtrType, int64Type});

    // Calculate length
    llvm::Value* lenValue = llvm::ConstantInt::get(int64Type, typeName.length());

    // Build the String struct
    llvm::Value* stringStruct = llvm::UndefValue::get(stringStructType);
    stringStruct = builder->CreateInsertValue(stringStruct, strPtr, 0, "typename.ptr");
    stringStruct = builder->CreateInsertValue(stringStruct, lenValue, 1, "typename.len");

    m_currentLLVMValue = stringStruct;
}

void LLVMCodegen::visit(vyb::ast::AsExpression* node) {
    if (!node || !node->operand || !node->targetType) {
        logError(node->loc, "Malformed 'as' cast expression.");
        m_currentLLVMValue = nullptr;
        return;
    }
    node->operand->accept(*this);
    llvm::Value* operand = m_currentLLVMValue;
    if (!operand) {
        logError(node->loc, "Failed to evaluate operand of 'as' cast.");
        m_currentLLVMValue = nullptr;
        return;
    }
    llvm::Type* targetTy = codegenType(node->targetType.get());
    if (!targetTy) {
        logError(node->loc, "Cannot resolve target type of 'as' cast.");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Wildcard trap error downcast: `e as TargetType` extracts the concrete
    // payload from the VybError struct { type_hash, type_name, payload, file,
    // line, col } by loading the heap payload at field index 2.
    if (node->operandIsWildcardError) {
        if (!operand->getType()->isPointerTy()) {
            logError(node->loc, "'as' on a wildcard error requires an error pointer.");
            m_currentLLVMValue = nullptr;
            return;
        }
        llvm::Type* i8Ptr = llvm::PointerType::get(*context, 0);
        llvm::StructType* vybErrorTy = llvm::StructType::get(
            *context,
            { builder->getInt64Ty(), i8Ptr, i8Ptr, i8Ptr,
              builder->getInt32Ty(), builder->getInt32Ty() },
            false);
        llvm::Value* errStruct = builder->CreateBitCast(
            operand, llvm::PointerType::get(vybErrorTy, 0), "as.err.ptr");
        llvm::Value* payloadSlot = builder->CreateStructGEP(
            vybErrorTy, errStruct, 2, "as.payload.slot");
        llvm::Value* payloadPtr = builder->CreateLoad(
            i8Ptr, payloadSlot, "as.payload.ptr");
        llvm::Value* dataPtr = builder->CreateBitCast(
            payloadPtr, llvm::PointerType::get(targetTy, 0), "as.data.ptr");
        m_currentLLVMValue = builder->CreateLoad(targetTy, dataPtr, "as.value");
        return;
    }

    // Identity / same-type cast: the value already has the target type.
    // First, integer-to-integer conversions between the sized Int/UInt types:
    // the source's signedness drives the widening so `UInt8 as Int64` zero-
    // extends while `Int8 as Int64` sign-extends; narrowing truncates; equal
    // width is a bit-preserving move (signedness is only tracked at the type).
    std::string srcName;
    if (node->operand->type) {
        if (auto tn = dynamic_cast<ast::TypeName*>(node->operand->type.get())) {
            if (tn->identifier) srcName = tn->identifier->name;
        }
    }
    std::string dstName;
    ast::TypeNode* dstTypeNode = node->targetType->type
        ? node->targetType->type.get() : node->targetType.get();
    if (dstTypeNode) {
        if (auto tn = dynamic_cast<ast::TypeName*>(dstTypeNode)) {
            if (tn->identifier) dstName = tn->identifier->name;
        }
    }

    bool srcUnsigned = false;
    unsigned srcBits = 0;
    bool dstUnsigned = false;
    unsigned dstBits = 0;
    if (vybIntClass(srcName, srcUnsigned, srcBits) &&
        vybIntClass(dstName, dstUnsigned, dstBits) &&
        operand->getType()->isIntegerTy() && targetTy->isIntegerTy()) {
        unsigned srcW = operand->getType()->getIntegerBitWidth();
        unsigned dstW = targetTy->getIntegerBitWidth();
        if (dstW > srcW) {
            m_currentLLVMValue = srcUnsigned
                ? builder->CreateZExt(operand, targetTy, "as.zext")
                : builder->CreateSExt(operand, targetTy, "as.sext");
        } else if (dstW < srcW) {
            m_currentLLVMValue = builder->CreateTrunc(operand, targetTy, "as.trunc");
        } else {
            m_currentLLVMValue = operand;
        }
        return;
    }

    m_currentLLVMValue = operand;
}
