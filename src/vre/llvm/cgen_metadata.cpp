// SPDX-License-Identifier: Apache-2.0

// Vyb Type Metadata Generation
// Generates runtime type metadata for JSON serialization and reflection

#include "vyb/vre/llvm/codegen.hpp"
#include <algorithm>
#include <functional>
#include <stdexcept>
#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>

namespace vyb {

// Generate field metadata for a struct type
void LLVMCodegen::generateTypeMetadata(const std::string& typeName, ast::StructDeclaration* structDecl) {
    if (!structDecl) return;

    VYB_CDBG << "DEBUG: Generating type metadata for: " << typeName << std::endl;

    // Get the LLVM struct type
    auto structIt = monomorphizedStructs.find(typeName);
    if (structIt == monomorphizedStructs.end()) {
        std::cerr << "ERROR: Could not find struct type " << typeName << " for metadata generation" << std::endl;
        return;
    }
    llvm::StructType* structType = structIt->second;

    // Guard against by-value-cyclic struct types (e.g. `struct Node { next<Node?> }`,
    // where the optional embeds Node by value). LLVM cannot size such a type and
    // DataLayout::getTypeSizeInBits recurses forever -> stack overflow. isSized() with
    // a visited set terminates and reports "not sized", so turn it into a clean error
    // instead of a hard crash. Recursive/self-referential structs are not supported yet.
    {
        llvm::SmallPtrSet<llvm::Type*, 8> visited;
        if (structType && !structType->isSized(&visited)) {
            throw std::runtime_error("struct '" + typeName +
                "' embeds itself by value (recursive/self-referential struct field); "
                "recursive struct types are not supported yet");
        }
    }

    // Create arrays for field metadata
    std::vector<llvm::Constant*> fieldMetadataArray;

    for (size_t i = 0; i < structDecl->fields.size(); ++i) {
        const auto& fieldDecl = structDecl->fields[i];
        if (!fieldDecl || !fieldDecl->typeNode) continue;

        std::string fieldName = fieldDecl->name->name;
        std::string fieldTypeName = fieldDecl->typeNode->toString();

        // Determine if field is primitive
        bool isPrimitive = (fieldTypeName == "Int" || fieldTypeName == "Float" ||
                           fieldTypeName == "Bool" || fieldTypeName == "String");

        // TODO: Detect Vec types
        bool isVec = false;

        // Create field metadata struct
        // typedef struct VybFieldMetadata {
        //     const char* name;
        //     const char* type_name;
        //     size_t offset;
        //     size_t size;
        //     bool is_primitive;
        //     bool is_vec;
        //     const char* vec_element_type;
        // } VybFieldMetadata;

        // Create global string constants manually (not using builder, which requires function context)
        llvm::Constant* fieldNameStrConst = llvm::ConstantDataArray::getString(*context, fieldName, /*AddNull=*/true);
        llvm::GlobalVariable* fieldNameGlobal = new llvm::GlobalVariable(
            *module, fieldNameStrConst->getType(), true,
            llvm::GlobalValue::PrivateLinkage, fieldNameStrConst,
            "field_name_" + typeName + "_" + fieldName
        );
        llvm::Constant* fieldNameStr = llvm::ConstantExpr::getBitCast(
            fieldNameGlobal,
            llvm::PointerType::get(llvm::Type::getInt8Ty(*context), 0)
        );

        llvm::Constant* fieldTypeStrConst = llvm::ConstantDataArray::getString(*context, fieldTypeName, /*AddNull=*/true);
        llvm::GlobalVariable* fieldTypeGlobal = new llvm::GlobalVariable(
            *module, fieldTypeStrConst->getType(), true,
            llvm::GlobalValue::PrivateLinkage, fieldTypeStrConst,
            "field_type_" + typeName + "_" + fieldName
        );
        llvm::Constant* fieldTypeNameStr = llvm::ConstantExpr::getBitCast(
            fieldTypeGlobal,
            llvm::PointerType::get(llvm::Type::getInt8Ty(*context), 0)
        );

        // Get field offset and size from LLVM DataLayout
        const llvm::DataLayout& dataLayout = module->getDataLayout();
        const llvm::StructLayout* structLayout = dataLayout.getStructLayout(structType);
        uint64_t offset = structLayout->getElementOffset(i);
        llvm::Type* fieldType = structType->getElementType(i);
        uint64_t size = dataLayout.getTypeAllocSize(fieldType);

        // Create field metadata constant
        llvm::PointerType* int8PtrType = llvm::PointerType::get(llvm::Type::getInt8Ty(*context), 0);
        llvm::Type* int64Type = llvm::Type::getInt64Ty(*context);
        llvm::Type* int1Type = llvm::Type::getInt1Ty(*context);

        std::vector<llvm::Constant*> fieldMetadataFields = {
            fieldNameStr,
            fieldTypeNameStr,
            llvm::ConstantInt::get(int64Type, offset),
            llvm::ConstantInt::get(int64Type, size),
            llvm::ConstantInt::get(int1Type, isPrimitive),
            llvm::ConstantInt::get(int1Type, isVec),
            llvm::ConstantPointerNull::get(int8PtrType) // vec_element_type (NULL for now)
        };

        // Create anonymous struct type for field metadata
        llvm::StructType* fieldMetadataType = llvm::StructType::get(
            *context,
            {int8PtrType, int8PtrType, int64Type, int64Type, int1Type, int1Type, int8PtrType},
            /*isPacked=*/false
        );

        llvm::Constant* fieldMetadata = llvm::ConstantStruct::get(fieldMetadataType, fieldMetadataFields);
        fieldMetadataArray.push_back(fieldMetadata);
    }

    // Create global array of field metadata
    if (fieldMetadataArray.empty()) {
        VYB_CDBG << "DEBUG: No fields to generate metadata for " << typeName << std::endl;
        return;
    }

    llvm::ArrayType* fieldArrayType = llvm::ArrayType::get(
        fieldMetadataArray[0]->getType(),
        fieldMetadataArray.size()
    );

    llvm::Constant* fieldArrayInit = llvm::ConstantArray::get(fieldArrayType, fieldMetadataArray);

    llvm::GlobalVariable* fieldArrayGlobal = new llvm::GlobalVariable(
        *module,
        fieldArrayType,
        true, // isConstant
        llvm::GlobalValue::PrivateLinkage,
        fieldArrayInit,
        "__vyb_fields_" + typeName
    );

    // Create VybTypeMetadata struct
    // typedef struct VybTypeMetadata {
    //     const char* type_name;
    //     size_t struct_size;
    //     size_t num_fields;
    //     VybFieldMetadata* fields;
    //     size_t num_aspects;
    //     VybAspectBinding* aspects;
    // } VybTypeMetadata;

    llvm::PointerType* int8PtrType = llvm::PointerType::get(llvm::Type::getInt8Ty(*context), 0);
    llvm::Type* int64Type = llvm::Type::getInt64Ty(*context);

    // Create type name string constant
    llvm::Constant* typeNameStrConst = llvm::ConstantDataArray::getString(*context, typeName, /*AddNull=*/true);
    llvm::GlobalVariable* typeNameGlobal = new llvm::GlobalVariable(
        *module, typeNameStrConst->getType(), true,
        llvm::GlobalValue::PrivateLinkage, typeNameStrConst,
        "type_name_" + typeName
    );
    llvm::Constant* typeNameStr = llvm::ConstantExpr::getBitCast(
        typeNameGlobal,
        int8PtrType
    );

    const llvm::DataLayout& dataLayout = module->getDataLayout();
    uint64_t structSize = dataLayout.getTypeAllocSize(structType);

    std::vector<llvm::Constant*> typeMetadataFields = {
        typeNameStr,
        llvm::ConstantInt::get(int64Type, structSize),
        llvm::ConstantInt::get(int64Type, fieldMetadataArray.size()),
        llvm::ConstantExpr::getBitCast(fieldArrayGlobal, int8PtrType), // Cast array to pointer
        llvm::ConstantInt::get(int64Type, 0), // num_aspects (TODO: implement aspect metadata)
        llvm::ConstantPointerNull::get(int8PtrType) // aspects (NULL for now)
    };

    llvm::StructType* typeMetadataStructType = llvm::StructType::get(
        *context,
        {int8PtrType, int64Type, int64Type, int8PtrType, int64Type, int8PtrType},
        /*isPacked=*/false
    );

    llvm::Constant* typeMetadataInit = llvm::ConstantStruct::get(typeMetadataStructType, typeMetadataFields);

    llvm::GlobalVariable* typeMetadataGlobal = new llvm::GlobalVariable(
        *module,
        typeMetadataStructType,
        true, // isConstant
        llvm::GlobalValue::ExternalLinkage, // Make it visible for runtime registration
        typeMetadataInit,
        "__vyb_metadata_" + typeName
    );

    // Add registration call (will be called at program startup)
    // We need to generate a constructor function that calls __vyb_register_type
    // For now, we'll just store the metadata global and register it later
    typeMetadataGlobals[typeName] = typeMetadataGlobal;

    VYB_CDBG << "DEBUG: Generated metadata for " << typeName
              << " with " << fieldMetadataArray.size() << " fields" << std::endl;
}

// Register all type metadata at runtime
void LLVMCodegen::registerTypeMetadata() {
    if (typeMetadataGlobals.empty()) {
        VYB_CDBG << "DEBUG: No type metadata to register" << std::endl;
        return;
    }

    VYB_CDBG << "DEBUG: Registering " << typeMetadataGlobals.size() << " type metadata entries" << std::endl;

    // Declare __vyb_register_type function
    llvm::PointerType* int8PtrType = llvm::PointerType::get(llvm::Type::getInt8Ty(*context), 0);
    llvm::FunctionType* registerFuncType = llvm::FunctionType::get(
        llvm::Type::getVoidTy(*context),
        {int8PtrType}, // Takes VybTypeMetadata* (as i8*)
        false
    );

    llvm::Function* registerFunc = module->getFunction("__vyb_register_type");
    if (!registerFunc) {
        registerFunc = llvm::Function::Create(
            registerFuncType,
            llvm::Function::ExternalLinkage,
            "__vyb_register_type",
            module.get()
        );
    }

    // Declare __vyb_register_enum function
    llvm::Function* registerEnumFunc = module->getFunction("__vyb_register_enum");
    if (!registerEnumFunc) {
        registerEnumFunc = llvm::Function::Create(
            registerFuncType,
            llvm::Function::ExternalLinkage,
            "__vyb_register_enum",
            module.get()
        );
    }

    // Create a global constructor function to register all types at startup
    llvm::FunctionType* ctorType = llvm::FunctionType::get(
        llvm::Type::getVoidTy(*context),
        false
    );

    llvm::Function* ctorFunc = llvm::Function::Create(
        ctorType,
        llvm::Function::ExternalLinkage,  // Make it visible to JIT
        "__vyb_register_all_types",
        module.get()
    );

    llvm::BasicBlock* ctorBB = llvm::BasicBlock::Create(*context, "entry", ctorFunc);
    llvm::IRBuilder<> ctorBuilder(ctorBB);

    // Call __vyb_register_type for each type
    for (const auto& pair : typeMetadataGlobals) {
        llvm::Value* metadataPtr = ctorBuilder.CreateBitCast(pair.second, int8PtrType);
        ctorBuilder.CreateCall(registerFunc, {metadataPtr});
        VYB_CDBG << "DEBUG: Added registration call for type: " << pair.first << std::endl;
    }

    // Call __vyb_register_enum for each enum
    for (const auto& pair : enumMetadataGlobals) {
        llvm::Value* metadataPtr = ctorBuilder.CreateBitCast(pair.second, int8PtrType);
        ctorBuilder.CreateCall(registerEnumFunc, {metadataPtr});
        VYB_CDBG << "DEBUG: Added enum registration call for: " << pair.first << std::endl;
    }

    ctorBuilder.CreateRetVoid();

    // Add this function to the global constructors list manually
    // This ensures it runs before main()
    // The llvm.global_ctors array contains {i32 priority, void()* ctor, i8* associated data or null}
    llvm::StructType* ctorStructType = llvm::StructType::get(
        *context,
        {
            llvm::Type::getInt32Ty(*context),  // priority
            ctorType->getPointerTo(),           // function pointer
            int8PtrType                         // associated data (null)
        }
    );

    std::vector<llvm::Constant*> ctorArray;
    ctorArray.push_back(llvm::ConstantStruct::get(
        ctorStructType,
        {
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 65535),  // priority
            ctorFunc,
            llvm::ConstantPointerNull::get(llvm::PointerType::get(llvm::Type::getInt8Ty(*context), 0))
        }
    ));

    llvm::ArrayType* ctorArrayType = llvm::ArrayType::get(ctorStructType, ctorArray.size());
    new llvm::GlobalVariable(
        *module,
        ctorArrayType,
        false,  // not constant
        llvm::GlobalValue::AppendingLinkage,
        llvm::ConstantArray::get(ctorArrayType, ctorArray),
        "llvm.global_ctors"
    );

    VYB_CDBG << "DEBUG: Type metadata registration complete" << std::endl;
}

// Generate enum metadata for a tagged-union enum. Emits a VybEnumMetadata
// constant (type_name, payload_size, variant array) so struct fields of an enum
// type can round-trip through JSON (runtime/vyb_type_metadata.c) instead of
// collapsing to null. Each variant records its name, tag, and payload field
// metadata (type_name + byte offset within the { i64, [N x i8] } data area).
void LLVMCodegen::generateEnumTypeMetadata(const std::string& typeName, vyb::ast::EnumDeclaration* enumDecl) {
    if (!enumDecl) return;

    auto it = taggedEnumInfo.find(typeName);
    if (it == taggedEnumInfo.end()) {
        VYB_CDBG << "DEBUG: No tagged-enum info for " << typeName << "; skipping enum metadata" << std::endl;
        return;
    }
    const TaggedEnumInfo& info = it->second;
    if (info.isScalar) {
        // C-like scalar enums are just an i64 tag with no struct/payload layout;
        // they carry no serializable payload, so nothing to register.
        return;
    }

    VYB_CDBG << "DEBUG: Generating enum metadata for: " << typeName << std::endl;

    llvm::PointerType* int8PtrType = llvm::PointerType::get(llvm::Type::getInt8Ty(*context), 0);
    llvm::Type* int64Type = llvm::Type::getInt64Ty(*context);
    const llvm::DataLayout& dataLayout = module->getDataLayout();

    // VybEnumField = { const char* type_name; size_t offset; }
    llvm::StructType* enumFieldType = llvm::StructType::get(
        *context, {int8PtrType, int64Type}, false);
    // VybEnumVariant = { const char* name; uint64 tag; size_t num_fields; VybEnumField* fields; }
    llvm::StructType* enumVariantType = llvm::StructType::get(
        *context, {int8PtrType, int64Type, int64Type, int8PtrType}, false);
    // VybEnumMetadata = { const char* type_name; size_t payload_size; size_t num_variants; VybEnumVariant* variants; }
    llvm::StructType* enumMetaType = llvm::StructType::get(
        *context, {int8PtrType, int64Type, int64Type, int8PtrType}, false);

    std::vector<llvm::Constant*> variantConstants;

    for (const auto& variantNode : enumDecl->variants) {
        if (!variantNode || !variantNode->name) continue;
        const std::string vname = variantNode->name->name;

        unsigned tag = 0;
        auto tagIt = info.variantTags.find(vname);
        if (tagIt != info.variantTags.end()) tag = tagIt->second;

        // Build the payload field metadata (type_name + byte offset in data area).
        std::vector<llvm::Constant*> fieldConstants;
        auto payIt = info.variantPayloadTypes.find(vname);
        if (payIt != info.variantPayloadTypes.end() && payIt->second) {
            llvm::StructType* payloadTy = payIt->second;
            const llvm::StructLayout* layout = dataLayout.getStructLayout(payloadTy);
            for (size_t fi = 0; fi < variantNode->associatedTypes.size() && fi < payloadTy->getNumElements(); ++fi) {
                std::string ftype = variantNode->associatedTypes[fi]->toString();
                llvm::Constant* ftypeConst = llvm::ConstantDataArray::getString(*context, ftype, true);
                llvm::GlobalVariable* ftypeGlobal = new llvm::GlobalVariable(
                    *module, ftypeConst->getType(), true, llvm::GlobalValue::PrivateLinkage,
                    ftypeConst, "enum_field_type_" + typeName + "_" + vname + "_" + std::to_string(fi));
                llvm::Constant* ftypeStr = llvm::ConstantExpr::getBitCast(ftypeGlobal, int8PtrType);
                llvm::Constant* offsetConst = llvm::ConstantInt::get(
                    int64Type, layout->getElementOffset(fi));
                fieldConstants.push_back(llvm::ConstantStruct::get(enumFieldType, {ftypeStr, offsetConst}));
            }
        }

        llvm::GlobalVariable* fieldArrayGlobal = nullptr;
        if (!fieldConstants.empty()) {
            llvm::ArrayType* fieldArrayType = llvm::ArrayType::get(enumFieldType, fieldConstants.size());
            llvm::Constant* fieldArrayInit = llvm::ConstantArray::get(fieldArrayType, fieldConstants);
            fieldArrayGlobal = new llvm::GlobalVariable(
                *module, fieldArrayType, true, llvm::GlobalValue::PrivateLinkage,
                fieldArrayInit, "__vyb_enum_fields_" + typeName + "_" + vname);
        }

        llvm::Constant* nameConst = llvm::ConstantDataArray::getString(*context, vname, true);
        llvm::GlobalVariable* nameGlobal = new llvm::GlobalVariable(
            *module, nameConst->getType(), true, llvm::GlobalValue::PrivateLinkage,
            nameConst, "enum_variant_name_" + typeName + "_" + vname);
        llvm::Constant* nameStr = llvm::ConstantExpr::getBitCast(nameGlobal, int8PtrType);

        variantConstants.push_back(llvm::ConstantStruct::get(enumVariantType, {
            nameStr,
            llvm::ConstantInt::get(int64Type, (uint64_t)tag),
            llvm::ConstantInt::get(int64Type, (uint64_t)fieldConstants.size()),
            fieldArrayGlobal ? llvm::ConstantExpr::getBitCast(fieldArrayGlobal, int8PtrType)
                             : llvm::ConstantPointerNull::get(int8PtrType)
        }));
    }

    if (variantConstants.empty()) return;

    llvm::ArrayType* variantArrayType = llvm::ArrayType::get(enumVariantType, variantConstants.size());
    llvm::Constant* variantArrayInit = llvm::ConstantArray::get(variantArrayType, variantConstants);
    llvm::GlobalVariable* variantArrayGlobal = new llvm::GlobalVariable(
        *module, variantArrayType, true, llvm::GlobalValue::PrivateLinkage,
        variantArrayInit, "__vyb_enum_variants_" + typeName);

    llvm::Constant* typeNameConst = llvm::ConstantDataArray::getString(*context, typeName, true);
    llvm::GlobalVariable* typeNameGlobal = new llvm::GlobalVariable(
        *module, typeNameConst->getType(), true, llvm::GlobalValue::PrivateLinkage,
        typeNameConst, "enum_type_name_" + typeName);
    llvm::Constant* typeNameStr = llvm::ConstantExpr::getBitCast(typeNameGlobal, int8PtrType);

    llvm::Constant* enumMetaInit = llvm::ConstantStruct::get(enumMetaType, {
        typeNameStr,
        llvm::ConstantInt::get(int64Type, (uint64_t)info.payloadBytes),
        llvm::ConstantInt::get(int64Type, (uint64_t)variantConstants.size()),
        llvm::ConstantExpr::getBitCast(variantArrayGlobal, int8PtrType)
    });

    llvm::GlobalVariable* enumMetaGlobal = new llvm::GlobalVariable(
        *module, enumMetaType, true, llvm::GlobalValue::ExternalLinkage,
        enumMetaInit, "__vyb_enum_metadata_" + typeName);

    enumMetadataGlobals[typeName] = enumMetaGlobal;
    VYB_CDBG << "DEBUG: Generated enum metadata for " << typeName
             << " with " << variantConstants.size() << " variants" << std::endl;
}

// Register every compile-time-known type name in the runtime type identity
// registry so a runtime `Type` value (an opaque uint64 hash) can be resolved
// back to its name via `__vyb_get_typename` (used by `typename(t)` where `t` is
// a `Type`). Runs before main via an llvm.global_ctors entry.
void LLVMCodegen::registerTypeNames() {
    std::vector<std::string> names = {
        "Type", "Int", "Float", "Bool", "String", "Char", "Rune",
        "Int8", "Int16", "Int32", "Int64",
        "UInt8", "UInt16", "UInt32", "UInt64",
        "Float32", "Float64"
    };
    for (const auto& kv : userTypeMap) {
        names.push_back(kv.first);
    }

    std::vector<std::string> unique;
    for (const auto& n : names) {
        if (std::find(unique.begin(), unique.end(), n) == unique.end()) {
            unique.push_back(n);
        }
    }

    llvm::Type* i64Ty = llvm::Type::getInt64Ty(*context);
    llvm::PointerType* i8Ptr = llvm::PointerType::get(*context, 0);

    // Declare the runtime registration function __vyb_register_typename(id, name)
    llvm::FunctionType* registerFnTy = llvm::FunctionType::get(
        llvm::Type::getVoidTy(*context), {i64Ty, i8Ptr}, false);
    llvm::Function* registerFn = module->getFunction("__vyb_register_typename");
    if (!registerFn) {
        registerFn = llvm::Function::Create(registerFnTy, llvm::Function::ExternalLinkage,
                                            "__vyb_register_typename", module.get());
    }

    // Build __vyb_module_init() that registers each type name under its type hash.
    // It may already exist as a declaration (main calls it); fill in the body here.
    llvm::FunctionType* initTy = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), false);
    llvm::Function* initFunc = module->getFunction("__vyb_module_init");
    bool defineInit = false;
    if (!initFunc) {
        initFunc = llvm::Function::Create(initTy, llvm::Function::ExternalLinkage,
                                          "__vyb_module_init", module.get());
        defineInit = true;
    } else if (initFunc->size() == 0) {
        defineInit = true; // declaration only; needs a body
    }
    if (defineInit) {
        llvm::BasicBlock* entry = llvm::BasicBlock::Create(*context, "entry", initFunc);
        llvm::IRBuilder<> initBuilder(entry);

        for (const auto& name : unique) {
            uint64_t typeHash = std::hash<std::string>{}(name);
            llvm::Value* id = llvm::ConstantInt::get(i64Ty, typeHash);
            llvm::Value* nameStr = initBuilder.CreateGlobalStringPtr(name);
            initBuilder.CreateCall(registerFn, {id, nameStr});
        }
        // Module-level globals whose initializers could not be constants (they
        // reference other globals or compute a value) are stored here so they
        // hold the correct runtime value before any function body runs.
        if (!pendingGlobalInits_.empty()) {
            builder->SetInsertPoint(initBuilder.GetInsertBlock());
            for (auto& globalInit : pendingGlobalInits_) {
                llvm::GlobalVariable* globalVar = globalInit.first;
                vyb::ast::Expression* initExpr = globalInit.second;
                initExpr->accept(*this);
                if (llvm::Value* val = m_currentLLVMValue) {
                    builder->CreateStore(val, globalVar);
                } else {
                    logError(initExpr->loc, "Initializer for global variable failed to evaluate.");
                }
            }
            initBuilder.SetInsertPoint(builder->GetInsertBlock());
        }
        initBuilder.CreateRetVoid();
    }

    VYB_CDBG << "DEBUG: Registered " << unique.size() << " type names in module init" << std::endl;
}

} // namespace vyb
