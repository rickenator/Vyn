// =============================================================================
// DESIGN: Trait monomorphization (compile-time specialization)
// Vyb uses compile-time monomorphization for traits - NOT runtime polymorphism.
// When a trait method is called on a concrete type, a specialized version of the
// method is generated at compile time. This is similar to Rust's monomorphic
// dispatch or C++ template instantiation.
//
// NO vtables, NO dynamic dispatch, NO trait objects.
// All trait resolution happens at compile time via type substitution.
// =============================================================================

#include "vyb/vre/llvm/codegen.hpp"
#include "vyb/semantic.hpp"
#include <sstream>
#include <algorithm>

namespace vyb {

// Parse a type string like "Box<Int>" into {base: "Box", args: ["Int"]}
LLVMCodegen::TypePattern LLVMCodegen::TypePattern::parse(const std::string& typeStr) {
    TypePattern pattern;

    size_t anglePos = typeStr.find('<');
    if (anglePos == std::string::npos) {
        // Simple type without generic args
        pattern.base = typeStr;
        return pattern;
    }

    // Extract base type
    pattern.base = typeStr.substr(0, anglePos);

    // Extract generic arguments
    size_t start = anglePos + 1;
    size_t end = typeStr.find_last_of('>');
    if (end == std::string::npos || end <= start) {
        // Malformed, return what we have
        return pattern;
    }

    std::string argsStr = typeStr.substr(start, end - start);

    // Split by comma (simple split, doesn't handle nested generics yet)
    std::stringstream ss(argsStr);
    std::string arg;
    while (std::getline(ss, arg, ',')) {
        // Trim whitespace
        arg.erase(0, arg.find_first_not_of(" \t"));
        arg.erase(arg.find_last_not_of(" \t") + 1);
        if (!arg.empty()) {
            pattern.args.push_back(arg);
        }
    }

    return pattern;
}

// Check if a concrete type matches this pattern, extracting type substitutions
// Example: pattern="Box<T>", concrete="Box<Int>" -> true, subst={"T":"Int"}
bool LLVMCodegen::TypePattern::matchesPattern(const TypePattern& concrete,
                                              std::map<std::string, std::string>& substitutions) const {
    // Base types must match exactly
    if (this->base != concrete.base) {
        return false;
    }

    // Number of type arguments must match
    if (this->args.size() != concrete.args.size()) {
        return false;
    }

    // Match each argument
    for (size_t i = 0; i < this->args.size(); ++i) {
        const std::string& patternArg = this->args[i];
        const std::string& concreteArg = concrete.args[i];

        // Check if pattern arg is a type parameter (single uppercase letter or capitalized identifier)
        // Simple heuristic: if it's a single char or starts with capital and no '<', it's a type param
        bool isTypeParam = (patternArg.length() == 1 && std::isupper(patternArg[0])) ||
                          (patternArg.find('<') == std::string::npos && std::isupper(patternArg[0]));

        if (isTypeParam) {
            // This is a type parameter - record the substitution
            auto it = substitutions.find(patternArg);
            if (it != substitutions.end()) {
                // Already have a substitution for this param - must be consistent
                if (it->second != concreteArg) {
                    return false;
                }
            } else {
                substitutions[patternArg] = concreteArg;
            }
        } else {
            // Concrete type - must match exactly
            if (patternArg != concreteArg) {
                return false;
            }
        }
    }

    return true;
}

// Convert TypePattern to mangled name: Box<Int> -> Box_Int
std::string LLVMCodegen::TypePattern::toMangled() const {
    if (args.empty()) {
        return base;
    }

    std::string result = base;
    for (const auto& arg : args) {
        result += "_" + arg;
    }
    return result;
}

vyb::ast::TypeNodePtr LLVMCodegen::typePatternToTypeNode(const TypePattern& pattern,
                                                   const SourceLocation& loc) {
    std::vector<vyb::ast::TypeNodePtr> args;
    for (const auto& arg : pattern.args) {
        TypePattern argPattern = TypePattern::parse(arg);
        args.push_back(typePatternToTypeNode(argPattern, loc));
    }

    return std::make_unique<vyb::ast::TypeName>(
        loc,
        std::make_unique<vyb::ast::Identifier>(loc, pattern.base),
        std::move(args)
    );
}

// Extract base pattern from concrete type: "Box<Int>" -> "Box"
std::string LLVMCodegen::extractBasePattern(const std::string& concreteType) {
    TypePattern parsed = TypePattern::parse(concreteType);
    return parsed.base;
}

// Get full type name from an expression (e.g., variable reference)
std::string LLVMCodegen::getFullTypeName(vyb::ast::Expression* expr) {
    if (!expr) return "";

    // Try to get type from the expression itself
    if (expr->type) {
        return expr->type->toString();
    }

    // For identifiers, check if we have type info
    if (auto ident = dynamic_cast<ast::Identifier*>(expr)) {
        if (ident->type) {
            return ident->type->toString();
        }
    }

    return "";
}

// Monomorphize a trait method for a concrete type
llvm::Function* LLVMCodegen::monomorphizeTraitMethod(const std::string& concreteType,
                                                     const std::string& traitName,
                                                     const std::string& methodName) {
    // Check cache first
    std::string cacheKey = concreteType + "::" + traitName + "::" + methodName;
    auto cacheIt = monomorphizedMethods.find(cacheKey);
    if (cacheIt != monomorphizedMethods.end()) {
        return cacheIt->second;
    }

    // Get semantic analyzer from driver
    if (!driver_.hasSemanticAnalyzer()) {
        logError(SourceLocation(), "SemanticAnalyzer not available for trait monomorphization");
        return nullptr;
    }

    SemanticAnalyzer* semantic = driver_.getSemanticAnalyzer();

    // Parse the concrete type to extract pattern matching info
    TypePattern concretePattern = TypePattern::parse(concreteType);



    // Search through generic trait impls to find a matching pattern
    const auto& genericImpls = semantic->getGenericTraitImpls();
    for (const auto& typeEntry : genericImpls) {
        const std::string& pattern = typeEntry.first;
        TypePattern templatePattern = TypePattern::parse(pattern);



        // Try to match the pattern
        std::map<std::string, std::string> typeSubstitutions;
        if (templatePattern.matchesPattern(concretePattern, typeSubstitutions)) {


            // Check if this pattern has an impl for the requested trait
            const auto& traitMap = typeEntry.second;
            auto traitIt = traitMap.find(traitName);
            if (traitIt != traitMap.end()) {
                const GenericImplInfo* implInfo = traitIt->second.get();

                // Check if the method exists in this impl
                auto methodIt = implInfo->methods.find(methodName);
                if (methodIt != implInfo->methods.end()) {
                    ast::FunctionDeclaration* methodAST = methodIt->second;



                    // Clone the method AST for modification
                    // Since we don't have a deep clone method for FunctionDeclaration,
                    // we'll work with the original and generate specialized code directly

                    // Build specialized function name: TypeName_TraitName_MethodName
                    // (e.g., "Vec_Int_Container_size"). Including the trait name
                    // disambiguates types that bind multiple aspects which
                    // declare the same method name.
                    std::string specializedName = concretePattern.toMangled() +
                        "_" + traitName + "_" + methodName;


                    // Establish the concrete impl context before resolving the
                    // signature so Self::Item / <Trait>::Item references in
                    // parameter and return types resolve against the specialized
                    // type and its associated-type bindings rather than the
                    // caller currently being generated.
                    auto currentImplConcreteTypeNode = typePatternToTypeNode(concretePattern, methodAST->loc);
                    ast::TypeNode* origImplTypeNode = m_currentImplTypeNode;
                    std::string origImplTraitName = m_currentImplTraitName;
                    m_currentImplTypeNode = currentImplConcreteTypeNode.get();
                    m_currentImplTraitName = traitName;

                    // Get the method's signature
                    std::vector<llvm::Type*> paramTypes;

                    // First parameter is always Self (the object type). When the
                    // receiver is declared by reference (an ownership-qualified
                    // receiver such as `self<their<Map<K,V>>>`), Self is lowered to a
                    // pointer to the concrete instantiation so in-place mutations to
                    // `self` persist on the caller's object.
                    bool selfIsByRef = false;
                    std::string selfOwnershipKw;
                    if (!methodAST->params.empty() && methodAST->params[0].typeNode) {
                        if (auto* recvTn = dynamic_cast<ast::TypeName*>(methodAST->params[0].typeNode.get())) {
                            if (recvTn->identifier) {
                                const std::string& kw = recvTn->identifier->name;
                                if (kw == "their" || kw == "my" || kw == "our" ||
                                    kw == "mild" || kw == "borrow" || kw == "view") {
                                    selfIsByRef = true;
                                    selfOwnershipKw = kw;
                                }
                            }
                        }
                    }
                    llvm::Type* selfType = resolveTypeForMonomorphization(concretePattern, typeSubstitutions);
                    if (!selfType) {
                        logError(SourceLocation(), "Failed to resolve Self type for " + concreteType);
                        m_currentImplTypeNode = origImplTypeNode;
                        m_currentImplTraitName = origImplTraitName;
                        return nullptr;
                    }
                    if (selfIsByRef) {
                        selfType = llvm::PointerType::get(*context, 0);
                    }
                    paramTypes.push_back(selfType);

                    // Add remaining parameters. The receiver is always the first
                    // source-level parameter for aspect/bind methods and is already
                    // represented by the concrete Self argument above.
                    std::vector<size_t> nonReceiverParamIndices;
                    for (size_t i = 1; i < methodAST->params.size(); ++i) {
                        const auto& param = methodAST->params[i];

                        // Substitute type parameters in this parameter's type
                        llvm::Type* paramType = resolveParameterTypeWithSubstitution(
                            param.typeNode.get(), typeSubstitutions);
                        if (!paramType) {
                            logError(SourceLocation(), "Failed to resolve parameter type for " + param.name->name);
                            m_currentImplTypeNode = origImplTypeNode;
                            m_currentImplTraitName = origImplTraitName;
                            return nullptr;
                        }
                        paramTypes.push_back(paramType);
                        nonReceiverParamIndices.push_back(i);
                    }

                    // Determine return type with substitution
                    llvm::Type* returnType = llvm::Type::getVoidTy(*context);
                    if (methodAST->returnTypeNode) {
                        returnType = resolveReturnTypeWithSubstitution(
                            methodAST->returnTypeNode.get(), typeSubstitutions);
                        if (!returnType) {
                            logError(SourceLocation(), "Failed to resolve return type");
                            m_currentImplTypeNode = origImplTypeNode;
                            m_currentImplTraitName = origImplTraitName;
                            return nullptr;
                        }
                    }

                    // Create the specialized function
                    llvm::FunctionType* funcType = llvm::FunctionType::get(returnType, paramTypes, false);
                    llvm::Function* specializedFunc = llvm::Function::Create(
                        funcType,
                        llvm::Function::ExternalLinkage,
                        specializedName,
                        module.get()
                    );

                                        // Cache it before generating body
                    monomorphizedMethods[cacheKey] = specializedFunc;

                    // Generate the function body with type substitution active.
                    // Monomorphized bind methods need the same active function and
                    // impl context as normal bind methods so return statements, Self,
                    // and associated type references resolve against the specialized
                    // function instead of the caller currently being generated.

                    auto savedTypeSubstitutions = currentTypeSubstitutions;
                    auto savedNamedValues = namedValues;
                    llvm::Function* savedFunction = currentFunction;
                    ast::FunctionDeclaration* savedFunctionAST = currentFunctionAST;
                    ast::TypeNode* savedImplTypeNode = m_currentImplTypeNode;
                    std::string savedImplTraitName = m_currentImplTraitName;
                    llvm::BasicBlock* savedInsertBlock = builder->GetInsertBlock();
                    llvm::BasicBlock::iterator savedInsertPoint = builder->GetInsertPoint();

                    currentTypeSubstitutions = typeSubstitutions;
                    currentFunction = specializedFunc;
                    currentFunctionAST = methodAST;
                    m_currentImplTypeNode = currentImplConcreteTypeNode.get();
                    m_currentImplTraitName = traitName;

                    llvm::BasicBlock* entry = llvm::BasicBlock::Create(*context, "entry", specializedFunc);
                    builder->SetInsertPoint(entry);
                    // Isolate this monomorphized bind method's scope tracking from
                    // the caller's: a method body with early `return`s inside loops
                    // (common in by-ref collection methods) can over-pop the shared
                    // scope stack otherwise, corrupting the caller's declarations.
                    // Mirror the wholesale save/clear/restore used by
                    // monomorphizeFunction rather than the depth-guard that cannot
                    // recover from an underflow below the caller's baseline.
                    auto savedMethodScopeStack = std::move(scopeStack);
                    scopeStack.clear();
                    m_functionScopeBaseline = 0;
                    size_t savedScopeDepth = 0;
                    enterScope();
                    generatePushFrameCall(specializedName, methodAST->loc);

                    namedValues.clear();

                    size_t argIdx = 0;
                    for (auto& arg : specializedFunc->args()) {
                        if (argIdx == 0) {
                            arg.setName("self");
                            llvm::AllocaInst* alloca = llvm::dyn_cast_or_null<llvm::AllocaInst>(
                                createEntryBlockAlloca(specializedFunc, "self", arg.getType()));
                            if (!alloca) {
                                logError(methodAST->loc, "Failed to create receiver alloca for monomorphized bind method");
                                if (scopeStack.size() > savedScopeDepth) exitScope();
                                namedValues = savedNamedValues;
                                currentTypeSubstitutions = savedTypeSubstitutions;
                                currentFunction = savedFunction;
                                currentFunctionAST = savedFunctionAST;
                                m_currentImplTypeNode = savedImplTypeNode;
                                m_currentImplTraitName = savedImplTraitName;
                                return nullptr;
                            }
                            builder->CreateStore(&arg, alloca);
                            namedValues["self"] = alloca;
                            // Register the receiver's type. A by-ref receiver keeps
                            // its ownership wrapper (e.g. `their<Map<String,Int>>`)
                            // so member access dereferences the pointer rather than
                            // treating the alloca as an inline struct value.
                            if (selfIsByRef) {
                                auto ownType = std::make_unique<ast::TypeName>(
                                    methodAST->loc,
                                    std::make_unique<ast::Identifier>(methodAST->loc, selfOwnershipKw));
                                ownType->genericArgs.push_back(currentImplConcreteTypeNode->clone());
                                valueTypeMap[alloca] = std::shared_ptr<ast::TypeNode>(ownType.release());
                            } else {
                                valueTypeMap[alloca] = std::shared_ptr<ast::TypeNode>(currentImplConcreteTypeNode->clone());
                            }
                        } else {
                            size_t paramListIdx = argIdx - 1;
                            if (paramListIdx < nonReceiverParamIndices.size()) {
                                const auto& param = methodAST->params[nonReceiverParamIndices[paramListIdx]];
                                arg.setName(param.name->name);
                                llvm::AllocaInst* alloca = llvm::dyn_cast_or_null<llvm::AllocaInst>(
                                    createEntryBlockAlloca(specializedFunc, param.name->name, arg.getType()));
                                if (!alloca) {
                                    logError(param.name->loc, "Failed to create parameter alloca for " + param.name->name);
                                    if (scopeStack.size() > savedScopeDepth) exitScope();
                                    namedValues = savedNamedValues;
                                    currentTypeSubstitutions = savedTypeSubstitutions;
                                    currentFunction = savedFunction;
                                    currentFunctionAST = savedFunctionAST;
                                    m_currentImplTypeNode = savedImplTypeNode;
                                    m_currentImplTraitName = savedImplTraitName;
                                    return nullptr;
                                }
                                builder->CreateStore(&arg, alloca);
                                namedValues[param.name->name] = alloca;
                                if (param.typeNode) {
                                    valueTypeMap[alloca] = std::shared_ptr<ast::TypeNode>(param.typeNode->clone());
                                }
                            }
                        }
                        argIdx++;
                    }

                    if (methodAST->body) {
                        methodAST->body->accept(*this);
                    }

                    if (!builder->GetInsertBlock()->getTerminator()) {
                        if (scopeStack.size() > savedScopeDepth) {
                            exitScope();
                        }
                        generatePopFrameCall();
                        if (returnType->isVoidTy()) {
                            builder->CreateRetVoid();
                        } else {
                            builder->CreateRet(llvm::Constant::getNullValue(returnType));
                        }
                    } else if (scopeStack.size() > savedScopeDepth) {
                        // Explicit returns clean up the active function scope. If a
                        // no-return body path left it active, restore only the scope
                        // introduced for this monomorphized method, not the caller's.
                        exitScope();
                    }

                    namedValues = savedNamedValues;
                    currentTypeSubstitutions = savedTypeSubstitutions;
                    currentFunction = savedFunction;
                    currentFunctionAST = savedFunctionAST;
                    m_currentImplTypeNode = savedImplTypeNode;
                    m_currentImplTraitName = savedImplTraitName;
                    if (savedInsertBlock) {
                        if (savedInsertPoint != savedInsertBlock->end()) {
                            builder->SetInsertPoint(savedInsertBlock, savedInsertPoint);
                        } else {
                            builder->SetInsertPoint(savedInsertBlock);
                        }
                    }
                    // Restore the caller's scope stack wholesale before returning to
                    // it, discarding any scopes this method's body left behind.
                    scopeStack = std::move(savedMethodScopeStack);

                    m_currentImplTypeNode = origImplTypeNode;
                    m_currentImplTraitName = origImplTraitName;
                    return specializedFunc;
                }
            }
        }
    }


    return nullptr;
}

// Helper: Resolve the concrete type for monomorphization (e.g., Box<Int> -> %struct.Box_Int)
llvm::Type* LLVMCodegen::resolveTypeForMonomorphization(const TypePattern& pattern,
                                                        const std::map<std::string, std::string>& substitutions) {
    std::string mangledName = pattern.toMangled();

    // Check if struct type already exists (without "struct." prefix - the name used in monomorphizeStruct)
    if (llvm::StructType* structType = llvm::StructType::getTypeByName(*context, mangledName)) {
        return structType;
    }

    // Also try with "struct." prefix for compatibility
    std::string structName = "struct." + mangledName;
    if (llvm::StructType* structType = llvm::StructType::getTypeByName(*context, structName)) {
        return structType;
    }

    // Built-in generic runtime types such as Vec<T> do not have named
    // StructType instances. Reconstruct an AST type and let the normal type
    // codegen path produce the canonical LLVM representation.
    auto concreteTypeNode = typePatternToTypeNode(pattern, SourceLocation());
    if (llvm::Type* resolvedType = codegenType(concreteTypeNode.get())) {
        return resolvedType;
    }

    // For now, return generic pointer if we can't find the struct.
    // In production, this should trigger struct monomorphization.
    return llvm::PointerType::get(llvm::Type::getInt8Ty(*context), 0);
}

// Helper: Resolve parameter type with substitution (e.g., T -> Int)
llvm::Type* LLVMCodegen::resolveParameterTypeWithSubstitution(vyb::ast::TypeNode* typeNode,
                                                              const std::map<std::string, std::string>& substitutions) {
    if (!typeNode) {
        return nullptr;
    }

    // Check if this is a type parameter that needs substitution
    if (auto typeName = dynamic_cast<ast::TypeName*>(typeNode)) {
        if (typeName->identifier) {
            const std::string& name = typeName->identifier->name;

            // Resolve Self::Item / <Trait>::Item against the associated-type
            // bindings of the current concrete type (and trait) first.
            if (m_currentImplTypeNode && !m_currentImplTraitName.empty()) {
                std::string typedName = typeName->toString();
                std::string assocName;
                if (typedName.rfind("Self::", 0) == 0) {
                    assocName = typedName.substr(6);
                } else if (typedName.rfind(m_currentImplTraitName + "::", 0) == 0) {
                    assocName = typedName.substr(m_currentImplTraitName.length() + 2);
                }
                if (!assocName.empty() && driver_.hasSemanticAnalyzer()) {
                    SemanticAnalyzer* semantic = driver_.getSemanticAnalyzer();
                    const std::string concreteType = m_currentImplTypeNode->toString();
                    // Concrete binds populate the semantic associated-type map directly.
                    std::string bound = semantic->resolveAssociatedTypeForType(concreteType, m_currentImplTraitName, typedName);
                    if (bound.empty()) {
                        // Generic binds: look up the matching generic impl's explicit
                        // associated-type assignment, then substitute its type params.
                        TypePattern concretePattern = TypePattern::parse(concreteType);
                        for (const auto& typeEntry : semantic->getGenericTraitImpls()) {
                            TypePattern tmplPattern = TypePattern::parse(typeEntry.first);
                            std::map<std::string, std::string> matchSubs;
                            if (!tmplPattern.matchesPattern(concretePattern, matchSubs)) continue;
                            auto traitIt = typeEntry.second.find(m_currentImplTraitName);
                            if (traitIt == typeEntry.second.end()) continue;
                            const GenericImplInfo* gii = traitIt->second.get();
                            if (!gii) continue;
                            auto assocIt = gii->associatedTypeBindings.find(assocName);
                            if (assocIt != gii->associatedTypeBindings.end() && assocIt->second) {
                                bound = assocIt->second->toString();
                                break;
                            }
                        }
                    }
                    if (!bound.empty()) {
                        // Substitute any remaining type parameters (e.g. Item = T -> Int).
                        auto replaceTokens = [](std::string s, const std::string& tok, const std::string& repl) {
                            if (tok.empty()) return s;
                            auto isIdChar = [](char c) {
                                return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
                            };
                            std::string out;
                            out.reserve(s.size());
                            for (size_t i = 0; i < s.size();) {
                                if (i + tok.size() <= s.size() && s.compare(i, tok.size(), tok) == 0 &&
                                    (i == 0 || !isIdChar(s[i - 1])) &&
                                    (i + tok.size() == s.size() || !isIdChar(s[i + tok.size()]))) {
                                    out += repl;
                                    i += tok.size();
                                } else {
                                    out += s[i];
                                    ++i;
                                }
                            }
                            return out;
                        };
                        for (const auto& kv : substitutions) {
                            bound = replaceTokens(bound, kv.first, kv.second);
                        }
                        auto concreteTypeNode = typePatternToTypeNode(TypePattern::parse(bound), typeNode->loc);
                        return codegenType(concreteTypeNode.get());
                    }
                }
            }

            // Check if it's a type parameter
            auto it = substitutions.find(name);
            if (it != substitutions.end()) {
                // Resolve the concrete type by creating a TypeName node
                auto concreteTypeNode = std::make_unique<ast::TypeName>(
                    SourceLocation(),
                    std::make_unique<ast::Identifier>(SourceLocation(), it->second)
                );
                return codegenType(concreteTypeNode.get());
            }

            // Recursively substitute generic arguments so a parameterized type
            // like Box<T> resolves to the concrete monomorphized struct Box_Int
            // instead of falling through to codegenType(Box<T>) -> Box_T.
            if (!typeName->genericArgs.empty()) {
                std::string substituted = typeName->toString();
                for (const auto& kv : substitutions) {
                    substituted = replaceTypeTokens(substituted, kv.first, kv.second);
                }
                auto concreteNode = typePatternToTypeNode(TypePattern::parse(substituted), typeName->loc);
                return codegenType(concreteNode.get());
            }
        }
    }

    // A function-typed parameter (`f<fn(T) -> T>`) references type parameters
    // in its signature. Resolve each constituent type with substitution so the
    // concrete monomorphized signature is built here, rather than falling
    // through to codegenType (which would see the abstract `T` before the
    // body's type-substitution scope is active).
    if (auto funcTypeNode = dynamic_cast<ast::FunctionType*>(typeNode)) {
        std::vector<llvm::Type*> paramLlvmTypes;
        for (const auto& pn : funcTypeNode->parameterTypes) {
            llvm::Type* pt = resolveParameterTypeWithSubstitution(pn.get(), substitutions);
            if (!pt) return nullptr;
            paramLlvmTypes.push_back(pt);
        }
        llvm::Type* returnLlvmType = llvm::Type::getVoidTy(*context);
        if (funcTypeNode->returnType) {
            returnLlvmType = resolveParameterTypeWithSubstitution(funcTypeNode->returnType.get(), substitutions);
            if (!returnLlvmType) return nullptr;
        }
        // A `fn` type is a closure value, not a bare function pointer.
        (void)returnLlvmType;
        (void)paramLlvmTypes;
        return getClosureStructType();
    }

    // Otherwise, use normal type resolution
    return codegenType(typeNode);
}

// Replace whole-word occurrences of `token` with `repl` in a type string, so a
// substitution like T -> Vec<Int> composes with surrounding generic syntax.
std::string LLVMCodegen::replaceTypeTokens(const std::string& s, const std::string& token, const std::string& repl) {
    if (token.empty()) return s;
    auto isIdChar = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    };
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if (i + token.size() <= s.size() && s.compare(i, token.size(), token) == 0 &&
            (i == 0 || !isIdChar(s[i - 1])) &&
            (i + token.size() == s.size() || !isIdChar(s[i + token.size()]))) {
            out += repl;
            i += token.size();
        } else {
            out += s[i];
            ++i;
        }
    }
    return out;
}

// Helper: Resolve return type with substitution
llvm::Type* LLVMCodegen::resolveReturnTypeWithSubstitution(vyb::ast::TypeNode* typeNode,
                                                           const std::map<std::string, std::string>& substitutions) {
    // Same logic as parameter resolution
    return resolveParameterTypeWithSubstitution(typeNode, substitutions);
}

} // namespace vyb
