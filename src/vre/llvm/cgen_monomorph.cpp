// SPDX-License-Identifier: Apache-2.0

// Monomorphization: Generate specialized versions of generic types
// This file implements the monomorphization system for generic structs, traits, and functions.
// When generic types like Box<T> are instantiated with concrete types like Box<Int>,
// this system generates specialized LLVM struct types with type parameters substituted.

#include "vyb/vre/llvm/codegen.hpp"
#include "vyb/parser/ast.hpp"
#include <sstream>

namespace vyb {

// Generate a mangled name for a generic type instantiation
// Example: Box with [Int] -> "Box_Int"
//          Vec with [Box<Int>] -> "Vec_Box_Int"
std::string LLVMCodegen::mangleGenericTypeName(const std::string& baseName, const std::vector<ast::TypeNodePtr>& typeArgs) {
    std::stringstream ss;
    ss << baseName;

    for (const auto& typeArg : typeArgs) {
        ss << "_";
        if (!typeArg) {
            ss << "Unknown";
            continue;
        }

        // Handle different type node categories
        if (auto* typeName = dynamic_cast<ast::TypeName*>(typeArg.get())) {
            if (typeName->identifier) {
                ss << typeName->identifier->name;

                // Handle nested generics like Box<Int>
                if (!typeName->genericArgs.empty()) {
                    ss << "_";
                    for (size_t i = 0; i < typeName->genericArgs.size(); ++i) {
                        if (i > 0) ss << "_";
                        if (typeName->genericArgs[i]) {
                            auto* innerType = dynamic_cast<ast::TypeName*>(typeName->genericArgs[i].get());
                            if (innerType && innerType->identifier) {
                                ss << innerType->identifier->name;
                            } else {
                                ss << "Unknown";
                            }
                        }
                    }
                }
            } else {
                ss << "Unknown";
            }
        } else {
            // For other type categories, use toString() and sanitize
            std::string typeStr = typeArg->toString();
            // Replace invalid characters for LLVM struct names
            for (char& c : typeStr) {
                if (c == '<' || c == '>' || c == ',' || c == ' ') {
                    c = '_';
                }
            }
            ss << typeStr;
        }
    }

    return ss.str();
}

// Substitute type parameters in a type node
// Example: T -> Int when instantiating Box<T> with Box<Int>
ast::TypeNodePtr substituteTypeParameter(ast::TypeNode* typeNode,
                                         const std::map<std::string, ast::TypeNode*>& typeParamMap) {
    if (!typeNode) return nullptr;

    // If it's a TypeName, check if it's a type parameter
    if (auto* typeName = dynamic_cast<ast::TypeName*>(typeNode)) {
        if (typeName->identifier) {
            const std::string& name = typeName->identifier->name;

            // Check if this is a type parameter that needs substitution
            auto it = typeParamMap.find(name);
            if (it != typeParamMap.end()) {
                // Substitute with concrete type
                return it->second->clone();
            }

            // Not a type parameter, but might have generic args that need substitution
            if (!typeName->genericArgs.empty()) {
                std::vector<ast::TypeNodePtr> substitutedArgs;
                for (const auto& arg : typeName->genericArgs) {
                    substitutedArgs.push_back(substituteTypeParameter(arg.get(), typeParamMap));
                }
                return std::make_unique<ast::TypeName>(
                    typeName->loc,
                    std::make_unique<ast::Identifier>(typeName->identifier->loc, typeName->identifier->name),
                    std::move(substitutedArgs)
                );
            }
        }
    }

    // Native optional `T?`: substitute into the contained payload type so
    // `V?()` / `V?(v)` inside a monomorphized generic body land on the concrete
    // payload instead of an unresolved `V`.
    if (auto* optTy = dynamic_cast<ast::OptionalType*>(typeNode)) {
        ast::TypeNodePtr contained = optTy->containedType
            ? substituteTypeParameter(optTy->containedType.get(), typeParamMap)
            : nullptr;
        return std::make_unique<ast::OptionalType>(
            optTy->loc, contained ? std::move(contained) : optTy->containedType->clone());
    }

    // Recurse into the other wrapper types so generic args inside them substitute too.
    if (auto* vecTy = dynamic_cast<ast::VecType*>(typeNode)) {
        ast::TypeNodePtr elt = vecTy->elementType
            ? substituteTypeParameter(vecTy->elementType.get(), typeParamMap)
            : nullptr;
        return std::make_unique<ast::VecType>(
            vecTy->loc, elt ? std::move(elt) : vecTy->elementType->clone());
    }
    if (auto* futTy = dynamic_cast<ast::FutureType*>(typeNode)) {
        ast::TypeNodePtr res = futTy->resultType
            ? substituteTypeParameter(futTy->resultType.get(), typeParamMap)
            : nullptr;
        return std::make_unique<ast::FutureType>(
            futTy->loc, res ? std::move(res) : futTy->resultType->clone());
    }
    if (auto* arrTy = dynamic_cast<ast::ArrayType*>(typeNode)) {
        ast::TypeNodePtr elt = arrTy->elementType
            ? substituteTypeParameter(arrTy->elementType.get(), typeParamMap)
            : nullptr;
        return std::make_unique<ast::ArrayType>(
            arrTy->loc, elt ? std::move(elt) : arrTy->elementType->clone());
    }

    // For other types, just clone
    return typeNode->clone();
}


// Apply the currently-active generic-function type substitutions (e.g. T -> Int,
// T -> Vec<Int>) to a list of generic type arguments, so a construction inside a
// monomorphized body such as `Box<T> { value = v }` lands on the concrete struct
// (Box_Int) instead of an unresolved Box_T.
std::vector<vyb::ast::TypeNodePtr> LLVMCodegen::applyTypeSubstitutions(
        const std::vector<vyb::ast::TypeNodePtr>& typeArgs) {
    std::vector<vyb::ast::TypeNodePtr> out;
    out.reserve(typeArgs.size());
    for (const auto& arg : typeArgs) {
        if (!arg) { out.emplace_back(); continue; }
        if (currentTypeSubstitutions.empty()) {
            out.push_back(arg->clone());
            continue;
        }
        std::string subst = arg->toString();
        for (const auto& kv : currentTypeSubstitutions) {
            subst = replaceTypeTokens(subst, kv.first, kv.second);
        }
        out.push_back(typePatternToTypeNode(TypePattern::parse(subst), arg->loc));
    }
    return out;
}

// Monomorphize a generic struct: create specialized LLVM type with type parameters substituted
// Example: Box<T> + [Int] -> Box_Int struct with field type T replaced by Int
llvm::StructType* LLVMCodegen::monomorphizeStruct(const std::string& baseName,
                                                   const std::vector<ast::TypeNodePtr>& typeArgs) {
    // Apply generic-function type substitutions only when one is active; otherwise
    // use the caller's type args directly so resolved type metadata is preserved.
    std::vector<ast::TypeNodePtr> substArgs;
    const std::vector<ast::TypeNodePtr>& effectiveArgs =
        currentTypeSubstitutions.empty() ? typeArgs
                                         : (substArgs = applyTypeSubstitutions(typeArgs), substArgs);
    // Generate mangled name for this instantiation
    std::string mangledName = mangleGenericTypeName(baseName, effectiveArgs);

    VYB_CDBG << "DEBUG: Monomorphizing " << baseName << " with " << typeArgs.size()
              << " type arguments -> " << mangledName << std::endl;

    // Check cache first
    auto cacheIt = monomorphizedStructs.find(mangledName);
    if (cacheIt != monomorphizedStructs.end()) {
        VYB_CDBG << "DEBUG: Found cached monomorphized struct: " << mangledName << std::endl;
        return cacheIt->second;
    }

    // Look up the generic struct template
    auto templateIt = genericStructTemplates.find(baseName);
    if (templateIt == genericStructTemplates.end()) {
        std::cerr << "ERROR: No generic struct template found for: " << baseName << std::endl;
        return nullptr;
    }

    ast::StructDeclaration* templateNode = templateIt->second;

    // Verify type argument count matches
    if (typeArgs.size() != templateNode->genericParams.size()) {
        std::cerr << "ERROR: Type argument count mismatch for " << baseName
                  << ": expected " << templateNode->genericParams.size()
                  << ", got " << typeArgs.size() << std::endl;
        return nullptr;
    }

    // Create type parameter substitution map (T -> Int, etc.)
    std::map<std::string, ast::TypeNode*> typeParamMap;
    for (size_t i = 0; i < templateNode->genericParams.size(); ++i) {
        const auto& param = templateNode->genericParams[i];
        if (param && param->name) {
            std::string paramName = param->name->name;
            typeParamMap[paramName] = effectiveArgs[i].get();
            VYB_CDBG << "DEBUG: Type parameter mapping: " << paramName << " -> "
                      << effectiveArgs[i]->toString() << std::endl;
        }
    }

    // Create the specialized LLVM struct type
    llvm::StructType* specializedType = llvm::StructType::create(*context, mangledName);

    // Create UserTypeInfo for the specialized type
    UserTypeInfo typeInfo;
    typeInfo.llvmType = specializedType;
    typeInfo.isStruct = true;

    // Add to userTypeMap early (for circular references)
    userTypeMap[mangledName] = typeInfo;

    // Process fields with type substitution
    std::vector<llvm::Type*> fieldTypes;
    for (size_t i = 0; i < templateNode->fields.size(); ++i) {
        const auto& fieldDecl = templateNode->fields[i];
        if (!fieldDecl || !fieldDecl->typeNode) {
            std::cerr << "ERROR: Field missing type in template " << baseName << std::endl;
            return nullptr;
        }

        // Substitute type parameters in field type
        ast::TypeNodePtr substitutedType = substituteTypeParameter(fieldDecl->typeNode.get(), typeParamMap);

        VYB_CDBG << "DEBUG: Field '" << fieldDecl->name->name << "' original type: "
                  << fieldDecl->typeNode->toString()
                  << " -> substituted: " << substitutedType->toString() << std::endl;

        // Generate LLVM type for substituted field type
        llvm::Type* fieldLLVMType = codegenType(substitutedType.get());
        if (!fieldLLVMType) {
            std::cerr << "ERROR: Could not generate LLVM type for field '"
                      << fieldDecl->name->name << "' in " << mangledName << std::endl;
            return nullptr;
        }

        fieldTypes.push_back(fieldLLVMType);
        typeInfo.fieldIndices[fieldDecl->name->name] = i;
    }

    // Set the struct body
    specializedType->setBody(fieldTypes, /*isPacked=*/false);

    VYB_CDBG << "DEBUG: Created specialized struct " << mangledName
              << " with " << fieldTypes.size() << " fields" << std::endl;

    // Update userTypeMap with complete field information
    userTypeMap[mangledName] = typeInfo;

    // Cache the monomorphized struct
    monomorphizedStructs[mangledName] = specializedType;

    // Generate type metadata for JSON serialization
    generateTypeMetadata(mangledName, templateNode);

    return specializedType;
}

// Monomorphize a generic data enum: create a specialized tagged-union LLVM type
// with the type parameters substituted. Example: Box<T> + [Int] ->
// Box_Int { i64 tag, [N x i8] data } with Value's payload field typed Int.
llvm::StructType* LLVMCodegen::monomorphizeEnum(const std::string& baseName,
                                                 const std::vector<ast::TypeNodePtr>& typeArgs) {
    // Apply generic-function type substitutions only when one is active; otherwise
    // use the caller's type args directly so resolved type metadata is preserved.
    std::vector<ast::TypeNodePtr> substArgs;
    const std::vector<ast::TypeNodePtr>& effectiveArgs =
        currentTypeSubstitutions.empty() ? typeArgs
                                         : (substArgs = applyTypeSubstitutions(typeArgs), substArgs);
    std::string mangledName = mangleGenericTypeName(baseName, effectiveArgs);

    // Built-in generic data enums: `enum Option<T> { Some(T), None }` and
    // `enum Result<T, E> { Ok(T), Err(E) }`. They are not declared in source, so
    // build their tagged-union layouts directly from the payload type arguments
    // rather than a codegen-registered template.
    if (baseName == "Option" || baseName == "core::option::Option" ||
        baseName == "Result" || baseName == "core::result::Result") {
        auto cacheIt = taggedEnumInfo.find(mangledName);
        if (cacheIt != taggedEnumInfo.end()) return cacheIt->second.llvmType;
        TaggedEnumInfo info;
        const bool isOption = (baseName == "Option" || baseName == "core::option::Option");
        struct PayloadVariant { const char* name; unsigned typeArgIdx; };
        std::vector<PayloadVariant> payloadVariants;
        int64_t tag = 0;
        if (isOption) {
            info.variantTags["Some"] = static_cast<unsigned>(tag++);
            payloadVariants.push_back({"Some", 0});
            info.variantTags["None"] = static_cast<unsigned>(tag++);
        } else {
            info.variantTags["Ok"] = static_cast<unsigned>(tag++);
            payloadVariants.push_back({"Ok", 0});
            info.variantTags["Err"] = static_cast<unsigned>(tag++);
            payloadVariants.push_back({"Err", 1});
        }
        unsigned payloadBytes = 0;
        for (const auto& pv : payloadVariants) {
            if (pv.typeArgIdx >= effectiveArgs.size() || !effectiveArgs[pv.typeArgIdx]) continue;
            llvm::Type* payloadTy = codegenType(effectiveArgs[pv.typeArgIdx].get());
            if (!payloadTy) payloadTy = llvm::Type::getInt64Ty(*context);
            llvm::StructType* payloadStruct = llvm::StructType::get(*context, {payloadTy}, false);
            info.variantPayloadTypes[pv.name] = payloadStruct;
            llvm::TypeSize sz = module->getDataLayout().getTypeAllocSize(payloadStruct);
            unsigned bytes = static_cast<unsigned>(sz.getFixedValue());
            if (bytes > payloadBytes) payloadBytes = bytes;
        }
        if (payloadBytes == 0) payloadBytes = 1;
        info.payloadBytes = payloadBytes;
        llvm::StructType* enumStruct = llvm::StructType::create(*context, mangledName);
        enumStruct->setBody({llvm::Type::getInt64Ty(*context),
            llvm::ArrayType::get(llvm::Type::getInt8Ty(*context), payloadBytes)}, /*isPacked=*/false);
        info.llvmType = enumStruct;
        taggedEnumInfo[mangledName] = info;
        return enumStruct;
    }

    // Cached? (monomorphized structs are registered under their mangled name)
    auto cacheIt = taggedEnumInfo.find(mangledName);
    if (cacheIt != taggedEnumInfo.end()) return cacheIt->second.llvmType;

    auto templateIt = genericEnumTemplates.find(baseName);
    if (templateIt == genericEnumTemplates.end()) {
        logError(SourceLocation(), "No generic enum template found for: " + baseName);
        return nullptr;
    }
    ast::EnumDeclaration* templateNode = templateIt->second;
    if (effectiveArgs.size() != templateNode->genericParams.size()) {
        logError(SourceLocation(), "Type argument count mismatch for generic enum " + baseName);
        return nullptr;
    }

    // Mapping from generic parameter name to concrete type argument (T -> Int).
    std::map<std::string, ast::TypeNode*> typeParamMap;
    for (size_t i = 0; i < templateNode->genericParams.size(); ++i) {
        const auto& param = templateNode->genericParams[i];
        if (param && param->name) typeParamMap[param->name->name] = effectiveArgs[i].get();
    }

    TaggedEnumInfo info;
    unsigned payloadBytes = 0;
    int64_t tag = 0;
    for (const auto& v : templateNode->variants) {
        if (!v || !v->name) continue;
        const std::string& vname = v->name->name;
        info.variantTags[vname] = static_cast<unsigned>(tag);
        if (!v->associatedTypes.empty()) {
            std::vector<llvm::Type*> fieldTypes;
            for (const auto& t : v->associatedTypes) {
                ast::TypeNodePtr sub = substituteTypeParameter(t.get(), typeParamMap);
                llvm::Type* ft = sub ? codegenType(sub.get()) : nullptr;
                if (!ft) ft = llvm::Type::getInt64Ty(*context);
                fieldTypes.push_back(ft);
            }
            llvm::StructType* payloadTy = llvm::StructType::get(*context, fieldTypes, false);
            info.variantPayloadTypes[vname] = payloadTy;
            const llvm::DataLayout& dl = module->getDataLayout();
            llvm::TypeSize sz = dl.getTypeAllocSize(payloadTy);
            if (sz.getFixedValue() > payloadBytes) {
                payloadBytes = static_cast<unsigned>(sz.getFixedValue());
            }
        }
        tag++;
    }
    if (payloadBytes == 0) payloadBytes = 1;
    info.payloadBytes = payloadBytes;

    llvm::StructType* enumStruct = llvm::StructType::create(*context, mangledName);
    enumStruct->setBody({llvm::Type::getInt64Ty(*context),
        llvm::ArrayType::get(llvm::Type::getInt8Ty(*context), payloadBytes)}, /*isPacked=*/false);
    info.llvmType = enumStruct;
    taggedEnumInfo[mangledName] = info;
    return enumStruct;
}

} // namespace vyb
