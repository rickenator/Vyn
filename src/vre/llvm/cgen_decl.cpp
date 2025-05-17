\
#include "vyn/vre/llvm/codegen.hpp"
#include "vyn/parser/ast.hpp"

#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instructions.h> // For AllocaInst, ReturnInst, BranchInst etc.
#include <llvm/IR/Constants.h>    // For Constant, UndefValue
#include <llvm/IR/Verifier.h>     // For verifyFunction
#include <llvm/Support/raw_ostream.h> // For llvm::errs for verifyFunction output

#include <string>
#include <vector>
#include <map>

using namespace vyn;
// using namespace llvm; // Uncomment if desired for brevity

// --- Declarations ---

std::unique_ptr<llvm::Module> LLVMCodegen::releaseModule() {
    return std::move(module);
}

void LLVMCodegen::visit(vyn::ast::VariableDeclaration* node) {
    llvm::Value* initialVal = nullptr;
    llvm::Type* varType = nullptr;

    if (node->typeNode) {
        varType = codegenType(node->typeNode.get());
        if (!varType) {
            logError(node->loc, "Could not determine LLVM type for variable '" + node->id->name + "'.");
            m_currentLLVMValue = nullptr;
            return;
        }
    }

    if (node->init) {
        node->init->accept(*this);
        initialVal = m_currentLLVMValue;
        if (!initialVal) {
            logError(node->init->loc, "Initialization expression for variable '" + node->id->name + "' evaluated to null.");
            m_currentLLVMValue = nullptr;
            return;
        }
        if (!varType) {
            varType = initialVal->getType();
        } else {
            // Type check: initialVal type must be assignable to varType
            if (initialVal->getType() != varType) {
                llvm::Value* castedVal = tryCast(initialVal, varType, node->init->loc);
                if (castedVal) {
                    initialVal = castedVal;
                } else {
                    logError(node->init->loc, "Type mismatch for variable '" + node->id->name + 
                                             "'. Initializer type " + getTypeName(initialVal->getType()) + 
                                             " is not assignable to declared type " + getTypeName(varType));
                    m_currentLLVMValue = nullptr;
                    return;
                }
            }
        }
    } else { // No initializer
        if (!varType) {
            logError(node->loc, "Variable '" + node->id->name + "' has no initializer and no explicit type. Type inference from no initializer is not possible.");
            m_currentLLVMValue = nullptr;
            return;
        }
        // Default initialization (e.g., 0 for numbers, nullptr for pointers)
        // Ensure it's not a void type, which cannot be default initialized this way.
        if (varType->isVoidTy()){
            logError(node->loc, "Cannot declare a variable of type void: " + node->id->name);
            m_currentLLVMValue = nullptr;
            return;
        }
        initialVal = llvm::Constant::getNullValue(varType);
    }

    if (!currentFunction) { // Global variable
        // Global variables must have constant initializers.
        if (!llvm::isa<llvm::Constant>(initialVal)) {
            logError(node->init ? node->init->loc : node->loc, 
                     "Global variable '" + node->id->name + "' initializer must be a constant.");
            m_currentLLVMValue = nullptr;
            return;
        }
        auto* globalVar = new llvm::GlobalVariable(
            *module,
            varType,
            node->isConst, // LLVM's const for global means its value is constant, not necessarily its memory
            llvm::GlobalValue::PrivateLinkage, // Or ExternalLinkage if exported
            static_cast<llvm::Constant*>(initialVal),
            node->id->name
        );
        namedValues[node->id->name] = globalVar;
        m_currentLLVMValue = globalVar;
    } else { // Local variable
        llvm::AllocaInst* alloca = llvm::dyn_cast_or_null<llvm::AllocaInst>(
            createEntryBlockAlloca(currentFunction, node->id->name, varType)
        );
        if (!alloca) {
             logError(node->loc, "Failed to create alloca instruction for local variable '" + node->id->name + "'.");
             m_currentLLVMValue = nullptr;
             return;
        }
        builder->CreateStore(initialVal, alloca);
        namedValues[node->id->name] = alloca; 
        m_currentLLVMValue = alloca; // The "value" of a declaration is its memory location (for l-value purposes)
    }
}

void LLVMCodegen::visit(vyn::ast::FunctionDeclaration* node) {
    std::vector<llvm::Type*> paramTypes;
    std::vector<std::string> paramNames;
    for (const auto& paramNode : node->params) {
        if (!paramNode.typeNode) {
            logError(paramNode.name->loc, "Parameter '" + paramNode.name->name + "' in function '" + node->id->name + "' is missing a type annotation.");
            m_currentLLVMValue = nullptr; return;
        }
        llvm::Type* llvmType = codegenType(paramNode.typeNode.get());
        if (!llvmType) {
            logError(paramNode.name->loc, "Could not determine LLVM type for parameter '" + paramNode.name->name + "' in function '" + node->id->name + "'.");
            m_currentLLVMValue = nullptr; return;
        }
        paramTypes.push_back(llvmType);
        paramNames.push_back(paramNode.name->name);
    }

    llvm::Type* returnType = nullptr;
    if (node->returnTypeNode) {
        returnType = codegenType(node->returnTypeNode.get());
        if (!returnType) {
            logError(node->loc, "Could not determine LLVM return type for function '" + node->id->name + "'.");
            m_currentLLVMValue = nullptr; return;
        }
    } else {
        returnType = llvm::Type::getVoidTy(*context);
    }
    
    llvm::FunctionType* funcType = llvm::FunctionType::get(returnType, paramTypes, false /*isVarArg*/);
    
    // Check for existing function (could be forward declaration or redefinition)
    llvm::Function* func = module->getFunction(node->id->name);
    if (func) {
        if (func->getFunctionType() != funcType) {
            logError(node->loc, "Redefinition of function '" + node->id->name + "' with different signature.");
            m_currentLLVMValue = nullptr; return;
        }
        if (!func->empty() && node->body) { // Already has a body, and we are trying to define another
            logError(node->loc, "Redefinition of function '" + node->id->name + "'.");
            m_currentLLVMValue = nullptr; return;
        }
        // If it was a forward declaration and types match, we are now providing the body.
    } else {
        func = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, node->id->name, module.get());
    }

    // Set current function for subsequent codegen (body, variable declarations)
    llvm::Function* oldFunction = currentFunction;
    currentFunction = func;

    // Store old namedValues and create a new scope for the function arguments and locals
    std::map<std::string, llvm::Value*> oldNamedValues;
    oldNamedValues.swap(namedValues); 

    if (node->body) { 
        llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(*context, "entry", func);
        builder->SetInsertPoint(entryBB);

        // Create allocas for parameters and store initial argument values
        auto argIt = func->arg_begin();
        for (size_t i = 0; i < paramTypes.size(); ++i, ++argIt) {
            llvm::Argument* argVal = &*argIt;
            argVal->setName(paramNames[i]);

            llvm::AllocaInst* alloca = llvm::dyn_cast_or_null<llvm::AllocaInst>(createEntryBlockAlloca(func, paramNames[i], paramTypes[i]));
            if (!alloca) {
                logError(node->params[i].name->loc, "Failed to create alloca for parameter '" + paramNames[i] + "'.");
                // Cleanup might be needed here
                currentFunction = oldFunction;
                namedValues.swap(oldNamedValues);
                m_currentLLVMValue = nullptr; return;
            }
            builder->CreateStore(argVal, alloca);
            namedValues[paramNames[i]] = alloca;
        }
        
        node->body->accept(*this); // Generate code for the function body

        // Verify function return: ensure all paths return if non-void, or add implicit void return
        if (returnType->isVoidTy()) {
            // Check if the last block has a terminator. If not, add ret void.
            if (!func->empty() && !func->back().getTerminator()) {
                builder->CreateRetVoid();
            }
        } else {
            // For non-void functions, ensure all paths return. LLVM's verifier will catch most issues.
            // A simple check: if the last block in a non-empty function doesn't have a terminator, it's an error.
            if (!func->empty() && !func->back().getTerminator()) {
                logError(node->loc, "Function '" + node->id->name + "' has a non-void return type but may not return on all paths (missing return at end of body).");
                // Optionally, builder->CreateUnreachable(); if this state should be impossible.
            }
        }
        
        // Verify the generated function
        if (llvm::verifyFunction(*func, &llvm::errs())) {
            logError(node->loc, "LLVM function verification failed for '" + node->id->name + "'. Errors printed to stderr.");
            // func->print(llvm::errs()); // Print the malformed function
            // Consider erasing the function: func->eraseFromParent();
            // For now, let it be, so errors are visible.
        }

    } // else it's a forward declaration or extern, no body to generate now.

    // Restore outer scope
    currentFunction = oldFunction;
    namedValues.swap(oldNamedValues); 

    m_currentLLVMValue = func; // The "value" of a function declaration is the function itself
}

void LLVMCodegen::visit(vyn::ast::StructDeclaration* node) {
    std::string nameStr = node->name->name;
    llvm::StructType* structType = llvm::StructType::create(*context, nameStr);

    UserTypeInfo typeInfo;
    typeInfo.llvmType = structType;
    typeInfo.isStruct = true;

    std::vector<llvm::Type*> fieldTypes;
    for (size_t i = 0; i < node->fields.size(); ++i) {
        const auto& fieldDecl = node->fields[i];
        if (!fieldDecl->typeNode) { // Changed .type to ->typeNode based on ast.hpp for FieldDeclaration
            logError(fieldDecl->name->loc, "Field \'" + fieldDecl->name->name + "\' in struct \'" + nameStr + "\' is missing a type.");
            m_currentLLVMValue = nullptr; return;
        }
        llvm::Type* fieldType = codegenType(fieldDecl->typeNode.get()); // Changed .type to ->typeNode
        if (!fieldType) {
            logError(fieldDecl->name->loc, "Could not determine LLVM type for field \'" + fieldDecl->name->name + "\' in struct \'" + nameStr + "\'.");
            m_currentLLVMValue = nullptr; return;
        }
        fieldTypes.push_back(fieldType);
        typeInfo.fieldIndices[fieldDecl->name->name] = i; // Changed .name to ->name
    }

    structType->setBody(fieldTypes, /*isPacked=*/false);
    userTypeMap[nameStr] = typeInfo;
    m_currentLLVMValue = nullptr; // structType is an llvm::Type*, not llvm::Value*
}

void LLVMCodegen::visit(vyn::ast::ClassDeclaration* node) {
    // Similar to StructDeclaration, but might involve vtables, inheritance, etc. in the future.
    // For now, treat classes like structs.
    std::string nameStr = node->name->name;
    llvm::StructType* classType = llvm::StructType::create(*context, nameStr);
    currentClassType = classType; // Set for member functions

    UserTypeInfo typeInfo;
    typeInfo.llvmType = classType;
    typeInfo.isStruct = false; // It's a class

    std::vector<llvm::Type*> fieldTypes;
    // TODO: Add vtable pointer as the first field if Vyn classes have virtual methods.
    // llvm::Type* vtablePtrType = llvm::PointerType::getUnqual(llvm::FunctionType::get(voidType, true)->getPointerTo());
    // fieldTypes.push_back(vtablePtrType);
    // typeInfo.fieldIndices["_vptr"] = 0; // Example vptr

    unsigned fieldIdxOffset = fieldTypes.size(); // Start field indices after any implicit members like vptr

    for (size_t i = 0; i < node->members.size(); ++i) {
        // Assuming members are fields for now. Methods are handled separately or as part of ImplDeclaration.
        // This part needs to distinguish between FieldDeclaration and MethodDeclaration within ClassDeclaration.
        // For now, let's assume `node->members` contains `FieldDeclaration` like nodes.
        // If `node->members` is a generic `Declaration*`, we need to dyn_cast.
        // Let's assume `node->fields` for explicit field declarations as in StructDecl for now.
        // This needs clarification from ast.hpp for ClassDeclaration members.
        // For now, skipping direct field processing here, assuming Impl blocks or similar will define them,
        // or that ClassDeclaration has a `fields` member like StructDeclaration.
        // Let's assume `node->fields` for now, similar to StructDeclaration for simplicity.
        /*
        if (auto* fieldDeclNode = dynamic_cast<ast::FieldDeclaration*>(node->members[i].get())) {
             if (!fieldDeclNode->typeNode) { logError(...); return; }
             llvm::Type* fieldType = codegenType(fieldDeclNode->typeNode.get());
             if (!fieldType) { logError(...); return; }
             fieldTypes.push_back(fieldType);
             typeInfo.fieldIndices[fieldDeclNode->id->name] = i + fieldIdxOffset;
        } else if (auto* methodDeclNode = dynamic_cast<ast::FunctionDeclaration*>(node->members[i].get())) {
            // Method declarations might contribute to vtable or be standalone functions.
            // Their codegen is typically handled when visiting the FunctionDeclaration itself,
            // with `currentClassType` set to associate them.
        }
        */
    }
    // Placeholder: If ClassDeclaration has a `fields` vector like StructDeclaration:
    // Iterating over node->members which are Declaration*, need to cast to FieldDeclaration*
    for (const auto& memberDecl : node->members) { // Changed from node->fields to node->members
        if (auto* fieldDecl = dynamic_cast<ast::FieldDeclaration*>(memberDecl.get())) {
            if (!fieldDecl->typeNode) { // was fieldDecl.type
                logError(fieldDecl->name->loc, "Field \'" + fieldDecl->name->name + "\' in class \'" + nameStr + "\' is missing a type.");
                m_currentLLVMValue = nullptr; return;
            }
            llvm::Type* fieldType = codegenType(fieldDecl->typeNode.get()); // was fieldDecl.type
            if (!fieldType) {
                logError(fieldDecl->name->loc, "Could not determine LLVM type for field \'" + fieldDecl->name->name + "\' in class \'" + nameStr + "\'.");
                m_currentLLVMValue = nullptr; return;
            }
            fieldTypes.push_back(fieldType);
            typeInfo.fieldIndices[fieldDecl->name->name] = fieldTypes.size() -1 + fieldIdxOffset; // was fieldDecl.name
        }
        // else if (auto* methodDecl = dynamic_cast<ast::FunctionDeclaration*>(memberDecl.get())) {
        // Methods are handled when `impl` block is visited or if defined inline.
        // Here we are only collecting explicit field types for the class structure.
        // }
    }


    classType->setBody(fieldTypes, /*isPacked=*/false);
    userTypeMap[nameStr] = typeInfo;
    m_currentLLVMValue = nullptr; // classType is an llvm::Type*, not llvm::Value*
    currentClassType = nullptr; // Reset after processing class
}

void LLVMCodegen::visit(vyn::ast::TypeAliasDeclaration* node) {
    // type NewType = OldType;
    // This primarily affects the type system and semantic analysis.
    // For codegen, it means NewType is equivalent to OldType\'s LLVM type.
    // We can record this equivalence if needed, but codegenType should resolve OldType.
    llvm::Type* originalLlvmType = codegenType(node->typeNode.get()); // Corrected: Was aliasedTypeNode, ast.hpp shows 'typeNode'
    if (!originalLlvmType) {
        logError(node->loc, "Could not resolve original type for type alias \'" + node->name->name + "\'."); // ast.hpp shows 'name' for the alias name
        m_currentLLVMValue = nullptr;
        return;
    }
    // Store this alias in a way that codegenType can use it for `aliasName`.
    // One way is to add it to userTypeMap if it's a struct-like alias, or a specific alias map.
    // For simplicity, if it's an alias to a struct, we can treat it like defining a new struct type that is identical.
    // However, LLVM types are structural, so if originalLlvmType is `StructType*`, then `aliasName` just refers to it.
    // The main thing is that when `codegenType` encounters `aliasName`, it should return `originalLlvmType`.
    // This is usually handled by the type system before codegen or by adding `aliasName` -> `originalLlvmType` to `m_typeCache` or `userTypeMap`.

    // If the alias name should be a distinct named type in LLVM (even if structurally identical):
    // llvm::StructType::create(*context, node->aliasName->name)->setBody(originalLlvmType-> ...get fields... );
    // But this is usually not what's desired for a simple alias.

    // For now, let's ensure that if `codegenType` is called with `aliasName` later,
    // it can resolve to `originalLlvmType`. This might involve populating `m_typeCache`
    // or ensuring the parser/semantic analyzer resolves aliases before `codegenType` sees them.
    // A simple approach for codegen: if `userTypeMap` can store `llvm::Type*` directly:
    // userTypeMap[node->aliasName->name] = { originalLlvmType, {}, true/false based on original };
    // This needs UserTypeInfo to be more flexible or a different map for aliases.

    // For now, we assume the type system handles this, and `codegenType(aliasName)` would work.
    // The value of a type alias declaration itself is not an LLVM value.
    logError(node->loc, "TypeAliasDeclaration codegen is conceptual. \'" + node->name->name + "\' will resolve to underlying type."); // ast.hpp shows 'name'
    m_currentLLVMValue = nullptr; // No direct LLVM value for an alias declaration itself.
}

void LLVMCodegen::visit(vyn::ast::ImportDeclaration* node) {
    // Imports are typically handled by a module loader or linker phase before/during codegen.
    // The codegen itself might not do much other than note that symbols from `node->modulePath` are available.
    // If Vyn compiles modules separately and links them, this is a linker concern.
    // If it\'s more like C++ #include, then parsing of the imported module would happen earlier.
    // ast.hpp: std::unique_ptr<StringLiteral> source;
    // std::vector<ImportSpecifier> specifiers;
    // std::unique_ptr<Identifier> defaultImport;
    // std::unique_ptr<Identifier> namespaceImport;

    std::string importSource = node->source ? node->source->value : "unknown"; // Using 'source' from ast.hpp

    logError(node->loc, "ImportDeclaration handling is typically a pre-codegen or linking step. Module source: " + importSource);
    m_currentLLVMValue = nullptr;
}

void LLVMCodegen::visit(vyn::ast::FieldDeclaration* node) {
    // FieldDeclarations are part of StructDeclaration or ClassDeclaration.
    // They are processed within those visitors, not typically visited standalone by the main codegen loop.
    // If visited standalone, it implies context is missing (e.g. which struct/class it belongs to).
    logError(node->loc, "FieldDeclaration '" + node->name->name + "' visited standalone. Should be part of struct/class.");
    m_currentLLVMValue = nullptr;
}

void LLVMCodegen::visit(vyn::ast::ImplDeclaration* node) {
    // `impl MyStruct { fn foo(&self) {} }`
    // This associates methods with a type.
    // Set `currentClassType` (or a more general `currentImplTargetType`)
    // so that FunctionDeclarations inside this block know their `this`/`self` type.

    // ast.hpp: TypeNodePtr selfType; // The type being implemented
    // ast.hpp: std::vector<std::unique_ptr<FunctionDeclaration>> methods;

    llvm::Type* targetType = codegenType(node->selfType.get()); // Corrected: Was targetType, ast.hpp shows 'selfType'
    if (!targetType || !targetType->isStructTy()) {
        logError(node->loc, "Target type for impl block is not a known struct/class type: " + node->selfType->toString()); // Corrected: Was targetType
        m_currentLLVMValue = nullptr; return;
    }
    llvm::StructType* oldCurrentClassType = currentClassType;
    currentClassType = llvm::dyn_cast<llvm::StructType>(targetType);
    m_currentImplTypeNode = node->selfType.get(); // Corrected: Was targetType

    for (const auto& member : node->methods) { // Corrected: Was body, ast.hpp shows 'methods'
        // Members are typically FunctionDeclarations (methods)
        member->accept(*this); // This will call visit(FunctionDeclaration*)
    }

    currentClassType = oldCurrentClassType;
    m_currentImplTypeNode = nullptr;
    m_currentLLVMValue = nullptr; // Impl block itself doesn't produce a value
}

void LLVMCodegen::visit(vyn::ast::EnumDeclaration* node) {
    // Enums in Vyn could be C-like (integer-based) or more complex (tagged unions like Rust/Swift).
    // This implementation sketch assumes C-like enums for simplicity, mapping to integers.
    // For tagged unions, each variant would be a struct, and the enum type itself a wrapper struct or i8* + tag.
    logError(node->loc, "EnumDeclaration codegen is not fully implemented (assuming C-like integer enums for now).");
    
    // For C-like enums, we might just define constants for each variant.
    // llvm::Type* enumBaseType = int32Type; // Or infer from values
    // int64_t currentValue = 0;
    // for (const auto& variantNode : node->variants) {
    //     std::string variantName = node->name->name + "::" + variantNode->name->name; // Or however Vyn names enum variants
    //     if (variantNode->value) { // If explicit value
    //         variantNode->value->accept(*this);
    //         if (auto* constInt = llvm::dyn_cast_or_null<llvm::ConstantInt>(m_currentLLVMValue)) {
    //             currentValue = constInt->getSExtValue();
    //         } else {
    //             logError(variantNode->loc, "Enum variant value for '" + variantName + "' is not a constant integer.");
    //         }
    //     }
    //     llvm::Constant* enumConst = llvm::ConstantInt::get(enumBaseType, currentValue);
    //     // Store this constant somewhere accessible, e.g. in namedValues or a specific enum map.
    //     // namedValues[variantName] = enumConst; 
    //     // This makes `MyEnum::Variant` resolve to the integer constant.
    //     currentValue++;
    // }
    m_currentLLVMValue = nullptr; // Enum declaration itself doesn't have a direct LLVM value in this model
}

void LLVMCodegen::visit(vyn::ast::EnumVariantNode* node) {
    // Visited as part of EnumDeclaration. Not typically standalone.
    logError(node->loc, "EnumVariantNode visited standalone.");
    m_currentLLVMValue = nullptr;
}

void LLVMCodegen::visit(vyn::ast::GenericParamNode* node) {
    // Generic parameters are resolved during template instantiation (monomorphization).
    // Standalone codegen for a GenericParamNode is not typical.
    logError(node->loc, "GenericParamNode visited standalone. Should be resolved during template instantiation.");
    m_currentLLVMValue = nullptr;
}

void LLVMCodegen::visit(vyn::ast::TemplateDeclarationNode* node) {
    // Template declarations are blueprints. Code is generated when they are instantiated.
    // The codegen might store the template definition for later instantiation.
    logError(node->loc, "TemplateDeclarationNode visited. Codegen happens on instantiation, not for the template itself.");
    m_currentLLVMValue = nullptr;
}

