\
#include "vyb/vre/llvm/codegen.hpp"
#include "vyb/parser/ast.hpp"

#include <set>
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instructions.h> // For AllocaInst, ReturnInst, BranchInst etc.
#include <llvm/IR/Constants.h>    // For Constant, UndefValue
#include <llvm/IR/Verifier.h>     // For verifyFunction
#include <llvm/Support/raw_ostream.h> // For llvm::errs for verifyFunction output

#include <string>
#include <vector>
#include <map>

// File-local helper: detect the Vyb String struct representation { ptr, i64 }.
// Used by both function declaration and forward-declaration code paths.
static bool isVybStringStructType(llvm::Type* t) {
    if (!t->isStructTy()) return false;
    auto* st = llvm::cast<llvm::StructType>(t);
    return st->getNumElements() == 2 &&
           st->getElementType(0)->isPointerTy() &&
           st->getElementType(1)->isIntegerTy(64);
}

using namespace vyb;
// using namespace llvm; // Uncomment if desired for brevity

// --- Declarations ---

void LLVMCodegen::visit(ast::AspectDeclaration* node) {
    // Traits are interfaces/type constraints that don't generate runtime code by themselves
    // They're primarily used for compile-time type checking and polymorphism

    // Verify trait name
    if (!node->name) {
        logError(node->loc, "Trait declaration missing name");
        m_currentLLVMValue = nullptr;
        return;
    }

    std::string traitName = node->name->name;
    if (traitName.empty()) {
        logError(node->loc, "Trait declaration has empty name");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Process generic parameters if any
    std::vector<std::string> genericParamNames;
    for (const auto& param : node->genericParams) {
        if (param && param->name) {
            genericParamNames.push_back(param->name->name);
        }
    }

    // Generate code for aspect methods that have default implementations
    // Methods without bodies are just signatures (no codegen needed)
    // Methods with bodies need LLVM functions so they can be called when bind doesn't override

    for (const auto& method : node->methods) {
        if (method && method->body) {
            VYB_CDBG << "DEBUG: Generating default implementation for aspect method: "
                      << traitName << "::" << method->id->name << std::endl;

            // Generate the method - but we can't use Self type directly
            // We'll generate a generic version that will be instantiated per type
            // For now, skip codegen for aspect default methods - they'll be generated
            // on-demand when a concrete type needs them
            // TODO: Implement on-demand generation or pre-generate for all implementing types
            VYB_CDBG << "DEBUG: Skipping codegen for aspect default method (needs per-type instantiation)" << std::endl;
        }
    }

    VYB_CDBG << "DEBUG: Trait '" << traitName << "' declaration processed in codegen" << std::endl;

    // Traits don't produce runtime values
    m_currentLLVMValue = nullptr;
}

void LLVMCodegen::visit(ast::NamespaceDeclaration* node) {
    // Namespaces provide scoping for declarations but don't generate runtime code themselves

    // Check if namespace name is valid
    if (!node->name) {
        logError(node->loc, "Namespace declaration missing name");
        m_currentLLVMValue = nullptr;
        return;
    }

    std::string namespaceName = node->name->name;
    if (namespaceName.empty()) {
        logError(node->loc, "Namespace declaration has empty name");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Store the current namespace context to restore it later
    std::string currentNamespacePrefix = ""; // In a full implementation, we would track the current namespace
    std::string newNamespacePrefix = namespaceName + "::";

    // Process all declarations in the namespace
    // For each declaration, we'll mangle its name to include the namespace prefix
    for (const auto& member : node->members) {
        if (member) {
            // Process the declaration
            member->accept(*this);
        }
    }

    // Restore the previous namespace context
    // (In a full implementation)

    // Namespaces don't produce runtime values
    m_currentLLVMValue = nullptr;
}

std::unique_ptr<llvm::Module> LLVMCodegen::releaseModule() {
    // The DIBuilder holds metadata tracking state that references both the
    // module and the context. Destroy it here, while both are still owned by
    // this codegen object. Otherwise, after releaseContext() moves the context
    // out of scope, the codegen destructor's DIBuilder teardown runs against a
    // freed context, causing a use-after-free in LLVM metadata cleanup.
    debugBuilder.reset();
    return std::move(module);
}

std::unique_ptr<llvm::LLVMContext> LLVMCodegen::releaseContext() {
    // Defensive: if releaseContext() is ever called without releaseModule(),
    // tear the DIBuilder down here while the context is still alive.
    debugBuilder.reset();
    return std::move(context);
}

void LLVMCodegen::visit(vyb::ast::VariableDeclaration* node) {
    llvm::Value* initialVal = nullptr;
    llvm::Type* varType = nullptr;

    // Variable declaration processing
    if (node->typeNode) {
        varType = codegenType(node->typeNode.get());
        if (!varType) {
            logError(node->loc, "Could not determine LLVM type for variable '" + node->id->name + "'.");
            m_currentLLVMValue = nullptr;
            return;
        }
    }

    if (node->init) {
        // Make sure the initializer knows its intended type if available
        if (node->typeNode && !node->init->type) {
            node->init->type = node->typeNode->clone();
        }

        node->init->accept(*this);
        initialVal = m_currentLLVMValue;
        if (!initialVal) {
            logError(node->init->loc, "Initialization expression for variable '" + node->id->name + "' evaluated to null.");
            m_currentLLVMValue = nullptr;
            return;
        }

        // Track lambda function types for later indirect calling
        if (dynamic_cast<vyb::ast::FunctionExpression*>(node->init.get())) {
            if (auto* lambdaFunc = llvm::dyn_cast<llvm::Function>(initialVal)) {
                localLambdaTypes[node->id->name] = lambdaFunc->getFunctionType();
            }
        }

        // If the initializer was an ObjectLiteral, ensure we properly set up type information
        if (auto* objLiteral = dynamic_cast<vyb::ast::ObjectLiteral*>(node->init.get())) {
            if (objLiteral->typePath && !node->typeNode) {
                // Create a TypeNode for the variable based on the ObjectLiteral's type
                node->typeNode = objLiteral->typePath->clone();
            }
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
        // Propagate type info for struct/class variables
        if (node->typeNode) {
            valueTypeMap[globalVar] = std::shared_ptr<vyb::ast::TypeNode>(node->typeNode->clone());
        }
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
        // Register the variable in namedValues
        namedValues[node->id->name] = alloca;
        // Store the type info for this variable (with type substitution if in monomorphization)
        if (node->id->type) {
            // Check if we need to substitute type parameters
            if (!currentTypeSubstitutions.empty()) {
                if (auto* typeName = dynamic_cast<ast::TypeName*>(node->id->type.get())) {
                    if (typeName->identifier) {
                        std::string typeStr = typeName->identifier->name;
                        auto substIt = currentTypeSubstitutions.find(typeStr);
                        if (substIt != currentTypeSubstitutions.end()) {
                            // Create substituted TypeName
                            auto substitutedType = std::make_unique<ast::TypeName>(
                                typeName->loc,
                                std::make_unique<ast::Identifier>(typeName->loc, substIt->second),
                                std::vector<ast::TypeNodePtr>()
                            );
                            valueTypeMap[alloca] = std::shared_ptr<ast::TypeNode>(std::move(substitutedType));
                            VYB_CDBG << "DEBUG: Variable '" << node->id->name << "' type substituted in valueTypeMap: "
                                      << typeStr << " -> " << substIt->second << std::endl;
                        } else {
                            valueTypeMap[alloca] = node->id->type;
                        }
                    } else {
                        valueTypeMap[alloca] = node->id->type;
                    }
                } else {
                    valueTypeMap[alloca] = node->id->type;
                }
            } else {
                valueTypeMap[alloca] = node->id->type;
            }
        }

        // Determine ownership kind from variable's type annotation
        ast::OwnershipKind ownership = ast::OwnershipKind::MY; // Default to MY ownership
        bool needsCleanup = false;

        // Extract ownership from type annotation
        if (node->typeNode) {
            std::string typeString = node->typeNode->toString();
            VYB_CDBG << "DEBUG: Variable '" << node->id->name << "' has AST type: '" << typeString << "'" << std::endl;

            // Check for ownership type wrappers
            if (auto* typeName = dynamic_cast<ast::TypeName*>(node->typeNode.get())) {
                if (typeName->identifier) {
                    std::string typeNameStr = typeName->identifier->name;

                    // Detect ownership types
                    if (typeNameStr == "my") {
                        ownership = ast::OwnershipKind::MY;
                        needsCleanup = true;
                        VYB_CDBG << "DEBUG: Variable '" << node->id->name << "' has MY ownership - needs cleanup" << std::endl;
                    } else if (typeNameStr == "our") {
                        ownership = ast::OwnershipKind::OUR;
                        needsCleanup = true;
                        VYB_CDBG << "DEBUG: Variable '" << node->id->name << "' has OUR ownership - needs cleanup" << std::endl;
                    } else if (typeNameStr == "their") {
                        ownership = ast::OwnershipKind::THEIR;
                        needsCleanup = false;
                        VYB_CDBG << "DEBUG: Variable '" << node->id->name << "' has THEIR ownership - no cleanup" << std::endl;
                    } else if (typeNameStr == "mild") {
                        ownership = ast::OwnershipKind::MILD;
                        needsCleanup = true;
                        VYB_CDBG << "DEBUG: Variable '" << node->id->name << "' has MILD ownership - needs cleanup" << std::endl;
                    }
                }
            }

            // Also check for Vec types that need cleanup
            if (typeString.find("Vec") != std::string::npos) {
                needsCleanup = true;
                VYB_CDBG << "DEBUG: Variable '" << node->id->name << "' is a Vec type requiring cleanup" << std::endl;
            }
        }

        // Fall back to LLVM struct type name check for Vec
        if (!needsCleanup) {
            if (auto structType = llvm::dyn_cast<llvm::StructType>(varType)) {
                std::string typeName = structType->getName().str();
                VYB_CDBG << "DEBUG: Variable '" << node->id->name << "' has struct type: '" << typeName << "'" << std::endl;
                if (typeName.find("Vec") != std::string::npos) {
                    needsCleanup = true;
                    VYB_CDBG << "DEBUG: Variable '" << node->id->name << "' is a Vec type (from LLVM) requiring cleanup" << std::endl;
                }
            }
        }

        // Register variable for scope-based cleanup
        registerVariable(node->id->name, alloca, initialVal, ownership, varType, needsCleanup);

        // Create debug information for the variable
        if (debugBuilder && !debugScopeStack.empty()) {
            std::string typeName = getTypeName(varType);
            llvm::DIType* debugType = getDebugType(varType, typeName);
            if (debugType) {
                llvm::DILocalVariable* debugVar = createDebugVariableInfo(
                    node->id->name, debugType, node->loc);
                if (debugVar) {
                    insertDebugVariableDeclaration(debugVar, alloca, node->loc);
                }
            }
        }

        m_currentLLVMValue = alloca;
        // Propagate type info for struct/class variables
        if (node->typeNode) {
            // Use resolved type if available (e.g., TypeName->type contains resolved TupleTypeNode or VecType)
            vyb::ast::TypeNode* typeToStore = node->typeNode->type ? node->typeNode->type.get() : node->typeNode.get();
            valueTypeMap[alloca] = std::shared_ptr<vyb::ast::TypeNode>(typeToStore->clone());
        }
    }
}

void LLVMCodegen::visit(vyb::ast::FunctionDeclaration* node) {
    VYB_CDBG << "DEBUG: FunctionDeclaration: " << node->id->name << std::endl;
    // DEBUG: Show error propagation metadata
    VYB_CDBG << "DEBUG: Function '" << node->id->name << "' - canFail=" << node->canFail
              << ", needsErrorReturn=" << node->needsErrorReturn
              << ", errorTypes.size=" << node->errorTypes.size() << std::endl;

    // Check if this is a generic function (has type parameters)
    if (!node->genericParams.empty()) {
        VYB_CDBG << "DEBUG: Storing generic function template: " << node->id->name
                  << " with " << node->genericParams.size() << " type parameters" << std::endl;

        // Store the generic function template for later monomorphization
        genericFunctionTemplates[node->id->name] = node;

        // Don't codegen generic functions directly - they'll be monomorphized on call
        m_currentLLVMValue = nullptr;
        return;
    }

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
    llvm::Type* originalReturnType = nullptr;  // Store original type before wrapping

    if (node->returnTypeNode) {
        if (currentAsyncState.isAsync) {
            // For async functions, the actual return type is wrapped in Future<T>
            originalReturnType = codegenType(node->returnTypeNode.get());
            if (!originalReturnType) {
                logError(node->loc, "Could not determine LLVM return type for async function '" + node->id->name + "'.");
                m_currentLLVMValue = nullptr; return;
            }
            returnType = createFutureStructType(originalReturnType);
        } else {
            originalReturnType = codegenType(node->returnTypeNode.get());
            VYB_CDBG << "DEBUG: Function " << node->id->name << " return type resolved to: "
                      << getTypeName(originalReturnType) << " with pointer: " << originalReturnType << std::endl;
            if (!originalReturnType) {
                logError(node->loc, "Could not determine LLVM return type for function '" + node->id->name + "'.");
                m_currentLLVMValue = nullptr; return;
            }

            // Phase 2: Wrap return type in {T, ptr} for failable functions
            if (node->needsErrorReturn) {
                VYB_CDBG << "DEBUG: Wrapping return type in {T, ptr} for failable function '"
                          << node->id->name << "'" << std::endl;
                llvm::Type* errorPtrType = llvm::PointerType::get(*context, 0);  // i8*
                if (originalReturnType->isVoidTy()) {
                    // Explicit <Void> return type still uses the failable void ABI {i1, i8*}.
                    returnType = llvm::StructType::get(*context, {llvm::Type::getInt1Ty(*context), errorPtrType});
                } else {
                    returnType = llvm::StructType::get(*context, {originalReturnType, errorPtrType});
                }
            } else {
                returnType = originalReturnType;
            }
        }

        // Auto-serialization for main():
        // - main()<Void>: nothing to serialize — no change.
        // - main()<String>: handled specially in the JIT runner (main.cpp) — no change.
        // - All other types (Int, Bool, Float, multi-value tuples): change return type to void
        //   and emit serialization (JSON) code in the return statement (cgen_stmt.cpp).
        //   m_mainAutoSerializeOrigRetType records the original type for cgen_stmt.
        if (node->id->name == "main" && !node->needsErrorReturn) {
            bool isVoidReturn  = returnType->isVoidTy();
            bool isStringRet   = isVybStringStructType(returnType);
            if (!isVoidReturn && !isStringRet) {
                // Emit serialization inside main(); change LLVM return type to void.
                m_mainAutoSerializeOrigRetType = returnType;
                returnType = voidType;
            }
        }
    } else {
        if (currentAsyncState.isAsync) {
            // Async void function returns Future<void>
            originalReturnType = llvm::Type::getVoidTy(*context);
            returnType = createFutureStructType(originalReturnType);
        } else {
            originalReturnType = llvm::Type::getVoidTy(*context);

            // Phase 2 ABI choice:
            // Keep one uniform failable ABI shape for codegen paths: {payload, error_ptr}.
            // For Void payloads we use i1 as a dummy field, giving {i1, i8*}.
            if (node->needsErrorReturn) {
                VYB_CDBG << "DEBUG: Wrapping void return in {i1, ptr} for failable function '"
                          << node->id->name << "' (using i1 as dummy)" << std::endl;
                llvm::Type* errorPtrType = llvm::PointerType::get(*context, 0);
                // Use i1 (bool) as dummy value for void functions
                returnType = llvm::StructType::get(*context, {llvm::Type::getInt1Ty(*context), errorPtrType});
            } else {
                returnType = originalReturnType;
            }
        }
    }

    llvm::FunctionType* funcType = llvm::FunctionType::get(returnType, paramTypes, false /*isVarArg*/);

    // DEBUG: Print the function type we're creating
    std::string funcTypeStr;
    llvm::raw_string_ostream typeStream(funcTypeStr);
    funcType->print(typeStream);
    VYB_CDBG << "DEBUG: Creating function '" << node->id->name << "' with type: " << typeStream.str() << std::endl;

    // Mangle function name if inside a bind/impl block
    std::string functionName = node->id->name;
    if (m_currentImplTypeNode) {
        // Create mangled name: TypeName[_TraitName]_methodName
        // (e.g., Person_goodbye, Thing_DisplayA_show). Including the trait
        // name disambiguates types that bind multiple aspects which declare
        // the same method name.
        functionName = m_currentImplTypeNode->toString();
        if (!m_currentImplTraitName.empty()) {
            functionName += "_" + m_currentImplTraitName;
        }
        functionName += "_" + node->id->name;
        VYB_CDBG << "DEBUG: Mangling bind method name: " << node->id->name
                  << " -> " << functionName << std::endl;
    }

    // Check for existing function (could be forward declaration or redefinition)
    llvm::Function* func = module->getFunction(functionName);
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
        func = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, functionName, module.get());
    }

    // Phase 6.4: Prevent inlining to preserve stack traces
    // Add noinline attribute so functions appear in call stack
    func->addFnAttr(llvm::Attribute::NoInline);

    // DEBUG: Print the ACTUAL function type after creation/retrieval
    std::string actualFuncTypeStr;
    llvm::raw_string_ostream actualTypeStream(actualFuncTypeStr);
    func->getFunctionType()->print(actualTypeStream);
    VYB_CDBG << "DEBUG: ACTUAL function '" << functionName << "' has type: " << actualTypeStream.str() << std::endl;

    // Set current function for subsequent codegen (body, variable declarations)
    llvm::Function* oldFunction = currentFunction;
    vyb::ast::FunctionDeclaration* oldFunctionAST = currentFunctionAST;
    currentFunction = func;
    currentFunctionAST = node;  // Track AST node for error propagation

    // Create debug information for the function
    llvm::DISubprogram* debugFunction = nullptr;
    if (node->body) { // Only create debug info for functions with bodies
        debugFunction = createDebugFunctionInfo(func, functionName, node->loc, node->isAsync);
    }

    // Handle async functions
    AsyncState oldAsyncState = currentAsyncState;
    if (node->isAsync) {
        currentAsyncState.isAsync = true;
        currentAsyncState.asyncFunction = func;
        currentAsyncState.stateCounter = 0;

        // Initialize debug information for async state machine
        initializeAsyncStateDebugInfo(functionName, node->loc);

        // For async functions, modify return type to Future<T> if not already
        if (node->returnTypeNode) {
            // Check if return type is already Future<T>
            auto futureType = dynamic_cast<ast::FutureType*>(node->returnTypeNode.get());
            if (!futureType) {
                // Wrap the return type in Future<T>
                // The actual LLVM function will return a Future struct
                llvm::Type* originalReturnType = returnType;
                llvm::StructType* futureStructType = createFutureStructType(originalReturnType);
                // Note: We keep the original function signature for now
                // The async transformation will happen during codegen
            }
        }
    } else {
        currentAsyncState.isAsync = false;
    }

    // Store old namedValues and create a new scope for the function arguments and locals
    std::map<std::string, llvm::Value*> oldNamedValues;
    oldNamedValues.swap(namedValues);

    if (node->body) {
        llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(*context, "entry", func);
        builder->SetInsertPoint(entryBB);

        // Set debug location for function entry
        setDebugLocation(node->loc);

        // Phase 6.4: Push call frame for stack trace capture
        generatePushFrameCall(functionName, node->loc);

        // Register all module types in the runtime type registry before any
        // program logic runs. (Called from main so it is reliable in the JIT,
        // where llvm.global_ctors may not be executed.)
        if (node->id->name == "main") {
            llvm::FunctionType* initTy = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), false);
            llvm::Function* initFn = module->getFunction("__vyb_module_init");
            if (!initFn) {
                initFn = llvm::Function::Create(initTy, llvm::Function::ExternalLinkage,
                                                "__vyb_module_init", module.get());
            }
            builder->CreateCall(initFn, {});
        }

        // Initialize scope management for function body
        enterScope();

        // Create allocas for parameters and store initial argument values
        auto argIt = func->arg_begin();
        for (size_t i = 0; i < paramTypes.size(); ++i, ++argIt) {
            llvm::Argument* argVal = &*argIt;
            argVal->setName(paramNames[i]);

            llvm::AllocaInst* alloca = llvm::dyn_cast_or_null<llvm::AllocaInst>(createEntryBlockAlloca(func, paramNames[i], paramTypes[i]));
            if (!alloca) {
                logError(node->params[i].name->loc, "Failed to create alloca for parameter '" + paramNames[i] + "'.");
                // Cleanup might be needed here
                exitScope(); // Clean up function scope before exiting
                currentFunction = oldFunction;
                currentFunctionAST = oldFunctionAST;
                namedValues.swap(oldNamedValues);
                m_currentLLVMValue = nullptr; return;
            }
            builder->CreateStore(argVal, alloca);
            namedValues[paramNames[i]] = alloca;

            // Store type information for function parameters
            if (node->params[i].typeNode) {
                valueTypeMap[alloca] = std::shared_ptr<vyb::ast::TypeNode>(node->params[i].typeNode->clone());
                VYB_CDBG << "DEBUG: Stored type mapping for parameter '" << paramNames[i] << "'" << std::endl;
            }

            // Deep-copy Vec parameters so that the callee owns independent data.
            // Without this, passing a Vec by value shares the data pointer between caller
            // and callee, causing double-frees when both try to clean up (e.g. in quicksort).
            bool vecParam = false;
            if (node->params[i].typeNode) {
                // Vec<T> can be represented as VecType OR as TypeName with identifier "Vec"
                ast::TypeNode* elemTypeNode = nullptr;
                if (auto* vecAstType = dynamic_cast<ast::VecType*>(node->params[i].typeNode.get())) {
                    if (vecAstType->elementType) elemTypeNode = vecAstType->elementType.get();
                } else if (auto* tnNode = dynamic_cast<ast::TypeName*>(node->params[i].typeNode.get())) {
                    if (tnNode->identifier && tnNode->identifier->name == "Vec" && !tnNode->genericArgs.empty()) {
                        elemTypeNode = tnNode->genericArgs[0].get();
                    }
                }
                if (elemTypeNode) {
                    llvm::Type* elemLLVMType = codegenType(elemTypeNode);
                    if (elemLLVMType) {
                        // Load the struct stored so far (the shallow copy)
                        llvm::Value* shallowVec = builder->CreateLoad(paramTypes[i], alloca, paramNames[i] + "_shallow");
                        // Clone the data
                        llvm::Value* deepVec = generateVecDeepCopy(shallowVec, elemLLVMType, paramTypes[i]);
                        if (deepVec) {
                            builder->CreateStore(deepVec, alloca);
                            VYB_CDBG << "DEBUG: Deep-copied Vec parameter '" << paramNames[i] << "'" << std::endl;
                            vecParam = true;
                        }
                    }
                }
            }

            // Register parameter for scope-based cleanup.
            // Vec parameters now own their data (deep-copied above) so they need cleanup.
            // Non-Vec parameters do not own heap data and are cleaned up by the caller.
            registerVariable(paramNames[i], alloca, argVal, ast::OwnershipKind::MY, paramTypes[i], vecParam);

            // Create debug information for the parameter
            if (debugBuilder && !debugScopeStack.empty()) {
                std::string typeName = getTypeName(paramTypes[i]);
                llvm::DIType* debugType = getDebugType(paramTypes[i], typeName);
                if (debugType) {
                    llvm::DILocalVariable* debugVar = createDebugVariableInfo(
                        paramNames[i], debugType, node->params[i].name->loc);
                    if (debugVar) {
                        insertDebugVariableDeclaration(debugVar, alloca, node->params[i].name->loc);
                    }
                }
            }
        }

        VYB_CDBG << "DEBUG: FunctionDeclaration - about to process function body" << std::endl;
        // Push a new defer scope for this function
        m_deferStack.push_back({});
        // Trap contexts are local to the function being generated. Clear any
        // context left over from an enclosing function so that a `fail` in this
        // callee propagates via the failable ABI instead of branching into a
        // caller's trap landing pad. Restored after the body is generated.
        std::vector<TrapContext> savedTrapStack;
        savedTrapStack.swap(trapStack);
        int savedTrapHandlerIndex = currentTrapHandlerIndex;
        currentTrapHandlerIndex = -1;

        node->body->accept(*this); // Generate code for the function body
        // Pop defer scope (any remaining deferred statements for implicit returns)
        if (!m_deferStack.empty()) {
            if (!m_deferStack.back().empty() && !func->empty() && !func->back().getTerminator()) {
                // Emit remaining deferred statements before implicit return
                auto& defers = m_deferStack.back();
                for (auto it = defers.rbegin(); it != defers.rend(); ++it) {
                    if (*it) (*it)->accept(*this);
                }
            }
            m_deferStack.pop_back();
        }
        VYB_CDBG << "DEBUG: FunctionDeclaration - finished processing function body" << std::endl;

        // Clean up function scope before return
        exitScope();

        // Verify function return: ensure all paths return if non-void, or add implicit return.
        const bool isFailableVoidFunction =
            node->needsErrorReturn && originalReturnType && originalReturnType->isVoidTy();
        if (returnType->isVoidTy()) {
            // Non-failable void function: if the last block has no terminator, add `ret void`.
            if (!func->empty() && !func->back().getTerminator()) {
                // Make sure we're inserting at the end of the last block
                builder->SetInsertPoint(&func->back());
                // Phase 6.4: Pop call frame before implicit return
                generatePopFrameCall();
                builder->CreateRetVoid();
            }
        } else if (isFailableVoidFunction) {
            if (!func->empty() && !func->back().getTerminator()) {
                builder->SetInsertPoint(&func->back());
                generatePopFrameCall();

                llvm::StructType* returnStructType = llvm::cast<llvm::StructType>(returnType);
                llvm::Value* nullErrorPtr = llvm::ConstantPointerNull::get(llvm::PointerType::get(*context, 0));
                llvm::Value* successStruct = llvm::UndefValue::get(returnStructType);
                successStruct = builder->CreateInsertValue(successStruct, llvm::ConstantInt::getFalse(*context), {0}, "implicit.void_dummy");
                successStruct = builder->CreateInsertValue(successStruct, nullErrorPtr, {1}, "implicit.error");
                builder->CreateRet(successStruct);
            }
        } else {
            // For non-void functions, ensure all paths return. LLVM's verifier will catch most issues.
            // A simple check: if the last block in a non-empty function doesn't have a terminator, it's an error.
            if (!func->empty() && !func->back().getTerminator()) {
                logError(node->loc, "Function '" + node->id->name + "' has a non-void return type but may not return on all paths (missing return at end of body).");
                // Keep the IR verifier happy: terminate the fall-through block with
                // `unreachable`. This happens when a function falls out of its last
                // statement (e.g. a `match` whose arms all `return`) without a final
                // trailing return; the diagnostic above reports the real bug.
                builder->SetInsertPoint(&func->back());
                builder->CreateUnreachable();
            }
        }

        // Verify the generated function
        if (llvm::verifyFunction(*func, &llvm::errs())) {
            logError(node->loc, "LLVM function verification failed for '" + node->id->name + "'. Errors printed to stderr.");
            // func->print(llvm::errs()); // Print the malformed function
            // Consider erasing the function: func->eraseFromParent();
            // For now, let it be, so errors are visible.
        }

        // Phase 6.4: DEBUG - Check if function still exists after verification
        VYB_CDBG << "DEBUG: Function '" << functionName << "' completed codegen, has "
                  << func->size() << " basic blocks" << std::endl;

        // Pop debug scope for function
        if (debugFunction) {
            popDebugScope();
        }

        // Restore the caller's trap contexts now that this function's body is done.
        trapStack.swap(savedTrapStack);
        currentTrapHandlerIndex = savedTrapHandlerIndex;
    } // else it's a forward declaration or extern, no body to generate now.

    // Restore outer scope and async state
    currentFunction = oldFunction;
    currentFunctionAST = oldFunctionAST;  // Restore AST node
    currentAsyncState = oldAsyncState;
    namedValues.swap(oldNamedValues);

    m_currentLLVMValue = func; // The "value" of a function declaration is the function itself
}

void LLVMCodegen::visit(vyb::ast::StructDeclaration* node) {
    std::string nameStr = node->name->name;

    // Check if this is a generic struct (has type parameters like Box<T>)
    if (!node->genericParams.empty()) {
        VYB_CDBG << "DEBUG: Storing generic struct template: " << nameStr << " with "
                  << node->genericParams.size() << " type parameters" << std::endl;
        // Store the AST node for later monomorphization when instantiated (e.g., Box<Int>)
        genericStructTemplates[nameStr] = node;
        m_currentLLVMValue = nullptr;
        return; // Don't generate LLVM type yet
    }

    // Non-generic struct: generate LLVM type immediately
    llvm::StructType* structType = llvm::StructType::create(*context, nameStr);

    UserTypeInfo typeInfo;
    typeInfo.llvmType = structType;
    typeInfo.isStruct = true;
    typeInfo.isReprC = node->reprC;

    // Add opaque struct to map BEFORE processing field types (for circular references)
    userTypeMap[nameStr] = typeInfo;
    VYB_CDBG << "DEBUG: Stored " << nameStr << " in userTypeMap with LLVM type pointer: " << structType << std::endl;

    std::vector<llvm::Type*> fieldTypes;
    for (size_t i = 0; i < node->fields.size(); ++i) {
        const auto& fieldDecl = node->fields[i];
        if (!fieldDecl->typeNode) { // Changed .type to ->typeNode based on ast.hpp for FieldDeclaration
            logError(fieldDecl->name->loc, "Field \'" + fieldDecl->name->name + "\' in struct \'" + nameStr + "\' is missing a type.");
            m_currentLLVMValue = nullptr; return;
        }
        VYB_CDBG << "DEBUG: Processing field '" << fieldDecl->name->name << "' with type: " << fieldDecl->typeNode->toString() << std::endl;
        llvm::Type* fieldType = codegenType(fieldDecl->typeNode.get()); // Changed .type to ->typeNode
        if (!fieldType) {
            logError(fieldDecl->name->loc, "Could not determine LLVM type for field \'" + fieldDecl->name->name + "\' in struct \'" + nameStr + "\'.");
            m_currentLLVMValue = nullptr; return;
        }
        VYB_CDBG << "DEBUG: Successfully generated LLVM type for field '" << fieldDecl->name->name << "'" << std::endl;
        fieldTypes.push_back(fieldType);
        typeInfo.fieldIndices[fieldDecl->name->name] = i; // Changed .name to ->name
    }

    structType->setBody(fieldTypes, /*isPacked=*/false);
    VYB_CDBG << "DEBUG: Set struct body for " << nameStr << " with " << fieldTypes.size() << " fields, struct is opaque: " << structType->isOpaque() << std::endl;

    // Update the map entry with complete field information
    userTypeMap[nameStr] = typeInfo;

    // Add struct to monomorphizedStructs for metadata generation
    monomorphizedStructs[nameStr] = structType;

    // Generate type metadata for JSON serialization
    generateTypeMetadata(nameStr, node);

    m_currentLLVMValue = nullptr; // structType is an llvm::Type*, not llvm::Value*
}

void LLVMCodegen::visit(vyb::ast::ClassDeclaration* node) {
    // Classes are treated like structs. Vyb uses compile-time monomorphization only.
    // NO vtables, NO virtual methods, NO inheritance. Classes are value types.
    std::string nameStr = node->name->name;
    llvm::StructType* classType = llvm::StructType::create(*context, nameStr);
    currentClassType = classType; // Set for member functions

    UserTypeInfo typeInfo;
    typeInfo.llvmType = classType;
    typeInfo.isStruct = false; // It's a class

    std::vector<llvm::Type*> fieldTypes;
    // NOTE: Vyb does not support virtual methods or vtables.
    // All dispatch is compile-time monomorphic. Classes have no implicit fields.

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
            // Method declarations are standalone functions (no vtable entries).
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

void LLVMCodegen::visit(vyb::ast::TypeAliasDeclaration* node) {
    // type<UnderlyingType> AliasName;
    // This registers the type alias so that when codegenType encounters the alias name,
    // it can resolve to the underlying LLVM type.

    if (!node->name || node->name->name.empty()) {
        logError(node->loc, "Type alias declaration missing or has empty name");
        m_currentLLVMValue = nullptr;
        return;
    }

    if (!node->typeNode) {
        logError(node->loc, "Type alias declaration missing underlying type");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Resolve the underlying type to LLVM type
    llvm::Type* underlyingLlvmType = codegenType(node->typeNode.get());
    if (!underlyingLlvmType) {
        logError(node->loc, "Could not resolve underlying type for type alias '" + node->name->name + "'");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Register the type alias mapping
    std::string aliasName = node->name->name;
    typeAliasMap[aliasName] = underlyingLlvmType;

    // Debug output to verify registration
    if (verbose) {
        logWarning(node->loc, "Registered type alias '" + aliasName + "' -> underlying LLVM type");
    }
    m_currentLLVMValue = nullptr; // No direct LLVM value for an alias declaration itself.
}

void LLVMCodegen::visit(vyb::ast::ImportDeclaration* node) {
    // Import declarations are resolved in a pre-codegen module-resolution pass.
    // During codegen, treat them as no-ops.
    (void)node;
    m_currentLLVMValue = nullptr;
}

void LLVMCodegen::visit(vyb::ast::FieldDeclaration* node) {
    // FieldDeclarations are part of StructDeclaration or ClassDeclaration.
    // They are processed within those visitors, not typically visited standalone by the main codegen loop.
    // If visited standalone, it implies context is missing (e.g. which struct/class it belongs to).
    logError(node->loc, "FieldDeclaration '" + node->name->name + "' visited standalone. Should be part of struct/class.");
    m_currentLLVMValue = nullptr;
}

void LLVMCodegen::visit(vyb::ast::BindDeclaration* node) {
    // impl blocks associate methods with types.
    // Generic impl blocks (e.g., impl<T> Display for Box<T>) are templates that don't
    // generate code until instantiated with concrete types.

    // Check if this is a generic impl block
    if (!node->genericParams.empty()) {
        VYB_CDBG << "DEBUG: Skipping generic impl block for " << node->selfType->toString()
                  << " - codegen happens on instantiation" << std::endl;
        m_currentLLVMValue = nullptr;
        return;
    }

    // For non-generic impl blocks, generate the methods for the concrete type
    // ast.hpp: TypeNodePtr selfType; // The type being implemented
    // ast.hpp: std::vector<std::unique_ptr<FunctionDeclaration>> methods;

    llvm::Type* targetType = codegenType(node->selfType.get());
    if (!targetType || !targetType->isStructTy()) {
        logError(node->loc, "Target type for impl block is not a known struct/class type: " + node->selfType->toString());
        m_currentLLVMValue = nullptr; return;
    }
    llvm::StructType* oldCurrentClassType = currentClassType;
    ast::TypeNode* oldCurrentImplTypeNode = m_currentImplTypeNode;
    std::string oldCurrentImplTraitName = m_currentImplTraitName;
    currentClassType = llvm::dyn_cast<llvm::StructType>(targetType);
    m_currentImplTypeNode = node->selfType.get();
    m_currentImplTraitName = node->traitType ? node->traitType->toString() : std::string();

    // Generate explicitly defined methods in the bind
    for (const auto& member : node->methods) {
        // Members are typically FunctionDeclarations (methods)
        member->accept(*this); // This will call visit(FunctionDeclaration*)
    }

    // Now generate default implementations for methods not explicitly defined
    if (node->traitType && driver_.hasSemanticAnalyzer()) {
        std::string aspectName = node->traitType->toString();
        std::string typeName = node->selfType->toString();

        VYB_CDBG << "DEBUG: Checking for default methods to generate for "
                  << typeName << " implementing " << aspectName << std::endl;

        SemanticAnalyzer* semantic = driver_.getSemanticAnalyzer();
        const auto& aspects = semantic->getTraitRegistry();

        auto aspectIt = aspects.find(aspectName);
        if (aspectIt != aspects.end() && aspectIt->second) {
            const TraitInfo* aspectInfo = aspectIt->second.get();

            // Get the list of methods explicitly defined in this bind
            std::set<std::string> implementedMethods;
            for (const auto& method : node->methods) {
                if (method) {
                    implementedMethods.insert(method->id->name);
                }
            }

            // Check each aspect method for default implementations
            for (const auto& traitMethod : aspectInfo->methods) {
                if (traitMethod.hasDefaultImpl &&
                    implementedMethods.find(traitMethod.name) == implementedMethods.end()) {

                    VYB_CDBG << "DEBUG: Generating default implementation for "
                              << typeName << "::" << traitMethod.name << std::endl;

                    // Generate the default method by visiting the aspect's method declaration
                    // with the current impl type set (so Self resolves to the concrete type)
                    if (traitMethod.declaration) {
                        traitMethod.declaration->accept(*this);
                    }
                }
            }
        }
    }

    currentClassType = oldCurrentClassType;
    m_currentImplTypeNode = oldCurrentImplTypeNode;
    m_currentImplTraitName = oldCurrentImplTraitName;
    m_currentLLVMValue = nullptr; // Impl block itself doesn't produce a value
}

void LLVMCodegen::visit(vyb::ast::EnumDeclaration* node) {
    if (!node->name) {
        logError(node->loc, "EnumDeclaration missing name");
        m_currentLLVMValue = nullptr;
        return;
    }

    const std::string& enumName = node->name->name;
    enumTypeNames.insert(enumName);

    // Determine whether this enum carries data variants (tagged union) or is a
    // plain C-like integer enum. Tagged unions are only built for non-generic
    // enums here; generic data enums defer to the integer fallback for now.
    bool hasData = false;
    for (const auto& v : node->variants) {
        if (v && !v->associatedTypes.empty()) { hasData = true; break; }
    }

    // Generic data enum (`enum Box<T> { Value(T), Empty }`): register the AST
    // template. The concrete tagged-union struct for each instantiation (e.g.
    // `Box_Int`) is monomorphized lazily when the type is used or constructed.
    if (hasData && !node->genericParams.empty()) {
        genericEnumTemplates[node->name->name] = node;
        m_currentLLVMValue = nullptr;
        return;
    }

    if (!hasData || !node->genericParams.empty()) {
        // C-like integer enum: each variant maps to a sequential i64 constant.
        int64_t currentValue = 0;
        for (const auto& variantNode : node->variants) {
            if (!variantNode || !variantNode->name) continue;
            const std::string qualName = enumName + "::" + variantNode->name->name;
            llvm::Constant* enumConst = llvm::ConstantInt::get(int64Type, currentValue, /*isSigned=*/true);
            enumVariantValues[qualName] = enumConst;
            currentValue++;
        }
        m_currentLLVMValue = nullptr;
        return;
    }

    // Tagged-union enum: represent as { i64 tag, [N x i8] data }, where N is the
    // largest payload (in bytes) across variants. Value semantics; C-like enums
    // above are untouched.
    TaggedEnumInfo info;
    unsigned payloadBytes = 0;
    int64_t tag = 0;
    for (const auto& variantNode : node->variants) {
        if (!variantNode || !variantNode->name) continue;
        const std::string& vname = variantNode->name->name;
        info.variantTags[vname] = static_cast<unsigned>(tag);
        if (!variantNode->associatedTypes.empty()) {
            std::vector<llvm::Type*> fieldTypes;
            for (const auto& t : variantNode->associatedTypes) {
                llvm::Type* ft = codegenType(t.get());
                if (!ft) {
                    logError(variantNode->loc, "Could not resolve payload type for variant " + vname);
                    ft = llvm::Type::getInt64Ty(*context);
                }
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
    llvm::Type* dataArrayTy = llvm::ArrayType::get(llvm::Type::getInt8Ty(*context), payloadBytes);
    llvm::StructType* enumStruct = llvm::StructType::get(*context, {llvm::Type::getInt64Ty(*context), dataArrayTy}, false);
    info.llvmType = enumStruct;

    // Register the type so codegenType('Shape') resolves and variables can hold it.
    UserTypeInfo typeInfo;
    typeInfo.llvmType = enumStruct;
    typeInfo.isStruct = true;
    userTypeMap[enumName] = typeInfo;
    taggedEnumInfo[enumName] = info;

    VYB_CDBG << "DEBUG: Registered tagged union enum " << enumName << " as " << getTypeName(enumStruct)
             << " with max payload " << payloadBytes << " bytes" << std::endl;
    m_currentLLVMValue = nullptr;
}

void LLVMCodegen::visit(vyb::ast::EnumVariant* node) {
    // Visited as part of EnumDeclaration. Not typically standalone.
    logError(node->loc, "EnumVariant visited standalone.");
    m_currentLLVMValue = nullptr;
}

void LLVMCodegen::visit(vyb::ast::GenericParameter* node) {
    // Generic parameters are resolved during template instantiation (monomorphization).
    // Standalone codegen for a GenericParameter is not typical.
    logError(node->loc, "GenericParameter visited standalone. Should be resolved during template instantiation.");
    m_currentLLVMValue = nullptr;
}

void LLVMCodegen::visit(vyb::ast::TemplateDeclaration* node) {
    // Template declarations are blueprints. Code is generated when they are instantiated.
    // No LLVM IR is generated for template declarations themselves - only for instantiations.
    // Silently skip template declarations during codegen.
    VYB_CDBG << "DEBUG: Skipping TemplateDeclaration '"
              << (node && node->name ? node->name->name : "<unnamed>")
              << "' - codegen happens on instantiation" << std::endl;
    m_currentLLVMValue = nullptr;
}

void LLVMCodegen::createFunctionForwardDeclaration(vyb::ast::FunctionDeclaration* node) {
    VYB_CDBG << "DEBUG: Creating forward declaration for function: " << node->id->name << std::endl;

    // Skip generic functions - they are monomorphized on call, not forward-declared
    if (!node->genericParams.empty()) {
        VYB_CDBG << "DEBUG: Skipping forward declaration for generic function: " << node->id->name << std::endl;
        return;
    }

    // Check if function already exists
    llvm::Function* existingFunc = module->getFunction(node->id->name);
    if (existingFunc) {
        VYB_CDBG << "DEBUG: Function " << node->id->name << " already exists, skipping forward declaration" << std::endl;
        return;
    }

    // Extract parameter types
    std::vector<llvm::Type*> paramTypes;
    for (const auto& paramNode : node->params) {
        if (!paramNode.typeNode) {
            logError(paramNode.name->loc, "Parameter '" + paramNode.name->name + "' in function '" + node->id->name + "' is missing a type annotation.");
            return;
        }
        llvm::Type* llvmType = codegenType(paramNode.typeNode.get());
        if (!llvmType) {
            logError(paramNode.name->loc, "Could not determine LLVM type for parameter '" + paramNode.name->name + "' in function '" + node->id->name + "'.");
            return;
        }
        paramTypes.push_back(llvmType);
    }

    // Extract return type
    llvm::Type* returnType = nullptr;
    llvm::Type* originalReturnType = nullptr;

    if (node->returnTypeNode) {
        if (currentAsyncState.isAsync) {
            // For async functions, the actual return type is wrapped in Future<T>
            originalReturnType = codegenType(node->returnTypeNode.get());
            if (!originalReturnType) {
                logError(node->loc, "Could not determine LLVM return type for async function '" + node->id->name + "'.");
                return;
            }
            returnType = createFutureStructType(originalReturnType);
        } else {
            originalReturnType = codegenType(node->returnTypeNode.get());
            if (!originalReturnType) {
                logError(node->loc, "Could not determine LLVM return type for function '" + node->id->name + "'.");
                return;
            }

            // Phase 2: Wrap return type in {T, ptr} for failable functions
            if (node->needsErrorReturn) {
                VYB_CDBG << "DEBUG: Forward decl - Wrapping return type in {T, ptr} for failable function '"
                          << node->id->name << "'" << std::endl;
                llvm::Type* errorPtrType = llvm::PointerType::get(*context, 0);
                if (originalReturnType->isVoidTy()) {
                    returnType = llvm::StructType::get(*context, {llvm::Type::getInt1Ty(*context), errorPtrType});
                } else {
                    returnType = llvm::StructType::get(*context, {originalReturnType, errorPtrType});
                }
            } else {
                returnType = originalReturnType;
            }
        }
    } else {
        if (currentAsyncState.isAsync) {
            // Async void function returns Future<void>
            originalReturnType = llvm::Type::getVoidTy(*context);
            returnType = createFutureStructType(originalReturnType);
        } else {
            originalReturnType = llvm::Type::getVoidTy(*context);

            // Phase 2 ABI choice:
            // Keep one uniform failable ABI shape for codegen paths: {payload, error_ptr}.
            // For Void payloads we use i1 as a dummy field, giving {i1, i8*}.
            if (node->needsErrorReturn) {
                VYB_CDBG << "DEBUG: Forward decl - Wrapping void return in {i1, ptr} for failable function '"
                          << node->id->name << "'" << std::endl;
                llvm::Type* errorPtrType = llvm::PointerType::get(*context, 0);
                returnType = llvm::StructType::get(*context, {llvm::Type::getInt1Ty(*context), errorPtrType});
            } else {
                returnType = originalReturnType;
            }
        }
    }

    // Create function type and forward declaration
    // Apply the same auto-serialization rule as in visit(FunctionDeclaration):
    // main() with any non-Void, non-String return → use void (auto-serialization).
    if (node->id->name == "main" && !node->needsErrorReturn) {
        bool isVoidReturn  = returnType->isVoidTy();
        bool isStringRet   = isVybStringStructType(returnType);
        if (!isVoidReturn && !isStringRet) {
            returnType = voidType;
        }
    }
    llvm::FunctionType* funcType = llvm::FunctionType::get(returnType, paramTypes, false /*isVarArg*/);
    llvm::Function* func = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, node->id->name, module.get());

    VYB_CDBG << "DEBUG: Successfully created forward declaration for function: " << node->id->name << std::endl;
}
