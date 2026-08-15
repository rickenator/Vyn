#include "vyb/vre/llvm/codegen.hpp"
#include "vyb/parser/ast.hpp"

#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Constants.h> // For ConstantPointerNull

#include <string>
#include <vector>
#include <map>

using namespace vyb;
// Using namespace llvm; // Uncomment if desired for brevity

void LLVMCodegen::visit(ast::PointerType* node) {
    // PointerType represents a pointer to another type
    // It generates an LLVM pointer type

    if (!node->pointeeType) {
        logError(node->loc, "Pointer type has no pointee type");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Process the pointee type
    node->pointeeType->accept(*this);

    // If the pointee type processing gave us a type directly in m_currentLLVMType,
    // use that, otherwise try to use the type of m_currentLLVMValue
    llvm::Type* pointeeType = nullptr;
    if (m_currentLLVMType) {
        pointeeType = m_currentLLVMType;
    } else if (m_currentLLVMValue && m_currentLLVMValue->getType()) {
        pointeeType = m_currentLLVMValue->getType();
    }

    if (!pointeeType) {
        logError(node->loc, "Failed to resolve pointee type");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Create a pointer type to the pointee type
    unsigned addressSpace = 0; // Use default address space
    llvm::Type* pointerType = llvm::PointerType::get(pointeeType, addressSpace);

    // Store the result type
    m_currentLLVMType = pointerType;
    m_currentLLVMValue = nullptr; // No value produced
}

void LLVMCodegen::visit(ast::ArrayType* node) {
    // ArrayType represents a fixed-size array of elements
    // It generates an LLVM array type

    if (!node->elementType) {
        logError(node->loc, "Array type has no element type");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Process the element type
    node->elementType->accept(*this);

    // If the element type processing gave us a type directly in m_currentLLVMType,
    // use that, otherwise try to use the type of m_currentLLVMValue
    llvm::Type* elementType = nullptr;
    if (m_currentLLVMType) {
        elementType = m_currentLLVMType;
    } else if (m_currentLLVMValue && m_currentLLVMValue->getType()) {
        elementType = m_currentLLVMValue->getType();
    }

    if (!elementType) {
        logError(node->loc, "Failed to resolve array element type");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Handle the array size expression if provided
    llvm::Value* sizeValue = nullptr;
    uint64_t arraySize = 0;

    if (node->sizeExpression) {
        // Evaluate the size expression
        node->sizeExpression->accept(*this);
        sizeValue = m_currentLLVMValue;

        if (!sizeValue) {
            logError(node->sizeExpression->loc, "Failed to evaluate array size expression");
            m_currentLLVMValue = nullptr;
            return;
        }

        // Try to get a constant size
        if (llvm::ConstantInt* constSize = llvm::dyn_cast<llvm::ConstantInt>(sizeValue)) {
            arraySize = constSize->getZExtValue();
        } else {
            // For runtime-sized arrays, we need to use a different approach
            // We'll represent it as a pointer to the element type for now
            logWarning(node->loc, "Runtime-sized arrays not fully implemented. Using pointer type instead.");
            m_currentLLVMType = llvm::PointerType::get(elementType, 0);
            m_currentLLVMValue = nullptr;
            return;
        }
    } else {
        // If no size specified, it's likely a slice or a dynamically sized array
        // For now, represent it as a pointer to the element type
        logWarning(node->loc, "Dynamically sized array type represented as a pointer.");
        m_currentLLVMType = llvm::PointerType::get(elementType, 0);
        m_currentLLVMValue = nullptr;
        return;
    }

    // Create an array type with the determined size
    llvm::Type* arrayType = llvm::ArrayType::get(elementType, arraySize);

    // Store the result type
    m_currentLLVMType = arrayType;
    m_currentLLVMValue = nullptr; // No value produced
}

void LLVMCodegen::visit(ast::VecType* node) {
    // VecType represents a dynamic vector Vec<T>
    // It generates an LLVM struct type with { ptr, size, capacity }

    if (!node->elementType) {
        logError(node->loc, "Vec type has no element type");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Get the LLVM type for the element type
    llvm::Type* elementLLVMType = codegenType(node->elementType.get());
    if (!elementLLVMType) {
        logError(node->loc, "Failed to resolve Vec element type");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Create Vec struct: { ptr T*, size i64, capacity i64 }
    std::vector<llvm::Type*> vecFields = {
        llvm::PointerType::get(*context, 0), // ptr to elements (opaque pointer)
        llvm::Type::getInt64Ty(*context),    // size
        llvm::Type::getInt64Ty(*context)     // capacity
    };

    llvm::StructType* vecLLVMType = llvm::StructType::get(*context, vecFields, false);
    m_currentLLVMType = vecLLVMType;
    m_currentLLVMValue = nullptr;
}

void LLVMCodegen::visit(ast::FutureType* node) {
    // FutureType represents an async Future<T>
    // Generate LLVM struct type: { T* result, i32 state, i8* runtime_data }

    if (!node->resultType) {
        logError(node->loc, "Future type has no result type");
        m_currentLLVMType = nullptr;
        return;
    }

    // Get the LLVM type for the result type
    llvm::Type* resultLLVMType = codegenType(node->resultType.get());
    if (!resultLLVMType) {
        logError(node->loc, "Failed to resolve Future result type");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Create Future struct: { result_ptr T*, state i32, completion_ptr void* }
    std::vector<llvm::Type*> futureFields = {
        llvm::PointerType::get(*context, 0), // ptr to result (opaque pointer)
        llvm::Type::getInt32Ty(*context),    // state (pending=0, completed=1, failed=2)
        llvm::PointerType::get(*context, 0)  // completion callback ptr (opaque pointer)
    };

    llvm::StructType* futureLLVMType = llvm::StructType::get(*context, futureFields, false);
    m_currentLLVMType = futureLLVMType;
    m_currentLLVMValue = nullptr;
}

void LLVMCodegen::visit(ast::FunctionType* node) {
    // FunctionType represents a function pointer type with parameter and return types
    // It generates an LLVM function type

    // Process return type
    llvm::Type* returnType = nullptr;
    if (node->returnType) {
        node->returnType->accept(*this);

        if (m_currentLLVMType) {
            returnType = m_currentLLVMType;
        } else if (m_currentLLVMValue && m_currentLLVMValue->getType()) {
            returnType = m_currentLLVMValue->getType();
        } else {
            logError(node->returnType->loc, "Failed to resolve function return type");
            m_currentLLVMValue = nullptr;
            return;
        }
    } else {
        // Default to void return type if not specified
        returnType = llvm::Type::getVoidTy(*context);
    }

    // Process parameter types
    std::vector<llvm::Type*> paramTypes;
    for (const auto& paramType : node->parameterTypes) {
        if (paramType) {
            paramType->accept(*this);

            llvm::Type* type = nullptr;
            if (m_currentLLVMType) {
                type = m_currentLLVMType;
            } else if (m_currentLLVMValue && m_currentLLVMValue->getType()) {
                type = m_currentLLVMValue->getType();
            }

            if (type) {
                paramTypes.push_back(type);
            } else {
                logError(paramType->loc, "Failed to resolve function parameter type");
                m_currentLLVMValue = nullptr;
                return;
            }
        }
    }

    // Create the function type
    llvm::FunctionType* funcType = llvm::FunctionType::get(
        returnType,
        paramTypes,
        false // VarArg support - using false as there's no isVarArg in the actual class
    );

    // Function types are represented as pointers to the function type
    // A Vyb `fn` value is a closure: `struct { ptr env, ptr fn }`.
    m_currentLLVMType = getClosureStructType();
    m_currentLLVMValue = nullptr; // No value produced
}

void LLVMCodegen::visit(ast::OptionalType* node) {
    // OptionalType represents a value that may be null/none
    // In LLVM IR, we represent this as a structure containing:
    // 1. A boolean flag indicating if the value is present
    // 2. The value itself (if the flag is true)

    if (!node->containedType) {
        logError(node->loc, "Optional type has no contained type");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Process the value type
    node->containedType->accept(*this);

    // If the value type processing gave us a type directly in m_currentLLVMType,
    // use that, otherwise try to use the type of m_currentLLVMValue
    llvm::Type* valueType = nullptr;
    if (m_currentLLVMType) {
        valueType = m_currentLLVMType;
    } else if (m_currentLLVMValue && m_currentLLVMValue->getType()) {
        valueType = m_currentLLVMValue->getType();
    }

    if (!valueType) {
        logError(node->loc, "Failed to resolve optional value type");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Create a structure type for the optional
    // [hasValue: bool, value: ValueType]
    std::vector<llvm::Type*> members = {
        llvm::Type::getInt1Ty(*context), // hasValue flag
        valueType                        // The actual value
    };

    // Generate a name for the optional type
    std::string optionalTypeName = "optional." + getTypeName(valueType);

    // Create the optional type structure
    // (note: getTypeByName is not available in LLVM 18.1, using create directly)
    llvm::StructType* optionalType = llvm::StructType::create(*context, members, optionalTypeName);

    // Store the result type
    m_currentLLVMType = optionalType;
    m_currentLLVMValue = nullptr; // No value produced
}

void LLVMCodegen::visit(ast::TupleTypeNode* node) {
    // TupleTypeNode represents a tuple of multiple types
    // In LLVM IR, we represent this as an anonymous structure

    // Process each member type
    std::vector<llvm::Type*> memberTypes;
    for (const auto& memberType : node->memberTypes) {
        if (memberType) {
            memberType->accept(*this);

            llvm::Type* type = nullptr;
            if (m_currentLLVMType) {
                type = m_currentLLVMType;
            } else if (m_currentLLVMValue && m_currentLLVMValue->getType()) {
                type = m_currentLLVMValue->getType();
            }

            if (type) {
                memberTypes.push_back(type);
            } else {
                logError(memberType->loc, "Failed to resolve tuple member type");
                m_currentLLVMValue = nullptr;
                return;
            }
        } else {
            logError(node->loc, "Tuple type contains null member type");
            m_currentLLVMValue = nullptr;
            return;
        }
    }

    if (memberTypes.empty()) {
        logError(node->loc, "Empty tuple type not supported");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Create a name for the tuple type based on its members
    std::string tupleTypeName = "tuple";
    for (const auto& type : memberTypes) {
        tupleTypeName += "." + getTypeName(type);
    }

    // Create the tuple type structure
    // (note: getTypeByName is not available in LLVM 18.1, using create directly)
    llvm::StructType* tupleType = llvm::StructType::create(*context, memberTypes, tupleTypeName);

    // Store the result type
    m_currentLLVMType = tupleType;
    m_currentLLVMValue = nullptr; // No value produced
}

// --- Type Mapping Helper ---
llvm::Type* LLVMCodegen::codegenType(vyb::ast::TypeNode* typeNode) {
    if (!typeNode) {
        logError(SourceLocation(), "Null type node in codegenType");
        return nullptr;
    }

    // Check cache first. Skip the cache entirely while generic-function type
    // substitutions are active: monomorphization reuses a single shared template
    // AST for every instantiation, so the same TypeNode (e.g. `Box<T>`) resolves
    // to *different* concrete types per instantiation and must not be cached
    // globally (e.g. Box_Int for one call and Box_String for another).
    const bool substitutionsActive = !currentTypeSubstitutions.empty();
    if (!substitutionsActive) {
        auto it = m_typeCache.find(typeNode);
        if (it != m_typeCache.end() && it->second.second == typeNode->toString()) {
            return it->second.first;
        }
    }

    llvm::Type* llvmType = nullptr;

    switch (typeNode->getCategory()) {
        case vyb::ast::TypeNode::Category::IDENTIFIER: {
            auto* typeNameNode = dynamic_cast<vyb::ast::TypeName*>(typeNode);
            if (!typeNameNode) {
                logError(typeNode->loc, "Type node is not a TypeName");
                return nullptr;
            }

            // Special handling for pointer-like ABI types.
            if (typeNameNode->identifier->name == "loc" || typeNameNode->identifier->name == "CPtr") {
                if (typeNameNode->genericArgs.empty()) {
                    logError(typeNode->loc, typeNameNode->identifier->name + " type requires a type parameter");
                    return nullptr;
                }
                llvm::Type* pointeeType = codegenType(typeNameNode->genericArgs[0].get());
                if (!pointeeType) {
                    logError(typeNode->loc, "Could not determine LLVM type for loc<T> pointee type");
                    return nullptr;
                }
                llvmType = llvm::PointerType::getUnqual(pointeeType);
                break;
            }

            if (!typeNameNode || !typeNameNode->identifier) { // Check typeNameNode and its identifier
                logError(typeNode->loc, "Type identifier node has no name or is not a TypeName.");
                return nullptr;
            }
            std::string typeNameStr = typeNameNode->identifier->name; // Access name via identifier

            // C-like enums are scalar i64 tags (distinct nominal types): a variable
            // or extern parameter typed with the enum name lowers to a plain i64 so
            // it is ABI-compatible with a C integer-backed enum at the FFI boundary.
            auto scalarEnumIt = taggedEnumInfo.find(typeNameStr);
            if (scalarEnumIt != taggedEnumInfo.end() && scalarEnumIt->second.isScalar) {
                llvmType = int64Type;
                break;
            }

            size_t assocSep = typeNameStr.find("::");
            if (assocSep != std::string::npos && m_currentImplTypeNode && driver_.hasSemanticAnalyzer()) {
                std::string traitName = typeNameStr.substr(0, assocSep);
                if (traitName == "Self") {
                    traitName = m_currentImplTraitName;
                }

                if (!traitName.empty()) {
                    SemanticAnalyzer* semantic = driver_.getSemanticAnalyzer();
                    std::string resolvedType = semantic->resolveAssociatedTypeForType(
                        m_currentImplTypeNode->toString(), traitName, typeNameStr);
                    if (!resolvedType.empty()) {
                        auto resolvedNode = std::make_unique<ast::TypeName>(
                            typeNode->loc,
                            std::make_unique<ast::Identifier>(typeNode->loc, resolvedType)
                        );
                        llvmType = codegenType(resolvedNode.get());
                        break;
                    }
                }
            }

            // Handle Self type in bind/impl methods
            if (typeNameStr == "Self") {
                if (m_currentImplTypeNode) {
                    VYB_CDBG << "DEBUG: Resolving Self to " << m_currentImplTypeNode->toString() << " in bind method" << std::endl;
                    llvmType = codegenType(m_currentImplTypeNode);
                    break;
                } else {
                    logError(typeNode->loc, "Self type used outside of bind/impl context");
                    return nullptr;
                }
            }

            // Handle Tuple<T, U, ...> types
            if (typeNameStr == "Tuple") {
                if (typeNameNode->genericArgs.empty()) {
                    logError(typeNode->loc, "Tuple type requires type parameters (e.g., Tuple<Int, String>)");
                    return nullptr;
                }

                // Process each tuple element type
                std::vector<llvm::Type*> tupleFields;
                for (const auto& elemTypeNode : typeNameNode->genericArgs) {
                    if (!elemTypeNode) {
                        logError(typeNode->loc, "Tuple contains null type parameter");
                        return nullptr;
                    }
                    llvm::Type* elemTy = codegenType(elemTypeNode.get());
                    if (!elemTy) {
                        logError(typeNode->loc, "Could not determine LLVM type for Tuple element: " + elemTypeNode->toString());
                        return nullptr;
                    }
                    tupleFields.push_back(elemTy);
                }

                // Create anonymous struct for the tuple
                llvmType = llvm::StructType::get(*context, tupleFields, false);
                break;
            }

            // Handle Vec<T> types
            if (typeNameStr == "CString") {
                llvmType = llvm::PointerType::getUnqual(int8Type);
                break;
            }

            if (typeNameStr == "Vec") {
                if (typeNameNode->genericArgs.empty() || !typeNameNode->genericArgs[0]) {
                    logError(typeNode->loc, "Vec type requires a type parameter (e.g., Vec<Int>)");
                    return nullptr;
                }
                llvm::Type* elemTy = codegenType(typeNameNode->genericArgs[0].get());
                if (!elemTy) {
                    logError(typeNode->loc, "Could not determine LLVM type for Vec element.");
                    return nullptr;
                }

                // Create Vec struct: { ptr T*, size i64, capacity i64 }
                std::vector<llvm::Type*> vecFields = {
                    llvm::PointerType::get(*context, 0), // ptr to elements (opaque pointer)
                    llvm::Type::getInt64Ty(*context),    // size
                    llvm::Type::getInt64Ty(*context)     // capacity
                };

                llvmType = llvm::StructType::get(*context, vecFields, false);
                break;
            }

            // Handle ownership types: my<T>, our<T>, their<T>, mild<T>, view<T>, borrow<T>
            if (typeNameStr == "my" || typeNameStr == "our" || typeNameStr == "their" ||
                typeNameStr == "mild" || typeNameStr == "view" || typeNameStr == "borrow") {
                if (typeNameNode->genericArgs.empty() || !typeNameNode->genericArgs[0]) {
                    logError(typeNode->loc, typeNameStr + " type requires a type parameter (e.g., " + typeNameStr + "<TreeNode>)");
                    return nullptr;
                }
                VYB_CDBG << "DEBUG: Processing ownership type " << typeNameStr << " with underlying type: "
                          << typeNameNode->genericArgs[0]->toString() << std::endl;
                // For LLVM code generation, ownership types become pointers to the underlying type.
                // However, for primitive types (Int, Bool, Float), keep the value directly
                // since there is no heap allocation to manage.
                llvm::Type* underlyingType = codegenType(typeNameNode->genericArgs[0].get());
                if (!underlyingType) {
                    logError(typeNode->loc, "Could not determine LLVM type for " + typeNameStr + " underlying type.");
                    return nullptr;
                }
                // Only create pointer for non-primitive types (structs, Vec, etc.)
                if (underlyingType->isIntegerTy() || underlyingType->isFloatTy() || underlyingType->isDoubleTy()) {
                    // Primitive: store value directly, no pointer needed
                    llvmType = underlyingType;
                    VYB_CDBG << "DEBUG: Ownership type " << typeNameStr << " of primitive keeps underlying type" << std::endl;
                } else {
                    // Heap type: create pointer to the underlying type
                    llvmType = llvm::PointerType::getUnqual(underlyingType);
                    VYB_CDBG << "DEBUG: Successfully resolved ownership type " << typeNameStr << " to pointer type" << std::endl;
                }
                break;
            }

            // MONOMORPHIZATION: Check if this is a generic struct instantiation (e.g., Box<Int>)
            // Before checking for primitive types, see if this matches a generic template
            if (!typeNameNode->genericArgs.empty()) {
                // Generic data enum (e.g. Box<Int>): monomorphize the tagged union.
                if (genericEnumTemplates.count(typeNameStr) || typeNameStr == "Option" ||
                    typeNameStr == "Result") {
                    llvm::StructType* enumTy = monomorphizeEnum(typeNameStr, typeNameNode->genericArgs);
                    if (!enumTy) {
                        logError(typeNode->loc, "Failed to monomorphize generic enum " + typeNameStr);
                        return nullptr;
                    }
                    llvmType = enumTy;
                    break;
                }
                auto templateIt = genericStructTemplates.find(typeNameStr);
                if (templateIt != genericStructTemplates.end()) {
                    VYB_CDBG << "DEBUG: Detected generic struct instantiation: " << typeNameStr
                              << " with " << typeNameNode->genericArgs.size() << " type arguments" << std::endl;

                    // Trigger monomorphization
                    llvm::StructType* specializedType = monomorphizeStruct(typeNameStr, typeNameNode->genericArgs);
                    if (!specializedType) {
                        logError(typeNode->loc, "Failed to monomorphize " + typeNameStr);
                        return nullptr;
                    }

                    llvmType = specializedType;
                    break;
                }
            }

            // Signed integer types (64-bit default)
            if (typeNameStr == "Int" || typeNameStr == "i64" ||
                typeNameStr == "CLong" || typeNameStr == "CSSize") {
                llvmType = int64Type;
            } else if (typeNameStr == "Int32" || typeNameStr == "i32" || typeNameStr == "CInt") {
                llvmType = int32Type;
            } else if (typeNameStr == "Int16" || typeNameStr == "i16" || typeNameStr == "CShort") {
                llvmType = llvm::Type::getInt16Ty(*context);
            } else if (typeNameStr == "Int8" || typeNameStr == "i8" || typeNameStr == "CChar") {
                llvmType = int8Type;

            // Unsigned integer types
            } else if (typeNameStr == "UInt64" || typeNameStr == "u64" ||
                       typeNameStr == "CULong" || typeNameStr == "CSize") {
                llvmType = llvm::Type::getInt64Ty(*context);  // Same as i64 at LLVM level
            } else if (typeNameStr == "UInt32" || typeNameStr == "u32" || typeNameStr == "CUInt") {
                llvmType = llvm::Type::getInt32Ty(*context);  // Same as i32 at LLVM level
            } else if (typeNameStr == "UInt16" || typeNameStr == "u16" || typeNameStr == "CUShort") {
                llvmType = llvm::Type::getInt16Ty(*context);  // Same as i16 at LLVM level
            } else if (typeNameStr == "UInt8" || typeNameStr == "u8" || typeNameStr == "CUChar") {
                llvmType = llvm::Type::getInt8Ty(*context);   // Same as i8 at LLVM level

            // Floating point types
            } else if (typeNameStr == "Float" || typeNameStr == "f64" || typeNameStr == "CDouble") {
                llvmType = doubleType;
            } else if (typeNameStr == "Float32" || typeNameStr == "f32" || typeNameStr == "CFloat") {
                llvmType = floatType;

            // Boolean type
            } else if (typeNameStr == "Bool") {
                llvmType = int1Type;

            // Type identity (opaque 8-byte type hash)
            } else if (typeNameStr == "Type") {
                llvmType = int64Type;

            // Void type
            } else if (typeNameStr == "Void" || typeNameStr == "CVoid") {
                llvmType = voidType;

            // String type (fat pointer: { ptr, len })
            } else if (typeNameStr == "String") {
                // String is a struct: { ptr: *i8, len: i64 }
                std::vector<llvm::Type*> strFields = {
                    llvm::PointerType::get(*context, 0), // ptr to bytes
                    llvm::Type::getInt64Ty(*context)     // length
                };
                llvmType = llvm::StructType::get(*context, strFields, false);

            // Character types
            } else if (typeNameStr == "Char") {
                // Char represents a single UTF-8 code unit (1 byte)
                llvmType = int8Type;
            } else if (typeNameStr == "Rune") {
                // Rune represents a full Unicode code point (up to 32 bits)
                llvmType = int32Type;

            // Byte sequence type
            } else if (typeNameStr == "Bytes" || typeNameStr == "bytes") {
                // Bytes is similar to String but for raw binary data: { ptr: *u8, len: i64 }
                std::vector<llvm::Type*> bytesFields = {
                    llvm::PointerType::get(*context, 0), // ptr to bytes
                    llvm::Type::getInt64Ty(*context)     // length
                };
                llvmType = llvm::StructType::get(*context, bytesFields, false);

            // Future type (async)
            } else if (typeNameStr == "Future") {
                // For now, treat generic Future type as opaque pointer
                // The real Future<T> types will be handled by the FutureType visitor
                llvmType = llvm::PointerType::getUnqual(int8Type);
            } else {
                // Check type alias map first
                auto typeAliasIt = typeAliasMap.find(typeNameStr);
                if (typeAliasIt != typeAliasMap.end()) {
                    VYB_CDBG << "DEBUG: Found type alias for " << typeNameStr << std::endl;
                    llvmType = typeAliasIt->second;
                } else {
                    // Check if this is a type parameter being substituted during monomorphization
                    auto substitutionIt = currentTypeSubstitutions.find(typeNameStr);
                    if (substitutionIt != currentTypeSubstitutions.end()) {
                        VYB_CDBG << "DEBUG: Substituting type parameter " << typeNameStr
                                  << " -> " << substitutionIt->second << " during monomorphization" << std::endl;

                        // Recursively resolve the substituted type
                        // Create a TypeName node for the concrete type
                        auto concreteTypeName = std::make_unique<ast::TypeName>(
                            typeNode->loc,
                            std::make_unique<ast::Identifier>(typeNode->loc, substitutionIt->second),
                            std::vector<ast::TypeNodePtr>()
                        );
                        llvmType = codegenType(concreteTypeName.get());
                    } else {
                        auto userTypeIt = userTypeMap.find(typeNameStr);
                        if (userTypeIt != userTypeMap.end()) {
                            VYB_CDBG << "DEBUG: Found user type " << typeNameStr << " in userTypeMap, isOpaque: "
                                      << userTypeIt->second.llvmType->isOpaque() << std::endl;
                            llvmType = userTypeIt->second.llvmType;
                        } else {
                            VYB_CDBG << "DEBUG: User type " << typeNameStr << " not found in userTypeMap, checking LLVM context" << std::endl;
                            llvm::StructType* existingType = llvm::StructType::getTypeByName(*context, typeNameStr);
                            if (existingType) {
                                VYB_CDBG << "DEBUG: Found existing type " << typeNameStr << " in LLVM context, isOpaque: "
                                          << existingType->isOpaque() << std::endl;
                                llvmType = existingType;
                            } else {
                                // This case should ideally be caught by semantic analysis if it\'s an undefined type.
                                // If it\'s a type that will be defined later (e.g. in a different module or due to ordering),
                                // creating an opaque struct might be an option, but can be risky.
                                // llvmType = llvm::StructType::create(*context, typeNameStr);
                                logError(typeNode->loc, "Unknown type identifier: " + typeNameStr + ". It might be a forward-declared type not yet fully defined or an undeclared type.");
                                return nullptr;
                            }
                        }
                    }
                }
            }
            break;
        }
        case vyb::ast::TypeNode::Category::ARRAY: {
            auto* arrayTypeNode = dynamic_cast<vyb::ast::ArrayType*>(typeNode);
            if (!arrayTypeNode || !arrayTypeNode->elementType) { // Check arrayTypeNode and its elementType
                logError(typeNode->loc, "Array type node has no element type or is not an ArrayType.");
                return nullptr;
            }
            llvm::Type* elemTy = codegenType(arrayTypeNode->elementType.get()); // Access elementType
            if (!elemTy) {
                logError(typeNode->loc, "Could not determine LLVM type for array element.");
                return nullptr;
            }

            if (arrayTypeNode->sizeExpression) { // Access sizeExpression
                // For fixed-size arrays. This requires constant evaluation.
                // Simplified: assumes IntegerLiteral for size.
                if (auto* intLit = dynamic_cast<vyb::ast::IntegerLiteral*>(arrayTypeNode->sizeExpression.get())) { // Access sizeExpression
                    uint64_t arraySize = intLit->value;
                    if (arraySize == 0) {
                        logError(typeNode->loc, "Array size cannot be zero.");
                        return nullptr;
                    }
                    llvmType = llvm::ArrayType::get(elemTy, arraySize);
                } else {
                    logError(typeNode->loc, "Array size is not a constant integer literal. Dynamic/complex-sized arrays need specific handling (e.g., as slices/structs or require constant folding). Treating as pointer for now.");
                    llvmType = llvm::PointerType::getUnqual(elemTy); // Fallback, might not be correct for all Vyb semantics
                }
            } else {
                // Unsized array (e.g., `arr: []Int`) - typically a pointer or a slice struct.
                llvmType = llvm::PointerType::getUnqual(elemTy); // Fallback, might not be correct for all Vyb semantics
            }
            break;
        }
        case vyb::ast::TypeNode::Category::TUPLE: {
            auto* tupleTypeNode = dynamic_cast<vyb::ast::TupleTypeNode*>(typeNode);
            if (!tupleTypeNode) {
                logError(typeNode->loc, "Type node is not a TupleTypeNode.");
                return nullptr;
            }
            std::vector<llvm::Type*> memberLlvmTypes;
            for (const auto& memberTypeNode : tupleTypeNode->memberTypes) { // Access memberTypes
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
        case vyb::ast::TypeNode::Category::VEC: {
            auto* vecTypeNode = dynamic_cast<vyb::ast::VecType*>(typeNode);
            if (!vecTypeNode || !vecTypeNode->elementType) {
                logError(typeNode->loc, "Vec type node has no element type or is not a VecType.");
                return nullptr;
            }
            llvm::Type* elemTy = codegenType(vecTypeNode->elementType.get());
            if (!elemTy) {
                logError(typeNode->loc, "Could not determine LLVM type for Vec element.");
                return nullptr;
            }

            // Create Vec struct: { ptr T*, size i64, capacity i64 }
            std::vector<llvm::Type*> vecFields = {
                llvm::PointerType::get(*context, 0), // ptr to elements (opaque pointer)
                llvm::Type::getInt64Ty(*context),    // size
                llvm::Type::getInt64Ty(*context)     // capacity
            };

            llvmType = llvm::StructType::get(*context, vecFields, false);
            break;
        }
        case vyb::ast::TypeNode::Category::FUTURE: {
            auto* futureTypeNode = dynamic_cast<vyb::ast::FutureType*>(typeNode);
            if (!futureTypeNode || !futureTypeNode->resultType) {
                logError(typeNode->loc, "Future type node has no result type or is not a FutureType.");
                return nullptr;
            }
            llvm::Type* resultTy = codegenType(futureTypeNode->resultType.get());
            if (!resultTy) {
                logError(typeNode->loc, "Could not determine LLVM type for Future result.");
                return nullptr;
            }

            // Create Future struct: { result_ptr T*, state i32, completion_ptr void* }
            std::vector<llvm::Type*> futureFields = {
                llvm::PointerType::get(*context, 0), // ptr to result (opaque pointer)
                llvm::Type::getInt32Ty(*context),    // state (pending=0, completed=1, failed=2)
                llvm::PointerType::get(*context, 0)  // completion callback ptr (opaque pointer)
            };

            llvmType = llvm::StructType::get(*context, futureFields, false);
            break;
        }
        case vyb::ast::TypeNode::Category::FUNCTION: { // Changed from FUNCTION_SIGNATURE
            auto* funcTypeNode = dynamic_cast<vyb::ast::FunctionType*>(typeNode);
            if (!funcTypeNode) {
                logError(typeNode->loc, "Type node is not a FunctionType.");
                return nullptr;
            }
            std::vector<llvm::Type*> paramLlvmTypes;
            for (const auto& paramTypeNode : funcTypeNode->parameterTypes) { // Access parameterTypes
                llvm::Type* paramLlvmType = codegenType(paramTypeNode.get());
                if (!paramLlvmType) {
                    logError(typeNode->loc, "Could not determine LLVM type for a function parameter in signature.");
                    return nullptr;
                }
                paramLlvmTypes.push_back(paramLlvmType);
            }
            llvm::Type* returnLlvmType = funcTypeNode->returnType ? codegenType(funcTypeNode->returnType.get()) : voidType; // Access returnType
            if (!returnLlvmType) {
                 logError(typeNode->loc, "Could not determine LLVM return type for function signature.");
                return nullptr;
            }
            // A `fn` type is a closure value: `struct { ptr env, ptr fn }`.
            // The parameter/return types above are validated for signature
            // consistency but the runtime value is the uniform closure struct.
            llvmType = getClosureStructType();
            break;
        }
        case vyb::ast::TypeNode::Category::POINTER: {
            auto* pointerTypeNode = dynamic_cast<vyb::ast::PointerType*>(typeNode);
            if (!pointerTypeNode || !pointerTypeNode->pointeeType) {
                logError(typeNode->loc, "Pointer type has no pointee type or is not a PointerType.");
                return nullptr;
            }
            llvm::Type* pointeeLlvmType = codegenType(pointerTypeNode->pointeeType.get());
            if (!pointeeLlvmType) {
                logError(typeNode->loc, "Could not determine LLVM type for pointee type in pointer.");
                return nullptr;
            }
            llvmType = llvm::PointerType::getUnqual(pointeeLlvmType);
            break;
        }
        case vyb::ast::TypeNode::Category::OPTIONAL: {
            auto* optionalTypeNode = dynamic_cast<vyb::ast::OptionalType*>(typeNode);
            if (!optionalTypeNode || !optionalTypeNode->containedType) {
                logError(typeNode->loc, "Optional type has no contained type or is not an OptionalType.");
                return nullptr;
            }
            llvm::Type* containedLlvmType = codegenType(optionalTypeNode->containedType.get());
            if (!containedLlvmType) {
                logError(typeNode->loc, "Could not determine LLVM type for contained type in optional.");
                return nullptr;
            }
            // Represent optional<T> as a struct { T value; bool has_value; }
            // Or, if T is a pointer, optional<T*> can be T* (where nullptr means no value).
            // For simplicity here, let's assume T is not a pointer and use a struct.
            // A more complex handling might be needed based on Vyb's specific semantics for optionals.
            if (containedLlvmType->isPointerTy()) {
                 // If T is already a pointer, optional<T*> can be represented by T* (nullptr for none)
                llvmType = containedLlvmType;
            } else {
                // For non-pointer types, use a struct { value, i1 has_value }
                // To avoid issues with recursive types if T is this optional type itself (though less common for optionals),
                // we should ideally name this struct if it's not anonymous.
                // For now, creating an anonymous struct.
                llvm::StructType* optionalStructType = llvm::StructType::get(*context, {containedLlvmType, int1Type});
                llvmType = optionalStructType;
            }
            break;
        }
        // case vyb::ast::TypeNode::Category::REFERENCE: // TODO: Add handling for REFERENCE if distinct from POINTER
        // case vyb::ast::TypeNode::Category::SLICE: // TODO: Add handling for SLICE
        default:
            logError(typeNode->loc, "Unknown or unsupported TypeNode category: " + typeNode->toString());
            return nullptr;
    }

    if (llvmType && !substitutionsActive) {
        m_typeCache[typeNode] = { llvmType, typeNode->toString() };
    }
    return llvmType;
}

void LLVMCodegen::visit(vyb::ast::TypeNode* node) {
    if (node) {
        m_currentLLVMType = codegenType(node);
    } else {
        m_currentLLVMType = nullptr;
        // logError(SourceLocation(), "visit(TypeNode*) called with null node."); // Optional: log if a location is available
    }
    // This visitor primarily populates m_currentLLVMType.
    // It does not produce a Value for m_currentLLVMValue.
}

void LLVMCodegen::visit(ast::TypeName* node) {
    // This visitor is used when a type name appears as an expression (e.g., in a type check)
    // In most cases, we don't generate any runtime code for type names

    logWarning(node->loc, "TypeName used as an expression; this might not behave as expected");

    // For now, we'll just return a null value
    m_currentLLVMValue = llvm::ConstantPointerNull::get(
        llvm::PointerType::get(*context, 0));
}

llvm::StructType* LLVMCodegen::getClosureStructType() {
    // Uniform closure representation: `struct { ptr env, ptr fn }`.
    // With opaque pointers both fields are `ptr`. The fn field holds the
    // lambda's function pointer; the env field is the capture environment
    // (null for non-capturing lambdas). Structurally-identical instances are
    // unified by LLVM, so every `fn` value shares this layout.
    std::vector<llvm::Type*> fields = {
        llvm::PointerType::get(*context, 0),
        llvm::PointerType::get(*context, 0)
    };
    return llvm::StructType::get(*context, fields, false);
}
