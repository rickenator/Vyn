// SPDX-License-Identifier: Apache-2.0

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

using namespace vyb;
// using namespace llvm; // Uncomment if desired for brevity

// Forward declaration: helper used by the routing branch before its definition.
enum class AsyncResultKind { None, Int, String, Void, Float, Bool };
static AsyncResultKind asyncFutureResultKind(vyb::ast::FunctionDeclaration* node);
static AsyncResultKind asyncResultKindFromTypeNode(vyb::ast::TypeNode* inner);
static bool asyncParamIsVec(const vyb::ast::TypeNode* tn);
static const vyb::ast::TypeNode* asyncParamVecElement(const vyb::ast::TypeNode* tn);

// --- Declarations ---

// A module-level global can be statically (compile-time) initialized when its
// initializer is a literal (or a parenthesised/negated literal); a reference to
// another global or a computed expression must be initialized at runtime.
static bool isStaticLiteralInit(const ast::Expression* e) {
    if (!e) return false;
    if (dynamic_cast<const ast::IntegerLiteral*>(e) ||
        dynamic_cast<const ast::FloatLiteral*>(e) ||
        dynamic_cast<const ast::StringLiteral*>(e) ||
        dynamic_cast<const ast::BooleanLiteral*>(e) ||
        dynamic_cast<const ast::NilLiteral*>(e)) {
        return true;
    }
    if (dynamic_cast<const ast::UnaryExpression*>(e)) {
        return true;  // e.g. `-5`; LLVM folds it to a constant
    }
    return false;
}

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

    // A module-level global whose initializer is not a literal (it references
    // another global, or computes a value) cannot be a compile-time constant, so
    // defer its value to a runtime store in __vyb_module_init. Locals and
    // literal-initialized globals keep the normal codegen path below.
    const bool isModuleGlobal = (currentFunction == nullptr);
    const bool globalNeedsRuntime =
        isModuleGlobal && node->init && !isStaticLiteralInit(node->init.get());

    if (node->init && !globalNeedsRuntime) {
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
            // The lambda value is now a closure struct; lastLambdaFuncType holds
            // its user-facing signature (without the hidden environment param).
            if (lastLambdaFuncType) {
                localLambdaTypes[node->id->name] = lastLambdaFuncType;
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
    } else if (!node->init) { // No initializer
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
        llvm::Constant* staticInit = nullptr;
        if (globalNeedsRuntime) {
            if (!varType) {
                logError(node->loc, "Global variable '" + node->id->name +
                                    "' initializer depends on a runtime value; it needs an explicit type.");
                m_currentLLVMValue = nullptr;
                return;
            }
            // Zero-initialize now; the real value is stored during __vyb_module_init.
            staticInit = llvm::Constant::getNullValue(varType);
        } else {
            if (!initialVal || !llvm::isa<llvm::Constant>(initialVal)) {
                logError(node->init ? node->init->loc : node->loc,
                         "Global variable '" + node->id->name + "' initializer must be a constant.");
                m_currentLLVMValue = nullptr;
                return;
            }
            staticInit = llvm::cast<llvm::Constant>(initialVal);
        }
        if (!varType) {
            logError(node->init ? node->init->loc : node->loc,
                     "Global variable '" + node->id->name + "' has no resolvable LLVM type.");
            m_currentLLVMValue = nullptr;
            return;
        }
        auto* globalVar = new llvm::GlobalVariable(
            *module,
            varType,
            node->isConst && !globalNeedsRuntime, // runtime-initialized globals must be writable memory
            llvm::GlobalValue::PrivateLinkage, // Or ExternalLinkage if exported
            staticInit,
            node->id->name
        );
        namedValues[node->id->name] = globalVar;
        globalValues_[node->id->name] = globalVar;
        if (globalNeedsRuntime) {
            pendingGlobalInits_.push_back(std::make_pair(globalVar, node->init.get()));
        }
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
        // A Vec whose initializer is a borrow (a field/member extract such as
        // `self.keys`) shares the source's data buffer. Give the new binding its
        // own deep copy so both the source and this binding own independent data
        // and each can be reclaimed on scope exit without a double-free. Fresh
        // `Vec(...)` constructions (transfers) are left as-is.
        if (initialVal && isVecStructType(varType) && isVecStructType(initialVal->getType()) &&
            node->init &&
            (dynamic_cast<ast::MemberExpression*>(node->init.get()) != nullptr ||
             dynamic_cast<ast::Identifier*>(node->init.get()) != nullptr)) {
            const vyb::ast::TypeNode* elemNode = nullptr;
            if (auto* vt = dynamic_cast<ast::VecType*>(node->typeNode.get())) {
                elemNode = vt->elementType.get();
            } else if (auto* nn = dynamic_cast<ast::TypeName*>(node->typeNode.get())) {
                if (!nn->genericArgs.empty()) elemNode = nn->genericArgs[0].get();
            }
            if (elemNode) {
                if (llvm::Type* elemT = codegenType(const_cast<vyb::ast::TypeNode*>(elemNode))) {
                    initialVal = generateVecDeepCopy(initialVal, elemT,
                        llvm::cast<llvm::StructType>(varType));
                }
            }
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

        // Closure-typed variables own one reference to a capture environment.
        // Only a confirmed `fn` type is treated as a closure: a bare `{ptr, ptr}`
        // layout (e.g. a 2-pointer tuple) must not be reference-counted.
        bool astIsFn = isFnTypeNode(node->typeNode.get())
            || (node->init != nullptr && isFnTypeNode(node->init->type.get()))
            || dynamic_cast<ast::FunctionExpression*>(node->init.get()) != nullptr;
        bool closureVar = isClosureStructType(varType) && astIsFn;
        if (closureVar) {
            needsCleanup = true;
            VYB_CDBG << "DEBUG: Variable '" << node->id->name << "' is a closure - needs cleanup" << std::endl;
        }

        // An owning (`my`) String binding holds one reference to a heap buffer.
        // Borrowed (`their`) strings must not be reference counted. Globals are
        // not in a scope so they never run cleanup; declaring one is fine here
        // but irrelevant (no scope-based release is ever emitted for them).
        bool stringVar = isVybStringStructType(varType) && ownership == ast::OwnershipKind::MY;
        if (stringVar) {
            needsCleanup = true;
            VYB_CDBG << "DEBUG: Variable '" << node->id->name << "' is a String - needs cleanup" << std::endl;
        }

        // An owning (`my`) struct may hold owned fields (Vec data buffers,
        // String references, or a nested owning struct). Mark it for scope-exit
        // reclaim so those fields are freed / released when the binding drops.
        if (!needsCleanup && ownership == ast::OwnershipKind::MY && node->typeNode &&
            structTypeHasOwnedFields(node->typeNode.get())) {
            needsCleanup = true;
            VYB_CDBG << "DEBUG: Variable '" << node->id->name
                      << "' is a struct with owned fields - needs cleanup" << std::endl;
        }

        // A data-carrying enum (Result<..., our<T>>) owns a
        // strong count on its payload's control block; register it so the count
        // is released on scope exit.
        bool enumOurVar = ownership == ast::OwnershipKind::MY && node->typeNode &&
                          enumPayloadHoldsOurRef(node->typeNode.get());
        if (enumOurVar) {
            needsCleanup = true;
            VYB_CDBG << "DEBUG: Variable '" << node->id->name
                      << "' is an enum with an our payload - needs cleanup" << std::endl;
        }

        // A `my<Struct>` binding initialized from another `my<Struct>` binding is
        // a true ownership move. At runtime the source slot still holds the heap
        // pointer after the init has stored it, so without invalidation both
        // bindings would reclaim the same allocation on scope exit (double free).
        // Transfer the pointer by nulling the source slot when the source is a
        // local owner. When the source is a borrowed `my` (a `my` parameter, which
        // the caller still owns), the pointer cannot be taken: the new binding
        // deep-copies the payload so it owns data independent of the caller's
        // (mirroring how owned struct fields are copied), and stays a real owner
        // so a later overwrite only frees its own copy.
        if (node->init && node->typeNode && isMyOwnedStructTypeNode(node->typeNode.get())) {
            if (auto* initIdent = dynamic_cast<ast::Identifier*>(node->init.get())) {
                const ScopeVariable* srcVar = nullptr;
                for (auto sit = scopeStack.rbegin(); sit != scopeStack.rend() && !srcVar; ++sit) {
                    for (const auto& sv : *sit) {
                        if (sv.name == initIdent->name) { srcVar = &sv; break; }
                    }
                }
                if (srcVar && srcVar->ownership == ast::OwnershipKind::MY &&
                    srcVar->allocaInst != alloca) {
                    if (srcVar->needsCleanup) {
                        // Local owner: null the source so ownership transfers cleanly.
                        llvm::PointerType* rawPtr = llvm::PointerType::get(*context, 0);
                        builder->CreateStore(llvm::ConstantPointerNull::get(rawPtr),
                                             srcVar->allocaInst, "move.null_src");
                        VYB_CDBG << "DEBUG: my<Struct> move '" << initIdent->name
                                  << "' -> '" << node->id->name << "': nulled source slot" << std::endl;
                    } else {
                        // Borrowed `my` param: deep-copy into the new binding.
                        const vyb::ast::TypeNode* pointeeAst = myPointeeOf(node->typeNode.get());
                        if (llvm::Type* pointeeT = pointeeAst
                                ? codegenType(const_cast<vyb::ast::TypeNode*>(pointeeAst)) : nullptr) {
                            if (auto* poise = llvm::dyn_cast<llvm::StructType>(pointeeT)) {
                                initialVal = deepCopyMyStruct(initialVal, pointeeAst, poise);
                                // The store of the (borrowed) source pointer already
                                // happened above; overwrite it so the binding owns the
                                // independent copy.
                                builder->CreateStore(initialVal, alloca, "init.borrowcopy");
                                VYB_CDBG << "DEBUG: my<Struct> init '" << node->id->name
                                          << "' from borrow param '" << initIdent->name
                                          << "': deep-copied payload" << std::endl;
                            }
                        }
                    }
                }
            }
        }

        // Register variable for scope-based cleanup
        registerVariable(node->id->name, alloca, initialVal, ownership, varType, needsCleanup);

        // Hand-off of a newly stowed closure value into a durable storage
        // location. A direct call that returns a closure already retained the
        // env at its return, so the caller takes that reference over as-is; any
        // other source (a fresh inline lambda, or a copy of another variable)
        // needs the new storage location to retain (+1) so environment teardown
        // stays in lock-step with the locations that reference it.
        if (closureVar) {
            bool transferred = dynamic_cast<ast::CallExpression*>(node->init.get()) != nullptr;
            if (!transferred) {
                retainClosureValue(initialVal);
                VYB_CDBG << "DEBUG: Closure variable '" << node->id->name << "' retained a shared env" << std::endl;
            } else {
                VYB_CDBG << "DEBUG: Closure variable '" << node->id->name << "' took over a returned env" << std::endl;
            }
        }

        // A String binding takes over the producing expression's single owned
        // reference when the initial value is a freshly-created transfer (concat,
        // to_string, or a String-returning call); any other borrowing source is
        // shared, so the binding retains (+1) its own reference here. Scope exit
        // (cleanupVariable) releases it.
        if (stringVar) {
            if (!exprIsStringTransfer(node->init.get())) {
                retainStringValue(initialVal);
                VYB_CDBG << "DEBUG: String variable '" << node->id->name << "' retained a shared buffer" << std::endl;
            }
        }

        // An `our<T>` binding (control-block backed, i.e. non-primitive `T`)
        // holds one strong reference and releases it on scope exit. The binding
        // takes over a freshly-created strong ref from a transfer producer
        // (`our(...)`, `grab()`, or a function returning `our<T>`); any other
        // source (a copy of an existing `our` binding/field) is shared, so the
        // new location retains (+1) so both releases stay in lock-step.
        bool ourVar = ownership == ast::OwnershipKind::OUR && varType &&
                      varType->isPointerTy() && node->typeNode &&
                      isOurRefType(node->typeNode.get());
        if (ourVar) {
            if (!exprIsOurTransfer(node->init.get())) {
                retainOurControlBlock(initialVal, node->id->name);
                VYB_CDBG << "DEBUG: our<T> variable '" << node->id->name << "' retained a shared control block" << std::endl;
            } else {
                VYB_CDBG << "DEBUG: our<T> variable '" << node->id->name << "' took over a fresh control block" << std::endl;
            }
        }

        // A `mild<T>` binding (weak reference) takes over a fresh weak ref from
        // `soft(...)`; a copy of an existing `mild` value (or a struct field) is
        // shared, so the new location must retain (+weak) so the release on scope
        // exit stays balanced with its own ref.
        bool mildVar = ownership == ast::OwnershipKind::MILD && varType &&
                       varType->isPointerTy() && node->typeNode &&
                       isMildRefType(node->typeNode.get());
        if (mildVar) {
            if (!exprIsMildTransfer(node->init.get())) {
                retainMildControlBlock(initialVal, node->id->name);
                VYB_CDBG << "DEBUG: mild<T> variable '" << node->id->name
                          << "' retained a shared weak reference" << std::endl;
            } else {
                VYB_CDBG << "DEBUG: mild<T> variable '" << node->id->name
                          << "' took over a fresh weak reference" << std::endl;
            }
        }

        // An enum binding with an `our<T>` payload owns a strong count that scope
        // exit will release. A fresh transfer (grab(), a function returning the
        // enum, or Ok(our(...))) already supplied that ref; a borrowed copy
        // (Ok(owner)) must retain so the later release stays balanced.
        if (enumOurVar && !enumInitIsOurTransfer(node->init.get())) {
            reclaimEnumOurPayload(alloca, node->typeNode.get(), /*retain=*/true);
            VYB_CDBG << "DEBUG: enum-with-our variable '" << node->id->name
                      << "' retained a shared payload control block" << std::endl;
        }

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

    // Stage-1..5 real async: an `async fn(params...)<Future<T>>` (T = Int, Float,
    // Bool, String, or Void) whose args are all primitive scalars or `String`s
    // runs its body as a task on the multi-threaded executor (worker + env-capture
    // + launcher split). Everything else keeps the legacy eager path. Top-level
    // non-failable declarations only.
    if (node->isAsync && !m_currentImplTypeNode &&
        asyncFutureResultKind(node) != AsyncResultKind::None) {
        bool paramsEnvSafe = true;
        for (const auto& p : node->params) {
            if (!p.typeNode) { paramsEnvSafe = false; break; }
            llvm::Type* pt = codegenType(p.typeNode.get());
            if (!pt || !(pt->isIntegerTy() || pt->isFloatTy() || pt->isDoubleTy() ||
                         isVybStringStructType(pt) || asyncParamIsVec(p.typeNode.get()) ||
                         (isFnTypeNode(p.typeNode.get()) && isClosureStructType(pt)) ||
                         (pt->isPointerTy() && isOurRefType(p.typeNode.get())) ||
                         isKnownStructTypeNode(p.typeNode.get()))) {
                paramsEnvSafe = false; break;
            }
        }
        if (paramsEnvSafe) {
            codegenAsyncTask(node);
            return;
        }
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

    llvm::FunctionType* funcType = llvm::FunctionType::get(returnType, paramTypes, node->variadic /*isVarArg*/);

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

        // Record the scope-stack depth before this function adds its own scopes,
        // so a `return` can clean up to exactly this baseline (never the caller's).
        m_functionScopeBaseline = scopeStack.size();
        // Snapshot the scope stack so the function's own scopes (parameters and
        // locals) can be discarded verbatim after its body is generated. A
        // lazily-generated function (e.g. a monomorphized aspect/bind method
        // emitted in the middle of its caller's body) otherwise leaves its
        // parameter scopes on the shared stack, and the surrounding caller's
        // return-path cleanup then releases allocas belonging to other functions
        // - an LLVM dominance violation. The mono/trait and lambda paths already
        // discard their scopes this way; the plain FunctionDeclaration path must
        // match so String/closure/Vec parameter refcounts never cross functions.
        auto savedFunctionScopeStack = scopeStack;
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

            // Closure-typed parameters take a reference to the shared capture env:
            // retain it here (balanced by the callee releasing it on scope exit),
            // so the callee's closure lifetime is independent of the argument.
            bool closureParam = isClosureStructType(paramTypes[i]) &&
                isFnTypeNode(node->params[i].typeNode.get());
            if (closureParam) {
                retainClosureValue(argVal);
                VYB_CDBG << "DEBUG: Parameter '" << paramNames[i] << "' is a closure - retained env" << std::endl;
            }

            // A String parameter takes a reference to the (possibly shared) buffer
            // passed by the caller, balanced by the callee releasing it on scope
            // exit, so the callee's use is independent of the argument's lifetime.
            bool stringParam = isVybStringStructType(paramTypes[i]);
            if (stringParam) {
                retainStringValue(argVal);
                VYB_CDBG << "DEBUG: Parameter '" << paramNames[i] << "' is a String - retained buffer" << std::endl;
            }

            // An `our<T>` parameter (control-block backed, non-primitive `T`)
            // takes a shared strong reference to the passed control block,
            // balanced by the callee releasing it on scope exit (owned by the
            // OUR cleanup branch). This makes the callee's ref independent of the
            // argument and lets a returned `our` param transfer that ref out.
            bool ourParam = paramTypes[i] && paramTypes[i]->isPointerTy() &&
                node->params[i].typeNode && isOurRefType(node->params[i].typeNode.get());
            if (ourParam) {
                retainOurControlBlock(argVal, paramNames[i]);
                VYB_CDBG << "DEBUG: Parameter '" << paramNames[i] << "' is our<T> - retained control block" << std::endl;
            }

            // Register parameter for scope-based cleanup.
            // Vec/closure/String/our parameters own a reference to release.
            // Plain value parameters do not own heap data and are cleaned up by the caller.
            ast::OwnershipKind paramOwnership = ast::OwnershipKind::MY;
            if (ourParam) paramOwnership = ast::OwnershipKind::OUR;
            registerVariable(paramNames[i], alloca, argVal, paramOwnership, paramTypes[i],
                             vecParam || closureParam || stringParam || ourParam);

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

        // Clean up the function scope (i.e. the parameter scope) before an
        // implicit/fall-through return. If the function already returned via an
        // explicit `return`, the ReturnStatement's exitToFunctionBaseline() has
        // already emitted cleanup for every live scope, and the current block is
        // terminated - inserting another cleanup here would corrupt the IR.
        if (!func->empty() && !func->back().getTerminator()) {
            exitScope();
        }

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

        // Discard any scopes this function's body left behind (parameters and
        // locals that an explicit-`return` path already emitted cleanup for but
        // whose bookkeeping was restored by exitToFunctionBaseline). Restoring
        // the enclosing caller's scope stack wholesale - exactly like the mono
        // and trait paths - prevents one function's parameter scopes from being
        // re-cleaned inside a surrounding function's body.
        scopeStack = std::move(savedFunctionScopeStack);
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
        registerStructConstructors(node);
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

    // Cache the AST template (no generic params) so owned-field cleanup can
    // resolve concrete field types for scope-exit reclaim / deep-copy.
    genericStructTemplates[nameStr] = node;

    // Generate type metadata for JSON serialization
    generateTypeMetadata(nameStr, node);

    registerStructConstructors(node);
    m_currentLLVMValue = nullptr; // structType is an llvm::Type*, not llvm::Value*
}

void LLVMCodegen::registerStructConstructors(vyb::ast::StructDeclaration* node) {
    const std::string& structName = node->name->name;
    for (unsigned ci = 0; ci < node->constructors.size(); ++ci) {
        ast::FunctionDeclaration* ctor = node->constructors[ci].get();
        if (!ctor) continue;
        std::string ctorName = "__ctor_" + structName + "_" + std::to_string(ci);
        // Ensure the name is consistent regardless of how the parser named it.
        if (ctor->id) ctor->id->name = ctorName;
        genericFunctionTemplates[ctorName] = ctor;
        structConstructors[structName].push_back({ (unsigned)ctor->params.size(), ctorName });
    }
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
    if (!targetType) {
        logError(node->loc, "Could not resolve bind target type: " + node->selfType->toString());
        m_currentLLVMValue = nullptr; return;
    }
    // A bind target may be a user struct, a built-in struct (String/Bytes/Vec), or a
    // primitive scalar (Int/Float/Bool/Char/sized ints). Scalar targets carry no struct
    // type to associate, so currentClassType is left null and Self resolves via
    // m_currentImplTypeNode (which is set to the target type node below).
    llvm::StructType* structTarget = llvm::dyn_cast<llvm::StructType>(targetType);
    bool isScalarTarget = targetType->isIntegerTy() || targetType->isFloatingPointTy();
    if (!structTarget && !isScalarTarget) {
        logError(node->loc, "Target type for bind is not a known struct/class or primitive scalar type: " + node->selfType->toString());
        m_currentLLVMValue = nullptr; return;
    }
    llvm::StructType* oldCurrentClassType = currentClassType;
    ast::TypeNode* oldCurrentImplTypeNode = m_currentImplTypeNode;
    std::string oldCurrentImplTraitName = m_currentImplTraitName;
    currentClassType = structTarget;
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
        if (!hasData && node->genericParams.empty()) {
            // Constant enum (`enum Sock { AF_INET = 2, ... }`): each variant is a
            // compile-time Int constant (scoped under the enum name), not a nominal
            // enum value, so it can flow straight into `Int` parameters.
            bool anyValue = false, allValue = true;
            for (const auto& v : node->variants) {
                if (!v) continue;
                if (v->hasValue) anyValue = true; else allValue = false;
            }
            if (anyValue) {
                if (!allValue) { m_currentLLVMValue = nullptr; return; } // rejected in semantic
                for (const auto& variantNode : node->variants) {
                    if (!variantNode || !variantNode->name) continue;
                    enumVariantValues[enumName + "::" + variantNode->name->name] =
                        llvm::ConstantInt::get(int64Type, static_cast<int64_t>(variantNode->value), /*isSigned=*/true);
                }
                m_currentLLVMValue = nullptr;
                return;
            }

            // C-like enum → first-class typed value backed by a single i64 tag
            // (isScalar). This keeps `Enum` values distinct nominal types while
            // dropping them into the same register/ABI slot a C integer-backed enum
            // occupies, so an extern function taking the enum by value works across
            // the FFI boundary (a `{ i64, [N x i8] }` struct would not). The variant
            // constants double as the raw positional tag for explicit FFI access.
            TaggedEnumInfo info;
            unsigned currentValue = 0;
            for (const auto& variantNode : node->variants) {
                if (!variantNode || !variantNode->name) continue;
                info.variantTags[variantNode->name->name] = currentValue;
                enumVariantValues[enumName + "::" + variantNode->name->name] =
                    llvm::ConstantInt::get(int64Type, static_cast<int64_t>(currentValue), true);
                currentValue++;
            }
            info.isScalar = true;
            info.llvmType = nullptr; // Scalars don't use a struct; codegenType maps the name to i64.
            taggedEnumInfo[enumName] = info;

            VYB_CDBG << "DEBUG: Registered C-like enum " << enumName << " as a typed scalar (i64 tag, "
                     << info.variantTags.size() << " variants)" << std::endl;
            m_currentLLVMValue = nullptr;
            return;
        }

        // Generic C-like enum (rare): fall back to sequential i64 constants.
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

static AsyncResultKind asyncFutureResultKind(vyb::ast::FunctionDeclaration* node) {
    if (!node || !node->isAsync || !node->returnTypeNode) return AsyncResultKind::None;
    vyb::ast::TypeNode* inner = nullptr;
    if (auto* ft = dynamic_cast<vyb::ast::FutureType*>(node->returnTypeNode.get())) {
        inner = ft->resultType.get();
    } else if (auto* tn = dynamic_cast<vyb::ast::TypeName*>(node->returnTypeNode.get())) {
        if (tn->identifier && tn->identifier->name == "Future" && tn->genericArgs.size() == 1)
            inner = tn->genericArgs[0].get();
    }
    if (!inner) return AsyncResultKind::None;
    auto name = [](vyb::ast::TypeNode* t) -> std::string {
        if (auto* tn = dynamic_cast<vyb::ast::TypeName*>(t))
            if (tn->identifier) return tn->identifier->name;
        return "";
    };
    const std::string n = name(inner);
    if (n == "Int") return AsyncResultKind::Int;
    if (n == "String") return AsyncResultKind::String;
    if (n == "Void") return AsyncResultKind::Void;
    if (n == "Float") return AsyncResultKind::Float;
    if (n == "Bool") return AsyncResultKind::Bool;
    return AsyncResultKind::None;
}

static std::vector<vyb::ast::FunctionParameter>
cloneParams(const std::vector<vyb::ast::FunctionParameter>& src) {
    std::vector<vyb::ast::FunctionParameter> out;
    for (const auto& p : src) {
        std::unique_ptr<vyb::ast::Identifier> name;
        if (p.name) name = std::make_unique<vyb::ast::Identifier>(p.name->loc, p.name->name);
        vyb::ast::TypeNodePtr tn = p.typeNode ? p.typeNode->clone() : nullptr;
        out.emplace_back(std::move(name), std::move(tn), p.isMutable);
    }
    return out;
}

// True when the AST type names a Vec<T> (either the `Vec<...>` node form or a
// `TypeName` whose base identifier is `Vec`).
static bool asyncParamIsVec(const vyb::ast::TypeNode* tn) {
    if (!tn) return false;
    if (dynamic_cast<const vyb::ast::VecType*>(tn)) return true;
    if (auto* nn = dynamic_cast<const vyb::ast::TypeName*>(tn))
        return nn->identifier && nn->identifier->name == "Vec";
    return false;
}

// The `T` inside a `Vec<T>` parameter's type node, or null.
static const vyb::ast::TypeNode* asyncParamVecElement(const vyb::ast::TypeNode* tn) {
    if (!tn) return nullptr;
    if (auto* vt = dynamic_cast<const vyb::ast::VecType*>(tn)) return vt->elementType.get();
    if (auto* nn = dynamic_cast<const vyb::ast::TypeName*>(tn))
        if (!nn->genericArgs.empty()) return nn->genericArgs[0].get();
    return nullptr;
}

void LLVMCodegen::codegenAsyncTask(vyb::ast::FunctionDeclaration* node) {
    const std::string base = node->id->name;
    const std::string workerName = base + "$__async_body";
    const std::string entryName = base + "$__async_entry";
    const AsyncResultKind kind = asyncFutureResultKind(node);
    const std::string& resultName =
        (kind == AsyncResultKind::String) ? "String"
        : (kind == AsyncResultKind::Void) ? "Void"
        : (kind == AsyncResultKind::Float) ? "Float"
        : (kind == AsyncResultKind::Bool) ? "Bool" : "Int";

    // 1) Worker: a plain `fn(params...) -> <Result>` that runs the original body
    //    through the normal codegen path (parameter scope, trap/epilogue handling,
    //    and the correct result-return/ownership semantics for the result type).
    std::vector<vyb::ast::FunctionParameter> workerParams = cloneParams(node->params);
    auto workerRet = std::make_unique<vyb::ast::TypeName>(node->loc,
        std::make_unique<vyb::ast::Identifier>(node->loc, resultName));
    std::unique_ptr<vyb::ast::FunctionDeclaration> workerNode = std::make_unique<vyb::ast::FunctionDeclaration>(
        node->loc,
        std::make_unique<vyb::ast::Identifier>(node->loc, workerName),
        std::move(workerParams),
        std::move(node->body),
        /*isAsync=*/false,
        std::move(workerRet));
    workerNode->canFail = node->canFail;
    workerNode->needsErrorReturn = node->needsErrorReturn;
    visit(workerNode.get());

    llvm::Function* worker = module->getFunction(workerName);
    if (!worker) {
        logError(node->loc, "internal error: failed to generate async worker '" + workerName + "'");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Param LLVM types/names shared by the env struct, entry, and launcher.
    std::vector<llvm::Type*> paramTypes;
    std::vector<std::string> paramNames;
    for (const auto& p : node->params) {
        if (!p.typeNode) { logError(node->loc, "async param missing type"); m_currentLLVMValue = nullptr; return; }
        llvm::Type* pt = codegenType(p.typeNode.get());
        if (!pt) { logError(p.name->loc, "could not type async param '" + p.name->name + "'"); m_currentLLVMValue = nullptr; return; }
        paramTypes.push_back(pt);
        paramNames.push_back(p.name ? p.name->name : "");
    }
    const size_t n = paramTypes.size();
    llvm::Value* nullPtr = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(int8PtrType));

    // Owned param fields snapshotted into the env (String / Vec<T>). A String is
    // stored inline with the buffer retained (+1); a Vec is deep-copied so the env
    // owns an independent copy. The env's per-layout dtor releases each one on
    // task cleanup. `vecElemType` (indexed by param) feeds the deep copy.
    std::vector<AsyncEnvField> ownedFields;
    std::vector<bool> vecParam(n, false);
    std::vector<llvm::Type*> vecElemType(n, nullptr);
    std::vector<const vyb::ast::TypeNode*> structParamAst(n, nullptr);
    for (size_t i = 0; i < n; ++i) {
        const vyb::ast::TypeNode* ptn = node->params[i].typeNode.get();
        if (paramTypes[i] && isVybStringStructType(paramTypes[i])) {
            AsyncEnvField f; f.fieldIx = i + 2; f.isString = true; f.isVec = false; f.vecIsString = false; f.isOur = false;
            ownedFields.push_back(f);
        } else if (ptn && paramTypes[i] && paramTypes[i]->isPointerTy() && isOurRefType(ptn)) {
            AsyncEnvField f; f.fieldIx = i + 2; f.isString = false; f.isVec = false; f.vecIsString = false; f.isOur = true;
            ownedFields.push_back(f);
        } else if (ptn && paramTypes[i] && isFnTypeNode(ptn) && isClosureStructType(paramTypes[i])) {
            // Closure param: the env owns its own reference to the closure's
            // capture environment (+1 on snapshot, released by the env dtor on
            // task cleanup), so the closure outlives the caller's scope.
            AsyncEnvField f; f.fieldIx = i + 2; f.isString = false; f.isVec = false; f.vecIsString = false; f.isOur = false;
            f.isClosure = true;
            ownedFields.push_back(f);
        } else if (ptn && asyncParamIsVec(ptn)) {
            AsyncEnvField f; f.fieldIx = i + 2; f.isString = false; f.isVec = true; f.isOur = false;
            f.vecIsString = isVecOfStringTypeNode(ptn);
            ownedFields.push_back(f);
            vecParam[i] = true;
            if (const vyb::ast::TypeNode* en = asyncParamVecElement(ptn))
                vecElemType[i] = codegenType(const_cast<vyb::ast::TypeNode*>(en));
        } else if (ptn && isKnownStructTypeNode(ptn)) {
            // Inline struct param: the launcher deep-copies it into an
            // independent owned snapshot; the env dtor reclaims its owned fields.
            AsyncEnvField f; f.fieldIx = i + 2; f.isString = false; f.isVec = false; f.vecIsString = false; f.isOur = false;
            f.isStruct = true; f.structType = ptn;
            ownedFields.push_back(f);
            structParamAst[i] = ptn;
        }
    }

    // 2) Async-worker environment: a heap block `{ i64 refcount; ptr cap_dtor;
    //    param0; param1; ... }` snapshotted by the launcher and passed to the
    //    event-loop runtime (which retains it and reclaims it on task cleanup).
    std::vector<llvm::Type*> envFields;
    envFields.push_back(int64Type);   // index 0: refcount
    envFields.push_back(int8PtrType); // index 1: cap_dtor (null for scalars)
    for (auto* pt : paramTypes) envFields.push_back(pt);
    llvm::StructType* envTy = llvm::StructType::create(*context, envFields, "async.env." + base);

    // 3) Entry trampoline: `i64(i8*)` — the event loop invokes it as `fn -> Int`
    //    with the env pointer. It unpacks the snapshot params, runs the worker,
    //    and encodes the result into the int64 slot the runtime returns:
    //      Int    -> the value itself
    //      String -> a pointer to a heap slot holding the {ptr,len} String
    //      Void   -> 0
    //    Failable tasks additionally receive the task-id, so a worker failure
    //    (an error from its `{T, i8*}` return) is recorded on the task for the
    //    awaiter to pick up via __vyb_async_take_error.
    const bool failable = node->canFail;
    // The async entry ABI is uniform: i64(i8* env, i64 task_id). The task handle
    // is the same value the cooperative trampoline owns, so a failable worker's
    // failure (an error from its `{T, i8*}` return) can be recorded on the task
    // for the awaiter to retrieve via __vyb_async_take_error.
    llvm::FunctionType* entryTy = llvm::FunctionType::get(int64Type, {int8PtrType, int64Type}, false);
    if (llvm::Function* old = module->getFunction(entryName)) old->eraseFromParent();
    llvm::Function* entry = llvm::Function::Create(entryTy, llvm::Function::InternalLinkage, entryName, module.get());
    {
        llvm::BasicBlock* ebb = llvm::BasicBlock::Create(*context, "entry", entry);
        llvm::IRBuilder<> b(ebb);
        llvm::Value* envCast = b.CreateBitCast(entry->getArg(0), envTy->getPointerTo(), "async.env.cast");
        llvm::Value* taskIdArg = entry->getArg(1);
        std::vector<llvm::Value*> args;
        for (size_t i = 0; i < n; ++i) {
            llvm::Value* fp = b.CreateStructGEP(envTy, envCast, i + 2, "async.env.p" + std::to_string(i));
            args.push_back(b.CreateLoad(paramTypes[i], fp, "async.env.v" + std::to_string(i)));
        }

        // Failable worker returns `{ T, i8* }`: split into success / error. On an
        // error the entry records the failure on the task (the Future stays ready;
        // the awaiter checks take_error before dereferencing the payload) and
        // returns 0 so no bogus payload is fabricated. On success it encodes the
        // payload exactly like the non-failable entry below.
        llvm::Function* setErrFn = module->getFunction("__vyb_async_set_error");
        if (!setErrFn) {
            llvm::FunctionType* ft = llvm::FunctionType::get(int64Type, {int64Type, int8PtrType}, false);
            setErrFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage,
                                              "__vyb_async_set_error", module.get());
        }
        llvm::Value* errPtr = nullptr;
        llvm::BasicBlock* ebOk = nullptr;
        llvm::BasicBlock* ebErr = nullptr;
        if (failable) {
            ebOk = llvm::BasicBlock::Create(*context, "entry.err_ok", entry);
            ebErr = llvm::BasicBlock::Create(*context, "entry.err_fail", entry);
        }

        llvm::Value* wr = nullptr;
        if (failable) {
            wr = b.CreateCall(worker, args, "async.worker");
            errPtr = b.CreateBitCast(b.CreateExtractValue(wr, 1, "async.err.raw"), int8PtrType, "async.err");
            b.CreateCondBr(b.CreateIsNull(errPtr), ebOk, ebErr);
            b.SetInsertPoint(ebOk);
        }

        if (kind == AsyncResultKind::Void) {
            if (!failable) b.CreateCall(worker, args, "async.worker");
            b.CreateRet(llvm::ConstantInt::get(int64Type, 0));
        } else {
            llvm::Value* val = failable ? b.CreateExtractValue(wr, 0, "async.worker.val")
                                        : b.CreateCall(worker, args, "async.worker");
            if (kind == AsyncResultKind::Int) {
                b.CreateRet(val);
            } else if (kind == AsyncResultKind::Bool) {
                b.CreateRet(b.CreateZExt(val, int64Type, "async.worker.zext"));
            } else if (kind == AsyncResultKind::Float) {
                b.CreateRet(b.CreateBitCast(val, int64Type, "async.worker.bitcast"));
            } else { // String
                llvm::DataLayout dl(module.get());
                llvm::Value* slotBytes = llvm::ConstantInt::get(int64Type, dl.getTypeAllocSize(val->getType()));
                llvm::Value* raw = b.CreateCall(getOrCreateMallocFunction(), {slotBytes}, "async.reslslot");
                llvm::Value* slot = b.CreateBitCast(raw, val->getType()->getPointerTo(), "async.resslot.ptr");
                b.CreateStore(val, slot);
                b.CreateRet(b.CreatePtrToInt(slot, int64Type, "async.resslot.i64"));
            }
        }

        if (failable) {
            b.SetInsertPoint(ebErr);
            b.CreateCall(setErrFn, {taskIdArg, errPtr}, "async.err.set");
            b.CreateRet(llvm::ConstantInt::get(int64Type, 0));
        }
    }

    // 4) Launcher: `fn(params...) -> Future<T>` ({T*, i32 state, i64 task,
    //    i8* runtime_data}). It builds the env, spawns the task, and hands back a
    //    Future whose `task_id` field lets `await` drive (main) / suspend (fiber).
    //    Note the Future is keyed on the *inner* result type (`String`, `Int`, ...),
    //    not the failable `{T, i8*}` worker ABI, so it matches the `Future<T>`
    //    type annotations and the await codegen's layout lookup. Failable tasks
    //    flag readiness in the state field (1) so `await` calls take_error.
    llvm::Type* resultSlotTy = failable
        ? llvm::cast<llvm::StructType>(worker->getReturnType())->getElementType(0)
        : worker->getReturnType();
    llvm::StructType* futureTy = createFutureStructType(resultSlotTy);
    llvm::FunctionType* launcherTy = llvm::FunctionType::get(futureTy, paramTypes, false);
    llvm::Function* launcher = module->getFunction(base);
    if (launcher) {
        if (launcher->getFunctionType() != launcherTy) { launcher->eraseFromParent(); launcher = nullptr; }
    }
    if (!launcher) {
        launcher = llvm::Function::Create(launcherTy, llvm::Function::ExternalLinkage, base, module.get());
    }
    launcher->addFnAttr(llvm::Attribute::NoInline);
    unsigned ai = 0;
    for (auto& arg : launcher->args()) {
        if (ai < paramNames.size()) arg.setName(paramNames[ai]);
        ++ai;
    }

    llvm::BasicBlock* lbb = llvm::BasicBlock::Create(*context, "entry", launcher);
    llvm::IRBuilder<> b(lbb);

    llvm::Function* mallocFn = getOrCreateMallocFunction();
    llvm::Function* releaseFn = getOrCreateClosureReleaseFunction();
    llvm::DataLayout dl(module.get());
    llvm::Value* envBytes = llvm::ConstantInt::get(int64Type, dl.getTypeAllocSize(envTy));
    llvm::Value* rawEnv = b.CreateCall(mallocFn, {envBytes}, "async.env.alloc");
    llvm::Value* envPtr = b.CreateBitCast(rawEnv, envTy->getPointerTo(), "async.env.ptr");

    llvm::Value* rcPtr = b.CreateStructGEP(envTy, envPtr, 0, "async.env.rc");
    b.CreateStore(llvm::ConstantInt::get(int64Type, 1), rcPtr);
    llvm::Value* dtorPtr = b.CreateStructGEP(envTy, envPtr, 1, "async.env.dtor");
    if (!ownedFields.empty()) {
        llvm::Function* envDtor = generateAsyncEnvDtor(envTy, base, ownedFields);
        b.CreateStore(envDtor
                          ? llvm::ConstantExpr::getBitCast(envDtor, int8PtrType)
                          : static_cast<llvm::Value*>(nullPtr),
                      dtorPtr);
    } else {
        b.CreateStore(nullPtr, dtorPtr);
    }
    // Point the member builder + currentFunction at the launcher so the shared
    // retain/deep-copy helpers (which emit through them, and create blocks in the
    // current function) go into this block, then restore.
    llvm::Function* savedCurrentFunc = currentFunction;
    currentFunction = launcher;
    std::unique_ptr<llvm::IRBuilder<>> savedBuilder = std::move(builder);
    builder = std::make_unique<llvm::IRBuilder<>>(lbb);
    for (size_t i = 0; i < n; ++i) {
        llvm::Value* fp = builder->CreateStructGEP(envTy, envPtr, i + 2, "async.env.sp" + std::to_string(i));
        llvm::Value* av = launcher->getArg(i);
        if (paramTypes[i] && isVybStringStructType(paramTypes[i])) {
            llvm::Value* data = builder->CreateExtractValue(av, 0, "async.env.str.data");
            builder->CreateCall(getOrCreateVybStringRetainFunction(), {data}, "async.env.str.retain");
        } else if (paramTypes[i] && paramTypes[i]->isPointerTy() &&
                   node->params[i].typeNode && isOurRefType(node->params[i].typeNode.get())) {
            retainOurControlBlock(av, "async.env.our");
        } else if (paramTypes[i] && node->params[i].typeNode &&
                   isFnTypeNode(node->params[i].typeNode.get()) && isClosureStructType(paramTypes[i])) {
            // Closure param: the env holds its own reference to the closure's
            // capture environment so it stays alive while the task runs.
            retainClosureValue(av);
        } else if (vecParam[i] && vecElemType[i] && paramTypes[i]) {
            llvm::Value* copy = generateVecDeepCopy(av, vecElemType[i], paramTypes[i]);
            av = copy ? copy : av;
        } else if (structParamAst[i] && paramTypes[i]) {
            if (auto* sot = llvm::dyn_cast<llvm::StructType>(paramTypes[i])) {
                llvm::Value* copy = generateStructDeepCopy(av, structParamAst[i], sot);
                av = copy ? copy : av;
            }
        }
        builder->CreateStore(av, fp);
    }
    // The deep copy may have advanced the insert point into a clone block; snap
    // the local builder to wherever the member builder ended before restoring.
    llvm::BasicBlock* contBlock = builder->GetInsertBlock();
    builder = std::move(savedBuilder);
    currentFunction = savedCurrentFunc;
    b.SetInsertPoint(contBlock);

    llvm::Function* spawn = module->getFunction("__vyb_async_spawn");
    if (!spawn) {
        llvm::FunctionType* spawnTy = llvm::FunctionType::get(int64Type, {int8PtrType, int8PtrType}, false);
        spawn = llvm::Function::Create(spawnTy, llvm::Function::ExternalLinkage, "__vyb_async_spawn", module.get());
    }
    llvm::Value* taskId = b.CreateCall(spawn,
        {b.CreateBitCast(envPtr, int8PtrType), b.CreateBitCast(entry, int8PtrType)}, "async.task");
    b.CreateCall(releaseFn, {b.CreateBitCast(envPtr, int8PtrType)}); // drop launcher ref

    llvm::Value* nullResult = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(futureTy->getElementType(0)));
    llvm::Value* fut = llvm::UndefValue::get(futureTy);
    fut = b.CreateInsertValue(fut, nullResult, {0});
    fut = b.CreateInsertValue(fut, llvm::ConstantInt::get(int32Type, failable ? 1 : 0), {1});
    fut = b.CreateInsertValue(fut, taskId, {2});
    fut = b.CreateInsertValue(fut, nullPtr, {3});
    b.CreateRet(fut);

    m_currentLLVMValue = launcher;
}

// Async lambda: `async |x| -> await process(x)`. The lambda body is compiled as
// a normal closure (the reused worker, so captures and retained params compose).
// The outer closure value `{ env, fn }` is a thin launcher whose `env` owns the
// inner closure and whose `fn`, when called with the user params, snapshots them
// into a task env, spawns the cooperative task, and returns a Future<T> for the
// caller to `await`. This mirrors the worker + env snapshot + entry + launcher
// split that real async functions use.
void LLVMCodegen::codegenAsyncLambda(ast::FunctionExpression* node) {
    static unsigned sAsyncLambdaCounter = 0;
    const unsigned tag = ++sAsyncLambdaCounter;
    const std::string funcName =
        "lambda_" + std::to_string(reinterpret_cast<uintptr_t>(node)) + "_a" + std::to_string(tag);
    const std::string entryName = funcName + "$__async_entry";
    const std::string userEnvName = funcName + "$__userenv";
    const std::string taskEnvName = funcName + "$__taskenv";

    llvm::Value* nullPtr = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(int8PtrType));

    // Param LLVM types; async lambdas require explicit param types (like async fn).
    std::vector<llvm::Type*> paramTypes;
    for (const auto& p : node->params) {
        if (!p.typeNode) {
            logError(p.name ? p.name->loc : node->loc,
                     "async lambda parameters require an explicit type annotation");
            m_currentLLVMValue = nullptr; return;
        }
        llvm::Type* pt = codegenType(p.typeNode.get());
        if (!pt) {
            logError(p.name ? p.name->loc : node->loc, "could not type async lambda parameter");
            m_currentLLVMValue = nullptr; return;
        }
        paramTypes.push_back(pt);
    }
    const size_t n = paramTypes.size();

    // Inner (non-Future) result type from the semantic FunctionType. After
    // semantic wrapping the lambda's public type is fn(...) -> Future<T>.
    ast::FunctionType* ft = dynamic_cast<ast::FunctionType*>(node->type.get());
    if (!ft || !ft->returnType) {
        logError(node->loc, "async lambda missing inferred function type");
        m_currentLLVMValue = nullptr; return;
    }
    ast::TypeNode* outerRet = ft->returnType.get();
    const ast::TypeNode* innerRet = nullptr;
    if (auto* tn = dynamic_cast<ast::TypeName*>(outerRet))
        if (tn->identifier && tn->identifier->name == "Future" && tn->genericArgs.size() == 1)
            innerRet = tn->genericArgs[0].get();
    if (!innerRet) {
        logError(node->loc, "async lambda return type must be Future<T>");
        m_currentLLVMValue = nullptr; return;
    }
    llvm::Type* innerRetTy = codegenType(const_cast<ast::TypeNode*>(innerRet));
    if (!innerRetTy) {
        logError(node->loc, "could not type async lambda result");
        m_currentLLVMValue = nullptr; return;
    }
    const AsyncResultKind kind =
        asyncResultKindFromTypeNode(const_cast<ast::TypeNode*>(innerRet));
    const std::string resultName =
        (kind == AsyncResultKind::String) ? "String"
        : (kind == AsyncResultKind::Void) ? "Void"
        : (kind == AsyncResultKind::Float) ? "Float"
        : (kind == AsyncResultKind::Bool) ? "Bool" : "Int";

    // 1) Compile the body as a normal closure (reused worker). Temporarily set
    //    the inferred FunctionType return type to the inner T so the closure
    //    function returns T (the worker result), then restore the Future form.
    llvm::StructType* closureTy = getClosureStructType();
    llvm::Value* innerClosure = nullptr;
    {
        std::unique_ptr<ast::TypeNode> savedReturn = std::move(ft->returnType);
        ft->returnType = std::unique_ptr<ast::TypeNode>(innerRet->clone());
        bool savedAsync = node->isAsync;
        node->isAsync = false;
        node->accept(*this); // inner closure value lands in m_currentLLVMValue
        node->isAsync = savedAsync;
        innerClosure = m_currentLLVMValue;
        ft->returnType = std::move(savedReturn);
    }
    if (!innerClosure || !isClosureStructType(innerClosure->getType())) {
        logError(node->loc, "internal error: failed to compile async lambda body");
        m_currentLLVMValue = nullptr; return;
    }
    // Inner worker signature: T(i8* capenv, params...).
    std::vector<llvm::Type*> workerParamTys;
    workerParamTys.push_back(llvm::PointerType::get(*context, 0));
    for (auto* pt : paramTypes) workerParamTys.push_back(pt);
    llvm::FunctionType* workerTy = llvm::FunctionType::get(innerRetTy, workerParamTys, false);

    // Owned fields snapshotted into the task env: the inner closure + params.
    std::vector<AsyncEnvField> ownedFields;
    std::vector<bool> vecParam(n, false);
    std::vector<llvm::Type*> vecElemType(n, nullptr);
    std::vector<const ast::TypeNode*> structParamAst(n, nullptr);
    AsyncEnvField innerField; innerField.fieldIx = 2; innerField.isClosure = true;
    innerField.isString = false; innerField.isVec = false; innerField.isOur = false;
    ownedFields.push_back(innerField);
    for (size_t i = 0; i < n; ++i) {
        const ast::TypeNode* ptn = node->params[i].typeNode.get();
        if (paramTypes[i] && isVybStringStructType(paramTypes[i])) {
            AsyncEnvField f; f.fieldIx = i + 3; f.isString = true; f.isVec = false; f.vecIsString = false; f.isOur = false;
            ownedFields.push_back(f);
        } else if (ptn && paramTypes[i] && paramTypes[i]->isPointerTy() && isOurRefType(ptn)) {
            AsyncEnvField f; f.fieldIx = i + 3; f.isString = false; f.isVec = false; f.vecIsString = false; f.isOur = true;
            ownedFields.push_back(f);
        } else if (ptn && paramTypes[i] && isFnTypeNode(ptn) && isClosureStructType(paramTypes[i])) {
            AsyncEnvField f; f.fieldIx = i + 3; f.isString = false; f.isVec = false; f.vecIsString = false; f.isOur = false;
            f.isClosure = true; ownedFields.push_back(f);
        } else if (ptn && asyncParamIsVec(ptn)) {
            AsyncEnvField f; f.fieldIx = i + 3; f.isString = false; f.isVec = true; f.isOur = false;
            f.vecIsString = isVecOfStringTypeNode(ptn);
            ownedFields.push_back(f);
            vecParam[i] = true;
            if (const ast::TypeNode* en = asyncParamVecElement(ptn))
                vecElemType[i] = codegenType(const_cast<ast::TypeNode*>(en));
        } else if (ptn && isKnownStructTypeNode(ptn)) {
            AsyncEnvField f; f.fieldIx = i + 3; f.isString = false; f.isVec = false; f.vecIsString = false; f.isOur = false;
            f.isStruct = true; f.structType = ptn;
            ownedFields.push_back(f);
            structParamAst[i] = ptn;
        }
    }

    // Task env: { i64 rc, ptr dtor, closureTy innerClosure, params... }.
    std::vector<llvm::Type*> taskFields;
    taskFields.push_back(int64Type);
    taskFields.push_back(int8PtrType);
    taskFields.push_back(closureTy);
    for (auto* pt : paramTypes) taskFields.push_back(pt);
    llvm::StructType* taskEnvTy = llvm::StructType::create(*context, taskFields, taskEnvName);
    llvm::Function* taskDtor = generateAsyncEnvDtor(taskEnvTy, funcName + "_task", ownedFields);

    // 2) Entry trampoline: i64(i8*, i64) unpacks the task env, calls the worker
    //    closure, and encodes the result into the i64 slot the runtime returns.
    //    The second (task-id) argument is a uniform part of the async entry ABI
    //    and is unused by non-failable lambdas.
    llvm::FunctionType* entryTy = llvm::FunctionType::get(int64Type, {int8PtrType, int64Type}, false);
    if (llvm::Function* old = module->getFunction(entryName)) old->eraseFromParent();
    llvm::Function* entry = llvm::Function::Create(entryTy, llvm::Function::InternalLinkage, entryName, module.get());
    {
        llvm::BasicBlock* ebb = llvm::BasicBlock::Create(*context, "entry", entry);
        llvm::IRBuilder<> b(ebb);
        llvm::Value* envCast = b.CreateBitCast(entry->getArg(0), taskEnvTy->getPointerTo(), "al.env.cast");
        llvm::Value* cl = b.CreateLoad(closureTy, b.CreateStructGEP(taskEnvTy, envCast, 2, "al.env.clr"), "al.env.closure");
        std::vector<llvm::Value*> args;
        args.push_back(b.CreateExtractValue(cl, 0, "al.env.capenv"));
        for (size_t i = 0; i < n; ++i) {
            llvm::Value* fp = b.CreateStructGEP(taskEnvTy, envCast, i + 3, "al.env.p" + std::to_string(i));
            args.push_back(b.CreateLoad(paramTypes[i], fp, "al.env.v" + std::to_string(i)));
        }
        llvm::Value* fnPtr = b.CreateBitCast(b.CreateExtractValue(cl, 1, "al.env.fn"),
                                             workerTy->getPointerTo(), "al.env.fptr");
        if (kind == AsyncResultKind::Void) {
            b.CreateCall(workerTy, fnPtr, args);
            b.CreateRet(llvm::ConstantInt::get(int64Type, 0));
        } else if (kind == AsyncResultKind::Int) {
            b.CreateRet(b.CreateCall(workerTy, fnPtr, args, "al.worker"));
        } else if (kind == AsyncResultKind::Bool) {
            llvm::Value* bv = b.CreateCall(workerTy, fnPtr, args, "al.worker");
            b.CreateRet(b.CreateZExt(bv, int64Type, "al.worker.zext"));
        } else if (kind == AsyncResultKind::Float) {
            llvm::Value* fv = b.CreateCall(workerTy, fnPtr, args, "al.worker");
            b.CreateRet(b.CreateBitCast(fv, int64Type, "al.worker.bitcast"));
        } else { // String
            llvm::Value* sv = b.CreateCall(workerTy, fnPtr, args, "al.worker");
            llvm::DataLayout dl(module.get());
            llvm::Value* slotBytes = llvm::ConstantInt::get(int64Type, dl.getTypeAllocSize(innerRetTy));
            llvm::Value* raw = b.CreateCall(getOrCreateMallocFunction(), {slotBytes}, "al.resslot");
            llvm::Value* slot = b.CreateBitCast(raw, innerRetTy->getPointerTo(), "al.resslot.ptr");
            b.CreateStore(sv, slot);
            b.CreateRet(b.CreatePtrToInt(slot, int64Type, "al.resslot.i64"));
        }
    }

    // 3) Launcher closure: Future<T>(i8* userenv, params...). Builds the task
    //    env (snapshotting the inner closure + params), spawns the task, and
    //    returns a Future whose task_id field lets `await` drive/suspend.
    llvm::StructType* futureTy = createFutureStructType(innerRetTy);
    llvm::StructType* userEnvTy = llvm::StructType::create(*context, {int64Type, int8PtrType, closureTy}, userEnvName);
    std::vector<llvm::Type*> launcherParamTys;
    launcherParamTys.push_back(llvm::PointerType::get(*context, 0));
    for (auto* pt : paramTypes) launcherParamTys.push_back(pt);
    llvm::FunctionType* launcherTy = llvm::FunctionType::get(futureTy, launcherParamTys, false);
    llvm::Function* launcher = llvm::Function::Create(
        launcherTy, llvm::Function::InternalLinkage, funcName + "$__launch", module.get());
    {
        llvm::BasicBlock* lbb = llvm::BasicBlock::Create(*context, "entry", launcher);
        llvm::IRBuilder<> b(lbb);
        generatePushFrameCall(funcName, node->loc);

        llvm::Function* mallocFn = getOrCreateMallocFunction();
        llvm::Function* releaseFn = getOrCreateClosureReleaseFunction();
        llvm::DataLayout dl(module.get());

        llvm::Value* ueCast = b.CreateBitCast(launcher->getArg(0), userEnvTy->getPointerTo(), "al.userenv.cast");
        llvm::Value* innerClosureForTask = b.CreateLoad(closureTy,
            b.CreateStructGEP(userEnvTy, ueCast, 2, "al.userenv.clr"), "al.userenv.closure");

        llvm::Value* taskBytes = llvm::ConstantInt::get(int64Type, dl.getTypeAllocSize(taskEnvTy));
        llvm::Value* rawTask = b.CreateCall(mallocFn, {taskBytes}, "al.task.alloc");
        llvm::Value* taskEnvPtr = b.CreateBitCast(rawTask, taskEnvTy->getPointerTo(), "al.task.ptr");
        b.CreateStore(llvm::ConstantInt::get(int64Type, 1),
            b.CreateStructGEP(taskEnvTy, taskEnvPtr, 0, "al.task.rc"));
        b.CreateStore(taskDtor ? llvm::ConstantExpr::getBitCast(taskDtor, int8PtrType)
                               : static_cast<llvm::Value*>(nullPtr),
            b.CreateStructGEP(taskEnvTy, taskEnvPtr, 1, "al.task.dtor"));
        b.CreateStore(innerClosureForTask,
            b.CreateStructGEP(taskEnvTy, taskEnvPtr, 2, "al.task.clr"));

        // Point the member builder at the launcher so the shared retain/deep-copy
        // helpers emit into this block, then restore.
        llvm::Function* savedFunc = currentFunction;
        currentFunction = launcher;
        std::unique_ptr<llvm::IRBuilder<>> savedBuilder = std::move(builder);
        builder = std::make_unique<llvm::IRBuilder<>>(lbb);
        retainClosureValue(innerClosureForTask); // task's own reference
        for (size_t i = 0; i < n; ++i) {
            llvm::Value* fp = builder->CreateStructGEP(taskEnvTy, taskEnvPtr, i + 3, "al.task.sp" + std::to_string(i));
            llvm::Value* av = launcher->getArg(i + 1);
            if (paramTypes[i] && isVybStringStructType(paramTypes[i])) {
                llvm::Value* data = builder->CreateExtractValue(av, 0, "al.task.str.data");
                builder->CreateCall(getOrCreateVybStringRetainFunction(), {data}, "al.task.str.retain");
            } else if (paramTypes[i] && paramTypes[i]->isPointerTy() &&
                       node->params[i].typeNode && isOurRefType(node->params[i].typeNode.get())) {
                retainOurControlBlock(av, "al.task.our");
            } else if (paramTypes[i] && node->params[i].typeNode &&
                       isFnTypeNode(node->params[i].typeNode.get()) && isClosureStructType(paramTypes[i])) {
                retainClosureValue(av);
            } else if (vecParam[i] && vecElemType[i] && paramTypes[i]) {
                llvm::Value* copy = generateVecDeepCopy(av, vecElemType[i], paramTypes[i]);
                av = copy ? copy : av;
            } else if (structParamAst[i] && paramTypes[i]) {
                if (auto* sot = llvm::dyn_cast<llvm::StructType>(paramTypes[i])) {
                    llvm::Value* copy = generateStructDeepCopy(av, structParamAst[i], sot);
                    av = copy ? copy : av;
                }
            }
            builder->CreateStore(av, fp);
        }
        llvm::BasicBlock* contBlock = builder->GetInsertBlock();
        builder = std::move(savedBuilder);
        currentFunction = savedFunc;
        b.SetInsertPoint(contBlock);

        llvm::Function* spawn = module->getFunction("__vyb_async_spawn");
        if (!spawn) {
            llvm::FunctionType* spawnTy = llvm::FunctionType::get(int64Type, {int8PtrType, int8PtrType}, false);
            spawn = llvm::Function::Create(spawnTy, llvm::Function::ExternalLinkage, "__vyb_async_spawn", module.get());
        }
        llvm::Value* taskId = b.CreateCall(spawn,
            {b.CreateBitCast(taskEnvPtr, int8PtrType),
             b.CreateBitCast(entry, int8PtrType)}, "al.task.id");
        b.CreateCall(releaseFn, {b.CreateBitCast(taskEnvPtr, int8PtrType)}); // drop launcher ref

        llvm::Value* nullResult = llvm::ConstantPointerNull::get(
            llvm::cast<llvm::PointerType>(futureTy->getElementType(0)));
        llvm::Value* fut = llvm::UndefValue::get(futureTy);
        fut = b.CreateInsertValue(fut, nullResult, {0});
        fut = b.CreateInsertValue(fut, llvm::ConstantInt::get(int32Type, 0), {1});
        fut = b.CreateInsertValue(fut, taskId, {2});
        fut = b.CreateInsertValue(fut, nullPtr, {3});
        generatePopFrameCall();
        b.CreateRet(fut);
    }

    // 4) The user-facing closure value stores the inner closure in its env so the
    //    launcher can rebuild a task from it on each call; null-free base. The
    //    user env's dtor drops the inner closure when the last reference is gone.
    llvm::Value* rawUser = builder->CreateCall(getOrCreateMallocFunction(),
        {llvm::ConstantInt::get(int64Type, llvm::DataLayout(module.get()).getTypeAllocSize(userEnvTy))}, "al.userenv.alloc");
    llvm::Value* userEnvPtr = builder->CreateBitCast(rawUser, userEnvTy->getPointerTo(), "al.userenv.ptr");
    // Env refcount starts at 0: each durable closure holder (the variable that
    // owns this async-lambda value) retains on store and releases on scope exit.
    builder->CreateStore(llvm::ConstantInt::get(int64Type, 0),
        builder->CreateStructGEP(userEnvTy, userEnvPtr, 0, "al.userenv.rc"));
    std::vector<AsyncEnvField> userFields;
    AsyncEnvField uf; uf.fieldIx = 2; uf.isClosure = true; uf.isString = false; uf.isVec = false; uf.isOur = false;
    userFields.push_back(uf);
    llvm::Function* userDtor = generateAsyncEnvDtor(userEnvTy, funcName + "_user", userFields);
    builder->CreateStore(userDtor ? llvm::ConstantExpr::getBitCast(userDtor, int8PtrType)
                                  : static_cast<llvm::Value*>(nullPtr),
        builder->CreateStructGEP(userEnvTy, userEnvPtr, 1, "al.userenv.dtor"));
    // The user env is one durable holder of the inner closure: retain it (+1) so
    // it outlives every task spawned from this lambda; the user env's dtor
    // releases it when the last reference is dropped.
    retainClosureValue(innerClosure);
    builder->CreateStore(innerClosure,
        builder->CreateStructGEP(userEnvTy, userEnvPtr, 2, "al.userenv.close"));
    llvm::Value* closureVal = llvm::UndefValue::get(closureTy);
    closureVal = builder->CreateInsertValue(closureVal, userEnvPtr, 0, "al.env");
    closureVal = builder->CreateInsertValue(closureVal, launcher, 1, "al.fn");
    m_currentLLVMValue = closureVal;
    lastLambdaFuncType = llvm::FunctionType::get(futureTy, paramTypes, false);
}

// Resolve the result kind for an async lambda's inner type (the T in Future<T>).
static AsyncResultKind asyncResultKindFromTypeNode(vyb::ast::TypeNode* inner) {
    auto name = [](vyb::ast::TypeNode* t) -> std::string {
        if (auto* tn = dynamic_cast<vyb::ast::TypeName*>(t))
            if (tn->identifier) return tn->identifier->name;
        return t ? t->toString() : "";
    };
    const std::string n = name(inner);
    if (n == "Int") return AsyncResultKind::Int;
    if (n == "String") return AsyncResultKind::String;
    if (n == "Void") return AsyncResultKind::Void;
    if (n == "Float") return AsyncResultKind::Float;
    if (n == "Bool") return AsyncResultKind::Bool;
    return AsyncResultKind::None;
}
