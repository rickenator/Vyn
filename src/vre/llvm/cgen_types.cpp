\
#include "vyn/vre/llvm/codegen.hpp"
#include "vyn/parser/ast.hpp"

#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Constants.h> // For ConstantPointerNull

#include <string>
#include <vector>
#include <map>

using namespace vyn;
// Using namespace llvm; // Uncomment if desired for brevity

// --- Type Mapping Helper ---
llvm::Type* LLVMCodegen::codegenType(vyn::ast::TypeNode* typeNode) {
    if (!typeNode) {
        // Consider adding a logError call here if a SourceLocation is available or a default one can be used.
        // logError(SourceLocation(), "codegenType called with null typeNode.");
        return nullptr;
    }

    // Check cache first
    auto cacheIt = m_typeCache.find(typeNode);
    if (cacheIt != m_typeCache.end()) {
        return cacheIt->second;
    }

    llvm::Type* llvmType = nullptr;

    // Handle different type categories
    switch (typeNode->category) {
        case vyn::ast::TypeNode::Category::IDENTIFIER: {
            if (!typeNode->name) {
                logError(typeNode->loc, "Type identifier node has no name.");
                return nullptr;
            }
            std::string typeNameStr = typeNode->name->name;

            if (typeNameStr == "Int" || typeNameStr == "int" || typeNameStr == "i64") {
                llvmType = int64Type;
            } else if (typeNameStr == "Float" || typeNameStr == "float" || typeNameStr == "f64") {
                llvmType = doubleType;
            } else if (typeNameStr == "Bool" || typeNameStr == "bool") {
                llvmType = int1Type;
            } else if (typeNameStr == "Void" || typeNameStr == "void") {
                llvmType = voidType;
            } else if (typeNameStr == "String" || typeNameStr == "string") {
                llvmType = int8PtrType; 
            } else if (typeNameStr == "char" || typeNameStr == "i8") {
                llvmType = int8Type;
            } else if (typeNameStr == "i32") {
                llvmType = int32Type;
            } else {
                auto userTypeIt = userTypeMap.find(typeNameStr);
                if (userTypeIt != userTypeMap.end()) {
                    llvmType = userTypeIt->second.llvmType;
                } else {
                    llvm::StructType* existingType = llvm::StructType::getTypeByName(*context, typeNameStr);
                    if (existingType) {
                        llvmType = existingType;
                    } else {
                        // This case should ideally be caught by semantic analysis if it's an undefined type.
                        // If it's a type that will be defined later (e.g. in a different module or due to ordering),
                        // creating an opaque struct might be an option, but can be risky.
                        // llvmType = llvm::StructType::create(*context, typeNameStr);
                        logError(typeNode->loc, "Unknown type identifier: " + typeNameStr + ". It might be a forward-declared type not yet fully defined or an undeclared type.");
                        return nullptr;
                    }
                }
            }
            break;
        }
        case vyn::ast::TypeNode::Category::ARRAY: {
            if (!typeNode->arrayElementType) {
                logError(typeNode->loc, "Array type node has no element type.");
                return nullptr;
            }
            llvm::Type* elemTy = codegenType(typeNode->arrayElementType.get());
            if (!elemTy) {
                logError(typeNode->loc, "Could not determine LLVM type for array element.");
                return nullptr;
            }

            if (typeNode->arraySizeExpression) {
                // For fixed-size arrays. This requires constant evaluation.
                // Simplified: assumes IntegerLiteral for size.
                if (auto* intLit = dynamic_cast<vyn::ast::IntegerLiteral*>(typeNode->arraySizeExpression.get())) {
                    uint64_t arraySize = intLit->value;
                    if (arraySize == 0) { 
                        logError(typeNode->loc, "Array size cannot be zero.");
                        return nullptr;
                    }
                    llvmType = llvm::ArrayType::get(elemTy, arraySize);
                } else {
                    logError(typeNode->loc, "Array size is not a constant integer literal. Dynamic/complex-sized arrays need specific handling (e.g., as slices/structs or require constant folding). Treating as pointer for now.");
                    llvmType = llvm::PointerType::getUnqual(elemTy); // Fallback, might not be correct for all Vyn semantics
                }
            } else {
                // Unsized array (e.g., `arr: []Int`) - typically a pointer or a slice struct.
                llvmType = llvm::PointerType::getUnqual(elemTy); // Fallback, might not be correct for all Vyn semantics
            }
            break;
        }
        case vyn::ast::TypeNode::Category::TUPLE: {
            std::vector<llvm::Type*> memberLlvmTypes;
            for (const auto& memberTypeNode : typeNode->tupleElementTypes) {
                llvm::Type* memberLlvmType = codegenType(memberTypeNode.get());
                if (!memberLlvmType) {
                    logError(typeNode->loc, "Could not determine LLVM type for a tuple member.");
                    return nullptr;
                }
                memberLlvmTypes.push_back(memberLlvmType);
            }
            llvmType = llvm::StructType::get(*context, memberLlvmTypes);
            break;
        }
        case vyn::ast::TypeNode::Category::FUNCTION_SIGNATURE: {
            std::vector<llvm::Type*> paramLlvmTypes;
            for (const auto& paramTypeNode : typeNode->functionParameters) {
                llvm::Type* paramLlvmType = codegenType(paramTypeNode.get());
                if (!paramLlvmType) {
                    logError(typeNode->loc, "Could not determine LLVM type for a function parameter in signature.");
                    return nullptr;
                }
                paramLlvmTypes.push_back(paramLlvmType);
            }
            llvm::Type* returnLlvmType = typeNode->functionReturnType ? codegenType(typeNode->functionReturnType.get()) : voidType;
            if (!returnLlvmType) {
                 logError(typeNode->loc, "Could not determine LLVM return type for function signature.");
                return nullptr;
            }
            llvmType = llvm::FunctionType::get(returnLlvmType, paramLlvmTypes, false)->getPointerTo();
            break;
        }
        case vyn::ast::TypeNode::Category::OWNERSHIP_WRAPPED: {
            if (typeNode->wrappedType) {
                llvm::Type* wrappedLlvmType = codegenType(typeNode->wrappedType.get());
                if (!wrappedLlvmType) {
                    logError(typeNode->loc, "Could not determine LLVM type for wrapped type in ownership wrapper.");
                    return nullptr;
                }
                // Assuming all ownership-wrapped types, including 'ptr<T>', become T* in LLVM.
                // If 'my', 'our', 'their' have different LLVM representations (e.g. a struct),
                // this logic would need to be more specific based on typeNode->ownership.
                llvmType = llvm::PointerType::getUnqual(wrappedLlvmType);
            } else {
                logError(typeNode->loc, "Ownership-wrapped type has no wrapped type.");
                return nullptr;
            }
            break;
        }
        default:
            logError(typeNode->loc, "Unknown or unsupported TypeNode category: " + typeNode->toString());
            return nullptr;
    }

    if (llvmType) {
        m_typeCache[typeNode] = llvmType;
    }
    return llvmType;
}

void LLVMCodegen::visit(vyn::ast::TypeNode* node) {
    if (node) {
        m_currentLLVMType = codegenType(node);
    } else {
        m_currentLLVMType = nullptr;
        // logError(SourceLocation(), "visit(TypeNode*) called with null node."); // Optional: log if a location is available
    }
    // This visitor primarily populates m_currentLLVMType.
    // It does not produce a Value for m_currentLLVMValue.
}
